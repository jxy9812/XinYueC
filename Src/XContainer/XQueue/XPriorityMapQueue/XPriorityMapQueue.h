#include"CXinYueConfig.h"
#if !defined(XPriorityMapQueue_H)&& XQueue_ON
#define XPriorityMapQueue_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include"XQueueBase.h"
/**
* @brief 优先映射队列虚函数表大小定义
* @note 与基类XQueueBase的虚函数表大小一致，继承基类所有虚函数接口
*/
#define XPRIORITYMAPQUEUE_VTABLE_SIZE (XQUEUEBASE_VTABLE_SIZE)
/**
* @brief 优先映射队列结构体定义
* @details 结合优先级队列与映射表的容器，支持高频数据的FIFO队列与低频数据的优先队列管理
* @param m_class 继承自容器基类，包含通用容器属性
* @param m_priorityCopyMethod 优先级数据的拷贝方法
* @param m_priorityMoveMethod 优先级数据的移动方法
* @param m_priorityDeinitMethod 优先级数据的释放方法
* @param mapPriority 优先级映射表，用于快速查找对应FIFO队列
* @param low_freq_queue 低频数据优先队列，用于存储无对应FIFO队列的低优先级数据
*/
typedef struct XPriorityMapQueue
{
	XContainer m_class;///< 容器基类成员，提供通用容器功能
	XCDataCopyMethod m_priorityCopyMethod;///< 优先级数据拷贝函数指针
	XCDataMoveMethod m_priorityMoveMethod;///< 优先级数据移动函数指针
	XCDataDeinitMethod m_priorityDeinitMethod;///< 优先级数据释放函数指针
	void* mapPriority;///< 优先级到FIFO队列的映射表
	XPriorityQueue* low_freq_queue;///< 存储低频数据的优先队列
}XPriorityMapQueue;
// ------------------------------ 类初始化与实例管理 ------------------------------
/**
* @brief 初始化优先映射队列的虚函数表
* @return 初始化完成的虚函数表指针XVtable*
* @note 绑定优先映射队列的虚函数实现，继承并扩展基类接口
*/
XVtable* XPriorityMapQueue_class_init();
/**
* @brief 初始化优先映射队列实例
* @param this_queue 待初始化的优先映射队列实例指针
* @param prioritySize 优先级数据的类型大小（字节数）
* @param priorityCom 优先级比较函数，用于排序和映射查找
* @param priorityOrder 优先级排序顺序（升序/降序）
* @param typeSize 队列中元素数据的类型大小（字节数）
* @note 初始化内部映射表和低频优先队列，设置类型大小与比较规则
*/
void XPriorityMapQueue_init(XPriorityMapQueue* this_queue, size_t prioritySize, XCompare priorityCom, XSortOrder priorityOrder, size_t typeSize);
/**
* @brief 创建优先映射队列实例（基础函数）
* @param prioritySize 优先级数据的类型大小（字节数）
* @param priorityCom 优先级比较函数
* @param priorityOrder 优先级排序顺序
* @param typeSize 元素数据的类型大小（字节数）
* @return 创建成功的实例指针，失败返回NULL
* @note 内部分配内存并调用XPriorityMapQueue_init完成初始化
*/
XPriorityMapQueue* XPriorityMapQueue_create(size_t prioritySize, XCompare priorityCom, XSortOrder priorityOrder, size_t typeSize);
/**
* @brief 类型安全的优先映射队列创建宏
* @param priority 优先级数据类型（如int）
* @param data 元素数据类型（如float）
* @param priorityCom 优先级比较函数
* @param priorityOrder 优先级排序顺序
* @return 创建成功的实例指针，失败返回NULL
* @note 自动推导优先级和元素数据的类型大小，简化XPriorityMapQueue_create调用
*/
#define XPriorityMapQueue_Create(priority,data,priorityCom,priorityOrder) XPriorityMapQueue_create(sizeof(priority),priorityCom,priorityOrder,sizeof(data))
// ------------------------------ 队列配置（FIFO队列管理） ------------------------------
/**
* @brief 为指定优先级添加FIFO队列（高频数据专用）
* @param this_queue 优先映射队列实例指针
* @param priority 优先级数据指针（作为映射键）
* @param queueSize FIFO队列的容量大小
* @return 添加成功返回true，失败（如优先级已存在）返回false
* @note 高频数据将优先存入对应FIFO队列，提升访问效率
*/
bool XPriorityMapQueue_addFifoQueue(XPriorityMapQueue* this_queue, void* priority, size_t queueSize);
/**
* @brief 移除指定优先级对应的FIFO队列
* @param this_queue 优先映射队列实例指针
* @param priority 优先级数据指针（映射键）
* @return 移除成功返回true，失败（如优先级不存在）返回false
* @note 移除后该优先级数据将存入低频优先队列
*/
bool XPriorityMapQueue_removeFifoQueue(XPriorityMapQueue* this_queue, void* priority);
// ------------------------------ 入队操作 ------------------------------
/**
* @brief 入队操作（拷贝语义，基于基类实现）
* @note 复用XQueueBase的入队接口，根据优先级判断存入FIFO队列或低频优先队列
*/
#define XPriorityMapQueue_Push_Base				    XQueueBase_Push_Base
/**
* @brief 入队操作（拷贝语义，基础版本）
* @param this_queue 优先映射队列实例指针
* @param pvPriority 优先级数据指针（拷贝源）
* @param pvValue 元素数据指针（拷贝源）
* @return 入队成功返回true，失败返回false
* @note 拷贝优先级和元素数据，根据优先级映射决定存储位置
*/
bool XPriorityMapQueue_push_base(XPriorityMapQueue* this_queue, void* pvPriority, void* pvValue);
/**
* @brief 入队操作（移动语义，基础版本）
* @param this_queue 优先映射队列实例指针
* @param pvPriority 优先级数据指针（移动源，所有权转移）
* @param pvValue 元素数据指针（移动源，所有权转移）
* @return 入队成功返回true，失败返回false
* @note 移动优先级和元素数据，减少拷贝开销，根据映射决定存储位置
*/
bool XPriorityMapQueue_push_move_base(XPriorityMapQueue* this_queue, void* pvPriority, void* pvValue);
/**
* @brief 入队操作（移动语义，基于基类实现）
* @note 复用XQueueBase的入队接口，通过移动语义入队并判断存储位置
*/
#define XPriorityMapQueue_Push_Move_Base			XQueueBase_Push_Move_Base
/**
* @brief 入队到指定优先级的FIFO队列（拷贝语义）
* @param this_queue 优先映射队列实例指针
* @param pvPriority 优先级数据指针（映射键）
* @param pvValue 元素数据指针（拷贝源）
* @return 入队成功返回true，失败（如FIFO队列不存在）返回false
* @note 直接操作指定优先级的FIFO队列，适用于已知高频数据场景
*/
bool XPriorityMapQueue_push_fifo(XPriorityMapQueue* this_queue, void* pvPriority, void* pvValue);
/**
* @brief 入队到指定优先级的FIFO队列（移动语义）
* @param this_queue 优先映射队列实例指针
* @param pvPriority 优先级数据指针（映射键）
* @param pvValue 元素数据指针（移动源）
* @return 入队成功返回true，失败（如FIFO队列不存在）返回false
* @note 直接操作指定FIFO队列，通过移动语义提升性能
*/
bool XPriorityMapQueue_push_fifo_move(XPriorityMapQueue* this_queue, void* pvPriority, void* pvValue);
// ------------------------------ 出队与接收操作 ------------------------------
/**
* @brief 出队操作（基于基类实现）
* @note 复用XQueueBase的出队接口，移除优先级最高的元素（FIFO队列元素优先于低频队列）
*/
#define XPriorityMapQueue_pop_base					XQueueBase_pop_base
/**
* @brief 接收并出队操作（基于基类实现）
* @note 复用XQueueBase的接口，获取队头元素数据并移除该元素
*/
#define XPriorityMapQueue_receive_base				XQueueBase_receive_base
// ------------------------------ 元素访问 ------------------------------
/**
* @brief 获取队头元素（类型安全，基于基类实现）
* @note 复用XQueueBase的接口，返回优先级最高的元素引用（FIFO队列元素优先）
*/
#define XPriorityMapQueue_Top_Base					XQueueBase_Top_Base
/**
* @brief 获取队头元素地址（基于基类实现）
* @note 复用XQueueBase的接口，返回优先级最高的元素地址
*/
#define XPriorityMapQueue_top_base					XQueueBase_top_base
// ------------------------------ 队列状态查询 ------------------------------
/**
* @brief 判断队列是否已满（基于基类实现）
* @note 复用XQueueBase的接口，判断队列是否达到容量上限
*/
#define XPriorityMapQueue_isFull_base				XQueueBase_isFull_base
/**
* @brief 判断队列是否为空（基于基类实现）
* @note 复用XQueueBase的接口，判断队列中是否无元素
*/
#define XPriorityMapQueue_isEmpty_base				XQueueBase_isEmpty_base
/**
* @brief 获取队列元素数量（基于基类实现）
* @note 复用XQueueBase的接口，返回当前队列中元素的总个数
*/
#define XPriorityMapQueue_size_base				    XQueueBase_size_base
/**
* @brief 获取队列容量（基于基类实现）
* @note 复用XQueueBase的接口，返回队列可容纳的最大元素数
*/
#define XPriorityMapQueue_capacity_base			    XQueueBase_capacity_base
/**
* @brief 获取元素类型大小（基于基类实现）
* @note 复用XQueueBase的接口，返回队列中元素数据的类型大小（字节数）
*/
#define XPriorityMapQueue_typeSize_base			    XQueueBase_typeSize_base
// ------------------------------ 容器管理 ------------------------------
/**
* @brief 拷贝容器（基于基类实现）
* @note 复用XQueueBase的接口，复制源队列的所有元素、映射关系和状态
*/
#define XPriorityMapQueue_copy_base				    XQueueBase_copy_base
/**
* @brief 移动容器资源（基于基类实现）
* @note 复用XQueueBase的接口，转移源队列的资源所有权至当前队列
*/
#define XPriorityMapQueue_move_base				    XQueueBase_move_base
/**
* @brief 反初始化容器（基于基类实现）
* @note 复用XQueueBase的接口，释放队列资源但不销毁实例本身
*/
#define XPriorityMapQueue_deinit_base				XQueueBase_deinit_base
/**
* @brief 删除容器实例（基于基类实现）
* @note 复用XQueueBase的接口，释放队列资源并销毁实例
*/
#define XPriorityMapQueue_delete_base				XQueueBase_delete_base
/**
* @brief 清空容器元素（基于基类实现）
* @note 复用XQueueBase的接口，删除所有元素但保留映射结构
*/
#define XPriorityMapQueue_clear_base				XQueueBase_clear_base
/**
* @brief 交换两个容器内容（基于基类实现）
* @note 复用XQueueBase的接口，快速交换两个队列的内部数据和映射关系
*/
#define XPriorityMapQueue_swap_base				    XQueueBase_swap_base
#ifdef __cplusplus
}
#endif
#endif