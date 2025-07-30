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
// 获取可修改的内部XChar数组
XChar* XString_data(XString* str);

// 前向声明
static uint32_t VXString_At(const XString* str, size_t index);
static bool VXString_Append(XString* str, const char* utf8_str);
static bool VXString_PushBack(XString* str, XChar ch);
static bool VXString_PopBack(XString* str);
static bool VXString_PushFront(XString* str, XChar ch);
static bool VXString_PopFront(XString* str);
static bool VXString_Assign(XString* str, const char* utf8_str);
static bool VXString_Prepend(XString* str, const char* utf8_str);
static bool VXString_Insert(XString* str, size_t pos, const char* utf8_str);
static bool VXString_Remove(XString* str, size_t pos, size_t len);
static bool VXString_Replace(XString* str, const char* before, const char* after);
static int64_t VXString_IndexOf(const XString* str, const char* substr, size_t from);
static int64_t VXString_LastIndexOf(const XString* str, const char* substr, size_t from);
static bool VXString_StartsWith(const XString* str, const char* prefix);
static bool VXString_EndsWith(const XString* str, const char* suffix);
static void VXClass_copy(XString* object, const XString* src);
static void VXClass_move(XString* object, XString* src);
static void VXClass_deinit(XString* str);
static void VXContainerObject_clear(XString* str);

// 内部辅助函数
static const XChar* XString_cdata(const XString* str) {
    return (const XChar*)XContainerDataPtr(str);
}

// 1. 获取指定位置的Unicode码点
static uint32_t VXString_At(const XString* str, size_t index) {
    if (!str || index >= XString_length_base(str)) return 0;

    const XChar* xchars = XString_cdata(str);
    // 处理代理对（高代理+低代理）
    if (XChar_is_high_surrogate(&xchars[index]) &&
        (index + 1 < XString_length_base(str)) &&
        XChar_is_low_surrogate(&xchars[index + 1])) {
        return XChar_surrogate_to_unicode(&xchars[index], &xchars[index + 1]);
    }
    return XChar_unicode(&xchars[index]);
}

// 2. 追加UTF-8字符串
static bool VXString_Append(XString* str, const char* utf8_str) 
{
    if (!str || !utf8_str) return false;

    XChar temp[1024];
    int xchar_count = XChar_from_utf8((const uint8_t*)utf8_str, temp, 1023);
    if (xchar_count <= 0) return false;

    XString_detach(str);
    size_t new_size = XString_length_base(str) + xchar_count;
    XString_reserve(str, new_size);

    XChar* data = XString_data(str);
    memcpy(data + XString_length_base(str), temp, xchar_count * sizeof(XChar));
    
    XContainerSize(str)=new_size;
    XString_data(str)[new_size].code=0;

    XString_deinitCache(str);
    return true;
}

