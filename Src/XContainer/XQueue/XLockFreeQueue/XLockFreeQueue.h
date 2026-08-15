#include"CXinYueConfig.h"
#if !defined(XLockFreeQueue_H)&& XLockFreeQueue_ON
#define XLockFreeQueue_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XCircularQueue.h"
#include "XAtomic.h"
/**
* @brief 原子环形队列虚函数表大小定义
* @note 继承自XCircularQueue的虚函数表大小，确保接口兼容性
*/
#define XLOCKFREEQUEUE_VTABLE_SIZE (XCIRCULARQUEUE_VTABLE_SIZE)
/**
* @brief 高性能无锁队列
* @details 基于环形队列实现，使用原子操作保证多线程环境下的线程安全，支持高效的入队/出队操作
* @param m_vector 底层存储向量，用于存储队列元素
* @param m_head 原子类型的队头索引，支持线程安全的读写操作
* @param m_tail 原子类型的队尾索引，支持线程安全的读写操作
*/
typedef struct XLockFreeQueue
{
	XVector m_vector;///< 底层向量容器，提供元素存储功能
	XCACHE_ALIGN XAtomic_size_t m_head;///< 原子化队头索引，标识队列头部位置
	XCACHE_ALIGN XAtomic_size_t m_tail;///< 原子化队尾索引，标识队列尾部位置
	// --- 无锁辅助字段 ---
	size_t m_index_bits;        ///< 用于存储索引的位数
	size_t m_index_mask;        ///< 索引掩码
	size_t m_version_mask;      ///< 版本号掩码
} XLockFreeQueue;
// ------------------------------ 类初始化与实例管理 ------------------------------
/**
* @brief 初始化原子环形队列的虚函数表
* @return 初始化完成的虚函数表指针XVtable*
* @note 绑定原子环形队列的线程安全虚函数实现，继承自XCircularQueue接口
*/
XVtable* XLockFreeQueue_class_init();
/**
* @brief 初始化原子环形队列实例
* @param this_queue 待初始化的原子环形队列实例指针
* @param typeSize 队列中元素的类型大小（字节数）
* @param count 队列的初始容量（可存储的最大元素数）
* @note 初始化底层向量、原子化队头和队尾索引，设置初始状态
*/
void XLockFreeQueue_init(XLockFreeQueue* this_queue, size_t typeSize, size_t count);
/**
* @brief 创建原子环形队列实例（基础函数）
* @param typeSize 元素的类型大小（字节数）
* @param count 队列的初始容量
* @return 创建成功的实例指针，失败返回NULL
* @note 动态分配内存并调用XLockFreeQueue_init完成初始化
*/
XLockFreeQueue* XLockFreeQueue_create_ex(XMemoryType memory,  size_t typeSize, size_t count);
/**
* @brief 类型安全的原子环形队列创建宏
* @param Type 元素数据类型（如int、float）
* @param count 队列的初始容量
* @return 创建成功的实例指针，失败返回NULL
* @note 自动推导元素类型大小，简化XLockFreeQueue_create的调用
*/
#define XLockFreeQueue_Create(Type, count) XLockFreeQueue_create(sizeof(Type), count)
// ------------------------------ 入队操作 ------------------------------
/**
* @brief 入队操作（拷贝语义，基于基类实现）
* @note 复用XCircularQueue的入队接口，通过原子操作保证线程安全
*/
#define XLockFreeQueue_Push_Base			XCircularQueue_Push_Base
/**
* @brief 入队操作（拷贝语义，基础版本）
* @note 复用XCircularQueue的基础入队接口，线程安全版本
*/
#define XLockFreeQueue_push_base			XCircularQueue_push_base
/**
* @brief 入队操作（移动语义，基于基类实现）
* @note 复用XQueueBase的移动入队接口，结合原子操作实现线程安全
*/
#define XLockFreeQueue_Push_Move_Base		XQueueBase_Push_Move_Base
/**
* @brief 入队操作（移动语义，基础版本）
* @note 复用XQueueBase的基础移动入队接口，线程安全版本
*/
#define XLockFreeQueue_push_move_base		XQueueBase_push_move_base
// ------------------------------ 出队与接收操作 ------------------------------
/**
* @brief 出队操作（基础版本）
* @note 复用XCircularQueue的出队接口，通过原子操作保证线程安全，移除队头元素
*/
#define XLockFreeQueue_pop_base			XCircularQueue_pop_base
/**
* @brief 接收并出队操作
* @note 复用XCircularQueue的接收接口，获取队头元素数据并安全移除，线程安全
*/
#define XLockFreeQueue_receive_base		XCircularQueue_receive_base
// ------------------------------ 元素访问 ------------------------------
/**
* @brief 获取队头元素（类型安全，基于基类实现）
* @note 复用XCircularQueue的类型安全接口，返回队头元素的引用，线程安全
*/
#define XLockFreeQueue_Top_Base			XCircularQueue_Top_Base
/**
* @brief 获取队头元素地址（基础版本）
* @note 复用XCircularQueue的接口，返回队头元素的地址，线程安全
*/
#define XLockFreeQueue_top_base			XCircularQueue_top_base
// ------------------------------ 队列状态查询 ------------------------------
/**
* @brief 判断队列是否已满
* @note 复用XCircularQueue的接口，通过原子操作判断队列是否达到容量上限，线程安全
*/
#define XLockFreeQueue_isFull_base		XCircularQueue_isFull_base
/**
* @brief 判断队列是否为空
* @note 复用XQueueBase的接口，通过原子操作判断队列是否无元素，线程安全
*/
#define XLockFreeQueue_isEmpty_base		XQueueBase_isEmpty_base
/**
* @brief 获取队列元素数量
* @note 复用XQueueBase的接口，通过原子操作计算当前元素个数，线程安全
*/
#define XLockFreeQueue_size_base			XQueueBase_size_base
/**
* @brief 获取队列容量
* @note 复用XQueueBase的接口，返回队列可容纳的最大元素数
*/
#define XLockFreeQueue_capacity_base		XQueueBase_capacity_base
/**
* @brief 获取元素类型大小
* @note 复用XQueueBase的接口，返回队列中元素的类型大小（字节数）
*/
#define XLockFreeQueue_typeSize_base		XQueueBase_typeSize_base
// ------------------------------ 容器管理 ------------------------------
/**
* @brief 拷贝容器
* @note 复用XQueueBase的接口，复制源队列的所有元素和状态，线程安全
*/
#define XLockFreeQueue_copy_base			XQueueBase_copy_base
/**
* @brief 移动容器资源
* @note 复用XQueueBase的接口，转移源队列的资源所有权至当前队列
*/
#define XLockFreeQueue_move_base			XQueueBase_move_base
/**
* @brief 反初始化容器
* @note 复用XQueueBase的接口，释放队列资源但不销毁实例本身
*/
#define XLockFreeQueue_deinit_base		XQueueBase_deinit_base
/**
* @brief 删除容器实例
* @note 复用XQueueBase的接口，释放队列资源并销毁实例
*/
void XLockFreeQueue_delete_base(XLockFreeQueue* this_queue);
/**
* @brief 清空容器元素
* @note 复用XQueueBase的接口，删除所有元素但保留队列结构，线程安全
*/
#define XLockFreeQueue_clear_base			XQueueBase_clear_base
/**
* @brief 交换两个容器内容
* @note 复用XQueueBase的接口，快速交换两个队列的内部数据
*/
#define XLockFreeQueue_swap_base			XQueueBase_swap_base

