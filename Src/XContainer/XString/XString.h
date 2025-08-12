#include "XContainerObject.h"
#if !defined(XSTRING_H) && XString_ON
#define XSTRING_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XChar.h"
#include "XString_iterator.h"
#include "XString_reverse_iterator.h"
#include "XAtomic.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
/**
 * @brief XString 类虚函数表枚举（继承自 XContainerObject）
 * @details 定义 XString 类支持的所有虚函数索引，用于虚函数调用机制
 */
XCLASS_DEFINE_BEGING(XString)
XCLASS_DEFINE_ENUM(XString, At) = XCLASS_VTABLE_GET_SIZE(XContainerObject),// 获取指定位置的 XChar 字符
XCLASS_DEFINE_ENUM(XString, PushBack),      // 尾插单个 XChar 字符
XCLASS_DEFINE_ENUM(XString, PopBack),       // 尾删单个字符
XCLASS_DEFINE_ENUM(XString, PushFront),     // 头插单个 XChar 字符
XCLASS_DEFINE_ENUM(XString, PopFront),      // 头删单个字符
XCLASS_DEFINE_ENUM(XString, Remove),        // 移除指定范围的字符
XCLASS_DEFINE_ENUM(XString, Erase),         // 移除指定迭代器字符
XCLASS_DEFINE_END(XString)
#define XSTRING_VTABLE_SIZE XCLASS_VTABLE_GET_SIZE(XString)  // XString 虚函数表大小

/**
 * @brief 字符串缓存类型枚举
 * @details 定义 XString 内部缓存的编码类型，用于避免重复转换
 */
enum XStringCacheType
{
    XStringCache_Local,    // 本地编码缓存（Windows 为 GBK，Linux 为 UTF-8）
    XStringCache_Utf8,     // UTF-8 编码缓存
    XStringCache_Utf16,    // UTF-16 编码缓存（wchar_t 类型）
    XStringCache_Utf32,    // UTF-32 编码缓存（uint32_t 类型）
    XStringCache_Gbk,      // GBK 编码缓存
    XStringCache_Size      // 缓存类型数量（用于数组大小定义）
};
//字符串缓存
typedef struct XStringCache
{
    char* m_data;//字符串数据
    size_t m_length;//长度
}XStringCache;
/**
 * @brief XString 字符串结构体（继承自容器基类）
 * @details 内部以 UTF-16 编码（XChar 数组）存储字符串，支持 Copy-On-Write 机制，
 *          维护多种编码的缓存以优化性能
 */
typedef struct XString
{
    XContainerObject parent;  // 继承容器基类，m_data 指向 XChar 数组（UTF-16 存储）
    XAtomic_int32_t* m_ref_count;         // 引用计数：用于 Copy-On-Write 机制的资源管理
    XStringCache* m_cache;           // 编码缓存数组：存储各类型编码的转换结果（索引对应 XStringCacheType）
} XString;

// -------------------------- 虚函数表初始化 --------------------------

/**
 * @brief 初始化 XString 类的虚函数表
 * @return 虚函数表指针
 */
XVtable* XString_class_init();

// -------------------------- 构造与初始化函数 --------------------------
XString* XString_create();
/**
 * @brief 拷贝引用 XString 对象
 * @param other 输入的XString对象
 * @return 成功返回 XString 指针，失败返回 NULL
 */
XString* XString_create_copy(const XString* other);

XString* XString_create_move(XString* other);
/**
 * @brief 从 UTF-8 字符串创建 XString 对象
 * @param utf8_str 输入的 UTF-8 字符串（NULL 则创建空字符串）
 * @return 成功返回 XString 指针，失败返回 NULL
 */
XString* XString_create_utf8(const char* utf8_str);

/**
 * @brief 从格式化 UTF-8 字符串创建 XString 对象
 * @param utf8_format 格式化字符串（类似 printf）
 * @param ... 可变参数列表
 * @return 成功返回 XString 指针，失败返回 NULL
 */
XString* XString_create_fmt_utf8(const char* utf8_format, ...);

/**
 * @brief 从指定长度的 UTF-8 字符串创建 XString 对象
 * @param utf8_str 输入的 UTF-8 字符串
 * @param len 字符串长度（字节数，不含终止符）
 * @return 成功返回 XString 指针，失败返回 NULL
 */
