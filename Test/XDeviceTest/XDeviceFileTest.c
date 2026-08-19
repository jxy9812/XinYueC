#include "XDeviceFileTest.h"
#include "CXinYueConfig.h"
#include "XDevice.h"
#include "XDeviceFile.h"
#include "XAbstractNetIoRing.h"
#include "XFileInfo.h"
#include "XString.h"
#include "XThread.h"
#include "XVariant.h"
#include "XVarList.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#if (defined(__unix__) || defined(__APPLE__) || defined(__BSD__)) && \
    defined(XFILE_USE_PLATFORM_API)
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static int XDeviceFileTest_sharedMemoryServer(const XString* name)
{
    static const char payload[] = "XDeviceFile shared mapping";
    XFd fd = XFD_INVALID;
    void* address = NULL;
    char acknowledgement = 0;
    char signal = 1;
    int result = 1;

    /* fork 后不复用父进程的 io_uring，信令读取退化为内核阻塞路径。 */
    XAbstractNetIoRing_setGlobal(NULL);
    fd = XDeviceFile_openSharedMemory(name, true, 256, NULL);
    if (fd == XFD_INVALID) goto cleanup;
    address = XDeviceFile_map(fd, 0, 256, 0x2);
    if (!address) goto cleanup;
    memset(address, 0, 256);
    memcpy(address, payload, sizeof(payload));
    if (XDevice_write(fd, &signal, 1) != 1 ||
        XDevice_read(fd, &acknowledgement, 1) != 1 || acknowledgement != 1)
        goto cleanup;
    result = 0;

cleanup:
    if (address) (void)XDeviceFile_unmap(fd, address, 256);
    if (fd != XFD_INVALID) XDevice_close(fd);
    return result;
}

static bool XDeviceFileTest_sharedMemory(void)
{
    static const char payload[] = "XDeviceFile shared mapping";
    XString* name;
    XFd fd = XFD_INVALID;
    void* address = NULL;
    pid_t child;
    int status = 0;
    int attempt;
    char signal = 0;
    char acknowledgement = 0;
    bool ok = false;

    name = XString_create_utf8("xdevicefile-shared-mapping-test");
    if (!name) return false;
    child = fork();
    if (child == 0)
        _exit(XDeviceFileTest_sharedMemoryServer(name));
    if (child < 0) goto cleanup;

    for (attempt = 0; attempt < 100; ++attempt) {
        fd = XDeviceFile_openSharedMemory(name, false, 0, NULL);
        if (fd != XFD_INVALID) break;
        XThread_msleep(20);
    }
    if (fd == XFD_INVALID) {
        (void)kill(child, SIGTERM);
        goto wait_child;
    }
    address = XDeviceFile_map(fd, 0, 256, 0);
    if (!address || XDevice_read(fd, &signal, 1) != 1 || signal != 1 ||
        memcmp(address, payload, sizeof(payload)) != 0)
        goto acknowledge;
    acknowledgement = 1;
    ok = true;

acknowledge:
    (void)XDevice_write(fd, &acknowledgement, 1);
    if (address) {
        (void)XDeviceFile_unmap(fd, address, 256);
        address = NULL;
    }
    XDevice_close(fd);
    fd = XFD_INVALID;

wait_child:
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        ok = false;

cleanup:
    if (address) (void)XDeviceFile_unmap(fd, address, 256);
    if (fd != XFD_INVALID) XDevice_close(fd);
    XString_delete_base((XClass*)name);
    return ok;
}
#endif

