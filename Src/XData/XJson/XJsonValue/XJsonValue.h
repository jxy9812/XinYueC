#ifndef XJSONVALUE_H
#define XJSONVALUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XJson.h"
#include "XString.h"
#include "XVariant.h"
#include <stdbool.h>
#include <stdint.h>
#if !XString_ON || !XVariant_ON
#error "XJsonValue requires XString and XVariant to be enabled in CXinYueConfig.h"
#endif
/**
* @brief JSON值类型枚举，定义了XJsonValue支持的所有数据类型
*/
typedef enum XJsonValueType
{
	XJsonValue_Invalid,    ///< 无效类型
	XJsonValue_Null,       ///< Null类型
	XJsonValue_Bool,       ///< 布尔类型（true/false）
	XJsonValue_Double,     ///< 双精度浮点类型
	XJsonValue_Int,        ///< 64位整数类型
	XJsonValue_String,     ///< 字符串类型（基于XString）
	XJsonValue_Array,      ///< 数组类型（XJsonArray）
	XJsonValue_Object      ///< 对象类型（XJsonObject）
} XJsonValueType;
// 前向声明
//typedef struct XJsonArray XJsonArray;
//typedef struct XJsonObject XJsonObject;
/**
* @brief JSON值结构体，用于存储各种类型的JSON数据
* @details 内部通过type字段标识数据类型，通过union存储对应类型的值
*/
typedef struct XJsonValue
{
	XJsonValueType type;   ///< 存储当前值的类型
	union {
		bool boolean;      ///< 布尔类型值
		double number;     ///< 双精度浮点类型值
		int64_t integer;   ///< 64位整数类型值
		XString* string;   ///< 字符串类型值（XString指针）
		XJsonArray* array; ///< 数组类型值（XJsonArray指针）
		XJsonObject* object; ///< 对象类型值（XJsonObject指针）
	} data;                ///< 存储具体值的联合体
} XJsonValue;
// 构造函数
/**
* @brief 创建一个Null类型的XJsonValue实例
* @return 成功返回XJsonValue指针，失败返回NULL
*/
XJsonValue* XJsonValue_create_null(void);
/**
* @brief 创建一个布尔类型的XJsonValue实例
* @param value 布尔值（true/false）
* @return 成功返回XJsonValue指针，失败返回NULL
*/
XJsonValue* XJsonValue_create_bool(bool value);
/**
* @brief 创建一个双精度浮点类型的XJsonValue实例
* @param value 双精度浮点值
* @return 成功返回XJsonValue指针，失败返回NULL
*/
XJsonValue* XJsonValue_create_double(double value);
/**
* @brief 创建一个64位整数类型的XJsonValue实例
* @param value 64位整数值
* @return 成功返回XJsonValue指针，失败返回NULL
*/
XJsonValue* XJsonValue_create_int(int64_t value);
/**
* @brief 创建一个字符串类型的XJsonValue实例（深拷贝）
* @param string 源字符串（XString指针）
* @return 成功返回XJsonValue指针，失败返回NULL（源字符串为NULL时返回NULL）
*/
XJsonValue* XJsonValue_create_string(const XString* string);
/**
* @brief 创建一个数组类型的XJsonValue实例（深拷贝）
* @param array 源数组（XJsonArray指针）
* @return 成功返回XJsonValue指针，失败返回NULL（源数组为NULL时返回NULL）
*/
XJsonValue* XJsonValue_create_array(XJsonArray* array);
/**
* @brief 创建一个对象类型的XJsonValue实例（深拷贝）
* @param object 源对象（XJsonObject指针）
* @return 成功返回XJsonValue指针，失败返回NULL
*/
XJsonValue* XJsonValue_create_object(XJsonObject* object);
/**
* @brief 通过深拷贝创建一个XJsonValue实例
* @param copy 被拷贝的XJsonValue实例
* @return 成功返回新的XJsonValue指针，失败返回NULL
*/
XJsonValue* XJsonValue_create_copy(XJsonValue* copy);
/**
* @brief 通过资源移动创建一个XJsonValue实例（转移源实例的资源所有权）
* @param move 被移动的XJsonValue实例
* @return 成功返回新的XJsonValue指针，失败返回NULL
*/
XJsonValue* XJsonValue_create_move(XJsonValue* move);
/**
* @brief 初始化XJsonValue实例
* @param var 需要初始化的XJsonValue指针
* @param type 初始化的目标类型
*/
void XJsonValue_init(XJsonValue* var, XJsonValueType type);
/**
* @brief 声明并初始化XJsonValue变量的宏
* @param var 变量名
* @param type 初始化的类型
* @details 宏展开为：声明一个临时变量_##var，将var定义为其指针，并调用XJsonValue_init初始化
*/
#define XJsonValue_Init(var,type)  XJsonValue _##var,*var=&_##var;XJsonValue_init(var,type)
// 复制和移动
/**
* @brief 深拷贝XJsonValue的值
* @param var 目标XJsonValue指针
* @param src 源XJsonValue指针
* @details 先释放目标原有资源，再拷贝源的值（根据类型进行对应深拷贝）
*/
void XJsonValue_copy(XJsonValue* var, const XJsonValue* src);
/**
* @brief 移动XJsonValue的资源（转移所有权）
* @param var 目标XJsonValue指针
* @param src 源XJsonValue指针
* @details 先释放目标原有资源，再交换源和目标的内容
*/
void XJsonValue_move(XJsonValue* var, XJsonValue* src);
// 析构函数
/**
* @brief 反初始化XJsonValue实例（释放内部资源，不释放实例本身）
* @param value 需要反初始化的XJsonValue指针
* @details 根据当前类型释放对应资源（如字符串、数组、对象），重置类型为Invalid
*/
void XJsonValue_deinit(XJsonValue* value);
/**
* @brief 销毁XJsonValue实例（释放内部资源及实例本身）
* @param value 需要销毁的XJsonValue指针
*/
void XJsonValue_delete(XJsonValue* value);
/**
* @brief 清空XJsonValue的内容（保留实例，重置值）
* @param value 需要清空的XJsonValue指针
* @details 对字符串、数组、对象清空内部数据；对其他类型重置为0或默认值
*/
void XJsonValue_clear(XJsonValue* value);
// 类型检查
/**
* @brief 获取XJsonValue的类型
* @param value XJsonValue指针
* @return 返回XJsonValueType枚举值，若value为NULL返回XJsonValue_Invalid
*/
XJsonValueType XJsonValue_type(const XJsonValue* value);
/**
* @brief 检查XJsonValue是否为Null类型
* @param value XJsonValue指针
* @return 是Null类型返回true，否则返回false
*/
bool XJsonValue_isNull(const XJsonValue* value);
/**
* @brief 检查XJsonValue是否为布尔类型
* @param value XJsonValue指针
* @return 是布尔类型返回true，否则返回false
*/
bool XJsonValue_isBool(const XJsonValue* value);
/**
* @brief 检查XJsonValue是否为双精度浮点类型
* @param value XJsonValue指针
* @return 是双精度浮点类型返回true，否则返回false
*/
bool XJsonValue_isDouble(const XJsonValue* value);
/**
* @brief 检查XJsonValue是否为64位整数类型
* @param value XJsonValue指针
* @return 是64位整数类型返回true，否则返回false
*/
bool XJsonValue_isInt(const XJsonValue* value);
/**
* @brief 检查XJsonValue是否为字符串类型
* @param value XJsonValue指针
* @return 是字符串类型返回true，否则返回false
*/
bool XJsonValue_isString(const XJsonValue* value);
/**
* @brief 检查XJsonValue是否为数组类型
* @param value XJsonValue指针
* @return 是数组类型返回true，否则返回false
*/
bool XJsonValue_isArray(const XJsonValue* value);
/**
* @brief 检查XJsonValue是否为对象类型
* @param value XJsonValue指针
* @return 是对象类型返回true，否则返回false
*/
bool XJsonValue_isObject(const XJsonValue* value);
// 值获取
/**
* @brief 获取布尔类型值（带默认值）
* @param value XJsonValue指针
* @param defaultValue 类型不匹配时返回的默认值
* @return 若为布尔类型返回对应值，否则返回defaultValue
*/
bool XJsonValue_toBool(const XJsonValue* value, bool defaultValue);
/**
* @brief 获取双精度浮点类型值（带默认值）
* @param value XJsonValue指针
* @param defaultValue 类型不匹配时返回的默认值
* @return 若为双精度浮点类型返回对应值，否则返回defaultValue
*/
double XJsonValue_toDouble(const XJsonValue* value, double defaultValue);
/**
* @brief 获取64位整数类型值（带默认值）
* @param value XJsonValue指针
* @param defaultValue 类型不匹配时返回的默认值
* @return 若为64位整数类型返回对应值，否则返回defaultValue
*/
int64_t XJsonValue_toInt(const XJsonValue* value, int64_t defaultValue);
/**
* @brief 获取字符串类型值
* @param value XJsonValue指针
* @return 若为字符串类型返回XString指针，否则返回NULL
*/
const XString* XJsonValue_toString(const XJsonValue* value);
/**
* @brief 获取数组类型值
* @param value XJsonValue指针
* @return 若为数组类型返回XJsonArray指针，否则返回NULL
*/
XJsonArray* XJsonValue_toArray(const XJsonValue* value);
/**
* @brief 获取对象类型值
* @param value XJsonValue指针
* @return 若为对象类型返回XJsonObject指针，否则返回NULL
*/
XJsonObject* XJsonValue_toObject(const XJsonValue* value);
// 值设置
/**
* @brief 将XJsonValue设置为Null类型
* @param value XJsonValue指针
*/
void XJsonValue_setNull(XJsonValue* value);
/**
* @brief 将XJsonValue设置为布尔类型
* @param value XJsonValue指针
* @param b 布尔值（true/false）
*/
void XJsonValue_setBool(XJsonValue* value, bool b);
/**
* @brief 将XJsonValue设置为双精度浮点类型
* @param value XJsonValue指针
* @param d 双精度浮点值
*/
void XJsonValue_setDouble(XJsonValue* value, double d);
/**
* @brief 将XJsonValue设置为64位整数类型
* @param value XJsonValue指针
* @param i 64位整数值
*/
void XJsonValue_setInt(XJsonValue* value, int64_t i);
/**
* @brief 将XJsonValue设置为字符串类型（深拷贝）
* @param value XJsonValue指针
* @param s 源字符串（XString指针）
*/
void XJsonValue_setString(XJsonValue* value, const XString* s);
/**
* @brief 将XJsonValue设置为字符串类型（移动资源）
* @param value XJsonValue指针
* @param s 源字符串（XString指针，所有权转移）
*/
void XJsonValue_setString_move(XJsonValue* value, const XString* s);
/**
* @brief 将XJsonValue设置为字符串类型（从UTF-8字符创创建）
* @param value XJsonValue指针
* @param utf8 UTF-8编码的字符串
*/
void XJsonValue_setString_utf8(XJsonValue* value, const char* utf8);
/**
* @brief 将XJsonValue设置为数组类型（深拷贝）
* @param value XJsonValue指针
* @param a 源数组（XJsonArray指针）
*/
void XJsonValue_setArray(XJsonValue* value, XJsonArray* a);
/**
* @brief 将XJsonValue设置为数组类型（移动资源）
* @param value XJsonValue指针
* @param a 源数组（XJsonArray指针，所有权转移）
*/
void XJsonValue_setArray_move(XJsonValue* value, XJsonArray* a);
/**
* @brief 将XJsonValue设置为对象类型（深拷贝）
* @param value XJsonValue指针
* @param o 源对象（XJsonObject指针）
*/
void XJsonValue_setObject(XJsonValue* value, XJsonObject* o);
/**
* @brief 将XJsonValue设置为对象类型（移动资源）
* @param value XJsonValue指针
* @param o 源对象（XJsonObject指针，所有权转移）
*/
void XJsonValue_setObject_move(XJsonValue* value, XJsonObject* o);
// 与XVariant转换
/**
* @brief 将XJsonValue转换为XVariant（深拷贝）
* @param value XJsonValue指针
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XJsonValue_toVariant(const XJsonValue* value);
/**
* @brief 将XJsonValue转换为XVariant（移动资源）
* @param value XJsonValue指针（所有权转移）
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XJsonValue_toVariant_move(XJsonValue* value);
/**
* @brief 将XJsonValue转换为XVariant（引用形式，不转移所有权）
* @param value XJsonValue指针
* @return 成功返回XVariant指针，失败返回NULL
*/
XVariant* XJsonValue_toVariant_ref(XJsonValue* value);
#ifdef __cplusplus
}
#endif
#endif // XJSONVALUE_H