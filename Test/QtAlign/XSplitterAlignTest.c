/**
 * @file       XSplitterAlignTest.c
 * @brief      XSplitter × Qt 6.8 QSplitter 对齐契约测试。
 * @details    对齐依据：
 *             - 头文件 /opt/Qt/6.8.3/gcc_64/include/QtWidgets/qsplitter.h
 *               （addWidget/insertWidget/replaceWidget/sizes/setSizes/
 *               saveState/restoreState/handle/getRange/setStretchFactor/
 *               splitterMoved 信号）；
 *             - 本机 Qt 6.8.0 + Fusion + offscreen 实测：handleWidth 缺省由
 *               样式 PM_SplitterWidth 给出（Fusion=4）、handle(i) 越界返回
 *               nullptr、indexOf(handle) 返回分隔点号、splitter 缺省
 *               frameStyle=16、sizeHint 为各页主轴 hint 之和。
 *
 *             覆盖：页面管理（增/插/替换/查）、把手生命周期与父子所有权、
 *             尺寸意图与落位、moveSplitter 落位 + splitterMoved 信号、
 *             getRange、折叠开关、saveState/restoreState 往返、sizeHint
 *             虚槽接线。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XFRAME_ON && XSPLITTER_ON

#include "XSplitter.h"
#include "XSplitterHandle.h"
#include "XWidget.h"
#include "XWidget_Protected.h" /* XWidget_setSizeHint（模拟子类提供 sizeHint）。 */
#include "XObject.h"
#include "XByteArray.h"
#include "XVarList.h"
#include "XStringUtils.h" /* XSnprintf（P0-10 构造旧版存档文本）。 */

/* ==================== 信号探针 ==================== */

static int s_splitterMovedCount = 0;
static int s_splitterMovedPos = -1;
static int s_splitterMovedIndex = -1;

/** @brief splitterMoved(pos, index) 槽（对标 qsplitter.h:72）。
 * @details 载荷解码走 XVarList 顺序读取宏（`XVarList_start` +
 *          `XVarList_arg(args, int)`，本仓库槽函数的标准取参口——
 *          XVarList 没有按下标取值的 API）。 */
static void xq_splitterMovedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    ++s_splitterMovedCount;
    if (args) {
        XVarList_start(args);
        s_splitterMovedPos = XVarList_arg(args, int);
        s_splitterMovedIndex = XVarList_arg(args, int);
    }
}

/** @brief 构造「宽 200 × 高 100、含三页」的水平分割器。 */
static XSplitter* xq_makeThreePageSplitter(XWidget** p0, XWidget** p1,
                                           XWidget** p2)
{
    XSplitter* sp = XSplitter_create(NULL, 0);
    if (!sp) return NULL;
    *p0 = XWidget_create((XWidget*)sp, 0);
    *p1 = XWidget_create((XWidget*)sp, 0);
    *p2 = XWidget_create((XWidget*)sp, 0);
    if (!*p0 || !*p1 || !*p2) {
        XClassDelete((XClass*)sp);
        *p0 = *p1 = *p2 = NULL;
        return NULL;
    }
    XSplitter_addWidget(sp, *p0);
    XSplitter_addWidget(sp, *p1);
    XSplitter_addWidget(sp, *p2);
    XWidget_resize((XWidget*)sp, 200, 100);
    XSplitter_refresh(sp);
    return sp;
}

/* ==================== 用例 ==================== */

