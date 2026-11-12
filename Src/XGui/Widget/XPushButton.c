/******************************************************************************
 * @file       XPushButton.c
 * @brief      XPushButton 按钮控件实现（对标 Qt 6.8 QPushButton，继承 XAbstractButton）。
 * @details    实现要点：
 *             - 继承 XAbstractButton（对齐 Qt QPushButton : QAbstractButton），
 *               QAbstractButton 的公共状态、激活流程、自动重复/动画定时器、
 *               自动互斥、输入事件与 pressed/released/clicked/toggled 信号
 *               全部由基类提供，本文件不再重复实现；
 *             - 本类只实现 QPushButton 特有部分：
 *               - autoDefault/default/flat 三态与父对话框链自动解析；
 *               - setMenu/menu/showMenu 菜单关联：pressed→弹层桥与
 *                 showMenu 真弹层（对标 qpushbutton.cpp:521-568，菜单
 *                 收起经 aboutToHide 恢复按钮按下态）；
 *               - sizeHint/minimumSizeHint 与 drawContents 绘制入口；
 *               - 键盘 Return/Enter 在默认或 autoDefault 生效时触发 click，
 *                 Space/Escape 等按键交基类处理；
 *               - 重载 XAbstractButton 的 contentChanged 保护槽，在
 *                 文本/图标/图标尺寸变化后刷新自身 sizeHint 存储位；
 *             - 生命周期走 XClass 体系：XPushButton_init/create_ex 挂
 *               XPushButton 虚表，copy/move/deinit 经 XClass_Parent 先调
 *               XAbstractButton 基类槽位再处理本类特有字段；
 *             - 绘制：XPainter 输出 raised/sunken/flat 外观，文本经当前
 *               XFont（内置 8x16 点阵字库回退），图标走 XIcon_paint；
 *             - 不依赖任何平台 API；嵌入式由 XPUSHBUTTON_ON 裁剪，且
 *               依赖 XABSTRACTBUTTON_ON（基类裁剪时本类一并裁剪）。
 * @note       近似边界：显式 QButtonGroup 登记由 XButtonGroup 承担（基类
 *             据 group() 承载位识别独占/任意组成员，本类不另行登记）；
 *             同一父控件的 autoExclusive 互斥已由基类按 Qt 规则处理；
 *             命中区按 Qt 走 style()->subElementRect(SE_PushButtonBevel)
 *             （默认样式 = 控件矩形，QSS margin 收窄待样式批次）；
 *             助记符 '&' 的显示/度量与 Alt+字符 shortcut 文本已实现
 *             （激活受 XShortcut 单键注册表限制，暂不登记裸键绑定）；
 *             菜单关联为借用语义，弹层经 XMenu_popup 非阻塞弹出
 *             （Qt showMenu 文档的阻塞语义由调用方事件循环承担）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XPushButton.h"

#include "XAlgorithm.h"
#include "XAbstractButton_Protected.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XWidget_Protected.h"
#include "XPainter.h"
#include "XPalette.h"
#include "XColor.h"
#include "XFont.h"
#include "XWindowEvent.h"
#include "XMemory.h"
#include "XString.h"
#include "XAlignment.h"
/* 复扫 R-76 配套：默认位互斥需识别 XPushButton 全部现役派生（vtable
 * 无父链可遍历，见 pushbutton_clearSiblingDefaults 注）。 */
#include "XCommandLinkButton.h"

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON

/* ==================== 内部工具 ==================== */

/* 菜单桥槽（定义在菜单节；copy/move/deinit 先行引用）。 */
static void pushbutton_menuHideSlot(XObject* receiver, XVarList* args);
static void pushbutton_popupPressedSlot(XObject* receiver, XVarList* args);

