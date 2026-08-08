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

static int xcs_tasks_cjk_width(const char* text)
{
    const unsigned char* p = (const unsigned char*)text;
    int width = 0;
    while (p && *p) {
        unsigned char c = *p;
        uint32_t cp;
        int len;
        if (c < 0x80) { cp = (uint32_t)c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = (uint32_t)(c & 0x1F); len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = (uint32_t)(c & 0x0F); len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = (uint32_t)(c & 0x07); len = 4; }
        else { ++width; ++p; continue; }
        if (len > 1) {
            int i;
            int good = 1;
            for (i = 1; i < len; ++i)
                if ((p[i] & 0xC0) != 0x80) { good = 0; break; }
            if (!good) { ++width; p += len; continue; }
            for (i = 1; i < len; ++i) cp = (cp << 6) | (p[i] & 0x3F);
        }
        p += len;
        if (cp >= 0x1100 &&
            (cp <= 0x115F || cp == 0x2329 || cp == 0x232A ||
             (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
             (cp >= 0xAC00 && cp <= 0xD7A3) ||
             (cp >= 0xF900 && cp <= 0xFAFF) ||
             (cp >= 0xFE10 && cp <= 0xFE19) ||
             (cp >= 0xFE30 && cp <= 0xFE6F) ||
             (cp >= 0xFF00 && cp <= 0xFF60) ||
             (cp >= 0xFFE0 && cp <= 0xFFE6)))
            width += 2;
        else
            width += 1;
    }
    return width;
}

static void xcs_tasks_append(char* out, size_t capacity, size_t* pos,
                             const char* text, int width, bool right)
{
    int textWidth = text ? xcs_tasks_cjk_width(text) : 0;
    int pad = width - textWidth;
    if (!out || !pos || *pos >= capacity) return;
    if (pad < 0) pad = 0;
    if (*pos > 0) {
        if (*pos + 1 >= capacity) { out[*pos] = '\0'; return; }
        out[(*pos)++] = ' ';
    }
    if (right)
        for (; pad > 0 && *pos + 1 < capacity; --pad) out[(*pos)++] = ' ';
    if (text)
        for (; *text && *pos + 1 < capacity; ++text) out[(*pos)++] = *text;
    for (; pad > 0 && *pos + 1 < capacity; --pad) out[(*pos)++] = ' ';
    out[*pos] = '\0';
}

static bool xcs_tasks_write_header(XConsoleShell* shell)
{
    char line[160];
    size_t pos = 0;
    if (!shell) return false;
    line[0] = '\0';
    xcs_tasks_append(line, sizeof(line), &pos, "id", 5, true);
    xcs_tasks_append(line, sizeof(line), &pos, "\xe5\x90\x8d\xe7\xa7\xb0", 20, false);
    xcs_tasks_append(line, sizeof(line), &pos, "\xe7\x8a\xb6\xe6\x80\x81", 9, false);
    xcs_tasks_append(line, sizeof(line), &pos, "\xe4\xbc\x98\xe5\x85\x88\xe7\xba\xa7", 8, true);
    xcs_tasks_append(line, sizeof(line), &pos, "\xe6\xa0\x88(\xe5\xad\x97\xe8\x8a\x82)", 11, true);
    xcs_tasks_append(line, sizeof(line), &pos, "\xe7\xa9\xba\xe9\x97\xb2(\xe5\xad\x97\xe8\x8a\x82)", 10, true);
    if (pos + 2 > sizeof(line)) return false;
    line[pos++] = '\n';
    line[pos] = '\0';
    return XConsoleShell_writeUtf8(shell, line);
}

static XConsoleResult xcs_tasks_emit(void* userData, const XTaskInfo* info)
{
    XConsoleShell* shell = (XConsoleShell*)userData;
    char line[160];
    char number[24];
    size_t pos = 0;
    if (!shell || !info || !info->name || info->name[0] == '\0')
        return XConsoleResult_InvalidArgument;
    if (XConsoleShell_isCancelled(shell)) return XConsoleResult_Cancelled;
    line[0] = '\0';
    snprintf(number, sizeof(number), "%u", info->id);
    xcs_tasks_append(line, sizeof(line), &pos, number, 5, true);
    xcs_tasks_append(line, sizeof(line), &pos, info->name, 20, false);
    xcs_tasks_append(line, sizeof(line), &pos,
                     xcs_tasks_state(info->state), 9, false);
    snprintf(number, sizeof(number), "%d", (int)info->priority);
    xcs_tasks_append(line, sizeof(line), &pos, number, 8, true);
    snprintf(number, sizeof(number), "%u", info->stackSize);
    xcs_tasks_append(line, sizeof(line), &pos, number, 11, true);
    snprintf(number, sizeof(number), "%u", info->stackFree);
    xcs_tasks_append(line, sizeof(line), &pos, number, 10, true);
    if (pos + 2 > sizeof(line)) return XConsoleResult_IoError;
    line[pos++] = '\n';
    line[pos] = '\0';
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
    if (!xcs_tasks_write_header(shell))
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
