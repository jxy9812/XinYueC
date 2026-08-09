// XStackBase.h
#ifndef XSTACKBASE_H
#define XSTACKBASE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
* @brief 包含容器基类头文件
* @note 提供栈基类所需的容器基础功能定义
*/
#include "XQueueBase.h"

/**
* @brief XStackBase虚函数表大小定义
* @note 基于容器基类XContainer的虚函数表大小扩展，确定栈基类所需的虚函数表容量
*/
#define XSTACKBASE_VTABLE_SIZE XQUEUEBASE_VTABLE_SIZE

/**
* @brief XStackBase虚函数表枚举
* @details 定义栈基类操作对应的虚函数索引，扩展自容器基类的虚函数表
* @note 枚举值从容器基类虚函数表大小开始，避免索引冲突
*/
XCLASS_DEFINE_BEGING(XStackBase)
XCLASS_DEFINE_ENUM(XStackBase, Push) = XCLASS_VTABLE_GET_SIZE(XContainer), /**< @brief 压栈操作虚函数索引（拷贝/移动元素到栈顶） */
XCLASS_DEFINE_ENUM(XStackBase, Pop),  /**< @brief 弹栈操作虚函数索引（移除栈顶元素） */
XCLASS_DEFINE_ENUM(XStackBase, Top),  /**< @brief 获取栈顶元素虚函数索引 */
XCLASS_DEFINE_ENUM(XStackBase, Receive), /**< @brief 接收并弹栈操作虚函数索引（获取栈顶元素并移除） */
XCLASS_DEFINE_ENUM(XStackBase, IsFull), /**< @brief 判断栈是否满虚函数索引 */
XCLASS_DEFINE_END(XStackBase)

/**
* @brief 栈基类结构体定义
* @details 作为所有栈（如普通栈、无锁栈）的基类，继承自容器对象基类
* @note 封装通用栈操作接口，具体实现由派生类（如XStack、XLockFreeStack）提供
*/
typedef struct XStackBase
{
    XContainer m_class;  ///< 继承自容器基类，包含通用容器属性（大小、容量、数据操作方法等）
} XStackBase;

// ------------------------------ 插入操作 ------------------------------

/**
* @brief 类型安全的压栈宏（拷贝语义）
* @param this_stack 栈实例指针
* @param type 元素数据类型（如int、float）
* @param value 待插入的元素值
* @note 自动创建临时变量存储value，通过XStackBase_push_base实现压栈，简化类型转换
*/
#define XStackBase_Push_Base(this_stack, type, value) \
{ type t = value; XStackBase_push_base(this_stack, &t); }

/**
* @brief 压栈操作（拷贝语义，基础版本）
* @param this_stack 栈实例指针
* @param pvData 待插入的元素数据指针（拷贝源）
* @return 插入成功返回true，失败返回false
* @note 拷贝pvData指向的数据到栈顶部，线程不安全
*/
#define XStackBase_push_base            XQueueBase_push_base

/**
* @brief 类型安全的压栈宏（移动语义）
* @param this_stack 栈实例指针
* @param type 元素数据类型（如int、float）
* @param value 待插入的元素值（所有权转移）
* @note 自动创建临时变量存储value，通过XStackBase_push_move_base实现压栈，结合移动语义提升性能
*/
#define XStackBase_Push_Move_Base(this_stack, type, value) \
{ type t = value; XStackBase_push_move_base(this_stack, &t); }

/**
* @brief 压栈操作（移动语义，基础版本）
* @param this_stack 栈实例指针
* @param pvData 待插入的元素数据指针（移动源，所有权转移）
* @return 插入成功返回true，失败返回false
* @note 移动pvData指向的数据到栈顶部，减少拷贝开销，线程不安全
*/
#define XStackBase_push_move_base      XQueueBase_push_move_base

// ------------------------------ 删除操作 ------------------------------

/**
* @brief 弹栈操作（基础版本）
* @param this_stack 栈实例指针
* @note 移除并释放栈顶部的第一个元素，若栈为空则无操作
*/
#define XStackBase_pop_base             XQueueBase_pop_base

// ------------------------------ 元素访问与接收 ------------------------------

/**
* @brief 接收并弹栈操作（基础版本）
* @param this_stack 栈实例指针
* @param pvBuffer 接收元素数据的缓冲区指针
* @return 成功接收并移除栈顶元素返回true，失败（如栈为空）返回false
* @note 将栈顶元素数据拷贝到pvBuffer，然后执行弹栈操作
*/
#define XStackBase_receive_base         XQueueBase_receive_base

/**
* @brief 类型安全的获取栈顶元素宏
* @param this_stack 栈实例指针
* @param Type 元素数据类型（如int、float）
* @return 栈顶元素的引用（Type类型）
* @note 通过XStackBase_top_base获取栈顶元素地址并转换为指定类型，简化访问
*/
#define XStackBase_Top_Base(this_stack, Type) (*(Type*)XStackBase_top_base(this_stack))

/**
* @brief 获取栈顶元素地址（基础版本）
* @param this_stack 栈实例指针
* @return 成功返回栈顶元素的地址，失败（如栈为空）返回NULL
* @note 仅返回地址，不移除元素，需确保栈非空时使用
*/
#define XStackBase_top_base                 XQueueBase_top_base

// ------------------------------ 栈状态查询 ------------------------------

/**
* @brief 判断栈是否已满（基础版本）
* @param this_stack 栈实例指针
* @return 栈已满返回true，否则返回false
* @note 适用于有界栈，无界栈可能始终返回false
*/
#define XStackBase_isFull_base            XQueueBase_isFull_base

// ------------------------------ 容器管理（继承自XContainer） ------------------------------

/**
* @brief 拷贝容器（基础版本）
* @note 继承自XContainer的拷贝操作，复制源容器的所有元素
*/
#define XStackBase_copy_base              XContainer_copy_base

/**
* @brief 移动容器资源（基础版本，转移所有权）
* @note 继承自XContainer的移动操作，接管源容器的资源，源容器失效
*/
#define XStackBase_move_base              XContainer_move_base

/**
* @brief 反初始化容器（基础版本）
* @note 继承自XContainer的反初始化操作，释放资源但不释放容器本身
*/
#define XStackBase_deinit_base            XContainer_deinit_base

/**
* @brief 删除容器实例（基础版本）
* @note 继承自XContainer的删除操作，释放资源并销毁容器实例
*/
#define XStackBase_delete_base            XContainer_delete_base

/**
* @brief 清空容器元素（基础版本）
* @note 继承自XContainer的清空操作，删除所有元素但保留容器结构
*/
#define XStackBase_clear_base             XContainer_clear_base

/**
* @brief 判断容器是否为空（基础版本）
* @note 继承自XContainer的判空操作，无元素返回true，否则返回false
*/
#define XStackBase_isEmpty_base           XContainer_isEmpty_base

/**
* @brief 获取容器元素数量（基础版本）
* @note 继承自XContainer的大小操作，返回当前存储的元素个数
*/
#define XStackBase_size_base              XContainer_size_base

/**
* @brief 获取容器容量（基础版本）
* @note 继承自XContainer的容量操作，返回栈可容纳的最大元素数（有界栈）
*/
#define XStackBase_capacity_base          XContainer_capacity_base

/**
* @brief 交换两个容器的内容（基础版本）
* @note 继承自XContainer的交换操作，快速交换两个容器的内部数据
*/
#define XStackBase_swap_base              XContainer_swap_base

/**
* @brief 获取容器存储元素的类型大小（基础版本）
* @note 继承自XContainer的类型大小操作，返回元素类型的字节数
*/
#define XStackBase_typeSize_base          XContainer_typeSize_base


/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：本文件的核心 API(push/pop/top)已与 Qt QStack 同名，天然对齐；
 *       此处仅补齐 QList 系列名称(count/length/empty)与 Qt QStack::pop 的
 *       "弹出并返回" 语义(映射到本项目的 receive_base)。所有别名仅做命名映射，
 *       不新增行为，语义与被映射函数完全等价。
 *       Qt 参考: QStack<T> : QList<T>。
 * ============================================================================== */

/**
 * @brief 弹栈并返回栈顶——Qt 别名，等价于 XStackBase_receive_base
 * @param this_stack 栈实例指针
 * @param pvBuffer   接收栈顶元素的缓冲区（不可为 NULL）
 * @return 成功接收并移除栈顶返回 true；栈为空或参数非法返回 false
 * @note Qt 映射: QStack::pop() 返回 T；本项目 XStackBase_pop_base 为 void 出栈，
 *       "读+弹" 语义使用本别名或直接调 XStackBase_receive_base
 */
#define XStackBase_pop_return_base    XStackBase_receive_base

/**
 * @brief 栈中当前元素个数——Qt 别名，等价于 XStackBase_size_base
 * @param this_stack 栈实例指针
 * @return 元素个数
 * @note Qt 映射: QList::count()
 */
#define XStackBase_count_base         XStackBase_size_base
/**
 * @brief 栈中当前元素个数——Qt 别名，等价于 XStackBase_size_base
 * @param this_stack 栈实例指针
 * @return 元素个数
 * @note Qt 映射: QList::length()，与 count() 完全等价，仅命名不同
 */
#define XStackBase_length_base        XStackBase_size_base

/**
 * @brief 判空——Qt 别名，等价于 XStackBase_isEmpty_base
 * @param this_stack 栈实例指针
 * @return 栈为空返回 true，否则返回 false
 * @note Qt 映射: QList::empty()（QList 同时提供 isEmpty，本项目仅将 empty 对齐 Qt）
 */
#define XStackBase_empty_base         XStackBase_isEmpty_base

#ifdef __cplusplus
}
#endif

#endif // !XSTACKBASE_H