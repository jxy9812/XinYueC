/**
 * @file XConsoleShell.c
 * @brief XConsoleShell 生命周期、输入入口、静态命令注册和核心命令。
 * @details
 * 本文件只依赖 XObject、XMemory 和公共 Shell 契约。文件命令与进程命令分别
 * 位于独立子模块，并由配置宏裁剪。对象使用固定数组，不改变进程全局目录，
 * 也不直接包含或调用任何平台文件、终端、进程和网络接口。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON

#include "XMemory.h"
#include "XPrintf.h"
#if XCONSOLE_SHELL_ASYNC_ON
#include "XCoreApplication.h"
#include "XEvent.h"
#include "XEventLoop.h"
#include "XThread.h"
#include "XVarList.h"
#endif
#if XCONSOLE_SHELL_FILESYSTEM_ON
#include "XFileSystem.h"
#include "XString.h"
#endif
#if XCONSOLE_SHELL_NETWORK_ON
#include "XConsoleShellNetwork.h"
#endif
#if XCONSOLE_SHELL_DATETIME_ON && XCONSOLE_SHELL_DATE_ON
#include "XConsoleShellDateTime.h"
#endif
#if XCONSOLE_SHELL_MEMORY_ON && XCONSOLE_SHELL_MEMORY_POOL_ON
#include "XConsoleShellMemory.h"
#endif
#if XCONSOLE_SHELL_INFO_ON
#include "XConsoleShellInfo.h"
#endif
#if XCONSOLE_SHELL_UPTIME_ON
#include "XConsoleShellUptime.h"
#include "XDateTime.h"
#endif
#if XCONSOLE_SHELL_TASKS_ON
#include "XConsoleShellTasks.h"
#endif
#if XCONSOLE_SHELL_CAN_ON
#include "XConsoleShellCan.h"
#endif
#include <stdio.h>
#include <string.h>

static void VXConsoleShell_deinit(XObject* object);

#if XCONSOLE_SHELL_ASYNC_ON
static XEventType g_xcs_async_input_event = XEVENT_TYPE_NONE;

static void xcs_async_quit_application(void)
{
    XCoreApplication* app = XCoreApplication_instance();
    if (app && app->m_in_exec) XCoreApplication_quit();
}

static void xcs_async_read_ready(XConsoleShell* self)
{
    size_t budget = XCONSOLE_SHELL_ASYNC_READ_BUDGET;
    XConsoleResult result = XConsoleResult_Ok;
    bool completedLine = false;
    if (!self || !XAtomic_load_bool(&self->m_asyncRunning,
                                    XAtomic_MemoryOrder_Acquire)) return;
    do {
        result = XConsoleShell_pump(self, 128);
        if (self->m_asyncLastReadBytes > 0 && self->m_lineLength == 0)
            completedLine = true;
        if (result == XConsoleResult_IoError ||
            result == XConsoleResult_NotSupported)
            break;
    } while (self->m_asyncLastReadBytes > 0 &&
             (!XAtomic_load_bool(&self->m_asyncInputAttached,
                                 XAtomic_MemoryOrder_Acquire) ||
              self->m_asyncLastReadBytes == 128) &&
             --budget > 0);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    /* 错误结果也可能已经写入用法或诊断文本，必须刷新异步输出队列。 */
    if (result != XConsoleResult_IoError)
        (void)XConsoleShell_flushOutput(self);
#endif
    if (result == XConsoleResult_IoError) {
        self->m_running = false;
        xcs_async_quit_application();
    }
    /* shutdown 等命令会主动停止 Shell；异步事件线程必须同步退出主事件循环，
       否则虽然命令已执行，exec() 仍会继续等待下一批输入。 */
    if (!self->m_running && result != XConsoleResult_IoError)
        xcs_async_quit_application();
    if (completedLine && self->m_running && self->m_io.prompt
#if XCONSOLE_SHELL_LOGIN_ON || XCONSOLE_SHELL_EDITOR_ON
        && !self->m_session.suppressPrompt
#endif
        )
        self->m_io.prompt(self->m_io.userData, self);
    if (self->m_asyncLastReadBytes > 0 &&
        XAtomic_load_bool(&self->m_asyncRunning, XAtomic_MemoryOrder_Acquire))
        (void)XConsoleShell_notifyInput(self);
}

#if XCONSOLE_SHELL_ASYNC_RUN_MODE == XCONSOLE_SHELL_ASYNC_MODE_THREAD
static void xcs_async_thread(XThread* thread, XVarList* arguments)
{
    XConsoleShell* self = NULL;
    XVarList_args_1(arguments, void*, shellArgument);
    self = (XConsoleShell*)shellArgument;
    if (!thread || !self) return;
    if (!XThread_isInterruptionRequested(thread)) {
        thread->m_loop = XEventLoop_create();
        XAtomic_store_bool(&self->m_asyncWorkerReady, thread->m_loop != NULL,
                           XAtomic_MemoryOrder_Release);
        /* startAsync 在主线程完成输入源附加后，工作线程再启动轮询定时器，
           避免在输入描述符尚未准备好时把一次空读误判为 I/O 错误。 */
        while (thread->m_loop && self->m_io.inputAttach &&
               !XAtomic_load_bool(&self->m_asyncInputAttached,
                                  XAtomic_MemoryOrder_Acquire) &&
               !XThread_isInterruptionRequested(thread))
            XThread_usleep(1000);
        if (thread->m_loop &&
            XAtomic_load_bool(&self->m_asyncInputAttached,
                              XAtomic_MemoryOrder_Acquire))
            self->m_asyncPollTimer = XObject_startTimer_ms(
                (XObject*)self, XCONSOLE_SHELL_ASYNC_POLL_INTERVAL_MS,
                XTimerType_CoarseTimer);
        if (thread->m_loop && !XThread_isInterruptionRequested(thread))
            (void)XEventLoop_exec(thread->m_loop, XEventLoop_AllEvents);
        if (self->m_asyncPollTimer != XTIMER_INVALID_ID) {
            XObject_killTimer((XObject*)self, self->m_asyncPollTimer);
            self->m_asyncPollTimer = XTIMER_INVALID_ID;
        }
    } else {
        XAtomic_store_bool(&self->m_asyncWorkerReady, true,
                           XAtomic_MemoryOrder_Release);
    }
    XAtomic_store_bool(&self->m_asyncRunning, false,
                       XAtomic_MemoryOrder_Release);
    XAtomic_store_bool(&self->m_asyncInputPosted, false,
                       XAtomic_MemoryOrder_Release);
    XAtomic_store_bool(&self->m_asyncWorkerReady, false,
                       XAtomic_MemoryOrder_Release);
    if (self->m_asyncEventType != XEVENT_TYPE_NONE)
        XCoreApplication_removePostedEvents((XObject*)self,
                                             self->m_asyncEventType);
    if (self->m_asyncOwnerThread)
        (void)XObject_moveToThread((XObject*)self, self->m_asyncOwnerThread);
}
#endif

static bool VXConsoleShell_event(XObject* object, XEvent* event)
{
    XConsoleShell* self = (XConsoleShell*)object;
    if (!self || !event) return false;
    if (event->type == self->m_asyncEventType &&
        event->type != XEVENT_TYPE_NONE) {
        XAtomic_store_bool(&self->m_asyncInputPosted, false,
                           XAtomic_MemoryOrder_Release);
        xcs_async_read_ready(self);
        XEvent_accept(event);
        return true;
    }
    return XClass_Parent(XObject, EXObject_Event,
                         bool(*)(XObject*, XEvent*))(object, event);
}

