#include"XDataFrameCommunicator.h"
#include"XEvent.h"
#include"XCircularQueueAtomic.h"
#include<string.h>
void XDataFrameCommunicator_init(XDataFrameCommunicator* comm, uint16_t sendQueueCount, uint16_t recvQueueCount, uint16_t eventQueueCount)
{
	if (comm == NULL)
		return NULL;
	//开始初始化
	memset(((XCommunicatorBase*)comm) + 1, 0, sizeof(XDataFrameCommunicator) - sizeof(XCommunicatorBase));
	XCommunicatorBase_class_init(comm);
	XClassGetVtable(comm) = XDataFrameCommunicator_class_init();
	comm->m_state = XDFC_STATE_NOT_INITIALIZED;
	comm->m_eventQueue = XCircularQueueAtomic_Create(XEvent, eventQueueCount);
}