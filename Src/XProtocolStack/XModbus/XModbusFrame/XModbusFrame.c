#include "XModbusFrame.h"
#include "XCircularQueueAtomic.h"
#include "XQueue.h"
#include "XMemory.h"
#include "XModbusRtu.h"
#include "XCrc.h"
#include <string.h>
XModbusFrameQueue* XModbusFrameQueue_create(size_t count)
{
	XModbusFrameQueue* queue = XCircularQueueAtomic_Create(XModbusFrame*,count);
	return queue;
}

bool XModbusFrameQueue_push(XModbusFrameQueue* queue, XModbusFrame* frameData)
{
	return XCircularQueue_push_base(queue, &frameData);
}

XModbusFrame* XModbusFrameQueue_top(XModbusFrameQueue* queue)
{
	if(queue==NULL)
		return NULL;
	return XCircularQueue_Top_Base(queue, XModbusFrameQueue*);
}

bool XModbusFrameQueue_empty(XModbusFrameQueue* queue)
{
	if (queue)
		return XCircularQueue_isEmpty_base(queue);
	return true;
}

void XModbusFrameQueue_pop(XModbusFrameQueue* queue)
{
	if (queue == NULL)
		return NULL;
	//没有设置自动释放手动释放
	if (XContainerDataFreeMethod(queue) == NULL)
	{
		XModbusFrame* top= XModbusFrameQueue_top(queue);
		////显释放里面的XVector数据
		//if(top->frame)
		//	XVector_delete_base(top->frame);
		if(top->autoDelete)
			XModbusFrame_free(top);
	}
	XCircularQueue_pop_base(queue);
}

bool XModbusFrameQueue_receive(XModbusFrameQueue* queue, XModbusFrame** pvFrame)
{
	return XCircularQueue_receive_base(queue,pvFrame);
}

void XModbusFrameQueue_clear(XModbusFrameQueue* queue)
{
	while (XModbusFrameQueue_empty(queue))
	{
		XModbusFrameQueue_pop(queue);
	}
}

void XModbusFrameQueue_free(XModbusFrameQueue* queue)
{
	XModbusFrameQueue_clear(queue);
	XCircularQueue_delete_base(queue);
}

XModbusFrame* XModbusFrame_create()
{
	XModbusFrame* frame=XMemory_malloc(sizeof(XModbusFrame));
	if (frame == NULL)
		return NULL;
	frame->mode = MB_NOT_MODE;
	frame->frameData = XVector_Create(uint8_t);
	frame->data = NULL;
	frame->recvHandle = NULL;
	return frame;
}
XModbusFrame* XModbusFrame_copy(XModbusFrame* frame)
{
	if(frame==NULL)
		return NULL;
	XModbusFrame* newFrame = NULL;
	if (frame->recvHandle == NULL)
	{
		newFrame = XModbusFrame_create();

	}
	else
	{
		newFrame = XModbusFrame_newRecvHandle();
		memcpy(newFrame->recvHandle, frame->recvHandle,sizeof(XModbusFrameDataRecvHandle));
	}
	XVector_copy_base(newFrame->frameData, frame->frameData);
	newFrame->mode = frame->mode;
	newFrame->data = frame->data;
	return newFrame;
}
XModbusFrame* XModbusFrame_newRecvHandle()
{
	XModbusFrame* frame = XModbusFrame_create();
	frame->recvHandle = XMemory_malloc(sizeof(XModbusFrameDataRecvHandle));
	//printf("创建\n");
	XModbusFrameDataRecvHandle_setZero(frame->recvHandle);
	return frame;
}
void XModbusFrame_free(XModbusFrame* frame)
{
	if (frame)
	{
		if (frame->frameData)
		{
			XVector_delete_base(frame->frameData);
		}
		if (frame->recvHandle)
		{
			XMemory_free(frame->recvHandle);
		}
		if (frame->data)
		{
			switch (frame->mode)
			{
			
			case MB_RTU_MASTER:
			case MB_RTU_SLAVE:XModbusFrameRTU_free(frame->data); break;
			case MB_NOT_MODE:printf("内存没释放\n"); break;
			default:
				break;
			}
		}
		XMemory_free(frame);
	}
}
uint8_t XModbusFrame_getAddress(XModbusFrame* frame)
{
	if (frame)
	{
		switch (frame->mode)
		{
		case MB_RTU_MASTER:
		case MB_RTU_SLAVE:
			if (frame->data)
				return ((XModbusFrameRTU*)frame->data)->address;
			else
				return XModbusFrameRTU_parseAddress(frame); break;
		default:
			break;
		}
	}
	return 0;
}
uint8_t XModbusFrame_getFuncCode(XModbusFrame* frame)
{
	if (frame)
	{
		switch (frame->mode)
		{
		case MB_RTU_MASTER:
		case MB_RTU_SLAVE:
			if (frame->data)
				return ((XModbusFrameRTU*)frame->data)->funcCode;
			else
				return XModbusFrameRTU_parseFuncCode(frame); break;
		default:
			break;
		}
	}
	return 0;
}
//解析CRC16
static uint8_t XModbusFrame_parse‌CRC16(uint8_t* pData, uint16_t pos)
{
	// 提取低位字节
	uint16_t lowByte = (uint16_t)pData[pos];
	// 提取高位字节
	uint16_t highByte = (uint16_t)pData[pos + 1];
	// 组合成 16 位 CRC 校验码
	return (highByte << 8) | lowByte;
}


void XModbusFrameDataRecvHandle_setZero(XModbusFrameDataRecvHandle* handle)
{
	if (handle)
	{
		handle->timeout = 0;
		handle->pRecvHandCallFunc = NULL;
		handle->userData = NULL;
		handle->waitAddressCode = NULL;
	}
}
