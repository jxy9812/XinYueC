//23树
#ifndef XTWOTHREETREE_H
#define XTWOTHREETREE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XTreeObject.h"
#include"XCompare.h"
//23树-获取父节点
#define XTTTree_GetParent(this_root)			(*XTreeNode_getNodeRef(this_root, XTreeParent))
//23树-获取节点,0是指向父节点
#define XTTTree_GetNode(this_root,nSel，node)	(*XTTTree_Node(this_root,nSel))
//23树-设置父节点
#define XTTTree_SetParent(this_root,nodes)		(*XTreeNode_getNodeRef(this_root, XTreeParent)=nodes)
//23树-设置节点,0是指向父节点
#define XTTTree_SetNode(this_root,nSel，node)	(*XTTTree_Node(this_root,nSel)=nodes)

//数据,索引从0开始
#define XTTTree_GetData(this_root,nSel,Type)	(*((Type*)XTTTree_data(this_root,nSel)))//23树-获取数据

//节点数
enum  XTTTree_NodeNum
{
	XTTTree_TwoNode=2,//二节点
	XTTTree_ThreeNode,//三节点
	XTTTree_FourNode,//四节点
};
//23树节点
typedef struct XTTTreeNode
{
	XTreeNode object;
	XVector* pvValueArray;//XVector数据数组,值
}XTTTreeNode;
//创建初始化一个23树节点
XTTTreeNode* XTTTree_create(const enum XTTTree_NodeNum nodeCount, const char* pvData, const size_t TypeSize);
//当前是几节点
const enum  XTTTree_NodeNum  XTTTree_NodeNum(const XTTTreeNode* this_root);
//升级当前节点
const enum  XTTTree_NodeNum XTTTree_NodeUp(XTTTreeNode* this_root, XCompare compare,const void* pvData, const size_t TypeSize);
//返回节点指针的地址
XTTTreeNode** XTTTree_Node(const XTTTreeNode* this_root, size_t nSel);
//返回数据指针
void* XTTTree_data(const XTTTreeNode* this_root, size_t nSel);
//释放节点
void XTTTree_delete(const XTTTreeNode* this_root);
//查找23树节点
XTTTreeNode* XTTTree_findData(XTTTreeNode* this_root, XLess less, XEquality equality, XCompareRuleOne equalityRule, void* pvData);
//插入一个数据
XTTTreeNode* XTTTree_insert(XTTTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* pvData, const size_t TypeSize);
//删除一个数据
XTTTreeNode* XTTTree_erase(XTTTreeNode** this_root, XLess less, XEquality equality, XCompareRuleOne Rule, const void* pvData);
#ifdef __cplusplus
}
#endif
#endif // !XTWOTHREETREE_H
