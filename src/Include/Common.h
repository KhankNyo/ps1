#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

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


#define FALL_THROUGH do { } while (0) 
#define IN_RANGE(lower, n, upper) ((lower) <= (n) && (n) <= (upper))
#define MASKED_LOAD(dst, src, mask) (dst = ((dst) & ~(mask)) | ((src) & (mask)))
#define KB 1024
#define MB (KB*KB)


#define RS 21
#define RT 16
#define RD 11
#define REG(Ins, R) (((Ins) >> R) & 0x1F)

#define OP(Ins) ((u32)(Ins) >> 26)
#define FUNCT(Ins) ((Ins) & 0x3F)
#define OP_GROUP(Ins) (((Ins) >> 29) & 0x7)
#define OP_MODE(Ins) (((Ins) >> 26) & 0x7)
#define FUNCT_MODE(Ins) ((Ins) & 0x7)
#define FUNCT_GROUP(Ins) (((Ins) >> 3) & 0x7)
#define SHAMT(Ins) (((Ins) >> 6) & 0x1F)


#endif /* COMMON_H */ 

