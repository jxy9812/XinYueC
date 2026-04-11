#include "XCommunicatorBase.h"
#include "XTimerGroupWheel.h"
#include "XVector.h"
#include "XMemory.h"
#include "XCircularQueue.h"
#include <string.h>
#include <assert.h>
static void VXCommunicatorBase_deinit(XCommunicatorBase* comm);
static bool VXCommunicatorBase_connect(XCommunicatorBase* comm);
static bool VXCommunicatorBase_disconnect(XCommunicatorBase* comm);
static size_t VXCommunicatorBase_send(XCommunicatorBase* comm, const void* data, size_t size);
static size_t VXCommunicatorBase_recv(XCommunicatorBase* comm, void* data, size_t maxSize);
static bool VXCommunicatorBase_sendAsync(XCommunicatorBase* comm, const void* data, size_t size); // 异步发送
static bool VXCommunicatorBase_recvAsync(XCommunicatorBase* comm, size_t maxSize); // 异步接收
static bool VXCommunicatorBase_isConnected(XCommunicatorBase* comm);
static void VXCommunicatorBase_poll(XCommunicatorBase* comm);
static void VXCommunicatorBase_setOption(XCommunicatorBase* comm, int optionId, const void* value, size_t size);
static void VXCommunicatorBase_getOption(XCommunicatorBase* comm, int optionId, void* value, size_t* size);
XVtable* XCommunicatorBase_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XCommunicatorBase))
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XObject_class_init());
	void* table[] = 
	{
		VXCommunicatorBase_connect,VXCommunicatorBase_disconnect,
		VXCommunicatorBase_send,VXCommunicatorBase_recv,
		VXCommunicatorBase_sendAsync,VXCommunicatorBase_recvAsync,
		VXCommunicatorBase_isConnected,
		VXCommunicatorBase_setOption,VXCommunicatorBase_getOption
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXCommunicatorBase_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXCommunicatorBase_poll);
#if SHOWCONTAINERSIZE
	printf("XIODevice size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXCommunicatorBase_deinit(XCommunicatorBase* comm)
{
	if(comm->m_io)
	{
		XIODevice_deleteLater(comm->m_io);
		comm->m_io = NULL;
	}
	if (comm->m_recvAsyncBuffer)
	{
		XVector_delete_base(comm->m_recvAsyncBuffer);
		comm->m_recvAsyncBuffer = NULL;
	}
		// 释放父对象
	XVtableGetFunc(XObject_class_init(), EXClass_Deinit, void(*)(XObject*))(comm);
}

bool VXCommunicatorBase_connect(XCommunicatorBase* comm)
{
	if(comm->m_io==NULL)
		return false;
	if (comm->m_io->m_openMode != XIODevice_NotOpen)
		return true;
	if (XIODevice_isOpen(comm->m_io))
		return true;
	if (XIODevice_open_base(comm->m_io, XIODevice_ReadWrite))
	{
		XCommunicatorBase_connect_signal(comm);
		return true;
	}
	return false;
}

bool VXCommunicatorBase_disconnect(XCommunicatorBase* comm)
{
	if (comm->m_io == NULL|| !VXCommunicatorBase_isConnected(comm))
		return false;
	XIODevice_close_base(comm->m_io);
	XCommunicatorBase_disconnect_signal(comm);
	return true;
}

bool VXCommunicatorBase_isConnected(XCommunicatorBase* comm)
{
	if (comm->m_io == NULL)
		return false;
	return XIODevice_isOpen(comm->m_io);
}

size_t VXCommunicatorBase_send(XCommunicatorBase* comm, const void* data, size_t size)
{
	if (comm->m_io == NULL)
		return 0;
	
	return XIODevice_write(comm->m_io, data, size);
	
}
size_t VXCommunicatorBase_recv(XCommunicatorBase* comm, void* data, size_t maxSize)
{
	if (comm->m_io == NULL)
		return 0;
	size_t size = 0,readSize=0;
	//XTimerTimeWheel* timer = NULL;
	if (comm->m_opt_timeout != 0)
	{//设置了超时时间
		comm->m_currentTimeout = XTimerBase_getCurrentTime();
	}
	while (size < maxSize)
	{
		readSize = XIODevice_bytesAvailable_base(comm->m_io);
		if (readSize > 0)
		{
			size += XIODevice_read(comm->m_io, ((char*)data) + size, (maxSize - size) > readSize ? readSize : (maxSize - size));
		}
		else if (comm->m_opt_timeout != 0 && (comm->m_currentTimeout + comm->m_opt_timeout) < XTimerBase_getCurrentTime())
		{//超时退出
			break;
		}
	}
	return size;
}

bool VXCommunicatorBase_sendAsync(XCommunicatorBase* comm, const void* data, size_t size)
{
	if (comm->m_io == NULL)
		return false;
	return XIODevice_write(comm->m_io, data, size)==size;
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
		XVector_delete_base(comm->m_recvAsyncBuffer);
		comm->m_recvAsyncBuffer = NULL;
	}
	return false;
}
static void recvAsync(XCommunicatorBase* comm)
{
	if (comm->m_io && comm->m_recvAsyncBuffer)
	{//开启了异步接收
		size_t size = XIODevice_bytesAvailable_base(comm->m_io);
		if (size == 0)
			return;
		size_t buffCapacity = XContainerCapacity(comm->m_recvAsyncBuffer);
		size_t buffSize = XContainerSize(comm->m_recvAsyncBuffer);
		//XContainerSize(comm->m_recvAsyncBuffer) = 0;
		if(XVector_size_base(comm->m_recvAsyncBuffer)< buffCapacity)
		{
			size_t readSize = XIODevice_read(comm->m_io, ((uint8_t*)XContainerDataPtr(comm->m_recvAsyncBuffer))+buffSize, buffCapacity-buffSize);
			if (readSize == 0)
				return;
			XContainerSize(comm->m_recvAsyncBuffer) += readSize;
			if (XContainerSize(comm->m_recvAsyncBuffer) >= buffCapacity)
			{//缓冲区已满
				XCommunicatorBase_recvBuffFull_signal(comm,comm->m_recvAsyncBuffer);
			}
		}
		//if (readSize > 0)
			//comm->m_recvDataCallback(XContainerDataPtr(comm->m_recvAsyncBuffer), readSize,comm->m_userData);
	}
}
void VXCommunicatorBase_poll(XCommunicatorBase* comm)
{
	//处理异步接收
	recvAsync(comm);
}

void VXCommunicatorBase_setOption(XCommunicatorBase* comm, int optionId, const void* value, size_t size)
{
	switch (optionId)
	{
	case OPT_TIMEOUT: if (size == sizeof(uint16_t))comm->m_opt_timeout = *((uint16_t*)value); break;
		case OPT_SEND_BUFFER_SIZE:if (comm->m_io&&size==sizeof(size_t)) 
		//{XIODevice_setWriteBuffer_base(comm->m_io, *((size_t*)value));}
								 break;
		case OPT_RECV_BUFFER_SIZE:if (comm->m_io && size == sizeof(size_t)) 
		//{ XIODevice_setReadBuffer_base(comm->m_io, *((size_t*)value)); } 
								 break;
		default:
			break;
	}
}

void VXCommunicatorBase_getOption(XCommunicatorBase* comm, int optionId, void* value, size_t* size)
{
	switch (optionId)
	{
	case OPT_TIMEOUT:  *((uint16_t*)value) = comm->m_opt_timeout; *size = sizeof(uint16_t); break;
	case OPT_SEND_BUFFER_SIZE:
	{
		/*if (comm->m_io != NULL &&comm->m_io->m_writeBuffer != NULL)
		{
			*((size_t*)value) = XCircularQueue_size_base(comm->m_io->m_writeBuffer);
			*size = sizeof(size_t);
		}*/
		break;
	}
	case OPT_RECV_BUFFER_SIZE:
	{
		/*if (comm->m_io != NULL && comm->m_io->m_readBuffer != NULL)
		{
			*((size_t*)value) = XCircularQueue_size_base(comm->m_io->m_readBuffer);
			*size = sizeof(size_t);
		}*/
		break;
	}
	default:
		break;
	}
}
