/* xgui_demo_apitest_dialogs.c —— 控件 API 测试族：dialogs。
 *
 * 覆盖控件（对标 Qt 6.8.3）：XDialog（QDialog）/ XDialogButtonBox
 * （QDialogButtonBox）/ XMessageBox（QMessageBox）/ XInputDialog
 * （QInputDialog）/ XFileDialog（QFileDialog）/ XColorDialog
 * （QColorDialog）/ XProgressDialog（QProgressDialog）/ XWizard +
 * XWizardPage（QWizard + QWizardPage）。
 *
 * 测试口径（见 xgui_demo_apitest.h 契约）：
 *  - 属性 setter/getter 往返一致；Qt 6.8.3 文档默认值确定的直接断言，
 *    出处不确定的写注释不硬断言（防误报）；语义以本库头文件注释为准，
 *    与 Qt 冲突处在断言注释与本文件尾注中如实记录；
 *  - 信号断言：XDialog族经 XObject_event_base 直发合成键盘事件（与真实
 *    输入同路径）或 XAbstractButton_click 程序化点击（clicked→角色信号
 *    链路），直发型信号按头文件注明"手动触发供测试"口径直调信号函数；
 *  - 无头语义：控件不 show 也可调绝大多数 API；全程不调用 exec() 与
 *    information/warning/critical/question/about/getText/getInt/
 *    getDouble/getItem/getOpenFileName/getSaveFileName/getColor 等静态
 *    便捷函数——demo 以 --apitest 运行时存在 XCoreApplication 实例，这
 *    些入口会进入模态事件循环阻塞测试进程（见 XDialog_exec 的
 *    WaitForMoreEvents 循环与 XInputDialog.h @note）；
 *  - 视觉边界：只断言 API 状态与几何/可见性事实，渲染效果由主线亲验。
 *
 * 【Qt 对齐偏差备忘（详见各断言注释与提交 notes）】
 *  - XDialog 默认 modal=true（Qt QDialog::modal() 读数 false，本库把
 *    exec 模态承载为属性默认 true，XDialog.h 注明）；
 *  - XFileDialog 默认 fileMode=ExistingFile（Qt 默认 AnyFile）；
 *  - XColorDialog 构造即把 initial 写入 selectedColor（Qt 中接受前为
 *    无效色）；customCount 按已设槽计数（Qt QColorDialog::customCount
 *    恒为 16）；
 *  - XProgressDialog::setRange min>max 时交换（Qt setRange 实为把 max
 *    钳到 min；本库头文件注明"交换（Qt 语义）"，两处口径不一致，按头
 *    文件断言并记 notes）；
 *  - XInputDialog 整数范围默认 INT_MIN..INT_MAX 与 Qt 文档一致；浮点
 *    范围默认 ±1e308 为本库头文件注明的对齐值（Qt 文档未给同形出处，
 *    按头文件断言）；
 *  - XWizard titleFormat/subTitleFormat 默认 0=PlainText（Qt 默认
 *    AutoText=2）；QWizard 帮助按钮仅在 init 时预设 HaveHelpButton 才
 *    创建，本库 init 固定 options=0 后再建按钮，等效于运行期永不创建
 *    （button(HelpButton) 恒 NULL，属文档化简化）。
 */
#include "xgui_demo_apitest.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "XObject.h"
#include "XEvent.h"
#include "XVarList.h"

/* 容器头自带 X*_ON 内部门禁，无条件包含安全。 */
#include "XString.h"
#include "XStringList.h"
#include "XVector.h"
#include "XByteArray.h"

#if XGUIAPPLICATION_ON
#include "XApplication.h"
#endif
#if XWIDGET_ON
#include "XWidget.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON
#include "XAbstractButton.h"
#endif
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
#include "XPushButton.h"
#endif
#if XWIDGET_ON && XDIALOG_ON
#include "XDialog.h"
#endif
#if XWIDGET_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON
#include "XDialogButtonBox.h"
#endif
#if XWIDGET_ON && XPROGRESSBAR_ON
#include "XProgressBar.h"
#endif
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
#include "XLabel.h"
#endif
#if XWIDGET_ON && XDIALOG_ON && XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON && XMESSAGEBOX_ON
#include "XMessageBox.h"
#if XCHECKBOX_ON
#include "XCheckBox.h"
#endif
#include "XImage.h"
#include "XIcon.h"
#endif
#if XWIDGET_ON && XDIALOG_ON
#include "XInputDialog.h"
#include "XFileDialog.h"
#include "XColorDialog.h"
#include "XProgressDialog.h"
#endif
#if XWIDGET_ON && XDIALOG_ON && XWIZARD_ON
#include "XWizard.h"
#endif

#if XWIDGET_ON && XDIALOG_ON

/* 按钮型槽位载荷在 XABSTRACTBUTTON_ON=0 的裁剪配置下仍可声明为不透明
 * 指针（对应小节已被裁剪，不参与断言）。 */
struct XAbstractButton;

/* ==================== 信号记录器（对标 QSignalSpy 的最小等价物） ==================== */

/** @brief 对话框族信号计数与最近载荷（跨小节复用，节首 reset）。 */
typedef struct DlgSigRec
{
    int accepted;              /**< XDialog::accepted 次数。 */
    int rejected;              /**< XDialog::rejected 次数。 */
    int finished;              /**< XDialog::finished 次数。 */
    int lastFinishedResult;    /**< 最近一次 finished(result) 载荷。 */
    char order[8];             /**< XDialog 发射顺序（'F'/'A'/'R'）。 */
    int orderLen;              /**< order 已写入长度。 */
    int boxAccepted;           /**< XDialogButtonBox::accepted 次数。 */
    int boxRejected;           /**< XDialogButtonBox::rejected 次数。 */
    int boxHelp;               /**< XDialogButtonBox::helpRequested 次数。 */
    int boxClicked;            /**< XDialogButtonBox::clicked(button) 次数。 */
    const XAbstractButton* lastBoxClicked; /**< 最近一次盒 clicked 载荷按钮。 */
    int msgButtonClicked;      /**< XMessageBox::buttonClicked 次数。 */
    const XAbstractButton* lastMsgButton;  /**< 最近一次消息框 buttonClicked 载荷。 */
    int currentIdChanged;      /**< XWizard::currentIdChanged 次数。 */
    int lastCurrentId;         /**< 最近一次 currentIdChanged(index) 载荷。 */
    int pageAdded;             /**< XWizard::pageAdded 次数。 */
    int pageRemoved;           /**< XWizard::pageRemoved 次数。 */
    int canceled;              /**< XProgressDialog::canceled 次数。 */
    int textValueChanged;      /**< XInputDialog::textValueChanged 次数。 */
    char lastTextValue[64];    /**< 最近一次 textValueChanged 文本载荷。 */
    int intValueChanged;       /**< XInputDialog::intValueChanged 次数。 */
    int lastIntValue;          /**< 最近一次 intValueChanged 载荷。 */
    int doubleValueChanged;    /**< XInputDialog::doubleValueChanged 次数。 */
    double lastDoubleValue;    /**< 最近一次 doubleValueChanged 载荷。 */
    int completeChanged;       /**< XWizardPage::completeChanged 次数。 */
    int fileSelected;          /**< XFileDialog::fileSelected 次数。 */
    int filesSelected;         /**< XFileDialog::filesSelected 次数。 */
    int currentChanged;        /**< XFileDialog::currentChanged 次数。 */
    int directoryEntered;      /**< XFileDialog::directoryEntered 次数。 */
    int filterSelected;        /**< XFileDialog::filterSelected 次数。 */
    int currentColorChanged;   /**< XColorDialog::currentColorChanged 次数。 */
    XColor lastColor;          /**< 最近一次颜色信号载荷。 */
    int colorSelected;         /**< XColorDialog::colorSelected 次数。 */
} DlgSigRec;

static DlgSigRec g_dlgSig;

static void dlg_sig_reset(void)
{
    memset(&g_dlgSig, 0, sizeof(g_dlgSig));
}

/** @brief 信号载荷 XString* 借用拷贝进记录器（载荷由发射方释放）。 */
static void dlg_sig_storeText(const XString* text)
{
    const char* utf8 = text ? xapi_u8(text) : NULL;
    if (!utf8) utf8 = "";
    snprintf(g_dlgSig.lastTextValue, sizeof(g_dlgSig.lastTextValue), "%s",
             utf8);
}

/* ==================== 槽函数（void f(XObject*, XVarList*) 定式） ==================== */

static void dlg_acceptedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.accepted;
    if (g_dlgSig.orderLen < (int)sizeof(g_dlgSig.order) - 1)
        g_dlgSig.order[g_dlgSig.orderLen++] = 'A';
}

static void dlg_rejectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.rejected;
    if (g_dlgSig.orderLen < (int)sizeof(g_dlgSig.order) - 1)
        g_dlgSig.order[g_dlgSig.orderLen++] = 'R';
}

static void dlg_finishedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, result);
    ++g_dlgSig.finished;
    g_dlgSig.lastFinishedResult = result;
    if (g_dlgSig.orderLen < (int)sizeof(g_dlgSig.order) - 1)
        g_dlgSig.order[g_dlgSig.orderLen++] = 'F';
}

static void dlg_boxAcceptedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.boxAccepted;
}

static void dlg_boxRejectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.boxRejected;
}

static void dlg_boxHelpSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.boxHelp;
}

static void dlg_boxClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XAbstractButton*, button);
    ++g_dlgSig.boxClicked;
    g_dlgSig.lastBoxClicked = button;
}

static void dlg_msgButtonClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XAbstractButton*, button);
    ++g_dlgSig.msgButtonClicked;
    g_dlgSig.lastMsgButton = button;
}

static void dlg_currentIdChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, index);
    ++g_dlgSig.currentIdChanged;
    g_dlgSig.lastCurrentId = index;
}

static void dlg_pageAddedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.pageAdded;
}

static void dlg_pageRemovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.pageRemoved;
}

static void dlg_canceledSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.canceled;
}

static void dlg_textValueChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XString*, text);
    ++g_dlgSig.textValueChanged;
    dlg_sig_storeText(text);
}

static void dlg_intValueChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, value);
    ++g_dlgSig.intValueChanged;
    g_dlgSig.lastIntValue = value;
}

static void dlg_doubleValueChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, double, value);
    ++g_dlgSig.doubleValueChanged;
    g_dlgSig.lastDoubleValue = value;
}

static void dlg_completeChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.completeChanged;
}

static void dlg_fileSelectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args; /* XString* 载荷由发射方 del 回调释放。 */
    ++g_dlgSig.fileSelected;
}

static void dlg_filesSelectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args; /* XStringList* 载荷由发射方 del 回调释放。 */
    ++g_dlgSig.filesSelected;
}

static void dlg_currentChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.currentChanged;
}

static void dlg_directoryEnteredSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.directoryEntered;
}

static void dlg_filterSelectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_dlgSig.filterSelected;
}

static void dlg_colorChangedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XColor, color);
    ++g_dlgSig.currentColorChanged;
    g_dlgSig.lastColor = color;
}

static void dlg_colorSelectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, XColor, color);
    ++g_dlgSig.colorSelected;
    g_dlgSig.lastColor = color;
}

/* ==================== 小工具 ==================== */

/** @brief 键盘按下合成事件直发（与真实输入同路径；对标
 *         QDialog::keyPressEvent 等按键处理，经 XObject_event_base）。 */
static void dlg_injectKeyPress(XWidget* target, int key)
{
    XKeyEvent kev;
    XKeyEvent_init(&kev, XEVENT_TYPE_KEY_PRESS, key, 0);
    XObject_event_base((XObject*)target, (XEvent*)&kev);
}

/** @brief 取 XString 副本内容并释放副本，与期望串比对。 */
static bool dlg_str_eq_take(XString* s, const char* want)
{
    bool eq;
    if (!s) return want && want[0] == '\0';
    eq = (want != NULL) && (strcmp(xapi_u8(s), want) == 0);
    XString_delete_base((XClass*)s);
    return eq;
}

/** @brief 构造单元素 XStringList（元素为 XString 值存储，push 后释放源）。 */
static XStringList* dlg_make_list1(const char* a)
{
    XStringList* list = XStringList_create();
    XString* s = a ? XString_create_utf8(a) : XString_create();
    if (list && s) XStringList_push_back_move_base((XVector*)list, s);
    if (s) XString_delete_base((XClass*)s);
    return list;
}

/** @brief 构造两元素 XStringList。 */
static XStringList* dlg_make_list2(const char* a, const char* b)
{
    XStringList* list = XStringList_create();
    XString* s;
    s = a ? XString_create_utf8(a) : XString_create();
    if (list && s) XStringList_push_back_move_base((XVector*)list, s);
    if (s) XString_delete_base((XClass*)s);
    s = b ? XString_create_utf8(b) : XString_create();
    if (list && s) XStringList_push_back_move_base((XVector*)list, s);
    if (s) XString_delete_base((XClass*)s);
    return list;
}

/** @brief 列表元素数（空表安全）。 */
static int dlg_list_count(const XStringList* list)
{
    if (!list) return -1;
    return (int)XStringList_size_base((const XContainer*)list);
}

/** @brief 列表第 i 个元素内容与期望串比对（空表/越界安全）。 */
static bool dlg_list_at_eq(const XStringList* list, int i, const char* want)
{
    XString* e;
    if (!list || i < 0 ||
        i >= (int)XStringList_size_base((const XContainer*)list)) {
        return (want == NULL);
    }
    e = (XString*)XStringList_at_base((const XVector*)list, (int64_t)i);
    return e && (strcmp(xapi_u8(e), want) == 0);
}

#endif /* XWIDGET_ON && XDIALOG_ON */

/* ==================== 入口 ==================== */

