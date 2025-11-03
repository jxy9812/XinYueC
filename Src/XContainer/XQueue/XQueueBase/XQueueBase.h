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
#include "XContainerObject.h"
/**
* @brief XQueueBase虚函数表大小定义
* @note 基于容器基类XContainerObject的虚函数表大小扩展，确定队列基类所需的虚函数表容量
*/
#define XQUEUEBASE_VTABLE_SIZE (XCLASS_VTABLE_GET_SIZE(XQueueBase))
/**
* @brief XQueueBase虚函数表枚举
* @details 定义队列基类操作对应的虚函数索引，扩展自容器基类的虚函数表
* @note 枚举值从容器基类虚函数表大小开始，避免索引冲突
*/
XCLASS_DEFINE_BEGING(XQueueBase)
XCLASS_DEFINE_ENUM(XQueueBase, Push) = XCLASS_VTABLE_GET_SIZE(XContainerObject),/** @brief 入队操作虚函数索引（拷贝/移动元素到队尾） */
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
	XContainerObject m_class;  ///< 继承自容器基类，包含通用容器属性（大小、容量、数据操作方法等）
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
{ type t = value; XQueueBase_push_base(this_vector, &t); }
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
{ type t = value; XQueueBase_push_move_base(this_vector, &t); }
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
// ------------------------------ 容器管理（继承自XContainerObject） ------------------------------
/**
* @brief 拷贝容器（基础版本）
* @note 继承自XContainerObject的拷贝操作，复制源容器的所有元素
*/
#define XQueueBase_copy_base              XContainerObject_copy_base
/**
* @brief 移动容器资源（基础版本，转移所有权）
* @note 继承自XContainerObject的移动操作，接管源容器的资源，源容器失效
*/
#define XQueueBase_move_base              XContainerObject_move_base
/**
* @brief 反初始化容器（基础版本）
* @note 继承自XContainerObject的反初始化操作，释放资源但不释放容器本身
*/
#define XQueueBase_deinit_base            XContainerObject_deinit_base
/**
* @brief 删除容器实例（基础版本）
* @note 继承自XContainerObject的删除操作，释放资源并销毁容器实例
*/
#define XQueueBase_delete_base            XContainerObject_delete_base
/**
* @brief 清空容器元素（基础版本）
* @note 继承自XContainerObject的清空操作，删除所有元素但保留容器结构
*/
#define XQueueBase_clear_base             XContainerObject_clear_base
/**
* @brief 判断容器是否为空（基础版本）
* @note 继承自XContainerObject的判空操作，无元素返回true，否则返回false
*/
#define XQueueBase_isEmpty_base           XContainerObject_isEmpty_base
/**
* @brief 获取容器元素数量（基础版本）
* @note 继承自XContainerObject的大小操作，返回当前存储的元素个数
*/
#define XQueueBase_size_base              XContainerObject_size_base
/**
* @brief 获取容器容量（基础版本）
* @note 继承自XContainerObject的容量操作，返回队列可容纳的最大元素数（有界队列）
*/
#define XQueueBase_capacity_base          XContainerObject_capacity_base
/**
* @brief 交换两个容器的内容（基础版本）
* @note 继承自XContainerObject的交换操作，快速交换两个容器的内部数据
*/
#define XQueueBase_swap_base              XContainerObject_swap_base
/**
* @brief 获取容器存储元素的类型大小（基础版本）
* @note 继承自XContainerObject的类型大小操作，返回元素类型的字节数
*/
#define XQueueBase_typeSize_base          XContainerObject_typeSize_base
/**
* @brief C++兼容性声明结束
*/
#ifdef __cplusplus
}
#endif
#endif  // !XQUEUEBASE_H