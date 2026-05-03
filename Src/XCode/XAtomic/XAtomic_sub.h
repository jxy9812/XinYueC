#ifndef XATOMIC_SUB_H
#define XATOMIC_SUB_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 原子减法操作 - 原子地从原子32位有符号整数变量中减去值，并返回旧值
 * @param var 指向原子32位有符号整数变量的指针
 * @param value 要减去的32位有符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
int32_t XAtomic_fetch_sub_int32(XAtomic_int32_t* var, int32_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子减法操作 - 原子地从原子32位无符号整数变量中减去值，并返回旧值
 * @param var 指向原子32位无符号整数变量的指针
 * @param value 要减去的32位无符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
uint32_t XAtomic_fetch_sub_uint32(XAtomic_uint32_t* var, uint32_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子减法操作 - 原子地从原子64位有符号整数变量中减去值，并返回旧值
 * @param var 指向原子64位有符号整数变量的指针
 * @param value 要减去的64位有符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
int64_t XAtomic_fetch_sub_int64(XAtomic_int64_t* var, int64_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子减法操作 - 原子地从原子64位无符号整数变量中减去值，并返回旧值
 * @param var 指向原子64位无符号整数变量的指针
 * @param value 要减去的64位无符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
uint64_t XAtomic_fetch_sub_uint64(XAtomic_uint64_t* var, uint64_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子减法操作 - 原子地从原子size_t类型变量中减去值，并返回旧值
 * @param var 指向原子size_t类型变量的指针
 * @param value 要减去的size_t类型值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
size_t XAtomic_fetch_sub_size_t(XAtomic_size_t* var, size_t value, XAtomic_MemoryOrder order);

uintptr_t XAtomic_fetch_sub_uintptr_t(XAtomic_uintptr_t* var, uintptr_t arg, XAtomic_MemoryOrder order);
#ifdef __cplusplus
}
#endif
#endif