/** @brief 从控件调色板读取颜色；调色板裁剪时回退黑色。 */
static uint32_t pushbutton_color(const XPushButton* self,
                                 XPaletteColorGroup group,
                                 XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette;
    XColor color;
    if (!self) return 0xFF000000u;
    palette = XWidget_palette((const XWidget*)self);
    color = XPalette_color(&palette, group, role);
    return XColor_rgba(&color);
#else
    (void)self;
    (void)group;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/** @brief 沿父链查找所属对话框（窗口边界截停）。
 * @details 对标 Qt 6.8 QPushButtonPrivate::dialogParent/autoDefault：
 *          沿父链向上遍历到第一个窗口前，命中窗口类型为 Dialog 的祖先
 *          控件即返回之；按钮自身为窗口或父链无对话框返回 NULL。
 *          autoDefault 判定与 setDefault 的默认位互斥共用本解析。 */
static XWidget* pushbutton_dialogParent(const XPushButton* self)
{
    const XWidget* p = (const XWidget*)self;

    while (p && !XWidget_isWindow(p)) {
        p = XWidget_parentWidget(p);
        if (p && XWidget_windowType(p) == XWindowType_Dialog)
            return (XWidget*)p;
    }
    return NULL;
}

/** @brief 是否三态 Auto 且当前上下文判定为对话框默认按钮。 */
static bool pushbutton_autoDefaultActive(const XPushButton* self)
{
    return pushbutton_dialogParent(self) != NULL;
}

/** @brief 是否把当前尺寸视为显式图标尺寸（字段位于 XAbstractButton 基类）。 */
static bool pushbutton_hasIconSize(const XPushButton* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;

    return ab && XSize_isValid(&ab->m_iconSize) &&
           ab->m_iconSize.width > 0 && ab->m_iconSize.height > 0;
}

/** @brief 返回按钮图标用于尺寸提示的渲染尺寸（未显式设置按 PM_ButtonIconSize=16）。 */
static XSize pushbutton_hintIconSize(const XPushButton* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;
    XSize out;

    XSize_init(&out, 16, 16);
    if (pushbutton_hasIconSize(self))
        out = ab->m_iconSize;
    return out;
}

/** @brief 按 Qt 6.8 QPushButton::sizeHint 内容语义计算建议尺寸。 */
static XSize pushbutton_computeSizeHint(const XPushButton* self)
{
    const XAbstractButton* ab = (const XAbstractButton*)self;
    XSize out;
    XSize iconSize;
    const char* text;
    XString* display = NULL;
    bool hasIcon;
    bool empty;
    int w = 0;
    int h = 0;
    XFont font;
    if (!self) { XSize_init(&out, -1, -1); return out; }
    hasIcon = !XIcon_isNull(&ab->m_icon);
    if (hasIcon) {
        iconSize = pushbutton_hintIconSize(self);
        w += iconSize.width + 4;
        if (h < iconSize.height) h = iconSize.height;
    }
    font = XWidget_font((XWidget*)self);
    /* 度量显示文本（Qt sizeFromContents/TextShowMnemonic：'&' 不占宽）。 */
    text = XAbstractButton_displayTextUtf8(ab, &display);
    if (!text) text = "";
    empty = text[0] == '\0';
    if (empty) {
        int placeholder;
        placeholder = XPainter_textWidth(&font, "XXXX");
        if (!w) w += placeholder;
        if (!h) h = XPainter_textHeight(&font);
    } else {
        w += XPainter_textWidth(&font, text);
        if (h < XPainter_textHeight(&font))
            h = XPainter_textHeight(&font);
    }
    XClassDeinit(&font);
    if (display)
        XClassDelete((XClass*)display);
    /* [sizeHint 收口 2026-10-07] 边距/指示器改走风格：与绘制路径同经
     * XStyle（含样式表切换一致性），删除硬编码 +12/+6+4（Fusion
     * DefaultFrameWidth=1 时 Qt 语义应 +8，旧硬编码恒 +10 差 2px）。 */
    {
        XStyleOption opt;
        XSize content;
        XSize size;
        XStyleOption_init(&opt, XStyleCT_PushButton);
        if (self->m_menu)
            w += XStyle_pixelMetric(XWidget_style((const XWidget*)self),
                                    XStylePM_MenuButtonIndicator, &opt);
        XSize_init(&content, w, h);
        size = XStyle_sizeFromContents(XWidget_style((const XWidget*)self),
                                       XStyleCT_PushButton, &opt, content);
        XSize_init(&out, size.width, size.height);
        return out;
    }
}

/** @brief 刷新基类 XWidget 的 sizeHint/minimumSizeHint 存储位。 */
static void pushbutton_refreshSizeHint(XPushButton* self)
{
    XSize hint;
    if (!self) return;
    hint = pushbutton_computeSizeHint(self);
    XWidget_setSizeHint((XWidget*)self, &hint);
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
}

/* ==================== 生命周期（对标 QPushButton 构造/析构） ==================== */

static void VXPushButton_copy(XPushButton* self, const XPushButton* other);
static void VXPushButton_move(XPushButton* self, XPushButton* other);
static void VXPushButton_deinit(XPushButton* self);

void XPushButton_init(XPushButton* self, XWidget* parent, XWidgetFlags flags)
{
    XWidgetSizePolicy policy;
    if (!self) return;
    XMemset(self, 0, sizeof(XPushButton));
    /* 基类初始化：文本/图标/状态位/定时器/调色板角色/焦点策略全部由
       XAbstractButton_init 设置 Qt 默认值。 */
    XAbstractButton_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XPushButton);
    self->m_flat = false;
    self->m_defaultButton = false;
    self->m_autoDefault = XPushButtonAutoDefault_Auto;
    self->m_menu = NULL;
    policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Minimum,
                                         XWidgetSizePolicy_Fixed,
                                         XWidgetSizePolicyControl_PushButton);
    XWidget_setSizePolicyFull((XWidget*)self, &policy);
    pushbutton_refreshSizeHint(self);
}

