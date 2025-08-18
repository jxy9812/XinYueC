#ifndef XMEMORY_H
#define XMEMORY_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"XDataStructConfig.h"
typedef void* (*MallocMethod)(size_t size);
typedef void (*DeleteMethod)(void* ptr);
typedef void* (*ReallocMethod)(void* ptr, size_t size);
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
void* XMemory_reallocPack(void* ptr, size_t size);
void* XMemory_callocPack(size_t count, size_t size);
//内存管理
void* XMemory_malloc(size_t size);
void XMemory_free(void* ptr);
void* XMemory_realloc(void* ptr, size_t size);
void* XMemory_calloc(size_t count, size_t size);
//realloc是否是空
bool XMemory_realloc_isNULL();
//内存字节顺序
typedef enum
{
	XMEMORY_BYTE_ORDER_LITTLE_ENDIAN = 0,  /**< 小端序：低字节在前，高字节在后 (LE) */
	XMEMORY_BYTE_ORDER_BIG_ENDIAN,        /**< 大端序：高字节在前，低字节在后 (BE) */
	XMEMORY_BYTE_ORDER_NATIVE            /**< 本机字节序：使用当前系统的默认字节序 */
}XMemoryByteOrder;
/**
 * @brief 将指定字节序的数据流按据到内存缓冲区，并根据字节序转换
 *
 * 该函数用于处理跨平台/协议间的数据交互，当数据源的字节序与当前系统字节序不一致时，
 * 自动进行字节顺序反转，确保内存中的数据符合当前系统的字节序格式。
 *
 * @param[in]  read       输入数据源指针（待读取的字节流）
 * @param[in]  readOrder  输入数据的字节序（XMEMORY_BYTE_ORDER_LITTLE_ENDIAN / BIG_ENDIAN / NATIVE）
 * @param[out] memBuff    输出内存缓冲区（存储转换后的字节数据）
 * @param[in]  size       数据长度（字节数，必须 >= 1，单字节数据无需转换）
 *
 * @return bool
 *         - true：转换成功（含无需转换的情况）
 *         - false：参数无效（空指针或长度为0）
 */
bool XMemory_read_data(const uint8_t* read, XMemoryByteOrder readOrder, uint8_t* memBuff,size_t size);
#ifdef __cplusplus
}
#endif
#endif // !XMemory_H
