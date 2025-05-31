#include"XSerialPortBase.h"
#include"XCircularQueueAtomic.h"
//声明
static bool VXSerialPort_open(XSerialPortBase* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortBaseParity parity);
static size_t VXSerialPort_read(XIODeviceBase* io, char* data, size_t maxSize);//读取

XVtable* XSerialPortBase_class_init()
{
	static XVtable* XClassVtable = NULL;
	if (XClassVtable)
		return XClassVtable;
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_DEFINITION(XSERIALPORT_VTABLE_SIZE)
	XVTABLE_STACK_INIT(XClassVtable)
#else
	XVTABLE_HEAP_INIT(XClassVtable)
#endif
	//继承的函数
	XVtable_append_vtable(XClassVtable, XIODeviceBase_class_init());
	//重写
	XVtable_At(XClassVtable, EXIODeviceBase_Open) = VXSerialPort_open;
	XVtable_At(XClassVtable, EXIODeviceBase_Read) = VXSerialPort_read;

#if SHOWCONTAINERSIZE
	printf("XSerialPortBase size:%d\n", XVtable_size(XClassVtable));
#endif
	return XClassVtable;
}

bool VXSerialPort_open(XSerialPortBase* serial, XIODeviceBaseMode mode, uint8_t portNum, uint32_t baudRate, XSerialPortBaseParity parity)
{
	//printf("准备打开\n");
	if (serial == NULL)
		return false;
	serial->m_baudRate = baudRate;
	serial->m_parity = parity;
	serial->m_portNum = portNum;
	//调用父类
	return XVtableGetFunc(XIODeviceBase_class_init(), EXIODeviceBase_Open,bool(*)(XIODeviceBase*, XIODeviceBaseMode))(serial, mode);
	//return XIODeviceBase_open_base(serial, mode);
}

size_t VXSerialPort_read(XIODeviceBase* io, char* data, size_t maxSize)
{
	/**************************************************/
	ISNULL(0, "请重载这个函数,这是模板");
	/**************************************************/
	if (io->m_mode & XIODeviceBase_ReadOnly == 0)
		return 0;
	size_t count = 0;
	//if (io->m_readBuffer == NULL)
	//{//没有读取缓冲区
	//	if (io->m_port.readData_funcPointer == NULL)
	//		return 0;
	//	count += io->m_port.readData_funcPointer(io, data, maxSize);
	//}
	//else
	//{
	//	while (XCircularQueue_receive_base(io->m_readBuffer, data + count))
	//	{
	//		++count;
	//		if (count >= maxSize)
	//			break;
	//	}
	//}
	return count;
}