XPushButton* XPushButton_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XPushButton* self = (XPushButton*)XMemory_malloc(sizeof(XPushButton), memory);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(XPushButton));
    XPushButton_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXPushButton_copy(XPushButton* self, const XPushButton* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XPushButton_init(self, NULL, 0);
    XClass_Parent(XAbstractButton, EXClass_Copy,
                  void(*)(XAbstractButton*, const XAbstractButton*))(
                      (XAbstractButton*)self, (const XAbstractButton*)other);
    self->m_flat = other->m_flat;
    self->m_defaultButton = other->m_defaultButton;
    self->m_autoDefault = other->m_autoDefault;
    self->m_menu = other->m_menu;
}

static void VXPushButton_move(XPushButton* self, XPushButton* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XPushButton_init(self, NULL, 0);
    XClass_Parent(XAbstractButton, EXClass_Move,
                  void(*)(XAbstractButton*, XAbstractButton*))(
                      (XAbstractButton*)self, (XAbstractButton*)other);
    /* 菜单接线的连接接收者是按钮对象本身，不可随移动换主：先拆双方
     * 接线，转移指针后按 self 重连（对标 C++ 移动后重挂 lambda 的语义）。
     */
    if (self->m_menu) {
        XObject_disconnect((XObject*)self->m_menu,
                             XSignal(XMenu_aboutToHide_signal),
                             (XObject*)self, pushbutton_menuHideSlot);
        XObject_disconnect((XObject*)self,
                             XSignal(XAbstractButton_pressed_signal),
                             (XObject*)self, pushbutton_popupPressedSlot);
    }
    if (other->m_menu) {
        XObject_disconnect((XObject*)other->m_menu,
                             XSignal(XMenu_aboutToHide_signal),
                             (XObject*)other, pushbutton_menuHideSlot);
        XObject_disconnect((XObject*)other,
                             XSignal(XAbstractButton_pressed_signal),
                             (XObject*)other, pushbutton_popupPressedSlot);
    }
    self->m_flat = other->m_flat;
    self->m_defaultButton = other->m_defaultButton;
    self->m_autoDefault = other->m_autoDefault;
    self->m_menu = other->m_menu;
    other->m_menu = NULL;
    if (self->m_menu) {
        XObject_connect((XObject*)self->m_menu,
                          XSignal(XMenu_aboutToHide_signal),
                          (XObject*)self, pushbutton_menuHideSlot,
                          XConnectionType_Direct);
        XObject_connect((XObject*)self,
                          XSignal(XAbstractButton_pressed_signal),
                          (XObject*)self, pushbutton_popupPressedSlot,
                          XConnectionType_Direct);
    }
    other->m_flat = false;
    other->m_defaultButton = false;
    other->m_autoDefault = XPushButtonAutoDefault_Auto;
}

static void VXPushButton_deinit(XPushButton* self)
{
    if (!self) return;
    if (self->m_menu) {
        XObject_disconnect((XObject*)self->m_menu,
                             XSignal(XMenu_aboutToHide_signal),
                             (XObject*)self, pushbutton_menuHideSlot);
        XObject_disconnect((XObject*)self,
                             XSignal(XAbstractButton_pressed_signal),
                             (XObject*)self, pushbutton_popupPressedSlot);
    }
    self->m_menu = NULL;
    XClass_Deinit_Parent(XAbstractButton, (XAbstractButton*)self);
}

/* ==================== 命中（QAbstractButton protected，经基类虚表分派） ==================== */

bool XPushButton_hitButton(const XPushButton* self, const XPoint* pos)
{
#if XSTYLE_ON
    XStyle* style;
    XStyleOptionButton opt;
    XRect bevel;
#endif /* XSTYLE_ON */

    if (!self || !pos)
        return false;
#if !XSTYLE_ON
    /* 样式子系统裁剪时无 bevel 可查，回落基类控件矩形命中。 */
    return XAbstractButton_hitButton_base((const XAbstractButton*)self, pos);
#else
    /*
     * 对标 Qt 6.8 QPushButton::hitButton（qpushbutton.cpp:499-505）：
     *   QStyleOptionButton opt; initStyleOption(&opt);
     *   return style()->subElementRect(SE_PushButtonBevel, &opt, this)
     *          .contains(pos);
     * 默认 QCommonStyle 的 SE_PushButtonBevel == opt->rect（qcommonstyle.cpp:
     * 2495-2499），与基类矩形命中一致；QStyleSheetStyle 在控件规则带
     * box/非 native 边框时返回 rule.borderRect(opt->rect)（qstylesheetstyle.cpp:
     * 6147-6166），即按 QSS margin 收窄命中区。
     * @note 本库 XStyleSheetStyle 目前未重载 SubElementRect，QSS margin
     *       收窄仍未生效（需样式批次的 rule borderRect 几何），此处先
     *       把 Qt 的样式调用链接通：默认样式命中与旧行为逐点一致，
     *       样式后端补齐后无需再改按钮。
     */
    style = XWidget_style((const XWidget*)self);
    if (!style)
        return XAbstractButton_hitButton_base((const XAbstractButton*)self,
                                               pos);
    XStyleOptionButton_init(&opt, XStyleCT_PushButton);
    opt.m_base.m_rect = XWidget_rect((const XWidget*)self);
    bevel = XStyle_subElementRect(style, XStyleSE_PushButtonBevel,
                                  &opt.m_base, (const XWidget*)self);
    return XRect_contains(&bevel, pos->x, pos->y);
#endif /* XSTYLE_ON */
}

