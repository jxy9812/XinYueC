/**
 * @file       XWizardAlignTest.c
 * @brief      XWizard / XWizardPage × Qt 6.8 QWizard / QWizardPage 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qwizard.h
 *               （WizardButton 0..8 + Stretch/NoButton/NStandardButtons/
 *               NButtons、WizardPixmap、WizardStyle 含 NStyles、WizardOption
 *               含 ExtendedWatermarkPixmap、addPage/setPage/removePage/
 *               page/pageIds/startId/currentId/next/back/restart/done、
 *               isFinalPage/isCommitPage/completeChanged、field/setField）；
 *             - 本机 Qt 6.8.3 + offscreen 实测（/tmp 探针）：
 *               ① 构造默认：style==ClassicStyle(0)、options==0、
 *                  startId==-1、currentId==-1、titleFormat/subTitleFormat
 *                  ==2(AutoText)；
 *               ② 三页向导：p0/p1 的 isFinalPage()==0，末页 p2==1；
 *                  setFinalPage(true) 后中间页==1、setFinalPage(false) 后==0；
 *                  **未加入向导的独立页 isFinalPage()==1**（X 侧登记偏差）；
 *               ③ 按钮态：第 0 页 Back 可见但**禁用**、Next 可见可用、
 *                  Finish 隐藏；末页 Next 隐藏、Finish 可见可用；
 *                  isComplete=false 时 Next 可见禁用；
 *                  NoBackButtonOnStartPage→起始页 Back 隐藏；
 *                  NoCancelButton→Cancel 隐藏；
 *                  HaveFinishButtonOnEarlyPages→非末页也显示 Finish（禁用）；
 *                  HaveNextButtonOnLastPage→末页也显示 Next（禁用）；
 *               ④ **back() 走导航历史**：0→2 直接跳页后 back() 回到 0
 *                  （不是 index-1）；
 *               ⑤ done(Rejected) 后 currentId==-1、访问史清空；
 *                  done(Accepted) 不改 currentId；
 *               ⑥ removePage 不删页、页仍是向导子对象；**页 id 稳定**：
 *                  0,1,2 删 1 后 addPage 得 3（页表不左移，r2 对齐）；
 *               ⑦ addPage 返回 id 0,1,2；pageIds().size() 同步；
 *               ⑧ 九个按钮构造即预建（含 Help/Custom1..3，默认隐藏）；
 *                  titleFormat/subTitleFormat 默认 2(AutoText)；
 *                  buttonText 未设返回缺省文本；
 *               ⑨ 2026-11-12 批次补探（整改台账 P0-43/44/45/46/47/48/
 *                  86/E6/E7、P3-48）：
 *                  - setButtonText/buttonText 对 CustomButton1..3（6..8）
 *                    全部受理（qwizard.cpp:2595 ensureButton 的 NButtons
 *                    门）；自定义槽未设自定义文本时回落**槽位活动按钮
 *                    自身文本**（qwizard.cpp:2634 d->btns[which]->text()）；
 *                    显式文本（含空串）下发登记按钮（qwizard.cpp:2605）；
 *                  - removePage 四情形收口（qwizard.cpp:2244-2265）：
 *                    非历史页→仅摘除；历史非当前→摘历史条目；当前且
 *                    历史空→reset()+restart()；当前且非首条→back()
 *                    （可见性/清理/当前页移交在摘页前完成）；
 *                  - setWizardStyle 兑现：缺省按钮文案随样式
 *                    （buttonDefaultText 的 mac 分支，qwizard.cpp:614-640）、
 *                    横幅呈现/水印列随样式；
 *                  - setPixmap 四槽参与布局/绘制（水印列让位，Mac 门）；
 *                  - setTitleFormat/setSubTitleFormat 经内建 XLabel 兑现
 *                    （AutoText 探测同源；>255 字节标题无截断，P0-E6）；
 *                  - sizeHint 虚槽 expandedTo(500,360)（P3-48，
 *                    qwizard.cpp sizeHint 的下限语义）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDIALOG_ON && XWIZARD_ON

#include "XWizard.h"
#include "XDialog.h"
#include "XWidget.h"
#include "XObject.h"
#include "XAbstractButton.h"
#include "XPushButton.h"
#include "XVariant.h"
#include "XVarList.h"
#include "XString.h"
#if XFRAME_ON && XLABEL_ON
#include "XLabel.h" /* P0-46/47：标题/副标题内建标签探针。 */
#endif
#include <stdio.h> /* remove()：水印临时图收尾。 */
#include "XImage.h"

/* ==================== 信号探针 ==================== */

static int wz_currentIdChanged = 0;
static int wz_lastId = -1;
static int wz_customClicked = -1;

static void wz_currentIdSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, id);
    wz_lastId = id;
    ++wz_currentIdChanged;
}

static void wz_customSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    XVarList_args_1(args, int, which);
    wz_customClicked = which;
}

/* ==================== 用例 ==================== */

