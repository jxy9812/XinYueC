#ifndef XIODEVICEBASE_H
#define XIODEVICEBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
#define XIODEVICEBASE_VTABLE_SIZE		(XCLASS_VTABLE_SIZE+12)       //XIODeviceBase虚函数表大小
//XContainerObject虚函数表枚举
enum XIODeviceBaseVtableEnum
{
	EXIODeviceBase_Open = XCLASS_VTABLE_SIZE,
	EXIODeviceBase_Write,
	EXIODeviceBase_WriteFull,
	EXIODeviceBase_Read,
	EXIODeviceBase_GetBytesAvailable,
	EXIODeviceBase_GetBytesToWrite,
	EXIODeviceBase_AtEnd,
	EXIODeviceBase_Close,
	EXIODeviceBase_Poll,
	EXIODeviceBase_SetWriteBuffer,
	EXIODeviceBase_SetReadBuffer,
	EXIODeviceBase_SetDevice,
};
typedef struct XCircularQueue XCircularQueue;
typedef enum /*XIODeviceBase*/
{
	XIODeviceBase_NotOpen= 0x0000,//设备未打开
	XIODeviceBase_ReadOnly= 0x0001,//设备以只读方式打开
	XIODeviceBase_WriteOnly= 0x0002,//设备以只写方式打开
	XIODeviceBase_ReadWrite= XIODeviceBase_ReadOnly | XIODeviceBase_WriteOnly, //设备以读写方式打开
	XIODeviceBase_Append= 0x0004,//设备以追加模式打开
	XIODeviceBase_Truncate= 0x0008,//如果可能的话，在打开设备之前会截断设备
	XIODeviceBase_Text= 0x0010,//在读取时，行尾终止符会被转换为 \n。在写入时，行终止符会被转换为本地编码，例如在 Windows 系统上会转换为 \r\n		   _
	XIODeviceBase_Unbuffered= 0x0020,//设备中的任何缓冲区都会被绕过
	XIODeviceBase_NewOnly= 0x0040,//如果要打开的文件已经存在，则操作失败。只有当件不存在时才创建并打开文件
	XIODeviceBase_ExistingOnly= 0x0080,//如果要打开的文件不存在，则操作失败。这个志必须与 ReadOnly（只读）、WriteOnly（只写）或 ReadWrite（读写）一起指定
}XIODeviceBaseMode;
//IO设备
typedef struct XIODeviceBase
{
	XClass m_parent;//继承类
	void* device;//设备
	uint16_t m_mode;//打开模式
	XCircularQueue* m_writeBuffer;//写入缓冲区
	XCircularQueue* m_readBuffer;//读取缓冲区
}XIODeviceBase;
XVtable* XIODeviceBase_class_init();
XIODeviceBase* XIODeviceBase_create(XVtable* vtable);
void XIODeviceBase_init(XIODeviceBase* io, XVtable* vtable);
#define XIODeviceBase_free_base	XClass_free_base
void XIODeviceBase_setWriteBuffer_base(XIODeviceBase* io,size_t count);
void XIODeviceBase_setReadBuffer_base(XIODeviceBase* io, size_t count);
void XIODeviceBase_setDevice_base(XIODeviceBase* io, void* device);
size_t XIODeviceBase_write_base(XIODeviceBase* io,const char* data, size_t maxSize);//写入 有缓冲区不阻塞
size_t XIODeviceBase_read_base(XIODeviceBase* io,char* data, size_t maxSize);		//读取 超出最大读取会阻塞
//获取可供读取的字节数
size_t XIODeviceBase_getBytesAvailable_base(XIODeviceBase* io);
//查询当前 待写入设备的数据量（即尚未完成写入操作的字节数）
size_t XIODeviceBase_getBytesToWrite_base(XIODeviceBase* io);
//是否到达末尾
bool XIODeviceBase_atEnd_base(XIODeviceBase* io);
bool XIODeviceBase_isOpen(XIODeviceBase* io);
//打开设备		必须要重载
bool XIODeviceBase_open_base(XIODeviceBase* io, XIODeviceBaseMode mode);
//关闭设备      需重载
bool XIODeviceBase_close_base(XIODeviceBase* io);
//轮询设备      需重载
void XIODeviceBase_poll_base(XIODeviceBase* io);
//将剩余的数据刷入设备   需重载
size_t XIODeviceBase_writeFull_base(XIODeviceBase* io);


#ifdef __cplusplus
}
#endif
#endif