#include "XIODevice.h"
#include "XIODevicePrivate.h"
#include "XMemory.h"
#include "XVariant.h"
#include "XVariantList.h"
#include <string.h>
#include <stdarg.h>
XIODevice* XIODevice_create()
{
	/*if (port == NULL)
		return NULL;*/
	XIODevice* io= XMemory_malloc(sizeof(XIODevice));
	if (io == NULL)
		return io;
	XIODevice_init(io);
	return io;
}
void XIODevice_init(XIODevice* io)
{
	if (ISNULL(io, ""))
		return;
	//开始初始化
	memset(((XObject*)io)+1, 0, sizeof(XIODevice)-sizeof(XObject));
	XObject_init(io);
	XClassGetVtable(io) = XIODevice_class_init();
	XObject_addEventFilter(io, XEVENT_FUNC_RUN, XEventFuncRunCB,NULL);
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
int XIODevice_readChannelCount(const XIODevice* self) {
	return (self && self->m_d) ? self->m_d->maxReadChannels : 1;
}

int XIODevice_writeChannelCount(const XIODevice* self) {
	(void)self;
	return 1; // Qt 中通常为 1
}

int XIODevice_currentReadChannel(const XIODevice* self) {
	return self ? self->m_currentReadChannel : 0;
}
void XIODevice_setCurrentReadChannel(XIODevice* self, int channel) {
	if (self && channel >= 0) {
		self->m_currentReadChannel = channel;
	}
}

int XIODevice_currentWriteChannel(const XIODevice* self) {
	return self ? self->m_currentWriteChannel : 0;
}

void XIODevice_setCurrentWriteChannel(XIODevice* self, int channel) {
	if (self) self->m_currentWriteChannel = channel;
}
int64_t XIODevice_read(XIODevice* self, char* data, int64_t maxlen)
{
	if (!self) return false;
	//if (XIODevice_isSequential(self)) return false;
	return XIODevice_readData_base(self, data, maxlen);
}
XByteArray* XIODevice_read_new(XIODevice* self, int64_t maxlen)
{
	if (maxlen <= 0) return XByteArray_create();
	char* buf = (char*)XMemory_malloc(maxlen);
	if (!buf) return XByteArray_create();
	int64_t n = XIODevice_read(self, buf, maxlen);
	XByteArray* result = XByteArray_create(buf, (n > 0) ? n : 0);
	XMemory_free(buf);
	return result;
}
XByteArray* XIODevice_readAll_new(XIODevice* self)
{
	XByteArray* result = XByteArray_create();
	char buffer[4096];
	int64_t n;
	while ((n = XIODevice_read(self, buffer, sizeof(buffer))) > 0) {
		XByteArray_append_array_base(result, buffer, n);
	}
	return result;
}
int64_t XIODevice_readLine(XIODevice* self, char* data, int64_t maxlen)
{
	if (!self || !self->m_d || !data || maxlen <= 0) return -1;
	if (!XIODevice_isReadable(self)) return -1;

	int ch = XIODevice_currentReadChannel(self);
	int64_t fromBuf = XIODevicePrivate_readLineFromBuffer(self->m_d, data, maxlen, ch);
	if (fromBuf > 0) return fromBuf;

	if (XClassGetVirtualFunc(self, EXIODevice_ReadData, int64_t(*)(XIODevice*, char*, int64_t))) {
		return XIODevice_readData_base(self, data, maxlen);
	}

	// Fallback to read one by one (inefficient but safe)
	int64_t i = 0;
	char c;
	while (i < maxlen - 1 && XIODevice_getChar(self, &c)) {
		data[i++] = c;
		if (c == '\n') break;
	}
	if (i > 0) {
		data[i] = '\0';
		return i;
	}
	return -1;
}
XByteArray* XIODevice_readLine_new(XIODevice* self, int64_t maxlen)
{
	if (maxlen <= 0) maxlen = 1024;
	char* buf = (char*)XMemory_malloc(maxlen);
	if (!buf) return XByteArray_create();
	int64_t n = XIODevice_readLine(self, buf, maxlen);
	XByteArray* result = XByteArray_create_with_data(buf, (n > 0) ? n : 0);
	XMemory_free(buf);
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
int64_t XIODevice_write(XIODevice* self, const char* data, int64_t len)
{
	if (!self || !data || len <= 0 || !XIODevice_isWritable(self)) return -1;
	int64_t written = XIODevice_writeData_base(self, data, len);
	if (written > 0) {
		XIODevice_bytesWritten_signal(self, written);
	}
	return written;
}
int64_t XIODevice_write_cstr(XIODevice* self, const char* data) {
	if (!data) return 0;
	return XIODevice_write(self, data, strlen(data));
}

int64_t XIODevice_write_byteArray(XIODevice* self, const XByteArray* data) {
	if (!data) return 0;
	return XIODevice_write(self, XContainerDataPtr(data), XByteArray_size_base(data));
}
int64_t XIODevice_peek(XIODevice* self, char* data, int64_t maxlen)
{
	if (!self || !self->m_d || !data || maxlen <= 0 || !XIODevice_isReadable(self)) return 0;
	int ch = XIODevice_currentReadChannel(self);
	return XIODevicePrivate_peek(self->m_d, data, maxlen, self, ch);
}
XByteArray* XIODevice_peek_new(XIODevice* self, int64_t maxlen)
{
	if (maxlen <= 0) return XByteArray_create();
	char* buf = (char*)XMemory_malloc(maxlen);
	if (!buf) return XByteArray_create();
	int64_t n = XIODevice_peek(self, buf, maxlen);
	XByteArray* result = XByteArray_create_with_data(buf, n);
	XMemory_free(buf);
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
		int ch = XIODevice_currentReadChannel(self);
		XIODevicePrivate_putChar(self->m_d, c, ch);
	}
}
bool XIODevice_putChar(XIODevice* self, char c)
{
	return XIODevice_write(self, &c, 1) == 1;
}
bool XIODevice_getChar(XIODevice* self, char* c)
{
	if (!self || !c) return false;
	int ch = XIODevice_currentReadChannel(self);
	if (self->m_d && XIODevicePrivate_getChar(self->m_d, c, ch)) return true;
	return XIODevice_read(self, c, 1) == 1;
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
	str ? self->m_d->errorString = XString_create_fmt_utf8(str) : NULL;
}
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
	return XClassGetVirtualFunc(self, EXIODevice_WaitForReadyRead, bool(*)(XIODevice*,int))(self,msecs);
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
	return XClassGetVirtualFunc(self, EXIODevice_ReadData, int64_t(*)(XIODevice*, char*,int64_t))(self, data,maxlen);
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
		return  ;
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
	return XClassGetVirtualFunc(self, EXIODevice_Seek, bool(*)(XIODevice*,int64_t))(self,pos);
}