/** @brief 页缺省值 / isFinalPage 三态 / 字段委派 / 所有权。 */
XQ_TEST(XWizard, pageDefaults)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XWizardPage* p1 = XWizardPage_create(NULL, 0);
    XWizardPage* p2 = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(wiz && p0 && p1 && p2);

    /* 页缺省。 */
    XQ_EQ_STR(XWizardPage_title(p0), "");
    XQ_EQ_STR(XWizardPage_subTitle(p0), "");
    XQ_CHECK(XWizardPage_isComplete(p0));
    XQ_CHECK(!XWizardPage_isCommitPage(p0));
    XQ_CHECK(XWizardPage_wizard(p0) == NULL);
    XQ_EQ_I64(XWizardPage_nextId(p0), -1);
    XQ_CHECK(XWizardPage_validatePage(p0));
    /* 独立页 isFinalPage()==true（nextId()==-1 ⇒ 末页；本机 Qt 6.8.3
     * 实测 1；r2 对齐，账本 239）。 */
    XQ_CHECK(XWizardPage_isFinalPage(p0));

    /* 提交页往返 + 显式末页位。 */
    XWizardPage_setCommitPage(p0, true);
    XQ_CHECK(XWizardPage_isCommitPage(p0));
    XWizardPage_setCommitPage(p0, false);
    XWizardPage_setFinalPage(p1, true);
    XQ_CHECK(XWizardPage_isFinalPage(p1)); /* 显式值优先，未加入向导也生效 */

    /* 加入三页向导：末页推导为 true、中间页 false（Qt 实测口径）。 */
    XQ_EQ_I64(XWizard_addPage(wiz, p0), 0);
    XQ_EQ_I64(XWizard_addPage(wiz, p1), 1);
    XQ_EQ_I64(XWizard_addPage(wiz, p2), 2);
    XQ_EQ_I64(XWizard_pageIds(wiz, NULL, 0), 3); /* P2-48：pageCount 删，等价计数口 */
    XQ_CHECK(XWizardPage_wizard(p2) == wiz);
    XQ_CHECK(XWizard_currentPage(wiz) == p0);
    XQ_CHECK(!XWizardPage_isFinalPage(p0));
    XQ_CHECK(XWizardPage_isFinalPage(p1)); /* 显式 setFinalPage(true) */
    XWizardPage_setFinalPage(p1, false);
    XQ_CHECK(!XWizardPage_isFinalPage(p1));

    /* 字段委派（对标 QWizardPage::setField/field → QWizard 字段表；
     * 未登记字段 setField 为空操作，Qt 同）。 */
    XWizardPage_registerField(p0, "user", NULL, NULL, 0);
    XWizardPage_setField(p0, "user", "xinyue");
    {
        const XString* v = XWizardPage_field(p0, "user");
        XQ_CHECK_PTR(v);
        XQ_CHECK(v && strcmp(XString_toUtf8(v), "xinyue") == 0);
    }
    XQ_CHECK(XWizard_field_2(wiz, "user") != NULL);
    XQ_CHECK(XWizardPage_field(p0, "缺失") == NULL);
    /* 未加入向导的页写字段为空操作。 */
    {
        XWizardPage stray;
        XWizardPage_init(&stray, NULL, 0);
        XWizardPage_setField(&stray, "k", "v");
        XQ_CHECK(XWizardPage_field(&stray, "k") == NULL);
        XClassDeinit(&stray);
    }

    /* 页随向导析构释放（父子所有权）。 */
    XQ_CHECK(XWidget_isAncestorOf((XWidget*)wiz, (XWidget*)p2));
    XClassDelete((XClass*)wiz);
}

/** @brief 导航：当前/起始页缺省 -1、next/back 历史栈、restart/startId。 */
XQ_TEST(XWizard, navigationHistory)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XWizardPage* p1 = XWizardPage_create(NULL, 0);
    XWizardPage* p2 = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(wiz && p0 && p1 && p2);

    /* 构造默认（Qt 实测 currentId==-1、startId==-1）。 */
    XQ_EQ_I64(XWizard_currentId(wiz), -1);
    XQ_EQ_I64(XWizard_startId(wiz), -1);
    XQ_EQ_I64(XWizard_pageIds(wiz, NULL, 0), 0); /* P2-48：pageCount 删 */

    XObject_connect((XObject*)wiz, XSignal(XWizard_currentIdChanged_signal),
                      (XObject*)wiz, wz_currentIdSlot, XConnectionType_Direct);

    XWizard_addPage(wiz, p0);
    XWizard_addPage(wiz, p1);
    XWizard_addPage(wiz, p2);
    /* 本库 addPage 首页即进入（既有回归锁定的便利语义）。 */
    XQ_EQ_I64(XWizard_currentId(wiz), 0);
    XQ_CHECK(XWizard_hasVisitedPage(wiz, 0));

    wz_currentIdChanged = 0;
    XWizard_next(wiz);
    XQ_EQ_I64(XWizard_currentId(wiz), 1);
    XQ_EQ_I64(wz_currentIdChanged, 1);
    XQ_EQ_I64(wz_lastId, 1);
    XWizard_next(wiz);
    XQ_EQ_I64(XWizard_currentId(wiz), 2);
    XWizard_next(wiz); /* 末页 next 不动 */
    XQ_EQ_I64(XWizard_currentId(wiz), 2);

    /* 顺序后退。 */
    XWizard_back(wiz);
    XQ_EQ_I64(XWizard_currentId(wiz), 1);
    XWizard_back(wiz);
    XQ_EQ_I64(XWizard_currentId(wiz), 0);
    XWizard_back(wiz); /* 历史为空：不动 */
    XQ_EQ_I64(XWizard_currentId(wiz), 0);

    /* 历史栈语义（Qt 实测）：0→2 直接跳页后 back() 回 0 而不是 1。 */
    XWizard_setCurrentId(wiz, 2);
    XQ_EQ_I64(XWizard_currentId(wiz), 2);
    XWizard_back(wiz);
    XQ_EQ_I64(XWizard_currentId(wiz), 0);

    /* 起始页：无页面时 -1；有页面时 startId 解析为**首个页 id**（Qt：
     * `pageMap.constBegin().key()`）；setStartId(2)+restart → 2
     * （P1-32：旧名 setStartIndex → Qt 名 setStartId）。 */
    XQ_EQ_I64(XWizard_startId(wiz), 0); /* 三页向导的首个页 id == 0 */
    XWizard_restart(wiz);
    XQ_EQ_I64(XWizard_currentId(wiz), 0);
    XWizard_setStartId(wiz, 2);
    XQ_EQ_I64(XWizard_startId(wiz), 2);
    /* 不存在的 id 被忽略（Qt setStartId 打印警告后 return）。 */
    XWizard_setStartId(wiz, 99);
    XQ_EQ_I64(XWizard_startId(wiz), 2);
    XWizard_restart(wiz);
    XQ_EQ_I64(XWizard_currentId(wiz), 2);
    XQ_CHECK(XWizard_hasVisitedPage(wiz, 2));
    XQ_CHECK(!XWizard_hasVisitedPage(wiz, 0));
    /* 访问史汇总接口（计数/写入两模式）。 */
    {
        int ids[4];
        XQ_EQ_I64(XWizard_visitedIds(wiz, NULL, 0), 1);
        XQ_EQ_I64(XWizard_visitedIds(wiz, ids, 4), 1);
        XQ_EQ_I64(ids[0], 2);
        XQ_EQ_I64(XWizard_pageIds(wiz, ids, 4), 3);
    }

    XClassDelete((XClass*)wiz);
}

/** @brief 按钮态与选项兑现（Back 起页可见禁用、末页 Finish 显示、
 *         HaveHelpButton 建/删 Help、CustomButton1 点击信号）。 */
XQ_TEST(XWizard, buttonsAndOptions)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XWizardPage* p1 = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(wiz && p0 && p1);

    XObject_connect((XObject*)wiz,
                      XSignal(XWizard_customButtonClicked_signal),
                      (XObject*)wiz, wz_customSlot, XConnectionType_Direct);

    /* 九个按钮**构造即预建**（Qt QWizardPrivate::init 的 ensureButton +
     * updateButtonLayout，qwizard.cpp:661-664/1460-1490；账本 242）——
     * Help 与 Custom1..3 也存在，只是默认隐藏。 */
    XQ_CHECK_PTR(XWizard_button(wiz, XWizardButton_BackButton));
    XQ_CHECK_PTR(XWizard_button(wiz, XWizardButton_NextButton));
    XQ_CHECK_PTR(XWizard_button(wiz, XWizardButton_CommitButton));
    XQ_CHECK_PTR(XWizard_button(wiz, XWizardButton_FinishButton));
    XQ_CHECK_PTR(XWizard_button(wiz, XWizardButton_CancelButton));
    XQ_CHECK_PTR(XWizard_button(wiz, XWizardButton_HelpButton));
    XQ_CHECK_PTR(XWizard_button(wiz, XWizardButton_CustomButton1));
    XQ_CHECK(XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_HelpButton)));
    XQ_CHECK(XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_CustomButton1)));
    /* 缺省按钮文本（buttonText 未设时返回缺省层，账本 241）。 */
    XQ_CHECK(strcmp(XWizard_buttonText(wiz, XWizardButton_NextButton), "") != 0);
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_CustomButton1), "");

    XWizard_addPage(wiz, p0);
    XWizard_addPage(wiz, p1);
    XQ_EQ_I64(XWizard_currentId(wiz), 0);

    /* 第 0 页：Back 可见但禁用、Next 可见可用、Finish 隐藏（Qt 实测）。 */
    XQ_CHECK(!XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_BackButton)));
    XQ_CHECK(!XWidget_isEnabled(
        (XWidget*)XWizard_button(wiz, XWizardButton_BackButton)));
    XQ_CHECK(!XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_NextButton)));
    XQ_CHECK(XWidget_isEnabled(
        (XWidget*)XWizard_button(wiz, XWizardButton_NextButton)));
    XQ_CHECK(XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_FinishButton)));

    /* 末页：Next 隐藏、Finish 可见可用。 */
    XWizard_setCurrentId(wiz, 1);
    XQ_CHECK(XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_NextButton)));
    XQ_CHECK(!XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_FinishButton)));
    XQ_CHECK(XWidget_isEnabled(
        (XWidget*)XWizard_button(wiz, XWizardButton_FinishButton)));
    /* 末页 Back 可用（历史非空）。 */
    XQ_CHECK(XWidget_isEnabled(
        (XWidget*)XWizard_button(wiz, XWizardButton_BackButton)));

    /* 页 incomplete → Next 禁用（回第一页验证）。 */
    XWizard_back(wiz);
    XWizardPage_setComplete(p0, false);
    XQ_CHECK(!XWidget_isEnabled(
        (XWidget*)XWizard_button(wiz, XWizardButton_NextButton)));
    XWizardPage_setComplete(p0, true);

    /* 选项兑现：HaveHelpButton 打开即**显示**预建 Help（不新建），
     * 关闭即隐藏（按钮常驻，Qt 同；账本 242/243）。 */
    XWizard_setOption(wiz, XWizardOption_HaveHelpButton, true);
    XQ_CHECK(XWizard_testOption(wiz, XWizardOption_HaveHelpButton));
    XQ_CHECK_PTR(XWizard_button(wiz, XWizardButton_HelpButton));
    XQ_CHECK(!XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_HelpButton)));
    XWizard_setOptions(wiz, 0);
    XQ_CHECK_PTR(XWizard_button(wiz, XWizardButton_HelpButton));
    XQ_CHECK(XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_HelpButton)));
    /* HaveCustomButton1..3 打开即显示对应预建自定义槽。 */
    XWizard_setOption(wiz, XWizardOption_HaveCustomButton2, true);
    XQ_CHECK(!XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_CustomButton2)));
    XWizard_setOption(wiz, XWizardOption_HaveCustomButton2, false);

    /* NoCancelButton / NoBackButtonOnStartPage 兑现。 */
    XWizard_setOption(wiz, XWizardOption_NoCancelButton, true);
    XQ_CHECK(XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_CancelButton)));
    XWizard_setOption(wiz, XWizardOption_NoCancelButton, false);
    XWizard_setOption(wiz, XWizardOption_NoBackButtonOnStartPage, true);
    XQ_CHECK(XWidget_isHidden(
        (XWidget*)XWizard_button(wiz, XWizardButton_BackButton)));
    XWizard_setOption(wiz, XWizardOption_NoBackButtonOnStartPage, false);

    /* 自定义按钮槽 CustomButton1（QWizard::CustomButton1==6）：登记后
     * 点击发射 customButtonClicked(6)。 */
    {
        XPushButton* custom = XPushButton_create(NULL, 0);
        XQ_REQUIRE(custom != NULL);
        XWizard_setButton(wiz, XWizardButton_CustomButton1,
                          (XAbstractButton*)custom);
        XQ_CHECK(XWizard_button(wiz, XWizardButton_CustomButton1) ==
                 (XAbstractButton*)custom);
        wz_customClicked = -1;
        XAbstractButton_click((XAbstractButton*)custom);
        XQ_EQ_I64(wz_customClicked, (int)XWizardButton_CustomButton1);
        XQ_EQ_I64((int)XWizardButton_CustomButton1, 6);
        /* 借用承载：清除登记后归还调用方，自行释放；槽位回到预建按钮
         * （Qt setButton(NULL) 会 ensureButton 重建默认按钮）。 */
        XWizard_setButton(wiz, XWizardButton_CustomButton1, NULL);
        XQ_CHECK(XWizard_button(wiz, XWizardButton_CustomButton1) != NULL);
        XQ_CHECK(XWizard_button(wiz, XWizardButton_CustomButton1) !=
                 (XAbstractButton*)custom);
        XClassDelete((XClass*)custom);
    }

    /* 提交页：Commit 按钮替代 Next（qwizard.cpp:_q_updateButtonStates：
     * next 隐藏、commit 可见；账本 291）。 */
    {
        XWizardPage* p0 = (XWizardPage*)XWizard_page(wiz, 0);
        XWizardPage* p1 = (XWizardPage*)XWizard_page(wiz, 1);
        XQ_REQUIRE(p0 && p1);
        XWizardPage_setCommitPage(p0, true);
        XWizard_setCurrentId(wiz, 1); /* 先离开 p0 */
        XWizard_setCurrentId(wiz, 0); /* 再回到提交页 */
        XQ_CHECK(XWidget_isHidden(
            (XWidget*)XWizard_button(wiz, XWizardButton_NextButton)));
        XQ_CHECK(!XWidget_isHidden(
            (XWidget*)XWizard_button(wiz, XWizardButton_CommitButton)));
        XQ_CHECK(XWidget_isEnabled(
            (XWidget*)XWizard_button(wiz, XWizardButton_CommitButton)));
        XWizardPage_setCommitPage(p0, false);
        XWizard_setCurrentId(wiz, 1);
        XWizard_setCurrentId(wiz, 0);
        XQ_CHECK(!XWidget_isHidden(
            (XWidget*)XWizard_button(wiz, XWizardButton_NextButton)));
        XQ_CHECK(XWidget_isHidden(
            (XWidget*)XWizard_button(wiz, XWizardButton_CommitButton)));
    }

    /* 枚举值对齐（Qt 6.8 qwizard.h）。 */
    XQ_EQ_I64((int)XWizardButton_NStandardButtons, 6);
    XQ_EQ_I64((int)XWizardButton_NButtons, 9);
    XQ_EQ_I64((int)XWizardButton_NoButton, -1);
    XQ_EQ_I64((int)XWizardOption_ExtendedWatermarkPixmap, 4);
    XQ_EQ_I64((int)XWizardStyle_NStyles, 4);
    XQ_EQ_I64((int)XWizardPixmap_NPixmaps, 4);

    XClassDelete((XClass*)wiz);
}

