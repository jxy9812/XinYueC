#ifndef XVARLIST_H
#define XVARLIST_H
#ifdef __cplusplus
extern "C" {
#endif
#include"CXinYueConfig.h"
#include"XTypes.h"
#include"XChar.h"
#include<stdio.h>
#include<stdint.h>
// 核心原理：
// 1. 用匿名数组初始化参数列表：{0, __VA_ARGS__}
// 2. 数组元素个数 = 1（初始0） + 参数个数
// 3. 参数个数 = 数组长度 / 单个元素长度 - 1
#define COUNT_ARGS(...) \
    (sizeof((int[]){0, __VA_ARGS__}) / sizeof(int) - 1)
#define XVar(type,var)   sizeof(type),&var
//变量列表,相比XVariantList 更轻量级 
typedef struct XVarList
{
	uint8_t* ptr;//指针
	void* data;
}XVarList;
#define XVarList_Create(...)     XVarList_create(COUNT_ARGS(__VA_ARGS__),__VA_ARGS__);
XVarList* XVarList_create(uint8_t count,...);
#define XVarList_delete			XMemory_free
//初始化指针指向开头
#define XVarList_start(list)	*((uint8_t**)list) = list+sizeof(uint8_t*)
//获取当前指向的参数指针
#define XVarList_argPtr(list)	*((uint8_t**)list)
//向后偏移
#define XVarList_argOffset(list,type) XVarList_argPtr(list)+=sizeof(type)
//获取变量并向后偏移
#define XVarList_arg(list,type)	*((type*)XVarList_argPtr(list));XVarList_argOffset(list,type)
#ifdef __cplusplus
}
#endif	
#endif