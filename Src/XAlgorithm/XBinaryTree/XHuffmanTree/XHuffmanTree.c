#include"XHuffmanTree.h"
#if XMap_ON
#include"XEquality.h"
#include"XLess.h"
#include<stdlib.h>
XHuffmanTree* XHfmTree_init()
{
	XHuffmanTree* tree = XMemory_malloc(sizeof(XHuffmanTree));
	if (ISNULL(tree, "初始化哈夫曼树失败"))
	{
		return NULL;
	}
	tree->root = NULL;
	tree->dictionaries = XMap_Create(char, DictionaryValue,XEquality_char,XLess_char);
	if (ISNULL(tree->dictionaries, "申请哈夫曼树字典失败"))
	{
		return NULL;
	}
	return tree;
}

const bool XHfmTree_readData(XHuffmanTree* tree,const char* data, const size_t size)
{
	if (ISNULL(tree, "传入的哈夫曼是NULL"))
	{
		return false;
	}
	XHfmTree_setDictionaries(tree->dictionaries,data,size);
	tree->root=XHfmTree_DictionariesToCreationTree(tree->dictionaries);
	if(tree->root)
	{
		XHfmTree_setCode(tree->root);
		return true;
	}
	return false;
}
//释放哈夫曼编码数组
static void freeCode(XPair** pair,void*args)
{
#if XVector_ON
	XVector* v = XPair_Second(*pair, DictionaryValue).code;
	XVector_delete_base(v);
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
void XHfmTree_clear(XHuffmanTree* tree)
{
	//释放哈夫曼树
	if(tree->root!=NULL)
		XBTree_delete(tree->root);
	tree->root = NULL;
	//清空哈夫曼编码
	XMap_iterator_for_each(tree->dictionaries, freeCode,NULL);
	XMap_clear_base(tree->dictionaries);
}

void XHfmTree_delete(XHuffmanTree* tree)
{
	XHfmTree_clear(tree);
	XMap_delete_base(tree->dictionaries);
	XMemory_free(tree);
}

#endif