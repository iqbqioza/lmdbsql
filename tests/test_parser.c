#include "test_harness.h"

#include "parser.h"

#include <string.h>

static lsql_statement *parse_ok(const char *sql, lsql_arena *arena, lsql_error *error) {
    lsql_statement *statement = NULL;
    lsql_status status = lsql_parse(sql, strlen(sql), arena, &statement, error);
    TEST_CHECK_MSG(status == LSQL_OK, "parse failed for '%s': %s", sql, error->message);
    return statement;
}

static void expect_parse_error(const char *sql) {
    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_error error;
    lsql_statement *statement = NULL;
    lsql_status status = lsql_parse(sql, strlen(sql), &arena, &statement, &error);

    TEST_CHECK_MSG(status != LSQL_OK, "expected parse error for: %s", sql);
    TEST_CHECK(error.status == LSQL_ERROR_PARSE || error.status == LSQL_ERROR_OUT_OF_MEMORY);
    TEST_CHECK(error.message[0] != '\0');
    TEST_CHECK(error.offset != LSQL_OFFSET_NONE);
    lsql_arena_destroy(&arena);
}

static void test_create(void) {
    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_error error;
    lsql_statement *statement =
        parse_ok("CREATE TABLE users (id INTEGER, name TEXT, score REAL)", &arena, &error);

    TEST_CHECK(statement != NULL);
    if (statement != NULL) {
        TEST_CHECK(statement->type == LSQL_STATEMENT_CREATE_TABLE);
        TEST_CHECK_STRING(statement->table, "users");
        TEST_CHECK_SIZE(statement->as.create.count, 3);
        TEST_CHECK(!statement->as.create.if_not_exists);
        if (statement->as.create.count == 3) {
            TEST_CHECK_STRING(statement->as.create.columns[0].name, "id");
            TEST_CHECK(statement->as.create.columns[0].type == LSQL_TYPE_INTEGER);
            TEST_CHECK_STRING(statement->as.create.columns[1].name, "name");
            TEST_CHECK(statement->as.create.columns[1].type == LSQL_TYPE_TEXT);
            TEST_CHECK_STRING(statement->as.create.columns[2].name, "score");
            TEST_CHECK(statement->as.create.columns[2].type == LSQL_TYPE_REAL);
        }
    }
    lsql_arena_destroy(&arena);

    lsql_arena_init(&arena);
    statement = parse_ok("create table if not exists T (X int, Y double)", &arena, &error);
    TEST_CHECK(statement != NULL);
    if (statement != NULL) {
        TEST_CHECK_STRING(statement->table, "t");
        TEST_CHECK(statement->as.create.if_not_exists);
        TEST_CHECK_SIZE(statement->as.create.count, 2);
        TEST_CHECK_STRING(statement->as.create.columns[0].name, "x");
        TEST_CHECK(statement->as.create.columns[0].type == LSQL_TYPE_INTEGER);
        TEST_CHECK(statement->as.create.columns[1].type == LSQL_TYPE_REAL);
    }
    lsql_arena_destroy(&arena);
}

static void test_insert(void) {
    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_error error;
    lsql_statement *statement =
        parse_ok("INSERT INTO users (id, name) VALUES (1, 'Alice')", &arena, &error);

    TEST_CHECK(statement != NULL);
    if (statement != NULL) {
        TEST_CHECK(statement->type == LSQL_STATEMENT_INSERT);
        TEST_CHECK_STRING(statement->table, "users");
        TEST_CHECK_SIZE(statement->as.insert.column_count, 2);
        TEST_CHECK_STRING(statement->as.insert.columns[0], "id");
        TEST_CHECK_STRING(statement->as.insert.columns[1], "name");
        TEST_CHECK_SIZE(statement->as.insert.value_count, 2);
        TEST_CHECK(statement->as.insert.values[0].type == LSQL_TYPE_INTEGER);
        TEST_CHECK_INT64(statement->as.insert.values[0].as.integer, 1);
        TEST_CHECK(statement->as.insert.values[1].type == LSQL_TYPE_TEXT);
        TEST_CHECK_SIZE(statement->as.insert.values[1].as.text.length, 5);
        TEST_CHECK(memcmp(statement->as.insert.values[1].as.text.data, "Alice", 5) == 0);
    }
    lsql_arena_destroy(&arena);

    lsql_arena_init(&arena);
    statement = parse_ok("insert into t values (-42, NULL, 2.5, true, false)", &arena, &error);
    TEST_CHECK(statement != NULL);
    if (statement != NULL) {
        TEST_CHECK_SIZE(statement->as.insert.value_count, 5);
        TEST_CHECK_INT64(statement->as.insert.values[0].as.integer, -42);
        TEST_CHECK(statement->as.insert.values[1].type == LSQL_TYPE_NULL);
        TEST_CHECK(statement->as.insert.values[2].type == LSQL_TYPE_REAL);
        TEST_CHECK_REAL(statement->as.insert.values[2].as.real, 2.5);
        TEST_CHECK_INT64(statement->as.insert.values[3].as.integer, 1);
        TEST_CHECK_INT64(statement->as.insert.values[4].as.integer, 0);
    }
    lsql_arena_destroy(&arena);
}

