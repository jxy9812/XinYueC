#include"XMemory.h"
#include"string.h"
#ifdef _WIN32
#include<stdlib.h>
static XMemory global_Memory = { malloc,free,realloc };
#elif defined(__linux__)
#include<stdlib.h>
static XMemory global_Memory = { malloc,free,realloc };
#elif defined(__APPLE__) && defined(__MACH__)
#include<stdlib.h>
static XMemory global_Memory = { malloc,free,realloc };
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

void XMemory_free(void* pointer)
{
	global_Memory.deallocate(pointer);
}

void XMemory_setFreeMethod(FreeMethod method)
{
	global_Memory.deallocate = method;
}

void* XMemory_realloc(void* pointer, size_t size)
{
	return global_Memory.reallocate(pointer,size);
}

void XMemory_setReallocMethod(ReallocMethod method)
{
	global_Memory.reallocate = method;
}

void* XMemory_reallocPack(void* pointer, size_t size)
{
	if(pointer==NULL)
		return NULL;
	if (size == 0)
	{
		XMemory_free(pointer);
		return NULL;
	}
	void* ptr = XMemory_malloc(size);
	if (ptr == NULL)
		return NULL;
	memcpy(ptr,pointer,size);
	XMemory_free(pointer);
	return ptr;
}
