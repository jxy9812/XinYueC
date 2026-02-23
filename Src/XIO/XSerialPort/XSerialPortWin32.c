#if defined(_WIN32)
#include <windows.h>
#include <stdio.h>
#include "XSerialPort_p.h"
#include "XMemory.h"
#include "XMutex.h"
#include "XWaitCondition.h"
// 平台数据定义（仅本文件可见）
struct PlatformData {
    HANDLE handle;
    OVERLAPPED waitOverlapped;
    HANDLE waitEvent;
    bool pendingWait;
    XSerialPort* owner; // 用于 emit 信号
    DWORD lastEvents; // 用于 WaitCommEvent 的 lpEvtMask
};
// Helper: 启动 WaitCommEvent
static bool startWaitCommEvent(PlatformData* pd) {
    if (pd->pendingWait) return true;

    // 调试：打印 OVERLAPPED 内容
    //printf("DEBUG: Internal=%p, InternalHigh=%p, Offset=%lu, OffsetHigh=%lu\n",
    //    (void*)pd->waitOverlapped.Internal,
    //    (void*)pd->waitOverlapped.InternalHigh,
    //    pd->waitOverlapped.Offset,
    //    pd->waitOverlapped.OffsetHigh);

    DWORD events = 0;
    if (!WaitCommEvent(pd->handle, &pd->lastEvents, &pd->waitOverlapped)) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            pd->pendingWait = true;
            return true;
        }
        printf("WaitCommEvent failed with error %lu\n", err); // 应不再出现 87
        return false;
    }
    pd->pendingWait = true;
    return true;
}
// Helper functions
static DWORD toDCBRate(int32_t rate) {
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
static BYTE toDCBDataBits(XSerialPort_DataBits d) {
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
static BYTE toDCBParity(XSerialPort_Parity p) 
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

static BYTE toDCBStopBits(XSerialPort_StopBits sb) {
    switch (sb) {
    case XSerialPort_OneStop:         return ONESTOPBIT;
    case XSerialPort_OneAndHalfStop:  return ONE5STOPBITS;
    case XSerialPort_TwoStop:         return TWOSTOPBITS;
    default: return ONESTOPBIT;
    }
}

bool platform_open(XSerialPortPrivate* d, XSerialPort* owner, const char* portName, XIODeviceBaseMode mode) {
    if (!portName || !d || !owner) return false;

    DWORD access = 0;
    if (mode & XIODevice_ReadOnly) access |= GENERIC_READ;
    if (mode & XIODevice_WriteOnly) access |= GENERIC_WRITE;

    char fullPortName[64];
    if (strncmp(portName, "\\\\.\\", 4) == 0) {
        strncpy(fullPortName, portName, sizeof(fullPortName) - 1);
    }
    else {
        snprintf(fullPortName, sizeof(fullPortName), "\\\\.\\%s", portName);
    }
    fullPortName[sizeof(fullPortName) - 1] = '\0';

    HANDLE h = CreateFileA(fullPortName, access, 0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    // 设置缓冲区
    if (d->readBufferSize > 0) {
        DWORD writeBuf = (d->readBufferSize > 2048) ? (DWORD)d->readBufferSize : 2048;
        SetupComm(h, (DWORD)d->readBufferSize, writeBuf);
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
    // >>>>>> 关键修复：分配 PlatformData <<<<<<
   
    PlatformData* pd = d->platform;
    if (!pd)
    {
        pd=(PlatformData*)XMemory_calloc(1, sizeof(PlatformData));
        d->platform = pd;
    }
    if (!pd) {
        CloseHandle(h);
        return false;
    }

    pd->handle = h;
    // >>>>>> 关键：显式清零 OVERLAPPED <<<<<<
    memset(&pd->waitOverlapped, 0, sizeof(OVERLAPPED));
    pd->waitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!pd->waitEvent) {
        XMemory_free(pd);
        CloseHandle(h);
        d->platform = NULL;
        return false;
    }
    pd->waitOverlapped.hEvent = pd->waitEvent;
    pd->owner = owner;
  

    // 应用串口配置
    if (!platform_applyConfig(d)) {
        CloseHandle(pd->waitEvent);
        XMemory_free(pd);
        CloseHandle(h);
        return false;
    }

    // 启动事件监听
    if (!startWaitCommEvent(pd)) {
        CloseHandle(pd->waitEvent);
        XMemory_free(pd);
        CloseHandle(h);
        return false;
    }

    d->platform = pd; // ← 正确赋值！
    d->isOpen = true;
    XObject_setPollingInterval(owner, 1000);
    return true;
}

void platform_close(XSerialPortPrivate* d) {
    if (!d->isOpen || !d->platform) return;
    PlatformData* pd = d->platform;
    XObject_setPollingInterval(d->platform->owner, 0);
    CancelIo(pd->handle);
    PurgeComm(pd->handle, PURGE_TXABORT | PURGE_RXABORT);
    CloseHandle(pd->handle);
    if (pd->waitEvent) CloseHandle(pd->waitEvent);
    XMemory_free(pd);

    d->platform = NULL;
    d->isOpen = false;
}

bool platform_isOpen(const XSerialPortPrivate* d) {
    return d && d->isOpen;
}

int64_t platform_read(XSerialPortPrivate* d, char* data, int64_t maxSize) {
    if (!d || !data || maxSize <= 0 || !d->platform) return -1;
    PlatformData* pd = d->platform;

    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return -1;

    DWORD bytesRead = 0;
    BOOL r = ReadFile(pd->handle, data, (DWORD)maxSize, &bytesRead, &ov);
    if (!r) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            // 等待完成
            if (WaitForSingleObject(ov.hEvent, INFINITE) == WAIT_OBJECT_0) {
                GetOverlappedResult(pd->handle, &ov, &bytesRead, FALSE);
                r = TRUE;
            }
            else {
                // 真正的错误
                d->error = XSerialPort_ReadError;
                XSerialPort_errorOccurred_signal(d->platform->owner, XSerialPort_ReadError);
            }
        }
        else {
            // 非 pending 错误（如句柄无效）
            d->error = XSerialPort_ReadError;
            XSerialPort_errorOccurred_signal(d->platform->owner, XSerialPort_ReadError);
        }
    }

    CloseHandle(ov.hEvent);
    return r ? (int64_t)bytesRead : -1;
}

int64_t platform_write(XSerialPortPrivate* d, const char* data, int64_t len) {
    if (!d || !data || len <= 0 || !d->platform) return -1;
    PlatformData* pd = d->platform;

    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return -1;

    DWORD bytesWritten = 0;
    BOOL r = WriteFile(pd->handle, data, (DWORD)len, &bytesWritten, &ov);
    if (!r && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 10) == WAIT_OBJECT_0) {
            GetOverlappedResult(pd->handle, &ov, &bytesWritten, FALSE);
            r = TRUE;
        }
    }

    CloseHandle(ov.hEvent);
    return r ? (int64_t)bytesWritten : -1;
}