/* ==================== 默认/扁平（对标 QPushButton） ==================== */

bool XPushButton_autoDefault(const XPushButton* self)
{
    if (!self) return false;
    if (self->m_autoDefault == XPushButtonAutoDefault_On)
        return true;
    if (self->m_autoDefault == XPushButtonAutoDefault_Off)
        return false;
    return pushbutton_autoDefaultActive(self);
}

void XPushButton_setAutoDefault(XPushButton* self, bool enable)
{
    XPushButtonAutoDefault value;
    if (!self) return;
    value = enable ? XPushButtonAutoDefault_On : XPushButtonAutoDefault_Off;
    if (self->m_autoDefault == value) return;
    self->m_autoDefault = value;
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

bool XPushButton_isDefault(const XPushButton* self)
{
    return self ? self->m_defaultButton : false;
}

/** @brief 递归清位：对话框子树内其他默认按钮解除 default 位。
 * @details 对标 Qt QPushButton::setDefault(true) 的
 *          dlg->findChildren<QPushButton*>() 清位循环。框架 vtable 无
 *          父类链可遍历（XVtable 无继承元数据），无法做「XPushButton
 *          及其派生」的通用 qobject_cast 等价判定，故按现役派生清单
 *          （XPushButton/XCommandLinkButton）精确匹配 vtable；新增
 *          派生类需在此补登记，否则其默认位不参与互斥（保守方向：
 *          只可能漏清位，不会误清非按钮对象）。 */
static void pushbutton_clearSiblingDefaults(XObject* object,
                                            XPushButton* except)
{
    const XVector* children;
    XVector* kids;
    int64_t i;
    int64_t n;
    if (!object) return;
    children = XObject_children(object);
    if (!children) return;
    kids = (XVector*)children;
    n = XVector_size_base((const XContainer*)kids);
    for (i = 0; i < n; ++i) {
        XObject** slot = (XObject**)XVector_at_base(kids, i);
        XObject* child;
        if (!slot) continue;
        child = *slot;
        if (!child) continue;
        if (child != (XObject*)except) {
            XVtable* vt = XClassGetVtable(child);

            if (vt == XPushButton_class_init()
#if XCOMMANDLINKBUTTON_ON
                || vt == XCommandLinkButton_class_init()
#endif
            ) {
                XPushButton* btn = (XPushButton*)child;
                if (btn->m_defaultButton) {
                    btn->m_defaultButton = false;
                    XWidget_update((XWidget*)btn);
                }
            }
        }
        pushbutton_clearSiblingDefaults(child, except);
    }
}

void XPushButton_setDefault(XPushButton* self, bool enable)
{
    if (!self || self->m_defaultButton == enable) return;
    self->m_defaultButton = enable;
    /* 复扫 R-76（PushButton 半边）：默认位对话框内互斥——此前纯存储
     * 可多个同真。对标 Qt：setDefault(true) 沿父链找所属对话框，清掉
     * 其子树内其他按钮的 default 位。注：对话框级 Enter→默认按钮派发
     * 属 XDialog 键盘路径（XDialog.c VXDialog_keyPressEvent 补 Enter
     * 分支），不在本文件所有权内（事件过滤器与快捷键路径经查证均不
     * 覆盖控件按键，见 XWidget_sendEvent/XShortcut.h @note）。 */
    if (enable) {
        XWidget* dlg = pushbutton_dialogParent(self);
        if (dlg)
            pushbutton_clearSiblingDefaults((XObject*)dlg, self);
    }
    XWidget_update((XWidget*)self);
}

bool XPushButton_isFlat(const XPushButton* self)
{
    return self ? self->m_flat : false;
}

void XPushButton_setFlat(XPushButton* self, bool flat)
{
    if (!self || self->m_flat == flat) return;
    self->m_flat = flat;
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/* ==================== 菜单（对标 QPushButton setMenu/menu/showMenu） ==================== */

/* 菜单收起槽：对标 QPushButtonPrivate::popupPressed 内 aboutToHide→
 * setDown(false) 恢复（SingleShotConnection 语义由先断后连等价承担）。 */
static void pushbutton_menuHideSlot(XObject* receiver, XVarList* args)
{
    XPushButton* self = (XPushButton*)receiver;

    (void)args;
    if (self)
        XAbstractButton_setDown((XAbstractButton*)self, false);
}

/* pressed→弹层桥（对标 QPushButtonPrivate::popupPressed，qpushbutton.cpp
 * :569+：!down||!menu 早退后弹层；press 路径 down 已由输入链置位）。 */
static void pushbutton_popupPressedSlot(XObject* receiver, XVarList* args)
{
    XPushButton* self = (XPushButton*)receiver;

    (void)args;
    if (!self || !self->m_menu)
        return;
    if (!XAbstractButton_isDown((XAbstractButton*)self))
        return;
    if (!XWidget_isEnabled((XWidget*)self))
        return;
    XPushButton_showMenu(self);
}

void XPushButton_setMenu(XPushButton* self, XMenu* menu)
{
    if (!self || self->m_menu == menu) return;
    /* 对标 Qt qpushbutton.cpp:521-547：pressed→popupPressed 以
     * UniqueConnection 建立（先断后连防重入）；菜单侧 aboutToHide 恢复
     * 按钮按下态。Qt 另把 menuAction 挂进按钮 action 列表，本库按钮无
     * action 列表，等价职责（菜单归按钮触发）由本桥承担。 */
    if (self->m_menu) {
        XObject_disconnect((XObject*)self->m_menu,
                             XSignal(XMenu_aboutToHide_signal),
                             (XObject*)self, pushbutton_menuHideSlot);
        XObject_disconnect((XObject*)self,
                             XSignal(XAbstractButton_pressed_signal),
                             (XObject*)self, pushbutton_popupPressedSlot);
    }
    self->m_menu = menu;
    if (menu) {
        XObject_connect((XObject*)menu,
                          XSignal(XMenu_aboutToHide_signal),
                          (XObject*)self, pushbutton_menuHideSlot,
                          XConnectionType_Direct);
        XObject_connect((XObject*)self,
                          XSignal(XAbstractButton_pressed_signal),
                          (XObject*)self, pushbutton_popupPressedSlot,
                          XConnectionType_Direct);
    }
    pushbutton_refreshSizeHint(self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

XMenu* XPushButton_menu(const XPushButton* self)
{
    return self ? self->m_menu : NULL;
}

void XPushButton_showMenu(XPushButton* self)
{
    XPoint global;
    XPoint pos;

    if (!self || !self->m_menu) return;
    if (!XWidget_isEnabled((XWidget*)self)) return;
    /* 对标 Qt qpushbutton.cpp:560-568 showMenu→popupPressed：setDown(true)
     * 后弹层；aboutToHide 时恢复 down（同 XToolButton_showMenu 口径）。
     * 弹出位置取按钮全局坐标正下方（对标 adjustedMenuPosition 的按钮
     * 下方对齐）。 */
    XAbstractButton_setDown((XAbstractButton*)self, true);
    XObject_disconnect((XObject*)self->m_menu,
                         XSignal(XMenu_aboutToHide_signal),
                         (XObject*)self, pushbutton_menuHideSlot);
    XObject_connect((XObject*)self->m_menu,
                      XSignal(XMenu_aboutToHide_signal),
                      (XObject*)self, pushbutton_menuHideSlot,
                      XConnectionType_Direct);
    /* [弹窗随转·逻辑全局 2026-10-22] mapToGlobal 返回设备全局（旋转正变
       换后），XMenu_popup→setGeometryRect 需逻辑全局——90/270 下菜单错
       位（同 XComboBox xcombo_popupReposition 根修）。自持父链累加逻辑
       偏移+顶层逻辑原点，rot==0 与 mapToGlobal 逐位一致。 */
    XPoint_init(&global, 0, 0);
    {
        XWidget* top = XWidget_topLevelWidget((XWidget*)self);
        const XWidget* w = (const XWidget*)self;
        while (w && !w->m_isWindow) {
            global.x += w->m_windowRect.x;
            global.y += w->m_windowRect.y;
            w = (const XWidget*)XObject_parent((XObject*)w);
        }
        if (top) {
            global.x += top->m_windowRect.x;
            global.y += top->m_windowRect.y;
        }
    }
    XPoint_init(&pos, global.x,
                global.y + XWidget_height((XWidget*)self));
    XMenu_popup(self->m_menu, &pos);
}

/* ==================== 尺寸（对标 QPushButton sizeHint/minimumSizeHint） ==================== */

/* SizeHint 虚槽体（对标 Qt QPushButton::sizeHint；《api命名风格》
 * `*_base` 契约：业务实现只放 VX* 槽，经 XPushButton_class_init 注册
 * 虚表，对外唯一入口是 XWidget_sizeHint_base 虚分派，不再设无后缀
 * 公开函数与转发桥）。 */
static XSize VXPushButton_sizeHint(const XWidget* self)
{
    return pushbutton_computeSizeHint((const XPushButton*)self);
}

/* MinimumSizeHint 虚槽体（对标 Qt 6.8 qpushbutton.h：minimumSizeHint
 * 恒等 sizeHint。同文件业务直调 VXPushButton_sizeHint，不经虚表再入，
 * 避免递归；无后缀公开入口 XPushButton_minimumSizeHint 已按《api命名
 * 风格》`*_base` 契约删除，多态取值走 XWidget_minimumSizeHint_base）。 */
static XSize VXPushButton_minimumSizeHint(const XWidget* self)
{
    return VXPushButton_sizeHint(self);
}

/* ==================== 绘制（对标 QPushButton::paintEvent 内容） ==================== */

void XPushButton_drawContents(XPushButton* self, XPainter* painter)
{
    XAbstractButton* ab = (XAbstractButton*)self;
    XRect rect, content;
    uint32_t bg, textColor, light, dark, mid;
    XPaletteColorGroup group;
    bool pressed;
    XFont font;
    const char* text;
    XString* display = NULL;
    bool drawIcon;
    int iconW, iconH, iconX, iconY;

    if (!self || !painter) return;
    if (!XPainter_save(painter)) return;
    rect = XWidget_rect((XWidget*)self);
    group = XWidget_isEnabled((XWidget*)self) ? XPaletteColorGroup_Current
                                              : XPaletteColorGroup_Disabled;
#if XPALETTE_ON
    light = pushbutton_color(self, group, XPaletteColorRole_Light);
    dark = pushbutton_color(self, group, XPaletteColorRole_Dark);
    mid = pushbutton_color(self, group, XPaletteColorRole_Mid);
#else
    light = 0xFFE0E0E0u;
    dark = 0xFF808080u;
    mid = 0xFFA0A0A0u;
#endif
    bg = pushbutton_color(self, group, XPaletteColorRole_Button);
    textColor = pushbutton_color(self, group, XPaletteColorRole_ButtonText);
    if (bg == 0u)
        bg = 0xFFCFCFCFu;
    pressed = ab->m_down || ab->m_checked;

#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        /* Fusion/公共风格接管：面板 + 焦点框由样式引擎绘制。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        XStyleOption_init(&opt, XStylePE_PanelButtonCommand);
        opt.m_rect = rect;
        opt.m_state = pressed
            ? (XStyleState_Enabled | XStyleState_Sunken | XStyleState_On)
            : (XStyleState_Enabled | XStyleState_Raised);
        if (!XWidget_isEnabled((XWidget*)self))
            opt.m_state &= ~(uint32_t)XStyleState_Enabled;
        if (XWidget_underMouse((XWidget*)self) &&
            XWidget_isEnabled((XWidget*)self))
            opt.m_state |= XStyleState_MouseOver;
        if (XWidget_hasFocus((XWidget*)self))
            opt.m_state |= XStyleState_HasFocus;
        opt.m_flat = self->m_flat;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((XWidget*)self);
#endif
        XStyle_drawPrimitive(style, XStylePE_PanelButtonCommand, &opt,
                             painter, (XWidget*)self);
        if (XWidget_hasFocus((XWidget*)self) && !self->m_flat) {
            XStyleOption foc = opt;
            XRect fr = rect;
            fr.x += 3;
            fr.y += 3;
            fr.width -= 6;
            fr.height -= 6;
            foc.m_type = XStylePE_FrameFocusRect;
            foc.m_rect = fr;
            XStyle_drawPrimitive(style, XStylePE_FrameFocusRect, &foc,
                                 painter, (XWidget*)self);
        }
        goto xpb_style_label;
    }
#endif /* XSTYLE_ON */
    XPainter_fillRect(painter, &rect, bg);
    if (!self->m_flat && rect.width > 2 && rect.height > 2) {
        XRect edge;
        if (!pressed) {
            edge.x = rect.x; edge.y = rect.y;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, light);
            edge.x = rect.x; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, light);
            edge.x = rect.x; edge.y = rect.y + rect.height - 1;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, dark);
            edge.x = rect.x + rect.width - 1; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, mid);
        } else {
            edge.x = rect.x; edge.y = rect.y;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, dark);
            edge.x = rect.x; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, mid);
            edge.x = rect.x; edge.y = rect.y + rect.height - 1;
            edge.width = rect.width; edge.height = 1;
            XPainter_fillRect(painter, &edge, light);
            edge.x = rect.x + rect.width - 1; edge.y = rect.y;
            edge.width = 1; edge.height = rect.height;
            XPainter_fillRect(painter, &edge, light);
        }
    }

xpb_style_label:
    font = XWidget_font((XWidget*)self);
    XPainter_setFont(painter, &font);
    /* 显示文本去助记符 '&'（Qt TextShowMnemonic）。 */
    text = XAbstractButton_displayTextUtf8(ab, &display);
    drawIcon = !XIcon_isNull(&ab->m_icon);
    iconW = 16;
    iconH = 16;
    if (pushbutton_hasIconSize(self)) {
        iconW = ab->m_iconSize.width;
        iconH = ab->m_iconSize.height;
    }
    content = rect;
    if (drawIcon) {
        iconX = rect.x + (rect.width - iconW) / 2;
        iconY = rect.y + (rect.height - iconH) / 2;
        if (text && text[0] != '\0') {
            iconX = rect.x + 4;
            iconY = rect.y + (rect.height - iconH) / 2;
            content.x += iconW + 4;
            content.width -= iconW + 4;
        }
        XIcon_paint(&ab->m_icon, painter, iconX, iconY, iconW, iconH,
                    XAlignment_Center,
                    XWidget_isEnabled((XWidget*)self) ? XIconMode_Normal
                                                      : XIconMode_Disabled,
                    ab->m_checked ? XIconState_On : XIconState_Off);
    }
    if (text && text[0] != '\0') {
        if (content.width < 4) content.width = 4;
#if XPAINTER_TEXTLAYOUT_ON
        XPainter_drawTextRect(painter, &content,
                              XPAINTER_TEXT_ALIGN_CENTER | XPAINTER_TEXT_SINGLE_LINE,
                              text, textColor);
#else
        /* 文本布局被裁剪时保留按钮可用性：用点阵字体度量做单行居中。 */
        {
            int textW = XPainter_textWidth(&font, text);
            int textH = XPainter_textHeight(&font);
            int ascent = XPainter_textAscent(&font);
            int textX = content.x + (content.width - textW) / 2;
            int baseline = content.y + (content.height - textH) / 2 + ascent;
            XPainter_drawText(painter, textX, baseline, text, textColor);
        }
#endif /* XPAINTER_TEXTLAYOUT_ON */
    }
    XClassDeinit(&font);
    if (display) {
        XClassDelete((XClass*)display);
        display = NULL;
    }
    if (self->m_menu && rect.width >= 12 && rect.height >= 7) {
        int arrowX = rect.x + rect.width - 9;
        int arrowY = rect.y + (rect.height / 2) - 2;
        XRect tri;
        tri.x = arrowX + 2; tri.y = arrowY; tri.width = 1; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX + 1; tri.y = arrowY + 1; tri.width = 3; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX; tri.y = arrowY + 2; tri.width = 5; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX + 1; tri.y = arrowY + 3; tri.width = 3; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
        tri.x = arrowX + 2; tri.y = arrowY + 4; tri.width = 1; tri.height = 1;
        XPainter_fillRect(painter, &tri, textColor);
    }
    XPainter_restore(painter);
}

