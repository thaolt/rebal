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
#define REBAL_MAX_ALLOC_SIZE ((size_t)(1ULL << 30)) /* 1GB max allocation */

typedef uint32_t rebal_offset_t; /* change to uint64_t for >4GB buffers */

/* Error codes */
typedef enum {
    REBAL_SUCCESS = 0,
    REBAL_ERROR_NULL_BUFFER = -1,
    REBAL_ERROR_BUFFER_TOO_SMALL = -2,
    REBAL_ERROR_INVALID_ALIGNMENT = -3,
    REBAL_ERROR_CORRUPTED = -4,
    REBAL_ERROR_INVALID_POINTER = -5,
    REBAL_ERROR_DOUBLE_FREE = -6,
    REBAL_ERROR_OUT_OF_MEMORY = -7,
    REBAL_ERROR_SIZE_TOO_LARGE = -8,
    REBAL_ERROR_INVALID_STATE = -9
} rebal_error_t;

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
    uint32_t pad2;               /* padding to align to 32 bytes total */
} rebal_block_header_t;

/* Allocator control header at buffer start */
struct rebal {
    uint32_t magic;
    uint32_t capacity;
    rebal_offset_t free_root;   /* root of RB free tree (0 if none) */
    rebal_offset_t first_block; /* offset of first physical block header */
};


/* -------------------- Public API -------------------- */

/**
 * Initialize the allocator with a user-provided buffer.
 * @param buffer Pointer to the buffer to use for allocation
 * @param buffer_size Size of the buffer in bytes
 * @return REBAL_SUCCESS on success, error code on failure
 */
int rebal_init(void *buffer, size_t buffer_size);

/**
 * Allocate memory from the allocator.
 * @param a Pointer to the allocator
 * @param size Number of bytes to allocate
 * @return Pointer to allocated memory, or NULL on failure
 */
void *rebal_alloc(rebal_t *a, size_t size);

/**
 * Free previously allocated memory.
 * @param a Pointer to the allocator
 * @param ptr Pointer to memory to free
 */
void rebal_free(rebal_t *a, void *ptr);

/**
 * Reallocate memory to a new size.
 * @param a Pointer to the allocator
 * @param ptr Pointer to previously allocated memory (or NULL)
 * @param size New size in bytes
 * @return Pointer to reallocated memory, or NULL on failure
 */
void *rebal_realloc(rebal_t *a, void *ptr, size_t size);

/**
 * Validate the integrity of the allocator.
 * @param a Pointer to the allocator
 * @return REBAL_SUCCESS if valid, error code if corrupted
 */
int rebal_validate(rebal_t *a);

/**
 * Get allocator statistics.
 * @param a Pointer to the allocator
 * @param total_free Output parameter for total free bytes
 * @param total_allocated Output parameter for total allocated bytes
 * @param free_blocks Output parameter for number of free blocks
 * @return REBAL_SUCCESS on success, error code on failure
 */
int rebal_get_stats(rebal_t *a, size_t *total_free, size_t *total_allocated, 
                    size_t *free_blocks);

#ifdef REBAL_DEBUG
#include <stdio.h>

/* Print a physical list of blocks (for debug) */
void dump_physical(rebal_t *a);

/* In-order traversal of RB tree to print node sizes and offsets */
void rb_inorder_print(rebal_t *a, rebal_block_header_t *n, int depth);

void dump_free_tree(rebal_t *a);

#endif // REBAL_DEBUG

#ifdef __cplusplus
} // end extern C
#endif


#endif // REBAL_H
