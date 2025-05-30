#ifdef WIN32
#ifndef XSERIALPORTWIN32_H
#define XSERIALPORTWIN32_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XSerialPortBase.h"
//下面是各平台的实现
#include <windows.h>
// 告诉编译器链接 winmm.lib 库
#pragma comment(lib, "winmm.lib")
//串口设备
typedef struct XSerialPortWin32
{
    XSerialPortBase m_parent;//父对象
    size_t m_readBufferSize;//
    size_t m_writeBufferSize;//
    HANDLE m_hSerial;
    OVERLAPPED m_ov;
}XSerialPortWin32;//串口
XSerialPortWin32* XSerialPortWin32_new();
void XSerialPortWin32_init(XSerialPortWin32* serial);


#ifdef __cplusplus
}
#endif
#endif
#endif // Win32