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
	EXList_Clear= EXContainerObject_Clear,
	EXList_Push_Front,
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
	XContainerObject m_object;
	XEquality m_equality;//相等比较函数
}XList;
//初始化类
void XList_class_init();
//创建链表
XList* XList_new(size_t TypeSize);
#define XList_New(Type) XList_new(sizeof(Type))
//初始化 链表
void XList_init(XList* this_list, size_t typeSize);
//释放内存
void  XList_free(XList* this_list);

//插入函数
//链表头部增加一个元素X
XListNode* XList_push_front(XList* this_list, void* LpValue);
#define XList_Push_Front(this_list,type,value){type t=value;XList_push_front(this_list,&t);}
// 链表尾部增加一个元素X
XListNode* XList_push_back(XList* this_list, void* LpValue);
#define XList_Push_Back(this_list,type,value){type t=value;XList_push_back(this_list,&t);}
//链表指定节点前插入n个数据
void XList_inserts(XList* this_list, XListNode* curNode, void* LpValue, size_t n);
//链表指定节点前插入1个数据
void XList_insert(XList* this_list, XListNode* curNode, void* LpValue);
// 链表中指向节点前插入另一个相同类型数组的数据，需要指出数组大小n
void  XList_insert_array(XList* this_list, XListNode* curNode, const void* begin, size_t n);
//删除函数
//删除链表中第一个元素
void  XList_pop_front(XList* this_list);
//删除链表中最后一个元素
void  XList_pop_back(XList* this_list);
//删除指定节点
void  XList_erase(XList* this_list, XListNode* node);
//删除指定元素
void  XList_remove(XList* this_list, void* LpValue);
#define XList_Remove(this_list,type,value){type t=value;XList_remove(this_list,&t);}
//清空list的队列，释放内存
void  XList_clear(XList* this_list);
//遍历函数
//返回元素节点的指针
//返回链表头
void* XList_front(XList* this_list);
#define XList_Front(list,Type) (*(Type*)XList_front(list))
//返回链表尾
void* XList_back(XList* this_list);
#define XList_Back(list,Type) (*(Type*)XList_back(list))
//查找数据，返回找到的节点，没有返回NULL
XListNode* XList_find(const  XList* this_list,const void* findVal);
//判断函数
//检测list内是否为空，空为真 O(1)
bool  XList_isEmpty(const XList* this_list);
//大小函数
//返回list内元素的个数 O(1)
size_t  XList_size(const XList* this_list);
//其他函数
//排序
void  XList_sort(XList* this_list, XCompare compare);
//交换两个同类型链表的数据
void  XList_swap(XList* this_listOne, XList* this_listTwo);
#ifdef __cplusplus
}
#endif
#endif // 