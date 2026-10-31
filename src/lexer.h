#ifndef LMDBSQL_LEXER_H
#define LMDBSQL_LEXER_H

#include "internal.h"

typedef enum lsql_token_type {
    LSQL_TOKEN_EOF = 0,
    LSQL_TOKEN_IDENTIFIER,
    LSQL_TOKEN_STRING,
    LSQL_TOKEN_INTEGER,
    LSQL_TOKEN_REAL,
    LSQL_TOKEN_LEFT_PAREN,
    LSQL_TOKEN_RIGHT_PAREN,
    LSQL_TOKEN_COMMA,
    LSQL_TOKEN_SEMICOLON,
    LSQL_TOKEN_STAR,
    LSQL_TOKEN_MINUS,
    LSQL_TOKEN_EQUAL,
    LSQL_TOKEN_NOT_EQUAL,
    LSQL_TOKEN_LESS,
    LSQL_TOKEN_LESS_EQUAL,
    LSQL_TOKEN_GREATER,
    LSQL_TOKEN_GREATER_EQUAL
} lsql_token_type;

typedef struct lsql_token {
    lsql_token_type type;
    const char *text;
    size_t length;
    size_t offset;
    int64_t integer;
    double real;
    bool quoted;
} lsql_token;

typedef struct lsql_lexer {
    const char *input;
    size_t length;
    size_t position;
    lsql_arena *arena;
    lsql_error *error;
} lsql_lexer;

void lsql_lexer_init(lsql_lexer *lexer, const char *input, size_t length, lsql_arena *arena,
                     lsql_error *error);
bool lsql_lexer_next(lsql_lexer *lexer, lsql_token *token);
bool lsql_token_is_keyword(const lsql_token *token, const char *keyword);

#endif /* LMDBSQL_LEXER_H */
