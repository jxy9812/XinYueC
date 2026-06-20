#include "XIODevice.h"
#include "XIODevice_Protected.h"
#include "XCircularQueue.h"
#include "XLockFreeQueue.h"
#include "XMemory.h"
#include "XIODevicePrivate.h"
#include "XCoreApplication.h"
#include "XDateTime.h"
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


XVtable* XIODevice_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XIODevice))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
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
#if SHOWCONTAINERSIZE
	printf("XIODevice size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
void VXIODevice_deinit(XIODevice* obj)
{
	XIODevice* self = (XIODevice*)obj;
	if (!self) return;

	//// 安全关闭
	//if (self->m_openMode != XIODevice_NotOpen) {
	//	if (!self->m_d->aboutToCloseEmitted) {
	//		XIODevice_aboutToClose_signal(self);
	//		self->m_d->aboutToCloseEmitted = true;
	//	}
	//	self->m_openMode = XIODevice_NotOpen;
	//}
	XIODevice_close_base(obj);
	// 清理私有数据: 现在只需要删除单个 readBuffer 和 writeBuffer
	if (self->m_d) {
		XIODevicePrivate_delete(self->m_d); // 直接调用新的 delete 函数
		self->m_d = NULL;
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

bool VXIODevice_open(XIODevice* self, XIODeviceBaseMode mode)
{
	if (!self || mode == XIODevice_NotOpen) return false;
	if (self->m_openMode != XIODevice_NotOpen) return false;
	self->m_openMode = mode;
	return true;
}

void VXIODevice_close(XIODevice* self)
{
	if (!self || self->m_openMode == XIODevice_NotOpen) return;

	if (!self->m_d->aboutToCloseEmitted) {
		XIODevice_aboutToClose_signal(self);
		self->m_d->aboutToCloseEmitted = true;
	}

	// 关闭时，基类不负责清空缓冲区，由具体子类决定
	// 但可以提交任何未完成的事务
	if (self->m_d->transactionStarted) {
		XIODevicePrivate_commitTransaction(self->m_d);
	}

	self->m_openMode = XIODevice_NotOpen;
	self->m_d->aboutToCloseEmitted = false;
}

bool VXIODevice_isSequential(const XIODevice* self)
{
	return false;
}

int64_t VXIODevice_pos(const XIODevice* self)
{
	if (!self) return 0;
	// sequential 设备无位置概念
	if (XIODevice_isSequential_base(self)) return 0;
	// 非 sequential：子类应重写，基类返回 0
	return 0;
}

int64_t VXIODevice_size(const XIODevice* self)
{
	return 0; // 默认无大小
}

bool VXIODevice_seek(XIODevice* self, int64_t pos)
{
	if (!self || pos < 0) return false;
	if (XIODevice_isSequential_base(self)) return false;
	// 基类无法 seek，返回 false（子类如 QFile 应重写）
	return false;
}

bool VXIODevice_atEnd(XIODevice* self)
{
	if (!self) return true;
	if (!XIODevice_isOpen(self)) return true;

	if (XIODevice_isSequential_base(self)) {
		return (XIODevice_bytesAvailable_base(self) == 0);
	}
	return (XIODevice_pos_base(self) >= XIODevice_size_base(self));
}

bool VXIODevice_reset(XIODevice* self)
{
	if (!self) return false;
	if (XIODevice_isSequential_base(self)) return false;
	return XIODevice_seek_base(self, 0);
}

int64_t VXIODevice_bytesAvailable(const XIODevice* self)
{
	if (!self || !self->m_d) return 0;
	// 直接返回 readBuffer 的可用字节数
	int currentReadChannel = XIODevice_currentReadChannel(self);
	struct XRingBuffer* readBuf = XIODevicePrivate_getOrCreateReadBuffer(self->m_d, currentReadChannel);
	return XRingBuffer_available(readBuf);
}

int64_t VXIODevice_bytesToWrite(const XIODevice* self)
{
	if (!self || !self->m_d) return 0;
	// 直接返回 readBuffer 的可用字节数
	int currentChannel = XIODevice_currentWriteChannel(self);
	struct XRingBuffer* buff = XIODevicePrivate_getOrCreateWriteBuffer(self->m_d, currentChannel);
	return XRingBuffer_available(buff);
}

bool VXIODevice_canReadLine(const XIODevice* self)
{
	if (!self || !self->m_d) return false;
	// 不再需要 channel，直接检查 readBuffer
	return XIODevicePrivate_canReadLineFromBuffer(self->m_d);
}

bool VXIODevice_waitForReadyRead(XIODevice* self, int msecs)
{
	if (XIODevice_bytesAvailable_base(self) > 0) return true;

	uint64_t current = XDateTime_currentMSecsSinceEpoch();
	while (XIODevice_bytesAvailable_base(self) ==0)
	{
		XCoreApplication_processEvents(XEventLoop_AllEvents);
		if (XDateTime_currentMSecsSinceEpoch() > (current + msecs))
			return false;
	}
	return true; // 
}

bool VXIODevice_waitForBytesWritten(XIODevice* self, int msecs)
{
	if (XIODevice_bytesToWrite_base(self)==0) return true;

	size_t current = XDateTime_currentMSecsSinceEpoch();
	while (XIODevice_bytesToWrite_base(self)>0)
	{
		XCoreApplication_processEvents(XEventLoop_AllEvents);
		if (XDateTime_currentMSecsSinceEpoch() > current + msecs)
			return false;
	}
	return true; // 
}

int64_t VXIODevice_readLineData(XIODevice* self, char* data, int64_t maxlen)
{
	return XIODevicePrivate_readLineFromBuffer(self->m_d, data, maxlen);
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
		int64_t n = XIODevice_readData_base(self, temp, toRead);
		if (n <= 0) break;
		skipped += n;
	}

	return skipped;
}