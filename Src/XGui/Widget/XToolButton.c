/**
 * @file       XToolButton.c
 * @brief      XToolButton 工具按钮实现（对标 Qt 6.8 QToolButton）。
 * @details    对齐 QToolButton 的核心语义：
 *             - 默认动作：setDefaultAction 把动作的文本/可选中/选中/启用
 *               镜像到按钮，按钮点击触发动作，动作 triggered 转发为按钮
 *               triggered(XAction*) 信号；
 *             - 外观：toolButtonStyle（四种排布 + FollowStyle）、
 *               autoRaise、arrowType；
 *             - 菜单：setMenu/popupMode/showMenu，弹出 XMenu 并在关闭时
 *               恢复按下状态；
 *             - 尺寸：sizeHint/minimumSizeHint 按样式与内容计算。
 *             点击路径对齐 Qt：click 的 nextCheckState 阶段触发默认动作
 *             （qtoolbutton.cpp:983-990），由动作承担状态翻转
 *             （action.toggled 镜像回按钮）；按钮自身不做二次翻转，
 *             最终选中状态与动作一致；checkStateSet 把按钮侧 setChecked
 *             回写动作（qtoolbutton.cpp:971-977）。
 */
#if XGUI_ON

#include "XToolButton.h"

#include "XAlgorithm.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XAbstractButton_Protected.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XString.h"
#include "XVariant.h"
#include "XIcon.h"
#include "XAlignment.h"
#include "XPainter.h"
#include "XFont.h"
#include "XVarList.h"


#if XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON

/* ==================== 内部工具 ==================== */

static void toolbutton_refresh(XToolButton* self);
static void toolbutton_mirrorFromAction(XToolButton* self);

/* DelayedPopup 按住延时弹层的定时器记账（对标 Qt SH_ToolButton_PopupDelay
 * 的按住弹出）。定时器 ID 存动态属性，避免改动公开契约头（同 XMenu.c
 * hover 定时器的既有模式）。 */
#define XTOOLBUTTON_PROP_POPUP_TIMER "xgui.toolbutton.popupTimer"
/* 按住弹出延时（对标 QCommonStyle::SH_ToolButton_PopupDelay 缺省 500ms）。 */
#define XTOOLBUTTON_POPUP_DELAY_MS 500u

static int64_t toolbutton_int64Prop(const XToolButton* self,
                                    const char* keyUtf8, int64_t fallback)
{
    XString key;
    XVariant* v;

    if (!self || !keyUtf8)
        return fallback;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    v = XObject_property_const((const XObject*)self, &key);
    XClassDeinit(&key);
    return v ? XVariant_toInt64(v) : fallback;
}

static void toolbutton_setInt64Prop(XToolButton* self, const char* keyUtf8,
                                    int64_t value)
{
    XString key;
    XVariant* v;

    if (!self || !keyUtf8)
        return;
    v = XVariant_create_int64(value);
    if (!v)
        return;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    /* 2026-11-11 口径：XObject_setProperty **总是**接管 value 所有权
     * （对齐 Qt 的按值 QVariant 形参），返回值改为 Qt 语义「是否命中静态
     * 属性」（动态属性写入恒 false）⇒ 调用方不得再按返回值释放 value
     * （旧写法 `if (!setProperty(...)) 释放` 会双释放）。 */
    (void)XObject_setProperty((XObject*)self, &key, v);
    XClassDeinit(&key);
}

static void toolbutton_removeProp(XToolButton* self, const char* keyUtf8)
{
    XString key;

    if (!self || !keyUtf8)
        return;
    XString_init(&key);
    XString_assign_utf8(&key, keyUtf8);
    /* Qt 6.8 删除动态属性的唯一入口是 setProperty(name, QVariant())；
     * XObject_removeProperty 已于 2026-11-11 删除（Qt 无此名）。 */
    (void)XObject_setProperty((XObject*)self, &key, NULL);
    XClassDeinit(&key);
}

/** @brief 取消未决的按住弹出定时器（无未决时为幂等）。 */
static void toolbutton_cancelPopupTimer(XToolButton* self)
{
    int64_t id = toolbutton_int64Prop(self, XTOOLBUTTON_PROP_POPUP_TIMER,
                                      (int64_t)XTIMER_INVALID_ID);

    if (id != (int64_t)XTIMER_INVALID_ID)
        XObject_killTimer((XObject*)self, (XTimerId)id);
    toolbutton_removeProp(self, XTOOLBUTTON_PROP_POPUP_TIMER);
}

/** @brief 启动 DelayedPopup 按住弹出定时器（对标 Qt popupTimer.start）。 */
static void toolbutton_startPopupTimer(XToolButton* self)
{
    XTimerId id;

    toolbutton_cancelPopupTimer(self);
    id = XObject_startTimer_ms((XObject*)self,
                               XTOOLBUTTON_POPUP_DELAY_MS,
                               XTimerType_PreciseTimer);
    if (id != XTIMER_INVALID_ID)
        toolbutton_setInt64Prop(self, XTOOLBUTTON_PROP_POPUP_TIMER,
                                (int64_t)id);
}

/* 有效样式：FollowStyle 按 TextBesideIcon 处理。 */
static XToolButtonStyle toolbutton_effectiveStyle(const XToolButton* self)
{
    if (!self)
        return XToolButtonStyle_IconOnly;
    if (self->m_toolButtonStyle == XToolButtonStyle_FollowStyle)
        return XToolButtonStyle_TextBesideIcon;
    return self->m_toolButtonStyle;
}

/* ==================== 动作/菜单联动槽 ==================== */

