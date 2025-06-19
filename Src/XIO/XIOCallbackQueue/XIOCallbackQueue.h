#ifndef XIOSTATECALLBACKQUEUE_H
#define XIOSTATECALLBACKQUEUE_H
#include"XCircularQueueAtomic.h"

//创建io设备的回调队列
XIOCallbackQueue* XIOCallbackQueue_create(size_t count);
//添加回调函数到队列
bool XIOCallbackQueue_push(XIOCallbackQueue* queue, XIODeviceBase* io, void (*callback)(XIODeviceBase* io));
//处理回调函数
void XIOCallbackQueue_poll(XIOCallbackQueue* queue);
#endif // !XIOCallbackQueue
