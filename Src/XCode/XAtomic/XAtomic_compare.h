#ifndef XATOMIC_COMPARE_H
#define XATOMIC_COMPARE_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 原子比较交换操作 (CAS) - 针对原子布尔变量
 * @param var 指向原子布尔变量的指针
 * @param expected 指向期望值的指针（用于比较）
 * @param desired 如果当前值等于期望值，则存储该布尔值
 * @return 如果交换成功返回非零值，否则返回0
 * @note 操作说明：
 * 1. 比较原子变量的当前值与`*expected`
 * 2. 如果相等，则将`desired`存储到原子变量中
 * 3. 如果不相等，则将原子变量的当前值写入`*expected`
 * 原子性保证：整个比较-交换过程不可分割
 */
bool XAtomic_compare_exchange_strong_bool(XAtomic_bool* var, bool* expected, bool desired);

/**
 * @brief 原子比较交换操作 (CAS) - 针对原子32位有符号整数变量
 * @param var 指向原子32位有符号整数变量的指针
 * @param expected 指向期望值的指针（用于比较）
 * @param desired 如果当前值等于期望值，则存储该32位有符号整数值
 * @return 如果交换成功返回非零值，否则返回0
 * @note 操作说明：
 * 1. 比较原子变量的当前值与`*expected`
 * 2. 如果相等，则将`desired`存储到原子变量中
 * 3. 如果不相等，则将原子变量的当前值写入`*expected`
 * 原子性保证：整个比较-交换过程不可分割
 */
bool XAtomic_compare_exchange_strong_int32(XAtomic_int32_t* var, int32_t* expected, int32_t desired);

/**
 * @brief 原子比较交换操作 (CAS) - 针对原子32位无符号整数变量
 * @param var 指向原子32位无符号整数变量的指针
 * @param expected 指向期望值的指针（用于比较）
 * @param desired 如果当前值等于期望值，则存储该32位无符号整数值
 * @return 如果交换成功返回非零值，否则返回0
 * @note 操作说明：
 * 1. 比较原子变量的当前值与`*expected`
 * 2. 如果相等，则将`desired`存储到原子变量中
 * 3. 如果不相等，则将原子变量的当前值写入`*expected`
 * 原子性保证：整个比较-交换过程不可分割
 */
bool XAtomic_compare_exchange_strong_uint32(XAtomic_uint32_t* var, uint32_t* expected, uint32_t desired);

/**
 * @brief 原子比较交换操作 (CAS) - 针对原子64位有符号整数变量
 * @param var 指向原子64位有符号整数变量的指针
 * @param expected 指向期望值的指针（用于比较）
 * @param desired 如果当前值等于期望值，则存储该64位有符号整数值
 * @return 如果交换成功返回非零值，否则返回0
 * @note 操作说明：
 * 1. 比较原子变量的当前值与`*expected`
 * 2. 如果相等，则将`desired`存储到原子变量中
 * 3. 如果不相等，则将原子变量的当前值写入`*expected`
 * 原子性保证：整个比较-交换过程不可分割
 */
bool XAtomic_compare_exchange_strong_int64(XAtomic_int64_t* var, int64_t* expected, int64_t desired);

/**
 * @brief 原子比较交换操作 (CAS) - 针对原子64位无符号整数变量
 * @param var 指向原子64位无符号整数变量的指针
 * @param expected 指向期望值的指针（用于比较）
 * @param desired 如果当前值等于期望值，则存储该64位无符号整数值
 * @return 如果交换成功返回非零值，否则返回0
 * @note 操作说明：
 * 1. 比较原子变量的当前值与`*expected`
 * 2. 如果相等，则将`desired`存储到原子变量中
 * 3. 如果不相等，则将原子变量的当前值写入`*expected`
 * 原子性保证：整个比较-交换过程不可分割
 */
bool XAtomic_compare_exchange_strong_uint64(XAtomic_uint64_t* var, uint64_t* expected, uint64_t desired);

/**
 * @brief 原子比较交换操作 (CAS) - 针对原子size_t类型变量
 * @param var 指向原子size_t类型变量的指针
 * @param expected 指向期望值的指针（用于比较）
 * @param desired 如果当前值等于期望值，则存储该size_t类型值
 * @return 如果交换成功返回非零值，否则返回0
 * @note 操作说明：
 * 1. 比较原子变量的当前值与`*expected`
 * 2. 如果相等，则将`desired`存储到原子变量中
 * 3. 如果不相等，则将原子变量的当前值写入`*expected`
 * 原子性保证：整个比较-交换过程不可分割
 */
bool XAtomic_compare_exchange_strong_size_t(XAtomic_size_t* var, size_t* expected, size_t desired);

/**
 * @brief 原子比较交换操作 (CAS) - 针对原子指针变量
 * @param var 指向原子指针变量的指针
 * @param expected 指向期望指针值的指针（用于比较）
 * @param desired 如果当前值等于期望值，则存储该指针值
 * @return 如果交换成功返回非零值，否则返回0
 * @note 操作说明：
 * 1. 比较原子变量的当前值与`*expected`
 * 2. 如果相等，则将`desired`存储到原子变量中
 * 3. 如果不相等，则将原子变量的当前值写入`*expected`
 * 原子性保证：整个比较-交换过程不可分割
 */
bool XAtomic_compare_exchange_strong_ptr(XAtomic_ptr_t* var, void** expected, void* desired);
#ifdef __cplusplus
}
#endif
#endif