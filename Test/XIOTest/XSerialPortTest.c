#include"XIOTest.h"
#include"XSerialPort.h"
#ifdef WIN32
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 串口句柄
static HANDLE hSerial;
static  HANDLE hEvent;
static OVERLAPPED ov;
// 打开串口
static bool SerialOpen(XIODeviceBase* io, XIODeviceBaseMode mode)
{
    //printf("打开串口\n");
    XSerialPort* serial = (XSerialPort*)io;
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
    return true;
}
// 线程接收函数
static DWORD WINAPI ThreadReceive(LPVOID lpParam)
{
    XSerialPort* serial = lpParam;
    COMSTAT comStat;
    DWORD errors;
    char buff[1024];
    while (1)
    {
        // 清除通信错误并获取串口状态
        if (!ClearCommError(hSerial, &errors, &comStat)) {
            printf("获取串口状态时发生错误，错误码: %d\n", GetLastError());
            return false;
        }
        if (comStat.cbInQue)
        {
            DWORD bytesRead= comStat.cbInQue;
            if (bytesRead > 1024)
                bytesRead = 1024;
            
            if (!ReadFile(hSerial, buff, bytesRead, &bytesRead, &ov))
            {
                if (GetLastError() != ERROR_IO_PENDING)
                {
                    printf("读取失败\n");
                    return false;
                }
              
                // 等待异步操作完成
                if (!GetOverlappedResult(hSerial, &ov, &bytesRead, true))
                {
                    printf("异步读取失败\n");
                    return false;
                }
            }
            //将接收到的数据保存到缓冲区
            XSerialPort_receive_base(serial, buff, bytesRead);
        }
        // 重置事件
        ResetEvent(ov.hEvent);
    }
    return 0;
}
static void threadTest(XSerialPort* serial)
{
    HANDLE hThread1;
    DWORD threadId1;
    // 创建线程 1
    hThread1 = CreateThread(NULL, 0, ThreadReceive, serial, 0, &threadId1);
    if (hThread1 == NULL) {
        printf("CreateThread1 failed with error %d\n", GetLastError());
        return 1;
    }

}
void XSerialPortTest()
{
    XIODevice_PortFuncInit port = { 0 };
    port.open_funcPointer = SerialOpen;
    XSerialPort* serial = XSerialPort_new(&port);
    if (!XSerialPort_open_base(serial, XIODeviceBase_ReadWrite, 6, 115200, SP_PAR_NONE))
    {
        XSerialPort_free_base(serial);
        return;
    }
    XSerialPort_setReadBuffer_base(serial,1024);
    //线程接收数据
    threadTest(serial);
    //主线程处理数据
    char buff[1024];
    while (true)
    {
        size_t len = XSerialPort_read_base(serial, buff, 1024);
        if (len >0)
        {
            for (size_t i = 0; i < len; i++)
            {
                printf("%c", buff[i]);
            }
        }
    }
}
#else
void XSerialPortTest()
{

}
#endif