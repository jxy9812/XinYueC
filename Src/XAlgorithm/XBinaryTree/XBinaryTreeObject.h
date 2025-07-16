//二叉树基类
#ifndef XBINARYTREEOBJECT_H
#define XBINARYTREEOBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTypes.h"
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"XVector.h"
//获取节点
#define XBTreeNode_GetParent(this_root) (((XBTreeNode**)(((XBTreeNode*)this_root)->nodes))[XBTreeParent])//二叉树-获取父节点(继承的子类均可以使用)
#define XBTreeNode_GetLChild(this_root) (((XBTreeNode**)(((XBTreeNode*)this_root)->nodes))[XBTreeLChild])//二叉树-获取左孩子(继承的子类均可以使用)
#define XBTreeNode_GetRChild(this_root) (((XBTreeNode**)(((XBTreeNode*)this_root)->nodes))[XBTreeRChild])//二叉树-获取右孩子(继承的子类均可以使用)
//设置节点
#define XBTreeNode_SetParent(this_root,node) (((XBTreeNode**)(((XBTreeNode*)this_root)->nodes))[XBTreeParent]=node)//二叉树-设置父节点(继承的子类均可以使用)
#define XBTreeNode_SetLChild(this_root,node) (((XBTreeNode**)(((XBTreeNode*)this_root)->nodes))[XBTreeLChild]=node)//二叉树-设置左孩子(继承的子类均可以使用)
#define XBTreeNode_SetRChild(this_root,node) (((XBTreeNode**)(((XBTreeNode*)this_root)->nodes))[XBTreeRChild]=node)//二叉树-设置右孩子(继承的子类均可以使用)
//数据
#define XBTreeNode_SetData(this_root,nSel,values) XBTreeNode_setData(this_root,nSel,&values)//二叉树-插入数据
#define XBTreeNode_GetData(this_root,nSel,Type) (*((Type*)(XBTreeNode_getData(this_root,nSel))))//二叉树-获取数据(继承的子类均可以使用)
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
	uint8_t nodeCount;//节点数量
	uint8_t valueCount;//值数量
	size_t  valueTypeSize;//值类型大小
	struct XBTreeNode** nodes;//节点数组
	void* values;//数据指针数组
}XBTreeNode;
//初始化
void XBTreeNode_init(XBTreeNode* node,const uint8_t nodeCount, const uint8_t dataCount, const size_t dataTypeSize);
XBTreeNode* XBTreeNode_create(const uint8_t nodeCount, const uint8_t dataCount, const size_t dataTypeSize);
bool XBTreeNode_setData(XBTreeNode* this_root, const uint8_t index, const void* pvData);
//获取数据
void* XBTreeNode_getData(XBTreeNode* this_root,const uint8_t index);
//设置一个节点指针
bool XBTreeNode_setNode(XBTreeNode* this_root, const uint8_t nodeType, XBTreeNode* node);
//获取节点指针
XBTreeNode* XBTreeNode_getNode(XBTreeNode* this_root, const uint8_t nodeType);
//获取节点指针引用
XBTreeNode** XBTreeNode_getNodeRef(XBTreeNode* this_root, const uint8_t nodeType);//
//获取节点在父节点指针的位置
XBTreeNode** XBTreeNode_getChildrenParentRef(XBTreeNode* this_root);//
//释放一个节点
void XBTreeNode_delete(XBTreeNode* node);
//递归释放整颗树
void XBTree_delete(XBTreeNode* this_root);

//创建初始化一个二叉树节点,并插入数据
XBTreeNode* XBTree_createInsertData(const void* pvData, const size_t nodeArrySize, const size_t TypeSize);
//二叉树遍历转数组存储
XVector* XBTree_TraversingToXVector(XBTreeNode* this_root,const enum XBTreeTraversing Traversing);
//替换孩子节点(将原孩子在父节点的指向修改为新的节点，并建立新的父子关系,旧节点的父指针指向空)
bool XBTree_ReplacementChildNode(XBTreeNode* formerChild/*旧的*/, XBTreeNode* freshChild/*新的*/);
//右旋
XBTreeNode* XBTree_SpinRR(XBTreeNode** this_root, XBTreeNode* nodes);
//左旋
XBTreeNode* XBTree_SpinLL(XBTreeNode** this_root, XBTreeNode* nodes);
#ifdef __cplusplus
}
#endif
#endif // !XBINARYTREEOBJECT_H
