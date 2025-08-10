#include "XJsonDocument.h"
#include "XJsonValue.h"
#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XString.h"
#include "XMemory.h"
// 辅助函数：转义字符串中的特殊字符
void  XJson_escape_string(const XString* str, XString* output)
{
    if (!str || !output) return;

    for_each_iterator(str, XString, it)
    {
        XChar* p = XString_iterator_data(&it);
        switch (p->code) {
        case '"':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('\"'));
        }
        /*XString_append_utf8(output, "\\\"");*/ break;
        case '\\':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('\\'));
        } /*XString_append_utf8(output, "\\\\");*/ break;
        case '\b':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('b'));
        }/*XString_append_utf8(output, "\\b"); */ break;
        case '\f':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('f'));
        }/*XString_append_utf8(output, "\\f");*/  break;
        case '\n':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('n'));
        }/*XString_append_utf8(output, "\\n"); */ break;
        case '\r':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('r'));
        }/* XString_append_utf8(output, "\\r"); */ break;
        case '\t':
        {
            XString_push_back_base(output, XChar_from('\\'));
            XString_push_back_base(output, XChar_from('t'));
        }/*XString_append_utf8(output, "\\t");*/  break;
        default:
            // 对于非ASCII字符，保持原样（JSON允许UTF-8编码）
            XString_push_back_base(output, *p);
            break;
        }
    }
}

// 辅助函数：序列化XJsonValue
void XJson_serialize_json_value(const XJsonValue* value, XString* output, XJsonDocumentFormat format)
{
    if (!value || !output) return;

    switch (XJsonValue_type(value))
    {
    case XJsonValue_Null:
        XString_append_utf8(output, "null");
        break;

    case XJsonValue_Bool:
        XString_append_utf8(output, XJsonValue_toBool(value, false) ? "true" : "false");
        break;

    case XJsonValue_Double: {
        char buffer[64];
        // 处理整数情况，避免显示为10.0这样的形式
        double num = XJsonValue_toDouble(value, 0.0);
        if (num == (long long)num) {
            snprintf(buffer, sizeof(buffer), "%lld", (long long)num);
        }
        else {
            snprintf(buffer, sizeof(buffer), "%g", num);
        }
        XString_append_utf8(output, buffer);
        break;
    }

    case XJsonValue_String: {
        const XString* str = XJsonValue_toString(value);
        XString_push_back_base(output, XChar_from('\"'));
        if (str)
        {
            XJson_escape_string(str, output);
        }
        XString_push_back_base(output, XChar_from('\"'));
        break;
    }

    case XJsonValue_Array: {
        const XJsonArray* array = XJsonValue_toArray(value);
        XString* arrayStr = XJsonArray_toString(array, format);
        if (arrayStr) {
            XString_append(output, arrayStr);
            XString_delete_base(arrayStr);
        }
        else
        {
            XString_push_back_base(output, XChar_from('['));
            XString_push_back_base(output, XChar_from(']'));
            /*  XString_append_utf8(output, "[]");*/
        }
        break;
    }

    case XJsonValue_Object:
    {
        const XJsonObject* object = XJsonValue_toObject(value);
        XString* objectStr = XJsonObject_toString(object,format);
        if (objectStr)
        {
            XString_append(output, objectStr);
            XString_delete_base(objectStr);
        }
        else
        {
            XString_push_back_base(output, XChar_from('{'));
            XString_push_back_base(output, XChar_from('}'));
            //XString_append_utf8(output, "{}");
        }
        break;
    }

    default:
        XString_append_utf8(output, "null");
        break;
    }
}

XJsonDocument* XJsonDocument_create(void)
{
    XJsonDocument* doc = (XJsonDocument*)XMemory_malloc(sizeof(XJsonDocument));
    XJsonDocument_init(doc);
    return doc;
}

XJsonDocument* XJsonDocument_create_object(XJsonObject* object) 
{
    if (!object) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc) 
    {
        XJsonValue_setObject(doc->root, object);
    }
    return doc;
}

