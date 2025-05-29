#include "XIODevice.h"
#include "XMemory.h"
#include<string.h>
XIODeviceBase* XIODevice_new(XVtable* vtable)
{
	/*if (port == NULL)
		return NULL;*/
	XIODeviceBase* io= XMemory_malloc(sizeof(XIODeviceBase));
	if (io == NULL)
		return io;
	XIODevice_init(io, vtable);
	return io;
}
void XIODevice_init(XIODeviceBase* io, XVtable* vtable)
{
	if (ISNULL(io, ""))
		return;
	//开始初始化
	memset(io, 0, sizeof(XIODeviceBase));

	XIODevice_class_init();
	if (vtable == NULL)
		XClassGetVtable(io) = XIODeviceVtable;
	else
		XClassGetVtable(io) = vtable;
}
void XIODevice_free_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return;
	XClassGetVirtualFunc(io, EXIODeviceBase_Free,void(*)(XIODeviceBase*))(io);
}
void XIODevice_setWriteBuffer_base(XIODeviceBase* io, size_t count)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return;
	XClassGetVirtualFunc(io, EXIODeviceBase_SetWriteBuffer, void(*)(XIODeviceBase*, size_t))(io,count);
}
void XIODevice_setReadBuffer_base(XIODeviceBase* io, size_t count)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return;
	XClassGetVirtualFunc(io, EXIODeviceBase_SetReadBuffer, void(*)(XIODeviceBase*, size_t))(io, count);
}
void XIODevice_setDevice_base(XIODeviceBase* io, void* device)
{
	if (ISNULL(io, "")|| ISNULL(device, "") || ISNULL(XClassGetVtable(io), ""))
		return;
	XClassGetVirtualFunc(io, EXIODeviceBase_SetDevice, void(*)(XIODeviceBase*, void*))(io,device);
}
size_t XIODevice_write_base(XIODeviceBase* io, const char* data, size_t maxSize)
{
	if (ISNULL(io, "") || ISNULL(data, "") || ISNULL(maxSize, "") || ISNULL(XClassGetVtable(io), ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODeviceBase_Write, size_t(*)(XIODeviceBase*, const char*, size_t))(io,data,maxSize);
}

size_t XIODevice_read_base(XIODeviceBase* io, char* data, size_t maxSize)
{
	if (ISNULL(io, "") || ISNULL(data, "") || ISNULL(maxSize, "") || ISNULL(XClassGetVtable(io), ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODeviceBase_Read, size_t(*)(XIODeviceBase*, char*, size_t))(io, data, maxSize);
}


size_t XIODevice_receive_base(XIODeviceBase* io,const char* data, size_t size)
{
	if (ISNULL(io, "") || ISNULL(data, "") || ISNULL(size, "") || ISNULL(XClassGetVtable(io), ""))
		return;
	return XClassGetVirtualFunc(io, EXIODeviceBase_Receive, size_t(*)(XIODeviceBase*,const char*, size_t))(io, data, size);
}

bool XIODevice_isOpen_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return false;
	return XClassGetVirtualFunc(io, EXIODeviceBase_IsOpen, bool(*)(XIODeviceBase*))(io);
}

bool XIODevice_open_base(XIODeviceBase* io, XIODeviceBaseMode mode)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return false;
	return XClassGetVirtualFunc(io, EXIODeviceBase_Open, bool(*)(XIODeviceBase*, XIODeviceBaseMode))(io, mode);
}

void XIODevice_close_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return ;
	XClassGetVirtualFunc(io, EXIODeviceBase_Close, void(*)(XIODeviceBase*))(io);
}

void XIODevice_poll_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return ;
	XClassGetVirtualFunc(io, EXIODeviceBase_Poll, void(*)(XIODeviceBase*))(io);
}

size_t XIODevice_writeFull_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODeviceBase_WriteFull, bool(*)(XIODeviceBase*))(io);
}
