/**
 * @file       XGroupBoxAlignTest.c
 * @brief      XGroupBox × Qt 6.8 QGroupBox 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qgroupbox.h；
 *             - Qt 6.8.3 源码 qgroupbox.cpp：
 *               init（:172-186 align=AlignLeft、flat=false、checkable=false、
 *               内部 checked=true、sizePolicy Preferred/Preferred）、
 *               isChecked（:562-566 = checkable && checked）、
 *               setCheckable（:526-553 可勾选时 setChecked(true) 并取
 *               StrongFocus、updateGeometry）、
 *               setChecked（:606-620 同值/不可勾选早退，变化时同步子控件
 *               并 emit toggled(bool)）、
 *               childEvent（:362-391 新增子控件按 checkable/checked 同步
 *               启用位）、changeEvent（:647-664 仅在「被启用且未勾选」时
 *               禁子控件）、
 *               minimumSizeHint（:456-480 标题宽 + 空格，可勾选时再加
 *               PM_IndicatorWidth + PM_CheckBoxLabelSpacing、高取指示器高
 *               与字高较大者）、setFlat/setAlignment 均 updateGeometry+update；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 实测：默认 isChecked()
 *               false、setCheckable(true) 后 checked=true 且 toggled 计数 0、
 *               setCheckable(false) **不**发 toggled、再 setCheckable(true)
 *               也不发（底层 checked 仍为 true）；setChecked(false) 后
 *               setCheckable(true) 则真迁移并发 toggled(true)；
 *               setAlignment(Right|VCenter) 后 alignment()==130、
 *               setAlignment(0x00FF) 后 ==255（原样存储）；
 *               标题 \"ABC\" 时 minHint 52x37、可勾选后 72x37（+20 =
 *               指示器 14 + 间距 6）。
 * @note       勾选框绘制走样式 CC_GroupBox → PE_IndicatorCheckBox
 *             （XCommonStyle.c:xcs_drawGroupBox），与 Qt 的
 *             SC_GroupBoxCheckBox 同位；无样式时的两段斜线只是退化回退。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XGROUPBOX_ON

#include "XGroupBox.h"
#include "XWidget.h"
#include "XWidget_Protected.h"
#include "XAlignment.h"
#include "XObject.h"
#include "XVarList.h"
#include "XString.h"
#include "XClass.h"
#include "XImage.h" /* XWidget_grab 的像素比对（勾选框绘制断言）。 */
#include "XStyle.h" /* XWidget_style（缺省样式恒非 NULL）。 */

/** @brief toggled/clicked 计数。 */
static int g_xgbToggled;
static int g_xgbClicked;

static void xgb_toggledSlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, bool, on);
    (void)receiver;
    (void)on;
    ++g_xgbToggled;
}

static void xgb_clickedSlot(XObject* receiver, XVarList* args)
{
    XVarList_args_1(args, bool, on);
    (void)receiver;
    (void)on;
    ++g_xgbClicked;
}

/* ==================== ① 缺省值 / 标题 / 对齐 / 最小尺寸提示 ============ */

