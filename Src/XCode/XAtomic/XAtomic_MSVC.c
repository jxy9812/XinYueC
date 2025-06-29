#ifdef _MSC_VER 
#include"XAtomic.h"
#include <intrin.h>
#if _M_X64
void XAtomic_store_int64(XAtomic_int64_t* var, int64_t value)
{
	_InterlockedExchange64((volatile __int64*)&var->value, value);
}
void XAtomic_store_uint64(XAtomic_uint64_t* var, uint64_t value)
{
	_InterlockedExchange64((volatile __int64*)&var->value, value);
}
int64_t XAtomic_exchange_int64(XAtomic_int64_t* var, int64_t value)
{
	return _InterlockedExchange64((volatile __int64*)&var->value, value);
}
uint64_t XAtomic_exchange_uint64(XAtomic_uint64_t* var, uint64_t value)
{
	return _InterlockedExchange64((volatile __int64*)&var->value, value);
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

#endif // _M_X64

size_t XAtomic_fetch_add_size_t(XAtomic_size_t* var, size_t value)
{
	//_InterlockedCompareExchangePointer
	//_InterlockedExchangeAdd64
#if _M_X64
	return XAtomic_fetch_add_uint64(var, value);
#else
	return XAtomic_fetch_add_uint32(var, value);
#endif
}
size_t XAtomic_fetch_sub_size_t(XAtomic_size_t* var, size_t value)
{
#if _M_X64
	return XAtomic_fetch_sub_uint64(var, value);
#else
	return XAtomic_fetch_sub_uint32(var, value);
#endif
}
void XAtomic_memory_barrier()
{
	_ReadWriteBarrier();
}
void XAtomic_memory_barrier_acquire()
{
	_ReadBarrier();
}
void XAtomic_memory_barrier_release()
{
	_WriteBarrier();
}
bool XAtomic_load_bool(const XAtomic_bool* var)
{
	return _InterlockedCompareExchange((volatile bool*)&(var->value), 0, 0);
}
int32_t XAtomic_load_int32(const XAtomic_int32_t* var)
{
	return _InterlockedCompareExchange((volatile long*)&(var->value), 0, 0);
}
uint32_t XAtomic_load_uint32(const XAtomic_uint32_t* var)
{
	return _InterlockedCompareExchange((volatile long*)&(var->value), 0, 0);
}
int64_t XAtomic_load_int64(const XAtomic_int64_t* var)
{
	return _InterlockedCompareExchange64((volatile __int64*)&(var->value), 0, 0);
}
uint64_t XAtomic_load_uint64(const XAtomic_uint64_t* var)
{
	return _InterlockedCompareExchange64((volatile __int64*)&var->value, 0, 0);
}
size_t XAtomic_load_size_t(const XAtomic_size_t* var)
{
	return XAtomic_load_ptr(var);
}
void* XAtomic_load_ptr(const XAtomic_ptr_t* var)
{
	return (void*)_InterlockedCompareExchangePointer((volatile void*)&var->value, NULL, NULL);
}
void XAtomic_store_bool(XAtomic_bool* var, bool value)
{
	_InterlockedExchange((volatile long*)&var->value, value);
}
void XAtomic_store_int32(XAtomic_int32_t* var, int32_t value)
{
	_InterlockedExchange((volatile long*)&var->value, value);
}
void XAtomic_store_uint32(XAtomic_uint32_t* var, uint32_t value)
{
	_InterlockedExchange((volatile long*)&var->value, value);
}
void XAtomic_store_ptr(XAtomic_ptr_t* var, void* value)
{
	_InterlockedExchangePointer((volatile void*)&var->value, value);
}
void XAtomic_store_size_t(XAtomic_size_t* var, size_t value)
{
	XAtomic_store_ptr(var, value);
}
bool XAtomic_exchange_bool(XAtomic_bool* var, bool value)
{
	return _InterlockedExchange((volatile bool*)&var->value, value);
}
int32_t XAtomic_exchange_int32(XAtomic_int32_t* var, int32_t value)
{
	return _InterlockedExchange((volatile long*)&var->value, value);
}
uint32_t XAtomic_exchange_uint32(XAtomic_uint32_t* var, uint32_t value)
{
	return _InterlockedExchange((volatile long*)&var->value, value);
}
size_t XAtomic_exchange_size_t(XAtomic_size_t* var, size_t value)
{
	return XAtomic_exchange_ptr(var, value);
}
void* XAtomic_exchange_ptr(XAtomic_ptr_t* var, void* value)
{
	return _InterlockedExchangePointer((volatile void*)&var->value, value);
}
bool XAtomic_compare_exchange_strong_bool(XAtomic_bool* var, bool* expected, bool desired)
{
	return _InterlockedCompareExchange((volatile bool*)&var->value, desired, *expected) == *expected;
}
bool XAtomic_compare_exchange_strong_int32(XAtomic_int32_t* var, int32_t* expected, int32_t desired)
{
	return _InterlockedCompareExchange((volatile long*)&var->value, desired, *expected) == *expected;
}
bool XAtomic_compare_exchange_strong_uint32(XAtomic_uint32_t* var, uint32_t* expected, uint32_t desired)
{
	return _InterlockedCompareExchange((volatile long*)&var->value, desired, *expected) == *expected;
}
bool XAtomic_compare_exchange_strong_int64(XAtomic_int64_t* var, int64_t* expected, int64_t desired)
{
	return _InterlockedCompareExchange64((volatile __int64*)&var->value, desired, *expected) == *expected;
}
bool XAtomic_compare_exchange_strong_uint64(XAtomic_uint64_t* var, uint64_t* expected, uint64_t desired)
{
	return _InterlockedCompareExchange64((volatile __int64*)&var->value, desired, *expected) == *expected;
}
bool XAtomic_compare_exchange_strong_size_t(XAtomic_size_t* var, size_t* expected, size_t desired)
{
	return XAtomic_compare_exchange_strong_ptr(var,expected,desired);
//#if _M_X64
//	return XAtomic_compare_exchange_strong_uint64(var, expected, desired);
//#else
//	return XAtomic_compare_exchange_strong_uint32(var,expected,desired);
//#endif
}
bool XAtomic_compare_exchange_strong_ptr(XAtomic_ptr_t* var, void** expected, void* desired)
{
	return (void*)_InterlockedCompareExchangePointer((volatile void*)&var->value, desired, *expected)== *expected;
}

int32_t XAtomic_fetch_add_int32(XAtomic_int32_t* var, int32_t value)
{
	return _InterlockedExchangeAdd((volatile long*)&var->value, value);
}
uint32_t XAtomic_fetch_add_uint32(XAtomic_uint32_t* var, uint32_t value)
{
	return _InterlockedExchangeAdd((volatile long*)&var->value, value);
}

int32_t XAtomic_fetch_sub_int32(XAtomic_int32_t* var, int32_t value)
{
	return _InterlockedExchangeAdd((volatile long*)&var->value, -value);
}
uint32_t XAtomic_fetch_sub_uint32(XAtomic_uint32_t* var, uint32_t value)
{
	return _InterlockedExchangeAdd((volatile long*)&var->value, -value);
}

#endif