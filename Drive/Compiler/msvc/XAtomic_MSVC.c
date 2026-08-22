#ifdef _MSC_VER 
#include "XAtomic.h"
#include <intrin.h>
// 引入内存屏障宏
#include <windows.h>

void XAtomic_memory_barrier()
{
    // 全内存屏障：防止所有重排序
    _ReadWriteBarrier();
    MemoryBarrier(); // 硬件内存屏障
}

void XAtomic_memory_barrier_acquire()
{
    // 获取屏障：防止后续读写重排到前面
    _ReadWriteBarrier();
    // 在 x86/x64 上，加载操作本身就具有获取语义
    // 但为了确保跨平台一致性，使用编译器屏障
}

void XAtomic_memory_barrier_release()
{
    // 释放屏障：防止前面读写重排到后面
    _ReadWriteBarrier();
    // 在 x86/x64 上，存储操作本身就具有释放语义
    // 但为了确保跨平台一致性，使用编译器屏障
}

// 辅助函数：应用 acquire 内存序
static inline void xatomic_apply_acquire(XAtomic_MemoryOrder order)
{
    switch (order) {
    case XAtomic_MemoryOrder_Acquire:
    case XAtomic_MemoryOrder_AcqRel:
    case XAtomic_MemoryOrder_SeqCst:
        _ReadWriteBarrier(); // 确保读写顺序
        break;
    case XAtomic_MemoryOrder_Consume:
        _ReadWriteBarrier();
        break;
    default:
        break;
    }
}

// 辅助函数：应用 release 内存序  
static inline void xatomic_apply_release(XAtomic_MemoryOrder order)
{
    switch (order) {
    case XAtomic_MemoryOrder_Release:
    case XAtomic_MemoryOrder_AcqRel:
    case XAtomic_MemoryOrder_SeqCst:
        _ReadWriteBarrier();
        break;
    default:
        break;
    }
}

// 失败路径的内存序处理（只处理 load 相关的语义）
static inline void xatomic_apply_failure_acquire(XAtomic_MemoryOrder order)
{
    // 失败时只有 load 操作，所以只关心 acquire/consume/seq_cst
    switch (order) {
    case XAtomic_MemoryOrder_Acquire:
    case XAtomic_MemoryOrder_AcqRel:
    case XAtomic_MemoryOrder_SeqCst:
    case XAtomic_MemoryOrder_Consume:
        _ReadWriteBarrier();
        break;
    default: // Relaxed, Release - 对 load 没有额外要求
        break;
    }
}

// ==============================================================================
// bool 原子操作 (使用32位存储以确保原子性)
// ==============================================================================

bool XAtomic_load_bool(const XAtomic_bool* var, XAtomic_MemoryOrder order)
{
    int32_t result;
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
        result = var->value;
        break;
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
    case XAtomic_MemoryOrder_SeqCst:
        // 使用原子加载确保获取语义
        result = _InterlockedCompareExchange((volatile long*)&var->value, 0, 0);
        break;
    default:
        result = var->value;
        break;
    }
    xatomic_apply_acquire(order);
    return (bool)result;
}

void XAtomic_store_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order); // 统一处理 release 语义

    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
    case XAtomic_MemoryOrder_Consume: // Consume 对 store 无效
    case XAtomic_MemoryOrder_Acquire: // Acquire 对 store 无效
        var->value = (int32_t)value;
        break;
    case XAtomic_MemoryOrder_Release:
    case XAtomic_MemoryOrder_AcqRel:
    case XAtomic_MemoryOrder_SeqCst:
        _InterlockedExchange((volatile long*)&var->value, (long)value);
        break;
    default:
        var->value = (int32_t)value;
        break;
    }
}

bool XAtomic_exchange_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    int32_t result = _InterlockedExchange((volatile long*)&var->value, (long)value);
    xatomic_apply_acquire(order);
    return (bool)result;
}

