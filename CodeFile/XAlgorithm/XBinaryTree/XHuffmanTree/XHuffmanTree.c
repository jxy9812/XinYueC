#include"XHuffmanTree.h"
#include"XEquality.h"
#include"XLess.h"
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
	return true;
}
