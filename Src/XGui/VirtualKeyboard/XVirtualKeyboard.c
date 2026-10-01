/**
 * @file       XVirtualKeyboard.c
 * @brief      XVirtualKeyboard 屏幕虚拟键盘控件实现（对标 LVGL 9.2.2
 *             lv_keyboard + lv_buttonmatrix 语义）。
 * @details    与公开头 XVirtualKeyboard.h 的公共 API 一一对应；实现要点：
 *             - 矩阵行为内聚：键位布局解析（xkb_rebuildLayout，每行按
 *               自身单位总数归一化铺满行宽，对标 lv_buttonmatrix.c 归
 *               一化公式）、命中测试（xkb_hitTest，O(n) 查
 *               m_keyRects[]）、按钮激活分发（xkb_activateButton，控
 *               制键文本匹配 + 字符写入 + 三信号发射）全部为本文件
 *               static 私有函数，不单拆公开按钮矩阵控件；
 *             - 触发时序（对标 lv_buttonmatrix）：默认按下触发；ctrl
 *               含 CLICK_TRIG 或 POPOVER 时释放触发；按住滑动出键取消
 *               武装不触发、回滑恢复；NO_REPEAT 未设的键按住
 *               XKEYBOARD_LONG_PRESS_START_MS 起以 XKEYBOARD_REPEAT_MS
 *               重复（XObject_startTimer_ms 经 EXObject_TimerEvent 虚
 *               槽接收 tick，非本类定时器 id 链回基类）；
 *             - 写入链（对标 lv_keyboard → textarea 单向调用）：单字节
 *               ASCII 键合成 XKeyEvent 经 XObject_event_base 直发目标
 *               （与真实输入同路径，大写字母键自动携带 ShiftModifier
 *               ——xlc_keyToText 把 a-z 归一为大写后按 Shift 位决定输
 *               出大小写）；多字节 UTF-8 键按目标 vtable 分派公开插入
 *               API 直写（XLineEdit_insert/XPlainTextEdit_insertPlainText/
 *               XTextEdit_insertPlainText，各自受 X*_ON 开关门控）；
 *             - 联动机制（XGui 适配）：XGui 事件过滤器对本控件全部关
 *               键事件不可达（XObject notify 仅遍历接收者自身过滤器，
 *               FOCUS_IN/输入桥直投 event_base），弹层联动一律走守护
 *               定时器轮询（xkb_guardTick：焦点跟随/编辑框隐藏/宿主
 *               Resize 三查）+ XObject_destroyed_signal 防悬垂连接，
 *               与 XCompleter 守护轮询先例同型；
 *             - 适配层分支守卫：XLineEdit/XPlainTextEdit/XTextEdit 的
 *               识别与直写各自受 XLINEEDIT_ON/XPLAINTEXTEDIT_ON/
 *               XTEXTEDIT_ON 门控；三开关全 0 时键盘核心（布局/绘制/
 *               弹层/信号/合成键写入）仍可用，setTextArea 接受任意
 *               XWidget*（合成键路径类型无关），仅直写回退与类型识别
 *               降级（xkb_supportedTarget 恒 true）；
 *             - 拼音 IME 接线（XVIRTUALKEYBOARD_ON 门控）：输入状态
 *               机零直连（m_ime 成员删除，插件独占）——四条触发路径
 *               （按下/释放/长按重复/复用入口）全部汇经
 *               xkb_activateButton，单点经 xkb_routeKey 投 engine 虚
 *               键（拼音插件消费：中文态 [a-z] 进组串/数字选候选/
 *               空格回车上屏/退格删组串；中/EN 与数字分页拦截在面板
 *               本地），未消费按键原语义放行；候选（词组+单字混排，
 *               变宽 chip）数据源=engine.wordCandidateListModel
 *               （dataAt 取 XVariant*）、组串显示=context.preeditText
 *               （状态镜像）、分页=面板本地状态；带三件套
 *               （xkb_imeBandLayout/Paint/Hit）共用同一几何，布局期
 *               预留顶行、复用气泡带预算，命中在 xkb_hitTest 之前直
 *               触发不进武装链；提交/虚键=context 公共信号
 *               commitRequested/keyEventRequested→既有写入链；
 *             - 主题化（单一绘制路径）：LVGL 9.2.2 默认主题浅色模板
 *               的键面圆角矩形/按压颜色数学/候选带配色全部收在本文
 *               件局部常量（XKB_LVGL_*）；XKEYBOARD_THEME_LVGL_ON 宏
 *               已删除（双分支合并为 LVGL 单路径，调色板旧分支移除），
 *               布局常量/命中几何不变。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#include "XStringUtils.h"

#if XWIDGET_ON && XKEYBOARD_ON

#include "XVirtualKeyboard.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XString.h"
#include "XPrintf.h"
#include "XPainter.h"
#if XWINDOW_ON
#include "XWindow.h" /* 悬浮面板接管原生鼠标捕获（XWindow_setMouseGrabEnabled）。 */
#endif
#if XAPPLICATION_ON
#include "XApplication.h" /* 悬浮锚主窗口解析：应用顶层表（XCalendarWidget 弹层容器锚）。 */
#include "XVector.h"      /* XApplication_topLevelWidgets 返回向量遍历/释放。 */
#endif
#if XLINEEDIT_ON
#include "XLineEdit.h"
#endif /* XLINEEDIT_ON */
#if XPLAINTEXTEDIT_ON
#include "XPlainTextEdit.h"
#endif /* XPLAINTEXTEDIT_ON */
#if XTEXTEDIT_ON
#include "XTextEdit.h"
#endif /* XTEXTEDIT_ON */
#if XKEYBOARD_IME_ON
#include "XPinyinEngine.h" /* 拼音布局静态表（XPinyinEngine_map/ctrlMap）。 */
#endif
#if XKEYBOARD_IME_PHRASE_ON
#include "XPinyinPhrase.h" /* 词组库懒加载（setImeEnabled(true)）。 */
#endif
#if XVIRTUALKEYBOARD_ON
#include "XVirtualKeyboardInputContext.h"
#include "XVirtualKeyboardInputContext_Protected.h"
#include "XVirtualKeyboardInputEngine.h"
#include "XVirtualKeyboardSelectionListModel.h"
#include "XVirtualKeyboardSelectionListModel_Protected.h"
#include "XVirtualKeyboardSettings.h"
#include "XVirtualKeyboardObserver.h"
#include "XVirtualKeyboardObserver_Protected.h"
#include "XVirtualKeyboardPinyinInputMethod.h"
#include "XVirtualKeyboardPlainInputMethod.h"
#endif /* XVIRTUALKEYBOARD_ON */

/* 长按重复节拍宏的双世界兜底：XVK=1 时由引擎头（#ifndef 门控，先包含
   者定义）给出 600/50；XVK=0 时引擎头整体裁剪、而 startRepeat/repeatTick
   的面板本地重复承载仍在（守护轮询形态），此处按同值兜底——两世界编
   译一致（独立评审②：VK=0 /Zs 实证 C2065）。 */
#ifndef XVIRTUALKEYBOARD_REPEAT_FIRST_MS
#define XVIRTUALKEYBOARD_REPEAT_FIRST_MS 600
#endif
#ifndef XVIRTUALKEYBOARD_REPEAT_MS
#define XVIRTUALKEYBOARD_REPEAT_MS 50
#endif

/* ==================== 内置静态映射表（对标 lv_keyboard.c:87-206） ==================== */

/** @brief 字符键控制字（宽度单位 | POPOVER 位，对标 LV_KB_BTN）。 */
#define XKB_BTN(w) ((XKeyboardButtonCtrl)((w) | (int)XKEYBOARD_CTRL_POPOVER))
/** @brief 选中样式字符键（CHECKED|POPOVER|宽度，对标 LVGL
 *         LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(w)，文本布局第 3 行）。 */
#define XKB_CHECKED_BTN(w) ((XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | (w) | (int)XKEYBOARD_CTRL_POPOVER))
/** @brief 控制键标志组合（NO_REPEAT|CLICK_TRIG|CHECKED）。 */
#define XKB_FLAGS ((int)XKEYBOARD_CTRL_BUTTON_FLAGS)

/* TEXT_LOWER：4 行 40 键（逐键复刻 LVGL 9.2.2 默认小写布局）。 */
static const char* const s_kbMapTextLower[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
    XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_UPPER, "a", "s", "d", "f", "g", "h", "j", "k", "l",
    XKEYBOARD_LBL_NEWLINE, "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    XKEYBOARD_LBL_DISMISS, XKEYBOARD_LBL_LEFT, " ", XKEYBOARD_LBL_RIGHT,
    XKEYBOARD_LBL_OK,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlTextLower[] = {
    (XKeyboardButtonCtrl)(XKB_FLAGS | 5),                       /* 1# */
    XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), /* q..t */
    XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), /* y..p */
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 7),     /* 退格 */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 6),                       /* ABC */
    XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), /* a..e */
    XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3),             /* f..l */
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 7),     /* 换行 */
    XKB_CHECKED_BTN(1),                                         /* _ */
    XKB_CHECKED_BTN(1),                                         /* - */
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), /* z..b */
    XKB_BTN(1), XKB_BTN(1),                                     /* n m */
    XKB_CHECKED_BTN(1),                                         /* . */
    XKB_CHECKED_BTN(1),                                         /* , */
    XKB_CHECKED_BTN(1),                                         /* : */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 收起 */
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),     /* <- */
    (XKeyboardButtonCtrl)6,                                     /* 空格 */
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),     /* -> */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2)                        /* 确认 */
};

/* TEXT_UPPER：与 LOWER 完全同构，字母全大写；行 2 切换键 abc、行 4 首
 * 键关闭（对标 LVGL 差异位）。 */
static const char* const s_kbMapTextUpper[] = {
    "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
    XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_LOWER, "A", "S", "D", "F", "G", "H", "J", "K", "L",
    XKEYBOARD_LBL_NEWLINE, "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    XKEYBOARD_LBL_CLOSE, XKEYBOARD_LBL_LEFT, " ", XKEYBOARD_LBL_RIGHT,
    XKEYBOARD_LBL_OK,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlTextUpper[] = {
    (XKeyboardButtonCtrl)(XKB_FLAGS | 5),
    XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4),
    XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 7),
    (XKeyboardButtonCtrl)(XKB_FLAGS | 6),
    XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3),
    XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 7),
    XKB_CHECKED_BTN(1),                                         /* _ */
    XKB_CHECKED_BTN(1),                                         /* - */
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1),
    XKB_CHECKED_BTN(1),                                         /* . */
    XKB_CHECKED_BTN(1),                                         /* , */
    XKB_CHECKED_BTN(1),                                         /* : */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    (XKeyboardButtonCtrl)6,
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2)
};

/* SPECIAL：4 行；行 1 数字+退格、行 2/3 符号、行 4 同 LOWER 行 4。 */
static const char* const s_kbMapSpecial[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_LOWER, "+", "&", "/", "*", "=", "%", "!", "?", "#",
    "<", ">", "\n",
    "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
    XKEYBOARD_LBL_DISMISS, XKEYBOARD_LBL_LEFT, " ", XKEYBOARD_LBL_RIGHT,
    XKEYBOARD_LBL_OK,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlSpecial[] = {
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),     /* 退格 */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* abc */
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1),
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    (XKeyboardButtonCtrl)6,
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2)
};

/* NUMBER：4 行 4 列；数字模式无 abc/1# 切换键，收起键发 CANCEL 非切回
 * 文本；退格仅宽度无控制标志（按住可重复，对标 KB.c:166-177）。 */
static const char* const s_kbMapNumber[] = {
    "1", "2", "3", XKEYBOARD_LBL_DISMISS, "\n",
    "4", "5", "6", XKEYBOARD_LBL_OK, "\n",
    "7", "8", "9", XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_SIGN, "0", ".", XKEYBOARD_LBL_LEFT, XKEYBOARD_LBL_RIGHT,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlNumber[] = {
    /* 逐位对齐 LVGL default_kb_ctrl_num_map（KB.c:172-177）：数字键与
     * 行 4 五键为裸宽度 1，仅收起/确认带控制标志、退格仅宽度可重复。 */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 收起 */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 确认 */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)2,                                     /* 退格（可重复） */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1,                                     /* +/- */
    (XKeyboardButtonCtrl)1,                                     /* 0 */
    (XKeyboardButtonCtrl)1,                                     /* . */
    (XKeyboardButtonCtrl)1,                                     /* <- */
    (XKeyboardButtonCtrl)1                                      /* -> */
};

/* DIGITS：VK 对齐新增合成布局（12 键 4 行；hints DigitsOnly 映射目
 * 标，对标 fallback/digits.qml 口径——数字 3 列 + 退格/确认/收起/光
 * 标控制键，键语义全部复用既有分发）。 */
static const char* const s_kbMapDigits[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    XKEYBOARD_LBL_BACKSPACE, "0", XKEYBOARD_LBL_OK,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlDigits[] = {
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)2,                                     /* 退格（可重复） */
    (XKeyboardButtonCtrl)2,                                     /* 0（可重复） */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2)                        /* 确认 */
};

/* DIALPAD：VK 对齐新增合成布局（3x4；hints DialableCharactersOnly 昂
 * 射目标，对标 fallback/dialpad.qml 口径——* 0 # 底行，12 键电话拨
 * 号盘特征）。 */
