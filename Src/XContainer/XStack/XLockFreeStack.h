#ifndef XLOCKFREESTACK_H
#define XLOCKFREESTACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XStackBase.h"
#include "XAtomic.h"
#include "XVector.h"
/**
* @brief 无锁栈虚函数表大小定义
* @note 继承自XStackBase的虚函数表大小
*/
#define XLOCKFREESTACK_VTABLE_SIZE (XSTACKBASE_VTABLE_SIZE)

/**
* @brief 高性能无锁栈
* @details 基于数组实现，使用原子操作保证多线程环境下的线程安全
* @param m_vector 底层存储向量，用于存储栈元素
* @param m_top 原子类型的栈顶索引，支持线程安全的读写操作
* @param m_index_bits 索引位数，用于版本号打包
* @param m_index_mask 索引掩码，用于从打包值中提取索引
* @param m_version_mask 版本号掩码，用于ABA问题防护
*/
typedef struct XLockFreeStack
{
    XVector m_vector;           ///< 底层向量容器，提供元素存储功能
    XAtomic_size_t m_top;       ///< 原子化栈顶索引（包含版本号），标识栈顶位置
    size_t m_index_bits;        ///< 索引位数
    size_t m_index_mask;        ///< 索引掩码
    size_t m_version_mask;      ///< 版本号掩码
} XLockFreeStack;

// ------------------------------ 类初始化与实例管理 ------------------------------

/**
* @brief 初始化无锁栈的虚函数表
* @return 初始化完成的虚函数表指针XVtable*
* @note 绑定无锁栈的线程安全虚函数实现，继承自XStackBase接口
*/
XVtable* XLockFreeStack_class_init();

/**
* @brief 初始化无锁栈实例
* @param this_stack 待初始化的无锁栈实例指针
* @param typeSize 栈中元素的类型大小（字节数）
* @param capacity 栈的最大容量
* @note 初始化底层向量、原子化栈顶索引，设置初始状态
*/
void XLockFreeStack_init(XLockFreeStack* this_stack, size_t typeSize, size_t capacity);

/**
* @brief 创建无锁栈实例（基础函数）
* @param typeSize 元素的类型大小（字节数）
* @param capacity 栈的最大容量
* @return 创建成功的实例指针，失败返回NULL
* @note 动态分配内存并调用XLockFreeStack_init完成初始化
*/
XLockFreeStack* XLockFreeStack_create(size_t typeSize, size_t capacity);

/**
* @brief 类型安全的无锁栈创建宏
* @param Type 元素数据类型（如int、float）
* @param capacity 栈的最大容量
* @return 创建成功的实例指针，失败返回NULL
* @note 自动推导元素类型大小，简化XLockFreeStack_create的调用
*/
#define XLockFreeStack_Create(Type, capacity) XLockFreeStack_create(sizeof(Type), capacity)

// ------------------------------ 压栈操作 ------------------------------

/**
* @brief 压栈操作（拷贝语义，基于基类实现）
* @note 复用XStackBase的压栈接口，通过原子操作保证线程安全
*/
#define XLockFreeStack_Push_Base            XStackBase_Push_Base

/**
* @brief 压栈操作（拷贝语义，基础版本）
* @note 复用XStackBase的基础压栈接口，线程安全版本
*/
#define XLockFreeStack_push_base            XStackBase_push_base

/**
* @brief 压栈操作（移动语义，基于基类实现）
* @note 复用XStackBase的移动压栈接口，结合原子操作实现线程安全
*/
#define XLockFreeStack_Push_Move_Base       XStackBase_Push_Move_Base

/**
* @brief 压栈操作（移动语义，基础版本）
* @note 复用XStackBase的基础移动压栈接口，线程安全版本
*/
#define XLockFreeStack_push_move_base       XStackBase_push_move_base

// ------------------------------ 弹栈与接收操作 ------------------------------

/**
* @brief 弹栈操作（基础版本）
* @note 复用XStackBase的弹栈接口，通过原子操作保证线程安全，移除栈顶元素
*/
#define XLockFreeStack_pop_base             XStackBase_pop_base

/**
* @brief 接收并弹栈操作
* @note 复用XStackBase的接收接口，获取栈顶元素数据并安全移除，线程安全
*/
#define XLockFreeStack_receive_base         XStackBase_receive_base

// ------------------------------ 元素访问 ------------------------------

/**
* @brief 获取栈顶元素（类型安全，基于基类实现）
* @note 复用XStackBase的类型安全接口，返回栈顶元素的引用，线程安全
*/
#define XLockFreeStack_Top_Base             XStackBase_Top_Base

/**
* @brief 获取栈顶元素地址（基础版本）
* @note 复用XStackBase的接口，返回栈顶元素的地址，线程安全
*/
#define XLockFreeStack_top_base             XStackBase_top_base

// ------------------------------ 栈状态查询 ------------------------------

/**
* @brief 判断栈是否已满
* @note 通过原子操作判断栈是否达到容量上限，线程安全
*/
#define XLockFreeStack_isFull_base          XStackBase_isFull_base

/**
* @brief 判断栈是否为空
* @note 通过原子操作判断栈是否无元素，线程安全
*/
#define XLockFreeStack_isEmpty_base         XStackBase_isEmpty_base

/**
* @brief 获取栈元素数量
* @note 通过原子操作计算当前元素个数，线程安全
*/
#define XLockFreeStack_size_base            XStackBase_size_base

/**
* @brief 获取栈容量
* @note 返回栈可容纳的最大元素数
*/
#define XLockFreeStack_capacity_base        XStackBase_capacity_base

/**
* @brief 获取元素类型大小
* @note 返回栈中元素的类型大小（字节数）
*/
#define XLockFreeStack_typeSize_base        XStackBase_typeSize_base

// ------------------------------ 容器管理 ------------------------------

/**
* @brief 拷贝容器
* @note 复用XStackBase的接口，复制源栈的所有元素和状态，线程安全
*/
#define XLockFreeStack_copy_base            XStackBase_copy_base

/**
* @brief 移动容器资源
* @note 复用XStackBase的接口，转移源栈的资源所有权至当前栈
*/
#define XLockFreeStack_move_base            XStackBase_move_base

/**
* @brief 反初始化容器
* @note 复用XStackBase的接口，释放栈资源但不销毁实例本身
*/
#define XLockFreeStack_deinit_base          XStackBase_deinit_base

/**
* @brief 删除容器实例
* @note 复用XStackBase的接口，释放栈资源并销毁实例
*/
#define XLockFreeStack_delete_base          XStackBase_delete_base

/**
* @brief 清空容器元素
* @note 复用XStackBase的接口，删除所有元素但保留栈结构，线程安全
*/
#define XLockFreeStack_clear_base           XStackBase_clear_base

/**
* @brief 交换两个容器内容
* @note 复用XStackBase的接口，快速交换两个栈的内部数据
*/
#define XLockFreeStack_swap_base            XStackBase_swap_base

#ifdef __cplusplus
}
#endif

#endif // !XLOCKFREESTACK_H