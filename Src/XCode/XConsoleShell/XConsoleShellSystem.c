/**
 * @file XConsoleShellSystem.c
 * @brief XConsoleShell 的 clear、reset、reboot 和 shutdown 命令实现。
 * @details
 * clear 使用固定 ANSI 序列；reset、reboot 和 shutdown 只调用 XSystem 公共接口。
 * 系统后端没有注册时输出明确的不支持结果，避免在 Linux、Windows 主机
 * 测试中误执行宿主系统重启。所有命令均不分配堆内存。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON

#include "XConsoleShellSystem.h"
#if XCONSOLE_SHELL_RESET_ON || XCONSOLE_SHELL_REBOOT_ON || XCONSOLE_SHELL_SHUTDOWN_ON
#include "XSystem.h"

static XConsoleResult xcs_system_result(XConsoleShell* shell,
                                        const char* operation,
                                        XSystemResult result)
{
    const char* suffix;
    if (result == XSystemResult_Ok) {
        suffix = " 已请求\n";
    } else if (result == XSystemResult_NotSupported) {
        suffix = ": 后端不可用\n";
    } else if (result == XSystemResult_InvalidArgument) {
        suffix = ": 后端参数无效\n";
    } else if (result == XSystemResult_PermissionDenied) {
        suffix = ": 权限不足\n";
    } else {
        suffix = ": 操作失败\n";
    }
    if (!shell || !operation || !XConsoleShell_writeUtf8(shell, operation) ||
        !XConsoleShell_writeUtf8(shell, suffix))
        return XConsoleResult_IoError;
    if (result == XSystemResult_Ok) return XConsoleResult_Ok;
    if (result == XSystemResult_NotSupported) return XConsoleResult_NotSupported;
    if (result == XSystemResult_InvalidArgument) return XConsoleResult_InvalidArgument;
    if (result == XSystemResult_PermissionDenied)
        return XConsoleResult_PermissionDenied;
    return XConsoleResult_Failed;
}
#endif

#if XCONSOLE_SHELL_CLEAR_ON
static int xcs_clear(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    (void)session;
    (void)argc;
    (void)argv;
    (void)userData;
    return XConsoleShell_writeUtf8(shell, "\x1b[2J\x1b[H")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

const XConsoleCommand XConsoleShellClear_command = {
    "clear", NULL, "清空 ANSI 终端显示", "clear", 0, 0,
    XConsoleCommandFlag_None, xcs_clear, NULL, 0, NULL
};
#endif

#if XCONSOLE_SHELL_RESET_ON
static int xcs_reset(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    (void)session;
    (void)argv;
    (void)userData;
    if (!shell || argc != 0) return XConsoleResult_InvalidArgument;
    return xcs_system_result(shell, "reset",
                             XSystem_reset(XSystemResetReason_Shell));
}

const XConsoleCommand XConsoleShellReset_command = {
    "reset", NULL, "立即复位系统", "reset", 0, 0,
    XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
    xcs_reset, NULL, 0, NULL
};
#endif

#if XCONSOLE_SHELL_REBOOT_ON
static int xcs_reboot(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    (void)session;
    (void)argv;
    (void)userData;
    if (!shell || argc != 0) return XConsoleResult_InvalidArgument;
    return xcs_system_result(shell, "reboot",
                             XSystem_reboot(XSystemRebootMode_Normal));
}

const XConsoleCommand XConsoleShellReboot_command = {
    "reboot", NULL, "有序重启系统", "reboot", 0, 0,
    XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
    xcs_reboot, NULL, 0, NULL
};
#endif

#if XCONSOLE_SHELL_SHUTDOWN_ON
static int xcs_shutdown(XConsoleShell* shell, XConsoleShellSession* session,
                        int argc, const char* const* argv, void* userData)
{
    XSystemResult result;
    (void)session;
    (void)argv;
    (void)userData;
    if (!shell || argc != 0) return XConsoleResult_InvalidArgument;
    result = XSystem_shutdown();
    /* 裸机或未接入后端时，shutdown 的语义是结束当前 Shell。 */
    if (result == XSystemResult_NotSupported) {
        XConsoleShell_setRunning(shell, false);
        return XConsoleShell_writeUtf8(shell, "已请求关闭\n")
                   ? XConsoleResult_Ok : XConsoleResult_IoError;
    }
    if (result == XSystemResult_Ok) XConsoleShell_setRunning(shell, false);
    return xcs_system_result(shell, "shutdown", result);
}

const XConsoleCommand XConsoleShellShutdown_command = {
    "shutdown", NULL, "关闭系统；无平台时退出程序", "shutdown", 0, 0,
    XConsoleCommandFlag_Dangerous | XConsoleCommandFlag_Administrator,
    xcs_shutdown, NULL, 0, NULL
};
#endif

#if XCONSOLE_SHELL_EXIT_ON
static int xcs_exit(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    int64_t code = 0;
    size_t i;
    (void)session;
    (void)userData;
    if (!shell) return XConsoleResult_InvalidArgument;
    if (argc > 1) return XConsoleResult_InvalidArgument;
    if (argc == 1) {
        for (i = 0; argv[0][i]; ++i) {
            if (argv[0][i] < '0' || argv[0][i] > '9') return XConsoleResult_InvalidArgument;
        }
        if (!argv[0][0]) return XConsoleResult_InvalidArgument;
        code = 0;
        for (i = 0; argv[0][i]; ++i)
            code = code * 10 + (int64_t)(argv[0][i] - '0');
        if (code > 255) code = code % 256;
    }
    XConsoleShell_setRunning(shell, false);
    return XConsoleShell_writeUtf8(shell, "已请求退出\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

const XConsoleCommand XConsoleShellExit_command = {
    "exit", NULL, "退出当前 Shell 和应用程序", "exit [n]", 0, 1,
    XConsoleCommandFlag_AllowUnauthenticated, xcs_exit, NULL, 0, NULL
};
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON */
