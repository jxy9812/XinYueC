#ifndef XCOMMANDLINEOPTION_H
#define XCOMMANDLINEOPTION_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XVector.h"
#include "XString.h"
#include "XStringList.h"

/**
 * @brief 命令行选项标志枚举（对标 QCommandLineOption::Flag）
 */
typedef enum {
    XCOMMANDLINE_OPTION_FLAG_NONE              = 0,     ///< 无特殊标志
    XCOMMANDLINE_OPTION_FLAG_HIDDEN_FROM_HELP  = 0x1,   ///< 在帮助信息中隐藏
    XCOMMANDLINE_OPTION_FLAG_SHORT_OPTION_STYLE = 0x2   ///< 短选项风格
} XCommandLineOptionFlag;

/**
 * @brief 命令行选项结构体（对标 QCommandLineOption）
 */
typedef struct {
    XStringList* names;         ///< 选项名称列表（如 {"o", "output"}）
    XString* description;       ///< 选项描述
    XString* valueName;         ///< 选项值名称（如 "file"）
    XStringList* defaultValues; ///< 默认值列表
    int flags;                  ///< 标志位（XCommandLineOptionFlag 组合）
} XCommandLineOption;

/* ==================== XCommandLineOption API（对标 QCommandLineOption） ==================== */

/**
 * @brief 创建命令行选项（仅指定名称，XString 主版本）
 * @param name 选项名称（如 "o" 或 "output"），可为 NULL；只在调用期间借用，不取得所有权
 * @return 新创建的选项指针，内存分配失败返回 NULL
 */
XCommandLineOption* XCommandLineOption_create(const XString* name);

/**
 * @brief 创建命令行选项（仅指定名称，UTF-8 兼容重载）
 * @param name UTF-8 编码的选项名称，可为 NULL；内部创建临时 XString 后转发到 XString 主版本
 * @return 新创建的选项指针，内存分配失败返回 NULL
 */
XCommandLineOption* XCommandLineOption_create_2(const char* name);

/**
 * @brief 创建命令行选项（指定多个名称）
 * @param names 选项名称字符串列表
 * @return 新创建的选项指针，内存分配失败返回 NULL
 */
XCommandLineOption* XCommandLineOption_createWithNames(const XStringList* names);

/**
 * @brief 创建完整的命令行选项（指定名称、描述、值名称和默认值，XString 主版本）
 * @param name 选项名称；只在调用期间借用，不取得所有权
 * @param description 选项描述；可为 NULL，借用参数
 * @param valueName 选项值名称（如 "file"），可为 NULL，借用参数
 * @param defaultValue 默认值，可为 NULL，借用参数
 * @return 新创建的选项指针，内存分配失败返回 NULL
 */
XCommandLineOption* XCommandLineOption_createFull(const XString* name, const XString* description,
    const XString* valueName, const XString* defaultValue);

/**
 * @brief 创建完整的命令行选项（UTF-8 兼容重载）
 * @param name UTF-8 编码的选项名称，可为 NULL
 * @param description UTF-8 编码的选项描述，可为 NULL
 * @param valueName UTF-8 编码的选项值名称，可为 NULL
 * @param defaultValue UTF-8 编码的默认值，可为 NULL
 * @return 新创建的选项指针，内存分配失败返回 NULL
 * @note 各 UTF-8 参数先转换为临时 XString，再转发到 XString 主版本
 */
XCommandLineOption* XCommandLineOption_createFull_2(const char* name, const char* description,
    const char* valueName, const char* defaultValue);

/**
 * @brief 创建完整的命令行选项（指定多个名称、描述、值名称和默认值，XString 主版本）
 * @param names 选项名称字符串列表
 * @param description 选项描述；可为 NULL，借用参数
 * @param valueName 选项值名称，可为 NULL，借用参数
 * @param defaultValue 默认值，可为 NULL，借用参数
 * @return 新创建的选项指针，内存分配失败返回 NULL
 */
XCommandLineOption* XCommandLineOption_createFullWithNames(const XStringList* names,
    const XString* description, const XString* valueName, const XString* defaultValue);

/**
 * @brief 创建完整的命令行选项（指定多个名称，UTF-8 兼容重载）
 * @param names 选项名称字符串列表
 * @param description UTF-8 编码的选项描述，可为 NULL
 * @param valueName UTF-8 编码的选项值名称，可为 NULL
 * @param defaultValue UTF-8 编码的默认值，可为 NULL
 * @return 新创建的选项指针，内存分配失败返回 NULL
 * @note 各 UTF-8 参数先转换为临时 XString，再转发到 XString 主版本
 */
