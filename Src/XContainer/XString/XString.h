#include "XContainer.h"
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
typedef struct XRegularExpression XRegularExpression;
typedef struct XRegularExpressionMatch XRegularExpressionMatch;
/**
 * @brief XString 类虚函数表枚举（继承自 XContainer）
 * @details 定义 XString 类支持的所有虚函数索引，用于虚函数调用机制
 */
XCLASS_DEFINE_BEGING(XString)
XCLASS_DEFINE_ENUM(XString, At) = XCLASS_VTABLE_GET_SIZE(XContainer),// 获取指定位置的 XChar 字符
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
    XContainer parent;  // 继承容器基类，m_data 指向 XChar 数组（UTF-16 存储），使用父类的 XSharedData 实现隐式共享
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
 * @brief 从 UTF-16 字符串创建 XString 对象
 * @param utf16_str 输入的 UTF-16 字符串（uint16_t 数组，NULL 则创建空字符串）
 * @return 成功返回 XString 指针，失败返回 NULL
 */
XString* XString_create_utf16(const uint16_t* utf16_str);

/**
 * @brief 从指定长度的 UTF-16 字符串创建 XString 对象
 * @param utf16_str 输入的 UTF-16 字符串（uint16_t 数组）
 * @param len 字符串长度（字符数，不含终止符）
 * @return 成功返回 XString 指针，失败返回 NULL
 */
XString* XString_create_with_length_utf16(const uint16_t* utf16_str, size_t len);

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
 * - 该宏在栈上创建XString对象，无需手动调用XMalloc_System分配内存
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
  * - 该宏在栈上创建XString对象，无需手动调用XMalloc_System分配内存
  * - 使用完毕后需根据XString的内存管理规则进行清理（如调用XString_deinit等）
  */
#define XString_Init_Fmt_Utf8(name,utf8_format, ...)     XString _##name,*name=&_##name;XString_init(name);XString_assign_fmt_utf8(name,utf8_format,##__VA_ARGS__)
     // -------------------------- 基础操作宏（继承自 XContainer） --------------------------

#define XString_copy_base				    XContainer_copy_base	// 复制对象（基础实现）
#define XString_move_base				    XContainer_move_base	// 移动对象（基础实现）
#define XString_deinit_base					XContainer_deinit_base	// 销毁对象（基础实现）
#define XString_delete_base					XContainer_delete_base	// 删除对象（释放内存）
#define XString_clear_base				    XContainer_clear_base	// 清空字符串内容
#define XString_isEmpty_base				XContainer_isEmpty_base	// 判断字符串是否为空
#define XString_size_base					XContainer_size_base	// 获取字符串长度（字符数）
#define XString_capacity_base				XContainer_capacity_base	// 获取当前容量（字符数）
#define XString_swap_base				    XContainer_swap_base	// 交换两个字符串内容
#define XString_typeSize_base				XContainer_typeSize_base	// 获取单个元素（XChar）的大小

/**
 * @brief 获取字符串长度（字符数，不含终止符）
 * @note 等价于 XString_size_base，为字符串场景提供更直观的命名
 */
#define XString_length_base                 XContainer_size_base

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
bool XString_replace(XString* str, const XString* before, const XString* after, XChar_CaseSensitivity cs);

/**
 * @brief 替换字符串中的子串
 * @param str XString 对象指针
 * @param before 待替换的子串（UTF-8）
 * @param after 替换后的子串（UTF-8）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回 true，失败返回 false
 */
bool XString_replace_utf8(XString* str, const char* before, const char* after, XChar_CaseSensitivity cs);

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
int64_t XString_indexOf(const XString* str, const XString* substr, size_t from, XChar_CaseSensitivity cs);
/**
 * @brief 查找子串首次出现的位置
 * @param str XString 对象指针
 * @param substr 待查找的子串（UTF-8）
 * @param from 起始查找位置
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回子串起始索引，失败返回 -1
 */
int64_t XString_indexOf_utf8(const XString* str, const char* substr, size_t from, XChar_CaseSensitivity cs);
/**
 * @brief 查找子串最后一次出现的位置
 * @param str XString 对象指针
 * @param substr 待查找的子串（XString 对象指针）
 * @param from 起始查找位置（从后往前）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回子串起始索引，失败返回 -1
 */
int64_t XString_lastIndexOf(const XString* str, const XString* substr, size_t from, XChar_CaseSensitivity cs);
/**
 * @brief 查找子串最后一次出现的位置
 * @param str XString 对象指针
 * @param substr 待查找的子串（UTF-8）
 * @param from 起始查找位置（从后往前）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回子串起始索引，失败返回 -1
 */
int64_t XString_lastIndexOf_utf8(const XString* str, const char* substr, size_t from, XChar_CaseSensitivity cs);
/**
 * @brief 检查字符串是否包含指定子串
 * @param str 源字符串
 * @param substr 要查找的子串
 * @param cs 大小写敏感性（XCharCaseSensitive/XChar_CaseInsensitive）
 * @return 包含返回true，否则返回false
 * @note 行为类似QString::contains，支持大小写敏感/不敏感匹配
 */
