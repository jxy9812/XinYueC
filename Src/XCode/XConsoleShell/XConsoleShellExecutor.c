/**
 * @file XConsoleShellExecutor.c
 * @brief 可选 exec 命令适配器。
 * @details
 * 外部进程功能只有在 XCONSOLE_SHELL_EXTERNAL_PROCESS_ON 和 XProcess_ON 同时
 * 打开时编译。实现只调用 XProcess 公共 API，不包含 POSIX、Win32、FreeRTOS
 * 或 Zephyr 进程接口；默认构建提供明确的 NotSupported 存根。
 */

#include "XConsoleShell_Protected.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON

#include "XByteArray.h"

#if XCONSOLE_SHELL_EXECUTOR_ON && XCONSOLE_SHELL_EXTERNAL_PROCESS_ON && XProcess_ON
#include "XProcess.h"
#include "XFileSystem.h"
#include "XMemory.h"
#include <string.h>

static XString* xexec_resolve_path(const XConsoleShellSession* session,
                                   const char* rawPath)
{
    XString* path;
    XString* resolved;
    if (!session || !rawPath || !rawPath[0]) return NULL;
    path = XString_create();
    resolved = XString_create();
    if (!path || !resolved) {
        if (path) XString_delete_base(path);
        if (resolved) XString_delete_base(resolved);
        return NULL;
    }
    if (rawPath[0] == '/') {
        if (!XString_assign_utf8(path, rawPath)) goto fail;
    } else if (!XString_assign_utf8(path, session->currentPath) ||
               !XString_append_utf8(path, "/") ||
               !XString_append_utf8(path, rawPath)) {
        goto fail;
    }
    if (!XFileSystem_resolvePath(path, resolved, XPathStyle_Absolute))
        XString_assign(resolved, path);
    XString_delete_base(path);
    return resolved;
fail:
    XString_delete_base(path);
    XString_delete_base(resolved);
    return NULL;
}

#if XCONSOLE_SHELL_REDIRECT_ON
static bool xexec_write_redirect(const XConsoleShellSession* session,
                                 const char* path, const XByteArray* bytes, bool append)
{
    XString* filePath;
    XFd fd;
    int error = 0;
    size_t offset = 0;
    size_t size;
    const uint8_t* data;
    if (!session || !path || !bytes) return false;
    filePath = xexec_resolve_path(session, path);
    if (!filePath) return false;
    fd = XFileSystem_open(filePath, XFileSystem_WriteOnly | XFileSystem_Create |
                          (append ? XFileSystem_Append : XFileSystem_Truncate), &error);
    XString_delete_base(filePath);
    if (fd == XFD_INVALID) return false;
    size = XByteArray_size_base(bytes);
    data = (const uint8_t*)XByteArray_data(bytes);
    while (offset < size) {
        int64_t written = XFileSystem_write(fd, data + offset, (int64_t)(size - offset));
        if (written <= 0 || (size_t)written > size - offset) {
            XFileSystem_close(fd);
            return false;
        }
        offset += (size_t)written;
    }
    if (!XFileSystem_flush(fd)) {
        XFileSystem_close(fd);
        return false;
    }
    XFileSystem_close(fd);
    return true;
}

#endif /* XCONSOLE_SHELL_REDIRECT_ON */

static XConsoleResult xexec_drain_channel(XConsoleShell* shell, XProcess* process,
                                          XConsoleShellSession* session,
                                          XProcessChannel channel, const char* path,
                                          bool append, bool* started)
{
    XByteArray* bytes;
    bool ok = true;
    if (!shell || !process || !started) return XConsoleResult_InvalidArgument;
    bytes = channel == XProcessChannel_StandardOutput
                ? XProcess_readAllStandardOutput(process)
                : XProcess_readAllStandardError(process);
    if (!bytes) return XConsoleResult_Failed;
    if (XByteArray_size_base(bytes)) {
        if (path) {
#if XCONSOLE_SHELL_REDIRECT_ON
            ok = xexec_write_redirect(session, path, bytes, append || *started);
#else
            ok = false;
#endif
        } else {
            ok = XConsoleShell_write(shell, XByteArray_data(bytes),
                                     XByteArray_size_base(bytes));
        }
        if (ok) *started = true;
    }
    XByteArray_delete_base(bytes);
    return ok ? XConsoleResult_Ok : XConsoleResult_IoError;
}

