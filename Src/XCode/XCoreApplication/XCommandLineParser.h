#ifndef XCOMMANDLINEPARSER_H
#define XCOMMANDLINEPARSER_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include "XTypes.h"

/**
 * @brief 命令行选项描述结构体
 * 用于定义一个可解析的命令行选项
 */
typedef struct {
    const char* shortName;  // 短选项名（如 "h" 对应 -h）
    const char* longName;   // 长选项名（如 "help" 对应 --help）
    const char* description;// 选项描述（用于生成帮助信息）
    bool requiresValue;     // 是否需要参数值（如 --config=xxx 需要值）
} XCommandLineOption;

/**
 * @brief 命令行解析结果结构体
 * 存储解析后的选项、位置参数及未识别选项
 */
typedef struct
{
    XVector* positionalArgs;      // 位置参数列表（char* 类型）
    XHashMap* optionMap;          // 选项键值对（key: char*, value: char*）
    XVector* unrecognizedOpts;    // 未识别的选项（char* 类型）
} XCommandLineParseResult;

/**
 * @brief 命令行解析器结构体
 * 用于注册选项并解析命令行参数
 */
typedef struct {
    XVector* options;             // 已注册的选项列表（XCommandLineOption 类型）
    XCommandLineParseResult* result; // 解析结果
    const char* programName;      // 程序名称（argv[0]）
} XCommandLineParser;

/**
 * @brief 创建命令行解析器
 * @return 新创建的解析器实例，失败返回 NULL
 */
XCommandLineParser* XCommandLineParser_create();

/**
 * @brief 销毁命令行解析器
 * @param parser 要销毁的解析器实例
 */
void XCommandLineParser_delete(XCommandLineParser* parser);

/**
 * @brief 注册命令行选项
 * @param parser 解析器实例
 * @param shortName 短选项名（NULL 表示无短选项）
 * @param longName 长选项名（NULL 表示无长选项）
 * @param description 选项描述
 * @param requiresValue 是否需要参数值
 */
void XCommandLineParser_addOption(XCommandLineParser* parser,
    const char* shortName,
    const char* longName,
    const char* description,
    bool requiresValue);

/**
 * @brief 解析命令行参数
 * @param parser 解析器实例
 * @param argc 参数数量
 * @param argv 参数数组
 * @return 解析是否成功
 */
bool XCommandLineParser_parse(XCommandLineParser* parser, int argc, char** argv);

/**
 * @brief 检查是否存在指定选项
 * @param parser 解析器实例
 * @param option 选项名（短选项不带 '-', 长选项不带 '--'）
 * @return 存在返回 true，否则返回 false
 */
bool XCommandLineParser_hasOption(XCommandLineParser* parser, const char* option);

/**
 * @brief 获取选项的值
 * @param parser 解析器实例
 * @param option 选项名（短选项不带 '-', 长选项不带 '--'）
 * @return 选项值（NULL 表示无此选项或无值）
 */
const char* XCommandLineParser_getOptionValue(XCommandLineParser* parser, const char* option);

/**
 * @brief 获取位置参数列表
 * @param parser 解析器实例
 * @return 位置参数向量（char* 类型），需通过 XVector 接口访问
 */
XVector* XCommandLineParser_positionalArguments(XCommandLineParser* parser);

/**
 * @brief 获取未识别的选项列表
 * @param parser 解析器实例
 * @return 未识别选项向量（char* 类型）
 */
XVector* XCommandLineParser_unrecognizedOptions(XCommandLineParser* parser);

/**
 * @brief 生成帮助信息
 * @param parser 解析器实例
 * @param description 程序功能描述
 * @return 帮助信息字符串（需调用 XString_delete 释放）
 */
XString* XCommandLineParser_helpText(XCommandLineParser* parser, const char* description);

/**
 * @brief 生成版本信息
 * @param parser 解析器实例
 * @param version 版本字符串
 * @return 版本信息字符串（需调用 XString_delete 释放）
 */
XString* XCommandLineParser_versionText(XCommandLineParser* parser, const char* version);

#ifdef __cplusplus
}
#endif
#endif // XCOMMANDLINEPARSER_H