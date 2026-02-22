#ifdef WIN32
#include "XSerialPort.h"
#include "XCircularQueue.h"
#include "XPrintf.h"
#include "XMemory.h"
#include <windows.h>

// 手动定义ERROR_IO_COMPLETION（如果系统头文件未包含）
#ifndef ERROR_IO_COMPLETION
#define ERROR_IO_COMPLETION 996
#endif

// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")

typedef struct XSerialPort {
    XSerialPortBase base;
    HANDLE m_hCom;              // Windows串口句柄
    DCB m_dcb;                  // 设备控制块
    COMMTIMEOUTS m_timeouts;    // 超时设置
    void* m_ovRead;
    void* m_ovWrite;
} XSerialPort;

// 前向声明
static void VXSerialPort_deinit(XSerialPort* serial);
static bool VXSerialPort_open(XSerialPort* serial, XIODeviceBaseMode mode);
static size_t VXIODevice_write(XSerialPort* serial, const char* data, size_t maxSize);
static size_t VXIODevice_writeFull(XSerialPort* serial);
static size_t VXIODevice_read(XSerialPort* serial, char* data, size_t maxSize);
static void VXIODevice_close(XSerialPort* serial);
static void VXIODevice_poll(XSerialPort* serial);
static void VXIODevice_setWriteBuffer(XSerialPort* serial, size_t count);
static void VXIODevice_setReadBuffer(XSerialPort* serial, size_t count);
static size_t VXIODevice_getBytesAvailable(XSerialPort* serial);

// 辅助函数：检查异步操作结果
static DWORD check_async_result(HANDLE hSerial, OVERLAPPED* ov, DWORD* bytesTransferred) {
    DWORD result = WaitForSingleObject(ov->hEvent, 0); // 非阻塞等待
    if (result == WAIT_OBJECT_0) {
        if (!GetOverlappedResult(hSerial, ov, bytesTransferred, FALSE)) {
            return GetLastError();
        }
        return ERROR_SUCCESS;
    }
    return ERROR_IO_PENDING;
}

XVtable* XSerialPort_class_init()
{
    XVTABLE_CREAT_DEFAULT
        // 虚函数表初始化
#if VTABLE_ISSTACK
        XVTABLE_STACK_INIT_DEFAULT(XSERIALPORT_VTABLE_SIZE)
#else
        XVTABLE_HEAP_INIT_DEFAULT
#endif
        // 继承类
        XVTABLE_INHERIT_DEFAULT(XIODeviceBase_class_init());
    // 重载
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSerialPort_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open, VXSerialPort_open);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Write, VXIODevice_write);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_WriteFull, VXIODevice_writeFull);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Read, VXIODevice_read);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Close, VXIODevice_close);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXIODevice_poll);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_SetWriteBuffer, VXIODevice_setWriteBuffer);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_SetReadBuffer, VXIODevice_setReadBuffer);
    XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_GetBytesAvailable, VXIODevice_getBytesAvailable);
#if SHOWCONTAINERSIZE
    printf("XSerialPort size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
    return XVTABLE_DEFAULT;
}

XSerialPort* XSerialPort_create()
{
    XSerialPort* serial = XMemory_malloc(sizeof(XSerialPort));
    if (serial == NULL)
        return serial;
    XSerialPort_init(serial);
    return serial;
}

void XSerialPort_init(XSerialPort* serial)
{
    if (serial == NULL)
        return;
    memset(((XSerialPortBase*)serial) + 1, 0, sizeof(XSerialPort) - sizeof(XSerialPortBase));
    XSerialPortBase_init(serial);
    serial->m_hCom = INVALID_HANDLE_VALUE;
    memset(&(serial->m_dcb), 0, sizeof(DCB));
    memset(&(serial->m_timeouts), 0, sizeof(COMMTIMEOUTS));
    XClassGetVtable(serial) = XSerialPort_class_init();

    // 初始化读操作的OVERLAPPED和事件
    serial->m_ovRead = XMemory_malloc(sizeof(OVERLAPPED));
    if (serial->m_ovRead) {
        memset(serial->m_ovRead, 0, sizeof(OVERLAPPED));
        ((OVERLAPPED*)serial->m_ovRead)->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    }

    // 初始化写操作的OVERLAPPED和事件
    serial->m_ovWrite = XMemory_malloc(sizeof(OVERLAPPED));
    if (serial->m_ovWrite) {
        memset(serial->m_ovWrite, 0, sizeof(OVERLAPPED));
        ((OVERLAPPED*)serial->m_ovWrite)->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    }
  
    // 默认缓冲区大小
    XIODeviceBase_setWriteBuffer_base(serial,1024);
    XIODeviceBase_setReadBuffer_base(serial, 1024);

}