void* XIODevice_aboutToClose_signal(XIODevice* io)
{
	if (io)
		XObject_emitSignal(io, XIODevice_aboutToClose_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
	return XIODevice_aboutToClose_signal;
}

void* XIODevice_channelBytesWritten_signal(XIODevice* self, int channel, int64_t bytes)
{
	if (self)
	{
		XVariant_Init(var,NULL,0, XVariantType_NULL);
		XVariantList* list = XVariantList_create();

		XVariant_setValue_int(var,channel);
		XVariantList_push_back_move_base(list, var);

		XVariant_setValue_int64(var, bytes);
		XVariantList_push_back_move_base(list, var);

		XObject_emitSignal(self, XIODevice_channelBytesWritten_signal, list, XVariantList_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
	}
	return XIODevice_channelBytesWritten_signal;
}

void* XIODevice_channelReadyRead_signal(XIODevice* self, int channel)
{
	if (self)
		XObject_emitSignal(self, XIODevice_channelReadyRead_signal, XVariant_create_int(channel), XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
	return XIODevice_channelReadyRead_signal;
}

void* XIODevice_readChannelFinished_signal(XIODevice* self)
{
	if (self)
		XObject_emitSignal(self, XIODevice_readChannelFinished_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
	return XIODevice_readChannelFinished_signal;
}

void* XIODevice_readyRead_signal(XIODevice* io)
{
	if (io)
		XObject_emitSignal(io, XIODevice_readyRead_signal, NULL, NULL, NULL, XEVENT_PRIORITY_NORMAL);
	return XIODevice_readyRead_signal;
}
void* XIODevice_bytesWritten_signal(XIODevice* io, int64_t bytes)
{
	if (io)
		XObject_emitSignal(io, XIODevice_bytesWritten_signal, XVariant_create_int64(bytes), XVariant_delete_base, NULL, XEVENT_PRIORITY_NORMAL);
	return XIODevice_bytesWritten_signal;
}
