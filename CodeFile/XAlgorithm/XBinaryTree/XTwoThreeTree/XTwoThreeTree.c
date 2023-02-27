#include"XTwoThreeTree.h"

XTTTreeNode* XTTTree_creationNode(const enum XTTTree_NodeNum nodeCount, const size_t TypeSize)
{
    XTTTreeNode* node = XBTree_creationNode(sizeof(XTTTreeNode), nodeCount + 1, TypeSize);
    if (node == NULL)//初始化父类失败
        return NULL;
    node->LPvalueArray = NULL;
    if (nodeCount == XTTTree_TwoNode)
        return node;
    node->LPvalueArray = XVector_init("", TypeSize);
    if (node->LPvalueArray == NULL)//创建数据数组失败
    {
        XBTree_freeNode(node,false);
        return NULL;
    }
    //插入0值,初始化剩余的值
    XVector_resize(node->LPvalueArray, nodeCount - 2);
    return node;
}

const enum  XTTTree_NodeNum XTTTree_NodeNum(const XTTTreeNode* this_root)
{
    if (isNULL(isNULLInfo(this_root, "")))
        return 0;
    return XVector_size(this_root->LPvalueArray)+2;
}

XTTTreeNode** XTTTree_Node(const XTTTreeNode* this_root, size_t nSel)
{
    return XBTree_GetTreeNode(this_root, nSel);
}

void* XTTTree_data(const XTTTreeNode* this_root, size_t nSel)
{
    if (isNULL(isNULLInfo(this_root, "")))
        return 0;
    if (/*this_root->LPvalueArray == NULL||*/ nSel==0)//当前是二节点
    {
        return this_root->object.LPvalue;
    }
    if (!XVector_empty(this_root->LPvalueArray)&& nSel<3)
    {
        return XVector_at(this_root->LPvalueArray, nSel - 1);
    }
    return NULL;
}

void XTTTree_free(const XTTTreeNode* this_root)
{
    if (isNULL(isNULLInfo(this_root, "")))
        return 0;
    if (this_root->LPvalueArray != NULL)
        XVector_free(this_root->LPvalueArray);
    XBTree_freeNode(this_root, false);
}

XTTTreeNode* XTTTree_findData(XTTTreeNode* this_root, XLess less, XEquality equality, XCompareRuleOne equalityRule, void* LPData)
{
    return NULL;
}

XTTTreeNode* XTTTree_insert(XTTTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* LPData, const size_t TypeSize)
{
    return NULL;
}

XTTTreeNode* XTTTree_erase(XTTTreeNode** this_root, XLess less, XEquality equality, XCompareRuleOne Rule, const void* LPData)
{
    return NULL;
}
