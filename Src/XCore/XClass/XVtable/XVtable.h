/**
 * @file       XVtable.h
 * @brief      虚函数表公开 API。
 * @details    XinYueC 对象模型基础设施：C 环境下的多态分派与类继承基础
 *             （对标 Qt 元对象系统的虚函数分派职责）。实现只依赖 XinYueC
 *             抽象层，禁止调用 Win32、POSIX、Qt 或其他平台 API。
 */
#ifndef XVtable_H
#define XVtable_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdbool.h>
#include<stdint.h>
#include <stddef.h>
#include"XTypes.h"
#include"XMemory.h"
#include"XClassConfig.h"
/**
 * @brief 虚函数表。
 *
 * m_data 只保存函数地址，不拥有函数本身。栈模式下 m_data 指向类初始化函数
 * 内的静态数组；堆模式下 m_data 由 XVtable 管理并随虚函数表释放。
 */
typedef struct
{
	void** m_data;
#if XCLASS_VTABLE_NAME_ON
	char* m_name;                 /* 借用的类名，不由虚函数表释放。 */
#endif
	uint16_t m_size;              /* 当前已登记的函数槽位数。 */
	uint16_t m_capacity;          /* m_data 可容纳的最大函数槽位数。 */
	uint16_t m_isStack : 1;       /* m_data 是否指向静态存储区。 */
	uint16_t m_unused : 15;       /* 保留位，便于后续扩展。 */
}XVtable;

/**
 * @brief 设置虚函数表名称。
 * @param Vtable 目标虚函数表，不能为空。
 * @param Name 借用的名称指针，通常传入字符串字面量；虚函数表不释放它。
 */
#if XCLASS_VTABLE_NAME_ON
#define XVTABLE_SET_NAME(Vtable, Name) \
	((Vtable)->m_name = (char*)(Name))
#else
#define XVTABLE_SET_NAME(Vtable, Name) ((void)(Vtable))
#endif
/**
 * @brief 获取虚函数表名称。
 * @param Vtable 虚函数表指针，可以为 NULL。
 * @return 已设置的借用名称；未启用名称或参数为 NULL 时返回 NULL。
 */
#if XCLASS_VTABLE_NAME_ON
#define XVTABLE_GET_NAME(Vtable) \
	((Vtable) ? (Vtable)->m_name : NULL)
#else
#define XVTABLE_GET_NAME(Vtable) ((void)(Vtable), NULL)
#endif

/**
 * @brief 创建一个可动态扩容的堆虚函数表。
 * @details 新表经 XVtable_init 置为堆模式，m_data 在首次登记函数时才分配；
 *          表只保存函数地址，不拥有函数本身。
 * @return 新建的 XVtable 指针（新对象，调用方拥有）；内存分配失败时返回 NULL。
 * @note 释放方式：表对象用 XFree_System 释放；堆模式扩容产生的 m_data 同样
 *       来自系统分配器，且模块未提供自动释放接口，销毁表对象前应先用
 *       XFree_System 释放非 NULL 的 m_data。作为 class_init 单例使用的表
 *       通常与进程同生命周期，无需手动释放。
 */
XVtable* XVtable_create();
/**
 * @brief 初始化调用方提供的静态虚函数表。
 * @param this_vtable 虚函数表对象，生命周期必须覆盖所有使用者。
 * @param data 静态函数指针数组。
 * @param size data 中可用槽位数量。
 */
void XVtable_init_stack(XVtable* this_vtable, void** data, size_t size);
/**
 * @brief 初始化调用方提供的堆虚函数表对象。
 * @details 将表置为空的堆模式（m_isStack 为 0），m_data 延迟到首次登记函数
 *          时才分配；对象自身可以在栈上，但其生命周期必须覆盖所有使用者。
 *          只应初始化一次，对已登记的表重复调用会丢弃原有内容。
 * @param this_vtable 目标虚函数表对象；为 NULL 时函数不执行任何操作。
 */
