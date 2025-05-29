#include"XHuffmanTree.h"
#if XMap_ON
#include<string.h>
//读取压缩后的数据//构建字典
static size_t readDictionaries(XHuffmanTree* tree, const char* data)
{
#if XVector_ON
	size_t offset = 0;//偏移字节
	size_t count = *(size_t*)data;//字典的DictionaryData数量
	offset += sizeof(size_t);
	DictionaryData* DictionData = data + offset;//字典数据
	for (size_t i = 0; i < count; i++)
	{
		DictionaryValue* value=&XMap_Value_Base(tree->dictionaries, DictionData->ch, DictionaryValue);
		size_t codeSize = DictionData->codeSize;//编码大小字节
		value->count = DictionData->count;
		XVector* code= XVector_New(char);
		value->code = code;
		XVector_resize_base(code,codeSize);
		offset += sizeof(DictionaryData);//指向写入的哈夫曼编码
		memcpy(XVector_begin(code), data + offset, codeSize);
		offset += codeSize;//指向下一组数据
		DictionData = data + offset;//重新定位指针
	}
	return offset;
#else
	IS_ON_DEBUG(XVector_ON);
	return 0;
#endif
}
//写入解压后的数据
static bool writeUnZip(XVector* unzipData, XHfmNode*node)
{
	//printf("%d ", XHfmTree_GetNodeData(node).ch);
	if (XHfmTree_GetNodeData(node).code != NULL)//当前的节点有数据
	{
		XVector_push_back_base(unzipData, &XHfmTree_GetNodeData(node).ch);
		return true;
	}
	return false;
}
//累加计算字符的次数
static void addCount(XPair** LPpair, size_t* LPcount)
{
	*LPcount += XPair_Second(*LPpair, DictionaryValue).count;
}
//读取压缩后的数据
static XVector*  writeUnZipData(XHuffmanTree* tree, const char* data,const size_t size)
{
#if XVector_ON
	size_t countMax = 0;//字符最大出现次数
	size_t count = 0;//字符出现次数
	XMap_iterator_for_each(tree->dictionaries, addCount, &countMax);//累加计算字符的最大次数
	XVector* unzipData = XVector_New(char);//返回的压缩后的数据
	XHfmNode* root = tree->root;//根节点
	XHfmNode* currentNode = root;//当前节点
	for (size_t i = 0; i < size; i++)//遍历每一个字节
	{
		char byteRead = data[i];//读取的一字节
		//字节内的比特位索引
		
		for (char charReadIdx = 0; charReadIdx < 8; charReadIdx++)//遍历每一个比特位
		{
			char temp = (byteRead >> (7 - charReadIdx)) & 0x01;
			if (temp == 0)//左边
			{
				if (writeUnZip(unzipData, currentNode = XBTree_GetLChild(currentNode)))
				{
					currentNode = root;
					++count;
				}
			}
			else if (temp == 1)//右边
			{
				if (writeUnZip(unzipData, currentNode = XBTree_GetRChild(currentNode)))
				{
					currentNode = root;
					count++;
				}
			}
			if (count == countMax)//已经遍历完了
				return unzipData;
		}
	}
	return unzipData;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif
}
XVector* XHfmTree_unzip(XHuffmanTree* tree, const char* data, const size_t size)
{
	XHfmTree_clear(tree);//哈夫曼树清空测试
	size_t offset = readDictionaries(tree, data);
	tree->root = XHfmTree_DictionariesToCreationTree(tree->dictionaries);
	XVector* unzipData = writeUnZipData(tree, data + offset, size- offset);//返回的压缩后的数据
	return unzipData;
}

#endif