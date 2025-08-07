#include "XQueueBase.h"

bool XQueueBase_push_base(XQueueBase* this_queue, void* pvData)
{
	if (ISNULL(this_queue, "") || ISNULL(pvData, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXQueueBase_Push, bool (*)(XQueueBase*, void*, XCDataCreatMethod))(this_queue, pvData, XContainerDataCopyMethod(this_queue));
}

bool XQueueBase_push_move_base(XQueueBase* this_queue, void* pvData)
{
	if (ISNULL(this_queue, "") || ISNULL(pvData, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXQueueBase_Push, bool (*)(XQueueBase*, void*, XCDataCreatMethod))(this_queue, pvData, XContainerDataMoveMethod(this_queue));
}

void XQueueBase_pop_base(XQueueBase* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return;
	XClassGetVirtualFunc(this_queue, EXQueueBase_Pop, void (*)(XQueueBase*))(this_queue);
}

bool XQueueBase_receive_base(XQueueBase* this_queue, void* pvBuffer)
{
	if (ISNULL(this_queue, "") || ISNULL(pvBuffer, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXQueueBase_Receive, bool (*)(XQueueBase*, void*))(this_queue, pvBuffer);
}

void* XQueueBase_top_base(XQueueBase* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return NULL;
	return XClassGetVirtualFunc(this_queue, EXQueueBase_Top, void* (*)(XQueueBase*))(this_queue);
}

bool XQueueBase_isFull_base(XQueueBase* this_queue)
{
	if (ISNULL(this_queue, "") || ISNULL(XClassGetVtable(this_queue), ""))
		return false;
	return XClassGetVirtualFunc(this_queue, EXQueueBase_IsFull, bool (*)(XQueueBase*))(this_queue);
}
