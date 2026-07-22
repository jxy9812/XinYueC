#include "XCodeTest.h"
#include "XMemory.h"
#include "XMenu.h"
#include "XAction.h"
#include "XCoreApplication.h"
#include "XCommandLineParser.h"
#include "XCommandLineOption.h"
#include "XString.h"
#include "XStringList.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* ==================== 辅助函数 ==================== */

static XStringList* makeArgs(const char* prog, const char* arg1, const char* arg2, const char* arg3)
{
    XStringList* args = XStringList_create();
    if (prog) XStringList_push_back_utf8(args, prog);
    if (arg1) XStringList_push_back_utf8(args, arg1);
    if (arg2) XStringList_push_back_utf8(args, arg2);
    if (arg3) XStringList_push_back_utf8(args, arg3);
    return args;
}

static void deleteArgs(XStringList* args)
{
    if (!args) return;
    XStringList_delete_base(args);
}

/* ==================== 测试 1: 基本选项解析 ==================== */
static void test_basic_parsing(void)
{
    printf("\n===== [测试 1] 基本选项解析 =====\n");

    XCommandLineParser* parser = XCommandLineParser_create();
    assert(parser != NULL);

    XCommandLineOption* optOutput = XCommandLineOption_createFull("o", "输出文件", "file", NULL);
    XCommandLineOption_addName(optOutput, "output");
    bool ok = XCommandLineParser_addOption(parser, optOutput);
    assert(ok);

    XCommandLineOption* optVerbose = XCommandLineOption_createFull("v", "详细输出", NULL, NULL);
    XCommandLineOption_addName(optVerbose, "verbose");
    ok = XCommandLineParser_addOption(parser, optVerbose);
    assert(ok);

    XStringList* args = makeArgs("prog", "-o", "out.txt", "-v");
    XStringList_push_back_utf8(args, "file1");

    bool result = XCommandLineParser_parse(parser, args);
    printf("  解析结果: %s (预期 true)\n", result ? "成功" : "失败");
    assert(result == true);

    bool isSet = XCommandLineParser_isSet(parser, "o");
    printf("  -o 已设置: %s (预期 true)\n", isSet ? "是" : "否");
    assert(isSet == true);

    const char* val = XCommandLineParser_value(parser, "o");
    printf("  -o 的值: %s (预期 'out.txt')\n", val ? val : "NULL");
    assert(val != NULL && strcmp(val, "out.txt") == 0);

    isSet = XCommandLineParser_isSet(parser, "v");
    printf("  -v 已设置: %s (预期 true)\n", isSet ? "是" : "否");
    assert(isSet == true);

    const XStringList* posArgs = XCommandLineParser_positionalArguments(parser);
    printf("  位置参数数量: %zu (预期 1)\n", XStringList_size_base(posArgs));
    assert(XStringList_size_base(posArgs) == 1);

    printf("  [通过] 基本选项解析正常\n");

    deleteArgs(args);
    XCommandLineParser_delete(parser);
}

/* ==================== 测试 2: 长选项 ==================== */
static void test_long_options(void)
{
    printf("\n===== [测试 2] 长选项解析 =====\n");

    XCommandLineParser* parser = XCommandLineParser_create();
    assert(parser != NULL);

    XCommandLineOption* opt = XCommandLineOption_createFull("o", "输出文件", "file", NULL);
    XCommandLineOption_addName(opt, "output");
    XCommandLineParser_addOption(parser, opt);

    XStringList* args = makeArgs("prog", "--output=result.txt", NULL, NULL);
    bool result = XCommandLineParser_parse(parser, args);
    printf("  解析 --output=result.txt: %s (预期 true)\n", result ? "成功" : "失败");
    assert(result == true);

    const char* val = XCommandLineParser_value(parser, "output");
    printf("  --output 的值: %s (预期 'result.txt')\n", val ? val : "NULL");
    assert(val != NULL && strcmp(val, "result.txt") == 0);

    printf("  [通过] 长选项解析正常\n");

    deleteArgs(args);
    XCommandLineParser_delete(parser);
}

/* ==================== 测试 3: 未知选项 ==================== */
static void test_unknown_options(void)
{
    printf("\n===== [测试 3] 未知选项 =====\n");

    XCommandLineParser* parser = XCommandLineParser_create();
    assert(parser != NULL);

    XStringList* args = makeArgs("prog", "--unknown-opt", NULL, NULL);
    bool result = XCommandLineParser_parse(parser, args);
    printf("  解析未知选项: %s (预期 false)\n", result ? "成功" : "失败");
    assert(result == false);

    const XStringList* unknown = XCommandLineParser_unknownOptionNames(parser);
    printf("  未知选项数量: %zu (预期 1)\n", XStringList_size_base(unknown));
    assert(XStringList_size_base(unknown) == 1);

    printf("  [通过] 未知选项检测正常\n");

    deleteArgs(args);
    XCommandLineParser_delete(parser);
}

/* ==================== 测试 4: 帮助文本 ==================== */
static void test_help_text(void)
{
    printf("\n===== [测试 4] 帮助文本生成 =====\n");

    XCommandLineParser* parser = XCommandLineParser_create();
    assert(parser != NULL);

    XCommandLineParser_setApplicationDescription(parser, "测试应用程序");

    XCommandLineOption* opt = XCommandLineOption_createFull("o", "输出文件", "file", NULL);
    XCommandLineOption_addName(opt, "output");
    XCommandLineParser_addOption(parser, opt);

    XCommandLineParser_addPositionalArgument(parser, "input", "输入文件", "<input>");

    XString* help = XCommandLineParser_helpText(parser);
    printf("  帮助文本:\n%s\n", help ? XString_toUtf8(help) : "NULL");
    assert(help != NULL && XString_length_base(help) > 0);

    XString_delete_base(help);
    XCommandLineParser_delete(parser);

    printf("  [通过] 帮助文本生成正常\n");
}

/* ==================== 测试 5: 位置参数后选项模式 ==================== */
static void test_positional_args_mode(void)
{
    printf("\n===== [测试 5] 位置参数后选项模式 =====\n");

    XCommandLineParser* parser = XCommandLineParser_create();
    assert(parser != NULL);

    XCommandLineOption* opt = XCommandLineOption_createFull("v", "详细模式", NULL, NULL);
    XCommandLineOption_addName(opt, "verbose");
    XCommandLineParser_addOption(parser, opt);

    XCommandLineParser_setOptionsAfterPositionalArgumentsMode(parser,
        XCOMMANDLINE_PARSER_PARSE_AS_POSITIONAL_ARGUMENTS);

    XStringList* args = makeArgs("prog", "file1", "-v", NULL);
    bool result = XCommandLineParser_parse(parser, args);
    printf("  解析结果: %s (预期 true)\n", result ? "成功" : "失败");
    assert(result == true);

    bool isSet = XCommandLineParser_isSet(parser, "v");
    printf("  -v 已设置: %s (预期 false)\n", isSet ? "是" : "否");
    assert(isSet == false);

    const XStringList* posArgs = XCommandLineParser_positionalArguments(parser);
    printf("  位置参数数量: %zu (预期 2: file1, -v)\n", XStringList_size_base(posArgs));
    assert(XStringList_size_base(posArgs) == 2);

    printf("  [通过] 位置参数后选项模式正常\n");

    deleteArgs(args);
    XCommandLineParser_delete(parser);
}

/* ==================== 测试 6: 默认值 ==================== */
static void test_default_values(void)
{
    printf("\n===== [测试 6] 默认值 =====\n");

    XCommandLineOption* opt = XCommandLineOption_createFull("p", "端口号", "port", "8080");
    assert(opt != NULL);

    const XStringList* defaults = XCommandLineOption_defaultValues(opt);
    printf("  默认值数量: %zu (预期 1)\n", XStringList_size_base(defaults));
    assert(XStringList_size_base(defaults) == 1);

    const XString* ds = XStringList_at_base(defaults, 0);
    printf("  默认值: %s (预期 '8080')\n", ds ? XString_toUtf8(ds) : "NULL");
    assert(ds != NULL && strcmp(XString_toUtf8(ds), "8080") == 0);

    printf("  需要值: %s (预期 true)\n", XCommandLineOption_requiresValue(opt) ? "是" : "否");
    assert(XCommandLineOption_requiresValue(opt) == true);

    XCommandLineOption_delete(opt);
    printf("  [通过] 默认值正常\n");
}

/* ==================== 测试 7: 选项标志 ==================== */
static void test_option_flags(void)
{
    printf("\n===== [测试 7] 选项标志 =====\n");

    XCommandLineOption* opt = XCommandLineOption_create("hidden");
    XCommandLineOption_setFlags(opt, XCOMMANDLINE_OPTION_FLAG_HIDDEN_FROM_HELP);

    printf("  隐藏: %s (预期 true)\n", XCommandLineOption_isHidden(opt) ? "是" : "否");
    assert(XCommandLineOption_isHidden(opt) == true);

    int flags = XCommandLineOption_flags(opt);
    printf("  flags: %d (预期 %d)\n", flags, XCOMMANDLINE_OPTION_FLAG_HIDDEN_FROM_HELP);
    assert(flags == XCOMMANDLINE_OPTION_FLAG_HIDDEN_FROM_HELP);

    XCommandLineOption_delete(opt);
    printf("  [通过] 选项标志正常\n");
}

/* ==================== 测试 8: 紧凑短选项 ==================== */
static void test_compacted_short_options(void)
{
    printf("\n===== [测试 8] 紧凑短选项 =====\n");

    XCommandLineParser* parser = XCommandLineParser_create();
    assert(parser != NULL);

    XCommandLineOption* optA = XCommandLineOption_createFull("a", "选项A", NULL, NULL);
    XCommandLineOption* optB = XCommandLineOption_createFull("b", "选项B", NULL, NULL);
    XCommandLineOption* optC = XCommandLineOption_createFull("c", "选项C", NULL, NULL);
    XCommandLineParser_addOption(parser, optA);
    XCommandLineParser_addOption(parser, optB);
    XCommandLineParser_addOption(parser, optC);

    /* -abc 应解析为 -a -b -c */
    XStringList* args = makeArgs("prog", "-abc", NULL, NULL);
    bool result = XCommandLineParser_parse(parser, args);
    printf("  解析 -abc: %s (预期 true)\n", result ? "成功" : "失败");
    assert(result == true);

    printf("  -a 已设置: %s (预期 true)\n", XCommandLineParser_isSet(parser, "a") ? "是" : "否");
    assert(XCommandLineParser_isSet(parser, "a"));

    printf("  -b 已设置: %s (预期 true)\n", XCommandLineParser_isSet(parser, "b") ? "是" : "否");
    assert(XCommandLineParser_isSet(parser, "b"));

    printf("  -c 已设置: %s (预期 true)\n", XCommandLineParser_isSet(parser, "c") ? "是" : "否");
    assert(XCommandLineParser_isSet(parser, "c"));

    printf("  [通过] 紧凑短选项解析正常\n");

    deleteArgs(args);
    XCommandLineParser_delete(parser);
}

/* ==================== 测试 9: 双横杠终止符 ==================== */
static void test_double_dash(void)
{
    printf("\n===== [测试 9] 双横杠终止符 =====\n");

    XCommandLineParser* parser = XCommandLineParser_create();
    assert(parser != NULL);

    XCommandLineOption* opt = XCommandLineOption_createFull("v", "详细模式", NULL, NULL);
    XCommandLineParser_addOption(parser, opt);

    /* -- 之后所有参数视为位置参数 */
    XStringList* args = XStringList_create();
    XStringList_push_back_utf8(args, "prog");
    XStringList_push_back_utf8(args, "--");
    XStringList_push_back_utf8(args, "-v");
    XStringList_push_back_utf8(args, "--foo");
    bool result = XCommandLineParser_parse(parser, args);
    printf("  解析 -- -v --foo: %s (预期 true)\n", result ? "成功" : "失败");
    assert(result == true);

    printf("  -v 已设置: %s (预期 false)\n", XCommandLineParser_isSet(parser, "v") ? "是" : "否");
    assert(!XCommandLineParser_isSet(parser, "v"));

    const XStringList* posArgs = XCommandLineParser_positionalArguments(parser);
    printf("  位置参数数量: %zu (预期 2: -v, --foo)\n", XStringList_size_base(posArgs));
    assert(XStringList_size_base(posArgs) == 2);

    printf("  [通过] 双横杠终止符正常\n");

    deleteArgs(args);
    XCommandLineParser_delete(parser);
}

/* ==================== 测试 10: isSetOption 通过选项对象 ==================== */
static void test_isSetOption(void)
{
    printf("\n===== [测试 10] isSetOption 通过选项对象 =====\n");

    XCommandLineParser* parser = XCommandLineParser_create();
    assert(parser != NULL);

    XCommandLineOption* opt = XCommandLineOption_createFull("v", "详细模式", NULL, NULL);
    XCommandLineOption_addName(opt, "verbose");
    XCommandLineParser_addOption(parser, opt);

    XStringList* args = makeArgs("prog", "--verbose", NULL, NULL);
    XCommandLineParser_parse(parser, args);

    printf("  isSetOption(opt): %s (预期 true)\n", XCommandLineParser_isSetOption(parser, opt) ? "是" : "否");
    assert(XCommandLineParser_isSetOption(parser, opt));

    printf("  [通过] isSetOption 正常\n");

    deleteArgs(args);
    XCommandLineParser_delete(parser);
}

/* ==================== 测试 11: values 多值 ==================== */
static void test_multi_values(void)
{
    printf("\n===== [测试 11] 多值选项 =====\n");

    XCommandLineParser* parser = XCommandLineParser_create();
    assert(parser != NULL);

    XCommandLineOption* opt = XCommandLineOption_createFull("I", "包含路径", "path", NULL);
    XCommandLineOption_addName(opt, "include");
    XCommandLineParser_addOption(parser, opt);

    XStringList* args = XStringList_create();
    XStringList_push_back_utf8(args, "prog");
    XStringList_push_back_utf8(args, "-I");
    XStringList_push_back_utf8(args, "/usr/include");
    XStringList_push_back_utf8(args, "-I");
    XStringList_push_back_utf8(args, "/usr/local/include");

    XCommandLineParser_parse(parser, args);

    const XStringList* vals = XCommandLineParser_values(parser, "I");
    printf("  -I 值的数量: %zu (预期 2)\n", XStringList_size_base(vals));
    assert(XStringList_size_base(vals) == 2);

    printf("  [通过] 多值选项正常\n");

    XStringList_delete_base(args);
    XCommandLineParser_delete(parser);
}

/* ==================== 主测试入口 ==================== */
void XCommandLineParserTest(XVariant* variant)
{
    (void)variant;
    printf("\n========================================\n");
    printf("  XCommandLineParser Qt 行为对齐测试\n");
    printf("========================================\n");

    test_basic_parsing();
    test_long_options();
    test_unknown_options();
    test_help_text();
    test_positional_args_mode();
    test_default_values();
    test_option_flags();
    test_compacted_short_options();
    test_double_dash();
    test_isSetOption();
    test_multi_values();

    printf("\n========================================\n");
    printf("  所有 XCommandLineParser 测试通过！\n");
    printf("========================================\n");
}

void XMenu_XCommandLineParserTest(XMenu* root)
{
    XMenu* menu = XMenu_create("XCommandLineParser");
    XMenu_addMenu(root, menu);
    {
        XAction* action = XMenu_addAction(menu, "Qt 对齐测试");
        XAction_setAction(action, XCommandLineParserTest);
    }
}
