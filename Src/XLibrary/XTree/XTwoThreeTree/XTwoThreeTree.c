#include"XTwoThreeTree.h"
#include"XVector.h"
#include"XClass.h"
#include<string.h>
XTTTreeNode* XTTTree_create(const enum XTTTree_NodeNum nodeCount, const char* pvData, const size_t TypeSize)
{
#if XVector_ON
    //XTTTreeNode* nodes = XBTree_creationNode(sizeof(XTTTreeNode), nodeCount + 1,1, TypeSize);
    XTTTreeNode* nodes = XMemory_malloc(sizeof(XTTTreeNode));
    if (nodes == NULL)//初始化父类失败
        return NULL;
    XTreeNode_init(nodes, nodeCount + 1,pvData, TypeSize);
    nodes->pvValueArray = NULL;
    if (nodeCount == XTTTree_TwoNode)
        return nodes;
    nodes->pvValueArray = XVector_create(TypeSize);
    if (nodes->pvValueArray == NULL)//创建数据数组失败
    {
        XTreeNode_delete(nodes);
        return NULL;
    }
    //插入0值,初始化剩余的值
    XVector_resize_base(nodes->pvValueArray, nodeCount - 2);
    return nodes;
#else
    IS_ON_DEBUG(XVector_ON);
    return NULL;
#endif
}

const enum  XTTTree_NodeNum XTTTree_NodeNum(const XTTTreeNode* this_root)
{
#if XVector_ON
    if (ISNULL(this_root, ""))
        return 0;
    return XVector_size_base(this_root->pvValueArray)+2;
#else
    IS_ON_DEBUG(XVector_ON);
    return XTTTree_TwoNode;
#endif
}

const enum XTTTree_NodeNum XTTTree_NodeUp(XTTTreeNode* this_root, XLess less, const void* pvData, const size_t TypeSize)
{
#if XVector_ON
    if (ISNULL(this_root, ""))
        return 0;
    XVector* LPNode = this_root->object.nodes;//储存节点指针的数组
    enum  XTTTree_NodeNum nodeNum = XTTTree_NodeNum(this_root);//当前是几节点
    XVector_resize_base(LPNode, XVector_size_base(LPNode)+1);//储存指针的扩容+1
    if (nodeNum == XTTTree_TwoNode)//当前是二节点
    {
       /* XTTTreeNode* temp = *(XTTTreeNode**)XVector_back_base(this_root);
        XVector_push_back_base(this_root,&temp);*/
        
        this_root->pvValueArray = XVector_create( TypeSize);//初始化值
    }
    //XVector_resize_base(this_root->pvValueArray, XVector_size_base(LPNode) + 1);//储存数据的扩容+1
    XVector_push_back_base(this_root->pvValueArray, pvData);//插入数值扩容
    XVector_push_back_base(this_root->pvValueArray, XTreeNode_GetDataPtr(this_root));//插入第一个数值
    XVector_sort_base(this_root->pvValueArray, less);//排序
    memcpy(XTreeNode_GetDataPtr(this_root), XContainerDataPtr(this_root->pvValueArray), TypeSize);//将最小的拷贝回去
    //XVector_erase_int(this_root, 0, 0);//删除重复的第一个
    XVector_pop_front_base(this_root);
    return XTTTree_NodeNum(this_root);
#else
    IS_ON_DEBUG(XVector_ON);
    return XTTTree_TwoNode;
#endif
}

XTTTreeNode** XTTTree_Node(const XTTTreeNode* this_root, size_t nSel)
{
    return XTreeNode_getChildRef(this_root, nSel);
}

void* XTTTree_data(const XTTTreeNode* this_root, size_t nSel)
{
#if XVector_ON
    if (ISNULL(this_root, ""))
        return 0;
    if (/*this_root->pvValueArray == NULL||*/ nSel==0)//当前是二节点
    {
        return XTreeNode_GetDataPtr(this_root);
    }
    if (!XVector_isEmpty_base(this_root->pvValueArray)&& nSel<3)
    {
        return XVector_at_base(this_root->pvValueArray, nSel - 1);
    }
    return NULL;
#else
    IS_ON_DEBUG(XVector_ON);
    return NULL;
#endif
}

void XTTTree_delete(const XTTTreeNode* this_root)
{
#if XVector_ON
    if (ISNULL(this_root, ""))
        return 0;
    if (this_root->pvValueArray != NULL)
        XVector_delete_base(this_root->pvValueArray);
    XTreeNode_delete(this_root);
#else
    IS_ON_DEBUG(XVector_ON);
    return NULL;
#endif
}

XTTTreeNode* XTTTree_findData(XTTTreeNode* this_root, XLess less, XEquality equality, XCompareRuleOne equalityRule, void* pvData)
{
    return NULL;
}

XTTTreeNode* XTTTree_insert(XTTTreeNode** this_root, XLess less, XCompareRuleTwo lessRule, const void* pvData, const size_t TypeSize)
{
    XTTTreeNode* nodes = XTTTree_create(XTTTree_TwoNode,pvData, TypeSize);
    if (ISNULL(nodes, ""))
        return NULL;
    //XBTree_insertData(nodes, pvData, 0);//插入数据
   // XTreeNode_setData(nodes, pvData);
    if (this_root == NULL)//创建根节点
    {
        return nodes;
    }

    return NULL;
}

