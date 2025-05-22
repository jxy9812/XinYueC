#include "XCustomQueue.h"
#include "XMemory.h"
#include <string.h>
XCustomQueue* XCustomQueue_new(XCustomQueue_Port* port)
{
	if (port == NULL)
		return NULL;
	XCustomQueue* queue = XMemory_malloc(sizeof(XCustomQueue));
	if (port->create_funcPointer)
	{
		if (!(port->create_funcPointer(queue)))
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
