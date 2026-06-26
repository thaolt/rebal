#include "rebal.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* Test framework */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_START(name) \
    printf("Running test: %s... ", name); \
    tests_run++;

#define TEST_PASS() \
    tests_passed++; \
    printf("PASSED\n");

#define TEST_FAIL(msg) \
    tests_failed++; \
    printf("FAILED: %s\n", msg);

#define ASSERT_TRUE(cond) \
    if (!(cond)) { \
        TEST_FAIL("Assertion failed: " #cond); \
        return; \
    }

#define ASSERT_FALSE(cond) \
    if (cond) { \
        TEST_FAIL("Assertion failed: !" #cond); \
        return; \
    }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        printf("FAILED: %s != %s (%lld != %lld)\n", #a, #b, (long long)(a), (long long)(b)); \
        tests_failed++; \
        return; \
    }

#define ASSERT_NEQ(a, b) \
    if ((a) == (b)) { \
        printf("FAILED: %s == %s (%lld == %lld)\n", #a, #b, (long long)(a), (long long)(b)); \
        tests_failed++; \
        return; \
    }

#define ASSERT_NULL(ptr) \
    if ((ptr) != NULL) { \
        TEST_FAIL("Expected NULL"); \
        return; \
    }

#define ASSERT_NOT_NULL(ptr) \
    if ((ptr) == NULL) { \
        TEST_FAIL("Expected non-NULL"); \
        return; \
    }

/* Test buffer — must be aligned to REBAL_MIN_ALIGN */
static _Alignas(REBAL_MIN_ALIGN) uint8_t test_buffer[65536];

void test_init_null_buffer(void) {
    TEST_START("init_null_buffer");
    int rc = rebal_init(NULL, 1024);
    ASSERT_EQ(rc, REBAL_ERROR_NULL_BUFFER);
    TEST_PASS();
}

void test_init_small_buffer(void) {
    TEST_START("init_small_buffer");
    uint8_t tiny_buffer[10];
    int rc = rebal_init(tiny_buffer, sizeof(tiny_buffer));
    ASSERT_EQ(rc, REBAL_ERROR_BUFFER_TOO_SMALL);
    TEST_PASS();
}

void test_init_success(void) {
    TEST_START("init_success");
    int rc = rebal_init(test_buffer, sizeof(test_buffer));
    ASSERT_EQ(rc, REBAL_SUCCESS);
    TEST_PASS();
}

void test_alloc_null_allocator(void) {
    TEST_START("alloc_null_allocator");
    void *ptr = rebal_alloc(NULL, 100);
    ASSERT_NULL(ptr);
    TEST_PASS();
}

void test_alloc_zero_size(void) {
    TEST_START("alloc_zero_size");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    void *ptr = rebal_alloc(a, 0);
    ASSERT_NULL(ptr);
    TEST_PASS();
}

void test_alloc_basic(void) {
    TEST_START("alloc_basic");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    void *ptr = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(ptr);
    TEST_PASS();
}

void test_alloc_large_size(void) {
    TEST_START("alloc_large_size");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    void *ptr = rebal_alloc(a, REBAL_MAX_ALLOC_SIZE + 1);
    ASSERT_NULL(ptr);
    TEST_PASS();
}

void test_free_null_allocator(void) {
    TEST_START("free_null_allocator");
    rebal_free(NULL, test_buffer);
    TEST_PASS();
}

void test_free_null_pointer(void) {
    TEST_START("free_null_pointer");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    rebal_free(a, NULL);
    TEST_PASS();
}

void test_alloc_free_cycle(void) {
    TEST_START("alloc_free_cycle");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    void *ptr1 = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(ptr1);
    
    rebal_free(a, ptr1);
    
    void *ptr2 = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(ptr2);
    
    rebal_free(a, ptr2);
    TEST_PASS();
}

void test_double_free(void) {
    TEST_START("double_free");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    void *ptr = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(ptr);
    
    rebal_free(a, ptr);
    rebal_free(a, ptr); /* Should not crash */
    TEST_PASS();
}

void test_multiple_allocations(void) {
    TEST_START("multiple_allocations");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = rebal_alloc(a, 100);
        ASSERT_NOT_NULL(ptrs[i]);
    }
    
    for (int i = 0; i < 10; i++) {
        rebal_free(a, ptrs[i]);
    }
    TEST_PASS();
}

void test_realloc_null_ptr(void) {
    TEST_START("realloc_null_ptr");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    void *ptr = rebal_realloc(a, NULL, 100);
    ASSERT_NOT_NULL(ptr);
    rebal_free(a, ptr);
    TEST_PASS();
}

void test_realloc_zero_size(void) {
    TEST_START("realloc_zero_size");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    void *ptr = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(ptr);
    
    void *result = rebal_realloc(a, ptr, 0);
    ASSERT_NULL(result);
    TEST_PASS();
}

void test_realloc_grow(void) {
    TEST_START("realloc_grow");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    void *ptr = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(ptr);
    memset(ptr, 0xAA, 100);
    
    void *new_ptr = rebal_realloc(a, ptr, 200);
    ASSERT_NOT_NULL(new_ptr);
    
    /* Check data preservation */
    uint8_t *data = (uint8_t *)new_ptr;
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(data[i], 0xAA);
    }
    
    rebal_free(a, new_ptr);
    TEST_PASS();
}

void test_realloc_shrink(void) {
    TEST_START("realloc_shrink");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    void *ptr = rebal_alloc(a, 200);
    ASSERT_NOT_NULL(ptr);
    memset(ptr, 0xBB, 200);
    
    void *new_ptr = rebal_realloc(a, ptr, 100);
    ASSERT_NOT_NULL(new_ptr);
    
    /* Check data preservation */
    uint8_t *data = (uint8_t *)new_ptr;
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(data[i], 0xBB);
    }
    
    rebal_free(a, new_ptr);
    TEST_PASS();
}

