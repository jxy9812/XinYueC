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
 * @brief 创建命令行选项（仅指定名称）
 * @param name 选项名称字符串（如 "o" 或 "output"）
 * @return 新创建的选项指针，内存分配失败返回 NULL
 */
XCommandLineOption* XCommandLineOption_create(const char* name);

/**
 * @brief 创建命令行选项（指定多个名称）
 * @param names 选项名称字符串列表
 * @return 新创建的选项指针，内存分配失败返回 NULL
 */
XCommandLineOption* XCommandLineOption_createWithNames(const XStringList* names);

/**
 * @brief 创建完整的命令行选项（指定名称、描述、值名称和默认值）
 * @param name 选项名称字符串
 * @param description 选项描述字符串
 * @param valueName 选项值名称字符串（如 "file"），可为 NULL
 * @param defaultValue 默认值字符串，可为 NULL
 * @return 新创建的选项指针，内存分配失败返回 NULL
 */
XCommandLineOption* XCommandLineOption_createFull(const char* name, const char* description,
    const char* valueName, const char* defaultValue);

/**
 * @brief 创建完整的命令行选项（指定多个名称、描述、值名称和默认值）
 * @param names 选项名称字符串列表
 * @param description 选项描述字符串
 * @param valueName 选项值名称字符串，可为 NULL
 * @param defaultValue 默认值字符串，可为 NULL
 * @return 新创建的选项指针，内存分配失败返回 NULL
 */
XCommandLineOption* XCommandLineOption_createFullWithNames(const XStringList* names,
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
 * @brief 添加选项名称
 * @param option 选项指针
 * @param name 要添加的名称字符串
 */
void XCommandLineOption_addName(XCommandLineOption* option, const char* name);

/**
 * @brief 设置选项值名称
 * @param option 选项指针
 * @param name 值名称字符串（如 "file"），可为 NULL
 */
void XCommandLineOption_setValueName(XCommandLineOption* option, const char* name);

/**
 * @brief 获取选项值名称
 * @param option 选项指针
 * @return 值名称字符串，未设置返回 NULL
 */
const char* XCommandLineOption_valueName(const XCommandLineOption* option);

/**
 * @brief 设置选项描述
 * @param option 选项指针
 * @param description 描述字符串，可为 NULL
 */
void XCommandLineOption_setDescription(XCommandLineOption* option, const char* description);

/**
 * @brief 获取选项描述
 * @param option 选项指针
 * @return 描述字符串，未设置返回 NULL
 */
const char* XCommandLineOption_description(const XCommandLineOption* option);

/**
 * @brief 设置选项默认值（单值）
 * @param option 选项指针
 * @param value 默认值字符串，可为 NULL
 */
void XCommandLineOption_setDefaultValue(XCommandLineOption* option, const char* value);

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
