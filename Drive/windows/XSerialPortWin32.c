#if defined(_WIN32)
#include <windows.h>
#include <stdio.h>
#include "XSerialPort.h"
#include "XRingBuffer.h"
#include "XMemory.h"
#include "XMutex.h"
#include "XWaitCondition.h"
#include "XSocketNotifier.h"
#include "XCoreApplication.h"
#include "XAbstractEventDispatcher.h"
#include "XIODevicePrivate.h"
#include "IOCPInfo.h"

bool IOCP_bind(XSocketDescriptor socket, XObject* obj);
bool XSerialPort_platform_applyConfig(XSerialPort* port);
static DWORD toDCBRate(int32_t rate);
static BYTE toDCBDataBits(XSerialPort_DataBits d);
static BYTE toDCBParity(XSerialPort_Parity p);
static BYTE toDCBStopBits(XSerialPort_StopBits sb);
//缓冲区大小
#define BUFFSIZE 2048

/** XSerialPort 平台私有数据（继承 XIODevicePrivate） */
typedef struct XSerialPortPrivate 
{
    XIODevicePrivate base;          /**< 第一位继承父类私有数据 */
    XEventContext_IOCP read;
    XEventContext_IOCP write;
    HANDLE handle;
    char readBuff[BUFFSIZE];
    char writeBuff[BUFFSIZE];
} XSerialPortPrivate;

/* 便捷转换宏 */
#define SPP(p) ((XSerialPortPrivate*)((XIODevice*)(p))->m_d)

size_t XSerialPort_typetSize()
{
    return sizeof(XSerialPort);
}

/* =========================================================================
 * 私有数据管理
 * ========================================================================= */
static XSerialPortPrivate* XSerialPortPrivate_create(void)
{
    XSerialPortPrivate* priv = (XSerialPortPrivate*)XCalloc_System(1, sizeof(XSerialPortPrivate));
    if (!priv) return NULL;
    XIODevicePrivate_init(&priv->base, NULL);
    priv->handle = INVALID_HANDLE_VALUE;
    return priv;
}

static void XSerialPortPrivate_delete(XSerialPortPrivate* priv)
{
    if (!priv) return;
    XIODevicePrivate_deinit(&priv->base);
    XFree_System(priv);
}

void XSerialPort_init(XSerialPort* serial)
{
    if (serial == NULL)
        return;
    // 只清零 XSerialPort 部分
    memset(((XIODevice*)serial) + 1, 0, sizeof(XSerialPort) - sizeof(XIODevice));
    XIODevice_init(serial);

    /* 替换父类默认的 XIODevicePrivate 为 XSerialPortPrivate */
    XSerialPortPrivate* priv = XSerialPortPrivate_create();
    if (!priv) return;
    if (((XIODevice*)serial)->m_d)
        XIODevicePrivate_delete(((XIODevice*)serial)->m_d);
    ((XIODevice*)serial)->m_d = (XIODevicePrivate*)priv;
    priv->base.q_ptr = (XIODevice*)serial;

    XClassGetVtable(serial) = XSerialPort_class_init();
    // 设置常用的默认串口参数
    serial->baudRate = XSerialPort_Baud9600;
    serial->dataBits = XSerialPort_Data8;
    serial->parity = XSerialPort_NoParity;
    serial->stopBits = XSerialPort_OneStop;
    serial->flowControl = XSerialPort_NoFlowControl;
    serial->readyReadTriggered = false;
    serial->bytesWrittenTriggered = true;
    // 设置默认的读缓冲区大小 (Qt 的默认值是 512 KB)
    serial->readBufferSize = 512 * 1024;
}

XSerialPort* XSerialPort_create()
{
    XSerialPort* port = XNew(XSerialPort);
    if (!port)return NULL;
    XSerialPort_init(port);
    Set_Class_MemoryFree(port, XFree_System);
    return port;
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
    case XSerialPort_Data5: return 5;
    case XSerialPort_Data6: return 6;
    case XSerialPort_Data7: return 7;
    case XSerialPort_Data8: return 8;
    default: return 8;
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

/* =========================================================================
 * 事件处理
 * ========================================================================= */
void XSerialPort_platform_XChildEvent_handler(XEventSockAct* event, XSerialPort* receiver)
{
    XSerialPortPrivate* priv = SPP(receiver);
    if (!event || !priv) return;
    int currentReadChannel = 0;
    if ((event->actType & XSocketAct_Read))
    {
        if(priv->read.finishedBytes)
        {
            XIODevicePrivate* d = ((XIODevice*)receiver)->m_d;
            struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(d, currentReadChannel);
            if (!readBuf) return;

            int64_t bytesFromBuffer = XRingBuffer_write(readBuf, priv->read.buffer, priv->read.finishedBytes);
            if (bytesFromBuffer)
            {
                receiver->readyReadTriggered = true;
                XIODevice_readyRead_signal(receiver);
                XIODevice_channelReadyRead_signal(receiver, currentReadChannel);
            }
        }
        priv->read.finishedBytes = 0;
        memset(&priv->read, 0, sizeof(OVERLAPPED));
        ReadFile(priv->handle, priv->readBuff, (DWORD)priv->read.bufferSize, &priv->read.finishedBytes, &priv->read);
    }
    if (event->actType & XSocketAct_Write)
    {
        if (priv->write.finishedBytes)
        {
            XIODevice_bytesWritten_signal(receiver, priv->write.finishedBytes);
            XIODevice_channelBytesWritten_signal(receiver, currentReadChannel, priv->write.finishedBytes);
        }
        XIODevicePrivate* d = ((XIODevice*)receiver)->m_d;
        struct XRingBuffer* writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentReadChannel);
        if (!writeBuf) return;

        int64_t bytesFromBuffer = XRingBuffer_read(writeBuf, priv->write.buffer, priv->write.bufferSize);
        if (bytesFromBuffer)
        {
            WriteFile(priv->handle, priv->writeBuff, (DWORD)bytesFromBuffer, &priv->write.finishedBytes, &priv->write);
            receiver->bytesWrittenTriggered = false;
        }
        else
        {
            receiver->bytesWrittenTriggered = true;
        }
    }
    XEvent_setAccepted_base(event,true);
}

/* =========================================================================
 * 平台函数实现
 * ========================================================================= */
bool XSerialPort_platform_open(XSerialPort* port, XIODeviceBaseMode mode) {
    if (!port->portName) return false;
    XSerialPortPrivate* priv = SPP(port);
    if (!priv) return false;
    
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

    if (port->readBufferSize > 0) {
        DWORD writeBuf = (port->readBufferSize > 2048) ? (DWORD)port->readBufferSize : 2048;
        SetupComm(h, (DWORD)port->readBufferSize, writeBuf);
    }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    SetCommTimeouts(h, &timeouts);
    PurgeComm(h, PURGE_TXCLEAR | PURGE_RXCLEAR);

    XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
    if (!IOCP_bind(XSocketDescriptor_fromIntptr(h), port))
    {
        CloseHandle(h);
        return false;
    }
    priv->handle = h;

    if (!XSerialPort_platform_applyConfig(port)) {
        CloseHandle(h);
        return false;
    }

    port->isOpen = true;

    /* 分配 XFileDescriptor 统一标识符 */
    XIODevicePrivate* d = ((XIODevice*)port)->m_d;
    if (d->xfd == XFD_INVALID) {
        d->xfd = XFd_alloc(XFD_TYPE_SERIAL, priv, (XIODevice*)port);
    }

    //打开成功发起异步接收
    memset(&priv->read, 0, sizeof(OVERLAPPED));
    priv->read.base.type = XEventContextType_Type_File;
    priv->read.base.fd = priv->base.xfd;
    priv->read.buffer = priv->readBuff;
    priv->read.bufferSize = BUFFSIZE;
    priv->read.eventMask = FD_READ;
    priv->read.socket = XSocketDescriptor_fromIntptr(priv->handle);
    priv->read.finishedBytes = 0;
    ReadFile(priv->handle, priv->readBuff, (DWORD)BUFFSIZE, &priv->read.finishedBytes, &priv->read);
    return true;
}

void XSerialPort_platform_close(XSerialPort* port) {
    XSerialPortPrivate* priv = SPP(port);
    if (!port->isOpen || !priv) return;
    
    CancelIo(priv->handle);
    PurgeComm(priv->handle, PURGE_TXABORT | PURGE_RXABORT);
    CloseHandle(priv->handle);
    priv->handle = INVALID_HANDLE_VALUE;

    XAbstractEventDispatcher* dispatcher = XCoreApplication_eventDispatcher();
    port->isOpen = false;
}

int64_t XSerialPort_platform_read(XSerialPort* port, char* data, int64_t maxSize) 
{
    if (!port || !data || maxSize <= 0) return -1;
    XSerialPortPrivate* priv = SPP(port);
    XIODevicePrivate* d = (XIODevicePrivate*)priv;
    int currentReadChannel = XIODevice_currentReadChannel(port);
    struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(d, currentReadChannel);
    if (!readBuf) return -1;

    while (XRingBuffer_size_base(readBuf) < (size_t)maxSize)
    {
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    }
    return XRingBuffer_read(readBuf, data, maxSize);
}

