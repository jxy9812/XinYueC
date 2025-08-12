#ifndef XJSONDOCUMENT_H
#define XJSONDOCUMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XJson.h"
#include <stdbool.h>

typedef struct XJsonDocument 
{
    XJsonValue* root;
} XJsonDocument;

// 构造与析构
XJsonDocument* XJsonDocument_create(void);
XJsonDocument* XJsonDocument_create_copy(XJsonDocument* copy);
XJsonDocument* XJsonDocument_create_move(XJsonDocument* move);
XJsonDocument* XJsonDocument_create_object(XJsonObject* object);
XJsonDocument* XJsonDocument_create_array(XJsonArray* array);
void XJsonDocument_init(XJsonDocument* document);
void XJsonDocument_deinit(XJsonDocument* document);
void XJsonDocument_delete(XJsonDocument* document);

//拷贝移动
void XJsonDocument_copy(XJsonDocument* doc, const XJsonDocument* src);
void XJsonDocument_move(XJsonDocument* doc, XJsonDocument* src);

// 根对象操作
XJsonValue* XJsonDocument_root(XJsonDocument* document);
const XJsonValue* XJsonDocument_root_const(const XJsonDocument* document);
void XJsonDocument_setRoot(XJsonDocument* document, XJsonValue* root);

// 对象和数组访问
bool XJsonDocument_isArray(const XJsonDocument* document);
bool XJsonDocument_isObject(const XJsonDocument* document);
bool XJsonDocument_isNull(const XJsonDocument* document);
bool XJsonDocument_isEmpty(const XJsonDocument* document);
XJsonObject* XJsonDocument_object(XJsonDocument* document);
XJsonArray* XJsonDocument_array(XJsonDocument* document);
bool XJsonDocument_setArray(XJsonDocument* document, const XJsonArray*array);
bool XJsonDocument_setObject(XJsonDocument* document, const XJsonObject* object);
bool XJsonDocument_setArray_move(XJsonDocument* document, XJsonArray* array);
bool XJsonDocument_setObject_move(XJsonDocument* document, XJsonObject* object);

// 解析与序列化
XJsonDocument* XJsonDocument_fromString(const XString* json);
//优先使用XJsonDocument_toJson 
XString* XJsonDocument_toString(const XJsonDocument* document, XJsonDocumentFormat format);

XJsonDocument* XJsonDocument_fromJson(const XByteArray* json);
//内部utf8编码适合传输
XByteArray* XJsonDocument_toJson(const XJsonDocument* document, XJsonDocumentFormat format);

XJsonDocument* XJsonDocument_fromBson(const XByteArray* bson);
XByteArray* XJsonDocument_toBson(const XJsonDocument* document);

XByteArray* XJson_toBson(const XByteArray* json);
XByteArray* XBson_toJson(const XByteArray* bson);

// 与XVariant转换
XVariant* XJsonDocument_toVariant(const XJsonDocument* document);
XJsonDocument* XJsonDocument_fromVariant(const XVariant* variant);

#ifdef __cplusplus
}
#endif

#endif // XJSONDOCUMENT_H