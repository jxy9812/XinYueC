/******************************************************************************
 * @file       XStyleHints.c
 * @brief      XStyleHints 平台风格提示类实现（对标 Qt 6.8 QStyleHints）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XStyleHints.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEventType.h"
#include <string.h>

#if XSTYLEHINTS_ON

/** @brief XStyleHints 私有参数块。 */
struct XStyleHintsPrivate
{
    int        m_cursorFlashTime;          /**< 光标闪烁周期（毫秒）。 */
    float      m_fontSmoothingGamma;       /**< 字体平滑 Gamma（只读常量）。 */
    float      m_keyboardAutoRepeatRateF;  /**< 键盘自动重复率（毫秒/字符）。 */
    int        m_keyboardInputInterval;    /**< 键盘输入间隔（毫秒）。 */
    int        m_mouseDoubleClickInterval; /**< 鼠标双击间隔（毫秒）。 */
    int        m_mouseDoubleClickDistance; /**< 鼠标双击距离（像素，只读）。 */
    int        m_mousePressAndHoldInterval;/**< 鼠标长按间隔（毫秒）。 */
    int        m_mouseQuickSelectionThreshold; /**< 文本快速选择阈值（像素）。 */
    int        m_passwordMaskDelay;        /**< 密码掩码延迟（毫秒，只读）。 */
    uint32_t   m_passwordMaskCharacter;    /**< 密码掩码字符（UTF-32，只读）。 */
    bool       m_setFocusOnTouchRelease;   /**< 触摸释放设置焦点（只读）。 */
    bool       m_showIsFullScreen;         /**< 显示即全屏（只读）。 */
    bool       m_showIsMaximized;          /**< 显示即最大化（只读）。 */
    bool       m_showShortcutsInContextMenus; /**< 上下文菜单显示快捷键。 */
    bool       m_singleClickActivation;    /**< 单击激活（只读）。 */
    bool       m_useHoverEffects;          /**< 悬停效果。 */
    bool       m_useRtlExtensions;         /**< RTL 布局扩展（只读）。 */
    int        m_startDragDistance;        /**< 拖拽启动距离（像素）。 */
    int        m_startDragTime;            /**< 拖拽启动耗时（毫秒）。 */
    int        m_startDragVelocity;        /**< 拖拽启动速度（只读）。 */
    int        m_wheelScrollLines;         /**< 滚轮滚动行数。 */
    XStyleHintsTabFocusBehavior   m_tabFocusBehavior;   /**< Tab 焦点行为。 */
    XStyleHintsContextMenuTrigger m_contextMenuTrigger; /**< 右键菜单触发时机。 */
    XStyleHintsColorScheme        m_colorScheme;        /**< 颜色方案。 */
};

static void VXStyleHints_deinit(XStyleHints* self)
{
    if (!self) return;
    if (self->m_data) {
        XFree_System(self->m_data);
        self->m_data = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XStyleHints_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XStyleHints)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXStyleHints_deinit);
    return XVTABLE_DEFAULT;
}

