/******************************************************************************
 * @file       XIconEnginePlugin.h
 * @brief      XIconEnginePlugin 图标引擎插件工厂（对标 Qt 6.8）。
 ******************************************************************************/
#ifndef XICONENGINEPLUGIN_H
#define XICONENGINEPLUGIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XIconEngine.h"
#include "XObject.h"

#define XICONENGINEPLUGIN_IID "org.qt-project.Qt.QIconEngineFactoryInterface"

XCLASS_DEFINE_BEGING(XIconEnginePlugin)
XCLASS_DEFINE_ENUM(XIconEnginePlugin, Create) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_END(XIconEnginePlugin)

typedef struct XIconEnginePlugin
{
    XObject m_class; /**< 继承的 XObject 基类成员。 */
} XIconEnginePlugin;

/**
 * @brief 初始化 XIconEnginePlugin 虚函数表。
 * @return 图标引擎插件类虚函数表指针。
 */
XVtable* XIconEnginePlugin_class_init(void);

/**
 * @brief 按指定内存类型创建图标引擎插件。
 * @param memory 对象使用的内存类型。
 * @return 新建插件指针；失败时返回 NULL。
 */
XIconEnginePlugin* XIconEnginePlugin_create_ex(XMemoryType memory);

/**
 * @brief 初始化图标引擎插件实例。
 * @param self 待初始化的插件指针。
 */
void XIconEnginePlugin_init(XIconEnginePlugin* self);

/**
 * @brief 释放插件实例资源。
 * @param self 待释放的插件指针。
 */
/** @brief 通过 XClass 虚表释放插件资源。 @param self 待释放的插件指针。 */
#define XIconEnginePlugin_deinit_base(self) XClass_deinit_base((XClass*)(self))

/**
 * @brief 删除堆上分配的插件实例。
 * @param self 待删除的插件指针。
 */
/** @brief 删除堆上的图标引擎插件。 @param self 待删除的插件指针。 */
#define XIconEnginePlugin_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief 通过 XString 文件名创建图标引擎。
 * @param self 插件指针。
 * @param fileName 图标文件名。
 * @return 新建图标引擎指针，未识别文件或创建失败时返回 NULL。
 * @note 返回对象所有权交给调用方。
 */
XIconEngine* XIconEnginePlugin_createEngine_base(XIconEnginePlugin* self,
                                                  const XString* fileName);

/**
 * @brief 通过 UTF-8 文件名创建图标引擎的兼容重载。
 * @param self 插件指针。
 * @param fileName UTF-8 编码的图标文件名。
 * @return 新建图标引擎指针，未识别文件或创建失败时返回 NULL。
 */
XIconEngine* XIconEnginePlugin_createEngine_2_base(XIconEnginePlugin* self,
                                                    const char* fileName);

/**
 * @brief 创建图标引擎的 XString 主 API。
 * @param self 插件指针。
 * @param fileName 图标文件名。
 * @return 新建图标引擎指针，未识别文件或创建失败时返回 NULL。
 */
XIconEngine* XIconEnginePlugin_create_base(XIconEnginePlugin* self,
                                           const XString* fileName);

/**
 * @brief 创建图标引擎的 UTF-8 兼容重载。
 * @param self 插件指针。
 * @param fileName UTF-8 编码的图标文件名。
 * @return 新建图标引擎指针，未识别文件或创建失败时返回 NULL。
 */
XIconEngine* XIconEnginePlugin_create_2_base(XIconEnginePlugin* self,
                                             const char* fileName);

#ifdef __cplusplus
}
#endif

#undef XIconEnginePlugin_create
#define XIconEnginePlugin_create() \
    XIconEnginePlugin_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif /* XICONENGINEPLUGIN_H */
