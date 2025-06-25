#ifndef XMEMORY_H
#define XMEMORY_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XDataStructConfig.h"
typedef void* (*MallocMethod)(size_t size);
typedef void (*DeleteMethod)(void* pointer);
typedef void* (*ReallocMethod)(void* pointer, size_t size);
typedef void* (*CallocMethod)(size_t count, size_t size);
//内存管理
typedef struct
{
	MallocMethod allocate;//申请内存
	DeleteMethod deallocate;//释放内存
	ReallocMethod reallocate;//重新申请内存扩容
	CallocMethod  callocZero;//分配指定数量和大小的连续内存块，并将所有字节初始化为 0
} XMemory;
void XMemory_setMethod(const XMemory* method);
void XMemory_setMallocMethod(MallocMethod method);
void XMemory_setDeleteMethod(DeleteMethod method);
void XMemory_setReallocMethod(ReallocMethod method);
void XMemory_setCallocMethod(CallocMethod method);
//XMemory_malloc XMemory_free配合实现XMemory_realloc  扩大内存拷贝时有一定隐患
void* XMemory_reallocPack(void* pointer, size_t size);
void* XMemory_callocPack(size_t count, size_t size);
//内存管理
void* XMemory_malloc(size_t size);
void XMemory_free(void* pointer);
void* XMemory_realloc(void* pointer, size_t size);
void* XMemory_calloc(size_t count, size_t size);
//realloc是否是空
bool XMemory_realloc_isNULL();
#ifdef __cplusplus
}
#endif
#endif // !XMemory_H
