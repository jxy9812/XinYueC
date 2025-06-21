#include"XModbusRegisterHandler.h"
#include"XMemory.h"
#include"XVector.h"
#include"XModbusFrame.h"
#include"XModbusFunctionHandler.h"
#include"XModbusBase.h"
//#include"XCrc.h"
//#include"XAlgorithm.h"
#include<string.h>
//寄存器大小
#define REGISTERSIZE 2
//读取16位数据
#define ReadData16(p) ((*(p)) << 8 | (*(p + 1)))
XModbusRegisterHandler* XModbusRegisterHandler_create(uint16_t regCount)
{
	if(regCount==0)
		return NULL;
	XModbusRegisterHandler* ptr = XMemory_malloc(sizeof(XModbusRegisterHandler));
	ptr->parent.data = XVector_create(REGISTERSIZE);
	XVector_resize_base(ptr->parent.data, regCount);
	return ptr;
}

void XModbusRegisterHandler_free(XModbusRegisterHandler* pRegHandler)
{
	if (pRegHandler)
	{
		if (pRegHandler->parent.data)
			XVector_delete_base(pRegHandler->parent.data);
		XMemory_free(pRegHandler);
	}
}

bool XModbusRegisterHandler_write_uint16_t(XModbusRegisterHandler* regFunc, uint16_t regAddress, uint16_t value)
{
	if (regFunc == NULL || regFunc->parent.data == NULL)
		return false;
	XVector* data= regFunc->parent.data;
	if(XVector_getSize_base(data)>regAddress)
	{
		XVector_At_Base(data, regAddress, uint16_t) = value;
		//printf("写入的值是:%d\n",value);
		return true;
	}
	return false;
}

bool XModbusRegisterHandler_write(XModbusRegisterHandler* regFunc, uint16_t regAddress, uint16_t regCount, const char* writeArray)
{
	if (regFunc == NULL || regFunc->parent.data == NULL|| writeArray==NULL||regCount==0)
		return false;
	XVector* data = regFunc->parent.data;
	uint32_t dataSize = XVector_getSize_base(data);
	//uint16_t typeSize = XVector_getTypeSize_base(data);
	if (dataSize > regAddress && (dataSize * REGISTERSIZE) >= (regAddress * REGISTERSIZE + regCount* REGISTERSIZE))
	{//开始写入
		char* p= XVector_at_base(data, regAddress);
		for (size_t i=0; i < regCount* REGISTERSIZE; i++)
		{
			p[i] = writeArray[i];
		}
		return true;
	}
	return false;
}

bool XModbusRegisterHandler_read(XModbusRegisterHandler* regFunc, uint16_t regAddress, uint16_t regCount, char* readArray, uint16_t readArraySize)
{
	if (regFunc == NULL || regFunc->parent.data == NULL || regCount==0|| readArray == NULL || readArraySize == 0)
		return false;
	//检查输入缓冲区是否够大
	if(REGISTERSIZE* regCount> readArraySize)
		return false;
	
	XVector* data = regFunc->parent.data;
	uint32_t dataSize = XVector_getSize_base(data);
	//检查寄存器地址和数量是否合法
	if (dataSize < (regAddress+ regCount))
		return false;
	//开始拷贝数据到读取缓冲区
	for (size_t i = 0; i < regCount; i++)
	{
		*(((uint16_t*)readArray) + i) = *XModbusRegisterHandler_at(regFunc, regAddress+i);
	}
	return true;
}

uint16_t* XModbusRegisterHandler_at(XModbusRegisterHandler* regFunc, uint16_t regAddress)
{
	if (regFunc == NULL || regFunc->parent.data == NULL);
		return NULL;
	return XVector_at_base(regFunc->parent.data, regAddress);
}

XModbusException XModbusRegisterHandler_0x03_RTU_masterRecvHandCallFunc(XModbus* modbus, XModbusFrame* frameData, XModbusFunctionHandler* FunctionHandler)
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
XModbusException XModbusRegisterHandler_0x03_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrame* recvFrame, XModbusFunctionHandler* FunctionHandler)
{
	if (modbus==NULL|| recvFrame == NULL || FunctionHandler == NULL)
		return MB_EX_ILLEGAL_DATA_ADDRESS;
	XModbusRegisterHandler* regFunc = FunctionHandler->data;
	XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
	XModbusFrame* sendFrame = XModbusFrame_create(modbus->m_mode);
	XVector* data = regFunc->parent.data;//寄存器数据
	if ((((rtu->regAddress) + (rtu->regCount)) <= XContainerSize(data)) && ((rtu->regCount) > 0))
	{
		void* readStart = XVector_at_base(data, rtu->regAddress);//寄存器数据缓冲区
		XModbusFrameRTU_setFrameData_0x03_reply(sendFrame, rtu->address, readStart, rtu->regCount);
	}
	else
	{//参数有问题
		XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->regAddress, MB_FUNC_READ_HOLDING_REGISTER, MB_EX_ILLEGAL_DATA_ADDRESS);
	}
	XModbus_sendFrame_base(modbus, sendFrame);
	//printf("读取保持寄存器\n");

	return MB_EX_NONE;
}
XModbusException XModbusRegisterHandler_0x04_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrame* recvFrame, XModbusFunctionHandler* FunctionHandler)
{
	if (modbus == NULL || recvFrame == NULL || FunctionHandler == NULL)
		return MB_EX_ILLEGAL_DATA_ADDRESS;
	XModbusRegisterHandler* regFunc = FunctionHandler->data;
	XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
	if (rtu == NULL)
		return;
	XModbusFrame* sendFrame = XModbusFrame_create(modbus->m_mode);
	XVector* data = regFunc->parent.data;//寄存器数据
	if ((((rtu->regAddress) + (rtu->regCount)) <= XContainerSize(data)) && ((rtu->regCount) > 0))
	{
		void* readStart = XVector_at_base(data, rtu->regAddress);//寄存器数据缓冲区
		XModbusFrameRTU_setFrameData_0x04_reply(sendFrame, rtu->address, readStart, rtu->regCount);
	}
	else
	{//参数有问题
		XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->regAddress, MB_FUNC_READ_INPUT_REGISTER, MB_EX_ILLEGAL_DATA_ADDRESS);
	}
	XModbus_sendFrame_base(modbus, sendFrame);
	//printf("读取输入寄存器\n");
	
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
XModbusException XModbusRegisterHandler_0x06_RTU_slaveRecvHandCallFunc(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusFunctionHandler* FunctionHandler)
{
	if (modbus == NULL || recvFrame == NULL || FunctionHandler == NULL)
		return MB_EX_ILLEGAL_DATA_ADDRESS;
	XModbusRegisterHandler* regFunc = FunctionHandler;
	XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
	if (rtu==NULL)
		return MB_EX_ILLEGAL_FUNCTION;

	XVector* sendFrame =XVector_Create(uint8_t);
	if (rtu->data != NULL)
	{
		if (XModbusRegisterHandler_write_uint16_t(regFunc, rtu->regAddress, XVector_At_Base(rtu->data, 0, uint16_t)))
		{//写入成功 将数据帧再次发送回去
			XVector_swap_base(recvFrame->frameData, sendFrame);
		}
		else
		{
			XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->regAddress, MB_FUNC_WRITE_REGISTER, MB_EX_SLAVE_DEVICE_FAILURE);//写入失败设备故障
		}
	}
	else
	{//参数有问题
		XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->regAddress, MB_FUNC_WRITE_REGISTER, MB_EX_ILLEGAL_DATA_ADDRESS);
	}
	XModbus_sendFrame_base(modbus, sendFrame);
	return MB_EX_NONE;
}

XModbusException XModbusRegisterHandler_0x10_RTU_slaveRecvHandCallFunc(XModbus* modbus, XModbusFrame* recvFrame, XModbusFunctionHandler* FunctionHandler)
{
	if (modbus == NULL || recvFrame == NULL || FunctionHandler == NULL)
		return MB_EX_ILLEGAL_DATA_ADDRESS;
	XModbusRegisterHandler* regFunc = FunctionHandler->data;
	XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
	if (rtu == NULL)
		return;
	//获取寄存器地址
	uint16_t regAddress = rtu->regAddress;
	//寄存器数量
	uint16_t regCount = rtu->regCount;

	XVector* data = regFunc->parent.data;//寄存器数据
	XModbusFrame* sendFrame = XModbusFrame_create(modbus->m_mode);
	if (rtu->data!=NULL)
	{
		void* readStart = XVector_at_base(data, regAddress);//寄存器数据缓冲区
		memcpy(readStart, XVector_front_base(rtu->data), XContainerSize(rtu->data));//写入数据
		XModbusFrameRTU_setFrameData_0x10_reply(sendFrame, rtu->regAddress, regAddress, regCount);
	}
	else
	{//参数有问题
		XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->regAddress, MB_FUNC_WRITE_MULTIPLE_REGISTERS, MB_EX_ILLEGAL_DATA_ADDRESS);
	}
	XModbus_sendFrame_base(modbus, sendFrame);
	//printf("写多个寄存器\n");

	return MB_EX_NONE;
}
