#include"XDataFrameComm.h"
#include"XEvent.h"
#include"XCircularQueueAtomic.h"
#include"XDataFrameCommConfig.h"
#include"XIODeviceBase.h"
#include"XListSLinked.h"
#include"XString.h"
#include"XEquality.h"
#include<string.h>
#include<stdarg.h>
XDataFrameComm* XDataFrameComm_create(XIODeviceBase* io)
{
	if (io == NULL)
		return NULL;
	XDataFrameComm* comm = XMemory_malloc(sizeof(XDataFrameComm));
	XDataFrameComm_init(comm, io);
	return comm;
}
void XDataFrameComm_init(XDataFrameComm* comm, XIODeviceBase* io)
{
	if (comm == NULL||io==NULL)
		return ;
	//开始初始化
	memset(((XCommunicatorBase*)comm) + 1, 0, sizeof(XDataFrameComm) - sizeof(XCommunicatorBase));
	XCommunicatorBase_init(comm,io);
	XClassGetVtable(comm) = XDataFrameComm_class_init();

	if (io != NULL)
	{
		XIODeviceBase_setReadBuffer_base(io, XDFC_DEVICE_RECV_BUFFER_SIZE);
		XIODeviceBase_setWriteBuffer_base(io, XDFC_DEVICE_SEND_BUFFER_SIZE);
	}
	XCommunicatorBase_recvAsync_base(comm, XDFC_RECV_BUFFER_SIZE);
	comm->m_state = XDFC_STATE_NOT_INITIALIZED;
	comm->m_eventDispatcher = XEventDispatcher_createDefault(XDFC_EVENT_QUEUE_COUNT);
	comm->m_sendFrameQueue = XCircularQueueAtomic_Create(XVector*, XDFC_FRAME_SEND_QUEUE_COUNT);
	comm->m_periodicSendList = XListSLinked_Create(XPair*);
	comm->m_periodicSendList->m_equality = XEquality_ptr;

	XEventDispatcher_setAllEventCb(comm->m_eventDispatcher, XDataFrameComm_EvnetHandCb, comm);
	XDataFrameComm_setGetFuncCodeCb(comm,XDataFrameComm_GetFuncCodeCb);
	XDataFrameComm_setCommMode_base(comm,XDFC_COMM_MODE_FULL_DUPLEX);
	XDataFrameComm_setFrameEndType_base(comm,XDFC_FRAME_END_TIMEOUT);
}

XDFC_ErrorCode XDataFrameComm_setCommMode_base(XDataFrameComm* comm, XDFC_CommMode mode)
{
	if (ISNULL(comm, "") || ISNULL(XClassGetVtable(comm), ""))
		return XDFC_EINVAL;
	return XClassGetVirtualFunc(comm, EXDataFrameComm_SetCommMode, XDFC_ErrorCode(*)(XDataFrameComm*, XDFC_CommMode))(comm, mode);
}

XDFC_ErrorCode XDataFrameComm_setFrameEndType_base(XDataFrameComm* comm, XDFC_FrameEndType mode)
{
	if (ISNULL(comm, "") || ISNULL(XClassGetVtable(comm), ""))
		return XDFC_EINVAL;
	return XClassGetVirtualFunc(comm, EXDataFrameComm_SetFrameEndType, XDFC_ErrorCode(*)(XDataFrameComm*, XDFC_FrameEndType))(comm, mode);
}

XDFC_ErrorCode XDataFrameComm_sendData_base(XDataFrameComm* comm, XVector* data)
{
	if (ISNULL(comm, "") || ISNULL(XClassGetVtable(comm), ""))
		return XDFC_EINVAL;
	return XClassGetVirtualFunc(comm, EXDataFrameComm_SendData, XDFC_ErrorCode(*)(XDataFrameComm*, XVector*))(comm, data);
}

XDFC_ErrorCode XDataFrameComm_sendText(XDataFrameComm* comm, bool appendNull, const char* str)
{
	if (ISNULL(comm, "") || ISNULL(str, "")|| ISNULL(XClassGetVtable(comm), ""))
		return XDFC_EINVAL;
	XVector* data = XVector_Create(uint8_t);
	if (data == NULL)
		return XDFC_ENORES;
	XVector_append_array_base(data, str, strlen(str) + (appendNull ? 1 : 0));
	return XDataFrameComm_sendData_base(comm, data);
}

XDFC_ErrorCode XDataFrameComm_sendTextFmt(XDataFrameComm* comm, bool appendNull, const char* format, ...)
{
	if (ISNULL(comm, "") || ISNULL(format, "") || ISNULL(XClassGetVtable(comm), ""))
		return XDFC_EINVAL;
	XVector* data = XVector_Create(uint8_t);
	if (data == NULL)
		return XDFC_ENORES;
	va_list args;
	va_start(args, format);
	bool result = XVector_format_text_core(data, appendNull, format, args);
	va_end(args);

	// 如果失败，释放内存并返回NULL
	if (!result) {
		XVector_delete_base(data);
		return XDFC_ENORES;
	}
	return XDataFrameComm_sendData_base(comm, data);
}

XHandle XDataFrameComm_addPeriodicSendData_base(XDataFrameComm* comm, XVector* data, uint32_t time)
{
	if (ISNULL(comm, "") || ISNULL(data, "") || ISNULL(time, "") || ISNULL(XClassGetVtable(comm), ""))
		return NULL;
	return XClassGetVirtualFunc(comm, EXDataFrameComm_AddSendDataPeriodic, XHandle(*)(XDataFrameComm*, XVector*, uint32_t))(comm, data,time);
}

XHandle XDataFrameComm_addPeriodicSendText(XDataFrameComm* comm, bool appendNull, uint32_t time, const char* str)
{
	if (ISNULL(comm, "") || ISNULL(str, "") || ISNULL(time, "") || ISNULL(XClassGetVtable(comm), ""))
		return NULL;
	XVector* data = XVector_Create(uint8_t);
	if (data == NULL)
		return NULL;
	XVector_append_array_base(data, str, strlen(str) + (appendNull ? 1 : 0));
	return XDataFrameComm_addPeriodicSendData_base(comm,data,time);
}

XHandle XDataFrameComm_addPeriodicSendTextFmt(XDataFrameComm* comm, bool appendNull, uint32_t time, const char* format, ...)
{
	if (ISNULL(comm, "") || ISNULL(format, "") || ISNULL(time, "") || ISNULL(XClassGetVtable(comm), ""))
		return NULL;
	XVector* data = XVector_Create(uint8_t);
	if (data == NULL)
		return XDFC_ENORES;
	va_list args;
	va_start(args, format);
	bool result = XVector_format_text_core(data, appendNull, format, args);
	va_end(args);

	// 如果失败，释放内存并返回NULL
	if (!result) {
		XVector_delete_base(data);
		return XDFC_ENORES;
	}
	return XDataFrameComm_addPeriodicSendData_base(comm, data, time);
}

bool XDataFrameComm_removePeriodicSendData_base(XDataFrameComm* comm, XHandle handle)
{
	if (ISNULL(comm, "") || ISNULL(handle, "") || ISNULL(XClassGetVtable(comm), ""))
		return false;
	return XClassGetVirtualFunc(comm, EXDataFrameComm_RemoveSendDataPeriodic, bool(*)(XDataFrameComm*, XHandle))(comm, handle);
}

void XDataFrameComm_setRecvFrameHead_base(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
{
	if (ISNULL(comm, "") || ISNULL(data, "") || ISNULL(dataSize, "") || ISNULL(XClassGetVtable(comm), ""))
		return ;
	XClassGetVirtualFunc(comm, EXDataFrameComm_SetRecvFrameHead, bool(*)(XDataFrameComm*, const uint8_t*, uint8_t))(comm, data, dataSize);
}

void XDataFrameComm_setRecvFrameTail_base(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
{
	if (ISNULL(comm, "") || ISNULL(data, "") || ISNULL(dataSize, "") || ISNULL(XClassGetVtable(comm), ""))
		return;
	XClassGetVirtualFunc(comm, EXDataFrameComm_SetRecvFrameTail, bool(*)(XDataFrameComm*, const uint8_t*, uint8_t))(comm, data, dataSize);
}

void XDataFrameComm_setSendFrameHead_base(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
{
	if (ISNULL(comm, "") || ISNULL(data, "") || ISNULL(dataSize, "") || ISNULL(XClassGetVtable(comm), ""))
		return;
	XClassGetVirtualFunc(comm, EXDataFrameComm_SetSendFrameHead, bool(*)(XDataFrameComm*, const uint8_t*, uint8_t))(comm, data, dataSize);
}

void XDataFrameComm_setSendFrameTail_base(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
{
	if (ISNULL(comm, "") || ISNULL(data, "") || ISNULL(dataSize, "") || ISNULL(XClassGetVtable(comm), ""))
		return;
	XClassGetVirtualFunc(comm, EXDataFrameComm_SetSendFrameTail, bool(*)(XDataFrameComm*, const uint8_t*, uint8_t))(comm, data, dataSize);
}

void XDataFrameComm_setRecvValidCb(XDataFrameComm* comm, XRecvValidCb cb)
{
	if (comm)
	{
		comm->m_recvValidCb = cb;
	}
}

void XDataFrameComm_setSendValidCb(XDataFrameComm* comm, XSendValidCb cb)
{
	if (comm)
	{
		comm->m_sendValidCb = cb;
	}
}

void XDataFrameComm_setRecvValidCRC16_base(XDataFrameComm* comm, bool enableCRC16)
{
	if (ISNULL(comm, "") || ISNULL(XClassGetVtable(comm), ""))
		return ;
	 XClassGetVirtualFunc(comm, EXDataFrameComm_SetRecvValidCRC16, void(*)(XDataFrameComm*, bool))(comm, enableCRC16);
}

void XDataFrameComm_setSendValidCRC16_base(XDataFrameComm* comm, bool enableCRC16)
{
	if (ISNULL(comm, "") || ISNULL(XClassGetVtable(comm), ""))
		return;
	XClassGetVirtualFunc(comm, EXDataFrameComm_SetSendValidCRC16, void(*)(XDataFrameComm*, bool))(comm, enableCRC16);
}

void XDataFrameComm_addFuncCode(XDataFrameComm* comm, uint8_t code, XFuncCodeCb cb, void* userData)
{
	if (comm == NULL)
		return;
	if (comm->m_funcCodeMap == NULL)
		comm->m_funcCodeMap = XFuncCodeMap_create();
	XFuncCodeMap_add(comm->m_funcCodeMap, code,cb,userData);
}

void XDataFrameComm_removeFuncCode(XDataFrameComm* comm, uint8_t code)
{
	if (comm == NULL|| comm->m_funcCodeMap == NULL)
		return;
	XFuncCodeMap_remove(comm->m_funcCodeMap,code);
}

void XDataFrameComm_clearFuncCode(XDataFrameComm* comm)
{
	if (comm == NULL || comm->m_funcCodeMap == NULL)
		return;
	XFuncCodeMap_delete(comm->m_funcCodeMap);
	comm->m_funcCodeMap = NULL;
}

void XDataFrameComm_setGetFuncCodeCb(XDataFrameComm* comm, GetFuncCodeCb cb)
{
	if (comm)
		comm->m_getFuncCode = cb;
}

XEventRecvFrame* XEventRecvFrame_create(int code, size_t timestamp,XVector* frame)
{
	XEventRecvFrame* ev = XMemory_malloc(sizeof(XEventRecvFrame));
	if (ev == NULL)
		return NULL;
	XEventMin_init(ev, code, timestamp);
	ev->frame = frame;
	return ev;
}
XEventFuncCode* XEventFuncCode_create(int code, size_t timestamp, XVector* frame, uint8_t funcCode)
{
	XEventFuncCode* ev = XMemory_malloc(sizeof(XEventFuncCode));
	if (ev == NULL)
		return NULL;
	XEventMin_init(ev, code, timestamp);
	ev->m_parent.frame = frame;
	ev->funcCode = funcCode;
	return ev;
}
//接收到完整帧事件
void XDataFrameComm_EvnetFrame_ReceivedCb(XEventMin* event)
{
	XEventRecvFrame* ev = event;
	XVector* frame = ev->frame;
	/*	if (!XQueueBase_receive_base(comm->m_recvFrameQueue, &v))
			return;*/
	//printf("接收帧\n");
#if XDFC_RECV_FRAME_16HEX_SHOW
	XString* str = XString_to16HexString(XContainerDataPtr(frame), XContainerSize(frame));
	if (str != NULL)
	{
		printf("\n16进制接收帧:%s\n", XString_c_str(str));
		XString_delete_base(str);
	}
#endif // XDFC_RECV_FRAME_16HEX_SHOW
#ifdef XDFC_RECV_FRAME_STR_SHOW
	if (XVector_Back_Base(frame, char) != 0)
	{
		char c = 0;
		XVector_push_back_base(frame, &c);
		printf("\nString接收帧:%s\n", XContainerDataPtr(frame));
		--XContainerSize(frame);
	}
	else
	{
		printf("\nString接收帧:%s\n", XContainerDataPtr(frame));
	}
#endif // XDFC_RECV_FRAME_STR_SHOW
	XDataFrameComm* comm = event->userData;
	uint8_t funcCode;
	if (comm->m_funcCodeMap == NULL||comm->m_getFuncCode==NULL|| !comm->m_getFuncCode(comm,frame,&funcCode))
	{//没有功能码处理或获取失败 直接释放
		ev->frame = NULL;
		XVector_delete_base(frame);
		XEvent_Accept(ev);
		return;
	}
	if (!XDataFrameComm_sendEvent(comm, XEventFuncCode_create(XDFC_EXECUTE,0, frame, funcCode)))
	{//添加失败，队列满了
		XVector_delete_base(frame);//释放帧数据以免内存泄露
	}
	ev->frame = NULL;
	XEvent_Accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
}
//功能码事件回调
void XDataFrameComm_EvnetExecuteCb(XEventMin* event)
{
	//printf("功能码事件\n");
	XEventFuncCode* ev = event;
	XVector* frame = ev->m_parent.frame;
	XDataFrameComm* comm = event->userData;
	XFuncCodeNode* node = NULL;
	if (comm->m_funcCodeMap != NULL)
	{
		node = XFuncCodeMap_value(comm->m_funcCodeMap, ev->funcCode);
	}
	if (node==NULL)
	{
		ev->m_parent.frame = NULL;
		XVector_delete_base(frame);//释放帧数据以免内存泄露
		XEvent_Accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
		return;
	}
	if(node->cb!=NULL)
		node->cb(ev->funcCode,comm, frame,node->userData);
	ev->m_parent.frame = NULL;
	XVector_delete_base(frame);//释放帧数据以免内存泄露
	XEvent_Accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
}
bool XDataFrameComm_GetFuncCodeCb(XDataFrameComm* comm, XVector* data, uint8_t* code)
{
	if(data==NULL||XVector_isEmpty_base(data)|| code==NULL)
		return false;
	*code=*((uint8_t*)XContainerDataPtr(data));
	return true;
}
bool XDataFrameComm_sendEvent(XDataFrameComm* comm, XEventMin* ev)
{
	if (comm == NULL || ev == NULL)
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
void XDataFrameComm_EvnetHandCb(XEventMin* event)
{
#if XDFC_EVENT_HANDLE_SHOW
#if XDFC_ENUM_TO_STRING
	printf("准备处理事件:%s\n", XDataFrameComm_EventType_toString(event->code));
#else
	printf("准备处理事件:%d\n", eEvent);
#endif
#endif // MB_EVENT_SHOH
	switch (event->code)
	{
		//case XDFC_READY:break;
		case XDFC_FRAME_RECEIVED:XDataFrameComm_EvnetFrame_ReceivedCb(event); break;
		case XDFC_EXECUTE:XDataFrameComm_EvnetExecuteCb(event); break;
		//case XDFC_FRAME_SENT:break;
		default:XEvent_Accept(event); break;
	}
}
