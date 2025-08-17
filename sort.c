#include <stdio.h>
#include <stdlib.h>

int descendingOrder(const void *a, const void *b) {
    return (*(int *)b - *(int *)a);  // Descending sort
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <numbers>\n", argv[0]);
        return 1;
    }

    int nums[argc - 1];
    for (int idx = 1; idx < argc; idx++) {
        nums[idx - 1] = atoi(argv[idx]);  // Convert ar store
    }

    qsort(nums, argc - 1, sizeof(int), descendingOrder);

    printf("Sorted array in descending order:\n");
    for (int i = 0; i < argc - 1; i++) {
        printf("%d ", nums[i]);
    }
    printf("\n");

    return 0;
}
