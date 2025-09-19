#include"CXinYueConfig.h"
#if !defined(XBITARRAY_H) && XBitArray_ON
#define XBITARRAY_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include "XContainerObject.h"

// 虚函数表大小定义，通过基类计算XBitArray所需的虚函数表大小
#define XBITARRAY_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XBitArray))

// 虚函数表枚举（继承XContainerObject）
// 定义XBitArray类的虚函数索引，用于虚函数表中定位具体函数
XCLASS_DEFINE_BEGING(XBitArray)
XCLASS_DEFINE_EXTEND_END(XBitArray, XContainerObject)

// 比特数组结构体
// 继承自XContainerObject基类，实现比特级别的存储与操作
typedef struct XBitArray
{
	XContainerObject m_class; // 继承自容器基类，包含数据、大小、容量等元信息
	XBitOrder m_bitOrder;
} XBitArray;


/**
 * @brief 创建一个初始包含指定比特数的比特数组
 * @param initialBitCount 初始比特数量
 * @return 成功返回创建的XBitArray对象指针，失败返回NULL
 */
XBitArray* XBitArray_create(size_t initialBitCount);

/**
 * @brief 基于另一个比特数组创建一个新的副本
 * @param other 被复制的比特数组
 * @return 成功返回新创建的XBitArray对象指针，失败返回NULL
 */
XBitArray* XBitArray_create_copy(const XBitArray* other);

/**
 * @brief 转移另一个比特数组的资源到新对象（原对象资源会被清空）
 * @param other 被移动的比特数组
 * @return 成功返回新创建的XBitArray对象指针，失败返回NULL
 */
XBitArray* XBitArray_create_move(XBitArray* other);

/**
 * @brief 简化XBitArray_create的调用宏
 * @param initialBits 初始比特数量
 * @return 同XBitArray_create的返回值
 */
#define XBitArray_Create(initialBits) XBitArray_create(initialBits)

/**
 * @brief 初始化指定的XBitArray对象，设置初始比特数
 * @param array 要初始化的XBitArray对象
 * @param initialBitCount 初始比特数量
 */
void XBitArray_init(XBitArray* array, size_t initialBitCount);

/**
 * @brief 设置指定索引的比特值（核心操作基础实现）
 * @param array 比特数组对象
 * @param index 比特索引（从0开始）
 * @param value 要设置的值（true为1，false为0）
 * @return 操作成功返回true，索引越界等失败情况返回false
 */
bool XBitArray_setBit(XBitArray* array, size_t index, bool value);

/**
 * @brief 获取指定索引的比特值（核心操作基础实现）
 * @param array 比特数组对象
 * @param index 比特索引（从0开始）
 * @return 成功返回指定索引的比特值（true为1，false为0），失败返回false
 */
bool XBitArray_getBit(const XBitArray* array, size_t index);

/**
 * @brief 翻转指定索引的比特值（0变1，1变0）（核心操作基础实现）
 * @param array 比特数组对象
 * @param index 比特索引（从0开始）
 * @return 操作成功返回true，索引越界等失败情况返回false
 */
bool XBitArray_toggleBit(XBitArray* array, size_t index);

/**
 * @brief 调整比特数组的大小（核心操作基础实现）
 * @param array 比特数组对象
 * @param newBitCount 新的比特数量
 * @return 调整成功返回true，失败返回false
 */
bool XBitArray_resize(XBitArray* array, size_t newBitCount);

/**
 * @brief 用指定值填充整个比特数组（核心操作基础实现）
 * @param array 比特数组对象
 * @param value 填充值（true为1，false为0）
 */
void XBitArray_fill(XBitArray* array, bool value);

/**
 * @brief 追加另一个比特数组的内容到当前数组（核心操作基础实现）
 * @param array 目标比特数组
 * @param other 要追加的比特数组
 * @return 追加成功返回true，失败返回false
 */
bool XBitArray_append(XBitArray* array, const XBitArray* other);

/**
 * @brief 清除指定索引的比特（设置为0）（核心操作基础实现）
 * @param array 比特数组对象
 * @param index 比特索引（从0开始）
 * @return 操作成功返回true，索引越界等失败情况返回false
 */
bool  XBitArray_clearBits(XBitArray* array, size_t index);
/**
 * @brief 从指定索引开始写入指定比特数的数据（从uint8数组读取）
 * @param array 比特数组对象
 * @param startIndex 起始写入索引（从0开始）
 * @param bitCount 要写入的比特数量
 * @param src 源uint8数组（每个元素包含8个比特）
 * @param srcByteLen 源数组的字节长度（需确保能提供至少bitCount个比特）
 * @return 操作成功返回true，失败返回false（参数无效或空间不足）
 * @note 写入规则：从src[0]的高位到低位依次读取比特，写入到array的startIndex开始位置
 */
bool XBitArray_writeBits(XBitArray* array, size_t startIndex, size_t bitCount, const uint8_t* src, size_t srcByteLen);
/**
 * @brief 从指定索引开始读取指定比特数的数据（写入到uint8数组）
 * @param array 比特数组对象
 * @param startIndex 起始读取索引（从0开始）
 * @param bitCount 要读取的比特数量
 * @param dest 目标uint8数组（存储读取的比特，每个元素包含8个比特）
 * @param destByteLen 目标数组的字节长度（需确保能容纳至少bitCount个比特）
 * @return 操作成功返回true，失败返回false（参数无效或空间不足）
 * @note 读取规则：从array的startIndex开始读取比特，依次写入dest[0]的高位到低位
 */
