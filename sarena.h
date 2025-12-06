#ifndef SARENA_H
#define SARENA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* -------------------- Config / Types -------------------- */

#define SARENA_MAGIC 0xC0FEBABE
#define SARENA_MIN_ALIGN 8u

typedef uint32_t offset_t; /* change to uint64_t for >4GB buffers */

/* forward */
typedef struct sarena sarena_t;

/* Block header stored in buffer before payload */
typedef struct block_header {
    uint32_t size;        /* total size of this block (including header) */
    uint8_t is_free;      /* 1 free, 0 allocated */
    uint8_t color;        /* 0 = BLACK, 1 = RED (for RB tree) */
    uint8_t pad[2];       /* padding to align to 4 bytes */

    offset_t left_off;    /* RB left child */
    offset_t right_off;   /* RB right child */
    offset_t parent_off;  /* RB parent */

    offset_t prev_phys_off; /* previous physical block (0 if none) */
    offset_t next_phys_off; /* next physical block (0 if none) */
} block_header_t;

/* Allocator control header at buffer start */
struct sarena {
    uint32_t magic;
    uint32_t capacity;
    offset_t free_root;   /* root of RB free tree (0 if none) */
    offset_t first_block; /* offset of first physical block header */
    uint8_t reserved[2];
};


/* -------------------- Public API -------------------- */

int sarena_init(void *buffer, size_t buffer_size);
void *sarena_alloc(sarena_t *a, size_t size);
void sarena_free(sarena_t *a, void *ptr);

#ifdef SARENA_DEBUG
#include <stdio.h>

/* Print a physical list of blocks (for debug) */
void dump_physical(sarena_t *a);

/* In-order traversal of RB tree to print node sizes and offsets */
void rb_inorder_print(sarena_t *a, block_header_t *n, int depth);

void dump_free_tree(sarena_t *a);

#endif // SARENA_DEBUG

#ifdef __cplusplus
}
#endif

#endif // SARENA_H
