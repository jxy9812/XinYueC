/**
 * @file       XDialogAlignTest.c
 * @brief      XDialog × Qt 6.8 QDialog 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qdialog.h
 *             与 Qt 6.8 源码 src/widgets/dialogs/qdialog.cpp（jsdelivr
 *             v6.8.0 原文），关键行为用本机 Qt 6.8.0（QT_QPA_PLATFORM=
 *             offscreen）探针实测：
 *             - 构造默认：result=0、modal=false、windowModality=NonModal、
 *               sizeGripEnabled=false（D0）；
 *             - setModal(true) → isModal=1 且 windowModality=2
 *               （ApplicationModal）；setModal(false) → 两者复位为 0
 *               （D1/D2，对应 qwidget.cpp 的 WA_ShowModal 置/清）；
 *             - sizeGripEnabled 真嵌入一个 QSizeGrip 子控件，关闭即删除
 *               （D3/D4，qdialog.cpp:1019-1049）；
 *             - Esc 无修饰 → rejected；Esc+Shift 不匹配 QKeySequence::
 *               Cancel，不关窗且事件未接受（D9/D10，qdialog.cpp:683-687）；
 *             - Return 无修饰/小键盘 Enter → 点击默认按钮；Ctrl+Return
 *               不点击（D6/D7/D8，qdialog.cpp:691-695）；
 *             - done(3) 只发 finished(3)，accept 先 accepted 后 finished(1)
 *               （D13/D14，qdialog.cpp:600-625）；
 *             - exec 期间 isModal=1/windowModality=2，返回后复原（D11/D12，
 *               qdialog.cpp:563-576）；
 *             - contextMenuEvent 的 What's This：命中最深子控件后沿父链
 *               找首个 whatsThis 非空者，弹「What's This?」菜单，选中
 *               向该控件投递 XEVENT_TYPE_WHATS_THIS（qdialog.cpp:654-680；
 *               本框架用非阻塞 XMenu_popup 代替 exec，见 XDialog.c 注）。
 *
 *             **已登记偏差**：对话框构建时预置 640×480 顶层几何（Qt 顶层
 *             默认 640×480 由平台/布局决定，见 XDialog.h 注），本测试按
 *             本库口径断言。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDIALOG_ON

#include "XDialog.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XVarList.h"
#include "XClass.h"
#include "XContainer.h"
#include "XVector.h"
#include "XMemory.h"
#include "XString.h"
#include "XPushButton.h"
#include "XAbstractButton.h"
#if XMENU_ON
#include "XMenu.h"
#endif
#if XSIZEGRIP_ON
#include "XSizeGrip.h"
#endif

#if XMENU_ON
/* ==================== 「这是什么」事件过滤器探针 ==================== */

XCLASS_DEFINE_BEGING(XDialogHelpProbe)
XCLASS_DEFINE_EXTEND_END(XDialogHelpProbe, XObject)

typedef struct XDialogHelpProbe
{
    XObject m_base;       /**< 基类；必须是第一个成员。 */
    int     m_whatsThis;  /**< 收到的 WHATS_THIS 帮助事件数。 */
} XDialogHelpProbe;

static bool VXDialogHelpProbe_eventFilter(XObject* self, XObject* watched,
                                          XEvent* event)
{
    XDialogHelpProbe* probe = (XDialogHelpProbe*)self;
    (void)watched;
    if (probe && event && XEvent_type(event) == XEVENT_TYPE_WHATS_THIS)
        ++probe->m_whatsThis;
    return false; /* 不消费。 */
}

static XVtable* XDialogHelpProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDialogHelpProbe)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_EventFilter,
                             VXDialogHelpProbe_eventFilter);
    return XVTABLE_DEFAULT;
}

