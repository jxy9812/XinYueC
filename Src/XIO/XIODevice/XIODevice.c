#include "XIODevice.h"
#include<string.h>
XIODevice* XIODevice_new(XIODevice_PortFuncInit* port)
{
	if (port == NULL)
		return NULL;
	XIODevice* io= XMemory_malloc(sizeof(XIODevice));
	if (io == NULL)
		return io;
	XIODevice_init(io,port);
	return io;
}
void XIODevice_init(XIODevice* io, XIODevice_PortFuncInit* port)
{
	if (ISNULL(io, "") || ISNULL(port, ""))
		return;
	//开始初始化
	memset(io, 0, sizeof(XIODevice) - sizeof(XIODevice_PortFunc));
	//绑定函数指针
	memcpy(&(io->m_port), port, sizeof(XIODevice_PortFunc));

	XIODevice_class_init();
	XClassGetVtable(io) = XIODeviceVtable;
}
void XIODevice_free(XIODevice* io)
{
	if (ISNULL(io, "") )
		return;
	XClassGetVirtualFunc(io, EXIODevice_Free,void(*)(XIODevice*))(io);
}
void XIODevice_setWriteBuffer(XIODevice* io, size_t count)
{
	if (ISNULL(io, ""))
		return;
	XClassGetVirtualFunc(io, EXIODevice_SetWriteBuffer, void(*)(XIODevice*, size_t))(io,count);
}
void XIODevice_setReadBuffer(XIODevice* io, size_t count)
{
	if (ISNULL(io, ""))
		return;
	XClassGetVirtualFunc(io, EXIODevice_SetReadBuffer, void(*)(XIODevice*, size_t))(io, count);
}
void XIODevice_setDevice(XIODevice* io, void* device)
{
	if (ISNULL(io, "")|| ISNULL(device, ""))
		return;
	XClassGetVirtualFunc(io, EXIODevice_SetDevice, void(*)(XIODevice*, void*))(io,device);
}
size_t XIODevice_write(XIODevice* io, const char* data, size_t maxSize)
{
	if (ISNULL(io, "") || ISNULL(data, "") || ISNULL(maxSize, ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODevice_Write, bool(*)(XIODevice*, const char*, size_t))(io,data,maxSize);
}

size_t XIODevice_read(XIODevice* io, char* data, size_t maxSize)
{
	if (ISNULL(io, "") || ISNULL(data, "") || ISNULL(maxSize, ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODevice_Read, bool(*)(XIODevice*, char*, size_t))(io, data, maxSize);
}


void XIODevice_receive(XIODevice* io,const char* data, size_t size)
{
	if (ISNULL(io, "") || ISNULL(data, "") || ISNULL(size, ""))
		return;
	return XClassGetVirtualFunc(io, EXIODevice_Receive, bool(*)(XIODevice*,const char*, size_t))(io, data, size);
}

bool XIODevice_isOpen(XIODevice* io)
{
	if (ISNULL(io, ""))
		return false;
	return XClassGetVirtualFunc(io, EXIODevice_IsOpen, bool(*)(XIODevice*))(io);
}

bool XIODevice_open(XIODevice* io, XIODeviceBase mode)
{
	if (ISNULL(io, ""))
		return false;
	return XClassGetVirtualFunc(io, EXIODevice_Open, bool(*)(XIODevice*, XIODeviceBase))(io, mode);
}

void XIODevice_close(XIODevice* io)
{
	if (ISNULL(io, ""))
		return ;
	XClassGetVirtualFunc(io, EXIODevice_Close, void(*)(XIODevice*))(io);
}

void XIODevice_poll(XIODevice* io)
{
	if (ISNULL(io, ""))
		return ;
	XClassGetVirtualFunc(io, EXIODevice_Poll, void(*)(XIODevice*))(io);
}

size_t XIODevice_writeFull(XIODevice* io)
{
	if (ISNULL(io, ""))
		return 0;
	return XClassGetVirtualFunc(io, EXIODevice_WriteFull, bool(*)(XIODevice*))(io);
}
