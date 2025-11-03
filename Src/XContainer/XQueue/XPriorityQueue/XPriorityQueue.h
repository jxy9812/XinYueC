#include"CXinYueConfig.h"
#if !defined(XPRIORITYQUEUE_H)&& XPriorityQueue_ON
#define XPRIORITYQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XQueueBase.h"
#include"XVector.h"
/**
* @brief 优先队列虚函数表大小定义
* @note 与基类XQueueBase的虚函数表大小一致，继承基类所有虚函数接口
*/
#define XPRIORITYQUEUE_VTABLE_SIZE (XQUEUEBASE_VTABLE_SIZE)      
/**
* @brief 优先队列结构体定义
* @details 基于向量(XVector)实现的优先队列，内部通过堆结构维护元素优先级
* @param m_vector 用于存储元素的向量容器
* @param m_order 排序顺序（升序/降序），决定优先级判断规则
*/
typedef struct XPriorityQueue
{
	XVector m_vector;    ///< 存储队列元素的底层向量
	XSortOrder m_order;  ///< 排序顺序，影响优先级比较逻辑
} XPriorityQueue;
// ------------------------------ 类初始化与实例管理 ------------------------------
/**
* @brief 初始化优先队列的虚函数表
* @return 初始化完成的虚函数表指针XVtable*
* @note 用于绑定优先队列的虚函数实现，提供基类XQueueBase接口的具体实现
*/
XVtable* XPriorityQueue_class_init();
/**
* @brief 初始化优先队列实例
* @param this_queue 待初始化的优先队列实例指针
* @param typeSize 队列中元素的类型大小（字节数）
* @param compare 元素比较函数，用于判断元素优先级
* @param order 排序顺序（升序/降序）
* @note 初始化内部向量，设置元素类型、比较函数和排序顺序，绑定虚函数表
*/
void XPriorityQueue_init(XPriorityQueue* this_queue, size_t typeSize, XCompare compare, XSortOrder order);
/**
* @brief 创建优先队列实例
* @param typeSize 队列中元素的类型大小（字节数）
* @param compare 元素比较函数，用于判断元素优先级
* @param order 排序顺序（升序/降序）
* @return 创建成功的优先队列实例指针，失败返回NULL
* @note 内部分配内存并调用XPriorityQueue_init完成初始化
*/
XPriorityQueue* XPriorityQueue_create(size_t typeSize, XCompare compare, XSortOrder order);
/**
* @brief 类型安全的优先队列创建宏
* @param Type 队列中元素的数据类型（如int、float）
* @param compare 元素比较函数
* @param order 排序顺序
* @return 创建成功的优先队列实例指针，失败返回NULL
* @note 自动计算元素类型大小，简化XPriorityQueue_create的调用
*/
#define XPriorityQueue_Create(Type,compare,order) XPriorityQueue_create(sizeof(Type),compare,order)
// ------------------------------ 入队操作 ------------------------------
/**
* @brief 入队操作（拷贝语义，基于基类实现）
* @note 复用XQueueBase的入队接口，内部通过堆调整维护优先级
*/
#define XPriorityQueue_Push_Base				XQueueBase_Push_Base
/**
* @brief 入队操作（拷贝语义，基础版本，基于基类实现）
* @note 复用XQueueBase的入队接口，向队列添加元素并调整堆结构
*/
#define XPriorityQueue_push_base				XQueueBase_push_base
/**
* @brief 入队操作（移动语义，基于基类实现）
* @note 复用XQueueBase的入队接口，通过移动语义添加元素并调整堆结构
*/
#define XPriorityQueue_Push_Move_Base			XQueueBase_Push_Move_Base
/**
* @brief 入队操作（移动语义，基础版本，基于基类实现）
* @note 复用XQueueBase的入队接口，移动元素所有权并调整堆结构
*/
#define XPriorityQueue_push_move_base			XQueueBase_push_move_base
// ------------------------------ 出队与接收操作 ------------------------------
/**
* @brief 出队操作（基于基类实现）
* @note 复用XQueueBase的出队接口，移除队头（优先级最高）元素并调整堆结构
*/
#define XPriorityQueue_pop_base					XQueueBase_pop_base
/**
* @brief 接收并出队操作（基于基类实现）
* @note 复用XQueueBase的接口，获取队头元素数据并移除该元素
*/
#define XPriorityQueue_receive_base				XQueueBase_receive_base
// ------------------------------ 元素访问 ------------------------------
/**
* @brief 获取队头元素（类型安全，基于基类实现）
* @note 复用XQueueBase的接口，返回队头（优先级最高）元素的引用
*/
#define XPriorityQueue_Top_Base					XQueueBase_Top_Base
/**
* @brief 获取队头元素地址（基于基类实现）
* @note 复用XQueueBase的接口，返回队头（优先级最高）元素的地址
*/
#define XPriorityQueue_top_base				XQueueBase_top_base
// ------------------------------ 队列状态查询 ------------------------------
/**
* @brief 判断队列是否已满（基于基类实现）
* @note 复用XQueueBase的接口，判断队列是否达到容量上限
*/
#define XPriorityQueue_isFull_base				XQueueBase_isFull_base
/**
* @brief 判断队列是否为空（基于基类实现）
* @note 复用XQueueBase的接口，判断队列中是否无元素
*/
#define XPriorityQueue_isEmpty_base				XQueueBase_isEmpty_base
/**
* @brief 获取队列元素数量（基于基类实现）
* @note 复用XQueueBase的接口，返回当前队列中元素的个数
*/
#define XPriorityQueue_size_base				XQueueBase_size_base
/**
* @brief 获取队列容量（基于基类实现）
* @note 复用XQueueBase的接口，返回队列可容纳的最大元素数
*/
#define XPriorityQueue_capacity_base			XQueueBase_capacity_base
/**
* @brief 获取元素类型大小（基于基类实现）
* @note 复用XQueueBase的接口，返回队列中元素的类型大小（字节数）
*/
#define XPriorityQueue_typeSize_base			XQueueBase_typeSize_base
// ------------------------------ 容器管理 ------------------------------
/**
* @brief 拷贝容器（基于基类实现）
* @note 复用XQueueBase的接口，复制源队列的所有元素和状态
*/
#define XPriorityQueue_copy_base				XQueueBase_copy_base
/**
* @brief 移动容器资源（基于基类实现）
* @note 复用XQueueBase的接口，转移源队列的资源所有权至当前队列
*/
#define XPriorityQueue_move_base				XQueueBase_move_base
/**
* @brief 反初始化容器（基于基类实现）
* @note 复用XQueueBase的接口，释放队列资源但不销毁实例本身
*/
#define XPriorityQueue_deinit_base				XQueueBase_deinit_base
/**
* @brief 删除容器实例（基于基类实现）
* @note 复用XQueueBase的接口，释放队列资源并销毁实例
*/
#define XPriorityQueue_delete_base				XQueueBase_delete_base
/**
* @brief 清空容器元素（基于基类实现）
* @note 复用XQueueBase的接口，删除队列中所有元素但保留结构
*/
#define XPriorityQueue_clear_base				XQueueBase_clear_base
/**
* @brief 交换两个容器内容（基于基类实现）
* @note 复用XQueueBase的接口，快速交换两个队列的内部数据
*/
#define XPriorityQueue_swap_base				XQueueBase_swap_base
#ifdef __cplusplus
}
#endif
#endif