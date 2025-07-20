#ifndef XHUFFMANTREE_MACRO_H
#define XHUFFMANTREE_MACRO_H
#ifdef __cplusplus
extern "C" {
#endif
//哈夫曼树获取节点数据
#define XHfmTree_GetNodeData(node) XTreeNode_GetData(node, XHfmNodeData)
#ifdef __cplusplus
}
#endif
#endif // !XHUFFMANTREE_MACRO_H
