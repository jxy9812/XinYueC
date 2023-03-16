#include"XHuffmanTree.h"
#include<string.h>
//读取压缩后的数据//构建哈夫曼树
static size_t readTree(XHuffmanTree* tree, const char* data)
{
	size_t offset = 0;//偏移字节
	size_t count = *(size_t*)data;//字典的DictionaryData数量
	offset += sizeof(size_t);
	DictionaryData* DictionData = data + offset;//字典数据
	for (size_t i = 0; i < count; i++)
	{
		DictionaryValue* value=&XMap_At(tree->dictionaries, DictionData->ch, DictionaryValue);
		size_t codeSize = DictionData->codeSize;//编码大小字节
		value->count = DictionData->count;
		XVector* code= XVector_Init(char);
		value->code = code;
		XVector_resize(code,codeSize);
		offset += sizeof(DictionaryData);//指向写入的哈夫曼编码
		memcpy(XVector_begin(code), data + offset, codeSize);
		offset += codeSize;//指向下一组数据
		DictionData = data + offset;//重新定位指针
	}
	return offset;
}
//读取压缩后的树
static XVector*  readData(XHuffmanTree* tree, const char* data,const size_t size)
{
	XVector* unzipData = XVector_Init(char);//返回的压缩后的数据
	for (size_t i = 0; i < size; i++)
	{
		char byteRead = data[i];//读取的一字节
		char charReadIdx = 0;//字节内的比特位索引
		//遍历每一个比特位
	}
	return unzipData;
}
XVector* XHfmTree_unzip(XHuffmanTree* tree, const char* data, const size_t size)
{
	XHfmTree_clear(tree);//哈夫曼树清空测试
	size_t offset = readTree(tree, data);
	XVector* unzipData = readData(tree, data + offset, size- offset);//返回的压缩后的数据
	return unzipData;
}