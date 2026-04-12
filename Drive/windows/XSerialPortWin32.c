#if defined(_WIN32)
#include <windows.h>
#include <stdio.h>
#include "XSerialPort.h"
#include "XMemory.h"
#include "XMutex.h"
#include "XWaitCondition.h"
bool XSerialPort_platform_applyConfig(XSerialPort* port);
static DWORD toDCBRate(int32_t rate);
static BYTE toDCBDataBits(XSerialPort_DataBits d);
static BYTE toDCBParity(XSerialPort_Parity p);
static BYTE toDCBStopBits(XSerialPort_StopBits sb);
typedef struct XSerialPortWin32
{
    XSerialPort base;
    HANDLE handle;
    OVERLAPPED waitOverlapped;
    HANDLE waitEvent;
    bool pendingWait;
    DWORD lastEvents; // 用于 WaitCommEvent 的 lpEvtMask
} XSerialPortWin32;

void XSerialPort_init(XSerialPort* serial)
{
    if (serial == NULL)
        return;
    // 只清零 XSerialPort 部分
    memset(((XIODevice*)serial) + 1, 0, sizeof(XSerialPort) - sizeof(XIODevice));
    XIODevice_init(serial);
    XClassGetVtable(serial) = XSerialPort_class_init();
    // 设置常用的默认串口参数
    serial->baudRate = XSerialPort_Baud9600; // 或者直接写 9600
    serial->dataBits = XSerialPort_Data8;
    serial->parity = XSerialPort_NoParity;
    serial->stopBits = XSerialPort_OneStop;
    serial->flowControl = XSerialPort_NoFlowControl;

    // 设置默认的读缓冲区大小 (Qt 的默认值是 512 KB)
    serial->readBufferSize = 512 * 1024;

    // 初始化同步原语，用于 waitForReadyRead / waitForBytesWritten
    serial->waitMutex = XMutex_create();
    serial->waitCondition = XWaitCondition_create();
}

XSerialPort* XSerialPort_create()
{
    XSerialPort* port = XNew(XSerialPortWin32);
    if (!port)return NULL;
    XSerialPort_init(port);
    SET_CLASS_HEAP(port);
    return port;
}
// Helper: 启动 WaitCommEvent
static bool startWaitCommEvent(XSerialPort* port) {
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    if (!port || win32->pendingWait) return true;

    DWORD events = 0;
    if (!WaitCommEvent(win32->handle, &win32->lastEvents, &win32->waitOverlapped)) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            win32->pendingWait = true;
            return true;
        }
        printf("WaitCommEvent failed with error %lu\n", err);
        return false;
    }
    win32->pendingWait = true;
    return true;
}

// Helper functions
DWORD toDCBRate(int32_t rate) {
    switch (rate) {
    case 1200: return CBR_1200;
    case 2400: return CBR_2400;
    case 4800: return CBR_4800;
    case 9600: return CBR_9600;
    case 19200: return CBR_19200;
    case 38400: return CBR_38400;
    case 57600: return CBR_57600;
    case 115200: return CBR_115200;
    default: return (DWORD)rate;
    }
}
BYTE toDCBDataBits(XSerialPort_DataBits d) {
    switch (d) {
    case XSerialPort_Data5:
        return 5;
    case XSerialPort_Data6:
        return 6;
    case XSerialPort_Data7:
        return 7;
    case XSerialPort_Data8:
        return 8;
    default:
        // 防御性编程：未知值回退到最常用的 8 数据位
        return 8;
    }
}
BYTE toDCBParity(XSerialPort_Parity p) 
{
    switch (p) {
    case XSerialPort_NoParity:    return NOPARITY;
    case XSerialPort_EvenParity:  return EVENPARITY;
    case XSerialPort_OddParity:   return ODDPARITY;
    case XSerialPort_SpaceParity: return SPACEPARITY;
    case XSerialPort_MarkParity:  return MARKPARITY;
    default: return NOPARITY;
    }
}

BYTE toDCBStopBits(XSerialPort_StopBits sb) {
    switch (sb) {
    case XSerialPort_OneStop:         return ONESTOPBIT;
    case XSerialPort_OneAndHalfStop:  return ONE5STOPBITS;
    case XSerialPort_TwoStop:         return TWOSTOPBITS;
    default: return ONESTOPBIT;
    }
}

