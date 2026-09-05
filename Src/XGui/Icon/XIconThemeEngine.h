/******************************************************************************
 * @file       XIconThemeEngine.h
 * @brief      XIconThemeEngine 主题图标引擎（对标 Qt 6.8 QIconLoaderEngine）。
 * @note       引擎按主题名称在 XDG 主题目录中按需解析像素图并缩放，
 *             图标名称在创建时保存，像素图解析延迟到第一次取图。
 ******************************************************************************/
#ifndef XICONTHEMEENGINE_H
#define XICONTHEMEENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "XIconEngine.h"
#include "XString.h"

XCLASS_DEFINE_BEGING(XIconThemeEngine)
XCLASS_DEFINE_EXTEND_END(XIconThemeEngine, XIconEngine)

/**
 * @brief      XIconThemeEngine 主题图标引擎对象。
 * @details    m_base 必须是第一个成员（嵌 XIconEngine）；m_iconName 持有
 *             主题图标名称，析构时自动释放。
 */
typedef struct XIconThemeEngine
{
    XIconEngine m_base;      /**< 基类成员；必须是第一个。 */
    XString*    m_iconName;  /**< 主题图标名称。 */
} XIconThemeEngine;

/**
 * @brief 初始化主题图标引擎类虚函数表。
 * @return 主题图标引擎类虚函数表指针。
 */
XVtable* XIconThemeEngine_class_init(void);

/**
 * @brief 按指定内存类型和主题名称创建主题图标引擎。
 * @param memory   对象使用的内存类型。
 * @param iconName 主题图标名称；可为 NULL，创建空引擎。
 * @return 新建主题图标引擎指针；失败时返回 NULL。
 */
XIconThemeEngine* XIconThemeEngine_create_ex(XMemoryType memory,
                                             const XString* iconName);

/**
 * @brief 按指定内存类型和 UTF-8 主题名称创建主题图标引擎。
 * @param memory   对象使用的内存类型。
 * @param iconName UTF-8 编码的主题图标名称；可为 NULL。
 * @return 新建主题图标引擎指针；失败时返回 NULL。
 */
XIconThemeEngine* XIconThemeEngine_create_2_ex(XMemoryType memory,
                                               const char* iconName);

/**
 * @brief 初始化主题图标引擎实例。
 * @param self     待初始化的主题图标引擎指针。
 * @param iconName 主题图标名称；可为 NULL。
 */
void XIconThemeEngine_init(XIconThemeEngine* self, const XString* iconName);

/**
 * @brief 使用 UTF-8 主题名称初始化主题图标引擎实例。
 * @param self     待初始化的主题图标引擎指针。
 * @param iconName UTF-8 编码的主题图标名称；可为 NULL。
 */
void XIconThemeEngine_init_2(XIconThemeEngine* self, const char* iconName);



/**
 * @brief 通过 XClass 虚表释放主题图标引擎资源。
 * @param self 待释放的主题图标引擎指针。
 */
#define XIconThemeEngine_deinit_base(self) XClass_deinit_base((XClass*)(self))

/**
 * @brief 删除堆上分配的主题图标引擎实例。
 * @param self 待删除的主题图标引擎指针。
 */
#define XIconThemeEngine_delete_base(self) XClass_delete_base((XClass*)(self))

#ifdef __cplusplus
}
#endif

#endif /* XICONTHEMEENGINE_H */
