// XAtomic_Keil.c
#if defined(__CC_ARM) || defined(__ARMCC_VERSION) || defined(__clang__)

#include "XAtomic.h"
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

// ========== 内存屏障 ==========
// ARM DMB (Data Memory Barrier) 指令
static inline void __dmb(void)
{
    __asm volatile ("dmb" ::: "memory");
}

void XAtomic_memory_barrier()
{
    __dmb();
}

void XAtomic_memory_barrier_acquire()
{
    __dmb();
}

void XAtomic_memory_barrier_release()
{
    __dmb();
}

// ========== 编译器特定实现 ==========
#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050) || defined(__clang__)
// ========== ARM Compiler 6 / Clang (armclang) ==========
// 这些编译器支持 __atomic 内置函数，实现方式与 GCC 非常相似

// --- 内存序映射 ---
static inline int xatomic_to_gcc_memory_order(XAtomic_MemoryOrder order)
{
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed: return __ATOMIC_RELAXED;
    case XAtomic_MemoryOrder_Consume: return __ATOMIC_CONSUME;
    case XAtomic_MemoryOrder_Acquire: return __ATOMIC_ACQUIRE;
    case XAtomic_MemoryOrder_Release: return __ATOMIC_RELEASE;
    case XAtomic_MemoryOrder_AcqRel:  return __ATOMIC_ACQ_REL;
    case XAtomic_MemoryOrder_SeqCst:  return __ATOMIC_SEQ_CST;
    default: return __ATOMIC_SEQ_CST;
    }
}

// --- Load ---
bool XAtomic_load_bool(const XAtomic_bool* var, XAtomic_MemoryOrder order)
{
    return __atomic_load_n(&(var->value), xatomic_to_gcc_memory_order(order));
}
int32_t XAtomic_load_int32(const XAtomic_int32_t* var, XAtomic_MemoryOrder order)
{
    return __atomic_load_n(&(var->value), xatomic_to_gcc_memory_order(order));
}
uint32_t XAtomic_load_uint32(const XAtomic_uint32_t* var, XAtomic_MemoryOrder order)
{
    return __atomic_load_n(&(var->value), xatomic_to_gcc_memory_order(order));
}
int64_t XAtomic_load_int64(const XAtomic_int64_t* var, XAtomic_MemoryOrder order)
{
    // 对于 Cortex-M，64位原子操作通常不被硬件支持，这里保留以供 A-profile 使用
    // 在 M-profile 上应避免使用或由软件模拟（性能差）
    return __atomic_load_n(&(var->value), xatomic_to_gcc_memory_order(order));
}
uint64_t XAtomic_load_uint64(const XAtomic_uint64_t* var, XAtomic_MemoryOrder order)
{
    return __atomic_load_n(&(var->value), xatomic_to_gcc_memory_order(order));
}
size_t XAtomic_load_size_t(const XAtomic_size_t* var, XAtomic_MemoryOrder order)
{
    return __atomic_load_n(&(var->value), xatomic_to_gcc_memory_order(order));
}
uintptr_t XAtomic_load_uintptr_t(const XAtomic_uintptr_t* var, XAtomic_MemoryOrder order)
{
    return __atomic_load_n(&(var->value), xatomic_to_gcc_memory_order(order));
}

