#include"XHuffmanTree.h"
#if XMap_ON
#include"XPriorityQueue.h"
//优先队列大于的回调函数
static bool Less(XHfmNode** nodeOne, XHfmNode** nodeTwo)
{
	return XHfmTree_GetNodeData(*nodeOne).count < XHfmTree_GetNodeData(*nodeTwo).count;
}
//字典数据插入优先队列
#if XPriorityQueue_ON
static void installQueue(XPair** LPpair, XPriorityQueue* queue)
{
	//创建节点插入优先队列
	unsigned char ch = XPair_First(*LPpair, unsigned char);
	DictionaryValue dv = XPair_Second(*LPpair, DictionaryValue);
	XHfmNode* node = XHfmTree_creationNode( ch, dv.count,dv.code);
	XPriorityQueue_push_base(queue, &node);
}
#endif
//根据字典创建树
XHfmNode* XHfmTree_DictionariesToCreationTree(XMap* dictionaries)
{
#if XPriorityQueue_ON
	XPriorityQueue* queue = XPriorityQueue_New(XHfmNode*, Less);
	//原始字典生成单独的节点插入优先队列
	XMap_iterator_for_each(dictionaries, installQueue, queue);
	//生成哈夫曼树
	XHfmNode* LPparent = NULL;//父节点
	XHfmNode* LPleft = NULL;//左节点
	XHfmNode* LPright = NULL;//右节点
	while (!XPriorityQueue_isEmpty_base(queue))
	{
		XHfmNode* LPNode = XPriorityQueue_Top_Base(queue, XHfmNode*);
		XPriorityQueue_pop_base(queue);
		//printf("ArgIsNULL:%s data:%d count:%d\n", XBTree_GetData(LPNode, 0, XHfmNodeData).code==0 ? "true" : "false", XBTree_GetData(LPNode, 0, XHfmNodeData).ch, XBTree_GetData(LPNode, 0, XHfmNodeData).count);
		if (LPleft == NULL)
		{
			LPleft = LPNode;
		}
		else
		{
			LPright = LPNode;
			//创建空父节点，并建立关系
			LPparent = XHfmTree_creationNode( 0, XHfmTree_GetNodeData(LPleft).count + XHfmTree_GetNodeData(LPright).count,NULL);
			XBTree_SetLChild(LPparent, LPleft);
			XBTree_SetRChild(LPparent, LPright);
			XBTree_SetParent(LPleft, LPparent);
			XBTree_SetParent(LPright, LPparent);
			XPriorityQueue_push_base(queue, &LPparent);
			LPleft = NULL;
			LPright = NULL;
		}

	}
	XPriorityQueue_free_base(queue);
	return LPparent;
#else
	IS_ON_DEBUG(XPriorityQueue_ON);
	return NULL;
#endif
}
XHfmNode* XHfmTree_creationNode(unsigned char ch, size_t count, XVector* code)
{
	XBTreeNode* node = XBTree_CreationNode(XBTreeNode, 3, 1, XHfmNodeData);
	if (node == NULL)
	{
		return NULL;
	}
	XHfmNodeData* LPData = XBTree_Getdata(node, 0);
	//LPData->null = null;
	LPData->ch = ch;
	LPData->count = count;
	LPData->code = code;
	/*if (LPData->code == NULL)
	{
		XBTree_freeNode(node, false);
		return NULL;
	}*/
	return node;
}
#endif