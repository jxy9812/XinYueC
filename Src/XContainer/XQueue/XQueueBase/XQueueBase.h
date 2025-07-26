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
XCLASS_DEFINE_ENUM(XQueueBase, Push_Copy) = XCLASS_VTABLE_GET_SIZE(XContainerObject),
XCLASS_DEFINE_ENUM(XQueueBase, Push_Move),
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
//释放内存
#define XQueueBase_delete_base				XContainerObject_delete_base
//清空，不是释放内存
#define XQueueBase_clear_base				XContainerObject_clear_base
//检测是否为空，空为真 O(1)
#define XQueueBase_isEmpty_base				XContainerObject_isEmpty_base
//返回元素的个数 O(1)
#define XQueueBase_getSize_base				XContainerObject_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XQueueBase_getCapacity_base			XContainerObject_getCapacity_base
//交换两个同类型向量的数据
#define XQueueBase_swap_base				XContainerObject_swap_base
//返回元素类型字节大小
#define XQueueBase_getTypeSize_base			XContainerObject_getTypeSize_base
#ifdef __cplusplus
}
#endif
#endif