/**
 * @file cc.h
 * @brief lwIP 编译器/平台抽象层
 */
#ifndef __CC_H__
#define __CC_H__

#include <stdio.h>
#include <stdint.h>
#include "XPrintf.h"

typedef unsigned   char    u8_t;
typedef signed     char    s8_t;
typedef unsigned   short   u16_t;
typedef signed     short   s16_t;
typedef unsigned   long    u32_t;
typedef signed     long    s32_t;
typedef uintptr_t          mem_ptr_t;
typedef u32_t              sys_prot_t;

#define BYTE_ORDER LITTLE_ENDIAN

#if defined (__ICCARM__)
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_USE_INCLUDES
#elif defined (__CC_ARM)
#define PACK_STRUCT_BEGIN __packed
#define PACK_STRUCT_STRUCT
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#elif defined (__GNUC__)
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__ ((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#elif defined (__TASKING__)
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#elif defined(_MSC_VER)
#define PACK_STRUCT_BEGIN __pragma(pack(push,1))
#define PACK_STRUCT_STRUCT
#define PACK_STRUCT_END   __pragma(pack(pop))
#define PACK_STRUCT_FIELD(x) x
#else
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x
#endif

#define U16_F "4d"
#define S16_F "4d"
#define X16_F "4x"
#define U32_F "8ld"
#define S32_F "8ld"
#define X32_F "8lx"

#ifndef LWIP_PLATFORM_ASSERT
#define LWIP_PLATFORM_ASSERT(x) \
    do { printf("Assertion \"%s\" at %s:%d\r\n", x, __FILE__, __LINE__); } while(0)
#endif

/* 使用 printf + fflush 输出调试，这是 lwIP 官方推荐方式，\n 换行正常 */
#ifndef LWIP_PLATFORM_DIAG
#define LWIP_PLATFORM_DIAG(x) do { printf x; fflush(stdout); } while(0)
#endif

#ifndef LWIP_RAND
#include "XRandomGenerator.h"
#define LWIP_RAND()  XRandomGenerator_generate(XRandomGenerator_system())
#endif

#endif /* __CC_H__ */