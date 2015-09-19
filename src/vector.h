#ifndef __LIBDS_VECTOR_H__
#define __LIBDS_VECTOR_H__

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
//#include "include/mys_include.h"
typedef unsigned char UINT8;
typedef int INT32;
typedef unsigned int UINT32;
typedef char MYBOOL;

#define E_NO_MEM 101
#define TRUE    1
#define FALSE   0
/* some private constants for vector tuning */

struct DSVector {
    UINT32 size;
    UINT32 capacity;
    UINT8* data;
};

/**
 * Creates a vector with DS_VECTOR_BASE_CAPACITY.
 * ds_vector_free or ds_vector_free_no_data will need to be called
 * when done with the vector to avoid memory leaks.
 */
struct DSVector *ds_vector_create(UINT32 capacity, float expand_ratio);

/**
 * Creates a vector with the given capacity.
 * (N.B. This vector will still automatically increase in size if necessary.)
 */
struct DSVector *ds_vector_create_capacity(UINT32 capacity);

/**
 * Free's a vector AND its data.
 */
void ds_vector_free(struct DSVector *vec);
/**
 * Adds an element to the end of a vector.
 * Runs in constant time.
 */
UINT32 ds_vector_append(struct DSVector *vec, UINT8* data, UINT32 length);

/**
 * Places an element at index i, and shifts the rest of the vector
 * to the right by one. If index == size of vector, then the element
 * will be appended to the end of the vector.
 */
UINT32 ds_vector_insert(struct DSVector *vec, UINT32 index, UINT8* data, UINT32 length);

UINT32 ds_vector_concat(struct DSVector *dest, struct DSVector *src);

UINT32 ds_vector_sprintf(struct DSVector *dest, const char* format, ...);

#endif

