#include"XDataFrameComm.h"
#include"XEvent.h"
#include"XCircularQueueAtomic.h"
#include"XDataFrameCommConfig.h"
#include"XIODeviceBase.h"
#include<string.h>
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
	comm->m_recvFrameQueue = XCircularQueueAtomic_Create(XVector*, XDFC_FRAME_RECV_QUEUE_COUNT);
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
