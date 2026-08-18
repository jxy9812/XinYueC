#include "XDeviceSerialPort.h"

#if defined(_WIN32) && defined(XSERIALPORT_USE_PLATFORM_API)

#include "XMemory.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XString.h"
#include "XSerialPort.h"
#include <windows.h>
#include <stdint.h>
#include <string.h>

typedef struct XDeviceSerialPortWin32Context
{
    XDeviceSerialPortContext m_base;
    HANDLE m_handle;
    COMMTIMEOUTS m_originalTimeouts;
    bool m_haveOriginalTimeouts;
} XDeviceSerialPortWin32Context;

static XDeviceSerialPortWin32Context* win32Context(XFd fd)
{
    return (XDeviceSerialPortWin32Context*)XDevice_handle(fd);
}

static BYTE win32DataBits(XSerialPort_DataBits value)
{
    return value >= XSerialPort_Data5 && value <= XSerialPort_Data8 ? (BYTE)value : 8;
}

static BYTE win32Parity(XSerialPort_Parity value)
{
    switch (value) {
    case XSerialPort_EvenParity: return EVENPARITY;
    case XSerialPort_OddParity: return ODDPARITY;
    case XSerialPort_MarkParity: return MARKPARITY;
    case XSerialPort_SpaceParity: return SPACEPARITY;
    default: return NOPARITY;
    }
}

static BYTE win32StopBits(XSerialPort_StopBits value)
{
    return value == XSerialPort_TwoStop ? TWOSTOPBITS : ONESTOPBIT;
}

static bool win32ApplyConfig(XDeviceSerialPortWin32Context* context)
{
    DCB dcb;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE) return false;
    memset(&dcb, 0, sizeof(dcb));
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(context->m_handle, &dcb)) return false;
    dcb.BaudRate = context->m_base.m_baudRate;
    dcb.ByteSize = win32DataBits(context->m_base.m_dataBits);
    dcb.Parity = win32Parity(context->m_base.m_parity);
    dcb.StopBits = win32StopBits(context->m_base.m_stopBits);
    dcb.fParity = context->m_base.m_parity != XSerialPort_NoParity;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fInX = FALSE;
    dcb.fOutX = FALSE;
    if (context->m_base.m_flowControl == XSerialPort_HardwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl) {
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
    }
    if (context->m_base.m_flowControl == XSerialPort_SoftwareControl ||
        context->m_base.m_flowControl == XSerialPort_BothControl) {
        dcb.fInX = TRUE;
        dcb.fOutX = TRUE;
    }
    return SetCommState(context->m_handle, &dcb) != FALSE;
}

XDeviceSerialPortContext* XDeviceSerialPort_platformCreateContext(void)
{
    XDeviceSerialPortWin32Context* context =
        (XDeviceSerialPortWin32Context*)XCalloc_System(1, sizeof(*context));
    if (context) context->m_handle = INVALID_HANDLE_VALUE;
    return context ? &context->m_base : NULL;
}

void XDeviceSerialPort_platformDeleteContext(XDeviceSerialPortContext* base)
{
    XFree_System(base);
}

bool XDeviceSerialPort_platformOpen(XFd fd, const XString* portName)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    const wchar_t* path;
    wchar_t fullPath[256];
    DWORD access = 0;
    COMMTIMEOUTS timeouts;
    if (!context || !portName) return false;
    path = (const wchar_t*)XString_toUtf16(portName);
    if (!path) return false;
    if (context->m_base.m_openMode & XIODevice_ReadOnly) access |= GENERIC_READ;
    if (context->m_base.m_openMode & XIODevice_WriteOnly) access |= GENERIC_WRITE;
    if (wcsncmp(path, L"\\\\.\\", 4) == 0)
        wcsncpy(fullPath, path, sizeof(fullPath) / sizeof(fullPath[0]) - 1);
    else {
        fullPath[0] = L'\\'; fullPath[1] = L'\\'; fullPath[2] = L'.'; fullPath[3] = L'\\';
        wcsncpy(fullPath + 4, path, sizeof(fullPath) / sizeof(fullPath[0]) - 5);
    }
    fullPath[sizeof(fullPath) / sizeof(fullPath[0]) - 1] = L'\0';
    context->m_handle = CreateFileW(fullPath, access, 0, NULL, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (context->m_handle == INVALID_HANDLE_VALUE) return false;
    memset(&context->m_originalTimeouts, 0, sizeof(context->m_originalTimeouts));
    context->m_haveOriginalTimeouts = GetCommTimeouts(context->m_handle,
                                                       &context->m_originalTimeouts) != FALSE;
    memset(&timeouts, 0, sizeof(timeouts));
    timeouts.ReadIntervalTimeout = MAXDWORD;
    if (!SetCommTimeouts(context->m_handle, &timeouts) ||
        !SetupComm(context->m_handle, (DWORD)context->m_base.m_readBufferSize,
                   (DWORD)context->m_base.m_readBufferSize) || !win32ApplyConfig(context)) {
        CloseHandle(context->m_handle);
        context->m_handle = INVALID_HANDLE_VALUE;
        return false;
    }
    PurgeComm(context->m_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return true;
}

void XDeviceSerialPort_platformClose(XFd fd)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    if (!context) return;
    if (context->m_handle != INVALID_HANDLE_VALUE) {
        if (context->m_haveOriginalTimeouts)
            SetCommTimeouts(context->m_handle, &context->m_originalTimeouts);
        CloseHandle(context->m_handle);
        context->m_handle = INVALID_HANDLE_VALUE;
    }
}

int64_t XDeviceSerialPort_platformRead(XFd fd, void* buffer, int64_t size)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    DWORD done = 0;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE || !buffer || size <= 0) return -1;
    if (!ReadFile(context->m_handle, buffer, (DWORD)size, &done, NULL)) return -1;
    return (int64_t)done;
}

int64_t XDeviceSerialPort_platformWrite(XFd fd, const void* data, int64_t size)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    DWORD done = 0;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE || !data || size <= 0) return -1;
    if (!WriteFile(context->m_handle, data, (DWORD)size, &done, NULL)) return -1;
    return (int64_t)done;
}

bool XDeviceSerialPort_platformFlush(XFd fd)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    return context && context->m_handle != INVALID_HANDLE_VALUE &&
           FlushFileBuffers(context->m_handle) != FALSE;
}

bool XDeviceSerialPort_platformSetProperty(XFd fd, uint32_t property, const XVariant* value)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    bool enabled;
    if (!context || !value || context->m_handle == INVALID_HANDLE_VALUE) return false;
    switch (property) {
    case XDeviceSerialPortProperty_BaudRate:
    case XDeviceSerialPortProperty_DataBits:
    case XDeviceSerialPortProperty_Parity:
    case XDeviceSerialPortProperty_StopBits:
    case XDeviceSerialPortProperty_FlowControl:
        return win32ApplyConfig(context);
    case XDeviceSerialPortProperty_ReadBufferSize:
        return SetupComm(context->m_handle, (DWORD)XVariant_toInt64(value),
                         (DWORD)XVariant_toInt64(value)) != FALSE;
    case XDeviceSerialPortProperty_DataTerminalReady:
        enabled = XVariant_toBool(value);
        return EscapeCommFunction(context->m_handle, enabled ? SETDTR : CLRDTR) != FALSE;
    case XDeviceSerialPortProperty_RequestToSend:
        enabled = XVariant_toBool(value);
        return EscapeCommFunction(context->m_handle, enabled ? SETRTS : CLRRTS) != FALSE;
    case XDeviceSerialPortProperty_BreakEnabled:
        enabled = XVariant_toBool(value);
        return EscapeCommFunction(context->m_handle, enabled ? SETBREAK : CLRBREAK) != FALSE;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformGetProperty(XFd fd, uint32_t property, XVariant* value)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    COMSTAT status;
    DWORD errors = 0;
    DWORD modem = 0;
    int signals;
    if (!context || !value || context->m_handle == INVALID_HANDLE_VALUE) return false;
    switch (property) {
    case XDeviceProperty_NativeHandle:
        XVariant_setValue_ptr(value, context->m_handle); return true;
    case XDeviceSerialPortProperty_BytesAvailable:
        if (!ClearCommError(context->m_handle, &errors, &status)) return false;
        XVariant_setValue_int64(value, status.cbInQue); return true;
    case XDeviceSerialPortProperty_PinoutSignals:
        if (!GetCommModemStatus(context->m_handle, &modem)) return false;
        signals = XSerialPort_NoSignal;
        if (modem & MS_CTS_ON) signals |= XSerialPort_ClearToSendSignal;
        if (modem & MS_DSR_ON) signals |= XSerialPort_DataSetReadySignal;
        if (modem & MS_RING_ON) signals |= XSerialPort_RingIndicatorSignal;
        if (modem & MS_RLSD_ON) signals |= XSerialPort_DataCarrierDetectSignal;
        XVariant_setValue_int(value, signals); return true;
    default:
        return false;
    }
}

bool XDeviceSerialPort_platformControl(XFd fd, uint32_t command,
                                       const XVarList* input, XVarList* output)
{
    XDeviceSerialPortWin32Context* context = win32Context(fd);
    XSerialPort_Direction directions;
    XVarList* arguments = (XVarList*)input;
    DWORD flags = 0;
    (void)output;
    if (!context || context->m_handle == INVALID_HANDLE_VALUE) return false;
    if (command == XDeviceSerialPortCommand_Clear) {
        if (!arguments || arguments->m_size != sizeof(directions)) return false;
        XVarList_start(arguments);
        directions = XVarList_arg(arguments, XSerialPort_Direction);
        if (directions & XSerialPort_Input) flags |= PURGE_RXCLEAR;
        if (directions & XSerialPort_Output) flags |= PURGE_TXCLEAR;
        return PurgeComm(context->m_handle, flags) != FALSE;
    }
    return command == XDeviceSerialPortCommand_HandleEvent ||
           command == XDeviceCommand_Cancel;
}

#endif
