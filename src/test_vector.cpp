#include "h2unit.h"

extern "C" {
#include "vector.c"
}

UINT32 ret = 0;

H2UNIT(cvector)
{
   void setup() {
   }

   void teardown() {
   }
};

H2CASE(cvector, "create and free")
{
    struct DSVector *vec = ds_vector_create_capacity(20);
    ds_vector_free(vec);
}

H2CASE(cvector, "append test") {
    struct DSVector *vec = ds_vector_create_capacity(20);
    UINT8 input[] = {1, 2, 3, 4};
    ret = ds_vector_append(vec, input, 4);

    H2EQ_MATH(ret, sizeof(input));
    H2EQ_MATH(20, vec->capacity);
    H2EQ_MATH(4, vec->size);
    H2EQ_MEMCMP(input, vec->data, sizeof(input));
    ds_vector_free(vec);
}

H2CASE(cvector, "append gt capacity") {
    struct DSVector *vec = ds_vector_create_capacity(3);
    UINT8 input[] = {1, 2, 3, 4};
    ret = ds_vector_append(vec, input, 4);

    H2EQ_MATH(ret, sizeof(input));
    H2EQ_MATH(8, vec->capacity);
    H2EQ_MATH(4, vec->size);
    H2EQ_MEMCMP(input, vec->data, sizeof(input));
    ds_vector_free(vec);
}

H2CASE(cvector, "append and expand") {
    struct DSVector *vec = ds_vector_create_capacity(5);
    UINT8 input[] = {1, 2, 3, 4, 5};
    ret = ds_vector_append(vec, input, 5);

    H2EQ_MATH(ret, sizeof(input));
    H2EQ_MATH(12, vec->capacity);
    H2EQ_MATH(5, vec->size);
    H2EQ_MEMCMP(input, vec->data, sizeof(input));
    ds_vector_free(vec);
}

H2CASE(cvector, "insert test") {
    struct  DSVector *vec = ds_vector_create_capacity(5);
    UINT8 input[] = {1, 2, 3, 4};
    ret = ds_vector_insert(vec, 3, input, sizeof(input));
    H2EQ_MATH(ret, 0);

    ds_vector_free(vec);
}

H2CASE(cvector, "insert test") {
    struct  DSVector *vec = ds_vector_create_capacity(5);
    UINT8 input[] = {1, 2, 3, 4};
    ret = ds_vector_insert(vec, 0, input, sizeof(input));
    H2EQ_MATH(ret, sizeof(input));
    H2EQ_MATH(5, vec->capacity);
    H2EQ_MATH(4, vec->size);
    H2EQ_MEMCMP(input, vec->data, sizeof(input));
    ds_vector_free(vec);
}

H2CASE(cvector, "insert test") {
    struct  DSVector *vec = ds_vector_create_capacity(5);
    UINT8 input[] = {1, 2, 3, 4};
    UINT8 input2[] = {5, 6, 7, 8};
    UINT8 expresult[] = {1, 5, 6, 7, 8, 2, 3, 4};

    ret = ds_vector_insert(vec, 0, input, sizeof(input));
    H2EQ_MATH(ret, sizeof(input));
    H2EQ_MATH(5, vec->capacity);
    H2EQ_MATH(4, vec->size);
    H2EQ_MEMCMP(input, vec->data, sizeof(input));

    ret = ds_vector_insert(vec, 1, input2, sizeof(input2));
    H2EQ_MATH(ret, sizeof(input2));
    H2EQ_MATH(11, vec->capacity);
    H2EQ_MATH(8, vec->size);
    H2EQ_MEMCMP(expresult, vec->data, sizeof(input));

    ds_vector_free(vec);
}

H2CASE(cvector, "concat test") {
    struct DSVector *dest = ds_vector_create_capacity(5);
    struct DSVector *src = ds_vector_create_capacity(5);
    UINT8 input[] = {1, 2, 3, 4};
    UINT8 input2[] = {5, 6, 7, 8};
    UINT8 expresult[] = {1, 2, 3, 4, 5, 6, 7, 8};

    ds_vector_append(dest, input, sizeof(input));
    ds_vector_append(src, input2, sizeof(input2));

    ret = ds_vector_concat(dest, src);
    H2EQ_MEMCMP(expresult, dest->data, sizeof(expresult));
    H2EQ_MATH(4, ret);
    H2EQ_MATH(8, dest->size);
    H2EQ_MATH(11, dest->capacity);

    ds_vector_free(dest);
    ds_vector_free(src);
}

H2CASE(cvector, "sprintf") {
    struct DSVector *dest = ds_vector_create_capacity(20);
    UINT8 input[] = {1, 2, 3, 4};
    UINT8 expresult[] = {1, 2, 3, 4, '1', '2', '3'};
    ds_vector_append(dest, input, sizeof(input));
    ret = ds_vector_sprintf(dest, "hello%d", 123);

    H2EQ_MATH(8, ret);
    H2EQ_MATH(12, dest->size);
    H2EQ_MATH(20, dest->capacity);
    ds_vector_free(dest);
}

H2CASE(cvector, "sprintf") {
    struct DSVector *dest = ds_vector_create_capacity(10);
    UINT8 input[] = {1, 2, 3, 4};
    UINT8 expresult[] = {1, 2, 3, 4, 'h', 'e', 'l', 'l', 'o', '1', '2', '3'};
    ds_vector_append(dest, input, sizeof(input));
    ret = ds_vector_sprintf(dest, "hello%d", 123);

    H2EQ_MATH(8, ret);
    H2EQ_MATH(12, dest->size);
    H2EQ_MATH(23, dest->capacity);
    H2EQ_MEMCMP(expresult, dest->data, sizeof(expresult));

    ds_vector_free(dest);
}