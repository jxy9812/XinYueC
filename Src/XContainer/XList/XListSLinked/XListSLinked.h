#include"XDataStructConfig.h"
#if !defined(XLISTSLINKED_H)&& XListSLinked_ON
#define XLISTSLINKED_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdio.h>
#include"XListSLinked_iterator.h"
#include"XListBase.h"
#define XLISTSLINKED_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XListBase))       //XList容器虚函数表大小
//List的一个节点
typedef struct XListSNode
{
	struct XListSNode* next;//指向下一个
	void* data;//储存的数据指针
}XListSNode;
#define XListSNode_DataPtr(Node)  (&(((XListSNode*)Node)->data))
//获取链表节点中的数据
#define XListSNode_Data(Node,Type) (*((Type*)XListSNode_DataPtr(Node)))
//单链表
typedef struct XListSLinked
{
	XListBase m_parent;
	XListSNode* m_tail;            // 尾节点指针
}XListSLinked;
//初始化类
XVtable* XListSLinked_class_init();
//创建链表
XListSLinked* XListSLinked_create(size_t typeSize);
#define XListSLinked_Create(Type) XListSLinked_create(sizeof(Type))
//初始化 链表
void XListSLinked_init(XListSLinked* this_list, size_t typeSize);
//插入函数
//链表头部增加一个元素X
#define XListSLinked_push_front_base				XListBase_push_front_base
#define XListSLinked_Push_Front_Base				XListBase_Push_Front_Base
#define XListSLinked_push_front_move_base			XListBase_push_front_move_base
#define XListSLinked_Push_Front_Move_Base			XListBase_Push_Front_Move_Base
// 链表尾部增加一个元素X
#define XListSLinked_push_back_base					XListBase_push_back_base
#define XListSLinked_Push_Back_Base					XListBase_Push_Back_Base
#define XListSLinked_push_back_move_base			XListBase_push_back_move_base
#define XListSLinked_Push_Back_Move_Base			XListBase_Push_Back_Move_Base
//链表指定节点前插入1个数据
#define XListSLinked_insert_base					XListBase_insert_base
#define XListSLinked_insert_move_base				XListBase_insert_move_base
// 链表中指向节点前插入另一个相同类型数组的数据，需要指出数组大小n
#define XListSLinked_insert_array_base				XListBase_insert_array_base
#define XListSLinked_insert_array_move_base			XListBase_insert_array_move_base
//删除函数
//删除链表中第一个元素
#define XListSLinked_pop_front_base					XListBase_pop_front_base
//删除链表中最后一个元素
#define XListSLinked_pop_back_base					XListBase_pop_back_base
//删除指定节点
#define XListSLinked_erase_base						XListBase_erase_base
//删除指定元素
#define XListSLinked_remove_base					XListBase_remove_base
#define XListSLinked_Remove_Base					XListBase_Remove_Base
//遍历函数
//返回链表头
#define XListSLinked_front_base						XListBase_front_base
#define XListSLinked_Front_Base						XListBase_Front_Base
//返回链表尾
#define XListSLinked_back_base						XListBase_back_base
#define XListSLinked_Back_Base						XListBase_Back_Base
//查找数据，返回找到的节点，没有返回NULL
#define XListSLinked_find_base						XListBase_find_base
//排序
#define XListSLinked_sort_base						XListBase_sort
//释放内存
#define XListSLinked_delete_base						XListBase_delete_base
//清空List的队列，不是释放内存
#define XListSLinked_clear_base						XListBase_clear_base
//检测List内是否为空，空为真 O(1)
#define XListSLinked_isEmpty_base					XListBase_isEmpty_base
//返回List内元素的个数 O(1)
#define XListSLinked_getSize_base					XListBase_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XListSLinked_getCapacity_base				XListBase_getCapacity_base
//交换两个同类型向量的数据
#define XListSLinked_swap_base						XListBase_swap_base
//返回元素类型字节大小
#define XListSLinked_getTypeSize_base				XListBase_getTypeSize_base
#ifdef __cplusplus
}
#endif
#endif // 