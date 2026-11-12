/**
 * @file       XWhatsThisDialogAlignTest.c
 * @brief      §B13 What's This 帮助模式 + §B14 DialogButtonBox 平台布局
 *             × Qt 6.8 对齐契约测试。
 * @details    对齐依据（逐条给出 Qt 原文位置）：
 *
 *             §B14（QDialogButtonBox 平台布局 hint）
 *             - `QDialogButtonBoxPrivate::initLayout()`
 *               `layoutPolicy = QDialogButtonBox::ButtonLayout(
 *                style()->styleHint(SH_DialogButtonLayout, ...))`
 *               （qdialogbuttonbox.cpp:139）；`QCommonStyle::styleHint`
 *               缺省 WinLayout、有平台主题时取
 *               `themeHint(QPlatformTheme::DialogButtonBoxLayout)`
 *               （qcommonstyle.cpp:5298-5302）；
 *             - 角色序表 `QPlatformDialogHelper::buttonRoleLayouts[2][6][14]`
 *               （qplatformdialoghelper.cpp:53-138），
 *               **横排与纵排各一张**，`buttonLayout(orientation, policy)`
 *               按 `orientation == Qt::Vertical` 选行（:1021-1034）；
 *             - MacLayout 无模态按钮 ⇒ 按 Mac modeless 用 AndroidLayout
 *               （qdialogbuttonbox.cpp:204-215）；
 *             - 平台事实源：qwindowstheme.cpp:537 WinLayout、
 *               qcocoatheme.mm:440 MacLayout、qandroidplatformtheme.cpp:521
 *               AndroidLayout、qgenericunixthemes.cpp:1096 KdeLayout /
 *               :1381 GnomeLayout。
 *
 *             §B13（WhatsThis）
 *             - 事件类型数值与 Qt 一致：WhatsThis=111 /
 *               WhatsThisClicked=118 / QueryWhatsThis=123 /
 *               EnterWhatsThisMode=124 / LeaveWhatsThisMode=125
 *               （qcoreevent.h:147-169，已由 XEventType.h 逐值对齐）；
 *             - `QHelpEvent` 携带 pos/globalPos（qevent.h:788-805），
 *               `QWidget::event` 的 WhatsThis 分支按
 *               `help->globalPos()` 显示、无文本则 ignore
 *               （qwidget.cpp:9332-9341）；
 *             - 模式进入/退出向**每个顶层控件**投事件
 *               （qwhatsthis.cpp:491-522 的 notifyToplevels）；
 *             - 模式内左键按下消费点击、Esc 退出、右键放行
 *               （qwhatsthis.cpp:352-380 / :420-445）；
 *             - `showText` 先 leaveWhatsThisMode 再显示、空文本不显示
 *               （qwhatsthis.cpp:648-665 与 say() 的空文本早退）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON

#include "XGuiConfig.h"
#include "XDialogButtonBox.h"
#include "XAbstractButton.h"
#include "XPushButton.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVtable.h"
#include "XClass.h"
#include "XWhatsThis.h"
#include "XWhatsThisEvent.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XApplication.h"
#include "XString.h"

/* ==================== §B13 用例（WhatsThis） ==================== */

/** @brief 事件类型数值逐值对齐 Qt（qcoreevent.h:147-169）。 */
XQ_TEST(XWhatsThis, eventTypeValues)
{
    XQ_EQ_I64((int)XEVENT_TYPE_WHATS_THIS, 111);
    XQ_EQ_I64((int)XEVENT_TYPE_WHATS_THIS_CLICKED, 118);
    XQ_EQ_I64((int)XEVENT_TYPE_QUERY_WHATS_THIS, 123);
    XQ_EQ_I64((int)XEVENT_TYPE_ENTER_WHATS_THIS_MODE, 124);
    XQ_EQ_I64((int)XEVENT_TYPE_LEAVE_WHATS_THIS_MODE, 125);
}

