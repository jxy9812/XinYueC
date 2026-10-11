#include "XCommandLineParser.h"
#include "XVector.h"
#include "XHashMap.h"
#include "XString.h"
#include "XStringList.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XPrintf.h"
#include "XCoreApplication.h"
#include "XCryptographic.h"
#include <stdlib.h>
#include <stdio.h>

/* ==================== 内部辅助函数 ==================== */

/* 哈希表键/值释放辅助函数 */
static void free_name_hash_key(void* data)
{
    /* m_nameHash 的键类型为 XString*，pair 中存储的是 XString* 指针值 */
    XString** p = (XString**)data;
    if (p && *p) XClassDelete(*p);
}

static void free_option_values_value(void* data)
{
    /* m_optionValuesHash 的值类型为 XStringList*，pair 中存储的是 XStringList* 指针 */
    XStringList** p = (XStringList**)data;
    if (p && *p) XClassDelete(*p);
}

/* [死码清理] stringHash 已删除：全仓无调用点（见审计清单）。
 */

static int stringCompare(const void* a, const void* b)
{
    const XString* sa = *(const XString**)a;
    const XString* sb = *(const XString**)b;
    if (!sa && !sb) return 0;
    if (!sa) return -1;
    if (!sb) return 1;
    return (int)XString_compare(sa, sb);
}

static uint64_t xcommandline_hash_string(const void* key, size_t len)
{
    (void)len;
    const XString* str = *(const XString**)key;
    if (!str) return 0;
    /* XString_hash 按内容哈希，与 stringCompare 的 XString_compare 语义一致 */
    return XString_hash(str, sizeof(XString));
}

/* ==================== 构造/析构 ==================== */

