#include"CXinYueConfig.h"
#if !defined(XQUEUEBASE_H)&& XQueue_ON
#define XQUEUEBASE_H
#ifdef __cplusplus
extern "C" {
#endif
/**
* @brief 包含容器基类头文件
* @note 提供队列基类所需的容器基础功能定义
*/
#include "XContainer.h"
/**
* @brief XQueueBase虚函数表大小定义
* @note 基于容器基类XContainer的虚函数表大小扩展，确定队列基类所需的虚函数表容量
*/
#define XQUEUEBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XQueueBase))
/**
* @brief XQueueBase虚函数表枚举
* @details 定义队列基类操作对应的虚函数索引，扩展自容器基类的虚函数表
* @note 枚举值从容器基类虚函数表大小开始，避免索引冲突
*/
XCLASS_DEFINE_BEGING(XQueueBase)
XCLASS_DEFINE_ENUM(XQueueBase, Push) = XCLASS_VTABLE_GET_SIZE(XContainer),/** @brief 入队操作虚函数索引（拷贝/移动元素到队尾） */
XCLASS_DEFINE_ENUM(XQueueBase, Pop),/** @brief 出队操作虚函数索引（移除队头元素） */
XCLASS_DEFINE_ENUM(XQueueBase, Top),/** @brief 获取队头元素虚函数索引 */
XCLASS_DEFINE_ENUM(XQueueBase, Receive),/** @brief 接收并出队操作虚函数索引（获取队头元素并移除） */
XCLASS_DEFINE_ENUM(XQueueBase, IsFull),/** @brief 判断队列是否满虚函数索引 */
XCLASS_DEFINE_END(XQueueBase)
/**
* @brief 队列基类结构体定义
* @details 作为所有队列（如普通队列、优先队列）的基类，继承自容器对象基类
* @note 封装通用队列操作接口，具体实现由派生类（如XQueue、XPriorityQueue）提供
*/
typedef struct XQueueBase
{
	XContainer m_class;  ///< 继承自容器基类，包含通用容器属性（大小、容量、数据操作方法等）
} XQueueBase;
// ------------------------------ 插入操作 ------------------------------
/**
* @brief 类型安全的入队宏（拷贝语义）
* @param this_queue 队列实例指针
* @param type 元素数据类型（如int、float）
* @param value 待插入的元素值
* @note 自动创建临时变量存储value，通过XQueueBase_push_base实现入队，简化类型转换
*/
#define XQueueBase_Push_Base(this_queue, type, value) \
{ type t = value; XQueueBase_push_base(this_queue, &t); }
/**
* @brief 入队操作（拷贝语义，基础版本）
* @param this_queue 队列实例指针
* @param pvData 待插入的元素数据指针（拷贝源）
* @return 插入成功返回true，失败返回false
* @note 拷贝pvData指向的数据到队列尾部，线程不安全
*/
bool XQueueBase_push_base(XQueueBase* this_queue, void* pvData);
/**
* @brief 类型安全的入队宏（移动语义）
* @param this_queue 队列实例指针
* @param type 元素数据类型（如int、float）
* @param value 待插入的元素值（所有权转移）
* @note 自动创建临时变量存储value，通过XQueueBase_push_move_base实现入队，结合移动语义提升性能
*/
#define XQueueBase_Push_Move_Base(this_queue, type, value) \
{ type t = value; XQueueBase_push_move_base(this_queue, &t); }
/**
* @brief 入队操作（移动语义，基础版本）
* @param this_queue 队列实例指针
* @param pvData 待插入的元素数据指针（移动源，所有权转移）
* @return 插入成功返回true，失败返回false
* @note 移动pvData指向的数据到队列尾部，减少拷贝开销，线程不安全
*/
bool XQueueBase_push_move_base(XQueueBase* this_queue, void* pvData);
// ------------------------------ 删除操作 ------------------------------
/**
* @brief 出队操作（基础版本）
* @param this_queue 队列实例指针
* @note 移除并释放队列头部的第一个元素，若队列为空则无操作
*/
void XQueueBase_pop_base(XQueueBase* this_queue);
// ------------------------------ 元素访问与接收 ------------------------------
/**
* @brief 接收并出队操作（基础版本）
* @param this_queue 队列实例指针
* @param pvBuffer 接收元素数据的缓冲区指针
* @return 成功接收并移除队头元素返回true，失败（如队列为空）返回false
* @note 将队头元素数据拷贝到pvBuffer，然后执行出队操作
*/
bool XQueueBase_receive_base(XQueueBase* this_queue, void* pvBuffer);
/**
* @brief 类型安全的获取队头元素宏
* @param this_queue 队列实例指针
* @param Type 元素数据类型（如int、float）
* @return 队头元素的引用（Type类型）
* @note 通过XQueueBase_top_base获取队头元素地址并转换为指定类型，简化访问
*/
#define XQueueBase_Top_Base(this_queue, Type) (*(Type*)XQueueBase_top_base(this_queue))
/**
* @brief 获取队头元素地址（基础版本）
* @param this_queue 队列实例指针
* @return 成功返回队头元素的地址，失败（如队列为空）返回NULL
* @note 仅返回地址，不移除元素，需确保队列非空时使用
*/
void* XQueueBase_top_base(XQueueBase* this_queue);
// ------------------------------ 队列状态查询 ------------------------------
/**
* @brief 判断队列是否已满（基础版本）
* @param this_queue 队列实例指针
* @return 队列已满返回true，否则返回false
* @note 适用于有界队列，无界队列可能始终返回false
*/
bool XQueueBase_isFull_base(XQueueBase* this_queue);
// ------------------------------ 容器管理（继承自XContainer） ------------------------------
/**
* @brief 反初始化容器（基础版本）
* @note 继承自XContainer的反初始化操作，释放资源但不释放容器本身
*/
#define XQueueBase_deinit_base            XContainer_deinit_base
/**
* @brief 删除容器实例（基础版本）
* @note 继承自XContainer的删除操作，释放资源并销毁容器实例
*/
#define XQueueBase_delete_base            XContainer_delete_base
/**
* @brief 清空容器元素（基础版本）
* @note 继承自XContainer的清空操作，删除所有元素但保留容器结构
*/
#define XQueueBase_clear_base             XContainer_clear_base
/**
* @brief 判断容器是否为空（基础版本）
* @note 继承自XContainer的判空操作，无元素返回true，否则返回false
*/
#define XQueueBase_isEmpty_base           XContainer_isEmpty_base
/**
* @brief 获取容器元素数量（基础版本）
* @note 继承自XContainer的大小操作，返回当前存储的元素个数
*/
#define XQueueBase_size_base              XContainer_size_base
/**
* @brief 获取容器容量（基础版本）
* @note 继承自XContainer的容量操作，返回队列可容纳的最大元素数（有界队列）
*/
#define XQueueBase_capacity_base          XContainer_capacity_base
/**
* @brief 交换两个容器的内容（基础版本）
* @note 继承自XContainer的交换操作，快速交换两个容器的内部数据
*/
#define XQueueBase_swap_base              XContainer_swap_base
/**
* @brief 获取容器存储元素的类型大小（基础版本）
* @note 继承自XContainer的类型大小操作，返回元素类型的字节数
*/
#define XQueueBase_typeSize_base          XContainer_typeSize_base
/**
* @brief C++兼容性声明结束
*/

/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：以下宏仅做命名映射，不新增任何运行时行为；语义与被映射的原函数完全等价。
 *       目的是让熟悉 Qt 的调用方可以直接用 enqueue/dequeue/head/count/length/empty
 *       等 Qt 风格名称调用本容器。Qt 参考: QQueue<T> : QList<T>。
 * ============================================================================== */

/**
 * @brief 入队(拷贝语义)——Qt 别名，等价于 XQueueBase_Push_Base
 * @param this_queue 队列实例指针
 * @param type       元素数据类型（如 int、float）
 * @param value      待插入的元素值（拷贝语义）
 * @note Qt 映射: QQueue::enqueue(const T&) → QList::append(const T&)
 */
#define XQueueBase_Enqueue_Base       XQueueBase_Push_Base
/**
 * @brief 入队(拷贝语义，函数式)——Qt 别名，等价于 XQueueBase_push_base
 * @param this_queue 队列实例指针
 * @param pvData     指向源数据的指针（按容器 typeSize 拷贝）
 * @return 成功返回 true，失败(如满/参数非法)返回 false
 * @note Qt 映射: QQueue::enqueue → QList::append
 */
#define XQueueBase_enqueue_base       XQueueBase_push_base
/**
 * @brief 入队(移动语义)——Qt 别名，等价于 XQueueBase_Push_Move_Base
 * @param this_queue 队列实例指针
 * @param type       元素数据类型
 * @param value      待插入的元素值（移动语义，所有权转移）
 * @note Qt 移动构造对应 QList::emplaceBack(std::move(v))
 */
#define XQueueBase_Enqueue_Move_Base  XQueueBase_Push_Move_Base
/**
 * @brief 入队(移动语义，函数式)——Qt 别名，等价于 XQueueBase_push_move_base
 * @param this_queue 队列实例指针
 * @param pvData     指向源数据的指针（所有权移交给队列）
 * @return 成功返回 true，失败返回 false
 */
#define XQueueBase_enqueue_move_base  XQueueBase_push_move_base

/**
 * @brief 出队并返回队头——Qt 别名，等价于 XQueueBase_receive_base
 * @param this_queue 队列实例指针
 * @param pvBuffer   接收队头元素的缓冲区（不可为 NULL）
 * @return 成功接收并移除队头返回 true；队列为空或参数非法返回 false
 * @note Qt 映射: QQueue::dequeue() → QList::takeFirst()，即 "先读后弹" 的原子语义
 */
#define XQueueBase_dequeue_base       XQueueBase_receive_base
/**
 * @brief 出队但不返回值——Qt 无直接对应，保留 pop 语义
 * @param this_queue 队列实例指针
 * @note 仅移除队头元素，若需要读取先用 XQueueBase_head_base / XQueueBase_Head_Base
 */
#define XQueueBase_dequeue_void_base  XQueueBase_pop_base

/**
 * @brief 获取队头(类型安全宏)——Qt 别名，等价于 XQueueBase_Top_Base
 * @param this_queue 队列实例指针
 * @param Type       元素类型
 * @return 队头元素的引用（Type 类型）；仅读，不出队
 * @note Qt 映射: QQueue::head() → QList::first()
 */
#define XQueueBase_Head_Base          XQueueBase_Top_Base
/**
 * @brief 获取队头地址(函数式)——Qt 别名，等价于 XQueueBase_top_base
 * @param this_queue 队列实例指针
 * @return 队头元素地址；队列为空返回 NULL；仅读，不出队
 */
#define XQueueBase_head_base          XQueueBase_top_base

/**
 * @brief 队列当前元素个数——Qt 别名，等价于 XQueueBase_size_base
 * @param this_queue 队列实例指针
 * @return 元素个数
 * @note Qt 映射: QList::count()
 */
#define XQueueBase_count_base         XQueueBase_size_base
/**
 * @brief 队列当前元素个数——Qt 别名，等价于 XQueueBase_size_base
 * @param this_queue 队列实例指针
 * @return 元素个数
 * @note Qt 映射: QList::length()，与 count() 完全等价，仅命名不同
 */
#define XQueueBase_length_base        XQueueBase_size_base

/**
 * @brief 判空——Qt 别名，等价于 XQueueBase_isEmpty_base
 * @param this_queue 队列实例指针
 * @return 队列为空返回 true，否则返回 false
 * @note Qt 映射: QList::empty()（QList 同时提供 isEmpty，本项目仅将 empty 对齐 Qt）
 */
#define XQueueBase_empty_base         XQueueBase_isEmpty_base


#ifdef __cplusplus
}
#endif
#endif  // !XQUEUEBASE_H