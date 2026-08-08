/**
 * @file XProcess_win32.c
 * @brief Windows XProcess 和 XProcessEnvironment 后端。
 * @details
 * 本文件是 XinYueC 的内部 Windows 平台后端，负责 CreateProcessW、匿名管道、
 * 环境块、工作目录和进程等待；公共 XProcess 层不包含 Win32 头文件。所有
 * 进程对象和缓冲仍由 XProcess_Protected.h 规定的接口回写和释放。
 */

#include "XProcess_Protected.h"

#if XProcess_ON && defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include "XFileSystem.h"
#include "XFileDescriptor.h"
#include "XMemory.h"
#include "XRingBuffer.h"
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <wchar.h>

typedef struct XProcessWin32Backend {
    HANDLE processHandle;       /**< 子进程句柄。 */
    HANDLE threadHandle;        /**< 子进程主线程句柄。 */
    HANDLE stdinWrite;          /**< 父端写入 stdin 的句柄。 */
    HANDLE stdoutRead;          /**< 父端读取 stdout 的句柄。 */
    HANDLE stderrRead;          /**< 父端读取 stderr 的句柄。 */
    bool childExited;           /**< 是否已经取得退出码。 */
    DWORD exitCode;             /**< GetExitCodeProcess 返回的退出码。 */
    bool stdoutEof;              /**< stdout 管道已经关闭。 */
    bool stderrEof;              /**< stderr 管道已经关闭。 */
    bool finishedNotified;       /**< 是否已经通知公共状态机。 */
    XRingBuffer* stdoutBuffer;   /**< stdout 缓冲；对象拥有。 */
    XRingBuffer* stderrBuffer;   /**< stderr 缓冲；对象拥有。 */
    XProcess* outputSinkProcess; /**< 标准输出目标；借用。 */
} XProcessWin32Backend;

static bool xpw_valid_handle(HANDLE handle)
{
    return handle && handle != INVALID_HANDLE_VALUE;
}

static void xpw_close_handle(HANDLE* handle)
{
    if (handle && xpw_valid_handle(*handle)) {
        CloseHandle(*handle);
        *handle = NULL;
    }
}

static char* xpw_last_error_text(DWORD error)
{
    char* result = (char*)XMalloc_System(256);
    DWORD length;
    if (!result) return NULL;
    result[0] = '\0';
    length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            NULL, error, 0, result, 256, NULL);
    if (length == 0) {
        /* 保证错误对象始终有可读文本，即使系统没有对应消息。 */
        result[0] = 'W'; result[1] = 'i'; result[2] = 'n'; result[3] = '3';
        result[4] = '2'; result[5] = ' '; result[6] = 'e'; result[7] = 'r';
        result[8] = 'r'; result[9] = 'o'; result[10] = 'r'; result[11] = '\0';
    }
    while (length > 0 && (result[length - 1] == '\r' || result[length - 1] == '\n'))
        result[--length] = '\0';
    return result;
}

static wchar_t* xpw_utf8_to_wide(const char* text)
{
    int length;
    wchar_t* result;
    if (!text) return NULL;
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (length <= 0) length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (length <= 0) return NULL;
    result = (wchar_t*)XMalloc_System((size_t)length * sizeof(wchar_t));
    if (!result) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, 0, text, -1, result, length)) {
        XFree_System(result);
        return NULL;
    }
    return result;
}

static wchar_t* xpw_xstring_to_wide(const XString* text)
{
    return text ? xpw_utf8_to_wide(XString_toUtf8(text)) : NULL;
}

