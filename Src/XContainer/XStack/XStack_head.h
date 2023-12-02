#ifndef STACK_HEAD
#define STACK_HEAD
#include<stdbool.h>
#include"XContainerObject.h"
typedef  struct XSTACK
{
	void (*clear) (struct XStack* this_stack);//清空stack的队列，释放内存
	void(*push)(struct XStack* this_stack, void*LpValue);//压栈，增加元素 O(1)
	void (*pop)(struct XStack* this_stack);//移除栈顶元素 O(1)
	int* (*top)(struct XStack* this_stack);// 取得栈顶元素（但不删除）O(1)
	bool (*empty)(struct XStack* this_stack);// 检测栈内是否为空，空为真 O(1)
	int (*size)(struct XStack* this_stack);//返回stack内元素的个数 O(1)
	int (*capacity)(struct XStack* this_stack);//返回当前stack所能容纳的最大元素值
	void (*copy)(struct XStack* this_stackOne, const struct XStack* this_stackTwo);//将st2拷贝到st1
	void (*rcopy)(struct XStack* this_stackOne, const struct XStack* this_stackTwo);//将st2逆序拷贝到st1
	void (*swap)(struct XStack* this_stackOne, struct XStack* this_stackTwo);//交换两个栈
	//释放
	void (*free)(struct XStack* this_stack);//释放内存
	struct XContainerObject object;
	//void* _data;//指向自定义数组类型
	//int  _current;//当前元素个数
	//int _size;//元素最大个数
	//int _typeSize;//类型占用字节数
}XSTACK;
//检测是否扩容,并返回需要插入的指针
void* StacketEnlargeCapacity(XSTACK* this_stack);
#endif // !stack_head
