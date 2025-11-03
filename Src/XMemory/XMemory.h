#ifndef XMEMORY_H
#define XMEMORY_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include"CXinYueConfig.h"
/**
* @brief 内存申请函数指针类型
* @param size 申请的内存大小（字节数）
* @return 成功返回内存块指针，失败返回NULL
*/
typedef void* (*MallocMethod)(size_t size);
/**
* @brief 内存释放函数指针类型
* @param ptr 待释放的内存块指针
*/
typedef void (*DeleteMethod)(void* ptr);
/**
* @brief 内存重分配函数指针类型
* @param ptr 原内存块指针
* @param size 新的内存大小（字节数）
* @return 成功返回新内存块指针，失败返回NULL
*/
typedef void* (*ReallocMethod)(void* ptr, size_t size);
/**
* @brief 零初始化内存分配函数指针类型
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0），失败返回NULL
*/
typedef void* (*CallocMethod)(size_t count, size_t size);
/**
* @brief 内存管理方法结构体，封装各类内存操作函数
*/
typedef struct
{
	MallocMethod allocate;      ///< 内存申请函数
	DeleteMethod deallocate;    ///< 内存释放函数
	ReallocMethod reallocate;   ///< 内存重分配函数
	CallocMethod callocZero;    ///< 零初始化内存分配函数
} XMemory;
/**
* @brief 字节序枚举定义
*/
typedef enum
{
	XBYTE_ORDER_LITTLE_ENDIAN = 0,  ///< 小端序：低字节在前，高字节在后 (LE)
	XBYTE_ORDER_BIG_ENDIAN,        ///< 大端序：高字节在前，低字节在后 (BE)
	XBYTE_ORDER_NATIVE            ///< 本机字节序：使用当前系统的默认字节序
} XByteOrder;
/**
* @brief 比特存储顺序枚举（控制字节内比特的高低位存储模式）
*/
typedef enum {
	XBIT_ORDER_MSB_FIRST = 0,  ///< 高位在前（Most Significant Bit First）：字节的第7位为第一个比特
	XBIT_ORDER_LSB_FIRST,       ///< 低位在前（Least Significant Bit First）：字节的第0位为第一个比特
	XBIT_ORDER_DEFAULT          ///< 默认使用XBIT_ORDER_LSB_FIRST
} XBitOrder;
/**
* @brief 设置全局内存管理方法
* @param method 内存管理方法结构体指针，为NULL时不执行操作
*/
void XMemory_setMethod(const XMemory* method);
/**
* @brief 设置内存申请函数
* @param method 自定义的内存申请函数指针
*/
void XMemory_setMallocMethod(MallocMethod method);
/**
* @brief 设置内存释放函数
* @param method 自定义的内存释放函数指针
*/
void XMemory_setDeleteMethod(DeleteMethod method);
/**
* @brief 设置内存重分配函数
* @param method 自定义的内存重分配函数指针
*/
void XMemory_setReallocMethod(ReallocMethod method);
/**
* @brief 设置零初始化内存分配函数
* @param method 自定义的零初始化内存分配函数指针
*/
void XMemory_setCallocMethod(CallocMethod method);
/**
* @brief 使用malloc实现的内存重分配函数（XMemory_realloc的备选实现）
* @details 扩大内存时通过malloc+拷贝+free实现，存在数据拷贝隐患
* @param ptr 原内存块指针
* @param size 新的内存大小（字节数）
* @return 成功返回新内存块指针，失败返回NULL
*/
void* XMemory_realloc_isMalloc(void* ptr, size_t size);
/**
* @brief 使用malloc+memset实现的零初始化内存分配函数（XMemory_calloc的备选实现）
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0），失败返回NULL
*/
void* XMemory_calloc_isMalloc(size_t count, size_t size);
/**
* @brief 内存申请函数（调用全局配置的allocate方法）
* @param size 申请的内存大小（字节数）
* @return 成功返回内存块指针，失败返回NULL
*/
void* XMemory_malloc(size_t size);
/**
* @brief 内存释放函数（调用全局配置的deallocate方法）
* @param ptr 待释放的内存块指针
*/
void XMemory_free(void* ptr);
/**
* @brief 内存重分配函数（调用全局配置的reallocate方法）
* @param ptr 原内存块指针
* @param size 新的内存大小（字节数）
* @return 成功返回新内存块指针，失败返回NULL
*/
void* XMemory_realloc(void* ptr, size_t size);
/**
* @brief 零初始化内存分配函数（调用全局配置的callocZero方法）
* @param count 元素数量
* @param size 单个元素大小（字节数）
* @return 成功返回连续内存块指针（已初始化为0），失败返回NULL
*/
void* XMemory_calloc(size_t count, size_t size);
/**
* @brief 检查当前内存重分配函数是否为NULL
* @return true表示realloc方法未设置（为NULL），false表示已设置
*/
bool XMemory_realloc_isNULL();
/**
* @brief 将指定字节序的数据流读取到内存缓冲区，并根据字节序转换
* @details 用于跨平台/协议数据交互，自动处理字节序转换，确保内存数据符合当前系统字节序
* @param[in]  src        输入数据源指针（待读取的字节流）
* @param[in]  readOrder  输入数据的字节序（XBYTE_ORDER_LITTLE_ENDIAN / BIG_ENDIAN / NATIVE）
* @param[out] out        输出内存缓冲区（存储转换后的字节数据）
* @param[in]  size       数据长度（字节数，必须 >= 1，单字节数据无需转换）
* @return bool 转换成功返回true，参数无效（空指针或长度为0）返回false
*/
bool XMemory_read_data(const uint8_t* src, XByteOrder readOrder, uint8_t* out, size_t size);
/**
* @brief 将内存缓冲区数据按指定字节序写入数据流，并根据字节序转换
* @details 用于跨平台/协议数据交互，自动处理字节序转换，确保输出数据符合目标字节序
* @param[out] write      输出数据流指针（待写入的字节流）
* @param[in]  writeOrder 输出数据的字节序（XBYTE_ORDER_LITTLE_ENDIAN / BIG_ENDIAN / NATIVE）
* @param[in]  in         输入内存缓冲区（读取转换前的字节数据）
* @param[in]  size       数据长度（字节数，必须 >= 1，单字节数据无需转换）
* @return bool 转换成功返回true，参数无效（空指针或长度为0）返回false
*/
bool XMemory_write_data(uint8_t* write, XByteOrder writeOrder, const uint8_t* in, size_t size);
/**
* @brief 申请指定类型对象的内存（封装XMemory_malloc）
* @param obj 目标对象类型（如int、struct xxx等）
* @return 成功返回对应类型的内存块指针，失败返回NULL
*/
#define XNew(obj)                      XMemory_malloc(sizeof(obj))
/**
* @brief 释放内存（封装XMemory_free）
* @param ptr 待释放的内存块指针
*/
#define XDelete(ptr)                   XMemory_free(ptr);
/**
* @brief XMemory_malloc的宏别名
*/
#define XMalloc                        XMemory_malloc
/**
* @brief XMemory_free的宏别名
*/
#define XFree                          XMemory_free
/**
* @brief XMemory_realloc的宏别名
*/
#define XRealloc                       XMemory_realloc
/**
* @brief XMemory_calloc的宏别名
*/
#define XCalloc                        XMemory_calloc
/**
* @brief 从字节流读取数据并转换为指定类型变量（封装XMemory_read_data）
* @param src        输入数据源指针
* @param readOrder  输入数据的字节序
* @param varName    输出变量名
* @param varType    输出变量类型
*/
#define XMemory_Read_Var(src,readOrder,varName,varType) varType varName; XMemory_read_data(src,readOrder,&varName,sizeof(varType));
/**
* @brief 将内存数据转换为指定字节序并写入变量（封装XMemory_write_data）
* @param in         输入内存缓冲区
* @param writeOrder 输出数据的字节序
* @param varName    输出变量名
* @param varType    输出变量类型
*/
#define XMemory_Write_Var(in,writeOrder,varName,varType) varType varName; XMemory_write_data(&varName,writeOrder,in,sizeof(varType));
#ifdef __cplusplus
}
#endif
#endif // !XMEMORY_H