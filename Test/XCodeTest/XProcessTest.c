/**
 * @file XProcessTest.c
 * @brief XProcess 公开行为与内存生命周期回归测试。
 * @details
 * 测试覆盖 QProcess 公开 API 的核心路径：环境集合、命令拆分、PATH 查找、
 * stdout/stderr、stdin EOF、文件重定向、启动失败、退出码、崩溃状态和分离启动。
 * 每个用例都在结束时释放 XString、XStringList、XByteArray 和 XProcess 对象。
 */

#include "CXinYueConfig.h"
#include "XProcessTest.h"

#if XProcess_ON

#include "XProcess.h"
#include "XProcessEnvironment.h"
#include "XString.h"
#include "XStringList.h"
#include "XByteArray.h"
#include "XDeviceFile.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <string.h>

#define XPTEST_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            XPrintf("[FAIL] XProcess: %s\n", message); \
            return false; \
        } \
    } while (0)

static XFd xprocesstest_open_file(const XString* path, int mode, int* error)
{
    XDeviceOpenOptions options;
    memset(&options, 0, sizeof(options));
    options.m_openMode = mode;
    options.m_target = path;
    return XDevice_open(XDeviceType_File, &options, error);
}

static bool XProcessTest_environment(void)
{
    XProcessEnvironment environment;
    XProcessEnvironment inherit;
    XProcessEnvironment source;
    XString* value;
    XStringList* keys;
    XProcessEnvironment_init(&environment);
    XPTEST_CHECK(XProcessEnvironment_insert_utf8(&environment, "XP_TEST", "one"),
                 "environment insert");
    XPTEST_CHECK(XProcessEnvironment_insert_utf8(&environment, "XP_TEST", "two"),
                 "environment replace");
    XPTEST_CHECK(XProcessEnvironment_contains_utf8(&environment, "XP_TEST"),
                 "environment contains");
    value = XProcessEnvironment_value_utf8(&environment, "XP_TEST", "missing");
    XPTEST_CHECK(value && XString_equals_utf8(value, "two", XChar_CaseSensitive),
                 "environment value");
    XString_delete_base(value);
    keys = XProcessEnvironment_keys(&environment);
    XPTEST_CHECK(keys && XStringList_size_base(keys) == 1,
                 "environment keys");
    XStringList_delete_base(keys);
    XPTEST_CHECK(XProcessEnvironment_remove_utf8(&environment, "XP_TEST"),
                 "environment remove");
    XProcessEnvironment_initInherit(&inherit);
    XProcessEnvironment_clear(&inherit);
    XPTEST_CHECK(XProcessEnvironment_inheritsFromParent(&inherit),
                 "inherit environment clear keeps inherit flag");
    XPTEST_CHECK(XProcessEnvironment_insert_utf8(&inherit, "XP_TEST", "inherit"),
                 "inherit environment insert");
    XProcessEnvironment_clear(&inherit);
    XPTEST_CHECK(!XProcessEnvironment_inheritsFromParent(&inherit),
                 "explicit environment clear keeps explicit flag");
    XProcessEnvironment_deinit(&inherit);

    XProcessEnvironment_init(&source);
    XPTEST_CHECK(XProcessEnvironment_insert_utf8(&source, "XP_TEST_SOURCE", "merged") &&
                     XProcessEnvironment_insertEnvironment(&environment, &source),
                 "environment bulk insert");
    XPTEST_CHECK(XProcessEnvironment_insert_utf8(&environment, "XP_TEST_LOCAL", "local"),
                 "environment local insert");
    XPTEST_CHECK(!XProcessEnvironment_equals(&environment, &source),
                 "environment inequality");
    XProcessEnvironment_swap(&environment, &source);
    XPTEST_CHECK(XProcessEnvironment_contains_utf8(&environment, "XP_TEST_SOURCE") &&
                     XProcessEnvironment_contains_utf8(&source, "XP_TEST_LOCAL"),
                 "environment swap");
    XProcessEnvironment_swap(&environment, &source);
    value = XProcessEnvironment_value_utf8(&environment, "XP_TEST_SOURCE", "");
    XPTEST_CHECK(value && XString_equals_utf8(value, "merged", XChar_CaseSensitive),
                 "environment bulk insert value");
    XString_delete_base(value);
    XProcessEnvironment_deinit(&source);
    XProcessEnvironment_deinit(&environment);
    return true;
}

static bool XProcessTest_splitCommand(void)
{
    XString* command = XString_create_utf8("tool \"hello world\" \"\"\"quoted\"\"\"");
    XStringList* parts = XProcess_splitCommand_static(command);
    XPTEST_CHECK(command && parts && XStringList_size_base(parts) == 3,
                 "split command count");
    XPTEST_CHECK(XString_equals_utf8(XStringList_at_base(parts, 1), "hello world",
                                     XChar_CaseSensitive),
                 "split command quoted argument");
    XPTEST_CHECK(XString_equals_utf8(XStringList_at_base(parts, 2), "\"quoted\"",
                                     XChar_CaseSensitive),
                 "split command escaped quote");
    XStringList_delete_base(parts);
    XString_delete_base(command);
    return true;
}

static bool XProcessTest_output(void)
{
    const char* arguments[] = { "-c", "printf out; printf err >&2; exit 7" };
    XProcess* process = XProcess_create();
    XByteArray* output;
    XByteArray* error;
    XPTEST_CHECK(process && XProcess_start_utf8(process, "sh", arguments, 2,
                                                XIODevice_ReadOnly),
                 "start shell output");
    XPTEST_CHECK(XProcess_waitForFinished(process, 3000), "wait shell output");
    output = XProcess_readAllStandardOutput(process);
    error = XProcess_readAllStandardError(process);
    XPTEST_CHECK(output && error &&
                     XByteArray_size_base(output) == 3 &&
                     memcmp(XByteArray_data(output), "out", 3) == 0,
                 "stdout content");
    XPTEST_CHECK(XByteArray_size_base(error) == 3 &&
                     memcmp(XByteArray_data(error), "err", 3) == 0,
                 "stderr content");
    XPTEST_CHECK(XProcess_exitCode(process) == 7 &&
                     XProcess_exitStatus(process) == XProcessExitStatus_NormalExit,
                 "exit result");
    XByteArray_delete_base(output);
    XByteArray_delete_base(error);
    XProcess_delete_base(process);
    return true;
}

static bool XProcessTest_environmentAndWorkingDirectory(void)
{
    const char* environmentArguments[] = { "-c", "printf \"$XPROCESS_ENV_TEST\"" };
    const char* setEnvironmentArguments[] = {
        "-c", "printf \"$XPROCESS_SET_ENV_TEST\""
    };
    const char* directoryArguments[] = { "-c", "pwd" };
    XProcessEnvironment environment;
    XProcess* process = XProcess_create();
    XByteArray* output;
    XStringList* systemEnvironment;
    XStringList* environmentList;
    XPTEST_CHECK(process != NULL, "environment process create");
    XProcessEnvironment_init(&environment);
    XPTEST_CHECK(XProcessEnvironment_insert_utf8(&environment, "XPROCESS_ENV_TEST", "environment-ok"),
                 "environment variable insert");
    XPTEST_CHECK(XProcess_setProcessEnvironment(process, &environment),
                 "set process environment");
    XProcessEnvironment_deinit(&environment);
    XPTEST_CHECK(XProcess_start_utf8(process, "sh", environmentArguments, 2,
                                     XIODevice_ReadOnly), "start environment process");
    XPTEST_CHECK(XProcess_waitForFinished(process, 3000), "wait environment process");
    output = XProcess_readAllStandardOutput(process);
    XPTEST_CHECK(output && XByteArray_size_base(output) == 14 &&
                 memcmp(XByteArray_data(output), "environment-ok", 14) == 0,
                 "environment child value");
    XByteArray_delete_base(output);
    XProcess_delete_base(process);

    process = XProcess_create();
    environmentList = XStringList_create();
    XPTEST_CHECK(process && environmentList, "set environment process create");
    XStringList_push_back_utf8(environmentList, "XPROCESS_SET_ENV_TEST=set-environment");
    XStringList_push_back_utf8(environmentList, "invalid-entry");
    XPTEST_CHECK(XStringList_size_base(environmentList) == 2 &&
                     XProcess_setEnvironment(process, environmentList) &&
                     XProcess_start_utf8(process, "sh", setEnvironmentArguments, 2,
                                         XIODevice_ReadOnly),
                 "set environment name value list");
    XPTEST_CHECK(XProcess_waitForFinished(process, 3000), "wait set environment process");
    output = XProcess_readAllStandardOutput(process);
    XPTEST_CHECK(output && XByteArray_size_base(output) == 15 &&
                     memcmp(XByteArray_data(output), "set-environment", 15) == 0,
                 "set environment child value");
    XByteArray_delete_base(output);
    XStringList_delete_base(environmentList);
    XProcess_delete_base(process);

    process = XProcess_create();
    XPTEST_CHECK(process && XProcess_setWorkingDirectory_utf8(process, "/tmp") &&
                 XProcess_start_utf8(process, "sh", directoryArguments, 2,
                                     XIODevice_ReadOnly), "start working directory process");
    XPTEST_CHECK(XProcess_waitForFinished(process, 3000), "wait working directory process");
    output = XProcess_readAllStandardOutput(process);
    XPTEST_CHECK(output && XByteArray_size_base(output) >= 5 &&
                 memcmp(XByteArray_data(output), "/tmp", 4) == 0,
                 "working directory child value");
    XByteArray_delete_base(output);
    XProcess_delete_base(process);

    systemEnvironment = XProcess_systemEnvironment_static();
    XPTEST_CHECK(systemEnvironment && XStringList_size_base(systemEnvironment) > 0,
                 "system environment enumeration");
    XStringList_delete_base(systemEnvironment);
    return true;
}

