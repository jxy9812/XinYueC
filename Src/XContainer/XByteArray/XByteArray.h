///**
//* @file XByteArray.h
//* @brief 字节数组容器头文件
//* @details 定义了基于XVector的字节数组容器XByteArray，专门用于存储和操作uint8_t类型数据.
//* 提供字节级别的添加、删除、查找、编码转换（16进制、Base64）、压缩解压等功能,
//* 继承自XVector以复用动态数组的基础管理能力。
//*/
#include"CXinYueConfig.h"///< 项目配置文件，控制XByteArray模块是否启用
#if !defined(XBYTEARRAY_H)&& XByteArray_ON
#define XBYTEARRAY_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XVector.h"                  ///< 向量容器头文件，XByteArray继承自XVector
#include "XByteArray_iterator.h"      ///< XByteArray正向迭代器定义
#include "XByteArray_reverse_iterator.h"  ///< XByteArray反向迭代器定义
/**
* @brief 字节数组容器结构体
* @details 继承自XVector，专门用于存储uint8_t（字节）类型数据。
*          复用XVector的动态数组管理能力（数据存储、大小、容量等），
*          同时扩展字节特有的操作（如编码转换、压缩等）。
*/
typedef struct XByteArray
{
	XVector m_class;  ///< 继承自XVector基类，包含数据存储、大小、容量等核心成员
} XByteArray;
//============================= 创建与初始化 =============================

/**
* @brief 创建指定初始大小的XByteArray实例
* @param size 初始字节数量（若为0则创建空数组）
* @return 成功返回XByteArray实例指针，失败返回NULL（内存分配失败）
*/
XByteArray* XByteArray_create_ex(bool useCow);
#define XByteArray_create() XByteArray_create_ex(true)
XByteArray* XByteArray_create_with_data(const char* data,size_t size);
//用字符串创建
XByteArray* XByteArray_create_utf8(const char* utf8);
/**
* @brief 基于已有XByteArray创建深拷贝实例
* @param other 被复制的XByteArray实例指针（不可为NULL）
* @return 成功返回新的XByteArray实例指针，失败返回NULL（参数无效或内存分配失败）
*/
XByteArray* XByteArray_create_copy(const XByteArray* other);

/**
* @brief 转移已有XByteArray的资源（移动构造）
* @param other 被移动的XByteArray实例指针（不可为NULL）
* @return 成功返回新的XByteArray实例指针，失败返回NULL（参数无效或内存分配失败）
* @note 资源转移后，原实例other会被清空
*/
XByteArray* XByteArray_create_move(XByteArray* other);

/**
* @brief 初始化XByteArray实例
* @param array 待初始化的XByteArray实例指针（需提前分配内存，不可为NULL）
* @details 初始化内部继承的XVector结构，设置元素类型为uint8_t并配置比较函数
*/
void XByteArray_init(XByteArray* array, bool useCow);


//============================= 元素添加 =============================

/**
* @brief 在字节数组头部添加一个字节
* @param array 目标XByteArray实例指针（不可为NULL）
* @param byte 待添加的字节（uint8_t类型）
* @return 添加成功返回true，失败返回false（参数无效或扩容失败）
* @note 原数据会依次后移，可能触发容器扩容
*/
bool XByteArray_push_front_1(XByteArray* array, const uint8_t byte);
#define XByteArray_push_front_2						XVector_push_front_2
#define XByteArray_push_front_3						XVector_push_front_3
/**
* @brief 在字节数组尾部添加一个字节
* @param array 目标XByteArray实例指针（不可为NULL）
* @param byte 待添加的字节（uint8_t类型）
* @return 添加成功返回true，失败返回false（参数无效或扩容失败）
*/
bool XByteArray_push_back_1(XByteArray* array, const uint8_t byte);
#define XByteArray_push_back_2							XVector_push_back_2
#define XByteArray_push_back_3							XVector_push_back_3

