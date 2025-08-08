#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XMemory.h"
#include "XMap.h"
#include "XVector.h"

// 辅助函数：序列化XJsonValue
void XJson_serialize_json_value(const XJsonValue* value, XString* output);
// 辅助函数：转义字符串中的特殊字符
void XJson_escape_string(const XString* str, XString* output);

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

bool XJsonObject_insert_keyUtf8_string(XJsonObject* object, const char* key, const XString* strValue)
{
    if (object == NULL || key == NULL || strValue == NULL)
        return false;
    XString_Init_Utf8(str, key);
    XJsonValue_Init(value, XJsonValue_String);
    value->data.string = XString_create(strValue);
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
void XJson_serialize_json_value(const XJsonValue* value, XString* output)
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
        XString* arrayStr = XJsonArray_toString(array);
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
           XString* objectStr = XJsonObject_toString(object);
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

// 对象序列化实现
XString* XJsonObject_toString(const XJsonObject* object) 
{
    if (!object || XJsonObject_isEmpty_base(object)) 
    {
        return XString_create_utf8("{}");
    }

    // 创建输出字符串
    XString* output = XString_create_utf8("");
    if (!output) return NULL;

    // 开始对象
    //XString_append_utf8(output, "{");
    XString_push_back_base(output, XChar_from('{'));
    // 获取所有键
    XVector* keys = XJsonObject_keys_base(object);
    if (!keys) 
    {
        XString_push_back_base(output, XChar_from('}'));
        return output;
    }

    size_t keyCount = XVector_size_base(keys);
    for (int i = 0; i < keyCount; i++) 
    {
        // 获取键
        XString* key = (XString*)XVector_at_base(keys, i);
        if (!key) 
            continue;

        // 序列化键
        XString_push_back_base(output, XChar_from('\"'));
        XJson_escape_string(key, output);
        XString_push_back_base(output, XChar_from('\"'));
        XString_push_back_base(output, XChar_from(':'));
        // 序列化值
        const XJsonValue* value = XJsonObject_value_base(object, key);
        XJson_serialize_json_value(value, output);

        // 添加逗号分隔（最后一个键值对除外）
        if (i != keyCount - 1) {
            XString_push_back_base(output, XChar_from(','));
        }
    }

    //内置了释放数据的方法
    XVector_delete_base(keys);

    // 结束对象
    XString_push_back_base(output, XChar_from('}'));

    return output;
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
