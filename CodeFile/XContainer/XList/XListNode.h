#ifndef XLISTNODE_H
#define XLISTNODE_H
//List的一个节点
typedef struct XListNode
{
	struct XListNode* prev;//指向上一个
	struct XListNode* next;//指向下一个
	void* date;//储存的数据指针
}XListNode;
#endif // !XLISTNODE_H
