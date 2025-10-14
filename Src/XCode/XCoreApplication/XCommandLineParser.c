#include "XCommandLineParser.h"
#include "XVector.h"
#include "XHashMap.h"
#include "XString.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>

static bool isOption(const char* arg) {
    return arg != NULL && arg[0] == '-';
}

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

static void splitOptionAndValue(const char* name, char** key, char** value) {
    *key = (char*)name;
    *value = strchr(name, '=');
    if (*value) {
        *(*value) = '\0'; // 分割 key 和 value
        (*value)++;
    }
    else {
        *value = ""; // 无值选项
    }
}

XCommandLineParser* XCommandLineParser_create() {
    XCommandLineParser* parser = XMemory_malloc(sizeof(XCommandLineParser));
    if (!parser) return NULL;

    parser->options = XVector_create(sizeof(XCommandLineOption));
    parser->result = XMemory_malloc(sizeof(XCommandLineParseResult));
    if (!parser->options || !parser->result) {
        XVector_delete_base(parser->options);
        XMemory_free(parser->result);
        XMemory_free(parser);
        return NULL;
    }

    parser->result->positionalArgs = XVector_create(sizeof(char*));
    parser->result->optionMap = XHashMap_Create(char*, char*,XCompare_ptr);
    parser->result->unrecognizedOpts = XVector_create(sizeof(char*));
    parser->programName = "";

    // 注册默认选项 --help/-h 和 --version/-v
    XCommandLineParser_addOption(parser, "h", "help", "显示帮助信息", false);
    XCommandLineParser_addOption(parser, "v", "version", "显示版本信息", false);

    return parser;
}

void XCommandLineParser_delete(XCommandLineParser* parser) {
    if (!parser) return;

    XVector_delete_base(parser->options);

    if (parser->result) {
        XVector_delete_base(parser->result->positionalArgs);
        XHashMap_delete_base(parser->result->optionMap);
        XVector_delete_base(parser->result->unrecognizedOpts);
        XMemory_free(parser->result);
    }

    XMemory_free(parser);
}

void XCommandLineParser_addOption(XCommandLineParser* parser,
    const char* shortName,
    const char* longName,
    const char* description,
    bool requiresValue) {
    if (!parser || (!shortName && !longName)) return;

    XCommandLineOption opt = {
        .shortName = shortName,
        .longName = longName,
        .description = description,
        .requiresValue = requiresValue
    };
    XVector_push_back_base(parser->options, &opt);
}

bool XCommandLineParser_parse(XCommandLineParser* parser, int argc, char** argv) {
    if (!parser || argc < 1 || !argv) return false;
    parser->programName = argv[0];

    for (int i = 1; i < argc; i++) {
        char* arg = argv[i];
        if (!isOption(arg)) {
            // 位置参数
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
        splitOptionAndValue(name, &key, &value);

        // 检查是否为已注册选项
        bool recognized = false;
        for (size_t j = 0; j < XVector_size_base(parser->options); j++) {
            XCommandLineOption* opt = XVector_at_base(parser->options, j);
            if ((isLong && opt->longName && strcmp(opt->longName, key) == 0) ||
                (!isLong && opt->shortName && strcmp(opt->shortName, key) == 0)) {

                recognized = true;
                // 处理需要值的选项（如 -c value 格式）
                if (opt->requiresValue && value[0] == '\0') {
                    if (i + 1 < argc && !isOption(argv[i + 1])) {
                        value = argv[++i];
                    }
                    else {
                        // 缺少必要的参数值
                        XVector_push_back_base(parser->result->unrecognizedOpts, &arg);
                        recognized = false;
                    }
                }
                break;
            }
        }

        if (recognized) {
            XHashMap_insert_base(parser->result->optionMap, &key, &value);
        }
        else {
            XVector_push_back_base(parser->result->unrecognizedOpts, &arg);
        }
    }

    return true;
}

bool XCommandLineParser_hasOption(XCommandLineParser* parser, const char* option) {
    if (!parser || !option || !parser->result) return false;
    return XMapBase_contains(parser->result->optionMap, &option);
}

const char* XCommandLineParser_getOptionValue(XCommandLineParser* parser, const char* option) {
    if (!parser || !option || !parser->result) return NULL;
    return XHashMap_value_base(parser->result->optionMap, &option);
}

XVector* XCommandLineParser_positionalArguments(XCommandLineParser* parser) {
    return parser && parser->result ? parser->result->positionalArgs : NULL;
}

XVector* XCommandLineParser_unrecognizedOptions(XCommandLineParser* parser) {
    return parser && parser->result ? parser->result->unrecognizedOpts : NULL;
}

XString* XCommandLineParser_helpText(XCommandLineParser* parser, const char* description) {
    if (!parser) return NULL;

    XString* text = XString_create("");
    XString_create_fmt_utf8(text, "%s\n", description ? description : "命令行工具");
    XString_create_fmt_utf8(text, "用法: %s [选项] [位置参数]\n\n选项:\n", parser->programName);

    for (size_t i = 0; i < XVector_size_base(parser->options); i++) {
        XCommandLineOption* opt = XVector_at_base(parser->options, i);
        XString* line = XString_create("  ");

        if (opt->shortName) {
            XString_create_fmt_utf8(line, "-%s", opt->shortName);
            if (opt->longName) {
                XString_append(line, ", ");
            }
        }

        if (opt->longName) {
            XString_create_fmt_utf8(line, "--%s", opt->longName);
        }

        if (opt->requiresValue) {
            XString_append(line, " <值>");
        }

        // 对齐描述文本
        while (XString_length_base(line) < 24) {
            XString_append(line, " ");
        }
        XString_create_fmt_utf8(line, "%s\n", opt->description);

        XString_append(text, line);
        XString_delete_base(line);
    }

    return text;
}

XString* XCommandLineParser_versionText(XCommandLineParser* parser, const char* version) {
    if (!parser) return NULL;
    return XString_create_fmt_utf8("%s 版本 %s", parser->programName, version ? version : "1.0.0");
}