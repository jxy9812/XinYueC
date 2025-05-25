#include"XIODevice.h"
#include"XCircularQueue.h"
#include"XCircularQueueAtomic.h"
#include "XMemory.h"
#include <string.h>
#include <assert.h>
//声明 
static void VXIODevice_free(XIODevice* io);
static bool VXIODevice_isOpen(XIODevice* io);
static bool VXIODevice_open(XIODevice* io, XIODeviceBase mode);
static size_t VXIODevice_write(XIODevice* io, const char* data, size_t maxSize);//写入
static size_t VXIODevice_writeFull(XIODevice* io);//将剩余的数据刷入设备
static size_t VXIODevice_read(XIODevice* io, char* data, size_t maxSize);//读取
static size_t VXIODevice_receive(XIODevice* io, const char* data, size_t size);//接收数据从硬件接收数据到缓冲区
static void VXIODevice_close(XIODevice* io);
static void VXIODevice_poll(XIODevice* io);
static void VXIODevice_setWriteBuffer(XIODevice* io, size_t count);
static void VXIODevice_setReadBuffer(XIODevice* io, size_t count);
static void VXIODevice_setDevice(XIODevice* io, void* device);
XVtable* XIODeviceVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XIODEVICE_VTABLE_SIZE];//虚函数数据
#endif

void XIODevice_class_init()
{
	//仅初始化一次
	if (XIODeviceVtable)
		return;
#if !VTABLE_ISSTACK
	XIODeviceVtable = XVtable_new();
#else
	XIODeviceVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#endif
	void* table[] = { VXIODevice_free,VXIODevice_isOpen,VXIODevice_open,VXIODevice_write,VXIODevice_writeFull,VXIODevice_read,VXIODevice_receive,VXIODevice_close, VXIODevice_poll,VXIODevice_setWriteBuffer,VXIODevice_setReadBuffer,VXIODevice_setDevice };
	XVtable_append_array(XIODeviceVtable, table, sizeof(table) / sizeof(table[0]));
#if SHOWCONTAINERSIZE
	printf("XIODevice size:%d\n", XVtable_size(XIODeviceVtable));
#endif
}

void VXIODevice_free(XIODevice* io)
{
	VXIODevice_close(io);
	if (io->m_writeBuffer)
		XCircularQueue_free(io->m_writeBuffer);
	if (io->m_readBuffer)
		XCircularQueue_free(io->m_readBuffer);
	XMemory_free(io);
}

bool VXIODevice_isOpen(XIODevice* io)
{
	return io->m_mode!= XIODeviceBase_NotOpen;
}

bool VXIODevice_open(XIODevice* io, XIODeviceBase mode)
{
	if (io->m_port.open_funcPointer == NULL)
		return false;
	if (io->m_port.open_funcPointer(io, mode))
	{
		io->m_mode = mode;
		return true;
	}
	return false;
}

size_t VXIODevice_write(XIODevice* io, const char* data, size_t maxSize)
{
	if (io->m_mode & XIODeviceBase_WriteOnly == 0)
		return 0;
	if (io->m_port.writeData_funcPointer == NULL)
		return 0;
	//printf("x");
	size_t count = 0;
	if (io->m_writeBuffer == NULL)
	{//没有写入缓冲区
		count += io->m_port.writeData_funcPointer(io, data, maxSize);
	}
	else
	{
		do
		{
			while (XCircularQueue_push(io->m_writeBuffer, data + count))
			{
				++count;
				if (count >= maxSize)
					break;
			}
			if (XCircularQueue_isFull(io->m_writeBuffer))
				io->m_port.writeBufferFull_funcPointer(io,io->m_writeBuffer);
			if (count >= maxSize)
				break;
		} while (!XCircularQueue_isFull(io->m_writeBuffer));
	}
	return count;
}

size_t VXIODevice_writeFull(XIODevice* io)
{
	size_t count = 0;
	if (io->m_writeBuffer != NULL)
	{
		if (!XCircularQueue_isEmpty(io->m_writeBuffer))
		{
			count = XCircularQueue_size(io->m_writeBuffer);
			io->m_port.writeBufferFull_funcPointer(io,io->m_writeBuffer);
			count -= XCircularQueue_size(io->m_writeBuffer);
		}
	}
	return count;
}

size_t VXIODevice_read(XIODevice* io, char* data, size_t maxSize)
{
	if (io->m_mode & XIODeviceBase_ReadOnly == 0)
		return 0;
	if (io->m_port.readData_funcPointer == NULL)
		return 0;
	size_t count = 0;
	if (io->m_readBuffer == NULL)
	{//没有读取缓冲区
		count += io->m_port.readData_funcPointer(io, data, maxSize);
	}
	else
	{
		do
		{
			while (XCircularQueue_receive(io->m_readBuffer, data + count))
			{
				++count;
				if (count >= maxSize)
					break;
			}
			if (XCircularQueue_isEmpty(io->m_readBuffer))
				io->m_port.readBufferEmpty_funcPointer(io,io->m_readBuffer);
			if (count >= maxSize)
				break;

		} while (!XCircularQueue_isEmpty(io->m_readBuffer));

	}
	return count;
}

size_t VXIODevice_receive(XIODevice* io, const char* data, size_t size)
{
	size_t count = 0;
	if (io->m_readBuffer != NULL)
	{
		for (size_t i = 0; i < size; i++)
		{
			if (XCircularQueue_push(io->m_readBuffer, data + i))
				++count;
		}
	}
	return count;
}

void VXIODevice_close(XIODevice* io)
{
	if (VXIODevice_isOpen(io))
	{
		if (io->m_port.close_funcPointer)
			io->m_port.close_funcPointer(io);
	}
	io->m_mode = XIODeviceBase_NotOpen;
}

void VXIODevice_poll(XIODevice* io)
{
	if (io->m_port.poll_funcPointer)
	{
		io->m_port.poll_funcPointer(io);
	}
}

void VXIODevice_setWriteBuffer(XIODevice* io, size_t count)
{
	if (count != 0)
	{
		if (io->m_writeBuffer == NULL)
			io->m_writeBuffer = XCircularQueueAtomic_New(char, count);
		assert(io->m_writeBuffer);
	}
	else if (io->m_writeBuffer != NULL)
	{
		XCircularQueueAtomic_free(io->m_writeBuffer);
		io->m_writeBuffer = NULL;
	}
}

void VXIODevice_setReadBuffer(XIODevice* io, size_t count)
{
	if (count != 0)
	{
		if (io->m_readBuffer == NULL)
			io->m_readBuffer = XCircularQueueAtomic_New(char, count);
		assert(io->m_readBuffer);
	}
	else if (io->m_readBuffer != NULL)
	{
		XCircularQueueAtomic_free(io->m_readBuffer);
		io->m_readBuffer = NULL;
	}
}

void VXIODevice_setDevice(XIODevice* io, void* device)
{
	io->device = device;
}
