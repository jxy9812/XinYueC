//23树
#ifndef XTWOTHREETREE_H
#define XTWOTHREETREE_H
#include"XBinaryTreeObject.h"
#include"XFunctionCallback.h"
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
	XBTreeNode object;
	XVector* LpValueArray;//XVector数据数组,值
}XTTTreeNode;
//创建初始化一个23树节点
XTTTreeNode* XTTTree_creationNode(const enum XTTTree_NodeNum nodeCount,const size_t TypeSize);
//当前是几节点
const enum  XTTTree_NodeNum  XTTTree_NodeNum(const XTTTreeNode* this_root);
//升级当前节点
const enum  XTTTree_NodeNum XTTTree_NodeUp(XTTTreeNode* this_root, XLess less,const void* LPData, const size_t TypeSize);
//返回节点指针的地址
XTTTreeNode** XTTTree_Node(const XTTTreeNode* this_root, size_t nSel);
//返回数据指针
void* XTTTree_data(const XTTTreeNode* this_root, size_t nSel);
//释放节点
void XTTTree_free(const XTTTreeNode* this_root);
//查找23树节点
XTTTreeNode* XTTTree_findData(XTTTreeNode* this_root, XLess less, XEquality equality, XCompareRuleOne equalityRule, void* LPData);
//插入一个数据
XTTTreeNode* XTTTree_insert(XTTTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* LPData, const size_t TypeSize);
//删除一个数据
XTTTreeNode* XTTTree_erase(XTTTreeNode** this_root, XLess less, XEquality equality, XCompareRuleOne Rule, const void* LPData);
#endif // !XTWOTHREETREE_H