// --- Store ---
void XAtomic_store_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
{
    __atomic_store_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
void XAtomic_store_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    __atomic_store_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
void XAtomic_store_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    __atomic_store_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
void XAtomic_store_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    __atomic_store_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
void XAtomic_store_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    __atomic_store_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
void XAtomic_store_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    __atomic_store_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
void XAtomic_store_uintptr_t(XAtomic_uintptr_t* var, uintptr_t value, XAtomic_MemoryOrder order)
{
    __atomic_store_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}

// --- Exchange ---
bool XAtomic_exchange_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
{
    return __atomic_exchange_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
int32_t XAtomic_exchange_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    return __atomic_exchange_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
uint32_t XAtomic_exchange_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    return __atomic_exchange_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
int64_t XAtomic_exchange_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    return __atomic_exchange_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
uint64_t XAtomic_exchange_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    return __atomic_exchange_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
size_t XAtomic_exchange_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    return __atomic_exchange_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
uintptr_t XAtomic_exchange_uintptr_t(XAtomic_uintptr_t* var, uintptr_t value, XAtomic_MemoryOrder order)
{
    return __atomic_exchange_n(&(var->value), value, xatomic_to_gcc_memory_order(order));
}

// --- Compare Exchange ---
bool XAtomic_compare_exchange_strong_bool(XAtomic_bool* var, bool* expected, bool desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    return __atomic_compare_exchange_n(&(var->value), expected, desired, false, xatomic_to_gcc_memory_order(success_order), xatomic_to_gcc_memory_order(failure_order));
}
bool XAtomic_compare_exchange_strong_int32(XAtomic_int32_t* var, int32_t* expected, int32_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    return __atomic_compare_exchange_n(&(var->value), expected, desired, false, xatomic_to_gcc_memory_order(success_order), xatomic_to_gcc_memory_order(failure_order));
}
bool XAtomic_compare_exchange_strong_uint32(XAtomic_uint32_t* var, uint32_t* expected, uint32_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    return __atomic_compare_exchange_n(&(var->value), expected, desired, false, xatomic_to_gcc_memory_order(success_order), xatomic_to_gcc_memory_order(failure_order));
}
bool XAtomic_compare_exchange_strong_int64(XAtomic_int64_t* var, int64_t* expected, int64_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    return __atomic_compare_exchange_n(&(var->value), expected, desired, false, xatomic_to_gcc_memory_order(success_order), xatomic_to_gcc_memory_order(failure_order));
}
bool XAtomic_compare_exchange_strong_uint64(XAtomic_uint64_t* var, uint64_t* expected, uint64_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    return __atomic_compare_exchange_n(&(var->value), expected, desired, false, xatomic_to_gcc_memory_order(success_order), xatomic_to_gcc_memory_order(failure_order));
}
bool XAtomic_compare_exchange_strong_size_t(XAtomic_size_t* var, size_t* expected, size_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    return __atomic_compare_exchange_n(&(var->value), expected, desired, false, xatomic_to_gcc_memory_order(success_order), xatomic_to_gcc_memory_order(failure_order));
}
bool XAtomic_compare_exchange_strong_uintptr_t(XAtomic_uintptr_t* var, uintptr_t* expected, uintptr_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    return __atomic_compare_exchange_n(&(var->value), expected, desired, false, xatomic_to_gcc_memory_order(success_order), xatomic_to_gcc_memory_order(failure_order));
}

// --- Fetch Add/Sub ---
int32_t XAtomic_fetch_add_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_add(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
uint32_t XAtomic_fetch_add_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_add(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
int64_t XAtomic_fetch_add_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_add(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
uint64_t XAtomic_fetch_add_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_add(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
size_t XAtomic_fetch_add_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_add(&(var->value), value, xatomic_to_gcc_memory_order(order));
}

int32_t XAtomic_fetch_sub_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_sub(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
uint32_t XAtomic_fetch_sub_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_sub(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
int64_t XAtomic_fetch_sub_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_sub(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
uint64_t XAtomic_fetch_sub_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_sub(&(var->value), value, xatomic_to_gcc_memory_order(order));
}
size_t XAtomic_fetch_sub_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_sub(&(var->value), value, xatomic_to_gcc_memory_order(order));
}

#else
// ========== ARM Compiler 5 (armcc) ==========
// 使用内联汇编实现核心的 32 位 CAS，其他操作基于它构建，并手动处理内存序

// 核心 32 位比较交换函数
static bool __xatomic_cas_32(volatile uint32_t* addr, uint32_t* expected, uint32_t desired)
{
    uint32_t old_val = *expected;
    uint32_t current_val;
    uint32_t result;

    __asm volatile (
    "1: \n\t"
        "ldrex  %0, [%3] \n\t"     // 加载独占
        "cmp    %0, %4 \n\t"       // 比较当前值与期望值
        "bne    2f \n\t"           // 不相等则跳转到失败
        "strex  %1, %5, [%3] \n\t" // 尝试存储新值
        "cmp    %1, #0 \n\t"       // 检查 strex 是否成功 (0=成功)
        "bne    1b \n\t"           // 失败则重试
        "2: \n\t"
        : "=&r" (current_val), "=&r" (result), "+m" (*addr)
        : "r" (addr), "r" (old_val), "r" (desired)
        : "cc"
        );

    if (current_val == old_val) {
        return true; // 成功
    }
    else {
        *expected = current_val; // 失败，更新 expected
        return false;
    }
}

// ========== Helper Macros for Memory Order ==========
// Apply acquire semantics after a load
static inline void __apply_acquire(XAtomic_MemoryOrder order)
{
    if (order == XAtomic_MemoryOrder_Acquire ||
        order == XAtomic_MemoryOrder_AcqRel ||
        order == XAtomic_MemoryOrder_SeqCst) {
        __dmb();
    }
}

// Apply release semantics before a store
static inline void __apply_release(XAtomic_MemoryOrder order)
{
    if (order == XAtomic_MemoryOrder_Release ||
        order == XAtomic_MemoryOrder_AcqRel ||
        order == XAtomic_MemoryOrder_SeqCst) {
        __dmb();
    }
}

// --- Load (simple load with acquire barrier if needed) ---
bool XAtomic_load_bool(const XAtomic_bool* var, XAtomic_MemoryOrder order)
{
    bool val = var->value;
    __apply_acquire(order);
    return val;
}
int32_t XAtomic_load_int32(const XAtomic_int32_t* var, XAtomic_MemoryOrder order)
{
    int32_t val = var->value;
    __apply_acquire(order);
    return val;
}
uint32_t XAtomic_load_uint32(const XAtomic_uint32_t* var, XAtomic_MemoryOrder order)
{
    uint32_t val = var->value;
    __apply_acquire(order);
    return val;
}
// Note: 64-bit loads are NOT atomic on most Cortex-M cores!
int64_t XAtomic_load_int64(const XAtomic_int64_t* var, XAtomic_MemoryOrder order)
{
    (void)order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
    return 0;
}
uint64_t XAtomic_load_uint64(const XAtomic_uint64_t* var, XAtomic_MemoryOrder order)
{
    (void)order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
    return 0;
}
size_t XAtomic_load_size_t(const XAtomic_size_t* var, XAtomic_MemoryOrder order)
{
    size_t val = var->value;
    __apply_acquire(order);
    return val;
}
uintptr_t XAtomic_load_uintptr_t(const XAtomic_uintptr_t* var, XAtomic_MemoryOrder order)
{
    uintptr_t val = var->value;
    __apply_acquire(order);
    return val;
}

// --- Store (simple store with release barrier if needed) ---
void XAtomic_store_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
{
    __apply_release(order);
    var->value = value;
}
void XAtomic_store_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    __apply_release(order);
    var->value = value;
}
void XAtomic_store_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    __apply_release(order);
    var->value = value;
}
void XAtomic_store_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
}
void XAtomic_store_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
}
void XAtomic_store_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    __apply_release(order);
    var->value = value;
}
void XAtomic_store_uintptr_t(XAtomic_uintptr_t* var, uintptr_t value, XAtomic_MemoryOrder order)
{
    __apply_release(order);
    var->value = value;
}

// --- Exchange (基于 CAS, which is seq_cst) ---
// For RMW operations, the CAS loop provides seq_cst semantics inherently.
bool XAtomic_exchange_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
{
    (void)order;
    bool expected = var->value;
    while (!XAtomic_compare_exchange_strong_bool(var, &expected, value, XAtomic_MemoryOrder_SeqCst, XAtomic_MemoryOrder_SeqCst)) {
        // expected 已被更新为当前值，继续循环
    }
    return expected;
}
int32_t XAtomic_exchange_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    int32_t expected = var->value;
    while (!XAtomic_compare_exchange_strong_int32(var, &expected, value, XAtomic_MemoryOrder_SeqCst, XAtomic_MemoryOrder_SeqCst)) {
        // expected 已被更新为当前值，继续循环
    }
    return expected;
}
uint32_t XAtomic_exchange_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    uint32_t expected = var->value;
    while (!XAtomic_compare_exchange_strong_uint32(var, &expected, value, XAtomic_MemoryOrder_SeqCst, XAtomic_MemoryOrder_SeqCst)) {
        // expected 已被更新为当前值，继续循环
    }
    return expected;
}
// 64位 exchange 在 armcc 下不支持
int64_t XAtomic_exchange_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
    return 0;
}
uint64_t XAtomic_exchange_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
    return 0;
}
size_t XAtomic_exchange_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    size_t expected = var->value;
    while (!XAtomic_compare_exchange_strong_size_t(var, &expected, value, XAtomic_MemoryOrder_SeqCst, XAtomic_MemoryOrder_SeqCst)) {
        // expected 已被更新为当前值，继续循环
    }
    return expected;
}
uintptr_t XAtomic_exchange_uintptr_t(XAtomic_uintptr_t* var, uintptr_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    uintptr_t expected = var->value;
    while (!XAtomic_compare_exchange_strong_uintptr_t(var, &expected, value, XAtomic_MemoryOrder_SeqCst, XAtomic_MemoryOrder_SeqCst)) {
        // expected 已被更新为当前值，继续循环
    }
    return expected;
}

