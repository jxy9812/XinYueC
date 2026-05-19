/**
* @file XMap_iterator.h
* @brief XMap正向迭代器头文件
* @details 定义XMap容器的正向迭代器类型及相关操作函数，用于从前向后遍历映射中的键值对
*/
#include"CXinYueConfig.h"
#if !defined(XMAP_ITERATOR_H)&& XMap_ON
#define XMAP_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XMapBase_iterator.h"

/**
* @brief 声明XMap类型
*/
XContainerTypeDeclare(XMap);

/**
* @brief XMap正向迭代器类型定义
* @details 复用XMapBase_iterator的实现，用于遍历XMap中的键值对
*/
typedef  XMapBase_iterator  XMap_iterator;

/**
* @brief 获取指向XMap第一个元素的迭代器
* @param this_map XMap实例指针
* @return 指向第一个键值对的迭代器，若映射为空则返回结束迭代器
*/
XMap_iterator XMap_begin(XMap* this_map);

/**
* @brief 获取XMap的结束迭代器（哨兵位置）
* @param this_map XMap实例指针
* @return 结束迭代器（指向映射末尾的下一个位置）
*/
XMap_iterator XMap_end(XMap* this_map);

/**
* @brief 判断迭代器是否已到达末尾
* @param it 迭代器指针
* @return 若迭代器已到达末尾返回true，否则返回false
*/
bool XMap_iterator_isEnd(const XMap_iterator* it);

/**
* @brief 将迭代器移动到下一个元素位置
* @param this_map XMap实例指针
* @param it 迭代器指针（会被修改）
*/
void XMap_iterator_add(XMap* this_map, XMap_iterator* it);

/**
* @brief 判断两个迭代器是否相等
* @param itFirst 第一个迭代器指针
* @param itSecond 第二个迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XMap_iterator_equality(XMap_iterator* itFirst, XMap_iterator* itSecond);

/**
* @brief 使用回调函数遍历XMap中的所有键值对
* @param this_map XMap实例指针
* @param ForFunction 对每个键值对调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XMap_iterator_for_each(XMap* this_map, XFor_each ForFunction, void* args);

/**
* @brief 获取迭代器当前指向的键值对数据指针
* @param it 迭代器指针
* @return 指向当前键值对的XPair指针，可通过XPair_first/XPair_second获取键和值
*/
XPair* XMap_iterator_data(XMap_iterator* it);

#ifdef __cplusplus
}
#endif
#endif
