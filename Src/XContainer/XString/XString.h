#include "XContainerObject.h"
#if !defined(XSTRING_H) && XString_ON
#define XSTRING_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XChar.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
// XString虚函数表枚举（继承XContainerObject）
XCLASS_DEFINE_BEGING(XString)
XCLASS_DEFINE_ENUM(XString, At) = XCLASS_VTABLE_GET_SIZE(XContainerObject),
XCLASS_DEFINE_ENUM(XString, Append),
XCLASS_DEFINE_ENUM(XString, PushBack),    // 新增：尾插单个XChar
XCLASS_DEFINE_ENUM(XString, PopBack),     // 新增：尾删单个XChar
XCLASS_DEFINE_ENUM(XString, PushFront),   // 新增：头插单个XChar
XCLASS_DEFINE_ENUM(XString, PopFront),    // 新增：头删单个XChar
XCLASS_DEFINE_ENUM(XString, Assign),
XCLASS_DEFINE_ENUM(XString, Prepend),
XCLASS_DEFINE_ENUM(XString, Insert),
XCLASS_DEFINE_ENUM(XString, Remove),
XCLASS_DEFINE_ENUM(XString, Replace),
XCLASS_DEFINE_ENUM(XString, IndexOf),
XCLASS_DEFINE_ENUM(XString, LastIndexOf),
XCLASS_DEFINE_ENUM(XString, Compare),
XCLASS_DEFINE_ENUM(XString, Equals),
XCLASS_DEFINE_ENUM(XString, StartsWith),
XCLASS_DEFINE_ENUM(XString, EndsWith),
XCLASS_DEFINE_END(XString)

#define XSTRING_VTABLE_SIZE XCLASS_VTABLE_GET_SIZE(XString)

        // 字符串结构定义（继承容器基类，内部存储XChar数组）
typedef struct XString 
 {
    XContainerObject parent;  // 继承容器基类（m_data指向XChar数组）
    bool m_is_shared;         // 共享标记（为true时修改需复制数据）
    int* m_ref_count;         // 引用计数（Copy-On-Write机制）
    char* m_utf8_cache;       // UTF-8缓存（延迟生成，提高重复调用效率）
 } XString;

// 构造函数
XString* XString_create(const char* utf8_str);
XString* XString_create_fmt(const char* format, ...);
XString* XString_create_with_length(const char* utf8_str, size_t len);
XString* XString_create_unicode(uint32_t code_point);
XString* XString_copy(const XString* other);
void XString_init(XString* str, const char* utf8_str, size_t len);
#define XString_Init(name,utf8_str)  XString _##name,*name=&_##name;XString_init(name,utf8_str,0)

#define XString_copy_base				    XContainerObject_copy_base	
#define XString_move_base				    XContainerObject_move_base	
#define XString_deinit_base					XContainerObject_deinit_base	
#define XString_delete_base					XContainerObject_delete_base	
#define XString_clear_base				    XContainerObject_clear_base	
#define XString_isEmpty_base				XContainerObject_isEmpty_base	
#define XString_size_base					XContainerObject_size_base	
#define XString_capacity_base				XContainerObject_capacity_base
#define XString_swap_base				    XContainerObject_swap_base	
#define XString_typeSize_base				XContainerObject_typeSize_base

// 基本操作（虚函数包装）
#define XString_length_base                 XContainerObject_size_base
// 获取可修改的内部XChar数组
XChar* XString_data(XString* str);
#define XString_c_str                       XString_to_utf8
const char* XString_to_utf8(const XString* str);
uint32_t XString_at(const XString* str, size_t index);

// 字符串操作（虚函数包装）
bool XString_append_base(XString* str, const char* utf8_str);  // 移除const，需修改原对象
bool XString_assign_base(XString* str, const char* utf8_str);  // 新增：assign API
bool XString_prepend(XString* str, const char* utf8_str);      // 移除const，需修改原对象
bool XString_insert(XString* str, size_t pos, const char* utf8_str);  // 移除const
bool XString_remove(XString* str, size_t pos, size_t len);     // 保持参数，修改返回值
bool XString_replace(XString* str, const char* before, const char* after);  // 移除const
// 在字符串操作部分添加函数声明
bool XString_push_back_base(XString* str, XChar ch);  // 尾插
bool XString_pop_back_base(XString* str);                        // 尾删一个字符
bool XString_push_front_base(XString* str, XChar ch); // 头插
bool XString_pop_front_base(XString* str);                       // 头删一个字符

// 查找操作（虚函数包装）
int64_t XString_index_of(const XString* str, const char* substr, size_t from);
int64_t XString_last_index_of(const XString* str, const char* substr, size_t from);

// 比较操作（虚函数包装）
int XString_compare(const XString* str1, const XString* str2);
bool XString_equals(const XString* str1, const XString* str2);
bool XString_starts_with(const XString* str, const char* prefix);
bool XString_ends_with(const XString* str, const char* suffix);

// 转换操作（虚函数包装）
XString* XString_toLower(const XString* str);
XString* XString_toUpper(const XString* str);
XString* XString_trimmed(const XString* str);
int XString_toInt(const XString* str, bool* ok, int base);
double XString_toDouble(const XString* str, bool* ok);

// 其他功能（虚函数包装）
XString* XString_left(const XString* str, size_t n);
XString* XString_right(const XString* str, size_t n);
XString* XString_mid(const XString* str, size_t pos, size_t n);
void XString_reserve(XString* str, size_t capacity);
// 在字符串操作部分添加函数声明
// 按分隔符拆分字符串，返回字符串数组（需手动释放）
XStringList* XString_split(const XString* str, const char* delimiter);
// 按分隔符拆分字符串（限制最大拆分次数），返回字符串数组（需手动释放）
XStringList* XString_split_limit(const XString* str, const char* delimiter, size_t limit);

// 分离共享数据（Copy-On-Write机制）
void XString_detach(XString* str);

int XPrint(const XString* str);

// 虚函数表初始化
XVtable* XString_class_init();

#ifdef __cplusplus
}
#endif

#endif // XSTRING_H