#include"XDataStructConfig.h"
#if !defined(XLIST_H)&& XList_ON
#define XLIST_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdio.h>
#include"XList_iterator.h"
#include"XList_reverse_iterator.h"
#include"XListNode.h"
#include"XListBase.h"
#define XLIST_VTABLE_SIZE (XLISTBASE_VTABLE_SIZE)       //XList容器虚函数表大小

typedef struct XList
{
	XListBase m_parent;
}XList;
//初始化类
XVtable* XList_class_init();
//创建链表
XList* XList_new(size_t TypeSize);
#define XList_New(Type) XList_new(sizeof(Type))
//初始化 链表
void XList_init(XList* this_list, size_t typeSize);
//插入函数
//链表头部增加一个元素X
#define XList_push_front_base				XListBase_push_front_base
#define XList_Push_Front_Base				XListBase_Push_Front_Base
// 链表尾部增加一个元素X
#define XList_push_back_base				XListBase_push_back_base
#define XList_Push_Back_Base				XListBase_Push_Back_Base
//链表指定节点前插入1个数据
#define XList_insert_base					XListBase_insert_base
// 链表中指向节点前插入另一个相同类型数组的数据，需要指出数组大小n
#define  XList_insert_array_base			XListBase_insert_array_base
//删除函数
//删除链表中第一个元素
#define  XList_pop_front_base				XListBase_pop_front_base
//删除链表中最后一个元素
#define  XList_pop_back_base				XListBase_pop_back_base
//删除指定节点
#define  XList_erase_base					XListBase_erase_base
//删除指定元素
#define  XList_remove_base					XListBase_remove_base
#define XList_Remove_Base					XListBase_Remove_Base
//遍历函数
//返回链表头
#define XList_front_base					XListBase_front_base
#define XList_Front_Base					XListBase_Front_Base
//返回链表尾
#define XList_back_base						XListBase_back_base
#define XList_Back_Base						XListBase_Back_Base
//查找数据，返回找到的节点，没有返回NULL
#define XList_find_base						XListBase_find_base
//排序
#define XList_sort_base						XListBase_sort
//释放内存
#define XList_free_base						XListBase_free_base
//清空vector的队列，不是释放内存
#define XList_clear_base					XListBase_clear_base
//检测vector内是否为空，空为真 O(1)
#define XList_isEmpty_base					XListBase_isEmpty_base
//返回vector内元素的个数 O(1)
#define XList_getSize_base					XListBase_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XList_getCapacity_base				XListBase_getCapacity_base
//交换两个同类型向量的数据
#define XList_swap_base						XListBase_swap_base
//返回元素类型字节大小
#define XList_getTypeSize_base				XListBase_getTypeSize_base
#ifdef __cplusplus
}
#endif
#endif // 