bool XBitArray_readBits(const XBitArray* array, size_t startIndex, size_t bitCount, uint8_t* dest, size_t destByteLen);
/**
 * @brief 设置比特数组的默认比特序（影响后续读写操作的比特存储模式）
 * @param array 比特数组对象
 * @param bitOrder 要设置的比特序（XBIT_ORDER_MSB_FIRST 或 XBIT_ORDER_LSB_FIRST）
 * @return 操作成功返回true（参数有效），失败返回false（空指针）
 */
bool XBitArray_setBitOrder(XBitArray* array, XBitOrder bitOrder);

/**
 * @brief 获取当前比特数组的默认比特序
 * @param array 比特数组对象
 * @return 当前比特序（默认返回XBIT_ORDER_DEFAULT）
 */
XBitOrder XBitArray_getBitOrder(const XBitArray* array);
/**
 * @brief 获取XBitArray内部存储的原始比特数据指针，对应QBitArray::bits()
 * @param array 目标比特数组对象
 * @return 指向内部字节数组的const char*指针（每个字节存储8个比特），数组为空指针时返回NULL
 * @note 指针有效期与数组对象一致，数组被修改或销毁后指针失效
 * @since 对应Qt 5.11的QBitArray::bits()行为
 */
const char* XBitArray_bits(const XBitArray* array);

/**
 * @brief 截断XBitArray到指定位置，保留0~pos-1的比特，删除pos及之后的比特，对应QBitArray::truncate
 * @param array 目标比特数组对象
 * @param pos 截断位置（从0开始），若为负数则清空数组
 */
void XBitArray_truncate(XBitArray* array, int64_t pos);

/**
 * @brief 复用XContainerObject的复制容器内容实现
 * @param dest 目标容器
 * @param src 源容器
 */
#define XBitArray_copy_base          XContainerObject_copy_base

/**
 * @brief 复用XContainerObject的移动容器资源实现
 * @param dest 目标容器
 * @param src 源容器（资源会被转移）
 */
#define XBitArray_move_base          XContainerObject_move_base

/**
 * @brief 复用XContainerObject的反初始化容器实现
 * @param array 要反初始化的比特数组
 */
#define XBitArray_deinit_base        XContainerObject_deinit_base

/**
* @brief 复用XContainerObject的销毁容器并释放内存实现
* @param array 要销毁的比特数组
*/
#define XBitArray_delete_base        XContainerObject_delete_base

/**
* @brief 复用XContainerObject的判断容器是否为空实现
* @param array 比特数组对象
* @return 容器为空返回true，否则返回false
*/
#define XBitArray_isEmpty_base       XContainerObject_isEmpty_base

/**
* @brief 复用XContainerObject的获取容器中比特数量实现
* @param array 比特数组对象
* @return 容器中当前的比特总数
*/
#define XBitArray_size_base          XContainerObject_size_base

/**
* @brief 复用XContainerObject的获取容器容量（可容纳的比特数）实现
* @param array 比特数组对象
* @return 容器可容纳的最大比特数
*/
#define XBitArray_capacity_base      XContainerObject_capacity_base

/**
* @brief 复用XContainerObject的交换两个容器的内容实现
* @param a 第一个要交换的容器
* @param b 第二个要交换的容器
*/
#define XBitArray_swap_base          XContainerObject_swap_base

/**
* @brief 复用XContainerObject的获取元素类型大小（此处为1字节）实现
* @param array 比特数组对象
* @return 元素类型大小（字节）
*/
#define XBitArray_typeSize_base      XContainerObject_typeSize_base

/**
* @brief 简化设置指定位的调用宏（参考QBitArray风格）
* @param array 比特数组对象
* @param idx 比特索引（从0开始）
* @param val 要设置的值（true为1，false为0）
* @return 同XBitArray_setBit_base的返回值
*/
#define XBitArray_set(array, idx, val) XBitArray_setBit(array, idx, val)

/**
 * @brief 简化获取指定位的值的调用宏（参考QBitArray风格）
 * @param array 比特数组对象
 * @param idx 比特索引（从0开始）
 * @return 同XBitArray_getBit_base的返回值
 */
#define XBitArray_test(array, idx)     XBitArray_getBit(array, idx)

/**
* @brief 简化翻转指定位的调用宏（参考QBitArray风格）
* @param array 比特数组对象
* @param idx 比特索引（从0开始）
* @return 同XBitArray_flip_bit_base的返回值
*/
#define XBitArray_flip(array, idx)     XBitArray_toggleBit(array, idx)

/**
 * @brief 简化获取比特总数的调用宏（参考QBitArray风格）
 * @param array 比特数组对象
 * @return 同XBitArray_size_base的返回值
 */
#define XBitArray_count(array)         XBitArray_size_base(array)
/**
* @brief 简化获取指定位的值的调用宏（参考QBitArray风格）
* @param array 比特数组对象
* @param idx 比特索引（从0开始）
* @return 同XBitArray_getBit_base的返回值
*/
#define XBitArray_at				   XBitArray_getBit
/**
* @brief 初始化XBitArray的虚函数表，注册虚函数实现
* @return 初始化后的XVtable指针
*/
XVtable* XBitArray_class_init();

#ifdef __cplusplus
}
#endif
#endif // XBITARRAY_H