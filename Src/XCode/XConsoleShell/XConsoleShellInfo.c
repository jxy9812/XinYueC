/**
 * @file XConsoleShellInfo.c
 * @brief XConsoleShell 内建 `info` 命令实现。
 * @details
 * 所有信息均来自编译期宏和固定字符串，通过 XConsoleShell_writeUtf8 输出。
 * 本文件不调用平台 API、不创建对象、不分配堆内存；每行使用固定栈缓冲生成。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_INFO_ON

#include "CXinYueConfig.h"
#include "XConsoleShellInfo.h"
#include <stdio.h>

static bool xcs_info_write_line(XConsoleShell* shell, const char* key,
                                const char* value)
{
    char line[96];
    int written;
    if (!shell || !key || !value) return false;
    written = snprintf(line, sizeof(line), "%-12s : %s\n", key, value);
    return written > 0 && (size_t)written < sizeof(line) &&
           XConsoleShell_writeUtf8(shell, line);
}

static const char* xcs_info_platform(void)
{
#if XPLATFORM_WINDOWS
    return "Windows";
#elif XPLATFORM_POSIX
    return "POSIX";
#elif XPLATFORM_FREERTOS
    return "FreeRTOS";
#elif XPLATFORM_BAREMETAL
    return "裸机";
#else
    return "未知";
#endif
}

static const char* xcs_info_state(int enabled)
{
    return enabled ? "已启用" : "已禁用";
}

static int xcs_info(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    (void)session;
    (void)argv;
    (void)userData;
    if (!shell || argc != 0) return XConsoleResult_InvalidArgument;
    if (!xcs_info_write_line(shell, "框架", "XinYueC") ||
        !xcs_info_write_line(shell, "模块", "XConsoleShell") ||
        !xcs_info_write_line(shell, "语言", "C99") ||
        !xcs_info_write_line(shell, "平台", xcs_info_platform()) ||
        !xcs_info_write_line(shell, "内存", "固定缓冲") ||
        !xcs_info_write_line(shell, "文件系统", xcs_info_state(XCONSOLE_SHELL_FILESYSTEM_ON)) ||
        !xcs_info_write_line(shell, "网络", xcs_info_state(XCONSOLE_SHELL_NETWORK_ON)) ||
        !xcs_info_write_line(shell, "日期时间", xcs_info_state(XCONSOLE_SHELL_DATETIME_ON)) ||
        !xcs_info_write_line(shell, "内存池", xcs_info_state(XCONSOLE_SHELL_MEMORY_POOL_ON))) {
        return XConsoleResult_IoError;
    }
    return XConsoleResult_Ok;
}

const XConsoleCommand XConsoleShellInfo_command = {
    "info", NULL, "显示框架、Shell 和编译平台信息", "info", 0, 0,
    XConsoleCommandFlag_None, xcs_info, NULL, 0, NULL
};

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
          XCONSOLE_SHELL_INFO_ON */
