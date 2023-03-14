#ifndef XHUFFMANTREE_H
#define XHUFFMANTREE_H
#include"XMap.h"
#include"XBinaryTreeObject.h"
typedef XBTreeNode XHfmNode;//哈夫曼树节点
//哈夫曼节点数据
typedef struct XHfmNodeData
{
	unsigned char ch;//字符
	size_t count;//出现次数
	XVector* code;//哈夫曼编码
}XHfmNodeData;
//哈夫曼树
typedef struct XHuffmanTree
{
	XHfmNode* root;//树的根据节点
	XMap* dictionaries;//字典
}XHuffmanTree;
//创建一个哈夫曼节点
XHfmNode* XHfmTree_creationNode(const size_t TypeSize);
//哈夫曼树初始化
void XHfmTree_init(XHuffmanTree* this_tree);
#endif // !XHuffman_h