void XStyleHints_init(XStyleHints* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XStyleHints));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XStyleHints);
    self->m_data = (XStyleHintsPrivate*)XMalloc_System(sizeof(XStyleHintsPrivate));
    if (!self->m_data) return;
    memset(self->m_data, 0, sizeof(XStyleHintsPrivate));

    /* 默认值参考桌面 Qt 平台（见各 getter 注释）。 */
    self->m_data->m_cursorFlashTime          = 1000;
    self->m_data->m_fontSmoothingGamma       = 1.0f;
    self->m_data->m_keyboardAutoRepeatRateF  = 33.0f;
    self->m_data->m_keyboardInputInterval    = 400;
    self->m_data->m_mouseDoubleClickInterval = 400;
    self->m_data->m_mouseDoubleClickDistance = 5;
    self->m_data->m_mousePressAndHoldInterval = 500;
    self->m_data->m_mouseQuickSelectionThreshold = 0;
    self->m_data->m_passwordMaskDelay        = 0;
    self->m_data->m_passwordMaskCharacter    = 0x2022; /* U+2022 BULLET */
    self->m_data->m_setFocusOnTouchRelease   = true;
    self->m_data->m_showIsFullScreen         = false;
    self->m_data->m_showIsMaximized          = false;
    self->m_data->m_showShortcutsInContextMenus = false;
    self->m_data->m_singleClickActivation    = false;
    self->m_data->m_useHoverEffects          = true;
    self->m_data->m_useRtlExtensions         = false;
    self->m_data->m_startDragDistance        = 10;
    self->m_data->m_startDragTime            = 500;
    self->m_data->m_startDragVelocity        = 0;
    self->m_data->m_wheelScrollLines         = 3;
    self->m_data->m_tabFocusBehavior         = XStyleHintsTabFocusBehavior_TabFocusAllControls;
    self->m_data->m_contextMenuTrigger       = XStyleHintsContextMenuTrigger_Press;
    self->m_data->m_colorScheme              = XStyleHintsColorScheme_Unknown;
}

