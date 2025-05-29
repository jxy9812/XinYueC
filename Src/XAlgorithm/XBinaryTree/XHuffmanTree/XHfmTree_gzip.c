#include"XHuffmanTree.h"
#if XMap_ON
//写入压缩数据
static void writeData(XVector* gzipData, XMap* dictionaries, const char* data, const size_t size)
{
#if XVector_ON
	size_t currentSize = XVector_getSize_base(gzipData);//当前字节大小
	XVector* code = NULL;//哈夫曼编码数组
	char byteWrite = 0;//写入的一字节
	char charWriteIdx = 0;//字节内的比特位索引
	for (size_t i = 0; i < size; i++)
	{
		char ch = data[i];//遍历每一个字节
		code = XMap_Value_Base(dictionaries, ch, DictionaryValue).code;
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
				XVector_push_back_base(gzipData, &byteWrite);
				byteWrite = 0;
				charWriteIdx = 0;
			}
		}
		//charIdx = setBit(data+i, charIdx);
	}
	if (charWriteIdx != 0)//剩下的比特位，写入
	{
		XVector_push_back_base(gzipData, &byteWrite);
	}
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
XVector* XHfmTree_gzip(XHuffmanTree* tree, const char* data, const size_t size)
{
#if XVector_ON
	XHfmTree_clear(tree);//哈夫曼树清空测试
	XHfmTree_readData(tree, data, size);//读取数据构建哈夫曼树
	XVector* gzipData=XVector_New(char);//返回的压缩后的数据
	size_t sizeD=XHfmTree_writeCompressDictionaries(gzipData, tree->dictionaries);
	//printf("Dictionaries:%d\n",sizeD);
	writeData(gzipData, tree->dictionaries,data,size);
	//printf("gzipData:%d\n", XVector_getSize_base(gzipData));
	return gzipData;
#else
	IS_ON_DEBUG(XVector_ON);
	return NULL;
#endif
}

#endif