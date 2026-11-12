#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XIODevicePrivate.h"
#include "XCircularQueue.h"
#include "XLockFreeQueue.h"
#include "XMemory.h"
#include "XCoreApplication.h"
#include "XFileDescriptor.h"
#include "XDateTime.h"
#include "XRingBuffer.h"
#include <string.h>
#include <assert.h>

static void VXIODevice_deinit(XIODevice* io);
static int64_t VXIODevice_readData(XIODevice* self, char* data, int64_t maxlen);
static int64_t VXIODevice_writeData(XIODevice* self, const char* data, int64_t len);
static bool VXIODevice_open(XIODevice* self, XIODeviceBaseMode mode);
static void VXIODevice_close(XIODevice* self);
static bool VXIODevice_isSequential(const XIODevice* self);
static int64_t VXIODevice_pos(const XIODevice* self);
static int64_t VXIODevice_size(const XIODevice* self);
static bool VXIODevice_seek(XIODevice* self, int64_t pos);
static bool VXIODevice_atEnd(const XIODevice* self);
static bool VXIODevice_reset(XIODevice* self);
static int64_t VXIODevice_bytesAvailable(const XIODevice* self);
static int64_t VXIODevice_bytesToWrite(const XIODevice* self);
static bool VXIODevice_canReadLine(const XIODevice* self);
static bool VXIODevice_waitForReadyRead(XIODevice* self, int msecs);
static bool VXIODevice_waitForBytesWritten(XIODevice* self, int msecs);
static int64_t VXIODevice_readLineData(XIODevice* self, char* data, int64_t maxlen);
static int64_t VXIODevice_skipData(XIODevice* self, int64_t maxSize);


void VXIODevice_deinit(XIODevice* obj)
{
	XIODevice* self = (XIODevice*)obj;
	if (!self) return;

	XIODevice_close_base(obj);
	// 清理私有数据: 现在只需要删除单个 readBuffer 和 writeBuffer
	if (self->m_d) {
		XIODevicePrivate_delete(self->m_d); // 直接调用新的 delete 函数
		self->m_d = NULL;
	}
	 if (obj->m_fd >= 0) {
		XFd_free(obj->m_fd);
		obj->m_fd = XFD_INVALID;
	}
	// 释放父对象
	XClass_Deinit_Parent(XObject, obj);
}

int64_t VXIODevice_readData(XIODevice* self, char* data, int64_t maxlen)
{
	return -1; // 表示未实现
}

int64_t VXIODevice_writeData(XIODevice* self, const char* data, int64_t len)
{
	return -1; // 表示未实现
}

/**
 * @brief 基类 open 的具体实现（非虚）：完全对齐 Qt 6.8 `QIODevice::open`
 *        （qiodevice.cpp:756-773）。
 *
 * @details Qt 原文语义（逐条落地）：
 *          ① `openMode = mode`；
 *          ② `pos = (mode & Append) ? size() : 0`；
 *          ③ `readBuffers.clear() / writeBuffers.clear()`——**重开设备必须丢掉
 *             上一次会话的预读数据**，否则同一对象换文件后会先读到旧文件尾巴；
 *          ④ 通道数按可读/可写置 1 或 0；
 *          ⑤ `errorString.clear()`；
 *          ⑥ 返回 true（Qt 允许对同一对象重复 open；QFile 才另加「已打开」闸）。
 *
 * @note 子类重载 open 时必须调用**本函数**（不是 `XIODevice_open_base`，那会
 *       经虚表回到子类自己）来收口上述状态——对齐 Qt 里 `QFile::open()` 中
 *       `engine->open()` 之后调 `QIODevice::open(mode)` 的写法（qfile.cpp:922）。
 * @note **`mode == NotOpen` 返回 true**（P0-E14，对齐 Qt 6.8 实测）：Qt 的
 *       `QIODevice::open(OpenMode)` 没有 NotOpen 早退，它把 `openMode = NotOpen`
 *       照写、随后 `isOpen()` 自然为 false —— 返回的 true 只表示「调用被接受」。
 *       本框架保留「不写状态」的边界早退（NotOpen 时不碰 pos/通道数/缓冲），
 *       仅把返回值改成 true 与 Qt 同口径。修复前返回 false 会让调用方以为
 *       「打开失败」而 Qt 里这个调用是成功的。
 */
