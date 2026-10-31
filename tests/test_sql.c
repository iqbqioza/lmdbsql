#include "test_harness.h"

#include <string.h>

static lsql_conn *open_database(const char *path, bool read_only) {
    lsql_options options;
    lsql_options_init(&options);
    options.read_only = read_only;

    lsql_error error;
    lsql_conn *conn = NULL;
    lsql_status status = lsql_open(path, &options, &conn, &error);
    TEST_CHECK_MSG(status == LSQL_OK, "open failed: %s", error.message);
    return conn;
}

static void check_int(const lsql_result *result, size_t row, size_t column, int64_t expected) {
    const lsql_value *value = lsql_result_value(result, row, column);
    TEST_CHECK_MSG(value != NULL && value->type == LSQL_TYPE_INTEGER,
                   "row %zu column %zu is not INTEGER", row, column);
    if (value != NULL && value->type == LSQL_TYPE_INTEGER) {
        TEST_CHECK_INT64(value->as.integer, expected);
    }
}

static void check_real(const lsql_result *result, size_t row, size_t column, double expected) {
    const lsql_value *value = lsql_result_value(result, row, column);
    TEST_CHECK_MSG(value != NULL && value->type == LSQL_TYPE_REAL, "row %zu column %zu is not REAL",
                   row, column);
    if (value != NULL && value->type == LSQL_TYPE_REAL) {
        TEST_CHECK_REAL(value->as.real, expected);
    }
}

static void check_text(const lsql_result *result, size_t row, size_t column, const char *expected) {
    const lsql_value *value = lsql_result_value(result, row, column);
    TEST_CHECK_MSG(value != NULL && value->type == LSQL_TYPE_TEXT, "row %zu column %zu is not TEXT",
                   row, column);
    if (value != NULL && value->type == LSQL_TYPE_TEXT) {
        size_t expected_length = strlen(expected);
        TEST_CHECK_SIZE(value->as.text.length, expected_length);
        TEST_CHECK(memcmp(value->as.text.data, expected, expected_length) == 0);
    }
}

static void check_null(const lsql_result *result, size_t row, size_t column) {
    const lsql_value *value = lsql_result_value(result, row, column);
    TEST_CHECK_MSG(value != NULL && value->type == LSQL_TYPE_NULL, "row %zu column %zu is not NULL",
                   row, column);
}

