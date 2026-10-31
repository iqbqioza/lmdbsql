#include "parser.h"

#include "lexer.h"

#include <string.h>

typedef struct lsql_parser {
    lsql_lexer lexer;
    lsql_token current;
    lsql_arena *arena;
    lsql_error *error;
    bool failed;
} lsql_parser;

static void parser_fail_v(lsql_parser *parser, lsql_status status, size_t offset,
                          const char *format, va_list arguments) LSQL_PRINTF(4, 0);
static void parser_fail(lsql_parser *parser, size_t offset, const char *format, ...)
    LSQL_PRINTF(3, 4);

static void parser_fail_v(lsql_parser *parser, lsql_status status, size_t offset,
                          const char *format, va_list arguments) {
    if (parser->failed) {
        return;
    }
    parser->failed = true;
    lsql_error_set_v(parser->error, status, offset, format, arguments);
}

static void parser_fail(lsql_parser *parser, size_t offset, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    parser_fail_v(parser, LSQL_ERROR_PARSE, offset, format, arguments);
    va_end(arguments);
}

static void parser_fail_oom(lsql_parser *parser, size_t offset) {
    if (parser->failed) {
        return;
    }
    parser->failed = true;
    lsql_error_set(parser->error, LSQL_ERROR_OUT_OF_MEMORY, offset, "out of memory");
}

static bool parser_advance(lsql_parser *parser) {
    if (parser->failed) {
        return false;
    }

    lsql_token token;
    if (!lsql_lexer_next(&parser->lexer, &token)) {
        parser->failed = true;
        if (parser->error->status == LSQL_OK) {
            lsql_error_set(parser->error, LSQL_ERROR_PARSE, parser->lexer.position,
                           "lexical error");
        }
        return false;
    }
    parser->current = token;
    return true;
}

static bool parser_match(lsql_parser *parser, lsql_token_type type) {
    if (parser->failed || parser->current.type != type) {
        return false;
    }
    return parser_advance(parser);
}

static bool parser_expect(lsql_parser *parser, lsql_token_type type, const char *description) {
    if (parser->failed) {
        return false;
    }
    if (parser->current.type != type) {
        parser_fail(parser, parser->current.offset, "expected %s", description);
        return false;
    }
    return parser_advance(parser);
}

static bool parser_match_keyword(lsql_parser *parser, const char *keyword) {
    if (parser->failed || !lsql_token_is_keyword(&parser->current, keyword)) {
        return false;
    }
    return parser_advance(parser);
}

static bool parser_expect_keyword(lsql_parser *parser, const char *keyword) {
    if (parser->failed) {
        return false;
    }
    if (!lsql_token_is_keyword(&parser->current, keyword)) {
        parser_fail(parser, parser->current.offset, "expected keyword '%s'", keyword);
        return false;
    }
    return parser_advance(parser);
}

static void *parser_alloc(lsql_parser *parser, size_t size, size_t offset) {
    void *memory = lsql_arena_alloc(parser->arena, size);
    if (memory == NULL) {
        parser_fail_oom(parser, offset);
    }
    return memory;
}

static const char *parser_take_identifier(lsql_parser *parser, const char *description) {
    if (parser->failed) {
        return NULL;
    }
    if (parser->current.type != LSQL_TOKEN_IDENTIFIER || parser->current.text == NULL) {
        parser_fail(parser, parser->current.offset, "expected %s", description);
        return NULL;
    }

    const char *name = parser->current.text;
    if (!lsql_identifier_is_valid(name, parser->current.length)) {
        parser_fail(parser, parser->current.offset, "invalid %s", description);
        return NULL;
    }
    if (!parser_advance(parser)) {
        return NULL;
    }
    return name;
}

static bool parser_parse_type(lsql_parser *parser, lsql_type *out_type) {
    if (parser->failed) {
        return false;
    }
    if (parser->current.type != LSQL_TOKEN_IDENTIFIER || parser->current.text == NULL) {
        parser_fail(parser, parser->current.offset, "expected a column type");
        return false;
    }

    size_t offset = parser->current.offset;
    lsql_type type;
    if (!lsql_type_from_name(parser->current.text, parser->current.length, &type)) {
        parser_fail(parser, offset, "unknown column type '%s'", parser->current.text);
        return false;
    }
    if (!parser_advance(parser)) {
        return false;
    }
    *out_type = type;
    return true;
}

static bool parser_parse_nonnegative_integer(lsql_parser *parser, int64_t *out_value) {
    if (parser->failed) {
        return false;
    }
    if (parser->current.type != LSQL_TOKEN_INTEGER) {
        parser_fail(parser, parser->current.offset, "expected an integer literal");
        return false;
    }
    *out_value = parser->current.integer;
    return parser_advance(parser);
}

static bool parser_parse_literal(lsql_parser *parser, lsql_value *out_value, size_t *out_offset) {
    if (parser->failed) {
        return false;
    }

    *out_offset = parser->current.offset;
    switch (parser->current.type) {
    case LSQL_TOKEN_INTEGER:
        out_value->type = LSQL_TYPE_INTEGER;
        out_value->as.integer = parser->current.integer;
        return parser_advance(parser);
    case LSQL_TOKEN_REAL:
        out_value->type = LSQL_TYPE_REAL;
        out_value->as.real = parser->current.real;
        return parser_advance(parser);
    case LSQL_TOKEN_STRING:
        out_value->type = LSQL_TYPE_TEXT;
        out_value->as.text.data = parser->current.text;
        out_value->as.text.length = parser->current.length;
        return parser_advance(parser);
    case LSQL_TOKEN_MINUS: {
        if (!parser_advance(parser)) {
            return false;
        }
        if (parser->current.type == LSQL_TOKEN_INTEGER) {
            out_value->type = LSQL_TYPE_INTEGER;
            out_value->as.integer = -parser->current.integer;
        } else if (parser->current.type == LSQL_TOKEN_REAL) {
            out_value->type = LSQL_TYPE_REAL;
            out_value->as.real = -parser->current.real;
        } else {
            parser_fail(parser, parser->current.offset, "expected a number after '-'");
            return false;
        }
        return parser_advance(parser);
    }
    default:
        break;
    }

    if (lsql_token_is_keyword(&parser->current, "null")) {
        out_value->type = LSQL_TYPE_NULL;
        return parser_advance(parser);
    }
    if (lsql_token_is_keyword(&parser->current, "true")) {
        out_value->type = LSQL_TYPE_INTEGER;
        out_value->as.integer = 1;
        return parser_advance(parser);
    }
    if (lsql_token_is_keyword(&parser->current, "false")) {
        out_value->type = LSQL_TYPE_INTEGER;
        out_value->as.integer = 0;
        return parser_advance(parser);
    }

    parser_fail(parser, parser->current.offset, "expected a literal value");
    return false;
}

static lsql_expression *parser_new_expression(lsql_parser *parser, lsql_expression_type type,
                                              size_t offset) {
    lsql_expression *expression = parser_alloc(parser, sizeof(*expression), offset);
    if (expression != NULL) {
        expression->type = type;
        expression->offset = offset;
    }
    return expression;
}

static lsql_expression *parser_parse_expression(lsql_parser *parser);

static lsql_expression *parser_parse_primary(lsql_parser *parser) {
    if (parser->failed) {
        return NULL;
    }

    if (parser_match(parser, LSQL_TOKEN_LEFT_PAREN)) {
        lsql_expression *expression = parser_parse_expression(parser);
        if (expression == NULL) {
            return NULL;
        }
        if (!parser_expect(parser, LSQL_TOKEN_RIGHT_PAREN, "')'")) {
            return NULL;
        }
        return expression;
    }

    bool literal_start =
        parser->current.type == LSQL_TOKEN_INTEGER || parser->current.type == LSQL_TOKEN_REAL ||
        parser->current.type == LSQL_TOKEN_STRING || parser->current.type == LSQL_TOKEN_MINUS ||
        lsql_token_is_keyword(&parser->current, "null") ||
        lsql_token_is_keyword(&parser->current, "true") ||
        lsql_token_is_keyword(&parser->current, "false");

    if (literal_start) {
        lsql_value value;
        size_t offset = parser->current.offset;
        if (!parser_parse_literal(parser, &value, &offset)) {
            return NULL;
        }
        lsql_expression *expression =
            parser_new_expression(parser, LSQL_EXPRESSION_LITERAL, offset);
        if (expression == NULL) {
            return NULL;
        }
        expression->as.literal = value;
        return expression;
    }

    if (parser->current.type == LSQL_TOKEN_IDENTIFIER) {
        size_t offset = parser->current.offset;
        const char *name = parser_take_identifier(parser, "column name");
        if (name == NULL) {
            return NULL;
        }
        lsql_expression *expression = parser_new_expression(parser, LSQL_EXPRESSION_COLUMN, offset);
        if (expression == NULL) {
            return NULL;
        }
        expression->as.column = name;
        return expression;
    }

    parser_fail(parser, parser->current.offset, "expected an expression");
    return NULL;
}

static bool token_binary_operator(lsql_token_type type, lsql_binary_operator *out_operator) {
    switch (type) {
    case LSQL_TOKEN_EQUAL:
        *out_operator = LSQL_BINARY_EQUAL;
        return true;
    case LSQL_TOKEN_NOT_EQUAL:
        *out_operator = LSQL_BINARY_NOT_EQUAL;
        return true;
    case LSQL_TOKEN_LESS:
        *out_operator = LSQL_BINARY_LESS;
        return true;
    case LSQL_TOKEN_LESS_EQUAL:
        *out_operator = LSQL_BINARY_LESS_EQUAL;
        return true;
    case LSQL_TOKEN_GREATER:
        *out_operator = LSQL_BINARY_GREATER;
        return true;
    case LSQL_TOKEN_GREATER_EQUAL:
        *out_operator = LSQL_BINARY_GREATER_EQUAL;
        return true;
    default:
        return false;
    }
}

static lsql_expression *parser_parse_comparison(lsql_parser *parser) {
    lsql_expression *left = parser_parse_primary(parser);
    if (left == NULL) {
        return NULL;
    }

    lsql_binary_operator op;
    if (token_binary_operator(parser->current.type, &op)) {
        size_t offset = parser->current.offset;
        if (!parser_advance(parser)) {
            return NULL;
        }
        lsql_expression *right = parser_parse_primary(parser);
        if (right == NULL) {
            return NULL;
        }
        lsql_expression *binary = parser_new_expression(parser, LSQL_EXPRESSION_BINARY, offset);
        if (binary == NULL) {
            return NULL;
        }
        binary->as.binary.left = left;
        binary->as.binary.right = right;
        binary->as.binary.op = op;
        left = binary;
    }

    if (lsql_token_is_keyword(&parser->current, "is")) {
        size_t offset = parser->current.offset;
        if (!parser_advance(parser)) {
            return NULL;
        }
        bool negated = parser_match_keyword(parser, "not");
        if (parser->failed) {
            return NULL;
        }
        if (!parser_expect_keyword(parser, "null")) {
            return NULL;
        }
        lsql_expression *is_null = parser_new_expression(parser, LSQL_EXPRESSION_IS_NULL, offset);
        if (is_null == NULL) {
            return NULL;
        }
        is_null->as.is_null.operand = left;
        is_null->as.is_null.negated = negated;
        left = is_null;
    }

    return left;
}

static lsql_expression *parser_parse_not(lsql_parser *parser) {
    if (lsql_token_is_keyword(&parser->current, "not")) {
        size_t offset = parser->current.offset;
        if (!parser_advance(parser)) {
            return NULL;
        }
        lsql_expression *operand = parser_parse_not(parser);
        if (operand == NULL) {
            return NULL;
        }
        lsql_expression *expression = parser_new_expression(parser, LSQL_EXPRESSION_NOT, offset);
        if (expression == NULL) {
            return NULL;
        }
        expression->as.operand = operand;
        return expression;
    }
    return parser_parse_comparison(parser);
}

static lsql_expression *parser_parse_and(lsql_parser *parser) {
    lsql_expression *left = parser_parse_not(parser);
    if (left == NULL) {
        return NULL;
    }

    while (!parser->failed && lsql_token_is_keyword(&parser->current, "and")) {
        size_t offset = parser->current.offset;
        if (!parser_advance(parser)) {
            return NULL;
        }
        lsql_expression *right = parser_parse_not(parser);
        if (right == NULL) {
            return NULL;
        }
        lsql_expression *binary = parser_new_expression(parser, LSQL_EXPRESSION_BINARY, offset);
        if (binary == NULL) {
            return NULL;
        }
        binary->as.binary.left = left;
        binary->as.binary.right = right;
        binary->as.binary.op = LSQL_BINARY_AND;
        left = binary;
    }
    return left;
}

static lsql_expression *parser_parse_expression(lsql_parser *parser) {
    lsql_expression *left = parser_parse_and(parser);
    if (left == NULL) {
        return NULL;
    }

    while (!parser->failed && lsql_token_is_keyword(&parser->current, "or")) {
        size_t offset = parser->current.offset;
        if (!parser_advance(parser)) {
            return NULL;
        }
        lsql_expression *right = parser_parse_and(parser);
        if (right == NULL) {
            return NULL;
        }
        lsql_expression *binary = parser_new_expression(parser, LSQL_EXPRESSION_BINARY, offset);
        if (binary == NULL) {
            return NULL;
        }
        binary->as.binary.left = left;
        binary->as.binary.right = right;
        binary->as.binary.op = LSQL_BINARY_OR;
        left = binary;
    }
    return left;
}

static lsql_statement *parser_new_statement(lsql_parser *parser, lsql_statement_type type) {
    lsql_statement *statement = parser_alloc(parser, sizeof(*statement), parser->current.offset);
    if (statement != NULL) {
        statement->type = type;
        statement->table = NULL;
        statement->table_offset = LSQL_OFFSET_NONE;
    }
    return statement;
}

static bool parser_table_exists_in_list(const lsql_column_definition *columns, size_t count,
                                        const char *name) {
    for (size_t index = 0; index < count; ++index) {
        if (strcmp(columns[index].name, name) == 0) {
            return true;
        }
    }
    return false;
}

static lsql_statement *parser_parse_create(lsql_parser *parser) {
    lsql_statement *statement = parser_new_statement(parser, LSQL_STATEMENT_CREATE_TABLE);
    if (statement == NULL) {
        return NULL;
    }

    if (!parser_expect_keyword(parser, "table")) {
        return NULL;
    }

    bool if_not_exists = false;
    if (parser_match_keyword(parser, "if")) {
        if (!parser_expect_keyword(parser, "not") || !parser_expect_keyword(parser, "exists")) {
            return NULL;
        }
        if_not_exists = true;
    }

    size_t table_offset = parser->current.offset;
    const char *table = parser_take_identifier(parser, "table name");
    if (table == NULL) {
        return NULL;
    }
    statement->table = table;
    statement->table_offset = table_offset;

    if (!parser_expect(parser, LSQL_TOKEN_LEFT_PAREN, "'('")) {
        return NULL;
    }

    lsql_column_definition *columns =
        parser_alloc(parser, LSQL_MAX_COLUMNS * sizeof(*columns), table_offset);
    if (columns == NULL) {
        return NULL;
    }

    size_t count = 0;
    for (;;) {
        size_t column_offset = parser->current.offset;
        const char *name = parser_take_identifier(parser, "column name");
        if (name == NULL) {
            return NULL;
        }
        if (strcmp(name, "rowid") == 0) {
            parser_fail(parser, column_offset, "column name 'rowid' is reserved");
            return NULL;
        }
        if (parser_table_exists_in_list(columns, count, name)) {
            parser_fail(parser, column_offset, "duplicate column name '%s'", name);
            return NULL;
        }

        lsql_type type;
        if (!parser_parse_type(parser, &type)) {
            return NULL;
        }
        if (count == LSQL_MAX_COLUMNS) {
            parser_fail(parser, column_offset, "too many columns (maximum %u)",
                        (unsigned)LSQL_MAX_COLUMNS);
            return NULL;
        }
        columns[count].name = name;
        columns[count].type = type;
        columns[count].offset = column_offset;
        count++;

        if (parser_match(parser, LSQL_TOKEN_COMMA)) {
            continue;
        }
        if (parser_match(parser, LSQL_TOKEN_RIGHT_PAREN)) {
            break;
        }
        parser_fail(parser, parser->current.offset, "expected ',' or ')'");
        return NULL;
    }

    if (count == 0) {
        parser_fail(parser, table_offset, "a table must have at least one column");
        return NULL;
    }

    statement->as.create.columns = columns;
    statement->as.create.count = count;
    statement->as.create.if_not_exists = if_not_exists;
    return statement;
}

static lsql_statement *parser_parse_drop(lsql_parser *parser) {
    lsql_statement *statement = parser_new_statement(parser, LSQL_STATEMENT_DROP_TABLE);
    if (statement == NULL) {
        return NULL;
    }
    if (!parser_expect_keyword(parser, "table")) {
        return NULL;
    }

    bool if_exists = false;
    if (parser_match_keyword(parser, "if")) {
        if (!parser_expect_keyword(parser, "exists")) {
            return NULL;
        }
        if_exists = true;
    }

    size_t table_offset = parser->current.offset;
    const char *table = parser_take_identifier(parser, "table name");
    if (table == NULL) {
        return NULL;
    }
    statement->table = table;
    statement->table_offset = table_offset;
    statement->as.drop.if_exists = if_exists;
    return statement;
}

static lsql_statement *parser_parse_insert(lsql_parser *parser) {
    lsql_statement *statement = parser_new_statement(parser, LSQL_STATEMENT_INSERT);
    if (statement == NULL) {
        return NULL;
    }
    if (!parser_expect_keyword(parser, "into")) {
        return NULL;
    }

    size_t table_offset = parser->current.offset;
    const char *table = parser_take_identifier(parser, "table name");
    if (table == NULL) {
        return NULL;
    }
    statement->table = table;
    statement->table_offset = table_offset;

    const char **columns = NULL;
    size_t *column_offsets = NULL;
    size_t column_count = 0;

    if (parser_match(parser, LSQL_TOKEN_LEFT_PAREN)) {
        columns =
            (const char **)parser_alloc(parser, LSQL_MAX_COLUMNS * sizeof(*columns), table_offset);
        column_offsets =
            parser_alloc(parser, LSQL_MAX_COLUMNS * sizeof(*column_offsets), table_offset);
        if (columns == NULL || column_offsets == NULL) {
            return NULL;
        }

        for (;;) {
            size_t offset = parser->current.offset;
            const char *name = parser_take_identifier(parser, "column name");
            if (name == NULL) {
                return NULL;
            }
            if (strcmp(name, "rowid") == 0) {
                parser_fail(parser, offset, "column 'rowid' cannot be inserted explicitly");
                return NULL;
            }
            if (column_count == LSQL_MAX_COLUMNS) {
                parser_fail(parser, offset, "too many columns (maximum %u)",
                            (unsigned)LSQL_MAX_COLUMNS);
                return NULL;
            }
            columns[column_count] = name;
            column_offsets[column_count] = offset;
            column_count++;

            if (parser_match(parser, LSQL_TOKEN_COMMA)) {
                continue;
            }
            if (!parser_expect(parser, LSQL_TOKEN_RIGHT_PAREN, "')'")) {
                return NULL;
            }
            break;
        }
    }

    if (!parser_expect_keyword(parser, "values")) {
        return NULL;
    }
    if (!parser_expect(parser, LSQL_TOKEN_LEFT_PAREN, "'('")) {
        return NULL;
    }

    lsql_value *values = parser_alloc(parser, LSQL_MAX_COLUMNS * sizeof(*values), table_offset);
    size_t *value_offsets =
        parser_alloc(parser, LSQL_MAX_COLUMNS * sizeof(*value_offsets), table_offset);
    if (values == NULL || value_offsets == NULL) {
        return NULL;
    }

    size_t value_count = 0;
    for (;;) {
        if (value_count == LSQL_MAX_COLUMNS) {
            parser_fail(parser, parser->current.offset, "too many values (maximum %u)",
                        (unsigned)LSQL_MAX_COLUMNS);
            return NULL;
        }
        lsql_value value;
        size_t offset;
        if (!parser_parse_literal(parser, &value, &offset)) {
            return NULL;
        }
        values[value_count] = value;
        value_offsets[value_count] = offset;
        value_count++;

        if (parser_match(parser, LSQL_TOKEN_COMMA)) {
            continue;
        }
        if (!parser_expect(parser, LSQL_TOKEN_RIGHT_PAREN, "')'")) {
            return NULL;
        }
        break;
    }

    if (column_count != 0 && column_count != value_count) {
        parser_fail(parser, table_offset, "column count (%zu) does not match value count (%zu)",
                    column_count, value_count);
        return NULL;
    }

    statement->as.insert.columns = columns;
    statement->as.insert.column_offsets = column_offsets;
    statement->as.insert.column_count = column_count;
    statement->as.insert.values = values;
    statement->as.insert.value_offsets = value_offsets;
    statement->as.insert.value_count = value_count;
    return statement;
}

