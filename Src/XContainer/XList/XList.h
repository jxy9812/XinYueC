#ifndef LIST_H
#define LIST_H
#include<stdbool.h>
#include<stdio.h>
#include"XList_Iterator/XList_iterator.h"
#include"XList_Iterator/XList_reverse_iterator.h"
#include"XListNode.h"
#include"XContainerObject.h"
//XList虚函数表
extern void* XListVtable[];
//XList虚函数表枚举
enum XListVtableEnum
{
	Push_Front= Free+1,
	Push_Back,
	Inserts,
	Insert,
	InsertArray,
	Pop_Front,
	Pop_Back,
	Erase,
	Remove,
	Clear,
	At,
	Front,
	Back,
	Find
};
typedef struct XList
{
	XContainerObject object;
	XEquality equality;//相等比较函数
	XLess less;//小于比较函数
}XList;

//插入函数
// 链表头部增加一个元素X
XListNode* XList_push_front(XList* this_list, void* LpValue);
// 链表尾部增加一个元素X
XListNode* XList_push_back(XList* this_list, void* LpValue);
void XList_inserts(XList* this_list, XListNode* curNode, void* LpValue, size_t n);
void XList_insert(XList* this_list, XListNode* curNode, void* LpValue);
// 链表中指向元素p前插入另一个相同类型数组[p1,p2]间的数据，数组传递用指针
void  XList_insertArray(XList* this_list, XListNode* curNode, const void* begin, size_t n);
//删除函数
//删除链表中第一个元素
void  XList_pop_front(XList* this_list);
//删除链表中最后一个元素
void  XList_pop_back(XList* this_list);
//删除指定节点
void  XList_erase(XList* this_list, XListNode* node);
//删除指定元素
void  XList_remove(XList* this_list, void* LpValue);
//清空list的队列，释放内存
void  XList_clear(XList* this_list);
//遍历函数
//返回元素节点的指针
XListNode* XList_at(const XList* this_list,const void* LpValue);
//返回链表头指针，指向第一个节点指针
XListNode* XList_front(struct XList* this_list);
//返回链表尾指针，指向链表最后一个节点指针
XListNode* XList_back(struct XList* this_list);
//查找数据，返回找到的指针，没有返回NULL
XListNode* XList_find(const struct XList* this_list, XEquality equality, const void* findVal);
//判断函数
//检测list内是否为空，空为真 O(1)
bool  XList_empty(const struct XList* this_list);
//大小函数
//返回list内元素的个数 O(1)
size_t   XList_size(const  struct XList* this_list);
//其他函数
//排序
void  XList_sort(struct XList* this_list, XCompare compare);
//交换两个同类型链表的数据
void  XList_swap(struct XList* this_listOne, struct XList* this_listTwo);
//释放内存
void  XList_free(struct XList* this_list);
//创建链表
XList* XList_init(int TypeSize,XEquality equality, XLess less);
#endif // 