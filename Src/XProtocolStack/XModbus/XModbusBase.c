#include "XModbusBase.h"
#include "XModbusFrame.h"
#include "XModbusConfig.h"
#include "XModbusProto.h"
#include "XModbusRegularlySendFrame.h"
#include "XCircularQueueAtomic.h"
#include "XTimerWheel.h"
#include "XSerialPortBase.h"
#include "XEquality.h"
#include <string.h>

static void XModbus_EvnetHandCb(XEventMin* event);
static void SetGetFuncCodeMode_RTU(XModbus* modbus, XModbusRecvHandMode mode);
static bool RTU_GetFuncCodeCb_CodeOnly(XModbus* modbus, XVector* data, XModbusRecvMatch* math);
static bool RTU_GetFuncCodeCb_AddressOnly(XModbus* modbus, XVector* data, XModbusRecvMatch* math);
static bool RTU_GetFuncCodeCb_AddressCode(XModbus* modbus, XVector* data, XModbusRecvMatch* math);
XModbus* XModbus_create(XIODeviceBase* io)
{
	if (io == NULL)
		return NULL;
	XModbus* comm = XMemory_malloc(sizeof(XModbus));
	XModbus_init(comm, io);
	return comm;
}
XModbus* XModbus_create_RTU_SerialPort(XSerialPortBase* serial, XTimerBase* timerT35Expired, XTimerBase* timerSendExpired)
{
	if (serial == NULL)
		return NULL;
	XModbus* modbus =XModbus_create(serial);
	if(modbus ==NULL)
		return NULL;
	((XDataFrameComm*)modbus)->m_timerSendExpired = timerSendExpired;
	((XDataFrameComm*)modbus)->m_timerRecvExpired = timerT35Expired;
	XDataFrameComm_setCommMode_base(modbus, XDFC_COMM_MODE_HALF_DUPLEX);
	XDataFrameComm_setFrameEndType_base(modbus, XDFC_FRAME_END_TIMEOUT);
	XDataFrameComm_setRecvValidCRC16_base(modbus, true);

	uint32_t timerout = 3.5 * (10 + serial->m_parity) * 1000 / serial->m_baudRate;
	if (timerout < 2)
		timerout = 2;
	XTimerBase_setTimeout_base(((XDataFrameComm*)modbus)->m_timerRecvExpired, timerout);
	XTimerBase_setTimeout_base(((XDataFrameComm*)modbus)->m_timerSendExpired, timerout * MB_MASTER_RECV_WAIT_TIME);
	return modbus;
}
void XModbus_init(XModbus* modbus, XIODeviceBase* io)
{
	if (modbus == NULL)
		return NULL;
	//开始初始化
	memset(((XDataFrameComm*)modbus) + 1, 0, sizeof(XModbus) - sizeof(XDataFrameComm));
	XDataFrameComm_init(modbus,io);
	//设置异步接收的缓冲区大小
	XCommunicatorBase_recvAsync_base(modbus, MB_RECV_BUFFER_SIZE);
	if (io != NULL)
	{
		XIODeviceBase_setReadBuffer_base(io, MB_DEVICE_RECV_BUFFER_SIZE);
		XIODeviceBase_setWriteBuffer_base(io, MB_DEVICE_SEND_BUFFER_SIZE);
	}
	XClassGetVtable(modbus) = XModbus_class_init();
	XEventDispatcher_setAllEventCb(((XDataFrameComm*)modbus)->m_eventDispatcher, XModbus_EvnetHandCb, modbus);
	XDataFrameComm_funcCodeMap_create(modbus,sizeof(XModbusRecvMatch),XEquality_uint16_t);
	modbus->m_address = 1;
	modbus->m_mode = MB_NOT_MODE;
	//modbus->m_state = STATE_NOT_INITIALIZED;
}

void XModbus_setAddress(XModbus* modbus, uint8_t address)
{
	if (modbus == NULL)
		return;
	modbus->m_address = address;
}
void XModbus_setMode(XModbus* modbus, XModbusMode mode)
{
	if (modbus == NULL)
		return;
	modbus->m_mode = mode;
	XModbus_setRecvHandMode(modbus,modbus->m_recvHandMode);
	switch (mode)
	{
	case MB_RTU_MASTER:
	case MB_RTU_SLAVE:

	default:
		break;
	}
}

bool XModbus_isMaster(XModbus* modbus)
{
	return modbus->m_mode % 2 == 0;
}
bool RTU_GetFuncCodeCb_CodeOnly(XModbus* modbus, XVector* data, XModbusRecvMatch* math)
{
	if (data == NULL || XVector_isEmpty_base(data) || math == NULL)
		return false;
	math->address = 0;
	math->code = ((uint8_t*)XContainerDataPtr(data))[1];
	return true;
}
bool RTU_GetFuncCodeCb_AddressOnly(XModbus* modbus, XVector* data, XModbusRecvMatch* math)
{
	if (data == NULL || XVector_isEmpty_base(data) || math == NULL)
		return false;
	math->code = 0;
	math->address = ((uint8_t*)XContainerDataPtr(data))[0];
	return true;
}
bool RTU_GetFuncCodeCb_AddressCode(XModbus* modbus, XVector* data, XModbusRecvMatch* math)
{
	if (data == NULL || XVector_isEmpty_base(data) || math == NULL)
		return false;
	math->addressCode = ((uint16_t*)XContainerDataPtr(data))[0];
	return true;
}
//设置RTU获取模式
void SetGetFuncCodeMode_RTU(XModbus* modbus, XModbusRecvHandMode mode)
{
	switch (modbus->m_mode)
	{
	case XMB_ModbusCodeOnly:XDataFrameComm_setGetFuncCodeCb(modbus, RTU_GetFuncCodeCb_CodeOnly); break;
	case XMB_ModbusAddressOnly:XDataFrameComm_setGetFuncCodeCb(modbus, RTU_GetFuncCodeCb_AddressOnly); break;
	case XMB_ModbusAddressCode:XDataFrameComm_setGetFuncCodeCb(modbus, RTU_GetFuncCodeCb_AddressCode); break;
	default:
		break;
	}
}

void XModbus_setRecvHandMode(XModbus* modbus, XModbusRecvHandMode mode)
{
	if (modbus == NULL)
		return;
	switch (modbus->m_mode)
	{
	case MB_RTU_MASTER:
	case MB_RTU_SLAVE:SetGetFuncCodeMode_RTU(modbus, mode); break;
	case MB_NOT_MODE:printf("运行模式还未设置\n"); break;
	default:
		break;
	}
	modbus->m_recvHandMode = mode;
}
void XModbus_addRecvHand_CodeOnly(XModbus* modbus, uint8_t modbusCode, XModbusRecvHandCb cb, void* userData)
{
	if (modbus == NULL || modbus->m_recvHandMode != XMB_ModbusCodeOnly)
		return;//与设置的类型不符合
	XModbusRecvMatch math = { 0 };
	math.code = modbusCode;
	XDataFrameComm_addFuncCode(modbus,&math,cb,userData);
}

void XModbus_addRecvHand_AddressOnly(XModbus* modbus, uint8_t modbusAddress, XModbusRecvHandCb cb, void* userData)
{
	if (modbus == NULL || modbus->m_recvHandMode != XMB_ModbusAddressOnly)
		return;//与设置的类型不符合
	XModbusRecvMatch math = { 0 };
	math.address = modbusAddress;
	XDataFrameComm_addFuncCode(modbus, &math, cb, userData);
}

void XModbus_addRecvHand_AddressCode(XModbus* modbus, uint8_t modbusAddress, uint8_t modbusCode, XModbusRecvHandCb cb, void* userData)
{
	if (modbus == NULL || modbus->m_recvHandMode != XMB_ModbusAddressCode)
		return;//与设置的类型不符合
	XModbusRecvMatch math = { 0 };
	math.address = modbusAddress;
	math.code = modbusCode;
	XDataFrameComm_addFuncCode(modbus, &math, cb, userData);
}

//功能码事件回调
void XModbus_EvnetExecuteCb(XEventMin* event)
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
void XModbus_EvnetFrame_ReceivedCb(XEventMin* event)
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
	XModbus* modbus = event->userData;
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
	/*printf("共呢个\n");*/
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
void XModbus_EvnetHandCb(XEventMin* event)
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
	case XDFC_FRAME_RECEIVED:XModbus_EvnetFrame_ReceivedCb(event); break;
	case XDFC_EXECUTE:XModbus_EvnetExecuteCb(event); break;
		//case XDFC_FRAME_SENT:break;
	default:XEvent_Accept(event); break;
	}
}