static bool XProcessTest_restart(void)
{
    const char* arguments[] = { "-c", "printf reused" };
    XProcess* process = XProcess_create();
    size_t i;
    XPTEST_CHECK(process && XProcess_setProgram_utf8(process, "sh") &&
                     XProcess_setArguments_utf8(process, arguments, 2),
                 "restart process configure");
    XPTEST_CHECK(XProcess_open(process, XIODevice_ReadOnly), "open starts configured process");
    XPTEST_CHECK(XProcess_waitForFinished(process, 3000), "open process wait");
    XPTEST_CHECK(XProcess_bytesAvailable_base(process) == 6,
                 "process bytesAvailable reports backend output");
    {
        XByteArray* output = XProcess_readAllStandardOutput(process);
        XPTEST_CHECK(output && XByteArray_size_base(output) == 6 &&
                         memcmp(XByteArray_data(output), "reused", 6) == 0,
                     "open process output");
        XByteArray_delete_base(output);
    }
    for (i = 0; i < 64; ++i) {
        XByteArray* output;
        XPTEST_CHECK(XProcess_start_2(process, XIODevice_ReadOnly), "restart process start");
        XPTEST_CHECK(XProcess_waitForFinished(process, 3000), "restart process wait");
        output = XProcess_readAllStandardOutput(process);
        XPTEST_CHECK(output && XByteArray_size_base(output) == 6 &&
                         memcmp(XByteArray_data(output), "reused", 6) == 0,
                     "restart process output");
        XByteArray_delete_base(output);
    }
    XPTEST_CHECK(XProcess_started_signal(process) != NULL &&
                     XProcess_finished_signal(process, 0, XProcessExitStatus_NormalExit) != NULL &&
                     XProcess_errorOccurred_signal(process, XProcessError_UnknownError) != NULL &&
                     XProcess_stateChanged_signal(process, XProcessState_NotRunning) != NULL &&
                     XProcess_readyReadStandardOutput_signal(process) != NULL &&
                     XProcess_readyReadStandardError_signal(process) != NULL,
                 "signal return value");
    XProcess_delete_base(process);
    return true;
}

static bool XProcessTest_stdin(void)
{
    const char* arguments[] = { "-c", "cat" };
    XProcess* process = XProcess_create();
    XByteArray* output;
    const char text[] = "stdin-data";
    XPTEST_CHECK(process && XProcess_start_utf8(process, "sh", arguments, 2,
                                                XIODevice_ReadWrite),
                 "start cat");
    XPTEST_CHECK(XIODevice_write_1(&process->base, text, sizeof(text) - 1) ==
                     (int64_t)(sizeof(text) - 1), "write stdin");
    XProcess_closeWriteChannel(process);
    XPTEST_CHECK(XProcess_waitForFinished(process, 3000), "wait cat");
    output = XProcess_readAllStandardOutput(process);
    XPTEST_CHECK(output && XByteArray_size_base(output) == sizeof(text) - 1 &&
                     memcmp(XByteArray_data(output), text, sizeof(text) - 1) == 0,
                 "cat output");
    XByteArray_delete_base(output);
    XProcess_delete_base(process);
    return true;
}

static bool XProcessTest_nullArguments(void)
{
    XProcess* process = XProcess_create();
    XString* program = XString_create_utf8("true");
    bool result = process && program &&
                  XProcess_start(process, program, NULL, XIODevice_ReadOnly) &&
                  XProcess_waitForFinished(process, 3000) &&
                  XProcess_exitCode(process) == 0;
    if (program) XString_delete_base(program);
    if (process) XProcess_delete_base(process);
    return result;
}

/**
 * @brief 验证进程的读写打开模式会限制对应的公共 I/O 操作。
 * @details
 * ReadOnly 不创建可写标准输入通道，WriteOnly 不创建可读输出通道，避免
 * 嵌入式目标为不会使用的方向分配管道和环形缓冲。
 */
static bool XProcessTest_openModes(void)
{
    const char* arguments[] = { "-c", "printf mode-ok" };
    XProcess* process = XProcess_create();
    char output[16];
    bool result = false;
    if (!process) return false;

    if (!XProcess_start_utf8(process, "sh", arguments, 2, XIODevice_ReadOnly)) goto cleanup;
    if (XIODevice_write_1(&process->base, "x", 1) >= 0) goto cleanup;
    if (!XProcess_waitForFinished(process, 3000)) goto cleanup;
    XProcess_delete_base(process);
    process = XProcess_create();
    if (!process) return false;
    if (!XProcess_start_utf8(process, "sh", arguments, 2, XIODevice_WriteOnly)) goto cleanup;
    if (XIODevice_read_1(&process->base, output, sizeof(output)) >= 0) goto cleanup;
    if (!XProcess_waitForFinished(process, 3000)) goto cleanup;
    result = XIODevice_bytesAvailable_base(&process->base) == 0;

cleanup:
    if (process) XProcess_delete_base(process);
    return result;
}

/**
 * @brief 验证大块标准输入在管道背压下仍能完整传输。
 * @details
 * 输入数据故意超过常见匿名管道容量；POSIX 后端必须先接受到库缓冲，再由
 * poll/写入冲刷，不能把短写或 EAGAIN 暴露为一次性写入失败。
 */
static bool XProcessTest_largeStdin(void)
{
    const char* arguments[] = { "-c", "cat" };
    const size_t dataSize = 256u * 1024u;
    XProcess* process = XProcess_create();
    XByteArray* input = XByteArray_create();
    XByteArray* output = NULL;
    size_t i;
    bool result = false;
    if (!process || !input || !XByteArray_resize_base(input, dataSize)) goto cleanup;
    for (i = 0; i < dataSize; ++i) XByteArray_data(input)[i] = (uint8_t)(i * 31u + 7u);
    if (!XProcess_start_utf8(process, "sh", arguments, 2, XIODevice_ReadWrite)) goto cleanup;
    if (XIODevice_write_1(&process->base, (const char*)XByteArray_data(input),
                          (int64_t)dataSize) != (int64_t)dataSize) goto cleanup;
    XProcess_closeWriteChannel(process);
    if (!XProcess_waitForFinished(process, 10000)) goto cleanup;
    output = XProcess_readAllStandardOutput(process);
    result = output && XByteArray_size_base(output) == dataSize &&
             memcmp(XByteArray_data(output), XByteArray_data(input), dataSize) == 0;
cleanup:
    if (output) XByteArray_delete_base(output);
    if (input) XByteArray_delete_base(input);
    if (process) XProcess_delete_base(process);
    return result;
}

static bool XProcessTest_outputProcess(void)
{
    const char* sinkArguments[] = { "-c", "cat" };
    const char* sourceArguments[] = { "-c", "printf piped" };
    XProcess* sink = XProcess_create();
    XProcess* source = XProcess_create();
    XByteArray* output;
    XPTEST_CHECK(sink && source && XProcess_start_utf8(sink, "sh", sinkArguments, 2,
                                                       XIODevice_ReadWrite),
                 "start pipe sink");
    XPTEST_CHECK(XProcess_setStandardOutputProcess(source, sink), "set output process");
    XPTEST_CHECK(XProcess_start_utf8(source, "sh", sourceArguments, 2,
                                     XIODevice_ReadOnly), "start pipe source");
    XPTEST_CHECK(XProcess_waitForFinished(source, 3000), "wait pipe source");
    XPTEST_CHECK(XProcess_waitForFinished(sink, 3000), "wait pipe sink");
    output = XProcess_readAllStandardOutput(sink);
    XPTEST_CHECK(output && XByteArray_size_base(output) == 5 &&
                     memcmp(XByteArray_data(output), "piped", 5) == 0,
                 "pipe output");
    XByteArray_delete_base(output);
    XProcess_delete_base(source);
    XProcess_delete_base(sink);

    /* 管道目标提前销毁后，源对象必须解除借用关系而不是解引用悬空指针。 */
    sink = XProcess_create();
    source = XProcess_create();
    XPTEST_CHECK(sink && source && XProcess_setStandardOutputProcess(source, sink),
                 "set pipe lifetime relation");
    XProcess_delete_base(sink);
    sink = NULL;
    XPTEST_CHECK(XProcess_start_utf8(source, "sh", sourceArguments, 2,
                                     XIODevice_ReadOnly) &&
                     XProcess_waitForFinished(source, 3000),
                 "pipe source survives destination destroy");
    XProcess_delete_base(source);
    return true;
}

static bool XProcessTest_redirect(void)
{
#if defined(XFILE_USE_FATFS) && !defined(XFILE_USE_PLATFORM_API)
    /* 子进程运行在宿主系统，无法通过 XDeviceFile 访问 FatFS 虚拟卷。 */
    XPrintf("[SKIP] XProcess 文件重定向（FatFS 虚拟卷不对宿主子进程开放）\n");
    return true;
#else
    XString* path = XString_create_utf8("xprocess_redirect_test.txt");
    XProcess* process = XProcess_create();
    const char* arguments[] = { "-c", "printf redirected" };
    int error = 0;
    XFd fd;
    char buffer[32] = { 0 };
    int64_t n;
    XPTEST_CHECK(path && process && XProcess_setStandardOutputFile_utf8(process,
                    "xprocess_redirect_test.txt", false) &&
                     XProcess_start_utf8(process, "sh", arguments, 2,
                                         XIODevice_ReadOnly), "redirect start");
    XPTEST_CHECK(XProcess_waitForFinished(process, 3000), "redirect wait");
    fd = xprocesstest_open_file(path, XDeviceFile_ReadOnly, &error);
    XPTEST_CHECK(fd != XFD_INVALID, "redirect open");
    n = XDeviceFile_read(fd, buffer, sizeof(buffer) - 1);
    XDeviceFile_close(fd);
    XPTEST_CHECK(n == 10 && memcmp(buffer, "redirected", 10) == 0, "redirect content");
    XProcess_delete_base(process);
    XDeviceFile_removePermanent(path);
    XString_delete_base(path);
    return true;
#endif
}

static bool XProcessTest_failures(void)
{
    XProcess* process = XProcess_create();
    bool started;
    if (!process) return false;
    started = XProcess_start_utf8(process, "/definitely/not/a/process", NULL, 0,
                                   XIODevice_ReadOnly);
    if (!started) {
        XPrintf("[FAIL] XProcess: 启动失败用例未建立错误握手\n");
        XProcess_delete_base(process);
        return false;
    }
    XProcess_poll(process, 1000);
    if (XProcess_state(process) != XProcessState_NotRunning ||
        XProcess_error(process) != XProcessError_FailedToStart) {
        XPrintf("[FAIL] XProcess: 启动失败状态未收敛\n");
        XProcess_delete_base(process);
        return false;
    }
    XProcess_delete_base(process);
    return true;
}

static bool XProcessTest_detached(void)
{
    const char* arguments[] = { "-c", "exit 0" };
    XString* program = XString_create_utf8("sh");
    XStringList* list = XStringList_create();
    XProcessId pid = -1;
    bool result;
    XStringList_push_back_utf8(list, "-c");
    XStringList_push_back_utf8(list, "exit 0");
    result = XProcess_startDetached_static(program, list, NULL, &pid);
    (void)arguments;
    XPTEST_CHECK(result && pid > 0, "detached start");
    XStringList_delete_base(list);
    XString_delete_base(program);
    return true;
}

static bool XProcessTest_unixParameters(void)
{
    const char* arguments[] = { "-c", "kill -PIPE $$; printf survived" };
    XProcessUnixProcessParameters parameters;
    XProcess* process = XProcess_create();
    XByteArray* output;
    memset(&parameters, 0, sizeof(parameters));
    parameters.flags = XProcessUnixProcessFlag_IgnoreSigPipe |
                       XProcessUnixProcessFlag_CloseFileDescriptors;
    parameters.lowestFileDescriptorToClose = 3;
    XPTEST_CHECK(process && XProcess_setUnixProcessParameters(process, &parameters) &&
                     XProcess_start_utf8(process, "sh", arguments, 2, XIODevice_ReadOnly),
                 "unix process parameters start");
    XPTEST_CHECK(XProcess_waitForFinished(process, 3000), "unix process parameters wait");
    output = XProcess_readAllStandardOutput(process);
    XPTEST_CHECK(output && XByteArray_size_base(output) == 8 &&
                     memcmp(XByteArray_data(output), "survived", 8) == 0,
                 "ignore SIGPIPE parameter");
    XByteArray_delete_base(output);
    XProcess_delete_base(process);
    return true;
}

static bool XProcessTest_lifecycleStress(void)
{
    size_t i;
    for (i = 0; i < 10000; ++i) {
        XProcess* process = XProcess_create();
        XPTEST_CHECK(process != NULL, "lifecycle stress create");
        XProcess_delete_base(process);
    }
    return true;
}

#if defined(_WIN32)

/* Windows 测试使用系统自带 cmd.exe，避免把 POSIX shell 命令带入 Win32 后端。 */
static bool XProcessTest_windowsCommand(XProcess* process,
                                        const char* command,
                                        XIODeviceBaseMode mode,
                                        XByteArray** output,
                                        XByteArray** error)
{
    const char* arguments[] = { "/D", "/C", command };
    if (output) *output = NULL;
    if (error) *error = NULL;
    if (!process || !command || !XProcess_start_utf8(process, "cmd.exe",
                                                       arguments, 3, mode) ||
        !XProcess_waitForFinished(process, 3000))
        return false;
    if (output) *output = XProcess_readAllStandardOutput(process);
    if (error) *error = XProcess_readAllStandardError(process);
    return true;
}

static bool XProcessTest_windowsRunAll(void)
{
    XProcess* process;
    XByteArray* output;
    XByteArray* error;
    XProcessEnvironment environment;
    XStringList* environmentList;
    XString* redirectPath = NULL;
    XFd fd;
    int fdError = 0;
    char fileData[64] = { 0 };
    int64_t fileSize;
    size_t i;
    bool result = false;

    process = XProcess_create();
    if (!process) return false;
    if (!XProcessTest_windowsCommand(process,
            "echo out& echo err 1>&2& exit /b 7", XIODevice_ReadOnly,
            &output, &error)) goto cleanup_process;
    if (!output || !error || XByteArray_size_base(output) != 5 ||
        memcmp(XByteArray_data(output), "out\r\n", 5) != 0 ||
        XByteArray_size_base(error) != 6 ||
        memcmp(XByteArray_data(error), "err \r\n", 6) != 0 ||
        XProcess_exitCode(process) != 7) {
        if (output) XByteArray_delete_base(output);
        if (error) XByteArray_delete_base(error);
        goto cleanup_process;
    }
    XByteArray_delete_base(output);
    XByteArray_delete_base(error);
    XProcess_delete_base(process);
    process = NULL;

    process = XProcess_create();
    XProcessEnvironment_init(&environment);
    if (!process || !XProcessEnvironment_insert_utf8(&environment,
            "XPROCESS_ENV_TEST", "environment-ok") ||
        !XProcess_setProcessEnvironment(process, &environment)) {
        XProcessEnvironment_deinit(&environment);
        goto cleanup_process;
    }
    XProcessEnvironment_deinit(&environment);
    if (!XProcessTest_windowsCommand(process, "echo %XPROCESS_ENV_TEST%",
                                     XIODevice_ReadOnly, &output, NULL) ||
        !output || XByteArray_size_base(output) != 16 ||
        memcmp(XByteArray_data(output), "environment-ok\r\n", 16) != 0) {
        if (output) XByteArray_delete_base(output);
        goto cleanup_process;
    }
    XByteArray_delete_base(output);
    XProcess_delete_base(process);
    process = NULL;

    process = XProcess_create();
    environmentList = XStringList_create();
    if (!process || !environmentList) {
        if (environmentList) XStringList_delete_base(environmentList);
        goto cleanup_process;
    }
    XStringList_push_back_utf8(environmentList,
                               "XPROCESS_SET_ENV_TEST=set-environment");
    XStringList_push_back_utf8(environmentList, "invalid-entry");
    if (!XProcess_setEnvironment(process, environmentList) ||
        !XProcessTest_windowsCommand(process, "echo %XPROCESS_SET_ENV_TEST%",
                                     XIODevice_ReadOnly, &output, NULL) ||
        !output || XByteArray_size_base(output) != 17 ||
        memcmp(XByteArray_data(output), "set-environment\r\n", 17) != 0) {
        if (output) XByteArray_delete_base(output);
        if (environmentList) XStringList_delete_base(environmentList);
        goto cleanup_process;
    }
    XByteArray_delete_base(output);
    XStringList_delete_base(environmentList);
    XProcess_delete_base(process);
    process = NULL;

    process = XProcess_create();
    if (!process || !XProcess_setWorkingDirectory_utf8(process, ".") ||
        !XProcessTest_windowsCommand(process, "cd", XIODevice_ReadOnly,
                                     &output, NULL) || !output ||
        XByteArray_size_base(output) < 3) {
        if (output) XByteArray_delete_base(output);
        goto cleanup_process;
    }
    XByteArray_delete_base(output);
    XProcess_delete_base(process);
    process = NULL;

    process = XProcess_create();
    if (!process || !XProcess_setProgram_utf8(process, "cmd.exe"))
        goto cleanup_process;
    {
        const char* arguments[] = { "/D", "/C", "echo reused" };
        if (!XProcess_setArguments_utf8(process, arguments, 3))
            goto cleanup_process;
    }
    for (i = 0; i < 64; ++i) {
        if (!XProcess_start_2(process, XIODevice_ReadOnly) ||
            !XProcess_waitForFinished(process, 3000))
            goto cleanup_process;
        output = XProcess_readAllStandardOutput(process);
        if (!output || XByteArray_size_base(output) != 8 ||
            memcmp(XByteArray_data(output), "reused\r\n", 8) != 0) {
            if (output) XByteArray_delete_base(output);
            goto cleanup_process;
        }
        XByteArray_delete_base(output);
    }
    XProcess_delete_base(process);
    process = NULL;

    process = XProcess_create();
    if (!process) goto cleanup_process;
    {
        const char* arguments[] = { "/D", "/C", "sort" };
        if (!XProcess_start_utf8(process, "cmd.exe", arguments, 3,
                                 XIODevice_ReadWrite) ||
            XIODevice_write_1(&process->base, "stdin-data\r\n", 12) != 12) {
            goto cleanup_process;
        }
    }
    XProcess_closeWriteChannel(process);
    if (!XProcess_waitForFinished(process, 3000)) goto cleanup_process;
    output = XProcess_readAllStandardOutput(process);
    if (!output || XByteArray_size_base(output) != 12 ||
        memcmp(XByteArray_data(output), "stdin-data\r\n", 12) != 0) {
        if (output) XByteArray_delete_base(output);
        goto cleanup_process;
    }
    XByteArray_delete_base(output);
    XProcess_delete_base(process);
    process = NULL;

    {
        const char* sinkArguments[] = { "/D", "/C", "more" };
        const char* sourceArguments[] = { "/D", "/C", "echo piped" };
        XProcess* sink = XProcess_create();
        XProcess* source = XProcess_create();
        if (!sink || !source) goto cleanup_process;
        if (!XProcess_start_utf8(sink, "cmd.exe", sinkArguments, 3,
                                 XIODevice_ReadWrite) ||
            !XProcess_setStandardOutputProcess(source, sink) ||
            !XProcess_start_utf8(source, "cmd.exe", sourceArguments, 3,
                                 XIODevice_ReadOnly)) {
            if (source) XProcess_delete_base(source);
            if (sink) XProcess_delete_base(sink);
            goto cleanup_process;
        }
        if (!XProcess_waitForFinished(source, 3000)) {
            XProcess_delete_base(source);
            XProcess_delete_base(sink);
            goto cleanup_process;
        }
        if (!XProcess_waitForFinished(sink, 3000)) {
            XProcess_delete_base(source);
            XProcess_delete_base(sink);
            goto cleanup_process;
        }
        output = XProcess_readAllStandardOutput(sink);
        if (!output || XByteArray_size_base(output) != 9 ||
            memcmp(XByteArray_data(output), "piped\r\n\r\n", 9) != 0) {
            if (output) XByteArray_delete_base(output);
            XProcess_delete_base(source);
            XProcess_delete_base(sink);
            goto cleanup_process;
        }
        XByteArray_delete_base(output);
        XProcess_delete_base(source);
        XProcess_delete_base(sink);
    }

    redirectPath = XString_create_utf8("xprocess_redirect_test.txt");
    process = XProcess_create();
    if (!redirectPath || !process ||
        !XProcess_setStandardOutputFile_utf8(process,
            "xprocess_redirect_test.txt", false))
        goto cleanup_redirect;
    {
        const char* arguments[] = { "/D", "/C", "echo redirected" };
        if (!XProcess_start_utf8(process, "cmd.exe", arguments, 3,
                                 XIODevice_ReadOnly) ||
            !XProcess_waitForFinished(process, 3000))
            goto cleanup_redirect;
    }
    XProcess_delete_base(process);
    process = NULL;
    fd = xprocesstest_open_file(redirectPath, XDeviceFile_ReadOnly, &fdError);
    fileSize = fd == XFD_INVALID ? -1 : XDeviceFile_read(fd, fileData, sizeof(fileData) - 1);
    if (fd != XFD_INVALID) XDeviceFile_close(fd);
    if (fileSize != 12 || memcmp(fileData, "redirected\r\n", 12) != 0)
        goto cleanup_redirect;
    XDeviceFile_removePermanent(redirectPath);
    XString_delete_base(redirectPath);
    redirectPath = NULL;

    process = XProcess_create();
    if (!process || XProcess_start_utf8(process, "xprocess-command-does-not-exist.exe",
                                        NULL, 0, XIODevice_ReadOnly) ||
        XProcess_state(process) != XProcessState_NotRunning ||
        XProcess_error(process) != XProcessError_FailedToStart)
        goto cleanup_process;
    XProcess_delete_base(process);
    process = NULL;

    {
        XString* program = XString_create_utf8("cmd.exe");
        XStringList* arguments = XStringList_create();
        XProcessId pid = -1;
        if (!program || !arguments) {
            if (program) XString_delete_base(program);
            if (arguments) XStringList_delete_base(arguments);
            goto cleanup_process;
        }
        XStringList_push_back_utf8(arguments, "/D");
        XStringList_push_back_utf8(arguments, "/C");
        XStringList_push_back_utf8(arguments, "exit 0");
        if (!XProcess_startDetached_static(program, arguments, NULL, &pid) || pid <= 0) {
            if (program) XString_delete_base(program);
            if (arguments) XStringList_delete_base(arguments);
            goto cleanup_process;
        }
        XString_delete_base(program);
        XStringList_delete_base(arguments);
    }
    result = true;

cleanup_process:
    if (process) XProcess_delete_base(process);
    if (!result && redirectPath) {
        XDeviceFile_removePermanent(redirectPath);
        XString_delete_base(redirectPath);
    }
    return result;

cleanup_redirect:
    if (process) XProcess_delete_base(process);
    if (redirectPath) {
        XDeviceFile_removePermanent(redirectPath);
        XString_delete_base(redirectPath);
    }
    return false;
}

