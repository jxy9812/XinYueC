#ifndef XIODEVICEPRIVATE_H
#define XIODEVICEPRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XByteArray.h"
#include "XString.h"

    struct XIODevice;

    typedef struct XIODevicePrivate {
        // 多读通道缓冲（动态扩展）
        struct XByteArray** readBuffers;        // [channel]
        struct XByteArray** ungetBuffers;       // [channel]
        int maxReadChannels;                    // 当前分配的通道数（>=1）

        // 写缓冲（全局，因 Qt 通常单写流）
        struct XByteArray* writeBuffer;

        // 事务（简化：仅作用于当前读通道）
        struct XByteArray* transactionBuffer;
        bool transactionStarted;

        // 错误 & 状态
        struct XString* errorString;
        bool aboutToCloseEmitted;

        struct XIODevice* q_ptr;
    } XIODevicePrivate;

    XIODevicePrivate* XIODevicePrivate_create(struct XIODevice* q);
    void XIODevicePrivate_delete(XIODevicePrivate* d);

    // 带通道参数的操作
    void XIODevicePrivate_putChar(XIODevicePrivate* d, char c, int channel);
    bool XIODevicePrivate_getChar(XIODevicePrivate* d, char* c, int channel);
    int64_t XIODevicePrivate_peek(XIODevicePrivate* d, char* data, int64_t maxlen, struct XIODevice* device, int channel);
    int64_t XIODevicePrivate_readLineFromBuffer(XIODevicePrivate* d, char* data, int64_t maxlen, int channel);
    bool XIODevicePrivate_canReadLineFromBuffer(const XIODevicePrivate* d, int channel);
    void XIODevicePrivate_startTransaction(XIODevicePrivate* d);
    void XIODevicePrivate_commitTransaction(XIODevicePrivate* d);
    void XIODevicePrivate_rollbackTransaction(XIODevicePrivate* d);

#ifdef __cplusplus
}
#endif

#endif // XIODEVICEPRIVATE_H