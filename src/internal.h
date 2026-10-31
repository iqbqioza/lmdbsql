#ifndef LMDBSQL_INTERNAL_H
#define LMDBSQL_INTERNAL_H

#include "lmdbsql/lmdbsql.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LSQL_MAX_IDENTIFIER_LENGTH 64u
#define LSQL_MAX_COLUMNS 256u

#if defined(__GNUC__) || defined(__clang__)
#define LSQL_PRINTF(format_index, argument_index)                                                  \
    __attribute__((format(printf, format_index, argument_index)))
#else
#define LSQL_PRINTF(format_index, argument_index)
#endif

#define LSQL_ARRAY_LENGTH(array) (sizeof(array) / sizeof((array)[0]))

typedef struct lsql_arena_block lsql_arena_block;

typedef struct lsql_arena {
    lsql_arena_block *head;
} lsql_arena;

void lsql_arena_init(lsql_arena *arena);
void *lsql_arena_alloc(lsql_arena *arena, size_t size);
char *lsql_arena_strndup(lsql_arena *arena, const char *text, size_t length);
char *lsql_arena_strdup(lsql_arena *arena, const char *text);
void lsql_arena_destroy(lsql_arena *arena);

void lsql_error_clear(lsql_error *error);
lsql_status lsql_error_set(lsql_error *error, lsql_status status, size_t offset, const char *format,
                           ...) LSQL_PRINTF(4, 5);
lsql_status lsql_error_set_v(lsql_error *error, lsql_status status, size_t offset,
                             const char *format, va_list arguments) LSQL_PRINTF(4, 0);
lsql_status lsql_error_from_lmdb(lsql_error *error, int rc, size_t offset, const char *context);
lsql_status lsql_status_from_lmdb(int rc);

bool lsql_identifier_is_valid(const char *name, size_t length);
bool lsql_type_from_name(const char *name, size_t length, lsql_type *out_type);
const char *lsql_type_name(lsql_type type);

bool lsql_value_is_null(const lsql_value *value);
bool lsql_value_truthy(const lsql_value *value);
int lsql_value_compare(const lsql_value *left, const lsql_value *right, bool *comparable);

#endif /* LMDBSQL_INTERNAL_H */
