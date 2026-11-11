/**
 * @file       XAbstractSpinBox.c
 * @brief      XAbstractSpinBox 微调框抽象基类实现（对齐 Qt 6.8
 *             QAbstractSpinBox）。
 * @details    基类职责：
 *             - 内嵌 XLineEdit 编辑区（默认占满整个控件；子类可在
 *               重绘/布局时调整 geometry，或经 setLineEdit 整体替换）；
 *             - 按钮符号/键盘跟踪/边框/修正模式/循环/加速/千分位/
 *               特殊值文本等属性存取（数值与 Qt 对齐）；
 *             - 虚槽：StepBy（基类空操作）、Validate（默认 Acceptable）、
 *               Fixup（默认空操作）、Clear（清成前缀+后缀并置 cleared）、
 *               StepEnabled（默认 StepNone）、Interpret/UpdateEdit（内部
 *               提交/刷新钩子）、PrefixText/SuffixText（前后缀借用取出，
 *               XSpinBox 重载）；
 *             - 事件：keyPressEvent（Up/Down/PageUp/PageDown 步进、
 *               Home/End 仅在 Shift 下选到前缀后/后缀前、其余交内嵌编辑框、
 *               Return/Enter 提交并发 editingFinished + 转发内嵌编辑框
 *               returnPressed）、wheelEvent（120 角度=1 步，Ctrl×10）、
 *               focusIn/focusOut（失焦提交）、close/hide/show/timer 转发父类、
 *               mouseMove 转发父类；
 *             - 步进按钮长按连发：spinButtonPress/_Release 驱动阈值+重复
 *               两枚定时器（accelerated 逐拍缩短间隔）。
 *             - editingFinished 信号（连接内嵌编辑框的 editingFinished
 *               转发，Return/失焦触发）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "CXinYueConfig.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#if XWIDGET_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON

#include "XAbstractSpinBox.h"
#include "XObject_Protected.h"
#include "XString.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
/* XStyle/XStyleOption 无条件包含（同 XSpinBox.c）：连发定时器的样式提示
 * 常量 XStyleSH_SpinBox_ClickAutoRepeat* 在 XSTYLE_ON=0 下仍参与
 * spinbox_styleHint 的入参表达式（取不到样式时只回落缺省值）。 */
#include "XStyle.h"
#include "XStyleOption.h"
#include <limits.h>
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif /* XWINDOWEVENT_ON */

/** @brief 步进按钮长按的缺省节拍（样式提示不可用时的兜底，与
 *         QCommonStyle 的 SH_SpinBox_ClickAutoRepeat{Threshold,Rate}
 *         缺省值 500/150 一致，qcommonstyle.cpp:5200-5207）。 */
#define XABSTRACTSPINBOX_REPEAT_THRESHOLD_MS 500
#define XABSTRACTSPINBOX_REPEAT_RATE_MS 150
/** @brief 加速连发间隔下限（Qt `effectiveSpinRepeatRate - acceleration >= 10`）。 */
#define XABSTRACTSPINBOX_REPEAT_MIN_MS 10

/* ==================== 前向声明 ==================== */
static void  VXAbstractSpinBox_deinit(XClass* obj);
static void  VXAbstractSpinBox_copy(XAbstractSpinBox* self,
                                    const XAbstractSpinBox* other);
static void  VXAbstractSpinBox_move(XAbstractSpinBox* self,
                                    XAbstractSpinBox* other);
static void  VXAbstractSpinBox_stepBy(XAbstractSpinBox* self, int steps);
static XValidatorState VXAbstractSpinBox_validate(XAbstractSpinBox* self,
                                                  const char* input,
                                                  int* pos);
static void  VXAbstractSpinBox_fixup(XAbstractSpinBox* self, char* input,
                                     size_t capacity);
static void  VXAbstractSpinBox_clear(XAbstractSpinBox* self);
static int   VXAbstractSpinBox_stepEnabled(XAbstractSpinBox* self);
static void  VXAbstractSpinBox_interpret(XAbstractSpinBox* self);
static void  VXAbstractSpinBox_updateEdit(XAbstractSpinBox* self);
static const char* VXAbstractSpinBox_prefixText(const XAbstractSpinBox* self);
static const char* VXAbstractSpinBox_suffixText(const XAbstractSpinBox* self);
static void  VXAbstractSpinBox_resizeEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_changeEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_keyPressEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_keyReleaseEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_wheelEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_focusInEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_focusOutEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_closeEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_hideEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_showEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_mouseMoveEvent(XWidget* self, XEvent* event);
static void  VXAbstractSpinBox_timerEvent(XObject* self, XTimerEvent* event);

/* ==================== 内部辅助 ==================== */

static void spinbox_forwardEditingFinished(XObject* sender, XVarList* args);
static void spinbox_syncEditFieldGeometry(XAbstractSpinBox* self);

/** @brief 失效建议尺寸缓存（对标 Qt `cachedSizeHint = QSize()`）。
 *
 *  @details Qt 的 sizeHint/minimumSizeHint 各有一枚缓存
 *           （qabstractspinbox_p.h:97），量程/前后缀/字体/样式变化时置空
 *           待下次查询重算。本库的缓存位即 m_cachedSizeHint/
 *           m_cachedMinimumSizeHint（width<0=未缓存）；失效**只清缓存**，
 *           布局通知（updateGeometry）由各 setter 按 Qt 同名函数的落点
 *           单独发——Qt 的 setPrefix/setSuffix/setRange 才带
 *           updateGeometry，setSpecialValueText 与 event(FontChange) 不带。
 *
 *  @param self        微调框对象。
 *  @param alsoMinimum true=两枚缓存都清；false=只清 sizeHint
 *                     （对标 Qt「minimumSizeHint doesn't care about
 *                     specialValueText / suffix」的单独失效口径）。 */
static void spinbox_invalidateSizeHints(XAbstractSpinBox* self,
                                        bool alsoMinimum)
{
    if (!self) return;
    XSize_init(&self->m_cachedSizeHint, -1, -1);
    if (alsoMinimum)
        XSize_init(&self->m_cachedMinimumSizeHint, -1, -1);
}

/** @brief 发射 void 信号（无连接时释放参数列表）。 */
static void spinbox_emitVoidSignal(XAbstractSpinBox* self, size_t signal)
{
    XVarList* arguments;
    if (!self) return;
    arguments = XVarList_create(0);
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 统计 UTF-8 文本的字符数（XLineEdit 光标/选区按字符索引；
 *         Qt 的 prefix.size() 是 UTF-16 码元数，本库对齐口径为字符数）。 */
static int spinbox_utf8Chars(const char* s)
{
    int chars = 0;
    if (!s) return 0;
    while (*s) {
        if (((unsigned char)*s & 0xC0) != 0x80) ++chars;
        ++s;
    }
    return chars;
}

#if XSTYLE_ON
/** @brief 取样式提示（样式模块关闭时不编译；调用方走缺省常量）。 */
static int spinbox_styleHint(XAbstractSpinBox* self, int hint, int fallback)
{
    XStyle* st;
    if (!self) return fallback;
    st = XWidget_style((const XWidget*)self);
    if (st) {
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCC_SpinBox);
        XRect_init(&opt.m_rect, 0, 0, XWidget_width((XWidget*)self),
                   XWidget_height((XWidget*)self));
        opt.m_spinFrame = self->m_frame;
        opt.m_spinSymbols = self->m_buttonSymbols;
        return XStyle_styleHint(st, hint, &opt, (XWidget*)self);
    }
    return fallback;
}
#endif /* XSTYLE_ON */

/** @brief 连发重复间隔（ms；样式 SH_SpinBox_ClickAutoRepeatRate）。 */
static int spinbox_repeatRate(XAbstractSpinBox* self)
{
#if XSTYLE_ON
    return spinbox_styleHint(self, XStyleSH_SpinBox_ClickAutoRepeatRate,
                             XABSTRACTSPINBOX_REPEAT_RATE_MS);
#else
    (void)self;
    return XABSTRACTSPINBOX_REPEAT_RATE_MS;
#endif
}

/** @brief 连发阈值（ms；样式 SH_SpinBox_ClickAutoRepeatThreshold）。 */
static int spinbox_repeatThreshold(XAbstractSpinBox* self)
{
#if XSTYLE_ON
    return spinbox_styleHint(self, XStyleSH_SpinBox_ClickAutoRepeatThreshold,
                             XABSTRACTSPINBOX_REPEAT_THRESHOLD_MS);
#else
    (void)self;
    return XABSTRACTSPINBOX_REPEAT_THRESHOLD_MS;
#endif
}