void test_realloc_same_size(void) {
    TEST_START("realloc_same_size");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    void *ptr = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(ptr);
    
    void *new_ptr = rebal_realloc(a, ptr, 100);
    ASSERT_EQ(ptr, new_ptr); /* Should return same pointer */
    
    rebal_free(a, new_ptr);
    TEST_PASS();
}

void test_validate_corrupted_allocator(void) {
    TEST_START("validate_corrupted_allocator");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    /* Corrupt the magic number */
    a->magic = 0x12345678;
    
    int rc = rebal_validate(a);
    ASSERT_EQ(rc, REBAL_ERROR_CORRUPTED);
    TEST_PASS();
}

void test_validate_structural_corruption(void) {
    TEST_START("validate_structural_corruption");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;

    void *p1 = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(p1);
    void *p2 = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(p2);

    /* Corrupt p2's next_phys_off to create a non-adjacent link */
    rebal_block_header_t *b2 = (rebal_block_header_t *)((uintptr_t)p2 - sizeof(rebal_block_header_t));
    b2->next_phys_off = 8; /* bogus offset */

    int rc = rebal_validate(a);
    ASSERT_EQ(rc, REBAL_ERROR_CORRUPTED);
    TEST_PASS();
}

void test_get_stats(void) {
    TEST_START("get_stats");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    size_t total_free, total_allocated, free_blocks;
    int rc = rebal_get_stats(a, &total_free, &total_allocated, &free_blocks);
    ASSERT_EQ(rc, REBAL_SUCCESS);
    ASSERT_TRUE(total_free > 0);
    ASSERT_EQ(total_allocated, 0);
    ASSERT_EQ(free_blocks, 1);
    
    void *ptr = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(ptr);
    
    rc = rebal_get_stats(a, &total_free, &total_allocated, &free_blocks);
    ASSERT_EQ(rc, REBAL_SUCCESS);
    ASSERT_TRUE(total_allocated > 0);
    
    rebal_free(a, ptr);
    TEST_PASS();
}

