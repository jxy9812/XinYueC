#include"XDataStructConfig.h"
#if !defined(XQUEUE_H)&& XQueue_ON
#define XQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XListSLinked.h"
#include"XQueueBase.h"
#define XQUEUE_VTABLE_SIZE (XQUEUEBASE_VTABLE_SIZE)       //XQueue容器虚函数表大小

typedef struct XQueue
{
	XListSLinked m_list;
}XQueue;
//初始化类
XVtable* XQueue_class_init();
//队列初始化函数
void XQueue_init(XQueue* this_queue, size_t typeSize);
//队列创建函数
#define XQueue_Create(Type,count) XQueue_create(sizeof(Type))
XQueue* XQueue_create(size_t typeSize);
//api
#define XQueue_Push_Base				XQueueBase_Push_Base
#define XQueue_push_base				XQueueBase_push_base
#define XQueue_Push_Move_Base			XQueueBase_Push_Move_Base
#define XQueue_push_move_base			XQueueBase_push_move_base
//出队
#define XQueue_pop_base					XQueueBase_pop_base
//接收数据并且出队	
#define XQueue_receive_base				XQueueBase_receive_base
// 返回队头元素
#define XQueue_Top_Base					XQueueBase_Top_Base
#define XQueue_top_base					XQueueBase_top_base
#define XQueue_isFull_base				XQueueBase_isFull_base

#define XQueue_copy_base				XQueueBase_copy_base
#define XQueue_move_base				XQueueBase_move_base
#define XQueue_deinit_base				XQueueBase_deinit_base
//释放内存
#define XQueue_delete_base				XQueueBase_delete_base
//清空，不是释放内存
#define XQueue_clear_base				XQueueBase_clear_base
//检测是否为空，空为真 O(1)
#define XQueue_isEmpty_base				XQueueBase_isEmpty_base
//返回元素的个数 O(1)
#define XQueue_getSize_base				XQueueBase_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XQueue_getCapacity_base			XQueueBase_getCapacity_base
//交换两个同类型向量的数据
#define XQueue_swap_base				XQueueBase_swap_base
//返回元素类型字节大小
#define XQueue_getTypeSize_base			XQueueBase_getTypeSize_base

#ifdef __cplusplus
}
#endif
#endif
