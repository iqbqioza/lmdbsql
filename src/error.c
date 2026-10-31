#include "internal.h"

#include <errno.h>
#include <lmdb.h>
#include <stdio.h>
#include <string.h>

void lsql_error_clear(lsql_error *error) {
    if (error == NULL) {
        return;
    }
    error->status = LSQL_OK;
    error->offset = LSQL_OFFSET_NONE;
    error->message[0] = '\0';
}

lsql_status lsql_error_set_v(lsql_error *error, lsql_status status, size_t offset,
                             const char *format, va_list arguments) {
    if (error != NULL) {
        error->status = status;
        error->offset = offset;
        if (format == NULL) {
            error->message[0] = '\0';
        } else {
            // The va_list is initialized by lsql_error_set(); the analyzer cannot see
            // across the call.
            // NOLINTNEXTLINE(clang-analyzer-security.VAList)
            int written = vsnprintf(error->message, sizeof(error->message), format, arguments);
            if (written < 0) {
                snprintf(error->message, sizeof(error->message), "failed to format error message");
            }
        }
    }
    return status;
}

lsql_status lsql_error_set(lsql_error *error, lsql_status status, size_t offset, const char *format,
                           ...) {
    va_list arguments;
    va_start(arguments, format);
    lsql_status result = lsql_error_set_v(error, status, offset, format, arguments);
    va_end(arguments);
    return result;
}

lsql_status lsql_status_from_lmdb(int rc) {
    switch (rc) {
    case MDB_NOTFOUND:
        return LSQL_ERROR_NOT_FOUND;
    case MDB_CORRUPTED:
    case MDB_PANIC:
    case MDB_VERSION_MISMATCH:
    case MDB_INVALID:
        return LSQL_ERROR_CORRUPT;
    case EACCES:
    case ENOENT:
    case EIO:
    case ENOSPC:
    case MDB_MAP_FULL:
    case MDB_MAP_RESIZED:
    case MDB_READERS_FULL:
    case MDB_TXN_FULL:
    case MDB_PAGE_FULL:
    default:
        return LSQL_ERROR_STORAGE;
    }
}

lsql_status lsql_error_from_lmdb(lsql_error *error, int rc, size_t offset, const char *context) {
    const char *reason = mdb_strerror(rc);
    if (context == NULL) {
        return lsql_error_set(error, lsql_status_from_lmdb(rc), offset, "%s", reason);
    }
    return lsql_error_set(error, lsql_status_from_lmdb(rc), offset, "%s: %s", context, reason);
}
