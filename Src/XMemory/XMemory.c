#include"XMemory.h"
#include"string.h"
#ifdef _WIN32
#include<stdlib.h>
static XMemory global_Memory = { malloc,free,realloc,calloc };
#elif defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include<stdlib.h>
static XMemory global_Memory = { malloc,free,realloc,calloc };
#elif defined(__FreeRTOS__) 
#include"FreeRTOS.h"
static XMemory global_Memory = { pvPortMalloc,vPortFree,XMemory_realloc_isMalloc,XMemory_calloc_isMalloc };
#else
static XMemory global_Memory = { 0 };
#endif

void* XMemory_malloc(size_t size)
{
	return global_Memory.allocate(size);
}

void XMemory_setMethod(const XMemory* method)
{
	if(method)
		global_Memory = *method;
}

void XMemory_setMallocMethod(MallocMethod method)
{
	global_Memory.allocate = method;
}

void XMemory_free(void* ptr)
{
	global_Memory.deallocate(ptr);
}

void XMemory_setDeleteMethod(DeleteMethod method)
{
	global_Memory.deallocate = method;
}

void* XMemory_realloc(void* ptr, size_t size)
{
	return global_Memory.reallocate(ptr,size);
}

void* XMemory_calloc(size_t count, size_t size)
{
	return global_Memory.callocZero(count,size);
}

bool XMemory_realloc_isNULL()
{
	return global_Memory.reallocate==NULL;
}

bool XMemory_read_data(const uint8_t* src, XByteOrder readOrder, uint8_t* out, size_t size)
{
	// 参数合法性检查：输入/输出指针为空或数据长度为0时返回失败
	if (src == NULL || out == NULL || size == 0)
		return false;

	// 根据当前系统字节序和输入数据字节序判断是否需要转换
#if IS_BIG_ENDIAN  // 当前系统是大端字节序
	// 若输入数据是小端字节序，则需要转换（大端 <-> 小端）
	if (readOrder == XBYTE_ORDER_LITTLE_ENDIAN)
#else  // 当前系统是小端字节序（默认分支）
	// 若输入数据是大端字节序，则需要转换（小端 <-> 大端）
	if (readOrder == XBYTE_ORDER_BIG_ENDIAN)
#endif
	{
		// 字节序转换：反转字节顺序（低地址字节与高地址字节互换）
		// 例：输入 [0x12, 0x34, 0x56]（3字节）-> 输出 [0x56, 0x34, 0x12]
		for (size_t i = 0; i < size; i++)
		{
			out[i] = src[size - 1 - i];  // 第i个位置存储原数据的倒数第i个字节
		}
	}
	else
	{
		// 无需转换：输入数据字节序与当前系统一致，直接拷贝
		memcpy(out, src, size);
	}

	return true;
}

bool XMemory_write_data(uint8_t* write, XByteOrder writeOrder, const uint8_t* in, size_t size)
{
	// 参数合法性检查：输入/输出指针为空或数据长度为0时返回失败
	if (write == NULL || in == NULL || size == 0)
		return false;

	// 根据当前系统字节序和输入数据字节序判断是否需要转换
#if IS_BIG_ENDIAN  // 当前系统是大端字节序
	// 若输入数据是小端字节序，则需要转换（大端 <-> 小端）
	if (readOrder == XBYTE_ORDER_LITTLE_ENDIAN)
#else  // 当前系统是小端字节序（默认分支）
	// 若输入数据是大端字节序，则需要转换（小端 <-> 大端）
	if (writeOrder == XBYTE_ORDER_BIG_ENDIAN)
#endif
	{
		// 字节序转换：反转字节顺序（低地址字节与高地址字节互换）
		// 例：输入 [0x12, 0x34, 0x56]（3字节）-> 输出 [0x56, 0x34, 0x12]
		for (size_t i = 0; i < size; i++)
		{
			write[size - 1 - i]=in[i];  // 第i个位置存储原数据的倒数第i个字节
		}
	}
	else
	{
		// 无需转换：输入数据字节序与当前系统一致，直接拷贝
		memcpy(write,in, size);
	}

	return true;
}

void XMemory_setReallocMethod(ReallocMethod method)
{
	global_Memory.reallocate = method;
}

void XMemory_setCallocMethod(CallocMethod method)
{
	global_Memory.callocZero = method;
}

void* XMemory_realloc_isMalloc(void* ptr, size_t size)
{
	if (ptr == NULL)
		return XMemory_malloc(size);
	if (size == 0)
	{
		XMemory_free(ptr);
		return NULL;
	}
	void* newPtr = XMemory_malloc(size);
	if (newPtr == NULL)
		return NULL;
	memcpy(newPtr,ptr,size);
	XMemory_free(ptr);
	return newPtr;
}

void* XMemory_calloc_isMalloc(size_t count, size_t size)
{
	void* ptr = XMemory_malloc(count*size);
	if (ptr)
		memset(ptr,0,count*size);
	return ptr;
}
