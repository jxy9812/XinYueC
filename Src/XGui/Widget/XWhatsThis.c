/**
 * @file       XWhatsThis.c
 * @brief      「What's This?」帮助模式实现（对标 Qt 6.8 QWhatsThis 全套）。
 * @details    结构与 Qt 逐项对应：
 *             - 模块级单例状态（Qt 的 `QWhatsThisPrivate::instance` /
 *               `QWhatsThat::instance`），本文件用两个静态变量承载；
 *             - 进程级事件过滤器 = 静态 XObject 派生类，覆写
 *               `EXObject_EventFilter` 槽（Qt 的
 *               `qApp->installEventFilter(new QWhatsThisPrivate)`，
 *               qwhatsthis.cpp:379-386）；
 *             - 浮层 = 顶层 XWidget（XLabel 可用时用 XLabel 承载文本），
 *               窗口类型 `XWindowType_ToolTip`（Qt 的 Qt::Popup）。
 *             所有依据的 Qt 原文行号见各函数注（qwhatsthis.cpp /
 *             qwidget.cpp / qtabbar.cpp）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XWhatsThis.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include "XWhatsThisEvent.h"      /* §B13：XHelpEvent / XWhatsThisClickedEvent。 */
#include "XObject.h"
#include "XCoreApplication.h"
#include "XEvent.h"               /* XKeyEvent/XMouseEvent + XEVENT_TYPE_*。 */
#include "XApplication.h"         /* XApplication_topLevelWidgets / widgetAt。 */
#include "XGuiApplication.h"      /* 覆盖光标 + 焦点对象。 */
#include "XCursor.h"
#include "XAlignment.h"           /* XLabel_setAlignment 的 XAlignments 位。 */
#include "XScreen.h"
#if XTABBAR_ON
#include "XTabBar.h"              /* [§B13] 页签命中：tabAt + tabWhatsThis。 */
#endif
#if XACTION_ON
#include "XAction.h"
#endif

#if XWIDGET_ON
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
#include "XLabel.h"
#endif

/* ==================== 模块级状态 ==================== */

/** @brief 是否处于帮助模式（对标 QWhatsThisPrivate::instance != nullptr）。 */
static bool s_whatsThisMode = false;
/** @brief 进程级事件过滤器对象（拥有；进入模式时创建、退出时释放）。 */
static XObject* s_whatsThisFilter = NULL;
/** @brief 帮助浮层（拥有；惰性创建、hideText/替换时销毁）。 */
static XWidget* s_whatsThisPopup = NULL;
/** @brief 浮层当前文本（拥有）。 */
static XString* s_whatsThisText = NULL;
/** @brief 浮层关联控件（借用）。 */
static XWidget* s_whatsThisTarget = NULL;
#if XACTION_ON
/** @brief 本进程创建的帮助动作（借用；退出模式时清勾选，对标
 *         `~QWhatsThisPrivate` 的 `action->setChecked(false)`）。 */
static XAction* s_whatsThisAction = NULL;
#endif

/** @brief 上下文菜单键（Qt `Qt::Key_Menu = 0x01000055`，
 *         qnamespace.h:728）。
 *  @details 框架的 XKey 枚举当前**没有** Menu 键（全仓 0 命中），而
 *           Qt 在帮助模式里对它必须放行（qwhatsthis.cpp:430-436
 *           「we don't react to these keys, they are used for context
 *           menus」）。为不扩枚举表（那是另一批次的 P1 命名对齐工作），
 *           此处按 Qt 的值就地常量比对；键码本身来自平台后端，值一致
 *           即语义一致。 */
#define XWT_KEY_MENU  (0x01000055)

/* ==================== 事件过滤器（对标 QWhatsThisPrivate::eventFilter） ==== */

XCLASS_DEFINE_BEGING(XWTFilter)
XCLASS_DEFINE_EXTEND_END(XWTFilter, XObject)

typedef struct XWTFilter
{
    XObject m_base;  /**< 基类；必须是第一个成员。 */
} XWTFilter;

static bool xwt_filterEvent(XObject* self, XObject* watched, XEvent* event);
static void xwt_installFilter(void);

/** @brief 浮层是否需要「本次释放后退出模式」标记（Qt 的
 *         `leaveOnMouseRelease`，qwhatsthis.cpp:359/384/414）。 */
static bool s_leaveOnRelease = false;