bool XString_contains(const XString* str, const XString* substr, XChar_CaseSensitivity cs);
/**
 * @brief 重载版本：检查字符串是否包含UTF-8编码的子串
 * @param str 源字符串
 * @param utf8_substr 要查找的UTF-8编码子串
 * @param cs 大小写敏感性
 * @return 包含返回true，否则返回false
 */
bool XString_contains_utf8(const XString* str, const char* utf8_substr, XChar_CaseSensitivity cs);

#if XRegularExpression_ON
/**
 * @brief 使用正则表达式查找第一次匹配。
 * @param str 源字符串
 * @param expression 正则表达式
 * @param from 起始 UTF-16 code unit 偏移；负数按 Qt 6.8 正则匹配规则从字符串末尾计算
 * @param match 可选输出匹配结果，调用前可为未初始化对象
 * @return 匹配起始位置，未匹配或参数无效返回 -1
 */
int64_t XString_indexOf_regularExpression(const XString* str,
                                          const XRegularExpression* expression,
                                          int64_t from,
                                          XRegularExpressionMatch* match);

/**
 * @brief 使用正则表达式查找最后一次匹配。
 * @param str 源字符串
 * @param expression 正则表达式
 * @param from 允许的最大匹配起始位置，负数表示从末尾查找
 * @param match 可选输出匹配结果
 * @return 匹配起始位置，未匹配或参数无效返回 -1
 */
int64_t XString_lastIndexOf_regularExpression(const XString* str,
                                               const XRegularExpression* expression,
                                               int64_t from,
                                               XRegularExpressionMatch* match);

/**
 * @brief 判断字符串是否包含正则匹配。
 * @param str 源字符串
 * @param expression 正则表达式
 * @return 包含匹配返回 true，否则返回 false
 */
bool XString_contains_regularExpression(const XString* str,
                                        const XRegularExpression* expression);

/**
 * @brief 判断字符串是否包含正则匹配并输出匹配结果。
 * @details 对齐 Qt 6.8 `QString::contains(const QRegularExpression&, QRegularExpressionMatch*)`；
 *          匹配成功时写入 `match`，失败时不修改已有的 `match` 内容。
 * @param str 源字符串；不能为 NULL。
 * @param expression 正则表达式对象；函数只借用，不能为 NULL。
 * @param match 可选输出匹配结果；必须指向已初始化对象，允许传入 NULL。
 * @return 包含匹配返回 true，否则返回 false。
 */
bool XString_contains_regularExpression_2(const XString* str,
                                          const XRegularExpression* expression,
                                          XRegularExpressionMatch* match);

/**
 * @brief 统计字符串中的正则匹配数量。
 * @param str 源字符串
 * @param expression 正则表达式
 * @return 匹配数量，参数无效返回0
 */
size_t XString_count_regularExpression(const XString* str,
                                       const XRegularExpression* expression);

/**
 * @brief 使用正则表达式替换字符串中的所有匹配。
 * @param str 待修改字符串
 * @param expression 正则表达式
 * @param after 替换文本；按 Qt 6.8 规则支持 \\1 到 \\9 捕获组引用及合法的两位捕获组编号，\\0 保留为普通文本。
 * @return 成功返回 true，参数无效或分配失败返回 false
 */
bool XString_replace_regularExpression(XString* str,
                                       const XRegularExpression* expression,
                                       const XString* after);

/**
 * @brief 移除字符串中的所有正则表达式匹配。
 * @details 对齐 Qt 6.8 `QString::remove(const QRegularExpression&)`，等价于使用空替换字符串调用正则替换。
 * @param str 待修改的字符串对象；不能为 NULL。
 * @param expression 正则表达式对象；函数只借用该对象，不能为 NULL。
 * @return 成功返回 true；参数无效或临时空替换字符串创建失败返回 false。
 */
bool XString_remove_regularExpression(XString* str,
                                      const XRegularExpression* expression);

/**
 * @brief 使用正则表达式分割字符串。
 * @param str 源字符串
 * @param separator 分隔符正则表达式
 * @param keepEmptyParts 是否保留空字段
 * @return 新字符串列表；分配失败或参数无效返回 NULL；正则无效时返回空列表。
 */
XStringList* XString_split_regularExpression(const XString* str,
                                             const XRegularExpression* separator,
                                             bool keepEmptyParts);
#endif
// -------------------------- 字符串比较操作函数 --------------------------

const bool XLess_XString(const XString* str1, const XString* str2);
/**
 * @brief 比较两个字符串（字典序）
 * @param str1 第一个 XString 对象指针
 * @param str2 第二个 XString 对象指针
 * @return 小于返回 -1，等于返回 0，大于返回 1
 */
int32_t XString_compare(const XString* str1, const XString* str2);

/**
 * @brief 判断两个字符串是否相等（支持大小写敏感性）
 * @param str1 第一个 XString 对象指针
 * @param str2 第二个 XString 对象指针
 * @param cs 大小写敏感性（区分/不区分）
 * @return 相等返回 true，否则返回 false
 */
bool XString_equals(const XString* str1, const XString* str2, XChar_CaseSensitivity cs);
/**
 * @brief 判断字符串是否与 UTF-8 字符串相等
 * @param str XString 对象指针
 * @param utf8_str UTF-8 字符串
 * @param cs 大小写敏感性
 * @return 相等返回 true，否则返回 false
 */
