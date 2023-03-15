#include"XHuffmanTree.h"
#include"XEquality.h"
#include"XLess.h"
XHuffmanTree* XHfmTree_init()
{
	XHuffmanTree* tree = malloc(sizeof(XHuffmanTree));
	if (ISNULL(tree, "初始化哈夫曼树失败"))
	{
		return NULL;
	}
	tree->root = NULL;
	tree->dictionaries = XMap_Init(char, DictionaryValue,XEquality_char,XLess_char);
	if (ISNULL(tree->dictionaries, "申请哈夫曼树字典失败"))
	{
		return NULL;
	}
	return tree;
}
void XHfmTree_setDictionaries(XMap* dictionaries, const char* data, const size_t size)
{
	if (ISNULL(dictionaries, "哈夫曼树字典不能NULL"))
	{
		return ;
	}
	for (size_t i = 0; i < size; i++)
	{
		DictionaryValue* dv = XMap_at(dictionaries, data + i);
		//创建哈夫曼编码数组
		if (dv->count == 0)
		{
			dv->code = XVector_Init(char);
		}
		dv->count += 1;//计数+1
		//XMap_At(tree->dictionaries,data[i],size_t)+=1;
	}
}

const bool XHfmTree_readData(XHuffmanTree* tree,const char* data, const size_t size)
{
	if (ISNULL(tree, "传入的哈夫曼是NULL"))
	{
		return false;
	}
	XHfmTree_setDictionaries(tree->dictionaries,data,size);
	tree->root=XHfmTree_creationTree(tree->dictionaries);
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
	XVector* v = XPair_Second(*pair, DictionaryValue).code;
	XVector_free(v);
}
void XHfmTree_clear(XHuffmanTree* tree)
{
	//释放哈夫曼树
	if(tree->root!=NULL)
		XBTree_freeNodeAll(tree->root);
	tree->root = NULL;
	//清空哈夫曼编码
	XMap_iterator_for_each(tree->dictionaries, freeCode,NULL);
	XMap_clear(tree->dictionaries);
}

void XHfmTree_free(XHuffmanTree* tree)
{
	XHfmTree_clear(tree);
	XMap_free(tree->dictionaries);
	free(tree);
}
