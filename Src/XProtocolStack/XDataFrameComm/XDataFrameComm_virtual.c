#include"XDataFrameComm.h"
#include"XTimerGroupBase.h"
#include"XByteArray.h"
#include"XCrc.h"
#include"XEvent.h"
#include"XEventDispatcher.h"
#include"XDataFrameCommConfig.h"
#include"XTimerTimeWheel.h"
#include"XString.h"
#include"XListSLinked.h"
#include"XPrintf.h"
#include<assert.h>
#include<string.h>
#include<stdlib.h>
typedef struct
{
	XDataFrameComm* comm;
	XByteArray* data;
	XTimerTimeWheel* timer;
}PeriodicNode;//定时发送的节点
static void XDataFrameComm_recvValid(XDataFrameComm* comm);//接收校验
static void VXDataFrameComm_RecvFrameFSM(XDataFrameComm* comm);
static void VXDataFrameComm_SendFrameFSM(XDataFrameComm* comm);
static void VXCommunicatorBase_poll(XDataFrameComm* comm);
static bool VXCommunicatorBase_connect(XDataFrameComm* comm);
static bool VXCommunicatorBase_disconnect(XDataFrameComm* comm);
static XDFC_ErrorCode VXDataFrameComm_setCommMode(XDataFrameComm* comm, XDFC_CommMode mode);
static XDFC_ErrorCode VXDataFrameComm_setFrameEndType(XDataFrameComm* comm, XDFC_FrameEndType mode);
static XDFC_ErrorCode VXDataFrameComm_sendData(XDataFrameComm* comm, XByteArray* data);
static XHandle VXDataFrameComm_sendPeriodicData(XDataFrameComm* comm, XByteArray* data, uint32_t time);
static bool  VXDataFrameComm_removePeriodicSendData(XDataFrameComm* comm, XHandle handle);
static void VXDataFrameComm_setRecvValidCRC16(XDataFrameComm* comm, bool enableCRC16);//接收验证数据使用CRC16，小端添加在数据末尾帧尾前
static void VXDataFrameComm_setSendValidCRC16(XDataFrameComm* comm, bool enableCRC16);//发送数据添加验证用CRC16，小端添加在数据末尾帧尾前
static void VXDataFrameComm_deinit(XDataFrameComm* comm);
static void VXDataFrameComm_setRecvFrameHead(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize);
static void VXDataFrameComm_setRecvFrameTail(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize);
static void VXDataFrameComm_setSendFrameHead(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize);
static void VXDataFrameComm_setSendFrameTail(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize);
XVtable* XDataFrameComm_class_init()
{
	XVTABLE_CREAT_DEFAULT
		//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCLASS_VTABLE_GET_SIZE(XDataFrameComm))
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
		VXDataFrameComm_setRecvValidCRC16,VXDataFrameComm_setSendValidCRC16,
		VXDataFrameComm_setRecvFrameHead,VXDataFrameComm_setRecvFrameTail,
		VXDataFrameComm_setSendFrameHead,VXDataFrameComm_setSendFrameTail,
		VXDataFrameComm_removePeriodicSendData,
	};
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXDataFrameComm_deinit);
	XVTABLE_OVERLOAD_DEFAULT(EXObject_Poll, VXCommunicatorBase_poll);
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Connect, VXCommunicatorBase_connect);
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Disconnect, VXCommunicatorBase_disconnect);
#if SHOWCONTAINERSIZE
	XPrintf("XDataFrameComm size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
//接受数据验证
void XDataFrameComm_recvValid(XDataFrameComm* comm)
{
	if (XVector_isEmpty_base(comm->m_class.m_recvAsyncBuffer))
		return;//数据缓冲区是空的也就没必要继续了
	
	if (comm->m_recvValidCb != NULL && !comm->m_recvValidCb(comm,comm->m_class.m_recvAsyncBuffer))
	{//真验证失败,错误帧
		XDataFrameComm_postEvent(comm, XEvent_create(comm,XDFC_RX_FRAME_ERROR, 0));
		return;//校验没通过
	}
	XByteArray* v =XByteArray_create(0);
	if (v == NULL)
		return;
	XVector_copy_base(v, comm->m_class.m_recvAsyncBuffer);

	if (!XDataFrameComm_postEvent(comm, XEventRecvFrame_create(comm,XDFC_FRAME_RECEIVED, 0, v)))
	{
		XVector_delete_base(v);//释放数组防止内存泄露
	}
}

void VXDataFrameComm_RecvFrameFSM(XDataFrameComm* comm)
{
	while (XIODeviceBase_getBytesAvailable_base(((XCommunicatorBase*)comm)->m_io))
	{
		//return;//没有可以接收的
		uint8_t           ucByte;
		XIODeviceBase_read_base(comm->m_class.m_io, &ucByte, 1);
		XByteArray* recvVector = comm->m_class.m_recvAsyncBuffer;
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
				XDataFrameComm_postEvent(comm, XDFC_READY);
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
			XContainerSize(recvVector) = 0;  // 重置接收缓冲区位置
			XVector_push_back_base(recvVector, &ucByte);  // 存储第一个字节
			if (comm->m_recvFrameHead && !XVector_isEmpty_base(comm->m_recvFrameHead))
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
				XDataFrameComm_postEvent(comm, XEvent_create(comm, XDFC_RX_BUFFER_OVERFLOW, 0));
				return;
			}
			XVector_push_back_base(recvVector, &ucByte);  // 存储字节到缓冲区
			size_t size = XContainerSize(recvVector);
			if (memcmp((uint8_t*)(XContainerDataPtr(recvVector)) + size - 1, ((uint8_t*)XContainerDataPtr(comm->m_recvFrameHead)) + size - 1, 1) != 0)
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
				XDataFrameComm_postEvent(comm, XEvent_create(comm, XDFC_RX_BUFFER_OVERFLOW, 0));
				return;
			}
			XVector_push_back_base(recvVector, &ucByte);  // 存储字节到缓冲区
			break;
		}
		}
	}
	if (comm->m_eRcvState == XDFC_STATE_RX_RCV)
	{
		if (comm->m_frameEndMode == XDFC_FRAME_END_TIMEOUT)
		{
			XTimerBase_start_base(comm->m_timerRecvExpired);
			//return;
		}
		else if (comm->m_frameEndMode == XDFC_FRAME_END_MARKER)
		{//检测结束标志
			if (comm->m_recvFrameTail == NULL || XVector_isEmpty_base(comm->m_recvFrameTail))
			{
				XPrintf("当前设置是判断结束标志模式,但是未设置帧结束标志,程序无法知道帧结束\n"
					"XDataFrameComm_setRecvFrameTail_base (设置接收帧尾)\n"
					"XDataFrameComm_setFrameEndType_base (切换帧尾结束方式)\n");
				exit(-1);
				return;
			}
			size_t size = XContainerSize(comm->m_recvFrameTail);
			XByteArray* recvVector = comm->m_class.m_recvAsyncBuffer;
			if ((XContainerSize(recvVector) >= size) && memcmp((uint8_t*)(XContainerDataPtr(recvVector)) + XContainerSize(recvVector) - size, XContainerDataPtr(comm->m_recvFrameTail), size) == 0)
			{//检测到帧结束标志
				XContainerSize(recvVector) -= size;//缓冲区删除结束标志
				if (XContainerSize(recvVector) != 0)
					XDataFrameComm_recvValid(comm);
				comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
			}
		}
	}

}
void VXDataFrameComm_SendFrameFSM(XDataFrameComm* comm)
{
	//XPrintf("检查是否可以发送\n");
	 //以下可以发送数据
	XQueueBase* queue = comm->m_sendFrameQueue;
	if (XQueueBase_isEmpty_base(queue))
		return;//无数据可以发送
	XByteArray* frame = XQueueBase_Top_Base(queue,XByteArray*);
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
				XIODeviceBase_write_base(comm->m_class.m_io, XContainerDataPtr(comm->m_sendFrameHead), XContainerSize(comm->m_sendFrameHead));
				XIODeviceBase_writeFull_base(comm->m_class.m_io);
			}
			comm->m_eSndState = XDFC_STATE_TX_XMIT;
			break;
		}
		case XDFC_STATE_TX_XMIT:  // 发送中状态（逐个字节发送）
		{
			if (comm->m_sendMode == XDFC_SEND_MODE_BYTE)
			{
				if (comm->m_sentBytes < XVector_size_base(frame))
				{//
					XIODeviceBase_write_base(comm->m_class.m_io, ((uint8_t*)XContainerDataPtr(frame)) + comm->m_sentBytes, 1);
					XIODeviceBase_writeFull_base(comm->m_class.m_io);
					++comm->m_sentBytes;
					return;
				}
			}
			else//整体一起发送
			{
				XIODeviceBase_write_base(comm->m_class.m_io, XContainerDataPtr(frame), XContainerSize(frame));
				XIODeviceBase_writeFull_base(comm->m_class.m_io);
			}
			//发送完成
			//XPrintf("设置发送帧尾巴\n");
			if (comm->m_sendFrameTail)
			{//当存在发送帧尾先发送帧尾
				XIODeviceBase_write_base(comm->m_class.m_io, XContainerDataPtr(comm->m_sendFrameTail), XContainerSize(comm->m_sendFrameTail));
				XIODeviceBase_writeFull_base(comm->m_class.m_io);
			}
#if XDFC_SEND_FRAME_16HEX_SHOW
			XByteArray* str = XByteArray_to16HexUtf8(frame);
			if (str != NULL)
			{
				XPrintf("\n16进制发送帧:%s\n", XContainerDataPtr(str));
				XByteArray_delete_base(str);
			}
#endif // 
#if XDFC_SEND_FRAME_STR_SHOW
			if (XVector_Back_Base(frame, char) != 0)
			{
				char c = 0;
				XVector_push_back_base(frame, &c);
			}
			XPrintf("\nString发送帧:%s\n", XContainerDataPtr(frame));
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
			XDataFrameComm_postEvent(comm, XEvent_create(comm,XDFC_FRAME_SENT, 0));
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
			//XPrintf("半双工通信中,发送数据\n");
			if (comm->m_eRcvState == XDFC_STATE_RX_IDLE && comm->m_eSndState != XDFC_STATE_TX_END)
			{
				//VXDataFrameComm_SendFrameFSM(comm);
				XClassGetVirtualFunc(comm, EXDataFrameComm_SendFrameFSM, void (*)(XDataFrameComm*))(comm);
			}
		}
		else if (comm->m_eSndState == XDFC_STATE_TX_IDLE)
		{
			//XPrintf("半双工通信中,接收数据\n");
			XClassGetVirtualFunc(comm,EXDataFrameComm_RecvFrameFSM, void (*)(XDataFrameComm *))(comm);
			//VXDataFrameComm_RecvFrameFSM(comm);
		}
	}
	else if (comm->m_commMode == XDFC_COMM_MODE_FULL_DUPLEX)
	{
		//VXDataFrameComm_SendFrameFSM(comm);
		//VXDataFrameComm_RecvFrameFSM(comm);
		XClassGetVirtualFunc(comm, EXDataFrameComm_SendFrameFSM, void (*)(XDataFrameComm*))(comm);
		XClassGetVirtualFunc(comm, EXDataFrameComm_RecvFrameFSM, void (*)(XDataFrameComm*))(comm);
	}
}
void VXCommunicatorBase_poll(XDataFrameComm* comm)
{
	if (comm->m_state != XDFC_STATE_ENABLED||!XIODeviceBase_isOpen_base(comm->m_class.m_io))
		return;//协议栈还未准备好
	RecvSendData(comm);
}
bool VXCommunicatorBase_connect(XDataFrameComm* comm)
{
	if (comm->m_state == XDFC_STATE_ENABLED)
		return true;
	if (XVtableGetFunc(XCommunicatorBase_class_init(), EXCommunicatorBase_Connect, bool(*)(XCommunicatorBase*))(comm))
	{
		if(comm->m_frameEndMode== XDFC_FRAME_END_TIMEOUT)
		{
			comm->m_eRcvState = XDFC_STATE_RX_INIT;  // 初始状态：等待总线空闲
			XTimerBase_start_base(comm->m_timerRecvExpired);
		}
		else
		{
			comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 初始状态：总线空闲
		}
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
			//m_modbus->pvMBFrameStopCur(m_modbus); // 停止协议栈（暂停接收/发送，清理临时资源）
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
//在各自事件循环中触发
static void SendExpired(XDataFrameComm* comm)
{
	comm->m_eSndState = XDFC_STATE_TX_IDLE;
	XTimerBase_stop_base(comm->m_timerSendExpired);  // 关闭定时器
}
//定时器触发
static void TimerSendExpired(XDataFrameComm* comm)
{
	//发完一帧数据总线等待
	//if (comm->m_eSndState == XDFC_STATE_TX_END)
	//XPrintf("发送完一帧数据\n");
	{
		//XObject_postEvent(comm, XEventFunc_create_oneAccept(SendExpired,comm),XEVENT_PRIORITY_NORMAL);
		XObject_postFunc(comm, SendExpired, comm, XEVENT_SEND_DIRECT, XEVENT_PRIORITY_NORMAL);
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
			/*XTimerBase_delete_base(comm->m_timerSendExpired);
			comm->m_timerSendExpired = NULL;*/
			XTimerBase_stop_base(comm->m_timerSendExpired);
		}
	}
	else if (mode == XDFC_COMM_MODE_HALF_DUPLEX)
	{//半双工
		
		if (comm->m_timerSendExpired==NULL)
		{
			XTimerBase* timer = XTimerTimeWheel_create();
			XTimerBase_setTimerCallback_base(timer, TimerSendExpired);
			XTimerBase_setUserData_base(timer, comm);
			XTimerBase_setAutoDelete(timer,false);
			XTimerBase_setTimeout_base(timer, XDFC_HALF_DUPLEX_SEND_WAIT_TIME);
			comm->m_timerSendExpired = timer;
		}
	}
	comm->m_commMode = mode;
	return XDFC_ENOERR;
}
//在各自事件循环中触发
static void RecvExpired(XDataFrameComm* comm)
{
	//接收超时等待
	//XPrintf("超时\n");
	switch (comm->m_eRcvState) {
	case XDFC_STATE_RX_INIT:  // 初始状态超时（总线空闲，进入IDLE）
		XDataFrameComm_postEvent(comm, XEvent_create(comm, XDFC_READY, 0));
		break;

	case XDFC_STATE_RX_RCV:   // 接收中状态超时（帧接收完成）
		XDataFrameComm_recvValid(comm);
		break;

	case XDFC_STATE_RX_ERROR: // 错误状态超时（忽略）
		XDataFrameComm_postEvent(comm, XEvent_create(comm, XDFC_RX_BUFFER_OVERFLOW, 0));
		break;
	case XDFC_STATE_RX_IDLE: //接收空闲
		break;
	default:            // 非法状态（断言检查）
		assert((comm->m_eRcvState == XDFC_STATE_RX_INIT) || (comm->m_eRcvState == XDFC_STATE_RX_RCV) || (comm->m_eRcvState == XDFC_STATE_RX_ERROR));
	}

	comm->m_eRcvState = XDFC_STATE_RX_IDLE;  // 切换到接收空闲状态
	XTimerBase_stop_base(comm->m_timerRecvExpired);  // 关闭定时器
}
//定时器触发
static void TimerRecvExpired(XDataFrameComm* comm)
{  
	//XObject_postEvent(comm, XEventFunc_create_oneAccept(RecvExpired, comm), XEVENT_PRIORITY_NORMAL);
	XObject_postFunc(comm, RecvExpired, comm, XEVENT_SEND_DIRECT, XEVENT_PRIORITY_NORMAL);
}
XDFC_ErrorCode VXDataFrameComm_setFrameEndType(XDataFrameComm* comm, XDFC_FrameEndType mode)
{
	if (comm->m_state == XDFC_STATE_ENABLED)
		return XDFC_EILLSTATE;//协议栈启动中不支持修改
	if (mode == XDFC_FRAME_END_TIMEOUT)
	{//设置为超时结束
		
		if (comm->m_timerRecvExpired == NULL)
		{
			XTimerBase* timer = XTimerTimeWheel_create();
			XTimerBase_setTimerCallback_base(timer, TimerRecvExpired);
			XTimerBase_setUserData_base(timer, comm);
			XTimerBase_setAutoDelete(timer, false);
			XTimerBase_setTimeout_base(timer, XDFC_FRAME_END_TIMEOUT_TIME);
			XTimerBase_setInterval_base(timer, XDFC_FRAME_END_TIMEOUT_TIME);
			comm->m_timerRecvExpired = timer;
		}
	}
	else if (mode == XDFC_FRAME_END_MARKER)
	{//设置为标志结束
		if (comm->m_timerRecvExpired)
		{
			XTimerBase_stop_base(comm->m_timerRecvExpired);
		}
	}
	comm->m_frameEndMode = mode;
	return XDFC_ENOERR;
}

XDFC_ErrorCode VXDataFrameComm_sendData(XDataFrameComm* comm, XByteArray* data)
{
	if (XVector_isEmpty_base(data))
	{
		XVector_delete_base(data);
		return XDFC_EINVAL;
	}
	if (comm->m_sendValidCb)
		comm->m_sendValidCb(comm,data);
	if (!XQueueBase_push_base(comm->m_sendFrameQueue, &data))
	{
#if XDFC_QUEUE_FULL_SHOW
		XPrintf("发送队列溢出当前最大:%d,建议增大队列,调整:XDFC_FRAME_SEND_QUEUE_COUNT\n", XDFC_FRAME_SEND_QUEUE_COUNT);
#endif
		XVector_delete_base(data);
		return XDFC_ENORES;
	}
	return XDFC_ENOERR;
}
//定期发送的定时器回调函数
static void SendDataPeriodicCb(PeriodicNode* node)
{
	if (!XDataFrameComm_isConnected_base(node->comm))
		return;//还未连接上不发送
	XByteArray* v =XByteArray_create(0);
	if (v == NULL)
		return;
	XVector_copy_base(v, node->data);
	if (!XQueueBase_push_base(node->comm->m_sendFrameQueue, &v))
	{
#if XDFC_QUEUE_FULL_SHOW
		XPrintf("发送队列溢出当前最大:%d,建议增大队列,调整:XDFC_FRAME_SEND_QUEUE_COUNT\n", XDFC_FRAME_SEND_QUEUE_COUNT);
#endif
		XVector_delete_base(v);
		return XDFC_ENORES;
	}
	//XDataFrameComm_sendData_base(XPair_First(pair, XDataFrameComm*),v);
}
XHandle VXDataFrameComm_sendPeriodicData(XDataFrameComm* comm, XByteArray* data, uint32_t time)
{
	if (XVector_isEmpty_base(data))
		return NULL;
	PeriodicNode* node= XMemory_malloc(sizeof(PeriodicNode));
	if (node == NULL)
	{
		return NULL;
	}
	XTimerTimeWheel* timer = XTimerTimeWheel_create();
	if (timer == NULL)
	{
		XMemory_free(node);
		return NULL;
	}
	if (comm->m_sendValidCb)
		comm->m_sendValidCb(comm,data);

	node->comm = comm;
	node->data = data;
	node->timer = timer;

	XListBase_push_back_base(comm->m_periodicSendList,&node);
	XTimerBase_setTimeout_base(timer, time);
	XTimerBase_setInterval_base(timer, time);
	XTimerBase_setUserData_base(timer, node);
	XTimerBase_setTimerCallback_base(timer, SendDataPeriodicCb);
	XTimerBase_start_base(timer);
	return node;
}

bool VXDataFrameComm_removePeriodicSendData(XDataFrameComm* comm, XHandle handle)
{
	if(XListBase_isEmpty_base(comm->m_periodicSendList))
		return false;
	size_t size = XContainerSize(comm->m_periodicSendList);
	XListBase_remove_base(comm->m_periodicSendList,&handle);
	if(size== XContainerSize(comm->m_periodicSendList))
		return false;
	PeriodicNode* node = handle;
	XByteArray_delete_base(node->data);
	XTimerBase_delete_base(node->timer);
	XMemory_free(node);
	return true;
}
//接收验证Crc16回调
static bool XRecvValidCrc16Cb(XDataFrameComm* comm, const XByteArray* data)
{
	return  XCrc_get16(XContainerDataPtr(data), XContainerSize(data)) == 0;
}
void VXDataFrameComm_setRecvValidCRC16(XDataFrameComm* comm, bool enableCRC16)
{
	if (comm)
	{
		if (enableCRC16)
			comm->m_recvValidCb = XRecvValidCrc16Cb;
		else
			comm->m_recvValidCb = NULL;
	}
}
//发送验证Crc16回调
static void XSendValidCrc16Cb(XDataFrameComm* comm, XByteArray* data)
{
	//设置crc校验
	XVector_append_crc16(data, XCRC_BYTE_ORDER_LITTLE_ENDIAN);
}
void VXDataFrameComm_setSendValidCRC16(XDataFrameComm* comm, bool enableCRC16)
{
	if (comm)
	{
		if (enableCRC16)
			comm->m_sendValidCb = XSendValidCrc16Cb;
		else
			comm->m_sendValidCb = NULL;
	}
}

void VXDataFrameComm_deinit(XDataFrameComm* comm)
{
	if (comm->m_sendFrameQueue)
	{
		XQueueBase_delete_base(comm->m_sendFrameQueue);
		comm->m_sendFrameQueue;
	}
	if (comm->m_periodicSendList)
	{
		for_each_iterator(comm->m_periodicSendList, XListSLinked, it)
		{
			PeriodicNode* node=XListSLinked_iterator_data(&it);
			XByteArray_delete_base(node->data);
			XTimerBase_delete_base(node->timer);
			XMemory_free(node);
		}
		XListBase_delete_base(comm->m_periodicSendList);
		comm->m_periodicSendList;
	}
	if (comm->m_funcCodeMap)
	{
		XFuncCodeMap_delete(comm->m_funcCodeMap);
		comm->m_funcCodeMap;
	}
	if (comm->m_sendFrameHead)
	{
		XVector_delete_base(comm->m_sendFrameHead);
		comm->m_sendFrameHead = NULL;
	}
	if (comm->m_sendFrameTail)
	{
		XVector_delete_base(comm->m_sendFrameTail);
		comm->m_sendFrameTail = NULL;
	}
	if (comm->m_recvFrameHead)
	{
		XVector_delete_base(comm->m_recvFrameHead);
		comm->m_recvFrameHead = NULL;
	}
	if (comm->m_recvFrameTail)
	{
		XVector_delete_base(comm->m_recvFrameTail);
		comm->m_recvFrameTail = NULL;
	}
	if (comm->m_timerRecvExpired)
	{
		XTimerBase_delete_base(comm->m_timerRecvExpired);
		comm->m_timerRecvExpired = NULL;
	}
	if (comm->m_timerSendExpired)
	{
		XTimerBase_delete_base(comm->m_timerSendExpired);
		comm->m_timerSendExpired = NULL;
	}
	//调用父类释放函数
	XVtableGetFunc(XCommunicatorBase_class_init(), EXClass_Deinit, void(*)(XCommunicatorBase*))(comm);
}
void VXDataFrameComm_setRecvFrameHead(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
{
	if (comm == NULL)
		return;
	if (data == NULL)
	{//关掉接收帧头判断
		if (comm->m_recvFrameHead != NULL)
		{
			XVector_delete_base(comm->m_recvFrameHead);
			comm->m_recvFrameHead = NULL;
		}
	}
	else if (dataSize > 0)
	{//设置接收帧头判断
		if (comm->m_recvFrameHead == NULL)
		{
			XByteArray* v =XByteArray_create(0);
			XVector_append_array_base(v, data, dataSize);
			comm->m_recvFrameHead = v;

		}
	}
}

void VXDataFrameComm_setRecvFrameTail(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
{
	if (comm == NULL)
		return;
	if (data == NULL)
	{//关掉接收帧尾判断
		if (comm->m_recvFrameTail != NULL)
		{
			XVector_delete_base(comm->m_recvFrameTail);
			comm->m_recvFrameTail = NULL;
		}
	}
	else if (dataSize > 0)
	{//设置接收帧尾判断
		if (comm->m_recvFrameTail == NULL)
		{
			XByteArray* v =XByteArray_create(0);
			XVector_append_array_base(v, data, dataSize);
			comm->m_recvFrameTail = v;
		}
	}
}

void VXDataFrameComm_setSendFrameHead(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
{
	if (comm == NULL)
		return;
	if (data == NULL)
	{//关掉发送帧头
		if (comm->m_sendFrameHead != NULL)
		{
			XVector_delete_base(comm->m_sendFrameHead);
			comm->m_sendFrameHead = NULL;
		}
	}
	else if (dataSize > 0)
	{//设置发送帧头
		if (comm->m_sendFrameHead == NULL)
		{
			XByteArray* v =XByteArray_create(0);
			XVector_append_array_base(v, data, dataSize);
			comm->m_sendFrameHead = v;
		}
	}
}

void VXDataFrameComm_setSendFrameTail(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
{
	if (comm == NULL)
		return;
	//XPrintf("设置发送帧尾巴\n");
	if (data == NULL)
	{//关掉发送帧尾
		if (comm->m_sendFrameTail != NULL)
		{
			XVector_delete_base(comm->m_sendFrameTail);
			comm->m_sendFrameTail = NULL;
		}
	}
	else if (dataSize > 0)
	{//设置发送帧尾
		if (comm->m_sendFrameTail == NULL)
		{
			XByteArray* v =XByteArray_create(0);
			XVector_append_array_base(v, data, dataSize);
			comm->m_sendFrameTail = v;
		}
	}
}
