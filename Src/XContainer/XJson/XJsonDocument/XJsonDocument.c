//#include "XJsonDocument.h"
//#include "XMemory.h"
//#include "XString.h"
//
//XJsonDocument* XJsonDocument_create(void) {
//    XJsonDocument* doc = (XJsonDocument*)XMemory_malloc(sizeof(XJsonDocument));
//    if (doc) {
//        doc->root = XJsonValue_create_null();
//        if (!doc->root) {
//            XMemory_free(doc);
//            return NULL;
//        }
//    }
//    return doc;
//}
//
//XJsonDocument* XJsonDocument_create_object(XJsonObject* object) {
//    if (!object) return NULL;
//
//    XJsonDocument* doc = XJsonDocument_create();
//    if (doc) {
//        XJsonValue_setObject(doc->root, object);
//    }
//    return doc;
//}
//
//XJsonDocument* XJsonDocument_create_array(XJsonArray* array) {
//    if (!array) return NULL;
//
//    XJsonDocument* doc = XJsonDocument_create();
//    if (doc) {
//        XJsonValue_setArray(doc->root, array);
//    }
//    return doc;
//}
//
//void XJsonDocument_delete(XJsonDocument* document) {
//    if (!document) return;
//
//    if (document->root) {
//        XJsonValue_delete(document->root);
//    }
//
//    XMemory_free(document);
//}
//
//XJsonValue* XJsonDocument_root(XJsonDocument* document) {
//    return document ? document->root : NULL;
//}
//
//const XJsonValue* XJsonDocument_root_const(const XJsonDocument* document) {
//    return XJsonDocument_root((XJsonDocument*)document);
//}
//
//void XJsonDocument_setRoot(XJsonDocument* document, XJsonValue* root) {
//    if (!document || !root) return;
//
//    if (document->root) {
//        XJsonValue_delete(document->root);
//    }
//
//    document->root = root;
//}
//
//XJsonObject* XJsonDocument_object(XJsonDocument* document) {
//    if (!document || !document->root) return NULL;
//
//    if (document->root->type != XJsonValue_Object) {
//        XJsonObject* obj = XJsonObject_create();
//        XJsonValue_setObject(document->root, obj);
//        return obj;
//    }
//
//    return document->root->data.object;
//}
//
//XJsonArray* XJsonDocument_array(XJsonDocument* document) {
//    if (!document || !document->root) return NULL;
//
//    if (document->root->type != XJsonValue_Array) {
//        XJsonArray* arr = XJsonArray_create();
//        XJsonValue_setArray(document->root, arr);
//        return arr;
//    }
//
//    return document->root->data.array;
//}
//
//XJsonDocument* XJsonDocument_fromString(const XString* json) {
//    // 实际实现需要解析JSON字符串
//    // 这里仅作为框架示例
//    XJsonDocument* doc = XJsonDocument_create();
//    if (doc && json) {
//        // 解析逻辑将在这里实现
//    }
//    return doc;
//}
//
//XString* XJsonDocument_toString(const XJsonDocument* document, XJsonDocumentFormat format) {
//    if (!document || !document->root) return NULL;
//
//    switch (document->root->type) {
//    case XJsonValue_Object:
//        return XJsonObject_toString(document->root->data.object);
//    case XJsonValue_Array:
//        return XJsonArray_toString(document->root->data.array);
//    default:
//        return NULL;
//    }
//}
//
//XVariant* XJsonDocument_toVariant(const XJsonDocument* document) {
//    if (!document || !document->root) return NULL;
//    return XJsonValue_toVariant(document->root);
//}
//
//XJsonDocument* XJsonDocument_fromVariant(const XVariant* variant) {
//    if (!variant) return NULL;
//
//    XJsonDocument* doc = XJsonDocument_create();
//    if (doc) {
//        XJsonValue* root = XJsonValue_fromVariant(variant);
//        if (root) {
//            XJsonDocument_setRoot(doc, root);
//        }
//        else {
//            XJsonDocument_delete(doc);
//            return NULL;
//        }
//    }
//
//    return doc;
//}