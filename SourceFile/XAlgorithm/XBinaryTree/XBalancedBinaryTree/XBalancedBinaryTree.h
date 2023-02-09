//平衡二叉树
#ifndef XBALANCEDBINARYTREE_H
#define XBALANCEDBINARYTREE_H
#include<stdio.h>
#include<stdbool.h>
#include"XFunctionCallback.h"
#include"XBinaryTreeObject.h"
//平衡二叉树节点
typedef struct XBBTreeNode
{
	XBinaryTreeNode XBTNode;//普通二叉树节点
	size_t maxLayer;			//左右两孩子到自己中最大层数
}XBBTreeNode;

//创建初始化一个二叉树节点
XBBTreeNode* XBalancedBinaryTree_creation(const size_t TypeSize);
//插入数据，自动创建节点
XBBTreeNode* XBalancedBinaryTree_insert(struct XBBTreeNode** this_root, XLess less, const void* LPData, const size_t TypeSize);
//二叉树删除节点
void* XBalancedBinaryTree_erase(struct XBBTreeNode** this_root, XLess less, XEquality equality, const void* LPData, const size_t TypeSize);
//查找二叉树节点
struct XBBTreeNode* XBalancedBinaryTree_find(struct XBBTreeNode* this_root, XLess less, XEquality equality, const void* LPData);
//获取本身的高度(层数最大孩子高度+1(自己))
const size_t XBalancedBinaryTree_GetLayerNumberThis(const XBBTreeNode* this_root);
//获取左右两孩子中最大层数
const size_t XBalancedBinaryTree_GetLayerNumberChild(const XBBTreeNode* this_root);
//设置高度(层数最大孩子高度+1(自己))
const size_t XBalancedBinaryTree_SetLayerNumberThis(XBBTreeNode* this_root);
//设置高度从当前节点一直到根
const size_t XBalancedBinaryTree_SetLayerNumberAll(XBBTreeNode** this_root, XBBTreeNode* currentNode);
//旋转
XBBTreeNode* XBalancedBinaryTree_Spin(const XBBTreeNode** this_root);
#endif // !BALANCEDBINARYTREE_H