// ========== 平台函数实现 ==========
bool XSerialPort_platform_open(XSerialPort* port, XIODeviceBaseMode mode) {
    if (!port->portName) return false;
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    DWORD access = 0;
    if (mode & XIODevice_ReadOnly) access |= GENERIC_READ;
    if (mode & XIODevice_WriteOnly) access |= GENERIC_WRITE;

    char fullPortName[64];
    if (strncmp(port->portName, "\\\\.\\", 4) == 0) {
        strncpy(fullPortName, port->portName, sizeof(fullPortName) - 1);
    }
    else {
        snprintf(fullPortName, sizeof(fullPortName), "\\\\.\\%s", port->portName);
    }
    fullPortName[sizeof(fullPortName) - 1] = '\0';

    HANDLE h = CreateFileA(fullPortName, access, 0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    // 设置缓冲区
    if (port->readBufferSize > 0) {
        DWORD writeBuf = (port->readBufferSize > 2048) ? (DWORD)port->readBufferSize : 2048;
        SetupComm(h, (DWORD)port->readBufferSize, writeBuf);
    }

    // 设置超时（非阻塞读写）
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    SetCommTimeouts(h, &timeouts);
    PurgeComm(h, PURGE_TXCLEAR | PURGE_RXCLEAR);

    // >>>>>> 关键修复：必须调用 SetCommMask <<<<<<
    if (!SetCommMask(h, EV_RXCHAR | EV_ERR)) {
        DWORD err = GetLastError();
        printf("SetCommMask failed with error %lu\n", err);
        CloseHandle(h);
        return false;
    }

    win32->handle = h;
    // >>>>>> 关键：显式清零 OVERLAPPED <<<<<<
    memset(&win32->waitOverlapped, 0, sizeof(OVERLAPPED));
    win32->waitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!win32->waitEvent) {
        CloseHandle(h);
        return false;
    }
    win32->waitOverlapped.hEvent = win32->waitEvent;

    // 应用串口配置
    if (!XSerialPort_platform_applyConfig(port)) {
        CloseHandle(win32->waitEvent);
        CloseHandle(h);
        return false;
    }

    // 启动事件监听
    if (!startWaitCommEvent(port)) {
        CloseHandle(win32->waitEvent);
        CloseHandle(h);
        return false;
    }

    port->isOpen = true;
    XObject_setPollTime(port, 10);
    return true;
}

void XSerialPort_platform_close(XSerialPort* port) {
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    if (!port->isOpen) return;
    XObject_setPollTime(port, 0);
    CancelIo(win32->handle);
    PurgeComm(win32->handle, PURGE_TXABORT | PURGE_RXABORT);
    CloseHandle(win32->handle);
    win32->handle = 0;
    if (win32->waitEvent) 
    {
        CloseHandle(win32->waitEvent);
        win32->waitEvent = 0;
    }

    port->isOpen = false;
}

int64_t XSerialPort_platform_read(XSerialPort* port, char* data, int64_t maxSize) {
    if (!port || !data || maxSize <= 0) return -1;
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;

    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return -1;

    DWORD bytesRead = 0;
    BOOL r = ReadFile(win32->handle, data, (DWORD)maxSize, &bytesRead, &ov);
    if (!r) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            // 等待完成
            if (WaitForSingleObject(ov.hEvent, INFINITE) == WAIT_OBJECT_0) {
                GetOverlappedResult(win32->handle, &ov, &bytesRead, FALSE);
                r = TRUE;
            }
            else {
                // 真正的错误
                port->error = XSerialPort_ReadError;
            }
        }
        else {
            // 非 pending 错误（如句柄无效）
            port->error = XSerialPort_ReadError;
        }
    }

    CloseHandle(ov.hEvent);
    return r ? (int64_t)bytesRead : -1;
}

int64_t XSerialPort_platform_write(XSerialPort* port, const char* data, int64_t len) {
    if (!port || !data || len <= 0) return -1;
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return -1;

    DWORD bytesWritten = 0;
    BOOL r = WriteFile(win32->handle, data, (DWORD)len, &bytesWritten, &ov);
    if (!r && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 10) == WAIT_OBJECT_0) {
            GetOverlappedResult(win32->handle, &ov, &bytesWritten, FALSE);
            r = TRUE;
        }
    }

    CloseHandle(ov.hEvent);
    return r ? (int64_t)bytesWritten : -1;
}

int64_t XSerialPort_platform_bytesAvailable(const XSerialPort* port) {
    if (!port) return 0;
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    COMSTAT comStat;
    DWORD errors;
    ClearCommError(win32->handle, &errors, &comStat);
    return (int64_t)comStat.cbInQue;
}
int64_t XSerialPort_platform_bytesToWrite(const XSerialPort* port) {
    if (!port) return 0;
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    COMSTAT comStat;
    DWORD errors;
    ClearCommError(win32->handle, &errors, &comStat);
    return (int64_t)comStat.cbOutQue;
}

bool XSerialPort_setDataBits(XSerialPort* port, XSerialPort_DataBits dataBits) {
    if (!port) return false;
    XSerialPortWin32* win32 = (XSerialPort*)port;
    if (port->dataBits == dataBits) return true;
    port->dataBits = dataBits;
    bool ok = true;
    if (XSerialPort_isOpen(port)) {
        ok = XSerialPort_platform_applyConfig(port);
    }
    if (ok) {
        XSerialPort_dataBitsChanged_signal(port, dataBits);
    }
    return ok;
}
bool XSerialPort_setParity(XSerialPort* port, XSerialPort_Parity parity) {
    if (!port) return false;
    XSerialPortWin32* win32 = (XSerialPort*)port;
    if (port->parity == parity) return true;
    port->parity = parity;
    bool ok = true;
    if (XSerialPort_isOpen(port)) 
    {
        ok = XSerialPort_platform_applyConfig(port);
    }
    if (ok) {
        XSerialPort_parityChanged_signal(port, parity);
    }
    return ok;
}
bool XSerialPort_setStopBits(XSerialPort* port, XSerialPort_StopBits stopBits) {
    if (!port) return false;
    XSerialPortWin32* win32 = (XSerialPort*)port;
    if (port->stopBits == stopBits) return true;
    port->stopBits = stopBits;
    bool ok = true;
    if (XSerialPort_isOpen(port)) 
    {
        ok = XSerialPort_platform_applyConfig(port);
    }
    if (ok) {
        XSerialPort_stopBitsChanged_signal(port, stopBits);
    }
    return ok;
}
bool XSerialPort_setFlowControl(XSerialPort* port, XSerialPort_FlowControl flowControl) {
    if (!port) return false;
    XSerialPortWin32* win32 = (XSerialPort*)port;
    if (port->flowControl == flowControl) return true;
    port->flowControl = flowControl;
    bool ok = true;
    if (XSerialPort_isOpen(port))
    {
        ok = XSerialPort_platform_applyConfig(port);
    }
    if (ok) {
        XSerialPort_flowControlChanged_signal(port, flowControl);
    }
    return ok;
}
void XSerialPort_platform_poll(XSerialPort* port)
{
    if (!port) return ;
    XSerialPortWin32* win32 = (XSerialPort*)port;

    if (!win32->pendingWait) return;

    DWORD transferred = 0;
    if (GetOverlappedResult(win32->handle, &win32->waitOverlapped, &transferred, FALSE)) {
        win32->pendingWait = false;
        startWaitCommEvent(win32); // 重新监听
        XIODevice_readyRead_signal((XIODevice*)port); // ✅ 安全 emit
    }
}
bool XSerialPort_platform_applyConfig(XSerialPort* port)
{
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(win32->handle, &dcb)) return false;

    dcb.BaudRate = toDCBRate(port->baudRate);
    dcb.ByteSize = toDCBDataBits(port->dataBits);
    dcb.Parity = toDCBParity(port->parity);
    dcb.StopBits = toDCBStopBits(port->stopBits);

    // 流控设置
    switch (port->flowControl) {
    case XSerialPort_NoFlowControl:
        dcb.fOutxCtsFlow = FALSE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fInX = FALSE;
        dcb.fOutX = FALSE;
        break;
    case XSerialPort_HardwareControl:
        dcb.fOutxCtsFlow = TRUE;
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        dcb.fInX = FALSE;
        dcb.fOutX = FALSE;
        break;
    case XSerialPort_SoftwareControl:
        dcb.fOutxCtsFlow = FALSE;
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fInX = TRUE;
        dcb.fOutX = TRUE;
        break;
    }

    return SetCommState(win32->handle, &dcb) != FALSE;
}
// ========== 新增的平台等待函数 ==========
bool XSerialPort_platform_waitForReadyRead(XSerialPort* port, int msecs) {
    if (XSerialPort_platform_bytesAvailable(port) > 0) return true;

    XMutex_lock(port->waitMutex);
    port->readyReadTriggered = false;
    XMutex_unlock(port->waitMutex);

    XMutex_lock(port->waitMutex);
    bool result = XWaitCondition_wait(port->waitCondition, port->waitMutex, msecs);
    if (!result) {
        port->error = XSerialPort_TimeoutError;
    }
    bool triggered = port->readyReadTriggered;
    port->readyReadTriggered = false;
    XMutex_unlock(port->waitMutex);

    return triggered || XSerialPort_platform_bytesAvailable(port) > 0;
}

bool XSerialPort_platform_waitForBytesWritten(XSerialPort* port, int msecs) {
    if (XSerialPort_platform_bytesToWrite(port) == 0) return true;

    XMutex_lock(port->waitMutex);
    port->bytesWrittenTriggered = false;
    XMutex_unlock(port->waitMutex);

    XMutex_lock(port->waitMutex);
    bool result = XWaitCondition_wait(port->waitCondition, port->waitMutex, msecs);
    if (!result) {
        port->error = XSerialPort_TimeoutError;
    }
    bool triggered = port->bytesWrittenTriggered;
    port->bytesWrittenTriggered = false;
    XMutex_unlock(port->waitMutex);

    return triggered || XSerialPort_platform_bytesToWrite(port) == 0;
}

// NEW: pinoutSignals
XSerialPort_PinoutSignal XSerialPort_pinoutSignals(const XSerialPort* port)
{
    XSerialPortWin32* win32 = (XSerialPort*)port;
    if (!port || !port->isOpen) return XSerialPort_NoSignal;
    DWORD modemStat;
    if (!GetCommModemStatus((HANDLE)win32->handle, &modemStat)) return XSerialPort_NoSignal;
    XSerialPort_PinoutSignal signals = XSerialPort_NoSignal;
    if (modemStat & MS_CTS_ON) signals |= XSerialPort_ClearToSendSignal;
    if (modemStat & MS_DSR_ON) signals |= XSerialPort_DataSetReadySignal;
    if (modemStat & MS_RING_ON) signals |= XSerialPort_RingIndicatorSignal;
    if (modemStat & MS_RLSD_ON) signals |= XSerialPort_DataCarrierDetectSignal;
    if (port->dataTerminalReady) signals |= XSerialPort_DataTerminalReadySignal;
    if (port->requestToSend) signals |= XSerialPort_RequestToSendSignal;
    return signals;
}


// ========== Control Functions ==========

bool XSerialPort_setDataTerminalReady(XSerialPort* port, bool set) {
    if (!port) return false;
    XSerialPortWin32* win32 = (XSerialPort*)port;
    if (port->dataTerminalReady == set) return true;

    if (port->isOpen&& EscapeCommFunction((HANDLE)win32->handle, set ? SETDTR : CLRDTR))
    {
        port->dataTerminalReady = set;
        XSerialPort_dataTerminalReadyChanged_signal(port, set);
        return true;
    }
    return false;
}
bool XSerialPort_setRequestToSend(XSerialPort* port, bool set)
{
    if (!port) return false;
    XSerialPortWin32* win32 = (XSerialPort*)port;
    if (port->requestToSend == set) return true;

    if (port->isOpen && port->flowControl != XSerialPort_HardwareControl&&
        EscapeCommFunction((HANDLE)win32->handle, set ? SETRTS : CLRRTS))
    {
        port->requestToSend = set;
        XSerialPort_requestToSendChanged_signal(port, set);
        return true;
    }
    return false;
}
bool XSerialPort_setBreakEnabled(XSerialPort* port, bool enable) 
{
    if (!port) return false;
    XSerialPortWin32* win32 = (XSerialPort*)port;
    if (port->breakEnabled == enable) return true;
    if (port->isOpen&& EscapeCommFunction((HANDLE)win32->handle, enable ? SETBREAK : CLRBREAK))
    {
        port->breakEnabled = enable;
        XSerialPort_breakEnabledChanged_signal(port, enable);
        return true;
    }
    return false;
}
// ========== Flush / Clear ==========
bool XSerialPort_flush(XSerialPort* port)
{
    if (!port)return;
    XSerialPortWin32* win32 = (XSerialPort*)port;
    if (!FlushFileBuffers((HANDLE)win32->handle)) {
        port->error = XSerialPort_WriteError;
        return false;
    }
    return true;
}

bool XSerialPort_clear(XSerialPort* port, XSerialPort_Direction directions)
{
    if (!port)return;
    XSerialPortWin32* win32 = (XSerialPort*)port;
    DWORD flags = 0;
    if (directions & XSerialPort_Input)  flags |= PURGE_RXCLEAR;
    if (directions & XSerialPort_Output) flags |= PURGE_TXCLEAR;

    if (!PurgeComm((HANDLE)win32->handle, flags)) {
        port->error = XSerialPort_ResourceError;
        return false;
    }
    return true;
}
XHandle XSerialPort_platform_handle(const XSerialPort* port)
{
    if (!port) return -1;
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    // Windows 的 HANDLE 可以直接转换为 intptr_t
    return (XHandle)(win32->handle);
}
#endif // _WIN32