/**
 * @file       XSvgIconEnginePlugin.h
 * @brief      XSvgIconEnginePlugin SVG 图标引擎插件（对标 Qt 6.8 内置 SVG
 *             QIconEnginePlugin；keys="svg,svgz"，Create 返回 XSvgIconEngine）。
 * @note       随 XSVGICON_ON 开关裁剪（定义于 XGuiConfig.h）；置 0 时本
 *             文件整体裁剪，插件无自动注册，关闭不影响其它模块。
 * @author     XinYueC 团队
 */
#ifndef XSVGICONENGINEPLUGIN_H
#define XSVGICONENGINEPLUGIN_H
#include "XGuiConfig.h"
#if XSVGICON_ON
#ifdef __cplusplus
extern "C" {
#endif

#include "XIconEnginePlugin.h"


XCLASS_DEFINE_BEGING(XSvgIconEnginePlugin)
XCLASS_DEFINE_EXTEND_END(XSvgIconEnginePlugin, XIconEnginePlugin)

/** @brief SVG 图标引擎插件对象；m_class 必须为第一个成员。 */
typedef struct XSvgIconEnginePlugin
{
    XIconEnginePlugin m_class; /**< 继承的 XIconEnginePlugin 基类成员。 */
} XSvgIconEnginePlugin;

/** @brief 初始化 XSvgIconEnginePlugin 虚函数表。 */
XVtable* XSvgIconEnginePlugin_class_init(void);
/** @brief 创建 SVG 图标引擎插件（完成后须注册到插件注册表）。 */
XSvgIconEnginePlugin* XSvgIconEnginePlugin_create(void);
#define XSvgIconEnginePlugin_delete_base(self) XClass_delete_base((XClass*)(self))
#define XSvgIconEnginePlugin_deinit_base(self) XClass_deinit_base((XClass*)(self))


#ifdef __cplusplus
}
#endif
#endif /* XSVGICON_ON */
#endif /* XSVGICONENGINEPLUGIN_H */
