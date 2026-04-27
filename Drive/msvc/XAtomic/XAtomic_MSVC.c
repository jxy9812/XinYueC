#ifdef _MSC_VER 
#include "XAtomic.h"
#include <intrin.h>
// 引入内存屏障宏（替代可能弃用的 _ReadWriteBarrier 等）
#include <windows.h>

#if _M_X64
void XAtomic_store_int64(XAtomic_int64_t* var, int64_t value)
{
    _InterlockedExchange64((volatile __int64*)&var->value, value);
}
void XAtomic_store_uint64(XAtomic_uint64_t* var, uint64_t value)
{
    // 无符号64位存储，转换为有符号指针不影响值（二进制兼容）
    _InterlockedExchange64((volatile __int64*)&var->value, (long long)value);
}
int64_t XAtomic_exchange_int64(XAtomic_int64_t* var, int64_t value)
{
    return _InterlockedExchange64((volatile __int64*)&var->value, value);
}
uint64_t XAtomic_exchange_uint64(XAtomic_uint64_t* var, uint64_t value)
{
    return (uint64_t)_InterlockedExchange64((volatile __int64*)&var->value, (long long)value);
}
int64_t XAtomic_fetch_add_int64(XAtomic_int64_t* var, int64_t value)
{
    return _InterlockedExchangeAdd64((volatile long long*)&var->value, value);
}
uint64_t XAtomic_fetch_add_uint64(XAtomic_uint64_t* var, uint64_t value)
{
    return (uint64_t)_InterlockedExchangeAdd64((volatile long long*)&var->value, (long long)value);
}
int64_t XAtomic_fetch_sub_int64(XAtomic_int64_t* var, int64_t value)
{
    return _InterlockedExchangeAdd64((volatile long long*)&var->value, -value);
}
uint64_t XAtomic_fetch_sub_uint64(XAtomic_uint64_t* var, uint64_t value)
{
    return (uint64_t)_InterlockedExchangeAdd64((volatile long long*)&var->value, -(long long)value);
}

#endif // _M_X64

size_t XAtomic_fetch_add_size_t(XAtomic_size_t* var, size_t value)
{
#if _M_X64
    return XAtomic_fetch_add_uint64((XAtomic_uint64_t*)var, value);
#else
    return XAtomic_fetch_add_uint32((XAtomic_uint32_t*)var, value);
#endif
}
size_t XAtomic_fetch_sub_size_t(XAtomic_size_t* var, size_t value)
{
#if _M_X64
    return XAtomic_fetch_sub_uint64((XAtomic_uint64_t*)var, value);
#else
    return XAtomic_fetch_sub_uint32((XAtomic_uint32_t*)var, value);
#endif
}

// 修复：使用更推荐的 MemoryBarrier 宏（替代可能弃用的 _ReadWriteBarrier）
void XAtomic_memory_barrier()
{
    MemoryBarrier();
}
void XAtomic_memory_barrier_acquire()
{
    _ReadBarrier(); // 对于 acquire 语义，保留读屏障
}
void XAtomic_memory_barrier_release()
{
    _WriteBarrier(); // 对于 release 语义，保留写屏障
}

// 修复：XAtomic_bool 的 value 是 long 类型，操作时使用 long 类型的 Interlocked 函数
bool XAtomic_load_bool(const XAtomic_bool* var)
{
    // _InterlockedCompareExchange 比较并返回旧值（第三个参数为比较值，第四个为新值，此处仅读取）
    return (bool)_InterlockedCompareExchange((volatile long*)&var->value, 0, 0);
}
int32_t XAtomic_load_int32(const XAtomic_int32_t* var)
{
    return (int32_t)_InterlockedCompareExchange((volatile long*)&var->value, 0, 0);
}
uint32_t XAtomic_load_uint32(const XAtomic_uint32_t* var)
{
    return (uint32_t)_InterlockedCompareExchange((volatile long*)&var->value, 0, 0);
}
int64_t XAtomic_load_int64(const XAtomic_int64_t* var)
{
    return _InterlockedCompareExchange64((volatile __int64*)&var->value, 0, 0);
}
uint64_t XAtomic_load_uint64(const XAtomic_uint64_t* var)
{
    return (uint64_t)_InterlockedCompareExchange64((volatile __int64*)&var->value, 0, 0);
}

// 修复：size_t 是整数类型，应复用整数加载函数而非指针函数
size_t XAtomic_load_size_t(const XAtomic_size_t* var)
{
#if _M_X64
    return XAtomic_load_uint64((const XAtomic_uint64_t*)var);
#else
    return XAtomic_load_uint32((const XAtomic_uint32_t*)var);
#endif
}
void* XAtomic_load_ptr(const XAtomic_ptr* var)
{
    return (void*)_InterlockedCompareExchangePointer((volatile void**)&var->value, NULL, NULL);
}

// 修复：XAtomic_bool 的存储需匹配 long 类型
void XAtomic_store_bool(XAtomic_bool* var, bool value)
{
    _InterlockedExchange((volatile long*)&var->value, (long)value);
}
void XAtomic_store_int32(XAtomic_int32_t* var, int32_t value)
{
    _InterlockedExchange((volatile long*)&var->value, (long)value);
}
void XAtomic_store_uint32(XAtomic_uint32_t* var, uint32_t value)
{
    _InterlockedExchange((volatile long*)&var->value, (long)value);
}
void XAtomic_store_ptr(XAtomic_ptr* var, void* value)
{
    _InterlockedExchangePointer((volatile void**)&var->value, value);
}