static void test_basic_crud(void) {
    char *path = test_db_create();
    TEST_CHECK(path != NULL);
    if (path == NULL) {
        return;
    }

    lsql_conn *conn = open_database(path, false);
    if (conn == NULL) {
        test_db_remove(path);
        return;
    }

    lsql_result *result = test_sql(conn, "CREATE TABLE users (id INTEGER, name TEXT, score REAL)");
    if (result != NULL) {
        TEST_CHECK(lsql_result_type(result) == LSQL_RESULT_CHANGE);
        TEST_CHECK_SIZE(lsql_result_affected_rows(result), 0);
    }
    lsql_result_free(result);

    const char *inserts[] = {
        "INSERT INTO users VALUES (1, 'Alice', 9.5)",
        "INSERT INTO users VALUES (2, 'Bob', NULL)",
        "INSERT INTO users (name, id) VALUES ('Carol', 3)",
    };
    for (size_t index = 0; index < sizeof(inserts) / sizeof(inserts[0]); ++index) {
        result = test_sql(conn, inserts[index]);
        if (result != NULL) {
            TEST_CHECK_SIZE(lsql_result_affected_rows(result), 1);
        }
        lsql_result_free(result);
    }

    result = test_sql(conn, "SELECT id, name FROM users ORDER BY id");
    if (result != NULL) {
        TEST_CHECK(lsql_result_type(result) == LSQL_RESULT_ROWS);
        TEST_CHECK_SIZE(lsql_result_column_count(result), 2);
        TEST_CHECK_STRING(lsql_result_column_name(result, 0), "id");
        TEST_CHECK_STRING(lsql_result_column_name(result, 1), "name");
        TEST_CHECK_SIZE(lsql_result_row_count(result), 3);
        check_int(result, 0, 0, 1);
        check_text(result, 0, 1, "Alice");
        check_int(result, 1, 0, 2);
        check_text(result, 1, 1, "Bob");
        check_int(result, 2, 0, 3);
        check_text(result, 2, 1, "Carol");
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT * FROM users WHERE id = 2");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 1);
        check_int(result, 0, 0, 2);
        check_text(result, 0, 1, "Bob");
        check_null(result, 0, 2);
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT name FROM users WHERE score IS NULL ORDER BY name");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 2);
        check_text(result, 0, 0, "Bob");
        check_text(result, 1, 0, "Carol");
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT id FROM users WHERE id >= 2 AND name <> 'Carol' "
                            "ORDER BY id DESC LIMIT 1");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 1);
        check_int(result, 0, 0, 2);
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT rowid, id FROM users ORDER BY rowid DESC");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 3);
        check_int(result, 0, 0, 3);
        check_int(result, 0, 1, 3);
        check_int(result, 1, 0, 2);
        check_int(result, 1, 1, 2);
        check_int(result, 2, 0, 1);
        check_int(result, 2, 1, 1);
    }
    lsql_result_free(result);

    result = test_sql(conn, "DELETE FROM users WHERE id = 1");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_affected_rows(result), 1);
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT id FROM users ORDER BY id");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 2);
        check_int(result, 0, 0, 2);
        check_int(result, 1, 0, 3);
    }
    lsql_result_free(result);

    result = test_sql(conn, "DROP TABLE users");
    if (result != NULL) {
        TEST_CHECK(lsql_result_type(result) == LSQL_RESULT_CHANGE);
    }
    lsql_result_free(result);

    lsql_error error;
    TEST_CHECK_STATUS(test_sql_error(conn, "SELECT * FROM users", &error), LSQL_ERROR_NOT_FOUND);
    TEST_CHECK(error.message[0] != '\0');

    lsql_close(conn);
    test_db_remove(path);
}

static void test_persistence_and_read_only(void) {
    char *path = test_db_create();
    TEST_CHECK(path != NULL);
    if (path == NULL) {
        return;
    }

    lsql_conn *conn = open_database(path, false);
    if (conn == NULL) {
        test_db_remove(path);
        return;
    }
    lsql_result_free(test_sql(conn, "CREATE TABLE items (id INTEGER, label TEXT)"));
    lsql_result_free(test_sql(conn, "INSERT INTO items VALUES (1, 'one')"));
    lsql_result_free(test_sql(conn, "INSERT INTO items VALUES (2, 'two')"));
    lsql_close(conn);

    conn = open_database(path, true);
    if (conn == NULL) {
        test_db_remove(path);
        return;
    }

    lsql_result *result = test_sql(conn, "SELECT id, label FROM items ORDER BY id");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 2);
        check_text(result, 0, 1, "one");
        check_text(result, 1, 1, "two");
    }
    lsql_result_free(result);

    lsql_error error;
    TEST_CHECK_STATUS(test_sql_error(conn, "INSERT INTO items VALUES (3, 'three')", &error),
                      LSQL_ERROR_READ_ONLY);
    TEST_CHECK_STATUS(test_sql_error(conn, "DROP TABLE items", &error), LSQL_ERROR_READ_ONLY);
    lsql_close(conn);

    conn = open_database(path, false);
    if (conn != NULL) {
        result = test_sql(conn, "SELECT rowid FROM items ORDER BY rowid");
        if (result != NULL) {
            TEST_CHECK_SIZE(lsql_result_row_count(result), 2);
        }
        lsql_result_free(result);
        lsql_close(conn);
    }

    test_db_remove(path);
}

