#ifndef XATOMIC_LOAD_H
#define XATOMIC_LOAD_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 原子加载操作 - 从原子布尔变量中读取值
 * @param var 指向原子布尔变量的常量指针
 * @param order 内存序
 * @return 原子变量的当前布尔值
 * @note 原子性保证：读取操作不会被中断，保证读取到完整的值
 */
bool XAtomic_load_bool(const XAtomic_bool* var, XAtomic_MemoryOrder order);

/**
 * @brief 原子加载操作 - 从原子32位有符号整数变量中读取值
 * @param var 指向原子32位有符号整数变量的常量指针
 * @param order 内存序
 * @return 原子变量的当前32位有符号整数值
 * @note 原子性保证：读取操作不会被中断，保证读取到完整的值
 */
int32_t XAtomic_load_int32(const XAtomic_int32_t* var, XAtomic_MemoryOrder order);

/**
 * @brief 原子加载操作 - 从原子32位无符号整数变量中读取值
 * @param var 指向原子32位无符号整数变量的常量指针
 * @param order 内存序
 * @return 原子变量的当前32位无符号整数值
 * @note 原子性保证：读取操作不会被中断，保证读取到完整的值
 */
uint32_t XAtomic_load_uint32(const XAtomic_uint32_t* var, XAtomic_MemoryOrder order);

/**
 * @brief 原子加载操作 - 从原子64位有符号整数变量中读取值
 * @param var 指向原子64位有符号整数变量的常量指针
 * @param order 内存序
 * @return 原子变量的当前64位有符号整数值
 * @note 原子性保证：读取操作不会被中断，保证读取到完整的值
 */
int64_t XAtomic_load_int64(const XAtomic_int64_t* var, XAtomic_MemoryOrder order);

/**
 * @brief 原子加载操作 - 从原子64位无符号整数变量中读取值
 * @param var 指向原子64位无符号整数变量的常量指针
 * @param order 内存序
 * @return 原子变量的当前64位无符号整数值
 * @note 原子性保证：读取操作不会被中断，保证读取到完整的值
 */
uint64_t XAtomic_load_uint64(const XAtomic_uint64_t* var, XAtomic_MemoryOrder order);

/**
 * @brief 原子加载操作 - 从原子size_t类型变量中读取值
 * @param var 指向原子size_t类型变量的常量指针
 * @param order 内存序
 * @return 原子变量的当前size_t类型值
 * @note 原子性保证：读取操作不会被中断，保证读取到完整的值
 */
size_t XAtomic_load_size_t(const XAtomic_size_t* var, XAtomic_MemoryOrder order);

/**
 * @brief 原子加载操作 - 从原子指针变量中读取值
 * @param var 指向原子指针变量的常量指针
 * @param order 内存序
 * @return 原子变量的当前指针值
 * @note 原子性保证：读取操作不会被中断，保证读取到完整的值
 */
void* XAtomic_load_ptr(const XAtomic_ptr* var, XAtomic_MemoryOrder order);

#ifdef __cplusplus
}
#endif
#endif