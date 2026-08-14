/**
 * @file main.c
 * @brief XinYueC 默认控制台入口。
 * @details
 * 无命令行参数时启动 XConsoleShell，通过标准输入输出驱动命令循环；
 * 旧的交互式 XMenuTest 不再作为默认入口，测试统一由 Test 命令调度。
 * --test 选项保留用于构建脚本和自动化测试兼容；交互退出使用 Shell 的 exit 命令。
 * Shell 输入由事件调度器非阻塞轮询，默认 Shell 生命周期由事件调度器托管。
 */

#include "CXinYueConfig.h"
#include "XCoreApplication.h"
#include "XCommandLineParser.h"
#include "XCommandLineOption.h"
#include "XString.h"
#include "XStringList.h"
#include "XPrintf.h"
#include "XDeviceTest.h"
#include "XProcessTest.h"
#include "XConsoleShellTest.h"
#include "XConsoleShellBackendTest.h"
#include "XSshTelnetClientTest.h"
#include "XCryptographicPrimitiveTest.h"
#include "XSslTest.h"
#include "XTestCommand.h"
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON
#include "XConsoleShell.h"
#include "XAbstractEventDispatcher.h"
#endif

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int main_run_test_path(const char* testPath)
{
    if (!testPath) return 1;
    if (strcmp(testPath, "esp8266-auto") == 0) {
        const char* port = getenv("XESP8266_PORT");
        const char* ssid = getenv("XESP8266_SSID");
        const char* password = getenv("XESP8266_PASSWORD");
        return XESP8266WifiTest_runAutomated(port, ssid, password);
    }
    if (strcmp(testPath, "esp8266-unit") == 0)
        return XESP8266WifiTest_runUnit();
    if (strcmp(testPath, "xprocess") == 0)
        return XProcessTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xconsole-shell") == 0)
        return XConsoleShellTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xconsole-shell-backend") == 0)
        return XConsoleShellBackendTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "ssh-telnet-client") == 0)
        return XSshTelnetClientTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xcryptographic") == 0)
        return XCryptographicPrimitiveTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "xssl") == 0)
        return XSslTest_runAll() ? 0 : 1;
    if (strcmp(testPath, "all") == 0) {
        bool process = XProcessTest_runAll();
        bool shell = XConsoleShellTest_runAll();
        bool backend = XConsoleShellBackendTest_runAll();
        bool clients = XSshTelnetClientTest_runAll();
        bool cryptographic = XCryptographicPrimitiveTest_runAll();
        bool ssl = XSslTest_runAll();
        return process && shell && backend && clients && cryptographic && ssl ? 0 : 1;
    }
    XPrintf("未知测试命令: %s\n", testPath);
    return 1;
}

static void main_print_test_list(void)
{
    XPrintf("可用测试命令:\n");
    XPrintf("  Test xprocess\n");
    XPrintf("  Test xconsole-shell\n");
    XPrintf("  Test xconsole-shell-backend\n");
    XPrintf("  Test ssh-telnet-client\n");
    XPrintf("  Test xcryptographic\n");
    XPrintf("  Test xssl\n");
    XPrintf("  Test all\n");
    XPrintf("  --test esp8266-unit\n");
    XPrintf("  --test esp8266-auto\n");
    XPrintf("  --test all\n");
}

int main(int argc, char* args[])
{
    XCoreApplication* app;
    XCommandLineParser* parser;
    XCommandLineOption* option;
    XStringList* arguments;
    int result = 0;

    setvbuf(stdout, NULL, _IONBF, 0);
    XPrintf("=== XinYueC 启动 ===\n");
    app = XCoreApplication_create(argc, args);
    if (!app) return 1;

    parser = XCommandLineParser_create();
    if (!parser) {
        XCoreApplication_delete_base(app);
        return 1;
    }
    XCommandLineParser_setApplicationDescription(parser, "XinYueC 控制台");
    option = XCommandLineOption_createFull("t", "直接运行指定测试", "test-path", NULL);
    if (!option) {
        if (option) XCommandLineOption_delete(option);
        XCommandLineParser_delete(parser);
        XCoreApplication_delete_base(app);
        return 1;
    }
    XCommandLineOption_addName(option, "test");
    if (!XCommandLineParser_addOption(parser, option)) {
        XCommandLineOption_delete(option);
        XCommandLineParser_delete(parser);
        XCoreApplication_delete_base(app);
        return 1;
    }
    XCommandLineOption_delete(option);
    option = XCommandLineOption_createFull("l", "列出测试命令", NULL, NULL);
    if (!option) {
        if (option) XCommandLineOption_delete(option);
        XCommandLineParser_delete(parser);
        XCoreApplication_delete_base(app);
        return 1;
    }
    XCommandLineOption_addName(option, "list");
    if (!XCommandLineParser_addOption(parser, option)) {
        XCommandLineOption_delete(option);
        XCommandLineParser_delete(parser);
        XCoreApplication_delete_base(app);
        return 1;
    }
    XCommandLineOption_delete(option);
    XCommandLineParser_addHelpOption(parser);

    if (argc > 1) {
        arguments = XStringList_create();
        if (!arguments) {
            XCommandLineParser_delete(parser);
            XCoreApplication_delete_base(app);
            return 1;
        }
        for (int i = 0; i < argc; ++i)
            XStringList_push_back_utf8(arguments, args[i]);
        if (!XCommandLineParser_parse(parser, arguments)) {
            XPrintf("错误: %s\n", XCommandLineParser_errorText(parser));
            XPrintf("使用 --help 查看帮助信息。\n");
            XStringList_delete_base(arguments);
            XCommandLineParser_delete(parser);
            XCoreApplication_delete_base(app);
            return 1;
        }
        if (XCommandLineParser_isSet(parser, "help") ||
            XCommandLineParser_isSet(parser, "h")) {
            XString* help = XCommandLineParser_helpText(parser);
            if (help) {
                XPrintf("%s\n", XString_toUtf8(help));
                XString_delete_base(help);
            }
            XStringList_delete_base(arguments);
            XCommandLineParser_delete(parser);
            XCoreApplication_delete_base(app);
            return 0;
        }
        if (XCommandLineParser_isSet(parser, "list") ||
            XCommandLineParser_isSet(parser, "l")) {
            main_print_test_list();
            XStringList_delete_base(arguments);
            XCommandLineParser_delete(parser);
            XCoreApplication_delete_base(app);
            return 0;
        }
        if (XCommandLineParser_isSet(parser, "test") ||
            XCommandLineParser_isSet(parser, "t")) {
            result = main_run_test_path(XCommandLineParser_value(parser, "test"));
            XStringList_delete_base(arguments);
            XCommandLineParser_delete(parser);
            XCoreApplication_delete_base(app);
            return result;
        }
        XStringList_delete_base(arguments);
    }
    XCommandLineParser_delete(parser);

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON
    {
        XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
        XConsoleShell* shell = XAbstractEventDispatcher_consoleShell(dispatcher);
        if (shell)
            XConsoleShell_registerStaticCommands(shell, &XTestCommand, 1);
        result = XAbstractEventDispatcher_runDefaultShell(dispatcher);
    }
#else
    result = XCoreApplication_exec();
#endif
    XCoreApplication_delete_base(app);
    return result;
}
