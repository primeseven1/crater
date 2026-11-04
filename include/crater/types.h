#pragma once

#include <crater/common.h>

#ifndef __LP64__
#error "LP64 not used, cannot compile!"
#endif /* __LP64__ */

typedef __INT8_TYPE__ i8;
typedef __UINT8_TYPE__ u8;
typedef __INT16_TYPE__ i16;
typedef __UINT16_TYPE__ u16;
typedef __INT32_TYPE__ i32;
typedef __UINT32_TYPE__ u32;
typedef __INT64_TYPE__ i64;
typedef __UINT64_TYPE__ u64;

static_assert(sizeof(__SIZE_TYPE__) == sizeof(unsigned long),
		"sizeof(__SIZE_TYPE__) == sizeof(unsigned long)");
typedef long ssize_t;
typedef unsigned long size_t;
typedef __INTPTR_TYPE__ intptr_t;
typedef __UINTPTR_TYPE__ uintptr_t;
typedef __PTRDIFF_TYPE__ ptrdiff_t;
typedef u64 physaddr_t;

static_assert((__WCHAR_TYPE__)(-1) < 0 && sizeof(__WCHAR_TYPE__) == sizeof(i32),
		"(__WCHAR_TYPE__)(-1) < 0 && sizeof(__WCHAR_TYPE__) == sizeof(i32)");
typedef i32 wchar_t;

typedef _Bool bool;
#define true 1
#define false 0

typedef __builtin_va_list va_list;
#define va_start(v, p) __builtin_va_start(v, p)
#define va_end(v) __builtin_va_end(v)
#define va_arg(v, t) __builtin_va_arg(v, t)
#define va_copy(d, s) __builtin_va_copy(d, s)

#define UCHAR_MAX ((unsigned char)-1)
#define SCHAR_MAX ((signed char)(UCHAR_MAX >> 1))
#define SCHAR_MIN ((signed char)(-SCHAR_MAX - 1))
#ifndef __CHAR_UNSIGNED__
#define CHAR_MAX SCHAR_MAX
#define CHAR_MIN SCHAR_MIN
#else
#define CHAR_MAX UCHAR_MAX
#define CHAR_MIN 0
#endif /* __CHAR_UNSIGNED__ */
#define USHRT_MAX ((unsigned short)-1)
#define SHRT_MAX ((short)(USHRT_MAX >> 1))
#define SHRT_MIN ((short)(-SHRT_MAX - 1))
#define UINT_MAX ((unsigned int)-1)
#define INT_MAX ((int)(UINT_MAX >> 1))
#define INT_MIN ((int)(-INT_MAX - 1))
#define ULONG_MAX ((unsigned long)-1)
#define LONG_MAX ((long)(ULONG_MAX >> 1))
#define LONG_MIN ((long)(-LONG_MAX - 1))
#define ULLONG_MAX ((unsigned long long)-1)
#define LLONG_MAX ((long long)(ULLONG_MAX >> 1))
#define LLONG_MIN (-LLONG_MAX - 1)

#define U8_MAX ((u8)-1)
#define I8_MAX ((i8)(U8_MAX >> 1))
#define I8_MIN ((i8)(-I8_MAX - 1))
#define U16_MAX ((u16)-1)
#define I16_MAX ((i16)(U16_MAX >> 1))
#define I16_MIN ((i16)(-I16_MAX - 1))
#define U32_MAX ((u32)-1)
#define I32_MAX ((i32)(U32_MAX >> 1))
#define I32_MIN ((i32)(-I32_MAX - 1))
#define U64_MAX ((u64)-1)
#define I64_MAX ((i64)(U64_MAX >> 1))
#define I64_MIN ((i64)(-I64_MAX - 1))

#define SIZE_MAX ((size_t)-1)
#define SSIZE_MAX ((ssize_t)(SIZE_MAX >> 1))
#define SSIZE_MIN ((ssize_t))(-SSIZE_MAX - 1)
#define UINTPTR_MAX ((uintptr_t)-1)
#define INTPTR_MAX ((intptr_t)(UINTPTR_MAX >> 1))
#define INTPTR_MIN ((intptr_t)(-INTPTR_MAX - 1))
#define PHYSADDR_MAX ((physaddr_t)-1)

#define WCHAR_MAX I32_MAX
#define WCHAR_MIN I32_MIN
