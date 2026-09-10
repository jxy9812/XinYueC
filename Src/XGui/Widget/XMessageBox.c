/**
 * @file       XMessageBox.c
 * @brief      消息对话框控件实现（对标 Qt 6.8 QMessageBox 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XMessageBox.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include "XDialog.h"
#include <string.h>

#if XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON

/* ==================== 内部工具 ==================== */

static void xmsg_setupText(XMessageBox* self)
{
    XRect r;
    int w = XWidget_width((XWidget*)self);
    if (!self || !self->m_textLabel) return;
    XRect_init(&r, 16, 12, w > 32 ? w - 32 : 0, 60);
    XWidget_setGeometry((XWidget*)self->m_textLabel,
                        r.x, r.y, r.width, r.height);
}

static void xmsg_setupButtonBox(XMessageBox* self)
{
    XRect r;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (!self || !self->m_buttonBox) return;
    XRect_init(&r, 0, h > 40 ? h - 40 : 0, w, 40);
    XWidget_setGeometryRect((XWidget*)self->m_buttonBox, &r);
}

/* ==================== 事件处理 ==================== */

static void VX_messageBox_resizeEvent(XWidget* self, XEvent* event)
{
    XMessageBox* box = (XMessageBox*)self;
    (void)event;
    if (!box) return;
    xmsg_setupText(box);
    xmsg_setupButtonBox(box);
}

/* 按钮盒 accepted/rejected → 结束 exec 循环。 */
static void xmsg_acceptedSlot(XObject* receiver, XVarList* args)
{
    XMessageBox* box = (XMessageBox*)receiver;
    (void)args;
    if (box) box->m_inExec = false;
}

static void xmsg_rejectedSlot(XObject* receiver, XVarList* args)
{
    XMessageBox* box = (XMessageBox*)receiver;
    (void)args;
    if (box) box->m_inExec = false;
}

/* ==================== 生命周期与虚表 ==================== */

XVtable* XMessageBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XMessageBox)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_messageBox_resizeEvent);
    return XVTABLE_DEFAULT;
}

void XMessageBox_init(XMessageBox* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XMessageBox);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_icon = (int)XMessageBoxIcon_NoIcon;
    self->m_inExec = false;
    self->m_text[0] = 0;
#if XDIALOGBUTTONBOX_ON
    self->m_buttonBox = XDialogButtonBox_create(self, 0);
    if (self->m_buttonBox) {
        XObject_connect_1((XObject*)self->m_buttonBox,
            (size_t)XDialogButtonBox_accepted_signal(self->m_buttonBox),
            (XObject*)self, xmsg_acceptedSlot, XConnectionType_Direct);
        XObject_connect_1((XObject*)self->m_buttonBox,
            (size_t)XDialogButtonBox_rejected_signal(self->m_buttonBox),
            (XObject*)self, xmsg_rejectedSlot, XConnectionType_Direct);
    }
    self->m_standards = XVector_Create(int);
#endif
#if XLABEL_ON
    self->m_textLabel = XLabel_create(self, 0);
    if (self->m_textLabel)
        XLabel_setText_2(self->m_textLabel, "");
#endif
    XWidget_resize(self, 320, 140);
}

