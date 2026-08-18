#include "XDeviceFileTest.h"
#include "CXinYueConfig.h"
#include "XDevice.h"
#include "XDeviceFile.h"
#include "XFileInfo.h"
#include "XString.h"
#include "XVariant.h"
#include "XVarList.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

bool XDeviceFileTest_runAll(void)
{
#if !XFILE_ON
    puts("XDeviceFile test: SKIP");
    return true;
#else
#if defined(XFILE_USE_FATFS)
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
#if defined(XFILE_USE_PLATFORM_API)
    {
        int64_t offset = 0;
        int64_t size = 4;
        int flags = 0;
        commandInput = XVarList_Create(XVar(int64_t, offset), XVar(int64_t, size),
                                       XVar(int, flags));
        commandOutput = XVarList_Create(XVar(void*, mapped));
    }
    if (!commandInput || !commandOutput ||
        !XDevice_control(fd, XDeviceFileCommand_Map, commandInput, commandOutput)) {
        failedStep = "map command";
        goto cleanup;
    }
    XVarList_start(commandOutput);
    mapped = XVarList_arg(commandOutput, void*);
    XVarList_delete(commandInput);
    XVarList_delete(commandOutput);
    commandInput = NULL;
    commandOutput = NULL;
    if (!mapped || memcmp(mapped, firstData, 4) != 0) {
        failedStep = "map result";
        goto cleanup;
    }
    {
        int64_t size = 4;
        commandInput = XVarList_Create(XVar(void*, mapped), XVar(int64_t, size));
    }
    if (!commandInput || !XDevice_control(fd, XDeviceFileCommand_Unmap,
                                          commandInput, NULL)) {
        failedStep = "unmap command";
        goto cleanup;
    }
    XVarList_delete(commandInput);
    commandInput = NULL;
    mapped = NULL;
#endif
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
    if (XDevice_read(fd, readBuffer, 2) != 2 || memcmp(readBuffer, firstData, 2) != 0) {
        failedStep = "read-only verification";
        goto cleanup;
    }

    ok = true;

cleanup:
    if (commandInput) XVarList_delete(commandInput);
    if (commandOutput) XVarList_delete(commandOutput);
    if (mapped && fd != XFD_INVALID) {
        int64_t size = 4;
        commandInput = XVarList_Create(XVar(void*, mapped), XVar(int64_t, size));
        if (commandInput) {
            (void)XDevice_control(fd, XDeviceFileCommand_Unmap, commandInput, NULL);
            XVarList_delete(commandInput);
        }
    }
    if (fd != XFD_INVALID) XDevice_close(fd);
    (void)XDeviceFile_removePermanent(path);
    XString_delete_base((XClass*)path);
    if (ok) puts("XDeviceFile test: PASS");
    else printf("XDeviceFile test: FAIL (%s)\n", failedStep ? failedStep : "unknown");
    return ok;
#endif
}
