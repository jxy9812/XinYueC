/**
 * @file       XVirtualKeyboardPlatformInputContext.c
 * @brief      XVirtualKeyboardPlatformInputContext 实现（面板驱动面）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"

#if XVIRTUALKEYBOARD_ON

#include "XVirtualKeyboardPlatformInputContext.h"
#include "XVirtualKeyboardInputContext.h"
#include "XVirtualKeyboardInputContext_Protected.h"
#include "XVirtualKeyboardSettings.h"
#include "XVirtualKeyboard.h"
#include "XWidget_Protected.h"
#include "XMemory.h"

/* ============ 虚槽签名（本类四槽；与公共头 typedef 一致）。 ============ */

/* 前向声明（class_init 注册与入口回落用）。 */
static void XVkPic_deinit(XVirtualKeyboardPlatformInputContext* self);
static void XVkPic_showInputPanel(XVirtualKeyboardPlatformInputContext* self);
static void XVkPic_hideInputPanel(XVirtualKeyboardPlatformInputContext* self);
static void XVkPic_update(XVirtualKeyboardPlatformInputContext* self,
                          uint32_t queries);
static void XVkPic_setFocusObject(XVirtualKeyboardPlatformInputContext* self,
                                  XObject* focusObject);
void XVirtualKeyboardPlatformInputContext_init(
        XVirtualKeyboardPlatformInputContext* self);

/** @brief 判定焦点编辑框可接受（控件 && WA14 && 总开关）。 */
static bool xvPic_focusAccepted(XObject* focus)
{
    if (!focus || !XVirtualKeyboardSettings_keyboardEnabled(
        XVirtualKeyboardSettings_instance())) return false;
    if (!XObject_isWidgetType(focus)) return false;
    if (!XWidget_testAttribute((XWidget*)focus,
                               XWidgetAttribute_InputMethodEnabled))
        return false;
    /* 类型面（三编辑控件 vtable 识别/全 0 降级）由面板 setTextArea
       校验兜底——此处仅做 WA14+开关预筛。 */
    return true;
}

XVtable* XVirtualKeyboardPlatformInputContext_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboardPlatformInputContext)
    XVTABLE_INHERIT_XCLASS(XPlatformInputContext);
    /* 本类无新增槽位（容量=基座槽位全长）：四槽实现是对基座已声明槽
       位（面板驱动面，Qt 同名虚函数）的重载，必须逐槽 OVERLOAD 定位；
       ADD_FUNC_LIST 是追加语义，追加只会越界（静态表容量检查 exit）
       或落在无效偏移上（与 InputMethod 两插件同类缺陷）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXPlatformInputContext_ShowInputPanel,
                             XVkPic_showInputPanel);
    XVTABLE_OVERLOAD_DEFAULT(EXPlatformInputContext_HideInputPanel,
                             XVkPic_hideInputPanel);
    XVTABLE_OVERLOAD_DEFAULT(EXPlatformInputContext_UpdateInputPanel,
                             XVkPic_update);
    XVTABLE_OVERLOAD_DEFAULT(EXPlatformInputContext_SetFocusObject,
                             XVkPic_setFocusObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, XVkPic_deinit);
    return XVTABLE_DEFAULT;
}

/** @brief 反初始化：清面板引用后调父类。 */
static void XVkPic_deinit(XVirtualKeyboardPlatformInputContext* self)
{
    if (!self) return;
    self->m_panel = NULL;
    XClass_Deinit_Parent(XPlatformInputContext,
                         (XPlatformInputContext*)self);
}

void XVirtualKeyboardPlatformInputContext_init(
        XVirtualKeyboardPlatformInputContext* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XPlatformInputContext_init((XPlatformInputContext*)self);
    XClassSetVtable(self, XVirtualKeyboardPlatformInputContext);
    self->m_panel = NULL;
}

XVirtualKeyboardPlatformInputContext*
XVirtualKeyboardPlatformInputContext_create_ex(XMemoryType memory)
{
    XVirtualKeyboardPlatformInputContext* self =
        (XVirtualKeyboardPlatformInputContext*)XMemory_malloc(sizeof(*self),
                                                              memory);
    if (!self) return NULL;
    XVirtualKeyboardPlatformInputContext_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XVirtualKeyboardPlatformInputContext_setInputPanel(
        XVirtualKeyboardPlatformInputContext* self, XWidget* panel)
{
    if (!self) return;
    self->m_panel = panel;
}

/* ==================== 虚槽实现 ==================== */

static void XVkPic_showInputPanel(XVirtualKeyboardPlatformInputContext* self)
{
    XObject* focus;
    if (!self) return;
    if (!self->m_panel) {
        /* 无面板装配（默认接管即此形态，默认面板不绑 VK 上下文——见
           XGuiApplication inputMethod 装配注释）：显隐回落基座记账。
           否则 isInputPanelVisible 无面板时回落基座旗标（见本类同名
           查询），写路径却在此吞没——show 永远翻不了旗标，「两种装配
           下契约一致」（gui_app 契约测试实测 FAIL）。走 *_base 直通，
           勿调公共入口防虚表重入。 */
        XPlatformInputContext_showInputPanel_base(
            (XPlatformInputContext*)self);
        return;
    }
    if (!XVirtualKeyboardSettings_keyboardEnabled(
        XVirtualKeyboardSettings_instance())) return;
    focus = XPlatformInputContext_focusObject((XPlatformInputContext*)self);
    if (!focus || !xvPic_focusAccepted(focus)) return;
    /* show→面板 popup 焦点编辑框（Qt updateInputPanelVisible show 向）。 */
    XVirtualKeyboard_popup((XVirtualKeyboard*)self->m_panel, (XWidget*)focus);
}

static void XVkPic_hideInputPanel(XVirtualKeyboardPlatformInputContext* self)
{
    if (!self) return;
    if (!self->m_panel) {
        /* 无面板装配：与 show 对称回落基座记账（见上）。 */
        XPlatformInputContext_hideInputPanel_base(
            (XPlatformInputContext*)self);
        return;
    }
    XVirtualKeyboard_closePopup((XVirtualKeyboard*)self->m_panel);
}

static void XVkPic_update(XVirtualKeyboardPlatformInputContext* self,
                          uint32_t queries)
{
    XObject* focus;
    XVirtualKeyboardInputContext* ctx;
    if (!self) return;
    /* 刷新上下文缓存（Qt PlatformInputContext::update :70-92 口径）。 */
    ctx = XVirtualKeyboardInputContext_instance();
    if (ctx) XVirtualKeyboardInputContext_update(ctx, queries);
    /* 可见性同步：accepted→可见、否则收层（evaluateInputPanelVisible
       = m_visible && (focusObject && inputMethodAccepted()) 等价）。 */
    if (!self->m_panel) return;
    focus = XPlatformInputContext_focusObject((XPlatformInputContext*)self);
    if (!focus || !xvPic_focusAccepted(focus))
        XVirtualKeyboard_closePopup((XVirtualKeyboard*)self->m_panel);
}

static void XVkPic_setFocusObject(XVirtualKeyboardPlatformInputContext* self,
                                  XObject* focusObject)
{
    XVirtualKeyboardInputContext* ctx;
    if (!self) return;
    /* 基座行为先走（记录焦点对象）：调 *_base 非分发入口——公共入口
       现经虚表分派，重入本覆盖会无限递归。 */
    XPlatformInputContext_setFocusObject_base((XPlatformInputContext*)self,
                                              focusObject);
    ctx = XVirtualKeyboardInputContext_instance();
    if (ctx) XVirtualKeyboardInputContext_setFocusObject(ctx, focusObject);
    /* 焦点变化后按 accepted 同步面板（show/hide 双向）。 */
    XVkPic_update(self, 0);
}

/* ==================== 覆写入口（虚表分派） ==================== */

void XVirtualKeyboardPlatformInputContext_showInputPanel(
        XVirtualKeyboardPlatformInputContext* self)
{
    if (!self || !XClassGetVtable(self)) {
        if (self)
            XPlatformInputContext_showInputPanel(
                (XPlatformInputContext*)self);
        return;
    }
    XClassGetVirtualFunc(self, EXPlatformInputContext_ShowInputPanel,
                         XVkPicShowSlot)(self);
}

void XVirtualKeyboardPlatformInputContext_hideInputPanel(
        XVirtualKeyboardPlatformInputContext* self)
{
    if (!self || !XClassGetVtable(self)) {
        if (self)
            XPlatformInputContext_hideInputPanel(
                (XPlatformInputContext*)self);
        return;
    }
    XClassGetVirtualFunc(self, EXPlatformInputContext_HideInputPanel,
                         XVkPicShowSlot)(self);
}

bool XVirtualKeyboardPlatformInputContext_isInputPanelVisible(
        const XVirtualKeyboardPlatformInputContext* self)
{
    if (!self) return false;
    if (self->m_panel) return XVirtualKeyboard_popupVisible((XVirtualKeyboard*)self->m_panel);
    return XPlatformInputContext_isInputPanelVisible(
        (XPlatformInputContext*)self);
}

void XVirtualKeyboardPlatformInputContext_update(
        XVirtualKeyboardPlatformInputContext* self, uint32_t queries)
{
    if (!self || !XClassGetVtable(self)) {
        if (self)
            XPlatformInputContext_emitInputPanelVisibleChanged(
                (XPlatformInputContext*)self);
        return;
    }
    XClassGetVirtualFunc(self, EXPlatformInputContext_UpdateInputPanel,
                         XVkPicUpdateSlot)(self, queries);
}

#endif /* XVIRTUALKEYBOARD_ON */
