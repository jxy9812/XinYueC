#ifdef WIN32
#ifndef XSERIALPORTWIN32_H
#define XSERIALPORTWIN32_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XSerialPort.h"
//下面是各平台的实现
//串口设备
typedef struct XSerialPort
{
    XSerialPortBase m_parent;//父对象
    size_t m_readBufferSize;//
    size_t m_writeBufferSize;//
    void* m_hSerial;
    void* m_ov;  //OVERLAPPED m_ov;
}XSerialPort;//串口
XVtable* XSerialPort_class_init();
XSerialPort* XSerialPort_create();
void XSerialPort_init(XSerialPort* serial);

#ifdef __cplusplus
}
#endif
#endif
#endif // Win32