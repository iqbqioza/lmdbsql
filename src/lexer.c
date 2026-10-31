#include "lexer.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool lsql_lexer_fail(lsql_lexer *lexer, size_t offset, const char *format, ...)
    LSQL_PRINTF(3, 4);

static bool lsql_lexer_fail(lsql_lexer *lexer, size_t offset, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    lsql_error_set_v(lexer->error, LSQL_ERROR_PARSE, offset, format, arguments);
    va_end(arguments);
    return false;
}

static bool lsql_is_space(unsigned char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

static bool lsql_is_digit(unsigned char character) {
    return character >= '0' && character <= '9';
}

static bool lsql_is_identifier_start(unsigned char character) {
    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z') ||
           character == '_';
}

static bool lsql_is_identifier_continue(unsigned char character) {
    return lsql_is_identifier_start(character) || lsql_is_digit(character);
}

static bool lsql_lexer_skip_space_and_comments(lsql_lexer *lexer) {
    while (lexer->position < lexer->length) {
        unsigned char character = (unsigned char)lexer->input[lexer->position];
        if (lsql_is_space(character)) {
            lexer->position++;
            continue;
        }

        if (character == '-' && lexer->position + 1u < lexer->length &&
            lexer->input[lexer->position + 1u] == '-') {
            lexer->position += 2u;
            while (lexer->position < lexer->length && lexer->input[lexer->position] != '\n') {
                lexer->position++;
            }
            continue;
        }

        if (character == '/' && lexer->position + 1u < lexer->length &&
            lexer->input[lexer->position + 1u] == '*') {
            size_t comment_offset = lexer->position;
            lexer->position += 2u;
            bool closed = false;
            while (lexer->position + 1u < lexer->length) {
                if (lexer->input[lexer->position] == '*' &&
                    lexer->input[lexer->position + 1u] == '/') {
                    lexer->position += 2u;
                    closed = true;
                    break;
                }
                lexer->position++;
            }
            if (!closed) {
                return lsql_lexer_fail(lexer, comment_offset, "unterminated block comment");
            }
            continue;
        }

        break;
    }
    return true;
}

static bool lsql_lexer_identifier(lsql_lexer *lexer, lsql_token *token) {
    token->offset = lexer->position;
    token->quoted = false;

    if (lexer->input[lexer->position] == '"') {
        token->quoted = true;
        size_t start = lexer->position + 1u;
        size_t maximum = lexer->length - start;
        char *text = lsql_arena_alloc(lexer->arena, maximum + 1u);
        if (text == NULL) {
            return lsql_error_set(lexer->error, LSQL_ERROR_OUT_OF_MEMORY, token->offset,
                                  "out of memory while reading identifier") == LSQL_OK;
        }

        size_t length = 0;
        size_t position = start;
        bool closed = false;
        while (position < lexer->length) {
            char character = lexer->input[position];
            if (character == '"') {
                if (position + 1u < lexer->length && lexer->input[position + 1u] == '"') {
                    text[length++] = '"';
                    position += 2u;
                    continue;
                }
                position++;
                closed = true;
                break;
            }
            text[length++] = character;
            position++;
        }

        if (!closed) {
            return lsql_lexer_fail(lexer, token->offset, "unterminated quoted identifier");
        }
        text[length] = '\0';
        token->type = LSQL_TOKEN_IDENTIFIER;
        token->text = text;
        token->length = length;
        lexer->position = position;
        return true;
    }

    size_t start = lexer->position;
    while (lexer->position < lexer->length &&
           lsql_is_identifier_continue((unsigned char)lexer->input[lexer->position])) {
        lexer->position++;
    }

    size_t length = lexer->position - start;
    char *text = lsql_arena_alloc(lexer->arena, length + 1u);
    if (text == NULL) {
        return lsql_error_set(lexer->error, LSQL_ERROR_OUT_OF_MEMORY, token->offset,
                              "out of memory while reading identifier") == LSQL_OK;
    }

    for (size_t index = 0; index < length; ++index) {
        char character = lexer->input[start + index];
        if (character >= 'A' && character <= 'Z') {
            character = (char)(character - 'A' + 'a');
        }
        text[index] = character;
    }
    text[length] = '\0';

    token->type = LSQL_TOKEN_IDENTIFIER;
    token->text = text;
    token->length = length;
    return true;
}

static bool lsql_lexer_string(lsql_lexer *lexer, lsql_token *token) {
    token->offset = lexer->position;
    size_t start = lexer->position + 1u;
    size_t maximum = lexer->length - start;
    char *text = lsql_arena_alloc(lexer->arena, maximum + 1u);
    if (text == NULL) {
        return lsql_error_set(lexer->error, LSQL_ERROR_OUT_OF_MEMORY, token->offset,
                              "out of memory while reading string literal") == LSQL_OK;
    }

    size_t length = 0;
    size_t position = start;
    bool closed = false;
    while (position < lexer->length) {
        char character = lexer->input[position];
        if (character == '\'') {
            if (position + 1u < lexer->length && lexer->input[position + 1u] == '\'') {
                text[length++] = '\'';
                position += 2u;
                continue;
            }
            position++;
            closed = true;
            break;
        }
        text[length++] = character;
        position++;
    }

    if (!closed) {
        return lsql_lexer_fail(lexer, token->offset, "unterminated string literal");
    }
    text[length] = '\0';

    token->type = LSQL_TOKEN_STRING;
    token->text = text;
    token->length = length;
    token->quoted = false;
    lexer->position = position;
    return true;
}

static bool lsql_lexer_number(lsql_lexer *lexer, lsql_token *token) {
    token->offset = lexer->position;
    size_t start = lexer->position;
    bool is_real = false;

    while (lexer->position < lexer->length &&
           lsql_is_digit((unsigned char)lexer->input[lexer->position])) {
        lexer->position++;
    }

    if (lexer->position < lexer->length && lexer->input[lexer->position] == '.') {
        is_real = true;
        lexer->position++;
        while (lexer->position < lexer->length &&
               lsql_is_digit((unsigned char)lexer->input[lexer->position])) {
            lexer->position++;
        }
    }

    if (lexer->position < lexer->length &&
        (lexer->input[lexer->position] == 'e' || lexer->input[lexer->position] == 'E')) {
        is_real = true;
        lexer->position++;
        if (lexer->position < lexer->length &&
            (lexer->input[lexer->position] == '+' || lexer->input[lexer->position] == '-')) {
            lexer->position++;
        }
        if (lexer->position >= lexer->length ||
            !lsql_is_digit((unsigned char)lexer->input[lexer->position])) {
            return lsql_lexer_fail(lexer, token->offset, "malformed numeric exponent");
        }
        while (lexer->position < lexer->length &&
               lsql_is_digit((unsigned char)lexer->input[lexer->position])) {
            lexer->position++;
        }
    }

    size_t length = lexer->position - start;
    char *text = lsql_arena_alloc(lexer->arena, length + 1u);
    if (text == NULL) {
        return lsql_error_set(lexer->error, LSQL_ERROR_OUT_OF_MEMORY, token->offset,
                              "out of memory while reading numeric literal") == LSQL_OK;
    }
    memcpy(text, lexer->input + start, length);
    text[length] = '\0';

    char *end = NULL;
    errno = 0;
    if (is_real) {
        double value = strtod(text, &end);
        if (end == text || *end != '\0') {
            return lsql_lexer_fail(lexer, token->offset, "invalid numeric literal");
        }
        if (errno == ERANGE || !isfinite(value)) {
            return lsql_lexer_fail(lexer, token->offset, "numeric literal is out of range");
        }
        token->type = LSQL_TOKEN_REAL;
        token->real = value;
    } else {
        long long value = strtoll(text, &end, 10);
        if (end == text || *end != '\0') {
            return lsql_lexer_fail(lexer, token->offset, "invalid integer literal");
        }
        if (errno == ERANGE) {
            return lsql_lexer_fail(lexer, token->offset, "integer literal is out of range");
        }
        token->type = LSQL_TOKEN_INTEGER;
        token->integer = (int64_t)value;
    }

    token->text = text;
    token->length = length;
    token->quoted = false;
    return true;
}

void lsql_lexer_init(lsql_lexer *lexer, const char *input, size_t length, lsql_arena *arena,
                     lsql_error *error) {
    lexer->input = input;
    lexer->length = length;
    lexer->position = 0;
    lexer->arena = arena;
    lexer->error = error;
}

bool lsql_lexer_next(lsql_lexer *lexer, lsql_token *token) {
    memset(token, 0, sizeof(*token));
    token->offset = lexer->position;

    if (!lsql_lexer_skip_space_and_comments(lexer)) {
        return false;
    }

    token->offset = lexer->position;
    if (lexer->position >= lexer->length) {
        token->type = LSQL_TOKEN_EOF;
        token->length = 0;
        token->text = NULL;
        return true;
    }

    unsigned char character = (unsigned char)lexer->input[lexer->position];
    if (lsql_is_identifier_start(character) || character == '"') {
        return lsql_lexer_identifier(lexer, token);
    }
    if (character == '\'') {
        return lsql_lexer_string(lexer, token);
    }
    if (lsql_is_digit(character)) {
        return lsql_lexer_number(lexer, token);
    }

    lexer->position++;
    token->quoted = false;
    switch (character) {
    case '(':
        token->type = LSQL_TOKEN_LEFT_PAREN;
        return true;
    case ')':
        token->type = LSQL_TOKEN_RIGHT_PAREN;
        return true;
    case ',':
        token->type = LSQL_TOKEN_COMMA;
        return true;
    case ';':
        token->type = LSQL_TOKEN_SEMICOLON;
        return true;
    case '*':
        token->type = LSQL_TOKEN_STAR;
        return true;
    case '-':
        token->type = LSQL_TOKEN_MINUS;
        return true;
    case '=':
        token->type = LSQL_TOKEN_EQUAL;
        return true;
    case '<':
        if (lexer->position < lexer->length && lexer->input[lexer->position] == '=') {
            lexer->position++;
            token->type = LSQL_TOKEN_LESS_EQUAL;
        } else if (lexer->position < lexer->length && lexer->input[lexer->position] == '>') {
            lexer->position++;
            token->type = LSQL_TOKEN_NOT_EQUAL;
        } else {
            token->type = LSQL_TOKEN_LESS;
        }
        return true;
    case '>':
        if (lexer->position < lexer->length && lexer->input[lexer->position] == '=') {
            lexer->position++;
            token->type = LSQL_TOKEN_GREATER_EQUAL;
        } else {
            token->type = LSQL_TOKEN_GREATER;
        }
        return true;
    case '!':
        if (lexer->position < lexer->length && lexer->input[lexer->position] == '=') {
            lexer->position++;
            token->type = LSQL_TOKEN_NOT_EQUAL;
            return true;
        }
        return lsql_lexer_fail(lexer, token->offset, "unexpected character '!'");
    default:
        break;
    }

    if (character >= 0x20 && character < 0x7F) {
        return lsql_lexer_fail(lexer, token->offset, "unexpected character '%c'", (int)character);
    }
    return lsql_lexer_fail(lexer, token->offset, "unexpected byte 0x%02X", (unsigned)character);
}

bool lsql_token_is_keyword(const lsql_token *token, const char *keyword) {
    return token != NULL && keyword != NULL && token->type == LSQL_TOKEN_IDENTIFIER &&
           !token->quoted && token->text != NULL && strcmp(token->text, keyword) == 0;
}
