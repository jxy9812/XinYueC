#include"XDataStructConfig.h"
#if !defined(XQUEUEBASE_H)&& XQueue_ON
#define XQUEUEBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainerObject.h"
#define XQUEUEBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XQueueBase))       //XQueueBase容器虚函数表大小
//XQueueBase虚函数表枚举
XCLASS_DEFINE_BEGING(XQueueBase)
XCLASS_DEFINE_ENUM(XQueueBase, Push) = XCLASS_VTABLE_GET_SIZE(XContainerObject),
XCLASS_DEFINE_ENUM(XQueueBase, Pop),
XCLASS_DEFINE_ENUM(XQueueBase, Top),
XCLASS_DEFINE_ENUM(XQueueBase, Receive),
XCLASS_DEFINE_ENUM(XQueueBase, IsFull),
XCLASS_DEFINE_END(XQueueBase)
//环形队列
typedef struct XQueueBase
{
	XContainerObject m_parent;
}XQueueBase;
//插入到队列的队尾
#define XQueueBase_Push_Base(this_queue,type,value){type t=value;XQueueBase_push_base(this_vector,&t);}
bool XQueueBase_push_base(XQueueBase* this_queue, void* pvData);

#define XQueueBase_Push_Move_Base(this_queue,type,value){type t=value;XQueueBase_push_move_base(this_vector,&t);}
bool XQueueBase_push_move_base(XQueueBase* this_queue, void* pvData);
//出队
void XQueueBase_pop_base(XQueueBase* this_queue);
//接收数据并且出队
bool XQueueBase_receive_base(XQueueBase* this_queue, void* pvBuffer);
// 返回队头元素
#define XQueueBase_Top_Base(this_queue,Type) (*(Type*)XQueueBase_top_base(this_queue))
void* XQueueBase_top_base(XQueueBase* this_queue);
bool XQueueBase_isFull_base(XQueueBase* this_queue);

#define XQueueBase_copy_base				XContainerObject_copy_base	
#define XQueueBase_move_base				XContainerObject_move_base	
#define XQueueBase_deinit_base				XContainerObject_deinit_base	
#define XQueueBase_delete_base				XContainerObject_delete_base	
#define XQueueBase_clear_base				XContainerObject_clear_base	
#define XQueueBase_isEmpty_base				XContainerObject_isEmpty_base	
#define XQueueBase_size_base				XContainerObject_size_base	
#define XQueueBase_capacity_base			XContainerObject_capacity_base
#define XQueueBase_swap_base				XContainerObject_swap_base	
#define XQueueBase_typeSize_base			XContainerObject_typeSize_base
#ifdef __cplusplus
}
#endif
#endif