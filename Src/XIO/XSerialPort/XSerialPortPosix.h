#ifdef __linux__ || defined(__APPLE__) || defined(__BSD__)
#ifndef XSERIALPORTPOSIX_H
#define XSERIALPORTPOSIX_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdbool.h>
#include "XSerialPort.h"
#include <termios.h>
// Posix 平台串口设备结构体
typedef struct XSerialPort {
    XSerialPortBase m_class;       // 父对象
    int m_fd;                       // 串口文件描述符
    size_t m_readBufferSize;        // 读缓冲区大小
    size_t m_writeBufferSize;       // 写缓冲区大小
    struct termios m_oldTios;       // 保存原始串口配置（用于恢复）
} XSerialPort;

XVtable* XSerialPort_class_init();
XSerialPort* XSerialPort_create();
void XSerialPort_init(XSerialPort* serial);

#ifdef __cplusplus
}
#endif
#endif // XSERIALPORTPOSIX_H
#endif // Posix 平台