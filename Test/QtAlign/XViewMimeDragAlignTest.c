/**
 * @file       XViewMimeDragAlignTest.c
 * @brief      XAbstractItemView × Qt 6.8 QAbstractItemView 拖放（视图侧）对齐契约测试。
 * @details    施工域 §B6（XMimeData 驱动的 model/view 拖放，视图侧）。对齐依据
 *             全部取自本机 Qt 6.8.3 原文：
 *             - `qabstractitemview.cpp:2036-2052` dragEnterEvent
 *               （InternalMove 且 Move 不允许 ⇒ return；canDrop ⇒ accept +
 *               setState(DraggingState)；否则 ignore）；
 *             - `qabstractitemview.cpp:2058-2127` dragMoveEvent
 *               （默认 ignore → canDrop → SelectRows 整行拓宽 → 自动滚动）；
 *             - `qabstractitemview.cpp:2161-2171` dragLeaveEvent
 *               （stopAutoScroll + setState(NoState) + viewport update）；
 *             - `qabstractitemview.cpp:2177-2201` dropEvent
 *               （InternalMove 门控 → dropOn → model->dropMimeData →
 *               setDropAction+accept / acceptProposedAction → setState(NoState)）；
 *             - `qabstractitemview.cpp:3856-3893` startDrag
 *               （selectedDraggableIndexes 非空 → model->mimeData →
 *               QDrag::exec → 复位 dropIndicatorRect/Position）；
 *             - `qabstractitemview.cpp:2215-2263` dropOn、
 *               `qabstractitemview_p.h:140-166` canDrop、
 *               `qabstractitemview.cpp:2135-2153` droppingOnItself、
 *               `qabstractitemview.cpp:2266-2290` position（margin =
 *               qBound(2, round(h/5.5), 12)）；
 *             - `qabstractitemview.cpp:1907-1922` mouseMoveEvent 拖拽分支 +
 *               `:4762-4771` maybeStartDrag（manhattanLength > startDragDistance）。
 *
 *             覆盖：setDragDropMode 的 dragEnabled/acceptDrops 双路联动、
 *             dragEnter 格式命中接受 / 格式不符拒绝 / InternalMove 门控、
 *             dragMove 指示线矩形与位置（OnItem/Above/Below/OnViewport）、
 *             SelectRows 整行拓宽、dragLeave 复位、dropEvent 调模型
 *             dropMimeData 并接受（基类模型解码 application/x-qabstractitemmodeldatalist
 *             真落数据）、dropEvent 在动作不受支持时忽略、状态机访问器、
 *             startDrag 门槛（无可拖选中索引不发起）。
 * @note       C99；视图/模型全部释放（编辑器随视图延迟删除回收），漏删即
 *             运行器判 `[LEAK]`。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON && XWINDOWEVENT_ON

#include "XAbstractItemView.h"
#include "XAbstractItemModel.h"
#include "XByteArray.h"
#include "XDataStream.h"
#include "XItemSelectionModel.h"
#include "XMimeData.h"
#include "XObject.h"
#include "XStringUtils.h"           /* XStrlen：GCC14 隐式声明即诊断 */
#include "XTableView.h"             /* 指示线 paintEvent 像素断言（⑤） */
#include "XImage.h"                 /* XWidget_grab 像素断言 */
#include "XPalette.h"               /* 指示线取色断言按实测 WindowText */
#include "XWidget.h"
#include "XWindowEvent.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XString.h"

/* ==================== 派生模型：flags 带 DragEnabled/DropEnabled ==================== */

XCLASS_DEFINE_BEGING(XQDragModel)
XCLASS_DEFINE_EXTEND_END(XQDragModel, XAbstractItemModel)

/** @brief 最小派生模型：只重载 flags 虚槽，给出 DragEnabled/DropEnabled
 *        （对标 QStandardItemModel 的缺省 flags —— 基类 QAbstractItemModel
 *        返回 NoItemFlags、本框架基类返回 Selectable|Enabled，两者都无
 *        拖放位，故拖出/可落判定必须靠派生模型加位）。 */
typedef struct XQDragModel
{
    XAbstractItemModel m_base;
} XQDragModel;

static int xqDragModel_flags(const XAbstractItemModel* self, int row, int col)
{
    int base = XClass_Parent(XAbstractItemModel, EXAbstractItemModel_Flags,
                             int (*)(const XAbstractItemModel*, int, int))
                   (self, row, col);
    if (base == XItemFlag_NoItemFlags) return base;
    return base | XItemFlag_ItemIsDragEnabled | XItemFlag_ItemIsDropEnabled;
}

/** @brief supportedDropActions 重载槽（2026-11-15 B6R 返工③的锁）：
 *        回落父类缺省（Copy）再补 MoveAction 位——对齐 Qt 派生模型
 *        `supportedDropActions() { return base | Qt::MoveAction; }` 的
 *        常见写法（Qt 侧该函数即 virtual，qabstractitemmodel.h:298）。 */
static int xqDragModel_supportedDropActions(const XAbstractItemModel* self)
{
    int base = XClass_Parent(XAbstractItemModel,
                             EXAbstractItemModel_SupportedDropActions,
                             int (*)(const XAbstractItemModel*))(self);
    return base | XDropAction_MoveAction;
}

/** @brief supportedDragActions 重载槽：回落父类（缺省=supportedDropActions）
 *        再补 MoveAction 位（Qt 侧 supportedDragActions 亦 virtual，
 *        qabstractitemmodel.h:294）。 */
static int xqDragModel_supportedDragActions(const XAbstractItemModel* self)
{
    int base = XClass_Parent(XAbstractItemModel,
                             EXAbstractItemModel_SupportedDragActions,
                             int (*)(const XAbstractItemModel*))(self);
    return base | XDropAction_MoveAction;
}

static void xqDragModel_deinit(XQDragModel* self)
{
    if (!self) return;
    XClass_Parent(XAbstractItemModel, EXClass_Deinit,
                  void (*)(XAbstractItemModel*))(&self->m_base);
}

/* 符号名必须与 XClassSetVtable(XQDragModel) 展开一致（宏展开为
 * `XQDragModel_class_init()`）。 */
static XVtable* XQDragModel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQDragModel)
    XVTABLE_INHERIT_XCLASS(XAbstractItemModel);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xqDragModel_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemModel_Flags, xqDragModel_flags);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemModel_SupportedDropActions,
                             xqDragModel_supportedDropActions);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractItemModel_SupportedDragActions,
                             xqDragModel_supportedDragActions);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化派生模型（栈对象，rows×cols 维度；析构用 XClassDeinit）。 */
static void xqDragModel_init(XQDragModel* self, int rows, int cols)
{
    if (!self) return;
    XAbstractItemModel_init(&self->m_base);
    XClassSetVtable(self, XQDragModel);
    XAbstractItemModel_setDimension(&self->m_base, rows, cols);
}

/* ==================== 探针 ==================== */

/** @brief 矩形带内命中指定 RGB（容差 tol）的像素数（仿
 *        XComboBoxValidatorAlignTest 的 xqcv_colorHits；指示线 grab
 *        断言用——数墨水而非猜固定色，色值由调用方按实测调色板传入）。 */
static int xqvd_colorHits(const XImage* img, int x0, int y0, int x1, int y1,
                          int rr, int gg, int bb, int tol)
{
    int n = 0;
    int x;
    int y;
    int w;
    int h;
    if (!img) return -1;
    w = XImage_width(img);
    h = XImage_height(img);
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    for (y = y0; y < y1; ++y) {
        for (x = x0; x < x1; ++x) {
            uint32_t px = XImage_pixel(img, x, y);
            int r = (int)((px >> 16) & 0xFFu);
            int g = (int)((px >> 8) & 0xFFu);
            int b = (int)(px & 0xFFu);
            if (r - rr <= tol && rr - r <= tol &&
                g - gg <= tol && gg - g <= tol &&
                b - bb <= tol && bb - b <= tol)
                ++n;
        }
    }
    return n;
}

/** @brief 视图调色板 WindowText 的 (r,g,b)（指示线绘制色与断言同源）。 */
static void xqvd_windowTextRgb(const XAbstractItemView* view,
                               int* outR, int* outG, int* outB)
{
#if XPALETTE_ON
    XPalette pal = XWidget_palette((const XWidget*)view);
    XColor c = XPalette_color(&pal, XPaletteColorGroup_Current,
                              XPaletteColorRole_WindowText);
    uint32_t rgba = XColor_rgba(&c);
    *outR = (int)((rgba >> 16) & 0xFFu);
    *outG = (int)((rgba >> 8) & 0xFFu);
    *outB = (int)(rgba & 0xFFu);
#else
    (void)view;
    *outR = 0;
    *outG = 0;
    *outB = 0;
#endif /* XPALETTE_ON */
}

