/**
* @file XHashMap_iterator.h
* @brief XHashMap正向迭代器头文件
* @details 定义XHashMap容器的正向迭代器结构及相关操作函数，用于从前向后遍历哈希映射中的键值对
*/
#include"CXinYueConfig.h"
#if !defined(XHASHMAP_ITERATOR_H)&& XHashMap_ON
#define XHASHMAP_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XMapBase_iterator.h"

/**
* @brief 声明XHashMap类型
*/
XContainerTypeDeclare(XHashMap);

/**
* @brief XHashMap正向迭代器结构体
* @details 用于遍历XHashMap中的键值对，基于节点指针和桶索引实现
*/
typedef struct XHashMap_iterator
{
    union ///< 联合体，node和parent实际指向同一内存位置
    {
        void* node;             ///< 当前节点指针（指向哈希桶节点）
        XMapBase_iterator parent; ///< 父类迭代器，用于兼容基类接口
    };
    size_t index; ///< 当前桶索引（用于遍历哈希表）
} XHashMap_iterator;

/**
* @brief 获取指向XHashMap第一个元素的迭代器
* @param this_map XHashMap实例指针
* @return 指向第一个键值对的迭代器，若映射为空则返回结束迭代器
*/
XHashMap_iterator XHashMap_begin(XHashMap* this_map);

/**
* @brief 获取XHashMap的结束迭代器（哨兵位置）
* @param this_map XHashMap实例指针
* @return 结束迭代器
*/
XHashMap_iterator XHashMap_end(XHashMap* this_map);

/**
* @brief 判断迭代器是否已到达末尾
* @param it 迭代器指针
* @return 若迭代器已到达末尾返回true，否则返回false
*/
bool XHashMap_iterator_isEnd( const XHashMap_iterator* it);

/**
* @brief 将迭代器移动到下一个元素位置
* @param this_map XHashMap实例指针
* @param it 迭代器指针（会被修改）
*/
void XHashMap_iterator_add(XHashMap* this_map, XHashMap_iterator* it);

/**
* @brief 判断两个迭代器是否相等
* @param itFirst 第一个迭代器指针
* @param itSecond 第二个迭代器指针
* @return 相等返回true，不相等返回false
*/
bool XHashMap_iterator_equality(const XHashMap_iterator* itFirst, const XHashMap_iterator* itSecond);

/**
* @brief 使用回调函数遍历XHashMap中的所有键值对
* @param this_map XHashMap实例指针
* @param ForFunction 对每个键值对调用的回调函数
* @param args 传递给回调函数的用户参数
*/
void XHashMap_iterator_for_each(XHashMap* this_map, XFor_each ForFunction, void* args);

/**
* @brief 获取迭代器当前指向的键值对数据指针
* @param it 迭代器指针
* @return 指向当前键值对的XPair指针，可通过XPair_first/XPair_second获取键和值
*/
XPair* XHashMap_iterator_data(XHashMap_iterator* it);

#ifdef __cplusplus
}
#endif
#endif
