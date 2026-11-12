/**
 * @file       XWizardPageAlignTest.c
 * @brief      XWizardPage × Qt 6.8 QWizardPage 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /usr/include/x86_64-linux-gnu/qt6/QtWidgets/
 *               qwizard.h（setTitle/title、setSubTitle/subTitle、
 *               setPixmap/pixmap、setFinalPage/isFinalPage、
 *               setCommitPage/isCommitPage、setButtonText/buttonText、
 *               virtual initializePage/cleanupPage/validatePage/
 *               isComplete/nextId、信号 completeChanged、protected
 *               setField/field/registerField/wizard）；
 *             - Qt 6.8 源码 src/widgets/dialogs/qwizard.cpp 原文：
 *               * isComplete 是 **virtual**（:3598-3629），向导的
 *                 Next/Finish 使能经 currentPage()->isComplete() 取值
 *                 （:1631-1638），validateCurrentPage 亦然（:3280-3295）；
 *               * setFinalPage（:3643-3660）= `explicitlyFinal = finalPage`
 *                 ——传 false 时**回落** `nextId() == -1` 推导，不是
 *                 "强制非末页"；isFinalPage（:3664-3680）对非当前页走
 *                 本页 `nextId() == -1`；
 *               * buttonText（:3742-3752）：页级自定义优先，否则回落
 *                 `wizard()->buttonText(which)`；
 *               * pixmap（:3503-3515）：页图非空返回页图，否则回落
 *                 `wizard()->pixmap(which)`；
 *               * validatePage 默认 true（:3576-3580）；
 *               * nextId 默认返回页 ID 序的下一个（:3771-3793）。
 *             - 本机 Qt 6.8.0 offscreen 探针实测：
 *               ① 三页向导中非当前的末页 isFinalPage()==1；末页
 *                  setFinalPage(false) 后仍为 1（回落 nextId()==-1）；
 *                  **未加入向导的独立页 isFinalPage()==1**（下表 ②）；
 *               ② 页未设 buttonText 时返回向导级文本（"WNext"），页级
 *                  设置后页返回 "PNext"、向导仍 "WNext"；
 *               ③ 页未设 pixmap 时返回向导级 pixmap；页设 Watermark 后
 *                  读 Logo **仍回落向导 Logo**（四槽独立存储）；
 *               ④ 独立页 isComplete()==1、validatePage()==1、nextId()==-1、
 *                  buttonText()==""、pixmap() 空。
 *
 *             r2 已对齐（原登记偏差撤回）：
 *             ① 独立页 isFinalPage() 现返回 true（账本 239）；旧 apitest
 *                Test/XGuiDemo/xgui_demo_apitest_dialogs.c:2041 的 false
 *                断言见 testMigrations；
 *             ② 页图四槽独立存储（账本 244/292）；旧演示 :2065-2073 的
 *                单槽断言见 testMigrations；
 *             ③ setCommitPage 参与按钮布局（Commit 按钮替代 Next）；
 *             ④ registerField 已实现：必填字段（名字末尾 `*`）未填 ⇒
 *                isComplete()==false，cleanupPage 恢复字段初值
 *                （账本 290）。
 * @note       C99；页面随向导析构（父子所有权），独立页显式释放，
 *             漏删由运行器 `[LEAK]` 判失败。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XDIALOG_ON && XWIZARD_ON

#include "XWizard.h"
#include "XWidget.h"
#include "XObject.h"
#include "XClass.h"
#include "XVarList.h"
#include "XString.h"
#include "XContainer.h"
#include "XMemory.h"

/* ==================== completeChanged 探针 ==================== */

static int g_wpCompleteChanged;

static void xwpCompleteSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++g_wpCompleteChanged;
}

/* ==================== isComplete 虚槽子类探针 ==================== */

XCLASS_DEFINE_BEGING(XqPageProbe)
XCLASS_DEFINE_EXTEND_END(XqPageProbe, XWizardPage)

typedef struct XqPageProbe
{
    XWizardPage m_base;       /**< 基类成员；必须是第一个。 */
    bool m_overrideComplete;  /**< 子类 isComplete 覆盖值。 */
} XqPageProbe;

