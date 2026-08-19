#include "XDevice.h"
#include "XDeviceFile.h"
#include "XDeviceNetwork.h"
#include "XDeviceSerialPort.h"
#include "XDeviceTimer.h"
#include "XDeviceDir.h"
#include "XDeviceConsole.h"
#include "XFileDescriptor.h"
#include "XVariant.h"
#include "XVarList.h"
#include <stdlib.h>
#include <string.h>

/* 兼容旧 XFd 文件描述符的内部实现，不属于 XDevice 公共 API。 */
void XDeviceFile_legacyClose(XFd fd);
int64_t XDeviceFile_legacyRead(XFd fd, void* buffer, int64_t size);
int64_t XDeviceFile_legacyWrite(XFd fd, const void* data, int64_t size);
int64_t XDeviceFile_legacySeek(XFd fd, int64_t offset, XSeekWhence whence);
bool XDeviceFile_legacyFlush(XFd fd);
bool XDeviceFile_legacyResize(XFd fd, int64_t size);
bool XDeviceFile_legacySetStandardInputEcho(XFd fd, bool enabled);
void* XDeviceFile_legacyMap(XFd fd, int64_t offset, int64_t size, int flags);
bool XDeviceFile_legacyUnmap(void* address, int64_t size);

/* ============================================================================
 * 后端注册表（静态注册，不动态加载）
 * ============================================================================ */
#ifndef XDEVICE_MAX_DEVICES
#define XDEVICE_MAX_DEVICES 32
#endif

typedef struct XDeviceRegistryEntry
{
    char* m_className;   /**< 设备类别名（借用，指向设备虚函数表） */
    XDevice* m_device;   /**< 已注册设备对象（借用） */
} XDeviceRegistryEntry;

static XDeviceRegistryEntry g_deviceRegistry[XDEVICE_MAX_DEVICES];
static size_t g_deviceCount = 0;

/* ============================================================================
 * XDevice 基类默认虚函数实现（具体设备未实现某操作时回落为“不支持”）
 * ============================================================================ */
static XDeviceContext* VXDevice_open(XDevice* self, const XDeviceOpenOptions* opts, int* err);
static void VXDevice_close(XDevice* self, XDeviceContext* handle);
static int64_t VXDevice_read(XDevice* self, XDeviceContext* handle, void* buffer, int64_t size);
static int64_t VXDevice_write(XDevice* self, XDeviceContext* handle, const void* data, int64_t size);
static int64_t VXDevice_seek(XDevice* self, XDeviceContext* handle, int64_t offset, int whence);
static bool VXDevice_flush(XDevice* self, XDeviceContext* handle);
static bool VXDevice_resize(XDevice* self, XDeviceContext* handle, int64_t size);
static bool VXDevice_setProperty(XDevice* self, XDeviceContext* handle, uint32_t property, const XVariant* value);
static bool VXDevice_getProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value);
static bool VXDevice_queryProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value);
static bool VXDevice_control(XDevice* self, XDeviceContext* handle, uint32_t command,
                             const XVarList* in, XVarList* out);

XVtable* XDevice_class_init(void)
{
	XVTABLE_INIT_DEFAULT(XDevice)
	XVTABLE_INHERIT_XCLASS(XClass);
	void* const table[] = {
		VXDevice_open,
		VXDevice_close,
		VXDevice_read,
		VXDevice_write,
		VXDevice_seek,
		VXDevice_flush,
		VXDevice_resize,
		VXDevice_setProperty,
		VXDevice_getProperty,
		VXDevice_queryProperty,
		VXDevice_control
	};
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	XCLASS_SET_CLASS_NAME_DEFAULT("device");
	XCLASS_SHOW_SIZE_DEFAULT(XDevice);
	return XVTABLE_DEFAULT;
}

void XDevice_init(XDevice* self)
{
	if (!self) return;
	memset(((XClass*)self) + 1, 0, sizeof(XDevice) - sizeof(XClass));
	XClass_init(&self->m_class);
	XClassSetVtable(self, XDevice);
	self->m_type = XDeviceType_Free;
	self->m_defaultOpenOptions = NULL;
	self->m_capabilities = XDeviceCap_None;
	self->m_registered = false;
	self->m_refCount = 0;
}

