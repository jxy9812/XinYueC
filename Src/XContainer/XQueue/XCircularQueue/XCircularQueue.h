#include"CXinYueConfig.h"
#if !defined(XCIRCULARQUEUE_H)&& XCircularQueue_ON
#define XCIRCULARQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif
#include "XQueueBase.h"
#include "XVector.h"
/**
* @brief 环形队列虚函数表大小定义
* @note 基于队列基类XQueueBase的虚函数表大小，确保接口兼容性
*/
#define XCIRCULARQUEUE_VTABLE_SIZE (XQUEUEBASE_VTABLE_SIZE)
/**
* @brief 环形队列结构体定义
* @details 基于向量容器实现的环形队列，支持固定容量或自动扩容模式
* @param m_vector 底层向量容器，用于存储队列元素
* @param m_autoExpansion 自动扩容标志，true表示队列满时自动扩容
* @param m_head 队头索引，标识队列头部元素位置
* @param m_tail 队尾索引，标识队列尾部元素的下一个位置
*/
typedef struct XCircularQueue
{
	XVector m_vector;///< 底层存储向量，提供连续内存空间
	bool    m_autoExpansion;///< 自动扩容开关，控制队列满时是否扩容
	size_t  m_head;///< 队头索引，指向当前队列头部元素
	size_t  m_tail;///< 队尾索引，指向当前队列尾部的下一个空位
} XCircularQueue;
// ------------------------------ 类初始化与实例管理 ------------------------------
/**
* @brief 初始化环形队列的虚函数表
* @return 初始化完成的虚函数表指针XVtable*
* @note 绑定环形队列的虚函数实现，继承自XQueueBase接口
*/
XVtable* XCircularQueue_class_init();
/**
* @brief 初始化环形队列实例
* @param this_queue 待初始化的环形队列实例指针
* @param typeSize 队列中元素的类型大小（字节数）
* @param count 队列的初始容量（可存储的最大元素数）
* @note 初始化底层向量、队头/队尾索引及自动扩容标志（默认关闭）
*/
void XCircularQueue_init(XCircularQueue* this_queue, size_t typeSize, size_t count);
/**
* @brief 创建环形队列实例（基础函数）
* @param typeSize 元素的类型大小（字节数）
* @param count 队列的初始容量
* @return 创建成功的实例指针，失败返回NULL
* @note 动态分配内存并调用XCircularQueue_init完成初始化
*/
XCircularQueue* XCircularQueue_create(size_t typeSize, size_t count);
/**
* @brief 类型安全的环形队列创建宏
* @param Type 元素数据类型（如int、float）
* @param count 队列的初始容量
* @return 创建成功的实例指针，失败返回NULL
* @note 自动推导元素类型大小，简化XCircularQueue_create的调用
*/
#define XCircularQueue_Create(Type, count) XCircularQueue_create(sizeof(Type), count)
/**
* @brief 从环形队列中删除指定数量的匹配元素
 * @param this_queue 环形队列实例指针
 * @param value 要删除的元素值指针
 * @param n 要删除的最大数量（0表示删除所有匹配元素）
 * @return 实际删除的元素数量
 * @note 使用compare函数遍历队列查找匹配元素，找到后移除并修复队列结构
 */