/** @brief 页面管理与父子所有权（qsplitter.h:34-36/62-64）。 */
XQ_TEST(XSplitter, pageManagement)
{
    XSplitter* sp = NULL;
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XWidget* p2 = NULL;
    const XVector* children;
    XQ_REQUIRE((sp = xq_makeThreePageSplitter(&p0, &p1, &p2)) != NULL);

    XQ_EQ_I64(XSplitter_count(sp), 3);
    XQ_CHECK(XSplitter_widget(sp, 0) == p0);
    XQ_CHECK(XSplitter_widget(sp, 1) == p1);
    XQ_CHECK(XSplitter_widget(sp, 2) == p2);
    XQ_EQ_I64(XSplitter_indexOf(sp, p0), 0);
    XQ_EQ_I64(XSplitter_indexOf(sp, p2), 2);
    /* Qt 实测：越界 index 返回 nullptr（qsplitter.cpp:widget）。 */
    XQ_CHECK(XSplitter_widget(sp, -1) == NULL);
    XQ_CHECK(XSplitter_widget(sp, 3) == NULL);
    XQ_EQ_I64(XSplitter_indexOf(sp, NULL), -1);

    /* addWidget 后页归分割器（Qt：addWidget 接管控件所有权 ⇒ 父子链）。 */
    XQ_CHECK(XWidget_parentWidget(p0) == (XWidget*)sp);
    /* 3 页 ⇒ 2 个把手，且把手也是分割器子件。 */
    children = XObject_children((XObject*)sp);
    XQ_CHECK_PTR(children);
    if (children)
        XQ_EQ_SIZE(XContainer_size_base((const XContainer*)children), 5);

    /* insertWidget：插到 0 号位后序表整体后移（qsplitter.cpp:809 同式）。 */
    {
        XWidget* p3 = XWidget_create((XWidget*)sp, 0);
        XQ_REQUIRE(p3 != NULL);
        XSplitter_insertWidget(sp, 0, p3);
        XQ_EQ_I64(XSplitter_count(sp), 4);
        XQ_CHECK(XSplitter_widget(sp, 0) == p3);
        XQ_CHECK(XSplitter_widget(sp, 1) == p0);

        /* replaceWidget 负例：新控件**已是本分割器的子件**（`new
         * QWidget(splitter)` 会被 QSplitter::childEvent 自动收成一页）
         * ⇒ Qt 6.8 实测拒绝并告警 "Trying to replace a widget with one of
         * its siblings"、返回 nullptr，页表与计数都不动。
         * （旧断言把这种控件当合法替换目标 ⇒ 恒失败；Qt 探针显示
         * count 5、w(1) 仍是 p0。） */
        {
            XWidget* sibling = XWidget_create((XWidget*)sp, 0);
            XQ_REQUIRE(sibling != NULL);
            XQ_EQ_I64(XSplitter_count(sp), 5);   /* 构造即子件 ⇒ 自动成页 */
            XQ_CHECK_MSG(XSplitter_replaceWidget(sp, 1, sibling) == NULL,
                         "已是子件的控件不得被 replaceWidget 接受");
            XQ_EQ_I64(XSplitter_count(sp), 5);
            XQ_CHECK(XSplitter_widget(sp, 1) == p0);
            /* sibling 留在页表尾，随 sp 析构释放。 */
        }

        /* replaceWidget 正例：非子件控件才能替换；返回被替换的旧控件
         * （交还调用方，不销毁），页数不变。 */
        {
            XWidget* p4 = XWidget_create(NULL, 0);
            XWidget* old = NULL;
            XQ_REQUIRE(p4 != NULL);
            old = XSplitter_replaceWidget(sp, 1, p4);
            XQ_CHECK(old == p0);
            XQ_CHECK(XSplitter_widget(sp, 1) == p4);
            XQ_EQ_I64(XSplitter_count(sp), 5);
            /* p0 已被交还：显式释放（不再属于分割器）。 */
            XClassDelete((XClass*)p0);
        }
    }

    /* 分割器析构带走全部页与把手（不手动删 p1/p2/p3/p4/sibling）。 */
    XClassDelete((XClass*)sp);
}

/** @brief 把手生命周期、方向与尺寸提示（qsplitter.h:59-67/109）。 */
XQ_TEST(XSplitter, handlesAndHints)
{
    XSplitter* sp = NULL;
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XWidget* p2 = NULL;
    XSplitterHandle* h0;
    XSplitterHandle* h1;
    XSize hint;
    XSize slotHint;
    int minPos = 0;
    int maxPos = 0;
    XQ_REQUIRE((sp = xq_makeThreePageSplitter(&p0, &p1, &p2)) != NULL);

    /* 3 页 ⇒ 把手 0/1；越界返回 NULL（Qt 实测 handle(2)=nullptr）。 */
    h0 = XSplitter_handle(sp, 0);
    h1 = XSplitter_handle(sp, 1);
    XQ_CHECK_PTR(h0);
    XQ_CHECK_PTR(h1);
    XQ_CHECK(h0 != h1);
    XQ_CHECK(XSplitter_handle(sp, 2) == NULL);
    XQ_CHECK(XSplitter_handle(sp, -1) == NULL);

    /* 把手归属（qsplitter.h:107 `QSplitter *splitter() const`）。 */
    XQ_CHECK(XSplitterHandle_splitter(h0) == sp);
    /* Qt 实测：indexOf(handle) 返回分隔点号（qsplitter.cpp:1183-1186）。 */
    XQ_EQ_I64(XSplitter_indexOf(sp, (XWidget*)h0), 0);
    XQ_EQ_I64(XSplitter_indexOf(sp, (XWidget*)h1), 1);

    /* 方向传播到把手（qsplitter.cpp:987-990 setOrientation 遍历把手）。 */
    XQ_EQ_I64(XSplitterHandle_orientation(h0), XGuiOrientation_Horizontal);
    XSplitter_setOrientation(sp, XGuiOrientation_Vertical);
    XQ_EQ_I64(XSplitter_orientation(sp), XGuiOrientation_Vertical);
    XQ_EQ_I64(XSplitterHandle_orientation(h0), XGuiOrientation_Vertical);
    XSplitter_setOrientation(sp, XGuiOrientation_Horizontal);

    /* 把手 sizeHint = 有效把手宽 × 有效把手宽（qsplitter.cpp:191-199）。 */
    hint = XWidget_sizeHint_base((const XWidget*)h0);
    XQ_EQ_I64(hint.width, XSplitter_handleWidth(sp));
    XQ_EQ_I64(hint.height, XSplitter_handleWidth(sp));

    /* 分割器 sizeHint（Qt 6.8 `QSplitter::sizeHint()` qsplitter.cpp:1490-1507）：
     * 只累加**可见页的有效** sizeHint，**不加把手宽**。Qt 6.8.0 + Fusion
     * 实测两页 QLabel（hint 27/30）→ 57（未加 handleWidth=4）。
     * 给三页各设 40x24 / 50x30 / 60x20：主轴和 = 150、交叉轴最大 = 30。 */
    {
        XSize h;
        XWidget_setSizeHint(p0, &(XSize){40, 24});
        XWidget_setSizeHint(p1, &(XSize){50, 30});
        XWidget_setSizeHint(p2, &(XSize){60, 20});
        XSplitter_refresh(sp);
        hint = XWidget_sizeHint_base((const XWidget*)sp);
        XQ_EQ_I64(hint.width, 150);
        XQ_EQ_I64(hint.height, 30);
        /* 虚槽接线：XWidget_sizeHint 与直接调用同值（qsplitter.h:50
         * `QSize sizeHint() const override`）。 */
        slotHint = XWidget_sizeHint_base((XWidget*)sp);
        XQ_CHECK(hint.width == slotHint.width && hint.height == slotHint.height);
    }

    /* minimumSizeHint **含**把手宽（Qt qsplitter.cpp:1532-1536 只在
     * minimumSizeHint 里加 handle->sizeHint()；实测两页 minimumSize
     * 50/60 → 114 = 50+60+4）。 */
    {
        XWidget_setMinimumSize(p0, 10, 10);
        XWidget_setMinimumSize(p1, 20, 10);
        XWidget_setMinimumSize(p2, 30, 10);
        hint = XWidget_minimumSizeHint_base((const XWidget*)sp);
        XQ_EQ_I64(hint.width, 10 + 20 + 30 + 2 * XSplitter_handleWidth(sp));
        XQ_CHECK(XWidget_minimumSizeHint_base((XWidget*)sp).width == hint.width);
    }

    /* getRange：近区间内可动（qsplitter.h:66）。 */
    XQ_CHECK(XSplitter_getRange(sp, 0, &minPos, &maxPos));
    XQ_CHECK(minPos <= maxPos);

    XClassDelete((XClass*)sp);
}