XDevice* XDevice_create(void)
{
	XDevice* self = (XDevice*)XClass_Malloc(XDevice);
	if (!self) return NULL;
	XDevice_init(self);
	Set_Class_IsHeap(self, true);
	return self;
}

void XDevice_ref(XDevice* self)
{
	if (!self) return;
	++self->m_refCount;
}

void XDevice_unref(XDevice* self)
{
	if (!self || self->m_refCount == 0) return;
	if (--self->m_refCount == 0 && !self->m_registered && Class_IsHeap(self)) {
		XDevice_delete_base((XClass*)self);
	}
}

static XDeviceContext* VXDevice_open(XDevice* self, const XDeviceOpenOptions* opts, int* err)
{
	(void)self; (void)opts;
	if (err) *err = (int)XDeviceError_NotSupported;
	return NULL;
}

static void VXDevice_close(XDevice* self, XDeviceContext* handle)
{
	(void)self; (void)handle;
}

static int64_t VXDevice_read(XDevice* self, XDeviceContext* handle, void* buffer, int64_t size)
{
	(void)self; (void)handle; (void)buffer; (void)size;
	return -1;
}

static int64_t VXDevice_write(XDevice* self, XDeviceContext* handle, const void* data, int64_t size)
{
	(void)self; (void)handle; (void)data; (void)size;
	return -1;
}

static int64_t VXDevice_seek(XDevice* self, XDeviceContext* handle, int64_t offset, int whence)
{
	(void)self; (void)handle; (void)offset; (void)whence;
	return -1;
}

static bool VXDevice_flush(XDevice* self, XDeviceContext* handle)
{
	(void)self; (void)handle;
	return false;
}

static bool VXDevice_resize(XDevice* self, XDeviceContext* handle, int64_t size)
{
	(void)self; (void)handle; (void)size;
	return false;
}

static bool VXDevice_setProperty(XDevice* self, XDeviceContext* handle, uint32_t property, const XVariant* value)
{
	(void)self; (void)handle; (void)property; (void)value;
	return false;
}

static bool VXDevice_getProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value)
{
	(void)self; (void)handle; (void)property; (void)value;
	return false;
}

static bool VXDevice_queryProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value)
{
	(void)self; (void)handle; (void)property; (void)value;
	return false;
}

static bool VXDevice_control(XDevice* self, XDeviceContext* handle, uint32_t command,
                             const XVarList* in, XVarList* out)
{
	(void)self; (void)handle; (void)command; (void)in; (void)out;
	return false;
}

/* ============================================================================
 * 内部虚函数调度入口（不对外公开，统一门面内部使用）
 * ============================================================================ */
static XDeviceContext* XDevice_dispatchOpen(XDevice* self, const XDeviceOpenOptions* opts, int* err)
{
	if (ISNULL(self, "") || ISNULL(opts, "")) {
		if (err) *err = (int)XDeviceError_InvalidArgument;
		return NULL;
	}
	return XClassGetVirtualFunc(self, EXDevice_Open, XDeviceOpenFn)(self, opts, err);
}

static void XDevice_dispatchClose(XDevice* self, XDeviceContext* handle)
{
	if (ISNULL(self, "") || ISNULL(handle, "")) return;
	XClassGetVirtualFunc(self, EXDevice_Close, XDeviceCloseFn)(self, handle);
}

static int64_t XDevice_dispatchRead(XDevice* self, XDeviceContext* handle, void* buffer, int64_t size)
{
	if (ISNULL(self, "") || ISNULL(handle, "") || ISNULL(buffer, "")) return -1;
	return XClassGetVirtualFunc(self, EXDevice_Read, XDeviceReadFn)(self, handle, buffer, size);
}

static int64_t XDevice_dispatchWrite(XDevice* self, XDeviceContext* handle, const void* data, int64_t size)
{
	if (ISNULL(self, "") || ISNULL(handle, "") || ISNULL(data, "")) return -1;
	return XClassGetVirtualFunc(self, EXDevice_Write, XDeviceWriteFn)(self, handle, data, size);
}