bool XString_equals_utf8(const XString* str, const char* utf8_str, XChar_CaseSensitivity cs);
/**
 * @brief 判断字符串是否以指定前缀开头
 * @param str XString 对象指针
 * @param prefix 前缀字符串（XString）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 是返回 true，否则返回 false
 */
bool XString_startsWith(const XString* str, const XString* prefix, XChar_CaseSensitivity cs);

/**
 * @brief 判断字符串是否以指定前缀开头
 * @param str XString 对象指针
 * @param prefix 前缀字符串（UTF-8）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 是返回 true，否则返回 false
 */
bool XString_startsWith_utf8(const XString* str, const char* prefix, XChar_CaseSensitivity cs);

/**
 * @brief 判断字符串是否以指定后缀结尾
 * @param str XString 对象指针
 * @param suffix 后缀字符串（XString）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 是返回 true，否则返回 false
 */
bool XString_endsWith(const XString* str, const XString* suffix, XChar_CaseSensitivity cs);

/**
 * @brief 判断字符串是否以指定后缀结尾
 * @param str XString 对象指针
 * @param suffix 后缀字符串（UTF-8）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 是返回 true，否则返回 false
 */
bool XString_endsWith_utf8(const XString* str, const char* suffix, XChar_CaseSensitivity cs);

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
/**
 * @brief 获取本地编码字符串长度（对齐 toUtf8_length/toUtf16_length 命名风格）
 * @param str XString 对象指针
 * @return 本地编码字节数（不含终止符），失败返回0
 */
size_t XString_toLocal_length(const XString* str);
/**
 * @brief toUtfLocal_length 旧名别名（命名历史遗留，保持向后兼容）
 * @note 宏实现，等价于 XString_toLocal_length
 */
#define XString_toUtfLocal_length		XString_toLocal_length
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
XStringList* XString_split_utf8(const XString* str, const char* delimiter, XChar_CaseSensitivity cs);

/**
 * @brief 按分隔符拆分字符串（限制最大拆分次数）
 * @param str 源 XString 对象指针
 * @param delimiter 分隔符（UTF-8）
 * @param limit 最大拆分次数（0 表示不限制）
 * @param cs 大小写敏感性（区分/不区分）
 * @return 成功返回 XStringList 指针（需手动释放），失败返回 NULL
 */
XStringList* XString_split_limit_utf8(const XString* str, const char* delimiter, size_t limit, XChar_CaseSensitivity cs);


// -------------------------- Qt 6.8 对齐：子串操作函数 --------------------------

/**
 * @brief 返回从pos开始到末尾的子串（对齐Qt6.0 QString::sliced(pos)）
 * @param str 源XString对象指针
 * @param pos 起始位置
 * @return 成功返回新XString指针，失败返回NULL
 * @note pos必须 <= size()，否则行为未定义
 */
XString* XString_sliced(const XString* str, size_t pos);

/**
 * @brief 返回从pos开始长度为n的子串（对齐Qt6.0 QString::sliced(pos, n)）
 * @param str 源XString对象指针
 * @param pos 起始位置
 * @param n 子串长度
 * @return 成功返回新XString指针，失败返回NULL
 * @note pos+n必须 <= size()，否则行为未定义
 */
XString* XString_sliced_2(const XString* str, size_t pos, size_t n);

/**
 * @brief 返回前n个字符的子串（对齐Qt6.0 QString::first(n)）
 * @param str 源XString对象指针
 * @param n 字符数
 * @return 成功返回新XString指针，失败返回NULL
 * @note n必须 <= size()，否则行为未定义
 */
XString* XString_first(const XString* str, size_t n);

/**
 * @brief 返回后n个字符的子串（对齐Qt6.0 QString::last(n)）
 * @param str 源XString对象指针
 * @param n 字符数
 * @return 成功返回新XString指针，失败返回NULL
 * @note n必须 <= size()，否则行为未定义
 */
XString* XString_last(const XString* str, size_t n);

/**
 * @brief 返回去掉末尾len个字符的子串（对齐Qt QString::chopped(len)）
 * @param str 源XString对象指针
 * @param len 要去掉的末尾字符数
 * @return 成功返回新XString指针，失败返回NULL
 * @note len必须 <= size()，否则行为未定义
 */
XString* XString_chopped(const XString* str, size_t len);

// -------------------------- Qt 6.8 对齐：原地修改函数 --------------------------

/**
 * @brief 移除末尾n个字符（对齐Qt QString::chop(n)）
 * @param str XString对象指针
 * @param n 要移除的字符数
 */
void XString_chop(XString* str, size_t n);

/**
 * @brief 调整字符串大小，新位置填充指定字符（对齐Qt QString::resize(n, fillChar)）
 * @param str XString对象指针
 * @param size 新的长度
 * @param fillChar 填充字符
 */
void XString_resize_fill(XString* str, size_t size, XChar fillChar);

/**
 * @brief 用指定字符填充整个字符串（对齐Qt QString::fill(ch, size)）
 * @param str XString对象指针
 * @param ch 填充字符
 * @param size 新的大小（-1表示保持当前大小）
 * @return 成功返回true，失败返回false
 */
bool XString_fill(XString* str, XChar ch, int64_t size);

/**
 * @brief 交换两个字符串内容（对齐Qt QString::swap(other)）
 * @param str XString对象指针
 * @param other 另一个XString对象指针
 */