bool XAtomic_compare_exchange_strong_bool(XAtomic_bool* var, bool* expected, bool desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    int32_t old_val = (int32_t)*expected;
    int32_t result = _InterlockedCompareExchange(
        (volatile long*)&var->value,
        (long)desired,
        (long)old_val
    );

    if (result == old_val) {
        // 成功：应用 success_order 的完整语义
        xatomic_apply_acquire(success_order);
        return true;
    }
    else {
        // 失败：只应用 failure_order 中的 acquire 部分
        xatomic_apply_failure_acquire(failure_order);
        *expected = (bool)result;
        return false;
    }
}

// ==============================================================================
// 64位原子操作 (仅在64位平台可用)
// ==============================================================================

#if defined(_M_X64)

int64_t XAtomic_load_int64(const XAtomic_int64_t* var, XAtomic_MemoryOrder order)
{
    int64_t result;
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
        result = var->value;
        break;
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
    case XAtomic_MemoryOrder_SeqCst:
        result = _InterlockedCompareExchange64((volatile __int64*)&var->value, 0, 0);
        break;
    default:
        result = var->value;
        break;
    }
    xatomic_apply_acquire(order);
    return result;
}

void XAtomic_store_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
        var->value = value;
        break;
    case XAtomic_MemoryOrder_Release:
    case XAtomic_MemoryOrder_AcqRel:
    case XAtomic_MemoryOrder_SeqCst:
        _InterlockedExchange64((volatile __int64*)&var->value, value);
        break;
    default:
        var->value = value;
        break;
    }
}

int64_t XAtomic_exchange_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    int64_t result = _InterlockedExchange64((volatile __int64*)&var->value, value);
    xatomic_apply_acquire(order);
    return result;
}

bool XAtomic_compare_exchange_strong_int64(XAtomic_int64_t* var, int64_t* expected, int64_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    int64_t old_val = *expected;
    int64_t result = _InterlockedCompareExchange64(
        (volatile __int64*)&var->value,
        desired,
        old_val
    );

    if (result == old_val) {
        xatomic_apply_acquire(success_order);
        return true;
    }
    else {
        xatomic_apply_failure_acquire(failure_order);
        *expected = result;
        return false;
    }
}

int64_t XAtomic_fetch_add_int64(XAtomic_int64_t* var, int64_t arg, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    int64_t result = _InterlockedExchangeAdd64((volatile __int64*)&var->value, arg);
    xatomic_apply_acquire(order);
    return result;
}

int64_t XAtomic_fetch_sub_int64(XAtomic_int64_t* var, int64_t arg, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    int64_t result = _InterlockedExchangeAdd64((volatile __int64*)&var->value, -arg);
    xatomic_apply_acquire(order);
    return result;
}

#endif // _M_X64

// ==============================================================================
// 32位原子操作 (32位和64位平台都支持)
// ==============================================================================

int32_t XAtomic_load_int32(const XAtomic_int32_t* var, XAtomic_MemoryOrder order)
{
    int32_t result;
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
        result = var->value;
        break;
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
    case XAtomic_MemoryOrder_SeqCst:
        result = (int32_t)_InterlockedCompareExchange((volatile long*)&var->value, 0, 0);
        break;
    default:
        result = var->value;
        break;
    }
    xatomic_apply_acquire(order);
    return result;
}

void XAtomic_store_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
        var->value = value;
        break;
    case XAtomic_MemoryOrder_Release:
    case XAtomic_MemoryOrder_AcqRel:
    case XAtomic_MemoryOrder_SeqCst:
        _InterlockedExchange((volatile long*)&var->value, (long)value);
        break;
    default:
        var->value = value;
        break;
    }
}

int32_t XAtomic_exchange_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    int32_t result = (int32_t)_InterlockedExchange((volatile long*)&var->value, (long)value);
    xatomic_apply_acquire(order);
    return result;
}

