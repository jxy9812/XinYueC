#include"XModbusBase.h"
#include"XTimerGroupWheel.h"
#include"XQueueBase.h"
#include"XModbusFrame.h"
#include"XModbusProto.h"
#include"XModbusFunctionHandler.h"
typedef struct XModbusFrame XModbusFrame;
static XModbusErrorCode VXModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frameData);
static XModbusErrorCode VXModbusBase_recvFrame(XModbusBase* modbus, XModbusFrame* frameData);
static void VXModbusBase_poll(XModbusBase* modbus);
static bool VXCommunicatorBase_connect(XModbusBase* modbus);
static bool VXCommunicatorBase_disconnect(XModbusBase* modbus);
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
		VXModbusBase_sendFrame,VXModbusBase_recvFrame,
	};*/
	//追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Poll, VXModbusBase_poll);
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Connect, VXCommunicatorBase_connect);
	XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Disconnect, VXCommunicatorBase_disconnect);
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
	//检查回调函数是否超时
	if (modbus->m_recvHandleMaster != NULL)
	{
		XModbusFrameDataRecvHandle** Handle = XVector_front_base(modbus->m_recvHandleMaster);
		if (Handle != NULL)
		{//已经超时了
			if ((*Handle) != NULL)
			{
				if ((*Handle)->timeout < XTimerBase_getCurrentTime())
				{
					//printf("%d\n", (*Handle)->timeout);
					if ((*Handle)->pRecvHandCallFunc)
						(*Handle)->pRecvHandCallFunc(modbus, NULL);
					XMemory_free(*Handle);
					XVector_pop_front_base(modbus->m_recvHandleMaster);
				}
			}
			else
			{
				XVector_pop_front_base(modbus->m_recvHandleMaster);
			}
		}
	}
	//return error;
	//处理设备缓冲区
	if (modbus->m_eSndState == STATE_TX_XMIT || XIODeviceBase_getBytesAvailable_base(((XCommunicatorBase*)modbus)->m_io)==0)
	{
		//printf("发送数据\n");
		XClassGetVirtualFunc(modbus, EXModbusBase_TransmitFSM,bool(*)(XModbusBase*))(modbus);
	}
	else //if(modbus->eSndState != STATE_TX_XMIT)
	{
		//printf("处理接收数据\n");
		XClassGetVirtualFunc(modbus, EXModbusBase_ReceiveFSM, bool(*)(XModbusBase*))(modbus);
	}
    return error;
}
//向接收队列中增加功能码帧数据
static bool recvFrameQueue_pushExecuteFrame(XModbusBase* modbus, XModbusFrame* recvFrame)
{
	if (!XModbusFrameQueue_push(modbus->m_recvFrameQueue, recvFrame))
	{
#if MB_QUEUE_FULL_SHOW
		printf("接收帧队列溢出当前最大:%d,建议增大队列,调整:MB_FRAME_RECV_QUEUE_COUNT\n", MB_FRAME_RECV_QUEUE_COUNT);
#endif // MB_QUEUE_FULL_SHOW
		return false;
	}

	return XModbusBase_sendEvent(modbus, EV_EXECUTE);
}
// 接收到完整的 Modbus 帧
static XModbusErrorCode XModbus_EV_FRAME_RECEIVED(XModbusBase* modbus)
{
	XModbusErrorCode error = MB_ENOERR;
	/* ((char*)(XVector_begin(modbus->recvBuffer)))[XVector_getSize_base(modbus->recvBuffer)] = 0;
	 printf("数据:%s  大小:%d buff接收缓冲区大小:%d\n",XVector_begin(modbus->recvBuffer), XVector_getSize_base(modbus->recvBuffer), XVector_getCapacity_base(modbus->recvBuffer));*/

	 // 调用对应模式的接收函数，获取帧数据（地址、缓冲区、长度）
	XModbusFrame* recvFrame = XModbusFrame_create();
	if (recvFrame == NULL)
		return MB_ENORES;
	recvFrame->mode = modbus->m_mode;
	//解析数据帧
	error = XClassGetVirtualFunc(modbus, EXModbusBase_RecvFrame, XModbusErrorCode(*)(XModbusBase*, XModbusFrame*))(modbus, recvFrame);
	//printf("进入帧数据处理:%d\n", error);
	if (error == MB_ENOERR) {
		uint8_t address = XModbusFrame_getAddress(recvFrame);
		uint8_t code = XModbusFrame_getFuncCode(recvFrame);
#if MB_RECV_FRAME_SHOW
		XString* str = XModbusFrameRTU_to16HexString(recvFrame);
		printf("接收帧:%s\n", XString_c_str(str));
		//            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
		XString_free_base(str);
#endif // MB_SEND_FRAME_SHOW

		if (XModbusBase_isMaster(modbus))
		{
			//printf("当前是主站\n");
		   //printf("将有效的帧加入接收队列处理\n");
			if (recvFrameQueue_pushExecuteFrame(modbus, recvFrame))
				return error;
			error = MB_ENORES;
		}
		else if ((address == modbus->m_address) || (address == MB_ADDRESS_BROADCAST))
		{//当前是从站
			//printf("将有效的帧加入接收队列处理\n");
			if (recvFrameQueue_pushExecuteFrame(modbus, recvFrame))
				return error;
			error = MB_ENORES;
			//                xMBPortEventPost(EV_EXECUTE); // 触发功能码执行事件
		}
		else
		{
			error = MB_EIO;
		}
	}
	XModbusFrame_free(recvFrame);
	return error;
}
//功能码处理
static void  XModbus_EV_EXECUTE(XModbusBase* modbus)
{
	//printf("处理功能码\n");
	if (modbus == NULL || XModbusFrameQueue_empty(modbus->m_recvFrameQueue))
		return;

	XModbusFrame* frame = XModbusFrameQueue_top(modbus->m_recvFrameQueue);
	uint8_t address = XModbusFrame_getAddress(frame);
	uint8_t code = XModbusFrame_getFuncCode(frame);
	//XString* str = XModbusFrameRTU_to16HexString(frame);
	//printf("地址:%X 功能码:%X 完整:%s\n", address, code, XString_c_str(str));
	////            // 检查帧是否针对当前从机或广播地址（广播地址帧无需响应）
	//XString_free_base(str);

	//如果是主站检查是否有回调函数
	if (XModbusBase_isMaster(modbus)/*&& modbus->recvHandleMaster!=NULL*/)
	{
		uint16_t waitAddressCode = (address << 8 | code);
		XModbusFrameDataRecvHandle** value = XVector_find_base(modbus->m_recvHandleMaster, &waitAddressCode);
		//printf("value:%p\n",value);
		if (value)
		{
			if (frame->recvHandle != NULL)//释放准备交换
				XMemory_free(frame->recvHandle);
			//拷贝数据
			frame->recvHandle = *value;
			*value = NULL;
			//执行回调函数
			frame->recvHandle->pRecvHandCallFunc(modbus, frame);
			//释放一个资源
			XModbusFrameQueue_pop(modbus->m_recvFrameQueue);
			XVector_erase_base(modbus->m_recvHandleMaster, value);
			return;
		}
	}
	//执行功能码
	XModbusFunctionHandler* FunctionHandler = XModbusFuncCodeList_findFuncCode(modbus->m_funcCodeList, code);
	if (FunctionHandler == NULL)
	{//没有对应的处理函数

	}
	else
	{
		//printf("执行功能码\n");
		assert(FunctionHandler->function);
		FunctionHandler->function(modbus, frame, FunctionHandler);//处理中
	}

	//释放一个资源
	XModbusFrameQueue_pop(modbus->m_recvFrameQueue);
}
//事件处理
static void XModbus_EventHandling(XModbusBase* modbus)
{
	XModbusEventType eEvent;
	// 获取端口事件（如接收完成、定时器超时，驱动协议栈处理）
	if (!XQueueBase_receive_base(modbus->m_eventQueue, &eEvent))
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
	case EV_FRAME_RECEIVED: XModbus_EV_FRAME_RECEIVED(modbus);  break; // 接收到完整的 Modbus 帧
	case EV_EXECUTE: XModbus_EV_EXECUTE(modbus); break;
	}
	//return error; // 轮询成功（无错误或错误已处理）
}
void VXModbusBase_poll(XModbusBase* modbus)
{
	//printf("poll\n");
	if (modbus->m_state != STATE_ENABLED) {
		return MB_EILLSTATE; // 非法状态，直接返回错误
	}
	//printf("chulishijian \n");
	XModbus_EventHandling(modbus);
	XCommunicatorBase* comm = modbus;
	//处理定时器任务
	if (comm->m_wheel)
		XTimerGroupWheel_poll_base(comm->m_wheel);
}

bool VXCommunicatorBase_connect(XModbusBase* modbus)
{
	
	if (modbus->m_state == STATE_ENABLED)
		return true;
	if (XVtableGetFunc(XCommunicatorBase_class_init(), EXCommunicatorBase_Connect, bool(*)(XCommunicatorBase*))(modbus))
	{
		modbus->m_eRcvState = STATE_RX_INIT;  // 初始状态：等待总线空闲
		//XTimerBase_start_base(modbus->);  // 启动定时器（T35用于检测帧间隔）
		modbus->m_state = STATE_ENABLED; // 更新状态为启用
		return true;
	}
	
	return false;
}

bool VXCommunicatorBase_disconnect(XModbusBase* modbus)
{
	if (XVtableGetFunc(XCommunicatorBase_class_init(), EXCommunicatorBase_Disconnect, bool(*)(XCommunicatorBase*))(modbus))
	{
		if (modbus->m_state == STATE_ENABLED) { // 从启用状态禁用
			//modbus->pvMBFrameStopCur(modbus); // 停止协议栈（暂停接收/发送，清理临时资源）
			modbus->m_state = STATE_DISABLED; // 更新状态为禁用
			return true;
		}
		else if (modbus->m_state == STATE_DISABLED) { // 已禁用状态，直接返回成功
			return true;
		}
		else { // 未初始化状态
			return false;
			//error = MB_EILLSTATE; // 非法状态错误
		}
		//return error;
	}
	return false;
}
