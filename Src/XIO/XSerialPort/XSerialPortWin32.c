#ifdef WIN32
#include "XSerialPort.h"
#include "XCircularQueue.h"
#include "XPrintf.h"
#include "XMemory.h"
#include "XSerialPortWin32.h"
#include <windows.h>

// 手动定义ERROR_IO_COMPLETION（如果系统头文件未包含）
#ifndef ERROR_IO_COMPLETION
#define ERROR_IO_COMPLETION 996
#endif

// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")

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
    serial->m_hSerial = INVALID_HANDLE_VALUE;
    XClassGetVtable(serial) = XSerialPort_class_init();

    // 初始化重叠结构和事件
    serial->m_ov = XMemory_malloc(sizeof(OVERLAPPED));
    if (serial->m_ov) {
        memset(serial->m_ov, 0, sizeof(OVERLAPPED));
        ((OVERLAPPED*)serial->m_ov)->hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    }

    // 默认缓冲区大小
    XIODeviceBase_setWriteBuffer_base(serial,1024);
    XIODeviceBase_setReadBuffer_base(serial, 1024);

}

void VXSerialPort_deinit(XSerialPort* serial)
{
    if (serial->m_ov) {
        if (((OVERLAPPED*)serial->m_ov)->hEvent != NULL) 
        {
            CloseHandle(((OVERLAPPED*)serial->m_ov)->hEvent);
        }
        XMemory_free(serial->m_ov);
        serial->m_ov = NULL;
    }
    // 释放父对象
    XVtableGetFunc(XIODeviceBase_class_init(), EXClass_Deinit, void(*)(XIODeviceBase*))(serial);
}

bool VXSerialPort_open(XSerialPort* serial, XIODeviceBaseMode mode)
{
    if (serial == NULL)
        return false;

    XSerialPortBase* parent = (XSerialPortBase*)serial;
    if (parent->m_dataBits == SP_DB_Nine || parent->m_stopBits == SP_ST_ZeroPointFive)
        return false; // 当前平台不支持

    char portName[10] = { 0 };
    sprintf(portName, "\\\\.\\COM%d", parent->m_portNum);

    // 打开串口，使用重叠I/O
    HANDLE hSerial = CreateFileA(portName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        XPrintf("无法打开串口%s！错误代码: %lu\n", portName, errorCode);
        return false;
    }

    // 配置串口缓冲区
    if (!SetupComm(hSerial, serial->m_readBufferSize? serial->m_readBufferSize:1024, serial->m_writeBufferSize? serial->m_writeBufferSize:1024)) {
        XPrintf("无法设置串口缓冲区！\n");
        CloseHandle(hSerial);
        return false;
    }

    // 获取并配置串口参数
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hSerial, &dcb)) {
        XPrintf("无法获取串口状态！\n");
        CloseHandle(hSerial);
        return false;
    }

    dcb.BaudRate = parent->m_baudRate;
    dcb.ByteSize = parent->m_dataBits;
    dcb.StopBits = parent->m_stopBits;
    dcb.Parity = parent->m_parity;

    // 配置流控制
    switch (parent->m_flowControl) {
    case SP_FC_None:
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fDtrControl = DTR_CONTROL_DISABLE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        break;
    case SP_FC_Hardware:
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        break;
    case SP_FC_Software:
        dcb.fRtsControl = RTS_CONTROL_DISABLE;
        dcb.fDtrControl = DTR_CONTROL_DISABLE;
        dcb.fOutX = TRUE;
        dcb.fInX = TRUE;
        break;
    case SP_FC_Both:
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
        dcb.fOutX = TRUE;
        dcb.fInX = TRUE;
        break;
    }

    if (!SetCommState(hSerial, &dcb)) {
        XPrintf("无法设置串口状态！\n");
        CloseHandle(hSerial);
        return false;
    }

    // 设置超时（完全异步模式）
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        XPrintf("设置超时失败\n");
    }

    // 配置事件通知
    if (!SetCommMask(hSerial, EV_RXCHAR | EV_TXEMPTY)) {
        XPrintf("设置事件掩码失败\n");
    }

    serial->m_hSerial = hSerial;
    parent->m_class.m_mode = mode;
    //// 关闭 DTR 和 RTS
 //if (!EscapeCommFunction(hSerial, CLRDTR)) {
 //    DWORD errorCode = GetLastError();
 //    printf("无法关闭 DTR！错误代码: %lu\n", errorCode);
 //    CloseHandle(hSerial);
 //    return false;
 //}
 //if (!EscapeCommFunction(hSerial, CLRRTS)) {
 //    DWORD errorCode = GetLastError();
 //    printf("无法关闭 RTS！错误代码: %lu\n", errorCode);
 //    CloseHandle(hSerial);
 //    return false;
 //}
    // 启动异步读取
    if (mode & XIODeviceBase_ReadOnly) {
        char dummy;
        DWORD bytesRead;
        ReadFile(hSerial, &dummy, 1, &bytesRead, (OVERLAPPED*)serial->m_ov);
    }
    XObject_setPollingInterval(serial, 10);
    return true;
}

// 异步写入实现
static size_t XSerialPort_write_async(XSerialPort* serial, const char* data, size_t maxSize)
{
    if (serial == NULL || serial->m_hSerial == INVALID_HANDLE_VALUE ||
        data == NULL || maxSize == 0) {
        return 0;
    }

    OVERLAPPED* ov = (OVERLAPPED*)serial->m_ov;
    // 重置事件
    ResetEvent(ov->hEvent);

    DWORD bytesWritten;
    if (!WriteFile((HANDLE)serial->m_hSerial, data, maxSize, &bytesWritten, ov)) {
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
        OVERLAPPED* ov = (OVERLAPPED*)serial->m_ov;
        DWORD bytesTransferred;
        DWORD result = check_async_result((HANDLE)serial->m_hSerial, ov, &bytesTransferred);

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
    OVERLAPPED* ov = (OVERLAPPED*)serial->m_ov;
    DWORD bytesTransferred;
    DWORD result = check_async_result((HANDLE)serial->m_hSerial, ov, &bytesTransferred);

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
        serial->m_hSerial == INVALID_HANDLE_VALUE) {
        return 0;
    }

    OVERLAPPED* ov = (OVERLAPPED*)serial->m_ov;
    DWORD bytesRead;

    // 检查之前的读取操作是否完成
    DWORD result = check_async_result((HANDLE)serial->m_hSerial, ov, &bytesRead);

    if (result == ERROR_SUCCESS && bytesRead > 0) {
        // 有数据可读
        if (bytesRead > maxSize) bytesRead = maxSize;
        memcpy(data, &data[0], bytesRead); // 实际应用中应从缓冲区读取

        // 立即启动下一次异步读取
        ResetEvent(ov->hEvent);
        ReadFile((HANDLE)serial->m_hSerial, data, maxSize, NULL, ov);
        return bytesRead;
    }
    else if (result == ERROR_IO_PENDING) {
        // 读取操作仍在进行中
        return 0;
    }

    // 启动新的异步读取
    ResetEvent(ov->hEvent);
    if (!ReadFile((HANDLE)serial->m_hSerial, data, maxSize, &bytesRead, ov)) {
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
    if (serial == NULL || serial->m_hSerial == INVALID_HANDLE_VALUE)
        return;

    // 取消所有未完成的异步操作
    CancelIo((HANDLE)serial->m_hSerial);
    CloseHandle((HANDLE)serial->m_hSerial);
    serial->m_hSerial = INVALID_HANDLE_VALUE;

    XSerialPortBase* parent = (XSerialPortBase*)serial;
    parent->m_class.m_mode = 0;
    XObject_setPollingInterval(serial, 0);
}

// 轮询处理异步操作结果
static void VXIODevice_poll(XSerialPort* serial)
{
    if (serial == NULL || serial->m_hSerial == INVALID_HANDLE_VALUE)
        return;

    XIODeviceBase* io = (XIODeviceBase*)serial;
    OVERLAPPED* ov = (OVERLAPPED*)serial->m_ov;
    DWORD bytesTransferred;

    // 检查写入操作结果
    if (io->m_mode & XIODeviceBase_WriteOnly) {
        DWORD result = check_async_result((HANDLE)serial->m_hSerial, ov, &bytesTransferred);
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

        if (ClearCommError((HANDLE)serial->m_hSerial, &errors, &comStat) && comStat.cbInQue > 0) {
            bytesRead = comStat.cbInQue;
            if (bytesRead > sizeof(buffer)) bytesRead = sizeof(buffer);

            if (ReadFile((HANDLE)serial->m_hSerial, buffer, bytesRead, &bytesRead, ov)) {
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
        ResetEvent(ov->hEvent);
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
    serial->m_writeBufferSize = count;
    //XIODeviceBase_setWriteBuffer_base((XIODeviceBase*)serial, count);
    XVtableGetFunc(XIODeviceBase_class_init(), EXIODeviceBase_SetWriteBuffer, void(*)(XIODeviceBase*, size_t))(serial, count);
}

static void VXIODevice_setReadBuffer(XSerialPort* serial, size_t count)
{
    if (serial == NULL) return;
    serial->m_readBufferSize = count;
    //XIODeviceBase_setReadBuffer_base((XIODeviceBase*)serial, count);
    XVtableGetFunc(XIODeviceBase_class_init(), EXIODeviceBase_SetReadBuffer, void(*)(XIODeviceBase*, size_t))(serial, count);
}

static size_t VXIODevice_getBytesAvailable(XSerialPort* serial)
{
    if (serial == NULL || serial->m_hSerial == INVALID_HANDLE_VALUE)
        return 0;

    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (io->m_readBuffer) {
        return XCircularQueue_size_base(io->m_readBuffer);
    }

    COMSTAT comStat;
    DWORD errors;
    // 清除通信错误并获取串口状态
    if (!ClearCommError(serial->m_hSerial, &errors, &comStat)) {
        printf("获取串口状态时发生错误，错误码: %d\n", GetLastError());
        return 0;
    }
    return comStat.cbInQue;
}

#endif