bool XAtomic_compare_exchange_strong_int32(XAtomic_int32_t* var, int32_t* expected, int32_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    int32_t old_val = *expected;
    int32_t result = (int32_t)_InterlockedCompareExchange(
        (volatile long*)&var->value,
        (long)desired,
        (long)old_val
    );

    if (result == old_val) {
        xatomic_apply_acquire(success_order);
        return true;
    }
    else {
        xatomic_apply_failure_acquire(failure_order);
        *expected = result;
        return false;
    }
}

int32_t XAtomic_fetch_add_int32(XAtomic_int32_t* var, int32_t arg, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    int32_t result = (int32_t)_InterlockedExchangeAdd((volatile long*)&var->value, (long)arg);
    xatomic_apply_acquire(order);
    return result;
}

int32_t XAtomic_fetch_sub_int32(XAtomic_int32_t* var, int32_t arg, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    int32_t result = (int32_t)_InterlockedExchangeAdd((volatile long*)&var->value, (long)(-arg));
    xatomic_apply_acquire(order);
    return result;
}

// ==============================================================================
// uint64_t 原子操作
// ==============================================================================

#if defined(_M_X64)

uint64_t XAtomic_load_uint64(const XAtomic_uint64_t* var, XAtomic_MemoryOrder order)
{
    uint64_t result;
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
        result = var->value;
        break;
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
    case XAtomic_MemoryOrder_SeqCst:
        result = (uint64_t)_InterlockedCompareExchange64((volatile __int64*)&var->value, 0, 0);
        break;
    default:
        result = var->value;
        break;
    }
    xatomic_apply_acquire(order);
    return result;
}

void XAtomic_store_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
        var->value = value;
        break;
    case XAtomic_MemoryOrder_Release:
    case XAtomic_MemoryOrder_AcqRel:
    case XAtomic_MemoryOrder_SeqCst:
        _InterlockedExchange64((volatile __int64*)&var->value, (__int64)value);
        break;
    default:
        var->value = value;
        break;
    }
}

uint64_t XAtomic_exchange_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    uint64_t result = (uint64_t)_InterlockedExchange64((volatile __int64*)&var->value, (__int64)value);
    xatomic_apply_acquire(order);
    return result;
}

bool XAtomic_compare_exchange_strong_uint64(XAtomic_uint64_t* var, uint64_t* expected, uint64_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    uint64_t old_val = *expected;
    uint64_t result = (uint64_t)_InterlockedCompareExchange64(
        (volatile __int64*)&var->value,
        (__int64)desired,
        (__int64)old_val
    );

    if (result == old_val) {
        xatomic_apply_acquire(success_order);
        return true;
    }
    else {
        xatomic_apply_failure_acquire(failure_order);
        *expected = result;
        return false;
    }
}

uint64_t XAtomic_fetch_add_uint64(XAtomic_uint64_t* var, uint64_t arg, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    uint64_t result = (uint64_t)_InterlockedExchangeAdd64((volatile __int64*)&var->value, (__int64)arg);
    xatomic_apply_acquire(order);
    return result;
}

uint64_t XAtomic_fetch_sub_uint64(XAtomic_uint64_t* var, uint64_t arg, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    uint64_t result = (uint64_t)_InterlockedExchangeAdd64((volatile __int64*)&var->value, -((__int64)arg));
    xatomic_apply_acquire(order);
    return result;
}

#endif // _M_X64

// ==============================================================================
// uint32_t 原子操作
// ==============================================================================

uint32_t XAtomic_load_uint32(const XAtomic_uint32_t* var, XAtomic_MemoryOrder order)
{
    uint32_t result;
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
        result = var->value;
        break;
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
    case XAtomic_MemoryOrder_SeqCst:
        result = (uint32_t)_InterlockedCompareExchange((volatile long*)&var->value, 0, 0);
        break;
    default:
        result = var->value;
        break;
    }
    xatomic_apply_acquire(order);
    return result;
}

void XAtomic_store_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
        var->value = value;
        break;
    case XAtomic_MemoryOrder_Release:
    case XAtomic_MemoryOrder_AcqRel:
    case XAtomic_MemoryOrder_SeqCst:
        _InterlockedExchange((volatile long*)&var->value, (long)value);
        break;
    default:
        var->value = value;
        break;
    }
}

uint32_t XAtomic_exchange_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    uint32_t result = (uint32_t)_InterlockedExchange((volatile long*)&var->value, (long)value);
    xatomic_apply_acquire(order);
    return result;
}

bool XAtomic_compare_exchange_strong_uint32(XAtomic_uint32_t* var, uint32_t* expected, uint32_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    uint32_t old_val = *expected;
    uint32_t result = (uint32_t)_InterlockedCompareExchange(
        (volatile long*)&var->value,
        (long)desired,
        (long)old_val
    );

    if (result == old_val) {
        xatomic_apply_acquire(success_order);
        return true;
    }
    else {
        xatomic_apply_failure_acquire(failure_order);
        *expected = result;
        return false;
    }
}

uint32_t XAtomic_fetch_add_uint32(XAtomic_uint32_t* var, uint32_t arg, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    uint32_t result = (uint32_t)_InterlockedExchangeAdd((volatile long*)&var->value, (long)arg);
    xatomic_apply_acquire(order);
    return result;
}

uint32_t XAtomic_fetch_sub_uint32(XAtomic_uint32_t* var, uint32_t arg, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    uint32_t result = (uint32_t)_InterlockedExchangeAdd((volatile long*)&var->value, (long)(-arg));
    xatomic_apply_acquire(order);
    return result;
}

// ==============================================================================
// size_t 原子操作 (平台自适应)
// ==============================================================================

size_t XAtomic_load_size_t(const XAtomic_size_t* var, XAtomic_MemoryOrder order)
{
    size_t result;
#if defined(_M_X64)
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
        result = var->value;
        break;
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
    case XAtomic_MemoryOrder_SeqCst:
        result = (size_t)_InterlockedCompareExchange64((volatile __int64*)&var->value, 0, 0);
        break;
    default:
        result = var->value;
        break;
    }
#elif defined(_M_IX86)
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
        result = var->value;
        break;
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
    case XAtomic_MemoryOrder_SeqCst:
        result = (size_t)_InterlockedCompareExchange((volatile long*)&var->value, 0, 0);
        break;
    default:
        result = var->value;
        break;
    }
#else
    result = var->value;
#endif
    xatomic_apply_acquire(order);
    return result;
}

void XAtomic_store_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
#if defined(_M_X64)
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
        var->value = value;
        break;
    case XAtomic_MemoryOrder_Release:
    case XAtomic_MemoryOrder_AcqRel:
    case XAtomic_MemoryOrder_SeqCst:
        _InterlockedExchange64((volatile __int64*)&var->value, (__int64)value);
        break;
    default:
        var->value = value;
        break;
    }
#elif defined(_M_IX86)
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed:
    case XAtomic_MemoryOrder_Consume:
    case XAtomic_MemoryOrder_Acquire:
        var->value = value;
        break;
    case XAtomic_MemoryOrder_Release:
    case XAtomic_MemoryOrder_AcqRel:
    case XAtomic_MemoryOrder_SeqCst:
        _InterlockedExchange((volatile long*)&var->value, (long)value);
        break;
    default:
        var->value = value;
        break;
    }
#else
    var->value = value;
#endif
}

size_t XAtomic_exchange_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    size_t result;
#if defined(_M_X64)
    result = (size_t)_InterlockedExchange64((volatile __int64*)&var->value, (__int64)value);
#elif defined(_M_IX86)
    result = (size_t)_InterlockedExchange((volatile long*)&var->value, (long)value);
#else
    result = var->value;
    var->value = value;
