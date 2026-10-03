/**
 * @file       XLineEdit.c
 * @brief      XLineEdit 单行编辑控件实现（对标 Qt 6.8 QLineEdit 全部公共
 *             API；壳-控制器架构）。
 * @details    对标 Qt 6.8 QLineEdit 持私有控制器 QWidgetLineControl 的
 *             关系：文本缓冲/光标与选区/撤销栈/回显状态机（含密码回显
 *             定时器）/输入掩码/校验与 fixup/IME/剪贴板/命中测试/键盘
 *             分派/补全联动/文本区绘制数据全部迁入 m_control 指向的
 *             XLineControl（Src/XGui/Text/XLineControl.{c,h}）；本壳
 *             只保留：
 *             - frame/面板/焦点框绘制与内置 action、清除按钮（side
 *               widget 体系，对标 QLineEditPrivate）；
 *             - sizeHint/minimumSizeHint（布局职责属壳）；
 *             - 失焦 editingFinished 门禁（m_finishedPending，对标
 *               Qt d->edited && (hasAcceptableInput||fixup())）；
 *             - 水平滚动偏移 m_viewOffset 的钳位（光标→X 取控制器
 *               cursorToX，像素守恒留壳）；
 *             - 上下文菜单弹出与生命周期（菜单动作槽指控制器操作）；
 *             - 平台层 IME 直投的全局焦点登记 g_focusedLineEdit；
 *             - 信号转发：控制器 textChanged/textEdited/
 *               cursorPositionChanged/selectionChanged/inputRejected/
 *               accepted/editingFinished 唯一发射点经壳转接为公开信号。
 *             事件入口：keyPressEvent → 控制器 processKeyEvent；
 *             inputMethodEvent → 控制器 processInputMethodEvent；鼠标
 *             命中的坐标平移（边框/边距/action 区/滚动偏移）在壳，命中
 *             后调控制器 xToPos/moveCursor。绘制：paintEvent 先从控件
 *             调色板取四色经 XLineControl_setPalette 注入控制器，再调
 *             XLineControl_draw 完成正文/选区/光标绘制；占位提示留壳。
 *             密码回显宿主判定：控制器无 widget 身份，壳在 focusIn/
 *             focusOut 把焦点状态经 updatePasswordEchoEditing 推送给控
 *             制器（对标 Qt updatePasswordEchoEditing 编排），光标常显
 *             以 updateCursorBlinking 置位（不启用闪烁定时器，行为与
 *             迁移前一致）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#if XWIDGET_ON && XLINEEDIT_ON

#include "XLineEdit.h"
#include "XStyle.h"
#include "XStyleOption.h"
#if XSTYLEHINTS_ON && XGUIAPPLICATION_ON
#include "XStyleHints.h"
#endif /* XSTYLEHINTS_ON && XGUIAPPLICATION_ON */
#include "XWidget_Protected.h"
#if XWINDOWEVENT_ON
#include "XWindowEvent.h"
#endif /* XWINDOWEVENT_ON */
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XString.h"
#include "XClipboard.h"
#include "XGuiApplication.h"
#include "XTextClipboard.h"
#if XINPUTMETHOD_ON
#include "XVariant.h"
#endif /* XINPUTMETHOD_ON */
#if XMENU_ON
#include "XMenu.h"
#include "XTextMenu.h"
#endif /* XMENU_ON */
#include "XColor.h"
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */
/* 补全器与 XAbstractItemModel 同受 XTABLEWIDGET_ON 门控；关闭时不接线。 */
#if XTABLEWIDGET_ON
#include "XCompleter.h"
#endif /* XTABLEWIDGET_ON */

/** @brief 当前聚焦的 XLineEdit（全局；IME CommitString 直投目标）。 */
static XLineEdit* g_focusedLineEdit = NULL;

/* 光标竖线宽度与文本估算度量（每字符 8px，与点阵默认一致） */
#define XLINEEDIT_CURSOR_W 1
#define XLINEEDIT_CHAR_W   8
/* 内置 action 图标区宽度 */
#define XLINEEDIT_ACTION_W 16
/** @brief 控制器侧「不限制长度」上限（对标 Qt 构造默认 32767；壳 API
 *         以 0 表示不限，两者在壳的委托层换算）。 */
#define XLINEEDIT_UNLIMITED_MAX_LENGTH 32767
/** @brief 三击窗口的回退时长/距离（XStyleHints 不可用时取 Qt 默认
 *         doubleClickInterval=400ms、startDragDistance=10px）。 */
#define XLINEEDIT_TRIPLE_CLICK_MS   400
#define XLINEEDIT_TRIPLE_CLICK_DIST 10
/** @brief 行编辑内边距（对标 QLineEditPrivate::verticalMargin=1/
 *         horizontalMargin=2，qlineedit_p.cpp:34-35）。 */
#define XLINEEDIT_VERTICAL_MARGIN   1
#define XLINEEDIT_HORIZONTAL_MARGIN 2

/* ==================== 前向声明 ==================== */
static void  VXLineEdit_keyPressEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_inputMethodEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_keyReleaseEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_mousePressEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_mouseReleaseEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_mouseMoveEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_mouseDoubleClickEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_focusInEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_focusOutEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_paintEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_changeEvent(XWidget* self, XEvent* event);
static void  VXLineEdit_timerEvent(XObject* object, XTimerEvent* event);
static void  VXLineEdit_deinit(XLineEdit* self);
static void  VXLineEdit_copy(XLineEdit* self, const XLineEdit* other);
static void  VXLineEdit_move(XLineEdit* self, XLineEdit* other);
static void  xlineedit_updateSizeHints(XLineEdit* self);
static void  xlineedit_updateViewOffset(XLineEdit* self);

/* ==================== 内部辅助 ==================== */

/** @brief 取编辑缓冲原文（控制器 surroundingText；字节↔字符换算基准）。 */
static const char* xlineedit_ctlRawText(const XLineEdit* self)
{
    if (!self || !self->m_control) return "";
    return XLineControl_surroundingText(self->m_control);
}

/** @brief 取指定角色颜色为 ARGB32；无调色板能力时回退纯黑。 */
static uint32_t xlineedit_color(const XLineEdit* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/** @brief UTF-8 字符串的字符数（字节长度不含续字节）。 */
static size_t xlineedit_charCount(const char* utf8)
{
    size_t chars = 0;
    if (!utf8) return 0;
    while (*utf8) {
        ++utf8;
        while ((*utf8 & 0xC0u) == 0x80u) ++utf8; /* 跳过 10xxxxxx 续字节 */
        ++chars;
    }
    return chars;
}

/** @brief UTF-8 文本前 byteLen 字节内的字符数（byteLen 须为字符边界）。 */
static size_t xlineedit_charCountPrefix(const char* text, size_t byteLen)
{
    size_t chars = 0;
    size_t i = 0;
    if (!text) return 0;
    while (i < byteLen && text[i]) {
        ++i;
        while (i < byteLen && ((unsigned char)text[i] & 0xC0u) == 0x80u) ++i;
        ++chars;
    }
    return chars;
}

/** @brief 字符索引 → UTF-8 字节偏移（公开 API 字符索引口径的换算层）。 */
static size_t xlineedit_charIndexToByte(const char* text, size_t charIndex)
{
    size_t byte = 0;
    size_t i = 0;
    if (!text) return 0;
    while (text[byte] && i < charIndex) {
        ++byte;
        while (((unsigned char)text[byte] & 0xC0u) == 0x80u) ++byte;
        ++i;
    }
    return byte;
}

/** @brief 计算显示文本前 charCount 个字符的真实像素宽度。
 * @details 与 XPainter_drawText 使用同一字体度量
 *          （XPainter_textWidthRange 逐字符累计）：中文等双宽字符
 *          按真实字形宽计算，西文按单宽。 */
static int xlineedit_displayWidth(const XFont* font, const char* display,
                                  size_t charCount)
{
    int width = 0;
    size_t byte = 0;
    size_t i;
    if (!font || !display) return 0;
    for (i = 0; i < charCount && display[byte]; ++i) {
        size_t next = byte;
        int charW;
        ++next;
        while ((display[next] & 0xC0u) == 0x80u) ++next;
        charW = XPainter_textWidthRange(font, display, (int)byte, (int)next);
        if (charW > 0) width += charW;
        byte = next;
    }
    return width;
}

/** @brief 回显模式 → 输入法提示位分流（对标 QLineEdit::setEchoMode 的
 *         imHints 调整，qlineedit.cpp:551-556）：Password/NoEcho 置
 *         HiddenText；非 Normal 置 NoAutoUppercase/NoPredictiveText/
 *         SensitiveData。 */
static XInputMethodHints xlineedit_echoImHints(XInputMethodHints hints,
                                               int echoMode)
{
    bool hidden = (echoMode == (int)XLineEditEchoMode_Password ||
                   echoMode == (int)XLineEditEchoMode_NoEcho);
    bool plain = (echoMode == (int)XLineEditEchoMode_Normal);
    if (hidden)
        hints |= (XInputMethodHints)XInputMethodHint_HiddenText;
    else
        hints &= ~(XInputMethodHints)XInputMethodHint_HiddenText;
    if (!plain)
        hints |= (XInputMethodHints)(XInputMethodHint_NoAutoUppercase |
                                     XInputMethodHint_NoPredictiveText |
                                     XInputMethodHint_SensitiveData);
    else
        hints &= ~(XInputMethodHints)(XInputMethodHint_NoAutoUppercase |
                                      XInputMethodHint_NoPredictiveText |
                                      XInputMethodHint_SensitiveData);
    return hints;
}

/** @brief 发射 const char* 参数信号（textChanged/textEdited）。 */
static void xlineedit_emitTextSignal(XLineEdit* self, size_t signal)
{
    XVarList* arguments;
    const char* text;
    if (!self || !self->m_control) return;
    text = XLineControl_text(self->m_control);
    arguments = XVarList_Create(XVar(const char*, text));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射 void 信号（returnPressed/editingFinished/selectionChanged/
 *         inputRejected）。 */
static void xlineedit_emitVoidSignal(XLineEdit* self, size_t signal)
{
    XVarList* arguments = XVarList_create(0);
    if (!arguments) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射 cursorPositionChanged(int,int) 信号。 */
static void xlineedit_emitCursorPosSignal(XLineEdit* self, int oldPos,
                                          int newPos)
{
    XVarList* arguments = XVarList_Create(XVar(int, oldPos),
                                          XVar(int, newPos));
    if (!arguments) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XLineEdit_cursorPositionChanged_signal,
                           arguments, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/* ==================== 控制器信号转发（发射点唯一） ==================== */

/**
 * @brief 控制器 textChanged → 壳 textChanged 转发 + 滚动/尺寸提示/重绘
 *        联动（对标原 setContent 的提交收尾，迁移后由信号驱动）。
 */
static void xlineedit_ctlTextChanged(XObject* receiver, XVarList* args)
{
    XLineEdit* self = (XLineEdit*)receiver;
    (void)args;
    if (!self || !self->m_control) return;
    xlineedit_emitTextSignal(self, (size_t)XLineEdit_textChanged_signal);
    xlineedit_updateViewOffset(self);
    xlineedit_updateSizeHints(self);
    XWidget_update((XWidget*)self);
}

/**
 * @brief 控制器 textEdited → 壳 textEdited 转发 + 置失焦待发标志。
 * @details 控制器 textEdited 仅在用户编辑结算（insert/backspace/del/
 *        removeSelection/paste/IME 提交等 edited 收尾且文本确变）时发射，
 *        与迁移前 setContent(userEdited=true) 的置位点集合一致：在此置
 *        m_finishedPending（对标 Qt 壳侧 d->edited 置位），供失焦
 *        editingFinished 门禁消费（审计 5.2 失焦门禁行为不变硬约束）。
 */
static void xlineedit_ctlTextEdited(XObject* receiver, XVarList* args)
{
    XLineEdit* self = (XLineEdit*)receiver;
    (void)args;
    if (!self || !self->m_control) return;
    self->m_finishedPending = true;
    xlineedit_emitTextSignal(self, (size_t)XLineEdit_textEdited_signal);
}

/** @brief 控制器 cursorPositionChanged → 壳 cursorPositionChanged 转发 +
 *        滚动跟随/重绘联动。 */
static void xlineedit_ctlCursorPositionChanged(XObject* receiver,
                                               XVarList* args)
{
    XLineEdit* self = (XLineEdit*)receiver;
    int oldPos = 0;
    int newPos = 0;
    if (!self || !self->m_control) return;
    if (args) {
        oldPos = XVarList_arg(args, int);
        newPos = XVarList_arg(args, int);
    }
    xlineedit_emitCursorPosSignal(self, oldPos, newPos);
    xlineedit_updateViewOffset(self);
    XWidget_update((XWidget*)self);
}

/** @brief 控制器 selectionChanged → 壳 selectionChanged 转发 + 重绘联动。 */
static void xlineedit_ctlSelectionChanged(XObject* receiver, XVarList* args)
{
    XLineEdit* self = (XLineEdit*)receiver;
    (void)args;
    if (!self || !self->m_control) return;
    xlineedit_emitVoidSignal(self,
                             (size_t)XLineEdit_selectionChanged_signal);
    xlineedit_updateViewOffset(self);
    XWidget_update((XWidget*)self);
}

/** @brief 控制器 inputRejected → 壳 inputRejected 转发（拒绝发射点唯一）。 */
static void xlineedit_ctlInputRejected(XObject* receiver, XVarList* args)
{
    XLineEdit* self = (XLineEdit*)receiver;
    (void)args;
    if (!self || !self->m_control) return;
    xlineedit_emitVoidSignal(self,
                             (size_t)XLineEdit_inputRejected_signal);
}

/** @brief 控制器 accepted（Return 提交）→ 壳 returnPressed 转发
 *        （对标 Qt QLineEdit 把 control 的 accepted 接为 returnPressed）。 */
static void xlineedit_ctlAccepted(XObject* receiver, XVarList* args)
{
    XLineEdit* self = (XLineEdit*)receiver;
    (void)args;
    if (!self || !self->m_control) return;
    xlineedit_emitVoidSignal(self,
                             (size_t)XLineEdit_returnPressed_signal);
}

/** @brief 控制器 editingFinished → 壳 editingFinished 转发，并复位壳的
 *        失焦待发标志（Return 与失焦两路共用一个公开信号）。 */
static void xlineedit_ctlEditingFinished(XObject* receiver, XVarList* args)
{
    XLineEdit* self = (XLineEdit*)receiver;
    (void)args;
    if (!self || !self->m_control) return;
    self->m_finishedPending = false;
    xlineedit_emitVoidSignal(self,
                             (size_t)XLineEdit_editingFinished_signal);
}

/** @brief 控制器 displayTextChanged（回显/掩码显示刷新）→ 重绘。 */
static void xlineedit_ctlDisplayTextChanged(XObject* receiver, XVarList* args)
{
    XLineEdit* self = (XLineEdit*)receiver;
    (void)args;
    if (!self || !self->m_control) return;
    XWidget_update((XWidget*)self);
}

/** @brief 控制器 updateNeeded（光标闪烁相位等局部重绘请求）→ 重绘。 */
static void xlineedit_ctlUpdateNeeded(XObject* receiver, XVarList* args)
{
    XLineEdit* self = (XLineEdit*)receiver;
    (void)args;
    if (!self || !self->m_control) return;
    XWidget_update((XWidget*)self);
}

/** @brief 建立控制器 → 壳的信号转发连接（receiverShell 为信号接收方，
 *         move 语义转移控制器后需重连）。 */
static void xlineedit_connectControlSignals(XLineEdit* self,
                                            XLineEdit* receiverShell)
{
    XLineControl* ctl;
    XObject* receiver;
    XRect empty;
    if (!self || !self->m_control || !receiverShell) return;
    ctl = self->m_control;
    receiver = (XObject*)receiverShell;
    XRect_init(&empty, 0, 0, 0, 0);
    XObject_connect_1((XObject*)ctl,
                      (size_t)XLineControl_textChanged_signal(ctl, NULL),
                      receiver, xlineedit_ctlTextChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)ctl,
                      (size_t)XLineControl_textEdited_signal(ctl, NULL),
                      receiver, xlineedit_ctlTextEdited,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)ctl,
                      (size_t)XLineControl_cursorPositionChanged_signal(
                          ctl, 0, 0),
                      receiver, xlineedit_ctlCursorPositionChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)ctl,
                      (size_t)XLineControl_selectionChanged_signal(ctl),
                      receiver, xlineedit_ctlSelectionChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)ctl,
                      (size_t)XLineControl_inputRejected_signal(ctl),
                      receiver, xlineedit_ctlInputRejected,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)ctl,
                      (size_t)XLineControl_accepted_signal(ctl),
                      receiver, xlineedit_ctlAccepted,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)ctl,
                      (size_t)XLineControl_editingFinished_signal(ctl),
                      receiver, xlineedit_ctlEditingFinished,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)ctl,
                      (size_t)XLineControl_displayTextChanged_signal(ctl,
                                                                     NULL),
                      receiver, xlineedit_ctlDisplayTextChanged,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)ctl,
                      (size_t)XLineControl_updateNeeded_signal(ctl, empty),
                      receiver, xlineedit_ctlUpdateNeeded,
                      XConnectionType_Direct);
}

/** @brief 断开控制器 → receiverShell 的全部转发连接（move 转移前调用）。 */
static void xlineedit_disconnectControlSignals(XLineEdit* self,
                                               XLineEdit* receiverShell)
{
    XLineControl* ctl;
    XObject* receiver;
    XRect empty;
    if (!self || !self->m_control || !receiverShell) return;
    ctl = self->m_control;
    receiver = (XObject*)receiverShell;
    XRect_init(&empty, 0, 0, 0, 0);
    XObject_disconnect_1((XObject*)ctl,
                         (size_t)XLineControl_textChanged_signal(ctl, NULL),
                         receiver, xlineedit_ctlTextChanged);
    XObject_disconnect_1((XObject*)ctl,
                         (size_t)XLineControl_textEdited_signal(ctl, NULL),
                         receiver, xlineedit_ctlTextEdited);
    XObject_disconnect_1((XObject*)ctl,
                         (size_t)XLineControl_cursorPositionChanged_signal(
                             ctl, 0, 0),
                         receiver, xlineedit_ctlCursorPositionChanged);
    XObject_disconnect_1((XObject*)ctl,
                         (size_t)XLineControl_selectionChanged_signal(ctl),
                         receiver, xlineedit_ctlSelectionChanged);
    XObject_disconnect_1((XObject*)ctl,
                         (size_t)XLineControl_inputRejected_signal(ctl),
                         receiver, xlineedit_ctlInputRejected);
    XObject_disconnect_1((XObject*)ctl,
                         (size_t)XLineControl_accepted_signal(ctl),
                         receiver, xlineedit_ctlAccepted);
    XObject_disconnect_1((XObject*)ctl,
                         (size_t)XLineControl_editingFinished_signal(ctl),
                         receiver, xlineedit_ctlEditingFinished);
    XObject_disconnect_1((XObject*)ctl,
                         (size_t)XLineControl_displayTextChanged_signal(ctl,
                                                                        NULL),
                         receiver, xlineedit_ctlDisplayTextChanged);
    XObject_disconnect_1((XObject*)ctl,
                         (size_t)XLineControl_updateNeeded_signal(ctl, empty),
                         receiver, xlineedit_ctlUpdateNeeded);
}

/* ==================== 校验适配钩子（壳回调 → 控制器委托） ==================== */

/**
 * @brief 控制器 validate 委托适配：把壳的 XLineEditValidatorFunc 桥接为
 *        XLineControlValidateFunc。validator 形参恒为壳指针（审计 5.2：
 *        壳指针生命周期覆盖控制器调用——控制器在壳 deinit 中先行销毁）。
 */
static int xlineedit_validateAdapter(void* validator, char** text,
                                     int* cursor, void* userData)
{
    XLineEdit* self = (XLineEdit*)validator;
    if (!self || !self->m_validator || !text) {
        return (int)XLineControlValidatorState_Acceptable;
    }
    (void)cursor; /* 壳回调契约不含光标出参（与迁移前一致）。 */
    return (int)self->m_validator(self, *text, userData);
}

/** @brief 控制器 fixup 委托适配：迁移前壳无 fixup 能力，恒原样返回。 */
static void xlineedit_fixupAdapter(void* validator, char** text,
                                   void* userData)
{
    (void)validator;
    (void)text;
    (void)userData;
}

/* ==================== 壳几何与滚动（viewOffset 钳位留壳） ==================== */

/** @brief 同步控件字体到控制器（深拷贝 + 重排；度量入口与绘制前调用，
 *         保证光标→X/自然宽与绘制同口径）。 */
static void xlineedit_syncControlFont(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    XLineControl_setFont(self->m_control, &((XWidget*)self)->m_font);
}

/** @brief 文本区起始 x（边框+边距+起始侧 action 区）。 */
static int xlineedit_textStartX(const XLineEdit* self)
{
    int x = (self->m_frame ? 4 : 2) + self->m_textMargins.left;
    int i;
    for (i = 0; i < (int)self->m_actionCount; ++i) {
        if (self->m_actionPositions[i] == XLineEditActionPosition_Leading)
            x += XLINEEDIT_ACTION_W;
    }
    return x;
}

/** @brief 按光标位置与对齐更新水平滚动偏移（光标→X 取控制器，钳位留壳）。
 *  @details 对标 Qt paintEvent 内联的 hscroll 结算（qlineedit.cpp:2036-
 *           2064）：文本未溢出时按水平对齐放置——Right→widthUsed−宽+1、
 *           HCenter→(widthUsed−宽)/2、Left→0；溢出时按光标位置钳位。 */
static void xlineedit_updateViewOffset(XLineEdit* self)
{
    int textStart;
    int textEnd;
    int visibleW;
    int textW;
    int cursorX;
    int lo;
    int hi;
    if (!self || !self->m_control) return;
    xlineedit_syncControlFont(self);
    textStart = xlineedit_textStartX(self);
    textEnd = XWidget_width((XWidget*)self) - (self->m_frame ? 4 : 2) -
              self->m_textMargins.right -
              ((self->m_clearButtonEnabled &&
                XLineControl_text(self->m_control)[0]) ? 18 : 0);
    if (textEnd <= textStart) textEnd = textStart + 1;
    visibleW = textEnd - textStart;
    /* 总宽/光标位用控制器布局度量（与 XLineControl_draw 渲染、xToPos
       命中同口径，中文双宽正确）。 */
    textW = XLineControl_naturalTextWidth(self->m_control);
    if (textW <= visibleW) {
        /* 文本未溢出：hscroll 由水平对齐决定（可为负，对标 Qt 的
         * AlignRight/AlignHCenter 分支；Justify 映射 Left）。 */
        int ha = self->m_alignment & XAlignment_HorizontalMask;
        if (ha & XAlignment_Right)
            self->m_viewOffset = textW - visibleW + 1;
        else if (ha & XAlignment_HCenter)
            self->m_viewOffset = (textW - visibleW) / 2;
        else
            self->m_viewOffset = 0;
        return;
    }
    cursorX = XLineControl_cursorToXCurrent(self->m_control);
    lo = cursorX - visibleW + 1;
    if (lo < 0) lo = 0;
    /* 滚动上限多留 1px 光标位（对标 Qt updateScroll 的 cursor−width+1
     * 口径）：此前 hi = textW−visibleW 时滚到行尾文本右缘恰好贴齐
     * 文本区 clip 右缘，光标竖线 rect.x == clip 右缘被
     * XLineControl_draw 的严格小于相交测试整条剔除（txt_21c 系列行尾
     * 无竖线）。多留 1px 后行尾光标落在 clip 内（textEnd−1），竖线
     * 可见；文本右缘移到 textEnd−1，无字形像素损失（拖选跟随与
     * xToPos 坐标口径同源于本函数，无需联动）。 */
    hi = textW - visibleW + 1;
    self->m_viewOffset = cursorX;
    if (self->m_viewOffset < lo) self->m_viewOffset = lo;
    if (self->m_viewOffset > hi) self->m_viewOffset = hi;
}

/** @brief 同步估算尺寸到 XWidget 尺寸提示存储并请求重布局。 */
static void xlineedit_updateSizeHints(XLineEdit* self)
{
    XSize hint;
    XSize min;
    if (!self) return;
    hint = XLineEdit_sizeHint(self);
    min = XLineEdit_minimumSizeHint(self);
    XWidget_setSizeHint((XWidget*)self, &hint);
    XWidget_setMinimumSizeHint((XWidget*)self, &min);
    XWidget_updateGeometry((XWidget*)self);
}

/** @brief 查询双击间隔 ms（对标 QApplication::doubleClickInterval；
 *         XStyleHints 不可用时回退 Qt 默认 400）。 */
static int xlineedit_doubleClickInterval(void)
{
#if XSTYLEHINTS_ON && XGUIAPPLICATION_ON
    XStyleHints* hints = XGuiApplication_styleHints();
    if (hints) {
        int ms = XStyleHints_mouseDoubleClickInterval(hints);
        if (ms > 0) return ms;
    }
#endif /* XSTYLEHINTS_ON && XGUIAPPLICATION_ON */
    return XLINEEDIT_TRIPLE_CLICK_MS;
}

/** @brief 查询拖拽启动距离（对标 QApplication::startDragDistance；
 *         XStyleHints 不可用时回退 Qt 默认 10）。 */
static int xlineedit_startDragDistance(void)
{
#if XSTYLEHINTS_ON && XGUIAPPLICATION_ON
    XStyleHints* hints = XGuiApplication_styleHints();
    if (hints) {
        int dist = XStyleHints_startDragDistance(hints);
        if (dist >= 0) return dist;
    }
#endif /* XSTYLEHINTS_ON && XGUIAPPLICATION_ON */
    return XLINEEDIT_TRIPLE_CLICK_DIST;
}

/** @brief 查询 SH_BlinkCursorWhenTextSelected（对标 QCommonStyle 非
 *         Darwin 默认 1；XStyleHints/XStyle 不可用时同默认）。 */
static bool xlineedit_blinkCursorWhenTextSelected(const XLineEdit* self)
{
#if XSTYLE_ON
    XStyle* style = XStyle_defaultStyle();
    XStyleOption opt;
    if (style) {
        XStyleOption_init(&opt, XStylePE_FrameLineEdit);
        opt.m_rect = XWidget_rect((XWidget*)self);
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((XWidget*)self);
#endif /* XPALETTE_ON */
        return XStyle_styleHint(style,
                                (int)XStyleSH_BlinkCursorWhenTextSelected,
                                &opt, (XWidget*)self) != 0;
    }
#else
    (void)self;
#endif /* XSTYLE_ON */
    return true; /* QCommonStyle 非 Darwin 平台默认。 */
}

/** @brief 按样式提示注入密码回显延迟（对标 QLineEditPrivate::init 的
 *         SH_LineEdit_PasswordMaskDelay 注入，qlineedit_p.cpp:227）。
 *  @note  SH_LineEdit_PasswordCharacter 注入有意不做：XCommonStyle 返回
 *         U+25CF「●」而本库控制器与既有测试契约统一用「*」掩码字形
 *         （XLineControl 默认 + XLineEditTest/xgui_demo_apitest 的
 *         displayText 断言），改字形属跨族视觉契约变更，见 deferred。 */
static void xlineedit_applyStylePasswordHints(XLineEdit* self)
{
#if XSTYLE_ON
    XStyle* style = XStyle_defaultStyle();
    XStyleOption opt;
    int delay;
    if (!self || !self->m_control || !style) return;
    XStyleOption_init(&opt, XStylePE_FrameLineEdit);
    opt.m_rect = XWidget_rect((XWidget*)self);
#if XPALETTE_ON
    opt.m_palette = XWidget_palette((XWidget*)self);
#endif /* XPALETTE_ON */
    delay = XStyle_styleHint(style,
                             (int)XStyleSH_LineEdit_PasswordMaskDelay,
                             &opt, (XWidget*)self);
    XLineControl_setPasswordMaskDelay(self->m_control, delay);
#else
    (void)self;
#endif /* XSTYLE_ON */
}

/** @brief 重启三击窗口定时器并记录双击坐标（对标 mouseDoubleClickEvent
 *         尾部的 tripleClickTimer.start + d->tripleClick 赋值）。 */
static void xlineedit_armTripleClick(XLineEdit* self, const XPoint* pos)
{
    XObject* object;
    if (!self) return;
    object = (XObject*)self;
    if (self->m_tripleClickTimer != XTIMER_INVALID_ID)
        XObject_killTimer(object, self->m_tripleClickTimer);
    self->m_tripleClickTimer = XObject_startTimer_ms(
        object, (uint64_t)xlineedit_doubleClickInterval(),
        XTimerType_CoarseTimer);
    self->m_tripleClick = pos ? *pos : self->m_tripleClick;
}

/** @brief 三击窗口内且按点邻近双击点判定（对标 mousePressEvent 的
 *         tripleClickTimer.isActive() + manhattanLength < startDragDistance）。 */
static bool xlineedit_isTripleClick(const XLineEdit* self, const XPoint* pos)
{
    int dx;
    int dy;
    if (!self || !pos) return false;
    if (self->m_tripleClickTimer == XTIMER_INVALID_ID) return false;
    dx = pos->x - self->m_tripleClick.x;
    dy = pos->y - self->m_tripleClick.y;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return (dx + dy) < xlineedit_startDragDistance();
}

/** @brief 停止三击窗口定时器（定时器到期/控件析构时）。 */
static void xlineedit_disarmTripleClick(XLineEdit* self)
{
    if (!self || self->m_tripleClickTimer == XTIMER_INVALID_ID) return;
    XObject_killTimer((XObject*)self, self->m_tripleClickTimer);
    self->m_tripleClickTimer = XTIMER_INVALID_ID;
}

/**
 * @brief 创建并预热编辑控制器（init 与 move 共用）。
 * @details 控制器现存缺陷的壳侧规避（不动控制器文件，缺口已在迁移报告
 *          单列）：
 *          - 缺陷一：xlc_bufAssign 对「n==0 且目标缓冲未分配」解引用空
 *            指针（XLineControl_create("") 即段错误）。以非空文本创建
 *            分配 text/display/layout/textReturn 四缓冲，再 setText("")
 *            清空；setPreeditArea("x")→("") 预热 preedit 缓冲。缓冲一经
 *            分配不再回收，此后空内容赋值路径安全。掩码返回缓存
 *            m_maskReturn 不做预热（预热需经 setInputMask，会触发缺陷
 *            二，见下）；公开 API 的 setInputMask("") 空清路径经壳的
 *            「同掩码早退」守卫拦截，setInputMask(非空) 本身分配缓存，
 *            其后再清空亦安全。
 *          - 缺陷二（P0，勿从壳触发）：xlc_maskString 的局部 xlc_str
 *            fill 未 xlc_strInit 即使用（XLineControl.c:951），
 *            xlc_strFree(&fill) 释放栈垃圾指针 → 堆管理器自由链被污染
 *            → 后续分配复用在用块。设置了输入掩码的控制器内部SetText/
 *            插入路径必经此代码，控制器修复前输入掩码不可用。
 *          控制器修复后本序列可整体退化为 XLineControl_create("")。
 * @return 预热完成的控制器（拥有）；分配失败返回 NULL。
 */
static XLineControl* xlineedit_createControl(void)
{
    XLineControl* ctl = XLineControl_create("x");
    if (!ctl) return NULL;
    XLineControl_setText(ctl, "");
    XLineControl_setPreeditArea(ctl, 0, "x");
    XLineControl_setPreeditArea(ctl, -1, "");
    return ctl;
}

/** @brief 命中 action（x 坐标 → 索引；未命中返回 -1）。 */
static int xlineedit_hitAction(const XLineEdit* self, int x)
{
    int w = XWidget_width((XWidget*)self);
    int textStart = xlineedit_textStartX(self);
    int i;
    int leadIdx = 0;
    int trailIdx = 0;
    if (!self->m_actionCount) return -1;
    for (i = 0; i < (int)self->m_actionCount; ++i) {
        if (self->m_actionPositions[i] == XLineEditActionPosition_Leading) {
            int ax = textStart - XLINEEDIT_ACTION_W * (leadIdx + 1);
            if (x >= ax && x < ax + XLINEEDIT_ACTION_W) return i;
            ++leadIdx;
        } else {
            int ax = w - XLINEEDIT_ACTION_W * (trailIdx + 1);
            if (x >= ax && x < ax + XLINEEDIT_ACTION_W) return i;
            ++trailIdx;
        }
    }
    return -1;
}

/** @brief 绘制 action 区（iconText 文本或空）。 */
static void xlineedit_paintActions(const XLineEdit* self, XPainter* painter,
                                   uint32_t textColor)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    int leadIdx = 0;
    int trailIdx = 0;
    int i;
    if (!self->m_actionCount) return;
    for (i = 0; i < (int)self->m_actionCount; ++i) {
        XAction* action = self->m_actions[i];
        const char* iconText = "";
        int ax;
        int ay;
        if (!action) continue;
        if (self->m_actionPositions[i] == XLineEditActionPosition_Leading) {
            ax = xlineedit_textStartX(self) - XLINEEDIT_ACTION_W * (leadIdx + 1);
            ++leadIdx;
        } else {
            ax = w - XLINEEDIT_ACTION_W * (trailIdx + 1);
            ++trailIdx;
        }
        ay = (h - 14) / 2;
        {
            const XString* it = XAction_iconText_const(action);
            if (it) iconText = XString_toUtf8(it);
        }
        if (iconText && iconText[0])
            XPainter_drawText(painter, ax + 4, ay + 12, iconText, textColor);
    }
}

/* ==================== 键盘处理 ==================== */

/** @brief 键盘按下：整体分派给控制器 processKeyEvent（编辑/移动/回车/
 *         快捷键/补全；readOnly 门禁与事件 accept/ignore 由控制器口径
 *         结算）；接受后重排光标闪烁相位（对标 qlineedit.cpp keyPressEvent
 *         的 event->isAccepted() → control->updateCursorBlinking()，每次
 *         击键重置闪烁计时，光标在连续输入期间保持常显）。 */
static void VXLineEdit_keyPressEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    if (!edit->m_control) {
        XEvent_ignore(event);
        return;
    }
    XLineControl_processKeyEvent(edit->m_control, (XKeyEvent*)event);
    if (XEvent_isAccepted(event))
        XLineControl_updateCursorBlinking(edit->m_control);
}

/** @brief 键盘释放：默认忽略（自动重复/修饰键行为为后续扩展）。 */
static void VXLineEdit_keyReleaseEvent(XWidget* self, XEvent* event)
{
    if (!self || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_RELEASE) return;
    XEvent_ignore(event);
}

/**
 * @brief      输入法事件：经控制器 processInputMethodEvent 处理（提交串
 *             经插入过滤链整串直插，语义与迁移前一致；preedit 组合区为
 *             控制器增量能力）。readOnly 时忽略（与迁移前的 insert 守卫
 *             等价）。
 * @param      self  编辑框对象。
 * @param      event 输入法事件。
 * @return     无返回值。
 */
static void VXLineEdit_inputMethodEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_INPUT_METHOD) return;
    if (!edit->m_control) return;
    if (XLineControl_isReadOnly(edit->m_control)) return;
#if XWINDOWEVENT_ON
    XLineControl_processInputMethodEvent(edit->m_control,
                                         (XInputMethodEvent*)event);
    XEvent_accept(event);
#else
    (void)edit;
#endif /* XWINDOWEVENT_ON */
}

#if XMENU_ON
/** @brief 上下文菜单事件：创建标准菜单并弹出到事件全局坐标（对标
 *         QLineEdit::contextMenuEvent 的 createStandardContextMenu +
 *         popup；关闭即删对齐 WA_DeleteOnClose）；IME 组合态中抑制
 *         （对标 QLineEdit::event 的 QEvent::ContextMenu 分支
 *         composeMode 早退，qlineedit.cpp:1439-1443）。 */
static void VXLineEdit_contextMenuEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XContextMenuEvent* ctx;
    XMenu* menu;
    XPoint global;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_CONTEXT_MENU) return;
    if (edit->m_control &&
        XLineControl_composeMode(edit->m_control)) {
        XEvent_accept(event);
        return;
    }
    ctx = (XContextMenuEvent*)event;
    menu = XLineEdit_createStandardContextMenu(edit);
    if (!menu) return;
    global = XContextMenuEvent_globalPosition(ctx);
    /* 对标 Qt：popup 非阻塞；关闭后由 DeleteOnClose 属性自删，
       与 Qt 菜单的 WA_DeleteOnClose 语义一致。 */
    XWidget_setAttribute((XWidget*)menu, XWidgetAttribute_DeleteOnClose,
                         true);
    XMenu_popup(menu, &global);
    XEvent_accept(event);
}
#endif /* XMENU_ON */

/* ==================== 鼠标处理 ==================== */

/** @brief 左键/中键按下：获得焦点；三击窗口内全选（对标 Qt 三击
 *         selectAll）；左键命中清除按钮则清空文本；否则把光标定位到
 *         点击处（坐标平移在壳，命中测试 xToPos 与移动 moveCursor 在
 *         控制器；Shift+点击扩展选区）。中键与左键同为光标移动（对标
 *         Qt mousePressEvent 无按键分流，qlineedit.cpp:1498-1536；
 *         中键的 Selection 粘贴在释放路径，qlineedit.cpp:1605-1608）。 */
static void VXLineEdit_mousePressEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XMouseEvent* me;
    XPoint pos;
    bool shift;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton &&
        XMouseEvent_button(me) != XMouseButton_MiddleButton) {
        /* 右键等按键保持 ignore（上下文菜单由投递层对未接受右键合成）。 */
        XEvent_ignore(event);
        return;
    }
    /* 点击聚焦：setFocus 不查策略位（XWidget.c XWidget_setFocusReason
       只门禁 enabled），此处不再改写策略——init 的 StrongFocus（含
       ClickFocus 位）保持终身有效，点击后 LE 仍是 Tab 候选（#40）。
       对标 Qt：QLineEdit 点击路径从不 setFocusPolicy（qlineedit.cpp
       mousePressEvent 仅 d->control->moveCursor 等编辑结算）。 */
    XWidget_setFocus((XWidget*)edit);
    g_focusedLineEdit = edit;
    pos = XMouseEvent_position(me);
    /* 三击：双击窗口期内且按点邻近双击点 → 全选并结束（对标 Qt
       mousePressEvent 的 tripleClick 分支，qlineedit.cpp:1516-1520；
       定时器保持活跃到自然到期）。 */
    if (xlineedit_isTripleClick(edit, &pos) && edit->m_control) {
        XLineControl_selectAll(edit->m_control);
        XEvent_accept(event);
        return;
    }
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        /* 中键：仅光标移动（对标 Qt；粘贴在 mouseReleaseEvent）。 */
        shift = (me->m_modifiers & XKeyboardModifier_ShiftModifier) != 0;
        if (edit->m_control) {
            int clickX = pos.x - xlineedit_textStartX(edit) +
                         edit->m_viewOffset;
            int bytePos;
            xlineedit_syncControlFont(edit);
            bytePos = XLineControl_xToPos(
                edit->m_control, clickX,
                (int)XLineControlCursorPosition_BetweenCharacters);
            XLineControl_moveCursor(edit->m_control, bytePos, shift);
        }
        XWidget_grabMouse((XWidget*)edit);
        XEvent_accept(event);
        return;
    }
    /* 内置 action 命中：触发 action 后返回（不移动光标）。 */
    {
        int actionIdx = xlineedit_hitAction(edit, pos.x);
        if (actionIdx >= 0 && edit->m_actions[actionIdx]) {
            XAction_trigger(edit->m_actions[actionIdx]);
            XEvent_accept(event);
            return;
        }
    }
    /* 清除按钮命中：点击清除文本（视为用户编辑：全选+删除选区路径，
       撤销栈保留记录并发射 textEdited/textChanged，与迁移前一致）。 */
    if (edit->m_clearButtonEnabled &&
        !XLineControl_isReadOnly(edit->m_control) &&
        XLineControl_text(edit->m_control)[0] &&
        pos.x >= edit->m_clearButtonRect.x &&
        pos.x < edit->m_clearButtonRect.x + edit->m_clearButtonRect.width &&
        pos.y >= edit->m_clearButtonRect.y &&
        pos.y < edit->m_clearButtonRect.y + edit->m_clearButtonRect.height) {
        XLineControl_selectAll(edit->m_control);
        XLineControl_removeSelection(edit->m_control);
        XEvent_accept(event);
        return;
    }
    shift = (me->m_modifiers & XKeyboardModifier_ShiftModifier) != 0;
    {
        /* 壳做 contents 平移（边框/边距/action 区/滚动偏移），控制器
           做像素→字节偏移命中与光标移动（对标 Qt d->xToPos + control
           ->moveCursor 编排）。 */
        int clickX = pos.x - xlineedit_textStartX(edit) + edit->m_viewOffset;
        int bytePos;
        xlineedit_syncControlFont(edit);
        bytePos = XLineControl_xToPos(edit->m_control, clickX,
                                      (int)XLineControlCursorPosition_BetweenCharacters);
        XLineControl_moveCursor(edit->m_control, bytePos, shift);
    }
    /* 对标 Qt 按压建立的隐式鼠标抓取（qwidgetwindow.cpp qt_button_down，
     * 同 XScrollBar/XSizeGrip 的 grabMouse 拖拽口径）：按住左键拖拽期间
     * 移动事件直投本控件（拖选在控件边界外仍延续），释放时经
     * mouseReleaseEvent 的 releaseMouse 解除（仅当 self 为当前抓取者时
     * 生效，不误清他人抓取）。命中 action/清除按钮的早退路径不抓取。 */
    XWidget_grabMouse((XWidget*)edit);
    XEvent_accept(event);
}

/** @brief 鼠标双击：选词（对标 Qt 双击 selectWordAtPos）并布防三击窗口
 *         定时器（对标 qlineedit.cpp:1657-1658）。 */
static void VXLineEdit_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XMouseEvent* me;
    XPoint pos;
    int clickX;
    int bytePos;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) return;
    /* 对标 Qt：仅左键走选词路径（qlineedit.cpp:1623）。 */
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    /* 对标 Qt 双击选词（此前简化为 selectAll；控制器已有
     * selectWordAtPos 未被使用）。坐标口径与 mousePress 一致：
     * contents 平移 + 滚动偏移。 */
    if (!edit->m_control) return;
    pos = XMouseEvent_position(me);
    clickX = pos.x - xlineedit_textStartX(edit) + edit->m_viewOffset;
    xlineedit_syncControlFont(edit);
    bytePos = XLineControl_xToPos(edit->m_control, clickX,
                                  (int)XLineControlCursorPosition_BetweenCharacters);
    XLineControl_selectWordAtPos(edit->m_control, bytePos);
    /* 布防三击窗口：窗口期内邻近按下 → mousePressEvent 全选。 */
    xlineedit_armTripleClick(edit, &pos);
    XWidget_update((XWidget*)edit);
    XEvent_accept(event);
}

/** @brief 鼠标释放：解除本控件在按下时建立的鼠标抓取（releaseMouse
 *         仅当 self 为当前抓取者时生效；非自身序列的释放零干预）；
 *         平台支持选择区（X11 PRIMARY）时左键释放复制选区、中键释放
 *         在点击处粘贴（对标 qlineedit.cpp:1601-1610）。 */
static void VXLineEdit_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XMouseEvent* me;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    me = (XMouseEvent*)event;
    /* 仅承接自身按下序列的释放（Qt 隐式抓取下释放恒投按下控件；此处
       以显式抓取登记等价判定，非自身序列保持既有 ignore 零回归）。 */
    if (XWidget_mouseGrabber() != self) {
        XEvent_ignore(event);
        return;
    }
    XWidget_releaseMouse(self);
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    if (edit->m_control) {
        /* 对标 Qt：Selection 通道操作仅当平台后端声明支持选择区
           （QGuiApplication::clipboard()->supportsSelection() 门禁）。 */
        XClipboard* clip = XGuiApplication_clipboard();
        if (clip && XClipboard_supportsSelection(clip)) {
            if (XMouseEvent_button(me) == XMouseButton_LeftButton) {
                /* copy 空选区/密码回显下零写入（控制器语义）。 */
                XLineControl_copy(edit->m_control,
                                  (int)XClipboardMode_Selection);
            } else if (XMouseEvent_button(me) == XMouseButton_MiddleButton &&
                       !XLineControl_isReadOnly(edit->m_control)) {
                XLineControl_deselect(edit->m_control);
                XLineControl_paste(edit->m_control,
                                   (int)XClipboardMode_Selection);
            }
        }
    }
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
    XEvent_accept(event);
}

/** @brief 鼠标移动：按住左键拖拽扩展选区（对标 Qt QLineEdit 拖选：
 *         QWidgetLineControl::processMouseEvent MouseMove 左键分支
 *         xToPos + moveCursor(pos, mark=true)——锚点保持在按下位置，
 *         控制器 moveCursor mark 分支与 Qt 同源）并更新 X11 PRIMARY
 *         （对标 Qt 拖选实时更新 Selection；键盘选区路径
 *         XLineControl_processKeyEvent 尾部的 copy(Selection) 同款
 *         编排，copy 内部空选区零写入）。 */
static void VXLineEdit_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XMouseEvent* me;
    XPoint pos;
    int clickX;
    int bytePos;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    if (!edit->m_control) return;
    /* 仅承接自身按住左键的拖拽序列（抓取登记在 mousePress）：
     * 他处按下拖经本控件、或无键悬停一律不扩展（对标 Qt 隐式抓取
     * 下移动事件靶向语义）。 */
    if (XWidget_mouseGrabber() != self) return;
    me = (XMouseEvent*)event;
    if (!(XMouseEvent_buttons(me) & XMouseButton_LeftButton)) return;
    /* 坐标口径与 mousePress 一致：壳 contents 平移（边框/边距/action
       区/滚动偏移），控制器像素→字节偏移命中。 */
    pos = XMouseEvent_position(me);
    clickX = pos.x - xlineedit_textStartX(edit) + edit->m_viewOffset;
    xlineedit_syncControlFont(edit);
    bytePos = XLineControl_xToPos(edit->m_control, clickX,
                                  (int)XLineControlCursorPosition_BetweenCharacters);
    /* mark=true：无选区时锚点=当前光标（=按下位置），有选区时锚点
       固定远端，拖拽持续扩展（xToPos 钳位，拖出边界自然到端点）。 */
    XLineControl_moveCursor(edit->m_control, bytePos, true);
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    {
        /* 拖选实时更新 PRIMARY（X11）：supportsSelection 门禁 + copy
           空选区零写入，与键盘选区路径行为一致。 */
        XClipboard* clip = XGuiApplication_clipboard();
        if (clip && XClipboard_supportsSelection(clip))
            XLineControl_copy(edit->m_control,
                              (int)XClipboardMode_Selection);
    }
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
    XWidget_update((XWidget*)edit);
    XEvent_accept(event);
}

/* ==================== 焦点处理 ==================== */

/** @brief 获得焦点：Tab/Backtab/快捷键聚焦时全选（有掩码则光标落首个
 *         可编辑位）（对标 qlineedit.cpp:1899-1907）；启用光标闪烁
 *         （qlineedit.cpp:1915）；PasswordEchoOnEdit 焦点态推送控制器
 *         切明文回显；重绘。 */
static void VXLineEdit_focusInEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XFocusReason reason = XFocusReason_Other;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_FOCUS_IN) return;
#if XWINDOWEVENT_ON
    reason = XFocusEvent_reason((XFocusEvent*)event);
#endif /* XWINDOWEVENT_ON */
    if (edit->m_control) {
        if (reason == XFocusReason_Tab || reason == XFocusReason_Backtab ||
            reason == XFocusReason_Shortcut) {
            if (XLineControl_inputMask(edit->m_control)[0])
                XLineControl_moveCursor(edit->m_control,
                                        XLineControl_nextMaskBlank(
                                            edit->m_control, 0),
                                        false);
            else if (!XLineControl_hasSelectedText(edit->m_control))
                XLineControl_selectAll(edit->m_control);
        }
        /* 密码回显宿主判定：控制器无 widget 身份，壳推送焦点态（审计
           5.1；对标 Qt updatePasswordEchoEditing 的壳侧编排）。 */
        XLineControl_updatePasswordEchoEditing(edit->m_control, true);
        /* 启用光标闪烁（焦点内 blink 定时器 + 相位置位；击键/移动经
           keyPressEvent 的 updateCursorBlinking 重置计时；失焦经
           focusOutEvent 关闭并断开 styleHints 联动）。 */
        XLineControl_setBlinkingCursorEnabled(edit->m_control, true);
    }
    XWidget_update((XWidget*)edit);
    XEvent_accept(event);
}

/** @brief 失去焦点：切回密码回显（PasswordEchoOnEdit）、失焦清选区
 *         （ActiveWindow/Popup 原因除外）、关闭闪烁（对标
 *         qlineedit.cpp focusOutEvent:1939-1951 的顺序）；自上次发射
 *         后用户编辑过则提交 editingFinished（壳门禁，对标 Qt
 *         d->edited && (hasAcceptableInput||fixup())）。 */
static void VXLineEdit_focusOutEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XFocusReason reason = XFocusReason_Other;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_FOCUS_OUT) return;
#if XWINDOWEVENT_ON
    reason = XFocusEvent_reason((XFocusEvent*)event);
#endif /* XWINDOWEVENT_ON */
    if (edit->m_control) {
        if (XLineControl_passwordEchoEditing(edit->m_control)) {
            /* PasswordEchoOnEdit：失焦切回密码回显（qlineedit.cpp:1939）。 */
            XLineControl_updatePasswordEchoEditing(edit->m_control, false);
        }
        if (reason != XFocusReason_ActiveWindow &&
            reason != XFocusReason_Popup)
            XLineControl_deselect(edit->m_control);
        XLineControl_setBlinkingCursorEnabled(edit->m_control, false);
    }
    if (edit->m_finishedPending) {
        edit->m_finishedPending = false;
        /* 对标 Qt d->edited && (hasAcceptableInput()||fixup())：不可
           接受时先交控制器 fixup 修复，修复后仍不可接受则不发射
           editingFinished（此前仅凭 edited 即发射）。 */
        if (!XLineEdit_hasAcceptableInput(edit)) {
            if (edit->m_control)
                XLineControl_fixup(edit->m_control);
            if (!XLineEdit_hasAcceptableInput(edit)) {
                XWidget_update((XWidget*)edit);
                XEvent_accept(event);
                return;
            }
            XWidget_update((XWidget*)edit);
        }
        xlineedit_emitVoidSignal(edit,
                                 (size_t)XLineEdit_editingFinished_signal);
    }
    XWidget_update((XWidget*)edit);
    XEvent_accept(event);
}

/* ==================== 绘制 ==================== */

/** @brief 绘制边框 + 文本/选区/光标（控制器 draw）+ 占位 + 清除按钮 +
 *         action 图标区。 */
static void VXLineEdit_paintEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r = XWidget_rect(self);
    uint32_t base;
    uint32_t dark;
    uint32_t light;
    uint32_t text;
    uint32_t mid;
    uint32_t highlight;
    uint32_t highlightedText;
    int tx;
    int ty;
    int baseline;
    int lineH = 14;

    (void)event; /* 绘制事件无载荷。 */
    if (!edit || r.width <= 2 || r.height <= 2) return;
    r.x = 0; r.y = 0;
    base  = xlineedit_color(edit, XPaletteColorRole_Base);
    dark  = xlineedit_color(edit, XPaletteColorRole_Dark);
    light = xlineedit_color(edit, XPaletteColorRole_Light);
    text  = xlineedit_color(edit, XPaletteColorRole_Text);
    mid   = xlineedit_color(edit, XPaletteColorRole_Mid);
    highlight       = xlineedit_color(edit, XPaletteColorRole_Highlight);
    highlightedText = xlineedit_color(edit, XPaletteColorRole_HighlightedText);

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
    /* 绘制字体 = 控件字体（与光标/点击的文本度量同一来源，否则
       估算与渲染错位随输入增长）。 */
    {
        const XFont* paintFont = &((XWidget*)self)->m_font;
        XPainter_setFont(&painter, paintFont);
    }

    if (edit->m_frame) {
#if XSTYLE_ON
        if (XStyle_defaultStyle() != NULL) {
            /* Fusion/公共风格接管：输入框面板由样式引擎绘制。 */
            XStyle* style = XStyle_defaultStyle();
            XStyleOption opt;
            XStyleOption_init(&opt, XStylePE_PanelLineEdit);
            opt.m_rect = r;
            opt.m_state = XWidget_isEnabled(self)
                ? XStyleState_Enabled : 0;
            if (XWidget_hasFocus(self))
                opt.m_state |= XStyleState_HasFocus;
            if (XWidget_underMouse(self) && XWidget_isEnabled(self))
                opt.m_state |= XStyleState_MouseOver;
            opt.m_text = XLineEdit_placeholderText(edit);
#if XPALETTE_ON
            opt.m_palette = XWidget_palette(self);
#endif
            XStyle_drawPrimitive(style, XStylePE_PanelLineEdit, &opt,
                                 &painter, self);
            if (XWidget_hasFocus(self)) {
                XStyleOption foc = opt;
                foc.m_type = XStylePE_FrameFocusRect;
                foc.m_rect = r;
                XStyle_drawPrimitive(style, XStylePE_FrameFocusRect,
                                     &foc, &painter, self);
            }
        } else
#endif /* XSTYLE_ON */
        {
            XRect e = r;
            e.height = 1;
            XPainter_fillRect(&painter, &e, dark);
            e = r; e.width = 1;
            XPainter_fillRect(&painter, &e, dark);
            e = r; e.x = r.x + r.width - 1; e.width = 1;
            XPainter_fillRect(&painter, &e, light);
            e = r; e.y = r.y + r.height - 1; e.height = 1;
            XPainter_fillRect(&painter, &e, light);
        }
    }

    /* 文本区绘制数据由控制器承载：注入调色板四色（Highlight/高亮前景/
       正文/掩码反选），同步字体后经 draw 绘制正文/选区/光标。 */
    if (edit->m_control) {
        XLineControlPalette pal;
        pal.m_highlight = highlight;
        pal.m_highlightedText = highlightedText;
        pal.m_text = text;
        pal.m_window = base; /* 掩码反选前景（迁移前无此绘制，取 Base）。 */
        XLineControl_setPalette(edit->m_control, &pal);
        xlineedit_syncControlFont(edit);
    }
    /* 滚动偏移按当前几何/对齐/光标结算（对标 Qt 在 paintEvent 内联
       结算 hscroll，尺寸变化/对齐变化无需额外事件联动）。 */
    xlineedit_updateViewOffset(edit);
    {
        const XFont* font = &((XWidget*)self)->m_font;
        int ascent = XPainter_textAscent(font);
        int descent = XPainter_textDescent(font);
        int va;
        lineH = ascent + descent; /* 字形盒高（不含行距）。 */
        if (lineH < 14) lineH = 14;
        /* 垂直对齐结算 vscroll（对标 qlineedit.cpp:1996-2008 的
         * AlignBottom/AlignTop/center 三分支，verticalMargin=1）；
         * ty 为行顶部；控制器以 offset.y + ascent 为基线逐字绘制。 */
        va = edit->m_alignment & XAlignment_VerticalMask;
        if (va & XAlignment_Bottom)
            ty = r.height - lineH - XLINEEDIT_VERTICAL_MARGIN;
        else if (va & XAlignment_Top)
            ty = XLINEEDIT_VERTICAL_MARGIN;
        else
            ty = (r.height - lineH + 1) / 2;
        baseline = ty + ascent;
    }
    tx = xlineedit_textStartX(edit);
    if (edit->m_control) {
        XPoint origin;
        /* 根因修复（R-32）壳层门禁（对标 Qt 6.8.3 QLineEdit::paintEvent
         * 旗标分流）：DrawSelections 仅在有选区或（有 inputMask 且光标
         * 亮且非只读）；DrawCursor 仅在光标亮且非只读且 inputMask 为空
         * ——掩码反选格与细光标互斥。旧代码恒传 Selections+焦点即传
         * Cursor，普通行编辑 blink 亮相时反相格+细光标同屏。 */
        bool hasSel = XLineControl_hasSelectedText(edit->m_control);
        bool cursorVisible = XWidget_hasFocus(self) &&
                             (!hasSel || xlineedit_blinkCursorWhenTextSelected(
                                             edit));
        bool hasMask = XLineControl_inputMask(edit->m_control)[0] != '\0';
        bool readOnly = XLineControl_isReadOnly(edit->m_control);
        int flags = (int)XLineControlDrawFlag_Text;
        int textEndPx = XWidget_width((XWidget*)self) -
                        (edit->m_frame ? 4 : 2) - edit->m_textMargins.right -
                        ((edit->m_clearButtonEnabled &&
                          XLineControl_text(edit->m_control)[0]) ? 18 : 0);
        XRect textClip;
        /* 水平滚动后 offset 左侧的已滚出文本会画到边框区外——对标
           QLineEdit：正文/选区/光标一律裁剪到文本矩形内绘制。 */
        textClip.x = tx;
        textClip.y = 0;
        textClip.width = (textEndPx > tx) ? (textEndPx - tx) : 1;
        textClip.height = r.height;
        origin.x = tx - edit->m_viewOffset;
        origin.y = ty;
        if (hasSel ||
            (cursorVisible && hasMask && !readOnly))
            flags |= (int)XLineControlDrawFlag_Selections;
        if (cursorVisible && !readOnly && !hasMask)
            flags |= (int)XLineControlDrawFlag_Cursor;
        XPainter_save(&painter);
        XPainter_setClipRect(&painter, &textClip,
                             XPainterClipOperation_IntersectClip);
        /* 控制器 draw 自带 clip 参数（裁剪矩形，对标 QLineEdit 传
           viewport）——双保险：painter 级与控制器级一致。 */
        XLineControl_draw(edit->m_control, &painter, &origin, &textClip,
                          flags);
        XPainter_restore(&painter);
    }
    /* 占位提示（对标 Qt shouldShowPlaceholderText + paintEvent 占位分支，
     * qlineedit.cpp:2012-2027）：空文本且非组合态时显示；AlignHCenter
     * 聚焦态抑制（qlineedit_p.h:165-169）；用 PlaceholderText 调色板
     * 角色（Qt pal.placeholderText()），按水平对齐放置并裁剪到文本区。 */
    if (xlineedit_ctlRawText(edit)[0] == '\0' &&
        (!edit->m_control || !XLineControl_composeMode(edit->m_control)) &&
        edit->m_placeholder && XString_toUtf8(edit->m_placeholder) &&
        XString_toUtf8(edit->m_placeholder)[0] &&
        !((edit->m_alignment & XAlignment_HCenter) &&
          XWidget_hasFocus(self))) {
        const char* phText = XString_toUtf8(edit->m_placeholder);
        const XFont* font = &((XWidget*)self)->m_font;
        /* 占位仅在空文本时显示——清除按钮此态不绘制，不预留 18px。 */
        int textEndPx = XWidget_width((XWidget*)self) -
                        (edit->m_frame ? 4 : 2) - edit->m_textMargins.right;
        int visibleW = (textEndPx > tx) ? (textEndPx - tx) : 1;
        int phW = xlineedit_displayWidth(font, phText,
                                         xlineedit_charCount(phText));
        int phX;
        uint32_t phColor;
        XRect textClip;
        textClip.x = tx;
        textClip.y = 0;
        textClip.width = visibleW;
        textClip.height = r.height;
        /* 按水平对齐放置（Qt drawText(lineRect, alignPhText, …)）；宽度
           溢出时钳到起始侧由 clip 裁剪（Qt 为省略号截断，见 deferred）。 */
        if (edit->m_alignment & XAlignment_Right)
            phX = textEndPx - (phW < visibleW ? phW : visibleW);
        else if (edit->m_alignment & XAlignment_HCenter)
            phX = tx + (visibleW - (phW < visibleW ? phW : visibleW)) / 2;
        else
            phX = tx;
        phColor = xlineedit_color(edit, XPaletteColorRole_PlaceholderText);
        XPainter_save(&painter);
        XPainter_setClipRect(&painter, &textClip,
                             XPainterClipOperation_IntersectClip);
        XPainter_drawText(&painter, phX, baseline, phText, phColor);
        XPainter_restore(&painter);
    }

    /* 清除按钮（简笔 ×；点击清除由 mousePressEvent 处理）。 */
    if (edit->m_clearButtonEnabled &&
        xlineedit_ctlRawText(edit)[0] != '\0') {
        XRect btn;
        btn.width = 16;
        btn.height = 14;
        btn.x = r.x + r.width - (edit->m_frame ? 4 : 2) -
                edit->m_textMargins.right - btn.width;
        btn.y = (r.height - btn.height) / 2;
        edit->m_clearButtonRect = btn;
        XPainter_setPen(&painter, mid);
        XPainter_drawLine(&painter, btn.x + 3, btn.y + 3,
                          btn.x + btn.width - 4, btn.y + btn.height - 4);
        XPainter_drawLine(&painter, btn.x + btn.width - 4, btn.y + 3,
                          btn.x + 3, btn.y + btn.height - 4);
    } else {
        edit->m_clearButtonRect.x = 0;
        edit->m_clearButtonRect.y = 0;
        edit->m_clearButtonRect.width = 0;
        edit->m_clearButtonRect.height = 0;
    }
    /* 内置 action 图标区（末尾侧，绘制在清除按钮之后/文本之上）。 */
    xlineedit_paintActions(edit, &painter, text);

    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/* ==================== 虚槽 ==================== */

/** @brief 变更事件（对标 QLineEdit::changeEvent，qlineedit.cpp:2282-2316）：
 *         FontChange → 控制器字体重排；StyleChange → 按样式提示重注密码
 *         掩码字符/回显延迟；ActivationChange → 重绘；尾转父类（Qt 以
 *         QWidget::changeEvent(ev) 收尾）。 */
static void VXLineEdit_changeEvent(XWidget* self, XEvent* event)
{
    XLineEdit* edit = (XLineEdit*)self;
    XEventType type;
    if (!edit || !event) return;
    type = XEvent_type(event);
    switch (type) {
    case XEVENT_TYPE_FONT_CHANGE:
        xlineedit_syncControlFont(edit);
        xlineedit_updateSizeHints(edit);
        XWidget_update(self);
        break;
    case XEVENT_TYPE_STYLE_CHANGE:
        xlineedit_applyStylePasswordHints(edit);
        XWidget_update(self);
        break;
    case XEVENT_TYPE_ACTIVATION_CHANGE:
        XWidget_update(self);
        break;
    default:
        XEvent_ignore(event);
        break;
    }
    XClass_Parent(XWidget, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 定时器事件：三击窗口到期即撤防（对标 QWidgetLineControl/
 *         QLineEditPrivate 对 tripleClickTimer 的到期停表）；其余交父类。 */
static void VXLineEdit_timerEvent(XObject* object, XTimerEvent* event)
{
    XLineEdit* edit = (XLineEdit*)object;
    XTimerId id;
    if (!edit || !event) return;
    id = XTimerEvent_timerId(event);
    if (id != XTIMER_INVALID_ID && id == edit->m_tripleClickTimer) {
        edit->m_tripleClickTimer = XTIMER_INVALID_ID;
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void(*)(XObject*, XTimerEvent*))((XObject*)object, event);
}

/** @brief 反初始化：销毁控制器与壳资源后调用父类 deinit。 */
static void VXLineEdit_deinit(XLineEdit* self)
{
    if (!self) return;
    xlineedit_disarmTripleClick(self);
    if (self->m_control) {
        /* 先关闪烁再删控制器：销毁聚焦中的编辑框时 FOCUS_OUT 由基类
         * deinit 派发（晚于本函数），若不在此显式关闭，控制器侧 blink
         * 定时器与 styleHints 联动连接将随悬空对象残留（对标 Qt：
         * QWidgetLineControl 析构前焦点清理已 setBlinkingCursorEnabled
         * (false)）。 */
        XLineControl_setBlinkingCursorEnabled(self->m_control, false);
        XClassDelete((XClass*)self->m_control);
        self->m_control = NULL;
    }
    if (self->m_placeholder) {
        XClassDelete((XClass*)self->m_placeholder);
        self->m_placeholder = NULL;
    }
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/** @brief 深拷贝：基类深拷贝后把编辑状态经公开 API 迁移到本方控制器
 *         （文本/回显/长度/掩码/校验/光标选区；控制器撤销历史无克隆
 *         API，不随拷贝迁移——见迁移报告缺口记录）。 */
static void VXLineEdit_copy(XLineEdit* self, const XLineEdit* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XLineEdit_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    XLineEdit_setText(self, XLineEdit_text(other));
    if (self->m_placeholder && other->m_placeholder)
        XString_assign(self->m_placeholder, other->m_placeholder);
    self->m_viewOffset = other->m_viewOffset;
    self->m_frame = other->m_frame;
    self->m_alignment = other->m_alignment;
    self->m_clearButtonEnabled = other->m_clearButtonEnabled;
    self->m_finishedPending = other->m_finishedPending;
    self->m_textMargins = other->m_textMargins;
    XLineEdit_setMaxLength(self, XLineEdit_maxLength(other));
    XLineEdit_setEchoMode(self, XLineEdit_echoMode(other));
    XLineEdit_setReadOnly(self, XLineEdit_isReadOnly(other));
    XLineEdit_setDragEnabled(self, XLineEdit_dragEnabled(other));
    XLineEdit_setCursorMoveStyle(self, XLineEdit_cursorMoveStyle(other));
    XLineEdit_setModified(self, XLineEdit_isModified(other));
    self->m_validator = other->m_validator;
    self->m_validatorUserData = other->m_validatorUserData;
    XLineEdit_setValidator(self, other->m_validator,
                           other->m_validatorUserData);
    XLineEdit_setInputMask(self, XLineEdit_inputMask(other));
    /* 光标/选区镜像：有选区按 setSelection 承载（锚点落选区端，对标
       控制器语义），否则恢复光标字符位置。 */
    if (other->m_control && self->m_control) {
        if (XLineControl_hasSelectedText(other->m_control)) {
            int ss = XLineControl_selectionStart(other->m_control);
            int se = XLineControl_selectionEnd(other->m_control);
            const char* raw = xlineedit_ctlRawText(other);
            int len = (int)(xlineedit_charCountPrefix(raw, (size_t)se) -
                            xlineedit_charCountPrefix(raw, (size_t)ss));
            XLineEdit_setSelection(self, ss, len);
        } else {
            XLineEdit_setCursorPosition(self,
                                        XLineEdit_cursorPosition(other));
        }
    }
    self->m_clearButtonRect = other->m_clearButtonRect;
    self->m_tripleClick = other->m_tripleClick;
    /* 三击窗口定时器不随拷贝迁移（窗口语义属源对象）。 */
    self->m_tripleClickTimer = XTIMER_INVALID_ID;
    xlineedit_updateSizeHints(self);
    XWidget_update((XWidget*)self);
}

/** @brief 移动语义：基类移动后转移控制器与壳缓冲，源对象归构造默认值
 *         （控制器信号转发连接随接收方重连）。 */
static void VXLineEdit_move(XLineEdit* self, XLineEdit* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XLineEdit_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    /* 断开源壳上的转发连接后整体转移控制器。 */
    xlineedit_disconnectControlSignals(other, other);
    if (self->m_control)
        XClassDelete((XClass*)self->m_control);
    self->m_control = other->m_control;
    other->m_control = xlineedit_createControl();
    if (other->m_control) {
        XLineControl_setAccessibleObject(other->m_control,
                                         (XObject*)other);
        xlineedit_connectControlSignals(other, other);
    }
    if (self->m_control) {
        XLineControl_setAccessibleObject(self->m_control, (XObject*)self);
        /* 转移后的控制器需按新宿主重建转发连接（断开时以 other 为
           接收方整体移除，信号转发链不随指针转移）。 */
        xlineedit_connectControlSignals(self, self);
    }
    if (self->m_placeholder) XClassDelete((XClass*)self->m_placeholder);
    self->m_placeholder = other->m_placeholder;
    other->m_placeholder = XString_create();
    self->m_viewOffset = other->m_viewOffset;
    self->m_frame = other->m_frame;
    self->m_alignment = other->m_alignment;
    self->m_clearButtonEnabled = other->m_clearButtonEnabled;
    self->m_finishedPending = other->m_finishedPending;
    self->m_textMargins = other->m_textMargins;
    self->m_validator = other->m_validator;
    self->m_validatorUserData = other->m_validatorUserData;
    self->m_clearButtonRect = other->m_clearButtonRect;
    /* 三击窗口随宿主整体转移；源对象归构造默认值。 */
    self->m_tripleClick = other->m_tripleClick;
    self->m_tripleClickTimer = other->m_tripleClickTimer;
    other->m_viewOffset = 0;
    other->m_frame = true;
    other->m_alignment = XAlignment_Left;
    other->m_clearButtonEnabled = false;
    other->m_finishedPending = false;
    XMargins_init(&other->m_textMargins, 0, 0, 0, 0);
    other->m_validator = NULL;
    other->m_validatorUserData = NULL;
    XRect_init(&other->m_clearButtonRect, 0, 0, 0, 0);
    other->m_tripleClickTimer = XTIMER_INVALID_ID;
    XPoint_init(&other->m_tripleClick, 0, 0);
    /* 校验钩子经适配层下发：控制器仅存壳借用指针，move 后重定位到
       目标壳（源壳指针随默认值复位）。 */
    XLineEdit_setValidator(self, self->m_validator,
                           self->m_validatorUserData);
    XLineEdit_setValidator(other, NULL, NULL);
    xlineedit_updateSizeHints(self);
    XWidget_update((XWidget*)self);
}

/* ==================== 生命周期 ==================== */

#if XINPUTMETHOD_ON
/** @brief IME 文本查询限幅（字节；与 XTextControl_inputMethodQuery 的
 *         前后文默认限幅一致）。 */
#define XLINEEDIT_IM_TEXT_LIMIT 1024

/** @brief [start, start+len) 字节区间深拷（XMemory 体系承载，调用方
 *         XFree_System 释放）。 */
static char* xlineedit_dupRange(const char* text, int start, int len)
{
    char* out;
    if (!text || start < 0 || len <= 0) return NULL;
    out = (char*)XMalloc_System((size_t)len + 1);
    if (!out) return NULL;
    XMemcpy(out, text + start, (size_t)len);
    out[len] = '\0';
    return out;
}

/** @brief 选区锚点字节偏移（无选区=光标；对标 QWidgetLineControl 的
 *         anchor 语义：锚点为选区未随光标移动的一端）。 */
static int xlineedit_anchorPos(const XLineControl* ctl)
{
    if (!ctl) return 0;
    if (!XLineControl_hasSelectedText(ctl)) return XLineControl_cursor(ctl);
    return (XLineControl_cursor(ctl) == XLineControl_selectionStart(ctl))
               ? XLineControl_selectionEnd(ctl)
               : XLineControl_selectionStart(ctl);
}

/** @brief 锚点矩形（控件局部坐标）：x 取控制器 cursorToX(anchor) 后加
 *         contents 偏移，纵向行框与 XLineEdit_cursorRect 同口径。 */
static XRect xlineedit_anchorRectWidget(const XLineEdit* self)
{
    XRect rect;
    const XFont* font;
    int lineH;
    int ty;
    if (!self || !self->m_control) {
        XRect_init(&rect, 0, 0, 0, 0);
        return rect;
    }
    font = &((XWidget*)self)->m_font;
    lineH = XPainter_textHeight(font);
    if (lineH < 14) lineH = 14;
    xlineedit_syncControlFont((XLineEdit*)self);
    ty = (XWidget_height((XWidget*)self) - lineH) / 2;
    rect.x = xlineedit_textStartX(self) - self->m_viewOffset +
             XLineControl_cursorToX(self->m_control,
                                    xlineedit_anchorPos(self->m_control));
    rect.y = ty + 1;
    rect.width = XLINEEDIT_CURSOR_W;
    rect.height = lineH - 2;
    return rect;
}

/** @brief 基类默认查询复刻（对标 QWidget::inputMethodQuery 默认实现）。
 *  @note  基类实现为 XWidget.c 内部静态，虚槽重载后无法显式回调，按其
 *         文档契约逐项复刻；其余查询项返回 NULL（等价无效 QVariant）。 */
static XVariant* xlineedit_inputMethodQueryBase(const XWidget* self,
                                                XInputMethodQuery query)
{
    if (!self) return NULL;
    switch (query) {
    case XInputMethodQuery_ImCursorRectangle: {
        XRectF rect;
        rect.x = (float)XWidget_width(self) / 2.0f;
        rect.y = 0.0f;
        rect.width = 1.0f;
        rect.height = (float)XWidget_height(self);
        return XVariant_create(&rect, sizeof(rect), XVariantType_User);
    }
    case XInputMethodQuery_ImInputItemClipRectangle: {
        XRect rect = XWidget_rect(self);
        XRectF rectF;
        rectF.x = (float)rect.x;
        rectF.y = (float)rect.y;
        rectF.width = (float)rect.width;
        rectF.height = (float)rect.height;
        return XVariant_create(&rectF, sizeof(rectF), XVariantType_User);
    }
    case XInputMethodQuery_ImHints: {
        int32_t value = (int32_t)XWidget_inputMethodHints(self);
        return XVariant_create(&value, sizeof(value), XVariantType_Int32);
    }
    case XInputMethodQuery_ImEnabled: {
        /* 对标 Qt 净语义：QLineEdit 路径 WA_InputMethodEnabled 按
           shouldEnableInputMethod()=!isReadOnly() 刷新（qlineedit_p.h:157
           非 Android 口径；qlineedit.cpp:1349），事件分发兜底再乘
           isEnabled()（qwidget.cpp:9057-9065）——合成即
           isEnabled()&&!isReadOnly()，不再硬编码 true。 */
        bool enabled = XWidget_isEnabled(self) &&
                       !XLineEdit_isReadOnly((const XLineEdit*)self);
        return XVariant_create(&enabled, sizeof(enabled), XVariantType_Bool);
    }
    default:
        return NULL;
    }
}

/**
 * @brief      输入法查询虚槽：转发控制器真实状态（对标 QLineEdit::
 *             inputMethodQuery 委托 QWidgetLineControl::inputMethodQuery）。
 * @details    ImCursorRectangle/ImAnchorRectangle 按壳 contents 偏移
 *             （textStartX − m_viewOffset + 控制器 cursorToX）换算为
 *             控件局部矩形，纵向行框与 XLineEdit_cursorRect 同口径
 *             （对标 Qt 把控制器 cursorRect 平移滚动偏移后上送）；
 *             ImSurroundingText/ImCursorPosition/ImAnchorPosition/
 *             ImAbsolutePosition/ImCurrentSelection/ImTextBeforeCursor/
 *             ImTextAfterCursor/ImMaximumTextLength 直取控制器对应状态
 *             ——位置一律字节偏移，与 surroundingText 字节索引同基
 *             （平铺承载约定；壳公开 API 的字符索引口径不用于 IME）。
 *             ImInputItemClipRectangle/ImHints/ImEnabled 走基类默认复刻
 *             （ImEnabled=isEnabled()&&!isReadOnly()，echoMode 派生的
 *             hints 见 XLineEdit_setEchoMode）；其余查询项与控制器缺席
 *             返回 NULL（等价无效 QVariant）。
 */
static XVariant* VXLineEdit_inputMethodQuery(const XWidget* self,
                                             XInputMethodQuery query)
{
    XLineEdit* edit = (XLineEdit*)self;
    XLineControl* ctl;
    if (!edit || !edit->m_control)
        return xlineedit_inputMethodQueryBase(self, query);
    ctl = edit->m_control;
    switch (query) {
    case XInputMethodQuery_ImEnabled: {
        /* 对标 Qt ImEnabled = isEnabled() && !isReadOnly()
         * （qlineedit.cpp:1847-1848；基类默认恒 true，此处按行编辑语义
         * 收紧）。 */
        bool enabled = XWidget_isEnabled(self) &&
                       !XLineControl_isReadOnly(ctl);
        return XVariant_create(&enabled, sizeof(enabled), XVariantType_Bool);
    }
    case XInputMethodQuery_ImReadOnly: {
        /* 对标 Qt ImReadOnly = isReadOnly()（qlineedit.cpp:1874-1875）。 */
        bool ro = XLineControl_isReadOnly(ctl);
        return XVariant_create(&ro, sizeof(ro), XVariantType_Bool);
    }
    case XInputMethodQuery_ImCursorRectangle: {
        XRect rect = XLineEdit_cursorRect(edit);
        XRectF rectF;
        rectF.x = (float)rect.x;
        rectF.y = (float)rect.y;
        rectF.width = (float)rect.width;
        rectF.height = (float)rect.height;
        return XVariant_create(&rectF, sizeof(rectF), XVariantType_User);
    }
    case XInputMethodQuery_ImAnchorRectangle: {
        XRect rect = xlineedit_anchorRectWidget(edit);
        XRectF rectF;
        rectF.x = (float)rect.x;
        rectF.y = (float)rect.y;
        rectF.width = (float)rect.width;
        rectF.height = (float)rect.height;
        return XVariant_create(&rectF, sizeof(rectF), XVariantType_User);
    }
    case XInputMethodQuery_ImCursorPosition: {
        int32_t pos = (int32_t)XLineControl_cursor(ctl);
        return XVariant_create(&pos, sizeof(pos), XVariantType_Int32);
    }
    case XInputMethodQuery_ImAnchorPosition: {
        int32_t anchor = (int32_t)xlineedit_anchorPos(ctl);
        return XVariant_create(&anchor, sizeof(anchor), XVariantType_Int32);
    }
    case XInputMethodQuery_ImAbsolutePosition: {
        /* 单行文档无块结构：绝对位置 == 光标位置（对标 QLineEdit）。 */
        int32_t pos = (int32_t)XLineControl_cursor(ctl);
        return XVariant_create(&pos, sizeof(pos), XVariantType_Int32);
    }
    case XInputMethodQuery_ImSurroundingText:
        /* 对标 QWidgetLineControl ImSurroundingText=m_text；借用串经
           XString_toVariant_utf8 深拷进变体，控件态不被外部持有。 */
        return XString_toVariant_utf8(XLineControl_surroundingText(ctl));
    case XInputMethodQuery_ImCurrentSelection: {
        /* 对标 QWidgetLineControl ImCurrentSelection=selectedText()；
           无选区返回 NULL（等价无效 QVariant，同 Qt 无选区行为）。 */
        char* sel = XLineControl_selectedText(ctl);
        XVariant* var = sel ? XString_toVariant_utf8(sel) : NULL;
        if (sel) XFree_System(sel);
        return var;
    }
    case XInputMethodQuery_ImTextBeforeCursor: {
        const char* text = XLineControl_text(ctl);
        int pos = XLineControl_cursor(ctl);
        int textLen = XLineControl_textEnd(ctl);
        int from;
        char* slice;
        XVariant* var;
        if (!text) return NULL;
        if (pos > textLen) pos = textLen;
        if (pos <= 0) return NULL;
        from = (pos > XLINEEDIT_IM_TEXT_LIMIT)
                   ? pos - XLINEEDIT_IM_TEXT_LIMIT : 0;
        slice = xlineedit_dupRange(text, from, pos - from);
        if (!slice) return NULL;
        var = XString_toVariant_utf8(slice);
        XFree_System(slice);
        return var;
    }
    case XInputMethodQuery_ImTextAfterCursor: {
        const char* text = XLineControl_text(ctl);
        int pos = XLineControl_cursor(ctl);
        int textLen = XLineControl_textEnd(ctl);
        int to;
        char* slice;
        XVariant* var;
        if (!text) return NULL;
        if (pos < 0) pos = 0;
        if (pos > textLen) pos = textLen;
        to = textLen - pos > XLINEEDIT_IM_TEXT_LIMIT
                 ? pos + XLINEEDIT_IM_TEXT_LIMIT
                 : textLen;
        if (to <= pos) return NULL;
        slice = xlineedit_dupRange(text, pos, to - pos);
        if (!slice) return NULL;
        var = XString_toVariant_utf8(slice);
        XFree_System(slice);
        return var;
    }
    case XInputMethodQuery_ImMaximumTextLength: {
        /* 对标 QLineEdit::inputMethodQuery 返回 maximumLength()（数值
           上限）：壳 0=不限按控制器实际承载 32767 上报，避免 0 被消费方
           误读为不允许输入。 */
        int32_t maxLen = (int32_t)XLineEdit_maxLength(edit);
        if (maxLen <= 0) maxLen = XLINEEDIT_UNLIMITED_MAX_LENGTH;
        return XVariant_create(&maxLen, sizeof(maxLen), XVariantType_Int32);
    }
    default:
        return xlineedit_inputMethodQueryBase(self, query);
    }
}
#endif /* XINPUTMETHOD_ON */

XVtable* XLineEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLineEdit)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXLineEdit_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_InputMethodEvent, VXLineEdit_inputMethodEvent);
#if XINPUTMETHOD_ON
    /* 输入法查询虚槽：转发控制器真实状态（对标 QLineEdit::
       inputMethodQuery 委托 QWidgetLineControl；基类兜底只回居中假矩形）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_InputMethodQuery, VXLineEdit_inputMethodQuery);
#endif /* XINPUTMETHOD_ON */
#if XMENU_ON
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ContextMenuEvent,
                             VXLineEdit_contextMenuEvent);
#endif /* XMENU_ON */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent,
                             VXLineEdit_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXLineEdit_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXLineEdit_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXLineEdit_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXLineEdit_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VXLineEdit_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent, VXLineEdit_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXLineEdit_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXLineEdit_changeEvent);
    /* 三击窗口定时器虚槽（对标 QLineEditPrivate tripleClickTimer 的壳侧
       承载；其余定时器交父类）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXLineEdit_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXLineEdit_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXLineEdit_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLineEdit_deinit);

    return XVTABLE_DEFAULT;
}

void XLineEdit_init(XLineEdit* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XLineEdit);

    self->m_control = NULL;
    self->m_placeholder = XString_create();
    self->m_viewOffset = 0;
    self->m_frame = true;
    self->m_alignment = XAlignment_Left;
    self->m_validator = NULL;
    self->m_validatorUserData = NULL;
    self->m_clearButtonEnabled = false;
    self->m_finishedPending = false;
    XMargins_init(&self->m_textMargins, 0, 0, 0, 0);
    /* 内置 action 槽：结构体成员数组，XWidget_init 只清基类部分；不初始化
       的话 m_actionCount 为堆残留垃圾，首帧绘制会解引用野指针（Debug CRT
       cdcd 填充模式直接暴露）。 */
    self->m_actionCount = 0;
    XMemset(self->m_actions, 0, sizeof(self->m_actions));
    XMemset(self->m_actionPositions, 0, sizeof(self->m_actionPositions));
    XRect_init(&self->m_clearButtonRect, 0, 0, 0, 0);
    self->m_tripleClickTimer = XTIMER_INVALID_ID;
    XPoint_init(&self->m_tripleClick, 0, 0);
    /* 编辑控制器：迁入编辑逻辑的私有控制器（对标 Qt d->control）。
       创建即预热（控制器空缓冲缺陷的壳侧规避，见 xlineedit_createControl）。 */
    self->m_control = xlineedit_createControl();
    if (self->m_control) {
        XLineControl_setAccessibleObject(self->m_control, (XObject*)self);
        xlineedit_connectControlSignals(self, self);
        xlineedit_syncControlFont(self);
        /* 密码掩码字符/回显延迟由样式提示注入（对标 QLineEditPrivate::
         * init 的 SH_LineEdit_PasswordCharacter/PasswordMaskDelay）。 */
        xlineedit_applyStylePasswordHints(self);
    }
    /* 对标 qlineedit_p.cpp:231（QLineEditPrivate::init 的
       q->setFocusPolicy(Qt::StrongFocus)）：行编辑可经 Tab 与点击双路
       聚焦（StrongFocus=TabFocus|ClickFocus|0x8，点击位仍在，点击聚焦
       语义不变）。此前 ClickFocus 无 TabFocus 位，XWidget_focusChainCandidate
       永不收录 LE——页3 冷启动 Tab 链=[SpinBox,Slider,nav0..nav8]，LE
       仅能点击进入、可 Tab 出链（复扫-5 路0 N3，final_report #40 残）。 */
    XWidget_setFocusPolicy((XWidget*)self, XWidgetFocusPolicy_StrongFocus);
    xlineedit_updateSizeHints(self);
    /* 悬停外观 opt-in：绘制按 State_MouseOver 出悬停高亮，ENTER/
     * LEAVE 须标脏自矩形（对标 Qt polish 的 WA_Hover 收口）。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_Hover, true);
    /* 输入法接入位：编辑框默认接入 IME（对标 QLineEditPrivate 的
       setAttribute(Qt::WA_InputMethodEnabled, shouldEnableInputMethod())
       ——qlineedit_p.cpp:283/qlineedit_p.h:157 非 Android 口径，init 时刻
       未只读恒 true）。虚拟键盘守护以此位为接受判据之一；opt-out 用
       XWidget_setAttribute(WA_InputMethodEnabled,false)，只读态由
       ImEnabled 查询实时判 false（见 xlineedit_inputMethodQueryBase）。
       内嵌场景（XAbstractSpinBox 内嵌编辑框）经 XLineEdit_create 同样
       携带本位，无需容器重复置位。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_InputMethodEnabled,
                         true);
}

XLineEdit* XLineEdit_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XLineEdit* self = (XLineEdit*)XMemory_malloc(sizeof(XLineEdit), memory);
    if (!self) return NULL;
    XLineEdit_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 文本 API ==================== */

const char* XLineEdit_text(const XLineEdit* self)
{
    if (!self || !self->m_control) return "";
    return XLineControl_text(self->m_control);
}

const char* XLineEdit_displayText(const XLineEdit* self)
{
    const XLineControlTextLayout* layout;
    if (!self || !self->m_control) return "";
    layout = XLineControl_textLayout(self->m_control);
    return (layout && layout->m_text) ? layout->m_text : "";
}

void XLineEdit_setText(XLineEdit* self, const char* text)
{
    if (!self || !text || !self->m_control) return;
    if (XStrcmp(XLineControl_text(self->m_control), text) == 0) return;
    XLineControl_setText(self->m_control, text);
}

void XLineEdit_clear(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    XLineControl_clear(self->m_control);
}

void XLineEdit_insert(XLineEdit* self, const char* utf8)
{
    if (!self || !utf8 || !self->m_control) return;
    /* 与 Qt 一致不设 readOnly 门禁（qlineedit.cpp insert 直通控制器；
     * 只读只拦键盘/IME 路径——分别由控制器 processKeyEvent 与壳
     * inputMethodEvent 守卫）。 */
    XLineControl_insert(self->m_control, utf8);
}

const char* XLineEdit_placeholderText(const XLineEdit* self)
{
    const char* text;
    if (!self || !self->m_placeholder) return "";
    text = XString_toUtf8(self->m_placeholder);
    return (text && text[0]) ? text : "";
}

void XLineEdit_setPlaceholderText(XLineEdit* self, const char* placeholder)
{
    if (!self) return;
    if (!self->m_placeholder) self->m_placeholder = XString_create();
    if (self->m_placeholder)
        XString_assign_utf8(self->m_placeholder, placeholder ? placeholder : "");
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

/* ==================== 编辑属性 ==================== */

bool XLineEdit_isReadOnly(const XLineEdit* self)
{
    if (!self || !self->m_control) return false;
    return XLineControl_isReadOnly(self->m_control);
}

void XLineEdit_setReadOnly(XLineEdit* self, bool readOnly)
{
    if (!self || !self->m_control) return;
    if (XLineControl_isReadOnly(self->m_control) == readOnly) return;
    /* 控制器联动光标闪烁（读写切换后焦点内闪烁启停，对标
     * QWidgetLineControl::setReadOnly 的 updateCursorBlinking）。 */
    XLineControl_setReadOnly(self->m_control, readOnly);
    XWidget_update((XWidget*)self);
}

int XLineEdit_echoMode(const XLineEdit* self)
{
    if (!self || !self->m_control) return (int)XLineEditEchoMode_Normal;
    return (int)XLineControl_echoMode(self->m_control);
}

void XLineEdit_setEchoMode(XLineEdit* self, int echoMode)
{
    if (!self || !self->m_control) return;
    if (XLineControl_echoMode(self->m_control) == (uint32_t)echoMode) return;
    if (echoMode < XLineEditEchoMode_Normal ||
        echoMode > XLineEditEchoMode_PasswordEchoOnEdit)
        return;
    /* 回显模式→输入法提示自标注（对标 qlineedit.cpp:546-559 的
       setFlag 规则，按位翻转保留用户已设提示位）：Password/NoEcho 置
       ImhHiddenText；非 Normal 置 ImhNoAutoUppercase|ImhNoPredictiveText|
       ImhSensitiveData。与 Qt 同序：先刷新 hints 再落控制器回显模式。 */
    {
        XInputMethodHints hints = XWidget_inputMethodHints((XWidget*)self);
        bool hidden = (echoMode == (int)XLineEditEchoMode_Password ||
                       echoMode == (int)XLineEditEchoMode_NoEcho);
        bool masked = (echoMode != (int)XLineEditEchoMode_Normal);
        if (hidden)
            hints |= (uint32_t)XInputMethodHint_HiddenText;
        else
            hints &= ~(uint32_t)XInputMethodHint_HiddenText;
        if (masked)
            hints |= (uint32_t)(XInputMethodHint_NoAutoUppercase |
                                XInputMethodHint_NoPredictiveText |
                                XInputMethodHint_SensitiveData);
        else
            hints &= ~(uint32_t)(XInputMethodHint_NoAutoUppercase |
                                 XInputMethodHint_NoPredictiveText |
                                 XInputMethodHint_SensitiveData);
        XWidget_setInputMethodHints((XWidget*)self, hints);
    }
    /* 控制器：取消密码回显定时器 + 复位编辑态 + 刷新显示（Qt 语义）。 */
    XLineControl_setEchoMode(self->m_control, (uint32_t)echoMode);
    XWidget_update((XWidget*)self);
}

int XLineEdit_maxLength(const XLineEdit* self)
{
    int maxLength;
    if (!self || !self->m_control) return 0;
    maxLength = XLineControl_maxLength(self->m_control);
    /* 控制器以 Qt 默认 32767 表示不限；壳 API 以 0 表示不限。 */
    return (maxLength >= XLINEEDIT_UNLIMITED_MAX_LENGTH) ? 0 : maxLength;
}

void XLineEdit_setMaxLength(XLineEdit* self, int maxLength)
{
    if (!self || !self->m_control || maxLength < 0) return;
    XLineControl_setMaxLength(self->m_control,
                              maxLength == 0
                                  ? XLINEEDIT_UNLIMITED_MAX_LENGTH
                                  : maxLength);
}

int XLineEdit_alignment(const XLineEdit* self)
{
    return self ? self->m_alignment : XAlignment_Left;
}

void XLineEdit_setAlignment(XLineEdit* self, int alignment)
{
    if (!self || self->m_alignment == alignment) return;
    self->m_alignment = alignment;
    XWidget_update((XWidget*)self);
}

bool XLineEdit_hasFrame(const XLineEdit* self)
{
    return self ? self->m_frame : true;
}

void XLineEdit_setFrame(XLineEdit* self, bool on)
{
    if (!self || self->m_frame == on) return;
    self->m_frame = on;
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

void XLineEdit_addAction(XLineEdit* self, XAction* action, int position)
{
    if (!self || !action) return;
    if (position != XLineEditActionPosition_Leading &&
        position != XLineEditActionPosition_Trailing)
        return;
    if (self->m_actionCount >= XLINEEDIT_MAX_ACTIONS) return;
    self->m_actions[self->m_actionCount] = action;
    self->m_actionPositions[self->m_actionCount] = (uint8_t)position;
    ++self->m_actionCount;
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

XAction* XLineEdit_addActionIcon(XLineEdit* self, const char* iconPath,
                                 int position)
{
    XAction* action;
    if (!self) return NULL;
    if (position != XLineEditActionPosition_Leading &&
        position != XLineEditActionPosition_Trailing)
        return NULL;
    /* 对标 QLineEdit::addAction(const QIcon&, ActionPosition)
     * （qlineedit.cpp:436-441）：新建 action → 注册 → 返回所有权。 */
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, "");
    if (!action) return NULL;
    if (iconPath && iconPath[0]) XAction_setIcon_2(action, iconPath);
    XLineEdit_addAction(self, action, position);
    return action;
}

bool XLineEdit_isClearButtonEnabled(const XLineEdit* self)
{
    return self ? self->m_clearButtonEnabled : false;
}

void XLineEdit_setClearButtonEnabled(XLineEdit* self, bool enable)
{
    if (!self || self->m_clearButtonEnabled == enable) return;
    self->m_clearButtonEnabled = enable;
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

void XLineEdit_setValidator(XLineEdit* self, XLineEditValidatorFunc validator,
                            void* userData)
{
    if (!self) return;
    self->m_validator = validator;
    self->m_validatorUserData = userData;
    if (self->m_control) {
        /* 校验器在控制器内单点消费（编辑拒绝回滚 + inputRejected 单一
           发射点）；壳回调经适配钩子桥接，self 恒为壳指针。 */
        XLineControl_setValidator(self->m_control, self,
                                  validator ? xlineedit_validateAdapter : NULL,
                                  validator ? xlineedit_fixupAdapter : NULL,
                                  userData);
    }
}

XLineEditValidatorFunc XLineEdit_validator(const XLineEdit* self)
{
    return self ? self->m_validator : NULL;
}

XSize XLineEdit_sizeHint(const XLineEdit* self)
{
    XSize s;
    size_t chars = 0;
    int w;
    int h;
    if (!self) {
        XSize_init(&s, 0, 0);
        return s;
    }
    {
        const char* raw = xlineedit_ctlRawText(self);
        const char* phText = (self->m_placeholder
                              ? XString_toUtf8(self->m_placeholder) : NULL);
        const XFont* font = &((XWidget*)self)->m_font;
        const char* phPtr = phText ? phText : "";
        int textW;
        int phW;
        int contentW;
        if (raw) chars = xlineedit_charCount(raw);
        if (phText && phText[0]) {
            size_t pc = xlineedit_charCount(phText);
            if (pc > chars) chars = pc;
        }
        /* 首选宽度按真实字体度量（取文本与 placeholder 中较宽者），
           中文双宽不再被按 8px 低估。 */
        textW = xlineedit_displayWidth(font, raw, chars);
        phW = (phText && phText[0])
                  ? xlineedit_displayWidth(font, phPtr,
                                           xlineedit_charCount(phPtr))
                  : 0;
        contentW = textW > phW ? textW : phW;
        w = (int)((self->m_frame ? 8 : 4) + self->m_textMargins.left +
                  self->m_textMargins.right + contentW +
                  ((self->m_clearButtonEnabled) ? 18 : 0));
    }
    if (w < 40) w = 40;
    h = 14 + self->m_textMargins.top + self->m_textMargins.bottom +
        (self->m_frame ? 4 : 2);
    XSize_init(&s, w, h);
    return s;
}

XSize XLineEdit_minimumSizeHint(const XLineEdit* self)
{
    XSize s;
    int w;
    int h;
    if (!self) {
        XSize_init(&s, 0, 0);
        return s;
    }
    w = (int)((self->m_frame ? 8 : 4) + self->m_textMargins.left +
              self->m_textMargins.right + XLINEEDIT_CHAR_W +
              ((self->m_clearButtonEnabled) ? 18 : 0));
    if (w < 24) w = 24;
    h = 14 + self->m_textMargins.top + self->m_textMargins.bottom +
        (self->m_frame ? 4 : 2);
    XSize_init(&s, w, h);
    return s;
}

XRect XLineEdit_cursorRect(const XLineEdit* self)
{
    XRect rect;
    int tx;
    int ty;
    int cx;
    XMemset(&rect, 0, sizeof(rect));
    if (!self || !self->m_control) return rect;
    {
        const XFont* font = &((XWidget*)self)->m_font;
        int lineH = XPainter_textHeight(font);
        if (lineH < 14) lineH = 14;
        /* 光标→X 由控制器提供（显示坐标），壳做 contents 偏移换算
           （textStartX 与滚动偏移）；矩形形状与迁移前一致（1px 宽）。 */
        xlineedit_syncControlFont((XLineEdit*)self);
        ty = (XWidget_height((XWidget*)self) - lineH) / 2;
        tx = xlineedit_textStartX(self);
        cx = tx - self->m_viewOffset +
             XLineControl_cursorToXCurrent(self->m_control);
        rect.x = cx;
        rect.y = ty + 1;
        rect.width = XLINEEDIT_CURSOR_W;
        rect.height = lineH - 2;
    }
    return rect;
}

int XLineEdit_cursorPosition(const XLineEdit* self)
{
    if (!self || !self->m_control) return 0;
    /* 控制器存字节偏移；公开 API 为字符索引（迁移前口径）。 */
    return (int)xlineedit_charCountPrefix(
        xlineedit_ctlRawText(self),
        (size_t)XLineControl_cursor(self->m_control));
}

void XLineEdit_setCursorPosition(XLineEdit* self, int position)
{
    size_t chars;
    size_t bytePos;
    const char* raw;
    if (!self || !self->m_control) return;
    if (position < 0) position = 0;
    /* 字符索引 → 字节偏移（对标迁移前公开口径），控制器按字节移动。 */
    raw = xlineedit_ctlRawText(self);
    chars = xlineedit_charCount(raw);
    if ((size_t)position > chars) position = (int)chars;
    bytePos = xlineedit_charIndexToByte(raw, (size_t)position);
    XLineControl_setCursorPosition(self->m_control, (int)bytePos);
}

int XLineEdit_cursorPositionAt(const XLineEdit* self, const XPoint* pos)
{
    int clickX;
    int bytePos;
    if (!self || !self->m_control) return 0;
    /* 壳做 contents 平移 → 控制器 xToPos → 字符索引（迁移前口径）。 */
    xlineedit_syncControlFont((XLineEdit*)self);
    clickX = (pos ? pos->x : 0) - xlineedit_textStartX(self) +
             self->m_viewOffset;
    bytePos = XLineControl_xToPos(self->m_control, clickX,
                                  (int)XLineControlCursorPosition_BetweenCharacters);
    return (int)xlineedit_charCountPrefix(xlineedit_ctlRawText(self),
                                          (size_t)bytePos);
}

/* ==================== 光标移动与编辑键 ==================== */

void XLineEdit_cursorForward(XLineEdit* self, bool mark, int steps)
{
    if (!self || !self->m_control) return;
    XLineControl_cursorForward(self->m_control, mark, steps);
}

void XLineEdit_cursorBackward(XLineEdit* self, bool mark, int steps)
{
    if (!self || !self->m_control) return;
    XLineControl_cursorForward(self->m_control, mark, -steps);
}

void XLineEdit_cursorWordForward(XLineEdit* self, bool mark)
{
    if (!self || !self->m_control) return;
    XLineControl_cursorWordForward(self->m_control, mark);
}

void XLineEdit_cursorWordBackward(XLineEdit* self, bool mark)
{
    if (!self || !self->m_control) return;
    XLineControl_cursorWordBackward(self->m_control, mark);
}

void XLineEdit_backspace(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    /* 与 Qt 一致不设 readOnly 门禁（qlineedit.cpp backspace 直通控制
     * 器；键盘路径由控制器 processKeyEvent 守卫）。 */
    XLineControl_backspace(self->m_control);
}

void XLineEdit_del(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    /* 与 Qt 一致不设 readOnly 门禁（同 backspace）。 */
    XLineControl_del(self->m_control);
}

void XLineEdit_home(XLineEdit* self, bool mark)
{
    if (!self || !self->m_control) return;
    XLineControl_home(self->m_control, mark);
}

void XLineEdit_end(XLineEdit* self, bool mark)
{
    if (!self || !self->m_control) return;
    XLineControl_end(self->m_control, mark);
}

/* ==================== 修改状态 ==================== */

bool XLineEdit_isModified(const XLineEdit* self)
{
    if (!self || !self->m_control) return false;
    return XLineControl_isModified(self->m_control);
}

void XLineEdit_setModified(XLineEdit* self, bool modified)
{
    if (!self || !self->m_control) return;
    XLineControl_setModified(self->m_control, modified);
}

/* ==================== 选区 ==================== */

void XLineEdit_setSelection(XLineEdit* self, int start, int length)
{
    const char* raw;
    int s;
    if (!self || !self->m_control) return;
    /* start 为字符索引（合法域 [0,字符数]，越界整个调用忽略——对标 Qt
       qlineedit.cpp setSelection 的 Q_UNLIKELY 分支，控制器同样告警并
       忽略）；length 为字符数（可负）；控制器以字节偏移承载 start。 */
    if (start < 0) return;
    raw = xlineedit_ctlRawText(self);
    if ((size_t)start > xlineedit_charCount(raw)) return;
    s = (int)xlineedit_charIndexToByte(raw, (size_t)start);
    XLineControl_setSelection(self->m_control, s, length);
}

bool XLineEdit_hasSelectedText(const XLineEdit* self)
{
    if (!self || !self->m_control) return false;
    return XLineControl_hasSelectedText(self->m_control);
}

char* XLineEdit_selectedText(const XLineEdit* self)
{
    if (!self || !self->m_control) return NULL;
    return XLineControl_selectedText(self->m_control);
}

int XLineEdit_selectionStart(const XLineEdit* self)
{
    int bytes;
    if (!self || !self->m_control) return -1;
    bytes = XLineControl_selectionStart(self->m_control);
    if (bytes < 0) return -1;
    /* 对标 Qt：字符位置口径（与 cursorPosition 同单位），控制器存
       字节偏移，按前缀字符数换算（此前直返字节，与组内 API 单位
       不一致）。 */
    return (int)xlineedit_charCountPrefix(xlineedit_ctlRawText(self),
                                          (size_t)bytes);
}

int XLineEdit_selectionEnd(const XLineEdit* self)
{
    int bytes;
    if (!self || !self->m_control) return -1;
    bytes = XLineControl_selectionEnd(self->m_control);
    if (bytes < 0) return -1;
    return (int)xlineedit_charCountPrefix(xlineedit_ctlRawText(self),
                                          (size_t)bytes);
}

int XLineEdit_selectionLength(const XLineEdit* self)
{
    int s;
    int e;
    const char* raw;
    if (!self || !self->m_control) return 0;
    if (!XLineControl_hasSelectedText(self->m_control)) return 0;
    s = XLineControl_selectionStart(self->m_control);
    e = XLineControl_selectionEnd(self->m_control);
    raw = xlineedit_ctlRawText(self);
    return (int)(xlineedit_charCountPrefix(raw, (size_t)e) -
                 xlineedit_charCountPrefix(raw, (size_t)s));
}

void XLineEdit_deselect(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    XLineControl_deselect(self->m_control);
}

void XLineEdit_selectAll(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    XLineControl_selectAll(self->m_control);
}

/* ==================== 撤销/重做 ==================== */

bool XLineEdit_isUndoAvailable(const XLineEdit* self)
{
    if (!self || !self->m_control) return false;
    return XLineControl_isUndoAvailable(self->m_control);
}

bool XLineEdit_isRedoAvailable(const XLineEdit* self)
{
    if (!self || !self->m_control) return false;
    return XLineControl_isRedoAvailable(self->m_control);
}

void XLineEdit_undo(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    XLineControl_undo(self->m_control);
}

void XLineEdit_redo(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    XLineControl_redo(self->m_control);
}

/* ==================== 剪贴板 ==================== */

void XLineEdit_cut(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    /* 对标 Qt cut（qlineedit.cpp:1377-1383）：有选区才 copy+del，无
     * readOnly 门禁（copy 通道在密码类回显下防泄漏不写剪贴板）。 */
    if (!XLineControl_hasSelectedText(self->m_control)) return;
    XLineControl_copy(self->m_control, (int)XClipboardMode_Clipboard);
    XLineControl_del(self->m_control);
}

void XLineEdit_copy(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    if (!XLineControl_hasSelectedText(self->m_control)) return;
    XLineControl_copy(self->m_control, (int)XClipboardMode_Clipboard);
}

void XLineEdit_paste(XLineEdit* self)
{
    if (!self || !self->m_control) return;
    /* 与 Qt 一致不设 readOnly 门禁（qlineedit.cpp paste 直通控制器）。 */
    XLineControl_paste(self->m_control, (int)XClipboardMode_Clipboard);
}

/* ==================== 其他属性 ==================== */

bool XLineEdit_dragEnabled(const XLineEdit* self)
{
    if (!self || !self->m_control) return false;
    return XLineControl_dragEnabled(self->m_control);
}

void XLineEdit_setDragEnabled(XLineEdit* self, bool b)
{
    if (!self || !self->m_control) return;
    XLineControl_setDragEnabled(self->m_control, b);
}

int XLineEdit_cursorMoveStyle(const XLineEdit* self)
{
    if (!self || !self->m_control)
        return XLineEditCursorMoveStyle_LogicalMoveStyle;
    return XLineControl_cursorMoveStyle(self->m_control);
}

void XLineEdit_setCursorMoveStyle(XLineEdit* self, int style)
{
    if (!self || !self->m_control) return;
    XLineControl_setCursorMoveStyle(self->m_control, style);
}

const char* XLineEdit_inputMask(const XLineEdit* self)
{
    if (!self || !self->m_control) return "";
    return XLineControl_inputMask(self->m_control);
}

void XLineEdit_setInputMask(XLineEdit* self, const char* inputMask)
{
    if (!self || !self->m_control) return;
    if (!inputMask) inputMask = "";
    if (XStrcmp(XLineControl_inputMask(self->m_control), inputMask) == 0)
        return;
    XLineControl_setInputMask(self->m_control, inputMask);
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

bool XLineEdit_hasAcceptableInput(const XLineEdit* self)
{
    if (!self || !self->m_control) return false;
    /* 对标 Qt：无校验器/掩码时空文本即可接受（此前"空文本恒 false"
     * 为迁移前口径，与 Qt 的 hasAcceptableInput 语义相反）。 */
    return XLineControl_hasAcceptableInput(self->m_control);
}

void XLineEdit_setTextMargins(XLineEdit* self, int left, int top,
                              int right, int bottom)
{
    if (!self) return;
    self->m_textMargins.left = left;
    self->m_textMargins.top = top;
    self->m_textMargins.right = right;
    self->m_textMargins.bottom = bottom;
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

void XLineEdit_setTextMargins_2(XLineEdit* self, const XMargins* margins)
{
    if (!self) return;
    if (margins)
        self->m_textMargins = *margins;
    else
        XMargins_init(&self->m_textMargins, 0, 0, 0, 0);
    XWidget_update((XWidget*)self);
    xlineedit_updateSizeHints(self);
}

XMargins XLineEdit_textMargins(const XLineEdit* self)
{
    XMargins m;
    XMargins_init(&m, 0, 0, 0, 0);
    if (self) m = self->m_textMargins;
    return m;
}

/* ==================== 补全器（对标 QLineEdit completer/setCompleter） ==================== */

void XLineEdit_setCompleter(XLineEdit* self, XCompleter* completer)
{
    XCompleter* old;
    if (!self) return;
    old = XLineEdit_completer(self);
    if (old == completer) return;
    /* 补全器借用指针由控制器持有（complete()/processKeyEvent 联动）。 */
    if (self->m_control)
        XLineControl_setCompleter(self->m_control, completer);
#if XTABLEWIDGET_ON
    /* 对标 Qt：安装时 completer->setWidget(this)，供弹出定位与焦点判断
       使用；解绑/替换时把仍指向本编辑框的原补全器关联位清空，避免悬挂
       借用指针。 */
    if (completer) {
        XCompleter_setWidget(completer, (XWidget*)self);
    } else if (old && XCompleter_widget(old) == (XWidget*)self) {
        XCompleter_setWidget(old, NULL);
    }
#endif /* XTABLEWIDGET_ON */
}

XCompleter* XLineEdit_completer(const XLineEdit* self)
{
    if (!self || !self->m_control) return NULL;
    return (XCompleter*)XLineControl_completer(self->m_control);
}

/* ==================== 信号 ==================== */

void* XLineEdit_textChanged_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_textChanged_signal;
}
void* XLineEdit_textEdited_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_textEdited_signal;
}
void* XLineEdit_cursorPositionChanged_signal(XLineEdit* self, int oldPos,
                                             int newPos)
{
    (void)self;
    (void)oldPos;
    (void)newPos;
    return (void*)(size_t)XLineEdit_cursorPositionChanged_signal;
}
void* XLineEdit_returnPressed_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_returnPressed_signal;
}
void* XLineEdit_editingFinished_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_editingFinished_signal;
}
void* XLineEdit_selectionChanged_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_selectionChanged_signal;
}
void* XLineEdit_inputRejected_signal(XLineEdit* self)
{
    (void)self;
    return (void*)(size_t)XLineEdit_inputRejected_signal;
}