int xapi_dialogs_run(void)
{
    int failures = 0;

#if XWIDGET_ON && XDIALOG_ON

    /* ================================================================
     * 1. XDialog（对标 QDialog）：modal/result/accept/reject/done/open
     *    与 Escape→reject 键盘路径。
     *    注：exec() 为阻塞事件循环（XDialog_exec 内 WaitForMoreEvents
     *    自旋直到 done()），无头测试不调用；其模态语义由回归/autotest
     *    覆盖。
     * ================================================================ */
    {
        XDialog* dlg = XDialog_create(NULL, 0);
        XKeyEvent kev;
        if (!dlg) {
            XAPI_EXPECT(0, "XDialog 堆构造成功");
        } else {
            dlg_sig_reset();

            /* ---- 默认值（Qt 6.8 QDialog 构造默认） ---- */
            XAPI_EXPECT(!XDialog_isModal(dlg),
                        "XDialog 默认 modal=false（P2 批次对齐 Qt QDialog::modal 默认）");
            XAPI_EXPECT(XDialog_result(dlg) == 0,
                        "XDialog 默认 result=0（对标 QDialog 构造后 result() 为 0）");
            XAPI_EXPECT(!XDialog_isSizeGripEnabled(dlg),
                        "XDialog 默认 sizeGripEnabled=false（对标 QDialog::sizeGripEnabled 默认 false）");

            /* ---- 属性 setter/getter 往返 ---- */
            XDialog_setResult(dlg, 7);
            XAPI_EXPECT(XDialog_result(dlg) == 7,
                        "setResult(7)→result()==7（对标 QDialog::setResult/result 往返）");
            XDialog_setResult(dlg, 0);
            XDialog_setModal(dlg, false);
            XAPI_EXPECT(!XDialog_isModal(dlg),
                        "setModal(false)→isModal()==false（对标 QDialog::setModal/isModal 往返）");
            XDialog_setModal(dlg, true);
            XAPI_EXPECT(XDialog_isModal(dlg),
                        "setModal(true) 回读往返一致（对标 QDialog::setModal）");
            XDialog_setSizeGripEnabled(dlg, true);
            XAPI_EXPECT(XDialog_isSizeGripEnabled(dlg),
                        "setSizeGripEnabled(true) 回读（对标 QDialog::setSizeGripEnabled；本库仅存储位，头文件注明不自动嵌手柄）");
            XDialog_setSizeGripEnabled(dlg, false);

            /* ---- 信号装配（sender==receiver 自连定式） ---- */
            XObject_connect_1((XObject*)dlg,
                              XSignal(XDialog_accepted_signal),
                              (XObject*)dlg, dlg_acceptedSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)dlg,
                              XSignal(XDialog_rejected_signal),
                              (XObject*)dlg, dlg_rejectedSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)dlg,
                              XSignal(XDialog_finished_signal),
                              (XObject*)dlg, dlg_finishedSlot,
                              XConnectionType_Direct);

            /* ---- accept：result=Accepted(1) + accepted/finished ---- */
            XWidget_show((XWidget*)dlg);
            XDialog_accept(dlg);
            XAPI_EXPECT(XDialog_result(dlg) == 1,
                        "accept() 后 result==1（对标 QDialog::Accepted==1）");
            XAPI_EXPECT(g_dlgSig.accepted == 1 && g_dlgSig.finished == 1 &&
                        g_dlgSig.lastFinishedResult == 1,
                        "accept() 发射 accepted + finished(1)（对标 QDialog::accept 的信号组）");
            XAPI_EXPECT(!XWidget_isVisible((XWidget*)dlg),
                        "accept() 后对话框隐藏（对标 QDialog::done 内部 hide）");
            XAPI_EXPECT(g_dlgSig.orderLen == 2 &&
                        g_dlgSig.order[0] == 'F' && g_dlgSig.order[1] == 'A',
                        "accept 发射顺序 finished 先于 accepted（对标 QDialog::done 先 finished 再 accepted 的时序）");

            /* ---- reject：result=0 + rejected/finished(0) ---- */
            dlg_sig_reset();
            XWidget_show((XWidget*)dlg);
            XDialog_reject(dlg);
            XAPI_EXPECT(XDialog_result(dlg) == 0 && g_dlgSig.rejected == 1 &&
                        g_dlgSig.lastFinishedResult == 0 &&
                        g_dlgSig.finished == 1,
                        "reject() 后 result==0 并发射 rejected + finished(0)（对标 QDialog::reject）");

            /* ---- done(自定义码)：只发 finished，隐藏 ---- */
            dlg_sig_reset();
            XWidget_show((XWidget*)dlg);
            XDialog_done(dlg, 3);
            XAPI_EXPECT(XDialog_result(dlg) == 3 && g_dlgSig.finished == 1 &&
                        g_dlgSig.lastFinishedResult == 3 &&
                        g_dlgSig.accepted == 0 && g_dlgSig.rejected == 0,
                        "done(3) 设自定义结果码且只发射 finished(3)（对标 QDialog::done(r)）");
            XAPI_EXPECT(!XWidget_isVisible((XWidget*)dlg),
                        "done() 后对话框隐藏（对标 QDialog::done → hide）");

            /* ---- open：显示 + 置模态，不进本地事件循环 ---- */
            XDialog_open(dlg);
            XAPI_EXPECT(XWidget_isVisible((XWidget*)dlg) &&
                        XDialog_isModal(dlg),
                        "open() 显示并保持窗口模态（对标 QDialog::open 显示+窗口模态、XGui 无嵌套 exec）");
            XDialog_reject(dlg); /* 收起并解除模态登记。 */

            /* ---- Escape 键 → reject（合成键盘事件直发） ---- */
            dlg_sig_reset();
            XWidget_show((XWidget*)dlg);
            XKeyEvent_init(&kev, XEVENT_TYPE_KEY_PRESS, (int)XKey_Escape, 0);
            XObject_event_base((XObject*)dlg, (XEvent*)&kev);
            XAPI_EXPECT(XDialog_result(dlg) == 0 && g_dlgSig.rejected == 1 &&
                        !XWidget_isVisible((XWidget*)dlg),
                        "Escape 键触发 reject（对标 QDialog::keyPressEvent 的 Esc→reject，XObject_event_base 直发同真实输入）");

            /* ---- 非 Esc 按键不关窗（边界） ---- */
            XWidget_show((XWidget*)dlg);
            XKeyEvent_init(&kev, XEVENT_TYPE_KEY_PRESS, (int)XKey_A, 0);
            XObject_event_base((XObject*)dlg, (XEvent*)&kev);
            XAPI_EXPECT(g_dlgSig.rejected == 1 &&
                        XWidget_isVisible((XWidget*)dlg),
                        "普通按键不触发 reject（对标 QDialog 非 Esc 分支 ignore 沿父链传播）");
            XDialog_reject(dlg); /* 收尾，解除模态登记。 */

            /* ---- NULL 边界（文档化 NULL 安全） ---- */
            XAPI_EXPECT(XDialog_result(NULL) == 0 && !XDialog_isModal(NULL) &&
                        !XDialog_isSizeGripEnabled(NULL),
                        "NULL 入参 getter 安全（文档化：result/isModal/isSizeGripEnabled(NULL) 返回 0/false）");

            XDialog_delete_base(dlg);
        }
    }

    /* ---- exec()/静态便捷入口：模态阻塞不在此测（见文件头口径）。
     * 对标 QDialog::exec 的返回值语义由 XDialog_exec 实现自证：
     * while(m_inExec) 循环 + return m_result，与 Qt 语义一致。 ---- */

#endif /* XWIDGET_ON && XDIALOG_ON */

#if XWIDGET_ON && XDIALOG_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON

    /* ================================================================
     * 2. XDialogButtonBox（对标 QDialogButtonBox）：角色/标准按钮/
     *    成员管理与角色→信号中继。
     * ================================================================ */
    {
        XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
        XPushButton* custom1;
        XPushButton* custom2;
        XPushButton* okBtn;
        XPushButton* cancelBtn;
        const XVector* members;
        if (!box) {
            XAPI_EXPECT(0, "XDialogButtonBox 堆构造成功");
        } else {
            dlg_sig_reset();
            custom1 = XPushButton_create(NULL, 0);
            custom2 = XPushButton_create(NULL, 0);

            /* ---- 默认值 ---- */
            XAPI_EXPECT(XDialogButtonBox_orientation(box) == 1,
                        "按钮盒默认方向=水平(1)（对标 QDialogButtonBox orientation 默认 Qt::Horizontal）");
            XAPI_EXPECT(!XDialogButtonBox_centerButtons(box),
                        "按钮盒默认不居中（对标 QDialogButtonBox 默认右对齐布局）");

            /* ---- 方向/居中往返 ---- */
            XDialogButtonBox_setOrientation(box, 2);
            XAPI_EXPECT(XDialogButtonBox_orientation(box) == 2,
                        "setOrientation(2)→orientation()==2（对标 Qt::Vertical 数值）");
            XDialogButtonBox_setOrientation(box, 1);
            XDialogButtonBox_setCenterButtons(box, true);
            XAPI_EXPECT(XDialogButtonBox_centerButtons(box),
                        "setCenterButtons(true) 回读（对标 QDialogButtonBox::setCenterButtons/centerButtons）");
            XDialogButtonBox_setCenterButtons(box, false);

            /* ---- addButton(控件, 角色) / buttonRole ---- */
            XDialogButtonBox_addButton(box, (XAbstractButton*)custom1,
                                       XDialogButtonBoxRole_ActionRole);
            members = XDialogButtonBox_buttons(box);
            XAPI_EXPECT(members != NULL &&
                        (int)XVector_size_base((const XContainer*)members) == 1,
                        "addButton(button, role) 后成员数==1（对标 QDialogButtonBox::addButton）");
            XAPI_EXPECT(XDialogButtonBox_buttonRole(
                            box, (XAbstractButton*)custom1) ==
                        XDialogButtonBoxRole_ActionRole,
                        "buttonRole(成员)==ActionRole（对标 buttonRole 返回 addButton 登记的角色）");
            XAPI_EXPECT(XDialogButtonBox_buttonRole(
                            box, (XAbstractButton*)custom2) ==
                        XDialogButtonBoxRole_InvalidRole,
                        "buttonRole(非成员)==InvalidRole(-1)（对标 QDialogButtonBox::buttonRole）");

            /* ---- addButton(文本, 角色)：创建按钮 ---- */
            XWidget_delete_base((XWidget*)custom2); /* 探针按钮即测即毁。 */
            custom2 = XDialogButtonBox_addButton_2(
                box, "自定义帮助", XDialogButtonBoxRole_HelpRole);
            XAPI_EXPECT(custom2 != NULL,
                        "addButton_2(文本, HelpRole) 创建按钮非空（对标 addButton(QString, role) 创建 QPushButton）");
            members = XDialogButtonBox_buttons(box);
            XAPI_EXPECT(members != NULL &&
                        (int)XVector_size_base((const XContainer*)members) == 2,
                        "addButton_2 后成员数==2（对标 addButton(QString, role) 追加）");
            XAPI_EXPECT(XDialogButtonBox_buttonRole(
                            box, (XAbstractButton*)custom2) ==
                        XDialogButtonBoxRole_HelpRole,
                        "addButton_2 按钮角色==HelpRole（对标角色登记）");

            /* ---- addButton(标准按钮) + standardButton/button 反查 ---- */
            okBtn = XDialogButtonBox_addButton_3(
                box, XDialogButtonBoxStandard_Ok);
            XAPI_EXPECT(okBtn != NULL &&
                        XDialogButtonBox_standardButton(
                            box, (XAbstractButton*)okBtn) ==
                        XDialogButtonBoxStandard_Ok,
                        "addButton_3(Ok) 创建且 standardButton 反查==Ok（对标 addButton(StandardButton)/standardButton）");
            XAPI_EXPECT(XDialogButtonBox_button(
                            box, XDialogButtonBoxStandard_Ok) == okBtn,
                        "button(Ok) 反查到同一颗按钮（对标 QDialogButtonBox::button(StandardButton)）");

            /* ---- setStandardButtons 重建 + 位掩码往返 ---- */
            XDialogButtonBox_setStandardButtons(
                box, (int)XDialogButtonBoxStandard_Ok |
                     (int)XDialogButtonBoxStandard_Cancel);
            XAPI_EXPECT(XDialogButtonBox_standardButtons(box) ==
                        ((int)XDialogButtonBoxStandard_Ok |
                         (int)XDialogButtonBoxStandard_Cancel),
                        "setStandardButtons(Ok|Cancel) 位掩码回读（对标 setStandardButtons/standardButtons）");
            members = XDialogButtonBox_buttons(box);
            XAPI_EXPECT(members != NULL &&
                        (int)XVector_size_base((const XContainer*)members) == 2,
                        "setStandardButtons 重建后成员数==2（对标整表重建替换全部按钮）");
            okBtn = XDialogButtonBox_button(box,
                                            XDialogButtonBoxStandard_Ok);
            cancelBtn = XDialogButtonBox_button(box,
                                                XDialogButtonBoxStandard_Cancel);
            XAPI_EXPECT(okBtn != NULL && cancelBtn != NULL,
                        "button(Ok)/button(Cancel) 均反查非空（对标 button(StandardButton)）");
            {
                const XString* okText = okBtn
                    ? XAbstractButton_text((XAbstractButton*)okBtn) : NULL;
                XAPI_EXPECT(okText &&
                            strcmp(xapi_u8(okText), "确定") == 0,
                            "标准按钮 Ok 文本=确定（对标 Qt 标准按钮经翻译的显示文本，本库为中文表）");
            }
            XAPI_EXPECT(XDialogButtonBox_standardButton(
                            box, (XAbstractButton*)cancelBtn) ==
                        XDialogButtonBoxStandard_Cancel,
                        "standardButton(Cancel 按钮)==Cancel（对标标准值反查）");

            /* ---- 信号中继：角色→accepted/rejected/helpRequested ----
             * 注：custom1/custom2 已被 setStandardButtons 的 clear 摘除
             * （用户按钮摘父归还调用方），需重新登记后点击。 */
            XObject_connect_1((XObject*)box,
                              XSignal(XDialogButtonBox_accepted_signal),
                              (XObject*)box, dlg_boxAcceptedSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)box,
                              XSignal(XDialogButtonBox_rejected_signal),
                              (XObject*)box, dlg_boxRejectedSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)box,
                              XSignal(XDialogButtonBox_helpRequested_signal),
                              (XObject*)box, dlg_boxHelpSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)box,
                              XSignal(XDialogButtonBox_clicked_signal),
                              (XObject*)box, dlg_boxClickedSlot,
                              XConnectionType_Direct);
            XAbstractButton_click((XAbstractButton*)okBtn);
            XAPI_EXPECT(g_dlgSig.boxAccepted == 1 && g_dlgSig.boxClicked == 1 &&
                        g_dlgSig.lastBoxClicked == (XAbstractButton*)okBtn,
                        "点击 AcceptRole 按钮（确定）发射 accepted + clicked(button)（对标 QDialogButtonBox AcceptRole 点击中继）");
            XAbstractButton_click((XAbstractButton*)cancelBtn);
            XAPI_EXPECT(g_dlgSig.boxRejected == 1,
                        "点击 RejectRole 按钮（取消）发射 rejected（对标 RejectRole 点击中继）");

            /* YesRole→accepted、NoRole→rejected（Qt 文档口径）。 */
            XDialogButtonBox_setStandardButtons(
                box, (int)XDialogButtonBoxStandard_Yes |
                     (int)XDialogButtonBoxStandard_No);
            dlg_sig_reset();
            {
                XPushButton* yesBtn =
                    XDialogButtonBox_button(box, XDialogButtonBoxStandard_Yes);
                XPushButton* noBtn =
                    XDialogButtonBox_button(box, XDialogButtonBoxStandard_No);
                XAbstractButton_click((XAbstractButton*)yesBtn);
                XAbstractButton_click((XAbstractButton*)noBtn);
                XAPI_EXPECT(g_dlgSig.boxAccepted == 1 && g_dlgSig.boxRejected == 1,
                            "点击 Yes 发射 accepted、点击 No 发射 rejected（对标 Qt 文档：YesRole→accepted、NoRole→rejected）");
            }

            /* ---- HelpRole → helpRequested ---- */
            XDialogButtonBox_addButton(box, (XAbstractButton*)custom1,
                                       XDialogButtonBoxRole_HelpRole);
            dlg_sig_reset();
            XAbstractButton_click((XAbstractButton*)custom1);
            XAPI_EXPECT(g_dlgSig.boxHelp == 1 && g_dlgSig.boxClicked == 1,
                        "点击 HelpRole 按钮发射 helpRequested + clicked（对标 QDialogButtonBox HelpRole 中继）");

            /* ---- 边界：未知标准值/NoButton 查询 ---- */
            XAPI_EXPECT(XDialogButtonBox_button(
                            box, XDialogButtonBoxStandard_NoButton) == NULL,
                        "button(NoButton)==NULL（无 NoButton 成员，对标不存在槽位返回空）");
            XAPI_EXPECT(XDialogButtonBox_addButton_3(
                            box, (XDialogButtonBoxStandardButton)0x12340000) == NULL,
                        "addButton_3(未知枚举)==NULL（头文件注明未知枚举返回 NULL）");

            /* ---- removeButton / clear ---- */
            XDialogButtonBox_removeButton(box, (XAbstractButton*)custom1);
            XAPI_EXPECT(XDialogButtonBox_buttonRole(
                            box, (XAbstractButton*)custom1) ==
                        XDialogButtonBoxRole_InvalidRole,
                        "removeButton 后 buttonRole==InvalidRole（对标 QDialogButtonBox::removeButton 摘除成员）");
            members = XDialogButtonBox_buttons(box);
            XAPI_EXPECT(members != NULL &&
                        (int)XVector_size_base((const XContainer*)members) == 2,
                        "removeButton 后成员数回到 2（Yes/No 标准按钮保留，对标成员表收缩）");
            XDialogButtonBox_clear(box);
            XAPI_EXPECT(XDialogButtonBox_buttons(box) != NULL &&
                        (int)XVector_size_base((const XContainer*)
                            XDialogButtonBox_buttons(box)) == 0 &&
                        XDialogButtonBox_standardButtons(box) == 0,
                        "clear() 后成员/标准位掩码全空（对标 QDialogButtonBox::clear；盒自建标准按钮由盒删除）");

            /* custom1 为调用方所有（摘父归还），即测即毁。 */
            XWidget_delete_base((XWidget*)custom1);
            XDialogButtonBox_delete_base(box);
            /* custom2 由 addButton_2 创建、归调用方（Qt 同口径），clear
             * 已摘父，此处释放。 */
            XWidget_delete_base((XWidget*)custom2);
        }
    }

