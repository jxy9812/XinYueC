/**
 * @file       XSplitterHandleAlignTest.c
 * @brief      XSplitterHandle × Qt 6.8 QSplitterHandle 对齐契约测试。
 * @details    对齐依据：/opt/Qt/6.8.3/gcc_64/include/QtWidgets/qsplitter.h
 *             （QSplitterHandle 声明于 qsplitter.h:96-125）与 Qt 6.8.0 +
 *             Fusion + offscreen 实测：
 *             - 把手由分割器创建并归其所有（`splitter()` 反查，实测
 *               handle(0)->splitter()==&splitter）；
 *             - `orientation()` 随分割器方向、`opaqueResize()` 借分割器
 *               getter（实测 handle orient=1/opaque=1）；
 *             - `sizeHint()` = 有效把手宽 × 有效把手宽（实测 4x4 对应
 *               Fusion 的 PM_SplitterWidth=4）；
 *             - 拖动路径：press 记 mouseOffset → move 落位
 *               （`QWidget::grabMouse` 保证释放点仍在把手）；
 *             - `moveSplitter(p)` / `closestLegalPosition(p)` 与
 *               分割器侧同名语义一致（本适配以 m_index 转调，O(1)）。
 *
 *             **扩展项**：`splitterIndex()`（Qt 无公开对应，Qt 内部用
 *             indexOf(this)）；`handleClicked` 信号亦为 XGui 扩展
 *             （Qt 把手无双击/空闲点击信号）。只断言存在与取值自洽，
 *             不冒充 Qt 契约。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XFRAME_ON && XSPLITTER_ON

#include "XSplitterHandle.h"
#include "XSplitter.h"
#include "XWidget.h"
#include "XObject.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XVarList.h"
#include "XCursor.h" /* 方向光标断言（P0-11）。 */
#include "XClass.h"

static int s_handleMovedCount = 0;

/** @brief splitterMoved 槽（把手拖动经分割器发射）。 */
static void xq_handleMovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_handleMovedCount;
}

/** @brief 构造「200x100、两页、opaque 拖动」分割器并返回 0 号把手。 */
static XSplitter* xq_makeSplitter(XSplitterHandle** out, XWidget** p0,
                                  XWidget** p1)
{
    XSplitter* sp = XSplitter_create(NULL, 0);
    if (!sp) return NULL;
    *p0 = XWidget_create((XWidget*)sp, 0);
    *p1 = XWidget_create((XWidget*)sp, 0);
    if (!*p0 || !*p1) {
        XClassDelete((XClass*)sp);
        *p0 = *p1 = NULL;
        return NULL;
    }
    XSplitter_addWidget(sp, *p0);
    XSplitter_addWidget(sp, *p1);
    XWidget_resize((XWidget*)sp, 200, 100);
    /* 显式 NOT opaque（Qt 缺省由样式 SH_Splitter_OpaqueResize 决定，
     * 本项目同样回落样式；测试要固定为实时落位路径）。 */
    XSplitter_setOpaqueResize(sp, false);
    XSplitter_setOpaqueResize(sp, true);
    /* 主轴 200 − 缺省 handleWidth 4 = 196 ⇒ 95 + 101 恰好铺满。 */
    XSplitter_setSizes(sp, (const int[]){95, 101}, 2);
    *out = XSplitter_handle(sp, 0);
    return sp;
}

