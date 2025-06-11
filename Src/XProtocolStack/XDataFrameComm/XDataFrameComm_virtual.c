#include"XDataFrameComm.h"
#include"XTimerGroupBase.h"
#include"XEvent.h"
static void VXCommunicatorBase_poll_base(XDataFrameComm* comm);

XVtable* XDataFrameComm_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XDataFrameComm_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XDataFrameComm_class_init());
	//void* table[] =
	//{
	//	VXModbusBase_sendFrame,VXModbusBase_recvFrame,VXModbusBase_ReceiveFSM,VXModbusBase_TransmitFSM
	//};
	////追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	/*XVTABLE_OVERLOAD_DEFAULT(EXDataFrameComm_Poll, VXModbusBase_poll);
	XVTABLE_OVERLOAD_DEFAULT(EXDataFrameComm_Connect, VXDataFrameComm_connect);
	XVTABLE_OVERLOAD_DEFAULT(EXDataFrameComm_Disconnect, VXDataFrameComm_disconnect);*/
#if SHOWCONTAINERSIZE
	printf("XDataFrameComm size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void VXCommunicatorBase_poll_base(XDataFrameComm* comm)
{
	if (comm->m_state != XDFC_STATE_ENABLED)
		return;//协议栈还未准备好
	XEventDispatcher_handler(comm->m_eventDispatcher);//处理事件
	//处理定时器任务
	if (((XCommunicatorBase*)comm)->m_wheel)
		XTimerGroupBase_poll_base(((XCommunicatorBase*)comm)->m_wheel);
}
