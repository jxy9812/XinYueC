#include "XModbusFrameQueue.h"
#include "XQueue.h"
#include "XMemory.h"
#include "XModbusRtu.h"
#include "XCrc.h"
XModbusFrameQueue* XModbusFrameQueue_new()
{
	XModbusFrameQueue* queue = XQueue_New(XModbusFrameData);
	/*if (queue)
	{
		XModbusFrameData dataFrame{1,XVector_New(UCHAR)};

	}*/
	return queue;
}

void XModbusFrameQueue_push(XModbusFrameQueue* queue, XModbusFrameData* dataFrame)
{
	XQueue_push(queue,dataFrame);
}

XModbusFrameData* XModbusFrameQueue_Top(XModbusFrameQueue* queue)
{
	if(queue==NULL)
		return NULL;
	XQueue_Top(queue, XModbusFrameQueue);
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
		XModbusFrameData* top= XModbusFrameQueue_Top(queue);
		//显释放里面的XVector数据
		if(top->dataFrame)
			XVector_free(top->dataFrame);
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

XModbusFrameData XModbusFrameData_new()
{
	XModbusFrameData dataFrame;
	dataFrame.address = 1;
	dataFrame.crc16 = 0;
	dataFrame.dataFrame = XVector_New(UCHAR);
	dataFrame.pduLength = 0;
	return dataFrame;
}

void XModbusFrameData_setRtuDataFrame(XModbusFrameData* dataFrame, UCHAR address, const UCHAR* pPduframe, USHORT pduLength)
{
	if (dataFrame = NULL)
		return;
	if(dataFrame->dataFrame==NULL)
		dataFrame->dataFrame= XVector_New(UCHAR);
	XVector* dataVector = dataFrame->dataFrame;
	if (dataVector == NULL)
		return;
	//设置数据的总长度
	XVector_resize(dataVector, pduLength + 1 + MB_SER_PDU_SIZE_CRC);
	//获取内部数据指针
	UCHAR* pData = (UCHAR*)XVector_front(dataVector);
	//保存从机地址
	pData[MB_SER_PDU_ADDR_OFF] = address;  // 添加从机地址
	//保存pdu数据
	for (size_t i = 0; i < pduLength; i++)
	{
		pData[MB_SER_PDU_PDU_OFF + i] = pPduframe[i];
	}
	// 计算CRC（包含地址和PDU）
	USHORT usCRC16 = XCRC16((UCHAR*)dataFrame, pduLength + 1);
	//保存校验码
	uint16_t pos = pduLength + 1;//校验码所属位置
	pData[pos++] = (UCHAR)(usCRC16 & 0xFF);  // CRC低位（小端模式）
	pData[pos++] = (UCHAR)(usCRC16 >> 8);       // CRC高位
}
