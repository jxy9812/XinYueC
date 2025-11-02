/**
* @file XContainerObject.h
* @brief 容器基类头文件，定义所有容器的公共接口和基础结构
* @details 该文件声明了容器基类XContainerObject的结构、虚函数表、回调函数类型及基础操作方法，
*          为派生容器（如XVector、XMap、XSet等）提供统一的接口规范和基础功能实现
*/
#include "CXinYueConfig.h"   ///< 项目配置文件，包含模块开关等全局配置
#include "XClass.h"          ///< 基类头文件，提供类继承和虚函数表基础支持
 /**
  * @brief 防止头文件重复包含，并仅在XContainerObject模块启用时生效
  * @details 通过条件编译确保头文件内容只被编译一次，且依赖XContainerObject_ON开关控制模块是否启用
  */
#if !defined(XCONTAINEROBJECT_H)&& XContainerObject_ON
#define XCONTAINEROBJECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdio.h>       ///< 标准输入输出库，用于调试输出等
#include <stdbool.h>     ///< 布尔类型定义，提供bool、true、false等
#include "XMemory.h"     ///< 内存管理工具，提供内存分配、释放等函数
#include "XTypes.h"      ///< 基础数据类型定义，统一跨平台类型（如size_t等）
#include "XCompare.h"    ///< 比较函数接口，提供元素比较的标准定义
/**
 * @brief 数据释放方法回调函数类型
 * @details 用于定义容器中元素的释放逻辑，由用户根据元素类型自定义实现
 * @param data 待释放的数据指针（指向需要释放内存的元素）
 */
typedef void (*XCDataDeinitMethod)(void* data);

/**
 * @brief 数据创建方法回调函数类型
 * @details 用于定义元素的初始化逻辑，从源数据复制内容到目标内存
 * @param data 目标数据指针（待初始化的内存地址）
 * @param sData 源数据指针（提供初始化数据的原始内存地址）
 */
typedef void (*XCDataCreatMethod)(void* data, const void* sData);

/**
 * @brief 数据拷贝方法回调函数类型
 * @details 复用数据创建方法的定义，语义上用于元素的深拷贝操作
 */
typedef XCDataCreatMethod XCDataCopyMethod;

/**
 * @brief 数据移动方法回调函数类型
 * @details 复用数据创建方法的定义，语义上用于元素的所有权转移（避免深拷贝，提升性能）
 */
typedef XCDataCreatMethod XCDataMoveMethod;

/**
 * @brief 数据清除方法回调函数类型
 * @details 复用数据释放方法的定义，语义上用于清除元素内容（不释放内存本身）
 */
typedef XCDataDeinitMethod XCDataClearMethod;
/**
 * @brief 定义XContainerObject的虚函数表枚举
 * @details 枚举项对应虚函数表中的索引，用于通过虚函数表调用对应方法，实现多态
 */
#define XCONTAINEROBJECT_VTABLE_SIZE   (XCLASS_VTABLE_GET_SIZE(XContainerObject))      //容器基类虚函数表大小
//XContainerObject虚函数表枚举
XCLASS_DEFINE_BEGING(XContainerObject)
XCLASS_DEFINE_ENUM(XContainerObject,IsEmpty) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XContainerObject,Size),
XCLASS_DEFINE_ENUM(XContainerObject,Capacity),
XCLASS_DEFINE_ENUM(XContainerObject,TypeSize),
XCLASS_DEFINE_ENUM(XContainerObject,Swap),
XCLASS_DEFINE_ENUM(XContainerObject,Clear),
XCLASS_DEFINE_END(XContainerObject)

/**
 * @brief 容器基类结构体
 * @details 所有容器的基类，封装了容器的公共属性和回调函数，支持继承和多态
 */
typedef struct XContainerObject
{
    XClass m_class;  ///< 继承自XClass，用于实现类的继承关系和虚函数机制

    XCDataCopyMethod m_dataCopyMethod;      ///< 数据拷贝回调函数，用于元素深拷贝
    XCDataMoveMethod m_dataMoveMethod;      ///< 数据移动回调函数，用于元素所有权转移
    XCDataDeinitMethod m_dataDeinitMethod;  ///< 数据释放回调函数，用于元素内存释放
    XCompare m_compare;                     ///< 元素比较回调函数，用于元素大小比较

    void* m_data;             ///< 指向容器实际存储数据的指针（数据区首地址）
    size_t m_capacity;        ///< 容器当前可容纳的最大元素数量（容量）
    size_t m_size;            ///< 容器当前实际存储的元素数量（大小）
    size_t m_typeSize;        ///< 单个元素的类型大小（字节数，如int为4字节）
}XContainerObject;
//宏函数
/**
 * @brief 获取派生类中存储的数据
 * @details 通过指针类型转换，从数据指针中提取指定类型的元素值
 * @param LPVal 数据指针（指向元素的内存地址）
 * @param type 元素的数据类型（如int、float等）
 * @return 转换后的元素值（type类型）
 */
