#ifndef LIST_HEAD_H
#define LIST_HEAD_H
//真实的结构体
static struct LIST
{
	//插入函数
	Node* (*push_front)(struct LIST* li, void* x);//头插
	Node* (*push_back)(struct LIST* li, void* x);//尾插
	void (*insert_front_p)(struct LIST*, Node* pval, ...);//list*li, const void* p, const void* x, const int n 链表中指向节点前增加元素n个,不填n默认1个(单次调用最多插入1000个，溢出均为1个)
	void (*insert_front_int)(struct LIST*, int n, ...);//(list*li, int n, const void* x, const int n) 链表中下标n的节点前增加x元素n个,不填n默认1个(单次调用最多插入1000个，溢出均为1个)
	void (*insert)(struct LIST*, Node* pval, const void* p1, const void* p2);// 链表中指向节点前插入另一个相同类型数组[p1,p2]间的数据，数组传递用指针
	//删除函数
	void (*pop_front)(void*);//头删
	void (*pop_back)(struct LIST*);//尾删
	void (*erase_p)(struct LIST*, const Node*, const Node*);//删除指定节点区间内的节点(包括其本身)，传入其指针地址，搭配find函数查找返回指针最佳，删除一个时请输入相同的指针
	void (*erase_int)(struct LIST*, const int, const int);//删除指定节点区间内的节点(包括其本身)，将其想象成数组下标访问，输入要删除的下标，删除一个时请输入相同下标
	void (*clear) (struct LIST*);//清空list数据，释放内存
	//遍历函数
	Node* (*at)(const struct LIST*, int);// 想象成数组，输入下标返回元素节点的指针
	Node* (*front)(const struct LIST*);// 返回链表头指针，指向第一个元素
	Node* (*back)(const struct LIST*);//返回链表尾指针，指向链表最后一个元素
	Node* (*find)(const struct LIST*, const void*);//查找数据，返回找到的节点指针，没有返回NULL
	//判断函数
	bool (*empty)(const struct LIST*);// 检测list内是否为空，空为真 O(1)
	//大小函数
	size_t(*size)(const struct LIST*);//返回list内元素的个数 O(1)
	//其他函数
	void (*sort)(struct LIST*, bool (*Sort)(void*, void*));//排序
	void (*swap)(struct LIST*, struct LIST*);//交换两个同类型链表的数据
	struct Node* _date;//指向链表的头节点
	size_t  _current;//当前元素个数
	size_t _type;//类型占用字节数
};
typedef struct LIST LIST;
#endif // !LIST_HEAD_H

