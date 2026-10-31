#ifndef LMDBSQL_PARSER_H
#define LMDBSQL_PARSER_H

#include "internal.h"

typedef enum lsql_statement_type {
    LSQL_STATEMENT_CREATE_TABLE,
    LSQL_STATEMENT_DROP_TABLE,
    LSQL_STATEMENT_INSERT,
    LSQL_STATEMENT_SELECT,
    LSQL_STATEMENT_DELETE
} lsql_statement_type;

typedef struct lsql_column_definition {
    const char *name;
    lsql_type type;
    size_t offset;
} lsql_column_definition;

typedef enum lsql_binary_operator {
    LSQL_BINARY_EQUAL,
    LSQL_BINARY_NOT_EQUAL,
    LSQL_BINARY_LESS,
    LSQL_BINARY_LESS_EQUAL,
    LSQL_BINARY_GREATER,
    LSQL_BINARY_GREATER_EQUAL,
    LSQL_BINARY_AND,
    LSQL_BINARY_OR
} lsql_binary_operator;

typedef enum lsql_expression_type {
    LSQL_EXPRESSION_LITERAL,
    LSQL_EXPRESSION_COLUMN,
    LSQL_EXPRESSION_BINARY,
    LSQL_EXPRESSION_NOT,
    LSQL_EXPRESSION_NEGATE,
    LSQL_EXPRESSION_IS_NULL
} lsql_expression_type;

typedef struct lsql_expression {
    lsql_expression_type type;
    size_t offset;
    union {
        lsql_value literal;
        const char *column;
        struct {
            struct lsql_expression *left;
            struct lsql_expression *right;
            lsql_binary_operator op;
        } binary;
        struct lsql_expression *operand;
        struct {
            struct lsql_expression *operand;
            bool negated;
        } is_null;
    } as;
} lsql_expression;

typedef struct lsql_statement {
    lsql_statement_type type;
    const char *table;
    size_t table_offset;
    union {
        struct {
            lsql_column_definition *columns;
            size_t count;
            bool if_not_exists;
        } create;
        struct {
            bool if_exists;
        } drop;
        struct {
            const char **columns;
            size_t *column_offsets;
            size_t column_count;
            lsql_value *values;
            size_t *value_offsets;
            size_t value_count;
        } insert;
        struct {
            bool star;
            const char **columns;
            size_t *column_offsets;
            size_t column_count;
            lsql_expression *where;
            const char *order_by;
            size_t order_by_offset;
            bool order_descending;
            bool has_limit;
            int64_t limit;
            bool has_offset;
            int64_t offset;
        } select;
        struct {
            lsql_expression *where;
        } delete;
    } as;
} lsql_statement;

lsql_status lsql_parse(const char *sql, size_t length, lsql_arena *arena,
                       lsql_statement **out_statement, lsql_error *error);

#endif /* LMDBSQL_PARSER_H */