static XDialogHelpProbe* XDialogHelpProbe_create(void)
{
    XDialogHelpProbe* probe = (XDialogHelpProbe*)XMemory_malloc(
        sizeof(XDialogHelpProbe), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!probe) return NULL;
    XMemset(probe, 0, sizeof(*probe));
    XObject_init(&probe->m_base);
    XClassSetVtable(probe, XDialogHelpProbe);
    Set_Class_Memory(probe, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(probe, true);
    return probe;
}

/** @brief 在直接子对象里找第一个 XMenu。 */
static XMenu* xdlg_findChildMenu(XDialog* dlg)
{
    XObject* object = (XObject*)dlg;
    int count;
    int i;
    if (!object || !object->m_children) return NULL;
    count = (int)XVector_size_base((const XContainer*)object->m_children);
    for (i = 0; i < count; ++i) {
        XObject* const* children =
            (XObject* const*)XContainerDataAddr(object->m_children);
        if (children[i] && children[i]->m_is_widget &&
            XClassGetVtable(children[i]) == XMenu_class_init())
            return (XMenu*)children[i];
    }
    return NULL;
}
#endif /* XMENU_ON */

/** @brief 对话框信号/点击计数（用例间复位）。 */
typedef struct XDialogAlignSig
{
    int accepted;      /**< accepted() 次数。 */
    int rejected;      /**< rejected() 次数。 */
    int finished;      /**< finished() 次数。 */
    int lastFinished;  /**< 最近 finished 载荷。 */
    int clicks;        /**< 默认按钮被点击次数。 */
    char order[8];     /**< 发射顺序（A/R/F）。 */
    int orderLen;      /**< 顺序缓冲已用长度。 */
} XDialogAlignSig;

static XDialogAlignSig g_xdlgSig;

/** @brief 清空计数（每用例起始调用）。 */
static void xdlgSigReset(void)
{
    memset(&g_xdlgSig, 0, sizeof(g_xdlgSig));
}

static void xdlgAcceptedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_xdlgSig.accepted;
    if (g_xdlgSig.orderLen < (int)sizeof(g_xdlgSig.order) - 1)
        g_xdlgSig.order[g_xdlgSig.orderLen++] = 'A';
}

static void xdlgRejectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_xdlgSig.rejected;
    if (g_xdlgSig.orderLen < (int)sizeof(g_xdlgSig.order) - 1)
        g_xdlgSig.order[g_xdlgSig.orderLen++] = 'R';
}

static void xdlgFinishedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, result);
    ++g_xdlgSig.finished;
    g_xdlgSig.lastFinished = result;
    if (g_xdlgSig.orderLen < (int)sizeof(g_xdlgSig.order) - 1)
        g_xdlgSig.order[g_xdlgSig.orderLen++] = 'F';
}

static void xdlgClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_xdlgSig.clicks;
}

/** @brief 向控件直发一次键盘事件（同真实输入经 event 分派到虚槽）。 */
static void xdlgKey(XWidget* target, int key, XKeyboardModifiers mods)
{
    XKeyEvent kev;
    XKeyEvent_init(&kev, XEVENT_TYPE_KEY_PRESS, key, mods);
    XObject_event_base((XObject*)target, (XEvent*)&kev);
}

/** @brief 把对话框的三个信号接到计数槽。 */
static void xdlgConnectSignals(XDialog* dlg)
{
    XObject_connect((XObject*)dlg, XSignal(XDialog_accepted_signal),
                      (XObject*)dlg, xdlgAcceptedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)dlg, XSignal(XDialog_rejected_signal),
                      (XObject*)dlg, xdlgRejectedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)dlg, XSignal(XDialog_finished_signal),
                      (XObject*)dlg, xdlgFinishedSlot,
                      XConnectionType_Direct);
}

/* ==================== ① 构造默认值 + 窗口类型 ==================== */

