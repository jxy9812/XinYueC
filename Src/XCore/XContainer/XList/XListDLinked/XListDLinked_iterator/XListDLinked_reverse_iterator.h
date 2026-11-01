/**
* @file XListDLinked_reverse_iterator.h
* @brief XListDLinked反向迭代器头文件
* @details 定义XListDLinked容器的反向迭代器类型及相关操作函数，用于从后向前遍历双向链表中的元素
*/
#include"CXinYueConfig.h"
#if !defined(XLIST_REVERSE_ITERATOR_H)&& XListDLinked_ON
#define XLIST_REVERSE_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XListBase_iterator.h"

/**
* @brief 声明XListDLinked类型
*/
XContainerTypeDeclare(XListDLinked);

/**
* @brief 声明XListDNode类型
*/
XContainerTypeDeclare(XListDNode);

/**
* @brief XListDLinked反向迭代器类型定义
* @details 复用XListBase_iterator的实现，用于反向遍历XListDLinked中的元素
*/
typedef XListBase_iterator XListDLinked_reverse_iterator;

/**
* @brief 获取指向XListDLinked最后一个元素的反向迭代器
* @param this_list XListDLinked实例指针
* @return 指向最后一个元素的反向迭代器，若链表为空则返回结束迭代器
*/
XListDLinked_reverse_iterator XListDLinked_rbegin(XListDLinked* this_list);

/**
* @brief 获取XListDLinked的反向结束迭代器（哨兵位置）
* @param this_list XListDLinked实例指针
* @return 反向结束迭代器（指向链表第一个元素的前一个位置）
*/
XListDLinked_reverse_iterator XListDLinked_rend(XListDLinked* this_list);

/**
* @brief 判断反向迭代器是否已到达末尾
* @param it 反向迭代器指针
* @return 若反向迭代器已到达开头返回true，否则返回false
*/
bool XListDLinked_reverse_iterator_isEnd(const XListDLinked_reverse_iterator* it);

/**
* @brief 将反向迭代器移动到前一个元素位置
* @param this_list XListDLinked实例指针
* @param it 反向迭代器指针（会被修改）
*/
void XListDLinked_reverse_iterator_add(XListDLinked* this_list, XListDLinked_reverse_iterator* it);

/**
* @brief 判断两个反向迭代器是否相等
* @param itFirst 第一个反向迭代器指针
* @param itSecond 第二个反向迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XListDLinked_reverse_iterator_equality(XListDLinked_reverse_iterator* itFirst, XListDLinked_reverse_iterator* itSecond);

/**
* @brief 使用回调函数反向遍历XListDLinked中的所有元素
* @param this_list XListDLinked实例指针
* @param ForFunction 对每个元素调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XListDLinked_reverse_iterator_for_each(XListDLinked* this_list, XFor_each ForFunction, void* args);

/**
* @brief 获取反向迭代器当前指向的元素数据指针
* @param it 反向迭代器指针
* @return 指向当前元素的void*指针，需由用户转换为实际类型
*/
void* XListDLinked_reverse_iterator_data(XListDLinked_reverse_iterator* it);

#ifdef __cplusplus
}
#endif
#endif // !REVERSE_ITERATOR_H