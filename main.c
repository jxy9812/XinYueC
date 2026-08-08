/**
 * @file main.c
 * @brief XinYueC 默认控制台入口。
 * @details
 * 无命令行参数时启动 XConsoleShell，通过标准输入输出驱动命令循环；
 * 旧的交互式 XMenuTest 不再作为默认入口，测试统一由 Test 命令调度。
 * --test 选项保留用于构建脚本和自动化测试兼容；交互退出使用 Shell 的 exit 命令。
 * Shell 输入由事件调度器非阻塞轮询。
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
#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON
#include "XConsoleShell.h"
#include "XFileSystem.h"
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

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON
typedef struct MainConsoleTransport {
    XFd inputFd;     /**< 由 XFileSystem 管理的非阻塞标准输入描述符。 */
    bool endOfInput; /**< 标准输入已到文件尾。 */
} MainConsoleTransport;

static int64_t main_console_read(void* userData, void* data, size_t size)
{
    MainConsoleTransport* transport = (MainConsoleTransport*)userData;
    int64_t result;
    if (!transport || (!data && size) || size == 0 ||
        transport->inputFd == XFD_INVALID) return -1;
    result = XFileSystem_readStandardInput(transport->inputFd, data,
                                            (int64_t)size);
    if (result == -1) transport->endOfInput = true;
    return result == -2 ? -1 : result;
}

static bool main_console_attach(void* userData, XConsoleShell* shell)
{
    MainConsoleTransport* transport = (MainConsoleTransport*)userData;
    int error = 0;
    (void)shell;
    if (!transport) return false;
    transport->inputFd = XFileSystem_openStandardInput(&error);
    transport->endOfInput = false;
    return transport->inputFd != XFD_INVALID;
}

static void main_console_detach(void* userData, XConsoleShell* shell)
{
    MainConsoleTransport* transport = (MainConsoleTransport*)userData;
    (void)shell;
    if (!transport) return;
    if (transport->inputFd != XFD_INVALID) {
        XFileSystem_close(transport->inputFd);
        transport->inputFd = XFD_INVALID;
    }
}

static bool main_console_input_echo(void* userData, bool enabled)
{
    MainConsoleTransport* transport = (MainConsoleTransport*)userData;
    if (!transport || transport->inputFd == XFD_INVALID) return false;
    return XFileSystem_setStandardInputEcho(transport->inputFd, enabled);
}

static void main_console_prompt(void* userData, XConsoleShell* shell)
{
    const char* name = NULL;
    (void)userData;
#if XCONSOLE_SHELL_LOGIN_ON
    name = XConsoleShellLogin_userName(shell);
#endif
    if (!name || !name[0]) name = "XinYueC";
    (void)XConsoleShell_writeUtf8(shell, name);
    (void)XConsoleShell_writeUtf8(shell, "> ");
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

static XConsoleShell* main_create_shell(MainConsoleTransport* transport)
{
    XConsoleShellIo io;
    XConsoleShell* shell;
    memset(&io, 0, sizeof(io));
    io.read = main_console_read;
    io.write = main_console_write;
    io.flush = main_console_flush;
    io.inputAttach = main_console_attach;
    io.inputDetach = main_console_detach;
    io.inputEcho = main_console_input_echo;
    io.prompt = main_console_prompt;
    io.userData = transport;
    shell = XConsoleShell_create(&io);
    if (!shell || !XConsoleShell_registerStaticCommands(shell, &XTestCommand, 1)) {
        if (shell) XConsoleShell_delete_base(shell);
        XPrintf("默认 Shell 初始化失败\n");
        return NULL;
    }
    main_console_prompt(transport, shell);
    return shell;
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

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_ASYNC_ON
    {
        MainConsoleTransport transport = { XFD_INVALID, false };
        XConsoleShell* shell = main_create_shell(&transport);
        if (!shell) {
            result = 1;
        } else {
            result = XCoreApplication_exec();
            XConsoleShell_delete_base(shell);
        }
    }
#else
    result = XCoreApplication_exec();
#endif
    XCoreApplication_delete_base(app);
    return result;
}