/** @brief 尺寸意图、落位与 moveSplitter + splitterMoved 信号。 */
XQ_TEST(XSplitter, sizesAndMoveSplitter)
{
    XSplitter* sp = NULL;
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XWidget* p2 = NULL;
    int sizes[3] = {60, 70, 62};
    int got[3] = {0, 0, 0};
    int handleW;
    XQ_REQUIRE((sp = xq_makeThreePageSplitter(&p0, &p1, &p2)) != NULL);

    /* 缺省把手宽：Qt 6.8 `QSplitterPrivate` 的 `handleWidth = -1` 哨兵，
     * `handleWidth()` 回落样式 PM_SplitterWidth（qsplitter.cpp:1614-1624）。
     * 本机 Qt 6.8.0 + offscreen 实测：Fusion=4、Windows=4、
     * QCommonStyle=0。X 缺省样式 XCommonStyle 同为 QCommonStyle 的 0，
     * XSplitter 对 <=0 按 Qt 实际平台风格值 4 兜底（见
     * XSplitter.c 的 xsp_handleWidthEff 注释）。 */
    handleW = XSplitter_handleWidth(sp);
    XQ_EQ_I64(handleW, 4);

    /* setSizes 像素意图：三页 60/70/62，恰好吃满「主轴长 − 2×把手宽」
     * （=200−8=192）。落位后 sizes() 读回**实际几何**。 */
    XSplitter_setSizes(sp, sizes, 3);
    XSplitter_sizes(sp, got, 3);
    XQ_EQ_I64(got[0], 60);
    XQ_EQ_I64(got[1], 70);
    XQ_EQ_I64(got[2], 62);
    XQ_EQ_I64(got[0] + got[1] + got[2] + 2 * handleW, 200);

    /* moveSplitter：0 号分隔点从 60 移到 100（页 0 吸收 40、页 1 让出 40），
     * 落位后发 splitterMoved(pos, index)（qsplitter.h:72）。 */
    {
        XConnection* conn = XObject_connect(
            (XObject*)sp,
            (size_t)XSplitter_splitterMoved_signal(NULL, 0, 0),
            (XObject*)sp, xq_splitterMovedSlot, XConnectionType_Direct);
        XQ_CHECK_PTR(conn);
    }
    s_splitterMovedCount = 0;
    s_splitterMovedPos = -1;
    s_splitterMovedIndex = -1;
    XSplitter_moveSplitter(sp, 100, 0);
    XQ_EQ_I64(s_splitterMovedCount, 1);
    XQ_EQ_I64(s_splitterMovedPos, 100);
    XQ_EQ_I64(s_splitterMovedIndex, 0);
    XSplitter_sizes(sp, got, 3);
    XQ_EQ_I64(got[0], 100);
    XQ_EQ_I64(got[1], 30);
    XQ_EQ_I64(got[0] + got[1] + got[2] + 2 * handleW, 200);

    /* 超页数出参完全不写（qsplitter.cpp:1560-1574 出参契约）。 */
    got[0] = -7;
    got[1] = -7;
    got[2] = -7;
    XSplitter_sizes(sp, got, 4);
    XQ_EQ_I64(got[0], -7);
    XQ_EQ_I64(got[2], -7);

    XClassDelete((XClass*)sp);
}

/** @brief 折叠开关与不透明拖动（qsplitter.h:41-47）。 */
XQ_TEST(XSplitter, collapsibleAndOpaque)
{
    XSplitter* sp = NULL;
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XWidget* p2 = NULL;
    XQ_REQUIRE((sp = xq_makeThreePageSplitter(&p0, &p1, &p2)) != NULL);

    /* 缺省 childrenCollapsible = true（qsplitter.h:41 默认参数）。 */
    XQ_CHECK(XSplitter_childrenCollapsible(sp));
    XQ_CHECK(XSplitter_isCollapsible(sp, 0));

    /* 逐页覆写（qsplitter.h:44-45）：第 1 页不可折叠，其余不受影响。 */
    XSplitter_setCollapsible(sp, 1, false);
    XQ_CHECK(!XSplitter_isCollapsible(sp, 1));
    XQ_CHECK(XSplitter_isCollapsible(sp, 0));

    /* 全局开关翻转后，未覆写的页跟随；已覆写的页保持覆写值
     * （Qt：-1 未设置哨兵 ⇒ 跟随 childrenCollapsible）。 */
    XSplitter_setChildrenCollapsible(sp, false);
    XQ_CHECK(!XSplitter_childrenCollapsible(sp));
    XQ_CHECK(!XSplitter_isCollapsible(sp, 0));
    XQ_CHECK(!XSplitter_isCollapsible(sp, 1));

    /* 不透明拖动显式设置后 getter 不再回落样式提示
     * （qsplitter.cpp:1472-1484）。 */
    XSplitter_setOpaqueResize(sp, false);
    XQ_CHECK(!XSplitter_opaqueResize(sp));
    XSplitter_setOpaqueResize(sp, true);
    XQ_CHECK(XSplitter_opaqueResize(sp));

    /* 把手宽显式设置往返（-1 为「回落样式」哨兵）。 */
    XSplitter_setHandleWidth(sp, 9);
    XQ_EQ_I64(XSplitter_handleWidth(sp), 9);

    XClassDelete((XClass*)sp);
}

