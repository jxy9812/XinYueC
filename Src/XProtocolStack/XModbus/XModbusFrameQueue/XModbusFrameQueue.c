#include "XModbusFrameQueue.h"
#include "XQueue.h"
#include "XMemory.h"
#include "XModbusRtu.h"
#include "XCrc.h"
XModbusFrameQueue* XModbusFrameQueue_new()
{
	XModbusFrameQueue* queue = XQueue_New(XModbusFrameData*);
	/*if (queue)
	{
		XModbusFrameData dataFrame{1,XVector_New(UCHAR)};

	}*/
	return queue;
}

void XModbusFrameQueue_push(XModbusFrameQueue* queue, XModbusFrameData* dataFrame)
{
	XQueue_push(queue,&dataFrame);
}

XModbusFrameData* XModbusFrameQueue_Top(XModbusFrameQueue* queue)
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
		XModbusFrameData* top= XModbusFrameQueue_Top(queue);
		////显释放里面的XVector数据
		//if(top->dataFrame)
		//	XVector_free(top->dataFrame);
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
	XModbusFrameData* dataFrame=XMemory_malloc(sizeof(XModbusFrameData));
	//dataFrame.address = 1;
	//dataFrame.crc16 = 0;
	dataFrame->dataFrame = XVector_New(UCHAR);
	dataFrame->pduLength = 0;
	return dataFrame;
}
void XModbusFrameData_free(XModbusFrameData* dataFrame)
{
	if (dataFrame)
	{
		if (dataFrame->dataFrame)
			XVector_free(dataFrame->dataFrame);
		XMemory_free(dataFrame);
	}
}
//解析CRC16
static USHORT XModbusFrameData_parse‌CRC16(uint8_t* pData, uint16_t pos)
{
	// 提取低位字节
	uint16_t lowByte = (uint16_t)pData[pos];
	// 提取高位字节
	uint16_t highByte = (uint16_t)pData[pos + 1];
	// 组合成 16 位 CRC 校验码
	return (highByte << 8) | lowByte;
}
//设置CRC16
static void XModbusFrameData_set‌CRC16(uint8_t* pData, uint16_t pos,uint16_t crc16)
{
	pData[pos++] = (UCHAR)(crc16 & 0xFF);  // CRC低位（小端模式）
	pData[pos++] = (UCHAR)(crc16 >> 8);       // CRC高位
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
	UCHAR* pData = (UCHAR*)XVector_begin(dataVector);
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
	XModbusFrameData_set‌CRC16(pData, pos, usCRC16);
	//pData[pos++] = (UCHAR)(usCRC16 & 0xFF);  // CRC低位（小端模式）
	//pData[pos++] = (UCHAR)(usCRC16 >> 8);       // CRC高位
	dataFrame->pduLength = pduLength;
	dataFrame->pPduFramePos= XVector_at(dataFrame->dataFrame, MB_SER_PDU_PDU_OFF);
}

void XModbusFrameData_setRtuData(XModbusFrameData* dataFrame, XVector* data)
{
	if (dataFrame == NULL|| data==NULL)
		return;
	if (dataFrame->dataFrame == NULL)
		dataFrame->dataFrame = XVector_New(UCHAR);
	if (dataFrame->dataFrame == NULL)
	{
		XVector_clear(dataFrame->dataFrame);
		dataFrame->pPduFramePos = NULL;
		return;
	}
	//开始解析RTU数据
	size_t dataSize = XVector_size(data);
	UCHAR* pData = (UCHAR*)XVector_begin(data);
	// 校验帧长度和CRC（最小长度4字节，CRC正确）
	if ((dataSize >= MB_SER_PDU_SIZE_MIN) && (XCRC16(pData, dataSize) == 0)) {
		//printf("校验通过\n");
		XVector_copy(dataFrame->dataFrame, data);
		//dataFrame->address = pData[MB_SER_PDU_ADDR_OFF];  // 提取从机地址
		// 计算PDU长度（总长度 - 地址长度 - CRC长度）
		dataFrame->pduLength = (USHORT)(dataSize - MB_SER_PDU_PDU_OFF - MB_SER_PDU_SIZE_CRC);
		dataFrame->pPduFramePos = XVector_at(dataFrame->dataFrame,MB_SER_PDU_PDU_OFF);  // PDU起始位置（地址后第1字节）

		//dataFrame->crc16= XModbusFrameData_parse‌CRC16(pData, dataFrame->pduLength +1);
	}
	else 
	{
		int num = *pData;
		XVector_clear(dataFrame->dataFrame);
		dataFrame->pPduFramePos = NULL;
	}
}

UCHAR XModbusFrameData_getRtuAddress(XModbusFrameData* dataFrame)
{
	if(dataFrame&& dataFrame->pPduFramePos)
		return *(dataFrame->pPduFramePos - (MB_SER_PDU_PDU_OFF - MB_SER_PDU_ADDR_OFF));
	return 0xFF;
}

UCHAR XModbusFrameData_getRtuFuncCode(XModbusFrameData* dataFrame)
{
	if (dataFrame && dataFrame->pPduFramePos)
		return *(dataFrame->pPduFramePos);
	return 0;
}

XString* XModbusFrameData_to16HexString(XModbusFrameData* dataFrame)
{
	if (dataFrame && dataFrame->pPduFramePos)
	{
		XVector* vector = dataFrame->dataFrame;
		XString* str = XString_new(NULL);
		char buff[10];
		for (XVector_iterator* it=XVector_begin(vector);it!=XVector_end(vector);it=XVector_iterator_add(vector,it))
		{
			sprintf(buff,"%02X ",*((UCHAR*)it));
			XString_append(str,buff);
		}
		XString_pop_back(str);
		return str;
	}
	return 0;
}