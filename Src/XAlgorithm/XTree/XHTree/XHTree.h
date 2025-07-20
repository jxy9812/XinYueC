#ifndef XHTREE_H
#define XHTREE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XTreeObject.h"
//定义节点类型
enum XHTreeNodeType
{
	XHTreeFirstChild,  //第一个子节点指针
	XHTreeNextSibling, //下一个兄弟节点指针
};
//分层树节点 Hierarchical 
typedef struct XHTreeNode
{
	XTreeNode parent;//普通树节点
}XHTreeNode;
//
XHTreeNode* XHTreeNode_create(const char* pvData, const size_t dataTypeSize);
void XHTreeNode_init(XHTreeNode* node, const char* pvData, const size_t dataTypeSize);
// 添加子节点
XHTreeNode* XHTreeNode_addChild(XHTreeNode* parent, const char* pvData, const size_t dataTypeSize);
//删除子节点(只找儿子不找孙子)
bool XHTreeNode_removeChild(XHTreeNode* parent, XEquality equality, XCompareRuleOne rule, const void* pvData, XTreeNodeDataDeleteMethod method, void* args);
//查找红黑树节点
XHTreeNode* XHTreeNode_findData(XHTreeNode* parent, XEquality equality, XCompareRuleOne rule, void* pvData);
// 前序遍历（根-子树）
void preOrderTraversal(XHTreeNode* node, int depth);
//释放一个节点(仅释放自己也不释放数据)
#define XHTreeNode_delete								XTreeNode_delete
//递归释放整颗树
#define XHTree_delete(this_root,method,args)			XTree_delete_base(this_root,XHTreeNode_delete,method,args)
//获取节点
#define XHTreeNode_GetParent(this_root)					XTreeNode_GetParent(this_root)//树-获取父节点(继承的子类均可以使用)
#define XHTreeNode_GetFirstChild(this_root)				XTreeNode_GetChild(this_root,XHTreeFirstChild)//树-获取第一个子节点指针(继承的子类均可以使用)
#define XHTreeNode_GetNextSibling(this_root)			XTreeNode_GetChild(this_root,XHTreeNextSibling)//树-获取下一个兄弟节点指针(继承的子类均可以使用)
//设置节点
#define XHTreeNode_SetParent(this_root,node)			XTreeNode_SetParent(this_root,node)//树-设置父节点(继承的子类均可以使用)
#define XHTreeNode_SetFirstChild(this_root,node)		XTreeNode_SetChild(this_root,XHTreeFirstChild,node)//树-设置第一个子节点指针(继承的子类均可以使用)
#define XHTreeNode_SetNextSibling(this_root,node)		XTreeNode_SetChild(this_root,XHTreeNextSibling,node)//树-设置下一个兄弟节点指针(继承的子类均可以使用)
#ifdef __cplusplus
}
#endif
#endif// !XREDBLACKTREE_H
