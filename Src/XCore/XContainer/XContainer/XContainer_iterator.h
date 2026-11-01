/**
* @file XContainer_iterator.h
* @brief 容器迭代器基础定义头文件
* @details 该文件定义了容器迭代器的声明宏和遍历宏，为所有容器提供统一的迭代器接口规范
*/
#include"CXinYueConfig.h"
#if !defined(XContainer_ITERATOR_H)&& XContainer_ON
#define XContainer_ITERATOR_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XFunctionCallback.h"

/**
* @brief 声明容器类型
* @param Container 容器名称（如XVector、XMap等）
* @details 使用该宏前置声明容器结构体类型，避免循环依赖
* @example XContainerTypeDeclare(XVector); // 展开为: typedef struct XVector XVector;
*/
#define XContainerTypeDeclare(Container) typedef struct Container Container

/**
* @brief 声明容器正向迭代器类型
* @param Container 容器名称
* @details 使用该宏声明容器的正向迭代器类型，迭代器用于从前向后遍历容器
* @example XContainerIteratorDeclare(XVector); // 展开为: typedef void XVector_iterator;
*/
#define XContainerIteratorDeclare(Container) typedef void Container##_iterator

/**
* @brief 声明容器反向迭代器类型
* @param Container 容器名称
* @details 使用该宏声明容器的反向迭代器类型，迭代器用于从后向前遍历容器
* @example XContainerReverseIteratorDeclare(XVector); // 展开为: typedef void XVector_reverse_iterator;
*/
#define XContainerReverseIteratorDeclare(Container) typedef void Container##_reverse_iterator

/**
* @brief 正向迭代器遍历宏
* @param container 容器实例指针
* @param type 容器类型名（如XVector、XMap等）
* @param it 迭代器变量名
* @details 使用该宏可以简洁地遍历容器中的所有元素（从前向后）
* @example
*   for_each_iterator(vec, XVector, it) {
*       void* data = XVector_iterator_data(&it);
*       // 处理数据...
*   }
*/
#define for_each_iterator(container,type,it) for(type##_iterator it=type##_begin(container),endIt=type##_end(container);!type##_iterator_equality(&it,&endIt);type##_iterator_add(container,&it))

/**
* @brief 反向迭代器遍历宏
* @param container 容器实例指针
* @param type 容器类型名（如XVector、XMap等）
* @param it 迭代器变量名
* @details 使用该宏可以简洁地反向遍历容器中的所有元素（从后向前）
* @example
*   for_each_reverse_iterator(vec, XVector, it) {
*       void* data = XVector_reverse_iterator_data(&it);
*       // 处理数据...
*   }
*/
#define for_each_reverse_iterator(container,type,it) for(type##_reverse_iterator it=type##_rbegin(container),endIt=type##_rend(container);!type##_reverse_iterator_equality(&it,&endIt);type##_reverse_iterator_add(container,&it))

#ifdef __cplusplus
}
#endif
#endif // ! ITERATOR_H
