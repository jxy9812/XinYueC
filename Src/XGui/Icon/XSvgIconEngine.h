/**
 * @file       XSvgIconEngine.h
 * @brief      XSvgIconEngine SVG 图标引擎（对标 Qt 6.8 QSvgIconEngine）。
 * @details    继承 XIconEngine，Pixmap 虚槽经 XImage_load（XIMAGECODEC_SVG_ON
 *             解码器）加载 SVG/SVGZ 文件；Key 返回 "svg"；IconName 返回
 *             空串（Qt SVG 引擎语义）；IsNull 按文件名是否为空判定。
 * @note       尺寸请求当前按解码固有尺寸返回（矢量按请求尺寸重新栅格化
 *             登记为已知偏差，Task 2.20）。
 * @author     XinYueC 团队
 */
#ifndef XSVGICONENGINE_H
#define XSVGICONENGINE_H
#ifdef __cplusplus
extern "C" {
#endif

#include "XIconEngine.h"
#include "XString.h"


XCLASS_DEFINE_BEGING(XSvgIconEngine)
XCLASS_DEFINE_EXTEND_END(XSvgIconEngine, XIconEngine)

/** @brief SVG 图标引擎对象；m_class 必须为第一个成员。 */
typedef struct XSvgIconEngine
{
    XIconEngine m_class;   /**< 继承的 XIconEngine 基类成员。 */
    XString* m_fileName;   /**< SVG 文件路径（拥有）。 */
} XSvgIconEngine;

/**
 * @brief 初始化 XSvgIconEngine 虚函数表。
 * @return 类共享虚函数表指针。
 */
XVtable* XSvgIconEngine_class_init(void);
/**
 * @brief 按文件名创建 SVG 图标引擎。
 * @param fileName SVG/SVGZ 文件路径；可为 NULL。
 * @return 新建引擎指针；失败返回 NULL。
 */
XSvgIconEngine* XSvgIconEngine_create(const XString* fileName);
/**
 * @brief 使用 UTF-8 文件名创建 SVG 图标引擎。
 * @param utf8FileName SVG/SVGZ 文件路径（UTF-8）；可为 NULL。
 * @return 新建引擎指针；失败返回 NULL。
 */
XSvgIconEngine* XSvgIconEngine_create_2(const char* utf8FileName);
#define XSvgIconEngine_delete_base(self) XClass_delete_base((XClass*)(self))
#define XSvgIconEngine_deinit_base(self) XClass_deinit_base((XClass*)(self))


#ifdef __cplusplus
}
#endif
#endif /* XSVGICONENGINE_H */