/* 动作 changed：刷新文本/可选中/选中/启用镜像。 */
static void toolbutton_actionChangedSlot(XObject* receiver, XVarList* args)
{
    XToolButton* self = (XToolButton*)receiver;

    (void)args;
    if (self)
        toolbutton_mirrorFromAction(self);
}

/* 动作 toggled(bool)：同步按钮选中状态。 */
static void toolbutton_actionToggledSlot(XObject* receiver, XVarList* args)
{
    XToolButton* self = (XToolButton*)receiver;

    if (!self)
        return;
    XVarList_args_1(args, bool, checked);
    XAbstractButton_setChecked((XAbstractButton*)self, checked);
}

/* 动作 enabledChanged(bool)：同步按钮启用状态。 */
static void toolbutton_actionEnabledSlot(XObject* receiver, XVarList* args)
{
    XToolButton* self = (XToolButton*)receiver;

    if (!self)
        return;
    XVarList_args_1(args, bool, enabled);
    XWidget_setEnabled((XWidget*)self, enabled);
}

/* 动作 destroyed：解除默认动作关联。 */
static void toolbutton_actionDestroyedSlot(XObject* receiver, XVarList* args)
{
    XToolButton* self = (XToolButton*)receiver;
    XObject* sender = XObject_sender(receiver);

    (void)args;
    if (self && sender && self->m_defaultAction == (XAction*)sender) {
        self->m_defaultAction = NULL;
        toolbutton_refresh(self);
    }
}

/* 动作 triggered：转发为按钮 triggered(action)。 */
static void toolbutton_actionTriggeredSlot(XObject* receiver, XVarList* args)
{
    XToolButton* self = (XToolButton*)receiver;

    (void)args;
    if (self && self->m_defaultAction)
        XToolButton_triggered_signal(self, self->m_defaultAction);
}

/* 按钮点击对默认动作的联动改由 nextCheckState 保护槽承担（对标
 * QToolButton::nextCheckState，qtoolbutton.cpp:983-990）：Qt 在 click()
 * 内部先 nextCheckState 再发 released/clicked，动作触发因此落在
 * clicked 之前；此前用 clicked→trigger 的桥会把触发挪到 clicked 之后，
 * 与 Qt 的顺序相反（本机 Qt 6.8 探针实测顺序：
 * tb.pressed → tb.toggled → action.toggled → action.triggered →
 * tb.released → tb.clicked）。 */

/* 菜单 aboutToHide：恢复按钮按下状态。 */
static void toolbutton_menuHideSlot(XObject* receiver, XVarList* args)
{
    XToolButton* self = (XToolButton*)receiver;

    (void)args;
    if (self)
        XAbstractButton_setDown((XAbstractButton*)self, false);
}

static void toolbutton_connectAction(XToolButton* self, XAction* action)
{
    XObject_connect_1((XObject*)action, XSignal(XAction_changed_signal),
                      (XObject*)self, toolbutton_actionChangedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)action, XSignal(XAction_toggled_signal),
                      (XObject*)self, toolbutton_actionToggledSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)action,
                      XSignal(XAction_enabledChanged_signal),
                      (XObject*)self, toolbutton_actionEnabledSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)action, XSignal(XObject_destroyed_signal),
                      (XObject*)self, toolbutton_actionDestroyedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)self, toolbutton_actionTriggeredSlot,
                      XConnectionType_Direct);
}

static void toolbutton_disconnectAction(XToolButton* self, XAction* action)
{
    if (!action)
        return;
    XObject_disconnect_1((XObject*)action, XSignal(XAction_changed_signal),
                         (XObject*)self, toolbutton_actionChangedSlot);
    XObject_disconnect_1((XObject*)action, XSignal(XAction_toggled_signal),
                         (XObject*)self, toolbutton_actionToggledSlot);
    XObject_disconnect_1((XObject*)action,
                         XSignal(XAction_enabledChanged_signal),
                         (XObject*)self, toolbutton_actionEnabledSlot);
    XObject_disconnect_1((XObject*)action, XSignal(XObject_destroyed_signal),
                         (XObject*)self, toolbutton_actionDestroyedSlot);
    XObject_disconnect_1((XObject*)action, XSignal(XAction_triggered_signal),
                         (XObject*)self, toolbutton_actionTriggeredSlot);
}

/* 把默认动作属性镜像到按钮。 */
static void toolbutton_mirrorFromAction(XToolButton* self)
{
    XAction* action;
    XString* text;

    if (!self)
        return;
    action = self->m_defaultAction;
    text = action ? XAction_text(action) : NULL;
    XAbstractButton_setText((XAbstractButton*)self, text);
    if (text)
        XClassDelete((XClass*)text);
    /* 对标 Qt 6.8 QToolButton::setDefaultAction：图标随动作镜像
       （QToolButtonPrivate 与动作 icon 联动）。XGui 动作图标以路径
       承载（XAction_icon），经 XIcon_init_file 落到按钮；动作无图
       标时置空按钮图标（动作镜像为覆盖语义）。 */
    if (action) {
        const XString* iconPath = XAction_icon_const(action);
        XIcon mirroredIcon;
        XIcon_init_file(&mirroredIcon,
                        iconPath); /* NULL 路径=空图标，置空按钮图标。 */
        XAbstractButton_setIcon((XAbstractButton*)self, &mirroredIcon);
        XClassDeinit((XClass*)&mirroredIcon);
    }
    XAbstractButton_setCheckable((XAbstractButton*)self,
                                 action ? XAction_isCheckable(action) : false);
    XAbstractButton_setChecked((XAbstractButton*)self,
                               action ? XAction_isChecked(action) : false);
    XWidget_setEnabled((XWidget*)self,
                       action ? XAction_isEnabled(action) : true);
    toolbutton_refresh(self);
}

static void toolbutton_refresh(XToolButton* self)
{
    if (!self)
        return;
    XAbstractButton_contentChanged_base((XAbstractButton*)self);
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/* ==================== 菜单存在性与弹出区（对标 QToolButtonPrivate::hasMenu） ==================== */

/**
 * @brief 实际弹出的菜单（Qt menuAction->menu() 优先，其次默认动作的菜单）。
 * @details 对标 QToolButtonPrivate::popupTimerDone（qtoolbutton.cpp:748-759）：
 *          setMenu 的 menuAction 优先；否则取 defaultAction->menu()。
 *          Qt 还会把「按钮自身动作列表」临时装进菜单；该分支见
 *          toolbutton_hasMenu 的 @note（本库暂不登记）。
 */
static XMenu* toolbutton_menuToShow(const XToolButton* self)
{
    if (!self)
        return NULL;
    if (self->m_menu)
        return self->m_menu;
    if (self->m_defaultAction)
        return XMenu_menuInAction(self->m_defaultAction);
    return NULL;
}

/**
 * @brief 是否有可弹出菜单（Qt QToolButtonPrivate::hasMenu）。
 * @details qtoolbutton.cpp:73-78：`(defaultAction && defaultAction->menu())
 *          || (menuAction && menuAction->menu()) || actions.size() > ...`。
 *          本库用 setMenu 与默认动作菜单覆盖前两项——修复前只认
 *          setMenu 的 m_menu，defaultAction->menu() 既不预留指示宽
 *          也不能弹出/走菜单分区。
 * @note      Qt 的第三项（`actions.size() > (defaultAction ? 1 : 0)`）
 *           需要「按钮自身动作列表 ⇒ 临时 QMenu」的弹出路径；本库
 *           XMenu_popup 是非阻塞的、无法在 popup 返回后确定临时菜单的
 *           销毁点（Qt 用阻塞 exec 的栈生命周期），故不登记该分支，
 *           避免出现“有箭头但点了不弹”的假功能。
 */
static bool toolbutton_hasMenu(const XToolButton* self)
{
    return toolbutton_menuToShow(self) != NULL;
}

/**
 * @brief 菜单指示区宽度（对标 PM_MenuButtonIndicator）。
 * @details Qt 用 style()->pixelMetric(PM_MenuButtonIndicator) 同时决定
 *          sizeHint 预留宽度与 SC_ToolButtonMenu 分区宽
 *          （qtoolbutton.cpp:334-336 + qcommonstyle.cpp:4246-4260）。
 *          本库 XCommonStyle 缺省 12（含 DPI 缩放，XCommonStyle.c:3249）；
 *          无样式可查时回落 12。
 */
static int toolbutton_menuIndicatorWidth(const XToolButton* self)
{
#if XSTYLE_ON
    XStyle* style;
    XStyleOption opt;
#endif /* XSTYLE_ON */
    int width = 12;

    if (!self)
        return width;
#if XSTYLE_ON
    style = XWidget_style((const XWidget*)self);
    if (!style)
        return width;
    XStyleOption_init(&opt, XStyleCT_ToolButton);
    opt.m_rect = XWidget_rect((const XWidget*)self);
    width = XStyle_pixelMetric(style, XStylePM_MenuButtonIndicator, &opt);
#endif /* XSTYLE_ON */
    return width > 0 ? width : 12;
}

/** @brief 菜单箭头区宽度（sizeHint 预留 / 分区命中 / 绘制同源）；
 *         按钮过窄时整钮都算菜单区。 */
static int toolbutton_menuZoneWidth(const XToolButton* self)
{
    XRect rect;
    int zone;

    if (!self)
        return 0;
    rect = XWidget_rect((const XWidget*)self);
    zone = toolbutton_menuIndicatorWidth(self);
    return rect.width < zone ? rect.width : zone;
}

/** @brief 局部坐标是否落在右侧菜单箭头区（MenuButtonPopup 命中判定，
 *         对标 QToolButton::MenuButtonPopup 的独立菜单按钮分区）。 */
static bool toolbutton_inMenuZone(const XToolButton* self,
                                  const XPoint* pos)
{
    XRect rect;
    int zone;

    if (!self || !pos)
        return false;
    rect = XWidget_rect((XWidget*)self);
    zone = toolbutton_menuZoneWidth(self);
    return pos->x >= rect.x + rect.width - zone &&
           pos->x < rect.x + rect.width && pos->y >= rect.y &&
           pos->y < rect.y + rect.height;
}

/** @brief 按 popupMode 判定左键按下是否立即弹出菜单（对标
 *         QToolButtonPrivate::onButtonPressed 的分流：InstantPopup 或
 *         延时为 0 → 立即弹出；DelayedPopup → 走按住定时器；
 *         MenuButtonPopup → 仅箭头区在 mousePressEvent 里弹出）。 */
static bool toolbutton_popupImmediately(XToolButton* self)
{
    if (!self || !toolbutton_hasMenu(self))
        return false;
    if (self->m_popupMode == XToolButtonPopupMode_InstantPopup)
        return true;
    return false;
}

/* ==================== 默认动作 ==================== */

XAction* XToolButton_defaultAction(const XToolButton* self)
{
    return self ? self->m_defaultAction : NULL;
}

void XToolButton_setDefaultAction(XToolButton* self, XAction* action)
{
    if (!self || self->m_defaultAction == action)
        return;
    toolbutton_disconnectAction(self, self->m_defaultAction);
    self->m_defaultAction = action;
    if (action)
        toolbutton_connectAction(self, action);
    toolbutton_mirrorFromAction(self);
}

/* ==================== 外观 ==================== */

XToolButtonStyle XToolButton_toolButtonStyle(const XToolButton* self)
{
    return self ? self->m_toolButtonStyle : XToolButtonStyle_IconOnly;
}

void XToolButton_setToolButtonStyle(XToolButton* self, XToolButtonStyle style)
{
    if (!self || self->m_toolButtonStyle == style)
        return;
    self->m_toolButtonStyle = style;
    toolbutton_refresh(self);
}

bool XToolButton_autoRaise(const XToolButton* self)
{
    return self ? self->m_autoRaise : false;
}

void XToolButton_setAutoRaise(XToolButton* self, bool enable)
{
    if (!self || self->m_autoRaise == enable)
        return;
    self->m_autoRaise = enable;
    XWidget_update((XWidget*)self);
}

XToolButtonArrowType XToolButton_arrowType(const XToolButton* self)
{
    return self ? self->m_arrowType : XToolButtonArrowType_NoArrow;
}

void XToolButton_setArrowType(XToolButton* self, XToolButtonArrowType type)
{
    if (!self || self->m_arrowType == type)
        return;
    self->m_arrowType = type;
    toolbutton_refresh(self);
}

/* ==================== 菜单 ==================== */

XMenu* XToolButton_menu(const XToolButton* self)
{
    return self ? self->m_menu : NULL;
}

void XToolButton_setMenu(XToolButton* self, XMenu* menu)
{
    if (!self || self->m_menu == menu)
        return;
    if (self->m_menu) {
        XObject_disconnect_1((XObject*)self->m_menu,
                             XSignal(XMenu_aboutToHide_signal),
                             (XObject*)self, toolbutton_menuHideSlot);
    }
    self->m_menu = menu;
    toolbutton_refresh(self);
}

XToolButtonPopupMode XToolButton_popupMode(const XToolButton* self)
{
    return self ? self->m_popupMode : XToolButtonPopupMode_DelayedPopup;
}

void XToolButton_setPopupMode(XToolButton* self, XToolButtonPopupMode mode)
{
    if (self)
        self->m_popupMode = mode;
}

void XToolButton_showMenu(XToolButton* self)
{
    XPoint global;
    XPoint pos;
    XMenu* menu;

    if (!self)
        return;
    /* 对标 QToolButton::showMenu（qtoolbutton.cpp:657-673）：!hasMenu()
     * 直接返回——defaultAction->menu() 同样算有菜单（见 hasMenu）。 */
    menu = toolbutton_menuToShow(self);
    if (!menu)
        return;
    if (!XWidget_isEnabled((XWidget*)self))
        return;
    /* 菜单即将展开，取消未决的按住弹出定时器，防止到点后重复弹层。 */
    toolbutton_cancelPopupTimer(self);
    XAbstractButton_setDown((XAbstractButton*)self, true);
    XObject_disconnect_1((XObject*)menu,
                         XSignal(XMenu_aboutToHide_signal),
                         (XObject*)self, toolbutton_menuHideSlot);
    XObject_connect_1((XObject*)menu,
                      XSignal(XMenu_aboutToHide_signal),
                      (XObject*)self, toolbutton_menuHideSlot,
                      XConnectionType_Direct);
    /* 菜单在按钮左下方弹出（对标 QToolButton::showMenu 的按钮下方对齐）；
     * 用按钮全局坐标 + 高度计算弹出位置，避免出现在屏幕左上角。
     * [弹窗随转·逻辑全局 2026-10-22] mapToGlobal 返回设备全局（旋转正变
     * 换后），XMenu_popup→setGeometryRect 需逻辑全局——90/270 下菜单错
     * 位（同 XComboBox xcombo_popupReposition 根修）。自持父链累加逻辑
     * 偏移+顶层逻辑原点，rot==0 与 mapToGlobal 逐位一致。 */
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
    XMenu_popup(menu, &pos);
}

/* ==================== 尺寸 ==================== */

XSize XToolButton_sizeHint(const XToolButton* self)
{
    const XAbstractButton* ab;
    XSize out;
    XFont font;
    XString* display = NULL;
    const char* text;
    int margin = 4;
    int gap = 4;
    int iconW;
    int iconH;
    int textW;
    int textH;

    XSize_init(&out, 0, 0);
    if (!self)
        return out;
    ab = (const XAbstractButton*)self;
    iconW = ab->m_iconSize.width;
    iconH = ab->m_iconSize.height;
    if (iconW <= 0 || iconH <= 0) {
        iconW = 16;
        iconH = 16;
    }
    font = XWidget_font((XWidget*)self);
    /* 度量显示文本（Qt sizeHint 用 fm.size(Qt::TextShowMnemonic, text)，
     * 助记符 '&' 不占宽）；文本没有内容时不取行高（Qt 只在
     * toolButtonStyle != IconOnly 分支量文本）。 */
    text = XAbstractButton_displayTextUtf8(ab, &display);
    textW = (text && text[0] != '\0') ? XPainter_textWidth(&font, text) : 0;
    /* 空文本仍按一行高度（Qt fm.size(TextShowMnemonic, "") 的高度 =
     * 字体行高；TextOnly 分支 h = textSize.height()）。 */
    textH = ab->m_text ? XPainter_textHeight(&font) : 0;
    XClassDeinit((XClass*)&font);
    if (display)
        XClassDelete((XClass*)display);

    switch (toolbutton_effectiveStyle(self)) {
    case XToolButtonStyle_IconOnly:
        out.width = iconW;
        out.height = iconH;
        break;
    case XToolButtonStyle_TextOnly:
        out.width = textW;
        out.height = textH;
        break;
    case XToolButtonStyle_TextUnderIcon:
        out.width = iconW > textW ? iconW : textW;
        out.height = iconH + gap + textH;
        break;
    default:
        out.width = iconW + gap + textW;
        out.height = iconH > textH ? iconH : textH;
        break;
    }
    out.width += margin * 2;
    out.height += margin * 2;
    /*
     * 对标 Qt 6.8 QToolButton::sizeHint（qtoolbutton.cpp:333-336）：
     *   opt.rect.setSize(QSize(w, h)); // PM_MenuButtonIndicator 依赖高度
     *   if (d->popupMode == MenuButtonPopup)
     *       w += style()->pixelMetric(PM_MenuButtonIndicator, &opt, this);
     * 只有 MenuButtonPopup + 有菜单才预留指示宽；DelayedPopup/
     * InstantPopup 不预留（本机 Qt 探针：无菜单 27、DelayedPopup 27、
     * MenuButtonPopup 39）。旧实现按「m_menu 或 arrowType」恒加 14px，
     * DelayedPopup 菜单按钮因此偏宽；arrowType 更不应加宽
     * （探针 setArrowType(Down) 仍 27）。
     */
    if (self->m_popupMode == XToolButtonPopupMode_MenuButtonPopup &&
        toolbutton_hasMenu(self)) {
        out.width += toolbutton_menuIndicatorWidth(self);
    }
    return out;
}

XSize XToolButton_minimumSizeHint(const XToolButton* self)
{
    return XToolButton_sizeHint(self);
}

/* ==================== 绘制 ==================== */

/* 画一个方向箭头（中心 cx,cy，半尺寸 size）。 */
static void toolbutton_drawArrow(XPainter* painter, int cx, int cy,
                                 int size, XToolButtonArrowType type,
                                 uint32_t color)
{
    int i;
    int x0;
    int y0;

    if (!painter)
        return;
    if (type == XToolButtonArrowType_NoArrow)
        return;
    for (i = 0; i < size; ++i) {
        XRect r;
        int half = i;

        if (type == XToolButtonArrowType_Right ||
            type == XToolButtonArrowType_Left) {
            x0 = cx + (type == XToolButtonArrowType_Right ? i : -i);
            XRect_init(&r, x0, cy - half, 1, half * 2 + 1);
        } else {
            y0 = cy + (type == XToolButtonArrowType_Down ? i : -i);
            XRect_init(&r, cx - half, y0, half * 2 + 1, 1);
        }
        XPainter_fillRect(painter, &r, color);
    }
}

static void VXToolButton_paintEvent(XWidget* self, XEvent* event)
{
#if !XWINDOWEVENT_ON
    (void)self;
    (void)event;
    return;
#else
    XToolButton* tb = (XToolButton*)self;
    XAbstractButton* ab = (XAbstractButton*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect rect;
    XFont font;
    XSize iconSize;
    const char* text;
    XString* display = NULL;
    bool hasIcon;
    bool hasText;
    int margin = 4;
    int gap = 4;
    int textW;
    int textH;
    int ascent;
    int iconX = 0;
    int iconY = 0;
    int textX = 0;
    int textY = 0;
    XToolButtonStyle style;
    bool enabled;

    if (!tb || !event || XEvent_type(event) != XEVENT_TYPE_PAINT)
        return;
    image = XWidget_paintImage(self);
    if (!image)
        return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);

    rect = XWidget_rect((XWidget*)tb);
    enabled = XWidget_isEnabled((XWidget*)tb);

    /* 背景与边框：非 autoRaise 或按下/悬停时绘制。 */
#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        /* Fusion/公共风格接管：面板走 PE_PanelButtonTool（AutoRaise
         * 时仅按下/悬停凸起，完整对标 QFusionStyle 工具按钮语义）。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        XStyleOption_init(&opt, XStylePE_PanelButtonTool);
        opt.m_rect = rect;
        opt.m_state = enabled ? XStyleState_Enabled : 0;
        if (ab->m_down)
            opt.m_state |= XStyleState_Sunken | XStyleState_On;
        else
            opt.m_state |= XStyleState_Raised;
        if (XWidget_underMouse((XWidget*)tb) && enabled)
            opt.m_state |= XStyleState_MouseOver;
        if (XWidget_hasFocus((XWidget*)tb))
            opt.m_state |= XStyleState_HasFocus;
        if (tb->m_autoRaise && !ab->m_down &&
            !(opt.m_state & XStyleState_MouseOver))
            opt.m_state |= XStyleState_AutoRaise;
        opt.m_flat = tb->m_autoRaise;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((XWidget*)tb);
#endif
        if (!tb->m_autoRaise || ab->m_down ||
            (opt.m_state & XStyleState_MouseOver))
            XStyle_drawPrimitive(style, XStylePE_PanelButtonTool, &opt,
                                 &painter, (XWidget*)tb);
        goto xtb_style_label;
    }
#endif /* XSTYLE_ON */
    if (!tb->m_autoRaise || ab->m_down) {
        XRect edge;
        XPainter_fillRect(&painter, &rect,
                          enabled ? 0xFFE0E0E0u : 0xFFF0F0F0u);
        XRect_init(&edge, rect.x, rect.y, rect.width, 1);
        XPainter_fillRect(&painter, &edge, 0xFFA0A0A0u);
        XRect_init(&edge, rect.x, rect.y + rect.height - 1, rect.width, 1);
        XPainter_fillRect(&painter, &edge, 0xFFA0A0A0u);
        XRect_init(&edge, rect.x, rect.y, 1, rect.height);
        XPainter_fillRect(&painter, &edge, 0xFFA0A0A0u);
        XRect_init(&edge, rect.x + rect.width - 1, rect.y, 1, rect.height);
        XPainter_fillRect(&painter, &edge, 0xFFA0A0A0u);
    }
    goto xtb_style_label;
#if XSTYLE_ON
xtb_style_label:
#endif
    hasIcon = !XIcon_isNull(&ab->m_icon);
    /* 显示文本去助记符 '&'（Qt TextShowMnemonic）。 */
    text = XAbstractButton_displayTextUtf8(ab, &display);
    if (!text)
        text = "";
    hasText = text[0] != '\0';

    font = XWidget_font((XWidget*)tb);
    XPainter_setFont(&painter, &font);
    textW = hasText ? XPainter_textWidth(&font, text) : 0;
    textH = hasText ? XPainter_textHeight(&font) : 0;
    ascent = XPainter_textAscent(&font);
    iconSize = ab->m_iconSize;
    if (iconSize.width <= 0 || iconSize.height <= 0) {
        iconSize.width = 16;
        iconSize.height = 16;
    }

    style = toolbutton_effectiveStyle(tb);
    if (style == XToolButtonStyle_IconOnly) {
        iconX = rect.x + (rect.width - iconSize.width) / 2;
        iconY = rect.y + (rect.height - iconSize.height) / 2;
    } else if (style == XToolButtonStyle_TextOnly) {
        textX = rect.x + (rect.width - textW) / 2;
        textY = rect.y + (rect.height - textH) / 2 + ascent;
    } else if (style == XToolButtonStyle_TextUnderIcon) {
        iconX = rect.x + (rect.width - iconSize.width) / 2;
        iconY = rect.y + margin;
        textX = rect.x + (rect.width - textW) / 2;
        textY = rect.y + margin + iconSize.height + gap + ascent;
    } else {
        if (hasIcon && hasText) {
            iconX = rect.x + margin;
            iconY = rect.y + (rect.height - iconSize.height) / 2;
            textX = rect.x + margin + iconSize.width + gap;
            textY = rect.y + (rect.height - textH) / 2 + ascent;
        } else if (hasIcon) {
            iconX = rect.x + (rect.width - iconSize.width) / 2;
            iconY = rect.y + (rect.height - iconSize.height) / 2;
        } else {
            textX = rect.x + (rect.width - textW) / 2;
            textY = rect.y + (rect.height - textH) / 2 + ascent;
        }
    }

    if (hasIcon) {
        XIcon_paint(&ab->m_icon, &painter, iconX, iconY, iconSize.width,
                    iconSize.height, XAlignment_Center,
                    enabled ? XIconMode_Normal : XIconMode_Disabled,
                    ab->m_checked ? XIconState_On : XIconState_Off);
    }
    if (hasText) {
        XPainter_drawText(&painter, textX, textY, text,
                          enabled ? 0xFF000000u : 0xFF808080u);
    }

    /* 箭头：菜单优先，其次 arrowType。MenuButtonPopup 画分区竖线 +
     * 箭头居中（复扫 R-20：让箭头区命中判定可见可发现）；其余模式
     * 绘制在右下角。 */
    {
        XToolButtonArrowType arrow = tb->m_arrowType;
        bool menu = toolbutton_hasMenu(tb);

        /* 对标 Qt QToolButtonPrivate::hasMenu：默认动作自带的菜单也算
         * “有菜单”（qtoolbutton.cpp:73-78）。 */
        if (menu && arrow == XToolButtonArrowType_NoArrow)
            arrow = XToolButtonArrowType_Down;
        if (menu &&
            tb->m_popupMode == XToolButtonPopupMode_MenuButtonPopup &&
            arrow != XToolButtonArrowType_NoArrow) {
            int zone = toolbutton_menuZoneWidth(tb);
            int zx = rect.x + rect.width - zone;
            XRect sepLine;

            XRect_init(&sepLine, zx, rect.y + 2, 1,
                       rect.height > 4 ? rect.height - 4 : rect.height);
            XPainter_fillRect(&painter, &sepLine, 0xFFA0A0A0u);
            toolbutton_drawArrow(&painter, zx + zone / 2,
                                 rect.y + rect.height / 2, 3, arrow,
                                 0xFF606060u);
        } else if (arrow != XToolButtonArrowType_NoArrow) {
            int ax = rect.x + rect.width - 8;
            int ay = rect.y + rect.height - 8;

            toolbutton_drawArrow(&painter, ax, ay, 3, arrow, 0xFF606060u);
        }
    }

    XClassDeinit((XClass*)&font);
    if (display)
        XClassDelete((XClass*)display);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XWINDOWEVENT_ON */
}

