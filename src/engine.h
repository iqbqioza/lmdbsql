#ifndef LMDBSQL_ENGINE_H
#define LMDBSQL_ENGINE_H

#include "internal.h"
#include "parser.h"
#include "result.h"

#include <lmdb.h>

struct lsql_conn {
    MDB_env *env;
    MDB_dbi catalog;
    bool read_only;
};

lsql_status lsql_engine_execute(lsql_conn *conn, const lsql_statement *statement,
                                lsql_result **out_result, lsql_error *error);

#endif /* LMDBSQL_ENGINE_H */
