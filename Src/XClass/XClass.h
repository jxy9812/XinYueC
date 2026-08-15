#ifndef XCLASS_H
#define XCLASS_H
#ifdef __cplusplus
extern "C" {
#endif
#include"XVtable.h"
#include"CXinYueConfig.h"
#include"XPrintf.h"
#include"XMemory.h"
#include<stdlib.h>
/** @brief XClass 基类虚函数表的固定槽位数量。 */
#define XCLASS_VTABLE_SIZE XCLASS_VTABLE_GET_SIZE(XClass)
/* XClass 基类虚函数槽位；子类通过 XVTABLE_INHERIT_XCLASS 继承。 */
XCLASS_DEFINE_BEGING(XClass)
XCLASS_DEFINE_ENUM(XClass, Copy),
XCLASS_DEFINE_ENUM(XClass, Move),
XCLASS_DEFINE_ENUM(XClass, Deinit),
XCLASS_DEFINE_END(XClass)
/**
 * @brief 所有 XClass 风格对象的公共基类。
 *
 * m_vtable 指向只读逻辑上的类虚函数表，m_memory 记录对象对应的内存方法；
 * m_is_heap 为 0 表示对象由 *_init 初始化，通常位于栈或外部存储中，1 表示
 * 对象由 *_create 在堆上创建。对象本身不拥有虚函数表，类初始化函数返回的
 * 表通常是共享的。
 */
typedef struct XClass
{
	XVtable* m_vtable;
	XMemory* m_memory;
	uint32_t m_is_heap : 1;
	uint32_t m_reserved : 31;
}XClass;

/** @brief 按 XClass 默认内存池创建指定类型的对象。 */
#define XClass_Malloc(Type) \
	((Type*)XMemory_malloc(sizeof(Type), XCLASS_DEFAULT_MEMORY_TYPE))

/** @brief 按枚举槽位读取虚函数；调用方必须保证表、槽位和函数类型有效。 */
#define XVtableGetFunc(Vtable, Offset, Type) \
	((Type)((((XVtable*)(Vtable))->data)[(Offset)]))
/** @brief 取得对象的虚函数表；Object 必须指向 XClass 或其派生对象。 */
#define XClassGetVtable(Object) (((XClass*)(Object))->m_vtable)
/** @brief 把对象的虚函数表设置为指定类的共享虚函数表。 */
#define XClassSetVtable(Object, Type) \
	(XClassGetVtable(Object) = Type##_class_init())

/** @brief 设置默认类名；名称为借用指针，通常传入字符串字面量。 */
#define XCLASS_SET_CLASS_NAME_DEFAULT(Name) \
	XVTABLE_SET_NAME(XVTABLE_DEFAULT, (Name))
/** @brief 获取默认类名；未设置或关闭类名配置时返回 NULL。 */
#define XCLASS_GET_CLASS_NAME_DEFAULT() \
	XVTABLE_GET_NAME(XVTABLE_DEFAULT)

/** @brief 判断对象是否尚未绑定虚函数表；Object 不能为空。 */
#define XClassIsVtableNull(Object) (XClassGetVtable(Object) == NULL)
/** @brief 通过对象取得指定类型的虚函数；调用方负责保证槽位已实现。 */
#define XClassGetVirtualFunc(Object, Offset, Type) \
	XVtableGetFunc(XClassGetVtable(Object), (Offset), Type)

/** @brief 判断表达式是否为空，并在开启错误输出时记录调用位置。 */
#define ISNULL(args, str) \
	(ArgIsNULL((args), #args, (str), __FUNCTION__, __FILE__, __LINE__))
/** @brief 参数为空时立即终止当前进程；适用于不可恢复的内部错误。 */
#define XAssert(args, str) do { if (ISNULL(args, str)) exit(-1); } while (0)
/** @brief 空指针检查的实际实现。 */
bool ArgIsNULL(const void* args, const char* argsName, const char* str,
	const char* funcName, const char* filePath, int line);

/**
 * @brief 声明一个类的共享虚函数表并在已初始化时直接返回。
 *
 * 该宏必须放在 class_init 函数开头，Vtable 必须是函数内的静态指针名。
 */
#define XVTABLE_CREAT(Vtable)  \
	static XVtable* Vtable = NULL; \
	if (Vtable) return Vtable;
/** @brief 使用堆内存创建默认虚函数表。 */
#define XVTABLE_HEAP_INIT(Vtable)\
	Vtable = XVtable_create();
/**
 * @brief 使用静态存储区初始化虚函数表。
 *
 * Size 必须是编译期常量；静态数组只分配一次，表本身不会自动扩容。
 */
#define XVTABLE_STACK_INIT(Vtable,Size)\
{\
	static XVtable vtable;\
	static void* vtable_data[Size];\
	Vtable = &vtable;\
	XVtable_init_stack(Vtable, vtable_data, sizeof(vtable_data) / sizeof(vtable_data[0]));\
}
/** @brief 把父类虚函数槽位追加到当前表。 */
#define XVTABLE_INHERIT(Vtable, VtableBase) \
	do { XVtable_append_vtable((Vtable), (VtableBase)); } while (0)
/** @brief 将一个槽位替换为子类实现，并检查容量。 */
#define XVTABLE_OVERLOAD(Vtable, Type, Func) \
do { \
	XVtable* _xvtable = (Vtable); \
	if (!_xvtable) break; \
	if ((Type) >= _xvtable->capacity) { \
		XERROR_PRINTF("文件:%s 函数:%s 行号:%d 重载索引超出范围了 索引:%d 容量:%d个\n", \
			__FILE__, __func__, __LINE__, (Type), _xvtable->capacity); \
		exit(-1); \
	} \
	XVtable_At(_xvtable, (Type)) = (Func); \
} while (0)
/** @brief 把函数指针数组追加到虚函数表，并检查静态表容量。 */
#define XVTABLE_ADD_FUNC_LIST(Vtable, Table) \
do { \
	XVtable* _xvtable = (Vtable); \
	size_t _xvtable_count = sizeof(Table) / sizeof((Table)[0]); \
	if (_xvtable && _xvtable->isStack && \
		(_xvtable->size + _xvtable_count > _xvtable->capacity)) { \
		XERROR_PRINTF("文件:%s 函数:%s 行号:%d 追加的函数超出最大容量了,超出:%d个\n", \
			__FILE__, __func__, __LINE__, \
			(_xvtable->size + _xvtable_count - _xvtable->capacity)); \
		exit(-1); \
	} \
	if (_xvtable) XVtable_append_array(_xvtable, (Table), _xvtable_count); \
} while (0)

/* 默认参数封装：标准 class_init 只需要使用这一组宏。 */
/** @brief class_init 当前类共享的虚函数表指针名。 */
#define XVTABLE_DEFAULT XClassVtable

/**
 * @brief 按当前配置完成默认虚函数表初始化。
 *
 * Type 是用于计算虚函数槽位数量的类名，并自动作为默认类名。栈模式
 * 使用该类的枚举容量，堆模式创建可动态扩容的虚函数表。宏展开时已经
 * 完成分支选择，不产生运行时条件判断。需要与 Type 不同的类名时，
 * 可在该宏后调用 XCLASS_SET_CLASS_NAME_DEFAULT 覆盖默认值。
 */
#if XCLASS_VTABLE_USE_STACK
#define XVTABLE_INIT_DEFAULT(Type) \
	XVTABLE_CREAT(XVTABLE_DEFAULT) \
	XVTABLE_STACK_INIT(XVTABLE_DEFAULT, XCLASS_VTABLE_GET_SIZE(Type)) \
	XCLASS_SET_CLASS_NAME_DEFAULT(#Type);
#define XVTABLE_INIT_DEFAULT_SIZE(Size) \
	XVTABLE_CREAT(XVTABLE_DEFAULT) \
	XVTABLE_STACK_INIT(XVTABLE_DEFAULT, (Size))
#else
#define XVTABLE_INIT_DEFAULT(Type) \
	XVTABLE_CREAT(XVTABLE_DEFAULT) \
	XVTABLE_HEAP_INIT(XVTABLE_DEFAULT) \
	XCLASS_SET_CLASS_NAME_DEFAULT(#Type);
#define XVTABLE_INIT_DEFAULT_SIZE(Size) \
	XVTABLE_CREAT(XVTABLE_DEFAULT) \
	XVTABLE_HEAP_INIT(XVTABLE_DEFAULT)
#endif

/**
 * @brief 按配置输出一个类型对应的 size_t 大小值。
 *
 * Type 既用于生成输出标签，也用于表达被统计的类型；Value 可以是虚表槽位数、
 * sizeof 表达式或其他 size_t 表达式。关闭 XCLASS_VTABLE_SHOW_SIZE 时，
 * Value 不会求值，也不会生成输出代码。
 */
#if XCLASS_VTABLE_SHOW_SIZE
#define XCLASS_SHOW_SIZE(Type, Value) \
	do { printf(#Type " size:%zu\n", (size_t)(Value)); } while (0)
#else
#define XCLASS_SHOW_SIZE(Type, Value) do { } while (0)
#endif

/**
 * @brief 输出默认虚函数表的槽位数量。
 *
 * Type 用于生成输出标签，并自动统计 XVtable_size(XVTABLE_DEFAULT)，
 * 由 XCLASS_VTABLE_SHOW_SIZE 统一控制，适用于标准 class_init 函数。
 */
#define XCLASS_SHOW_SIZE_DEFAULT(Type) \
	XCLASS_SHOW_SIZE(Type, XVtable_size(XVTABLE_DEFAULT))

/** @brief 把指定虚函数表追加到默认表。 */
#define XVTABLE_INHERIT_DEFAULT(VtableBase) \
	XVTABLE_INHERIT(XVTABLE_DEFAULT, (VtableBase))
/** @brief 把指定类的虚函数表追加到默认表。 */
#define XVTABLE_INHERIT_XCLASS(Type) \
	XVTABLE_INHERIT(XVTABLE_DEFAULT, Type##_class_init())
/** @brief 重载默认表中的一个虚函数槽位。 */
#define XVTABLE_OVERLOAD_DEFAULT(Type, Func) \
	XVTABLE_OVERLOAD(XVTABLE_DEFAULT, (Type), (Func))
/** @brief 把函数指针数组追加到默认虚函数表。 */
#define XVTABLE_ADD_FUNC_LIST_DEFAULT(Table) \
	XVTABLE_ADD_FUNC_LIST(XVTABLE_DEFAULT, (Table))
/* 类的创建和生命周期接口。 */
XVtable* XClass_class_init();
void XClass_init(XClass* object);
void XClass_copy_base(XClass* object, const XClass* src);
void XClass_move_base(XClass* object, XClass* src);
void XClass_deinit_base(XClass* object);
void XClass_delete_base(XClass* object);

/** @brief 兼容 C++ 风格的保护区标记；C 语言中不产生任何代码。 */
#define Protected 

/** @brief 获取对象的内存方法；由 *_init 绑定默认方法，*_create 可覆盖并标记所有权。 */
#define Class_Memory(Object) (((XClass*)(Object))->m_memory)
/** @brief 判断对象是否由 *_create 堆分配；0 表示由 *_init 初始化。 */
#define Class_IsHeap(Object) (((XClass*)(Object))->m_is_heap)
/** @brief 按内存池类型设置对象的内存方法；不改变对象的堆所有权位。 */
#define Set_Class_Memory(Object, MemoryType) \
	(Class_Memory(Object) = XMemory_method((MemoryType)))
/** @brief 设置对象是否由 *_create 堆分配；Value 为 true 表示堆对象。 */
#define Set_Class_IsHeap(Object, Value) \
	(Class_IsHeap(Object) = !!(Value))

/** @brief 取得父类指定虚函数；Type 必须提供 Type##_class_init。 */
#define XClass_Parent(Type, FuncEnum, FuncType) \
	(XVtableGetFunc(Type##_class_init(), (FuncEnum), FuncType))
/** @brief 调用父类的 Deinit 实现释放派生对象中的父类资源。 */
#define XClass_Deinit_Parent(Type, Object) \
	(XClass_Parent(Type, EXClass_Deinit, void(*)(Type*))(Object))

#ifdef __cplusplus
}
#endif
#endif // !XVirtual_H