XJsonDocument* XJsonDocument_create_array(XJsonArray* array) {
    if (!array) return NULL;

    XJsonDocument* doc = XJsonDocument_create();
    if (doc)
    {
        XJsonValue_setArray(doc->root, array);
    }
    return doc;
}

void XJsonDocument_init(XJsonDocument* document)
{
    if (document == NULL)
        return;
    document->root = XJsonValue_create_null();
}

void XJsonDocument_deinit(XJsonDocument* document)
{
    if (!document) return;

    if (document->root) 
    {
        XJsonValue_delete(document->root);
        document->root = NULL;
    }
}

void XJsonDocument_delete(XJsonDocument* document)
{
    XJsonDocument_deinit(document);
    if(document)
        XMemory_free(document);
}

XJsonValue* XJsonDocument_root(XJsonDocument* document) 
{
    return document ? document->root : NULL;
}

const XJsonValue* XJsonDocument_root_const(const XJsonDocument* document) 
{
    return XJsonDocument_root((XJsonDocument*)document);
}

void XJsonDocument_setRoot(XJsonDocument* document, XJsonValue* root) 
{
    if (!document || !root) return;

    if (document->root)
    {
        XJsonValue_delete(document->root);
    }
    document->root = root;
}

bool XJsonDocument_isArray(const XJsonDocument* document)
{
    if (!document || !document->root) 
        return false;
    return document->root->type == XJsonValue_Array;
}

bool XJsonDocument_isObject(const XJsonDocument* document)
{
    if (!document || !document->root)
        return false;
    return document->root->type == XJsonValue_Object;
}

bool XJsonDocument_isNull(const XJsonDocument* document)
{
    if (!document || !document->root)
        return false;
    switch (document->root->type)
    {
    case XJsonValue_Invalid:
    case XJsonValue_Null: return true;
    default:
        break; 
    };
    return false;
}

bool XJsonDocument_isEmpty(const XJsonDocument* document)
{
    if (!document || !document->root)
        return false;
    switch (document->root->type)
    {
    case XJsonValue_Invalid:
    case XJsonValue_Null: return true;
    case XJsonValue_String:return XString_isEmpty_base(document->root->data.string);
    case XJsonValue_Array: return XJsonArray_isEmpty_base(document->root->data.array);
    case XJsonValue_Object:return XJsonObject_isEmpty_base(document->root->data.object);
    default:
        break;
    };
    return false;
}

XJsonObject* XJsonDocument_object(XJsonDocument* document) 
{
    if (!document || !document->root) return NULL;

    if (document->root->type != XJsonValue_Object) 
    {
       /* XJsonObject* obj = XJsonObject_create();
        XJsonValue_setObject(document->root, obj);*/
        return NULL;
    }

    return document->root->data.object;
}

XJsonArray* XJsonDocument_array(XJsonDocument* document) 
{
    if (!document || !document->root) return NULL;

    if (document->root->type != XJsonValue_Array) {
     /*   XJsonArray* arr = XJsonArray_create();
        XJsonValue_setArray(document->root, arr);
        return arr;*/
        return NULL;
    }

    return document->root->data.array;
}

bool XJsonDocument_setArray(XJsonDocument* document, const XJsonArray* array)
{
    if (!document || array) 
        return false;
    if (document->root)
        XJsonValue_setArray(document->root, array);
    else
        document->root = XJsonValue_create_array(array);
    return true;
}

bool XJsonDocument_setObject(XJsonDocument* document, const XJsonObject* object)
{
    if (!document || object)
        return false;
    if (document->root)
        XJsonValue_setObject(document->root, object);
    else
        document->root = XJsonValue_create_object(object);
    return true;
}

bool XJsonDocument_setArray_move(XJsonDocument* document, XJsonArray* array)
{
    if (!document || array)
        return false;
    if (document->root=NULL)
        document->root = XJsonValue_create_null();
    XJsonValue_setArray_move(document->root, array);
    return true;
}

bool XJsonDocument_setObject_move(XJsonDocument* document, XJsonObject* object)
{
    if (!document || object)
        return false;
    if (document->root = NULL)
        document->root = XJsonValue_create_null();
    XJsonValue_setObject_move(document->root, object);
    return true;
}

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