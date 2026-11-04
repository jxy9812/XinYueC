#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XIODevicePrivate.h"
#include "XMemory.h"
#include "XVariant.h"
#include "XVariantList.h"
#include "XByteArray.h"
#include "XString.h"
#include "XRingBuffer.h" /* XRingBuffer_read/write/available（防 C4013 隐式声明）。 */
#include "XStringUtils.h"
#include <string.h>
#include <stdarg.h>
#include <assert.h> // for assert
#define XIO_DEFAULT_CHANNEL_ID 0


XIODevice* XIODevice_create_ex(XMemoryType memory)
{
	XIODevice* io = XMemory_malloc(sizeof(XIODevice), memory);
	if (io == NULL)
		return io;
	XIODevice_init(io);
	Set_Class_Memory(io, memory); Set_Class_IsHeap(io, true);
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
	io->m_fd = XFD_INVALID;
	io->m_d = XIODevicePrivate_create(io);
	io->m_currentReadChannel = XIO_DEFAULT_CHANNEL_ID;
	io->m_currentWriteChannel = XIO_DEFAULT_CHANNEL_ID;
}

// —————— 虚函数（_base） ——————
// 这部分保持不变，因为它只是调用虚函数表

bool XIODevice_atEnd_base(const XIODevice* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return false;
	return XClassGetVirtualFunc(io, EXIODevice_AtEnd, bool(*)(const XIODevice*))(io);
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
	return XClassGetVirtualFunc(self, EXIODevice_CanReadLine, bool(*)(const XIODevice*))(self);
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
	return XClassGetVirtualFunc(self, EXIODevice_IsSequential, bool(*)(const XIODevice*))(self);
}

int64_t XIODevice_pos_base(const XIODevice* self)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return 0;
	return XClassGetVirtualFunc(self, EXIODevice_Pos, int64_t(*)(const XIODevice*))(self);
}

int64_t XIODevice_size_base(const XIODevice* self)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return 0;
	return XClassGetVirtualFunc(self, EXIODevice_Size, int64_t(*)(const XIODevice*))(self);
}

bool XIODevice_seek_base(XIODevice* self, int64_t pos)
{
	if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), ""))
		return false;
	return XClassGetVirtualFunc(self, EXIODevice_Seek, bool(*)(XIODevice*, int64_t))(self, pos);
}

XFd XIODevice_fd(const XIODevice* io)
{
	return io?io->m_fd: XFD_INVALID;
}

void XIODevice_setFd(XIODevice* self, XFd fd)
{
	if (self) self->m_fd = fd;
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
	/* Qt 行为: XIODevice 默认至少有 1 个读通道 (即使尚未创建缓冲区) */
	if (!self) return 0;
	if (!self->m_d || !self->m_d->readBuffers) return 1;
	int n = (int)XVector_size_base(self->m_d->readBuffers);
	return n > 0 ? n : 1;
}

int XIODevice_writeChannelCount(const XIODevice* self) {
	/* Qt 行为: XIODevice 默认至少有 1 个写通道 */
	if (!self) return 0;
	if (!self->m_d || !self->m_d->writeBuffers) return 1;
	int n = (int)XVector_size_base(self->m_d->writeBuffers);
	return n > 0 ? n : 1;
}

int XIODevice_currentReadChannel(const XIODevice* self) {
	if (!self) {
		return -1; // 或 XIO_DEFAULT_CHANNEL_ID，但-1更能表示错误
	}
	return self->m_currentReadChannel;
}
void XIODevice_setCurrentReadChannel(XIODevice* self, int channelIndex) {
	if (!self || !self->m_d || channelIndex < 0) {
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
	if (!self || !self->m_d || channelIndex < 0) {
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

/**
 * @brief 从设备读取至多 maxlen 字节（对标 `QIODevice::read(char*, qint64)`）。
 * @param self   设备对象；NULL 返回 -1。
 * @param data   调用方提供存储空间；NULL 返回 -1。
 * @param maxlen 最大读取字节数；`< 0` 返回 -1，`0` 返回 0。
 * @return 实际读到的字节数；真错误返回 -1；无数据可读返回 0。
 *
 * @details **2026-11-04 全面对齐 Qt 6.8 `QIODevice::read`**：
 *
 *          1. **守卫判据对齐 `CHECK_READABLE`**（qiodevice.cpp:109-118）
 *             只要求 `openMode & ReadOnly`；未打开（NotOpen）与只写设备同样
 *             拒绝并返回 -1。原实现把 `!self->m_d` 与读权限混在一条判断里，
 *             调用方无法区分「对象无效」与「设备未打开」。
 *
 *          2. **`maxlen == 0` 返回 0 而不是 -1**
 *             Qt 只在 `maxSize < 0` 时报错（`CHECK_MAXLEN`，qiodevice.cpp:73），
 *             `0` 是合法的空读。原实现把 `maxlen <= 0` 一并判错。
 *
 *          3. **先消费缓冲区、再向设备补齐**，与 `QIODevicePrivate::read`
 *             同构（qiodevice.cpp:1046+）：缓冲区满额即返回；否则把剩余
 *             额度交给 `readData`。短读合法，不要求一次填满 maxlen。
 *
 *          4. **错误传播对齐 `readFromDevice < 0 && readSoFar == 0 → -1`**
 *             只要本次已从缓冲区取到字节就返回已取到的量；只有一字未得
 *             且底层出错才返回 -1。原实现在底层出错时**无条件**返回 -1，
 *             会丢掉 `bytesFromBuffer` 里已成功取出的数据——实测 DDNS
 *             端到端把「已读到 64 字节响应头」判成读失败。
 *
 *          5. **`readData` 返回 0 视为 EOF**：返回已取到的字节数（可 0）。
 *
 * @note 每次调用按当前读通道取缓冲区（对应 Qt 单缓冲模型），不改变通道。
 */
int64_t XIODevice_read_1(XIODevice* self, char* data, int64_t maxlen)
{
	int64_t bytesFromBuffer;
	int64_t bytesFromDevice;
	int64_t remaining;
	XIODevicePrivate* d;
	int currentReadChannel;
	struct XRingBuffer* readBuf;

	/* 1. 参数守卫（Qt CHECK_MAXLEN：负值才是参数错误，0 合法）。 */
	if (!self || !self->m_d || !data || maxlen < 0)
		return -1;
	if (maxlen == 0)
		return 0;

	/* 2. Qt CHECK_READABLE：仅要求 ReadOnly 位。 */
	if (!XIODevice_isReadable(self))
		return -1;

	d = self->m_d;
	currentReadChannel = XIODevice_currentReadChannel(self);
	readBuf = XIODevicePrivate_getOrCreateReadBuffer(d, currentReadChannel);
	if (!readBuf)
		return -1;   /* 无法获取或创建缓冲区：真错误。 */

	/* 3. 先吃缓冲区；不足时把剩余额度交给 readData（短读合法）。 */
	bytesFromBuffer = (int64_t)XRingBuffer_read(readBuf, data, (size_t)maxlen);
	if (bytesFromBuffer < 0)
		bytesFromBuffer = 0;
	if (bytesFromBuffer >= maxlen)
		return bytesFromBuffer;          /* 缓冲区已满足本次请求。 */

	remaining = maxlen - bytesFromBuffer;
	bytesFromDevice = XIODevice_readData_base(self, data + bytesFromBuffer, remaining);

	if (bytesFromDevice > 0)
		return bytesFromBuffer + bytesFromDevice;

	if (bytesFromDevice == 0)
		return bytesFromBuffer;          /* EOF / 暂无数据：返回已取到的量。 */

	/* 4. 底层出错（含「暂不可读」的负值）：已从缓冲区取到字节就照常返回，
	 *    避免丢弃有效数据（Qt：readFromDevice < 0 && readSoFar == 0 才 -1）。 */
	return (bytesFromBuffer > 0) ? bytesFromBuffer : -1;
}
int64_t XIODevice_read_2(XIODevice* self, XByteArray* buff, int64_t maxlen, bool isAppend)
{
	if (!self || !buff|| maxlen <= 0) return 0;
	size_t count = XByteArray_size_base(buff);
	if(isAppend)
		XByteArray_resize_base(buff, maxlen+ count);
	else
		XByteArray_resize_base(buff, maxlen);
	int64_t n = XIODevice_read_1(self, isAppend? &XByteArray_at_base(buff, count): XContainerDataAddr(buff), maxlen);
	/* 读失败（-1）不能写进容器尺寸：size_t 下会变成 SIZE_MAX，后续遍历越界
	 * （对标 Qt：QIODevice::read 出错返回 -1，缓冲内容不增长）。 */
	XContainerSize(buff) = (n > 0) ? (isAppend ? ((size_t)n + count) : (size_t)n)
	                               : (isAppend ? count : 0);
	return n;
}

XByteArray* XIODevice_read_3(XIODevice* self, int64_t maxlen)
{
	if (!self || maxlen <= 0) return NULL;
	XByteArray* buff = XByteArray_create();
	XIODevice_read_2(self, buff, maxlen,false);
	return buff;
}
int64_t XIODevice_readAll_1(XIODevice* self, char* buff, int64_t buffSize)
{
	if (!self || !buff || buffSize <= 0) return 0;
	int64_t len = XIODevice_bytesAvailable_base(self);
	return XIODevice_read_1(self, buff, len>buffSize? buffSize:len);
}
int64_t XIODevice_readAll_2(XIODevice* self, XByteArray* buff, bool isAppend)
{
	if (!self || !buff ) return 0;
	return XIODevice_read_2(self, buff, XIODevice_bytesAvailable_base(self), isAppend);
}

XByteArray* XIODevice_readAll_3(XIODevice* self)
{
	if (!self)return NULL;
	XByteArray* result = XByteArray_create();
	if (result)XIODevice_readAll_2(self,result,false);
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
	int64_t maxLen = XIODevice_bytesAvailable_base(self);
	if (maxLen <= 0) return 0;
	XByteArray_resize_base(buff, maxLen);
	/* Qt 行为：readLine_2 读出 maxLen 字节或直到换行符；
	   直接 read_1 走底层 readData，绕过 XByteArray 自身的 NUL 终止。 */
	int64_t n = XIODevice_read_1(self, XContainerDataAddr(buff), maxLen);
	XContainerSize(buff) = n > 0 ? n : 0;
	return n;
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
	if (!self || !self->m_d || !data || len <= 0 || !XIODevice_isWritable(self)) {
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
	return XIODevice_write_1(self, data, XStrlen(data));
}

int64_t XIODevice_write_2(XIODevice* self, const XByteArray* data) {
	if (!data) return 0;
	return XIODevice_write_1(self, XContainerDataAddr(data), XByteArray_size_base(data));
}

bool XIODevice_flush(XIODevice* self)
{
	if (!self || !self->m_d || !XIODevice_isWritable(self)) {
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
	int64_t len;
	if (!self || !buff || maxlen <= 0) return 0;
	/* Qt QIODevice::peek() may fill the requested amount from the backing
	 * device even when the internal read buffer currently contains less data.
	 * Looking only at bytesAvailable() here made a preceding short plugin
	 * probe permanently truncate every later probe (notably SVG dimensions). */
	if (!XByteArray_resize_base(buff, maxlen)) return 0;
	len = XIODevice_peek_1(self, XContainerDataAddr(buff), maxlen);
	if (len < 0) len = 0;
	XContainerSize(buff) = (size_t)len;
	return len;
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
		XClassDelete(self->m_d->errorString);
		self->m_d->errorString = NULL;
	}
	if (str) {
		self->m_d->errorString = XString_create_fmt_utf8(str);
	}
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

void XIODevice_poll_base(XIODevice* self) {
	(void)self;
}
