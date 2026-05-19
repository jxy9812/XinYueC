/**
* @file XListSLinked_iterator.h
* @brief XListSLinked正向迭代器头文件
* @details 定义XListSLinked容器的正向迭代器类型及相关操作函数，用于从前向后遍历单向链表中的元素
*/
#include"CXinYueConfig.h"
#if !defined(XLISTSLINKED_ITERATOR_H)&& XListSLinked_ON
#define XLISTSLINKED_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XListBase_iterator.h"

/**
* @brief 声明XListSLinked类型
*/
XContainerTypeDeclare(XListSLinked);

/**
* @brief 声明XListSNode类型
*/
XContainerTypeDeclare(XListSNode);

/**
* @brief XListSLinked正向迭代器类型定义
* @details 复用XListBase_iterator的实现，用于遍历XListSLinked中的元素
*/
typedef XListBase_iterator XListSLinked_iterator;

/**
* @brief 获取指向XListSLinked第一个元素的迭代器
* @param this_list XListSLinked实例指针
* @return 指向第一个元素的迭代器，若链表为空则返回结束迭代器
*/
XListSLinked_iterator XListSLinked_begin(XListSLinked* this_list);

/**
* @brief 获取XListSLinked的结束迭代器（哨兵位置）
* @param this_list XListSLinked实例指针
* @return 结束迭代器（指向链表末尾的下一个位置）
*/
XListSLinked_iterator XListSLinked_end(XListSLinked* this_list);

/**
* @brief 判断迭代器是否已到达末尾
* @param it 迭代器指针
* @return 若迭代器已到达末尾返回true，否则返回false
*/
bool XListSLinked_iterator_isEnd(const XListSLinked_iterator* it);

/**
* @brief 将迭代器移动到下一个元素位置
* @param this_list XListSLinked实例指针
* @param it 迭代器指针（会被修改）
*/
void XListSLinked_iterator_add(XListSLinked* this_list,XListSLinked_iterator*it);

/**
* @brief 判断两个迭代器是否相等
* @param itFirst 第一个迭代器指针
* @param itSecond 第二个迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XListSLinked_iterator_equality(XListSLinked_iterator* itFirst, XListSLinked_iterator* itSecond);

/**
* @brief 使用回调函数遍历XListSLinked中的所有元素
* @param this_list XListSLinked实例指针
* @param ForFunction 对每个元素调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XListSLinked_iterator_for_each(XListSLinked* this_list, XFor_each ForFunction, void* args);

/**
* @brief 获取迭代器当前指向的元素数据指针
* @param it 迭代器指针
* @return 指向当前元素的void*指针，需由用户转换为实际类型
*/
void* XListSLinked_iterator_data(XListSLinked_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