static bool XqPageProbe_isComplete(const XWizardPage* self)
{
    return ((const XqPageProbe*)self)->m_overrideComplete;
}

static XVtable* XqPageProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XqPageProbe);
    XVTABLE_INHERIT_XCLASS(XWizardPage);
    XVTABLE_OVERLOAD_DEFAULT(EXWizardPage_IsComplete,
                             XqPageProbe_isComplete);
    return XVTABLE_DEFAULT;
}

static XqPageProbe* XqPageProbe_create(bool complete)
{
    XqPageProbe* self = (XqPageProbe*)XMemory_malloc(
        sizeof(*self), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    XMemset(self, 0, sizeof(*self));
    XWizardPage_init(&self->m_base, NULL, 0);
    XClassSetVtable(self, XqPageProbe);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    self->m_overrideComplete = complete;
    return self;
}

/* ==================== ① 缺省值 ==================== */

XQ_TEST(XWizardPage, defaults)
{
    XWizardPage* page = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(page != NULL);

    XQ_EQ_STR(XWizardPage_title(page), "");
    XQ_EQ_STR(XWizardPage_subTitle(page), "");
    /* Qt 默认 isComplete()==true（无 registerField 必填字段时）。 */
    XQ_CHECK(XWizardPage_isComplete(page));
    XQ_CHECK(!XWizardPage_isCommitPage(page));
    /* 独立页 isFinalPage()==true：nextId()==-1 ⇒ 末页（qwizard.cpp:
     * 3664-3680；本机 6.8.0/6.8.3 实测 1；r2 对齐，账本 239）。 */
    XQ_CHECK(XWizardPage_isFinalPage(page));
    XQ_CHECK(XWizardPage_wizard(page) == NULL);
    XQ_EQ_I64(XWizardPage_nextId(page), -1);
    /* validatePage 默认 true（qwizard.cpp:3576-3580）。 */
    XQ_CHECK(XWizardPage_validatePage(page));
    /* 无向导 ⇒ buttonText 空串、pixmap NULL（Qt 探针同）。 */
    XQ_EQ_STR(XWizardPage_buttonText(page, XWizardButton_NextButton), "");
    XQ_CHECK(XWizardPage_pixmap(page, XWizardPixmap_WatermarkPixmap) == NULL);
    /* 越界 which 安全返回空串/NULL。 */
    XQ_EQ_STR(XWizardPage_buttonText(page, (XWizardButton)99), "");
    XQ_EQ_STR(XWizardPage_buttonText(page, XWizardButton_NoButton), "");

    XClassDelete((XClass*)page);
}

/* ==================== ② setFinalPage(false) 回落推导 ==================== */

XQ_TEST(XWizardPage, finalPageFallsBack)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XWizardPage* p1 = XWizardPage_create(NULL, 0);
    XWizardPage* p2 = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(wiz != NULL && p0 != NULL && p1 != NULL && p2 != NULL);
    XQ_EQ_I64(XWizard_addPage(wiz, p0), 0);
    XQ_EQ_I64(XWizard_addPage(wiz, p1), 1);
    XQ_EQ_I64(XWizard_addPage(wiz, p2), 2);

    /* 当前页 p0、中间页 p1、末页 p2：p2 的 nextId()==-1，非当前也判末页
     * （qwizard.cpp:3664-3680 的非当前分支；Qt 6.8.0 探针 final2=1）。 */
    XQ_CHECK(XWizard_currentPage(wiz) == p0);
    XQ_CHECK(!XWizardPage_isFinalPage(p0));
    XQ_CHECK(!XWizardPage_isFinalPage(p1));
    XQ_CHECK(XWizardPage_isFinalPage(p2));

    /* setFinalPage(true) 早结束 ⇒ true；false 回落 nextId != -1 ⇒ false。 */
    XWizardPage_setFinalPage(p1, true);
    XQ_CHECK(XWizardPage_isFinalPage(p1));
    XWizardPage_setFinalPage(p1, false);
    XQ_CHECK(!XWizardPage_isFinalPage(p1));

    /* 关键回归：末页 setFinalPage(false) 后仍应是末页（Qt 的
     * explicitlyFinal=false ⇒ nextId()==-1 ⇒ true）。旧实现把 false 也
     * 当"显式值"锁存，返回 false。 */
    XWizardPage_setFinalPage(p2, false);
    XQ_CHECK(XWizardPage_isFinalPage(p2));

    /* 非末页 setFinalPage(true) 仍可提前完成。 */
    XWizardPage_setFinalPage(p2, true);
    XQ_CHECK(XWizardPage_isFinalPage(p2));
    XWizardPage_setFinalPage(p0, true);
    XQ_CHECK(XWizardPage_isFinalPage(p0));
    XWizardPage_setFinalPage(p0, false);
    XQ_CHECK(!XWizardPage_isFinalPage(p0));

    XClassDelete((XClass*)wiz);
}

