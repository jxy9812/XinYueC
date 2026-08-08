/**
 * @file XConsoleShellTasks.c
 * @brief XConsoleShell 内建 `tasks` 命令实现。
 * @details
 * 命令默认消费 XThread 注册表快照，也支持产品提供者覆盖；不访问平台原生
 * 任务接口。提供者同步逐条调用 Shell 的输出回调，整个过程使用固定栈行缓冲；
 * 没有注册线程或平台不支持时返回明确结果。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_TASKS_ON

#include "XConsoleShellTasks.h"
#include "XTask.h"
#include <stdio.h>

static const char* xcs_tasks_state(XTaskState state)
{
    switch (state) {
    case XTaskState_Ready: return "就绪";
    case XTaskState_Running: return "运行";
    case XTaskState_Blocked: return "阻塞";
    case XTaskState_Sleeping: return "睡眠";
    case XTaskState_Suspended: return "挂起";
    case XTaskState_Finished: return "完成";
    default: return "未知";
    }
}

static XConsoleResult xcs_tasks_emit(void* userData, const XTaskInfo* info)
{
    XConsoleShell* shell = (XConsoleShell*)userData;
    char line[160];
    int written;
    if (!shell || !info || !info->name || info->name[0] == '\0')
        return XConsoleResult_InvalidArgument;
    if (XConsoleShell_isCancelled(shell)) return XConsoleResult_Cancelled;
    written = snprintf(line, sizeof(line), "%5u %-20.20s %-9s %8d %11u %10u\n",
                       info->id, info->name, xcs_tasks_state(info->state),
                       (int)info->priority, info->stackSize, info->stackFree);
    if (written < 0 || (size_t)written >= sizeof(line)) return XConsoleResult_IoError;
    return XConsoleShell_writeUtf8(shell, line) ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static int xcs_tasks(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XConsoleResult result;
    (void)argv;
    (void)userData;
    if (!shell || !session || argc != 0) return XConsoleResult_InvalidArgument;
    if (!shell->m_taskProvider) {
        if (!XConsoleShell_writeUtf8(shell, "tasks: 提供者不可用\n"))
            return XConsoleResult_IoError;
        return XConsoleResult_NotSupported;
    }
    if (!XConsoleShell_writeUtf8(shell,
            "  id 名称                状态        优先级  栈(字节)    空闲(字节)\n"))
        return XConsoleResult_IoError;
    result = shell->m_taskProvider(shell->m_taskProviderUserData, shell, session,
                                   xcs_tasks_emit, shell);
    return result;
}

typedef struct XConsoleShellTaskPlatformContext {
    XConsoleShellTaskEmitFn emit;
    void* emitUserData;
    XConsoleResult result;
    size_t count;
} XConsoleShellTaskPlatformContext;

static bool xcs_tasks_platform_emit(void* userData, const XTaskInfo* info)
{
    XConsoleShellTaskPlatformContext* context =
        (XConsoleShellTaskPlatformContext*)userData;
    if (!context || !context->emit) return false;
    context->result = context->emit(context->emitUserData, info);
    if (context->result == XConsoleResult_Ok) ++context->count;
    return context->result == XConsoleResult_Ok;
}

XConsoleResult XConsoleShellTasks_platformProvider(
    void* userData, XConsoleShell* shell, XConsoleShellSession* session,
    XConsoleShellTaskEmitFn emit, void* emitUserData)
{
    XConsoleShellTaskPlatformContext context;
    bool enumerated;
    (void)userData;
    (void)session;
    if (!shell || !emit) return XConsoleResult_InvalidArgument;
    context.emit = emit;
    context.emitUserData = emitUserData;
    context.result = XConsoleResult_Ok;
    context.count = 0;
    enumerated = XTask_enumerateThreads(xcs_tasks_platform_emit, &context);
    if (context.result != XConsoleResult_Ok) return context.result;
    if (!enumerated) return XConsoleResult_NotSupported;
    if (context.count == 0 &&
        !XConsoleShell_writeUtf8(shell, "tasks: 没有注册 XThread\n"))
        return XConsoleResult_IoError;
    return XConsoleResult_Ok;
}

const XConsoleCommand XConsoleShellTasks_command = {
    "tasks", NULL, "显示产品提供的任务快照", "tasks", 0, 0,
    XConsoleCommandFlag_None, xcs_tasks, NULL, 0, NULL
};

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
          XCONSOLE_SHELL_TASKS_ON */
