#include "XModbusFrameData.h"
#include "XQueue.h"
#include "XMemory.h"
#include "XModbusRtu.h"
#include "XCrc.h"
#include <string.h>
XModbusFrameQueue* XModbusFrameQueue_new()
{
	XModbusFrameQueue* queue = XQueue_New(XModbusFrameData*);
	/*if (queue)
	{
		XModbusFrameData frame{1,XVector_New(UCHAR)};

	}*/
	return queue;
}

void XModbusFrameQueue_push(XModbusFrameQueue* queue, XModbusFrameData* dataFrame)
{
	XQueue_push(queue,&dataFrame);
}

XModbusFrameData* XModbusFrameQueue_top(XModbusFrameQueue* queue)
{
	if(queue==NULL)
		return NULL;
	return XQueue_Top(queue, XModbusFrameQueue*);
}

bool XModbusFrameQueue_empty(XModbusFrameQueue* queue)
{
	if (queue)
		return XQueue_empty(queue);
	return true;
}

void XModbusFrameQueue_pop(XModbusFrameQueue* queue)
{
	if (queue == NULL)
		return NULL;
	//没有设置自动释放手动释放
	if (XContainerDataFreeMethod(queue) == NULL)
	{
		XModbusFrameData* top= XModbusFrameQueue_top(queue);
		////显释放里面的XVector数据
		//if(top->frame)
		//	XVector_free(top->frame);
		XModbusFrameData_free(top);
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

XModbusFrameData* XModbusFrameData_new()
{
	XModbusFrameData* frame=XMemory_malloc(sizeof(XModbusFrameData));
	frame->mode = MB_NOT_MODE;
	frame->dataFrame = XVector_New(uint8_t);
	frame->data = NULL;
	frame->recvHandle = NULL;
	return frame;
}
XModbusFrameData* XModbusFrameData_newRecvHandle()
{
	XModbusFrameData* frame = XModbusFrameData_new();
	frame->recvHandle = XMemory_malloc(sizeof(XModbusFrameDataRecvHandle));
	XModbusFrameDataRecvHandle_setZero(frame->recvHandle);
	return frame;
}
void XModbusFrameData_free(XModbusFrameData* frame)
{
	if (frame)
	{
		if (frame->dataFrame)
			XVector_free(frame->dataFrame);
		if (frame->recvHandle)
			XMemory_free(frame->recvHandle);
		XMemory_free(frame);
	}
}
//解析CRC16
static uint8_t XModbusFrameData_parse‌CRC16(uint8_t* pData, uint16_t pos)
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
