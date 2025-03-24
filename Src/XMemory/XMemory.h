#ifndef XMEMORY_H
#define XMEMORY_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdint.h>
#include"XDataStructConfig.h"
typedef void* (*MallocMethod)(size_t size);
typedef void* (*FreeMethod)(void* pointer);
typedef void* (*ReallocMethod)(void* pointer, size_t size);
//内存管理
typedef struct
{
	MallocMethod allocate;//申请内存
	FreeMethod deallocate;//释放内存
	ReallocMethod reallocate;//重新申请内存扩容
} XMemory;
void XMemory_setMethod(const XMemory* method);
void XMemory_setMallocMethod(MallocMethod method);
void XMemory_setFreeMethod(FreeMethod method);
void XMemory_setReallocMethod(ReallocMethod method);
//XMemory_malloc XMemory_free配合实现XMemory_realloc  扩大内存拷贝时有一定隐患
void* XMemory_reallocPack(void* pointer, size_t size);
//内存管理
void* XMemory_malloc(size_t size);
void XMemory_free(void* pointer);
void* XMemory_realloc(void* pointer, size_t size);
#ifdef __cplusplus
}
#endif
#endif // !XMemory_H