static const char* const s_kbMapDialpad[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "*", "0", "#",
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlDialpad[] = {
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1,                                     /* *（符号直写） */
    (XKeyboardButtonCtrl)2,                                     /* 0 */
    (XKeyboardButtonCtrl)1                                      /* #（符号直写） */
};

/* 槽位容器：0..3 内置、4..7（USER_1..4）默认回落小写表（对标
 * KB.c:179-206）、8..9 VK 合成布局、末位 NULL（经 init 装入实例槽位
 * 数组）。 */
static const char* const* const s_kbMapDefaults[XKEYBOARD_MODE_SLOT_COUNT] = {
    s_kbMapTextLower, s_kbMapTextUpper, s_kbMapSpecial, s_kbMapNumber,
    s_kbMapTextLower, s_kbMapTextLower, s_kbMapTextLower, s_kbMapTextLower,
    s_kbMapDigits, s_kbMapDialpad
};
static const XKeyboardButtonCtrl* const s_kbCtrlDefaults[XKEYBOARD_MODE_SLOT_COUNT] = {
    s_kbCtrlTextLower, s_kbCtrlTextUpper, s_kbCtrlSpecial, s_kbCtrlNumber,
    s_kbCtrlTextLower, s_kbCtrlTextLower, s_kbCtrlTextLower, s_kbCtrlTextLower,
    s_kbCtrlDigits, s_kbCtrlDialpad
};

/* ==================== 前向声明 ==================== */
static void VXKeyboard_paintEvent(XWidget* self, XEvent* event);
static void VXKeyboard_resizeEvent(XWidget* self, XEvent* event);
static void VXKeyboard_mousePressEvent(XWidget* self, XEvent* event);
static void VXKeyboard_mouseReleaseEvent(XWidget* self, XEvent* event);
static void VXKeyboard_mouseMoveEvent(XWidget* self, XEvent* event);
static void VXKeyboard_hideEvent(XWidget* self, XEvent* event);
static void VXKeyboard_timerEvent(XObject* object, XTimerEvent* event);
static void VXKeyboard_deinit(XVirtualKeyboard* self);
static void VXKeyboard_copy(XVirtualKeyboard* self, const XVirtualKeyboard* other);
static void VXKeyboard_move(XVirtualKeyboard* self, XVirtualKeyboard* other);
static void xkb_rebuildLayout(XVirtualKeyboard* self);
static uint32_t xkb_hitTest(const XVirtualKeyboard* self, int x, int y);
static bool xkb_activateButton(XVirtualKeyboard* self, uint32_t buttonId);
static void xkb_writeText(XVirtualKeyboard* self, const char* text);
static void xkb_sendKey(XVirtualKeyboard* self, int key,
                        XKeyboardModifiers modifiers);
static void xkb_signFlip(XVirtualKeyboard* self);
static void xkb_startRepeat(XVirtualKeyboard* self);
static void xkb_repeatTick(XVirtualKeyboard* self);
static void xkb_guardTick(XVirtualKeyboard* self);
static void xkb_reposition(XVirtualKeyboard* self);
static void xkb_bindTargetDestroyed(XVirtualKeyboard* self, XWidget* newTarget);
static void xkb_bindHostDestroyed(XVirtualKeyboard* self, XWidget* newHost);
static void xkb_stopRepeat(XVirtualKeyboard* self);
static void xkb_stopGuard(XVirtualKeyboard* self);
#if XVIRTUALKEYBOARD_ON
static bool xkb_routeKey(XVirtualKeyboard* self, const char* text);
static void xkb_applyFocusHints(XVirtualKeyboard* self);
static void xkb_syncShiftMode(XVirtualKeyboard* self);
static void xkb_notifyObserver(const XVirtualKeyboard* self);
static void xkb_bindContextConns(XVirtualKeyboard* self, bool bind);
static void xkb_commitSlot(XObject* receiver, XVarList* args);
static void xkb_keyEventSlot(XObject* receiver, XVarList* args);
#endif

/* ==================== 内部辅助 ==================== */

#if XVIRTUALKEYBOARD_ON

/** @brief 取进程单例输入上下文（惰性；未初始化返回 NULL）。 */
static XVirtualKeyboardInputContext* xkb_contextOf(const XVirtualKeyboard* self)
{
    (void)self;
    return XVirtualKeyboardInputContext_instance();
}

/** @brief 取引擎（经上下文单例；NULL 安全）。 */
static XVirtualKeyboardInputEngine* xkb_engineOf(const XVirtualKeyboard* self)
{
    XVirtualKeyboardInputContext* ctx = xkb_contextOf(self);
    return ctx ? XVirtualKeyboardInputContext_inputEngine(ctx) : NULL;
}

/** @brief 取候选列表模型（引擎持有；NULL 安全）。 */
static XVirtualKeyboardSelectionListModel* xkb_modelOf(const XVirtualKeyboard* self)
{
    XVirtualKeyboardInputEngine* engine = xkb_engineOf(self);
    return engine
               ? XVirtualKeyboardInputEngine_wordCandidateListModel(engine)
               : NULL;
}

/** @brief 中文态读数（=engine 当前输入模式为 Pinyin 且插件已装载）。 */
static bool xkb_imeChineseState(const XVirtualKeyboard* self)
{
    XVirtualKeyboardInputEngine* engine;
    if (!self || !self->m_imeEnabled) return false;
    engine = xkb_engineOf(self);
    if (!engine) return false;
    return XVirtualKeyboardInputEngine_inputMode(engine) ==
           (int)XVirtualKeyboardInputEngineInputMode_Pinyin;
}

/** @brief 组串中判定（=context.preeditText 非空——插件状态镜像）。 */
static bool xkb_imeComposing(const XVirtualKeyboard* self)
{
    XVirtualKeyboardInputContext* ctx;
    XString* preedit;
    bool composing;
    if (!self || !self->m_imeEnabled) return false;
    ctx = xkb_contextOf(self);
    if (!ctx) return false;
    preedit = XVirtualKeyboardInputContext_preeditText(ctx);
    composing = preedit && XString_toUtf8(preedit) &&
                XString_toUtf8(preedit)[0] != '\0';
    if (preedit) XString_delete_base(preedit);
    return composing;
}

/** @brief 候选总数读数（engine.wordCandidateListModel().count）。 */
static int xkb_candidateCount(const XVirtualKeyboard* self)
{
    XVirtualKeyboardSelectionListModel* model = xkb_modelOf(self);
    return model ? XVirtualKeyboardSelectionListModel_count(model) : 0;
}

/** @brief 取候选文本快照（dataAt→XVariant String→UTF-8 拷入缓冲）。 */
static bool xkb_candidateText(const XVirtualKeyboard* self, int index, char* buf,
                              int bufSize)
{
    XVirtualKeyboardSelectionListModel* model = xkb_modelOf(self);
    XVariant* value;
    const XString* text;
    const char* utf8;
    if (!model || !buf || bufSize <= 0) return false;
    buf[0] = '\0';
    value = XVirtualKeyboardSelectionListModel_dataAt(
        model, index, (int)XVirtualKeyboardSelectionListModelRole_Display);
    if (!value) return false;
    text = XVariant_toString_const(value);
    utf8 = text ? XString_toUtf8(text) : NULL;
    if (utf8) XStrncpy(buf, utf8, (size_t)bufSize);
    buf[bufSize - 1] = '\0';
    XVariant_delete_base(value);
    return utf8 != NULL;
}

/** @brief 组串文本快照（context.preeditText→缓冲）。 */
static void xkb_preeditSnapshot(const XVirtualKeyboard* self, char* buf,
                                int bufSize)
{
    XVirtualKeyboardInputContext* ctx;
    XString* preedit;
    if (!buf || bufSize <= 0) return;
    buf[0] = '\0';
    ctx = xkb_contextOf(self);
    if (!ctx) return;
    preedit = XVirtualKeyboardInputContext_preeditText(ctx);
    if (preedit) {
        const char* utf8 = XString_toUtf8(preedit);
        if (utf8) XStrncpy(buf, utf8, (size_t)bufSize);
        XString_delete_base(preedit);
    }
    buf[bufSize - 1] = '\0';
}

/** @brief 候选总页数（钳 [0, ...)；ceil(count/pageSize)）。 */
static int xkb_candidatePageCount(const XVirtualKeyboard* self)
{
    int count = xkb_candidateCount(self);
    int pageSize = self->m_candidatePageSize > 0 ? self->m_candidatePageSize
                                                 : 1;
    if (count <= 0) return 0;
    return (count + pageSize - 1) / pageSize;
}

/** @brief 布局/模式切换后通知 Observer（layoutChanged 通知链）。 */
static void xkb_notifyObserver(const XVirtualKeyboard* self)
{
    const char* layoutType = "main";
    XVirtualKeyboardInputEngine* engine;
    int mode;
    if (!self) return;
    switch (self->m_mode) {
    case XKeyboardMode_Special: layoutType = "symbols"; break;
    case XKeyboardMode_Number: layoutType = "numbers"; break;
    case XKeyboardMode_Digits: layoutType = "digits"; break;
    case XKeyboardMode_Dialpad: layoutType = "dialpad"; break;
    default: layoutType = "main"; break;
    }
    engine = xkb_engineOf(self);
    mode = engine ? XVirtualKeyboardInputEngine_inputMode(engine) : 0;
    XVirtualKeyboardObserver_invalidateLayout(XVirtualKeyboardObserver_instance(),
                                              layoutType, "zh_CN", mode);
}

#endif /* XVIRTUALKEYBOARD_ON */

/**
 * @brief 判定目标控件是否为受支持的输入目标（适配层类型识别 + WA14
 *        放行回退）。
 * @details 双口径：①vtable 快路径——逐一比对三编辑控件（XCompleter.c:
 *          535 先例），直写+合成键双写入通道齐备；②WA_InputMethodEnabled
 *          放行回退——对齐 Qt inputMethodAccepted=WA14 口径
 *          （qwidget.cpp:9057-9057 同族），任意开了 WA14 的控件皆可绑
 *          定。修复「XDateEdit/XDateTimeEdit 字段壳获焦后面板闪退」：
 *          壳（XDateTimeEdit vtable）曾在此被拒（init 置 WA14+DigitsOnly，
 *          XDateTimeEdit.c:3067-3070）→popup 经 setTextArea 早退不弹/
 *          守护 ②′ 与 notifyPress 幂等收层。放行目标的写入面=合成键
 *          注入（xkb_sendKey 经 XObject_event_base 直发 m_target，类型
 *          无关）：单字节 ASCII 字符键/Return 照常可达——XDateTimeEdit
 *          壳的数字键由其 keyPressEvent 分段消化（xdt_typeDigit，按下
 *          即终结不转发行编辑），行为对齐无头 apitest 注入口径；多字节/
 *          IME 上屏直写仍仅认三编辑控件 vtable（writeText 尾部诊断忽
 *          略），+/- 符号翻转仍仅 XLineEdit——能力不对称在 writeText
 *          注释与本回退处双声明。三个适配开关全 0 时类型识别降级：
 *          恒返回 true（setTextArea 接受任意 XWidget*）。
 */
static bool xkb_supportedTarget(const XWidget* target)
{
    XVtable* vt;
    if (!target) return true;
    vt = XClassGetVtable(target);
#if XLINEEDIT_ON
    if (vt == XLineEdit_class_init()) return true;
#endif
#if XPLAINTEXTEDIT_ON
    if (vt == XPlainTextEdit_class_init()) return true;
#endif
#if XTEXTEDIT_ON
    if (vt == XTextEdit_class_init()) return true;
#endif
#if !XLINEEDIT_ON && !XPLAINTEXTEDIT_ON && !XTEXTEDIT_ON
    (void)vt;
    return true; /* 类型识别降级：接受任意控件（合成键路径类型无关）。 */
#else
    /* WA14 放行回退：非三编辑控件但显式开启输入法属性（WA14）即接受
       绑定（合成键注入焦点路径承载写入；直写回退缺席见函数头）。 */
    return XWidget_testAttribute(target, XWidgetAttribute_InputMethodEnabled);
#endif
}

/** @brief 判定命中控件是否落在键盘面板子树内（notifyPress 豁免判据）。
 *  @details 沿父链上溯比对 self：面板自身或其子控件命中→true（键盘自
 *           身按键 PRESS 不驱动弹收，弹出态按键照常输入）。 */
static bool xkb_insidePanel(const XVirtualKeyboard* self, const XWidget* hit)
{
    const XWidget* w = hit;
    while (w) {
        if (w == (const XWidget*)self) return true;
        w = XWidget_parentWidget(w);
    }
    return false;
}

#if XAPPLICATION_ON
/** @brief 判定顶层控件是否 Popup 型窗口（悬浮锚识别弹层容器）。
 *  @details 类型位=windowFlags & TypeMask（XWidget_windowType 口径）；
 *           XDateTimeEdit 日历弹层容器在 xdt_popupReposition 里
 *           XWidget_setWindowFlags(Popup)。XWINDOW_ON=0 无弹层形态，
 *           恒 false。 */
static bool xkb_isPopupTypeWindow(const XWidget* w)
{
    if (!w) return false;
#if XWINDOW_ON
    return XWidget_windowType(w) == XWindowType_Popup;
#else
    (void)w;
    return false; /* 无窗口层：Popup 型弹层容器不存在。 */
#endif
}

/** @brief 悬浮锚候选可用性（非键盘自身/非锚本身/顶层/非 Popup 型）。 */
static bool xkb_anchorCandidateOk(const XVirtualKeyboard* self,
                                  const XWidget* host, const XWidget* w)
{
    if (!w || w == (const XWidget*)self || w == host) return false;
    if (!XWidget_isWindow(w) || xkb_isPopupTypeWindow(w)) return false;
    return true;
}

/** @brief 悬浮锚解析：锚为弹层容器时定位主窗口顶层。
 *  @details 弹层容器是 NULL 父的独立 Popup 顶层，其 widget 父链与桥接
 *           窗 transient parent 均无主窗口反向引用（XWidget_createWindow
 *           的 owner 链走父控件链，弹层父链为空），应用顶层表
 *           （XApplication_topLevelWidgets）在 XGuiApplication 单根应
 *           用（不创建 XApplication 基类实例，g_xapp 空）下亦不可用——
 *           按可信次序取候选：① m_hostHint=setHostWindow 时刻焦点顶
 *           层快照（弹层打开前用户所在主窗口，transient parent 语义）；
 *           ② m_host=上一次内嵌弹层的宿主顶层（普通编辑框键盘的记忆
 *           宿主）；③ 应用顶层表（g_xapp 存在的多窗口应用）可见非
 *           Popup 顶层中客户面积最大者。全缺回落锚自身（键盘仍悬浮
 *           压过弹层，仅几何缩为弹层底部）。 */
static XWidget* xkb_resolveFloatingHost(XVirtualKeyboard* self, XWidget* host)
{
    XVector* tops;
    XWidget* best;
    int64_t bestArea;
    size_t n;
    size_t i;
    if (!xkb_isPopupTypeWindow(host)) return host; /* 锚即主窗口顶层。 */
    if (xkb_anchorCandidateOk(self, host, self->m_hostHint))
        return self->m_hostHint;
    if (xkb_anchorCandidateOk(self, host, self->m_host))
        return self->m_host;
    tops = XApplication_topLevelWidgets();
    if (!tops) return host;
    best = NULL;
    bestArea = -1;
    n = XVector_size_base((const XContainer*)tops);
    for (i = 0; i < n; ++i) {
        XWidget* w = XVector_At_Base(tops, (int64_t)i, XWidget*);
        int64_t area;
        if (!xkb_anchorCandidateOk(self, host, w)) continue;
        if (!XWidget_isVisible(w)) continue;
        area = (int64_t)XWidget_width(w) * (int64_t)XWidget_height(w);
        if (area > bestArea) {
            bestArea = area;
            best = w;
        }
    }
    XVector_delete_base((XClass*)tops);
    return best ? best : host;
}
#else /* !XAPPLICATION_ON */
static XWidget* xkb_resolveFloatingHost(XVirtualKeyboard* self, XWidget* host)
{
    (void)self;
    return host; /* 无应用顶层表：锚原样（调用方保证非 Popup 型）。 */
}
#endif /* XAPPLICATION_ON */

/** @brief 目标 destroyed 防悬垂槽：解绑并收层（守护 tick 解引用已销毁
 *         editor 即崩溃，此连接是前置防线）。 */
static void xkb_targetDestroyedSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)receiver;
    (void)args;
    if (!self) return;
    self->m_target = NULL;
    self->m_targetConn = NULL; /* 发送方连接表随销毁失效，句柄不可再断。 */
    XVirtualKeyboard_closePopup(self);
}

/** @brief 宿主 destroyed 防悬垂槽：清宿主、收层并摘挂归还单例。
 *  @details destroyed 发射于宿主 is_deleting_children 置位之前
 *           （XObject.c VXObject_deinit），此刻 setParent(NULL) 经
 *           prev_parent 分支（XVector_remove_base）把面板从宿主
 *           children 摘除，随后的子控件级联删除循环不再连带删除堆
 *           面板——面板存活并归还 XGuiApplication 惰性单例所有
 *           （m_virtualKeyboard 未清，后续按压经 virtualKeyboard()
 *           拿到的仍是活对象），下次 popup() 无条件
 *           bindHostDestroyed+setParent 自动重挂新宿主（幂等）。 */
static void xkb_hostDestroyedSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)receiver;
    (void)args;
    if (!self) return;
    self->m_host = NULL;
    self->m_hostConn = NULL; /* 发送方连接表随销毁失效，句柄不可再断。 */
    self->m_hostW = 0;
    self->m_hostH = 0;
    XVirtualKeyboard_closePopup(self);
    /* 摘挂归还：脱离濒死宿主的 children 列表，躲过级联删除；同时
       归位顶层形态并重注册顶层表（XWidget_setParent NULL 分支）。 */
    XWidget_setParent((XWidget*)self, NULL, 0);
}

/* ==================== 键盘主题化（单一 LVGL 路径；原
 *                       XKEYBOARD_THEME_LVGL_ON 双分支已合并） ==================== */
/* LVGL 9.2.2 默认主题浅色模板色板（lv_theme_default.c 浅色口径；全部
 * 收在键盘自身局部常量，不动全局调色板。标注"派生值"者为按
 * lv_color_mix/lv_color_darken 公式逐通道手算，未运行时取色验证）：
 * SCR=0xF5F5F5、CARD/白键=0xFFFFFF、TEXT=0x212121、GREY=0xE0E0E0、
 * 主色=0x2196F3（lv_palette BLUE，仅局部常量）。 */
#define XKB_LVGL_SCR           0xFFF5F5F5u /* 面板底（lighten(GREY,4)）。 */
#define XKB_LVGL_CARD          0xFFFFFFFFu /* 键面/候选带卡底（card）。 */
#define XKB_LVGL_TEXT          0xFF212121u /* 键文字（darken(GREY,4)）。 */
#define XKB_LVGL_GREY          0xFFE0E0E0u /* checked 键底/灰边（lighten(GREY,2)）。 */
#define XKB_LVGL_PRIMARY       0xFF2196F3u /* 主色（默认 BLUE）。 */
#define XKB_LVGL_PRIMARY_MUTED 0xFFD2EAFCu /* 主色@20%opa 白卡合成（派生值；按本模板 LV_COLOR_MIX_ROUND_OFS=0 复算——旧值 0xD3EAFD 为 OFS=128 四舍五入口径，Δ1/255）。 */
#define XKB_LVGL_PRESSED_FACE  0xFFDCDCDCu /* pressed=darken(c,35) 白键派生值。 */
#define XKB_LVGL_PRESSED_TEXT  0xFF1C1C1Cu /* pressed 文字同步压暗（派生值）。 */
#define XKB_LVGL_DISABLED_TEXT 0xFF808080u /* disabled=向 GREY 混 LV_OPA_50=127（派生值；OFS 无关恒 0x808080——旧值 0x818181 需 OPA=128+OFS=128，与 lv_color.h 口径矛盾）。 */
#define XKB_LVGL_ARROW_TEXT    0xFF616161u /* 翻页箭头文字灰（派生值）。 */

/** @brief 键圆角半径（rowH>=44px 取 8、否则 4——≈12dp/8dp@130dpi 的
 *         像素取整口径，对齐 LVGL RADIUS_DEFAULT 12/8dp+小屏减半）。 */
static int xkb_keyRadius(int rowH)
{
    return rowH >= 44 ? 8 : 4;
}

/**
 * @brief 键面/候选带矩形绘制：圆角矩形一次成形（画刷填充+画笔描边）。
 * @details XPAINTER_SHAPE_ON 开且矩形足够容纳圆角时走
 *          XPainter_drawRoundedRect；SHAPE=0 或矩形过小回退 fillRect
 *          方角 + 1px 描边（功能不缺）。border==fill 时无可见描边
 *          （LVGL 键面无 border、无阴影的口径）。
 */
static void xkb_roundRect(XPainter* painter, const XRect* rect,
                          uint32_t fill, uint32_t border, int radius)
{
#if XPAINTER_SHAPE_ON
    if (radius > 0 && rect->width > 2 * radius && rect->height > 2 * radius) {
        XPainter_setBrush(painter, fill);
        XPainter_setPen(painter, border);
        XPainter_drawRoundedRect(painter, rect, radius, radius);
        return;
    }
#else
    (void)radius;
#endif
    XPainter_fillRect(painter, rect, fill);
    if (border != fill) {
        XRect edge = *rect;
        edge.height = 1;
        XPainter_fillRect(painter, &edge, border);
        edge = *rect;
        edge.y = rect->y + rect->height - 1;
        edge.height = 1;
        XPainter_fillRect(painter, &edge, border);
        edge = *rect;
        edge.width = 1;
        XPainter_fillRect(painter, &edge, border);
        edge = *rect;
        edge.x = rect->x + rect->width - 1;
        edge.width = 1;
        XPainter_fillRect(painter, &edge, border);
    }
}

#if XVIRTUALKEYBOARD_ON

/* ==================== 拼音候选带（面板内嵌顶行，XGui 扩展；数据源
 *                       =engine 候选模型 + context.preeditText 镜像） ==================== */

/** @brief 候选带左「中」模式指示 chip 宽（px，固定）。 */
#define XKB_IME_MODE_CHIP_W 40
/** @brief 候选带右端翻页区单元宽（"<"/页码/">" 各一，px）。 */
#define XKB_IME_PAGE_CELL_W 24
/** @brief 候选带内子区间间距（px）。 */
#define XKB_IME_GAP 4
/** @brief 候选带内缩边距（px）。 */
#define XKB_IME_PAD 2
/** @brief 变宽 chip 文本两侧垫宽（px；实宽=textWidth+2*垫宽 钳位）。 */
#define XKB_IME_CHIP_PAD 3
/** @brief 候选单页 chip 数上限（与 IME 页容量口径一致，数字键 1..9）。 */
#define XKB_IME_PAGE_MAX 9

/**
 * @brief 候选带内部子区域几何（布局/绘制/命中三方共用同一几何函数产
 *        出，保证所见即所点）。
 */
typedef struct XkbImeBandGeo
{
    XRect band;      /**< 整带矩形（控件局部坐标）。 */
    XRect modeChip;  /**< 「中」模式指示 chip（点按切中英）。 */
    XRect compose;   /**< 组串区（ASCII 组串显示，空闲留白）。 */
    XRect chips;     /**< 候选 chip 流式排布区。 */
    XRect pagePrev;  /**< "<" 上一页热区。 */
    XRect pageInfo;  /**< 页码区（"页/页数" ASCII）。 */
    XRect pageNext;  /**< ">" 下一页热区。 */
    int chipW;       /**< 单个候选 chip 最小宽（=键行高钳 8，近方形
                          下限；变宽 chip 实宽见 chipWv）。 */
    int chipGap;     /**< chip 间距（px）。 */
    int chipCount;   /**< 当前页实际排布 chip 数（超排布区截断后的
                          生效数；绘制与命中共同消费）。 */
    int chipXv[XKB_IME_PAGE_MAX]; /**< 每 chip 左 x（累进，所见即所点）。 */
    int chipWv[XKB_IME_PAGE_MAX]; /**< 每 chip 实宽（clamp(textWidth+
                          2*垫宽, chipMinW, chips 区宽)）。 */
} XkbImeBandGeo;

/** @brief 候选带排版字符宽（控件字体 ASCII 'a' 步进；下限 4px 防退
 *         化——组串区宽公式的字号输入，布局/绘制/命中同源）。 */
static int xkb_imeCharWidth(const XVirtualKeyboard* self)
{
    int w = XPainter_textWidth(&((XWidget*)self)->m_font, "a");
    return w < 4 ? 4 : w;
}

/** @brief 候选 chip 实宽（变宽口径：clamp(textWidth+2*垫宽, chipMinW,
 *         chips 区宽)；chipMinW=rowH 钳 8 近方形下限，超宽候选钳排布
 *         区）。空文本退化 chipMinW。布局与页容量换算同源。 */
static int xkb_imeChipTextWidth(const XVirtualKeyboard* self, const char* text,
                                int chipMinW, int areaW)
{
    int w = chipMinW;
    if (text && text[0])
    {
        w = XPainter_textWidth(&((XWidget*)self)->m_font, text) +
            2 * XKB_IME_CHIP_PAD;
        if (w < chipMinW) w = chipMinW;
        if (w > areaW) w = areaW;
    }
    return w;
}

/**
 * @brief 候选带几何计算（paint 与 hit 共用；无带时全部子区间清零）。
 * @param self 键盘对象；NULL 时 geo 清零返回。
 * @param rowH 键行高（chip 最小宽输入；<=0 时以带内高兜底）。
 * @param geo 输出几何（调用方提供存储空间）。
 */
