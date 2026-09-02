/******************************************************************************
 * @file       XFontBitmapFace.h
 * @brief      XFont 点阵字库具体实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XFONTBITMAPFACE_H
#define XFONTBITMAPFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XFontFace.h"

/* 点阵 face 继承 XFontFace，不新增虚函数槽位。 */
XCLASS_DEFINE_BEGING(XFontBitmapFace)
XCLASS_DEFINE_EXTEND_END(XFontBitmapFace, XFontFace)

/** @brief 点阵字库具体类；m_class 必须是第一个成员。 */
typedef struct XFontBitmapFace
{
    XFontFace m_class;             /**< XFontFace 基类成员，必须位于第一位。 */
    XFontBitmapProvider m_provider; /**< 点阵 provider 的值拷贝。 */
    bool m_file;                   /**< 是否使用外挂文件后端。 */
} XFontBitmapFace;

/** @brief 初始化点阵 face；provider 指针成员只保存借用引用。 */
void XFontBitmapFace_init(XFontBitmapFace* self,
                          const XFontBitmapProvider* provider);

/** @brief 初始化用于外挂文件解析的点阵 face。 */
void XFontBitmapFace_initFile(XFontBitmapFace* self);

/** @brief 初始化点阵 face 类虚函数表。 */
XVtable* XFontBitmapFace_class_init(void);

/** @brief 通过 XClass 虚表复制点阵 face。 */
#define XFontBitmapFace_copy_base(self, other) \
    XClass_copy_base((XClass*)(self), (const XClass*)(other))
/** @brief 通过 XClass 虚表移动点阵 face。 */
#define XFontBitmapFace_move_base(self, other) \
    XClass_move_base((XClass*)(self), (XClass*)(other))
/** @brief 通过 XClass 虚表反初始化点阵 face。 */
#define XFontBitmapFace_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 通过 XClass 虚表删除堆上的点阵 face。 */
#define XFontBitmapFace_delete_base(self) XClass_delete_base((XClass*)(self))

/** @brief 注册一个点阵 provider，并接入 XFontFace 解析表。 */
bool XFontBitmapFace_registerProvider(const XFontBitmapProvider* provider);

/** @brief 返回外挂文件 face；对象由模块静态持有。 */
const XFontFace* XFontBitmapFace_fileFace(void);

#ifdef __cplusplus
}
#endif

#endif /* XFONTBITMAPFACE_H */
