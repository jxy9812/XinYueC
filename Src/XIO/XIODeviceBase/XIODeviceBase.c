#include "XIODeviceBase.h"
#include "XMemory.h"
#include<string.h>
XIODeviceBase* XIODeviceBase_new(XVtable* vtable)
{
	/*if (port == NULL)
		return NULL;*/
	XIODeviceBase* io= XMemory_malloc(sizeof(XIODeviceBase));
	if (io == NULL)
		return io;
	XIODeviceBase_init(io, vtable);
	return io;
}
void XIODeviceBase_init(XIODeviceBase* io, XVtable* vtable)
{
	if (ISNULL(io, ""))
		return;
	//开始初始化
	memset(io, 0, sizeof(XIODeviceBase));
	XClass_init(io);
	XIODeviceBase_class_init();
	if (vtable == NULL)
		XClassGetVtable(io) = XIODeviceBase_class_init();
	else
		XClassGetVtable(io) = vtable;
}
void XIODeviceBase_setWriteBuffer_base(XIODeviceBase* io, size_t count)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return;
	XClassGetVirtualFunc(io, EXIODeviceBase_SetWriteBuffer, void(*)(XIODeviceBase*, size_t))(io,count);
}
void XIODeviceBase_setReadBuffer_base(XIODeviceBase* io, size_t count)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return;
	XClassGetVirtualFunc(io, EXIODeviceBase_SetReadBuffer, void(*)(XIODeviceBase*, size_t))(io, count);
}
void XIODeviceBase_setDevice_base(XIODeviceBase* io, void* device)
{
	if (ISNULL(io, "")|| ISNULL(device, "") || ISNULL(XClassGetVtable(io), ""))
		return;
	XClassGetVirtualFunc(io, EXIODeviceBase_SetDevice, void(*)(XIODeviceBase*, void*))(io,device);
}
size_t XIODeviceBase_write_base(XIODeviceBase* io, const char* data, size_t maxSize)
{
	if (ISNULL(io, "") || ISNULL(data, "") || ISNULL(maxSize, "") || ISNULL(XClassGetVtable(io), ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODeviceBase_Write, size_t(*)(XIODeviceBase*, const char*, size_t))(io,data,maxSize);
}

size_t XIODeviceBase_read_base(XIODeviceBase* io, char* data, size_t maxSize)
{
	if (ISNULL(io, "") || ISNULL(data, "") || ISNULL(maxSize, "") || ISNULL(XClassGetVtable(io), ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODeviceBase_Read, size_t(*)(XIODeviceBase*, char*, size_t))(io, data, maxSize);
}


size_t XIODeviceBase_getBytesAvailable_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODeviceBase_GetBytesAvailable, size_t(*)(XIODeviceBase*))(io);
}

size_t XIODeviceBase_getBytesToWrite_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODeviceBase_GetBytesToWrite, size_t(*)(XIODeviceBase*))(io);
}

bool XIODeviceBase_atEnd_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return false;
	return XClassGetVirtualFunc(io, EXIODeviceBase_AtEnd, bool(*)(XIODeviceBase*))(io);
}

bool XIODeviceBase_isOpen(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return false;
	return io->m_mode != XIODeviceBase_NotOpen;
}

bool XIODeviceBase_open_base(XIODeviceBase* io, XIODeviceBaseMode mode)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return false;
	return XClassGetVirtualFunc(io, EXIODeviceBase_Open, bool(*)(XIODeviceBase*, XIODeviceBaseMode))(io, mode);
}

bool XIODeviceBase_close_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return false ;
	return XClassGetVirtualFunc(io, EXIODeviceBase_Close, bool(*)(XIODeviceBase*))(io);
}

void XIODeviceBase_poll_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return ;
	XClassGetVirtualFunc(io, EXIODeviceBase_Poll, void(*)(XIODeviceBase*))(io);
}

size_t XIODeviceBase_writeFull_base(XIODeviceBase* io)
{
	if (ISNULL(io, "") || ISNULL(XClassGetVtable(io), ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODeviceBase_WriteFull, bool(*)(XIODeviceBase*))(io);
}
