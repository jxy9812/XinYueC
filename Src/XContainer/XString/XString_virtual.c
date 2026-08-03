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
#define XString_cdata(str) ((const XChar*)XContainerDataAddr(str))

/**
 * @brief 分离共享数据（Copy-On-Write 机制）
 * @details 当字符串数据被共享时，复制一份独立数据供修改，避免影响其他对象
 * @param str XString 对象指针
 */
void XString_detach(XString* str);

/**
 * @brief 释放所有编码缓存
 * @param str XString 对象指针
 */
void XString_deinitCache(XString* str);

/**
 * @brief 删除字符串数据（用于 XSharedData 回调）
 */
static void VXStringDataDelete(void* data, XString* str);

// 获取可修改的内部XChar数组
XChar* XString_data(XString* str);

// 前向声明
static XChar VXString_At(const XString* str, size_t index);
static bool VXString_PushBack(XString* str, XChar ch);
static bool VXString_PopBack(XString* str);
static bool VXString_PushFront(XString* str, XChar ch);
static bool VXString_PopFront(XString* str);
static bool VXString_Remove(XString* str, size_t pos, size_t len);
static void VXString_Erase(XString* str, const XString_iterator* it, XString_iterator* next);
static void VXClass_copy(XString* object, const XString* src);
static void VXClass_move(XString* object, XString* src);
static void VXClass_deinit(XString* str);
static void VXContainer_clear(XString* str);

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
    XString_data(str)[new_size]=0;

    XString_deinitCache(str);
    return true;
}

// 4. 尾删一个字符
static bool VXString_PopBack(XString* str) 
{
    if (!str || XString_isEmpty_base(str)) return false;

    XString_detach(str);

    XContainerSize(str) -= 1;
    XString_data(str)[XContainerSize(str)]=0;

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
    XString_data(str)[new_size]=0;

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
    XString_data(str)[new_size]=0;

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
    XString_data(str)[new_size]=0;

    XString_deinitCache(str);
    return true;
}

void VXString_Erase(XString* str, const XString_iterator* it, XString_iterator* next)
{
    // 参数有效性检查
    if (!str || !it) {
        return;
    }
    if (next != NULL) {
        *next = XString_end(str);
    }

    // 获取数据区指针和长度
    const XChar* data = XString_cdata(str);
    if (!data) {
        return;
    }
    size_t current_len = XString_length_base(str);

    // 计算迭代器指向的字符索引（整数，不依赖指针有效性）
    size_t pos = (const XChar*)it->data - data;
    if (pos >= current_len) {
        return;
    }

    // 执行删除（内部会分离并可能重新分配内存）
    if (!XString_remove_base(str, pos, 1)) {
        return;
    }

    // 如果需要返回下一个迭代器
    if (next != NULL) {
        const XChar* new_data = XString_cdata(str);
        size_t new_len = XString_length_base(str);
        if (new_data && pos < new_len) {
            // 下一个字符在相同索引位置（原 pos+1 已前移）
            next->data = (void*)(new_data + pos);
        }
        else {
            *next = XString_end(str);
        }
    }
}

// 删除字符串数据（用于 XSharedData 回调）
static void VXStringDataDelete(void* data, XString* str)
{
    if (data == NULL || str == NULL)
        return;

    //XFree_System(data);
    XContainerSize(str) = 0;
    XContainerCapacity(str) = 0;
    XContainerSetDataPtr(str, NULL);

    // 释放缓存
    if (str->m_cache)
    {
        XString_deinitCache(str);
        XFree_System(str->m_cache);
        str->m_cache = NULL;
    }
}