void XVtable_init(XVtable* this_vtable);
/**
 * @brief 在虚函数表的指定位置插入一个函数地址。
 * @param this_vtable 目标虚函数表，不能为 NULL。
 * @param index 插入位置，0 表示头部。
 * @param func 待登记的函数地址，不能为 NULL；表只借用该地址。
 * @note 堆表容量不足时自动扩容；栈表不会扩容，已登记槽位数不能超过
 *       XVtable_init_stack 给定的容量，超出属未定义行为。
 * @warning 当前实现仅支持 index 为 0 的头部插入；索引非法或表为空时函数
 *          直接返回，对象保持不变。
 */
void XVtable_insert(XVtable* this_vtable, int64_t index, const void* func);
/**
 * @brief 在虚函数表的指定位置插入一段函数地址数组。
 * @param this_vtable 目标虚函数表，不能为 NULL。
 * @param index 插入位置，0 表示头部。
 * @param begin 函数地址数组首地址，借用、不取得所有权；不能为 NULL。
 * @param n 插入的元素个数；为 0 时函数不执行任何操作。
 * @note 扩容约定同 XVtable_insert；索引非法或表为空时函数直接返回，
 *       对象保持不变。
 * @warning 当前实现仅支持 index 为 0 的头部插入。
 */
void XVtable_insert_array(XVtable* this_vtable, int64_t index, void* const* begin, size_t n);
/**
 * @brief 把一段函数地址数组依次追加到虚函数表末尾。
 * @param this_vtable 目标虚函数表，不能为 NULL。
 * @param begin 函数地址数组首地址，借用、不取得所有权；不能为 NULL。
 * @param n 追加的元素个数；为 0 时函数不执行任何操作。
 * @note 逐个经由 XVtable_push_back 完成，扩容约定同 XVtable_push_back。
 */
void XVtable_append_array(XVtable* this_vtable, void* const* begin, size_t n);
/**
 * @brief 把另一张虚函数表已登记的槽位依次追加到当前表末尾。
 * @param this_vtable 目标虚函数表，不能为 NULL。
 * @param table 来源虚函数表，不能为 NULL（实现直接读取其已登记槽位）。
 * @note 只拷贝函数地址值，两张表互不共享所有权；继承父类槽位的
 *       XVTABLE_INHERIT 宏即基于本函数。
 */
void XVtable_append_vtable(XVtable* this_vtable, XVtable* table);
/**
 * @brief 在虚函数表末尾登记一个函数地址。
 * @param this_vtable 目标虚函数表，不能为 NULL。
 * @param func 待登记的函数地址；实现不检查空指针，会原样存入槽位。
 * @note 堆表容量不足时自动扩容；栈表不会扩容，超出 XVtable_init_stack
 *       给定容量属未定义行为。
 */
void XVtable_push_back(XVtable* this_vtable, const void* func);
/**
 * @brief 移除虚函数表末尾的一个槽位。
 * @param this_vtable 目标虚函数表；为 NULL 或表为空时不执行任何操作。
 * @note 仅把已登记槽位数减一；不调用该函数、不释放内存、不缩容，容量
 *       供后续登记复用。
 */
void XVtable_pop_back(XVtable* this_vtable);
/**
 * @brief 清空虚函数表全部已登记的槽位。
 * @param this_vtable 目标虚函数表；为 NULL 或表为空时不执行任何操作。
 * @note 仅把已登记槽位数清零；不释放 m_data、不缩容，容量供后续复用。
 */
void XVtable_clear(XVtable* this_vtable);
/**
 * @brief 判断虚函数表是否没有已登记的槽位。
 * @param this_vtable 虚函数表指针，可以为 NULL。
 * @return 表为 NULL 或已登记槽位数为 0 时返回 true，否则返回 false。
 */
bool XVtable_empty(XVtable* this_vtable);
/**
 * @brief 获取虚函数表已登记的槽位数量。
 * @param this_vtable 虚函数表指针，可以为 NULL。
 * @return 已登记槽位数量；参数为 NULL 时返回 0。
 */
