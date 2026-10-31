#include "result.h"

#include "schema.h"

#include <stdlib.h>
#include <string.h>

lsql_status lsql_result_create(lsql_result_kind kind, size_t column_count, lsql_result **out_result,
                               lsql_error *error) {
    if (out_result == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }
    *out_result = NULL;

    if (column_count > SIZE_MAX / sizeof(char *)) {
        return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                              "result is too large");
    }

    lsql_result *result = calloc(1, sizeof(*result));
    if (result == NULL) {
        return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                              "out of memory while creating result");
    }
    lsql_arena_init(&result->arena);
    result->kind = kind;
    result->column_count = column_count;

    if (column_count > 0) {
        result->column_names =
            (const char **)lsql_arena_alloc(&result->arena, column_count * sizeof(char *));
        if (result->column_names == NULL) {
            lsql_result_free(result);
            return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                  "out of memory while creating result");
        }
    }

    *out_result = result;
    return LSQL_OK;
}

lsql_status lsql_result_set_column_name(lsql_result *result, size_t index, const char *name,
                                        lsql_error *error) {
    if (result == NULL || name == NULL || index >= result->column_count) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }
    const char *copy = lsql_arena_strdup(&result->arena, name);
    if (copy == NULL) {
        return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                              "out of memory while storing column name");
    }
    result->column_names[index] = copy;
    return LSQL_OK;
}

lsql_status lsql_result_allocate_rows(lsql_result *result, size_t row_count, lsql_error *error) {
    if (result == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }
    if (row_count > 0 && result->column_count > SIZE_MAX / row_count) {
        return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                              "result is too large");
    }

    size_t value_count = row_count * result->column_count;
    if (value_count > SIZE_MAX / sizeof(*result->values)) {
        return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                              "result is too large");
    }

    if (value_count > 0) {
        result->values = lsql_arena_alloc(&result->arena, value_count * sizeof(*result->values));
        if (result->values == NULL) {
            return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                  "out of memory while allocating result rows");
        }
    }
    result->row_count = row_count;
    return LSQL_OK;
}

void lsql_result_free(lsql_result *result) {
    if (result == NULL) {
        return;
    }
    lsql_arena_destroy(&result->arena);
    free(result);
}

lsql_result_kind lsql_result_type(const lsql_result *result) {
    return result == NULL ? LSQL_RESULT_CHANGE : result->kind;
}

size_t lsql_result_column_count(const lsql_result *result) {
    return result == NULL ? 0 : result->column_count;
}

const char *lsql_result_column_name(const lsql_result *result, size_t index) {
    if (result == NULL || index >= result->column_count) {
        return NULL;
    }
    return result->column_names[index];
}

size_t lsql_result_row_count(const lsql_result *result) {
    return result == NULL ? 0 : result->row_count;
}

const lsql_value *lsql_result_value(const lsql_result *result, size_t row, size_t column) {
    if (result == NULL || row >= result->row_count || column >= result->column_count) {
        return NULL;
    }
    return &result->values[row * result->column_count + column];
}

size_t lsql_result_affected_rows(const lsql_result *result) {
    return result == NULL ? 0 : result->affected_rows;
}