void VXSerialPort_deinit(XSerialPort* serial)
{
    // 释放读事件
    if (serial->m_ovRead) {
        CloseHandle(((OVERLAPPED*)serial->m_ovRead)->hEvent);
        XMemory_free(serial->m_ovRead);
        serial->m_ovRead = NULL;
    }
    // 释放写事件
    if (serial->m_ovWrite) {
        CloseHandle(((OVERLAPPED*)serial->m_ovWrite)->hEvent);
        XMemory_free(serial->m_ovWrite);
        serial->m_ovWrite = NULL;
    }
    // 释放父对象
    XVtableGetFunc(XIODeviceBase_class_init(), EXClass_Deinit, void(*)(XIODeviceBase*))(serial);
}

bool VXSerialPort_open(XSerialPort* serial, XIODeviceBaseMode mode)
{
    if (serial == NULL)
        return false;

    if (serial->m_hCom != INVALID_HANDLE_VALUE) {
        return false; // 已打开
    }

    XSerialPortBase* parent = (XSerialPortBase*)serial;
    if (parent->m_dataBits == XSerialPort_Data9 || parent->m_stopBits == XSerialPort__ZeroPointFive)
        return false; // 当前平台不支持

    char portName[10] = { 0 };
    sprintf(portName, "\\\\.\\COM%d", parent->m_portNum);

    // 打开串口，使用重叠I/O
    serial->m_hCom = CreateFileA(
        portName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL);
    if (serial->m_hCom == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        XPrintf("无法打开串口%s！错误代码: %lu\n", portName, errorCode);
        return false;
    }

    //// 配置串口缓冲区
    //if (!SetupComm(hSerial, serial->m_readBufferSize? serial->m_readBufferSize:1024, serial->m_writeBufferSize? serial->m_writeBufferSize:1024)) {
    //    XPrintf("无法设置串口缓冲区！\n");
    //    CloseHandle(hSerial);
    //    return false;
    //}

    // 获取当前DCB配置
    serial->m_dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(serial->m_hCom, &serial->m_dcb)) {
        CloseHandle(serial->m_hCom);
        serial->m_hCom = INVALID_HANDLE_VALUE;
        parent->m_error = XSerialPort_OpenError;
        return false;
    }
    // 配置DCB
    serial->m_dcb.BaudRate = parent->m_baudRate;
    serial->m_dcb.ByteSize = parent->m_dataBits;  // 默认8位数据位
    serial->m_dcb.Parity = parent->m_parity;
    serial->m_dcb.StopBits = parent->m_stopBits;  // 默认1位停止位

    // 流控制设置
    switch (parent->m_flowControl) {
    case XSerialPort_NoFlowControl:
        serial->m_dcb.fInX = FALSE;
        serial->m_dcb.fOutX = FALSE;
        serial->m_dcb.fOutxCtsFlow = FALSE;
        serial->m_dcb.fRtsControl = RTS_CONTROL_DISABLE;
        break;
    case XSerialPort_HardwareControl:
        serial->m_dcb.fOutxCtsFlow = TRUE;
        serial->m_dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        break;
    case XSerialPort_SoftwareControl:
        serial->m_dcb.fInX = TRUE;
        serial->m_dcb.fOutX = TRUE;
        break;
    case XSerialPort_BothControl:
        serial->m_dcb.fInX = TRUE;
        serial->m_dcb.fOutX = TRUE;
        serial->m_dcb.fOutxCtsFlow = TRUE;
        serial->m_dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        break;
    }
    if (!SetCommState(serial->m_hCom, &serial->m_dcb)) {
        CloseHandle(serial->m_hCom);
        serial->m_hCom = INVALID_HANDLE_VALUE;
        parent->m_error = XSerialPort_OpenError;
        return false;
    }
     // 设置超时
    serial->m_timeouts.ReadIntervalTimeout = MAXDWORD;
    serial->m_timeouts.ReadTotalTimeoutMultiplier = 0;
    serial->m_timeouts.ReadTotalTimeoutConstant = 1000;  // 1秒超时
    serial->m_timeouts.WriteTotalTimeoutMultiplier = 0;
    serial->m_timeouts.WriteTotalTimeoutConstant = 1000; // 1秒超时

    if (!SetCommTimeouts(serial->m_hCom, &serial->m_timeouts)) {
        CloseHandle(serial->m_hCom);
        serial->m_hCom = INVALID_HANDLE_VALUE;
        parent->m_error = XSerialPort_OpenError;
        return false;
    }
    // 设置事件掩码
    if (!SetCommMask(serial->m_hCom, EV_RXCHAR | EV_TXEMPTY | EV_CTS | EV_DSR | EV_ERR)) {
        CloseHandle(serial->m_hCom);
        serial->m_hCom = INVALID_HANDLE_VALUE;
        parent->m_error = XSerialPort_OpenError;
        return false;
    }
    // 设置DTR和RTS
    EscapeCommFunction(serial->m_hCom, parent->m_dataTerminalReady ? SETDTR : CLRDTR);
    EscapeCommFunction(serial->m_hCom, parent->m_requestToSend ? SETRTS : CLRRTS);
    parent->m_class.m_mode = mode;
    // 启动异步读取
    if (mode & XIODeviceBase_ReadOnly) {
        char dummy;
        DWORD bytesRead;
        ReadFile(serial->m_hCom, &dummy, 1, &bytesRead, (OVERLAPPED*)serial->m_ovRead);
        ReadFile(serial->m_hCom, &dummy, 1, &bytesRead, (OVERLAPPED*)serial->m_ovWrite);
    }
    XObject_setPollingInterval(serial, 10);
    return true;
}

// 异步写入实现
static size_t XSerialPort_write_async(XSerialPort* serial, const char* data, size_t maxSize)
{
    if (serial == NULL || serial->m_hCom == INVALID_HANDLE_VALUE ||
        data == NULL || maxSize == 0) {
        return 0;
    }

    OVERLAPPED* ov = (OVERLAPPED*)serial->m_ovWrite;
    // 重置事件
    ResetEvent(ov->hEvent);

    DWORD bytesWritten;
    if (!WriteFile((HANDLE)serial->m_hCom, data, maxSize, &bytesWritten, ov)) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            XPrintf("写入失败，错误: %d\n", err);
            return 0;
        }
        // 异步操作正在进行，返回0表示操作已提交
        return 0;
    }

    // 同步完成的情况
    return bytesWritten;
}

size_t VXIODevice_write(XSerialPort* serial, const char* data, size_t maxSize)
{
    if (serial == NULL || data == NULL || maxSize == 0)
        return 0;

    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (!(io->m_mode & XIODeviceBase_WriteOnly))
        return 0;

    // 无缓冲区直接异步写入
    if (io->m_writeBuffer == NULL) {
        return XSerialPort_write_async(serial, data, maxSize);
    }

    // 有缓冲区时先写入队列
    size_t count = 0;
    do {
        // 填充缓冲区
        while (count < maxSize && XCircularQueue_push_base(io->m_writeBuffer, &data[count])) {
            count++;
        }

        // 检查是否有未完成的写入操作
        DWORD bytesTransferred;
        DWORD result = check_async_result((HANDLE)serial->m_hCom, serial->m_ovWrite, &bytesTransferred);

        // 如果写入操作已完成，尝试刷写缓冲区
        if (result == ERROR_SUCCESS) {
            VXIODevice_writeFull(serial);
        }

    } while (count < maxSize && !XCircularQueue_isFull_base(io->m_writeBuffer));

    return count;
}

size_t VXIODevice_writeFull(XSerialPort* serial)
{
    if (serial == NULL)
        return 0;

    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (!(io->m_mode & XIODeviceBase_WriteOnly))
        return 0;

    XCircularQueue* queue = io->m_writeBuffer;
    if (queue == NULL || XCircularQueue_isEmpty_base(queue))
        return 0;

    // 检查是否有未完成的写入
    OVERLAPPED* ov = (OVERLAPPED*)serial->m_ovWrite;
    DWORD bytesTransferred;
    DWORD result = check_async_result((HANDLE)serial->m_hCom, ov, &bytesTransferred);
    // 如果有未完成的操作，不进行新的写入
    if (result == ERROR_IO_PENDING) {
        return 0;
    }

    // 从缓冲区读取数据并异步发送
    size_t dataSize = XCircularQueue_size_base(queue);
    char* buffer = XMemory_malloc(dataSize);
    if (!buffer) return 0;

    // 从循环队列复制数据
    for (size_t i = 0; i < dataSize; i++) {
        XCircularQueue_receive_base(queue, &buffer[i]);
    }

    // 异步发送数据
    size_t written = XSerialPort_write_async(serial, buffer, dataSize);
    XMemory_free(buffer);

    return written;
}

// 异步读取实现
static size_t XSerialPort_read_async(XSerialPort* serial, char* data, size_t maxSize)
{
    if (serial == NULL || data == NULL || maxSize == 0 ||
        serial->m_hCom == INVALID_HANDLE_VALUE) {
        return 0;
    }

    OVERLAPPED* ov = (OVERLAPPED*)serial->m_ovRead;
    DWORD bytesRead;

    // 检查之前的读取操作是否完成
    DWORD result = check_async_result((HANDLE)serial->m_hCom, ov, &bytesRead);

    if (result == ERROR_SUCCESS && bytesRead > 0) {
        // 有数据可读
        //if (bytesRead > maxSize) bytesRead = maxSize;
        ////memcpy(data, &data[0], bytesRead); // 实际应用中应从缓冲区读取

        // 立即启动下一次异步读取
        ResetEvent(ov->hEvent);
        ReadFile((HANDLE)serial->m_hCom, data, maxSize, &bytesRead, ov);
        return bytesRead;
    }
    else if (result == ERROR_IO_PENDING) {
        // 读取操作仍在进行中
        return 0;
    }

    // 启动新的异步读取
    ResetEvent(ov->hEvent);
    if (!ReadFile((HANDLE)serial->m_hCom, data, maxSize, &bytesRead, ov)) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            XPrintf("读取失败: %d\n", err);
            return 0;
        }
        return 0; // 异步操作已启动，等待完成
    }

    // 同步读取到数据
    return bytesRead;
}

size_t VXIODevice_read(XSerialPort* serial, char* data, size_t maxSize)
{
    if (serial == NULL || data == NULL || maxSize == 0)
        return 0;

    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (!(io->m_mode & XIODeviceBase_ReadOnly))
        return 0;

    // 无缓冲区直接异步读取
    if (io->m_readBuffer == NULL) {
        return XSerialPort_read_async(serial, data, maxSize);
    }

    // 有缓冲区时从缓冲区读取
    size_t count = 0;
    while (count < maxSize && XCircularQueue_receive_base(io->m_readBuffer, &data[count])) {
        count++;
    }
    return count;
}

void VXIODevice_close(XSerialPort* serial)
{
    if (serial == NULL || serial->m_hCom == INVALID_HANDLE_VALUE||!XIODeviceBase_isOpen_base(serial))
        return;

    // 取消所有未完成的异步操作
    CancelIo((HANDLE)serial->m_hCom);
    CloseHandle((HANDLE)serial->m_hCom);
    serial->m_hCom = INVALID_HANDLE_VALUE;

    XObject_setPollingInterval(serial, 0);
    ((XIODeviceBase*)serial)->m_mode = XIODeviceBase_NotOpen;
}