XQ_TEST(XDialog, constructorDefaults)
{
    XDialog* dlg = XDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);

    /* qdialog.cpp:366-379：flags 不含窗口类型位时自动叠加 Dialog。 */
    XQ_CHECK(XWidget_isWindow((XWidget*)dlg));
    XQ_EQ_I64(XDialog_result(dlg), 0);
    XQ_CHECK(!XDialog_isModal(dlg));
    XQ_EQ_I64(XWidget_windowModality((XWidget*)dlg),
              XWindowModality_NonModal);
    XQ_CHECK(!XDialog_isSizeGripEnabled(dlg));
    /* 本库顶层缺省几何（XDialog_init 注：带父也不落成 100×30 空壳）。 */
    XQ_EQ_I64(XWidget_width((XWidget*)dlg), 640);
    XQ_EQ_I64(XWidget_height((XWidget*)dlg), 480);

    /* setResult 原样存取任意 int（qdialog.h:44）。 */
    XDialog_setResult(dlg, 7);
    XQ_EQ_I64(XDialog_result(dlg), 7);
    XDialog_setResult(dlg, 0);

    /* NULL 安全。 */
    XQ_EQ_I64(XDialog_result(NULL), 0);
    XQ_CHECK(!XDialog_isModal(NULL));
    XQ_CHECK(!XDialog_isSizeGripEnabled(NULL));

    XClassDelete(dlg);
}

/* ==================== ② modal 属性 ↔ 窗口模态 ==================== */

XQ_TEST(XDialog, modalFlagRoundTrip)
{
    XDialog* dlg = XDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);

    /* 本机 Qt 6.8 实测 D1：setModal(true) → isModal=1、modality=2。 */
    XDialog_setModal(dlg, true);
    XQ_CHECK(XDialog_isModal(dlg));
    XQ_EQ_I64(XWidget_windowModality((XWidget*)dlg),
              XWindowModality_ApplicationModal);

    /* 实测 D2：setModal(false) → isModal=0、modality=0（Qt 清
     * WA_ShowModal 时同步清窗口模态）。 */
    XDialog_setModal(dlg, false);
    XQ_CHECK(!XDialog_isModal(dlg));
    XQ_EQ_I64(XWidget_windowModality((XWidget*)dlg),
              XWindowModality_NonModal);

    /* 幂等：重复置同值不改变状态。 */
    XDialog_setModal(dlg, false);
    XQ_EQ_I64(XWidget_windowModality((XWidget*)dlg),
              XWindowModality_NonModal);

    XClassDelete(dlg);
}

/* ==================== ③ done/accept/reject 结果与信号 ==================== */

XQ_TEST(XDialog, doneResultAndSignals)
{
    XDialog* dlg = XDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);
    xdlgSigReset();
    xdlgConnectSignals(dlg);

    /* 实测 D13：done(3) 只发 finished(3)（3 既非 Accepted 也非
     * Rejected，不映射 accepted/rejected；qdialog.cpp:614-621）。 */
    XWidget_show((XWidget*)dlg);
    XDialog_done(dlg, 3);
    XQ_EQ_I64(XDialog_result(dlg), 3);
    XQ_EQ_I64(g_xdlgSig.finished, 1);
    XQ_EQ_I64(g_xdlgSig.lastFinished, 3);
    XQ_EQ_I64(g_xdlgSig.accepted, 0);
    XQ_EQ_I64(g_xdlgSig.rejected, 0);
    XQ_CHECK(!XWidget_isVisible((XWidget*)dlg));

    /* 实测 D14：accept → 先 accepted 后 finished(1)。 */
    xdlgSigReset();
    XWidget_show((XWidget*)dlg);
    XDialog_accept(dlg);
    XQ_EQ_I64(XDialog_result(dlg), XDialogCode_Accepted);
    XQ_EQ_I64(g_xdlgSig.accepted, 1);
    XQ_EQ_I64(g_xdlgSig.finished, 1);
    XQ_EQ_I64(g_xdlgSig.lastFinished, XDialogCode_Accepted);
    XQ_EQ_I64(g_xdlgSig.orderLen, 2);
    XQ_CHECK(g_xdlgSig.order[0] == 'A' && g_xdlgSig.order[1] == 'F');

    /* reject → 先 rejected 后 finished(0)。 */
    xdlgSigReset();
    XWidget_show((XWidget*)dlg);
    XDialog_reject(dlg);
    XQ_EQ_I64(XDialog_result(dlg), XDialogCode_Rejected);
    XQ_EQ_I64(g_xdlgSig.rejected, 1);
    XQ_EQ_I64(g_xdlgSig.finished, 1);
    XQ_CHECK(g_xdlgSig.order[0] == 'R' && g_xdlgSig.order[1] == 'F');

    /* hide 不发 finished（qdialog.cpp:1079-1082 文档）。 */
    xdlgSigReset();
    XWidget_show((XWidget*)dlg);
    XWidget_hide((XWidget*)dlg);
    XQ_EQ_I64(g_xdlgSig.finished, 0);

    XClassDelete(dlg);
}