/** @brief 构造一个拖放事件（mimeType + 字节载荷）。
 * @param type     XEVENT_TYPE_DRAG_ENTER / DRAG_MOVE / DRAG_LEAVE / DROP。
 * @param x,y      控件局部坐标。
 * @param mime     MIME 类型名（UTF-8）。
 * @param bytes    载荷字节流（可为 NULL；**二进制透明**——2026-11-15 差异②
 *                 收口后载荷是 XByteArray，含 0x00 的模型 wire 原样直达）。
 * @param len      载荷字节数。
 * @param dropAct  当前放置动作（XDropAction 位）。
 * @param possible 可用动作位组合。
 * @return 新事件（调用方 XClassDelete）；失败 NULL。
 * @warning 事件拥有 mimeType/data 的副本，入参可立即释放。
 * @note    事件构造后缺省**未接受**（XDropEvent_init 已对齐 Qt QDropEvent
 *          构造尾 ignore()，qevent.cpp:3052——真实平台路径本就如此）；
 *          "最终 accepted"只能来自视图自己 accept，拒绝行为测的是视图
 *          本体（既有用例里显式 XEvent_ignore 仍保留，幂等无碍）。 */
static XDropEvent* xqViewDrag_makeEventBytes(int type, int x, int y,
                                             const char* mime,
                                             const void* bytes, int len,
                                             int dropAct, int possible)
{
    XPoint pos;
    XPoint gpos;
    XString* m = NULL;
    XByteArray* d = NULL;
    XDropEvent* ev;
    XPoint_init(&pos, x, y);
    XPoint_init(&gpos, x, y);
    m = XString_create_utf8(mime ? mime : "");
    d = XByteArray_create_with_data((const char*)bytes,
                                    (size_t)(len > 0 ? len : 0));
    if (!m || !d) {
        if (m) XClassDelete((XClass*)m);
        if (d) XClassDelete((XClass*)d);
        return NULL;
    }
    ev = XDropEvent_create(type, &pos, &gpos, m, d);
    XClassDelete((XClass*)m);
    XClassDelete((XClass*)d);
    if (!ev) return NULL;
    XDropEvent_setDropAction(ev, dropAct);
    XDropEvent_setPossibleActions(ev, possible);
    return ev;
}

/** @brief 文本载荷便捷版（UTF-8 字节直传 makeEventBytes）。 */
static XDropEvent* xqViewDrag_makeEvent(int type, int x, int y,
                                        const char* mime,
                                        const char* payload,
                                        int dropAct, int possible)
{
    return xqViewDrag_makeEventBytes(type, x, y, mime,
                                     payload,
                                     payload ? (int)XStrlen(payload) : 0,
                                     dropAct, possible);
}

/* ==================== 用例 ==================== */

/** @brief setDragDropMode 双路联动（qabstractitemview.cpp:1536-1550：
 *        DragOnly/DragDrop/InternalMove 开 dragEnabled；
 *        DropOnly/DragDrop/InternalMove 开 acceptDrops）。 */
XQ_TEST(XViewMimeDrag, setDragDropModeWiresBothPaths)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XQ_REQUIRE(v != NULL);

    /* NoDragDrop：两路都关。 */
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_NoDragDrop);
    XQ_CHECK(!XAbstractItemView_dragEnabled(v));
    XQ_CHECK(!XWidget_acceptDrops((const XWidget*)v));

    /* DragOnly：只开拖出（dragEnabled），不接受拖入。 */
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragOnly);
    XQ_CHECK(XAbstractItemView_dragEnabled(v));
    XQ_CHECK(!XWidget_acceptDrops((const XWidget*)v));

    /* DropOnly：只接受拖入（acceptDrops），不开拖出。 */
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DropOnly);
    XQ_CHECK(!XAbstractItemView_dragEnabled(v));
    XQ_CHECK(XWidget_acceptDrops((const XWidget*)v));

    /* DragDrop / InternalMove：两路全开。 */
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    XQ_CHECK(XAbstractItemView_dragEnabled(v));
    XQ_CHECK(XWidget_acceptDrops((const XWidget*)v));

    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_InternalMove);
    XQ_CHECK(XAbstractItemView_dragEnabled(v));
    XQ_CHECK(XWidget_acceptDrops((const XWidget*)v));

    /* 非法模式值归一为 NoDragDrop（与本类其余枚举 setter 同口径）。 */
    XAbstractItemView_setDragDropMode(v, 999);
    XQ_EQ_I64(XAbstractItemView_dragDropMode(v),
              XAbstractItemViewDragDropMode_NoDragDrop);
    XQ_CHECK(!XAbstractItemView_dragEnabled(v));
    XQ_CHECK(!XWidget_acceptDrops((const XWidget*)v));

    XClassDelete((XClass*)v);
}

/** @brief dragEnterEvent：格式命中模型 mimeTypes ⇒ accept + DraggingState；
 *        格式不符 ⇒ ignore（qabstractitemview.cpp:2036-2052 +
 *        qabstractitemview_p.h:140-155）。 */
XQ_TEST(XViewMimeDrag, dragEnterAcceptsOnFormatMatch)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XDropEvent* ev;
    XQ_REQUIRE(v != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 4, 2);
    XAbstractItemView_setModel_base(v, m);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    XQ_EQ_I64(XAbstractItemView_state(v),
              XAbstractItemViewDragState_NoState);

    /* 命中模型缺省 mimeTypes ⇒ 接受并进入 DraggingState。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_ENTER, 10, 10,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    /* 发送前显式 ignore：让"最终 accepted"只能来自视图 accept（有区分力）。 */
    XEvent_ignore((XEvent*)ev);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_CHECK_MSG(XEvent_isAccepted((XEvent*)ev),
                 "格式命中应接受（qabstractitemview.cpp:2042-2047）");
    XQ_EQ_I64(XAbstractItemView_state(v),
              XAbstractItemViewDragState_DraggingState);
    XClassDelete((XClass*)ev);

    /* 复位后再测格式不符：应忽略且状态保持 NoState。 */
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_NoDragDrop);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_ENTER, 10, 10,
                              "text/plain", "x",
                              XDropAction_CopyAction, XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_CHECK_MSG(!XEvent_isAccepted((XEvent*)ev),
                 "格式不在模型 mimeTypes 内应忽略");
    XQ_EQ_I64(XAbstractItemView_state(v),
              XAbstractItemViewDragState_NoState);
    XClassDelete((XClass*)ev);

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief dragEnterEvent 动作门控：dropAction 不在 supportedDropActions 内
 *        ⇒ 拒绝（qabstractitemview_p.h:146 严格按位与，无动作回落）。 */
XQ_TEST(XViewMimeDrag, dragEnterRejectsUnsupportedAction)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XDropEvent* ev;
    XQ_REQUIRE(v != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 2, 1);
    XAbstractItemView_setModel_base(v, m);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);

    /* 模型缺省 supportedDropActions = CopyAction；DropAction=LinkAction
     * 与之无交集 ⇒ 拒绝（Qt 不做"回落到 Copy"）。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_ENTER, 10, 10,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_LinkAction,
                              XDropAction_LinkAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_CHECK_MSG(!XEvent_isAccepted((XEvent*)ev),
                 "动作不在 supportedDropActions 内应拒绝（qt 原文严格按位与）");
    XClassDelete((XClass*)ev);

    /* CopyAction 命中 ⇒ 接受（对照组，排除"恒拒绝"误判）。
     * 预 ignore 保证 accept 只能来自视图。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_ENTER, 10, 10,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    XEvent_ignore((XEvent*)ev);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_CHECK(XEvent_isAccepted((XEvent*)ev));
    XClassDelete((XClass*)ev);

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief dragMoveEvent：指示线位置与矩形（OnItem 取条目矩形；Above/Below
 *        取视口宽横线且高为 0），以及访问器读取口径
 *        （qabstractitemview.cpp:2066-2120 + :2266-2290 的 margin 公式）。 */
