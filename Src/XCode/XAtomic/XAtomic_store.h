#ifndef XATOMIC_STORE_H
#define XATOMIC_STORE_H
#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief 原子存储操作 - 将值写入原子布尔变量
 * @param var 指向原子布尔变量的指针
 * @param value 要存储的新布尔值
 * @param order 内存序
 * @note 原子性保证：写入操作不会被中断，保证写入的完整性
 */
void XAtomic_store_bool(XAtomic_Bool* var, bool value, XAtomic_MemoryOrder order);

/**
 * @brief 原子存储操作 - 将值写入原子32位有符号整数变量
 * @param var 指向原子32位有符号整数变量的指针
 * @param value 要存储的新32位有符号整数值
 * @param order 内存序
 * @note 原子性保证：写入操作不会被中断，保证写入的完整性
 */
void XAtomic_store_int32(XAtomic_Int32* var, int32_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子存储操作 - 将值写入原子32位无符号整数变量
 * @param var 指向原子32位无符号整数变量的指针
 * @param value 要存储的新32位无符号整数值
 * @param order 内存序
 * @note 原子性保证：写入操作不会被中断，保证写入的完整性
 */
void XAtomic_store_uint32(XAtomic_Uint32* var, uint32_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子存储操作 - 将值写入原子64位有符号整数变量
 * @param var 指向原子64位有符号整数变量的指针
 * @param value 要存储的新64位有符号整数值
 * @param order 内存序
 * @note 原子性保证：写入操作不会被中断，保证写入的完整性
 */
void XAtomic_store_int64(XAtomic_Int64* var, int64_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子存储操作 - 将值写入原子64位无符号整数变量
 * @param var 指向原子64位无符号整数变量的指针
 * @param value 要存储的新64位无符号整数值
 * @param order 内存序
 * @note 原子性保证：写入操作不会被中断，保证写入的完整性
 */
void XAtomic_store_uint64(XAtomic_Uint64* var, uint64_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子存储操作 - 将值写入原子size_t类型变量
 * @param var 指向原子size_t类型变量的指针
 * @param value 要存储的新size_t类型值
 * @param order 内存序
 * @note 原子性保证：写入操作不会被中断，保证写入的完整性
 */
void XAtomic_store_size_t(XAtomic_SizeT* var, size_t value, XAtomic_MemoryOrder order);

/**
 * @brief 原子存储操作 - 将值写入原子指针变量
 * @param var 指向原子指针变量的指针
 * @param value 要存储的新指针值
 * @param order 内存序
 * @note 原子性保证：写入操作不会被中断，保证写入的完整性
 */
void XAtomic_store_uintptr_t(XAtomic_Uintptr* var, uintptr_t value, XAtomic_MemoryOrder order);

#ifdef __cplusplus
}
#endif
#endif