size_t XCircularQueue_remove(XCircularQueue* this_queue, const void* value, size_t n);
// ------------------------------ 配置操作 ------------------------------
/**
* @brief 设置环形队列是否开启自动扩容
* @param this_queue 环形队列实例指针
* @param autoExpansion 自动扩容开关（true为开启，false为关闭）
* @note 开启后，队列满时会自动扩容至原容量的1.5倍
*/
void XCircularQueue_setAutoExpansion(XCircularQueue* this_queue, bool autoExpansion);
// ------------------------------ 入队操作 ------------------------------
/**
* @brief 入队操作（拷贝语义，类型安全）
* @note 基于XQueueBase的入队接口，自动处理类型转换，拷贝元素到队尾
*/
#define XCircularQueue_Push_Base			XQueueBase_Push_Base
/**
* @brief 入队操作（拷贝语义，基础版本）
* @note 基于XQueueBase的基础入队接口，拷贝元素数据到队尾
*/
#define XCircularQueue_push_base			XQueueBase_push_base
/**
* @brief 入队操作（移动语义，类型安全）
* @note 基于XQueueBase的移动入队接口，自动处理类型转换，转移元素所有权到队尾
*/
#define XCircularQueue_Push_Move_Base		XQueueBase_Push_Move_Base
/**
* @brief 入队操作（移动语义，基础版本）
* @note 基于XQueueBase的基础移动入队接口，转移元素数据所有权到队尾
*/
#define XCircularQueue_push_move_base		XQueueBase_push_move_base
// ------------------------------ 出队与接收操作 ------------------------------
/**
* @brief 出队操作（基础版本）
* @note 基于XQueueBase的出队接口，移除并释放队头元素
*/
#define XCircularQueue_pop_base				XQueueBase_pop_base
/**
* @brief 接收并出队操作
* @note 基于XQueueBase的接收接口，拷贝队头元素数据到缓冲区并移除队头元素
*/
#define XCircularQueue_receive_base			XQueueBase_receive_base
// ------------------------------ 元素访问 ------------------------------
/**
* @brief 获取队头元素（类型安全）
* @note 基于XQueueBase的类型安全接口，返回队头元素的引用
*/
#define XCircularQueue_Top_Base				XQueueBase_Top_Base
/**
* @brief 获取队头元素地址（基础版本）
* @note 基于XQueueBase的接口，返回队头元素的内存地址
*/
#define XCircularQueue_top_base				XQueueBase_top_base
// ------------------------------ 队列状态查询 ------------------------------
/**
* @brief 判断队列是否已满
* @note 基于XQueueBase的接口，判断当前队列是否达到容量上限
*/
#define XCircularQueue_isFull_base			XQueueBase_isFull_base
/**
* @brief 判断队列是否为空
* @note 基于XQueueBase的接口，判断当前队列是否无元素
*/
#define XCircularQueue_isEmpty_base			XQueueBase_isEmpty_base
/**
* @brief 获取队列元素数量
* @note 基于XQueueBase的接口，返回当前队列中存储的元素个数
*/
#define XCircularQueue_size_base			XQueueBase_size_base
/**
* @brief 获取队列容量
* @note 基于XQueueBase的接口，返回队列可容纳的最大元素数
*/
#define XCircularQueue_capacity_base		XQueueBase_capacity_base
/**
* @brief 获取元素类型大小
* @note 基于XQueueBase的接口，返回队列中元素的类型大小（字节数）
*/
#define XCircularQueue_typeSize_base		XQueueBase_typeSize_base
// ------------------------------ 容器管理 ------------------------------
/**
* @brief 拷贝容器
* @note 基于XQueueBase的接口，复制源队列的所有元素和状态
*/
#define XCircularQueue_copy_base			XQueueBase_copy_base
/**
* @brief 移动容器资源
* @note 基于XQueueBase的接口，转移源队列的资源所有权至当前队列
*/
#define XCircularQueue_move_base			XQueueBase_move_base
/**
* @brief 反初始化容器
* @note 基于XQueueBase的接口，释放队列资源但不销毁实例本身
*/
#define XCircularQueue_deinit_base			XQueueBase_deinit_base
/**
* @brief 删除容器实例
* @note 基于XQueueBase的接口，释放队列资源并销毁实例
*/
#define XCircularQueue_delete_base			XQueueBase_delete_base
/**
* @brief 清空容器元素
* @note 基于XQueueBase的接口，删除所有元素但保留队列结构
*/
#define XCircularQueue_clear_base			XQueueBase_clear_base
/**
* @brief 交换两个容器内容
* @note 基于XQueueBase的接口，快速交换两个队列的内部数据
*/
#define XCircularQueue_swap_base			XQueueBase_swap_base

/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：以下宏仅做命名映射，不新增任何运行时行为；语义与被映射的原函数完全等价。
 *       目的是让熟悉 Qt 的调用方可以直接用 enqueue/dequeue/head/count/length/empty
 *       等 Qt 风格名称调用本容器。Qt 参考: QQueue<T> : QList<T>。
 * ============================================================================== */

