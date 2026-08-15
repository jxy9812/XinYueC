#include"CXinYueConfig.h"
/**
* @brief 条件编译控制
* @note 防止头文件重复包含，且仅在XStack模块启用时编译
*/
#if !defined(XSTACK_H) && XStack_ON
#define XSTACK_H

/**
* @brief C++兼容声明
* @note 支持C++编译时的C语言链接规范，避免名称修饰问题
*/
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>    /** @brief 标准输入输出库，用于调试输出 */
#include <stdbool.h>  /** @brief 布尔类型支持，提供bool、true、false定义 */
#include "XVector.h"  /** @brief 依赖XVector作为底层存储容器，栈操作复用向量尾操作 */
#include "XStackBase.h"
#define XSTACK_VTABLE_SIZE (XSTACKBASE_VTABLE_SIZE)
/**
* @brief 栈结构体定义
* @details 采用组合复用设计，内嵌XVector作为底层存储容器
* @note 隐藏底层实现细节，对外暴露栈特有的接口
*/
typedef struct XStack
{
	XVector vector;  /** @brief 底层向量容器，用于存储栈元素 */
} XStack;

/**
* @brief 类初始化与创建接口
*/

/**
* @brief 初始化XStack的虚函数表
* @return 初始化后的虚函数表指针（复用XVector的虚函数表）
*/
XVtable* XStack_class_init();

/**
* @brief 创建栈实例
* @param typeSize 单个元素的类型大小（字节数，如sizeof(int)）
* @return 栈实例指针（失败返回NULL）
*/
XStack* XStack_create_ex(XMemoryType memory,  size_t typeSize);

/**
* @brief 类型安全的栈创建宏
* @param Type 元素数据类型（如int、float）
* @note 自动推导元素类型大小，简化创建过程
*/
#define XStack_Create(Type) XStack_create(sizeof(Type))

/**
* @brief 初始化已分配内存的栈实例
* @param this_stack 待初始化的栈指针
* @param typeSize 单个元素的类型大小（字节数）
* @note 需先手动分配栈实例内存，再调用此函数初始化
*/
void XStack_init(XStack* this_stack, size_t typeSize);

/**
* @brief 调整栈的容量大小
* @param this_stack 栈实例指针
* @param new_capacity 新的容量大小（元素个数）
* @return 成功返回true，失败返回false
* @note 如果new_capacity小于当前元素数量，多余元素将被截断
*/
bool XStack_resize(XStack* this_stack, size_t new_capacity);
/**
* @brief 核心栈操作接口（压栈、出栈、取栈顶）
*/

/**
* @brief 压栈（拷贝语义）
* @param this_stack 栈实例指针
* @param pvData 待压入的元素数据指针
* @note 宏定义
*/
#define XStack_push_base         XQueueBase_push_base

/**
* @brief 压栈（拷贝语义，命名风格差异）
* @param this_stack 栈实例指针
* @param pvData 待压入的元素数据指针
* @note 宏定义
*/
#define XStack_Push_Base         XStackBase_Push_Base

/**
* @brief 压栈（移动语义）
* @param this_stack 栈实例指针
* @param pvData 待移动的元素数据指针（所有权转移）
* @note 宏定义减少拷贝开销，提升性能
*/
#define XStack_push_move_base    XStackBase_push_move_base

/**
* @brief 出栈（删除栈顶元素）
* @param this_stack 栈实例指针
* @note 宏定义，移除并释放栈顶元素
*/
#define XStack_pop_base          XStackBase_pop_base

/**
* @brief 弹栈并接收栈顶元素到缓冲区（弹出+返回）
* @param this_stack 栈实例指针
* @param pvBuffer   接收栈顶元素的缓冲区（不可为 NULL）
* @return 成功接收并移除栈顶返回 true；栈为空或参数非法返回 false
* @note 对应 Qt QStack::pop() 的 "弹出并返回 T" 语义
*/
#define XStack_receive_base      XStackBase_receive_base


/**
* @brief 取栈顶元素（不删除）
* @param this_stack 栈实例指针
* @return 栈顶元素的指针（若栈空返回NULL）
* @note 宏定义，仅获取栈顶元素引用
*/
#define XStack_top_base          XStackBase_top_base

/**
* @brief 取栈顶元素（不删除，命名风格差异）
* @param this_stack 栈实例指针
* @return 栈顶元素的指针（若栈空返回NULL）
* @note 宏定义，功能同XStack_top_base
*/
#define XStack_Top_Base          XStackBase_Top_Base


/**
* @brief 通用容器操作接口（拷贝、移动、销毁等）
*/

/**
* @brief 拷贝栈（从源栈拷贝到目标栈）
* @param this_stackOne 目标栈指针
* @param this_stackTwo 源栈指针
* @note 宏定义，深拷贝所有元素
*/
#define XStack_copy_base         XStackBase_copy_base

/**
* @brief 移动容器资源（复用XVector的移动逻辑）
* @param this_stackOne 目标栈指针
* @param this_stackTwo 源栈指针
* @note 宏定义，转移资源所有权，避免深拷贝
*/
#define XStack_move_base         XStackBase_move_base

/**
* @brief 销毁栈（释放内部资源，保留实例本身）
* @param this_stack 栈实例指针
* @note 宏定义，释放元素数据，不释放栈实例内存
*/
#define XStack_deinit_base       XStackBase_deinit_base

/**
* @brief 删除栈（释放内部资源及实例本身）
* @param this_stack 栈实例指针
* @note 宏定义，释放所有资源（包括栈实例内存）
*/
#define XStack_delete_base       XStackBase_delete_base

/**
* @brief 清空栈（删除所有元素，保留容量）
* @param this_stack 栈实例指针
* @note 宏定义，元素数量置0，容量不变
*/
#define XStack_clear_base        XStackBase_clear_base


/**
* @brief 状态查询接口
*/

/**
* @brief 判断栈是否为空
* @param this_stack 栈实例指针
* @return 若栈空返回true，否则返回false
* @note 宏定义，等价于XVector_isEmpty_base，通过元素数量判断
*/
#define XStack_isEmpty_base      XVector_isEmpty_base

/**
* @brief 获取栈中元素数量
* @param this_stack 栈实例指针
* @return 元素个数（size_t类型）
* @note 宏定义，等价于XVector_size_base
*/
#define XStack_size_base         XVector_size_base

/**
* @brief 获取栈的容量（当前可容纳的最大元素数）
* @param this_stack 栈实例指针
* @return 容量值（size_t类型）
* @note 宏定义，等价于XVector_capacity_base
*/
#define XStack_capacity_base     XVector_capacity_base

/**
* @brief 交换两个栈的内容
* @param this_stackOne 第一个栈指针
* @param this_stackTwo 第二个栈指针
* @note 宏定义，等价于XVector_swap_base，交换元素、容量等所有属性
*/
#define XStack_swap_base         XVector_swap_base

/**
* @brief 获取栈中单个元素的类型大小
* @param this_stack 栈实例指针
* @return 元素类型大小（字节数，size_t类型）
* @note 宏定义，等价于XVector_typeSize_base
*/
#define XStack_typeSize_base     XVector_typeSize_base


/**
* @brief C++兼容声明结束
*/

/* ============================== Qt 6.8 命名对齐别名 ==============================
 * 说明：本文件的核心 API(push/pop/top)已与 Qt QStack 同名，天然对齐；
 *       此处仅补齐 QList 系列名称(count/length/empty)与 Qt QStack::pop 的
 *       "弹出并返回" 语义(映射到本项目的 receive_base)。所有别名仅做命名映射，
 *       不新增行为，语义与被映射函数完全等价。
 *       Qt 参考: QStack<T> : QList<T>。
 * ============================================================================== */

/**
 * @brief 弹栈并返回栈顶——Qt 别名，等价于 XStack_receive_base
 * @param this_stack 栈实例指针
 * @param pvBuffer   接收栈顶元素的缓冲区（不可为 NULL）
 * @return 成功接收并移除栈顶返回 true；栈为空或参数非法返回 false
 * @note Qt 映射: QStack::pop() 返回 T；本项目 XStack_pop_base 为 void 出栈，
 *       "读+弹" 语义使用本别名或直接调 XStack_receive_base
 */
#define XStack_pop_return_base    XStack_receive_base

/**
 * @brief 栈中当前元素个数——Qt 别名，等价于 XStack_size_base
 * @param this_stack 栈实例指针
 * @return 元素个数
 * @note Qt 映射: QList::count()
 */
#define XStack_count_base         XStack_size_base
/**
 * @brief 栈中当前元素个数——Qt 别名，等价于 XStack_size_base
 * @param this_stack 栈实例指针
 * @return 元素个数
 * @note Qt 映射: QList::length()，与 count() 完全等价，仅命名不同
 */
#define XStack_length_base        XStack_size_base

/**
 * @brief 判空——Qt 别名，等价于 XStack_isEmpty_base
 * @param this_stack 栈实例指针
 * @return 栈为空返回 true，否则返回 false
 * @note Qt 映射: QList::empty()（QList 同时提供 isEmpty，本项目仅将 empty 对齐 Qt）
 */
#define XStack_empty_base         XStack_isEmpty_base

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XStack_create
#define XStack_create(...) XStack_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif  // !XSTACK_H