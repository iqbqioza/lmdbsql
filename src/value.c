#include "internal.h"

#include <math.h>
#include <string.h>

bool lsql_identifier_is_valid(const char *name, size_t length) {
    if (name == NULL || length == 0 || length > LSQL_MAX_IDENTIFIER_LENGTH) {
        return false;
    }

    unsigned char first = (unsigned char)name[0];
    if (!((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z') || first == '_')) {
        return false;
    }

    for (size_t index = 1; index < length; ++index) {
        unsigned char character = (unsigned char)name[index];
        bool valid = (character >= 'A' && character <= 'Z') ||
                     (character >= 'a' && character <= 'z') ||
                     (character >= '0' && character <= '9') || character == '_';
        if (!valid) {
            return false;
        }
    }
    return true;
}

static bool lsql_ascii_equal_ignore_case(const char *left, const char *right, size_t length) {
    for (size_t index = 0; index < length; ++index) {
        unsigned char a = (unsigned char)left[index];
        unsigned char b = (unsigned char)right[index];
        if (a >= 'A' && a <= 'Z') {
            a = (unsigned char)(a - 'A' + 'a');
        }
        if (b >= 'A' && b <= 'Z') {
            b = (unsigned char)(b - 'A' + 'a');
        }
        if (a != b) {
            return false;
        }
    }
    return true;
}

bool lsql_type_from_name(const char *name, size_t length, lsql_type *out_type) {
    static const struct {
        const char *name;
        lsql_type type;
    } types[] = {
        {"integer", LSQL_TYPE_INTEGER}, {"int", LSQL_TYPE_INTEGER}, {"real", LSQL_TYPE_REAL},
        {"double", LSQL_TYPE_REAL},     {"text", LSQL_TYPE_TEXT},
    };

    if (name == NULL || out_type == NULL) {
        return false;
    }

    for (size_t index = 0; index < LSQL_ARRAY_LENGTH(types); ++index) {
        size_t candidate_length = strlen(types[index].name);
        if (candidate_length == length &&
            lsql_ascii_equal_ignore_case(name, types[index].name, length)) {
            *out_type = types[index].type;
            return true;
        }
    }
    return false;
}

const char *lsql_type_name(lsql_type type) {
    switch (type) {
    case LSQL_TYPE_INTEGER:
        return "INTEGER";
    case LSQL_TYPE_REAL:
        return "REAL";
    case LSQL_TYPE_TEXT:
        return "TEXT";
    case LSQL_TYPE_NULL:
        return "NULL";
    }
    return "UNKNOWN";
}

bool lsql_value_is_null(const lsql_value *value) {
    return value == NULL || value->type == LSQL_TYPE_NULL;
}

bool lsql_value_truthy(const lsql_value *value) {
    if (value == NULL) {
        return false;
    }

    switch (value->type) {
    case LSQL_TYPE_INTEGER:
        return value->as.integer != 0;
    case LSQL_TYPE_REAL:
        return value->as.real != 0.0;
    case LSQL_TYPE_TEXT:
        return value->as.text.length != 0;
    case LSQL_TYPE_NULL:
        return false;
    }
    return false;
}

int lsql_value_compare(const lsql_value *left, const lsql_value *right, bool *comparable) {
    if (comparable != NULL) {
        *comparable = false;
    }
    if (left == NULL || right == NULL || left->type == LSQL_TYPE_NULL ||
        right->type == LSQL_TYPE_NULL) {
        return 0;
    }

    if (left->type == LSQL_TYPE_INTEGER && right->type == LSQL_TYPE_INTEGER) {
        if (comparable != NULL) {
            *comparable = true;
        }
        if (left->as.integer < right->as.integer) {
            return -1;
        }
        return left->as.integer > right->as.integer ? 1 : 0;
    }

    if ((left->type == LSQL_TYPE_INTEGER || left->type == LSQL_TYPE_REAL) &&
        (right->type == LSQL_TYPE_INTEGER || right->type == LSQL_TYPE_REAL)) {
        double left_number =
            left->type == LSQL_TYPE_INTEGER ? (double)left->as.integer : left->as.real;
        double right_number =
            right->type == LSQL_TYPE_INTEGER ? (double)right->as.integer : right->as.real;
        if (isnan(left_number) || isnan(right_number)) {
            return 0;
        }
        if (comparable != NULL) {
            *comparable = true;
        }
        if (left_number < right_number) {
            return -1;
        }
        return left_number > right_number ? 1 : 0;
    }

    if (left->type == LSQL_TYPE_TEXT && right->type == LSQL_TYPE_TEXT) {
        size_t common = left->as.text.length < right->as.text.length ? left->as.text.length
                                                                     : right->as.text.length;
        int result = common == 0 ? 0 : memcmp(left->as.text.data, right->as.text.data, common);
        if (result != 0) {
            if (comparable != NULL) {
                *comparable = true;
            }
            return result < 0 ? -1 : 1;
        }
        if (comparable != NULL) {
            *comparable = true;
        }
        if (left->as.text.length < right->as.text.length) {
            return -1;
        }
        return left->as.text.length > right->as.text.length ? 1 : 0;
    }

    return 0;
}