static bool xexec_parse_redirects(int argc, const char* const* argv,
                                  const char** childArgs, int* childCount,
                                  const char** stdinPath, const char** stdoutPath,
                                  const char** stderrPath, bool* append)
{
    int i;
    int count = 0;
    if (!argv || !childArgs || !childCount || !stdinPath || !stdoutPath ||
        !stderrPath || !append) return false;
    *stdinPath = NULL;
    *stdoutPath = NULL;
    *stderrPath = NULL;
    *append = false;
    for (i = 0; i < argc; ++i) {
#if XCONSOLE_SHELL_REDIRECT_ON
        if ((!strcmp(argv[i], "--stdin") || !strcmp(argv[i], "<")) && i + 1 < argc) {
            *stdinPath = argv[++i];
            continue;
        }
        if ((!strcmp(argv[i], "--stdout") || !strcmp(argv[i], ">")) && i + 1 < argc) {
            *stdoutPath = argv[++i];
            continue;
        }
        if ((!strcmp(argv[i], "--stderr") || !strcmp(argv[i], "2>")) && i + 1 < argc) {
            *stderrPath = argv[++i];
            continue;
        }
        if (!strcmp(argv[i], "--append")) {
            *append = true;
            continue;
        }
#endif
        if (count >= XCONSOLE_SHELL_MAX_ARGUMENTS - 1) return false;
        childArgs[count++] = argv[i];
    }
    *childCount = count;
    return count > 0;
}

static bool xexec_set_input_file(XProcess* process,
                                 const XConsoleShellSession* session,
                                 const char* rawPath)
{
    XString* path;
    bool result;
    if (!process || !session || !rawPath) return false;
    path = xexec_resolve_path(session, rawPath);
    if (!path) return false;
    result = XProcess_setStandardInputFile(process, path);
    XString_delete_base(path);
    return result;
}

#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
/** @brief Shell 拥有的异步 XProcess 任务；进程对象和任务结构均由 Executor 释放。 */
typedef struct XConsoleShellAsyncProcess {
    XProcess* process;                              /**< 正在运行的进程对象。 */
    uint32_t sessionId;                             /**< 启动时的稳定会话 ID。 */
} XConsoleShellAsyncProcess;

static bool xexec_write_to_session(XConsoleShell* shell, uint32_t sessionId,
                                   const void* data, size_t size)
{
    if (!shell || (!data && size)) return false;
#if XCONSOLE_SHELL_MULTI_SESSION_ON
    {
        XConsoleShellSession* session = XConsoleShell_findSession(shell, sessionId);
        /* 会话已关闭时丢弃其迟到输出，进程仍照常回收。 */
        return !session || XConsoleShell_writeForSession(shell, session, data, size);
    }
#else
    (void)sessionId;
    return XConsoleShell_write(shell, data, size);
#endif
}

/* 每次轮询都取走子进程输出，避免管道写满后子进程与 Shell 相互等待。 */
static void xexec_drain_async_channel(XConsoleShell* shell,
                                      XConsoleShellAsyncProcess* job,
                                      XProcessChannel channel)
{
    XByteArray* bytes;
    if (!shell || !job || !job->process) return;
    bytes = channel == XProcessChannel_StandardOutput
                ? XProcess_readAllStandardOutput(job->process)
                : XProcess_readAllStandardError(job->process);
    if (!bytes) return;
    if (XByteArray_size_base(bytes))
        (void)xexec_write_to_session(shell, job->sessionId,
                                     XByteArray_data(bytes), XByteArray_size_base(bytes));
    XByteArray_delete_base(bytes);
}

static int xexec_start_async(XConsoleShell* shell, XConsoleShellSession* session,
                             int argc, const char* const* argv)
{
    XConsoleShellAsyncProcess* job;
    XProcess* process;
    size_t i;
    if (!shell || !argv || argc < 2) return XConsoleResult_InvalidArgument;
    for (i = 0; i < XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY; ++i) {
        if (!shell->m_asyncProcesses[i]) break;
    }
    if (i == XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY) return XConsoleResult_ResourceLimit;
    process = XProcess_create();
    if (!process || !session ||
        !XProcess_setWorkingDirectory_utf8(process, session->currentPath) ||
        !XProcess_start_utf8(process, argv[1], argv + 2,
                                          (size_t)(argc - 2), XIODevice_ReadOnly)) {
        if (process) XProcess_delete_base(process);
        return XConsoleResult_Failed;
    }
    job = (XConsoleShellAsyncProcess*)XCalloc_System(1, sizeof(*job));
    if (!job) {
        XProcess_kill(process);
        XProcess_waitForFinished(process, -1);
        XProcess_delete_base(process);
        return XConsoleResult_ResourceLimit;
    }
    job->process = process;
    job->sessionId = session ? session->id : 1u;
    shell->m_asyncProcesses[i] = job;
    return XConsoleResult_MoreOutput;
}