static void test_errors(void) {
    char *path = test_db_create();
    TEST_CHECK(path != NULL);
    if (path == NULL) {
        return;
    }

    lsql_conn *conn = open_database(path, false);
    if (conn == NULL) {
        test_db_remove(path);
        return;
    }

    lsql_error error;
    TEST_CHECK_STATUS(test_sql_error(conn, "", &error), LSQL_ERROR_PARSE);
    TEST_CHECK(error.offset != LSQL_OFFSET_NONE);

    TEST_CHECK_STATUS(test_sql_error(conn, "SELECT FROM nowhere", &error), LSQL_ERROR_PARSE);
    TEST_CHECK(error.offset != LSQL_OFFSET_NONE);

    lsql_result_free(test_sql(conn, "CREATE TABLE t (id INTEGER, name TEXT)"));

    TEST_CHECK_STATUS(test_sql_error(conn, "CREATE TABLE t (id INTEGER)", &error),
                      LSQL_ERROR_ALREADY_EXISTS);
    TEST_CHECK_STATUS(test_sql_error(conn, "CREATE TABLE IF NOT EXISTS t (id INTEGER)", &error),
                      LSQL_OK);

    TEST_CHECK_STATUS(test_sql_error(conn, "DROP TABLE missing", &error), LSQL_ERROR_NOT_FOUND);
    TEST_CHECK_STATUS(test_sql_error(conn, "DROP TABLE IF EXISTS missing", &error), LSQL_OK);

    TEST_CHECK_STATUS(test_sql_error(conn, "INSERT INTO t (missing) VALUES (1)", &error),
                      LSQL_ERROR_NOT_FOUND);
    TEST_CHECK_STATUS(test_sql_error(conn, "INSERT INTO t VALUES ('bad', 'name')", &error),
                      LSQL_ERROR_TYPE);
    TEST_CHECK_STATUS(test_sql_error(conn, "INSERT INTO t VALUES (1)", &error), LSQL_ERROR_TYPE);
    TEST_CHECK_STATUS(test_sql_error(conn, "INSERT INTO t (id, id) VALUES (1, 2)", &error),
                      LSQL_ERROR_TYPE);
    TEST_CHECK_STATUS(test_sql_error(conn, "SELECT missing FROM t", &error), LSQL_ERROR_NOT_FOUND);
    TEST_CHECK_STATUS(test_sql_error(conn, "SELECT * FROM t ORDER BY missing", &error),
                      LSQL_ERROR_NOT_FOUND);
    TEST_CHECK_STATUS(test_sql_error(conn, "SELECT * FROM t WHERE missing = 1", &error),
                      LSQL_ERROR_NOT_FOUND);
    TEST_CHECK_STATUS(test_sql_error(conn, "SELECT * FROM missing", &error), LSQL_ERROR_NOT_FOUND);

    lsql_result_free(test_sql(conn, "INSERT INTO t VALUES (1, 'Alice')"));
    TEST_CHECK_STATUS(test_sql_error(conn, "INSERT INTO t (id, name) VALUES (2, 3)", &error),
                      LSQL_ERROR_TYPE);

    lsql_close(conn);
    test_db_remove(path);
}

static void test_values_and_ordering(void) {
    char *path = test_db_create();
    TEST_CHECK(path != NULL);
    if (path == NULL) {
        return;
    }

    lsql_conn *conn = open_database(path, false);
    if (conn == NULL) {
        test_db_remove(path);
        return;
    }

    lsql_result_free(test_sql(conn, "CREATE TABLE items (id INTEGER, value REAL, note TEXT)"));
    lsql_result_free(test_sql(conn, "INSERT INTO items VALUES (1, 42, '')"));
    lsql_result_free(test_sql(conn, "INSERT INTO items VALUES (2, 1.25, 'a''b')"));
    lsql_result_free(test_sql(conn, "INSERT INTO items VALUES (3, NULL, NULL)"));
    lsql_result_free(test_sql(conn, "INSERT INTO items VALUES (-4, -2.5, 'negative')"));

    lsql_result *result = test_sql(conn, "SELECT id, value, note FROM items ORDER BY id");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 4);
        check_int(result, 0, 0, -4);
        check_real(result, 0, 1, -2.5);
        check_text(result, 0, 2, "negative");
        check_int(result, 1, 0, 1);
        check_real(result, 1, 1, 42.0);
        check_text(result, 1, 2, "");
        check_int(result, 2, 0, 2);
        check_real(result, 2, 1, 1.25);
        check_text(result, 2, 2, "a'b");
        check_int(result, 3, 0, 3);
        check_null(result, 3, 1);
        check_null(result, 3, 2);
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT id FROM items WHERE value > 10");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 1);
        check_int(result, 0, 0, 1);
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT id FROM items ORDER BY value");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 4);
        check_int(result, 0, 0, 3);
        check_int(result, 1, 0, -4);
        check_int(result, 2, 0, 2);
        check_int(result, 3, 0, 1);
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT id FROM items ORDER BY value DESC");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 4);
        check_int(result, 0, 0, 1);
        check_int(result, 1, 0, 2);
        check_int(result, 2, 0, -4);
        check_int(result, 3, 0, 3);
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT id FROM items WHERE note = ''");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 1);
        check_int(result, 0, 0, 1);
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT id FROM items WHERE value IS NULL AND note IS NULL");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 1);
        check_int(result, 0, 0, 3);
    }
    lsql_result_free(result);

    result = test_sql(conn, "SELECT id FROM items WHERE id = 1 OR id = 2 ORDER BY id");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 2);
    }
    lsql_result_free(result);

    lsql_close(conn);
    test_db_remove(path);
}