XStyleHints* XStyleHints_create_ex(XMemoryType memory)
{
    XStyleHints* self = (XStyleHints*)XMemory_malloc(sizeof(XStyleHints), memory);
    if (!self) return NULL;
    XStyleHints_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/** @brief 发射信号并管理参数列表生命周期（与 XScreen/XWindow 相同模式）。 */
static void styleHints_emit(XStyleHints* self, size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    else if (args) XVarList_delete(args);
}

/* ==================== 可写属性：setter + 信号 ==================== */

int XStyleHints_cursorFlashTime(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_cursorFlashTime : 0;
}

void XStyleHints_setCursorFlashTime(XStyleHints* self, int cursorFlashTime)
{
    if (!self || !self->m_data || self->m_data->m_cursorFlashTime == cursorFlashTime)
        return;
    self->m_data->m_cursorFlashTime = cursorFlashTime;
    XStyleHints_cursorFlashTimeChanged_signal(self, cursorFlashTime);
}

float XStyleHints_keyboardAutoRepeatRateF(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_keyboardAutoRepeatRateF : 0.0f;
}

int XStyleHints_keyboardAutoRepeatRate(const XStyleHints* self)
{
    return (int)XStyleHints_keyboardAutoRepeatRateF(self);
}

int XStyleHints_keyboardInputInterval(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_keyboardInputInterval : 0;
}

void XStyleHints_setKeyboardInputInterval(XStyleHints* self, int keyboardInputInterval)
{
    if (!self || !self->m_data || self->m_data->m_keyboardInputInterval == keyboardInputInterval)
        return;
    self->m_data->m_keyboardInputInterval = keyboardInputInterval;
    XStyleHints_keyboardInputIntervalChanged_signal(self, keyboardInputInterval);
}

int XStyleHints_mouseDoubleClickInterval(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_mouseDoubleClickInterval : 0;
}

void XStyleHints_setMouseDoubleClickInterval(XStyleHints* self, int mouseDoubleClickInterval)
{
    if (!self || !self->m_data || self->m_data->m_mouseDoubleClickInterval == mouseDoubleClickInterval)
        return;
    self->m_data->m_mouseDoubleClickInterval = mouseDoubleClickInterval;
    XStyleHints_mouseDoubleClickIntervalChanged_signal(self, mouseDoubleClickInterval);
}

int XStyleHints_mousePressAndHoldInterval(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_mousePressAndHoldInterval : 0;
}

void XStyleHints_setMousePressAndHoldInterval(XStyleHints* self, int mousePressAndHoldInterval)
{
    if (!self || !self->m_data || self->m_data->m_mousePressAndHoldInterval == mousePressAndHoldInterval)
        return;
    self->m_data->m_mousePressAndHoldInterval = mousePressAndHoldInterval;
    XStyleHints_mousePressAndHoldIntervalChanged_signal(self, mousePressAndHoldInterval);
}

int XStyleHints_startDragDistance(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_startDragDistance : 0;
}

void XStyleHints_setStartDragDistance(XStyleHints* self, int startDragDistance)
{
    if (!self || !self->m_data || self->m_data->m_startDragDistance == startDragDistance)
        return;
    self->m_data->m_startDragDistance = startDragDistance;
    XStyleHints_startDragDistanceChanged_signal(self, startDragDistance);
}

int XStyleHints_startDragTime(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_startDragTime : 0;
}

void XStyleHints_setStartDragTime(XStyleHints* self, int startDragTime)
{
    if (!self || !self->m_data || self->m_data->m_startDragTime == startDragTime)
        return;
    self->m_data->m_startDragTime = startDragTime;
    XStyleHints_startDragTimeChanged_signal(self, startDragTime);
}

XStyleHintsTabFocusBehavior XStyleHints_tabFocusBehavior(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_tabFocusBehavior
                                  : XStyleHintsTabFocusBehavior_TabFocusAllControls;
}

void XStyleHints_setTabFocusBehavior(XStyleHints* self, XStyleHintsTabFocusBehavior behavior)
{
    if (!self || !self->m_data || self->m_data->m_tabFocusBehavior == behavior)
        return;
    self->m_data->m_tabFocusBehavior = behavior;
    XStyleHints_tabFocusBehaviorChanged_signal(self, behavior);
}

bool XStyleHints_useHoverEffects(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_useHoverEffects : false;
}

void XStyleHints_setUseHoverEffects(XStyleHints* self, bool on)
{
    if (!self || !self->m_data || self->m_data->m_useHoverEffects == on)
        return;
    self->m_data->m_useHoverEffects = on;
    XStyleHints_useHoverEffectsChanged_signal(self, on);
}

bool XStyleHints_showShortcutsInContextMenus(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_showShortcutsInContextMenus : false;
}

void XStyleHints_setShowShortcutsInContextMenus(XStyleHints* self, bool show)
{
    if (!self || !self->m_data || self->m_data->m_showShortcutsInContextMenus == show)
        return;
    self->m_data->m_showShortcutsInContextMenus = show;
    XStyleHints_showShortcutsInContextMenusChanged_signal(self, show);
}

XStyleHintsContextMenuTrigger XStyleHints_contextMenuTrigger(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_contextMenuTrigger
                                  : XStyleHintsContextMenuTrigger_Press;
}

void XStyleHints_setContextMenuTrigger(XStyleHints* self, XStyleHintsContextMenuTrigger trigger)
{
    if (!self || !self->m_data || self->m_data->m_contextMenuTrigger == trigger)
        return;
    self->m_data->m_contextMenuTrigger = trigger;
    XStyleHints_contextMenuTriggerChanged_signal(self, trigger);
}

int XStyleHints_wheelScrollLines(const XStyleHints* self)
{
    /* Qt 6.8 qstylehints.cpp:618-624 规定只有正数覆盖值有效；零和负数
       表示尚未取得有效覆盖值，应重新查询平台主题。XStyleHints 是本项目
       的嵌入式值对象，没有桌面主题查询层，因此这里回落到与初始化相同
       的嵌入式默认值 3，而不是把无效覆盖值暴露给调用者。 */
    if (!self || !self->m_data)
        return 0;
    return self->m_data->m_wheelScrollLines > 0
               ? self->m_data->m_wheelScrollLines : 3;
}

void XStyleHints_setWheelScrollLines(XStyleHints* self, int scrollLines)
{
    if (!self || !self->m_data || self->m_data->m_wheelScrollLines == scrollLines)
        return;
    self->m_data->m_wheelScrollLines = scrollLines;
    XStyleHints_wheelScrollLinesChanged_signal(self, scrollLines);
}

int XStyleHints_mouseQuickSelectionThreshold(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_mouseQuickSelectionThreshold : 0;
}

void XStyleHints_setMouseQuickSelectionThreshold(XStyleHints* self, int threshold)
{
    if (!self || !self->m_data || self->m_data->m_mouseQuickSelectionThreshold == threshold)
        return;
    self->m_data->m_mouseQuickSelectionThreshold = threshold;
    XStyleHints_mouseQuickSelectionThresholdChanged_signal(self, threshold);
}

XStyleHintsColorScheme XStyleHints_colorScheme(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_colorScheme
                                  : XStyleHintsColorScheme_Unknown;
}

void XStyleHints_setColorScheme(XStyleHints* self, XStyleHintsColorScheme scheme)
{
    if (!self || !self->m_data || self->m_data->m_colorScheme == scheme)
        return;
    self->m_data->m_colorScheme = scheme;
    XStyleHints_colorSchemeChanged_signal(self, scheme);
}

void XStyleHints_unsetColorScheme(XStyleHints* self)
{
    XStyleHints_setColorScheme(self, XStyleHintsColorScheme_Unknown);
}

/* ==================== 只读常量 ==================== */

float XStyleHints_fontSmoothingGamma(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_fontSmoothingGamma : 0.0f;
}

uint32_t XStyleHints_passwordMaskCharacter(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_passwordMaskCharacter : 0;
}

int XStyleHints_passwordMaskDelay(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_passwordMaskDelay : 0;
}

bool XStyleHints_setFocusOnTouchRelease(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_setFocusOnTouchRelease : false;
}

bool XStyleHints_showIsFullScreen(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_showIsFullScreen : false;
}

bool XStyleHints_showIsMaximized(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_showIsMaximized : false;
}

int XStyleHints_startDragVelocity(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_startDragVelocity : 0;
}

bool XStyleHints_useRtlExtensions(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_useRtlExtensions : false;
}

bool XStyleHints_singleClickActivation(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_singleClickActivation : false;
}

int XStyleHints_mouseDoubleClickDistance(const XStyleHints* self)
{
    return (self && self->m_data) ? self->m_data->m_mouseDoubleClickDistance : 0;
}

int XStyleHints_touchDoubleTapDistance(const XStyleHints* self)
{
    return (self && self->m_data) ? 40 : 0;
}

/* ==================== 信号（13 个，对标 QStyleHints 全部信号） ==================== */

void* XStyleHints_cursorFlashTimeChanged_signal(XStyleHints* self, int cursorFlashTime)
{
    if (!self) return (void*)(size_t)XStyleHints_cursorFlashTimeChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_cursorFlashTimeChanged_signal,
                    XVarList_Create(XVar(int, cursorFlashTime)));
    return (void*)(size_t)XStyleHints_cursorFlashTimeChanged_signal;
}