/** @brief done 收口（Rejected → currentId -1 / Accepted 过校验门）。 */
XQ_TEST(XWizard, doneFlows)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XWizardPage* p1 = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(wiz && p0 && p1);

    XWizard_addPage(wiz, p0);
    XWizard_addPage(wiz, p1);

    /* Rejected：对标 QWizard::done → d->reset()（qwizard.cpp:688-704）：
     * 隐藏当前页、清理历史页、清空 history/访问史、**current = -1**、
     * 发 currentIdChanged(-1)；随后 QDialog::done 隐藏。本机 6.8.3 实测
     * done(Rejected) 后 currentId==-1、visitedIds 为空（账本 237）。 */
    XWizard_setCurrentId(wiz, 1);
    XWidget_show((XWidget*)wiz);
    XWizard_done(wiz, (int)XDialogCode_Rejected);
    XQ_EQ_I64(XDialog_result(&wiz->m_base), (int)XDialogCode_Rejected);
    XQ_CHECK(!XWidget_isVisible((XWidget*)wiz));
    XQ_EQ_I64(XWizard_currentId(wiz), -1);
    XQ_EQ_I64(XWizard_visitedIds(wiz, NULL, 0), 0);

    /* Accepted：当前页校验不过不关窗；过了才收口。 */
    XWizard_setCurrentId(wiz, 1);
    XQ_CHECK(XWizard_currentId(wiz) == 1);
    XWizardPage_setComplete(p1, false);
    XWidget_show((XWidget*)wiz);
    XWizard_done(wiz, (int)XDialogCode_Accepted);
    XQ_CHECK(XWidget_isVisible((XWidget*)wiz));
    XQ_EQ_I64(XDialog_result(&wiz->m_base), (int)XDialogCode_Rejected);
    XWizardPage_setComplete(p1, true);
    XWizard_done(wiz, (int)XDialogCode_Accepted);
    XQ_CHECK(!XWidget_isVisible((XWidget*)wiz));
    XQ_EQ_I64(XDialog_result(&wiz->m_base), (int)XDialogCode_Accepted);
    /* Qt 实测：Accepted 不复位当前页。 */
    XQ_EQ_I64(XWizard_currentId(wiz), 1);

    /* 页面 id 稳定：删中间页后再 addPage 分配新 id（不左移）。 */
    {
        XWizard* w2 = XWizard_create(NULL, 0);
        XWizardPage* a = XWizardPage_create(NULL, 0);
        XWizardPage* b = XWizardPage_create(NULL, 0);
        XWizardPage* c = XWizardPage_create(NULL, 0);
        XWizardPage* d = XWizardPage_create(NULL, 0);
        XQ_REQUIRE(w2 && a && b && c && d);
        XQ_EQ_I64(XWizard_addPage(w2, a), 0);
        XQ_EQ_I64(XWizard_addPage(w2, b), 1);
        XQ_EQ_I64(XWizard_addPage(w2, c), 2);
        XWizard_removePage(w2, 1);
        XQ_EQ_I64(XWizard_pageIds(w2, NULL, 0), 2); /* P2-48：pageCount 删 */
        XQ_CHECK(XWizard_page(w2, 1) == NULL);  /* id 1 已删、不左移 */
        XQ_CHECK(XWizard_page(w2, 2) == c);
        XQ_EQ_I64(XWizard_addPage(w2, d), 3);
        {
            int ids[4];
            XQ_EQ_I64(XWizard_pageIds(w2, ids, 4), 3);
            XQ_EQ_I64(ids[0], 0);
            XQ_EQ_I64(ids[1], 2);
            XQ_EQ_I64(ids[2], 3);
        }
        XClassDelete((XClass*)b); /* 已解绑，自行释放 */
        XClassDelete((XClass*)w2);
    }

    XClassDelete((XClass*)wiz);
}

