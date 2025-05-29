#include "XCustomQueue.h"
#include "XMemory.h"
#include <string.h>
XCustomQueue* XCustomQueue_new(XCustomQueue_Port* port, size_t typeSize, size_t count)
{
	if (port == NULL)
		return NULL;
	XCustomQueue* queue = XMemory_malloc(sizeof(XCustomQueue));
	if (port->create_funcPointer)
	{
		if (!(port->create_funcPointer(queue, typeSize,count)))
		{
			XMemory_free(queue);
			return NULL;
		}
	}
	//拷贝队列接口
	memcpy(&(queue->m_port),port,sizeof(XCustomQueue_Port));
	return queue;
}

void XCustomQueue_free(XCustomQueue* queue)
{
	if (queue && queue->m_port.free_funcPointer)
	{
		queue->m_port.free_funcPointer(queue);
		XMemory_free(queue);
	}
	
}

bool XCustomQueue_push(XCustomQueue* queue, void* pvData)
{
	if (queue && queue->m_port.push_funcPointer)
		return queue->m_port.push_funcPointer(queue, pvData);
	return false;
}

void* XCustomQueue_top(XCustomQueue* queue)
{
	if (queue && queue->m_port.top_funcPointer)
		return queue->m_port.top_funcPointer(queue);
}

bool XCustomQueue_receive(XCustomQueue* queue, void* pvBuffer, uint32_t wait)
{
	if (queue && queue->m_port.receive_funcPointer)
		return queue->m_port.receive_funcPointer(queue,pvBuffer, wait);
}

bool XCustomQueue_pop(XCustomQueue* queue)
{
	if (queue && queue->m_port.pop_funcPointer)
		return queue->m_port.pop_funcPointer(queue);
	return false;
}

bool XCustomQueue_isEmpty(XCustomQueue* queue)
{
	if (queue && queue->m_port.isEmpty_funcPointer)
		return queue->m_port.isEmpty_funcPointer(queue);
	return true;
}

size_t XCustomQueue_size(XCustomQueue* queue)
{
	if (queue && queue->m_port.size_funcPointer)
		return queue->m_port.size_funcPointer(queue);
	return 0;
}

void XCustomQueue_clear(XCustomQueue* queue)
{
	if (queue && queue->m_port.clear_funcPointer)
		queue->m_port.clear_funcPointer(queue);
}
/*											XCircularQueue															*/
#include"XCircularQueueAtomic.h"
static bool XCircularQueue_createFunc(XCustomQueue* queue, size_t typeSize, size_t count)
{
	queue->m_queue = XCircularQueueAtomic_new(typeSize,count);
}
static void XCircularQueue_freeFunc(XCustomQueue* queue)
{ 
	XCircularQueue_free_base(queue->m_queue);
}
static bool XCircularQueue_pushFunc(XCustomQueue* queue, void* pvData)
{
	return XCircularQueue_push_base(queue->m_queue,pvData);
}
static void* XCircularQueue_topFunc(XCustomQueue* queue)
{
	return XCircularQueue_top_base(queue->m_queue);
}
static void XCircularQueue_popFunc(XCustomQueue* queue)
{
	XCircularQueue_pop_base(queue->m_queue);
}
static bool XCircularQueue_receiveFunc(XCustomQueue* queue, void* pvBuffer, uint32_t wait)
{
	return XCircularQueue_receive_base(queue->m_queue,pvBuffer);
	//XCircularQueue* circularQueue = (XCircularQueue*)queue->m_queue;
}
static bool XCircularQueue_isEmptyFunc(XCustomQueue* queue)
{
	return XCircularQueue_isEmpty_base(queue->m_queue);
}
static size_t XCircularQueue_sizeFunc(XCustomQueue* queue)
{
	return XCircularQueue_getSize_base(queue->m_queue);
}
static void XCircularQueue_clearFunc(XCustomQueue* queue)
{
	XCircularQueue_clear_base(queue->m_queue);
}
XCustomQueue* XCustomQueue_new_XCircularQueue(size_t typeSize, size_t count)
{
	XCustomQueue_Port port = { 0 };
	port.create_funcPointer = XCircularQueue_createFunc;
	port.free_funcPointer = XCircularQueue_freeFunc;
	port.push_funcPointer = XCircularQueue_pushFunc;
	port.top_funcPointer = XCircularQueue_topFunc;
	port.pop_funcPointer = XCircularQueue_popFunc;
	port.receive_funcPointer = XCircularQueue_receiveFunc;
	port.isEmpty_funcPointer = XCircularQueue_isEmptyFunc;
	port.size_funcPointer = XCircularQueue_sizeFunc;
	port.clear_funcPointer = XCircularQueue_clearFunc;
	return XCustomQueue_new(&port, typeSize,count);
}