static bool xpw_append_quoted(XString* command, const char* text)
{
    const char* p;
    size_t slashCount = 0;
    bool quote = false;
    if (!command || !text) return false;
    if (!*text) quote = true;
    for (p = text; *p; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '"') {
            quote = true;
            break;
        }
    }
    if (!quote) return XString_append_utf8(command, text);
    if (!XString_append_utf8(command, "\"")) return false;
    for (p = text; *p; ++p) {
        if (*p == '\\') {
            ++slashCount;
            continue;
        }
        if (*p == '"') {
            while (slashCount > 0) {
                --slashCount;
                if (!XString_append_utf8(command, "\\\\")) return false;
            }
            if (!XString_append_utf8(command, "\\\"")) return false;
        } else {
            while (slashCount > 0) {
                --slashCount;
                if (!XString_append_utf8(command, "\\")) return false;
            }
            if (!XString_append_with_length_utf8(command, p, 1)) return false;
        }
    }
    while (slashCount > 0) {
        --slashCount;
        if (!XString_append_utf8(command, "\\\\")) return false;
    }
    return XString_append_utf8(command, "\"");
}

static wchar_t* xpw_build_command_line(const XProcess* self)
{
    XString* command;
    wchar_t* result;
    size_t i;
    if (!self || !self->m_program) return NULL;
    command = XString_create();
    if (!command) return NULL;
    if (!xpw_append_quoted(command, XString_toUtf8(self->m_program))) {
        XString_delete_base(command);
        return NULL;
    }
    for (i = 0; self->m_arguments && i < XStringList_size_base(self->m_arguments); ++i) {
        const XString* argument = XStringList_at_base(self->m_arguments, i);
        if (!XString_append_utf8(command, " ") ||
            !xpw_append_quoted(command, argument ? XString_toUtf8(argument) : "")) {
            XString_delete_base(command);
            return NULL;
        }
    }
    result = xpw_utf8_to_wide(XString_toUtf8(command));
    XString_delete_base(command);
    return result;
}

static HANDLE xpw_duplicate_inheritable(HANDLE source)
{
    HANDLE duplicate = NULL;
    if (!xpw_valid_handle(source)) return NULL;
    if (!DuplicateHandle(GetCurrentProcess(), source, GetCurrentProcess(), &duplicate,
                         0, TRUE, DUPLICATE_SAME_ACCESS))
        return NULL;
    return duplicate;
}

static HANDLE xpw_open_nul(bool write)
{
    SECURITY_ATTRIBUTES security;
    HANDLE handle;
    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = NULL;
    security.bInheritHandle = TRUE;
    handle = CreateFileW(L"NUL", write ? GENERIC_WRITE : GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, &security,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return xpw_valid_handle(handle) ? handle : NULL;
}

static HANDLE xpw_open_redirect(const XString* path, bool write, bool append)
{
#if defined(XFILE_USE_FATFS) && !defined(XFILE_USE_PLATFORM_API)
    /* FatFS 的 XFd 句柄是 FIL*，不能伪装成 CreateProcessW 所需的 HANDLE。 */
    (void)path;
    (void)write;
    (void)append;
    return NULL;
#else
    int mode;
    int error = 0;
    XFd fd;
    HANDLE source;
    HANDLE duplicate;
    if (!path || XString_isEmpty_base(path)) return NULL;
    mode = write ? (XFileSystem_WriteOnly | XFileSystem_Create |
                    (append ? XFileSystem_Append : XFileSystem_Truncate))
                 : XFileSystem_ReadOnly;
    fd = XFileSystem_open(path, mode, &error);
    if (fd == XFD_INVALID) return NULL;
    source = (HANDLE)XFd_handle(fd);
    duplicate = xpw_duplicate_inheritable(source);
    XFileSystem_close(fd);
    return duplicate;
#endif
}

static void xpw_dispose_backend(XProcessWin32Backend* backend)
{
    if (!backend) return;
    xpw_close_handle(&backend->stdinWrite);
    xpw_close_handle(&backend->stdoutRead);
    xpw_close_handle(&backend->stderrRead);
    xpw_close_handle(&backend->threadHandle);
    xpw_close_handle(&backend->processHandle);
    if (backend->stdoutBuffer) XRingBuffer_delete_base(backend->stdoutBuffer);
    if (backend->stderrBuffer) XRingBuffer_delete_base(backend->stderrBuffer);
    XFree_System(backend);
}

static bool xpw_append_environment_entry(wchar_t* target, size_t capacity,
                                          size_t* used, const char* entry)
{
    wchar_t* wide;
    size_t length;
    if (!target || !used || !entry) return false;
    wide = xpw_utf8_to_wide(entry);
    if (!wide) return false;
    length = wcslen(wide);
    if (*used + length + 1 > capacity) {
        XFree_System(wide);
        return false;
    }
    memcpy(target + *used, wide, length * sizeof(wchar_t));
    *used += length;
    target[(*used)++] = L'\0';
    XFree_System(wide);
    return true;
}

/*
 * Windows 环境变量名称不区分 ASCII 大小写；显式环境列表仍以 UTF-8
 * 保存，因此这里不能直接使用区分大小写的字符串比较。
 */
static bool xpw_utf8_environment_name_equal(const char* entry, const char* name)
{
    size_t index = 0;
    unsigned char left;
    unsigned char right;
    if (!entry || !name) return false;
    while (entry[index] && entry[index] != '=' && name[index]) {
        left = (unsigned char)entry[index];
        right = (unsigned char)name[index];
        if (left >= 'A' && left <= 'Z') left = (unsigned char)(left + ('a' - 'A'));
        if (right >= 'A' && right <= 'Z') right = (unsigned char)(right + ('a' - 'A'));
        if (left != right) return false;
        ++index;
    }
    return entry[index] == '=' && name[index] == '\0';
}

static bool xpw_explicit_environment_contains(const XStringList* list,
                                               const char* name)
{
    size_t i;
    if (!list || !name) return false;
    for (i = 0; i < XStringList_size_base(list); ++i) {
        const XString* item = XStringList_at_base(list, i);
        const char* text = item ? XString_toUtf8(item) : NULL;
        if (xpw_utf8_environment_name_equal(text, name)) return true;
    }
    return false;
}

/* 需要补齐的系统变量名称均为 ASCII，直接比较宽字符可避免临时分配。 */
static bool xpw_wide_environment_name_equal(const wchar_t* entry,
                                            const wchar_t* name)
{
    size_t index = 0;
    wchar_t left;
    wchar_t right;
    if (!entry || !name) return false;
    while (entry[index] && entry[index] != L'=' && name[index]) {
        left = entry[index];
        right = name[index];
        if (left >= L'A' && left <= L'Z') left = (wchar_t)(left + (L'a' - L'A'));
        if (right >= L'A' && right <= L'Z') right = (wchar_t)(right + (L'a' - L'A'));
        if (left != right) return false;
        ++index;
    }
    return entry[index] == L'=' && name[index] == L'\0';
}

static bool xpw_is_missing_parent_entry(const wchar_t* entry,
                                        const XStringList* explicitList)
{
    if (xpw_wide_environment_name_equal(entry, L"PATH"))
        return !xpw_explicit_environment_contains(explicitList, "PATH");
    if (xpw_wide_environment_name_equal(entry, L"SystemRoot"))
        return !xpw_explicit_environment_contains(explicitList, "SystemRoot");
    return false;
}

static bool xpw_append_wide_environment_entry(wchar_t* target, size_t capacity,
                                              size_t* used, const wchar_t* entry)
{
    size_t length;
    if (!target || !used || !entry) return false;
    length = wcslen(entry);
    if (*used + length + 1 > capacity) return false;
    memcpy(target + *used, entry, length * sizeof(wchar_t));
    *used += length;
    target[(*used)++] = L'\0';
    return true;
}

static wchar_t* xpw_build_environment(const XProcess* self, bool* borrowed)
{
    XStringList* list;
    LPWCH inherited = NULL;
    LPWCH entry;
    wchar_t* result;
    size_t count;
    size_t i;
    size_t capacity = 1;
    size_t used = 0;
    if (borrowed) *borrowed = false;
    if (!self) return NULL;
    if (XProcessEnvironment_inheritsFromParent(&self->m_environment)) {
        wchar_t* parentBlock = GetEnvironmentStringsW();
        if (parentBlock && borrowed) *borrowed = true;
        return parentBlock;
    }
    list = XProcessEnvironment_toStringList(&self->m_environment);
    if (!list) return NULL;
    count = XStringList_size_base(list);
    for (i = 0; i < count; ++i) {
        const XString* item = XStringList_at_base(list, i);
        const char* text = item ? XString_toUtf8(item) : "";
        int length = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
        if (length > 0) capacity += (size_t)length;
    }
    /*
     * 显式环境并不自动继承父环境，但 Windows 的 PATH/SystemRoot 对程序
     * 搜索和系统 DLL 加载具有基础作用。仅在调用方没有提供对应名称时，
     * 从启动时父环境快照中补齐，避免覆盖显式值或引入其他变量。
     */
    inherited = GetEnvironmentStringsW();
    if (inherited) {
        for (entry = inherited; *entry; entry += wcslen(entry) + 1) {
            if (xpw_is_missing_parent_entry(entry, list))
                capacity += wcslen(entry) + 1;
        }
    }
    result = (wchar_t*)XCalloc_System(capacity + 1, sizeof(wchar_t));
    if (result) {
        for (i = 0; i < count; ++i) {
            const XString* item = XStringList_at_base(list, i);
            if (!xpw_append_environment_entry(result, capacity + 1, &used,
                                               item ? XString_toUtf8(item) : "")) {
                XFree_System(result);
                result = NULL;
                break;
            }
        }
        if (result && inherited) {
            for (entry = inherited; *entry; entry += wcslen(entry) + 1) {
                if (xpw_is_missing_parent_entry(entry, list) &&
                    !xpw_append_wide_environment_entry(result, capacity + 1,
                                                        &used, entry)) {
                    XFree_System(result);
                    result = NULL;
                    break;
                }
            }
        }
        if (result) result[used] = L'\0';
    }
    if (inherited) FreeEnvironmentStringsW(inherited);
    XStringList_delete_base(list);
    return result;
}

static void xpw_close_child_handles(HANDLE input, HANDLE output, HANDLE error)
{
    if (xpw_valid_handle(error) && error != output) CloseHandle(error);
    if (xpw_valid_handle(output) && output != input) CloseHandle(output);
    if (xpw_valid_handle(input)) CloseHandle(input);
}

static void xpw_read_pipe(XProcess* self, XProcessWin32Backend* backend,
                          XProcessChannel channel, HANDLE* handle, bool* eof,
                          XRingBuffer* buffer)
{
    char data[XPROCESS_IO_BUFFER_SIZE];
    bool got = false;
    DWORD available = 0;
    DWORD readCount;
    if (!self || !backend || !handle || !eof || !buffer || !xpw_valid_handle(*handle)) return;
    for (;;) {
        if (!PeekNamedPipe(*handle, NULL, 0, NULL, &available, NULL)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) *eof = true;
            break;
        }
        if (available == 0) break;
        if (available > sizeof(data)) available = (DWORD)sizeof(data);
        if (!ReadFile(*handle, data, available, &readCount, NULL) || readCount == 0) {
            if (GetLastError() == ERROR_BROKEN_PIPE) *eof = true;
            break;
        }
        if (XRingBuffer_write(buffer, data, (size_t)readCount) != (size_t)readCount) {
            XProcess_backend_setError(self, XProcessError_ReadError,
                                      "process output buffer allocation failed");
            break;
        }
        got = true;
    }
    if (*eof) xpw_close_handle(handle);
    if (got) {
        if (channel == XProcessChannel_StandardOutput)
            XProcess_readyReadStandardOutput_signal(self);
        else
            XProcess_readyReadStandardError_signal(self);
        XIODevice_readyRead_signal(&self->base);
    }
}

static void xpw_notify_if_finished(XProcess* self, XProcessWin32Backend* backend)
{
    bool crashed;
    if (!self || !backend || backend->finishedNotified || !backend->childExited) return;
    /* When stdout is forwarded to another process, closing the sink write
       end immediately after the source exits lets the sink see EOF even if
       the source stderr pipe is still draining. */
    if (backend->outputSinkProcess) {
        XProcess_backend_closeWriteChannel(backend->outputSinkProcess);
        backend->outputSinkProcess = NULL;
    }
    if (xpw_valid_handle(backend->stdoutRead) || xpw_valid_handle(backend->stderrRead)) return;
    backend->finishedNotified = true;
    /* WaitForSingleObject 已确认进程退出，STILL_ACTIVE(259) 也可能是合法退出码。 */
    /* Windows 异常退出码位于 NTSTATUS 严重错误范围；不要只识别访问冲突。 */
    crashed = (backend->exitCode & 0xC0000000u) == 0xC0000000u;
    XProcess_backend_notifyFinished(self, (int)backend->exitCode,
        crashed ? XProcessExitStatus_CrashExit : XProcessExitStatus_NormalExit,
        crashed);
}

bool XProcess_backend_start(XProcess* self, XIODeviceBaseMode mode, bool detached)
{
    XProcessWin32Backend* backend;
    SECURITY_ATTRIBUTES security;
    STARTUPINFOW startup;
    PROCESS_INFORMATION processInfo;
    wchar_t* commandLine = NULL;
    wchar_t* environment = NULL;
    wchar_t* workingDirectory = NULL;
    bool environmentBorrowed = false;
    HANDLE childInput = NULL;
    HANDLE childOutput = NULL;
    HANDLE childError = NULL;
    HANDLE forwarded;
    DWORD creationFlags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP;
    bool success = false;
    bool canRead = (mode & XIODevice_ReadOnly) != 0;
    bool canWrite = (mode & XIODevice_WriteOnly) != 0;
    if (!self) return false;
    backend = (XProcessWin32Backend*)XCalloc_System(1, sizeof(*backend));
    if (!backend) return false;
    backend->stdoutBuffer = canRead ? XRingBuffer_create(XPROCESS_IO_BUFFER_SIZE) : NULL;
    backend->stderrBuffer = canRead ? XRingBuffer_create(XPROCESS_IO_BUFFER_SIZE) : NULL;
    if (canRead && (!backend->stdoutBuffer || !backend->stderrBuffer)) goto cleanup;

    security.nLength = sizeof(security);
    security.lpSecurityDescriptor = NULL;
    security.bInheritHandle = TRUE;
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    memset(&processInfo, 0, sizeof(processInfo));

    if (self->m_standardInputFile && !XString_isEmpty_base(self->m_standardInputFile)) {
        childInput = xpw_open_redirect(self->m_standardInputFile, false, false);
        if (!childInput) goto cleanup;
    } else if (!detached && canWrite &&
               self->m_inputMode == XProcessInputChannelMode_ManagedInputChannel) {
        if (!CreatePipe(&childInput, &backend->stdinWrite, &security, 0)) goto cleanup;
        if (!SetHandleInformation(backend->stdinWrite, HANDLE_FLAG_INHERIT, 0)) goto cleanup;
    } else if (!detached && canWrite) {
        forwarded = GetStdHandle(STD_INPUT_HANDLE);
        childInput = xpw_duplicate_inheritable(forwarded);
    }

    if (self->m_standardOutputProcess) {
        XProcessWin32Backend* sink = (XProcessWin32Backend*)self->m_standardOutputProcess->m_backend;
        if (self->m_standardOutputProcess->m_state != XProcessState_Running ||
            !sink || !xpw_valid_handle(sink->stdinWrite)) {
            XProcess_backend_setError(self, XProcessError_FailedToStart,
                                      "Output process is not running");
            goto cleanup;
        }
        childOutput = xpw_duplicate_inheritable(sink->stdinWrite);
        if (!childOutput) goto cleanup;
        backend->outputSinkProcess = self->m_standardOutputProcess;
    } else if (self->m_channelMode == XProcessChannelMode_ForwardedChannels ||
               self->m_channelMode == XProcessChannelMode_ForwardedOutputChannel) {
        forwarded = GetStdHandle(STD_OUTPUT_HANDLE);
        childOutput = xpw_duplicate_inheritable(forwarded);
    } else if (self->m_standardOutputFile &&
               !XString_isEmpty_base(self->m_standardOutputFile)) {
        childOutput = xpw_open_redirect(self->m_standardOutputFile, true, self->m_stdoutAppend);
        if (!childOutput) goto cleanup;
    } else if (!detached && canRead) {
        if (!CreatePipe(&backend->stdoutRead, &childOutput, &security, 0)) goto cleanup;
        if (!SetHandleInformation(backend->stdoutRead, HANDLE_FLAG_INHERIT, 0)) goto cleanup;
    }

    if (self->m_channelMode == XProcessChannelMode_MergedChannels) {
        childError = childOutput;
    } else if (self->m_channelMode == XProcessChannelMode_ForwardedChannels ||
               self->m_channelMode == XProcessChannelMode_ForwardedErrorChannel) {
        forwarded = GetStdHandle(STD_ERROR_HANDLE);
        childError = xpw_duplicate_inheritable(forwarded);
    } else if (self->m_standardErrorFile && !XString_isEmpty_base(self->m_standardErrorFile)) {
        childError = xpw_open_redirect(self->m_standardErrorFile, true, self->m_stderrAppend);
        if (!childError) goto cleanup;
    } else if (!detached && canRead) {
        if (!CreatePipe(&backend->stderrRead, &childError, &security, 0)) goto cleanup;
        if (!SetHandleInformation(backend->stderrRead, HANDLE_FLAG_INHERIT, 0)) goto cleanup;
    }

    commandLine = xpw_build_command_line(self);
    environment = xpw_build_environment(self, &environmentBorrowed);
    workingDirectory = (self->m_workingDirectory &&
                        !XString_isEmpty_base(self->m_workingDirectory))
                           ? xpw_xstring_to_wide(self->m_workingDirectory) : NULL;
    /* 无论是继承环境的快照还是显式环境块，构建失败都不能静默回退为
       CreateProcessW 的 NULL 环境指针，否则会错误继承父进程环境。 */
    if (!commandLine || !environment) goto cleanup;
    if (detached) creationFlags |= DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP;
    /* 使用 STARTF_USESTDHANDLES 时三个标准句柄都必须有效；分离启动中
       未显式重定向的通道落到 NUL，避免 Windows 返回 ERROR_INVALID_HANDLE。 */
    if (childInput || childOutput || childError) {
        if (!childInput) childInput = xpw_open_nul(false);
        if (!childOutput) childOutput = xpw_open_nul(true);
        if (!childError) childError = xpw_open_nul(true);
        if (!childInput || !childOutput || !childError) goto cleanup;
    }
    startup.hStdInput = childInput;
    startup.hStdOutput = childOutput;
    startup.hStdError = childError;
    startup.dwFlags = (childInput || childOutput || childError) ? STARTF_USESTDHANDLES : 0;
    if (!CreateProcessW(NULL, commandLine, NULL, NULL, TRUE, creationFlags,
                        environment, workingDirectory, &startup, &processInfo)) {
        char* message = xpw_last_error_text(GetLastError());
        XProcess_backend_setError(self, XProcessError_FailedToStart,
                                  message ? message : "CreateProcessW failed");
        if (message) XFree_System(message);
        goto cleanup;
    }
    backend->processHandle = processInfo.hProcess;
    backend->threadHandle = processInfo.hThread;
    self->m_backend = backend;
    self->m_processId = (XProcessId)processInfo.dwProcessId;
    success = true;
    if (detached) {
        /* 分离启动不保留任何父端句柄；子进程继续独立运行。 */
        xpw_close_child_handles(childInput, childOutput, childError);
        childInput = childOutput = childError = NULL;
        xpw_dispose_backend(backend);
        self->m_backend = NULL;
    }

cleanup:
    if (environmentBorrowed && environment) FreeEnvironmentStringsW(environment);
    else if (environment) XFree_System(environment);
    if (workingDirectory) XFree_System(workingDirectory);
    if (commandLine) XFree_System(commandLine);
    if (!success) {
        xpw_close_child_handles(childInput, childOutput, childError);
        xpw_dispose_backend(backend);
        self->m_backend = NULL;
    } else if (!detached) {
        /* CreateProcess 已复制需要的句柄，父端不再持有子端句柄。 */
        xpw_close_child_handles(childInput, childOutput, childError);
    }
    return success;
}

bool XProcess_backend_poll(XProcess* self, int timeoutMsecs)
{
    XProcessWin32Backend* backend;
    DWORD waitResult;
    if (!self || !self->m_backend) return false;
    backend = (XProcessWin32Backend*)self->m_backend;
    xpw_read_pipe(self, backend, XProcessChannel_StandardOutput,
                  &backend->stdoutRead, &backend->stdoutEof, backend->stdoutBuffer);
    xpw_read_pipe(self, backend, XProcessChannel_StandardError,
                  &backend->stderrRead, &backend->stderrEof, backend->stderrBuffer);
    if (!backend->childExited && xpw_valid_handle(backend->processHandle)) {
        waitResult = WaitForSingleObject(backend->processHandle,
                                         timeoutMsecs < 0 ? INFINITE : (DWORD)timeoutMsecs);
        if (waitResult == WAIT_OBJECT_0) {
            backend->childExited = true;
            if (!GetExitCodeProcess(backend->processHandle, &backend->exitCode))
                backend->exitCode = 1;
        }
    }
    xpw_read_pipe(self, backend, XProcessChannel_StandardOutput,
                  &backend->stdoutRead, &backend->stdoutEof, backend->stdoutBuffer);
    xpw_read_pipe(self, backend, XProcessChannel_StandardError,
                  &backend->stderrRead, &backend->stderrEof, backend->stderrBuffer);
    xpw_notify_if_finished(self, backend);
    return backend->childExited || XProcess_backend_bytesAvailable(self, self->m_readChannel) > 0;
}

int64_t XProcess_backend_bytesAvailable(const XProcess* self, XProcessChannel channel)
{
    const XProcessWin32Backend* backend;
    const XRingBuffer* buffer;
    if (!self || !self->m_backend) return 0;
    backend = (const XProcessWin32Backend*)self->m_backend;
    buffer = channel == XProcessChannel_StandardOutput ? backend->stdoutBuffer : backend->stderrBuffer;
    return buffer ? (int64_t)XRingBuffer_available(buffer) : 0;
}

int64_t XProcess_backend_bytesToWrite(const XProcess* self)
{
    const XProcessWin32Backend* backend = self ? (const XProcessWin32Backend*)self->m_backend : NULL;
    return backend && xpw_valid_handle(backend->stdinWrite) ? 0 : 0;
}

int64_t XProcess_backend_read(XProcess* self, XProcessChannel channel,
                              char* data, int64_t maxlen)
{
    XProcessWin32Backend* backend;
    XRingBuffer* buffer;
    size_t count;
    if (!self || !data || maxlen <= 0 || !self->m_backend) return -1;
    backend = (XProcessWin32Backend*)self->m_backend;
    XProcess_backend_poll(self, 0);
    buffer = channel == XProcessChannel_StandardOutput ? backend->stdoutBuffer : backend->stderrBuffer;
    if (!buffer || XRingBuffer_available(buffer) == 0) return 0;
    count = XRingBuffer_read(buffer, data, (size_t)maxlen);
    return (int64_t)count;
}

