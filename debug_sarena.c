#include "sarena.h"

/* -------------------- Simple Test Main -------------------- */

int main(void) {
    /* small buffer for testing */
    static uint8_t buffer[2048];

    int rc = sarena_init(buffer, sizeof(buffer));
    if (rc != 0) {
        printf("allocator_init failed: %d\n", rc);
        return 1;
    }

    sarena_t *A = (sarena_t *)buffer;

    printf("Allocator initialized: capacity=%u free_root=%u first_block=%u\n",
           A->capacity, (unsigned)A->free_root, (unsigned)A->first_block);

    dump_physical(A);
    dump_free_tree(A);

    void *p1 = sarena_alloc(A, 64);
    printf("\nAllocated p1 (64): %p\n", p1);
    dump_physical(A);
    dump_free_tree(A);

    void *p2 = sarena_alloc(A, 120);
    printf("\nAllocated p2 (120): %p\n", p2);
    dump_physical(A);
    dump_free_tree(A);

    void *p3 = sarena_alloc(A, 40);
    printf("\nAllocated p3 (40): %p\n", p3);
    dump_physical(A);
    dump_free_tree(A);

    printf("\nFree p2\n");
    sarena_free(A, p2);
    dump_physical(A);
    dump_free_tree(A);

    printf("\nFree p1\n");
    sarena_free(A, p1);
    dump_physical(A);
    dump_free_tree(A);

    printf("\nFree p3\n");
    sarena_free(A, p3);
    dump_physical(A);
    dump_free_tree(A);

    /* allocate again to see reuse */
    void *p4 = sarena_alloc(A, 200);
    printf("\nAllocated p4 (200): %p\n", p4);
    dump_physical(A);
    dump_free_tree(A);

    return 0;
}
