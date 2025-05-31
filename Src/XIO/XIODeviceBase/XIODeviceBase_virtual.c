#include"XIODeviceBase.h"
#include"XCircularQueue.h"
#include"XCircularQueueAtomic.h"
#include "XMemory.h"
#include <string.h>
#include <assert.h>
//声明 
static void VXIODevice_free(XIODeviceBase* io);
static bool VXIODevice_open(XIODeviceBase* io, XIODeviceBaseMode mode);
static size_t VXIODevice_write(XIODeviceBase* io, const char* data, size_t maxSize);//写入
static size_t VXIODevice_writeFull(XIODeviceBase* io);//将剩余的数据刷入设备
static size_t VXIODevice_read(XIODeviceBase* io, char* data, size_t maxSize);//读取
static size_t VXIODevice_getBytesAvailable(XIODeviceBase* io);
static size_t VXIODeviceBase_getBytesToWrite(XIODeviceBase* io);
static bool VXIODeviceBase_atEnd(XIODeviceBase* io);
static void VXIODevice_close(XIODeviceBase* io);
static void VXIODevice_poll(XIODeviceBase* io);
static void VXIODevice_setWriteBuffer(XIODeviceBase* io, size_t count);
static void VXIODevice_setReadBuffer(XIODeviceBase* io, size_t count);
static void VXIODevice_setDevice(XIODeviceBase* io, void* device);
XVtable* XIODeviceBaseVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XIODEVICEBASE_VTABLE_SIZE];//虚函数数据
#endif

void XIODeviceBase_class_init()
{
	//仅初始化一次
	if (XIODeviceBaseVtable)
		return;
#if !VTABLE_ISSTACK
	XIODeviceVtable = XVtable_new();
#else
	XIODeviceBaseVtable = &vtable;
	XVtable_init_stack(XIODeviceBaseVtable, vtable_data, XIODEVICEBASE_VTABLE_SIZE);
#endif
	//继承的函数
	XVtable_append_vtable(XIODeviceBaseVtable, XClassVtable);
	void* table[] = {
		VXIODevice_open,VXIODevice_write,
		VXIODevice_writeFull,VXIODevice_read,
		VXIODevice_getBytesAvailable,VXIODeviceBase_getBytesToWrite,
		VXIODeviceBase_atEnd,VXIODevice_close,
		VXIODevice_poll,VXIODevice_setWriteBuffer,
		VXIODevice_setReadBuffer,VXIODevice_setDevice 
	};
	XVtable_append_array(XIODeviceBaseVtable, table, sizeof(table) / sizeof(table[0]));
	//重写的函数
	XVtable_At(XIODeviceBaseVtable, EXClass_Free) = VXIODevice_free;
#if SHOWCONTAINERSIZE
	printf("XIODeviceBase size:%d\n", XVtable_size(XIODeviceBaseVtable));
#endif
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
		XClassGetVtable(io) = XIODeviceBaseVtable;
	else
		XClassGetVtable(io) = vtable;
}

void VXIODevice_free(XIODeviceBase* io)
{
	XIODeviceBase_close_base(io);
	if (io->m_writeBuffer)
		XCircularQueue_free_base(io->m_writeBuffer);
	if (io->m_readBuffer)
		XCircularQueue_free_base(io->m_readBuffer);
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
	/**************************************************/
	ISNULL(0, "请重载这个函数,这是模板");
	/**************************************************/
	return 0;
	//if (io->m_mode & XIODeviceBase_WriteOnly == 0)
	//	return 0;
	//if (io->m_port.writeData_funcPointer == NULL)
	//	return 0;
	////printf("x");
	//size_t count = 0;
	//if (io->m_writeBuffer == NULL)
	//{//没有写入缓冲区
	//	count += io->m_port.writeData_funcPointer(io, data, maxSize);
	//}
	//else
	//{
	//	do
	//	{
	//		while (XCircularQueue_push_base(io->m_writeBuffer, data + count))
	//		{
	//			++count;
	//			if (count >= maxSize)
	//				break;
	//		}
	//		if (XCircularQueue_isFull_base(io->m_writeBuffer))
	//			io->m_port.writeBufferFull_funcPointer(io,io->m_writeBuffer);
	//		if (count >= maxSize)
	//			break;
	//	} while (!XCircularQueue_isFull_base(io->m_writeBuffer));
	//}
	//return count;
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
	/**************************************************/
	ISNULL(0, "请重载这个函数,这是模板");
	/**************************************************/
	return;
	//if (io->m_mode & XIODeviceBase_ReadOnly == 0)
	//	return 0;
	//if (io->m_port.readData_funcPointer == NULL)
	//	return 0;
	//size_t count = 0;
	//if (io->m_readBuffer == NULL)
	//{//没有读取缓冲区
	//	count += io->m_port.readData_funcPointer(io, data, maxSize);
	//}
	//else
	//{
	//	do
	//	{
	//		while (XCircularQueue_receive_base(io->m_readBuffer, data + count))
	//		{
	//			++count;
	//			if (count >= maxSize)
	//				break;
	//		}
	//		if (XCircularQueue_isEmpty_base(io->m_readBuffer))
	//			io->m_port.readBufferEmpty_funcPointer(io,io->m_readBuffer);
	//		if (count >= maxSize)
	//			break;

	//	} while (!XCircularQueue_isEmpty_base(io->m_readBuffer));

	//}
	//return count;
}

size_t VXIODevice_getBytesAvailable(XIODeviceBase* io)
{
	if (io->m_readBuffer == NULL)
		return 0;
	return XCircularQueue_getSize_base(io->m_readBuffer);
}

size_t VXIODeviceBase_getBytesToWrite(XIODeviceBase* io)
{
	if (io->m_writeBuffer == NULL)
		return 0;
	return XCircularQueue_getSize_base(io->m_writeBuffer);
}

bool VXIODeviceBase_atEnd(XIODeviceBase* io)
{
	return false;
}

void VXIODevice_close(XIODeviceBase* io)
{
	/**************************************************/
	ISNULL(0, "请重载这个函数,这是模板");
	/**************************************************/
	/*if (VXIODevice_isOpen(io))
	{
		if (io->m_port.close_funcPointer)
			io->m_port.close_funcPointer(io);
	}
	io->m_mode = XIODeviceBase_NotOpen;*/
}

void VXIODevice_poll(XIODeviceBase* io)
{
	/**************************************************/
	ISNULL(0, "请重载这个函数,这是模板");
	/**************************************************/
	/*if (io->m_port.poll_funcPointer)
	{
		io->m_port.poll_funcPointer(io);
	}*/
}

void VXIODevice_setWriteBuffer(XIODeviceBase* io, size_t count)
{
	if (count != 0)
	{
		if (io->m_writeBuffer == NULL)
			io->m_writeBuffer = XCircularQueueAtomic_New(char, count);
		assert(io->m_writeBuffer);
	}
	else if (io->m_writeBuffer != NULL)
	{
		XCircularQueueAtomic_free_base(io->m_writeBuffer);
		io->m_writeBuffer = NULL;
	}
}

void VXIODevice_setReadBuffer(XIODeviceBase* io, size_t count)
{
	if (count != 0)
	{
		if (io->m_readBuffer == NULL)
			io->m_readBuffer = XCircularQueueAtomic_New(char, count);
		assert(io->m_readBuffer);
	}
	else if (io->m_readBuffer != NULL)
	{
		XCircularQueueAtomic_free_base(io->m_readBuffer);
		io->m_readBuffer = NULL;
	}
}

void VXIODevice_setDevice(XIODeviceBase* io, void* device)
{
	io->device = device;
}