/** @brief 缺省格式/文本 + 自定义按钮布局（setButtonLayout 参与布局）。 */
XQ_TEST(XWizard, defaultsAndButtonLayout)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    int i;
    XQ_REQUIRE(wiz && p0);

    /* Qt 6.8 实测：titleFormat/subTitleFormat 缺省 2(AutoText)；
     * 标准槽缺省文本非空（< &Back / &Next > / Commit / &Finish /
     * Cancel / &Help），自定义槽为空串（账本 240/241）。 */
    XQ_EQ_I64(XWizard_titleFormat(wiz), 2);
    XQ_EQ_I64(XWizard_subTitleFormat(wiz), 2);
    for (i = 0; i < XWizardButton_NStandardButtons; ++i) {
        XQ_CHECK_MSG(XWizard_buttonText(wiz, (XWizardButton)i)[0] != '\0',
                     "standard slot %d default text empty", i);
    }
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_CustomButton1), "");
    /* 显式设文本后返回自定义值；显式设空串保持空串（Qt 的
     * buttonCustomTexts.contains 语义）。 */
    XWizard_setButtonText(wiz, XWizardButton_NextButton, "继续");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_NextButton), "继续");
    XWizard_setButtonText(wiz, XWizardButton_NextButton, "");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_NextButton), "");

    /* 标题格式往返。 */
    XWizard_setTitleFormat(wiz, 1);
    XWizard_setSubTitleFormat(wiz, 1);
    XQ_EQ_I64(XWizard_titleFormat(wiz), 1);
    XQ_EQ_I64(XWizard_subTitleFormat(wiz), 1);

    XQ_EQ_I64(XWizard_addPage(wiz, p0), 0); /* 单页 ⇒ 末页：Finish 可见 */

    /* 自定义布局：只列 Cancel[左] + Stretch + Finish[右] ⇒ Back/Next 不
     * 出现在布局里被隐藏（Qt setButtonLayout 的 layoutContains 语义）。 */
    {
        const int seq[] = { (int)XWizardButton_CancelButton,
                            (int)XWizardButton_Stretch,
                            (int)XWizardButton_FinishButton };
        XWizard_setButtonLayout(wiz, seq, 3);
        XQ_CHECK(XWidget_isHidden(
            (XWidget*)XWizard_button(wiz, XWizardButton_BackButton)));
        XQ_CHECK(XWidget_isHidden(
            (XWidget*)XWizard_button(wiz, XWizardButton_NextButton)));
        XQ_CHECK(!XWidget_isHidden(
            (XWidget*)XWizard_button(wiz, XWizardButton_CancelButton)));
        XQ_CHECK(!XWidget_isHidden(
            (XWidget*)XWizard_button(wiz, XWizardButton_FinishButton)));
        XQ_EQ_I64(XWidget_x((XWidget*)XWizard_button(wiz,
                                                     XWizardButton_CancelButton)),
                  8);
        XQ_EQ_I64(XWidget_x((XWidget*)XWizard_button(wiz,
                                                     XWizardButton_FinishButton)),
                  XWidget_width((XWidget*)wiz) - 80 - 8);
    }
    /* 重复按钮的布局整份拒绝（Qt 打印 "Duplicate button in layout"）。 */
    {
        const int dup[] = { (int)XWizardButton_FinishButton,
                            (int)XWizardButton_FinishButton };
        XWizard_setButtonLayout(wiz, dup, 2);
        XQ_CHECK(!XWidget_isHidden(
            (XWidget*)XWizard_button(wiz, XWizardButton_FinishButton)));
        XQ_CHECK(XWidget_isHidden(
            (XWidget*)XWizard_button(wiz, XWizardButton_NextButton)));
    }

    XClassDelete((XClass*)wiz);
}

/* ==================== P0-43/P0-86：按钮文本 0..8 全受理 ==================== */

/** @brief setButtonText/buttonText 对 CustomButton1..3（6..8）受理、
 *         自定义槽回落槽位活动按钮自身文本、显式文本（含空串）下发
 *         登记按钮（qwizard.cpp:2595/2620-2635/2704）。 */
XQ_TEST(XWizard, buttonTextCustomSlots)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XPushButton* custom;
    XQ_REQUIRE(wiz && p0);
    XQ_EQ_I64(XWizard_addPage(wiz, p0), 0);

    /* P0-43：自定义槽 6..8 受理（此前守卫 which >= 6 直接 return，
     * 文本被静默丢弃；Qt ensureButton 对 NButtons=9 内槽位全受理）。 */
    XWizard_setButtonText(wiz, XWizardButton_CustomButton1, "自一甲");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_CustomButton1), "自一甲");
    XWizard_setButtonText(wiz, XWizardButton_CustomButton2, "自二");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_CustomButton2), "自二");
    XWizard_setButtonText(wiz, XWizardButton_CustomButton3, "自三");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_CustomButton3), "自三");
    /* Stretch(9)/NoButton(-1)/越界不受理（Qt ensureButton false）。 */
    XWizard_setButtonText(wiz, XWizardButton_Stretch, "x");
    XWizard_setButtonText(wiz, XWizardButton_NoButton, "x");
    XWizard_setButtonText(wiz, (XWizardButton)42, "x");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_Stretch), "");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_NoButton), "");
    XQ_EQ_STR(XWizard_buttonText(wiz, (XWizardButton)42), "");

    /* P0-86：显式设空即空串（custom 位区分「设过空」与「未设」）。 */
    XWizard_setButtonText(wiz, XWizardButton_CustomButton1, "");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_CustomButton1), "");

    /* P0-86：setButton 登记按钮后 buttonText 回落**登记按钮自身文本**
     * （Qt 的 d->btns[which]->text()，qwizard.cpp:2634；登记按钮即被
     * setButton 写进 btns[which] 的按钮，qwizard.cpp:2704）。 */
    custom = XPushButton_create(NULL, 0);
    XQ_REQUIRE(custom != NULL);
    XAbstractButton_setText_2((XAbstractButton*)custom, "登记按钮甲");
    XWizard_setButton(wiz, XWizardButton_CustomButton1,
                      (XAbstractButton*)custom);
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_CustomButton1),
              "登记按钮甲");
    /* P0-43：显式文本下发**登记按钮**（此前 xwiz_applySlot 对登记槽位
     * 提前 return，登记按钮收不到文本）。 */
    XWizard_setButtonText(wiz, XWizardButton_CustomButton1, "自一新");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_CustomButton1), "自一新");
    XQ_CHECK(XAbstractButton_text((XAbstractButton*)custom) &&
             strcmp(XString_toUtf8(
                        XAbstractButton_text((XAbstractButton*)custom)),
                    "自一新") == 0);
    /* P0-43：显式空串**清空**按钮文本（此前 `txt[0]` 闸跳过空串）。 */
    XWizard_setButtonText(wiz, XWizardButton_CustomButton1, "");
    XQ_CHECK(XAbstractButton_text((XAbstractButton*)custom) &&
             XString_toUtf8_length(
                 XAbstractButton_text((XAbstractButton*)custom)) == 0);
    /* 借用承载：清除登记归还调用方（XWizard.h 契约），槽位回到预建
     * 按钮（文本空 ⇒ buttonText 空串）。 */
    XWizard_setButton(wiz, XWizardButton_CustomButton1, NULL);
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_CustomButton1), "");
    XClassDelete((XClass*)custom);
    XClassDelete((XClass*)wiz);
}

/* ==================== P0-44：removePage 四情形收口 ==================== */

/** @brief 删页导航/可见性/清理移交（qwizard.cpp:2244-2265 的四情形）。
 *  @details 修复前：删当前页只夹 m_currentIndex ⇒ 被删页仍 visible 继续
 *           画、currentPage 指另一张隐藏页、不发 currentIdChanged；
 *           且删「当前页之前」的页后 dense 页表左移，current 下标不跟
 *           ⇒ currentPage 指错页。 */
