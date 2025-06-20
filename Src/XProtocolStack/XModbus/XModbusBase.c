#include "XModbusBase.h"
#include "XModbusFrame.h"
#include "XModbusConfig.h"
#include "XModbusProto.h"
#include "XModbusFunctionHandler.h"
#include "XModbusRegularlySendFrame.h"
#include "XCircularQueueAtomic.h"
#include "XTimerWheel.h"
#include <string.h>
static void XModbusBase_EvnetHandCb(XEventMin* event);
static bool XModbusBase_GetFuncCodeCb(XModbusBase* modbus, XVector* data, uint8_t* code);
void XModbusBase_init(XModbusBase* modbus, XIODeviceBase* io)
{
	if (modbus == NULL)
		return NULL;
	//开始初始化
	memset(((XDataFrameComm*)modbus) + 1, 0, sizeof(XModbusBase) - sizeof(XDataFrameComm));
	XDataFrameComm_init(modbus,io);
	//设置异步接收的缓冲区大小
	XCommunicatorBase_recvAsync_base(modbus, MB_RECV_BUFFER_SIZE);
	if (io != NULL)
	{
		XIODeviceBase_setReadBuffer_base(io, MB_DEVICE_RECV_BUFFER_SIZE);
		XIODeviceBase_setWriteBuffer_base(io, MB_DEVICE_SEND_BUFFER_SIZE);
	}
	XClassGetVtable(modbus) = XModbusBase_class_init();
	XEventDispatcher_setAllEventCb(((XDataFrameComm*)modbus)->m_eventDispatcher, XModbusBase_EvnetHandCb, modbus);
	XDataFrameComm_setGetFuncCodeCb(modbus, XModbusBase_GetFuncCodeCb);
	modbus->m_address = 1;
	modbus->m_mode = MB_NOT_MODE;
	//modbus->m_state = STATE_NOT_INITIALIZED;
}

void XModbusBase_setAddress(XModbusBase* modbus, uint8_t address)
{
	if (modbus == NULL)
		return;
	modbus->m_address = address;
}
static const bool recvHandleMaster_XEquality(const void* Value, const void* CompareValue)
{
	uint16_t waitAddressCode = (*((XModbusFrameDataRecvHandle**)Value))->waitAddressCode;
	uint16_t  value = *((uint16_t*)CompareValue);
	return (*((XModbusFrameDataRecvHandle**)Value))->waitAddressCode == *((uint16_t*)CompareValue);
}
void XModbusBase_setMode(XModbusBase* modbus, XModbusMode mode)
{
	if (modbus == NULL)
		return;
	modbus->m_mode = mode;
	if (XModbusBase_isMaster(modbus))
	{
		if (modbus->m_recvHandleMaster == NULL)
		{
			modbus->m_recvHandleMaster= XVector_Create(XModbusFrameDataRecvHandle*);
			modbus->m_recvHandleMaster->m_equality = recvHandleMaster_XEquality;
		}
		//if(modbus->m_regularlySendMaster==NULL)
		//{
		//	modbus->m_regularlySendMaster = XModbusRegularlySendFrameList_create();
		//	//modbus->regularlySendMaster = XListSLinked_Create(XModbusRegularlySendFrame);
		//}
	}
	else
	{//释放资源暂时还没写

	}
}

bool XModbusBase_isMaster(XModbusBase* modbus)
{
	return modbus->m_mode % 2 == 0;
}
//发送帧之前处理一些信息
static bool setSendFrame(XModbusBase* modbus, XModbusFrame* frame)
{
	if (modbus == NULL || frame == NULL)
		return false;
	if (frame->frameData == NULL || XVector_isEmpty_base(frame->frameData))
	{
		XModbusFrame_free(frame);
		return false;
	}
	//
	frame->mode = modbus->m_mode;
	//
	if (frame->recvHandle != NULL)
	{
		frame->recvHandle->waitAddressCode = XModbusFrame_getAddress(frame) << 8 | XModbusFrame_getFuncCode(frame);
		frame->recvHandle->timeout = XTimerBase_getCurrentTime() + MB_MASTER_RECV_OUT_TIME;//设置超时时间
	}
	return true;
}
XDFC_ErrorCode XModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frame)
{
	if (modbus == NULL || frame == NULL)
		return XDFC_ENOERR;
	XDFC_ErrorCode code=XDataFrameComm_sendData_base(modbus, frame->frameData);
	frame->frameData = NULL;
	XModbusFrame_free(frame);
	return code;
}
XTimerBase* XModbusBase_sendFrameRegularlyMaster(XModbusBase* modbus, XModbusFrame* frame, uint32_t time)
{
	if (modbus == NULL || frame == NULL)
		return NULL;
	XDataFrameComm_addPeriodicSendData_base(modbus,frame->frameData,time);
}
//功能码事件回调
void XModbusBase_EvnetExecuteCb(XEventMin* event)
{
	//printf("功能码事件\n");
	XEventFuncCode* ev = event;
	XModbusFrame* frame = ev->m_parent.frame;
	XDataFrameComm* comm = event->userData;
	XFuncCodeNode* node = NULL;
	if (comm->m_funcCodeMap != NULL)
	{
		node = XFuncCodeMap_value(comm->m_funcCodeMap, ev->funcCode);
	}
	if (node == NULL)
	{
		ev->m_parent.frame = NULL;
		//XVector_delete_base(frame);//释放帧数据以免内存泄露
		XModbusFrame_free(frame);
		XEvent_Accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
		return;
	}
	if (node->cb != NULL)
		node->cb(ev->funcCode, comm, frame, node->userData);
	ev->m_parent.frame = NULL;
	//XVector_delete_base(frame);//释放帧数据以免内存泄露
	XModbusFrame_free(frame);
	XEvent_Accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
}
//接收到完整帧事件
void XModbusBase_EvnetFrame_ReceivedCb(XEventMin* event)
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
	XModbusFrame* modbusFrame= XModbusFrame_newRecvHandle();//当前帧
	XModbusBase* modbus= event->userData;
	modbusFrame->mode = modbus->m_mode;
	{
		if(XModbusBase_isMaster(modbus))
			XModbusFrameRTU_parseData_reply(modbusFrame, frame,false);//主站接收到的是响应帧
		else
			XModbusFrameRTU_parseData_request(modbusFrame, frame,false);//从站接收到的是请求帧
	}
	if (modbusFrame->frameData == NULL)
	{//解析失败了
		XModbusFrame_free(modbusFrame);
		ev->frame = NULL;
		XVector_delete_base(frame);
		XEvent_Accept(ev);
		return;
	}
	XDataFrameComm* comm = event->userData;
	uint8_t funcCode;
	{
		if (comm->m_funcCodeMap == NULL || comm->m_getFuncCode == NULL || !comm->m_getFuncCode(comm, frame, &funcCode))
		{//没有功能码处理或获取失败 直接释放
			XModbusFrame_free(modbusFrame);
			ev->frame = NULL;
			//XVector_delete_base(frame);
			XEvent_Accept(ev);
			return;
		}
	}
	if (!XDataFrameComm_sendEvent(comm, XEventFuncCode_create(XDFC_EXECUTE, 0, modbusFrame, funcCode)))
	{//添加失败，队列满了
		//XVector_delete_base(frame);//释放帧数据以免内存泄露
		XModbusFrame_free(modbusFrame);
	}
	ev->frame = NULL;
	XEvent_Accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
}
void XModbusBase_EvnetHandCb(XEventMin* event)
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
	case XDFC_FRAME_RECEIVED:XModbusBase_EvnetFrame_ReceivedCb(event); break;
	case XDFC_EXECUTE:XModbusBase_EvnetExecuteCb(event); break;
		//case XDFC_FRAME_SENT:break;
	default:XEvent_Accept(event); break;
	}
}

bool XModbusBase_GetFuncCodeCb(XModbusBase* modbus, XVector* data, uint8_t* code)
{
	if (data == NULL || XVector_isEmpty_base(data) || code == NULL)
		return false;
	*code = ((uint8_t*)XContainerDataPtr(data))[1];
	return true;
}