/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：以下宏仅做命名映射，不新增任何运行时行为；语义与被映射的原函数完全等价。
 *       目的是让熟悉 Qt 的调用方可以直接用 enqueue/dequeue/head/count/length/empty
 *       等 Qt 风格名称调用本容器。Qt 参考: QQueue<T> : QList<T>。
 * ============================================================================== */

/**
 * @brief 入队(拷贝语义)——Qt 别名，等价于 XLockFreeQueue_Push_Base
 * @param this_queue 队列实例指针
 * @param type       元素数据类型（如 int、float）
 * @param value      待插入的元素值（拷贝语义）
 * @note Qt 映射: QQueue::enqueue(const T&) → QList::append(const T&)
 */
#define XLockFreeQueue_Enqueue_Base       XLockFreeQueue_Push_Base
/**
 * @brief 入队(拷贝语义，函数式)——Qt 别名，等价于 XLockFreeQueue_push_base
 * @param this_queue 队列实例指针
 * @param pvData     指向源数据的指针（按容器 typeSize 拷贝）
 * @return 成功返回 true，失败(如满/参数非法)返回 false
 * @note Qt 映射: QQueue::enqueue → QList::append
 */
#define XLockFreeQueue_enqueue_base       XLockFreeQueue_push_base
/**
 * @brief 入队(移动语义)——Qt 别名，等价于 XLockFreeQueue_Push_Move_Base
 * @param this_queue 队列实例指针
 * @param type       元素数据类型
 * @param value      待插入的元素值（移动语义，所有权转移）
 * @note Qt 移动构造对应 QList::emplaceBack(std::move(v))
 */
