#include"CXinYueConfig.h"
#if !defined(XQUEUE_H)&& XQueue_ON
#define XQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>      ///< 标准输入输出头文件，提供基础IO操作支持
#include <stdbool.h>    ///< 布尔类型定义头文件，提供bool、true、false等定义
#include "XListSLinked.h"///< 单链表头文件，XQueue基于单链表实现底层存储
#include "XQueueBase.h"  ///< 队列基类头文件，XQueue继承自XQueueBase
/**
* @brief XQueue虚函数表大小定义
* @note 与基类XQueueBase的虚函数表大小一致，继承基类所有虚函数接口
*/
#define XQUEUE_VTABLE_SIZE (XQUEUEBASE_VTABLE_SIZE)
/**
* @brief 队列结构体定义
* @details 基于单链表实现的队列，继承XQueueBase的接口规范
* @note 内部通过m_list成员（单链表）存储元素，实现FIFO（先进先出）特性
*/
typedef struct XQueue
{
	XListSLinked m_list;  ///< 单链表成员，用于存储队列元素
} XQueue;
// ------------------------------ 类初始化与实例管理 ------------------------------
/**
* @brief 初始化XQueue的虚函数表
* @return 初始化完成的虚函数表指针XVtable*
* @note 用于设置XQueue的虚函数映射，实现基类XQueueBase定义的接口
*/
XVtable* XQueue_class_init();
/**
* @brief 初始化队列实例
* @param this_queue 待初始化的队列实例指针
* @param typeSize 队列中元素的类型大小（字节数）
* @note 初始化内部单链表，设置元素类型大小，绑定虚函数表
*/
void XQueue_init(XQueue* this_queue, size_t typeSize);
/**
* @brief 类型安全的队列创建宏
* @param Type 队列中元素的数据类型（如int、float）
* @return 创建成功的队列实例指针XQueue*，失败返回NULL
* @note 封装XQueue_create，自动计算元素类型大小，简化创建过程
*/
#define XQueue_Create(Type) XQueue_create(sizeof(Type))
/**
* @brief 创建队列实例
* @param typeSize 队列中元素的类型大小（字节数）
* @return 创建成功的队列实例指针XQueue*，失败返回NULL
* @note 内部调用XMemory_malloc分配内存并调用XQueue_init初始化
*/
XQueue* XQueue_create(size_t typeSize);
// ------------------------------ 入队操作 ------------------------------
/**
* @brief 类型安全的入队宏（拷贝语义，继承自基类）
* @note 复用XQueueBase的XQueueBase_Push_Base宏，实现向队尾拷贝插入元素
*/
#define XQueue_Push_Base				XQueueBase_Push_Base
/**
* @brief 入队操作（拷贝语义，基础版本，继承自基类）
* @note 复用XQueueBase的XQueueBase_push_base函数，向队尾拷贝插入元素
*/
#define XQueue_push_base				XQueueBase_push_base
/**
* @brief 类型安全的入队宏（移动语义，继承自基类）
* @note 复用XQueueBase的XQueueBase_Push_Move_Base宏，实现向队尾移动插入元素
*/
#define XQueue_Push_Move_Base			XQueueBase_Push_Move_Base
/**
* @brief 入队操作（移动语义，基础版本，继承自基类）
* @note 复用XQueueBase的XQueueBase_push_move_base函数，向队尾移动插入元素
*/
#define XQueue_push_move_base			XQueueBase_push_move_base
// ------------------------------ 出队操作 ------------------------------
/**
* @brief 出队操作（继承自基类）
* @note 复用XQueueBase的XQueueBase_pop_base函数，移除并释放队头元素
*/
#define XQueue_pop_base					XQueueBase_pop_base
// ------------------------------ 元素接收与访问 ------------------------------
/**
* @brief 接收并出队操作（继承自基类）
* @note 复用XQueueBase的XQueueBase_receive_base函数，获取队头元素并移除
*/
#define XQueue_receive_base				XQueueBase_receive_base
/**
* @brief 类型安全的获取队头元素宏（继承自基类）
* @note 复用XQueueBase的XQueueBase_Top_Base宏，获取队头元素的引用
*/
#define XQueue_Top_Base					XQueueBase_Top_Base
/**
* @brief 获取队头元素地址（继承自基类）
* @note 复用XQueueBase的XQueueBase_top_base函数，返回队头元素的地址
*/
#define XQueue_top_base					XQueueBase_top_base
// ------------------------------ 队列状态查询 ------------------------------
/**
* @brief 判断队列是否已满（继承自基类）
* @note 复用XQueueBase的XQueueBase_isFull_base函数，对于基于链表的队列通常返回false
*/
#define XQueue_isFull_base				XQueueBase_isFull_base
// ------------------------------ 容器管理（继承自XQueueBase/XContainerObject） ------------------------------
/**
* @brief 拷贝容器（继承自基类）
* @note 复用XQueueBase的XQueueBase_copy_base，复制源队列的所有元素
*/
#define XQueue_copy_base				XQueueBase_copy_base
/**
* @brief 移动容器资源（继承自基类）
* @note 复用XQueueBase的XQueueBase_move_base，转移源队列的资源所有权
*/
#define XQueue_move_base				XQueueBase_move_base
/**
* @brief 反初始化容器（继承自基类）
* @note 复用XQueueBase的XQueueBase_deinit_base，释放资源但不销毁实例本身
*/
#define XQueue_deinit_base				XQueueBase_deinit_base
/**
* @brief 删除容器实例（继承自基类）
* @note 复用XQueueBase的XQueueBase_delete_base，释放资源并销毁实例
*/
#define XQueue_delete_base				XQueueBase_delete_base
/**
* @brief 清空容器元素（继承自基类）
* @note 复用XQueueBase的XQueueBase_clear_base，删除所有元素但保留队列结构
*/
#define XQueue_clear_base				XQueueBase_clear_base
/**
* @brief 判断容器是否为空（继承自基类）
* @note 复用XQueueBase的XQueueBase_isEmpty_base，无元素时返回true
*/
#define XQueue_isEmpty_base				XQueueBase_isEmpty_base
/**
* @brief 获取容器元素数量（继承自基类）
* @note 复用XQueueBase的XQueueBase_size_base，返回当前元素个数
*/
#define XQueue_size_base				XQueueBase_size_base
/**
* @brief 获取容器容量（继承自基类）
* @note 复用XQueueBase的XQueueBase_capacity_base，对于链表实现通常返回元素数量
*/
#define XQueue_capacity_base			XQueueBase_capacity_base
/**
* @brief 交换两个容器的内容（继承自基类）
* @note 复用XQueueBase的XQueueBase_swap_base，快速交换两个队列的内部数据
*/
#define XQueue_swap_base				XQueueBase_swap_base
/**
* @brief 获取容器元素类型大小（继承自基类）
* @note 复用XQueueBase的XQueueBase_typeSize_base，返回元素类型的字节数
*/
#define XQueue_typeSize_base			XQueueBase_typeSize_base
/**
* @brief C++兼容性声明结束
*/
#ifdef __cplusplus
}
#endif
#endif  // !XQUEUE_H