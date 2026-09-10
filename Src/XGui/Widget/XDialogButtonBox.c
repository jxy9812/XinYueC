/**
 * @file       XDialogButtonBox.c
 * @brief      标准按钮盒控件实现（对标 Qt 6.8 QDialogButtonBox 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XDialogButtonBox.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON

/* ==================== 标准按钮表（对标 Qt 标准按钮的角色映射） ==================== */

typedef struct XDBStandard
{
    int standard;
    const char* text;
    int role;
} XDBStandard;

static const XDBStandard g_xdbStandards[] = {
    { (int)XDialogButtonBoxStandard_Ok,              "确定",     (int)XDialogButtonBoxRole_AcceptRole },
    { (int)XDialogButtonBoxStandard_Save,            "保存",     (int)XDialogButtonBoxRole_AcceptRole },
    { (int)XDialogButtonBoxStandard_SaveAll,         "全部保存", (int)XDialogButtonBoxRole_AcceptRole },
    { (int)XDialogButtonBoxStandard_Open,            "打开",     (int)XDialogButtonBoxRole_AcceptRole },
    { (int)XDialogButtonBoxStandard_Yes,             "是",       (int)XDialogButtonBoxRole_YesRole },
    { (int)XDialogButtonBoxStandard_YesToAll,        "全部是",   (int)XDialogButtonBoxRole_YesRole },
    { (int)XDialogButtonBoxStandard_No,              "否",       (int)XDialogButtonBoxRole_NoRole },
    { (int)XDialogButtonBoxStandard_NoToAll,         "全部否",   (int)XDialogButtonBoxRole_NoRole },
    { (int)XDialogButtonBoxStandard_Abort,           "中止",     (int)XDialogButtonBoxRole_DestructiveRole },
    { (int)XDialogButtonBoxStandard_Retry,           "重试",     (int)XDialogButtonBoxRole_AcceptRole },
    { (int)XDialogButtonBoxStandard_Ignore,          "忽略",     (int)XDialogButtonBoxRole_AcceptRole },
    { (int)XDialogButtonBoxStandard_Close,           "关闭",     (int)XDialogButtonBoxRole_RejectRole },
    { (int)XDialogButtonBoxStandard_Cancel,          "取消",     (int)XDialogButtonBoxRole_RejectRole },
    { (int)XDialogButtonBoxStandard_Discard,         "放弃",     (int)XDialogButtonBoxRole_DestructiveRole },
    { (int)XDialogButtonBoxStandard_Help,            "帮助",     (int)XDialogButtonBoxRole_HelpRole },
    { (int)XDialogButtonBoxStandard_Apply,           "应用",     (int)XDialogButtonBoxRole_ApplyRole },
    { (int)XDialogButtonBoxStandard_Reset,           "重置",     (int)XDialogButtonBoxRole_ResetRole },
    { (int)XDialogButtonBoxStandard_RestoreDefaults, "恢复默认", (int)XDialogButtonBoxRole_ResetRole }
};

#define XDB_STANDARD_COUNT (int)(sizeof(g_xdbStandards) / sizeof(g_xdbStandards[0]))

static const XDBStandard* xdb_findStandard(int standard)
{
    int i;
    for (i = 0; i < XDB_STANDARD_COUNT; ++i) {
        if (g_xdbStandards[i].standard == standard)
            return &g_xdbStandards[i];
    }
    return NULL;
}

/* ==================== 成员桥接（per-button 闭包等价物） ==================== */

XCLASS_DEFINE_BEGING(XDBBridge)
XCLASS_DEFINE_EXTEND_END(XDBBridge, XObject)

typedef struct XDBBridge
{
    XObject m_base;
    XDialogButtonBox* m_box;
    XAbstractButton* m_button;
} XDBBridge;

static XVtable* XDBBridge_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDBBridge)
    XVTABLE_INHERIT_XCLASS(XObject);
    return XVTABLE_DEFAULT;
}