// 修复：size_t 存储应复用整数存储函数而非指针函数
void XAtomic_store_size_t(XAtomic_size_t* var, size_t value)
{
#if _M_X64
    XAtomic_store_uint64((XAtomic_uint64_t*)var, value);
#else
    XAtomic_store_uint32((XAtomic_uint32_t*)var, value);
#endif
}

bool XAtomic_exchange_bool(XAtomic_bool* var, bool value)
{
    return (bool)_InterlockedExchange((volatile long*)&var->value, (long)value);
}
int32_t XAtomic_exchange_int32(XAtomic_int32_t* var, int32_t value)
{
    return (int32_t)_InterlockedExchange((volatile long*)&var->value, (long)value);
}
uint32_t XAtomic_exchange_uint32(XAtomic_uint32_t* var, uint32_t value)
{
    return (uint32_t)_InterlockedExchange((volatile long*)&var->value, (long)value);
}

// 修复：size_t 交换应复用整数交换函数而非指针函数
size_t XAtomic_exchange_size_t(XAtomic_size_t* var, size_t value)
{
#if _M_X64
    return XAtomic_exchange_uint64((XAtomic_uint64_t*)var, value);
#else
    return XAtomic_exchange_uint32((XAtomic_uint32_t*)var, value);
#endif
}
void* XAtomic_exchange_ptr(XAtomic_ptr* var, void* value)
{
    return _InterlockedExchangePointer((volatile void**)&var->value, value);
}

// 修复：XAtomic_bool 的 CAS 操作需匹配 long 类型
bool XAtomic_compare_exchange_strong_bool(XAtomic_bool* var, bool* expected, bool desired)
{
    long old_val = (long)*expected;
    long result = _InterlockedCompareExchange(
        (volatile long*)&var->value,
        (long)desired,
        old_val
    );
    *expected = (bool)result; // 若失败，更新 expected 为当前值
    return result == old_val;
}
bool XAtomic_compare_exchange_strong_int32(XAtomic_int32_t* var, int32_t* expected, int32_t desired)
{
    long old_val = (long)*expected;
    long result = _InterlockedCompareExchange(
        (volatile long*)&var->value,
        (long)desired,
        old_val
    );
    *expected = (int32_t)result;
    return result == old_val;
}
bool XAtomic_compare_exchange_strong_uint32(XAtomic_uint32_t* var, uint32_t* expected, uint32_t desired)
{
    long old_val = (long)*expected;
    long result = _InterlockedCompareExchange(
        (volatile long*)&var->value,
        (long)desired,
        old_val
    );
    *expected = (uint32_t)result;
    return result == old_val;
}
bool XAtomic_compare_exchange_strong_int64(XAtomic_int64_t* var, int64_t* expected, int64_t desired)
{
    __int64 old_val = *expected;
    __int64 result = _InterlockedCompareExchange64(
        (volatile __int64*)&var->value,
        desired,
        old_val
    );
    *expected = result;
    return result == old_val;
}
bool XAtomic_compare_exchange_strong_uint64(XAtomic_uint64_t* var, uint64_t* expected, uint64_t desired)
{
    __int64 old_val = (__int64)*expected;
    __int64 result = _InterlockedCompareExchange64(
        (volatile __int64*)&var->value,
        (__int64)desired,
        old_val
    );
    *expected = (uint64_t)result;
    return result == old_val;
}

// 修复：size_t 的 CAS 应复用整数 CAS 函数而非指针函数
bool XAtomic_compare_exchange_strong_size_t(XAtomic_size_t* var, size_t* expected, size_t desired)
{
#if _M_X64
    return XAtomic_compare_exchange_strong_uint64((XAtomic_uint64_t*)var, expected, desired);
#else
    return XAtomic_compare_exchange_strong_uint32((XAtomic_uint32_t*)var, expected, desired);
#endif
}
bool XAtomic_compare_exchange_strong_ptr(XAtomic_ptr* var, void** expected, void* desired)
{
    void* actual_value = _InterlockedCompareExchangePointer(
        (volatile void**)&var->value,
        desired,
        *expected
    );

    bool success = (actual_value == *expected); // 1. 先判断是否成功
    *expected = actual_value;                  // 2. 再更新 expected

    return success;
}

int32_t XAtomic_fetch_add_int32(XAtomic_int32_t* var, int32_t value)
{
    return (int32_t)_InterlockedExchangeAdd((volatile long*)&var->value, (long)value);
}
uint32_t XAtomic_fetch_add_uint32(XAtomic_uint32_t* var, uint32_t value)
{
    return (uint32_t)_InterlockedExchangeAdd((volatile long*)&var->value, (long)value);
}

int32_t XAtomic_fetch_sub_int32(XAtomic_int32_t* var, int32_t value)
{
    return (int32_t)_InterlockedExchangeAdd((volatile long*)&var->value, -(long)value);
}
uint32_t XAtomic_fetch_sub_uint32(XAtomic_uint32_t* var, uint32_t value)
{
    return (uint32_t)_InterlockedExchangeAdd((volatile long*)&var->value, -(long)value);
}

#endif