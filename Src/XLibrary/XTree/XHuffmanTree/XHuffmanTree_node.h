//哈夫曼节点类型声明
#ifndef XHUFFMANTREE_NODE_H
#define XHUFFMANTREE_NODE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef XTreeNode XHfmNode;//哈夫曼树节点
//哈夫曼节点数据
typedef struct XHfmNodeData
{
	char ch;//字符
	size_t count;//出现次数
	XVector* code;//哈夫曼编码   value:char类型
}XHfmNodeData;
#ifdef __cplusplus
}
#endif
#endif// !XHUFFMANTREE_NODE_H
