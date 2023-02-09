#ifndef XREDBLACKTREE_H
#define XREDBLACKTREE_H
#include "XBinaryTreeObject.h"
//红黑树-颜色
enum  XRBTreeColor
{
	XRBTreeRED , //红黑树-红色
	XRBTreeBLACK  //红黑树-黑色
};
//辅助宏
#define XRBTREE_GET_COLOR(this_root)  ((this_root)->color)//红黑树-获取颜色
#define XRBTREE_SET_COLOR(this_root,Color)  ((this_root)->color=Color)//红黑树-设置颜色
#define XRBTREE_SET_BLACK(this_root) ((this_root)->color=XRBTreeBLACK)//红黑树-设置颜色黑色
#define XRBTREE_SET_RED(this_root) ((this_root)->color=XRBTreeRED)//红黑树-设置颜色红色
#define XRBTREE_IS_BLACK(this_root) ((this_root)->color==XRBTreeBLACK)//红黑树-是否是黑色
#define XRBTREE_IS_RED(this_root) ((this_root)->color==XRBTreeRED)//红黑树-是否是红色
//红黑二叉树节点
typedef struct XRBTreeNode
{
	XBTreeNode XBTNode;//普通二叉树节点
	size_t color;		//颜色
}XBBTreeNode;
//创建初始化一个红黑树节点
XRBTreeNode* XRBTree_creation(const size_t TypeSize);
#endif // !XREDBLACKTREE_H
