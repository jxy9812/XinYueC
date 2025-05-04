#include"XModbusRegisterFunc.h"
#include"XMemory.h"
#include"XVector.h"
#include"XModbusFrameData.h"
#include"XModbusFunctionHandler.h"
#include"XModbus.h"
#include"XModbusRtu.h"
#include"XCrc.h"
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

	//printf("读取保持寄存器\n");
	
	//指向功能码后的寄存器地址数据
	UCHAR* p = recvFrame->pPduFramePos + 1;
	//获取寄存器地址
	uint16_t regAddress = ReadData16(p);
	//需要读取的寄存器数量
	uint16_t regCount = ReadData16(p+2);
	
	XVector* data = regFunc->parent.data;//寄存器数据
	uint32_t dataSize = XVector_size(data);//寄存器个数
	//判断地址和数量是否合法  一次最多127个寄存器
	if (dataSize < (regAddress + regCount)|| regCount>127)
		return MB_EX_ILLEGAL_DATA_ADDRESS;
	//开始准备发送数据
	XModbusFrameData* sendFrame = XModbusFrameData_new();
	XVector* recvVector = recvFrame->dataFrame;
	XVector* sendVector = sendFrame->dataFrame;
	XVector_resize(sendVector, MB_SER_PDU_PDU_OFF+1+1+ regCount * REGISTERSIZE+MB_SER_PDU_SIZE_CRC);
	//拷贝地址和功能码
	XVector_At(sendVector,0,uint16_t)= XVector_At(recvVector, 0, uint16_t);

	uint8_t len = regCount * REGISTERSIZE;//字节数据量
	//设置数据字节数
	XVector_At(sendVector, 2, uint8_t) = len;
	//拷贝寄存器的数据
	{
		char* readStart = XVector_at(data, regAddress);
		char* writeStart = XVector_at(sendVector, 3);
		for (size_t i = 0; i < len; i++)
		{
			writeStart[i] = readStart[i];
		}
	}
	//设置crc16校验码
	uint16_t crc16 = XCrc_get16(XVector_begin(sendVector), XVector_size(sendVector)- MB_SER_PDU_SIZE_CRC);
	XCrc_set16Data(XVector_at(sendVector, XVector_size(sendVector) - MB_SER_PDU_SIZE_CRC), crc16);
	sendFrame->pPduFramePos = XVector_at(sendVector, MB_SER_PDU_PDU_OFF);
	sendFrame->pduLength = 1 + 1 + len;
	XModbus_sendData(modbus, sendFrame);

	

	////拷贝地址
	//XVector_Push_Back(sendVector,UCHAR, XModbusFrameData_getRtuAddress(recvFrame));
	////拷贝功能码
	//XVector_Push_Back(sendVector, UCHAR, XModbusFrameData_getRtuFuncCode(recvFrame));
	////设置数据字节数
	//uint8_t len = regCount * REGISTERSIZE;
	//XVector_push_back(sendVector, &len);
	////拷贝寄存器的数据
	//char* p=XVector_at(data, regAddress);
	//for (size_t i = 0; i < len; i++)
	//{
	//	XVector_push_back(sendVector,p+i);
	//}
	//添加crc校验码

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

	//printf("写保持寄存器\n");
	//XString* str = XModbusFrameData_to16HexString(recvFrame);
	//printf("完整:%s\n", XString_c_str(str));
	////            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
	//XString_free(str);
	
	//指向功能码后的寄存器地址数据
	UCHAR* p = recvFrame->pPduFramePos+1;
	//获取寄存器地址
	uint16_t regAddress = ReadData16(p);
	uint16_t value = *((uint16_t*)(p + 2));
	//uint16_t value = (*(p + 2)) << 8 | (*(p + 3));;
	if (XModbusRegisterFunc_write_uint16_t(regFunc, regAddress, value))
	{//写入成功 将数据帧再次发送回去
		XModbusFrameData* sendFrame = XModbusFrameData_new();
		XVector_swap(recvFrame->dataFrame, sendFrame->dataFrame);
		XModbus_sendData(modbus, sendFrame);
	}
	return MB_EX_NONE;
}
