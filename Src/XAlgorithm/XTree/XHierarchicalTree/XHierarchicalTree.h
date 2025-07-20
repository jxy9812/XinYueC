#ifndef XHIERARCHICALTREE_H
#define XHIERARCHICALTREE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XTreeObject.h"
#include"XFunctionCallback.h"
//分成树节点
typedef struct XRBTreeNode
{
	XTreeNode XBTNode;//普通树节点
	char color;		//颜色
}XRBTreeNode;
//红黑树-创建初始化一个节点
XRBTreeNode* XRBTree_create(const size_t TypeSize);
//红黑树-自动创建节点，插入数据，并自动调整高度和旋转保证平衡
XRBTreeNode* XRBTree_insert(XRBTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* pvData, const size_t TypeSize);
XRBTreeNode* XRBTree_erase(XRBTreeNode** this_root, XLess less, XEquality equality, XCompareRuleOne Rule, const void* pvData);
//查找红黑树节点
XRBTreeNode* XRBTree_findData(XRBTreeNode* this_root, XLess less, XEquality equality, XCompareRuleOne equalityRule, void* pvData);
//递归释放整颗树
#define XRBTree_delete					XTree_delete
#ifdef __cplusplus
}
#endif
#endif// !XREDBLACKTREE_H
