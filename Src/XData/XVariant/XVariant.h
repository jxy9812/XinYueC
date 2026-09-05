#ifndef XVARIANT_H
#define XVARIANT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XClass.h"
#include "XVariantTypeOps.h"
#include"XCompare.h"
#include"XContainer.h"
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>

XCLASS_DEFINE_BEGING(XVariant)
XCLASS_DEFINE_EXTEND_END(XVariant, XClass)

/**
* @brief 变体数据结构，可存储多种类型的数据
*/
typedef struct XVariant
{
	XClass m_class;           ///< 类信息（继承自XClass）
	int m_type;               ///< 数据类型（XVariantType枚举值）
	size_t m_dataSize;        ///< 数据大小（字节数）
	void* m_data;             ///< 数据指针（指向实际存储的数据）
}XVariant;
/**
* @brief 初始化XVariant类的虚函数表
* @return 指向XVariant类虚函数表的指针
*/
XVtable* XVariant_class_init();
/* 创建与初始化函数 */
/**
* @brief 创建一个XVariant数据
* 当传入data参数时，函数会复制数据内容；若传入NULL，则仅分配内存空间，
* 这种方式可配合XVariant_data函数创建自定义数据类型。
* @param data: XVariant内的数据(将会拷贝)
* @param dataSize: 数据大小
* @param type: 数据类型（XVariantType）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_ex(XMemoryType memory,  void* data, size_t dataSize, int type);
/**
* @brief 创建一个XVariant的副本，复制源对象的数据
* @param copy: 待复制的XVariant对象
* @retval 返回新的XVariant副本，失败返回NULL
*/
XVariant* XVariant_create_copy(const XVariant* copy);
/**
* @brief 创建一个XVariant，转移源对象的数据所有权
* @param move: 待转移数据的XVariant对象（转移后源对象数据失效）
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_move(XVariant* move);
/**
* @brief 通过变量直接创建XVariant的宏
* 自动获取变量地址和大小，简化创建流程
* @param data: 变量数据
* @param type: 数据类型（XVariantType）
* @retval 调用XVariant_create创建的XVariant实例
*/
#define XVariant_Create(data,type)    XVariant_create(&data,sizeof(data),type)
/**
* @brief 初始化XVariant对象
* @param var: 待初始化的XVariant指针
* @param data: 初始化数据（会被拷贝）
* @param dataSize: 数据大小
* @param type: 数据类型（XVariantType）
*/
void XVariant_init(XVariant* var, void* data, size_t dataSize, int type);
/**
* @brief 声明并初始化XVariant对象的宏
* 简化局部变量的声明与初始化流程，自动创建临时变量并初始化
* @param var: 变量名（将作为XVariant指针使用）
* @param data: 初始化数据
* @param dataSize: 数据大小
* @param type: 数据类型（XVariantType）
*/
#define XVariant_Init(var,data,dataSize,type)  XVariant _##var,*var=&_##var;XVariant_init(var,data,dataSize,type)
/* 基础数据类型创建函数 */
/**
* @brief 创建一个空类型的XVariant
* @retval 返回空类型XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_null();
/**
* @brief 创建存储8位无符号整数的XVariant
* @param val: 8位无符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_uint8(uint8_t val);
/**
* @brief 创建存储16位无符号整数的XVariant
* @param val: 16位无符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_uint16(uint16_t val);
/**
* @brief 创建存储32位无符号整数的XVariant
* @param val: 32位无符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_uint32(uint32_t val);
/**
* @brief 创建存储64位无符号整数的XVariant
* @param val: 64位无符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_uint64(uint64_t val);
/**
* @brief 创建存储8位有符号整数的XVariant
* @param val: 8位有符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_int8(int8_t val);
/**
* @brief 创建存储16位有符号整数的XVariant
* @param val: 16位有符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_int16(int16_t val);
/**
* @brief 创建存储32位有符号整数的XVariant
* @param val: 32位有符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_int32(int32_t val);
/**
* @brief 创建存储64位有符号整数的XVariant
* @param val: 64位有符号整数数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_int64(int64_t val);
/**
* @brief 创建存储布尔值的XVariant
* @param val: 布尔数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_bool(bool val);
/**
* @brief 创建存储字符的XVariant
* @param val: 字符数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_char(char val);
/**
* @brief 创建存储无符号字符的XVariant
* @param val: 无符号字符数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_uchar(unsigned char val);
/**
* @brief 创建存储int类型的XVariant
* @param val: int数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_int(int val);
/**
* @brief 创建存储size_t类型的XVariant
* @param val: size_t数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_size_t(size_t val);
/**
* @brief 创建存储指针的XVariant
* @param val: 指针数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_ptr(void* val);
/**
* @brief 创建存储单精度浮点数的XVariant
* @param val: 单精度浮点数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_float(float val);
/**
* @brief 创建存储双精度浮点数的XVariant
* @param val: 双精度浮点数据
* @retval 返回新的XVariant实例，失败返回NULL
*/
XVariant* XVariant_create_double(double val);
/* 数据转换函数（获取数据） */
/**
* @brief 获取XVariant中指定类型数据的引用
* @param var: XVariant对象指针
* @param type: 期望的数据类型（XVariantType）
* @retval 指向数据的指针，类型不匹配返回NULL
*/
void* XVariant_toRef(const XVariant* var, XVariantType type);
/**
* @brief 将XVariant中的数据转换为8位无符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的8位无符号整数，类型不匹配返回默认值
*/
uint8_t  XVariant_toUint8(const XVariant* var);
/**
* @brief 获取XVariant中8位无符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向8位无符号整数数据的指针，类型不匹配返回NULL
*/
uint8_t* XVariant_toUint8_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为16位无符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的16位无符号整数，类型不匹配返回默认值
*/
uint16_t XVariant_toUint16(const XVariant* var);
/**
* @brief 获取XVariant中16位无符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向16位无符号整数数据的指针，类型不匹配返回NULL
*/
uint16_t* XVariant_toUint16_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为32位无符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的32位无符号整数，类型不匹配返回默认值
*/
uint32_t XVariant_toUint32(const XVariant* var);
/**
* @brief 获取XVariant中32位无符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向32位无符号整数数据的指针，类型不匹配返回NULL
*/
uint32_t* XVariant_toUint32_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为64位无符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的64位无符号整数，类型不匹配返回默认值
*/
uint64_t XVariant_toUint64(const XVariant* var);
/**
* @brief 获取XVariant中64位无符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向64位无符号整数数据的指针，类型不匹配返回NULL
*/
uint64_t* XVariant_toUint64_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为8位有符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的8位有符号整数，类型不匹配返回默认值
*/
int8_t  XVariant_toInt8(const XVariant* var);
/**
* @brief 获取XVariant中8位有符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向8位有符号整数数据的指针，类型不匹配返回NULL
*/
int8_t* XVariant_toInt8_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为16位有符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的16位有符号整数，类型不匹配返回默认值
*/
int16_t XVariant_toInt16(const XVariant* var);
/**
* @brief 获取XVariant中16位有符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向16位有符号整数数据的指针，类型不匹配返回NULL
*/
int16_t* XVariant_toInt16_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为32位有符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的32位有符号整数，类型不匹配返回默认值
*/
int32_t XVariant_toInt32(const XVariant* var);
/**
* @brief 获取XVariant中32位有符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向32位有符号整数数据的指针，类型不匹配返回NULL
*/
int32_t* XVariant_toInt32_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为64位有符号整数并返回
* @param var: XVariant对象指针
* @retval 转换后的64位有符号整数，类型不匹配返回默认值
*/
int64_t XVariant_toInt64(const XVariant* var);
/**
* @brief 获取XVariant中64位有符号整数数据的指针
* @param var: XVariant对象指针
* @retval 指向64位有符号整数数据的指针，类型不匹配返回NULL
*/
int64_t* XVariant_toInt64_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为布尔值并返回
* @param var: XVariant对象指针
* @retval 转换后的布尔值，类型不匹配返回默认值
*/
bool XVariant_toBool(const XVariant* var);
/**
* @brief 获取XVariant中布尔数据的指针
* @param var: XVariant对象指针
* @retval 指向布尔数据的指针，类型不匹配返回NULL
*/
bool* XVariant_toBool_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为字符并返回
* @param var: XVariant对象指针
* @retval 转换后的字符，类型不匹配返回默认值
*/
char XVariant_toChar(const XVariant* var);
/**
* @brief 获取XVariant中字符数据的指针
* @param var: XVariant对象指针
* @retval 指向字符数据的指针，类型不匹配返回NULL
*/
char* XVariant_toChar_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为无符号字符并返回
* @param var: XVariant对象指针
* @retval 转换后的无符号字符，类型不匹配返回默认值
*/
unsigned char XVariant_toUChar(const XVariant* var);
/**
* @brief 获取XVariant中无符号字符数据的指针
* @param var: XVariant对象指针
* @retval 指向无符号字符数据的指针，类型不匹配返回NULL
*/
unsigned char* XVariant_toUChar_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为int并返回
* @param var: XVariant对象指针
* @retval 转换后的int值，类型不匹配返回默认值
*/
int XVariant_toInt(const XVariant* var);
/**
* @brief 获取XVariant中int数据的指针
* @param var: XVariant对象指针
* @retval 指向int数据的指针，类型不匹配返回NULL
*/
int* XVariant_toInt_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为size_t并返回
* @param var: XVariant对象指针
* @retval 转换后的size_t值，类型不匹配返回默认值
*/
size_t XVariant_toSize_t(const XVariant* var);
/**
* @brief 获取XVariant中size_t数据的指针
* @param var: XVariant对象指针
* @retval 指向size_t数据的指针，类型不匹配返回NULL
*/
size_t* XVariant_toSize_t_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为指针并返回
* @param var: XVariant对象指针
* @retval 转换后的指针，类型不匹配返回NULL
*/
void* XVariant_toPtr(const XVariant* var);
/**
* @brief 获取XVariant中指针数据的指针
* @param var: XVariant对象指针
* @retval 指向指针数据的二级指针，类型不匹配返回NULL
*/
void** XVariant_toPtr_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为单精度浮点数并返回
* @param var: XVariant对象指针
* @retval 转换后的单精度浮点数，类型不匹配返回默认值
*/
float XVariant_toFloat(const XVariant* var);
/**
* @brief 获取XVariant中单精度浮点数数据的指针
* @param var: XVariant对象指针
* @retval 指向单精度浮点数数据的指针，类型不匹配返回NULL
*/
float* XVariant_toFloat_ref(const XVariant* var);
/**
* @brief 将XVariant中的数据转换为双精度浮点数并返回
* @param var: XVariant对象指针
* @retval 转换后的双精度浮点数，类型不匹配返回默认值
*/
double XVariant_toDouble(const XVariant* var);
/**
* @brief 获取XVariant中双精度浮点数数据的指针
* @param var: XVariant对象指针
* @retval 指向双精度浮点数数据的指针，类型不匹配返回NULL
*/
double* XVariant_toDouble_ref(const XVariant* var);
/* 数据设置函数 */
/**
* @brief 将XVariant的值设置为另一个XVariant的值（复制数据）
* @param var: 目标XVariant指针
* @param newVar: 源XVariant指针（提供新值）
*/
void XVariant_setValue(XVariant* var, const XVariant* newVar);
/**
* @brief 将XVariant设置为空类型
* @param var: 目标XVariant指针
*/
void XVariant_setValue_null(XVariant* var);
/**
* @brief 设置XVariant的值为8位无符号整数
* @param var: 目标XVariant指针
* @param val: 8位无符号整数数据
*/
void XVariant_setValue_uint8(XVariant* var, uint8_t val);
/**
* @brief 设置XVariant的值为16位无符号整数
* @param var: 目标XVariant指针
* @param val: 16位无符号整数数据
*/
void XVariant_setValue_uint16(XVariant* var, uint16_t val);
/**
* @brief 设置XVariant的值为32位无符号整数
* @param var: 目标XVariant指针
* @param val: 32位无符号整数数据
*/
void XVariant_setValue_uint32(XVariant* var, uint32_t val);
/**
* @brief 设置XVariant的值为64位无符号整数
* @param var: 目标XVariant指针
* @param val: 64位无符号整数数据
*/
void XVariant_setValue_uint64(XVariant* var, uint64_t val);
/**
* @brief 设置XVariant的值为8位有符号整数
* @param var: 目标XVariant指针
* @param val: 8位有符号整数数据
*/
void XVariant_setValue_int8(XVariant* var, int8_t val);
/**
* @brief 设置XVariant的值为16位有符号整数
* @param var: 目标XVariant指针
* @param val: 16位有符号整数数据
*/
void XVariant_setValue_int16(XVariant* var, int16_t val);
/**
* @brief 设置XVariant的值为32位有符号整数
* @param var: 目标XVariant指针
* @param val: 32位有符号整数数据
*/
void XVariant_setValue_int32(XVariant* var, int32_t val);
/**
* @brief 设置XVariant的值为64位有符号整数
* @param var: 目标XVariant指针
* @param val: 64位有符号整数数据
*/
void XVariant_setValue_int64(XVariant* var, int64_t val);
/** @brief 设置 size_t 值。 @param var 目标变体；不能为 NULL。 @param val size_t 值。 @return 无；内存不足时保留原值。 */
void XVariant_setValue_size_t(XVariant* var, size_t val);
/** @brief 设置指针值。 @param var 目标变体；不能为 NULL。 @param val 指针值；不转移所指对象所有权。 @return 无。 */
void XVariant_setValue_ptr(XVariant* var, void* val);
/** @brief 设置单精度浮点值。 @param var 目标变体；不能为 NULL。 @param val 浮点值。 @return 无；内存不足时保留原值。 */
void XVariant_setValue_float(XVariant* var, float val);
/** @brief 设置双精度浮点值。 @param var 目标变体；不能为 NULL。 @param val 浮点值。 @return 无；内存不足时保留原值。 */
void XVariant_setValue_double(XVariant* var, double val);
/**
* @brief 设置XVariant的值为布尔值
* @param var: 目标XVariant指针
* @param val: 布尔数据
*/
void XVariant_setValue_bool(XVariant* var, bool val);
/**
* @brief 设置XVariant的值为字符
* @param var: 目标XVariant指针
* @param val: 字符数据
*/
void XVariant_setValue_char(XVariant* var, char val);
/**
* @brief 设置XVariant的值为无符号字符
* @param var: 目标XVariant指针
* @param val: 无符号字符数据
*/
void XVariant_setValue_uchar(XVariant* var, unsigned char val);
/**
* @brief 设置XVariant的值为int类型
* @param var: 目标XVariant指针
* @param val: int数据
*/
void XVariant_setValue_int(XVariant* var, int val);
/**
 * @brief 检查XVariant是否有效（非空且类型不为NULL）
 * @param var XVariant对象指针
 * @return 有效返回true，无效返回false
 */