int64_t XProcess_backend_write(XProcess* self, const char* data, int64_t len)
{
    XProcessWin32Backend* backend;
    DWORD written = 0;
    DWORD request;
    if (!self || !data || len < 0 || !self->m_backend) return -1;
    backend = (XProcessWin32Backend*)self->m_backend;
    if (!xpw_valid_handle(backend->stdinWrite)) return -1;
    request = len > (int64_t)0xffffffffu ? 0xffffffffu : (DWORD)len;
    if (!WriteFile(backend->stdinWrite, data, request, &written, NULL)) {
        char* message = xpw_last_error_text(GetLastError());
        XProcess_backend_setError(self, XProcessError_WriteError,
                                  message ? message : "WriteFile failed");
        if (message) XFree_System(message);
        return -1;
    }
    return (int64_t)written;
}

bool XProcess_backend_waitForBytesWritten(XProcess* self, int msecs)
{
    XProcessWin32Backend* backend = self ? (XProcessWin32Backend*)self->m_backend : NULL;
    (void)msecs;
    return !backend || xpw_valid_handle(backend->stdinWrite);
}

void XProcess_backend_closeReadChannel(XProcess* self, XProcessChannel channel)
{
    XProcessWin32Backend* backend = self ? (XProcessWin32Backend*)self->m_backend : NULL;
    if (!backend) return;
    if (channel == XProcessChannel_StandardOutput) xpw_close_handle(&backend->stdoutRead);
    else xpw_close_handle(&backend->stderrRead);
    xpw_notify_if_finished(self, backend);
}

void XProcess_backend_closeWriteChannel(XProcess* self)
{
    XProcessWin32Backend* backend = self ? (XProcessWin32Backend*)self->m_backend : NULL;
    if (backend) xpw_close_handle(&backend->stdinWrite);
}

void XProcess_backend_terminate(XProcess* self)
{
    XProcessWin32Backend* backend = self ? (XProcessWin32Backend*)self->m_backend : NULL;
    if (!backend || !xpw_valid_handle(backend->processHandle)) return;
    if (!GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, GetProcessId(backend->processHandle)))
        (void)TerminateProcess(backend->processHandle, 1);
}

void XProcess_backend_kill(XProcess* self)
{
    XProcessWin32Backend* backend = self ? (XProcessWin32Backend*)self->m_backend : NULL;
    if (backend && xpw_valid_handle(backend->processHandle))
        (void)TerminateProcess(backend->processHandle, 1);
}

void XProcess_backend_deinit(XProcess* self)
{
    if (!self || !self->m_backend) return;
    xpw_dispose_backend((XProcessWin32Backend*)self->m_backend);
    self->m_backend = NULL;
}

bool XProcess_backend_startDetached(const XString* program,
                                    const XStringList* arguments,
                                    const XString* workingDirectory,
                                    XProcessId* pidOut)
{
    XProcess* temporary = XProcess_create();
    bool result;
    if (pidOut) *pidOut = -1;
    if (!temporary) return false;
    result = XProcess_setProgram(temporary, program) &&
             XProcess_setArguments(temporary, arguments);
    if (result && workingDirectory)
        result = XProcess_setWorkingDirectory(temporary, workingDirectory);
    if (result) {
        result = XProcess_backend_start(temporary, XIODevice_NotOpen, true);
        if (result && pidOut) *pidOut = temporary->m_processId;
    }
    XProcess_delete_base(temporary);
    return result;
}

XString* XProcess_backend_nullDevice(void)
{
    return XString_create_utf8("NUL");
}

#if XPROCESS_ENVIRONMENT_ON

XProcessEnvironment* XProcessEnvironment_platform_systemEnvironment(void)
{
    XProcessEnvironment* result = XProcessEnvironment_create();
    LPWCH block;
    LPWCH entry;
    if (!result) return NULL;
    block = GetEnvironmentStringsW();
    if (!block) return result;
    for (entry = block; *entry; entry += wcslen(entry) + 1) {
        int length = WideCharToMultiByte(CP_UTF8, 0, entry, -1, NULL, 0, NULL, NULL);
        char* utf8;
        char* equal;
        if (length <= 0) continue;
        utf8 = (char*)XMalloc_System((size_t)length);
        if (!utf8) continue;
        WideCharToMultiByte(CP_UTF8, 0, entry, -1, utf8, length, NULL, NULL);
        equal = strchr(utf8, '=');
        if (equal && equal != utf8) {
            *equal = '\0';
            (void)XProcessEnvironment_insert_utf8(result, utf8, equal + 1);
        }
        XFree_System(utf8);
    }
    FreeEnvironmentStringsW(block);
    return result;
}

#endif /* XPROCESS_ENVIRONMENT_ON */

#endif /* XProcess_ON && _WIN32 */
