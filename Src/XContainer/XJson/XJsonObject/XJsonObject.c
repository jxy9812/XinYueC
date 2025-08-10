#include "XJsonObject.h"
#include "XJsonArray.h"
#include "XMemory.h"
#include "XMap.h"
#include "XVector.h"
#include "XStack.h"
// 辅助函数：序列化XJsonValue
void XJson_serialize_json_value(const XJsonValue* value, XString* output, XJsonDocumentFormat format, XStack* stack);
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


// 对象序列化实现
XString* XJsonObject_toString(const XJsonObject* object, XJsonDocumentFormat format, XStack* stack)
{
    if (!object || XJsonObject_isEmpty_base(object))
    {
        return XString_create_utf8("{}");
    }

    XString* output = XString_create(NULL);
    if (!output) return NULL;

    // 初始化栈（顶层调用时）
    bool is_top_level = false;
    if (!stack) {
        stack = XStack_Create(int);
        is_top_level = true;
        XStack_Push_Base(stack, int,0); // 压入初始深度0
    }

    // 获取当前缩进深度
    int current_depth =XStack_Top_Base(stack,int);
    XString* indent = XString_create(NULL);

    // 生成当前层级缩进字符串（4个空格为一层）
    if (format == XJsonDocument_Indented) {
        for (int i = 0; i < current_depth; i++) {
            XString_append_utf8(indent, "    ");
        }
    }

    // 写入对象开始符
    //XString_append(output, indent); // 缩进后写入{
    XString_push_back_base(output, XChar_from('{'));

    // 处理缩进格式：换行并增加层级
    if (format == XJsonDocument_Indented) {
        XString_push_back_base(output, XChar_from('\n'));
        XStack_Push_Base(stack, int, current_depth + 1); // 子层级深度+1
    }

    // 获取所有键并遍历
    XVector* keys = XJsonObject_keys_base(object);
    size_t keyCount = XVector_size_base(keys);

    for (int i = 0; i < keyCount; i++) {
        XString* key = (XString*)XVector_at_base(keys, i);
        if (!key) continue;

        // 子元素缩进
        if (format == XJsonDocument_Indented) {
            XString* child_indent = XString_create(NULL);
            int child_depth = XStack_Top_Base(stack,int);
            for (int j = 0; j < child_depth; j++) {
                XString_append_utf8(child_indent, "    ");
            }
            XString_append(output, child_indent);
            XString_delete_base(child_indent);
        }

        // 序列化键（带引号和转义）
        XString_push_back_base(output, XChar_from('\"'));
        XJson_escape_string(key, output);
        XString_push_back_base(output, XChar_from('\"'));
        XString_push_back_base(output, XChar_from(':'));

        // 冒号后加空格（缩进格式）
        if (format == XJsonDocument_Indented) {
            XString_push_back_base(output, XChar_from(' '));
        }

        // 序列化值（传递栈以处理嵌套）
        const XJsonValue* value = XJsonObject_value_base(object, key);
        XJson_serialize_json_value(value, output, format, stack);

        // 非最后一个元素加逗号
        if (i != keyCount - 1) {
            XString_push_back_base(output, XChar_from(','));
        }

        // 缩进格式下换行
        if (format == XJsonDocument_Indented) {
            XString_append_utf8(output, "\n");
        }
    }

    XVector_delete_base(keys);

    // 处理对象结束符
    if (format == XJsonDocument_Indented) {
        XStack_pop_base(stack); // 恢复父层级深度
        current_depth =XStack_Top_Base(stack,int);

        // 结束符缩进
        XString* closing_indent = XString_create(NULL);
        for (int i = 0; i < current_depth; i++) {
            XString_append_utf8(closing_indent, "    ");
        }
        XString_append(output, closing_indent);
        XString_delete_base(closing_indent);
    }

    XString_push_back_base(output, XChar_from('}'));
    XString_delete_base(indent);

    // 顶层调用时销毁栈
    if (is_top_level) 
    {
        XStack_delete_base(stack);
    }

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
