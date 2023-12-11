//二叉树基类
#ifndef XBINARYTREEOBJECT_H
#define XBINARYTREEOBJECT_H
#include<stdio.h>
#include<stdbool.h>
#include"XVector.h"
#include"XBinaryTreeObject_macro.h"
//定义节点类型
enum XBTreeNodeType
{
	XBTreeParent,//二叉树-父节点
	XBTreeLChild,//二叉树-左孩子
	XBTreeRChild //二叉树-右孩子
};
//二叉树遍历方式
enum XBTreeTraversing
{
	XBTreePreorder,//前序(根左右)
	XBTreeInorder,//中序(左根右)
	XBTreePostorder//后序(左右根)
};
//二叉树节点
typedef struct XBTreeNode
{
	XVector* nodes;//节点数组
	XVector* values;//数据指针数组
}XBTreeNode;

//创建初始化一个二叉树节点
void* XBTree_creationNode(const size_t NodeTypeSize, const size_t nodeCount,const size_t dataCount,const size_t TypeSize);
//创建初始化一个二叉树节点,并插入数据
struct XBTreeNode* XBTree_creationInsertData(const void* LPData, const size_t nodeArrySize, const size_t TypeSize);
//插入数据-不创建节点，传入节点
const bool XBTree_insertData(struct XBTreeNode* this_root,const void* LPData,const size_t index, const size_t TypeSize);
//释放一个树节点,parentSetNull父节点指向的指针置为空
const bool XBTree_freeNode(struct XBTreeNode* this_root,const bool parentSetNull);
//获取节点指针
XBTreeNode** XBTree_GetTreeNode(XBTreeNode* this_root, const size_t nSel);
//获取数据
void* XBTree_Getdata(XBTreeNode* this_root, const size_t nSel);
//二叉树遍历转数组存储
XVector* XBTree_TraversingToXVector(struct XBTreeNode* this_root,const enum XBTreeTraversing Traversing);
//释放整个树(当前节点及其所有子节点)
const size_t XBTree_freeNodeAll(struct XBTreeNode* this_root);
//查找在孩子在父节点指针的位置
struct XBTreeNode** XBTree_findChildisParent(struct XBTreeNode* Child);
//替换孩子节点(将原孩子在父节点的指向修改为新的节点，并建立新的父子关系,旧节点的父指针指向空)
bool XBTree_ReplacementChildNode(struct XBTreeNode* formerChild/*旧的*/, struct XBTreeNode* freshChild/*新的*/);
//右旋
XBTreeNode* XBTree_SpinRR(XBTreeNode** this_root, XBTreeNode* nodes);
//左旋
XBTreeNode* XBTree_SpinLL(XBTreeNode** this_root, XBTreeNode* nodes);
#endif // !XBINARYTREEOBJECT_H
