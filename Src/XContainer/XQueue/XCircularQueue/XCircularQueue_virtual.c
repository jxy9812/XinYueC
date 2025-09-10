#include "XCircularQueue.h"
#if XCircularQueue_ON
#include"XAlgorithm.h"
#include<string.h>
#include<stdlib.h>
static bool VXCircularQueue_isEmpty(const XCircularQueue* this_queue);
static bool VXCircularQueue_isFull(const XCircularQueue* this_queue);
static void VXCircularQueue_clear(XCircularQueue* this_queue);//清空
static size_t VXCircularQueue_getSize(const XCircularQueue* this_queue);
//插入到队列的队尾
static bool VXCircularQueue_push(XCircularQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod);
//出队
static void VXCircularQueue_pop(XCircularQueue* this_queue);
// 返回队头元素
static void* VXCircularQueue_top(XCircularQueue* this_queue);
static bool VXCircularQueue_receive(XCircularQueue* this_queue, void* pvBuffer);

static void VXClass_copy(XCircularQueue* object, const XCircularQueue* src);
static void VXClass_move(XCircularQueue* object, XCircularQueue* src);
static void VXClass_deinit(XCircularQueue* this_queue);

XVtable* XCircularQueue_class_init()
{
	XVTABLE_CREAT_DEFAULT
	//虚函数表初始化
#if VTABLE_ISSTACK
	XVTABLE_STACK_INIT_DEFAULT(XCIRCULARQUEUE_VTABLE_SIZE)
#else
	XVTABLE_HEAP_INIT_DEFAULT
#endif
	//继承类
	XVTABLE_INHERIT_DEFAULT(XContainerObject_class_init());
	void* table[] = { VXCircularQueue_push,VXCircularQueue_pop,VXCircularQueue_top,VXCircularQueue_receive,VXCircularQueue_isFull };
	//追加虚函数
	XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
	//重载
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_IsEmpty,VXCircularQueue_isEmpty);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Clear,VXCircularQueue_clear);
	XVTABLE_OVERLOAD_DEFAULT(EXContainerObject_Size,VXCircularQueue_getSize);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXClass_copy);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXClass_move);
	XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXClass_deinit);
#if SHOWCONTAINERSIZE
	printf("XCircularQueue size:%d\n", XVtable_size(XVTABLE_DEFAULT));
#endif // SHOWCONTAINERSIZE
	return XVTABLE_DEFAULT;
}


bool VXCircularQueue_isEmpty(const XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return true;*/
	return ((this_queue->m_head) == (this_queue->m_tail));//头指针等于尾指针时为空
}

bool VXCircularQueue_isFull(const XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return false;*/
	return ((this_queue->m_tail + 1) % XContainerSize(this_queue) == this_queue->m_head);//尾指针下一个位置等于头指针时为满

}

void VXCircularQueue_clear(XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return;*/
	this_queue->m_head = this_queue->m_tail;
	/*while (!VXCircularQueue_isEmpty(this_queue))
	{
		VXCircularQueue_pop(this_queue);
	}*/
}

size_t VXCircularQueue_getSize(const XCircularQueue* this_queue)
{
	/*if (this_queue == NULL)
		return 0;*/
	if (this_queue->m_tail >= this_queue->m_head)
		return this_queue->m_tail - this_queue->m_head;
	else
		return this_queue->m_tail+XContainerSize(this_queue) - this_queue->m_head;;

}

bool VXCircularQueue_push(XCircularQueue* this_queue, void* pvValue, XCDataCreatMethod dataCreatMethod)
{
	if (VXCircularQueue_isFull(this_queue))
	{
		if (!this_queue->m_autoExpansion)
			return true;//不开启自动扩容
		//准备扩容
		size_t newSize = XContainerSize(this_queue) * 1.5;
		char* new_array = XMemory_malloc( XContainerTypeSize(this_queue) * newSize);
		if (new_array == NULL)
			return true;
		//暂时将队列修改为空
		//this_queue->m_tail = this_queue->m_head;
		//复制数组内容
		// 复制元素到新数组
		int i = this_queue->m_head;
		int count = 0;
		while (i != this_queue->m_tail) {
			new_array[count++] = ((char*)XContainerDataPtr(this_queue))[i];
			i = (i + 1) % XContainerSize(this_queue);
		}
		// 设置新数组的头尾指针
		this_queue->m_head = 0;
		this_queue->m_tail = count;  // 新队列的长度等于原队列的元素个数
		XMemory_free(XContainerDataPtr(this_queue));//释放原数组
		XContainerDataPtr(this_queue) = new_array;//设置新数组
		XContainerCapacity(this_queue) = newSize;//改变容量大小
		XContainerSize(this_queue) = newSize;
	}
	if (dataCreatMethod)
	{
		memset(((char*)XContainerDataPtr(this_queue)) + this_queue->m_tail * XContainerTypeSize(this_queue),0, XContainerTypeSize(this_queue));
		dataCreatMethod(((char*)XContainerDataPtr(this_queue)) + this_queue->m_tail * XContainerTypeSize(this_queue), pvValue);
	}
	else
	{
		memcpy(((char*)XContainerDataPtr(this_queue)) + this_queue->m_tail * XContainerTypeSize(this_queue), pvValue, XContainerTypeSize(this_queue));
	}
	this_queue->m_tail = (this_queue->m_tail + 1) % XContainerSize(this_queue);//指针后移取模实现环形
	return true;
}

void VXCircularQueue_pop(XCircularQueue* this_queue)
{
	if (VXCircularQueue_isEmpty(this_queue))
		return;
	if (XContainerDataDeinitMethod(this_queue) != NULL)
		XContainerDataDeinitMethod(this_queue)(VXCircularQueue_top(this_queue));
	this_queue->m_head= (this_queue->m_head + 1) % XContainerSize(this_queue);//指针后移取模实现环形
}

void* VXCircularQueue_top(XCircularQueue* this_queue)
{
	if(VXCircularQueue_isEmpty(this_queue))
		return NULL;
	return ((char*)XContainerDataPtr(this_queue)) + (this_queue->m_head * XContainerTypeSize(this_queue));
}
bool VXCircularQueue_receive(XCircularQueue* this_queue, void* pvBuffer)
{
	if (VXCircularQueue_isEmpty(this_queue))
		return false;
	void* pvTop = ((char*)XContainerDataPtr(this_queue)) + (this_queue->m_head * XContainerTypeSize(this_queue));
	memcpy(pvBuffer, pvTop, XContainerTypeSize(this_queue));
	if (XContainerDataDeinitMethod(this_queue) != NULL)
		XContainerDataDeinitMethod(this_queue)(pvTop);
	this_queue->m_head = (this_queue->m_head + 1) % XContainerSize(this_queue);//指针后移取模实现环形
	return true;
}
void VXClass_copy(XCircularQueue* object, const XCircularQueue* src)
{
	XVtableGetFunc(XVector_class_init(), EXClass_Copy, void(*)(XVector*,const XVector*))(object, src);
	object->m_autoExpansion = src->m_autoExpansion;
	object->m_head = src->m_head;
	object->m_tail = src->m_tail;
}
void VXClass_move(XCircularQueue* object, XCircularQueue* src)
{
	if (((XClass*)object)->m_vtable == NULL)
	{
		XCircularQueue_init(object,XContainerTypeSize(src), XContainerCapacity(src)-1);
	}
	else if (!XCircularQueue_isEmpty_base(object))
	{
		XCircularQueue_clear_base(object);
	}
	XSwap(object, src, sizeof(XCircularQueue));
}
void VXClass_deinit(XCircularQueue* this_queue)
{
	XVtableGetFunc(XVector_class_init(), EXClass_Deinit,void(*)(XVector*))(this_queue);
	this_queue->m_autoExpansion = false;
	this_queue->m_head = 0;
	this_queue->m_tail = 0;
}
#endif