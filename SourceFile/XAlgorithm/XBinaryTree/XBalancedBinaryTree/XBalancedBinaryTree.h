//平衡二叉树
#ifndef XBALANCEDBINARYTREE_H
#define XBALANCEDBINARYTREE_H
#include"XFunctionCallback.h"
#include"XBinaryTreeObject.h"
//平衡二叉树节点
typedef struct XBBTreeNode
{
	XBTreeNode XBTNode;//普通二叉树节点
	size_t maxLayer;			//左右两孩子到自己中最大层数
}XBBTreeNode;

//创建初始化一个二叉树节点
XBBTreeNode* XBBTree_creation(const size_t TypeSize);
//仅仅插入数据
bool XBBTree_insertAlign(XBBTreeNode** this_root, XBBTreeNode* insertNode, XLess less, const void* LPData, const size_t TypeSize);
//自动创建节点，插入数据，并自动调整高度和旋转保证平衡
XBBTreeNode* XBBTree_insert(XBBTreeNode** this_root, XLess less, const void* LPData, const size_t TypeSize);
//二叉树删除节点
void* XBBTree_erase(XBBTreeNode** this_root, XLess less, XEquality equality, const void* LPData, const size_t TypeSize);
//查找二叉树节点
XBBTreeNode* XBBTree_findData(XBBTreeNode* this_root, XLess less, XEquality equality, const void* LPData);
//获取本身的高度(层数最大孩子高度+1(自己))
const size_t XBBTree_GetLayerNumberThis(const XBBTreeNode* this_root);
//获取左右两孩子中最大层数
const size_t XBBTree_GetLayerNumberChild(const XBBTreeNode* this_root);
//设置高度(层数最大孩子高度+1(自己))
const size_t XBBTree_SetLayerNumberThis(XBBTreeNode* this_root);
//设置高度从当前节点一直到根
const size_t XBBTree_SetLayerNumberAll(XBBTreeNode** this_root, XBBTreeNode* currentNode);
//旋转
XBBTreeNode* XBBTree_Spin(const XBBTreeNode** this_root);
//右旋
XBBTreeNode* XBBTree_SpinRR(XBBTreeNode* this_root);
//左旋
XBBTreeNode* XBBTree_SpinLL(XBBTreeNode* this_root);
//右左旋
XBBTreeNode* XBBTree_SpinRL(XBBTreeNode* this_root);
//左右旋
XBBTreeNode* XBBTree_SpinLR(XBBTreeNode* this_root);
#endif // !BALANCEDBINARYTREE_H