/**
 * @brief 入队(拷贝语义)——Qt 别名，等价于 XCircularQueue_Push_Base
 * @param this_queue 队列实例指针
 * @param type       元素数据类型（如 int、float）
 * @param value      待插入的元素值（拷贝语义）
 * @note Qt 映射: QQueue::enqueue(const T&) → QList::append(const T&)
 */
#define XCircularQueue_Enqueue_Base       XCircularQueue_Push_Base
/**
 * @brief 入队(拷贝语义，函数式)——Qt 别名，等价于 XCircularQueue_push_base
 * @param this_queue 队列实例指针
 * @param pvData     指向源数据的指针（按容器 typeSize 拷贝）
 * @return 成功返回 true，失败(如满/参数非法)返回 false
 * @note Qt 映射: QQueue::enqueue → QList::append
 */
#define XCircularQueue_enqueue_base       XCircularQueue_push_base
/**
 * @brief 入队(移动语义)——Qt 别名，等价于 XCircularQueue_Push_Move_Base
 * @param this_queue 队列实例指针
 * @param type       元素数据类型
 * @param value      待插入的元素值（移动语义，所有权转移）
 * @note Qt 移动构造对应 QList::emplaceBack(std::move(v))
 */
#define XCircularQueue_Enqueue_Move_Base  XCircularQueue_Push_Move_Base
/**
 * @brief 入队(移动语义，函数式)——Qt 别名，等价于 XCircularQueue_push_move_base
 * @param this_queue 队列实例指针
 * @param pvData     指向源数据的指针（所有权移交给队列）
 * @return 成功返回 true，失败返回 false
 */
#define XCircularQueue_enqueue_move_base  XCircularQueue_push_move_base

/**
 * @brief 出队并返回队头——Qt 别名，等价于 XCircularQueue_receive_base
 * @param this_queue 队列实例指针
 * @param pvBuffer   接收队头元素的缓冲区（不可为 NULL）
 * @return 成功接收并移除队头返回 true；队列为空或参数非法返回 false
 * @note Qt 映射: QQueue::dequeue() → QList::takeFirst()，即 "先读后弹" 的原子语义
 */
#define XCircularQueue_dequeue_base       XCircularQueue_receive_base
/**
 * @brief 出队但不返回值——Qt 无直接对应，保留 pop 语义
 * @param this_queue 队列实例指针
 * @note 仅移除队头元素，若需要读取先用 XCircularQueue_head_base / XCircularQueue_Head_Base
 */
#define XCircularQueue_dequeue_void_base  XCircularQueue_pop_base

/**
 * @brief 获取队头(类型安全宏)——Qt 别名，等价于 XCircularQueue_Top_Base
 * @param this_queue 队列实例指针
 * @param Type       元素类型
 * @return 队头元素的引用（Type 类型）；仅读，不出队
 * @note Qt 映射: QQueue::head() → QList::first()
 */
#define XCircularQueue_Head_Base          XCircularQueue_Top_Base
/**
 * @brief 获取队头地址(函数式)——Qt 别名，等价于 XCircularQueue_top_base
 * @param this_queue 队列实例指针
 * @return 队头元素地址；队列为空返回 NULL；仅读，不出队
 */
#define XCircularQueue_head_base          XCircularQueue_top_base

/**
 * @brief 队列当前元素个数——Qt 别名，等价于 XCircularQueue_size_base
 * @param this_queue 队列实例指针
 * @return 元素个数
 * @note Qt 映射: QList::count()
 */
#define XCircularQueue_count_base         XCircularQueue_size_base
/**
 * @brief 队列当前元素个数——Qt 别名，等价于 XCircularQueue_size_base
 * @param this_queue 队列实例指针
 * @return 元素个数
 * @note Qt 映射: QList::length()，与 count() 完全等价，仅命名不同
 */
#define XCircularQueue_length_base        XCircularQueue_size_base

/**
 * @brief 判空——Qt 别名，等价于 XCircularQueue_isEmpty_base
 * @param this_queue 队列实例指针
 * @return 队列为空返回 true，否则返回 false
 * @note Qt 映射: QList::empty()（QList 同时提供 isEmpty，本项目仅将 empty 对齐 Qt）
 */
#define XCircularQueue_empty_base         XCircularQueue_isEmpty_base


#ifdef __cplusplus
}
#endif
#endif