void XString_swap(XString* str, XString* other);

/**
 * @brief 释放多余容量（对齐Qt QString::squeeze()）
 * @param str XString对象指针
 */
void XString_squeeze(XString* str);

/**
 * @brief 释放多余容量（对齐Qt QString::shrink_to_fit()，等价于squeeze）
 * @param str XString对象指针
 */
#define XString_shrink_to_fit   XString_squeeze

/**
 * @brief 原地切片：从pos开始到末尾（对齐Qt6.8 QString::slice(pos)）
 * @param str XString对象指针
 * @param pos 起始位置
 * @return 成功返回true，失败返回false
 */
bool XString_slice(XString* str, size_t pos);

/**
 * @brief 原地切片：从pos开始长度为n（对齐Qt6.8 QString::slice(pos, n)）
 * @param str XString对象指针
 * @param pos 起始位置
 * @param n 子串长度
 * @return 成功返回true，失败返回false
 */
bool XString_slice_2(XString* str, size_t pos, size_t n);

/**
 * @brief 调整大小用于覆写（对齐Qt6.8 QString::resizeForOverwrite(size)）
 * @param str XString对象指针
 * @param size 新的大小
 */
void XString_resizeForOverwrite(XString* str, size_t size);

// -------------------------- Qt 6.8 对齐：转换函数 --------------------------

/**
 * @brief 转换为大小写折叠形式（对齐Qt QString::toCaseFolded()）
 * @param str 源XString对象指针
 * @return 成功返回新XString指针，失败返回NULL
 */
XString* XString_toCaseFolded(const XString* str);

/**
 * @brief 转换为HTML转义字符串（对齐Qt QString::toHtmlEscaped()）
 * @param str 源XString对象指针
 * @return 成功返回新XString指针，失败返回NULL
 */
XString* XString_toHtmlEscaped(const XString* str);

/**
 * @brief 左对齐填充（对齐Qt QString::leftJustified(width, fill, truncate)）
 * @param str 源XString对象指针
 * @param width 目标宽度
 * @param fill 填充字符
 * @param truncate 超出时是否截断
 * @return 成功返回新XString指针，失败返回NULL
 */
XString* XString_leftJustified(const XString* str, size_t width, XChar fill, bool truncate);

/**
 * @brief 右对齐填充（对齐Qt QString::rightJustified(width, fill, truncate)）
 * @param str 源XString对象指针
 * @param width 目标宽度
 * @param fill 填充字符
 * @param truncate 超出时是否截断
 * @return 成功返回新XString指针，失败返回NULL
 */
XString* XString_rightJustified(const XString* str, size_t width, XChar fill, bool truncate);

/**
 * @brief 简化空白（对齐Qt QString::simplified()）
 * @param str 源XString对象指针
 * @return 成功返回新XString指针（去除首尾空白，内部连续空白替换为单个空格），失败返回NULL
 */
XString* XString_simplified(const XString* str);

/**
 * @brief 重复字符串（对齐Qt QString::repeated(times)）
 * @param str 源XString对象指针
 * @param times 重复次数（<1返回空字符串）
 * @return 成功返回新XString指针，失败返回NULL
 */
XString* XString_repeated(const XString* str, size_t times);

// -------------------------- Qt 6.8 对齐：查询函数 --------------------------

/**
 * @brief 统计子串出现次数（对齐Qt QString::count(str, cs)）
 * @param str 源XString对象指针
 * @param sub 子串（XString）
 * @param cs 大小写敏感性
 * @return 出现次数
 */
size_t XString_count(const XString* str, const XString* sub, XChar_CaseSensitivity cs);

/**
 * @brief 统计UTF-8子串出现次数
 * @param str 源XString对象指针
 * @param sub 子串（UTF-8）
 * @param cs 大小写敏感性
 * @return 出现次数
 */
size_t XString_count_utf8(const XString* str, const char* sub, XChar_CaseSensitivity cs);

/**
 * @brief 统计字符出现次数（对齐Qt QString::count(ch, cs)）
 * @param str 源XString对象指针
 * @param ch 要统计的字符
 * @param cs 大小写敏感性
 * @return 出现次数
 */
size_t XString_count_char(const XString* str, XChar ch, XChar_CaseSensitivity cs);

// -------------------------- Qt 6.8 对齐：静态/最大值函数 --------------------------

/**
 * @brief 获取XString理论最大容量（对齐Qt6.8 QString::maxSize()）
 * @return 理论最大字符数
 */
size_t XString_maxSize(void);

// -------------------------- Qt 6.8 对齐：数值转字符串静态函数 --------------------------

/**
 * @brief 将int转为XString（对齐Qt QString::number(int, base)，Qt内联委托number(qlonglong)）
 * @param n 整数值
 * @param base 进制（2-36，默认10）
 * @return 成功返回新XString指针，失败返回NULL
 * @note 宏实现，等价于 XString_number_llong((long long)(n), base)
 */
#define XString_number_int(n, base)  XString_number_llong((long long)(n), base)

/**
 * @brief 将unsigned int转为XString（对齐Qt QString::number(uint, base)，Qt内联委托number(qulonglong)）
 * @note 宏实现，等价于 XString_number_ullong((unsigned long long)(n), base)
 */
