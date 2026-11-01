/**
 * @file XConsoleShell_XIODevice.c
 * @brief XConsoleShell 的 XIODevice 公共 API 传输适配实现。
 */

#include "XConsoleShell_XIODevice.h"

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XIODEVICE_BACKEND_ON

#include <string.h>

static int64_t xcs_xiodevice_read(void* userData, void* data, size_t size)
{
    XConsoleShellXIODeviceAdapter* adapter =
        (XConsoleShellXIODeviceAdapter*)userData;
    if (!adapter || !adapter->device || (!data && size)) return -1;
    return XIODevice_read_1(adapter->device, (char*)data, (int64_t)size);
}

static int64_t xcs_xiodevice_write(void* userData, const void* data, size_t size)
{
    XConsoleShellXIODeviceAdapter* adapter =
        (XConsoleShellXIODeviceAdapter*)userData;
    if (!adapter || !adapter->device || (!data && size)) return -1;
    return XIODevice_write_1(adapter->device, (const char*)data, (int64_t)size);
}

static bool xcs_xiodevice_flush(void* userData)
{
    XConsoleShellXIODeviceAdapter* adapter =
        (XConsoleShellXIODeviceAdapter*)userData;
    return adapter && adapter->device && XIODevice_flush(adapter->device);
}

void XConsoleShellXIODeviceAdapter_init(XConsoleShellXIODeviceAdapter* adapter,
                                        XIODevice* device)
{
    if (!adapter) return;
    adapter->device = device;
}

bool XConsoleShellXIODeviceAdapter_makeIo(
    XConsoleShellXIODeviceAdapter* adapter, XConsoleShellIo* io)
{
    if (!adapter || !adapter->device || !io) return false;
    memset(io, 0, sizeof(*io));
    io->read = xcs_xiodevice_read;
    io->write = xcs_xiodevice_write;
    io->flush = xcs_xiodevice_flush;
    io->userData = adapter;
    return true;
}

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_XIODEVICE_BACKEND_ON */
