#include "engine.h"

#include "row.h"
#include "schema.h"

#include <stdlib.h>
#include <string.h>

#define LSQL_CATALOG_SCHEMA_PREFIX 0x01u
#define LSQL_CATALOG_COUNTER_PREFIX 0x02u
#define LSQL_TABLE_DB_PREFIX "t:"
#define LSQL_CATALOG_KEY_CAPACITY (1u + LSQL_MAX_IDENTIFIER_LENGTH)
#define LSQL_TABLE_DB_NAME_CAPACITY (sizeof(LSQL_TABLE_DB_PREFIX) + LSQL_MAX_IDENTIFIER_LENGTH)

typedef struct selected_row {
    uint64_t rowid;
    lsql_value *values;
} selected_row;

static void lsql_write_u64(unsigned char *data, uint64_t value) {
    data[0] = (unsigned char)(value >> 56);
    data[1] = (unsigned char)((value >> 48) & 0xFFu);
    data[2] = (unsigned char)((value >> 40) & 0xFFu);
    data[3] = (unsigned char)((value >> 32) & 0xFFu);
    data[4] = (unsigned char)((value >> 24) & 0xFFu);
    data[5] = (unsigned char)((value >> 16) & 0xFFu);
    data[6] = (unsigned char)((value >> 8) & 0xFFu);
    data[7] = (unsigned char)(value & 0xFFu);
}

static uint64_t lsql_read_u64(const unsigned char *data) {
    return ((uint64_t)data[0] << 56) | ((uint64_t)data[1] << 48) | ((uint64_t)data[2] << 40) |
           ((uint64_t)data[3] << 32) | ((uint64_t)data[4] << 24) | ((uint64_t)data[5] << 16) |
           ((uint64_t)data[6] << 8) | (uint64_t)data[7];
}

static size_t lsql_catalog_key(unsigned char *key, unsigned char prefix, const char *table) {
    size_t length = strlen(table);
    key[0] = prefix;
    // Binary catalog key, length-delimited rather than NUL-terminated.
    // NOLINTNEXTLINE(bugprone-not-null-terminated-result)
    memcpy(key + 1u, table, length);
    return length + 1u;
}

static void lsql_table_db_name(char *name, const char *table) {
    size_t length = strlen(table);
    memcpy(name, LSQL_TABLE_DB_PREFIX, sizeof(LSQL_TABLE_DB_PREFIX) - 1u);
    memcpy(name + sizeof(LSQL_TABLE_DB_PREFIX) - 1u, table, length);
    name[sizeof(LSQL_TABLE_DB_PREFIX) - 1u + length] = '\0';
}

static size_t lsql_clamp_size(int64_t value) {
    uint64_t unsigned_value = (uint64_t)value;
    if (unsigned_value > (uint64_t)SIZE_MAX) {
        return SIZE_MAX;
    }
    return (size_t)unsigned_value;
}

static lsql_status lsql_require_writable(const lsql_conn *conn, size_t offset, lsql_error *error) {
    if (conn->read_only) {
        return lsql_error_set(error, LSQL_ERROR_READ_ONLY, offset,
                              "database is opened in read-only mode");
    }
    return LSQL_OK;
}

static lsql_status lsql_begin_transaction(lsql_conn *conn, unsigned flags, MDB_txn **out_txn,
                                          size_t offset, lsql_error *error) {
    int rc = mdb_txn_begin(conn->env, NULL, flags, out_txn);
    if (rc != 0) {
        return lsql_error_from_lmdb(error, rc, offset, "cannot begin transaction");
    }
    return LSQL_OK;
}

static lsql_status lsql_commit_transaction(MDB_txn *txn, size_t offset, lsql_error *error) {
    int rc = mdb_txn_commit(txn);
    if (rc != 0) {
        return lsql_error_from_lmdb(error, rc, offset, "cannot commit transaction");
    }
    return LSQL_OK;
}

static lsql_status lsql_open_table(MDB_txn *txn, const char *table, bool create, MDB_dbi *out_dbi,
                                   size_t offset, lsql_error *error) {
    char name[LSQL_TABLE_DB_NAME_CAPACITY];
    lsql_table_db_name(name, table);

    int rc = mdb_dbi_open(txn, name, create ? MDB_CREATE : 0, out_dbi);
    if (rc == MDB_NOTFOUND) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, offset,
                              "table storage for '%s' is missing", table);
    }
    if (rc != 0) {
        return lsql_error_from_lmdb(error, rc, offset, "cannot open table storage");
    }
    return LSQL_OK;
}

static lsql_status lsql_schema_exists(lsql_conn *conn, MDB_txn *txn, const char *table,
                                      bool *out_exists, size_t offset, lsql_error *error) {
    unsigned char key[LSQL_CATALOG_KEY_CAPACITY];
    size_t key_length = lsql_catalog_key(key, LSQL_CATALOG_SCHEMA_PREFIX, table);
    MDB_val key_value = {.mv_size = key_length, .mv_data = key};
    MDB_val data_value;

    int rc = mdb_get(txn, conn->catalog, &key_value, &data_value);
    if (rc == 0) {
        *out_exists = true;
        return LSQL_OK;
    }
    if (rc == MDB_NOTFOUND) {
        *out_exists = false;
        return LSQL_OK;
    }
    return lsql_error_from_lmdb(error, rc, offset, "cannot read table catalog");
}

