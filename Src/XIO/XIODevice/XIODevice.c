#include "XIODevice.h"
#include "XMemory.h"
#include"XCircularQueueAtomic.h"
#include <string.h>
#include <assert.h>
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
	if (io&&port)
	{
		//开始初始化
		memset(io, 0, sizeof(XIODevice) - sizeof(XIODevice_PortFunc));
		//绑定函数指针
		memcpy(&(io->m_port), port, sizeof(XIODevice_PortFunc));
	}
}
void XIODevice_free(XIODevice* io)
{
	if (io == NULL)
		return;
	if (io->m_port.close_funcPointer)
		io->m_port.close_funcPointer(io);
	if(io->m_writeBuffer)
		XVector_free(io->m_writeBuffer);
	if (io->m_readBuffer)
		XVector_free(io->m_readBuffer);
	XMemory_free(io);
}
void XIODevice_setWriteBuffer(XIODevice* io, size_t count)
{
	if (io == NULL)
		return;
	if (count != 0)
	{
		if (io->m_writeBuffer == NULL)
			io->m_writeBuffer= XCircularQueueAtomic_New(char,count);
		assert(io->m_writeBuffer);
	}
	else if(io->m_writeBuffer!=NULL)
	{
		XCircularQueueAtomic_free(io->m_writeBuffer);
		io->m_writeBuffer = NULL;
	}
}
void XIODevice_setReadBuffer(XIODevice* io, size_t count)
{
	if (io == NULL)
		return;
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
void XIODevice_setDevice(XIODevice* io, void* device)
{
	if (io != NULL)
		io->device = device;
}
size_t XIODevice_write(XIODevice* io, const char* data, size_t maxSize)
{
	if(io==NULL||data==NULL||maxSize==0||io->m_mode& XIODeviceBase_WriteOnly==0)
		return 0;
	if (io->m_port.writeData_funcPointer == NULL)
		return 0;
	size_t count=0;
	if (io->m_writeBuffer == NULL)
	{//没有写入缓冲区
		count+=io->m_port.writeData_funcPointer(io, data, maxSize);
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
			if(XCircularQueue_isFull(io->m_writeBuffer))
				io->m_port.writeBufferFull_funcPointer(io);
			if (count >= maxSize)
				break;
		}while(!XCircularQueue_isFull(io->m_writeBuffer));
	}
	return count;
}

size_t XIODevice_read(XIODevice* io, char* data, size_t maxSize)
{
	if (io == NULL || data == NULL || maxSize == 0  || io->m_mode & XIODeviceBase_ReadOnly == 0)
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
			if (XCircularQueue_isFull(io->m_readBuffer))
				io->m_port.readBufferEmpty_funcPointer(io);
			if (count >= maxSize)
				break;
			
		} while (!XCircularQueue_isEmpty(io->m_readBuffer));
		
	}
	return count;

}


void XIODevice_receive(XIODevice* io, char* data, size_t size)
{
	if (io != NULL&&io->m_readBuffer!=NULL)
	{
		for (size_t i = 0; i < size; i++)
		{
			XCircularQueue_push(io->m_readBuffer, data + size);
		}
	}
}

bool XIODevice_isOpen(XIODevice* io)
{
	if(io==NULL)
		return false;
	return (io->m_mode) == XIODeviceBase_NotOpen;
}

bool XIODevice_open(XIODevice* io, XIODeviceBase mode)
{
	if(io==NULL||io->m_port.open_funcPointer==NULL)
		return false;
	if (io->m_port.open_funcPointer(io, mode))
	{
		io->m_mode = mode;
		return true;
	}
	return false;
}

void XIODevice_close(XIODevice* io)
{
	if (io == NULL || io->m_port.close_funcPointer == NULL)
		return ;
	if (io->m_mode != XIODeviceBase_NotOpen)
	{
		io->m_port.close_funcPointer(io);
		io->m_mode = XIODeviceBase_NotOpen;
	}
}

void XIODevice_poll(XIODevice* io)
{
	if (io&&io->m_port.poll_funcPointer)
	{
		io->m_port.poll_funcPointer(io);
	}
}

size_t XIODevice_writeFull(XIODevice* io)
{
	size_t count = 0;
	if (io != NULL && io->m_writeBuffer != NULL)
	{
		if (!XCircularQueue_isEmpty(io->m_writeBuffer))
		{
			count = XCircularQueue_size(io->m_writeBuffer);
			io->m_port.writeBufferFull_funcPointer(io);
			count-= XCircularQueue_size(io->m_writeBuffer);
		}
	}
	return count;
}
