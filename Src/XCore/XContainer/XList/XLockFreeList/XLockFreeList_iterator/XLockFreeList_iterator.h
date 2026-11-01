/**
* @file XLockFreeList_iterator.h
* @brief XLockFreeList正向迭代器头文件
* @details 定义XLockFreeList容器的正向迭代器类型及相关操作函数，用于遍历无锁链表中的元素
*/
#include"CXinYueConfig.h"
#if !defined(XLockFreeList_ITERATOR_H) && XLockFreeList_ON
#define XLockFreeList_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XListBase_iterator.h"

/**
* @brief 声明XLockFreeList类型
*/
XContainerTypeDeclare(XLockFreeList);

/**
* @brief 声明XLockFreeListNode类型
*/
XContainerTypeDeclare(XLockFreeListNode);

/**
* @brief XLockFreeList正向迭代器类型定义
* @details 复用XListBase_iterator的实现，用于遍历XLockFreeList中的元素
*/
typedef XListBase_iterator XLockFreeList_iterator;

/**
* @brief 获取指向XLockFreeList第一个元素的迭代器
* @param this_list XLockFreeList实例指针
* @return 指向第一个元素的迭代器，若链表为空则返回结束迭代器
*/
XLockFreeList_iterator XLockFreeList_begin(XLockFreeList* this_list);

/**
* @brief 获取XLockFreeList的结束迭代器（哨兵位置）
* @param this_list XLockFreeList实例指针
* @return 结束迭代器（指向链表末尾的下一个位置）
*/
XLockFreeList_iterator XLockFreeList_end(XLockFreeList* this_list);

/**
* @brief 判断迭代器是否已到达末尾
* @param it 迭代器指针
* @return 若迭代器已到达末尾返回true，否则返回false
*/
bool XLockFreeList_iterator_isEnd(const XLockFreeList_iterator* it);

/**
* @brief 将迭代器移动到下一个元素位置
* @param this_list XLockFreeList实例指针
* @param it 迭代器指针（会被修改）
*/
void XLockFreeList_iterator_add(XLockFreeList* this_list, XLockFreeList_iterator* it);

/**
* @brief 判断两个迭代器是否相等
* @param itFirst 第一个迭代器指针
* @param itSecond 第二个迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XLockFreeList_iterator_equality(XLockFreeList_iterator* itFirst, XLockFreeList_iterator* itSecond);

/**
* @brief 使用回调函数遍历XLockFreeList中的所有元素
* @param this_list XLockFreeList实例指针
* @param ForFunction 对每个元素调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XLockFreeList_iterator_for_each(XLockFreeList* this_list, XFor_each ForFunction, void* args);

/**
* @brief 获取迭代器当前指向的元素数据指针
* @param it 迭代器指针
* @return 指向当前元素的void*指针，需由用户转换为实际类型
*/
void* XLockFreeList_iterator_data(XLockFreeList_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // ! XLockFreeList_ITERATOR_H