#ifndef XPAIR_H
#define XPAIR_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdint.h>
#include"XTypes.h"
/**
* @brief 用于组合两个不同类型数据的结构体
* @details 可将两个任意类型的数据封装为一个整体，存储数据的类型大小及数据指针，支持拷贝、移动等操作
*/
typedef struct XPair
{
	size_t m_firstTypeSize;  ///< 第一个数据的类型大小（字节）
	size_t m_secondTypeSize; ///< 第二个数据的类型大小（字节）
	void* m_first;           ///< 指向第一个数据的指针
	void* m_second;          ///< 指向第二个数据的指针
} XPair;
// 构造函数
/**
* @brief 创建XPair实例的宏（通过类型自动推导大小）
* @param firstType 第一个数据的类型
* @param secondType 第二个数据的类型
* @return 成功返回XPair指针，失败返回NULL
*/
#define XPair_Create(firstType,secondType) XPair_create(sizeof(firstType),sizeof(secondType))
/**
* @brief 创建XPair实例
* @param firstTypeSize 第一个数据的类型大小（字节）
* @param secondTypeSize 第二个数据的类型大小（字节）
* @return 成功返回XPair指针，类型大小为0时返回NULL
*/
XPair* XPair_create(const size_t firstTypeSize, const size_t secondTypeSize);
/**
* @brief 通过深拷贝创建XPair实例
* @param other 被拷贝的XPair实例
* @return 成功返回新的XPair指针，other为NULL时返回NULL
*/
XPair* XPair_create_copy(const XPair* other);
/**
* @brief 通过资源移动创建XPair实例（转移源实例的资源所有权）
* @param other 被移动的XPair实例
* @return 成功返回新的XPair指针，other为NULL时返回NULL
*/
XPair* XPair_create_move(XPair* other);
// 拷贝与移动操作
/**
* @brief 深拷贝XPair数据
* @param this_pair 目标XPair实例（接收拷贝数据）
* @param copy 源XPair实例（被拷贝数据）
* @details 仅当两个实例的类型大小匹配时才执行拷贝，否则不操作
*/
void XPair_copy(XPair* this_pair, const XPair* copy);
/**
* @brief 移动XPair资源（转移所有权）
* @param this_pair 目标XPair实例（接收资源）
* @param move 源XPair实例（转移资源）
* @details 仅当两个实例的类型大小匹配时才执行移动，移动后源实例数据清零
*/
void XPair_move(XPair* this_pair, XPair* move);
// 数据插入操作
/**
* @brief 插入两个数据到XPair的宏（自动取地址）
* @param this_pair 目标XPair实例
* @param firstData 第一个数据（值）
* @param secondData 第二个数据（值）
*/
#define XPair_Insert(this_pair,firstData,secondData) XPair_insert(this_pair,&firstData,&secondData)
/**
* @brief 插入两个数据到XPair
* @param this_pair 目标XPair实例
* @param firstData 第一个数据的指针（可为NULL，此时清零第一个数据）
* @param secondData 第二个数据的指针（可为NULL，此时清零第二个数据）
*/
void XPair_insert(XPair* this_pair, void* firstData, void* secondData);
/**
* @brief 插入第一个数据到XPair的宏（自动取地址）
* @param this_pair 目标XPair实例
* @param firstData 第一个数据（值）
*/
#define XPair_InsertFirst(this_pair,firstData) XPair_insertFirst(this_pair,&firstData)
/**
* @brief 插入第一个数据到XPair
* @param this_pair 目标XPair实例
* @param firstData 第一个数据的指针（可为NULL，此时清零第一个数据）
*/
void XPair_insertFirst(XPair* this_pair, void* firstData);
/**
* @brief 插入第二个数据到XPair的宏（自动取地址）
* @param this_pair 目标XPair实例
* @param secondData 第二个数据（值）
*/
#define XPair_InsertSecond(this_pair,secondData) XPair_insertSecond(this_pair,&secondData)
/**
* @brief 插入第二个数据到XPair
* @param this_pair 目标XPair实例
* @param secondData 第二个数据的指针（可为NULL，此时清零第二个数据）
*/
void XPair_insertSecond(XPair* this_pair, void* secondData);
// 数据获取操作
/**
* @brief 获取第一个数据的宏（自动转换类型）
* @param this_pair 目标XPair实例
* @param firstType 第一个数据的类型
* @return 第一个数据的值（类型为firstType）
*/
#define XPair_First(this_pair,firstType) (*(firstType*)XPair_first(this_pair))
/**
* @brief 获取第一个数据的指针
* @param this_pair 目标XPair实例
* @return 成功返回第一个数据的指针，this_pair为NULL时返回NULL
*/
void* XPair_first(XPair* this_pair);
/**
* @brief 获取第二个数据的宏（自动转换类型）
* @param this_pair 目标XPair实例
* @param secondType 第二个数据的类型
* @return 第二个数据的值（类型为secondType）
*/
#define XPair_Second(this_pair,secondType) (*(secondType*)XPair_second(this_pair))
/**
* @brief 获取第二个数据的指针
* @param this_pair 目标XPair实例
* @return 成功返回第二个数据的指针，this_pair为NULL时返回NULL
*/
void* XPair_second(XPair* this_pair);
// 辅助操作
/**
* @brief 获取XPair实例的总大小（字节）
* @param this_pair 目标XPair实例
* @return 返回包含类型信息和数据的总大小
*/
size_t XPair_getSize(XPair* this_pair);
/**
* @brief 释放XPair实例占用的内存
* @param this_pair 待释放的XPair实例（可为NULL，此时不操作）
*/
void XPair_delete(XPair* this_pair);
/**
* @brief 比较两个XPair实例是否相等
* @param lhs 左侧XPair实例
* @param rhs 右侧XPair实例
* @return 相等返回XCompare_Equality，否则返回XCompare_Other
*/
int32_t XPair_compare(const XPair* lhs, const XPair* rhs);
#ifdef __cplusplus
}
#endif
#endif