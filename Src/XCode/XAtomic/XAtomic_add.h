#ifndef XATOMIC_ADD_H
#define XATOMIC_ADD_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * 原子算术操作 - 原子地将值加到原子变量上，并返回旧值
 * @param var 指向原子变量的指针
 * @param value 要添加的值
 * @return 原子变量的旧值
 *
 * 原子性保证：整个读取-修改-写入过程不可分割
 */
int32_t XAtomic_fetch_add_int32(XAtomic_int32_t* var, int32_t value);
uint32_t XAtomic_fetch_add_uint32(XAtomic_uint32_t* var, uint32_t value);
int64_t XAtomic_fetch_add_int64(XAtomic_int64_t* var, int64_t value);
uint64_t XAtomic_fetch_add_uint64(XAtomic_uint64_t* var, uint64_t value);
size_t XAtomic_fetch_add_size_t(XAtomic_size_t* var, size_t value);
#ifdef __cplusplus
}
#endif
#endif