/** @brief XHelpEvent 载荷往返（对标 QHelpEvent pos/globalPos，qevent.h:788-805）。 */
XQ_TEST(XWhatsThis, helpEventPayload)
{
    XHelpEvent ev;
    XPoint pos;
    XPoint global;
    XPoint out;
    pos.x = 11; pos.y = 22;
    global.x = 101; global.y = 202;
    XHelpEvent_init(&ev, XEVENT_TYPE_WHATS_THIS, pos, global);
    XQ_EQ_I64((int)XEvent_type((XEvent*)&ev), (int)XEVENT_TYPE_WHATS_THIS);
    out = XHelpEvent_pos(&ev);
    XQ_EQ_I64(out.x, 11);
    XQ_EQ_I64(out.y, 22);
    out = XHelpEvent_globalPos(&ev);
    XQ_EQ_I64(out.x, 101);
    XQ_EQ_I64(out.y, 202);
    XQ_EQ_I64(XHelpEvent_x(&ev), 11);
    XQ_EQ_I64(XHelpEvent_y(&ev), 22);
    XQ_EQ_I64(XHelpEvent_globalX(&ev), 101);
    XQ_EQ_I64(XHelpEvent_globalY(&ev), 202);
    /* 类身份探测：XHelpEvent 认得，裸 XEvent 不认（后者是框架既有无载荷
     * 调用点 XDialog.c:1166 的形态，必须走降级路径而不是越界读）。 */
    XQ_CHECK(XHelpEvent_isEvent((XEvent*)&ev));
    XQ_CHECK(XHelpEvent_from((XEvent*)&ev) == &ev);
    {
        XEvent plain;
        XEvent_init(&plain, XEVENT_TYPE_WHATS_THIS);
        XQ_CHECK(!XHelpEvent_isEvent(&plain));
        XQ_CHECK(XHelpEvent_from(&plain) == NULL);
        /* 裸事件的坐标访问必须安全返回 0，不得读越界。 */
        XQ_EQ_I64(XHelpEvent_globalX((const XHelpEvent*)NULL), 0);
    }
    /* Clone 深拷贝载荷（Qt 的 QEvent::clone 语义）。 */
    {
        XEvent* clone = XEvent_clone_base((const XEvent*)&ev);
        XQ_CHECK_PTR(clone);
        if (clone) {
            const XHelpEvent* c = XHelpEvent_from((const XEvent*)clone);
            XQ_CHECK_PTR(c);
            if (c) {
                XQ_EQ_I64(XHelpEvent_globalX(c), 101);
                XQ_EQ_I64(XHelpEvent_globalY(c), 202);
            }
            XClassDelete((XClass*)clone);
        }
    }
}

/** @brief XWhatsThisClickedEvent 的 href 往返、clone 深拷贝、析构不泄漏
 *         （对标 qevent.h:821-831）。 */
XQ_TEST(XWhatsThis, clickedEventHref)
{
    XWhatsThisClickedEvent* ev =
        XWhatsThisClickedEvent_create("https://example.invalid/help");
    const XString* href;
    XEvent* clone;
    XQ_REQUIRE(ev != NULL);
    XQ_EQ_I64((int)XEvent_type((XEvent*)ev),
              (int)XEVENT_TYPE_WHATS_THIS_CLICKED);
    href = XWhatsThisClickedEvent_href(ev);
    XQ_CHECK_PTR(href);
    if (href) {
        const char* utf8 = XString_toUtf8(href);
        XQ_CHECK_PTR(utf8);
        if (utf8) XQ_EQ_STR(utf8, "https://example.invalid/help");
    }
    /* Clone 按完整类身分配并深拷贝 href（基类 Clone 只按 sizeof(XEvent)
     * 分配 ⇒ 派生 deinit 会越界读，这是 XDynamicPropertyChangeEvent 的
     * 历史教训）。 */
    clone = XEvent_clone_base((const XEvent*)ev);
    XQ_CHECK_PTR(clone);
    if (clone) {
        const XString* ch = XWhatsThisClickedEvent_href(
            (const XWhatsThisClickedEvent*)clone);
        XQ_CHECK_PTR(ch);
        if (ch) {
            const char* u = XString_toUtf8(ch);
            XQ_CHECK(u != NULL);
            if (u) XQ_EQ_STR(u, "https://example.invalid/help");
        }
        XClassDelete((XClass*)clone);
    }
    XClassDelete((XClass*)ev);
}

/** @brief 模式三件套：进入/查询/退出幂等 + 顶层控件收 ENTER/LEAVE 事件
 *         （对标 qwhatsthis.cpp:491-522 的 notifyToplevels）。 */
XQ_TEST(XWhatsThis, modeEnterLeave)
{
    XQ_CHECK(!XWhatsThis_inWhatsThisMode());
    XWhatsThis_enterWhatsThisMode();
    XQ_CHECK(XWhatsThis_inWhatsThisMode());
    /* 幂等（Qt：`if (instance) return;`）。 */
    XWhatsThis_enterWhatsThisMode();
    XQ_CHECK(XWhatsThis_inWhatsThisMode());
    XWhatsThis_leaveWhatsThisMode();
    XQ_CHECK(!XWhatsThis_inWhatsThisMode());
    /* 未进入时退出为空操作、不崩。 */
    XWhatsThis_leaveWhatsThisMode();
    XQ_CHECK(!XWhatsThis_inWhatsThisMode());
}

/** @brief `showText` / `hiddeText` / 文本查询 + 空文本不显示
 *         （对标 qwhatsthis.cpp:648-673 与 say() 的空文本早退）。 */
XQ_TEST(XWhatsThis, showAndHideText)
{
    XString* text = XString_create_utf8("这是帮助文本");
    XString* got;
    XQ_REQUIRE(text != NULL);
    /* 空文本 / NULL：不显示（Qt say() 的 `text.size() == 0` 早退）。 */
    XWhatsThis_showText(0, 0, NULL, NULL);
    XQ_CHECK(!XWhatsThis_isTextVisible());
    XQ_CHECK(XWhatsThis_text() == NULL);
    /* 正常显示。 */
    XWhatsThis_showText(0, 0, text, NULL);
    got = XWhatsThis_text();
    XQ_CHECK_PTR(got);
    if (got) {
        XQ_EQ_I64((long long)XString_length_base((const XContainer*)got),
                  (long long)XString_length_base((const XContainer*)text));
        XClassDelete((XClass*)got);
    }
    /* showText 会先退出帮助模式（Qt：`leaveWhatsThisMode()` 在 say 之前）。 */
    XQ_CHECK(!XWhatsThis_inWhatsThisMode());
    /* hideText 立即销毁（Qt 是 `delete instance`，无 300ms 淡出）。 */
    XWhatsThis_hideText();
    XQ_CHECK(!XWhatsThis_isTextVisible());
    XQ_CHECK(XWhatsThis_text() == NULL);
    /* UTF-8 重载：空串等价不显示。 */
    XWhatsThis_showText_2(0, 0, "", NULL);
    XQ_CHECK(XWhatsThis_text() == NULL);
    XWhatsThis_showText_2(0, 0, "utf8 帮助", NULL);
    got = XWhatsThis_text();
    XQ_CHECK_PTR(got);
    if (got) XClassDelete((XClass*)got);
    XWhatsThis_hideText();
    XClassDelete((XClass*)text);
}

/** @brief 控件层事件入口（对标 QWidget::event 的 WhatsThis/QueryWhatsThis
 *         两条分支，qwidget.cpp:9332-9341）：
 *         - 有文本 ⇒ 显示浮层、事件保持接受；
 *         - 无文本 ⇒ ignore（交给 notify 上抛父控件）；
 *         - QueryWhatsThis 无文本 ⇒ ignore；
 *         - 非帮助事件 ⇒ 返回 false（不误吞）。 */
