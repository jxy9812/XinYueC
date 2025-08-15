#ifndef XBSONDOCUMENT_H
#define XBSONDOCUMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XBson.h"
#include "XBsonObject.h"
#include "XJsonDocument.h"

typedef struct XBsonDocument 
{
    XBsonObject object;
} XBsonDocument;

// 构造与析构
XBsonDocument* XBsonDocument_create();
XBsonDocument* XBsonDocument_create_copy(const XBsonDocument* other);
XBsonDocument* XBsonDocument_create_move(XBsonDocument* other);
void XBsonDocument_init(XBsonDocument* document);
void XBsonDocument_deinit(XBsonDocument* document);
void XBsonDocument_delete(XBsonDocument* document);

// 拷贝与移动
void XBsonDocument_copy(XBsonDocument* dest, const XBsonDocument* src);
void XBsonDocument_move(XBsonDocument* dest, XBsonDocument* src);

// 元素操作
#define XBsonDocument_size(doc)              XBsonObject_size(&(doc)->object)
#define XBsonDocument_is_empty(doc)          XBsonObject_is_empty(&(doc)->object)
#define XBsonDocument_clear(doc)             XBsonObject_clear(&(doc)->object)
#define XBsonDocument_insert(doc, key, val)  XBsonObject_insert(&(doc)->object, key, val)
#define XBsonDocument_insert_move(doc, key, val) XBsonObject_insert_move(&(doc)->object, key, val)
#define XBsonDocument_remove(doc, key)       XBsonObject_remove(&(doc)->object, key)
#define XBsonDocument_get(doc, key)          XBsonObject_get(&(doc)->object, key)
#define XBsonDocument_contains(doc, key)     XBsonObject_contains(&(doc)->object, key)

// 转换函数
XJsonDocument* XBsonDocument_to_json_document(const XBsonDocument* bson_doc);
XString* XBsonDocument_to_json_string(const XBsonDocument* bson_doc);
XJsonObject* XBsonDocument_to_json_object(const XBsonDocument* bson_doc);

void XBsonDocument_from_json_document(XBsonDocument* bson_doc, const XJsonDocument* json_doc);
void XBsonDocument_from_json_object(XBsonDocument* bson_doc, const XJsonObject* json_obj);

// 序列化与反序列化
XByteArray* XBsonDocument_to_bytes(const XBsonDocument* document);
bool XBsonDocument_from_bytes(XBsonDocument* document, const uint8_t* data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // XBSONDOCUMENT_H