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
* @note 内部调用XMalloc_System分配内存并调用XQueue_init初始化
*/
XQueue* XQueue_create_ex(XMemoryType memory,  size_t typeSize);
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
// ------------------------------ 容器管理（继承自XQueueBase/XContainer） ------------------------------
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

/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：以下宏仅做命名映射，不新增任何运行时行为；语义与被映射的原函数完全等价。
 *       目的是让熟悉 Qt 的调用方可以直接用 enqueue/dequeue/head/count/length/empty
 *       等 Qt 风格名称调用本容器。Qt 参考: QQueue<T> : QList<T>。
 * ============================================================================== */

/**
 * @brief 入队(拷贝语义)——Qt 别名，等价于 XQueue_Push_Base
 * @param this_queue 队列实例指针
 * @param type       元素数据类型（如 int、float）
 * @param value      待插入的元素值（拷贝语义）
 * @note Qt 映射: QQueue::enqueue(const T&) → QList::append(const T&)
 */
#define XQueue_Enqueue_Base       XQueue_Push_Base
/**
 * @brief 入队(拷贝语义，函数式)——Qt 别名，等价于 XQueue_push_base
 * @param this_queue 队列实例指针
 * @param pvData     指向源数据的指针（按容器 typeSize 拷贝）
 * @return 成功返回 true，失败(如满/参数非法)返回 false
 * @note Qt 映射: QQueue::enqueue → QList::append
 */
#define XQueue_enqueue_base       XQueue_push_base
/**
 * @brief 入队(移动语义)——Qt 别名，等价于 XQueue_Push_Move_Base
 * @param this_queue 队列实例指针
 * @param type       元素数据类型
 * @param value      待插入的元素值（移动语义，所有权转移）
 * @note Qt 移动构造对应 QList::emplaceBack(std::move(v))
 */
#define XQueue_Enqueue_Move_Base  XQueue_Push_Move_Base
/**
 * @brief 入队(移动语义，函数式)——Qt 别名，等价于 XQueue_push_move_base
 * @param this_queue 队列实例指针
 * @param pvData     指向源数据的指针（所有权移交给队列）
 * @return 成功返回 true，失败返回 false
 */
#define XQueue_enqueue_move_base  XQueue_push_move_base

/**
 * @brief 出队并返回队头——Qt 别名，等价于 XQueue_receive_base
 * @param this_queue 队列实例指针
 * @param pvBuffer   接收队头元素的缓冲区（不可为 NULL）
 * @return 成功接收并移除队头返回 true；队列为空或参数非法返回 false
 * @note Qt 映射: QQueue::dequeue() → QList::takeFirst()，即 "先读后弹" 的原子语义
 */
#define XQueue_dequeue_base       XQueue_receive_base
/**
 * @brief 出队但不返回值——Qt 无直接对应，保留 pop 语义
 * @param this_queue 队列实例指针
 * @note 仅移除队头元素，若需要读取先用 XQueue_head_base / XQueue_Head_Base
 */
#define XQueue_dequeue_void_base  XQueue_pop_base

/**
 * @brief 获取队头(类型安全宏)——Qt 别名，等价于 XQueue_Top_Base
 * @param this_queue 队列实例指针
 * @param Type       元素类型
 * @return 队头元素的引用（Type 类型）；仅读，不出队
 * @note Qt 映射: QQueue::head() → QList::first()
 */
#define XQueue_Head_Base          XQueue_Top_Base
/**
 * @brief 获取队头地址(函数式)——Qt 别名，等价于 XQueue_top_base
 * @param this_queue 队列实例指针
 * @return 队头元素地址；队列为空返回 NULL；仅读，不出队
 */
#define XQueue_head_base          XQueue_top_base

/**
 * @brief 队列当前元素个数——Qt 别名，等价于 XQueue_size_base
 * @param this_queue 队列实例指针
 * @return 元素个数
 * @note Qt 映射: QList::count()
 */
#define XQueue_count_base         XQueue_size_base
/**
 * @brief 队列当前元素个数——Qt 别名，等价于 XQueue_size_base
 * @param this_queue 队列实例指针
 * @return 元素个数
 * @note Qt 映射: QList::length()，与 count() 完全等价，仅命名不同
 */
#define XQueue_length_base        XQueue_size_base

/**
 * @brief 判空——Qt 别名，等价于 XQueue_isEmpty_base
 * @param this_queue 队列实例指针
 * @return 队列为空返回 true，否则返回 false
 * @note Qt 映射: QList::empty()（QList 同时提供 isEmpty，本项目仅将 empty 对齐 Qt）
 */
#define XQueue_empty_base         XQueue_isEmpty_base


#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XQueue_create
#define XQueue_create(...) XQueue_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif  // !XQUEUE_H