XCommandLineParser* XCommandLineParser_create(void)
{
    XCommandLineParser* parser = XMalloc_System(sizeof(XCommandLineParser));
    if (!parser) return NULL;

    parser->m_errorText = XString_create();
    parser->m_commandLineOptionList = XVector_create(sizeof(XCommandLineOption*));
    parser->m_nameHash = XHashMap_create(
        sizeof(XString*), sizeof(int), xcommandline_hash_string, stringCompare);
    XMapBaseSetKeyDeinitMethod(parser->m_nameHash, free_name_hash_key);
    parser->m_optionValuesHash = XHashMap_create(
        sizeof(int), sizeof(XStringList*),
        XCryptographicHash_function(XCryptographicHash_XxHash64), Xint_compare);
    XContainerSetDataDeinitMethod(parser->m_optionValuesHash, free_option_values_value);
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
    XClassDelete(parser->m_errorText);
    // 释放选项列表中的选项
    if (parser->m_commandLineOptionList) {
        for (size_t i = 0; i < XVector_size_base(parser->m_commandLineOptionList); ++i) {
            XCommandLineOption** opt = (XCommandLineOption**)XVector_at_base(parser->m_commandLineOptionList, i);
            if (opt && *opt) XCommandLineOption_delete(*opt);
        }
        XClassDelete(parser->m_commandLineOptionList);
    }
    XClassDelete(parser->m_nameHash);
    // 释放选项值哈希表（deinit 方法会自动释放 XStringList* 值）
    if (parser->m_optionValuesHash) {
        XClassDelete(parser->m_optionValuesHash);
    }
    XClassDelete(parser->m_optionNames);
    XClassDelete(parser->m_positionalArgumentList);
    XClassDelete(parser->m_unknownOptionNames);
    XClassDelete(parser->m_description);
    // 释放位置参数定义
    if (parser->m_positionalArgumentDefinitions) {
        for (size_t i = 0; i < XVector_size_base(parser->m_positionalArgumentDefinitions); ++i) {
            XPositionalArgumentDefinition* def = (XPositionalArgumentDefinition*)XVector_at_base(parser->m_positionalArgumentDefinitions, i);
            XClassDelete(def->name);
            XClassDelete(def->description);
            XClassDelete(def->syntax);
        }
        XClassDelete(parser->m_positionalArgumentDefinitions);
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

static int findOptionIndex(XCommandLineParser* parser, const XString* name)
{
    int* idx;
    if (!parser || !name) return -1;
    /* m_nameHash 的键是 XString*；按键内容哈希/比较（见 stringCompare/
       xcommandline_hash_string），name 只在调用期间借用、不取得所有权。 */
    idx = (int*)XHashMap_value_base(parser->m_nameHash, &name);
    return idx ? *idx : -1;
}

/* UTF-8 兼容入口：argv/char* 流动的解析路径先转临时 XString 再转发到主版本 */
static int findOptionIndex_utf8(XCommandLineParser* parser, const char* name)
{
    int idx;
    XString key;
    if (!parser || !name) return -1;
    XString_init(&key);
    if (!XString_assign_utf8(&key, name)) {
        XClassDeinit(&key);
        return -1;
    }
    idx = findOptionIndex(parser, &key);
    XClassDeinit(&key);
    return idx;
}

static XCommandLineOption* findOption(XCommandLineParser* parser, const XString* name)
{
    int idx = findOptionIndex(parser, name);
    if (idx < 0) return NULL;
    XCommandLineOption** opt = (XCommandLineOption**)XVector_at_base(parser->m_commandLineOptionList, idx);
    return opt ? *opt : NULL;
}

static XCommandLineOption* findOption_utf8(XCommandLineParser* parser, const char* name)
{
    int idx = findOptionIndex_utf8(parser, name);
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
        if (s && findOptionIndex(parser, s) >= 0)
            return false; // 名称已存在
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
                    XClassDelete(copy_s);
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
                        XClassDelete(copy_dv);
                    }
                }
        }
    }

    copy->flags = option->flags;

    // 添加到选项列表
    size_t idx = XVector_size_base(parser->m_commandLineOptionList);
    if (!XVector_push_back_1_base(parser->m_commandLineOptionList, &copy)) {
        /* 入列失败时 copy 未被容器接管，其深拷贝的 names/description/
           valueName/defaultValues 和结构体本身必须在此释放；同时不能
           继续执行下方的哈希注册，避免注册越界索引 idx。 */
        XCommandLineOption_delete(copy);
        return false;
    }

    // 注册所有名称到哈希表
    for (size_t i = 0; i < XStringList_size_base(copy->names); ++i) {
        const XString* s = XStringList_at_base(copy->names, i);
        if (s) {
            /* 键以 XString* 长期保存（约束：私有状态禁止以 char* 长期保存）。
               插入成功后键由哈希表接管，随条目经 free_name_hash_key 释放；
               插入失败时 VXMap_insert 各失败出口一律不消费键（失败属主
               归一，见 XHashMap.c），由调用方在此释放，避免 OOM 泄漏。 */
            XString* key = XString_create_copy(s);
            if (key) {
                if (!XMapBase_insert_base(parser->m_nameHash, &key, &idx))
                    XClassDelete(key);
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
    XStringList_push_back_utf8(names, "v");
    XStringList_push_back_utf8(names, "version");

    XCommandLineOption* opt = XCommandLineOption_createFullWithNames_2(names,
        "显示版本信息。", NULL, NULL);
    XClassDelete(names);

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
    XStringList_push_back_utf8(names, "h");
    XStringList_push_back_utf8(names, "help");

    XCommandLineOption* opt = XCommandLineOption_createFullWithNames_2(names,
        "显示命令行选项的帮助信息。", NULL, NULL);
    XClassDelete(names);

    if (!opt) return NULL;

    if (!XCommandLineParser_addOption(parser, opt)) {
        XCommandLineOption_delete(opt);
        return NULL;
    }

    parser->m_builtinHelpOption = true;
    /* addOption 内部已复制，释放原始对象 */
    XCommandLineOption_delete(opt);
    return NULL;
}

void XCommandLineParser_setApplicationDescription(XCommandLineParser* parser, const XString* description)
{
    if (!parser) return;
    XClassDelete(parser->m_description);
    /* 主版本负责拷贝与生命周期：description 为 NULL 时保留空描述 */
    parser->m_description = description ? XString_create_copy(description) : XString_create();
}

void XCommandLineParser_setApplicationDescription_2(XCommandLineParser* parser, const char* description)
{
    XString* temp = description ? XString_create_utf8(description) : NULL;
    XCommandLineParser_setApplicationDescription(parser, temp);
    if (temp) XClassDelete(temp);
}

const char* XCommandLineParser_applicationDescription(const XCommandLineParser* parser)
{
    return (parser && parser->m_description) ? XString_toUtf8(parser->m_description) : "";
}

void XCommandLineParser_addPositionalArgument(XCommandLineParser* parser,
    const XString* name, const XString* description, const XString* syntax)
{
    if (!parser) return;
    XPositionalArgumentDefinition def;
    def.name = name ? XString_create_copy(name) : XString_create();
    def.description = description ? XString_create_copy(description) : XString_create();
    /* 无 syntax 时回退为参数名，与 Qt 对齐；空串视同未提供 */
    if (syntax && XString_length_base(syntax) > 0)
        def.syntax = XString_create_copy(syntax);
    else
        def.syntax = name ? XString_create_copy(name) : XString_create();
    if (!XVector_push_back_1_base(parser->m_positionalArgumentDefinitions, &def)) {
        /* 入列失败时 def 未被容器接管，按 delete/clearPositionalArguments
           的同一释放入口清理三个字符串，防止 OOM 时泄漏。 */
        XClassDelete(def.name);
        XClassDelete(def.description);
        XClassDelete(def.syntax);
        return;
    }
}

void XCommandLineParser_addPositionalArgument_2(XCommandLineParser* parser,
    const char* name, const char* description, const char* syntax)
{
    XString* nameStr = name ? XString_create_utf8(name) : NULL;
    XString* descStr = description ? XString_create_utf8(description) : NULL;
    XString* syntaxStr = syntax ? XString_create_utf8(syntax) : NULL;
    XCommandLineParser_addPositionalArgument(parser, nameStr, descStr, syntaxStr);
    if (nameStr) XClassDelete(nameStr);
    if (descStr) XClassDelete(descStr);
    if (syntaxStr) XClassDelete(syntaxStr);
}

void XCommandLineParser_clearPositionalArguments(XCommandLineParser* parser)
{
    if (!parser) return;
    for (size_t i = 0; i < XVector_size_base(parser->m_positionalArgumentDefinitions); ++i) {
        XPositionalArgumentDefinition* def = (XPositionalArgumentDefinition*)XVector_at_base(parser->m_positionalArgumentDefinitions, i);
        XClassDelete(def->name);
        XClassDelete(def->description);
        XClassDelete(def->syntax);
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

    int optIdx = findOptionIndex_utf8(parser, optionName);
    if (optIdx < 0) return false; // 未知选项

    XCommandLineOption* opt;
    XCommandLineOption** optPtr = (XCommandLineOption**)XVector_at_base(parser->m_commandLineOptionList, optIdx);
    if (!optPtr) return false;
    opt = *optPtr;

    // 检查是否需要值
    if (XCommandLineOption_requiresValue(opt)) {
        const char* value = NULL;

        // 检查是否在 --key=value 格式中已提供值
        if (argument && XStrlen(argument) > 0) {
            value = argument;
        } else if (*argIdx + 1 < argc) {
            // 从下一个参数获取值
            (*argIdx)++;
            value = argv[*argIdx];
        } else {
            // 缺少值
            XClassDelete(parser->m_errorText);
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
            /* 先以空值占位插入、成功后再回填新建列表：占位插入保证任何失败
               出口都不存在孤儿资源；VXMap_insert 失败出口一律不消费存入的
               值（失败属主归一，见 XHashMap.c），此处存入的是空指针，对值
               清理回调 free_option_values_value 也安全。 */
            values = NULL;
            if (!XMapBase_insert_base(parser->m_optionValuesHash, &key, &values))
                return false;
            existing = (XStringList**)XHashMap_value_base(parser->m_optionValuesHash, &key);
            if (!existing)
                return false;
            values = XStringList_create();
            if (!values) {
                /* 新建列表失败时先移除占位条目：残留 (key, NULL) 会让同一
                   选项的下一次解析在下方 push_back_utf8(NULL,…) 处崩溃
                   （XStringList 侧无判空）；移除后哈希不残留空值条目。 */
                XMapBase_remove_base(parser->m_optionValuesHash, &key);
                return false;
            }
            *existing = values;
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
    XClassDelete(parser->m_errorText);
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
        const char* eqPos = XStrchr(optArg, '=');
        const char* optionName = optArg;
        const char* optionValue = NULL;
        char* optCopy = NULL;

        if (eqPos) {
            optCopy = XStrdup(optArg);
            if (optCopy) {
                optCopy[eqPos - optArg] = '\0';
                optionName = optCopy;
                optionValue = eqPos + 1;
            }
        }

        if (isLong) {
            // 长选项
            if (findOptionIndex_utf8(parser, optionName) >= 0) {
                registerFoundOption(parser, optionName);
                if (!parseOptionValue(parser, optionName, optionValue, &i, argc, argv)) {
                    result = false;
                    goto parse_cleanup;
                }
            } else {
                // 未知选项
                XStringList_push_back_utf8(parser->m_unknownOptionNames, optionName);
                XClassDelete(parser->m_errorText);
                parser->m_errorText = XString_create_fmt_utf8("未知选项: %s", optionName);
                result = false;
                goto parse_cleanup;
            }
        } else {
            // 短选项
            if (parser->m_singleDashWordOptionMode == XCOMMANDLINE_PARSER_PARSE_AS_LONG_OPTIONS) {
                // 将整个 -abc 视为长选项 abc
                if (findOptionIndex_utf8(parser, optionName) >= 0) {
                    registerFoundOption(parser, optionName);
                    if (!parseOptionValue(parser, optionName, optionValue, &i, argc, argv)) {
                        result = false;
                        goto parse_cleanup;
                    }
                } else {
                    XStringList_push_back_utf8(parser->m_unknownOptionNames, optionName);
                    XClassDelete(parser->m_errorText);
                    parser->m_errorText = XString_create_fmt_utf8("未知选项: %s", optionName);
                    result = false;
                    goto parse_cleanup;
                }
            } else {
                // ParseAsCompactedShortOptions: 将 -abc 解析为 -a -b -c
                size_t optLen = XStrlen(optionName);
                for (size_t j = 0; j < optLen; ++j) {
                    char singleName[2] = { optionName[j], '\0' };
                    if (findOptionIndex_utf8(parser, singleName) >= 0) {
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
                        XClassDelete(parser->m_errorText);
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
        XERROR_PRINTF( "%s\n", err);
        XERROR_PRINTF( "使用 --help 查看帮助信息。\n");
        exit(1);
    }

    // 检查 --help
    if (parser->m_builtinHelpOption) {
        if (XCommandLineParser_isSet_2(parser, "help") || XCommandLineParser_isSet_2(parser, "h")
            || XCommandLineParser_isSet_2(parser, "help-all")) {
            XString* help = XCommandLineParser_helpText(parser);
            if (help) {
                XPrintf("%s\n", XString_toUtf8(help));
                XClassDelete(help);
            }
            exit(0);
        }
    }

    // 检查 --version
    if (parser->m_builtinVersionOption) {
        if (XCommandLineParser_isSet_2(parser, "version") || XCommandLineParser_isSet_2(parser, "v")) {
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
        /* XCoreApplication_arguments() 每次调用都新建列表且契约由调用者释放；
           process 仅按 const 借用（--help/--version/解析失败路径直接 exit，
           不经过此处）。正常返回路径必须释放，否则每次调用整体泄漏。 */
        XCommandLineParser_process(parser, args);
        XClassDelete(args);
    }
}


const char* XCommandLineParser_errorText(const XCommandLineParser* parser)
{
    return (parser && parser->m_errorText) ? XString_toUtf8(parser->m_errorText) : "";
}

/* ==================== 查询 ==================== */

bool XCommandLineParser_isSet(XCommandLineParser* parser, const XString* name)
{
    if (!parser || !name) return false;

    // 检查是否在已找到的选项名称中
    for (size_t i = 0; i < XStringList_size_base(parser->m_optionNames); ++i) {
        const XString* s = XStringList_at_base(parser->m_optionNames, i);
        if (s && XString_compare(s, name) == 0)
            return true;
    }
    return false;
}

bool XCommandLineParser_isSet_2(XCommandLineParser* parser, const char* name)
{
    XString* temp = name ? XString_create_utf8(name) : NULL;
    bool ret = XCommandLineParser_isSet(parser, temp);
    if (temp) XClassDelete(temp);
    return ret;
}

bool XCommandLineParser_isSetOption(XCommandLineParser* parser, const XCommandLineOption* option)
{
    if (!parser || !option) return false;
    const XStringList* names = XCommandLineOption_names(option);
    if (!names) return false;
    for (size_t i = 0; i < XStringList_size_base(names); ++i) {
        const XString* s = XStringList_at_base(names, i);
        if (s && XCommandLineParser_isSet(parser, s))
            return true;
    }
    return false;
}

const char* XCommandLineParser_value(XCommandLineParser* parser, const XString* name)
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

const char* XCommandLineParser_value_2(XCommandLineParser* parser, const char* name)
{
    XString* temp = name ? XString_create_utf8(name) : NULL;
    const char* ret = XCommandLineParser_value(parser, temp);
    if (temp) XClassDelete(temp);
    return ret;
}

const char* XCommandLineParser_valueOption(XCommandLineParser* parser, const XCommandLineOption* option)
{
    if (!parser || !option) return NULL;
    const XStringList* names = XCommandLineOption_names(option);
    if (!names || XStringList_size_base(names) == 0) return NULL;
    const XString* s = XStringList_at_base(names, 0);
    if (!s) return NULL;
    return XCommandLineParser_value(parser, s);
}

const XStringList* XCommandLineParser_values(XCommandLineParser* parser, const char* name)
{
    if (!parser || !name) return NULL;

    int idx = findOptionIndex_utf8(parser, name);
    if (idx < 0) return NULL;

    int key = idx;
    XStringList** values = (XStringList**)XHashMap_value_base(parser->m_optionValuesHash, &key);
    if (values) return *values;

    // 返回默认值
    XCommandLineOption* opt = findOption_utf8(parser, name);
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
    XPrintf("%s %s\n", parser->m_description ? XString_toUtf8(parser->m_description) : "应用程序", version);
    fflush(stdout);
    exit(0);
}

void XCommandLineParser_showHelp(XCommandLineParser* parser, int exitCode)
{
    if (!parser) return;
    XString* help = XCommandLineParser_helpText(parser);
    if (help) {
        XPrintf("%s\n", XString_toUtf8(help));
        XClassDelete(help);
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
                    if (XStrlen(n) == 1) {
                        snprintf(buf, sizeof(buf), "-%s", n); XString_append_utf8(line, buf);
                    } else {
                        snprintf(buf, sizeof(buf), "--%s", n); XString_append_utf8(line, buf);
                    }
                    first = false;
                }
            }

            // 值名称
            const char* vn = XCommandLineOption_valueName(opt);
            if (vn && XStrlen(vn) > 0) {
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
            XClassDelete(line);
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
            XClassDelete(line);
        }
    }

    return text;
}
