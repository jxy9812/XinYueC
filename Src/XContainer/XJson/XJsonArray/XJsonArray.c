#include "XJsonArray.h"
#include "XMemory.h"
#include "XVariantList.h"
#include "XJsonValue.h"
#include "XStack.h"
// 辅助函数：序列化XJsonValue
void XJson_serialize_json_value(const XJsonValue* value, XString* output, XJsonDocumentFormat format, XStack* stack);

XJsonArray* XJsonArray_create()
{
    XJsonArray* array = (XJsonArray*)XMemory_malloc(sizeof(XJsonArray));
    if (array == NULL)
        return NULL;
    XJsonArray_init(array);
    return array;
}

XJsonArray* XJsonArray_create_copy(XJsonArray* copy)
{
    XJsonArray* array = XJsonArray_create();
    if (array&& copy)
        XJsonArray_copy_base(array,copy);
    return array;
}

XJsonArray* XJsonArray_create_move(XJsonArray* move)
{
    XJsonArray* array = XJsonArray_create();
    if (array && move)
        XJsonArray_move_base(array, move);
    return array;
}

void XJsonArray_init(XJsonArray* array)
{
    if (array==NULL)
        return;
    XVector_init(array, sizeof(XJsonValue));
    XContainerSetDataDeinitMethod(array, XJsonValue_deinit);
    XContainerSetDataCopyMethod(array, XJsonValue_copy);
    XContainerSetDataMoveMethod(array, XJsonValue_move);
}

// 数组序列化实现
XString* XJsonArray_toString(const XJsonArray* array, XJsonDocumentFormat format, XStack* stack)
{
    if (!array) return NULL;

    XString* output = XString_create(NULL);
    if (!output) return NULL;

    // 初始化栈（顶层调用时）
    bool is_top_level = false;
    if (!stack) {
        stack = XStack_Create(int);
        is_top_level = true;
        XStack_Push_Base(stack, int,0); // 初始深度0
    }

    // 获取当前缩进深度
    int current_depth = XStack_Top_Base(stack,int);
    XString* indent = XString_create(NULL);

    // 生成当前层级缩进字符串
    if (format == XJsonDocument_Indented) {
        for (int i = 0; i < current_depth; i++) {
            XString_append_utf8(indent, "    ");
        }
    }

    // 写入数组开始符
    //XString_append(output, indent);
    XString_push_back_base(output, XChar_from('['));

    // 处理缩进格式：换行并增加层级
    if (format == XJsonDocument_Indented) {
        XString_push_back_base(output, XChar_from('\n'));
        XStack_Push_Base(stack, int, current_depth + 1); // 子层级深度+1
    }

    // 遍历数组元素
    int size = XJsonArray_size_base(array);
    for (int i = 0; i < size; i++) {
        const XJsonValue* value = XJsonArray_at_const(array, i);

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

        // 序列化元素值
        if (value) {
            XJson_serialize_json_value(value, output, format, stack);
        }
        else {
            XString_append_utf8(output, "null");
        }

        // 非最后一个元素加逗号
        if (i != size - 1) {
            XString_push_back_base(output, XChar_from(','));
        }

        // 缩进格式下换行
        if (format == XJsonDocument_Indented) {
            XString_push_back_base(output, XChar_from('\n'));
        }
    }

    // 处理数组结束符
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

    XString_push_back_base(output, XChar_from(']'));
    XString_delete_base(indent);

    // 顶层调用时销毁栈
    if (is_top_level) {
        XStack_delete_base(stack);
    }

    return output;
}
//XVariantList* XJsonArray_toVariantList(const XJsonArray* array) {
//    if (!array) return NULL;
//
//    XVariantList* list = XVariantList_create();
//    if (!list) return NULL;
//
//    int size = XJsonArray_size(array);
//    for (int i = 0; i < size; i++) {
//        const XJsonValue* jsonVal = XJsonArray_at_const(array, i);
//        XVariant* var = XJsonValue_toVariant(jsonVal);
//        if (var) {
//            XVariantList_push_back_base(list, var);
//        }
//    }
//
//    return list;
//}
//
//XJsonArray* XJsonArray_fromVariantList(const XVariantList* list) {
//    if (!list) return NULL;
//
//    XJsonArray* array = XJsonArray_create();
//    if (!array) return NULL;
//
//    int size = XVariantList_size_base(list);
//    for (int i = 0; i < size; i++) {
//        const XVariant* var = XVariantList_at_base(list, i);
//        XJsonValue* jsonVal = XJsonValue_fromVariant(var);
//        if (jsonVal) {
//            XJsonArray_append(array, jsonVal);
//        }
//    }
//
//    return array;
//}

XJsonValue* XJsonArray_at(XJsonArray* array, int64_t index)
{
    return XVector_at_base(array,index);
}

const XJsonValue* XJsonArray_at_const(const XJsonArray* array, int64_t index)
{
    return XJsonArray_at((XJsonArray*)array, index);
}