static void VXConsoleShell_timerEvent(XObject* object, XTimerEvent* event)
{
    XConsoleShell* self = (XConsoleShell*)object;
    if (self && event && XTimerEvent_timerId(event) == self->m_asyncPollTimer) {
        xcs_async_read_ready(self);
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(object, event);
}
#endif

#if XCONSOLE_SHELL_HISTORY_ON
static size_t xcs_history_index(const XConsoleShell* self, size_t index)
{
    return (self->m_historyNext + XCONSOLE_SHELL_HISTORY_CAPACITY -
            self->m_historyCount + index) % XCONSOLE_SHELL_HISTORY_CAPACITY;
}

static void xcs_record_history(XConsoleShell* self, const char* line, size_t length)
{
    size_t previous;
    if (!self || !line || !length || length >= XCONSOLE_SHELL_LINE_BUFFER_SIZE ||
        XCONSOLE_SHELL_HISTORY_CAPACITY == 0) return;
    if (self->m_historyCount) {
        previous = (self->m_historyNext + XCONSOLE_SHELL_HISTORY_CAPACITY - 1u) %
                   XCONSOLE_SHELL_HISTORY_CAPACITY;
        if (strlen(self->m_history[previous]) == length &&
            memcmp(self->m_history[previous], line, length) == 0) return;
    }
    memcpy(self->m_history[self->m_historyNext], line, length);
    self->m_history[self->m_historyNext][length] = '\0';
    self->m_historyNext = (self->m_historyNext + 1u) % XCONSOLE_SHELL_HISTORY_CAPACITY;
    if (self->m_historyCount < XCONSOLE_SHELL_HISTORY_CAPACITY) ++self->m_historyCount;
}

static void xcs_history_reset_cursor(XConsoleShell* self)
{
    if (!self) return;
    self->m_historyCursor = self->m_historyCount;
}
#else
static void xcs_record_history(XConsoleShell* self, const char* line, size_t length)
{
    (void)self;
    (void)line;
    (void)length;
}
#endif

#if XCONSOLE_SHELL_HISTORY_ON
static bool xcs_parse_nonnegative(const char* text, int64_t* value)
{
    uint64_t result = 0;
    size_t i;
    if (!text || !text[0] || !value) return false;
    for (i = 0; text[i]; ++i) {
        unsigned digit;
        if (text[i] < '0' || text[i] > '9') return false;
        digit = (unsigned)(text[i] - '0');
        if (result > ((uint64_t)INT64_MAX - digit) / 10u) return false;
        result = result * 10u + digit;
    }
    *value = (int64_t)result;
    return true;
}

static int xcs_history(XConsoleShell* shell, XConsoleShellSession* session,
                       int argc, const char* const* argv, void* userData)
{
    size_t i;
    size_t count;
    char number[24];
    (void)session;
    (void)userData;
    if (!shell) return XConsoleResult_InvalidArgument;
    count = shell->m_historyCount;
    if (argc == 1) {
        int64_t value;
        if (strcmp(argv[0], "-c") == 0 || strcmp(argv[0], "--clear") == 0) {
            shell->m_historyCount = 0;
            shell->m_historyNext = 0;
            xcs_history_reset_cursor(shell);
            return XConsoleResult_Ok;
        }
        if (strcmp(argv[0], "-d") == 0 || strcmp(argv[0], "--delete") == 0)
            return XConsoleResult_InvalidArgument;
        if (!xcs_parse_nonnegative(argv[0], &value)) return XConsoleResult_InvalidArgument;
        if (value < (int64_t)count) count = (size_t)value;
    } else if (argc == 2 && (strcmp(argv[0], "-d") == 0 ||
                             strcmp(argv[0], "--delete") == 0)) {
        int64_t value;
        size_t index;
        if (!xcs_parse_nonnegative(argv[1], &value) || value < 1 ||
            (size_t)value > shell->m_historyCount)
            return XConsoleResult_InvalidArgument;
        index = (size_t)value - 1u;
        for (i = index; i + 1u < shell->m_historyCount; ++i) {
            strcpy(shell->m_history[i], shell->m_history[i + 1u]);
        }
        --shell->m_historyCount;
        if (shell->m_historyNext > 0u) --shell->m_historyNext;
        xcs_history_reset_cursor(shell);
        return XConsoleResult_Ok;
    } else if (argc != 0) {
        return XConsoleResult_InvalidArgument;
    }
    for (i = 0; i < count; ++i) {
        const char* text = XConsoleShell_historyAt(shell, i);
        int written = snprintf(number, sizeof(number), "%4zu ", i + 1u);
        if (!text || written < 0 || (size_t)written >= sizeof(number) ||
            !XConsoleShell_write(shell, number, (size_t)written) ||
            !XConsoleShell_writeUtf8(shell, text) ||
            !XConsoleShell_writeUtf8(shell, "\n"))
            return XConsoleResult_IoError;
    }
    return XConsoleResult_Ok;
}

static const XConsoleCommand g_historyCommand = {
    "history", NULL, "列出或管理历史命令", "history [-c|-d N] [N]", 0, 2,
    XConsoleCommandFlag_None, xcs_history, NULL, 0, NULL
};
#endif

#if XCONSOLE_SHELL_STATS_ON
static int xcs_stats(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    char line[192];
    int written;
    (void)session;
    (void)argc;
    (void)argv;
    (void)userData;
    if (!shell) return XConsoleResult_InvalidArgument;
    written = snprintf(line, sizeof(line),
                       "行数=%llu\n成功=%llu\n失败=%llu\n输入=%llu\n输出=%llu\n命令数=%zu\n",
                       (unsigned long long)shell->m_processedLines,
                       (unsigned long long)shell->m_successfulCommands,
                       (unsigned long long)shell->m_failedCommands,
                       (unsigned long long)shell->m_inputBytes,
                       (unsigned long long)shell->m_outputBytes,
                       shell->m_commandCount);
    return written > 0 && (size_t)written < sizeof(line) &&
           XConsoleShell_write(shell, line, (size_t)written)
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static const XConsoleCommand g_statsCommand = {
    "stats", NULL, "显示 Shell 运行统计", "stats", 0, 0,
    XConsoleCommandFlag_None, xcs_stats, NULL, 0, NULL
};
#endif

#if XCONSOLE_SHELL_SCRIPT_ON && XCONSOLE_SHELL_FILESYSTEM_ON
static int xcs_source(XConsoleShell* shell, XConsoleShellSession* session,
                      int argc, const char* const* argv, void* userData)
{
    XString* rawPath = NULL;
    XString* path = NULL;
    XFd fd = XFD_INVALID;
    char line[XCONSOLE_SHELL_LINE_BUFFER_SIZE];
    size_t lineLength = 0;
    int error = 0;
    int result = XConsoleResult_Ok;
    uint8_t bytes[64];
    (void)userData;
    if (!shell || !session || argc != 1 || !argv[0] || shell->m_scriptDepth >= 4u)
        return argc == 1 ? XConsoleResult_ResourceLimit : XConsoleResult_InvalidArgument;
    rawPath = XString_create();
    path = XString_create();
    if (!rawPath || !path) result = XConsoleResult_Failed;
    if (result == XConsoleResult_Ok) {
        if (argv[0][0] == '/') {
            if (!XString_assign_utf8(rawPath, argv[0])) result = XConsoleResult_Failed;
        } else if (!XString_assign_utf8(rawPath, session->currentPath) ||
                   !XString_append_utf8(rawPath, "/") ||
                   !XString_append_utf8(rawPath, argv[0])) {
            result = XConsoleResult_Failed;
        }
    }
    if (result == XConsoleResult_Ok &&
        !XFileSystem_resolvePath(rawPath, path, XPathStyle_Absolute))
        result = XConsoleResult_Failed;
    if (result == XConsoleResult_Ok) {
        fd = XFileSystem_open(path, XFileSystem_ReadOnly, &error);
        if (fd == XFD_INVALID) result = XConsoleResult_Failed;
    }
    if (result == XConsoleResult_Ok) {
        ++shell->m_scriptDepth;
        for (;;) {
            int64_t count = XFileSystem_read(fd, bytes, sizeof(bytes));
            size_t i;
            if (count < 0) {
                result = XConsoleResult_IoError;
                break;
            }
            if (count == 0) break;
            for (i = 0; i < (size_t)count; ++i) {
                if (bytes[i] == '\r') continue;
                if (bytes[i] == '\n') {
                    if (lineLength) {
                        line[lineLength] = '\0';
                        result = XConsoleShell_processLine(shell, line, lineLength);
                        lineLength = 0;
                        if (result < 0) break;
                    }
                    continue;
                }
                if (lineLength + 1u >= sizeof(line)) {
                    result = XConsoleResult_ResourceLimit;
                    break;
                }
                line[lineLength++] = (char)bytes[i];
            }
            if (result < 0) break;
        }
        if (result == XConsoleResult_Ok && lineLength) {
            line[lineLength] = '\0';
            result = XConsoleShell_processLine(shell, line, lineLength);
        }
        --shell->m_scriptDepth;
    }
    if (fd != XFD_INVALID) XFileSystem_close(fd);
    if (rawPath) XString_delete_base(rawPath);
    if (path) XString_delete_base(path);
    return result;
}

static const XConsoleCommand g_sourceCommand = {
    "source", NULL, "逐行执行 Shell 脚本文件", "source <file>", 1, 1,
    XConsoleCommandFlag_Dangerous, xcs_source, NULL, 0, NULL
};
#endif

static XConsoleResult xcs_finish_command(XConsoleShell* self,
                                         const XConsoleCommand* command,
                                         XConsoleResult result)
{
#if XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON
    /* 参数数量或格式错误时直接给出用法，避免异步控制台只显示下一次提示符。 */
    if (result == XConsoleResult_InvalidArgument && self && command &&
        command->usage) {
        (void)XConsoleShell_writeUtf8(self, "用法: ");
        (void)XConsoleShell_writeUtf8(self, command->usage);
        (void)XConsoleShell_writeUtf8(self, "\n");
    }
    if (result == XConsoleResult_PermissionDenied && self && command &&
        !(command->flags & XConsoleCommandFlag_Sensitive))
        (void)XConsoleShell_writeError(self, result, NULL);
#endif
#if XCONSOLE_SHELL_STATS_ON
    if (!self) return result;
    if (result < 0) ++self->m_failedCommands;
    else ++self->m_successfulCommands;
#endif
#if XCONSOLE_SHELL_AUDIT_ON
    if (self && self->m_io.audit)
        self->m_io.audit(self->m_io.userData, &self->m_session, command, result);
#endif
#if !XCONSOLE_SHELL_STATS_ON && !XCONSOLE_SHELL_AUDIT_ON
    (void)self;
    (void)command;
#endif
    return result;
}

#if XCONSOLE_SHELL_COMPLETION_ON
static void xcs_complete_root_command(XConsoleShell* self)
{
    const XConsoleCommand* match = NULL;
    size_t i;
    if (!self || self->m_lineLength == 0 ||
        memchr(self->m_lineBuffer, ' ', self->m_lineLength) ||
        memchr(self->m_lineBuffer, '\t', self->m_lineLength)) return;
    for (i = 0; i < self->m_commandCount; ++i) {
        const XConsoleCommand* command = self->m_commands[i];
        if (!command || !command->name ||
            strncmp(command->name, self->m_lineBuffer, self->m_lineLength) != 0)
            continue;
        if (match) return;
        match = command;
    }
    if (!match || strlen(match->name) >= sizeof(self->m_lineBuffer)) return;
    strcpy(self->m_lineBuffer, match->name);
    self->m_lineLength = strlen(match->name);
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    self->m_lineCursor = self->m_lineLength;
#endif
}
#endif

static bool xcs_write_literal(XConsoleShell* shell, const char* text)
{
    return text && XConsoleShell_write(shell, text, strlen(text));
}

#if XCONSOLE_SHELL_LOGIN_ON
/* 登录、改密和用户管理命令包含密码参数，不能写入历史缓冲。 */
static bool xcs_is_sensitive_line(const char* line, size_t length)
{
    char command[16];
    size_t i = 0;
    size_t count = 0;
    if (!line) return false;
    while (i < length && (line[i] == ' ' || line[i] == '\t')) ++i;
    while (i < length && line[i] != ' ' && line[i] != '\t' &&
           count + 1u < sizeof(command))
        command[count++] = line[i++];
    command[count] = '\0';
    return strcmp(command, "login") == 0 || strcmp(command, "useradd") == 0 ||
           strcmp(command, "usermod") == 0 || strcmp(command, "passwd") == 0 ||
           strcmp(command, "password") == 0;
}
#endif

#if XCONSOLE_SHELL_HELP_ON
static int xcs_help(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    size_t i;
    (void)session;
    (void)userData;
    if (!shell) return XConsoleResult_InvalidArgument;
    if (argc == 0) {
        size_t nameWidth = 4u;
        static const char spaces[] = "                                ";
        /* 先计算最长命令名，避免不同长度的命令在串口终端错列。 */
        for (i = 0; i < shell->m_commandCount; ++i) {
            const XConsoleCommand* command = shell->m_commands[i];
            size_t length;
            if (!command || (command->flags & XConsoleCommandFlag_Hidden) || !command->name)
                continue;
            length = strlen(command->name);
            if (length > nameWidth) nameWidth = length;
        }
        for (i = 0; i < shell->m_commandCount; ++i) {
            const XConsoleCommand* command = shell->m_commands[i];
            const char* name;
            size_t length;
            if (!command || (command->flags & XConsoleCommandFlag_Hidden)) continue;
            name = command->name ? command->name : "";
            length = strlen(name);
            if (!xcs_write_literal(shell, name)) return XConsoleResult_IoError;
            while (length < nameWidth) {
                size_t padding = nameWidth - length;
                size_t chunk = padding < sizeof(spaces) - 1u ? padding : sizeof(spaces) - 1u;
                if (!XConsoleShell_write(shell, spaces, chunk)) return XConsoleResult_IoError;
                length += chunk;
            }
            if (!xcs_write_literal(shell, "  ") ||
                !xcs_write_literal(shell, command->description ? command->description : "") ||
                !xcs_write_literal(shell, "\n")) return XConsoleResult_IoError;
        }
        return XConsoleResult_Ok;
    }
    {
        const XConsoleCommand* command = NULL;
        if (!XConsoleShell_findCommand(shell, argv[0], &command) || !command)
            return XConsoleResult_UnknownCommand;
        if (!xcs_write_literal(shell, command->usage ? command->usage : command->name) ||
            !xcs_write_literal(shell, "\n") ||
            !xcs_write_literal(shell, command->description ? command->description : "") ||
            !xcs_write_literal(shell, "\n")) return XConsoleResult_IoError;
    }
    return XConsoleResult_Ok;
}

static const XConsoleCommand g_helpCommand = {
    "help", "?", "列出命令或显示命令用法", "help [command]", 0, 1,
    XConsoleCommandFlag_AllowUnauthenticated, xcs_help, NULL, 0, NULL
};
#endif /* XCONSOLE_SHELL_HELP_ON */

static int xcs_version(XConsoleShell* shell, XConsoleShellSession* session,
                       int argc, const char* const* argv, void* userData)
{
    (void)session;
    (void)argc;
    (void)argv;
    (void)userData;
    return xcs_write_literal(shell, "XinYueC XConsoleShell\n")
               ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xcs_echo(XConsoleShell* shell, XConsoleShellSession* session,
                    int argc, const char* const* argv, void* userData)
{
    int i;
    int start = 0;
    bool newline = true;
    bool interpretEscape = false;
    (void)session;
    (void)userData;
    while (start < argc && argv[start][0] == '-' && argv[start][1]) {
        if (strcmp(argv[start], "-n") == 0) {
            newline = false;
            ++start;
        } else if (strcmp(argv[start], "-e") == 0) {
            interpretEscape = true;
            ++start;
        } else if (strcmp(argv[start], "-E") == 0) {
            interpretEscape = false;
            ++start;
        } else {
            break;
        }
    }
    for (i = start; i < argc; ++i) {
        const char* text = argv[i];
        if (i > start && !xcs_write_literal(shell, " ")) return XConsoleResult_IoError;
        if (interpretEscape) {
            size_t j;
            for (j = 0; text[j]; ++j) {
                if (text[j] == '\\') {
                    char ch = text[j + 1];
                    if (ch == 'n') {
                        if (!xcs_write_literal(shell, "\n")) return XConsoleResult_IoError;
                        ++j;
                    } else if (ch == 't') {
                        if (!xcs_write_literal(shell, "\t")) return XConsoleResult_IoError;
                        ++j;
                    } else if (ch == 'r') {
                        if (!xcs_write_literal(shell, "\r")) return XConsoleResult_IoError;
                        ++j;
                    } else {
                        if (!xcs_write_literal(shell, "\\")) return XConsoleResult_IoError;
                    }
                } else if (!XConsoleShell_write(shell, text + j, 1)) {
                    return XConsoleResult_IoError;
                }
            }
        } else if (!xcs_write_literal(shell, text)) {
            return XConsoleResult_IoError;
        }
    }
    return newline && !xcs_write_literal(shell, "\n")
               ? XConsoleResult_IoError : XConsoleResult_Ok;
}

static const XConsoleCommand g_versionCommand = {
    "version", NULL, "显示库版本", "version", 0, 0,
    XConsoleCommandFlag_AllowUnauthenticated, xcs_version, NULL, 0, NULL
};
static const XConsoleCommand g_echoCommand = {
    "echo", NULL, "回显参数", "echo [-neE] <text...>", 0, -1,
    XConsoleCommandFlag_None, xcs_echo, NULL, 0, NULL
};
XVtable* XConsoleShell_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XConsoleShell);
    XCLASS_SET_CLASS_NAME_DEFAULT("XConsoleShell");
    XVTABLE_INHERIT_XCLASS(XObject);
#if XCONSOLE_SHELL_ASYNC_ON
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXConsoleShell_event);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXConsoleShell_timerEvent);
#endif
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXConsoleShell_deinit);
    return XVTABLE_DEFAULT;
}

