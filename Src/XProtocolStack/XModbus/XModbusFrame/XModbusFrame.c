#include "XModbusFrame.h"
#include "XQueue.h"
#include "XMemory.h"
#include "XModbusRtu.h"
#include "XCrc.h"
#include <string.h>
XModbusFrameQueue* XModbusFrameQueue_new()
{
	XModbusFrameQueue* queue = XQueue_New(XModbusFrame*);
	/*if (queue)
	{
		XModbusFrame frame{1,XVector_New(UCHAR)};

	}*/
	return queue;
}

void XModbusFrameQueue_push(XModbusFrameQueue* queue, XModbusFrame* frameData)
{
	XQueue_push(queue,&frameData);
}

XModbusFrame* XModbusFrameQueue_top(XModbusFrameQueue* queue)
{
	if(queue==NULL)
		return NULL;
	return XQueue_Top(queue, XModbusFrameQueue*);
}

bool XModbusFrameQueue_empty(XModbusFrameQueue* queue)
{
	if (queue)
		return XQueue_isEmpty(queue);
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
		//	XVector_free(top->frame);
		XModbusFrame_free(top);
	}
	XQueue_pop(queue);
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
	XQueue_free(queue);
}

XModbusFrame* XModbusFrame_new()
{
	XModbusFrame* frame=XMemory_malloc(sizeof(XModbusFrame));
	frame->mode = MB_NOT_MODE;
	frame->frameData = XVector_New(uint8_t);
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
		newFrame = XModbusFrame_new();

	}
	else
	{
		newFrame = XModbusFrame_newRecvHandle();
		memcpy(newFrame->recvHandle, frame->recvHandle,sizeof(XModbusFrameDataRecvHandle));
	}
	XVector_copy(newFrame->frameData, frame->frameData);
	newFrame->mode = frame->mode;
	newFrame->data = frame->data;
	return newFrame;
}
XModbusFrame* XModbusFrame_newRecvHandle()
{
	XModbusFrame* frame = XModbusFrame_new();
	frame->recvHandle = XMemory_malloc(sizeof(XModbusFrameDataRecvHandle));
	XModbusFrameDataRecvHandle_setZero(frame->recvHandle);
	return frame;
}
void XModbusFrame_free(XModbusFrame* frame)
{
	if (frame)
	{
		if (frame->frameData)
		{
			XVector_free(frame->frameData);
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
		handle->pRecvHandCallFunc = NULL;
		handle->userData = NULL;
		handle->waitAddressCode = NULL;
	}
}
