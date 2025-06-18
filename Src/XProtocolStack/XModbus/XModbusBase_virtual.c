#include"XModbusBase.h"
#include"XTimerGroupWheel.h"
#include"XQueueBase.h"
#include"XModbusFrame.h"
#include"XModbusProto.h"
#include"XModbusFunctionHandler.h"
typedef struct XModbusFrame XModbusFrame;
static XModbusErrorCode VXModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frameData);
static XModbusErrorCode VXModbusBase_recvFrame(XModbusBase* modbus, XModbusFrame* frameData);
static bool VXModbusBase_ReceiveFSM(XModbusBase* modbus);
static bool VXModbusBase_TransmitFSM(XModbusBase* modbus);
XVtable* XModbusBase_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
		XVTABLE_STACK_INIT_DEFAULT(XMODBUSBASE_VTABLE_SIZE)
#else
		XVTABLE_HEAP_INIT_DEFAULT
#endif
		//继承类
		XVTABLE_INHERIT_DEFAULT(XDataFrameComm_class_init());
	void* table[] =
	{
		VXModbusBase_sendFrame,VXModbusBase_recvFrame,VXModbusBase_ReceiveFSM,VXModbusBase_TransmitFSM
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	/*XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Poll, VXModbusBase_poll);
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Connect, VXCommunicatorBase_connect);
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Disconnect, VXCommunicatorBase_disconnect);*/
#if SHOWCONTAINERSIZE
	printf("XModbusBase size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
//static void recvAsync(XCommunicatorBase* comm)
//{
//	if (comm->m_io && comm->m_recvAsyncBuffer && comm->m_recvDataCallback)
//	{//开启了异步接收
//		size_t size = XIODeviceBase_getBytesAvailable_base(comm->m_io);
//		if (size == 0)
//			return;
//		size_t buffSize = XContainerCapacity(comm->m_recvAsyncBuffer);
//		XContainerSize(comm->m_recvAsyncBuffer) = 0;
//		size_t readSize = XIODeviceBase_read_base(comm->m_io, XContainerDataPtr(comm->m_recvAsyncBuffer), (buffSize > size) ? size : buffSize);
//		if (readSize > 0)
//			comm->m_recvDataCallback(XContainerDataPtr(comm->m_recvAsyncBuffer), readSize, comm->m_userData);
//	}
//}
//处理事件是空的情况
XModbusErrorCode VXModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frameData)
{
	printf("请在子类中实现\n");
	return MB_EPORTERR;
}
XModbusErrorCode VXModbusBase_recvFrame(XModbusBase* modbus, XModbusFrame* frameData)
{
	printf("请在子类中实现\n");
	return MB_EPORTERR;
}
bool VXModbusBase_ReceiveFSM(XModbusBase* modbus)
{
	printf("请在子类中实现\n");
}
bool VXModbusBase_TransmitFSM(XModbusBase* modbus)
{
	printf("请在子类中实现\n");
}
