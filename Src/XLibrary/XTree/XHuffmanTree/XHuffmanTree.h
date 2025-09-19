#ifndef XHUFFMANTREE_H
#define XHUFFMANTREE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "CXinYueConfig.h"
#if XMap_ON
#include "XMap.h"
#include "XBinaryTree.h"
#include "XVector.h"
#include "XPriorityQueue.h"
#include "XCompare.h"
#include <stdlib.h>
#include <string.h>

    // 哈夫曼节点数据
    typedef struct XHfmNodeData {
        unsigned char ch;       // 字符（使用unsigned避免符号问题）
        size_t count;           // 出现次数
        XVector* code;          // 哈夫曼编码（存储char类型的0/1）
    } XHfmNodeData;

    // 哈夫曼树节点（复用二叉树节点）
    typedef XTreeNode XHfmNode;

    // 字典值（用于存储字符计数和编码）
    typedef struct DictionaryValue {
        size_t count;           // 出现次数
        XVector* code;          // 哈夫曼编码
    } DictionaryValue;

    // 压缩时写入的字典数据结构
    typedef struct DictionaryData {
        unsigned char ch;       // 字符
        size_t count;           // 出现次数
        size_t codeSize;        // 编码长度（字节数）
    } DictionaryData;

    // 哈夫曼树结构
    typedef struct XHuffmanTree {
        XHfmNode* root;         // 根节点
        XMap* dictionaries;     // 字典（key: unsigned char, value: DictionaryValue）
    } XHuffmanTree;

    // 宏定义：获取节点数据
#define XHfmTree_GetNodeData(node) ((XHfmNodeData*)XTreeNode_getData(node))

// 函数声明
    XHuffmanTree* XHfmTree_init();
    XHfmNode* XHfmTree_createNode(unsigned char ch, size_t count, XVector* code);
    XHfmNode* XHfmTree_DictionariesToCreationTree(XMap* dictionaries);
    void XHfmTree_setCode(XHfmNode* root);
    void XHfmTree_setDictionaries(XMap* dictionaries, const char* data, const size_t size);
    int XHfmTree_writeCompressDictionaries(XVector* retData, XMap* dictionaries);
    const bool XHfmTree_readData(XHuffmanTree* tree, const char* data, const size_t size);
    XVector* XHfmTree_gzip(XHuffmanTree* tree, const char* data, const size_t size);
    XVector* XHfmTree_unzip(XHuffmanTree* tree, const char* data, const size_t size);
    void XHfmTree_clear(XHuffmanTree* tree);
    void XHfmTree_delete(XHuffmanTree* tree);

#endif // XMap_ON
#ifdef __cplusplus
}
#endif
#endif // XHUFFMANTREE_H