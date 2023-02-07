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
	struct TreeNode* leftChild;//左孩子节点
	struct TreeNode* rightChild;//右孩子节点
	void* data;//数据指针
}TreeNode;
//创建初始化一个二叉树节点
void* XBinaryTreeObject_creationNode(const size_t NodeSize,const size_t TypeSize);
//创建初始化一个二叉树节点,并插入数据
struct TreeNode* XBinaryTreeObject_creationInsertData(const void* LPData, const size_t TypeSize);
//插入数据-不创建节点，传入节点
const bool XBinaryTreeObject_insertData(struct TreeNode* this_root,const void* LPData, const size_t TypeSize);
//释放一个树节点,parentSetNull父节点指向的指针置为空
const bool XBinaryTreeObject_freeNode(struct TreeNode* this_root,const bool parentSetNull);
//二叉树遍历转数组存储
struct XVector* XBinaryTreeObject_TraversingToXVector(struct TreeNode* this_root,const enum BinaryTreeTraversing Traversing);
//释放整个树(当前节点及其所有子节点)
const size_t XBinaryTreeObject_freeNodeAll(struct TreeNode* this_root);
//查找在孩子在父节点指针的位置
struct TreeNode** XBinaryTreeObject_findChildisParent(struct TreeNode* Parent, struct TreeNode* Child);
#endif // !XBINARYTREEOBJECT_H