bool XDeviceFileTest_runAll(void)
{
#if !XFILE_ON
    puts("XDeviceFile test: SKIP");
    return true;
#else
#if defined(_WIN32)
    /* 避免向 C: 根目录写文件；普通用户可在当前工作目录执行回归测试。 */
    static const char testPathText[] = "xdevicefile-platform-test.bin";
#elif defined(XFILE_USE_FATFS)
    static const char testPathText[] = "C:/xdevicefile-test.bin";
#else
    static const char testPathText[] = "/tmp/xdevicefile-platform-test.bin";
#endif
    static const char firstData[] = "XinYueC XDeviceFile";
    XString* path = XString_create_utf8(testPathText);
    XDeviceOpenOptions options;
    XVarList* commandInput = NULL;
    XVarList* commandOutput = NULL;
    XVariant value;
    XFd fd = XFD_INVALID;
    XFileStat stat;
    void* mapped = NULL;
    char readBuffer[sizeof(firstData)];
    int error = 0;
    bool ok = false;
    const char* failedStep = NULL;

    if (!path) return false;
    (void)XDeviceFile_removePermanent(path);
    memset(&options, 0, sizeof(options));
    options.m_openMode = XDeviceFile_ReadWrite |
                         XDeviceFile_Create |
                         XDeviceFile_Truncate;
    options.m_target = path;
    fd = XDevice_open(XDeviceType_File, &options, &error);
    if (fd == XFD_INVALID || error != XDeviceError_None) {
        failedStep = "open read-write";
        goto cleanup;
    }
    if (XDevice_write(fd, firstData, (int64_t)strlen(firstData)) !=
            (int64_t)strlen(firstData)) {
        failedStep = "write";
        goto cleanup;
    }
    if (!XDevice_flush(fd) || XDevice_seek(fd, 0, XDeviceSeekWhence_Begin) != 0) {
        failedStep = "flush or seek";
        goto cleanup;
    }
    memset(readBuffer, 0, sizeof(readBuffer));
    if (XDevice_read(fd, readBuffer, (int64_t)strlen(firstData)) !=
            (int64_t)strlen(firstData) || strcmp(readBuffer, firstData) != 0) {
        failedStep = "read verification";
        goto cleanup;
    }

    memset(&value, 0, sizeof(value));
    if (!XDevice_queryProperty(fd, XDeviceProperty_Size, &value) ||
        XVariant_toInt64(&value) != (int64_t)strlen(firstData)) {
        failedStep = "query size";
        goto cleanup;
    }
    if (!XDevice_resize(fd, 4)) {
        failedStep = "resize";
        goto cleanup;
    }
    XVariant_setValue_int64(&value, 0);
    if (!XDevice_getProperty(fd, XDeviceProperty_Size, &value) ||
        XVariant_toInt64(&value) != 4) {
        failedStep = "get size";
        goto cleanup;
    }
    memset(&stat, 0, sizeof(stat));
    commandOutput = XVarList_Create(XVar(XFileStat, stat));
    if (!commandOutput || !XDevice_control(fd, XDeviceFileCommand_GetFileStat,
                                            NULL, commandOutput)) {
        failedStep = "get file stat command";
        goto cleanup;
    }
    XVarList_start(commandOutput);
    stat = XVarList_arg(commandOutput, XFileStat);
    XVarList_delete(commandOutput);
    commandOutput = NULL;
    if (!stat.exists || stat.size != 4) {
        failedStep = "file stat result";
        goto cleanup;
    }
    mapped = XDeviceFile_map(fd, 0, 4, 0x2);
    if (!mapped) {
        failedStep = "map command";
        goto cleanup;
    }
    if (!mapped || memcmp(mapped, firstData, 4) != 0) {
        failedStep = "map result";
        goto cleanup;
    }
    memcpy(mapped, "ABCD", 4);
    if (!XDeviceFile_unmap(fd, mapped, 4)) {
        failedStep = "unmap command";
        goto cleanup;
    }
    mapped = NULL;
    if (!XDevice_flush(fd) || XDevice_seek(fd, 0, XDeviceSeekWhence_Begin) != 0 ||
        XDevice_read(fd, readBuffer, 4) != 4 || memcmp(readBuffer, "ABCD", 4) != 0) {
        failedStep = "mapped writeback";
        goto cleanup;
    }
    {
        XFileTime timeType = XFile_ModificationTime;
        int64_t timeValue = 1700000000;
        commandInput = XVarList_Create(XVar(XFileTime, timeType),
                                       XVar(int64_t, timeValue));
    }
    if (!commandInput || !XDevice_control(fd, XDeviceFileCommand_SetFileTime,
                                          commandInput, NULL)) {
        failedStep = "set file time command";
        goto cleanup;
    }
    XVarList_delete(commandInput);
    commandInput = NULL;
    XDevice_close(fd);
    fd = XFD_INVALID;

    memset(&options, 0, sizeof(options));
    options.m_openMode = XDeviceFile_ReadOnly;
    options.m_flags = XDeviceOpenFlag_NonBlocking;
    options.m_target = path;
    fd = XDevice_open(XDeviceType_File, &options, &error);
#if defined(XFILE_USE_PLATFORM_API) && \
    (defined(__linux__) || defined(__APPLE__) || defined(__BSD__))
    if (fd == XFD_INVALID) {
        failedStep = "open non-blocking";
        goto cleanup;
    }
    memset(&value, 0, sizeof(value));
    if (!XDevice_getProperty(fd, XDeviceProperty_NonBlocking, &value) ||
        !XVariant_toBool(&value)) {
        failedStep = "non-blocking property";
        goto cleanup;
    }
    XDevice_close(fd);
    fd = XFD_INVALID;
#else
    if (fd != XFD_INVALID) {
        failedStep = "unsupported non-blocking open";
        goto cleanup;
    }
#endif

    memset(&options, 0, sizeof(options));
    options.m_openMode = XDeviceFile_ReadOnly;
    options.m_target = path;
    fd = XDevice_open(XDeviceType_File, &options, &error);
    if (fd == XFD_INVALID || XDevice_seek(fd, 0, XDeviceSeekWhence_Begin) != 0) {
        failedStep = "open read-only";
        goto cleanup;
    }
    memset(readBuffer, 0, sizeof(readBuffer));
    if (XDevice_read(fd, readBuffer, 2) != 2 || memcmp(readBuffer, "AB", 2) != 0) {
        failedStep = "read-only verification";
        goto cleanup;
    }
#if (defined(__unix__) || defined(__APPLE__) || defined(__BSD__)) && \
    defined(XFILE_USE_PLATFORM_API)
    if (!XDeviceFileTest_sharedMemory()) {
        failedStep = "shared-memory map command";
        goto cleanup;
    }
#endif

    ok = true;

cleanup:
    if (commandInput) XVarList_delete(commandInput);
    if (commandOutput) XVarList_delete(commandOutput);
    if (mapped && fd != XFD_INVALID)
        (void)XDeviceFile_unmap(fd, mapped, 4);
    if (fd != XFD_INVALID) XDevice_close(fd);
    (void)XDeviceFile_removePermanent(path);
    XString_delete_base((XClass*)path);
    if (ok) puts("XDeviceFile test: PASS");
    else printf("XDeviceFile test: FAIL (%s)\n", failedStep ? failedStep : "unknown");
    return ok;
#endif
}
