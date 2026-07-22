#include "XCommandLineParser.h"
#include "XVector.h"
#include "XHashMap.h"
#include "XString.h"
#include "XStringList.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "XCoreApplication.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ==================== 内部辅助函数 ==================== */

static char* xStrDup(const char* str)
{
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* copy = XMalloc_System(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

static int stringHash(const void* key)
{
    const char* s = *(const char**)key;
    if (!s) return 0;
    unsigned long hash = 5381;
    int c;
    while ((c = *s++))
        hash = ((hash << 5) + hash) + (unsigned char)c;
    return (int)(hash & 0x7FFFFFFF);
}

static int stringCompare(const void* a, const void* b)
{
    const char* sa = *(const char**)a;
    const char* sb = *(const char**)b;
    if (!sa && !sb) return 0;
    if (!sa) return -1;
    if (!sb) return 1;
    return strcmp(sa, sb);
}

static uint64_t XHash_string(const void* key, size_t len)
{
    (void)len;
    const char* str = *(const char**)key;
    if (!str) return 0;
    return XHash_xxhash64(str, strlen(str));
}

/* ==================== 构造/析构 ==================== */

XCommandLineParser* XCommandLineParser_create(void)
{
    XCommandLineParser* parser = XMalloc_System(sizeof(XCommandLineParser));
    if (!parser) return NULL;

    parser->m_errorText = XString_create();
    parser->m_commandLineOptionList = XVector_create(sizeof(XCommandLineOption*));
    parser->m_nameHash = XHashMap_create(sizeof(char*), sizeof(int), XHash_string, stringCompare);
    parser->m_optionValuesHash = XHashMap_create(sizeof(int), sizeof(XStringList*), XHash_xxhash64, int_compare);
    parser->m_optionNames = XStringList_create();
    parser->m_positionalArgumentList = XStringList_create();
    parser->m_unknownOptionNames = XStringList_create();
    parser->m_description = XString_create();
    parser->m_positionalArgumentDefinitions = XVector_create(sizeof(XPositionalArgumentDefinition));
    parser->m_singleDashWordOptionMode = XCOMMANDLINE_PARSER_PARSE_AS_COMPACTED_SHORT_OPTIONS;
    parser->m_optionsAfterPositionalArgumentsMode = XCOMMANDLINE_PARSER_PARSE_AS_OPTIONS;
    parser->m_builtinVersionOption = false;
    parser->m_builtinHelpOption = false;
    parser->m_needsParsing = true;

    if (!parser->m_errorText || !parser->m_commandLineOptionList || !parser->m_nameHash ||
        !parser->m_optionValuesHash || !parser->m_optionNames || !parser->m_positionalArgumentList ||
        !parser->m_unknownOptionNames || !parser->m_description || !parser->m_positionalArgumentDefinitions) {
        XCommandLineParser_delete(parser);
        return NULL;
    }

    return parser;
}

void XCommandLineParser_delete(XCommandLineParser* parser)
{
    if (!parser) return;
    XString_delete_base(parser->m_errorText);
    // 释放选项列表中的选项
    if (parser->m_commandLineOptionList) {
        for (size_t i = 0; i < XVector_size_base(parser->m_commandLineOptionList); ++i) {
            XCommandLineOption** opt = (XCommandLineOption**)XVector_at_base(parser->m_commandLineOptionList, i);
            if (opt && *opt) XCommandLineOption_delete(*opt);
        }
        XVector_delete_base(parser->m_commandLineOptionList);
    }
    XHashMap_delete_base(parser->m_nameHash);
    // 释放选项值哈希表中的值列表
    if (parser->m_optionValuesHash) {
        // TODO: free XStringList* values if needed
        XHashMap_delete_base(parser->m_optionValuesHash);
    }
    XStringList_delete_base(parser->m_optionNames);
    XStringList_delete_base(parser->m_positionalArgumentList);
    XStringList_delete_base(parser->m_unknownOptionNames);
    XString_delete_base(parser->m_description);
    // 释放位置参数定义
    if (parser->m_positionalArgumentDefinitions) {
        for (size_t i = 0; i < XVector_size_base(parser->m_positionalArgumentDefinitions); ++i) {
            XPositionalArgumentDefinition* def = (XPositionalArgumentDefinition*)XVector_at_base(parser->m_positionalArgumentDefinitions, i);
            XString_delete_base(def->name);
            XString_delete_base(def->description);
            XString_delete_base(def->syntax);
        }
        XVector_delete_base(parser->m_positionalArgumentDefinitions);
    }
    XFree_System(parser);
}

/* ==================== 模式设置 ==================== */

void XCommandLineParser_setSingleDashWordOptionMode(XCommandLineParser* parser,
    XCommandLineParserSingleDashWordOptionMode mode)
{
    if (parser) parser->m_singleDashWordOptionMode = mode;
}

void XCommandLineParser_setOptionsAfterPositionalArgumentsMode(XCommandLineParser* parser,
    XCommandLineParserOptionsAfterPositionalArgumentsMode mode)
{
    if (parser) parser->m_optionsAfterPositionalArgumentsMode = mode;
}

/* ==================== 内部：名称查找 ==================== */

static int findOptionIndex(XCommandLineParser* parser, const char* name)
{
    if (!parser || !name) return -1;
    const char* key = name;
    int* idx = (int*)XHashMap_value_base(parser->m_nameHash, &key);
    return idx ? *idx : -1;
}

static XCommandLineOption* findOption(XCommandLineParser* parser, const char* name)
{
    int idx = findOptionIndex(parser, name);
    if (idx < 0) return NULL;
    XCommandLineOption** opt = (XCommandLineOption**)XVector_at_base(parser->m_commandLineOptionList, idx);
    return opt ? *opt : NULL;
}

/* ==================== 选项定义 ==================== */

bool XCommandLineParser_addOption(XCommandLineParser* parser, const XCommandLineOption* option)
{
    if (!parser || !option) return false;

    const XStringList* names = XCommandLineOption_names(option);
    if (!names || XStringList_size_base(names) == 0) return false;

    // 检查名称冲突
    for (size_t i = 0; i < XStringList_size_base(names); ++i) {
        const XString* s = XStringList_at_base(names, i);
        if (s) {
            const char* nameStr = XString_toUtf8(s);
            if (findOptionIndex(parser, nameStr) >= 0)
                return false; // 名称已存在
        }
    }

    // 复制选项
    XCommandLineOption* copy = XMalloc_System(sizeof(XCommandLineOption));
    if (!copy) return false;
    *copy = *option;
    copy->names = NULL;
    copy->description = NULL;
    copy->valueName = NULL;
    copy->defaultValues = NULL;

    // 复制名称列表
    copy->names = XStringList_create();
    if (!copy->names) { XFree_System(copy); return false; }
    for (size_t i = 0; i < XStringList_size_base(names); ++i) {
        const XString* s = XStringList_at_base(names, i);
        if (s) {
                XString* copy_s = XString_create_copy(s);
                if (copy_s) {
                    XStringList_push_back_move_base(copy->names, copy_s);
                    XString_delete_base(copy_s);
                }
            }
    }

    // 复制描述
    const char* desc = XCommandLineOption_description(option);
    if (desc) copy->description = XString_create_utf8(desc);

    // 复制值名称
    const char* vn = XCommandLineOption_valueName(option);
    if (vn) copy->valueName = XString_create_utf8(vn);

    // 复制默认值
    const XStringList* dvals = XCommandLineOption_defaultValues(option);
    if (dvals) {
        copy->defaultValues = XStringList_create();
        for (size_t i = 0; i < XStringList_size_base(dvals); ++i) {
            const XString* s = XStringList_at_base(dvals, i);
            if (s) {
                    XString* copy_dv = XString_create_copy(s);
                    if (copy_dv) {
                        XStringList_push_back_move_base(copy->defaultValues, copy_dv);
                        XString_delete_base(copy_dv);
                    }
                }
        }
    }

    copy->flags = option->flags;

    // 添加到选项列表
    size_t idx = XVector_size_base(parser->m_commandLineOptionList);
    XVector_push_back_1_base(parser->m_commandLineOptionList, &copy);

    // 注册所有名称到哈希表
    for (size_t i = 0; i < XStringList_size_base(copy->names); ++i) {
        const XString* s = XStringList_at_base(copy->names, i);
        if (s) {
            const char* nameStr = XString_toUtf8(s);
            char* key = xStrDup(nameStr);
            if (key) {
                XMapBase_insert_base(parser->m_nameHash, &key, &idx);
            }
        }
    }

    parser->m_needsParsing = true;
    return true;
}

bool XCommandLineParser_addOptions(XCommandLineParser* parser, const XCommandLineOption** options, size_t count)
{
    bool result = true;
    for (size_t i = 0; i < count; ++i) {
        if (!XCommandLineParser_addOption(parser, options[i]))
            result = false;
    }
    return result;
}

XCommandLineOption* XCommandLineParser_addVersionOption(XCommandLineParser* parser)
{
    if (!parser) return NULL;

    // 创建 -v/--version 选项
    XStringList* names = XStringList_create();
    if (!names) return NULL;
    XString* v = XString_create_utf8("v");
    XString* version = XString_create_utf8("version");
    if (v) XStringList_push_back_utf8(names, v);
    if (version) XStringList_push_back_utf8(names, version);

    XCommandLineOption* opt = XCommandLineOption_createFullWithNames(names,
        "显示版本信息。", NULL, NULL);
    XStringList_delete_base(names);

    if (!opt) return NULL;

    if (!XCommandLineParser_addOption(parser, opt)) {
        XCommandLineOption_delete(opt);
        return NULL;
    }

    parser->m_builtinVersionOption = true;
    return opt;
}

XCommandLineOption* XCommandLineParser_addHelpOption(XCommandLineParser* parser)
{
    if (!parser) return NULL;

    // 创建 -h/--help 选项
    XStringList* names = XStringList_create();
    if (!names) return NULL;
    XString* h = XString_create_utf8("h");
    XString* help = XString_create_utf8("help");
    if (h) XStringList_push_back_utf8(names, h);
    if (help) XStringList_push_back_utf8(names, help);

    XCommandLineOption* opt = XCommandLineOption_createFullWithNames(names,
        "显示命令行选项的帮助信息。", NULL, NULL);
    XStringList_delete_base(names);

    if (!opt) return NULL;

    if (!XCommandLineParser_addOption(parser, opt)) {
        XCommandLineOption_delete(opt);
        return NULL;
    }

    parser->m_builtinHelpOption = true;
    return opt;
}

void XCommandLineParser_setApplicationDescription(XCommandLineParser* parser, const char* description)
{
    if (!parser) return;
    XString_delete_base(parser->m_description);
    parser->m_description = description ? XString_create_utf8(description) : XString_create();
}

const char* XCommandLineParser_applicationDescription(const XCommandLineParser* parser)
{
    return (parser && parser->m_description) ? XString_toUtf8(parser->m_description) : "";
}

void XCommandLineParser_addPositionalArgument(XCommandLineParser* parser,
    const char* name, const char* description, const char* syntax)
{
    if (!parser) return;
    XPositionalArgumentDefinition def;
    def.name = name ? XString_create_utf8(name) : XString_create();
    def.description = description ? XString_create_utf8(description) : XString_create();
    def.syntax = (syntax && strlen(syntax) > 0) ? XString_create_utf8(syntax) : (name ? XString_create_utf8(name) : XString_create());
    XVector_push_back_1_base(parser->m_positionalArgumentDefinitions, &def);
}

void XCommandLineParser_clearPositionalArguments(XCommandLineParser* parser)
{
    if (!parser) return;
    for (size_t i = 0; i < XVector_size_base(parser->m_positionalArgumentDefinitions); ++i) {
        XPositionalArgumentDefinition* def = (XPositionalArgumentDefinition*)XVector_at_base(parser->m_positionalArgumentDefinitions, i);
        XString_delete_base(def->name);
        XString_delete_base(def->description);
        XString_delete_base(def->syntax);
    }
    XVector_clear_base(parser->m_positionalArgumentDefinitions);
}

/* ==================== 内部：解析逻辑 ==================== */

static bool isOption(const char* arg)
{
    return arg && arg[0] == '-' && arg[1] != '\0';
}

static bool isEndOfOptions(const char* arg)
{
    return arg && arg[0] == '-' && arg[1] == '-' && arg[2] == '\0';
}

static bool registerFoundOption(XCommandLineParser* parser, const char* optionName)
{
    if (!parser || !optionName) return false;

    // 添加到已找到的选项名称列表
    XStringList_push_back_utf8(parser->m_optionNames, optionName);

    return true;
}

static bool parseOptionValue(XCommandLineParser* parser, const char* optionName,
    const char* argument, int* argIdx, int argc, char** argv)
{
    if (!parser || !optionName) return false;

    int optIdx = findOptionIndex(parser, optionName);
    if (optIdx < 0) return false; // 未知选项

    XCommandLineOption* opt;
    XCommandLineOption** optPtr = (XCommandLineOption**)XVector_at_base(parser->m_commandLineOptionList, optIdx);
    if (!optPtr) return false;
    opt = *optPtr;

    // 检查是否需要值
    if (XCommandLineOption_requiresValue(opt)) {
        const char* value = NULL;

        // 检查是否在 --key=value 格式中已提供值
        if (argument && strlen(argument) > 0) {
            value = argument;
        } else if (*argIdx + 1 < argc) {
            // 从下一个参数获取值
            (*argIdx)++;
            value = argv[*argIdx];
        } else {
            // 缺少值
            XString_delete_base(parser->m_errorText);
            parser->m_errorText = XString_create_fmt_utf8("选项 '%s' 缺少必需的值。", optionName);
            return false;
        }

        // 存储值
        int key = optIdx;
        XStringList** existing = (XStringList**)XHashMap_value_base(parser->m_optionValuesHash, &key);
        XStringList* values;
        if (existing) {
            values = *existing;
        } else {
            values = XStringList_create();
            XMapBase_insert_base(parser->m_optionValuesHash, &key, &values);
        }
        XStringList_push_back_utf8(values, value);
    }

    return true;
}

bool XCommandLineParser_parse(XCommandLineParser* parser, const XStringList* args)
{
    if (!parser || !args) return false;

    // 清空之前的结果
    XStringList_clear_base(parser->m_optionNames);
    XStringList_clear_base(parser->m_positionalArgumentList);
    XStringList_clear_base(parser->m_unknownOptionNames);
    XString_delete_base(parser->m_errorText);
    parser->m_errorText = XString_create();
    // 清空选项值哈希表
    // TODO: free old values
    XHashMap_clear_base(parser->m_optionValuesHash);

    // 转换 args 为 argc/argv 格式便于处理
    int argc = (int)XStringList_size_base(args);
    char** argv = NULL;
    if (argc > 0) {
        argv = XMalloc_System(sizeof(char*) * argc);
        if (!argv) return false;
        for (int i = 0; i < argc; ++i) {
            const XString* s = XStringList_at_base(args, i);
            argv[i] = s ? (char*)XString_toUtf8(s) : (char*)"";
        }
    }

    bool endOfOptions = false;
    bool result = true;

    for (int i = 1; i < argc; ++i) { // 从 1 开始跳过程序名
        const char* arg = argv[i];

        if (endOfOptions) {
            // -- 之后全部视为位置参数
            XStringList_push_back_utf8(parser->m_positionalArgumentList, arg);
            continue;
        }

        if (isEndOfOptions(arg)) {
            endOfOptions = true;
            continue;
        }

        if (!isOption(arg)) {
            // 位置参数
            if (parser->m_optionsAfterPositionalArgumentsMode == XCOMMANDLINE_PARSER_PARSE_AS_POSITIONAL_ARGUMENTS) {
                endOfOptions = true;
            }
            XStringList_push_back_utf8(parser->m_positionalArgumentList, arg);
            continue;
        }

        // 解析选项
        const char* optArg = arg + 1; // 跳过 '-'
        bool isLong = (optArg[0] == '-');
        if (isLong) optArg++; // 跳过第二个 '-'

        // 处理 --key=value 格式
        char* eqPos = strchr(optArg, '=');
        const char* optionName = optArg;
        const char* optionValue = NULL;
        char* optCopy = NULL;

        if (eqPos) {
            optCopy = xStrDup(optArg);
            if (optCopy) {
                optCopy[eqPos - optArg] = '\0';
                optionName = optCopy;
                optionValue = eqPos + 1;
            }
        }

        if (isLong) {
            // 长选项
            if (findOptionIndex(parser, optionName) >= 0) {
                registerFoundOption(parser, optionName);
                if (!parseOptionValue(parser, optionName, optionValue, &i, argc, argv)) {
                    result = false;
                    goto parse_cleanup;
                }
            } else {
                // 未知选项
                XStringList_push_back_utf8(parser->m_unknownOptionNames, optionName);
                XString_delete_base(parser->m_errorText);
                parser->m_errorText = XString_create_fmt_utf8("未知选项: %s", optionName);
                result = false;
                goto parse_cleanup;
            }
        } else {
            // 短选项
            if (parser->m_singleDashWordOptionMode == XCOMMANDLINE_PARSER_PARSE_AS_LONG_OPTIONS) {
                // 将整个 -abc 视为长选项 abc
                if (findOptionIndex(parser, optionName) >= 0) {
                    registerFoundOption(parser, optionName);
                    if (!parseOptionValue(parser, optionName, optionValue, &i, argc, argv)) {
                        result = false;
                        goto parse_cleanup;
                    }
                } else {
                    XStringList_push_back_utf8(parser->m_unknownOptionNames, optionName);
                    XString_delete_base(parser->m_errorText);
                    parser->m_errorText = XString_create_fmt_utf8("未知选项: %s", optionName);
                    result = false;
                    goto parse_cleanup;
                }
            } else {
                // ParseAsCompactedShortOptions: 将 -abc 解析为 -a -b -c
                size_t optLen = strlen(optionName);
                for (size_t j = 0; j < optLen; ++j) {
                    char singleName[2] = { optionName[j], '\0' };
                    if (findOptionIndex(parser, singleName) >= 0) {
                        registerFoundOption(parser, singleName);
                        // 如果是最后一个字符且可能有值，尝试从下一个参数获取
                        const char* val = (j == optLen - 1) ? optionValue : NULL;
                        if (!parseOptionValue(parser, singleName, val, &i, argc, argv)) {
                            result = false;
                            goto parse_cleanup;
                        }
                    } else {
                        char unknownName[2] = { optionName[j], '\0' };
                        XStringList_push_back_utf8(parser->m_unknownOptionNames, unknownName);
                        XString_delete_base(parser->m_errorText);
                        parser->m_errorText = XString_create_fmt_utf8("未知选项: -%c", optionName[j]);
                        result = false;
                        goto parse_cleanup;
                    }
                }
            }
        }

parse_cleanup:
        XFree_System(optCopy);
        if (!result) break;
    }

    XFree_System(argv);
    parser->m_needsParsing = false;
    return result;
}

void XCommandLineParser_process(XCommandLineParser* parser, const XStringList* args)
{
    if (!parser || !args) return;

    if (!XCommandLineParser_parse(parser, args)) {
        // 解析失败，显示错误并退出
        const char* err = XCommandLineParser_errorText(parser);
        fprintf(stderr, "%s\n", err);
        fprintf(stderr, "使用 --help 查看帮助信息。\n");
        exit(1);
    }

    // 检查 --help
    if (parser->m_builtinHelpOption) {
        if (XCommandLineParser_isSet(parser, "help") || XCommandLineParser_isSet(parser, "h")
            || XCommandLineParser_isSet(parser, "help-all")) {
            XString* help = XCommandLineParser_helpText(parser);
            if (help) {
                printf("%s\n", XString_toUtf8(help));
                XString_delete_base(help);
            }
            exit(0);
        }
    }

    // 检查 --version
    if (parser->m_builtinVersionOption) {
        if (XCommandLineParser_isSet(parser, "version") || XCommandLineParser_isSet(parser, "v")) {
            XCommandLineParser_showVersion(parser);
            exit(0);
        }
    }
}

void XCommandLineParser_processApplication(XCommandLineParser* parser, const XCoreApplication* app)
{
    (void)app;
    /* Qt 6.8: process(const QCoreApplication &app) 使用 app.arguments() */
    if (!parser) return;
    XStringList* args = XCoreApplication_arguments();
    if (args) {
        XCommandLineParser_process(parser, args);
    }
}


const char* XCommandLineParser_errorText(const XCommandLineParser* parser)
{
    return (parser && parser->m_errorText) ? XString_toUtf8(parser->m_errorText) : "";
}

/* ==================== 查询 ==================== */

bool XCommandLineParser_isSet(XCommandLineParser* parser, const char* name)
{
    if (!parser || !name) return false;

    // 检查是否在已找到的选项名称中
    for (size_t i = 0; i < XStringList_size_base(parser->m_optionNames); ++i) {
        const XString* s = XStringList_at_base(parser->m_optionNames, i);
        if (s && strcmp(XString_toUtf8(s), name) == 0)
            return true;
    }
    return false;
}

bool XCommandLineParser_isSetOption(XCommandLineParser* parser, const XCommandLineOption* option)
{
    if (!parser || !option) return false;
    const XStringList* names = XCommandLineOption_names(option);
    if (!names) return false;
    for (size_t i = 0; i < XStringList_size_base(names); ++i) {
        const XString* s = XStringList_at_base(names, i);
        if (s && XCommandLineParser_isSet(parser, XString_toUtf8(s)))
            return true;
    }
    return false;
}

const char* XCommandLineParser_value(XCommandLineParser* parser, const char* name)
{
    if (!parser || !name) return NULL;

    int idx = findOptionIndex(parser, name);
    if (idx < 0) return NULL;

    // 检查是否有值
    int key = idx;
    XStringList** values = (XStringList**)XHashMap_value_base(parser->m_optionValuesHash, &key);
    if (values && *values && XStringList_size_base(*values) > 0) {
        const XString* s = XStringList_at_base(*values, 0);
        if (s) return XString_toUtf8(s);
    }

    // 没有设置值，检查默认值
    XCommandLineOption* opt = findOption(parser, name);
    if (opt) {
        const XStringList* dvals = XCommandLineOption_defaultValues(opt);
        if (dvals && XStringList_size_base(dvals) > 0) {
            const XString* s = XStringList_at_base(dvals, 0);
            if (s) return XString_toUtf8(s);
        }
    }

    return NULL;
}

const char* XCommandLineParser_valueOption(XCommandLineParser* parser, const XCommandLineOption* option)
{
    if (!parser || !option) return NULL;
    const XStringList* names = XCommandLineOption_names(option);
    if (!names || XStringList_size_base(names) == 0) return NULL;
    const XString* s = XStringList_at_base(names, 0);
    if (!s) return NULL;
    return XCommandLineParser_value(parser, XString_toUtf8(s));
}

const XStringList* XCommandLineParser_values(XCommandLineParser* parser, const char* name)
{
    if (!parser || !name) return NULL;

    int idx = findOptionIndex(parser, name);
    if (idx < 0) return NULL;

    int key = idx;
    XStringList** values = (XStringList**)XHashMap_value_base(parser->m_optionValuesHash, &key);
    if (values) return *values;

    // 返回默认值
    XCommandLineOption* opt = findOption(parser, name);
    if (opt) {
        return XCommandLineOption_defaultValues(opt);
    }
    return NULL;
}

const XStringList* XCommandLineParser_valuesOption(XCommandLineParser* parser, const XCommandLineOption* option)
{
    if (!parser || !option) return NULL;
    const XStringList* names = XCommandLineOption_names(option);
    if (!names || XStringList_size_base(names) == 0) return NULL;
    const XString* s = XStringList_at_base(names, 0);
    if (!s) return NULL;
    return XCommandLineParser_values(parser, XString_toUtf8(s));
}

const XStringList* XCommandLineParser_positionalArguments(const XCommandLineParser* parser)
{
    return parser ? parser->m_positionalArgumentList : NULL;
}

const XStringList* XCommandLineParser_optionNames(const XCommandLineParser* parser)
{
    return parser ? parser->m_optionNames : NULL;
}

const XStringList* XCommandLineParser_unknownOptionNames(const XCommandLineParser* parser)
{
    return parser ? parser->m_unknownOptionNames : NULL;
}

/* ==================== 显示 ==================== */

void XCommandLineParser_showVersion(XCommandLineParser* parser)
{
    if (!parser) return;
    // 从 QCoreApplication 获取版本号
    // 使用全局应用实例
    
    const char* version = "";
    XCoreApplication* app = XCoreApplication_instance();
    if (app && app->m_version) {
        version = XString_toUtf8(app->m_version);
    }
    printf("%s %s\n", parser->m_description ? XString_toUtf8(parser->m_description) : "应用程序", version);
    fflush(stdout);
    exit(0);
}

void XCommandLineParser_showHelp(XCommandLineParser* parser, int exitCode)
{
    if (!parser) return;
    XString* help = XCommandLineParser_helpText(parser);
    if (help) {
        printf("%s\n", XString_toUtf8(help));
        XString_delete_base(help);
    }
    fflush(stdout);
    exit(exitCode);
}

XString* XCommandLineParser_helpText(const XCommandLineParser* parser)
{
    if (!parser) return NULL;

    XString* text = XString_create();
    char buf[256];

    // 用法行
    
    const char* appName = "应用程序";
    XCoreApplication* app = XCoreApplication_instance();
    if (app && app->m_argv && app->m_argv[0]) {
        appName = app->m_argv[0];
    }

    XString_append_utf8(text, "用法: ");
    XString_append_utf8(text, appName);

    // 添加位置参数语法到用法行
    bool hasPositionalDefs = (parser->m_positionalArgumentDefinitions &&
        XVector_size_base(parser->m_positionalArgumentDefinitions) > 0);
    if (hasPositionalDefs) {
        for (size_t i = 0; i < XVector_size_base(parser->m_positionalArgumentDefinitions); ++i) {
            XPositionalArgumentDefinition* def = (XPositionalArgumentDefinition*)XVector_at_base(parser->m_positionalArgumentDefinitions, i);
            if (def->syntax) {
                XString_append_utf8(text, " ");
                XString_append(text, def->syntax);
            }
        }
    } else {
        XString_append_utf8(text, " [选项]");
    }

    XString_append_utf8(text, "\n\n");

    // 描述
    if (parser->m_description && XString_length_base(parser->m_description) > 0) {
        XString_append(text, parser->m_description);
        XString_append_utf8(text, "\n\n");
    }

    // 选项列表
    if (parser->m_commandLineOptionList && XVector_size_base(parser->m_commandLineOptionList) > 0) {
        XString_append_utf8(text, "选项:\n");
        for (size_t i = 0; i < XVector_size_base(parser->m_commandLineOptionList); ++i) {
            XCommandLineOption** optPtr = (XCommandLineOption**)XVector_at_base(parser->m_commandLineOptionList, i);
            if (!optPtr || !*optPtr) continue;
            XCommandLineOption* opt = *optPtr;

            // 跳过隐藏选项
            if (XCommandLineOption_isHidden(opt)) continue;

            // 生成选项行
            XString* line = XString_create_utf8("  ");
            const XStringList* names = XCommandLineOption_names(opt);
            bool first = true;
            if (names) {
                for (size_t j = 0; j < XStringList_size_base(names); ++j) {
                    const XString* ns = XStringList_at_base(names, j);
                    if (!ns) continue;
                    const char* n = XString_toUtf8(ns);
                    if (!n) continue;
                    if (!first) XString_append_utf8(line, ", ");
                    if (strlen(n) == 1) {
                        snprintf(buf, sizeof(buf), "-%s", n); XString_append_utf8(line, buf);
                    } else {
                        snprintf(buf, sizeof(buf), "--%s", n); XString_append_utf8(line, buf);
                    }
                    first = false;
                }
            }

            // 值名称
            const char* vn = XCommandLineOption_valueName(opt);
            if (vn && strlen(vn) > 0) {
                snprintf(buf, sizeof(buf), " <%s>", vn); XString_append_utf8(line, buf);
            }

            // 对齐
            while (XString_length_base(line) < 30) XString_append_utf8(line, " ");

            // 描述
            const char* desc = XCommandLineOption_description(opt);
            if (desc) XString_append_utf8(line, desc);

            // 默认值
            const XStringList* dvals = XCommandLineOption_defaultValues(opt);
            if (dvals && XStringList_size_base(dvals) > 0) {
                const XString* ds = XStringList_at_base(dvals, 0);
                if (ds && XString_length_base(ds) > 0) {
                    const char* dsStr = XString_toUtf8(ds);
                    if (dsStr) {
                        snprintf(buf, sizeof(buf), " (默认: %s)", dsStr); XString_append_utf8(line, buf);
                    }
                }
            }

            XString_append_utf8(line, "\n");
            XString_append(text, line);
            XString_delete_base(line);
        }
    }

    // 位置参数说明
    if (hasPositionalDefs) {
        XString_append_utf8(text, "\n参数:\n");
        for (size_t i = 0; i < XVector_size_base(parser->m_positionalArgumentDefinitions); ++i) {
            XPositionalArgumentDefinition* def = (XPositionalArgumentDefinition*)XVector_at_base(parser->m_positionalArgumentDefinitions, i);
            XString* line = XString_create_utf8("  ");
            if (def->name) XString_append(line, def->name);
            while (XString_length_base(line) < 30) XString_append_utf8(line, " ");
            if (def->description) XString_append(line, def->description);
            XString_append_utf8(line, "\n");
            XString_append(text, line);
            XString_delete_base(line);
        }
    }

    return text;
}

