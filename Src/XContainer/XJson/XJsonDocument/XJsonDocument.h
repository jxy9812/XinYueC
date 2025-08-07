#ifndef XJSONDOCUMENT_H
#define XJSONDOCUMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XDataStructConfig.h"
#include "XJsonValue.h"
#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XString.h"

typedef enum XJsonDocumentFormat 
{
    XJsonDocument_Indented,
    XJsonDocument_Compact
} XJsonDocumentFormat;

typedef struct XJsonDocument 
{
    XJsonValue* root;
} XJsonDocument;

// 构造与析构
XJsonDocument* XJsonDocument_create(void);
XJsonDocument* XJsonDocument_create_object(XJsonObject* object);
XJsonDocument* XJsonDocument_create_array(XJsonArray* array);
void XJsonDocument_delete(XJsonDocument* document);

// 根对象操作
XJsonValue* XJsonDocument_root(XJsonDocument* document);
const XJsonValue* XJsonDocument_root_const(const XJsonDocument* document);
void XJsonDocument_setRoot(XJsonDocument* document, XJsonValue* root);

// 对象和数组访问
XJsonObject* XJsonDocument_object(XJsonDocument* document);
XJsonArray* XJsonDocument_array(XJsonDocument* document);

// 解析与序列化
XJsonDocument* XJsonDocument_fromString(const XString* json);
XString* XJsonDocument_toString(const XJsonDocument* document, XJsonDocumentFormat format);

// 与XVariant转换
XVariant* XJsonDocument_toVariant(const XJsonDocument* document);
XJsonDocument* XJsonDocument_fromVariant(const XVariant* variant);

#ifdef __cplusplus
}
#endif

#endif // XJSONDOCUMENT_H