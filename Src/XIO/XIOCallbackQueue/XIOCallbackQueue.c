#include "XIOCallbackQueue.h"
//状态函数
typedef struct StateFunc
{
	XIODeviceBase* io;
	void (*ChangeCallback)(XIODeviceBase* io);
}StateFunc;
XIOCallbackQueue* XIOCallbackQueue_create(size_t count)
{
	return XCircularQueueAtomic_create(sizeof(StateFunc),count);
}

bool XIOCallbackQueue_push(XIOCallbackQueue* queue, XIODeviceBase* io, void(*callback)(XIODeviceBase* io))
{
	StateFunc func = { io ,callback };
	return XQueueBase_push_base(queue,&func);
}

void XIOCallbackQueue_poll(XIOCallbackQueue* queue)
{
	StateFunc func;
	while (XQueueBase_receive_base(queue, &func))
	{
		func.ChangeCallback(func.io);
	}
}
