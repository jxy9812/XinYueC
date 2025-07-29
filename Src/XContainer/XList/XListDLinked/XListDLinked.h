#include"XDataStructConfig.h"
#if !defined(XLISTDLINKED_H)&& XListDLinked_ON
#define XLISTDLINKED_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdio.h>
#include"XListDLinked_iterator.h"
#include"XListDLinked_reverse_iterator.h"
#include"XListBase.h"
#define XLISTDLINKED_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XListBase))       //XList容器虚函数表大小
//List的一个节点
typedef struct XListDNode
{
	struct XListDNode* prev;//指向上一个
	struct XListDNode* next;//指向下一个
	void* data;//储存的数据指针
}XListDNode;
#define XListDNode_DataPtr(Node)  (&(((XListDNode*)Node)->data))
//获取链表节点中的数据
#define XListDNode_Data(Node,Type) (*((Type*)XListDNode_DataPtr(Node)))
//双向循环链表
typedef struct XListDLinked
{
	XListBase m_parent;
}XListDLinked;
//初始化类
XVtable* XListDLinked_class_init();
//创建链表
XListDLinked* XListDLinked_create(size_t TypeSize);
#define XListDLinked_Create(Type) XListDLinked_create(sizeof(Type))
//初始化 链表
void XListDLinked_init(XListDLinked* this_list, size_t typeSize);
//插入函数
//链表头部增加一个元素X
#define XListDLinked_push_front_base				XListBase_push_front_base
#define XListDLinked_Push_Front_Base				XListBase_Push_Front_Base
#define XListDLinked_push_front_move_base			XListBase_push_front_move_base
#define XListDLinked_Push_Front_Move_Base			XListBase_Push_Front_Move_Base
// 链表尾部增加一个元素X
#define XListDLinked_push_back_base					XListBase_push_back_base
#define XListDLinked_Push_Back_Base					XListBase_Push_Back_Base
#define XListDLinked_push_back_move_base			XListBase_push_back_move_base
#define XListDLinked_Push_Back_Move_Base			XListBase_Push_Back_Move_Base
//链表指定节点前插入1个数据
#define XListDLinked_insert_base					XListBase_insert_base
#define XListDLinked_insert_move_base				XListBase_insert_move_base
// 链表中指向节点前插入另一个相同类型数组的数据，需要指出数组大小n
#define XListDLinked_insert_array_base				XListBase_insert_array_base
#define XListDLinked_insert_array_move_base			XListBase_insert_array_move_base
//删除函数
//删除链表中第一个元素
#define  XListDLinked_pop_front_base				XListBase_pop_front_base
//删除链表中最后一个元素
#define  XListDLinked_pop_back_base					XListBase_pop_back_base
//删除指定节点
#define  XListDLinked_erase_base					XListBase_erase_base
//删除指定元素
#define  XListDLinked_remove_base					XListBase_remove_base
#define XListDLinked_Remove_Base					XListBase_Remove_Base
//遍历函数
//返回链表头
#define XListDLinked_front_base						XListBase_front_base
#define XListDLinked_Front_Base						XListBase_Front_Base
//返回链表尾
#define XListDLinked_back_base						XListBase_back_base
#define XListDLinked_Back_Base						XListBase_Back_Base
//查找数据，返回找到的节点，没有返回NULL
#define XListDLinked_find_base						XListBase_find_base
//排序
#define XListDLinked_sort_base						XListBase_sort
#define XListDLinked_copy_base				        XListBase_copy_base	
#define XListDLinked_move_base				        XListBase_move_base	
#define XListDLinked_deinit_base			        XListBase_deinit_base	
#define XListDLinked_delete_base			        XListBase_delete_base	
#define XListDLinked_clear_base			            XListBase_clear_base	
#define XListDLinked_isEmpty_base			        XListBase_isEmpty_base	
#define XListDLinked_size_base			        XListBase_size_base	
#define XListDLinked_capacity_base		        XListBase_capacity_base
#define XListDLinked_swap_base				        XListBase_swap_base	
#define XListDLinked_typeSize_base		        XListBase_typeSize_base
#ifdef __cplusplus
}
#endif
#endif // 