bool XIODevice_open(XIODevice* self, XIODeviceBaseMode mode)
{
	if (!self || !self->m_d) return false;
	if (mode == XIODevice_NotOpen) return true;   /* Qt 同：接受调用、状态仍为 NotOpen */

	self->m_openMode = (uint16_t)mode;
	self->m_d->pos = (mode & XIODevice_Append) ? XIODevice_size_base(self) : 0;
	self->m_d->devicePos = self->m_d->pos;
	XIODevicePrivate_clearReadBuffers(self->m_d);
	XIODevicePrivate_setWriteChannelCount(self->m_d, 0);
	XIODevicePrivate_setReadChannelCount(self->m_d, XIODevice_isReadable(self) ? 1 : 0);
	XIODevicePrivate_setWriteChannelCount(self->m_d, XIODevice_isWritable(self) ? 1 : 0);
	if (self->m_d->errorString) {
		XClassDelete(self->m_d->errorString);
		self->m_d->errorString = NULL;
	}
	return true;
}

/** @brief 虚函数默认实现：直接用基类 open 语义（对齐 Qt `QIODevice::open`）。 */
static bool VXIODevice_open(XIODevice* self, XIODeviceBaseMode mode)
{
	return XIODevice_open(self, mode);
}

/**
 * @brief 基类 close 的具体实现（非虚）：对齐 Qt 6.8 `QIODevice::close`
 *        （qiodevice.cpp:779-800）。
 *
 * @details 顺序：**先发 aboutToClose()**，再置 NotOpen / pos=0 / 事务清零 /
 *          读通道数=0（读缓冲随通道销毁而丢弃）。Qt 明确**不清写缓冲**
 *          （"Do not clear write buffers to allow delayed close in sockets"），
 *          这里同样保留。
 * @note 子类重载 Close 时必须调用**本函数**（不是 `XIODevice_close_base`），
 *       再自行关闭句柄——对齐 Qt `QFileDevice::close()` 里
 *       `QIODevice::close()` 之后 `fileEngine->close()` 的顺序。
 */
void XIODevice_close(XIODevice* self)
{
	if (!self) return;
	if (self->m_openMode == XIODevice_NotOpen) return;

	if (self->m_d) {
		if (!self->m_d->aboutToCloseEmitted) {
			XIODevice_aboutToClose_signal(self);
			self->m_d->aboutToCloseEmitted = true;
		}
		self->m_d->aboutToCloseEmitted = false;
	}
	self->m_openMode = XIODevice_NotOpen;
	/* readChannelFinished：读通道随 close 结束（对齐 Qt 多通道设备的通道
	 * 生命周期信号；Qt 由 QProcess 在通道 EOF 时发射，本框架的 XProcess
	 * 尚未发射，基类 close 是「读通道数归零」的唯一收口点，见对齐台账
	 * gaps #32）。只在确有打开的读通道时发，重复 close 不重复发；放在
	 * openMode 清零之后，槽里重入 close 会因 NotOpen 早退。 */
	if (self->m_d && self->m_d->readChannelCount > 0)
		XIODevice_readChannelFinished_signal(self);
	if (self->m_d) {
		self->m_d->pos = 0;
		self->m_d->devicePos = 0;
		self->m_d->transactionStarted = false;
		self->m_d->transactionReadPos = 0;
		self->m_d->baseReadLineDataCalled = false;
		self->m_d->ungetCount = 0;
		XIODevicePrivate_setReadChannelCount(self->m_d, 0);
		self->m_d->writeChannelCount = 0;
	}
}

