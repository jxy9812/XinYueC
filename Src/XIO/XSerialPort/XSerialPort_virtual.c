#include"XSerialPort.h"
#include"XCircularQueueAtomic.h"
//声明
static bool VXSerialPort_open(XSerialPort* serial, XIODeviceBase mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity);
static size_t VXSerialPort_read(XIODevice* io, char* data, size_t maxSize);//读取
XVtable* XSerialPortVtable = NULL;
#if VTABLE_ISSTACK
static XVtable vtable;//虚函数类
static void* vtable_data[XSERIALPORT_VTABLE_SIZE];//虚函数数据
#endif

void XSerialPort_class_init()
{
	//仅初始化一次
	if (XSerialPortVtable)
		return;
	//printf("配置虚函数表\n");
#if !VTABLE_ISSTACK
	XSerialPortVtable = XVtable_new();
#else
	XSerialPortVtable = &vtable;
	XVtable_init_stack(&vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));
#if SHOWCONTAINERSIZE
	printf("XSerialPort size:%d\n", XVtable_size(XSerialPortVtable));
#endif
	//继承的函数
	XVtable_append_vtable(XSerialPortVtable, XIODeviceVtable);
	//重写
	XVtable_At(XSerialPortVtable, EXIODevice_Open) = VXSerialPort_open;
	XVtable_At(XSerialPortVtable, EXIODevice_Read) = VXSerialPort_read;
#endif
}

bool VXSerialPort_open(XSerialPort* serial, XIODeviceBase mode, uint8_t portNum, uint32_t baudRate, XSerialPortParity parity)
{
	//printf("准备打开\n");
	if (serial == NULL)
		return false;
	serial->m_baudRate = baudRate;
	serial->m_parity = parity;
	serial->m_portNum = portNum;
	//调用父类
	return XVtableGetFunc(XIODeviceVtable, EXIODevice_Open,bool(*)(XIODevice*, XIODeviceBase))(serial, mode);
	//return XIODevice_open_base(serial, mode);
}

size_t VXSerialPort_read(XIODevice* io, char* data, size_t maxSize)
{
	if (io->m_mode & XIODeviceBase_ReadOnly == 0)
		return 0;
	size_t count = 0;
	if (io->m_readBuffer == NULL)
	{//没有读取缓冲区
		if (io->m_port.readData_funcPointer == NULL)
			return 0;
		count += io->m_port.readData_funcPointer(io, data, maxSize);
	}
	else
	{
		while (XCircularQueue_receive_base(io->m_readBuffer, data + count))
		{
			++count;
			if (count >= maxSize)
				break;
		}
	}
	return count;
}