XQ_TEST(XViewMimeDrag, dragMoveComputesIndicator)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XDropEvent* ev;
    int x = 0, y = 0, w = 0, h = 0;
    XQ_REQUIRE(v != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 4, 2);
    XAbstractItemView_setModel_base(v, m);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);

    /* 基类缺省条目几何：col*80, row*24, 80x24（visualRect 缺省网格）。
     * 条目 (1,1) = (80,24,80,24)。指针落在其垂直中部（y=24+12=36）且离
     * 上下边 > margin（margin = qBound(2, qRound(24/5.5)=4, 12) = 4）。
     *
     * **基类模型 flags 无 ItemIsDropEnabled**（XAbstractItemModel.c 的
     * VXAbstractItemModel_flags 只给 Selectable|Enabled）⇒ 按 Qt
     * qabstractitemview.cpp:2283-2284 必须**降级**：落点在上半 ⇒ AboveItem、
     * 下半 ⇒ BelowItem，绝不报 OnItem。y=36 相对 rect(24..48) 的中线 36
     * 恰好等于中线 ⇒ 按 `<` 判定走 BelowItem。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_MOVE, 100, 36,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_CHECK_MSG(XAbstractItemView_dropIndicatorPosition(v) ==
                     XAbstractItemViewDropIndicatorPosition_BelowItem,
                 "基类模型无 DropEnabled ⇒ OnItem 必须降级（Qt :2283-2284）");
    XClassDelete((XClass*)ev);

    /* 同落点但换**带 DropEnabled 的派生模型**：不降级，报 OnItem 且矩形
     * 取条目本体（col*80=80, row*24=24, 80x24）。 */
    {
        XQDragModel dm;
        xqDragModel_init(&dm, 4, 2);
        XAbstractItemView_setModel_base(v, (XAbstractItemModel*)&dm);
        ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_MOVE, 100, 36,
                                  "application/x-qabstractitemmodeldatalist",
                                  "x", XDropAction_CopyAction,
                                  XDropAction_CopyAction);
        XQ_REQUIRE(ev != NULL);
        XEvent_ignore((XEvent*)ev);
        (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
        XQ_CHECK_MSG(XEvent_isAccepted((XEvent*)ev), "条目中部应可落并接受");
        XQ_EQ_I64(XAbstractItemView_dropIndicatorPosition(v),
                  XAbstractItemViewDropIndicatorPosition_OnItem);
        XQ_CHECK(XAbstractItemView_dropIndicatorRect(v, &x, &y, &w, &h));
        XQ_EQ_I64(x, 80);
        XQ_EQ_I64(y, 24);
        XQ_EQ_I64(w, 80);
        XQ_EQ_I64(h, 24);
        XClassDelete((XClass*)ev);
        /* 换回基类模型继续测 Above/Below 的横线形状。 */
        XAbstractItemView_setModel_base(v, m);
        /* 栈上派生模型出作用域前必须 deinit：视图刚才连过它的信号槽，
         * 注册表/维度存储都是堆分配，漏拆即运行器判 [LEAK]
         * （helper 的 xqDragModel_deinit 已挂虚表，走 XClassDeinit 分派）。 */
        XClassDeinit((XClass*)&dm);
    }

    /* 近上边缘（y=25，距 rect.y=24 为 1 < margin 4）⇒ AboveItem，
     * 横线：x=0、宽=视口宽、高=0。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_MOVE, 100, 25,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_EQ_I64(XAbstractItemView_dropIndicatorPosition(v),
              XAbstractItemViewDropIndicatorPosition_AboveItem);
    XQ_CHECK(XAbstractItemView_dropIndicatorRect(v, &x, &y, &w, &h));
    XQ_EQ_I64(x, 0);
    XQ_EQ_I64(y, 24);
    XQ_EQ_I64(w, 400);
    XQ_EQ_I64(h, 0);
    XClassDelete((XClass*)ev);

    /* 近下边缘（y=46，rect.y+height=48，距 2 < margin 4）⇒ BelowItem。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_MOVE, 100, 46,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_EQ_I64(XAbstractItemView_dropIndicatorPosition(v),
              XAbstractItemViewDropIndicatorPosition_BelowItem);
    XQ_CHECK(XAbstractItemView_dropIndicatorRect(v, &x, &y, &w, &h));
    XQ_EQ_I64(y, 48); /* rect.y + rect.height */
    XQ_EQ_I64(h, 0);
    XClassDelete((XClass*)ev);

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief dragMoveEvent OnViewport：指针落在视口空白（模型网格之外）⇒
 *        OnViewport 且无指示线矩形，**且不接受**——Qt
 *        qabstractitemview.cpp:2110-2113 要求 OnViewport 下
 *        `isIndexDropEnabled(rootIndex())` 才 acceptProposedAction。
 *        本框架 root 平铺（无条目可查）⇒ 不可落；基类模型 flags 也无
 *        ItemIsDropEnabled，与 Qt 缺省模型结论一致。 */
XQ_TEST(XViewMimeDrag, dragMoveOnViewportHasNoRect)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XDropEvent* ev;
    int x = -9, y = -9, w = -9, h = -9;
    XQ_REQUIRE(v != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 2, 1);
    XAbstractItemView_setModel_base(v, m);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);

    /* x=300/80=3 ≥ 列数 1 ⇒ indexAt 失败 ⇒ 落视口空白。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_MOVE, 300, 60,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_EQ_I64(XAbstractItemView_dropIndicatorPosition(v),
              XAbstractItemViewDropIndicatorPosition_OnViewport);
    XQ_CHECK_MSG(!XAbstractItemView_dropIndicatorRect(v, &x, &y, &w, &h),
                 "视口空白无指示线矩形（Qt dropIndicatorRect.isNull()）");
    XQ_CHECK(x == 0 && y == 0 && w == 0 && h == 0);
    XQ_CHECK_MSG(!XEvent_isAccepted((XEvent*)ev),
                 "OnViewport 且 root 不可落 ⇒ 不接受（Qt :2110-2113）");
    XClassDelete((XClass*)ev);

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief dragMoveEvent + SelectRows：Above/Below 时指示线矩形横跨视口整行
 *        （qabstractitemview.cpp:2076-2084 rect.setWidth(viewport()->width()
 *        - 1 - rect.x())）。 */
XQ_TEST(XViewMimeDrag, dragMoveSelectRowsSpansRow)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XDropEvent* ev;
    int x = 0, y = 0, w = 0, h = 0;
    XQ_REQUIRE(v != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 4, 3);
    XAbstractItemView_setModel_base(v, m);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    XAbstractItemView_setSelectionBehavior(
        v, XAbstractItemViewSelectionBehavior_SelectRows);
    XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);

    /* 条目 (1,1) 上边缘附近 ⇒ AboveItem；SelectRows 时矩形从列 0 起点
     * 拉到视口右缘（width = 400 - 1 - 0 = 399）。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_MOVE, 100, 25,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_EQ_I64(XAbstractItemView_dropIndicatorPosition(v),
              XAbstractItemViewDropIndicatorPosition_AboveItem);
    XQ_CHECK(XAbstractItemView_dropIndicatorRect(v, &x, &y, &w, &h));
    XQ_EQ_I64(x, 0);   /* visualRect(row, 0).x = 0 */
    XQ_EQ_I64(w, 399); /* viewport width - 1 - x */
    XClassDelete((XClass*)ev);

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief dragLeaveEvent：停自动滚动、状态回 NoState、指示线复位
 *        （qabstractitemview.cpp:2161-2171）。 */