XQ_TEST(XWizard, removePageCases)
{
    /* 情形 2：历史非当前 → 摘除 + 摘历史条目；current 与可见页不动，
     * 且**当前页下标随页表左移修复**（删的是当前页之前的页）。 */
    {
        XWizard* wiz = XWizard_create(NULL, 0);
        XWizardPage* p0 = XWizardPage_create(NULL, 0);
        XWizardPage* p1 = XWizardPage_create(NULL, 0);
        XWizardPage* p2 = XWizardPage_create(NULL, 0);
        XQ_REQUIRE(wiz && p0 && p1 && p2);
        XWizard_addPage(wiz, p0);
        XWizard_addPage(wiz, p1);
        XWizard_addPage(wiz, p2);
        XWizard_setCurrentId(wiz, 2); /* 前进：历史只含直接前驱 [0]
                                       * （Qt history 含当前页为 [0,2]；
                                       * 页 1 未访问不入栈，探针
                                       * hasVisitedPage(1)==0） */
        XWizard_removePage(wiz, 0);   /* id 0 在历史中且非当前 */
        XQ_EQ_I64(XWizard_currentId(wiz), 2);
        XQ_CHECK(XWizard_currentPage(wiz) == p2); /* 下标修复后仍是 p2 */
        XQ_CHECK(!XWidget_isHidden((XWidget*)p2)); /* 可见页仍是当前页 */
        XQ_CHECK(XWidget_isHidden((XWidget*)p0)); /* 被删页不再可见 */
        XQ_CHECK(XWizard_page(wiz, 0) == NULL);
        /* 历史条目已摘（历史空）：back() 无栈可退**不动**——本机 Qt
         * 6.8.3 offscreen 探针同序列实测 currentId 保持 2、currentPage
         * 仍是 p2（Qt history 含当前页为 [0,2]，摘 0 后仅剩 [2]，
         * back 的 n=size-2<0 直接 return；与 navigationHistory 用例
         * 「0→2 跳页后 back 回 0」锁定的单前驱入栈口径同源）。若历史
         * 残留 0，back 会误消耗栈（历史计数失真、Back 使能错判）。 */
        XWizard_back(wiz);
        XQ_EQ_I64(XWizard_currentId(wiz), 2);
        XQ_CHECK(XWizard_currentPage(wiz) == p2);
        XQ_CHECK(!XWidget_isHidden((XWidget*)p2));
        XClassDelete((XClass*)p0); /* 已解绑，自行释放 */
        XClassDelete((XClass*)wiz);
    }
    /* 情形 1：非历史非当前 → 仅摘除（current/可见页不动）。 */
    {
        XWizard* wiz = XWizard_create(NULL, 0);
        XWizardPage* p0 = XWizardPage_create(NULL, 0);
        XWizardPage* p1 = XWizardPage_create(NULL, 0);
        XWizardPage* p2 = XWizardPage_create(NULL, 0);
        XQ_REQUIRE(wiz && p0 && p1 && p2);
        XWizard_addPage(wiz, p0);
        XWizard_addPage(wiz, p1);
        XWizard_addPage(wiz, p2); /* current=0，history 空 */
        XWizard_removePage(wiz, 2);
        XQ_EQ_I64(XWizard_currentId(wiz), 0);
        XQ_CHECK(XWizard_currentPage(wiz) == p0);
        XQ_CHECK(!XWidget_isHidden((XWidget*)p0));
        XQ_CHECK(XWizardPage_wizard(p2) == NULL);
        XClassDelete((XClass*)p2); /* 已解绑，自行释放 */
        XClassDelete((XClass*)wiz);
    }
    /* 情形 3：当前页且历史空（Qt history.size()==1）→ reset() + 摘页
     * + restart()：currentIdChanged(-1) 后进新起始页；被删页隐藏、
     * 解绑、已清理（initialized 复位）。 */
    {
        XWizard* wiz = XWizard_create(NULL, 0);
        XWizardPage* p0 = XWizardPage_create(NULL, 0);
        XWizardPage* p1 = XWizardPage_create(NULL, 0);
        XQ_REQUIRE(wiz && p0 && p1);
        XWizard_addPage(wiz, p0);
        XWizard_addPage(wiz, p1);
        XQ_CHECK(p0->m_initialized); /* 首页挂载即进入并初始化 */
        XWizard_removePage(wiz, 0);
        /* reset（current=-1）+ 摘页 + restart 进调整后的起始页 1。 */
        XQ_EQ_I64(XWizard_currentId(wiz), 1);
        XQ_CHECK(XWizard_currentPage(wiz) == p1);
        XQ_CHECK(XWidget_isHidden((XWidget*)p0)); /* 不再继续画 */
        XQ_CHECK(!XWidget_isHidden((XWidget*)p1)); /* 可见性移交新起始页 */
        XQ_CHECK(XWizardPage_wizard(p0) == NULL);
        XQ_CHECK(!p0->m_initialized); /* reset 对被删当前页做过清理 */
        XQ_CHECK(XWizard_hasVisitedPage(wiz, 1)); /* restart 重建访问史 */
        XQ_CHECK(!XWizard_hasVisitedPage(wiz, 0));
        XClassDelete((XClass*)p0); /* 已解绑，自行释放 */
        XClassDelete((XClass*)wiz);
    }
    /* 情形 4：当前页且非历史首条 → back()：可见性/清理移交给历史栈顶，
     * 被删页作为旧页在切换时被隐藏并 cleanup（摘页前、页仍在页表时）。 */
    {
        XWizard* wiz = XWizard_create(NULL, 0);
        XWizardPage* p0 = XWizardPage_create(NULL, 0);
        XWizardPage* p1 = XWizardPage_create(NULL, 0);
        XWizardPage* p2 = XWizardPage_create(NULL, 0);
        XQ_REQUIRE(wiz && p0 && p1 && p2);
        XWizard_addPage(wiz, p0);
        XWizard_addPage(wiz, p1);
        XWizard_addPage(wiz, p2);
        XWizard_next(wiz);
        XWizard_next(wiz); /* history=[0,1]，current=2 */
        XQ_CHECK(p2->m_initialized);
        XWizard_removePage(wiz, 2);
        XQ_EQ_I64(XWizard_currentId(wiz), 1); /* back 到历史栈顶 */
        XQ_CHECK(XWizard_currentPage(wiz) == p1);
        XQ_CHECK(XWidget_isHidden((XWidget*)p2)); /* 被删页不再可见 */
        XQ_CHECK(!XWidget_isHidden((XWidget*)p1)); /* 可见性移交 */
        XQ_CHECK(!p2->m_initialized); /* back 对被删页 cleanupPage */
        XQ_CHECK(XWizardPage_wizard(p2) == NULL);
        XClassDelete((XClass*)p2); /* 已解绑，自行释放 */
        XClassDelete((XClass*)wiz);
    }
}