int64_t XSerialPort_platform_write(XSerialPort* port, const char* data, int64_t len) {
    if (!port || !data || len <= 0) return -1;
    XSerialPortPrivate* priv = SPP(port);
    XIODevicePrivate* d = (XIODevicePrivate*)priv;
    int currentWriteChannel = XIODevice_currentWriteChannel(port);
    struct XRingBuffer* writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentWriteChannel);
    int64_t written = 0;

    if (!port->bytesWrittenTriggered)
    {
        written = XRingBuffer_write(writeBuf, data, (size_t)len);
    }
    else
    {
        memset(&priv->write, 0, sizeof(OVERLAPPED));
        priv->write.base.type = XEventContextType_Type_File;
        priv->write.base.fd = priv->base.xfd;
        priv->write.buffer = priv->writeBuff;
        priv->write.bufferSize = BUFFSIZE;
        priv->write.eventMask = FD_WRITE;
        priv->write.socket = XSocketDescriptor_fromIntptr(priv->handle);
        priv->write.finishedBytes = 0;
        port->bytesWrittenTriggered = false;

        size_t toWrite = (len <= BUFFSIZE) ? (size_t)len : BUFFSIZE;
        memcpy(priv->writeBuff, data, toWrite);
        BOOL success = WriteFile(priv->handle, priv->writeBuff, (DWORD)toWrite,
            &priv->write.finishedBytes, &priv->write);

        if (success) {
            written = priv->write.finishedBytes;
        }
        else {
            DWORD error = GetLastError();
            if (error == ERROR_IO_PENDING) {
                written = toWrite;
            }
            else {
                port->bytesWrittenTriggered = true;
                return -1;
            }
        }

        if (len > BUFFSIZE) {
            written += XRingBuffer_write(writeBuf, data + BUFFSIZE, (size_t)len - BUFFSIZE);
        }
    }
    return written;
}

bool XSerialPort_setDataBits(XSerialPort* port, XSerialPort_DataBits dataBits) {
    if (!port) return false;
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
bool XSerialPort_platform_applyConfig(XSerialPort* port)
{
    XSerialPortPrivate* priv = SPP(port);
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(priv->handle, &dcb)) return false;

    dcb.BaudRate = toDCBRate(port->baudRate);
    dcb.ByteSize = toDCBDataBits(port->dataBits);
    dcb.Parity = toDCBParity(port->parity);
    dcb.StopBits = toDCBStopBits(port->stopBits);

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

    return SetCommState(priv->handle, &dcb) != FALSE;
}

XSerialPort_PinoutSignal XSerialPort_pinoutSignals(const XSerialPort* port)
{
    XSerialPortPrivate* priv = SPP(port);
    if (!port || !port->isOpen) return XSerialPort_NoSignal;
    DWORD modemStat;
    if (!GetCommModemStatus((HANDLE)priv->handle, &modemStat)) return XSerialPort_NoSignal;
    XSerialPort_PinoutSignal signals = XSerialPort_NoSignal;
    if (modemStat & MS_CTS_ON) signals |= XSerialPort_ClearToSendSignal;
    if (modemStat & MS_DSR_ON) signals |= XSerialPort_DataSetReadySignal;
    if (modemStat & MS_RING_ON) signals |= XSerialPort_RingIndicatorSignal;
    if (modemStat & MS_RLSD_ON) signals |= XSerialPort_DataCarrierDetectSignal;
    if (port->dataTerminalReady) signals |= XSerialPort_DataTerminalReadySignal;
    if (port->requestToSend) signals |= XSerialPort_RequestToSendSignal;
    return signals;
}

bool XSerialPort_setDataTerminalReady(XSerialPort* port, bool set) {
    if (!port) return false;
    XSerialPortPrivate* priv = SPP(port);
    if (port->dataTerminalReady == set) return true;

    if (port->isOpen && EscapeCommFunction((HANDLE)priv->handle, set ? SETDTR : CLRDTR))
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
    XSerialPortPrivate* priv = SPP(port);
    if (port->requestToSend == set) return true;

    if (port->isOpen && port->flowControl != XSerialPort_HardwareControl &&
        EscapeCommFunction((HANDLE)priv->handle, set ? SETRTS : CLRRTS))
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
    XSerialPortPrivate* priv = SPP(port);
    if (port->breakEnabled == enable) return true;
    if (port->isOpen && EscapeCommFunction((HANDLE)priv->handle, enable ? SETBREAK : CLRBREAK))
    {
        port->breakEnabled = enable;
        XSerialPort_breakEnabledChanged_signal(port, enable);
        return true;
    }
    return false;
}
bool XSerialPort_flush(XSerialPort* port)
{
    if (!port) return false;
    XSerialPortPrivate* priv = SPP(port);
    if (!FlushFileBuffers((HANDLE)priv->handle)) {
        port->error = XSerialPort_WriteError;
        return false;
    }
    return true;
}

bool XSerialPort_clear(XSerialPort* port, XSerialPort_Direction directions)
{
    if (!port) return false;
    XSerialPortPrivate* priv = SPP(port);
    DWORD flags = 0;
    if (directions & XSerialPort_Input)  flags |= PURGE_RXCLEAR;
    if (directions & XSerialPort_Output) flags |= PURGE_TXCLEAR;

    if (!PurgeComm((HANDLE)priv->handle, flags)) {
        port->error = XSerialPort_ResourceError;
        return false;
    }
    return true;
}
XHandle XSerialPort_platform_handle(const XSerialPort* port)
{
    if (!port) return -1;
    XSerialPortPrivate* priv = SPP(port);
    return (XHandle)(priv->handle);
}
#endif // _WIN32