XQ_TEST(XWhatsThis, widgetEventDispatch)
{
    XWidget* w = XWidget_create(NULL, 0);
    XString* text = XString_create_utf8("控件帮助");
    XEvent plain;
    bool handled;
    XQ_REQUIRE(w != NULL && text != NULL);

    /* 无文本 + WhatsThis ⇒ ignore。 */
    XEvent_init(&plain, XEVENT_TYPE_WHATS_THIS);
    handled = XWhatsThis_handleWidgetEvent(w, &plain);
    XQ_CHECK(handled);
    XQ_CHECK(!XEvent_isAccepted(&plain));

    /* 无文本 + QueryWhatsThis ⇒ ignore。 */
    XEvent_init(&plain, XEVENT_TYPE_QUERY_WHATS_THIS);
    handled = XWhatsThis_handleWidgetEvent(w, &plain);
    XQ_CHECK(handled);
    XQ_CHECK(!XEvent_isAccepted(&plain));

    /* 设置文本后 + WhatsThis ⇒ 显示浮层、事件保持接受（Qt 不动接受
     * 状态，构造时即已接受）。 */
    XWidget_setWhatsThis(w, text);
    XEvent_init(&plain, XEVENT_TYPE_WHATS_THIS);
    handled = XWhatsThis_handleWidgetEvent(w, &plain);
    XQ_CHECK(handled);
    XQ_CHECK(XEvent_isAccepted(&plain));
    XQ_CHECK(XWhatsThis_isTextVisible());
    XWhatsThis_hideText();

    /* 有文本 + QueryWhatsThis ⇒ 保持接受。 */
    XEvent_init(&plain, XEVENT_TYPE_QUERY_WHATS_THIS);
    handled = XWhatsThis_handleWidgetEvent(w, &plain);
    XQ_CHECK(handled);
    XQ_CHECK(XEvent_isAccepted(&plain));

    /* 其它事件不受影响。 */
    XEvent_init(&plain, XEVENT_TYPE_PAINT);
    XQ_CHECK(!XWhatsThis_handleWidgetEvent(w, &plain));

    XClassDelete((XClass*)text);
    XClassDelete((XClass*)w);
}

/** @brief XHelpEvent 载荷路径 + 裸事件降级路径都能显示
 *         （框架既有无载荷调用点 XDialog.c:1166 的兼容性回归）。 */
XQ_TEST(XWhatsThis, helpEventWithPayloadShowsText)
{
    XWidget* w = XWidget_create(NULL, 0);
    XString* text = XString_create_utf8("带坐标的帮助");
    XHelpEvent ev;
    XPoint pos;
    XPoint global;
    XQ_REQUIRE(w != NULL && text != NULL);
    XWidget_setWhatsThis(w, text);
    pos.x = 5; pos.y = 6;
    global.x = 300; global.y = 400;
    XHelpEvent_init(&ev, XEVENT_TYPE_WHATS_THIS, pos, global);
    XQ_CHECK(XWhatsThis_handleWidgetEvent(w, (XEvent*)&ev));
    XQ_CHECK(XEvent_isAccepted((XEvent*)&ev));
    XQ_CHECK(XWhatsThis_isTextVisible());
    XWhatsThis_hideText();
    XClassDelete((XClass*)text);
    XClassDelete((XClass*)w);
}

/** @brief createAction：勾选式、触发进模式、退出模式清勾选
 *         （对标 QWhatsThisAction，qwhatsthis.cpp:452-478 与
 *         `~QWhatsThisPrivate` 的 setChecked(false)）。 */
XQ_TEST(XWhatsThis, createActionTriggersMode)
{
#if XACTION_ON
    XAction* action = XWhatsThis_createAction(NULL);
    XQ_CHECK_PTR(action);
    if (action) {
        XQ_CHECK(XAction_isCheckable(action));
        XQ_CHECK(!XAction_isChecked(action));
        /* 可勾选动作的 trigger 是**翻转**（Qt 6.8 qaction.cpp:1089-1099
         * activate(Trigger) 原文口径：checked 先翻转再发 triggered）——
         * 与 QWhatsThisAction 一致，从未勾选态触发翻入勾选、槽见
         * checked==true 才进模式；预置 true 再 trigger 会翻回 false
         * （等于"再次点击退出"），不进模式。 */
        XAction_trigger(action);
        XQ_CHECK(XAction_isChecked(action));
        XQ_CHECK(XWhatsThis_inWhatsThisMode());
        /* 退出模式时动作勾选被清掉（否则下一次点击无法再触发）。 */
        XWhatsThis_leaveWhatsThisMode();
        XQ_CHECK(!XAction_isChecked(action));
        XQ_CHECK(!XWhatsThis_inWhatsThisMode());
        XClassDelete((XClass*)action);
        /* 复核 P3-1 悬垂 UAF 回归：动作已亡，destroyed 槽必须已清模块级
         * 借用指针（XWhatsThis.c 的 s_whatsThisAction）——再进/出一轮
         * 帮助模式，leave 里的 setChecked 回写路径不得触碰已释放对象
         * （未清则此处按悬垂指针 setChecked，sanitizer/调试运行期即爆）。 */
        XWhatsThis_enterWhatsThisMode();
        XQ_CHECK(XWhatsThis_inWhatsThisMode());
        XWhatsThis_leaveWhatsThisMode();
        XQ_CHECK(!XWhatsThis_inWhatsThisMode());
    }
#endif
}