/* Test that best-fit search works correctly after the rb_insert fix.
 * Alloc blocks of varying sizes, free them all, then re-alloc varying sizes
 * and verify the allocator doesn't spuriously run out of memory. */
void test_best_fit_varying_sizes(void) {
    TEST_START("best_fit_varying_sizes");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;

    /* Allocate blocks of different sizes to populate the tree with
     * distinct size keys and distinct offsets. */
    void *ptrs[16];
    size_t sizes[16] = {32, 64, 96, 128, 160, 192, 224, 256,
                        288, 320, 352, 384, 416, 448, 480, 512};
    for (int i = 0; i < 16; i++) {
        ptrs[i] = rebal_alloc(a, sizes[i]);
        ASSERT_NOT_NULL(ptrs[i]);
    }
    /* Free all — tree should contain 16 free blocks of different sizes */
    for (int i = 0; i < 16; i++) {
        rebal_free(a, ptrs[i]);
    }
    /* Validate after heavy tree churn */
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);

    /* Re-alloc in a different order to exercise best-fit search */
    for (int i = 15; i >= 0; i--) {
        void *p = rebal_alloc(a, sizes[i]);
        ASSERT_NOT_NULL(p);
        rebal_free(a, p);
    }
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);
    TEST_PASS();
}

/* Test that realloc grow-in-place works correctly when the next block is free,
 * and that the tree stays consistent (the coalesce-before-insert fix). */
void test_realloc_grow_into_free(void) {
    TEST_START("realloc_grow_into_free");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;

    void *p1 = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(p1);
    void *p2 = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(p2);
    void *p3 = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(p3);

    /* Free p2 so p1 can grow into it */
    rebal_free(a, p2);
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);

    /* Grow p1 — should merge with p2's free block in place */
    void *new_p1 = rebal_realloc(a, p1, 180);
    ASSERT_NOT_NULL(new_p1);
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);

    rebal_free(a, new_p1);
    rebal_free(a, p3);
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);
    TEST_PASS();
}

/* Test that realloc shrink creates a free remainder and the tree stays valid */
void test_realloc_shrink_creates_free(void) {
    TEST_START("realloc_shrink_creates_free");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;

    void *p1 = rebal_alloc(a, 300);
    ASSERT_NOT_NULL(p1);
    memset(p1, 0xCC, 300);

    void *new_p1 = rebal_realloc(a, p1, 100);
    ASSERT_NOT_NULL(new_p1);
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);

    /* Verify data preserved */
    uint8_t *data = (uint8_t *)new_p1;
    for (int i = 0; i < 100; i++) {
        ASSERT_EQ(data[i], 0xCC);
    }

    /* The remainder should be usable */
    void *p2 = rebal_alloc(a, 150);
    ASSERT_NOT_NULL(p2);
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);

    rebal_free(a, new_p1);
    rebal_free(a, p2);
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);
    TEST_PASS();
}

/* Test that freeing an invalid pointer (middle of an allocation) is rejected */
void test_free_invalid_pointer_middle(void) {
    TEST_START("free_invalid_pointer_middle");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;

    void *p = rebal_alloc(a, 200);
    ASSERT_NOT_NULL(p);

    /* Try to free a pointer in the middle of the allocation —
     * it won't have a valid block magic, so validate_block should reject it */
    void *mid = (void *)((uintptr_t)p + 50);
    rebal_free(a, mid);

    /* Arena should still be valid */
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);
    rebal_free(a, p);
    TEST_PASS();
}

/* Test that freeing a forged pointer (random memory) is rejected */
void test_free_forged_pointer(void) {
    TEST_START("free_forged_pointer");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;

    /* A stack variable that's clearly not from the arena */
    uint8_t dummy[64];
    rebal_free(a, (void *)dummy);

    /* Arena should still be valid */
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);
    TEST_PASS();
}

/* Test that double-free leaves the arena in a valid state */
void test_double_free_valid_state(void) {
    TEST_START("double_free_valid_state");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;

    void *p = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(p);
    rebal_free(a, p);
    rebal_free(a, p); /* double free — should be a no-op */

    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);

    /* Allocator should still be usable */
    void *p2 = rebal_alloc(a, 100);
    ASSERT_NOT_NULL(p2);
    rebal_free(a, p2);
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);
    TEST_PASS();
}

/* Test that init rejects unaligned buffers */
void test_init_unaligned_buffer(void) {
    TEST_START("init_unaligned_buffer");
    static uint8_t buf[1024 + 8];
    /* Use an address that's 1 byte off from REBAL_MIN_ALIGN */
    void *unaligned = (void *)((uintptr_t)buf + 1);
    int rc = rebal_init(unaligned, 1024);
    ASSERT_EQ(rc, REBAL_ERROR_INVALID_ALIGNMENT);
    TEST_PASS();
}

/* Stress/fuzz test: random alloc/free/realloc sequences, validate after each */
void test_stress_fuzz(void) {
    TEST_START("stress_fuzz");
    static _Alignas(REBAL_MIN_ALIGN) uint8_t buf[65536];
    rebal_init(buf, sizeof(buf));
    rebal_t *a = (rebal_t *)buf;

    srand(12345); /* deterministic seed */
    void *ptrs[32];
    size_t sizes[32];
    for (int i = 0; i < 32; i++) { ptrs[i] = NULL; sizes[i] = 0; }

    for (int iter = 0; iter < 5000; iter++) {
        int idx = rand() % 32;
        int action = rand() % 3;

        if (action == 0) {
            /* alloc */
            if (ptrs[idx] == NULL) {
                size_t sz = (size_t)(rand() % 500 + 1);
                ptrs[idx] = rebal_alloc(a, sz);
                if (ptrs[idx]) {
                    sizes[idx] = sz;
                    memset(ptrs[idx], (idx & 0xFF), sz);
                }
            }
        } else if (action == 1) {
            /* free */
            if (ptrs[idx]) {
                rebal_free(a, ptrs[idx]);
                ptrs[idx] = NULL;
                sizes[idx] = 0;
            }
        } else {
            /* realloc */
            if (ptrs[idx]) {
                size_t old_sz = sizes[idx];
                size_t new_sz = (size_t)(rand() % 500 + 1);
                void *np = rebal_realloc(a, ptrs[idx], new_sz);
                if (np) {
                    /* verify preserved data (min of old/new) */
                    size_t check = (old_sz < new_sz) ? old_sz : new_sz;
                    uint8_t *d = (uint8_t *)np;
                    for (size_t i = 0; i < check; i++) {
                        ASSERT_EQ(d[i], (uint8_t)(idx & 0xFF));
                    }
                    ptrs[idx] = np;
                    sizes[idx] = new_sz;
                    /* fill new region if grown */
                    if (new_sz > old_sz) {
                        memset((void *)((uintptr_t)np + old_sz), (idx & 0xFF), new_sz - old_sz);
                    }
                }
            }
        }

        /* Validate every 100 iterations to catch corruption early */
        if (iter % 100 == 0) {
            ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);
        }
    }

    /* Clean up and final validation */
    for (int i = 0; i < 32; i++) {
        if (ptrs[i]) rebal_free(a, ptrs[i]);
    }
    ASSERT_EQ(rebal_validate(a), REBAL_SUCCESS);

    /* After freeing everything, there should be exactly 1 free block */
    size_t tf, ta, fb;
    ASSERT_EQ(rebal_get_stats(a, &tf, &ta, &fb), REBAL_SUCCESS);
    ASSERT_EQ(fb, 1);
    ASSERT_EQ(ta, 0);
    TEST_PASS();
}

void test_fragmentation(void) {
    TEST_START("fragmentation");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    /* Allocate and free in a pattern that causes fragmentation */
    void *ptrs[20];
    for (int i = 0; i < 20; i++) {
        ptrs[i] = rebal_alloc(a, 50);
        ASSERT_NOT_NULL(ptrs[i]);
    }
    
    /* Free every other allocation */
    for (int i = 0; i < 20; i += 2) {
        rebal_free(a, ptrs[i]);
    }
    
    /* Allocate larger blocks - should coalesce */
    void *large = rebal_alloc(a, 200);
    ASSERT_NOT_NULL(large);
    
    /* Clean up */
    for (int i = 1; i < 20; i += 2) {
        rebal_free(a, ptrs[i]);
    }
    rebal_free(a, large);
    TEST_PASS();
}

void test_alignment(void) {
    TEST_START("alignment");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    /* Allocate various sizes and check alignment */
    for (size_t size = 1; size <= 100; size++) {
        void *ptr = rebal_alloc(a, size);
        ASSERT_NOT_NULL(ptr);
        ASSERT_EQ((uintptr_t)ptr % REBAL_MIN_ALIGN, 0);
        rebal_free(a, ptr);
    }
    TEST_PASS();
}

void test_out_of_memory(void) {
    TEST_START("out_of_memory");
    _Alignas(REBAL_MIN_ALIGN) uint8_t small_buffer[1024];
    rebal_init(small_buffer, sizeof(small_buffer));
    rebal_t *a = (rebal_t *)small_buffer;
    
    /* Try to allocate more than available */
    void *ptr = rebal_alloc(a, 10000);
    ASSERT_NULL(ptr);
    TEST_PASS();
}

void test_memory_write_read(void) {
    TEST_START("memory_write_read");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    void *ptr = rebal_alloc(a, 256);
    ASSERT_NOT_NULL(ptr);
    
    /* Write pattern */
    uint8_t *data = (uint8_t *)ptr;
    for (int i = 0; i < 256; i++) {
        data[i] = (uint8_t)(i & 0xFF);
    }
    
    /* Read and verify */
    for (int i = 0; i < 256; i++) {
        ASSERT_EQ(data[i], (uint8_t)(i & 0xFF));
    }
    
    rebal_free(a, ptr);
    TEST_PASS();
}

int main(void) {
    printf("=== REBAL Test Suite ===\n\n");
    
    /* Initialization tests */
    test_init_null_buffer();
    test_init_small_buffer();
    test_init_success();
    test_init_unaligned_buffer();

    /* Allocation tests */
    test_alloc_null_allocator();
    test_alloc_zero_size();
    test_alloc_basic();
    test_alloc_large_size();
    test_best_fit_varying_sizes();

    /* Free tests */
    test_free_null_allocator();
    test_free_null_pointer();
    test_alloc_free_cycle();
    test_double_free();
    test_double_free_valid_state();
    test_free_invalid_pointer_middle();
    test_free_forged_pointer();

    /* Multiple allocations */
    test_multiple_allocations();

    /* Realloc tests */
    test_realloc_null_ptr();
    test_realloc_zero_size();
    test_realloc_grow();
    test_realloc_shrink();
    test_realloc_same_size();
    test_realloc_grow_into_free();
    test_realloc_shrink_creates_free();

    /* Validation tests */
    test_validate_corrupted_allocator();
    test_validate_structural_corruption();

    /* Statistics tests */
    test_get_stats();

    /* Stress tests */
    test_fragmentation();
    test_alignment();
    test_out_of_memory();
    test_memory_write_read();
    test_stress_fuzz();
    
    printf("\n=== Test Results ===\n");
    printf("Total tests: %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("\nAll tests passed!\n");
        return 0;
    } else {
        printf("\nSome tests failed!\n");
        return 1;
    }
}