bool XVariant_isValid(const XVariant* var);
/** @brief 清空变体并恢复 NULL 类型。 @param var 变体对象；不能为 NULL。 @return 无；原有数据释放。 */
void XVariant_clear(XVariant* var);
/** @brief 交换两个变体内容。 @param var 左变体；不能为 NULL。 @param other 右变体；不能为 NULL。 @return 无。 */
void XVariant_swap(XVariant* var, XVariant* other);
/** @brief 获取变体类型。 @param var 变体对象；可为 NULL。 @return XVariantType 编号；NULL 返回 XVariantType_NULL。 */
int XVariant_type(XVariant* var);
/** @brief 获取变体类型名称。 @param var 变体对象；可为 NULL。 @return 库静态持有的 UTF-8 名称；调用者不得释放。 */
const char* XVariant_typeName(XVariant* var);
/** @brief 比较两个变体。 @param var 左变体；可为 NULL。 @param cmp 右变体；可为 NULL。 @return 按库比较约定返回负数、0 或正数。 */
int32_t XVariant_compare(XVariant* var, XVariant* cmp);
/** @brief 获取内部数据指针。 @param var 变体对象；可为 NULL。 @return 内部数据借用指针；不得释放，变体重设或销毁后失效。 */
void* XVariant_data(XVariant* var);
/** @brief 获取内部数据大小。 @param var 变体对象；可为 NULL。 @return 数据字节数；NULL 返回 0。 */
size_t XVariant_dataSize(XVariant* var);
/**
 * @brief 将外部数据对象直接交给 Variant 管理。
 * @param var 目标 Variant；不能为 NULL。
 * @param data 外部数据对象；成功后由 Variant 负责其生命周期。
 * @param dataSize 数据大小，单位为字节。
 * @param type 数据类型。
 */
void XVariant_setDataRef(XVariant* var, void* data, size_t dataSize, int type);
/** @brief 反初始化变体并释放其内部数据。 @param var 变体对象；不能为 NULL。 @return 无。 */
#define XVariant_deinit_base		XClass_deinit_base
/** @brief 释放由 XVariant_create 系列函数返回的变体。 @param var 变体对象所有权。 @return 无。 */
#define XVariant_delete_base		XClass_delete_base
/** @brief 取出变体内部数据并按指定类型解引用；仅适用于类型已确认的对象。 */
#define XVariant_Value(Var,Type)   (*((Type*)XVariant_data(Var)))
#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XVariant_create
#define XVariant_create(...) XVariant_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XVARIANT_H