XQ_TEST(XGroupBox, defaultsTitleAndMinHint)
{
    XGroupBox* box = XGroupBox_create(NULL, 0);
    XSize hint;
    XSize minEmpty;
    XSize minTitle;

    XQ_REQUIRE(box != NULL);
    XWidget_setGeometry((XWidget*)box, 0, 0, 200, 120);

    /* 缺省值（qgroupbox.cpp:172-186）：无标题、左对齐、非 flat、
     * 不可勾选、isChecked()=false（checkable && checked）。 */
    XQ_EQ_STR(XGroupBox_title(box), "");
    XQ_EQ_I64(XGroupBox_alignment(box), XAlignment_Left);
    XQ_CHECK(!XGroupBox_isFlat(box));
    XQ_CHECK(!XGroupBox_isCheckable(box));
    XQ_CHECK(!XGroupBox_isChecked(box));
    /* QGroupBox 不 override sizeHint（继承 QWidget 空尺寸）。 */
    hint = XWidget_sizeHint((XWidget*)box);
    XQ_CHECK(hint.width < 0 && hint.height < 0);

    /* 标题往返 + 最小尺寸提示含标题宽 + 一个空格（qgroupbox.cpp:462-464）。 */
    minEmpty = XWidget_minimumSizeHint((XWidget*)box);
    XGroupBox_setTitle(box, "网络设置");
    XQ_EQ_STR(XGroupBox_title(box), "网络设置");
    minTitle = XWidget_minimumSizeHint((XWidget*)box);
    XQ_CHECK(minTitle.width > minEmpty.width);
    XQ_EQ_I64(minTitle.height, minEmpty.height);

    /* flat / alignment 往返（setFlat 走 updateGeometry+update）。 */
    XGroupBox_setFlat(box, true);
    XQ_CHECK(XGroupBox_isFlat(box));
    XGroupBox_setFlat(box, false);
    XQ_CHECK(!XGroupBox_isFlat(box));
    XGroupBox_setAlignment(box, XAlignment_HCenter);
    XQ_EQ_I64(XGroupBox_alignment(box), XAlignment_HCenter);
    XGroupBox_setAlignment(box, XAlignment_Right);
    XQ_EQ_I64(XGroupBox_alignment(box), XAlignment_Right);
    /* Qt 原样存参（qgroupbox.cpp:259-264 `d->align = alignment;`）：垂直位
     * 与冲突位都保留，alignment() 原样返回；本机 Qt 6.8 实测
     * setAlignment(Right|VCenter) ⇒ 130、setAlignment(0x00FF) ⇒ 255。 */
    XGroupBox_setAlignment(box, XAlignment_Right | XAlignment_VCenter);
    XQ_EQ_I64(XGroupBox_alignment(box), XAlignment_Right | XAlignment_VCenter);
    XGroupBox_setAlignment(box, 0x00FF);
    XQ_EQ_I64(XGroupBox_alignment(box), 0x00FF);
    XGroupBox_setAlignment(box, XAlignment_Left);

    XClassDelete((XClass*)box);
}

/* ==================== ② 可勾选：勾选态 ↔ 子控件启用 + 指示器入最小尺寸 == */

XQ_TEST(XGroupBox, checkableChildrenAndIndicator)
{
    XGroupBox* box = XGroupBox_create(NULL, 0);
    XWidget* child = XWidget_create(NULL, 0);
    XWidget* child2;
    XSize minPlain;
    XSize minCheckable;
    XWidgetFocusPolicy policy;

    XQ_REQUIRE(box != NULL);
    XQ_REQUIRE(child != NULL);
    XWidget_setGeometry((XWidget*)box, 0, 0, 200, 120);
    XGroupBox_setTitle(box, "设备组");
    minPlain = XWidget_minimumSizeHint((XWidget*)box);

    g_xgbToggled = 0;
    g_xgbClicked = 0;
    XObject_connect_1((XObject*)box,
                      (size_t)XGroupBox_toggled_signal(NULL, false),
                      (XObject*)box, xgb_toggledSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)box,
                      (size_t)XGroupBox_clicked_signal(NULL, false),
                      (XObject*)box, xgb_clickedSlot,
                      XConnectionType_Direct);

    /* 开启可勾选：立即勾选（qgroupbox.cpp:533-535 `setChecked(true)`）、
     * 取 StrongFocus、最小尺寸加指示器宽与间距。
     * 信号口径（gap #179）：Qt 内部 checked 初值即 true（:178），
     * setChecked(true) 同值早退 ⇒ **不发** toggled；本机 Qt 6.8 实测
     * toggled 计数 0。 */
    XGroupBox_setCheckable(box, true);
    XQ_CHECK(XGroupBox_isCheckable(box));
    XQ_CHECK(XGroupBox_isChecked(box));
    XQ_EQ_I64(g_xgbToggled, 0);
    policy = XWidget_focusPolicy((XWidget*)box);
    XQ_CHECK((policy & XWidgetFocusPolicy_StrongFocus) != 0);
    minCheckable = XWidget_minimumSizeHint((XWidget*)box);
    XQ_EQ_I64(minCheckable.width - minPlain.width, 16); /* 12 框 + 4 间距 */
    XQ_CHECK(minCheckable.height >= minPlain.height);

    /* 新子控件在「已勾选」态加入 ⇒ 立即可用（qgroupbox.cpp:362-391）。 */
    XWidget_setParent(child, (XWidget*)box, 0);
    XQ_CHECK(XWidget_isEnabled(child));

    /* 取消勾选：子控件禁用 + 发 toggled(false)；重复设置同值不再发。 */
    XGroupBox_setChecked(box, false);
    XQ_CHECK(!XGroupBox_isChecked(box));
    XQ_CHECK(!XWidget_isEnabled(child));
    XQ_EQ_I64(g_xgbToggled, 1);
    XGroupBox_setChecked(box, false);
    XQ_EQ_I64(g_xgbToggled, 1);
    /* 程序化 setChecked 不发 clicked（Qt 文档：clicked 只由交互触发）。 */
    XQ_EQ_I64(g_xgbClicked, 0);

    /* 重新勾选：恢复子控件可用（并发 toggled(true)）。 */
    XGroupBox_setChecked(box, true);
    XQ_CHECK(XWidget_isEnabled(child));
    XQ_EQ_I64(g_xgbToggled, 2);

    /* setCheckable(false)：只恢复子控件可用、**不发** toggled，且底层
     * checked 位保留（gap #180；qgroupbox.cpp:541-548）。 */
    XGroupBox_setCheckable(box, false);
    XQ_CHECK(!XGroupBox_isCheckable(box));
    XQ_CHECK(!XGroupBox_isChecked(box)); /* checkable && checked */
    XQ_EQ_I64(g_xgbToggled, 2);
    XQ_CHECK(XWidget_isEnabled(child));
    /* 再开可勾选：底层 checked 仍为 true ⇒ setChecked(true) 同值早退，
     * 仍不发 toggled（本机 Qt 6.8 实测同）。 */
    XGroupBox_setCheckable(box, true);
    XQ_CHECK(XGroupBox_isChecked(box));
    XQ_EQ_I64(g_xgbToggled, 2);
    /* 用户先取消勾选、再重复 setCheckable(true)：真迁移并发 toggled(true)
     * （Qt 的 setCheckable(true) 无条件走 setChecked(true)）。 */
    XGroupBox_setChecked(box, false);
    XQ_EQ_I64(g_xgbToggled, 3);
    XGroupBox_setCheckable(box, true); /* 已可勾选 + 未勾选 */
    XQ_CHECK(XGroupBox_isChecked(box));
    XQ_EQ_I64(g_xgbToggled, 4);

    /* 已勾选态下新加入的子控件即便此前被禁用也应被恢复
     * （childEvent 的 `if (d->checked) w->setEnabled(true)` 分支）。 */
    child2 = XWidget_create(NULL, 0);
    XQ_REQUIRE(child2 != NULL);
    XWidget_setEnabled(child2, false);
    XWidget_setParent(child2, (XWidget*)box, 0);
    XQ_CHECK(XWidget_isEnabled(child2));

    XClassDelete((XClass*)box); /* child/child2 随容器级联释放。 */
}

/* ==================== ③ 勾选框走样式原语（gap #182） ==================== */

XQ_TEST(XGroupBox, indicatorDrawnThroughStyle)
{
    XGroupBox* box = XGroupBox_create(NULL, 0);
    XImage* checkedShot;
    XImage* uncheckedShot;
    int diff = 0;
    int x;
    int y;

    XQ_REQUIRE(box != NULL);
    XWidget_setGeometry((XWidget*)box, 0, 0, 200, 120);
    XGroupBox_setTitle(box, "组");
    /* 缺省样式恒非 NULL（XWidget_style 回落 XStyle_defaultStyle）⇒
     * XGroupBox_drawControl 走 CC_GroupBox 分支，样式内用
     * PE_IndicatorCheckBox 绘制勾选框——与 Qt 的 SC_GroupBoxCheckBox
     * 同位（无样式时的两段斜线只是死回退）。 */
    XQ_CHECK_PTR((void*)XWidget_style((const XWidget*)box));

    XGroupBox_setCheckable(box, true); /* 勾选态 */
    checkedShot = XWidget_grab((XWidget*)box);
    XQ_REQUIRE(checkedShot != NULL);
    XGroupBox_setChecked(box, false); /* 未勾选态 */
    uncheckedShot = XWidget_grab((XWidget*)box);
    XQ_REQUIRE(uncheckedShot != NULL);

    /* 勾选态与未勾选态两幅图必须有像素差：勾选标记由绘制路径产生。
     * 全图扫描（不写死勾选框坐标，标题行高/样式度量变化后仍成立）。 */
    for (y = 0; y < 120; ++y) {
        for (x = 0; x < 200; ++x) {
            if (XImage_pixel(checkedShot, x, y) !=
                XImage_pixel(uncheckedShot, x, y))
                ++diff;
        }
    }
    XQ_CHECK_MSG(diff > 0, "勾选标记必须由绘制路径产生（差异像素 %d）", diff);

    XClassDelete((XClass*)checkedShot);
    XClassDelete((XClass*)uncheckedShot);
    XClassDelete((XClass*)box);
}

#endif /* XGUI_ON && XWIDGET_ON && XGROUPBOX_ON */