XCommandLineOption* XCommandLineOption_createFullWithNames_2(const XStringList* names,
    const char* description, const char* valueName, const char* defaultValue);

/**
 * @brief 销毁命令行选项
 * @param option 要销毁的选项指针，传 NULL 无操作
 */
void XCommandLineOption_delete(XCommandLineOption* option);

/**
 * @brief 获取选项名称列表
 * @param option 选项指针
 * @return 名称字符串列表指针
 */
const XStringList* XCommandLineOption_names(const XCommandLineOption* option);

/**
 * @brief 添加选项名称（XString 主版本）
 * @param option 选项指针
 * @param name 要添加的名称；只在调用期间借用，内部拷贝进选项名称列表
 */
void XCommandLineOption_addName(XCommandLineOption* option, const XString* name);

/**
 * @brief 添加选项名称（UTF-8 兼容重载）
 * @param option 选项指针
 * @param name UTF-8 编码的名称；内部创建临时 XString 后转发到 XString 主版本
 */
void XCommandLineOption_addName_2(XCommandLineOption* option, const char* name);

/**
 * @brief 设置选项值名称（XString 主版本）
 * @param option 选项指针
 * @param name 值名称（如 "file"），可为 NULL；拷贝进选项，调用方可立即释放
 */
void XCommandLineOption_setValueName(XCommandLineOption* option, const XString* name);

/**
 * @brief 设置选项值名称（UTF-8 兼容重载）
 * @param option 选项指针
 * @param name UTF-8 编码的值名称，可为 NULL；内部创建临时 XString 后转发到 XString 主版本
 */
void XCommandLineOption_setValueName_2(XCommandLineOption* option, const char* name);

/**
 * @brief 获取选项值名称
 * @param option 选项指针
 * @return 值名称字符串，未设置返回 NULL
 */
const char* XCommandLineOption_valueName(const XCommandLineOption* option);

/**
 * @brief 设置选项描述（XString 主版本）
 * @param option 选项指针
 * @param description 描述，可为 NULL；拷贝进选项，调用方可立即释放
 */
void XCommandLineOption_setDescription(XCommandLineOption* option, const XString* description);

/**
 * @brief 设置选项描述（UTF-8 兼容重载）
 * @param option 选项指针
 * @param description UTF-8 编码的描述，可为 NULL；内部创建临时 XString 后转发到 XString 主版本
 */
void XCommandLineOption_setDescription_2(XCommandLineOption* option, const char* description);

/**
 * @brief 获取选项描述
 * @param option 选项指针
 * @return 描述字符串，未设置返回 NULL
 */
const char* XCommandLineOption_description(const XCommandLineOption* option);

/**
 * @brief 设置选项默认值（单值，XString 主版本）
 * @param option 选项指针
 * @param value 默认值，可为 NULL；拷贝进选项的默认值列表，调用方可立即释放
 */
void XCommandLineOption_setDefaultValue(XCommandLineOption* option, const XString* value);

/**
 * @brief 设置选项默认值（单值，UTF-8 兼容重载）
 * @param option 选项指针
 * @param value UTF-8 编码的默认值，可为 NULL；内部创建临时 XString 后转发到 XString 主版本
 */
void XCommandLineOption_setDefaultValue_2(XCommandLineOption* option, const char* value);

/**
 * @brief 设置选项默认值列表
 * @param option 选项指针
 * @param values 默认值字符串列表
 */
void XCommandLineOption_setDefaultValues(XCommandLineOption* option, const XStringList* values);

/**
 * @brief 获取选项默认值列表
 * @param option 选项指针
 * @return 默认值字符串列表指针
 */
const XStringList* XCommandLineOption_defaultValues(const XCommandLineOption* option);

/**
 * @brief 设置选项标志
 * @param option 选项指针
 * @param flags 标志位（XCommandLineOptionFlag 组合）
 */
void XCommandLineOption_setFlags(XCommandLineOption* option, int flags);

/**
 * @brief 获取选项标志
 * @param option 选项指针
 * @return 标志位
 */
int XCommandLineOption_flags(const XCommandLineOption* option);

/**
 * @brief 检查选项是否在帮助信息中隐藏
 * @param option 选项指针
 * @return true 隐藏，false 显示
 */
bool XCommandLineOption_isHidden(const XCommandLineOption* option);

/**
 * @brief 检查选项是否需要值
 * @param option 选项指针
 * @return true 需要值，false 不需要
 */
bool XCommandLineOption_requiresValue(const XCommandLineOption* option);


#ifdef __cplusplus
}
#endif
#endif // XCOMMANDLINEOPTION_H
