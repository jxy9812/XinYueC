#include"XProtocolStackTest.h"
#include"XModbusRtu.h"
#include"XMemory.h"
#ifdef WIN32
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 串口句柄
static HANDLE hSerial;
static  HANDLE hEvent;
static OVERLAPPED ov;
// 打开串口
static BOOL OpenSerialPort(const char* portName, DWORD baudRate) {
   // 打开串口
    hSerial = CreateFile(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        printf("无法打开串口%s！错误代码: %lu\n", portName, errorCode);
        return FALSE;
    }

    // 配置串口缓冲区
    if (!SetupComm(hSerial, 1024, 1024)) {
        printf("无法设置串口缓冲区！\n");
        CloseHandle(hSerial);
        return FALSE;
    }

    // 获取当前串口配置
    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        printf("无法获取串口状态！\n");
        CloseHandle(hSerial);
        return FALSE;
    }

    // 设置串口参数
    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    if (!SetCommState(hSerial, &dcbSerialParams)) {
        printf("无法设置串口状态！\n");
        CloseHandle(hSerial);
        return FALSE;
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
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    if (ov.hEvent == NULL)
        printf("创建事件对象失败");

    // 5. 设置事件通知
    if (!SetCommMask(hSerial, EV_RXCHAR))
        printf("设置事件掩码失败");

   // printf("串口监听中... 按Ctrl+C退出\n");
    return TRUE;
}
static bool XModbus_GetByte(XModbus* modbus, uint8_t* byte)
{
	DWORD bytesRead;
    if (!ReadFile(hSerial, byte, 1, &bytesRead, &ov))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            printf("读取失败\n");
            return false;
        }

        // 等待异步操作完成
        if (!GetOverlappedResult(hSerial, &ov, &bytesRead, TRUE))
        {
            printf("异步读取失败\n");
            return false;
        }
	}
   // printf("接收到字符%c\n", *byte);
	return TRUE;
}
static bool XModbus_PutByte(XModbus* modbus, uint8_t Byte)
{
	DWORD bytesWritten;
	if (!WriteFile(hSerial, &Byte,1, &bytesWritten, &ov))
    {
        if (GetLastError() != ERROR_IO_PENDING)
        {
            printf("写入失败");
            return FALSE;
        }
        // 等待异步操作完成
        if (!GetOverlappedResult(hSerial, &ov, &bytesWritten, TRUE))
        {
            printf("异步写入失败");
            return FALSE;
        }
	}
	return TRUE;
}

// 定时器回调函数
VOID CALLBACK TimerCallback(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2) 
{
    XTimer* timer = ((XTimer*)dwUser);
    if(timer->timeout)
        timer->timeout(dwUser);
}
static void  XTimer_out(XTimer* timer)
{
   // printf("定时器触发\n");
    //XModbusRtuTimerT35Expired(timer->data);
    ((XModbus*)timer->data)->pxMBPortCBTimerExpired(timer->data);
}
static  void XTimer_Start(XTimer* timer)
{
    XTimer_stop(timer);
    // 创建定时器，间隔为 2 毫秒
    UINT timerId = timeSetEvent(2, 1, TimerCallback, timer, TIME_PERIODIC);
    if (timerId == 0) {
        timeEndPeriod(1);
        printf("定时器创建失败\n");
    }
    timer->timerId = timerId;
}
static  void XTimer_Stop(XTimer* timer)
{
    if(timer->timerId)
    {
        timeKillEvent(timer->timerId);
        timer->timerId = 0;
    }
}
static  void XTimerCreat(XTimer* timer)
{
    // 设置定时器分辨率
    //timeBeginPeriod(1);
    //timer->data = XMemory_malloc(sizeof(UINT));
    timer->start = XTimer_Start;
    timer->stop = XTimer_Stop;
    timer->timeout = XTimer_out;
    //timer->interval = 1750;//1750微秒
    //static LARGE_INTEGER frequency;
    //static LARGE_INTEGER start;
    //LARGE_INTEGER current;
    //double elapsedMicroseconds;
    //// 获取性能计数器的频率
    //QueryPerformanceFrequency(&frequency);
    //// 获取开始时间
    //QueryPerformanceCounter(&start);
    //while (1) {
    //    // 获取当前时间
    //    QueryPerformanceCounter(&current);
    //    // 计算经过的时间（微秒）
    //    elapsedMicroseconds = ((double)(current.QuadPart - start.QuadPart) * 1000000) / frequency.QuadPart;

    //    if (elapsedMicroseconds >= timer->interval) {
    //        // 定时器触发
    //        if(timer->timeout)
    //            timer->timeout(timer);
    //        // 更新开始时间
    //        start = current;
    //    }
    //}
}


static XModbusTest_WIn32(XModbus* modbus)
{
    //HANDLE hEvent=NULL;
    // 打开串口
    if (OpenSerialPort("COM6", CBR_115200)) {
        // 关闭 DTR 和 RTS
        if (!EscapeCommFunction(hSerial, CLRDTR)) {
            DWORD errorCode = GetLastError();
            printf("无法关闭 DTR！错误代码: %lu\n", errorCode);
            CloseHandle(hSerial);
            return 1;
        }
        if (!EscapeCommFunction(hSerial, CLRRTS)) {
            DWORD errorCode = GetLastError();
            printf("无法关闭 RTS！错误代码: %lu\n", errorCode);
            CloseHandle(hSerial);
            return 1;
        }
        
    }
   
}
char buffer[1024];
#define BUFFER_SIZE 1024
static void SerialPoll(XModbus* modbus)
{
    DWORD dwEvent;
    DWORD bytesRead;
    char buffer[BUFFER_SIZE];
    COMSTAT comStat;
    DWORD errors;

    // 清除通信错误并获取串口状态
    if (!ClearCommError(hSerial, &errors, &comStat)) {
        printf("获取串口状态时发生错误，错误码: %d\n", GetLastError());
        return FALSE;
    }
    if (comStat.cbInQue && modbus->eSndState == STATE_TX_IDLE)
    {
        modbus->pxMBFrameCBByteReceived(modbus);
    }
    else
    {
        modbus->pxMBFrameCBTransmitterEmpty(modbus);
    }
    // 重置事件
    ResetEvent(ov.hEvent);
}
#endif // WIN32
void XModbusTest()
{
    XModbus* modbus = XMemory_malloc(sizeof(XModbus));
    XModbus_init(modbus, 256, MB_RTU_MASTER, XModbus_GetByte, XModbus_PutByte);
    modbus->SerialEnable = NULL;
    modbus->timer= XTimer_new(XTimerCreat);
    modbus->timer->data = modbus;
    //XTimer_start(modbus->timer);
    //XTimer_stop(modbus->timer);
#ifdef WIN32
	XModbusTest_WIn32(modbus);
#endif // WIN32
    XModbus_enable(modbus);
    while (true)
    {
        XModbus_poll(modbus);
        SerialPoll(modbus);
    }
}