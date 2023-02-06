#ifndef  XLIST_FUNC_H
#define XLIST_FUNC_H
#include<stdbool.h>
#include"XFunctionCallback.h"
#include"XListNode.h"
struct XList;
//插入函数
// 链表头部增加一个元素X
 XListNode* XList_push_front(struct XList* this_list, void* LValue);
// 链表尾部增加一个元素X
 XListNode* XList_push_back(struct XList* this_list, void* LValue);
//(list*li, const void* p, const void*LValue, const int n) 链表中指向元素p前增加x元素n个,不填n默认1个(单次调用最多插入1000个，溢出均为1个)
void  XList_insert_front_p(struct XList* this_list,XListNode* pval, ...);
//(list*li, int n, const void*LValue, const int n) 链表中下标n的节点前增加x元素n个,不填n默认1个(单次调用最多插入1000个，溢出均为1个)
void XList_insert_front_int(struct XList* this_list, int n, ...);
// 链表中指向元素p前插入另一个相同类型数组[p1,p2]间的数据，数组传递用指针
void  XList_insert(struct XList* this_list,XListNode* pval, const void* p1, const void* p2);
//删除函数
//删除链表中第一个元素
void  XList_pop_front(struct XList* this_list);
//删除链表中最后一个元素
void  XList_pop_back(struct XList* this_list);
//删除指定元素区间内的数据(包括其本身)，传入其节点指针地址，搭配find函数查找返回指针最佳，删除一个时请输入相同的指针
void  XList_erase_p(struct XList* this_list, const XListNode* p1, const XListNode* p2);
//删除指定元素区间内的数据(包括其本身)，将其想象成数组下标访问，输入要删除的下标，删除一个时请输入相同下标
void  XList_erase_int(struct XList* this_list, const int left, const int right);
//清空list的队列，释放内存
void  XList_clear(struct XList* this_list);
//遍历函数
// 想象成数组，输入下标返回元素节点的指针
XListNode* XList_at(const struct XList* this_list, int i);
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
void  XList_swap(struct XList* this_list1, struct XList* this_list2);
//释放内存
void  XList_free(struct XList* this_list);
//创建链表
struct XList* XList_init(int size);
#endif