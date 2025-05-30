#ifdef WIN32
//Windows接口文件
#include"XModbusTest_Port.h"
#include"XSerialPort.h"
#include <windows.h>
#include"XCircularQueue.h"
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 串口句柄
static HANDLE hSerial;
//static  HANDLE hEvent;
static OVERLAPPED ov;
// 获取当前毫秒级时间戳（自1970-01-01 00:00:00 UTC）
static long long GetCurrentTimeMillis() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    // 将FILETIME转换为64位整数（100纳秒间隔数）
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;

    // 1601年到1970年的偏移量（100纳秒间隔数）
    const long long EPOCH_OFFSET = 116444736000000000LL;

    // 转换为毫秒（除以10,000）
    return (uli.QuadPart - EPOCH_OFFSET) / 10000;
}
// 打开串口
bool XModbusTest_SerialOpen(XIODeviceBase* io, XIODeviceBaseMode mode)
{
    XSerialPortBase* serial = (XSerialPortBase*)io;
    char portName[10] = { 0 };
    sprintf(portName, "COM%d", serial->m_portNum);
    // 打开串口
    hSerial = CreateFile(portName, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) {
        DWORD errorCode = GetLastError();
        printf("无法打开串口%s！错误代码: %lu\n", portName, errorCode);
        return false;
    }

    // 配置串口缓冲区
    if (!SetupComm(hSerial, 1024, 1024)) {
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
    dcbSerialParams.BaudRate = serial->m_baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = serial->m_parity;
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
    ov.hEvent = CreateEvent(NULL, true, false, NULL);

    if (ov.hEvent == NULL)
        printf("创建事件对象失败");

    // 5. 设置事件通知
    if (!SetCommMask(hSerial, EV_RXCHAR))
        printf("设置事件掩码失败");

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
    //设置XTimer获取毫秒时间搓
    XTimer_setCurrentTimeFunc(GetCurrentTimeMillis);
    return true;
}
bool XModbusTest_writeByte(XIODeviceBase* io, XCircularQueue* queue)
{
    char data;
    while (XCircularQueue_receive_base(queue,&data))
    {
        DWORD bytesWritten;
        if (!WriteFile(hSerial, &data, 1, &bytesWritten, &ov))
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                printf("写入失败");
                return false;
            }
            // 等待异步操作完成
            if (!GetOverlappedResult(hSerial, &ov, &bytesWritten, true))
            {
                printf("异步写入失败");
                return false;
            }
        }
    }
    //printf("写入数据:%02x\n",*data);
    return true;
}
// 定时器回调函数
static void CALLBACK TimerCallbackReceive(UINT uID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2)
{
    XSerialPortBase* serial = dwUser;
    COMSTAT comStat;
    DWORD errors;
    char buff[1024];
    // 清除通信错误并获取串口状态
    if (!ClearCommError(hSerial, &errors, &comStat)) {
        printf("获取串口状态时发生错误，错误码: %d\n", GetLastError());
        return;
    }
    if (comStat.cbInQue)
    {
        DWORD bytesRead = comStat.cbInQue;
        if (bytesRead > 1024)
            bytesRead = 1024;

        if (!ReadFile(hSerial, buff, bytesRead, &bytesRead, &ov))
        {
            if (GetLastError() != ERROR_IO_PENDING)
            {
                printf("读取失败\n");
                return;
            }

            // 等待异步操作完成
            if (!GetOverlappedResult(hSerial, &ov, &bytesRead, true))
            {
                printf("异步读取失败\n");
                return;
            }
        }
        //将接收到的数据保存到缓冲区
       //printf("接收到数据size:%d\n", bytesRead);
        XSerialPortBase_receive_base(serial, buff, bytesRead);
    }
    else
    {
        //printf("准备推送数据\n");
        //XSerialPortBase_writeFull_base(serial);
    }
    // 重置事件
    ResetEvent(ov.hEvent);
    //Sleep(10);
}
// 线程接收函数
static DWORD WINAPI ThreadReceive(LPVOID lpParam)
{
    while (1)
    {
        TimerCallbackReceive(NULL, NULL, lpParam, NULL, NULL);
    }
    return 0;
}
void XModbusTest_threadReceiveCreate(XModbus* modbus)
{
#if 0

    HANDLE hThread1;
    DWORD threadId1;
    // 创建线程 1
    hThread1 = CreateThread(NULL, 0, ThreadReceive, modbus->ioDevice, 0, &threadId1);
    if (hThread1 == NULL) {
        printf("CreateThread1 failed with error %d\n", GetLastError());
        return 1;
    }
#else
    timeSetEvent(
        10,           // 触发间隔毫秒
        1,             // 精度1毫秒
        TimerCallbackReceive, // 回调函数
        modbus->ioDevice,             // 不传递用户数据
        TIME_PERIODIC  // 周期性触发
    );
#endif // 0
}

#endif // WIN32