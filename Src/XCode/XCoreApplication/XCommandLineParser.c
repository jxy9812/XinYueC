#include "XCommandLineParser.h"
#include "XVector.h"
#include "XHashMap.h"
#include "XString.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>

/**
 * @brief 安全的字符串复制函数
 * @param str 要复制的字符串
 * @return 新分配的字符串副本，内存分配失败返回NULL
 */
static char* XStrDup(const char* str) {
    if (!str) return NULL;
    size_t len = strlen(str) + 1;
    char* copy = XMemory_malloc(len);
    if (copy) {
        memcpy(copy, str, len);
    }
    return copy;
}

/**
 * @brief 判断参数是否为选项（以'-'开头）
 * @param arg 命令行参数
 * @return 是选项返回true，否则返回false
 */
static bool isOption(const char* arg) {
    return arg != NULL && arg[0] == '-';
}

/**
 * @brief 从选项参数中提取选项名
 * @param arg 命令行参数（如"-h"或"--help"）
 * @param isLong 输出参数，标识是否为长选项
 * @return 选项名，提取失败返回NULL
 */
static const char* getOptionName(const char* arg, bool* isLong) {
    if (arg == NULL || arg[0] != '-') return NULL;

    if (arg[1] == '-') {
        *isLong = true;
        return arg + 2; // --long -> long
    }
    else {
        *isLong = false;
        return arg + 1; // -s -> s
    }
}

/**
 * @brief 分割选项名和值（处理--key=value格式）
 * @param name 选项名（来自getOptionName的返回值）
 * @param key 输出参数，指向选项名
 * @param value 输出参数，指向选项值
 * @param allocatedStrings 用于跟踪分配的内存
 */
static void splitOptionAndValue(const char* name, char** key, char** value, XVector* allocatedStrings) {
    // 复制原始字符串，避免修改argv中的常量
    char* copy = XStrDup(name);
    if (!copy) {
        *key = NULL;
        *value = NULL;
        return;
    }

    // 保存分配的字符串，以便后续释放
    XVector_push_back_base(allocatedStrings, &copy);

    *key = copy;
    *value = strchr(copy, '=');
    if (*value) {
        *(*value) = '\0'; // 分割复制的字符串，不影响原始argv
        (*value)++;
    }
    else {
        *value = ""; // 无值选项
    }
}

/**
 * @brief 检查互斥组冲突
 * @param parser 命令行解析器实例
 */
static void checkExclusiveGroups(XCommandLineParser* parser) {
    if (!parser || !parser->groups) return;

    for (size_t g = 0; g < XVector_size_base(parser->groups); g++) {
        XCommandLineOptionGroup* group = *(XCommandLineOptionGroup**)XVector_at_base(parser->groups, g);
        if (!group->isExclusive) continue;

        int foundCount = 0;
        const char* lastFound = NULL;

        // 检查组内每个选项是否被使用
        for (size_t o = 0; o < XVector_size_base(group->options); o++) {
            XCommandLineOption* opt = *(XCommandLineOption**)XVector_at_base(group->options, o);
            const char* optName = opt->longName ? opt->longName : opt->shortName;
            if (!optName) continue;

            if (XCommandLineParser_hasOption(parser, optName)) {
                foundCount++;
                lastFound = optName;
            }
        }

        // 互斥组中出现多个选项
        if (foundCount > 1) {
            XVector_push_back_base(parser->result->exclusiveGroupConflicts, &lastFound);
        }
    }
}

