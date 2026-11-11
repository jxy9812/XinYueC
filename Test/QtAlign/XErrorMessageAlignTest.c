/**
 * @file       XErrorMessageAlignTest.c
 * @brief      XErrorMessage × Qt 6.8 QErrorMessage 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/
 *               qerrormessage.h（构造 parent、静态 qtHandler、showMessage
 *               单参/双参重载、受保护 done(int)/changeEvent；Qt 无公开
 *               currentMessage 访问器，本库开放为普通函数便于测试）；
 *             - Qt 6.8 源码 src/widgets/dialogs/qerrormessage.cpp 原文：
 *               * showMessage（:398-405）：**先**过 isMessageToBeShown，
 *                 通过后**入 pending 队列**，仅当对话框不可见时弹队首并
 *                 show()；
 *               * isMessageToBeShown（:343-347）：
 *                 `!message.isEmpty() && (type.isEmpty() ?
 *                  !doNotShow.contains(message) :
 *                  !doNotShowType.contains(type))` —— **空串直接短路**；
 *               * nextPending（:349-366）：逐条出队，可显示者写入横幅文本
 *                 与 currentMessage/currentType，并把"再次显示"复选框
 *                 setChecked(true)；
 *               * done（:292-307）：复选框未勾时记抑制表 → **清空
 *                 currentMessage/currentType** → QDialog::done → 若队列
 *                 还有可显示消息则 show()（保持可见）。
 *             - 本机 Qt 6.8.3 + offscreen 实测（/tmp 探针）：
 *               ① 构造后 windowTitle 空、无 min/max 约束（minw/minh==0、
 *                  maxw/maxh==16777215）、无 MSWindowsFixedSizeDialogHint、
 *                  resize(700,500) 生效；
 *               ② showMessage("") 后 visible==0、文本不变（空串短路）；
 *               ③ 显示中再 showMessage("BBB") 只入队：可见、文本仍 "AAA"；
 *                  done() 后仍可见且文本变 "BBB"；再 done() 才隐藏
 *                  （文本在 QTextEdit 里保留，但 currentMessage 已清）。
 * @note       r1 的"后到覆盖单槽 / 空串参与显示 / 固定尺寸"三项偏差已
 *             全部撤销（账本 225/226/228/229），旧 apitest 断言见
 *             testMigrations。静态 qtHandler() 仍不提供（依赖
 *             qInstallMessageHandler；账本 227 deferred）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON

#include "XErrorMessage.h"
#include "XDialog.h"
#include "XWidget.h"
#include "XObject.h"

/* ==================== 用例 ==================== */

/** @brief 缺省态 + 空串短路 + 队列语义（对标 Qt 6.8 nextPending/done）。 */
XQ_TEST(XErrorMessage, defaultsQueueAndEmptyMessage)
{
    XErrorMessage* em = XErrorMessage_create(NULL, 0);
    XQ_REQUIRE(em != NULL);

    /* 缺省：复选框口径"再次显示"默认勾选 ⇒ doneShown==true；
     * 无当前消息。 */
    XQ_CHECK(XErrorMessage_isDoneShown(em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "");
    XQ_EQ_STR(XErrorMessage_currentMessage(NULL), "");

    /* 空串消息**直接短路**（isMessageToBeShown 的 !message.isEmpty()）：
     * 不显示、不改当前消息、不入队。 */
    XErrorMessage_showMessage(em, "");
    XQ_CHECK(!XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "");
    /* 双参且 type 非空时空串仍短路（第一项判空先于 type 分支）。 */
    XErrorMessage_showMessage_2(em, "", "type");
    XQ_CHECK(!XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "");

    /* 第一条消息：立即显示并成为 currentMessage。 */
    XErrorMessage_showMessage(em, "错误 A");
    XQ_CHECK(XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "错误 A");

    /* 显示中再 showMessage 只**入队**：横幅/当前消息保持前一条
     * （Qt 实测文本仍 "AAA"；账本 225）。 */
    XErrorMessage_showMessage(em, "错误 B");
    XQ_CHECK(XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "错误 A");

    /* done() 弹出下一条并保持可见（currentMessage 换成队列下一条）。 */
    XErrorMessage_done(em, (int)XDialogCode_Rejected);
    XQ_CHECK(XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "错误 B");

    /* 队列排空：done() 真正收口隐藏，并**清空** currentMessage（账本 226）。 */
    XErrorMessage_done(em, (int)XDialogCode_Rejected);
    XQ_CHECK(!XWidget_isVisible((XWidget*)em));
    XQ_EQ_I64(XDialog_result(&em->m_base), (int)XDialogCode_Rejected);
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "");

    /* NULL 边界零操作。 */
    XErrorMessage_done(NULL, 0);
    XErrorMessage_showMessage(NULL, "x");
    XErrorMessage_showMessage_2(NULL, "x", "t");
    XQ_CHECK(!XErrorMessage_isDoneShown(NULL));

    XClassDelete((XClass*)em);
}