static lsql_status lsql_load_schema(lsql_conn *conn, MDB_txn *txn, const char *table,
                                    lsql_arena *arena, lsql_schema *out_schema, bool *out_found,
                                    size_t offset, lsql_error *error) {
    unsigned char key[LSQL_CATALOG_KEY_CAPACITY];
    size_t key_length = lsql_catalog_key(key, LSQL_CATALOG_SCHEMA_PREFIX, table);
    MDB_val key_value = {.mv_size = key_length, .mv_data = key};
    MDB_val data_value;

    int rc = mdb_get(txn, conn->catalog, &key_value, &data_value);
    if (rc == MDB_NOTFOUND) {
        *out_found = false;
        return LSQL_OK;
    }
    if (rc != 0) {
        return lsql_error_from_lmdb(error, rc, offset, "cannot read table catalog");
    }

    *out_found = true;
    return lsql_schema_decode(data_value.mv_data, data_value.mv_size, arena, out_schema, error);
}

static lsql_status lsql_read_counter(lsql_conn *conn, MDB_txn *txn, const char *table,
                                     uint64_t *out_counter, size_t offset, lsql_error *error) {
    unsigned char key[LSQL_CATALOG_KEY_CAPACITY];
    size_t key_length = lsql_catalog_key(key, LSQL_CATALOG_COUNTER_PREFIX, table);
    MDB_val key_value = {.mv_size = key_length, .mv_data = key};
    MDB_val data_value;

    int rc = mdb_get(txn, conn->catalog, &key_value, &data_value);
    if (rc == MDB_NOTFOUND) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, offset,
                              "row counter for table '%s' is missing", table);
    }
    if (rc != 0) {
        return lsql_error_from_lmdb(error, rc, offset, "cannot read row counter");
    }
    if (data_value.mv_size != 8u) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, offset,
                              "corrupt row counter for table '%s'", table);
    }

    *out_counter = lsql_read_u64(data_value.mv_data);
    return LSQL_OK;
}

static lsql_status lsql_write_counter(lsql_conn *conn, MDB_txn *txn, const char *table,
                                      uint64_t counter, size_t offset, lsql_error *error) {
    unsigned char key[LSQL_CATALOG_KEY_CAPACITY];
    unsigned char value[8];
    size_t key_length = lsql_catalog_key(key, LSQL_CATALOG_COUNTER_PREFIX, table);
    lsql_write_u64(value, counter);

    MDB_val key_value = {.mv_size = key_length, .mv_data = key};
    MDB_val data_value = {.mv_size = sizeof(value), .mv_data = value};
    int rc = mdb_put(txn, conn->catalog, &key_value, &data_value, 0);
    if (rc != 0) {
        return lsql_error_from_lmdb(error, rc, offset, "cannot update row counter");
    }
    return LSQL_OK;
}

static lsql_status lsql_make_change_result(size_t affected, lsql_result **out_result,
                                           lsql_error *error) {
    lsql_result *result;
    lsql_status status = lsql_result_create(LSQL_RESULT_CHANGE, 0, &result, error);
    if (status != LSQL_OK) {
        return status;
    }
    result->affected_rows = affected;
    *out_result = result;
    return LSQL_OK;
}

static lsql_status lsql_validate_expression(const lsql_expression *expression,
                                            const lsql_schema *schema, lsql_error *error) {
    if (expression == NULL) {
        return LSQL_OK;
    }

    switch (expression->type) {
    case LSQL_EXPRESSION_LITERAL:
        return LSQL_OK;
    case LSQL_EXPRESSION_COLUMN:
        if (strcmp(expression->as.column, "rowid") != 0 &&
            lsql_schema_find_column(schema, expression->as.column) < 0) {
            return lsql_error_set(error, LSQL_ERROR_NOT_FOUND, expression->offset,
                                  "no such column '%s'", expression->as.column);
        }
        return LSQL_OK;
    case LSQL_EXPRESSION_BINARY: {
        lsql_status status = lsql_validate_expression(expression->as.binary.left, schema, error);
        if (status != LSQL_OK) {
            return status;
        }
        return lsql_validate_expression(expression->as.binary.right, schema, error);
    }
    case LSQL_EXPRESSION_NOT:
    case LSQL_EXPRESSION_NEGATE:
        return lsql_validate_expression(expression->as.operand, schema, error);
    case LSQL_EXPRESSION_IS_NULL:
        return lsql_validate_expression(expression->as.is_null.operand, schema, error);
    }
    return lsql_error_set(error, LSQL_ERROR_CORRUPT, expression->offset, "invalid expression");
}

static lsql_status lsql_evaluate_expression(const lsql_expression *expression,
                                            const lsql_schema *schema, uint64_t rowid,
                                            const lsql_value *values, lsql_value *out_value,
                                            lsql_error *error) {
    switch (expression->type) {
    case LSQL_EXPRESSION_LITERAL:
        *out_value = expression->as.literal;
        return LSQL_OK;
    case LSQL_EXPRESSION_COLUMN:
        if (strcmp(expression->as.column, "rowid") == 0) {
            out_value->type = LSQL_TYPE_INTEGER;
            out_value->as.integer = (int64_t)rowid;
            return LSQL_OK;
        }
        int index = lsql_schema_find_column(schema, expression->as.column);
        if (index < 0) {
            return lsql_error_set(error, LSQL_ERROR_NOT_FOUND, expression->offset,
                                  "no such column '%s'", expression->as.column);
        }
        *out_value = values[index];
        return LSQL_OK;
    case LSQL_EXPRESSION_NOT: {
        lsql_value operand;
        lsql_status status = lsql_evaluate_expression(expression->as.operand, schema, rowid, values,
                                                      &operand, error);
        if (status != LSQL_OK) {
            return status;
        }
        out_value->type = LSQL_TYPE_INTEGER;
        out_value->as.integer = lsql_value_truthy(&operand) ? 0 : 1;
        return LSQL_OK;
    }
    case LSQL_EXPRESSION_NEGATE: {
        lsql_value operand;
        lsql_status status = lsql_evaluate_expression(expression->as.operand, schema, rowid, values,
                                                      &operand, error);
        if (status != LSQL_OK) {
            return status;
        }
        if (operand.type == LSQL_TYPE_NULL) {
            *out_value = operand;
            return LSQL_OK;
        }
        if (operand.type == LSQL_TYPE_INTEGER) {
            if (operand.as.integer == INT64_MIN) {
                return lsql_error_set(error, LSQL_ERROR_TYPE, expression->offset,
                                      "integer overflow in unary minus");
            }
            out_value->type = LSQL_TYPE_INTEGER;
            out_value->as.integer = -operand.as.integer;
            return LSQL_OK;
        }
        if (operand.type == LSQL_TYPE_REAL) {
            out_value->type = LSQL_TYPE_REAL;
            out_value->as.real = -operand.as.real;
            return LSQL_OK;
        }
        return lsql_error_set(error, LSQL_ERROR_TYPE, expression->offset,
                              "unary minus requires a numeric value");
    }
    case LSQL_EXPRESSION_IS_NULL: {
        lsql_value operand;
        lsql_status status = lsql_evaluate_expression(expression->as.is_null.operand, schema, rowid,
                                                      values, &operand, error);
        if (status != LSQL_OK) {
            return status;
        }
        bool is_null = lsql_value_is_null(&operand);
        out_value->type = LSQL_TYPE_INTEGER;
        out_value->as.integer = (is_null != expression->as.is_null.negated) ? 1 : 0;
        return LSQL_OK;
    }
    case LSQL_EXPRESSION_BINARY:
        break;
    }

    lsql_binary_operator op = expression->as.binary.op;
    if (op == LSQL_BINARY_AND || op == LSQL_BINARY_OR) {
        lsql_value left;
        lsql_status status = lsql_evaluate_expression(expression->as.binary.left, schema, rowid,
                                                      values, &left, error);
        if (status != LSQL_OK) {
            return status;
        }
        bool left_truth = lsql_value_truthy(&left);
        if (op == LSQL_BINARY_AND && !left_truth) {
            out_value->type = LSQL_TYPE_INTEGER;
            out_value->as.integer = 0;
            return LSQL_OK;
        }
        if (op == LSQL_BINARY_OR && left_truth) {
            out_value->type = LSQL_TYPE_INTEGER;
            out_value->as.integer = 1;
            return LSQL_OK;
        }

        lsql_value right;
        status = lsql_evaluate_expression(expression->as.binary.right, schema, rowid, values,
                                          &right, error);
        if (status != LSQL_OK) {
            return status;
        }
        out_value->type = LSQL_TYPE_INTEGER;
        out_value->as.integer = lsql_value_truthy(&right) ? 1 : 0;
        return LSQL_OK;
    }

    lsql_value left;
    lsql_value right;
    lsql_status status =
        lsql_evaluate_expression(expression->as.binary.left, schema, rowid, values, &left, error);
    if (status != LSQL_OK) {
        return status;
    }
    status =
        lsql_evaluate_expression(expression->as.binary.right, schema, rowid, values, &right, error);
    if (status != LSQL_OK) {
        return status;
    }

    int64_t result = 0;
    bool comparable = false;
    int comparison = lsql_value_compare(&left, &right, &comparable);
    if (comparable) {
        switch (op) {
        case LSQL_BINARY_EQUAL:
            result = comparison == 0;
            break;
        case LSQL_BINARY_NOT_EQUAL:
            result = comparison != 0;
            break;
        case LSQL_BINARY_LESS:
            result = comparison < 0;
            break;
        case LSQL_BINARY_LESS_EQUAL:
            result = comparison <= 0;
            break;
        case LSQL_BINARY_GREATER:
            result = comparison > 0;
            break;
        case LSQL_BINARY_GREATER_EQUAL:
            result = comparison >= 0;
            break;
        case LSQL_BINARY_AND:
        case LSQL_BINARY_OR:
            break;
        }
    }

    out_value->type = LSQL_TYPE_INTEGER;
    out_value->as.integer = result;
    return LSQL_OK;
}

static lsql_status lsql_row_matches(const lsql_expression *where, const lsql_schema *schema,
                                    uint64_t rowid, const lsql_value *values, bool *out_matches,
                                    lsql_error *error) {
    if (where == NULL) {
        *out_matches = true;
        return LSQL_OK;
    }

    lsql_value predicate;
    lsql_status status = lsql_evaluate_expression(where, schema, rowid, values, &predicate, error);
    if (status != LSQL_OK) {
        return status;
    }
    *out_matches = lsql_value_truthy(&predicate);
    return LSQL_OK;
}

static lsql_value lsql_row_value_at(const selected_row *row, int column) {
    if (column < 0) {
        lsql_value value;
        value.type = LSQL_TYPE_INTEGER;
        value.as.integer = (int64_t)row->rowid;
        return value;
    }
    return row->values[column];
}

static int lsql_compare_rows(const selected_row *left, const selected_row *right, int column) {
    lsql_value left_value = lsql_row_value_at(left, column);
    lsql_value right_value = lsql_row_value_at(right, column);

    bool left_null = lsql_value_is_null(&left_value);
    bool right_null = lsql_value_is_null(&right_value);
    if (left_null && right_null) {
        return 0;
    }
    if (left_null) {
        return -1;
    }
    if (right_null) {
        return 1;
    }

    bool comparable = false;
    int comparison = lsql_value_compare(&left_value, &right_value, &comparable);
    return comparable ? comparison : 0;
}

static void lsql_sort_rows(selected_row *rows, selected_row *temporary, size_t count, int column,
                           bool descending) {
    if (count < 2) {
        return;
    }

    size_t middle = count / 2u;
    lsql_sort_rows(rows, temporary, middle, column, descending);
    lsql_sort_rows(rows + middle, temporary + middle, count - middle, column, descending);

    size_t left = 0;
    size_t right = middle;
    size_t output = 0;
    while (left < middle && right < count) {
        int comparison = lsql_compare_rows(&rows[left], &rows[right], column);
        if (descending) {
            comparison = -comparison;
        }
        if (comparison <= 0) {
            temporary[output++] = rows[left++];
        } else {
            temporary[output++] = rows[right++];
        }
    }
    while (left < middle) {
        temporary[output++] = rows[left++];
    }
    while (right < count) {
        temporary[output++] = rows[right++];
    }
    memcpy(rows, temporary, count * sizeof(*rows));
}

static void lsql_free_selected_rows(selected_row *rows, size_t count) {
    if (rows == NULL) {
        return;
    }
    for (size_t index = 0; index < count; ++index) {
        free(rows[index].values);
    }
    free(rows);
}

static lsql_status lsql_copy_value(lsql_value *destination, const lsql_value *source,
                                   lsql_arena *arena, lsql_error *error) {
    *destination = *source;
    if (source->type == LSQL_TYPE_TEXT) {
        const char *copy = lsql_arena_strndup(arena, source->as.text.data, source->as.text.length);
        if (copy == NULL) {
            return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                  "out of memory while copying result value");
        }
        destination->as.text.data = copy;
    }
    return LSQL_OK;
}

static lsql_status lsql_execute_create(lsql_conn *conn, const lsql_statement *statement,
                                       lsql_result **out_result, lsql_error *error) {
    lsql_status status = lsql_require_writable(conn, statement->table_offset, error);
    if (status != LSQL_OK) {
        return status;
    }

    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_schema schema;
    status = lsql_schema_from_definitions(statement->as.create.columns, statement->as.create.count,
                                          &arena, &schema, error);
    if (status != LSQL_OK) {
        lsql_arena_destroy(&arena);
        return status;
    }

    unsigned char *encoded = NULL;
    size_t encoded_size = 0;
    status = lsql_schema_encode(&schema, &encoded, &encoded_size, error);
    if (status != LSQL_OK) {
        lsql_arena_destroy(&arena);
        return status;
    }

    MDB_txn *txn = NULL;
    MDB_dbi dbi = 0;
    bool dbi_open = false;
    status = lsql_begin_transaction(conn, 0, &txn, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }

    bool exists = false;
    status =
        lsql_schema_exists(conn, txn, statement->table, &exists, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    if (exists) {
        if (!statement->as.create.if_not_exists) {
            status = lsql_error_set(error, LSQL_ERROR_ALREADY_EXISTS, statement->table_offset,
                                    "table '%s' already exists", statement->table);
            goto done;
        }
        mdb_txn_abort(txn);
        txn = NULL;
        status = lsql_make_change_result(0, out_result, error);
        goto done;
    }

    unsigned char key[LSQL_CATALOG_KEY_CAPACITY];
    size_t key_length = lsql_catalog_key(key, LSQL_CATALOG_SCHEMA_PREFIX, statement->table);
    MDB_val key_value = {.mv_size = key_length, .mv_data = key};
    MDB_val data_value = {.mv_size = encoded_size, .mv_data = encoded};
    int rc = mdb_put(txn, conn->catalog, &key_value, &data_value, 0);
    if (rc != 0) {
        status = lsql_error_from_lmdb(error, rc, statement->table_offset,
                                      "cannot write table catalog entry");
        goto done;
    }

    status = lsql_write_counter(conn, txn, statement->table, 1, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }

    status = lsql_open_table(txn, statement->table, true, &dbi, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    dbi_open = true;

    status = lsql_commit_transaction(txn, statement->table_offset, error);
    txn = NULL;
    if (status != LSQL_OK) {
        goto done;
    }
    status = lsql_make_change_result(0, out_result, error);

done:
    if (txn != NULL) {
        mdb_txn_abort(txn);
    }
    if (dbi_open) {
        mdb_dbi_close(conn->env, dbi);
    }
    free(encoded);
    lsql_arena_destroy(&arena);
    return status;
}

static lsql_status lsql_execute_drop(lsql_conn *conn, const lsql_statement *statement,
                                     lsql_result **out_result, lsql_error *error) {
    lsql_status status = lsql_require_writable(conn, statement->table_offset, error);
    if (status != LSQL_OK) {
        return status;
    }

    lsql_arena arena;
    lsql_arena_init(&arena);
    MDB_txn *txn = NULL;
    MDB_dbi dbi = 0;
    bool dbi_open = false;

    status = lsql_begin_transaction(conn, 0, &txn, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }

    bool found = false;
    lsql_schema schema;
    status = lsql_load_schema(conn, txn, statement->table, &arena, &schema, &found,
                              statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    if (!found) {
        if (!statement->as.drop.if_exists) {
            status = lsql_error_set(error, LSQL_ERROR_NOT_FOUND, statement->table_offset,
                                    "table '%s' does not exist", statement->table);
            goto done;
        }
        mdb_txn_abort(txn);
        txn = NULL;
        status = lsql_make_change_result(0, out_result, error);
        goto done;
    }

    status = lsql_open_table(txn, statement->table, false, &dbi, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    dbi_open = true;

    int rc = mdb_drop(txn, dbi, 1);
    if (rc != 0) {
        status =
            lsql_error_from_lmdb(error, rc, statement->table_offset, "cannot delete table storage");
        goto done;
    }
    dbi_open = false;

    unsigned char key[LSQL_CATALOG_KEY_CAPACITY];
    size_t key_length = lsql_catalog_key(key, LSQL_CATALOG_SCHEMA_PREFIX, statement->table);
    MDB_val key_value = {.mv_size = key_length, .mv_data = key};
    rc = mdb_del(txn, conn->catalog, &key_value, NULL);
    if (rc != 0 && rc != MDB_NOTFOUND) {
        status = lsql_error_from_lmdb(error, rc, statement->table_offset,
                                      "cannot remove table catalog entry");
        goto done;
    }

    key_length = lsql_catalog_key(key, LSQL_CATALOG_COUNTER_PREFIX, statement->table);
    key_value.mv_size = key_length;
    rc = mdb_del(txn, conn->catalog, &key_value, NULL);
    if (rc != 0 && rc != MDB_NOTFOUND) {
        status =
            lsql_error_from_lmdb(error, rc, statement->table_offset, "cannot remove row counter");
        goto done;
    }

    status = lsql_commit_transaction(txn, statement->table_offset, error);
    txn = NULL;
    if (status != LSQL_OK) {
        goto done;
    }
    status = lsql_make_change_result(0, out_result, error);

done:
    if (txn != NULL) {
        mdb_txn_abort(txn);
    }
    if (dbi_open) {
        mdb_dbi_close(conn->env, dbi);
    }
    lsql_arena_destroy(&arena);
    return status;
}

static lsql_status lsql_coerce_value(const lsql_value *input, lsql_type target,
                                     const char *column_name, size_t offset, lsql_value *out_value,
                                     lsql_error *error) {
    if (input->type == LSQL_TYPE_NULL) {
        out_value->type = LSQL_TYPE_NULL;
        return LSQL_OK;
    }

    switch (target) {
    case LSQL_TYPE_INTEGER:
        if (input->type != LSQL_TYPE_INTEGER) {
            return lsql_error_set(error, LSQL_ERROR_TYPE, offset,
                                  "column '%s' expects INTEGER but got %s", column_name,
                                  lsql_type_name(input->type));
        }
        *out_value = *input;
        return LSQL_OK;
    case LSQL_TYPE_REAL:
        if (input->type == LSQL_TYPE_INTEGER) {
            out_value->type = LSQL_TYPE_REAL;
            out_value->as.real = (double)input->as.integer;
            return LSQL_OK;
        }
        if (input->type != LSQL_TYPE_REAL) {
            return lsql_error_set(error, LSQL_ERROR_TYPE, offset,
                                  "column '%s' expects REAL but got %s", column_name,
                                  lsql_type_name(input->type));
        }
        *out_value = *input;
        return LSQL_OK;
    case LSQL_TYPE_TEXT:
        if (input->type != LSQL_TYPE_TEXT) {
            return lsql_error_set(error, LSQL_ERROR_TYPE, offset,
                                  "column '%s' expects TEXT but got %s", column_name,
                                  lsql_type_name(input->type));
        }
        *out_value = *input;
        return LSQL_OK;
    case LSQL_TYPE_NULL:
        break;
    }
    return lsql_error_set(error, LSQL_ERROR_CORRUPT, offset, "invalid column type in table schema");
}

static lsql_status lsql_execute_insert(lsql_conn *conn, const lsql_statement *statement,
                                       lsql_result **out_result, lsql_error *error) {
    lsql_status status = lsql_require_writable(conn, statement->table_offset, error);
    if (status != LSQL_OK) {
        return status;
    }

    lsql_arena arena;
    lsql_arena_init(&arena);
    MDB_txn *txn = NULL;
    MDB_dbi dbi = 0;
    bool dbi_open = false;
    unsigned char *encoded = NULL;

    status = lsql_begin_transaction(conn, 0, &txn, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }

    bool found = false;
    lsql_schema schema;
    status = lsql_load_schema(conn, txn, statement->table, &arena, &schema, &found,
                              statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    if (!found) {
        status = lsql_error_set(error, LSQL_ERROR_NOT_FOUND, statement->table_offset,
                                "table '%s' does not exist", statement->table);
        goto done;
    }

    lsql_value *row_values = lsql_arena_alloc(&arena, schema.column_count * sizeof(*row_values));
    if (row_values == NULL) {
        status = lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, statement->table_offset,
                                "out of memory while building row");
        goto done;
    }
    for (size_t index = 0; index < schema.column_count; ++index) {
        row_values[index].type = LSQL_TYPE_NULL;
    }

    if (statement->as.insert.column_count == 0) {
        if (statement->as.insert.value_count != schema.column_count) {
            status = lsql_error_set(error, LSQL_ERROR_TYPE, statement->table_offset,
                                    "table '%s' has %zu columns but %zu values were supplied",
                                    statement->table, schema.column_count,
                                    statement->as.insert.value_count);
            goto done;
        }
        for (size_t index = 0; index < schema.column_count; ++index) {
            status = lsql_coerce_value(&statement->as.insert.values[index],
                                       schema.columns[index].type, schema.columns[index].name,
                                       statement->as.insert.value_offsets[index],
                                       &row_values[index], error);
            if (status != LSQL_OK) {
                goto done;
            }
        }
    } else {
        bool *assigned = lsql_arena_alloc(&arena, schema.column_count * sizeof(*assigned));
        if (assigned == NULL) {
            status = lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, statement->table_offset,
                                    "out of memory while mapping columns");
            goto done;
        }

        for (size_t index = 0; index < statement->as.insert.column_count; ++index) {
            const char *column_name = statement->as.insert.columns[index];
            size_t offset = statement->as.insert.column_offsets[index];
            int target = lsql_schema_find_column(&schema, column_name);
            if (target < 0) {
                status = lsql_error_set(error, LSQL_ERROR_NOT_FOUND, offset, "no such column '%s'",
                                        column_name);
                goto done;
            }
            if (assigned[target]) {
                status = lsql_error_set(error, LSQL_ERROR_TYPE, offset,
                                        "column '%s' specified more than once", column_name);
                goto done;
            }
            assigned[target] = true;
            status = lsql_coerce_value(&statement->as.insert.values[index],
                                       schema.columns[target].type, schema.columns[target].name,
                                       statement->as.insert.value_offsets[index],
                                       &row_values[target], error);
            if (status != LSQL_OK) {
                goto done;
            }
        }
    }

    size_t encoded_size = 0;
    status = lsql_row_encode(&schema, row_values, &encoded, &encoded_size, error);
    if (status != LSQL_OK) {
        goto done;
    }

    uint64_t counter = 0;
    status =
        lsql_read_counter(conn, txn, statement->table, &counter, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    if (counter == 0 || counter > (uint64_t)INT64_MAX) {
        status = lsql_error_set(error, LSQL_ERROR_STORAGE, statement->table_offset,
                                "row id space exhausted for table '%s'", statement->table);
        goto done;
    }

    status = lsql_open_table(txn, statement->table, false, &dbi, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    dbi_open = true;

    unsigned char row_key[8];
    lsql_write_u64(row_key, counter);
    MDB_val key_value = {.mv_size = sizeof(row_key), .mv_data = row_key};
    MDB_val data_value = {.mv_size = encoded_size, .mv_data = encoded};

    int rc = mdb_put(txn, dbi, &key_value, &data_value, 0);
    if (rc != 0) {
        status = lsql_error_from_lmdb(error, rc, statement->table_offset, "cannot insert row");
        goto done;
    }

    status = lsql_write_counter(conn, txn, statement->table, counter + 1, statement->table_offset,
                                error);
    if (status != LSQL_OK) {
        goto done;
    }

    status = lsql_commit_transaction(txn, statement->table_offset, error);
    txn = NULL;
    if (status != LSQL_OK) {
        goto done;
    }
    status = lsql_make_change_result(1, out_result, error);

done:
    if (txn != NULL) {
        mdb_txn_abort(txn);
    }
    if (dbi_open) {
        mdb_dbi_close(conn->env, dbi);
    }
    free(encoded);
    lsql_arena_destroy(&arena);
    return status;
}

static lsql_status lsql_execute_select(lsql_conn *conn, const lsql_statement *statement,
                                       lsql_result **out_result, lsql_error *error) {
    lsql_status status = LSQL_OK;
    lsql_arena arena;
    lsql_arena_init(&arena);

    MDB_txn *txn = NULL;
    MDB_dbi dbi = 0;
    bool dbi_open = false;
    MDB_cursor *cursor = NULL;
    lsql_result *result = NULL;
    lsql_value *decoded = NULL;
    selected_row *rows = NULL;
    selected_row *temporary = NULL;
    size_t row_count = 0;
    size_t row_capacity = 0;
    size_t result_column_count = 0;
    int *projection = NULL;
    const char **projection_names = NULL;
    int order_column = -1;

    status = lsql_begin_transaction(conn, MDB_RDONLY, &txn, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }

    bool found = false;
    lsql_schema schema;
    status = lsql_load_schema(conn, txn, statement->table, &arena, &schema, &found,
                              statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    if (!found) {
        status = lsql_error_set(error, LSQL_ERROR_NOT_FOUND, statement->table_offset,
                                "table '%s' does not exist", statement->table);
        goto done;
    }

    status = lsql_validate_expression(statement->as.select.where, &schema, error);
    if (status != LSQL_OK) {
        goto done;
    }

    result_column_count =
        statement->as.select.star ? schema.column_count : statement->as.select.column_count;
    projection = lsql_arena_alloc(&arena, result_column_count * sizeof(*projection));
    projection_names =
        (const char **)lsql_arena_alloc(&arena, result_column_count * sizeof(*projection_names));
    if (projection == NULL || projection_names == NULL) {
        status = lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                "out of memory while resolving query columns");
        goto done;
    }

    for (size_t index = 0; index < result_column_count; ++index) {
        const char *name;
        size_t offset = LSQL_OFFSET_NONE;
        if (statement->as.select.star) {
            name = schema.columns[index].name;
        } else {
            name = statement->as.select.columns[index];
            offset = statement->as.select.column_offsets[index];
        }

        if (strcmp(name, "rowid") == 0) {
            projection[index] = -1;
            projection_names[index] = "rowid";
        } else {
            int column = lsql_schema_find_column(&schema, name);
            if (column < 0) {
                status = lsql_error_set(error, LSQL_ERROR_NOT_FOUND, offset, "no such column '%s'",
                                        name);
                goto done;
            }
            projection[index] = column;
            projection_names[index] = schema.columns[column].name;
        }
    }

    if (statement->as.select.order_by != NULL) {
        if (strcmp(statement->as.select.order_by, "rowid") == 0) {
            order_column = -1;
        } else {
            order_column = lsql_schema_find_column(&schema, statement->as.select.order_by);
            if (order_column < 0) {
                status = lsql_error_set(error, LSQL_ERROR_NOT_FOUND,
                                        statement->as.select.order_by_offset, "no such column '%s'",
                                        statement->as.select.order_by);
                goto done;
            }
        }
    }

    status = lsql_open_table(txn, statement->table, false, &dbi, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    dbi_open = true;

    int rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        status =
            lsql_error_from_lmdb(error, rc, statement->table_offset, "cannot open table cursor");
        goto done;
    }

    if (schema.column_count == 0) {
        status = lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                "table '%s' has an empty schema", statement->table);
        goto done;
    }

    decoded = malloc(schema.column_count * sizeof(*decoded));
    if (decoded == NULL) {
        status = lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                "out of memory while scanning table");
        goto done;
    }

    row_capacity = 16;
    rows = malloc(row_capacity * sizeof(*rows));
    if (rows == NULL) {
        status = lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                "out of memory while scanning table");
        goto done;
    }

    MDB_val key;
    MDB_val data;
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        if (key.mv_size != 8u) {
            status = lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                    "corrupt row key in table '%s'", statement->table);
            goto done;
        }
        uint64_t rowid = lsql_read_u64(key.mv_data);

        status = lsql_row_decode(&schema, data.mv_data, data.mv_size, decoded, error);
        if (status != LSQL_OK) {
            goto done;
        }

        bool matches = false;
        status =
            lsql_row_matches(statement->as.select.where, &schema, rowid, decoded, &matches, error);
        if (status != LSQL_OK) {
            goto done;
        }

        if (matches) {
            if (row_count == row_capacity) {
                if (row_capacity > SIZE_MAX / 2u) {
                    status = lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                            "result set is too large");
                    goto done;
                }
                size_t new_capacity = row_capacity * 2u;
                selected_row *new_rows = realloc(rows, new_capacity * sizeof(*rows));
                if (new_rows == NULL) {
                    status = lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                            "out of memory while scanning table");
                    goto done;
                }
                rows = new_rows;
                row_capacity = new_capacity;
            }

            lsql_value *row_values = malloc(schema.column_count * sizeof(*row_values));
            if (row_values == NULL) {
                status = lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                        "out of memory while scanning table");
                goto done;
            }
            memcpy(row_values, decoded, schema.column_count * sizeof(*row_values));
            rows[row_count].rowid = rowid;
            rows[row_count].values = row_values;
            row_count++;
        }

        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    if (rc != MDB_NOTFOUND) {
        status = lsql_error_from_lmdb(error, rc, statement->table_offset, "cannot scan table");
        goto done;
    }

    if (statement->as.select.order_by != NULL && row_count > 1) {
        temporary = malloc(row_count * sizeof(*temporary));
        if (temporary == NULL) {
            status = lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                    "out of memory while sorting result");
            goto done;
        }
        lsql_sort_rows(rows, temporary, row_count, order_column,
                       statement->as.select.order_descending);
    }

    size_t start = 0;
    if (statement->as.select.has_offset) {
        start = lsql_clamp_size(statement->as.select.offset);
        if (start > row_count) {
            start = row_count;
        }
    }
    size_t output_count = row_count - start;
    if (statement->as.select.has_limit) {
        size_t limit = lsql_clamp_size(statement->as.select.limit);
        if (limit < output_count) {
            output_count = limit;
        }
    }

    status = lsql_result_create(LSQL_RESULT_ROWS, result_column_count, &result, error);
    if (status != LSQL_OK) {
        goto done;
    }
    for (size_t index = 0; index < result_column_count; ++index) {
        status = lsql_result_set_column_name(result, index, projection_names[index], error);
        if (status != LSQL_OK) {
            goto done;
        }
    }
    status = lsql_result_allocate_rows(result, output_count, error);
    if (status != LSQL_OK) {
        goto done;
    }

    for (size_t row_index = 0; row_index < output_count; ++row_index) {
        const selected_row *source = &rows[start + row_index];
        for (size_t column_index = 0; column_index < result_column_count; ++column_index) {
            lsql_value source_value = lsql_row_value_at(source, projection[column_index]);
            lsql_value *destination =
                &result->values[row_index * result_column_count + column_index];
            status = lsql_copy_value(destination, &source_value, &result->arena, error);
            if (status != LSQL_OK) {
                goto done;
            }
        }
    }

    *out_result = result;
    result = NULL;

done:
    if (cursor != NULL) {
        mdb_cursor_close(cursor);
    }
    if (txn != NULL) {
        mdb_txn_abort(txn);
    }
    if (dbi_open) {
        mdb_dbi_close(conn->env, dbi);
    }
    lsql_free_selected_rows(rows, row_count);
    free(temporary);
    free(decoded);
    lsql_arena_destroy(&arena);
    lsql_result_free(result);
    return status;
}

static lsql_status lsql_execute_delete(lsql_conn *conn, const lsql_statement *statement,
                                       lsql_result **out_result, lsql_error *error) {
    lsql_status status = lsql_require_writable(conn, statement->table_offset, error);
    if (status != LSQL_OK) {
        return status;
    }

    lsql_arena arena;
    lsql_arena_init(&arena);
    MDB_txn *txn = NULL;
    MDB_dbi dbi = 0;
    bool dbi_open = false;
    MDB_cursor *cursor = NULL;
    lsql_value *decoded = NULL;
    size_t affected = 0;

    status = lsql_begin_transaction(conn, 0, &txn, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }

    bool found = false;
    lsql_schema schema;
    status = lsql_load_schema(conn, txn, statement->table, &arena, &schema, &found,
                              statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    if (!found) {
        status = lsql_error_set(error, LSQL_ERROR_NOT_FOUND, statement->table_offset,
                                "table '%s' does not exist", statement->table);
        goto done;
    }

    status = lsql_validate_expression(statement->as.delete.where, &schema, error);
    if (status != LSQL_OK) {
        goto done;
    }

    status = lsql_open_table(txn, statement->table, false, &dbi, statement->table_offset, error);
    if (status != LSQL_OK) {
        goto done;
    }
    dbi_open = true;

    int rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc != 0) {
        status =
            lsql_error_from_lmdb(error, rc, statement->table_offset, "cannot open table cursor");
        goto done;
    }

    if (schema.column_count == 0) {
        status = lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                "table '%s' has an empty schema", statement->table);
        goto done;
    }

    decoded = malloc(schema.column_count * sizeof(*decoded));
    if (decoded == NULL) {
        status = lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                "out of memory while scanning table");
        goto done;
    }

    MDB_val key;
    MDB_val data;
    rc = mdb_cursor_get(cursor, &key, &data, MDB_FIRST);
    while (rc == 0) {
        if (key.mv_size != 8u) {
            status = lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                    "corrupt row key in table '%s'", statement->table);
            goto done;
        }
        uint64_t rowid = lsql_read_u64(key.mv_data);
        status = lsql_row_decode(&schema, data.mv_data, data.mv_size, decoded, error);
        if (status != LSQL_OK) {
            goto done;
        }

        bool matches = false;
        status =
            lsql_row_matches(statement->as.delete.where, &schema, rowid, decoded, &matches, error);
        if (status != LSQL_OK) {
            goto done;
        }
        if (matches) {
            rc = mdb_cursor_del(cursor, 0);
            if (rc != 0) {
                status =
                    lsql_error_from_lmdb(error, rc, statement->table_offset, "cannot delete row");
                goto done;
            }
            affected++;
        }
        rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT);
    }

    if (rc != MDB_NOTFOUND) {
        status = lsql_error_from_lmdb(error, rc, statement->table_offset, "cannot scan table");
        goto done;
    }

    if (cursor != NULL) {
        mdb_cursor_close(cursor);
        cursor = NULL;
    }

    status = lsql_commit_transaction(txn, statement->table_offset, error);
    txn = NULL;
    if (status != LSQL_OK) {
        goto done;
    }
    status = lsql_make_change_result(affected, out_result, error);

done:
    if (cursor != NULL) {
        mdb_cursor_close(cursor);
    }
    if (txn != NULL) {
        mdb_txn_abort(txn);
    }
    if (dbi_open) {
        mdb_dbi_close(conn->env, dbi);
    }
    free(decoded);
    lsql_arena_destroy(&arena);
    return status;
}

lsql_status lsql_engine_execute(lsql_conn *conn, const lsql_statement *statement,
                                lsql_result **out_result, lsql_error *error) {
    if (conn == NULL || statement == NULL || out_result == NULL || error == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }
    *out_result = NULL;

    switch (statement->type) {
    case LSQL_STATEMENT_CREATE_TABLE:
        return lsql_execute_create(conn, statement, out_result, error);
    case LSQL_STATEMENT_DROP_TABLE:
        return lsql_execute_drop(conn, statement, out_result, error);
    case LSQL_STATEMENT_INSERT:
        return lsql_execute_insert(conn, statement, out_result, error);
    case LSQL_STATEMENT_SELECT:
        return lsql_execute_select(conn, statement, out_result, error);
    case LSQL_STATEMENT_DELETE:
        return lsql_execute_delete(conn, statement, out_result, error);
    }
    return lsql_error_set(error, LSQL_ERROR_CORRUPT, statement->table_offset,
                          "unsupported statement");
}
