#include "XModbusRTU.h"
#include "XModbusProto.h"
#include "XModbusConfig.h"
#include "XModbusFrame.h"
#include "XModbusFrame_RTU.h"
#include "XTimerWheel.h"
#include "XVector.h"
#include "XQueueBase.h"
#include "XCrc.h"
#include <string.h>
typedef struct XModbusFrame XModbusFrame;
static XModbusErrorCode VXModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frameData);
static XModbusErrorCode VXModbusBase_recvFrame(XModbusBase* modbus, XModbusFrame* frameData);

static bool VXCommunicatorBase_disconnect(XModbusRTU* modbus);
XVtable* XModbusRTU_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XMODBUSRTU_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XModbusBase_class_init());
	/*void* table[] =
	{

	};*/
	//追加虚函数
	//XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	//XVTABLE_OVERLOAD_DEFAULT(EXModbusBase_SendFrame, VXModbusBase_sendFrame);
	//XVTABLE_OVERLOAD_DEFAULT(EXModbusBase_RecvFrame, VXModbusBase_recvFrame);
 //   XVTABLE_OVERLOAD_DEFAULT(EXModbusBase_ReceiveFSM, VXModbusBase_ReceiveFSM);
 //   XVTABLE_OVERLOAD_DEFAULT(EXModbusBase_TransmitFSM, VXModbusBase_TransmitFSM);
 //   XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Connect, VXCommunicatorBase_connect);
 //   XVTABLE_OVERLOAD_DEFAULT(EXCommunicatorBase_Disconnect, VXCommunicatorBase_disconnect);
#if SHOWCONTAINERSIZE
	printf("XModbusRTU size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}
void XModbusRTU_init(XModbusRTU* modbus, XIODeviceBase* io, XTimerBase* timerT35Expired, XTimerBase* timerSendExpired)
{
    if (modbus == NULL)
        return NULL;
    //开始初始化
    memset(((XModbusBase*)modbus) + 1, 0, sizeof(XModbusRTU) - sizeof(XModbusBase));
    XModbusBase_init(modbus, io);
    XClassGetVtable(modbus) = XModbusRTU_class_init();
	((XDataFrameComm*)modbus)->m_timerSendExpired = timerSendExpired;
	((XDataFrameComm*)modbus)->m_timerRecvExpired = timerT35Expired;
	XDataFrameComm_setCommMode_base(modbus, XDFC_COMM_MODE_HALF_DUPLEX);
	XDataFrameComm_setFrameEndType_base(modbus, XDFC_FRAME_END_TIMEOUT);
	XDataFrameComm_setRecvValidCRC16_base(modbus,true);
}
XModbusErrorCode VXModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frameData)
{
	if (modbus == NULL)
		return MB_EINVAL;
	XModbusErrorCode error = MB_ENOERR;
	/*XVector* dataVector = XVector_Create(uint8_t);
	if (dataVector == NULL)
	{
		error = MB_ENORES;
		return error;
	}
	XVector_copy_base(dataVector, frameData->frameData);

	ENTER_CRITICAL_SECTION();
	XQueueBase_push_base(modbus->m_sendQueue, &dataVector);
	EXIT_CRITICAL_SECTION();*/
	return error;
}

XModbusErrorCode VXModbusBase_recvFrame(XModbusBase* modbus, XModbusFrame* frameData)
{
	if (modbus == NULL || frameData == NULL)
		return MB_EINVAL;
	bool            xFrameReceived = false;

	ENTER_CRITICAL_SECTION();
    //if(XModbusBase_isMaster(modbus))
	   // XModbusFrameRTU_parseData_reply(frameData, modbus->m_parent.m_recvAsyncBuffer);//主站接收到的是相应帧
    //else
    //    XModbusFrameRTU_parseData_request(frameData, modbus->m_parent.m_recvAsyncBuffer);//从站接收到的是请求帧
	//解析的帧有问题
	if (XVector_isEmpty_base(frameData->frameData))
		return MB_EIO;
	EXIT_CRITICAL_SECTION();
	return MB_ENOERR;
}

bool VXModbusBase_RecvValidCb(XModbusBase* modbus, const XVector* data)
{
	//XModbusFrame* frame = modbus->m_currentFrame;

	//if(XModbusBase_isMaster(modbus))
	//  XModbusFrameRTU_parseData_reply(frameData, data);//主站接收到的是相应帧
 // else
 //     XModbusFrameRTU_parseData_request(frameData, data);//从站接收到的是请求帧

	if(XCrc_get16(XContainerDataPtr(data), XContainerSize(data)) != 0)
		return false;//Crc校验没过
	return true;
}