static void xcs_copy_io(XConsoleShellIo* target, const XConsoleShellIo* source)
{
    if (!target) return;
    memset(target, 0, sizeof(*target));
#if XCONSOLE_SHELL_TRANSPORT_CALLBACK_ON || XCONSOLE_SHELL_XIODEVICE_BACKEND_ON
    if (source) *target = *source;
#else
    /* 关闭原始回调传输时，仅保留命令解析能力，避免误接入产品回调。 */
    (void)source;
#endif
}

#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
/** @brief 动态命令的私有所有者，负责描述字符串和子命令树的深复制。 */
typedef struct XConsoleShellDynamicCommand {
    XConsoleCommand command;                         /**< 暴露给解析器的命令描述。 */
    XConsoleCommand* subcommands;                    /**< 子命令描述副本数组。 */
    struct XConsoleShellDynamicCommand** children;   /**< 子命令所有者数组。 */
    struct XConsoleShellDynamicCommand* nextRetired; /**< 延迟释放链表中的下一个节点。 */
} XConsoleShellDynamicCommand;

static void xcs_dynamic_destroy(XConsoleShellDynamicCommand* entry)
{
    size_t i;
    if (!entry) return;
    for (i = 0; i < entry->command.subcommandCount; ++i)
        xcs_dynamic_destroy(entry->children ? entry->children[i] : NULL);
    if (entry->children) XFree_System(entry->children);
    if (entry->subcommands) XFree_System(entry->subcommands);
    if (entry->command.name) XFree_System((void*)entry->command.name);
    if (entry->command.aliases) XFree_System((void*)entry->command.aliases);
    if (entry->command.description) XFree_System((void*)entry->command.description);
    if (entry->command.usage) XFree_System((void*)entry->command.usage);
    XFree_System(entry);
}

static void xcs_collect_retired_commands(XConsoleShell* self)
{
    XConsoleShellDynamicCommand* entry;
    XConsoleShellDynamicCommand* next;
    if (!self || self->m_commandExecutionDepth != 0) return;
    entry = self->m_retiredDynamicCommands;
    self->m_retiredDynamicCommands = NULL;
    while (entry) {
        next = entry->nextRetired;
        entry->nextRetired = NULL;
        xcs_dynamic_destroy(entry);
        entry = next;
    }
}

static XConsoleShellDynamicCommand* xcs_dynamic_clone(const XConsoleCommand* source,
                                                       size_t depth)
{
    XConsoleShellDynamicCommand* entry;
    size_t i;
    if (!source || !source->name || !source->name[0] || depth > 8u ||
        (source->subcommandCount && !source->subcommands)) return NULL;
    entry = (XConsoleShellDynamicCommand*)XCalloc_System(1, sizeof(*entry));
    if (!entry) return NULL;
    entry->command = *source;
    entry->command.name = XMemory_strdup(source->name);
    entry->command.aliases = source->aliases ? XMemory_strdup(source->aliases) : NULL;
    entry->command.description = source->description ? XMemory_strdup(source->description) : NULL;
    entry->command.usage = source->usage ? XMemory_strdup(source->usage) : NULL;
    if (!entry->command.name || (source->aliases && !entry->command.aliases) ||
        (source->description && !entry->command.description) ||
        (source->usage && !entry->command.usage)) {
        xcs_dynamic_destroy(entry);
        return NULL;
    }
    if (!source->subcommandCount) return entry;
    entry->subcommands = (XConsoleCommand*)XCalloc_System(source->subcommandCount,
                                                            sizeof(*entry->subcommands));
    entry->children = (XConsoleShellDynamicCommand**)XCalloc_System(
        source->subcommandCount, sizeof(*entry->children));
    if (!entry->subcommands || !entry->children) {
        xcs_dynamic_destroy(entry);
        return NULL;
    }
    for (i = 0; i < source->subcommandCount; ++i) {
        entry->children[i] = xcs_dynamic_clone(&source->subcommands[i], depth + 1u);
        if (!entry->children[i]) {
            xcs_dynamic_destroy(entry);
            return NULL;
        }
        entry->subcommands[i] = entry->children[i]->command;
    }
    entry->command.subcommands = entry->subcommands;
    return entry;
}
#endif

/* 内置命令需要在 CALLBACK_COMMAND 关闭时继续可用，因此不走公共注册 API。 */
static bool xcs_register_static_commands(XConsoleShell* self,
                                         const XConsoleCommand* commands,
                                         size_t count)
{
    size_t i;
    size_t j;
    if (!self || (!commands && count)) return false;
    if (count > XCONSOLE_SHELL_COMMAND_CAPACITY - self->m_commandCount)
        return false;
    for (i = 0; i < count; ++i) {
        if (!commands[i].name || !commands[i].name[0]) return false;
        for (j = 0; j < self->m_commandCount; ++j) {
            if (self->m_commands[j] && self->m_commands[j]->name &&
                strcmp(self->m_commands[j]->name, commands[i].name) == 0)
                return false;
        }
        for (j = 0; j < i; ++j) {
            if (strcmp(commands[j].name, commands[i].name) == 0) return false;
        }
    }
    for (i = 0; i < count; ++i) {
        self->m_commands[self->m_commandCount++] = &commands[i];
    }
    return true;
}

static bool xcs_unregister_static_commands(XConsoleShell* self,
                                           const XConsoleCommand* commands,
                                           size_t count)
{
    size_t i;
    size_t j;
    if (!self || (!commands && count)) return false;
    for (i = 0; i < count; ++i) {
        bool found = false;
        for (j = 0; j < i; ++j) {
            if (&commands[i] == &commands[j]) return false;
        }
        for (j = 0; j < self->m_commandCount; ++j) {
            if (self->m_commands[j] == &commands[i]) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    for (i = 0; i < count; ++i) {
        for (j = 0; j < self->m_commandCount; ++j) {
            if (self->m_commands[j] == &commands[i]) {
                memmove(&self->m_commands[j], &self->m_commands[j + 1],
                        (self->m_commandCount - j - 1u) *
                            sizeof(self->m_commands[0]));
                --self->m_commandCount;
                break;
            }
        }
    }
    return true;
}

void XConsoleShell_init(XConsoleShell* self, const XConsoleShellIo* io)
{
    bool registered = true;
    if (!self) return;
    XObject_init(&self->base);
    XClassGetVtable(self) = XConsoleShell_class_init();
    memset(self->m_commands, 0, sizeof(self->m_commands));
    xcs_copy_io(&self->m_io, io);
    self->m_commandCount = 0;
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
    memset(self->m_dynamicCommands, 0, sizeof(self->m_dynamicCommands));
    self->m_dynamicCommandCount = 0;
    self->m_retiredDynamicCommands = NULL;
    self->m_commandExecutionDepth = 0;
#endif
#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
    memset(self->m_asyncProcesses, 0, sizeof(self->m_asyncProcesses));
#endif
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    memset(self->m_sessions, 0, sizeof(self->m_sessions));
    self->m_sessionCount = 1;
    self->m_nextSessionId = 2;
#endif
    memset(&self->m_session, 0, sizeof(self->m_session));
    self->m_session.id = 1;
    self->m_session.permissionMask = UINT32_MAX;
    self->m_session.authenticated = XCONSOLE_SHELL_AUTH_ON ? false : true;
#if XCONSOLE_SHELL_LOGIN_ON
    self->m_session.uid = UINT32_MAX;
    self->m_session.gid = UINT32_MAX;
    self->m_session.groupCount = 0;
    self->m_loginDatabasePath[0] = '\0';
    strncpy(self->m_loginDatabasePath, XCONSOLE_SHELL_LOGIN_CONFIG_PATH,
            sizeof(self->m_loginDatabasePath) - 1u);
    self->m_loginDatabasePath[sizeof(self->m_loginDatabasePath) - 1u] = '\0';
#endif
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    self->m_session.m_io = self->m_io;
    self->m_session.m_open = true;
#endif
    self->m_session.currentPath[0] = '/';
    self->m_session.currentPath[1] = '\0';
#if XCONSOLE_SHELL_FS_CD_ON
    self->m_session.previousPath[0] = '\0';
#endif
#if XCONSOLE_SHELL_FILESYSTEM_ON
    {
        XString* current = XString_create();
        if (current && XFileSystem_getSpecialPath(XSpecialPath_Current, current) &&
            XString_size_base(current) < sizeof(self->m_session.currentPath)) {
            strncpy(self->m_session.currentPath, XString_toUtf8(current),
                    sizeof(self->m_session.currentPath) - 1);
            self->m_session.currentPath[sizeof(self->m_session.currentPath) - 1] = '\0';
        }
        if (current) XString_delete_base(current);
    }
#endif
    self->m_lineLength = 0;
    self->m_argumentCount = 0;
    self->m_discardLine = false;
#if XCONSOLE_SHELL_HISTORY_ON
    memset(self->m_history, 0, sizeof(self->m_history));
    self->m_historyCount = 0;
    self->m_historyNext = 0;
    self->m_historyCursor = 0;
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    self->m_lineCursor = 0;
    self->m_escapeState = 0;
#endif
#if XCONSOLE_SHELL_STATS_ON
    self->m_processedLines = 0;
    self->m_successfulCommands = 0;
    self->m_failedCommands = 0;
    self->m_inputBytes = 0;
    self->m_outputBytes = 0;
#endif
#if XCONSOLE_SHELL_SCRIPT_ON
    self->m_scriptDepth = 0;
#endif
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    self->m_asyncOutputHead = 0;
    self->m_asyncOutputTail = 0;
    self->m_asyncOutputSize = 0;
#endif
    self->m_running = true;
#if XCONSOLE_SHELL_ASYNC_ON
    XAtomic_init(self->m_asyncRunning, false);
    XAtomic_init(self->m_asyncInputPosted, false);
    if (g_xcs_async_input_event == XEVENT_TYPE_NONE)
        g_xcs_async_input_event = (XEventType)XEvent_registerEventType(-1);
    self->m_asyncEventType = g_xcs_async_input_event;
    self->m_asyncOwnerThread = XThread_currentThread();
    self->m_asyncThread = NULL;
    XAtomic_init(self->m_asyncWorkerReady, false);
    self->m_asyncLastReadBytes = 0;
    self->m_asyncPollTimer = XTIMER_INVALID_ID;
    XAtomic_init(self->m_asyncInputAttached, false);
#endif
#if XCONSOLE_SHELL_HELP_ON
    registered = xcs_register_static_commands(self, &g_helpCommand, 1) && registered;
#endif
    registered = xcs_register_static_commands(self, &g_versionCommand, 1) && registered;
    registered = xcs_register_static_commands(self, &g_echoCommand, 1) && registered;
#if XCONSOLE_SHELL_CLEAR_ON
    registered = xcs_register_static_commands(
        self, &XConsoleShellClear_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_RESET_ON
    registered = xcs_register_static_commands(
        self, &XConsoleShellReset_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_REBOOT_ON
    registered = xcs_register_static_commands(
        self, &XConsoleShellReboot_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_SHUTDOWN_ON
    registered = xcs_register_static_commands(
        self, &XConsoleShellShutdown_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_EXIT_ON
    registered = xcs_register_static_commands(
        self, &XConsoleShellExit_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_LOGIN_ON && XCONSOLE_SHELL_USER_COMMANDS_ON
    registered = XConsoleShellLogin_registerCommands(self) && registered;
#endif
#if XCONSOLE_SHELL_HISTORY_ON
    registered = xcs_register_static_commands(self, &g_historyCommand, 1) && registered;
#endif
#if XCONSOLE_SHELL_STATS_ON
    registered = xcs_register_static_commands(self, &g_statsCommand, 1) && registered;
#endif
#if XCONSOLE_SHELL_SCRIPT_ON && XCONSOLE_SHELL_FILESYSTEM_ON
    registered = xcs_register_static_commands(self, &g_sourceCommand, 1) && registered;
#endif
#if XCONSOLE_SHELL_FILESYSTEM_ON
    registered = xcs_register_static_commands(self, &XConsoleShellFileSystem_command, 1) && registered;
#if XCONSOLE_SHELL_FS_LS_ON
    registered = xcs_register_static_commands(self, &XConsoleShellFileSystem_ls_command, 1) && registered;
#endif
    registered = XConsoleShellFileSystem_registerRootCommands(self) && registered;
#endif
#if XCONSOLE_SHELL_NETWORK_ON
    registered = xcs_register_static_commands(self, &XConsoleShellNetwork_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_DATETIME_ON && XCONSOLE_SHELL_DATE_ON
    registered = xcs_register_static_commands(self, &XConsoleShellDateTime_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_MEMORY_ON && XCONSOLE_SHELL_MEMORY_POOL_ON
    registered = xcs_register_static_commands(self, &XConsoleShellMemory_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_INFO_ON
    registered = xcs_register_static_commands(self, &XConsoleShellInfo_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_UPTIME_ON
    self->m_startTimeMsecs = XDateTime_currentMSecsSinceEpoch();
    registered = xcs_register_static_commands(self, &XConsoleShellUptime_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_TASKS_ON
    self->m_taskProvider = XConsoleShellTasks_platformProvider;
    self->m_taskProviderUserData = NULL;
    registered = xcs_register_static_commands(self, &XConsoleShellTasks_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_GPIO_ON
    memset(self->m_gpioSlots, 0, sizeof(self->m_gpioSlots));
    self->m_gpioAuthorize = NULL;
    self->m_gpioAuthorizeUserData = NULL;
    registered = xcs_register_static_commands(
        self, &XConsoleShellGpio_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_ADC_ON
    memset(self->m_adcSlots, 0, sizeof(self->m_adcSlots));
    self->m_adcAuthorize = NULL;
    self->m_adcAuthorizeUserData = NULL;
    registered = xcs_register_static_commands(
        self, &XConsoleShellAdc_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_PWM_ON
    memset(self->m_pwmSlots, 0, sizeof(self->m_pwmSlots));
    self->m_pwmAuthorize = NULL;
    self->m_pwmAuthorizeUserData = NULL;
    registered = xcs_register_static_commands(
        self, &XConsoleShellPwm_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_I2C_ON
    memset(self->m_i2cSlots, 0, sizeof(self->m_i2cSlots));
    self->m_i2cAuthorize = NULL;
    self->m_i2cAuthorizeUserData = NULL;
    registered = xcs_register_static_commands(
        self, &XConsoleShellI2c_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_SPI_ON
    memset(self->m_spiSlots, 0, sizeof(self->m_spiSlots));
    self->m_spiAuthorize = NULL;
    self->m_spiAuthorizeUserData = NULL;
    registered = xcs_register_static_commands(
        self, &XConsoleShellSpi_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_CAN_ON
    memset(self->m_canSlots, 0, sizeof(self->m_canSlots));
    self->m_canAuthorize = NULL;
    self->m_canAuthorizeUserData = NULL;
    registered = xcs_register_static_commands(
        self, &XConsoleShellCan_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_EDITOR_ON
    registered = xcs_register_static_commands(
        self, &XConsoleShellVi_command, 1) && registered;
    registered = xcs_register_static_commands(
        self, &XConsoleShellVim_command, 1) && registered;
#endif
#if XCONSOLE_SHELL_EXECUTOR_ON && XCONSOLE_SHELL_EXTERNAL_PROCESS_ON
    registered = xcs_register_static_commands(self, &XConsoleShellExecutor_command, 1) && registered;
#endif
    if (!registered) {
        /* 内置命令表初始化失败时不暴露半成品命令集合。 */
        memset(self->m_commands, 0, sizeof(self->m_commands));
        self->m_commandCount = 0;
        self->m_running = false;
    }
#if XCONSOLE_SHELL_ASYNC_ON && XCONSOLE_SHELL_ASYNC_AUTO_START_ON
    if (registered && self->m_io.inputAttach)
        (void)XConsoleShell_startAsync(self);
#endif
}

XConsoleShell* XConsoleShell_create(const XConsoleShellIo* io)
{
    XConsoleShell* self = (XConsoleShell*)XMalloc_System(sizeof(*self));
    if (!self) return NULL;
    XConsoleShell_init(self, io);
    Set_Class_MemoryFree(self, XFree_System);
    return self;
}

static void VXConsoleShell_deinit(XObject* object)
{
    XConsoleShell* self = (XConsoleShell*)object;
    size_t i;
    if (!self) return;
#if XCONSOLE_SHELL_ASYNC_ON
    (void)XConsoleShell_stopAsync(self, 0);
    if (self->m_asyncEventType != XEVENT_TYPE_NONE)
        XCoreApplication_removePostedEvents(object, self->m_asyncEventType);
#endif
#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
    XConsoleShellExecutor_abortAsync(self);
#endif
#if XCONSOLE_SHELL_GPIO_ON
    XConsoleShellGpio_deinit(self);
#endif
#if XCONSOLE_SHELL_ADC_ON
    XConsoleShellAdc_deinit(self);
#endif
#if XCONSOLE_SHELL_PWM_ON
    XConsoleShellPwm_deinit(self);
#endif
#if XCONSOLE_SHELL_I2C_ON
    XConsoleShellI2c_deinit(self);
#endif
#if XCONSOLE_SHELL_SPI_ON
    XConsoleShellSpi_deinit(self);
#endif
#if XCONSOLE_SHELL_CAN_ON
    XConsoleShellCan_deinit(self);
#endif
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
    for (i = 0; i < self->m_dynamicCommandCount; ++i)
        xcs_dynamic_destroy(self->m_dynamicCommands[i]);
    while (self->m_retiredDynamicCommands) {
        XConsoleShellDynamicCommand* entry = self->m_retiredDynamicCommands;
        self->m_retiredDynamicCommands = entry->nextRetired;
        entry->nextRetired = NULL;
        xcs_dynamic_destroy(entry);
    }
    memset(self->m_dynamicCommands, 0, sizeof(self->m_dynamicCommands));
    self->m_dynamicCommandCount = 0;
#else
    (void)i;
#endif
    memset(self->m_commands, 0, sizeof(self->m_commands));
    self->m_commandCount = 0;
#if XCONSOLE_SHELL_TASKS_ON
    self->m_taskProvider = NULL;
    self->m_taskProviderUserData = NULL;
#endif
    self->m_running = false;
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    memset(self->m_sessions, 0, sizeof(self->m_sessions));
    self->m_sessionCount = 0;
#endif
#if XCONSOLE_SHELL_HISTORY_ON
    memset(self->m_history, 0, sizeof(self->m_history));
    self->m_historyCount = 0;
    self->m_historyNext = 0;
    self->m_historyCursor = 0;
#endif
#if XCONSOLE_SHELL_SCRIPT_ON
    self->m_scriptDepth = 0;
#endif
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    memset(self->m_asyncOutput, 0, sizeof(self->m_asyncOutput));
    self->m_asyncOutputHead = 0;
    self->m_asyncOutputTail = 0;
    self->m_asyncOutputSize = 0;
#endif
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XConsoleShell_deinit_base(XConsoleShell* self)
{
    if (self) XClass_deinit_base((XClass*)self);
}

void XConsoleShell_delete_base(XConsoleShell* self)
{
    if (self) XClass_delete_base((XClass*)self);
}

#if XCONSOLE_SHELL_TASKS_ON
bool XConsoleShell_setTaskProvider(XConsoleShell* self,
                                   XConsoleShellTaskProviderFn provider,
                                   void* userData)
{
    if (!self) return false;
    self->m_taskProvider = provider;
    self->m_taskProviderUserData = provider ? userData : NULL;
    return true;
}
#endif

bool XConsoleShell_registerStaticCommands(XConsoleShell* self,
                                          const XConsoleCommand* commands,
                                          size_t count)
{
#if XCONSOLE_SHELL_CALLBACK_COMMAND_ON
    return xcs_register_static_commands(self, commands, count);
#else
    (void)self;
    (void)commands;
    (void)count;
    return false;
#endif
}

bool XConsoleShell_unregisterStaticCommands(XConsoleShell* self,
                                            const XConsoleCommand* commands,
                                            size_t count)
{
#if XCONSOLE_SHELL_CALLBACK_COMMAND_ON
    return xcs_unregister_static_commands(self, commands, count);
#else
    (void)self;
    (void)commands;
    (void)count;
    return false;
#endif
}

#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
bool XConsoleShell_registerCommand(XConsoleShell* self, const XConsoleCommand* command)
{
    XConsoleShellDynamicCommand* entry;
    size_t i;
    if (!self || !command || !command->name || !command->name[0] ||
        self->m_dynamicCommandCount >= XCONSOLE_SHELL_DYNAMIC_COMMAND_CAPACITY)
        return false;
    for (i = 0; i < self->m_commandCount; ++i) {
        if (self->m_commands[i] && self->m_commands[i]->name &&
            strcmp(self->m_commands[i]->name, command->name) == 0)
            return false;
    }
    entry = xcs_dynamic_clone(command, 0);
    if (!entry || !xcs_register_static_commands(self, &entry->command, 1)) {
        xcs_dynamic_destroy(entry);
        return false;
    }
    self->m_dynamicCommands[self->m_dynamicCommandCount++] = entry;
    return true;
}

bool XConsoleShell_unregisterCommand(XConsoleShell* self, const char* name)
{
    size_t i;
    if (!self || !name || !name[0]) return false;
    for (i = 0; i < self->m_dynamicCommandCount; ++i) {
        XConsoleShellDynamicCommand* entry = self->m_dynamicCommands[i];
        if (!entry || !entry->command.name || strcmp(entry->command.name, name) != 0)
            continue;
        if (!xcs_unregister_static_commands(self, &entry->command, 1)) return false;
        memmove(&self->m_dynamicCommands[i], &self->m_dynamicCommands[i + 1],
                (self->m_dynamicCommandCount - i - 1u) *
                    sizeof(self->m_dynamicCommands[0]));
        --self->m_dynamicCommandCount;
        self->m_dynamicCommands[self->m_dynamicCommandCount] = NULL;
        if (self->m_commandExecutionDepth) {
            entry->nextRetired = self->m_retiredDynamicCommands;
            self->m_retiredDynamicCommands = entry;
        } else {
            xcs_dynamic_destroy(entry);
        }
        return true;
    }
    return false;
}
#endif

#if XCONSOLE_SHELL_MULTI_SESSION_ON
/** @brief 临时保存主工作区，以便串行执行附加会话而不改变既有解析器实现。 */
typedef struct XConsoleShellSessionSwap {
    XConsoleShellIo io;                              /**< 切换前的传输回调。 */
    XConsoleShellSession session;                    /**< 切换前的会话状态。 */
    char lineBuffer[XCONSOLE_SHELL_LINE_BUFFER_SIZE]; /**< 切换前的输入行缓冲。 */
    size_t lineLength;                               /**< 切换前的输入长度。 */
    bool discardLine;                                /**< 切换前的超长行丢弃状态。 */
#if XCONSOLE_SHELL_HISTORY_ON
    char history[XCONSOLE_SHELL_HISTORY_CAPACITY][XCONSOLE_SHELL_LINE_BUFFER_SIZE]; /**< 切换前的历史。 */
    size_t historyCount;                             /**< 切换前的历史数量。 */
    size_t historyNext;                              /**< 切换前的历史写入位置。 */
    size_t historyCursor;                            /**< 切换前的历史浏览位置。 */
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    size_t lineCursor;                               /**< 切换前的行编辑光标。 */
    uint8_t escapeState;                             /**< 切换前的转义状态。 */
#endif
} XConsoleShellSessionSwap;

static bool xcs_is_session(const XConsoleShell* self,
                           const XConsoleShellSession* session)
{
    size_t i;
    if (!self || !session) return false;
    if (session == &self->m_session) return true;
    for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
        if (session == &self->m_sessions[i] && self->m_sessions[i].m_open)
            return true;
    }
    return false;
}

static bool xcs_enter_session(XConsoleShell* self, XConsoleShellSession* session,
                              XConsoleShellSessionSwap* swap)
{
    if (!self || !session || !swap) return false;
    if (session == &self->m_session) return true;
    swap->io = self->m_io;
    swap->session = self->m_session;
    memcpy(swap->lineBuffer, self->m_lineBuffer, sizeof(swap->lineBuffer));
    swap->lineLength = self->m_lineLength;
    swap->discardLine = self->m_discardLine;
#if XCONSOLE_SHELL_HISTORY_ON
    memcpy(swap->history, self->m_history, sizeof(swap->history));
    swap->historyCount = self->m_historyCount;
    swap->historyNext = self->m_historyNext;
    swap->historyCursor = self->m_historyCursor;
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    swap->lineCursor = self->m_lineCursor;
    swap->escapeState = self->m_escapeState;
#endif
    self->m_io = session->m_io;
    self->m_session = *session;
    memcpy(self->m_lineBuffer, session->m_lineBuffer, sizeof(self->m_lineBuffer));
    self->m_lineLength = session->m_lineLength;
    self->m_discardLine = session->m_discardLine;
#if XCONSOLE_SHELL_HISTORY_ON
    memcpy(self->m_history, session->m_history, sizeof(self->m_history));
    self->m_historyCount = session->m_historyCount;
    self->m_historyNext = session->m_historyNext;
    self->m_historyCursor = session->m_historyCursor;
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    self->m_lineCursor = session->m_lineCursor;
    self->m_escapeState = session->m_escapeState;
#endif
    return true;
}

static void xcs_leave_session(XConsoleShell* self, XConsoleShellSession* session,
                              const XConsoleShellSessionSwap* swap)
{
    if (!self || !session || !swap || session == &self->m_session) return;
    self->m_session.m_io = self->m_io;
    *session = self->m_session;
    memcpy(session->m_lineBuffer, self->m_lineBuffer, sizeof(session->m_lineBuffer));
    session->m_lineLength = self->m_lineLength;
    session->m_discardLine = self->m_discardLine;
#if XCONSOLE_SHELL_HISTORY_ON
    memcpy(session->m_history, self->m_history, sizeof(session->m_history));
    session->m_historyCount = self->m_historyCount;
    session->m_historyNext = self->m_historyNext;
    session->m_historyCursor = self->m_historyCursor;
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    session->m_lineCursor = self->m_lineCursor;
    session->m_escapeState = self->m_escapeState;
#endif
    self->m_io = swap->io;
    self->m_session = swap->session;
    memcpy(self->m_lineBuffer, swap->lineBuffer, sizeof(self->m_lineBuffer));
    self->m_lineLength = swap->lineLength;
    self->m_discardLine = swap->discardLine;
#if XCONSOLE_SHELL_HISTORY_ON
    memcpy(self->m_history, swap->history, sizeof(self->m_history));
    self->m_historyCount = swap->historyCount;
    self->m_historyNext = swap->historyNext;
    self->m_historyCursor = swap->historyCursor;
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    self->m_lineCursor = swap->lineCursor;
    self->m_escapeState = swap->escapeState;
#endif
}

XConsoleShellSession* XConsoleShell_openSession(XConsoleShell* self,
                                                 const XConsoleShellIo* io)
{
    size_t i;
    if (!self) return NULL;
    for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
        XConsoleShellSession* session = &self->m_sessions[i];
        if (session->m_open) continue;
        memset(session, 0, sizeof(*session));
        do {
            session->id = self->m_nextSessionId++;
        } while (session->id == 0 || session->id == 1);
        session->permissionMask = UINT32_MAX;
        session->authenticated = XCONSOLE_SHELL_AUTH_ON ? false : true;
#if XCONSOLE_SHELL_LOGIN_ON
        session->uid = UINT32_MAX;
        session->gid = UINT32_MAX;
        session->groupCount = 0;
#endif
        memcpy(session->currentPath, self->m_session.currentPath,
               sizeof(session->currentPath));
#if XCONSOLE_SHELL_FS_CD_ON
        memcpy(session->previousPath, self->m_session.previousPath,
               sizeof(session->previousPath));
#endif
        xcs_copy_io(&session->m_io, io ? io : &self->m_io);
        session->m_open = true;
        ++self->m_sessionCount;
        return session;
    }
    return NULL;
}

bool XConsoleShell_closeSession(XConsoleShell* self, XConsoleShellSession* session)
{
    if (!self || !session || session == &self->m_session || !xcs_is_session(self, session))
        return false;
    memset(session, 0, sizeof(*session));
    if (self->m_sessionCount) --self->m_sessionCount;
    return true;
}

size_t XConsoleShell_sessionCount(const XConsoleShell* self)
{
    return self ? self->m_sessionCount : 0;
}

XConsoleShellSession* XConsoleShell_findSession(XConsoleShell* self, uint32_t id)
{
    size_t i;
    if (!self) return NULL;
    if (self->m_session.id == id) return &self->m_session;
    for (i = 0; i < XCONSOLE_SHELL_MAX_SESSIONS - 1u; ++i) {
        if (self->m_sessions[i].m_open && self->m_sessions[i].id == id)
            return &self->m_sessions[i];
    }
    return NULL;
}

XConsoleResult XConsoleShell_feedByteForSession(XConsoleShell* self,
                                                XConsoleShellSession* session,
                                                uint8_t byte)
{
    XConsoleShellSessionSwap swap;
    XConsoleResult result;
    if (!xcs_is_session(self, session)) return XConsoleResult_InvalidArgument;
    if (session == &self->m_session) return XConsoleShell_feedByte(self, byte);
    if (!xcs_enter_session(self, session, &swap)) return XConsoleResult_InvalidArgument;
    result = XConsoleShell_feedByte(self, byte);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    /* 即使命令返回错误，也必须在恢复主会话前排空本会话已经产生的输出。 */
    if (!XConsoleShell_flushOutput(self))
        result = XConsoleResult_IoError;
#endif
    xcs_leave_session(self, session, &swap);
    return result;
}

XConsoleResult XConsoleShell_feedDataForSession(XConsoleShell* self,
                                                XConsoleShellSession* session,
                                                const void* data, size_t size)
{
    XConsoleShellSessionSwap swap;
    XConsoleResult result;
    if (!xcs_is_session(self, session)) return XConsoleResult_InvalidArgument;
    if (session == &self->m_session) return XConsoleShell_feedData(self, data, size);
    if (!xcs_enter_session(self, session, &swap)) return XConsoleResult_InvalidArgument;
    result = XConsoleShell_feedData(self, data, size);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    if (!XConsoleShell_flushOutput(self))
        result = XConsoleResult_IoError;
#endif
    xcs_leave_session(self, session, &swap);
    return result;
}

XConsoleResult XConsoleShell_pumpForSession(XConsoleShell* self,
                                            XConsoleShellSession* session,
                                            size_t maxBytes)
{
    uint8_t buffer[128];
    int64_t count;
    XConsoleShellSessionSwap swap;
    XConsoleResult result;
    if (!xcs_is_session(self, session)) return XConsoleResult_InvalidArgument;
    if (session == &self->m_session) return XConsoleShell_pump(self, maxBytes);
    if (!session->m_io.read) return XConsoleResult_NotSupported;
    if (maxBytes == 0 || maxBytes > sizeof(buffer)) maxBytes = sizeof(buffer);
    if (!xcs_enter_session(self, session, &swap)) return XConsoleResult_InvalidArgument;
    count = self->m_io.read(self->m_io.userData, buffer, maxBytes);
    if (count < 0 || (size_t)count > maxBytes)
        result = XConsoleResult_IoError;
    else
        result = count == 0 ? XConsoleResult_Ok : XConsoleShell_feedData(self, buffer, (size_t)count);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    if (!XConsoleShell_flushOutput(self)) result = XConsoleResult_IoError;
#endif
    xcs_leave_session(self, session, &swap);
    return result;
}

XConsoleResult XConsoleShell_processLineForSession(XConsoleShell* self,
                                                   XConsoleShellSession* session,
                                                   const char* line, size_t length)
{
    XConsoleShellSessionSwap swap;
    XConsoleResult result;
    if (!xcs_is_session(self, session)) return XConsoleResult_InvalidArgument;
    if (session == &self->m_session) return XConsoleShell_processLine(self, line, length);
    if (!xcs_enter_session(self, session, &swap)) return XConsoleResult_InvalidArgument;
    result = XConsoleShell_processLine(self, line, length);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    if (!XConsoleShell_flushOutput(self))
        result = XConsoleResult_IoError;
#endif
    xcs_leave_session(self, session, &swap);
    return result;
}
#endif

bool XConsoleShell_findCommand(const XConsoleShell* self,
                               const char* path,
                               const XConsoleCommand** result)
{
    char lineBuffer[XCONSOLE_SHELL_LINE_BUFFER_SIZE];
    const char* arguments[XCONSOLE_SHELL_MAX_ARGUMENTS];
    size_t argumentCount = 0;
    XConsoleResult tokenResult;
    size_t consumed = 0;
    const XConsoleCommand* command;
    if (result) *result = NULL;
    if (!self || !path) return false;
    tokenResult = XConsoleShellParser_tokenizeBuffer(path, strlen(path),
                                                     lineBuffer, sizeof(lineBuffer),
                                                     arguments,
                                                     XCONSOLE_SHELL_MAX_ARGUMENTS,
                                                     &argumentCount);
    if (tokenResult != XConsoleResult_Ok) return false;
    command = XConsoleShellParser_find(self, arguments, argumentCount, &consumed);
    if (!command || consumed != argumentCount) return false;
    if (result) *result = command;
    return true;
}

XConsoleResult XConsoleShell_processLine(XConsoleShell* self,
                                         const char* line, size_t length)
{
    XConsoleResult tokenResult;
    const XConsoleCommand* command;
    size_t consumed = 0;
    int argc;
    int result;
    if (!self || (!line && length)) return XConsoleResult_InvalidArgument;
    if (!self->m_running) return XConsoleResult_NotSupported;
    /* 直接提交完整行意味着开始新的输入事务，清除上一条异常输入的丢弃态。 */
    self->m_discardLine = false;
#if XCONSOLE_SHELL_STATS_ON
    ++self->m_processedLines;
#endif
    self->m_lineLength = 0;
    self->m_session.cancelled = false;
#if XCONSOLE_SHELL_LOGIN_ON
    /* 密码行必须在历史、tokenizer 和普通权限判断之前分流。 */
    if (XConsoleShellLogin_isInputPending(&self->m_session)) {
        XConsoleResult pendingResult = XConsoleShellLogin_submitInput(
            self, &self->m_session, line, length);
        return xcs_finish_command(self, NULL, pendingResult);
    }
#endif
#if XCONSOLE_SHELL_EDITOR_ON
    /* vi/vim 命令模式与插入模式的后续输入行直接在编辑器状态机分流。 */
    if (XConsoleShellVi_isActive(&self->m_session)) {
        XConsoleResult pendingResult = XConsoleShellVi_submitLine(
            self, &self->m_session, line, length);
        return xcs_finish_command(self, NULL, pendingResult);
    }
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    self->m_lineCursor = 0;
#endif
#if XCONSOLE_SHELL_LOGIN_ON
    if (!xcs_is_sensitive_line(line, length))
        xcs_record_history(self, line, length);
#else
    xcs_record_history(self, line, length);
#endif
#if XCONSOLE_SHELL_HISTORY_ON
    xcs_history_reset_cursor(self);
#endif
#if XCONSOLE_SHELL_CANCEL_ON
    if (self->m_io.cancelled && self->m_io.cancelled(self->m_io.userData))
        return xcs_finish_command(self, NULL, XConsoleResult_Cancelled);
#endif
    tokenResult = XConsoleShellParser_tokenize(self, line, length);
    self->m_lineLength = 0;
    if (tokenResult != XConsoleResult_Ok)
        return xcs_finish_command(self, NULL, tokenResult);
    if (self->m_argumentCount == 0)
        return xcs_finish_command(self, NULL, XConsoleResult_Ok);
    command = XConsoleShellParser_find(self, self->m_arguments,
                                       self->m_argumentCount, &consumed);
    if (!command) return xcs_finish_command(self, NULL, XConsoleResult_UnknownCommand);
    argc = (int)(self->m_argumentCount - consumed);
    if (argc < command->minArgs || (command->maxArgs >= 0 && argc > command->maxArgs))
        return xcs_finish_command(self, command, XConsoleResult_InvalidArgument);
#if XCONSOLE_SHELL_LOGIN_REQUIRED_ON
    if (!self->m_session.authenticated &&
        !(command->flags & XConsoleCommandFlag_AllowUnauthenticated))
        return xcs_finish_command(self, command, XConsoleResult_PermissionDenied);
#endif
#if XCONSOLE_SHELL_AUTH_ON
    if ((command->flags & XConsoleCommandFlag_Dangerous) &&
        !self->m_session.authenticated)
        return xcs_finish_command(self, command, XConsoleResult_PermissionDenied);
#endif
#if XCONSOLE_SHELL_PERMISSION_ON
    if ((command->flags & XConsoleCommandFlag_Dangerous) &&
        (!self->m_session.authenticated ||
         !(self->m_session.permissionMask & XConsoleShellPermission_Dangerous)))
        return xcs_finish_command(self, command, XConsoleResult_PermissionDenied);
#endif
    if ((command->flags & XConsoleCommandFlag_Administrator) &&
        (!self->m_session.authenticated
#if XCONSOLE_SHELL_PERMISSION_ON
         || !(self->m_session.permissionMask & XConsoleShellPermission_Administrator)
#endif
        ))
        return xcs_finish_command(self, command, XConsoleResult_PermissionDenied);
    if (!command->handler)
        return xcs_finish_command(self, command, XConsoleResult_NotSupported);
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
    ++self->m_commandExecutionDepth;
#endif
    result = command->handler(self, &self->m_session, argc,
                              self->m_arguments + consumed, command->userData);
    if (result < XConsoleResult_Failed || result > XConsoleResult_MoreOutput)
        result = XConsoleResult_Failed;
    {
        XConsoleResult finished = xcs_finish_command(self, command,
                                                      (XConsoleResult)result);
#if XCONSOLE_SHELL_DYNAMIC_REGISTER_ON
        --self->m_commandExecutionDepth;
        xcs_collect_retired_commands(self);
#endif
        return finished;
    }
}

XConsoleResult XConsoleShell_feedByte(XConsoleShell* self, uint8_t byte)
{
    XConsoleResult result;
    if (!self) return XConsoleResult_InvalidArgument;
#if XCONSOLE_SHELL_LOGIN_ON
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    if (XConsoleShellLogin_isInputPending(&self->m_session) &&
        self->m_escapeState) {
        self->m_escapeState = 0;
        return XConsoleResult_Ok;
    }
#endif
    if (XConsoleShellLogin_isInputPending(&self->m_session) && byte == 0x1b)
        return XConsoleResult_Ok;
#endif
#if XCONSOLE_SHELL_STATS_ON
    ++self->m_inputBytes;
#endif
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    if (self->m_escapeState) {
        if (self->m_escapeState == 1 && byte == '[') {
            self->m_escapeState = 2;
            return XConsoleResult_Ok;
        }
#if XCONSOLE_SHELL_HISTORY_ON
        if (self->m_escapeState == 2 && (byte == 'A' || byte == 'B')) {
            size_t target = self->m_historyCursor;
            self->m_escapeState = 0;
            if (self->m_historyCount == 0) return XConsoleResult_Ok;
            if (byte == 'A') {
                if (target == self->m_historyCount) target = self->m_historyCount - 1u;
                else if (target > 0) --target;
            } else {
                if (target < self->m_historyCount - 1u) ++target;
                else {
                    self->m_historyCursor = self->m_historyCount;
                    self->m_lineLength = 0;
                    self->m_lineBuffer[0] = '\0';
                    self->m_lineCursor = 0;
                    return XConsoleResult_Ok;
                }
            }
            {
                const char* text = XConsoleShell_historyAt(self, target);
                size_t length = strlen(text ? text : "");
                memcpy(self->m_lineBuffer, text ? text : "", length + 1u);
                self->m_lineLength = length;
                self->m_lineCursor = length;
                self->m_historyCursor = target;
            }
            return XConsoleResult_Ok;
        }
#endif
        if (self->m_escapeState == 2 && (byte == 'C' || byte == 'D')) {
            self->m_escapeState = 0;
            if (byte == 'C' && self->m_lineCursor < self->m_lineLength)
                ++self->m_lineCursor;
            else if (byte == 'D' && self->m_lineCursor > 0)
                --self->m_lineCursor;
            return XConsoleResult_Ok;
        }
        self->m_escapeState = 0;
        return XConsoleResult_Ok;
    }
    if (byte == 0x1b) {
        self->m_escapeState = 1;
        return XConsoleResult_Ok;
    }
#endif
    if (byte == 0x03 && XCONSOLE_SHELL_CANCEL_ON) {
#if XCONSOLE_SHELL_LOGIN_ON
        if (XConsoleShellLogin_isInputPending(&self->m_session))
            XConsoleShellLogin_cancelInput(self, &self->m_session);
#endif
#if XCONSOLE_SHELL_EDITOR_ON
        if (XConsoleShellVi_isActive(&self->m_session))
            XConsoleShellVi_cancel(self, &self->m_session);
#endif
        self->m_session.cancelled = true;
        self->m_discardLine = false;
        self->m_lineLength = 0;
        self->m_lineBuffer[0] = '\0';
#if XCONSOLE_SHELL_LINE_EDITOR_ON
        self->m_lineCursor = 0;
#endif
        return XConsoleResult_Cancelled;
    }
#if XCONSOLE_SHELL_COMPLETION_ON
    if (byte == '\t') {
        xcs_complete_root_command(self);
        return XConsoleResult_Ok;
    }
#endif
    if (byte == '\r' || byte == '\n') {
        if (self->m_discardLine) {
            /* 超长行必须整体丢弃，不能在换行时执行截断前缀。 */
            self->m_discardLine = false;
            self->m_lineLength = 0;
            self->m_lineBuffer[0] = '\0';
#if XCONSOLE_SHELL_LINE_EDITOR_ON
            self->m_lineCursor = 0;
#endif
            return XConsoleResult_ResourceLimit;
        }
        result = XConsoleShell_processLine(self, self->m_lineBuffer, self->m_lineLength);
        self->m_lineLength = 0;
        self->m_lineBuffer[0] = '\0';
#if XCONSOLE_SHELL_LINE_EDITOR_ON
        self->m_lineCursor = 0;
#endif
        return result;
    }
    if (byte == '\b' || byte == 0x7f) {
        if (self->m_lineLength
#if XCONSOLE_SHELL_LINE_EDITOR_ON
            && self->m_lineCursor
#endif
        ) {
#if XCONSOLE_SHELL_LINE_EDITOR_ON
            memmove(self->m_lineBuffer + self->m_lineCursor - 1u,
                    self->m_lineBuffer + self->m_lineCursor,
                    self->m_lineLength - self->m_lineCursor + 1u);
            --self->m_lineCursor;
            --self->m_lineLength;
#else
            --self->m_lineLength;
#endif
        }
        self->m_lineBuffer[self->m_lineLength] = '\0';
        return XConsoleResult_Ok;
    }
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    if (byte == 0x04) {
        if (self->m_lineCursor < self->m_lineLength) {
            memmove(self->m_lineBuffer + self->m_lineCursor,
                    self->m_lineBuffer + self->m_lineCursor + 1u,
                    self->m_lineLength - self->m_lineCursor);
            --self->m_lineLength;
            self->m_lineBuffer[self->m_lineLength] = '\0';
        }
        return XConsoleResult_Ok;
    }
#endif
    if (self->m_discardLine) return XConsoleResult_ResourceLimit;
    if (self->m_lineLength + 1 >= sizeof(self->m_lineBuffer)) {
        self->m_discardLine = true;
        return XConsoleResult_ResourceLimit;
    }
#if XCONSOLE_SHELL_LINE_EDITOR_ON
    if (self->m_lineCursor < self->m_lineLength) {
        memmove(self->m_lineBuffer + self->m_lineCursor + 1u,
                self->m_lineBuffer + self->m_lineCursor,
                self->m_lineLength - self->m_lineCursor + 1u);
        self->m_lineBuffer[self->m_lineCursor++] = (char)byte;
    } else {
        self->m_lineBuffer[self->m_lineLength++] = (char)byte;
        self->m_lineCursor = self->m_lineLength;
    }
#else
    self->m_lineBuffer[self->m_lineLength++] = (char)byte;
#endif
    self->m_lineBuffer[self->m_lineLength] = '\0';
    return XConsoleResult_Ok;
}

XConsoleResult XConsoleShell_feedData(XConsoleShell* self,
                                      const void* data, size_t size)
{
    const uint8_t* bytes = (const uint8_t*)data;
    size_t i;
    XConsoleResult result = XConsoleResult_Ok;
    if (!self || (!data && size)) return XConsoleResult_InvalidArgument;
    for (i = 0; i < size; ++i) {
        result = XConsoleShell_feedByte(self, bytes[i]);
    }
    return result;
}

XConsoleResult XConsoleShell_pump(XConsoleShell* self, size_t maxBytes)
{
    uint8_t buffer[128];
    int64_t count;
    if (!self || !self->m_io.read) return XConsoleResult_NotSupported;
#if XCONSOLE_SHELL_ASYNC_ON
    self->m_asyncLastReadBytes = 0;
#endif
    if (maxBytes == 0 || maxBytes > sizeof(buffer)) maxBytes = sizeof(buffer);
    count = self->m_io.read(self->m_io.userData, buffer, maxBytes);
    if (count < 0 || (size_t)count > maxBytes) return XConsoleResult_IoError;
#if XCONSOLE_SHELL_ASYNC_ON
    self->m_asyncLastReadBytes = (size_t)count;
#endif
    return count == 0 ? XConsoleResult_Ok : XConsoleShell_feedData(self, buffer, (size_t)count);
}

void XConsoleShell_setRunning(XConsoleShell* self, bool running)
{
    if (!self) return;
    self->m_running = running;
#if XCONSOLE_SHELL_ASYNC_ON
    /* 主程序 Shell 提供 inputAttach；状态停止时立即请求应用事件循环退出，
       避免依赖当前输入事件回调返回后的额外调度时序。 */
    if (!running && self->m_io.inputAttach)
        xcs_async_quit_application();
#endif
}

bool XConsoleShell_isRunning(const XConsoleShell* self)
{
    return self && self->m_running;
}

#if XCONSOLE_SHELL_ASYNC_ON
bool XConsoleShell_startAsync(XConsoleShell* self)
{
    if (!self || !self->m_io.read || self->m_asyncEventType == XEVENT_TYPE_NONE)
        return false;
#if XCONSOLE_SHELL_ASYNC_RUN_MODE == XCONSOLE_SHELL_ASYNC_MODE_THREAD
    {
        XVarList* arguments;
        XThread* thread;
        XThread* owner;
        if (self->m_asyncThread ||
            XObject_thread((const XObject*)self) != XThread_currentThread())
            return self->m_asyncThread != NULL;
        owner = XThread_currentThread();
        arguments = XVarList_Create(XVar(void*, self));
        if (!arguments) return false;
        thread = XThread_create_func(xcs_async_thread, arguments);
        if (!thread) {
            XVarList_delete(arguments);
            return false;
        }
        if (!XObject_moveToThread((XObject*)self, thread)) {
            XClass_delete_base((XClass*)thread);
            return false;
        }
        self->m_asyncOwnerThread = owner;
        self->m_asyncThread = thread;
        XAtomic_store_bool(&self->m_asyncWorkerReady, false,
                           XAtomic_MemoryOrder_Release);
        XAtomic_store_bool(&self->m_asyncRunning, true,
                           XAtomic_MemoryOrder_Release);
        self->m_running = true;
        if (!XThread_start(thread)) {
            (void)XObject_moveToThread((XObject*)self, owner);
            self->m_asyncThread = NULL;
            XClass_delete_base((XClass*)thread);
            XAtomic_store_bool(&self->m_asyncRunning, false,
                               XAtomic_MemoryOrder_Release);
            return false;
        }
        {
            size_t waitCount = 0;
            while (!XAtomic_load_bool(&self->m_asyncWorkerReady,
                                      XAtomic_MemoryOrder_Acquire) &&
                   XThread_isRunning(thread) && waitCount++ < 1000u)
                XThread_usleep(1000);
            if (!XAtomic_load_bool(&self->m_asyncWorkerReady,
                                   XAtomic_MemoryOrder_Acquire)) {
                (void)XConsoleShell_stopAsync(self, UINT32_MAX);
                return false;
            }
        }
        if (self->m_io.inputAttach &&
            !self->m_io.inputAttach(self->m_io.userData, self)) {
            (void)XConsoleShell_stopAsync(self, UINT32_MAX);
            return false;
        }
        XAtomic_store_bool(&self->m_asyncInputAttached,
                           self->m_io.inputAttach != NULL,
                           XAtomic_MemoryOrder_Release);
        return true;
    }
#elif XCONSOLE_SHELL_ASYNC_RUN_MODE == XCONSOLE_SHELL_ASYNC_MODE_EVENT_DISPATCHER
    if (XObject_thread((const XObject*)self) != XThread_currentThread())
        return false;
    if (!XObject_eventDispatcher((const XObject*)self)) return false;
    XAtomic_store_bool(&self->m_asyncRunning, true, XAtomic_MemoryOrder_Release);
    self->m_running = true;
    if (self->m_io.inputAttach &&
        !self->m_io.inputAttach(self->m_io.userData, self)) {
        XAtomic_store_bool(&self->m_asyncRunning, false,
                           XAtomic_MemoryOrder_Release);
        self->m_running = false;
        return false;
    }
    XAtomic_store_bool(&self->m_asyncInputAttached,
                       self->m_io.inputAttach != NULL,
                       XAtomic_MemoryOrder_Release);
    if (XAtomic_load_bool(&self->m_asyncInputAttached,
                          XAtomic_MemoryOrder_Acquire)) {
        self->m_asyncPollTimer = XObject_startTimer_ms(
            (XObject*)self, XCONSOLE_SHELL_ASYNC_POLL_INTERVAL_MS,
            XTimerType_CoarseTimer);
        if (self->m_asyncPollTimer == XTIMER_INVALID_ID) {
            if (self->m_io.inputDetach)
                self->m_io.inputDetach(self->m_io.userData, self);
            XAtomic_store_bool(&self->m_asyncInputAttached, false,
                               XAtomic_MemoryOrder_Release);
            XAtomic_store_bool(&self->m_asyncRunning, false,
                               XAtomic_MemoryOrder_Release);
            self->m_running = false;
            return false;
        }
    }
    return true;
#else
    return false;
#endif
}

bool XConsoleShell_stopAsync(XConsoleShell* self, uint32_t timeoutMs)
{
    if (!self) return false;
#if XCONSOLE_SHELL_ASYNC_RUN_MODE == XCONSOLE_SHELL_ASYNC_MODE_THREAD
    if (self->m_asyncThread) {
        XThread* thread = self->m_asyncThread;
        uint32_t waitMs = timeoutMs ? timeoutMs : UINT32_MAX;
        XThread_requestInterruption(thread);
        XThread_quit(thread);
        if (!XThread_wait(thread, waitMs)) return false;
        self->m_asyncThread = NULL;
        XClass_delete_base((XClass*)thread);
    }
#else
    (void)timeoutMs;
#endif
    if (self->m_asyncPollTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_asyncPollTimer);
        self->m_asyncPollTimer = XTIMER_INVALID_ID;
    }
    if (XAtomic_load_bool(&self->m_asyncInputAttached,
                          XAtomic_MemoryOrder_Acquire)) {
        if (self->m_io.inputDetach)
            self->m_io.inputDetach(self->m_io.userData, self);
        XAtomic_store_bool(&self->m_asyncInputAttached, false,
                           XAtomic_MemoryOrder_Release);
    }
    XAtomic_store_bool(&self->m_asyncRunning, false, XAtomic_MemoryOrder_Release);
    XAtomic_store_bool(&self->m_asyncInputPosted, false,
                       XAtomic_MemoryOrder_Release);
#if XCONSOLE_SHELL_ASYNC_RUN_MODE == XCONSOLE_SHELL_ASYNC_MODE_THREAD
    XAtomic_store_bool(&self->m_asyncWorkerReady, false,
                       XAtomic_MemoryOrder_Release);
#endif
    if (self->m_asyncEventType != XEVENT_TYPE_NONE &&
        XObject_thread((const XObject*)self) == XThread_currentThread()) {
        XAtomic_store_bool(&self->m_asyncInputPosted, false,
                           XAtomic_MemoryOrder_Release);
        XCoreApplication_removePostedEvents((XObject*)self,
                                             self->m_asyncEventType);
    }
    return true;
}

bool XConsoleShell_isAsyncRunning(const XConsoleShell* self)
{
    return self && XAtomic_load_bool(&self->m_asyncRunning,
                                     XAtomic_MemoryOrder_Acquire);
}

bool XConsoleShell_notifyInput(XConsoleShell* self)
{
    bool expected = false;
    XEvent* event;
    if (!self || !XConsoleShell_isAsyncRunning(self) ||
        self->m_asyncEventType == XEVENT_TYPE_NONE)
        return false;
    if (!XAtomic_compare_exchange_strong_bool(
            &self->m_asyncInputPosted, &expected, true,
            XAtomic_MemoryOrder_AcqRel, XAtomic_MemoryOrder_Acquire))
        return true;
    event = XEvent_create(self->m_asyncEventType);
    if (!event) {
        XAtomic_store_bool(&self->m_asyncInputPosted, false,
                           XAtomic_MemoryOrder_Release);
        return false;
    }
    if (!XCoreApplication_tryPostEvent((XObject*)self, event,
                                        XEVENT_PRIORITY_NORMAL)) {
        XAtomic_store_bool(&self->m_asyncInputPosted, false,
                           XAtomic_MemoryOrder_Release);
        XEvent_delete_base(event);
        return false;
    }
    return true;
}

bool XConsoleShell_event_base(XConsoleShell* self, XEvent* event)
{
    if (!self || !event) return false;
    return XClassGetVirtualFunc(self, EXConsoleShell_Event,
                                bool(*)(XConsoleShell*, XEvent*))(self, event);
}
#endif

const XConsoleShellSession* XConsoleShell_session_const(const XConsoleShell* self)
{
    return self ? &self->m_session : NULL;
}

XConsoleShellSession* XConsoleShell_session(XConsoleShell* self)
{
    return self ? &self->m_session : NULL;
}

void XConsoleShell_setAuthenticated(XConsoleShell* self, bool authenticated)
{
    if (self) self->m_session.authenticated = authenticated;
}

#if XCONSOLE_SHELL_HISTORY_ON
size_t XConsoleShell_historyCount(const XConsoleShell* self)
{
    return self ? self->m_historyCount : 0;
}

const char* XConsoleShell_historyAt(const XConsoleShell* self, size_t index)
{
    if (!self || index >= self->m_historyCount || XCONSOLE_SHELL_HISTORY_CAPACITY == 0)
        return NULL;
    return self->m_history[xcs_history_index(self, index)];
}

void XConsoleShell_clearHistory(XConsoleShell* self)
{
    if (!self) return;
    memset(self->m_history, 0, sizeof(self->m_history));
    self->m_historyCount = 0;
    self->m_historyNext = 0;
    self->m_historyCursor = 0;
}
#endif

#if XCONSOLE_SHELL_STATS_ON
bool XConsoleShell_stats(const XConsoleShell* self, XConsoleShellStats* stats)
{
    if (!self || !stats) return false;
    stats->processedLines = self->m_processedLines;
    stats->successfulCommands = self->m_successfulCommands;
    stats->failedCommands = self->m_failedCommands;
    stats->inputBytes = self->m_inputBytes;
    stats->outputBytes = self->m_outputBytes;
    stats->registeredCommands = self->m_commandCount;
    return true;
}

void XConsoleShell_clearStats(XConsoleShell* self)
{
    if (!self) return;
    self->m_processedLines = 0;
    self->m_successfulCommands = 0;
    self->m_failedCommands = 0;
    self->m_inputBytes = 0;
    self->m_outputBytes = 0;
}
#endif

static bool xcs_write_transport(XConsoleShell* self, const void* data, size_t size)
{
    size_t offset = 0;
    if (!self || (!data && size) || !self->m_io.write) return false;
    while (offset < size) {
        int64_t written = self->m_io.write(self->m_io.userData,
                                           (const uint8_t*)data + offset,
                                           size - offset);
        if (written <= 0 || (size_t)written > size - offset) return false;
        offset += (size_t)written;
    }
    return true;
}

#if XCONSOLE_SHELL_LOG_ON
static void xcs_log_output(XConsoleShell* self, const void* data, size_t size)
{
    if (self && size && self->m_io.log)
        self->m_io.log(self->m_io.userData, &self->m_session, data, size);
}
#endif

bool XConsoleShell_write(XConsoleShell* self, const void* data, size_t size)
{
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    const uint8_t* bytes = (const uint8_t*)data;
    size_t offset = 0;
    if (!self || (!data && size) || !self->m_io.write)
        return false;
    while (offset < size) {
        size_t available = XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY -
                           self->m_asyncOutputSize;
        size_t chunk;
        size_t i;
        /* 队列满时主动排空，避免单条命令的多段输出因固定容量失败。 */
        if (!available) {
            if (!XConsoleShell_flushOutput(self)) return false;
            available = XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY;
        }
        chunk = size - offset;
        if (chunk > available) chunk = available;
        for (i = 0; i < chunk; ++i) {
            self->m_asyncOutput[self->m_asyncOutputTail] = bytes[offset + i];
            self->m_asyncOutputTail =
                (self->m_asyncOutputTail + 1u) % XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY;
        }
        self->m_asyncOutputSize += chunk;
        offset += chunk;
    }
#if XCONSOLE_SHELL_STATS_ON
    self->m_outputBytes += size;
#endif
#if XCONSOLE_SHELL_LOG_ON
    xcs_log_output(self, data, size);
#endif
    return true;
#else
    if (!xcs_write_transport(self, data, size)) return false;
#if XCONSOLE_SHELL_STATS_ON
    self->m_outputBytes += size;
#endif
    if (self->m_io.flush && !self->m_io.flush(self->m_io.userData)) return false;
#if XCONSOLE_SHELL_LOG_ON
    xcs_log_output(self, data, size);
#endif
    return true;
#endif
}

bool XConsoleShell_writeUtf8(XConsoleShell* self, const char* text)
{
    return text && XConsoleShell_write(self, text, strlen(text));
}

#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
bool XConsoleShell_flushOutput(XConsoleShell* self)
{
    if (!self || !self->m_io.write) return false;
    while (self->m_asyncOutputSize) {
        size_t contiguous = XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY -
                            self->m_asyncOutputHead;
        if (contiguous > self->m_asyncOutputSize)
            contiguous = self->m_asyncOutputSize;
        if (!xcs_write_transport(self, self->m_asyncOutput + self->m_asyncOutputHead,
                                  contiguous)) return false;
        self->m_asyncOutputHead =
            (self->m_asyncOutputHead + contiguous) % XCONSOLE_SHELL_ASYNC_OUTPUT_CAPACITY;
        self->m_asyncOutputSize -= contiguous;
    }
    return !self->m_io.flush || self->m_io.flush(self->m_io.userData);
}
#endif

#if XCONSOLE_SHELL_MULTI_SESSION_ON
bool XConsoleShell_writeForSession(XConsoleShell* self, XConsoleShellSession* session,
                                   const void* data, size_t size)
{
    XConsoleShellSessionSwap swap;
    bool result;
    if (!xcs_is_session(self, session)) return false;
    if (session == &self->m_session) return XConsoleShell_write(self, data, size);
    if (!xcs_enter_session(self, session, &swap)) return false;
    result = XConsoleShell_write(self, data, size);
#if XCONSOLE_SHELL_ASYNC_OUTPUT_ON
    if (result) result = XConsoleShell_flushOutput(self);
#endif
    xcs_leave_session(self, session, &swap);
    return result;
}
#endif

#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
size_t XConsoleShell_pollProcesses(XConsoleShell* self, int timeoutMsecs)
{
    return self ? XConsoleShellExecutor_pollAsync(self, timeoutMsecs) : 0;
}
#endif

bool XConsoleShell_writeError(XConsoleShell* self, XConsoleResult result,
                              const char* detail)
{
    const char* prefix = "错误: ";
    if (!XConsoleShell_writeUtf8(self, prefix)) return false;
    if (detail && !XConsoleShell_writeUtf8(self, detail)) return false;
    if (!detail) {
        const char* fallback = result == XConsoleResult_UnknownCommand ? "未知命令" :
                               result == XConsoleResult_InvalidArgument ? "参数无效" :
                               result == XConsoleResult_PermissionDenied ? "权限不足" :
                               result == XConsoleResult_Cancelled ? "已取消" : "操作失败";
        if (!XConsoleShell_writeUtf8(self, fallback)) return false;
    }
    return XConsoleShell_writeUtf8(self, "\n");
}

bool XConsoleShell_isCancelled(const XConsoleShell* self)
{
#if XCONSOLE_SHELL_CANCEL_ON
    return self && self->m_session.cancelled;
#else
    (void)self;
    return false;
#endif
}

void XConsoleShell_clearCancelled(XConsoleShell* self)
{
#if XCONSOLE_SHELL_CANCEL_ON
    if (self) self->m_session.cancelled = false;
#else
    (void)self;
#endif
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON */
