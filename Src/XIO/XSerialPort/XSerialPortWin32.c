#ifdef WIN32
#include "XSerialPort.h"
#include "XCircularQueue.h"
#include "XMemory.h"
#include "XSerialPortWin32.h"
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
static void VXSerialPort_delete(XSerialPort* serial);
static bool VXSerialPort_open(XSerialPort* serial, XIODeviceBaseMode mode);
static size_t VXIODevice_write(XSerialPort* serial, const char* data, size_t maxSize);//写入
static size_t VXIODevice_writeFull(XSerialPort* serial);//将剩余的数据刷入设备
static size_t VXIODevice_read(XSerialPort* serial, char* data, size_t maxSize);//读取
static void VXIODevice_close(XSerialPort* serial);
static void VXIODevice_poll(XSerialPort* serial);
static void VXIODevice_setWriteBuffer(XSerialPort* serial, size_t count);
static void VXIODevice_setReadBuffer(XSerialPort* serial, size_t count);
static size_t VXIODevice_getBytesAvailable(XSerialPort* serial);
XVtable* XSerialPort_class_init()
{
    XVTABLE_CREAT_DEFAULT
        //虚函数表初始化
#if VTABLE_ISSTACK
    XVTABLE_STACK_INIT_DEFAULT(XSERIALPORT_VTABLE_SIZE)
#else
    XVTABLE_HEAP_INIT_DEFAULT
#endif
    //继承类
    XVTABLE_INHERIT_DEFAULT(XIODeviceBase_class_init());
    //重载
    XVTABLE_OVERLOAD_DEFAULT( EXClass_Delete, VXSerialPort_delete);
    XVTABLE_OVERLOAD_DEFAULT( EXIODeviceBase_Open, VXSerialPort_open);
    XVTABLE_OVERLOAD_DEFAULT( EXIODeviceBase_Write, VXIODevice_write);
    XVTABLE_OVERLOAD_DEFAULT( EXIODeviceBase_WriteFull, VXIODevice_writeFull);
    XVTABLE_OVERLOAD_DEFAULT( EXIODeviceBase_Read, VXIODevice_read);
    XVTABLE_OVERLOAD_DEFAULT( EXIODeviceBase_Close, VXIODevice_close);
    XVTABLE_OVERLOAD_DEFAULT( EXObject_Poll, NULL);
    XVTABLE_OVERLOAD_DEFAULT( EXIODeviceBase_SetWriteBuffer, VXIODevice_setWriteBuffer);
    XVTABLE_OVERLOAD_DEFAULT( EXIODeviceBase_SetReadBuffer, VXIODevice_setReadBuffer);
    XVTABLE_OVERLOAD_DEFAULT( EXIODeviceBase_GetBytesAvailable, VXIODevice_getBytesAvailable);
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
    XSerialPortBase_init(serial, NULL);
    serial->m_hSerial = INVALID_HANDLE_VALUE;
    XClassGetVtable(serial) = XSerialPort_class_init();
    serial->m_ov=XMemory_malloc(sizeof(OVERLAPPED));
}

void VXSerialPort_delete(XSerialPort* serial)
{
    if (serial->m_ov)
        XMemory_free(serial->m_ov);
    XMemory_free(serial);
}