static lsql_statement *parser_parse_select(lsql_parser *parser) {
    lsql_statement *statement = parser_new_statement(parser, LSQL_STATEMENT_SELECT);
    if (statement == NULL) {
        return NULL;
    }

    bool star = false;
    const char **columns = NULL;
    size_t *column_offsets = NULL;
    size_t column_count = 0;

    if (parser_match(parser, LSQL_TOKEN_STAR)) {
        star = true;
    } else {
        columns = (const char **)parser_alloc(parser, LSQL_MAX_COLUMNS * sizeof(*columns),
                                              parser->current.offset);
        column_offsets = parser_alloc(parser, LSQL_MAX_COLUMNS * sizeof(*column_offsets),
                                      parser->current.offset);
        if (columns == NULL || column_offsets == NULL) {
            return NULL;
        }

        for (;;) {
            size_t offset = parser->current.offset;
            const char *name = parser_take_identifier(parser, "column name");
            if (name == NULL) {
                return NULL;
            }
            if (column_count == LSQL_MAX_COLUMNS) {
                parser_fail(parser, offset, "too many columns (maximum %u)",
                            (unsigned)LSQL_MAX_COLUMNS);
                return NULL;
            }
            columns[column_count] = name;
            column_offsets[column_count] = offset;
            column_count++;

            if (parser_match(parser, LSQL_TOKEN_COMMA)) {
                continue;
            }
            break;
        }
    }

    if (!parser_expect_keyword(parser, "from")) {
        return NULL;
    }

    size_t table_offset = parser->current.offset;
    const char *table = parser_take_identifier(parser, "table name");
    if (table == NULL) {
        return NULL;
    }
    statement->table = table;
    statement->table_offset = table_offset;

    lsql_expression *where = NULL;
    if (parser_match_keyword(parser, "where")) {
        where = parser_parse_expression(parser);
        if (where == NULL) {
            return NULL;
        }
    }

    const char *order_by = NULL;
    size_t order_by_offset = LSQL_OFFSET_NONE;
    bool order_descending = false;
    if (parser_match_keyword(parser, "order")) {
        if (!parser_expect_keyword(parser, "by")) {
            return NULL;
        }
        order_by_offset = parser->current.offset;
        order_by = parser_take_identifier(parser, "column name");
        if (order_by == NULL) {
            return NULL;
        }
        if (parser_match_keyword(parser, "desc")) {
            order_descending = true;
        } else {
            (void)parser_match_keyword(parser, "asc");
        }
    }

    bool has_limit = false;
    int64_t limit = 0;
    bool has_offset = false;
    int64_t offset = 0;

    for (;;) {
        if (parser->failed) {
            return NULL;
        }
        if (lsql_token_is_keyword(&parser->current, "limit")) {
            if (has_limit) {
                parser_fail(parser, parser->current.offset, "LIMIT specified more than once");
                return NULL;
            }
            if (!parser_advance(parser) || !parser_parse_nonnegative_integer(parser, &limit)) {
                return NULL;
            }
            has_limit = true;
            continue;
        }
        if (lsql_token_is_keyword(&parser->current, "offset")) {
            if (has_offset) {
                parser_fail(parser, parser->current.offset, "OFFSET specified more than once");
                return NULL;
            }
            if (!parser_advance(parser) || !parser_parse_nonnegative_integer(parser, &offset)) {
                return NULL;
            }
            has_offset = true;
            continue;
        }
        break;
    }

    statement->as.select.star = star;
    statement->as.select.columns = columns;
    statement->as.select.column_offsets = column_offsets;
    statement->as.select.column_count = column_count;
    statement->as.select.where = where;
    statement->as.select.order_by = order_by;
    statement->as.select.order_by_offset = order_by_offset;
    statement->as.select.order_descending = order_descending;
    statement->as.select.has_limit = has_limit;
    statement->as.select.limit = limit;
    statement->as.select.has_offset = has_offset;
    statement->as.select.offset = offset;
    return statement;
}