// 类方法：拷贝
static void VXClass_copy(XString* object, const XString* src)
{
    if (XClassIsVtableNull(object))
    {
        XString_init(object);
    }
    else if ((XSharedData*)XContainerDataPtr(object))
    {
        XSharedData_release_with((XSharedData*)XContainerDataPtr(object), VXStringDataDelete, object);
    }

    // 共享源数据的 XSharedData（COW 机制）
    XContainerSetDataPtr(object, (XSharedData*)XContainerDataPtr(src));
    if ((XSharedData*)XContainerDataPtr(object))
    {
        XSharedData_addRef((XSharedData*)XContainerDataPtr(object));
    }

    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
    object->m_cache = NULL;
}

// 类方法：移动
static void VXClass_move(XString* object, XString* src) 
{
    if (XClassIsVtableNull(object))
    {
        XString_init(object);
    }
    else if ((XSharedData*)XContainerDataPtr(object))
    {
        XSharedData_release_with((XSharedData*)XContainerDataPtr(object), VXStringDataDelete, object);
    }

    // 转移资源所有权
    XContainerSetDataPtr(object, (XSharedData*)XContainerDataPtr(src));
    XContainerSize(object) = XContainerSize(src);
    XContainerCapacity(object) = XContainerCapacity(src);
    object->m_cache = src->m_cache;

    // 清空源对象（使其处于有效但为空的状态）
    XContainerSetDataPtr(src, NULL);
    XContainerSize(src) = 0;
    XContainerCapacity(src) = 0;
    src->m_cache = NULL;
}

// 类方法：销毁
static void VXClass_deinit(XString* str) 
{
    if (!str) return;
    if((XSharedData*)XContainerDataPtr(str))
        XSharedData_release_with((XSharedData*)XContainerDataPtr(str), VXStringDataDelete, str);
    XContainerSize(str) = 0;
    XContainerCapacity(str) = 0;
    XContainerSetDataPtr(str, NULL);

    //释放缓存
    if (str->m_cache)
    {
        XString_deinitCache(str);
        XFree_System(str->m_cache);
        str->m_cache = NULL;
    }
}

// 容器方法：清空
static void VXContainer_clear(XString* str)
{
    if (!str) return;

    // 无需操作空字符串
    if (XString_isEmpty_base(str)) 
    {
        XString_deinitCache(str); // 仅清理缓存
        return;
    }

    // 如果数据被共享，减少引用并创建空数据
    if ((XSharedData*)XContainerDataPtr(str) && XSharedData_isShared((XSharedData*)XContainerDataPtr(str)))
    {
        XSharedData_release((XSharedData*)XContainerDataPtr(str));

                // 创建新的空数据（一次分配）
        size_t bytes = sizeof(XChar) * (XSTRING_MIN_CAPACITY + 1);
        XSharedData* newShared = XSharedData_create(NULL, bytes);
        if (newShared)
        {
            memset(newShared->data, 0, bytes);
            XContainerSetDataPtr(str, newShared);
            XContainerCapacity(str) = XSTRING_MIN_CAPACITY;
        }
        else
        {
            XContainerSetDataPtr(str, NULL);
            XContainerCapacity(str) = 0;
        }
        XContainerSize(str) = 0;
        XString_deinitCache(str);
        return;
    }

    // 不共享，直接清空数据
    if (XString_data(str))
    {
        XContainerSize(str) = 0;
        XString_data(str)[0] = 0;
    }

    XString_deinitCache(str);
}

// 虚函数表初始化
XVtable* XString_class_init() {
    XVTABLE_CREAT_DEFAULT

#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XString)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif

        XVTABLE_INHERIT_XCLASS(XContainer);

    void* vtable_funcs[] = {
        VXString_At,
        VXString_PushBack,
        VXString_PopBack,
        VXString_PushFront,
        VXString_PopFront,
        VXString_Remove,
        VXString_Erase,
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(vtable_funcs);

    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClass_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXContainer_Clear, VXContainer_clear);

#if SHOWCONTAINERSIZE
    printf("XString vtable size: %d\n", XSTRING_VTABLE_SIZE);
#endif

    return XVTABLE_DEFAULT;
}


#endif