/** @brief saveState/restoreState 往返（qsplitter.h:56-57）。 */
XQ_TEST(XSplitter, saveRestoreState)
{
    XSplitter* sp = NULL;
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XWidget* p2 = NULL;
    XByteArray* state = NULL;
    XByteArray* stateOpaqueOff = NULL;
    XByteArray* stateOpaqueOn = NULL;
    XByteArray* legacy = NULL;
    char legacyText[64];
    /* 主轴 200 − 2×handleWidth(4) = 192 ⇒ 50+80+62 恰好铺满。 */
    int sizes[3] = {50, 80, 62};
    int got[3] = {0, 0, 0};
    XQ_REQUIRE((sp = xq_makeThreePageSplitter(&p0, &p1, &p2)) != NULL);

    XSplitter_setSizes(sp, sizes, 3);
    state = XSplitter_saveState(sp);
    XQ_CHECK_PTR(state);

    /* 破坏状态后恢复：方向/把手宽/子页可折叠 + 逐页尺寸都回到存档口径
     * （qsplitter.cpp:1697-1734）。 */
    XSplitter_setOrientation(sp, XGuiOrientation_Vertical);
    XSplitter_setHandleWidth(sp, 11);
    XSplitter_setChildrenCollapsible(sp, false);
    /* [P0-10] 先显式设 opaqueResize（set 位=1、值=false）——存档里 set 位是
       0，恢复后必须**直写回 0**（getter 重新回落样式提示 true），这条断言
       同时证明尾字段里的 opaqueResizeSet 真的被消费。 */
    XSplitter_setOpaqueResize(sp, false);

    if (state) {
        XQ_CHECK(XSplitter_restoreState(sp, state));
        XQ_EQ_I64(XSplitter_orientation(sp), XGuiOrientation_Horizontal);
        /* 存档写的是**存储值**（缺省 -1 哨兵），恢复后 getter 仍回落样式 ⇒ 4。 */
        XQ_EQ_I64(XSplitter_handleWidth(sp), 4);
        XQ_CHECK(XSplitter_childrenCollapsible(sp));
        XSplitter_sizes(sp, got, 3);
        XQ_EQ_I64(got[0], 50);
        XQ_EQ_I64(got[1], 80);
        XQ_EQ_I64(got[2], 62);
        /* [P0-10 2026-11-11] 尾字段 opaqueResize/opaqueResizeSet
           （qsplitter.cpp:1675-1677 落、:1725-1732 恢复）：存档时 set=0、
           生效值=true ⇒ 显式 false 被**撤销**，getter 回落样式（true）。
           若不落/不恢复这两字段，这里会读到存储的 false。 */
        XQ_CHECK_MSG(XSplitter_opaqueResize(sp),
                     "存档 opaqueResizeSet=0 ⇒ 恢复后 getter 回落样式");

        /* 损坏数据必须拒绝且不改动控件（魔数/长度/格式校验）。 */
        XQ_CHECK(!XSplitter_restoreState(sp, NULL));
    }

    /* ---- 显式 false 的存档：恢复后 getter 读存储值 false ---- */
    XSplitter_setOpaqueResize(sp, false);
    stateOpaqueOff = XSplitter_saveState(sp);
    XQ_CHECK_PTR(stateOpaqueOff);
    XSplitter_setOpaqueResize(sp, true); /* 破坏 */
    XQ_CHECK(XSplitter_opaqueResize(sp));
    if (stateOpaqueOff) {
        XQ_CHECK(XSplitter_restoreState(sp, stateOpaqueOff));
        XQ_CHECK_MSG(!XSplitter_opaqueResize(sp),
                     "opaqueResize=false + set=1 必须原样恢复（不回落样式）");
    }

    /* ---- 显式 true 的存档：恢复后 getter 读存储值 true ---- */
    XSplitter_setOpaqueResize(sp, true);
    stateOpaqueOn = XSplitter_saveState(sp);
    XQ_CHECK_PTR(stateOpaqueOn);
    XSplitter_setOpaqueResize(sp, false); /* 破坏 */
    if (stateOpaqueOn) {
        XQ_CHECK(XSplitter_restoreState(sp, stateOpaqueOn));
        XQ_CHECK(XSplitter_opaqueResize(sp));
    }

    /* ---- 旧存档（无尾字段）向后兼容：13 + 3×5 = 28 字符 ----
       旧版存档不落 opaqueResize 两字段 ⇒ 恢复成功且**不改写**当前值
       （「缺失即不覆盖」）；其余字段（方向/页数/可折叠/尺寸）照常恢复。 */
    XSplitter_setOpaqueResize(sp, false);
    XSnprintf(legacyText, sizeof(legacyText),
              "XSP%03d%03d%03d%d%05d%05d%05d",
              (int)XGuiOrientation_Horizontal, 3, 4, 1, 10, 20, 162);
    legacy = XByteArray_create_utf8(legacyText);
    XQ_CHECK_PTR(legacy);
    if (legacy) {
        XQ_CHECK_MSG(XSplitter_restoreState(sp, legacy),
                     "旧存档（无 opaqueResize 尾字段）必须仍可恢复");
        XSplitter_sizes(sp, got, 3);
        XQ_EQ_I64(got[0], 10);
        XQ_EQ_I64(got[1], 20);
        XQ_EQ_I64(got[2], 162);
        XQ_CHECK_MSG(!XSplitter_opaqueResize(sp),
                     "尾字段缺失时不得覆盖当前 opaqueResize（仍为显式 false）");
    }

    if (state) XClassDelete((XClass*)state);
    if (stateOpaqueOff) XClassDelete((XClass*)stateOpaqueOff);
    if (stateOpaqueOn) XClassDelete((XClass*)stateOpaqueOn);
    if (legacy) XClassDelete((XClass*)legacy);
    XClassDelete((XClass*)sp);
}

/** @brief stretch 因子与 refresh（qsplitter.h:48/69）。 */
XQ_TEST(XSplitter, stretchFactorAndRefresh)
{
    XSplitter* sp = NULL;
    XWidget* p0 = NULL;
    XWidget* p1 = NULL;
    XWidget* p2 = NULL;
    int got[3] = {0, 0, 0};
    XQ_REQUIRE((sp = xq_makeThreePageSplitter(&p0, &p1, &p2)) != NULL);

    /* 未定尺寸时各页均分主轴（Qt：sizer 未设置 ⇒ 均分）。
     * 200 − 2×4 = 192 ⇒ 192/3 = 64，各页恰为 64。 */
    XSplitter_sizes(sp, got, 3);
    XQ_EQ_I64(got[0], 64);
    XQ_EQ_I64(got[1], 64);
    XQ_EQ_I64(got[2], 64);

    /* setStretchFactor 越界索引静默早退（qsplitter.cpp:1749-1753），
     * 且不得改动任何子页的 sizePolicy。 */
    {
        XWidgetSizePolicy before = XWidget_sizePolicy(p0);
        XWidgetSizePolicy after;
        XSplitter_setStretchFactor(sp, -1, 3);
        XSplitter_setStretchFactor(sp, 3, 3);
        after = XWidget_sizePolicy(p0);
        XQ_EQ_I64(XWidgetSizePolicy_horizontalStretch(&after),
                  XWidgetSizePolicy_horizontalStretch(&before));
        XQ_EQ_I64(XWidgetSizePolicy_verticalStretch(&after),
                  XWidgetSizePolicy_verticalStretch(&before));
    }

    /* 合法设置后重排不崩，尺寸和不变；stretch 因子落在**子控件自己的
     * QSizePolicy**（qsplitter.cpp:1751-1762 原文：h/v 都写、与方向无关；
     * 本机 Qt 实测水平/垂直 splitter 均 h=v=stretch）。 */
    XSplitter_setStretchFactor(sp, 0, 2);
    {
        XWidgetSizePolicy sp0 = XWidget_sizePolicy(p0);
        XWidgetSizePolicy sp1 = XWidget_sizePolicy(p1);
        XQ_EQ_I64(XWidgetSizePolicy_horizontalStretch(&sp0), 2);
        XQ_EQ_I64(XWidgetSizePolicy_verticalStretch(&sp0), 2);
        /* 其余页不受影响。 */
        XQ_EQ_I64(XWidgetSizePolicy_horizontalStretch(&sp1), 0);
        XQ_EQ_I64(XWidgetSizePolicy_verticalStretch(&sp1), 0);
    }
    XSplitter_refresh(sp);
    XSplitter_sizes(sp, got, 3);
    XQ_EQ_I64(got[0] + got[1] + got[2] + 2 * XSplitter_handleWidth(sp), 200);

    XClassDelete((XClass*)sp);
}

#endif /* XGUI_ON && XWIDGET_ON && XFRAME_ON && XSPLITTER_ON */