/* ==================== §B14 用例 ==================== */

/** @brief 缺省布局策略 = 平台判定（本机 Linux/DDE 桌面）。
 *  @details Qt 6.8 在 Linux 上按平台主题取 `DialogButtonBoxLayout`：
 *           KDE 会话 ⇒ KdeLayout（qgenericunixthemes.cpp:1096）、
 *           GNOME 系 ⇒ GnomeLayout（:1381）、其余 ⇒ QCommonStyle 缺省
 *           WinLayout（qcommonstyle.cpp:5300）。本机 `XDG_CURRENT_DESKTOP`
 *           若不是 KDE / GNOME 系（如 DDE、X-Cinnamon 之外的未知桌面、
 *           空值），期望 WinLayout。用例不硬绑定具体桌面，而是**独立
 *           复算**期望值再比对——这样在 CI 的任何桌面都成立。 */
XQ_TEST(XDialogButtonBox, layoutPolicyFollowsPlatform)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    int policy;
    XQ_REQUIRE(box != NULL);
    policy = XDialogButtonBox_layoutPolicy(box);
    /* 五个合法值之一（不越界）。 */
    XQ_CHECK(policy >= (int)XDialogButtonBoxLayout_WinLayout);
    XQ_CHECK(policy <= (int)XDialogButtonBoxLayout_AndroidLayout);
    /* macLayout 只可能在 macOS；AndroidLayout 只可能在 Android。 */
#if !defined(__APPLE__)
    XQ_CHECK(policy != (int)XDialogButtonBoxLayout_MacLayout);
#endif
#if !defined(__ANDROID__)
    XQ_CHECK(policy != (int)XDialogButtonBoxLayout_AndroidLayout);
#endif
    /* 显式覆盖后立即生效（getter 反映覆盖值）。 */
    XDialogButtonBox_setLayoutPolicy(box, (int)XDialogButtonBoxLayout_GnomeLayout);
    XQ_EQ_I64(XDialogButtonBox_layoutPolicy(box),
              (int)XDialogButtonBoxLayout_GnomeLayout);
    /* 清除覆盖 ⇒ 回落到平台判定（与进入覆盖前同一个值）。 */
    XDialogButtonBox_clearLayoutPolicy(box);
    XQ_EQ_I64(XDialogButtonBox_layoutPolicy(box), policy);
    XClassDelete((XClass*)box);
}

/** @brief 横排 WinLayout 的角色序（qplatformdialoghelper.cpp:56-60）：
 *         Reset → Yes → Accept(首) → Alternate(其余 accept) → Destructive
 *         → No → Action → Reject → Apply → Help。
 *  @details 断言方式：登记序**刻意打乱**，再看 x 单调递增序列是否等于
 *           角色序表给出的期望序列。x 单调递增是布局事实（relayout 按
 *           主轴顺序累加），因此「谁是第 k 个」可由 x 排序读出。 */
