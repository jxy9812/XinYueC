#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XIODevicePrivate.h"
#include "XMemory.h"
#include "XVariant.h"
#include "XVariantList.h"
#include "XByteArray.h"
#include <string.h>
#include <stdarg.h>
#include <assert.h> // for assert
#define XIO_DEFAULT_CHANNEL_ID 0


XIODevice* XIODevice_create()
{
	XIODevice* io = XMalloc_System(sizeof(XIODevice));
	if (io == NULL)
		return io;
	XIODevice_init(io);
	Set_Class_MemoryFree(io, XFree_System);
	return io;
}
void XIODevice_init(XIODevice* io)
{
	if (ISNULL(io, ""))
		return;
	//开始初始化
	memset(((XObject*)io) + 1, 0, sizeof(XIODevice) - sizeof(XObject));
	XObject_init(io);
	XClassGetVtable(io) = XIODevice_class_init();
	// 为 XIODevice 分配并初始化其私有数据
	io->m_d = XIODevicePrivate_create(io);
	io->m_currentReadChannel = XIO_DEFAULT_CHANNEL_ID;
	io->m_currentWriteChannel = XIO_DEFAULT_CHANNEL_ID;
}

XIODeviceBaseMode XIODevice_openMode(const XIODevice* self)
{
	return self ? self->m_openMode : XIODevice_NotOpen;
}
void XIODevice_setTextModeEnabled(XIODevice* self, bool enabled)
{
	if (self) self->m_textModeEnabled = enabled;
}
bool XIODevice_isTextModeEnabled(const XIODevice* self) {
	return self ? self->m_textModeEnabled : false;
}
bool XIODevice_isOpen(const XIODevice* self) {
	return self && (self->m_openMode != XIODevice_NotOpen);
}

bool XIODevice_isReadable(const XIODevice* self) {
	return self && (self->m_openMode & XIODevice_ReadOnly);
}

bool XIODevice_isWritable(const XIODevice* self) {
	return self && (self->m_openMode & XIODevice_WriteOnly);
}

bool XIODevice_isSequential(const XIODevice* self) {
	if (!self) return false;
	return XIODevice_isSequential_base(self);
}

// ========== 通道相关 API ==========
// 为了保持 API 兼容性，这些函数被保留。
// 但在单通道模型下，它们的行为是固定的。

int XIODevice_readChannelCount(const XIODevice* self) {
	if (!self || !self->m_d || !self->m_d->readBuffers) {
		return 0;
	}
	// readBuffers 的大小即为已创建的读通道数量
	return (int)XVector_size_base(self->m_d->readBuffers);
}

int XIODevice_writeChannelCount(const XIODevice* self) {
	if (!self || !self->m_d || !self->m_d->writeBuffers) {
		return 0;
	}
	// writeBuffers 的大小即为已创建的写通道数量
	return (int)XVector_size_base(self->m_d->writeBuffers);
}

int XIODevice_currentReadChannel(const XIODevice* self) {
	if (!self) {
		return -1; // 或 XIO_DEFAULT_CHANNEL_ID，但-1更能表示错误
	}
	return self->m_currentReadChannel;
}
void XIODevice_setCurrentReadChannel(XIODevice* self, int channelIndex) {
	if (!self || channelIndex < 0) {
		return ;
	}

	// 尝试获取或创建该通道的缓冲区以验证其有效性
	struct XRingBuffer* buf = XIODevicePrivate_getOrCreateReadBuffer(self->m_d, channelIndex);
	if (!buf) {
		return ; // 缓冲区创建失败，通道无效
	}

	self->m_currentReadChannel = channelIndex;
	return ;
}

int XIODevice_currentWriteChannel(const XIODevice* self) {
	if (!self) {
		return -1;
	}
	return self->m_currentWriteChannel;
}