static void xkb_imeBandLayout(const XVirtualKeyboard* self, int rowH,
                              XkbImeBandGeo* geo)
{
    int innerH;
    int composeW;
    int right;
    if (!geo) return;
    XRect_init(&geo->band, 0, 0, 0, 0);
    XRect_init(&geo->modeChip, 0, 0, 0, 0);
    XRect_init(&geo->compose, 0, 0, 0, 0);
    XRect_init(&geo->chips, 0, 0, 0, 0);
    XRect_init(&geo->pagePrev, 0, 0, 0, 0);
    XRect_init(&geo->pageInfo, 0, 0, 0, 0);
    XRect_init(&geo->pageNext, 0, 0, 0, 0);
    geo->chipW = 0;
    geo->chipGap = XKB_IME_GAP;
    geo->chipCount = 0;
    if (!self || self->m_imeBandRect.height <= 0 ||
        self->m_imeBandRect.width <= 0)
        return;
    geo->band = self->m_imeBandRect;
    innerH = geo->band.height - 2 * XKB_IME_PAD;
    if (innerH < 1) innerH = 1;
    /* 左「中」chip（固定 40px，点按=切中英）。 */
    geo->modeChip.x = geo->band.x + XKB_IME_PAD;
    geo->modeChip.y = geo->band.y + XKB_IME_PAD;
    geo->modeChip.width = XKB_IME_MODE_CHIP_W;
    geo->modeChip.height = innerH;
    /* 右端翻页区（自右向左：">" "页码" "<"，各 24px）。 */
    right = geo->band.x + geo->band.width - XKB_IME_PAD;
    geo->pageNext.x = right - XKB_IME_PAGE_CELL_W;
    geo->pageInfo.x = geo->pageNext.x - XKB_IME_PAGE_CELL_W;
    geo->pagePrev.x = geo->pageInfo.x - XKB_IME_PAGE_CELL_W;
    geo->pageNext.y = geo->band.y + XKB_IME_PAD;
    geo->pageInfo.y = geo->band.y + XKB_IME_PAD;
    geo->pagePrev.y = geo->band.y + XKB_IME_PAD;
    geo->pageNext.width = XKB_IME_PAGE_CELL_W;
    geo->pageInfo.width = XKB_IME_PAGE_CELL_W;
    geo->pagePrev.width = XKB_IME_PAGE_CELL_W;
    geo->pageNext.height = innerH;
    geo->pageInfo.height = innerH;
    geo->pagePrev.height = innerH;
    /* 组串区（宽=min(带宽/3, 15×字符宽+16)，显示 ASCII 组串——15 为
       INV2 组串容量，跨音节组串最长 15 字母）。 */
    composeW = geo->band.width / 3;
    if (xkb_imeCharWidth(self) * 15 + 16 < composeW)
        composeW = xkb_imeCharWidth(self) * 15 + 16;
    if (composeW < 1) composeW = 1;
    geo->compose.x = geo->modeChip.x + geo->modeChip.width + XKB_IME_GAP;
    geo->compose.y = geo->band.y + XKB_IME_PAD;
    geo->compose.width = composeW;
    geo->compose.height = innerH;
    /* chip 排布区与 chip 最小尺寸（chipW=键行高钳 8 近方形下限；行高
       缺席以带内高兜底）。 */
    geo->chipW = rowH > 0 ? rowH : innerH;
    if (geo->chipW < 8) geo->chipW = 8;
    geo->chips.x = geo->compose.x + composeW + XKB_IME_GAP;
    geo->chips.y = geo->band.y + XKB_IME_PAD;
    geo->chips.width = geo->pagePrev.x - XKB_IME_GAP - geo->chips.x;
    if (geo->chips.width < 0) geo->chips.width = 0;
    geo->chips.height = innerH;
    /* 变宽 chip 几何（不受 PHRASE_ON 门控——单字 3 字节汉字等宽，视
       觉与近方形差异仅垫宽，属实现细节非行为分叉）：每 chip 实宽按
       候选文本测宽钳位，x 累进；绘制与命中消费同一数组（所见即所
       点）。 */
    geo->chipCount = 0;
    {
        int pageSize = self->m_candidatePageSize > 0 ? self->m_candidatePageSize
                                                     : 1;
        int first = self->m_candidatePage * pageSize;
        int count = xkb_candidateCount(self);
        int shown = count - first;
        int x = geo->chips.x;
        int i;
        char text[64];
        if (shown > pageSize) shown = pageSize;
        if (shown < 0) shown = 0;
        if (shown > XKB_IME_PAGE_MAX) shown = XKB_IME_PAGE_MAX;
        for (i = 0; i < shown; ++i) {
            int w;
            if (!xkb_candidateText(self, first + i, text, (int)sizeof(text)))
                break;
            w = xkb_imeChipTextWidth(self, text, geo->chipW,
                                     geo->chips.width);
            if (i > 0) x += geo->chipGap;
            if (x + w > geo->chips.x + geo->chips.width) break; /* 超出截断。 */
            geo->chipXv[i] = x;
            geo->chipWv[i] = w;
            x += w;
            geo->chipCount = i + 1;
        }
    }
}

/** @brief 页容量 k 模拟：每条 stride 页（候选 i 按 i%k 落页内位）的
 *         k chip 皆容纳返回 true。chip 宽与页内位置无关（按候选文本
 *         测宽钳位），仅页内累进 x 依赖 k。 */
static bool xkb_imePageFits(const XVirtualKeyboard* self, int count, int k,
                            int chipMinW, int areaX, int areaW, int gap)
{
    int x = areaX;
    int i;
    char text[64];
    for (i = 0; i < count; ++i) {
        int w;
        if (!xkb_candidateText(self, i, text, (int)sizeof(text))) return true;
        if (i % k == 0)
            x = areaX; /* 新 stride 页从头排。 */
        else
            x += gap;
        w = xkb_imeChipTextWidth(self, text, chipMinW, areaW);
        if (x + w > areaX + areaW) return false;
        x += w;
    }
    return true;
}

/** @brief 候选页容量注入（变宽 chip 口径；面板本地状态）：按当前候选
 *         全量测宽，自 k=min(9,count) 向下搜索『每条 stride 页的 k
 *         chip 皆容纳』的最大 k，钳位 [1,9]；同时钳当前页到有效域。 */
static void xkb_imeInjectPageSize(XVirtualKeyboard* self, int rowH)
{
    XkbImeBandGeo geo;
    int count;
    int maxK;
    int k;
    xkb_imeBandLayout(self, rowH, &geo);
    count = xkb_candidateCount(self);
    maxK = count < XKB_IME_PAGE_MAX ? count : XKB_IME_PAGE_MAX;
    for (k = maxK; k >= 2; --k) {
        if (xkb_imePageFits(self, count, k, geo.chipW, geo.chips.x,
                            geo.chips.width, geo.chipGap))
            break;
    }
    if (k < 1) k = 1;
    if (k > XKB_IME_PAGE_MAX) k = XKB_IME_PAGE_MAX;
    self->m_candidatePageSize = k;
    /* 页钳位（组串变化后总页数可能缩）。 */
    {
        int pages = xkb_candidatePageCount(self);
        if (self->m_candidatePage >= pages) self->m_candidatePage = pages - 1;
        if (self->m_candidatePage < 0) self->m_candidatePage = 0;
    }
}

/** @brief 组串变化后的页容量同步（feed 路径出口调用；带缺席时无操
 *         作，行高取首键矩形与 paint/hit 同源）。 */
static void xkb_imeSyncPageSize(XVirtualKeyboard* self)
{
    int rowH;
    if (!self || self->m_imeBandRect.height <= 0) return;
    rowH = self->m_keyCount > 0 ? (int)self->m_keyRects[0].height : 0;
    xkb_imeInjectPageSize(self, rowH);
}

/** @brief 无符号整数转十进制 ASCII（页码格式化专用，不引入 stdio；
 *         负值按 0 处理，最多 3 位）。 */
static int xkb_imeFormatUInt(int value, char* out)
{
    char tmp[4];
    int n = 0;
    int i = 0;
    if (value < 0) value = 0;
    do {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && n < 3);
    while (n > 0)
        out[i++] = tmp[--n];
    out[i] = '\0';
    return i;
}

/**
 * @brief 候选带绘制（组串区/候选 chip/翻页区/中英指示 chip）。
 * @details 配色=LVGL 浅色模板常量单路径（带卡白底圆角+灰边、组串区
 *          主色@20%、chip 键面样式、翻页箭头文字灰/禁用混灰；原
 *          XKEYBOARD_THEME_LVGL_ON 调色板分支已合并移除）。数据源=
 *          engine 候选模型（分页为面板本地状态）+context.preeditText。
 *          绘制前先经 xkb_imeBandLayout 取同一几何。
 */
static void xkb_imeBandPaint(XVirtualKeyboard* self, XPainter* painter)
{
    XkbImeBandGeo geo;
    uint32_t bandFace = XKB_LVGL_CARD;
    uint32_t bandBorder = XKB_LVGL_GREY;
    uint32_t chipFace = XKB_LVGL_CARD;
    uint32_t chipText = XKB_LVGL_TEXT;
    uint32_t composeFace = XKB_LVGL_PRIMARY_MUTED;
    uint32_t composeText = XKB_LVGL_PRIMARY;
    uint32_t modeFace = XKB_LVGL_GREY;
    uint32_t modeText = XKB_LVGL_TEXT;
    uint32_t arrowCol = XKB_LVGL_ARROW_TEXT;
    uint32_t arrowDis = XKB_LVGL_DISABLED_TEXT;
    int radius;
    int pageIdx;
    int pageCount;
    int pageSize;
    int first;
    int i;
    char pageText[16];
    char preedit[64];
    char chipBuf[64];
    int pos = 0;

    if (!self || self->m_imeBandRect.height <= 0) return;
    xkb_imeBandLayout(self,
                      self->m_keyCount > 0 ? (int)self->m_keyRects[0].height
                                           : 0,
                      &geo);
    if (geo.band.height <= 0) return;
    pageIdx = self->m_candidatePage;
    pageCount = xkb_candidatePageCount(self);
    pageSize = self->m_candidatePageSize > 0 ? self->m_candidatePageSize : 1;
    first = pageIdx * pageSize; /* chip 绘制条数=geo.chipCount（同源几何）。 */
    /* 带面板（card 白底 + 灰边；圆角随带高钳位）。 */
    radius = 8;
    if (radius > geo.band.height / 2) radius = geo.band.height / 2;
    xkb_roundRect(painter, &geo.band, bandFace, bandBorder, radius);
    /* 「中」模式指示 chip（点击切中英；带仅中文态存在，恒显示「中」）。 */
    xkb_roundRect(painter, &geo.modeChip, modeFace, modeFace, 4);
    XPainter_drawTextRect(painter, &geo.modeChip,
                          XPAINTER_TEXT_ALIGN_CENTER |
                              XPAINTER_TEXT_SINGLE_LINE,
                          "\xE4\xB8\xAD", modeText);
    /* 组串区（主色弱高亮=『进行中』语义；空闲留白；文本=context 镜像）。 */
    xkb_roundRect(painter, &geo.compose, composeFace, composeFace, 4);
    xkb_preeditSnapshot(self, preedit, (int)sizeof(preedit));
    if (preedit[0])
        XPainter_drawTextRect(painter, &geo.compose,
                              XPAINTER_TEXT_ALIGN_LEFT |
                                  XPAINTER_TEXT_ALIGN_VCENTER |
                                  XPAINTER_TEXT_SINGLE_LINE,
                              preedit, composeText);
    /* 候选 chip 流式排布（候选 UTF-8 串居中；变宽几何 chipXv/chipWv
       由 xkb_imeBandLayout 按同源候选测宽产出，超出排布区截断——
       chipCount 为生效数）。 */
    for (i = 0; i < geo.chipCount; ++i) {
        XRect chip;
        if (!xkb_candidateText(self, first + i, chipBuf,
                               (int)sizeof(chipBuf)))
            break;
        chip.x = geo.chipXv[i];
        chip.y = geo.chips.y;
        chip.width = geo.chipWv[i];
        chip.height = geo.chips.height;
        xkb_roundRect(painter, &chip, chipFace, chipFace, 4);
        XPainter_drawTextRect(painter, &chip,
                              XPAINTER_TEXT_ALIGN_CENTER |
                                  XPAINTER_TEXT_SINGLE_LINE,
                              chipBuf, chipText);
    }
    /* 右端翻页区（"<" 页码 ">"；单页/边界置灰且点按无效）。 */
    XPainter_drawTextRect(painter, &geo.pagePrev,
                          XPAINTER_TEXT_ALIGN_CENTER |
                              XPAINTER_TEXT_SINGLE_LINE,
                          "<",
                          (pageCount > 1 && pageIdx > 0) ? arrowCol
                                                         : arrowDis);
    if (pageCount > 0) {
        pos += xkb_imeFormatUInt(pageIdx + 1, pageText + pos);
        pageText[pos++] = '/';
        pos += xkb_imeFormatUInt(pageCount, pageText + pos);
        pageText[pos] = '\0';
        XPainter_drawTextRect(painter, &geo.pageInfo,
                              XPAINTER_TEXT_ALIGN_CENTER |
                                  XPAINTER_TEXT_SINGLE_LINE,
                              pageText,
                              pageCount > 1 ? arrowCol : arrowDis);
    }
    XPainter_drawTextRect(painter, &geo.pageNext,
                          XPAINTER_TEXT_ALIGN_CENTER |
                              XPAINTER_TEXT_SINGLE_LINE,
                          ">",
                          (pageCount > 1 && pageIdx < pageCount - 1)
                              ? arrowCol
                              : arrowDis);
}

/**
 * @brief 候选带命中（mousePressEvent 在 xkb_hitTest 之前调用）。
 * @details 命中即 press 直触发（chip/翻页/「中」chip），不进
 *          m_pressedKey 武装链（move/release 对 NONE 键零动作，天然
 *          兼容）；带内其余空白一并消费防误透键区。提交路径经
 *          xkb_writeText 复用既有写入链。
 * @return 命中带返回 true（事件已消费）；未命中返回 false。
 */
static bool xkb_imeBandHit(XVirtualKeyboard* self, int x, int y)
{
    XkbImeBandGeo geo;
    XVirtualKeyboardSelectionListModel* model;
    int pageCount;
    int pageSize;
    int first;
    int shown;
    int i;
    if (!self || self->m_imeBandRect.height <= 0) return false;
    if (x < self->m_imeBandRect.x ||
        x >= self->m_imeBandRect.x + self->m_imeBandRect.width ||
        y < self->m_imeBandRect.y ||
        y >= self->m_imeBandRect.y + self->m_imeBandRect.height)
        return false;
    xkb_imeBandLayout(self,
                      self->m_keyCount > 0 ? (int)self->m_keyRects[0].height
                                           : 0,
                      &geo);
    model = xkb_modelOf(self);
    /* 「中」chip：切英文态（带随重建隐藏；切回走布局「中/EN」键）。 */
    if (x >= geo.modeChip.x && x < geo.modeChip.x + geo.modeChip.width &&
        y >= geo.modeChip.y && y < geo.modeChip.y + geo.modeChip.height) {
        XVirtualKeyboard_setImeChinese(self, false);
        XWidget_update((XWidget*)self);
        return true;
    }
    pageCount = xkb_candidatePageCount(self);
    if (pageCount > 1) {
        if (x >= geo.pagePrev.x && x < geo.pagePrev.x + geo.pagePrev.width &&
            y >= geo.pagePrev.y && y < geo.pagePrev.y + geo.pagePrev.height) {
            if (self->m_candidatePage > 0) {
                --self->m_candidatePage; /* 面板本地翻页（钳位）。 */
                XWidget_update((XWidget*)self);
            }
            return true;
        }
        if (x >= geo.pageNext.x && x < geo.pageNext.x + geo.pageNext.width &&
            y >= geo.pageNext.y && y < geo.pageNext.y + geo.pageNext.height) {
            if (self->m_candidatePage < pageCount - 1) {
                ++self->m_candidatePage;
                XWidget_update((XWidget*)self);
            }
            return true;
        }
    }
    /* 候选 chip：点选即 model.selectItem（全量下标=页基+页内序；提交
       经插件 selectionListItemSelected→context.commit→面板写入链）；
       几何消费与绘制同一 chipXv/chipWv（所见即所点）。 */
    pageSize = self->m_candidatePageSize > 0 ? self->m_candidatePageSize : 1;
    first = self->m_candidatePage * pageSize;
    shown = xkb_candidateCount(self) - first;
    if (shown > pageSize) shown = pageSize;
    for (i = 0; i < geo.chipCount && i < shown; ++i) {
        if (x >= geo.chipXv[i] && x < geo.chipXv[i] + geo.chipWv[i] &&
            y >= geo.chips.y && y < geo.chips.y + geo.chips.height) {
            if (model)
                XVirtualKeyboardSelectionListModel_selectItem(model,
                                                              first + i);
            xkb_imeSyncPageSize(self); /* 提交后候选集变化，页容量同步。 */
            XWidget_update((XWidget*)self);
            return true;
        }
    }
    /* 带内其余空白：消费点击不透键区。 */
    return true;
}

/**
 * @brief 数字键 1..9 面板分页拦截（xkb_activateButton 单点调用）。
 * @details 【分页面板本地化（apiMapping#13）】组串中且有候选时按面板
 *          页状态换算全量下标 page*pageSize+d-1 → model.selectItem
 *          （提交经插件候选钩子→context.commit→写入链）；无候选/IDLE
 *          返回 false（放行为普通数字/插件吞掉语义）。
 * @return 已处理返回 true。
 */
static bool xkb_imeInterceptDigit(XVirtualKeyboard* self, char digit)
{
    XVirtualKeyboardSelectionListModel* model;
    int pageSize;
    int index;
    if (!self || digit < '1' || digit > '9') return false;
    if (!xkb_imeComposing(self)) return false;
    if (xkb_candidateCount(self) <= 0) return true; /* 组串中无候选：吞掉。 */
    model = xkb_modelOf(self);
    if (!model) return false;
    pageSize = self->m_candidatePageSize > 0 ? self->m_candidatePageSize : 1;
    index = self->m_candidatePage * pageSize + (digit - '0') - 1;
    if (index < 0 || index >= xkb_candidateCount(self)) {
        XWidget_update((XWidget*)self); /* 越界：吞掉（原语义）。 */
        return true;
    }
    XVirtualKeyboardSelectionListModel_selectItem(model, index);
    xkb_imeSyncPageSize(self);
    XWidget_update((XWidget*)self);
    return true;
}

/**
 * @brief Qt 形态单点路由：按钮文本 → engine 虚键（插件可消费）。
 * @details 未列出的控制键（确认/关闭/收起/中EN/+/-）不路由，面板本地
 *          语义先行；路由返回 false=插件未消费 → 回落内置语义匹配。
 *          拼音插件消费面：小写字母进组串/退格删组串/回车原串/空格
 *          首选或原串；Plain 插件恒不消费（拉丁直通等价现状）。
 * @return 已消费返回 true（激活分发终止）。
 */
static bool xkb_routeKey(XVirtualKeyboard* self, const char* text)
{
    XVirtualKeyboardInputEngine* engine = xkb_engineOf(self);
    int key = 0;
    char buf[2];
    const char* keyText = NULL;
    char ch;
    if (!engine || !text || !text[0]) return false;
    if (text[1] == '\0') {
        ch = text[0];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a'); /* 组串小写。 */
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == ' ') {
            buf[0] = ch;
            buf[1] = '\0';
            key = (int)(unsigned char)ch;
            keyText = buf;
        } else {
            return false; /* 符号键不路由（内置直写）。 */
        }
    } else if (XStrcmp(text, XKEYBOARD_LBL_BACKSPACE) == 0) {
        key = XKey_Backspace;
    } else if (XStrcmp(text, XKEYBOARD_LBL_NEWLINE) == 0 ||
               XStrcmp(text, XKEYBOARD_LBL_ENTER) == 0) {
        key = XKey_Return;
    } else if (XStrcmp(text, XKEYBOARD_LBL_LEFT) == 0 ||
               XStrcmp(text, XKEYBOARD_LBL_RIGHT) == 0) {
        return false; /* 光标键放行内置（组串显示在候选带，无害）。 */
    } else {
        return false;
    }
    return XVirtualKeyboardInputEngine_virtualKeyClick(engine, key, keyText,
                                                       XKeyboardModifier_NoModifier);
}

#endif /* XVIRTUALKEYBOARD_ON */

/* ==================== 布局解析（对标 lv_buttonmatrix.c:154-177） ==================== */

/**
 * @brief 按当前模式槽位表重建键位矩形与生效控制字表。
 * @details 解析规则：map 以 NULL 数组尾或 "" 按钮终止；"\n" 为换行分
 *          隔符（不占按钮索引）；行内每键宽=行宽×该键单位/该行单位总
 *          数（整数累进边界，行内无缝隙）；行高=内容高/行数均分；
 *          popovers=0 时剥全部 POPOVER 位存 m_keyCtrls（对标 KB.c:
 *          468-487）；popovers=1 且顶行含 POPOVER 键时键区上方预留一
 *          行高气泡带防裁剪（对标 BM.c:410-411）。按钮数超
 *          XKEYBOARD_MAX_BUTTONS 整体拒绝（保持原布局，XPrintf 诊断）。
 */
static void xkb_rebuildLayout(XVirtualKeyboard* self)
{
    const char* const* map;
    const XKeyboardButtonCtrl* ctrlMap;
    XKeyboardButtonCtrl ctrls[XKEYBOARD_MAX_BUTTONS];
    XRect rects[XKEYBOARD_MAX_BUTTONS];
    int rowCount = 1;
    int rowOf[XKEYBOARD_MAX_BUTTONS];
    uint32_t count = 0;
    bool topRowPopover = false;
    int contentX;
    int contentY;
    int contentW;
    int contentH;
    int bandH = 0;
    int rowH;
    int row;
    int i;
    int rowStart[XKEYBOARD_MAX_BUTTONS + 1];
    int rows = 0;
    const char* labels[XKEYBOARD_MAX_BUTTONS];

    if (!self) return;
    map = self->m_maps[self->m_mode];
    if (!map) {
        self->m_keyCount = 0;
#if XVIRTUALKEYBOARD_ON
        XRect_init(&self->m_imeBandRect, 0, 0, 0, 0); /* 空表无带。 */
#endif
        return;
    }
    ctrlMap = self->m_ctrls[self->m_mode];
    rowStart[0] = 0; /* 首行起点（首行前无换行分隔符）。 */

    /* 第一遍：解析按钮/行结构并落生效控制字（临时表，拒绝时不破坏原布局）。 */
    for (i = 0; map[i] != NULL; ++i) {
        const char* label = map[i];
        if (label[0] == '\0') break; /* "" 按钮终止（兼容 LVGL 习惯）。 */
        if (XStrcmp(label, "\n") == 0) {
            /* >=：恰 64 个换行时 rows 会到 65，收尾写 rowStart[rows]=65
               将越过容量 65 的合法下标 0..64（评审复核发现的病态表越界）。 */
            if (rowCount >= XKEYBOARD_MAX_BUTTONS) {
                XPrintf("[XVirtualKeyboard] setMap: 行数超出上限 %d，整体拒绝\n",
                        (int)XKEYBOARD_MAX_BUTTONS);
                return; /* 保持原布局不变。 */
            }
            /* 换行分隔符=当前行结束：登记「下一行起点」（差一修复——
               原写 rowStart[rowCount-1] 覆盖了当前行起点，导致中间行
               起点漏登、末行起点成栈垃圾，第二遍按垃圾越界读 ctrls）。
               不变式：rowStart[0]=0、rowStart[row] 为第 row 行首键在
               ctrl/rects 表中的索引、rowStart[rows]=count。 */
            rowStart[rowCount] = (int)count;
            ++rowCount;
            continue;
        }
        if (count >= XKEYBOARD_MAX_BUTTONS) {
            XPrintf("[XVirtualKeyboard] setMap: 按钮数超出上限 %d，整体拒绝\n",
                    (int)XKEYBOARD_MAX_BUTTONS);
            return; /* 保持原布局不变。 */
        }
        {
            XKeyboardButtonCtrl ctrl = ctrlMap ? ctrlMap[count]
                                               : (XKeyboardButtonCtrl)1;
            int width = (int)ctrl & (int)XKEYBOARD_CTRL_WIDTH_MASK;
            if (width <= 0) width = 1;
            if (!self->m_popovers)
                ctrl = (XKeyboardButtonCtrl)(
                    (int)ctrl & ~(int)XKEYBOARD_CTRL_POPOVER);
            ctrls[count] = (XKeyboardButtonCtrl)(
                ((int)ctrl & ~(int)XKEYBOARD_CTRL_WIDTH_MASK) | width);
            labels[count] = label;
            rowOf[count] = rowCount - 1;
            ++count;
        }
    }
    rows = rowCount;
    rowStart[rows] = (int)count;

    for (i = 0; i < (int)count; ++i) {
        if (rowOf[i] == 0 &&
            ((int)ctrls[i] & (int)XKEYBOARD_CTRL_POPOVER) != 0)
            topRowPopover = true;
    }

    /* 第二遍：几何归一化（每行按自身单位总数铺满行宽）。 */
    {
        XRect area = XWidget_rect((XWidget*)self);
        contentX = area.x + 2;
        contentY = area.y + 2;
        contentW = area.width - 4;
        contentH = area.height - 4;
        if (contentW < 1) contentW = 1;
        if (contentH < 1) contentH = 1;
    }
    if (self->m_popovers && topRowPopover) bandH = contentH / rows;
#if XVIRTUALKEYBOARD_ON
    /* 拼音候选带：中文态时占用顶行（contentH/(rows+1)），与气泡带共
       用同一行高预留通道且天然互斥——拼音表全表无 POPOVER 位，
       topRowPopover 恒 false，两带不叠加。带矩形记入 m_imeBandRect
       （h==0=无带），键区 y 自 contentY+bandH 起（既有偏移通道）。 */
    if (self->m_imeEnabled && self->m_mode == XKeyboardMode_User1 &&
        xkb_imeChineseState(self)) {
        bandH = contentH / (rows + 1);
        XRect_init(&self->m_imeBandRect, contentX, contentY, contentW,
                   bandH);
    } else {
        XRect_init(&self->m_imeBandRect, 0, 0, 0, 0);
    }
#endif
    rowH = (contentH - bandH) / rows;
    if (rowH < 1) rowH = 1;
#if XVIRTUALKEYBOARD_ON
    /* 页容量注入：按带内 chip 排布区宽度换算（钳位 [1,9]）。放本处而
       非 setImeEnabled 一处——setMap/setMode/setPopovers/setButtonCtrl
       /resize 的重建路径全部汇入本函数，任意重建后带几何与页容量同
       步刷新（风险清单②）。控件未布局（宽 0）时 xkb_imeBandLayout
       兜底钳位，无除零。 */
    if (self->m_imeBandRect.height > 0)
        xkb_imeInjectPageSize(self, rowH);
#endif

    for (row = 0; row < rows; ++row) {
        int total = 0;
        int x = contentX;
        int y = contentY + bandH + row * rowH;
        int b;
        for (b = rowStart[row]; b < rowStart[row + 1]; ++b)
            total += (int)ctrls[b] & (int)XKEYBOARD_CTRL_WIDTH_MASK;
        if (total <= 0) total = 1;
        for (b = rowStart[row]; b < rowStart[row + 1]; ++b) {
            int unit = (int)ctrls[b] & (int)XKEYBOARD_CTRL_WIDTH_MASK;
            /* 边界式：每键占 unit/total 行宽，x 为像素边界直加（原式
               (x-contentX+unit)*W/T 把像素偏移当单位累加，宽度逐键乘
               W/T 复利爆炸至 INT_MAX 溢出，键位矩形互相错位）。 */
            int xEnd = x + (int)(((int64_t)unit * contentW) / total);
            rects[b].x = x;
            rects[b].y = y;
            rects[b].width = xEnd - x;
            rects[b].height = rowH;
            x = xEnd;
        }
    }

    /* 提交（拒绝路径不触及成员，保持原布局）。 */
    self->m_keyCount = count;
    for (i = 0; i < (int)count; ++i) {
        self->m_keyCtrls[i] = ctrls[i];
        self->m_keyRects[i] = rects[i];
        self->m_keyLabels[i] = labels[i];
    }
}

/** @brief 命中测试（控件局部坐标 → 按钮索引；未命中返回 BUTTON_NONE）。 */
static uint32_t xkb_hitTest(const XVirtualKeyboard* self, int x, int y)
{
    uint32_t i;
    if (!self) return XKEYBOARD_BUTTON_NONE;
    for (i = 0; i < self->m_keyCount; ++i) {
        const XRect* r = &self->m_keyRects[i];
        if (x >= r->x && x < r->x + r->width &&
            y >= r->y && y < r->y + r->height)
            return i;
    }
    return XKEYBOARD_BUTTON_NONE;
}

/* ==================== 写入链（对标 lv_keyboard → textarea） ==================== */

/** @brief 向目标合成一次按键（XKeyEvent_init + XObject_event_base 直发，
 *         与真实输入同路径；demo 先例 xgui_window_demo.c:1332-1349）。 */
static void xkb_sendKey(XVirtualKeyboard* self, int key, XKeyboardModifiers modifiers)
{
    XKeyEvent ke;
    if (!self || !self->m_target) return;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, key, modifiers);
    XObject_event_base((XObject*)self->m_target, (XEvent*)&ke);
}

/** @brief 数字模式 +/- 键：翻转目标文本首字符符号（仅 XLineEdit 目标）。
 *  @details 主路径=光标 home → 原文首字符为 +/- 时选 1 删 → 行首插入
 *           新符号（全程公开 API，保留撤销栈与光标位，对标 LVGL 借临
 *           时移动光标的等价实现）；XLineEdit 适配缺席时仅诊断忽略。 */
static void xkb_signFlip(XVirtualKeyboard* self)
{
#if XLINEEDIT_ON
    XLineEdit* le;
    char first;
    const char* flipped;
    if (!self || !self->m_target) return;
    if (XClassGetVtable(self->m_target) != XLineEdit_class_init()) {
        XPrintf("[XVirtualKeyboard] +/-: 目标类型不支持符号翻转（仅 XLineEdit）\n");
        return;
    }
    le = (XLineEdit*)self->m_target;
    XLineEdit_home(le, false);
    first = XLineEdit_text(le)[0];
    flipped = (first == '-') ? "+" : "-";
    if (first == '+' || first == '-') {
        XLineEdit_setSelection(le, 0, 1);
        XLineEdit_del(le);
    }
    XLineEdit_insert(le, flipped);
#else
    (void)self;
    XPrintf("[XVirtualKeyboard] +/-: XLineEdit 适配缺席（XLINEEDIT_ON=0），忽略\n");
#endif /* XLINEEDIT_ON */
}

/**
 * @brief 把按钮文本写入输出目标（字符键公共出口）。
 * @details 双路：单字节可打印 ASCII（0x20-0x7E）合成 XKeyEvent 直发
 *          （类型无关；大写字母键自动携带 ShiftModifier——漏 Shift 则
 *          TEXT_UPPER 打出全小写，先例 xgui_window_demo.c:1343-1346）；
 *          其余（多字节 UTF-8/换行符）按目标 vtable 分派公开插入 API
 *          直写，目标改文本后其自身 textChanged/textEdited 信号照常发
 *          射。类型未识别时仅诊断（不猜测写入通道）。
 */
static void xkb_writeText(XVirtualKeyboard* self, const char* text)
{
    size_t len;
    if (!self || !self->m_target || !text || !text[0]) return;
    len = XStrlen(text);
    if (len == 1 && text[0] >= 0x20 && text[0] <= 0x7E) {
        XKeyboardModifiers mods = XKeyboardModifier_NoModifier;
        char out = text[0];
#if XVIRTUALKEYBOARD_ON
        /* shifthandler 出字链（shiftHandler :507-510 对齐）：上下文大写
         * 态（shiftActive||capsLockActive）小写键帽打出大写——布局不随
         * shift 重建（视觉映射不变），大写在此单点落地。手动 TextUpper
         * 模式（1#/ABC 表切换）走大写键帽，上下文态为 false 不受影响。 */
        {
            XVirtualKeyboardInputContext* ctx = xkb_contextOf(self);
            if (ctx && out >= 'a' && out <= 'z' &&
                XVirtualKeyboardInputContext_isUppercase(ctx)) {
                out = (char)(out - 'a' + 'A');
                mods = XKeyboardModifier_ShiftModifier;
            }
        }
#endif
        if (out >= 'A' && out <= 'Z')
            mods = XKeyboardModifier_ShiftModifier;
        xkb_sendKey(self, (int)(unsigned char)out, mods);
        return;
    }
#if XLINEEDIT_ON
    if (XClassGetVtable(self->m_target) == XLineEdit_class_init()) {
        XLineEdit_insert((XLineEdit*)self->m_target, text);
        return;
    }
#endif
#if XPLAINTEXTEDIT_ON
    if (XClassGetVtable(self->m_target) == XPlainTextEdit_class_init()) {
        XPlainTextEdit_insertPlainText((XPlainTextEdit*)self->m_target, text);
        return;
    }
#endif
#if XTEXTEDIT_ON
    if (XClassGetVtable(self->m_target) == XTextEdit_class_init()) {
        XTextEdit_insertPlainText((XTextEdit*)self->m_target, text);
        return;
    }
#endif
    XPrintf("[XVirtualKeyboard] writeText: 目标类型不支持直写，忽略（合成键路径"
            "仅承载单字节 ASCII）\n");
}

/** @brief 换行动作：XLineEdit 合成 Return（触发 returnPressed 信号链）；
 *         多行控件经 writeText 直插 "\n"（XPlainTextEdit/XTextEdit 的
 *         insertPlainText 支持 \n 跨行）；其余目标回退合成 Return。 */
static void xkb_sendNewline(XVirtualKeyboard* self)
{
    if (!self || !self->m_target) return;
#if XLINEEDIT_ON
    if (XClassGetVtable(self->m_target) == XLineEdit_class_init()) {
        xkb_sendKey(self, XKey_Return, XKeyboardModifier_NoModifier);
        return;
    }
#endif
#if XPLAINTEXTEDIT_ON
    if (XClassGetVtable(self->m_target) == XPlainTextEdit_class_init()) {
        xkb_writeText(self, "\n");
        return;
    }
#endif
#if XTEXTEDIT_ON
    if (XClassGetVtable(self->m_target) == XTextEdit_class_init()) {
        xkb_writeText(self, "\n");
        return;
    }
#endif
    xkb_sendKey(self, XKey_Return, XKeyboardModifier_NoModifier);
}

/**
 * @brief 按内置语义处理指定按钮（可复用入口，对标 lv_keyboard_def_event_cb）。
 * @return 已识别处理返回 true；按钮无文本/索引越界返回 false。
 */
static bool xkb_activateButton(XVirtualKeyboard* self, uint32_t buttonId)
{
    const char* text;
    XVarList* args;
    if (!self || buttonId >= self->m_keyCount) return false;
    self->m_selectedKey = buttonId;
    /* 任意按钮激活信号（对标按钮矩阵 VALUE_CHANGED，param=按钮 id）。 */
    args = XVarList_Create(XVar(int32_t, buttonId));
    if (args) {
        if (((XObject*)self)->m_signalSlot)
            XObject_emitSignal((XObject*)self,
                               (size_t)XVirtualKeyboard_buttonActivated_signal(NULL, 0),
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        else
            XVarList_delete(args);
    }
    text = XVirtualKeyboard_buttonText(self, buttonId);
    if (!text || !text[0]) return false;
#if XVIRTUALKEYBOARD_ON
    /* 拼音面板本地拦截（四条触发路径全部汇经本分发函数）：
       ① 中/EN 切换键——engine.setInputMode 翻转（两态均拦截，否则标签
       经字符键回路把字面量写进编辑框）；② 数字 1..9——面板分
       页换算后 model.selectItem（提交经插件候选钩子→context.commit→
       写入链）。其余按键单点经 xkb_routeKey 投 engine 虚键（插件可
       消费：字母进组串/退格删组串/回车原串/空格首选），未消费
       回落内置控制键匹配与字符写入。 */
    if (self->m_imeEnabled && self->m_mode == XKeyboardMode_User1 &&
        XStrcmp(text, XKEYBOARD_LBL_IME) == 0) {
        XVirtualKeyboard_setImeChinese(self, !XVirtualKeyboard_imeChinese(self));
        return true;
    }
    if (self->m_imeEnabled && self->m_mode == XKeyboardMode_User1 &&
        text[1] == '\0' && text[0] >= '1' && text[0] <= '9' &&
        xkb_imeInterceptDigit(self, text[0]))
        return true;
    if (self->m_imeEnabled && self->m_mode == XKeyboardMode_User1 &&
        xkb_routeKey(self, text)) {
        xkb_imeSyncPageSize(self); /* 组串变化：变宽 chip 页容量同步。 */
        XWidget_update((XWidget*)self);
        return true;
    }
#endif
    /* 模式切换键。 */
    if (XStrcmp(text, XKEYBOARD_LBL_LOWER) == 0) {
        XVirtualKeyboard_setMode(self, XKeyboardMode_TextLower);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_UPPER) == 0) {
#if XVIRTUALKEYBOARD_ON
        /* shifthandler 单点（Qt Keyboard.qml shift 键 click→toggleShift
           口径）：单击=临时大写（sticky）、间隔内双击=capsLock 钉住、
           caps 下单击=解除——双击判定/互斥在 context.toggleShift 内部
           （XStyleHints 双击间隔数据源）。模式读数随 isUppercase 同步
           （TextUpper/TextLower），键帽布局不重建（视觉映射不变；大写
           出字经 writeText 的上下文大写钩子落地）。 */
        XVirtualKeyboardInputContext_toggleShift(xkb_contextOf(self));
        xkb_syncShiftMode(self);
        return true;
#else
        XVirtualKeyboard_setMode(self, XKeyboardMode_TextUpper);
        return true;
#endif
    }
    if (XStrcmp(text, XKEYBOARD_LBL_SPECIAL) == 0) {
        XVirtualKeyboard_setMode(self, XKeyboardMode_Special);
        return true;
    }
    /* 确认键：仅发 ready 信号不自动收层（对标 LVGL 默认无动作）。 */
    if (XStrcmp(text, XKEYBOARD_LBL_OK) == 0) {
        XVarList* empty = XVarList_create(0);
        if (empty) {
            if (((XObject*)self)->m_signalSlot)
                XObject_emitSignal((XObject*)self,
                                   (size_t)XVirtualKeyboard_ready_signal(NULL),
                                   empty, NULL, NULL,
                                   XEVENT_PRIORITY_NORMAL);
            else
                XVarList_delete(empty);
        }
        return true;
    }
    /* 关闭/收起键：发 cancel 信号；弹层可见即自动收层（XGui 扩展行为）。
       组串中先弃草稿（engine reset——closePopup 弃草稿链一致口径）。 */
    if (XStrcmp(text, XKEYBOARD_LBL_CLOSE) == 0 ||
        XStrcmp(text, XKEYBOARD_LBL_DISMISS) == 0) {
#if XVIRTUALKEYBOARD_ON
        if (xkb_imeComposing(self) && xkb_engineOf(self))
            XVirtualKeyboardInputEngine_reset(xkb_engineOf(self));
#endif
        XVarList* empty = XVarList_create(0);
        if (empty) {
            if (((XObject*)self)->m_signalSlot)
                XObject_emitSignal((XObject*)self,
                                   (size_t)XVirtualKeyboard_cancel_signal(NULL),
                                   empty, NULL, NULL,
                                   XEVENT_PRIORITY_NORMAL);
            else
                XVarList_delete(empty);
        }
        if (XVirtualKeyboard_popupVisible(self)) XVirtualKeyboard_closePopup(self);
        return true;
    }
    /* 换行键：同时匹配 "Enter" 字面量（对标 KB.c:371 兼容 LVGL 移植）。
       closeOnReturn 新增分支：设置开且非 MultiLine → 回车后收面板。 */
    if (XStrcmp(text, XKEYBOARD_LBL_NEWLINE) == 0 ||
        XStrcmp(text, XKEYBOARD_LBL_ENTER) == 0) {
        xkb_sendNewline(self);
#if XVIRTUALKEYBOARD_ON
        {
            XVirtualKeyboardSettings* settings =
                XVirtualKeyboardSettings_instance();
            if (settings && XVirtualKeyboardSettings_closeOnReturn(settings) &&
                !(XVirtualKeyboardInputContext_inputMethodHints(
                      xkb_contextOf(self)) &
                  XInputMethodHint_MultiLine) &&
                self->m_popped)
                XVirtualKeyboard_closePopup(self);
        }
#endif
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_LEFT) == 0) {
        xkb_sendKey(self, XKey_Left, XKeyboardModifier_NoModifier);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_RIGHT) == 0) {
        xkb_sendKey(self, XKey_Right, XKeyboardModifier_NoModifier);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_BACKSPACE) == 0) {
        xkb_sendKey(self, XKey_Backspace, XKeyboardModifier_NoModifier);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_SIGN) == 0) {
        xkb_signFlip(self);
        return true;
    }
    /* 其余按钮文本按字符键写入。 */
    xkb_writeText(self, text);
    return true;
}

/* ==================== 定时器（长按重复 + 守护轮询） ==================== */

/** @brief 停止长按重复定时器（幂等）。 */
static void xkb_stopRepeat(XVirtualKeyboard* self)
{
    if (!self || self->m_repeatTimer == XTIMER_INVALID_ID) return;
    XObject_killTimer((XObject*)self, self->m_repeatTimer);
    self->m_repeatTimer = XTIMER_INVALID_ID;
}

/** @brief 按下可重复键时启动长按计时（先 400ms 起振；先停旧计时再判
 *         NO_REPEAT，避免前一按键的挂起计时串键）。 */
static void xkb_startRepeat(XVirtualKeyboard* self)
{
    if (!self || self->m_pressedKey == XKEYBOARD_BUTTON_NONE) return;
    if ((int)self->m_keyCtrls[self->m_pressedKey] &
        (int)XKEYBOARD_CTRL_NO_REPEAT)
        return;
    /* 节拍单一事实源=engine 头宏（600/50；测试注入覆写同源生效）——
       「重复从面板 400/100 移入 engine 600/50」的节拍承载点。 */
    self->m_repeatTimer = XObject_startTimer_ms(
        (XObject*)self, XVIRTUALKEYBOARD_REPEAT_FIRST_MS,
        XTimerType_CoarseTimer);
}

/** @brief 长按 tick：重发当前按住键并切换到 50ms 重复间隔（重启定时器
 *         即换挡，无需相位计数）。 */
static void xkb_repeatTick(XVirtualKeyboard* self)
{
    if (self->m_pressedKey == XKEYBOARD_BUTTON_NONE || !self->m_pressArmed) {
        return;
    }
    xkb_activateButton(self, self->m_pressedKey);
    self->m_repeatTimer = XObject_startTimer_ms(
        (XObject*)self, XVIRTUALKEYBOARD_REPEAT_MS, XTimerType_CoarseTimer);
}

/** @brief 停止守护轮询定时器（幂等）。 */
static void xkb_stopGuard(XVirtualKeyboard* self)
{
    if (!self || self->m_guardTimer == XTIMER_INVALID_ID) return;
    XObject_killTimer((XObject*)self, self->m_guardTimer);
    self->m_guardTimer = XTIMER_INVALID_ID;
}

/** @brief 启动守护轮询（幂等；运行条件=弹层可见或 autoPopup 开；
 *         XVIRTUALKEYBOARD_DESKTOP_ON=0 时守护整体停用——仅显式
 *         popup/总开关形态）。 */
static void xkb_startGuard(XVirtualKeyboard* self)
{
    if (!self || self->m_guardTimer != XTIMER_INVALID_ID) return;
#if !XVIRTUALKEYBOARD_DESKTOP_ON
    return;
#endif
    if (!self->m_autoPopup && !self->m_popped) return;
    self->m_guardTimer = XObject_startTimer_ms(
        (XObject*)self, XKEYBOARD_GUARD_INTERVAL_MS, XTimerType_CoarseTimer);
}

/** @brief 按宿主当前几何重定位弹层（高度钳位：hostH/2 → 下界 120 →
 *         上界 hostH-32，上界最终生效恒给编辑区留 32px）。
 *  @details 内嵌形态（m_floating=false）按宿主局部坐标
 *           setGeometry(0, hostH-kbH, hostW, kbH)；悬浮形态
 *           （setHostWindow 锚生效）按宿主全局坐标
 *           setGeometry(gx, gy+hostH-kbH, hostW, kbH)——顶层无父偏移
 *           全局坐标即屏幕坐标（XComboBox 弹层 setGeometryRect 先例），
 *           外观/尺寸与内嵌形态完全一致（同一钳位公式、宿主全宽），
 *           仅坐标系不同。两种形态均缓存宿主宽高（悬浮另缓存全局位
 *           置），供守护 tick ③ Resize/位移检测比对。 */
static void xkb_reposition(XVirtualKeyboard* self)
{
    int kbH;
    if (!self || !self->m_host) return;
    self->m_hostW = XWidget_width(self->m_host);
    self->m_hostH = XWidget_height(self->m_host);
    kbH = self->m_hostH / 2;
    if (kbH < XKEYBOARD_POPUP_MIN_H) kbH = XKEYBOARD_POPUP_MIN_H;
    if (kbH > self->m_hostH - XKEYBOARD_POPUP_MARGIN_H)
        kbH = self->m_hostH - XKEYBOARD_POPUP_MARGIN_H;
    if (kbH < 1) kbH = 1;
    self->m_popupHeight = kbH;
    if (self->m_floating) {
        XPoint anchorLocal;
        XPoint anchorGlobal;
        XPoint originGlobal;
        anchorLocal.x = 0;
        anchorLocal.y = self->m_hostH - kbH;
        anchorGlobal = XWidget_mapToGlobal(self->m_host, &anchorLocal);
        /* 缓存基准改=宿主 (0,0) 全局点（r2 项5c）：守护 ③ 用
         * mapToGlobal(host,(0,0)) 比对，原缓存取键盘贴附点
         * (0,hostH-kbH) 恒失配 → 每 200ms 守护 tick 空转一次
         * reposition（几何相同的 churn），并把外部对键盘几何的合法调
         * 整（XDateTimeEdit 时间行避让）打回原位。放置点本式不变。 */
        originGlobal.x = 0;
        originGlobal.y = 0;
        originGlobal = XWidget_mapToGlobal(self->m_host, &originGlobal);
        self->m_hostGX = originGlobal.x;
        self->m_hostGY = originGlobal.y;
        XWidget_setGeometry((XWidget*)self, anchorGlobal.x, anchorGlobal.y,
                            self->m_hostW, kbH);
    } else {
        XWidget_setGeometry((XWidget*)self, 0, self->m_hostH - kbH,
                            self->m_hostW, kbH);
    }
}

/**
 * @brief 守护轮询 tick（单次遍历，幂等；焦点跟随兜底，非弹收主判据）。
 * @details 自动弹收主判据=按下位置驱动（notifyPress，见
 *          XGuiApplication_virtualKeyboardNotifyPress 转发链：PRESS 命中
 *          受支持编辑框→弹/重绑、非编辑→收，点空白收起不依赖焦点迁
 *          移）。本守护轮询降为兜底，保留原因：①焦点跟随换框（物理键
 *          盘 Tab 遍历/程序化 setFocus 等无 PRESS 的焦点迁移仍自动重
 *          绑/收层）；②IME 候选带等稳态巡检依赖轮询（宿主 Resize 重
 *          定位③）。三段（统一边沿，去 m_target 水平判据）：①m_target
 *          不可见→closePopup（原样）；②边沿 focus != m_prevFocus——焦
 *          点可接受（xkb_supportedTarget=vtable 快路径 ∪ WA14 放行回
 *          退，Qt inputMethodAccepted 口径）&& isEnabled && 总开关→
 *          popup(focus)，否则（不可接受目标/NULL/总开关）若
 *          m_popped→closePopup；两路均更新 m_prevFocus=focus—
 *          对齐 Qt evaluateInputPanelVisible = m_visible &&
 *          (focusObject && inputMethodAccepted())（platforminputcontext.cpp
 *          :251-259/266-283）的 show/hide 双向。行为变化声明：点按
 *          「取焦点」的非编辑控件将收键盘（Qt 口径；按下路径同判据即
 *          时收，本段 200ms 内兜底二次确认）；不取焦点的控件（XGui 按
 *          钮默认不抢焦点）的收起由按下路径即时承载（旧「无边沿不收」
 *          缺口已由 notifyPress 闭合）；焦点往返重弹；同框 retap 经按
 *          下路径重弹（旧 dismissFix「同框 retap 不重弹」契约作废）；
 *          ③宿主 Resize→重定位（原样）。②′稳态不可接受收层：
 *          焦点存在、accept 判据为假且弹层仍在时幂等收层——accept 可
 *          在焦点不变时翻转（原地 opt-out/禁用/总开关切换），无边沿也
 *          要复核（Qt hide 臂的轮询化）。销毁路径已由 destroyed 防悬
 *          垂连接前置处理。
 * @note     accept 不含可见性合取：合成/无头环境从未 show 的顶层编辑
 *           框生效可见性恒假（XWidget_effectiveVisible 走父链），而其
 *           持有应用焦点即为活动输入目标（⑦.4 长按豁免同理）；控件
 *           隐藏/禁用在公共 API 下会自动清焦（setVisible(false)/
 *           setEnabled(false) 路径 clearFocusBase），失焦即由②覆盖。
 */
static void xkb_guardTick(XVirtualKeyboard* self)
{
    XWidget* focus;
    bool accept;
    if (!self) return;
    /* ① 目标不可见且非当前输入焦点→收层（页切换/祖先隐藏链）。
       判据加「非当前焦点」豁免：无头/合成环境里从未 show 的顶层编辑
       框生效可见性恒假（XWidget_isVisible 走父链），但其持有应用焦点
       即为活动输入目标——旧判据会在长按重复进行中把面板收掉（⑦.4 
       长按 600ms 起振失败的根因）。Qt 对齐口径：收层判据=焦点对象是
       否仍被输入法接受（edge 分支 accept），非控件可见性。 */
    if (self->m_target && self->m_target != XWidget_appFocusWidget() &&
        !XWidget_isVisible(self->m_target)) {
        XVirtualKeyboard_closePopup(self);
        return;
    }
    /* ② 焦点边沿（dismissFix 核心：判据去 m_target 化，边沿触发）。
       accept 兜底口径=qwidget.cpp:9057-9065（isEnabled()&&
       WA_InputMethodEnabled；评审遗留修正：原缺 isEnabled 合取，父链
       setEnabled(false) 传播禁用的持焦编辑框会误报可接受）。总开关关
       → 边沿分支走 closePopup 路径（面板随开关收起）。 */
    focus = XWidget_appFocusWidget();
#if XVIRTUALKEYBOARD_ON
    accept = focus && xkb_supportedTarget(focus) &&
             XWidget_isEnabled(focus) &&
             XWidget_testAttribute(
                 focus, XWidgetAttribute_InputMethodEnabled) &&
             XVirtualKeyboardSettings_keyboardEnabled(
                 XVirtualKeyboardSettings_instance());
#else
    accept = focus && xkb_supportedTarget(focus) &&
             XWidget_isEnabled(focus) &&
             XWidget_testAttribute(
                 focus, XWidgetAttribute_InputMethodEnabled);
#endif
    if (focus != self->m_prevFocus) {
        self->m_prevFocus = focus; /* 两路均更新采样（先记边沿已消费）。 */
        /* 焦点迁出（≠收起时目标）即清用户收起闩锁：换框跟随恢复。 */
        if (focus != self->m_target) self->m_userCollapsed = false;
        if (accept) {
            if (self->m_userCollapsed && focus == self->m_target) {
                /* 用户收起后原地（同目标）不重弹——收起语义保持。 */
                self->m_prevFocus = focus;
                return;
            }
            XVirtualKeyboard_popup(self, focus);
            return;
        }
        if (self->m_popped) {
            /* hide 方向（评审#4）：焦点边沿迁入不可接受目标（非编辑控件/
               NULL/总开关关）即收层——行为矩阵「总开关关→已弹层收
               起」与 ⑦.1 hide 方向断言的承载点；closePopup 内部同步
               m_prevFocus 采样，防收层瞬间被记为新边沿。 */
            XVirtualKeyboard_closePopup(self);
            return;
        }
    }
    /* ②′ 稳态不可接受收层（focusFocusDismiss）：焦点存在但不可接受
       （非受支持编辑控件/WA14 opt-out/禁用/总开关关）且弹层仍在→
       幂等收层。无边沿也要收：accept 判据可在焦点不变时原地翻转
       （setAttribute(WA14,false)/父链禁用传播/总开关切换），边沿分支
       只覆盖判据翻转恰好伴随焦点迁移的情形；焦点为 NULL 保持现状
       （Qt 无头模式口径，不主动收）。 */
    if (focus && !accept && self->m_popped) {
        XVirtualKeyboard_closePopup(self);
        return;
    }
    /* ③ 宿主 Resize 检测（原样）。悬浮形态扩为 Resize+位移复核：主窗
       口被拖动/缩放时悬浮面板跟随重定位（内嵌形态是宿主子控件、随父
       移动，仅查尺寸不变）。 */
    if (self->m_host && self->m_popped) {
        bool hostMoved = XWidget_width(self->m_host) != self->m_hostW ||
                         XWidget_height(self->m_host) != self->m_hostH;
        if (!hostMoved && self->m_floating) {
            XPoint origin;
            XPoint global;
            origin.x = 0;
            origin.y = 0;
            global = XWidget_mapToGlobal(self->m_host, &origin);
            hostMoved = global.x != self->m_hostGX ||
                        global.y != self->m_hostGY;
        }
        if (hostMoved) xkb_reposition(self);
#if XWINDOW_ON
        /* 悬浮态 Z 序不变式复核（S5 回修）：外源 SetForegroundWindow/
         * activateWindow(锚主窗)（宿主焦点回交、自动化探针、他窗切换
         * 等路径）会把非 topmost 的锚主窗整体抬到键盘原生 Popup 之上，
         * 面板被遮蔽「隐形」（HWND 仍可见、捕获/键入照常，唯屏幕不可
         * 见——实测探针复现）。与 Qt「popup 恒浮于其父窗之上」不变式
         * 相悖，200ms 周期幂等校正：XWidget_raise = SetWindowPos
         * (HWND_TOP, SWP_NOACTIVATE|NOMOVE|NOSIZE)，不夺焦、零几何副
         * 作用（XPlatformNativeWindow_raise）。 */
        if (self->m_floating)
            XWidget_raise((XWidget*)self);
#endif
    }
}

/* ==================== 虚槽实现 ==================== */

/** @brief 绘制：面板底 + 逐键矩形 + 文本居中 + 按压态 + CHECKED 底色 +
 *         气泡带 + 拼音候选带（XVK）。全部键位文本用控件当前字体
 *         （默认=配置默认字库）居中绘制，控件零字体自带（不调
 *         XFont_setFamily）；键面/候选带配色=LVGL 单一路径（主题宏
 *         已删，双分支合并）。 */
static void VXKeyboard_paintEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r = XWidget_rect(self);
    int bandH = 0;
    int rowH = 0;
    uint32_t i;

    (void)event;
    if (!kb || r.width <= 4 || r.height <= 4) return;
    r.x = 0;
    r.y = 0;

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
    /* 绘制字体 = 控件字体（默认构造字体即配置默认字库，见 fontPlan）。 */
    XPainter_setFont(&painter, &((XWidget*)self)->m_font);

    /* 主题化面板（LVGL keyboard 容器=scr 样式：平涂 0xF5F5F5、无凹
       边、无圆角；唯一绘制路径，调色板旧分支随
       XKEYBOARD_THEME_LVGL_ON 删除）。 */
    XPainter_fillRect(&painter, &r, XKB_LVGL_SCR);
    /* 气泡带高度（与布局同判据：顶行「任一键」含 POPOVER 即预留一行
       高——原仅查 0 号键，文本布局 0 号键为 "1#" 无 POPOVER，导致带
       空置且气泡永不绘制）。 */
    if (kb->m_popovers && kb->m_keyCount > 0) {
        int rows = 1;
        for (i = 0; i + 1 < kb->m_keyCount; ++i) {
            if (kb->m_keyRects[i + 1].y != kb->m_keyRects[i].y) ++rows;
        }
        for (i = 0; i < kb->m_keyCount; ++i) {
            if (kb->m_keyRects[i].y != kb->m_keyRects[0].y) break;
            if (((int)kb->m_keyCtrls[i] & (int)XKEYBOARD_CTRL_POPOVER) != 0) {
                bandH = (r.height - 4) / rows;
                break;
            }
        }
        rowH = kb->m_keyRects[0].height;
    }

#if XVIRTUALKEYBOARD_ON
    /* 拼音候选带（内嵌顶行；布局已预留 m_imeBandRect，无带即空操作；
       与气泡带判据互斥——拼音表无 POPOVER 位，上方 bandH 恒 0）。 */
    xkb_imeBandPaint(kb, &painter);
#endif

    /* 主题化逐键：底色（CHECKED→GREY、按住武装→dark_filter(35) 颜色
       数学）+ 圆角矩形一次成形（绘制期内缩 2px 形成键视觉间距，
       m_keyRects 命中几何不动；LVGL 键面无 border/无阴影）。 */
    for (i = 0; i < kb->m_keyCount; ++i) {
        const XRect* kr = &kb->m_keyRects[i];
        XRect faceRect;
        bool pressed = (kb->m_pressedKey == i && kb->m_pressArmed);
        bool checked = ((int)kb->m_keyCtrls[i] &
                        (int)XKEYBOARD_CTRL_CHECKED) != 0;
        const char* text = XVirtualKeyboard_buttonText(kb, i);
        uint32_t fill = pressed ? XKB_LVGL_PRESSED_FACE
                                : (checked ? XKB_LVGL_GREY : XKB_LVGL_CARD);
        uint32_t txt = pressed ? XKB_LVGL_PRESSED_TEXT : XKB_LVGL_TEXT;
        if (kr->width < 5 || kr->height < 5) continue;
        faceRect.x = kr->x + 2;
        faceRect.y = kr->y + 2;
        /* 宽高 -5 而非 -4：xkb_roundRect→XPainter_drawRoundedRect 按
           路径 x+w/y+h 收边后再描 1px 同色边（落在填充区外侧的
           x+w/y+h 边界像素上，XPainter.c drawRoundedRect 填充+折线描
           边两段式），-4 会让键面白色右/下各溢出 1px、4px 面板缝变
           3px（风格检查 pixelPlan ①「4px 缝全为面板」失配）。-5 使
           填充+描边合成可视面恰为命中矩形四边内缩 2px。 */
        faceRect.width = kr->width - 5;
        faceRect.height = kr->height - 5;
        xkb_roundRect(&painter, &faceRect, fill, fill,
                      xkb_keyRadius(kr->height));
        if (text && text[0])
            XPainter_drawTextRect(&painter, &faceRect,
                                  XPAINTER_TEXT_ALIGN_CENTER |
                                      XPAINTER_TEXT_SINGLE_LINE,
                                  text, txt);
        /* 气泡：非浮层、绘制期实现——按压中的 POPOVER 键文本在键位上
           方预留带内放大区域绘制（relocate 语义对标 BM.c:765-767,
           796-800；同键面样式）。 */
        if (pressed && bandH > 0 &&
            ((int)kb->m_keyCtrls[i] & (int)XKEYBOARD_CTRL_POPOVER) != 0 &&
            text && text[0]) {
            XRect bubble;
            int bw = kr->width * 2;
            if (bw < 24) bw = 24;
            bubble.x = kr->x + kr->width / 2 - bw / 2;
            if (bubble.x < 2) bubble.x = 2;
            if (bubble.x + bw > r.width - 2) bubble.x = r.width - 2 - bw;
            if (bubble.x < 2) bubble.x = 2;
            bubble.y = 2;
            bubble.width = bw;
            bubble.height = (bandH > rowH ? bandH : rowH) - 4;
            if (bubble.height < 1) bubble.height = 1;
            xkb_roundRect(&painter, &bubble, XKB_LVGL_CARD, XKB_LVGL_CARD,
                          xkb_keyRadius(rowH));
            XPainter_drawTextRect(&painter, &bubble,
                                  XPAINTER_TEXT_ALIGN_CENTER |
                                      XPAINTER_TEXT_SINGLE_LINE,
                                  text, XKB_LVGL_TEXT);
        }
    }


    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 调整大小：几何随尺寸重建（行宽/行高/气泡带均按比例重算）。 */
static void VXKeyboard_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_RESIZE) return;
    xkb_rebuildLayout((XVirtualKeyboard*)self);
    XWidget_update(self);
}

/** @brief 鼠标按下：命中→武装（按下触发键立即激活；CLICK_TRIG/POPOVER
 *         键留释放触发）+ 可重复键起振长按计时 + 隐式抓取（按住滑动
 *         出键的取消/恢复语义依赖移动事件，对标 XLineEdit 按压抓取）。 */
static void VXKeyboard_mousePressEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    XMouseEvent* me;
    XPoint pos;
    uint32_t id;
    if (!kb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
#if XWINDOW_ON
    /* 悬浮态界外按下：收层+重放（Qt Popup 点外语义）。悬浮双抓取+原
       生捕获是本面板收到界外点击的唯一通道（跨顶层抓取改道 XWidget.c
       派发入口把一切按下改投抓取者），界外坐标在键面命中天然全 miss，
       原实现吞掉即死锁（日历弹层越界收层/点选收层/主窗口关闭钮全部
       不可达）。先 closePopup 成对释放并归还抢占前抓取者，重放管线即
       无抓取态；重放单次派发无回环（界外坐标不可能再落回已收层键
       盘）。 */
    if (kb->m_floating && kb->m_popped &&
        (pos.x < 0 || pos.y < 0 ||
         pos.x >= XWidget_width(self) || pos.y >= XWidget_height(self))) {
        XPoint g = XMouseEvent_globalPosition(me);
        XWidget* redir;
        if (g.x == 0 && g.y == 0) /* 平台未注入全局坐标兜底（XEvent.h
                                     globalPosition 契约；win32 必注入）。 */
            g = XWidget_mapToGlobal(self, &pos);
        XVirtualKeyboard_closePopup(kb);
        /* closePopup 刚归还的被抢占弹层（日历/时间弹层链；prev==NULL
           表示无被抢占弹层，下方第一发重放已直达命中控件）。 */
        redir = XWidget_mouseGrabber();
        XWidget_replayPressAtGlobal(me, &g);
        /* 链式二次派发（S4：一次点击级联关闭整条弹层链并直达真实命中
           控件，主窗自绘关闭钮首击生效）：第一发重放被恢复的弹层抓取
           改道→弹层越界分支同步自关（此刻鼠标/键盘抓取者皆空）。判据
           三合一：①确有被抢占弹层归还（redir 非空且非自己）；②命中点
           在弹层顶层几何之外（排除「弹层内容自关」——如点选日期经
           calClick 收层，此拍已被弹层内容消费，不得再透传）；③两抓取
           者已空（弹层链确已闭合）。三判据齐备才补发第二发——此刻无
           抓取者，直达光标下控件（Qt 点外重放口径）。 */
        if (redir && redir != (XWidget*)kb) {
            XWidget* redirTop = XWidget_topLevelWidget(redir);
            if (redirTop && !XWidget_mouseGrabber() &&
                !XWidget_keyboardGrabber()) {
                XPoint lp = XWidget_mapFromGlobal(redirTop, &g);
                if (lp.x < 0 || lp.y < 0 ||
                    lp.x >= XWidget_width(redirTop) ||
                    lp.y >= XWidget_height(redirTop))
                    XWidget_replayPressAtGlobal(me, &g);
            }
        }
        XEvent_accept(event);
        return;
    }
#endif
#if XVIRTUALKEYBOARD_ON
    /* 候选带命中优先（xkb_hitTest 之前）：命中即 press 直触发（chip/
       翻页/「中」chip），不进 m_pressedKey 武装链——键位矩形起点已在
       带下方，两区域天然无交叠，未命中带才走按键路径。 */
    if (xkb_imeBandHit(kb, pos.x, pos.y)) {
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
#endif
    id = xkb_hitTest(kb, pos.x, pos.y);
    kb->m_pressedKey = id;
    kb->m_pressArmed = (id != XKEYBOARD_BUTTON_NONE);
    if (id != XKEYBOARD_BUTTON_NONE) {
        int ctrl = (int)kb->m_keyCtrls[id];
        if ((ctrl & (int)XKEYBOARD_CTRL_CLICK_TRIG) == 0 &&
            (ctrl & (int)XKEYBOARD_CTRL_POPOVER) == 0) {
            /* 默认按下触发（无 CLICK_TRIG 且无 POPOVER）。 */
            xkb_activateButton(kb, id);
        }
        xkb_startRepeat(kb);
    }
    XWidget_grabMouse(self);
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 鼠标移动：按住滑动出键取消武装不触发、回滑恢复（BM.c:446-506
 *         语义）；仅在按下序列内生效。 */
static void VXKeyboard_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    XMouseEvent* me;
    XPoint pos;
    uint32_t id;
    bool armed;
    if (!kb || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    if (kb->m_pressedKey == XKEYBOARD_BUTTON_NONE) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    id = xkb_hitTest(kb, pos.x, pos.y);
    armed = (id == kb->m_pressedKey);
    if (armed != kb->m_pressArmed) {
        kb->m_pressArmed = armed;
        if (armed)
            xkb_startRepeat(kb); /* 回滑恢复：重起长按计时。 */
        else
        XWidget_update(self);
    }
    XEvent_accept(event);
}

/** @brief 鼠标释放：释放触发键（CLICK_TRIG/POPOVER）在按住原键且未滑
 *         出时激活；解除按压态/抓取/长按计时。 */
static void VXKeyboard_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    XMouseEvent* me;
    XPoint pos;
    uint32_t id;
    if (!kb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    id = xkb_hitTest(kb, pos.x, pos.y);
    if (kb->m_pressedKey != XKEYBOARD_BUTTON_NONE && kb->m_pressArmed &&
        id == kb->m_pressedKey) {
        int ctrl = (int)kb->m_keyCtrls[id];
        if ((ctrl & (int)XKEYBOARD_CTRL_CLICK_TRIG) != 0 ||
            (ctrl & (int)XKEYBOARD_CTRL_POPOVER) != 0)
            xkb_activateButton(kb, id);
    }
    kb->m_pressedKey = XKEYBOARD_BUTTON_NONE;
    kb->m_pressArmed = false;
#if XVIRTUALKEYBOARD_ON
#endif
    if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 隐藏：停长按计时；弹层态复位。守护定时器按运行条件保留
 *         （autoPopup 开时键盘隐藏期间仍需轮询以便自动再弹出）。 */
static void VXKeyboard_hideEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    if (!kb || !event || XEvent_type(event) != XEVENT_TYPE_HIDE) return;
    kb->m_popped = false;
    kb->m_pressedKey = XKEYBOARD_BUTTON_NONE;
    kb->m_pressArmed = false;
#if XVIRTUALKEYBOARD_ON
    /* 页切换/祖先隐藏弃草稿（engine reset→插件 resetComposition，
       不翻转中文态）。 */
    if (xkb_engineOf(kb)) XVirtualKeyboardInputEngine_reset(xkb_engineOf(kb));
#endif
    if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
    /* 悬浮面板隐藏随行释放键盘抓取与原生鼠标捕获（弹出时 popup 悬浮
       分支夺取的弹层模态抓取，成对归还；identity 判据不误放他者）。 */
    if (XWidget_keyboardGrabber() == self) XWidget_releaseKeyboard(self);
#if XWINDOW_ON
    {
        XWindow* handle = XWidget_windowHandle(self);
        if (handle) XWindow_setMouseGrabEnabled(handle, false);
    }
#endif
    if (!kb->m_autoPopup) xkb_stopGuard(kb);
}

/** @brief 定时器分派：重复 tick + 守护 tick；非本类 id 链回基类
 *         （XCompleter.c:627-630 范式，漏链则基类定时器全哑）。 */
static void VXKeyboard_timerEvent(XObject* object, XTimerEvent* event)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)object;
    XTimerId id;
    if (!self || !event) return;
    id = (XTimerId)XTimerEvent_timerId(event);
    if (id == self->m_repeatTimer) {
        xkb_repeatTick(self);
        return;
    }
    if (id == self->m_guardTimer) {
        xkb_guardTick(self);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(object, event);
}

/** @brief 反初始化：停全部定时器并解绑 m_target/m_host 连接后调父类。 */
static void VXKeyboard_deinit(XVirtualKeyboard* self)
{
    if (!self) return;
    xkb_stopGuard(self);
    if (self->m_targetConn) {
        XObject_disconnect_2(self->m_targetConn);
        self->m_targetConn = NULL;
    }
    if (self->m_hostConn) {
        XObject_disconnect_2(self->m_hostConn);
        self->m_hostConn = NULL;
    }
    self->m_target = NULL;
    self->m_host = NULL;
    self->m_hostOverride = NULL; /* 悬浮锚为借用指针，反初始化一并解除。 */
    self->m_hostHint = NULL;
    self->m_prevMouseGrab = NULL; /* 抓取者快照同属运行态借用，一并解除。 */
    self->m_prevKbdGrab = NULL;
    self->m_floating = false;
    self->m_keyCount = 0;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/** @brief 深拷贝：基类深拷贝后复制槽位指针（借用语义）与布局快照；
 *         绑定/弹层/定时器属对象运行态，不随拷贝迁移（防悬垂）。 */
static void VXKeyboard_copy(XVirtualKeyboard* self, const XVirtualKeyboard* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XVirtualKeyboard_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_mode = other->m_mode;
    self->m_popovers = other->m_popovers;
    self->m_autoPopup = other->m_autoPopup;
    for (i = 0; i <= XKEYBOARD_MODE_SLOT_COUNT; ++i) {
        self->m_maps[i] = other->m_maps[i];
        self->m_ctrls[i] = other->m_ctrls[i];
    }
    self->m_keyCount = other->m_keyCount;
    for (i = 0; i < (int)self->m_keyCount; ++i) {
        self->m_keyCtrls[i] = other->m_keyCtrls[i];
        self->m_keyRects[i] = other->m_keyRects[i];
    }
    self->m_hostW = other->m_hostW;
    self->m_hostH = other->m_hostH;
    self->m_hostGX = other->m_hostGX;
    self->m_hostGY = other->m_hostGY;
    self->m_popupHeight = other->m_popupHeight;
#if XVIRTUALKEYBOARD_ON
    /* 候选带几何/插件装载镜像/分页面板本地状态随拷贝迁移；落地契约
       连接/守护采样属运行态，不跨对象迁移。 */
    self->m_imeBandRect = other->m_imeBandRect;
    self->m_imeEnabled = other->m_imeEnabled;
    self->m_candidatePage = other->m_candidatePage;
    self->m_candidatePageSize = other->m_candidatePageSize;
#endif
    /* 运行态归零：绑定/连接/弹层/按压/定时器不跨对象迁移。 */
    self->m_selectedKey = XKEYBOARD_BUTTON_NONE;
    self->m_pressedKey = XKEYBOARD_BUTTON_NONE;
    self->m_pressArmed = false;
    self->m_popped = false;
    self->m_target = NULL;
    self->m_host = NULL;
    self->m_hostOverride = NULL; /* 悬浮锚属运行态借用，不跨对象迁移。 */
    self->m_hostHint = NULL;
    self->m_prevMouseGrab = NULL; /* 抓取者快照同属运行态借用，不迁移。 */
    self->m_prevKbdGrab = NULL;
    self->m_floating = false;
    self->m_targetConn = NULL;
    self->m_hostConn = NULL;
    /* 守护/重复定时器按拷贝结果重同步（init 兜底路径可能已把守护拉
       起；autoPopup=false 时不得遗留运行中的轮询）。 */
    xkb_stopGuard(self);
    if (self->m_autoPopup) xkb_startGuard(self);
    XWidget_update((XWidget*)self);
}

/** @brief 移动语义：基类移动后整体转移槽位与布局快照；源定时器停运、
 *         源 destroyed 连接断开（连接以源为接收方，不可随指针转移），
 *         目标按新宿主重挂防悬垂连接；源归构造默认值。 */
static void VXKeyboard_move(XVirtualKeyboard* self, XVirtualKeyboard* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XVirtualKeyboard_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    xkb_stopGuard(other);
    if (other->m_targetConn) {
        XObject_disconnect_2(other->m_targetConn);
        other->m_targetConn = NULL;
    }
    if (other->m_hostConn) {
        XObject_disconnect_2(other->m_hostConn);
        other->m_hostConn = NULL;
    }
    self->m_mode = other->m_mode;
    self->m_popovers = other->m_popovers;
    self->m_autoPopup = other->m_autoPopup;
    for (i = 0; i <= XKEYBOARD_MODE_SLOT_COUNT; ++i) {
        self->m_maps[i] = other->m_maps[i];
        self->m_ctrls[i] = other->m_ctrls[i];
        other->m_maps[i] = NULL;
        other->m_ctrls[i] = NULL;
    }
    self->m_keyCount = other->m_keyCount;
    other->m_keyCount = 0;
    for (i = 0; i < XKEYBOARD_MAX_BUTTONS; ++i) {
        self->m_keyCtrls[i] = other->m_keyCtrls[i];
        self->m_keyRects[i] = other->m_keyRects[i];
        other->m_keyCtrls[i] = (XKeyboardButtonCtrl)0;
        XRect_init(&other->m_keyRects[i], 0, 0, 0, 0);
    }
    self->m_target = other->m_target;
    other->m_target = NULL;
    self->m_host = other->m_host;
    other->m_host = NULL;
    self->m_hostOverride = other->m_hostOverride; /* 悬浮态随宿主整体转移。 */
    other->m_hostOverride = NULL;
    self->m_hostHint = other->m_hostHint;
    other->m_hostHint = NULL;
    /* 抓取者快照不随移动转移（框架抓取态不在源对象上，快照即失效）：
       双方清空，悬浮重弹时由 popup 重拍。 */
    self->m_prevMouseGrab = NULL;
    other->m_prevMouseGrab = NULL;
    self->m_prevKbdGrab = NULL;
    other->m_prevKbdGrab = NULL;
    self->m_floating = other->m_floating;
    other->m_floating = false;
    self->m_hostW = other->m_hostW;
    self->m_hostH = other->m_hostH;
    self->m_hostGX = other->m_hostGX;
    self->m_hostGY = other->m_hostGY;
    self->m_popupHeight = other->m_popupHeight;
    other->m_hostW = 0;
    other->m_hostH = 0;
    other->m_hostGX = 0;
    other->m_hostGY = 0;
    other->m_popupHeight = 0;
#if XVIRTUALKEYBOARD_ON
    /* 候选带几何/插件装载镜像/分页状态随移动转移，源归零（构造默认：
       开关关/无带/首页/页容量 9）。 */
    self->m_imeBandRect = other->m_imeBandRect;
    self->m_imeEnabled = other->m_imeEnabled;
    self->m_candidatePage = other->m_candidatePage;
    self->m_candidatePageSize = other->m_candidatePageSize;
    /* 落地契约/长按重复连接不随移动转移（接收方 this 变更即失效），
       置空由 popup/setTextArea 幂等重连。 */
    self->m_commitConn = NULL;
    self->m_keyEventConn = NULL;
    other->m_commitConn = NULL;
    other->m_keyEventConn = NULL;
    XRect_init(&other->m_imeBandRect, 0, 0, 0, 0);
    other->m_imeEnabled = false;
    other->m_candidatePage = 0;
    other->m_candidatePageSize = 9;
#endif
    self->m_selectedKey = other->m_selectedKey;
    other->m_selectedKey = XKEYBOARD_BUTTON_NONE;
    other->m_pressedKey = XKEYBOARD_BUTTON_NONE;
    other->m_pressArmed = false;
    other->m_popped = false;
    /* 守护/重复定时器按移动结果重同步（init 兜底路径可能已把本方守
       护拉起，源侧 autoPopup 关时不得遗留运行中的轮询）。 */
    xkb_stopGuard(self);
    if (self->m_autoPopup) xkb_startGuard(self);
    /* 目标按新对象重挂防悬垂连接。 */
    if (self->m_target)
        self->m_targetConn = XObject_connect_1(
            (XObject*)self->m_target, XSignal(XObject_destroyed_signal),
            (XObject*)self, xkb_targetDestroyedSlot,
            XConnectionType_Direct);
    if (self->m_host)
        self->m_hostConn = XObject_connect_1(
            (XObject*)self->m_host, XSignal(XObject_destroyed_signal),
            (XObject*)self, xkb_hostDestroyedSlot,
            XConnectionType_Direct);
    XWidget_update((XWidget*)self);
}

/* ==================== 生命周期 ==================== */

XVtable* XVirtualKeyboard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboard)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXKeyboard_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXKeyboard_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXKeyboard_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXKeyboard_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXKeyboard_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VXKeyboard_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXKeyboard_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXKeyboard_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXKeyboard_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXKeyboard_deinit);

    return XVTABLE_DEFAULT;
}

void XVirtualKeyboard_init(XVirtualKeyboard* self, XWidget* parent, XWidgetFlags flags)
{
    int i;
    if (!self) return;
    /* 结构体先整体清零再初始化基类（XWidget_init 只清基类部分；槽位
       数组不清零会带堆残留，见 XLineEdit.c 内置 action 槽注释）。 */
    XMemset(self, 0, sizeof(*self));
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XVirtualKeyboard);

    self->m_mode = XKeyboardMode_TextLower;
    self->m_popovers = false;
    self->m_autoPopup = true;
    self->m_popped = false;
    self->m_userCollapsed = false;
    self->m_selectedKey = XKEYBOARD_BUTTON_NONE;
    self->m_pressedKey = XKEYBOARD_BUTTON_NONE;
    self->m_pressArmed = false;
    for (i = 0; i < XKEYBOARD_MODE_SLOT_COUNT; ++i) {
        self->m_maps[i] = s_kbMapDefaults[i];
        self->m_ctrls[i] = s_kbCtrlDefaults[i];
    }
    self->m_maps[XKEYBOARD_MODE_SLOT_COUNT] = NULL;
    self->m_ctrls[XKEYBOARD_MODE_SLOT_COUNT] = NULL;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_guardTimer = XTIMER_INVALID_ID;
#if XVIRTUALKEYBOARD_ON
    /* 候选带无带、插件装载镜像关、分页面板本地态首页/容量 9、落地
       契约连接未建、守护边沿采样空（状态机在插件，面板零直连）。 */
    self->m_prevFocus = NULL; /* 守护边沿采样（无条件成员）。 */
    XRect_init(&self->m_imeBandRect, 0, 0, 0, 0);
    self->m_imeEnabled = false;
    self->m_commitConn = NULL;
    self->m_keyEventConn = NULL;
    self->m_prevFocus = NULL;
    self->m_candidatePage = 0;
    self->m_candidatePageSize = 9;
#endif
    /* 弹层面板构造契约（对标 Qt InputPanel 构造即隐藏）：非窗口子控
       件形态补置 WState_Hidden——XWidget_init 只对顶层置 Hidden（子
       控件不带、随父 show 自动显示），页面 show 的传播会把未弹出的
       面板按默认子控件几何 (0,0,100,30) 整块画进页面（paintEvent 仅
       查宽高）并吞掉矩形内点击（childAt 逆序命中）——键盘页首行白
       卡残影根因。置位后 paintTree（!m_visible）与 childAt
       （WState_Hidden）双门拦截：未弹不绘制、不命中；popup()/显式
       show 唤起，弹出后键面与候选带绘制/命中照旧。顶层形态构造已
       Hidden、窗口形态子控件不随父传播（paintTree/childAt 均跳过
       isWindow），均无此患、不在此列。 */
    if (parent && !((XWidget*)self)->m_isWindow)
        XWidget_setVisible((XWidget*)self, false);
    xkb_rebuildLayout(self);
    /* autoPopup 默认开：构造即常驻守护轮询，兑现头文件『on 时守护定时
       器常驻轮询』承诺（首次 popup() 之前焦点落编辑框即可自动弹出）。
       无事件调度器环境（无头构造）下 startTimer 返回 INVALID_ID 不崩
       溃，后续 popup()/setAutoPopup 路径会重试拉起。 */
    xkb_startGuard(self);
}

XVirtualKeyboard* XVirtualKeyboard_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)XMemory_malloc(sizeof(XVirtualKeyboard), memory);
    if (!self) return NULL;
    XVirtualKeyboard_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 绑定与布局属性 ==================== */

/** @brief 重挂目标 destroyed 防悬垂连接（先断旧后连新；NULL 解绑）。 */
static void xkb_bindTargetDestroyed(XVirtualKeyboard* self, XWidget* newTarget)
{
    if (self->m_targetConn) {
        XObject_disconnect_2(self->m_targetConn);
        self->m_targetConn = NULL;
    }
    if (newTarget)
        self->m_targetConn = XObject_connect_1(
            (XObject*)newTarget, XSignal(XObject_destroyed_signal),
            (XObject*)self, xkb_targetDestroyedSlot,
            XConnectionType_Direct);
}

/** @brief 重挂宿主 destroyed 防悬垂连接（先断旧后连新；NULL 解绑）。 */
static void xkb_bindHostDestroyed(XVirtualKeyboard* self, XWidget* newHost)
{
    if (self->m_hostConn) {
        XObject_disconnect_2(self->m_hostConn);
        self->m_hostConn = NULL;
    }
    if (newHost)
        self->m_hostConn = XObject_connect_1(
            (XObject*)newHost, XSignal(XObject_destroyed_signal),
            (XObject*)self, xkb_hostDestroyedSlot,
            XConnectionType_Direct);
}

#if XVIRTUALKEYBOARD_ON

/** @brief 【落地契约槽】context.commitRequested(text) → 既有写入链。
 *         无目标编辑框/总开关关时丢弃（Qt 无平台集成层同样无处落地）。 */
static void xkb_commitSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)receiver;
    if (!self || !args) return;
    XVarList_args_1(args, const char*, commitText);
    if (!XVirtualKeyboardSettings_keyboardEnabled(
            XVirtualKeyboardSettings_instance()))
        return; /* 总开关关。 */
    if (commitText && commitText[0] && self->m_target)
        xkb_writeText(self, commitText);
    xkb_imeSyncPageSize(self);
    XWidget_update((XWidget*)self);
}

/** @brief 【落地契约槽】context.keyEventRequested(key,text,mods) →
 *         合成键/直写（sendKeyClick 通道）。 */
static void xkb_keyEventSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)receiver;
    if (!self || !args) return;
    XVarList_args_3(args, int, reqKey, const char*, reqText, uint32_t,
                    reqMods);
    if (!XVirtualKeyboardSettings_keyboardEnabled(
            XVirtualKeyboardSettings_instance()))
        return;
    if (!self->m_target) return;
    if (reqText && reqText[0] && !reqText[1] && reqText[0] >= 0x20 &&
        reqText[0] <= 0x7E)
        xkb_writeText(self, reqText); /* 单字节可打印走直写（大写带 Shift）。 */
    else
        xkb_sendKey(self, reqKey, (XKeyboardModifiers)reqMods);
}

/** @brief 连接/断开 context 落地契约信号（setTextArea/popup 连接，
 *         closePopup 断开；幂等）。长按重复不经 engine 信号回环：多面
 *         板共享进程单例 engine，任一面板的延迟析构都会摘清单例上的
 *         全部点击连接（探针实证 recv 恒 0），重复驱动回落面板本地定
 *         时器（节拍宏仍取 engine 头的 600/50，单一事实源不丢）。 */
static void xkb_bindContextConns(XVirtualKeyboard* self, bool bind)
{
    XVirtualKeyboardInputContext* ctx;
    if (!self) return;
    if (!bind) {
        if (self->m_commitConn) {
            XObject_disconnect_2(self->m_commitConn);
            self->m_commitConn = NULL;
        }
        if (self->m_keyEventConn) {
            XObject_disconnect_2(self->m_keyEventConn);
            self->m_keyEventConn = NULL;
        }
        return;
    }
    if (self->m_commitConn && self->m_keyEventConn) return; /* 幂等。 */
    ctx = xkb_contextOf(self);
    if (!ctx) return;
    /* 连接判据只看连接句柄：XObject_connect_1 自建的是发送方（ctx）
       的信号槽（XObject.c:420-424），接收方（本面板）作为 receiver 不
       需要——若再以 m_signalSlot 判接收方，新建面板（从未作为发送方
       连接过信号）恒为 NULL，连接被静默跳过（commit/keyEvent 落地链
       全断，探针实证 commitConn 恒 0）。 */
    if (!self->m_commitConn)
        self->m_commitConn = XObject_connect_1(
            (XObject*)ctx,
            (size_t)XVirtualKeyboardInputContext_commitRequested_signal(
                NULL, NULL),
            (XObject*)self, xkb_commitSlot, XConnectionType_Direct);
    if (!self->m_keyEventConn)
        self->m_keyEventConn = XObject_connect_1(
            (XObject*)ctx,
            (size_t)XVirtualKeyboardInputContext_keyEventRequested_signal(
                NULL, 0, NULL, 0),
            (XObject*)self, xkb_keyEventSlot, XConnectionType_Direct);
}

/** @brief hints→布局映射（Keyboard.qml:42-49 优先级链；手写 N-A）。
 *  @details dialpad(Dialable) > numbers(FormattedNumbers) > digits
 *           (DigitsOnly) > numbers(PreferNumbers 软提示) > main；latin
 *           系只锁 Latin 输入模式（Keyboard.qml:1516-1519 口径），数
 *           字系切 Numeric/Dialable（仅当输入模式在当前插件申报集内
 *           才切，避免 Qt setInputMode 拒绝噪声）；无互斥 hints
 *           （ImhNone）对称回落 main 布局，但 User1（IME 拼音槽位，
 *           槽位占用约定）完全保持——重绑普通编辑框不打断拼音会话
 *           （XKeyboardTest ④/⑤ 门禁口径）；回落时 IME 状态按页面
 *           build 口径恢复英文态：Latin 经下方申报集守卫设置（拼音
 *           插件申报 {Pinyin,Latin}，Plain 恒 {Latin}，未申报静默跳
 *           过），m_imeEnabled 不随重绑翻转——拼音能力保留（回 User1
 *           后中/EN 仍可进中文），上一目标残留的 Numeric/Pinyin 引擎
 *           态不外溢（main→digits→main 数字盘残留实测缺陷的对向修
 *           复）。 */
static void xkb_applyFocusHints(XVirtualKeyboard* self)
{
    XVirtualKeyboardInputContext* ctx;
    XVirtualKeyboardInputEngine* engine;
    uint32_t hints;
    XKeyboardMode mode = self->m_mode;
    int inputMode = -1;
    int modes[8];
    int modeCount;
    int i;
    if (!self || !self->m_target) return;
    ctx = xkb_contextOf(self);
    if (!ctx) return;
    hints = XVirtualKeyboardInputContext_inputMethodHints(ctx);
    if (hints & XInputMethodHint_DialableCharactersOnly) {
        mode = XKeyboardMode_Dialpad;
        inputMode = (int)XVirtualKeyboardInputEngineInputMode_Dialable;
    } else if (hints & XInputMethodHint_FormattedNumbersOnly) {
        mode = XKeyboardMode_Number;
        inputMode = (int)XVirtualKeyboardInputEngineInputMode_Numeric;
    } else if (hints & XInputMethodHint_DigitsOnly) {
        mode = XKeyboardMode_Digits;
        inputMode = (int)XVirtualKeyboardInputEngineInputMode_Numeric;
    } else if (hints & XInputMethodHint_PreferNumbers) {
        mode = XKeyboardMode_Number;
        inputMode = (int)XVirtualKeyboardInputEngineInputMode_Numeric;
    } else if (hints & (XInputMethodHint_LatinOnly |
                        XInputMethodHint_EmailCharactersOnly |
                        XInputMethodHint_UrlCharactersOnly)) {
        mode = XKeyboardMode_TextLower;
        inputMode = (int)XVirtualKeyboardInputEngineInputMode_Latin;
    } else {
        /* 无互斥 hints（ImhNone）：
           - 当前已在 User1（IME 拼音槽位＝IME 面板的主布局，槽位占用
             约定）→ 完全保持：不重建布局、不动引擎输入模式——重绑普通
             编辑框不得打断拼音会话（XKeyboardTest ④/⑤ 门禁口径：
             setImeEnabled(true)→setTextArea(无 hints 框) 后 n/i 仍进组
             串，中文/英文态随引擎不翻转；demo build 的 User1+英文态初
             始演示态同理不被重绑打落）。
           - 其余布局（数字盘等专用布局残留）→ 对称回落 main（TextLower），
             引擎输入模式按页面 build 口径恢复英文态（Latin 经下方申报
             集守卫设置，未申报静默跳过），m_imeEnabled 不动——实测缺陷
             main→digits→main 数字盘残留即本方向缺失。 */
        if (self->m_mode == XKeyboardMode_User1) {
            mode = self->m_mode;
            inputMode = -1;
        } else {
            mode = XKeyboardMode_TextLower;
            inputMode = (int)XVirtualKeyboardInputEngineInputMode_Latin;
        }
    }
    if (mode != self->m_mode) XVirtualKeyboard_setMode(self, mode);
    engine = xkb_engineOf(self);
    if (inputMode >= 0 && engine) {
        modeCount = XVirtualKeyboardInputEngine_inputModes(engine, modes, 8);
        for (i = 0; i < modeCount; ++i) {
            if (modes[i] == inputMode) {
                XVirtualKeyboardInputEngine_setInputMode(
                    engine, (XVirtualKeyboardInputEngineInputMode)inputMode);
                break;
            }
        }
    }
    xkb_notifyObserver(self);
}

/** @brief shift/caps 读数→模式读数同步（不重建键帽布局）。
 *  @details isUppercase（shift||capsLock）→TextUpper，否则 TextLower；
 *           仅切 m_mode 读数（观察者/重绘照发）——重建会换装大写键帽
 *           表，字符键下标随测试侧 findButton("q") 契约失配（视觉映射
 *           不变口径）；大写出字经 xkb_writeText 的上下文大写钩子。 */
static void xkb_syncShiftMode(XVirtualKeyboard* self)
{
    XVirtualKeyboardInputContext* ctx;
    XKeyboardMode mode;
    if (!self) return;
    ctx = xkb_contextOf(self);
    if (!ctx) return;
    mode = XVirtualKeyboardInputContext_isUppercase(ctx)
               ? XKeyboardMode_TextUpper
               : XKeyboardMode_TextLower;
    if (self->m_mode == mode) return;
    self->m_mode = mode;
    xkb_notifyObserver(self);
    XWidget_update((XWidget*)self);
}


#endif /* XVIRTUALKEYBOARD_ON */

void XVirtualKeyboard_setTextArea(XVirtualKeyboard* self, XWidget* target)
{
    if (!self) return;
    if (target == self->m_target) return;
    if (target && !xkb_supportedTarget(target)) {
        XPrintf("[XVirtualKeyboard] setTextArea: 不支持的目标类型（三编辑控件/"
                "WA_InputMethodEnabled 控件之外），保持原绑定\n");
        return;
    }
    xkb_bindTargetDestroyed(self, target);
#if XVIRTUALKEYBOARD_ON
    /* 实际换绑（含守护 tick 焦点跟随重绑）即弃组串草稿（engine
       reset）——覆盖「换框残留拼音」风险；同目标重绑不误清。同步
       连接落地契约信号（有目标才连接；无面板连接时提交/虚键丢弃）。 */
    if (xkb_engineOf(self))
        XVirtualKeyboardInputEngine_reset(xkb_engineOf(self));
    xkb_bindContextConns(self, target != NULL);
#endif
    self->m_target = target;
#if XVIRTUALKEYBOARD_ON
    /* 绑定目标即上下文输入项（Qt inputContext.focusObject 口径）：后
       续 hints/包围文本查询以绑定目标为事实源——直呼 popup 的目标未
       持应用焦点时，焦点对象链拿不到它的 hints（closeOnReturn 的
       MultiLine 豁免失灵）。 */
    {
        XVirtualKeyboardInputContext* ctx = xkb_contextOf(self);
        if (ctx)
            XVirtualKeyboardInputContext_setFocusObject(
                ctx, (XObject*)target);
    }
    xkb_applyFocusHints(self); /* 绑定后按生效 hints 映射布局（含直呼
                                  setTextArea 路径；popup 内会再同步）。 */
#endif
}

XWidget* XVirtualKeyboard_textArea(const XVirtualKeyboard* self)
{
    return self ? self->m_target : NULL;
}

void XVirtualKeyboard_setMode(XVirtualKeyboard* self, XKeyboardMode mode)
{
    if (!self || mode == self->m_mode) return;
    if (mode < XKeyboardMode_TextLower ||
        mode > XKeyboardMode_Dialpad)
        return;
    self->m_mode = mode;
#if XVIRTUALKEYBOARD_ON
    /* 模式实际切换即弃组串草稿（engine reset；离开/重入 User1 均复
       位、不翻转中文态）并通知 Observer。 */
    if (xkb_engineOf(self))
        XVirtualKeyboardInputEngine_reset(xkb_engineOf(self));
    self->m_candidatePage = 0; /* 切布局重置面板本地页。 */
    xkb_notifyObserver(self);
#endif
    xkb_rebuildLayout(self);
    XWidget_update((XWidget*)self);
}

XKeyboardMode XVirtualKeyboard_mode(const XVirtualKeyboard* self)
{
    return self ? self->m_mode : XKeyboardMode_TextLower;
}

void XVirtualKeyboard_setPopovers(XVirtualKeyboard* self, bool enabled)
{
    if (!self || enabled == self->m_popovers) return;
    self->m_popovers = enabled;
    /* 重建生效 ctrl 表（popovers=0 剥 POPOVER 位；源=槽位表，对标
       KB.c:260-270,468-487）。 */
    xkb_rebuildLayout(self);
    XWidget_update((XWidget*)self);
}

bool XVirtualKeyboard_popovers(const XVirtualKeyboard* self)
{
    return self ? self->m_popovers : false;
}

void XVirtualKeyboard_setMap(XVirtualKeyboard* self, XKeyboardMode mode,
                      const char* const* map,
                      const XKeyboardButtonCtrl* ctrlMap)
{
    if (!self || !map) return;
    if (mode < XKeyboardMode_TextLower || mode > XKeyboardMode_User4) return;
    self->m_maps[mode] = map;
    self->m_ctrls[mode] = ctrlMap;
    if (mode == self->m_mode) {
        xkb_rebuildLayout(self);
        XWidget_update((XWidget*)self);
    }
}

const char* const* XVirtualKeyboard_mapArray(const XVirtualKeyboard* self)
{
    if (!self) return NULL;
    return self->m_maps[self->m_mode];
}

uint32_t XVirtualKeyboard_buttonCount(const XVirtualKeyboard* self)
{
    return self ? self->m_keyCount : 0;
}

uint32_t XVirtualKeyboard_selectedButton(const XVirtualKeyboard* self)
{
    return self ? self->m_selectedKey : XKEYBOARD_BUTTON_NONE;
}

const char* XVirtualKeyboard_buttonText(const XVirtualKeyboard* self, uint32_t buttonId)
{
    if (!self || buttonId >= self->m_keyCount) return NULL;
    /* 读重建落表的生效键帽（借用指针，与 map 同生命周期）：不读
       m_maps[m_mode]——shift/caps 态逻辑切模式读数不重建布局（视觉
       映射不变口径），经 mode 槽位直读会拿到新槽表键帽，键下标与
       findButton/handleButton 的查找口径错位。 */
    return self->m_keyLabels[buttonId];
}

XKeyboardButtonCtrl XVirtualKeyboard_buttonCtrl(const XVirtualKeyboard* self,
                                         uint32_t buttonId)
{
    if (!self || buttonId >= self->m_keyCount) return (XKeyboardButtonCtrl)0;
    return self->m_keyCtrls[buttonId];
}

void XVirtualKeyboard_setButtonCtrl(XVirtualKeyboard* self, uint32_t buttonId,
                             XKeyboardButtonCtrl ctrl)
{
    int width;
    if (!self || buttonId >= self->m_keyCount) return;
    /* 生效表覆写：宽度位与标志位一并生效（宽度 0 按单位 1 兜底）。 */
    width = (int)ctrl & (int)XKEYBOARD_CTRL_WIDTH_MASK;
    if (width <= 0) width = 1;
    self->m_keyCtrls[buttonId] = (XKeyboardButtonCtrl)(
        ((int)ctrl & ~(int)XKEYBOARD_CTRL_WIDTH_MASK) | width);
    xkb_rebuildLayout(self);
    XWidget_update((XWidget*)self);
}

/* ==================== 按键语义复用 ==================== */

bool XVirtualKeyboard_handleButton(XVirtualKeyboard* self, uint32_t buttonId)
{
    return xkb_activateButton(self, buttonId);
}

/* ==================== 弹层（XGui 扩展） ==================== */

void XVirtualKeyboard_popup(XVirtualKeyboard* self, XWidget* editor)
{
    XWidget* host;
    if (!self || !editor) return;
    self->m_userCollapsed = false; /* 显式/自动弹出即清用户收起闩锁。 */
    XVirtualKeyboard_setTextArea(self, editor); /* 含类型校验与防悬垂连接。 */
    if (self->m_target != editor) return; /* setTextArea 拒绝（类型不支持）。 */
#if XVIRTUALKEYBOARD_ON
    /* 首帧布局定版前置（缺陷⑤根修）：悬浮分支下方 flushBackingStore 是
       唯一同步首帧——契约连接/hints→模式映射若滞留在 show+flush 之后，
       首帧按残留模式（init TextLower/上一会话映射）上屏，随后的显式
       setMode 只能救第二帧（QWERTY 闪帧）。前置后 flush 即按定版
       m_mode 出帧；同目标重弹（setTextArea 同值早退不重跑）由本块补跑
       幂等对（bindContextConns 幂等、applyFocusHints 同值 setMode 早
       退），换绑路径 setTextArea 内已在同位序跑同一对函数，不引入新时
       序面。m_prevFocus 采样前移等价：show/setParent/setWindowFlags 均
       不改应用焦点（键盘 NoFocus 不夺焦）；键位矩形随后由 reposition
       的 setGeometry→resizeEvent→rebuildLayout 按最终几何重算，前置无
       几何失配。 */
    xkb_bindContextConns(self, true);
    xkb_applyFocusHints(self);
    self->m_prevFocus = XWidget_appFocusWidget();
#endif
    host = XWidget_topLevelWidget(editor);
    if (!host) return;
    /* 悬浮模式（setHostWindow 锚生效）：跳过挂父，保持独立顶层窗口形
       态；锚为 Popup 型弹层容器时自动解析主窗口顶层为实际锚（几何因
       此锚定主窗口底部全宽，与普通编辑框弹出一致），raise 压过日历弹
       层——时序上键盘 popup 晚于弹层最近一次 show（弹层 show 即 raise），
       再显式 raise 一次兜底。 */
    self->m_floating = (self->m_hostOverride != NULL);
    if (self->m_floating) {
        XWidget* anchor = xkb_resolveFloatingHost(self, host);
        if (!anchor) anchor = host;
        xkb_bindHostDestroyed(self, anchor); /* 覆盖宿主 destroyed 防悬垂。 */
        self->m_host = anchor;
        if (XWidget_parentWidget((XWidget*)self))
            XWidget_setParent((XWidget*)self, NULL, 0); /* 归位顶层形态。 */
        /* 窗口类型=Popup（XComboBox 弹层同款）：Popup/ToolTip 等瞬态类
           型永不装饰（XWindowDecoration_activeFor 类型分支）——悬浮键
           盘外观因此与内嵌形态完全一致（无 CSD 标题栏条带）。 */
        XWidget_setWindowFlags((XWidget*)self,
                               (XWidgetFlags)XWindowType_Popup);
    } else {
        xkb_bindHostDestroyed(self, host);
        self->m_host = host;
        /* 键盘挂宿主顶层窗口底部（XCompleter 弹层挂顶层窗口先例；子控件
           浮层形态，无独立 OS 窗口、无应用模态登记）。 */
        XWidget_setParent((XWidget*)self, host, 0);
    }
    xkb_reposition(self);
    XWidget_show((XWidget*)self);
    XWidget_raise((XWidget*)self);
    if (self->m_floating)
        XWidget_flushBackingStore((XWidget*)self, NULL);
    self->m_popped = true;
    if (self->m_floating) {
        /* 抢占弹层模态双抓取：日历弹层 show 即 grabMouse/grabKeyboard
           （跨顶层抓取改道 XWidget.c 派发入口）+ 1ms 后原生 SetCapture
           （win32 把全部鼠标消息收入弹层 HWND）——悬浮面板不夺取则键
           面点击被改道/吞掉。接管后键盘自身点击经本地抓取分支直达，
           物理按键经守护抓取 topLevel 失配回退焦点链照常到编辑框。
           closePopup/hideEvent 成对释放。 */
        /* 抢占前快照框架双抓取者（界外收层时归还，Qt closePopup 的
           previous-grabber 语义；抓取者仍是自己=未收层的重弹，保留旧
           快照不覆盖——closePopup 只按最早会话归还一次）。 */
        if (XWidget_mouseGrabber() != (XWidget*)self)
            self->m_prevMouseGrab = XWidget_mouseGrabber();
        if (XWidget_keyboardGrabber() != (XWidget*)self)
            self->m_prevKbdGrab = XWidget_keyboardGrabber();
        XWidget_grabMouse((XWidget*)self);
        XWidget_grabKeyboard((XWidget*)self);
#if XWINDOW_ON
        {
            XWindow* handle = XWidget_windowHandle((XWidget*)self);
            if (handle) XWindow_setMouseGrabEnabled(handle, true);
        }
#endif
    }
    /* 键盘 NoFocus（XWidget 基类默认）不抢焦点：焦点保持/交还 editor，
       物理键盘直入编辑框不受影响。 */
    xkb_startGuard(self);
}

void XVirtualKeyboard_closePopup(XVirtualKeyboard* self)
{
    if (!self) return;
    /* 用户收起闩锁置位：守护轮询不再原地自动重弹（焦点未迁移时），
     * 直到用户再次按下编辑框（notifyPress 清锁即弹）。 */
    self->m_userCollapsed = true;
    /* 悬浮面板收层先释放双抓取与原生鼠标捕获再隐藏（popup 悬浮分支
       夺取的弹层模态抓取成对归还；identity 判据不误放他者抓取），随后
       隐藏、保持顶层归属（无需归还主窗口，下次 popup 按锚重挂）并清
       空悬浮锚（回内嵌模式）。 */
    if (self->m_floating) {
        XWidget_releaseMouse((XWidget*)self);
        XWidget_releaseKeyboard((XWidget*)self);
#if XWINDOW_ON
        {
            XWindow* handle = XWidget_windowHandle((XWidget*)self);
            if (handle) XWindow_setMouseGrabEnabled(handle, false);
        }
#endif
        /* 归还抢占前抓取者（Qt closePopup 的 previous-grabber 语义；
           XMenu 嵌套弹层交接同款）：框架双抓取+原生鼠标捕获成对还原
           ——悬浮分支从未夺取原生键盘抓取（popup 侧仅 setMouseGrab
           Enabled），只还原鼠标侧。快照=自己（异常态）或已隐藏不还
           原（grabMouse 对不可见控件本就 no-op，isVisible 守卫为
           windowHandle 取用兜底），防把抓取交给不可见控件。 */
        if (self->m_prevMouseGrab &&
            self->m_prevMouseGrab != (XWidget*)self &&
            XWidget_isVisible(self->m_prevMouseGrab)) {
            XWidget_grabMouse(self->m_prevMouseGrab);
#if XWINDOW_ON
            {
                XWindow* prevHandle =
                    XWidget_windowHandle(self->m_prevMouseGrab);
                if (prevHandle) XWindow_setMouseGrabEnabled(prevHandle, true);
            }
#endif
        }
        if (self->m_prevKbdGrab &&
            self->m_prevKbdGrab != (XWidget*)self &&
            XWidget_isVisible(self->m_prevKbdGrab))
            XWidget_grabKeyboard(self->m_prevKbdGrab);
        self->m_prevMouseGrab = NULL;
        self->m_prevKbdGrab = NULL;
    }
    /* 无条件隐藏（不以 isVisible 为前置）：pre-show 收层（无头钩子在
       宿主 show 前调本接口）effectiveVisible 恒假，带「isVisible 才
       隐藏」护栏会跳过隐藏——弹层 show 位残留，宿主 show 后传播把整
       块面板画出（demo 无头钩子记录的 closed 场景残影根因）。已可见
       面板行为不变；未生效可见面板仅补置 WState_Hidden（m_visible
       不变、无 SHOW/HIDE 事件）。 */
    XWidget_setVisible((XWidget*)self, false);
    self->m_popped = false;
    self->m_floating = false;
    self->m_hostOverride = NULL; /* 收层自动清悬浮锚（回内嵌挂父模式）。 */
    self->m_hostHint = NULL;     /* 锚解析提示同周期失效。 */
#if XVIRTUALKEYBOARD_ON
    /* 收层弃组串草稿（含关闭键路径）：engine reset→插件 reset
       Composition（弃草稿链，不翻转中文态）+ 清面板本地页；断开落地
       契约连接（无面板连接时 commitRequested/keyEventRequested 无消
       费者=丢弃，文档化口径）；同步 m_prevFocus=当前焦点采样（按下路
       径收层后守护不误记边沿）。 */
    if (xkb_engineOf(self))
        XVirtualKeyboardInputEngine_reset(xkb_engineOf(self));
    self->m_candidatePage = 0;
    xkb_bindContextConns(self, false);
    self->m_prevFocus = XWidget_appFocusWidget();
#endif
    /* 守护按运行条件保留（与 xkb_startGuard 自述一致：弹层可见或
       autoPopup 开）：autoPopup 开时常驻轮询继续（焦点跟随换框兜底），
       仅弹层隐藏；autoPopup 关才随层停。 */
    if (!self->m_autoPopup) xkb_stopGuard(self);
    /* 收层保持解除目标绑定（多框跟随/防悬垂契约不动）。取舍结论（按
       下位置驱动定版）：旧 dismissFix「同框 retap 不重弹」契约作废——
       收起后再按同一编辑框由 notifyPress 按下路径即时重弹（无需焦点
       往返，点空白收起的对称操作）；守护边沿仅为换框兜底。 */
    if (self->m_targetConn) {
        XObject_disconnect_2(self->m_targetConn);
        self->m_targetConn = NULL;
    }
    self->m_target = NULL;
}

bool XVirtualKeyboard_popupVisible(const XVirtualKeyboard* self)
{
    if (!self) return false;
    return XWidget_isVisible((XWidget*)self);
}

void XVirtualKeyboard_setHostWindow(XVirtualKeyboard* self, XWidget* host)
{
    XWidget* focus;
    if (!self) return;
    /* 仅存锚（借用）；悬浮形态在随后 popup() 生效（m_floating 置位）。
       已弹出期间改锚不重定位（先 closePopup 再设锚重弹；收层自动清锚
       契约使残留锚不越过一次弹层生命周期）。 */
    self->m_hostOverride = host;
    self->m_hostHint = NULL;
    if (host) {
        /* 锚为 Popup 型弹层容器时主窗口反向引用不可达（widget 父链/
           transient parent 均空，应用顶层表在 XGuiApplication 单根应用
           下不可用），此刻补拍焦点顶层快照作解析提示（m_hostHint）：
           契约时序=布锚先于落焦（XCalendarWidget beginYearEdit 先布锚
           后 setFocus），快照即弹层打开前用户所在主窗口顶层（transient
           parent 语义）；焦点为空/已在锚内/Popup 型时无快照，解析回落
           m_host/应用顶层表/锚自身。 */
        focus = XWidget_appFocusWidget();
        if (focus && !xkb_insidePanel(self, focus)) {
            XWidget* top = XWidget_isWindow(focus)
                               ? (XWidget*)focus
                               : XWidget_topLevelWidget(focus);
            if (top != host && XWidget_isWindow(top) &&
                !xkb_isPopupTypeWindow(top))
                self->m_hostHint = top;
        }
    }
    if (!host) self->m_floating = false;
}

XWidget* XVirtualKeyboard_hostWindow(const XVirtualKeyboard* self)
{
    return self ? self->m_hostOverride : NULL;
}

void XVirtualKeyboard_setAutoPopup(XVirtualKeyboard* self, bool on)
{
    if (!self) return;
    if (on == self->m_autoPopup) {
        /* 值未变不空转：守护缺席时补启（自愈 init 后被外部停掉、无调
           度器环境下首次启动失败后恢复等漂移，保证 on 恒常驻）。 */
        if (on && self->m_guardTimer == XTIMER_INVALID_ID)
            xkb_startGuard(self);
        return;
    }
    self->m_autoPopup = on;
    if (on)
        xkb_startGuard(self); /* 常驻轮询（运行条件满足即启动）。 */
    else if (!self->m_popped)
        xkb_stopGuard(self);
}

bool XVirtualKeyboard_autoPopup(const XVirtualKeyboard* self)
{
    return self ? self->m_autoPopup : false;
}

void XVirtualKeyboard_notifyPress(XVirtualKeyboard* self, XWidget* hit)
{
#if XVIRTUALKEYBOARD_DESKTOP_ON
    bool accept;
    if (!self || !self->m_autoPopup) return; /* autoPopup 关=应用全权接管。 */
    if (xkb_insidePanel(self, hit)) return;  /* 键盘自身按键不受影响。 */
    /* 穿透归属上溯（与按下派发循环 XWidget.c:1529-1547 同口径：
       XWidget_attrTest(TransparentForMouseEvents) 跳过、沿父链上抛）。
       childAt 不跳
       穿透控件，点日期时间字段编辑区时 hit=内嵌行编辑（F3-② 置穿透，
       XDateTimeEdit.c:3014；其 hints=ImhNone→误弹 QWERTY、合成数字键
       直插显示文本绕过分段消化）。收敛到实际接收按下事件的祖先=字段壳
       （WA14+DigitsOnly），键盘目标与事件接收者对齐；hit 为形参本地量，
       调用方汇聚点（XWidget.c:1520）不受影响。 */
    while (hit &&
           XWidget_testAttribute(hit,
                                 XWidgetAttribute_TransparentForMouseEvents))
        hit = XWidget_parentWidget(hit);
    /* accept 判据与守护 tick ②全等（supportedTarget=vtable 快路径 ∪
       WA14 放行回退 &&enabled&&总开关）：同框 opt-out 编辑框按下不弹
       （Qt inputMethodAccepted 口径），已弹则随非编辑分支幂等收层；
       WA14 放行使 XDateEdit/XDateTimeEdit 字段壳按下即弹面板不闪退。 */
    accept = hit && xkb_supportedTarget(hit) &&
             XWidget_isEnabled(hit) &&
             XWidget_testAttribute(hit, XWidgetAttribute_InputMethodEnabled);
#if XVIRTUALKEYBOARD_ON
    accept = accept &&
             XVirtualKeyboardSettings_keyboardEnabled(
                 XVirtualKeyboardSettings_instance());
#endif
    if (accept) {
        /* 弹出/重绑；已弹且同框→保持（不重放 hints，面板内手动模式
           切换不被复位置回）。 */
        if (!self->m_popped || self->m_target != hit)
            XVirtualKeyboard_popup(self, hit);
    } else if (self->m_popped) {
        /* 非编辑区域（页面空白/按钮/标签/不可接受编辑框）：dismiss，
           组串弃置等 closePopup 副作用随行。 */
        XVirtualKeyboard_closePopup(self);
    }
#else /* !XVIRTUALKEYBOARD_DESKTOP_ON */
    (void)self;
    (void)hit; /* 非桌面形态：守护同停，仅显式 popup/closePopup。 */
#endif /* XVIRTUALKEYBOARD_DESKTOP_ON */
}

/* ==================== 拼音输入（XGui 扩展，XVIRTUALKEYBOARD_ON 门控；
 *                       薄委托引擎/插件） ==================== */

#if XVIRTUALKEYBOARD_ON

bool XVirtualKeyboard_setImeEnabled(XVirtualKeyboard* self, bool on)
{
    XVirtualKeyboardInputEngine* engine;
    XVirtualKeyboardAbstractInputMethod* plugin;
    if (!self) return false;
    if (on == self->m_imeEnabled) return true; /* 值未变无操作。 */
    engine = xkb_engineOf(self);
    if (!engine) return false;
    if (on) {
#if XKEYBOARD_IME_ON
        /* 薄委托（apiMapping#13）：engine 装 Pinyin 插件（XPinyinEngine
           实例=插件独占，面板零直连）+ setInputMode(Pinyin)（中文态
           默认 true），再装表切槽——使 setMap/setMode 内部重建即按候
           选带预留行高。 */
        plugin = (XVirtualKeyboardAbstractInputMethod*)
            XVirtualKeyboardPinyinInputMethod_create();
        if (!plugin) return false;
        {
            XVirtualKeyboardAbstractInputMethod* old =
                XVirtualKeyboardInputEngine_inputMethod(engine);
            XVirtualKeyboardInputEngine_setInputMethod(engine, plugin);
            if (old) {
                /* 旧输入法的 keyClick 等回调可能仍挂在引擎事件链上，
                   延迟回收防发射帧内同步释放。 */
                XObject_deleteLater((XObject*)old);
            }
        }
        self->m_imeEnabled = true;
#if XKEYBOARD_IME_PHRASE_ON
        /* 词组库懒加载（幂等+负结果粘滞；本处值未变早退保证仅首次
           enable 触发一次 IO——守护定时器等纯 UI 巡检路径不挂 IO）。 */
        XPinyinPhrase_load();
#endif
        XVirtualKeyboardInputEngine_setInputMode(
            engine, XVirtualKeyboardInputEngineInputMode_Pinyin);
        XVirtualKeyboard_setMap(self, XKeyboardMode_User1, XPinyinEngine_map(),
                         XPinyinEngine_ctrlMap());
        XVirtualKeyboard_setMode(self, XKeyboardMode_User1);
#else
        return false; /* IME 状态机裁剪：无拼音插件可装。 */
#endif
    } else {
        /* 薄委托：engine 装回 Plain 插件（旧插件实例由装配链卸载——
           本实现引擎不持有所有权，此处显式释放）。模式不动：User1 槽
           位仍持拼音表（槽位占用约定，不恢复原表也不切走——切走会让
           buttonCount 回落 40，丢失「槽位仍为拼音表」契约）；候选带随
           m_imeEnabled=false 在尾部统一重建中归零。 */
        XVirtualKeyboardAbstractInputMethod* old =
            XVirtualKeyboardInputEngine_inputMethod(engine);
        self->m_imeEnabled = false;
        XVirtualKeyboardInputEngine_setInputMethod(
            engine, (XVirtualKeyboardAbstractInputMethod*)
                        XVirtualKeyboardPlainInputMethod_create());
        if (old) XObject_deleteLater((XObject*)old);
    }
    /* 无论 setMode 是否已触发过重建（值未变早退），统一重建一次确保
       带矩形与布局同步（重建幂等且廉价）。 */
    xkb_rebuildLayout(self);
    xkb_notifyObserver(self);
    XWidget_update((XWidget*)self);
    return true;
}

bool XVirtualKeyboard_imeEnabled(const XVirtualKeyboard* self)
{
    return self ? self->m_imeEnabled : false;
}

bool XVirtualKeyboard_setImeChinese(XVirtualKeyboard* self, bool chinese)
{
    XVirtualKeyboardInputEngine* engine;
    if (!self || !self->m_imeEnabled) return false; /* IME 未启用拒绝。 */
    if (chinese == xkb_imeChineseState(self)) return true;
    engine = xkb_engineOf(self);
    if (!engine) return false;
    /* 薄委托：中/EN=输入模式切换（对齐 Qt 中/EN 键语义；插件内部清
       组串候选）。候选带显隐随中文态（行高 5/6 行重排），一次重建代
       价可接受。 */
    if (!XVirtualKeyboardInputEngine_setInputMode(
            engine, chinese ? XVirtualKeyboardInputEngineInputMode_Pinyin
                            : XVirtualKeyboardInputEngineInputMode_Latin))
        return false;
    self->m_candidatePage = 0;
    xkb_rebuildLayout(self);
    xkb_notifyObserver(self);
    XWidget_update((XWidget*)self);
    return true;
}

bool XVirtualKeyboard_imeChinese(const XVirtualKeyboard* self)
{
    return xkb_imeChineseState(self);
}

#endif /* XVIRTUALKEYBOARD_ON */

/* ==================== 信号（纯 ID getter） ==================== */

void* XVirtualKeyboard_ready_signal(XVirtualKeyboard* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboard_ready_signal;
}

void* XVirtualKeyboard_cancel_signal(XVirtualKeyboard* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboard_cancel_signal;
}

void* XVirtualKeyboard_buttonActivated_signal(XVirtualKeyboard* self, int32_t buttonId)
{
    (void)self;
    (void)buttonId;
    return (void*)(size_t)XVirtualKeyboard_buttonActivated_signal;
}

#endif /* XWIDGET_ON && XKEYBOARD_ON */