#define XString_number_uint(n, base)  XString_number_ullong((unsigned long long)(n), base)

/**
 * @brief 将long转为XString（对齐Qt QString::number(long, base)，Qt内联委托number(qlonglong)）
 * @note 宏实现，等价于 XString_number_llong((long long)(n), base)
 */
#define XString_number_long(n, base)  XString_number_llong((long long)(n), base)

/**
 * @brief 将unsigned long转为XString（对齐Qt QString::number(ulong, base)，Qt内联委托number(qulonglong)）
 * @note 宏实现，等价于 XString_number_ullong((unsigned long long)(n), base)
 */
#define XString_number_ulong(n, base)  XString_number_ullong((unsigned long long)(n), base)

/**
 * @brief 将long long转为XString（对齐Qt QString::number(qlonglong, base)）
 */
XString* XString_number_llong(long long n, int base);

/**
 * @brief 将unsigned long long转为XString（对齐Qt QString::number(qulonglong, base)）
 */
XString* XString_number_ullong(unsigned long long n, int base);

/**
 * @brief 将double转为XString（对齐Qt QString::number(double, format, precision)）
 * @param n 浮点数值
 * @param format 格式（'e'/'E'科学计数, 'f'/'F'定点, 'g'/'G'自动）
 * @param precision 精度（默认6）
 * @return 成功返回新XString指针，失败返回NULL
 */
XString* XString_number_double(double n, char format, int precision);

/**
 * @brief 将float转为XString（对齐Qt QString::number(float, format, precision)）
 * @note 宏实现，等价于 XString_number_double((double)(n), format, precision)
 */
#define XString_number_float(n, format, precision)  XString_number_double((double)(n), format, precision)

// -------------------------- Qt 6.8 对齐：数据访问函数 --------------------------

/**
 * @brief 获取可修改的XChar数据指针（对齐Qt QString::data()）
 * @param str XString对象指针
 * @return XChar数据指针，失败返回NULL
 * @note 返回指针在字符串被修改前有效
 */
XChar* XString_data(XString* str);

/**
 * @brief 获取常量XChar数据指针（对齐Qt QString::constData()，与unicode()实现等价）
 * @param str XString对象指针
 * @return 常量XChar数据指针，失败返回NULL
 * @note 宏实现，等价于 XString_unicode
 */
#define XString_constData				XString_unicode

/**
 * @brief 获取可写数据指针（对齐Qt QString::data()）
 * @param str XString对象指针
 * @return XChar数据指针，失败返回NULL
/**
 * @brief 移除指定范围的字符（对齐Qt QString::remove(i, len)）
 * @param str XString对象指针
 * @param pos 起始位置
 * @param len 移除长度
 * @return 移除成功返回true
 * @note 宏实现，等价于 XString_remove_base
 */
#define XString_remove					XString_remove_base

/**
 * @brief 按分隔符拆分字符串（对齐Qt QString::split()）
 * @param str XString对象指针
 * @param delimiter 分隔符(UTF-8)
 * @param cs 大小写敏感性
 * @return 拆分后的XStringList，失败返回NULL
 * @note 宏实现，等价于 XString_split_utf8
 */
#define XString_split					XString_split_utf8

/**
 * @brief 获取字符串长度（对齐Qt QString::size()）
 * @param str XString对象指针
 * @return 字符数
 * @note 宏实现，等价于 XString_length_base
 */
#define XString_size					XString_length_base


/**
 * @brief 获取字符串长度（等价于size，对齐Qt QString::length()）
 * @param str XString对象指针
 * @return 字符数
 */
#define XString_length XString_length_base

/**
 * @brief 获取元素大小（对齐Qt value_type相关）
 */
#define XString_typeSize XString_typeSize_base

// -------------------------- Qt 6.8 对齐：setNum扩展 --------------------------

/**
 * @brief 将short转为字符串并设置（对齐Qt QString::setNum(short, base)）
 */
bool XString_setNum_short(XString* str, short n, int base);

// -------------------------- Qt 6.8 对齐：setUnicode/setUtf16/setRawData --------------------------

/**
 * @brief 从XChar数组设置内容（对齐Qt QString::setUnicode(unicode, size)）
 * @param str XString对象指针
 * @param unicode XChar数组
 * @param size 字符数
 * @return 成功返回true，失败返回false
 */
bool XString_setUnicode(XString* str, const XChar* unicode, size_t size);

/**
 * @brief 从uint16_t数组设置内容（对齐Qt QString::setUtf16(unicode, size)）
 * @param str XString对象指针
 * @param unicode uint16_t数组（UTF-16）
 * @param size 字符数
 * @return 成功返回true，失败返回false
 */
bool XString_setUtf16(XString* str, const uint16_t* unicode, size_t size);

// -------------------------- Qt 6.8 对齐：XChar重载查找/包含/替换/移除 --------------------------

/**
 * @brief 查找字符首次出现的位置（对齐Qt QString::indexOf(QChar, from, cs)）
 * @param str XString对象指针
 * @param ch 要查找的字符
 * @param from 起始位置
 * @param cs 大小写敏感性
 * @return 成功返回索引，失败返回-1
 */
int64_t XString_indexOf_char(const XString* str, XChar ch, size_t from, XChar_CaseSensitivity cs);