#define XLockFreeQueue_Enqueue_Move_Base  XLockFreeQueue_Push_Move_Base
/**
 * @brief 入队(移动语义，函数式)——Qt 别名，等价于 XLockFreeQueue_push_move_base
 * @param this_queue 队列实例指针
 * @param pvData     指向源数据的指针（所有权移交给队列）
 * @return 成功返回 true，失败返回 false
 */
#define XLockFreeQueue_enqueue_move_base  XLockFreeQueue_push_move_base

/**
 * @brief 出队并返回队头——Qt 别名，等价于 XLockFreeQueue_receive_base
 * @param this_queue 队列实例指针
 * @param pvBuffer   接收队头元素的缓冲区（不可为 NULL）
 * @return 成功接收并移除队头返回 true；队列为空或参数非法返回 false
 * @note Qt 映射: QQueue::dequeue() → QList::takeFirst()，即 "先读后弹" 的原子语义
 */
#define XLockFreeQueue_dequeue_base       XLockFreeQueue_receive_base
/**
 * @brief 出队但不返回值——Qt 无直接对应，保留 pop 语义
 * @param this_queue 队列实例指针
 * @note 仅移除队头元素，若需要读取先用 XLockFreeQueue_head_base / XLockFreeQueue_Head_Base
 */
#define XLockFreeQueue_dequeue_void_base  XLockFreeQueue_pop_base

/**
 * @brief 获取队头(类型安全宏)——Qt 别名，等价于 XLockFreeQueue_Top_Base
 * @param this_queue 队列实例指针
 * @param Type       元素类型
 * @return 队头元素的引用（Type 类型）；仅读，不出队
 * @note Qt 映射: QQueue::head() → QList::first()
 */
#define XLockFreeQueue_Head_Base          XLockFreeQueue_Top_Base
/**
 * @brief 获取队头地址(函数式)——Qt 别名，等价于 XLockFreeQueue_top_base
 * @param this_queue 队列实例指针
 * @return 队头元素地址；队列为空返回 NULL；仅读，不出队
 */
#define XLockFreeQueue_head_base          XLockFreeQueue_top_base

/**
 * @brief 队列当前元素个数——Qt 别名，等价于 XLockFreeQueue_size_base
 * @param this_queue 队列实例指针
 * @return 元素个数
 * @note Qt 映射: QList::count()
 */
#define XLockFreeQueue_count_base         XLockFreeQueue_size_base
/**
 * @brief 队列当前元素个数——Qt 别名，等价于 XLockFreeQueue_size_base
 * @param this_queue 队列实例指针
 * @return 元素个数
 * @note Qt 映射: QList::length()，与 count() 完全等价，仅命名不同
 */
#define XLockFreeQueue_length_base        XLockFreeQueue_size_base

/**
 * @brief 判空——Qt 别名，等价于 XLockFreeQueue_isEmpty_base
 * @param this_queue 队列实例指针
 * @return 队列为空返回 true，否则返回 false
 * @note Qt 映射: QList::empty()（QList 同时提供 isEmpty，本项目仅将 empty 对齐 Qt）
 */
#define XLockFreeQueue_empty_base         XLockFreeQueue_isEmpty_base


#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XLockFreeQueue_create
#define XLockFreeQueue_create(...) XLockFreeQueue_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif
