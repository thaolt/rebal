#include "rebal.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

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

/* Test buffer */
static uint8_t test_buffer[65536];

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

void test_validate_corrupted_magic(void) {
    TEST_START("validate_corrupted_magic");
    rebal_init(test_buffer, sizeof(test_buffer));
    rebal_t *a = (rebal_t *)test_buffer;
    
    /* Corrupt the magic number */
    a->magic = 0x12345678;
    
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
    uint8_t small_buffer[1024];
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
    
    /* Allocation tests */
    test_alloc_null_allocator();
    test_alloc_zero_size();
    test_alloc_basic();
    test_alloc_large_size();
    
    /* Free tests */
    test_free_null_allocator();
    test_free_null_pointer();
    test_alloc_free_cycle();
    test_double_free();
    
    /* Multiple allocations */
    test_multiple_allocations();
    
    /* Realloc tests */
    test_realloc_null_ptr();
    test_realloc_zero_size();
    test_realloc_grow();
    test_realloc_shrink();
    test_realloc_same_size();
    
    /* Validation tests */
    test_validate_corrupted_allocator();
    test_validate_corrupted_magic();
    
    /* Statistics tests */
    test_get_stats();
    
    /* Stress tests */
    test_fragmentation();
    test_alignment();
    test_out_of_memory();
    test_memory_write_read();
    
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