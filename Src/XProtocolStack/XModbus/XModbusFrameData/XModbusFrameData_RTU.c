#include"XModbusFrameData_RTU.h"
#if MB_RTU_ENABLED
#include"XModbusFrameData.h"
#include "XModbusRtu.h"
#include "XCrc.h"
#include "XAlgorithm.h"
#include <string.h>
//CRC16校验设置  其他数据必须都设置好了,并且预留了两字节的CRC空间
static XModbusFrameData_set16Data(XVector* v)
{
	//设置crc校验
	uint16_t crc16 = XCrc_get16(XVector_begin(v), XVector_size(v) - MB_SER_PDU_SIZE_CRC);
	XCrc_set16Data(XVector_at(v, 6), crc16, 0);
}
//8字节数据帧设置
static void setRtuDataFrame_8(XModbusFrameData* frame, uint8_t address, uint8_t funcCode, uint16_t dataOne, uint16_t dataTwo)
{
	//主机地址(1)+功能码(1)+数据(2)+数据(2)+crc16(2)
	if (frame == NULL)
		return;
	//数据(2)+数据(2)
	XModbusFrameDataRTU_initDataFrame(frame, address, funcCode, 2+2);
	XVector* v = frame->dataFrame;
	//第一个双字节数据
	memcpy(XVector_at(v, 2), &dataOne, 2);
	//设置寄存器数据
	memcpy(XVector_at(v, 4), &dataTwo, 2);

	XModbusFrameData_set16Data(v);
	/*
	XString* str = XModbusFrameDataRTU_to16HexString(frame);
	printf("当前帧:%s\n", XString_c_str(str));
	//            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
	XString_free(str);
	*/
}
//可变字节数据帧 读取寄存器 响应帧
static void setRtuDataFrame_readReg_reply(XModbusFrameData* frame, uint8_t address, uint8_t funcCode, uint16_t* regData, const uint16_t regCount)
{
	//主机地址(1)+功能码(1)+返回字节数量(1)+数据(2*regCount)+crc16(2)
	if (frame == NULL)
		return;
	//返回字节数量(1) + 数据(2 * regCount)
	XModbusFrameDataRTU_initDataFrame(frame,address,funcCode, 1 + 2 * regCount);
	XVector* v = frame->dataFrame;
	//设置返回的字节数量
	XVector_At(v, 2, uint8_t) = 2 * regCount;
	//准备拷贝寄存器数据
	if (regCount > 0)
		memcpy(XVector_at(v, 3), regData, regCount * 2);

	XModbusFrameData_set16Data(v);
}

void XModbusFrameDataRTU_initDataFrame(XModbusFrameData* frame, uint8_t address, uint8_t funcCode, uint16_t dataSize)
{
	
	/*使用模板
	//主机地址(1)+功能码(1)+?+crc16(2)
	if (frame == NULL)
		return;
	//?
	XModbusFrameDataRTU_initDataFrame(frame, address, funcCode, 2+2);
	XVector* v = frame->dataFrame;
	
	
	//CRC校验
	XModbusFrameData_set16Data(v);
	
	*/
	if (frame == NULL)
		return;
	if (frame->dataFrame == NULL)
		frame->dataFrame = XVector_New(uint8_t);
	XVector* v = frame->dataFrame;
	//主机地址(1)+功能码(1)+数据(dataSIze)+crc16(2)
	XVector_resize(v, MB_SER_PDU_PDU_OFF + 1 + dataSize + MB_SER_PDU_SIZE_CRC);
	frame->pduLength = 1 + dataSize;
	frame->pPduFramePos = XVector_at(v, 1);
	//设置地址
	XVector_At(v, 0, uint8_t) = address;
	frame->address = address;
	//设置功能码
	XVector_At(v, 1, uint8_t) = funcCode;
	frame->funcCode = funcCode;
	
}

void XModbusFrameDataRTU_setDataFrame_0x03_request(XModbusFrameData* frame, uint8_t address, uint16_t regAddress, const uint16_t regCount)
{
	if(regCount!=0)
	setRtuDataFrame_8(frame, address, MB_FUNC_READ_HOLDING_REGISTER, SwapEndian16(regAddress, 1), SwapEndian16(regCount, 1));
}

void XModbusFrameDataRTU_setDataFrame_0x03_reply(XModbusFrameData* frame, uint8_t address, uint16_t* regData, const uint16_t regCount)
{
	setRtuDataFrame_readReg_reply(frame, address, MB_FUNC_READ_HOLDING_REGISTER, regData, regCount);
}

void XModbusFrameDataRTU_setDataFrame_0x04_request(XModbusFrameData* frame, uint8_t address, uint16_t regAddress, const uint16_t regCount)
{
	setRtuDataFrame_8(frame, address, MB_FUNC_READ_INPUT_REGISTER, SwapEndian16(regAddress, 1), SwapEndian16(regCount, 1));
}

void XModbusFrameDataRTU_setDataFrame_0x04_reply(XModbusFrameData* frame, uint8_t address, uint16_t* regData, uint16_t regCount)
{
	setRtuDataFrame_readReg_reply(frame, address, MB_FUNC_READ_INPUT_REGISTER, regData, regCount);
}

