# lmdbsql

`lmdbsql` is a small embeddable SQL engine written in C23 and backed by
[LMDB](https://www.lmdb.tech/). It stores each table in its own LMDB named
database and keeps the catalog in the environment's main database.

The project is intentionally compact: a hand-written lexer and recursive-descent
parser feed a single-file execution engine. There are no generated parsers or
external runtime dependencies besides LMDB.

## Features

- `CREATE TABLE` and `DROP TABLE`, with optional `IF NOT EXISTS` / `IF EXISTS`
- `INSERT` with an explicit or implicit column list
- `SELECT` with projection, `*`, `WHERE`, `ORDER BY`, `LIMIT`, and `OFFSET`
- `DELETE` with an optional `WHERE` clause
- `INTEGER`, `REAL`, `TEXT`, and `NULL` values
- Implicit `rowid` column for every table
- Comparison operators, `AND`, `OR`, `NOT`, `IS [NOT] NULL`, and unary minus
- A command-line shell that can execute a script or read SQL from standard input

Every statement runs in an LMDB transaction. Read-only statements use a read
transaction; DDL and DML use a write transaction.

## Supported SQL

```sql
CREATE TABLE [IF NOT EXISTS] table_name (
    column_name INTEGER,
    column_name REAL,
    column_name TEXT
);

DROP TABLE [IF EXISTS] table_name;

INSERT INTO table_name [(column_name, ...)] VALUES (value, ...);

SELECT * | column_name [, column_name ...]
FROM table_name
[WHERE expression]
[ORDER BY column_name [ASC | DESC]]
[LIMIT count [OFFSET count]];

DELETE FROM table_name [WHERE expression];
```

Values may be integer or real numbers, single-quoted strings (with `''` as an
escaped quote), `NULL`, `TRUE`, or `FALSE`. Identifiers are case-sensitive;
keywords are not.

Example:

```sql
CREATE TABLE users (id INTEGER, name TEXT, score REAL);

INSERT INTO users VALUES (1, 'Alice', 9.5);
INSERT INTO users (name, id) VALUES ('Bob', 2);

SELECT id, name
FROM users
WHERE score IS NULL AND id >= 1
ORDER BY id DESC
LIMIT 10;

DELETE FROM users WHERE id = 2;
DROP TABLE users;
```

`rowid` is generated automatically and can be selected, filtered, and sorted:

```sql
SELECT rowid, name FROM users ORDER BY rowid;
```

### Current limitations

- No joins, subqueries, aggregates, grouping, or `UPDATE`
- No indexes other than the implicit primary row ordering
- No arithmetic expressions or functions
- No `ALTER TABLE`
- `ORDER BY` accepts one table column or `rowid`
- Identifiers are limited to 64 bytes; tables are limited to 256 columns
- The database directory must exist before `lsql_open()` is called

## Requirements

- C23-capable compiler: GCC 15+ or Clang 18+
- CMake 3.21+ and Ninja
- LMDB 0.9+ development headers and library
- For static analysis: clang-tidy (clang-tools-extra) and Cppcheck
- For sanitizer builds: GCC/Clang sanitizer runtimes

On Fedora:

```sh
sudo dnf install cmake ninja-build gcc gcc-c++ clang clang-tools-extra \
    compiler-rt libasan libubsan lmdb-devel cppcheck
```

## Build and test

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

A plain CMake invocation works as well:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DLSQL_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Compiler warnings include `-Wall -Wextra -Wpedantic` plus additional
correctness warnings, and are treated as errors in the presets.

### Sanitizers

```sh
cmake --preset asan-ubsan
cmake --build --preset asan-ubsan
ctest --preset asan-ubsan
```

The `asan-ubsan` test preset enables leak detection and makes UBSan failures
fatal.

### Static analysis

Run clang-tidy and Cppcheck over the whole tree:

```sh
cmake --preset tidy
cmake --build --preset tidy

cmake --preset cppcheck
cmake --build --preset cppcheck
```

Alternatively, reproduce the CI checks directly after configuring a Clang
build:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang -DLSQL_WARNINGS_AS_ERRORS=ON
cmake --build build
clang-tidy -p build --quiet --warnings-as-errors='*' src/*.c tools/*.c tests/*.c
cppcheck --enable=warning,style,performance,portability \
    --inline-suppr --suppress=missingIncludeSystem --error-exitcode=1 \
    --std=c23 --check-level=exhaustive --quiet -Iinclude -Isrc src tools tests
```

Formatting is enforced with clang-format:

```sh
clang-format --dry-run --Werror $(git ls-files '*.c' '*.h')
```

## Command-line shell

```sh
lmdbsql DATABASE [SQL]
lmdbsql DATABASE -c "SELECT * FROM users"
lmdbsql DATABASE < script.sql
```

Options:

```text
-c, --command SQL   Execute SQL and exit
    --read-only     Open the database read-only
    --map-size SIZE Set maximum map size in bytes (K/M/G suffixes)
-h, --help          Show help
    --version       Show version information
```

The shell splits input into statements on semicolons while respecting quoted
strings and SQL comments.

## C API

```c
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <lmdbsql/lmdbsql.h>

lsql_options options;
lsql_options_init(&options);

lsql_error error;
lsql_conn *conn = NULL;
if (lsql_open("./database", &options, &conn, &error) != LSQL_OK) {
    fprintf(stderr, "%s\n", lsql_error_message(&error));
    return 1;
}

const char *sql = "SELECT id, name FROM users ORDER BY id";
lsql_result *result = NULL;
if (lsql_execute(conn, sql, strlen(sql), &result, &error) == LSQL_OK) {
    for (size_t row = 0; row < lsql_result_row_count(result); ++row) {
        const lsql_value *value = lsql_result_value(result, row, 0);
        if (value->type == LSQL_TYPE_INTEGER) {
            printf("%" PRId64 "\n", value->as.integer);
        }
    }
    lsql_result_free(result);
}

lsql_close(conn);
```

`lsql_execute()` parses and executes exactly one statement. Results must be
freed with `lsql_result_free()`. Text values returned by a result remain valid
until that result is freed.

## Storage layout

- The main LMDB database contains the table catalog and per-table row counters.
- Each table is stored in an LMDB named database named `t:<table>`.
- Catalog keys and schema values are length-delimited binary records.
- Rows are keyed by an 8-byte big-endian row id, which also backs `rowid`.
- Schema and row payloads carry magic bytes and version numbers so corrupt or
  incompatible data is rejected with `LSQL_ERROR_CORRUPT`.

## Continuous integration

GitHub Actions runs in Fedora 42 containers with a matrix over GCC and Clang
for both Debug and Release builds, plus sanitizer and static-analysis jobs.
The CI installs CMake, Ninja, both compilers, sanitizer runtimes, LMDB, and the
analysis tools, then builds with warnings as errors, runs CTest, checks
formatting, runs clang-tidy with all warnings promoted to errors, and runs
Cppcheck.