/* ==================== ④ 键盘：Esc / Enter 的修饰键门 ==================== */

XQ_TEST(XDialog, keyPressModifiers)
{
    XDialog* dlg = XDialog_create(NULL, 0);
    XPushButton* ok;
    XQ_REQUIRE(dlg != NULL);
    xdlgSigReset();
    xdlgConnectSignals(dlg);
    ok = XPushButton_create((XWidget*)dlg, 0);
    XQ_REQUIRE(ok != NULL);
    XPushButton_setText_2(ok, "OK");
    XWidget_setGeometry((XWidget*)ok, 12, 12, 80, 28);
    XPushButton_setDefault(ok, true);
    XObject_connect((XObject*)ok, XSignal(XAbstractButton_clicked_signal),
                      (XObject*)ok, xdlgClickedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)dlg);

    /* 实测 D6：Ctrl+Return 不点击默认按钮（qdialog.cpp:691 修饰键门）。 */
    xdlgKey((XWidget*)dlg, (int)XKey_Return,
            XKeyboardModifier_ControlModifier);
    XQ_EQ_I64(g_xdlgSig.clicks, 0);

    /* 实测 D7：无修饰 Return 点击默认按钮。 */
    xdlgKey((XWidget*)dlg, (int)XKey_Return, XKeyboardModifier_NoModifier);
    XQ_EQ_I64(g_xdlgSig.clicks, 1);

    /* 实测 D8：小键盘 Enter 也点击。 */
    xdlgKey((XWidget*)dlg, (int)XKey_Enter, XKeyboardModifier_KeypadModifier);
    XQ_EQ_I64(g_xdlgSig.clicks, 2);

    /* 实测 D9：Esc+Shift 不关窗（QKeySequence::Cancel 精确匹配）。 */
    xdlgKey((XWidget*)dlg, (int)XKey_Escape, XKeyboardModifier_ShiftModifier);
    XQ_EQ_I64(g_xdlgSig.rejected, 0);
    XQ_CHECK(XWidget_isVisible((XWidget*)dlg));

    /* 实测 D10：无修饰 Esc → reject。 */
    xdlgKey((XWidget*)dlg, (int)XKey_Escape, XKeyboardModifier_NoModifier);
    XQ_EQ_I64(g_xdlgSig.rejected, 1);
    XQ_CHECK(!XWidget_isVisible((XWidget*)dlg));

    XClassDelete(dlg);
}

/* ==================== ⑤ 尺寸手柄真嵌入 ==================== */

