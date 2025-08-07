#ifndef XJSONVALUE_H
#define XJSONVALUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XDataStructConfig.h"
#include "XString.h"
#include "XVariant.h"
#include <stdbool.h>
#include <stdint.h>

#if !XString_ON || !XVariant_ON
#error "XJsonValue requires XString and XVariant to be enabled in XDataStructConfig.h"
#endif

typedef enum XJsonValueType 
{
    XJsonValue_Invalid,
    XJsonValue_Null,
    XJsonValue_Bool,
    XJsonValue_Double,
    XJsonValue_String,
    XJsonValue_Array,
    XJsonValue_Object
} XJsonValueType;

typedef struct XJsonArray XJsonArray;
typedef struct XJsonObject XJsonObject;

typedef struct XJsonValue 
{
    XJsonValueType type;
    union {
        bool boolean;
        double number;
        XString* string;
        XJsonArray* array;
        XJsonObject* object;
    } data;
} XJsonValue;

// 构造函数
XJsonValue* XJsonValue_create_null(void);
XJsonValue* XJsonValue_create_bool(bool value);
XJsonValue* XJsonValue_create_double(double value);
XJsonValue* XJsonValue_create_string(const XString* string);
XJsonValue* XJsonValue_create_array(XJsonArray* array);
XJsonValue* XJsonValue_create_object(XJsonObject* object);
//复制和移动
void XJsonValue_copy(XJsonValue* var, const XJsonValue* src);
void XJsonValue_move(XJsonValue* var, XJsonValue* src);
// 析构函数
void XJsonValue_deinit(XJsonValue* value);
void XJsonValue_delete(XJsonValue* value);

// 类型检查
XJsonValueType XJsonValue_type(const XJsonValue* value);
bool XJsonValue_isNull(const XJsonValue* value);
bool XJsonValue_isBool(const XJsonValue* value);
bool XJsonValue_isDouble(const XJsonValue* value);
bool XJsonValue_isString(const XJsonValue* value);
bool XJsonValue_isArray(const XJsonValue* value);
bool XJsonValue_isObject(const XJsonValue* value);

// 值获取
bool XJsonValue_toBool(const XJsonValue* value, bool defaultValue);
double XJsonValue_toDouble(const XJsonValue* value, double defaultValue);
const XString* XJsonValue_toString(const XJsonValue* value);
XJsonArray* XJsonValue_toArray(const XJsonValue* value);
XJsonObject* XJsonValue_toObject(const XJsonValue* value);

// 值设置
void XJsonValue_setNull(XJsonValue* value);
void XJsonValue_setBool(XJsonValue* value, bool b);
void XJsonValue_setDouble(XJsonValue* value, double d);
void XJsonValue_setString(XJsonValue* value, const XString* s);
void XJsonValue_setString_move(XJsonValue* value, const XString* s);
void XJsonValue_setString_utf8(XJsonValue* value, const char* utf8);
void XJsonValue_setArray(XJsonValue* value, XJsonArray* a);
void XJsonValue_setArray_move(XJsonValue* value, XJsonArray* a);
void XJsonValue_setObject(XJsonValue* value, XJsonObject* o);

// 与XVariant转换
XVariant* XJsonValue_toVariant(const XJsonValue* value);
XJsonValue* XJsonValue_fromVariant(const XVariant* variant);

#ifdef __cplusplus
}
#endif

#endif // XJSONVALUE_H