static XVtable* XWTFilter_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XWTFilter)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter, xwt_filterEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 安装进程级事件过滤器（标对 qApp->installEventFilter(this)）。
 *  @details Qt 把过滤器装在 **QApplication** 上（应用级过滤器，见
 *           XCoreApplication.c 的 VXCoreApplication_notify 里
 *           `g_app->m_filters` 分支）。本实现同样装在应用对象上，
 *           拿到「任何控件的事件先经过它」的语义。
 *           与 Qt 的**一处偏离**（含理由）：Qt 的过滤器只在模式内存活
 *           （进入时装、退出时 delete）；本实现装上后**常驻**——因为
 *           Shift+F1 进入模式的入口（XWidget 键派发的 §B13 接线）在
 *           模式外也要能看到按键，而「退出模式时摘过滤器」恰逢
 *           VXCoreApplication_notify 正在遍历 `g_app->m_filters`
 *           （showText→leave 的调用链就在过滤器回调内），遍历中改表
 *           有跳项风险。代价是每次事件一次额外回调，快速路径
 *           （非 Shift+F1/非模式/无浮层）直接返回 false，零状态写入。 */
static void xwt_installFilter(void)
{
    XCoreApplication* app;
    if (s_whatsThisFilter) return;
    s_whatsThisFilter =
        (XObject*)XMemory_malloc(sizeof(XWTFilter), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!s_whatsThisFilter) return;
    XObject_init(s_whatsThisFilter);
    XClassSetVtable(s_whatsThisFilter, XWTFilter);
    Set_Class_Memory(s_whatsThisFilter, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(s_whatsThisFilter, true);
    app = XCoreApplication_instance();
    if (!app) {
        /* 无应用对象：过滤器无处安装 ⇒ 释放并放弃（不残留悬挂单例）。 */
        XClassDelete((XClass*)s_whatsThisFilter);
        s_whatsThisFilter = NULL;
        return;
    }
    XObject_installEventFilter((XObject*)app, s_whatsThisFilter);
}

/** @brief 向每个顶层控件投递事件（对标
 *         `QWhatsThisPrivate::notifyToplevels`，qwhatthiss.cpp:316-322）。
 *  @note  顶层注册表 API 在 `XAPPLICATION_ON && XGUIAPPLICATION_ON` 门内
 *         （XApplication.h）；组合裁掉时（纯 XCoreApplication 宿主）
 *         退化为不广播——模式状态与过滤器照常工作。 */
static void xwt_notifyToplevels(XEventType type)
{
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    XVector* tops = XApplication_topLevelWidgets();
    size_t i;
    size_t n;
    if (!tops) return;
    n = XVector_size_base((const XContainer*)tops);
    for (i = 0; i < n; ++i) {
        XWidget* w = XVector_At_Base(tops, (int64_t)i, XWidget*);
        XEvent e;
        if (!w) continue;
        XEvent_init(&e, type);
        (void)XCoreApplication_sendEvent((XObject*)w, &e);
    }
    XClassDelete((XClass*)tops);
#else
    (void)type;
#endif
}

/** @brief 命中全局坐标下的控件（对标 `QApplication::widgetAt(pos)`）。
 *  @note  注册表门控同上；组合裁掉时返回 NULL，命中回落到事件接收者
 *         自身（xwt_filterEvent 的 `if (!hit) hit = w;`）。 */
static XWidget* xwt_widgetAt(const XPoint* global)
{
#if XAPPLICATION_ON && XGUIAPPLICATION_ON
    return XApplication_widgetAt(global);
#else
    (void)global;
    return NULL;
#endif
}

/** @brief 解析全局坐标下的帮助目标与文本（对齐 Qt 的两级链：
 *         ① QTabBar::event 按命中页签取 `tabWhatsThis`
 *         （qtabbar.cpp:1715-1730，与逐页 ToolTip 是两套独立存储）；
 *         ② 控件级沿父链上溯取 `whatsThis`（QWidget::event 缺省分支 +
 *         QApplication::notify 上抛，qwidget.cpp:9332-9341 /
 *         qapplication.cpp:2962-2986）。
 *  @details 本解析在过滤器内完成，**同时覆盖**两条链：XTabBar/XWidget
 *           侧的 sharedEdits 分支应用后，事件在控件侧被 accept，本函数
 *           的结果只用于投递对象选择，不会二次显示（复核 P2-2：页签级
 *           文本在未应用 sharedEdits 时也有显示路径）。
 *  @param hit 命中控件（非 NULL）。
 *  @param global 全局坐标。
 *  @param outTarget 输出持有文本的控件（借用）。
 *  @param outText 输出文本（借用，随控件存活）。
 *  @return 解析到文本返回 true。 */
static bool xwt_resolveHelp(XWidget* hit, const XPoint* global,
                            XWidget** outTarget, const XString** outText)
{
    const XString* text;
#if XTABBAR_ON
    /* ① 页签级：命中控件是页签条 ⇒ 按局部坐标命中页签取其 whatsThis
     *    （对标 QTabBar::event：`tab->whatsThis` 非空 ⇒ showText）。 */
    if (XObject_isA_class((XObject*)hit, XTabBar_class_init())) {
        XPoint local = XWidget_mapFromGlobal(hit, global);
        int index = XTabBar_tabAt((XTabBar*)hit, &local);
        if (index >= 0) {
            text = XTabBar_tabWhatsThis((XTabBar*)hit, index);
            if (text && XString_size((const XContainer*)text) > 0) {
                *outTarget = hit;
                *outText = text;
                return true;
            }
        }
    }
#endif
    /* ② 控件级：沿父链上溯（Qt：逐级 ignore → 父控件重试）。 */
    while (hit) {
        text = XWidget_whatsThis(hit);
        if (text && XString_size((const XContainer*)text) > 0) {
            *outTarget = hit;
            *outText = text;
            return true;
        }
        if (XWidget_isWindow(hit)) break;
        hit = XWidget_parentWidget(hit);
    }
    return false;
}

/** @brief 事件过滤器主体（对标 QWhatsThisPrivate::eventFilter，
 *         qwhatsthis.cpp:345-448）。
 *  @details 逐分支对齐：
 *           - **MouseButtonPress**（:352-361）：右键放行；否则向命中控件
 *             投 `XEVENT_TYPE_WHATS_THIS`（QHelpEvent 载荷），若未被接受
 *             则置 `leaveOnMouseRelease`（下次释放退出模式）；
 *             **本分支恒返回 true（吞掉点击）**——模式内的点击不得穿透到
 *             控件（「浮层不触发控件点击」的契约即由这里保证）。
 *           - **MouseMove**（:363-373）：投 `XEVENT_TYPE_QUERY_WHATS_THIS`
 *             供控件决定是否可显示（Qt 据此换禁止/问号光标），并继续
 *             落到释放分支。
 *           - **MouseButtonRelease**（:375-380）：`leaveOnMouseRelease`
 *             为真则退出模式；右键放行。
 *           - **KeyPress**（:419-443）：Esc（`matches(Cancel)`，:423-425）
 *             退出并吞掉；**customWhatsThis（WA_CustomWhatsThis 控件）**
 *             放行（:428-429 `if (customWhatsThis) return false;`）——
 *             本框架未建模该属性位（先例 XDialog.c:1144「等价于恒不置
 *             位」），该分支不可达故不实现；Menu / Shift+F10 放行
 *             （:430-434，上下文菜单优先）；非修饰键先退出模式，修饰键
 *             仅保持模式，**两类都落到函数尾 `return true`（:443）被吞
 *             掉**——Qt 提前 `return false` 的只有
 *             customWhatsThis/Menu/Shift+F10 三条（复核 P2-1 订正）。
 *           - 其它事件放行。 */
static bool xwt_filterEvent(XObject* self, XObject* watched, XEvent* event)
{
    XWidget* w;
    XEventType type;
    (void)self;
    if (!watched || !event) return false;
    if (!XObject_isWidgetType(watched)) return false;
    w = (XWidget*)watched;
    type = XEvent_type(event);
    /* 【快速闸】浮层可见 ⇒ 任意按下/按键先关浮层并吞掉（对标 Qt::Popup
     * 的抓取语义：QWhatsThat::mousePressEvent → close()，qwhatsthis.cpp
     * :180-198；键盘同理 keyPressEvent → close()）。这就是「点击别处
     * 关闭」，且点击不穿透到底下控件（B13 验收 ③ 的事件消费断言）。
     * 【非模式】其余事件一律放行（过滤器常驻后的零副作用直通）。 */
    if (s_whatsThisPopup && XWidget_isVisible(s_whatsThisPopup)) {
        if (type == XEVENT_TYPE_MOUSE_BUTTON_PRESS ||
            type == XEVENT_TYPE_KEY_PRESS) {
            XWhatsThis_hideText();
            return true;
        }
        return false;
    }
    if (!s_whatsThisMode) return false;
    switch (type) {
    case XEVENT_TYPE_MOUSE_BUTTON_PRESS: {
        XMouseEvent* me = (XMouseEvent*)event;
        XPoint global;
        XWidget* hit;
        XWidget* target;
        const XString* text;
        bool accepted;
        if (XMouseEvent_button(me) == XMouseButton_RightButton)
            return false;                 /* 右键：交给上下文菜单 */
        global = XMouseEvent_globalPosition(me);
        hit = xwt_widgetAt(&global);
        if (!hit) hit = w;
        if (!xwt_resolveHelp(hit, &global, &target, &text)) {
            /* 无文本控件：不显示、不改变焦点（B13 验收 ②），
             * 但模式内的点击仍不得穿透到控件。 */
            s_leaveOnRelease = true;
            return true;
        }
        {
            XHelpEvent help;
            XPoint local = XWidget_mapFromGlobal(target, &global);
            XHelpEvent_init(&help, XEVENT_TYPE_WHATS_THIS, local, global);
            accepted = XCoreApplication_sendEvent((XObject*)target,
                                                  (XEvent*)&help);
            /* Qt：`if (!sendEvent(w,&e) || !e.isAccepted())
             *         leaveOnMouseRelease = true;`
             *      （qwhatsthis.cpp:357-359）——**返回值为假**或**未接受**
             *      都算「此处没帮上忙」。XEvent 缺省已接受
             *      （XEvent.c 的 XEvent_init 置 m_accepted=true，与 Qt
             *      `bool m_accept = true` 同口径），故必须显式再查
             *      isAccepted，否则「控件 event 返回 true 但主动 ignore」
             *      这一 Qt 分支永远走不到。
             *
             *      兜底显示：Qt 里这段「有文本 ⇒ showText」是
             *      QWidget::event / QTabBar::event 的缺省分支
             *      （qwidget.cpp:9332-9334 / qtabbar.cpp:1725-1727），
             *      控件收不到就会 ignore。本框架 XWidget/XTabBar 侧若已
             *      接同一分支（sharedEdits），事件在控件侧已显示并
             *      accept，这里 accepted=true 不进兜底；若控件侧没有该
             *      分支/主动 ignore，则由本过滤器按 Qt 缺省分支的语义
             *      代为显示（text 即 xwt_resolveHelp 的解析结果，含页签
             *      级）——保证「点击有文本控件必出浮层」，不因接线先后
             *      而半途失效。 */
            if (!accepted || !XEvent_isAccepted((XEvent*)&help)) {
                accepted = false;
                if (text && XString_size((const XContainer*)text) > 0)
                    XWhatsThis_showText(global.x, global.y, text, target);
            }
        }
        if (!accepted) s_leaveOnRelease = true;
        /* Qt 在派发后**不 break**，落到下一分支统一处理释放/双击的右键
         * 放行；本实现按「按下已被消费」直接返回 true（等价效果：
         * 该次点击不进控件）。 */
        return true;
    }
    case XEVENT_TYPE_MOUSE_MOVE: {
        XMouseEvent* me = (XMouseEvent*)event;
        XPoint global = XMouseEvent_globalPosition(me);
        XWidget* hit = xwt_widgetAt(&global);
        XWidget* target;
        const XString* text;
        if (!hit) hit = w;
        /* 只回答「此处有没有帮助文本」——不显示浮层（Qt 用 Query 结果
         * 切光标，:365-372）。查询事件的接受状态由控件决定（Qt：控件
         * 无文本则 ignore）。 */
        if (xwt_resolveHelp(hit, &global, &target, &text)) {
            XPoint local = XWidget_mapFromGlobal(target, &global);
            XHelpEvent query;
            XHelpEvent_init(&query, XEVENT_TYPE_QUERY_WHATS_THIS,
                            local, global);
            (void)XCoreApplication_sendEvent((XObject*)target, (XEvent*)&query);
        }
        return false;   /* 移动事件继续派发，不消费。 */
    }
    case XEVENT_TYPE_MOUSE_BUTTON_RELEASE: {
        XMouseEvent* me = (XMouseEvent*)event;
        if (s_leaveOnRelease)
            XWhatsThis_leaveWhatsThisMode();
        if (XMouseEvent_button(me) == XMouseButton_RightButton)
            return false;
        return true;    /* 模式内释放亦不穿透。 */
    }
    case XEVENT_TYPE_KEY_PRESS: {
        XKeyEvent* ke = (XKeyEvent*)event;
        int key = XKeyEvent_key(ke);
        XKeyboardModifiers mods = XKeyEvent_modifiers(ke);
        /* Esc 退出并吞掉（Qt：`kev->matches(QKeySequence::Cancel)` →
         * leave + return true，qwhatsthis.cpp:423-425）。 */
        if (key == (int)XKey_Escape) {
            XWhatsThis_leaveWhatsThisMode();
            return true;
        }
        /* 上下文菜单键放行（Qt：qwhatsthis.cpp:430-434 的 Key_Menu /
         * Shift+F10，「we don't react to these keys, they are used for
         * context menus」）。其前的 customWhatsThis 放行分支（:428-429）
         * 依赖 WA_CustomWhatsThis 属性位——本框架未建模该位（XDialog.c
         * :1144 同先例），恒不置位 ⇒ 分支不可达，不实现。 */
        if (key == XWT_KEY_MENU ||
            (key == (int)XKey_F10 &&
             mods == XKeyboardModifier_ShiftModifier))
            return false;
        if (key != (int)XKey_Shift && key != (int)XKey_Alt &&
            key != (int)XKey_Control && key != (int)XKey_Meta) {
            XWhatsThis_leaveWhatsThisMode();
        }
        /* Qt：KeyPress 分支处理完落到函数尾 `return true`（qwhatsthis.cpp
         * :443）⇒ 修饰键与 leave 后的非修饰键一样被吞掉，模式内按键
         * 不泄漏给控件（复核 P2-1；Qt 提前 `return false` 放行的只有
         * Esc/Menu/Shift+F10 与 customWhatsThis 三类，见上）。 */
        return true;
    }
    default:
        return false;
    }
}

/* ==================== 浮层 ==================== */

/** @brief 销毁浮层与文本（对标 `delete QWhatsThat::instance`）。 */
static void xwt_destroyPopup(void)
{
    if (s_whatsThisPopup) {
        XWidget* popup = s_whatsThisPopup;
        s_whatsThisPopup = NULL;
        XWidget_hide(popup);
        XClassDelete((XClass*)popup);
    }
    if (s_whatsThisText) {
        XClassDelete((XClass*)s_whatsThisText);
        s_whatsThisText = NULL;
    }
    s_whatsThisTarget = NULL;
}

/** @brief 确保浮层存在（XLabel 优先；XLABEL_ON=0 退化为普通顶层 XWidget）。
 *  @details 窗口类型与 XToolTip 同款取 `XWindowType_ToolTip`：Qt 用
 *           Qt::Popup（qwhatsthis.cpp:155 `QWidget(parent, Qt::Popup)`），
 *           本框架平台层对瞬态弹层族（Popup/ToolTip/Splash）做了屏幕默认
 *           visual/depth 修复，ToolTip 与之同族且**不抢焦点**（Popup 在
 *           本框架会参与模态/焦点栈），故取 ToolTip。 */
static XWidget* xwt_ensurePopup(void)
{
    if (s_whatsThisPopup) return s_whatsThisPopup;
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    s_whatsThisPopup = (XWidget*)XLabel_create(NULL, 0);
#else
    s_whatsThisPopup = XWidget_create(NULL, 0);
#endif
    if (!s_whatsThisPopup) return NULL;
    XWidget_setWindowFlags(s_whatsThisPopup,
                           (XWidgetFlags)XWindowType_ToolTip);
    XWidget_setAutoFillBackground(s_whatsThisPopup, true);
    /* 不抢焦点：不调用 XWidget_setFocus（B13 验收 ③）。浮层的输入由
     * 应用级事件过滤器消费，无需键盘抓取（Qt 的 grabKeyboard 是为
     * Esc 收口，本框架的 Esc 已在过滤器里处理）。 */
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    XLabel_setWordWrap((XLabel*)s_whatsThisPopup, true);
    XLabel_setAlignment((XLabel*)s_whatsThisPopup,
                        (XAlignments)(XAlignment_Left | XAlignment_Top));
#endif
    return s_whatsThisPopup;
}

/** @brief 按文本刷新浮层内容与尺寸（对标 QWhatsThat 构造里的
 *         fontMetrics().boundingRect + resize，qwhatsthis.cpp:158-198 的
 *         纯文本分支：宽度上限按屏宽/3 夹到 [200,300]，此处以
 *         `XLabel` 的 sizeHint 承担同一事实）。 */
static void xwt_updatePopupContent(const XString* text)
{
    if (s_whatsThisText) {
        XClassDelete((XClass*)s_whatsThisText);
        s_whatsThisText = NULL;
    }
    if (text) s_whatsThisText = XString_create_copy(text);
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
    if (s_whatsThisPopup) {
        XSize hint;
        XLabel_setText((XLabel*)s_whatsThisPopup, text);
        hint = XWidget_sizeHint_base((const XWidget*)s_whatsThisPopup);
        if (hint.width > 0 && hint.height > 0)
            XWidget_resize(s_whatsThisPopup, hint.width + 1, hint.height);
    }
#else
    (void)text;
#endif
}

/** @brief 浮层落位（对标 QWhatsThisPrivate::say 的定位段，
 *         qwhatsthis.cpp:539-640）：优先控件下方 2px；放不下改其上方；
 *         越出屏幕工作区则钳制回来。 */
static void xwt_placePopup(int x, int y, XWidget* w)
{
    XRect avail;
    XPoint anchor;
    int ww = 0;
    int wh = 0;
    int popW;
    int popH;
    const int gap = 2;
    if (!s_whatsThisPopup) return;
    popW = XWidget_width(s_whatsThisPopup);
    popH = XWidget_height(s_whatsThisPopup);
    anchor.x = x;
    anchor.y = y;
    avail.x = 0; avail.y = 0; avail.width = 0; avail.height = 0;
    {
        XScreen* screen = XGuiApplication_screenAt(&anchor);
        if (screen) avail = XScreen_availableGeometry(screen);
    }
    if (w) {
        XPoint origin;
        XPoint global;
        origin.x = 0; origin.y = 0;
        global = XWidget_mapToGlobal(w, &origin);
        ww = XWidget_width(w);
        wh = XWidget_height(w);
        /* 水平：浮层比控件宽 ⇒ 与控件居中对齐；否则按给定点居中。 */
        if (popW > ww + 16)
            x = global.x + ww / 2 - popW / 2;
        else
            x = x - popW / 2;
        /* 垂直：优先控件下方 2px；越出下缘则改控件上方。 */
        y = global.y + wh + gap;
        if (avail.height > 0 && y + popH > avail.y + avail.height)
            y = global.y - gap - popH;
    } else {
        x = x - popW / 2;
        y = y + gap;
    }
    if (avail.width > 0) {
        if (x + popW > avail.x + avail.width)
            x = avail.x + avail.width - popW;
        if (x < avail.x) x = avail.x;
    } else if (x < 0) {
        x = 0;
    }
    if (avail.height > 0) {
        if (y + popH > avail.y + avail.height)
            y = avail.y + avail.height - popH;
        if (y < avail.y) y = avail.y;
    } else if (y < 0) {
        y = 0;
    }
    XWidget_move_2(s_whatsThisPopup, &(XPoint){x, y});
}

/* ==================== 模式三件套 ==================== */

void XWhatsThis_enterWhatsThisMode(void)
{
    /* Qt：`if (QWhatsThisPrivate::instance) return;`（:493-494）——幂等。 */
    if (s_whatsThisMode) return;
    s_whatsThisMode = true;
    s_leaveOnRelease = false;
    xwt_installFilter();
    /* 覆盖光标（Qt：:384-392 setOverrideCursor(WhatsThisCursor)；
     * 命中处不可帮助时 Qt 换 Forbidden，本实现的 Query 分支只投事件、
     * 不切光标——光标随模式固定为 WhatsThis，语义不丢）。 */
    {
        XCursor* cursor = XCursor_create_shape(XCursor_WhatsThis);
        if (cursor) {
            XGuiApplication_setOverrideCursor(cursor);
            XClassDelete((XClass*)cursor);
        }
    }
    xwt_notifyToplevels(XEVENT_TYPE_ENTER_WHATS_THIS_MODE);
}

bool XWhatsThis_inWhatsThisMode(void)
{
    return s_whatsThisMode;
}

void XWhatsThis_leaveWhatsThisMode(void)
{
    if (!s_whatsThisMode) return;   /* 未进入：空操作（见头文件注）。 */
    s_whatsThisMode = false;
    s_leaveOnRelease = false;
#if XACTION_ON
    /* 退出模式清勾选（对标 ~QWhatsThisPrivate 的 action->setChecked(false)，
     * qwhatsthis.cpp:365-369：析构 :365、判空 :368、清勾选 :369）：
     * 否则动作的勾选框会停在「已按下」，下一次点击（Qt 语义：勾上才进
     * 模式）无法再次触发。 */
    if (s_whatsThisAction) {
        XAction_setChecked(s_whatsThisAction, false);
        s_whatsThisAction = NULL;
    }
#endif
    /* 过滤器**不摘**（常驻，理由见 xwt_installFilter 注）：本函数可能
     * 正在过滤器回调内被调用（showText→leave 的链路），遍历中改
     * g_app->m_filters 有跳项风险；常驻过滤器的非模式路径是零副作用
     * 直通（见 xwt_filterEvent 首部闸）。
     * 覆盖光标弹出（对标 ~QWhatsThisPrivate 的 restoreOverrideCursor）。 */
    XGuiApplication_restoreOverrideCursor();
    xwt_notifyToplevels(XEVENT_TYPE_LEAVE_WHATS_THIS_MODE);
}

/* ==================== 浮层 API ==================== */

void XWhatsThis_showText(int x, int y, const XString* text, XWidget* w)
{
    /* Qt：空文本直接返回（say(): `if (text.size() == 0) return;`）。 */
    if (!text || XString_size((const XContainer*)text) == 0) return;
    /* Qt：`QWhatsThis::showText` 先 leaveWhatsThisMode()
     * （qwhatsthis.cpp:598-600：函数 :598、首句清模式 :600）。 */
    XWhatsThis_leaveWhatsThisMode();
    if (!xwt_ensurePopup()) {
        /* 无窗口环境：仅记录状态（与 XToolTip 的退化路径同口径）。 */
        xwt_updatePopupContent(text);
        s_whatsThisTarget = w;
        return;
    }
    xwt_updatePopupContent(text);
    s_whatsThisTarget = w;
    xwt_placePopup(x, y, w);
    XWidget_show(s_whatsThisPopup);
    XWidget_raise(s_whatsThisPopup);
    /* 独立顶层窗口无宿主帧泵：主动完成首帧绘制上屏（同 XToolTip 口径）。 */
    XWidget_flushBackingStore(s_whatsThisPopup, NULL);
}

void XWhatsThis_showText_2(int x, int y, const char* utf8, XWidget* w)
{
    XString* text;
    if (!utf8 || utf8[0] == '\0') return;   /* 空文本：Qt 同款早退。 */
    text = XString_create_utf8(utf8);
    if (!text) return;
    XWhatsThis_showText(x, y, text, w);
    XClassDelete((XClass*)text);
}

void XWhatsThis_hideText(void)
{
    xwt_destroyPopup();
}

bool XWhatsThis_isTextVisible(void)
{
    return s_whatsThisPopup && XWidget_isVisible(s_whatsThisPopup);
}

XString* XWhatsThis_text(void)
{
    if (!s_whatsThisText) return NULL;
    return XString_create_copy(s_whatsThisText);
}

/* ==================== 控件层事件入口（对标 QWidget::event 两分支） ======== */

bool XWhatsThis_handleWidgetEvent(XWidget* self, XEvent* event)
{
    XEventType type;
    const XString* text;
    XPoint global;
    const XHelpEvent* help;
    if (!self || !event) return false;
    type = XEvent_type(event);
    if (type != XEVENT_TYPE_WHATS_THIS && type != XEVENT_TYPE_QUERY_WHATS_THIS)
        return false;
    text = XWidget_whatsThis(self);
    /* 坐标：有载荷取载荷（QHelpEvent::globalPos），无载荷回落控件原点
     * （框架既有无载荷调用点 XDialog.c:1166 的兼容降级）。 */
    global.x = 0;
    global.y = 0;
    help = XHelpEvent_from(event);
    if (help) {
        global = help->m_globalPos;
    } else {
        XPoint origin;
        origin.x = 0;
        origin.y = 0;
        global = XWidget_mapToGlobal(self, &origin);
    }
    if (type == XEVENT_TYPE_WHATS_THIS) {
        if (text && XString_size((const XContainer*)text) > 0) {
            /* 对标 qwidget.cpp:9332-9334：`if (d->whatsThis.size())
             *   QWhatsThis::showText(help->globalPos(), d->whatsThis, this);`
             * 注意 Qt 此处**不动接受状态**（保持构造时的已接受）。 */
            XWhatsThis_showText(global.x, global.y, text, self);
            return true;
        }
        /* `else event->ignore();`（:9335-9336）——由 notify 上抛父控件。 */
        XEvent_ignore(event);
        return true;
    }
    /* QueryWhatsThis（:9338-9341）：有文本则保持接受，无文本 ignore。
     * Qt 里这条接受状态被 QWhatsThisPrivate 用来切
     * WhatsThis/Forbidden 光标；本实现在按下时按文本有无决定是否
     * 退出模式（见 xwt_filterEvent），语义一致。 */
    if (!text || XString_size((const XContainer*)text) == 0)
        XEvent_ignore(event);
    return true;
}

/* ==================== 动作 ==================== */

#if XACTION_ON
/** @brief 动作触发槽：勾选时进入帮助模式（对标
 *         `QWhatsThisAction::actionTriggered`，qwhatsthis.cpp:470-477）。
 *  @note  接收者就是动作本身（createAction 的连接），不依赖模块级指针。 */
static void xwt_actionTriggeredSlot(XObject* receiver, XVarList* args)
{
    XAction* action = (XAction*)receiver;
    (void)args;
    if (!action) return;
    if (XAction_isChecked(action))
        XWhatsThis_enterWhatsThisMode();
}

/** @brief 动作销毁槽：被跟踪的动作先亡则清模块级借用指针
 *         （复核 P3-1：s_whatsThisAction 是**借用**，动作经 parent 析构
 *         或调用方 XClassDelete 先亡时，leaveWhatsThisMode 里的
 *         setChecked 会读悬垂指针）。发送者经 XObject_sender 动态取
 *         （框架惯例，同 XButtonGroup 的 destroyed 槽），与被跟踪指针
 *         比对命中才清——多次 createAction 时前序动作死亡不影响对
 *         最新动作的跟踪。 */
static void xwt_actionDestroyedSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    if ((XObject*)s_whatsThisAction == XObject_sender(receiver))
        s_whatsThisAction = NULL;
}
#endif

