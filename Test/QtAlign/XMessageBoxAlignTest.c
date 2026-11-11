/**
 * @file       XMessageBoxAlignTest.c
 * @brief      XMessageBox × Qt 6.8 QMessageBox 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qmessagebox.h
 *             与 Qt 6.8 源码 src/widgets/dialogs/qmessagebox.cpp（jsdelivr
 *             v6.8.0 原文），关键行为用本机 Qt 6.8.0（QT_QPA_PLATFORM=
 *             offscreen）探针实测：
 *             - 构造默认：icon=NoIcon(0)、standardButtons=NoButton(0)、
 *               text/informativeText 空；QMessageBoxPrivate::init 末尾
 *               q->setModal(true) ⇒ isModal=1、windowModality=2（M0/M1）；
 *             - defaultButton()/escapeButton() 只反映**显式**设置，未设
 *               返回 NULL（M2：setStandardButtons(Ok|Cancel) 后仍 NULL）；
 *             - 标准按钮位值与 QDialogButtonBox 一致（Ok=0x400、
 *               Cancel=0x400000），clickedButton 的 result 为标准位值
 *               （qmessagebox.cpp:448-469 execReturnCode）；
 *             - Esc 无修饰 → 点击探测转义按钮并 done(位值)；Esc+Shift 不
 *               匹配 QKeySequence::Cancel，不点击、事件不接受、窗口留存
 *               （W0/W1，qmessagebox.cpp:1564-1577）；
 *             - 无转义按钮时 closeEvent 被 ignore（qmessagebox.cpp:1513-
 *               1525）；有转义按钮时关闭等价点击转义按钮并回填 result。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDIALOG_ON && XMESSAGEBOX_ON && \
    XDIALOGBUTTONBOX_ON && XPUSHBUTTON_ON && XLABEL_ON

#include "XMessageBox.h"
#include "XDialogButtonBox.h"
#include "XAbstractButton.h"
#include "XPushButton.h"
#include "XWidget.h"
#include "XObject.h"
#include "XObject_Protected.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XClass.h"

/** @brief 消息框按钮点击计数（用例间复位）。 */
static int g_xmsgClicks;

static void xmsgClickedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    ++g_xmsgClicks;
}

/** @brief 向控件直发一次键盘事件。 */
static void xmsgKey(XWidget* target, int key, XKeyboardModifiers mods)
{
    XKeyEvent kev;
    XKeyEvent_init(&kev, XEVENT_TYPE_KEY_PRESS, key, mods);
    XObject_event_base((XObject*)target, (XEvent*)&kev);
}

/** @brief 空串安全转换：本库 XString_toUtf8 对空串返回 NULL。 */
static const char* xmsgCStr(const char* s)
{
    return s ? s : "";
}

/* ==================== ① 构造默认值（M0/M1） ==================== */

XQ_TEST(XMessageBox, constructorDefaults)
{
    XMessageBox* box = XMessageBox_create(NULL, 0);
    XQ_REQUIRE(box != NULL);

    /* qmessagebox.cpp:281 `q->setModal(true)`：构造即应用模态。 */
    XQ_CHECK(XDialog_isModal(&box->m_base));
    XQ_EQ_I64(XWidget_windowModality((XWidget*)box),
              XWindowModality_ApplicationModal);
    /* 属性默认：NoIcon / NoButton / 空文本。 */
    XQ_EQ_I64(XMessageBox_icon(box), XMessageBoxIcon_NoIcon);
    XQ_EQ_I64(XMessageBox_standardButtons(box), 0);
    XQ_EQ_STR(xmsgCStr(XMessageBox_text(box)), "");
    XQ_EQ_STR(xmsgCStr(XMessageBox_informativeText(box)), "");
    /* 显式默认/转义按钮未设时均为 NULL（M2）。 */
    XQ_CHECK(XMessageBox_defaultButton(box) == NULL);
    XQ_CHECK(XMessageBox_escapeButton(box) == NULL);
    XQ_CHECK(XMessageBox_clickedButton(box) == NULL);

    /* setText/setIcon 往返。 */
    XMessageBox_setText(box, "保存失败");
    XMessageBox_setIcon(box, XMessageBoxIcon_Warning);
    XQ_EQ_STR(XMessageBox_text(box), "保存失败");
    XQ_EQ_I64(XMessageBox_icon(box), XMessageBoxIcon_Warning);

    XClassDelete(box);
}

/* ==================== ② 标准按钮 / 角色 / 点击结果位值 ==================== */

XQ_TEST(XMessageBox, standardButtonsAndRole)
{
    XMessageBox* box = XMessageBox_create(NULL, 0);
    XAbstractButton* ok;
    XAbstractButton* cancel;
    XQ_REQUIRE(box != NULL);

    XMessageBox_setStandardButtons(
        box, (int)(XDialogButtonBoxStandard_Ok |
                   XDialogButtonBoxStandard_Cancel));
    XQ_EQ_I64(XMessageBox_standardButtons(box),
              (XDialogButtonBoxStandard_Ok | XDialogButtonBoxStandard_Cancel));

    ok = XMessageBox_button(box, XDialogButtonBoxStandard_Ok);
    cancel = XMessageBox_button(box, XDialogButtonBoxStandard_Cancel);
    XQ_CHECK(ok != NULL);
    XQ_CHECK(cancel != NULL);
    XQ_CHECK(ok != cancel);
    if (ok) {
        /* qmessagebox.h:71-83 按钮角色与 QDialogButtonBox 数值一致。 */
        XQ_EQ_I64(XMessageBox_standardButton(box, ok),
                  XDialogButtonBoxStandard_Ok);
        XQ_EQ_I64(XMessageBox_buttonRole(box, ok),
                  XMessageBoxButtonRole_AcceptRole);
    }
    if (cancel) {
        XQ_EQ_I64(XMessageBox_standardButton(box, cancel),
                  XDialogButtonBoxStandard_Cancel);
        XQ_EQ_I64(XMessageBox_buttonRole(box, cancel),
                  XMessageBoxButtonRole_RejectRole);
    }

    /* 点击 Ok：clickedButton 记录、result 为标准按钮位值
     * （qmessagebox.cpp:448-469）。 */
    g_xmsgClicks = 0;
    XWidget_show((XWidget*)box);
    if (ok) {
        XObject_connect_1((XObject*)ok,
                          XSignal(XAbstractButton_clicked_signal),
                          (XObject*)box, xmsgClickedSlot,
                          XConnectionType_Direct);
        XAbstractButton_click(ok);
        XQ_EQ_I64(g_xmsgClicks, 1);
        XQ_CHECK(XMessageBox_clickedButton(box) == ok);
        XQ_EQ_I64(XDialog_result(&box->m_base),
                  XDialogButtonBoxStandard_Ok);
        XQ_CHECK(!XWidget_isVisible((XWidget*)box));
    }

    XClassDelete(box);
}

/* ==================== ③ Esc 修饰键门（W0/W1） ==================== */

XQ_TEST(XMessageBox, escapeKeyModifiers)
{
    XMessageBox* box = XMessageBox_create(NULL, 0);
    XQ_REQUIRE(box != NULL);
    XMessageBox_setStandardButtons(
        box, (int)(XDialogButtonBoxStandard_Ok |
                   XDialogButtonBoxStandard_Cancel));
    XWidget_show((XWidget*)box);
    g_xmsgClicks = 0;
    XObject_connect_1((XObject*)box, XSignal(XMessageBox_buttonClicked_signal),
                      (XObject*)box, xmsgClickedSlot,
                      XConnectionType_Direct);

    /* 实测 W0：Esc+Shift 不匹配 Cancel 快捷键 → 不点击、不关窗。 */
    xmsgKey((XWidget*)box, (int)XKey_Escape,
            XKeyboardModifier_ShiftModifier);
    XQ_EQ_I64(g_xmsgClicks, 0);
    XQ_CHECK(XWidget_isVisible((XWidget*)box));
    XQ_EQ_I64(XDialog_result(&box->m_base), 0);

    /* 实测 W1：无修饰 Esc 点击探测转义按钮（Cancel）并 done(位值)。 */
    xmsgKey((XWidget*)box, (int)XKey_Escape, XKeyboardModifier_NoModifier);
    XQ_EQ_I64(g_xmsgClicks, 1);
    XQ_CHECK(!XWidget_isVisible((XWidget*)box));
    XQ_EQ_I64(XDialog_result(&box->m_base),
              XDialogButtonBoxStandard_Cancel);

    XClassDelete(box);
}

/* ==================== ④ 关闭事件：无转义按钮时 ignore ==================== */

XQ_TEST(XMessageBox, closeEventNeedsEscapeButton)
{
    XMessageBox* bare = XMessageBox_create(NULL, 0);
    XMessageBox* withOk = XMessageBox_create(NULL, 0);
    XQ_REQUIRE(bare != NULL);
    XQ_REQUIRE(withOk != NULL);

    /* 构造两个「无转义按钮」形态：Ok+Yes 都不是 Cancel/RejectRole/
     * NoRole，且按钮数不为 1 ⇒ detectEscapeButton 探测失败
     * （qmessagebox.cpp:1084-1136）⇒ closeEvent ignore、窗口留存
     * （qmessagebox.cpp:1513-1520）。
     * 注：不能用「零按钮」——autoAddOkButton 会在 show 时自动补 Ok
     * （qmessagebox.cpp:1663-1664，本库 VXMessageBox_showEvent 同口径）。 */
    XMessageBox_setStandardButtons(
        bare, (int)(XDialogButtonBoxStandard_Ok |
                    XDialogButtonBoxStandard_Yes));
    XWidget_show((XWidget*)bare);
    XQ_CHECK(!XWidget_close((XWidget*)bare));
    XQ_CHECK(XWidget_isVisible((XWidget*)bare));

    /* Ok 单按钮 ⇒ 转义按钮即该按钮 ⇒ 关闭等价点击并回填 result。 */
    XMessageBox_setStandardButtons(withOk,
                                   (int)XDialogButtonBoxStandard_Ok);
    XWidget_show((XWidget*)withOk);
    (void)XWidget_close((XWidget*)withOk);
    XQ_CHECK(XMessageBox_clickedButton(withOk) != NULL);
    XQ_EQ_I64(XDialog_result(&withOk->m_base),
              XDialogButtonBoxStandard_Ok);

    XClassDelete(bare);
    XClassDelete(withOk);
}

/* ==================== ⑤ 父子所有权 ==================== */

XQ_TEST(XMessageBox, parentOwnership)
{
    XWidget* parent = XWidget_create(NULL, 0);
    XMessageBox* box;
    XQ_REQUIRE(parent != NULL);
    box = XMessageBox_create(parent, 0);
    XQ_REQUIRE(box != NULL);

    XQ_CHECK(XWidget_parentWidget((XWidget*)box) == parent);
    /* 内部文本标签/按钮盒是对话框子对象，所有权随对话框。 */
    XQ_CHECK(box->m_textLabel != NULL);
    XQ_CHECK(XWidget_parentWidget((XWidget*)box->m_textLabel) ==
             (XWidget*)box);

    XClassDelete(box);
    /* 父控件仍可用（对话框删除不反噬父）。 */
    XQ_CHECK(XWidget_parentWidget(parent) == NULL);
    XClassDelete(parent);
}

#endif /* 模块开关 */