/**
* @brief 在指定索引位置插入n个相同字节
* @param array 目标XByteArray实例指针（不可为NULL）
* @param index 插入位置索引（支持负数）
* @param byte 待插入的字节值
* @param n 插入的字节数量（需大于0）
* @return 插入成功返回true，失败返回false（参数无效、索引越界或扩容失败）
*/
bool XByteArray_insert_1_base(XByteArray* array, int64_t index, uint8_t byte, size_t n);
/**
* @brief 在指定索引位置插入一个字节
* @param array 目标XByteArray实例指针（不可为NULL）
* @param index 插入位置索引（支持负数，-1表示末尾）
* @param byte 待插入的字节（uint8_t类型）
* @return 插入成功返回true，失败返回false（参数无效、索引越界或扩容失败）
* @note 原index及之后的数据会后移
*/
bool XByteArray_insert_2(XByteArray* array, int64_t index, const uint8_t byte);
#define XByteArray_insert_3						XVector_insert_3	
/**
* @brief 追加UTF8字符串到字节数组（不含终止符'\0'）
* @param array 目标XByteArray实例指针（不可为NULL）
* @param utf8 待追加的UTF8字符串指针（不可为NULL且长度需大于0）
* @return 追加成功返回true，失败返回false（参数无效或字符串长度为0）
*/
bool XByteArray_append_utf8(XByteArray* array, const char* utf8);

#define XByteArray_prepend_1							XByteArray_push_front_1
#define XByteArray_prepend_2							XByteArray_push_front_2
#define XByteArray_prepend_3							XByteArray_push_front_3
#define XByteArray_append_1								XByteArray_push_back_1
#define XByteArray_append_2								XByteArray_push_back_2
#define XByteArray_append_3								XByteArray_push_back_3

//============================= 元素删除 =============================

/**
* @brief 复用XVector的接口，删除头部第一个元素
*/
#define XByteArray_pop_front_base					XVector_pop_front_base

/**
* @brief 复用XVector的接口，删除尾部最后一个元素
*/
#define XByteArray_pop_back_base					XVector_pop_back_base

/**
* @brief 复用XVector的接口，删除指定指针位置的元素
* @note 指针需指向容器内的有效元素
*/
#define XByteArray_erase_base						XVector_erase_base

/**
* @brief 复用XVector的接口，删除指定范围的元素
* @param n 若n<0则删除index之后的所有元素，否则删除index开始的n个元素
*/
#define XByteArray_remove_base						XVector_remove_base

/**
* @brief 复用XVector的接口，清空数组所有元素（保留容量）
*/
#define XByteArray_clear_base						XVector_clear_base


//============================= 容器调整 =============================

/**
* @brief 复用XVector的接口，调整字节数组大小
* @details 若新大小大于当前容量则扩容，多余空间用默认值填充
*/
#define XByteArray_resize_base						XVector_resize_base

/**
* @brief 复用XVector的接口，在指定位置插入另一个数组的指定范围数据
*/
#define XByteArray_insert_array_base				XVector_insert_1_base


//============================= 元素访问与查询 =============================
uint8_t* XByteArray_data(XByteArray* other);
/**
* @brief 复用XVector的接口，获取指定索引的元素指针
* @return 成功返回元素指针，失败返回NULL（索引越界）
*/
#define XByteArray_at_base(array,index)						XVector_At_Base(array,index,uint8_t)

/**
* @brief 复用XVector的接口，获取头部第一个元素的指针
* @return 成功返回指针，数组为空时返回NULL
*/
#define XByteArray_front_base(array)						XVector_Front_Base(array,uint8_t)

/**
* @brief 复用XVector的接口，获取尾部最后一个元素的指针
* @return 成功返回指针，数组为空时返回NULL
*/
#define XByteArray_back_base(array)							XVector_Back_Base(array,uint8_t)

/**
* @brief 在字节数组中查找指定字节
* @param array 目标XByteArray实例指针（不可为NULL）
* @param findVal 待查找的字节值
* @param it 输出参数，用于存储找到的位置的迭代器（未找到时内容不确定）
* @return 找到返回true，未找到或参数无效返回false
*/
bool XByteArray_find_base(const XByteArray* array, const uint8_t findVal, XByteArray_iterator* it);

/**
* @brief 复用XVector的接口，统计指定元素的出现次数
* @return 返回元素在数组中出现的次数
*/
#define XByteArray_count_base						XVector_count_base

/**
* @brief 复用XVector的接口，判断数组是否为空
* @return 为空返回true，否则返回false
*/
#define XByteArray_isEmpty_base						XVector_isEmpty_base

/**
* @brief 复用XVector的接口，获取数组当前元素数量（字节数）
* @return 返回数组中的字节总数
*/
#define XByteArray_size_base						XVector_size_base

/**
* @brief 复用XVector的接口，获取数组当前容量（可容纳的最大字节数）
* @return 返回数组的容量
*/
#define XByteArray_capacity_base					XVector_capacity_base


//============================= 容器操作 =============================

/**
* @brief 复用XVector的接口，复制另一个数组的内容到当前数组
* @param this_One 目标数组
* @param this_Two 源数组（被复制的数组）
*/
#define XByteArray_copy_base						XVector_copy_base

/**
* @brief 复用XVector的接口，逆序复制另一个数组的内容到当前数组
* @param this_One 目标数组
* @param this_Two 源数组（被复制的数组）
*/
#define XByteArray_rcopy_base						XVector_rcopy_base

/**
* @brief 复用XVector的接口，转移另一个数组的资源到当前数组
* @param this_One 目标数组
* @param this_Two 源数组（资源被转移后会被清空）
*/
#define XByteArray_move_base						XVector_move_base

/**
* @brief 复用XVector的接口，交换两个数组的内容
* @param a 第一个数组
* @param b 第二个数组
*/
#define XByteArray_swap_base						XVector_swap_base

/**
* @brief 复用XVector的接口，获取元素类型大小（uint8_t为1字节）
* @return 返回1（字节）
*/
#define XByteArray_typeSize_base					XVector_typeSize_base

/**
* @brief 比较两个字节数组
* @param lhs 左操作数数组
* @param rhs 右操作数数组
* @return 若lhs < rhs返回XCompare_Less；lhs > rhs返回XCompare_Greater；相等返回XCompare_Equality
*/
int32_t XByteArray_compare(const XByteArray* lhs, const XByteArray* rhs);

/**
 * @brief 深复制创建存储 XByteArray 的 XVariant。
 * @param array 源字节数组，借用，可为 NULL。
 * @return 新建并由调用者负责释放的 XVariant，失败返回 NULL。
 */
XVariant* XByteArray_toVariant(const XByteArray* array);
/**
 * @brief 移动创建存储 XByteArray 的 XVariant。
 * @param array 源字节数组；成功后由 XVariant 接管其资源。
 * @return 新建并由调用者负责释放的 XVariant，失败返回 NULL。
 */
XVariant* XByteArray_toVariant_move(XByteArray* array);
/**
 * @brief 将已有 XByteArray 直接交给 XVariant 管理。
 * @param array 源字节数组；成功后由 XVariant 负责其生命周期。
 * @return 新建并由调用者负责释放的 XVariant，失败返回 NULL。
 */
XVariant* XByteArray_toVariant_ref(XByteArray* array);
/**
 * @brief 从 XVariant 深复制取得 XByteArray。
 * @param var 源 XVariant；借用，可为 NULL。
 * @return 新建并由调用者负责释放的 XByteArray，类型不匹配或失败返回 NULL。
 */
XByteArray* XByteArray_fromVariant(const XVariant* var);
/**
 * @brief 从 XVariant 借用取得 XByteArray。
 * @param var 源 XVariant；借用，可为 NULL。
 * @return XVariant 内部 XByteArray 指针；调用者不得释放，类型不匹配返回 NULL。
 */
XByteArray* XByteArray_fromVariant_ref(const XVariant* var);
/**
 * @brief 深复制设置 XVariant 的 XByteArray 值。
 * @param var 目标 XVariant；不能为 NULL。
 * @param array 源字节数组；借用，可为 NULL。
 */
void XByteArray_setVariant(XVariant* var, const XByteArray* array);
/**
 * @brief 移动设置 XVariant 的 XByteArray 值。
 * @param var 目标 XVariant；不能为 NULL。
 * @param array 源字节数组；成功后由 XVariant 接管其资源。
 */
void XByteArray_setVariant_move(XVariant* var, XByteArray* array);
/**
 * @brief 将已有 XByteArray 直接交给 XVariant 管理。
 * @param var 目标 XVariant；不能为 NULL。
 * @param array 源字节数组；成功后由 XVariant 负责其生命周期。
 */
void XByteArray_setVariant_ref(XVariant* var, XByteArray* array);
/**
 * @brief 从原始字节数据设置 XVariant 的 XByteArray 值。
 * @param var 目标 XVariant；不能为 NULL。
 * @param data 原始字节数据；借用，可为 NULL。
 * @param size 数据长度，单位为字节。
 */
void XByteArray_setVariant_data(XVariant* var, const void* data, size_t size);
/**
 * @brief 从原始字节数据创建 XByteArray 类型的 XVariant。
 * @param data 原始字节数据；借用，可为 NULL。
 * @param size 数据长度，单位为字节。
 * @return 新建并由调用者负责释放的 XVariant，失败返回 NULL。
 */
XVariant* XByteArray_toVariant_data(const void* data, size_t size);

/**
 * @brief 兼容旧的 XVariant 扩展 API 名称。
 * @details 以下宏仅保留源代码兼容性，实际实现均归属 XByteArray。
 */
/** @brief 兼容旧的深复制创建 ByteArray Variant 接口。 */
#define XVariant_create_ByteArray        XByteArray_toVariant
/** @brief 兼容旧的移动创建 ByteArray Variant 接口。 */
#define XVariant_create_ByteArray_move   XByteArray_toVariant_move
/** @brief 兼容旧的直接交给 Variant 管理的 ByteArray 创建接口。 */
#define XVariant_create_ByteArray_ref    XByteArray_toVariant_ref
/** @brief 兼容旧的原始字节创建 ByteArray Variant 接口。 */
#define XVariant_create_byteArray        XByteArray_toVariant_data
/** @brief 兼容旧的深复制取得 ByteArray 接口。 */
#define XVariant_toByteArray             XByteArray_fromVariant
/** @brief 兼容旧的借用取得 ByteArray 接口。 */
#define XVariant_toByteArray_ref         XByteArray_fromVariant_ref
/** @brief 兼容旧的深复制设置 ByteArray Variant 接口。 */
#define XVariant_setValue_ByteArray      XByteArray_setVariant
/** @brief 兼容旧的移动设置 ByteArray Variant 接口。 */
#define XVariant_setValue_ByteArray_move XByteArray_setVariant_move
/** @brief 兼容旧的直接交给 Variant 管理的 ByteArray 设置接口。 */
#define XVariant_setValue_ByteArray_ref  XByteArray_setVariant_ref
/** @brief 兼容旧的原始字节设置 ByteArray Variant 接口。 */
#define XVariant_setValue_byteArray      XByteArray_setVariant_data


//============================= 编码与转换 =============================

/**
* @brief 将字节数组转换为16进制UTF8字符串（字节数组形式）
* @param array 目标XByteArray实例指针（不可为NULL且非空）
* @return 成功返回包含16进制字符串的XByteArray实例，失败返回NULL
*/
XByteArray* XByteArray_to16HexUtf8(XByteArray* array);

/**
* @brief 将字节数组转换为16进制字符串（XString形式）
* @param array 目标XByteArray实例指针（不可为NULL且非空）
* @return 成功返回包含16进制字符串的XString实例，失败返回NULL
*/
XString* XByteArray_to16HexString(XByteArray* array);

/**
* @brief 将字节数组转换为Base64编码的字节数组
* @param array 目标XByteArray实例指针（不可为NULL且非空）
* @return 成功返回Base64编码的XByteArray实例，失败返回NULL
*/
XByteArray* XByteArray_toBase64(XByteArray* array);

/**
* @brief 将Base64编码的字节数组解码为原始字节数组
* @param base64 包含Base64编码数据的XByteArray实例（不可为NULL且非空）
* @return 成功返回解码后的原始XByteArray实例，失败返回NULL
*/
XByteArray* XByteArray_fromBase64(XByteArray* base64);


//============================= 压缩与解压 =============================

/**
* @brief 对字节数组进行压缩
* @param sData 待压缩的字节数组（不可为NULL且非空）
* @return 成功返回压缩后的XByteArray实例，失败返回NULL
*/
XByteArray* XByteArray_toCompress(XByteArray* sData);

/**
* @brief 对字节数组进行解压
* @param sData 待解压的字节数组（不可为NULL且非空）
* @return 成功返回解压后的XByteArray实例，失败返回NULL
*/
XByteArray* XByteArray_toDecompress(XByteArray* sData);


//============================= 销毁与清理 =============================

/**
* @brief 复用XVector的接口，反初始化数组（释放内部资源，保留实例本身）
* @param array 目标XByteArray实例指针
*/
#define XByteArray_deinit_base						XVector_deinit_base

/**
* @brief 复用XVector的接口，销毁数组并释放所有内存
* @param array 目标XByteArray实例指针（可NULL，NULL时不操作）
*/
#define XByteArray_delete_base						XVector_delete_base



//============================= Qt 6.8 命名对齐 =============================
/* 说明：XByteArray 内部继承自 XVector，XVector 已提供大部分与 Qt QByteArray 同语义的
 *       API（reserve/squeeze/indexOf/lastIndexOf/startsWith/endsWith/removeIf/removeAt 等）。
 *       此处以宏别名的方式将 XByteArray 的对外接口对齐 Qt 6.8 QByteArray 命名，
 *       同时补齐 fill / truncate / chop / left / right / mid / sliced 等轻量操作。
 *       参考: qtbase/src/corelib/text/qbytearray.h (QByteArray)
 */

/**
* @brief 元素个数（字节数）——Qt 别名
* @note Qt 映射: QByteArray::length()（与 size() 等价）
*/
#define XByteArray_length_base            XByteArray_size_base

/**
* @brief 判空——Qt 别名
* @note Qt 映射: QByteArray::empty()（与 isEmpty() 等价）
*/
#define XByteArray_empty_base             XByteArray_isEmpty_base

/**
* @brief 只读原始数据指针
* @note Qt 映射: QByteArray::constData()（本项目共用 data，故仅命名别名）
*/
#define XByteArray_constData              XByteArray_data

/**
* @brief 预留容量——对齐 Qt QByteArray::reserve
* @param array XByteArray 实例指针
* @param size  期望容量下限
* @return 成功返回 true，失败返回 false
*/
#define XByteArray_reserve_base           XVector_reserve_base

/**
* @brief 释放多余容量——对齐 Qt QByteArray::squeeze
*/
#define XByteArray_squeeze_base           XVector_squeeze_base

/**
* @brief 语义等价的 shrink_to_fit——STL 别名
*/
#define XByteArray_shrink_to_fit          XVector_squeeze_base

/**
* @brief 判断是否包含指定字节——对齐 Qt QByteArray::contains(char)
* @param array XByteArray 实例指针
* @param byte  待查找字节（uint8_t）
* @return 存在返回 true
*/
#define XByteArray_contains(array, byte)  XVector_contains(array, &(uint8_t){(byte)})

/**
* @brief 查找指定字节首次出现位置——对齐 Qt QByteArray::indexOf(char, qsizetype from)
* @param array XByteArray 实例指针
* @param byte  待查找字节（uint8_t）
* @param from  起始索引
* @return 找到返回索引，找不到返回 -1
*/
#define XByteArray_indexOf(array, byte, from)     XVector_indexOf((const XVector*)(array), &(uint8_t){(byte)}, (from))

/**
* @brief 查找指定字节最后一次出现位置——对齐 Qt QByteArray::lastIndexOf
* @param from 反向起始索引，-1 表示从末尾开始
*/
#define XByteArray_lastIndexOf(array, byte, from)     XVector_lastIndexOf((const XVector*)(array), &(uint8_t){(byte)}, (from))

/**
* @brief 判断是否以指定字节开头——对齐 Qt QByteArray::startsWith(char)
*/
#define XByteArray_startsWith(array, byte)     XVector_startsWith((const XVector*)(array), &(uint8_t){(byte)})

/**
* @brief 判断是否以指定字节结尾——对齐 Qt QByteArray::endsWith(char)
*/
#define XByteArray_endsWith(array, byte)     XVector_endsWith((const XVector*)(array), &(uint8_t){(byte)})

/**
* @brief 按谓词条件删除元素——对齐 Qt QByteArray::removeIf
* @param array XByteArray 实例指针
* @param pred  谓词函数（XEquality 类型: (const void* pv, const void* userData)->bool）
* @param userData 透传参数
* @return 被删除的字节数
*/
#define XByteArray_removeIf(array, pred, userData)     XVector_removeIf((XVector*)(array), (pred), (userData))

/**
* @brief 删除指定索引处单个字节——对齐 Qt QByteArray::removeAt
*/
#define XByteArray_removeAt_base(array, index)     XVector_removeAt_base((XVector*)(array), (index))

/**
* @brief 删除首字节——对齐 Qt QByteArray::removeFirst
*/
#define XByteArray_removeFirst_base       XByteArray_pop_front_base

/**
* @brief 删除尾字节——对齐 Qt QByteArray::removeLast
*/
#define XByteArray_removeLast_base        XByteArray_pop_back_base

/**
* @brief 十六进制字符串——对齐 Qt QByteArray::toHex
* @note 本项目原名 to16HexUtf8；此为别名。
*/
#define XByteArray_toHex                  XByteArray_to16HexUtf8

/**
* @brief 以指定字节值填充/重置数组——对齐 Qt QByteArray::fill
* @param array XByteArray 实例指针
* @param byte  填充字节
* @param size  若 >=0 则先 resize 到该大小；<0 保持当前大小
* @return 成功返回 array，失败返回 NULL
* @note Qt 映射: QByteArray::fill(char c, qsizetype size = -1)
*/
XByteArray* XByteArray_fill(XByteArray* array, uint8_t byte, int64_t size);

/**
* @brief 截断到指定长度——对齐 Qt QByteArray::truncate
* @param array XByteArray 实例指针
* @param pos   保留前 pos 个字节，超出部分被丢弃；pos<=0 时清空；pos>=size 时无操作
* @note Qt 映射: QByteArray::truncate(qsizetype pos)
*/
void XByteArray_truncate(XByteArray* array, int64_t pos);

/**
* @brief 从尾部丢弃 n 个字节——对齐 Qt QByteArray::chop
* @param array XByteArray 实例指针
* @param n     待丢弃字节数；n<=0 无操作；n>=size 时清空
* @note Qt 映射: QByteArray::chop(qsizetype n)
*/
void XByteArray_chop(XByteArray* array, int64_t n);

/**
* @brief 返回包含前 n 个字节的新数组——对齐 Qt QByteArray::left / first
* @param array 源数组
* @param n     取前 n 字节；n<0 或 n>size 时视为 size
* @return 新 XByteArray；调用方负责释放
* @note Qt 映射: QByteArray::left(qsizetype)/QByteArray::first(qsizetype)
*/
XByteArray* XByteArray_left(const XByteArray* array, int64_t n);

/**
* @brief 返回包含后 n 个字节的新数组——对齐 Qt QByteArray::right / last
* @note Qt 映射: QByteArray::right(qsizetype)/QByteArray::last(qsizetype)
*/
XByteArray* XByteArray_right(const XByteArray* array, int64_t n);

/**
* @brief 返回从 pos 起、长度 n 的子数组——对齐 Qt QByteArray::mid / sliced
* @param pos 起始索引（0 起）
* @param n   长度；n<0 表示到结尾
* @note Qt 映射: QByteArray::mid(qsizetype pos, qsizetype len=-1)/QByteArray::sliced()
*/
XByteArray* XByteArray_mid(const XByteArray* array, int64_t pos, int64_t n);

/** @brief Qt QByteArray::first(n) 别名，等价于 XByteArray_left */
#define XByteArray_first             XByteArray_left
/** @brief Qt QByteArray::last(n) 别名，等价于 XByteArray_right */
#define XByteArray_last              XByteArray_right
/** @brief Qt QByteArray::sliced(pos, n) 别名，等价于 XByteArray_mid */
#define XByteArray_sliced            XByteArray_mid


/* ============================== Qt 6.8 命名对齐 (重量项): replace/split/simplified/trimmed/toUpper/toLower/toInt/toDouble/setNum/percentEncoding/compare ============================== */

/**
* @brief 按字节序列查找/替换——对齐 Qt QByteArray::replace(const char*, qsizetype, const char*, qsizetype)
* @param array   原地修改的目标数组
* @param before  待查找子串（可为空指针配合 beforeLen=0）
* @param beforeLen 待查找子串长度
* @param after   替换子串
* @param afterLen 替换子串长度
* @return 已替换次数
*/
size_t XByteArray_replace(XByteArray* array,
    const uint8_t* before, size_t beforeLen,
    const uint8_t* after,  size_t afterLen);

/**
* @brief 按单字节分隔符切分——对齐 Qt QByteArray::split(char)
* @param array 源数组
* @param sep   分隔字节
* @return 新 XVector（元素为 XByteArray*，调用方负责释放每个元素及本 XVector）
* @note 使用 XByteArray_split_free 一次性释放
*/
XVector* XByteArray_split(const XByteArray* array, uint8_t sep);

/**
* @brief 释放 XByteArray_split 的返回值
*/
void XByteArray_split_free(XVector* parts);

/**
* @brief 首尾空白裁剪——对齐 Qt QByteArray::trimmed()
* @return 新 XByteArray；调用方负责释放
* @note 空白: ' ' '\\t' '\\n' '\\r' '\\v' '\\f'
*/
XByteArray* XByteArray_trimmed(const XByteArray* array);

/**
* @brief 首尾裁剪+中间连续空白合并为单空格——对齐 Qt QByteArray::simplified()
*/
XByteArray* XByteArray_simplified(const XByteArray* array);

/**
* @brief 就地全大写（ASCII 范围）——对齐 Qt QByteArray::toUpper()
*/
XByteArray* XByteArray_toUpper(XByteArray* array);

/**
* @brief 就地全小写（ASCII 范围）——对齐 Qt QByteArray::toLower()
*/
XByteArray* XByteArray_toLower(XByteArray* array);

/**
* @brief 解析为 int64——对齐 Qt QByteArray::toLongLong(bool* ok, int base)
* @param base 进制，0 表示自动（0x/0/十进制），2..36 有效
* @param ok   可选输出，成功为 true
*/
int64_t XByteArray_toLongLong(const XByteArray* array, bool* ok, int base);

/** @brief QByteArray::toInt */
int32_t XByteArray_toInt(const XByteArray* array, bool* ok, int base);

/** @brief QByteArray::toDouble */
double  XByteArray_toDouble(const XByteArray* array, bool* ok);

/**
* @brief 覆盖为数值的十进制/指定进制字符串——对齐 Qt QByteArray::setNum(qlonglong, int base)
*/
XByteArray* XByteArray_setNum_i64(XByteArray* array, int64_t value, int base);

/** @brief QByteArray::setNum(int, base) */
XByteArray* XByteArray_setNum_i32(XByteArray* array, int32_t value, int base);

/** @brief QByteArray::setNum(double, char f='g', int prec=6) */
XByteArray* XByteArray_setNum_double(XByteArray* array, double value, char fmt, int prec);

/**
* @brief 百分号编码（URL 编码）——对齐 Qt QByteArray::toPercentEncoding()
* @param array 源数据
* @return 新 XByteArray，调用方负责释放
*/
XByteArray* XByteArray_toPercentEncoding(const XByteArray* array);

/** @brief 百分号解码——对齐 Qt QByteArray::fromPercentEncoding() */
XByteArray* XByteArray_fromPercentEncoding(const XByteArray* array);

/**
* @brief 大小写敏感比较（对齐 Qt QByteArrayView 语义）——对齐 QByteArray::compare(view, Qt::CaseSensitivity)
* @param cs 0=不敏感 1=敏感
* @return <0 / 0 / >0
*/
int32_t XByteArray_compareCS(const XByteArray* lhs, const XByteArray* rhs, int cs);

/** @brief 大小写不敏感比较别名 */
#define XByteArray_compareCI(lhs, rhs)    XByteArray_compareCS((lhs), (rhs), 0)

#ifdef __cplusplus
}
#endif
#endif // !VECTOR_H
