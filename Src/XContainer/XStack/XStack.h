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


/**
* @brief XStack虚函数表枚举
* @details 定义栈操作对应的虚函数索引，映射到XVector的尾操作以实现LIFO特性
* @note 栈的核心操作复用XVector的尾操作（时间复杂度O(1)）
*/
enum XStackEnum
{
	EXStack_Push = EXVector_Push_Back,  /** @brief 压栈操作，映射到XVector的尾插 */
	EXStack_Pop = EXVector_Pop_Back,    /** @brief 出栈操作，映射到XVector的尾删 */
	EXStack_Top = EXVector_Back,        /** @brief 取栈顶操作，映射到XVector的取尾元素 */
};


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
XStack* XStack_create(size_t typeSize);

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
* @brief 核心栈操作接口（压栈、出栈、取栈顶）
*/

/**
* @brief 压栈（拷贝语义）
* @param this_stack 栈实例指针
* @param pvData 待压入的元素数据指针
* @note 宏定义，等价于XVector_push_back_base，通过拷贝方式添加元素到栈顶
*/
#define XStack_push_base         XVector_push_back_base

/**
* @brief 压栈（拷贝语义，命名风格差异）
* @param this_stack 栈实例指针
* @param pvData 待压入的元素数据指针
* @note 宏定义，等价于XVector_Push_Back_Base，功能同XStack_push_base
*/
#define XStack_Push_Base         XVector_Push_Back_Base

/**
* @brief 压栈（移动语义）
* @param this_stack 栈实例指针
* @param pvData 待移动的元素数据指针（所有权转移）
* @note 宏定义，等价于XVector_push_back_move_base，减少拷贝开销，提升性能
*/
#define XStack_push_move_base    XVector_push_back_move_base

/**
* @brief 出栈（删除栈顶元素）
* @param this_stack 栈实例指针
* @note 宏定义，等价于XVector_pop_back_base，移除并释放栈顶元素
*/
#define XStack_pop_base          XVector_pop_back_base

/**
* @brief 取栈顶元素（不删除）
* @param this_stack 栈实例指针
* @return 栈顶元素的指针（若栈空返回NULL）
* @note 宏定义，等价于XVector_back_base，仅获取栈顶元素引用
*/
#define XStack_top_base          XVector_back_base

/**
* @brief 取栈顶元素（不删除，命名风格差异）
* @param this_stack 栈实例指针
* @return 栈顶元素的指针（若栈空返回NULL）
* @note 宏定义，等价于XVector_Back_Base，功能同XStack_top_base
*/
#define XStack_Top_Base          XVector_Back_Base


/**
* @brief 通用容器操作接口（拷贝、移动、销毁等）
*/

/**
* @brief 拷贝栈（从源栈拷贝到目标栈）
* @param this_stackOne 目标栈指针
* @param this_stackTwo 源栈指针
* @note 宏定义，等价于XVector_copy_base，深拷贝所有元素
*/
#define XStack_copy_base         XVector_copy_base

/**
* @brief 逆序拷贝栈（源栈元素逆序拷贝到目标栈）
* @param this_stackOne 目标栈指针
* @param this_stackTwo 源栈指针
* @note 宏定义，等价于XVector_rcopy_base，拷贝后元素顺序与源栈相反
*/
#define XStack_rcopy_base        XVector_rcopy_base

/**
* @brief 移动容器资源（复用XVector的移动逻辑）
* @param this_stackOne 目标栈指针
* @param this_stackTwo 源栈指针
* @note 宏定义，等价于XVector_move_base，转移资源所有权，避免深拷贝
*/
#define XStack_move_base         XVector_move_base

/**
* @brief 销毁栈（释放内部资源，保留实例本身）
* @param this_stack 栈实例指针
* @note 宏定义，等价于XVector_deinit_base，释放元素数据，不释放栈实例内存
*/
#define XStack_deinit_base       XVector_deinit_base

/**
* @brief 删除栈（释放内部资源及实例本身）
* @param this_stack 栈实例指针
* @note 宏定义，等价于XVector_delete_base，释放所有资源（包括栈实例内存）
*/
#define XStack_delete_base       XVector_delete_base

/**
* @brief 清空栈（删除所有元素，保留容量）
* @param this_stack 栈实例指针
* @note 宏定义，等价于XVector_clear_base，元素数量置0，容量不变
*/
#define XStack_clear_base        XVector_clear_base


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
#ifdef __cplusplus
}
#endif

#endif  // !XSTACK_H