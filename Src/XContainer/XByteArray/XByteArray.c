#include "XByteArray.h"
#if XByteArray_ON
#include "XString.h"
#include <string.h>
XByteArray* XByteArray_create()
{
	XByteArray* array = XMalloc_System(sizeof(XByteArray));
	if (array == NULL)
		return NULL;
	XByteArray_init(array);
	Set_Class_MemoryFree(array, XFree_System);
	return array;
}

XByteArray* XByteArray_create_with_data(const char* data,size_t size)
{
	XByteArray* array=XByteArray_create();
	if (!array)return;
	//if (size != 0)
	//{
	//	XVector_resize_base(array, size);
	//}
	XByteArray_append_array_base(array,data,size);
	return array;
}

XByteArray* XByteArray_create_copy(const XByteArray* other)
{
	if (other == NULL)
		return NULL;
	XByteArray* v = XByteArray_create();
	XByteArray_copy_base(v, other);
	return v;
}

XByteArray* XByteArray_create_move(XByteArray* other)
{
	if (other == NULL)
		return NULL;
	XByteArray* v = XByteArray_create();
	XByteArray_move_base(v, other);
	return v;
}

void XByteArray_init(XByteArray* array)
{
	if (array == NULL)
		return;
	XVector_init(array,sizeof(uint8_t));
	XContainerSetCompare(array, uint8_t_compare);
	//array->m_class.m_equality = XEquality_uint8_t;
}

bool XByteArray_push_front_base(XByteArray* array, const uint8_t byte)
{
	return XVector_push_front_base(array,&byte);
}

bool XByteArray_push_back_base(XByteArray* array, const uint8_t byte)
{
	return XVector_push_back_base(array, &byte);
}

bool XByteArray_insert_base(XByteArray* array, int64_t index, const uint8_t byte)
{
	return XVector_insert(array,index,&byte);
}

bool XByteArray_inserts_base(XByteArray* array, int64_t index, uint8_t byte, size_t n)
{
	return XVector_insert_array_base(array, index, &byte,n);
}

bool XByteArray_append_utf8(XByteArray* array, const char* utf8)
{
	if (array == NULL || utf8 == NULL)
		return false;
	size_t len = strlen(utf8);
	if (len == 0)
		return false;
	return XByteArray_append_array_base(array,utf8,len);
}

bool XByteArray_find_base(const XByteArray* array, const uint8_t findVal, XByteArray_iterator* it)
{
	return XVector_find_base(array,&findVal,it);
}

int32_t XByteArray_compare(const XByteArray* lhs, const XByteArray* rhs)
{
	if(XContainerSize(lhs)< XContainerSize(rhs))
		return XCompare_Less;
	if (XContainerSize(lhs) > XContainerSize(rhs))
		return XCompare_Greater;
	if (XByteArray_isEmpty_base(lhs) && XByteArray_isEmpty_base(rhs))
		return XCompare_Equality;
	int cmp = memcmp(XContainerSharedDataPtr(lhs), XContainerSharedDataPtr(rhs), XContainerSize(lhs));
	if(cmp==0)
		return XCompare_Equality;
	if(cmp<0)
		return XCompare_Less;
	return XCompare_Greater;
}

XByteArray* XByteArray_to16HexUtf8(XByteArray* array)
{
	if (array == NULL || XByteArray_isEmpty_base(array))
		return NULL;
	XByteArray* bytes = XByteArray_create();
	uint8_t temp[6];
	for (size_t i = 0; i < XByteArray_size_base(array); i++)
	{
		sprintf(temp,"%02X ", XByteArray_At_Base(array, i));
		XByteArray_append_array_base(bytes,temp,3);
	}
	XByteArray_Back_Base(bytes) = 0;
	return bytes;
}
XString* XByteArray_to16HexString(XByteArray* array)
{
	if (array == NULL || XByteArray_isEmpty_base(array))
		return NULL;
	XString* str = XString_create();
	uint8_t temp[6];
	for (size_t i = 0; i < XByteArray_size_base(array); i++)
	{
		sprintf(temp, "%02X ", XByteArray_At_Base(array, i));
		XString_append_with_length_utf8(str, temp,3);
	}
	return str;
}