/**
 * @brief 查找字符最后出现的位置（对齐Qt6.3 QString::lastIndexOf(QChar, cs)）
 * @param str XString对象指针
 * @param ch 要查找的字符
 * @param cs 大小写敏感性
 * @return 成功返回索引，失败返回-1
 */
int64_t XString_lastIndexOf_char(const XString* str, XChar ch, XChar_CaseSensitivity cs);

/**
 * @brief 检查是否包含指定字符（对齐Qt QString::contains(QChar, cs)）
 * @param str 源XString对象指针
 * @param ch 要查找的字符
 * @param cs 大小写敏感性
 * @return 包含返回true，否则返回false
 */
bool XString_contains_char(const XString* str, XChar ch, XChar_CaseSensitivity cs);

/**
 * @brief 移除所有指定字符（对齐Qt QString::remove(QChar, cs)）
 * @param str XString对象指针
 * @param ch 要移除的字符
 * @param cs 大小写敏感性
 * @return 成功返回true，失败返回false
 */
bool XString_remove_char(XString* str, XChar ch, XChar_CaseSensitivity cs);

/**
 * @brief 替换字符（对齐Qt QString::replace(QChar before, QChar after, cs)）
 * @param str XString对象指针
 * @param before 要替换的字符
 * @param after 替换后的字符
 * @param cs 大小写敏感性
 * @return 成功返回true，失败返回false
 */
bool XString_replace_char(XString* str, XChar before, XChar after, XChar_CaseSensitivity cs);

// -------------------------- Qt 6.8 对齐：SectionFlags 分段标志枚举 --------------------------

/**
 * @brief section() 的分段标志位（对齐Qt QString::SectionFlag）
 * @details 多个标志可按位或组合，作为 XString_section 系列函数的 flags 参数
 */
typedef enum XString_SectionFlag {
	XString_SectionDefault             = 0x00, /**< 默认：不跳过空段，分隔符大小写敏感 */
	XString_SectionSkipEmpty           = 0x01, /**< 跳过空段（空段不计入段号） */
	XString_SectionIncludeLeadingSep   = 0x02, /**< 结果包含起始段之前的分隔符 */
	XString_SectionIncludeTrailingSep  = 0x04, /**< 结果包含结束段之后的分隔符 */
	XString_SectionCaseInsensitiveSeps = 0x08  /**< 分隔符匹配大小写不敏感 */
} XString_SectionFlag;

// -------------------------- Qt 6.8 对齐：section() 分段提取 --------------------------

/**
 * @brief 按分隔符提取指定区间的段（对齐Qt QString::section(QString,start,end,flags)）
 * @param str 源XString
 * @param sep 分隔符XString
 * @param start 起始段号（0基；负数从右计数，-1为最后一段）
 * @param end 结束段号（含；-1表示到字符串末尾）
 * @param flags XString_SectionFlag 标志按位或
 * @return 成功返回新XString，失败/越界返回空串；str或sep为NULL返回空串
 * @note 空分隔符时整个字符串视作单段；段号从0开始计数
 */
XString* XString_section(const XString* str, const XString* sep, int64_t start, int64_t end, int flags);

/**
 * @brief 按UTF-8分隔符提取段（对齐Qt QString::section(QLatin1String,...)）
 * @param str 源XString
 * @param sep UTF-8分隔符
 * @param start 起始段号
 * @param end 结束段号
 * @param flags 分段标志
 * @return 成功返回新XString，失败返回空串
 * @note 内部构造分隔符XString后委托 XString_section 实现
 */
XString* XString_section_utf8(const XString* str, const char* sep, int64_t start, int64_t end, int flags);

/**
 * @brief 按单字符分隔符提取段（对齐Qt QString::section(QChar,...)）
 * @param str 源XString
 * @param sep 单个XChar分隔符
 * @param start 起始段号
 * @param end 结束段号
 * @param flags 分段标志
 * @return 成功返回新XString，失败返回空串
 * @note 内部构造单字符分隔符XString后委托 XString_section 实现
 */
XString* XString_section_char(const XString* str, XChar sep, int64_t start, int64_t end, int flags);

#if XRegularExpression_ON
/**
 * @brief 按正则表达式分隔符提取指定区间的字符串段。
 * @details 对齐 Qt 6.8 `QString::section(const QRegularExpression&, ...)`；正则匹配结果作为分隔符，
 *          `start` 和 `end` 按段编号处理，返回值为新创建的 XString。
 * @param str 源字符串；函数只借用该对象，不能传入 NULL。
 * @param expression 分隔符正则表达式；函数只借用该对象，不能传入 NULL。
 * @param start 起始段编号，包含该段；负数从末尾倒数。
 * @param end 结束段编号，包含该段；负数从末尾倒数，通常传 -1 表示最后一段。
 * @param flags `XString_SectionFlag` 按位或组合；支持跳过空段、包含首尾分隔符和分隔符大小写不敏感。
 * @return 成功返回新创建的 XString，调用者必须使用 XString_delete_base 释放；正则无效、参数无效或范围无效时返回空 XString。
 * @note 字符索引和长度均按 XString 内部 UTF-16 code unit 计数；函数不修改输入对象。
 */