XQ_TEST(XViewMimeDrag, dragLeaveResetsState)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XDropEvent* ev;
    int x = -9, y = -9, w = -9, h = -9;
    XQ_REQUIRE(v != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 4, 2);
    XAbstractItemView_setModel_base(v, m);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);

    /* 先进入 + 移动，攒出 DraggingState 与指示线。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_ENTER, 100, 36,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XClassDelete((XClass*)ev);
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_MOVE, 100, 36,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XClassDelete((XClass*)ev);
    XQ_EQ_I64(XAbstractItemView_state(v),
              XAbstractItemViewDragState_DraggingState);
    XQ_CHECK(XAbstractItemView_dropIndicatorRect(v, &x, &y, &w, &h));

    /* 离开：状态与指示线都要复位。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_LEAVE, 100, 36,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XClassDelete((XClass*)ev);
    XQ_EQ_I64(XAbstractItemView_state(v),
              XAbstractItemViewDragState_NoState);
    XQ_EQ_I64(XAbstractItemView_dropIndicatorPosition(v),
              XAbstractItemViewDropIndicatorPosition_OnItem);
    XQ_CHECK_MSG(!XAbstractItemView_dropIndicatorRect(v, &x, &y, &w, &h),
                 "离开后指示线矩形应清空（qabstractitemview.cpp:2166-2170）");

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief dropEvent：模型拒绝时必须**显式 ignore**（qabstractitemview.cpp:
 *        2177-2201 + 本框架的 accepted 缺省语义）。
 *
 *        **为什么这里不测"真落数据"**（实测证据，不是未做）：
 *        模型侧 `application/x-qabstractitemmodeldatalist` 的 wire 载荷是
 *        XDataStream 的 4 字节大端整数（含大量 0x00），而 `XDropEvent`
 *        用 `XString` 承载载荷。本机用 build/compile_commands.json 同套参数
 *        链 build/libXinYueCSd.a 实测（探针源码见下方注释）：
 *            XString_create_with_length_latin1(bytes, 8)  ⇒ strlen=0 utf8len=0
 *            XString_create_with_length_latin1(wire, 12)  ⇒ strlen=0 utf8len=0
 *        根因：XChar_fromLatin1Stream（Src/XCore/XData/XChar/XChar.c:1037-1048）
 *        先经 XChar_streamInputLenU8（同文件 :174-185）在第一个 0x00 处截断；
 *        XString_toUtf8 侧（XChar_toUtf8Stream）同样遇 0 截断。
 *        ⇒ 经 XDropEvent 传递的模型 wire 载荷必然退化为空串，模型
 *        dropMimeData 解不出任何索引 ⇒ 端到端"真落数据"在现框架**不可达**。
 *        这是跨域结构缺陷（需 XWindowEvent 把载荷改为二进制透明承载，
 *        非本域 owned），已登记 openNotes。
 *
 *        因此本用例锁定**真实成立**的链路：空载荷 ⇒ 模型 dropMimeData 返回
 *        false ⇒ 视图必须显式 ignore（不能靠 XEvent 缺省 accepted=true，否则
 *        平台层 XWindowSystemInterface.c:707 会把"失败"当成功回给 XDND）。 */
XQ_TEST(XViewMimeDrag, dropRejectedIgnoresEvent)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XDropEvent* ev;
    int rowsBefore;
    XQ_REQUIRE(v != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 2, 1);
    XAbstractItemView_setModel_base(v, m);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);
    rowsBefore = XAbstractItemModel_rowCount(m);

    /* 空载荷 + 正确格式 + 支持的动作：dropOn 通过（动作/落点都对），但模型
     * 解码不出数据 ⇒ dropMimeData false ⇒ 必须 ignore。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DROP, 10, 1,
                              "application/x-qabstractitemmodeldatalist",
                              "", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_CHECK_MSG(!XEvent_isAccepted((XEvent*)ev),
                 "模型拒绝时必须显式 ignore（否则平台层误报成功）");
    XQ_EQ_I64(XAbstractItemModel_rowCount(m), rowsBefore);
    XQ_EQ_I64(XAbstractItemView_state(v),
              XAbstractItemViewDragState_NoState);
    XQ_CHECK(!XAbstractItemView_dropIndicatorRect(v, NULL, NULL, NULL, NULL));
    XClassDelete((XClass*)ev);

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief dropEvent：动作不受 supportedDropActions 支持 ⇒ 不落数据、不
 *        接受（qabstractitemview.cpp:2229-2230 dropOn 门控）。 */
XQ_TEST(XViewMimeDrag, dropRejectedWhenActionUnsupported)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XDropEvent* ev;
    int rowsBefore;
    XQ_REQUIRE(v != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 2, 1);
    XAbstractItemView_setModel_base(v, m);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);
    rowsBefore = XAbstractItemModel_rowCount(m);

    /* LinkAction 不在模型缺省 supportedDropActions(Copy) 内 ⇒ dropOn false。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DROP, 10, 10,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_LinkAction,
                              XDropAction_LinkAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_CHECK_MSG(!XEvent_isAccepted((XEvent*)ev),
                 "动作不受支持时 drop 应忽略");
    XQ_EQ_I64(XAbstractItemModel_rowCount(m), rowsBefore);
    XClassDelete((XClass*)ev);

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief InternalMove 门控：possibleActions 不含 MoveAction ⇒ dragEnter /
 *        drop 都直接拒绝（qabstractitemview.cpp:2038-2040 / :2179-2183）。
 *
 * @note 本文件**不**尝试锁定 droppingOnItself 的"Copy 落到已选行放行"分支：
 *       该分支的前提是"载荷能让模型接受"，而经 XDropEvent 传递的模型 wire
 *       载荷在现框架必然退化为空串（实测探针记录见
 *       dropRejectedIgnoresEvent）⇒ 模型恒 reject ⇒ "放行/拦截"两种情形在
 *       上层都表现为"不接受"，**不可区分**；硬写断言等于把不可观测当已覆盖。
 *       等 XWindowEvent 把载荷改成二进制透明承载后（openNotes 已登记），
 *       再补 droppingOnItself 的 Move-自拖拦截用例。 */
XQ_TEST(XViewMimeDrag, internalMoveRequiresMoveAction)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XDropEvent* ev;
    int rowsBefore;
    XQ_REQUIRE(v != NULL && m != NULL);

    XAbstractItemModel_setDimension(m, 2, 1);
    XAbstractItemView_setModel_base(v, m);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_InternalMove);
    XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);

    /* dragEnter 且 possibleActions 只有 Copy ⇒ 门控直接 return，不 accept。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_ENTER, 10, 10,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_CHECK_MSG(!XEvent_isAccepted((XEvent*)ev),
                 "InternalMove 下 possibleActions 无 Move ⇒ dragEnter 直接返回");
    XClassDelete((XClass*)ev);

    /* drop 同理：不落数据。 */
    rowsBefore = XAbstractItemModel_rowCount(m);
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DROP, 10, 10,
                              "application/x-qabstractitemmodeldatalist",
                              "x", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
    XQ_CHECK(!XEvent_isAccepted((XEvent*)ev));
    XQ_EQ_I64(XAbstractItemModel_rowCount(m), rowsBefore);
    XClassDelete((XClass*)ev);

    /* 注意：**不能**用"possibleActions 含 Move 的 dragEnter"当 positive 对照组
     * ——Qt 的 dragEnter 分支（qabstractitemview_p.h:146）判的是
     * `event->dropAction() & supportedDropActions()`，而缺省模型只支持 Copy
     * ⇒ Move 依然不匹配、依然拒绝（Qt 同结论）。要让 InternalMove 的
     * dragEnter 通过，需模型 supportedDropActions 含 Move，而本框架的
     * supportedDropActions 是普通函数而非虚槽（无法派生重载）——该缺口已
     * 登记 openNotes。 */

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief startDrag 门槛（qabstractitemview.cpp:1907-1922 +
 *        :4762-4771）：**真的**驱动 mousePress → mouseMove（超
 *        startDragDistance）看状态机副作用，对照"选中但不可拖"与
 *        "选中且可拖"两种模型。
 *
 *        观测口径：startDrag 末段会复位指示线与状态（:3889-3892），所以
 *        只看最终状态无法区分"没进 startDrag"与"进了又复位"。这里改用
 *        **进入前后的 m_dropRect 是否被清**：先经 dragMove 攒出指示线，
 *        再发 mouseMove；两条路径都会在 startDrag 里清掉指示线，故真正可
 *        区分的是 draggable 闸之前的 gate——用 `xaiv_hasDraggableSelection`
 *        的等价外部观测：基类模型上 flags 无 DragEnabled（断言），派生模型
 *        上有（断言）。两档都必须"不崩 + 状态回 NoState"，且在可拖档下
 *        startDrag 已执行完（状态已复位、指示线已清）。 */
