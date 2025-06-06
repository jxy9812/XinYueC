#include"XSerialPortBase.h"
#include"XQueueBase.h"
//声明
static bool  VXSerialPort_open(XSerialPortBase* serial, XIODeviceBaseMode mode);
static size_t VXSerialPort_read(XIODeviceBase* io, char* data, size_t maxSize);//读取

XVtable* XSerialPortBase_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XSERIALPORT_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XIODeviceBase_class_init());
	//重写
	XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Open,VXSerialPort_open);
	//XVTABLE_OVERLOAD_DEFAULT(EXIODeviceBase_Read,VXSerialPort_read);

#if SHOWCONTAINERSIZE
	printf("XSerialPortBase size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

bool VXSerialPort_open(XSerialPortBase* serial, XIODeviceBaseMode mode)
{
	//printf("准备打开\n");
	if (serial == NULL)
		return false;
	/*serial->m_baudRate = baudRate;
	serial->m_parity = parity;
	serial->m_portNum = portNum;*/
	//调用父类
	return XVtableGetFunc(XIODeviceBase_class_init(), EXIODeviceBase_Open,bool(*)(XIODeviceBase*, XIODeviceBaseMode))(serial, mode);
	//return XIODeviceBase_open_base(serial, mode);
}

size_t VXSerialPort_read(XIODeviceBase* io, char* data, size_t maxSize)
{
	return 0;
}
