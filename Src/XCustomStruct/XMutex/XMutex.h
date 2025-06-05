#ifndef XMUTEX_H
#define XMUTEX_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
typedef struct XMutex XMutex;
//自定义队列接口
typedef struct XMutex_Port
{
	bool (*create_funcPointer)(XMutex* mutex);
	void (*free_funcPointer)(XMutex* mutex);
	bool (*lock_funcPointer)(XMutex* mutex);
	bool (*unlock_funcPointer)(XMutex* mutex);
	bool (*lockISR_funcPointer)(XMutex* mutex);
	bool (*unlockISR_funcPointer)(XMutex* mutex);
}XMutex_Port;
//互斥锁
typedef struct XMutex
{
	void* m_mutex;//
	XMutex_Port m_port;
}XMutex;
XMutex* XMutex_create(XMutex_Port* port);
void XMutex_free(XMutex* mutex);
bool XMutex_lock(XMutex* mutex);
bool XMutex_unlock(XMutex* mutex);
bool XMutex_lockISR(XMutex* mutex);
bool XMutex_unlockISR(XMutex* mutex);
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
