#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <numbers>\n", argv[0]);
        return 1;
    }

    printf("Odd/Even status:\n");

    for (int idx = 1; idx < argc; idx++) {
        int value = atoi(argv[idx]);  // Convert string to int
        if (value % 2 == 0) {
            printf("%d is even\n", value);
        } else {
            printf("%d is odd\n", value);
        }
    }

    return 0;
}