XMessageBox* XMessageBox_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XMessageBox* self =
        (XMessageBox*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XMessageBox_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 文本与图标 ==================== */

void XMessageBox_setText(XMessageBox* self, const char* utf8)
{
    if (!self || !self->m_textLabel) return;
    XLabel_setText_2(self->m_textLabel, utf8 ? utf8 : "");
    strncpy(self->m_text, utf8 ? utf8 : "",
            sizeof(self->m_text) - 1);
    self->m_text[sizeof(self->m_text) - 1] = 0;
}

const char* XMessageBox_text(const XMessageBox* self)
{
    if (!self || !self->m_textLabel)
        return "";
    return self->m_text;
}

void XMessageBox_setTitle(XMessageBox* self, const char* utf8)
{
    if (!self) return;
    strncpy(self->m_title, utf8 ? utf8 : "",
            sizeof(self->m_title) - 1);
    self->m_title[sizeof(self->m_title) - 1] = '\0';
}

const char* XMessageBox_title(const XMessageBox* self)
{
    return self ? self->m_title : "";
}

void XMessageBox_setIcon(XMessageBox* self, XMessageBoxIcon icon)
{
    if (!self) return;
    self->m_icon = (int)icon;
}

XMessageBoxIcon XMessageBox_icon(const XMessageBox* self)
{
    return self ? (XMessageBoxIcon)self->m_icon
                : XMessageBoxIcon_NoIcon;
}

/* ==================== 按钮管理 ==================== */

void XMessageBox_setStandardButtons(XMessageBox* self, int buttons)
{
    if (!self || !self->m_buttonBox) return;
    XDialogButtonBox_setStandardButtons(self->m_buttonBox, buttons);
    xmsg_setupButtonBox(self);
}

int XMessageBox_standardButtons(const XMessageBox* self)
{
    if (!self || !self->m_buttonBox) return 0;
    return XDialogButtonBox_standardButtons(self->m_buttonBox);
}

XAbstractButton* XMessageBox_button(const XMessageBox* self,
                                    XDialogButtonBoxStandardButton which)
{
    if (!self || !self->m_buttonBox) return NULL;
    return (XAbstractButton*)XDialogButtonBox_button(
        self->m_buttonBox, which);
}

XAbstractButton* XMessageBox_clickedButton(const XMessageBox* self)
{
    return self ? self->m_clicked : NULL;
}

/* ==================== 模态执行 ==================== */

XDialogButtonBoxStandardButton XMessageBox_exec(XMessageBox* self)
{
    XWidget* parent;
    if (!self) return XDialogButtonBoxStandard_NoButton;
    parent = (XWidget*)XObject_parent((XObject*)self);
    XWidget_show((XWidget*)self);
    self->m_inExec = true;
    self->m_clicked = NULL;
    while (self->m_inExec)
        XCoreApplication_processEvents(XEventLoop_AllEvents);
    XWidget_hide((XWidget*)self);
    if (self->m_clicked)
        return XDialogButtonBox_standardButton(self->m_buttonBox,
                                               self->m_clicked);
    return XDialogButtonBoxStandard_NoButton;
}

/* ==================== 静态便捷方法 ==================== */

static XDialogButtonBoxStandardButton xmsg_runStatic(
    XWidget* parent, const char* title, const char* text, int buttons,
    int icon)
{
    XMessageBox* box = XMessageBox_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, parent, 0);
    XDialogButtonBoxStandardButton result;
    if (!box) return XDialogButtonBoxStandard_NoButton;
    XMessageBox_setTitle(box, title);
    XMessageBox_setText(box, text);
    XMessageBox_setIcon(box, (XMessageBoxIcon)icon);
    XMessageBox_setStandardButtons(box, buttons);
    result = XMessageBox_exec(box);
    XMessageBox_delete_base(box);
    return result;
}

XDialogButtonBoxStandardButton XMessageBox_information(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Information);
}

XDialogButtonBoxStandardButton XMessageBox_warning(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Warning);
}

XDialogButtonBoxStandardButton XMessageBox_critical(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Critical);
}

XDialogButtonBoxStandardButton XMessageBox_question(
    XWidget* parent, const char* title, const char* text, int buttons)
{
    return xmsg_runStatic(parent, title, text, buttons,
                          (int)XMessageBoxIcon_Question);
}

void XMessageBox_about(XWidget* parent, const char* title,
                       const char* text)
{
    xmsg_runStatic(parent, title, text, (int)XDialogButtonBoxStandard_Ok,
                   (int)XMessageBoxIcon_Information);
}

#endif /* XWIDGET_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON */