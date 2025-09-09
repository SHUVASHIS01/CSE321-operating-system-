
#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <time.h>
#include <sys/types.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u

#pragma pack(push,1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_start;
    uint64_t inode_bitmap_blocks;
    uint64_t data_bitmap_start;
    uint64_t data_bitmap_blocks;
    uint64_t inode_table_start;
    uint64_t inode_table_blocks;
    uint64_t data_region_start;
    uint64_t data_region_blocks;
    uint64_t root_inode;
    uint64_t mtime_epoch;
    uint32_t flags;
    uint32_t checksum;
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    uint16_t mode;
    uint16_t links;
    uint32_t uid;
    uint32_t gid;
    uint64_t size_bytes;
    uint64_t atime;
    uint64_t mtime;
    uint64_t ctime;
    uint32_t direct[12];
    uint32_t reserved_0;
    uint32_t reserved_1;
    uint32_t reserved_2;
    uint32_t proj_id;
    uint32_t uid16_gid16;
    uint64_t xattr_ptr;
    uint64_t inode_crc;
} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t) == INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    uint32_t inode_no;
    uint8_t type;
    char name[58];
    uint8_t checksum;
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t) == 64, "dirent size mismatch");

// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================

static uint32_t CRC32_TAB[256];
static void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i;
        for (int j=0;j<8;j++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        CRC32_TAB[i] = c;
    }
}
static uint32_t crc32(const void *data, size_t n){
    const uint8_t *p = data; uint32_t c = 0xFFFFFFFFu;
    for (size_t i=0;i<n;i++) c = CRC32_TAB[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED

static uint32_t superblock_crc_finalize(superblock_t *sb){
    sb->checksum = 0;
    uint32_t s = crc32((void *)sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static void inode_crc_finalize(inode_t *ino){
    uint8_t tmp[INODE_SIZE];
    memcpy(tmp, ino, INODE_SIZE);
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static void dirent_checksum_finalize(dirent64_t *de){
    const uint8_t *p = (const uint8_t*)de;
    uint8_t x = 0;
    for (int i=0;i<63;i++) x ^= p[i];
    de->checksum = x;
}


static void die(const char *msg){ perror(msg); exit(EXIT_FAILURE); }
static void fseek_block(FILE *f, uint64_t blk){
    if (fseeko(f, (off_t)(blk * (uint64_t)BS), SEEK_SET) != 0) die("fseeko");
}
static void set_bit(uint8_t *bm, uint64_t idx){ bm[idx/8] |= (uint8_t)(1u << (idx%8)); }
//cli parsing[start]
typedef struct { const char *image; uint64_t size_kib; uint64_t inodes; uint32_t proj_id; } cli_t;
static void usage(const char *p){
    fprintf(stderr, "Usage: %s --image out.img --size-kib <180..4096, mult of 4> --inodes <128..512> [--proj-id N]\n", p);
    exit(EXIT_FAILURE);
}
static cli_t parse_cli(int argc, char **argv){
    cli_t c = {0,0,0,0};
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--image") && i+1<argc) c.image = argv[++i];
        else if (!strcmp(argv[i],"--size-kib") && i+1<argc) c.size_kib = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i],"--inodes") && i+1<argc) c.inodes = strtoull(argv[++i], NULL, 10);
        else if (!strcmp(argv[i],"--proj-id") && i+1<argc) c.proj_id = (uint32_t)strtoul(argv[++i], NULL, 10);
        else usage(argv[0]);
    }
    if (!c.image || c.size_kib < 180 || c.size_kib > 4096 || (c.size_kib % 4) != 0 || c.inodes < 128 || c.inodes > 512) usage(argv[0]);
    return c;
}
//cli end

//memory start
int main(int argc, char **argv){
    crc32_init();
    cli_t cli = parse_cli(argc, argv);

    uint64_t total_blocks = (cli.size_kib * 1024ull) / BS;
    uint64_t inode_tbl_bytes = cli.inodes * INODE_SIZE;
    uint64_t inode_table_blocks = (inode_tbl_bytes + BS - 1) / BS;

    uint64_t sb_start = 0;
    uint64_t ibm_start = 1;
    uint64_t dbm_start = 2;
    uint64_t it_start = 3;
    uint64_t it_blocks = inode_table_blocks;
    uint64_t data_start = it_start + it_blocks;
    if (data_start >= total_blocks){ fprintf(stderr,"Image too small\n"); return EXIT_FAILURE; }
    uint64_t data_blocks = total_blocks - data_start;
//memory end
    FILE *f = fopen(cli.image, "wb+"); if (!f) die("fopen");
    uint8_t zero[BS]; memset(zero,0,sizeof(zero));
    for (uint64_t i=0;i<total_blocks;i++){
        if (fwrite(zero,1,BS,f) != BS) die("write zero");
    }
    fflush(f);

    time_t now = time(NULL);

    superblock_t sb; memset(&sb,0,sizeof(sb));
    sb.magic = 0x4D565346u; /* "MVSF" */
    sb.version = 1;
    sb.block_size = BS;
    sb.total_blocks = total_blocks;
    sb.inode_count = cli.inodes;
    sb.inode_bitmap_start = ibm_start;
    sb.inode_bitmap_blocks = 1;
    sb.data_bitmap_start = dbm_start;
    sb.data_bitmap_blocks = 1;
    sb.inode_table_start = it_start;
    sb.inode_table_blocks = it_blocks;
    sb.data_region_start = data_start;
    sb.data_region_blocks = data_blocks;
    sb.root_inode = ROOT_INO;
    sb.mtime_epoch = (uint64_t)now;
    sb.flags = 0;
    superblock_crc_finalize(&sb);

    fseek_block(f, sb_start);
    if (fwrite(&sb,1,sizeof(sb),f) != sizeof(sb)) die("write sb");

    uint8_t ibm[BS]; memset(ibm,0,sizeof(ibm));
    uint8_t dbm[BS]; memset(dbm,0,sizeof(dbm));
    set_bit(ibm, 0); 
    set_bit(dbm, 0); 
    fseek_block(f, ibm_start); if (fwrite(ibm,1,BS,f) != BS) die("write ibm");
    fseek_block(f, dbm_start); if (fwrite(dbm,1,BS,f) != BS) die("write dbm");

    inode_t root; memset(&root,0,sizeof(root));
    root.mode = 0040000; 
    root.links = 2;
    root.uid = root.gid = 0;
    root.size_bytes = BS;
    root.atime = root.mtime = root.ctime = (uint64_t)now;
    root.direct[0] = (uint32_t)(sb.data_region_start + 0);
    root.proj_id = 7;
    inode_crc_finalize(&root);

    uint8_t itbuf[BS]; memset(itbuf,0,sizeof(itbuf));
    memcpy(itbuf, &root, sizeof(root));
    fseek_block(f, it_start);
    if (fwrite(itbuf,1,BS,f) != BS) die("write it block 0");
    for (uint64_t b=1; b<it_blocks; b++){
        fseek_block(f, it_start + b);
        if (fwrite(zero,1,BS,f) != BS) die("zero it block");
    }

    dirent64_t entries[BS / sizeof(dirent64_t)];
    memset(entries,0,sizeof(entries));
    entries[0].inode_no = 1; entries[0].type = 2; entries[0].name[0] = '.';
    dirent_checksum_finalize(&entries[0]);
    entries[1].inode_no = 1; entries[1].type = 2; entries[1].name[0] = '.'; entries[1].name[1] = '.';
    dirent_checksum_finalize(&entries[1]);

    fseek_block(f, root.direct[0]);
    if (fwrite(entries,1,sizeof(entries),f) != sizeof(entries)) die("write root dir");

    fclose(f);
    printf("Created MiniVSFS image: %s\n", cli.image);
    printf("Blocks: %" PRIu64 " | Inodes: %" PRIu64 " | Data blocks: %" PRIu64 "\n", total_blocks, cli.inodes, data_blocks);
    
    return 0;
}

