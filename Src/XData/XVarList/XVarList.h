#ifndef XVARLIST_H
#define XVARLIST_H
#ifdef __cplusplus
extern "C" {
#endif
#include "CXinYueConfig.h"
#include "XTypes.h"
#include "XChar.h"
#include <stdio.h>
#include <stdint.h>
/**
* @brief 计算可变参数的数量
* 核心原理：
* 1. 用匿名数组初始化参数列表：{0, __VA_ARGS__}
* 2. 数组元素个数 = 1（初始0） + 参数个数
* 3. 参数个数 = 数组长度 / 单个元素长度 - 1
*/
#define COUNT_ARGS(...) \
(sizeof((int[]){0, __VA_ARGS__}) / sizeof(int) - 1)
/**
* @brief 包装变量的类型大小和地址，用于构建XVarList的参数列表
* @param type 变量的数据类型
* @param var 变量实例
* @return 包含类型大小和变量地址的组合，用于XVarList创建时的参数传递
*/
#define XVar(type, var)   sizeof(type), &var
/**
* @brief 轻量级变量列表结构，相比XVariantList更简洁
* 用于存储一系列不同类型的变量，通过指针偏移实现元素访问
*/
typedef struct XVarList
{
	uint8_t* ptr;  ///< 当前访问的指针位置，用于遍历元素
    void(*del)(struct XVarList*);//释放方法
    void* data;    ///< 存储变量数据的起始地址
} XVarList;
/**
* @brief 创建XVarList实例，自动计算参数数量
* 需配合XVar宏使用，参数格式为XVar(type1, var1), XVar(type2, var2), ...
* @param ... 由XVar宏包装的参数列表
* @return 新创建的XVarList实例，失败返回NULL
*/
#define XVarList_Create(...)     XVarList_create(COUNT_ARGS(__VA_ARGS__), __VA_ARGS__);
/**
* @brief 释放XVarList实例占用的内存
* 本质是调用XMemory_free，用于统一内存释放接口
*/
void  XVarList_delete(XVarList*list);
//设置参数删除函数
#define XVarList_setArgsDel(list,d)       (((XVarList*)list)->del = d)
//参数删除函数
#define XVarList_argsDel(list)            ((XVarList*)list)->del
/**
* @brief 初始化XVarList的指针，使其指向数据起始位置
* 将ptr成员设置为数据区域的起始地址（跳过内部指针存储区）
* @param list XVarList实例指针
*/
#define XVarList_start(list)     *((uint8_t**)list) = (uint8_t*)list + sizeof(uint8_t*)*2
/**
* @brief 获取当前指针指向的参数地址
* @param list XVarList实例指针
* @return 当前指针指向的参数的地址
*/
#define XVarList_argPtr(list)    *((uint8_t**)list)
/**
* @brief 将当前指针向后偏移指定类型大小的字节数
* 用于访问下一个元素前的指针调整
* @param list XVarList实例指针
* @param type 要偏移的类型（决定偏移字节数）
*/
#define XVarList_argOffset(list, type) XVarList_argPtr(list) += sizeof(type)
/**
* @brief 获取当前指针指向的指定类型变量，并将指针向后偏移
* 先获取当前位置的变量值，再调整指针到下一个元素位置
* @param list XVarList实例指针
* @param type 要获取的变量类型
* @return 当前指针指向的指定类型变量值
*/
#define XVarList_arg(list, type) *((type*)XVarList_argPtr(list)); XVarList_argOffset(list, type)
//#define XVarList_arg(list, type) \
//    ( \
//        *((type*)XVarList_argPtr(list)), \
//        (XVarList_argPtr(list) += sizeof(type)), \
//        *((type*)(XVarList_argPtr(list) - sizeof(type))) \
//    )
/**
* @brief 创建XVarList实例
* 接收参数数量和由XVar宏包装的参数列表，内部分配内存并拷贝数据
* @param count 参数数量（需为偶数，因每个变量由类型大小和地址组成）
* @param ... 由XVar宏包装的参数列表（格式：类型大小1, 变量地址1, 类型大小2, 变量地址2, ...）
* @return 新创建的XVarList实例，失败返回NULL
*/
XVarList* XVarList_create(uint8_t count, ...);

// 现在，所有 XVarList_args_N 宏都变得极其简单和一致
#define XVarList_args_0(list)

#define XVarList_args_1(list, type1, name1) \
    XVarList_start(list);\
    type1 name1 = XVarList_arg(list, type1)

#define XVarList_args_2(list, type1, name1, type2, name2) \
    XVarList_args_1(list,type1, name1);\
    type2 name2 = XVarList_arg(list, type2)

#define XVarList_args_3(list, type1, name1, type2, name2, type3, name3) \
    XVarList_args_2(list, type1, name1, type2, name2);\
    type3 name3 = XVarList_arg(list, type3)

#define XVarList_args_4(list, type1, name1, type2, name2, type3, name3, type4, name4) \
    XVarList_args_3(list, type1, name1, type2, name2, type3, name3);\
    type4 name4 = XVarList_arg(list, type4)

#define XVarList_args_5(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5) \
    XVarList_args_4(list, type1, name1, type2, name2, type3, name3, type4, name4); \
    type5 name5 = XVarList_arg(list, type5)

// 6 对参数
#define XVarList_args_6(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6) \
    XVarList_args_5(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5); \
    type6 name6 = XVarList_arg(list, type6)

// 7 对参数
#define XVarList_args_7(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7) \
    XVarList_args_6(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6); \
    type7 name7 = XVarList_arg(list, type7)

// 8 对参数
#define XVarList_args_8(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7, type8, name8) \
    XVarList_args_7(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7); \
    type8 name8 = XVarList_arg(list, type8)

// 9 对参数
#define XVarList_args_9(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7, type8, name8, type9, name9) \
    XVarList_args_8(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7, type8, name8); \
    type9 name9 = XVarList_arg(list, type9)

// 10 对参数
#define XVarList_args_10(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7, type8, name8, type9, name9, type10, name10) \
    XVarList_args_9(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7, type8, name8, type9, name9); \
    type10 name10 = XVarList_arg(list, type10)
#ifdef __cplusplus
}
#endif
#endif