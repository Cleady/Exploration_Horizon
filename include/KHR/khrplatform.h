#ifndef __khrplatform_h_
#define __khrplatform_h_

/*
** Copyright (c) 2008-2018 The Khronos Group Inc.
** ...
*/

#if defined(_WIN32) && !defined(__SCITECH_SNAP__)
#   define KHRONOS_APICALL __declspec(dllimport)
#   define KHRONOS_APIENTRY __stdcall
#else
#   define KHRONOS_APICALL
#   define KHRONOS_APIENTRY
#endif

#if defined(__GNUC__)
#   define KHRONOS_APIATTRIBUTES
#else
#   define KHRONOS_APIATTRIBUTES
#endif

#include <stdint.h>
#include <stddef.h>

typedef int32_t                 khronos_int32_t;
typedef uint32_t                khronos_uint32_t;
typedef int64_t                 khronos_int64_t;
typedef uint64_t                khronos_uint64_t;

typedef int8_t                  khronos_int8_t;
typedef uint8_t                 khronos_uint8_t;
typedef int16_t                 khronos_int16_t;
typedef uint16_t                khronos_uint16_t;

typedef float                   khronos_float_t;

typedef intptr_t                khronos_intptr_t;
typedef uintptr_t               khronos_uintptr_t;
typedef int32_t                 khronos_ssize_t;
typedef uint32_t                khronos_usize_t;
typedef size_t                  khronos_size_t;

typedef uint64_t                khronos_utime_nanoseconds_t;
typedef int64_t                 khronos_stime_nanoseconds_t;

#endif
