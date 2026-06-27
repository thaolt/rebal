/* wasm/rebal_wasm.c
 *
 * Minimal WASM wrapper for the rebal allocator.
 * Built with clang --target=wasm32 and wasm-ld (no Emscripten).
 *
 * Memory layout managed by the linker:
 *   - data/BSS end at __heap_base
 *   - the rebal arena lives at __heap_base
 *   - a small stack reserve lives at the top of the linear memory
 */

#include "rebal.h"
#include <stdint.h>

#define STACK_RESERVE 2048
#define DEMO_ARENA_SIZE 10240
#define WASM_PAGE_SIZE 65536

static uintptr_t heap_base(void) {
    extern unsigned char __heap_base;
    return (uintptr_t)&__heap_base;
}

static uint32_t total_memory_size(void) {
    return (uint32_t)(__builtin_wasm_memory_size(0) * WASM_PAGE_SIZE);
}

__attribute__((used, visibility("default")))
int rebal_wasm_init(void) {
    uint32_t hb = (uint32_t)heap_base();
    uint32_t mem = total_memory_size();
    uint32_t arena_size = DEMO_ARENA_SIZE;
    if (hb + arena_size + STACK_RESERVE > mem) {
        return REBAL_ERROR_BUFFER_TOO_SMALL;
    }
    return rebal_init((void *)(uintptr_t)hb, arena_size);
}

__attribute__((used, visibility("default")))
uint32_t rebal_wasm_alloc(uint32_t size) {
    rebal_t *arena = (rebal_t *)heap_base();
    void *p = rebal_alloc(arena, size);
    return (uint32_t)(uintptr_t)p;
}

__attribute__((used, visibility("default")))
void rebal_wasm_free(uint32_t ptr) {
    rebal_t *arena = (rebal_t *)heap_base();
    rebal_free(arena, (void *)(uintptr_t)ptr);
}

__attribute__((used, visibility("default")))
uint32_t rebal_wasm_realloc(uint32_t ptr, uint32_t size) {
    rebal_t *arena = (rebal_t *)heap_base();
    void *p = rebal_realloc(arena, (void *)(uintptr_t)ptr, size);
    return (uint32_t)(uintptr_t)p;
}

__attribute__((used, visibility("default")))
int rebal_wasm_validate(void) {
    rebal_t *arena = (rebal_t *)heap_base();
    return rebal_validate(arena);
}

__attribute__((used, visibility("default")))
uint32_t rebal_wasm_get_stats_total_free(void) {
    rebal_t *arena = (rebal_t *)heap_base();
    size_t total_free = 0, total_allocated = 0, free_blocks = 0;
    if (rebal_get_stats(arena, &total_free, &total_allocated, &free_blocks) !=
        REBAL_SUCCESS) {
        return 0;
    }
    return (uint32_t)total_free;
}

__attribute__((used, visibility("default")))
uint32_t rebal_wasm_get_stats_total_allocated(void) {
    rebal_t *arena = (rebal_t *)heap_base();
    size_t total_free = 0, total_allocated = 0, free_blocks = 0;
    if (rebal_get_stats(arena, &total_free, &total_allocated, &free_blocks) !=
        REBAL_SUCCESS) {
        return 0;
    }
    return (uint32_t)total_allocated;
}

__attribute__((used, visibility("default")))
uint32_t rebal_wasm_get_stats_free_blocks(void) {
    rebal_t *arena = (rebal_t *)heap_base();
    size_t total_free = 0, total_allocated = 0, free_blocks = 0;
    if (rebal_get_stats(arena, &total_free, &total_allocated, &free_blocks) !=
        REBAL_SUCCESS) {
        return 0;
    }
    return (uint32_t)free_blocks;
}