void* XStyleHints_keyboardInputIntervalChanged_signal(XStyleHints* self, int keyboardInputInterval)
{
    if (!self) return (void*)(size_t)XStyleHints_keyboardInputIntervalChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_keyboardInputIntervalChanged_signal,
                    XVarList_Create(XVar(int, keyboardInputInterval)));
    return (void*)(size_t)XStyleHints_keyboardInputIntervalChanged_signal;
}

void* XStyleHints_mouseDoubleClickIntervalChanged_signal(XStyleHints* self, int mouseDoubleClickInterval)
{
    if (!self) return (void*)(size_t)XStyleHints_mouseDoubleClickIntervalChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_mouseDoubleClickIntervalChanged_signal,
                    XVarList_Create(XVar(int, mouseDoubleClickInterval)));
    return (void*)(size_t)XStyleHints_mouseDoubleClickIntervalChanged_signal;
}

void* XStyleHints_mousePressAndHoldIntervalChanged_signal(XStyleHints* self, int mousePressAndHoldInterval)
{
    if (!self) return (void*)(size_t)XStyleHints_mousePressAndHoldIntervalChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_mousePressAndHoldIntervalChanged_signal,
                    XVarList_Create(XVar(int, mousePressAndHoldInterval)));
    return (void*)(size_t)XStyleHints_mousePressAndHoldIntervalChanged_signal;
}

void* XStyleHints_startDragDistanceChanged_signal(XStyleHints* self, int startDragDistance)
{
    if (!self) return (void*)(size_t)XStyleHints_startDragDistanceChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_startDragDistanceChanged_signal,
                    XVarList_Create(XVar(int, startDragDistance)));
    return (void*)(size_t)XStyleHints_startDragDistanceChanged_signal;
}

void* XStyleHints_startDragTimeChanged_signal(XStyleHints* self, int startDragTime)
{
    if (!self) return (void*)(size_t)XStyleHints_startDragTimeChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_startDragTimeChanged_signal,
                    XVarList_Create(XVar(int, startDragTime)));
    return (void*)(size_t)XStyleHints_startDragTimeChanged_signal;
}

void* XStyleHints_tabFocusBehaviorChanged_signal(XStyleHints* self, XStyleHintsTabFocusBehavior tabFocusBehavior)
{
    if (!self) return (void*)(size_t)XStyleHints_tabFocusBehaviorChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_tabFocusBehaviorChanged_signal,
                    XVarList_Create(XVar(XStyleHintsTabFocusBehavior, tabFocusBehavior)));
    return (void*)(size_t)XStyleHints_tabFocusBehaviorChanged_signal;
}

void* XStyleHints_useHoverEffectsChanged_signal(XStyleHints* self, bool useHoverEffects)
{
    if (!self) return (void*)(size_t)XStyleHints_useHoverEffectsChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_useHoverEffectsChanged_signal,
                    XVarList_Create(XVar(bool, useHoverEffects)));
    return (void*)(size_t)XStyleHints_useHoverEffectsChanged_signal;
}

void* XStyleHints_showShortcutsInContextMenusChanged_signal(XStyleHints* self, bool showShortcutsInContextMenus)
{
    if (!self) return (void*)(size_t)XStyleHints_showShortcutsInContextMenusChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_showShortcutsInContextMenusChanged_signal,
                    XVarList_Create(XVar(bool, showShortcutsInContextMenus)));
    return (void*)(size_t)XStyleHints_showShortcutsInContextMenusChanged_signal;
}

void* XStyleHints_contextMenuTriggerChanged_signal(XStyleHints* self, XStyleHintsContextMenuTrigger contextMenuTrigger)
{
    if (!self) return (void*)(size_t)XStyleHints_contextMenuTriggerChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_contextMenuTriggerChanged_signal,
                    XVarList_Create(XVar(XStyleHintsContextMenuTrigger, contextMenuTrigger)));
    return (void*)(size_t)XStyleHints_contextMenuTriggerChanged_signal;
}

void* XStyleHints_wheelScrollLinesChanged_signal(XStyleHints* self, int scrollLines)
{
    if (!self) return (void*)(size_t)XStyleHints_wheelScrollLinesChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_wheelScrollLinesChanged_signal,
                    XVarList_Create(XVar(int, scrollLines)));
    return (void*)(size_t)XStyleHints_wheelScrollLinesChanged_signal;
}

void* XStyleHints_mouseQuickSelectionThresholdChanged_signal(XStyleHints* self, int threshold)
{
    if (!self) return (void*)(size_t)XStyleHints_mouseQuickSelectionThresholdChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_mouseQuickSelectionThresholdChanged_signal,
                    XVarList_Create(XVar(int, threshold)));
    return (void*)(size_t)XStyleHints_mouseQuickSelectionThresholdChanged_signal;
}

void* XStyleHints_colorSchemeChanged_signal(XStyleHints* self, XStyleHintsColorScheme scheme)
{
    if (!self) return (void*)(size_t)XStyleHints_colorSchemeChanged_signal;
    styleHints_emit(self, (size_t)XStyleHints_colorSchemeChanged_signal,
                    XVarList_Create(XVar(XStyleHintsColorScheme, scheme)));
    return (void*)(size_t)XStyleHints_colorSchemeChanged_signal;
}

#endif /* XSTYLEHINTS_ON */