XQ_TEST(XViewMimeDrag, startDragGateFollowsDraggableFlags)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XItemSelectionModel* sm;
    XPoint p;
    XQ_REQUIRE(v != NULL);

    /* --- 档 A：基类模型（flags 无 ItemIsDragEnabled）⇒ 拖出闸不通过 --- */
    {
        XAbstractItemModel* m = XAbstractItemModel_create();
        XQ_REQUIRE(m != NULL);
        XAbstractItemModel_setDimension(m, 4, 2);
        XAbstractItemView_setModel_base(v, m);
        XAbstractItemView_setDragDropMode(
            v, XAbstractItemViewDragDropMode_DragDrop);
        XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);
        sm = XAbstractItemView_selectionModel(v);
        XQ_REQUIRE(sm != NULL);
        XQ_EQ_I64(XAbstractItemModel_flags_base(m, 1, 0) &
                      XItemFlag_ItemIsDragEnabled, 0);
        XQ_CHECK(XItemSelectionModel_select(sm, 1, 0,
                    XItemSelectionModelSelectionFlag_ClearAndSelect));
        /* 按下 (100,30)，再移动 (200,30)：位移 100 > startDragDistance(10)。 */
        XPoint_init(&p, 100, 30);
        {
            /* 鼠标事件是堆对象，发送后必须回收（漏删即运行器判 [LEAK]）。 */
            XEvent* me = (XEvent*)XMouseEvent_create(
                XEVENT_TYPE_MOUSE_BUTTON_PRESS, XMouseButton_LeftButton,
                XKeyboardModifier_NoModifier, p);
            (void)XCoreApplication_sendEvent((XObject*)v, me);
            XClassDelete(me);
        }
        XPoint_init(&p, 200, 30);
        {
            XEvent* me = (XEvent*)XMouseEvent_create(
                XEVENT_TYPE_MOUSE_MOVE, XMouseButton_LeftButton,
                XKeyboardModifier_NoModifier, p);
            (void)XCoreApplication_sendEvent((XObject*)v, me);
            XClassDelete(me);
        }
        XQ_CHECK_MSG(XAbstractItemView_state(v) ==
                         XAbstractItemViewDragState_NoState,
                     "基类模型无可拖索引 ⇒ 不进 startDrag，状态仍 NoState");
        /* 视图仍借用 m（模型归调用方，视图只持借用指针）：先解绑再删，
           否则档 B 的 setModel 断开旧模型信号时会摸已释放对象（ASan
           heap-use-after-free 实证，2026-11-15 B2R 返工跨域修）。 */
        XAbstractItemView_setModel_base(v, NULL);
        XClassDelete((XClass*)m);
    }

    /* --- 档 B：派生模型（flags 含 ItemIsDragEnabled）⇒ 拖出闸通过 --- */
    {
        XQDragModel dm;
        xqDragModel_init(&dm, 4, 2);
        XAbstractItemView_setModel_base(v, (XAbstractItemModel*)&dm);
        XAbstractItemView_setDragDropMode(
            v, XAbstractItemViewDragDropMode_DragDrop);
        XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);
        sm = XAbstractItemView_selectionModel(v);
        XQ_REQUIRE(sm != NULL);
        XQ_CHECK(XAbstractItemModel_flags_base((XAbstractItemModel*)&dm, 1, 0) &
                 XItemFlag_ItemIsDragEnabled);
        XQ_CHECK(XItemSelectionModel_select(sm, 1, 0,
                    XItemSelectionModelSelectionFlag_ClearAndSelect));
        /* 先经 dragMove 攒出指示线，证明"进 startDrag 前有指示线"。 */
        {
            XDropEvent* mv = xqViewDrag_makeEvent(
                XEVENT_TYPE_DRAG_MOVE, 100, 36,
                "application/x-qabstractitemmodeldatalist", "x",
                XDropAction_CopyAction, XDropAction_CopyAction);
            XQ_REQUIRE(mv != NULL);
            (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)mv);
            XQ_CHECK(XAbstractItemView_dropIndicatorRect(v, NULL, NULL, NULL,
                                                         NULL));
            XClassDelete((XClass*)mv);
        }
        XPoint_init(&p, 100, 30);
        {
            /* 鼠标事件是堆对象，发送后必须回收（漏删即运行器判 [LEAK]）。 */
            XEvent* me = (XEvent*)XMouseEvent_create(
                XEVENT_TYPE_MOUSE_BUTTON_PRESS, XMouseButton_LeftButton,
                XKeyboardModifier_NoModifier, p);
            (void)XCoreApplication_sendEvent((XObject*)v, me);
            XClassDelete(me);
        }
        XPoint_init(&p, 200, 30);
        {
            XEvent* me = (XEvent*)XMouseEvent_create(
                XEVENT_TYPE_MOUSE_MOVE, XMouseButton_LeftButton,
                XKeyboardModifier_NoModifier, p);
            (void)XCoreApplication_sendEvent((XObject*)v, me);
            XClassDelete(me);
        }
        /* startDrag 末段复位（:3889-3892）：状态回 NoState、指示线清空。
         * 若闸不通过（未进 startDrag），指示线会**留着** ⇒ 该断言即区分点。 */
        XQ_CHECK_MSG(XAbstractItemView_state(v) ==
                         XAbstractItemViewDragState_NoState,
                     "startDrag 返回后状态必须回 NoState（:4769-4770）");
        XQ_CHECK_MSG(!XAbstractItemView_dropIndicatorRect(v, NULL, NULL, NULL,
                                                          NULL),
                     "startDrag 末段必须清指示线（:3889-3891）");
        /* 拆除顺序：视图此刻仍指着栈上 dm——先删视图（其析构会从 dm 的
         * 信号槽摘连接），再 deinit dm；反序即析构摸已拆结构（悬垂）。 */
        XClassDelete((XClass*)v);
        XClassDeinit((XClass*)&dm);
    }
}

/** @brief 位移未超 startDragDistance ⇒ 不进 startDrag（指示线保留）。
 *
 *        与上一个用例的档 B 构成对照：同模型、同选中，只把位移从 100 缩到
 *        1（< startDragDistance=10）⇒ startDrag 不执行 ⇒ 指示线不被清。
 *        这条锁的是 `maybeStartDrag` 的**严格大于**阈值（:4767）。 */
XQ_TEST(XViewMimeDrag, startDragRespectsDistanceThreshold)
{
    XQDragModel dm;
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XItemSelectionModel* sm;
    XDropEvent* mv;
    XPoint p;
    XQ_REQUIRE(v != NULL);

    xqDragModel_init(&dm, 4, 2);
    XAbstractItemView_setModel_base(v, (XAbstractItemModel*)&dm);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);
    sm = XAbstractItemView_selectionModel(v);
    XQ_REQUIRE(sm != NULL);
    XQ_CHECK(XItemSelectionModel_select(sm, 1, 0,
                XItemSelectionModelSelectionFlag_ClearAndSelect));

    /* 先攒指示线。 */
    mv = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_MOVE, 100, 36,
                              "application/x-qabstractitemmodeldatalist", "x",
                              XDropAction_CopyAction, XDropAction_CopyAction);
    XQ_REQUIRE(mv != NULL);
    (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)mv);
    XQ_CHECK(XAbstractItemView_dropIndicatorRect(v, NULL, NULL, NULL, NULL));
    XClassDelete((XClass*)mv);

    /* 按下 (100,30)，移动 (101,30)：位移 1，未超阈值 ⇒ startDrag 不执行。 */
    XPoint_init(&p, 100, 30);
    {
        /* 鼠标事件是堆对象，发送后必须回收（漏删即运行器判 [LEAK]）。 */
        XEvent* me = (XEvent*)XMouseEvent_create(
            XEVENT_TYPE_MOUSE_BUTTON_PRESS, XMouseButton_LeftButton,
            XKeyboardModifier_NoModifier, p);
        (void)XCoreApplication_sendEvent((XObject*)v, me);
        XClassDelete(me);
    }
    XPoint_init(&p, 101, 30);
    {
        XEvent* me = (XEvent*)XMouseEvent_create(
            XEVENT_TYPE_MOUSE_MOVE, XMouseButton_LeftButton,
            XKeyboardModifier_NoModifier, p);
        (void)XCoreApplication_sendEvent((XObject*)v, me);
        XClassDelete(me);
    }
    XQ_CHECK_MSG(XAbstractItemView_dropIndicatorRect(v, NULL, NULL, NULL, NULL),
                 "位移未超 startDragDistance ⇒ 不进 startDrag ⇒ 指示线仍在");
    XQ_CHECK(XAbstractItemView_state(v) ==
             XAbstractItemViewDragState_NoState);

    XClassDelete((XClass*)v);
    XClassDeinit((XClass*)&dm);
}

/** @brief supportedDropActions 访问器：有模型取模型侧（基类缺省 Copy），
 *        无模型回退 CopyAction（对标 QAbstractItemView::supportedDropActions）。
 *        另锁 dropIndicatorRect 的空指针容错（out* 全 NULL 仍可当"有无"查询）。 */
