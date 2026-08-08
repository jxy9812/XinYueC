/**
 * @file       XVarList.h
 * @brief      轻量级可变参数值列表公开 API。
 * @details    XVarList 将 XVar 宏指定的值按字节复制到一块连续存储中，
 *             主要用于信号、回调和菜单参数传递。本头文件只依赖 XinYueC
 *             类型和内存 API，不调用 Win32、POSIX 或硬件平台 API。
 * @note       列表保存的是参数值字节，不会深拷贝值中的指针所指对象。
 */
#ifndef XVARLIST_H
#define XVARLIST_H
#ifdef __cplusplus
extern "C" {
#endif
#include "CXinYueConfig.h"
#include "XMemory.h"
#include "XTypes.h"
#include "XChar.h"
#include <stdio.h>
#include <stdint.h>
/** @private 仅供 COUNT_ARGS 宏展开时取得参数总数。 */
#define XVARLIST_COUNT_ARGS(_1, _2, _3, _4, _5, _6, _7, _8, \
                            _9, _10, _11, _12, _13, _14, _15, _16, \
                            _17, _18, _19, _20, _21, _22, _23, _24, \
                            _25, _26, _27, _28, _29, _30, _31, _32, N, ...) N
/**
 * @brief      计算 XVarList_Create 的预处理器参数总数。
 * @details    每个 XVar(type, value) 展开为类型大小和数据地址两个参数。
 *             最多支持 16 个 XVar 项，避免将 64 位指针转换为 int。
 * @param      ... 由 XVar 宏生成的非空参数序列。
 * @return     预处理器参数数量；数值为 XVar 项数量的两倍。
 */
#define COUNT_ARGS(...) \
    XVARLIST_COUNT_ARGS(__VA_ARGS__, 32, 31, 30, 29, 28, 27, 26, 25, \
                        24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, \
                        12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
/**
 * @brief      将变量包装为 XVarList_create 接受的大小和地址参数对。
 * @param      type var 的完整 C 数据类型。
 * @param      var 具有可取地址存储的变量；只在 XVarList_create 调用期间借用。
 * @return     `sizeof(type), &var` 两个实参，只能用于 XVarList_Create 或
 *             XVarList_create 的可变参数序列。
 */
#define XVar(type, var)   sizeof(type), &var
/**
 * @brief      连续存储多个异构参数值的轻量级列表。
 * @details    结构体尾部的 data 保存参数值，ptr 作为解包游标。调用方不应
 *             手工修改 m_free 或 m_size；应通过本头文件的宏读取参数。
 */
typedef struct XVarList
{
	uint8_t* ptr; /**< 当前解包位置；指向对象自有 data 区域，禁止手工释放。 */
    FreeMethod m_free; /**< 列表存储的释放函数；由创建函数设置，禁止手工修改。 */
    void(*argsDel)(struct XVarList*); /**< 可选的参数清理回调；可为 NULL，不转移函数所有权。 */
    size_t m_size; /**< data 区域的有效字节数；由创建函数维护，单位为字节。 */
    char data[]; /**< 存放参数值的柔性数组；容量为 m_size，生命周期与列表相同。 */
} XVarList;
/**
 * @brief      根据 XVar 参数项自动计数并创建参数列表。
 * @param      ... 一个或多个 `XVar(type, var)` 项，最多 16 项。
 * @return     新建 XVarList；分配失败返回 NULL。调用方必须使用
 *             XVarList_delete 释放返回对象。
 */
#define XVarList_Create(...)     XVarList_create(COUNT_ARGS(__VA_ARGS__), __VA_ARGS__)
/**
 * @brief      执行可选参数清理回调并释放 XVarList。
 * @param      list 由 XVarList_Create、XVarList_create 或 XVarList_create_copy
 *             返回的对象；可为 NULL，函数返回后不得再访问。
 * @return     无。
 */
void  XVarList_delete(XVarList*list);
/**
 * @brief      设置列表删除前执行的参数清理回调。
 * @param      list 目标 XVarList；不能为 NULL。
 * @param      d 清理回调；可为 NULL，列表不取得回调所有权。
 * @return     无。
 */
#define XVarList_setArgsDel(list,d)       (((XVarList*)list)->argsDel = d)
/**
 * @brief      返回当前参数清理回调。
 * @param      list 目标 XVarList；不能为 NULL。
 * @return     借用的回调函数指针；未设置时返回 NULL。
 */
#define XVarList_argsDel(list)            ((XVarList*)list)->argsDel
/**
 * @brief      将解包游标重置到 data 区域起始位置。
 * @param      list 已初始化的 XVarList；不能为 NULL。
 * @return     无。后续 XVarList_arg 从第一个参数开始读取。
 */
#define XVarList_start(list)     *((uint8_t**)list) = ((XVarList*)list)->data
/**
 * @brief      获取当前解包游标。
 * @param      list 已初始化的 XVarList；不能为 NULL。
 * @return     data 区域内的借用地址；不得释放或写入超出 m_size 的范围。
 */
#define XVarList_argPtr(list)    *((uint8_t**)list)
/**
 * @brief      按指定类型大小推进解包游标。
 * @param      list 已初始化的 XVarList；不能为 NULL。
 * @param      type 完整 C 类型；必须与创建时对应 XVar 项的类型一致。
 * @return     无。游标向后移动 sizeof(type) 字节。
 */
#define XVarList_argOffset(list, type) XVarList_argPtr(list) += sizeof(type)
/**
 * @brief      读取当前类型的参数并推进解包游标。
 * @param      list 已初始化且游标位于有效参数的 XVarList；不能为 NULL。
 * @param      type 完整 C 类型；必须与创建时对应 XVar 项的类型和大小一致。
 * @return     当前参数的值。宏展开包含赋值表达式，调用方应在独立语句中使用。
 * @warning    调用方必须保证游标后仍有 sizeof(type) 个有效字节，否则行为未定义。
 */
#define XVarList_arg(list, type) *((type*)XVarList_argPtr(list)); XVarList_argOffset(list, type)
/**
 * @brief      按显式参数数量创建并复制 XVarList。
 * @param      count 可变参数总数；必须为偶数，每个变量占用大小和地址两个参数。
 * @param      ... 由 XVar 宏生成的交替序列；大小参数必须为 int 可变参数类型，
 *             地址参数必须指向至少对应大小的可读存储。
 * @return     新建 XVarList；参数非法、大小溢出或分配失败返回 NULL，调用方必须
 *             使用 XVarList_delete 释放返回对象。
 */
XVarList* XVarList_create(uint8_t count, ...);

/**
 * @brief      创建参数值的浅拷贝。
 * @param      other 源参数列表；调用期间借用，不能为 NULL。
 * @return     新建 XVarList；失败返回 NULL，调用方必须使用 XVarList_delete 释放。
 * @note       仅复制 data 中的值，不复制指针指向的对象，也不继承 argsDel。
 */
XVarList* XVarList_create_copy(const XVarList* other);

/**
 * @brief      将 XVarList 的解包游标重置并按类型声明局部参数变量。
 * @details    XVarList_args_0 不读取参数；XVarList_args_1 至 XVarList_args_10
 *             依次调用 XVarList_arg。每个 typeN 必须与创建列表时的类型一致，
 *             nameN 是当前作用域中新声明的只读局部变量名。
 * @param      list 已初始化的 XVarList；不能为 NULL。
 * @param      typeN 第 N 个参数的完整 C 类型。
 * @param      nameN 第 N 个参数在当前作用域中的局部变量名。
 * @return     无；宏可能声明局部变量并推进 list 的内部游标。
 * @note       下面的 XVarList_args_0 至 XVarList_args_10 使用相同参数语义。
 */
#define XVarList_args_0(list)

/** @brief 解包一个参数。 @param list XVarList 借用对象。 @param type1 参数类型。 @param name1 局部变量名。 @return 无。 */
#define XVarList_args_1(list, type1, name1) \
    XVarList_start(list);\
    const type1 const name1 = XVarList_arg(list, type1)

/** @brief 解包两个参数。 @param list XVarList 借用对象。 @param type1 第一个参数类型。 @param name1 第一个局部变量名。 @param type2 第二个参数类型。 @param name2 第二个局部变量名。 @return 无。 */
#define XVarList_args_2(list, type1, name1, type2, name2) \
    XVarList_args_1(list,type1, name1);\
    const type2 const name2 = XVarList_arg(list, type2)

/** @brief 解包三个参数。 @param list XVarList 借用对象。 @param type1 第一个类型。 @param name1 第一个局部名。 @param type2 第二个类型。 @param name2 第二个局部名。 @param type3 第三个类型。 @param name3 第三个局部名。 @return 无。 */
#define XVarList_args_3(list, type1, name1, type2, name2, type3, name3) \
    XVarList_args_2(list, type1, name1, type2, name2);\
    const type3 const name3 = XVarList_arg(list, type3)

/** @brief 解包四个参数。 @param list XVarList 借用对象。 @param type1 至 type4 依次为四个参数类型。 @param name1 至 name4 依次为四个局部变量名。 @param type2 第二个类型参数。 @param name2 第二个局部名。 @param type3 第三个类型参数。 @param name3 第三个局部名。 @param type4 第四个类型参数。 @param name4 第四个局部名。 @return 无。 */
#define XVarList_args_4(list, type1, name1, type2, name2, type3, name3, type4, name4) \
    XVarList_args_3(list, type1, name1, type2, name2, type3, name3);\
    const type4 const name4 = XVarList_arg(list, type4)

/** @brief 解包五个参数。 @param list XVarList 借用对象。 @param type1 至 type5 依次为五个参数类型。 @param name1 至 name5 依次为五个局部变量名。 @param type2 第二个类型参数。 @param name2 第二个局部名。 @param type3 第三个类型参数。 @param name3 第三个局部名。 @param type4 第四个类型参数。 @param name4 第四个局部名。 @param type5 第五个类型参数。 @param name5 第五个局部名。 @return 无。 */
#define XVarList_args_5(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5) \
    XVarList_args_4(list, type1, name1, type2, name2, type3, name3, type4, name4); \
    const type5 const name5 = XVarList_arg(list, type5)

/** @brief 解包六个参数。 @param list XVarList 借用对象。 @param type1 至 type6 依次为六个参数类型。 @param name1 至 name6 依次为六个局部变量名。 @param type2 第二个类型参数。 @param name2 第二个局部名。 @param type3 第三个类型参数。 @param name3 第三个局部名。 @param type4 第四个类型参数。 @param name4 第四个局部名。 @param type5 第五个类型参数。 @param name5 第五个局部名。 @param type6 第六个类型参数。 @param name6 第六个局部名。 @return 无。 */
#define XVarList_args_6(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6) \
    XVarList_args_5(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5); \
    const type6 const name6 = XVarList_arg(list, type6)

/** @brief 解包七个参数。 @param list XVarList 借用对象。 @param type1/name1 至 type7/name7 依次为参数类型和局部名。 @return 无。 */
#define XVarList_args_7(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7) \
    XVarList_args_6(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6); \
    const type7 const name7 = XVarList_arg(list, type7)

/** @brief 解包八个参数。 @param list XVarList 借用对象。 @param type1/name1 至 type8/name8 依次为参数类型和局部名。 @return 无。 */
#define XVarList_args_8(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7, type8, name8) \
    XVarList_args_7(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7); \
    const type8 const name8 = XVarList_arg(list, type8)

/** @brief 解包九个参数。 @param list XVarList 借用对象。 @param type1/name1 至 type9/name9 依次为参数类型和局部名。 @return 无。 */
#define XVarList_args_9(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7, type8, name8, type9, name9) \
    XVarList_args_8(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7, type8, name8); \
    const type9 const name9 = XVarList_arg(list, type9)

/** @brief 解包十个参数。 @param list XVarList 借用对象。 @param type1/name1 至 type10/name10 依次为参数类型和局部名。 @return 无。 */
#define XVarList_args_10(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7, type8, name8, type9, name9, type10, name10) \
    XVarList_args_9(list, type1, name1, type2, name2, type3, name3, type4, name4, type5, name5, type6, name6, type7, name7, type8, name8, type9, name9); \
    const type10 const name10 = XVarList_arg(list, type10)
#ifdef __cplusplus
}
#endif
#endif
