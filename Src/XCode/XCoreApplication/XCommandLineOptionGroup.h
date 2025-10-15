#ifndef XCOMMANDLINEOPTIONGROUP_H
#define XCOMMANDLINEOPTIONGROUP_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XVector.h"
#include "XString.h"
/**
 * @brief 命令行选项结构体
 * 描述单个命令行选项的属性和行为
 */
typedef struct {
    const char* shortName;       // 短选项名（如 "h" 对应 -h）
    const char* longName;        // 长选项名（如 "help" 对应 --help）
    const char* description;     // 选项描述（用于生成帮助信息）
    const char* defaultValue;    // 默认值字符串，当选项未指定时使用
    bool requiresValue;          // 标识该选项是否需要参数值
    bool isHidden;               // 标识该选项是否在帮助信息中隐藏
} XCommandLineOption;

/**
 * @brief 命令行选项组结构体
 * 用于对相关选项进行分组管理，支持互斥组功能
 */
typedef struct {
    const char* name;            // 组名称（内部标识用）
    const char* description;     // 组描述（显示在帮助信息中）
    XVector* options;            // 组内选项列表（存储XCommandLineOption*）
    bool isExclusive;            // 是否为互斥组（组内选项只能出现一个）
} XCommandLineOptionGroup;

/**
 * @brief 创建选项组
 * @param name 组名称，用于内部标识
 * @param description 组描述，显示在帮助信息中
 * @param isExclusive 是否为互斥组
 * @return 新创建的选项组实例，内存分配失败返回NULL
 */
XCommandLineOptionGroup* XCommandLineOptionGroup_create(const char* name,
    const char* description,
    bool isExclusive);

/**
 * @brief 销毁选项组
 * @param group 要销毁的选项组实例，传NULL无操作
 */
void XCommandLineOptionGroup_delete(XCommandLineOptionGroup* group);

/**
 * @brief 向选项组添加选项
 * @param group 目标选项组
 * @param option 要添加的选项
 * @note 选项不会被复制，仅存储指针，需确保选项生命周期长于组
 */
void XCommandLineOptionGroup_addOption(XCommandLineOptionGroup* group,
    const XCommandLineOption* option);

#ifdef __cplusplus
}
#endif
#endif // XCOMMANDLINEOPTIONGROUP_H