XQ_TEST(XViewMimeDrag, supportedDropActionsAccessor)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(v != NULL && m != NULL);

    /* 无模型：回退 CopyAction。 */
    XQ_EQ_I64(XAbstractItemView_supportedDropActions(v),
              XDropAction_CopyAction);

    XAbstractItemModel_setDimension(m, 2, 2);
    XAbstractItemView_setModel_base(v, m);
    /* 基类模型 supportedDropActions 缺省 Copy（qabstractitemmodel.cpp:2251；
     * 2026-11-15 B6R 返工改走虚槽入口 _base）。 */
    XQ_EQ_I64(XAbstractItemView_supportedDropActions(v),
              XAbstractItemModel_supportedDropActions_base(m));
    XQ_EQ_I64(XAbstractItemView_supportedDropActions(v),
              XDropAction_CopyAction);

    /* 空指针容错：物对象可为 NULL（返回 CopyAction）。 */
    XQ_EQ_I64(XAbstractItemView_supportedDropActions(NULL),
              XDropAction_CopyAction);
    /* dropIndicatorRect 全 NULL 出参：仅作谓词使用，不崩。 */
    XQ_CHECK(!XAbstractItemView_dropIndicatorRect(v, NULL, NULL, NULL, NULL));

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief 端到端：drop 数据**经模型真落库**（§B2 侧 mimeData→dropMimeData
 *        全链）＋ **经视图 XDropEvent 链路的真落库 round-trip**（含 0x00
 *        的 wire 载荷经视图 drop 事件→模型落库，中途无截断；2026-11-15
 *        差异②收口翻正）＋ Move 动作真可达（B6R 返工③虚槽化）。
 *
 *        分段（为什么这么分，见段内注释）：
 *        A. **模型侧真落数据**：按 §B2 wire 格式（XDataStream 大端 int32
 *           序列）编码一行 → XMimeData → XAbstractItemModel_dropMimeData
 *           → 行数 +1、落点数据到位。drop 的「数据应用」以模型侧真接口
 *           先行验证解码/插入链。
 *        ②. **载荷通道收口断言**：同款含 0x00 的 wire 探针经 XByteArray
 *           全量保留（size/字节逐一对得上）；XString 通道对同款载荷仍
 *           截断（固有行为对照——历史缺口正是「载荷误走 XString 通道」）。
 *        B. **视图侧真端到端**：含 0x00 wire 载荷经 makeEventBytes 进
 *           XDropEvent（XByteArray）→ dragEnter/dragMove 门控放行（虚槽
 *           重载 Copy|Move）→ drop → 视图调 dropMimeData ⇒ 真落库
 *           （行数 2→3、落点 (2,0)=="moved"、事件 accepted）；空载荷
 *           对照：模型解不出数据 ⇒ 拒 ⇒ 视图显式 ignore、行数不变——
 *           「链路通、载荷通道二进制透明」双锁。 */
XQ_TEST(XViewMimeDrag, dropAppliesThroughModel)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XString* s0 = NULL;
    XAbstractItemView* v = NULL;
    XQ_REQUIRE(m != NULL);
    XAbstractItemModel_setDimension(m, 2, 2);
    s0 = XString_create_utf8("r0");
    XQ_REQUIRE(s0 != NULL);
    (void)XAbstractItemModel_setData(m, 0, 0, s0);
    XClassDelete((XClass*)s0);

    /* ---- A 段：wire 编码 → dropMimeData 真落库 ---- */
    {
        XByteArray* bytes = XByteArray_create();
        XDataStream ws;
        XMimeData* md;
        XString* moved = XString_create_utf8("moved");
        XQ_REQUIRE(bytes != NULL && moved != NULL);
        XDataStream_init_array(&ws, bytes);
        /* 与 XAbstractItemModel_mimeData 的编码逐字节同格式：
         * (row, col, 角色数, role, 字符串)。 */
        XDataStream_writeInt32(&ws, 0);   /* 源行 */
        XDataStream_writeInt32(&ws, 0);   /* 源列 */
        XDataStream_writeInt32(&ws, 1);   /* 角色数 */
        XDataStream_writeInt32(&ws, (int)XItemDataRole_DisplayRole);
        XDataStream_writeString(&ws, moved);
        XClassDelete((XClass*)moved);
        XClassDeinit((XClass*)&ws);
        md = XMimeData_create();
        XQ_REQUIRE(md != NULL);
        {
            const uint8_t* raw =
                (const uint8_t*)XByteArray_data(bytes);
            int len = (int)XVector_size_base((const XVector*)bytes);
            XMimeData_setData_bytes(md,
                "application/x-qabstractitemmodeldatalist", raw, len);
        }
        XClassDelete((XClass*)bytes);
        /* Copy 落到 row=1（模型缺省 supportedDropActions=Copy，动作合法）。 */
        XQ_CHECK_MSG(XAbstractItemModel_dropMimeData(m, md,
                        XPlatformDragAction_Copy, 1, 0, NULL),
                     "非空 wire 载荷必须真落库（§B2 解码/插入链）");
        XQ_EQ_I64(XAbstractItemModel_rowCount(m), 3);
        {
            const XString* cell = XAbstractItemModel_data(m, 1, 0);
            XQ_CHECK_PTR(cell);
            XQ_EQ_STR(cell ? XString_toUtf8(cell) : "", "moved");
        }
        {
            const XString* cell = XAbstractItemModel_data(m, 0, 0);
            XQ_CHECK_PTR(cell);
            XQ_EQ_STR(cell ? XString_toUtf8(cell) : "", "r0");
        }
        XClassDelete((XClass*)md);
    }

    /* ---- 差异②收口翻正（2026-11-15）：载荷通道二进制透明 ----
     * 历史缺口：XDropEvent 载荷曾是 XString，XChar_streamInputLenU8
     * （XChar.c:174-185）在首个 0x00 截断 ⇒ wire int32 大端首字节恒
     * 0x00 ⇒ 载荷必空、端到端不可达。现载荷已是 XByteArray：同款含
     * 0x00 的 wire 探针字节**全量保留**（size 逐字节不截断），而
     * XString 通道的截断是它自身的固有行为（对照保留，说明为何必须
     * 走 XByteArray）。 */
    {
        static const unsigned char wireProbe[] = {
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00
        };
        XByteArray* pb = XByteArray_create_with_data(
            (const char*)wireProbe, sizeof(wireProbe));
        XString* probe = XString_create_with_length_utf8(
            (const char*)wireProbe, sizeof(wireProbe));
        XQ_REQUIRE(pb != NULL && probe != NULL);
        XQ_CHECK_MSG((int)XVector_size_base((const XVector*)pb) ==
                         (int)sizeof(wireProbe),
                     "XByteArray 通道：含 0x00 wire 载荷全量保留"
                     "（差异②收口）");
        XQ_CHECK_MSG(XByteArray_constData(pb)[0] == 0x00 &&
                         XByteArray_constData(pb)[3] == 0x01,
                     "XByteArray 通道：首字节 0x00 与有效字节均原样");
        XQ_CHECK_MSG(XString_length_base((const XContainer*)probe) == 0,
                     "对照：XString 通道对同款载荷仍截断（固有行为，"
                     "即历史缺口根因——故载荷必须走 XByteArray）");
        XClassDelete((XClass*)pb);
        XClassDelete((XClass*)probe);
    }

    /* ---- B 段：视图侧 Move 可达（虚槽重载模型） ---- */
    v = XAbstractItemView_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XQ_REQUIRE(v != NULL);
    {
        XQDragModel dm;
        XDropEvent* ev;
        xqDragModel_init(&dm, 2, 2);
        /* ③ 锁：supported* 虚槽重载经 _base 分派可见（普通函数版不可达）。 */
        XQ_EQ_I64(XAbstractItemModel_supportedDropActions_base(
                      (XAbstractItemModel*)&dm),
                  XDropAction_CopyAction | XDropAction_MoveAction);
        XQ_EQ_I64(XAbstractItemModel_supportedDragActions_base(
                      (XAbstractItemModel*)&dm),
                  XDropAction_CopyAction | XDropAction_MoveAction);
        XAbstractItemView_setModel_base(v, (XAbstractItemModel*)&dm);
        XAbstractItemView_setDragDropMode(
            v, XAbstractItemViewDragDropMode_InternalMove);
        XWidget_setGeometry((XWidget*)v, 0, 0, 400, 200);
        /* InternalMove 下 dragEnter(dropAction=Move) 必须 accept——
         * 第一道门 possibleActions&Move 通过 + 第二道门 dropAction &
         * supportedDropActions（虚槽重载 Copy|Move）通过。 */
        ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_ENTER, 10, 10,
                                  "application/x-qabstractitemmodeldatalist",
                                  "", XDropAction_MoveAction,
                                  XDropAction_MoveAction |
                                      XDropAction_CopyAction);
        XQ_REQUIRE(ev != NULL);
        XEvent_ignore((XEvent*)ev);
        (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
        XQ_CHECK_MSG(XEvent_isAccepted((XEvent*)ev),
                     "虚槽重载 Copy|Move 后 InternalMove 的 dragEnter(Move) "
                     "必须放行（B6R 返工③前不可达）");
        XQ_EQ_I64(XAbstractItemView_state(v),
                  XAbstractItemViewDragState_DraggingState);
        XClassDelete((XClass*)ev);
        /* dragMove(Move) 落条目中部：OnItem + 矩形=条目本体（DropEnabled
         * 不降级）。 */
        ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_MOVE, 100, 36,
                                  "application/x-qabstractitemmodeldatalist",
                                  "", XDropAction_MoveAction,
                                  XDropAction_MoveAction |
                                      XDropAction_CopyAction);
        XQ_REQUIRE(ev != NULL);
        XEvent_ignore((XEvent*)ev);
        (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
        XQ_CHECK(XEvent_isAccepted((XEvent*)ev));
        XQ_EQ_I64(XAbstractItemView_dropIndicatorPosition(v),
                  XAbstractItemViewDropIndicatorPosition_OnItem);
        XClassDelete((XClass*)ev);
        /* drop(Move) 真端到端（2026-11-15 差异②收口翻正）：按 §B2 wire
         * 格式编码的载荷（首字节 0x00）经 XDropEvent（XByteArray）**原样**
         * 直达模型 dropMimeData ⇒ 真落库（行数+1、落点数据到位）；
         * Move 动作由虚槽重载放行（B6R 返工③）。 */
        XQ_REQUIRE(XAbstractItemModel_rowCount((XAbstractItemModel*)&dm) == 2);
        {
            XByteArray* bytes = XByteArray_create();
            XDataStream ws;
            XString* moved = XString_create_utf8("moved");
            XQ_REQUIRE(bytes != NULL && moved != NULL);
            XDataStream_init_array(&ws, bytes);
            /* 与 XAbstractItemModel_mimeData 的编码逐字节同格式。 */
            XDataStream_writeInt32(&ws, 0);   /* 源行 */
            XDataStream_writeInt32(&ws, 0);   /* 源列 */
            XDataStream_writeInt32(&ws, 1);   /* 角色数 */
            XDataStream_writeInt32(&ws, (int)XItemDataRole_DisplayRole);
            XDataStream_writeString(&ws, moved);
            XClassDelete((XClass*)moved);
            XClassDeinit((XClass*)&ws);
            {
                const uint8_t* raw =
                    (const uint8_t*)XByteArray_data(bytes);
                int len = (int)XVector_size_base((const XVector*)bytes);
                /* 前置：wire 首字节确为 0x00（历史缺口正是它）。 */
                XQ_REQUIRE(len > 0 && raw[0] == 0x00);
                ev = xqViewDrag_makeEventBytes(
                    XEVENT_TYPE_DROP, 100, 36,
                    "application/x-qabstractitemmodeldatalist",
                    raw, len, XDropAction_MoveAction,
                    XDropAction_MoveAction | XDropAction_CopyAction);
            }
            XClassDelete((XClass*)bytes);
        }
        XQ_REQUIRE(ev != NULL);
        (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
        XQ_CHECK_MSG(XEvent_isAccepted((XEvent*)ev),
                     "含 0x00 wire 载荷经视图 drop 真落库 ⇒ accept"
                     "（差异②收口端到端，中途无截断）");
        XQ_EQ_I64(XAbstractItemModel_rowCount((XAbstractItemModel*)&dm), 3);
        {
            const XString* cell = XAbstractItemModel_data(
                (XAbstractItemModel*)&dm, 2, 0);
            XQ_CHECK_PTR(cell);
            XQ_EQ_STR(cell ? XString_toUtf8(cell) : "", "moved");
        }
        XQ_EQ_I64(XAbstractItemView_state(v),
                  XAbstractItemViewDragState_NoState);
        XClassDelete((XClass*)ev);
        /* 空载荷对照（保留既有边界锁）：模型解不出数据 ⇒ 拒 ⇒ 显式
         * ignore、行数不再变。 */
        ev = xqViewDrag_makeEvent(XEVENT_TYPE_DROP, 100, 36,
                                  "application/x-qabstractitemmodeldatalist",
                                  "", XDropAction_MoveAction,
                                  XDropAction_MoveAction |
                                      XDropAction_CopyAction);
        XQ_REQUIRE(ev != NULL);
        (void)XCoreApplication_sendEvent((XObject*)v, (XEvent*)ev);
        XQ_CHECK_MSG(!XEvent_isAccepted((XEvent*)ev),
                     "空载荷 ⇒ 模型拒 ⇒ 视图必须显式 ignore（对照）");
        XQ_EQ_I64(XAbstractItemModel_rowCount((XAbstractItemModel*)&dm), 3);
        XClassDelete((XClass*)ev);
        /* 拆除顺序：先删视图（从 dm 摘信号槽），再 deinit dm。 */
        XClassDelete((XClass*)v);
        XClassDeinit((XClass*)&dm);
    }

    XClassDelete((XClass*)m);
}

/** @brief supportedDropActions/supportedDragActions 虚槽分派契约
 *        （2026-11-15 B6R 返工③的专项锁）：
 *        - 基类缺省：drop=Copy、drag=drop（qabstractitemmodel.cpp:
 *          2251-2257 / :2267-2269）；
 *        - NULL/无虚表守卫：回退 Copy；
 *        - 派生重载：XQDragModel 的 SupportedDropActions/SupportedDragActions
 *          槽经 _base 分派给出 Copy|Move（普通函数版无法重载 ⇒ 已提虚槽，
 *          Qt 侧两函数即 virtual，qabstractitemmodel.h:294/:298）；
 *        - 内部校验走虚槽：canDropMimeData 的动作校验在派生模型上放行
 *          Move（XAbstractItemModel.c 的动作门已改调 _base，B6R 返工）。 */
XQ_TEST(XViewMimeDrag, supportedActionsAreVirtualSlots)
{
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQDragModel dm;
    XQ_REQUIRE(m != NULL);
    xqDragModel_init(&dm, 2, 2);

    /* 基类缺省。 */
    XQ_EQ_I64(XAbstractItemModel_supportedDropActions_base(m),
              XPlatformDragAction_Copy);
    XQ_EQ_I64(XAbstractItemModel_supportedDragActions_base(m),
              XAbstractItemModel_supportedDropActions_base(m));
    /* NULL 守卫：回退 Copy（与旧普通函数版口径一致）。 */
    XQ_EQ_I64(XAbstractItemModel_supportedDropActions_base(NULL),
              XPlatformDragAction_Copy);
    XQ_EQ_I64(XAbstractItemModel_supportedDragActions_base(NULL),
              XPlatformDragAction_Copy);

    /* 派生重载经虚表分派可见。 */
    XQ_EQ_I64(XAbstractItemModel_supportedDropActions_base(
                  (XAbstractItemModel*)&dm),
              XPlatformDragAction_Copy | XPlatformDragAction_Move);
    XQ_EQ_I64(XAbstractItemModel_supportedDragActions_base(
                  (XAbstractItemModel*)&dm),
              XPlatformDragAction_Copy | XPlatformDragAction_Move);

    /* canDropMimeData 的动作校验走虚槽：派生模型上 Move 不再被拒。 */
    {
        XMimeData* md = XMimeData_create();
        XQ_REQUIRE(md != NULL);
        XMimeData_setData_bytes(md,
            "application/x-qabstractitemmodeldatalist",
            (const unsigned char*)"x", 1);
        XQ_CHECK_MSG(XAbstractItemModel_canDropMimeData(
                         (XAbstractItemModel*)&dm, md,
                         XPlatformDragAction_Move, 0, 0, NULL),
                     "派生模型 supportedDropActions 含 Move ⇒ "
                     "canDropMimeData 放行 Move（虚槽化后）");
        XQ_CHECK_MSG(!XAbstractItemModel_canDropMimeData(
                         m, md, XPlatformDragAction_Move, 0, 0, NULL),
                     "基类模型只 Copy ⇒ canDropMimeData 拒 Move（对照组）");
        XClassDelete((XClass*)md);
    }

    XClassDeinit((XClass*)&dm);
    XClassDelete((XClass*)m);
}

/** @brief K-51 定案锁（2026-11-15）：**程序化 edit 不受 editTriggers
 *        拦截**（对齐 Qt 6.8.0 实测）。Qt 依据：edit(index) 公共槽显式以
 *        AllEditTriggers 调三参版（qabstractitemview.cpp:1231-1238），
 *        shouldEdit 对 AllEditTriggers 直接 force editing（:4390-4391
 *        `if (trigger == AllEditTriggers) return true;`）——
 *        setEditTriggers(NoEditTriggers) 只关**交互触发**（双击/单击/
 *        按键路径按位与过滤后才进 edit），是否真开编辑器由 itemsEditable
 *        ∪ 模型 flags 决定。 */
XQ_TEST(XViewMimeDrag, programmaticEditIgnoresNoEditTriggers)
{
    XAbstractItemView* v = XAbstractItemView_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE, NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XQ_REQUIRE(v != NULL && m != NULL);
    XAbstractItemModel_setDimension(m, 2, 2);
    XAbstractItemView_setModel_base(v, m);

    /* 对照：只读模型（基类 flags 无 ItemIsEditable）+ 总闸关 ⇒ 不开。 */
    XAbstractItemView_setEditTriggers(
        v, XAbstractItemViewEditTrigger_NoEditTriggers);
    XQ_CHECK(!XAbstractItemView_edit(v, 0, 0));
    XQ_CHECK(!XAbstractItemView_isEditing(v));

    /* 总闸开启 ⇒ NoEditTriggers 下程序化 edit **仍真开编辑器**（K-51）。 */
    XAbstractItemView_setItemsEditable(v, true);
    XQ_CHECK_MSG(XAbstractItemView_edit(v, 0, 0) &&
                     XAbstractItemView_isEditing(v),
                 "NoEditTriggers 下程序化 edit 仍真开编辑器"
                 "（K-51 对齐 Qt：triggers 只管交互触发）");
    /* 同格重复 edit 幂等 true（同 Qt 重复 edit 语义）。 */
    XQ_CHECK(XAbstractItemView_edit(v, 0, 0));
    XAbstractItemView_reset(v);
    XQ_CHECK(!XAbstractItemView_isEditing(v));
    /* reset 不清总闸：NoEditTriggers 仍在 ⇒ 换格再次真开（门禁不拦）。 */
    XQ_CHECK(XAbstractItemView_edit(v, 1, 1));
    XQ_CHECK(XAbstractItemView_isEditing(v));
    XAbstractItemView_reset(v);

    XClassDelete((XClass*)v);
    XClassDelete((XClass*)m);
}

/** @brief 拖放指示线绘制接线锁（2026-11-15，⑤）：拖放中 &&
 *        showDropIndicator ⇒ dropIndicatorRect 按 2px 指示线**真实上屏**。
 *        Qt 依据：QTableView/QListView/QTreeView::paintEvent 尾段统一
 *        paintDropIndicator（qtableview.cpp:1687 / qlistview.cpp:2006 /
 *        qtreeview.cpp:1345 → qabstractitemview_p.h:164-176 四条件），
 *        CommonStyle PE_IndicatorItemViewItemDrop 对 H==0 矩形画横线
 *        （qcommonstyle.cpp:698-705）；任务书口径 2px 实心横线。
 *        数据层先锁 dropIndicatorRect 返回真值（与绘制同源字段），像素层
 *        grab 断言该带内出现 WindowText 色墨水；关闭 showDropIndicator
 *        对照：同带零墨水（死旋钮反向锁）。 */
XQ_TEST(XViewMimeDrag, dropIndicatorPaintedOnScreen)
{
    XTableView* tv = XTableView_create(NULL, 0);
    XAbstractItemModel* m = XAbstractItemModel_create();
    XAbstractItemView* v;
    XDropEvent* ev;
    XImage* shot;
    int dx;
    int dy;
    int dw;
    int dh;
    int hits;
    XQ_REQUIRE(tv != NULL && m != NULL);
    v = &tv->m_base;
    XAbstractItemModel_setDimension(m, 4, 2);
    XAbstractItemView_setModel_base(v, m);
    XAbstractItemView_setDragDropMode(
        v, XAbstractItemViewDragDropMode_DragDrop);
    XWidget_setGeometry((XWidget*)tv, 0, 0, 300, 200);
    XAbstractItemView_setDropIndicatorShown(v, true);

    /* dragEnter(Copy)：mime 命中 + 动作受基类模型 supported(Copy) 允许
     * ⇒ accept + DraggingState。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_ENTER, 60, 30,
                              "application/x-qabstractitemmodeldatalist",
                              "", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)tv, (XEvent*)ev);
    XQ_CHECK(XEvent_isAccepted((XEvent*)ev));
    XQ_EQ_I64(XAbstractItemView_state(v),
              XAbstractItemViewDragState_DraggingState);
    XClassDelete((XClass*)ev);
    /* dragMove(Copy) 落首行条目上缘（AboveItem 落点 ⇒ 横线矩形）：
     * canDrop（dropOn + 基类 canDropMimeData）通过 ⇒ accept 并缓存矩形。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_MOVE, 60, 30,
                              "application/x-qabstractitemmodeldatalist",
                              "", XDropAction_CopyAction,
                              XDropAction_CopyAction);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)tv, (XEvent*)ev);
    XQ_CHECK(XEvent_isAccepted((XEvent*)ev));
    /* 数据层：矩形真实缓存且与 paintEvent 同源（h==0 横线形态）。 */
    XQ_CHECK_MSG(XAbstractItemView_dropIndicatorRect(v, &dx, &dy, &dw, &dh),
                 "dragMove 命中条目 ⇒ dropIndicatorRect 真值");
    XQ_CHECK(dw > 0 && dh == 0);
    XClassDelete((XClass*)ev);
    /* 像素层：grab 真驱动 paintEvent，横线带 (y-1..y+1) 应有墨水
     * （色值取实测调色板 WindowText，与绘制同源）。 */
    shot = XWidget_grab((XWidget*)tv);
    XQ_REQUIRE(shot != NULL);
    {
        int wr;
        int wg;
        int wb;
        xqvd_windowTextRgb(v, &wr, &wg, &wb);
        hits = xqvd_colorHits(shot, dx, dy - 1, dx + dw, dy + 1,
                              wr, wg, wb, 40);
        XQ_CHECK_MSG(hits > 0,
                     "拖放态指示线真实上屏（横线带 (y-1..y+1) 命中 %d px）",
                     hits);
    }
    XClassDelete((XClass*)shot);
    /* 对照：关掉 showDropIndicator ⇒ 同带零墨水（不留残线）。 */
    XAbstractItemView_setDropIndicatorShown(v, false);
    shot = XWidget_grab((XWidget*)tv);
    XQ_REQUIRE(shot != NULL);
    {
        int wr;
        int wg;
        int wb;
        xqvd_windowTextRgb(v, &wr, &wg, &wb);
        XQ_EQ_I64(xqvd_colorHits(shot, dx, dy - 1, dx + dw, dy + 1,
                                 wr, wg, wb, 40), 0);
    }
    XClassDelete((XClass*)shot);
    /* 拖放序列收尾（dragLeave ⇒ stopAutoScroll + 状态复位；对齐真实
     * 拖放生命周期——不留挂起的自动滚动定时器给视图析构兜底）。 */
    ev = xqViewDrag_makeEvent(XEVENT_TYPE_DRAG_LEAVE, 0, 0, "", "",
                              XDropAction_IgnoreAction, 0);
    XQ_REQUIRE(ev != NULL);
    (void)XCoreApplication_sendEvent((XObject*)tv, (XEvent*)ev);
    XClassDelete((XClass*)ev);
    XQ_EQ_I64(XAbstractItemView_state(v),
              XAbstractItemViewDragState_NoState);

    XClassDelete((XClass*)tv);
    XClassDelete((XClass*)m);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON && XWINDOWEVENT_ON */
