/**
 * @file XProcess_unsupported.c
 * @brief 未提供进程和系统环境后端的平台存根。
 * @details
 * 裸机、FreeRTOS 或尚未接入进程模型的平台仍可链接 XProcess 公共模块；
 * 启动操作返回明确的 FailedToStart，系统环境列表和空设备路径保持可用。
 * 具体平台接入时只需在 Drive 中替换本文件对应的后端实现。本文件同时承载
 * XProcessEnvironment 的系统环境存根，避免同一平台条件拆分为两个源文件。
 */

#include "XProcess_Protected.h"
#include "XProcessEnvironment.h"

#if XProcess_ON && !(defined(__linux__) || defined(__APPLE__) || defined(__BSD__) || defined(_WIN32))

#include "XMemory.h"

void XProcess_backend_deinit(XProcess* self)
{
    if (self) self->m_backend = NULL;
}

bool XProcess_backend_start(XProcess* self, XIODeviceBaseMode mode, bool detached)
{
    (void)mode;
    (void)detached;
    if (self) XProcess_backend_setError(self, XProcessError_FailedToStart,
                                        "Process backend is unavailable on this platform");
    return false;
}

bool XProcess_backend_poll(XProcess* self, int timeoutMsecs)
{
    (void)self;
    (void)timeoutMsecs;
    return false;
}

int64_t XProcess_backend_bytesAvailable(const XProcess* self, XProcessChannel channel)
{
    (void)self;
    (void)channel;
    return 0;
}

int64_t XProcess_backend_bytesToWrite(const XProcess* self)
{
    (void)self;
    return 0;
}

void XProcess_backend_closeReadChannel(XProcess* self, XProcessChannel channel)
{
    (void)self;
    (void)channel;
}

void XProcess_backend_closeWriteChannel(XProcess* self)
{
    (void)self;
}

int64_t XProcess_backend_read(XProcess* self, XProcessChannel channel,
                              char* data, int64_t maxlen)
{
    (void)self;
    (void)channel;
    (void)data;
    (void)maxlen;
    return -1;
}

int64_t XProcess_backend_write(XProcess* self, const char* data, int64_t len)
{
    (void)self;
    (void)data;
    (void)len;
    return -1;
}

bool XProcess_backend_waitForBytesWritten(XProcess* self, int msecs)
{
    (void)self;
    (void)msecs;
    return false;
}

void XProcess_backend_terminate(XProcess* self)
{
    (void)self;
}

void XProcess_backend_kill(XProcess* self)
{
    (void)self;
}

bool XProcess_backend_startDetached(const XString* program,
                                    const XStringList* arguments,
                                    const XString* workingDirectory,
                                    XProcessId* pid)
{
    (void)program;
    (void)arguments;
    (void)workingDirectory;
    if (pid) *pid = -1;
    return false;
}

XString* XProcess_backend_nullDevice(void)
{
    return XString_create();
}

XStringList* XProcess_backend_systemEnvironment(void)
{
    return XStringList_create();
}

#if XPROCESS_ENVIRONMENT_ON

XProcessEnvironment* XProcessEnvironment_platform_systemEnvironment(void)
{
    return XProcessEnvironment_create();
}

#endif /* XPROCESS_ENVIRONMENT_ON */

#endif /* XProcess_ON && unsupported */