/** @brief 虚函数默认实现：直接用基类 close 语义（对齐 Qt `QIODevice::close`）。 */
static void VXIODevice_close(XIODevice* self)
{
	XIODevice_close(self);
}

bool VXIODevice_isSequential(const XIODevice* self)
{
	return false;
}

/**
 * @brief 基类 pos：返回基类维护的逻辑位置（对齐 Qt `QIODevice::pos`）。
 * @note 顺序设备无位置概念，Qt 对顺序设备同样返回 d->pos（恒 0，因为读路径
 *       不推进它）；这里保持一致。
 */
int64_t VXIODevice_pos(const XIODevice* self)
{
	if (!self || !self->m_d) return 0;
	if (XIODevice_isSequential_base(self)) return 0;
	return self->m_d->pos;
}

/**
 * @brief 基类 size：顺序设备返回 bytesAvailable()，随机访问设备返回 0
 *        （对齐 Qt 6.8 qiodevice.cpp:832-838：随机访问的「真实大小」由子类给出）。
 */
int64_t VXIODevice_size(const XIODevice* self)
{
	if (!self) return 0;
	return XIODevice_isSequential_base(self) ? XIODevice_bytesAvailable_base(self) : 0;
}

/**
 * @brief 基类 seek 的具体实现（非虚）：对齐 Qt 6.8 qiodevice.cpp:853-880。
 *
 * @details 顺序设备拒绝、未打开拒绝、负值拒绝；成功后把 `devicePos` 置为目标
 *          并同步缓冲（seekBuffer）。**子类重载 Seek 时必须调用本函数**
 *          （对齐 Qt 里 `QBuffer::seek()` → `QIODevice::seek(pos)` 的写法）。
 */
bool XIODevice_seek(XIODevice* self, int64_t pos)
{
	if (!self || !self->m_d) return false;
	if (XIODevice_isSequential_base(self)) return false;
	if (self->m_openMode == XIODevice_NotOpen) return false;
	if (pos < 0) return false;

	self->m_d->devicePos = pos;
	XIODevicePrivate_seekBuffer(self->m_d, self->m_currentReadChannel, pos);
	return true;
}

/** @brief 虚函数默认实现：直接用基类 seek 语义（对齐 Qt `QIODevice::seek`）。 */
static bool VXIODevice_seek(XIODevice* self, int64_t pos)
{
	return XIODevice_seek(self, pos);
}

/**
 * @brief 基类 atEnd：对齐 Qt 6.8 qiodevice.cpp:910-921。
 * @details `openMode == NotOpen || (缓冲区为空 && bytesAvailable() == 0)`。
 *          原实现「顺序设备只看 ring buffer 计数」会把「内核里还有数据但尚未
 *          预读」误判成 EOF。
 */
bool VXIODevice_atEnd(const XIODevice* self)
{
	if (!self) return true;
	if (!XIODevice_isOpen(self)) return true;
	if (self->m_d && XIODevicePrivate_bufferedBytes(self->m_d, self->m_currentReadChannel) > 0)
		return false;
	return XIODevice_bytesAvailable_base(self) == 0;
}

bool VXIODevice_reset(XIODevice* self)
{
	return XIODevice_seek_base(self, 0);
}

/**
 * @brief 基类 bytesAvailable 的具体实现（非虚）：对齐 Qt 6.8
 *        `QIODevice::bytesAvailable()`（qiodevice.cpp:953-959）。
 * @details 随机访问设备 = max(size()-pos, 0)；顺序设备 = 缓冲字节 - 事务偏移。
 * @note 重载 BytesAvailable 的子类调用本函数叠加自己的引擎字节，**不能**
 *       调 `XIODevice_bytesAvailable_base`（虚表派发 → 回到子类自身 →
 *       无限递归，历史实测栈溢出；Qt 里 `QAbstractSocket::bytesAvailable()`
 *       调的是限定名 `QIODevice::bytesAvailable()`，不存在该问题）。
 */