// 轮询处理异步操作结果
static void VXIODevice_poll(XSerialPort* serial)
{
    if (serial == NULL || serial->m_hCom == INVALID_HANDLE_VALUE)
        return;

    XIODeviceBase* io = (XIODeviceBase*)serial;
    //OVERLAPPED* ov = (OVERLAPPED*)serial->m_ov;
    DWORD bytesTransferred;

    // 检查写入操作结果
    if (io->m_mode & XIODeviceBase_WriteOnly) {
        DWORD result = check_async_result((HANDLE)serial->m_hCom, serial->m_ovWrite, &bytesTransferred);
        if (result == ERROR_SUCCESS && bytesTransferred > 0) {
            // 发送写入完成信号
            XIODeviceBase_bytesWritten_signal(io, bytesTransferred);

            // 如果有写缓冲区且不为空，继续发送
            if (io->m_writeBuffer && !XCircularQueue_isEmpty_base(io->m_writeBuffer)) {
                VXIODevice_writeFull(serial);
            }
        }
    }

    // 检查读取操作结果
    if (io->m_readBuffer && (io->m_mode & XIODeviceBase_ReadOnly))
    {
        // 读取缓冲区数据
        char buffer[128];
        DWORD bytesRead;
        COMSTAT comStat;
        DWORD errors;

        if (ClearCommError((HANDLE)serial->m_hCom, &errors, &comStat) && comStat.cbInQue > 0) {
            bytesRead = comStat.cbInQue;
            if (bytesRead > sizeof(buffer)) bytesRead = sizeof(buffer);

            if (ReadFile((HANDLE)serial->m_hCom, buffer, bytesRead, &bytesRead, serial->m_ovRead)) {
                // 读取成功，放入缓冲区
                if (io->m_readBuffer) {
                    for (DWORD i = 0; i < bytesRead; i++) {
                        XCircularQueue_push_base(io->m_readBuffer, &buffer[i]);
                    }
                }
                // 发送读取信号
                XIODeviceBase_readyRead_signal(io);
            }
        }

        // 重新启动异步读取
        ResetEvent(serial->m_ovRead);
        //ReadFile((HANDLE)serial->m_hSerial, buffer, sizeof(buffer), NULL, ov);
    }
    else if(io->m_readBuffer==NULL && (io->m_mode & XIODeviceBase_ReadOnly))
    {
        if(XIODeviceBase_getBytesAvailable_base(serial))
            XIODeviceBase_readyRead_signal(io);// 发送读取信号
    }
}

static void VXIODevice_setWriteBuffer(XSerialPort* serial, size_t count)
{
    if (serial == NULL) return;
    //serial->m_writeBufferSize = count;
    //XIODeviceBase_setWriteBuffer_base((XIODeviceBase*)serial, count);
    XVtableGetFunc(XIODeviceBase_class_init(), EXIODeviceBase_SetWriteBuffer, void(*)(XIODeviceBase*, size_t))(serial, count);
}

static void VXIODevice_setReadBuffer(XSerialPort* serial, size_t count)
{
    if (serial == NULL) return;
    //serial->m_readBufferSize = count;
    //XIODeviceBase_setReadBuffer_base((XIODeviceBase*)serial, count);
    XVtableGetFunc(XIODeviceBase_class_init(), EXIODeviceBase_SetReadBuffer, void(*)(XIODeviceBase*, size_t))(serial, count);
}