/* ==================== 虚槽实现（对标 QPushButton 事件） ==================== */

static void VXPushButton_paintEvent(XWidget* self, XEvent* event)
{
#if !XPALETTE_ON || !XWINDOWEVENT_ON
    (void)self;
    (void)event;
    return;
#else
    XImage* image;
    XPoint offset;
    XPainter painter;
#if XPAINTER_CLIP_ON
    XPaintEvent* pe;
    XRect clip;
#endif
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
#if XPAINTER_CLIP_ON
    pe = (XPaintEvent*)event;
#endif
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPAINTER_CLIP_ON
    clip = XPaintEvent_rect(pe);
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_ReplaceClip);
#endif
    XPushButton_drawContents((XPushButton*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XPALETTE_ON && XWINDOWEVENT_ON */
}

static void VXPushButton_keyPressEvent(XWidget* self, XEvent* event)
{
    XPushButton* button = (XPushButton*)self;
    XKeyEvent* keyEvent;
    int key;

    if (!button || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS)
        return;
    keyEvent = (XKeyEvent*)event;
    key = XKeyEvent_key(keyEvent);
    /* 对标 QPushButton::keyPressEvent：Enter/Return 仅在默认或
       autoDefault 生效时触发 click，其余按键（Space/Escape 等）交给
       XAbstractButton 基类处理。 */
    if (key == XKey_Return || key == XKey_Enter) {
        if (XPushButton_autoDefault(button) || XPushButton_isDefault(button)) {
            XPushButton_click(button);
            XEvent_accept(event);
            return;
        }
    }
    XClass_Parent(XAbstractButton, EXWidget_KeyPressEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/* ==================== 焦点与对话框默认按钮（对标 QPushButton 焦点事件） ==================== */

/**
 * @brief      重载 FocusIn：autoDefault 生效时把按钮提升为对话框默认按钮。
 * @details    对标 Qt 6.8 QPushButton::focusInEvent（qpushbutton.cpp:
 *             438-453）：焦点原因不是 PopupFocusReason、autoDefault()
 *             为真且当前不是默认按钮 ⇒ defaultButton=true，并清掉同一
 *             对话框内其它按钮的默认位（QDialogPrivate::setDefault 的
 *             等价实现，见 pushbutton_clearSiblingDefaults）。
 *             本机 Qt 6.8 探针实测（QDialog 内 autoDefault 缺省按钮）：
 *             show() + setFocus() 后 autoDefault=1、isDefault 由 0 变 1。
 */
static void VXPushButton_focusInEvent(XWidget* self, XEvent* event)
{
    XPushButton* button = (XPushButton*)self;
    XFocusReason reason = XFocusReason_Other;

    if (!button || !event)
        return;
#if XWINDOWEVENT_ON
    if (XFocusEvent_gotFocus((const XFocusEvent*)event))
        reason = XFocusEvent_reason((const XFocusEvent*)event);
#endif /* XWINDOWEVENT_ON */
    if (reason != XFocusReason_Popup && XPushButton_autoDefault(button) &&
        !button->m_defaultButton) {
        XWidget* dlg;

        button->m_defaultButton = true;
        dlg = pushbutton_dialogParent(button);
        if (dlg)
            pushbutton_clearSiblingDefaults((XObject*)dlg, button);
        XWidget_update(self);
    }
    XClass_Parent(XAbstractButton, EXWidget_FocusInEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/**
 * @brief      重载 FocusOut：autoDefault 生效的默认按钮失焦即交还默认位。
 * @details    对标 Qt 6.8 QPushButton::focusOutEvent（qpushbutton.cpp:
 *             455-476）：焦点原因不是 PopupFocusReason、autoDefault()
 *             为真且当前是默认按钮 ⇒ 清默认位（Qt 对对话框调用
 *             setDefault(nullptr)，即清掉对话框内所有默认位；无对话框
 *             时只清自己）。
 *             本机 Qt 6.8 探针实测：两个 autoDefault 按钮先后 setFocus
 *             ⇒ 焦点在两个按钮间迁移时 isDefault 随之迁移（0/1 互换）。
 */
static void VXPushButton_focusOutEvent(XWidget* self, XEvent* event)
{
    XPushButton* button = (XPushButton*)self;
    XFocusReason reason = XFocusReason_Other;

    if (!button || !event)
        return;
#if XWINDOWEVENT_ON
    if (XFocusEvent_lostFocus((const XFocusEvent*)event))
        reason = XFocusEvent_reason((const XFocusEvent*)event);
#endif /* XWINDOWEVENT_ON */
    if (reason != XFocusReason_Popup && XPushButton_autoDefault(button) &&
        button->m_defaultButton) {
        XWidget* dlg = pushbutton_dialogParent(button);

        if (dlg)
            pushbutton_clearSiblingDefaults((XObject*)dlg, NULL);
        else
            button->m_defaultButton = false;
        XWidget_update(self);
    }
    XClass_Parent(XAbstractButton, EXWidget_FocusOutEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/* ==================== 内容变更虚槽（刷新本类 sizeHint 存储位） ==================== */

/**
 * @brief      重载 XAbstractButton 的 contentChanged 保护槽。
 * @details    XWidget 的 sizeHint/minimumSizeHint 是存储位而非虚函数，
 *             基类 setText/setIcon/setIconSize 不会自动重算派生尺寸；
 *             本槽在基类内容变更后、刷新几何之前重算并写回存储位。
 */
static void VXPushButton_contentChanged(XAbstractButton* base)
{
    XPushButton* self = (XPushButton*)base;

    if (self)
        pushbutton_refreshSizeHint(self);
}

/**
 * @brief 命中保护槽（对标 QPushButton::hitButton 覆写 QAbstractButton）。
 * @details 此前 XPushButton 只有公共函数包装、没有虚表覆写 ⇒ 鼠标按压
 *          走基类的整矩形命中，样式的 bevel 区域根本没被查询。Qt 中
 *          QPushButton::hitButton 是覆写，QAbstractButton::mousePress/
 *          Release/Move 的 hitButton 虚调用落在 QPushButton 版本
 *          （qabstractbutton.cpp:989-1005 + qpushbutton.cpp:499-505）。
 */
static bool VXPushButton_hitButton(const XAbstractButton* base,
                                   const XPoint* pos)
{
    return XPushButton_hitButton((const XPushButton*)base, pos);
}

/* ==================== 类虚表 ==================== */

XVtable* XPushButton_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPushButton)
    XVTABLE_INHERIT_XCLASS(XAbstractButton);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXPushButton_sizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXPushButton_minimumSizeHint);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_ContentChanged,
                             VXPushButton_contentChanged);
    /* 对标 QPushButton::hitButton 覆写：命中区走样式 SE_PushButtonBevel
       （qpushbutton.cpp:499-505），事件路径必须落到本实现。 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_HitButton,
                             VXPushButton_hitButton);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXPushButton_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXPushButton_keyPressEvent);
    /* 对标 QPushButton::focusInEvent/focusOutEvent：焦点进出迁移对话框
       默认按钮位（qpushbutton.cpp:438-476）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent,
                             VXPushButton_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent,
                             VXPushButton_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPushButton_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPushButton_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPushButton_deinit);
    /* 登记派生虚表：XAbstractButton 的自动互斥/实例识别（isInstance）
       依赖该登记表，必须在类虚表构建完成后调用。 */
    XAbstractButton_registerClass(XVTABLE_DEFAULT);
    return XVTABLE_DEFAULT;
}

#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON */

#endif /* XGUI_ON */
