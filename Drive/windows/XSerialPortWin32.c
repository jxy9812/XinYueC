#if defined(_WIN32)
#include <windows.h>
#include <stdio.h>
#include "XSerialPort.h"
#include "XMemory.h"
#include "XMutex.h"
#include "XWaitCondition.h"
#include "XSocketNotifier.h"
#include "XCoreApplication.h"
#include "XAbstractEventDispatcher.h"
#include "XIODevicePrivate.h"
#include "IOCPInfo.h"
//XEventDispatcher_win.c引入
bool IOCP_bind(XSocketDescriptor socket, XObject* obj);
bool XSerialPort_platform_applyConfig(XSerialPort* port);
static DWORD toDCBRate(int32_t rate);
static BYTE toDCBDataBits(XSerialPort_DataBits d);
static BYTE toDCBParity(XSerialPort_Parity p);
static BYTE toDCBStopBits(XSerialPort_StopBits sb);
//缓冲区大小
#define BUFFSIZE 2048
typedef struct XSerialPortWin32
{
    XSerialPort base;
    XEventContext_IOCP read;
    XEventContext_IOCP write;
    HANDLE handle;
    bool isWritePending; // 新增：标记是否有写操作在进行
    char readBuff[BUFFSIZE];
    char writeBuff[BUFFSIZE];
} XSerialPortWin32;
size_t XSerialPort_typetSize()
{
    return sizeof(XSerialPortWin32);
}
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
    XSerialPortWin32* win32 = (XSerialPortWin32*)serial;
    win32->isWritePending =false;
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
    //if (!port || win32->pendingWait) return true;

    //DWORD events = 0;
    //if (!WaitCommEvent(win32->handle, &win32->lastEvents, &win32->waitOverlapped)) {
    //    DWORD err = GetLastError();
    //    if (err == ERROR_IO_PENDING)
    //    {
    //        win32->pendingWait = true;
    //        return true;
    //    }
    //    printf("WaitCommEvent failed with error %lu\n", err);
    //    return false;
    //}
    //win32->pendingWait = true;
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
void XSerialPort_platform_XChildEvent_handler(XEventSockAct* event, XSerialPort* receiver)
{
    XSerialPortWin32* win32 = (XSerialPortWin32*)receiver;
    if (!event)return;
    int currentReadChannel = 0;
    if ((event->actType & XSocketAct_Read))
    {
        if(win32->read.finishedBytes)
        {
            //写入到缓冲区
            XIODevicePrivate* d = ((XIODevice*)receiver)->m_d;
            // 获取当前读通道ID
         
            struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(d, currentReadChannel);
            if (!readBuf) {
                return -1; // 无法获取或创建缓冲区
            }

            // 1. 首先尝试从当前通道的缓冲区读取
            int64_t bytesFromBuffer = XRingBuffer_write(readBuf, win32->read.buffer, win32->read.finishedBytes);
            if (bytesFromBuffer)
            {
                XIODevice_readyRead_signal(receiver);
                XIODevice_channelReadyRead_signal(receiver, currentReadChannel);
            }
        }
        //再次读取
        //win32->read.type = XEventContextType_Type_File;
        //win32->read.buffer = win32->readBuff;
        //win32->read.bufferSize = BUFFSIZE;
        //win32->read.eventMask = FD_READ;
        //win32->read.socket = XSocketDescriptor_fromIntptr(win32->handle);
        win32->read.finishedBytes = 0;
        BOOL r = ReadFile(win32->handle, win32->readBuff, (DWORD)win32->read.bufferSize, &win32->read
            .finishedBytes, &win32->read);
    }
    if (event->actType & XSocketAct_Write)
    {
        if (win32->write.finishedBytes)
        {
            XIODevice_bytesWritten_signal(receiver, win32->write.finishedBytes);
            XIODevice_channelBytesWritten_signal(receiver, currentReadChannel, win32->write.finishedBytes);
        }
        //写入到缓冲区
        XIODevicePrivate* d = ((XIODevice*)receiver)->m_d;
        // 获取当前读通道ID
        struct XRingBuffer* writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentReadChannel);
        if (!writeBuf) {
            return -1; // 无法获取或创建缓冲区
        }
        int64_t bytesFromBuffer = XRingBuffer_read(writeBuf, win32->write.buffer, win32->write.bufferSize);
        if (bytesFromBuffer)
        {
           BOOL r = WriteFile(win32->handle, win32->writeBuff, (DWORD)bytesFromBuffer, &win32->write
               .finishedBytes, &win32->write);
           win32->isWritePending = true;
        }
        else
        {
            win32->isWritePending = false;
        }
       
    }
    XEvent_setAccepted_base(event,true);
}
int64_t XSerialPort_platform_read(XSerialPort* port, char* data, int64_t maxSize);

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

    // 打开串口：必须使用 FILE_FLAG_OVERLAPPED
    HANDLE h = CreateFileA(fullPortName, access, 0, NULL, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, NULL);

    if (h == INVALID_HANDLE_VALUE) return false;

    // 设置缓冲区大小（可选）
    if (port->readBufferSize > 0) {
        DWORD writeBuf = (port->readBufferSize > 2048) ? (DWORD)port->readBufferSize : 2048;
        SetupComm(h, (DWORD)port->readBufferSize, writeBuf);
    }

    // 设置超时：非阻塞模式（对重叠 I/O 实际影响不大，但建议设为 0）
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD; // 仅当有第一个字节后才计时
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    SetCommTimeouts(h, &timeouts);
    // 清空缓冲区
    PurgeComm(h, PURGE_TXCLEAR | PURGE_RXCLEAR);

    //// >>>>>> 关键修复：必须调用 SetCommMask <<<<<<
    //if (!SetCommMask(h, EV_RXCHAR | EV_ERR)) {
    //    DWORD err = GetLastError();
    //    printf("SetCommMask failed with error %lu\n", err);
    //    CloseHandle(h);
    //    return false;
    //}
    XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
   /* XSocketNotifier_setSocket(win32->notifier, XSocketDescriptor_fromIntptr(h));
    XAbstractEventDispatcher_registerSocketNotifier_base(dispatcher,win32->notifier);*/
    if (!IOCP_bind(XSocketDescriptor_fromIntptr(h), port))
    {
        CloseHandle(h);
        return false;
    }
    win32->handle = h;
    // >>>>>> 关键：显式清零 OVERLAPPED <<<<<<
 /*   memset(&win32->waitOverlapped, 0, sizeof(OVERLAPPED));
    win32->waitEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!win32->waitEvent) {
        CloseHandle(h);
        return false;
    }
    win32->waitOverlapped.hEvent = win32->waitEvent;*/

    // 应用串口配置
    if (!XSerialPort_platform_applyConfig(port)) {
        //CloseHandle(win32->waitEvent);
        CloseHandle(h);
        return false;
    }

    // 启动事件监听
   /* if (!startWaitCommEvent(port)) {
        CloseHandle(win32->waitEvent);
        CloseHandle(h);
        return false;
    }*/
  
    port->isOpen = true;
    //XObject_setPollTime(port, 10);

    //打开成功发起异步接收
    memset(&win32->read, 0, sizeof(OVERLAPPED)); // hEvent 必须为 NULL！
    win32->read.type = XEventContextType_Type_File;
    win32->read.buffer = win32->readBuff;
    win32->read.bufferSize = BUFFSIZE;
    win32->read.eventMask = FD_READ;
    win32->read.socket=XSocketDescriptor_fromIntptr(win32->handle);
    win32->read.finishedBytes = 0;
    BOOL r = ReadFile(win32->handle, win32->readBuff, (DWORD)BUFFSIZE, &win32->read
        .finishedBytes, &win32->read);
    return true;
}