XString* XString_create_with_length_utf8(const char* utf8_str, size_t len);

/**
 * @brief 从 GBK 字符串创建 XString 对象
 * @param gbk_str 输入的 GBK 字符串（NULL 则创建空字符串）
 * @return 成功返回 XString 指针，失败返回 NULL
 */
XString* XString_create_gbk(const char* gbk_str);

/**
 * @brief 从格式化 GBK 字符串创建 XString 对象
 * @param gbk_format 格式化字符串（类似 printf）
 * @param ... 可变参数列表
 * @return 成功返回 XString 指针，失败返回 NULL
 */
XString* XString_create_fmt_gbk(const char* gbk_format, ...);

/**
 * @brief 从指定长度的 GBK 字符串创建 XString 对象
 * @param gbk_str 输入的 GBK 字符串
 * @param len 字符串长度（字节数，不含终止符）
 * @return 成功返回 XString 指针，失败返回 NULL
 */
XString* XString_create_with_length_gbk(const char* gbk_str, size_t len);

/**
 * @brief 初始化 XString 对象
 * @param str 待初始化的 XString 指针
 * @param utf8_str 初始化用的 UTF-8 字符串（NULL 则初始化为空）
 * @param len 字符串长度（字节数，0 则自动计算）
 */
void XString_init(XString* str);

/**
 * @brief 快速定义并初始化 XString 变量的宏
 * @param name 变量名
 * @param utf8_str 初始化用的 UTF-8 字符串
 *
 * 宏展开后执行以下操作：
 * 1. 声明一个XString类型的实例_##name（通过##连接符生成唯一名称，避免冲突）
 * 2. 声明一个XString指针name，并让它指向实例_##name
 * 3. 调用XString_init初始化该XString对象
 * 4. 调用XString_assign_utf8用格式化字符串给XString赋值
 *
 * 注意：
 * - 该宏在栈上创建XString对象，无需手动调用XMemory_malloc分配内存
 * - 使用完毕后需根据XString的内存管理规则进行清理（如调用XString_deinit等）
 */
#define XString_Init_Utf8(name,utf8_str)  XString _##name,*name=&_##name;XString_init(name);XString_assign_utf8(name,utf8_str)
 /**
  * @brief 定义一个宏，用于快速初始化并格式化赋值XString对象
  * @param name 要创建的XString对象指针名称（宏会自动生成对应的XString实例）
  * @param utf8_format UTF-8格式字符串（用于格式化）
  * @param ... 格式字符串对应的可变参数列表
  *
  * 宏展开后执行以下操作：
  * 1. 声明一个XString类型的实例_##name（通过##连接符生成唯一名称，避免冲突）
  * 2. 声明一个XString指针name，并让它指向实例_##name
  * 3. 调用XString_init初始化该XString对象
  * 4. 调用XString_assign_fmt_utf8用格式化字符串给XString赋值
  *
  * 注意：
  * - 使用##__VA_ARGS__是为了兼容GCC等编译器，在可变参数为空时自动消除多余逗号
  * - 该宏在栈上创建XString对象，无需手动调用XMemory_malloc分配内存
  * - 使用完毕后需根据XString的内存管理规则进行清理（如调用XString_deinit等）
  */
#define XString_Init_Fmt_Utf8(name,utf8_format, ...)     XString _##name,*name=&_##name;XString_init(name);XString_assign_fmt_utf8(name,utf8_format,##__VA_ARGS__)
     // -------------------------- 基础操作宏（继承自 XContainerObject） --------------------------

#define XString_copy_base				    XContainerObject_copy_base	// 复制对象（基础实现）
#define XString_move_base				    XContainerObject_move_base	// 移动对象（基础实现）
#define XString_deinit_base					XContainerObject_deinit_base	// 销毁对象（基础实现）
#define XString_delete_base					XContainerObject_delete_base	// 删除对象（释放内存）
#define XString_clear_base				    XContainerObject_clear_base	// 清空字符串内容
#define XString_isEmpty_base				XContainerObject_isEmpty_base	// 判断字符串是否为空
#define XString_size_base					XContainerObject_size_base	// 获取字符串长度（字符数）
#define XString_capacity_base				XContainerObject_capacity_base	// 获取当前容量（字符数）
#define XString_swap_base				    XContainerObject_swap_base	// 交换两个字符串内容
#define XString_typeSize_base				XContainerObject_typeSize_base	// 获取单个元素（XChar）的大小