#define XContainerValue(LPVal, type) (*(type*)LPVal)

/**
* @brief 获取容器的数据区指针
* @details 返回容器内部存储数据的首地址
* @param Object XContainerObject实例指针（可传入派生类实例指针）
* @return 数据区首地址（void*类型）
*/
#define XContainerDataPtr(Object) (((XContainerObject*)(Object))->m_data)

 /**
* @brief 获取容器数据区的实际数据（指定类型）
* @details 将数据区指针转换为指定类型的指针并解引用，直接获取数据值
* @param Object XContainerObject实例指针
* @param Type 数据类型（如int数组则传入int[]）
* @return 转换后的Type类型数据
*/
#define XContainerData(Object, Type) (*(Type*)XContainerDataPtr(Object))

/**
* @brief 获取容器的容量
* @details 返回容器当前可容纳的最大元素数量
* @param Object XContainerObject实例指针
* @return 容量值（size_t类型）
*/
#define XContainerCapacity(Object) (((XContainerObject*)(Object))->m_capacity)

/**
 * @brief 获取容器当前元素数量
 * @details 返回容器中实际存储的元素个数
 * @param Object XContainerObject实例指针
 * @return 元素数量（size_t类型）
 */
#define XContainerSize(Object) (((XContainerObject*)(Object))->m_size)

/**
 * @brief 判断容器是否为空
 * @details 当容器元素数量为0时返回true，否则返回false
 * @param Object XContainerObject实例指针
 * @return 布尔值（true为空，false为非空）
 */
#define XContainerIsEmpty(Object) (XContainerSize(Object) == 0)

/**
 * @brief 获取容器中单个元素的类型大小
 * @details 返回单个元素占用的字节数
 * @param Object XContainerObject实例指针
 * @return 类型大小（字节数，size_t类型）
 */
#define XContainerTypeSize(Object) (((XContainerObject*)(Object))->m_typeSize)

/**
 * @brief 获取容器的数据拷贝方法
 * @details 返回当前设置的元素拷贝回调函数
 * @param Object XContainerObject实例指针
 * @return 数据拷贝方法（XCDataCopyMethod类型）
 */
#define XContainerDataCopyMethod(Object) (((XContainerObject*)(Object))->m_dataCopyMethod)

/**
 * @brief 设置容器的数据拷贝方法
 * @details 自定义元素的拷贝逻辑（如深拷贝、浅拷贝）
 * @param Object XContainerObject实例指针
 * @param method 数据拷贝回调函数（XCDataCopyMethod类型）
 */
#define XContainerSetDataCopyMethod(Object, method) (((XContainerObject*)(Object))->m_dataCopyMethod = method)

/**
 * @brief 获取容器的数据移动方法
 * @details 返回当前设置的元素移动回调函数
 * @param Object XContainerObject实例指针
 * @return 数据移动方法（XCDataMoveMethod类型）
 */
#define XContainerDataMoveMethod(Object) (((XContainerObject*)(Object))->m_dataMoveMethod)

/**
 * @brief 设置容器的数据移动方法
 * @details 自定义元素的移动逻辑（如转移指针所有权）
 * @param Object XContainerObject实例指针
 * @param method 数据移动回调函数（XCDataMoveMethod类型）
 */
#define XContainerSetDataMoveMethod(Object, method) (((XContainerObject*)(Object))->m_dataMoveMethod = method)

/**
 * @brief 获取容器的数据释放方法
 * @details 返回当前设置的元素释放回调函数
 * @param Object XContainerObject实例指针
 * @return 数据释放方法（XCDataDeinitMethod类型）
 */
#define XContainerDataDeinitMethod(Object) (((XContainerObject*)(Object))->m_dataDeinitMethod)