static XDBBridge* xdb_bridgeCreate(XDialogButtonBox* box,
                                   XAbstractButton* button)
{
    XDBBridge* bridge =
        (XDBBridge*)XMemory_malloc(sizeof(*bridge),
                                   XCLASS_DEFAULT_MEMORY_TYPE);
    if (!bridge) return NULL;
    memset(bridge, 0, sizeof(*bridge));
    XObject_init(&bridge->m_base);
    XClassSetVtable(bridge, XDBBridge);
    Set_Class_Memory(bridge, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(bridge, true);
    bridge->m_box = box;
    bridge->m_button = button;
    return bridge;
}

static void xdb_emitVoid(XDialogButtonBox* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xdb_emitButton(XDialogButtonBox* self, size_t signal,
                           XAbstractButton* button)
{
    XVarList* args = XVarList_Create(XVar(XAbstractButton*, button));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xdb_bridgeClickedSlot(XObject* receiver, XVarList* args)
{
    XDBBridge* bridge = (XDBBridge*)receiver;
    XDialogButtonBox* self;
    XAbstractButton* button;
    int role;
    if (!bridge || !bridge->m_box || !bridge->m_button) return;
    self = bridge->m_box;
    button = bridge->m_button;
    (void)args;
    role = (int)XDialogButtonBox_buttonRole(self, button);
    if (role == (int)XDialogButtonBoxRole_AcceptRole ||
        role == (int)XDialogButtonBoxRole_YesRole)
        xdb_emitVoid(self, (size_t)XDialogButtonBox_accepted_signal);
    else if (role == (int)XDialogButtonBoxRole_RejectRole ||
             role == (int)XDialogButtonBoxRole_NoRole)
        xdb_emitVoid(self, (size_t)XDialogButtonBox_rejected_signal);
    else if (role == (int)XDialogButtonBoxRole_HelpRole)
        xdb_emitVoid(self, (size_t)XDialogButtonBox_helpRequested_signal);
    xdb_emitButton(self, (size_t)XDialogButtonBox_clicked_signal, button);
}

static void xdb_bridgeDestroy(XDBBridge* bridge)
{
    if (!bridge) return;
    XObject_disconnect_1((XObject*)bridge->m_button,
                         XSignal(XAbstractButton_clicked_signal),
                         (XObject*)bridge, xdb_bridgeClickedSlot);
    XClass_delete_base((XClass*)bridge);
}

/* ==================== 内部排布（第一版：右对齐/居中横排） ==================== */

static void xdb_relayout(XDialogButtonBox* self)
{
    int64_t i;
    int64_t n;
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int bw = 80;
    int bh = 26;
    int gap = 8;
    int total;
    int x;
    if (!self || !self->m_buttons) return;
    n = XVector_size_base((const XContainer*)self->m_buttons);
    total = (int)n * bw + (int)(n > 0 ? n - 1 : 0) * gap;
    x = self->m_center ? (w - total) / 2 : w - total - 4;
    if (x < 4) x = 4;
    for (i = 0; i < n; ++i) {
        XAbstractButton** item =
            (XAbstractButton**)XVector_at_base(self->m_buttons, i);
        XRect r;
        XRect_init(&r, x, (h - bh) / 2, bw, bh);
        if (item && *item)
            XWidget_setGeometryRect((XWidget*)*item, &r);
        x += bw + gap;
    }
}

static void VX_dialogButtonBox_resizeEvent(XWidget* self, XEvent* event)
{
    (void)event;
    xdb_relayout((XDialogButtonBox*)self);
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_dialogButtonBox_deinit(XDialogButtonBox* self)
{
    int64_t i;
    int64_t n;
    if (!self) return;
    n = self->m_bridges ? XVector_size_base(
            (const XContainer*)self->m_bridges) : 0;
    for (i = 0; i < n; ++i) {
        XDBBridge** bp =
            (XDBBridge**)XVector_at_base(self->m_bridges, i);
        xdb_bridgeDestroy(bp ? *bp : NULL);
    }
    if (self->m_bridges) {
        XVector_delete_base(self->m_bridges);
        self->m_bridges = NULL;
    }
    if (self->m_buttons) {
        XVector_delete_base(self->m_buttons);
        self->m_buttons = NULL;
    }
    if (self->m_roles) {
        XVector_delete_base(self->m_roles);
        self->m_roles = NULL;
    }
    if (self->m_standards) {
        XVector_delete_base(self->m_standards);
        self->m_standards = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XDialogButtonBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDialogButtonBox)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VX_dialogButtonBox_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_dialogButtonBox_deinit);
    return XVTABLE_DEFAULT;
}

void XDialogButtonBox_init(XDialogButtonBox* self, XWidget* parent,
                           XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XDialogButtonBox);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_buttons = XVector_Create(XAbstractButton*);
    self->m_roles = XVector_Create(int);
    self->m_standards = XVector_Create(int);
    self->m_bridges = XVector_Create(XDBBridge*);
    self->m_orientation = (int)XDialogButtonBoxLayout_WinLayout;
    self->m_orientation = 1; /* 水平 */
    XWidget_resize(self, 300, 34);
}

XDialogButtonBox* XDialogButtonBox_create_ex(XMemoryType memory,
                                             XWidget* parent,
                                             XWidgetFlags flags)
{
    XDialogButtonBox* self =
        (XDialogButtonBox*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDialogButtonBox_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 方向与布局 ==================== */

void XDialogButtonBox_setOrientation(XDialogButtonBox* self, int orientation)
{
    if (!self) return;
    self->m_orientation = orientation;
    xdb_relayout(self);
}

int XDialogButtonBox_orientation(const XDialogButtonBox* self)
{
    return self ? self->m_orientation : 1;
}

void XDialogButtonBox_setCenterButtons(XDialogButtonBox* self, bool center)
{
    if (!self) return;
    self->m_center = center;
    xdb_relayout(self);
}

bool XDialogButtonBox_centerButtons(const XDialogButtonBox* self)
{
    return self ? self->m_center : false;
}

/* ==================== 按钮管理 ==================== */

void XDialogButtonBox_addButton(XDialogButtonBox* self,
                                XAbstractButton* button,
                                XDialogButtonBoxRole role)
{
    XDBBridge* bridge;
    int zero = 0;
    if (!self || !button || !self->m_buttons) return;
    bridge = xdb_bridgeCreate(self, button);
    if (!bridge) return;
    XVector_push_back_1_base(self->m_buttons, &button);
    XVector_push_back_1_base(self->m_roles, &role);
    XVector_push_back_1_base(self->m_standards, &zero);
    XVector_push_back_1_base(self->m_bridges, &bridge);
    XObject_connect_1((XObject*)button,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)bridge, xdb_bridgeClickedSlot,
                      XConnectionType_Direct);
    XWidget_setParent((XWidget*)button, (XWidget*)self, 0);
    xdb_relayout(self);
}

XPushButton* XDialogButtonBox_addButton_2(XDialogButtonBox* self,
                                          const char* utf8,
                                          XDialogButtonBoxRole role)
{
    XPushButton* button;
    if (!self) return NULL;
    button = XPushButton_create(self, 0);
    if (!button) return NULL;
    XPushButton_setText_2(button, utf8 ? utf8 : "");
    XDialogButtonBox_addButton(self, (XAbstractButton*)button, role);
    return button;
}

XPushButton* XDialogButtonBox_addButton_3(
    XDialogButtonBox* self, XDialogButtonBoxStandardButton which)
{
    const XDBStandard* std;
    XPushButton* button;
    int role;
    int stdVal;
    if (!self) return NULL;
    std = xdb_findStandard((int)which);
    if (!std) return NULL;
    button = XPushButton_create(self, 0);
    if (!button) return NULL;
    XPushButton_setText_2(button, std->text);
    role = std->role;
    stdVal = (int)which;
    {
        XDBBridge* bridge = xdb_bridgeCreate(self, (XAbstractButton*)button);
        if (!bridge) {
            XPushButton_delete_base(button);
            return NULL;
        }
        XVector_push_back_1_base(self->m_buttons, &button);
        XVector_push_back_1_base(self->m_roles, &role);
        XVector_push_back_1_base(self->m_standards, &stdVal);
        XVector_push_back_1_base(self->m_bridges, &bridge);
        XObject_connect_1((XObject*)button,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)bridge, xdb_bridgeClickedSlot,
                          XConnectionType_Direct);
        XWidget_setParent((XWidget*)button, (XWidget*)self, 0);
        xdb_relayout(self);
    }
    return button;
}

static int xdb_bridgeIndexOf(XDialogButtonBox* self, XDBBridge* bridge)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_bridges) return -1;
    n = XVector_size_base((const XContainer*)self->m_bridges);
    for (i = 0; i < n; ++i) {
        XDBBridge** bp =
            (XDBBridge**)XVector_at_base(self->m_bridges, i);
        if (bp && *bp == bridge) return (int)i;
    }
    return -1;
}

void XDialogButtonBox_removeButton(XDialogButtonBox* self,
                                   XAbstractButton* button)
{
    int64_t i;
    int64_t n;
    int index = -1;
    if (!self || !button || !self->m_bridges) return;
    n = XVector_size_base((const XContainer*)self->m_bridges);
    for (i = 0; i < n && index < 0; ++i) {
        XDBBridge** bp =
            (XDBBridge**)XVector_at_base(self->m_bridges, i);
        if (bp && *bp && (*bp)->m_button == button)
            index = (int)i;
    }
    if (index < 0) return;
    {
        XDBBridge** bp =
            (XDBBridge**)XVector_at_base(self->m_bridges, index);
        xdb_bridgeDestroy(bp ? *bp : NULL);
    }
    XVector_remove_base(self->m_bridges, index, 1);
    XVector_remove_base(self->m_buttons, index, 1);
    XVector_remove_base(self->m_roles, index, 1);
    XVector_remove_base(self->m_standards, index, 1);
    xdb_relayout(self);
}

void XDialogButtonBox_clear(XDialogButtonBox* self)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_bridges) return;
    n = XVector_size_base((const XContainer*)self->m_bridges);
    for (i = (int)n - 1; i >= 0; --i) {
        XAbstractButton** btn;
        XDBBridge** bp =
            (XDBBridge**)XVector_at_base(self->m_bridges, i);
        btn = self->m_buttons
                  ? (XAbstractButton**)XVector_at_base(self->m_buttons, i)
                  : NULL;
        xdb_bridgeDestroy(bp ? *bp : NULL);
        XVector_remove_base(self->m_bridges, i, 1);
        if (self->m_buttons)
            XVector_remove_base(self->m_buttons, i, 1);
        if (self->m_roles)
            XVector_remove_base(self->m_roles, i, 1);
        if (self->m_standards)
            XVector_remove_base(self->m_standards, i, 1);
        if (btn && *btn)
            XWidget_setParent((XWidget*)*btn, NULL, 0);
    }
    xdb_relayout(self);
}

