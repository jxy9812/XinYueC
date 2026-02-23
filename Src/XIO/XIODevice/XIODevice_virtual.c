#include"XIODevice.h"
#include"XCircularQueue.h"
#include"XCircularQueueAtomic.h"
#include "XMemory.h"
#include "XIODevicePrivate.h"
#include <string.h>
#include <assert.h>
//声明 
//static bool XIODevicePrivate_fillFromBuffer(XIODevice* self, char* data, int64_t maxlen, int channel);
static bool XIODevicePrivate_canReadLineFromBuffer(const XIODevicePrivate* d, int channel);

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
	XVTABLE_INHERIT_DEFAULT(XObject_class_init());
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

	// 安全关闭
	if (self->m_openMode != XIODevice_NotOpen) {
		if (!self->m_d->aboutToCloseEmitted) {
			XIODevice_aboutToClose_signal(self);
			self->m_d->aboutToCloseEmitted = true;
		}
		self->m_openMode = XIODevice_NotOpen;
	}

	// 清理私有数据
	if (self->m_d) {
		for (int i = 0; i < self->m_d->maxReadChannels; ++i) {
			if (self->m_d->readBuffers[i]) XByteArray_delete_base(self->m_d->readBuffers[i]);
			if (self->m_d->ungetBuffers[i]) XByteArray_delete_base(self->m_d->ungetBuffers[i]);
		}
		XMemory_free(self->m_d->readBuffers);
		XMemory_free(self->m_d->ungetBuffers);
		if (self->m_d->writeBuffer) XByteArray_delete_base(self->m_d->writeBuffer);
		if (self->m_d->transactionBuffer) XByteArray_delete_base(self->m_d->transactionBuffer);
		XMemory_free(self->m_d);
		self->m_d = NULL;
	}
	// 释放父对象
	XClass_Deinit_Parent(XObject, obj);
	//XVtableGetFunc(XObject_class_init(), EXClass_Deinit, void(*)(XObject*))(io);
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

	// 清空所有缓冲区
	for (int i = 0; i < self->m_d->maxReadChannels; ++i) {
		XByteArray_clear_base(self->m_d->readBuffers[i]);
		XByteArray_clear_base(self->m_d->ungetBuffers[i]);
	}
	XByteArray_clear_base(self->m_d->writeBuffer);

	if (self->m_d->transactionStarted) {
		XIODevicePrivate_commitTransaction(self->m_d);
	}

	self->m_openMode = XIODevice_NotOpen;
	self->m_d->aboutToCloseEmitted = false;
}

bool VXIODevice_isSequential(const XIODevice * self)
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
	int ch = XIODevice_currentReadChannel(self);
	if (ch < 0 || ch >= self->m_d->maxReadChannels) return 0;
	return XByteArray_size_base(self->m_d->readBuffers[ch]);
}

int64_t VXIODevice_bytesToWrite(const XIODevice* self)
{
	(void)self;
	// Qt 基类不管理写缓冲，返回 0
	return 0;
}

bool VXIODevice_canReadLine(const XIODevice* self)
{
	if (!self || !self->m_d) return false;
	int ch = XIODevice_currentReadChannel(self);
	if (XIODevicePrivate_canReadLineFromBuffer(self->m_d, ch)) return true;
	return false;
}

bool VXIODevice_waitForReadyRead(XIODevice* self, int msecs)
{
	// 默认不支持同步等待
	return false;
}

bool VXIODevice_waitForBytesWritten(XIODevice* self, int msecs)
{
	return false;
}

int64_t VXIODevice_readLineData(XIODevice* self, char* data, int64_t maxlen)
{
	return -1; // 表示未实现
}

int64_t VXIODevice_skipData(XIODevice* self, int64_t maxSize)
{
	if (!self || maxSize <= 0) return 0;

	int ch = XIODevice_currentReadChannel(self);
	XByteArray* buf = self->m_d->readBuffers[ch];
	int64_t buffered = buf ? XByteArray_size_base(buf) : 0;
	int64_t skipped = 0;

	// 先跳过缓冲区
	if (buffered > 0) {
		int64_t toSkip = (buffered < maxSize) ? buffered : maxSize;
		XByteArray_remove_base(buf, 0, toSkip);
		skipped = toSkip;
		if (skipped >= maxSize) return skipped;
	}

	// 从设备读取并丢弃剩余部分
	char temp[4096];
	while (skipped < maxSize) {
		int64_t remaining = maxSize - skipped;
		int64_t toRead = (remaining > (int64_t)sizeof(temp)) ? (int64_t)sizeof(temp) : remaining;
		int64_t n = XIODevice_readData_base(self, temp, toRead);
		if (n <= 0) break;
		skipped += n;
	}

	return skipped;
}
// ========== 内部辅助函数 ==========
//static bool XIODevicePrivate_fillFromBuffer(XIODevice* self, char* data, int64_t maxlen, int channel) {
//	if (!self || !self->m_d || !data || maxlen <= 0) return false;
//	XByteArray* buf = self->m_d->readBuffers[channel];
//	if (!buf || XByteArray_size_base(buf) == 0) return false;
//
//	int64_t toCopy = (XByteArray_size_base(buf) < maxlen) ? XByteArray_size_base(buf) : maxlen;
//	memcpy(data, XByteArray_const_data_base(buf), (size_t)toCopy);
//	XByteArray_remove_base(buf, 0, toCopy);
//	return true;
//}

bool XIODevicePrivate_canReadLineFromBuffer(const XIODevicePrivate* d, int channel) {
	if (!d || !d->readBuffers[channel]) return false;
	const char* p = XContainerDataPtr(d->readBuffers[channel]);
	int64_t len = XByteArray_size_base(d->readBuffers[channel]);
	for (int64_t i = 0; i < len; ++i) {
		if (p[i] == '\n') return true;
	}
	return false;
}