#include"XDataFrameComm.h"
#include"XTimerGroupBase.h"
#include"XVector.h"
#include"XEvent.h"
#include"XDataFrameCommConfig.h"
#include"XTimerWheel.h"
#include<assert.h>
#include<string.h>
//发送一个事件
static bool XDataFrameComm_sendEvent(XDataFrameComm* comm, XDFC_EventType event);
static void VXDataFrameComm_RecvFrame(XDataFrameComm* comm);
static void VXDataFrameComm_SendFrame(XDataFrameComm* comm);
static void VXCommunicatorBase_poll(XDataFrameComm* comm);
static XDFC_ErrorCode VXDataFrameComm_setCommMode(XDataFrameComm* comm, XDFC_CommMode mode);
static XDFC_ErrorCode VXDataFrameComm_setFrameEndType(XDataFrameComm* comm, XDFC_FrameEndType mode);
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
		VXDataFrameComm_setCommMode,VXDataFrameComm_setFrameEndType
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Poll, VXCommunicatorBase_poll);
	//XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Connect, VXDataFrameComm_connect);
	//XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Disconnect, VXDataFrameComm_disconnect);
#if SHOWCONTAINERSIZE
	printf("XDataFrameComm size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
bool XDataFrameComm_sendEvent(XDataFrameComm* comm, XDFC_EventType event)
{
	return false;
}
void VXDataFrameComm_RecvFrame(XDataFrameComm* comm)
{
	if (XIODeviceBase_getBytesAvailable_base(((XCommunicatorBase*)comm)->m_io) == 0)
		return;//没有可以接收的
	uint8_t           ucByte;
	XIODeviceBase_read_base(comm->m_parent.m_io, &ucByte, 1);
	XVector* recvVector = comm->m_parent.m_recvAsyncBuffer;
	switch (comm->m_eRcvState)//if (mode == XDFC_FRAME_END_TIMEOUT)
	{
		case XDFC_STATE_RX_INIT:  // 初始状态（等待总线空闲）
		{
			if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
			{
				XTimerBase_start_base(comm->m_timerT35Expired);
			}
			else
			{
				comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
				//发送一个事件
				XDataFrameComm_sendEvent(comm, XDFC_READY);
			}
			break;
		}
		case XDFC_STATE_RX_ERROR:  // 接收错误状态（忽略后续字节）
		{
			if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
			{
				XTimerBase_start_base(comm->m_timerT35Expired);// 保持定时器运行，等待错误帧结束
				break;
			}
			else
			{
				//comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
			}
			
		}
		case XDFC_STATE_RX_IDLE:  // 空闲状态（接收到新帧起始字节）
		{
			XContainerSize(recvVector)=0;  // 重置接收缓冲区位置
			XVector_push_back_base(recvVector, &ucByte);  // 存储第一个字节
			if (comm->m_recvFrameHead&&!XVector_isEmpty_base(comm->m_recvFrameHead))
			{//存在接收帧头
				if (memcmp(XContainerDataPtr(recvVector), XContainerDataPtr(comm->m_recvFrameHead), 1) != 0)
				{//比较第一个
					return;//第一个就不一样 重新来过
				}
				if (XContainerSize(comm->m_recvFrameHead) == 1)
				{
					comm->m_eRcvState = XDFC_STATE_RX_RCV;//切换到接收数据中
				}
				else
				{
					comm->m_eRcvState = XDFC_STATE_RX_HEAD;//切换到接收帧头中
				}
			}
			else
			{
				comm->m_eRcvState = XDFC_STATE_RX_RCV;//切换到接收数据中
			}

			if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
				XTimerBase_start_base(comm->m_timerT35Expired);
			break;
		}
		case XDFC_STATE_RX_HEAD:    // 接收帧头中
		{
			if (XContainerSize(recvVector) >= XContainerCapacity(recvVector))
			{
				comm->m_eRcvState = XDFC_STATE_RX_ERROR;  // 缓冲区溢出，标记错误状态
				return;
			}
			XVector_push_back_base(recvVector, &ucByte);  // 存储字节到缓冲区
			size_t size = XContainerSize(recvVector);
			if (memcmp((uint8_t*)(XContainerDataPtr(recvVector)) + size - 1, ((uint8_t*)XContainerDataPtr(comm->m_recvFrameHead))+ size-1, 1) != 0)
			{
				comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
				return;//校验失败重新开始
			}
			if (size == XContainerSize(comm->m_recvFrameHead))
			{//校验通过了
				XContainerSize(recvVector) = 0;//清空缓冲区用来存数据了
				comm->m_eRcvState = XDFC_STATE_RX_RCV;//切换到接收数据中
			}
			if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
				XTimerBase_start_base(comm->m_timerT35Expired);
			break;
		}
		case XDFC_STATE_RX_RCV:  // 接收中状态（连续接收字节）
		{
			if (XContainerSize(recvVector) >= XContainerCapacity(recvVector))
			{
				comm->m_eRcvState = XDFC_STATE_RX_ERROR;  // 缓冲区溢出，标记错误状态
				return;
			}
			XVector_push_back_base(recvVector, &ucByte);  // 存储字节到缓冲区

			if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
			{
				XTimerBase_start_base(comm->m_timerT35Expired);
				return;
			}
			else if (comm->m_frameEndMode == XDFC_FRAME_END_MARKER)
			{//检测结束标志
				if (comm->m_recvFrameTail == NULL || XVector_isEmpty_base(comm->m_recvFrameTail))
				{
					printf("当前设置是判断结束标志模式,但是未设置帧结束标志,程序无法知道帧结束\n");
					return;
				}
				size_t size = XContainerSize(comm->m_recvFrameTail);
				if (memcmp((uint8_t*)(XContainerDataPtr(recvVector))+ XContainerSize(recvVector)- size, XContainerDataPtr(comm->m_recvFrameTail), size) == 0)
				{//检测到帧结束标志
					XContainerSize(recvVector) -= size;//缓冲区删除结束标志
					//发送到接收帧队列中处理
					XDataFrameComm_sendEvent(comm, XDFC_FRAME_RECEIVED);
					comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
				}
			}
			break;
		}
	}
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
			if (comm->m_commMode == XDFC_COMM_MODE_HALF_DUPLEX)
			{//半双工模式时等待一段时间才可以继续发送，留给接收响应
				comm->m_eSndState = XDFC_STATE_TX_END;  // 切换到发送结束状态
				XTimerBase_start_base(comm->m_timerSendExpired);
			}
			else if (comm->m_commMode == XDFC_COMM_MODE_FULL_DUPLEX)
			{
				comm->m_eSndState = XDFC_STATE_TX_IDLE;  // 切换到发送空闲状态
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
			if (comm->m_eSndState == XDFC_STATE_TX_IDLE)
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
static void TimerT35Expired(XDataFrameComm* comm)
{  //接收超时等待
	switch (comm->m_eRcvState) {
	case XDFC_STATE_RX_INIT:  // 初始状态超时（总线空闲，进入IDLE）
		XDataFrameComm_sendEvent(comm, XDFC_READY);
		break;

	case XDFC_STATE_RX_RCV:   // 接收中状态超时（帧接收完成）
		XDataFrameComm_sendEvent(comm, XDFC_FRAME_RECEIVED);
		break;

	case XDFC_STATE_RX_ERROR: // 错误状态超时（忽略）
		break;
	case XDFC_STATE_RX_IDLE: //接收空闲
		break;
	default:            // 非法状态（断言检查）
		assert((comm->m_eRcvState == XDFC_STATE_RX_INIT) || (comm->m_eRcvState == XDFC_STATE_RX_RCV) || (comm->m_eRcvState == XDFC_STATE_RX_ERROR));
	}

	comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态

}
XDFC_ErrorCode VXDataFrameComm_setFrameEndType(XDataFrameComm* comm, XDFC_FrameEndType mode)
{
	if (comm->m_state == XDFC_STATE_ENABLED)
		return XDFC_EILLSTATE;//协议栈启动中不支持修改
	if (mode == XDFC_FRAME_END_TIMEOUT)
	{//设置为超时结束
		if (comm->m_timerT35Expired == NULL)
		{
			XTimerBase* timer = XTimerWheel_create();
			XTimerBase_setTimerCallback(timer, TimerT35Expired);
			XTimerBase_setUserData(timer, comm);
			XTimerBase_setAutoDelete(timer, false);
			XTimerBase_setTimerGroup(timer, ((XCommunicatorBase*)comm)->m_wheel);
			XTimerBase_setTimeout_base(timer, XDFC_FRAME_END_TIMEOUT_TIME);
			comm->m_timerT35Expired = timer;
		}
	}
	else if (mode == XDFC_FRAME_END_MARKER)
	{//设置为标志结束
		if (comm->m_timerT35Expired)
		{
			XTimerBase_delete_base(comm->m_timerT35Expired);
			comm->m_timerT35Expired = NULL;
		}
	}
	comm->m_frameEndMode = mode;
	return XDFC_ENOERR;
}
