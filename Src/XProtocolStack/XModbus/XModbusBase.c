#include "XModbusBase.h"
#include "XModbusFrame.h"
#include "XModbusConfig.h"
#include "XModbusProto.h"
#include "XModbusFunctionHandler.h"
#include "XModbusRegularlySendFrame.h"
#include "XCircularQueueAtomic.h"
#include "XTimerWheel.h"
#include <string.h>
void XModbusBase_init(XModbusBase* modbus)
{
	if (modbus == NULL)
		return NULL;
	//开始初始化
	memset(((XCommunicatorBase*)modbus) + 1, 0, sizeof(XModbusBase) - sizeof(XCommunicatorBase));
	XCommunicatorBase_init(modbus);
	//设置异步接收的缓冲区大小
	XCommunicatorBase_recvAsync_base(modbus, MB_RECV_BUFFER_SIZE);
	XClassGetVtable(modbus) = XModbusBase_class_init();
	modbus->m_address = 1;
	modbus->m_mode = MB_NOT_MODE;
	modbus->m_state = STATE_NOT_INITIALIZED;
	modbus->m_sendQueue = XModbusFrameQueue_create(MB_FRAME_SEND_QUEUE_COUNT);
	modbus->m_recvFrameQueue = XModbusFrameQueue_create(MB_FRAME_RECV_QUEUE_COUNT);
	modbus->m_eventQueue = XCircularQueueAtomic_Create(XModbusEventType, MB_EVENT_QUEUE_COUNT);
	modbus->m_funcCodeList = XModbusFuncCodeList_create();
	

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
		if(modbus->m_regularlySendMaster==NULL)
		{
			modbus->m_regularlySendMaster = XModbusRegularlySendFrameList_create();
			//modbus->regularlySendMaster = XListSLinked_Create(XModbusRegularlySendFrame);
		}
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
XModbusErrorCode XModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frame)
{
	if (modbus == NULL || frame == NULL)
		return MB_EINVAL;
	if (setSendFrame(modbus, frame))
	{
		if (!XModbusFrameQueue_push(modbus->m_sendQueue, frame))
		{
#if MB_QUEUE_FULL_SHOW
			printf("发送帧队列溢出当前最大:%d,建议增大队列,调整:MB_FRAME_SEND_QUEUE_COUNT\n", MB_FRAME_SEND_QUEUE_COUNT);
#endif // MB_QUEUE_FULL_SHOW
			XModbusFrame_free(frame);
			return MB_ENORES;
		}
	}
	return MB_ENOERR;
}
//发送帧数据的回调函数
static void sendFrameCallback(XModbusRegularlySendFrame* regularly)
{
#if MB_SEND_FRAME_REGULARLY_COPY
	XModbusFrame* frame=XModbusFrame_copy(regularly->frame);
#else
	XModbusFrame* frame = regularly->frame;
#endif
	XModbusBase_sendFrame(regularly->modbus, frame);
#if !MB_SEND_FRAME_REGULARLY_COPY
	frame->autoDelete = false;
#endif
	//frame->recvHandle->timeout = XTimerBase_getCurrentTime() + MB_MASTER_RECV_OUT_TIME;
	//printf("发送的:%d\n", frame->recvHandle->timeout);
}
XTimerBase* XModbusBase_sendFrameRegularlyMaster(XModbusBase* modbus, XModbusFrame* frame, uint32_t time)
{
	if (modbus == NULL || frame == NULL)
		return NULL;
	if (setSendFrame(modbus, frame))
	{
		
		XTimerWheel* timer = XTimerWheel_create();
		XModbusRegularlySendFrame regularly = { 0 };
		regularly.frame = frame;
		regularly.time = time;
		//regularly.timeOut = MB_MASTER_RECV_OUT_TIME;
		regularly.modbus = modbus;
		regularly.timer = timer;
		XListBase_push_back_base(modbus->m_regularlySendMaster, &regularly);
		XTimerBase_setTimeout_base(timer,time);
		XTimerBase_setInterval_base(timer, time);
		XTimerBase_setTimerId(timer, ((XCommunicatorBase*)modbus)->m_wheel);
		XTimerBase_setUserData(timer, XListBase_back_base(modbus->m_regularlySendMaster));
		XTimerBase_setTimerCallback(timer, sendFrameCallback);
		XTimerBase_start_base(timer);
		return timer;
	}
	return NULL;
}

XModbusErrorCode XModbusBase_setFunctionHandler(XModbusBase* modbus, XModbusFunctionHandler* FunctionHandler)
{
	if (modbus == NULL)
		return MB_EINVAL;
	if (FunctionHandler == NULL)
		return MB_EINVAL;
	XModbusFuncCodeList_push(modbus->m_funcCodeList, FunctionHandler);
	return MB_ENOERR;
}

bool XModbusBase_sendEvent(XModbusBase* modbus, XModbusEventType event)
{
	if (!XQueueBase_push_base(modbus->m_eventQueue, &event))
	{
#if MB_QUEUE_FULL_SHOW
		printf("事件队列溢出当前最大:%d,建议增大队列,调整:MB_EVENT_QUEUE_COUNT\n", MB_EVENT_QUEUE_COUNT);
#endif // MB_QUEUE_FULL_SHOW
		return false;
	}
	return true;
}