/** @brief 复位步进连发状态（对标 Qt QAbstractSpinBoxPrivate::reset）。
 *  @details 杀两枚定时器、清按钮态与加速累计、标脏重绘（按钮按下态的
 *           凹面外观随之复原）。Qt 在 close/hide/focusOut/禁用/失焦窗口
 *           与鼠标移出按钮时调用。 */
static void spinbox_resetButtons(XAbstractSpinBox* self)
{
    if (!self) return;
    if (self->m_spinClickTimerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_spinClickTimerId);
        self->m_spinClickTimerId = XTIMER_INVALID_ID;
    }
    if (self->m_spinClickThresholdTimerId != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_spinClickThresholdTimerId);
        self->m_spinClickThresholdTimerId = XTIMER_INVALID_ID;
    }
    if (self->m_buttonState != (uint8_t)XAbstractSpinBoxButtonState_None)
        XWidget_update((XWidget*)self);
    self->m_buttonState = (uint8_t)XAbstractSpinBoxButtonState_None;
    self->m_acceleration = 0;
}

/** @brief 按方向做一次连发步进（Ctrl 按住时 ×10，对标 Qt timerEvent）。 */
static void spinbox_repeatStep(XAbstractSpinBox* self, bool up)
{
    int steps = self->m_stepModifierDown ? 10 : 1;
    XAbstractSpinBox_stepBy_base(self, up ? steps : -steps);
}

/** @brief 按样式同步内嵌编辑框几何（SC_SpinBoxEditField 口径）。
 * @details    修复「SpinBox 聚焦无可见指示」第二段：此前 resizeEvent/
 *             setButtonSymbols 把编辑框写死 (0,0,w-16,h)，黄底样式表
 *             （XLineEdit{background:#FFFFE0}）铺满容器自身矩形，把容器
 *             frame/虚线聚焦环整圈盖住（实测 709/724 像素黄底）。现改经
 *             样式 subControlRect(CC_SpinBox, SC_SpinBoxEditField) 取编辑
 *             区矩形——样式侧按 PM_SpinBoxFrameWidth（XCommonStyle=2/
 *             Fusion=3）四周留边距，给 frame 斜面与聚焦虚线留出可见带；
 *             按钮列宽亦由样式按高度推导，与绘制侧同源。无样式时回退
 *             既有口径（占满减固定 16px 按钮列、无边距——非样式回退
 *             绘制路径本无 frame）。 */
static void spinbox_syncEditFieldGeometry(XAbstractSpinBox* self)
{
    int w;
    int h;
    if (!self || !self->m_lineEdit) return;
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        XStyleOption opt;
        XRect field;
        XStyleOption_init(&opt, XStyleCC_SpinBox);
        XRect_init(&opt.m_rect, 0, 0, w, h);
        opt.m_state = XWidget_isEnabled((XWidget*)self)
            ? (uint32_t)XStyleState_Enabled : 0;
        /* frame/按钮符号对接基类属性（与绘制侧 XSpinBox paintEvent
           同口径：m_spinFrame 裁边距、m_spinSymbols=NoButtons 时
           EditField 全宽）。 */
        opt.m_spinFrame = self->m_frame;
        opt.m_spinSymbols = self->m_buttonSymbols;
        field = XStyle_subControlRect(XWidget_style((const XWidget*)self),
                                      XStyleCC_SpinBox, &opt,
                                      XStyleSC_SpinBoxEditField,
                                      (XWidget*)self);
        if (field.width > 0 && field.height > 0) {
            XWidget_setGeometry((XWidget*)self->m_lineEdit,
                                field.x, field.y, field.width, field.height);
            return;
        }
    }
#endif /* XSTYLE_ON */
    {
        int ew = w;
        if (self->m_buttonSymbols !=
            XAbstractSpinBoxButtonSymbols_NoButtons)
            ew -= 16;
        if (ew < 1) ew = 1;
        XWidget_setGeometry((XWidget*)self->m_lineEdit, 0, 0, ew, h);
    }
}

/** @brief 内部创建默认编辑框。 */
static XLineEdit* spinbox_createDefaultLineEdit(XAbstractSpinBox* self)
{
    XLineEdit* edit = XLineEdit_create((XWidget*)self, 0);
    if (!edit) return NULL;
    /* 初创临时占满（此时 self->m_lineEdit 尚未挂上，样式同步须待
       resizeEvent/setButtonSymbols 收口）；真实几何随首次 RESIZE
       经 spinbox_syncEditFieldGeometry 收敛。 */
    XWidget_setGeometry((XWidget*)edit, 0, 0,
                        XWidget_width((XWidget*)self),
                        XWidget_height((XWidget*)self));
    /* 子控件默认可见（QAbstractSpinBox 内嵌编辑框语义）。 */
    XWidget_show((XWidget*)edit);
    XObject_connect_2((XObject*)edit,
                      (size_t)XLineEdit_editingFinished_signal(edit),
                      spinbox_forwardEditingFinished);
    /* 对标 Qt QAbstractSpinBoxPrivate::init 的
       d->edit->setFocusProxy(q)（qabstractspinbox.cpp:691）：容器注册
       焦点代理指向内嵌编辑框。修复「SpinBox 聚焦无可见指示」第一段：
       此前未注册，XWidget_hasFocus(容器) 只认 g_focusWidget==容器
       本体——Tab/点击后焦点实际落内嵌编辑框，容器 hasFocus 恒假，
       XSpinBox_paintEvent 的 HasFocus 位永远置不上。注册后
       hasFocus(容器) 经 deepestFocusProxy 与编辑框持焦同一真值。
       注意此处焦点代理方向与 Qt 相反（Qt 是编辑框代理到容器、焦点
       停容器本体；XGui 取容器代理到编辑框、焦点落编辑框——编辑框
       直持焦点使键入/选区/IME 走自身完整链路，容器经代理判据与
       宿主 update（XWidget_setFocus_2 三段配套）画出聚焦环）。 */
    XWidget_setFocusProxy((XWidget*)self, (XWidget*)edit);
    return edit;
}

/** @brief 内嵌编辑框 editingFinished 转发槽（转发为自身 editingFinished）。 */
static void spinbox_forwardEditingFinished(XObject* sender, XVarList* args)
{
    XAbstractSpinBox* self;
    (void)args;
    if (!sender) return;
    self = (XAbstractSpinBox*)XWidget_parentWidget((XWidget*)sender);
    /* sender 即内嵌编辑框；parent 即本控件 */
    if (self)
        spinbox_emitVoidSignal(
            self, (size_t)XAbstractSpinBox_editingFinished_signal(self));
}

/* [死码清理] spinbox_strdup/spinbox_strfree 已删除：与 XSpinBox.c 活本
 * 同体而本文件零调用（审计清单）。 */

/* ==================== 虚槽实现 ==================== */

/** @brief 尺寸变化：同步内嵌编辑框几何（样式 SC_SpinBoxEditField 边距
 *         口径，见 spinbox_syncEditFieldGeometry）。RESIZE 事件走
 *         ResizeEvent 虚槽（不走 changeEvent），在此同步编辑框。 */
static void VXAbstractSpinBox_resizeEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_RESIZE) return;
    if (spin->m_lineEdit)
        spinbox_syncEditFieldGeometry(spin);
    XClass_Parent(XWidget, EXWidget_ResizeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 属性变化事件：字体/样式变化时失效两枚尺寸缓存（对标 Qt
 *         QAbstractSpinBox::event 的 QEvent::FontChange/StyleChange 分支，
 *         qabstractspinbox.cpp:756-759 `d->cachedSizeHint =
 *         d->cachedMinimumSizeHint = QSize()`）。
 *
 *  @details 只清缓存、不发 updateGeometry：字体/样式入口（XWidget_setFont
 *           / XWidget_setStyle）自身已带布局失效通知，布局下次协商时
 *           查询 sizeHint 即命中空缓存重算——与 Qt 同构（Qt 同样依赖
 *           setFont → updateGeometry 提供通知，event 分支只清缓存）。 */
static void VXAbstractSpinBox_changeEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event) return;
    switch (XEvent_type(event)) {
    case XEVENT_TYPE_FONT_CHANGE:
    case XEVENT_TYPE_STYLE_CHANGE:
        spinbox_invalidateSizeHints(spin, true);
        break;
    default:
        break;
    }
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief StepBy 默认实现：空操作（子类重载）。 */
static void VXAbstractSpinBox_stepBy(XAbstractSpinBox* self, int steps)
{
    (void)self;
    (void)steps;
}

/** @brief Validate 默认实现：恒为 Acceptable（对标 Qt 基类默认）。 */
static XValidatorState VXAbstractSpinBox_validate(XAbstractSpinBox* self,
                                                  const char* input,
                                                  int* pos)
{
    (void)self;
    (void)input;
    (void)pos;
    return XValidatorState_Acceptable;
}

/** @brief Fixup 默认实现：空操作（子类重载）。 */
static void VXAbstractSpinBox_fixup(XAbstractSpinBox* self, char* input,
                                    size_t capacity)
{
    (void)self;
    (void)input;
    (void)capacity;
}

/** @brief Clear 默认实现：清成「前缀+后缀」并置内部 cleared 标志。
 *  @details 对标 QAbstractSpinBox::clear（qabstractspinbox.cpp:510-517）：
 *           setText 在前、cleared 在后——否则文本变化回调会把刚置位的
 *           标志冲掉，clear 后的失焦提交又按旧文本回填（gap 97 实锅）。
 *           前缀/后缀经虚槽取（XSpinBox 返回自有文本，其余子类为空）。 */
static void VXAbstractSpinBox_clear(XAbstractSpinBox* self)
{
    const char* prefix;
    const char* suffix;
    char buf[512];
    if (!self) return;
    prefix = XAbstractSpinBox_prefixText_base(self);
    suffix = XAbstractSpinBox_suffixText_base(self);
    if (self->m_lineEdit) {
        XSnprintf(buf, sizeof(buf), "%s%s",
                  prefix ? prefix : "", suffix ? suffix : "");
        XLineEdit_setText(self->m_lineEdit, buf);
        XLineEdit_setCursorPosition(self->m_lineEdit,
                                    spinbox_utf8Chars(prefix));
    }
    self->m_cleared = true;
}

/**
 * @brief StepEnabled 默认实现：返回 StepNone。
 * @details Qt 侧 QAbstractSpinBox::stepEnabled (qabstractspinbox.cpp:537-552)
 *          的位组合依赖私有 value/minimum/maximum 三个 QVariant 与 type
 *          字段（qabstractspinbox_p.h:92 附近），基类本身无范围/取值概念：
 *          未设置 QVariant type 的裸子类实测恒返回 StepNone（Qt 6.8.3
 *          实测：基类默认 0、readOnly 0、wrapping 0），本实现与之同。
 *          XSpinBox / XDateTimeEdit 均已覆写该虚槽（各自按范围给位）。 */
static int VXAbstractSpinBox_stepEnabled(XAbstractSpinBox* self)
{
    (void)self;
    return XAbstractSpinBoxStepEnabledFlag_StepNone;
}

/** @brief Interpret 默认实现：空操作（提交语义由子类承接）。 */
static void VXAbstractSpinBox_interpret(XAbstractSpinBox* self)
{
    (void)self;
}

/** @brief UpdateEdit 默认实现：仅重绘（显示文本刷新由子类承接）。 */
static void VXAbstractSpinBox_updateEdit(XAbstractSpinBox* self)
{
    if (self) XWidget_update((XWidget*)self);
}

/** @brief PrefixText 默认实现：空串（基类无前后缀概念）。 */
static const char* VXAbstractSpinBox_prefixText(const XAbstractSpinBox* self)
{
    (void)self;
    return "";
}

/** @brief SuffixText 默认实现：空串。 */
static const char* VXAbstractSpinBox_suffixText(const XAbstractSpinBox* self)
{
    (void)self;
    return "";
}

/** @brief 键盘按下：Up/Down/PageUp/PageDown 步进、Home/End 仅在 Shift
 *         组合下选到前缀后/后缀前、Return/Enter 提交并转发
 *         returnPressed；其余按键转发内嵌编辑框。
 *  @details 对齐 Qt 6.8 QAbstractSpinBox::keyPressEvent：
 *           - Up/Down：先 accept 再判步进使能（qabstractspinbox.cpp:
 *             1006-1010），Ctrl（stepModifier）×10；
 *           - PageUp/PageDown：恒 ±10（Ctrl 的 ×10 只作用于普通 Up/Down）；
 *           - Home/End：**仅在带 Shift 时**本控件处理——End 从光标选到
 *             后缀前、Home 从光标选到前缀后（qabstractspinbox.cpp:
 *             1074-1093）；不带 Shift 时落到内嵌编辑框做光标移动，
 *             值不变（探针实测 value 不变）；
 *           - Return/Enter：interpret + selectAll + 忽略事件 + 发
 *             editingFinished + 转发内嵌编辑框的 returnPressed
 *             （qabstractspinbox.cpp:1052）；
 *           - 其余：转发内嵌编辑框，随后 `if (!edit->text().isEmpty())`
 *             复位 cleared（qabstractspinbox.cpp:1098-1100）。
 *           此前 Home/End 被当作「跳到 minimum/maximum」的大步数步进，
 *           与 Qt 语义相反（gap 93：Home 直接改值 = 数据破坏）。 */
static void VXAbstractSpinBox_keyPressEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    XKeyEvent* ke;
    int key;
    int mods;
    int steps;
    int flags;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    key = XKeyEvent_key(ke);
    mods = (int)XKeyEvent_modifiers(ke);

    switch (key) {
    case XKey_Up:
    case XKey_Down: {
        /* 对标 Qt：按住 stepModifier(Ctrl) 时 Up/Down 步进 ×10。
         * 事件先 accept 再看步进使能（qabstractspinbox.cpp:1006-1010：
         * `event->accept(); ... if (!(stepEnabled() & ...)) return;`）——
         * 步进被禁时按键被本控件吞掉、不转投内嵌编辑框（编辑框若收到
         * 会当成光标上/下移动）。 */
        bool up = (key == XKey_Up);
        steps = (mods & (int)XKeyboardModifier_ControlModifier) ? 10 : 1;
        XEvent_accept(event);
        flags = XAbstractSpinBox_stepEnabled_base(spin);
        if (flags & (up ? XAbstractSpinBoxStepEnabledFlag_StepUpEnabled
                        : XAbstractSpinBoxStepEnabledFlag_StepDownEnabled))
            XAbstractSpinBox_stepBy_base(spin, up ? steps : -steps);
        return;
    }
    case XKey_PageUp:
    case XKey_PageDown: {
        /* PageUp/PageDown 恒 stepBy(±10)；Qt 的 Ctrl×10 只作用于普通
         * Up/Down（qabstractspinbox.cpp:1008 `if (!isPgUpOrDown && ...)`）。 */
        bool up = (key == XKey_PageUp);
        steps = 10;
        XEvent_accept(event);
        flags = XAbstractSpinBox_stepEnabled_base(spin);
        if (flags & (up ? XAbstractSpinBoxStepEnabledFlag_StepUpEnabled
                        : XAbstractSpinBoxStepEnabledFlag_StepDownEnabled))
            XAbstractSpinBox_stepBy_base(spin, up ? steps : -steps);
        return;
    }
    case XKey_Home:
    case XKey_End: {
        /* 对标 Qt：只在 Shift 组合下处理（选到前缀后/后缀前），其余交
         * 内嵌编辑框移动光标（qabstractspinbox.cpp:1074-1093）。 */
        if (spin->m_lineEdit &&
            (mods & (int)XKeyboardModifier_ShiftModifier)) {
            const char* prefix = XAbstractSpinBox_prefixText_base(spin);
            const char* suffix = XAbstractSpinBox_suffixText_base(spin);
            int prefixChars = spinbox_utf8Chars(prefix);
            int suffixChars = spinbox_utf8Chars(suffix);
            int totalChars = spinbox_utf8Chars(
                XLineEdit_text(spin->m_lineEdit));
            int currentPos = XLineEdit_cursorPosition(spin->m_lineEdit);
            if (key == XKey_End) {
                if (!(currentPos == 0 && prefixChars > 0) &&
                    totalChars - suffixChars > currentPos) {
                    XLineEdit_setSelection(spin->m_lineEdit, currentPos,
                        totalChars - suffixChars - currentPos);
                    XEvent_accept(event);
                    return;
                }
            } else {
                if (!(currentPos == totalChars && suffixChars > 0) &&
                    currentPos > prefixChars) {
                    /* Qt 此处 setSelection(currentPos, prefix.size()-
                     * currentPos) 传负长度＝向前扩选（prefix..光标）。 */
                    XLineEdit_setSelection(spin->m_lineEdit, currentPos,
                        prefixChars - currentPos);
                    XEvent_accept(event);
                    return;
                }
            }
            /* 已在边界：交编辑框（Qt 的 break 分支）。 */
        }
        break;
    }
    case XKey_Return:
    case XKey_Enter:
        /* 提交编辑：解释文本 + 全选 + 忽略事件 + 发 editingFinished +
         * 转发内嵌编辑框的 returnPressed（qabstractspinbox.cpp:
         * 1048-1052 `emit editingFinished(); emit d->edit->returnPressed();`）。 */
        XAbstractSpinBox_interpretText(spin);
        XAbstractSpinBox_selectAll(spin);
        XEvent_ignore(event);
        spinbox_emitVoidSignal(
            spin, (size_t)XAbstractSpinBox_editingFinished_signal(spin));
        if (spin->m_lineEdit) {
            XVarList* args = XVarList_create(0);
            if (args) {
                if (((XObject*)spin->m_lineEdit)->m_signalSlot) {
                    XObject_emitSignal(
                        (XObject*)spin->m_lineEdit,
                        (size_t)XLineEdit_returnPressed_signal(spin->m_lineEdit),
                        args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
                } else {
                    XVarList_delete(args);
                }
            }
        }
        return;
    case XKey_Tab:
    case XKey_Backtab:
        /* Tab/Backtab 不转发内嵌编辑框（单行编辑从不插 Tab）：显式忽略，
         * 让框架焦点遍历迁焦到下一候选（对标 Qt QWidget::event 对 Tab 的
         * 焦点链拦截先于 keyPressEvent，qwidget.cpp:9324 前段）。此前被
         * 编辑框消费致 SpinBox→Slider 焦点链断岛（复扫-3 #41 残）。 */
        XEvent_ignore(event);
        return;
    default:
        break;
    }

    /* 其余按键转发内嵌编辑框（对标 Qt：d->edit->event(event)）。 */
    if (spin->m_lineEdit) {
        XObject_event_base((XObject*)spin->m_lineEdit, event);
        /* 对标 Qt qabstractspinbox.cpp:1098-1100：用户键入出非空文本即
         * 复位 clear() 置的 cleared 标志（此后提交/刷新恢复正常）。 */
        if (XLineEdit_text(spin->m_lineEdit)[0] != '\0')
            spin->m_cleared = false;
        return;
    }
    XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 键盘释放：转发内嵌编辑框（长按连续步进为后续扩展）。 */
static void VXAbstractSpinBox_keyReleaseEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_RELEASE) return;
    if (spin->m_lineEdit) {
        XObject_event_base((XObject*)spin->m_lineEdit, event);
        return;
    }
    XClass_Parent(XWidget, EXWidget_KeyReleaseEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 滚轮步进：角度增量累计，每 120 角度=1 步；Ctrl 时 ×10。 */
static void VXAbstractSpinBox_wheelEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    int steps = 0;
    int flags;
    if (!spin || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
#if XWINDOWEVENT_ON
    {
        XWheelEvent* we = (XWheelEvent*)event;
        XPoint delta = XWheelEvent_angleDelta(we);
        XKeyboardModifiers mods = XWheelEvent_modifiers(we);
        /* 只取纵向角度增量（对标 qabstractspinbox.cpp:1151-1158
         * `wheelDeltaRemainder += event->angleDelta().y()`；只有 macOS
         * +Shift 的合成滚轮才改读 x，本库无该平台分支）。正角度=向上
         * 步进，120=1 步，Ctrl（stepModifier）×10。 */
        spin->m_wheelDeltaRemainder += delta.y;
        steps = spin->m_wheelDeltaRemainder / 120;
        spin->m_wheelDeltaRemainder -= steps * 120;
        if (mods & XKeyboardModifier_ControlModifier) steps *= 10;
    }
#endif /* XWINDOWEVENT_ON */
    if (steps != 0) {
        flags = XAbstractSpinBox_stepEnabled_base(spin);
        if (flags & (steps > 0 ? XAbstractSpinBoxStepEnabledFlag_StepUpEnabled
                               : XAbstractSpinBoxStepEnabledFlag_StepDownEnabled))
            XAbstractSpinBox_stepBy_base(spin, steps);
    }
    XEvent_accept(event);
}

/** @brief 获得焦点：把焦点事件先转发内嵌编辑框再链父类（对标 Qt
 *         QAbstractSpinBox::focusInEvent 的 d->edit->event(event)）。
 *         编辑框收到焦点事件会同步焦点态并自行重绘——修复 night #40：
 *         Tab 进入时焦点落在微调框本体，父控件带焦重绘把子编辑框像素
 *         抹掉而无人重画，编辑区整白、值文本不可见；鼠标点击路径因
 *         mousePressEvent 直发编辑框（编辑框自取焦点重绘）故正常。
 *         随后 Tab/Backtab 原因进入时全选编辑框文本（对标 Qt 同函数
 *         的 selectAll 分支）。 */
static void VXAbstractSpinBox_focusInEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_FOCUS_IN) return;
    if (spin->m_lineEdit)
        XObject_event_base((XObject*)spin->m_lineEdit, event);
    if (((XFocusEvent*)event)->m_reason == XFocusReason_Tab ||
        ((XFocusEvent*)event)->m_reason == XFocusReason_Backtab) {
        if (spin->m_lineEdit)
            XLineEdit_selectAll(spin->m_lineEdit);
    }
    XClass_Parent(XWidget, EXWidget_FocusInEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 失去焦点：先解释当前文本，再刷新编辑框显示并转发父类、发射
 *         editingFinished。对标 Qt QAbstractSpinBox::focusOutEvent 的
 *         d->updateEdit()：失焦后父控件无焦重绘同样会抹掉子编辑框像素，
 *         经 updateEdit 链（子类刷新编辑框文本→编辑框重绘）恢复显示。
 *         注意不把失焦事件转发编辑框：编辑框失焦门禁会经既有转发线重复
 *         发射 editingFinished（Qt 中二者是不同信号，XGui 已把编辑框
 *         editingFinished 转发为本控件同名信号，详见
 *         spinbox_forwardEditingFinished）。 */
static void VXAbstractSpinBox_focusOutEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event) return;
    /* 对标 Qt focusOutEvent：先 reset 连发定时器（qabstractspinbox.cpp:
     * 1244 `d->reset()`），再 updateEdit（cleared 期间早退）。 */
    spinbox_resetButtons(spin);
    XAbstractSpinBox_interpretText(spin);
    XAbstractSpinBox_updateEdit_base(spin);
    XClass_Parent(XWidget, EXWidget_FocusOutEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    spinbox_emitVoidSignal(
        spin, (size_t)XAbstractSpinBox_editingFinished_signal(spin));
}

/** @brief 关闭事件：先解释当前文本，再转发父类。 */
static void VXAbstractSpinBox_closeEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event) return;
    spinbox_resetButtons(spin);
    XAbstractSpinBox_interpretText(spin);
    XClass_Parent(XWidget, EXWidget_CloseEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 隐藏事件：先解释当前文本，再转发父类。 */
static void VXAbstractSpinBox_hideEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event) return;
    spinbox_resetButtons(spin);
    XAbstractSpinBox_interpretText(spin);
    XClass_Parent(XWidget, EXWidget_HideEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 显示事件：刷新显示文本后转发父类。 */
static void VXAbstractSpinBox_showEvent(XWidget* self, XEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    if (!spin || !event) return;
    XAbstractSpinBox_updateEdit_base(spin);
    XClass_Parent(XWidget, EXWidget_ShowEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 鼠标移动：转发父类（按住步进按钮期间的方向保持/取消由
 *         XSpinBox 的重载按命中区处理，见 VXSpinBox_mouseMoveEvent）。 */
static void VXAbstractSpinBox_mouseMoveEvent(XWidget* self, XEvent* event)
{
    if (self && event) {
        XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                      void(*)(XWidget*, XEvent*))((XWidget*)self, event);
    }
}

/** @brief 定时器事件：消费长按阈值/重复两枚定时器，其余转发父类。
 *  @details 对标 Qt QAbstractSpinBox::timerEvent（qabstractspinbox.cpp:
 *           1228-1268）：
 *           - 阈值定时器到期 ⇒ 杀阈值、记 effectiveSpinRepeatRate（鼠标
 *             按用样式 SH_SpinBox_ClickAutoRepeatRate）、起重复定时器、
 *             立即步进一拍；
 *           - 重复定时器到期 ⇒ accelerated 时 `acceleration +=
 *             rate*0.05`，若 `rate-acceleration >= 10` 则按缩短后的间隔
 *             重起定时器；随后步进一拍；
 *           - 步进方向不可用（stepEnabled 关）⇒ reset 停发。 */
static void VXAbstractSpinBox_timerEvent(XObject* self, XTimerEvent* event)
{
    XAbstractSpinBox* spin = (XAbstractSpinBox*)self;
    XTimerId id;
    bool doStep = false;
    bool up;
    if (!spin || !event) return;
    id = XTimerEvent_timerId(event);
    if (id != XTIMER_INVALID_ID && id == spin->m_spinClickThresholdTimerId) {
        XObject_killTimer(self, spin->m_spinClickThresholdTimerId);
        spin->m_spinClickThresholdTimerId = XTIMER_INVALID_ID;
        spin->m_effectiveSpinRepeatRate = spinbox_repeatRate(spin);
        spin->m_acceleration = 0;
        spin->m_spinClickTimerId = XObject_startTimer_ms(
            self, (uint64_t)(spin->m_effectiveSpinRepeatRate > 0
                                 ? spin->m_effectiveSpinRepeatRate
                                 : XABSTRACTSPINBOX_REPEAT_RATE_MS),
            XTimerType_CoarseTimer);
        doStep = true;
    } else if (id != XTIMER_INVALID_ID && id == spin->m_spinClickTimerId) {
        if (spin->m_accelerated) {
            int interval = spin->m_effectiveSpinRepeatRate > 0
                ? spin->m_effectiveSpinRepeatRate
                : XABSTRACTSPINBOX_REPEAT_RATE_MS;
            spin->m_acceleration += (int)((double)interval * 0.05);
            if (interval - spin->m_acceleration >=
                XABSTRACTSPINBOX_REPEAT_MIN_MS) {
                XObject_killTimer(self, spin->m_spinClickTimerId);
                spin->m_spinClickTimerId = XObject_startTimer_ms(
                    self, (uint64_t)(interval - spin->m_acceleration),
                    XTimerType_CoarseTimer);
            }
        }
        doStep = true;
    }
    if (doStep) {
        const int flags = XAbstractSpinBox_stepEnabled_base(spin);
        up = (spin->m_buttonState &
              (uint8_t)XAbstractSpinBoxButtonState_Up) != 0;
        if (!(flags & (up ? XAbstractSpinBoxStepEnabledFlag_StepUpEnabled
                          : XAbstractSpinBoxStepEnabledFlag_StepDownEnabled))) {
            spinbox_resetButtons(spin);
        } else {
            spinbox_repeatStep(spin, up);
        }
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))(self, event);
}

/** @brief 深拷贝：基类深拷贝后复制编辑框指针语义（拷贝为重建默认编辑框）。 */
static void VXAbstractSpinBox_copy(XAbstractSpinBox* self,
                                   const XAbstractSpinBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractSpinBox_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_buttonSymbols = other->m_buttonSymbols;
    self->m_keyboardTracking = other->m_keyboardTracking;
    self->m_frame = other->m_frame;
    self->m_correctionMode = other->m_correctionMode;
    self->m_wrapping = other->m_wrapping;
    self->m_accelerated = other->m_accelerated;
    self->m_groupSeparatorShown = other->m_groupSeparatorShown;
    self->m_cleared = other->m_cleared;
    self->m_wheelDeltaRemainder = other->m_wheelDeltaRemainder;
    /* 连发定时器/按钮态不随拷贝迁移（新对象无按住会话）。 */
    self->m_buttonState = (uint8_t)XAbstractSpinBoxButtonState_None;
    self->m_spinClickThresholdTimerId = XTIMER_INVALID_ID;
    self->m_spinClickTimerId = XTIMER_INVALID_ID;
    self->m_effectiveSpinRepeatRate = XABSTRACTSPINBOX_REPEAT_RATE_MS;
    self->m_acceleration = 0;
    self->m_stepModifierDown = false;
    /* 尺寸缓存不随拷贝迁移（新对象属性域未定，置未缓存待重算）。 */
    XSize_init(&self->m_cachedSizeHint, -1, -1);
    XSize_init(&self->m_cachedMinimumSizeHint, -1, -1);
    if (self->m_specialValueText) {
        XClassDelete(self->m_specialValueText);
        self->m_specialValueText = NULL;
    }
    if (other->m_specialValueText)
        self->m_specialValueText =
            XString_create_copy(other->m_specialValueText);
    /* 编辑框不可复制：重建默认编辑框（父控件指针指向自身）。 */
    if (self->m_lineEdit)
        XClassDelete(self->m_lineEdit);
    self->m_lineEdit = spinbox_createDefaultLineEdit(self);
}

/** @brief 移动语义：转移编辑框指针与字符串，源对象归构造默认值。 */
static void VXAbstractSpinBox_move(XAbstractSpinBox* self,
                                   XAbstractSpinBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XAbstractSpinBox_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    if (self->m_lineEdit)
        XClassDelete(self->m_lineEdit);
    self->m_lineEdit = other->m_lineEdit;
    other->m_lineEdit = NULL;
    if (self->m_specialValueText) {
        XClassDelete(self->m_specialValueText);
        self->m_specialValueText = NULL;
    }
    self->m_specialValueText = other->m_specialValueText;
    other->m_specialValueText = NULL;
    self->m_buttonSymbols = other->m_buttonSymbols;
    self->m_keyboardTracking = other->m_keyboardTracking;
    self->m_frame = other->m_frame;
    self->m_correctionMode = other->m_correctionMode;
    self->m_wrapping = other->m_wrapping;
    self->m_accelerated = other->m_accelerated;
    self->m_groupSeparatorShown = other->m_groupSeparatorShown;
    self->m_cleared = other->m_cleared;
    self->m_wheelDeltaRemainder = other->m_wheelDeltaRemainder;
    /* 连发定时器随内容搬家但源件已无按住会话：源侧复位、目标侧
     * 只搬状态位不搬定时器 ID（定时器归源对象所有）。 */
    self->m_buttonState = other->m_buttonState;
    self->m_spinClickThresholdTimerId = XTIMER_INVALID_ID;
    self->m_spinClickTimerId = XTIMER_INVALID_ID;
    self->m_effectiveSpinRepeatRate = other->m_effectiveSpinRepeatRate;
    self->m_acceleration = other->m_acceleration;
    self->m_stepModifierDown = other->m_stepModifierDown;
    /* 尺寸缓存随值域一起转移（值域/前后缀域同属内容域；Qt 无此语义，
       本库 move 惯例=内容整体搬家，缓存与内容保持一致）。 */
    self->m_cachedSizeHint = other->m_cachedSizeHint;
    self->m_cachedMinimumSizeHint = other->m_cachedMinimumSizeHint;
    XSize_init(&other->m_cachedSizeHint, -1, -1);
    XSize_init(&other->m_cachedMinimumSizeHint, -1, -1);
    other->m_buttonSymbols = (uint8_t)XAbstractSpinBoxButtonSymbols_UpDownArrows;
    other->m_keyboardTracking = true;
    other->m_frame = true;
    other->m_correctionMode = (uint8_t)XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue;
    other->m_wrapping = false;
    other->m_accelerated = false;
    other->m_groupSeparatorShown = false;
    other->m_cleared = false;
    other->m_wheelDeltaRemainder = 0;
    other->m_buttonState = (uint8_t)XAbstractSpinBoxButtonState_None;
    other->m_spinClickThresholdTimerId = XTIMER_INVALID_ID;
    other->m_spinClickTimerId = XTIMER_INVALID_ID;
    other->m_effectiveSpinRepeatRate = XABSTRACTSPINBOX_REPEAT_RATE_MS;
    other->m_acceleration = 0;
    other->m_stepModifierDown = false;
}

/** @brief 析构：释放内嵌编辑框与特殊值文本。 */
static void VXAbstractSpinBox_deinit(XClass* obj)
{
    XAbstractSpinBox* self = (XAbstractSpinBox*)obj;
    if (!self) return;
    /* 先停连发定时器（定时器节点挂在事件分发器上，对象析构后回调即
     * 悬垂；Qt 析构同样 reset）。 */
    spinbox_resetButtons(self);
    if (self->m_lineEdit)
        XClassDelete(self->m_lineEdit);
    self->m_lineEdit = NULL;
    if (self->m_specialValueText) {
        XClassDelete(self->m_specialValueText);
        self->m_specialValueText = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)obj);
}

