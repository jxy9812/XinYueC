#include"XHuffmanTree.h"
#if XMap_ON
#include"XPriority_Queue.h"
//优先队列大于的回调函数
static bool Less(XHfmNode** nodeOne, XHfmNode** nodeTwo)
{
	return XHfmTree_GetNodeData(*nodeOne).count < XHfmTree_GetNodeData(*nodeTwo).count;
}
//字典数据插入优先队列
#if XPriority_Queue_ON
static void installQueue(XPair** LPpair, XPriority_Queue* queue)
{
	//创建节点插入优先队列
	unsigned char ch = XPair_First(*LPpair, unsigned char);
	DictionaryValue dv = XPair_Second(*LPpair, DictionaryValue);
	XHfmNode* node = XHfmTree_creationNode( ch, dv.count,dv.code);
	XPriority_Queue_push(queue, &node);
}
#endif
//根据字典创建树
XHfmNode* XHfmTree_DictionariesToCreationTree(XMap* dictionaries)
{
#if XPriority_Queue_ON
	XPriority_Queue* queue = XPriority_Queue_New(XHfmNode*, Less);
	//原始字典生成单独的节点插入优先队列
	XMap_iterator_for_each(dictionaries, installQueue, queue);
	//生成哈夫曼树
	XHfmNode* LPparent = NULL;//父节点
	XHfmNode* LPleft = NULL;//左节点
	XHfmNode* LPright = NULL;//右节点
	while (!XPriority_Queue_empty(queue))
	{
		XHfmNode* LPNode = XPriority_Queue_Top(queue, XHfmNode*);
		XPriority_Queue_pop(queue);
		//printf("isNULL:%s data:%d count:%d\n", XBTree_GetData(LPNode, 0, XHfmNodeData).code==0 ? "true" : "false", XBTree_GetData(LPNode, 0, XHfmNodeData).ch, XBTree_GetData(LPNode, 0, XHfmNodeData).count);
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
			XPriority_Queue_push(queue, &LPparent);
			LPleft = NULL;
			LPright = NULL;
		}

	}
	XPriority_Queue_free(queue);
	return LPparent;
#else
	IS_ON_DEBUG(XPriority_Queue_ON);
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