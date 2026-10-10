/******************************************************************************
 * @file       XFontOutlineFace.h
 * @brief      XFont 轮廓字库具体实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XFONTOUTLINEFACE_H
#define XFONTOUTLINEFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XFontFace.h"

/* 轮廓 face 继承 XFontFace，不新增虚函数槽位。 */
XCLASS_DEFINE_BEGING(XFontOutlineFace)
XCLASS_DEFINE_EXTEND_END(XFontOutlineFace, XFontFace)

/** @brief 轮廓字库具体类；m_class 必须是第一个成员。 */
typedef struct XFontOutlineFace
{
    XFontFace m_class;              /**< XFontFace 基类成员，必须位于第一位。 */
    XFontOutlineProvider m_provider; /**< 轮廓 provider 的值拷贝。 */
    bool m_ft;                      /**< 是否使用 FT 文件后端（XFontFt 家族槽）。
                                         [已移除] 原 m_file 位（外挂 XFO1 文件
                                         后端）随自研轮廓字实现删除，2026-10-07。 */
} XFontOutlineFace;

/** @brief 初始化轮廓 face；provider 指针成员只保存借用引用。 */
void XFontOutlineFace_init(XFontOutlineFace* self,
                           const XFontOutlineProvider* provider);

/** @brief 初始化轮廓 face 类虚函数表。 */
XVtable* XFontOutlineFace_class_init(void);

/** @brief 通过 XClass 虚表反初始化轮廓 face。 */
/** @brief 通过 XClass 虚表删除堆上的轮廓 face。 */

/** @brief 注册一个轮廓 provider，并接入 XFontFace 解析表。 */
bool XFontOutlineFace_registerProvider(const XFontOutlineProvider* provider);

/* [已移除 2026-10-07] XFontOutlineFace_initFile / XFontOutlineFace_fileFace：
 * XFO1 外挂轮廓文件后端随自研轮廓字实现整体删除。 */

#ifdef __cplusplus
}
#endif

#endif /* XFONTOUTLINEFACE_H */
