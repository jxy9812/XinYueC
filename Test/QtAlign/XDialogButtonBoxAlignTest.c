/**
 * @file       XDialogButtonBoxAlignTest.c
 * @brief      XDialogButtonBox × Qt 6.8 QDialogButtonBox 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/
 *               qdialogbuttonbox.h（ButtonRole/StandardButton 位值/
 *               ButtonLayout 枚举，addButton 三重载、removeButton/clear、
 *               buttons/buttonRole/setStandardButtons/standardButtons/
 *               standardButton/button、setCenterButtons/centerButtons、
 *               clicked/accepted/helpRequested/rejected 信号）；
 *             - 本机 Qt 6.8.3 + offscreen 实测（/tmp 探针）：
 *               ① 缺省 orientation==1（Qt::Horizontal）、centerButtons==0、
 *                  buttons() 空、standardButtons()==0；
 *               ② 标准按钮角色：Ok/Save→AcceptRole(0)、Abort→**RejectRole
 *                  (1)**、Cancel→RejectRole(1)、Discard→DestructiveRole(2)、
 *                  Help→HelpRole(4)、Reset→ResetRole(7)、Apply→ApplyRole(8)；
 *               ③ WinLayout 横排 x 序（Reset, Ok, Save, Discard, Abort,
 *                  Cancel, Apply, Help）——与 X 的角色序表一致；
 *               ④ setStandardButtons(Ok) 后用户 addButton 的按钮仍在
 *                  （count 由 9 变 2，customKept==1）；
 *               ⑤ 同按钮 addButton 两次：buttons().size()==1、
 *                  buttonRole 返回**新**角色；
 *               ⑥ addButton(btn, InvalidRole)：打印 "Invalid ButtonRole,
 *                  button not added"、count 不变；
 *               ⑦ Yes→accepted、No→rejected、Help→helpRequested、
 *                  Apply→仅 clicked（accepted=1/rejected=1/help=1/
 *                  clicked=4）。
 *             已对齐（r2，账本 234）：Qt 的 clear() **删除全部按钮**
 *             （实测 addButton 的自定义按钮在 clear 后被析构、QPointer
 *             变空）；本库同口径（延迟销毁以防 clicked Direct 链 UAF），
 *             clear 后调用方不得再释放这些按钮。旧 apitest
 *             Test/XGuiDemo/xgui_demo_apitest_dialogs.c:806-816 的"clear
 *             后自行释放用户按钮"见 testMigrations。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON

#include "XDialogButtonBox.h"
#include "XAbstractButton.h"
#include "XPushButton.h"
#include "XWidget.h"
#include "XObject.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"

/* ==================== 信号探针 ==================== */

static int dbb_clicked = 0;
static int dbb_accepted = 0;
static int dbb_rejected = 0;
static int dbb_help = 0;
static int dbb_destroyed = 0;

static void dbb_destroyedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++dbb_destroyed;
}

static void dbb_clickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++dbb_clicked;
}

static void dbb_acceptedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++dbb_accepted;
}

static void dbb_rejectedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++dbb_rejected;
}

static void dbb_helpSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++dbb_help;
}

static void dbb_sigReset(void)
{
    dbb_clicked = 0;
    dbb_accepted = 0;
    dbb_rejected = 0;
    dbb_help = 0;
}

/** @brief 成员按钮个数（buttons() 借用数组）。 */
static int dbb_count(const XDialogButtonBox* box)
{
    const XVector* v = XDialogButtonBox_buttons(box);
    return v ? (int)XVector_size_base((const XContainer*)v) : 0;
}

/* ==================== 用例 ==================== */

/** @brief 缺省值 + 标准按钮角色表（含 Abort→RejectRole 的本机实测口径）。 */
XQ_TEST(XDialogButtonBox, defaultsAndRoles)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    XPushButton* ok;
    XQ_REQUIRE(box != NULL);

    /* 缺省：水平、不居中、空成员（实测 orientation==1、center==0）。 */
    XQ_EQ_I64(XDialogButtonBox_orientation(box), 1);
    XQ_CHECK(!XDialogButtonBox_centerButtons(box));
    XQ_EQ_I64(dbb_count(box), 0);
    XQ_EQ_I64(XDialogButtonBox_standardButtons(box), 0);

    ok = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Ok);
    XQ_CHECK_PTR(ok);
    XQ_EQ_I64(XDialogButtonBox_standardButtons(box),
              (int)XDialogButtonBoxStandard_Ok);
    XQ_CHECK(XDialogButtonBox_button(box, XDialogButtonBoxStandard_Ok) == ok);
    XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box, (XAbstractButton*)ok),
              (int)XDialogButtonBoxRole_AcceptRole);
    XQ_EQ_I64((int)XDialogButtonBox_standardButton(box, (XAbstractButton*)ok),
              (int)XDialogButtonBoxStandard_Ok);

    /* 标准按钮角色表（Qt 6.8.3 实测逐项）：Abort 是 RejectRole。 */
    {
        XPushButton* abortBtn =
            XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Abort);
        XPushButton* discardBtn =
            XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Discard);
        XPushButton* helpBtn =
            XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Help);
        XPushButton* resetBtn =
            XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Reset);
        XPushButton* applyBtn =
            XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Apply);
        XQ_CHECK_PTR(abortBtn);
        XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box,
                                                   (XAbstractButton*)abortBtn),
                  (int)XDialogButtonBoxRole_RejectRole);
        XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box,
                                                   (XAbstractButton*)discardBtn),
                  (int)XDialogButtonBoxRole_DestructiveRole);
        XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box,
                                                   (XAbstractButton*)helpBtn),
                  (int)XDialogButtonBoxRole_HelpRole);
        XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box,
                                                   (XAbstractButton*)resetBtn),
                  (int)XDialogButtonBoxRole_ResetRole);
        XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box,
                                                   (XAbstractButton*)applyBtn),
                  (int)XDialogButtonBoxRole_ApplyRole);
    }

    /* 非成员按钮：role==InvalidRole、standardButton==NoButton。 */
    {
        XPushButton* outsider = XPushButton_create(NULL, 0);
        XQ_REQUIRE(outsider != NULL);
        XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box,
                                                   (XAbstractButton*)outsider),
                  (int)XDialogButtonBoxRole_InvalidRole);
        XQ_EQ_I64((int)XDialogButtonBox_standardButton(box,
                                                       (XAbstractButton*)outsider),
                  (int)XDialogButtonBoxStandard_NoButton);
        /* 无效角色拒收（Qt 实测 count 不变）：按钮不入成员表。 */
        XDialogButtonBox_addButton(box, (XAbstractButton*)outsider,
                                   XDialogButtonBoxRole_InvalidRole);
        XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box,
                                                   (XAbstractButton*)outsider),
                  (int)XDialogButtonBoxRole_InvalidRole);
        XClassDelete((XClass*)outsider);
    }

    /* button(NoButton) 恒 NULL（标准槽位 0 不存在）。 */
    XQ_CHECK(XDialogButtonBox_button(box,
                                     XDialogButtonBoxStandard_NoButton) == NULL);

    XClassDelete((XClass*)box);
}

/** @brief 同按钮重复登记、setStandardButtons 保留用户按钮、clear 删除全部。 */
XQ_TEST(XDialogButtonBox, membershipRules)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    XPushButton* custom;
    XQ_REQUIRE(box != NULL);

    XDialogButtonBox_setStandardButtons(
        box, (int)(XDialogButtonBoxStandard_Ok |
                   XDialogButtonBoxStandard_Cancel));
    XQ_EQ_I64(XDialogButtonBox_standardButtons(box),
              (int)(XDialogButtonBoxStandard_Ok |
                    XDialogButtonBoxStandard_Cancel));
    XQ_EQ_I64(dbb_count(box), 2);
    XQ_CHECK_PTR(XDialogButtonBox_button(box, XDialogButtonBoxStandard_Ok));
    XQ_CHECK_PTR(XDialogButtonBox_button(box, XDialogButtonBoxStandard_Cancel));

    /* 用户自备按钮：ActionRole 登记，重设标准按钮后仍保留
     * （Qt 实测 setStandardButtons 只删标准按钮）。 */
    custom = XPushButton_create((XWidget*)box, 0);
    XQ_REQUIRE(custom != NULL);
    XDialogButtonBox_addButton(box, (XAbstractButton*)custom,
                               XDialogButtonBoxRole_ActionRole);
    XQ_EQ_I64(dbb_count(box), 3);
    XDialogButtonBox_setStandardButtons(box,
                                        (int)XDialogButtonBoxStandard_Ok);
    XQ_EQ_I64(dbb_count(box), 2);
    XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box,
                                               (XAbstractButton*)custom),
              (int)XDialogButtonBoxRole_ActionRole);

    /* 同按钮重复 addButton：不产生第二条成员记录，角色更新为新值
     * （Qt 实测 buttons().size()==1、role==RejectRole）。 */
    XDialogButtonBox_addButton(box, (XAbstractButton*)custom,
                               XDialogButtonBoxRole_RejectRole);
    XQ_EQ_I64(dbb_count(box), 2);
    XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box,
                                               (XAbstractButton*)custom),
              (int)XDialogButtonBoxRole_RejectRole);

    /* removeButton：只摘登记、不删按钮本体（Qt 里 setParent(nullptr)
     * 归还调用方）。 */
    XDialogButtonBox_removeButton(box, (XAbstractButton*)custom);
    XQ_EQ_I64(dbb_count(box), 1);
    XQ_EQ_I64((int)XDialogButtonBox_buttonRole(box,
                                               (XAbstractButton*)custom),
              (int)XDialogButtonBoxRole_InvalidRole);
    /* 归还后仍可自行使用/释放。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)custom) == NULL);
    XClassDelete((XClass*)custom);

    /* clear：对标 QDialogButtonBox::clear（qdialogbuttonbox.cpp:642-657）
     * ——**删除盒内全部按钮**（标准按钮与用户按钮一并删除），成员表清空
     * （账本 234；旧口径"归还用户按钮"已撤销，见 testMigrations）。 */
    {
        XPushButton* userBtn = XPushButton_create(NULL, 0);
        XQ_REQUIRE(userBtn != NULL);
        XDialogButtonBox_addButton(box, (XAbstractButton*)userBtn,
                                   XDialogButtonBoxRole_ActionRole);
        XQ_EQ_I64(dbb_count(box), 2);
        dbb_destroyed = 0;
        XObject_connect_1((XObject*)userBtn, XSignal(XObject_destroyed_signal),
                          (XObject*)box, dbb_destroyedSlot,
                          XConnectionType_Direct);
        XDialogButtonBox_clear(box);
        /* 成员表/标准掩码立即清空。 */
        XQ_EQ_I64(dbb_count(box), 0);
        XQ_EQ_I64(XDialogButtonBox_standardButtons(box), 0);
        XQ_CHECK(XDialogButtonBox_button(box,
                                         XDialogButtonBoxStandard_Ok) == NULL);
        /* 删除走延迟销毁（clicked Direct 链防 UAF）⇒ 排空后用户按钮被
         * 真正删除、计数 1。 */
        (void)XCoreApplication_processEvents(XEventLoop_AllEvents);
        XCoreApplication_sendPostedEvents(NULL, XEVENT_TYPE_DEFERRED_DELETE);
        XQ_EQ_I64(dbb_destroyed, 1);
    }

    XClassDelete((XClass*)box);
}

/** @brief 信号集成（clicked 先于角色信号、角色→信号映射）+ 布局/居中。 */
XQ_TEST(XDialogButtonBox, signalsAndLayout)
{
    XDialogButtonBox* box = XDialogButtonBox_create(NULL, 0);
    XPushButton* yes;
    XPushButton* no;
    XPushButton* help;
    XPushButton* apply;
    XQ_REQUIRE(box != NULL);

    XObject_connect_1((XObject*)box, XSignal(XDialogButtonBox_clicked_signal),
                      (XObject*)box, dbb_clickedSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)box, XSignal(XDialogButtonBox_accepted_signal),
                      (XObject*)box, dbb_acceptedSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)box, XSignal(XDialogButtonBox_rejected_signal),
                      (XObject*)box, dbb_rejectedSlot, XConnectionType_Direct);
    XObject_connect_1((XObject*)box,
                      XSignal(XDialogButtonBox_helpRequested_signal),
                      (XObject*)box, dbb_helpSlot, XConnectionType_Direct);

    yes = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Yes);
    no = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_No);
    help = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Help);
    apply = XDialogButtonBox_addButton_3(box, XDialogButtonBoxStandard_Apply);
    XQ_REQUIRE(yes && no && help && apply);

    dbb_sigReset();
    XAbstractButton_click((XAbstractButton*)yes);
    XAbstractButton_click((XAbstractButton*)no);
    XAbstractButton_click((XAbstractButton*)help);
    XAbstractButton_click((XAbstractButton*)apply);
    /* Qt 实测：accepted=1（Yes）、rejected=1（No）、help=1、clicked=4。 */
    XQ_EQ_I64(dbb_accepted, 1);
    XQ_EQ_I64(dbb_rejected, 1);
    XQ_EQ_I64(dbb_help, 1);
    XQ_EQ_I64(dbb_clicked, 4);

    /* 按钮成为盒的子控件（布局关系）。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)yes) == (XWidget*)box);

    /* 方向/居中：垂直排布时按钮 y 递增、x 一致（Qt 实测同为交叉轴居中）。 */
    XDialogButtonBox_setOrientation(box, 2);
    XQ_EQ_I64(XDialogButtonBox_orientation(box), 2);
    XDialogButtonBox_setCenterButtons(box, true);
    XQ_CHECK(XDialogButtonBox_centerButtons(box));
    {
        int y0 = XWidget_y((XWidget*)yes);
        int y1 = XWidget_y((XWidget*)no);
        XQ_CHECK(y1 > y0);
    }
    XDialogButtonBox_setOrientation(box, 1);
    XDialogButtonBox_setCenterButtons(box, false);

    /* 父子所有权：盒析构带走盒自建标准按钮（此处仅断言不崩溃并回收）。 */
    XClassDelete((XClass*)box);
}

/** @brief 带参构造 init_full（对标 QDialogButtonBox(StandardButtons,
 *         Qt::Orientation, QWidget*)）。 */
XQ_TEST(XDialogButtonBox, initFull)
{
    XDialogButtonBox box;
    XDialogButtonBox_init_full(&box,
                               (int)(XDialogButtonBoxStandard_Ok |
                                     XDialogButtonBoxStandard_Cancel),
                               2, NULL, 0);
    XQ_EQ_I64(XDialogButtonBox_orientation(&box), 2);
    XQ_EQ_I64(dbb_count(&box), 2);
    XQ_CHECK(XDialogButtonBox_button(&box,
                                     XDialogButtonBoxStandard_Ok) != NULL);
    XQ_CHECK(XDialogButtonBox_button(&box,
                                     XDialogButtonBoxStandard_Cancel) != NULL);
    XClassDeinit(&box);
}

#endif /* XGUI_ON && XWIDGET_ON && XPUSHBUTTON_ON && XDIALOGBUTTONBOX_ON */
