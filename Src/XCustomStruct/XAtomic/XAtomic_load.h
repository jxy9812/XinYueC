#ifndef XATOMIC_LOAD_H
#define XATOMIC_LOAD_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * 原子加载操作 - 从原子变量中读取值
 * @param var 指向原子变量的常量指针
 * @return 原子变量的当前值
 *
 * 原子性保证：读取操作不会被中断，保证读取到完整的值
 */
bool XAtomic_load_bool(const XAtomic_bool* var);
int32_t XAtomic_load_int32(const XAtomic_int32_t* var);
uint32_t XAtomic_load_uint32(const XAtomic_uint32_t* var);
int64_t XAtomic_load_int64(const XAtomic_int64_t* var);
uint64_t XAtomic_load_uint64(const XAtomic_uint64_t* var);
size_t XAtomic_load_size_t(const XAtomic_size_t* var);
void* XAtomic_load_ptr(const XAtomic_ptr_t* var);
#ifdef __cplusplus
}
#endif
#endif