XAction* XWhatsThis_createAction(XObject* parent)
{
#if XACTION_ON
    XAction* action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, parent,
                                        "What's This?");
    if (!action) return NULL;
    XAction_setCheckable(action, true);
    XObject_connect((XObject*)action, XSignal(XAction_triggered_signal),
                    (XObject*)action, xwt_actionTriggeredSlot,
                    XConnectionType_Direct);
    /* 退出模式时把勾选清掉（对标 `~QWhatsThisPrivate` 的
     * `action->setChecked(false)`，qwhatsthis.cpp:365-369：析构 :365、
     * 清勾选 :369）——模块级指针 s_whatsThisAction 承担该回写；其
     * 生命周期由 destroyed 钩子兜底（动作先亡即清针，见
     * xwt_actionDestroyedSlot）。过滤器常驻（xwt_installFilter），
     * 恰好是 destroyed 连接的稳定接收者。 */
    xwt_installFilter();
    if (s_whatsThisFilter) {
        XObject_connect((XObject*)action, XSignal(XObject_destroyed_signal),
                        s_whatsThisFilter, xwt_actionDestroyedSlot,
                        XConnectionType_Direct);
    }
    s_whatsThisAction = action;
    return action;
#else
    (void)parent;
    return NULL;
#endif
}

/* ==================== 直显入口 ==================== */

bool XWhatsThis_showTextForWidget(XWidget* widget)
{
    XWidget* target;
    const XString* text;
    XPoint origin;
    XPoint global;
    if (!widget) return false;
    if (!xwt_resolveHelp(widget, &(XPoint){0, 0}, &target, &text))
        return false;   /* 无文本：不显示、不改变焦点（B13 验收 ②）。 */
    origin.x = 0;
    origin.y = 0;
    global = XWidget_mapToGlobal(target, &origin);
    XWhatsThis_showText(global.x, global.y, text, target);
    return true;
}

#endif /* XWIDGET_ON */

#endif /* XGUI_ON */
