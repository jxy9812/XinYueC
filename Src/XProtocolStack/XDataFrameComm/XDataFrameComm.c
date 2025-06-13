#include"XDataFrameComm.h"
#include"XEvent.h"
#include"XCircularQueueAtomic.h"
#include"XDataFrameCommConfig.h"
#include"XIODeviceBase.h"
#include"XListSLinked.h"
#include"XString.h"
#include"XEquality.h"
#include"XCrc.h"
#include<string.h>
#include<stdarg.h>
static void XDataFrameComm_EvnetHandCb(XEventMin* event);
XDataFrameComm* XDataFrameComm_create(XIODeviceBase* io)
{
	XDataFrameComm* comm = XMemory_malloc(sizeof(XDataFrameComm));
	XDataFrameComm_init(comm, io);
	return comm;
}
void XDataFrameComm_init(XDataFrameComm* comm, XIODeviceBase* io)
{
	if (comm == NULL)
		return NULL;
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
	//comm->m_recvFrameQueue = XCircularQueueAtomic_Create(XVector*, XDFC_FRAME_RECV_QUEUE_COUNT);

	XEventDispatcher_setAllEventCb(comm->m_eventDispatcher, XDataFrameComm_EvnetHandCb, comm);
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

void XDataFrameComm_setRecvFrameHead(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
{
	if (comm == NULL)
		return;
	if (data == NULL)
	{//关掉接收帧头判断
		if (comm->m_recvFrameHead!=NULL)
		{
			XVector_delete_base(comm->m_recvFrameHead);
			comm->m_recvFrameHead = NULL;
		}
	}
	else if (dataSize > 0)
	{//设置接收帧头判断
		if (comm->m_recvFrameHead == NULL)
		{
			XVector* v = XVector_Create(uint8_t);
			XVector_append_array_base(v,data,dataSize);
			comm->m_recvFrameHead = v;
		}
	}
}

void XDataFrameComm_setRecvFrameTail(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
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
			XVector* v = XVector_Create(uint8_t);
			XVector_append_array_base(v, data, dataSize);
			comm->m_recvFrameTail = v;
		}
	}
}

void XDataFrameComm_setSendFrameHead(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
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
			XVector* v = XVector_Create(uint8_t);
			XVector_append_array_base(v, data, dataSize);
			comm->m_sendFrameHead = v;
		}
	}
}

void XDataFrameComm_setSendFrameTail(XDataFrameComm* comm, const uint8_t* data, uint8_t dataSize)
{
	if (comm == NULL)
		return;
	//printf("设置发送帧尾巴\n");
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
			XVector* v = XVector_Create(uint8_t);
			XVector_append_array_base(v, data, dataSize);
			comm->m_sendFrameTail = v;
		}
	}
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
//接收验证Crc16回调
static bool XRecvValidCrc16Cb(const XVector* data)
{
	return  XCrc_get16(XContainerDataPtr(data), XContainerSize(data)) == 0;
}
void XDataFrameComm_setRecvValidCRC16(XDataFrameComm* comm, bool enableCRC16)
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
static void XSendValidCrc16Cb(XVector* data)
{
	//设置crc校验
	XVector_append_crc16(data,XCRC_BYTE_ORDER_LITTLE_ENDIAN);
	
	//uint16_t crc16 = XCrc_get16(XContainerDataPtr(data), XContainerSize(data));
	//uint8_t buffer[2];
	//XCrc_set16Data(buffer, crc16, 0);
	//XVector_append_array_base(data, buffer, 2);
}
void XDataFrameComm_setSendValidCRC16(XDataFrameComm* comm, bool enableCRC16)
{
	if (comm)
	{
		if (enableCRC16)
			comm->m_sendValidCb = XSendValidCrc16Cb;
		else
			comm->m_sendValidCb = NULL;
	}
}

XEventRecvFrame* XEventRecvFrame_create(int code, size_t timestamp,XVector* frame)
{
	XEventRecvFrame* ev = XMemory_malloc(sizeof(XEventRecvFrame));
	if (ev == NULL)
		return NULL;
	XEvent_Code(ev)=code;
	XEvent_Timestamp(ev) = timestamp;
	XEvent_UserData(ev) = NULL;
	ev->m_frame = frame;
	return ev;
}

void XDataFrameComm_EvnetHandCb(XEventMin* event)
{
	XDataFrameComm* comm = event->userData;
#if XDFC_EVENT_HANDLE_SHOW
#if XDFC_ENUM_TO_STRING
	printf("准备处理事件:%s\n", XDataFrameComm_EventType_toString(event->code));
#else
	printf("准备处理事件:%d\n", eEvent);
#endif
#endif // MB_EVENT_SHOH
	switch (event->code)
	{
		case XDFC_READY:break;
		case XDFC_FRAME_RECEIVED:
		{
			XEventRecvFrame* ev = event;
			XVector* v = ev->m_frame;
		/*	if (!XQueueBase_receive_base(comm->m_recvFrameQueue, &v))
				return;*/
#if XDFC_RECV_FRAME_16HEX_SHOW
			XString* str = XString_to16HexString(XContainerDataPtr(v), XContainerSize(v));
			if (str != NULL)
			{
				printf("\n16进制接收帧:%s\n",XString_c_str(str));
				XString_delete_base(str);
			}
#endif // XDFC_RECV_FRAME_16HEX_SHOW
#ifdef XDFC_RECV_FRAME_STR_SHOW
			if (XVector_Back_Base(v, char) != 0)
			{
				char c = 0;
				XVector_push_back_base(v, &c);
			}
			printf("\nString接收帧:%s\n", XContainerDataPtr(v));
#endif // XDFC_RECV_FRAME_STR_SHOW
			ev->m_frame = NULL;
			XVector_delete_base(v);
			break;
		}
		case XDFC_EXECUTE:break;
		case XDFC_FRAME_SENT:break;
	}
}
