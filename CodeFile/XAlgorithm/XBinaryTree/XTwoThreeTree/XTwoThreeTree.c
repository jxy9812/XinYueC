#include"XTwoThreeTree.h"

XTTTreeNode* XTTTree_creationNode(const size_t nodeCount, const size_t TypeSize)
{
    XTTTreeNode* node = XBTree_creationNode(sizeof(XTTTreeNode), nodeCount + 1, TypeSize);
    if (node == NULL)//初始化父类失败
        return NULL;
    node->value = NULL;
    if (nodeCount == 2)
        return node;
    node->value = XVector_init("", TypeSize);
    if (node->value == NULL)//创建数据数组失败
    {
        XBTree_freeNode(node,false);
        return NULL;
    }
    //插入0值,初始化剩余的值
    XVector_resize(node->value, nodeCount - 2);
    return node;
}
