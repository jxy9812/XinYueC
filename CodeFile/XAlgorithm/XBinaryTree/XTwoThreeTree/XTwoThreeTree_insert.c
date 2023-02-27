#include"XTwoThreeTree.h"
XTTTreeNode* XTTTree_insert(XTTTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* LPData, const size_t TypeSize)
{
    XTTTreeNode* node = XTTTree_creationNode(XTTTree_TwoNode,TypeSize);
    if (isNULL(isNULLInfo(node, "")))
        return NULL;
    XBTree_insertData(node, LPData, TypeSize);//插入数据
    if (this_root == NULL)//创建根节点
    {
        return node;
    }

    return NULL;
}