/* ==================== ③ buttonText：页级 → 向导级回落 ==================== */

XQ_TEST(XWizardPage, buttonTextFallsBackToWizard)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* page = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(wiz != NULL && page != NULL);
    XQ_EQ_I64(XWizard_addPage(wiz, page), 0);

    /* 未设页级文本：回落向导级自定义文本（qwizard.cpp:3742-3752）。 */
    XWizardPage_setButtonText(page, XWizardButton_NextButton, NULL);
    XWizard_setButtonText(wiz, XWizardButton_NextButton, "向导下一页");
    XQ_EQ_STR(XWizardPage_buttonText(page, XWizardButton_NextButton),
              "向导下一页");

    /* 页级自定义覆盖向导级；向导级读取不受影响。 */
    XWizardPage_setButtonText(page, XWizardButton_NextButton, "页下一页");
    XQ_EQ_STR(XWizardPage_buttonText(page, XWizardButton_NextButton),
              "页下一页");
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_NextButton),
              "向导下一页");

    /* 清除页级文本（NULL）→ 恢复回落。
     * setButtonText(which, "") 存的是"自定义空串"，仍算页级设置（Qt 的
     * contains(which) 语义），因此用 NULL 清除。 */
    XWizardPage_setButtonText(page, XWizardButton_NextButton, NULL);
    XQ_EQ_STR(XWizardPage_buttonText(page, XWizardButton_NextButton),
              "向导下一页");

    /* 不同按钮槽互不串扰：Back 槽未设任何自定义文本 ⇒ 回落向导缺省文本
     * （Qt buttonText 的缺省层，qwizard.cpp:2623-2648；r2 对齐，账本 241）。 */
    XQ_CHECK(XWizardPage_buttonText(page, XWizardButton_BackButton)[0] != '\0');
    XQ_EQ_STR(XWizard_buttonText(wiz, XWizardButton_BackButton),
              XWizardPage_buttonText(page, XWizardButton_BackButton));

    /* 2026-11-12 批次（P0-43 配套，对标 qwizard.cpp:3718-3724 与
     * updateButtonTexts 的页级分支 1397-1398）：页级文本在本页为当前页
     * 时**立即下发**按钮行，页级优先于向导级；NULL 清除后按钮回到
     * 向导级自定义文本。 */
    {
        struct XAbstractButton* nextBtn =
            XWizard_button(wiz, XWizardButton_NextButton);
        XQ_CHECK_PTR(nextBtn);
        XWizardPage_setButtonText(page, XWizardButton_NextButton, "页显示甲");
        XQ_CHECK(nextBtn && XAbstractButton_text(nextBtn) &&
                 strcmp(XString_toUtf8(XAbstractButton_text(nextBtn)),
                        "页显示甲") == 0);
        XWizardPage_setButtonText(page, XWizardButton_NextButton, NULL);
        XQ_CHECK(nextBtn && XAbstractButton_text(nextBtn) &&
                 strcmp(XString_toUtf8(XAbstractButton_text(nextBtn)),
                        "向导下一页") == 0);
    }

    XClassDelete((XClass*)wiz);
}

/* ==================== ④ pixmap：页图 → 向导图回落 ==================== */

XQ_TEST(XWizardPage, pixmapFallsBackToWizard)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* page = XWizardPage_create(NULL, 0);
    const XString* got;
    XQ_REQUIRE(wiz != NULL && page != NULL);
    XQ_EQ_I64(XWizard_addPage(wiz, page), 0);

    XQ_CHECK(XWizardPage_pixmap(page, XWizardPixmap_LogoPixmap) == NULL);

    /* 页图未设：回落向导图（qwizard.cpp:3503-3515）。 */
    XWizard_setPixmap_2(wiz, XWizardPixmap_LogoPixmap, "wizard-logo.png");
    got = XWizardPage_pixmap(page, XWizardPixmap_LogoPixmap);
    XQ_CHECK_PTR(got);
    XQ_CHECK(got && strcmp(XString_toUtf8(got), "wizard-logo.png") == 0);

    /* 页图非空：页图优先；**四槽独立存储**——设 Watermark 后读 Logo
     * 仍回落到向导 Logo（qwizard.cpp:3503-3515；r2 对齐，账本 244/292）。 */
    XWizardPage_setPixmap_2(page, XWizardPixmap_WatermarkPixmap,
                            "page-watermark.png");
    got = XWizardPage_pixmap(page, XWizardPixmap_WatermarkPixmap);
    XQ_CHECK_PTR(got);
    XQ_CHECK(got && strcmp(XString_toUtf8(got), "page-watermark.png") == 0);
    got = XWizardPage_pixmap(page, XWizardPixmap_LogoPixmap);
    XQ_CHECK(got && strcmp(XString_toUtf8(got), "wizard-logo.png") == 0);
    /* 未设的 Banner/Background 槽既无页图也无向导图 ⇒ NULL。 */
    XQ_CHECK(XWizardPage_pixmap(page, XWizardPixmap_BannerPixmap) == NULL);

    /* 清除页 Watermark → 该槽回到向导回落（向导同槽未设 ⇒ NULL）。 */
    XWizardPage_setPixmap_2(page, XWizardPixmap_WatermarkPixmap, NULL);
    XQ_CHECK(XWizardPage_pixmap(page, XWizardPixmap_WatermarkPixmap) == NULL);
    got = XWizardPage_pixmap(page, XWizardPixmap_LogoPixmap);
    XQ_CHECK_PTR(got);
    XQ_CHECK(got && strcmp(XString_toUtf8(got), "wizard-logo.png") == 0);

    XClassDelete((XClass*)wiz);
}

/* ==================== ⑤ isComplete 虚函数分派 ==================== */

XQ_TEST(XWizardPage, isCompleteIsVirtual)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XqPageProbe* page = XqPageProbe_create(false);
    XQ_REQUIRE(wiz != NULL && page != NULL);

    /* 基类完备位是 true，但子类覆盖返回 false（Qt 的 virtual
     * isComplete 语义，qwizard.cpp:3598）。 */
    XQ_CHECK(page->m_base.m_complete);
    XQ_CHECK(!XWizardPage_isComplete((XWizardPage*)page));

    /* 向导的 validateCurrentPage 必须经虚槽看到覆盖值：完备性不足 ⇒
     * 校验直接判负（qwizard.cpp:3280-3295、1631-1638）。 */
    XQ_EQ_I64(XWizard_addPage(wiz, (XWizardPage*)page), 0);
    XQ_CHECK(!XWizard_validateCurrentPage(wiz));

    /* 子类覆盖为 true ⇒ 校验继续走 validatePage 默认实现（true）。 */
    page->m_overrideComplete = true;
    XQ_CHECK(XWizardPage_isComplete((XWizardPage*)page));
    XQ_CHECK(XWizard_validateCurrentPage(wiz));

    XClassDelete((XClass*)wiz);
}

/* ==================== ⑥ completeChanged 发射时机 ==================== */

XQ_TEST(XWizardPage, completeChangedSignal)
{
    XWizardPage* page = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(page != NULL);
    g_wpCompleteChanged = 0;
    XObject_connect((XObject*)page,
                      XSignal(XWizardPage_completeChanged_signal),
                      (XObject*)page, xwpCompleteSlot,
                      XConnectionType_Direct);

    /* 实变才发；同值不发（对标 completeChanged 语义）。 */
    XWizardPage_setComplete(page, false);
    XQ_EQ_I64(g_wpCompleteChanged, 1);
    XQ_CHECK(!XWizardPage_isComplete(page));
    XWizardPage_setComplete(page, false);
    XQ_EQ_I64(g_wpCompleteChanged, 1);
    XWizardPage_setComplete(page, true);
    XQ_EQ_I64(g_wpCompleteChanged, 2);
    XQ_CHECK(XWizardPage_isComplete(page));

    XClassDelete((XClass*)page);
}

/* ==================== ⑦ 字段委派与 removePage 解绑 ==================== */

XQ_TEST(XWizardPage, fieldDelegation)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XWizardPage* p1 = XWizardPage_create(NULL, 0);
    const XString* value;
    XQ_REQUIRE(wiz != NULL && p0 != NULL && p1 != NULL);
    XQ_EQ_I64(XWizard_addPage(wiz, p0), 0);
    XQ_EQ_I64(XWizard_addPage(wiz, p1), 1);

    /* Qt 的 setField 只对**已登记**字段生效（未登记打印
     * "No such field" 并忽略，qwizard.cpp）。故先 registerField。 */
    XQ_CHECK(XWizard_field_2(wiz, "user") == NULL);
    XWizardPage_setField(p0, "user", "xinyue"); /* 未登记 ⇒ 空操作 */
    XQ_CHECK(XWizard_field_2(wiz, "user") == NULL);
    XWizardPage_registerField(p0, "user", NULL, NULL, 0);
    XWizardPage_setField(p0, "user", "xinyue");
    value = XWizardPage_field(p0, "user");
    XQ_CHECK_PTR(value);
    XQ_CHECK(value && strcmp(XString_toUtf8(value), "xinyue") == 0);
    /* 同一向导内其它页读到同一字段表。 */
    value = XWizardPage_field(p1, "user");
    XQ_CHECK(value && strcmp(XString_toUtf8(value), "xinyue") == 0);
    XQ_CHECK(XWizardPage_field(p0, "缺失") == NULL);

    /* removePage 解绑：页不再属于向导，**本页登记的字段随页摘出字段表**
     * （对标 Qt removePage 把该页字段退回 pendingFields），字段读写为空
     * 操作 / NULL。 */
    XWizard_removePage(wiz, 0);
    XQ_CHECK(XWizardPage_wizard(p0) == NULL);
    XQ_CHECK(XWizardPage_field(p0, "user") == NULL);
    XWizardPage_setField(p0, "user", "ignored"); /* 无向导 ⇒ 空操作 */
    XQ_CHECK(XWizard_field_2(wiz, "user") == NULL);
    XQ_CHECK(XWizardPage_field(p1, "user") == NULL);

    XClassDelete((XClass*)p0); /* 已解绑，自行释放 */
    XClassDelete((XClass*)wiz);
}

/* ==================== ⑦b 必填字段与 cleanupPage 初值恢复 ==================== */

XQ_TEST(XWizardPage, mandatoryFieldAndCleanup)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XWizardPage* p1 = XWizardPage_create(NULL, 0);
    const XString* value;
    XQ_REQUIRE(wiz != NULL && p0 != NULL && p1 != NULL);
    XQ_EQ_I64(XWizard_addPage(wiz, p0), 0);
    XQ_EQ_I64(XWizard_addPage(wiz, p1), 1);

    /* 字段名**末尾** `*` 才是必填（qwizard.cpp:173-176）。 */
    XWizardPage_registerField(p0, "email*", NULL, NULL, 0);
    XQ_CHECK(XWizardPage_isComplete(p0) == false); /* 仍为初值 ⇒ 不完整 */
    XWizard_setField_2(wiz, "email", "a@b");
    XQ_CHECK(XWizardPage_isComplete(p0));
    value = XWizardPage_field(p0, "email");
    XQ_CHECK(value && strcmp(XString_toUtf8(value), "a@b") == 0);
    /* 名字里的星号被剥掉，字段名就是 "email"。 */
    XQ_CHECK(XWizard_field_2(wiz, "email*") == NULL);
    /* 非 p0 的页不受该必填字段约束。 */
    XQ_CHECK(XWizardPage_isComplete(p1));

    /* cleanupPage 默认实现把本页字段恢复为初值（qwizard.cpp:3550-3569）。 */
    XWizardPage_cleanupPage(p0);
    value = XWizardPage_field(p0, "email");
    /* XString_toUtf8 对空串返回 NULL（XString.c 空串早退），不能解引用；
     * 用长度断言「字段存在且为空」——QString 空串同理（QVariant 有效、
     * 文本长度为 0）。 */
    XQ_CHECK(value && XString_toUtf8_length(value) == 0);
    XQ_CHECK(XWizardPage_isComplete(p0) == false);

    XClassDelete((XClass*)wiz);
}

