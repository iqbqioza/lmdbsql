#define _POSIX_C_SOURCE 200809L

#include "lmdbsql/lmdbsql.h"

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef LSQL_VERSION
#define LSQL_VERSION "unknown"
#endif

typedef struct cli_options {
    const char *path;
    const char *sql;
    bool read_only;
    bool has_map_size;
    size_t map_size;
} cli_options;

static void print_usage(FILE *stream) {
    fprintf(stream, "Usage: lmdbsql [OPTIONS] DATABASE [SQL]\n"
                    "\n"
                    "Execute SQL against an LMDB-backed database directory. If SQL is\n"
                    "omitted, statements are read from standard input.\n"
                    "\n"
                    "Options:\n"
                    "  -c, --command SQL   Execute SQL and exit\n"
                    "      --read-only     Open the database read-only\n"
                    "      --map-size SIZE Set maximum map size in bytes (K/M/G suffixes)\n"
                    "  -h, --help          Show this help\n"
                    "      --version       Show version information\n");
}

static bool parse_size(const char *text, size_t *out_size) {
    if (text == NULL || text[0] == '\0') {
        return false;
    }

    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if (end == text || errno == ERANGE) {
        return false;
    }

    unsigned long long multiplier = 1;
    if (*end == 'k' || *end == 'K') {
        multiplier = 1024ull;
        end++;
    } else if (*end == 'm' || *end == 'M') {
        multiplier = 1024ull * 1024ull;
        end++;
    } else if (*end == 'g' || *end == 'G') {
        multiplier = 1024ull * 1024ull * 1024ull;
        end++;
    }
    if (*end == 'b' || *end == 'B') {
        end++;
    }
    if (*end != '\0' || value == 0 || value > (unsigned long long)SIZE_MAX / multiplier) {
        return false;
    }

    *out_size = (size_t)(value * multiplier);
    return true;
}

static bool read_all(FILE *stream, char **out_text, size_t *out_length) {
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = malloc(capacity);
    if (buffer == NULL) {
        return false;
    }

    for (;;) {
        if (length + 1u >= capacity) {
            if (capacity > SIZE_MAX / 2u) {
                free(buffer);
                return false;
            }
            size_t new_capacity = capacity * 2u;
            char *new_buffer = realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                free(buffer);
                return false;
            }
            buffer = new_buffer;
            capacity = new_capacity;
        }

        size_t available = capacity - length - 1u;
        size_t read_count = fread(buffer + length, 1, available, stream);
        if (read_count > available) {
            free(buffer);
            return false;
        }
        length += read_count;
        if (read_count == 0) {
            if (ferror(stream)) {
                free(buffer);
                return false;
            }
            break;
        }
    }

    if (length >= capacity) {
        free(buffer);
        return false;
    }
    buffer[length] = '\0';
    *out_text = buffer;
    *out_length = length;
    return true;
}

static bool is_space(char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

static bool segment_has_content(const char *sql, size_t start, size_t end) {
    size_t position = start;
    while (position < end) {
        if (is_space(sql[position])) {
            position++;
            continue;
        }
        if (sql[position] == '-' && position + 1u < end && sql[position + 1u] == '-') {
            position += 2u;
            while (position < end && sql[position] != '\n') {
                position++;
            }
            continue;
        }
        if (sql[position] == '/' && position + 1u < end && sql[position + 1u] == '*') {
            position += 2u;
            bool closed = false;
            while (position + 1u < end) {
                if (sql[position] == '*' && sql[position + 1u] == '/') {
                    position += 2u;
                    closed = true;
                    break;
                }
                position++;
            }
            if (!closed) {
                return true;
            }
            continue;
        }
        return true;
    }
    return false;
}

static void format_value(const lsql_value *value, char *buffer, size_t buffer_size,
                         const char **out_text, size_t *out_length) {
    switch (value->type) {
    case LSQL_TYPE_INTEGER: {
        int written = snprintf(buffer, buffer_size, "%" PRId64, value->as.integer);
        *out_text = buffer;
        *out_length = written < 0 ? 0 : (size_t)written;
        return;
    }
    case LSQL_TYPE_REAL: {
        int written = snprintf(buffer, buffer_size, "%.17g", value->as.real);
        *out_text = buffer;
        *out_length = written < 0 ? 0 : (size_t)written;
        return;
    }
    case LSQL_TYPE_TEXT:
        *out_text = value->as.text.data;
        *out_length = value->as.text.length;
        return;
    case LSQL_TYPE_NULL:
        *out_text = "NULL";
        *out_length = 4;
        return;
    }
    *out_text = "";
    *out_length = 0;
}

static void print_padded(const char *text, size_t length, size_t width) {
    fwrite(text, 1, length, stdout);
    for (size_t index = length; index < width; ++index) {
        fputc(' ', stdout);
    }
}

static void print_result(const lsql_result *result) {
    if (lsql_result_type(result) == LSQL_RESULT_CHANGE) {
        size_t affected = lsql_result_affected_rows(result);
        printf("%zu row%s affected\n", affected, affected == 1 ? "" : "s");
        return;
    }

    size_t column_count = lsql_result_column_count(result);
    size_t row_count = lsql_result_row_count(result);
    if (column_count == 0) {
        return;
    }

    size_t *widths = calloc(column_count, sizeof(*widths));
    if (widths == NULL) {
        return;
    }

    char number_buffer[64];
    for (size_t column = 0; column < column_count; ++column) {
        const char *name = lsql_result_column_name(result, column);
        widths[column] = name == NULL ? 0 : strlen(name);
    }
    for (size_t row = 0; row < row_count; ++row) {
        for (size_t column = 0; column < column_count; ++column) {
            const lsql_value *value = lsql_result_value(result, row, column);
            const char *text;
            size_t length;
            format_value(value, number_buffer, sizeof(number_buffer), &text, &length);
            if (length > widths[column]) {
                widths[column] = length;
            }
        }
    }

    for (size_t column = 0; column < column_count; ++column) {
        if (column != 0) {
            fputs(" | ", stdout);
        }
        const char *name = lsql_result_column_name(result, column);
        print_padded(name == NULL ? "" : name, name == NULL ? 0 : strlen(name), widths[column]);
    }
    fputc('\n', stdout);

    for (size_t column = 0; column < column_count; ++column) {
        if (column != 0) {
            fputs("-+-", stdout);
        }
        for (size_t index = 0; index < widths[column]; ++index) {
            fputc('-', stdout);
        }
    }
    fputc('\n', stdout);

    for (size_t row = 0; row < row_count; ++row) {
        for (size_t column = 0; column < column_count; ++column) {
            if (column != 0) {
                fputs(" | ", stdout);
            }
            const lsql_value *value = lsql_result_value(result, row, column);
            const char *text;
            size_t length;
            format_value(value, number_buffer, sizeof(number_buffer), &text, &length);
            print_padded(text, length, widths[column]);
        }
        fputc('\n', stdout);
    }

    free(widths);
}

static bool execute_segment(lsql_conn *conn, const char *sql, size_t start, size_t end) {
    if (!segment_has_content(sql, start, end)) {
        return true;
    }

    lsql_error error;
    lsql_result *result = NULL;
    lsql_status status = lsql_execute(conn, sql + start, end - start, &result, &error);
    if (status != LSQL_OK) {
        fprintf(stderr, "error: %s", lsql_error_message(&error));
        if (error.offset != LSQL_OFFSET_NONE) {
            fprintf(stderr, " (at byte %zu)", start + error.offset);
        }
        fputc('\n', stderr);
        return false;
    }

    print_result(result);
    lsql_result_free(result);
    return true;
}

static bool execute_script(lsql_conn *conn, const char *sql, size_t length) {
    bool single_quote = false;
    bool double_quote = false;
    bool line_comment = false;
    bool block_comment = false;
    size_t start = 0;

    for (size_t position = 0; position < length; ++position) {
        char character = sql[position];
        char next = position + 1u < length ? sql[position + 1u] : '\0';

        if (line_comment) {
            if (character == '\n') {
                line_comment = false;
            }
            continue;
        }
        if (block_comment) {
            if (character == '*' && next == '/') {
                block_comment = false;
                position++;
            }
            continue;
        }
        if (single_quote) {
            if (character == '\'') {
                if (next == '\'') {
                    position++;
                } else {
                    single_quote = false;
                }
            }
            continue;
        }
        if (double_quote) {
            if (character == '"') {
                if (next == '"') {
                    position++;
                } else {
                    double_quote = false;
                }
            }
            continue;
        }

        if (character == '-' && next == '-') {
            line_comment = true;
            position++;
        } else if (character == '/' && next == '*') {
            block_comment = true;
            position++;
        } else if (character == '\'') {
            single_quote = true;
        } else if (character == '"') {
            double_quote = true;
        } else if (character == ';') {
            if (!execute_segment(conn, sql, start, position)) {
                return false;
            }
            start = position + 1u;
        }
    }

    return execute_segment(conn, sql, start, length);
}

int main(int argc, char **argv) {
    cli_options options = {0};
    bool end_of_options = false;

    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (!end_of_options && strcmp(argument, "--") == 0) {
            end_of_options = true;
            continue;
        }
        if (!end_of_options && (strcmp(argument, "-h") == 0 || strcmp(argument, "--help") == 0)) {
            print_usage(stdout);
            return 0;
        }
        if (!end_of_options && strcmp(argument, "--version") == 0) {
            printf("lmdbsql %s\n", LSQL_VERSION);
            return 0;
        }
        if (!end_of_options &&
            (strcmp(argument, "-c") == 0 || strcmp(argument, "--command") == 0)) {
            if (index + 1 >= argc) {
                fprintf(stderr, "lmdbsql: option '%s' requires an argument\n", argument);
                return 2;
            }
            options.sql = argv[++index];
            continue;
        }
        if (!end_of_options && strncmp(argument, "--command=", 10) == 0) {
            options.sql = argument + 10;
            continue;
        }
        if (!end_of_options && strcmp(argument, "--read-only") == 0) {
            options.read_only = true;
            continue;
        }
        if (!end_of_options && strcmp(argument, "--map-size") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "lmdbsql: option '--map-size' requires an argument\n");
                return 2;
            }
            if (!parse_size(argv[++index], &options.map_size)) {
                fprintf(stderr, "lmdbsql: invalid map size\n");
                return 2;
            }
            options.has_map_size = true;
            continue;
        }
        if (!end_of_options && strncmp(argument, "--map-size=", 11) == 0) {
            if (!parse_size(argument + 11, &options.map_size)) {
                fprintf(stderr, "lmdbsql: invalid map size\n");
                return 2;
            }
            options.has_map_size = true;
            continue;
        }
        if (!end_of_options && argument[0] == '-' && argument[1] != '\0') {
            fprintf(stderr, "lmdbsql: unknown option '%s'\n", argument);
            print_usage(stderr);
            return 2;
        }

        if (options.path == NULL) {
            options.path = argument;
        } else if (options.sql == NULL) {
            options.sql = argument;
        } else {
            fprintf(stderr, "lmdbsql: too many positional arguments\n");
            return 2;
        }
    }

    if (options.path == NULL) {
        print_usage(stderr);
        return 2;
    }

    if (!options.read_only) {
        if (mkdir(options.path, 0775) != 0 && errno != EEXIST) {
            fprintf(stderr, "lmdbsql: cannot create '%s': %s\n", options.path, strerror(errno));
            return 1;
        }
    }

    lsql_options db_options;
    lsql_options_init(&db_options);
    db_options.read_only = options.read_only;
    if (options.has_map_size) {
        db_options.map_size = options.map_size;
    }

    lsql_error error;
    lsql_conn *conn = NULL;
    lsql_status status = lsql_open(options.path, &db_options, &conn, &error);
    if (status != LSQL_OK) {
        fprintf(stderr, "lmdbsql: %s\n", lsql_error_message(&error));
        return 1;
    }

    char *stdin_text = NULL;
    size_t sql_length = 0;
    if (options.sql != NULL) {
        sql_length = strlen(options.sql);
    } else {
        if (!read_all(stdin, &stdin_text, &sql_length)) {
            fprintf(stderr, "lmdbsql: cannot read SQL from standard input\n");
            lsql_close(conn);
            return 1;
        }
        options.sql = stdin_text;
    }

    bool success = execute_script(conn, options.sql, sql_length);

    free(stdin_text);
    lsql_close(conn);
    return success ? 0 : 1;
}