XCommandLineParser* XCommandLineParser_create() {
    XCommandLineParser* parser = XMemory_malloc(sizeof(XCommandLineParser));
    if (!parser) return NULL;

    // 初始化选项和组向量
    parser->options = XVector_create(sizeof(XCommandLineOption));
    parser->groups = XVector_create(sizeof(XCommandLineOptionGroup*));
    parser->result = XMemory_malloc(sizeof(XCommandLineParseResult));

    // 检查内存分配
    if (!parser->options || !parser->groups || !parser->result) {
        XVector_delete_base(parser->options);
        XVector_delete_base(parser->groups);
        XMemory_free(parser->result);
        XMemory_free(parser);
        return NULL;
    }

    // 初始化解析结果
    parser->result->positionalArgs = XVector_create(sizeof(char*));
    parser->result->optionMap = XHashMap_Create(char*, char*, uintptr_t_compare);
    parser->result->optionCounts = XHashMap_Create(char*, int, uintptr_t_compare);
    parser->result->unrecognizedOpts = XVector_create(sizeof(char*));
    parser->result->exclusiveGroupConflicts = XVector_create(sizeof(const char*));
    parser->result->allocatedStrings = XVector_create(sizeof(char*)); // 初始化内存跟踪向量

    // 检查结果向量分配
    if (!parser->result->positionalArgs || !parser->result->optionMap ||
        !parser->result->optionCounts || !parser->result->unrecognizedOpts ||
        !parser->result->exclusiveGroupConflicts || !parser->result->allocatedStrings) {
        // 释放已分配的资源
        XVector_delete_base(parser->result->positionalArgs);
        XHashMap_delete_base(parser->result->optionMap);
        XHashMap_delete_base(parser->result->optionCounts);
        XVector_delete_base(parser->result->unrecognizedOpts);
        XVector_delete_base(parser->result->exclusiveGroupConflicts);
        XVector_delete_base(parser->result->allocatedStrings);
        XMemory_free(parser->result);
        XVector_delete_base(parser->options);
        XVector_delete_base(parser->groups);
        XMemory_free(parser);
        return NULL;
    }

    parser->programName = "";
    parser->applicationDescription = "";

    // 注册默认选项 --help/-h 和 --version/-v
    XCommandLineParser_addOption(parser, "h", "help", "显示帮助信息", false, false, "");
    XCommandLineParser_addOption(parser, "v", "version", "显示版本信息", false, false, "");

    return parser;
}

void XCommandLineParser_delete(XCommandLineParser* parser) {
    if (!parser) return;

    // 释放选项向量
    XVector_delete_base(parser->options);

    // 销毁选项组
    for (size_t i = 0; i < XVector_size_base(parser->groups); i++) {
        XCommandLineOptionGroup* group = *(XCommandLineOptionGroup**)XVector_at_base(parser->groups, i);
        XCommandLineOptionGroup_delete(group);
    }
    XVector_delete_base(parser->groups);

    // 释放解析结果
    if (parser->result) {
        XVector_delete_base(parser->result->positionalArgs);
        XHashMap_delete_base(parser->result->optionMap);
        XHashMap_delete_base(parser->result->optionCounts);
        XVector_delete_base(parser->result->unrecognizedOpts);
        XVector_delete_base(parser->result->exclusiveGroupConflicts);

        // 释放所有动态分配的字符串
        for (size_t i = 0; i < XVector_size_base(parser->result->allocatedStrings); i++) {
            char* str = *(char**)XVector_at_base(parser->result->allocatedStrings, i);
            XMemory_free(str);
        }
        XVector_delete_base(parser->result->allocatedStrings);

        XMemory_free(parser->result);
    }

    XMemory_free(parser);
}

void XCommandLineParser_addOption(XCommandLineParser* parser,
    const char* shortName,
    const char* longName,
    const char* description,
    bool requiresValue,
    bool isHidden,
    const char* defaultValue) {
    if (!parser || (!shortName && !longName)) return;

    XCommandLineOption opt = {
        .shortName = shortName,
        .longName = longName,
        .description = description,
        .defaultValue = defaultValue,
        .requiresValue = requiresValue,
        .isHidden = isHidden
    };
    XVector_push_back_base(parser->options, &opt);
}

void XCommandLineParser_addOptionGroup(XCommandLineParser* parser, XCommandLineOptionGroup* group) {
    if (!parser || !group) return;
    XVector_push_back_base(parser->groups, &group);
}

