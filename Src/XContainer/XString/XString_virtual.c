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
    // 检查参数有效性（str和it为必需参数，next可空）
    if (!str || !it) {
        return;
    }

    // 初始化下一个迭代器（若next非空）
    if (next != NULL) 
    {
        *next = XString_end(str);
    }

    // 检查迭代器数据有效性
    if (!it->data)
    {
        return;
    }

    // 获取字符串数据起始地址
    XChar* data = (XChar*)XContainerDataPtr(str);
    if (!data) 
    {
        return;
    }

    // 计算当前迭代器指向的字符位置
    size_t pos = ((XChar*)it->data) - data;
    size_t current_len = XString_length_base(str);

    // 检查位置是否有效
    if (pos >= current_len) {
        return;
    }

    // 执行删除操作（删除当前位置的1个字符）
    if (!XString_remove_base(str, pos, 1)) {
        return;
    }

    // 若不需要返回下一个迭代器，直接返回
    if (next == NULL) {
        return;
    }

    // 删除后获取新的字符串数据和长度
    XChar* new_data = (XChar*)XContainerDataPtr(str);
    size_t new_len = XString_length_base(str);

    // 设置下一个迭代器位置
    if (new_data && pos < new_len) {
        // 未到达末尾时，下一个位置为当前位置（原后续字符已前移）
        next->data = new_data + pos;
    }
    else {
        // 到达末尾时，下一个迭代器为结束迭代器
        *next = XString_end(str);
    }
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
    if (object->m_ref_count)
        XMemory_free(object->m_ref_count);
    object->m_ref_count = src->m_ref_count;
    if (object->m_ref_count) {
        XAtomic_fetch_add_int32(object->m_ref_count, 1, XAtomic_MemoryOrder_Relaxed);  // 原子加1
    }
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
    XSwap((XClass*)object + 1, (XClass*)src + 1, sizeof(XString) - sizeof(XClass));
    //memset((XClass*)src, 0, sizeof(XString) - sizeof(XClass));
}

// 类方法：销毁
static void VXClass_deinit(XString* str) 
{
    if (!str) return;

    if (str->m_ref_count) 
    {
        // 原子减少原引用计数（若减到0，原数据会被其他持有者释放）
        if (XAtomic_fetch_sub_int32(str->m_ref_count, 1, XAtomic_MemoryOrder_Relaxed) == 0)
        {
            if (XContainerDataPtr(str)) 
            {
                XMemory_free(XContainerDataPtr(str));
            }
            XMemory_free(str->m_ref_count);
            
        }
        str->m_ref_count = NULL;
        XContainerDataPtr(str) = NULL;
    }

    //释放缓存
    if (str->m_cache)
    {
        XString_deinitCache(str);
        XMemory_free(str->m_cache);
        str->m_cache = NULL;
    }

    XContainerDataPtr(str) = NULL;
    XContainerSize(str) = 0;
    XContainerCapacity(str) =0;
    //XVtableGetFunc(XContainer_class_init(), EXClass_Deinit, void(*)(XClass*))((XClass*)str);
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

    //XString_detach(str);
    
    if (XAtomic_load_int32(str->m_ref_count, XAtomic_MemoryOrder_Relaxed) >1)
    {//被其他对象拷贝引用了,将缓冲区交给其他对象管理
        int32_t old_ref = XAtomic_fetch_sub_int32(str->m_ref_count, 1, XAtomic_MemoryOrder_Relaxed);
        // 为新数据创建独立的引用计数（初始化为1）
        XAtomic_int32_t* new_ref_count = (XAtomic_int32_t*)XMemory_malloc(sizeof(XAtomic_int32_t));
        if (!new_ref_count) {
            // 恢复原引用计数（拷贝失败需回滚）
            XAtomic_fetch_add_int32(str->m_ref_count, 1, XAtomic_MemoryOrder_Relaxed);
            return;
        }
        XAtomic_store_int32(new_ref_count, 1, XAtomic_MemoryOrder_Relaxed);  // 原子初始化新计数
        str->m_ref_count = new_ref_count;
        XContainerDataPtr(str) = XMemory_malloc(sizeof(XChar)*(XContainerCapacity(str)+1));
    }
    if (XContainerDataPtr(str)) 
    {
        XContainerSize(str) = 0;
        ((XChar*)XContainerDataPtr(str))[XString_length_base(str)] = 0;
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

        XVTABLE_INHERIT_DEFAULT(XContainer_class_init());

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