static void test_failed_insert_does_not_consume_rowid(void) {
    char *path = test_db_create();
    TEST_CHECK(path != NULL);
    if (path == NULL) {
        return;
    }

    lsql_conn *conn = open_database(path, false);
    if (conn == NULL) {
        test_db_remove(path);
        return;
    }

    lsql_result_free(test_sql(conn, "CREATE TABLE t (id INTEGER)"));
    lsql_error error;
    TEST_CHECK_STATUS(test_sql_error(conn, "INSERT INTO t VALUES ('bad')", &error),
                      LSQL_ERROR_TYPE);
    lsql_result_free(test_sql(conn, "INSERT INTO t VALUES (7)"));

    lsql_result *result = test_sql(conn, "SELECT rowid, id FROM t");
    if (result != NULL) {
        TEST_CHECK_SIZE(lsql_result_row_count(result), 1);
        check_int(result, 0, 0, 1);
        check_int(result, 0, 1, 7);
    }
    lsql_result_free(result);

    lsql_close(conn);
    test_db_remove(path);
}

static void test_api_edges(void) {
    TEST_CHECK_STRING(lsql_status_name(LSQL_OK), "LSQL_OK");
    // Intentionally out-of-range to exercise the fallback branch.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    TEST_CHECK_STRING(lsql_status_name((lsql_status)999), "LSQL_ERROR_UNKNOWN");
    TEST_CHECK_STRING(lsql_error_message(NULL), "");
    TEST_CHECK(lsql_result_type(NULL) == LSQL_RESULT_CHANGE);
    TEST_CHECK_SIZE(lsql_result_column_count(NULL), 0);
    TEST_CHECK(lsql_result_column_name(NULL, 0) == NULL);
    TEST_CHECK_SIZE(lsql_result_row_count(NULL), 0);
    TEST_CHECK(lsql_result_value(NULL, 0, 0) == NULL);
    TEST_CHECK_SIZE(lsql_result_affected_rows(NULL), 0);

    lsql_options options;
    lsql_options_init(&options);
    TEST_CHECK(options.map_size > 0);
    TEST_CHECK(options.max_readers > 0);
    TEST_CHECK(options.max_dbs >= 2);
    TEST_CHECK(!options.read_only);

    lsql_options_init(NULL);

    lsql_error error;
    lsql_conn *conn = NULL;
    TEST_CHECK_STATUS(lsql_open(NULL, NULL, &conn, &error), LSQL_ERROR_INVALID_ARGUMENT);
    TEST_CHECK(conn == NULL);
    TEST_CHECK_STATUS(lsql_open("/tmp", NULL, NULL, &error), LSQL_ERROR_INVALID_ARGUMENT);
    TEST_CHECK_STATUS(lsql_execute(NULL, "SELECT 1", 8, NULL, &error), LSQL_ERROR_INVALID_ARGUMENT);
}

void run_sql_tests(void) {
    test_basic_crud();
    test_persistence_and_read_only();
    test_errors();
    test_values_and_ordering();
    test_failed_insert_does_not_consume_rowid();
    test_api_edges();
}
