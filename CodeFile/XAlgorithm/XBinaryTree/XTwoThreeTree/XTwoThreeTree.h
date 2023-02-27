//23树
#ifndef XTWOTHREETREE_H
#define XTWOTHREETREE_H
#include "XBinaryTreeObject.h"
#include"XFunctionCallback.h"
//23树节点
typedef struct XTTTreeNode
{
	XBTreeNode object;
	XVector* value;//数据数组,值
}XTTTreeNode;
//创建初始化一个23树节点
XTTTreeNode* XTTTree_creationNode(const size_t nodeCount,const size_t TypeSize);
#endif // !XTWOTHREETREE_H