/* ==================== P0-45/P0-48：样式与四槽图兑现 ==================== */

/** @brief setWizardStyle 兑现（缺省按钮文案随样式 + 预建按钮改写）；
 *         setPixmap 水印参与布局（列让位/样式门/清除回落）。 */
XQ_TEST(XWizard, styleAndPixmapRendering)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XWizardPage* p1 = XWizardPage_create(NULL, 0);
    struct XAbstractButton* backBtn;
    XQ_REQUIRE(wiz && p0 && p1);
    XQ_EQ_I64(XWizard_addPage(wiz, p0), 0);
    XQ_EQ_I64(XWizard_addPage(wiz, p1), 1);

    /* 缺省文案随样式（buttonDefaultText 的 mac 分支，qwizard.cpp:614-640；
     * Qt C locale 非 Mac 为 "< &Back"/"&Next >"，中文缺省同前映射）。 */
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_BackButton), "< 上一页");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_NextButton), "下一页 >");
    XWizard_setWizardStyle(wiz, XWizardStyle_MacStyle);
    XQ_EQ_I64((int)XWizard_wizardStyle(wiz), (int)XWizardStyle_MacStyle);
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_BackButton), "返回");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_NextButton), "继续");
    /* 预建按钮文本随样式下发改写（setWizardStyle → updateButtonTexts，
     * qwizard.cpp:2478）。 */
    backBtn = XWizard_button(wiz, XWizardButton_BackButton);
    XQ_CHECK_PTR(backBtn);
    XQ_CHECK(backBtn && XAbstractButton_text(backBtn) &&
             strcmp(XString_toUtf8(XAbstractButton_text(backBtn)),
                    "返回") == 0);
    XWizard_setWizardStyle(wiz, XWizardStyle_ModernStyle);
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_BackButton), "< 上一页");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_NextButton), "下一页 >");
    /* 向导级自定义文本优先于缺省层，不受样式切换影响（Qt 的
     * buttonCustomTexts.contains 先于 buttonDefaultText）。 */
    XWizard_setButtonText(wiz, XWizardButton_BackButton, "自定义返回");
    XWizard_setWizardStyle(wiz, XWizardStyle_MacStyle);
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_BackButton), "自定义返回");

    /* P0-48：水印参与布局——页区右侧让出列宽（自然宽、上限 2/5 窗宽）；
     * 样式门（Mac 不显水印）与清除回落。临时图先探针解码，不可解码的
     * 环境只跳过布局断言（同 XComboBoxAlignTest 的环境无关先例）。 */
    {
        const char* wmPath = "/tmp/xwizard_align_wm.png";
        int wizW = XWidget_width((XWidget*)wiz);
        XImage image;
        XImage probeImg;
        bool decodable;
        XImage_init_ex(&image, 120, 200, XImageFormat_ARGB32);
        XImage_fill(&image, 0xFF3366CCu);
        XImage_save_2(&image, wmPath, "png", 100);
        XClassDeinit((XClass*)&image);
        XImage_init(&probeImg);
        decodable = XImage_load_2(&probeImg, wmPath, NULL);
        XClassDeinit((XClass*)&probeImg);
        XWizard_setWizardStyle(wiz, XWizardStyle_ClassicStyle);
        XWizardPage_setPixmap_2(p0, (int)XWizardPixmap_WatermarkPixmap,
                                wmPath);
        if (decodable) {
            XQ_CHECK(XWidget_width((XWidget*)p0) < wizW);
            XQ_CHECK(XWidget_width((XWidget*)p0) >= wizW - wizW * 2 / 5);
            /* 样式门：Mac 无水印列 ⇒ 页宽恢复整宽。 */
            XWizard_setWizardStyle(wiz, XWizardStyle_MacStyle);
            XQ_EQ_I64(XWidget_width((XWidget*)p0), wizW);
            /* 回 Classic ⇒ 列回来。 */
            XWizard_setWizardStyle(wiz, XWizardStyle_ClassicStyle);
            XQ_CHECK(XWidget_width((XWidget*)p0) < wizW);
            /* 清除页水印 ⇒ 回落向导同槽（未设）⇒ 列消失。 */
            XWizardPage_setPixmap_2(p0, (int)XWizardPixmap_WatermarkPixmap,
                                    NULL);
            XQ_EQ_I64(XWidget_width((XWidget*)p0), wizW);
        }
        remove(wmPath);
    }
    XClassDelete((XClass*)wiz);
}

/* ==================== P0-46/47/P0-E6：横幅文本格式与无截断 ==================== */

#if XFRAME_ON && XLABEL_ON
/** @brief 标题/副标题由内建 XLabel 承载：格式 0/1/2 兑现（AutoText 探测
 *         与 XLabel 同源）、>255 字节标题无截断（旧实现 256 字节栈缓冲
 *         XSnprintf 静默截断且可能截断 UTF-8 多字节序列）。 */
XQ_TEST(XWizard, bannerTextFormats)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XLabel* title;
    XLabel* sub;
    static char big[301];
    int i;
    XQ_REQUIRE(wiz && p0);
    for (i = 0; i < 300; ++i) big[i] = (char)('A' + (i % 26));
    big[300] = '\0';
    XWizardPage_setTitle(p0, big);
    XQ_EQ_I64((int)strlen(XWizardPage_title(p0)), 300);
    XQ_EQ_I64(XWizard_addPage(wiz, p0), 0);
    title = wiz->m_titleLabel;
    sub = wiz->m_subTitleLabel;
    XQ_CHECK_PTR(title);
    XQ_CHECK_PTR(sub);
    /* 文本全量进标签（直传，无中间缓冲）。 */
    XQ_CHECK(title && XLabel_text(title) &&
             strcmp(XString_toUtf8(XLabel_text(title)), big) == 0);
    /* 缺省格式 AutoText(2)，与 m_titleFormat 缺省一致。 */
    XQ_EQ_I64((int)XLabel_textFormat(title), 2);
    /* P0-46：setTitleFormat 兑现到标题标签（0/1/2 与 XLabelTextFormat
     * 数值一致）。 */
    XWizard_setTitleFormat(wiz, 1); /* RichText */
    XQ_EQ_I64((int)XLabel_textFormat(title), 1);
    XWizard_setTitleFormat(wiz, 0); /* PlainText */
    XQ_EQ_I64((int)XLabel_textFormat(title), 0);
    /* P0-47：setSubTitleFormat 兑现到副标题标签（一次做两条标签）。 */
    XWizardPage_setSubTitle(p0, "副标题文本");
    XQ_CHECK(sub && !XWidget_isHidden((XWidget*)sub));
    XWizard_setSubTitleFormat(wiz, 1);
    XQ_EQ_I64((int)XLabel_textFormat(sub), 1);
    XWizard_setSubTitleFormat(wiz, 0);
    XQ_EQ_I64((int)XLabel_textFormat(sub), 0);
    /* 文本清空 ⇒ 对应标签隐藏（Qt 无标题时不画标题行）。 */
    XWizardPage_setSubTitle(p0, "");
    XQ_CHECK(sub && XWidget_isHidden((XWidget*)sub));
    XWizardPage_setTitle(p0, "");
    XQ_CHECK(title && XWidget_isHidden((XWidget*)title));
    XClassDelete((XClass*)wiz); /* 标签为子控件随父级联释放 */
}
#endif /* XFRAME_ON && XLABEL_ON */

