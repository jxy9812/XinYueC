#include"XTwoThreeTree.h"
XTTTreeNode* XTTTree_insert(XTTTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* LPData, const size_t TypeSize)
{
    XTTTreeNode* nodes = XTTTree_creationNode(XTTTree_TwoNode,TypeSize);
    if (isNULL(isNULLInfo(nodes, "")))
        return NULL;
    XBTree_insertData(nodes, LPData, 0,TypeSize);//插入数据
    if (this_root == NULL)//创建根节点
    {
        return nodes;
    }

    return NULL;
}