int64_t XIODevice_bytesAvailable(const XIODevice* self)
{
	int64_t buffered;
	if (!self || !self->m_d) return 0;
	if (!XIODevice_isSequential_base(self)) {
		int64_t avail = XIODevice_size_base(self) - self->m_d->pos;
		return avail > 0 ? avail : 0;
	}
	buffered = XIODevicePrivate_bufferedBytes(self->m_d, self->m_currentReadChannel);
	if (self->m_d->transactionStarted)
		buffered -= self->m_d->transactionReadPos;
	return buffered > 0 ? buffered : 0;
}

/** @brief 虚函数默认实现：直接用基类 bytesAvailable 语义（对齐 Qt 虚函数体）。 */
static int64_t VXIODevice_bytesAvailable(const XIODevice* self)
{
	return XIODevice_bytesAvailable(self);
}

int64_t VXIODevice_bytesToWrite(const XIODevice* self)
{
	int currentChannel;
	struct XRingBuffer* buff;
	if (!self || !self->m_d) return 0;
	currentChannel = XIODevice_currentWriteChannel(self);
	/* 查询路径只读不建：修复前这里走 getOrCreateWriteBuffer，一次
	 * bytesToWrite() 就给空设备分配一块写缓冲 chunk（Qt 只读不分配）。 */
	buff = XIODevicePrivate_writeBufferConst(self->m_d, currentChannel);
	return buff ? (int64_t)XRingBuffer_available(buff) : 0;
}

/**
 * @brief 基类 canReadLine：对齐 Qt 6.8 qiodevice.cpp:1538-1544。
 * @details 缓冲区中（顺序设备从事务偏移起）存在 '\n' 即可读整行。
 */
bool VXIODevice_canReadLine(const XIODevice* self)
{
	if (!self || !self->m_d) return false;
	return XIODevicePrivate_canReadLineFromBuffer(self->m_d);
}

/**
 * @brief 基类 waitForReadyRead：对齐 Qt 6.8 qiodevice.cpp:2072-2078。
 * @details Qt 基类**什么都不做、立即返回 false**（"The default implementation
 *          does nothing, and returns false"）。原实现在这里自旋 processEvents
 *          等 bytesAvailable 变化，会让没有重载该函数的设备（XFile 就在此列）
 *          把调用方阻塞满 msecs。
 */
bool VXIODevice_waitForReadyRead(XIODevice* self, int msecs)
{
	(void)self;
	(void)msecs;
	return false;
}

/**
 * @brief 基类 waitForBytesWritten：对齐 Qt 6.8 qiodevice.cpp:2104-2110（同上，立即 false）。
 */
bool VXIODevice_waitForBytesWritten(XIODevice* self, int msecs)
{
	(void)self;
	(void)msecs;
	return false;
}

/**
 * @brief 基类 readLineData 的具体实现（**非虚**）：对齐 Qt 6.8
 *        `QIODevice::readLineData`（qiodevice.cpp:1502-1522）。
 * @details 逐字节 getChar 直到 '\n' 或 maxlen；**返回部分行**是合法结果。
 *          成功读完时把 `baseReadLineDataCalled` 置位，供 readLine 判断是否需要
 *          再补一次 pos 推进（对齐 Qt 同名字段）。
 * @note **事务中的 readLine 必须调用本函数**（不是 `XIODevice_readLineData_base`）：
 *       Qt 原文 qiodevice.cpp:1391-1395 —— "Force base implementation for
 *       transaction on sequential device as it stores the data in internal buffer
 *       automatically"。XSerialPort / XFileDevice 的 readLineData 重载绕过
 *       `read()` 直读 fd，事务里读到的行不会进内部缓冲，rollback 就无法重放。
 *       调用 `_base` 会经虚表回到子类自身，正是漏掉这条分支的表现。
 */
