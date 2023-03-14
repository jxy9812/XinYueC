#include"XHuffmanTree.h"

XHfmNode* XHfmTree_creationNode(const size_t TypeSize)
{
	XBTreeNode* node= XBTree_CreationNode(XBTreeNode, 3, 1, XHfmNodeData);
	XHfmNodeData* LPData=XBTree_Getdata(node, 0);
	LPData->ch = 0;
	LPData->count = 0;
	//LPData->code = XVector_init();
	return node;
}

void XHfmTree_init(XHuffmanTree* this_tree)
{
	if (isNULL(isNULLInfo(this_tree, "不能用空的哈夫曼树来初始化")))
		return;
	//this_tree->root = 
}
