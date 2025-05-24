#ifndef XCUSTOMQUEUE_H
#define XCUSTOMQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
typedef struct XCustomQueue XCustomQueue;
//自定义队列接口
typedef struct XCustomQueue_Port
{
	bool (*create_funcPointer)(XCustomQueue* queue, size_t typeSize, size_t count);
	void (*free_funcPointer)(XCustomQueue* queue);
	bool (*push_funcPointer)(XCustomQueue* queue,void* pvData);
	void* (*top_funcPointer)(XCustomQueue* queue);
	bool (*receive_funcPointer)(XCustomQueue* queue, void* pvBuffer, uint32_t wait);
	bool (*pop_funcPointer)(XCustomQueue* queue);
	bool (*isEmpty_funcPointer)(XCustomQueue* queue);
	size_t(*size_funcPointer)(XCustomQueue* queue);
	void (*clear_funcPointer)(XCustomQueue* queue);
}XCustomQueue_Port;
//任务队列
typedef struct XCustomQueue
{
	void* m_queue;//队列本体
	XCustomQueue_Port  m_port;//队列接口
}XCustomQueue;
XCustomQueue* XCustomQueue_new(XCustomQueue_Port* port, size_t typeSize, size_t count);
void XCustomQueue_free(XCustomQueue* queue);
bool XCustomQueue_push(XCustomQueue* queue, void* pvData);
void* XCustomQueue_top(XCustomQueue* queue);
bool XCustomQueue_receive(XCustomQueue* queue, void* pvBuffer, uint32_t wait);
bool XCustomQueue_pop(XCustomQueue* queue);
bool XCustomQueue_isEmpty(XCustomQueue* queue);
size_t XCustomQueue_size(XCustomQueue* queue);
void XCustomQueue_clear(XCustomQueue* queue);

/*                                以下是内置接口                        */
XCustomQueue* XCustomQueue_new_XCircularQueue(size_t typeSize, size_t count);
#ifdef __cplusplus
}
#endif
#endif // !XEventQueue_H
