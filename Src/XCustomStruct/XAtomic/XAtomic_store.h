#ifndef XATOMIC_STORE_H
#define XATOMIC_STORE_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * 原子存储操作 - 将值写入原子变量
 * @param var 指向原子变量的指针
 * @param value 要存储的新值
 *
 * 原子性保证：写入操作不会被中断，保证写入的完整性
 */
void XAtomic_store_int32(XAtomic_int32_t* var, int32_t value);
void XAtomic_store_uint32(XAtomic_uint32_t* var, uint32_t value);
void XAtomic_store_int64(XAtomic_int64_t* var, int64_t value);
void XAtomic_store_uint64(XAtomic_uint64_t* var, uint64_t value);
void XAtomic_store_size_t(XAtomic_size_t* var, size_t value);
void XAtomic_store_ptr(XAtomic_ptr_t* var, void* value);
#ifdef __cplusplus
}
#endif
#endif