#include"XQueue.h"
//虚函数表定义
XVtable* XQueueVtable = NULL;
//插入到队列的队尾
static void VXQueue_push(XQueue* this_queue, void* LpValue);
//删除queue的队头元素
static void VXQueue_pop(XQueue* this_queue);

void XQueue_class_init()
{
	void* vtable[] = {
		VXQueue_push,VXQueue_pop
	};
	XQueueVtable = XVtable_new();
	//继承的函数
	XVtable_append_vtable(XQueueVtable, XListVtable);
	//追加函数
	XVtable_append_array(XQueueVtable, vtable, sizeof(vtable) / sizeof(vtable[0]));
}

void VXQueue_push(XQueue* this_queue, void* LpValue)
{
	XList_push_back(this_queue, LpValue);
}

void VXQueue_pop(XQueue* this_queue)
{
	XList_pop_front(this_queue);
}