#endif /* _WIN32 */

bool XProcessTest_runAll(void)
{
#if defined(XFILE_USE_FATFS)
    /* FatFs 面向无操作系统嵌入式设备，不提供宿主进程启动与管道后端。 */
    XPrintf("[SKIP] XProcess 全量测试（FatFs 无进程后端）\n");
    return true;
#elif !XPROCESS_ENVIRONMENT_ON
    /* 环境子模块关闭时，环境相关用例不具备前置条件；构建验证仍由此入口完成。 */
    {
        XProcess* process = XProcess_create();
        XProcessUnixProcessParameters parameters;
        memset(&parameters, 0, sizeof(parameters));
        if (!process) return false;
#if !XPROCESS_REDIRECT_ON
        if (XProcess_setStandardInputFile_utf8(process, "disabled-input")) {
            XProcess_delete_base(process);
            return false;
        }
#endif
#if !XPROCESS_PIPE_ON
        if (XProcess_setStandardOutputProcess(process, process)) {
            XProcess_delete_base(process);
            return false;
        }
#endif
#if !XPROCESS_DETACHED_ON
        if (XProcess_startDetached(process, NULL)) {
            XProcess_delete_base(process);
            return false;
        }
#endif
#if !XPROCESS_UNIX_PARAMETERS_ON
        if (XProcess_setUnixProcessParameters(process, &parameters)) {
            XProcess_delete_base(process);
            return false;
        }
#endif
#if !XPROCESS_SIGNAL_ON
        if (XProcess_started_signal(process) != NULL) {
            XProcess_delete_base(process);
            return false;
        }
#endif
        XProcess_delete_base(process);
    }
    XPrintf("[SKIP] XProcess 全量测试（环境子模块关闭）\n");
    return true;
#else
    bool result;
#if defined(_WIN32)
    result = XProcessTest_environment() &&
             XProcessTest_splitCommand() &&
             XProcessTest_windowsRunAll() &&
             XProcessTest_lifecycleStress();
#else
    result = XProcessTest_environment() &&
             XProcessTest_splitCommand() &&
             XProcessTest_output() &&
             XProcessTest_environmentAndWorkingDirectory() &&
             XProcessTest_restart() &&
             XProcessTest_stdin() &&
             XProcessTest_nullArguments() &&
             XProcessTest_openModes() &&
             XProcessTest_largeStdin() &&
             XProcessTest_outputProcess() &&
             XProcessTest_redirect() &&
             XProcessTest_failures() &&
             XProcessTest_detached() &&
             XProcessTest_unixParameters() &&
             XProcessTest_lifecycleStress();
#endif
    XPrintf("[%s] XProcess 全量测试\n", result ? "PASS" : "FAIL");
    return result;
#endif
}

#else

/* XProcess 被总开关裁剪时，保留测试入口以便测试菜单无需条件分支。 */
bool XProcessTest_runAll(void)
{
    return true;
}

#endif /* XProcess_ON */
