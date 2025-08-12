#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XJsonDocument.h"
#include "XMemory.h"
#include "XMap.h"
#include "XVector.h"
#include "XStack.h"

XJsonObject* XJsonObject_create(void)
{
	XJsonObject* object = (XJsonObject*)XMemory_malloc(sizeof(XJsonObject));
    XJsonObject_init(object);
	return object;
}

XJsonObject* XJsonObject_create_copy(XJsonObject* copy)
{
    XJsonObject* object = XJsonObject_create();
    if (object && copy)
        XJsonObject_copy_base(object, copy);
    return object;
}

XJsonObject* XJsonObject_create_move(XJsonObject* move)
{
    XJsonObject* object = XJsonObject_create();
    if (object && move)
        XJsonObject_move_base(object, move);
    return object;
}

void XJsonObject_init(XJsonObject* object)
{
    if (object == NULL)
        return;
    XMap_init(object, sizeof(XString), sizeof(XJsonValue), XEquality_XString, XLess_XString);

    XMapBaseSetKeyCopyMethod(object, XString_copy_base);
    XMapBaseSetKeyMoveMethod(object, XString_move_base);
    XMapBaseSetKeyDeinitMethod(object, XString_deinit_base);

    XContainerSetDataCopyMethod(object, XJsonValue_copy);
    XContainerSetDataMoveMethod(object, XJsonValue_move);
    XContainerSetDataDeinitMethod(object, XJsonValue_deinit);
}

bool XJsonObject_insert_keyUtf8_value(XJsonObject* object, const char* key, XJsonValue* value)
{
    if (object == NULL || key == NULL || value == NULL)
        return false;
    XString_Init_Utf8(str, key);
    bool ret=XMap_insert_keyMove_base(object, str, value);
    XString_deinit_base(str);
    return ret;
}

bool XJsonObject_insert_keyUtf8_value_move(XJsonObject* object, const char* key, XJsonValue* value)
{
    if (object == NULL || key == NULL || value == NULL)
        return false;
    XString_Init_Utf8(str, key);
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    return ret;
}

bool XJsonObject_insert_keyUtf8_double(XJsonObject* object, const char* key, double d)
{
    if (object == NULL || key == NULL )
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_Double);
    value->data.number = d;
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_insert_keyUtf8_int(XJsonObject* object, const char* key, int64_t i)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_Int);
    value->data.integer = i;
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_insert_keyUtf8_string(XJsonObject* object, const char* key, const XString* strValue)
{
    if (object == NULL || key == NULL || strValue == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_String);
    value->data.string = XString_create_copy(strValue);
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_insert_keyUtf8_string_move(XJsonObject* object, const char* key, XString* strValue)
{
    if (object == NULL || key == NULL || strValue == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_String);
    value->data.string = strValue;
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_insert_keyUtf8_utf8(XJsonObject* object, const char* key, const char* utf8)
{
    if (object == NULL || key == NULL || utf8 == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_String);
    value->data.string = XString_create_utf8(utf8);
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_insert_keyUtf8_null(XJsonObject* object, const char* key)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_Null);
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_insert_keyUtf8_bool(XJsonObject* object, const char* key, bool b)
{
    if (object == NULL || key == NULL )
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_Bool);
    value->data.boolean=b;
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_insert_keyUtf8_array(XJsonObject* object, const char* key, const XJsonArray* array)
{
    if (object == NULL || key == NULL || array == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_Array);
    value->data.array = XJsonArray_create_copy(array);
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_insert_keyUtf8_array_move(XJsonObject* object, const char* key, XJsonArray* array)
{
    if (object == NULL || key == NULL || array == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_Array);
    value->data.array = XJsonArray_create_move(array);
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_insert_keyUtf8_object(XJsonObject* object, const char* key, const XJsonObject* val)
{
    if (object == NULL || key == NULL || val == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_Object);
    value->data.object = XJsonObject_create_copy(val);
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_insert_keyUtf8_object_move(XJsonObject* object, const char* key, XJsonObject* val)
{
    if (object == NULL || key == NULL || val == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_Object);
    value->data.object = XJsonObject_create_move(val);
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
    XJsonValue_deinit(value);
    return ret;
}

bool XJsonObject_remove_keyUtf8(XJsonObject* object, const char* key)
{
    if (object == NULL || key == NULL)
        return false;
    XString_Init_Utf8(str,key);
    bool ret = XJsonObject_remove_base(object,str);
    XString_deinit_base(str);
    return ret;
}


// 对象序列化实现
XString* XJsonObject_toString(const XJsonObject* object, XJsonDocumentFormat format)
{
    XJsonDocument* doc = XJsonDocument_create();
    //引用XJsonObject 
    doc->root->data.object = object;
    doc->root->type = XJsonValue_Object;
    XString* str= XJsonDocument_toString(doc,format);
    //恢复防止释放 XJsonObject
    doc->root->data.object = NULL;
    doc->root->type = XJsonValue_Invalid;
    XJsonDocument_delete(doc);
    return str;
}
//XVariantMap* XJsonObject_toVariantMap(const XJsonObject* object) {
//    if (!object) return NULL;
//
//    XVariantMap* map = XVariantMap_create();
//    if (!map) return NULL;
//
//    XMapIterator it = XMap_begin(object->members);
//    while (!XMapIterator_isEnd(&it)) {
//        XString* key = *(XString**)XMapIterator_key(&it);
//        XJsonValue* jsonVal = *(XJsonValue**)XMapIterator_value(&it);
//
//        XVariant* var = XJsonValue_toVariant(jsonVal);
//        if (var) {
//            XVariantMap_insert(map, key, var);
//        }
//
//        XMapIterator_next(&it);
//    }
//
//    return map;
//}
//
//XJsonObject* XJsonObject_fromVariantMap(const XVariantMap* map) {
//    if (!map) return NULL;
//
//    XJsonObject* object = XJsonObject_create();
//    if (!object) return NULL;
//
//    XMapIterator it = XMap_begin((XMap*)map);
//    while (!XMapIterator_isEnd(&it)) {
//        XString* key = *(XString**)XMapIterator_key(&it);
//        XVariant* var = *(XVariant**)XMapIterator_value(&it);
//
//        XJsonValue* jsonVal = XJsonValue_fromVariant(var);
//        if (jsonVal) {
//            XJsonObject_insert(object, key, jsonVal);
//        }
//
//        XMapIterator_next(&it);
//    }
//
//    return object;
//}