static size_t VXIODevice_getBytesAvailable(XSerialPort* serial)
{
    if (serial == NULL || serial->m_hCom == INVALID_HANDLE_VALUE)
        return 0;

    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (io->m_readBuffer) {
        return XCircularQueue_size_base(io->m_readBuffer);
    }

    COMSTAT comStat;
    DWORD errors;
    // 清除通信错误并获取串口状态
    if (!ClearCommError(serial->m_hCom, &errors, &comStat)) {
        printf("获取串口状态时发生错误，错误码: %d\n", GetLastError());
        return 0;
    }
    return comStat.cbInQue;
}
bool XSerialPort_setDataTerminalReady(XSerialPort* port, bool set)
{
    XSerialPortBase* base = (XSerialPort*)port;
    if (port->m_hCom == INVALID_HANDLE_VALUE) {
        return false;
    }

    base->m_dataTerminalReady = set;
    bool result = EscapeCommFunction(port->m_hCom, set ? SETDTR : CLRDTR);
    if (result) {
        XSerialPort_dataTerminalReadyChanged_signal((XSerialPort*)base, set);
    }
    return result;
}
bool XSerialPort_isDataTerminalReady(const XSerialPort* port)
{
    const XSerialPortBase* base = (const XSerialPortBase*)port;
    if (base == NULL) return false;
    return base->m_dataTerminalReady;
}
bool XSerialPort_setRequestToSend(XSerialPort* port, bool set)
{
    XSerialPortBase* base = (XSerialPort*)port;
    if (port->m_hCom == INVALID_HANDLE_VALUE) {
        return false;
    }

    base->m_requestToSend = set;
    bool result = EscapeCommFunction(port->m_hCom, set ? SETRTS : CLRRTS);
    if (result) {
        XSerialPort_requestToSendChanged_signal((XSerialPort*)base, set);
    }
    return result;
}
bool XSerialPort_isRequestToSend(const XSerialPort* port)
{
    const XSerialPortBase* base = (const XSerialPortBase*)port;
    if (base == NULL) return false;
    return base->m_requestToSend;
}
bool XSerialPort_setBaudRate(XSerialPort* port, uint32_t baudRate, XSerialPort_Direction directions)
{
    XSerialPortBase* base = (XSerialPortBase*)port;
    if (base == NULL) return;

    if (base->m_baudRate != baudRate) {
        base->m_baudRate = baudRate;
        // 如果串口已打开，需要重新配置
        if (base->m_class.m_mode != XIODeviceBase_NotOpen) {
            // 在Windows平台上，需要重新应用DCB设置
            if (port->m_hCom != INVALID_HANDLE_VALUE) {
                port->m_dcb.BaudRate = baudRate;
                SetCommState(port->m_hCom, &port->m_dcb);
            }
        }
        XSerialPort_baudRateChanged_signal(port, baudRate, directions);
    }
}
uint32_t XSerialPort_baudRate(const XSerialPort* port, XSerialPort_Direction directions)
{
    if (!port)
        return 0;
    return ((XSerialPortBase*)port)->m_baudRate;
}
bool XSerialPort_setDataBits(XSerialPort* port, XSerialPort_DataBits dataBits)
{
    XSerialPortBase* base = (XSerialPortBase*)port;
    if (base == NULL) return;

    if (base->m_dataBits != dataBits) {
        base->m_dataBits = dataBits;
        // 如果串口已打开，需要重新配置
        if (base->m_class.m_mode != XIODeviceBase_NotOpen) {
            if (port->m_hCom != INVALID_HANDLE_VALUE) {
                port->m_dcb.ByteSize = dataBits;
                SetCommState(port->m_hCom, &port->m_dcb);
            }
        }
        XSerialPort_dataBitsChanged_signal(port, dataBits);
    }
}
XSerialPort_DataBits XSerialPort_dataBits(const XSerialPort* port)
{
    const XSerialPortBase* base = (const XSerialPortBase*)port;
    if (base == NULL) return XSerialPort_Data8;
    return base->m_dataBits;
}
bool XSerialPort_setParity(XSerialPort* port, XSerialPort_Parity parity)
{
    XSerialPortBase* base = (XSerialPortBase*)port;
    if (base == NULL) return;

    if (base->m_parity != parity) {
        base->m_parity = parity;
        // 如果串口已打开，需要重新配置
        if (base->m_class.m_mode != XIODeviceBase_NotOpen) {
          
            if (port->m_hCom != INVALID_HANDLE_VALUE) {
                port->m_dcb.Parity = parity;
                SetCommState(port->m_hCom, &port->m_dcb);
            }
        }
        XSerialPort_parityChanged_signal(port, parity);
    }
}
XSerialPort_Parity XSerialPort_parity(const XSerialPort* port)
{
    const XSerialPortBase* base = (const XSerialPortBase*)port;
    if (base == NULL) return XSerialPort_NoParity;
    return base->m_parity;
}
bool XSerialPort_setStopBits(XSerialPort* port, XSerialPort_StopBits stopBits)
{
    XSerialPortBase* base = (XSerialPortBase*)port;
    if (base == NULL) return;

    if (base->m_stopBits != stopBits) {
        base->m_stopBits = stopBits;
        // 如果串口已打开，需要重新配置
        if (base->m_class.m_mode != XIODeviceBase_NotOpen) {
          
            if (port->m_hCom != INVALID_HANDLE_VALUE) {
                port->m_dcb.StopBits = stopBits;
                SetCommState(port->m_hCom, &port->m_dcb);
            }
        }
        XSerialPort_stopBitsChanged_signal(port, stopBits);
    }
}
XSerialPort_StopBits XSerialPort_stopBits(const XSerialPort* port)
{
    const XSerialPortBase* base = (const XSerialPortBase*)port;
    if (base == NULL) return XSerialPort_OneStop;
    return base->m_stopBits;
}
bool XSerialPort_setFlowControl(XSerialPort* port, XSerialPort_FlowControl flowControl)
{
    XSerialPortBase* base = (XSerialPortBase*)port;
    if (base == NULL) return;

    if (base->m_flowControl != flowControl) {
        base->m_flowControl = flowControl;
        // 如果串口已打开，需要重新配置
        if (base->m_class.m_mode != XIODeviceBase_NotOpen) {
           
            if (port->m_hCom != INVALID_HANDLE_VALUE) {
                switch (flowControl) {
                case XSerialPort_NoFlowControl:
                    port->m_dcb.fInX = FALSE;
                    port->m_dcb.fOutX = FALSE;
                    port->m_dcb.fOutxCtsFlow = FALSE;
                    port->m_dcb.fRtsControl = RTS_CONTROL_DISABLE;
                    break;
                case XSerialPort_HardwareControl :
                    port->m_dcb.fOutxCtsFlow = TRUE;
                    port->m_dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
                    break;
                case XSerialPort_SoftwareControl:
                    port->m_dcb.fInX = TRUE;
                    port->m_dcb.fOutX = TRUE;
                    break;
                case XSerialPort_BothControl:
                    port->m_dcb.fInX = TRUE;
                    port->m_dcb.fOutX = TRUE;
                    port->m_dcb.fOutxCtsFlow = TRUE;
                    port->m_dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
                    break;
                }
                SetCommState(port->m_hCom, &port->m_dcb);
            }
        }
        XSerialPort_flowControlChanged_signal(port, flowControl);
    }
}
XSerialPort_FlowControl XSerialPort_flowControl(const XSerialPort* port)
{
    const XSerialPortBase* base = (const XSerialPortBase*)port;
    if (base == NULL) return XSerialPort_NoFlowControl;
    return base->m_flowControl;
}
bool XSerialPort_setBreakEnabled(XSerialPort* port, bool enable)
{
    XSerialPortBase* base = (XSerialPort*)port;
    if (port->m_hCom == INVALID_HANDLE_VALUE) {
        return false;
    }

    base->m_breakEnabled = enable;
    bool result = EscapeCommFunction(port->m_hCom, enable ? SETBREAK : CLRBREAK);
    if (result) {
        XSerialPort_breakEnabledChanged_signal((XSerialPort*)base, enable);
    }
    return result;
}

bool XSerialPort_isBreakEnabled(const XSerialPort* port)
{
    const XSerialPortBase* base = (const XSerialPortBase*)port;
    if (base == NULL) return false;
    return base->m_breakEnabled;
}

XSerialPort_PinoutSignal XSerialPort_pinoutSignals(const XSerialPort* port)
{
    XSerialPortBase* base = (XSerialPort*)port;
    if (port->m_hCom == INVALID_HANDLE_VALUE) {
        return XSerialPort_NoSignal;
    }

    DWORD modemStat;
    if (GetCommModemStatus(port->m_hCom, &modemStat)) {
        XSerialPort_PinoutSignal signals = XSerialPort_NoSignal;
        if (modemStat & MS_CTS_ON) signals |= XSerialPort_ClearToSendSignal;
        if (modemStat & MS_DSR_ON) signals |= XSerialPort_DataSetReadySignal;
        if (modemStat & MS_RING_ON) signals |= XSerialPort_RingIndicatorSignal;
        if (modemStat & MS_RLSD_ON) signals |= XSerialPort_DataCarrierDetectSignal;
        return signals;
    }
    return XSerialPort_NoSignal;
}

#endif