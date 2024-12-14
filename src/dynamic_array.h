#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct dynamic_array_t
{
    void *data;

    u32 len;
    u32 capacity;
    u32 size_per_element;
};

internal dynamic_array_t create_dynamic_array(u32 capacity, u32 size_per_element)
{
    dynamic_array_t result = {};

    result.len = 0;
    result.capacity = capacity;
    result.size_per_element = size_per_element;

    // TODO: Custom memory allocator.
    result.data = (void *)malloc(size_per_element * capacity);
    ASSERT(result.data);

    return result;
}

internal void delete_dynamic_array(dynamic_array_t *dynamic_array)
{
    ASSERT(dynamic_array);
    ASSERT(dynamic_array->data);

    free(dynamic_array->data);

    dynamic_array->len = 0;
    dynamic_array->capacity = 0;

    dynamic_array = NULL;
}

internal void push_to_dynamic_array(dynamic_array_t *dynamic_array, void *element)
{
    ASSERT(dynamic_array);
    ASSERT(dynamic_array->data);
    ASSERT(element);

    if (dynamic_array->len == dynamic_array->capacity)
    {
        // Capacity must become * 2.
        dynamic_array->capacity *= 2;
        dynamic_array->data = realloc(dynamic_array->data, dynamic_array->capacity * dynamic_array->size_per_element);
        ASSERT(dynamic_array->data);
    }

    u8 *destination = (u8 *)dynamic_array->data + dynamic_array->len++ * dynamic_array->size_per_element;
    u8 *source = (u8 *)element;

    memcpy(destination, source, dynamic_array->size_per_element);
}

internal void *get_from_dynamic_array(dynamic_array_t *dynamic_array, u32 index)
{
    ASSERT(dynamic_array);
    ASSERT(dynamic_array->data);

    ASSERT(index < dynamic_array->len);

    void *element = ((u8 *)dynamic_array->data + index * dynamic_array->size_per_element);
    ASSERT(element);

    return element;
}
