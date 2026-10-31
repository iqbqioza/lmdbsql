#define _POSIX_C_SOURCE 200809L

#include "test_harness.h"

#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *current_test = "";

int test_failures = 0;

void test_begin(const char *name) {
    current_test = name;
    printf("[ RUN      ] %s\n", name);
}

void test_failure(const char *file, int line, const char *format, ...) {
    test_failures++;
    fprintf(stderr, "%s:%d: [%s] ", file, line, current_test);
    va_list arguments;
    va_start(arguments, format);
    vfprintf(stderr, format, arguments); // NOLINT(clang-analyzer-security.VAList)
    va_end(arguments);
    fputc('\n', stderr);
}

char *test_db_create(void) {
    const char *temporary = getenv("TMPDIR");
    if (temporary == NULL || temporary[0] == '\0') {
        temporary = "/tmp";
    }

    size_t length = strlen(temporary) + sizeof("/lmdbsql-test-XXXXXX");
    char *pattern = malloc(length);
    if (pattern == NULL) {
        return NULL;
    }
    snprintf(pattern, length, "%s/lmdbsql-test-XXXXXX", temporary);

    if (mkdtemp(pattern) == NULL) {
        free(pattern);
        return NULL;
    }
    return pattern;
}

static void remove_file(const char *directory, const char *name) {
    size_t length = strlen(directory) + 1u + strlen(name) + 1u;
    char *path = malloc(length);
    if (path == NULL) {
        return;
    }
    snprintf(path, length, "%s/%s", directory, name);
    (void)remove(path);
    free(path);
}

void test_db_remove(char *path) {
    if (path == NULL) {
        return;
    }
    remove_file(path, "data.mdb");
    remove_file(path, "lock.mdb");
    (void)rmdir(path);
    free(path);
}

lsql_result *test_sql(lsql_conn *conn, const char *sql) {
    lsql_error error;
    lsql_result *result = NULL;
    lsql_status status = lsql_execute(conn, sql, strlen(sql), &result, &error);
    if (status != LSQL_OK) {
        test_failure(__FILE__, __LINE__, "SQL failed: %s\n  %s", sql, error.message);
        return NULL;
    }
    return result;
}

lsql_status test_sql_error(lsql_conn *conn, const char *sql, lsql_error *error) {
    lsql_error local_error;
    lsql_error *actual_error = error == NULL ? &local_error : error;
    lsql_result *result = NULL;
    lsql_status status = lsql_execute(conn, sql, strlen(sql), &result, actual_error);
    lsql_result_free(result);
    return status;
}
