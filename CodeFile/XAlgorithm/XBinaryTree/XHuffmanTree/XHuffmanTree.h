#ifndef XHUFFMANTREE_H
#define XHUFFMANTREE_H
#include"XMap.h"
#include"XBinaryTreeObject.h"
#include"XHuffmanTree_macro.h"
#include"XHuffmanTree_node.h"
#include"XHuffmanTree_dictionaries.h"
//哈夫曼树
typedef struct XHuffmanTree
{
	XHfmNode* root;//树的根节点
	XMap* dictionaries;//字典  key:char value: DictionaryValue
}XHuffmanTree;
//创建一个哈夫曼节点
XHfmNode* XHfmTree_creationNode(unsigned char ch,size_t count, XVector* code);
//创建哈夫曼树初始化
XHuffmanTree* XHfmTree_init();
//根据字典创建树
XHfmNode* XHfmTree_DictionariesToCreationTree(XMap* dictionaries);
//根据哈夫曼树设置哈夫曼编码
void XHfmTree_setCode(XHfmNode* root);
//根据数据生成字典(不带编码)
void XHfmTree_setDictionaries(XMap* dictionaries, const char* data, const size_t size);
//根据字典写入压缩数据
int XHfmTree_writeCompressDictionaries(XVector* retData, XMap* dictionaries);
//读取数据构建哈夫曼树
const bool XHfmTree_readData(XHuffmanTree* tree,const char* data,const size_t size);
//根据哈夫曼树和字典压缩数据
XVector* XHfmTree_gzip(XHuffmanTree* tree, const char* data, const size_t size);
//根据哈夫曼树和字典解压数据
XVector* XHfmTree_unzip(XHuffmanTree* tree, const char* data, const size_t size);
//清空哈夫曼树
void XHfmTree_clear(XHuffmanTree* tree);
//释放哈夫曼树
void XHfmTree_free(XHuffmanTree* tree);
#endif // !XHuffman_h