const XVector* XDialogButtonBox_buttons(const XDialogButtonBox* self)
{
    return self ? self->m_buttons : NULL;
}

XDialogButtonBoxRole XDialogButtonBox_buttonRole(
    const XDialogButtonBox* self, XAbstractButton* button)
{
    int64_t i;
    int64_t n;
    if (!self || !button || !self->m_buttons || !self->m_roles) return -1;
    n = XVector_size_base((const XContainer*)self->m_buttons);
    for (i = 0; i < n; ++i) {
        XAbstractButton** item =
            (XAbstractButton**)XVector_at_base(self->m_buttons, i);
        int* role = (int*)XVector_at_base(self->m_roles, i);
        if (item && *item == button && role)
            return (XDialogButtonBoxRole)*role;
    }
    return XDialogButtonBoxRole_InvalidRole;
}

void XDialogButtonBox_setStandardButtons(
    XDialogButtonBox* self, int buttons)
{
    int i;
    if (!self) return;
    XDialogButtonBox_clear(self);
    for (i = 0; i < XDB_STANDARD_COUNT; ++i) {
        if (g_xdbStandards[i].standard & buttons)
            XDialogButtonBox_addButton_3(
                self, (XDialogButtonBoxStandardButton)g_xdbStandards[i].standard);
    }
}

int XDialogButtonBox_standardButtons(const XDialogButtonBox* self)
{
    int64_t i;
    int64_t n;
    int mask = 0;
    if (!self || !self->m_standards) return 0;
    n = XVector_size_base((const XContainer*)self->m_standards);
    for (i = 0; i < n; ++i) {
        int* std = (int*)XVector_at_base(self->m_standards, i);
        if (std) mask |= *std;
    }
    return mask;
}

XDialogButtonBoxStandardButton XDialogButtonBox_standardButton(
    const XDialogButtonBox* self, XAbstractButton* button)
{
    int64_t i;
    int64_t n;
    if (!self || !button || !self->m_buttons || !self->m_standards)
        return XDialogButtonBoxStandard_NoButton;
    n = XVector_size_base((const XContainer*)self->m_buttons);
    for (i = 0; i < n; ++i) {
        XAbstractButton** item =
            (XAbstractButton**)XVector_at_base(self->m_buttons, i);
        int* std = (int*)XVector_at_base(self->m_standards, i);
        if (item && *item == button && std)
            return (XDialogButtonBoxStandardButton)*std;
    }
    return XDialogButtonBoxStandard_NoButton;
}

XPushButton* XDialogButtonBox_button(
    const XDialogButtonBox* self, XDialogButtonBoxStandardButton which)
{
    int64_t i;
    int64_t n;
    if (!self || !self->m_buttons || !self->m_standards) return NULL;
    n = XVector_size_base((const XContainer*)self->m_buttons);
    for (i = 0; i < n; ++i) {
        int* std = (int*)XVector_at_base(self->m_standards, i);
        XAbstractButton** item =
            (XAbstractButton**)XVector_at_base(self->m_buttons, i);
        if (std && *std == (int)which && item && *item)
            return (XPushButton*)*item;
    }
    return NULL;
}

/* ==================== 信号 ==================== */

void* XDialogButtonBox_clicked_signal(XDialogButtonBox* self,
                                      XAbstractButton* button)
{
    (void)self;
    (void)button;
    return (void*)(size_t)XDialogButtonBox_clicked_signal;
}

void* XDialogButtonBox_accepted_signal(XDialogButtonBox* self)
{
    (void)self;
    return (void*)(size_t)XDialogButtonBox_accepted_signal;
}

void* XDialogButtonBox_helpRequested_signal(XDialogButtonBox* self)
{
    (void)self;
    return (void*)(size_t)XDialogButtonBox_helpRequested_signal;
}

void* XDialogButtonBox_rejected_signal(XDialogButtonBox* self)
{
    (void)self;
    return (void*)(size_t)XDialogButtonBox_rejected_signal;
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON */
