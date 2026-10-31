#ifndef LMDBSQL_LMDBSQL_H
#define LMDBSQL_LMDBSQL_H

/**
 * @file lmdbsql.h
 * @brief Public API for the lmdbsql embedded SQL engine.
 *
 * lmdbsql stores tables in an LMDB environment and exposes a small SQL
 * dialect.  All strings passed to this API are UTF-8 byte strings; text
 * values may contain embedded NUL bytes when their length is given
 * explicitly.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Value used in lsql_error::offset when no source position is available. */
#define LSQL_OFFSET_NONE (SIZE_MAX)

/** Capacity of lsql_error::message, including the terminating NUL. */
#define LSQL_ERROR_MESSAGE_SIZE 256

/** Result codes returned by the public API. */
typedef enum lsql_status {
    LSQL_OK = 0,
    LSQL_ERROR_INVALID_ARGUMENT,
    LSQL_ERROR_OUT_OF_MEMORY,
    LSQL_ERROR_PARSE,
    LSQL_ERROR_NOT_FOUND,
    LSQL_ERROR_ALREADY_EXISTS,
    LSQL_ERROR_TYPE,
    LSQL_ERROR_CORRUPT,
    LSQL_ERROR_READ_ONLY,
    LSQL_ERROR_STORAGE
} lsql_status;

/** Detailed error information. */
typedef struct lsql_error {
    /** Status code, matching the value returned by the failing function. */
    lsql_status status;
    /** Byte offset in the SQL text, or LSQL_OFFSET_NONE. */
    size_t offset;
    /** Human readable, NUL terminated message. */
    char message[LSQL_ERROR_MESSAGE_SIZE];
} lsql_error;

/** SQL value types. */
typedef enum lsql_type {
    LSQL_TYPE_NULL = 0,
    LSQL_TYPE_INTEGER,
    LSQL_TYPE_REAL,
    LSQL_TYPE_TEXT
} lsql_type;

/**
 * A single SQL value.
 *
 * Text values are not NUL terminated.  A text pointer returned by a query
 * remains valid until the owning lsql_result is freed.
 */
typedef struct lsql_value {
    lsql_type type;
    union {
        int64_t integer;
        double real;
        struct {
            const char *data;
            size_t length;
        } text;
    } as;
} lsql_value;

/** Options used when opening a database environment. */
typedef struct lsql_options {
    /** Maximum size of the memory map in bytes.  Defaults to 64 MiB. */
    size_t map_size;
    /** Maximum number of concurrent readers.  Defaults to 126. */
    unsigned max_readers;
    /** Maximum number of named databases (catalog plus tables).  Defaults to 256. */
    unsigned max_dbs;
    /** Open the environment in read-only mode.  Defaults to false. */
    bool read_only;
    /** Skip LMDB fsync calls.  Faster, but not crash safe.  Defaults to false. */
    bool no_sync;
} lsql_options;

/** Database connection handle. */
typedef struct lsql_conn lsql_conn;

/** Materialized query result handle. */
typedef struct lsql_result lsql_result;

/** Kind of result produced by a statement. */
typedef enum lsql_result_kind {
    /** The statement produced rows (SELECT). */
    LSQL_RESULT_ROWS,
    /** The statement changed the database (DDL, INSERT, DELETE). */
    LSQL_RESULT_CHANGE
} lsql_result_kind;

/** Fill @p options with the default connection settings. */
void lsql_options_init(lsql_options *options);

/** Return a stable name for a status code. */
[[nodiscard]] const char *lsql_status_name(lsql_status status);

/** Return the message of an error object, or an empty string for NULL. */
[[nodiscard]] const char *lsql_error_message(const lsql_error *error);

/**
 * Open an LMDB environment and prepare it for SQL execution.
 *
 * The directory must already exist.  On failure, @p out_conn is set to NULL.
 */
[[nodiscard]] lsql_status lsql_open(const char *path, const lsql_options *options,
                                    lsql_conn **out_conn, lsql_error *error);

/** Close a connection and release all associated resources. */
void lsql_close(lsql_conn *conn);

/**
 * Parse and execute exactly one SQL statement.
 *
 * A single trailing semicolon is accepted.  @p sql_length must not include a
 * terminating NUL unless it is part of the SQL text.
 *
 * On success @p out_result receives a result that must be freed with
 * lsql_result_free().  On failure @p out_result is set to NULL.
 */
[[nodiscard]] lsql_status lsql_execute(lsql_conn *conn, const char *sql, size_t sql_length,
                                       lsql_result **out_result, lsql_error *error);

/** Free a result returned by lsql_execute(). */
void lsql_result_free(lsql_result *result);

/** Return whether a result contains rows or a change count. */
[[nodiscard]] lsql_result_kind lsql_result_type(const lsql_result *result);

/** Return the number of columns in a row result. */
[[nodiscard]] size_t lsql_result_column_count(const lsql_result *result);

/** Return the name of a column, or NULL when @p index is out of range. */
[[nodiscard]] const char *lsql_result_column_name(const lsql_result *result, size_t index);

/** Return the number of rows in a row result. */
[[nodiscard]] size_t lsql_result_row_count(const lsql_result *result);

/** Return a value, or NULL when @p row or @p column is out of range. */
[[nodiscard]] const lsql_value *lsql_result_value(const lsql_result *result, size_t row,
                                                  size_t column);

/** Return the number of rows changed by a DDL or DML statement. */
[[nodiscard]] size_t lsql_result_affected_rows(const lsql_result *result);

#ifdef __cplusplus
}
#endif

#endif /* LMDBSQL_LMDBSQL_H */