size_t XVtable_size(XVtable* this_vtable);
/**
 * @brief 安全读取虚函数槽位。
 * @param this_vtable 目标虚函数表，可以为 NULL。
 * @param index 槽位索引，从 0 开始。
 * @return 该槽位登记的函数地址（借用指针，表不拥有函数本身）；参数为
 *         NULL 或索引越界时返回 NULL。
 */
void* XVtable_at(XVtable* this_vtable, size_t index);
/** @brief 直接读取虚函数槽位；调用方必须保证索引有效。 */
#define XVtable_At(this_vtable, index) (((this_vtable)->m_data)[(index)])

/** @brief 开始定义 Class 的虚函数枚举。 */
#define XCLASS_DEFINE_BEGING(Class)  enum Class##VtableEnum{
/** @brief 声明一个虚函数槽位，Value 应使用稳定的 PascalCase 名称。 */
#define XCLASS_DEFINE_ENUM(Class,Value) E##Class##_##Value
/** @brief 结束当前类的虚函数枚举并生成槽位总数。 */
#define XCLASS_DEFINE_END(Class)    XCLASS_VTABLE_GET_SIZE(Class)};
/** @brief 结束派生类枚举，并从父类槽位数量开始编号。 */
#define XCLASS_DEFINE_EXTEND_END(Class,Parent)    XCLASS_VTABLE_GET_SIZE(Class)=XCLASS_VTABLE_GET_SIZE(Parent)};
/** @brief 获取类的虚函数槽位总数，用于静态表容量。 */
#define XCLASS_VTABLE_GET_SIZE(Class)   E##Class##_END_SIZE

/**
 * @brief 通过结构体成员的指针反推出该结构体的起始指针。
 *
 * 这是 Linux 内核中广为使用的经典宏，也是实现通用容器（链表、队列、红黑树等）
 * 和“面向对象风格”类型继承的基础设施。已知某个成员（member）在结构体（type）
 * 内部的地址（ptr），该宏可以在编译期计算出包含此成员的整个结构体的首地址。
 *
 * @param[in] ptr     指向结构体中某个成员的指针（该成员必须属于 type 类型）
 * @param[in] type    希望获取的结构体类型名称（例如 struct person）
 * @param[in] member  ptr 所指向的成员在结构体 type 中的名称
 *
 * @return 指向包含该成员的整个结构体的指针（类型为 type*）
 *
 * @note 宏内部使用 offsetof()，它会在编译时求值，不会产生运行时开销。
 * @note 必须包含头文件 <stddef.h> 以获得 offsetof 的定义。
 * @warning 若 ptr 为 NULL，本宏的行为是未定义的（虽然大多数实现会返回 NULL - offset，
 *          但不应依赖于此）。
 * @warning 调用者必须确保 ptr 确实指向 type 类型结构体中的 member 成员；
 *          否则会造成数据错乱且没有错误提示。
 *
 * @par 示例
 * @code
 * struct person {
 *     char name[16];
 *     int age;
 *     struct list_head node;   // 链表节点
 * };
 *
 * // 在链表遍历回调中，我们只能拿到 node 的指针
 * struct list_head *p = &some_person->node;
 *
 * // 通过 container_of 反向获取 person 结构体的起始地址
 * struct person *parent = container_of(p, struct person, node);
 *
 * // 现在可以访问 person 的其他成员
 * printf("age = %d\n", parent->age);
 * @endcode
 *
 * @see offsetof()
 * @see list_entry()  许多内核 API 中的同功能宏
 */
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
/**
 * @brief 把 size 向上取整到 align 的整数倍。
 * @param size 待对齐的数值。
 * @param align 对齐粒度，必须是 2 的幂，否则结果不正确。
 * @return 对齐后的数值，不小于 size；宏只做整数运算，无副作用。
 */
#define ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))
#ifdef __cplusplus
}
#endif
#endif// !XVtable_H
