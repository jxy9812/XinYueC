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
#include"XContainerObject.h"
//XList虚函数表
extern XVtable* XListVtable;
#define XLIST_VTABLE_SIZE (XCONTAINEROBJECT_VTABLE_SIZE+13)       //XList容器虚函数表大小
//XList虚函数表枚举
enum XListVtableEnum
{
	EXList_Push_Front= XCONTAINEROBJECT_VTABLE_SIZE,
	EXList_Push_Back,
	EXList_Inserts,
	EXList_Insert,
	EXList_Insert_Array,
	EXList_Pop_Front,
	EXList_Pop_Back,
	EXList_Erase,
	EXList_Remove,
	EXList_Front,
	EXList_Back,
	EXList_Find,
	EXList_Sort
};
typedef struct XList
{
	XContainerObject m_parent;
	XEquality m_equality;//相等比较函数
}XList;
//初始化类
void XList_class_init();
//创建链表
XList* XList_new(size_t TypeSize);
#define XList_New(Type) XList_new(sizeof(Type))
//初始化 链表
void XList_init(XList* this_list, size_t typeSize);
//插入函数
//链表头部增加一个元素X
XListNode* XList_push_front_base(XList* this_list, void* LpValue);
#define XList_Push_Front_Base(this_list,type,value){type t=value;XList_push_front_base(this_list,&t);}
// 链表尾部增加一个元素X
XListNode* XList_push_back_base(XList* this_list, void* LpValue);
#define XList_Push_Back_Base(this_list,type,value){type t=value;XList_push_back_base(this_list,&t);}
//链表指定节点前插入n个数据
void XList_inserts_base(XList* this_list, XListNode* curNode, void* LpValue, size_t n);
//链表指定节点前插入1个数据
void XList_insert_base(XList* this_list, XListNode* curNode, void* LpValue);
// 链表中指向节点前插入另一个相同类型数组的数据，需要指出数组大小n
void  XList_insert_array_base(XList* this_list, XListNode* curNode, const void* begin, size_t n);
//删除函数
//删除链表中第一个元素
void  XList_pop_front_base(XList* this_list);
//删除链表中最后一个元素
void  XList_pop_back_base(XList* this_list);
//删除指定节点
void  XList_erase_base(XList* this_list, XListNode* node);
//删除指定元素
void  XList_remove_base(XList* this_list, void* LpValue);
#define XList_Remove_Base(this_list,type,value){type t=value;XList_remove_base(this_list,&t);}
//遍历函数
//返回链表头
void* XList_front_base(XList* this_list);
#define XList_Front_Base(list,Type) (*(Type*)XList_front_base(list))
//返回链表尾
void* XList_back_base(XList* this_list);
#define XList_Back_Base(list,Type) (*(Type*)XList_back_base(list))
//查找数据，返回找到的节点，没有返回NULL
XListNode* XList_find_base(const  XList* this_list,const void* findVal);
//释放内存
#define XList_free_base					XContainerObject_free_base
//清空vector的队列，不是释放内存
#define XList_clear_base				XContainerObject_clear_base
//检测vector内是否为空，空为真 O(1)
#define XList_isEmpty_base				XContainerObject_isEmpty_base
//返回vector内元素的个数 O(1)
#define XList_getSize_base				XContainerObject_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XList_getCapacity_base			XContainerObject_getCapacity_base
//交换两个同类型向量的数据
#define XList_swap_base					XContainerObject_swap_base
//返回元素类型字节大小
#define XList_getTypeSize_base			XContainerObject_getTypeSize_base
#ifdef __cplusplus
}
#endif
#endif // 