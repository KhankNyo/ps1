#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef uint64_t u64;
typedef int64_t i64;
typedef uint32_t u32;
typedef int32_t i32;
typedef uint16_t u16;
typedef int16_t i16;
typedef uint8_t u8;
typedef int8_t i8;

typedef uint8_t Bool8;
typedef intptr_t iSize;
typedef unsigned uint;

#ifndef false
#  define false 0
#endif /* false */
#ifndef true 
#  define true 1
#endif /* true */

typedef struct StringView 
{
    const char *Ptr;
    int Len;
} StringView;


#define STRFY_2(x) #x
#define STRFY_1(x) STRFY_2(x)
#define STRFY(x) STRFY_1(x)

#define FALL_THROUGH do { } while (0) 
#define IN_RANGE(lower, n, upper) ((lower) <= (n) && (n) <= (upper))
#define MASKED_LOAD(dst, src, mask) (dst = ((dst) & ~(mask)) | ((src) & (mask)))
#define STATIC_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define MIN(a, b) ((a) > (b)? (b) : (a))
#define MAX(a, b) ((a) < (b)? (b) : (a))

#define KB 1024
#define MB (1024*1024)


#define TODO(Msg) do {\
    fprintf(stderr, "TODO in function %s of %s on line %d: \n"Msg"\n", __func__, __FILE__, __LINE__);\
    abort();\
} while (0)
#define UNREACHABLE(Msg) do {\
    fprintf(stderr, "Unreachable in function %s of %s on line %d: \n"Msg"\n", __func__, __FILE__, __LINE__);\
    abort();\
} while (0)
#ifdef DEBUG
#   define ASSERT(expr) do {\
        if (!(expr)) {\
            fprintf(stderr, "ASSERTION FAILED in "__FILE__\
                " in function '%s'"\
                " on line "STRFY(__LINE__)": \n"\
                "\t"STRFY(expr), \
                __func__\
            );\
            abort();\
        }\
    } while (0)
#else
#   define ASSERT(expr) 
#endif /* DEBUG */


#define RS 21
#define RT 16
#define RD 11
#define REG(ins, r) (((ins) >> r) & 0x1F)

#define OP(ins) ((u32)(ins) >> 26)
#define FUNCT(ins) ((ins) & 0x3F)
#define OP_GROUP(ins) (((ins) >> 29) & 0x7)
#define OP_MODE(ins) (((ins) >> 26) & 0x7)
#define FUNCT_MODE(ins) ((ins) & 0x7)
#define FUNCT_GROUP(ins) (((ins) >> 3) & 0x7)
#define SHAMT(ins) (((ins) >> 6) & 0x1F)


#endif /* COMMON_H */ 

