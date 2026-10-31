#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define LSQL_ARENA_BLOCK_SIZE 4096u

struct lsql_arena_block {
    lsql_arena_block *next;
    size_t capacity;
    size_t used;
    max_align_t data[];
};

static size_t lsql_align_size(size_t size) {
    const size_t alignment = _Alignof(max_align_t);
    if (size > SIZE_MAX - (alignment - 1u)) {
        return 0;
    }
    return (size + (alignment - 1u)) & ~(alignment - 1u);
}

void lsql_arena_init(lsql_arena *arena) {
    if (arena != NULL) {
        arena->head = NULL;
    }
}

void *lsql_arena_alloc(lsql_arena *arena, size_t size) {
    if (arena == NULL) {
        return NULL;
    }

    size_t aligned_size = lsql_align_size(size == 0 ? 1u : size);
    if (aligned_size == 0) {
        return NULL;
    }

    lsql_arena_block *block = arena->head;
    if (block == NULL || block->capacity - block->used < aligned_size) {
        size_t capacity =
            aligned_size > LSQL_ARENA_BLOCK_SIZE ? aligned_size : LSQL_ARENA_BLOCK_SIZE;
        if (sizeof(lsql_arena_block) > SIZE_MAX - capacity) {
            return NULL;
        }

        lsql_arena_block *new_block = calloc(1, sizeof(lsql_arena_block) + capacity);
        if (new_block == NULL) {
            return NULL;
        }
        new_block->next = arena->head;
        new_block->capacity = capacity;
        arena->head = new_block;
        block = new_block;
    }

    unsigned char *base = (unsigned char *)block->data;
    void *memory = base + block->used;
    block->used += aligned_size;
    memset(memory, 0, aligned_size);
    return memory;
}

char *lsql_arena_strndup(lsql_arena *arena, const char *text, size_t length) {
    if (text == NULL || length == SIZE_MAX) {
        return NULL;
    }

    char *copy = lsql_arena_alloc(arena, length + 1u);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, length);
    copy[length] = '\0';
    return copy;
}

char *lsql_arena_strdup(lsql_arena *arena, const char *text) {
    if (text == NULL) {
        return NULL;
    }
    return lsql_arena_strndup(arena, text, strlen(text));
}

void lsql_arena_destroy(lsql_arena *arena) {
    if (arena == NULL) {
        return;
    }

    lsql_arena_block *block = arena->head;
    while (block != NULL) {
        lsql_arena_block *next = block->next;
        free(block);
        block = next;
    }
    arena->head = NULL;
}