static lsql_statement *parser_parse_delete(lsql_parser *parser) {
    lsql_statement *statement = parser_new_statement(parser, LSQL_STATEMENT_DELETE);
    if (statement == NULL) {
        return NULL;
    }
    if (!parser_expect_keyword(parser, "from")) {
        return NULL;
    }

    size_t table_offset = parser->current.offset;
    const char *table = parser_take_identifier(parser, "table name");
    if (table == NULL) {
        return NULL;
    }
    statement->table = table;
    statement->table_offset = table_offset;

    lsql_expression *where = NULL;
    if (parser_match_keyword(parser, "where")) {
        where = parser_parse_expression(parser);
        if (where == NULL) {
            return NULL;
        }
    }
    statement->as.delete.where = where;
    return statement;
}

static lsql_statement *parser_parse_statement(lsql_parser *parser) {
    if (parser->failed) {
        return NULL;
    }
    if (parser_match_keyword(parser, "create")) {
        return parser_parse_create(parser);
    }
    if (parser_match_keyword(parser, "drop")) {
        return parser_parse_drop(parser);
    }
    if (parser_match_keyword(parser, "insert")) {
        return parser_parse_insert(parser);
    }
    if (parser_match_keyword(parser, "select")) {
        return parser_parse_select(parser);
    }
    if (parser_match_keyword(parser, "delete")) {
        return parser_parse_delete(parser);
    }

    if (parser->current.type == LSQL_TOKEN_EOF) {
        parser_fail(parser, parser->current.offset, "expected a SQL statement");
    } else {
        parser_fail(parser, parser->current.offset,
                    "expected SELECT, INSERT, CREATE, DROP, or DELETE");
    }
    return NULL;
}

lsql_status lsql_parse(const char *sql, size_t length, lsql_arena *arena,
                       lsql_statement **out_statement, lsql_error *error) {
    if (out_statement == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }
    *out_statement = NULL;
    if (sql == NULL || arena == NULL || error == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }

    lsql_error_clear(error);

    lsql_parser parser;
    parser.arena = arena;
    parser.error = error;
    parser.failed = false;
    memset(&parser.current, 0, sizeof(parser.current));
    lsql_lexer_init(&parser.lexer, sql, length, arena, error);

    if (!parser_advance(&parser)) {
        return error->status;
    }

    lsql_statement *statement = parser_parse_statement(&parser);
    if (statement == NULL) {
        if (error->status == LSQL_OK) {
            lsql_error_set(error, LSQL_ERROR_PARSE, 0, "invalid SQL statement");
        }
        return error->status;
    }

    while (parser_match(&parser, LSQL_TOKEN_SEMICOLON)) {
    }
    if (!parser.failed && parser.current.type != LSQL_TOKEN_EOF) {
        parser_fail(&parser, parser.current.offset, "unexpected token after statement");
    }
    if (parser.failed) {
        return error->status;
    }

    *out_statement = statement;
    return LSQL_OK;
}