#endif /* XWIDGET_ON && XDIALOG_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON */

#if XWIDGET_ON && XDIALOG_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON && XLABEL_ON && XMESSAGEBOX_ON

    /* ================================================================
     * 3. XMessageBox（对标 QMessageBox）：文本/图标/标准按钮/默认与
     *    转义按钮/复选框/选项/iconPixmap。
     *    注：exec() 与 information/warning/critical/question/about 静态
     *    便捷函数均为模态阻塞入口，无头测试不调用。
     * ================================================================ */
    {
        XMessageBox* box = XMessageBox_create(NULL, 0);
        XAbstractButton* custom;
        if (!box) {
            XAPI_EXPECT(0, "XMessageBox 堆构造成功");
        } else {
            dlg_sig_reset();

            /* ---- 默认值（Qt 6.8 QMessageBox 构造默认） ---- */
            XAPI_EXPECT(XMessageBox_icon(box) == XMessageBoxIcon_NoIcon,
                        "消息框默认图标==NoIcon（对标 QMessageBox 构造默认无图标）");
            XAPI_EXPECT(xapi_cstr(XMessageBox_text(box))[0] == '\0',
                        "消息框默认 text 为空串（对标 QMessageBox::text 默认空）");
            XAPI_EXPECT(xapi_cstr(XMessageBox_detailedText(box))[0] == '\0' &&
                        xapi_cstr(XMessageBox_informativeText(box))[0] == '\0',
                        "默认 detailedText/informativeText 均空（对标 QMessageBox 默认无详细/补充文本）");
            XAPI_EXPECT(XMessageBox_textFormat(box) == XLabelTextFormat_AutoText,
                        "默认 textFormat==AutoText（对标 QMessageBox::textFormat 默认 Qt::AutoText）");
            XAPI_EXPECT(XMessageBox_textInteractionFlags(box) ==
                        (XLabelTextInteractionFlags)
                        XLabelTextInteraction_LinksAccessibleByMouse,
                        "默认 textInteractionFlags==LinksAccessibleByMouse(0x4)（对标 QMessageBox 默认交互标志）");

            /* ---- 文本/标题/图标往返 ---- */
            XMessageBox_setText(box, "保存更改吗？");
            XAPI_EXPECT(strcmp(xapi_cstr(XMessageBox_text(box)), "保存更改吗？") == 0,
                        "setText→text 往返（对标 QMessageBox::setText/text）");
            XMessageBox_setTitle(box, "提示");
            XAPI_EXPECT(strcmp(xapi_cstr(XMessageBox_title(box)), "提示") == 0,
                        "setTitle→title 往返（对标 QMessageBox 窗口标题属性）");
            XMessageBox_setIcon(box, XMessageBoxIcon_Warning);
            XAPI_EXPECT(XMessageBox_icon(box) == XMessageBoxIcon_Warning,
                        "setIcon(Warning)→icon()==Warning（对标 QMessageBox::setIcon/icon）");

            /* ---- 标准按钮与角色 ---- */
            XMessageBox_setStandardButtons(
                box, (int)XDialogButtonBoxStandard_Save |
                     (int)XDialogButtonBoxStandard_Discard |
                     (int)XDialogButtonBoxStandard_Cancel);
            XAPI_EXPECT(XMessageBox_standardButtons(box) ==
                        ((int)XDialogButtonBoxStandard_Save |
                         (int)XDialogButtonBoxStandard_Discard |
                         (int)XDialogButtonBoxStandard_Cancel),
                        "setStandardButtons 位掩码回读（对标 QMessageBox::setStandardButtons/standardButtons）");
            {
                XAbstractButton* saveBtn =
                    XMessageBox_button(box, XDialogButtonBoxStandard_Save);
                XAbstractButton* cancelBtn =
                    XMessageBox_button(box, XDialogButtonBoxStandard_Cancel);
                XAbstractButton* discardBtn =
                    XMessageBox_button(box, XDialogButtonBoxStandard_Discard);
                XAPI_EXPECT(saveBtn != NULL && cancelBtn != NULL &&
                            discardBtn != NULL,
                            "button(Save/Discard/Cancel) 反查非空（对标 QMessageBox::button(StandardButton)）");
                XAPI_EXPECT(XMessageBox_buttonRole(box, saveBtn) ==
                            XMessageBoxButtonRole_AcceptRole,
                            "Save 按钮角色==AcceptRole（对标 Qt 表：Save 属 AcceptRole）");
                XAPI_EXPECT(XMessageBox_buttonRole(box, cancelBtn) ==
                            XMessageBoxButtonRole_RejectRole,
                            "Cancel 按钮角色==RejectRole（对标 Qt 表：Cancel 属 RejectRole）");

                /* ---- addButton_2 自定义按钮 ---- */
                custom = XMessageBox_addButton_2(box, "自定义操作",
                                                 XMessageBoxButtonRole_ActionRole);
                XAPI_EXPECT(custom != NULL &&
                            XMessageBox_buttonRole(box, custom) ==
                            XMessageBoxButtonRole_ActionRole,
                            "addButton_2(文本, ActionRole) 创建并登记角色（对标 QMessageBox::addButton(QString, role)）");
                XAPI_EXPECT(XMessageBox_standardButton(box, custom) ==
                            (int)XDialogButtonBoxStandard_NoButton &&
                            XMessageBox_standardButton(box, saveBtn) ==
                            (int)XDialogButtonBoxStandard_Save,
                            "standardButton 区分自定义(NoButton)/标准(Save) 按钮（对标 QMessageBox::standardButton）");

                /* ---- 默认/转义按钮 ---- */
                XMessageBox_setDefaultButton_2(
                    box, (int)XDialogButtonBoxStandard_Save);
                XAPI_EXPECT(XMessageBox_defaultButton(box) == saveBtn,
                            "setDefaultButton_2(Save) 回读同一颗（对标 QMessageBox::setDefaultButton(StandardButton)）");
                XMessageBox_setEscapeButton_2(
                    box, (int)XDialogButtonBoxStandard_Discard);
                XAPI_EXPECT(XMessageBox_escapeButton(box) == discardBtn,
                            "setEscapeButton_2(Discard) 回读（对标 QMessageBox::setEscapeButton(StandardButton)）");

                /* ---- 键盘路径：回车→默认按钮、Esc→转义按钮 ---- */
                XObject_connect_1((XObject*)box,
                                  XSignal(XMessageBox_buttonClicked_signal),
                                  (XObject*)box, dlg_msgButtonClickedSlot,
                                  XConnectionType_Direct);
                dlg_injectKeyPress((XWidget*)box, (int)XKey_Return);
                XAPI_EXPECT(XMessageBox_clickedButton(box) == saveBtn &&
                            g_dlgSig.msgButtonClicked == 1,
                            "回车键点击默认按钮 Save（对标 QMessageBox::keyPressEvent Enter→defaultButton，clickedButton 记录）");
                dlg_injectKeyPress((XWidget*)box, (int)XKey_Escape);
                XAPI_EXPECT(XMessageBox_clickedButton(box) == discardBtn,
                            "Esc 键点击转义按钮 Discard（对标 QMessageBox Esc→escapeButton 路径）");

                /* ---- 清转义按钮后 Esc 回退基类 reject ---- */
                XObject_connect_1((XObject*)box,
                                  XSignal(XDialog_rejected_signal),
                                  (XObject*)box, dlg_rejectedSlot,
                                  XConnectionType_Direct);
                XMessageBox_setEscapeButton(box, NULL);
                dlg_sig_reset();
                dlg_injectKeyPress((XWidget*)box, (int)XKey_Escape);
                XAPI_EXPECT(XDialog_result(&box->m_base) == 0 &&
                            g_dlgSig.rejected == 1,
                            "无转义按钮时 Esc 回退 QDialog::reject（对标 QMessageBox 未设 escapeButton 的 Esc 行为）");

                /* ---- 程序化点击 → clickedButton/buttonClicked ---- */
                XAbstractButton_click(saveBtn);
                XAPI_EXPECT(XMessageBox_clickedButton(box) == saveBtn &&
                            g_dlgSig.msgButtonClicked == 1,
                            "点击 Save 后 clickedButton==Save 且发射 buttonClicked（对标 QMessageBox 按钮盒 clicked→clickedButton/buttonClicked 链路）");

                /* ---- removeButton 连带清默认/转义/最近点击 ---- */
                XMessageBox_removeButton(box, custom);
                XAPI_EXPECT(XMessageBox_buttonRole(box, custom) ==
                            XMessageBoxButtonRole_InvalidRole &&
                            XMessageBox_clickedButton(box) != custom,
                            "removeButton 摘除成员并清悬空指针（对标 QMessageBox::removeButton；头文件注明连带清理）");
            }

            /* ---- setButtonText / buttonText（Qt 弃用但保留的 API 面） ---- */
            XMessageBox_setButtonText_2(box,
                                        (int)XDialogButtonBoxStandard_Save,
                                        "保存并继续");
            XAPI_EXPECT(dlg_str_eq_take(
                            XMessageBox_buttonText(
                                box, (int)XDialogButtonBoxStandard_Save),
                            "保存并继续"),
                        "setButtonText_2(Save, 文本)→buttonText 回读（对标 QMessageBox::setButtonText/buttonText，Qt 已弃用保留）");
            XAPI_EXPECT(dlg_str_eq_take(
                            XMessageBox_buttonText(box, 0x1000), ""),
                        "buttonText(不存在标准值) 返回空文本（对标找不到按钮空串；返回副本归调用方）");

            /* ---- 选项位（本库未列 Option 枚举，仅状态往返） ---- */
            XMessageBox_setOption(box, 0x1, true);
            XAPI_EXPECT(XMessageBox_testOption(box, 0x1),
                        "setOption(0x1)/testOption 对称（对标 QMessageBox::setOption/testOption；位值本库未对齐枚举，不硬断言 Qt 常量）");
            XMessageBox_setOptions(box, 0x2 | 0x4);
            XAPI_EXPECT(XMessageBox_options(box) == (0x2 | 0x4),
                        "setOptions(0x2|0x4)→options 回读（对标 QMessageBox::setOptions/options）");

            /* ---- 复选框（所有权转移） ---- */
#if XCHECKBOX_ON
            {
                XCheckBox* cb = XCheckBox_create(NULL, 0);
                XMessageBox_setCheckBox(box, cb);
                XAPI_EXPECT(XMessageBox_checkBox(box) == cb,
                            "setCheckBox 后 checkBox 回读（对标 QMessageBox::setCheckBox，所有权转移给消息框）");
                XMessageBox_setCheckBox(box, NULL);
                XAPI_EXPECT(XMessageBox_checkBox(box) == NULL,
                            "setCheckBox(NULL) 清除并释放复选框（对标 QMessageBox 传入 NULL 仅清除；旧对象随消息框释放）");
            }
#endif /* XCHECKBOX_ON */

            /* ---- iconPixmap（深拷贝存储，暂不参与绘制） ---- */
            {
                XImage pixmap;
                XImage_init(&pixmap);
                XMessageBox_setIconPixmap(box, &pixmap);
                XAPI_EXPECT(XMessageBox_iconPixmap(box) != NULL,
                            "setIconPixmap 后 iconPixmap 回读非空（对标 QMessageBox::setIconPixmap 深拷贝存储）");
                XMessageBox_setIconPixmap(box, NULL);
                XAPI_EXPECT(XMessageBox_iconPixmap(box) == NULL,
                            "setIconPixmap(NULL) 清除位图（对标传 NULL 清空）");
                XImage_deinit_base(&pixmap);
            }

            /* ---- 文本格式/交互标志往返 ---- */
            XMessageBox_setTextFormat(box, XLabelTextFormat_PlainText);
            XAPI_EXPECT(XMessageBox_textFormat(box) ==
                        XLabelTextFormat_PlainText,
                        "setTextFormat(PlainText) 回读（对标 QMessageBox::setTextFormat，转发内部标签）");
            XMessageBox_setTextInteractionFlags(
                box, (XLabelTextInteractionFlags)
                     XLabelTextInteraction_TextSelectableByMouse);
            XAPI_EXPECT(XMessageBox_textInteractionFlags(box) ==
                        (XLabelTextInteractionFlags)
                        XLabelTextInteraction_TextSelectableByMouse,
                        "setTextInteractionFlags 回读（对标 QMessageBox::setTextInteractionFlags）");

            /* ---- 标准图标（对标静态 standardIcon） ---- */
            XAPI_EXPECT(XMessageBox_standardIcon(
                            XMessageBoxIcon_NoIcon) == NULL,
                        "standardIcon(NoIcon)==NULL（对标 Qt：NoIcon 映射空图标）");
            XAPI_EXPECT((XMessageBox_standardIcon(
                             XMessageBoxIcon_Information) != NULL) ==
                        (XApplication_style() != NULL),
                        "standardIcon(Information) 与样式存在性一致（对标 standardIcon 经当前样式生成；无样式环境恒 NULL）");

            /* aboutQt：文档化空操作（对标 QMessageBox::aboutQt），冒烟
             * 调用无断言。 */
            XMessageBox_aboutQt(NULL, NULL);

            /* custom 经 removeButton 摘除、归调用方，即测即毁。 */
            XWidget_delete_base(custom);
            XMessageBox_delete_base(box);
        }
    }

#endif /* XWIDGET_ON && XDIALOG_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON && XLABEL_ON && XMESSAGEBOX_ON */

#if XWIDGET_ON && XDIALOG_ON

    /* ================================================================
     * 4. XInputDialog（对标 QInputDialog）：inputMode/值/范围/选项/下拉。
     *    注：getText/getInt/getDouble/getItem 等静态便捷函数有应用实例
     *    即阻塞 exec，无头测试不调用。
     * ================================================================ */
    {
        XInputDialog* input = XInputDialog_create(NULL, 0);
        if (!input) {
            XAPI_EXPECT(0, "XInputDialog 堆构造成功");
        } else {
            dlg_sig_reset();

            /* ---- 默认值（Qt 6.8 QInputDialog 构造默认） ---- */
            XAPI_EXPECT(XInputDialog_inputMode(input) ==
                        XInputDialog_TextInput,
                        "默认 inputMode==TextInput(0)（对标 QInputDialog 构造默认单行文本）");
            XAPI_EXPECT(XInputDialog_options(input) == 0,
                        "默认 options==0（对标 QInputDialog 默认无选项位）");
            XAPI_EXPECT(XInputDialog_intValue(input) == 0,
                        "默认 intValue==0（对标 QInputDialog::intValue 默认 0）");
            XAPI_EXPECT(XInputDialog_doubleValue(input) == 0.0,
                        "默认 doubleValue==0.0（对标 QInputDialog::doubleValue 默认 0.0）");
            XAPI_EXPECT(XInputDialog_intMinimum(input) == (-2147483647 - 1) &&
                        XInputDialog_intMaximum(input) == 2147483647,
                        "整数范围默认 INT_MIN..INT_MAX（对标 Qt 文档 intMinimum/intMaximum 默认值）");
            XAPI_EXPECT(XInputDialog_intStep(input) == 1,
                        "默认 intStep==1（对标 QInputDialog::intStep 默认 1）");
            /* 浮点范围默认 ±1e308 为本库头文件注明的对齐值；Qt 文档未
             * 给出同形默认值出处，按头文件口径断言（不硬指 Qt）。 */
            XAPI_EXPECT(XInputDialog_doubleMinimum(input) == -1.0e308 &&
                        XInputDialog_doubleMaximum(input) == 1.0e308,
                        "浮点范围默认 ±1e308（本库 XInputDialog.h 注明的对齐默认；Qt 文档未给同形出处）");
            XAPI_EXPECT(XInputDialog_doubleStep(input) == 1.0 &&
                        XInputDialog_doubleDecimals(input) == 2,
                        "默认 doubleStep==1、doubleDecimals==2（对标 Qt 文档 doubleStep 1 / decimals 默认 2）");
            XAPI_EXPECT(!XInputDialog_isComboBoxEditable(input),
                        "默认下拉不可编辑（对标 QInputDialog::isComboBoxEditable 默认 false）");
            XAPI_EXPECT(XInputDialog_textEchoMode(input) ==
                        XInputDialogEchoMode_Normal,
                        "默认 textEchoMode==Normal（对标 QInputDialog::textEchoMode 默认 Normal）");

            /* ---- 输入模式/选项往返 ---- */
            XInputDialog_setInputMode(input, XInputDialog_IntInput);
            XAPI_EXPECT(XInputDialog_inputMode(input) ==
                        XInputDialog_IntInput,
                        "setInputMode(IntInput) 回读（对标 QInputDialog::setInputMode/inputMode）");
            XInputDialog_setOption(input, XInputDialog_NoButtons, true);
            XAPI_EXPECT(XInputDialog_testOption(input, XInputDialog_NoButtons),
                        "setOption(NoButtons)/testOption 对称（对标 QInputDialog::setOption/testOption）");
            XInputDialog_setOptions(
                input, (XInputDialogOptions)XInputDialog_NoButtons |
                       (XInputDialogOptions)XInputDialog_UseListViewForComboBoxItems);
            XAPI_EXPECT(XInputDialog_options(input) == 0x3,
                        "setOptions(NoButtons|UseListView)==0x3 回读（对标 setOptions/options，位值与 Qt InputDialogOption 一致）");
            XInputDialog_setOptions(input, 0);

            /* ---- 文本值 + textValueChanged（实变才发射） ---- */
            XObject_connect_1((XObject*)input,
                              XSignal(XInputDialog_textValueChanged_signal),
                              (XObject*)input, dlg_textValueChangedSlot,
                              XConnectionType_Direct);
            {
                XString* t = XString_create_utf8("8080");
                XInputDialog_setTextValue(input, t);
                XString_delete_base((XClass*)t);
            }
            XAPI_EXPECT(dlg_str_eq_take(XInputDialog_textValue(input),
                                        "8080"),
                        "setTextValue→textValue 往返（对标 QInputDialog::setTextValue/textValue）");
            XAPI_EXPECT(g_dlgSig.textValueChanged == 1 &&
                        strcmp(g_dlgSig.lastTextValue, "8080") == 0,
                        "textValue 实际变化发射 textValueChanged 且载荷一致（对标 QInputDialog::textValueChanged；本库头文件注明实变才发射）");
            {
                XString* t = XString_create_utf8("8080");
                XInputDialog_setTextValue(input, t);
                XString_delete_base((XClass*)t);
            }
            XAPI_EXPECT(g_dlgSig.textValueChanged == 1,
                        "同值重设不重复发射 textValueChanged（本库口径：实际变化才发射；Qt 为无条件发射，见 notes）");

            /* ---- 整数值 + intValueChanged ---- */
            XObject_connect_1((XObject*)input,
                              XSignal(XInputDialog_intValueChanged_signal),
                              (XObject*)input, dlg_intValueChangedSlot,
                              XConnectionType_Direct);
            XInputDialog_setIntValue(input, 42);
            XAPI_EXPECT(XInputDialog_intValue(input) == 42 &&
                        g_dlgSig.intValueChanged == 1 &&
                        g_dlgSig.lastIntValue == 42,
                        "setIntValue(42) 回读并发射 intValueChanged(42)（对标 QInputDialog::setIntValue/intValueChanged）");

            /* ---- 浮点值 + doubleValueChanged ---- */
            XObject_connect_1((XObject*)input,
                              XSignal(XInputDialog_doubleValueChanged_signal),
                              (XObject*)input, dlg_doubleValueChangedSlot,
                              XConnectionType_Direct);
            XInputDialog_setDoubleValue(input, 2.5);
            XAPI_EXPECT(XInputDialog_doubleValue(input) == 2.5 &&
                        g_dlgSig.doubleValueChanged == 1 &&
                        g_dlgSig.lastDoubleValue == 2.5,
                        "setDoubleValue(2.5) 回读并发射 doubleValueChanged（对标 QInputDialog::setDoubleValue/doubleValueChanged）");

            /* ---- 范围/步进/小数位 ---- */
            XInputDialog_setIntRange(input, 0, 100);
            XAPI_EXPECT(XInputDialog_intMinimum(input) == 0 &&
                        XInputDialog_intMaximum(input) == 100,
                        "setIntRange(0,100) 回读（对标 QInputDialog::setIntRange）");
            XInputDialog_setIntStep(input, 5);
            XInputDialog_setIntStep(input, 0);
            XAPI_EXPECT(XInputDialog_intStep(input) == 5,
                        "setIntStep(5) 生效、setIntStep(0) 被忽略（本库头文件/实现口径：非正步进不收；Qt 文档未约束步进取值）");
            XInputDialog_setDoubleRange(input, -1.0, 1.0);
            XAPI_EXPECT(XInputDialog_doubleMinimum(input) == -1.0 &&
                        XInputDialog_doubleMaximum(input) == 1.0,
                        "setDoubleRange(-1,1) 回读（对标 QInputDialog::setDoubleRange）");
            XInputDialog_setDoubleStep(input, 0.5);
            XInputDialog_setDoubleDecimals(input, 3);
            XAPI_EXPECT(XInputDialog_doubleStep(input) == 0.5 &&
                        XInputDialog_doubleDecimals(input) == 3,
                        "setDoubleStep/setDoubleDecimals 回读（对标 QInputDialog 同名属性）");

            /* ---- 下拉模式：可编辑 + 条目深拷贝 ---- */
            XInputDialog_setComboBoxEditable(input, true);
            XAPI_EXPECT(XInputDialog_isComboBoxEditable(input),
                        "setComboBoxEditable(true) 回读（对标 QInputDialog::setComboBoxEditable）");
            {
                XStringList* items = dlg_make_list2("HTTP", "SOCKS");
                XStringList* got;
                XInputDialog_setComboBoxItems(input, items);
                XStringList_delete_base((XClass*)items); /* setter 深拷贝。 */
                got = XInputDialog_comboBoxItems(input);
                XAPI_EXPECT(dlg_list_count(got) == 2 &&
                            dlg_list_at_eq(got, 0, "HTTP") &&
                            dlg_list_at_eq(got, 1, "SOCKS"),
                            "setComboBoxItems 深拷贝两元素并按序回读（对标 QInputDialog::setComboBoxItems/comboBoxItems）");
                if (got) XStringList_delete_base((XClass*)got);
            }

            /* ---- 按钮/标签文本往返（副本 getter） ---- */
            {
                XString* t = XString_create_utf8("请输入端口");
                XInputDialog_setLabelText(input, t);
                XString_delete_base((XClass*)t);
            }
            XAPI_EXPECT(dlg_str_eq_take(XInputDialog_labelText(input),
                                        "请输入端口"),
                        "setLabelText→labelText 往返（对标 QInputDialog::setLabelText/labelText）");
            {
                XString* t = XString_create_utf8("走你");
                XInputDialog_setOkButtonText(input, t);
                XString_delete_base((XClass*)t);
                t = XString_create_utf8("算了");
                XInputDialog_setCancelButtonText(input, t);
                XString_delete_base((XClass*)t);
            }
            XAPI_EXPECT(dlg_str_eq_take(XInputDialog_okButtonText(input),
                                        "走你") &&
                        dlg_str_eq_take(XInputDialog_cancelButtonText(input),
                                        "算了"),
                        "okButtonText/cancelButtonText 往返（对标 QInputDialog::setOkButtonText/setCancelButtonText）");
            XInputDialog_setTextEchoMode(input, XInputDialogEchoMode_Password);
            XAPI_EXPECT(XInputDialog_textEchoMode(input) ==
                        XInputDialogEchoMode_Password,
                        "setTextEchoMode(Password) 回读（对标 QInputDialog::setTextEchoMode）");

            /* ---- 手动直发确认信号（头文件注明测试口径） ---- */
            {
                XString* t = XString_create_utf8("abc");
                XInputDialog_textValueSelected_signal(input, t);
                XInputDialog_intValueSelected_signal(input, 9);
                XInputDialog_comboBoxTextChanged_signal(input, t);
                XString_delete_base((XClass*)t);
                /* 信号函数直发冒烟：发射路径与 XObject_emitSignal 一致，
                 * 断言其可安全调用（无接收者也不崩溃）。 */
                XAPI_EXPECT(1, "textValueSelected/intValueSelected/comboBoxTextChanged 手动触发可安全直发（头文件注明的测试口径）");
            }

            /* ---- NULL 边界（文档化 NULL 安全） ---- */
            XAPI_EXPECT(XInputDialog_inputMode(NULL) ==
                        XInputDialog_TextInput &&
                        XInputDialog_intValue(NULL) == 0 &&
                        XInputDialog_doubleValue(NULL) == 0.0 &&
                        XInputDialog_options(NULL) == 0,
                        "NULL 入参 getter 安全（文档化：inputMode/intValue/doubleValue/options(NULL) 返回默认）");

            XInputDialog_delete_base(input);
        }
    }

    /* ================================================================
     * 5. XFileDialog（对标 QFileDialog）：fileMode/acceptMode/过滤器/
     *    目录/选中文件/选项/标签/MIME/历史/协议。
     *    注：getOpenFileName 等静态便捷函数有应用实例即阻塞 exec，无头
     *    测试不调用。
     * ================================================================ */
    {
        XFileDialog* file = XFileDialog_create(NULL, 0);
        if (!file) {
            XAPI_EXPECT(0, "XFileDialog 堆构造成功");
        } else {
            dlg_sig_reset();

            /* ---- 登记信号槽（手动直发计数前置条件；此前只发未接，
             *      计数恒 0 属测试侧缺陷） ---- */
            XObject_connect_1((XObject*)file,
                              XSignal(XFileDialog_fileSelected_signal),
                              (XObject*)file, dlg_fileSelectedSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)file,
                              XSignal(XFileDialog_filesSelected_signal),
                              (XObject*)file, dlg_filesSelectedSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)file,
                              XSignal(XFileDialog_currentChanged_signal),
                              (XObject*)file, dlg_currentChangedSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)file,
                              XSignal(XFileDialog_directoryEntered_signal),
                              (XObject*)file, dlg_directoryEnteredSlot,
                              XConnectionType_Direct);
            XObject_connect_1((XObject*)file,
                              XSignal(XFileDialog_filterSelected_signal),
                              (XObject*)file, dlg_filterSelectedSlot,
                              XConnectionType_Direct);

            /* ---- 默认值 ---- */
            XAPI_EXPECT(XFileDialog_fileMode(file) == XFileDialog_ExistingFile,
                        "默认 fileMode==ExistingFile(1)（本库 XFileDialog.h 注明默认；Qt 6.8 默认 AnyFile(0)，偏差见文件头备忘）");
            XAPI_EXPECT(XFileDialog_acceptMode(file) == XFileDialog_AcceptOpen,
                        "默认 acceptMode==AcceptOpen(0)（对标 QFileDialog 默认打开模式）");
            XAPI_EXPECT(XFileDialog_viewMode(file) == XFileDialogViewMode_Detail,
                        "默认 viewMode==Detail(0)（对标 QFileDialog 默认详细信息视图）");
            XAPI_EXPECT(XFileDialog_options(file) == 0,
                        "默认 options==0（对标 QFileDialog 默认无选项位）");
            XAPI_EXPECT(dlg_str_eq_take(XFileDialog_directory(file), "") &&
                        dlg_str_eq_take(XFileDialog_defaultSuffix(file), ""),
                        "默认 directory/defaultSuffix 空串（对标 QFileDialog 初始无目录/后缀）");

            /* ---- 模式往返 ---- */
            XFileDialog_setFileMode(file, XFileDialog_ExistingFiles);
            XAPI_EXPECT(XFileDialog_fileMode(file) ==
                        XFileDialog_ExistingFiles,
                        "setFileMode(ExistingFiles) 回读（对标 QFileDialog::setFileMode/fileMode）");
            XFileDialog_setAcceptMode(file, XFileDialog_AcceptSave);
            XAPI_EXPECT(XFileDialog_acceptMode(file) == XFileDialog_AcceptSave,
                        "setAcceptMode(AcceptSave) 回读（对标 QFileDialog::setAcceptMode）");
            XFileDialog_setViewMode(file, XFileDialogViewMode_List);
            XAPI_EXPECT(XFileDialog_viewMode(file) == XFileDialogViewMode_List,
                        "setViewMode(List) 回读（对标 QFileDialog::setViewMode）");

            /* ---- 选项位（位值与 Qt 6.8.3 qfiledialog.h 一致） ---- */
            XFileDialog_setOption(file, XFileDialog_ShowDirsOnly, true);
            XAPI_EXPECT(XFileDialog_testOption(file, XFileDialog_ShowDirsOnly),
                        "setOption(ShowDirsOnly)/testOption 对称（对标 QFileDialog::setOption/testOption）");
            XFileDialog_setOptions(
                file, (XFileDialogOptions)XFileDialog_DontConfirmOverwrite |
                      (XFileDialogOptions)XFileDialog_ReadOnly);
            XAPI_EXPECT(XFileDialog_options(file) == 0x14,
                        "setOptions(DontConfirmOverwrite|ReadOnly)==0x14（对标 Qt 位值 0x00000004|0x00000010，头文件注明与 qfiledialog.h 一致）");

            /* ---- 名称过滤器 ---- */
            {
                XString* f = XString_create_utf8("Images (*.png *.jpg)");
                XFileDialog_setNameFilter(file, f);
                XString_delete_base((XClass*)f);
            }
            {
                XStringList* got = XFileDialog_nameFilters(file);
                XAPI_EXPECT(dlg_list_count(got) == 1 &&
                            dlg_list_at_eq(got, 0, "Images (*.png *.jpg)"),
                            "setNameFilter 后 nameFilters 单元素回读（对标 QFileDialog::setNameFilter/nameFilters）");
                if (got) XStringList_delete_base((XClass*)got);
            }
            {
                XStringList* filters = dlg_make_list2("C 源码 (*.c)",
                                                      "文本 (*.txt)");
                XStringList* got;
                XString* sel;
                XFileDialog_setNameFilters(file, filters);
                XStringList_delete_base((XClass*)filters);
                got = XFileDialog_nameFilters(file);
                XAPI_EXPECT(dlg_list_count(got) == 2,
                            "setNameFilters 两元素回读（对标 QFileDialog::setNameFilters）");
                if (got) XStringList_delete_base((XClass*)got);
                sel = XString_create_utf8("文本 (*.txt)");
                XFileDialog_selectNameFilter(file, sel);
                XString_delete_base((XClass*)sel);
                XAPI_EXPECT(dlg_str_eq_take(
                                XFileDialog_selectedNameFilter(file),
                                "文本 (*.txt)"),
                            "selectNameFilter→selectedNameFilter 往返（对标 QFileDialog::selectNameFilter/selectedNameFilter）");
            }

            /* ---- 目录 / 选中文件 ---- */
            {
                XString* d = XString_create_utf8("doc");
                XFileDialog_setDirectory(file, d);
                XString_delete_base((XClass*)d);
            }
            XAPI_EXPECT(dlg_str_eq_take(XFileDialog_directory(file), "doc"),
                        "setDirectory→directory 往返（对标 QFileDialog::setDirectory/directory）");
            {
                XString* a = XString_create_utf8("a.txt");
                XString* b = XString_create_utf8("b.txt");
                XFileDialog_selectFile(file, a);
                XFileDialog_selectFile(file, b);
                XString_delete_base((XClass*)a);
                XString_delete_base((XClass*)b);
            }
            {
                XStringList* sf = XFileDialog_selectedFiles(file);
                XAPI_EXPECT(dlg_list_count(sf) == 2 &&
                            dlg_list_at_eq(sf, 0, "a.txt"),
                            "selectFile 两次累积并按序回读（对标 QFileDialog::selectFile/selectedFiles）");
                XAPI_EXPECT(dlg_str_eq_take(XFileDialog_selectedFile(file),
                                            "a.txt"),
                            "selectedFile 返回首项 a.txt（对标 QFileDialog::selectedFile 即 selectedFiles 首元素）");
                {
                    XStringList* urls = XFileDialog_selectedUrls(file);
                    XAPI_EXPECT(dlg_list_count(urls) == 2,
                                "selectedUrls 与 selectedFiles 同数（对标 selectedUrls 由选中文件派生）");
                    if (urls) XStringList_delete_base((XClass*)urls);
                }
                if (sf) XStringList_delete_base((XClass*)sf);
            }

            /* ---- 默认后缀 / 标签文本 ---- */
            {
                XString* s = XString_create_utf8("png");
                XFileDialog_setDefaultSuffix(file, s);
                XString_delete_base((XClass*)s);
            }
            XAPI_EXPECT(dlg_str_eq_take(XFileDialog_defaultSuffix(file), "png"),
                        "setDefaultSuffix→defaultSuffix 往返（对标 QFileDialog::setDefaultSuffix）");
            {
                XString* s = XString_create_utf8("文件名：");
                XFileDialog_setLabelText(file, XFileDialogDialogLabel_FileName,
                                         s);
                XString_delete_base((XClass*)s);
            }
            XAPI_EXPECT(dlg_str_eq_take(
                            XFileDialog_labelText(
                                file, XFileDialogDialogLabel_FileName),
                            "文件名：") &&
                        dlg_str_eq_take(
                            XFileDialog_labelText(
                                file, XFileDialogDialogLabel_FileType), ""),
                        "setLabelText(FileName) 回读、未设 FileType 仍空（对标 QFileDialog::setLabelText/labelText）");

            /* ---- 简单过滤器 / MIME / 历史 / 侧栏 / 协议 ---- */
            {
                XString* s = XString_create_utf8("(*.cpp)");
                XFileDialog_setFilter(file, s);
                XString_delete_base((XClass*)s);
            }
            XAPI_EXPECT(dlg_str_eq_take(XFileDialog_filter(file), "(*.cpp)"),
                        "setFilter→filter 往返（对标 Qt5 遗留 QFileDialog::setFilter/filter，本库保留）");
            {
                XStringList* mime = dlg_make_list2("text/plain", "text/html");
                XStringList* got;
                XString* sel = XString_create_utf8("text/html");
                XFileDialog_setMimeTypeFilters(file, mime);
                XStringList_delete_base((XClass*)mime);
                got = XFileDialog_mimeTypeFilters(file);
                XAPI_EXPECT(dlg_list_count(got) == 2,
                            "setMimeTypeFilters 两元素回读（对标 QFileDialog::setMimeTypeFilters）");
                if (got) XStringList_delete_base((XClass*)got);
                XFileDialog_selectMimeTypeFilter(file, sel);
                XString_delete_base((XClass*)sel);
                XAPI_EXPECT(dlg_str_eq_take(
                                XFileDialog_selectedMimeTypeFilter(file),
                                "text/html"),
                            "selectMimeTypeFilter→selectedMimeTypeFilter 往返（对标 QFileDialog 同名属性）");
            }
            {
                XStringList* history = dlg_make_list1("/tmp");
                XStringList* got;
                XFileDialog_setHistory(file, history);
                XStringList_delete_base((XClass*)history);
                got = XFileDialog_history(file);
                XAPI_EXPECT(dlg_list_count(got) == 1,
                            "setHistory→history 单元素回读（对标 QFileDialog::setHistory/history）");
                if (got) XStringList_delete_base((XClass*)got);
                got = XFileDialog_sidebarUrls(file);
                XAPI_EXPECT(got != NULL && dlg_list_count(got) == 0,
                            "sidebarUrls 默认空列表非 NULL（对标 QFileDialog::sidebarUrls 默认空）");
                if (got) XStringList_delete_base((XClass*)got);
                got = XFileDialog_supportedSchemes(file);
                XAPI_EXPECT(got != NULL && dlg_list_count(got) == 0,
                            "supportedSchemes 默认空列表非 NULL（对标 QFileDialog::supportedSchemes 默认空）");
                if (got) XStringList_delete_base((XClass*)got);
            }

            /* ---- 目录 URL / selectUrl ---- */
            {
                XString* u = XString_create_utf8("file:///tmp");
                XFileDialog_setDirectoryUrl(file, u);
                XString_delete_base((XClass*)u);
            }
            XAPI_EXPECT(dlg_str_eq_take(XFileDialog_directoryUrl(file),
                                        "file:///tmp"),
                        "setDirectoryUrl→directoryUrl 往返（对标 QFileDialog::setDirectoryUrl）");
            {
                XString* u = XString_create_utf8("file:///tmp/x.c");
                XFileDialog_selectUrl(file, u);
                XString_delete_base((XClass*)u);
            }
            {
                XStringList* sf = XFileDialog_selectedFiles(file);
                XAPI_EXPECT(dlg_list_count(sf) == 3 &&
                            dlg_list_at_eq(sf, 2, "file:///tmp/x.c"),
                            "selectUrl 追加记录为选中文件（对标 QFileDialog::selectUrl；头文件注明落地为 selectedFiles，前有 a/b.txt 累积）");
                if (sf) XStringList_delete_base((XClass*)sf);
            }

            /* ---- saveState/restoreState（Task 2.20 已知未实现项） ---- */
#if XByteArray_ON
            {
                XByteArray* ba = XByteArray_create();
                XFileDialog_saveState(file, ba); /* 冒烟：当前输出空。 */
                XAPI_EXPECT(!XFileDialog_restoreState(file, ba),
                            "restoreState 恒 false（对标 QFileDialog::restoreState 返回是否成功；头文件注明未实现恒 false，Task 2.20）");
                XByteArray_delete_base((XClass*)ba);
            }
#endif

            /* ---- 图标提供者/代理（不透明借用承载） ---- */
            XFileDialog_setIconProvider(file, (void*)(intptr_t)0x1);
            XAPI_EXPECT(XFileDialog_iconProvider(file) == (void*)(intptr_t)0x1,
                        "setIconProvider/iconProvider 借用回读（对标 QFileDialog::setIconProvider；类型不映射仅记录）");
            XFileDialog_setIconProvider(file, NULL);
            XFileDialog_setItemDelegate(file, (void*)(intptr_t)0x2);
            XAPI_EXPECT(XFileDialog_itemDelegate(file) == (void*)(intptr_t)0x2,
                        "setItemDelegate/itemDelegate 回读（对标 QFileDialog::setItemDelegate）");
            XFileDialog_setItemDelegate(file, NULL);
            XFileDialog_setProxyModel(file, (void*)(intptr_t)0x3);
            XAPI_EXPECT(XFileDialog_proxyModel(file) == (void*)(intptr_t)0x3,
                        "setProxyModel/proxyModel 回读（对标 QFileDialog::setProxyModel）");
            XFileDialog_setProxyModel(file, NULL);

            /* ---- 信号手动直发（头文件注明测试口径） ---- */
            {
                XString* s = XString_create_utf8("a.txt");
                XStringList* ls = dlg_make_list1("a.txt");
                XFileDialog_fileSelected_signal(file, s);
                XFileDialog_filesSelected_signal(file, ls);
                XFileDialog_currentChanged_signal(file, s);
                XFileDialog_directoryEntered_signal(file, s);
                XFileDialog_filterSelected_signal(file, s);
                XString_delete_base((XClass*)s);
                XStringList_delete_base((XClass*)ls);
                XAPI_EXPECT(g_dlgSig.fileSelected == 1 &&
                            g_dlgSig.filesSelected == 1 &&
                            g_dlgSig.currentChanged == 1 &&
                            g_dlgSig.directoryEntered == 1 &&
                            g_dlgSig.filterSelected == 1,
                            "fileSelected/filesSelected/currentChanged/directoryEntered/filterSelected 手动直发各一次（头文件注明信号可手动触发供测试）");
            }

            /* ---- NULL 边界：getter 返回空副本可释放 ---- */
            XAPI_EXPECT(XFileDialog_nameFilters(NULL) != NULL,
                        "nameFilters(NULL) 返回空列表非 NULL（文档化：无效时返回空列表，调用方可释放）");
            {
                XStringList* got = XFileDialog_nameFilters(NULL);
                if (got) XStringList_delete_base((XClass*)got);
            }

            XFileDialog_delete_base(file);
        }
    }

    /* ================================================================
     * 6. XColorDialog（对标 QColorDialog）：当前色/选中色/选项/自定义与
     *    标准色表。
     *    注：getColor 静态便捷有应用实例即阻塞 exec，无头测试不调用。
     * ================================================================ */
    {
        XColor red = XColor_create_rgb(255, 0, 0, 255);
        XColor blue = XColor_create_rgb(0, 0, 255, 255);
        XColor green = XColor_create_rgb(0, 128, 0, 255);
        XColor fallbackBlack;
        XColorDialog* color;
        XColorDialog plain;
        XColor_init_rgb(&fallbackBlack, 0, 0, 0, 0);
        color = XColorDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, red,
                                       NULL, 0);
        XColorDialog_init_default(&plain, NULL); /* 栈实例：默认白。 */
        if (!color) {
            XAPI_EXPECT(0, "XColorDialog 堆构造成功");
        } else {
            dlg_sig_reset();

            /* ---- 初值 ---- */
            {
                XColor got = XColorDialog_currentColor(color);
                XAPI_EXPECT(XColor_equals(&got, &red),
                            "构造指定初色后 currentColor==红（对标 QColorDialog(parent, initial) 语义）");
            }
            {
                XColor white = XColor_create_rgb(255, 255, 255, 255);
                XColor got = XColorDialog_currentColor(&plain);
                XAPI_EXPECT(XColor_equals(&got, &white),
                            "init_default 后 currentColor==白（对标 QColorDialog 默认白色）");
            }
            {
                XColor got = XColorDialog_selectedColor(color);
                XAPI_EXPECT(XColor_equals(&got, &red),
                            "构造即写入 selectedColor==初色（本库口径；Qt 中 selectedColor 在接受前为无效色，偏差见文件头备忘）");
            }

            /* ---- setCurrentColor + currentColorChanged（实变才发射） ---- */
            XObject_connect_1((XObject*)color,
                              XSignal(XColorDialog_currentColorChanged_signal),
                              (XObject*)color, dlg_colorChangedSlot,
                              XConnectionType_Direct);
            XColorDialog_setCurrentColor(color, blue);
            {
                XColor got = XColorDialog_currentColor(color);
                XAPI_EXPECT(XColor_equals(&got, &blue) &&
                            g_dlgSig.currentColorChanged == 1 &&
                            XColor_equals(&g_dlgSig.lastColor, &blue),
                            "setCurrentColor(蓝) 回读并发射 currentColorChanged（对标 QColorDialog::setCurrentColor/currentColorChanged）");
            }
            XColorDialog_setCurrentColor(color, blue);
            XAPI_EXPECT(g_dlgSig.currentColorChanged == 1,
                        "等色重设不重复发射 currentColorChanged（对标 Qt 同色 setCurrentColor 不发信号）");

            /* ---- colorSelected 手动触发（头文件注明测试口径） ---- */
            XObject_connect_1((XObject*)color,
                              XSignal(XColorDialog_colorSelected_signal),
                              (XObject*)color, dlg_colorSelectedSlot,
                              XConnectionType_Direct);
            XColorDialog_colorSelected_signal(color, green);
            {
                XColor got = XColorDialog_selectedColor(color);
                XAPI_EXPECT(XColor_equals(&got, &green) &&
                            g_dlgSig.colorSelected == 1,
                            "colorSelected 手动触发置 selectedColor==绿（头文件注明由应用在接受动作处触发）");
            }

            /* ---- 选项位（位值与 Qt 6.8.3 qcolordialog.h 一致） ---- */
            XAPI_EXPECT(XColorDialog_options(&plain) == 0,
                        "默认 options==0（对标 QColorDialog 默认无选项位）");
            XColorDialog_setOption(&plain, XColorDialog_ShowAlphaChannel, true);
            XAPI_EXPECT(XColorDialog_testOption(&plain,
                                                XColorDialog_ShowAlphaChannel),
                        "setOption(ShowAlphaChannel)/testOption 对称（对标 QColorDialog::setOption/testOption）");
            XColorDialog_setOptions(&plain,
                                    (XColorDialogOptions)XColorDialog_NoButtons |
                                    (XColorDialogOptions)XColorDialog_DontUseNativeDialog);
            XAPI_EXPECT(XColorDialog_options(&plain) == 0x6,
                        "setOptions(NoButtons|DontUseNative)==0x6（对标 Qt 位值 0x2|0x4，头文件注明与 qcolordialog.h 一致）");

            /* ---- 自定义颜色表（16 槽） ---- */
            XAPI_EXPECT(XColorDialog_customCount(color) == 0,
                        "默认 customCount==0（本库按已设槽计数；Qt QColorDialog::customCount 恒 16，偏差见文件头备忘）");
            XColorDialog_setCustomColor(color, 0, green);
            {
                XColor got = XColorDialog_customColor(color, 0);
                XAPI_EXPECT(XColorDialog_customCount(color) == 1 &&
                            XColor_equals(&got, &green),
                            "setCustomColor(0) 后 customCount 增至 1 且回读一致（对标 QColorDialog::setCustomColor/customColor）");
            }
            {
                XColor got = XColorDialog_customColor(&plain, -1);
                XColor got2 = XColorDialog_customColor(&plain, 99);
                XAPI_EXPECT(XColor_equals(&got, &fallbackBlack) &&
                            XColor_equals(&got2, &fallbackBlack),
                            "customColor(-1/99) 越界回退黑（头文件注明越界返回黑色，Qt 为断言行为，本库安全化）");
            }

            /* ---- 标准颜色表（48 槽） ---- */
            XColorDialog_setStandardColor(&plain, 0, red);
            {
                XColor got = XColorDialog_standardColor(&plain, 0);
                XAPI_EXPECT(XColor_equals(&got, &red),
                            "setStandardColor(0) 后 standardColor 回读（对标 QColorDialog::setStandardColor/standardColor）");
            }
            {
                XColor got = XColorDialog_standardColor(&plain, -1);
                XAPI_EXPECT(XColor_equals(&got, &fallbackBlack),
                            "standardColor(-1) 越界回退黑（头文件注明越界返回黑色）");
            }

            /* ---- open：非模态显示 ---- */
            XColorDialog_open(color);
            XAPI_EXPECT(XWidget_isVisible((XWidget*)color),
                        "open() 显示对话框（对标 QDialog::open 的显示语义；本库简化为 show，头文件注明无模态循环）");
            XWidget_hide((XWidget*)color);

            /* ---- NULL 边界：返回无效色 ---- */
            {
                XColor got = XColorDialog_currentColor(NULL);
                XAPI_EXPECT(!XColor_isValid(&got),
                            "currentColor(NULL) 返回无效色（文档化：无效时返回无效 XColor）");
            }

            XColorDialog_delete_base(color);
        }
        XColorDialog_deinit_base(&plain); /* 栈实例反初始化。 */
    }

    /* ================================================================
     * 7. XProgressDialog（对标 QProgressDialog）：范围/值/autoReset/
     *    autoClose/取消/自定义子控件。
     * ================================================================ */
    {
        XProgressDialog* pd = XProgressDialog_create(NULL, 0);
        XProgressBar bar;
        if (!pd) {
            XAPI_EXPECT(0, "XProgressDialog 堆构造成功");
        } else {
            dlg_sig_reset();

            /* ---- 默认值（Qt 6.8 QProgressDialog 构造默认） ---- */
            XAPI_EXPECT(XProgressDialog_minimum(pd) == 0 &&
                        XProgressDialog_maximum(pd) == 100 &&
                        XProgressDialog_value(pd) == 0,
                        "默认范围 [0,100]、值 0（对标 QProgressDialog 默认 0..100）");
            XAPI_EXPECT(XProgressDialog_autoReset(pd) &&
                        XProgressDialog_autoClose(pd),
                        "默认 autoReset/autoClose 均 true（对标 QProgressDialog 默认）");
            XAPI_EXPECT(XProgressDialog_minimumDuration(pd) == 4000,
                        "默认 minimumDuration==4000ms（对标 QProgressDialog 默认 4 秒）");
            XAPI_EXPECT(!XProgressDialog_wasCanceled(pd),
                        "默认 wasCanceled==false（对标 QProgressDialog::wasCanceled 初始 false）");

            /* ---- 范围往返与交换 ---- */
            XProgressDialog_setRange(pd, 10, 20);
            XAPI_EXPECT(XProgressDialog_minimum(pd) == 10 &&
                        XProgressDialog_maximum(pd) == 20,
                        "setRange(10,20) 回读（对标 QProgressDialog::setRange）");
            XProgressDialog_setRange(pd, 50, 20);
            XAPI_EXPECT(XProgressDialog_minimum(pd) == 20 &&
                        XProgressDialog_maximum(pd) == 50,
                        "setRange(50,20) 交换为 (20,50)（本库头文件注明交换语义；Qt setRange 实为把 max 钳到 min，偏差见文件头备忘）");
            XProgressDialog_setRange(pd, 0, 100);

            /* ---- setValue 钳位与达最大值复位 ---- */
            XProgressDialog_setValue(pd, 150);
            XAPI_EXPECT(XProgressDialog_value(pd) == 100,
                        "setValue(150) 钳位到 maximum==100（对标 QProgressDialog::setValue 越界钳位）");
            XProgressDialog_setValue(pd, -5);
            XAPI_EXPECT(XProgressDialog_value(pd) == 0,
                        "setValue(-5) 钳位到 minimum==0（对标越界钳位）");
            XProgressDialog_setValue(pd, 40);
            XProgressDialog_setValue(pd, 100);
            XAPI_EXPECT(XProgressDialog_value(pd) == 0,
                        "setValue(100) 达最大值且 autoReset 时复位到 minimum（对标 Qt：value 达 max 且 autoReset 执行 reset）");
            XProgressDialog_setValue(pd, 60);
            XProgressDialog_setAutoReset(pd, false);
            XProgressDialog_setValue(pd, 100);
            XAPI_EXPECT(XProgressDialog_value(pd) == 100,
                        "autoReset=false 时 setValue(100) 保持 100 不复位（对标 Qt autoReset=false 关闭达峰复位）");
            XProgressDialog_setAutoReset(pd, true);

            /* ---- reset：复位到 minimum；autoClose 隐藏 ---- */
            XProgressDialog_setValue(pd, 50);
            XProgressDialog_reset(pd);
            XAPI_EXPECT(XProgressDialog_value(pd) == 0 &&
                        !XProgressDialog_wasCanceled(pd),
                        "reset() 值复位到 minimum 且清取消标记（对标 QProgressDialog::reset）");
            XWidget_show((XWidget*)pd);
            XProgressDialog_reset(pd);
            XAPI_EXPECT(!XWidget_isVisible((XWidget*)pd),
                        "autoClose=true 时 reset() 隐藏对话框（对标 Qt reset 在 autoClose 时 hide）");

            /* ---- cancel：canceled 信号 + wasCanceled + 复位 + 强制隐藏 ---- */
            XObject_connect_1((XObject*)pd,
                              XSignal(XProgressDialog_canceled_signal),
                              (XObject*)pd, dlg_canceledSlot,
                              XConnectionType_Direct);
            XProgressDialog_setValue(pd, 70);
            XWidget_show((XWidget*)pd);
            XProgressDialog_cancel(pd);
            XAPI_EXPECT(XProgressDialog_wasCanceled(pd) &&
                        g_dlgSig.canceled == 1 &&
                        XProgressDialog_value(pd) == 0,
                        "cancel() 置 wasCanceled、发射 canceled() 并复位（对标 Qt 取消的可观察效果：canceled→复位）");
            XAPI_EXPECT(!XWidget_isVisible((XWidget*)pd),
                        "cancel() 无条件隐藏（对标 Qt 取消时 forceHide，即使 autoClose=false）");

            /* ---- init_full 完整构造（栈实例，避免对已构造堆对象二次
             *      init；对标 QProgressDialog 完整参构造） ---- */
            {
                XProgressDialog pd2;
                XString* lt = XString_create_utf8("处理中");
                XString* ct = XString_create_utf8("停止");
                XProgressDialog_init_full(&pd2, lt, ct, 10, 20, NULL);
                XString_delete_base((XClass*)lt);
                XString_delete_base((XClass*)ct);
                XAPI_EXPECT(XProgressDialog_minimum(&pd2) == 10 &&
                            XProgressDialog_maximum(&pd2) == 20,
                            "init_full(标签,取消文本,10,20) 设置范围（对标 QProgressDialog 完整参构造）");
                XProgressDialog_deinit_base(&pd2);
            }

            /* ---- 标签文本往返 ---- */
            {
                XString* t = XString_create_utf8("复制中...");
                XProgressDialog_setLabelText(pd, t);
                XString_delete_base((XClass*)t);
            }
            XAPI_EXPECT(dlg_str_eq_take(XProgressDialog_labelText(pd),
                                        "复制中..."),
                        "setLabelText→labelText 往返（对标 QProgressDialog::setLabelText/labelText）");

            /* ---- 自定义标签（所有权转移 + 文本转发） ---- */
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON
            {
                XLabel* label = XLabel_create(NULL, 0);
                XProgressDialog_setLabel(pd, label);
                XAPI_EXPECT(XProgressDialog_label(pd) == label,
                            "setLabel 后 label 回读（对标 QProgressDialog::setLabel 接管所有权）");
                {
                    XString* t = XString_create_utf8("自定义标签文本");
                    XProgressDialog_setLabelText(pd, t);
                    XString_delete_base((XClass*)t);
                }
                XAPI_EXPECT(dlg_str_eq_take(XProgressDialog_labelText(pd),
                                            "自定义标签文本"),
                            "存在自定义标签时 labelText 转发到标签（对标 Qt：setLabel 后 labelText 读标签文本）");
                /* label 所有权已转移给对话框，随 pd 释放，不再手动删。 */
            }
#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */

            /* ---- 自定义取消按钮（所有权转移 + 点击→cancel 链） ---- */
#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON
            {
                XPushButton* btn = XPushButton_create(NULL, 0);
                XProgressDialog_setCancelButton(pd, btn);
                XAPI_EXPECT(XProgressDialog_cancelButton(pd) == btn,
                            "setCancelButton 后 cancelButton 回读（对标 QProgressDialog::setCancelButton 接管所有权）");
                {
                    XString* t = XString_create_utf8("停止");
                    XProgressDialog_setCancelButtonText(pd, t);
                    XString_delete_base((XClass*)t);
                }
                XAPI_EXPECT(strcmp(xapi_u8(
                                 XAbstractButton_text((XAbstractButton*)btn)),
                                 "停止") == 0,
                            "setCancelButtonText 转发到自定义按钮文本（对标 Qt：存在取消按钮时文本转发）");
                dlg_sig_reset();
                XProgressDialog_setValue(pd, 30);
                XAbstractButton_click((XAbstractButton*)btn);
                XAPI_EXPECT(XProgressDialog_wasCanceled(pd) &&
                            g_dlgSig.canceled == 1 &&
                            XProgressDialog_value(pd) == 0,
                            "点击自定义取消按钮走 cancel() 链（对标 Qt clicked→canceled→cancel 链路）");
                XProgressDialog_setCancelButton(pd, NULL);
                XAPI_EXPECT(XProgressDialog_cancelButton(pd) == NULL,
                            "setCancelButton(NULL) 移除取消按钮（对标 Qt：NULL 表示不再显示取消按钮；旧按钮随对话框释放）");
            }
#endif /* XWIDGET_ON && XABSTRACTBUTTON_ON && XPUSHBUTTON_ON */

            /* ---- setBar：仅借用存储，无 getter，冒烟不崩溃 ---- */
            XProgressBar_init(&bar, NULL, 0);
            XProgressDialog_setBar(pd, &bar);
            XProgressDialog_setBar(pd, NULL);
            XProgressBar_deinit_base(&bar);
            XAPI_EXPECT(1, "setBar 借用登记/清除冒烟通过（对标 QProgressDialog::setBar；本库无 getter 仅存储，头文件注明）");

            /* ---- forceShow ---- */
            XWidget_hide((XWidget*)pd);
            XProgressDialog_forceShow(pd);
            XAPI_EXPECT(XWidget_isVisible((XWidget*)pd),
                        "forceShow 立即显示（对标 QProgressDialog::forceShow）");
            XWidget_hide((XWidget*)pd);

            /* ---- NULL 边界（文档化 NULL 安全） ---- */
            XAPI_EXPECT(XProgressDialog_minimum(NULL) == 0 &&
                        XProgressDialog_maximum(NULL) == 100 &&
                        XProgressDialog_value(NULL) == 0 &&
                        !XProgressDialog_wasCanceled(NULL) &&
                        XProgressDialog_autoReset(NULL) &&
                        XProgressDialog_autoClose(NULL) &&
                        XProgressDialog_minimumDuration(NULL) == 4000,
                        "NULL 入参 getter 安全（文档化：NULL 返回 Qt 默认值）");

            XProgressDialog_delete_base(pd);
        }
    }

#if XWIDGET_ON && XWIZARD_ON

    /* ================================================================
     * 8. XWizard + XWizardPage（对标 QWizard/QWizardPage）：页属性、
     *    四虚槽默认、导航（next/back/restart）、访问史、字段、按钮。
     * ================================================================ */
    {
        XWizardPage* p0 = XWizardPage_create(NULL, 0);
        XWizardPage* p1 = NULL;
        XWizardPage* p2 = NULL;
        XWizardPage* p3 = NULL;
        XWizard* wiz;
        int ids[4];
        if (!p0) {
            XAPI_EXPECT(0, "XWizardPage 堆构造成功");
            wiz = NULL;
        } else {
            /* ---- XWizardPage 默认值与页属性 ---- */
            XAPI_EXPECT(xapi_cstr(XWizardPage_title(p0))[0] == '\0' &&
                        xapi_cstr(XWizardPage_subTitle(p0))[0] == '\0',
                        "页默认 title/subTitle 空串（对标 QWizardPage 默认无标题）");
            XWizardPage_setTitle(p0, "第 1 步");
            XWizardPage_setSubTitle(p0, "请填写资料");
            XAPI_EXPECT(strcmp(xapi_cstr(XWizardPage_title(p0)), "第 1 步") == 0 &&
                        strcmp(xapi_cstr(XWizardPage_subTitle(p0)), "请填写资料") == 0,
                        "setTitle/setSubTitle 回读（对标 QWizardPage::setTitle/setSubTitle）");
            XAPI_EXPECT(XWizardPage_isComplete(p0),
                        "页默认 isComplete==true（对标 Qt 6.8.3 QWizardPage::isComplete 默认 true）");
            XWizardPage_setComplete(p0, false);
            XAPI_EXPECT(!XWizardPage_isComplete(p0),
                        "setComplete(false) 回读（对标 QWizardPage::setComplete）");
            XWizardPage_setComplete(p0, true);
            XAPI_EXPECT(!XWizardPage_isCommitPage(p0) &&
                        !XWizardPage_isFinalPage(p0),
                        "页默认非提交页/非强制末页（对标 QWizardPage::isCommitPage/isFinalPage 默认 false）");
            XWizardPage_setCommitPage(p0, true);
            XWizardPage_setFinalPage(p0, true);
            XAPI_EXPECT(XWizardPage_isCommitPage(p0) &&
                        XWizardPage_isFinalPage(p0),
                        "setCommitPage/setFinalPage 回读（对标 QWizardPage 同名属性；本库仅存储状态）");
            XWizardPage_setCommitPage(p0, false);
            XWizardPage_setFinalPage(p0, false);
            XAPI_EXPECT(XWizardPage_validatePage(p0),
                        "validatePage 默认实现返回 true（对标 Qt 6.8.3 默认校验通过）");
            XAPI_EXPECT(XWizardPage_nextId(p0) == -1,
                        "无所属向导时 nextId 默认 -1（对标 QWizardPage::nextId 无 wizard 返回 -1）");
            XAPI_EXPECT(XWizardPage_wizard(p0) == NULL,
                        "页默认 wizard()==NULL（对标 QWizardPage::wizard 未加入向导）");
            XAPI_EXPECT(xapi_cstr(XWizardPage_buttonText(p0,
                                                XWizardButton_NextButton))[0] == '\0',
                        "页默认无自定义按钮文本（对标 QWizardPage::buttonText 未设为空）");
            XWizardPage_setButtonText(p0, XWizardButton_NextButton, "前进");
            XAPI_EXPECT(strcmp(XWizardPage_buttonText(p0,
                                                      XWizardButton_NextButton),
                               "前进") == 0,
                        "setButtonText(Next, 前进) 页级回读（对标 QWizardPage::setButtonText/buttonText）");
            XWizardPage_setPixmap_2(p0, 0, "banner.png");
            {
                const XString* px = XWizardPage_pixmap(p0, 0);
                /* 实现口径：本库页面图为单槽承载（XWizardPage_setPixmap
                 * 头文件注明"which 参数保留，当前单图承载"），未按 Qt 分
                 * Watermark/Logo/Banner/Background 四槽独立存储——任一
                 * which 读到的都是同一张图，非 Qt"未设槽位返回 NULL"。 */
                XAPI_EXPECT(px && strcmp(xapi_u8(px), "banner.png") == 0 &&
                            XWizardPage_pixmap(p0, 1) == px,
                            "setPixmap_2 回读（实现口径：页面图单槽承载，which 参数保留见 XWizard.h；Qt 为四槽独立存储）");
            }
            /* completeChanged：setComplete 实变时发射（对标
             * QWizardPage::completeChanged 联动向导按钮刷新）。 */
            XObject_connect_1((XObject*)p0,
                              XSignal(XWizardPage_completeChanged_signal),
                              (XObject*)p0, dlg_completeChangedSlot,
                              XConnectionType_Direct);
            XWizardPage_setComplete(p0, false);
            XWizardPage_setComplete(p0, true);
            XAPI_EXPECT(g_dlgSig.completeChanged == 2,
                        "setComplete 两次实变发射 completeChanged 两次（对标 completeChanged 由 setComplete 触发）");

            /* ---- XWizard 默认值 ---- */
            wiz = XWizard_create(NULL, 0);
            if (!wiz) {
                XAPI_EXPECT(0, "XWizard 堆构造成功");
            } else {
                dlg_sig_reset();
                XObject_connect_1((XObject*)wiz,
                                  XSignal(XWizard_currentIdChanged_signal),
                                  (XObject*)wiz, dlg_currentIdChangedSlot,
                                  XConnectionType_Direct);
                XObject_connect_1((XObject*)wiz,
                                  XSignal(XWizard_pageAdded_signal),
                                  (XObject*)wiz, dlg_pageAddedSlot,
                                  XConnectionType_Direct);
                XObject_connect_1((XObject*)wiz,
                                  XSignal(XWizard_pageRemoved_signal),
                                  (XObject*)wiz, dlg_pageRemovedSlot,
                                  XConnectionType_Direct);
                XAPI_EXPECT(XWizard_wizardStyle(wiz) ==
                            XWizardStyle_ClassicStyle,
                            "向导默认样式==ClassicStyle(0)（对标 QWizard 默认 ClassicStyle）");
                XWizard_setWizardStyle(wiz, XWizardStyle_AeroStyle);
                XAPI_EXPECT(XWizard_wizardStyle(wiz) == XWizardStyle_AeroStyle,
                            "setWizardStyle(AeroStyle) 回读（对标 QWizard::setWizardStyle）");
                XAPI_EXPECT(XWizard_options(wiz) == 0,
                            "向导默认 options==0（对标 QWizard 默认无选项）");
                XWizard_setOption(wiz, XWizardOption_HaveHelpButton, true);
                XAPI_EXPECT(XWizard_testOption(wiz,
                                               XWizardOption_HaveHelpButton),
                            "setOption(HaveHelpButton)/testOption 对称（对标 QWizard::setOption/testOption）");
                XWizard_setOptions(wiz, 0);
                XAPI_EXPECT(XWizard_options(wiz) == 0,
                            "setOptions(0) 整体复位（对标 QWizard::setOptions 覆盖式）");

                /* ---- 内建按钮与登记按钮（内建按钮依赖 XPUSHBUTTON_ON） ---- */
#if XPUSHBUTTON_ON
                XAPI_EXPECT(XWizard_button(wiz, XWizardButton_BackButton) != NULL &&
                            XWizard_button(wiz, XWizardButton_NextButton) != NULL &&
                            XWizard_button(wiz, XWizardButton_FinishButton) != NULL &&
                            XWizard_button(wiz, XWizardButton_CancelButton) != NULL,
                            "内建 上一页/下一页/完成/取消 按钮反查非空（对标 QWizard::button 返回内建导航按钮）");
                {
                    struct XAbstractButton* custom =
                        (struct XAbstractButton*)XPushButton_create(NULL, 0);
                    XWizard_setButton(wiz, XWizardButton_CommitButton, custom);
                    XAPI_EXPECT(XWizard_button(wiz,
                                               XWizardButton_CommitButton) == custom,
                                "setButton 登记 Commit 槽后 button 反查（对标 QWizard::setButton/button；本库为借用承载）");
                    XWizard_setButton(wiz, XWizardButton_CommitButton, NULL);
                    XAPI_EXPECT(XWizard_button(wiz,
                                               XWizardButton_CommitButton) == NULL,
                                "setButton(NULL) 清除登记（头文件注明 NULL 恢复内建）");
                    XWidget_delete_base((XWidget*)custom);
                }
                {
                    struct XAbstractButton* nextBtn =
                        XWizard_button(wiz, XWizardButton_NextButton);
                    const XString* nextText = nextBtn
                        ? XAbstractButton_text(nextBtn) : NULL;
                    XAPI_EXPECT(nextText &&
                                strcmp(xapi_u8(nextText),
                                       "下一页 >") == 0,
                                "内建下一页按钮回退文本=下一页 >（本库中文默认文本表，对标 Qt 经翻译的默认按钮文本）");
                }
#endif /* XPUSHBUTTON_ON */
                XAPI_EXPECT(XWizard_button(wiz, XWizardButton_CommitButton) == NULL,
                            "button(CommitButton)==NULL（头文件注明本版未创建 Commit 专用按钮）");
                XAPI_EXPECT(XWizard_button(wiz, XWizardButton_HelpButton) == NULL,
                            "button(HelpButton)==NULL（帮助按钮仅 init 预设选项才创建，本库 init 恒 options=0，文档化简化）");
                XAPI_EXPECT(xapi_cstr(XWizard_buttonText(wiz,
                                               XWizardButton_CancelButton))[0] == '\0',
                            "向导级 buttonText 默认空（自定义层未设；内建按钮显示回退文本）");
                XWizard_setButtonText(wiz, XWizardButton_NextButton, "继续");
                XAPI_EXPECT(strcmp(XWizard_buttonText(wiz,
                                                      XWizardButton_NextButton),
                                   "继续") == 0,
                            "setButtonText(Next, 继续) 向导级回读（对标 QWizard::setButtonText/buttonText）");
                XWizard_setButtonText(wiz, XWizardButton_NextButton, "");

                /* ---- 页登记与导航 ---- */
                XAPI_EXPECT(XWizard_addPage(wiz, p0) == 0,
                            "addPage(p0) 返回页索引 0（对标 QWizard::addPage 返回页 id）");
                p1 = XWizardPage_create(NULL, 0);
                p2 = XWizardPage_create(NULL, 0);
                XWizard_addPage(wiz, p1);
                XWizard_addPage(wiz, p2);
                XAPI_EXPECT(XWizard_pageCount(wiz) == 3 &&
                            XWizard_page(wiz, 1) == p1,
                            "addPage×3 后 pageCount==3 且 page(1) 反查（对标 QWizard::pageCount/page）");
                XAPI_EXPECT(XWizardPage_wizard(p1) == wiz,
                            "addPage 后页 wizard()==向导（对标 QWizardPage::wizard 登记归属）");
                XAPI_EXPECT(XWizard_currentId(wiz) == 0 &&
                            XWizard_currentPage(wiz) == p0 &&
                            XWizard_hasVisitedPage(wiz, 0),
                            "首页挂载即进入：currentId==0 且已访问（本库 addPage 首页立即显示并标记访问）");
                XAPI_EXPECT(p0->m_initialized,
                            "首页挂载即触发 initializePage（对标 QWizard showEvent 首进触发 initializePage；经公开结构字段观察）");
                XAPI_EXPECT(g_dlgSig.pageAdded == 3,
                            "pageAdded(index) 发射三次（对标 QWizard::pageAdded，Qt 6.5+ 信号）");
                XWizard_setCurrentIndex(wiz, 1);
                XAPI_EXPECT(XWizard_currentId(wiz) == 1 &&
                            g_dlgSig.currentIdChanged == 1 &&
                            g_dlgSig.lastCurrentId == 1,
                            "setCurrentIndex(1) 切页并发射 currentIdChanged(1)（对标 QWizard::currentIdChanged 载荷）");
                XWizard_setCurrentIndex(wiz, -1);
                XWizard_setCurrentIndex(wiz, 99);
                XAPI_EXPECT(XWizard_currentId(wiz) == 1,
                            "setCurrentIndex 越界索引被忽略（头文件注明越界忽略，对标 setCurrentId 非法 id 不动）");
                XAPI_EXPECT(XWizard_hasVisitedPage(wiz, 0) &&
                            XWizard_hasVisitedPage(wiz, 1) &&
                            !XWizard_hasVisitedPage(wiz, 2),
                            "访问史：0/1 已访问、2 未访问（对标 QWizard::hasVisitedPage）");
                XAPI_EXPECT(XWizard_visitedIds(wiz, NULL, 0) == 2,
                            "visitedIds 计数模式==2（对标 QWizard::visitedIds 数量）");
                XAPI_EXPECT(XWizard_visitedIds(wiz, ids, 4) == 2 &&
                            ids[0] == 0 && ids[1] == 1,
                            "visitedIds 写入模式按升序 {0,1}（头文件注明按索引升序）");
                XAPI_EXPECT(XWizard_pageIds(wiz, NULL, 0) == 3,
                            "pageIds 计数==3（对标 QWizard::pageIds 全部页 id）");
                XAPI_EXPECT(XWizard_pageIds(wiz, ids, 4) == 3 &&
                            ids[2] == 2,
                            "pageIds 写入模式 {0,1,2}（头文件注明覆盖全部已登记页）");

                /* ---- next/back 与校验 ---- */
                XWizard_next(wiz);
                XAPI_EXPECT(XWizard_currentId(wiz) == 2,
                            "next() 经校验后进至页 2（对标 QWizard::next：validateCurrentPage→nextId→前进）");
                XWizard_next(wiz);
                XAPI_EXPECT(XWizard_currentId(wiz) == 2,
                            "末页 next() 不再前进（对标 QWizard 末页 next 无后续页不动）");
                XWizardPage_setComplete(p2, false);
                XAPI_EXPECT(!XWizard_validateCurrentPage(wiz),
                            "当前页 isComplete=false 时校验判负（对标 QWizard::validateCurrentPage 先查 complete）");
                XWizard_next(wiz);
                XAPI_EXPECT(XWizard_currentId(wiz) == 2,
                            "校验不过 next() 停留当前页（对标 QWizard next 被校验阻塞）");
                XWizardPage_setComplete(p2, true);
                XAPI_EXPECT(XWizard_validateCurrentPage(wiz),
                            "恢复 complete 后校验通过（分派 validatePage 默认 true）");
                XWizard_back(wiz);
                XWizard_back(wiz);
                XAPI_EXPECT(XWizard_currentId(wiz) == 0,
                            "back() 两步回到页 0（对标 QWizard::back 逐页后退）");
                XWizard_back(wiz);
                XAPI_EXPECT(XWizard_currentId(wiz) == 0,
                            "首页 back() 不动（对标 QWizard 首页无上页）");
                XAPI_EXPECT(!p1->m_initialized,
                            "back 离开页 1 时 cleanupPage 并复位初始化标记（对标 QWizard 后退触发 cleanupPage；结构字段观察）");
                XWizard_setOption(wiz, XWizardOption_IndependentPages, true);
                XWizard_setCurrentIndex(wiz, 1);
                XWizard_back(wiz);
                XAPI_EXPECT(p1->m_initialized,
                            "IndependentPages 时 back 跳过 cleanupPage（对标 Qt 该选项下页面独立不清理；头文件注明）");
                XWizard_setOption(wiz, XWizardOption_IndependentPages, false);

                /* ---- restart 与起始页 ---- */
                XWizard_setStartIndex(wiz, 1);
                XAPI_EXPECT(XWizard_startIndex(wiz) == 1,
                            "setStartIndex(1)/startIndex 回读（对标 QWizard::setStartId/startId；宏别名同一实现）");
                XWizard_restart(wiz);
                XAPI_EXPECT(XWizard_currentId(wiz) == 1 &&
                            !XWizard_hasVisitedPage(wiz, 0) &&
                            XWizard_hasVisitedPage(wiz, 1),
                            "restart 后回到起始页 1 且访问史清零（对标 QWizard::restart 复位历史）");
                XAPI_EXPECT(p1->m_initialized,
                            "restart 重新触发起始页 initializePage（对标 restart 复位 initialized 后重进）");
                XWizard_setStartIndex(wiz, 0);

                /* ---- removePage / setPage ---- */
                p3 = XWizardPage_create(NULL, 0);
                XWizard_addPage(wiz, p3);
                XWizard_removePage(wiz, 0);
                XAPI_EXPECT(XWizard_pageCount(wiz) == 3 &&
                            XWizard_page(wiz, 0) == p1 &&
                            XWizardPage_wizard(p0) == NULL,
                            "removePage(0) 后页表左移且被移除页解除归属（对标 QWizard 移除页向导置空；头文件注明）");
                XAPI_EXPECT(g_dlgSig.pageRemoved == 1,
                            "pageRemoved(index) 发射（对标 QWizard 移除页通知）");
                XWizard_setPage(wiz, 3, p0);
                XAPI_EXPECT(XWizard_pageCount(wiz) == 4 &&
                            XWizard_page(wiz, 3) == p0,
                            "setPage(3, p0) 稀疏扩展 pageCount==4（对标 QWizard::setPage 按 id 登记）");
                XWizard_removePage(wiz, 3);

                /* ---- 字段 / 横幅 / 侧边 / 格式 ---- */
                XWizard_setField_2(wiz, "user", "xinyue");
                XAPI_EXPECT(XWizard_field_2(wiz, "user") &&
                            strcmp(XWizard_field_2(wiz, "user"),
                                   "xinyue") == 0,
                            "setField_2/field_2 往返（对标 QWizard::setField/field 跨页字段）");
                XAPI_EXPECT(XWizard_field_2(wiz, "缺失") == NULL,
                            "field_2 未登记字段返回 NULL（对标 Qt field 未知字段）");
                XWizard_setField_2(wiz, "user", NULL);
                XAPI_EXPECT(XWizard_field_2(wiz, "user") == NULL,
                            "setField_2(NULL) 清空字段（头文件注明可为 NULL 清空）");
                XWizard_setPixmap_2(wiz, 0, "wizard.png");
                {
                    const XString* px = XWizard_pixmap(wiz, 0);
                    XAPI_EXPECT(px && strcmp(xapi_u8(px),
                                             "wizard.png") == 0,
                                "setPixmap_2/pixmap 横幅路径回读（对标 QWizard::setPixmap/pixmap）");
                }
                {
                    XWidget side;
                    XWidget_init(&side, NULL, 0);
                    XWizard_setSideWidget(wiz, &side);
                    XAPI_EXPECT(XWizard_sideWidget(wiz) == &side,
                                "setSideWidget 借用回读（对标 QWizard::setSideWidget）");
                    XWizard_setSideWidget(wiz, NULL);
                    XAPI_EXPECT(XWizard_sideWidget(wiz) == NULL,
                                "setSideWidget(NULL) 清除（借用方自持生命周期）");
                    XWidget_deinit_base(&side);
                }
                XAPI_EXPECT(XWizard_titleFormat(wiz) == 0 &&
                            XWizard_subTitleFormat(wiz) == 0,
                            "标题/子标题格式默认 0（本库默认 PlainText；Qt 默认 AutoText==2，偏差见文件头备忘）");
                XWizard_setTitleFormat(wiz, 1);
                XWizard_setSubTitleFormat(wiz, 1);
                XAPI_EXPECT(XWizard_titleFormat(wiz) == 1 &&
                            XWizard_subTitleFormat(wiz) == 1,
                            "setTitleFormat/setSubTitleFormat 回读（对标 QWizard 同名属性）");
                XWizard_setButtonLayout(wiz, 1);
                XAPI_EXPECT(1, "setButtonLayout 冒烟通过（对标 QWizard::setButtonLayout；本库无 getter，头文件注明仅存储）");
                XWizard_setDefaultProperty(wiz, "XLineEdit", NULL);
                XWizard_setDefaultProperty(wiz, "XLineEdit", (void*)(intptr_t)0x1);
                XAPI_EXPECT(1, "setDefaultProperty 同名重复登记冒烟通过（对标 QWizard::setDefaultProperty 覆盖语义；本库无 getter 且容量 16）");

                /* ---- 手动直发信号（头文件注明测试口径） ---- */
                dlg_sig_reset();
                XWizard_helpRequested_signal(wiz);
                XWizard_customButtonClicked_signal(wiz, 1);
                XAPI_EXPECT(1, "helpRequested/customButtonClicked 手动直发可安全调用（对标 QWizard 同名信号，发射经 XObject_emitSignal）");

                /* ---- done：结果码 + 隐藏 ---- */
                XWidget_show((XWidget*)wiz);
                XWizard_done(wiz, 1);
                XAPI_EXPECT(XDialog_result(&wiz->m_base) == 1 &&
                            !XWidget_isVisible((XWidget*)wiz),
                            "done(1) 设置结果码并隐藏（对标 QWizard::done 委托 QDialog::done）");

                XWizard_delete_base(wiz); /* 页 p0..p3 已入向导子树，级联释放。 */
                wiz = NULL;
                p0 = NULL;
                p1 = NULL;
                p2 = NULL;
                p3 = NULL;
            }
        }
        (void)ids;
    }

#endif /* XWIDGET_ON && XWIZARD_ON */

#endif /* XWIDGET_ON && XDIALOG_ON */

#if !XWIDGET_ON || !XDIALOG_ON
    /* 对话框模块整体裁剪时的哨兵断言：保持入口语义（0=全过）。 */
    XAPI_EXPECT(1, "dialogs 族：XDIALOG_ON 裁剪环境下无可用 API，哨兵通过");
    (void)failures;
#endif

    XPrintf("XGuiApiTest: [dialogs 族] %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}
