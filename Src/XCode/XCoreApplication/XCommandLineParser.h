#ifndef XCOMMANDLINEPARSER_H
#define XCOMMANDLINEPARSER_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include "XTypes.h"
#include "XVector.h"
#include "XHashMap.h"
#include "XString.h"
#include "XCommandLineOptionGroup.h"

/**
 * @brief 命令行解析结果结构体
 * 存储解析后的所有信息，包括选项值、位置参数等
 */
typedef struct {
    XVector* positionalArgs;        // 位置参数列表（char* 类型）
    XHashMap* optionMap;            // 选项键值对（key: char*, value: char*）
    XHashMap* optionCounts;         // 选项出现次数（key: char*, value: int）
    XVector* unrecognizedOpts;      // 未识别的选项（char* 类型）
    XVector* exclusiveGroupConflicts; // 互斥组冲突选项列表
    XVector* allocatedStrings;      // 跟踪动态分配的字符串，用于释放
} XCommandLineParseResult;

/**
 * @brief 命令行解析器结构体
 * 管理命令行选项定义和解析过程
 */
typedef struct {
    XVector* options;               // 所有选项列表（XCommandLineOption类型）
    XVector* groups;                // 选项组列表（XCommandLineOptionGroup*类型）
    XCommandLineParseResult* result; // 解析结果
    const char* programName;        // 程序名称（来自argv[0]）
    const char* applicationDescription; // 应用程序描述
} XCommandLineParser;

/**
 * @brief 创建命令行解析器
 * @return 新创建的解析器实例，内存分配失败返回NULL
 */
XCommandLineParser* XCommandLineParser_create();

/**
 * @brief 销毁命令行解析器
 * @param parser 要销毁的解析器实例，传NULL无操作
 */
void XCommandLineParser_delete(XCommandLineParser* parser);

/**
 * @brief 向解析器添加选项
 * @param parser 解析器实例
 * @param shortName 短选项名（如"h"），NULL表示无短选项
 * @param longName 长选项名（如"help"），NULL表示无长选项
 * @param description 选项描述
 * @param requiresValue 该选项是否需要参数值
 * @param isHidden 是否在帮助信息中隐藏该选项
 * @param defaultValue 选项的默认值
 * @note 至少需要提供短选项名或长选项名中的一个
 */
void XCommandLineParser_addOption(XCommandLineParser* parser,
    const char* shortName,
    const char* longName,
    const char* description,
    bool requiresValue,
    bool isHidden,
    const char* defaultValue);

/**
 * @brief 向解析器添加选项组
 * @param parser 解析器实例
 * @param group 要添加的选项组
 */
void XCommandLineParser_addOptionGroup(XCommandLineParser* parser,
    XCommandLineOptionGroup* group);

/**
 * @brief 解析命令行参数
 * @param parser 解析器实例
 * @param argc 参数数量（来自main函数）
 * @param argv 参数数组（来自main函数）
 * @return 解析成功返回true，否则返回false
 */
bool XCommandLineParser_parse(XCommandLineParser* parser, int argc, char** argv);

/**
 * @brief 检查是否存在指定选项
 * @param parser 解析器实例
 * @param option 选项名（短选项或长选项）
 * @return 存在返回true，否则返回false
 */
bool XCommandLineParser_hasOption(XCommandLineParser* parser, const char* option);

/**
 * @brief 获取选项的值
 * @param parser 解析器实例
 * @param option 选项名（短选项或长选项）
 * @return 选项的值，未找到时返回默认值，无默认值返回NULL
 */
const char* XCommandLineParser_getOptionValue(XCommandLineParser* parser, const char* option);

/**
 * @brief 获取位置参数列表
 * @param parser 解析器实例
 * @return 位置参数向量（char*类型），解析器为NULL时返回NULL
 */
XVector* XCommandLineParser_positionalArguments(XCommandLineParser* parser);

/**
 * @brief 获取未识别的选项列表
 * @param parser 解析器实例
 * @return 未识别选项向量（char*类型），解析器为NULL时返回NULL
 */
XVector* XCommandLineParser_unrecognizedOptions(XCommandLineParser* parser);

/**
 * @brief 获取选项出现的次数
 * @param parser 解析器实例
 * @param option 选项名
 * @return 选项出现的次数，未出现返回0
 */
int XCommandLineParser_optionCount(XCommandLineParser* parser, const char* option);

/**
 * @brief 获取互斥组冲突的选项列表
 * @param parser 解析器实例
 * @return 冲突选项向量，无冲突或解析器为NULL时返回NULL
 */
XVector* XCommandLineParser_exclusiveGroupConflicts(XCommandLineParser* parser);

/**
 * @brief 设置应用程序描述
 * @param parser 解析器实例
 * @param description 描述文本，NULL表示清空描述
 */
void XCommandLineParser_setApplicationDescription(XCommandLineParser* parser,
    const char* description);

/**
 * @brief 生成帮助信息文本
 * @param parser 解析器实例
 * @param description 应用程序描述，若已通过set方法设置则可传NULL
 * @return 包含帮助信息的XString实例，需调用者释放
 */
XString* XCommandLineParser_helpText(XCommandLineParser* parser, const char* description);

/**
 * @brief 生成版本信息文本
 * @param parser 解析器实例
 * @param version 版本字符串
 * @return 包含版本信息的XString实例，需调用者释放
 */
XString* XCommandLineParser_versionText(XCommandLineParser* parser, const char* version);

#ifdef __cplusplus
}
#endif
#endif // XCOMMANDLINEPARSER_H