XQ_TEST(XDialog, sizeGripEmbedsChild)
{
    XDialog* dlg = XDialog_create(NULL, 0);
    XQ_REQUIRE(dlg != NULL);
    XWidget_resize((XWidget*)dlg, 400, 300);

#if XSIZEGRIP_ON
    {
        XSizeGrip* grip = NULL;
        XObject* object;
        int i;
        int count;

        /* 实测 D3：Qt setSizeGripEnabled(true) 创建 1 个 QSizeGrip 子控件。 */
        XDialog_setSizeGripEnabled(dlg, true);
        XQ_CHECK(XDialog_isSizeGripEnabled(dlg));
        object = (XObject*)dlg;
        count = object->m_children
                    ? XVector_size_base((const XContainer*)object->m_children)
                    : 0;
        for (i = 0; i < count; ++i) {
            XObject* const* children =
                (XObject* const*)XContainerDataAddr(object->m_children);
            if (children[i] && children[i]->m_is_widget &&
                XClassGetVtable(children[i]) == XSizeGrip_class_init()) {
                grip = (XSizeGrip*)children[i];
                break;
            }
        }
        XQ_CHECK(grip != NULL);
        if (grip) {
            /* qdialog.cpp:1052-1066：贴 rect().bottomRight()。 */
            XQ_EQ_I64(XWidget_x((XWidget*)grip),
                      XWidget_width((XWidget*)dlg) -
                          XWidget_width((XWidget*)grip));
            XQ_EQ_I64(XWidget_y((XWidget*)grip),
                      XWidget_height((XWidget*)dlg) -
                          XWidget_height((XWidget*)grip));
            /* resize 后重摆（同源公式）。 */
            XWidget_resize((XWidget*)dlg, 500, 360);
            XQ_EQ_I64(XWidget_x((XWidget*)grip),
                      XWidget_width((XWidget*)dlg) -
                          XWidget_width((XWidget*)grip));
            XQ_EQ_I64(XWidget_y((XWidget*)grip),
                      XWidget_height((XWidget*)dlg) -
                          XWidget_height((XWidget*)grip));
        }
        /* 实测 D4：关闭即删除手柄子控件。 */
        XDialog_setSizeGripEnabled(dlg, false);
        XQ_CHECK(!XDialog_isSizeGripEnabled(dlg));
    }
#else
    /* 裁剪构建：仅存位（XSIZEGRIP_ON=0）。 */
    XDialog_setSizeGripEnabled(dlg, true);
    XQ_CHECK(XDialog_isSizeGripEnabled(dlg));
    XDialog_setSizeGripEnabled(dlg, false);
    XQ_CHECK(!XDialog_isSizeGripEnabled(dlg));
#endif

    XClassDelete(dlg);
}

/* ==================== ⑦ What's This 帮助菜单（contextMenuEvent） ======= */

XQ_TEST(XDialog, contextMenuWhatsThisMenu)
{
#if XMENU_ON
    XDialog* dlg = XDialog_create(NULL, 0);
    XWidget* target;
    XDialogHelpProbe* probe;
    XString* wt;
    XContextMenuEvent ev;
    XPoint pos;
    XPoint global;
    XMenu* menu;
    XRect trect;
    const XVector* actions;
    XQ_REQUIRE(dlg != NULL);

    /* 命中控件带 whatsThis（qdialog.cpp:654-680 沿父链找到的第一个
     * whatsThis 非空控件即为帮助事件接收者）。 */
    target = XWidget_create((XWidget*)dlg, 0);
    XQ_REQUIRE(target != NULL);
    XWidget_setGeometry(target, 10, 40, 80, 30);
    wt = XString_create_utf8("帮助文本");
    XQ_REQUIRE(wt != NULL);
    XWidget_setWhatsThis(target, wt);
    XClassDelete((XClass*)wt);
    probe = XDialogHelpProbe_create();
    XQ_REQUIRE(probe != NULL);
    XObject_installEventFilter((XObject*)target, (XObject*)probe);
    XWidget_show((XWidget*)dlg);
    /* CSD 内容避让（XDialog.c xdlg_applyContentAvoidance，显式几何路径）
     * 在 show 时把直接子控件整体下移装饰条高，不能用 show 前的设定坐标
     * 命中；按 show 后的实际控件矩形取中心（与 QDialog::contextMenuEvent
     * 的 childAt(e->pos()) 同口径）。 */
    trect = XWidget_geometry(target);   /* 父坐标系，childAt 同系 */
    pos.x = trect.x + trect.width / 2;
    pos.y = trect.y + trect.height / 2;
    global.x = 200; global.y = 300;
    XContextMenuEvent_init(&ev, XEVENT_TYPE_CONTEXT_MENU, &pos, &global,
                           XContextMenuReason_Mouse,
                           XKeyboardModifier_NoModifier);
    XObject_event_base((XObject*)dlg, (XEvent*)&ev);

    menu = xdlg_findChildMenu(dlg);
    XQ_CHECK_PTR(menu);
    if (menu) {
        XQ_CHECK(XWidget_isVisible((XWidget*)menu));
        actions = XMenu_actions(menu);
        XQ_REQUIRE(actions != NULL);
        XQ_EQ_SIZE(XVector_size_base((const XContainer*)actions), 1);
        {
            XAction* const* first =
                (XAction* const*)XContainerDataAddr((XContainer*)actions);
            XString* text;
            XQ_REQUIRE(first != NULL && first[0] != NULL);
            text = XAction_text(first[0]);
            if (text) {
                const char* utf8 = XString_toUtf8(text);
                XQ_EQ_STR(utf8 ? utf8 : "", "What's This?");
                XClassDelete((XClass*)text);
            }
            /* 触发菜单项：对话框把 XEVENT_TYPE_WHATS_THIS 帮助事件投递给
             * 命中控件（qdialog.cpp:674-678 sendEvent(QHelpEvent)）；
             * 过滤器探针计得 1 次。菜单的关闭由交互路径负责（本用例
             * 直接 XAction_trigger，不模拟鼠标释放）。 */
            XAction_trigger(first[0]);
            XQ_EQ_I64(probe->m_whatsThis, 1);
            XWidget_hide((XWidget*)menu);
        }
    }
    XClassDelete((XClass*)probe);
    XClassDelete(dlg);
#else
    (void)0;
#endif
}

