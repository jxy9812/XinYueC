/******************************************************************************
 * @file       XColorDialog.c
 * @brief      颜色对话框控件实现（对标 Qt 6.8 QColorDialog 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。setCurrentColor 在颜色
 *             实际变化时发射 currentColorChanged；colorSelected 由应用在
 *             接受动作处手动触发。getColor 无 GUI 环境直接返回 initial。
 * @note       本文件不依赖任何平台 API；原生颜色面板为后续扩展。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XString.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"
#include "XColor.h"

#if XWIDGET_ON && XDIALOG_ON

#include "XColorDialog.h"
#include "XWidget_Protected.h"

/* ==================== 内部辅助 ==================== */

/** @brief 发射携带 XColor 值参数的信号。 */
static void xcolordialog_emitColor(XColorDialog* self, size_t signal,
                                   XColor color)
{
    XVarList* args = XVarList_Create(XVar(XColor, color));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 类与实例生命周期 ==================== */

XVtable* XColorDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XColorDialog)
    XVTABLE_INHERIT_XCLASS(XDialog);
    return XVTABLE_DEFAULT;
}

void XColorDialog_init(XColorDialog* self, XColor initial, XWidget* parent,
                       XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XColorDialog);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_currentColor = initial;
    self->m_selectedColor = initial;
    self->m_options = 0;
}

void XColorDialog_init_default(XColorDialog* self, XWidget* parent)
{
    XColorDialog_init(self, XColor_White, parent, 0);
}

XColorDialog* XColorDialog_create_ex(XMemoryType memory, XColor initial,
                                     XWidget* parent, XWidgetFlags flags)
{
    XColorDialog* self = (XColorDialog*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XColorDialog_init(self, initial, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 实例属性 ==================== */

void XColorDialog_setCurrentColor(XColorDialog* self, XColor color)
{
    if (!self) return;
    if (XColor_equals(&self->m_currentColor, &color))
        return;
    self->m_currentColor = color;
    xcolordialog_emitColor(self, (size_t)XColorDialog_currentColorChanged_signal,
                           color);
}

XColor XColorDialog_currentColor(const XColorDialog* self)
{
    return self ? self->m_currentColor : XColor_create();
}

XColor XColorDialog_selectedColor(const XColorDialog* self)
{
    return self ? self->m_selectedColor : XColor_create();
}

void XColorDialog_setOption(XColorDialog* self, XColorDialogOption option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= (XColorDialogOptions)option;
    else self->m_options &= (XColorDialogOptions)~option;
}

bool XColorDialog_testOption(const XColorDialog* self, XColorDialogOption option)
{
    return self ? (self->m_options & (XColorDialogOptions)option) != 0 : false;
}

void XColorDialog_setOptions(XColorDialog* self, XColorDialogOptions options)
{ if (self) self->m_options = options; }

XColorDialogOptions XColorDialog_options(const XColorDialog* self)
{ return self ? self->m_options : 0; }

/* ==================== 静态便捷函数 ==================== */

XColor XColorDialog_getColor(XColor initial, XWidget* parent,
                             const XString* title, XColorDialogOptions options)
{
    XColorDialog* dlg;
    XColor result;
    (void)parent;
    dlg = XColorDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, initial, parent, 0);
    if (!dlg) return initial;
    if (title)
        XWidget_setWindowTitle((XWidget*)dlg, title);
    XColorDialog_setOptions(dlg, options);
    result = initial;
    XColorDialog_delete_base(dlg);
    return result;
}

XColor XColorDialog_getColor_2(XColor initial, XWidget* parent,
                               const char* title, XColorDialogOptions options)
{
    XString* t = title ? XString_create_utf8(title) : NULL;
    XColor result = XColorDialog_getColor(initial, parent, t, options);
    if (t) XString_delete_base((XClass*)t);
    return result;
}

/* ==================== 信号 ==================== */

void* XColorDialog_currentColorChanged_signal(XColorDialog* self, XColor color)
{
    xcolordialog_emitColor(self, (size_t)XColorDialog_currentColorChanged_signal,
                           color);
    return (void*)(size_t)XColorDialog_currentColorChanged_signal;
}

void* XColorDialog_colorSelected_signal(XColorDialog* self, XColor color)
{
    if (self)
        self->m_selectedColor = color;
    xcolordialog_emitColor(self, (size_t)XColorDialog_colorSelected_signal,
                           color);
    return (void*)(size_t)XColorDialog_colorSelected_signal;
}

#endif /* XWIDGET_ON && XDIALOG_ON */

/* ==================== Task 2.21 回检补齐：自定义/标准颜色 ============== */

int XColorDialog_customCount(const XColorDialog* self)
{
    return self ? self->m_customCount : 0;
}

void XColorDialog_setCustomColor(XColorDialog* self, int index, XColor color)
{
    if (!self || index < 0 || index >= 16) return;
    self->m_customColors[index] = color;
    if (index >= self->m_customCount)
        self->m_customCount = index + 1;
}

XColor XColorDialog_customColor(const XColorDialog* self, int index)
{
    XColor black;
    XColor_init_rgb(&black, 0, 0, 0, 0);
    if (!self || index < 0 || index >= 16) return black;
    return self->m_customColors[index];
}

void XColorDialog_setStandardColor(XColorDialog* self, int index, XColor color)
{
    if (!self || index < 0 || index >= 48) return;
    self->m_standardColors[index] = color;
    if (index >= self->m_standardCount)
        self->m_standardCount = index + 1;
}

XColor XColorDialog_standardColor(const XColorDialog* self, int index)
{
    XColor black;
    XColor_init_rgb(&black, 0, 0, 0, 0);
    if (!self || index < 0 || index >= 48) return black;
    return self->m_standardColors[index];
}

void XColorDialog_open(XColorDialog* self)
{
    if (!self) return;
    XWidget_show((XWidget*)self);
}
