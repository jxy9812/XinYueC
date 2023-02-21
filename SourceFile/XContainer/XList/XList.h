#ifndef LIST_H
#define LIST_H
#include<stdbool.h>
#include<stdio.h>
#include"XList_func.h"
#include"XList_Iterator/XList_iterator.h"
#include"XList_Iterator/XList_reverse_iterator.h"
#include"XListNode.h"
typedef struct XList
{
	//插入函数
	XListNode* (*push_front)(struct XList* this_list, void* LValue);//头插
	XListNode* (*push_back)(struct XList* this_list, void*LValue);//尾插
	void (*insert_front_p)(struct XList*, XListNode* pval, ...);//list*li, const void* p, const void*LValue, const int n 链表中指向节点前增加元素n个,不填n默认1个(单次调用最多插入1000个，溢出均为1个)
	void (*insert_front_int)(struct XList*, int n, ...);//(list*li, int n, const void*LValue, const int n) 链表中下标n的节点前增加x元素n个,不填n默认1个(单次调用最多插入1000个，溢出均为1个)
	void (*insert)(struct XList*, XListNode* pval, const void* p1, const void* p2);// 链表中指向节点前插入另一个相同类型数组[p1,p2]间的数据，数组传递用指针
	//删除函数
	void (*pop_front)(void*);//头删
	void (*pop_back)(struct XList*);//尾删
	void (*erase_p)(struct XList*, const XListNode*, const XListNode*);//删除指定节点区间内的节点(包括其本身)，传入其指针地址，搭配find函数查找返回指针最佳，删除一个时请输入相同的指针
	void (*erase_int)(struct XList*, const int, const int);//删除指定节点区间内的节点(包括其本身)，将其想象成数组下标访问，输入要删除的下标，删除一个时请输入相同下标
	void (*clear) (struct XList*);//清空list数据，释放内存
	//遍历函数
	XListNode* (*at)(const struct XList*, int);// 想象成数组，输入下标返回元素节点的指针
	XListNode* (*front)(const struct XList*);// 返回链表头指针，指向第一个元素
	XListNode* (*back)(const struct XList*);//返回链表尾指针，指向链表最后一个元素
	XListNode* (*find)(const struct XList* this_list, XEquality equality,const void* findVal);//查找数据，返回找到的节点指针，没有返回NULL
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

#endif // 