/**
 * @brief 获取字符串长度（字符数，不含终止符）
 * @note 等价于 XString_size_base，为字符串场景提供更直观的命名
 */
#define XString_length_base                 XContainerObject_size_base

// -------------------------- 字符串数据访问函数 --------------------------

/**
 * @brief 获取本地编码字符串（兼容 C 风格字符串）
 * @note 等价于 XString_toLocal，为兼容 C 接口提供的宏
 */
#define XString_c_str                       XString_toLocal

/**
 * @brief 获取指定位置的 XChar 字符
 * @param str XString 对象指针
 * @param index 字符索引（从 0 开始）
 * @return 成功返回对应 XChar，失败返回空字符（code=0）
 */
XChar XString_at(const XString* str, size_t index);

/**
 * @brief 获取XString的第一个字符
 * @param str XString对象指针
 * @return 返回第一个XChar字符，如果字符串为空或指针无效则返回空字符
 * @note 与QString::front()行为一致，返回字符串的首字符
 */
XChar XString_front(const XString* str);

/**
 * @brief 获取XString的最后一个字符（类似QString::back()）
 * @param str XString对象指针
 * @return 返回最后一个XChar字符，如果字符串为空则返回空字符
 */
XChar XString_back(const XString* str);
/**
 * @brief 获取内部存储的 Unicode 字符数组（XChar 数组）
 * @param str XString 对象指针
 * @return 常量 XChar 数组指针（以 code=0 终止）
 */
const XChar* XString_unicode(const XString* str);

/**
 * @brief 获取 UTF-16 编码的字符数组（uint16_t 类型）
 * @param str XString 对象指针
 * @return 常量 uint16_t 数组指针（以 0 终止）
 */
const uint16_t* XString_utf16(const XString* str);

// -------------------------- 字符串修改操作函数 --------------------------

/**
 * @brief 追加 XString 字符串到末尾
 * @param str XString 对象指针
 * @param app_str 待追加的 XString字符串
 * @return 成功返回 true，失败返回 false
 */
bool XString_append(XString* str, const XString* app_str);

/**
 * @brief 追加 UTF-8 字符串到末尾
 * @param str XString 对象指针
 * @param utf8_str 待追加的 UTF-8 字符串
 * @return 成功返回 true，失败返回 false
 */
bool XString_append_utf8(XString* str, const char* utf8_str);
bool XString_append_with_length_utf8(XString* str, const char* utf8_str, size_t len);
bool XString_append_char(XString* str,XChar ch);
/**
 * @brief 替换字符串内容为指定 XString 字符串
 * @param str XString 对象指针
 * @param ass_str 替换用的 XString 字符串
 * @return 成功返回 true，失败返回 false
 */
bool XString_assign(XString* str, const XString* ass_str);

/**
 * @brief 替换字符串内容为指定 UTF-8 字符串
 * @param str XString 对象指针
 * @param utf8_str 替换用的 UTF-8 字符串
 * @return 成功返回 true，失败返回 false
 */
bool XString_assign_utf8(XString* str, const char* utf8_str);
/**
 * @brief 将指定长度的UTF-8字符串赋值给XString对象
 * @param str 指向要赋值的XString对象的指针
 * @param utf8_str 待赋值的UTF-8字符串指针
 * @param len 要处理的UTF-8字符串长度（字节数）
 * @return 成功返回true，失败返回false
 * @note 仅处理前len个字节的UTF-8数据，自动忽略不完整的UTF-8序列
 */
bool XString_assign_with_length_utf8(XString* str, const char* utf8_str,size_t len);
/**
 * @brief 使用UTF-8格式字符串格式化赋值给XString
 * @param str 目标XString对象指针
 * @param utf8_format UTF-8格式字符串
 * @param ... 格式字符串对应的可变参数列表
 * @return 成功返回true，失败返回false
 * @note 会覆盖str原有的内容，内部处理内存管理和编码转换
 */
bool XString_assign_fmt_utf8(XString* str, const char* utf8_format, ...);

/**
 * @brief 在字符串开头前置添加 XString 字符串
 * @param str XString 对象指针
 * @param pre_str 待前置的 XString 字符串
 * @return 成功返回 true，失败返回 false
 */
