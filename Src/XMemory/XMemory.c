#include"XMemory.h"
#include"XMultiPool.h"
#include<string.h>
// ============ 新增：组合内存池的内部函数 ============
#define HYBRID_THRESHOLD (256) // 小于等于此阈值使用 XMultiPool，大于则使用系统 malloc
static void* hybrid_calloc(size_t count, size_t size);
static void* hybrid_realloc(void* ptr, size_t size);
static void hybrid_free(void* ptr);
static void* hybrid_malloc(size_t size);
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__) || defined(__BSD__)
#include<stdlib.h>
static XMemory global_Memory[] = { {malloc,free,realloc,calloc},{XMultiPool_global_malloc,XMultiPool_global_free,XMultiPool_global_realloc,XMultiPool_global_calloc},{hybrid_malloc,hybrid_free,hybrid_realloc,hybrid_calloc} };
#elif defined(__FreeRTOS__) 
#include"FreeRTOS.h"
static XMemory global_Memory = { { pvPortMalloc,vPortFree,XMemory_realloc_isMalloc,XMemory_calloc_isMalloc },{XMultiPool_global_malloc,XMultiPool_global_free,XMultiPool_global_realloc,XMultiPool_global_calloc},{hybrid_malloc,hybrid_free,hybrid_realloc,hybrid_calloc} };
#else//裸机环境
static XMemory global_Memory = { { NULL,NULL,XMemory_realloc_isMalloc,XMemory_calloc_isMalloc },{XMultiPool_global_malloc,XMultiPool_global_free,XMultiPool_global_realloc,XMultiPool_global_calloc},{hybrid_malloc,hybrid_free,hybrid_realloc,hybrid_calloc} };
#endif

void* XMemory_malloc(size_t size, XMemoryType type)
{
	return global_Memory[type].malloc(size);
}
void* XMemory_realloc(void* ptr, size_t size, XMemoryType type)
{
	return global_Memory[type].realloc(ptr, size);
}

void* XMemory_calloc(size_t count, size_t size, XMemoryType type)
{
	return global_Memory[type].calloc(count, size);
}

void XMemory_free(void* ptr, XMemoryType type)
{
	global_Memory[type].free(ptr);
}
void* XMalloc_System(size_t size)
{
	return XMemory_malloc(size, XMEMORY_TYPE_SYSTEM);
}
void* XMalloc_MultiPool(size_t size)
{
	return XMemory_malloc(size, XMEMORY_TYPE_MULTIPOOL);
}
void* XMalloc_Hybrid(size_t size)
{
	return XMemory_malloc(size, XMEMORY_TYPE_HYBRID);
}
char* XMemory_strdup(const char* text)
{
	size_t length;
	char* copy;
	if (!text) return NULL;
	length = strlen(text);
	if (length == SIZE_MAX) return NULL;
	copy = (char*)XMalloc_System(length + 1);
	if (!copy) return NULL;
	memcpy(copy, text, length + 1);
	return copy;
}
void* XAlignedMalloc_System(size_t size, size_t alignment)
{
	if (alignment < sizeof(void*))
		alignment = sizeof(void*);
	if ((alignment & (alignment - 1)) != 0)
		return NULL;

	size_t overhead = alignment - 1 + sizeof(void*);
	if (size > SIZE_MAX - overhead)
		return NULL;

	void* allocation = XMalloc_System(size + overhead);
	if (!allocation)
		return NULL;

	uintptr_t address = (uintptr_t)allocation + sizeof(void*);
	uintptr_t aligned = (address + alignment - 1) & ~(uintptr_t)(alignment - 1);
	((void**)aligned)[-1] = allocation;
	return (void*)aligned;
}
void XAlignedFree_System(void* ptr)
{
	if (ptr)
		XFree_System(((void**)ptr)[-1]);
}
void XFree_System(void* ptr)
{
	XFree(ptr, XMEMORY_TYPE_SYSTEM);
}
void XFree_MultiPool(void* ptr)
{
	XFree(ptr, XMEMORY_TYPE_MULTIPOOL);
}
void XFree_Hybrid(void* ptr)
{
	XFree(ptr, XMEMORY_TYPE_HYBRID);
}
void* XRealloc_System(void* ptr, size_t size)
{
	return XMemory_realloc(ptr, size, XMEMORY_TYPE_SYSTEM);
}
void* XRealloc_MultiPool(void* ptr, size_t size)
{
	return XMemory_realloc(ptr, size, XMEMORY_TYPE_MULTIPOOL);
}
void* XRealloc_Hybrid(void* ptr, size_t size)
{
	return XMemory_realloc(ptr, size, XMEMORY_TYPE_HYBRID);
}
void* XCalloc_System(size_t count, size_t size)
{
	return XMemory_calloc(count, size, XMEMORY_TYPE_SYSTEM);
}
void* XCalloc_MultiPool(size_t count, size_t size)
{
	return XMemory_calloc(count, size, XMEMORY_TYPE_MULTIPOOL);
}
void* XCalloc_Hybrid(size_t count, size_t size)
{
	return  XMemory_calloc(count, size, XMEMORY_TYPE_HYBRID);
}
void XMemory_setMethod(const XMemory* method, XMemoryType type)
{
	if(method)
		global_Memory[type] = *method;
}

