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
#include "XStringList.h"
#include "XCommandLineOption.h"

/**
 * @brief 命令行解析器（对标 QCommandLineParser）
 */

/**
 * @brief 单破折号单词选项模式枚举（对标 QCommandLineParser::SingleDashWordOptionMode）
 */
typedef enum {
    XCOMMANDLINE_PARSER_PARSE_AS_COMPACTED_SHORT_OPTIONS = 0,  ///< 将单破折号单词解析为多个短选项（如 -abc = -a -b -c）
    XCOMMANDLINE_PARSER_PARSE_AS_LONG_OPTIONS = 1              ///< 将单破折号单词解析为长选项（如 -abc 作为选项名）
} XCommandLineParserSingleDashWordOptionMode;

/**
 * @brief 位置参数后的选项处理模式枚举（对标 QCommandLineParser::OptionsAfterPositionalArgumentsMode）
 */
typedef enum {
    XCOMMANDLINE_PARSER_PARSE_AS_OPTIONS = 0,              ///< 位置参数后的参数仍解析为选项
    XCOMMANDLINE_PARSER_PARSE_AS_POSITIONAL_ARGUMENTS = 1  ///< 位置参数后的参数解析为位置参数
} XCommandLineParserOptionsAfterPositionalArgumentsMode;

/**
 * @brief 位置参数定义结构体
 */
typedef struct {
    XString* name;         ///< 参数名称
    XString* description;  ///< 参数描述
    XString* syntax;       ///< 参数语法（如 "[file]"）
} XPositionalArgumentDefinition;

/**
 * @brief 命令行解析器结构体（对标 QCommandLineParser）
 */
typedef struct XCommandLineParser {
    XString* m_errorText;                                        ///< 解析错误文本
    XVector* m_commandLineOptionList;                            ///< 已注册的选项列表
    XHashMap* m_nameHash;                                        ///< 选项名称到索引的哈希表
    XHashMap* m_optionValuesHash;                                ///< 选项索引到值的哈希表
    XStringList* m_optionNames;                                  ///< 解析到的选项名称列表
    XStringList* m_positionalArgumentList;                       ///< 位置参数列表
    XStringList* m_unknownOptionNames;                           ///< 未知选项名称列表
    XString* m_description;                                      ///< 应用程序描述
    XVector* m_positionalArgumentDefinitions;                    ///< 位置参数定义列表
    XCommandLineParserSingleDashWordOptionMode m_singleDashWordOptionMode;          ///< 单破折号单词选项模式
    XCommandLineParserOptionsAfterPositionalArgumentsMode m_optionsAfterPositionalArgumentsMode;  ///< 位置参数后选项处理模式
    bool m_builtinVersionOption;                                 ///< 是否已添加内置版本选项
    bool m_builtinHelpOption;                                    ///< 是否已添加内置帮助选项
    bool m_needsParsing;                                         ///< 是否需要重新解析
} XCommandLineParser;

/* ==================== 构造/析构 ==================== */

/**
 * @brief 创建命令行解析器（对标 QCommandLineParser 构造函数）
 * @return 新创建的解析器指针，内存分配失败返回 NULL
 */
XCommandLineParser* XCommandLineParser_create(void);

/**
 * @brief 销毁命令行解析器（对标 QCommandLineParser 析构函数）
 * @param parser 要销毁的解析器指针，传 NULL 无操作
 */
void XCommandLineParser_delete(XCommandLineParser* parser);

/* ==================== 模式设置 ==================== */

/**
 * @brief 设置单破折号单词选项模式（对标 QCommandLineParser::setSingleDashWordOptionMode）
 * @param parser 解析器指针
 * @param mode 模式枚举值
 */
void XCommandLineParser_setSingleDashWordOptionMode(XCommandLineParser* parser,
    XCommandLineParserSingleDashWordOptionMode mode);

/**
 * @brief 设置位置参数后的选项处理模式（对标 QCommandLineParser::setOptionsAfterPositionalArgumentsMode）
 * @param parser 解析器指针
 * @param mode 模式枚举值
 */
void XCommandLineParser_setOptionsAfterPositionalArgumentsMode(XCommandLineParser* parser,
    XCommandLineParserOptionsAfterPositionalArgumentsMode mode);

/* ==================== 选项定义 ==================== */

/**
 * @brief 添加命令行选项（对标 QCommandLineParser::addOption）
 * @param parser 解析器指针
 * @param option 要添加的选项指针
 * @return true 添加成功，false 失败（如名称重复）
 */
bool XCommandLineParser_addOption(XCommandLineParser* parser, const XCommandLineOption* option);

/**
 * @brief 批量添加命令行选项（对标 QCommandLineParser::addOptions）
 * @param parser 解析器指针
 * @param options 选项指针数组
 * @param count 选项数量
 * @return true 全部添加成功，false 有失败
 */
bool XCommandLineParser_addOptions(XCommandLineParser* parser, const XCommandLineOption** options, size_t count);

/**
 * @brief 添加内置版本选项（对标 QCommandLineParser::addVersionOption）
 * @param parser 解析器指针
 * @return 创建的版本选项指针
 */
XCommandLineOption* XCommandLineParser_addVersionOption(XCommandLineParser* parser);

/**
 * @brief 添加内置帮助选项（对标 QCommandLineParser::addHelpOption）
 * @param parser 解析器指针
 * @return 创建的帮助选项指针
 */
XCommandLineOption* XCommandLineParser_addHelpOption(XCommandLineParser* parser);

/**
 * @brief 设置应用程序描述（对标 QCommandLineParser::setApplicationDescription）
 * @param parser 解析器指针
 * @param description 描述字符串
 */