/* ==================== 虚槽实现（尺寸与生命周期） ==================== */

/* 左键按下：按 popupMode 与箭头区分流弹层与触发（对标
 * QToolButton::mousePressEvent + QToolButtonPrivate::onButtonPressed）：
 * - InstantPopup：立即弹层且不链基类（Qt 同款吞掉按压，菜单弹出路径
 *   不触发默认动作）；
 * - MenuButtonPopup：仅箭头区弹层，其余走普通按钮链；
 * - DelayedPopup：链基类进入正常按压（保留点击触发动作的回退路径），
 *   同时启动按住延时定时器，按住到点弹层（Qt popupTimer 语义）。
 * 修复 night #1：原实现 DelayedPopup 一律 accept+return 吞掉整条点击
 * 链且无回退，菜单未可见时按钮彻底无响应。 */
static void VXToolButton_mousePressEvent(XWidget* self, XEvent* event)
{
    XToolButton* tb = (XToolButton*)self;

    if (tb && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        XMouseEvent_button((XMouseEvent*)event) ==
            XMouseButton_LeftButton &&
        XWidget_isEnabled(self)) {
        XPoint pos = XMouseEvent_position((XMouseEvent*)event);

        if (toolbutton_popupImmediately(tb) ||
            (tb->m_popupMode ==
                 XToolButtonPopupMode_MenuButtonPopup &&
             toolbutton_hasMenu(tb) &&
             toolbutton_inMenuZone(tb, &pos))) {
            XEvent_accept(event);
            XToolButton_showMenu(tb);
            return;
        }
    }
    XClass_Parent(XAbstractButton, EXWidget_MousePressEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    /* 对标 Qt onButtonPressed：DelayedPopup 在基类按压（pressed）之后
     * 启动按住弹出定时器；快速释放则由 mouseReleaseEvent 取消，按压
     * 保持普通点击语义。hasMenu 含默认动作菜单（qtoolbutton.cpp:675-686）。 */
    if (tb && toolbutton_hasMenu(tb) &&
        tb->m_popupMode == XToolButtonPopupMode_DelayedPopup &&
        XWidget_isEnabled(self))
        toolbutton_startPopupTimer(tb);
}

/* 左键释放：两种分流（对标 Qt）：
 * - 弹出菜单仍打开：吞掉释放——按下开菜单后、菜单关闭前到来的
 *   release 不得转成 clicked 误触发动作（对标 Qt 由弹出窗口抓取鼠标
 *   承接释放；无抓取环境由此分支兜底）；
 * - 按住弹出定时器未到期就释放：取消定时器并链基类，本次按压回归
 *   普通点击（对标 QToolButtonPrivate::onButtonReleased 停表后由
 *   QAbstractButton 正常发 clicked）——即 night #1 要求的回退路径。 */
static void VXToolButton_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XToolButton* tb = (XToolButton*)self;
    XMenu* menu = toolbutton_menuToShow(tb);

    if (tb && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_RELEASE &&
        menu && menu->m_popupActive) {
        XEvent_accept(event);
        return;
    }
    if (tb && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        int64_t id = toolbutton_int64Prop(
            tb, XTOOLBUTTON_PROP_POPUP_TIMER, (int64_t)XTIMER_INVALID_ID);

        if (id != (int64_t)XTIMER_INVALID_ID) {
            toolbutton_cancelPopupTimer(tb);
        }
    }
    XClass_Parent(XAbstractButton, EXWidget_MouseReleaseEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/* 定时器事件：DelayedPopup 按住延时到点弹层（对标 QToolButton::
 * timerEvent → QToolButtonPrivate::popupTimerDone）。 */
static void VXToolButton_timerEvent(XObject* object, XTimerEvent* event)
{
    XToolButton* tb = (XToolButton*)object;

    if (tb && event &&
        XTimerEvent_timerId(event) ==
            (XTimerId)toolbutton_int64Prop(
                tb, XTOOLBUTTON_PROP_POPUP_TIMER,
                (int64_t)XTIMER_INVALID_ID)) {
        toolbutton_removeProp(tb, XTOOLBUTTON_PROP_POPUP_TIMER);
        XToolButton_showMenu(tb);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(object, event);
}

/* ==================== 选中状态与默认动作（对标 QToolButton 保护槽） ==================== */

/**
 * @brief      重载 nextCheckState：有默认动作时把翻转交给动作。
 * @details    对标 Qt 6.8 QToolButton::nextCheckState（qtoolbutton.cpp:
 *             983-990）：设置默认动作后按钮自身不再翻转 checked，改由
 *             action->trigger() 承担（动作的可选中/选中态经镜像回按钮）；
 *             无默认动作时回落基类的二态翻转。该槽在 click() 的
 *             nextCheckState 阶段被调用（qabstractbutton.cpp:359），故
 *             triggered(action) 落在 released/clicked 之前——与 Qt 一致。
 */
static void VXToolButton_nextCheckState(XAbstractButton* base)
{
    XToolButton* self = (XToolButton*)base;

    if (self && self->m_defaultAction) {
        XAction_trigger(self->m_defaultAction);
        return;
    }
    XClass_Parent(XAbstractButton, EXAbstractButton_NextCheckState,
                  void(*)(XAbstractButton*))((XAbstractButton*)self);
}

/**
 * @brief      重载 checkStateSet：把按钮选中态回写到默认动作。
 * @details    对标 Qt 6.8 QToolButton::checkStateSet（qtoolbutton.cpp:
 *             971-977）：默认动作可选中时 action->setChecked(isChecked())，
 *             使 setChecked() 程序化设置也同步动作。动作侧 setChecked
 *             对同值早退，镜像回写不会递归。
 */
static void VXToolButton_checkStateSet(XAbstractButton* base)
{
    XToolButton* self = (XToolButton*)base;

    if (!self || !self->m_defaultAction)
        return;
    if (!XAction_isCheckable(self->m_defaultAction))
        return;
    XAction_setChecked(self->m_defaultAction,
                       XAbstractButton_isChecked(base));
}

static void VXToolButton_contentChanged(XAbstractButton* base)
{
    XToolButton* self = (XToolButton*)base;
    XSize hint;

    if (!self)
        return;
    hint = XToolButton_sizeHint(self);
    XWidget_setSizeHint((XWidget*)self, &hint);
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
}

static void VXToolButton_copy(XToolButton* self, const XToolButton* other)
{
    if (!self || !other || self == other)
        return;
    if (XClassIsVtableNull(self))
        XToolButton_init(self, NULL, 0);

    XClass_Parent(XAbstractButton, EXClass_Copy,
                  void(*)(XAbstractButton*, const XAbstractButton*))(
        (XAbstractButton*)self, (const XAbstractButton*)other);
    toolbutton_disconnectAction(self, self->m_defaultAction);
    self->m_defaultAction = other->m_defaultAction;
    self->m_menu = other->m_menu;
    self->m_toolButtonStyle = other->m_toolButtonStyle;
    self->m_arrowType = other->m_arrowType;
    self->m_popupMode = other->m_popupMode;
    self->m_autoRaise = other->m_autoRaise;
    if (self->m_defaultAction)
        toolbutton_connectAction(self, self->m_defaultAction);
    toolbutton_refresh(self);
}

static void VXToolButton_move(XToolButton* self, XToolButton* other)
{
    if (!self || !other || self == other)
        return;
    if (XClassIsVtableNull(self))
        XToolButton_init(self, NULL, 0);

    XClass_Parent(XAbstractButton, EXClass_Move,
                  void(*)(XAbstractButton*, XAbstractButton*))(
        (XAbstractButton*)self, (XAbstractButton*)other);
    toolbutton_disconnectAction(self, self->m_defaultAction);
    self->m_defaultAction = other->m_defaultAction;
    other->m_defaultAction = NULL;
    self->m_menu = other->m_menu;
    other->m_menu = NULL;
    self->m_toolButtonStyle = other->m_toolButtonStyle;
    other->m_toolButtonStyle = XToolButtonStyle_IconOnly;
    self->m_arrowType = other->m_arrowType;
    other->m_arrowType = XToolButtonArrowType_NoArrow;
    self->m_popupMode = other->m_popupMode;
    other->m_popupMode = XToolButtonPopupMode_DelayedPopup;
    self->m_autoRaise = other->m_autoRaise;
    other->m_autoRaise = false;
    if (self->m_defaultAction)
        toolbutton_connectAction(self, self->m_defaultAction);
    toolbutton_refresh(self);
}

static void VXToolButton_deinit(XToolButton* self)
{
    if (!self)
        return;
    toolbutton_disconnectAction(self, self->m_defaultAction);
    self->m_defaultAction = NULL;
    if (self->m_menu) {
        XObject_disconnect_1((XObject*)self->m_menu,
                             XSignal(XMenu_aboutToHide_signal),
                             (XObject*)self, toolbutton_menuHideSlot);
        self->m_menu = NULL;
    }
    XClass_Deinit_Parent(XAbstractButton, (XAbstractButton*)self);
}

/* ==================== 类初始化与生命周期 ==================== */

/** @brief SizeHint 虚槽（对标 Qt：QLabel/QPushButton 等的 sizeHint 是虚函数）：
 *         转发到本类的实时计算实现，供 XWidget_sizeHint 虚分派调用。 */
static XSize VXToolButton_sizeHintSlot(const XWidget* self)
{
    return XToolButton_sizeHint((const XToolButton*)self);
}

static XSize VXToolButton_minimumSizeHintSlot(const XWidget* self)
{
    return XToolButton_minimumSizeHint((const XToolButton*)self);
}

XVtable* XToolButton_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XToolButton)
    XVTABLE_INHERIT_XCLASS(XAbstractButton);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXToolButton_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXToolButton_minimumSizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_ContentChanged,
                             VXToolButton_contentChanged);
    /* 对标 QToolButton 的两个保护槽：有默认动作时翻转/回写交给动作
       （qtoolbutton.cpp:971-990）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_NextCheckState,
                             VXToolButton_nextCheckState);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractButton_CheckStateSet,
                             VXToolButton_checkStateSet);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXToolButton_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXToolButton_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXToolButton_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXToolButton_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXToolButton_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXToolButton_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXToolButton_deinit);
    /* 登记派生虚表：XAbstractButton 自动互斥/实例识别依赖该登记表。 */
    XAbstractButton_registerClass(XVTABLE_DEFAULT);
    return XVTABLE_DEFAULT;
}