bool XCommandLineParser_parse(XCommandLineParser* parser, int argc, char** argv) {
    if (!parser || argc < 1 || !argv) return false;
    parser->programName = argv[0];

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        if (!isOption(arg)) {
            // 位置参数 - 不复制，因为我们只读取不修改
            XVector_push_back_base(parser->result->positionalArgs, &arg);
            continue;
        }

        bool isLong;
        const char* name = getOptionName(arg, &isLong);
        if (!name) {
            XVector_push_back_base(parser->result->unrecognizedOpts, &arg);
            continue;
        }

        // 分割选项名和值（处理 --key=value 格式）
        char* key = NULL;
        char* value = NULL;
        splitOptionAndValue(name, &key, &value, parser->result->allocatedStrings);

        if (!key) {
            // 内存分配失败，跳过此选项
            continue;
        }

        // 检查是否为已注册选项
        bool recognized = false;
        const char* matchedName = NULL;
        XCommandLineOption* matchedOpt = NULL;

        for (size_t j = 0; j < XVector_size_base(parser->options); j++) {
            XCommandLineOption* opt = XVector_at_base(parser->options, j);
            if ((isLong && opt->longName && strcmp(opt->longName, key) == 0) ||
                (!isLong && opt->shortName && strcmp(opt->shortName, key) == 0)) {

                recognized = true;
                matchedName = isLong ? opt->longName : opt->shortName;
                matchedOpt = opt;

                // 处理需要值的选项（如 -c value 格式）
                if (opt->requiresValue && value[0] == '\0') {
                    if (i + 1 < argc && !isOption(argv[i + 1])) {
                        // 复制值，避免引用argv原始数据
                        char* valCopy = XStrDup(argv[i + 1]);
                        if (valCopy) {
                            XVector_push_back_base(parser->result->allocatedStrings, &valCopy);
                            value = valCopy;
                        }
                        i++;
                    }
                    else {
                        // 缺少必要的参数值，使用默认值
                        if (opt->defaultValue) {
                            char* defCopy = XStrDup(opt->defaultValue);
                            if (defCopy) {
                                XVector_push_back_base(parser->result->allocatedStrings, &defCopy);
                                value = defCopy;
                            }
                            else {
                                value = "";
                            }
                        }
                        else {
                            // 无默认值则视为未识别
                            XVector_push_back_base(parser->result->unrecognizedOpts, &arg);
                            recognized = false;
                        }
                    }
                }
                break;
            }
        }

        if (recognized) {
            // 存储选项值，无值选项存储空字符串
            const char* storedValue = value ? value : "";
            XHashMap_insert_base(parser->result->optionMap, &matchedName, &storedValue);

            // 更新选项计数
            int* count = XHashMap_value_base(parser->result->optionCounts, &matchedName);
            if (count) {
                (*count)++;
            }
            else {
                int initial = 1;
                XHashMap_insert_base(parser->result->optionCounts, &matchedName, &initial);
            }
        }
        else {
            XVector_push_back_base(parser->result->unrecognizedOpts, &arg);
        }
    }

    // 检查互斥组冲突
    checkExclusiveGroups(parser);

    return true;
}

int XCommandLineParser_optionCount(XCommandLineParser* parser, const char* option) {
    if (!parser || !option || !parser->result) return 0;
    int* count = XHashMap_value_base(parser->result->optionCounts, &option);
    return count ? *count : 0;
}

XVector* XCommandLineParser_exclusiveGroupConflicts(XCommandLineParser* parser) {
    return parser && parser->result ? parser->result->exclusiveGroupConflicts : NULL;
}

void XCommandLineParser_setApplicationDescription(XCommandLineParser* parser, const char* description) {
    if (parser) {
        parser->applicationDescription = description ? description : "";
    }
}

