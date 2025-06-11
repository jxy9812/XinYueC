#include"XDataFrameComm.h"
#include"XTimerGroupBase.h"
#include"XVector.h"
#include"XEvent.h"
#include"XDataFrameCommConfig.h"
#include"XTimerWheel.h"
static void VXDataFrameComm_RecvFrame(XDataFrameComm* comm);
static void VXDataFrameComm_SendFrame(XDataFrameComm* comm);
static void VXCommunicatorBase_poll(XDataFrameComm* comm);
static XDFC_ErrorCode VXDataFrameComm_setCommMode(XDataFrameComm* comm, XDFC_CommMode mode);
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
	void* table[] =
	{
		VXDataFrameComm_SendFrame,VXDataFrameComm_RecvFrame,
		VXDataFrameComm_setCommMode
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	/*XVTABLE_OVERLOAD_DEFAULT(EXDataFrameComm_Poll, VXModbusBase_poll);
	XVTABLE_OVERLOAD_DEFAULT(EXDataFrameComm_Connect, VXDataFrameComm_connect);
	XVTABLE_OVERLOAD_DEFAULT(EXDataFrameComm_Disconnect, VXDataFrameComm_disconnect);*/
#if SHOWCONTAINERSIZE
	printf("XDataFrameComm size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
void VXDataFrameComm_RecvFrame(XDataFrameComm* comm)
{

}
void VXDataFrameComm_SendFrame(XDataFrameComm* comm)
{
	//printf("检查是否可以发送\n");
	 //以下可以发送数据
	XQueueBase* queue = comm->m_sendFrameQueue;
	if (XQueueBase_isEmpty_base(queue))
		return;//无数据可以发送
	XVector* frame = XQueueBase_Top_Base(queue,XVector*);
	switch (comm->m_eSndState)
	{
		case XDFC_STATE_TX_IDLE:  // 发送空闲状态（无数据发送）
		{
			if (comm->m_sendMode == XDFC_SEND_MODE_BYTE)
			{//当逐字节发送时
				comm->m_sentBytes = 0;
			}
			if (comm->m_sendFrameHead)
			{//当存在发送帧头先发送帧头
				XIODeviceBase_write_base(comm->m_parent.m_io, XContainerDataPtr(comm->m_sendFrameHead), XContainerSize(comm->m_sendFrameHead));
				XIODeviceBase_writeFull_base(comm->m_parent.m_io);
			}
			comm->m_eSndState = XDFC_STATE_TX_XMIT;
			break;
		}
		case XDFC_STATE_TX_XMIT:  // 发送中状态（逐个字节发送）
		{
			if (comm->m_sendMode == XDFC_SEND_MODE_BYTE)
			{
				if (comm->m_sentBytes < XVector_getSize_base(frame))
				{//
					XIODeviceBase_write_base(comm->m_parent.m_io, ((uint8_t*)XContainerDataPtr(frame)) + comm->m_sentBytes, 1);
					XIODeviceBase_writeFull_base(comm->m_parent.m_io);
					++comm->m_sentBytes;
					return;
				}
			}
			else//整体一起发送
			{
				XIODeviceBase_write_base(comm->m_parent.m_io, XContainerDataPtr(frame), XContainerSize(frame));
				XIODeviceBase_writeFull_base(comm->m_parent.m_io);
			}
			//发送完成
			if (comm->m_sendFrameTail)
			{//当存在发送帧尾先发送帧尾
				XIODeviceBase_write_base(comm->m_parent.m_io, XContainerDataPtr(comm->m_sendFrameTail), XContainerSize(comm->m_sendFrameTail));
				XIODeviceBase_writeFull_base(comm->m_parent.m_io);
			}
			comm->m_eSndState = XDFC_STATE_TX_END;  // 切换到发送结束状态
			if (comm->m_commMode == XDFC_COMM_MODE_HALF_DUPLEX)
			{//半双工模式时等待一段时间才可以继续发送，留给接收响应
				XTimerBase_start_base(comm->m_timerSendExpired);
			}
			break;
		}
	}
}
static void  RecvSendData(XDataFrameComm* comm)
{
	if (comm->m_commMode == XDFC_COMM_MODE_HALF_DUPLEX)
	{
		if (comm->m_eSndState == XDFC_STATE_TX_XMIT || XIODeviceBase_getBytesAvailable_base(((XCommunicatorBase*)comm)->m_io) == 0)
		{
			//printf("发送数据\n");
			if (comm->m_eRcvState == XDFC_STATE_RX_IDLE && comm->m_eSndState != XDFC_STATE_TX_END)
				VXDataFrameComm_SendFrame(comm);
		}
		else //if(modbus->eSndState != STATE_TX_XMIT)
		{
			//printf("处理接收数据\n");
			VXDataFrameComm_RecvFrame(comm);
		}
	}
	else if (comm->m_commMode == XDFC_COMM_MODE_FULL_DUPLEX)
	{
		VXDataFrameComm_SendFrame(comm);
		VXDataFrameComm_RecvFrame(comm);
	}
}
void VXCommunicatorBase_poll(XDataFrameComm* comm)
{
	if (comm->m_state != XDFC_STATE_ENABLED)
		return;//协议栈还未准备好
	if (XQueueBase_isEmpty_base(comm->m_eventDispatcher->m_queue))
	{//当前无事件，执行接收和发送数据
		RecvSendData(comm);
	}
	else
	{
		XEventDispatcher_handler(comm->m_eventDispatcher);//处理事件
	}
	//处理定时器任务
	if (((XCommunicatorBase*)comm)->m_wheel)
		XTimerGroupBase_poll_base(((XCommunicatorBase*)comm)->m_wheel);
}
static void TimerSendExpired(XDataFrameComm* comm)
{
	//发完一帧数据总线等待
	//if (comm->m_eSndState == XDFC_STATE_TX_END)
	{
		comm->m_eSndState = XDFC_STATE_TX_IDLE;
		XTimerBase_stop_base( comm->m_timerSendExpired);  // 关闭定时器
	}
}
XDFC_ErrorCode VXDataFrameComm_setCommMode(XDataFrameComm* comm, XDFC_CommMode mode)
{
	if (comm->m_state == XDFC_STATE_ENABLED)
		return XDFC_EILLSTATE;//协议栈启动中不支持修改
	if (mode == XDFC_COMM_MODE_FULL_DUPLEX)
	{//全双工
		if (comm->m_timerSendExpired)
		{
			XTimerBase_delete_base(comm->m_timerSendExpired);
			comm->m_timerSendExpired = NULL;
		}
	}
	else if (mode == XDFC_COMM_MODE_HALF_DUPLEX)
	{//半双工
		if (comm->m_timerSendExpired==NULL)
		{
			XTimerBase* timer = XTimerWheel_create();
			XTimerBase_setTimerCallback(timer, TimerSendExpired);
			XTimerBase_setUserData(timer, comm);
			XTimerBase_setAutoDelete(timer,false);
			XTimerBase_setTimerGroup(timer, ((XCommunicatorBase*)comm)->m_wheel);
			XTimerBase_setTimeout_base(timer, XDFC_HALF_DUPLEX_SEND_WAIT_TIME);
			comm->m_timerSendExpired = timer;
		}
	}
	comm->m_commMode = mode;
	return XDFC_ENOERR;
}