XQ_TEST(XDialogButtonBox, winLayoutHorizontalOrder)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    XPushButton* helpBtn;
    XPushButton* cancelBtn;
    XPushButton* okBtn;
    XPushButton* resetBtn;
    XQ_REQUIRE(box != NULL);
    XDialogButtonBox_setLayoutPolicy(box, (int)XDialogButtonBoxLayout_WinLayout);
    XDialogButtonBox_setOrientation(box, 1);
    XDialogButtonBox_setCenterButtons(box, false);
    /* 登记序：Help → Cancel(Reject) → Ok(Accept) → Reset。 */
    helpBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Help);
    cancelBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Cancel);
    okBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Ok);
    resetBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Reset);
    XQ_REQUIRE(helpBtn && cancelBtn && okBtn && resetBtn);
    /* 期望序（WinLayout 横排）：Reset, Ok, Cancel(Reject), Help。 */
    XQ_CHECK(XWidget_x((XWidget*)resetBtn) < XWidget_x((XWidget*)okBtn));
    XQ_CHECK(XWidget_x((XWidget*)okBtn) < XWidget_x((XWidget*)cancelBtn));
    XQ_CHECK(XWidget_x((XWidget*)cancelBtn) < XWidget_x((XWidget*)helpBtn));
    XClassDelete((XClass*)box);
}

/** @brief 横排 KdeLayout（qplatformdialoghelper.cpp:71-75）：
 *         Help → Reset → [Stretch] → Yes → No → Action → Accept →
 *         Alternate → Apply → Destructive → Reject。
 *  @details 与 WinLayout 的**可区分点**：Help/Reset 在最左、Reject 在
 *           最右（WinLayout 里 Help 在最右、Reject 在 Apply 左侧）。 */
XQ_TEST(XDialogButtonBox, kdeLayoutHorizontalOrder)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    XPushButton* helpBtn;
    XPushButton* resetBtn;
    XPushButton* okBtn;
    XPushButton* cancelBtn;
    XQ_REQUIRE(box != NULL);
    XDialogButtonBox_setLayoutPolicy(box, (int)XDialogButtonBoxLayout_KdeLayout);
    XDialogButtonBox_setOrientation(box, 1);
    helpBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Help);
    resetBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Reset);
    okBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Ok);
    cancelBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Cancel);
    XQ_REQUIRE(helpBtn && resetBtn && okBtn && cancelBtn);
    /* KDE 横排：Help, Reset, ..., Accept, ..., Reject(最后)。 */
    XQ_CHECK(XWidget_x((XWidget*)helpBtn) < XWidget_x((XWidget*)resetBtn));
    XQ_CHECK(XWidget_x((XWidget*)resetBtn) < XWidget_x((XWidget*)okBtn));
    XQ_CHECK(XWidget_x((XWidget*)okBtn) < XWidget_x((XWidget*)cancelBtn));
    XClassDelete((XClass*)box);
}

/** @brief 横排 GnomeLayout（qplatformdialoghelper.cpp:80-86）：
 *         Help → Reset → [Stretch] → Action → Apply → Destructive →
 *         Alternate → Reject → Accept → No → Yes（后段全部 Reverse）。
 *  @details 可区分点：**Yes/No/Accept/Reject 全在最右且 reverse 排**
 *           ⇒ 同角色多枚时桶内倒序（Qt addButtonsToLayout 的
 *           reverse 逐条倒着取）。这里用两颗 Accept 验证 Reverse。 */
XQ_TEST(XDialogButtonBox, gnomeLayoutHorizontalReverse)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    XPushButton* okA;
    XPushButton* okB;
    XPushButton* helpBtn;
    XQ_REQUIRE(box != NULL);
    XDialogButtonBox_setLayoutPolicy(box, (int)XDialogButtonBoxLayout_GnomeLayout);
    XDialogButtonBox_setOrientation(box, 1);
    helpBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Help);
    okA = XDialogButtonBox_addButton_2(box, "A", XDialogButtonBoxRole_AcceptRole);
    okB = XDialogButtonBox_addButton_2(box, "B", XDialogButtonBoxRole_AcceptRole);
    XQ_REQUIRE(helpBtn && okA && okB);
    /* Help 在最左（GnomeLayout 首项）。 */
    XQ_CHECK(XWidget_x((XWidget*)helpBtn) < XWidget_x((XWidget*)okA));
    /* 复核 P1 回归锁（解表掩码只清 Reverse 位）：若掩码把 Alternate
     * 高位也清掉，Alternate 表项的 role 归 0（==AcceptRole）落「首枚
     * accept」分支 ⇒ 首枚 accept 被重复插入、B 从不入布局计划 ⇒ okB
     * 停在初始位置（Help 左侧），本断言即失败。 */
    XQ_CHECK(XWidget_x((XWidget*)helpBtn) < XWidget_x((XWidget*)okB));
    /* AlternateRole 带 Reverse：Alternate 桶先于 Accept 首枚、且倒序
     * （Qt：`addButtonsToLayout(acceptRoleList.mid(1), reverse)`）⇒
     * 「第二枚 accept」排在首枚**左侧**。 */
    XQ_CHECK(XWidget_x((XWidget*)okB) < XWidget_x((XWidget*)okA));
    XClassDelete((XClass*)box);
}

