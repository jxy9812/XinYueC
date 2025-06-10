#include"XHuffmanTree.h"
#if XMap_ON
#include<string.h>
void XHfmTree_setDictionaries(XMap* dictionaries, const char* data, const size_t size)
{
#if XVector_ON
	if (ISNULL(dictionaries, "哈夫曼树字典不能NULL"))
	{
		return;
	}
	for (size_t i = 0; i < size; i++)
	{
		DictionaryValue* dv = XMap_value_base(dictionaries, data + i);
		//创建哈夫曼编码数组
		if (dv->count == 0)
		{
			dv->code = XVector_Create(char);
		}
		dv->count += 1;//计数+1
		//XMap_At(tree->dictionaries,data[i],size_t)+=1;
	}
#else
	IS_ON_DEBUG(XVector_ON);
#endif
}
//写入字典数据到压缩的数组中
static void writeDictionaryData(XPair** LPpair, XVector* gzipData)
{
	size_t currentSize= XVector_getSize_base(gzipData);//当前字节大小
	XVector* code = XPair_Second(*LPpair, DictionaryValue).code;//编码数组
	size_t codeSize = XVector_getSize_base(code);//编码大小(字节)
	DictionaryData data = {XPair_First(*LPpair,char),XPair_Second(*LPpair,size_t),codeSize };
	XVector_resize_base(gzipData,currentSize + sizeof(DictionaryData) + codeSize);//扩容到可以写入一组数据
	char* LPcurrent = (char*)XContainerDataPtr(gzipData)+ currentSize;//当前可以写入的指针
	memcpy(LPcurrent, &data, sizeof(DictionaryData));//拷贝字典基本数据
	LPcurrent += sizeof(DictionaryData);//移位到后面写入编码
	memcpy(LPcurrent, XContainerDataPtr(code), codeSize);//拷贝编码
}

int XHfmTree_writeCompressDictionaries(XVector* gzipData, XMap* dictionaries)
{
#if XVector_ON
	size_t count = XMap_getSize_base(dictionaries);
	XVector_resize_base(gzipData, sizeof(size_t));
	//写入数量
	memcpy(XContainerDataPtr(gzipData), &count,sizeof(size_t));//写入数据个数
	XMap_iterator_for_each(dictionaries, writeDictionaryData, gzipData);
	return XVector_getSize_base(gzipData);
#else
	IS_ON_DEBUG(XVector_ON);
	return 0;
#endif
}

#endif