/**
 * @brief 设置容器的数据释放方法
 * @details 自定义元素的内存释放逻辑（如释放动态分配的成员）
 * @param Object XContainerObject实例指针
 * @param method 数据释放回调函数（XCDataDeinitMethod类型）
 */
#define XContainerSetDataDeinitMethod(Object, method) (((XContainerObject*)(Object))->m_dataDeinitMethod = method)

/**
 * @brief 获取容器的元素比较方法
 * @details 返回当前设置的元素比较回调函数
 * @param Object XContainerObject实例指针
 * @return 比较方法（XCompare类型）
 */
#define XContainerCompare(Object) (((XContainerObject*)(Object))->m_compare)

/**
 * @brief 设置容器的元素比较方法
 * @details 自定义元素的大小比较逻辑（用于排序、查找等操作）
 * @param Object XContainerObject实例指针
 * @param compare 比较回调函数（XCompare类型）
 */
#define XContainerSetCompare(Object, compare) (((XContainerObject*)(Object))->m_compare = compare)

/**
* @brief 拷贝操作的基础实现
* @details 复用基类XClass的拷贝逻辑，作为容器拷贝的默认实现
*/
#define XContainerObject_copy_base XClass_copy_base

/**
* @brief 移动操作的基础实现
* @details 复用基类XClass的移动逻辑，作为容器移动的默认实现
*/
#define XContainerObject_move_base XClass_move_base

/**
* @brief 销毁操作的基础实现
* @details 复用基类XClass的销毁逻辑，作为容器销毁的默认实现
*/
#define XContainerObject_deinit_base XClass_deinit_base

/**
* @brief 内存释放的基础实现
* @details 复用基类XClass的内存释放逻辑，作为容器内存释放的默认实现
*/
#define XContainerObject_delete_base XClass_delete_base
/**
* @brief 初始化XContainerObject的虚函数表
* @details 为容器基类创建并初始化虚函数表，注册各类虚函数实现
* @return 虚函数表指针（XVtable*类型）
*/
XVtable* XContainerObject_class_init();

/**
 * @brief 初始化XContainerObject实例
 * @details 初始化容器的基础属性（如类型大小、虚函数表等），为容器使用做准备
 * @param Object 待初始化的XContainerObject实例指针
 * @param typeSize 容器中元素的类型大小（字节数）
 */
void XContainerObject_init(XContainerObject* Object, size_t typeSize);

/**
 * @brief 获取容器元素数量的基础实现
 * @details 通过虚函数调用获取容器当前元素数量，支持多态
 * @param Object XContainerObject实例指针（const修饰，不可修改）
 * @return 元素数量（size_t类型）
 */
size_t XContainerObject_size_base(const XContainerObject* Object);

/**
 * @brief 判断容器是否为空的基础实现
 * @details 通过虚函数调用判断容器是否为空，支持多态
 * @param Object XContainerObject实例指针（const修饰，不可修改）
 * @return 布尔值（true为空，false为非空）
 */
bool XContainerObject_isEmpty_base(const XContainerObject* Object);

/**
 * @brief 获取容器容量的基础实现
 * @details 通过虚函数调用获取容器当前容量，支持多态
 * @param Object XContainerObject实例指针（const修饰，不可修改）
 * @return 容量值（size_t类型）
 */
size_t XContainerObject_capacity_base(const XContainerObject* Object);

/**
 * @brief 获取元素类型大小的基础实现
 * @details 通过虚函数调用获取单个元素的类型大小，支持多态
 * @param Object XContainerObject实例指针（const修饰，不可修改）
 * @return 类型大小（字节数，size_t类型）
 */
size_t XContainerObject_typeSize_base(const XContainerObject* Object);

/**
 * @brief 交换两个容器内容的基础实现
 * @details 通过虚函数调用交换两个容器的内容（数据、容量、大小等），支持多态
 * @param ObjectOne 第一个容器实例指针
 * @param ObjectTwo 第二个容器实例指针
 */
void XContainerObject_swap_base(XContainerObject* ObjectOne, XContainerObject* ObjectTwo);

/**
 * @brief 清空容器的基础实现
 * @details 通过虚函数调用清空容器内容（保留容量，重置大小），支持多态
 * @param Object XContainerObject实例指针
 */
void XContainerObject_clear_base(XContainerObject* Object);

#ifdef __cplusplus
}
#endif
#endif // !ContainerObject_h