size_t XConsoleShellExecutor_pollAsync(XConsoleShell* shell, int timeoutMsecs)
{
    size_t completed = 0;
    size_t i;
    if (!shell) return 0;
    for (i = 0; i < XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY; ++i) {
        XConsoleShellAsyncProcess* job = shell->m_asyncProcesses[i];
        if (!job || !job->process) continue;
        (void)XProcess_poll(job->process, completed ? 0 : timeoutMsecs);
        xexec_drain_async_channel(shell, job, XProcessChannel_StandardOutput);
        xexec_drain_async_channel(shell, job, XProcessChannel_StandardError);
        if (XProcess_state(job->process) != XProcessState_NotRunning) continue;
        XProcess_delete_base(job->process);
        XFree_System(job);
        shell->m_asyncProcesses[i] = NULL;
        ++completed;
    }
    return completed;
}

void XConsoleShellExecutor_abortAsync(XConsoleShell* shell)
{
    size_t i;
    if (!shell) return;
    for (i = 0; i < XCONSOLE_SHELL_ASYNC_PROCESS_CAPACITY; ++i) {
        XConsoleShellAsyncProcess* job = shell->m_asyncProcesses[i];
        if (!job) continue;
        if (job->process) {
            if (XProcess_state(job->process) != XProcessState_NotRunning) {
                XProcess_kill(job->process);
                XProcess_waitForFinished(job->process, -1);
            }
            XProcess_delete_base(job->process);
        }
        XFree_System(job);
        shell->m_asyncProcesses[i] = NULL;
    }
}
#endif

#if XCONSOLE_SHELL_PIPE_ON
static int xexec_run_pipe(XConsoleShell* shell, XConsoleShellSession* session,
                          int argc, const char* const* argv)
{
    XProcess* source = NULL;
    XProcess* sink = NULL;
    XByteArray* output = NULL;
    XByteArray* error = NULL;
    int separator = -1;
    int i;
    int producerCount;
    int consumerCount;
    int result = XConsoleResult_Failed;
    if (!shell || !session || !argv || argc < 4 || strcmp(argv[0], "--pipe") != 0)
        return XConsoleResult_InvalidArgument;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--") == 0) {
            separator = i;
            break;
        }
    }
    if (separator < 2 || separator + 1 >= argc) return XConsoleResult_InvalidArgument;
    producerCount = separator - 1;
    consumerCount = argc - separator - 1;
    if (producerCount >= XCONSOLE_SHELL_MAX_ARGUMENTS ||
        consumerCount >= XCONSOLE_SHELL_MAX_ARGUMENTS)
        return XConsoleResult_ResourceLimit;
    source = XProcess_create();
    sink = XProcess_create();
    if (!source || !sink ||
        !XProcess_setWorkingDirectory_utf8(source, session->currentPath) ||
        !XProcess_setWorkingDirectory_utf8(sink, session->currentPath) ||
        !XProcess_start_utf8(sink, argv[separator + 1], argv + separator + 2,
                              (size_t)(consumerCount - 1), XIODevice_ReadWrite) ||
        !XProcess_setStandardOutputProcess(source, sink) ||
        !XProcess_start_utf8(source, argv[1], argv + 2,
                             (size_t)(producerCount - 1), XIODevice_ReadOnly)) {
        if (source) XProcess_kill(source);
        if (sink) XProcess_kill(sink);
        if (source) XProcess_delete_base(source);
        if (sink) XProcess_delete_base(sink);
        return XConsoleResult_Failed;
    }
    if (!XProcess_waitForFinished(source, -1) || !XProcess_waitForFinished(sink, -1)) {
        XProcess_kill(source);
        XProcess_kill(sink);
        XProcess_waitForFinished(source, -1);
        XProcess_waitForFinished(sink, -1);
        XProcess_delete_base(source);
        XProcess_delete_base(sink);
        return XConsoleResult_Failed;
    }
    output = XProcess_readAllStandardOutput(sink);
    error = XProcess_readAllStandardError(sink);
    if ((output && XByteArray_size_base(output) &&
         !XConsoleShell_write(shell, XByteArray_data(output), XByteArray_size_base(output))) ||
        (error && XByteArray_size_base(error) &&
         !XConsoleShell_write(shell, XByteArray_data(error), XByteArray_size_base(error))))
        result = XConsoleResult_IoError;
    else if (XProcess_exitCode(source) == 0 && XProcess_exitCode(sink) == 0)
        result = XConsoleResult_Ok;
    if (output) XByteArray_delete_base(output);
    if (error) XByteArray_delete_base(error);
    XProcess_delete_base(source);
    XProcess_delete_base(sink);
    return result;
}
#endif

