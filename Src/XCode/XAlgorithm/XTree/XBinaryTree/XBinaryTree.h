#ifndef XBINARYTREE_H
#define XBINARYTREE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XTreeObject.h"
//定义节点类型
enum XBTreeNodeType
{
	XBTreeLChild,//二叉树-左孩子
	XBTreeRChild, //二叉树-右孩子
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
	XTreeNode m_class;//树节点
}XBTreeNode;
//计算树节点大小(不带数据)
size_t XBTreeNode_typeSize();

XBTreeNode* XBTreeNode_create(const char* pvData,const size_t typeSize);
void XBTreeNode_init(XBTreeNode* node, size_t treeNodeSize, const char* pvData, const size_t typeSize);
//二叉树遍历转数组存储

XVector* XBTree_TraversingToXVector(XTreeNode* this_root, const enum XBTreeTraversing Traversing, XVector*buffer);
//右旋
XTreeNode* XBTree_SpinRR(XTreeNode** this_root, XTreeNode* nodes);
//左旋
XTreeNode* XBTree_SpinLL(XTreeNode** this_root, XTreeNode* nodes);
//获取节点
#define XBTreeNode_GetParent(this_root) XTreeNode_GetParent(this_root)//二叉树-获取父节点(继承的子类均可以使用)
#define XBTreeNode_GetLChild(this_root) XTreeNode_GetChild(this_root,XBTreeLChild)//二叉树-获取左孩子(继承的子类均可以使用)
#define XBTreeNode_GetRChild(this_root) XTreeNode_GetChild(this_root,XBTreeRChild)//二叉树-获取右孩子(继承的子类均可以使用)
//设置节点
#define XBTreeNode_SetParent(this_root,node) XTreeNode_SetParent(this_root,node)//二叉树-设置父节点(继承的子类均可以使用)
#define XBTreeNode_SetLChild(this_root,node) XTreeNode_SetChild(this_root,XBTreeLChild,node)//二叉树-设置左孩子(继承的子类均可以使用)
#define XBTreeNode_SetRChild(this_root,node) XTreeNode_SetChild(this_root,XBTreeRChild,node)//二叉树-设置右孩子(继承的子类均可以使用)
//数据
#define XBTreeNode_GetDataPtr						XTreeNode_GetDataPtr
#define XBTreeNode_GetData							XTreeNode_GetData

#define XBTreeNode_delete							XTreeNode_delete
//递归释放整颗树
#define XBTree_delete(this_root,method,args)		XTree_delete_base(this_root,XBTreeNode_delete,method,args)
#ifdef __cplusplus
}
#endif
#endif// !XREDBLACKTREE_H
