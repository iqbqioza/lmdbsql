#ifndef LMDBSQL_ROW_H
#define LMDBSQL_ROW_H

#include "internal.h"
#include "schema.h"

lsql_status lsql_row_encode(const lsql_schema *schema, const lsql_value *values,
                            unsigned char **out_data, size_t *out_size, lsql_error *error);
lsql_status lsql_row_decode(const lsql_schema *schema, const void *data, size_t size,
                            lsql_value *out_values, lsql_error *error);

#endif /* LMDBSQL_ROW_H */
