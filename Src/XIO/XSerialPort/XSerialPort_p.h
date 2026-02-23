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
    typedef struct XSerialPortPrivate {
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
        XSerialPort_Error error;
        int64_t readBufferSize;
        bool isOpen;
        // For waitFor functionality
        XEventLoop* eventLoop;
        XMutex* waitMutex;
        XWaitCondition* waitCondition;
        bool readyReadTriggered;
        bool bytesWrittenTriggered;
        int64_t bytesToWriteValue;

        // 唯一平台指针
        PlatformData* platform;
    } XSerialPortPrivate;

    // ========== 平台函数声明 ==========
    // 这些函数由各平台 .c 文件提供（通过 static inline 或普通 static 实现）
    bool platform_open(XSerialPortPrivate* d, XSerialPort* owner, const char* portName, XIODeviceBaseMode mode);
    void platform_close(XSerialPortPrivate* d);
    bool platform_isOpen(const XSerialPortPrivate* d);
    int64_t platform_read(XSerialPortPrivate* d, char* data, int64_t maxSize);
    int64_t platform_write(XSerialPortPrivate* d, const char* data, int64_t len);
    int64_t platform_bytesAvailable(const XSerialPortPrivate* d);
    int64_t platform_bytesToWrite(const XSerialPortPrivate* d); // NEW
    XSerialPort_PinoutSignal platform_pinoutSignals(const XSerialPortPrivate* d); // NEW
    // 配置应用函数
    bool platform_applyConfig(XSerialPortPrivate* d);
    bool platform_waitForReadyRead(XSerialPortPrivate* d, int msecs);
    bool platform_waitForBytesWritten(XSerialPortPrivate* d, int msecs);
    void platform_poll(XSerialPortPrivate* d);
#ifdef __cplusplus
}
#endif

#endif // XSERIALPORT_P_H