//平衡二叉树
#ifndef XBALANCEDBINARYTREE_H
#define XBALANCEDBINARYTREE_H
#include<stdio.h>
#include<stdbool.h>
#include"XFunctionCallback.h"
//平衡二叉树节点
typedef struct TreeNodeBalance
{
	struct TreeNodeBalance* parent;	//父节点
	struct TreeNodeBalance* leftChild; //左孩子节点
	struct TreeNodeBalance* rightChild;//右孩子节点
	void* data;					//数据指针
	size_t maxLayer;			//左右两孩子到自己中最大层数
}TreeNodeBalance;

//创建初始化一个二叉树节点
TreeNodeBalance* XBalancedBinaryTree_creation(const size_t TypeSize);
//插入数据，自动创建节点
TreeNodeBalance* XBalancedBinaryTree_insert(struct TreeNodeBalance** this_root, XLess less, const void* LPData, const size_t TypeSize);
//二叉树删除节点
void* XBalancedBinaryTree_erase(struct TreeNodeBalance** this_root, XLess less, XEquality equality, const void* LPData, const size_t TypeSize);
//查找二叉树节点
struct TreeNodeBalance* XBalancedBinaryTree_find(struct TreeNodeBalance* this_root, XLess less, XEquality equality, const void* LPData);
//获取本身的高度(层数最大孩子高度+1(自己))
const size_t XBalancedBinaryTree_GetLayerNumberThis(const TreeNodeBalance* this_root);
//获取左右两孩子中最大层数
const size_t XBalancedBinaryTree_GetLayerNumberChild(const TreeNodeBalance* this_root);
//设置高度(层数最大孩子高度+1(自己))
const size_t XBalancedBinaryTree_SetLayerNumberThis(TreeNodeBalance* this_root);
//设置高度从当前节点一直到根
const size_t XBalancedBinaryTree_SetLayerNumberAll(TreeNodeBalance** this_root, TreeNodeBalance* currentNode);
//旋转
void XBalancedBinaryTree_Spin(const TreeNodeBalance** this_root);
#endif // !BALANCEDBINARYTREE_H
