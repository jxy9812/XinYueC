#include "XBsonDocument.h"
#include "XMemory.h"
#include "XJsonDocument.h"

XBsonDocument* XBsonDocument_create() {
    XBsonDocument* document = (XBsonDocument*)XMemory_malloc(sizeof(XBsonDocument));
    if (document) {
       XBsonDocument_init(document);
    }
    return document;
}
//
XBsonDocument* XBsonDocument_create_copy(const XBsonDocument* other) {
    if (!other) return NULL;

    XBsonDocument* document = XBsonDocument_create();
    if (document) {
        XBsonDocument_copy(document, other);
    }
    return document;
}

XBsonDocument* XBsonDocument_create_move(XBsonDocument* other) {
    if (!other) return NULL;

    XBsonDocument* document = XBsonDocument_create();
    if (document) {
        XBsonDocument_move(document, other);
    }
    return document;
}
//
void XBsonDocument_init(XBsonDocument* document) {
    if (!document) return;

    XBsonObject_init(&document->object);
}

void XBsonDocument_deinit(XBsonDocument* document) {
    if (!document) return;

    XBsonObject_deinit_base(&document->object);
}

void XBsonDocument_delete(XBsonDocument* document) {
    if (document) {
        XBsonDocument_deinit(document);
        XMemory_free(document);
    }
}
//
void XBsonDocument_copy(XBsonDocument* dest, const XBsonDocument* src) {
    if (!dest || !src || dest == src) return;

    XBsonObject_copy_base(&dest->object, &src->object);
}

void XBsonDocument_move(XBsonDocument* dest, XBsonDocument* src) {
    if (!dest || !src || dest == src) return;

    XBsonObject_move_base(&dest->object, &src->object);
}
//
XJsonDocument* XBsonDocument_to_json_document(const XBsonDocument* bson_doc) {
    if (!bson_doc) return NULL;

    XJsonObject* json_obj = XBsonObject_to_json_object(&bson_doc->object);
    if (!json_obj) return NULL;

    XJsonDocument* json_doc = XJsonDocument_create_object(json_obj);
  /*  if (!json_doc) {
        XJsonObject_delete_base(json_obj);
    }*/

    return json_doc;
}

XString* XBsonDocument_to_json_string(const XBsonDocument* bson_doc) {
    if (!bson_doc) return NULL;

    XJsonDocument* json_doc = XBsonDocument_to_json_document(bson_doc);
    if (!json_doc) return NULL;

    XString* json_str = XJsonDocument_toString(json_doc, XJsonDocument_Compact);
    XJsonDocument_delete(json_doc);

    return json_str;
}

XJsonObject* XBsonDocument_to_json_object(const XBsonDocument* bson_doc) {
    if (!bson_doc) return NULL;

    return XBsonObject_to_json_object(&bson_doc->object);
}

void XBsonDocument_from_json_document(XBsonDocument* bson_doc, const XJsonDocument* json_doc) {
    if (!bson_doc || !json_doc) return;

    const XJsonValue* root = XJsonDocument_root_const(json_doc);
    if (XJsonValue_isObject(root)) {
        XBsonDocument_from_json_object(bson_doc, XJsonValue_toObject(root));
    }
}

void XBsonDocument_from_json_object(XBsonDocument* bson_doc, const XJsonObject* json_obj) {
    if (!bson_doc || !json_obj) return;

    XBsonObject_from_json_object(&bson_doc->object, json_obj);
}

XByteArray* XBsonDocument_to_bytes(const XBsonDocument* document) {
    if (!document) return NULL;

    return XBsonObject_to_bytes(&document->object);
}

bool XBsonDocument_from_bytes(XBsonDocument* document, const uint8_t* data, size_t size) {
    if (!document || !data || size == 0) return false;

    return XBsonObject_from_bytes(&document->object, data, size);
}