void XIODevice_setCurrentWriteChannel(XIODevice* self, int channelIndex) {
	if (!self || channelIndex < 0) {
		return ;
	}

	// 尝试获取或创建该通道的缓冲区以验证其有效性
	struct XRingBuffer* buf = XIODevicePrivate_getOrCreateWriteBuffer(self->m_d, channelIndex);
	if (!buf) {
		return ; // 缓冲区创建失败，通道无效
	}

	self->m_currentWriteChannel = channelIndex;
	return ;
}

// ========== 核心读写 API ==========
// 这些函数现在使用新的单通道 XIODevicePrivate

int64_t XIODevice_read_1(XIODevice* self, char* data, int64_t maxlen)
{
	if (!self || !data || maxlen <= 0) return -1;
	if (!XIODevice_isReadable(self)) return -1;

	XIODevicePrivate* d = self->m_d;
	// 获取当前读通道ID
	int currentReadChannel = XIODevice_currentReadChannel(self);
	struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(d, currentReadChannel);
	if (!readBuf) {
		return -1; // 无法获取或创建缓冲区
	}

	// 1. 首先尝试从当前通道的缓冲区读取
	int64_t bytesFromBuffer = XRingBuffer_read(readBuf, data, maxlen);
	if (bytesFromBuffer == maxlen) {
		return bytesFromBuffer; // 缓冲区数据足够
	}

	// 2. 如果缓冲区数据不足，调用子类实现的底层 readData 函数
	// 注意: 此处并未将通道ID传递给底层设备，这是一个限制。
	int64_t remaining = maxlen - bytesFromBuffer;
	int64_t bytesFromDevice = XIODevice_readData_base(self, data + bytesFromBuffer, remaining);

	if (bytesFromDevice > 0) {
		return bytesFromBuffer + bytesFromDevice;
	}
	else if (bytesFromDevice == 0) {
		// EOF
		return bytesFromBuffer;
	}
	else {
		// Error
		return -1;
	}
}
int64_t XIODevice_read_2(XIODevice* self, XByteArray* buff, int64_t maxlen)
{
	if (!self || !buff|| maxlen <= 0) return 0;
	XByteArray_resize_base(buff, maxlen);
	int64_t n = XIODevice_read_1(self, XContainerDataAddr(buff), maxlen);
	XContainerSize(buff) = n;
	return n;
}

XByteArray* XIODevice_read_3(XIODevice* self, int64_t maxlen)
{
	if (!self || maxlen <= 0) return NULL;
	XByteArray* buff = XByteArray_create();
	XIODevice_read_2(self, buff, maxlen);
	return buff;
}
int64_t XIODevice_readAll_1(XIODevice* self, char* buff, int64_t buffSize)
{
	if (!self || !buff || buffSize <= 0) return 0;
	int64_t len = XIODevice_bytesAvailable_base(self);
	return XIODevice_read_1(self, buff, len>buffSize? buffSize:len);
}
int64_t XIODevice_readAll_2(XIODevice* self, XByteArray* buff)
{
	if (!self || !buff ) return 0;
	return XIODevice_read_2(self, buff, XIODevice_bytesAvailable_base(self));
}

XByteArray* XIODevice_readAll_3(XIODevice* self)
{
	if (!self)return NULL;
	XByteArray* result = XByteArray_create();
	if (result)XIODevice_readAll_2(self,result);
	return result;
}

int64_t XIODevice_readLine_1(XIODevice* self, char* data, int64_t maxlen)
{
	if (!self || !self->m_d || !data || maxlen <= 0) return -1;
	if (!XIODevice_isReadable(self)) return -1;

	int64_t fromBuf = XIODevicePrivate_readLineFromBuffer(self->m_d, data, maxlen);
	if (fromBuf > 0) return fromBuf;

	// 如果缓冲区没有完整行，则回退到逐字节读取
	int64_t i = 0;
	char c;
	while (i < maxlen - 1 && XIODevice_getChar(self, &c)) {
		data[i++] = c;
		if (c == '\n') break;
	}
	if (i > 0) {
		return i;
	}
	return -1;
}

int64_t XIODevice_readLine_2(XIODevice* self, XByteArray* buff)
{
	if (!self || !buff) return 0;
	int64_t maxLen=XIODevice_bytesAvailable_base(self);
	XByteArray_resize_base(buff, maxLen);
	return XIODevice_readLine_1(self, XContainerDataAddr(buff), maxLen);
}

XByteArray* XIODevice_readLine_3(XIODevice* self)
{
	if (!self)return NULL;
	XByteArray* result = XByteArray_create();
	if (result)XIODevice_readLine_2(self, result);
	return result;
}

void XIODevice_startTransaction(XIODevice* self)
{
	if (self && self->m_d) {
		XIODevicePrivate_startTransaction(self->m_d);
	}
}
void XIODevice_commitTransaction(XIODevice* self)
{
	if (self && self->m_d) {
		XIODevicePrivate_commitTransaction(self->m_d);
	}
}
void XIODevice_rollbackTransaction(XIODevice* self) {
	if (self && self->m_d) {
		XIODevicePrivate_rollbackTransaction(self->m_d);
	}
}

bool XIODevice_isTransactionStarted(const XIODevice* self) {
	return self && self->m_d && self->m_d->transactionStarted;
}

int64_t XIODevice_write_1(XIODevice* self, const char* data, int64_t len)
{
	if (!self || !data || len <= 0 || !XIODevice_isWritable(self)) {
		return -1;
	}

	XIODevicePrivate* d = self->m_d;
	// 获取当前写通道ID
	int currentWriteChannel = XIODevice_currentWriteChannel(self);
	struct XRingBuffer* writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentWriteChannel);
	if (!writeBuf) {
		return -1; // 无法获取或创建缓冲区
	}

	// 当前实现直接调用 writeData_base，未使用 writeBuf 进行缓冲。
	// 如果您希望启用写缓冲，可以在此处将数据写入 writeBuf，
	// 并在事件循环或 flush() 中统一发送。
	int64_t written = XIODevice_writeData_base(self, data, len);
	//size_t written = XRingBuffer_write(writeBuf, data, (size_t)len);
	//if (written > 0) {
	//	// 发射通用信号
	//	//XIODevice_bytesWritten_signal(self, written);
	//	// 发射带通道的信号
	//	XIODevice_channelBytesWritten_signal(self, currentWriteChannel, written);
	//}
	return written;
}

int64_t XIODevice_write_3(XIODevice* self, const char* data) {
	if (!data) return 0;
	return XIODevice_write_1(self, data, strlen(data));
}

int64_t XIODevice_write_2(XIODevice* self, const XByteArray* data) {
	if (!data) return 0;
	return XIODevice_write_1(self, XContainerDataAddr(data), XByteArray_size_base(data));
}

bool XIODevice_flush(XIODevice* self)
{
	if (!self || !XIODevice_isWritable(self)) {
		return false;
	}
	// 获取当前写通道ID
	int currentWriteChannel = XIODevice_currentWriteChannel(self);
	XIODevicePrivate* d = self->m_d;
	struct XRingBuffer* writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentWriteChannel);
	if (!writeBuf) {
		return false;
	}

	size_t available = XRingBuffer_available(writeBuf);
	if (available == 0) {
		return true; // 缓冲区为空，无需刷新
	}

	// 分配临时缓冲区来读取待发送的数据
	char* tempBuffer = (char*)XMalloc_System(available);
	if (!tempBuffer) {
		return false; // 内存分配失败
	}

	// 从 writeBuf 读取所有数据
	size_t readFromBuffer = XRingBuffer_read(writeBuf, tempBuffer, available);
	if (readFromBuffer != available) {
		XFree_System(tempBuffer);
		return false; // 读取失败
	}

	// 调用底层 writeData_base 尝试发送所有数据
	int64_t writtenToDevice = XIODevice_writeData_base(self, tempBuffer, (int64_t)readFromBuffer);

	bool success = true;
	if (writtenToDevice < 0) {
		// 完全写入失败，将所有数据重新写回缓冲区
		success = false;
		writtenToDevice = 0; // 视为0字节写入
	}
	else if (writtenToDevice < (int64_t)readFromBuffer) {
		// 部分写入成功，将剩余未写入的数据重新写回缓冲区
		size_t remaining = readFromBuffer - (size_t)writtenToDevice;
		if (XRingBuffer_write(writeBuf, tempBuffer + writtenToDevice, remaining) != remaining) {
			// 如果重新写回缓冲区也失败了，那情况就比较严重了
			success = false;
		}
	}
	// 如果 writtenToDevice == readFromBuffer，则全部成功，无需操作

	XFree_System(tempBuffer);
	return success;
}

int64_t XIODevice_peek_1(XIODevice* self, char* data, int64_t maxlen)
{
	if (!self || !self->m_d || !data || maxlen <= 0 || !XIODevice_isReadable(self)) return 0;
	return XIODevicePrivate_peek(self->m_d, data, maxlen, self);
}

int64_t XIODevice_peek_2(XIODevice* self, XByteArray* buff, int64_t maxlen)
{
	if (!self || !buff) return 0;
	int64_t len = XIODevice_bytesAvailable_base(self);
	len = len > maxlen ? maxlen : len;
	XByteArray_resize_base(buff, len);
	return XIODevice_peek_1(self, XContainerDataAddr(buff), len);
}

XByteArray* XIODevice_peek_3(XIODevice* self, int64_t maxlen)
{
	if (!self)return NULL;
	XByteArray* result = XByteArray_create();
	if (result)XIODevice_peek_2(self, result, maxlen);
	return result;
}

int64_t XIODevice_skip(XIODevice* self, int64_t maxSize)
{
	if (!self || maxSize <= 0) return 0;
	return XIODevice_skipData_base(self, maxSize);
}

void XIODevice_ungetChar(XIODevice* self, char c)
{
	if (self && self->m_d) {
		XIODevicePrivate_putChar(self->m_d, c);
	}
}

bool XIODevice_putChar(XIODevice* self, char c)
{
	return XIODevice_write_1(self, &c, 1) == 1;
}

bool XIODevice_getChar(XIODevice* self, char* c)
{
	if (!self || !c) return false;
	if (self->m_d && XIODevicePrivate_getChar(self->m_d, c)) return true;
	return XIODevice_read_1(self, c, 1) == 1;
}

XString* XIODevice_errorString(const XIODevice* self)
{
	if (self && self->m_d && self->m_d->errorString) {
		return XString_create_copy(self->m_d->errorString);
	}
	return XString_create_fmt_utf8("Unknown error");
}

void XIODevice_setErrorString(XIODevice* self, const char* str)
{
	if (!self || !self->m_d) return;
	if (self->m_d->errorString) {
		XString_delete_base(self->m_d->errorString);
		self->m_d->errorString = NULL;
	}
	if (str) {
		self->m_d->errorString = XString_create_fmt_utf8(str);
	}
}

// —————— 虚函数（_base） ——————
// 这部分保持不变，因为它只是调用虚函数表

bool XIODevice_atEnd_base(XIODevice* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return false;
	return XClassGetVirtualFunc(io, EXIODevice_AtEnd, bool(*)(XIODevice*))(io);
}

bool XIODevice_reset_base(XIODevice* self)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXIODevice_Reset, bool(*)(XIODevice*))(self);
}

int64_t XIODevice_bytesAvailable_base(const XIODevice* self)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return 0;
	return XClassGetVirtualFunc(self, EXIODevice_BytesAvailable, int64_t(*)(const XIODevice*))(self);
}

int64_t XIODevice_bytesToWrite_base(const XIODevice* self)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return 0;
	return XClassGetVirtualFunc(self, EXIODevice_BytesToWrite, int64_t(*)(const XIODevice*))(self);
}

bool XIODevice_canReadLine_base(const XIODevice* self)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXIODevice_CanReadLine, bool(*)(XIODevice*))(self);
}

bool XIODevice_waitForReadyRead_base(XIODevice* self, int msecs)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXIODevice_WaitForReadyRead, bool(*)(XIODevice*, int))(self, msecs);
}

bool XIODevice_waitForBytesWritten_base(XIODevice* self, int msecs)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXIODevice_WaitForBytesWritten, bool(*)(XIODevice*, int))(self, msecs);
}

int64_t XIODevice_readLineData_base(XIODevice* self, char* data, int64_t maxlen)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return 0;
	return XClassGetVirtualFunc(self, EXIODevice_ReadLineData, int64_t(*)(XIODevice*, char*, int64_t))(self, data, maxlen);
}

int64_t XIODevice_skipData_base(XIODevice* self, int64_t maxSize)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return 0;
	return XClassGetVirtualFunc(self, EXIODevice_SkipData, int64_t(*)(XIODevice*, int64_t))(self, maxSize);
}

int64_t XIODevice_readData_base(XIODevice* self, char* data, int64_t maxlen)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return 0;
	return XClassGetVirtualFunc(self, EXIODevice_ReadData, int64_t(*)(XIODevice*, char*, int64_t))(self, data, maxlen);
}

int64_t XIODevice_writeData_base(XIODevice* self, const char* data, int64_t len)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return 0;
	return XClassGetVirtualFunc(self, EXIODevice_WriteData, int64_t(*)(XIODevice*, const char*, int64_t))(self, data, len);
}

bool XIODevice_open_base(XIODevice* io, XIODeviceBaseMode mode)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return false;
	return XClassGetVirtualFunc(io, EXIODevice_Open, bool(*)(XIODevice*, XIODeviceBaseMode))(io, mode);
}

void XIODevice_close_base(XIODevice* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return;
	XClassGetVirtualFunc(io, EXIODevice_Close, void(*)(XIODevice*))(io);
}

bool XIODevice_isSequential_base(const XIODevice* self)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXIODevice_IsSequential, bool(*)(XIODevice*))(self);
}

int64_t XIODevice_pos_base(const XIODevice* self)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return 0;
	return XClassGetVirtualFunc(self, EXIODevice_Pos, int64_t(*)(XIODevice*))(self);
}

int64_t XIODevice_size_base(const XIODevice* self)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return 0;
	return XClassGetVirtualFunc(self, EXIODevice_Size, int64_t(*)(XIODevice*))(self);
}

bool XIODevice_seek_base(XIODevice* self, int64_t pos)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXIODevice_Seek, bool(*)(XIODevice*, int64_t))(self, pos);
}

// —————— 信号 ——————
// 这部分也保持不变

void* XIODevice_aboutToClose_signal(XIODevice* io)
{
	XEmitSignal(io, XIODevice_aboutToClose_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XIODevice_channelBytesWritten_signal(XIODevice* self, int channel, int64_t bytes)
{
	XEmitSignal(self, XIODevice_channelBytesWritten_signal, XVarList_Create(XVar(int, channel), XVar(int64_t, bytes)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XIODevice_channelReadyRead_signal(XIODevice* self, int channel)
{
	XEmitSignal(self, XIODevice_channelReadyRead_signal, XVarList_Create(XVar(int, channel)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XIODevice_readChannelFinished_signal(XIODevice* self)
{
	XEmitSignal(self, XIODevice_readChannelFinished_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}

void* XIODevice_readyRead_signal(XIODevice* self)
{
	XEmitSignal(self, XIODevice_readyRead_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
}
void* XIODevice_bytesWritten_signal(XIODevice* self, int64_t bytes)
{
	XEmitSignal(self, XIODevice_bytesWritten_signal, XVarList_Create(XVar(int64_t, bytes)), NULL, NULL, XEVENT_PRIORITY_NORMAL);
}