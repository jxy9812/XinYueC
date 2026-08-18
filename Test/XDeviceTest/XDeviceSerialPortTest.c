#include "XDeviceSerialPortTest.h"
#include "XDevice.h"
#include "XDeviceSerialPort.h"
#include "XFileDescriptor.h"
#include "XSerialPort.h"
#include "XString.h"
#include "XVariant.h"
#include "XVarList.h"
#include <stdio.h>
#include <string.h>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#endif

static bool checkDevicePropertyInt(XFd fd, uint32_t property, int expected)
{
    XVariant value;
    bool ok;
    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    ok = XDevice_getProperty(fd, (XDeviceProperty)property, &value) &&
         XVariant_toInt(&value) == expected;
    XVariant_deinit_base((XClass*)&value);
    return ok;
}

bool XDeviceSerialPortTest_runAll(void)
{
#if !XSERIALPORT_ON
    puts("XDeviceSerialPort test: SKIP");
    return true;
#elif !defined(__linux__) && !defined(__unix__) && !defined(__APPLE__)
    puts("XDeviceSerialPort test: SKIP");
    return true;
#else
    int master = -1;
    int slaveFd = -1;
    const char* slaveName;
    XString* path = NULL;
    XDeviceSerialPortOpenOptions options;
    XSerialPort serial;
    XFd fd = XFD_INVALID;
    XVarList* clearArgs = NULL;
    XVariant value;
    XSerialPort_Direction directions = XSerialPort_AllDirections;
    char buffer[32];
    const char incoming[] = "device-in";
    const char outgoing[] = "device-out";
    const char highIncoming[] = "high-in";
    const char highOutgoing[] = "high-out";
    int error = XDeviceError_None;
    bool ok = false;

    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) goto cleanup;
    slaveName = ptsname(master);
    if (!slaveName) goto cleanup;
    path = XString_create_utf8(slaveName);
    if (!path) goto cleanup;

    XDeviceSerialPortOpenOptions_init(&options);
    options.m_base.m_target = path;
    options.m_owner = NULL;
    fd = XDevice_open(XDeviceType_Serial, &options.m_base, &error);
    if (fd == XFD_INVALID || error != XDeviceError_None || XFd_type(fd) != XFD_TYPE_CLASS)
        goto cleanup;
    if (!XDevice_handle(fd) || XDevice_handle(fd)->m_fd != fd) goto cleanup;
    if (!checkDevicePropertyInt(fd, XDeviceSerialPortProperty_BaudRate, XSerialPort_Baud9600) ||
        !checkDevicePropertyInt(fd, XDeviceSerialPortProperty_DataBits, XSerialPort_Data8) ||
        !checkDevicePropertyInt(fd, XDeviceSerialPortProperty_Parity, XSerialPort_NoParity) ||
        !checkDevicePropertyInt(fd, XDeviceSerialPortProperty_StopBits, XSerialPort_OneStop) ||
        !checkDevicePropertyInt(fd, XDeviceSerialPortProperty_FlowControl, XSerialPort_NoFlowControl))
        goto cleanup;

    memset(&value, 0, sizeof(value));
    XVariant_init(&value, NULL, 0, XVariantType_NULL);
    XVariant_setValue_int(&value, XSerialPort_Baud115200);
    if (!XDevice_setProperty(fd, (XDeviceProperty)XDeviceSerialPortProperty_BaudRate, &value) ||
        !XDevice_getProperty(fd, (XDeviceProperty)XDeviceSerialPortProperty_BaudRate, &value) ||
        XVariant_toInt(&value) != XSerialPort_Baud115200) {
        XVariant_deinit_base((XClass*)&value);
        goto cleanup;
    }
    XVariant_deinit_base((XClass*)&value);

    if (write(master, incoming, sizeof(incoming) - 1) != (ssize_t)(sizeof(incoming) - 1) ||
        XDevice_read(fd, buffer, sizeof(incoming) - 1) != (int64_t)(sizeof(incoming) - 1) ||
        memcmp(buffer, incoming, sizeof(incoming) - 1) != 0 ||
        XDevice_write(fd, outgoing, sizeof(outgoing) - 1) != (int64_t)(sizeof(outgoing) - 1) ||
        read(master, buffer, sizeof(outgoing) - 1) != (ssize_t)(sizeof(outgoing) - 1) ||
        memcmp(buffer, outgoing, sizeof(outgoing) - 1) != 0)
        goto cleanup;

    clearArgs = XVarList_Create(XVar(XSerialPort_Direction, directions));
    if (!clearArgs || !XDevice_control(fd, XDeviceSerialPortCommand_Clear, clearArgs, NULL))
        goto cleanup;
    XVarList_delete(clearArgs);
    clearArgs = NULL;
    XDevice_close(fd);
    fd = XFD_INVALID;

    XSerialPort_init(&serial);
    XSerialPort_setPortName(&serial, slaveName);
    if (!XSerialPort_setBaudRate(&serial, XSerialPort_Baud9600, XSerialPort_AllDirections) ||
        !XSerialPort_open_base(&serial.base, XIODevice_ReadWrite))
        goto cleanup_serial;
    if (XSerialPort_handle(&serial) == (XHandle)-1) goto cleanup_serial;
    if (write(master, highIncoming, sizeof(highIncoming) - 1) != (ssize_t)(sizeof(highIncoming) - 1) ||
        XSerialPort_read_base(&serial.base, buffer, sizeof(highIncoming) - 1) != (int64_t)(sizeof(highIncoming) - 1) ||
        memcmp(buffer, highIncoming, sizeof(highIncoming) - 1) != 0 ||
        XSerialPort_write_base(&serial.base, highOutgoing, sizeof(highOutgoing) - 1) != (int64_t)(sizeof(highOutgoing) - 1) ||
        read(master, buffer, sizeof(highOutgoing) - 1) != (ssize_t)(sizeof(highOutgoing) - 1) ||
        memcmp(buffer, highOutgoing, sizeof(highOutgoing) - 1) != 0)
        goto cleanup_serial;
    (void)XSerialPort_setDataTerminalReady(&serial, true);
    (void)XSerialPort_setRequestToSend(&serial, true);
    (void)XSerialPort_setBreakEnabled(&serial, true);
    (void)XSerialPort_setBreakEnabled(&serial, false);
    if (!XSerialPort_clear(&serial, XSerialPort_AllDirections) || !XSerialPort_flush(&serial))
        goto cleanup_serial;
    ok = true;

cleanup_serial:
    if (XSerialPort_isOpen(&serial.base)) XSerialPort_close_base(&serial.base);
    XClass_deinit_base((XClass*)&serial);
cleanup:
    if (clearArgs) XVarList_delete(clearArgs);
    if (fd != XFD_INVALID) XDevice_close(fd);
    if (slaveFd >= 0) close(slaveFd);
    if (path) XString_delete_base((XClass*)path);
    if (master >= 0) close(master);
    puts(ok ? "XDeviceSerialPort test: PASS" : "XDeviceSerialPort test: FAIL");
    return ok;
#endif
}