/* ==================== 生命周期 ==================== */

XVtable* XAbstractSpinBox_class_init(void)
{
    void* protectedSlots[] = {
        VXAbstractSpinBox_stepBy,
        VXAbstractSpinBox_validate,
        VXAbstractSpinBox_fixup,
        VXAbstractSpinBox_clear,
        VXAbstractSpinBox_stepEnabled,
        VXAbstractSpinBox_interpret,
        VXAbstractSpinBox_updateEdit,
        VXAbstractSpinBox_prefixText,
        VXAbstractSpinBox_suffixText
    };

    XVTABLE_INIT_DEFAULT(XAbstractSpinBox)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_ADD_FUNC_LIST_DEFAULT(protectedSlots);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXAbstractSpinBox_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXAbstractSpinBox_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXAbstractSpinBox_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent, VXAbstractSpinBox_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VXAbstractSpinBox_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VXAbstractSpinBox_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent, VXAbstractSpinBox_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VXAbstractSpinBox_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VXAbstractSpinBox_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXAbstractSpinBox_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXAbstractSpinBox_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXAbstractSpinBox_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepBy, VXAbstractSpinBox_stepBy);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Validate, VXAbstractSpinBox_validate);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Fixup, VXAbstractSpinBox_fixup);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Clear, VXAbstractSpinBox_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepEnabled, VXAbstractSpinBox_stepEnabled);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Interpret, VXAbstractSpinBox_interpret);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_UpdateEdit, VXAbstractSpinBox_updateEdit);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_PrefixText, VXAbstractSpinBox_prefixText);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_SuffixText, VXAbstractSpinBox_suffixText);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXAbstractSpinBox_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXAbstractSpinBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXAbstractSpinBox_move);

    return XVTABLE_DEFAULT;
}

void XAbstractSpinBox_init(XAbstractSpinBox* self, XWidget* parent,
                           XWidgetFlags flags)
{
    if (!self) return;
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XAbstractSpinBox);
    /* 对标 QAbstractSpinBoxPrivate::init 的 setFocusPolicy
     * （qabstractspinbox.cpp:1605 `q->setFocusPolicy(Qt::WheelFocus)`）：
     * Tab/点击/滚轮都可聚焦；Tab/方向键由 keyPressEvent 分工（Tab 显式
     * ignore 交焦点遍历，见 VXAbstractSpinBox_keyPressEvent）。此前本库
     * 设 StrongFocus（缺滚轮聚焦位 0x4，取值 0x0b）；Qt 为 WheelFocus
     * （0x0f），现按 Qt 对齐。 */
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_WheelFocus);
    /* 对标 QAbstractSpinBoxPrivate::init 的尺寸策略
     * （qabstractspinbox.cpp:1606 `q->setSizePolicy(QSizePolicy(
     * QSizePolicy::Minimum, QSizePolicy::Fixed, QSizePolicy::SpinBox))`）：
     * 水平取最小提示（可增长）、垂直固定（高度锁在 sizeHint，不随布局
     * 纵向拉伸）。 */
    XWidget_setSizePolicy((XWidget*)self, XWidgetSizePolicy_Minimum,
                          XWidgetSizePolicy_Fixed);

    self->m_lineEdit = NULL;
    self->m_buttonSymbols = (uint8_t)XAbstractSpinBoxButtonSymbols_UpDownArrows;
    self->m_keyboardTracking = true;
    self->m_frame = true;
    self->m_correctionMode = (uint8_t)XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue;
    self->m_wrapping = false;
    self->m_accelerated = false;
    self->m_groupSeparatorShown = false;
    self->m_specialValueText = NULL;
    self->m_cleared = false;
    self->m_wheelDeltaRemainder = 0;
    self->m_buttonState = (uint8_t)XAbstractSpinBoxButtonState_None;
    self->m_spinClickThresholdTimerId = XTIMER_INVALID_ID;
    self->m_spinClickTimerId = XTIMER_INVALID_ID;
    self->m_effectiveSpinRepeatRate = XABSTRACTSPINBOX_REPEAT_RATE_MS;
    self->m_acceleration = 0;
    self->m_stepModifierDown = false;
    /* 尺寸缓存置「未缓存」（width<0 哨兵；Qt 构造态缓存即空 QSize）。 */
    XSize_init(&self->m_cachedSizeHint, -1, -1);
    XSize_init(&self->m_cachedMinimumSizeHint, -1, -1);
    self->m_lineEdit = spinbox_createDefaultLineEdit(self);
}