/** @brief 属性与归属（qsplitter.h:101-109）。 */
XQ_TEST(XSplitterHandle, properties)
{
    XSplitter* sp = NULL;
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XSplitterHandle* h = NULL;
    XSize hint;
    XQ_REQUIRE((sp = xq_makeSplitter(&h, &p0, &p1)) != NULL);
    XQ_REQUIRE(h != NULL);

    /* 把手归分割器所有（Qt：QSplitter::createHandle 里 new QSplitterHandle(o, this)）。 */
    XQ_CHECK(XSplitterHandle_splitter(h) == sp);
    XQ_CHECK(XWidget_parentWidget((XWidget*)h) == (XWidget*)sp);

    /* 方向与不透明拖动均取分割器口径（qsplitter.cpp:987-990）。 */
    XQ_EQ_I64(XSplitterHandle_orientation(h), XGuiOrientation_Horizontal);
    XQ_CHECK(XSplitterHandle_opaqueResize(h));
    XSplitter_setOrientation(sp, XGuiOrientation_Vertical);
    XQ_EQ_I64(XSplitterHandle_orientation(h), XGuiOrientation_Vertical);
    XSplitter_setOrientation(sp, XGuiOrientation_Horizontal);

    /* sizeHint = 有效把手宽 × 有效把手宽（qsplitter.cpp:191-199）。 */
    hint = XWidget_sizeHint_base((const XWidget*)h);
    XQ_EQ_I64(hint.width, XSplitter_handleWidth(sp));
    XQ_EQ_I64(hint.height, XSplitter_handleWidth(sp));

    /* 扩展项：分隔点号 O(1) 读回（Qt 用 indexOf(this)，本适配 m_index）。 */
    XQ_EQ_I64(XSplitterHandle_splitterIndex(h), 0);

    XClassDelete((XClass*)sp);
}

/**
 * @brief 把手方向光标 = SplitH/SplitV（**不是** SizeHor/SizeVer）。
 * @details 对齐依据：Qt QSplitterHandle 构造
 *          qsplitter.cpp:112：
 *            `setCursor(orientation == Qt::Horizontal ? Qt::SplitHCursor
 *                                                    : Qt::SplitVCursor);`
 *          判据是**分割器方向**：水平分割（页左右排、把手竖条）⇒ SplitH，
 *          垂直分割 ⇒ SplitV；随 setOrientation 同步刷新
 *          （XSplitterHandle_setOrientation）。
 *          【P0-11 2026-11-11】X 原用 SizeHor/SizeVer，与 Qt 的 Split 族
 *          不同；且底层平台映射（posix/win32 的 SplitV↔SplitH 两 case）
 *          原本与 Qt 相反，故本用例同时锁定「把手取 Split 族 + 判据是
 *          方向」这两条，平台字形反向由两处 case 成对交换修正
 *          （xpwn_cursorShapeToFontGlyph / xpwn_cursorHandleForShape）。
 */
XQ_TEST(XSplitterHandle, orientationCursor)
{
    XSplitter* sp = NULL;
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XSplitterHandle* h = NULL;
    XCursor cur;
    XQ_REQUIRE((sp = xq_makeSplitter(&h, &p0, &p1)) != NULL);
    XQ_REQUIRE(h != NULL);

    /* 水平分割 ⇒ SplitH（Qt 实测 Horizontal 把手 cursor().shape()
     * == Qt::SplitHCursor(16)）。 */
    cur = XWidget_cursor((XWidget*)h);
    XQ_EQ_I64(XCursor_shape(&cur), XCursor_SplitH);
    XClassDeinit((XClass*)&cur);

    /* 切垂直 ⇒ SplitV（setOrientation 遍历把手重设光标）。 */
    XSplitter_setOrientation(sp, XGuiOrientation_Vertical);
    cur = XWidget_cursor((XWidget*)h);
    XQ_EQ_I64(XCursor_shape(&cur), XCursor_SplitV);
    XClassDeinit((XClass*)&cur);

    /* 换回水平 ⇒ 再回到 SplitH（方向判据对称，非单次状态残留）。 */
    XSplitter_setOrientation(sp, XGuiOrientation_Horizontal);
    cur = XWidget_cursor((XWidget*)h);
    XQ_EQ_I64(XCursor_shape(&cur), XCursor_SplitH);
    XClassDeinit((XClass*)&cur);

    XClassDelete((XClass*)sp);
}

/** @brief 拖动落位 + splitterMoved 信号（qsplitter.cpp:255-301）。 */
XQ_TEST(XSplitterHandle, dragMovesSplitter)
{
    XSplitter* sp = NULL;
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XSplitterHandle* h = NULL;
    XMouseEvent press;
    XMouseEvent move;
    XMouseEvent release;
    XPoint pos;
    int sizes[2] = {0, 0};
    XQ_REQUIRE((sp = xq_makeSplitter(&h, &p0, &p1)) != NULL);
    XQ_REQUIRE(h != NULL);

    XSplitter_sizes(sp, sizes, 2);
    XQ_EQ_I64(sizes[0], 95);
    XQ_EQ_I64(sizes[1], 101);

    {
        XConnection* conn = XObject_connect(
            (XObject*)sp,
            (size_t)XSplitter_splitterMoved_signal(NULL, 0, 0),
            (XObject*)sp, xq_handleMovedSlot, XConnectionType_Direct);
        XQ_CHECK_PTR(conn);
    }
    s_handleMovedCount = 0;

    /* 按下：记录把手内拾取偏移（mouseOffset），进入拖动态。 */
    pos.x = 2;
    pos.y = 40;
    XMouseEvent_init(&press, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier,
                     pos);
    XCoreApplication_sendEvent((XObject*)h, (XEvent*)&press);
    XClassDeinit((XClass*)&press);

    /* 拖动 +40：把手原点 95 ⇒ 目标位 95 + 2 - 2 + 40 = 135？——
     * 口径是「把手原点 + 本地位 − 按压偏移」= 95 + 42 − 2 = 135。
     * 页 1 只有 101 px，故钳到合法区间末（页 1 的 smartMin=0 ⇒ 可让满）。
     * 断言：坡位后页 0 变大、页 1 变小、和不变、信号发出。 */
    pos.x = 42;
    XMouseEvent_init(&move, XEVENT_TYPE_MOUSE_MOVE,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier,
                     pos);
    XCoreApplication_sendEvent((XObject*)h, (XEvent*)&move);
    XClassDeinit((XClass*)&move);

    XSplitter_sizes(sp, sizes, 2);
    XQ_CHECK_MSG(sizes[0] > 95, "拖动后页 0 应变大（实际 %d）", sizes[0]);
    XQ_CHECK_MSG(sizes[1] < 101, "拖动后页 1 应变小（实际 %d）", sizes[1]);
    XQ_EQ_I64(sizes[0] + sizes[1] + XSplitter_handleWidth(sp), 200);
    XQ_CHECK(s_handleMovedCount >= 1);

    /* 释放：结束按压态（非 opaque 分支在此落位，opaque 只清状态）。 */
    pos.x = 42;
    XMouseEvent_init(&release, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, XKeyboardModifier_NoModifier,
                     pos);
    XCoreApplication_sendEvent((XObject*)h, (XEvent*)&release);
    XClassDeinit((XClass*)&release);

    XClassDelete((XClass*)sp);
}

/** @brief moveSplitter / closestLegalPosition 转调（qsplitter.h:119-120）。 */
XQ_TEST(XSplitterHandle, moveSplitterAndClosestLegal)
{
    XSplitter* sp = NULL;
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XSplitterHandle* h = NULL;
    int sizes[2] = {0, 0};
    int legal;
    int minPos = 0;
    int maxPos = 0;
    XQ_REQUIRE((sp = xq_makeSplitter(&h, &p0, &p1)) != NULL);
    XQ_REQUIRE(h != NULL);
    XQ_REQUIRE(XSplitter_getRange(sp, 0, &minPos, &maxPos));

    /* closestLegalPosition：输入在区间内时原样返回、越界时钳到端点
     * （对标 Qt private closestLegalPosition 的 getRange 夹取）。 */
    legal = XSplitterHandle_closestLegalPosition(h, 60);
    XQ_EQ_I64(legal, 60);
    legal = XSplitterHandle_closestLegalPosition(h, -50);
    XQ_EQ_I64(legal, minPos);
    legal = XSplitterHandle_closestLegalPosition(h, 100000);
    XQ_EQ_I64(legal, maxPos);

    /* moveSplitter 槽（Qt 签名带 index，实现恒用 m_index）：
     * 0 号分隔点落到 60 ⇒ 页 0 = 60、页 1 = 200 − handleWidth(4) − 60 = 136。 */
    XSplitterHandle_moveSplitter(h, 60, 0);
    XSplitter_sizes(sp, sizes, 2);
    XQ_EQ_I64(sizes[0], 60);
    XQ_EQ_I64(sizes[1], 136);

    XClassDelete((XClass*)sp);
}

#endif /* XGUI_ON && XWIDGET_ON && XFRAME_ON && XSPLITTER_ON */
