#ifndef XATOMIC_ADD_H
#define XATOMIC_ADD_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 原子加法操作 - 原子地将值加到原子32位有符号整数变量上，并返回旧值
 * @param var 指向原子32位有符号整数变量的指针
 * @param value 要添加的32位有符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
int32_t XAtomic_fetch_add_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子加法操作 - 原子地将值加到原子32位无符号整数变量上，并返回旧值
 * @param var 指向原子32位无符号整数变量的指针
 * @param value 要添加的32位无符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
uint32_t XAtomic_fetch_add_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子加法操作 - 原子地将值加到原子64位有符号整数变量上，并返回旧值
 * @param var 指向原子64位有符号整数变量的指针
 * @param value 要添加的64位有符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
int64_t XAtomic_fetch_add_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子加法操作 - 原子地将值加到原子64位无符号整数变量上，并返回旧值
 * @param var 指向原子64位无符号整数变量的指针
 * @param value 要添加的64位无符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
uint64_t XAtomic_fetch_add_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子加法操作 - 原子地将值加到原子size_t类型变量上，并返回旧值
 * @param var 指向原子size_t类型变量的指针
 * @param value 要添加的size_t类型值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
size_t XAtomic_fetch_add_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order);
#ifdef __cplusplus
}
#endif
#endif