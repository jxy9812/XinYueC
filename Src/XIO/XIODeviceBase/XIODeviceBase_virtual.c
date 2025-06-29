#include"XIODeviceBase.h"
#include"XCircularQueue.h"
#include"XCircularQueueAtomic.h"
#include "XMemory.h"
#include <string.h>
#include <assert.h>
//声明 
static void VXIODevice_delete(XIODeviceBase* io);
static bool VXIODevice_open(XIODeviceBase* io, XIODeviceBaseMode mode);
static size_t VXIODevice_write(XIODeviceBase* io, const char* data, size_t maxSize);//写入
static size_t VXIODevice_writeFull(XIODeviceBase* io);//将剩余的数据刷入设备
static size_t VXIODevice_read(XIODeviceBase* io, char* data, size_t maxSize);//读取
static size_t VXIODevice_getBytesAvailable(XIODeviceBase* io);
static size_t VXIODeviceBase_getBytesToWrite(XIODeviceBase* io);
static bool VXIODeviceBase_atEnd(XIODeviceBase* io);
static bool VXIODevice_close(XIODeviceBase* io);
static void VXIODevice_setWriteBuffer(XIODeviceBase* io, size_t count);
static void VXIODevice_setReadBuffer(XIODeviceBase* io, size_t count);
static void VXIODevice_setDevice(XIODeviceBase* io, void* device);

XVtable* XIODeviceBase_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XIODeviceBase))
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XObject_class_init());
	void* table[] = {
		VXIODevice_open,VXIODevice_write,
		VXIODevice_writeFull,VXIODevice_read,
		VXIODevice_getBytesAvailable,VXIODeviceBase_getBytesToWrite,
		VXIODeviceBase_atEnd,VXIODevice_close,
	    VXIODevice_setWriteBuffer,
		VXIODevice_setReadBuffer,VXIODevice_setDevice 
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Delete, VXIODevice_delete);
#if SHOWCONTAINERSIZE
	printf("XIODeviceBase size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
void VXIODevice_delete(XIODeviceBase* io)
{
	XIODeviceBase_close_base(io);
	if (io->m_writeBuffer)
		XCircularQueue_delete_base(io->m_writeBuffer);
	if (io->m_readBuffer)
		XCircularQueue_delete_base(io->m_readBuffer);
	XMemory_free(io);
}

bool VXIODevice_open(XIODeviceBase* io, XIODeviceBaseMode mode)
{
	/**************************************************/
	ISNULL(0, "请重载这个函数,这是模板");
	/**************************************************/
	//if (io->m_port.open_funcPointer == NULL)
	//	return false;
	//if (io->m_port.open_funcPointer(io, mode))
	//{
	//	io->m_mode = mode;
	//	return true;
	//}
	return false;
}

size_t VXIODevice_write(XIODeviceBase* io, const char* data, size_t maxSize)
{
	if (io->m_mode & XIODeviceBase_WriteOnly == 0)
		return 0;
	size_t count = 0;
	if (io->m_writeBuffer == NULL)
	{//没有写入缓冲区
		return count;
	}
	else
	{
		do
		{
			while (XQueueBase_push_base(io->m_writeBuffer, data + count))
			{
				++count;
				if (count >= maxSize)
					break;
			}
			if (XQueueBase_isFull_base(io->m_writeBuffer))
				XIODeviceBase_writeFull_base(io);
			if (count >= maxSize)
				break;
		} while (!XQueueBase_isFull_base(io->m_writeBuffer));
	}
	return count;
}

size_t VXIODevice_writeFull(XIODeviceBase* io)
{
	size_t count = 0;
	/**************************************************/
	ISNULL(0, "请重载这个函数,这是模板");
	/**************************************************/
	/*if (io->m_writeBuffer != NULL)
	{
		if (!XCircularQueue_isEmpty_base(io->m_writeBuffer))
		{
			count = XCircularQueue_getSize_base(io->m_writeBuffer);
			io->m_port.writeBufferFull_funcPointer(io,io->m_writeBuffer);
			count -= XCircularQueue_getSize_base(io->m_writeBuffer);
		}
	}*/
	return count;
}

size_t VXIODevice_read(XIODeviceBase* io, char* data, size_t maxSize)
{
	if (io->m_mode & XIODeviceBase_ReadOnly == 0)
		return 0;
	size_t count = 0;
	if (io->m_readBuffer == NULL)
		return 0;
	while (count < maxSize)
	{
		if (XQueueBase_receive_base(io->m_readBuffer, data + count))
			++count;
		else
			break;
	}
	return count;
}

size_t VXIODevice_getBytesAvailable(XIODeviceBase* io)
{
	if (io->m_readBuffer == NULL)
		return 0;
	return XQueueBase_getSize_base(io->m_readBuffer);
}

size_t VXIODeviceBase_getBytesToWrite(XIODeviceBase* io)
{
	if (io->m_writeBuffer == NULL)
		return 0;
	return XQueueBase_getSize_base(io->m_writeBuffer);
}

bool VXIODeviceBase_atEnd(XIODeviceBase* io)
{
	return false;
}

bool VXIODevice_close(XIODeviceBase* io)
{
	XIODeviceBase_writeFull_base(io);
	return true;
}


void VXIODevice_setWriteBuffer(XIODeviceBase* io, size_t count)
{
	if (count != 0)
	{
		if (io->m_writeBuffer == NULL)
			io->m_writeBuffer = XCircularQueueAtomic_Create(char, count);
		assert(io->m_writeBuffer);
	}
	else if (io->m_writeBuffer != NULL)
	{
		XCircularQueueAtomic_delete_base(io->m_writeBuffer);
		io->m_writeBuffer = NULL;
	}
}

void VXIODevice_setReadBuffer(XIODeviceBase* io, size_t count)
{
	if (count != 0)
	{
		if (io->m_readBuffer == NULL)
			io->m_readBuffer = XCircularQueueAtomic_Create(char, count);
		assert(io->m_readBuffer);
	}
	else if (io->m_readBuffer != NULL)
	{
		XCircularQueueAtomic_delete_base(io->m_readBuffer);
		io->m_readBuffer = NULL;
	}
}

void VXIODevice_setDevice(XIODeviceBase* io, void* device)
{
	io->device = device;
}
