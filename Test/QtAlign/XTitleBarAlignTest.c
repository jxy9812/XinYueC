/**
 * @file       XTitleBarAlignTest.c
 * @brief      XTitleBar 内部一致性 / 所有权契约测试。
 * @details    本机 Qt 6.8 **无对应类**（标题栏在 Qt 侧由 WM 或
 *             QCommonStyle 的 CC_TitleBar 绘制，不存在 QTitleBar 控件），
 *             故本文件只做本模块的内部一致性对齐：
 *             - 类型守卫 XTitleBar_isBar 的虚表指纹判定（本类体系 true、
 *               普通 XWidget false、NULL false）；
 *             - 活动子控件位注入的往返与幂等（装饰模块驱动口径）；
 *             - 未挂载宿主时 buildOption / hitTest 必须安全失败（返回
 *               false / 0），不得解引用空宿主；
 *             - 挂载到宿主后父子与 titleBarWidget 槽位一致
 *               （XWidget_setTitleBarWidget 借用挂载契约）；
 *             - 冗余 API 清理后类内不再持有定时器（原
 *               XTitleBar_armReleaseVerify 及其 TimerEvent 重载已删，
 *               见 XTitleBar.h 的清理注）。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON

#include "XTitleBar.h"
#include "XTitleBar_Protected.h"
#include "XStyleOption.h"
#include "XWidget.h"
#include "XObject.h"
#include "XClass.h"

/* ==================== ① 类型守卫 ==================== */

XQ_TEST(XTitleBar, isBarTypeGuard)
{
    XTitleBar* bar = XTitleBar_create(NULL, 0);
    XWidget* plain;
    XWidget* other;
    XQ_REQUIRE(bar != NULL);
    plain = XWidget_create(NULL, 0);
    XQ_REQUIRE(plain != NULL);
    other = XWidget_create(NULL, 0);
    XQ_REQUIRE(other != NULL);

    /* 本类实例与派生类经虚表槽指纹识别为 true。 */
    XQ_CHECK(XTitleBar_isBar((const XWidget*)bar));
    /* 普通控件不是标题栏（指纹不匹配 → 装饰路径降级为纯子控件）。 */
    XQ_CHECK(!XTitleBar_isBar(plain));
    XQ_CHECK(!XTitleBar_isBar(NULL));
    /* 临时控件同样为 false；必须与本用例其余控件一起释放（此前内联
     * 创建未回收 ⇒ 每遍 +1128 字节的假泄漏）。 */
    XQ_CHECK(!XTitleBar_isBar((const XWidget*)other));

    XClassDelete((XClass*)other);
    XClassDelete((XClass*)plain);
    XClassDelete((XClass*)bar);
}

/* ==================== ② 活动子控件位往返 / 幂等 / NULL 安全 ============ */

XQ_TEST(XTitleBar, activeSubControlsRoundTrip)
{
    XTitleBar* bar = XTitleBar_create(NULL, 0);
    XQ_REQUIRE(bar != NULL);

    /* 初值 0（未按住/悬停任何按钮）。 */
    XQ_EQ_I64(XTitleBar_activeSubControls(bar), 0);

    XTitleBar_setActiveSubControls(bar, (int)XStyleSC_TitleBarCloseButton);
    XQ_EQ_I64(XTitleBar_activeSubControls(bar),
              (int)XStyleSC_TitleBarCloseButton);

    /* 位组合（按住优先合并口径）原样存取。 */
    XTitleBar_setActiveSubControls(
        bar, (int)XStyleSC_TitleBarCloseButton | (int)XStyleSC_TitleBarMinButton);
    XQ_EQ_I64(XTitleBar_activeSubControls(bar),
              (int)(XStyleSC_TitleBarCloseButton | XStyleSC_TitleBarMinButton));

    /* 清回 0。 */
    XTitleBar_setActiveSubControls(bar, 0);
    XQ_EQ_I64(XTitleBar_activeSubControls(bar), 0);

    /* NULL 安全：注入无操作、读回 0。 */
    XTitleBar_setActiveSubControls(NULL, (int)XStyleSC_TitleBarMinButton);
    XQ_EQ_I64(XTitleBar_activeSubControls(NULL), 0);

    XClassDelete((XClass*)bar);
}

/* ==================== ③ 无宿主时 buildOption/hitTest 安全失败 =========== */

XQ_TEST(XTitleBar, buildOptionAndHitTestWithoutHost)
{
    XTitleBar* bar = XTitleBar_create(NULL, 0);
    XStyleOptionTitleBar opt;
    XPoint pt;
    XQ_REQUIRE(bar != NULL);

    /* 宿主缺席（未挂载）：选项组装失败，调用方不得使用 opt。 */
    XQ_CHECK(!XTitleBar_buildOption(bar, &opt, NULL));
    XQ_CHECK(!XTitleBar_buildOption(NULL, &opt, NULL));
    XQ_CHECK(!XTitleBar_buildOption(bar, NULL, NULL));

    /* 命中测试走同一组装：失败即返回 0（无按钮位）。 */
    XPoint_init(&pt, 4, 4);
    XQ_EQ_I64(XTitleBar_hitTest_base(bar, &pt), 0);
    XQ_EQ_I64(XTitleBar_hitTest_base(NULL, &pt), 0);
    XQ_EQ_I64(XTitleBar_hitTest_base(bar, NULL), 0);

    /* 样式度量条高恒非负（样式缺席时为 0，存在时 ≥ 8——本类钳位口径）。 */
    XQ_CHECK(XTitleBar_defaultHeight() >= 0);

    XClassDelete((XClass*)bar);
}

/* ==================== ④ 挂载到宿主：父子与 titleBarWidget 槽位一致 ===== */

XQ_TEST(XTitleBar, mountToHostKeepsSingleOwner)
{
    XWidget* host = XWidget_create(NULL, (XWidgetFlags)XWindowType_Window);
    XTitleBar* bar;
    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 320, 240);
    bar = XTitleBar_create(host, 0);
    XQ_REQUIRE(bar != NULL);

    /* 控件级通用槽：挂载后父链与查询一一对应（借用，不转移所有权）。 */
    XWidget_setTitleBarWidget(host, (XWidget*)bar);
    XQ_CHECK(XWidget_titleBarWidget(host) == (XWidget*)bar);
    XQ_CHECK(XWidget_parentWidget((XWidget*)bar) == host);
    XQ_CHECK(XTitleBar_isBar(XWidget_titleBarWidget(host)));
    /* 挂载后仍可注入活动子控件（装饰模块按条控件类型注入）。 */
    XTitleBar_setActiveSubControls(bar, (int)XStyleSC_TitleBarMinButton);
    XQ_EQ_I64(XTitleBar_activeSubControls(bar),
              (int)XStyleSC_TitleBarMinButton);

    /* 摘除：槽位清空、条控件不被删除（借用契约），旧条摘父链归还创建方
     * （XWidget_setTitleBarWidget 的 `setParent(old, NULL, 0)`）。 */
    XWidget_setTitleBarWidget(host, NULL);
    XQ_CHECK(XWidget_titleBarWidget(host) == NULL);
    XQ_CHECK(XTitleBar_isBar((const XWidget*)bar));
    XQ_CHECK(XWidget_parentWidget((XWidget*)bar) == NULL);

    /* 借用契约：条控件由创建方释放（此处即本用例）。 */
    XClassDelete((XClass*)bar);
    XClassDelete((XClass*)host);
}

#endif /* XGUI_ON && XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON */
