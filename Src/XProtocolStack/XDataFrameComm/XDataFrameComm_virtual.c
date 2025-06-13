#include"XDataFrameComm.h"
#include"XTimerGroupBase.h"
#include"XVector.h"
#include"XEvent.h"
#include"XDataFrameCommConfig.h"
#include"XTimerWheel.h"
#include"XString.h"
#include"XListBase.h"
#include<assert.h>
#include<string.h>
#include<stdlib.h>
static void XDataFrameComm_recvValid(XDataFrameComm* comm);//接收校验
//发送一个事件
static bool XDataFrameComm_sendEvent(XDataFrameComm* comm, XDFC_EventType event);
static bool XDataFrameComm_sendEventRecvFrameReceived(XDataFrameComm* comm, XDFC_EventType event, XVector* frame);
static void VXDataFrameComm_RecvFrameFSM(XDataFrameComm* comm);
static void VXDataFrameComm_SendFrameFSM(XDataFrameComm* comm);
static void VXCommunicatorBase_poll(XDataFrameComm* comm);
static bool VXCommunicatorBase_connect(XDataFrameComm* comm);
static bool VXCommunicatorBase_disconnect(XDataFrameComm* comm);
static XDFC_ErrorCode VXDataFrameComm_setCommMode(XDataFrameComm* comm, XDFC_CommMode mode);
static XDFC_ErrorCode VXDataFrameComm_setFrameEndType(XDataFrameComm* comm, XDFC_FrameEndType mode);
static XDFC_ErrorCode VXDataFrameComm_sendData(XDataFrameComm* comm, XVector* data);
static XHandle VXDataFrameComm_sendPeriodicData(XDataFrameComm* comm, XVector* data, uint32_t time);
static bool  VXDataFrameComm_removePeriodicSendData(XDataFrameComm* comm, XHandle handle);
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
	XVTABLE_INHERIT_DEFAULT(XCommunicatorBase_class_init());
	void* table[] =
	{
		VXDataFrameComm_SendFrameFSM,VXDataFrameComm_RecvFrameFSM,
		VXDataFrameComm_setCommMode,VXDataFrameComm_setFrameEndType,
		VXDataFrameComm_sendData,VXDataFrameComm_sendPeriodicData,
		VXDataFrameComm_removePeriodicSendData
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Poll, VXCommunicatorBase_poll);
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Connect, VXCommunicatorBase_connect);
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Disconnect, VXCommunicatorBase_disconnect);
#if SHOWCONTAINERSIZE
	printf("XDataFrameComm size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

void XDataFrameComm_recvValid(XDataFrameComm* comm)
{
	if (XVector_isEmpty_base(comm->m_parent.m_recvAsyncBuffer))
		return;//数据缓冲区是空的也就没必要继续了
	if (comm->m_recvValidCb != NULL && !comm->m_recvValidCb(comm->m_parent.m_recvAsyncBuffer))
		return;//校验没通过
	XVector* v = XVector_Create(uint8_t);
	if (v == NULL)
		return;
	XVector_copy_base(v, comm->m_parent.m_recvAsyncBuffer);
	
//	if (!XQueueBase_push_base(comm->m_recvFrameQueue, &v))
//	{//入队失败
//		XVector_delete_base(v);//释放数组防止内存泄露
//#if XDFC_QUEUE_FULL_SHOW
//		printf("接收帧队列溢出当前最大:%d,建议增大队列,调整:XDFC_FRAME_RECV_QUEUE_COUNT\n", XDFC_FRAME_RECV_QUEUE_COUNT);
//#endif
//	}
	if (!XDataFrameComm_sendEventRecvFrameReceived(comm, XDFC_FRAME_RECEIVED,v))
	{
		XVector_delete_base(v);//释放数组防止内存泄露
	}
}

bool XDataFrameComm_sendEvent(XDataFrameComm* comm, XDFC_EventType event)
{
	XEventMin* ev = XEventMin_create(event, 0);
	if(ev==NULL)
		return false;
	if (!XEventDispatcher_addEvent(comm->m_eventDispatcher, ev))
	{//添加失败，队列满了
		XMemory_free(ev);
#if XDFC_QUEUE_FULL_SHOW
		printf("事件队列溢出当前最大:%d,建议增大队列,调整:XDFC_EVENT_QUEUE_COUNT\n", XDFC_EVENT_QUEUE_COUNT);
#endif
		return false;
	}
	return true;
}
bool XDataFrameComm_sendEventRecvFrameReceived(XDataFrameComm* comm, XDFC_EventType event, XVector* frame)
{
	XEventMin* ev = XEventRecvFrame_create(event, 0, frame);
	if (ev == NULL)
		return false;
	if (!XEventDispatcher_addEvent(comm->m_eventDispatcher, ev))
	{//添加失败，队列满了
		XMemory_free(ev);
#if XDFC_QUEUE_FULL_SHOW
		printf("事件队列溢出当前最大:%d,建议增大队列,调整:XDFC_EVENT_QUEUE_COUNT\n", XDFC_EVENT_QUEUE_COUNT);
#endif
		return false;
	}
	return true;
}
void VXDataFrameComm_RecvFrameFSM(XDataFrameComm* comm)
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
				XTimerBase_start_base(comm->m_timerRecvExpired);
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
				XTimerBase_start_base(comm->m_timerRecvExpired);// 保持定时器运行，等待错误帧结束
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
				XTimerBase_start_base(comm->m_timerRecvExpired);
			break;
		}
		case XDFC_STATE_RX_HEAD:    // 接收帧头中
		{
			if (XContainerSize(recvVector) >= XContainerCapacity(recvVector))
			{
				comm->m_eRcvState = XDFC_STATE_RX_ERROR;  // 缓冲区溢出，标记错误状态
				XDataFrameComm_sendEvent(comm, XDFC_RX_BUFFER_OVERFLOW);
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
				XTimerBase_start_base(comm->m_timerRecvExpired);
			break;
		}
		case XDFC_STATE_RX_RCV:  // 接收中状态（连续接收字节）
		{
			if (XContainerSize(recvVector) >= XContainerCapacity(recvVector))
			{
				comm->m_eRcvState = XDFC_STATE_RX_ERROR;  // 缓冲区溢出，标记错误状态
				XDataFrameComm_sendEvent(comm, XDFC_RX_BUFFER_OVERFLOW);
				return;
			}
			XVector_push_back_base(recvVector, &ucByte);  // 存储字节到缓冲区

			if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
			{
				XTimerBase_start_base(comm->m_timerRecvExpired);
				return;
			}
			else if (comm->m_frameEndMode == XDFC_FRAME_END_MARKER)
			{//检测结束标志
				if (comm->m_recvFrameTail == NULL || XVector_isEmpty_base(comm->m_recvFrameTail))
				{
					printf("当前设置是判断结束标志模式,但是未设置帧结束标志,程序无法知道帧结束\n"
						"XDataFrameComm_setRecvFrameTail (设置接收帧尾)\n"
						"XDataFrameComm_setFrameEndType_base (切换帧尾结束方式)\n");
					exit(-1);
					return;
				}
				size_t size = XContainerSize(comm->m_recvFrameTail);
				if (memcmp((uint8_t*)(XContainerDataPtr(recvVector))+ XContainerSize(recvVector)- size, XContainerDataPtr(comm->m_recvFrameTail), size) == 0)
				{//检测到帧结束标志
					XContainerSize(recvVector) -= size;//缓冲区删除结束标志
					XDataFrameComm_recvValid(comm);
					comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
				}
			}
			break;
		}
	}
}
void VXDataFrameComm_SendFrameFSM(XDataFrameComm* comm)
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
			//printf("设置发送帧尾巴\n");
			if (comm->m_sendFrameTail)
			{//当存在发送帧尾先发送帧尾
				XIODeviceBase_write_base(comm->m_parent.m_io, XContainerDataPtr(comm->m_sendFrameTail), XContainerSize(comm->m_sendFrameTail));
				XIODeviceBase_writeFull_base(comm->m_parent.m_io);
			}
#if XDFC_SEND_FRAME_16HEX_SHOW
			XString* str = XString_to16HexString(XContainerDataPtr(frame), XContainerSize(frame));
			if (str != NULL)
			{
				printf("\n16进制发送帧:%s\n", XString_c_str(str));
				XString_delete_base(str);
			}
#endif // 
#ifdef XDFC_SEND_FRAME_STR_SHOW
			if (XVector_Back_Base(frame, char) != 0)
			{
				char c = 0;
				XVector_push_back_base(frame, &c);
			}
			printf("\nString发送帧:%s\n", XContainerDataPtr(frame));
#endif // 
			//发送完成释放资源
			XQueueBase_pop_base(queue);
			XVector_delete_base(frame);

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
				VXDataFrameComm_SendFrameFSM(comm);
		}
		else //if(modbus->eSndState != STATE_TX_XMIT)
		{
			//printf("处理接收数据\n");
			if (comm->m_eSndState == XDFC_STATE_TX_IDLE)
				VXDataFrameComm_RecvFrameFSM(comm);
		}
	}
	else if (comm->m_commMode == XDFC_COMM_MODE_FULL_DUPLEX)
	{
		VXDataFrameComm_SendFrameFSM(comm);
		VXDataFrameComm_RecvFrameFSM(comm);
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
bool VXCommunicatorBase_connect(XDataFrameComm* comm)
{
	if (comm->m_state == XDFC_STATE_ENABLED)
		return true;
	if (XVtableGetFunc(XCommunicatorBase_class_init(), EXCommunicatorBase_Connect, bool(*)(XCommunicatorBase*))(comm))
	{
		comm->m_eRcvState = XDFC_STATE_RX_INIT;  // 初始状态：等待总线空闲
		//comm->m_eSndState = XDFC_STATE_TX_IDLE;//发送空闲
		comm->m_state = XDFC_STATE_ENABLED; // 更新状态为启用
		return true;
	}
	return false;
}
bool VXCommunicatorBase_disconnect(XDataFrameComm* comm)
{
	if (XVtableGetFunc(XCommunicatorBase_class_init(), EXCommunicatorBase_Disconnect, bool(*)(XCommunicatorBase*))(comm))
	{
		if (comm->m_state == XDFC_STATE_ENABLED) 
		{ // 从启用状态禁用
			//modbus->pvMBFrameStopCur(modbus); // 停止协议栈（暂停接收/发送，清理临时资源）
			comm->m_state = XDFC_STATE_DISABLED; // 更新状态为禁用
			return true;
		}
		else if (comm->m_state == XDFC_STATE_DISABLED) 
		{ // 已禁用状态，直接返回成功
			return true;
		}
		else 
		{ // 未初始化状态
			return false;
			//error = MB_EILLSTATE; // 非法状态错误
		}
		//return error;
	}
	return false;
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
static void TimerRecvExpired(XDataFrameComm* comm)
{  //接收超时等待
	switch (comm->m_eRcvState) {
	case XDFC_STATE_RX_INIT:  // 初始状态超时（总线空闲，进入IDLE）
		XDataFrameComm_sendEvent(comm, XDFC_READY);
		break;

	case XDFC_STATE_RX_RCV:   // 接收中状态超时（帧接收完成）
		XDataFrameComm_recvValid(comm);
		break;

	case XDFC_STATE_RX_ERROR: // 错误状态超时（忽略）
		XDataFrameComm_sendEvent(comm, XDFC_RX_BUFFER_OVERFLOW);
		break;
	case XDFC_STATE_RX_IDLE: //接收空闲
		break;
	default:            // 非法状态（断言检查）
		assert((comm->m_eRcvState == XDFC_STATE_RX_INIT) || (comm->m_eRcvState == XDFC_STATE_RX_RCV) || (comm->m_eRcvState == XDFC_STATE_RX_ERROR));
	}

	comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
	XTimerBase_stop_base(comm->m_timerRecvExpired);  // 关闭定时器
}
XDFC_ErrorCode VXDataFrameComm_setFrameEndType(XDataFrameComm* comm, XDFC_FrameEndType mode)
{
	if (comm->m_state == XDFC_STATE_ENABLED)
		return XDFC_EILLSTATE;//协议栈启动中不支持修改
	if (mode == XDFC_FRAME_END_TIMEOUT)
	{//设置为超时结束
		if (comm->m_timerRecvExpired == NULL)
		{
			XTimerBase* timer = XTimerWheel_create();
			XTimerBase_setTimerCallback(timer, TimerRecvExpired);
			XTimerBase_setUserData(timer, comm);
			XTimerBase_setAutoDelete(timer, false);
			XTimerBase_setTimerGroup(timer, ((XCommunicatorBase*)comm)->m_wheel);
			XTimerBase_setTimeout_base(timer, XDFC_FRAME_END_TIMEOUT_TIME);
			comm->m_timerRecvExpired = timer;
		}
	}
	else if (mode == XDFC_FRAME_END_MARKER)
	{//设置为标志结束
		if (comm->m_timerRecvExpired)
		{
			XTimerBase_delete_base(comm->m_timerRecvExpired);
			comm->m_timerRecvExpired = NULL;
		}
	}
	comm->m_frameEndMode = mode;
	return XDFC_ENOERR;
}

XDFC_ErrorCode VXDataFrameComm_sendData(XDataFrameComm* comm, XVector* data)
{
	if (XVector_isEmpty_base(data))
	{
		XVector_delete_base(data);
		return XDFC_EINVAL;
	}
	if (comm->m_sendValidCb)
		comm->m_sendValidCb(data);
	if (!XQueueBase_push_base(comm->m_sendFrameQueue, &data))
	{
#if XDFC_QUEUE_FULL_SHOW
		printf("发送队列溢出当前最大:%d,建议增大队列,调整:XDFC_FRAME_SEND_QUEUE_COUNT\n", XDFC_FRAME_SEND_QUEUE_COUNT);
#endif
		XVector_delete_base(data);
		return XDFC_ENORES;
	}
	return XDFC_ENOERR;
}
//定期发送的定时器回调函数
static void SendDataPeriodicCb(XPair* pair)
{
	XVector* v = XVector_Create(uint8_t);
	if (v == NULL)
		return;
	XVector_copy_base(v, XPair_Second(pair, XVector*));
	if (!XQueueBase_push_base(XPair_First(pair, XDataFrameComm*)->m_sendFrameQueue, &v))
	{
#if XDFC_QUEUE_FULL_SHOW
		printf("发送队列溢出当前最大:%d,建议增大队列,调整:XDFC_FRAME_SEND_QUEUE_COUNT\n", XDFC_FRAME_SEND_QUEUE_COUNT);
#endif
		XVector_delete_base(v);
		return XDFC_ENORES;
	}
	//XDataFrameComm_sendData_base(XPair_First(pair, XDataFrameComm*),v);
}
XHandle VXDataFrameComm_sendPeriodicData(XDataFrameComm* comm, XVector* data, uint32_t time)
{
	if (XVector_isEmpty_base(data))
		return NULL;
	XPair* pair = XPair_Create(XDataFrameComm*, XVector*);
	if (pair == NULL)
	{
		return NULL;
	}
	XTimerWheel* timer = XTimerWheel_create();
	if (timer == NULL)
	{
		XPair_delete(pair);
		return NULL;
	}
	if (comm->m_sendValidCb)
		comm->m_sendValidCb(data);

	XPair_First(pair, XDataFrameComm*) = comm;
	XPair_Second(pair, XVector*) = data;
	
	XListBase_push_back_base(comm->m_periodicSendList,&pair);
	XTimerBase_setTimeout_base(timer, time);
	XTimerBase_setInterval_base(timer, time);
	XTimerBase_setTimerGroup(timer, ((XCommunicatorBase*)comm)->m_wheel);
	XTimerBase_setUserData(timer, pair);
	XTimerBase_setTimerCallback(timer, SendDataPeriodicCb);
	XTimerBase_start_base(timer);
	return pair;
}

bool VXDataFrameComm_removePeriodicSendData(XDataFrameComm* comm, XHandle handle)
{
	if(XListBase_isEmpty_base(comm->m_periodicSendList))
		return false;
	size_t size = XContainerSize(comm->m_periodicSendList);
	XListBase_remove_base(comm->m_periodicSendList,&handle);
	if(size== XContainerSize(comm->m_periodicSendList))
		return false;
	XPair* pair = handle;
	XVector_delete_base(XPair_Second(pair, XVector*));
	XPair_delete(pair);
	return true;
}
