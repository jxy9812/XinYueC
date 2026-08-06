/**
 * @file XProcess_posix.c
 * @brief POSIX XProcess 和 XProcessEnvironment 后端。
 * @details
 * 本文件是 XinYueC 的内部平台后端，负责 pipe/fork/exec/wait 的系统边界；
 * 公共 XProcess 核心不包含这些头文件。文件重定向通过 XFileSystem 公共
 * API 打开，进程状态、错误和输出缓冲通过 XProcess_Protected.h 回写；系统
 * 环境枚举也在这里转换为 XProcessEnvironment 公共对象，避免拆分同一平台
 * 后端的源文件。
 */

#include "XProcess_Protected.h"

#if XProcess_ON && (defined(__linux__) || defined(__APPLE__) || defined(__BSD__))

#include "XFileSystem.h"
#include "XFileDescriptor.h"
#include "XMemory.h"
#include "XByteArray.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct XProcessPosixBackend {
    pid_t pid;                  /**< 当前子进程 ID。 */
    int stdinFd;                /**< 父端写入端；-1 表示关闭或不管理。 */
    int outputSinkFd;           /**< 输出目标 stdin 的子端副本。 */
    int stdoutFd;               /**< 父端 stdout 读端。 */
    int stderrFd;               /**< 父端 stderr 读端。 */
    int startupFd;              /**< exec 错误握手读端。 */
    bool childExited;           /**< waitpid 已得到退出状态。 */
    int waitStatus;             /**< waitpid 返回的原始状态。 */
    bool stdoutEof;              /**< stdout 管道已到 EOF。 */
    bool stderrEof;              /**< stderr 管道已到 EOF。 */
    XByteArray* stdoutBuffer;    /**< stdout 缓冲；对象拥有。 */
    XByteArray* stderrBuffer;    /**< stderr 缓冲；对象拥有。 */
    XProcess* outputSinkProcess; /**< 借用目标；源进程结束时关闭其 stdin。 */
} XProcessPosixBackend;

typedef struct XProcessDetachedMessage {
    int32_t type;                /**< 0 为孙进程 PID，1 为错误码。 */
    int32_t value;               /**< PID 或 errno。 */
} XProcessDetachedMessage;

static int xpp_set_nonblocking(int fd)
{
    int flags;
    if (fd < 0) return -1;
    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void xpp_close_fd(int* fd)
{
    if (fd && *fd >= 0) {
        close(*fd);
        *fd = -1;
    }
}

static void xpp_free_string_array(char** values)
{
    size_t i;
    if (!values) return;
    for (i = 0; values[i]; ++i) XFree_System(values[i]);
    XFree_System(values);
}

static char** xpp_build_argv(const XProcess* self)
{
    size_t count = self && self->m_arguments ? XStringList_size_base(self->m_arguments) : 0;
    size_t i;
    char** argv = (char**)XCalloc_System(count + 2, sizeof(char*));
    if (!argv) return NULL;
    argv[0] = XMemory_strdup(self && self->m_program ? XString_toUtf8(self->m_program) : "");
    if (!argv[0]) {
        xpp_free_string_array(argv);
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        const XString* arg = XStringList_at_base(self->m_arguments, i);
        argv[i + 1] = XMemory_strdup(arg ? XString_toUtf8(arg) : "");
        if (!argv[i + 1]) {
            xpp_free_string_array(argv);
            return NULL;
        }
    }
    return argv;
}

static char** xpp_build_envp(const XProcess* self)
{
    XProcessEnvironment* env = NULL;
    XStringList* list = NULL;
    char** result = NULL;
    size_t count, i;
    if (!self) return NULL;
    env = XProcessEnvironment_inheritsFromParent(&self->m_environment)
            ? XProcessEnvironment_systemEnvironment()
            : XProcessEnvironment_createCopy(&self->m_environment);
    if (!env) return NULL;
    list = XProcessEnvironment_toStringList(env);
    XProcessEnvironment_delete(env);
    if (!list) return NULL;
    count = XStringList_size_base(list);
    result = (char**)XCalloc_System(count + 1, sizeof(char*));
    if (!result) {
        XStringList_delete_base(list);
        return NULL;
    }
    for (i = 0; i < count; ++i) {
        const XString* item = XStringList_at_base(list, i);
        result[i] = XMemory_strdup(item ? XString_toUtf8(item) : "");
        if (!result[i]) {
            xpp_free_string_array(result);
            XStringList_delete_base(list);
            return NULL;
        }
    }
    XStringList_delete_base(list);
    return result;
}

static int xpp_open_redirect(const XString* path, bool write, bool append)
{
    int mode;
    int error = 0;
    XFd fd;
    if (!path || XString_isEmpty_base(path)) return -1;
    if (write) {
        mode = XFileSystem_WriteOnly | XFileSystem_Create |
               (append ? XFileSystem_Append : XFileSystem_Truncate);
    } else {
        mode = XFileSystem_ReadOnly;
    }
    fd = XFileSystem_open(path, mode, &error);
    if (fd == XFD_INVALID) return -1;
    /* XFileSystem_open 返回的是拥有者句柄；复制底层 fd 后立即释放
       XFd 包装，避免进程后端绕过库的生命周期管理造成 fd 表泄漏。 */
    {
        int raw = (int)(intptr_t)XFd_handle(fd);
        int duplicate = raw >= 0 ? dup(raw) : -1;
        XFileSystem_close(fd);
        return duplicate;
    }
}

static void xpp_dispose_backend(XProcessPosixBackend* backend)
{
    if (!backend) return;
    xpp_close_fd(&backend->stdinFd);
    xpp_close_fd(&backend->outputSinkFd);
    xpp_close_fd(&backend->stdoutFd);
    xpp_close_fd(&backend->stderrFd);
    xpp_close_fd(&backend->startupFd);
    if (backend->stdoutBuffer) XByteArray_delete_base(backend->stdoutBuffer);
    if (backend->stderrBuffer) XByteArray_delete_base(backend->stderrBuffer);
    XFree_System(backend);
}

static void xpp_close_pipe_set(int pipes[3][2])
{
    size_t i;
    if (!pipes) return;
    for (i = 0; i < 3; ++i) {
        xpp_close_fd(&pipes[i][0]);
        xpp_close_fd(&pipes[i][1]);
    }
}

static void xpp_child_fail(int errorFd, int error, bool detached)
{
    if (detached) {
        XProcessDetachedMessage message;
        message.type = 1;
        message.value = error;
        (void)write(errorFd, &message, sizeof(message));
    } else {
        (void)write(errorFd, &error, sizeof(error));
    }
    _exit(127);
}

static void xpp_child_dup_or_exit(int source, int target, int errorFd, bool detached)
{
    if (source >= 0 && source != target && dup2(source, target) < 0) {
        xpp_child_fail(errorFd, errno, detached);
    }
}

static void xpp_child_close_extra_fds(int lowest, int keep)
{
    long limit;
    int fd;
    if (lowest < 3) lowest = 3;
    limit = sysconf(_SC_OPEN_MAX);
    if (limit < lowest) limit = 1024;
    if (limit > 1048576) limit = 1048576;
    for (fd = lowest; fd < limit; ++fd)
        if (fd != keep) (void)close(fd);
}

static void xpp_child_apply_unix_parameters(const XProcess* self, int startupWrite)
{
    uint32_t flags;
    int signo;
    if (!self) return;
    flags = self->m_unixParameters.flags;
#ifdef NSIG
    if (flags & XProcessUnixProcessFlag_ResetSignalHandlers) {
        for (signo = 1; signo < NSIG; ++signo) {
            if (signo == SIGKILL || signo == SIGSTOP) continue;
            (void)signal(signo, SIG_DFL);
        }
    }
#endif
    if (flags & XProcessUnixProcessFlag_IgnoreSigPipe)
        (void)signal(SIGPIPE, SIG_IGN);
    if (flags & (XProcessUnixProcessFlag_CreateNewSession |
                 XProcessUnixProcessFlag_DisconnectControllingTerminal))
        (void)setsid();
    if (flags & XProcessUnixProcessFlag_ResetIds) {
        (void)setgid(getgid());
        (void)setuid(getuid());
    }
}

static const char* xpp_environment_path(char** envp)
{
    size_t i;
    if (!envp) return NULL;
    for (i = 0; envp[i]; ++i)
        if (strncmp(envp[i], "PATH=", 5) == 0) return envp[i] + 5;
    return NULL;
}

static void xpp_exec_search(const char* program, char* const* argv, char* const* envp)
{
    const char* path;
    const char* begin;
    const char* end;
    char candidate[PATH_MAX];
    size_t directoryLength;
    size_t programLength;
    int savedError = ENOENT;

    if (!program || !*program) {
        errno = ENOENT;
        return;
    }
    if (strchr(program, '/')) {
        execve(program, argv, envp);
        return;
    }

    path = xpp_environment_path((char**)envp);
    if (!path) path = "/bin:/usr/bin";
    programLength = strlen(program);
    begin = path;
    for (;;) {
        end = strchr(begin, ':');
        directoryLength = end ? (size_t)(end - begin) : strlen(begin);
        if (directoryLength == 0) directoryLength = 1;
        if (directoryLength + 1 + programLength < sizeof(candidate)) {
            if (end && end == begin) candidate[0] = '.';
            else if (!end && begin[0] == '\0') candidate[0] = '.';
            else memcpy(candidate, begin, directoryLength);
            if (begin[0] == '\0') candidate[0] = '.';
            candidate[directoryLength] = '/';
            memcpy(candidate + directoryLength + 1, program, programLength + 1);
            execve(candidate, argv, envp);
            if (errno != ENOENT && errno != ENOTDIR) savedError = errno;
        } else {
            savedError = ENAMETOOLONG;
        }
        if (!end) break;
        begin = end + 1;
    }
    errno = savedError;
}

static void xpp_child_close(int fd, int keep0, int keep1, int keep2, int keep3)
{
    if (fd >= 0 && fd != keep0 && fd != keep1 && fd != keep2 && fd != keep3)
        close(fd);
}

static void xpp_exec_child(XProcess* self, XProcessPosixBackend* backend,
                           char** argv, char** envp, int startupWrite,
                           int inputFd, int outputFd, int errorFd,
                           int parentIn, int parentOut, int parentErr,
                           bool detached)
{
    const char* program = XString_toUtf8(self->m_program);
    const XString* directory = self->m_workingDirectory;
    if (directory && !XString_isEmpty_base(directory) &&
        chdir(XString_toUtf8(directory)) < 0) {
        xpp_child_fail(startupWrite, errno, detached);
    }
    if (detached || self->m_unixParameters.flags)
        xpp_child_apply_unix_parameters(self, startupWrite);
    if (detached)
        (void)setsid();
    if (detached) {
        pid_t grandchild = fork();
        if (grandchild < 0) xpp_child_fail(startupWrite, errno, true);
        if (grandchild > 0) {
            XProcessDetachedMessage message;
            message.type = 0;
            message.value = (int32_t)grandchild;
            (void)write(startupWrite, &message, sizeof(message));
            _exit(0);
        }
    }
    xpp_child_dup_or_exit(inputFd, STDIN_FILENO, startupWrite, detached);
    xpp_child_dup_or_exit(outputFd, STDOUT_FILENO, startupWrite, detached);
    xpp_child_dup_or_exit(errorFd, STDERR_FILENO, startupWrite, detached);
    xpp_child_close(parentIn, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, startupWrite);
    xpp_child_close(parentOut, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, startupWrite);
    xpp_child_close(parentErr, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, startupWrite);
    xpp_child_close(backend->stdinFd, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, startupWrite);
    xpp_child_close(backend->outputSinkFd, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, startupWrite);
    xpp_child_close(backend->stdoutFd, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, startupWrite);
    xpp_child_close(backend->stderrFd, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, startupWrite);
    if (self->m_unixParameters.flags & XProcessUnixProcessFlag_CloseFileDescriptors)
        xpp_child_close_extra_fds(self->m_unixParameters.lowestFileDescriptorToClose,
                                  startupWrite);
    xpp_exec_search(program, argv, envp);
    xpp_child_fail(startupWrite, errno, detached);
}

static bool xpp_check_startup(XProcess* self, XProcessPosixBackend* backend)
{
    struct pollfd pfd;
    int error = 0;
    ssize_t n;
    if (!backend || backend->startupFd < 0) return true;
    pfd.fd = backend->startupFd;
    pfd.events = POLLIN | POLLHUP;
    if (poll(&pfd, 1, 0) <= 0) return true;
    n = read(backend->startupFd, &error, sizeof(error));
    xpp_close_fd(&backend->startupFd);
    if (n == 0) return true; /* close-on-exec means exec succeeded. */
    XProcess_backend_setError(self, XProcessError_FailedToStart,
                              strerror(error ? error : EIO));
    return false;
}

static bool xpp_make_pipes(XProcess* self, XProcessPosixBackend* backend,
                           XIODeviceBaseMode mode, bool detached, int pipes[3][2],
                           int* inputFile, int* outputFile, int* errorFile,
                           XFd* inputHandle, XFd* outputHandle, XFd* errorHandle)
{
    bool canRead = (mode & XIODevice_ReadOnly) != 0;
    bool canWrite = (mode & XIODevice_WriteOnly) != 0;
    size_t i;
    for (i = 0; i < 3; ++i) pipes[i][0] = pipes[i][1] = -1;
    *inputFile = *outputFile = *errorFile = -1;
    *inputHandle = *outputHandle = *errorHandle = XFD_INVALID;
    if (self->m_standardInputFile && !XString_isEmpty_base(self->m_standardInputFile)) {
        *inputFile = xpp_open_redirect(self->m_standardInputFile, false, false);
        if (*inputFile < 0) return false;
    } else if (!detached && self->m_inputMode == XProcessInputChannelMode_ManagedInputChannel) {
        if (pipe(pipes[0]) < 0) return false;
        backend->stdinFd = pipes[0][1];
    }
    if (self->m_standardOutputFile && !XString_isEmpty_base(self->m_standardOutputFile)) {
        *outputFile = xpp_open_redirect(self->m_standardOutputFile, true, self->m_stdoutAppend);
        if (*outputFile < 0) return false;
    } else if (!detached && (self->m_channelMode == XProcessChannelMode_SeparateChannels ||
               self->m_channelMode == XProcessChannelMode_MergedChannels)) {
        if (pipe(pipes[1]) < 0) return false;
        backend->stdoutFd = pipes[1][0];
    }
    if (self->m_channelMode == XProcessChannelMode_SeparateChannels &&
        self->m_standardErrorFile && !XString_isEmpty_base(self->m_standardErrorFile)) {
        *errorFile = xpp_open_redirect(self->m_standardErrorFile, true, self->m_stderrAppend);
        if (*errorFile < 0) return false;
    } else if (!detached && self->m_channelMode == XProcessChannelMode_SeparateChannels) {
        if (pipe(pipes[2]) < 0) return false;
        backend->stderrFd = pipes[2][0];
    }
    (void)inputHandle;
    (void)outputHandle;
    (void)errorHandle;
    (void)canRead;
    (void)canWrite;
    return true;
}

bool XProcess_backend_start(XProcess* self, XIODeviceBaseMode mode, bool detached)
{
    XProcessPosixBackend* backend;
    char** argv = NULL;
    char** envp = NULL;
    int pipes[3][2] = {{-1, -1}, {-1, -1}, {-1, -1}};
    int startup[2] = {-1, -1};
    int inputFile = -1;
    int outputFile = -1;
    int errorFile = -1;
    XFd inputHandle = XFD_INVALID;
    XFd outputHandle = XFD_INVALID;
    XFd errorHandle = XFD_INVALID;
    pid_t pid;
    size_t i;
    if (!self) return false;
    backend = (XProcessPosixBackend*)XCalloc_System(1, sizeof(*backend));
    if (!backend) return false;
    backend->pid = -1;
    backend->stdinFd = backend->stdoutFd = backend->stderrFd = -1;
    backend->outputSinkFd = -1;
    backend->startupFd = -1;
    backend->outputSinkProcess = NULL;
    backend->stdoutBuffer = XByteArray_create();
    backend->stderrBuffer = XByteArray_create();
    if (!backend->stdoutBuffer || !backend->stderrBuffer ||
        !xpp_make_pipes(self, backend, mode, detached, pipes, &inputFile, &outputFile, &errorFile,
                        &inputHandle, &outputHandle, &errorHandle)) {
        xpp_close_pipe_set(pipes);
        xpp_close_fd(&inputFile);
        xpp_close_fd(&outputFile);
        xpp_close_fd(&errorFile);
        backend->stdinFd = backend->stdoutFd = backend->stderrFd = -1;
        xpp_dispose_backend(backend);
        return false;
    }
    if (self->m_standardOutputProcess) {
        XProcessPosixBackend* sink =
            (XProcessPosixBackend*)self->m_standardOutputProcess->m_backend;
        if (self->m_standardOutputProcess->m_state != XProcessState_Running ||
            !sink || sink->stdinFd < 0) {
            XProcess_backend_setError(self, XProcessError_FailedToStart,
                                      "Output process is not running");
            xpp_close_pipe_set(pipes);
            xpp_close_fd(&inputFile);
            xpp_close_fd(&outputFile);
            xpp_close_fd(&errorFile);
            backend->stdinFd = backend->stdoutFd = backend->stderrFd = -1;
            xpp_dispose_backend(backend);
            return false;
        }
        backend->outputSinkFd = dup(sink->stdinFd);
        if (backend->outputSinkFd < 0) {
            XProcess_backend_setError(self, XProcessError_FailedToStart,
                                      "Failed to connect output process");
            xpp_close_pipe_set(pipes);
            xpp_close_fd(&inputFile);
            xpp_close_fd(&outputFile);
            xpp_close_fd(&errorFile);
            backend->stdinFd = backend->stdoutFd = backend->stderrFd = -1;
            xpp_dispose_backend(backend);
            return false;
        }
        backend->outputSinkProcess = self->m_standardOutputProcess;
    }
    argv = xpp_build_argv(self);
    envp = xpp_build_envp(self);
    if (!argv || !envp || pipe(startup) < 0) {
        xpp_free_string_array(argv);
        xpp_free_string_array(envp);
        xpp_close_pipe_set(pipes);
        xpp_close_fd(&inputFile);
        xpp_close_fd(&outputFile);
        xpp_close_fd(&errorFile);
        backend->stdinFd = backend->stdoutFd = backend->stderrFd = -1;
        xpp_dispose_backend(backend);
        return false;
    }
    (void)fcntl(startup[1], F_SETFD, FD_CLOEXEC);
    pid = fork();
    if (pid < 0) {
        xpp_close_fd(&startup[0]);
        xpp_close_fd(&startup[1]);
        xpp_free_string_array(argv);
        xpp_free_string_array(envp);
        xpp_close_pipe_set(pipes);
        xpp_close_fd(&inputFile);
        xpp_close_fd(&outputFile);
        xpp_close_fd(&errorFile);
        backend->stdinFd = backend->stdoutFd = backend->stderrFd = -1;
        xpp_dispose_backend(backend);
        return false;
    }
    if (pid == 0) {
        int childInput = inputFile >= 0 ? inputFile : pipes[0][0];
        int childOutput = outputFile >= 0 ? outputFile : pipes[1][1];
        int childError = errorFile >= 0 ? errorFile : pipes[2][1];
        if (self->m_channelMode == XProcessChannelMode_ForwardedChannels ||
            self->m_channelMode == XProcessChannelMode_ForwardedOutputChannel)
            childOutput = -1;
        if (self->m_channelMode == XProcessChannelMode_ForwardedChannels ||
            self->m_channelMode == XProcessChannelMode_ForwardedErrorChannel)
            childError = -1;
        if (self->m_channelMode == XProcessChannelMode_MergedChannels)
            childError = childOutput;
        if (backend->outputSinkFd >= 0) {
            childOutput = backend->outputSinkFd;
            if (self->m_channelMode == XProcessChannelMode_MergedChannels)
                childError = childOutput;
        }
        if (self->m_inputMode == XProcessInputChannelMode_ForwardedInputChannel)
            childInput = -1;
        xpp_exec_child(self, backend, argv, envp, startup[1], childInput,
                       childOutput, childError, pipes[0][1], pipes[1][0], pipes[2][0],
                       detached);
        _exit(127);
    }
    backend->pid = pid;
    backend->startupFd = startup[0];
    xpp_close_fd(&startup[1]);
    if (pipes[0][0] >= 0) xpp_close_fd(&pipes[0][0]);
    if (pipes[0][1] >= 0 && backend->stdinFd != pipes[0][1]) xpp_close_fd(&pipes[0][1]);
    if (pipes[1][1] >= 0) xpp_close_fd(&pipes[1][1]);
    if (pipes[1][0] >= 0 && backend->stdoutFd != pipes[1][0]) xpp_close_fd(&pipes[1][0]);
    if (pipes[2][1] >= 0) xpp_close_fd(&pipes[2][1]);
    if (pipes[2][0] >= 0 && backend->stderrFd != pipes[2][0]) xpp_close_fd(&pipes[2][0]);
    if (inputFile >= 0) close(inputFile);
    if (outputFile >= 0) close(outputFile);
    if (errorFile >= 0) close(errorFile);
    xpp_close_fd(&backend->outputSinkFd);
    xpp_free_string_array(argv);
    xpp_free_string_array(envp);
    if (backend->stdinFd >= 0) (void)xpp_set_nonblocking(backend->stdinFd);
    if (backend->stdoutFd >= 0) (void)xpp_set_nonblocking(backend->stdoutFd);
    if (backend->stderrFd >= 0) (void)xpp_set_nonblocking(backend->stderrFd);
    self->m_backend = backend;
    self->m_processId = (XProcessId)pid;
    if (detached) {
        struct pollfd startupPoll;
        XProcessDetachedMessage message;
        ssize_t messageSize;
        startupPoll.fd = backend->startupFd;
        startupPoll.events = POLLIN | POLLHUP;
        if (poll(&startupPoll, 1, 1000) <= 0 ||
            (messageSize = read(backend->startupFd, &message, sizeof(message))) !=
                (ssize_t)sizeof(message) || message.type != 0 || message.value <= 0) {
            (void)kill(pid, SIGKILL);
            (void)waitpid(pid, NULL, 0);
            XProcess_backend_setError(self, XProcessError_FailedToStart,
                                      "Detached process handshake failed");
            XProcess_backend_deinit(self);
            self->m_processId = 0;
            return false;
        }
        (void)waitpid(pid, NULL, 0);
        backend->pid = (pid_t)message.value;
        self->m_processId = (XProcessId)message.value;
        /* 等待孙进程 exec 的 close-on-exec 确认；失败时读取第二条错误消息。 */
        startupPoll.fd = backend->startupFd;
        startupPoll.events = POLLIN | POLLHUP;
        if (poll(&startupPoll, 1, 1000) > 0) {
            XProcessDetachedMessage failure;
            messageSize = read(backend->startupFd, &failure, sizeof(failure));
            if (messageSize == (ssize_t)sizeof(failure) && failure.type == 1) {
                XProcess_backend_setError(self, XProcessError_FailedToStart,
                                          strerror(failure.value));
                (void)kill((pid_t)message.value, SIGKILL);
                (void)waitpid((pid_t)message.value, NULL, 0);
                XProcess_backend_deinit(self);
                self->m_processId = 0;
                return false;
            }
        }
        xpp_dispose_backend(backend);
        self->m_backend = NULL;
        return true;
    }
    if (!xpp_check_startup(self, backend)) {
        kill(pid, SIGKILL);
        (void)waitpid(pid, NULL, 0);
        XProcess_backend_deinit(self);
        self->m_backend = NULL;
        self->m_processId = 0;
        return false;
    }
    for (i = 0; i < 3; ++i) {
        if (pipes[i][0] >= 0 && ((i == 0 && backend->stdinFd != pipes[i][0]) ||
                                 (i == 1 && backend->stdoutFd != pipes[i][0]) ||
                                 (i == 2 && backend->stderrFd != pipes[i][0])))
            close(pipes[i][0]);
    }
    return true;
}

static void xpp_read_fd(XProcess* self, XProcessPosixBackend* backend,
                        XProcessChannel channel, int* fd, bool* eof,
                        XByteArray* target)
{
    char buffer[XPROCESS_IO_BUFFER_SIZE];
    bool got = false;
    ssize_t n;
    if (!fd || !eof || *fd < 0 || !target) return;
    for (;;) {
        n = read(*fd, buffer, sizeof(buffer));
        if (n > 0) {
            XByteArray_append_2(target, buffer, (size_t)n);
            got = true;
            continue;
        }
        if (n == 0) {
            *eof = true;
            xpp_close_fd(fd);
        }
        break;
    }
    if (got) {
        if (channel == XProcessChannel_StandardOutput)
            XProcess_readyReadStandardOutput_signal(self);
        else
            XProcess_readyReadStandardError_signal(self);
        XIODevice_readyRead_signal(&self->base);
    }
    (void)backend;
}

bool XProcess_backend_poll(XProcess* self, int timeoutMsecs)
{
    XProcessPosixBackend* backend;
    struct pollfd fds[3];
    int count = 0;
    int pollResult;
    int status;
    if (!self || !self->m_backend) return false;
    backend = (XProcessPosixBackend*)self->m_backend;
    if (backend->startupFd >= 0 && !xpp_check_startup(self, backend)) {
        if (backend->pid > 0) {
            (void)kill(backend->pid, SIGKILL);
            (void)waitpid(backend->pid, NULL, 0);
        }
        XProcess_backend_deinit(self);
        self->m_processId = 0;
        return false;
    }
    if (backend->stdoutFd >= 0) { fds[count].fd = backend->stdoutFd; fds[count].events = POLLIN | POLLHUP; ++count; }
    if (backend->stderrFd >= 0) { fds[count].fd = backend->stderrFd; fds[count].events = POLLIN | POLLHUP; ++count; }
    pollResult = count ? poll(fds, (nfds_t)count, timeoutMsecs < 0 ? -1 : timeoutMsecs) : 0;
    (void)pollResult;
    xpp_read_fd(self, backend, XProcessChannel_StandardOutput, &backend->stdoutFd,
                &backend->stdoutEof, backend->stdoutBuffer);
    xpp_read_fd(self, backend, XProcessChannel_StandardError, &backend->stderrFd,
                &backend->stderrEof, backend->stderrBuffer);
    if (backend->startupFd >= 0 && !xpp_check_startup(self, backend)) {
        if (backend->pid > 0) {
            (void)kill(backend->pid, SIGKILL);
            (void)waitpid(backend->pid, NULL, 0);
        }
        XProcess_backend_deinit(self);
        self->m_processId = 0;
        return false;
    }
    if (!backend->childExited && backend->pid > 0) {
        pid_t result = waitpid(backend->pid, &status, WNOHANG);
        if (result == backend->pid) {
            backend->childExited = true;
            backend->waitStatus = status;
        }
    }
    if (backend->childExited && backend->stdoutFd < 0 && backend->stderrFd < 0) {
        bool crashed = WIFSIGNALED(backend->waitStatus);
        int exitCode = WIFEXITED(backend->waitStatus) ? WEXITSTATUS(backend->waitStatus) : -1;
        if (backend->outputSinkProcess) {
            XProcess_backend_closeWriteChannel(backend->outputSinkProcess);
            backend->outputSinkProcess = NULL;
        }
        XProcess_backend_notifyFinished(self, exitCode,
            crashed ? XProcessExitStatus_CrashExit : XProcessExitStatus_NormalExit,
            crashed);
    }
    return pollResult > 0 || backend->childExited;
}

int64_t XProcess_backend_bytesAvailable(const XProcess* self, XProcessChannel channel)
{
    const XProcessPosixBackend* backend;
    const XByteArray* buffer;
    if (!self || !self->m_backend) return 0;
    backend = (const XProcessPosixBackend*)self->m_backend;
    buffer = channel == XProcessChannel_StandardOutput ? backend->stdoutBuffer : backend->stderrBuffer;
    return buffer ? (int64_t)XByteArray_size_base(buffer) : 0;
}

int64_t XProcess_backend_bytesToWrite(const XProcess* self)
{
    const XProcessPosixBackend* backend = self ? (const XProcessPosixBackend*)self->m_backend : NULL;
    return backend && backend->stdinFd >= 0 ? 0 : 0;
}

int64_t XProcess_backend_read(XProcess* self, XProcessChannel channel,
                              char* data, int64_t maxlen)
{
    XProcessPosixBackend* backend;
    XByteArray* buffer;
    size_t count;
    if (!self || !data || maxlen <= 0 || !self->m_backend) return -1;
    backend = (XProcessPosixBackend*)self->m_backend;
    XProcess_backend_poll(self, 0);
    buffer = channel == XProcessChannel_StandardOutput ? backend->stdoutBuffer : backend->stderrBuffer;
    if (!buffer || XByteArray_size_base(buffer) == 0) return 0;
    count = XByteArray_size_base(buffer);
    if (count > (size_t)maxlen) count = (size_t)maxlen;
    memcpy(data, XByteArray_data(buffer), count);
    if (count < XByteArray_size_base(buffer))
        memmove(XByteArray_data(buffer), XByteArray_data(buffer) + count,
                XByteArray_size_base(buffer) - count);
    XByteArray_resize_base(buffer, XByteArray_size_base(buffer) - count);
    return (int64_t)count;
}

int64_t XProcess_backend_write(XProcess* self, const char* data, int64_t len)
{
    XProcessPosixBackend* backend;
    ssize_t n;
    if (!self || !data || len < 0 || !self->m_backend) return -1;
    backend = (XProcessPosixBackend*)self->m_backend;
    if (backend->stdinFd < 0) return -1;
    n = write(backend->stdinFd, data, (size_t)len);
    if (n < 0) {
        XProcess_backend_setError(self, XProcessError_WriteError, strerror(errno));
        return -1;
    }
    return (int64_t)n;
}

bool XProcess_backend_waitForBytesWritten(XProcess* self, int msecs)
{
    XProcessPosixBackend* backend = self ? (XProcessPosixBackend*)self->m_backend : NULL;
    struct pollfd pfd;
    if (!backend || backend->stdinFd < 0) return true;
    pfd.fd = backend->stdinFd;
    pfd.events = POLLOUT;
    return poll(&pfd, 1, msecs < 0 ? -1 : msecs) > 0;
}

void XProcess_backend_closeReadChannel(XProcess* self, XProcessChannel channel)
{
    XProcessPosixBackend* backend = self ? (XProcessPosixBackend*)self->m_backend : NULL;
    if (!backend) return;
    if (channel == XProcessChannel_StandardOutput) xpp_close_fd(&backend->stdoutFd);
    else xpp_close_fd(&backend->stderrFd);
}

void XProcess_backend_closeWriteChannel(XProcess* self)
{
    XProcessPosixBackend* backend = self ? (XProcessPosixBackend*)self->m_backend : NULL;
    if (backend) xpp_close_fd(&backend->stdinFd);
}

void XProcess_backend_terminate(XProcess* self)
{
    XProcessPosixBackend* backend = self ? (XProcessPosixBackend*)self->m_backend : NULL;
    if (backend && backend->pid > 0) (void)kill(backend->pid, SIGTERM);
}

void XProcess_backend_kill(XProcess* self)
{
    XProcessPosixBackend* backend = self ? (XProcessPosixBackend*)self->m_backend : NULL;
    if (backend && backend->pid > 0) (void)kill(backend->pid, SIGKILL);
}

void XProcess_backend_deinit(XProcess* self)
{
    if (!self || !self->m_backend) return;
    xpp_dispose_backend((XProcessPosixBackend*)self->m_backend);
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
    if (result && workingDirectory) result = XProcess_setWorkingDirectory(temporary, workingDirectory);
    if (result) {
        /* 后端成功时已释放监听状态，但核心仍保留刚创建的 PID。 */
        result = XProcess_backend_start(temporary, XIODevice_NotOpen, true);
        if (result && pidOut) *pidOut = temporary->m_processId;
    }
    XProcess_delete_base(temporary);
    return result;
}

XString* XProcess_backend_nullDevice(void)
{
    return XString_create_utf8("/dev/null");
}

#if XPROCESS_ENVIRONMENT_ON

extern char** environ;

XProcessEnvironment* XProcessEnvironment_platform_systemEnvironment(void)
{
    XProcessEnvironment* result = XProcessEnvironment_create();
    char** entry;
    if (!result) return NULL;
    for (entry = environ; entry && *entry; ++entry) {
        const char* equal = (*entry)[0] ? strchr(*entry, '=') : NULL;
        if (equal && equal != *entry) {
            XString* name = XString_create_with_length_utf8(*entry,
                (size_t)(equal - *entry));
            XString* value = XString_create_utf8(equal + 1);
            if (name && value) {
                (void)XProcessEnvironment_insert(result, name, value);
            }
            if (name) XString_delete_base(name);
            if (value) XString_delete_base(value);
        }
    }
    return result;
}

#endif /* XPROCESS_ENVIRONMENT_ON */

#endif /* XProcess_ON && POSIX */
