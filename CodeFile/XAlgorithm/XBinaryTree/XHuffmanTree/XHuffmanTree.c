#include"XHuffmanTree.h"
#include"XEquality.h"
#include"XLess.h"
#include"XPriority_Queue.h"
//优先队列大于的回调函数
static bool greater(XPair**pairOne, XPair** pairTwo)
{
	return XPair_Second(*pairOne, size_t) > XPair_Second(*pairTwo, size_t);
}
//字典数据插入优先队列
static void installQueue(XPair** LPpair, XPriority_Queue* queue)
{
	XPriority_Queue_push(queue, LPpair);
}
//根据字典创建树
static void XHfmTree_creationTree(XHuffmanTree* tree)
{
	XPriority_Queue* queue = XPriority_Queue_Init(XPair*, greater);
	XMap_iterator_for_each(tree->dictionaries, installQueue, queue);
	//测试队列数据是否正确
	while (!XPriority_Queue_empty(queue))
	{
		XPair** LPpair = XPriority_Queue_top(queue);
		printf("data:%d count:%d\n", XPair_First(*LPpair, char), XPair_Second(*LPpair, size_t));
		XPriority_Queue_pop(queue);
	}
}
XHfmNode* XHfmTree_creationNode()
{
	XBTreeNode* node = XBTree_CreationNode(XBTreeNode, 3, 1, XHfmNodeData);
	if (node==NULL)
	{
		return NULL;
	}
	XHfmNodeData* LPData = XBTree_Getdata(node, 0);
	LPData->ch = 0;
	LPData->count = 0;
	LPData->code = XVector_Init(char);
	if (LPData->code==NULL)
	{
		XBTree_freeNode(node,false);
		return NULL;
	}
	return node;
}

XHuffmanTree* XHfmTree_init()
{
	XHuffmanTree* tree = malloc(sizeof(XHuffmanTree));
	if (ISNULL(tree, "初始化哈夫曼树失败"))
	{
		return NULL;
	}
	tree->root = XHfmTree_creationNode();
	if (ISNULL(tree->root, "申请哈夫曼树节点失败"))
	{
		return NULL;
	}
	tree->dictionaries = XMap_Init(char,size_t,XEquality_char,XLess_char);
	if (ISNULL(tree->dictionaries, "申请哈夫曼树字典失败"))
	{
		XBTree_freeNode(tree->root,false);
		return NULL;
	}
	return tree;
}

const bool XHfmTree_readData(XHuffmanTree* tree,const char* data, const size_t size)
{
	if (ISNULL(tree, "传入的哈夫曼是NULL"))
	{
		return false;
	}
	for (size_t i = 0; i < size; i++)
	{
		XMap_At(tree->dictionaries,data[i],size_t)+=1;
	}
	XHfmTree_creationTree(tree);
	return true;
}
