#include "XModbusFrame_RTU.h"
#if MB_RTU_ENABLED
#include"XByteArray.h"
#include "XModbusFrame.h"
#include "XModbusProto.h"
#include "XCrc.h"
#include "XAlgorithm.h"
#include <string.h>
//CRC16校验设置  其他数据必须都设置好了,并且预留了两字节的CRC空间
static XModbusFrame_set16Data(XByteArray* v)
{
	size_t size = XVector_size_base(v) - MB_SER_PDU_SIZE_CRC;
	//设置crc校验
	uint16_t crc16 = XCrc_get16(XContainerDataPtr(v), size);
	XCrc_set16Data(XVector_at_base(v, size), crc16, 0);
}
//8字节数据帧设置
static void setRtuDataFrame_8(XByteArray* frame, uint8_t address, uint8_t funcCode, uint16_t dataOne, uint16_t dataTwo)
{
	//主机地址(1)+功能码(1)+数据(2)+数据(2)+crc16(2)
	if (frame == NULL)
		return;
	//数据(2)+数据(2)
	XModbusFrameRTU_initDataFrame(frame, address, funcCode, 2+2);
	XByteArray* v = frame;
	//第一个双字节数据
	memcpy(XVector_at_base(v, 2), &dataOne, 2);
	//设置寄存器数据
	memcpy(XVector_at_base(v, 4), &dataTwo, 2);

	XModbusFrame_set16Data(v);
	/*
	XString* str = XModbusFrameRTU_to16HexString(frame);
	printf("当前帧:%s\n", XString_c_str(str));
	//            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
	XString_delete_base(str);
	*/
}
//可变字节数据帧 读取线圈 响应帧
static void setDataFrame_readCoils_reply(XByteArray* frame, uint8_t address, uint8_t funcCode, uint8_t* coilsData, const uint16_t coilsCount)
{
	//主机地址(1)+功能码(1)+数据字节数目(1)+数据()+crc16(2)
	if (frame == NULL)
		return;
	uint8_t coilsSize = (coilsCount % 8 == 0) ? (coilsCount / 8) : ((coilsCount / 8) + 1);
	/*uint8_t coilsSize = (coilsCount / 8) +(coilsCount % 8 == 0);*/
	//数据字节数目(1)+数据(?)
	XModbusFrameRTU_initDataFrame(frame, address, funcCode, 1 + coilsSize);
	XByteArray* v = frame;
	//设置返回的字节数量
	XVector_At_Base(v, 2, uint8_t) = coilsSize;
	if (coilsData > 0)
		memcpy(XVector_at_base(v, 3), coilsData, coilsSize);
	XModbusFrame_set16Data(v);
}
//可变字节数据帧 读取寄存器 响应帧
static void setRtuDataFrame_readReg_reply(XByteArray* frame, uint8_t address, uint8_t funcCode, uint16_t* regData, const uint16_t regCount)
{
	//主机地址(1)+功能码(1)+返回字节数量(1)+数据(2*regCount)+crc16(2)
	if (frame == NULL)
		return;
	//返回字节数量(1) + 数据(2 * regCount)
	XModbusFrameRTU_initDataFrame(frame,address,funcCode, 1 + 2 * regCount);
	XByteArray* v = frame;
	//设置返回的字节数量
	XVector_At_Base(v, 2, uint8_t) = 2 * regCount;
	//准备拷贝寄存器数据
	if (regCount > 0)
		memcpy(XVector_at_base(v, 3), regData, regCount * 2);

	XModbusFrame_set16Data(v);
}

XModbusFrameRTU* XModbusFrameRTU_create()
{
	XModbusFrameRTU* rtu = XMemory_malloc(sizeof(XModbusFrameRTU));
	memset(rtu,0,sizeof(XModbusFrameRTU));
	return rtu;
}

void XModbusFrameRTU_delete(XModbusFrameRTU* data)
{
	if (data)
	{
		if (data->data)
			XVector_delete_base(data->data);
	}
	XMemory_free(data);
}

void XModbusFrameRTU_initDataFrame(XByteArray* frame, uint8_t address, uint8_t funcCode, uint16_t dataSize)
{
	if (frame == NULL)
		return;
	XByteArray* v = frame;
	//主机地址(1)+功能码(1)+数据(dataSIze)+crc16(2)
	XVector_resize_base(v, MB_SER_PDU_PDU_OFF + 1 + dataSize + MB_SER_PDU_SIZE_CRC);
	//frame->pduLength = 1 + dataSize;
	//frame->pPduFramePos = XVector_at_base(v, 1);
	//设置地址
	XVector_At_Base(v, 0, uint8_t) = address;
	//frame->address = address;
	//设置功能码
	XVector_At_Base(v, 1, uint8_t) = funcCode;
	//frame->funcCode = funcCode;
	
}

