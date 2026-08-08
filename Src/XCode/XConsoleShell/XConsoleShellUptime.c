/**
 * @file XConsoleShellUptime.c
 * @brief XConsoleShell 内建 `uptime` 命令实现。
 * @details
 * 运行时间通过 Shell 初始化时记录的 XDateTime 日历毫秒时间戳计算，结果使用
 * 固定栈缓冲输出为天、时、分、秒。所有输出经过 Shell I/O，未调用平台 API。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_UPTIME_ON

#include "XConsoleShellUptime.h"
#include "XDateTime.h"
#include <stdint.h>
#include <stdio.h>

static int xcs_uptime(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    int64_t now;
    uint64_t elapsed;
    uint64_t totalSeconds;
    uint64_t days;
    unsigned hours;
    unsigned minutes;
    unsigned seconds;
    char line[96];
    int written;
    (void)session;
    (void)argv;
    (void)userData;
    if (!shell || argc != 0) return XConsoleResult_InvalidArgument;
    now = XDateTime_currentMSecsSinceEpoch();
    if (shell->m_startTimeMsecs <= 0 || now < shell->m_startTimeMsecs) {
        if (!XConsoleShell_writeUtf8(shell, "uptime: 不可用\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_NotSupported;
    }
    elapsed = (uint64_t)(now - shell->m_startTimeMsecs);
    totalSeconds = elapsed / 1000u;
    days = totalSeconds / 86400u;
    hours = (unsigned)((totalSeconds / 3600u) % 24u);
    minutes = (unsigned)((totalSeconds / 60u) % 60u);
    seconds = (unsigned)(totalSeconds % 60u);
    written = snprintf(line, sizeof(line), "uptime: %llud %02u:%02u:%02u\n",
                       (unsigned long long)days, hours, minutes, seconds);
    if (written < 0 || (size_t)written >= sizeof(line)) return XConsoleResult_IoError;
    return XConsoleShell_writeUtf8(shell, line) ? XConsoleResult_Ok : XConsoleResult_IoError;
}

const XConsoleCommand XConsoleShellUptime_command = {
    "uptime", NULL, "显示 Shell 初始化后的运行时间", "uptime", 0, 0,
    XConsoleCommandFlag_None, xcs_uptime, NULL, 0, NULL
};

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
          XCONSOLE_SHELL_UPTIME_ON */