static int xexec_run(XConsoleShell* shell, XConsoleShellSession* session,
                     int argc, const char* const* argv, void* userData)
{
    XProcess* process;
    const char* childArgs[XCONSOLE_SHELL_MAX_ARGUMENTS];
    const char* stdinPath;
    const char* stdoutPath;
    const char* stderrPath;
    bool append;
    bool stdoutStarted = false;
    bool stderrStarted = false;
    int childCount;
    int i;
    (void)userData;
    if (argc < 1) return XConsoleResult_InvalidArgument;
#if XCONSOLE_SHELL_PROCESS_ASYNC_ON
    if (strcmp(argv[0], "--async") == 0)
        return xexec_start_async(shell, session, argc, argv);
#endif
#if XCONSOLE_SHELL_PIPE_ON
    if (strcmp(argv[0], "--pipe") == 0)
        return xexec_run_pipe(shell, session, argc, argv);
#endif
    if (!xexec_parse_redirects(argc, argv, childArgs, &childCount,
                               &stdinPath, &stdoutPath, &stderrPath, &append))
        return XConsoleResult_InvalidArgument;
#if !XCONSOLE_SHELL_REDIRECT_ON
    (void)stdinPath;
    (void)stdoutPath;
    (void)stderrPath;
    (void)append;
#endif
    process = XProcess_create();
    if (!process) return XConsoleResult_Failed;
    if (!session || !XProcess_setWorkingDirectory_utf8(process, session->currentPath)) {
        XProcess_delete_base(process);
        return XConsoleResult_Failed;
    }
#if XCONSOLE_SHELL_REDIRECT_ON
    if (stdinPath && !xexec_set_input_file(process, session, stdinPath)) {
        XProcess_delete_base(process);
        return XConsoleResult_Failed;
    }
#endif
    if (!XProcess_start_utf8(process, childArgs[0], childArgs + 1,
                             (size_t)(childCount - 1), XIODevice_ReadOnly)) {
        if (process) XProcess_delete_base(process);
        return XConsoleResult_Failed;
    }
    for (;;) {
        if (!XProcess_poll(process, 10)) {
            if (XProcess_state(process) == XProcessState_NotRunning) break;
        }
        if (shell->m_io.cancelled && shell->m_io.cancelled(shell->m_io.userData)) {
            XProcess_kill(process);
            (void)XProcess_waitForFinished(process, -1);
            XProcess_delete_base(process);
            return XConsoleResult_Cancelled;
        }
        {
            XConsoleResult rd1 = xexec_drain_channel(shell, process, session, XProcessChannel_StandardOutput,
                                                     stdoutPath, append, &stdoutStarted);
            XConsoleResult rd2 = xexec_drain_channel(shell, process, session, XProcessChannel_StandardError,
                                                     stderrPath, append, &stderrStarted);
            if (rd1 != XConsoleResult_Ok || rd2 != XConsoleResult_Ok) {
                XProcess_kill(process);
                (void)XProcess_waitForFinished(process, -1);
                XProcess_delete_base(process);
                return XConsoleResult_IoError;
            }
        }
        if (XProcess_state(process) == XProcessState_NotRunning) break;
    }
    {
        XConsoleResult rd1 = xexec_drain_channel(shell, process, session, XProcessChannel_StandardOutput,
                                                 stdoutPath, append, &stdoutStarted);
        XConsoleResult rd2 = xexec_drain_channel(shell, process, session, XProcessChannel_StandardError,
                                                 stderrPath, append, &stderrStarted);
        if (rd1 != XConsoleResult_Ok || rd2 != XConsoleResult_Ok) {
            XProcess_delete_base(process);
            return XConsoleResult_IoError;
        }
    }
    i = XProcess_exitCode(process);
    XProcess_delete_base(process);
    return i == 0 ? XConsoleResult_Ok : XConsoleResult_Failed;
}

const XConsoleCommand XConsoleShellExecutor_command = {
    "exec", NULL, "通过 XProcess 执行外部程序", "exec <program> [args...]",
    1, -1, XConsoleCommandFlag_Dangerous, xexec_run, NULL, 0, NULL
};

int XConsoleShellExecutor_execute(XConsoleShell* shell,
                                  XConsoleShellSession* session,
                                  int argc, const char* const* argv,
                                  void* userData)
{
    return xexec_run(shell, session, argc, argv, userData);
}

#else

static int xexec_unsupported(XConsoleShell* shell, XConsoleShellSession* session,
                             int argc, const char* const* argv, void* userData)
{
    (void)shell;
    (void)session;
    (void)argc;
    (void)argv;
    (void)userData;
    return XConsoleResult_NotSupported;
}

const XConsoleCommand XConsoleShellExecutor_command = {
    "exec", NULL, "外部进程功能未启用", "exec <program> [args...]", 1, -1,
    XConsoleCommandFlag_Dangerous, xexec_unsupported, NULL, 0, NULL
};

int XConsoleShellExecutor_execute(XConsoleShell* shell,
                                  XConsoleShellSession* session,
                                  int argc, const char* const* argv,
                                  void* userData)
{
    return xexec_unsupported(shell, session, argc, argv, userData);
}

#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON */
