#ifndef XALGORITHM_H
#define XALGORITHM_H

/**
 * @file XAlgorithm.h
 * @brief XAlgorithm 基础算法与通用内存操作 API。
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

typedef struct XStack XStack;
typedef struct XVector XVector;

/**
 * @brief 交换两个相同类型对象的值。
 * @param ValOne 第一个对象的地址。
 * @param ValTwo 第二个对象的地址；应与 ValOne 具有相同对象大小。
 * @note 宏会按 ValOne 表达式的大小调用 XSwap。
 */
#define XSWAP(ValOne, ValTwo) (XSwap(ValOne, ValTwo, sizeof(ValOne)))

/**
 * @brief 按指定字节数交换两个内存对象。
 * @param valOne 第一个对象的起始地址。
 * @param valTwo 第二个对象的起始地址。
 * @param typeSize 每个对象的字节数，必须大于 0。
 * @note 参数无效时函数不执行交换；大对象交换可能临时申请内存。
 */
void XSwap(void* valOne, void* valTwo, const int typeSize);

/**
 * @brief 将栈中的元素按栈底到栈顶的顺序写入向量。
 * @param stack 源栈；元素类型和大小由栈自身记录。
 * @param vector 目标向量；调用前应已按元素类型初始化。
 * @note 函数会先清空 vector；栈和向量都由调用者管理。
 */
void XStackRCopyXVector(const XStack* stack, XVector* vector);

/**
 * @brief 将栈中的元素按栈顶到栈底的顺序写入向量。
 * @param stack 源栈；元素类型和大小由栈自身记录。
 * @param vector 目标向量；调用前应已按元素类型初始化。
 * @note 函数会先清空 vector；栈和向量都由调用者管理。
 */
void XStackCopyXVector(const XStack* stack, XVector* vector);

/**
 * @brief 将 16 位整数转换为指定字节序的数值表示。
 * @param data 待转换的 16 位整数。
 * @param mode 目标字节序：非 0 表示大端序，0 表示小端序。
 * @return 转换后的 16 位整数。
 */
uint16_t SwapEndian16(uint16_t data, uint8_t mode);

#ifdef __cplusplus
}
#endif

#endif /* XALGORITHM_H */

