#ifndef LIST_H
#define LIST_H
#include<stdbool.h>
#include<stdio.h>
//一个节点
typedef struct Node
{
	struct Node* prev;//指向上一个
	struct Node* next;//指向下一个
	void* date;//储存的数据指针
}Node;
typedef struct XList
{
	//插入函数
	Node* (*push_front)(struct XList* this_list, void* LValue);//头插
	Node* (*push_back)(struct XList* this_list, void*LValue);//尾插
	void (*insert_front_p)(struct XList*, Node* pval, ...);//list*li, const void* p, const void*LValue, const int n 链表中指向节点前增加元素n个,不填n默认1个(单次调用最多插入1000个，溢出均为1个)
	void (*insert_front_int)(struct XList*, int n, ...);//(list*li, int n, const void*LValue, const int n) 链表中下标n的节点前增加x元素n个,不填n默认1个(单次调用最多插入1000个，溢出均为1个)
	void (*insert)(struct XList*, Node* pval, const void* p1, const void* p2);// 链表中指向节点前插入另一个相同类型数组[p1,p2]间的数据，数组传递用指针
	//删除函数
	void (*pop_front)(void*);//头删
	void (*pop_back)(struct XList*);//尾删
	void (*erase_p)(struct XList*, const Node*, const Node*);//删除指定节点区间内的节点(包括其本身)，传入其指针地址，搭配find函数查找返回指针最佳，删除一个时请输入相同的指针
	void (*erase_int)(struct XList*, const int, const int);//删除指定节点区间内的节点(包括其本身)，将其想象成数组下标访问，输入要删除的下标，删除一个时请输入相同下标
	void (*clear) (struct XList*);//清空list数据，释放内存
	//遍历函数
	Node* (*at)(const struct XList*, int);// 想象成数组，输入下标返回元素节点的指针
	Node* (*front)(const struct XList*);// 返回链表头指针，指向第一个元素
	Node* (*back)(const struct XList*);//返回链表尾指针，指向链表最后一个元素
	Node* (*find)(const struct XList* this_list, bool (*find)(const struct Node* node, const void* val),const void* findVal);//查找数据，返回找到的节点指针，没有返回NULL
	//判断函数
	bool (*empty)(const struct XList*);// 检测list内是否为空，空为真 O(1)
	//大小函数
	size_t(*size)(const struct XList*);//返回list内元素的个数 O(1)
	//其他函数
	void (*sort)(struct XList* this_list, bool(*Sort)(const void* LPrevValue, const void* LNextValue));//排序
	void (*swap)(struct XList*, struct XList*);//交换两个同类型链表的数据
	//释放
	void (*free)(struct XList* this_list);//释放内存
}XList;
//插入函数
// 链表头部增加一个元素X
Node* List_push_front(XList* this_list, void*LValue);
// 链表尾部增加一个元素X
Node* List_push_back(XList* this_list, void*LValue);
//(list*li, const void* p, const void*LValue, const int n) 链表中指向元素p前增加x元素n个,不填n默认1个(单次调用最多插入1000个，溢出均为1个)
void  List_insert_front_p(XList* this_list, Node* pval, ...);
//(list*li, int n, const void*LValue, const int n) 链表中下标n的节点前增加x元素n个,不填n默认1个(单次调用最多插入1000个，溢出均为1个)
void List_insert_front_int(XList* this_list, int n, ...);
// 链表中指向元素p前插入另一个相同类型数组[p1,p2]间的数据，数组传递用指针
void  List_insert(XList* this_list, Node* pval, const void* p1, const void* p2);
//删除函数
//删除链表中第一个元素
void  List_pop_front(XList* this_list);
//删除链表中最后一个元素
void  List_pop_back(XList* this_list);
//删除指定元素区间内的数据(包括其本身)，传入其节点指针地址，搭配find函数查找返回指针最佳，删除一个时请输入相同的指针
void  List_erase_p(XList* this_list, const Node* p1, const Node* p2);
//删除指定元素区间内的数据(包括其本身)，将其想象成数组下标访问，输入要删除的下标，删除一个时请输入相同下标
void  List_erase_int(XList* this_list, const int left, const int right);
//清空list的队列，释放内存
void  List_clear(XList* this_list);
//遍历函数
// 想象成数组，输入下标返回元素节点的指针
Node* List_at(const XList* this_list, int i);
//返回链表头指针，指向第一个节点指针
Node* List_front(XList* this_list);
//返回链表尾指针，指向链表最后一个节点指针
Node* List_back(XList* this_list);
//查找数据，返回找到的指针，没有返回NULL
Node* List_find(const XList* this_list, bool (*find)(const struct Node* node, const void* val),const void* findVal);
//判断函数
//检测list内是否为空，空为真 O(1)
bool  List_empty(const XList* this_list);
//大小函数
//返回list内元素的个数 O(1)
size_t   List_size(const  XList* this_list);
//其他函数
//排序
void  List_sort(XList* this_list, bool(*Sort)(const void* LPrevValue, const void* LNextValue));
//交换两个同类型链表的数据
void  List_swap(XList* this_list1, XList* this_list2);
//释放内存
void  List_free(XList* this_list);
//创建链表
XList* List_init(int size);
#endif // 