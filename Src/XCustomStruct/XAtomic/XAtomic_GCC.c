#ifdef __GNUC__ 
// 判断是否为 64 位系统（x86_64 或 ARM64 等）
#if defined(__x86_64__) || defined(__aarch64__) || defined(__ppc64__)
#define IS_64BIT 1
#else
#define IS_64BIT 0
#endif
#include"XAtomic.h"
#include <stdatomic.h>
#if IS_64BIT
void XAtomic_store_int64(XAtomic_int64_t* var, int64_t value)
{
	__atomic_store_n(&(var->value), value, __ATOMIC_SEQ_CST);
}
void XAtomic_store_uint64(XAtomic_uint64_t* var, uint64_t value)
{
	__atomic_store_n(&(var->value), value, __ATOMIC_SEQ_CST);
}
int64_t XAtomic_exchange_int64(XAtomic_int64_t* var, int64_t value)
{
	return __atomic_exchange_n(&(var->value), value, __ATOMIC_SEQ_CST);
}
uint64_t XAtomic_exchange_uint64(XAtomic_uint64_t* var, uint64_t value)
{
	return __atomic_exchange_n(&(var->value), value, __ATOMIC_SEQ_CST);
}

int64_t XAtomic_fetch_add_int64(XAtomic_int64_t* var, int64_t value)
{
	return _InterlockedExchangeAdd64((volatile long long*)&var->value, value);
}
uint64_t XAtomic_fetch_add_uint64(XAtomic_uint64_t* var, uint64_t value)
{
	return _InterlockedExchangeAdd64((volatile long long*)&var->value, value);
}
int64_t XAtomic_fetch_sub_int64(XAtomic_int64_t* var, int64_t value)
{
	return _InterlockedExchangeAdd64((volatile long long*)&var->value, -value);
}
uint64_t XAtomic_fetch_sub_uint64(XAtomic_uint64_t* var, uint64_t value)
{
	return _InterlockedExchangeAdd64((volatile long long*)&var->value, -value);
}

#endif // IS_64BIT
size_t XAtomic_fetch_add_size_t(XAtomic_size_t* var, size_t value)
{
	//_InterlockedCompareExchangePointer
	//_InterlockedExchangeAdd64
#if IS_64BIT
	return XAtomic_fetch_add_uint64(var, value);
#else
	return XAtomic_fetch_add_uint32(var, value);
#endif
}
size_t XAtomic_fetch_sub_size_t(XAtomic_size_t* var, size_t value)
{
#if IS_64BIT
	return XAtomic_fetch_sub_uint64(var, value);
#else
	return XAtomic_fetch_sub_uint32(var, value);
#endif
}
void XAtomic_memory_barrier()
{
	__sync_synchronize();
}
void XAtomic_memory_barrier_acquire()
{
	__atomic_thread_fence(__ATOMIC_ACQUIRE);
}
void XAtomic_memory_barrier_release()
{
	__atomic_thread_fence(__ATOMIC_RELEASE);
}
bool XAtomic_load_bool(const XAtomic_bool* var)
{
	return __atomic_load_n(&(var->value), __ATOMIC_SEQ_CST);
}
int32_t XAtomic_load_int32(const XAtomic_int32_t* var)
{
	return __atomic_load_n(&(var->value), __ATOMIC_SEQ_CST);
}
uint32_t XAtomic_load_uint32(const XAtomic_uint32_t* var)
{
	return __atomic_load_n(&(var->value), __ATOMIC_SEQ_CST);
}
int64_t XAtomic_load_int64(const XAtomic_int64_t* var)
{
	return __atomic_load_n(&(var->value), __ATOMIC_SEQ_CST);
}
uint64_t XAtomic_load_uint64(const XAtomic_uint64_t* var)
{
	return __atomic_load_n(&(var->value), __ATOMIC_SEQ_CST);
}
size_t XAtomic_load_size_t(const XAtomic_size_t* var)
{
	return XAtomic_load_ptr(var);
}
void* XAtomic_load_ptr(const XAtomic_ptr_t* var)
{
	return __atomic_load_n(&(var->ptr), __ATOMIC_SEQ_CST);
}
void XAtomic_store_bool(XAtomic_bool* var, bool value)
{
	__atomic_store_n(&(var->value), value, __ATOMIC_SEQ_CST);
}
void XAtomic_store_int32(XAtomic_int32_t* var, int32_t value)
{
	__atomic_store_n(&(var->value), value, __ATOMIC_SEQ_CST);
}
void XAtomic_store_uint32(XAtomic_uint32_t* var, uint32_t value)
{
	__atomic_store_n(&(var->value), value, __ATOMIC_SEQ_CST);
}
void XAtomic_store_ptr(XAtomic_ptr_t* var, void* value)
{
	__atomic_store_n(&(var->ptr), value, __ATOMIC_SEQ_CST);
}
void XAtomic_store_size_t(XAtomic_size_t* var, size_t value)
{
	XAtomic_store_ptr(var, value);
}
bool XAtomic_exchange_bool(XAtomic_bool* var, bool value)
{
	return __atomic_exchange_n(&(var->value), value, __ATOMIC_SEQ_CST);
}
int32_t XAtomic_exchange_int32(XAtomic_int32_t* var, int32_t value)
{
	return __atomic_exchange_n(&(var->value), value, __ATOMIC_SEQ_CST);
}
uint32_t XAtomic_exchange_uint32(XAtomic_uint32_t* var, uint32_t value)
{
	return __atomic_exchange_n(&(var->value), value, __ATOMIC_SEQ_CST);
}
size_t XAtomic_exchange_size_t(XAtomic_size_t* var, size_t value)
{
	return XAtomic_exchange_ptr(var, value);
}
void* XAtomic_exchange_ptr(XAtomic_ptr_t* var, void* value)
{
	return __atomic_exchange_n(&(var->ptr), value, __ATOMIC_SEQ_CST);
}
bool XAtomic_compare_exchange_strong_bool(XAtomic_bool* var, bool* expected, bool desired)
{
	return __atomic_compare_exchange_n(
		&(var->value), expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
bool XAtomic_compare_exchange_strong_int32(XAtomic_int32_t* var, int32_t* expected, int32_t desired)
{
	return __atomic_compare_exchange_n(
		&(var->value), expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
bool XAtomic_compare_exchange_strong_uint32(XAtomic_uint32_t* var, uint32_t* expected, uint32_t desired)
{
	return __atomic_compare_exchange_n(
		&(var->value), expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
bool XAtomic_compare_exchange_strong_int64(XAtomic_int64_t* var, int64_t* expected, int64_t desired)
{
	return __atomic_compare_exchange_n(
		&(var->value), expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
bool XAtomic_compare_exchange_strong_uint64(XAtomic_uint64_t* var, uint64_t* expected, uint64_t desired)
{
	return __atomic_compare_exchange_n(
		&(var->value), expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
bool XAtomic_compare_exchange_strong_size_t(XAtomic_size_t* var, size_t* expected, size_t desired)
{
	return XAtomic_compare_exchange_strong_ptr(var, expected, desired);
}
bool XAtomic_compare_exchange_strong_ptr(XAtomic_ptr_t* var, void** expected, void* desired)
{
	return __atomic_compare_exchange_n(
		&(var->ptr), expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}
int32_t XAtomic_fetch_add_int32(XAtomic_int32_t* var, int32_t value)
{
	return __atomic_fetch_add(&(var->value), value, __ATOMIC_SEQ_CST);
}
uint32_t XAtomic_fetch_add_uint32(XAtomic_uint32_t* var, uint32_t value)
{
	return __atomic_fetch_add(&(var->value), value, __ATOMIC_SEQ_CST);
}

int32_t XAtomic_fetch_sub_int32(XAtomic_int32_t* var, int32_t value)
{
	return __atomic_fetch_sub(&(var->value), value, __ATOMIC_SEQ_CST);
}
uint32_t XAtomic_fetch_sub_uint32(XAtomic_uint32_t* var, uint32_t value)
{
	return __atomic_fetch_sub(&(var->value), value, __ATOMIC_SEQ_CST);
}

#endif