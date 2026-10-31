#ifndef LMDBSQL_RESULT_H
#define LMDBSQL_RESULT_H

#include "internal.h"

struct lsql_result {
    lsql_result_kind kind;
    size_t column_count;
    const char **column_names;
    size_t row_count;
    lsql_value *values;
    size_t affected_rows;
    lsql_arena arena;
};

lsql_status lsql_result_create(lsql_result_kind kind, size_t column_count, lsql_result **out_result,
                               lsql_error *error);
lsql_status lsql_result_set_column_name(lsql_result *result, size_t index, const char *name,
                                        lsql_error *error);
lsql_status lsql_result_allocate_rows(lsql_result *result, size_t row_count, lsql_error *error);

#endif /* LMDBSQL_RESULT_H */