#endif
    xatomic_apply_acquire(order);
    return result;
}

bool XAtomic_compare_exchange_strong_size_t(XAtomic_size_t* var, size_t* expected, size_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    size_t old_val = *expected;
    size_t result;

#if defined(_M_X64)
    result = (size_t)_InterlockedCompareExchange64(
        (volatile __int64*)&var->value,
        (__int64)desired,
        (__int64)old_val
    );
#elif defined(_M_IX86)
    result = (size_t)_InterlockedCompareExchange(
        (volatile long*)&var->value,
        (long)desired,
        (long)old_val
    );
#else
    // 其他平台的回退实现（非原子）
    result = var->value;
    if (result == old_val) {
        var->value = desired;
    }
#endif

    if (result == old_val) {
        xatomic_apply_acquire(success_order);
        return true;
    }
    else {
        xatomic_apply_failure_acquire(failure_order);
        *expected = result;
        return false;
    }
}

size_t XAtomic_fetch_add_size_t(XAtomic_size_t* var, size_t arg, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    size_t result;
#if defined(_M_X64)
    result = (size_t)_InterlockedExchangeAdd64((volatile __int64*)&var->value, (__int64)arg);
#elif defined(_M_IX86)
    result = (size_t)_InterlockedExchangeAdd((volatile long*)&var->value, (long)arg);
#else
    result = var->value;
    var->value += arg;
#endif
    xatomic_apply_acquire(order);
    return result;
}

size_t XAtomic_fetch_sub_size_t(XAtomic_size_t* var, size_t arg, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    size_t result;
#if defined(_M_X64)
    result = (size_t)_InterlockedExchangeAdd64((volatile __int64*)&var->value, -((__int64)arg));
#elif defined(_M_IX86)
    result = (size_t)_InterlockedExchangeAdd((volatile long*)&var->value, (long)(-arg));
#else
    result = var->value;
    var->value -= arg;
#endif
    xatomic_apply_acquire(order);
    return result;
}

// ==============================================================================
// uintptr_t 原子操作 (平台自适应)
// ==============================================================================

uintptr_t XAtomic_load_uintptr_t(const XAtomic_uintptr_t* var, XAtomic_MemoryOrder order)
{
    return (uintptr_t)XAtomic_load_size_t((const XAtomic_size_t*)var, order);
}

void XAtomic_store_uintptr_t(XAtomic_uintptr_t* var, uintptr_t value, XAtomic_MemoryOrder order)
{
    XAtomic_store_size_t((XAtomic_size_t*)var, (size_t)value, order);
}

uintptr_t XAtomic_exchange_uintptr_t(XAtomic_uintptr_t* var, uintptr_t value, XAtomic_MemoryOrder order)
{
    return (uintptr_t)XAtomic_exchange_size_t((XAtomic_size_t*)var, (size_t)value, order);
}

bool XAtomic_compare_exchange_strong_uintptr_t(XAtomic_uintptr_t* var, uintptr_t* expected, uintptr_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    size_t exp = (size_t)*expected;
    bool result = XAtomic_compare_exchange_strong_size_t((XAtomic_size_t*)var, &exp, (size_t)desired, success_order, failure_order);
    *expected = (uintptr_t)exp;
    return result;
}

uintptr_t XAtomic_fetch_add_uintptr_t(XAtomic_uintptr_t* var, uintptr_t arg, XAtomic_MemoryOrder order)
{
    return (uintptr_t)XAtomic_fetch_add_size_t((XAtomic_size_t*)var, (size_t)arg, order);
}

uintptr_t XAtomic_fetch_sub_uintptr_t(XAtomic_uintptr_t* var, uintptr_t arg, XAtomic_MemoryOrder order)
{
    return (uintptr_t)XAtomic_fetch_sub_size_t((XAtomic_size_t*)var, (size_t)arg, order);
}

#endif // _MSC_VER