static int64_t XDevice_dispatchSeek(XDevice* self, XDeviceContext* handle, int64_t offset, int whence)
{
	if (ISNULL(self, "") || ISNULL(handle, "")) return -1;
	return XClassGetVirtualFunc(self, EXDevice_Seek, XDeviceSeekFn)(self, handle, offset, whence);
}

static bool XDevice_dispatchFlush(XDevice* self, XDeviceContext* handle)
{
	if (ISNULL(self, "") || ISNULL(handle, "")) return false;
	return XClassGetVirtualFunc(self, EXDevice_Flush, XDeviceFlushFn)(self, handle);
}

static bool XDevice_dispatchResize(XDevice* self, XDeviceContext* handle, int64_t size)
{
	if (ISNULL(self, "") || ISNULL(handle, "")) return false;
	return XClassGetVirtualFunc(self, EXDevice_Resize, XDeviceResizeFn)(self, handle, size);
}

static bool XDevice_dispatchSetProperty(XDevice* self, XDeviceContext* handle, uint32_t property, const XVariant* value)
{
	if (ISNULL(self, "") || ISNULL(handle, "") || ISNULL(value, "")) return false;
	return XClassGetVirtualFunc(self, EXDevice_SetProperty, XDeviceSetPropertyFn)(self, handle, property, value);
}

static bool XDevice_dispatchGetProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value)
{
	if (ISNULL(self, "") || ISNULL(handle, "") || ISNULL(value, "")) return false;
	return XClassGetVirtualFunc(self, EXDevice_GetProperty, XDeviceGetPropertyFn)(self, handle, property, value);
}

static bool XDevice_dispatchQueryProperty(XDevice* self, XDeviceContext* handle, uint32_t property, XVariant* value)
{
	if (ISNULL(self, "") || ISNULL(handle, "") || ISNULL(value, "")) return false;
	return XClassGetVirtualFunc(self, EXDevice_QueryProperty, XDeviceQueryPropertyFn)(self, handle, property, value);
}

static bool XDevice_dispatchControl(XDevice* self, XDeviceContext* handle, uint32_t command,
                                    const XVarList* in, XVarList* out)
{
	if (ISNULL(self, "") || ISNULL(handle, "")) return false;
	return XClassGetVirtualFunc(self, EXDevice_Control, XDeviceControlFn)(self, handle, command, in, out);
}

/* ============================================================================
 * 设备类别注册 / 查找
 * ============================================================================ */
static const char* XDevice_getClassName(const XDevice* device)
{
	if (!device) return NULL;
	return (const char*)XVTABLE_GET_NAME(XClassGetVtable(device));
}

bool XDevice_register(XDevice* device)
{
	const char* className;
	size_t i;

	if (ISNULL(device, "")) return false;
	className = XDevice_getClassName(device);
	if (ISNULL(className, "") || className[0] == '\0') return false;

	for (i = 0; i < g_deviceCount; ++i) {
		if (g_deviceRegistry[i].m_device == device ||
			(g_deviceRegistry[i].m_className &&
			 strcmp(g_deviceRegistry[i].m_className, className) == 0)) {
			return false; /* 重复注册 */
		}
	}
	if (g_deviceCount >= XDEVICE_MAX_DEVICES) return false;

	g_deviceRegistry[g_deviceCount].m_className = (char*)className;
	g_deviceRegistry[g_deviceCount].m_device = device;
	++g_deviceCount;
	device->m_registered = true;
	return true;
}

XDevice* XDevice_find(const char* className)
{
	size_t i;
	if (ISNULL(className, "") || className[0] == '\0') return NULL;
	for (i = 0; i < g_deviceCount; ++i) {
		if (g_deviceRegistry[i].m_className &&
			strcmp(g_deviceRegistry[i].m_className, className) == 0) {
			return g_deviceRegistry[i].m_device;
		}
	}
	return NULL;
}

const char* XDevice_typeClassName(XDeviceType type)
{
	switch (type) {
	case XDeviceType_File:     return "file";
	case XDeviceType_Socket:   return "socket";
	case XDeviceType_Serial:   return "serial";
	case XDeviceType_Timer:    return "timer";
	case XDeviceType_Dir:      return "dir";
	case XDeviceType_Console:  return "console";
	default:                   return NULL;
	}
}

/* ============================================================================
 * 统一门面 API
 * ============================================================================ */
