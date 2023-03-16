#include"XHuffmanTree.h"
//写入压缩数据
static void writeData(XVector* compressData, XMap* dictionaries, const char* data, const size_t size)
{
	size_t currentSize = XVector_size(compressData);//当前字节大小
	XVector* code = NULL;//哈夫曼编码数组
	char byteWrite = 0;//写入的一字节
	char charWriteIdx = 0;//字节内的比特位索引
	for (size_t i = 0; i < size; i++)
	{
		char ch = data[i];//遍历每一个字节
		code = XMap_At(dictionaries, ch, DictionaryValue).code;
		//遍历编码
		for (XVector_iterator* it = XVector_begin(code); it != XVector_end(code); it = XVector_iterator_add(code, it))
		{
			if (0 == *(char*)it)
			{
				byteWrite &= ~(1 << (7 - charWriteIdx));
				++charWriteIdx;
			}
			else if (1 == *(char*)it)
			{
				byteWrite |= (1 << (7 - charWriteIdx));
				++charWriteIdx;
			}
			if (charWriteIdx == 8)//写入了一字节
			{
				XVector_push_back(compressData, &byteWrite);
				byteWrite = 0;
				charWriteIdx = 0;
			}
		}
		//charIdx = setBit(data+i, charIdx);
	}
	if (charWriteIdx != 0)//剩下的比特位，写入
	{
		XVector_push_back(compressData, &byteWrite);
	}
}
XVector* XHfmTree_compress(XHuffmanTree* tree, const char* data, const size_t size)
{
	XVector* compressData=XVector_Init(char);//返回的压缩后的数据
	size_t sizeD=XHfmTree_writeCompressDictionaries(compressData, tree->dictionaries);
	//printf("Dictionaries:%d\n",sizeD);
	writeData(compressData, tree->dictionaries,data,size);
	//printf("compressData:%d\n", XVector_size(compressData));
	return compressData;
}