#ifndef XCOMMANDLINEOPTIONGROUP_H
#define XCOMMANDLINEOPTIONGROUP_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XVector.h"
#include "XString.h"
#include "XStringList.h"
#include "XCommandLineOption.h"

/**
 * @brief 命令行选项组结构体（对标 QCommandLineOptionGroup — Qt 6.8 中无此公开类，此为扩展）
 * 用于对相关选项进行分组管理，支持互斥组功能
 */
typedef struct {
    XString* name;               ///< 组名称（内部标识用）
    XString* description;        ///< 组描述（显示在帮助信息中）
    XVector* options;            ///< 组内选项列表（存储 XCommandLineOption*）
    bool isExclusive;            ///< 是否为互斥组（组内选项只能出现一个）
} XCommandLineOptionGroup;

/* ==================== XCommandLineOptionGroup API ==================== */

/**
 * @brief 创建命令行选项组
 * @param name 组名称字符串（用于内部标识），可为 NULL
 * @param description 组描述字符串（显示在帮助信息中），可为 NULL
 * @param isExclusive 是否为互斥组（组内选项只能出现一个）
 * @return 新创建的选项组指针，内存分配失败返回 NULL
 */
XCommandLineOptionGroup* XCommandLineOptionGroup_create(const XString* name,
    const XString* description,
    bool isExclusive);

/**
 * @brief 销毁命令行选项组
 * @param group 要销毁的选项组指针，传 NULL 无操作
 */
void XCommandLineOptionGroup_delete(XCommandLineOptionGroup* group);

/**
 * @brief 向选项组添加选项
 * @param group 目标选项组指针
 * @param option 要添加的选项指针（仅存储指针，不复制，需确保生命周期长于组）
 */
void XCommandLineOptionGroup_addOption(XCommandLineOptionGroup* group,
    const XCommandLineOption* option);

/**
 * @brief 获取选项组中的选项数量
 * @param group 选项组指针
 * @return 选项数量
 */
size_t XCommandLineOptionGroup_optionCount(const XCommandLineOptionGroup* group);

/**
 * @brief 获取选项组中指定索引的选项
 * @param group 选项组指针
 * @param index 索引值
 * @return 选项指针，越界返回 NULL
 */
const XCommandLineOption* XCommandLineOptionGroup_optionAt(const XCommandLineOptionGroup* group, size_t index);

#ifdef __cplusplus
}
#endif
#endif // XCOMMANDLINEOPTIONGROUP_H