int64_t platform_bytesAvailable(const XSerialPortPrivate* d) {
     if (!d->platform) return 0;
    PlatformData* pd = d->platform;
    COMSTAT comStat;
    DWORD errors;
    ClearCommError(pd->handle, &errors, &comStat);
    return (int64_t)comStat.cbInQue;
}
// NEW: bytesToWrite
int64_t platform_bytesToWrite(const XSerialPortPrivate* d) {
    if (!d->platform) return 0;
    PlatformData* pd = d->platform;
    COMSTAT comStat;
    DWORD errors;
    ClearCommError(pd->handle, &errors, &comStat);
    return (int64_t)comStat.cbOutQue;
}
void platform_poll(XSerialPortPrivate* d) {
    if (!d->platform) return;
    PlatformData* pd = d->platform;
    if (!pd->pendingWait) return;

    DWORD transferred = 0;
    if (GetOverlappedResult(pd->handle, &pd->waitOverlapped, &transferred, FALSE)) {
        pd->pendingWait = false;
        startWaitCommEvent(pd); // 重新监听
        if (pd->owner) {
            XIODevice_readyRead_signal((XIODevice*)pd->owner); // ✅ 安全 emit
        }
    }
}
// NEW: pinoutSignals
XSerialPort_PinoutSignal platform_pinoutSignals(const XSerialPortPrivate* d) {
    if (!d || !d->isOpen) return XSerialPort_NoSignal;
    DWORD modemStat;
    if (!GetCommModemStatus((HANDLE)d->platform->handle, &modemStat)) return XSerialPort_NoSignal;
    XSerialPort_PinoutSignal signals = XSerialPort_NoSignal;
    if (modemStat & MS_CTS_ON) signals |= XSerialPort_ClearToSendSignal;
    if (modemStat & MS_DSR_ON) signals |= XSerialPort_DataSetReadySignal;
    if (modemStat & MS_RING_ON) signals |= XSerialPort_RingIndicatorSignal;
    if (modemStat & MS_RLSD_ON) signals |= XSerialPort_DataCarrierDetectSignal;
    if (d->dataTerminalReady) signals |= XSerialPort_DataTerminalReadySignal;
    if (d->requestToSend) signals |= XSerialPort_RequestToSendSignal;
    return signals;
}