void XSerialPort_platform_close(XSerialPort* port) {
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    if (!port->isOpen) return;
    //XObject_setPollTime(port, 0);
    CancelIo(win32->handle);
    PurgeComm(win32->handle, PURGE_TXABORT | PURGE_RXABORT);
    CloseHandle(win32->handle);
    win32->handle = 0;

    XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
    //XAbstractEventDispatcher_unregisterSocketNotifier_base(dispatcher, win32->notifier);
    /*if (win32->waitEvent) 
    {
        CloseHandle(win32->waitEvent);
        win32->waitEvent = 0;
    }*/

    port->isOpen = false;
}

int64_t XSerialPort_platform_read(XSerialPort* port, char* data, int64_t maxSize) 
{
    if (!port || !data || maxSize <= 0) return -1;
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    return -1;
    /*OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return -1;*/
    memset(&win32->read, 0, sizeof(OVERLAPPED)); // hEvent 必须为 NULL！
    win32->read.type = XEventContextType_Type_File;
    win32->read.buffer = data;
    win32->read.bufferSize = maxSize;
    win32->read.eventMask = FD_READ;
    DWORD bytesRead = 0;
    BOOL r = ReadFile(win32->handle, data, (DWORD)maxSize, &bytesRead, &win32->read);
    if (!r) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            // 真正的错误（如同步失败）
            //free(ctx);
            return false;
        }
        // 否则：I/O pending，等待 IOCP 通知
    }
    else {
        // 极少数情况：同步完成（如驱动缓存中有数据）
       
    }
    return r ? (int64_t)bytesRead : -1;
}