XString* XCommandLineParser_helpText(XCommandLineParser* parser, const char* description) {
    if (!parser) return NULL;

    XString* text = XString_create("");
    const char* appDesc = parser->applicationDescription ? parser->applicationDescription : description;
    XString_create_fmt_utf8(text, "%s\n", appDesc ? appDesc : "命令行工具");
    XString_create_fmt_utf8(text, "用法: %s [选项] [位置参数]\n\n", parser->programName);

    // 先显示未分组选项
    if (XVector_size_base(parser->options) > 0) {
        XString_append(text, "选项:\n");
        for (size_t i = 0; i < XVector_size_base(parser->options); i++) {
            XCommandLineOption* opt = XVector_at_base(parser->options, i);
            if (opt->isHidden) continue; // 跳过隐藏选项

            // 检查是否已在组中
            bool inGroup = false;
            for (size_t g = 0; g < XVector_size_base(parser->groups); g++) {
                XCommandLineOptionGroup* group = *(XCommandLineOptionGroup**)XVector_at_base(parser->groups, g);
                for (size_t o = 0; o < XVector_size_base(group->options); o++) {
                    XCommandLineOption* groupOpt = *(XCommandLineOption**)XVector_at_base(group->options, o);
                    if (groupOpt == opt) {
                        inGroup = true;
                        break;
                    }
                }
                if (inGroup) break;
            }
            if (inGroup) continue;

            // 生成选项行
            XString* line = XString_create("  ");
            if (opt->shortName) {
                XString_create_fmt_utf8(line, "-%s", opt->shortName);
                if (opt->longName) XString_append(line, ", ");
            }
            if (opt->longName) {
                XString_create_fmt_utf8(line, "--%s", opt->longName);
            }
            if (opt->requiresValue) {
                XString_append(line, " <值>");
                if (opt->defaultValue && strlen(opt->defaultValue) > 0) {
                    XString_create_fmt_utf8(line, " (默认: %s)", opt->defaultValue);
                }
            }
            // 对齐描述
            while (XString_length_base(line) < 24) XString_append(line, " ");
            XString_create_fmt_utf8(line, "%s\n", opt->description);
            XString_append(text, line);
            XString_delete_base(line);
        }
    }

    // 显示选项组
    for (size_t g = 0; g < XVector_size_base(parser->groups); g++) {
        XCommandLineOptionGroup* group = *(XCommandLineOptionGroup**)XVector_at_base(parser->groups, g);
        if (XVector_size_base(group->options) == 0) continue;

        XString_create_fmt_utf8(text, "\n%s:\n", group->description ? group->description : "选项组");
        for (size_t o = 0; o < XVector_size_base(group->options); o++) {
            XCommandLineOption* opt = *(XCommandLineOption**)XVector_at_base(group->options, o);
            if (opt->isHidden) continue;

            XString* line = XString_create("  ");
            if (opt->shortName) {
                XString_create_fmt_utf8(line, "-%s", opt->shortName);
                if (opt->longName) XString_append(line, ", ");
            }
            if (opt->longName) {
                XString_create_fmt_utf8(line, "--%s", opt->longName);
            }
            if (opt->requiresValue) {
                XString_append(line, " <值>");
                if (opt->defaultValue && strlen(opt->defaultValue) > 0) {
                    XString_create_fmt_utf8(line, " (默认: %s)", opt->defaultValue);
                }
            }
            // 对齐描述
            while (XString_length_base(line) < 24) XString_append(line, " ");
            XString_create_fmt_utf8(line, "%s\n", opt->description);
            XString_append(text, line);
            XString_delete_base(line);
        }
    }

    return text;
}

bool XCommandLineParser_hasOption(XCommandLineParser* parser, const char* option) {
    if (!parser || !option || !parser->result) return false;
    return XMapBase_contains(parser->result->optionMap, &option);
}

const char* XCommandLineParser_getOptionValue(XCommandLineParser* parser, const char* option) {
    if (!parser || !option || !parser->result) return NULL;

    const char* value = XHashMap_value_base(parser->result->optionMap, &option);
    // 如果未找到且有默认值，返回默认值
    if (!value) {
        for (size_t i = 0; i < XVector_size_base(parser->options); i++) {
            XCommandLineOption* opt = XVector_at_base(parser->options, i);
            if ((opt->longName && strcmp(opt->longName, option) == 0) ||
                (opt->shortName && strcmp(opt->shortName, option) == 0)) {
                return opt->defaultValue;
            }
        }
    }
    return value;
}

XVector* XCommandLineParser_positionalArguments(XCommandLineParser* parser) {
    return parser && parser->result ? parser->result->positionalArgs : NULL;
}

XVector* XCommandLineParser_unrecognizedOptions(XCommandLineParser* parser) {
    return parser && parser->result ? parser->result->unrecognizedOpts : NULL;
}

XString* XCommandLineParser_versionText(XCommandLineParser* parser, const char* version) {
    if (!parser) return NULL;
    return XString_create_fmt_utf8("%s 版本 %s", parser->programName, version ? version : "1.0.0");
}