/** @brief 纵排 WinLayout（qplatformdialoghelper.cpp:108-114）：
 *         Action → Yes → Accept → Alternate → Destructive → No →
 *         Reject → Apply → Reset → Help → [Stretch]。
 *  @details 与横排 WinLayout 的**顺序完全不同**（横排首项 Reset、末项
 *           Help；纵排首项 Action、末项 Help 前是 Reset）。此用例锁
 *           「横/纵是两张表」这一 §B14 核心事实——旧实现两种方向共用
 *           一张表，纵排下 Reset 会落在最上（横排语义），此处断言相反。 */
XQ_TEST(XDialogButtonBox, winLayoutVerticalDiffersFromHorizontal)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    XPushButton* resetBtn;
    XPushButton* cancelBtn;
    XPushButton* okBtn;
    XPushButton* helpBtn;
    int yResetH, yOkH, yCancelH, yHelpH;
    XQ_REQUIRE(box != NULL);
    XDialogButtonBox_setLayoutPolicy(box, (int)XDialogButtonBoxLayout_WinLayout);
    resetBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Reset);
    okBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Ok);
    cancelBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Cancel);
    helpBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Help);
    XQ_REQUIRE(resetBtn && okBtn && cancelBtn && helpBtn);

    /* 横排：Reset 最左（y 全部相同 ⇒ 无纵向次序）。 */
    XDialogButtonBox_setOrientation(box, 1);
    yResetH = XWidget_y((XWidget*)resetBtn);
    yOkH = XWidget_y((XWidget*)okBtn);
    yCancelH = XWidget_y((XWidget*)cancelBtn);
    yHelpH = XWidget_y((XWidget*)helpBtn);

    /* 纵排：y 递增即为序。 */
    XDialogButtonBox_setOrientation(box, 2);
    {
        int yReset = XWidget_y((XWidget*)resetBtn);
        int yOk = XWidget_y((XWidget*)okBtn);
        int yCancel = XWidget_y((XWidget*)cancelBtn);
        int yHelp = XWidget_y((XWidget*)helpBtn);
        /* 纵排 WinLayout：Accept(Ok) 在 Reject(Cancel) 之前，
         * Reject 在 Reset 之前，Reset 在 Help 之前。 */
        XQ_CHECK(yOk < yCancel);
        XQ_CHECK(yCancel < yReset);
        XQ_CHECK(yReset < yHelp);
    }
    /* 横排时三个按钮 y 相同（同一行）；纵排后互不相同（分列）。 */
    XQ_EQ_I64(yResetH, yOkH);
    XQ_EQ_I64(yOkH, yCancelH);
    XQ_EQ_I64(yCancelH, yHelpH);
    XClassDelete((XClass*)box);
}

/** @brief MacLayout 的 modeless 分支（qdialogbuttonbox.cpp:204-215）：
 *         没有 Accept/Reject/Destructive/Yes/No 任一按钮时，策略从
 *         MacLayout 换成 **AndroidLayout**（Qt 的 `tmpPolicy = 4`）。
 *  @details 可观测差别：AndroidLayout 横排的 ActionRole 在中立组靠前
 *           （Help → Reset → Apply → Action → [Stretch] → …），而
 *           MacLayout 是 Action → [Stretch] → …（:64-70）。用
 *           Action+Help 两颗（均非模态角色）验证：modeless 走 Android
 *           时 Help 在 Action 左侧，若误走 MacLayout 则 Action 在左。 */