void XModbusFrameDataRTU_setDataFrame_0x06_request(XModbusFrameData* frame, uint8_t address, uint16_t regAddress, const uint16_t* regData)
{
	if (regData != NULL)
		setRtuDataFrame_8(frame, address, MB_FUNC_WRITE_REGISTER, SwapEndian16(regAddress, 1), *regData);
}

void XModbusFrameDataRTU_setDataFrame_0x06_reply(XModbusFrameData* frame, uint8_t address, uint16_t regAddress, const uint16_t* regData)
{
	XModbusFrameDataRTU_setDataFrame_0x06_request(frame, address, regAddress, regData);
}

void XModbusFrameDataRTU_setDataFrame_0x10_request(XModbusFrameData* frame, uint8_t address, uint16_t regAddress, uint16_t regCount, uint16_t* regData)
{
	//主机地址(1)+功能码(1)+寄存器起始地址(2)+寄存器数量(2)+数据(2*regCount)+crc16(2)
	if (frame == NULL)
		return;
	//寄存器起始地址(2)+寄存器数量(2)+数据(2*regCount)
	XModbusFrameDataRTU_initDataFrame(frame, address, MB_FUNC_WRITE_MULTIPLE_REGISTERS, 2 + 2+ 2 * regCount);
	XVector* v = frame->dataFrame;
	//写入 寄存器起始地址
	XVector_At(v,2, uint16_t)=SwapEndian16(regAddress,1);
	//写入 寄存器数量
	XVector_At(v, 4, uint16_t) = SwapEndian16(regCount, 1);
	//开始写入数据
	if(regCount>0)
		memcpy(XVector_at(v, 6 ), regData, 2 * regCount);
	//CRC校验
	XModbusFrameData_set16Data(v);
}

void XModbusFrameDataRTU_setDataFrame_0x10_reply(XModbusFrameData* frame, uint8_t address, uint16_t regAddress, uint16_t regCount)
{
	//主机地址(1)+功能码(1)+寄存器起始地址(2)+已经写入的寄存器数量(2)+crc16(2)
	setRtuDataFrame_8(frame, address, MB_FUNC_WRITE_MULTIPLE_REGISTERS, SwapEndian16(regAddress,1), SwapEndian16(regCount, 1));
}

void XModbusFrameDataRTU_parseData(XModbusFrameData* frame, XVector* data)
{
	if (frame == NULL || data == NULL)
		return;
	if (frame->dataFrame == NULL)
		frame->dataFrame = XVector_New(uint8_t);
	if (frame->dataFrame == NULL)
	{
		XVector_clear(frame->dataFrame);
		frame->pPduFramePos = NULL;
		return;
	}
	//开始解析RTU数据
	size_t dataSize = XVector_size(data);
	uint8_t* pData = (uint8_t*)XVector_begin(data);
	// 校验帧长度和CRC（最小长度4字节，CRC正确）
	if ((dataSize >= MB_SER_PDU_SIZE_MIN) && (XCrc_get16(pData, dataSize) == 0)) {
		//printf("校验通过\n");
		XVector_copy(frame->dataFrame, data);
		//frame->address = pData[MB_SER_PDU_ADDR_OFF];  // 提取从机地址
		// 计算PDU长度（总长度 - 地址长度 - CRC长度）
		frame->pduLength = (USHORT)(dataSize - MB_SER_PDU_PDU_OFF - MB_SER_PDU_SIZE_CRC);
		frame->pPduFramePos = XVector_at(frame->dataFrame, MB_SER_PDU_PDU_OFF);  // PDU起始位置（地址后第1字节）
		frame->address = XModbusFrameDataRTU_parseAddress(frame);
		frame->funcCode = XModbusFrameDataRTU_parseFuncCode(frame);
	}
	else
	{
		//int num = *pData;
		XVector_clear(frame->dataFrame);
		frame->pPduFramePos = NULL;
	}
}

uint8_t XModbusFrameDataRTU_parseAddress(XModbusFrameData* frame)
{
	if (frame && frame->pPduFramePos)
		return *(frame->pPduFramePos - (MB_SER_PDU_PDU_OFF - MB_SER_PDU_ADDR_OFF));
	return 0xFF;
}

uint8_t XModbusFrameDataRTU_parseFuncCode(XModbusFrameData* frame)
{
	if (frame && frame->pPduFramePos)
		return *(frame->pPduFramePos);
	return 0;
}

XString* XModbusFrameDataRTU_to16HexString(XModbusFrameData* frame)
{
	if (frame && frame->pPduFramePos)
	{
		XVector* vector = frame->dataFrame;
		XString* str = XString_new(NULL);
		char buff[10];
		for (XVector_iterator* it = XVector_begin(vector); it != XVector_end(vector); it = XVector_iterator_add(vector, it))
		{
			sprintf(buff, "%02X ", *((uint8_t*)it));
			XString_append(str, buff);
		}
		XString_pop_back(str);
		return str;
	}
	return NULL;
}
#endif