#include "XModbusRTU.h"
#include "XModbusProto.h"
#include "XModbusConfig.h"
#include "XModbusFrame_RTU.h"
#include "XVector.h"
#include "XQueueBase.h"
#include "XCrc.h"
typedef struct XModbusFrame XModbusFrame;
static XModbusErrorCode VXModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frameData);
static XModbusErrorCode VXModbusBase_recvFrame(XModbusBase* modbus, XModbusFrame* frameData);

XVtable* XModbusRTU_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCOMMUNICATORBASE_VTABLE_SIZE)
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
	XVTABLE_OVERLOAD_DEFAULT(EXModbusBase_SendFrame, VXModbusBase_sendFrame);
	XVTABLE_OVERLOAD_DEFAULT(EXModbusBase_RecvFrame, VXModbusBase_recvFrame);
#if SHOWCONTAINERSIZE
	printf("XModbusRTU size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif
	return XVTABLE_DEFAULT;
}

XModbusErrorCode VXModbusBase_sendFrame(XModbusBase* modbus, XModbusFrame* frameData)
{
	if (modbus == NULL)
		return MB_EINVAL;
	XModbusErrorCode error = MB_ENOERR;
	XVector* dataVector = XVector_New(uint8_t);
	if (dataVector == NULL)
	{
		error = MB_ENORES;
		return error;
	}
	XVector_copy_base(dataVector, frameData->frameData);

	ENTER_CRITICAL_SECTION();
	XQueueBase_push_base(modbus->sendQueue, &dataVector);
	EXIT_CRITICAL_SECTION();
	return error;
}

XModbusErrorCode VXModbusBase_recvFrame(XModbusBase* modbus, XModbusFrame* frameData)
{
	if (modbus == NULL || frameData == NULL)
		return MB_EINVAL;
	bool            xFrameReceived = false;

	ENTER_CRITICAL_SECTION();

	XModbusFrameRTU_parseData_reply(frameData, modbus->m_parent.m_recvAsyncBuffer);

	//解析的帧有问题
	if (XVector_isEmpty_base(frameData->frameData))
		return MB_EIO;
	EXIT_CRITICAL_SECTION();
	return MB_ENOERR;
}
