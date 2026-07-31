#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XJsonDocument.h"
#include "XMemory.h"
#include "XMap.h"
#include "XHashMap.h"
#include "XVector.h"
#include "XStack.h"

XJsonObject* XJsonObject_create(void)
{
	XJsonObject* object = (XJsonObject*)XMalloc_System(sizeof(XJsonObject));
	if (!object)
		return NULL;
    XJsonObject_init(object);
    Set_Class_MemoryFree(object, XFree_System);
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
    XMap_init(object, sizeof(XString), sizeof(XJsonValue), XString_compare, true);

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

bool XJsonObject_insert_value_move(XJsonObject* object, const XString* key, XJsonValue* value)
{
    if (object == NULL || key == NULL || value == NULL)
        return false;
    XString_Init_Utf8(str, NULL);
    XString_assign(str,key);
    bool ret = XJsonObject_insert_move_base(object, str, value);
    XString_deinit_base(str);
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

XJsonValue* XJsonObject_value_keyUtf8(const XJsonObject* object, const char* key)
{
    XJsonValue* value;
    XString* string;
    if (!object || !key)
        return XJsonValue_create_undefined();
    string = XString_create_utf8(key);
    if (!string)
        return NULL;
    value = XJsonObject_value_base((XJsonObject*)object, string);
    XString_delete_base(string);
    return value ? XJsonValue_create_copy(value) : XJsonValue_create_undefined();
}

bool XJsonObject_contains_keyUtf8(const XJsonObject* object, const char* key)
{
    XString* string;
    bool contains;
    if (!object || !key)
        return false;
    string = XString_create_utf8(key);
    if (!string)
        return false;
    contains = XJsonObject_contains(object, string);
    XString_delete_base(string);
    return contains;
}

XJsonValue* XJsonObject_take_keyUtf8(XJsonObject* object, const char* key)
{
    XJsonValue* value = XJsonObject_value_keyUtf8(object, key);
    if (!value || XJsonValue_isUndefined(value))
        return value;
    if (!XJsonObject_remove_keyUtf8(object, key)) {
        XJsonValue_delete(value);
        return XJsonValue_create_undefined();
    }
    return value;
}

bool XJsonObject_equals(const XJsonObject* left, const XJsonObject* right)
{
    XVector* keys;
    size_t index;
    bool equal = true;
    if (left == right)
        return true;
    if (!left || !right || XJsonObject_size_base(left) != XJsonObject_size_base(right))
        return false;
    keys = XJsonObject_keys_base(left);
    if (!keys)
        return XJsonObject_size_base(left) == 0;
    for (index = 0; index < XVector_size_base(keys); ++index) {
        XString* key = XVector_at_base(keys, (int64_t)index);
        if (!key || !XJsonObject_contains(right, key) ||
            !XJsonValue_equals(XJsonObject_value_base((XJsonObject*)left, key),
                                       XJsonObject_value_base((XJsonObject*)right, key))) {
            equal = false;
            break;
        }
    }
    XVector_delete_base(keys);
    return equal;
}

XJsonObject* XJsonObject_fromVariantMap(const XVariantMap* map)
{
    XJsonObject* object;
    XVector* keys;
    size_t index;
    if (!map)
        return NULL;
    object = XJsonObject_create();
    if (!object)
        return NULL;
    keys = XMap_keys_base(map);
    if (!keys) {
        XJsonObject_delete_base(object);
        return NULL;
    }
    for (index = 0; index < XVector_size_base(keys); ++index) {
        XString* key = XVector_at_base(keys, (int64_t)index);
        XVariant* variant = key ? XMap_value_base((XVariantMap*)map, key) : NULL;
        XJsonValue* value = XJsonValue_fromVariant(variant);
        if (!key || !value || !XJsonObject_insert_value_move(object, key, value)) {
            if (value) XJsonValue_delete(value);
            XVector_delete_base(keys);
            XJsonObject_delete_base(object);
            return NULL;
        }
        XJsonValue_delete(value);
    }
    XVector_delete_base(keys);
    return object;
}

XJsonObject* XJsonObject_fromVariantHash(const XVariantHashMap* hash)
{
    XJsonObject* object;
    XVector* keys;
    size_t index;
    if (!hash)
        return NULL;
    object = XJsonObject_create();
    if (!object)
        return NULL;
    keys = XMapBase_keys_base((const XMapBase*)hash);
    if (!keys) {
        XJsonObject_delete_base(object);
        return NULL;
    }
    for (index = 0; index < XVector_size_base(keys); ++index) {
        XString* key = XVector_at_base(keys, (int64_t)index);
        XVariant* variant = key ? XMapBase_value_base((XMapBase*)hash, key) : NULL;
        XJsonValue* value = XJsonValue_fromVariant(variant);
        if (!key || !value || !XJsonObject_insert_value_move(object, key, value)) {
            if (value) XJsonValue_delete(value);
            XVector_delete_base(keys);
            XJsonObject_delete_base(object);
            return NULL;
        }
        XJsonValue_delete(value);
    }
    XVector_delete_base(keys);
    return object;
}


// 对象序列化实现
XString* XJsonObject_toString(const XJsonObject* object, XJsonDocumentFormat format)
{
    XJsonDocument* doc = XJsonDocument_create();
	if (!doc)
		return NULL;
    //引用XJsonObject 
    doc->root.data.object = (XJsonObject*)object;
    doc->root.type = XJsonValue_Object;
    XString* str= XJsonDocument_toString(doc,format);
    //恢复防止释放 XJsonObject
    doc->root.data.object = NULL;
    doc->root.type = XJsonValue_Invalid;
    XJsonDocument_delete(doc);
    return str;
}
XByteArray* XJsonObject_toJson(const XJsonObject* object, XJsonDocumentFormat format)
{
    XJsonDocument* doc = XJsonDocument_create();
	if (!doc)
		return NULL;
    //引用XJsonObject 
    doc->root.data.object = (XJsonObject*)object;
    doc->root.type = XJsonValue_Object;
    XByteArray* json = XJsonDocument_toJson(doc, format);
    //恢复防止释放 XJsonObject
    doc->root.data.object = NULL;
    doc->root.type = XJsonValue_Invalid;
    XJsonDocument_delete(doc);
    return json;
}
XVariantMap* XJsonObject_toVariantMap(const XJsonObject* object)
{
    if (object == NULL)
        return NULL;
    XVariantMap* map = XMap_create_XVariantMap();
    XPair* pair=NULL;
    XVariant* var = NULL;
    for_each_iterator(object, XMap, it)
    {
        pair = XMap_iterator_data(&it);
        var = XJsonValue_toVariant(XPair_second(pair));
        XMap_insert_valueMove_base(map,XPair_first(pair),var);
        XVariant_delete_base(var);
    }
    return map;
}

XVariantHashMap* XJsonObject_toVariantHash(const XJsonObject* object)
{
    XVariantHashMap* hash;
    XVector* keys;
    size_t index;
    if (!object)
        return NULL;
    hash = XHashMap_create_XVariantHashMap();
    if (!hash)
        return NULL;
    keys = XMapBase_keys_base((const XMapBase*)object);
    if (!keys) {
        XHashMap_delete_base(hash);
        return NULL;
    }
    for (index = 0; index < XVector_size_base(keys); ++index) {
        XString* key = XVector_at_base(keys, (int64_t)index);
        XJsonValue* value = key ? XMapBase_value_base((XMapBase*)object, key) : NULL;
        XVariant* variant = XJsonValue_toVariant(value);
        if (!key || !variant || !XHashMap_insert_base(hash, key, variant)) {
            if (variant) XVariant_delete_base(variant);
            XVector_delete_base(keys);
            XHashMap_delete_base(hash);
            return NULL;
        }
        XVariant_delete_base(variant);
    }
    XVector_delete_base(keys);
    return hash;
}
XVariantMap* XJsonObject_toVariantMap_move(XJsonObject* object)
{
    if (object == NULL)
        return NULL;
    XVariantMap* map = XMap_create_XVariantMap();
    if (map == NULL)
        return NULL;
    XMap_detach((XMap*)object);
    if (!XMap_isDetached((const XMap*)object)) {
        XMap_delete_base(map);
        return NULL;
    }
    XPair* pair = NULL;
    XVariant* var = NULL;
    for_each_iterator(object, XMap, it)
    {
        pair = XMap_iterator_data(&it);
        var = XJsonValue_toVariant_move(XPair_second(pair));
        XMap_insert_valueMove_base(map, XPair_first(pair), var);
        XVariant_delete_base(var);
    }
    return map;
}
XVariant* XJsonObject_toVariant(const XJsonObject* obj)
{
    if (obj == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, sizeof(XJsonObject), XVariantType_JsonObject);
    XJsonObject_init(var->m_data);
    XJsonObject_copy_base(var->m_data, obj);
    return var;
}
XVariant* XJsonObject_toVariant_move(XJsonObject* obj)
{
    if (obj == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, sizeof(XJsonObject), XVariantType_JsonObject);
    XJsonObject_init(var->m_data);
    XJsonObject_move_base(var->m_data, obj);
    return var;
}
XVariant* XJsonObject_toVariant_ref(XJsonObject* obj)
{
    if (obj == NULL)
        return NULL;
    XVariant* var = XVariant_create(NULL, 0, XVariantType_JsonObject);
    if (var == NULL)
        return NULL;
    var->m_data = obj;
    var->m_dataSize = sizeof(XJsonObject);
    return var;
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
