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
    bool m_file;                    /**< 是否使用外挂文件后端。 */
} XFontOutlineFace;

/** @brief 初始化轮廓 face；provider 指针成员只保存借用引用。 */
void XFontOutlineFace_init(XFontOutlineFace* self,
                           const XFontOutlineProvider* provider);

/** @brief 初始化用于外挂 XFO1 文件解析的轮廓 face。 */
void XFontOutlineFace_initFile(XFontOutlineFace* self);

/** @brief 初始化轮廓 face 类虚函数表。 */
XVtable* XFontOutlineFace_class_init(void);

/** @brief 通过 XClass 虚表复制轮廓 face。 */
#define XFontOutlineFace_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 通过 XClass 虚表移动轮廓 face。 */
#define XFontOutlineFace_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))
/** @brief 通过 XClass 虚表反初始化轮廓 face。 */
#define XFontOutlineFace_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 通过 XClass 虚表删除堆上的轮廓 face。 */
#define XFontOutlineFace_delete_base(self) XClass_delete_base((XClass*)(self))

/** @brief 注册一个轮廓 provider，并接入 XFontFace 解析表。 */
bool XFontOutlineFace_registerProvider(const XFontOutlineProvider* provider);

/** @brief 返回外挂文件 face；对象由模块静态持有。 */
const XFontFace* XFontOutlineFace_fileFace(void);

#ifdef __cplusplus
}
#endif

#endif /* XFONTOUTLINEFACE_H */
