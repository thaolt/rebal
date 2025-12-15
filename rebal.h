#ifndef REBAL_H
#define REBAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* -------------------- Config / Types -------------------- */

#define REBAL_MAGIC 0xC0FEBABE
#define REBAL_MIN_ALIGN 8u

typedef uint32_t rebal_offset_t; /* change to uint64_t for >4GB buffers */

/* forward */
typedef struct rebal rebal_t;

/* Block header stored in buffer before payload */
typedef struct rebal_block_header {
    uint32_t size;        /* total size of this block (including header) */
    uint8_t is_free;      /* 1 free, 0 allocated */
    uint8_t color;        /* 0 = BLACK, 1 = RED (for RB tree) */
    uint8_t pad[2];       /* padding to align to 4 bytes */

    rebal_offset_t left_off;    /* RB left child */
    rebal_offset_t right_off;   /* RB right child */
    rebal_offset_t parent_off;  /* RB parent */

    rebal_offset_t prev_phys_off; /* previous physical block (0 if none) */
    rebal_offset_t next_phys_off; /* next physical block (0 if none) */
} rebal_block_header_t;

/* Allocator control header at buffer start */
struct rebal {
    uint32_t magic;
    uint32_t capacity;
    rebal_offset_t free_root;   /* root of RB free tree (0 if none) */
    rebal_offset_t first_block; /* offset of first physical block header */
    uint8_t reserved[2];
};


/* -------------------- Public API -------------------- */

int rebal_init(void *buffer, size_t buffer_size);
void *rebal_alloc(rebal_t *a, size_t size);
void rebal_free(rebal_t *a, void *ptr);
void *rebal_realloc(rebal_t *a, void *ptr, size_t size);

#ifdef REBAL_DEBUG
#include <stdio.h>

/* Print a physical list of blocks (for debug) */
void dump_physical(rebal_t *a);

/* In-order traversal of RB tree to print node sizes and offsets */
void rb_inorder_print(rebal_t *a, rebal_block_header_t *n, int depth);

void dump_free_tree(rebal_t *a);

#endif // REBAL_DEBUG

#ifdef __cplusplus
}
#endif

#endif // REBAL_H
