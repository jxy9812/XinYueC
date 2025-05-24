#ifndef XATOMIC_EXCHANGE_H
#define XATOMIC_EXCHANGE_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * 原子交换操作 - 用新值替换原子变量的值，并返回旧值
 * @param var 指向原子变量的指针
 * @param value 要存储的新值
 * @return 原子变量的旧值
 *
 * 原子性保证：整个读取-替换过程不可分割
 */
int32_t XAtomic_exchange_int32(XAtomic_int32_t* var, int32_t value);
uint32_t XAtomic_exchange_uint32(XAtomic_uint32_t* var, uint32_t value);
int64_t XAtomic_exchange_int64(XAtomic_int64_t* var, int64_t value);
uint64_t XAtomic_exchange_uint64(XAtomic_uint64_t* var, uint64_t value);
size_t XAtomic_exchange_size_t(XAtomic_size_t* var, size_t value);
void* XAtomic_exchange_ptr(XAtomic_ptr_t* var, void* value);
#ifdef __cplusplus
}
#endif
#endif