#ifndef XIODevice_H
#define XIODevice_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XObject.h"
//缓冲区大小
#define XBuffSize						256
#define XIODevice_VTABLE_SIZE		(XCLASS_VTABLE_GET_SIZE(XIODevice))       //XIODeviceBase虚函数表大小
//XContainerObject虚函数表枚举
XCLASS_DEFINE_BEGING(XIODevice)
XCLASS_DEFINE_ENUM(XIODevice, Open) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XIODevice, Close),
XCLASS_DEFINE_ENUM(XIODevice, IsSequential),
XCLASS_DEFINE_ENUM(XIODevice, Pos),
XCLASS_DEFINE_ENUM(XIODevice, Size),
XCLASS_DEFINE_ENUM(XIODevice, Seek),
XCLASS_DEFINE_ENUM(XIODevice, AtEnd),
XCLASS_DEFINE_ENUM(XIODevice, Reset),
XCLASS_DEFINE_ENUM(XIODevice, BytesAvailable),
XCLASS_DEFINE_ENUM(XIODevice, BytesToWrite),
XCLASS_DEFINE_ENUM(XIODevice, CanReadLine),
XCLASS_DEFINE_ENUM(XIODevice, WaitForReadyRead),
XCLASS_DEFINE_ENUM(XIODevice, WaitForBytesWritten),
XCLASS_DEFINE_ENUM(XIODevice, ReadData),        // ← 纯虚
XCLASS_DEFINE_ENUM(XIODevice, ReadLineData),
XCLASS_DEFINE_ENUM(XIODevice, SkipData),
XCLASS_DEFINE_ENUM(XIODevice, WriteData),       // ← 纯虚
XCLASS_DEFINE_END(XIODevice)
typedef struct XCircularQueue XCircularQueue;
typedef struct XIODevicePrivate  XIODevicePrivate;
typedef enum /*XIODevice*/
{
	XIODevice_NotOpen      = 0x0000, ///< 设备未打开
    XIODevice_ReadOnly     = 0x0001, ///< 只读
    XIODevice_WriteOnly    = 0x0002, ///< 只写
    XIODevice_ReadWrite    = XIODevice_ReadOnly | XIODevice_WriteOnly, ///< 读写
    XIODevice_Append       = 0x0004, ///< 追加模式
    XIODevice_Truncate     = 0x0008, ///< 打开时截断
    XIODevice_Text         = 0x0010, ///< 文本模式（自动处理换行符）
    XIODevice_Unbuffered   = 0x0020, ///< 绕过缓冲（保留 Qt 内部标志）
    XIODevice_NewOnly      = 0x0040, ///< 仅当不存在时创建
    XIODevice_ExistingOnly = 0x0080  ///< 仅当存在时打开
}XIODeviceBaseMode;
//IO设备
typedef struct XIODevice
{
	XObject m_class;//继承类
	void* device;//设备
	uint16_t m_openMode;//打开模式
    bool m_textModeEnabled;
    int m_currentReadChannel;       // ← 多通道支持
    int m_currentWriteChannel;
    XIODevicePrivate* m_d;
}XIODevice;
XVtable* XIODevice_class_init();
XIODevice* XIODevice_create();
void XIODevice_init(XIODevice* io);
#define XIODevice_delete_base		XObject_delete_base
#define XIODevice_poll_base         XObject_poll_base
// —————— Public API ——————
XIODeviceBaseMode XIODevice_openMode(const XIODevice* self);
void XIODevice_setTextModeEnabled(XIODevice* self, bool enabled);
bool XIODevice_isTextModeEnabled(const XIODevice* self);
bool XIODevice_isOpen(const XIODevice* self);
bool XIODevice_isReadable(const XIODevice* self);
bool XIODevice_isWritable(const XIODevice* self);
bool XIODevice_isSequential(const XIODevice* self);
int XIODevice_readChannelCount(const XIODevice* self);
int XIODevice_writeChannelCount(const XIODevice* self);
int XIODevice_currentReadChannel(const XIODevice* self);
void XIODevice_setCurrentReadChannel(XIODevice* self, int channel);
int XIODevice_currentWriteChannel(const XIODevice* self);
void XIODevice_setCurrentWriteChannel(XIODevice* self, int channel);
int64_t XIODevice_read(XIODevice* self, char* data, int64_t maxlen);
XByteArray* XIODevice_read_new(XIODevice* self, int64_t maxlen);
XByteArray* XIODevice_readAll(XIODevice* self);
int64_t XIODevice_readLine(XIODevice* self, char* data, int64_t maxlen);
XByteArray* XIODevice_readLine_new(XIODevice* self, int64_t maxlen);
void XIODevice_startTransaction(XIODevice* self);
void XIODevice_commitTransaction(XIODevice* self);
void XIODevice_rollbackTransaction(XIODevice* self);
bool XIODevice_isTransactionStarted(const XIODevice* self);
int64_t XIODevice_write(XIODevice* self, const char* data, int64_t len);
int64_t XIODevice_write_cstr(XIODevice* self, const char* data);
int64_t XIODevice_write_byteArray(XIODevice* self, const XByteArray* data);
int64_t XIODevice_peek(XIODevice* self, char* data, int64_t maxlen);
XByteArray* XIODevice_peek_new(XIODevice* self, int64_t maxlen);
int64_t XIODevice_skip(XIODevice* self, int64_t maxSize);
void XIODevice_ungetChar(XIODevice* self, char c);
bool XIODevice_putChar(XIODevice* self, char c);
bool XIODevice_getChar(XIODevice* self, char* c);
XString* XIODevice_errorString(const XIODevice* self);
void XIODevice_setErrorString(XIODevice* self, const char* str);

// —————— 虚函数（_base） ——————
int64_t XIODevice_readData_base(XIODevice* self, char* data, int64_t maxlen);
int64_t XIODevice_writeData_base(XIODevice* self, const char* data, int64_t len);
bool XIODevice_open_base(XIODevice* self, XIODeviceBaseMode mode);
void XIODevice_close_base(XIODevice* self);
bool XIODevice_isSequential_base(const XIODevice* self);
int64_t XIODevice_pos_base(const XIODevice* self);
int64_t XIODevice_size_base(const XIODevice* self);
bool XIODevice_seek_base(XIODevice* self, int64_t pos);
bool XIODevice_atEnd_base(const XIODevice* self);
bool XIODevice_reset_base(XIODevice* self);
int64_t XIODevice_bytesAvailable_base(const XIODevice* self);
int64_t XIODevice_bytesToWrite_base(const XIODevice* self);
bool XIODevice_canReadLine_base(const XIODevice* self);
bool XIODevice_waitForReadyRead_base(XIODevice* self, int msecs);
bool XIODevice_waitForBytesWritten_base(XIODevice* self, int msecs);
int64_t XIODevice_readLineData_base(XIODevice* self, char* data, int64_t maxlen);
int64_t XIODevice_skipData_base(XIODevice* self, int64_t maxSize);

// —————— 信号 ——————
void* XIODevice_readyRead_signal(XIODevice* self);
void* XIODevice_bytesWritten_signal(XIODevice* self, int64_t bytes);
void* XIODevice_aboutToClose_signal(XIODevice* self);
void* XIODevice_channelBytesWritten_signal(XIODevice* self, int channel, int64_t bytes);
void* XIODevice_channelReadyRead_signal(XIODevice* self, int channel);
void* XIODevice_readChannelFinished_signal(XIODevice* self);
#ifdef __cplusplus
}
#endif
#endif