#ifndef XREDBLACKTREE_H
#define XREDBLACKTREE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XBinaryTreeObject.h"
#include"XFunctionCallback.h"
//辅助宏
#define XRBTree_GetColor(this_root)  ((this_root)->color)//红黑树-获取颜色
#define XRBTree_SetColor(this_root,Color)  ((this_root)->color=Color)//红黑树-设置颜色
#define XRBTree_SetBlack(this_root) ((this_root)->color=XRBTreeBlack)//红黑树-设置颜色黑色
#define XRBTree_SetRed(this_root) ((this_root)->color=XRBTreeRed)//红黑树-设置颜色红色
#define XRBTree_IsBlack(this_root) ((this_root)->color==XRBTreeBlack)//红黑树-是否是黑色
#define XRBTree_IsRed(this_root) ((this_root)->color==XRBTreeRed)//红黑树-是否是红色
//数据
#define XRBTree_GetData(this_root,Type) XBTreeNode_GetData(this_root,Type)//红黑树-获取数据
#define XRBTree_getData(this_root) XBTreeNode_getData(this_root)//红黑树-获取数据指针
//红黑树-颜色
enum  XRBTreeColor
{
	XRBTreeRed , //红黑树-红色
	XRBTreeBlack  //红黑树-黑色
};
//红黑二叉树节点
typedef struct XRBTreeNode
{
	XBTreeNode XBTNode;//普通二叉树节点
	char color;		//颜色
}XRBTreeNode;
//红黑树-创建初始化一个节点
XRBTreeNode* XRBTree_create(const size_t TypeSize);
//红黑树-自动创建节点，插入数据，并自动调整高度和旋转保证平衡
XRBTreeNode* XRBTree_insert(XRBTreeNode** this_root, XLess less, XCompareRuleTwo lessRule,const void* pvData, const size_t TypeSize);
XRBTreeNode* XRBTree_erase(XRBTreeNode** this_root, XLess less,XEquality equality, XCompareRuleOne Rule, const void* pvData);
//查找红黑树节点
XRBTreeNode* XRBTree_findData(XRBTreeNode* this_root, XLess less, XEquality equality, XCompareRuleOne equalityRule, void* pvData);
//递归释放整颗树
#define XRBTree_delete					XBTree_delete
#ifdef __cplusplus
}
#endif
#endif// !XREDBLACKTREE_H