void XModbusFrameRTU_setFrameData_0x01_request(XByteArray* frame, uint8_t address, uint16_t coilsAddress, const uint16_t coilsCount)
{
	setRtuDataFrame_8(frame,address, MB_FUNC_READ_COILS, SwapEndian16(coilsAddress, 1), SwapEndian16(coilsCount, 1));
}

void XModbusFrameRTU_setFrameData_0x01_reply(XByteArray* frame, uint8_t address, uint8_t* coilsData, const uint16_t coilsCount)
{
	//主机地址(1)+功能码(1)+数据字节数目(1)+数据(?)+crc16(2)
	setDataFrame_readCoils_reply(frame,address, MB_FUNC_READ_COILS, coilsData, coilsCount);
}

void XModbusFrameRTU_setFrameData_0x02_request(XByteArray* frame, uint8_t address, uint16_t discAddress, const uint16_t discCount)
{
	setRtuDataFrame_8(frame, address, MB_FUNC_READ_DISCRETE_INPUTS, SwapEndian16(discAddress, 1), SwapEndian16(discCount, 1));
}

void XModbusFrameRTU_setFrameData_0x02_reply(XByteArray* frame, uint8_t address, uint8_t* discAddress, const uint16_t discCount)
{
	//主机地址(1)+功能码(1)+数据字节数目(1)+数据(?)+crc16(2)
	setDataFrame_readCoils_reply(frame, address, MB_FUNC_READ_DISCRETE_INPUTS, discAddress, discCount);
}

void XModbusFrameRTU_setFrameData_0x03_request(XByteArray* frame, uint8_t address, uint16_t regAddress, const uint16_t regCount)
{
	if(regCount!=0)
	setRtuDataFrame_8(frame, address, MB_FUNC_READ_HOLDING_REGISTER, SwapEndian16(regAddress, 1), SwapEndian16(regCount, 1));
}

void XModbusFrameRTU_setFrameData_0x03_reply(XByteArray* frame, uint8_t address, uint16_t* regData, const uint16_t regCount)
{
	setRtuDataFrame_readReg_reply(frame, address, MB_FUNC_READ_HOLDING_REGISTER, regData, regCount);
}

void XModbusFrameRTU_setFrameData_0x04_request(XByteArray* frame, uint8_t address, uint16_t regAddress, const uint16_t regCount)
{
	setRtuDataFrame_8(frame, address, MB_FUNC_READ_INPUT_REGISTER, SwapEndian16(regAddress, 1), SwapEndian16(regCount, 1));
}

void XModbusFrameRTU_setFrameData_0x04_reply(XByteArray* frame, uint8_t address, uint16_t* regData, uint16_t regCount)
{
	setRtuDataFrame_readReg_reply(frame, address, MB_FUNC_READ_INPUT_REGISTER, regData, regCount);
}

void XModbusFrameRTU_setFrameData_0x05_request(XByteArray* frame, uint8_t address, uint16_t coilsAddress, uint16_t coilsState)
{
	setRtuDataFrame_8(frame, address, MB_FUNC_WRITE_SINGLE_COIL, SwapEndian16(coilsAddress, 1), SwapEndian16(coilsState, 1));
}

void XModbusFrameRTU_setFrameData_0x05_reply(XByteArray* frame, uint8_t address, uint16_t coilsAddress, uint16_t coilsState)
{
	XModbusFrameRTU_setFrameData_0x05_request(frame,address,coilsAddress,coilsAddress);
}

void XModbusFrameRTU_setFrameData_0x06_request(XByteArray* frame, uint8_t address, uint16_t regAddress, const uint16_t* regData)
{
	if (regData != NULL)
		setRtuDataFrame_8(frame, address, MB_FUNC_WRITE_REGISTER, SwapEndian16(regAddress, 1), *regData);
}

void XModbusFrameRTU_setFrameData_0x06_reply(XByteArray* frame, uint8_t address, uint16_t regAddress, const uint16_t* regData)
{
	XModbusFrameRTU_setFrameData_0x06_request(frame, address, regAddress, regData);
}

void XModbusFrameRTU_setFrameData_0x10_request(XByteArray* frame, uint8_t address, uint16_t regAddress, uint16_t regCount, uint16_t* regData)
{
	//主机地址(1)+功能码(1)+寄存器起始地址(2)+寄存器数量(2)+数据(2*regCount)+crc16(2)
	if (frame == NULL)
		return;
	//寄存器起始地址(2)+寄存器数量(2)+数据(2*regCount)
	XModbusFrameRTU_initDataFrame(frame, address, MB_FUNC_WRITE_MULTIPLE_REGISTERS, 2 + 2+ 2 * regCount);
	XByteArray* v = frame;
	//写入 寄存器起始地址
	XVector_At_Base(v,2, uint16_t)=SwapEndian16(regAddress,1);
	//写入 寄存器数量
	XVector_At_Base(v, 4, uint16_t) = SwapEndian16(regCount, 1);
	//开始写入数据
	if(regCount>0)
		memcpy(XVector_at_base(v, 6 ), regData, 2 * regCount);
	//CRC校验
	XModbusFrame_set16Data(v);
}

void XModbusFrameRTU_setFrameData_0x10_reply(XByteArray* frame, uint8_t address, uint16_t regAddress, uint16_t regCount)
{
	//主机地址(1)+功能码(1)+寄存器起始地址(2)+已经写入的寄存器数量(2)+crc16(2)
	setRtuDataFrame_8(frame, address, MB_FUNC_WRITE_MULTIPLE_REGISTERS, SwapEndian16(regAddress,1), SwapEndian16(regCount, 1));
}

void XModbusFrameRTU_setFrameData_0x0F_request(XByteArray* frame, uint8_t address, uint16_t coilsAddress, uint16_t coilsCount, uint8_t* coilsData)
{
	//主机地址(1)+功能码(1)+起始线圈地址(2)+线圈数量(2)+数据字节数(1)+数据()+crc16(2)
	if (frame == NULL)
		return;
	uint8_t coilsSize = (coilsCount % 8 == 0) ? (coilsCount / 8) : ((coilsCount / 8) + 1);
	//起始线圈地址(2)+线圈数量(2)+数据字节数(1)+数据()
	XModbusFrameRTU_initDataFrame(frame, address, MB_FUNC_WRITE_MULTIPLE_COILS, 2 + 2 + 1+ coilsSize);
	XByteArray* v = frame;
	//写入 寄存器起始地址
	XVector_At_Base(v, 2, uint16_t) = SwapEndian16(coilsAddress, 1);
	//写入 寄存器数量
	XVector_At_Base(v, 4, uint16_t) = SwapEndian16(coilsCount, 1);
	//写入数据字节数
	XVector_At_Base(v, 6, uint8_t) = coilsSize;
	//开始写入数据
	if (coilsData > 0)
		memcpy(XVector_at_base(v, 7), coilsData, coilsSize);
	//CRC校验
	XModbusFrame_set16Data(v);
}

void XModbusFrameRTU_setFrameData_0x0F_reply(XByteArray* frame, uint8_t address, uint16_t coilsAddress, uint16_t coilsCount)
{
	//主机地址(1)+功能码(1)+线圈起始地址(2)+已经写入的线圈数量(2)+crc16(2)
	setRtuDataFrame_8(frame, address, MB_FUNC_WRITE_MULTIPLE_COILS, SwapEndian16(coilsAddress, 1), SwapEndian16(coilsCount, 1));
}
//解析帧数据
static bool  parseFrameData(XModbusFrameRTU* rtu, XByteArray* data)
{
	if (rtu == NULL || data == NULL)
		return false;
	//开始解析RTU数据
	size_t dataSize = XContainerSize(data);
	uint8_t* pData = (uint8_t*)XContainerDataPtr(data);
	// 校验帧长度和CRC（最小长度4字节，CRC正确）
	if ((dataSize >= MB_SER_PDU_SIZE_MIN) && (XCrc_get16(pData, dataSize) == 0)) {
		//printf("校验通过\n");
		rtu->address = XModbusFrameRTU_parseAddress(data);
		if ((rtu->address != MB_ADDRESS_BROADCAST) && (rtu->address > MB_ADDRESS_MAX || rtu->address < MB_ADDRESS_MIN))
			return false;
		rtu->funcCode = XModbusFrameRTU_parseFuncCode(data);
		rtu->crc16 = SwapEndian16(XVector_At_Base(data, dataSize - MB_SER_PDU_SIZE_CRC, uint16_t), 0);
		return true;
	}
	return false;
}
//解析0x01响应头
static void XModbusFrameRTU_parse0x01_reply(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	if (rtu == NULL || frameData == NULL)
		return;
	uint8_t len = XVector_At_Base(frameData,2,uint8_t);//获取返回的字节数
	XByteArray* data = rtu->data;
	if (len)
	{
		if (data == NULL)
			rtu->data =XByteArray_create(0);
		data = rtu->data;
		if (data == NULL)
			assert(data);//新建数组失败了
		XVector_resize_base(data, len);
		memcpy(XContainerDataPtr(data), XVector_at_base(frameData, 3), len);//拷贝线圈状态数据
	}
	else if (data != NULL)//释放数据
	{
		XVector_delete_base(data);
		rtu->data = NULL;
	}
	
}
//解析0x01请求头
static void XModbusFrameRTU_parse0x01_request(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	if (rtu == NULL || frameData == NULL)
		return;
	rtu->coilsAddress= SwapEndian16(XVector_At_Base(frameData, 2, uint16_t),1);//获取起始线圈地址
	rtu->coilsCount= SwapEndian16(XVector_At_Base(frameData, 4, uint16_t), 1);//获取线圈数量
}
//解析0x02响应头
static void XModbusFrameRTU_parse0x02_reply(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x01_reply(rtu,frameData);
}
//解析0x02请求头
static void XModbusFrameRTU_parse0x02_request(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x01_request(rtu,frameData);
}
//解析0x03响应头
static void XModbusFrameRTU_parse0x03_reply(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x01_reply(rtu,frameData);
}
//解析0x03请求头
static void XModbusFrameRTU_parse0x03_request(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x01_request(rtu,frameData);
}
//解析0x04响应头
static void XModbusFrameRTU_parse0x04_reply(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x01_reply(rtu, frameData);
}
//解析0x04请求头
static void XModbusFrameRTU_parse0x04_request(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x01_request(rtu, frameData);
}
//解析0x05响应头
static void XModbusFrameRTU_parse0x05_reply(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	if (rtu == NULL || frameData == NULL)
		return;
	rtu->coilsAddress = SwapEndian16(XVector_At_Base(frameData, 2, uint16_t), 1);
	if (rtu->data == NULL)
		rtu->data =XByteArray_create(0);
	XByteArray* data = rtu->data;
	if (data == NULL)
		assert(data);//新建数组失败了
	XVector_resize_base(data, 2);
	memcpy(XContainerDataPtr(data), XVector_at_base(frameData, 4), 2);//拷贝寄存器数据
}
//解析0x05请求头
static void XModbusFrameRTU_parse0x05_request(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x05_reply(rtu, frameData);
}
//解析0x06响应头
static void XModbusFrameRTU_parse0x06_reply(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x05_reply(rtu, frameData);
}
//解析0x06请求头
static void XModbusFrameRTU_parse0x06_request(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x06_reply(rtu,frameData);
}
//解析0x10响应头
static void XModbusFrameRTU_parse0x10_reply(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x01_request(rtu,frameData);
}
//解析0x10请求头
static void XModbusFrameRTU_parse0x10_request(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	if (rtu == NULL || frameData == NULL)
		return;
	rtu->regAddress = SwapEndian16(XVector_At_Base(frameData, 2, uint16_t), 1);//获取寄存器地址
	rtu->regCount = SwapEndian16(XVector_At_Base(frameData, 4, uint16_t), 1);//获取寄存器数量
	uint8_t len = XVector_At_Base(frameData, 6, uint8_t);//获取返回的字节数
	if ((len > 0) && (((rtu->regCount) * 2) == len))
	{
		if (rtu->data == NULL)
			rtu->data =XByteArray_create(0);
		XByteArray* data = rtu->data;
		if (data == NULL)
			assert(data);//新建数组失败了
		XVector_resize_base(data, len);
		memcpy(XContainerDataPtr(data), XVector_at_base(frameData, 7), len);//拷贝寄存器数据
	}
	else if(rtu->data!=NULL)
	{
		XVector_delete_base(rtu->data);
		rtu->data = NULL;
	}
}
//解析0x0F响应头
static void XModbusFrameRTU_parse0x0F_reply(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x01_request(rtu, frameData);
}
//解析0x0F请求头
static void XModbusFrameRTU_parse0x0F_request(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	XModbusFrameRTU_parse0x10_request(rtu, frameData);
}
//解析0x8X响应头
static void XModbusFrameRTU_parse0x8X_reply(XModbusFrameRTU* rtu, XByteArray* frameData)
{
	if (rtu == NULL || frameData == NULL)
		return;
	rtu->exception = XVector_At_Base(frameData, 2, uint8_t);//获取错误码
}
void XModbusFrameRTU_setFrameData_0x8X_reply(XByteArray* frame, uint8_t address, uint8_t funcCode, XModbusException exception)
{
	//主机地址(1)+异常功能码(1)+错误码(1)+crc16(2)
	if (frame == NULL)
		return;
	//数据(2)+数据(2)
	XModbusFrameRTU_initDataFrame(frame, address, funcCode| MB_FUNC_ERROR,1);
	XByteArray* v = frame;
	XVector_At_Base(v,2,uint8_t)= exception;
	XModbusFrame_set16Data(v);
}
bool XModbusFrameRTU_parseData_request(XModbusFrameRTU* frame, XByteArray* frameData)
{
	if (frame == NULL || frameData == NULL)
		return false;
	if (!parseFrameData(frame, frameData))
		return false;//初步解析失败没必要在解析下去了
	switch (frame->funcCode)
	{
	case MB_FUNC_READ_COILS:XModbusFrameRTU_parse0x01_request(frame, frameData); break;
	case MB_FUNC_READ_DISCRETE_INPUTS:XModbusFrameRTU_parse0x02_request(frame, frameData); break;
	case MB_FUNC_READ_HOLDING_REGISTER:XModbusFrameRTU_parse0x03_request(frame, frameData); break;
	case MB_FUNC_READ_INPUT_REGISTER:XModbusFrameRTU_parse0x04_request(frame, frameData); break;
	case MB_FUNC_WRITE_SINGLE_COIL:XModbusFrameRTU_parse0x05_request(frame, frameData); break;
	case MB_FUNC_WRITE_REGISTER:XModbusFrameRTU_parse0x06_request(frame, frameData); break;
	case MB_FUNC_WRITE_MULTIPLE_REGISTERS:XModbusFrameRTU_parse0x10_request(frame, frameData); break;
	case MB_FUNC_WRITE_MULTIPLE_COILS:XModbusFrameRTU_parse0x0F_request(frame, frameData); break;
	default:
		break;
	}
	return true;
}
bool XModbusFrameRTU_parseData_reply(XModbusFrameRTU* frame, XByteArray* frameData)
{
	if (frame == NULL || frameData == NULL)
		return false;
	if (!parseFrameData(frame, frameData))
		return false;//初步解析失败没必要在解析下去了
	switch (frame->funcCode)
	{
	case MB_FUNC_READ_COILS:XModbusFrameRTU_parse0x01_reply(frame, frameData); break;
	case MB_FUNC_READ_DISCRETE_INPUTS:XModbusFrameRTU_parse0x02_reply(frame, frameData); break;
	case MB_FUNC_READ_HOLDING_REGISTER:XModbusFrameRTU_parse0x03_reply(frame, frameData); break;
	case MB_FUNC_READ_INPUT_REGISTER:XModbusFrameRTU_parse0x04_reply(frame, frameData); break;
	case MB_FUNC_WRITE_SINGLE_COIL:XModbusFrameRTU_parse0x05_reply(frame, frameData); break;
	case MB_FUNC_WRITE_REGISTER:XModbusFrameRTU_parse0x06_reply(frame, frameData); break;
	case MB_FUNC_WRITE_MULTIPLE_REGISTERS:XModbusFrameRTU_parse0x10_reply(frame, frameData); break;
	case MB_FUNC_WRITE_MULTIPLE_COILS:XModbusFrameRTU_parse0x0F_reply(frame, frameData); break;
	default:
	if (frame->funcCode & MB_FUNC_ERROR)
		XModbusFrameRTU_parse0x8X_reply(frame, frameData);
		break;
	}
	return true;
}

uint8_t XModbusFrameRTU_parseAddress(XByteArray* frame)
{
	if (frame && !XVector_isEmpty_base(frame))
		return XVector_At_Base(frame, 0, uint8_t);
	return 0xFF;
}

uint8_t XModbusFrameRTU_parseFuncCode(XByteArray* frame)
{
	if (frame && !XVector_isEmpty_base(frame))
		return XVector_At_Base(frame, MB_SER_PDU_PDU_OFF, uint8_t);
	return 0;
}

XString* XModbusFrameRTU_to16HexString(XByteArray* frame)
{
	if (frame && !XVector_isEmpty_base(frame))
	{
		XByteArray* vector = frame;
		XString* str = XString_create(NULL);
		char buff[10];
		//for (XVector_iterator* it = XVector_begin(vector); it != XVector_end(vector); it = XVector_iterator_add(vector, it))
		for_each_iterator(vector, XVector, it)
		{
			sprintf(buff, "%02X ", *((uint8_t*)XVector_iterator_data(&it)));
			XString_append_base(str, buff);
		}
		XString_pop_back_base(str);
		return str;
	}
	return NULL;
}
#endif