XAbstractSpinBox* XAbstractSpinBox_create_ex(XMemoryType memory,
                                             XWidget* parent,
                                             XWidgetFlags flags)
{
    XAbstractSpinBox* self =
        (XAbstractSpinBox*)XMemory_malloc(sizeof(XAbstractSpinBox), memory);
    if (!self) return NULL;
    XAbstractSpinBox_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 属性（对标 QAbstractSpinBox public API） ==================== */

XLineEdit* XAbstractSpinBox_lineEdit(const XAbstractSpinBox* self)
{
    return self ? self->m_lineEdit : NULL;
}

/** @brief 替换内嵌编辑框（对标 QAbstractSpinBox::setLineEdit）。
 *  @note  不变量警示（缺陷⑥ 2026-10-02 定版）：日期时间家族
 *         （XDateTimeEdit/XDateEdit/XTimeEdit）不得经此换编辑框——
 *         XDateTimeEdit_init 在 init 链上对默认编辑框置鼠标穿透
 *         （TransparentForMouseEvents）+NoFocus+反向焦点代理（编辑框
 *         →壳），配合壳 WA14+DigitsOnly 构成「字段直点→notifyPress
 *         穿透上溯到壳→Digits 数字盘」整链；经此换上的新编辑框三样
 *         全无（hints=ImhNone），childAt 重新命中编辑框、vtable 短路
 *         放行（xkb_supportedTarget 行编辑分支）直弹 QWERTY，缺陷⑥
 *         复发。当前全仓库无调用点（仅 XSpinBox 宏别名），留作契约
 *         锚点。 */
void XAbstractSpinBox_setLineEdit(XAbstractSpinBox* self, XLineEdit* lineEdit)
{
    if (!self) return;
    if (lineEdit == self->m_lineEdit) return;
    if (self->m_lineEdit) {
        /* 旧编辑框可能在自身按键/输入法回调链中被换；延迟回收
           （对标 Qt QAbstractSpinBox::setLineEdit）。 */
        XObject_deleteLater((XObject*)self->m_lineEdit);
    }
    self->m_lineEdit = lineEdit;
    if (!self->m_lineEdit)
        self->m_lineEdit = spinbox_createDefaultLineEdit(self);
    else {
        /* 对标 Qt QAbstractSpinBox::setLineEdit 的
           d->edit->setFocusProxy(q)：替换编辑框后焦点代理跟迁，
           保持「容器 hasFocus ⇔ 编辑框持焦」判据（与
           spinbox_createDefaultLineEdit 注册点同口径）。 */
        spinbox_syncEditFieldGeometry(self);
        XWidget_setFocusProxy((XWidget*)self, (XWidget*)self->m_lineEdit);
    }
}

int XAbstractSpinBox_buttonSymbols(const XAbstractSpinBox* self)
{
    return self ? self->m_buttonSymbols
                : XAbstractSpinBoxButtonSymbols_UpDownArrows;
}

void XAbstractSpinBox_setButtonSymbols(XAbstractSpinBox* self, int symbols)
{
    if (!self || self->m_buttonSymbols == symbols) return;
    if (symbols != XAbstractSpinBoxButtonSymbols_NoButtons &&
        symbols != XAbstractSpinBoxButtonSymbols_UpDownArrows &&
        symbols != XAbstractSpinBoxButtonSymbols_PlusMinus)
        return;
    self->m_buttonSymbols = (uint8_t)symbols;
    XWidget_update((XWidget*)self);
    /* 编辑框几何与绘制同源（样式 SC_SpinBoxEditField；NoButtons 全宽）。 */
    spinbox_syncEditFieldGeometry(self);
    /* 对标 Qt setButtonSymbols（qabstractspinbox.cpp:177-185）：改符号后
     * 通知布局重新协商（updateGeometry）。注意 Qt 此处**不清**尺寸缓存
     * （缓存只在字体/样式/量程/前后缀/特殊值变化时失效）——布局再协商时
     * sizeHint 返回的仍是旧符号下算出的缓存值，Qt 行为原样保留。 */
    XWidget_updateGeometry((XWidget*)self);
}

void XAbstractSpinBox_setCorrectionMode(XAbstractSpinBox* self, int mode)
{
    if (!self) return;
    if (mode != XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue &&
        mode != XAbstractSpinBoxCorrectionMode_CorrectToNearestValue)
        return;
    self->m_correctionMode = (uint8_t)mode;
}

int XAbstractSpinBox_correctionMode(const XAbstractSpinBox* self)
{
    return self ? self->m_correctionMode
                : XAbstractSpinBoxCorrectionMode_CorrectToPreviousValue;
}

bool XAbstractSpinBox_hasAcceptableInput(const XAbstractSpinBox* self)
{
    const char* text;
    int pos = 0;
    if (!self || !self->m_lineEdit) return false;
    text = XLineEdit_text(self->m_lineEdit);
    return XAbstractSpinBox_validate_base((XAbstractSpinBox*)self,
                                          text, &pos) ==
           XValidatorState_Acceptable;
}

const char* XAbstractSpinBox_text(const XAbstractSpinBox* self)
{
    if (!self || !self->m_lineEdit) return "";
    return XLineEdit_text(self->m_lineEdit);
}

const char* XAbstractSpinBox_specialValueText(const XAbstractSpinBox* self)
{
    const char* text;
    if (!self || !self->m_specialValueText) return "";
    text = XString_toUtf8(self->m_specialValueText);
    return text ? text : "";
}

void XAbstractSpinBox_setSpecialValueText(XAbstractSpinBox* self,
                                          const char* text)
{
    if (!self) return;
    if (!text) text = "";
    {
        const char* cur = self->m_specialValueText
            ? XString_toUtf8(self->m_specialValueText) : NULL;
        if (cur && XStrcmp(cur, text) == 0) return;
        if (!cur && text[0] == '\0') return;
    }
    if (!self->m_specialValueText)
        self->m_specialValueText = XString_create();
    if (self->m_specialValueText)
        XString_assign_utf8(self->m_specialValueText, text);
    /* 对标 Qt setSpecialValueText（qabstractspinbox.cpp:246）：只失效
     * sizeHint 缓存（minimumSizeHint 不量特殊值文本，注释原文
     * "minimumSizeHint doesn't care about specialValueText"），且不发
     * updateGeometry——布局通知 Qt 也不发，下次协商自然重算。 */
    spinbox_invalidateSizeHints(self, false);
    /* 值==minimum 时显示刷新（经 UpdateEdit 虚槽）。 */
    XAbstractSpinBox_updateEdit_base(self);
}

bool XAbstractSpinBox_wrapping(const XAbstractSpinBox* self)
{
    return self ? self->m_wrapping : false;
}

void XAbstractSpinBox_setWrapping(XAbstractSpinBox* self, bool wrapping)
{
    if (self) self->m_wrapping = wrapping;
}

bool XAbstractSpinBox_isReadOnly(const XAbstractSpinBox* self)
{
    return self && self->m_lineEdit
        ? XLineEdit_isReadOnly(self->m_lineEdit) : false;
}

void XAbstractSpinBox_setReadOnly(XAbstractSpinBox* self, bool readOnly)
{
    if (self && self->m_lineEdit)
        XLineEdit_setReadOnly(self->m_lineEdit, readOnly);
}

bool XAbstractSpinBox_keyboardTracking(const XAbstractSpinBox* self)
{
    return self ? self->m_keyboardTracking : true;
}

void XAbstractSpinBox_setKeyboardTracking(XAbstractSpinBox* self,
                                          bool tracking)
{
    if (self) self->m_keyboardTracking = tracking;
}

int XAbstractSpinBox_alignment(const XAbstractSpinBox* self)
{
    return self && self->m_lineEdit
        ? XLineEdit_alignment(self->m_lineEdit) : 0;
}

void XAbstractSpinBox_setAlignment(XAbstractSpinBox* self, int alignment)
{
    if (self && self->m_lineEdit)
        XLineEdit_setAlignment(self->m_lineEdit, alignment);
}

bool XAbstractSpinBox_hasFrame(const XAbstractSpinBox* self)
{
    return self ? self->m_frame : true;
}

void XAbstractSpinBox_setFrame(XAbstractSpinBox* self, bool on)
{
    /* 对标 Qt setFrame（qabstractspinbox.cpp:355-361）：置位后立即重绘 +
     * 重排内嵌编辑框几何（EditField 边距按 PM_SpinBoxFrameWidth 收放，
     * frame 关→编辑区吃满全宽；此前缺同步，边框变化要等下一次 RESIZE
     * 才可见；XSpinBox 转发层用逗号宏补的 update 已并入此处基类 setter，
     * 其它子类直呼基类亦生效）。Qt 同样不清尺寸缓存。 */
    if (!self || self->m_frame == on) return;
    self->m_frame = on;
    XWidget_update((XWidget*)self);
    spinbox_syncEditFieldGeometry(self);
}

void XAbstractSpinBox_setAccelerated(XAbstractSpinBox* self, bool on)
{
    if (self) self->m_accelerated = on;
}

bool XAbstractSpinBox_isAccelerated(const XAbstractSpinBox* self)
{
    return self ? self->m_accelerated : false;
}

void XAbstractSpinBox_setGroupSeparatorShown(XAbstractSpinBox* self,
                                             bool shown)
{
    if (!self || self->m_groupSeparatorShown == shown) return;
    self->m_groupSeparatorShown = shown;
    /* 显示文本刷新（经 UpdateEdit 虚槽）。 */
    XAbstractSpinBox_updateEdit_base(self);
}

bool XAbstractSpinBox_isGroupSeparatorShown(const XAbstractSpinBox* self)
{
    return self ? self->m_groupSeparatorShown : false;
}

void XAbstractSpinBox_interpretText(XAbstractSpinBox* self)
{
    if (!self) return;
    XAbstractSpinBox_interpret_base(self);
}

/* ==================== 公共槽 ==================== */

void XAbstractSpinBox_stepUp(XAbstractSpinBox* self)
{
    XAbstractSpinBox_stepBy_base(self, 1);
}

void XAbstractSpinBox_stepDown(XAbstractSpinBox* self)
{
    XAbstractSpinBox_stepBy_base(self, -1);
}

void XAbstractSpinBox_selectAll(XAbstractSpinBox* self)
{
    if (self && self->m_lineEdit)
        XLineEdit_selectAll(self->m_lineEdit);
}

/* ==================== 步进按钮长按连发 ==================== */

void XAbstractSpinBox_spinButtonPress(XAbstractSpinBox* self, bool up)
{
    uint8_t dir = (uint8_t)(up ? XAbstractSpinBoxButtonState_Up
                               : XAbstractSpinBoxButtonState_Down);
    int flags;
    if (!self) return;
    /* 对标 Qt updateState：同向已按住 ⇒ 幂等返回。 */
    if (self->m_buttonState & dir) return;
    spinbox_resetButtons(self);
    flags = XAbstractSpinBox_stepEnabled_base(self);
    if (!(flags & (up ? XAbstractSpinBoxStepEnabledFlag_StepUpEnabled
                      : XAbstractSpinBoxStepEnabledFlag_StepDownEnabled)))
        return;
    self->m_buttonState = (uint8_t)(dir |
                                    XAbstractSpinBoxButtonState_Mouse);
    XWidget_update((XWidget*)self);
    spinbox_repeatStep(self, up);
    self->m_effectiveSpinRepeatRate = spinbox_repeatRate(self);
    self->m_acceleration = 0;
    self->m_spinClickThresholdTimerId = XObject_startTimer_ms(
        (XObject*)self, (uint64_t)spinbox_repeatThreshold(self),
        XTimerType_CoarseTimer);
}

void XAbstractSpinBox_spinButtonRelease(XAbstractSpinBox* self)
{
    if (!self) return;
    spinbox_resetButtons(self);
}

bool XAbstractSpinBox_isSpinButtonActive(const XAbstractSpinBox* self)
{
    return self &&
           (self->m_buttonState &
            (uint8_t)XAbstractSpinBoxButtonState_ButtonMask) != 0;
}

int XAbstractSpinBox_buttonState(const XAbstractSpinBox* self)
{
    return self ? (int)self->m_buttonState
                : (int)XAbstractSpinBoxButtonState_None;
}

void XAbstractSpinBox_setStepModifierDown(XAbstractSpinBox* self, bool down)
{
    if (self) self->m_stepModifierDown = down;
}

void XAbstractSpinBox_clear(XAbstractSpinBox* self)
{
    /* 对标 QAbstractSpinBox::clear 公共虚槽（qabstractspinbox.cpp:510-
     * 517）：经 EXAbstractSpinBox_Clear 虚槽分派，基类默认清空全部文本并
     * 置内部 cleared 标志；XSpinBox 保留前缀/后缀，XDateTimeEdit 只清
     * 当前分段（两者的 Override 与 Qt 完全同构）。 */
    XAbstractSpinBox_clear_base(self);
}

/* ==================== 虚槽调度入口（*_base） ==================== */

void XAbstractSpinBox_stepBy_base(XAbstractSpinBox* self, int steps)
{
    if (ISNULL(self, "XAbstractSpinBox") ||
        ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    XClassGetVirtualFunc(self, EXAbstractSpinBox_StepBy,
                         void(*)(XAbstractSpinBox*, int))(self, steps);
}

XValidatorState XAbstractSpinBox_validate_base(XAbstractSpinBox* self,
                                               const char* input, int* pos)
{
    if (ISNULL(self, "XAbstractSpinBox") ||
        ISNULL(XClassGetVtable(self), "Vtable"))
        return XValidatorState_Invalid;
    return XClassGetVirtualFunc(self, EXAbstractSpinBox_Validate,
                                XValidatorState(*)(XAbstractSpinBox*,
                                                   const char*, int*))(
        self, input, pos);
}

void XAbstractSpinBox_fixup_base(XAbstractSpinBox* self, char* input,
                                 size_t capacity)
{
    if (ISNULL(self, "XAbstractSpinBox") ||
        ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_Fixup, bool)) return;
    XClassGetVirtualFunc(self, EXAbstractSpinBox_Fixup,
                         void(*)(XAbstractSpinBox*, char*, size_t))(
        self, input, capacity);
}

void XAbstractSpinBox_clear_base(XAbstractSpinBox* self)
{
    if (ISNULL(self, "XAbstractSpinBox") ||
        ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_Clear, bool)) return;
    XClassGetVirtualFunc(self, EXAbstractSpinBox_Clear,
                         void(*)(XAbstractSpinBox*))(self);
}

