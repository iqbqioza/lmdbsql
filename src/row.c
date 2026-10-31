#include "row.h"

#include <stdlib.h>
#include <string.h>

static void lsql_write_u32(unsigned char *data, uint32_t value) {
    data[0] = (unsigned char)(value >> 24);
    data[1] = (unsigned char)((value >> 16) & 0xFFu);
    data[2] = (unsigned char)((value >> 8) & 0xFFu);
    data[3] = (unsigned char)(value & 0xFFu);
}

static void lsql_write_u64(unsigned char *data, uint64_t value) {
    data[0] = (unsigned char)(value >> 56);
    data[1] = (unsigned char)((value >> 48) & 0xFFu);
    data[2] = (unsigned char)((value >> 40) & 0xFFu);
    data[3] = (unsigned char)((value >> 32) & 0xFFu);
    data[4] = (unsigned char)((value >> 24) & 0xFFu);
    data[5] = (unsigned char)((value >> 16) & 0xFFu);
    data[6] = (unsigned char)((value >> 8) & 0xFFu);
    data[7] = (unsigned char)(value & 0xFFu);
}

static uint32_t lsql_read_u32(const unsigned char *data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) |
           (uint32_t)data[3];
}

static uint64_t lsql_read_u64(const unsigned char *data) {
    return ((uint64_t)data[0] << 56) | ((uint64_t)data[1] << 48) | ((uint64_t)data[2] << 40) |
           ((uint64_t)data[3] << 32) | ((uint64_t)data[4] << 24) | ((uint64_t)data[5] << 16) |
           ((uint64_t)data[6] << 8) | (uint64_t)data[7];
}

static size_t lsql_null_bitmap_size(size_t column_count) {
    return (column_count + 7u) / 8u;
}

static bool lsql_add_size(size_t *size, size_t amount) {
    if (*size > SIZE_MAX - amount) {
        return false;
    }
    *size += amount;
    return true;
}

lsql_status lsql_row_encode(const lsql_schema *schema, const lsql_value *values,
                            unsigned char **out_data, size_t *out_size, lsql_error *error) {
    if (out_data == NULL || out_size == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }
    *out_data = NULL;
    *out_size = 0;

    if (schema == NULL || values == NULL) {
        return lsql_error_set(error, LSQL_ERROR_INVALID_ARGUMENT, LSQL_OFFSET_NONE, "invalid row");
    }

    size_t size = lsql_null_bitmap_size(schema->column_count);
    for (size_t index = 0; index < schema->column_count; ++index) {
        const lsql_value *value = &values[index];
        if (value->type == LSQL_TYPE_NULL) {
            continue;
        }

        switch (schema->columns[index].type) {
        case LSQL_TYPE_INTEGER:
            if (value->type != LSQL_TYPE_INTEGER) {
                return lsql_error_set(error, LSQL_ERROR_TYPE, LSQL_OFFSET_NONE,
                                      "column '%s' requires an INTEGER value",
                                      schema->columns[index].name);
            }
            if (!lsql_add_size(&size, 8u)) {
                return lsql_error_set(error, LSQL_ERROR_STORAGE, LSQL_OFFSET_NONE,
                                      "row is too large");
            }
            break;
        case LSQL_TYPE_REAL:
            if (value->type != LSQL_TYPE_REAL) {
                return lsql_error_set(error, LSQL_ERROR_TYPE, LSQL_OFFSET_NONE,
                                      "column '%s' requires a REAL value",
                                      schema->columns[index].name);
            }
            if (!lsql_add_size(&size, 8u)) {
                return lsql_error_set(error, LSQL_ERROR_STORAGE, LSQL_OFFSET_NONE,
                                      "row is too large");
            }
            break;
        case LSQL_TYPE_TEXT:
            if (value->type != LSQL_TYPE_TEXT) {
                return lsql_error_set(error, LSQL_ERROR_TYPE, LSQL_OFFSET_NONE,
                                      "column '%s' requires a TEXT value",
                                      schema->columns[index].name);
            }
            if (value->as.text.length > UINT32_MAX ||
                !lsql_add_size(&size, 4u + value->as.text.length)) {
                return lsql_error_set(error, LSQL_ERROR_STORAGE, LSQL_OFFSET_NONE,
                                      "row is too large");
            }
            break;
        case LSQL_TYPE_NULL:
            return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                  "invalid column type in table schema");
        }
    }

    unsigned char *data = malloc(size);
    if (data == NULL) {
        return lsql_error_set(error, LSQL_ERROR_OUT_OF_MEMORY, LSQL_OFFSET_NONE,
                              "out of memory while encoding row");
    }
    memset(data, 0, size);

    size_t bitmap_size = lsql_null_bitmap_size(schema->column_count);
    size_t offset = bitmap_size;
    for (size_t index = 0; index < schema->column_count; ++index) {
        const lsql_value *value = &values[index];
        if (value->type == LSQL_TYPE_NULL) {
            data[index / 8u] |= (unsigned char)(1u << (index % 8u));
            continue;
        }

        switch (schema->columns[index].type) {
        case LSQL_TYPE_INTEGER:
            lsql_write_u64(data + offset, (uint64_t)value->as.integer);
            offset += 8u;
            break;
        case LSQL_TYPE_REAL: {
            uint64_t bits;
            memcpy(&bits, &value->as.real, sizeof(bits));
            lsql_write_u64(data + offset, bits);
            offset += 8u;
            break;
        }
        case LSQL_TYPE_TEXT:
            lsql_write_u32(data + offset, (uint32_t)value->as.text.length);
            offset += 4u;
            memcpy(data + offset, value->as.text.data, value->as.text.length);
            offset += value->as.text.length;
            break;
        case LSQL_TYPE_NULL:
            break;
        }
    }

    *out_data = data;
    *out_size = size;
    return LSQL_OK;
}

lsql_status lsql_row_decode(const lsql_schema *schema, const void *data, size_t size,
                            lsql_value *out_values, lsql_error *error) {
    if (schema == NULL || data == NULL || out_values == NULL) {
        return LSQL_ERROR_INVALID_ARGUMENT;
    }

    const unsigned char *bytes = data;
    size_t bitmap_size = lsql_null_bitmap_size(schema->column_count);
    if (size < bitmap_size) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                              "corrupt row: truncated null bitmap");
    }

    memset(out_values, 0, schema->column_count * sizeof(*out_values));
    size_t offset = bitmap_size;
    for (size_t index = 0; index < schema->column_count; ++index) {
        bool is_null = (bytes[index / 8u] & (unsigned char)(1u << (index % 8u))) != 0;
        if (is_null) {
            out_values[index].type = LSQL_TYPE_NULL;
            continue;
        }

        switch (schema->columns[index].type) {
        case LSQL_TYPE_INTEGER:
            if (offset > size || size - offset < 8u) {
                return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                      "corrupt row: truncated INTEGER");
            }
            out_values[index].type = LSQL_TYPE_INTEGER;
            out_values[index].as.integer = (int64_t)lsql_read_u64(bytes + offset);
            offset += 8u;
            break;
        case LSQL_TYPE_REAL: {
            if (offset > size || size - offset < 8u) {
                return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                      "corrupt row: truncated REAL");
            }
            uint64_t bits = lsql_read_u64(bytes + offset);
            memcpy(&out_values[index].as.real, &bits, sizeof(bits));
            out_values[index].type = LSQL_TYPE_REAL;
            offset += 8u;
            break;
        }
        case LSQL_TYPE_TEXT: {
            if (offset > size || size - offset < 4u) {
                return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                      "corrupt row: truncated TEXT length");
            }
            uint32_t length = lsql_read_u32(bytes + offset);
            offset += 4u;
            if (offset > size || size - offset < length) {
                return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                      "corrupt row: truncated TEXT value");
            }
            out_values[index].type = LSQL_TYPE_TEXT;
            out_values[index].as.text.data = (const char *)(bytes + offset);
            out_values[index].as.text.length = length;
            offset += length;
            break;
        }
        case LSQL_TYPE_NULL:
            return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                                  "invalid column type in table schema");
        }
    }

    if (offset != size) {
        return lsql_error_set(error, LSQL_ERROR_CORRUPT, LSQL_OFFSET_NONE,
                              "corrupt row: trailing data");
    }
    return LSQL_OK;
}