bool XString_prepend(XString* str, const XString* pre_str);

/**
 * @brief 在字符串开头前置添加 UTF-8 字符串
 * @param str XString 对象指针
 * @param utf8_str 待前置的 UTF-8 字符串
 * @return 成功返回 true，失败返回 false
 */
bool XString_prepend_utf8(XString* str, const char* utf8_str);

/**
 * @brief 在指定位置插入 XString 字符串
 * @param str XString 对象指针
 * @param pos 插入位置（0 表示开头，>=长度表示末尾）
 * @param in_str 待插入的 XString 字符串
 * @return 成功返回 true，失败返回 false
 */
bool XString_insert(XString* str, size_t pos, const XString* in_str);

/**
 * @brief 在指定位置插入 UTF-8 字符串
 * @param str XString 对象指针
 * @param pos 插入位置（0 表示开头，>=长度表示末尾）
 * @param utf8_str 待插入的 UTF-8 字符串
 * @return 成功返回 true，失败返回 false
 */
bool XString_insert_utf8(XString* str, size_t pos, const char* utf8_str);

/**
 * @brief 移除指定范围的字符
 * @param str XString 对象指针
 * @param pos 起始位置
 * @param len 移除的字符数
 * @return 成功返回 true，失败返回 false（位置越界或长度为 0）
 */
bool XString_remove_base(XString* str, size_t pos, size_t len);

//删除迭代器数据，并返回下一个迭代器
void XString_erase_base(XString* str, const XString_iterator* it, XString_iterator* next);
/**
 * @brief 替换字符串中的子串
 * @param str XString 对象指针
 * @param before 待替换的子串（XString）
 * @param after 替换后的子串（XString）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回 true，失败返回 false
 */
bool XString_replace(XString* str, const XString* before, const XString* after, XCharCaseSensitivity cs);

/**
 * @brief 替换字符串中的子串
 * @param str XString 对象指针
 * @param before 待替换的子串（UTF-8）
 * @param after 替换后的子串（UTF-8）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回 true，失败返回 false
 */
bool XString_replace_utf8(XString* str, const char* before, const char* after, XCharCaseSensitivity cs);

/**
 * @brief 在字符串末尾插入单个 XChar 字符
 * @param str XString 对象指针
 * @param ch 待插入的 XChar 字符
 * @return 成功返回 true，失败返回 false
 */
bool XString_push_back_base(XString* str, XChar ch);

/**
 * @brief 删除字符串末尾的单个字符
 * @param str XString 对象指针
 * @return 成功返回 true，失败返回 false（字符串为空）
 */
bool XString_pop_back_base(XString* str);

/**
 * @brief 在字符串开头插入单个 XChar 字符
 * @param str XString 对象指针
 * @param ch 待插入的 XChar 字符
 * @return 成功返回 true，失败返回 false
 */
bool XString_push_front_base(XString* str, XChar ch);

/**
 * @brief 删除字符串开头的单个字符
 * @param str XString 对象指针
 * @return 成功返回 true，失败返回 false（字符串为空）
 */
bool XString_pop_front_base(XString* str);

// -------------------------- 字符串查找操作函数 --------------------------

/**
 * @brief 查找子串首次出现的位置
 * @param str XString 对象指针
 * @param substr 待查找的子串（XString 对象指针）
 * @param from 起始查找位置
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回子串起始索引，失败返回 -1
 */
int64_t XString_index_of(const XString* str, const XString* substr, size_t from, XCharCaseSensitivity cs);
/**
 * @brief 查找子串首次出现的位置
 * @param str XString 对象指针
 * @param substr 待查找的子串（UTF-8）
 * @param from 起始查找位置
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回子串起始索引，失败返回 -1
 */
int64_t XString_index_of_utf8(const XString* str, const char* substr, size_t from, XCharCaseSensitivity cs);
/**
 * @brief 查找子串最后一次出现的位置
 * @param str XString 对象指针
 * @param substr 待查找的子串（XString 对象指针）
 * @param from 起始查找位置（从后往前）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回子串起始索引，失败返回 -1
 */
int64_t XString_last_index_of(const XString* str, const XString* substr, size_t from, XCharCaseSensitivity cs);
/**
 * @brief 查找子串最后一次出现的位置
 * @param str XString 对象指针
 * @param substr 待查找的子串（UTF-8）
 * @param from 起始查找位置（从后往前）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回子串起始索引，失败返回 -1
 */
