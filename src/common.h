#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdio.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef float f32;
typedef double f64;

#define ASSERT(x)                                                                                                      \
    if (!x)                                                                                                            \
    {                                                                                                                  \
        int *ptr = NULL;                                                                                               \
        *ptr = 0;                                                                                                      \
    }

#define SECONDS_IN_NS(x) (u64)(x * 1e9)

#define internal static
#define local_persist static
#define global_variable static

#endif