void XMemory_setMallocMethod(MallocMethod method, XMemoryType type)
{
	global_Memory[type].malloc = method;
}
void XMemory_setReallocMethod(ReallocMethod method, XMemoryType type)
{
	global_Memory[type].realloc = method;
}
void XMemory_setCallocMethod(CallocMethod method, XMemoryType type)
{
	global_Memory[type].calloc = method;
}

void XMemory_setFreeMethod(FreeMethod method, XMemoryType type)
{
	global_Memory[type].free = method;
}


bool XMemory_realloc_isNULL(XMemoryType type)
{
	return global_Memory[type].realloc==NULL;
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
void* XMemory_realloc_isMalloc(void* ptr, size_t size, XMemoryType type)
{
	if (ptr == NULL)
		return XMalloc(size, type);
	if (size == 0)
	{
		XFree(ptr, type);
		return NULL;
	}
	void* newPtr = XMalloc(size, type);
	if (newPtr == NULL)
		return NULL;
	memcpy(newPtr,ptr,size);
	XFree(ptr, type);
	return newPtr;
}

void* XMemory_calloc_isMalloc(size_t count, size_t size, XMemoryType type)
{
	void* ptr = XMalloc(count*size, type);
	if (ptr)
		memset(ptr,0,count*size);
	return ptr;
}

static void* hybrid_malloc(size_t size) {
	if (size <= HYBRID_THRESHOLD) {
		return XMalloc_MultiPool(size);
	}
	else {
		return XMalloc_System(size);
	}
}

static void hybrid_free(void* ptr)
{
	if (ptr == NULL) return;

	// 利用 XMultiPool_is_from_pool 来判断指针来源
	// 注意: 这里传入了全局池实例
	if (XMultiPool_is_from_pool(XMultiPool_global(), ptr)) {
		XFree_MultiPool(ptr);
	}
	else {
		XFree_System(ptr);
	}
}

static void* hybrid_realloc(void* ptr, size_t size)
{
	if (ptr == NULL) {
		return hybrid_malloc(size);
	}
	if (size == 0) {
		hybrid_free(ptr);
		return NULL;
	}

	XMultiPool* global_pool = XMultiPool_global();
	bool is_from_multipool = XMultiPool_is_from_pool(global_pool, ptr);

	if (is_from_multipool) {
		if (size <= HYBRID_THRESHOLD) {
			// 原块和新块都适合 XMultiPool
			return XMultiPool_global_realloc(ptr, size);
		}
		else {
			// 原块来自 XMultiPool，但新块太大，需要迁移到系统堆
			void* new_ptr = XMalloc_System(size);
			if (new_ptr != NULL) {
				// --- 关键修改：使用新的公开 API XMultiPool_get_size ---
				size_t old_user_size = XMultiPool_getMaxUserSize(global_pool, ptr);
				memcpy(new_ptr, ptr, (old_user_size < size) ? old_user_size : size);
				XFree_MultiPool(ptr);
			}
			return new_ptr;
		}
	}
	else {
		// 原块来自系统堆
		if (size <= HYBRID_THRESHOLD) {
			// 分配一个 XMultiPool 块并复制数据
			void* new_ptr = XMalloc_MultiPool(size);
			if (new_ptr != NULL) {
				// 对于系统堆的指针，无法知道确切大小，这是一个已知限制。
				memcpy(new_ptr, ptr, size);
				XFree_System(ptr);
			}
			return new_ptr;
		}
		else {
			// 原块和新块都适合系统堆
			return XRealloc_System(ptr, size);
		}
	}
}

static void* hybrid_calloc(size_t count, size_t size) {
	if (count == 0 || size == 0) return NULL;
	if (count > SIZE_MAX / size) return NULL; // 防止溢出

	size_t total_size = count * size;
	if (total_size <= HYBRID_THRESHOLD) {
		return XCalloc_MultiPool(count, size);
	}
	else {
		return XCalloc_System(count, size);
	}
}