static void test_select(void) {
    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_error error;
    lsql_statement *statement = parse_ok("SELECT * FROM users WHERE id >= 10 AND name IS NOT NULL "
                                         "ORDER BY id DESC LIMIT 5 OFFSET 2",
                                         &arena, &error);

    TEST_CHECK(statement != NULL);
    if (statement != NULL) {
        TEST_CHECK(statement->type == LSQL_STATEMENT_SELECT);
        TEST_CHECK_STRING(statement->table, "users");
        TEST_CHECK(statement->as.select.star);
        TEST_CHECK(statement->as.select.where != NULL);
        if (statement->as.select.where != NULL) {
            const lsql_expression *where = statement->as.select.where;
            TEST_CHECK(where->type == LSQL_EXPRESSION_BINARY);
            TEST_CHECK(where->as.binary.op == LSQL_BINARY_AND);
            TEST_CHECK(where->as.binary.left->type == LSQL_EXPRESSION_BINARY);
            TEST_CHECK(where->as.binary.left->as.binary.op == LSQL_BINARY_GREATER_EQUAL);
            TEST_CHECK_STRING(where->as.binary.left->as.binary.left->as.column, "id");
            TEST_CHECK(where->as.binary.left->as.binary.right->type == LSQL_EXPRESSION_LITERAL);
            TEST_CHECK_INT64(where->as.binary.left->as.binary.right->as.literal.as.integer, 10);
            TEST_CHECK(where->as.binary.right->type == LSQL_EXPRESSION_IS_NULL);
            TEST_CHECK(where->as.binary.right->as.is_null.negated);
            TEST_CHECK_STRING(where->as.binary.right->as.is_null.operand->as.column, "name");
        }
        TEST_CHECK_STRING(statement->as.select.order_by, "id");
        TEST_CHECK(statement->as.select.order_descending);
        TEST_CHECK(statement->as.select.has_limit);
        TEST_CHECK_INT64(statement->as.select.limit, 5);
        TEST_CHECK(statement->as.select.has_offset);
        TEST_CHECK_INT64(statement->as.select.offset, 2);
    }
    lsql_arena_destroy(&arena);

    lsql_arena_init(&arena);
    statement = parse_ok("SELECT id, name, rowid FROM users ORDER BY rowid ASC", &arena, &error);
    TEST_CHECK(statement != NULL);
    if (statement != NULL) {
        TEST_CHECK(!statement->as.select.star);
        TEST_CHECK_SIZE(statement->as.select.column_count, 3);
        TEST_CHECK_STRING(statement->as.select.columns[0], "id");
        TEST_CHECK_STRING(statement->as.select.columns[1], "name");
        TEST_CHECK_STRING(statement->as.select.columns[2], "rowid");
        TEST_CHECK(!statement->as.select.order_descending);
        TEST_CHECK(!statement->as.select.has_limit);
    }
    lsql_arena_destroy(&arena);
}

static void test_delete_and_drop(void) {
    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_error error;
    const lsql_statement *statement =
        parse_ok("DELETE FROM users WHERE name = 'Alice'", &arena, &error);
    TEST_CHECK(statement != NULL);
    if (statement != NULL) {
        TEST_CHECK(statement->type == LSQL_STATEMENT_DELETE);
        TEST_CHECK_STRING(statement->table, "users");
        TEST_CHECK(statement->as.delete.where != NULL);
        if (statement->as.delete.where != NULL) {
            TEST_CHECK(statement->as.delete.where->type == LSQL_EXPRESSION_BINARY);
        } else {
            test_failure(__FILE__, __LINE__, "DELETE has no WHERE expression");
        }
    }
    lsql_arena_destroy(&arena);

    lsql_arena_init(&arena);
    statement = parse_ok("DROP TABLE IF EXISTS users", &arena, &error);
    TEST_CHECK(statement != NULL);
    if (statement != NULL) {
        TEST_CHECK(statement->type == LSQL_STATEMENT_DROP_TABLE);
        TEST_CHECK(statement->as.drop.if_exists);
        TEST_CHECK_STRING(statement->table, "users");
    }
    lsql_arena_destroy(&arena);
}

static void test_parse_errors(void) {
    expect_parse_error("");
    expect_parse_error("SELECT");
    expect_parse_error("SELECT FROM users");
    expect_parse_error("SELECT * FROM");
    expect_parse_error("SELECT * FROM users WHERE");
    expect_parse_error("CREATE TABLE t");
    expect_parse_error("CREATE TABLE t ()");
    expect_parse_error("CREATE TABLE t (id INTEGER, id TEXT)");
    expect_parse_error("CREATE TABLE t (id BLOB)");
    expect_parse_error("CREATE TABLE t (rowid INTEGER)");
    expect_parse_error("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    expect_parse_error("INSERT INTO t VALUES ()");
    expect_parse_error("INSERT INTO t (a, b) VALUES (1)");
    expect_parse_error("INSERT INTO t VALUES (1); SELECT 1");
    expect_parse_error("SELECT * FROM t LIMIT -1");
    expect_parse_error("SELECT * FROM t LIMIT 1 LIMIT 2");
    expect_parse_error("SELECT * FROM \"has space\"");
    expect_parse_error("DELETE t");
    expect_parse_error("DROP users");
    expect_parse_error("UPDATE t SET x = 1");
}

void run_parser_tests(void) {
    test_create();
    test_insert();
    test_select();
    test_delete_and_drop();
    test_parse_errors();
}