/* ==================== ⑧ nextId 顺序与稳定页 id ==================== */

XQ_TEST(XWizardPage, nextIdSequential)
{
    XWizard* wiz = XWizard_create(NULL, 0);
    XWizardPage* p0 = XWizardPage_create(NULL, 0);
    XWizardPage* p1 = XWizardPage_create(NULL, 0);
    XWizardPage* p2 = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(wiz != NULL && p0 != NULL && p1 != NULL && p2 != NULL);
    XQ_EQ_I64(XWizard_addPage(wiz, p0), 0);
    XQ_EQ_I64(XWizard_addPage(wiz, p1), 1);
    XQ_EQ_I64(XWizard_addPage(wiz, p2), 2);

    /* 默认 nextId = 页 id 序下一个（首三页 id 与插入序一致）。 */
    XQ_EQ_I64(XWizardPage_nextId(p0), 1);
    XQ_EQ_I64(XWizardPage_nextId(p1), 2);
    XQ_EQ_I64(XWizardPage_nextId(p2), -1);

    /* 删中间页 id=1：**页 id 稳定不左移**（qwizard.cpp pageMap 语义；
     * r2 对齐，账本 236）——p0 的下一个 id 变成 2，p2 仍是末页。 */
    XWizard_removePage(wiz, 1);
    XQ_EQ_I64(XWizardPage_nextId(p0), 2);
    XQ_EQ_I64(XWizardPage_nextId(p2), -1);
    XQ_CHECK(XWizardPage_isFinalPage(p2));
    XQ_CHECK(!XWizardPage_isFinalPage(p0));
    /* 再 addPage 分配**新** id 3（不回收 1）。 */
    {
        XWizardPage* p3 = XWizardPage_create(NULL, 0);
        XQ_REQUIRE(p3 != NULL);
        XQ_EQ_I64(XWizard_addPage(wiz, p3), 3);
        XQ_EQ_I64(XWizardPage_nextId(p2), 3);
        XQ_EQ_I64(XWizardPage_nextId(p3), -1);
    }

    XClassDelete((XClass*)p1); /* 已解绑，自行释放 */
    XClassDelete((XClass*)wiz);
}

/* ==================== ⑨ title/subTitle/commitPage 状态 ==================== */

XQ_TEST(XWizardPage, titleAndCommitPage)
{
    XWizardPage* page = XWizardPage_create(NULL, 0);
    XQ_REQUIRE(page != NULL);

    XWizardPage_setTitle(page, "第 1 步");
    XWizardPage_setSubTitle(page, "请填写资料");
    XQ_EQ_STR(XWizardPage_title(page), "第 1 步");
    XQ_EQ_STR(XWizardPage_subTitle(page), "请填写资料");
    XWizardPage_setTitle(page, "第 2 步");
    XQ_EQ_STR(XWizardPage_title(page), "第 2 步");
    XWizardPage_setSubTitle(page, "");
    XQ_EQ_STR(XWizardPage_subTitle(page), "");

    /* setCommitPage 只是状态位（Qt 同名语义）；按钮布局未接该标志，
     * 见交付 gaps。 */
    XQ_CHECK(!XWizardPage_isCommitPage(page));
    XWizardPage_setCommitPage(page, true);
    XQ_CHECK(XWizardPage_isCommitPage(page));
    XWizardPage_setCommitPage(page, false);
    XQ_CHECK(!XWizardPage_isCommitPage(page));

    XClassDelete((XClass*)page);
}

#endif /* XGUI_ON && XWIDGET_ON && XDIALOG_ON && XWIZARD_ON */
