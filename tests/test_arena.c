#include "test_harness.h"

#include "internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void run_arena_tests(void) {
    lsql_arena arena;
    lsql_arena_init(&arena);

    const void *one = lsql_arena_alloc(&arena, 1);
    const void *sixteen = lsql_arena_alloc(&arena, 16);
    const unsigned char *large = lsql_arena_alloc(&arena, 10000);
    TEST_CHECK(one != NULL);
    TEST_CHECK(sixteen != NULL);
    TEST_CHECK(large != NULL);

    const size_t alignment = _Alignof(max_align_t);
    TEST_CHECK(((uintptr_t)one % alignment) == 0);
    TEST_CHECK(((uintptr_t)sixteen % alignment) == 0);
    TEST_CHECK(((uintptr_t)large % alignment) == 0);

    bool zeroed = true;
    for (size_t index = 0; index < 10000; ++index) {
        if (large[index] != 0) {
            zeroed = false;
            break;
        }
    }
    TEST_CHECK(zeroed);

    const char *copy = lsql_arena_strndup(&arena, "hello world", 5);
    TEST_CHECK(copy != NULL);
    TEST_CHECK_STRING(copy, "hello");

    const char *full = lsql_arena_strdup(&arena, "full");
    TEST_CHECK(full != NULL);
    TEST_CHECK_STRING(full, "full");

    lsql_arena_destroy(&arena);

    lsql_arena_init(&arena);
    const void *after = lsql_arena_alloc(&arena, 8);
    TEST_CHECK(after != NULL);
    lsql_arena_destroy(&arena);
}