XQ_TEST(XDialog, contextMenuWithoutWhatsThisShowsNothing)
{
#if XMENU_ON
    XDialog* dlg = XDialog_create(NULL, 0);
    XWidget* child;
    XContextMenuEvent ev;
    XPoint pos;
    XPoint global;
    XQ_REQUIRE(dlg != NULL);
    child = XWidget_create((XWidget*)dlg, 0);
    XQ_REQUIRE(child != NULL);
    XWidget_setGeometry(child, 10, 10, 80, 30);
    XWidget_show((XWidget*)dlg);
    pos.x = 20; pos.y = 20;
    global.x = 200; global.y = 300;
    XContextMenuEvent_init(&ev, XEVENT_TYPE_CONTEXT_MENU, &pos, &global,
                           XContextMenuReason_Mouse,
                           XKeyboardModifier_NoModifier);
    XObject_event_base((XObject*)dlg, (XEvent*)&ev);
    /* 无 whatsThis 目标：不弹菜单（qdialog.cpp:668-670 `if (!w) return`）。 */
    XQ_CHECK(xdlg_findChildMenu(dlg) == NULL);
    XClassDelete(dlg);
#else
    (void)0;
#endif
}

/* ==================== ⑥ 父子所有权 ==================== */

XQ_TEST(XDialog, parentOwnership)
{
    XWidget* parent = XWidget_create(NULL, 0);
    XDialog* dlg;
    XPushButton* button;
    int parentWidth;
    XQ_REQUIRE(parent != NULL);
    parentWidth = XWidget_width(parent);
    dlg = XDialog_create(parent, 0);
    XQ_REQUIRE(dlg != NULL);

    /* parent 为借用：对话框仍归调用方（顶层窗口语义）。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)dlg) == parent);
    button = XPushButton_create((XWidget*)dlg, 0);
    XQ_REQUIRE(button != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)button) == (XWidget*)dlg);

    /* 删除对话框即带走其子按钮（父析构带走子对象；泄漏门复核）。 */
    XClassDelete(dlg);

    /* 父控件仍可用且几何未被对话框改动。 */
    XQ_EQ_I64(XWidget_width(parent), parentWidth);
    XClassDelete(parent);
}

#endif /* XGUI_ON && XWIDGET_ON && XDIALOG_ON */
