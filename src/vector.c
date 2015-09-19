#include <stdlib.h>
#include <string.h>

#include "vector.h"

static INT32 DS_VECTOR_BASE_CAPACITY = 10;
static float DS_VECTOR_EXPAND_RATIO = 1.5;

/* private function to check and possibly expand a vector's capacity */
static MYBOOL ds_vector_maybe_expand(struct DSVector *vec, UINT32 length)
{
    UINT8* new_data = NULL;
    if (vec->size  + length < vec->capacity)
        return TRUE;

    vec->capacity = (INT32)(vec->capacity * DS_VECTOR_EXPAND_RATIO + length);
    new_data = (UINT8 *)realloc(vec->data, vec->capacity * sizeof(vec->data));
    if (!new_data) {
        return FALSE;
    }

    vec->data = new_data;
    return TRUE;
}

struct DSVector *ds_vector_create(UINT32 capacity, float expand_ratio)
{
    struct DSVector *vec = NULL;
    vec = (struct DSVector *)malloc(sizeof(*vec));
    if (!vec) {
        return NULL;
    }
    vec->size = 0;
    vec->capacity = capacity;
    DS_VECTOR_BASE_CAPACITY = capacity;
    DS_VECTOR_EXPAND_RATIO = expand_ratio;
    vec->data = (UINT8 *)malloc(vec->capacity * sizeof(UINT8));
    if (!vec->data) {
        free(vec);
        return NULL;
    }

    return vec;
}

struct DSVector *ds_vector_create_capacity(UINT32 capacity)
{
    return ds_vector_create(capacity, DS_VECTOR_EXPAND_RATIO);
}

void ds_vector_free(struct DSVector *vec)
{
    if (!vec) {
        return;
    }

    free(vec->data);
    free(vec);
}

UINT32 ds_vector_append(struct DSVector *vec, UINT8* data, UINT32 length)
{
    UINT32 i = 0;
    if (!vec || !data || length <= 0) {
        return 0;
    }

    if (!ds_vector_maybe_expand(vec, length)) {
        return 0;
    }
    for (; i < length; ++i) {
        vec->data[vec->size++] = data[i];
    }
    return length;
}

UINT32 ds_vector_insert(struct DSVector *vec, UINT32 index, UINT8* data, UINT32 length)
{
    UINT32 i;

    if (!vec || !data|| index > vec->size) {
        return 0;
    }

    if (!ds_vector_maybe_expand(vec, length)) {
        return 0;
    }

    for (i = vec->size - 1; i >= index && i < (i + 1); --i) {
        vec->data[i + length] = vec->data[i];
    }

    for (i = 0; i < length; ++i) {
        vec->data[index + i] = data[i];
        ++vec->size;
    }

    return length;
}

UINT32 ds_vector_concat(struct DSVector *dest, struct DSVector *src) {
    if (!dest || !src) {
        return 0;
    }

    return ds_vector_append(dest, src->data, src->size);
}

UINT32 ds_vector_sprintf(struct DSVector *dest, const char* format, ...) {
    UINT32 size = 0, actually_size = 0;
    va_list arg;
    va_start(arg, format);
    size = vsnprintf(NULL, 0, format, arg);
    if (!ds_vector_maybe_expand(dest, size)) {
        return 0;
    }
    va_start(arg, format);
    actually_size = vsnprintf((char *)&dest->data[dest->size], size + 1, format, arg);
    dest->size += actually_size;
    return actually_size;
}