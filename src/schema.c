#include "schema.h"

#include <stdlib.h>
#include <string.h>

#define LSQL_SCHEMA_VERSION 1u
#define LSQL_SCHEMA_HEADER_SIZE 7u
#define LSQL_SCHEMA_MAGIC_0 'L'
#define LSQL_SCHEMA_MAGIC_1 'S'
#define LSQL_SCHEMA_MAGIC_2 'Q'
#define LSQL_SCHEMA_MAGIC_3 'L'

static void lsql_write_u16(unsigned char *data, uint16_t value) {
    data[0] = (unsigned char)(value >> 8);
    data[1] = (unsigned char)(value & 0xFFu);
}

static bool lsql_read_u16(const unsigned char *data, size_t size, size_t *offset,
                          uint16_t *out_value) {
    if (*offset > size || size - *offset < 2u) {
        return false;
    }
    *out_value = (uint16_t)(((uint16_t)data[*offset] << 8) | (uint16_t)data[*offset + 1u]);
    *offset += 2u;
    return true;
}

lsql_status lsql_schema_from_definitions(const lsql_column_definition *definitions, size_t count,
                                         lsql_arena *arena, lsql_schema *out_schema,
                                         lsql_error *error) {
    if (definitions == NULL || arena == NULL || out_schema == NULL || error == NULL || count == 0 ||
        count > LSQL_MAX_COLUMNS) {
        return lsql_error_set(error, LSQL_ERROR_INVALID_ARGUMENT, LSQL_OFFSET_NONE,
                              "invalid table definition");
    }

    lsql_column *columns = lsql_arena_alloc(arena, count * sizeof(*columns));
    if (columns == NULL) {
        return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                              "out of memory while creating table schema");
    }

    for (size_t index = 0; index < count; ++index) {
        columns[index].name = definitions[index].name;
        columns[index].type = definitions[index].type;
    }

    *out_schema = (lsql_schema){.column_count = count, .columns = columns};
    return lsql_schema_validate(out_schema, error);
}

lsql_status lsql_schema_validate(const lsql_schema *schema, lsql_error *error) {
    if (schema == NULL || schema->columns == NULL || schema->column_count == 0 ||
        schema->column_count > LSQL_MAX_COLUMNS) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE, "invalid table schema");
    }

    for (size_t index = 0; index < schema->column_count; ++index) {
        const lsql_column *column = &schema->columns[index];
        if (column->name == NULL || !lsql_identifier_is_valid(column->name, strlen(column->name))) {
            return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                  "invalid column name in table schema");
        }
        if (strcmp(column->name, "rowid") == 0) {
            return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                  "column name 'rowid' is reserved");
        }
        if (column->type != LSQL_TYPE_INTEGER && column->type != LSQL_TYPE_REAL &&
            column->type != LSQL_TYPE_TEXT) {
            return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                  "invalid column type in table schema");
        }
        for (size_t other = 0; other < index; ++other) {
            if (strcmp(schema->columns[other].name, column->name) == 0) {
                return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                      "duplicate column '%s' in table schema", column->name);
            }
        }
    }
    return LSQL_OK;
}

int lsql_schema_find_column(const lsql_schema *schema, const char *name) {
    if (schema == NULL || name == NULL) {
        return -1;
    }
    for (size_t index = 0; index < schema->column_count; ++index) {
        if (strcmp(schema->columns[index].name, name) == 0) {
            return (int)index;
        }
    }
    return -1;
}

lsql_status lsql_schema_encode(const lsql_schema *schema, unsigned char **out_data,
                               size_t *out_size, lsql_error *error) {
    if (out_data == NULL || out_size == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }
    *out_data = NULL;
    *out_size = 0;

    lsql_status status = lsql_schema_validate(schema, error);
    if (status != LSQL_OK) {
        return status;
    }

    size_t size = LSQL_SCHEMA_HEADER_SIZE;
    for (size_t index = 0; index < schema->column_count; ++index) {
        size_t name_length = strlen(schema->columns[index].name);
        if (name_length > UINT16_MAX || size > SIZE_MAX - (3u + name_length)) {
            return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                  "table schema is too large");
        }
        size += 3u + name_length;
    }

    unsigned char *data = malloc(size);
    if (data == NULL) {
        return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                              "out of memory while encoding table schema");
    }

    data[0] = LSQL_SCHEMA_MAGIC_0;
    data[1] = LSQL_SCHEMA_MAGIC_1;
    data[2] = LSQL_SCHEMA_MAGIC_2;
    data[3] = LSQL_SCHEMA_MAGIC_3;
    data[4] = LSQL_SCHEMA_VERSION;
    lsql_write_u16(data + 5u, (uint16_t)schema->column_count);

    size_t offset = LSQL_SCHEMA_HEADER_SIZE;
    for (size_t index = 0; index < schema->column_count; ++index) {
        const lsql_column *column = &schema->columns[index];
        size_t name_length = strlen(column->name);
        data[offset] = (unsigned char)column->type;
        lsql_write_u16(data + offset + 1u, (uint16_t)name_length);
        memcpy(data + offset + 3u, column->name, name_length);
        offset += 3u + name_length;
    }

    *out_data = data;
    *out_size = size;
    return LSQL_OK;
}

lsql_status lsql_schema_decode(const void *data, size_t size, lsql_arena *arena,
                               lsql_schema *out_schema, lsql_error *error) {
    if (data == NULL || arena == NULL || out_schema == NULL || error == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }
    if (size < LSQL_SCHEMA_HEADER_SIZE) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                              "corrupt table schema: truncated header");
    }

    const unsigned char *bytes = data;
    if (bytes[0] != LSQL_SCHEMA_MAGIC_0 || bytes[1] != LSQL_SCHEMA_MAGIC_1 ||
        bytes[2] != LSQL_SCHEMA_MAGIC_2 || bytes[3] != LSQL_SCHEMA_MAGIC_3) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                              "corrupt table schema: bad magic");
    }
    if (bytes[4] != LSQL_SCHEMA_VERSION) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                              "unsupported table schema version");
    }

    uint16_t column_count = (uint16_t)(((uint16_t)bytes[5] << 8) | (uint16_t)bytes[6]);
    if (column_count == 0 || column_count > LSQL_MAX_COLUMNS) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                              "corrupt table schema: invalid column count");
    }

    lsql_column *columns = lsql_arena_alloc(arena, (size_t)column_count * sizeof(*columns));
    if (columns == NULL) {
        return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                              "out of memory while decoding table schema");
    }

    size_t offset = LSQL_SCHEMA_HEADER_SIZE;
    for (uint16_t index = 0; index < column_count; ++index) {
        if (offset >= size) {
            return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                  "corrupt table schema: truncated column");
        }
        uint8_t type = bytes[offset++];
        if (type != LSQL_TYPE_INTEGER && type != LSQL_TYPE_REAL && type != LSQL_TYPE_TEXT) {
            return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                  "corrupt table schema: invalid column type");
        }

        uint16_t name_length;
        if (!lsql_read_u16(bytes, size, &offset, &name_length) || name_length == 0 ||
            name_length > LSQL_MAX_IDENTIFIER_LENGTH || offset > size ||
            size - offset < name_length) {
            return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                  "corrupt table schema: invalid column name");
        }

        const char *name = lsql_arena_strndup(arena, (const char *)(bytes + offset), name_length);
        if (name == NULL) {
            return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                                  "out of memory while decoding table schema");
        }
        offset += name_length;

        columns[index].name = name;
        columns[index].type = (lsql_type)type;
    }

    if (offset != size) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                              "corrupt table schema: trailing data");
    }

    lsql_schema schema = {.column_count = column_count, .columns = columns};
    lsql_status status = lsql_schema_validate(&schema, error);
    if (status != LSQL_OK) {
        return status;
    }
    *out_schema = schema;
    return LSQL_OK;
}
