#ifndef XVtable_H
#define XVtable_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdbool.h>
#include<stdint.h>
#include <stddef.h>
#include"XTypes.h"
#include"XMemory.h"
#include"XClassConfig.h"
/**
 * @brief 虚函数表。
 *
 * data 只保存函数地址，不拥有函数本身。栈模式下 data 指向类初始化函数
 * 内的静态数组；堆模式下 data 由 XVtable 管理并随虚函数表释放。
 */
typedef struct
{
	void** data;
#if XCLASS_VTABLE_ENABLE_NAME
	char* name;                 /* 借用的类名，不由虚函数表释放。 */
#endif
	uint16_t size;              /* 当前已登记的函数槽位数。 */
	uint16_t capacity;          /* data 可容纳的最大函数槽位数。 */
	uint16_t isStack : 1;       /* data 是否指向静态存储区。 */
	uint16_t unused : 15;       /* 保留位，便于后续扩展。 */
}XVtable;

/**
 * @brief 设置虚函数表名称。
 * @param Vtable 目标虚函数表，不能为空。
 * @param Name 借用的名称指针，通常传入字符串字面量；虚函数表不释放它。
 */
#if XCLASS_VTABLE_ENABLE_NAME
#define XVTABLE_SET_NAME(Vtable, Name) \
	((Vtable)->name = (char*)(Name))
#else
#define XVTABLE_SET_NAME(Vtable, Name) ((void)(Vtable))
#endif
/**
 * @brief 获取虚函数表名称。
 * @param Vtable 虚函数表指针，可以为 NULL。
 * @return 已设置的借用名称；未启用名称或参数为 NULL 时返回 NULL。
 */
#if XCLASS_VTABLE_ENABLE_NAME
#define XVTABLE_GET_NAME(Vtable) \
	((Vtable) ? (Vtable)->name : NULL)
#else
#define XVTABLE_GET_NAME(Vtable) ((void)(Vtable), NULL)
#endif

/** @brief 创建一个可动态扩容的堆虚函数表。 */
XVtable* XVtable_create();
/**
 * @brief 初始化调用方提供的静态虚函数表。
 * @param this_vtable 虚函数表对象，生命周期必须覆盖所有使用者。
 * @param data 静态函数指针数组。
 * @param size data 中可用槽位数量。
 */
void XVtable_init_stack(XVtable* this_vtable, void** data, size_t size);
/** @brief 初始化调用方提供的堆虚函数表对象。 */
void XVtable_init(XVtable* this_vtable);
void XVtable_insert(XVtable* this_vtable, int64_t index, const void* func);
void XVtable_insert_array(XVtable* this_vtable, int64_t index, void* const* begin, size_t n);
void XVtable_append_array(XVtable* this_vtable, void* const* begin, size_t n);
void XVtable_append_vtable(XVtable* this_vtable, XVtable* table);
void XVtable_push_back(XVtable* this_vtable, const void* func);
void XVtable_pop_back(XVtable* this_vtable);
void XVtable_clear(XVtable* this_vtable);
bool XVtable_empty(XVtable* this_vtable);
size_t XVtable_size(XVtable* this_vtable);
/** @brief 安全读取虚函数槽位，索引越界时返回 NULL。 */
void* XVtable_at(XVtable* this_vtable, size_t index);
/** @brief 直接读取虚函数槽位；调用方必须保证索引有效。 */
#define XVtable_At(this_vtable, index) (((this_vtable)->data)[(index)])

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
//内存对齐宏
#define ALIGN_UP(size, align) (((size) + (align) - 1) & ~((align) - 1))
#ifdef __cplusplus
}
#endif
#endif// !XVtable_H
