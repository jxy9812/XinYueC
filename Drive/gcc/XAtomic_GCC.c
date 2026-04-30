#ifdef __GNUC__ 
// 判断是否为 64 位系统（x86_64 或 ARM64 等）
#if defined(__x86_64__) || defined(__aarch64__) || defined(__ppc64__)
#define IS_64BIT 1
#else
#define IS_64BIT 0
#endif
#include "XAtomic.h"
#include <stdatomic.h>

// ========== 辅助函数：将 XAtomic_MemoryOrder 映射到 GCC 的 __ATOMIC_* 常量 ==========
static inline int xatomic_to_gcc_memory_order(XAtomic_MemoryOrder order)
{
    switch (order) {
    case XAtomic_MemoryOrder_Relaxed: return __ATOMIC_RELAXED;
    case XAtomic_MemoryOrder_Consume: return __ATOMIC_CONSUME;
    case XAtomic_MemoryOrder_Acquire: return __ATOMIC_ACQUIRE;
    case XAtomic_MemoryOrder_Release: return __ATOMIC_RELEASE;
    case XAtomic_MemoryOrder_AcqRel:  return __ATOMIC_ACQ_REL;
    case XAtomic_MemoryOrder_SeqCst:  return __ATOMIC_SEQ_CST;
    default: return __ATOMIC_SEQ_CST; // 默认使用最强内存序
    }
}

// ========== 通用加载/存储操作 ==========
// Load
bool XAtomic_load_bool(const XAtomic_bool* var, XAtomic_MemoryOrder order)
{
    bool result;
    __atomic_load(&var->value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

int32_t XAtomic_load_int32(const XAtomic_int32_t* var, XAtomic_MemoryOrder order)
{
    int32_t result;
    __atomic_load(&var->value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

uint32_t XAtomic_load_uint32(const XAtomic_uint32_t* var, XAtomic_MemoryOrder order)
{
    uint32_t result;
    __atomic_load(&var->value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

int64_t XAtomic_load_int64(const XAtomic_int64_t* var, XAtomic_MemoryOrder order)
{
    int64_t result;
    __atomic_load(&var->value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

uint64_t XAtomic_load_uint64(const XAtomic_uint64_t* var, XAtomic_MemoryOrder order)
{
    uint64_t result;
    __atomic_load(&var->value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

size_t XAtomic_load_size_t(const XAtomic_size_t* var, XAtomic_MemoryOrder order)
{
    size_t result;
    __atomic_load(&var->value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

void* XAtomic_load_ptr(const XAtomic_ptr* var, XAtomic_MemoryOrder order)
{
    void* result;
    __atomic_load(&var->value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

// Store
void XAtomic_store_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
{
    __atomic_store(&var->value, &value, xatomic_to_gcc_memory_order(order));
}

void XAtomic_store_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    __atomic_store(&var->value, &value, xatomic_to_gcc_memory_order(order));
}

void XAtomic_store_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    __atomic_store(&var->value, &value, xatomic_to_gcc_memory_order(order));
}

void XAtomic_store_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    __atomic_store(&var->value, &value, xatomic_to_gcc_memory_order(order));
}

void XAtomic_store_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    __atomic_store(&var->value, &value, xatomic_to_gcc_memory_order(order));
}

void XAtomic_store_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    __atomic_store(&var->value, &value, xatomic_to_gcc_memory_order(order));
}

void XAtomic_store_ptr(XAtomic_ptr* var, void* value, XAtomic_MemoryOrder order)
{
    __atomic_store(&var->value, &value, xatomic_to_gcc_memory_order(order));
}

// ========== 交换操作 (Exchange) ==========
bool XAtomic_exchange_bool(XAtomic_bool* var, bool value, XAtomic_MemoryOrder order)
{
    bool result;
    __atomic_exchange(&var->value, &value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

int32_t XAtomic_exchange_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    int32_t result;
    __atomic_exchange(&var->value, &value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

uint32_t XAtomic_exchange_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    uint32_t result;
    __atomic_exchange(&var->value, &value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

int64_t XAtomic_exchange_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    int64_t result;
    __atomic_exchange(&var->value, &value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

uint64_t XAtomic_exchange_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    uint64_t result;
    __atomic_exchange(&var->value, &value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

size_t XAtomic_exchange_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    size_t result;
    __atomic_exchange(&var->value, &value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

void* XAtomic_exchange_ptr(XAtomic_ptr* var, void* value, XAtomic_MemoryOrder order)
{
    void* result;
    __atomic_exchange(&var->value, &value, &result, xatomic_to_gcc_memory_order(order));
    return result;
}

// ========== 比较交换操作 (Compare Exchange) ==========
// 注意：GCC 的 __atomic_compare_exchange_n 函数有一个 'weak' 参数。
// 我们的 API 是 'strong' 版本，所以我们将其设为 false。
#define DEFINE_CAS_FUNC(type_name, type) \
bool XAtomic_compare_exchange_strong_##type_name(XAtomic_##type_name* var, type* expected, type desired, XAtomic_MemoryOrder success_order, XAtomic_MemoryOrder failure_order) \
{ \
    return __atomic_compare_exchange_n( \
        &var->value, \
        expected, \
        desired, \
        /* weak = */ false, \
        xatomic_to_gcc_memory_order(success_order), \
        xatomic_to_gcc_memory_order(failure_order) \
    ); \
}

DEFINE_CAS_FUNC(bool, bool)
DEFINE_CAS_FUNC(int32_t, int32_t)
DEFINE_CAS_FUNC(uint32_t, uint32_t)
DEFINE_CAS_FUNC(int64_t, int64_t)
DEFINE_CAS_FUNC(uint64_t, uint64_t)
DEFINE_CAS_FUNC(size_t, size_t)
DEFINE_CAS_FUNC(ptr, void*)

// ========== 加法/减法操作 (Fetch Add/Sub) ==========
// Fetch Add
int32_t XAtomic_fetch_add_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_add(&var->value, value, xatomic_to_gcc_memory_order(order));
}

uint32_t XAtomic_fetch_add_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_add(&var->value, value, xatomic_to_gcc_memory_order(order));
}

int64_t XAtomic_fetch_add_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_add(&var->value, value, xatomic_to_gcc_memory_order(order));
}

uint64_t XAtomic_fetch_add_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_add(&var->value, value, xatomic_to_gcc_memory_order(order));
}

size_t XAtomic_fetch_add_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_add(&var->value, value, xatomic_to_gcc_memory_order(order));
}

// Fetch Sub
int32_t XAtomic_fetch_sub_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_sub(&var->value, value, xatomic_to_gcc_memory_order(order));
}

uint32_t XAtomic_fetch_sub_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_sub(&var->value, value, xatomic_to_gcc_memory_order(order));
}

int64_t XAtomic_fetch_sub_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_sub(&var->value, value, xatomic_to_gcc_memory_order(order));
}

uint64_t XAtomic_fetch_sub_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_sub(&var->value, value, xatomic_to_gcc_memory_order(order));
}

size_t XAtomic_fetch_sub_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order)
{
    return __atomic_fetch_sub(&var->value, value, xatomic_to_gcc_memory_order(order));
}

// ========== 内存屏障 API ==========
void XAtomic_memory_barrier()
{
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void XAtomic_memory_barrier_acquire()
{
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

void XAtomic_memory_barrier_release()
{
    __atomic_thread_fence(__ATOMIC_RELEASE);
}
#endif