// 3. 尾插单个XChar
static bool VXString_PushBack(XString* str, XChar ch) {
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

// 7. 替换为指定UTF-8字符串
static bool VXString_Assign(XString* str, const char* utf8_str) {
    if (!str) return false;

    XString_clear_base(str);
    if (!utf8_str || *utf8_str == '\0') return true;

    XChar temp[1024];
    int xchar_count = XChar_from_utf8((const uint8_t*)utf8_str, temp, 1023);
    if (xchar_count <= 0) return false;

    XString_detach(str);
    XString_reserve(str, xchar_count);

    XChar* data = XString_data(str);
    memcpy(data, temp, xchar_count * sizeof(XChar));

    XContainerSize(str) = xchar_count;
    XString_data(str)[xchar_count].code = 0;

    XString_deinitCache(str);
    return true;
}

// 8. 前置添加UTF-8字符串
static bool VXString_Prepend(XString* str, const char* utf8_str) {
    if (!str || !utf8_str) return false;

    XString* original = XString_copy(str);
    if (!original) return false;

    XString_clear_base(str);
    if (!XString_append_base(str, utf8_str)) {
        XString_delete_base(original);
        return false;
    }

    bool success = XString_append_base(str, XString_toUtf8(original));
    XString_delete_base(original);
    return success;
}

// 9. 在指定位置插入UTF-8字符串
static bool VXString_Insert(XString* str, size_t pos, const char* utf8_str) {
    if (!str || !utf8_str || pos > XString_length_base(str)) return false;

    XString* insert_str = XString_create(utf8_str);
    if (!insert_str) return false;
    size_t insert_len = XString_length_base(insert_str);
    if (insert_len == 0) {
        XString_delete_base(insert_str);
        return true;
    }

    XString_detach(str);
    size_t original_size = XString_length_base(str);
    size_t new_size = original_size + insert_len;

    XString_reserve(str, new_size);
    XChar* data = XString_data(str);

    memmove(data + pos + insert_len, data + pos, (original_size - pos) * sizeof(XChar));
    memcpy(data + pos, XString_cdata(insert_str), insert_len * sizeof(XChar));

    XContainerSize(str) = new_size;
    XString_data(str)[new_size].code = 0;

    XString_deinitCache(str);

    XString_delete_base(insert_str);
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

// 11. 替换子串
static bool VXString_Replace(XString* str, const char* before, const char* after) {
    if (!str || !before || !after) return false;

    XString* before_str = XString_create(before);
    XString* after_str = XString_create(after);
    if (!before_str || !after_str) {
        XString_delete_base(before_str);
        XString_delete_base(after_str);
        return false;
    }

    size_t before_len = XString_length_base(before_str);
    size_t after_len = XString_length_base(after_str);
    if (before_len == 0) {
        XString_delete_base(before_str);
        XString_delete_base(after_str);
        return false;
    }

    int64_t pos = XString_index_of(str, before, 0);
    while (pos != -1) {
        if (!XString_remove(str, (size_t)pos, before_len)) break;
        if (!XString_insert(str, (size_t)pos, after)) break;
        pos = XString_index_of(str, before, (size_t)pos + after_len);
    }

    XString_delete_base(before_str);
    XString_delete_base(after_str);
    return true;
}

// 12. 查找子串首次出现位置
static int64_t VXString_IndexOf(const XString* str, const char* substr, size_t from) {
    if (!str || !substr || from >= XString_length_base(str)) return -1;

    XString* substr_str = XString_create(substr);
    if (!substr_str || XString_isEmpty_base(substr_str)) {
        XString_delete_base(substr_str);
        return (from <= XString_length_base(str)) ? (int64_t)from : -1;
    }

    size_t str_len = XString_length_base(str);
    size_t substr_len = XString_length_base(substr_str);
    const XChar* str_data = XString_cdata(str);
    const XChar* substr_data = XString_cdata(substr_str);

    for (size_t i = from; i <= str_len - substr_len; i++) {
        bool match = true;
        for (size_t j = 0; j < substr_len; j++) {
            if (str_data[i + j].code != substr_data[j].code) {
                match = false;
                break;
            }
        }
        if (match) {
            XString_delete_base(substr_str);
            return (int64_t)i;
        }
    }

    XString_delete_base(substr_str);
    return -1;
}

// 13. 查找子串最后出现位置
static int64_t VXString_LastIndexOf(const XString* str, const char* substr, size_t from) {
    if (!str || !substr) return -1;

    XString* substr_str = XString_create(substr);
    if (!substr_str || XString_isEmpty_base(substr_str)) {
        XString_delete_base(substr_str);
        return (int64_t)XString_length_base(str);
    }

    size_t str_len = XString_length_base(str);
    size_t substr_len = XString_length_base(substr_str);
    if (substr_len > str_len) {
        XString_delete_base(substr_str);
        return -1;
    }

    size_t end = (from >= str_len) ? (str_len - substr_len) : from;
    const XChar* str_data = XString_cdata(str);
    const XChar* substr_data = XString_cdata(substr_str);

    for (int64_t i = (int64_t)end; i >= 0; i--) {
        bool match = true;
        for (size_t j = 0; j < substr_len; j++) {
            if (str_data[i + j].code != substr_data[j].code) {
                match = false;
                break;
            }
        }
        if (match) {
            XString_delete_base(substr_str);
            return i;
        }
    }

    XString_delete_base(substr_str);
    return -1;
}

// 16. 判断以指定前缀开始
static bool VXString_StartsWith(const XString* str, const char* prefix) {
    if (!str || !prefix) return false;

    XString* prefix_str = XString_create(prefix);
    if (!prefix_str) return false;

    bool result = (XString_length_base(prefix_str) <= XString_length_base(str)) &&
        (VXString_IndexOf(str, prefix, 0) == 0);
    XString_delete_base(prefix_str);
    return result;
}

// 17. 判断以指定后缀结束
static bool VXString_EndsWith(const XString* str, const char* suffix) {
    if (!str || !suffix) return false;

    XString* suffix_str = XString_create(suffix);
    if (!suffix_str) return false;

    size_t str_len = XString_length_base(str);
    size_t suffix_len = XString_length_base(suffix_str);
    bool result = (suffix_len <= str_len) &&
        (VXString_LastIndexOf(str, suffix, str_len - suffix_len) == (int64_t)(str_len - suffix_len));

    XString_delete_base(suffix_str);
    return result;
}

// 类方法：拷贝
static void VXClass_copy(XString* object, const XString* src)
{
    //printf("拷贝\n");
    if (((XClass*)object)->m_vtable == NULL)
    {
        XString_init(object, NULL, 0);
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
        XString_init(object,NULL,0);
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

// 字符串创建函数
XString* XString_create(const char* utf8_str) {
    return XString_create_with_length(utf8_str, utf8_str ? strlen(utf8_str) : 0);
}

XString* XString_create_with_length(const char* utf8_str, size_t len) {
    XString* str = (XString*)XMemory_malloc(sizeof(XString));
    if (!str) return NULL;
    XString_init(str, utf8_str, len);
    return str;
}

XString* XString_create_fmt(const char* format, ...) {
    if (!format) return NULL;

    va_list args;
    va_start(args, format);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    return XString_create(buf);
}

XString* XString_create_unicode(uint32_t code_point) {
    XString* str = XString_create("");
    if (!str) return NULL;

    XString_detach(str);
    XString_reserve(str, 2);

    XChar* data = XString_data(str);
    if (code_point <= 0xFFFF) {
        data[0] = XChar_from_unicode(code_point);
        str->parent.m_size = 1;
    }
    else {
        data[0] = XChar_from_unicode(code_point);
        data[1] = XChar_from_unicode_low(code_point);
        str->parent.m_size = 2;
    }

    return str;
}

XString* XString_copy(const XString* other) {
    if (!other) return NULL;

    XString* str = (XString*)XMemory_malloc(sizeof(XString));
    if (!str) return NULL;

    VXClass_copy(str, other);
    return str;
}

// 初始化函数
void XString_init(XString* str, const char* utf8_str, size_t len) {
    if (!str) return;
    size_t actual_len = (len == 0 && utf8_str) ? strlen(utf8_str) : len;

    XContainerObject_init(&str->parent, sizeof(XChar));
    str->m_ref_count = (int*)XMemory_malloc(sizeof(int));
    *str->m_ref_count = 1;
    str->m_is_shared = false;
    str->m_cache = NULL;

    XClassGetVtable((XClass*)str) = XString_class_init();

    if (utf8_str && actual_len > 0) 
    {
        int xchar_count = XChar_from_utf8((const uint8_t*)utf8_str, NULL, 0);
        if (xchar_count > 0) {
            XString_reserve(str, xchar_count);
            xchar_count = XChar_from_utf8((const uint8_t*)utf8_str, XString_data(str), xchar_count+1);
            str->parent.m_size = xchar_count;
        }
    }
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
        VXString_Append,
        VXString_PushBack,
        VXString_PopBack,
        VXString_PushFront,
        VXString_PopFront,
        VXString_Assign,
        VXString_Prepend,
        VXString_Insert,
        VXString_Remove,
        VXString_Replace,
        VXString_IndexOf,
        VXString_LastIndexOf,
        VXString_StartsWith,
        VXString_EndsWith
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