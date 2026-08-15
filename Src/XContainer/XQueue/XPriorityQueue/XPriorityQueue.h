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
	XCompare compare;	//优先级比较规则
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
XPriorityQueue* XPriorityQueue_create_ex(XMemoryType memory,  size_t typeSize, XCompare compare, XSortOrder order);
/**
* @brief 从优先队列中移除指定元素
* @param this_queue 优先队列实例指针
* @param value 要移除的元素值
* @param n 最多移除的元素数量
* @return 实际移除的元素数量
* @note 使用元素比较函数进行匹配
*/
size_t XPriorityQueue_remove(XPriorityQueue* this_queue, const void* value, size_t n);
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

/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：以下宏仅做命名映射，不新增任何运行时行为；语义与被映射的原函数完全等价。
 *       目的是让熟悉 Qt 的调用方可以直接用 enqueue/dequeue/head/count/length/empty
 *       等 Qt 风格名称调用本容器。Qt 参考: QQueue<T> : QList<T>。
 * ============================================================================== */

/**
 * @brief 入队(拷贝语义)——Qt 别名，等价于 XPriorityQueue_Push_Base
 * @param this_queue 队列实例指针
 * @param type       元素数据类型（如 int、float）
 * @param value      待插入的元素值（拷贝语义）
 * @note Qt 映射: QQueue::enqueue(const T&) → QList::append(const T&)
 */
#define XPriorityQueue_Enqueue_Base       XPriorityQueue_Push_Base
/**
 * @brief 入队(拷贝语义，函数式)——Qt 别名，等价于 XPriorityQueue_push_base
 * @param this_queue 队列实例指针
 * @param pvData     指向源数据的指针（按容器 typeSize 拷贝）
 * @return 成功返回 true，失败(如满/参数非法)返回 false
 * @note Qt 映射: QQueue::enqueue → QList::append
 */
#define XPriorityQueue_enqueue_base       XPriorityQueue_push_base
/**
 * @brief 入队(移动语义)——Qt 别名，等价于 XPriorityQueue_Push_Move_Base
 * @param this_queue 队列实例指针
 * @param type       元素数据类型
 * @param value      待插入的元素值（移动语义，所有权转移）
 * @note Qt 移动构造对应 QList::emplaceBack(std::move(v))
 */
#define XPriorityQueue_Enqueue_Move_Base  XPriorityQueue_Push_Move_Base
/**
 * @brief 入队(移动语义，函数式)——Qt 别名，等价于 XPriorityQueue_push_move_base
 * @param this_queue 队列实例指针
 * @param pvData     指向源数据的指针（所有权移交给队列）
 * @return 成功返回 true，失败返回 false
 */
#define XPriorityQueue_enqueue_move_base  XPriorityQueue_push_move_base

/**
 * @brief 出队并返回队头——Qt 别名，等价于 XPriorityQueue_receive_base
 * @param this_queue 队列实例指针
 * @param pvBuffer   接收队头元素的缓冲区（不可为 NULL）
 * @return 成功接收并移除队头返回 true；队列为空或参数非法返回 false
 * @note Qt 映射: QQueue::dequeue() → QList::takeFirst()，即 "先读后弹" 的原子语义
 */
#define XPriorityQueue_dequeue_base       XPriorityQueue_receive_base
/**
 * @brief 出队但不返回值——Qt 无直接对应，保留 pop 语义
 * @param this_queue 队列实例指针
 * @note 仅移除队头元素，若需要读取先用 XPriorityQueue_head_base / XPriorityQueue_Head_Base
 */
#define XPriorityQueue_dequeue_void_base  XPriorityQueue_pop_base

/**
 * @brief 获取队头(类型安全宏)——Qt 别名，等价于 XPriorityQueue_Top_Base
 * @param this_queue 队列实例指针
 * @param Type       元素类型
 * @return 队头元素的引用（Type 类型）；仅读，不出队
 * @note Qt 映射: QQueue::head() → QList::first()
 */
#define XPriorityQueue_Head_Base          XPriorityQueue_Top_Base
/**
 * @brief 获取队头地址(函数式)——Qt 别名，等价于 XPriorityQueue_top_base
 * @param this_queue 队列实例指针
 * @return 队头元素地址；队列为空返回 NULL；仅读，不出队
 */
#define XPriorityQueue_head_base          XPriorityQueue_top_base

/**
 * @brief 队列当前元素个数——Qt 别名，等价于 XPriorityQueue_size_base
 * @param this_queue 队列实例指针
 * @return 元素个数
 * @note Qt 映射: QList::count()
 */
#define XPriorityQueue_count_base         XPriorityQueue_size_base
/**
 * @brief 队列当前元素个数——Qt 别名，等价于 XPriorityQueue_size_base
 * @param this_queue 队列实例指针
 * @return 元素个数
 * @note Qt 映射: QList::length()，与 count() 完全等价，仅命名不同
 */
#define XPriorityQueue_length_base        XPriorityQueue_size_base

/**
 * @brief 判空——Qt 别名，等价于 XPriorityQueue_isEmpty_base
 * @param this_queue 队列实例指针
 * @return 队列为空返回 true，否则返回 false
 * @note Qt 映射: QList::empty()（QList 同时提供 isEmpty，本项目仅将 empty 对齐 Qt）
 */
#define XPriorityQueue_empty_base         XPriorityQueue_isEmpty_base


#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XPriorityQueue_create
#define XPriorityQueue_create(...) XPriorityQueue_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif