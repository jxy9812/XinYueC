/**
* @file XListBase_iterator.h
* @brief XListBase迭代器头文件
* @details 定义XListBase容器的迭代器结构，为XListDLinked、XListSLinked等派生类提供统一的迭代器基础
*/
#include"CXinYueConfig.h"
#if !defined(XLISTBASE_ITERATOR_ITERATOR_H)
#define XLISTBASE_ITERATOR_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XContainer_iterator.h"

/**
* @brief 声明XListBase类型
*/
XContainerTypeDeclare(XListBase);

/**
* @brief XListBase迭代器结构体
* @details 用于遍历链表容器中的元素，基于节点指针实现
*/
typedef struct XListBase_iterator
{
    void* node;   ///< 当前节点指针（指向链表节点）
} XListBase_iterator;

//XListBase_iterator XListBase_begin(XListBase* this_set);
//XListBase_iterator XListBase_end(XListBase* this_set);
//bool XListBase_iterator_isEnd(const XListBase_iterator* it);
//void XListBase_iterator_add(XListBase* this_set, XListBase_iterator* it);
//bool XListBase_iterator_equality(XListBase_iterator* itFirst, XListBase_iterator* itSecond);
//void XListBase_iterator_for_each(XListBase* this_set, XFor_each ForFunction, void* argList);
//void* XListBase_iterator_data(XListBase_iterator* it);

#ifdef __cplusplus
}
#endif
#endif