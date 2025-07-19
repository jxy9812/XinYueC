#include"XMemory.h"
#include"string.h"
#ifdef _WIN32
#include<stdlib.h>
static XMemory global_Memory = { malloc,free,realloc,calloc };
#elif defined(__linux__)
#include<stdlib.h>
static XMemory global_Memory = { malloc,free,realloc,calloc };
#elif defined(__APPLE__) && defined(__MACH__)
#include<stdlib.h>
static XMemory global_Memory = { malloc,free,realloc,calloc };
#elif defined(configUSE_FREERTOS) 
#include"FreeRTOS.h"
static XMemory global_Memory = { pvPortMalloc,vPortFree,XMemory_reallocPack,XMemory_callocPack };
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

void XMemory_setReallocMethod(ReallocMethod method)
{
	global_Memory.reallocate = method;
}

void XMemory_setCallocMethod(CallocMethod method)
{
	global_Memory.callocZero = method;
}

void* XMemory_reallocPack(void* ptr, size_t size)
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

void* XMemory_callocPack(size_t count, size_t size)
{
	void* ptr = XMemory_malloc(count*size);
	if (ptr)
		memset(ptr,0,count*size);
	return ptr;
}
