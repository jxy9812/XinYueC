#include"XModbusRegister.h"
#include"XMemory.h"
#include"XByteArray.h"
#include"XModbusFrame.h"
#include"XModbus.h"
#include<string.h>
//寄存器大小
#define REGISTERSIZE 2
//读取16位数据
#define ReadData16(p) ((*(p)) << 8 | (*(p + 1)))
XModbusRegister* XModbusRegister_create(uint16_t regCount)
{
	if(regCount==0)
		return NULL;
	XModbusRegister* ptr = XMemory_malloc(sizeof(XModbusRegister));
	ptr->parent.data = XVector_create(REGISTERSIZE);
	XVector_resize_base(ptr->parent.data, regCount);
	return ptr;
}

void XModbusRegister_delete(XModbusRegister* pRegHandler)
{
	if (pRegHandler)
	{
		if (pRegHandler->parent.data)
			XVector_delete_base(pRegHandler->parent.data);
		XMemory_free(pRegHandler);
	}
}

bool XModbusRegister_write_uint16_t(XModbusRegister* regFunc, uint16_t regAddress, uint16_t value)
{
	if (regFunc == NULL || regFunc->parent.data == NULL)
		return false;
	XByteArray* data= regFunc->parent.data;
	if(XVector_getSize_base(data)>regAddress)
	{
		XVector_At_Base(data, regAddress, uint16_t) = value;
		//printf("写入的值是:%d\n",value);
		return true;
	}
	return false;
}

bool XModbusRegister_write(XModbusRegister* regFunc, uint16_t regAddress, uint16_t regCount, const uint8_t* writeArray)
{
	if (regFunc == NULL || regFunc->parent.data == NULL|| writeArray==NULL||regCount==0)
		return false;
	XByteArray* data = regFunc->parent.data;
	uint32_t dataSize = XVector_getSize_base(data);
	//uint16_t typeSize = XVector_getTypeSize_base(data);
	if (dataSize > regAddress && (dataSize * REGISTERSIZE) >= (regAddress * REGISTERSIZE + regCount* REGISTERSIZE))
	{//开始写入
		uint8_t* p= XVector_at_base(data, regAddress);
		for (size_t i=0; i < regCount* REGISTERSIZE; i++)
		{
			p[i] = writeArray[i];
		}
		return true;
	}
	return false;
}

bool XModbusRegister_read(XModbusRegister* regFunc, uint16_t regAddress, uint16_t regCount, uint8_t* readArray, uint16_t readArraySize)
{
	if (regFunc == NULL || regFunc->parent.data == NULL || regCount==0|| readArray == NULL || readArraySize == 0)
		return false;
	//检查输入缓冲区是否够大
	if(REGISTERSIZE* regCount> readArraySize)
		return false;
	
	XByteArray* data = regFunc->parent.data;
	uint32_t dataSize = XVector_getSize_base(data);
	//检查寄存器地址和数量是否合法
	if (dataSize < (regAddress+ regCount))
		return false;
	//开始拷贝数据到读取缓冲区
	for (size_t i = 0; i < regCount; i++)
	{
		*(((uint16_t*)readArray) + i) = *XModbusRegister_at(regFunc, regAddress+i);
	}
	return true;
}

uint16_t* XModbusRegister_at(XModbusRegister* regFunc, uint16_t regAddress)
{
	if (regFunc == NULL || regFunc->parent.data == NULL);
		return NULL;
	return XVector_at_base(regFunc->parent.data, regAddress);
}

void XModbusRegister_0x03_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
	if (modbus == NULL || recvFrame == NULL || hand == NULL)
		return;
	XModbusRegister* regFunc = hand;
	XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
	if (rtu == NULL)
		return;

	if (rtu->data != NULL)
	{
		XModbusRegister_write(regFunc, rtu->regAddress,rtu->regCount, XContainerDataPtr(rtu->data));
	}
	else
	{//参数有问题
	}
}
void XModbusRegister_0x04_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
	XModbusRegister_0x03_RTU_masterRecvHandCb(math,modbus,recvFrame,hand);
}
void XModbusRegister_0x06_RTU_masterRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
	if (modbus == NULL || recvFrame == NULL || hand == NULL)
		return;
	XModbusRegister* regFunc = hand;
	XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
	if (rtu == NULL)
		return;
	if (rtu->data != NULL)
	{
		XModbusRegister_write_uint16_t(regFunc, rtu->regAddress, XContainerDataPtr(rtu->data));
	}
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
void XModbusRegister_0x03_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
	if (modbus==NULL|| recvFrame == NULL || hand == NULL)
		return ;
	XModbusRegister* regFunc = hand;
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
		XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_READ_HOLDING_REGISTER, MB_EX_ILLEGAL_DATA_ADDRESS);
	}
	XModbus_sendData_base(modbus, sendFrame);
	//printf("读取保持寄存器\n");
}
void XModbusRegister_0x04_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
	if (modbus == NULL || recvFrame == NULL || hand == NULL)
		return ;
	XModbusRegister* regFunc = hand;
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
		XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_READ_INPUT_REGISTER, MB_EX_ILLEGAL_DATA_ADDRESS);
	}
	XModbus_sendData_base(modbus, sendFrame);
	//printf("读取输入寄存器\n");
	
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
void XModbusRegister_0x06_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
	if (modbus == NULL || recvFrame == NULL || hand == NULL)
		return ;
	XModbusRegister* regFunc = hand;
	XModbusFrameRTU* rtu = (XModbusFrameRTU*)recvFrame->data;
	if (rtu==NULL)
		return ;

	XByteArray* sendFrame =XByteArray_create(0);
	if (rtu->data != NULL)
	{
		if (XModbusRegister_write_uint16_t(regFunc, rtu->regAddress, XVector_At_Base(rtu->data, 0, uint16_t)))
		{//写入成功 将数据帧再次发送回去
			XVector_swap_base(recvFrame->frameData, sendFrame);
		}
		else
		{
			XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_WRITE_REGISTER, MB_EX_SLAVE_DEVICE_FAILURE);//写入失败设备故障
		}
	}
	else
	{//参数有问题
		XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_WRITE_REGISTER, MB_EX_ILLEGAL_DATA_ADDRESS);
	}
	XModbus_sendData_base(modbus, sendFrame);
}

void XModbusRegister_0x10_RTU_slaveRecvHandCb(XModbusRecvMatch* math, XModbus* modbus, XModbusFrame* recvFrame, XModbusDeviceObject* hand)
{
	if (modbus == NULL || recvFrame == NULL || hand == NULL)
		return ;
	XModbusRegister* regFunc = hand;
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
		XModbusFrameRTU_setFrameData_0x10_reply(sendFrame, rtu->address, regAddress, regCount);
	}
	else
	{//参数有问题
		XModbusFrameRTU_setFrameData_0x8X_reply(sendFrame, rtu->address, MB_FUNC_WRITE_MULTIPLE_REGISTERS, MB_EX_ILLEGAL_DATA_ADDRESS);
	}
	XModbus_sendData_base(modbus, sendFrame);
	//printf("写多个寄存器\n");

}