bool VXSerialPort_open(XSerialPort* serial, XIODeviceBaseMode mode)
{
    if (serial == NULL)
        return false;
    //printf("打开串口\n");
    XSerialPortBase* parent = serial;
    if (parent->m_dataBits == SP_DB_Nine|| parent->m_stopBits== SP_ST_ZeroPointFive)
        return false;//当前平台不支持
    char portName[10] = { 0 };
    sprintf(portName, "\\\\.\\COM%d", parent->m_portNum);
    // 打开串口
    HANDLE hSerial = CreateFile(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        printf("无法打开串口%s！错误代码: %lu\n", portName, errorCode);
        return false;
    }

    // 配置串口缓冲区
    if (!SetupComm(hSerial, serial->m_readBufferSize, serial->m_writeBufferSize)) 
    {
        printf("无法设置串口缓冲区！\n");
        CloseHandle(hSerial);
        return false;
    }

    // 获取当前串口配置
    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hSerial, &dcb)) {
        printf("无法获取串口状态！\n");
        CloseHandle(hSerial);
        return false;
    }
   

    // 设置串口参数
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
        dcb.fOutX = TRUE;  // 启用输出XON/XOFF
        dcb.fInX = TRUE;   // 启用输入XON/XOFF
        break;

    case SP_FC_Both:
        dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
        dcb.fOutX = TRUE;
        dcb.fInX = TRUE;
        break;
    }
    if (!SetCommState(hSerial, &dcb)) {
        printf("无法设置串口状态！\n");
        CloseHandle(hSerial);
        return false;
    }

    // 3. 设置超时
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;  // 完全异步模式
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;

    if (!SetCommTimeouts(hSerial, &timeouts))
        printf("设置超时失败");

    // 4. 创建异步操作结构
    //OVERLAPPED ov = { 0 };
    ((OVERLAPPED*)(serial->m_ov))->hEvent = CreateEvent(NULL, true, false, NULL);

    if (((OVERLAPPED*)(serial->m_ov))->hEvent == NULL)
        printf("创建事件对象失败");

    // 5. 设置事件通知
    if (!SetCommMask(hSerial, EV_RXCHAR))
        printf("设置事件掩码失败");

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
    serial->m_hSerial = hSerial;
    parent->m_parent.m_mode = mode;
   
    //serial->m_ov.hEvent = serial->m_hEvent;  // 使用已创建的事件句柄
    return true;
}
//串口写入
static size_t XSerialPort_write(XSerialPort* serial, const char* data, size_t maxSize)
{
    if (serial == NULL || serial->m_hSerial == NULL||maxSize==0)
        return 0;
    DWORD bytesWritten;
    if (!WriteFile(serial->m_hSerial, data, maxSize, &bytesWritten, &(serial->m_ov)))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            printf("写入失败");
            return 0;
        }
        // 等待异步操作完成
        if (!GetOverlappedResult(serial->m_hSerial, &(serial->m_ov), &bytesWritten, true))
        {
            printf("异步写入失败");
            return 0;
        }
    }
    return bytesWritten;
}

size_t VXIODevice_write(XSerialPort* serial, const char* data, size_t maxSize)
{
    if (serial == NULL || data == NULL || maxSize == 0)
        return 0;
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (io->m_mode & XIODeviceBase_WriteOnly == 0)
     	return 0;
    return XSerialPort_write(io, data, maxSize);
     //printf("x");
     size_t count = 0;
     if (io->m_writeBuffer == NULL)
     {//没有写入缓冲区
     	count += XSerialPort_write(io, data, maxSize);
     }
     else
     {
     	do
     	{
     		while (XCircularQueue_push_base(io->m_writeBuffer, data + count))
     		{
     			++count;
     			if (count >= maxSize)
     				break;
     		}
     		if (XCircularQueue_isFull_base(io->m_writeBuffer))
                VXIODevice_writeFull(serial);
     		if (count >= maxSize)
     			break;
     	} while (!XCircularQueue_isFull_base(io->m_writeBuffer));
     }
     return count;
}
size_t VXIODevice_writeFull(XSerialPort* serial)
{
    if(serial==NULL)
        return 0;
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (io->m_mode & XIODeviceBase_WriteOnly == 0)
        return 0;
    XCircularQueue* queue= io->m_writeBuffer;
    if(queue==NULL|| XCircularQueue_isEmpty_base(queue))
        return 0;
    size_t count=0;
    char c;
    while (XCircularQueue_receive_base(queue, &c))
    {
        count+= XSerialPort_write(serial,&c,1);
    }
    //printf("写入数据:%02x\n",*data);
    return count;
}
//串口读取
static size_t XSerialPort_read(XSerialPort* serial,char* data, size_t maxSize)
{
    if (serial == NULL || data == NULL || maxSize == 0)
        return 0;
    DWORD bytesRead = XSerialPort_getBytesAvailable_base(serial);
    if (bytesRead == 0)
        return 0;
    if (bytesRead > maxSize)
        bytesRead = maxSize;
   
    if (!ReadFile(serial->m_hSerial, data, bytesRead, NULL, (serial->m_ov)))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            printf("读取失败:%d\n", GetLastError());
            return 0;
        }
        // 重置事件
        //ResetEvent(serial->m_ov.hEvent);
        // 等待异步操作完成
        if (!GetOverlappedResult(serial->m_hSerial, (serial->m_ov), &bytesRead, true))
        {
            printf("异步读取失败\n");
            return 0;
        }
    }
    return bytesRead;
}
size_t VXIODevice_read(XSerialPort* serial, char* data, size_t maxSize)
{
    if (serial == NULL)
        return 0;
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (io->m_mode & XIODeviceBase_ReadOnly == 0)
        return 0;
    size_t size = 0;
    while (size<maxSize)
    {
        size += XSerialPort_read(io, data+size, maxSize-size);
    }
    return size;
}
void VXIODevice_close(XSerialPort* serial)
{
    if (serial == NULL || serial->m_hSerial == INVALID_HANDLE_VALUE)
        return ;
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (XIODeviceBase_isOpen(io))
    { //开始关闭串口
        // 1. 取消所有未完成的异步操作
        if (!CancelIoEx(serial->m_hSerial, &(serial->m_ov)))
        {
            DWORD error = GetLastError();
            printf("取消异步操作失败，错误码: %lu\n", error);
            // 即使取消失败，也应继续尝试关闭其他资源
        }
        // 2. 关闭事件对象
        if (((OVERLAPPED*)(serial->m_ov))->hEvent != NULL)
        {
            CloseHandle(((OVERLAPPED*)(serial->m_ov))->hEvent);
            ((OVERLAPPED*)(serial->m_ov))->hEvent = NULL;
        }
         // 3. 关闭串口句柄
        CloseHandle(serial->m_hSerial);
        serial->m_hSerial = INVALID_HANDLE_VALUE;
       
        io->m_mode = XIODeviceBase_NotOpen;
    }
}
void VXIODevice_poll(XSerialPort* serial)
{
    if (serial == NULL)
        return ;
   // char buff[1024];
   // size_t bytesRead =XSerialPort_read(serial, buff,1024);
    //将接收到的数据保存到缓冲区
    //printf("接收到数据size:%d\n", bytesRead);
   /* if(bytesRead)
        XCircularQueue_push_base(serial->, buff, bytesRead);*/
}
void VXIODevice_setWriteBuffer(XSerialPort* serial, size_t count)
{
    if (XIODeviceBase_isOpen(serial))
    {
        // 配置串口缓冲区
        if (!SetupComm(serial->m_hSerial, serial->m_readBufferSize, count)) {
            printf("无法设置串口缓冲区！\n");
            CloseHandle(serial->m_hSerial);
            return;
        }
    }
    serial->m_writeBufferSize = count;
}
void VXIODevice_setReadBuffer(XSerialPort* serial, size_t count)
{
    if (XIODeviceBase_isOpen(serial))
    {
        // 配置串口缓冲区
        if (!SetupComm(serial->m_hSerial, count, serial->m_writeBufferSize)) {
            printf("无法设置串口缓冲区！\n");
            CloseHandle(serial->m_hSerial);
            return;
        }
    }
    serial->m_readBufferSize = count;
}
size_t VXIODevice_getBytesAvailable(XSerialPort* serial)
{
    COMSTAT comStat;
    DWORD errors;
    // 清除通信错误并获取串口状态
    if (!ClearCommError(serial->m_hSerial, &errors, &comStat)) {
        printf("获取串口状态时发生错误，错误码: %d\n", GetLastError());
        return 0;
    }
    return comStat.cbInQue;
}
#endif // Win32