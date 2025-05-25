#ifndef XIODEVICE_H
#define XIODEVICE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
//XIODevice虚函数表
extern XVtable* XIODeviceVtable;
#define XIODEVICE_VTABLE_SIZE (12)       //XIODevice容器虚函数表大小
//XContainerObject虚函数表枚举
enum XIODeviceVtableEnum
{
	EXIODevice_Free,
	EXIODevice_IsOpen,
	EXIODevice_Open,
	EXIODevice_Write,
	EXIODevice_WriteFull,
	EXIODevice_Read,
	EXIODevice_Receive,
	EXIODevice_Close,
	EXIODevice_Poll,
	EXIODevice_SetWriteBuffer,
	EXIODevice_SetReadBuffer,
	EXIODevice_SetDevice,
};
typedef struct XCircularQueue XCircularQueue;
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
typedef size_t(*XIOBufferEmpty)(XIODevice* io,char* data, size_t size);//缓冲区空
typedef size_t(*XIOBufferFull)(XIODevice* io,const char* data,size_t size);//缓冲区满
typedef bool (*XIODeviceOpen)(XIODevice* io, XIODeviceBase mode);//打开IO设备
typedef void (*XIODeviceClose)(XIODevice* io);//关闭IO,释放资源
//IO设备接口
typedef struct XIODevice_PortFunc
{
	union {
		void (*writeBufferFull_funcPointer)(XIODevice* io, XCircularQueue*queue);//写入缓冲区满
		XIOBufferFull     writeData_funcPointer;//无缓冲区直接写入数据
	};
	union{
		void (*readBufferEmpty_funcPointer)(XIODevice* io, XCircularQueue* queue);//读取缓冲区空
		XIOBufferEmpty   readData_funcPointer;//无缓冲区读取数据
	};
	XIODeviceOpen  open_funcPointer;//打开IO设备
	XIODeviceClose   close_funcPointer;//关闭IO
	void (*poll_funcPointer)(XIODevice* io);//设备轮询
}XIODevice_PortFunc;
//IO设备初始化接口
typedef XIODevice_PortFunc XIODevice_PortFuncInit;
//IO设备
typedef struct XIODevice
{
	XClass m_parent;//继承类
	void* device;//设备
	uint16_t m_mode;//打开模式
	XCircularQueue* m_writeBuffer;//写入缓冲区
	XCircularQueue* m_readBuffer;//读取缓冲区
	/* ----------------------- 接口指针-------------------------------------*/
	XIODevice_PortFunc m_port;//接口
}XIODevice;
//初始化类
void XIODevice_class_init();
XIODevice* XIODevice_new(XIODevice_PortFuncInit* port);
void XIODevice_init(XIODevice* io, XIODevice_PortFuncInit* port);
void XIODevice_free(XIODevice* io);
void XIODevice_setWriteBuffer(XIODevice* io,size_t count);
void XIODevice_setReadBuffer(XIODevice* io, size_t count);
void XIODevice_setDevice(XIODevice* io, void* device);
size_t XIODevice_write(XIODevice* io,const char* data, size_t maxSize);//写入
size_t XIODevice_read(XIODevice* io,char* data, size_t maxSize);//读取
//接收数据从硬件接收数据到缓冲区
size_t XIODevice_receive(XIODevice* io,const char* data, size_t size);
bool XIODevice_isOpen(XIODevice* io);
bool XIODevice_open(XIODevice* io, XIODeviceBase mode);
void XIODevice_close(XIODevice* io);
void XIODevice_poll(XIODevice* io);
size_t XIODevice_writeFull(XIODevice* io);//将剩余的数据刷入设备


#ifdef __cplusplus
}
#endif
#endif