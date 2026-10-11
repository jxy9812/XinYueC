/**
 * @file XAtomic_sub.h
 * @brief 原子减法操作公开 API。
 * @details 提供 fetch_sub 族原子读-改-写接口，内存序语义与 C11/C++11 标准对应。
 *          实现只依赖 XinYueC 抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API；
 *          平台相关原子指令由 Drive/ 平台适配层实现。
 */
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
int32_t XAtomic_fetch_sub_int32(XAtomic_Int32* var, int32_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子减法操作 - 原子地从原子32位无符号整数变量中减去值，并返回旧值
 * @param var 指向原子32位无符号整数变量的指针
 * @param value 要减去的32位无符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
uint32_t XAtomic_fetch_sub_uint32(XAtomic_Uint32* var, uint32_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子减法操作 - 原子地从原子64位有符号整数变量中减去值，并返回旧值
 * @param var 指向原子64位有符号整数变量的指针
 * @param value 要减去的64位有符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
int64_t XAtomic_fetch_sub_int64(XAtomic_Int64* var, int64_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子减法操作 - 原子地从原子64位无符号整数变量中减去值，并返回旧值
 * @param var 指向原子64位无符号整数变量的指针
 * @param value 要减去的64位无符号整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
uint64_t XAtomic_fetch_sub_uint64(XAtomic_Uint64* var, uint64_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子减法操作 - 原子地从原子size_t类型变量中减去值，并返回旧值
 * @param var 指向原子size_t类型变量的指针
 * @param value 要减去的size_t类型值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
size_t XAtomic_fetch_sub_size_t(XAtomic_SizeT* var, size_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子减法操作 - 原子地从原子指针变量中减去值，并返回旧值
 * @param var 指向原子指针变量的指针
 * @param arg 要减去的指针整数值
 * @param order 内存序
 * @return 原子变量的旧值
 * @note 原子性保证：整个读取-修改-写入过程不可分割
 */
uintptr_t XAtomic_fetch_sub_uintptr_t(XAtomic_Uintptr* var, uintptr_t arg, XAtomic_MemoryOrder order);
#ifdef __cplusplus
}
#endif
#endif