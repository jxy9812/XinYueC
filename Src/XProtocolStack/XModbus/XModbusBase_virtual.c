#include"XModbusBase.h"
#include"XTimerGroupWheel.h"
#include"XQueueBase.h"
typedef struct XModbusFrame XModbusFrame;
static XModbusErrorCode VXModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frameData);
static XModbusErrorCode VXModbusBase_recvFrame(XModbusBase* modbus, XModbusFrame* frameData);
static void VXModbusBase_poll(XModbusBase* modbus);
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
	XVTABLE_INHERIT_DEFAULT(XCommunicatorBase_class_init());
	/*void* table[] =
	{
		VXCommunicatorBase_connect_base,VXCommunicatorBase_disconnect_base,
		VXCommunicatorBase_send_base,VXCommunicatorBase_recv_base,
		VXCommunicatorBase_sendAsync,VXCommunicatorBase_recvAsync,
		VXCommunicatorBase_isConnected_base,VXCommunicatorBase_poll_base,
		VXCommunicatorBase_setOption_base,VXCommunicatorBase_getOption_base
	};*/
	//追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Poll, VXModbusBase_poll);
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
static XModbusErrorCode XModbus_EventEmpty(XModbusBase* modbus)
{
	XModbusErrorCode error = MB_ENOERR;

    return error;
}
//事件处理
static void XModbus_EventHandling(XModbusBase* modbus)
{
	XModbusEventType eEvent;
	// 获取端口事件（如接收完成、定时器超时，驱动协议栈处理）
	if (!XQueueBase_receive_base(modbus->eventQueue, &eEvent))
	{
		return XModbus_EventEmpty(modbus);
	}
#if MB_EVENT_HANDLE_SHOW
#if MB_ENUM_TO_STRING
	printf("准备处理事件:%s\n", XModbusEventType_toString(eEvent));
#else
	printf("准备处理事件:%d\n", eEvent);
#endif
#endif // MB_EVENT_SHOH
	//{ // 有事件待处理
	switch (eEvent)
	{
	//case EV_FRAME_RECEIVED: XModbus_EV_FRAME_RECEIVED(modbus);  break; // 接收到完整的 Modbus 帧
	//case EV_EXECUTE: XModbus_EV_EXECUTE(modbus); break;
	}
	//return error; // 轮询成功（无错误或错误已处理）
}
void VXModbusBase_poll(XModbusBase* modbus)
{
	if (modbus->state != STATE_ENABLED) {
		return MB_EILLSTATE; // 非法状态，直接返回错误
	}
	XModbus_EventHandling(modbus);
	XCommunicatorBase* comm = modbus;
	//处理异步接收
	//recvAsync(comm);
	//处理定时器任务
	if (comm->m_wheel)
		XTimerGroupWheel_poll_base(comm->m_wheel);
}