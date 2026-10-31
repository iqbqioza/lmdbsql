#include "test_harness.h"

#include "lexer.h"

#include <string.h>

static void test_token_sequence(void) {
    static const struct {
        lsql_token_type type;
        const char *text;
    } expected[] = {
        {LSQL_TOKEN_IDENTIFIER, "select"}, {LSQL_TOKEN_IDENTIFIER, "id"},
        {LSQL_TOKEN_COMMA, NULL},          {LSQL_TOKEN_IDENTIFIER, "name"},
        {LSQL_TOKEN_IDENTIFIER, "from"},   {LSQL_TOKEN_IDENTIFIER, "users"},
        {LSQL_TOKEN_IDENTIFIER, "where"},  {LSQL_TOKEN_IDENTIFIER, "age"},
        {LSQL_TOKEN_GREATER_EQUAL, NULL},  {LSQL_TOKEN_INTEGER, NULL},
        {LSQL_TOKEN_IDENTIFIER, "and"},    {LSQL_TOKEN_IDENTIFIER, "name"},
        {LSQL_TOKEN_NOT_EQUAL, NULL},      {LSQL_TOKEN_STRING, NULL},
        {LSQL_TOKEN_SEMICOLON, NULL},      {LSQL_TOKEN_EOF, NULL},
    };

    const char *sql = "SELECT id, name FROM users WHERE age >= 18 AND name <> 'O''Brien';";
    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_error error;
    lsql_lexer lexer;
    lsql_lexer_init(&lexer, sql, strlen(sql), &arena, &error);

    size_t previous_offset = 0;
    for (size_t index = 0; index < sizeof(expected) / sizeof(expected[0]); ++index) {
        lsql_token token;
        TEST_CHECK(lsql_lexer_next(&lexer, &token));
        TEST_CHECK(token.type == expected[index].type);
        if (expected[index].text != NULL) {
            TEST_CHECK_STRING(token.text, expected[index].text);
        }
        if (index == 9) {
            TEST_CHECK_INT64(token.integer, 18);
        }
        if (index == 13) {
            TEST_CHECK_SIZE(token.length, 7);
            TEST_CHECK(memcmp(token.text, "O'Brien", 7) == 0);
        }
        TEST_CHECK(token.offset >= previous_offset);
        previous_offset = token.offset;
    }

    lsql_arena_destroy(&arena);
}

static void test_case_folding_and_quoting(void) {
    const char *sql = "SeLeCt \"MixedCase\", \"a\"\"b\"";
    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_error error;
    lsql_lexer lexer;
    lsql_lexer_init(&lexer, sql, strlen(sql), &arena, &error);

    lsql_token token;
    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK_STRING(token.text, "select");
    TEST_CHECK(!token.quoted);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK_STRING(token.text, "MixedCase");
    TEST_CHECK(token.quoted);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK(token.type == LSQL_TOKEN_COMMA);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK_STRING(token.text, "a\"b");
    TEST_CHECK(token.quoted);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK(token.type == LSQL_TOKEN_EOF);

    lsql_arena_destroy(&arena);
}

static void test_comments_and_numbers(void) {
    const char *sql = "-- line comment\n/* block\ncomment */ 42 3.5 1e2 -7 1. 1e-2";
    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_error error;
    lsql_lexer lexer;
    lsql_lexer_init(&lexer, sql, strlen(sql), &arena, &error);

    lsql_token token;
    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK(token.type == LSQL_TOKEN_INTEGER);
    TEST_CHECK_INT64(token.integer, 42);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK(token.type == LSQL_TOKEN_REAL);
    TEST_CHECK_REAL(token.real, 3.5);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK(token.type == LSQL_TOKEN_REAL);
    TEST_CHECK_REAL(token.real, 100.0);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK(token.type == LSQL_TOKEN_MINUS);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK(token.type == LSQL_TOKEN_INTEGER);
    TEST_CHECK_INT64(token.integer, 7);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK(token.type == LSQL_TOKEN_REAL);
    TEST_CHECK_REAL(token.real, 1.0);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK(token.type == LSQL_TOKEN_REAL);
    TEST_CHECK_REAL(token.real, 0.01);

    TEST_CHECK(lsql_lexer_next(&lexer, &token));
    TEST_CHECK(token.type == LSQL_TOKEN_EOF);

    lsql_arena_destroy(&arena);
}

static void expect_lexer_error(const char *sql) {
    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_error error;
    lsql_lexer lexer;
    lsql_lexer_init(&lexer, sql, strlen(sql), &arena, &error);

    lsql_token token;
    bool failed = false;
    for (;;) {
        if (!lsql_lexer_next(&lexer, &token)) {
            failed = true;
            break;
        }
        if (token.type == LSQL_TOKEN_EOF) {
            break;
        }
    }

    TEST_CHECK_MSG(failed, "expected lexer error for: %s", sql);
    TEST_CHECK(error.status == LSQL_ERROR_PARSE);
    TEST_CHECK(error.offset != LSQL_OFFSET_NONE);
    lsql_arena_destroy(&arena);
}

static void test_lexer_errors(void) {
    expect_lexer_error("'unterminated");
    expect_lexer_error("\"unterminated");
    expect_lexer_error("/* unterminated");
    expect_lexer_error("1e");
    expect_lexer_error("1e+");
    expect_lexer_error("@");
    expect_lexer_error(".5");
    expect_lexer_error("999999999999999999999999");
}

void run_lexer_tests(void) {
    test_token_sequence();
    test_case_folding_and_quoting();
    test_comments_and_numbers();
    test_lexer_errors();
}