int64_t XSerialPort_platform_write(XSerialPort* port, const char* data, int64_t len) {
    if (!port || !data || len <= 0) return -1;
    XSerialPortWin32* win32 = (XSerialPortWin32*)port;
    XIODevicePrivate* d = ((XIODevice*)port)->m_d;
    int currentWriteChannel = XIODevice_currentWriteChannel(port);
    struct XRingBuffer* writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentWriteChannel);
    size_t written = 0;
    if (win32->isWritePending)
    {//当前还在写操作，写到缓冲区
       
        written += XRingBuffer_write(writeBuf, data, (size_t)len);
    }
    else
    {
        win32->write.type = XEventContextType_Type_File;
        win32->write.buffer = win32->writeBuff;
        win32->write.bufferSize = BUFFSIZE;
        win32->write.eventMask = FD_WRITE;
        win32->write.socket = XSocketDescriptor_fromIntptr(win32->handle);
        win32->write.finishedBytes = 0;
        win32->isWritePending = true;

        if(len<= BUFFSIZE)
        {
            memcpy(win32->writeBuff, data, len);
            written += WriteFile(win32->handle, win32->writeBuff, (DWORD)len, &win32->write
                .finishedBytes, &win32->write);
        }
        else
        {
            memcpy(win32->writeBuff, data, BUFFSIZE);
            written += WriteFile(win32->handle, win32->writeBuff, (DWORD)BUFFSIZE, &win32->write
                .finishedBytes, &win32->write);
            //剩余的进缓冲器
            written += XRingBuffer_write(writeBuf, data+ BUFFSIZE, (size_t)len- BUFFSIZE);
        }
    }
    return written;
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

    //if (!win32->pendingWait) return;

    //DWORD transferred = 0;
    //if (GetOverlappedResult(win32->handle, &win32->waitOverlapped, &transferred, FALSE)) {
    //    win32->pendingWait = false;
    //    startWaitCommEvent(win32); // 重新监听
    //    XIODevice_readyRead_signal((XIODevice*)port); // ✅ 安全 emit
    //}
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
    if (XSerialPort_bytesAvailable_base(port) > 0) return true;

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

    return triggered || XSerialPort_bytesAvailable_base(port) > 0;
}

bool XSerialPort_platform_waitForBytesWritten(XSerialPort* port, int msecs) {
    if (XSerialPort_bytesToWrite_base(port) == 0) return true;

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

    return triggered || XSerialPort_bytesToWrite_base(port) == 0;
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