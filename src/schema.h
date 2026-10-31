#ifndef LMDBSQL_SCHEMA_H
#define LMDBSQL_SCHEMA_H

#include "internal.h"
#include "parser.h"

typedef struct lsql_column {
    const char *name;
    lsql_type type;
} lsql_column;

typedef struct lsql_schema {
    size_t column_count;
    lsql_column *columns;
} lsql_schema;

lsql_status lsql_schema_from_definitions(const lsql_column_definition *definitions, size_t count,
                                         lsql_arena *arena, lsql_schema *out_schema,
                                         lsql_error *error);
lsql_status lsql_schema_validate(const lsql_schema *schema, lsql_error *error);
int lsql_schema_find_column(const lsql_schema *schema, const char *name);
lsql_status lsql_schema_encode(const lsql_schema *schema, unsigned char **out_data,
                               size_t *out_size, lsql_error *error);
lsql_status lsql_schema_decode(const void *data, size_t size, lsql_arena *arena,
                               lsql_schema *out_schema, lsql_error *error);

#endif /* LMDBSQL_SCHEMA_H */