#include"XBase64.h"
XByteArray* XByteArray_toBase64(XByteArray* array)
{
	if (array == NULL || XByteArray_isEmpty_base(array))
		return NULL;
	XByteArray* base64 = XByteArray_create();
	if (base64 == NULL)
		return NULL;
	XByteArray_resize_base(base64, XBase64_encoded_size(XContainerSize(array)));
	size_t len = XContainerSize(base64);
	if (XBase64_encode(XContainerSharedDataPtr(array), XContainerSize(array), XContainerSharedDataPtr(base64), &len) != 0)
	{
		XByteArray_delete_base(base64);
		return NULL;
	}
	XContainerSize(base64) = len;
	return base64;
}
XByteArray* XByteArray_fromBase64(XByteArray* base64)
{
	if (base64 == NULL || XByteArray_isEmpty_base(base64))
		return NULL;
	XByteArray* data = XByteArray_create();
	if (data == NULL)
		return NULL;
	XByteArray_resize_base(data, XBase64_decoded_size(XContainerSharedDataPtr(base64), XContainerSize(base64)));
	size_t len = XContainerSize(data);
	if (XBase64_decode(XContainerSharedDataPtr(base64), XContainerSize(base64), XContainerSharedDataPtr(data), &len) != 0)
	{
		XByteArray_delete_base(data);
		return NULL;
	}
	XContainerSize(data) = len;
	return data;
}

#include"zlib.h"
XByteArray* XByteArray_toCompress(XByteArray* sData)
{
	if (sData == NULL || XByteArray_isEmpty_base(sData))
		return NULL;

	// 获取输入数据
	const uint8_t* input_data = XContainerSharedDataPtr(sData);
	uLongf input_len = XContainerSize(sData);

	// 计算压缩缓冲区所需的最大大小
	uLongf output_len = compressBound(input_len);

	// 创建压缩结果缓冲区
	XByteArray* compressed = XByteArray_create(output_len);
	if (compressed == NULL)
		return NULL;

	uint8_t* output_data = XContainerSharedDataPtr(compressed);

	// 执行压缩
	int ret = compress(output_data, &output_len, input_data, input_len);
	if (ret != Z_OK)
	{
		XByteArray_delete_base(compressed);
		return NULL;
	}

	// 调整实际大小
	XContainerSize(compressed) = output_len;
	return compressed;
}
XByteArray* XByteArray_toDecompress(XByteArray* sData)
{
	if (sData == NULL || XByteArray_isEmpty_base(sData))
		return NULL;

	// 获取压缩数据
	const uint8_t* input_data = XContainerSharedDataPtr(sData);
	uLongf input_len = XContainerSize(sData);

	// 初始解压缓冲区大小（设为输入大小的4倍，可根据实际情况调整）
	uLongf output_len = input_len * 4;
	XByteArray* decompressed = XByteArray_create(output_len);
	if (decompressed == NULL)
		return NULL;

	int ret;
	const int max_attempts = 10; // 最大尝试次数，避免无限循环
	int attempts = 0;

	while (attempts < max_attempts)
	{
		uint8_t* output_data = XContainerSharedDataPtr(decompressed);
		uLongf current_output_len = output_len;

		// 执行解压
		ret = uncompress(output_data, &current_output_len, input_data, input_len);

		if (ret == Z_OK)
		{
			// 解压成功，调整大小并返回
			XContainerSize(decompressed) = current_output_len;
			return decompressed;
		}
		else if (ret == Z_BUF_ERROR)
		{
			// 缓冲区不足，尝试更大的缓冲区
			output_len *= 2;
			if (!XByteArray_resize_base(decompressed, output_len))
			{
				XByteArray_delete_base(decompressed);
				return NULL;
			}
			attempts++;
		}
		else
		{
			// 其他错误（如数据损坏）
			XByteArray_delete_base(decompressed);
			return NULL;
		}
	}

	// 超过最大尝试次数
	XByteArray_delete_base(decompressed);
	return NULL;
}
#endif