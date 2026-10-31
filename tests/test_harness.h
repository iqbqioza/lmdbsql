#ifndef LMDBSQL_TEST_HARNESS_H
#define LMDBSQL_TEST_HARNESS_H

#include "lmdbsql/lmdbsql.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int test_failures;

void test_begin(const char *name);
void test_failure(const char *file, int line, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#define TEST_CHECK(condition)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            test_failure(__FILE__, __LINE__, "CHECK failed: %s", #condition);                      \
        }                                                                                          \
    } while (0)

#define TEST_CHECK_MSG(condition, ...)                                                             \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            test_failure(__FILE__, __LINE__, __VA_ARGS__);                                         \
        }                                                                                          \
    } while (0)

#define TEST_CHECK_STATUS(expression, expected)                                                    \
    do {                                                                                           \
        lsql_status status_value = (expression);                                                   \
        if (status_value != (expected)) {                                                          \
            test_failure(__FILE__, __LINE__, "status %s, expected %s",                             \
                         lsql_status_name(status_value), lsql_status_name(expected));              \
        }                                                                                          \
    } while (0)

#define TEST_CHECK_INT64(actual, expected)                                                         \
    do {                                                                                           \
        int64_t actual_value = (actual);                                                           \
        int64_t expected_value = (expected);                                                       \
        if (actual_value != expected_value) {                                                      \
            test_failure(__FILE__, __LINE__, "%s = %lld, expected %lld", #actual,                  \
                         (long long)actual_value, (long long)expected_value);                      \
        }                                                                                          \
    } while (0)

#define TEST_CHECK_SIZE(actual, expected)                                                          \
    do {                                                                                           \
        size_t actual_value = (actual);                                                            \
        size_t expected_value = (expected);                                                        \
        if (actual_value != expected_value) {                                                      \
            test_failure(__FILE__, __LINE__, "%s = %zu, expected %zu", #actual, actual_value,      \
                         expected_value);                                                          \
        }                                                                                          \
    } while (0)

#define TEST_CHECK_STRING(actual, expected)                                                        \
    do {                                                                                           \
        const char *actual_value = (actual);                                                       \
        const char *expected_value = (expected);                                                   \
        if (actual_value == NULL || expected_value == NULL ||                                      \
            strcmp(actual_value, expected_value) != 0) {                                           \
            test_failure(__FILE__, __LINE__, "%s = %s, expected %s", #actual,                      \
                         actual_value == NULL ? "(null)" : actual_value,                           \
                         expected_value == NULL ? "(null)" : expected_value);                      \
        }                                                                                          \
    } while (0)

#define TEST_CHECK_REAL(actual, expected)                                                          \
    do {                                                                                           \
        double actual_value = (actual);                                                            \
        double expected_value = (expected);                                                        \
        double difference = actual_value - expected_value;                                         \
        if (difference < 0.0) {                                                                    \
            difference = -difference;                                                              \
        }                                                                                          \
        if (difference > 1e-9) {                                                                   \
            test_failure(__FILE__, __LINE__, "%s = %.17g, expected %.17g", #actual, actual_value,  \
                         expected_value);                                                          \
        }                                                                                          \
    } while (0)

char *test_db_create(void);
void test_db_remove(char *path);

lsql_result *test_sql(lsql_conn *conn, const char *sql);
lsql_status test_sql_error(lsql_conn *conn, const char *sql, lsql_error *error);

void run_arena_tests(void);
void run_lexer_tests(void);
void run_parser_tests(void);
void run_sql_tests(void);

#endif /* LMDBSQL_TEST_HARNESS_H */