XString* XString_section_regularExpression(const XString* str,
                                           const XRegularExpression* expression,
                                           int64_t start,
                                           int64_t end,
                                           int flags);
#endif

// -------------------------- Qt 6.8 对齐：arg() 占位符替换 --------------------------

/**
 * @brief 替换最低编号占位符%N为指定字符串（对齐Qt QString::arg(QString,fieldWidth,fillChar)）
 * @param str 含占位符的源XString
 * @param a 替换内容XString
 * @param fieldWidth 最小占位宽度（>0右对齐左填充，<0左对齐右填充，0不填充）
 * @param fillChar 填充字符
 * @return 返回新XString；无占位符时返回源串拷贝
 * @note 占位符为%1..%99，替换所有出现的最低编号占位符；支持%L前缀（按非本地化处理）
 */
XString* XString_arg(const XString* str, const XString* a, int fieldWidth, XChar fillChar);

/**
 * @brief 替换最低编号占位符为UTF-8字符串（对齐Qt QString::arg(QLatin1String,...)）
 * @param str 含占位符的源XString
 * @param a UTF-8替换内容
 * @param fieldWidth 最小占位宽度
 * @param fillChar 填充字符
 * @return 返回新XString
 * @note 内部构造XString后委托 XString_arg 实现
 */
XString* XString_arg_utf8(const XString* str, const char* a, int fieldWidth, XChar fillChar);

/**
 * @brief 替换最低编号占位符为单字符（对齐Qt QString::arg(QChar,...)）
 * @param str 含占位符的源XString
 * @param a 单个XChar替换内容
 * @param fieldWidth 最小占位宽度
 * @param fillChar 填充字符
 * @return 返回新XString
 * @note 内部构造单字符XString后委托 XString_arg 实现
 */
XString* XString_arg_char(const XString* str, XChar a, int fieldWidth, XChar fillChar);

/**
 * @brief 替换最低编号占位符为long long数值（对齐Qt QString::arg(qlonglong,fieldWidth,base,fillChar)）
 * @param str 含占位符的源XString
 * @param a 数值
 * @param fieldWidth 最小占位宽度
 * @param base 进制（2~36）
 * @param fillChar 填充字符
 * @return 返回新XString
 * @note 数值按base格式化后委托 XString_arg 实现
 */
XString* XString_arg_llong(const XString* str, long long a, int fieldWidth, int base, XChar fillChar);

/**
 * @brief 替换最低编号占位符为unsigned long long数值（对齐Qt QString::arg(qulonglong,...)）
 * @param str 含占位符的源XString
 * @param a 数值
 * @param fieldWidth 最小占位宽度
 * @param base 进制（2~36）
 * @param fillChar 填充字符
 * @return 返回新XString
 */
XString* XString_arg_ullong(const XString* str, unsigned long long a, int fieldWidth, int base, XChar fillChar);

/**
 * @brief 替换最低编号占位符为double数值（对齐Qt QString::arg(double,fieldWidth,format,precision,fillChar)）
 * @param str 含占位符的源XString
 * @param a 浮点数值
 * @param fieldWidth 最小占位宽度
 * @param format 格式（'e'/'E'科学计数, 'f'/'F'定点, 'g'/'G'自动）
 * @param precision 精度
 * @param fillChar 填充字符
 * @return 返回新XString
 */
XString* XString_arg_double(const XString* str, double a, int fieldWidth, char format, int precision, XChar fillChar);

/**
 * @brief arg(int,...) 重载，委托 XString_arg_llong（对齐Qt内联重载）
 * @note 宏实现，等价于 XString_arg_llong(str,(long long)(a),fieldWidth,base,fillChar)
 */
#define XString_arg_int(str, a, fieldWidth, base, fillChar)  XString_arg_llong(str, (long long)(a), fieldWidth, base, fillChar)
/**
 * @brief arg(uint,...) 重载，委托 XString_arg_ullong（对齐Qt内联重载）
 * @note 宏实现，等价于 XString_arg_ullong(str,(unsigned long long)(a),fieldWidth,base,fillChar)
 */
#define XString_arg_uint(str, a, fieldWidth, base, fillChar)  XString_arg_ullong(str, (unsigned long long)(a), fieldWidth, base, fillChar)
/**
 * @brief arg(long,...) 重载，委托 XString_arg_llong（对齐Qt内联重载）
 * @note 宏实现，等价于 XString_arg_llong(str,(long long)(a),fieldWidth,base,fillChar)
 */
#define XString_arg_long(str, a, fieldWidth, base, fillChar)  XString_arg_llong(str, (long long)(a), fieldWidth, base, fillChar)
/**
 * @brief arg(ulong,...) 重载，委托 XString_arg_ullong（对齐Qt内联重载）
 * @note 宏实现，等价于 XString_arg_ullong(str,(unsigned long long)(a),fieldWidth,base,fillChar)
 */
#define XString_arg_ulong(str, a, fieldWidth, base, fillChar)  XString_arg_ullong(str, (unsigned long long)(a), fieldWidth, base, fillChar)
/**
 * @brief arg(short,...) 重载，委托 XString_arg_llong（对齐Qt内联重载）
 * @note 宏实现，等价于 XString_arg_llong(str,(long long)(a),fieldWidth,base,fillChar)
 */
