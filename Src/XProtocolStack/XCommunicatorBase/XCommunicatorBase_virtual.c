#include "XCommunicatorBase.h"
#include "XTimerGroupWheel.h"
#include "XVector.h"
#include "XMemory.h"
#include "XCircularQueue.h"
#include <string.h>
#include <assert.h>
static void VXCommunicatorBase_free(XCommunicatorBase* comm);
static bool VXCommunicatorBase_connect_base(XCommunicatorBase* comm);
static bool VXCommunicatorBase_disconnect_base(XCommunicatorBase* comm);
static size_t VXCommunicatorBase_send_base(XCommunicatorBase* comm, const void* data, size_t size);
static size_t VXCommunicatorBase_recv_base(XCommunicatorBase* comm, void* data, size_t maxSize);
static bool VXCommunicatorBase_sendAsync(XCommunicatorBase* comm, const void* data, size_t size); // 异步发送
static bool VXCommunicatorBase_recvAsync(XCommunicatorBase* comm, size_t maxSize); // 异步接收
static bool VXCommunicatorBase_isConnected_base(XCommunicatorBase* comm);
static void VXCommunicatorBase_poll_base(XCommunicatorBase* comm);
static void VXCommunicatorBase_setOption_base(XCommunicatorBase* comm, int optionId, const void* value, size_t size);
static void VXCommunicatorBase_getOption_base(XCommunicatorBase* comm, int optionId, void* value, size_t* size);
XVtable* XCommunicatorBase_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCOMMUNICATORBASE_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XClass_class_init());
	void* table[] = 
	{
		VXCommunicatorBase_connect_base,VXCommunicatorBase_disconnect_base,
		VXCommunicatorBase_send_base,VXCommunicatorBase_recv_base,
		VXCommunicatorBase_sendAsync,VXCommunicatorBase_recvAsync,
		VXCommunicatorBase_isConnected_base,VXCommunicatorBase_poll_base,
		VXCommunicatorBase_setOption_base,VXCommunicatorBase_getOption_base
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Free, VXCommunicatorBase_free);
#if SHOWCONTAINERSIZE
	printf("XIODeviceBase size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXCommunicatorBase_free(XCommunicatorBase* comm)
{
	if(comm->m_io)
		XIODeviceBase_free_base(comm->m_io);
	if (comm->m_recvAsyncBuffer)
		XVector_free_base(comm->m_recvAsyncBuffer);
	XMemory_free(comm);
}

bool VXCommunicatorBase_connect_base(XCommunicatorBase* comm)
{
	if(comm->m_io==NULL)
		return false;
	if (comm->m_io->m_mode != XIODeviceBase_NotOpen)
		return true;
	return XIODeviceBase_open_base(comm->m_io,XIODeviceBase_ReadWrite);
}

bool VXCommunicatorBase_disconnect_base(XCommunicatorBase* comm)
{
	if (comm->m_io == NULL)
		return false;
	if (comm->m_io->m_mode != XIODeviceBase_NotOpen)
		return XIODeviceBase_close_base(comm->m_io);
	return true;
}

bool VXCommunicatorBase_isConnected_base(XCommunicatorBase* comm)
{
	if (comm->m_io == NULL)
		return false;
	return XIODeviceBase_isOpen(comm->m_io);
}

size_t VXCommunicatorBase_send_base(XCommunicatorBase* comm, const void* data, size_t size)
{
	if (comm->m_io == NULL)
		return 0;
	
	if (comm->m_io->m_writeBuffer != NULL)
	{//存在缓冲区
		XIODeviceBase_writeFull_base(comm->m_io);
		XIODeviceBase_write_base(comm->m_io, data, size);
		return  XIODeviceBase_writeFull_base(comm->m_io);
	}
	else
	{
		return XIODeviceBase_write_base(comm->m_io, data, size);
	}
	
}
static void recvOut(bool* run)
{
	*run = false;
}
size_t VXCommunicatorBase_recv_base(XCommunicatorBase* comm, void* data, size_t maxSize)
{
	if (comm->m_io == NULL)
		return 0;
	size_t size = 0,readSize=0;
	bool run = true;
	XTimerWheel* timer = NULL;
	if (comm->m_opt_timeout != 0)
	{
		timer = XTimerWheel_create();
		XTimerWheel_setUserData(timer, &run);
		XTimerWheel_setTimeout_base(timer, 5);
		XTimerWheel_setTimerCallback(timer, recvOut);
		XTimerWheel_start_base(timer);
		XTimerGroupWheel_addTimer_base(comm->m_wheel, timer);
	}
	while (size < maxSize)
	{
		readSize = XIODeviceBase_getBytesAvailable_base(comm->m_io);
		if (readSize > 0)
		{
			size += XIODeviceBase_read_base(comm->m_io, ((char*)data) + size, (maxSize - size) > readSize ? readSize : (maxSize - size));
		}
		else if (!run || comm->m_opt_timeout == 0)
		{
			break;
		}
		else
		{
			XTimerGroupWheel_poll_base(comm->m_wheel);
		}
	}
	if (timer && run)
	{
		XTimerGroupWheel_removeTimer_base(comm->m_wheel,timer);
		XTimerWheel_free_base(timer);
	}
	return size;
	//return XIODeviceBase_read_base(comm->m_io, data, maxSize);
}

bool VXCommunicatorBase_sendAsync(XCommunicatorBase* comm, const void* data, size_t size)
{
	if (comm->m_io == NULL)
		return false;
	return XIODeviceBase_write_base(comm->m_io, data, size)==size;
}

bool VXCommunicatorBase_recvAsync(XCommunicatorBase* comm, size_t maxSize)
{
	/*if (comm->m_io == NULL)
		return false;*/
	if (maxSize > 0)
	{
		if (comm->m_recvAsyncBuffer == NULL)
			comm->m_recvAsyncBuffer = XVector_Create(uint8_t);
		XVector_resize_base(comm->m_recvAsyncBuffer, maxSize);
		return true;
	}
	if (comm->m_recvAsyncBuffer != NULL)
	{
		XVector_free_base(comm->m_recvAsyncBuffer);
		comm->m_recvAsyncBuffer = NULL;
	}
	return false;
}
static void recvAsync(XCommunicatorBase* comm)
{
	if (comm->m_io && comm->m_recvAsyncBuffer && comm->m_recvDataCallback)
	{//开启了异步接收
		size_t size = XIODeviceBase_getBytesAvailable_base(comm->m_io);
		if (size == 0)
			return;
		size_t buffSize = XContainerCapacity(comm->m_recvAsyncBuffer);
		XContainerSize(comm->m_recvAsyncBuffer) = 0;
		size_t readSize = XIODeviceBase_read_base(comm->m_io, XContainerDataPtr(comm->m_recvAsyncBuffer), (buffSize > size) ? size : buffSize);
		if (readSize > 0)
			comm->m_recvDataCallback(XContainerDataPtr(comm->m_recvAsyncBuffer), readSize,comm->m_userData);
	}
}
void VXCommunicatorBase_poll_base(XCommunicatorBase* comm)
{
	//处理异步接收
	recvAsync(comm);
	//处理定时器任务
	if (comm->m_wheel)
		XTimerGroupWheel_poll_base(comm->m_wheel);

}

void VXCommunicatorBase_setOption_base(XCommunicatorBase* comm, int optionId, const void* value, size_t size)
{
	switch (optionId)
	{
	case OPT_TIMEOUT: if (size == sizeof(uint16_t))comm->m_opt_timeout = *((uint16_t*)value); break;
		case OPT_SEND_BUFFER_SIZE:if (comm->m_io&&size==sizeof(size_t)) {XIODeviceBase_setWriteBuffer_base(comm->m_io, *((size_t*)value));} break;
		case OPT_RECV_BUFFER_SIZE:if (comm->m_io && size == sizeof(size_t)) { XIODeviceBase_setReadBuffer_base(comm->m_io, *((size_t*)value)); } break;
		default:
			break;
	}
}

void VXCommunicatorBase_getOption_base(XCommunicatorBase* comm, int optionId, void* value, size_t* size)
{
	switch (optionId)
	{
	case OPT_TIMEOUT:  *((uint16_t*)value) = comm->m_opt_timeout; *size = sizeof(uint16_t); break;
	case OPT_SEND_BUFFER_SIZE:
	{
		if (comm->m_io != NULL &&comm->m_io->m_writeBuffer != NULL)
		{
			*((size_t*)value) = XCircularQueue_getSize_base(comm->m_io->m_writeBuffer);
			*size = sizeof(size_t);
		}
		break;
	}
	case OPT_RECV_BUFFER_SIZE:
	{
		if (comm->m_io != NULL && comm->m_io->m_readBuffer != NULL)
		{
			*((size_t*)value) = XCircularQueue_getSize_base(comm->m_io->m_readBuffer);
			*size = sizeof(size_t);
		}
		break;
	}
	default:
		break;
	}
}
