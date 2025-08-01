#include"XString.h"
#if XString_ON
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include "XAlgorithm.h"
// 内部常量定义
#define UTF8_CACHE_SIZE 1024  // 初始UTF-8缓存大小
#define XSTRING_MIN_CAPACITY 16  // 最小容量
#define XString_cdata(str) ((const XChar*)XContainerDataPtr(str))
// 获取可修改的内部XChar数组
XChar* XString_data(XString* str);
XString* XString_copy(const XString* other);

// 前向声明
static XChar VXString_At(const XString* str, size_t index);
static bool VXString_PushBack(XString* str, XChar ch);
static bool VXString_PopBack(XString* str);
static bool VXString_PushFront(XString* str, XChar ch);
static bool VXString_PopFront(XString* str);
static bool VXString_Remove(XString* str, size_t pos, size_t len);
static void VXClass_copy(XString* object, const XString* src);
static void VXClass_move(XString* object, XString* src);
static void VXClass_deinit(XString* str);
static void VXContainerObject_clear(XString* str);

// 1. 获取指定位置的Unicode码点
static XChar VXString_At(const XString* str, size_t index)
{
    if (!str || index >= XString_length_base(str)) return XChar_from(0);

    const XChar* xchars = XString_cdata(str);
    return xchars[index];
    //// 处理代理对（高代理+低代理）
    //if (XChar_is_high_surrogate(&xchars[index]) &&
    //    (index + 1 < XString_length_base(str)) &&
    //    XChar_is_low_surrogate(&xchars[index + 1])) {
    //    return XChar_surrogate_to_unicode(&xchars[index], &xchars[index + 1]);
    //}
    //return XChar_unicode(&xchars[index]);
}
// 3. 尾插单个XChar
static bool VXString_PushBack(XString* str, XChar ch) 
{
    if (!str) return false;

    XString_detach(str);
    size_t new_size = XString_length_base(str) + 1;
    XString_reserve(str, new_size);

    XChar* data = XString_data(str);
    data[XString_length_base(str)] = ch;

    XContainerSize(str) = new_size;
    XString_data(str)[new_size].code = 0;

    XString_deinitCache(str);
    return true;
}

// 4. 尾删一个字符
static bool VXString_PopBack(XString* str) 
{
    if (!str || XString_isEmpty_base(str)) return false;

    XString_detach(str);

    XContainerSize(str) -= 1;
    XString_data(str)[XContainerSize(str)].code = 0;

    XString_deinitCache(str);
    return true;
}

// 5. 头插单个XChar
static bool VXString_PushFront(XString* str, XChar ch) {
    if (!str) return false;

    XString_detach(str);
    size_t new_size = XString_length_base(str) + 1;
    XString_reserve(str, new_size);

    XChar* data = XString_data(str);
    memmove(data + 1, data, XString_length_base(str) * sizeof(XChar));
    data[0] = ch;

    XContainerSize(str) = new_size;
    XString_data(str)[new_size].code = 0;

    XString_deinitCache(str);
    return true;
}

// 6. 头删一个字符
static bool VXString_PopFront(XString* str) {
    if (!str || XString_isEmpty_base(str)) return false;

    XString_detach(str);
    size_t new_size = XString_length_base(str) - 1;

    XChar* data = XString_data(str);
    memmove(data, data + 1, new_size * sizeof(XChar));

    XContainerSize(str) = new_size;
    XString_data(str)[new_size].code = 0;

    XString_deinitCache(str);
    return true;
}

// 10. 移除指定范围字符
static bool VXString_Remove(XString* str, size_t pos, size_t len) {
    if (!str || pos >= XString_length_base(str) || len == 0) return false;

    XString_detach(str);
    size_t actual_len = (pos + len > XString_length_base(str))
        ? (XString_length_base(str) - pos)
        : len;
    size_t new_size = XString_length_base(str) - actual_len;

    XChar* data = XString_data(str);
    memmove(data + pos, data + pos + actual_len, (new_size - pos) * sizeof(XChar));

    XContainerSize(str) = new_size;
    XString_data(str)[new_size].code = 0;

    XString_deinitCache(str);
    return true;
}

// 类方法：拷贝
static void VXClass_copy(XString* object, const XString* src)
{
    //printf("拷贝\n");
    if (((XClass*)object)->m_vtable == NULL)
    {
        XString_init(object);
    }
    else if (!XString_isEmpty_base(object))
    {
        XString_clear_base(object);
    }
    XContainerDataPtr(object)= XContainerDataPtr(src);
    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
    object->m_ref_count = src->m_ref_count;
    *(object->m_ref_count) += 1;

    object->m_is_shared = true;
    object->m_cache = NULL;
}

// 类方法：移动
static void VXClass_move(XString* object, XString* src) 
{
    if (((XClass*)object)->m_vtable == NULL)
    {
        XString_init(object);
    }
    else if( !XString_isEmpty_base(object))
    {
        XString_clear_base(object);
    }
    XSwap(object, src, sizeof(XString));
   /// memset(src, 0, sizeof(XString));
}

// 类方法：销毁
static void VXClass_deinit(XString* str) {
    if (!str) return;

    if (str->m_ref_count) 
    {
        *(str->m_ref_count) -= 1;
        if (*(str->m_ref_count) == 0) 
        {
            if (XContainerDataPtr(str)) 
            {
                XMemory_free(XContainerDataPtr(str));
                XContainerDataPtr(str) = NULL;
            }
            XMemory_free(str->m_ref_count);
            str->m_ref_count = NULL;
        }
    }

    //释放缓存
    if (str->m_cache)
    {
        XString_deinitCache(str);
        XMemory_free(str->m_cache);
        str->m_cache = NULL;
    }
    XVtableGetFunc(XContainerObject_class_init(), EXClass_Deinit, void(*)(XClass*))((XClass*)str);
}

// 容器方法：清空
static void VXContainerObject_clear(XString* str) {
    if (!str) return;

    XString_detach(str);

    if (XContainerDataPtr(str)) {
        XMemory_free(XContainerDataPtr(str));
        XContainerDataPtr(str) = NULL;
        XContainerSize(str) = 0;
        XContainerCapacity(str) = 0;
    }

    XString_deinitCache(str);
}

// 虚函数表初始化
XVtable* XString_class_init() {
    XVTABLE_CREAT_DEFAULT

#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XSTRING_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif

        XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());

    void* vtable_funcs[] = {
        VXString_At,
        VXString_PushBack,
        VXString_PopBack,
        VXString_PushFront,
        VXString_PopFront,
        VXString_Remove,
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(vtable_funcs);

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClass_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear, VXContainerObject_clear);

#if SHOWCONTAINERSIZE
    printf("XString vtable size: %d\n", XSTRING_VTABLE_SIZE);
#endif

    return XVTABLE_DEFAULT;
}

#endif