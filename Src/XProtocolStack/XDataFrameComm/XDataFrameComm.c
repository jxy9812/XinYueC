#include"XDataFrameComm.h"
#include"XByteArray.h"
#include"XEvent.h"
#include"XMapBase.h"
#include"XCircularQueueAtomic.h"
#include"XDataFrameCommConfig.h"
#include"XIODevice.h"
#include"XListSLinked.h"
#include"XString.h"
#include"XTimerBase.h"
#include"XCoreApplication.h"
#include"XPrintf.h"
#include<string.h>
#include<stdarg.h>
XDataFrameComm* XDataFrameComm_create(XIODevice* io)
{
	if (io == NULL)
		return NULL;
	XDataFrameComm* comm = XMemory_malloc(sizeof(XDataFrameComm));
	XDataFrameComm_init(comm, io);
	Set_Class_MemoryFree(comm, XFree);
	return comm;
}
void XDataFrameComm_init(XDataFrameComm* comm, XIODevice* io)
{
	if (comm == NULL||io==NULL)
		return ;
	//开始初始化
	memset(((XCommunicatorBase*)comm) + 1, 0, sizeof(XDataFrameComm) - sizeof(XCommunicatorBase));
	XCommunicatorBase_init(comm,io);
	XClassGetVtable(comm) = XDataFrameComm_class_init();

	if (io != NULL)
	{
		//XIODevice_setReadBuffer_base(io, XDFC_DEVICE_RECV_BUFFER_SIZE);
		//XIODevice_setWriteBuffer_base(io, XDFC_DEVICE_SEND_BUFFER_SIZE);
	}
	XCommunicatorBase_recvAsync_base(comm, XDFC_RECV_BUFFER_SIZE);
	comm->m_state = XDFC_STATE_NOT_INITIALIZED;
	comm->m_sendMode = XDFC_SEND_MODE_WHOLE;

	comm->m_sendFrameQueue = XCircularQueueAtomic_Create(XByteArray*, XDFC_FRAME_SEND_QUEUE_COUNT);
	comm->m_periodicSendList = XListSLinked_Create(void*);
	//comm->m_periodicSendList->m_equality = XEquality_ptr;
	XContainerSetCompare(comm->m_periodicSendList, ptr_compare);
	//XObject_addEventFilter(comm, XEVENT_ALL, XDataFrameComm_EvnetHandCb,comm);

	XDataFrameComm_setCommMode_base(comm,XDFC_COMM_MODE_FULL_DUPLEX);
	XDataFrameComm_setFrameEndType_base(comm,XDFC_FRAME_END_TIMEOUT);
	//XObject_connect1(io,XSignal(XIODevice_readyRead_signal),comm,XObject_poll_base,XConnectionType_Auto);
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

XDFC_ErrorCode XDataFrameComm_setSendMode(XDataFrameComm* comm, XDFC_SendMode mode)
{
	if (comm->m_state == XDFC_STATE_ENABLED)
		return XDFC_EILLSTATE;//协议栈启动中不支持修改
	comm->m_sendMode = mode;
	return XDFC_ENOERR;
}

XDFC_ErrorCode XDataFrameComm_sendData_base(XDataFrameComm* comm, XByteArray* data)
{
	if (ISNULL(comm, "") || ISNULL(XClassGetVtable(comm), ""))
		return XDFC_EINVAL;
	return XClassGetVirtualFunc(comm, EXDataFrameComm_SendData, XDFC_ErrorCode(*)(XDataFrameComm*, XByteArray*))(comm, data);
}

XDFC_ErrorCode XDataFrameComm_sendText(XDataFrameComm* comm, bool appendNull, const char* str)
{
	if (ISNULL(comm, "") || ISNULL(str, "")|| ISNULL(XClassGetVtable(comm), ""))
		return XDFC_EINVAL;
	XByteArray* data =XByteArray_create();
	if (data == NULL)
		return XDFC_ENORES;
	XVector_append_array_base(data, str, strlen(str) + (appendNull ? 1 : 0));
	return XDataFrameComm_sendData_base(comm, data);
}

XDFC_ErrorCode XDataFrameComm_sendTextFmt(XDataFrameComm* comm, bool appendNull, const char* format, ...)
{
	if (ISNULL(comm, "") || ISNULL(format, "") || ISNULL(XClassGetVtable(comm), ""))
		return XDFC_EINVAL;
	XByteArray* data =XByteArray_create();
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

XHandle XDataFrameComm_addPeriodicSendData_base(XDataFrameComm* comm, XByteArray* data, uint32_t time)
{
	if (ISNULL(comm, "") || ISNULL(data, "") || ISNULL(time, "") || ISNULL(XClassGetVtable(comm), ""))
		return NULL;
	return XClassGetVirtualFunc(comm, EXDataFrameComm_AddSendDataPeriodic, XHandle(*)(XDataFrameComm*, XByteArray*, uint32_t))(comm, data,time);
}

XHandle XDataFrameComm_addPeriodicSendText(XDataFrameComm* comm, bool appendNull, uint32_t time, const char* str)
{
	if (ISNULL(comm, "") || ISNULL(str, "") || ISNULL(time, "") || ISNULL(XClassGetVtable(comm), ""))
		return NULL;
	XByteArray* data =XByteArray_create();
	if (data == NULL)
		return NULL;
	XVector_append_array_base(data, str, strlen(str) + (appendNull ? 1 : 0));
	return XDataFrameComm_addPeriodicSendData_base(comm,data,time);
}

XHandle XDataFrameComm_addPeriodicSendTextFmt(XDataFrameComm* comm, bool appendNull, uint32_t time, const char* format, ...)
{
	if (ISNULL(comm, "") || ISNULL(format, "") || ISNULL(time, "") || ISNULL(XClassGetVtable(comm), ""))
		return NULL;
	XByteArray* data =XByteArray_create();
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

void XDataFrameComm_funcCodeMap_create(XDataFrameComm* comm, size_t codeSize, XCompare codeCompare)
{
	if (comm == NULL)
		return;
	if (comm->m_funcCodeMap != NULL)
		XFuncCodeMap_delete(comm->m_funcCodeMap);
	comm->m_funcCodeMap = XFuncCodeMap_create(codeSize, codeCompare);
}

void XDataFrameComm_addFuncCode(XDataFrameComm* comm, void* funcCode, XFuncCodeCb cb, void* userData)
{
	if (comm == NULL || comm->m_funcCodeMap == NULL)
		return;
	XFuncCodeMap_add(comm->m_funcCodeMap, funcCode,cb,userData);
}

void XDataFrameComm_removeFuncCode(XDataFrameComm* comm, void* funcCode)
{
	if (comm == NULL|| comm->m_funcCodeMap == NULL)
		return;
	XFuncCodeMap_remove(comm->m_funcCodeMap,funcCode);
}

void XDataFrameComm_clearFuncCode(XDataFrameComm* comm)
{
	if (comm == NULL || comm->m_funcCodeMap == NULL)
		return;
	XFuncCodeMap_clear(comm->m_funcCodeMap);
}

void XDataFrameComm_deleteFuncCode(XDataFrameComm* comm)
{
	if (comm == NULL)
		return;
	if(comm->m_funcCodeMap!=NULL)
		XFuncCodeMap_delete(comm->m_funcCodeMap);
	comm->m_funcCodeMap = NULL;
}

void XDataFrameComm_setGetFuncCodeCb(XDataFrameComm* comm, GetFuncCodeCb cb)
{
	if (comm)
		comm->m_getFuncCode = cb;
}

void XDataFrameComm_setTimerRecvExpired(XDataFrameComm* comm, XTimerBase* timer)
{
	if (comm == NULL)
	{
		if(timer)
			XTimerBase_deleteLater(timer);
		return;
	}
	if (comm->m_timerRecvExpired)
		XTimerBase_deleteLater(comm->m_timerRecvExpired);
	comm->m_timerRecvExpired = timer;
}

void XDataFrameComm_setTimerSendExpired(XDataFrameComm* comm, XTimerBase* timer)
{
	if (comm == NULL)
	{
		if (timer)
			XTimerBase_deleteLater(timer);
		return;
	}
	if (comm->m_timerSendExpired)
		XTimerBase_deleteLater(comm->m_timerSendExpired);
	comm->m_timerSendExpired = timer;

}
static void XEventRecvFrame_deinit(XEventRecvFrame* ev)
{
	if (ev->ref_count && XAtomic_fetch_sub_int32(ev->ref_count, 1) > 1)
		return;
	if (ev->frame)
	{
		XByteArray_delete_base(ev->frame);
		ev->frame = NULL;
	}
	if (ev->ref_count)
	{
		XAtomic_delete(ev->ref_count);
		ev->ref_count = NULL;
	}
}
XEventRecvFrame* XEventRecvFrame_create(XObject* object, int eventCode, size_t timestamp,XByteArray* frame, XAtomic_int32_t* ref_count)
{
	XEventRecvFrame* ev = XMemory_malloc(sizeof(XEventRecvFrame));
	if (ev == NULL)
		return NULL;
	XEvent_init(ev, eventCode);
	
	if(!ref_count)
	{
		XByteArray* v = XByteArray_create();
		if (v && frame)
			XByteArray_copy_base(v, frame);
		ev->frame = v;
	}
	else
	{
		ev->frame = frame;
	}
	ev->ref_count = ref_count;
	//ev->m_class.deinit = XEventRecvFrame_deinit;
	return ev;
}
XEventFuncCode* XEventFuncCode_create(XObject* object, int eventCode, size_t timestamp, XByteArray* frame, void* funcCode)
{
	XEventFuncCode* ev = XMemory_malloc(sizeof(XEventFuncCode));
	if (ev == NULL)
		return NULL;
	XEvent_init(ev, eventCode);
	ev->m_class.frame = frame;
	ev->funcCode = funcCode;
	return ev;
}
//接收到完整帧事件
void XDataFrameComm_EvnetFrame_ReceivedCb(XEvent* event)
{
	XEventRecvFrame* ev = event;
	XByteArray* frame = ev->frame;
	/*	if (!XQueueBase_receive_base(comm->m_recvFrameQueue, &v))
			return;*/
	//XPrintf("接收帧\n");
#if XDFC_RECV_FRAME_16HEX_SHOW
	XByteArray* str = XByteArray_to16HexUtf8(frame);
	if (str != NULL)
	{
		XPrintf("\n16进制接收帧:%s\n", XContainerDataPtr(str));
		XByteArray_delete_base(str);
	}
#endif // XDFC_RECV_FRAME_16HEX_SHOW
#ifdef XDFC_RECV_FRAME_STR_SHOW
	if (XVector_Back_Base(frame, char) != 0)
	{
		char c = 0;
		XVector_push_back_base(frame, &c);
		XPrintf("\nString接收帧:%s\n", XContainerDataPtr(frame));
		--XContainerSize(frame);
	}
	else
	{
		XPrintf("\nString接收帧:%s\n", XContainerDataPtr(frame));
	}
#endif // XDFC_RECV_FRAME_STR_SHOW
	XDataFrameComm* comm =NULL;
	void* funcCode = XFuncCodeMap_createCode(comm->m_funcCodeMap);
	//if (!(comm->m_funcCodeMap != NULL && !XFuncCodeMap_isEmpty_base(comm->m_funcCodeMap) && comm->m_getFuncCode != NULL && comm->m_getFuncCode(comm, frame, funcCode) && XDataFrameComm_postEvent(comm, XEventFuncCode_create(event->receiver, XDFC_EXECUTE, 0, frame, funcCode))))
	//{//没有功能码处理或获取失败 直接释放
	//	XVector_delete_base(frame);
	//	XFuncCodeMap_deleteCode(funcCode);
	//}
	ev->frame = NULL;//帧数据转移到功能码帧中
	XEvent_accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
}
//功能码事件回调
void XDataFrameComm_EvnetExecuteCb(XEvent* event)
{
	//XPrintf("功能码事件\n");
	XEventFuncCode* ev = event;
	XByteArray* frame = ev->m_class.frame;
	XDataFrameComm* comm = NULL;
	XFuncCodeNode* node = NULL;
	if (comm->m_funcCodeMap != NULL)
	{
		node = XFuncCodeMap_value(comm->m_funcCodeMap, ev->funcCode);
	}
	if (node==NULL)
	{
		ev->m_class.frame = NULL;
		XVector_delete_base(frame);//释放帧数据以免内存泄露
		XEvent_accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
		return;
	}
	if(node->cb!=NULL)
		node->cb(ev->funcCode,comm, frame,node->userData);
	ev->m_class.frame = NULL;
	ev->funcCode = NULL;
	XVector_delete_base(frame);//释放帧数据以免内存泄露
	XEvent_accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
}
bool XDataFrameComm_postEvent(XDataFrameComm* comm, XEvent* ev)
{
	if (comm == NULL || ev == NULL)
		return false;
	XCoreApplication_postEvent(comm, ev, XEVENT_PRIORITY_NORMAL);
	//if(!)
	{//添加失败，队列满了
		XEvent_delete_base(ev);
#if XDFC_QUEUE_FULL_SHOW
		XPrintf("事件队列溢出当前最大:%d,建议增大队列,调整:XDFC_EVENT_QUEUE_COUNT\n", XDFC_EVENT_QUEUE_COUNT);
#endif
		return false;
	}
	return true;
}
void* XDataFrameComm_frameReceived_signal(XDataFrameComm* comm, XByteArray* data, XAtomic_int32_t* ref_count)
{
	XEmitSignal(comm, XDataFrameComm_frameReceived_signal, XByteArray_create_copy(data), XByteArray_delete_base, ref_count, XEVENT_PRIORITY_NORMAL);
}
void XDataFrameComm_EvnetHandCb(XEvent* event)
{
#if XDFC_EVENT_HANDLE_SHOW
#if XDFC_ENUM_TO_STRING
	XPrintf("准备处理事件:%s\n", XDataFrameComm_EventType_toString(event->code));
#else
	XPrintf("准备处理事件:%d\n", eEvent);
#endif
#endif // MB_EVENT_SHOH
	switch (event->type)
	{
		//case XDFC_READY:break;
		case XDFC_FRAME_RECEIVED:XDataFrameComm_EvnetFrame_ReceivedCb(event); break;
		case XDFC_EXECUTE:XDataFrameComm_EvnetExecuteCb(event); break;
		//case XDFC_FRAME_SENT:break;
		default:XEvent_accept(event); break;
	}
}
