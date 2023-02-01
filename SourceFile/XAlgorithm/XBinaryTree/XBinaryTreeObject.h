//二叉树基类
#ifndef XBINARYTREEOBJECT_H
#define XBINARYTREEOBJECT_H
#include<stdio.h>
#include<stdbool.h>
#include"XVector.h"
//二叉树遍历
enum BinaryTreeTraversing
{
	BinaryTreePreorder,//前序(根左右)
	BinaryTreeInorder,//中序(左根右)
	BinaryTreePostorder//后序(左右根)
};
//二叉树节点
typedef struct TreeNode
{
	struct TreeNode* parent;//父节点
	struct TreeNode* LeftChild;//左孩子节点
	struct TreeNode* RightChild;//右孩子节点
	void* data;//数据指针
}TreeNode;
//创建初始化一个二叉树节点
struct TreeNode* TreeNode_creation(const size_t TypeSize);
//创建初始化一个二叉树节点,并插入数据
struct TreeNode* TreeNode_creationInsertData(const void* LPData, const size_t TypeSize);
//插入数据
const bool TreeNode_insertData(struct TreeNode* this_node,const void* LPData, const size_t TypeSize);
//释放一个树节点,parentSetNull父节点指向的指针置为空
const bool TreeNode_free(struct TreeNode* this_node,const bool parentSetNull);
//二叉树遍历转数组存储
struct XVector* BinaryTreeTraversingToXVector(struct TreeNode* this_root,const enum BinaryTreeTraversing Traversing);
//释放整个树(当前节点及其所有子节点)
const size_t Tree_freeAll(struct TreeNode* this_root);
#endif // !XBINARYTREEOBJECT_H