/* ==================== P3-48：sizeHint 虚槽 ==================== */

/** @brief 首选尺寸 = 当前页首选 + 横幅高 + 按钮带 40 + 边距，再
 *         expandedTo (500,360)（Qt QWizard::sizeHint 的下限语义；
 *         无页时内容为 0 ⇒ 恰为下限）。 */
XQ_TEST(XWizard, sizeHintFloor)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XSize hint;
    XSize pageHint;
    int decoTop;
    int expW;
    int expH;
    XQ_REQUIRE(wiz && p0);
    /* 无页：hint(0,0) expandedTo (500,360) ⇒ 恰为下限。 */
    hint = XWidget_sizeHint_base((XWidget*)wiz);
    XQ_EQ_I64(hint.width, XWIZARD_SIZEHINT_MIN_WIDTH);
    XQ_EQ_I64(hint.height, XWIZARD_SIZEHINT_MIN_HEIGHT);
    /* 有页：与实现同源公式（页首选 + 16 边距 + CSD 让位 + 横幅 32 +
     * 按钮带 40 + 下边距 12）。 */
    XQ_EQ_I64(XWizard_addPage(wiz, p0), 0);
    pageHint = XWidget_sizeHint_base((XWidget*)p0);
    if (pageHint.width < 0) pageHint.width = 0;
    if (pageHint.height < 0) pageHint.height = 0;
    decoTop = XDialog_decorationTopOffset((const XDialog*)wiz);
    hint = XWidget_sizeHint_base((XWidget*)wiz);
    expW = pageHint.width + 16;
    expH = pageHint.height + decoTop + 32 + 40 + 12;
    if (expW < XWIZARD_SIZEHINT_MIN_WIDTH) expW = XWIZARD_SIZEHINT_MIN_WIDTH;
    if (expH < XWIZARD_SIZEHINT_MIN_HEIGHT)
        expH = XWIZARD_SIZEHINT_MIN_HEIGHT;
    XQ_EQ_I64(hint.width, expW);
    XQ_EQ_I64(hint.height, expH);
    XClassDelete((XClass*)wiz);
}

/* ==================== 向导级虚槽（initializePage/cleanupPage id） ==================== */

XCLASS_DEFINE_BEGING(XqWizardProbe)
XCLASS_DEFINE_EXTEND_END(XqWizardProbe, XWizard)

typedef struct XqWizardProbe
{
    XWizard m_base;
    int m_initId;
    int m_cleanupId;
    int m_initCount;
    int m_cleanupCount;
} XqWizardProbe;

static void XqWizardProbe_initializePage(XWizard* self, int id)
{
    XqWizardProbe* p = (XqWizardProbe*)self;
    p->m_initId = id;
    ++p->m_initCount;
    /* 不转调基类默认实现：本探针只观察向导级虚槽分派（id 载荷）。 */
}

static void XqWizardProbe_cleanupPage(XWizard* self, int id)
{
    XqWizardProbe* p = (XqWizardProbe*)self;
    p->m_cleanupId = id;
    ++p->m_cleanupCount;
}

static XVtable* XqWizardProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XqWizardProbe);
    XVTABLE_INHERIT_XCLASS(XWizard);
    XVTABLE_OVERLOAD_DEFAULT(EXWizard_InitializePage,
                             XqWizardProbe_initializePage);
    XVTABLE_OVERLOAD_DEFAULT(EXWizard_CleanupPage, XqWizardProbe_cleanupPage);
    return XVTABLE_DEFAULT;
}

static XqWizardProbe* XqWizardProbe_create(void)
{
    XqWizardProbe* self = (XqWizardProbe*)XMemory_malloc(
        sizeof(*self), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(*self));
    XWizard_init(&self->m_base, NULL, 0);
    XClassSetVtable(self, XqWizardProbe);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    self->m_initId = -1;
    self->m_cleanupId = -1;
    return self;
}

/** @brief QWizard::initializePage(int)/cleanupPage(int) 虚槽分派。 */
XQ_TEST(XWizard, virtualPageHooks)
{
    XqWizardProbe* wiz = XqWizardProbe_create();
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XWizardPage* p1 = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(wiz && p0 && p1);

    XQ_EQ_I64(XWizard_addPage(&wiz->m_base, p0), 0);
    XQ_EQ_I64(wiz->m_initId, 0); /* 首页进入经向导级虚槽，载荷=页 id */
    XQ_EQ_I64(XWizard_addPage(&wiz->m_base, p1), 1);
    XWizard_next(&wiz->m_base);
    XQ_EQ_I64(XWizard_currentId(&wiz->m_base), 1);
    XQ_EQ_I64(wiz->m_initId, 1);
    XWizard_back(&wiz->m_base);
    XQ_EQ_I64(wiz->m_cleanupId, 1); /* 后退离开的是页 1 */
    /* 回到页 0 **不再** initializePage：Qt 的 initialized 只在 Backward
     * 对**被离开页**复位（qwizard.cpp:767-771），Forward 离开的页 0
     * 仍保持 initialized=true（本机 6.8.0 探针：restart→next→back 依次
     * 打印 init 0 / init 1 / cleanup 1，回页 0 无 init）。此前的
     * `m_initId==0` 断言与 Qt 相反。 */
    XQ_EQ_I64(wiz->m_initId, 1);
    XQ_EQ_I64(wiz->m_initCount, 2);
    XQ_EQ_I64(wiz->m_cleanupCount, 1);

    XClassDelete((XClass*)wiz);
}

#endif /* XGUI_ON && XWIDGET_ON && XDIALOG_ON && XWIZARD_ON */
