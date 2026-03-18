// include/XSerialPort_p.h
#ifndef XSERIALPORT_P_H
#define XSERIALPORT_P_H

#include "XSerialPort.h"  // 引入公共类型（枚举等）
#include "XIODevice.h"    // 假设 XIODeviceBaseMode 定义在此

#ifdef __cplusplus
extern "C" {
#endif
// 不透明平台数据
typedef struct PlatformData PlatformData;
// ========== 私有数据结构 (PIMPL) ==========
// 所有平台共享此结构
typedef struct XSerialPortPrivate 
{
    // --- 平台无关配置 ---
    char* portName;
    int32_t baudRate;
    XSerialPort_DataBits dataBits;
    XSerialPort_Parity parity;
    XSerialPort_StopBits stopBits;
    XSerialPort_FlowControl flowControl;
    bool dataTerminalReady;
    bool requestToSend;
    bool breakEnabled;
    bool isOpen;
    XSerialPort_Error error;
    int64_t readBufferSize;
    // For waitFor functionality
    XMutex* waitMutex;
    XWaitCondition* waitCondition;
    bool readyReadTriggered;
    bool bytesWrittenTriggered;
    // 唯一平台指针
    PlatformData* platform;
} XSerialPortPrivate;

// ========== 平台函数声明 ==========
// 这些函数由各平台 .c 文件提供（通过 static inline 或普通 static 实现）
bool XSerialPort_platform_open(XSerialPortPrivate* d, XSerialPort* owner, const char* portName, XIODeviceBaseMode mode);
void XSerialPort_platform_close(XSerialPortPrivate* d);
bool XSerialPort_platform_isOpen(const XSerialPortPrivate* d);
int64_t XSerialPort_platform_read(XSerialPortPrivate* d, char* data, int64_t maxSize);
int64_t XSerialPort_platform_write(XSerialPortPrivate* d, const char* data, int64_t len);
int64_t XSerialPort_platform_bytesAvailable(const XSerialPortPrivate* d);
int64_t XSerialPort_platform_bytesToWrite(const XSerialPortPrivate* d); // NEW
XSerialPort_PinoutSignal XSerialPort_platform_pinoutSignals(const XSerialPortPrivate* d); // NEW
// 配置应用函数
bool XSerialPort_platform_applyConfig(XSerialPortPrivate* d);
bool XSerialPort_platform_waitForReadyRead(XSerialPortPrivate* d, int msecs);
bool XSerialPort_platform_waitForBytesWritten(XSerialPortPrivate* d, int msecs);
void XSerialPort_platform_poll(XSerialPortPrivate* d);

bool XSerialPort_platform_setDataTerminalReady(XSerialPortPrivate* d, bool set);
bool XSerialPort_platform_setRequestToSend(XSerialPortPrivate* d, bool set);
bool XSerialPort_platform_setBreakEnabled(XSerialPortPrivate* d, bool set);
bool XSerialPort_platform_flush(XSerialPortPrivate* d);
bool XSerialPort_platform_clear(XSerialPortPrivate* d, XSerialPort_Direction dir);
#ifdef __cplusplus
}
#endif

#endif // XSERIALPORT_P_H