XFd XDevice_open(XDeviceType type, const XDeviceOpenOptions* opts, int* err)
{
	const char* className = XDevice_typeClassName(type);
	if (!className) {
		if (err) *err = (int)XDeviceError_InvalidArgument;
		return XFD_INVALID;
	}
	return XDevice_openClass(className, opts, err);
}

XFd XDevice_openClass(const char* className, const XDeviceOpenOptions* opts, int* err)
{
	XDevice* device;
	XDeviceOpenOptions defaultOpts;
	const XDeviceOpenOptions* effectiveOpts;
	XDeviceContext* ctx = NULL;
	XFd fd;

	if (ISNULL(className, "") || className[0] == '\0') {
		if (err) *err = (int)XDeviceError_InvalidArgument;
		return XFD_INVALID;
	}
	device = XDevice_find(className);
	if (!device) {
		/* 内置文件设备支持懒注册，保证 XDevice_open(XDeviceType_File, ...) 开箱即用。 */
		if (strcmp(className, "file") == 0) {
			XDeviceFile_register();
			device = XDevice_find(className);
		}
		if (!device && strcmp(className, "socket") == 0) {
			XDeviceNetwork_register();
			device = XDevice_find(className);
		}
		if (!device && strcmp(className, "serial") == 0) {
			XDeviceSerialPort_register();
			device = XDevice_find(className);
		}
		if (!device && strcmp(className, "timer") == 0) {
			XDeviceTimer_register();
			device = XDevice_find(className);
		}
		if (!device && strcmp(className, "dir") == 0) {
			XDeviceDir_register();
			device = XDevice_find(className);
		}
		if (!device && strcmp(className, "console") == 0) {
			XDeviceConsole_register();
			device = XDevice_find(className);
		}
	}
	if (!device) {
		if (err) *err = (int)XDeviceError_NotFound;
		return XFD_INVALID;
	}
	if (device->m_type == XDeviceType_Free)
		device->m_type = XDeviceType_Class;

	if (opts) {
		effectiveOpts = opts;
	} else if (device->m_defaultOpenOptions) {
		effectiveOpts = device->m_defaultOpenOptions;
	} else {
		memset(&defaultOpts, 0, sizeof(defaultOpts));
		effectiveOpts = &defaultOpts;
	}

	ctx = XDevice_dispatchOpen(device, effectiveOpts, err);
	if (!ctx) {
		if (err && *err == 0) *err = (int)XDeviceError_IoFail;
		return XFD_INVALID;
	}
	fd = ctx->m_fd;
	if (fd == XFD_INVALID || XFd_type(fd) != XFD_TYPE_CLASS || XFd_handle(fd) != ctx) {
		fd = XFd_alloc(XFD_TYPE_CLASS, ctx, NULL);
		if (fd == XFD_INVALID) {
			XDevice_dispatchClose(device, ctx);
			if (err) *err = (int)XDeviceError_OutOfMemory;
			return XFD_INVALID;
		}
		ctx->m_fd = fd;
	}
	ctx->m_device = device;
	ctx->m_state = (uint16_t)XDeviceState_Active;
	ctx->m_pendingOps = 0;
	ctx->m_lastError = (int16_t)XDeviceError_None;
	XDevice_ref(device);
	return fd;
}

void XDevice_close(XFd fd)
{
	XFileDescriptor* desc = XFd_get(fd);
	XDeviceContext* ctx;
	XDevice* device;
	if (!desc) return;
	if ((XFdType)desc->m_type != XFD_TYPE_CLASS) {
		if ((XFdType)desc->m_type == XFD_TYPE_FILE ||
			(XFdType)desc->m_type == XFD_TYPE_MAPPING ||
			(XFdType)desc->m_type == XFD_TYPE_CONSOLE)
			XDeviceFile_legacyClose(fd);
		return;
	}
	ctx = (XDeviceContext*)desc->m_deviceCtx;
	device = ctx ? ctx->m_device : NULL;
	if (ctx) ctx->m_state = (uint16_t)XDeviceState_Closing;
	if (device && ctx) XDevice_dispatchClose(device, ctx);
	XDevice_unref(device);
	XFd_free(fd);
}

static XDeviceContext* XDevice_getCtx(XFd fd)
{
	XFileDescriptor* desc;
	XDeviceContext* ctx;
	if (fd < 0 || fd >= XFD_TABLE_SIZE) return NULL;
	desc = XFd_get(fd);
	if (!desc || (XFdType)desc->m_type != XFD_TYPE_CLASS) return NULL;
	ctx = (XDeviceContext*)desc->m_deviceCtx;
	if (!ctx || ctx->m_state == (uint16_t)XDeviceState_Closing) return NULL;
	return ctx;
}

int64_t XDevice_read(XFd fd, void* buffer, int64_t size)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	int64_t ret;
	XFileDescriptor* desc = XFd_get(fd);
	if (!ctx && desc && ((XFdType)desc->m_type == XFD_TYPE_FILE ||
		(XFdType)desc->m_type == XFD_TYPE_MAPPING ||
		(XFdType)desc->m_type == XFD_TYPE_CONSOLE))
		return XDeviceFile_legacyRead(fd, buffer, size);
	if (!ctx || !buffer || size < 0) {
		if (ctx) ctx->m_lastError = (int16_t)XDeviceError_InvalidArgument;
		return -1;
	}
	ret = XDevice_dispatchRead(ctx->m_device, ctx, buffer, size);
	if (ret < 0) ctx->m_lastError = (int16_t)XDeviceError_IoFail;
	return ret;
}

int64_t XDevice_write(XFd fd, const void* data, int64_t size)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	int64_t ret;
	XFileDescriptor* desc = XFd_get(fd);
	if (!ctx && desc && ((XFdType)desc->m_type == XFD_TYPE_FILE ||
		(XFdType)desc->m_type == XFD_TYPE_MAPPING))
		return XDeviceFile_legacyWrite(fd, data, size);
	if (!ctx || !data || size < 0) {
		if (ctx) ctx->m_lastError = (int16_t)XDeviceError_InvalidArgument;
		return -1;
	}
	ret = XDevice_dispatchWrite(ctx->m_device, ctx, data, size);
	if (ret < 0) ctx->m_lastError = (int16_t)XDeviceError_IoFail;
	return ret;
}

int64_t XDevice_seek(XFd fd, int64_t offset, XDeviceSeekWhence whence)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	int64_t ret;
	XFileDescriptor* desc = XFd_get(fd);
	if (!ctx && desc && ((XFdType)desc->m_type == XFD_TYPE_FILE ||
		(XFdType)desc->m_type == XFD_TYPE_MAPPING))
		return XDeviceFile_legacySeek(fd, offset, (XSeekWhence)whence);
	if (!ctx) return -1;
	ret = XDevice_dispatchSeek(ctx->m_device, ctx, offset, (int)whence);
	if (ret < 0) ctx->m_lastError = (int16_t)XDeviceError_IoFail;
	return ret;
}

bool XDevice_flush(XFd fd)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	XFileDescriptor* desc = XFd_get(fd);
	if (!ctx && desc && ((XFdType)desc->m_type == XFD_TYPE_FILE ||
		(XFdType)desc->m_type == XFD_TYPE_MAPPING))
		return XDeviceFile_legacyFlush(fd);
	if (!ctx) return false;
	return XDevice_dispatchFlush(ctx->m_device, ctx);
}

bool XDevice_resize(XFd fd, int64_t size)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	XFileDescriptor* desc = XFd_get(fd);
	if (!ctx && desc && ((XFdType)desc->m_type == XFD_TYPE_FILE ||
		(XFdType)desc->m_type == XFD_TYPE_MAPPING))
		return size >= 0 && XDeviceFile_legacyResize(fd, size);
	if (!ctx || size < 0) {
		if (ctx) ctx->m_lastError = (int16_t)XDeviceError_InvalidArgument;
		return false;
	}
	return XDevice_dispatchResize(ctx->m_device, ctx, size);
}

bool XDevice_setProperty(XFd fd, XDeviceProperty property, const XVariant* value)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	if (!ctx || !value) {
		if (ctx) ctx->m_lastError = (int16_t)XDeviceError_InvalidArgument;
		return false;
	}
	return XDevice_dispatchSetProperty(ctx->m_device, ctx, (uint32_t)property, value);
}

bool XDevice_getProperty(XFd fd, XDeviceProperty property, XVariant* value)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	if (!ctx || !value) {
		if (ctx) ctx->m_lastError = (int16_t)XDeviceError_InvalidArgument;
		return false;
	}
	if (XDevice_dispatchGetProperty(ctx->m_device, ctx, (uint32_t)property, value))
		return true;
	if (property == XDeviceProperty_LastError) {
		XVariant_setValue_int(value, (int)ctx->m_lastError);
		return true;
	}
	if (property == XDeviceProperty_State) {
		XVariant_setValue_int(value, (int)ctx->m_state);
		return true;
	}
	return false;
}

bool XDevice_queryProperty(XFd fd, XDeviceProperty property, XVariant* value)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	if (!ctx || !value) {
		if (ctx) ctx->m_lastError = (int16_t)XDeviceError_InvalidArgument;
		return false;
	}
	if (XDevice_dispatchQueryProperty(ctx->m_device, ctx, (uint32_t)property, value))
		return true;
	if (property == XDeviceProperty_LastError) {
		XVariant_setValue_int(value, (int)ctx->m_lastError);
		return true;
	}
	if (property == XDeviceProperty_State) {
		XVariant_setValue_int(value, (int)ctx->m_state);
		return true;
	}
	return false;
}

bool XDevice_control(XFd fd, uint32_t command, const XVarList* in, XVarList* out)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	if (!ctx) {
		XFileDescriptor* desc = XFd_get(fd);
		XVarList* input = (XVarList*)in;
		bool enabled;
		int64_t offset;
		int64_t size;
		int flags;
		void* address;
		if (!desc) return false;
		if (command == XDeviceFileCommand_SetStandardInputEcho) {
			if (((XFdType)desc->m_type != XFD_TYPE_CONSOLE &&
				(XFdType)desc->m_type != XFD_TYPE_FILE) || !input || out ||
				input->m_size != sizeof(enabled))
				return false;
			XVarList_start(input);
			enabled = XVarList_arg(input, bool);
			return XDeviceFile_legacySetStandardInputEcho(fd, enabled);
		}
		/* 命名共享内存仍由平台创建信令 XFd；其数据区映射作为文件命令转发。 */
		if ((XFdType)desc->m_type != XFD_TYPE_FILE &&
			(XFdType)desc->m_type != XFD_TYPE_MAPPING)
			return false;
		if (command == XDeviceFileCommand_Map) {
			if (!input || !out ||
				input->m_size != sizeof(offset) + sizeof(size) + sizeof(flags) ||
				out->m_size != sizeof(address))
				return false;
			XVarList_start(input);
			offset = XVarList_arg(input, int64_t);
			size = XVarList_arg(input, int64_t);
			flags = XVarList_arg(input, int);
			if (offset < 0 || size <= 0 || offset > INT64_MAX - size)
				return false;
			address = XDeviceFile_legacyMap(fd, offset, size, flags);
			if (!address) return false;
			memcpy(out->data, &address, sizeof(address));
			XVarList_start(out);
			return true;
		}
		if (command == XDeviceFileCommand_Unmap) {
			if (!input || out ||
				input->m_size != sizeof(address) + sizeof(size))
				return false;
			XVarList_start(input);
			address = XVarList_arg(input, void*);
			size = XVarList_arg(input, int64_t);
			return size > 0 && XDeviceFile_legacyUnmap(address, size);
		}
		return false;
	}
	return XDevice_dispatchControl(ctx->m_device, ctx, command, in, out);
}

int XDevice_lastError(XFd fd)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	return ctx ? (int)ctx->m_lastError : (int)XDeviceError_InvalidDescriptor;
}

XDevice* XDevice_class(XFd fd)
{
	XDeviceContext* ctx = XDevice_getCtx(fd);
	return ctx ? ctx->m_device : NULL;
}

XDeviceContext* XDevice_handle(XFd fd)
{
	XFileDescriptor* descriptor = XFd_get(fd);
	if (!descriptor || (XFdType)descriptor->m_type != XFD_TYPE_CLASS)
		return NULL;
	return (XDeviceContext*)descriptor->m_deviceCtx;
}