void XCommandLineParser_setApplicationDescription(XCommandLineParser* parser, const char* description);

/**
 * @brief 获取应用程序描述（对标 QCommandLineParser::applicationDescription）
 * @param parser 解析器指针
 * @return 描述字符串
 */
const char* XCommandLineParser_applicationDescription(const XCommandLineParser* parser);

/**
 * @brief 添加位置参数定义（对标 QCommandLineParser::addPositionalArgument）
 * @param parser 解析器指针
 * @param name 参数名称
 * @param description 参数描述
 * @param syntax 参数语法（如 "[file]"），可为 NULL
 */
void XCommandLineParser_addPositionalArgument(XCommandLineParser* parser, const char* name,
    const char* description, const char* syntax);

/**
 * @brief 清除所有位置参数定义（对标 QCommandLineParser::clearPositionalArguments）
 * @param parser 解析器指针
 */
void XCommandLineParser_clearPositionalArguments(XCommandLineParser* parser);

/* 前向声明 */
typedef struct XCoreApplication XCoreApplication;

/* ==================== 解析 ==================== */

/**
 * @brief 解析命令行参数并处理内置选项（对标 QCommandLineParser::process）
 * @param parser 解析器指针
 * @param args 参数字符串列表
 */
void XCommandLineParser_process(XCommandLineParser* parser, const XStringList* args);

/**
 * @brief 从 XCoreApplication 获取参数并解析（对标 QCommandLineParser::process(const QCoreApplication &)）
 * @param parser 解析器指针
 * @param app XCoreApplication 实例指针
 */
void XCommandLineParser_processApplication(XCommandLineParser* parser, const XCoreApplication* app);

/**
 * @brief 解析命令行参数（对标 QCommandLineParser::parse）
 * @param parser 解析器指针
 * @param args 参数字符串列表
 * @return true 解析成功，false 解析失败
 */
bool XCommandLineParser_parse(XCommandLineParser* parser, const XStringList* args);

/**
 * @brief 获取解析错误文本（对标 QCommandLineParser::errorText）
 * @param parser 解析器指针
 * @return 错误文本字符串
 */
const char* XCommandLineParser_errorText(const XCommandLineParser* parser);

/* ==================== 查询 ==================== */

/**
 * @brief 检查选项是否已设置（按名称）（对标 QCommandLineParser::isSet）
 * @param parser 解析器指针
 * @param name 选项名称
 * @return true 已设置，false 未设置
 */
bool XCommandLineParser_isSet(XCommandLineParser* parser, const char* name);

/**
 * @brief 检查选项是否已设置（按选项对象）（对标 QCommandLineParser::isSet）
 * @param parser 解析器指针
 * @param option 选项指针
 * @return true 已设置，false 未设置
 */
bool XCommandLineParser_isSetOption(XCommandLineParser* parser, const XCommandLineOption* option);

/**
 * @brief 获取选项值（按名称）（对标 QCommandLineParser::value）
 * @param parser 解析器指针
 * @param name 选项名称
 * @return 选项值字符串，未设置返回 NULL
 */
const char* XCommandLineParser_value(XCommandLineParser* parser, const char* name);

/**
 * @brief 获取选项值（按选项对象）（对标 QCommandLineParser::value）
 * @param parser 解析器指针
 * @param option 选项指针
 * @return 选项值字符串，未设置返回 NULL
 */
const char* XCommandLineParser_valueOption(XCommandLineParser* parser, const XCommandLineOption* option);

/**
 * @brief 获取选项值列表（按名称）（对标 QCommandLineParser::values）
 * @param parser 解析器指针
 * @param name 选项名称
 * @return 选项值字符串列表指针
 */
const XStringList* XCommandLineParser_values(XCommandLineParser* parser, const char* name);

/**
 * @brief 获取选项值列表（按选项对象）（对标 QCommandLineParser::values）
 * @param parser 解析器指针
 * @param option 选项指针
 * @return 选项值字符串列表指针
 */
const XStringList* XCommandLineParser_valuesOption(XCommandLineParser* parser, const XCommandLineOption* option);

/**
 * @brief 获取位置参数列表（对标 QCommandLineParser::positionalArguments）
 * @param parser 解析器指针
 * @return 位置参数字符串列表指针
 */
const XStringList* XCommandLineParser_positionalArguments(const XCommandLineParser* parser);

/**
 * @brief 获取已解析的选项名称列表（对标 QCommandLineParser::optionNames）
 * @param parser 解析器指针
 * @return 选项名称字符串列表指针
 */
const XStringList* XCommandLineParser_optionNames(const XCommandLineParser* parser);

/**
 * @brief 获取未知选项名称列表（对标 QCommandLineParser::unknownOptionNames）
 * @param parser 解析器指针
 * @return 未知选项名称字符串列表指针
 */
const XStringList* XCommandLineParser_unknownOptionNames(const XCommandLineParser* parser);

/* ==================== 显示 ==================== */

/**
 * @brief 显示版本信息并退出（对标 QCommandLineParser::showVersion）
 * @param parser 解析器指针
 */
void XCommandLineParser_showVersion(XCommandLineParser* parser);

/**
 * @brief 显示帮助信息并退出（对标 QCommandLineParser::showHelp）
 * @param parser 解析器指针
 * @param exitCode 退出码
 */
void XCommandLineParser_showHelp(XCommandLineParser* parser, int exitCode);

/**
 * @brief 生成帮助文本（对标 QCommandLineParser::helpText）
 * @param parser 解析器指针
 * @return 帮助文本字符串，调用者负责释放
 */
XString* XCommandLineParser_helpText(const XCommandLineParser* parser);


#ifdef __cplusplus
}
#endif
#endif // XCOMMANDLINEPARSER_H
