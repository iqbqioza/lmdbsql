# Find the LMDB library and headers.
#
# Imported target:
#   LMDB::LMDB

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_LMDB QUIET lmdb)
endif()

find_path(
    LMDB_INCLUDE_DIR
    NAMES lmdb.h
    HINTS ${PC_LMDB_INCLUDE_DIRS}
)
find_library(
    LMDB_LIBRARY
    NAMES lmdb
    HINTS ${PC_LMDB_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    LMDB
    REQUIRED_VARS LMDB_LIBRARY LMDB_INCLUDE_DIR
    VERSION_VAR PC_LMDB_VERSION
)

if(LMDB_FOUND AND NOT TARGET LMDB::LMDB)
    add_library(LMDB::LMDB UNKNOWN IMPORTED)
    set_target_properties(
        LMDB::LMDB
        PROPERTIES
            IMPORTED_LOCATION "${LMDB_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${LMDB_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(LMDB_INCLUDE_DIR LMDB_LIBRARY)
