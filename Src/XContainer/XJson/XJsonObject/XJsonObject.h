#ifndef XJSONOBJECT_H
#define XJSONOBJECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XDataStructConfig.h"
#include "XJsonValue.h"
#include "XMap.h"

#if !XMap_ON
#error "XJsonObject requires XMap to be enabled in XDataStructConfig.h"
#endif

typedef struct XJsonObject {
    XMap* members; // 键为XString*，值为XJsonValue*
} XJsonObject;

// 构造与析构
XJsonObject* XJsonObject_create(void);
void XJsonObject_delete(XJsonObject* object);

// 基本操作
int XJsonObject_size(const XJsonObject* object);
bool XJsonObject_isEmpty(const XJsonObject* object);
void XJsonObject_clear(XJsonObject* object);

// 成员访问
bool XJsonObject_contains(const XJsonObject* object, const XString* key);
XJsonValue* XJsonObject_value(XJsonObject* object, const XString* key);
const XJsonValue* XJsonObject_value_const(const XJsonObject* object, const XString* key);

// 成员修改
bool XJsonObject_insert(XJsonObject* object, const XString* key, XJsonValue* value);
bool XJsonObject_remove(XJsonObject* object, const XString* key);
void XJsonObject_removeAll(XJsonObject* object);

// 键操作
XVector* XJsonObject_keys(const XJsonObject* object); // 返回XString*的向量

// 转换函数
XString* XJsonObject_toString(const XJsonObject* object);
XVariantMap* XJsonObject_toVariantMap(const XJsonObject* object);
XJsonObject* XJsonObject_fromVariantMap(const XVariantMap* map);

#ifdef __cplusplus
}
#endif

#endif // XJSONOBJECT_H