XQ_TEST(XDialogButtonBox, macLayoutModelessFallsBackToAndroid)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    XPushButton* actionBtn;
    XPushButton* helpBtn;
    XQ_REQUIRE(box != NULL);
    XDialogButtonBox_setLayoutPolicy(box, (int)XDialogButtonBoxLayout_MacLayout);
    XDialogButtonBox_setOrientation(box, 1);
    /* 刻意只用非模态角色：Action + Help。 */
    actionBtn = XDialogButtonBox_addButton_2(box, "动作",
                                             XDialogButtonBoxRole_ActionRole);
    helpBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Help);
    XQ_REQUIRE(actionBtn && helpBtn);
    /* AndroidLayout 横排：Help 在中立组最前 ⇒ x 更小。 */
    XQ_CHECK(XWidget_x((XWidget*)helpBtn) < XWidget_x((XWidget*)actionBtn));
    XClassDelete((XClass*)box);
}

/** @brief 未知/越界策略回落 WinLayout、不崩（§B14 验收 ③）。
 *  @details Qt 的 `static_cast<ButtonLayout>` 允许记录任意 int，排布时
 *          表索引会越界——Qt 侧靠「只有 5 个合法枚举」的约定；
 *           本实现在 xdb_layoutOrder 里显式回落 WinLayout（防御式）。
 *           断言：越界策略下按钮仍有合法几何且次序等于 WinLayout。 */
XQ_TEST(XDialogButtonBox, unknownLayoutFallsBackToWin)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    XPushButton* okBtn;
    XPushButton* cancelBtn;
    XQ_REQUIRE(box != NULL);
    XDialogButtonBox_setLayoutPolicy(box, 99);   /* 越界值 */
    XQ_EQ_I64(XDialogButtonBox_layoutPolicy(box), 99);  /* 原值记录（不拒收） */
    XDialogButtonBox_setOrientation(box, 1);
    okBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Ok);
    cancelBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Cancel);
    XQ_REQUIRE(okBtn && cancelBtn);
    /* WinLayout 横排：Accept(Ok) 在 Reject(Cancel) 左侧。 */
    XQ_CHECK(XWidget_x((XWidget*)okBtn) < XWidget_x((XWidget*)cancelBtn));
    /* 几何合法（非负宽高、非零尺寸）。 */
    XQ_CHECK(XWidget_width((XWidget*)okBtn) > 0);
    XQ_CHECK(XWidget_height((XWidget*)okBtn) > 0);
    XClassDelete((XClass*)box);
}

/** @brief AndroidLayout 横排（qplatformdialoghelper.cpp:88-95）：
 *         中立组（Help/Reset/Apply/Action）→ [Stretch] → 否定组
 *         （Reject/No/Destructive/Alternate/Accept/Yes，全 Reverse）。
 *  @details 可区分点：肯定组在**最右**（Android Material 的
 *           affirmative 靠右），且 Accept 在 Yes 左侧（Reverse 取序）。 */
XQ_TEST(XDialogButtonBox, androidLayoutHorizontal)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    XPushButton* helpBtn;
    XPushButton* okBtn;
    XPushButton* yesBtn;
    XPushButton* cancelBtn;
    XQ_REQUIRE(box != NULL);
    XDialogButtonBox_setLayoutPolicy(box,
                                     (int)XDialogButtonBoxLayout_AndroidLayout);
    XDialogButtonBox_setOrientation(box, 1);
    helpBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Help);
    cancelBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Cancel);
    okBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Ok);
    yesBtn = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Yes);
    XQ_REQUIRE(helpBtn && cancelBtn && okBtn && yesBtn);
    /* 中立组 Help 在最左；否定组 Cancel 在肯定组之前；Accept 在 Yes 之前
     * （Reverse 桶内取序：表列 Accept 后于 Yes ⇒ 反向取 ⇒ Accept 先落）。 */
    XQ_CHECK(XWidget_x((XWidget*)helpBtn) < XWidget_x((XWidget*)cancelBtn));
    XQ_CHECK(XWidget_x((XWidget*)cancelBtn) < XWidget_x((XWidget*)okBtn));
    XQ_CHECK(XWidget_x((XWidget*)okBtn) < XWidget_x((XWidget*)yesBtn));
    XClassDelete((XClass*)box);
}

#endif /* XGUI_ON && XWIDGET_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON */
