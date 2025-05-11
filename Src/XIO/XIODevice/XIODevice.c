#include "XIODevice.h"
#include "XMemory.h"
#include "XVector.h"
#include <string.h>
#include <assert.h>
XIODevice* XIODevice_new(XIODevice_PortFunc* port)
{
	if (port == NULL)
		return NULL;
	XIODevice* io= XMemory_malloc(sizeof(XIODevice));
	if (io == NULL)
		return;
	//开始初始化
	memset(io, 0, sizeof(XIODevice)- sizeof(XIODevice_PortFunc));
	//绑定函数指针
	memcpy(&(io->m_port), port, sizeof(XIODevice_PortFunc));
	return io;
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
void XIODevice_setWriteBuffer(XIODevice* io, size_t size)
{
	if (io == NULL)
		return;
	if (size != 0)
	{
		if (io->m_writeBuffer == NULL)
			io->m_writeBuffer=XVector_New(char);
		assert(io->m_writeBuffer);
		XVector_resize(io->m_writeBuffer, size);
	}
	else if(io->m_writeBuffer!=NULL)
	{
		XVector_free(io->m_writeBuffer);
		io->m_writeBuffer = NULL;
	}
}
void XIODevice_setReadBuffer(XIODevice* io, size_t size)
{
	if (io == NULL)
		return;
	if (size != 0)
	{
		if (io->m_readBuffer == NULL)
			io->m_readBuffer = XVector_New(char);
		assert(io->m_readBuffer);
		XVector_resize(io->m_readBuffer, size);
	}
	else if (io->m_readBuffer != NULL)
	{
		XVector_free(io->m_readBuffer);
		io->m_readBuffer = NULL;
	}
}
size_t XIODevice_write(XIODevice* io, const char* data, size_t maxSize)
{
	if(io==NULL||data==NULL||maxSize==0||io->m_mode& XIODeviceBase_WriteOnly==0)
		return 0;
	if (io->m_port.writeBufferFull_funcPointer == NULL)
		return 0;
	size_t count=0;
	if (io->m_writeBuffer == NULL)
	{//没有写入缓冲区
		count+=io->m_port.writeBufferFull_funcPointer(io, data, maxSize);
	}
	else
	{//如果有缓冲区
		//count = maxSize;
		XVector* buff = io->m_writeBuffer;
		size_t marginSize = maxSize;//剩余大小
		while (marginSize)
		{
			size_t marginBuffSize = XContainerCapacity(buff) - XContainerSize(buff);//缓冲区剩余大小
			if (marginBuffSize >= marginSize)
			{//如果缓冲区可以放下全部数据
				XVector_append_array(buff, data[maxSize- marginSize], marginSize);
				marginSize = 0;
			}
			else 
			{//缓冲区不够
				XVector_append_array(buff, data[maxSize - marginSize], marginBuffSize);
				marginSize -= marginBuffSize;
			}
			if (XContainerCapacity(buff) == XContainerSize(buff)&& io->m_port.writeBufferFull_funcPointer!=NULL)
			{//缓冲区已满，清空缓冲区
				char* buffData = XContainerDataPtr(buff);
				size_t len=io->m_port.writeBufferFull_funcPointer(io, buffData, XContainerSize(buff));
				count +=len;
				if (len != XContainerSize(buff))
				{//缓冲区满后写入设备出错
					break;
				}
				XContainerSize(buff) = 0;
			}
		}
	}
	return count;
}

size_t XIODevice_writeVector(XIODevice* io, XVector* array)
{
	return XIODevice_write(io, XContainerDataPtr(array), XContainerSize(array));
}

size_t XIODevice_read(XIODevice* io, char* data, size_t maxSize)
{
	if (io == NULL || data == NULL || maxSize == 0  || io->m_mode & XIODeviceBase_ReadOnly == 0)
		return 0;
	if (io->m_port.readBufferEmpty_funcPointer == NULL)
			return 0;
	size_t count = 0;
	if (io->m_readBuffer == NULL)
	{//没有读取缓冲区
		count += io->m_port.readBufferEmpty_funcPointer(io, data, maxSize);
	}
	else
	{//如果有缓冲区
		XVector* buff = io->m_readBuffer;
		size_t marginSize = maxSize;//剩余大小
		size_t buffSize = 0;//缓冲区中的数据量
		char* buffData = NULL;
		while (marginSize)
		{
			buffSize = XContainerSize(buff);
			buffData = XContainerDataPtr(buff);
			if (buffSize >= marginSize)
			{//缓冲区中的数据量大于等于要读取的数据量
				memcpy(data[maxSize - marginSize], buffData, marginSize);
				for (size_t i = 0; i < buffSize - marginSize; i++)
				{//缓冲区剩余数据往前挪
					buffData[i] = buffData[marginSize + i];
				}
				XContainerSize(buff) = buffSize - marginSize;//修改数据个数
				count += marginSize;
				marginSize = 0;
			}
			else if(buffSize>0)
			{//如果小于不够
				memcpy(data[maxSize - marginSize], buffData, buffSize);
				XContainerSize(buff) = 0;
				count += buffSize;
				marginSize -= buffSize;
				//数据为空判断是否有读取缓冲区空的方法
				if (io->m_port.readBufferEmpty_funcPointer)
				{
					size_t readSize = 0;
					if(XContainerCapacity(buff)> marginSize)
					{
						readSize = io->m_port.readBufferEmpty_funcPointer(io, buffData, marginSize);
					}
					else
					{
						readSize = io->m_port.readBufferEmpty_funcPointer(io, buffData, XContainerCapacity(buff));
					}
					XContainerSize(buff) += readSize;
				}
			}
			else
			{
				break;//无法在获得更多数据
			}
		}
	}
	return count;

}

XVector* XIODevice_readVector(XIODevice* io, size_t maxSize)
{
	if (io == NULL)
		return NULL;
	XVector* v = XVector_New(char);	
	if (io->m_readBuffer == NULL)
	{//没有读取缓冲区
		for (size_t i = 0; i < maxSize; i++)
		{
			char byte;
			if (io->m_port.readBufferEmpty_funcPointer(io, &byte,1))
				XVector_push_back(v,&byte);
		}
	}
	else if(maxSize!=0)
	{
		XVector_resize(v, maxSize);
		size_t readSize=XIODevice_read(io, XContainerDataPtr(v),maxSize);
		if(readSize!= maxSize)
			XVector_resize(v, readSize);
	}
	return v;
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
	return io->m_port.open_funcPointer(io,mode);
}

void XIODevice_close(XIODevice* io)
{
	if (io == NULL || io->m_port.close_funcPointer == NULL)
		return ;
	io->m_port.close_funcPointer(io);
}

size_t XIODevice_writeFull(XIODevice* io)
{
	if(io!=NULL||io->m_writeBuffer!=NULL)
		return io->m_port.writeBufferFull_funcPointer(io, XContainerDataPtr(io->m_writeBuffer), XContainerSize(io->m_writeBuffer));
	return 0;
}
