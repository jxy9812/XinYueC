//二叉树基类
#ifndef XBINARYTREEOBJECT_H
#define XBINARYTREEOBJECT_H
#include<stdio.h>
#include<stdbool.h>
#include"XVector.h"
#define XBTreeParent 0//父节点
#define XBTreeLChild 1//左孩子
#define XBTreeRChild 2//右孩子
//二叉树遍历
enum XBinaryTreeTraversing
{
	XBinaryTreePreorder,//前序(根左右)
	XBinaryTreeInorder,//中序(左根右)
	XBinaryTreePostorder//后序(左右根)
};

//二叉树节点
typedef struct XBinaryTreeNode
{
	XVector* node;//节点数组
	void* data;//数据指针
}XBinaryTreeNode;
//创建初始化一个二叉树节点
void* XBinaryTreeObject_creationNode(const size_t NodeSize, const size_t nodeArrySize,const size_t TypeSize);
//创建初始化一个二叉树节点,并插入数据
struct XBinaryTreeNode* XBinaryTreeObject_creationInsertData(const void* LPData, const size_t nodeArrySize, const size_t TypeSize);
//插入数据-不创建节点，传入节点
const bool XBinaryTreeObject_insertData(struct XBinaryTreeNode* this_root,const void* LPData, const size_t TypeSize);
//释放一个树节点,parentSetNull父节点指向的指针置为空
const bool XBinaryTreeObject_freeNode(struct XBinaryTreeNode* this_root,const bool parentSetNull);
//获取节点指针
struct XBinaryTreeNode** XBinaryTreeObject_GetTreeNode(XBinaryTreeNode* this_root, const size_t nSel);
//二叉树遍历转数组存储
struct XVector* XBinaryTreeObject_TraversingToXVector(struct XBinaryTreeNode* this_root,const enum XBinaryTreeTraversing Traversing);
//释放整个树(当前节点及其所有子节点)
const size_t XBinaryTreeObject_freeNodeAll(struct XBinaryTreeNode* this_root);
//查找在孩子在父节点指针的位置
struct XBinaryTreeNode** XBinaryTreeObject_findChildisParent(struct XBinaryTreeNode* Child);
//替换孩子节点(将原孩子在父节点的指向修改为新的节点，并建立新的父子关系)
bool XBinaryTreeObject_ReplacementChildNode(struct XBinaryTreeNode* formerChild/*旧的*/, struct XBinaryTreeNode* freshChild/*新的*/);
#endif // !XBINARYTREEOBJECT_H
