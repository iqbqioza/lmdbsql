#include "lmdbsql/lmdbsql.h"

#include "engine.h"

#include <lmdb.h>
#include <stdlib.h>
#include <string.h>

static void lsql_initialize_error(lsql_error *error) {
    lsql_error_clear(error);
}

void lsql_options_init(lsql_options *options) {
    if (options == NULL) {
        return;
    }
    options->map_size = (size_t)64u * 1024u * 1024u;
    options->max_readers = 126;
    options->max_dbs = 256;
    options->read_only = false;
    options->no_sync = false;
}

const char *lsql_status_name(lsql_status status) {
    switch (status) {
    case LSQL_OK:
        return "LSQL_OK";
    case LSQL_ERROR_INVALID_ARGUMENT:
        return "LSQL_ERROR_INVALID_ARGUMENT";
    case LSQL_ERROR_OUT_OF_MEMORY:
        return "LSQL_ERROR_OUT_OF_MEMORY";
    case LSQL_ERROR_PARSE:
        return "LSQL_ERROR_PARSE";
    case LSQL_ERROR_NOT_FOUND:
        return "LSQL_ERROR_NOT_FOUND";
    case LSQL_ERROR_ALREADY_EXISTS:
        return "LSQL_ERROR_ALREADY_EXISTS";
    case LSQL_ERROR_TYPE:
        return "LSQL_ERROR_TYPE";
    case LSQL_ERROR_CORRUPT:
        return "LSQL_ERROR_CORRUPT";
    case LSQL_ERROR_READ_ONLY:
        return "LSQL_ERROR_READ_ONLY";
    case LSQL_ERROR_STORAGE:
        return "LSQL_ERROR_STORAGE";
    }
    return "LSQL_ERROR_UNKNOWN";
}

const char *lsql_error_message(const lsql_error *error) {
    return error == NULL ? "" : error->message;
}

lsql_status lsql_open(const char *path, const lsql_options *options, lsql_conn **out_conn,
                      lsql_error *error) {
    if (out_conn == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }
    *out_conn = NULL;
    lsql_initialize_error(error);

    if (path == NULL || path[0] == '\0') {
        return lsql_error_set(error, LSQL_ERROR_INVALID_ARGUMENT, LSQL_OFFSET_NONE,
                              "database path must not be empty");
    }

    lsql_options defaults;
    if (options == NULL) {
        lsql_options_init(&defaults);
        options = &defaults;
    }
    if (options->map_size == 0) {
        return lsql_error_set(error, LSQL_ERROR_INVALID_ARGUMENT, LSQL_OFFSET_NONE,
                              "map_size must be greater than zero");
    }
    if (options->max_readers == 0) {
        return lsql_error_set(error, LSQL_ERROR_INVALID_ARGUMENT, LSQL_OFFSET_NONE,
                              "max_readers must be greater than zero");
    }
    if (options->max_dbs < 2) {
        return lsql_error_set(error, LSQL_ERROR_INVALID_ARGUMENT, LSQL_OFFSET_NONE,
                              "max_dbs must be at least 2");
    }

    lsql_conn *conn = calloc(1, sizeof(*conn));
    if (conn == NULL) {
        return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                              "out of memory while opening database");
    }
    conn->read_only = options->read_only;

    MDB_env *env = NULL;
    int rc = mdb_env_create(&env);
    if (rc != 0) {
        free(conn);
        return lsql_error_from_lmdb(error, rc, LSQL_OFFSET_NONE, "cannot create LMDB environment");
    }

    rc = mdb_env_set_maxdbs(env, options->max_dbs);
    if (rc == 0) {
        rc = mdb_env_set_maxreaders(env, options->max_readers);
    }
    if (rc == 0) {
        rc = mdb_env_set_mapsize(env, options->map_size);
    }
    if (rc != 0) {
        mdb_env_close(env);
        free(conn);
        return lsql_error_from_lmdb(error, rc, LSQL_OFFSET_NONE,
                                    "cannot configure LMDB environment");
    }

    unsigned int flags = 0;
    if (options->read_only) {
        flags |= MDB_RDONLY;
    } else if (options->no_sync) {
        flags |= MDB_NOSYNC;
    }

    rc = mdb_env_open(env, path, flags, 0644);
    if (rc != 0) {
        mdb_env_close(env);
        free(conn);
        return lsql_error_from_lmdb(error, rc, LSQL_OFFSET_NONE, "cannot open LMDB environment");
    }

    MDB_txn *txn = NULL;
    rc = mdb_txn_begin(env, NULL, options->read_only ? MDB_RDONLY : 0, &txn);
    if (rc != 0) {
        mdb_env_close(env);
        free(conn);
        return lsql_error_from_lmdb(error, rc, LSQL_OFFSET_NONE,
                                    "cannot begin catalog transaction");
    }

    rc = mdb_dbi_open(txn, NULL, 0, &conn->catalog);
    if (rc != 0) {
        mdb_txn_abort(txn);
        mdb_env_close(env);
        free(conn);
        return lsql_error_from_lmdb(error, rc, LSQL_OFFSET_NONE, "cannot open catalog database");
    }

    if (options->read_only) {
        mdb_txn_abort(txn);
    } else {
        rc = mdb_txn_commit(txn);
        if (rc != 0) {
            mdb_env_close(env);
            free(conn);
            return lsql_error_from_lmdb(error, rc, LSQL_OFFSET_NONE,
                                        "cannot commit catalog transaction");
        }
    }

    conn->env = env;
    *out_conn = conn;
    return LSQL_OK;
}

void lsql_close(lsql_conn *conn) {
    if (conn == NULL) {
        return;
    }
    if (conn->env != NULL) {
        mdb_dbi_close(conn->env, conn->catalog);
        mdb_env_close(conn->env);
    }
    free(conn);
}

lsql_status lsql_execute(lsql_conn *conn, const char *sql, size_t sql_length,
                         lsql_result **out_result, lsql_error *error) {
    if (out_result == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }
    *out_result = NULL;
    lsql_initialize_error(error);

    if (conn == NULL) {
        return lsql_error_set(error, LSQL_ERROR_INVALID_ARGUMENT, LSQL_OFFSET_NONE,
                              "connection must not be NULL");
    }
    if (sql == NULL) {
        return lsql_error_set(error, LSQL_ERROR_INVALID_ARGUMENT, LSQL_OFFSET_NONE,
                              "SQL text must not be NULL");
    }

    lsql_error local_error;
    lsql_error *execution_error = error == NULL ? &local_error : error;
    lsql_error_clear(execution_error);

    lsql_arena arena;
    lsql_arena_init(&arena);
    lsql_statement *statement = NULL;
    lsql_status status = lsql_parse(sql, sql_length, &arena, &statement, execution_error);
    if (status == LSQL_OK) {
        status = lsql_engine_execute(conn, statement, out_result, execution_error);
    }
    lsql_arena_destroy(&arena);
    return status;
}
