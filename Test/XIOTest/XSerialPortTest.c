#include"XIOTest.h"
#include"XSerialPortBase.h"
#include"XMemory.h"
#ifdef WIN32
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
static char* UTF8ToLocal(const char* utf8Str) {
    if (!utf8Str) return NULL;

    // 1. 计算UTF-8转宽字符所需的缓冲区大小
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, NULL, 0);
    if (wideLen == 0) return NULL;

    // 2. 分配宽字符缓冲区并转换
    wchar_t* wideStr = (wchar_t*)XMemory_malloc(wideLen * sizeof(wchar_t));
    if (!wideStr) return NULL;

    MultiByteToWideChar(CP_UTF8, 0, utf8Str, -1, wideStr, wideLen);

    // 3. 计算宽字符转本地多字节所需的缓冲区大小
    int ansiLen = WideCharToMultiByte(CP_ACP, 0, wideStr, -1, NULL, 0, NULL, NULL);
    if (ansiLen == 0) {
        free(wideStr);
        return NULL;
    }

    // 4. 分配本地多字节缓冲区并转换
    char* ansiStr = (char*)XMemory_malloc(ansiLen * sizeof(char));
    if (!ansiStr) {
        XMemory_free(wideStr);
        return NULL;
    }

    WideCharToMultiByte(CP_ACP, 0, wideStr, -1, ansiStr, ansiLen, NULL, NULL);

    // 5. 释放宽字符缓冲区并返回结果
    XMemory_free(wideStr);
    return ansiStr;
}
void XSerialPortTest()
{
    XSerialPortBase* serial = XSerialPortWin32_new();
    if (!XSerialPortBase_open_base(serial, XIODeviceBase_ReadWrite, 6, 115200, SP_PAR_NONE))
    {
        XSerialPortBase_free_base(serial);
        return;
    }
    //XSerialPortBase_setReadBuffer_base(serial,1024);
    //线程接收数据
    //threadTest(serial);
    //主线程处理数据
    char buff[1024];
    while (true)
    {
        size_t len = XSerialPortBase_read_base(serial, buff, 1024);
        if (len >0)
        {
            buff[len] = 0;
            char* str=UTF8ToLocal(buff);
            if (str)
            {
                printf("%s", str);
                XMemory_free(str);
            }
        }
    }
}
#else
void XSerialPortTest()
{

}
#endif