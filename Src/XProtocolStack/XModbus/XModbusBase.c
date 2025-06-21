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
void XModbusBase_setMode(XModbusBase* modbus, XModbusMode mode)
{
	if (modbus == NULL)
		return;
	modbus->m_mode = mode;
}

bool XModbusBase_isMaster(XModbusBase* modbus)
{
	return modbus->m_mode % 2 == 0;
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
		XModbusFrame_delete(frame);
		XEvent_Accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
		return;
	}
	if (node->cb != NULL)
		node->cb(ev->funcCode, comm, frame, node->userData);
	ev->m_parent.frame = NULL;
	//XVector_delete_base(frame);//释放帧数据以免内存泄露
	XModbusFrame_delete(frame);
	XEvent_Accept(ev);//事件回调函数中不能直接释放事件，接受后调度器会释放
}
//接收到完整帧事件
void XModbusBase_EvnetFrame_ReceivedCb(XEventMin* event)
{
	XEventRecvFrame* ev = event;
	XVector* frame = ev->frame;
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
	//printf("接收数据\n");
	XModbusBase* modbus = event->userData;
	XModbusFrame* modbusFrame= XModbusFrame_create(modbus->m_mode);//当前帧
	//XModbusFrame_setMode(modbusFrame, modbus->m_mode);
	if (!XModbusFrame_parseData(modbusFrame, frame))
	{//解析失败了
		XModbusFrame_delete(modbusFrame);
		ev->frame = NULL;
		XVector_delete_base(frame);
		XEvent_Accept(ev);
		return;
	}
	modbusFrame->frameData = frame;//解析成功后将帧数据转移
	XDataFrameComm* comm = event->userData;
	{
		if (comm->m_funcCodeMap == NULL || comm->m_getFuncCode == NULL || !comm->m_getFuncCode(comm, frame, comm->m_funcCode))
		{//没有功能码处理或获取失败 直接释放
			XModbusFrame_delete(modbusFrame);
			ev->frame = NULL;
			//XVector_delete_base(frame);
			XEvent_Accept(ev);
			return;
		}
	}
	if (!XDataFrameComm_sendEvent(comm, XEventFuncCode_create(XDFC_EXECUTE, 0, modbusFrame, comm->m_funcCode)))
	{//添加失败，队列满了
		//XVector_delete_base(frame);//释放帧数据以免内存泄露
		XModbusFrame_delete(modbusFrame);
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