#define XString_arg_short(str, a, fieldWidth, base, fillChar)  XString_arg_llong(str, (long long)(a), fieldWidth, base, fillChar)
/**
 * @brief arg(ushort,...) 重载，委托 XString_arg_ullong（对齐Qt内联重载）
 * @note 宏实现，等价于 XString_arg_ullong(str,(unsigned long long)(a),fieldWidth,base,fillChar)
 */
#define XString_arg_ushort(str, a, fieldWidth, base, fillChar)  XString_arg_ullong(str, (unsigned long long)(a), fieldWidth, base, fillChar)
/**
 * @brief arg(float,...) 重载，委托 XString_arg_double（对齐Qt内联重载）
 * @note 宏实现，等价于 XString_arg_double(str,(double)(a),fieldWidth,format,precision,fillChar)
 */
#define XString_arg_float(str, a, fieldWidth, format, precision, fillChar)  XString_arg_double(str, (double)(a), fieldWidth, format, precision, fillChar)

// -------------------------- Qt 6.8 对齐：localeAwareCompare 区域感知比较 --------------------------

/**
 * @brief 按当前区域规则比较两个字符串（对齐Qt QString::localeAwareCompare）
 * @param str1 字符串1
 * @param str2 字符串2
 * @return 小于0表示str1<str2，等于0表示相等，大于0表示str1>str2
 * @note 基于C库strcoll实现，遵循LC_COLLATE区域设置；应用需setlocale(LC_ALL,"")以启用区域感知
 */
int32_t XString_localeAwareCompare(const XString* str1, const XString* str2);

// -------------------------- Qt 6.8 对齐：fromLatin1/fromUcs4/fromLocal8Bit 创建 --------------------------

/**
 * @brief 从Latin-1字符串创建XString（对齐Qt QString::fromLatin1）
 * @param latin1 Latin-1编码字符串
 * @return 新XString，latin1为NULL返回NULL
 */
XString* XString_create_latin1(const char* latin1);

/**
 * @brief 从指定长度的Latin-1字符串创建XString（对齐Qt QString::fromLatin1）
 * @param latin1 Latin-1编码字符串（uint8_t数组）
 * @param len 字符串长度（字节数，不含终止符）
 * @return 新XString，latin1为NULL或len为0返回空字符串
 */
XString* XString_create_with_length_latin1(const uint8_t* latin1, size_t len);

/**
 * @brief 从UTF-32(UCS-4)字符串创建XString（对齐Qt QString::fromUcs4）
 * @param ucs4 UTF-32编码字符串（以0结尾）
 * @return 新XString，ucs4为NULL返回NULL
 */
XString* XString_create_utf32(const uint32_t* ucs4);

/**
 * @brief 从指定长度的UTF-32(UCS-4)字符串创建XString（对齐Qt QString::fromUcs4）
 * @param ucs4 UTF-32编码字符串（uint32_t数组）
 * @param len 字符串长度（字符数，不含终止符）
 * @return 新XString，ucs4为NULL或len为0返回空字符串
 */
XString* XString_create_with_length_utf32(const uint32_t* ucs4, size_t len);

/**
 * @brief 从本地8位编码字符串创建XString（对齐Qt QString::fromLocal8Bit）
 * @param local_str 本地编码字符串（Windows为GBK，Linux为UTF-8）
 * @return 新XString，local_str为NULL返回NULL
 */
XString* XString_create_local(const char* local_str);

// -------------------------- Qt 6.8 对齐：别名宏 --------------------------

/**
 * @brief toLocal8Bit别名，返回本地8位编码（对齐Qt QString::toLocal8Bit）
 * @note 宏实现，等价于 XString_toLocal
 */
#define XString_toLocal8Bit				XString_toLocal
/**
 * @brief toUcs4别名，返回UTF-32(UCS-4)编码（对齐Qt QString::toUcs4）
 * @note 宏实现，等价于 XString_toUtf32
 */
#define XString_toUcs4					XString_toUtf32
/**
 * @brief fromLatin1别名（对齐Qt QString::fromLatin1）
 * @note 宏实现，等价于 XString_create_latin1
 */
#define XString_fromLatin1				XString_create_latin1
/**
 * @brief fromUcs4别名（对齐Qt QString::fromUcs4）
 * @note 宏实现，等价于 XString_create_utf32
 */
#define XString_fromUcs4					XString_create_utf32
/**
 * @brief fromLocal8Bit别名（对齐Qt QString::fromLocal8Bit）
 * @note 宏实现，等价于 XString_create_local
 */
#define XString_fromLocal8Bit			XString_create_local
/**
 * @brief fromUtf8别名（对齐Qt QString::fromUtf8）
 * @note 宏实现，等价于 XString_create_utf8
 */
#define XString_fromUtf8					XString_create_utf8
/**
 * @brief fromUtf16别名（对齐Qt QString::fromUtf16）
 * @note 宏实现，等价于 XString_create_utf16
 */
#define XString_fromUtf16					XString_create_utf16
/**
 * @brief asprintf别名，按printf格式创建新XString（对齐Qt QString::asprintf）
 * @note 宏实现，等价于 XString_create_fmt_utf8
 */
#define XString_asprintf					XString_create_fmt_utf8

#ifdef __cplusplus
}
#endif

#endif // XSTRING_H