int XAbstractSpinBox_stepEnabled_base(XAbstractSpinBox* self)
{
    if (ISNULL(self, "XAbstractSpinBox") ||
        ISNULL(XClassGetVtable(self), "Vtable"))
        return XAbstractSpinBoxStepEnabledFlag_StepNone;
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_StepEnabled, bool))
        return XAbstractSpinBoxStepEnabledFlag_StepNone;
    return XClassGetVirtualFunc(self, EXAbstractSpinBox_StepEnabled,
                                int(*)(XAbstractSpinBox*))(self);
}

void XAbstractSpinBox_interpret_base(XAbstractSpinBox* self)
{
    if (ISNULL(self, "XAbstractSpinBox") ||
        ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_Interpret, bool))
        return;
    XClassGetVirtualFunc(self, EXAbstractSpinBox_Interpret,
                         void(*)(XAbstractSpinBox*))(self);
}

void XAbstractSpinBox_updateEdit_base(XAbstractSpinBox* self)
{
    if (ISNULL(self, "XAbstractSpinBox") ||
        ISNULL(XClassGetVtable(self), "Vtable"))
        return;
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_UpdateEdit, bool))
        return;
    XClassGetVirtualFunc(self, EXAbstractSpinBox_UpdateEdit,
                         void(*)(XAbstractSpinBox*))(self);
}

const char* XAbstractSpinBox_prefixText_base(const XAbstractSpinBox* self)
{
    const char* text;
    if (!self || !XClassGetVtable(self)) return "";
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_PrefixText, bool))
        return "";
    text = XClassGetVirtualFunc(self, EXAbstractSpinBox_PrefixText,
                                const char*(*)(const XAbstractSpinBox*))(
        self);
    return text ? text : "";
}

const char* XAbstractSpinBox_suffixText_base(const XAbstractSpinBox* self)
{
    const char* text;
    if (!self || !XClassGetVtable(self)) return "";
    if (!XClassGetVirtualFunc(self, EXAbstractSpinBox_SuffixText, bool))
        return "";
    text = XClassGetVirtualFunc(self, EXAbstractSpinBox_SuffixText,
                                const char*(*)(const XAbstractSpinBox*))(
        self);
    return text ? text : "";
}

/* ==================== 信号 ==================== */

void* XAbstractSpinBox_editingFinished_signal(XAbstractSpinBox* self)
{
    return (void*)(size_t)XAbstractSpinBox_editingFinished_signal;
}

/* ==================== 尺寸提示族共用预算 ==================== */

int XAbstractSpinBox_frameWidthFor(const XAbstractSpinBox* self)
{
    XStyle* st = NULL;
    if (!self) return 1;
#if XSTYLE_ON
    st = XWidget_style((const XWidget*)self);
    if (st) {
        XStyleOption opt;
        int fw;
        XStyleOption_init(&opt, XStyleCT_SpinBox);
        XRect_init(&opt.m_rect, 0, 0, 0, 0);
        opt.m_spinFrame = self->m_frame;
        fw = XStyle_pixelMetric(st, XStylePM_SpinBoxFrameWidth, &opt);
        return fw >= 0 ? fw : 1;
    }
#endif /* XSTYLE_ON */
    (void)st;
    return 1;
}

int XAbstractSpinBox_hintWidthFor(const XAbstractSpinBox* self,
                                  int probeWidth)
{
    XStyle* st;
    XLineEdit* edit;
    int fw;
    int hEff;
    int w;
    int i;
    if (!self) return probeWidth > 0 ? probeWidth : 0;
    if (probeWidth < 0) probeWidth = 0;
    fw = XAbstractSpinBox_frameWidthFor(self);
    /* 高度基线：编辑框提示高夹进 min/max 高度约束——列宽公式
       （bh=h/2−fw）吃的是**控件最终高度**，固定高控件（HSF 各页
       setFixedHeight(40)）必须按约束高预算，否则列宽少 4~6px、
       首位数字被步进列压掉（走查实锅）。 */
    edit = self->m_lineEdit;
    hEff = edit ? XWidget_sizeHint((XWidget*)edit).height : 14;
    if (hEff <= 0) hEff = 14;
    {
        XSize mn = XWidget_minimumSize((const XWidget*)self);
        XSize mx = XWidget_maximumSize((const XWidget*)self);
        if (mn.height > hEff) hEff = mn.height;
        if (mx.height < XWIDGET_MAX_SIZE && mx.height > 0 &&
            hEff > mx.height)
            hEff = mx.height;
    }
    if (self->m_buttonSymbols ==
        (uint8_t)XAbstractSpinBoxButtonSymbols_NoButtons)
        return probeWidth + 2 * fw;
#if XSTYLE_ON
    /* 不动点迭代：列宽取样式 subControlRect 实量（与绘制同一条
       xcs_spinBoxColumn 几何），w/4 上限使列随宽长——迭代 4 轮必收敛
       （列增长 ≤ Δw/4，几何级数收缩）。 */
    st = XWidget_style((const XWidget*)self);
    if (st) {
        w = probeWidth + 16 + 2 * fw; /* 起步：16px 最小列（同公式的下限）。 */
        for (i = 0; i < 4; ++i) {
            XStyleOption opt;
            XRect rUp;
            int colW;
            int nw;
            XStyleOption_init(&opt, XStyleCT_SpinBox);
            XRect_init(&opt.m_rect, 0, 0, w, hEff);
            opt.m_spinFrame = self->m_frame;
            opt.m_spinSymbols = self->m_buttonSymbols;
            rUp = XStyle_subControlRect(st, XStyleCC_SpinBox, &opt,
                                        XStyleSC_SpinBoxUp, NULL);
            colW = rUp.width;
            if (colW < 16) colW = 16;
            nw = probeWidth + colW + 2 * fw;
            if (nw == w) break;
            w = nw;
        }
        return w;
    }
#else
    (void)hEff;
#endif /* XSTYLE_ON */
    return probeWidth + 16 + 2 * fw;
}


























#endif /* XWIDGET_ON && XABSTRACTSPINBOX_ON && XLINEEDIT_ON */

#endif /* XGUI_ON */
