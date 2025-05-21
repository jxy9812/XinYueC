#include "XMutex.h"
#include "XMemory.h"
#include <string.h>
XMutex* XMutex_new(XMutex_Port* port)
{
	if (port == NULL)
		return NULL;
	XMutex* mutex = XMemory_malloc(sizeof(XMutex));
	if (port->create_funcPointer)
	{
		if (!(port->create_funcPointer(mutex)))
		{
			XMemory_free(mutex);
			return NULL;
		}
	}
	//拷贝队列接口
	memcpy(&(mutex->m_port), port, sizeof(XMutex));
	return mutex;
}

void XMutex_free(XMutex* mutex)
{
	if (mutex && mutex->m_port.free_funcPointer)
	{
		mutex->m_port.free_funcPointer(mutex);
	}
}
