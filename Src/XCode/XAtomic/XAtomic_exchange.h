#ifndef XATOMIC_EXCHANGE_H
#define XATOMIC_EXCHANGE_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 原子交换操作 - 用新值替换原子布尔变量的值，并返回旧值
 * @param var 指向原子布尔变量的指针
 * @param value 要存储的新布尔值
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-替换过程不可分割
 */
bool XAtomic_exchange_bool(XAtomic_bool* var, bool value);

/**
 * @brief 原子交换操作 - 用新值替换原子32位有符号整数变量的值，并返回旧值
 * @param var 指向原子32位有符号整数变量的指针
 * @param value 要存储的新32位有符号整数值
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-替换过程不可分割
 */
int32_t XAtomic_exchange_int32(XAtomic_int32_t* var, int32_t value);

/**
 * @brief 原子交换操作 - 用新值替换原子32位无符号整数变量的值，并返回旧值
 * @param var 指向原子32位无符号整数变量的指针
 * @param value 要存储的新32位无符号整数值
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-替换过程不可分割
 */
uint32_t XAtomic_exchange_uint32(XAtomic_uint32_t* var, uint32_t value);

/**
 * @brief 原子交换操作 - 用新值替换原子64位有符号整数变量的值，并返回旧值
 * @param var 指向原子64位有符号整数变量的指针
 * @param value 要存储的新64位有符号整数值
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-替换过程不可分割
 */
int64_t XAtomic_exchange_int64(XAtomic_int64_t* var, int64_t value);

/**
 * @brief 原子交换操作 - 用新值替换原子64位无符号整数变量的值，并返回旧值
 * @param var 指向原子64位无符号整数变量的指针
 * @param value 要存储的新64位无符号整数值
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-替换过程不可分割
 */
uint64_t XAtomic_exchange_uint64(XAtomic_uint64_t* var, uint64_t value);

/**
 * @brief 原子交换操作 - 用新值替换原子size_t类型变量的值，并返回旧值
 * @param var 指向原子size_t类型变量的指针
 * @param value 要存储的新size_t类型值
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-替换过程不可分割
 */
size_t XAtomic_exchange_size_t(XAtomic_size_t* var, size_t value);

/**
 * @brief 原子交换操作 - 用新值替换原子指针变量的值，并返回旧值
 * @param var 指向原子指针变量的指针
 * @param value 要存储的新指针值
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-替换过程不可分割
 */
void* XAtomic_exchange_ptr(XAtomic_ptr_t* var, void* value);
#ifdef __cplusplus
}
#endif
#endif