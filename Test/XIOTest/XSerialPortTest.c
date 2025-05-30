#include"XIOTest.h"
#include"XSerialPortBase.h"
#ifdef WIN32
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
// 线程接收函数
static DWORD WINAPI ThreadReceive(LPVOID lpParam)
{
    XSerialPortBase* serial = lpParam;
    while (1)
    {
        XSerialPortBase_poll_base(serial);
    }
    return 0;
}
static void threadTest(XSerialPortBase* serial)
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
    XSerialPortBase* serial = XSerialPortWin32_new();
    if (!XSerialPortBase_open_base(serial, XIODeviceBase_ReadWrite, 6, 115200, SP_PAR_NONE))
    {
        XSerialPortBase_free_base(serial);
        return;
    }
    XSerialPortBase_setReadBuffer_base(serial,1024);
    //线程接收数据
    threadTest(serial);
    //主线程处理数据
    char buff[1024];
    while (true)
    {
        size_t len = XSerialPortBase_read_base(serial, buff, 1024);
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