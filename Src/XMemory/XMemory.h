#ifndef XMEMORY_H
#define XMEMORY_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"CXinYueConfig.h"
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
void* XMemory_realloc_isMalloc(void* ptr, size_t size);
void* XMemory_calloc_isMalloc(size_t count, size_t size);

//内存管理
#define XNew(obj)						XMemory_malloc(sizeof(obj))
#define XDelete(ptr)					XMemory_free(ptr);
#define XMalloc							XMemory_malloc
#define XFree							XMemory_free
#define XRealloc						XMemory_realloc
#define XCalloc							XMemory_calloc
void* XMemory_malloc(size_t size);
void XMemory_free(void* ptr);
void* XMemory_realloc(void* ptr, size_t size);
void* XMemory_calloc(size_t count, size_t size);
//realloc是否是空
bool XMemory_realloc_isNULL();
//内存字节顺序
typedef enum
{
	XBYTE_ORDER_LITTLE_ENDIAN = 0,  /**< 小端序：低字节在前，高字节在后 (LE) */
	XBYTE_ORDER_BIG_ENDIAN,        /**< 大端序：高字节在前，低字节在后 (BE) */
	XBYTE_ORDER_NATIVE            /**< 本机字节序：使用当前系统的默认字节序 */
}XByteOrder;
/*                比特存储顺序配置                          */
/**
 * @brief 比特存储顺序枚举（控制字节内比特的高低位存储模式）
 */
typedef enum {
	XBIT_ORDER_MSB_FIRST = 0,  /**< 高位在前（Most Significant Bit First）：字节的第7位为第一个比特 */
	XBIT_ORDER_LSB_FIRST,       /**< 低位在前（Least Significant Bit First）：字节的第0位为第一个比特 */
	XBIT_ORDER_DEFAULT  //默认 XBIT_ORDER_LSB_FIRST
} XBitOrder;
/**
 * @brief 将指定字节序的数据流按据到内存缓冲区，并根据字节序转换
 *
 * 该函数用于处理跨平台/协议间的数据交互，当数据源的字节序与当前系统字节序不一致时，
 * 自动进行字节顺序反转，确保内存中的数据符合当前系统的字节序格式。
 *
 * @param[in]  src        输入数据源指针（待读取的字节流）
 * @param[in]  readOrder  输入数据的字节序（XMEMORY_BYTE_ORDER_LITTLE_ENDIAN / BIG_ENDIAN / NATIVE）
 * @param[out] out		  输出内存缓冲区（存储转换后的字节数据）
 * @param[in]  size       数据长度（字节数，必须 >= 1，单字节数据无需转换）
 *
 * @return bool
 *         - true：转换成功（含无需转换的情况）
 *         - false：参数无效（空指针或长度为0）
 */
bool XMemory_read_data(const uint8_t* src, XByteOrder readOrder, uint8_t* out,size_t size);

/**
 * @brief 内存缓冲区保存指定字节序的数据流，并根据字节序转换
 *
 * 该函数用于处理跨平台/协议间的数据交互，当数据源的字节序与当前系统字节序不一致时，
 * 自动进行字节顺序反转，确保内存中的数据符合当前系统的字节序格式。
 *
 * @param[out]  write       输出数据源指针（待写入的字节流）
 * @param[out]  writeOrder  输出数据的字节序（XMEMORY_BYTE_ORDER_LITTLE_ENDIAN / BIG_ENDIAN / NATIVE）
 * @param[in]	in			输入内存缓冲区（读取转换前的字节数据）
 * @param[out]  size        数据长度（字节数，必须 >= 1，单字节数据无需转换）
 *
 * @return bool
 *         - true：转换成功（含无需转换的情况）
 *         - false：参数无效（空指针或长度为0）
 */
bool XMemory_write_data(uint8_t* write, XByteOrder writeOrder,const uint8_t* in, size_t size);
#define XMemory_Read_Var(src,readOrder,varName,varType) varType varName; XMemory_read_data(src,readOrder,&varName,sizeof(varType));
#define XMemory_Write_Var(in,writeOrder,varName,varType) varType varName; XMemory_write_data(&varName,writeOrder,in,sizeof(varType));
#ifdef __cplusplus
}
#endif
#endif // !XMemory_H