// --- Compare Exchange (32位由汇编实现，64位不支持) ---
bool XAtomic_compare_exchange_strong_bool(XAtomic_bool* var, bool* expected, bool desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    uint32_t exp_u32 = (uint32_t)*expected;
    bool success = __xatomic_cas_32((volatile uint32_t*)&var->value, &exp_u32, (uint32_t)desired);
    *expected = (bool)exp_u32;
    return success;
}
bool XAtomic_compare_exchange_strong_int32(XAtomic_int32_t* var, int32_t* expected, int32_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    uint32_t exp_u32 = (uint32_t)*expected;
    bool success = __xatomic_cas_32((volatile uint32_t*)&var->value, &exp_u32, (uint32_t)desired);
    *expected = (int32_t)exp_u32;
    return success;
}
bool XAtomic_compare_exchange_strong_uint32(XAtomic_uint32_t* var, uint32_t* expected, uint32_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    return __xatomic_cas_32((volatile uint32_t*)&var->value, expected, desired);
}
bool XAtomic_compare_exchange_strong_int64(XAtomic_int64_t* var, int64_t* expected, int64_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
    return false;
}
bool XAtomic_compare_exchange_strong_uint64(XAtomic_uint64_t* var, uint64_t* expected, uint64_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
    return false;
}
bool XAtomic_compare_exchange_strong_size_t(XAtomic_size_t* var, size_t* expected, size_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    return __xatomic_cas_32((volatile uint32_t*)&var->value, expected, desired);
}
bool XAtomic_compare_exchange_strong_uintptr_t(XAtomic_uintptr_t* var, uintptr_t* expected, uintptr_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    return __xatomic_cas_32((volatile uint32_t*)&var->value, (uint32_t*)expected, (uint32_t)desired);
}

// --- Fetch Add/Sub (基于 CAS, which is seq_cst) ---
#define DEFINE_FETCH_OP(type, suffix, op) \
type XAtomic_fetch_##op##_##suffix(XAtomic_##suffix* var, type value, XAtomic_MemoryOrder order) \
{ \
    (void)order; \
    type old_val = var->value; \
    type new_val; \
    do { \
        new_val = old_val op value; \
    } while (!XAtomic_compare_exchange_strong_##suffix(var, &old_val, new_val, XAtomic_MemoryOrder_SeqCst, XAtomic_MemoryOrder_SeqCst)); \
    return old_val; \
}

DEFINE_FETCH_OP(int32_t, int32_t, +)
DEFINE_FETCH_OP(uint32_t, uint32_t, +)
DEFINE_FETCH_OP(int32_t, int32_t, -)
DEFINE_FETCH_OP(uint32_t, uint32_t, -)

// 64位操作不支持
int64_t XAtomic_fetch_add_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
    return 0;
}
uint64_t XAtomic_fetch_add_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
    return 0;
}
int64_t XAtomic_fetch_sub_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
    return 0;
}
uint64_t XAtomic_fetch_sub_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    assert(0 && "64-bit atomics not supported with ARM Compiler 5 on Cortex-M!");
    return 0;
}

size_t XAtomic_fetch_add_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    size_t old_val = var->value;
    size_t new_val;
    do {
        new_val = old_val + value;
    } while (!XAtomic_compare_exchange_strong_size_t(var, &old_val, new_val, XAtomic_MemoryOrder_SeqCst, XAtomic_MemoryOrder_SeqCst));
    return old_val;
}
size_t XAtomic_fetch_sub_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    size_t old_val = var->value;
    size_t new_val;
    do {
        new_val = old_val - value;
    } while (!XAtomic_compare_exchange_strong_size_t(var, &old_val, new_val, XAtomic_MemoryOrder_SeqCst, XAtomic_MemoryOrder_SeqCst));
    return old_val;
}

#endif // ARM Compiler 5 / 6 分支结束

#endif // Keil/ARM Compiler 宏定义