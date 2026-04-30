#ifdef _MSC_VER 
#include "XAtomic.h"
#include <intrin.h>
// 引入内存屏障宏
#include <windows.h>

// ========== 辅助函数：应用内存序 ==========
// 对于 Load 操作，应用 Acquire 语义
static inline void xatomic_apply_acquire(XAtomic_MemoryOrder order)
{
    // 在 x86/x64 上，CPU 层面不需要额外指令，但需要防止编译器重排
    if (order == XAtomic_MemoryOrder_Acquire ||
        order == XAtomic_MemoryOrder_AcqRel ||
        order == XAtomic_MemoryOrder_SeqCst) {
        _ReadBarrier(); // 编译器屏障：后续读写不能重排到此之前
    }
    // Note: memory_order_consume 在实践中通常被当作 acquire 处理
}

// 对于 Store 操作，应用 Release 语义
static inline void xatomic_apply_release(XAtomic_MemoryOrder order)
{
    // 在 x86/x64 上，CPU 层面不需要额外指令，但需要防止编译器重排
    if (order == XAtomic_MemoryOrder_Release ||
        order == XAtomic_MemoryOrder_AcqRel ||
        order == XAtomic_MemoryOrder_SeqCst) {
        _WriteBarrier(); // 编译器屏障：前面的读写不能重排到此之后
    }
}

// ========== 64位操作 (仅在 x64 下可用) ==========
#if _M_X64

// --- Store ---
void XAtomic_store_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    // 对于非 SeqCst 的 Store，理论上可以用普通 mov + 编译器屏障
    // 但在实践中，为了简单和保证原子性（避免撕裂写），仍使用 InterlockedExchange
    if (order == XAtomic_MemoryOrder_Relaxed ||
        order == XAtomic_MemoryOrder_Release) {
        // 注意：直接赋值 `var->value = value;` 在 64 位对齐时是原子的，
        // 但为了绝对安全和代码清晰，这里依然使用 Interlocked。
        // MSVC 的 volatile 64-bit store on x64 is atomic, but let's be explicit.
        _InterlockedExchange64((volatile __int64*)&var->value, value);
    }
    else {
        // Acquire, AcqRel, SeqCst 都需要最强的保证
        _InterlockedExchange64((volatile __int64*)&var->value, value);
    }
}

void XAtomic_store_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    XAtomic_store_int64((XAtomic_int64_t*)var, (int64_t)value, order);
}

// --- Load ---
int64_t XAtomic_load_int64(const XAtomic_int64_t* var, XAtomic_MemoryOrder order)
{
    xatomic_apply_acquire(order);
    // 对于非 SeqCst/Acquire 的 Load，理论上可以用普通 mov + 编译器屏障
    // MSVC 保证对 volatile 64-bit 变量的读取是原子的（在 x64 上）
    if (order == XAtomic_MemoryOrder_Relaxed ||
        order == XAtomic_MemoryOrder_Consume) {
        return var->value;
    }
    else {
        // 使用 InterlockedCompareExchange 来确保获取语义（虽然硬件层面可能不必要）
        return _InterlockedCompareExchange64((volatile __int64*)&var->value, 0, 0);
    }
}

uint64_t XAtomic_load_uint64(const XAtomic_uint64_t* var, XAtomic_MemoryOrder order)
{
    return (uint64_t)XAtomic_load_int64((const XAtomic_int64_t*)var, order);
}

// --- RMW 操作 (Exchange, FetchAdd, CAS) ---
// 这些操作在 x86/x64 上天然就是 seq_cst，无法实现更弱的内存序
int64_t XAtomic_exchange_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    (void)order; // Ignored, as hardware provides seq_cst
    return _InterlockedExchange64((volatile __int64*)&var->value, value);
}

uint64_t XAtomic_exchange_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return (uint64_t)_InterlockedExchange64((volatile __int64*)&var->value, (long long)value);
}

int64_t XAtomic_fetch_add_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return _InterlockedExchangeAdd64((volatile long long*)&var->value, value);
}

uint64_t XAtomic_fetch_add_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return (uint64_t)_InterlockedExchangeAdd64((volatile long long*)&var->value, (long long)value);
}

int64_t XAtomic_fetch_sub_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return _InterlockedExchangeAdd64((volatile long long*)&var->value, -value);
}

uint64_t XAtomic_fetch_sub_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return (uint64_t)_InterlockedExchangeAdd64((volatile long long*)&var->value, -(long long)value);
}

bool XAtomic_compare_exchange_strong_int64(XAtomic_int64_t* var, int64_t* expected, int64_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    __int64 old_val = *expected;
    __int64 result = _InterlockedCompareExchange64(
        (volatile __int64*)&var->value,
        desired,
        old_val
    );
    *expected = result;
    return result == old_val;
}

bool XAtomic_compare_exchange_strong_uint64(XAtomic_uint64_t* var, uint64_t* expected, uint64_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    return XAtomic_compare_exchange_strong_int64((XAtomic_int64_t*)var, (int64_t*)expected, (int64_t)desired, success_order, failure_order);
}

#endif // _M_X64

// ========== 32位及通用操作 ==========
// --- Size_t 操作 (根据平台选择 32/64 位实现) ---
size_t XAtomic_fetch_add_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
#if _M_X64
    return XAtomic_fetch_add_uint64((XAtomic_uint64_t*)var, value, order);
#else
    return XAtomic_fetch_add_uint32((XAtomic_uint32_t*)var, value, order);
#endif
}

size_t XAtomic_fetch_sub_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
#if _M_X64
    return XAtomic_fetch_sub_uint64((XAtomic_uint64_t*)var, value, order);
#else
    return XAtomic_fetch_sub_uint32((XAtomic_uint32_t*)var, value, order);
#endif
}

// --- 内存屏障 API ---
void XAtomic_memory_barrier()
{
    MemoryBarrier(); // Full hardware + compiler barrier
}

void XAtomic_memory_barrier_acquire()
{
    _ReadBarrier(); // Compiler barrier for acquire
}

void XAtomic_memory_barrier_release()
{
    _WriteBarrier(); // Compiler barrier for release
}

// --- Bool / Int32 / Uint32 / Ptr 操作 ---
// Load
bool XAtomic_load_bool(const XAtomic_bool* var, XAtomic_MemoryOrder order)
{
    xatomic_apply_acquire(order);
    if (order == XAtomic_MemoryOrder_Relaxed ||
        order == XAtomic_MemoryOrder_Consume) {
        return (bool)var->value;
    }
    else {
        return (bool)_InterlockedCompareExchange((volatile long*)&var->value, 0, 0);
    }
}

int32_t XAtomic_load_int32(const XAtomic_int32_t* var, XAtomic_MemoryOrder order)
{
    xatomic_apply_acquire(order);
    if (order == XAtomic_MemoryOrder_Relaxed ||
        order == XAtomic_MemoryOrder_Consume) {
        return var->value;
    }
    else {
        return (int32_t)_InterlockedCompareExchange((volatile long*)&var->value, 0, 0);
    }
}

uint32_t XAtomic_load_uint32(const XAtomic_uint32_t* var, XAtomic_MemoryOrder order)
{
    xatomic_apply_acquire(order);
    if (order == XAtomic_MemoryOrder_Relaxed ||
        order == XAtomic_MemoryOrder_Consume) {
        return var->value;
    }
    else {
        return (uint32_t)_InterlockedCompareExchange((volatile long*)&var->value, 0, 0);
    }
}

size_t XAtomic_load_size_t(const XAtomic_size_t* var, XAtomic_MemoryOrder order)
{
#if _M_X64
    return XAtomic_load_uint64((const XAtomic_uint64_t*)var, order);
#else
    return XAtomic_load_uint32((const XAtomic_uint32_t*)var, order);
#endif
}

void* XAtomic_load_ptr(const XAtomic_ptr* var, XAtomic_MemoryOrder order)
{
    xatomic_apply_acquire(order);
    if (order == XAtomic_MemoryOrder_Relaxed ||
        order == XAtomic_MemoryOrder_Consume) {
        return var->value;
    }
    else {
        return (void*)_InterlockedCompareExchangePointer((volatile void**)&var->value, NULL, NULL);
    }
}

// Store
void XAtomic_store_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    if (order == XAtomic_MemoryOrder_Relaxed ||
        order == XAtomic_MemoryOrder_Release) {
        var->value = (long)value;
    }
    else {
        _InterlockedExchange((volatile long*)&var->value, (long)value);
    }
}

void XAtomic_store_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    if (order == XAtomic_MemoryOrder_Relaxed ||
        order == XAtomic_MemoryOrder_Release) {
        var->value = value;
    }
    else {
        _InterlockedExchange((volatile long*)&var->value, (long)value);
    }
}