bool platform_applyConfig(XSerialPortPrivate* d) 
{
    PlatformData* pd = d->platform;
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(pd->handle, &dcb)) return false;

    dcb.BaudRate = toDCBRate(d->baudRate);
    dcb.ByteSize = toDCBDataBits(d->dataBits);
    dcb.Parity = toDCBParity(d->parity);
    dcb.StopBits = toDCBStopBits(d->stopBits);

    // 流控设置
    switch (d->flowControl) {
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

    return SetCommState(pd->handle, &dcb) != FALSE;
}
//WaitFor implementations
bool platform_waitForReadyRead(XSerialPortPrivate * d, int msecs) {
    if (!d->isOpen) return false;

    // Check if already available
    if (platform_bytesAvailable(d) > 0) return true;

    XMutex_lock(d->waitMutex);
    d->readyReadTriggered = false;
    XMutex_unlock(d->waitMutex);

    // Wait for signal or timeout
    XMutex_lock(d->waitMutex);
    bool result = XWaitCondition_wait(d->waitCondition, d->waitMutex, msecs);
    if (!result) {
        // Timeout occurred
        d->error = XSerialPort_TimeoutError;
    }
    bool triggered = d->readyReadTriggered;
    d->readyReadTriggered = false;
    XMutex_unlock(d->waitMutex);

    return triggered || platform_bytesAvailable(d) > 0;
}

bool platform_waitForBytesWritten(XSerialPortPrivate* d, int msecs) {
    if (!d->isOpen) return false;

    // Check if already written
    if (platform_bytesToWrite(d) == 0) return true;

    XMutex_lock(d->waitMutex);
    d->bytesWrittenTriggered = false;
    XMutex_unlock(d->waitMutex);

    // Wait for signal or timeout
    XMutex_lock(d->waitMutex);
    bool result = XWaitCondition_wait(d->waitCondition, d->waitMutex, msecs);
    if (!result) {
        // Timeout occurred
        d->error = XSerialPort_TimeoutError;
    }
    bool triggered = d->bytesWrittenTriggered;
    d->bytesWrittenTriggered = false;
    XMutex_unlock(d->waitMutex);

    return triggered || platform_bytesToWrite(d) == 0;
}
// ========== Control Functions ==========
bool platform_setDataTerminalReady(XSerialPortPrivate* d, bool set)
{
    if (!d->isOpen) return false;
    return EscapeCommFunction((HANDLE)d->platform->handle, set ? SETDTR : CLRDTR);
}
bool platform_setRequestToSend(XSerialPortPrivate* d, bool set)
{
    if (d->isOpen && d->flowControl != XSerialPort_HardwareControl) {
        return EscapeCommFunction((HANDLE)d->platform->handle, set ? SETRTS : CLRRTS);
    }
    return false;
}
bool platform_setBreakEnabled(XSerialPortPrivate* d, bool enable)
{
    if (!d->isOpen) return false;
    return EscapeCommFunction((HANDLE)d->platform->handle, enable ? SETBREAK : CLRBREAK);
}

// ========== Flush / Clear ==========
bool platform_flush(XSerialPortPrivate* d) {

    if (!FlushFileBuffers((HANDLE)d->platform->handle)) {
        d->error = XSerialPort_WriteError;
        return false;
    }
    return true;
}

bool platform_clear(XSerialPortPrivate* d, XSerialPort_Direction directions)
{
    DWORD flags = 0;
    if (directions & XSerialPort_Input)  flags |= PURGE_RXCLEAR;
    if (directions & XSerialPort_Output) flags |= PURGE_TXCLEAR;

    if (!PurgeComm((HANDLE)d->platform->handle, flags)) {
        d->error = XSerialPort_ResourceError;
        return false;
    }
    return true;
}

#endif // _WIN32