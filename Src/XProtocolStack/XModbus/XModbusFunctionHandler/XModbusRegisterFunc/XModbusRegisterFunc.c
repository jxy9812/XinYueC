#include"XModbusRegisterFunc.h"
#include"XMemory.h"
#include"XVector.h"
#include"XModbusFrameData.h"
#include"XModbusFunctionHandler.h"
#include"XModbus.h"
#include"XModbusRtu.h"
#include"XCrc.h"
#include"XAlgorithm.h"
#include<string.h>
//寄存器大小
#define REGISTERSIZE 2
//读取16位数据
#define ReadData16(p) ((*(p)) << 8 | (*(p + 1)))
XModbusRegisterFunc* XModbusRegisterFunc_new(uint16_t regCount)
{
	if(regCount==0)
		return NULL;
	XModbusRegisterFunc* ptr = XMemory_malloc(sizeof(XModbusRegisterFunc));
	ptr->parent.data = XVector_New(REGISTERSIZE);
	XVector_resize(ptr->parent.data, regCount);
	return ptr;
}

bool XModbusRegisterFunc_write_uint16_t(XModbusRegisterFunc* regFunc, uint16_t regAddress, uint16_t value)
{
	if (regFunc == NULL || regFunc->parent.data == NULL)
		return false;
	XVector* data= regFunc->parent.data;
	if(XVector_size(data)>regAddress)
	{
		XVector_At(data, regAddress, uint16_t) = value;
		//printf("写入的值是:%d\n",value);
		return true;
	}
	return false;
}

bool XModbusRegisterFunc_write(XModbusRegisterFunc* regFunc, uint16_t regAddress, const char* writeArray, uint16_t arraySize)
{
	if (regFunc == NULL || regFunc->parent.data == NULL|| writeArray==NULL||arraySize==0)
		return false;
	XVector* data = regFunc->parent.data;
	uint32_t dataSize = XVector_size(data);
	//uint16_t typeSize = XVector_typeSize(data);
	if (dataSize > regAddress && (dataSize * REGISTERSIZE) >= (regAddress * REGISTERSIZE + arraySize))
	{//开始写入
		char* p= XVector_at(data, regAddress);
		for (size_t i=0; i < arraySize; i++)
		{
			p[i] = writeArray[i];
		}
		return true;
	}
	return false;
}

bool XModbusRegisterFunc_read(XModbusRegisterFunc* regFunc, uint16_t regAddress, uint16_t regCount, char* readArray, uint16_t readArraySize)
{
	if (regFunc == NULL || regFunc->parent.data == NULL || regCount==0|| readArray == NULL || readArraySize == 0)
		return false;
	//检查输入缓冲区是否够大
	if(REGISTERSIZE* regCount> readArraySize)
		return false;
	
	XVector* data = regFunc->parent.data;
	uint32_t dataSize = XVector_size(data);
	//检查寄存器地址和数量是否合法
	if (dataSize < (regAddress+ regCount))
		return false;
	//开始拷贝数据到读取缓冲区
	for (size_t i = 0; i < regCount; i++)
	{
		*(((uint16_t*)readArray) + i) = *XModbusRegisterFunc_at(regFunc, regAddress+i);
	}
	return true;
}

uint16_t* XModbusRegisterFunc_at(XModbusRegisterFunc* regFunc, uint16_t regAddress)
{
	if (regFunc == NULL || regFunc->parent.data == NULL);
		return NULL;
	return XVector_at(regFunc->parent.data, regAddress);
}

XModbusException XModbusRegisterFunc_0x03_RTU_masterRecvHandCallFunc(XModbus* modbus, XModbusFrameData* frameData, XModbusFunctionHandler* FunctionHandler)
{
	if (frameData == NULL || FunctionHandler == NULL)
		return MB_EX_ILLEGAL_DATA_ADDRESS;

	//主站收到数据调用回调函数用户自己处理数据
	/*if (recvFrame->pRecvHandCallFunc)
		recvFrame->pRecvHandCallFunc(recvFrame);*/
	return MB_EX_NONE;
}
/*
请求报文（主站→从站）：
0x03 0x03 0x00 0x00 0x00 0x02 CRC16
解析：
0x03：从机地址（3）
0x03：功能码（读保持寄存器）
0x00 0x00：起始寄存器地址（0）
0x00 0x02：读取寄存器数量（2 个，共 4 字节）
响应报文（从站→主站）：
0x03 0x03 0x04 0x01 0x02 0x03 0x04 CRC16
解析：
0x04：数据字节数（4 字节，2 个寄存器，每个 2 字节）
0x01 0x02：第一个寄存器值（0x0102）
0x03 0x04：第二个寄存器值（0x0304）
*/
XModbusException XModbusRegisterFunc_0x03_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrameData* recvFrame, XModbusFunctionHandler* FunctionHandler)
{
	if (recvFrame == NULL || FunctionHandler == NULL)
		return MB_EX_ILLEGAL_DATA_ADDRESS;
	XModbusRegisterFunc* regFunc = FunctionHandler->data;

	//指向功能码后的寄存器地址数据
	uint16_t* p =XVector_at(recvFrame->frameData,2);
	//获取寄存器地址
	uint16_t regAddress = SwapEndian16(p[0], 1);
	//需要读取的寄存器数量
	uint16_t regCount = SwapEndian16(p[1], 1);
	XVector* data = regFunc->parent.data;//寄存器数据
	void* readStart = XVector_at(data, regAddress);//寄存器数据缓冲区
	XModbusFrameData* sendFrame = XModbusFrameData_new();
	XModbusFrameDataRTU_setDataFrame_0x03_reply(sendFrame, XModbusFrameData_getAddress(recvFrame), readStart, regCount);
	//printf("读取保持寄存器\n");

	return MB_EX_NONE;
}
/*
请求报文（主站→从站）：
0x02 0x06 0x00 0x03 0x00 0xA1 CRC16
解析：
0x00 0x03：寄存器地址（3）
0x00 0xA1：写入值（0x00A1，即 161）

响应报文（从站→主站）：
0x02 0x06 0x00 0x03 0x00 0xA1 CRC16
解析：响应与请求相同，确认写入成功
*/
XModbusException XModbusRegisterFunc_0x06_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrameData* recvFrame, XModbusFunctionHandler* FunctionHandler)
{
	if (recvFrame == NULL || FunctionHandler == NULL)
		return MB_EX_ILLEGAL_DATA_ADDRESS;
	XModbusRegisterFunc* regFunc = FunctionHandler->data;
	XModbusFrameDataRTU* rtu = (XModbusFrameDataRTU*)recvFrame->data;
	if (rtu==NULL)
		return;

	if (XModbusRegisterFunc_write_uint16_t(regFunc, rtu->regAddress,XVector_At(rtu->data,0,uint16_t)))
	{//写入成功 将数据帧再次发送回去
		XModbusFrameData* sendFrame = XModbusFrameData_new();
		XVector_swap(recvFrame->frameData, sendFrame->frameData);
		XModbus_sendData(modbus, sendFrame);
	}
	return MB_EX_NONE;
}
