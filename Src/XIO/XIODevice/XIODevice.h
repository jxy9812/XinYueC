#ifndef XIODEVICE_H
#define XIODEVICE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XVector.h"
typedef enum /*XIODeviceBase*/
{
	XIODeviceBase_NotOpen= 0x0000,//设备未打开
	XIODeviceBase_ReadOnly= 0x0001,//设备以只读方式打开
	XIODeviceBase_WriteOnly= 0x0002,//设备以只写方式打开
	XIODeviceBase_ReadWrite= XIODeviceBase_ReadOnly | XIODeviceBase_WriteOnly, //备以读写方式打开
	XIODeviceBase_Append= 0x0004,//设备以追加模式打开
	XIODeviceBase_Truncate= 0x0008,//如果可能的话，在打开设备之前会截断设备
	XIODeviceBase_Text= 0x0010,//在读取时，行尾终止符会被转换为 \n。在写入时，行终止符会被转换为本地编码，例如在 Windows 系统上会转换为 \r\n		   _
	XIODeviceBase_Unbuffered= 0x0020,//设备中的任何缓冲区都会被绕过
	XIODeviceBase_NewOnly= 0x0040,//如果要打开的文件已经存在，则操作失败。只有当件不存在时才创建并打开文件
	XIODeviceBase_ExistingOnly= 0x0080,//如果要打开的文件不存在，则操作失败。这个志必须与 ReadOnly（只读）、WriteOnly（只写）或 ReadWrite（读写）一起指定
}XIODeviceBase;
typedef struct XIODevice XIODevice;
typedef bool (*XIODeviceGetByte)(XIODevice* io, uint8_t* Byte);//获取一个字节
typedef bool (*XIODevicePutByte)(XIODevice* io, uint8_t Byte);//发送一个字节
typedef size_t(*XIOBufferEmpty)(XIODevice* io,char* data, size_t size);//缓冲区空
typedef size_t(*XIOBufferFull)(XIODevice* io,const char* data,size_t size);//缓冲区满
typedef bool (*XIODeviceOpen)(XIODevice* io, XIODeviceBase mode);//打开IO设备
typedef void (*XIODeviceClose)(XIODevice* io);//关闭IO,释放资源
//IO设备接口
typedef struct XIODevice_PortFunc
{
	XIOBufferEmpty   writeBufferEmpty_funcPointer;
	union {
		XIOBufferFull     writeBufferFull_funcPointer;
		XIOBufferFull     writeData_funcPointer;
	};
	union{
		XIOBufferEmpty   readBufferEmpty_funcPointer;
		XIOBufferEmpty   readData_funcPointer;
	};
	XIOBufferFull     readBufferFull_funcPointer;
	//XIODeviceGetByte getByte_funcPointer;//获取一个字节
	//XIODevicePutByte putByte_funcPointer;//发送一个字节
	XIODeviceOpen  open_funcPointer;//打开IO设备
	XIODeviceClose   close_funcPointer;//关闭IO
}XIODevice_PortFunc;
//IO设备
typedef struct XIODevice
{
	uint16_t m_mode;//打开模式
	void* device;//设备
	XVector* m_writeBuffer;//写入缓冲区
	XVector* m_readBuffer;//读取缓冲区
	/* ----------------------- 函数指针-------------------------------------*/
	XIODevice_PortFunc m_port;//接口
}XIODevice;
XIODevice* XIODevice_new(XIODevice_PortFunc* port);
void XIODevice_free(XIODevice* io);
void XIODevice_setWriteBuffer(XIODevice* io,size_t size);
void XIODevice_setReadBuffer(XIODevice* io, size_t size);
size_t XIODevice_write(XIODevice* io,const char* data, size_t maxSize);//写入
size_t XIODevice_writeVector(XIODevice* io, XVector* array);//写入
size_t XIODevice_read(XIODevice* io,char* data, size_t maxSize);//读取
XVector* XIODevice_readVector(XIODevice* io, size_t maxSize);//读取
bool XIODevice_isOpen(XIODevice* io);
bool XIODevice_open(XIODevice* io, XIODeviceBase mode);
void XIODevice_close(XIODevice* io);
size_t XIODevice_writeFull(XIODevice* io);//将剩余的数据刷入设备
#ifdef __cplusplus
}
#endif
#endif