#include"XDataStructConfig.h"
#if !defined(XLISTBASE_H)&& XList_ON
#define XLISTBASE_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdio.h>
#include"XContainerObject.h"
#include"XFunctionCallback.h"
#define XLISTBASE_VTABLE_SIZE (XCONTAINEROBJECT_VTABLE_SIZE+13)       //XList容器虚函数表大小
//XList虚函数表枚举
enum XListBaseVtableEnum
{
	EXListBase_Push_Front = XCONTAINEROBJECT_VTABLE_SIZE,
	EXListBase_Push_Back,
	EXListBase_Insert,
	EXListBase_Insert_Array,
	EXListBase_Pop_Front,
	EXListBase_Pop_Back,
	EXListBase_Erase,
	EXListBase_Remove,
	EXListBase_Front,
	EXListBase_Back,
	EXListBase_Find,
	EXListBase_Sort
};
typedef struct XListBase
{
	XContainerObject m_parent;
	XEquality m_equality;//相等比较函数
}XListBase;
typedef struct XListBaseNode XListBaseNode;
//初始化 链表
void XListBase_init(XListBase* this_list, size_t typeSize);
//插入函数
//链表头部增加一个元素X
XListBaseNode* XListBase_push_front_base(XListBase* this_list, void* pvData);
#define XListBase_Push_Front_Base(this_list,type,value){type t=value;XListBase_push_front_base(this_list,&t);}
// 链表尾部增加一个元素X
XListBaseNode* XListBase_push_back_base(XListBase* this_list, void* pvData);
#define XListBase_Push_Back_Base(this_list,type,value){type t=value;XListBase_push_back_base(this_list,&t);}
//链表指定节点前插入1个数据
void XListBase_insert_base(XListBase* this_list, XListBaseNode* curNode, void* pvData);
// 链表中指向节点前插入另一个相同类型数组的数据，需要指出数组大小n
void  XListBase_insert_array_base(XListBase* this_list, XListBaseNode* curNode, const void* array, size_t size);
//删除函数
//删除链表中第一个元素
void  XListBase_pop_front_base(XListBase* this_list);
//删除链表中最后一个元素
void  XListBase_pop_back_base(XListBase* this_list);
//删除指定节点
void  XListBase_erase_base(XListBase* this_list, XListBaseNode* node);
//删除指定元素
void  XListBase_remove_base(XListBase* this_list, void* pvData);
#define XListBase_Remove_Base(this_list,type,value){type t=value;XListBase_remove_base(this_list,&t);}
//遍历函数
//返回链表头
void* XListBase_front_base(XListBase* this_list);
#define XListBase_Front_Base(list,Type) (*(Type*)XListBase_front_base(list))
//返回链表尾
void* XListBase_back_base(XListBase* this_list);
#define XListBase_Back_Base(list,Type) (*(Type*)XListBase_back_base(list))
//查找数据，返回找到的节点，没有返回NULL
XListBaseNode* XListBase_find_base(const  XListBase* this_list, const void* findVal);
void XListBase_sort(XListBase* this_list, XCompare compare);
//释放内存
#define XListBase_free_base					XContainerObject_free_base
//清空List的队列，不是释放内存
#define XListBase_clear_base				XContainerObject_clear_base
//检测List内是否为空，空为真 O(1)
#define XListBase_isEmpty_base				XContainerObject_isEmpty_base
//返回List内元素的个数 O(1)
#define XListBase_getSize_base				XContainerObject_getSize_base
//返回当前向量所能容纳的最大元素个数
#define XListBase_getCapacity_base			XContainerObject_getCapacity_base
//交换两个同类型向量的数据
#define XListBase_swap_base					XContainerObject_swap_base
//返回元素类型字节大小
#define XListBase_getTypeSize_base			XContainerObject_getTypeSize_base
#ifdef __cplusplus
}
#endif
#endif // 