int64_t XIODevice_readLineData(XIODevice* self, char* data, int64_t maxlen)
{
	int64_t readSoFar = 0;
	int64_t lastReadReturn = 0;
	char c;
	if (!self || !data || maxlen <= 0) return 0;
	if (self->m_d) self->m_d->baseReadLineDataCalled = true;

	while (readSoFar < maxlen && (lastReadReturn = XIODevice_read_1(self, &c, 1)) == 1) {
		data[readSoFar++] = c;
		if (c == '\n')
			break;
	}
	if (lastReadReturn != 1 && readSoFar == 0)
		return XIODevice_isSequential_base(self) ? lastReadReturn : -1;
	return readSoFar;
}

/** @brief 虚函数默认实现：直接用基类 readLineData 语义。 */
static int64_t VXIODevice_readLineData(XIODevice* self, char* data, int64_t maxlen)
{
	return XIODevice_readLineData(self, data, maxlen);
}

int64_t VXIODevice_skipData(XIODevice* self, int64_t maxSize)
{
	if (!self || maxSize <= 0) return 0;

	// 从设备读取并丢弃
	char temp[4096];
	int64_t skipped = 0;
	while (skipped < maxSize) {
		int64_t remaining = maxSize - skipped;
		int64_t toRead = (remaining > (int64_t)sizeof(temp)) ? (int64_t)sizeof(temp) : remaining;
		int64_t n = XIODevice_read_1(self, temp, toRead);
		if (n <= 0) break;
		skipped += n;
	}

	return skipped;
}

/**
 * @brief 判断子类是否重载了 Pos 虚函数槽。
 * @details 基类读路径需要知道「d->pos 是否就是设备的真实位置」：重载了 Pos 的
 *          子类（XFileDevice 等）会自己维护位置，基类必须在读入口按它报告的值
 *          校准，否则「外部 seek + 预读 + 部分读」会让基类把设备指针拉回旧值。
 *          未重载时字面量就是基类自己的 d->pos，无需任何额外调用。
 */
bool XIODevice_isPosOverridden(const XIODevice* self)
{
	typedef void (*XIODeviceFn)(void);
	XIODeviceFn fn;
	if (!self || !XClassGetVtable(self)) return false;
	fn = XClassGetVirtualFunc(self, EXIODevice_Pos, XIODeviceFn);
	return fn != (XIODeviceFn)VXIODevice_pos;
}

/** @brief 判断子类是否重载了 Seek 虚函数槽（同上，仅供诊断/子类契约检查）。 */
bool XIODevice_isSeekOverridden(const XIODevice* self)
{
	typedef void (*XIODeviceFn)(void);
	XIODeviceFn fn;
	if (!self || !XClassGetVtable(self)) return false;
	fn = XClassGetVirtualFunc(self, EXIODevice_Seek, XIODeviceFn);
	return fn != (XIODeviceFn)VXIODevice_seek;
}

XVtable* XIODevice_class_init()
{
	XVTABLE_INIT_DEFAULT(XIODevice)
	//继承类
	XVTABLE_INHERIT_XCLASS(XObject);
	void* table[] = {
		VXIODevice_open,VXIODevice_close,VXIODevice_isSequential ,
		VXIODevice_pos ,VXIODevice_size ,VXIODevice_seek ,
		VXIODevice_atEnd ,VXIODevice_reset ,VXIODevice_bytesAvailable ,
		VXIODevice_bytesToWrite ,VXIODevice_canReadLine ,
		VXIODevice_waitForReadyRead ,VXIODevice_waitForBytesWritten ,
		VXIODevice_readData ,VXIODevice_readLineData ,
		VXIODevice_skipData ,VXIODevice_writeData
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXIODevice_deinit);
	XCLASS_SHOW_SIZE_DEFAULT(XIODevice);
	return XVTABLE_DEFAULT;
}
