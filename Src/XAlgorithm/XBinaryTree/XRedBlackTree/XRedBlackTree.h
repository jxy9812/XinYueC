#ifndef XREDBLACKTREE_H
#define XREDBLACKTREE_H
#include "XBinaryTreeObject.h"
#include"XFunctionCallback.h"
#include"XRedBlackTree_macro.h"
//红黑树-颜色
enum  XRBTreeColor
{
	XRBTreeRed , //红黑树-红色
	XRBTreeBlack  //红黑树-黑色
};
//红黑二叉树节点
typedef struct XRBTreeNode
{
	XBTreeNode XBTNode;//普通二叉树节点
	size_t color;		//颜色
}XRBTreeNode;
//红黑树-创建初始化一个节点
XRBTreeNode* XRBTree_creation(const size_t TypeSize);
//红黑树-自动创建节点，插入数据，并自动调整高度和旋转保证平衡
XRBTreeNode* XRBTree_insert(XRBTreeNode** this_root, XLess less, XCompareRuleTwo lessRule,const void* LPData, const size_t TypeSize);
XRBTreeNode* XRBTree_erase(XRBTreeNode** this_root, XLess less,XEquality equality, XCompareRuleOne Rule, const void* LPData);
#endif // !XREDBLACKTREE_H