void XToolButton_init(XToolButton* self, XWidget* parent,
                      XWidgetFlags flags)
{
    if (!self)
        return;
    XMemset(self, 0, sizeof(XToolButton));
    XAbstractButton_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XToolButton);
    self->m_toolButtonStyle = XToolButtonStyle_IconOnly;
    self->m_arrowType = XToolButtonArrowType_NoArrow;
    self->m_popupMode = XToolButtonPopupMode_DelayedPopup;
    /* 默认动作的触发/回写走 nextCheckState/checkStateSet 保护槽
       （对标 QToolButton），不再用 clicked 信号桥。 */
    toolbutton_refresh(self);
}

XToolButton* XToolButton_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags)
{
    XToolButton* self =
        (XToolButton*)XMemory_malloc(sizeof(XToolButton), memory);

    if (!self)
        return NULL;
    XMemset(self, 0, sizeof(XToolButton));
    XToolButton_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公开信号 ==================== */

void* XToolButton_triggered_signal(XToolButton* self, XAction* action)
{
    XVarList* args;

    if (!self)
        return (void*)(size_t)XToolButton_triggered_signal;
    args = XVarList_Create(XVar(XAction*, action));
    if (!args)
        return (void*)(size_t)XToolButton_triggered_signal;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XToolButton_triggered_signal, args, NULL,
                           NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
    return (void*)(size_t)XToolButton_triggered_signal;
}







#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XTOOLBUTTON_ON */

#endif /* XGUI_ON */
