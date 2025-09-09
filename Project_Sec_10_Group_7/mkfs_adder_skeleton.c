#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define BS 4096u
#define INODE_SIZE 128u
#define ROOT_INO 1u
#define DIRECT_MAX 12
#define MINI_VSFS_MAGIC 0x4D565346u

#pragma pack(push,1)
typedef struct {
    uint32_t magic, version, block_size;
    uint64_t total_blocks;
    uint64_t inode_count;
    uint64_t inode_bitmap_start, inode_bitmap_blocks;
    uint64_t data_bitmap_start,  data_bitmap_blocks;
    uint64_t inode_table_start,  inode_table_blocks;
    uint64_t data_region_start,  data_region_blocks;
    uint64_t root_inode, mtime_epoch;
    uint32_t flags;
    uint32_t checksum;
} superblock_t;
#pragma pack(pop)
_Static_assert(sizeof(superblock_t) == 116, "superblock must fit in one block");

#pragma pack(push,1)
typedef struct {
    uint16_t mode, links;
    uint32_t uid, gid;
    uint64_t size_bytes;
    uint64_t atime, mtime, ctime;
    uint32_t direct[12];
    uint32_t reserved_0, reserved_1, reserved_2;
    uint32_t proj_id, uid16_gid16;
    uint64_t xattr_ptr;
    uint64_t inode_crc;
} inode_t;
#pragma pack(pop)
_Static_assert(sizeof(inode_t)==INODE_SIZE, "inode size mismatch");

#pragma pack(push,1)
typedef struct {
    uint32_t inode_no;
    uint8_t type;
    char name[58];
    uint8_t checksum;
} dirent64_t;
#pragma pack(pop)
_Static_assert(sizeof(dirent64_t)==64, "dirent size mismatch");

// ==========================DO NOT CHANGE THIS PORTION=========================
// These functions are there for your help. You should refer to the specifications to see how you can use them.
// ====================================CRC32====================================

uint32_t CRC32_TAB[256];
static void crc32_init(void){
    for (uint32_t i=0;i<256;i++){
        uint32_t c=i; for(int j=0;j<8;j++) c = (c&1)?(0xEDB88320u^(c>>1)):(c>>1);
        CRC32_TAB[i]=c;
    }
}
static uint32_t crc32(const void* data, size_t n){
    const uint8_t* p=(const uint8_t*)data; uint32_t c=0xFFFFFFFFu;
    for(size_t i=0;i<n;i++) c = CRC32_TAB[(c^p[i])&0xFF] ^ (c>>8);
    return c ^ 0xFFFFFFFFu;
}

// ====================================CRC32====================================

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static uint32_t superblock_crc_finalize(superblock_t *sb) {
    sb->checksum = 0;
    uint32_t s = crc32((void *)sb, BS - 4);
    sb->checksum = s;
    return s;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static void inode_crc_finalize(inode_t* ino){
    uint8_t tmp[INODE_SIZE]; memcpy(tmp, ino, INODE_SIZE);
    memset(&tmp[120], 0, 8);
    uint32_t c = crc32(tmp, 120);
    ino->inode_crc = (uint64_t)c;
}

// WARNING: CALL THIS ONLY AFTER ALL OTHER SUPERBLOCK ELEMENTS HAVE BEEN FINALIZED
static void dirent_checksum_finalize(dirent64_t* de) {
    const uint8_t* p = (const uint8_t*)de;
    uint8_t x = 0; for (int i = 0; i < 63; i++) x ^= p[i];
    de->checksum = x;
}

static void die(const char* msg){ 
perror(msg); 
exit(EXIT_FAILURE); 
}

static void fseek_block(FILE* f, uint64_t blk){ if (fseeko(f, (off_t)(blk * (uint64_t)BS), SEEK_SET) != 0) die("fseeko"); }
// BIT map {start}
static int bit_test(const uint8_t* bm, uint64_t idx){ return (bm[idx/8] >> (idx%8)) & 1u; }
static void set_bit(uint8_t* bm, uint64_t idx){ bm[idx/8] |= (uint8_t)(1u << (idx%8)); }
static uint64_t find_first_zero_bit(const uint8_t* bm, uint64_t limit_bits){
    for (uint64_t i=0;i<limit_bits;i++) if (!bit_test(bm,i)) return i;
    return (uint64_t)-1;
}
//Bit map {end}
//cli parsing[start]
typedef struct {
    const char* input;
    const char* output;
    char** files;
    int files_count;
    const char* single_file;
    int files_allocated; // if we allocated c.files in parse_cli 
} cli_t;

static void usage(const char* p){ 
fprintf(stderr, "Usage: %s --input in.img --output out.img --files f1 f2 ... OR --file single\n", p);
exit(EXIT_FAILURE); 
}

static cli_t parse_cli(int argc, char** argv){
    cli_t c={0};
    c.files_allocated = 0;
    for (int i=1;i<argc;i++){
        if (!strcmp(argv[i],"--input") && i+1<argc) c.input=argv[++i];
        else if (!strcmp(argv[i],"--output") && i+1<argc) c.output=argv[++i];
        else if (!strcmp(argv[i],"--files") && i+1<argc){
            c.files = &argv[i+1];
            // count files until next -- or end 
            while (i+1<argc && argv[i+1][0]!='-'){ i++; c.files_count++; }
        }
        else if (!strcmp(argv[i], "--file") && i + 1 < argc) {
            c.single_file = argv[++i];
            c.files_count = 1;
            c.files = malloc(sizeof(char*));
            if (!c.files) { perror("malloc"); exit(1); }
            c.files[0] = c.single_file;
            c.files_allocated = 1;
        }
        else usage(argv[0]);
    }
    if (!c.input || !c.output || !c.files || c.files_count==0) usage(argv[0]);
    return c;
}
//cli parsing {end}
int main(int argc, char** argv){
    crc32_init();
    cli_t cli = parse_cli(argc, argv);

    FILE* fi = fopen(cli.input, "rb"); if(!fi) die("open input");
    superblock_t sb;
    if (fread(&sb,1,sizeof(sb),fi)!=sizeof(sb)) { fprintf(stderr, "Error: read sb\n"); fclose(fi); return EXIT_FAILURE; }

    //Validate magic and block size
    if (sb.magic != 0x4D565346u || sb.block_size != BS){ fprintf(stderr,"Error: Invalid filesystem image (magic or block size mismatch)\n"); fclose(fi); return EXIT_FAILURE; }

    uint8_t ibm[BS], dbm[BS];
    fseek_block(fi, sb.inode_bitmap_start); if (fread(ibm,1,BS,fi)!=BS) die("read ibm");
    fseek_block(fi, sb.data_bitmap_start); if (fread(dbm,1,BS,fi)!=BS) die("read dbm");

    size_t it_bytes = (size_t)sb.inode_table_blocks * BS;
    inode_t* itab = malloc(it_bytes); if(!itab) die("malloc itab");
    fseek_block(fi, sb.inode_table_start); if (fread(itab,1,it_bytes,fi)!=it_bytes) die("read itab");

    dirent64_t* rootdir = malloc(BS); if(!rootdir) die("malloc rootdir");
    uint32_t root_data_abs = itab[0].direct[0];
    fseek_block(fi, root_data_abs); if (fread(rootdir,1,BS,fi)!=BS) die("read rootdir");

    FILE* fo = fopen(cli.output, "wb+"); if(!fo) die("open output");
    /* copy input image into output first */
    fseeko(fi,0,SEEK_SET);
    uint8_t tmp[BS];
    for (uint64_t b=0;b<sb.total_blocks;b++){
        if (fread(tmp,1,BS,fi) != BS) die("copy read");
        if (fwrite(tmp,1,BS,fo) != BS) die("copy write");
    }

    /* Process each file */
    for (int fidx=0; fidx<cli.files_count; fidx++){
        const char* filepath = cli.files[fidx];
        /* Extract basename */
        const char* slash = strrchr(filepath,'/');
        const char* fname = slash ? slash+1 : filepath;

        /* DUPLICATE CHECK (before allocations) */
        int exists = 0;
        for (int i = 0; i < (int)(BS / sizeof(dirent64_t)); i++) {
            if (rootdir[i].inode_no != 0 && strncmp(rootdir[i].name, fname, sizeof(rootdir[i].name)) == 0) {
                fprintf(stderr, "Error: File '%s' already exists in root directory\n", fname);
                exists = 1;
                break;
            }
        }
        if (exists) continue; /* skip this file */

        /* Now open and stat the host file */
        FILE* ff = fopen(filepath,"rb");
        if (!ff) { fprintf(stderr, "Error: Cannot open input file '%s': %s\n", filepath, strerror(errno)); continue; }
        struct stat st;
        if (stat(filepath,&st)!=0) { fprintf(stderr, "Error: stat failed for '%s': %s\n", filepath, strerror(errno)); fclose(ff); continue; }
        uint64_t fsize = (uint64_t)st.st_size;
        uint64_t blocks_needed = (fsize + BS - 1) / BS;
        if (blocks_needed > DIRECT_MAX) {
            fprintf(stderr,
                "Warning: file '%s' is too large for MiniVSFS. "
                "Only first %d blocks will be stored, the rest will be ignored.\n",
                filepath, DIRECT_MAX);
            blocks_needed = DIRECT_MAX;
        }

        /* Find a free inode (first-fit) */
        uint64_t free_idx = find_first_zero_bit(ibm, sb.inode_count);
        if (free_idx == (uint64_t)-1){ fprintf(stderr,"Error: No free inode\n"); fclose(ff); break; }
        uint32_t new_ino_no = (uint32_t)(free_idx + 1);
        set_bit(ibm, free_idx);

        /* Allocate data blocks (first-fit) */
        uint32_t direct[DIRECT_MAX]; memset(direct,0,sizeof(direct));
        uint64_t allocated = 0;
        for (uint64_t i=0;i<sb.data_region_blocks && allocated<blocks_needed;i++){
            if(!bit_test(dbm,i)){ set_bit(dbm,i); direct[allocated++]=(uint32_t)(sb.data_region_start+i); }
        }
        if(allocated<blocks_needed){ fprintf(stderr,"Error: Not enough data blocks for '%s'\n", filepath); fclose(ff); break; }

        /* Create inode */
        inode_t newi; memset(&newi,0,sizeof(newi));
        newi.mode = 0100000; newi.links = 1; newi.uid = newi.gid = 0;
        newi.size_bytes = fsize;
        newi.atime = newi.mtime = newi.ctime = (uint64_t)time(NULL);
        /* set proj_id to 7 per your request */
        newi.proj_id = 7;
        for (uint32_t i=0;i<allocated;i++) newi.direct[i] = direct[i];
        inode_crc_finalize(&newi);
// directory entry management{start radiah}
        /* Insert directory entry (find empty slot) */
        int placed = 0;
        for (int i=0;i<(int)(BS/sizeof(dirent64_t));i++){
            if(rootdir[i].inode_no==0){
                rootdir[i].inode_no=new_ino_no; rootdir[i].type=1;
                memset(rootdir[i].name,0,sizeof(rootdir[i].name));
                snprintf(rootdir[i].name,sizeof(rootdir[i].name),"%s",fname);
                dirent_checksum_finalize(&rootdir[i]);
                placed=1; break;
            }
        }
        // end {radiah}
        if(!placed){ fprintf(stderr,"Error: Root directory full\n"); fclose(ff); break; }

        /* Update root inode */
        itab[0].links += 1;
        itab[0].mtime = itab[0].ctime = (uint64_t)time(NULL);
        inode_crc_finalize(&itab[0]);

        /* Store new inode into table */
        itab[free_idx] = newi;

        /* Write file data blocks into output image */
        uint8_t buf[BS];
        fseeko(ff,0,SEEK_SET);
        for (uint64_t i=0;i<allocated;i++){
            size_t toread = (size_t)((i+1)*BS<=fsize?BS:(fsize-i*BS));
            if(toread){
                if (fread(buf,1,toread,ff) != toread) {
                    fprintf(stderr, "Error: reading '%s'\n", filepath);
                    fclose(ff);
                    goto writeback; /* ensure we still write back state we modified */
                }
            }
            if(toread<BS) memset(buf+toread,0,BS-toread);
            fseek_block(fo,direct[i]);
            if(fwrite(buf,1,BS,fo)!=BS){ fprintf(stderr,"Error: write data\n"); fclose(ff); goto writeback; }
        }
        fclose(ff);
        printf("Added '%s' as inode #%u\n", filepath, new_ino_no);
    }

writeback:
    /* write modified metadata back to output image */
    fseek_block(fo,sb.inode_bitmap_start); if(fwrite(ibm,1,BS,fo)!=BS) die("write ibm");
    fseek_block(fo,sb.data_bitmap_start); if(fwrite(dbm,1,BS,fo)!=BS) die("write dbm");
    fseek_block(fo,sb.inode_table_start); if(fwrite(itab,1,it_bytes,fo)!=it_bytes) die("write itab");
    fseek_block(fo,root_data_abs); if(fwrite(rootdir,1,BS,fo)!=BS) die("write rootdir");

    /* update superblock checksum and write */
    superblock_crc_finalize(&sb);
    fseek(fo, 0, SEEK_SET);
    if (fwrite(&sb,1,sizeof(sb),fo) != sizeof(sb)) die("write sb");

    fclose(fi);
    fclose(fo);
    free(itab);
    free(rootdir);
    if (cli.files_allocated) free(cli.files);

    printf("All files added successfully into %s\n",cli.output);
    return 0;
}

