#ifdef WIN32
#include"XSerialPort.h"
#include"XCircularQueue.h"
#include"XMemory.h"

static bool VXSerialPort_open(XSerialPortWin32* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortBaseParity parity);
static size_t VXIODevice_write(XSerialPortWin32* serial, const char* data, size_t maxSize);//写入
static size_t VXIODevice_writeFull(XSerialPortWin32* serial);//将剩余的数据刷入设备
static size_t VXIODevice_read(XSerialPortWin32* serial, char* data, size_t maxSize);//读取
static void VXIODevice_close(XSerialPortWin32* serial);
static void VXIODevice_poll(XSerialPortWin32* serial);
XSerialPortWin32* XSerialPort_new_Win32()
{
	static XVtable* pvVtable = NULL;
	if (pvVtable != NULL)
		return XSerialPortBase_new(pvVtable);
#if VTABLE_ISSTACK
	static XVtable vtable;//虚函数类
	static void* vtable_data[XSERIALPORT_VTABLE_SIZE];//虚函数数据
	XVtable_init_stack(&vtable, vtable_data, XSERIALPORT_VTABLE_SIZE);
	pvVtable = &vtable;
#else
	pvVtable = XVtable_new();
#endif
    XSerialPortWin32* serialPort = XMemory_malloc(sizeof(XSerialPortWin32));
    if (serialPort == NULL)
        return serialPort;
    memset(((XSerialPortBase*)serialPort)+1,0,sizeof(XSerialPortWin32)-sizeof(XSerialPortBase));
    XSerialPortBase_init(serialPort, NULL);
    serialPort->m_hSerial = INVALID_HANDLE_VALUE;
    XClassGetVtable(serialPort) = pvVtable;
	//继承的函数
	XVtable_append_vtable(pvVtable, XIODeviceBaseVtable);
	//重写
	XVtable_At(pvVtable, EXIODeviceBase_Open) = VXSerialPort_open;
    XVtable_At(pvVtable, EXIODeviceBase_Write) = VXIODevice_write;
    XVtable_At(pvVtable, EXIODeviceBase_WriteFull) = VXIODevice_writeFull;
    XVtable_At(pvVtable, EXIODeviceBase_Read) = VXIODevice_read;
    XVtable_At(pvVtable, EXIODeviceBase_Close) = VXIODevice_close;
    XVtable_At(pvVtable, EXIODeviceBase_Poll) = VXIODevice_poll;
	return serialPort;
}

bool VXSerialPort_open(XSerialPortWin32* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortBaseParity parity)
{
    if (serial == NULL)
        return false;
    printf("打开串口\n");
    XSerialPortBase* parent = serial;
    parent->m_baudRate = baudRate;
    parent->m_parity = parity;
    parent->m_portNum = portNum;
    //memset(&(serial->m_ov), 0, sizeof(OVERLAPPED));
    char portName[10] = { 0 };
    sprintf(portName, "COM%d", parent->m_portNum);
    // 打开串口
    HANDLE hSerial = CreateFile(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        printf("无法打开串口%s！错误代码: %lu\n", portName, errorCode);
        return false;
    }

    // 配置串口缓冲区
    if (!SetupComm(hSerial, 128, 128)) {
        printf("无法设置串口缓冲区！\n");
        CloseHandle(hSerial);
        return false;
    }

    // 获取当前串口配置
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        printf("无法获取串口状态！\n");
        CloseHandle(hSerial);
        return false;
    }
   

    // 设置串口参数
    dcbSerialParams.BaudRate = parent->m_baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = parent->m_parity;
    if (!SetCommState(hSerial, &dcbSerialParams)) {
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
    serial->m_ov.hEvent = CreateEvent(NULL, true, false, NULL);

    if (serial->m_ov.hEvent == NULL)
        printf("创建事件对象失败");

    // 5. 设置事件通知
    if (!SetCommMask(hSerial, EV_RXCHAR))
        printf("设置事件掩码失败");

    // 关闭 DTR 和 RTS
    if (!EscapeCommFunction(hSerial, CLRDTR)) {
        DWORD errorCode = GetLastError();
        printf("无法关闭 DTR！错误代码: %lu\n", errorCode);
        CloseHandle(hSerial);
        return false;
    }
    if (!EscapeCommFunction(hSerial, CLRRTS)) {
        DWORD errorCode = GetLastError();
        printf("无法关闭 RTS！错误代码: %lu\n", errorCode);
        CloseHandle(hSerial);
        return false;
    }
    serial->m_hSerial = hSerial;
    parent->m_parent.m_mode = mode;
   
    //serial->m_ov.hEvent = serial->m_hEvent;  // 使用已创建的事件句柄
    return true;
}
//串口写入
static size_t XSerialPort_write(XSerialPortWin32* serial, const char* data, size_t maxSize)
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

size_t VXIODevice_write(XSerialPortWin32* serial, const char* data, size_t maxSize)
{
    if (serial == NULL || data == NULL || maxSize == 0)
        return 0;
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (io->m_mode & XIODeviceBase_WriteOnly == 0)
     	return 0;
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
size_t VXIODevice_writeFull(XSerialPortWin32* serial)
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
static size_t XSerialPort_read(XSerialPortWin32* serial,char* data, size_t maxSize)
{
    if (serial == NULL || data == NULL || maxSize == 0)
        return 0;
    COMSTAT comStat;
    DWORD errors;
    // 清除通信错误并获取串口状态
    if (!ClearCommError(serial->m_hSerial, &errors, &comStat)) {
        printf("获取串口状态时发生错误，错误码: %d\n", GetLastError());
        return 0;
    }
    DWORD bytesRead = comStat.cbInQue;
    if (bytesRead == 0)
        return 0;
    if (bytesRead > maxSize)
        bytesRead = maxSize;
   
    if (!ReadFile(serial->m_hSerial, data, bytesRead, NULL, &(serial->m_ov)))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            printf("读取失败:%d\n", GetLastError());
            return 0;
        }
        // 重置事件
        //ResetEvent(serial->m_ov.hEvent);
        // 等待异步操作完成
        if (!GetOverlappedResult(serial->m_hSerial, &(serial->m_ov), &bytesRead, true))
        {
            printf("异步读取失败\n");
            return 0;
        }
    }
    return bytesRead;
}
size_t VXIODevice_read(XSerialPortWin32* serial, char* data, size_t maxSize)
{
    if (serial == NULL)
        return 0;
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (io->m_mode & XIODeviceBase_ReadOnly == 0)
        return 0;
    size_t count = 0;
    if (io->m_readBuffer == NULL)
    {//没有读取缓冲区
    	count += XSerialPort_read(io, data, maxSize);
    }
    else
    {
    	while (XCircularQueue_receive_base(io->m_readBuffer, data + count))
    	{
    		++count;
    		if (count >= maxSize)
    			break;
    	}
    }
    return count;
}
void VXIODevice_close(XSerialPortWin32* serial)
{
    if (serial == NULL || serial->m_hSerial == INVALID_HANDLE_VALUE)
        return ;
    XIODeviceBase* io = (XIODeviceBase*)serial;
    if (XIODeviceBase_isOpen_base(io))
    { //开始关闭串口
        // 1. 取消所有未完成的异步操作
        if (!CancelIoEx(serial->m_hSerial, &(serial->m_ov)))
        {
            DWORD error = GetLastError();
            printf("取消异步操作失败，错误码: %lu\n", error);
            // 即使取消失败，也应继续尝试关闭其他资源
        }
        // 2. 关闭事件对象
        if (serial->m_ov.hEvent != NULL)
        {
            CloseHandle(serial->m_ov.hEvent);
            serial->m_ov.hEvent = NULL;
        }
         // 3. 关闭串口句柄
        CloseHandle(serial->m_hSerial);
        serial->m_hSerial = INVALID_HANDLE_VALUE;
       
        io->m_mode = XIODeviceBase_NotOpen;
    }
}
void VXIODevice_poll(XSerialPortWin32* serial)
{
    if (serial == NULL)
        return ;
    char buff[1024];
    size_t bytesRead =XSerialPort_read(serial, buff,1024);
    //将接收到的数据保存到缓冲区
    //printf("接收到数据size:%d\n", bytesRead);
    if(bytesRead)
        XSerialPortBase_receive_base(serial, buff, bytesRead);
}
#endif // Win32