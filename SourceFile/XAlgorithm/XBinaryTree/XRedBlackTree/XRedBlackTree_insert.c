#include"XRedBlackTree.h"
#include"XBalancedBinaryTree.h"

XRBTreeNode* XRBTree_insert(XRBTreeNode** this_root, XLess less, const void* LPData, const size_t TypeSize)
{
	if (isNULL(isNULLInfo(less, "")))
		return NULL;
	if (isNULL(isNULLInfo(LPData, "")))
		return NULL;
	if (isNULL(isNULLInfo(TypeSize, "")))
		return NULL;
	XRBTreeNode* node = XRBTree_creation(TypeSize);//创建一个红黑树节点并且初始化
	if (isNULL(isNULLInfo(node, "")))
		return NULL;
	bool flag = XBBTree_insertAlign(this_root, node, less, LPData, TypeSize);//将数据插入到节点，并且链接
	if (isNULL(isNULLInfo(flag, "节点插入失败")))
		return NULL;
	return node;
}