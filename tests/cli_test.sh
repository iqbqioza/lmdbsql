#!/usr/bin/env bash
set -euo pipefail

cli=${1:?usage: cli_test.sh /path/to/lmdbsql}
temporary=$(mktemp -d)
trap 'rm -rf "$temporary"' EXIT

database="$temporary/database"

"$cli" --help | grep -q "Usage: lmdbsql"
"$cli" --version | grep -q "lmdbsql"

create_output=$(
    "$cli" "$database" -c "
        CREATE TABLE users (id INTEGER, name TEXT);
        INSERT INTO users VALUES (1, 'Alice');
        INSERT INTO users VALUES (2, 'Bob');
        SELECT id, name FROM users ORDER BY id;
    "
)
grep -q "Alice" <<<"$create_output"
grep -q "Bob" <<<"$create_output"

stdin_output=$(
    printf "INSERT INTO users VALUES (3, 'Carol');\nSELECT name FROM users ORDER BY id;\n" |
        "$cli" "$database"
)
grep -q "Carol" <<<"$stdin_output"

read_only_output=$("$cli" --read-only "$database" -c "SELECT id FROM users ORDER BY id;")
grep -q "3" <<<"$read_only_output"

if "$cli" "$database" -c "SELECT * FROM missing" >/dev/null 2>&1; then
    echo "expected querying a missing table to fail" >&2
    exit 1
fi

comment_output=$(
    "$cli" "$database" -c "
        INSERT INTO users VALUES (4, 'semi;colon'); -- a comment containing a ;
        SELECT name FROM users WHERE id = 4;
    "
)
grep -q "semi;colon" <<<"$comment_output"

"$cli" "$database" --map-size 8M -c "SELECT id FROM users LIMIT 1" >/dev/null

printf "SELECT id FROM users ORDER BY id;\n-- trailing comment\n" | "$cli" "$database" >/dev/null

if "$cli" "$database" -c "SELEC 1" >/dev/null 2>&1; then
    echo "expected invalid SQL to fail" >&2
    exit 1
fi

exit 0
