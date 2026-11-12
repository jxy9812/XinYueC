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
/* readAll/readLine 增量读的中转块大小（对齐 Qt 的 QRingBuffer chunk 口径量级）。 */
#define XIO_CHUNK_SIZE 4096


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

// —————— 虚函数（_base）——————
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
	return self ? (XIODeviceBaseMode)self->m_openMode : XIODevice_NotOpen;
}

/**
 * @brief 设置打开模式并同步通道数（对齐 Qt 6.8 protected `QIODevice::setOpenMode`）。
 */
void XIODevice_setOpenMode(XIODevice* self, XIODeviceBaseMode mode)
{
	int readCount, writeCount;
	if (!self || !self->m_d) return;
	self->m_openMode = (uint16_t)mode;
	readCount = XIODevice_isReadable(self) ? (self->m_d->readChannelCount > 1 ? self->m_d->readChannelCount : 1) : 0;
	writeCount = XIODevice_isWritable(self) ? (self->m_d->writeChannelCount > 1 ? self->m_d->writeChannelCount : 1) : 0;
	XIODevicePrivate_setReadChannelCount(self->m_d, readCount);
	XIODevicePrivate_setWriteChannelCount(self->m_d, writeCount);
}

void XIODevice_setTextModeEnabled(XIODevice* self, bool enabled)
{
	/* 对齐 Qt 6.8 `QIODevice::setTextModeEnabled`（qiodevice.cpp:529-540）：
	 * **未打开时只告警、不改状态**——Text 是 openMode 里的一个位，关闭态
	 * 改它没有意义，而且会在下一次 open 前伪造出「已启用文本模式」的
	 * 假象（Qt 本机实测：关闭态 setTextModeEnabled(true) 后
	 * isTextModeEnabled() 仍为 false）。本框架无 qWarning 通道，静默忽略。 */
	if (!self || !XIODevice_isOpen(self)) return;
	self->m_textModeEnabled = enabled;
	if (enabled)
		self->m_openMode = (uint16_t)(self->m_openMode | (uint16_t)XIODevice_Text);
	else
		self->m_openMode = (uint16_t)(self->m_openMode & (uint16_t)~XIODevice_Text);
}
bool XIODevice_isTextModeEnabled(const XIODevice* self) {
	return self ? ((self->m_openMode & XIODevice_Text) != 0) : false;
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

int XIODevice_readChannelCount(const XIODevice* self) {
	/* 对齐 Qt：关闭态 0，可读设备打开后 1（由 setOpenMode/open/close 维护）。 */
	if (!self || !self->m_d) return 0;
	return self->m_d->readChannelCount;
}

int XIODevice_writeChannelCount(const XIODevice* self) {
	if (!self || !self->m_d) return 0;
	return self->m_d->writeChannelCount;
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
	/* 对齐 Qt 6.8 qiodevice.cpp:641-655：读事务进行中拒绝切换通道。 */
	if (self->m_d->transactionStarted) {
		return ;
	}
	self->m_currentReadChannel = (uint8_t)channelIndex;
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
	self->m_currentWriteChannel = (uint8_t)channelIndex;
	return ;
}

// ========== 核心读写 API ==========

/**
 * @brief 读核心：`QIODevicePrivate::read`（qiodevice.cpp:1046-1195）的同构实现。
 *
 * @details 与 Qt 逐条对应：
 *          ① `keepDataInBuffer`：顺序设备在「预览或事务中」、随机访问设备在
 *             「预览且带缓冲」时必须保留数据（从缓冲里 peek 而不是 read）；
 *          ② 每次循环先用缓冲、再向设备补 `readData`；`readData` 短读即视为
 *             设备到尾（`deviceAtEof`），下一次循环不再问设备；
 *          ②b 向设备取数分两条路（Qt qiodevice.cpp:1074-1108）：只有
 *             「无缓冲」或「请求不小于一个 chunk」才**直读**调用方缓冲；
 *             带缓冲设备的小读与预览/事务一样，先**回填**进内部环再回放
 *             （P0-08）。
 *          ③ 随机访问设备在向设备取数前要保证设备指针在 `pos`
 *             （`pos != devicePos` 时调 seek）；
 *          ④ Text 模式在读路径上剥掉 '\r'（qiodevice.cpp:1150-1177）；
 *          ⑤ 全部数据都来自缓冲且缓冲已空时，回调一次 `readData(data, 0)`
 *             作为「读后钩子」（静态缓冲型子类靠它做状态维护）；
 *          ⑥ 出错且一字未得才返回 -1——已从缓冲取到数据时必须照常返回。
 *
 * @param self     设备对象（不可读直接 -1）。
 * @param data     输出缓冲。
 * @param maxSize  最多读取字节数（>= 0）。
 * @param peeking  为 true 时数据留在缓冲里（peek 语义）。
 * @return 实际读到字节数；真错误返回 -1。
 */
static int64_t xioRead(XIODevice* self, char* data, int64_t maxSize, bool peeking)
{
	XIODevicePrivate* d;
	struct XRingBuffer* ring;
	bool buffered, sequential, keepDataInBuffer, madeBufferReadsOnly, deviceAtEof;
	int64_t savedPos, readSoFar = 0, bufferPos, textPtr = 0;
	int channel;

	if (!self || !self->m_d || !data) return -1;
	/* 对齐 Qt CHECK_READABLE：先判可读（未打开/只写都算错）。 */
	if (!XIODevice_isReadable(self)) return -1;
	if (maxSize < 0) return -1;            /* CHECK_MAXLEN */
	if (maxSize == 0) {
		/* 空读：缓冲里已有数据就直接返回 0；否则给子类一次 readData(data,0) 钩子。 */
		if (XIODevicePrivate_bufferedBytes(self->m_d, self->m_currentReadChannel) == 0)
			XIODevice_readData_base(self, data, 0);
		return 0;
	}

	d = self->m_d;
	channel = self->m_currentReadChannel;
	sequential = XIODevice_isSequential_base(self);
	buffered = (self->m_openMode & XIODevice_Unbuffered) == 0;

	/* 随机访问设备且子类自己维护位置（重载了 Pos）：先以子类报告值为准校准。
	 * 子类可能在本基类不知情的情况下移动过设备指针（XFileDevice_seek 就是），
	 * 不校准会让「外部 seek + 预读 + 部分读」把设备拉回旧位置。 */
	if (!sequential && XIODevice_isPosOverridden(self)) {
		int64_t virtPos = XIODevice_pos_base(self);
		if (virtPos != d->pos) {
			d->pos = virtPos;
			d->devicePos = virtPos;
			XIODevicePrivate_clearReadBuffers(d);
		}
	}

	ring = XIODevicePrivate_getOrCreateReadBuffer(d, channel);
	if (!ring) return -1;

	keepDataInBuffer = sequential ? (peeking || d->transactionStarted)
	                              : (peeking && buffered);
	savedPos = d->pos;
	madeBufferReadsOnly = true;
	deviceAtEof = false;
	bufferPos = (sequential && d->transactionStarted) ? d->transactionReadPos : 0;

	for (;;) {
		int64_t chunk = keepDataInBuffer
			? XIODevicePrivate_bufferPeek(d, channel, data + readSoFar, maxSize, bufferPos)
			: XIODevicePrivate_bufferRead(d, channel, data + readSoFar, maxSize);
		if (chunk < 0) chunk = 0;
		if (chunk > 0) {
			bufferPos += chunk;
			if (!sequential) d->pos += chunk;
			readSoFar += chunk;
			maxSize -= chunk;
		}

		if (maxSize > 0 && !deviceAtEof) {
			int64_t readFromDevice = 0;
			bool positioned = true;
			/* 保证设备指针在逻辑位置；随机访问设备不同步就 seek。 */
			if (!sequential && d->pos != d->devicePos) {
				if (XIODevice_seek_base(self, d->pos))
					d->devicePos = d->pos;
				else
					positioned = false;
			}
			if (positioned) {
				madeBufferReadsOnly = false;
				/* Qt 6.8 qiodevice.cpp:1074：直读分支条件
				 * `(!buffered || maxSize >= readBufferChunkSize) && !keepDataInBuffer`——
				 * 只有「无缓冲」或「请求不小于一个 chunk」才把数据直写调用方缓冲。 */
				if (!keepDataInBuffer && (!buffered || maxSize >= XIO_CHUNK_SIZE)) {
					readFromDevice = XIODevice_readData_base(self, data + readSoFar, maxSize);
					deviceAtEof = (readFromDevice != maxSize);
					if (readFromDevice > 0) {
						readSoFar += readFromDevice;
						maxSize -= readFromDevice;
						if (!sequential) {
							d->pos += readFromDevice;
							d->devicePos += readFromDevice;
						}
					}
				} else {
					/* 回填分支（P0-08，Qt qiodevice.cpp:1091-1108）：带缓冲设备的
					 * 小读（maxSize < chunkSize）与预览/事务一样，先把数据预读进
					 * 内部环再回放。修复前缺这条分支：① 顺序缓冲设备 read(n) 后
					 * bytesAvailable()/canReadLine() 看不到预读内容；② 基类
					 * readLineData/getChar 逐字节读时每字节一次 readData（一次
					 * 系统调用）。 */
					int64_t toBuffer = (!buffered && maxSize < XIO_CHUNK_SIZE)
						? maxSize : XIO_CHUNK_SIZE;
					char* tmp = (char*)XMalloc_System((size_t)toBuffer);
					if (tmp) {
						readFromDevice = XIODevice_readData_base(self, tmp, toBuffer);
						deviceAtEof = (readFromDevice != toBuffer);
						if (readFromDevice > 0) {
							XRingBuffer_write(ring, tmp, (size_t)readFromDevice);
							if (!sequential)
								d->devicePos += readFromDevice;
							XFree_System(tmp);
							continue;   /* 回到循环顶部，从缓冲里取这段数据 */
						}
						XFree_System(tmp);
					} else {
						readFromDevice = -1;
					}
				}
			} else {
				readFromDevice = -1;
			}
			if (readFromDevice < 0 && readSoFar == 0)
				return -1;   /* 出错且一字未得：真错误。 */
		}

		/* Text 模式：把已读到的 '\r' 去掉（Qt 在读路径上做，写路径的 \n→\r\n
		 * 展开只在 Windows 侧做，见对齐台账）。 */
		if ((self->m_openMode & XIODevice_Text) && textPtr < readSoFar) {
			int64_t r = textPtr, w = textPtr;
			while (r < readSoFar) {
				char ch = data[r++];
				if (ch != '\r') {
					data[w++] = ch;
				} else {
					--readSoFar;
					++maxSize;
				}
			}
			textPtr = readSoFar;
			continue;   /* 剥掉字节后腾出了空间，继续向缓冲/设备要数据。 */
		}
		break;
	}

	if (keepDataInBuffer) {
		if (peeking)
			d->pos = savedPos;              /* 预览不移动逻辑位置 */
		else
			d->transactionReadPos = bufferPos;   /* 事务中记录消费到哪 */
	} else if (peeking) {
		/* 无缓冲随机访问设备的预览：设备指针已前进，把缓冲位置退回原处，
		 * 下一次读会因 pos != devicePos 触发 seek。 */
		XIODevicePrivate_seekBuffer(d, channel, savedPos);
	}

	if (madeBufferReadsOnly && XIODevicePrivate_isBufferEmpty(d, channel))
		XIODevice_readData_base(self, data + readSoFar, 0);

	return readSoFar;
}

int64_t XIODevice_read_1(XIODevice* self, char* data, int64_t maxlen)
{
	return xioRead(self, data, maxlen, false);
}
/**
 * @brief 读进调用方 XByteArray（覆盖写）：`XIODevice_read_3` 的内部实现。
 * @param self 设备对象。
 * @param buff 调用方数组（借用）。
 * @param maxlen 最多读取字节数。
 * @return 实际读到的字节数；参数非法返回 0。
 * @note 原公有重载 `XIODevice_read_2`（带 isAppend）已按 API 收敛删除：Qt 6.8
 *       无「读进调用方数组」语义，设备层能力由 `XIODevice_read_1` 完全覆盖，
 *       数组扩容/尺寸回填属容器职责。
 */
static int64_t xioReadToByteArray(XIODevice* self, XByteArray* buff, int64_t maxlen)
{
	int64_t n;
	if (!self || !buff || maxlen <= 0) return 0;
	XByteArray_resize_base(buff, (size_t)maxlen);
	n = XIODevice_read_1(self, (char*)XContainerDataAddr(buff), maxlen);
	/* 读失败（-1）不能写进容器尺寸：size_t 下会变成 SIZE_MAX，后续遍历越界
	 * （对标 Qt：QIODevice::read 出错返回 -1，缓冲内容不增长）。 */
	XContainerSize(buff) = (n > 0) ? (size_t)n : 0;
	return n;
}

XByteArray* XIODevice_read_3(XIODevice* self, int64_t maxlen)
{
	XByteArray* buff;
	if (!self) return NULL;
	buff = XByteArray_create();
	if (!buff) return NULL;
	if (maxlen <= 0) {
		/* Qt `read(qint64)`：CHECK_READABLE / CHECK_MAXLEN 失败都返回**空
		 * QByteArray 对象**，不返回 NULL（P0-56）；`maxSize == 0` 还会经
		 * `d->read(result.data(), 0)` 给子类一次 `readData(data, 0)` 钩子
		 * （qiodevice.cpp:1187-1213）。空 XByteArray 的 data 指针可能为 NULL
		 * ⇒ 用局部哨兵字节保住该钩子语义。 */
		char empty = 0;
		if (maxlen == 0)
			(void)XIODevice_read_1(self, &empty, 0);
		return buff;
	}
	xioReadToByteArray(self, buff, maxlen);
	return buff;
}

/** @brief 把 [data, data+n) 追加到 XByteArray 尾部（容量不足先扩容）。 */
static bool xioByteArrayAppend(XByteArray* buff, const char* data, int64_t n)
{
	size_t old;
	char* base;
	if (!buff || !data || n <= 0) return true;
	old = XByteArray_size_base(buff);
	if (!XByteArray_resize_base(buff, old + (size_t)n)) return false;
	/* 注意：XContainerDataAddr 是**没有外层括号的三元表达式**，写成
	 * `(char*)XContainerDataAddr(x) + off` 会让 `+ off` 落到 NULL 分支上、偏移被
	 * 静默丢弃（2026-11-10 实测）。必须先落成局部指针再加偏移。 */
	base = (char*)XContainerDataAddr(buff);
	memcpy(base + old, data, (size_t)n);
	return true;
}

/**
 * @brief readAll 核心：等价 Qt `QIODevice::readAll`（qiodevice.cpp:1226-1275）。
 * @details 随机访问设备一次按 `size()-pos` 取；顺序设备（size() 不可知）用
 *          「按块增量读，直到 readData 返回 <= 0」的循环——这正是原实现缺的分支：
 *          套接字 bytesAvailable() 恒为 ring buffer 计数，只看它会导致 readAll 恒空。
 * @param self 设备。
 * @param out 追加目标（借用）。
 * @return 读到的总字节数；设备不可读返回 -1。
 */
static int64_t xioReadAllToByteArray(XIODevice* self, XByteArray* out)
{
	char chunk[XIO_CHUNK_SIZE];
	int64_t total = 0;
	if (!self || !out) return 0;
	if (!XIODevice_isReadable(self)) return -1;

	if (!XIODevice_isSequential_base(self)) {
		int64_t want = XIODevice_size_base(self) - XIODevice_pos_base(self);
		if (want > 0) {
			size_t old = XByteArray_size_base(out);
			char* base;
			if (!XByteArray_resize_base(out, old + (size_t)want))
				return 0;
			base = (char*)XContainerDataAddr(out);   /* 先落指针再加偏移，见 xioByteArrayAppend 注释 */
			total = XIODevice_read_1(self, base + old, want);
			if (total < 0) {
				XContainerSize(out) = old;
				return -1;
			}
			XContainerSize(out) = old + (size_t)total;
			return total;
		}
		/* size() 不可用（返回 0/负）：退回增量读。 */
	}

	for (;;) {
		int64_t n = XIODevice_read_1(self, chunk, (int64_t)sizeof(chunk));
		if (n <= 0) break;
		if (!xioByteArrayAppend(out, chunk, n)) break;
		total += n;
	}
	return total;
}

int64_t XIODevice_readAll_1(XIODevice* self, char* buff, int64_t buffSize)
{
	int64_t total = 0;
	if (!self || !buff || buffSize <= 0) return 0;
	if (!XIODevice_isReadable(self)) return -1;

	for (;;) {
		int64_t room = buffSize - total;
		int64_t n;
		if (room <= 0) break;
		if (room > XIO_CHUNK_SIZE) room = XIO_CHUNK_SIZE;
		n = XIODevice_read_1(self, buff + total, room);
		if (n < 0) {
			if (total == 0) return -1;
			break;
		}
		if (n == 0) break;
		total += n;
	}
	return total;
}
int64_t XIODevice_readAll_2(XIODevice* self, XByteArray* buff, bool isAppend)
{
	int64_t n;
	if (!self || !buff ) return 0;
	if (!isAppend) XByteArray_clear_base(buff);
	n = xioReadAllToByteArray(self, buff);
	return n;
}

XByteArray* XIODevice_readAll_3(XIODevice* self)
{
	if (!self)return NULL;
	XByteArray* result = XByteArray_create();
	if (result)XIODevice_readAll_2(self,result,false);
	return result;
}

/**
 * @brief readLine 核心：`QIODevicePrivate::readLine`（qiodevice.cpp:1333-1417）的同构实现。
 * @param maxSize 含结尾 '\0' 的缓冲容量，调用方保证 >= 2。
 * @return 实际读到的字节数（不含 '\0'）；无数据返回 0，错误返回 -1。
 * @note **总会写结尾 '\0'**（Qt 契约），且最多写 maxSize-1 个数据字节。
 */
static int64_t xioReadLine(XIODevice* self, char* data, int64_t maxSize)
{
	XIODevicePrivate* d;
	bool sequential, keepDataInBuffer, textMode;
	int64_t readSoFar = 0, readBytes;
	int channel;

	if (!self || !self->m_d || !data || maxSize < 2) return -1;
	if (!XIODevice_isReadable(self)) return -1;

	d = self->m_d;
	channel = self->m_currentReadChannel;
	sequential = XIODevice_isSequential_base(self);
	textMode = (self->m_openMode & XIODevice_Text) != 0;
	keepDataInBuffer = sequential && d->transactionStarted;

	/* 与读路径同口径：子类自己维护位置（重载 Pos）时先按它校准。 */
	if (!sequential && XIODevice_isPosOverridden(self)) {
		int64_t virtPos = XIODevice_pos_base(self);
		if (virtPos != d->pos) {
			d->pos = virtPos;
			d->devicePos = virtPos;
			XIODevicePrivate_clearReadBuffers(d);
		}
	}

	--maxSize;   /* 留一个字节给 '\0' */

	if (keepDataInBuffer) {
		int64_t avail = XIODevicePrivate_bufferedBytes(d, channel);
		if (d->transactionReadPos < avail) {
			int64_t idx = XIODevicePrivate_bufferIndexOf(d, channel, '\n', maxSize, d->transactionReadPos);
			readSoFar = XIODevicePrivate_bufferPeek(d, channel, data,
				(idx >= 0) ? (idx - d->transactionReadPos + 1) : maxSize,
				d->transactionReadPos);
			d->transactionReadPos += readSoFar;
			if (d->transactionReadPos == XIODevicePrivate_bufferedBytes(d, channel))
				XIODevice_readData_base(self, data, 0);
		}
	} else if (XIODevicePrivate_bufferedBytes(d, channel) > 0) {
		readSoFar = XIODevicePrivate_readLineFromBuffer(d, data, maxSize);
		if (XIODevicePrivate_isBufferEmpty(d, channel))
			XIODevice_readData_base(self, data, 0);
		if (!sequential)
			d->pos += readSoFar;
	}

	if (readSoFar > 0) {
		if (data[readSoFar - 1] == '\n') {
			if (textMode && readSoFar > 1 && data[readSoFar - 2] == '\r') {
				--readSoFar;
				data[readSoFar - 1] = '\n';
			}
			data[readSoFar] = '\0';
			return readSoFar;
		}
	}

	/* 缓冲里没有完整行：交给 readLineData 继续。事务中强制走**基类实现**：
	 * Qt 原文 qiodevice.cpp:1391-1395 —— "Force base implementation for
	 * transaction on sequential device as it stores the data in internal buffer
	 * automatically"。XSerialPort / XFileDevice 的 readLineData 重载绕过 read()
	 * 直读 fd，事务里读到的行不进内部缓冲 ⇒ rollback 只复位
	 * transactionReadPos、无法重放（P0-09）。 */
	if (!sequential && XIODevice_pos_base(self) != d->devicePos && !XIODevice_seek_base(self, d->pos))
		return -1;
	if (d) d->baseReadLineDataCalled = false;
	readBytes = keepDataInBuffer
		? XIODevice_readLineData(self, data + readSoFar, maxSize - readSoFar)
		: XIODevice_readLineData_base(self, data + readSoFar, maxSize - readSoFar);

	if (readBytes < 0) {
		data[readSoFar] = '\0';
		return readSoFar ? readSoFar : -1;
	}
	readSoFar += readBytes;
	if (!d->baseReadLineDataCalled && !sequential) {
		/* 子类的 readLineData 没有走基类读路径，位置需要我们自己推进，
		 * 并把 devicePos 标为未知（对齐 Qt）。 */
		d->pos += readBytes;
		d->devicePos = -1;
	}
	data[readSoFar] = '\0';

	if (textMode) {
		if (readSoFar > 1 && data[readSoFar - 1] == '\n' && data[readSoFar - 2] == '\r') {
			data[readSoFar - 2] = '\n';
			data[readSoFar - 1] = '\0';
			--readSoFar;
		}
	}
	return readSoFar;
}

int64_t XIODevice_readLine_1(XIODevice* self, char* data, int64_t maxlen)
{
	/* 对齐 Qt：先 CHECK_READABLE，再 CHECK_LINEMAXLEN（maxSize < 2 直接 -1）。 */
	if (!self || !data) return -1;
	if (!XIODevice_isReadable(self)) return -1;
	if (maxlen < 2) return -1;
	return xioReadLine(self, data, maxlen);
}

/**
 * @brief 读一行到调用方 XByteArray（先清空）：`XIODevice_readLine_3/_4` 的内部实现。
 * @param self 设备对象。
 * @param buff 调用方数组（借用）。
 * @return 读到的字节数（含换行符）；无数据返回 0，设备不可读返回 -1。
 * @note 原公有重载 `XIODevice_readLine_2` 已按 API 收敛删除：它只是 Qt
 *       `readLine(0)` 的「写进调用方数组」包装，Qt 6.8 无该签名；同一语义由
 *       `XIODevice_readLine_3`（= readLine(0)）与 `XIODevice_readLine_4`
 *       （= readLine(maxSize)）覆盖。
 */
static int64_t xioReadLineToByteArray(XIODevice* self, XByteArray* buff)
{
	char chunk[XIO_CHUNK_SIZE];
	int64_t total = 0;
	if (!self || !buff) return 0;
	XByteArray_clear_base(buff);
	if (!XIODevice_isReadable(self)) return -1;
	/* 对齐 Qt 的 readLine(0) 增量分支：按块读，直到读到换行或读不动。 */
	for (;;) {
		int64_t n = xioReadLine(self, chunk, (int64_t)sizeof(chunk));
		if (n <= 0) {
			if (total == 0 && n < 0) return -1;
			break;
		}
		if (!xioByteArrayAppend(buff, chunk, n)) break;
		total += n;
		if (chunk[n - 1] == '\n') break;
		/* 没有写满且没有换行 ⇒ 已到设备末尾，不再试。 */
		if (n < (int64_t)sizeof(chunk) - 1) break;
	}
	return total;
}

XByteArray* XIODevice_readLine_3(XIODevice* self)
{
	if (!self)return NULL;
	XByteArray* result = XByteArray_create();
	if (result)xioReadLineToByteArray(self, result);
	return result;
}

XByteArray* XIODevice_readLine_4(XIODevice* self, int64_t maxlen)
{
	XByteArray* result;
	int64_t n;
	if (!self) return NULL;
	result = XByteArray_create();
	if (!result) return NULL;
	if (maxlen == 0) {
		xioReadLineToByteArray(self, result);
		return result;
	}
	if (maxlen < 2) return result;   /* Qt：maxSize < 2 返回空 */
	{
		char* tmp = (char*)XMalloc_System((size_t)maxlen);
		if (!tmp) return result;
		n = XIODevice_readLine_1(self, tmp, maxlen);
		if (n > 0)
			xioByteArrayAppend(result, tmp, n);
		XFree_System(tmp);
	}
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

/**
 * @brief 写核心：对齐 Qt 6.8 `QIODevice::write`（qiodevice.cpp:1640-1710）。
 * @details ① **先 CHECK_WRITABLE 再 CHECK_MAXLEN**（所以 write(x,0) == 0）；
 *          ② 随机访问设备写前保证设备指针在 pos；
 *          ③ 写成功后推进 pos/devicePos，并让读缓冲失效（Qt: `d->buffer.skip(written)`）；
 *          ④ 基类**不发** bytesWritten——该信号由真正完成设备写入的子类发射。
 */
int64_t XIODevice_write_1(XIODevice* self, const char* data, int64_t len)
{
	XIODevicePrivate* d;
	struct XRingBuffer* writeBuf;
	int currentWriteChannel;
	bool sequential;
	int64_t written;

	if (!self || !self->m_d || !data) return -1;
	if (!XIODevice_isWritable(self)) return -1;     /* CHECK_WRITABLE */
	if (len < 0) return -1;                          /* CHECK_MAXLEN */
	if (len == 0) return 0;                          /* 合法空写 */

	d = self->m_d;
	sequential = XIODevice_isSequential_base(self);
	currentWriteChannel = XIODevice_currentWriteChannel(self);
	writeBuf = XIODevicePrivate_getOrCreateWriteBuffer(d, currentWriteChannel);
	if (!writeBuf) return -1;

	if (!sequential) {
		if (XIODevice_isPosOverridden(self)) {
			int64_t virtPos = XIODevice_pos_base(self);
			if (virtPos != d->pos) {
				d->pos = virtPos;
				d->devicePos = virtPos;
			}
		}
		if (d->pos != d->devicePos) {
			if (!XIODevice_seek_base(self, d->pos)) return -1;
			d->devicePos = d->pos;
		}
	}

#ifdef _WIN32
	/* Text 模式写侧的 '\n' → "\r\n" 展开：**只在 Windows 做**，与 Qt 6.8
	 * 完全一致（qiodevice.cpp:1652-1699 的整段 `#ifdef Q_OS_WIN`）——
	 * POSIX 文本模式不做任何转换（只读侧剥 '\r'）。返回值是**源字节数**
	 * （每个 '\n' 记 1，不按展开后的 2 计），与 Qt 相同。 */
	if (self->m_openMode & XIODevice_Text) {
		const char* endOfData = data + len;
		const char* startOfBlock = data;
		int64_t writtenSoFar = 0;
		int64_t savedPos = d->pos;
		for (;;) {
			const char* endOfBlock = startOfBlock;
			int64_t blockSize, ret;
			while (endOfBlock < endOfData && *endOfBlock != '\n')
				++endOfBlock;
			blockSize = (int64_t)(endOfBlock - startOfBlock);
			if (blockSize > 0) {
				ret = XIODevice_writeData_base(self, startOfBlock, blockSize);
				if (ret <= 0) {
					if (writtenSoFar && !sequential)
						XIODevicePrivate_bufferSkip(d, self->m_currentReadChannel, d->pos - savedPos);
					return writtenSoFar ? writtenSoFar : ret;
				}
				if (!sequential) {
					d->pos += ret;
					d->devicePos += ret;
				}
				writtenSoFar += ret;
			}
			if (endOfBlock == endOfData)
				break;
			ret = XIODevice_writeData_base(self, "\r\n", 2);
			if (ret <= 0) {
				if (writtenSoFar && !sequential)
					XIODevicePrivate_bufferSkip(d, self->m_currentReadChannel, d->pos - savedPos);
				return writtenSoFar ? writtenSoFar : ret;
			}
			if (!sequential) {
				d->pos += ret;
				d->devicePos += ret;
			}
			++writtenSoFar;                 /* 源数据里的那个 '\n' 记 1 字节 */
			startOfBlock = endOfBlock + 1;
		}
		if (writtenSoFar && !sequential)
			XIODevicePrivate_bufferSkip(d, self->m_currentReadChannel, d->pos - savedPos);
		return writtenSoFar;
	}
#endif

	/* 写缓冲模型：本框架的 write 直接下发（不排队），因此写缓冲始终为空、
	 * bytesToWrite() 恒 0；需要用写缓冲的子类自行接管（见对齐台账）。 */
	written = XIODevice_writeData_base(self, data, len);
	if (!sequential && written > 0) {
		d->pos += written;
		d->devicePos += written;
		/* 覆写会让预读内容失效，丢弃前缀（对齐 Qt d->buffer.skip(written)）。 */
		XIODevicePrivate_bufferSkip(d, self->m_currentReadChannel, written);
	}
	return written;
}

int64_t XIODevice_write_3(XIODevice* self, const char* data) {
	if (!data) return 0;
	return XIODevice_write_1(self, data, XStrlen(data));
}

int64_t XIODevice_write_2(XIODevice* self, const XByteArray* data) {
	if (!data) return 0;
	if (XByteArray_size_base(data) == 0) {
		/* 空数组：XByteArray 空对象的 m_data 为 NULL，直接把 `XContainerDataAddr`
		 * 传给 write_1 会命中它的 `!data` 早退返回 -1（P0-55）。Qt 的
		 * `write(const QByteArray &)` = `write(data.constData(), data.size())`
		 * ——空 QByteArray 的 constData() 是合法指针，走 CHECK_WRITABLE 后按
		 * `maxSize==0` 返回 0（未打开仍 -1）。用局部哨兵指针保留这套语义。 */
		char empty = 0;
		return XIODevice_write_1(self, &empty, 0);
	}
	return XIODevice_write_1(self, (const char*)XContainerDataAddr(data),
		(int64_t)XByteArray_size_base(data));
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
	/* 对齐 Qt：CHECK_MAXLEN 在 CHECK_READABLE 之前，两者都返回 -1。 */
	if (!self || !data) return -1;
	if (maxlen < 0) return -1;
	return xioRead(self, data, maxlen, true);
}

/**
 * @brief 窥探进调用方 XByteArray（覆盖写）：`XIODevice_peek_3` 的内部实现。
 * @param self 设备对象。
 * @param buff 调用方数组（借用）。
 * @param maxlen 最多窥探字节数。
 * @return 实际窥探到的字节数；失败返回 0。
 * @note 原公有重载 `XIODevice_peek_2` 已按 API 收敛删除：Qt 6.8 只有
 *       `peek(char*, qint64)` 与 `peek(qint64)`，设备层能力由
 *       `XIODevice_peek_1` 与 `XIODevice_peek_3` 完全覆盖。
 */
static int64_t xioPeekToByteArray(XIODevice* self, XByteArray* buff, int64_t maxlen)
{
	int64_t len;
	if (!self || !buff || maxlen < 0) return 0;
	if (!XByteArray_resize_base(buff, (size_t)maxlen)) return 0;
	len = XIODevice_peek_1(self, (char*)XContainerDataAddr(buff), maxlen);
	if (len < 0) len = 0;
	XContainerSize(buff) = (size_t)len;
	return len;
}

XByteArray* XIODevice_peek_3(XIODevice* self, int64_t maxlen)
{
	if (!self)return NULL;
	XByteArray* result = XByteArray_create();
	if (result)xioPeekToByteArray(self, result, maxlen);
	return result;
}

/**
 * @brief 按「读」跳过 maxSize 字节（对齐 Qt `QIODevicePrivate::skipByReading`）。
 * @details 文本模式与顺序设备事务中走这条路径，保证剥 '\r' 后的字节计数与 read 一致。
 */
static int64_t xioSkipByReading(XIODevice* self, int64_t maxSize)
{
	char dummy[XIO_CHUNK_SIZE];
	int64_t readSoFar = 0;
	if (maxSize <= 0) return 0;
	do {
		int64_t readBytes = (maxSize > (int64_t)sizeof(dummy)) ? (int64_t)sizeof(dummy) : maxSize;
		int64_t readResult = XIODevice_read_1(self, dummy, readBytes);
		if (readResult != readBytes) {
			if (readSoFar == 0)
				return readResult;
			if (readResult == -1)
				return readSoFar;
			return readSoFar + readResult;
		}
		readSoFar += readResult;
		maxSize -= readResult;
	} while (maxSize > 0);
	return readSoFar;
}

/**
 * @brief 跳过数据：对齐 Qt 6.8 `QIODevice::skip`（qiodevice.cpp:1939-2003）。
 */
int64_t XIODevice_skip(XIODevice* self, int64_t maxSize)
{
	XIODevicePrivate* d;
	int channel;
	bool sequential;
	int64_t skippedSoFar = 0;
	int64_t skipResult;

	if (!self || !self->m_d) return -1;
	if (maxSize < 0) return -1;              /* CHECK_MAXLEN */
	if (!XIODevice_isReadable(self)) return -1;  /* CHECK_READABLE */

	d = self->m_d;
	channel = self->m_currentReadChannel;
	sequential = XIODevice_isSequential_base(self);

	if ((sequential && d->transactionStarted) || (self->m_openMode & XIODevice_Text))
		return xioSkipByReading(self, maxSize);

	/* ① 先吃掉内部读缓冲（peek/readLine 预读过的前缀不得重复读出）。 */
	if (!XIODevicePrivate_isBufferEmpty(d, channel)) {
		skippedSoFar = XIODevicePrivate_bufferSkip(d, channel, maxSize);
		if (!sequential)
			d->pos += skippedSoFar;
		if (XIODevicePrivate_isBufferEmpty(d, channel))
			XIODevice_readData_base(self, NULL, 0);
		if (skippedSoFar == maxSize)
			return skippedSoFar;
		maxSize -= skippedSoFar;
	}

	/* ② 随机访问设备走 seek 快路径。 */
	if (!sequential) {
		int64_t curPos = XIODevice_pos_base(self);
		int64_t bytesToSkip = XIODevice_size_base(self) - curPos;
		if (bytesToSkip > maxSize)
			bytesToSkip = maxSize;
		if (bytesToSkip > 0) {
			if (!XIODevice_seek_base(self, curPos + bytesToSkip))
				return skippedSoFar ? skippedSoFar : -1;
			d->pos = curPos + bytesToSkip;
			d->devicePos = d->pos;
			if (bytesToSkip == maxSize)
				return skippedSoFar + bytesToSkip;
			skippedSoFar += bytesToSkip;
			maxSize -= bytesToSkip;
		}
	}

	/* ③ 兜底：逐块读丢弃。 */
	skipResult = XIODevice_skipData_base(self, maxSize);
	if (skippedSoFar == 0)
		return skipResult;
	if (skipResult == -1)
		return skippedSoFar;
	return skippedSoFar + skipResult;
}

void XIODevice_ungetChar(XIODevice* self, char c)
{
	XIODevicePrivate* d;
	if (!self || !self->m_d) return;
	if (!XIODevice_isReadable(self)) return;          /* 对齐 Qt CHECK_READABLE */
	d = self->m_d;
	if (d->transactionStarted) return;                /* 对齐 Qt：事务中拒绝 */
	XIODevicePrivate_ungetChar(d, c);
	if (!XIODevice_isSequential_base(self) && d->pos > 0)
		--d->pos;                                     /* 对齐 Qt：位置回退一格 */
}

bool XIODevice_putChar(XIODevice* self, char c)
{
	return XIODevice_write_1(self, &c, 1) == 1;
}

bool XIODevice_getChar(XIODevice* self, char* c)
{
	char discard;
	if (!self) return false;
	/* 对齐 Qt 6.8：c 为 NULL 表示丢弃该字符（getChar(nullptr) 合法）。 */
	return XIODevice_read_1(self, c ? c : &discard, 1) == 1;
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