/** @brief 文本维度抑制 + done() 清当前消息 + 新消息复位"再次显示"。 */
XQ_TEST(XErrorMessage, textSuppression)
{
    XErrorMessage* em = XErrorMessage_create(NULL, 0);
    XQ_REQUIRE(em != NULL);

    XErrorMessage_showMessage(em, "磁盘空间不足");
    XQ_CHECK(XWidget_isVisible((XWidget*)em));

    /* 勾掉"再次显示"（doneShown=false）后 done 记抑制表并清当前消息。 */
    XErrorMessage_setDoneShown(em, false);
    XQ_CHECK(!XErrorMessage_isDoneShown(em));
    XErrorMessage_done(em, (int)XDialogCode_Rejected);
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "");

    /* 同文本不再弹出（isMessageToBeShown 过滤在入队前 return）。 */
    XErrorMessage_showMessage(em, "磁盘空间不足");
    XQ_CHECK(!XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "");

    /* 未被抑制的其他文本照常弹出。 */
    XErrorMessage_showMessage(em, "网络已断开");
    XQ_CHECK(XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "网络已断开");

    /* 新消息显示时"再次显示"复位为勾选（Qt nextPending 里
     * again->setChecked(true)）。 */
    XQ_CHECK(XErrorMessage_isDoneShown(em));

    XClassDelete((XClass*)em);
}

/** @brief 类型维度抑制（对标 isMessageToBeShown 的 doNotShowType 维度）。 */
XQ_TEST(XErrorMessage, typeSuppression)
{
    XErrorMessage* em = XErrorMessage_create(NULL, 0);
    XQ_REQUIRE(em != NULL);

    XErrorMessage_showMessage_2(em, "校验失败", "auth");
    XQ_CHECK(XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "校验失败");

    /* 记类型后**同类型不同文本**也被抑制（Qt 实测同口径）。 */
    XErrorMessage_setDoneShown(em, false);
    XErrorMessage_done(em, (int)XDialogCode_Rejected);
    XErrorMessage_showMessage_2(em, "完全不同的消息", "auth");
    XQ_CHECK(!XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "");

    /* 同文本不同类型不被抑制（type 非空只查类型维度）。 */
    XErrorMessage_showMessage_2(em, "校验失败", "net");
    XQ_CHECK(XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "校验失败");

    /* 类型为空的双参调用退化为文本维度。 */
    XErrorMessage_setDoneShown(em, true);
    XErrorMessage_done(em, (int)XDialogCode_Rejected);
    XErrorMessage_showMessage_2(em, "空类型消息", "");
    XQ_CHECK(XWidget_isVisible((XWidget*)em));
    XQ_EQ_STR(XErrorMessage_currentMessage(em), "空类型消息");

    XClassDelete((XClass*)em);
}

/** @brief 非固定尺寸：Qt 无 min/max 约束、可拉伸（账本 229）。 */
XQ_TEST(XErrorMessage, resizableNotFixed)
{
    XErrorMessage* em = XErrorMessage_create(NULL, 0);
    XQ_REQUIRE(em != NULL);

    /* 内容驱动首选尺寸：空消息落 300x(行高+12) 的内容下限。 */
    XQ_CHECK(XWidget_width((XWidget*)em) >= 300);
    XQ_CHECK(XWidget_height((XWidget*)em) >= 40);

    /* 不补 MSWindowsFixedSizeDialogHint（r1 曾补，账本 229 撤销）。 */
    XQ_CHECK((XWidget_windowFlags((XWidget*)em) &
              XWindowType_MSWindowsFixedSizeDialogHint) == 0);

    /* 无 min/max 约束：拖大/拖小都生效（Qt 实测 resize(700,500)）。 */
    XWidget_resize((XWidget*)em, 700, 500);
    XQ_EQ_I64(XWidget_width((XWidget*)em), 700);
    XQ_EQ_I64(XWidget_height((XWidget*)em), 500);
    XWidget_resize((XWidget*)em, 120, 60);
    XQ_EQ_I64(XWidget_width((XWidget*)em), 120);
    XQ_EQ_I64(XWidget_height((XWidget*)em), 60);

    /* sizeHint 是内容驱动的正值（Qt 的布局 sizeHint 等价物）。 */
    {
        XSize hint = XWidget_sizeHint((const XWidget*)em);
        XQ_CHECK(hint.width >= 300);
        XQ_CHECK(hint.height >= 40);
    }

    /* 长文本按内容增长（expandedTo：可见期变大即 resize）。
     * 注意先 showMessage 再断言：QErrorMessage::showMessage 在对话框
     * **已可见**时只入队、横幅仍显示前一条（qerrormessage.cpp:398-405，
     * 本库 xerr_updateSize 同口径）——先 show 空框会让长文本进不了
     * 当前横幅。 */
    XErrorMessage_showMessage(em, "这是一条足够长的错误消息，用来验证内容驱动宽度增长确实生效而不是固定尺寸");
    XQ_CHECK(XWidget_isVisible((XWidget*)em));
    XQ_CHECK(XWidget_width((XWidget*)em) > 300);

    XClassDelete((XClass*)em);
}

/** @brief 父子关系（对标 QErrorMessage(QWidget* parent)：父子链 + 随父析构）。 */
XQ_TEST(XErrorMessage, parentOwnership)
{
    XWidget* host = XWidget_create(NULL, 0);
    XErrorMessage* em;
    XQ_REQUIRE(host != NULL);
    em = XErrorMessage_create(host, 0);
    XQ_REQUIRE(em != NULL);
    /* 父子链登记（对话框以 host 为父 → 成为其子控件）。 */
    XQ_CHECK(XWidget_parentWidget((XWidget*)em) == host);
    XQ_CHECK(XWidget_isAncestorOf(host, (XWidget*)em));
    /* 随父析构级联释放（此用例不再单独删 em）。 */
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON */
