#include"XDataFrameComm.h"
#include"XEvent.h"
#include"XCircularQueueAtomic.h"
#include<string.h>
void XDataFrameComm_init(XDataFrameComm* comm, uint16_t sendQueueCount, uint16_t recvQueueCount, uint16_t eventQueueCount)
{
	if (comm == NULL)
		return NULL;
	//开始初始化
	memset(((XCommunicatorBase*)comm) + 1, 0, sizeof(XDataFrameComm) - sizeof(XCommunicatorBase));
	XCommunicatorBase_class_init(comm);
	XClassGetVtable(comm) = XDataFrameComm_class_init();
	comm->m_state = XDFC_STATE_NOT_INITIALIZED;
	comm->m_eventDispatcher = XEventDispatcher_createDefault(eventQueueCount);
	comm->m_sendFrameQueue = XCircularQueueAtomic_Create(XVector*, sendQueueCount);
	comm->m_recvFrameQueue = XCircularQueueAtomic_Create(XVector*, recvQueueCount);
}

XDFC_ErrorCode XDataFrameComm_setCommMode_base(XDataFrameComm* comm, XDFC_CommMode mode)
{
	if (ISNULL(comm, "") || ISNULL(XClassGetVtable(comm), ""))
		return XDFC_EINVAL;
	return XClassGetVirtualFunc(comm, EXDataFrameComm_SetCommMode, XDFC_ErrorCode(*)(XDataFrameComm*, XDFC_CommMode))(comm, mode);
}