int64_t XString_last_index_of_utf8(const XString* str, const char* substr, size_t from, XCharCaseSensitivity cs);
/**
 * @brief 检查字符串是否包含指定子串
 * @param str 源字符串
 * @param substr 要查找的子串
 * @param cs 大小写敏感性（XCharCaseSensitive/XCharCaseInsensitive）
 * @return 包含返回true，否则返回false
 * @note 行为类似QString::contains，支持大小写敏感/不敏感匹配
 */
bool XString_contains(const XString* str, const XString* substr, XCharCaseSensitivity cs);
/**
 * @brief 重载版本：检查字符串是否包含UTF-8编码的子串
 * @param str 源字符串
 * @param utf8_substr 要查找的UTF-8编码子串
 * @param cs 大小写敏感性
 * @return 包含返回true，否则返回false
 */
bool XString_contains_utf8(const XString* str, const char* utf8_substr, XCharCaseSensitivity cs);
// -------------------------- 字符串比较操作函数 --------------------------

/**
 * @brief 判断两个字符串是否相等（基于 XString_compare）
 * @param str1 第一个 XString 对象指针
 * @param str2 第二个 XString 对象指针
 * @return 相等返回 true，否则返回 false
 */
bool XEquality_XString(const XString* str1, const XString* str2);

const bool XLess_XString(const XString* str1, const XString* str2);
/**
 * @brief 比较两个字符串（字典序）
 * @param str1 第一个 XString 对象指针
 * @param str2 第二个 XString 对象指针
 * @return 小于返回 -1，等于返回 0，大于返回 1
 */
int XString_compare(const XString* str1, const XString* str2);

/**
 * @brief 判断两个字符串是否相等（支持大小写敏感性）
 * @param str1 第一个 XString 对象指针
 * @param str2 第二个 XString 对象指针
 * @param cs 大小写敏感性（区分/不区分）
 * @return 相等返回 true，否则返回 false
 */
bool XString_equals(const XString* str1, const XString* str2, XCharCaseSensitivity cs);

/**
 * @brief 判断字符串是否以指定前缀开头
 * @param str XString 对象指针
 * @param prefix 前缀字符串（XString）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 是返回 true，否则返回 false
 */
bool XString_starts_with(const XString* str, const XString* prefix, XCharCaseSensitivity cs);

/**
 * @brief 判断字符串是否以指定前缀开头
 * @param str XString 对象指针
 * @param prefix 前缀字符串（UTF-8）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 是返回 true，否则返回 false
 */
bool XString_starts_with_utf8(const XString* str, const char* prefix, XCharCaseSensitivity cs);

/**
 * @brief 判断字符串是否以指定后缀结尾
 * @param str XString 对象指针
 * @param suffix 后缀字符串（XString）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 是返回 true，否则返回 false
 */
bool XString_ends_with(const XString* str, const XString* suffix, XCharCaseSensitivity cs);

/**
 * @brief 判断字符串是否以指定后缀结尾
 * @param str XString 对象指针
 * @param suffix 后缀字符串（UTF-8）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 是返回 true，否则返回 false
 */
bool XString_ends_with_utf8(const XString* str, const char* suffix, XCharCaseSensitivity cs);

/**
 * @brief 判断检查当前检查字符串是否全部由小写字符组成
 * @param str 要检查的XString对象指针
 * @return 若字符串非空且所有可大小写转换的字符均为小写，返回true；否则返回false
 * @note 空字符串返回false，忽略无法大小写转换的字符（如数字、符号等）
 */
bool XString_isLower(const XString* str);

/**
 * @brief 判断字符串是否全部由大写字符组成
 * @param str 要检查的XString对象指针
 * @return 若字符串非空且所有可大小写转换的字符均为大写，返回true；否则返回false
 * @note 空字符串返回false，忽略无法大小写转换的字符（如数字、符号等）
 */
bool XString_isUpper(const XString* str);

/**
 * @brief 判断XString是否为null
 * @param str 要检查的XString对象指针
 * @return 若字符串指针为NULL或内部数据未初始化，返回true；否则返回false
 * @note 与isEmpty()的区别：isNull()强调指针指针有效性或初始化状态，isEmpty()强调长度为0
 */
bool XString_isNull(const XString* str);

/**
 * @brief 检查字符串是否包含有效的UTF-16编码序列 * @param str 要检查的XString对象指针
 * @return 若字符串是有效的UTF-16编码则返回true，否则返回false
 * @note 检查代理对的完整性
 */
bool XString_isValidUtf16(const XString* str);

/**
 * @brief 判断字符串是否包含从右到左(RTL)书写的字符
 * @param str 要检查的XString对象指针
 * @return 若字符串包含强从右到左书写的字符，返回true；否则返回false
 * @note 主要用于识别阿拉伯语、希伯来语等从右到左书写的文本
 */
bool XString_isRightToLeft(const XString* str);

// -------------------------- 编码转换函数 --------------------------

/**
 * @brief 转换为 UTF-8 编码字符串
 * @param str XString 对象指针
 * @return 成功返回常量 UTF-8 字符串指针（内部缓存），失败返回 NULL
 */
const char* XString_toUtf8(const XString* str);
size_t XString_toUtf8_length(const XString* str);
/**
 * @brief 转换为 UTF-16 编码字符串（wchar_t 类型）
 * @param str XString 对象指针
 * @return 成功返回常量 wchar_t 数组指针（内部缓存），失败返回 NULL
 */
const uint16_t* XString_toUtf16(const XString* str);
size_t XString_toUtf16_length(const XString* str);
/**
 * @brief 转换为 UTF-32 编码字符串（uint32_t 类型）
 * @param str XString 对象指针
 * @return 成功返回常量 uint32_t 数组指针（内部缓存），失败返回 NULL
 */
const uint32_t* XString_toUtf32(const XString* str);
size_t XString_toUtf32_length(const XString* str);
/**
 * @brief 转换为 GBK 编码字符串
 * @param str XString 对象指针
 * @return 成功返回常量 GBK 字符串指针（内部缓存），失败返回 NULL
 */
const char* XString_toGbk(const XString* str);
size_t XString_toGbk_length(const XString* str);
/**
 * @brief 转换为本地编码字符串（Windows 为 GBK，Linux 为 UTF-8）
 * @param str XString 对象指针
 * @return 成功返回常量本地编码字符串指针（内部缓存），失败返回 NULL
 */
const char* XString_toLocal(const XString* str);
size_t XString_toUtfLocal_length(const XString* str);
// -------------------------- 字符串转换（大小写/修剪） --------------------------

/**
 * @brief 转换为小写字符串（创建新对象）
 * @param str 源 XString 对象指针
 * @return 成功返回新的 XString 指针（小写），失败返回 NULL
 */
XString* XString_toLower(const XString* str);

/**
 * @brief 转换为大写字符串（创建新对象）
 * @param str 源 XString 对象指针
 * @return 成功返回新的 XString 指针（大写），失败返回 NULL
 */
XString* XString_toUpper(const XString* str);

/**
 * @brief 修剪字符串前后的空白字符（创建新对象）
 * @param str 源 XString 对象指针
 * @return 成功返回新的 XString 指针（修剪后），失败返回 NULL
 */
XString* XString_trimmed(const XString* str);

// -------------------------- 字符串转数值函数 --------------------------

/**
 * @brief 转换为 short 类型数值
 * @param str XString 对象指针
 * @param ok 输出参数：转换成功则为 true，否则为 false（可为 NULL）
 * @param base 进制（2-36，0 表示自动识别）
 * @return 转换结果（失败返回 0）
 */
short XString_toShort(const XString* str, bool* ok, int base);

/**
 * @brief 转换为 int 类型数值
 * @param str XString 对象指针
 * @param ok 输出参数：转换成功则为 true，否则为 false（可为 NULL）
 * @param base 进制（2-36，0 表示自动识别）
 * @return 转换结果（失败返回 0）
 */
int XString_toInt(const XString* str, bool* ok, int base);

/**
 * @brief 转换为 long 类型数值
 * @param str XString 对象指针
 * @param ok 输出参数：转换成功则为 true，否则为 false（可为 NULL）
 * @param base 进制（2-36，0 表示自动识别）
 * @return 转换结果（失败返回 0）
 */
long XString_toLong(const XString* str, bool* ok, int base);

/**
 * @brief 转换为 long long 类型数值
 * @param str XString 对象指针
 * @param ok 输出参数：转换成功则为 true，否则为 false（可为 NULL）
 * @param base 进制（2-36，0 表示自动识别）
 * @return 转换结果（失败返回 0）
 */
long long XString_toLongLong(const XString* str, bool* ok, int base);

/**
 * @brief 转换为 unsigned short 类型数值
 * @param str XString 对象指针
 * @param ok 输出参数：转换成功则为 true，否则为 false（可为 NULL）
 * @param base 进制（2-36，0 表示自动识别）
 * @return 转换结果（失败返回 0）
 */
unsigned short XString_toUShort(const XString* str, bool* ok, int base);

/**
 * @brief 转换为 unsigned int 类型数值
 * @param str XString 对象指针
 * @param ok 输出参数：转换成功则为 true，否则为 false（可为 NULL）
 * @param base 进制（2-36，0 表示自动识别）
 * @return 转换结果（失败返回 0）
 */
unsigned int XString_toUInt(const XString* str, bool* ok, int base);

/**
 * @brief 转换为 unsigned long 类型数值
 * @param str XString 对象指针
 * @param ok 输出参数：转换成功则为 true，否则为 false（可为 NULL）
 * @param base 进制（2-36，0 表示自动识别）
 * @return 转换结果（失败返回 0）
 */
unsigned long XString_toULong(const XString* str, bool* ok, int base);

/**
 * @brief 转换为 unsigned long long 类型数值
 * @param str XString 对象指针
 * @param ok 输出参数：转换成功则为 true，否则为 false（可为 NULL）
 * @param base 进制（2-36，0 表示自动识别）
 * @return 转换结果（失败返回 0）
 */
unsigned long long XString_toULongLong(const XString* str, bool* ok, int base);

/**
 * @brief 转换为 float 类型数值
 * @param str XString 对象指针
 * @param ok 输出参数：转换成功则为 true，否则为 false（可为 NULL）
 * @return 转换结果（失败返回 0.0f）
 */
float XString_toFloat(const XString* str, bool* ok);

/**
 * @brief 转换为 double 类型数值
 * @param str XString 对象指针
 * @param ok 输出参数：转换成功则为 true，否则为 false（可为 NULL）
 * @return 转换结果（失败返回 0.0）
 */
double XString_toDouble(const XString* str, bool* ok);

// -------------------------- 数值转字符串函数 --------------------------
/**
 * @brief 将int类型转换为字符串并设置XString
 * @param str 目标XString对象
 * @param n 要转换的整数
 * @param base 进制（2-36，默认为10）
 * @return 成功返回true，失败返回false
 */
bool XString_setNum_int(XString* str, int n, int base);
/**
 * @brief 将unsigned int类型转换为字符串并设置XString
 * @param str 目标XString对象
 * @param n 要转换的无符号整数
 * @param base 进制（2-36，默认为10）
 * @return 成功返回true，失败返回false
 */
bool XString_setNum_uInt(XString* str, unsigned int n, int base);
/**
 * @brief 将long类型转换为字符串并设置XString
 * @param str 目标XString对象
 * @param n 要转换的长整数
 * @param base 进制（2-36，默认为10）
 * @return 成功返回true，失败返回false
 */
bool XString_setNum_long(XString* str, long n, int base);
/**
 * @brief 将unsigned long类型转换为字符串并设置XString
 * @param str 目标XString对象
 * @param n 要转换的无符号长整数
 * @param base 进制（2-36，默认为10）
 * @return 成功返回true，失败返回false
 */
bool XString_setNum_uLong(XString* str, unsigned long n, int base);
/**
 * @brief 将long long类型转换为字符串并设置XString
 * @param str 目标XString对象
 * @param n 要转换的长长整数
 * @param base 进制（2-36，默认为10）
 * @return 成功返回true，失败返回false
 */
bool XString_setNum_llong(XString* str, long long n, int base);
/**
 * @brief 将unsigned long long类型转换为字符串并设置XString
 * @param str 目标XString对象
 * @param n 要转换的无符号长长整数
 * @param base 进制（2-36，默认为10）
 * @return 成功返回true，失败返回false
 */
