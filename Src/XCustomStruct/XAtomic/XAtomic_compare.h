#ifndef XATOMIC_COMPARE_H
#define XATOMIC_COMPARE_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * 原子比较交换操作 (CAS)
 * @param var 指向原子变量的指针
 * @param expected 指向期望值的指针（用于比较）
 * @param desired 如果当前值等于期望值，则存储该值
 * @return 如果交换成功返回非零值，否则返回0
 *
 * 操作说明：
 * 1. 比较原子变量的当前值与`*expected`
 * 2. 如果相等，则将`desired`存储到原子变量中
 * 3. 如果不相等，则将原子变量的当前值写入`*expected`
 *
 * 原子性保证：整个比较-交换过程不可分割
 */
bool XAtomic_compare_exchange_strong_bool(XAtomic_bool* var, bool* expected, bool desired);
bool XAtomic_compare_exchange_strong_int32(XAtomic_int32_t* var, int32_t* expected, int32_t desired);
bool XAtomic_compare_exchange_strong_uint32(XAtomic_uint32_t* var, uint32_t* expected, uint32_t desired);
bool XAtomic_compare_exchange_strong_int64(XAtomic_int64_t* var, int64_t* expected, int64_t desired);
bool XAtomic_compare_exchange_strong_uint64(XAtomic_uint64_t* var, uint64_t* expected, uint64_t desired);
bool XAtomic_compare_exchange_strong_size_t(XAtomic_size_t* var, size_t* expected, size_t desired);
bool XAtomic_compare_exchange_strong_ptr(XAtomic_ptr_t* var, void** expected, void* desired);
#ifdef __cplusplus
}
#endif
#endif