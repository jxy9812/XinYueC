#include "XQueue.h"

XQueue* XQueue_init(size_t TypeSize)
{
	return XList_init(TypeSize);
}

void XQueue_free(XQueue* this_queue)
{
	XList_free(this_queue);
}

void XQueue_clear(XQueue* this_queue)
{
	XList_clear(this_queue);
}

void XQueue_push(XQueue* this_queue, void* LpValue)
{
	XList_push_back(this_queue,LpValue);
}

void XQueue_pop(XQueue* this_queue)
{
	XList_pop_front(this_queue);
}

void* XQueue_front(XQueue* this_queue)
{
	return XList_front(this_queue);
}

void* XQueue_back(XQueue* this_queue)
{
	return XList_back(this_queue);
}

bool XQueue_empty(XQueue* this_queue)
{
	return XList_empty(this_queue);
}

size_t XQueue_size(XQueue* this_queue)
{
	return XList_size(this_queue);
}
