#include"CXinYueConfig.h"
#if !defined(XLockFreeListConfig_H)&& XLockFreeList_ON
#define XLockFreeListConfig_H
#ifdef __cplusplus
extern "C" {
#endif
    // ==================== 跨平台 ABA 防护定义 ====================
 // 根据平台自动选择策略
#if defined(__x86_64__) || defined(_M_X64)
    // 64-bit x86-64: 利用高16位
#define PTR_BITS        (48)
#define VERSION_BITS    (16)
#define PTR_MASK        (0x0000FFFFFFFFFFFFULL)
#define VERSION_MASK    (0xFFFF000000000000ULL)

#elif defined(__i386__) || defined(_M_IX86) || defined(__arm__) || defined(__thumb__)
    // 32-bit platforms (x86, ARM): 假设8字节对齐，并利用高位 (e.g., bit31 on STM32/x86 user space)
#define PTR_ALIGN_BITS      (3) // 8-byte alignment -> bits[2:0] = 0
#define HIGH_UNUSED_BITS    (1) // e.g., bit31 is often 0 in user space
#define VERSION_BITS        (PTR_ALIGN_BITS + HIGH_UNUSED_BITS) // Total 4 bits
#define PTR_MASK            (0x7FFFFFF8U) // bits[30:3] for address
#define VERSION_LOW_MASK    (0x00000007U) // bits[2:0]
#define VERSION_HIGH_SHIFT  (31)          // Use bit31

#else
#error "Unsupported platform for ABA protection"
#endif

typedef uintptr_t tagged_ptr_t;

#ifdef __cplusplus
}
#endif
#endif // XLockFreeList_H