void XAtomic_store_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    if (order == XAtomic_MemoryOrder_Relaxed ||
        order == XAtomic_MemoryOrder_Release) {
        var->value = value;
    }
    else {
        _InterlockedExchange((volatile long*)&var->value, (long)value);
    }
}

void XAtomic_store_ptr(XAtomic_ptr* var, void* value, XAtomic_MemoryOrder order)
{
    xatomic_apply_release(order);
    if (order == XAtomic_MemoryOrder_Relaxed ||
        order == XAtomic_MemoryOrder_Release) {
        var->value = value;
    }
    else {
        _InterlockedExchangePointer((volatile void**)&var->value, value);
    }
}

void XAtomic_store_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
#if _M_X64
    XAtomic_store_uint64((XAtomic_uint64_t*)var, value, order);
#else
    XAtomic_store_uint32((XAtomic_uint32_t*)var, value, order);
#endif
}

// Exchange (RMW - order ignored)
bool XAtomic_exchange_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
{
    (void)order;
    return (bool)_InterlockedExchange((volatile long*)&var->value, (long)value);
}

int32_t XAtomic_exchange_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return (int32_t)_InterlockedExchange((volatile long*)&var->value, (long)value);
}

uint32_t XAtomic_exchange_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return (uint32_t)_InterlockedExchange((volatile long*)&var->value, (long)value);
}

size_t XAtomic_exchange_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
#if _M_X64
    return XAtomic_exchange_uint64((XAtomic_uint64_t*)var, value, order);
#else
    return XAtomic_exchange_uint32((XAtomic_uint32_t*)var, value, order);
#endif
}

void* XAtomic_exchange_ptr(XAtomic_ptr* var, void* value, XAtomic_MemoryOrder order)
{
    (void)order;
    return _InterlockedExchangePointer((volatile void**)&var->value, value);
}

// Compare Exchange (RMW - order ignored)
bool XAtomic_compare_exchange_strong_bool(XAtomic_bool* var, bool* expected, bool desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    long old_val = (long)*expected;
    long result = _InterlockedCompareExchange(
        (volatile long*)&var->value,
        (long)desired,
        old_val
    );
    *expected = (bool)result;
    return result == old_val;
}

bool XAtomic_compare_exchange_strong_int32(XAtomic_int32_t* var, int32_t* expected, int32_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    long old_val = (long)*expected;
    long result = _InterlockedCompareExchange(
        (volatile long*)&var->value,
        (long)desired,
        old_val
    );
    *expected = (int32_t)result;
    return result == old_val;
}

bool XAtomic_compare_exchange_strong_uint32(XAtomic_uint32_t* var, uint32_t* expected, uint32_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    long old_val = (long)*expected;
    long result = _InterlockedCompareExchange(
        (volatile long*)&var->value,
        (long)desired,
        old_val
    );
    *expected = (uint32_t)result;
    return result == old_val;
}

bool XAtomic_compare_exchange_strong_size_t(XAtomic_size_t* var, size_t* expected, size_t desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
#if _M_X64
    return XAtomic_compare_exchange_strong_uint64((XAtomic_uint64_t*)var, expected, desired, success_order, failure_order);
#else
    return XAtomic_compare_exchange_strong_uint32((XAtomic_uint32_t*)var, expected, desired, success_order, failure_order);
#endif
}

bool XAtomic_compare_exchange_strong_ptr(XAtomic_ptr* var, void** expected, void* desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order)
{
    (void)success_order;
    (void)failure_order;
    void* actual_value = _InterlockedCompareExchangePointer(
        (volatile void**)&var->value,
        desired,
        *expected
    );
    bool success = (actual_value == *expected);
    *expected = actual_value;
    return success;
}

// Fetch Add/Sub (RMW - order ignored)
int32_t XAtomic_fetch_add_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return (int32_t)_InterlockedExchangeAdd((volatile long*)&var->value, (long)value);
}

uint32_t XAtomic_fetch_add_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return (uint32_t)_InterlockedExchangeAdd((volatile long*)&var->value, (long)value);
}

int32_t XAtomic_fetch_sub_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return (int32_t)_InterlockedExchangeAdd((volatile long*)&var->value, -(long)value);
}

uint32_t XAtomic_fetch_sub_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    (void)order;
    return (uint32_t)_InterlockedExchangeAdd((volatile long*)&var->value, -(long)value);
}

#endif // _MSC_VER