bool XString_setNum_uLLong(XString* str, unsigned long long n, int base);
/**
 * @brief 将float类型转换为字符串并设置XString
 * @param str 目标XString对象
 * @param f 要转换的浮点数
 * @param format 格式字符（'e'/'E'科学计数, 'f'/'F'固定点, 'g'/'G'自动选择）
 * @param precision 精度（小数位数，默认为6）
 * @return 成功返回true，失败返回false
 */
bool XString_setNum_float(XString* str, float f, char format, int precision);
/**
 * @brief 将double类型转换为字符串并设置XString
 * @param str 目标XString对象
 * @param d 要转换的双精度浮点数
 * @param format 格式字符（'e'/'E'科学计数, 'f'/'F'固定点, 'g'/'G'自动选择）
 * @param precision 精度（小数位数，默认为6）
 * @return 成功返回true，失败返回false
 */
bool XString_setNum_double(XString* str, double d, char format, int precision);
// -------------------------- 子串与容量操作函数 --------------------------

/**
 * @brief 获取字符串左侧指定长度的子串
 * @param str 源 XString 对象指针
 * @param n 子串长度（超过原长则返回整个字符串）
 * @return 成功返回新的 XString 指针（子串），失败返回 NULL
 */
XString* XString_left(const XString* str, size_t n);

/**
 * @brief 获取字符串右侧指定长度的子串
 * @param str 源 XString 对象指针
 * @param n 子串长度（超过原长则返回整个字符串）
 * @return 成功返回新的 XString 指针（子串），失败返回 NULL
 */
XString* XString_right(const XString* str, size_t n);

/**
 * @brief 获取从指定位置开始的子串
 * @param str 源 XString 对象指针
 * @param pos 起始位置
 * @param n 子串长度（0 表示到末尾）
 * @return 成功返回新的 XString 指针（子串），失败返回 NULL
 */
XString* XString_mid(const XString* str, size_t pos, size_t n);

/**
 * @brief 预留指定容量的存储空间（优化性能）
 * @param str XString 对象指针
 * @param capacity 预分配的字符数（不含终止符）
 *  @return 成功返回 true，失败返回 false
 */
bool XString_reserve(XString* str, size_t capacity);

/**
 * @brief 调整XString的长度
 * @param str 目标XString对象指针
 * @param size 新的长度（以XChar为单位）
 * @note 若新长度大于当前长度，将用空字符填充；若小于当前长度，将截断短字符串
 */
void XString_resize(XString* str, size_t size);

/**
 * @brief 将字符串截断到指定位置
 * @param str XString 对象指针
 * @param position 截断位置（截断后长度为 position）
 */
void XString_truncate(XString* str, size_t position);

// -------------------------- 字符串拆分函数 --------------------------

/**
 * @brief 按分隔符拆分字符串为字符串列表
 * @param str 源 XString 对象指针
 * @param delimiter 分隔符（UTF-8）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回 XStringList 指针（需手动释放），失败返回 NULL
 */
XStringList* XString_split_utf8(const XString* str, const char* delimiter, XCharCaseSensitivity cs);

/**
 * @brief 按分隔符拆分字符串（限制最大拆分次数）
 * @param str 源 XString 对象指针
 * @param delimiter 分隔符（UTF-8）
 * @param limit 最大拆分次数（0 表示不限制）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回 XStringList 指针（需手动释放），失败返回 NULL
 */
XStringList* XString_split_limit_utf8(const XString* str, const char* delimiter, size_t limit, XCharCaseSensitivity cs);

// -------------------------- 打印函数 --------------------------

/**
 * @brief 打印 XString 字符串（使用本地编码）
 * @param str XString 对象指针
 * @return 打印的字符数（参考 printf 返回值）
 */
int XPrint(const XString* str);

/**
 * @brief 打印 UTF-8 编码字符串
 * @param utf8_str 待打印的 UTF-8 字符串
 * @return 打印的字符数（参考 printf 返回值）
 */
int XPrint_utf8(const char* utf8_str);

/**
 * @brief 格式化打印 UTF-8 字符串
 * @param format 格式化字符串（UTF-8）
 * @param ... 可变参数列表
 * @return 打印的字符数（参考 printf 返回值）
 */
int XPrint_utf8_fmt(const char* format, ...);

// 输出单个XChar字符
int XPrint_XChar(XChar* ch);

#ifdef __cplusplus
}
#endif

#endif // XSTRING_H