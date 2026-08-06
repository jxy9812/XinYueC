/**
 * @file main.c
 * @brief XinYueC 默认控制台入口。
 * @details
 * 无命令行参数时启动 XConsoleShell，通过标准输入输出驱动命令循环；
 * 旧的交互式 XMenuTest 不再作为默认入口，测试统一由 Test 命令调度。
 * --test 选项保留用于构建脚本和自动化测试兼容。
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
#include "XTestCommand.h"
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON
#include "XConsoleShell.h"
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
    XPrintf("未知测试命令: %s\n", testPath);
    return 1;
}

static void main_print_test_list(void)
{
    XPrintf("可用测试命令:\n");
    XPrintf("  Test xprocess\n");
    XPrintf("  Test xconsole-shell\n");
    XPrintf("  Test xconsole-shell-backend\n");
    XPrintf("  Test all\n");
    XPrintf("  --test esp8266-unit\n");
    XPrintf("  --test esp8266-auto\n");
}

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON
typedef struct MainConsoleTransport {
    bool endOfInput; /**< 标准输入已到文件尾。 */
} MainConsoleTransport;

static int64_t main_console_read(void* userData, void* data, size_t size)
{
    MainConsoleTransport* transport = (MainConsoleTransport*)userData;
    char* line;
    if (!transport || (!data && size)) return -1;
    if (size < 2) return -1;
    /* fgets 在终端遇到换行立即返回，避免 fread 等待填满整块缓冲。 */
    line = fgets((char*)data, (int)size, stdin);
    if (!line) {
        if (feof(stdin)) transport->endOfInput = true;
        return ferror(stdin) ? -1 : 0;
    }
    return (int64_t)strlen(line);
}

static int64_t main_console_write(void* userData, const void* data, size_t size)
{
    (void)userData;
    if ((!data && size) || (size && fwrite(data, 1, size, stdout) != size)) return -1;
    return (int64_t)size;
}

static bool main_console_flush(void* userData)
{
    (void)userData;
    return fflush(stdout) == 0;
}

static int main_run_shell(void)
{
    MainConsoleTransport transport = { false };
    XConsoleShellIo io;
    XConsoleShell* shell;
    memset(&io, 0, sizeof(io));
    io.read = main_console_read;
    io.write = main_console_write;
    io.flush = main_console_flush;
    io.userData = &transport;
    shell = XConsoleShell_create(&io);
    if (!shell || !XConsoleShell_registerStaticCommands(shell, &XTestCommand, 1)) {
        if (shell) XConsoleShell_delete_base(shell);
        XPrintf("默认 Shell 初始化失败\n");
        return 1;
    }
    XConsoleShell_writeUtf8(shell, "XinYueC> ");
    while (!transport.endOfInput) {
        XConsoleResult result = XConsoleShell_pump(shell, 128);
#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
        XConsoleShell_pollProcesses(shell, 0);
#endif
        if (transport.endOfInput) break;
        if (result == XConsoleResult_IoError) {
            XConsoleShell_writeError(shell, result, "控制台输入失败");
            break;
        }
        XConsoleShell_writeUtf8(shell, "XinYueC> ");
    }
    XConsoleShell_delete_base(shell);
    return 0;
}
#endif

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

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON
    result = main_run_shell();
#else
    result = XCoreApplication_exec();
#endif
    XCoreApplication_delete_base(app);
    return result;
}
