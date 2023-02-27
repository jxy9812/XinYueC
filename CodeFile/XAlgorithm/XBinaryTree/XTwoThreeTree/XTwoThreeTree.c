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

const enum XTTTree_NodeNum XTTTree_NodeUp(XTTTreeNode* this_root, XLess less, const void* LPData, const size_t TypeSize)
{
    if (isNULL(isNULLInfo(this_root, "")))
        return 0;
    XVector* LPNode = this_root->object.node;//储存节点指针的数组
    enum  XTTTree_NodeNum nodeNum = XTTTree_NodeNum(this_root);//当前是几节点
    XVector_resize(LPNode, XVector_size(LPNode)+1);//储存指针的扩容+1
    if (nodeNum == XTTTree_TwoNode)//当前是二节点
    {
       /* XTTTreeNode* temp = *(XTTTreeNode**)XVector_back(this_root);
        XVector_push_back(this_root,&temp);*/
        
        this_root->LPvalueArray = XVector_init("", TypeSize);//初始化值
    }
    //XVector_resize(this_root->LPvalueArray, XVector_size(LPNode) + 1);//储存数据的扩容+1
    XVector_push_back(this_root->LPvalueArray, LPData);//插入数值扩容
    XVector_push_back(this_root->LPvalueArray, this_root->object.LPvalue);//插入第一个数值
    XVector_sort(this_root->LPvalueArray, less);//排序
    memcpy(this_root->object.LPvalue, XVector_begin(this_root->LPvalueArray), TypeSize);//将最小的拷贝回去
    XVector_erase_int(this_root, 0, 0);//删除重复的第一个
    return XTTTree_NodeNum(this_root);
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