XLineEdit* XLineEdit_focusedLineEdit(void)
{
    return g_focusedLineEdit;
}

#endif /* XWIDGET_ON && XLINEEDIT_ON */

#if XMENU_ON
/* ==================== 标准右键菜单（对标 QLineEdit::
   createStandardContextMenu / contextMenuEvent） ==================== */

/* 标准菜单构建共享化：动作触发槽与连接辅助在共享文本工具层 XTextMenu
   （XTextMenu_createStandard）；本控件只提供 XTextMenuOps 适配槽，槽体
   经壳公开 API 委托控制器（一行转发），动作集合与灰化条件逐一对应：
   - readOnly 时撤销/重做/剪切/粘贴/删除槽置 NULL（菜单不出现这些项，
     仅保留复制/全选）；
   - 剪切/复制灰化 = 有选区且 Normal 回显（hasSel && echoNormal）；
   - 粘贴灰化 = 剪贴板有非空文本（hasClip）；
   - 删除灰化 = 有文本且有选区（hasText && hasSel）；
   - 全选灰化 = 有文本且未全选（hasText && !allSelected）。 */

/** @brief ops 适配槽：撤销/重做。 */
static void xlineedit_menuOpUndo(void* ud)
{
    XLineEdit_undo((XLineEdit*)ud);
}

static bool xlineedit_menuOpCanUndo(void* ud)
{
    return XLineEdit_isUndoAvailable((XLineEdit*)ud);
}

static void xlineedit_menuOpRedo(void* ud)
{
    XLineEdit_redo((XLineEdit*)ud);
}

static bool xlineedit_menuOpCanRedo(void* ud)
{
    return XLineEdit_isRedoAvailable((XLineEdit*)ud);
}

/** @brief ops 适配槽：剪切/复制（灰化 = 有选区且 Normal 回显）。 */
static void xlineedit_menuOpCut(void* ud)
{
    XLineEdit_cut((XLineEdit*)ud);
}

static bool xlineedit_menuOpCanCutCopy(void* ud)
{
    XLineEdit* edit = (XLineEdit*)ud;
    return XLineEdit_hasSelectedText(edit) &&
           XLineEdit_echoMode(edit) == (int)XLineEditEchoMode_Normal;
}

static void xlineedit_menuOpCopy(void* ud)
{
    XLineEdit_copy((XLineEdit*)ud);
}

/** @brief ops 适配槽：粘贴（灰化 = 剪贴板有非空文本）。 */
static void xlineedit_menuOpPaste(void* ud)
{
    XLineEdit_paste((XLineEdit*)ud);
}

static bool xlineedit_menuOpCanPaste(void* ud)
{
    const char* clip = XTextClipboard_getText();
    (void)ud;
    return clip && clip[0] != '\0';
}

/** @brief ops 适配槽：删除选中文本（对标
 *         QWidgetLineControl::_q_deleteSelected；灰化 = 有文本且有选区）。 */
static void xlineedit_menuOpDel(void* ud)
{
    XLineEdit_del((XLineEdit*)ud);
}

static bool xlineedit_menuOpCanDel(void* ud)
{
    XLineEdit* edit = (XLineEdit*)ud;
    return XLineEdit_text(edit)[0] != '\0' &&
           XLineEdit_hasSelectedText(edit);
}

/** @brief ops 适配槽：全选（灰化 = 有文本且未全选）。 */
static void xlineedit_menuOpSelectAll(void* ud)
{
    XLineEdit_selectAll((XLineEdit*)ud);
}

static bool xlineedit_menuOpCanSelectAll(void* ud)
{
    XLineEdit* edit = (XLineEdit*)ud;
    if (!edit || !edit->m_control) return false;
    return XLineEdit_text(edit)[0] != '\0' &&
           !XLineControl_allSelected(edit->m_control);
}

XMenu* XLineEdit_createStandardContextMenu(XLineEdit* self)
{
    XTextMenuOps ops;
    bool readOnly;
    if (!self) return NULL;
    readOnly = XLineEdit_isReadOnly(self);
    XMemset(&ops, 0, sizeof(ops));
    ops.ud = self;
    /* readOnly 时仅提供复制/全选（其余槽保持 NULL，菜单不出现）。 */
    if (!readOnly) {
        ops.undo = xlineedit_menuOpUndo;
        ops.canUndo = xlineedit_menuOpCanUndo;
        ops.redo = xlineedit_menuOpRedo;
        ops.canRedo = xlineedit_menuOpCanRedo;
        ops.cut = xlineedit_menuOpCut;
        ops.canCut = xlineedit_menuOpCanCutCopy;
        ops.paste = xlineedit_menuOpPaste;
        ops.canPaste = xlineedit_menuOpCanPaste;
        ops.del = xlineedit_menuOpDel;
        ops.canDel = xlineedit_menuOpCanDel;
    }
    ops.copy = xlineedit_menuOpCopy;
    ops.canCopy = xlineedit_menuOpCanCutCopy;
    ops.selectAll = xlineedit_menuOpSelectAll;
    ops.canSelectAll = xlineedit_menuOpCanSelectAll;
    return XTextMenu_createStandard(&ops);
}
#endif /* XMENU_ON */
