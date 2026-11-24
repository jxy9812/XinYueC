/**
 * @file       XHeaderView.c
 * @brief      XHeaderView 表头视图实现。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XHeaderView.h"
/* 保护接口按约束显式 include（XTabBar.c 同款约定）：本文件同时是
 * XHeaderView_Protected.h 四个入口的定义点，包含即做签名一致性核对。 */
#include "XHeaderView_Protected.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XAbstractItemModel.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XMap.h"
#include "XEvent.h"
#include "XDateTime.h"
#include "XEventType.h"
#include "XCursor.h"
#include "XPainter.h"        /* §B3 paintSection 自绘入口（画家 API）。 */
#include "XImage.h"          /* §B3 paintEvent 取控件离屏画布。 */
#include "XWindowEvent.h"    /* §B3 XPaintEvent_rect（PAINT 区域裁剪）。 */
#if XSTYLE_ON
#include "XStyle.h"          /* §B3 样式分派（drawControl/sizeFromContents）。 */
#include "XStyleOption.h"    /* §B3 构造 XStyleOptionHeader。 */
#endif

#if XWIDGET_ON && XTABLEWIDGET_ON

/* xheaderview_updateSizeHint 调 XWidget_setSizeHint/setMinimumSizeHint：
 * 保护接口按约束必须显式 include，不得依赖传递（XTabBar.c 同款约定）。 */
#include "XWidget_Protected.h"

static void xhv_modelHeaderDataChangedSlot(XObject* receiver, XVarList* args);

/** @brief 区间尺寸硬上限（对标 Qt qheaderview.cpp 的 maxSizeSection=1048575）。 */
#define XHEADERVIEW_SECTION_SIZE_LIMIT 1048575
/** @brief 最小区间尺寸缺省值（水平/垂直）。
 *
 *  [2026-11-11 对齐] 原为单值 20。本机 Qt 6.8.0（QT_QPA_PLATFORM=offscreen）
 *  实测 QHeaderView 缺省：水平头 minimumSectionSize()==40、垂直头 ==21
 *  （QTableView/QTableWidget 的自建表头同为 40/21）。无样式/字体测量时
 *  按**方向**取实测常量。 */
#define XHEADERVIEW_DEFAULT_MINIMUM_SECTION_SIZE_H 40
#define XHEADERVIEW_DEFAULT_MINIMUM_SECTION_SIZE_V 21
/** @brief 默认区间尺寸缺省值（水平/垂直；resetDefaultSectionSize 复位点）。
 *
 *  [2026-11-11 对齐] 原为单值 30。本机 Qt 6.8.0 实测
 *  QHeaderView(Qt::Horizontal)::defaultSectionSize()==100、
 *  QHeaderView(Qt::Vertical)::defaultSectionSize()==30；QTableView /
 *  QTableWidget 的自建表头同为 100/30。 */
#define XHEADERVIEW_DEFAULT_SECTION_SIZE_H 100
#define XHEADERVIEW_DEFAULT_SECTION_SIZE_V 30
/** @brief ResizeToContents 测算精度缺省值（对标 Qt 缺省 1000）。 */
#define XHEADERVIEW_DEFAULT_RESIZE_CONTENTS_PRECISION 1000
/** @brief 分隔线手柄命中热区半径（第八轮②；任务口径 ±3px，对标 Qt
 *         PM_HeaderGripMargin=4 的样式近似，qcommonstyle.cpp:4766）。 */
#define XHEADERVIEW_HANDLE_HIT_MARGIN 3

/** @brief 按方向取默认段尺寸缺省值。
 *  @param orientation 0=水平 1=垂直。
 *  @return 像素值（水平 100 / 垂直 30；Qt 6.8.0 实测）。 */
static int xhv_defaultSectionSizeFor(int orientation)
{
    return orientation == 1 ? XHEADERVIEW_DEFAULT_SECTION_SIZE_V
                            : XHEADERVIEW_DEFAULT_SECTION_SIZE_H;
}

/** @brief 按方向取最小段尺寸缺省值。
 *  @param orientation 0=水平 1=垂直。
 *  @return 像素值（水平 40 / 垂直 21；Qt 6.8.0 实测）。 */
static int xhv_defaultMinimumSectionSizeFor(int orientation)
{
    return orientation == 1 ? XHEADERVIEW_DEFAULT_MINIMUM_SECTION_SIZE_V
                            : XHEADERVIEW_DEFAULT_MINIMUM_SECTION_SIZE_H;
}

/** @brief 发射 int 信号（载荷单值）。 */
static void xhv_emitInt(XHeaderView* self, size_t signal, int value)
{
    XVarList* arguments;
    /* [NULL 调用＝取信号地址，不发射 2026-11-09] XObject_signalIdOf 的
     * 约定：以 NULL 调用信号函数取其返回值（真实地址）。故发射前必须判
     * self——否则「连信号」这一步（XSignal(...) 展开即调用）就会拿 NULL
     * 解引用崩掉（实测：客户端启动即段错误，栈 xhv_emitInt ←
     * XHeaderView_sectionDoubleClicked_signal ← XObject_signalIdOf）。
     * 其余发射助手同理。 */
    if (!self) return;
    arguments = XVarList_Create(XVar(int, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射 bool 信号（载荷单值，对标 sortIndicatorClearableChanged）。 */
static void xhv_emitBool(XHeaderView* self, size_t signal, bool value)
{
    XVarList* arguments;
    if (!self) return;   /* NULL 调用＝取信号地址（见 xhv_emitInt 注）。 */
    arguments = XVarList_Create(XVar(bool, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射双 int 信号（载荷两值，对标 sectionCountChanged）。 */
static void xhv_emitInt2(XHeaderView* self, size_t signal, int a, int b)
{
    XVarList* arguments;
    if (!self) return;   /* NULL 调用＝取信号地址（见 xhv_emitInt 注）。 */
    arguments = XVarList_Create(XVar(int, a), XVar(int, b));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射三 int 信号（载荷三值，对标 sectionMoved/sectionResized）。 */
static void xhv_emitInt3(XHeaderView* self, size_t signal, int a, int b, int c)
{
    XVarList* arguments = XVarList_Create(XVar(int, a), XVar(int, b),
                                          XVar(int, c));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射无载荷信号（对标 geometriesChanged）。 */
static void xhv_emitVoid(XHeaderView* self, size_t signal)
{
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, NULL, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    }
}

/* ==================== 鼠标点击链（sectionPressed/sectionClicked） ==================== */

/* 头文件无按压段字段的替代承载：表头指针 → 按压段逻辑号 的映射旁表
 * （库内 XMap 指针键承载）。键随首次按压创建、随表头析构移除
 * （VXHeaderView_deinit 为全路径唯一析构入口），无泄漏与悬垂键。 */

/** @brief XMap 指针键比较回调：按表头地址定序（XCompare 约定 -1/0/1）。 */
static int32_t xhv_viewKeyCompare(const void* lhs, const void* rhs)
{
    const XHeaderView* const* l = (const XHeaderView* const*)lhs;
    const XHeaderView* const* r = (const XHeaderView* const*)rhs;
    if (*l == *r) return 0;
    return (*l < *r) ? -1 : 1;
}

static XMap* xhv_pressMap = NULL;

/** @brief 读表头的按压段；无记录返回 -1。 */
static int xhv_pressGet(const XHeaderView* view)
{
    const XHeaderView* key = view;
    const int* pressed;
    if (!view || !xhv_pressMap) return -1;
    pressed = (const int*)XMap_value_base((XMapBase*)xhv_pressMap, &key);
    return pressed ? *pressed : -1;
}

/** @brief 写表头的按压段（无记录则建键；映射创建失败忽略——
 *         释放路径按 -1 基准收敛，最多不发射 sectionClicked）。 */
static void xhv_pressSet(const XHeaderView* view, int section)
{
    const XHeaderView* key = view;
    int value = section;
    int* slot;
    if (!view) return;
    if (!xhv_pressMap) {
        xhv_pressMap = XMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                      sizeof(XHeaderView*), sizeof(int),
                                      xhv_viewKeyCompare, false);
        if (!xhv_pressMap) return;
    }
    slot = (int*)XMap_value_base((XMapBase*)xhv_pressMap, &key);
    if (slot) {
        *slot = section;
        return;
    }
    XMap_insert_base((XMapBase*)xhv_pressMap, &key, &value);
}

/** @brief 移除表头的按压记录（析构路径）。 */
static void xhv_pressRelease(const XHeaderView* view)
{
    const XHeaderView* key = view;
    if (!view || !xhv_pressMap) return;
    XMap_remove_base((XMapBase*)xhv_pressMap, &key);
}

/* ==================== 分隔线拖拽调宽手势（第八轮②） ==================== */

/* 头文件无手势字段承载：与按压段旁表同款「表头指针 → 手势态」映射
 * 旁表（库内 XMap 指针键承载；键随手势创建、随表头析构移除，无泄漏
 * 与悬垂键）。对标 Qt QHeaderViewPrivate 的 state=ResizeSection +
 * d->section + d->firstPos + d->originalSize 四元态
 * （qheaderview_p.h）。 */
typedef struct XhvResizeState
{
    bool m_active;      /**< 手势进行中（state==ResizeSection）。 */
    int m_section;      /**< 拖拽命中的段逻辑号。 */
    int m_pressPos;     /**< 按下点沿轴位置（d->firstPos）。 */
    int m_originalSize; /**< 按下时段宽（d->originalSize）。 */
    /* [K-74 2026-11-15] 级联路径的 d->lastPos（qheaderview.cpp:2571
     * `pos - d->lastPos`）：每拍移动后更新，级联 delta 按「相对上一拍」
     * 算而非相对按下点。**尾部追加**，前面四个字段的写入顺序不变。 */
    int m_lastPos;      /**< 上一拍沿轴位置（d->lastPos）。 */
} XhvResizeState;

static XMap* xhv_resizeMap = NULL;

/** @brief 读表头的手势态；无记录返回 NULL。 */
static XhvResizeState* xhv_resizeGet(const XHeaderView* view)
{
    const XHeaderView* key = view;
    if (!view || !xhv_resizeMap) return NULL;
    return (XhvResizeState*)XMap_value_base((XMapBase*)xhv_resizeMap, &key);
}

/** @brief 写表头的手势态（无记录则建键；映射创建失败忽略——
 *         最坏退化为该表头本次不可拖拽调宽）。 */
static void xhv_resizeSet(const XHeaderView* view, bool active, int section,
                          int pressPos, int originalSize)
{
    const XHeaderView* key = view;
    XhvResizeState state;
    XhvResizeState* slot;
    if (!view) return;
    if (!xhv_resizeMap) {
        xhv_resizeMap = XMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       sizeof(XHeaderView*),
                                       sizeof(XhvResizeState),
                                       xhv_viewKeyCompare, false);
        if (!xhv_resizeMap) return;
    }
    slot = (XhvResizeState*)XMap_value_base((XMapBase*)xhv_resizeMap, &key);
    if (slot) {
        slot->m_active = active;
        slot->m_section = section;
        slot->m_pressPos = pressPos;
        slot->m_originalSize = originalSize;
        slot->m_lastPos = pressPos;
        return;
    }
    state.m_active = active;
    state.m_section = section;
    state.m_pressPos = pressPos;
    state.m_originalSize = originalSize;
    state.m_lastPos = pressPos;
    XMap_insert_base((XMapBase*)xhv_resizeMap, &key, &state);
}

/** @brief 移除表头的手势记录（析构路径）。 */
static void xhv_resizeRelease(const XHeaderView* view)
{
    const XHeaderView* key = view;
    if (!view || !xhv_resizeMap) return;
    XMap_remove_base((XMapBase*)xhv_resizeMap, &key);
}

/* ==================== 段拖拽换位手势（K-68 2026-11-14） ==================== */

/* 头文件无手势字段承载：与前两套「表头指针 → 手势态」映射旁表同款
 * （键随手势创建、随表头析构移除，无泄漏与悬垂键）。对标 Qt
 * QHeaderViewPrivate 的 state=MoveSection + d->section + d->firstPos +
 * d->target 四元态（qheaderview.cpp:2518-2528 建立手势、:2580-2617
 * 移动机算 target、:2673-2682 释放提交 moveSection）。 */
typedef struct XhvMoveState
{
    bool m_active;      /**< 手势进行中（Qt state==MoveSection）。 */
    bool m_dragged;     /**< 已超过拖动阈值（对标 Qt 段指示器可见，:2590-2592）。 */
    int  m_section;     /**< 拖拽命中的段逻辑号（d->section）。 */
    int  m_pressPos;    /**< 按下点沿轴位置（d->firstPos）。 */
    int  m_target;      /**< 机算出的目标段逻辑号（d->target；-1=未成）。 */
} XhvMoveState;

static XMap* xhv_moveMap = NULL;

/** @brief 读表头的换位手势态；无记录返回 NULL。 */
static XhvMoveState* xhv_moveGet(const XHeaderView* view)
{
    const XHeaderView* key = view;
    if (!view || !xhv_moveMap) return NULL;
    return (XhvMoveState*)XMap_value_base((XMapBase*)xhv_moveMap, &key);
}

/** @brief 写表头的换位手势态（无记录则建键；映射创建失败忽略——
 *         最坏退化为该表头本次不可拖拽换位）。 */
static void xhv_moveSet(const XHeaderView* view, bool active, bool dragged,
                        int section, int pressPos, int target)
{
    const XHeaderView* key = view;
    XhvMoveState state;
    XhvMoveState* slot;
    if (!view) return;
    if (!xhv_moveMap) {
        xhv_moveMap = XMap_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     sizeof(XHeaderView*),
                                     sizeof(XhvMoveState),
                                     xhv_viewKeyCompare, false);
        if (!xhv_moveMap) return;
    }
    slot = (XhvMoveState*)XMap_value_base((XMapBase*)xhv_moveMap, &key);
    if (slot) {
        slot->m_active = active;
        slot->m_dragged = dragged;
        slot->m_section = section;
        slot->m_pressPos = pressPos;
        slot->m_target = target;
        return;
    }
    state.m_active = active;
    state.m_dragged = dragged;
    state.m_section = section;
    state.m_pressPos = pressPos;
    state.m_target = target;
    XMap_insert_base((XMapBase*)xhv_moveMap, &key, &state);
}

/** @brief 移除表头的换位手势记录（析构路径）。 */
static void xhv_moveRelease(const XHeaderView* view)
{
    const XHeaderView* key = view;
    if (!view || !xhv_moveMap) return;
    XMap_remove_base((XMapBase*)xhv_moveMap, &key);
}

/** @brief 拖动阈值（对标 QApplication::startDragDistance 缺省 10px，
 *         qheaderview.cpp:2589 qAbs(pos - d->firstPos) >= startDragDistance）。 */
#define XHEADERVIEW_DRAG_DISTANCE 10

/* K-73 stretchLastSection 引擎（定义在段族之后；触发点分散在
 * resizeSection/hideSection/showSection/setCount/setter 与视口长推送，
 * 故此处前置声明）。 */
static void xhv_stretchMaybe(XHeaderView* self);

static void VXHeaderView_mousePressEvent(XWidget* self, XEvent* event);
static void VXHeaderView_mouseMoveEvent(XWidget* self, XEvent* event);
static void VXHeaderView_mouseReleaseEvent(XWidget* self, XEvent* event);
static void VXHeaderView_mouseDoubleClickEvent(XWidget* self, XEvent* event);

/* [K-74 2026-11-15] 级联调整快照表（对标 QHeaderViewPrivate::
 * cascadingSectionSize，qheaderview_p.h:246）。按视觉序号索引的 int 表：
 * -1=该段无快照（未因级联被收缩），>=0=被收缩前的原宽。懒同步到 m_count，
 * 长度不足时补 -1；表指针初始 NULL（无级联需求则不分配）。 */

/** @brief 确保级联快照表存在且覆盖 m_count 个视觉槽（不足补 -1）。 */
static XVector* xhv_cascadeTable(XHeaderView* self)
{
    int cur;
    int i;
    int sentinel = -1;
    if (!self) return NULL;
    if (!self->m_cascadingSizes) {
        self->m_cascadingSizes = XVector_Create(int);
        if (!self->m_cascadingSizes) return NULL;
    }
    cur = (int)XVector_size_base(
        (const XContainer*)self->m_cascadingSizes);
    for (i = cur; i < self->m_count; ++i)
        XVector_push_back_1_base(self->m_cascadingSizes, &sentinel);
    return self->m_cascadingSizes;
}

/** @brief 读某视觉段的快照原宽；无记录/越界返回 -1。 */
static int xhv_cascadeOriginal(const XHeaderView* self, int visual)
{
    int n;
    if (!self || !self->m_cascadingSizes || visual < 0) return -1;
    n = (int)XVector_size_base(
        (const XContainer*)self->m_cascadingSizes);
    if (visual >= n) return -1;
    return XVector_At_Base(self->m_cascadingSizes, (int64_t)visual, int);
}

/** @brief 记某视觉段被级联收缩前的原宽（只记首次，对标
 *         saveCascadingSectionSize「contains 则不覆盖」）。 */
static void xhv_cascadeSave(XHeaderView* self, int visual, int size)
{
    XVector* table = xhv_cascadeTable(self);
    int n;
    if (!table || visual < 0) return;
    n = (int)XVector_size_base((const XContainer*)table);
    if (visual >= n) return;
    if (XVector_At_Base(table, (int64_t)visual, int) == -1)
        XVector_At_Base(table, (int64_t)visual, int) = size;
}

/** @brief 段是否可级联（对标 Qt sectionIsCascadable：生效模式
 *         == Interactive，qheaderview_p.h:201-203）。 */
static bool xhv_cascadable(const XHeaderView* self, int visual)
{
    int logical = XHeaderView_logicalIndex(self, visual);
    if (logical < 0) return false;
    return XHeaderView_sectionResizeModeAt(self, logical) ==
        (int)XHeaderViewResizeMode_Interactive;
}

/** @brief 隐藏段在级联链里跳过（对标 Qt isVisualIndexHidden 门）。 */
static bool xhv_cascadeHidden(const XHeaderView* self, int visual)
{
    int logical = XHeaderView_logicalIndex(self, visual);
    return logical < 0 || XHeaderView_isSectionHidden(self, logical);
}

/** @brief 单段尺寸改写（级联路径统一走 setSectionSize 直写，绕开
 *         resizeSection 的末段快照/引擎重排钩——对标 Qt
 *         resizeSectionItem→createSectionItems 不经 resizeSection）。 */
static void xhv_cascadeApply(XHeaderView* self, int visual, int newSize)
{
    int logical = XHeaderView_logicalIndex(self, visual);
    int old;
    if (logical < 0) return;
    old = XHeaderView_sectionSize(self, logical);
    if (old != newSize)
        XHeaderView_setSectionSize(self, logical, newSize);
}

/**
 * @brief 交互式拖拽的级联调整（K-74；对标
 *        QHeaderViewPrivate::cascadingResize，qheaderview.cpp:3737-3842）。
 *
 *        visual = 被拖拽段的视觉序号，newSize = 拖拽目标尺寸（已由调用方
 *        按 [min,max] 钳制）。delta>0（拉大）时：先尝试把空间归还给此前
 *        被本段级联收缩过的段（restore old sizes），否则直接改本段；再把
 *        「多占的 delta」向后续可见 Interactive 段逐一收缩（每段最多压到
 *        minimumSectionSize，记快照），delta 分尽即停。delta<0（缩回）时：
 *        优先把空间归还曾收缩过的后续段，再改本段；若目标尺寸已触底
 *        minimumSectionSize，则向前一段借空间；最后把本段让出的空间给
 *        下一个可级联段。关闭级联（m_cascadingResizes=false）时不调用本
 *        函数，鼠标移动路径退化为普通 resizeSection（旧行为）。
 */
static void xhv_cascadeResize(XHeaderView* self, int visual, int newSize)
{
    int minSize;
    int oldSize;
    int delta;
    int i;
    bool restored = false;
    if (!self || visual < 0 || visual >= self->m_count) return;
    minSize = self->m_minimumSectionSize;
    oldSize = XHeaderView_sectionSize(self,
                                      XHeaderView_logicalIndex(self, visual));
    if (oldSize < 0) return;
    delta = newSize - oldSize;
    if (delta == 0) return;
    if (delta > 0) {
        /* ① restore old section sizes：此前被本段压过的段优先回吃。 */
        for (i = 0; i < visual; ++i) {
            int orig = xhv_cascadeOriginal(self, i);
            int cur;
            if (orig < 0) continue;
            cur = XHeaderView_sectionSize(self,
                        XHeaderView_logicalIndex(self, i));
            if (cur < 0) continue;
            if (cur < orig) {
                int target = cur + delta;
                if (target > orig) target = orig;
                xhv_cascadeApply(self, i, target);
                restored = true;
                break;
            }
        }
        /* ② 改本段（无归还对象时）。 */
        if (!restored) {
            int target = newSize < minSize ? minSize : newSize;
            xhv_cascadeApply(self, visual, target);
        }
        /* ③ 向后续可见可级联段逐一收缩让出 delta。 */
        {
            int remain = delta;
            for (i = visual + 1; i < self->m_count && remain > 0; ++i) {
                int cur;
                int target;
                if (xhv_cascadeHidden(self, i) || !xhv_cascadable(self, i))
                    continue;
                cur = XHeaderView_sectionSize(self,
                          XHeaderView_logicalIndex(self, i));
                if (cur < 0 || cur <= minSize) continue;
                target = cur - remain;
                if (target < minSize) target = minSize;
                xhv_cascadeSave(self, i, cur);
                xhv_cascadeApply(self, i, target);
                remain -= (cur - target);
            }
        }
    } else {
        /* ① restore old section sizes：把空间还给曾收缩的后续段。 */
        for (i = self->m_count - 1; i > visual; --i) {
            int orig = xhv_cascadeOriginal(self, i);
            int cur;
            int target;
            if (orig < 0) continue;
            cur = XHeaderView_sectionSize(self,
                      XHeaderView_logicalIndex(self, i));
            if (cur < 0 || cur >= orig) continue;
            target = cur - delta;   /* delta<0 ⇒ 加回空间 */
            if (target > orig) target = orig;
            xhv_cascadeApply(self, i, target);
            restored = true;
            break;
        }
        /* ② 改本段（钳到 >=min）。 */
        {
            int target = newSize < minSize ? minSize : newSize;
            xhv_cascadeApply(self, visual, target);
        }
        /* ③ 目标触底 ⇒ 向前一段借空间（Qt :3811-3824）。 */
        if (delta < 0 && newSize < minSize) {
            for (i = visual - 1; i >= 0; --i) {
                int cur;
                int target;
                if (xhv_cascadeHidden(self, i) || !xhv_cascadable(self, i))
                    continue;
                cur = XHeaderView_sectionSize(self,
                          XHeaderView_logicalIndex(self, i));
                if (cur < 0 || cur <= minSize) continue;
                target = cur + delta;   /* delta<0 ⇒ 收缩前段 */
                if (target < minSize) target = minSize;
                xhv_cascadeSave(self, i, cur);
                xhv_cascadeApply(self, i, target);
                break;
            }
        }
        /* ④ 未归还旧段 ⇒ 把本段让出的空间给下一个可级联段
         * （Qt :3828-3840「let the next section get the space」）。 */
        if (!restored) {
            for (i = visual + 1; i < self->m_count; ++i) {
                int cur;
                int target;
                if (xhv_cascadeHidden(self, i) || !xhv_cascadable(self, i))
                    continue;
                cur = XHeaderView_sectionSize(self,
                          XHeaderView_logicalIndex(self, i));
                if (cur < 0) continue;
                target = cur - delta;   /* delta<0 ⇒ 放大后段 */
                xhv_cascadeApply(self, i, target);
                break;
            }
        }
    }
}

/* 对标 Qt QHeaderView::mousePressEvent（qheaderview.cpp:2505）：先查
 * 分隔线手柄（sectionHandleAt，qheaderview.cpp:2511）——命中且该段
 * Interactive 时进入 ResizeSection 手势（记段/按下点/原宽，抓取鼠标，
 * 不发 sectionPressed，qheaderview.cpp:2523-2528）；未命中手柄才走
 * 既有按压链（clickableSections 时发射 sectionPressed）。 */
static void VXHeaderView_mousePressEvent(XWidget* self, XEvent* event)
{
    XHeaderView* header = (XHeaderView*)self;
    XMouseEvent* me;
    XPoint pos;
    int position;
    int section;
    int handle;
    if (!header || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    pos = XMouseEvent_position(me);
    position = (header->m_orientation == 0) ? pos.x : pos.y;
    handle = XHeaderView_resizeHandleSectionAt(header, position);
    /* [K-74 2026-11-15] 对标 Qt mousePressEvent 起手势前的
     * clearCascadingSections（qheaderview.cpp:2543）：每次新手势清掉
     * 上一轮的级联快照，避免旧快照把新拖拽的归还链带偏。 */
    if (header->m_cascadingSizes)
        XVector_clear_base(header->m_cascadingSizes);
    if (handle >= 0 &&
        XHeaderView_sectionResizeModeAt(header, handle) ==
            (int)XHeaderViewResizeMode_Interactive) {
        /* 对标 qheaderview.cpp:2524-2526：originalSize=sectionSize(handle)、
         * state=ResizeSection；拖拽起点即当前点。 */
        xhv_resizeSet(header, true, handle, position,
                      XHeaderView_sectionSize(header, handle));
        XWidget_grabMouse(self);
        XEvent_accept(event);
        return;
    }
    /* 手柄命中但模式非 Interactive：同 Qt 不进手势也不发点击。 */
    if (handle >= 0) {
        XEvent_accept(event);
        return;
    }
    section = XHeaderView_logicalIndexAt(header, position);
    xhv_pressSet(header, section);
    if (section >= 0 && header->m_sectionsClickable)
        XHeaderView_sectionPressed_signal(header, section);
    /* [K-68 段拖拽换位 2026-11-14] 对标 Qt mousePressEvent 的 MoveSection
     * 预备（qheaderview.cpp:2518-2528）：门控 sectionsMovable && 非隐藏段
     * （logicalIndexAt 按可见几何累加，隐藏段尺寸 0 不被命中）&&
     * （firstSectionMovable || 非首段——Qt 以 pressed==0 && !allowUserMoveOf
     * Section0 拒树结首段）。Qt 在 MoveSection 态下不拦 sectionPressed 的
     * 发射（:2515-2516 先发 pressed），释放未拖动时回落单击链
     * （:2675-2682 经 SelectSections fallthrough），故此处不 return。 */
    if (section >= 0 && header->m_sectionsMovable &&
        (section != 0 || header->m_firstSectionMovable))
        xhv_moveSet(header, true, false, section, position, -1);
    /* [双击段 2026-11-09 接入 / 2026-11-10 定位保留] sectionDoubleClicked
     * 的句柄此前**只声明没有发射点**（全仓无 XEmitSignal）⇒ 页面侧接了也
     * 永远收不到。本段是按压链上的 400ms 窗口判定——Qt 没有这条通道
     * （双击判定完全由窗口系统的 DBL 事件承担，见
     * VXHeaderView_mouseDoubleClickEvent），本库暂保留它：回归锁
     * hsf_regression_test 的 D1（同段 400ms 内第二次按压发射 + 三击不连发）
     * 锁着这条口径，且它兼容「后端不产 DBL 事件类型」的输入路径。真实
     * 平台序列（桌面 WM 双击消息 / 触摸 DoubleTap 合成，均为
     * PRESS→RELEASE→DBL→RELEASE）没有第二次 PRESS ⇒ 真实双击永远走
     * 下方 DBL 处理器，本段只在合成两连击下生效。 */
    if (section >= 0) {
        const uint64_t now = (uint64_t)XDateTime_currentMSecsSinceEpoch();
        if (header->m_lastDblSection == section &&
            header->m_lastDblPressMs != 0 && now >= header->m_lastDblPressMs &&
            (now - header->m_lastDblPressMs) <= 400u) {
            header->m_lastDblPressMs = 0;      /* 消耗掉：三击不连发 */
            header->m_lastDblSection = -1;
            XHeaderView_sectionDoubleClicked_signal(header, section);
            /* [盖章 2026-11-10 与 DBL 处理器同源] 这条窗口通道发出的双击
               同样记「刚发过双击」⇒ 释放处理器在窗口内同段让位（不再翻转
               指示器）——两通道一个口径。此前的盖章只在 DBL 事件处理器里，
               两连击路径排序后仍被本拍之后的 RELEASE 翻转跳升序。 */
            header->m_lastDblEmitMs =
                (uint64_t)XDateTime_currentMSecsSinceEpoch();
            header->m_lastDblEmitSection = section;
        } else {
            header->m_lastDblPressMs = now;
            header->m_lastDblSection = section;
        }
    }
    XEvent_accept(event);
}

/* 对标 Qt QHeaderView::mouseMoveEvent（qheaderview.cpp:2544）：手势中
 * 按 delta=pos-firstPos 实时 resizeSection（qBound 钳位，
 * qheaderview.cpp:2566-2577——sectionResized/geometriesChanged 由
 * resizeSection 统一发射，本库补 XWidget_update 即时重绘表头本体）；
 * 无手势时作分隔线光标提示（命中→SplitH/SplitV，离开→unset，
 * qheaderview.cpp:2636-2644 的 WA_SetCursor 差分同款）。 */
static void VXHeaderView_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XHeaderView* header = (XHeaderView*)self;
    XMouseEvent* me;
    XPoint pos;
    int position;
    XhvResizeState* gesture;
    if (!header || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE)
        return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    position = (header->m_orientation == 0) ? pos.x : pos.y;
    gesture = xhv_resizeGet(header);
    if (gesture && gesture->m_active) {
        /* [K-74 2026-11-15] 级联开启时走 Qt mouseMoveEvent 的
         * cascadingResizing 分支（qheaderview.cpp:2569-2577）：delta 按
         * 「本拍 − 上一拍」（d->lastPos）算、逐拍累加到段尺寸，再向后续
         * 段级联收缩/归还。关闭级联时保持旧口径（相对按下点一次钳制）。
         * @note **先把手势字段拷成局部量**：xhv_resizeGet 返回 XMap 的
         *       内部槽位指针，而 xhv_cascadeResize 会经 setSectionSize 发
         *       sectionResized/geometriesChanged ⇒ 视图侧槽可能触碰旁表
         *       （插入会 realloc 使槽指针悬垂）。动作完成后**重新查表**
         *       回写 m_lastPos（取不到就放弃回写——手势已被别处收尾）。 */
        const int gSection = gesture->m_section;
        const int gPressPos = gesture->m_pressPos;
        const int gOriginalSize = gesture->m_originalSize;
        const int gLastPos = gesture->m_lastPos;
        gesture = NULL;
        if (header->m_cascadingResizes) {
            int visual = XHeaderView_visualIndex(header, gSection);
            int delta = position - gLastPos;
            int cur = XHeaderView_sectionSize(header, gSection);
            int newSize = cur + delta;
            XhvResizeState* after;
            if (newSize < header->m_minimumSectionSize)
                newSize = header->m_minimumSectionSize;
            if (newSize > header->m_maximumSectionSize)
                newSize = header->m_maximumSectionSize;
            if (visual >= 0)
                xhv_cascadeResize(header, visual, newSize);
            after = xhv_resizeGet(header);   /* 动作后重新查表（防悬垂）。 */
            if (after && after->m_active) after->m_lastPos = position;
        } else {
            int delta = position - gPressPos;
            int newSize = gOriginalSize + delta;
            /* 对标 qheaderview.cpp:2575：
             * qBound(minimumSectionSize(), originalSize+delta, maximumSectionSize())。 */
            if (newSize < header->m_minimumSectionSize)
                newSize = header->m_minimumSectionSize;
            if (newSize > header->m_maximumSectionSize)
                newSize = header->m_maximumSectionSize;
            XHeaderView_resizeSection(header, gSection, newSize);
        }
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    /* [K-68 段拖拽换位 2026-11-14] 对标 Qt mouseMoveEvent 的 MoveSection
     * 分支（qheaderview.cpp:2580-2617）：未过拖动阈值且未起步时只保持
     * 预备态（Qt 按 qAbs(pos-firstPos) >= startDragDistance 或指示器可见
     * 才机算目标）；过阈值后按「目标视觉序的中点界」机算 target 段——
     * Qt 的 posThreshold = headerSectionPosition(visual) - headerOffset +
     * headerSectionSize(visual)/2（本库 K-64 已接线 offset，
     * XHeaderView_sectionViewportPosition 即 sectionPosition − offset，
     * 故阈值按视口位置算，见 XHeaderView_setOffset 的 @note），
     * checkPos=pos（本库无 RTL 镜像）。
     * 命中首视觉段且逻辑 0 仍居首且首段不可动时不成交（:2591-2593，
     * 树结列首段不许被拖过）。 */
    {
        XhvMoveState* moveGesture = xhv_moveGet(header);
        if (moveGesture && moveGesture->m_active) {
            const int distance = position - moveGesture->m_pressPos;
            int visual;
            int moving;
            int posThreshold;
            if (!moveGesture->m_dragged &&
                distance < XHEADERVIEW_DRAG_DISTANCE &&
                -distance < XHEADERVIEW_DRAG_DISTANCE) {
                XEvent_accept(event);
                return;
            }
            moveGesture->m_dragged = true;
            visual = XHeaderView_visualIndexAt(header, position);
            if (visual < 0) {                 /* 对标 :2584 visual == -1 ⇒ return */
                XEvent_accept(event);
                return;
            }
            if (visual == 0 &&
                XHeaderView_logicalIndex(header, 0) == 0 &&
                !header->m_firstSectionMovable) {
                XEvent_accept(event);
                return;
            }
            posThreshold =
                XHeaderView_sectionPosition(
                    header, XHeaderView_logicalIndex(header, visual)) +
                XHeaderView_sectionSize(
                    header, XHeaderView_logicalIndex(header, visual)) / 2;
            moving = XHeaderView_visualIndex(header, moveGesture->m_section);
            if (visual < moving)
                moveGesture->m_target = (position < posThreshold)
                    ? XHeaderView_logicalIndex(header, visual)
                    : XHeaderView_logicalIndex(header, visual + 1);
            else if (visual > moving)
                moveGesture->m_target = (position > posThreshold)
                    ? XHeaderView_logicalIndex(header, visual)
                    : XHeaderView_logicalIndex(header, visual - 1);
            else
                moveGesture->m_target = moveGesture->m_section;
            XEvent_accept(event);
            return;
        }
    }
    /* [悬停进入新段 ⇒ sectionEntered 2026-11-09 Qt 口径补全] */
    {
        const int sec = XHeaderView_logicalIndexAt(header, position);
        if (sec != header->m_lastEnteredSection) {
            header->m_lastEnteredSection = sec;
            if (sec >= 0)
                XHeaderView_sectionEntered_signal(header, sec);
        }
    }
#if XCURSOR_ON
    {
        /* 光标提示（NoState 分支，qheaderview.cpp:2636-2644）。
         * [P0-11 2026-11-12 核对] 水平头用 XCursor_SplitH、垂直头用
         * XCursor_SplitV，与 Qt SplitHCursor/SplitVCursor 一致；平台侧
         * 映射已在 2026-11-11 交换为 Qt 口径（posix
         * XPlatformNativeWindow_posix.c:1972-1974 SplitV→sb_v_double_arrow
         * /SplitH→sb_h_double_arrow；win32 同文件 SplitV→IDC_SIZENS/
         * SplitH→IDC_SIZEWE），故本处两端视觉均为正确方向。 */
        int handle = XHeaderView_resizeHandleSectionAt(header, position);
        bool hasCursor = XWidget_testAttribute(
            self, XWidgetAttribute_SetCursor);
        if (handle >= 0 &&
            XHeaderView_sectionResizeModeAt(header, handle) ==
                (int)XHeaderViewResizeMode_Interactive) {
            if (!hasCursor) {
                XCursor cursor;
                XCursor_init(&cursor);
                XCursor_setShape(&cursor, (header->m_orientation == 0)
                                              ? XCursor_SplitH
                                              : XCursor_SplitV);
                XWidget_setCursor(self, &cursor);
            }
        } else if (hasCursor) {
            XWidget_unsetCursor(self);
        }
    }
#else
    (void)position;
#endif /* XCURSOR_ON */
}

/* 对标 Qt QHeaderView::mouseReleaseEvent（qheaderview.cpp:2668）：手势
 * 中释放仅落定（state 复位、originalSize 清账，qheaderview.cpp:2726-
 * 2731），不发射 sectionClicked——分隔线拖拽与段点击（排序）语义互斥
 * 的边界；其余走既有点击链（clickable 且同段发射 sectionClicked）。 */
static void VXHeaderView_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XHeaderView* header = (XHeaderView*)self;
    XMouseEvent* me;
    XPoint pos;
    int position;
    int section;
    int pressed;
    XhvResizeState* gesture;
    if (!header || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    pos = XMouseEvent_position(me);
    position = (header->m_orientation == 0) ? pos.x : pos.y;
    gesture = xhv_resizeGet(header);
    if (gesture && gesture->m_active) {
        xhv_resizeSet(header, false, -1, 0, 0);
        /* [K-74 2026-11-15] 对标 Qt mouseReleaseEvent 的
         * clearCascadingSections（qheaderview.cpp:2728）：手势收尾清
         * 快照，下一轮拖拽从干净状态起算。 */
        if (header->m_cascadingSizes)
            XVector_clear_base(header->m_cascadingSizes);
        XWidget_releaseMouse(self);
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    /* [K-68 段拖拽换位 2026-11-14] 对标 Qt mouseReleaseEvent 的
     * MoveSection 分支（qheaderview.cpp:2673-2682）：已起步（Qt 判
     * 指示器可见）⇒ 提交 moveSection(visualIndex(section),
     * visualIndex(target)) 并**不走单击链**（break，不发 sectionClicked、
     * 不翻指示器）；未起步 ⇒ 经 SelectSections fallthrough 落下方单击链
     * ——同 Qt「按了没拖 = 单击」。 */
    {
        XhvMoveState* moveGesture = xhv_moveGet(header);
        if (moveGesture && moveGesture->m_active) {
            const bool moved = moveGesture->m_dragged;
            const int sectionNo = moveGesture->m_section;
            const int target = moveGesture->m_target;
            xhv_moveSet(header, false, false, -1, 0, -1);
            if (moved) {
                const int from = XHeaderView_visualIndex(header, sectionNo);
                const int to =
                    (target >= 0)
                        ? XHeaderView_visualIndex(header, target)
                        : -1;
                if (from >= 0 && to >= 0)
                    XHeaderView_moveSection(header, from, to);
                /* Qt 提交路径先 section = target = -1 再 break；按压账
                 * 一并清（单击链的 section == pressed 判定不再成立）。 */
                xhv_pressSet(header, -1);
                XWidget_update(self);
                XEvent_accept(event);
                return;
            }
            /* 未拖动：落下方单击链（Qt SelectSections fallthrough 口径）。 */
        }
    }
    section = XHeaderView_logicalIndexAt(header, position);
    pressed = xhv_pressGet(header);
    xhv_pressSet(header, -1);
    /* [双击窗口让位 2026-11-10] 同段 400ms 内刚发过 sectionDoubleClicked
     * ⇒ 本释放属于双击周期的收尾：应用侧的三态排序已在 DBL 里执行，
     * 这里再翻转指示器会与应用排序态错位（实测：双击降序后指示器跳升序）。
     * sectionClicked 照发（对齐 Qt：单击与双击信号在 Qt 里都发）。 */
    const uint64_t dblEmitMs = header->m_lastDblEmitMs;
    const int dblEmitSec = header->m_lastDblEmitSection;
    const int afterDbl =
        (dblEmitMs != 0 && dblEmitSec == section &&
         dblEmitMs + 400u >= (uint64_t)XDateTime_currentMSecsSinceEpoch());
    if (header->m_sectionsClickable && section >= 0 && section == pressed) {
        /* [Qt 口径补全 2026-11-09] 释放于按压段 ⇒ 翻转排序指示器（同段
         * 再点换向；点别的段切到该段升序），再发 sectionClicked——**单击
         * 口径**。双击周期内（afterDbl）不翻转：排序已由页面在 DBL 槽里
         * 三态推进，指示器由页面侧同步。 */
        if (!afterDbl) {
            const int curSec = header->m_sortIndicatorSection;
            const int curOrd = header->m_sortIndicatorOrder;
            const int newOrd = (curSec == section) ? (curOrd ? 0 : 1) : 0;
            XHeaderView_setSortIndicator(header, section, newOrd);
        }
        XHeaderView_sectionClicked_signal(header, section);
    }
    XEvent_accept(event);
}

/* 对标 Qt QHeaderView::mouseDoubleClickEvent（qheaderview.cpp:2741-2760）：
 * 双击落在 Interactive 模式的分隔线手柄 ⇒ 发射 sectionHandleDoubleClicked
 * （拖拽语义优先）；**其余一律发射 sectionDoubleClicked**——Qt 原文不看
 * sectionsClickable、也不要求命中有效段（logicalIndexAt=-1 的表头带空白
 * 照发，页面槽自行忽略负段）。clickable 门控只在 mouseReleaseEvent 里管
 * 单击口径的 sectionClicked 与指示器翻转，管不到双击。此前的实现把双击
 * 判定挂在按压链的 400ms 窗口上，而真实点击路径里表头事件由 XTableWidget
 * 转发进来（表头子件是隐藏的，见 XTableWidget 表头带转发），转发端直接
 * 以 DBL 事件类型进来 ⇒ 这里必须有独立的双击处理器。
 * [2026-11-10 根修] 原实现自加「section>=0 且 sectionsClickable」门控，
 * 而 HSF 页面（历史/日志）只接信号、从不 setSectionsClickable ⇒ 真实
 * 双击（桌面 WM 双击消息 / 触摸 DoubleTap 合成，序列均为
 * PRESS→RELEASE→DBL→RELEASE，没有第二次 PRESS）被门控吞掉，「双击列
 * 标题排序」失效——旧探针的「两连击」假路径走按压链 400ms 窗口（不受
 * 门控）才绿灯，掩盖了死区。 */
static void VXHeaderView_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XHeaderView* header = (XHeaderView*)self;
    XMouseEvent* me;
    XPoint pos;
    int position;
    int section;
    int handle;
    if (!header || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) return;
    pos = XMouseEvent_position(me);
    position = (header->m_orientation == 0) ? pos.x : pos.y;
    handle = XHeaderView_resizeHandleSectionAt(header, position);
    /* [Qt 口径 2026-11-10] 手柄双击仅 Interactive 模式算分隔线语义
     * （qheaderview.cpp:2745）；Fixed/Stretch 等模式的段尾双击仍落段
     * 双击分支（本库 resizeHandleSectionAt 不看模式，此处补判）。 */
    if (handle >= 0 &&
        XHeaderView_sectionResizeModeAt(header, handle) ==
            (int)XHeaderViewResizeMode_Interactive) {
        XHeaderView_sectionHandleDoubleClicked_signal(header, handle);
        XEvent_accept(event);
        return;
    }
    section = XHeaderView_logicalIndexAt(header, position);
    /* [Qt 口径 2026-11-10] 无条件发射（qheaderview.cpp:2759）——不看
     * sectionsClickable、不要求命中有效段；页面槽对负段自行忽略。 */
    XHeaderView_sectionDoubleClicked_signal(header, section);
    /* [盖章 2026-11-10] 记下"刚发过双击"，释放处理器的单击翻转在窗口
     * 内同段让位（否则一个双击周期里 DBL 排序(降)后被 RELEASE 翻转(升)
     * ⇒ 指示器与应用排序态错位；盖章段号可为 -1，负段本就进不了翻转
     * 分支，盖章无副作用）。 */
    header->m_lastDblEmitMs = (uint64_t)XDateTime_currentMSecsSinceEpoch();
    header->m_lastDblEmitSection = section;
    XEvent_accept(event);
}

static void VXHeaderView_deinit(XHeaderView* self)
{
    if (!self) return;
    /* 按压段/拖拽手势旁表键随表头析构移除（防悬垂键；旁表为静态 XMap 承载）。 */
    xhv_pressRelease(self);
    xhv_resizeRelease(self);
    /* K-68 换位手势旁表键随表头析构移除（与前两套同纪律）。 */
    xhv_moveRelease(self);
    if (self->m_sections) {
        XClassDelete(self->m_sections);
        self->m_sections = NULL;
    }
    if (self->m_sectionModes) {
        XClassDelete(self->m_sectionModes);
        self->m_sectionModes = NULL;
    }
    if (self->m_hidden) {
        XClassDelete(self->m_hidden);
        self->m_hidden = NULL;
    }
    if (self->m_visualToLogical) {
        XClassDelete(self->m_visualToLogical);
        self->m_visualToLogical = NULL;
    }
    /* [K-74 §B3] 级联快照表是对象拥有的第五张向量表，随析构释放。 */
    if (self->m_cascadingSizes) {
        XClassDelete(self->m_cascadingSizes);
        self->m_cascadingSizes = NULL;
    }
    /* m_model 为借用指针（表头不拥有），仅解除关联。 */
    self->m_model = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

static void VXHeaderView_copy(XHeaderView* self, const XHeaderView* other)
{
    int i;
    int n;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XHeaderView_init(self, NULL, 0, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_orientation = other->m_orientation;
    self->m_count = other->m_count;
    self->m_defaultSize = other->m_defaultSize;
    self->m_stretchLast = other->m_stretchLast;
    XVector_clear_base(self->m_sections);
    n = other->m_sections
            ? (int)XVector_size_base((const XContainer*)other->m_sections)
            : 0;
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(other->m_sections, (int64_t)i, int);
        XVector_push_back_1_base(self->m_sections, &v);
    }
    XVector_clear_base(self->m_sectionModes);
    n = other->m_sectionModes
            ? (int)XVector_size_base((const XContainer*)other->m_sectionModes)
            : 0;
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(other->m_sectionModes, (int64_t)i, int);
        XVector_push_back_1_base(self->m_sectionModes, &v);
    }
    XVector_clear_base(self->m_hidden);
    n = other->m_hidden
            ? (int)XVector_size_base((const XContainer*)other->m_hidden)
            : 0;
    for (i = 0; i < n; ++i) {
        bool v = XVector_At_Base(other->m_hidden, (int64_t)i, bool);
        XVector_push_back_1_base(self->m_hidden, &v);
    }
    if (self->m_visualToLogical && other->m_visualToLogical) {
        XVector_clear_base(self->m_visualToLogical);
        n = (int)XVector_size_base(
            (const XContainer*)other->m_visualToLogical);
        for (i = 0; i < n; ++i) {
            int v = XVector_At_Base(other->m_visualToLogical, (int64_t)i, int);
            XVector_push_back_1_base(self->m_visualToLogical, &v);
        }
    }
    /* [K-74 §B3] 级联快照表深拷贝（与其它四张表同口径）；源无表时
     * 目标也置空，避免继承一份长度不符的旧快照。 */
    if (self->m_cascadingSizes) {
        XClassDelete(self->m_cascadingSizes);
        self->m_cascadingSizes = NULL;
    }
    if (other->m_cascadingSizes) {
        self->m_cascadingSizes = XVector_Create(int);
        if (self->m_cascadingSizes) {
            n = (int)XVector_size_base(
                (const XContainer*)other->m_cascadingSizes);
            for (i = 0; i < n; ++i) {
                int v = XVector_At_Base(other->m_cascadingSizes,
                                        (int64_t)i, int);
                XVector_push_back_1_base(self->m_cascadingSizes, &v);
            }
        }
    }
    self->m_hiddenCount = other->m_hiddenCount;
    self->m_sectionsClickable = other->m_sectionsClickable;
    self->m_sectionsMovable = other->m_sectionsMovable;
    self->m_sortIndicatorShown = other->m_sortIndicatorShown;
    self->m_sortIndicatorSection = other->m_sortIndicatorSection;
    self->m_sortIndicatorOrder = other->m_sortIndicatorOrder;
    self->m_sortIndicatorClearable = other->m_sortIndicatorClearable;
    self->m_minimumSectionSize = other->m_minimumSectionSize;
    self->m_maximumSectionSize = other->m_maximumSectionSize;
    self->m_resizeMode = other->m_resizeMode;
    self->m_firstSectionMovable = other->m_firstSectionMovable;
    self->m_defaultAlignment = other->m_defaultAlignment;
    self->m_highlightSections = other->m_highlightSections;
    self->m_cascadingResizes = other->m_cascadingResizes;
    self->m_resizeContentsPrecision = other->m_resizeContentsPrecision;
    self->m_sectionsMovedFlag = other->m_sectionsMovedFlag;
    self->m_offset = other->m_offset;
    /* [K-73] 末段快照随拷贝走；视口可用长是宿主推送的运行期事实，
     * 拷贝件不继承（引擎停跑，直到新宿主重新推送）。 */
    self->m_lastSectionSize = other->m_lastSectionSize;
    self->m_lastSectionLogical = other->m_lastSectionLogical;
    self->m_stretchViewportLen = -1;
    /* [K-64 §B3] 视口宽是宿主推送的运行期事实，拷贝件不继承。 */
    self->m_offsetViewportLen = -1;
    /* m_model 为借用指针，浅拷贝即可（不转移所有权）。 */
    self->m_model = other->m_model;
}

static void VXHeaderView_move(XHeaderView* self, XHeaderView* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XHeaderView_init(self, NULL, 0, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    if (self->m_sections) XClassDelete(self->m_sections);
    if (self->m_sectionModes) XClassDelete(self->m_sectionModes);
    if (self->m_hidden) XClassDelete(self->m_hidden);
    if (self->m_visualToLogical) XClassDelete(self->m_visualToLogical);
    if (self->m_cascadingSizes) XClassDelete(self->m_cascadingSizes);
    self->m_orientation = other->m_orientation;
    self->m_count = other->m_count;
    self->m_defaultSize = other->m_defaultSize;
    self->m_stretchLast = other->m_stretchLast;
    self->m_sections = other->m_sections;
    other->m_sections = XVector_Create(int);
    self->m_sectionModes = other->m_sectionModes;
    other->m_sectionModes = XVector_Create(int);
    self->m_hidden = other->m_hidden;
    other->m_hidden = XVector_Create(bool);
    self->m_visualToLogical = other->m_visualToLogical;
    other->m_visualToLogical = XVector_Create(int);
    /* [K-74 §B3] 级联快照表随移动转移，源置空（与其它四表同口径）。 */
    self->m_cascadingSizes = other->m_cascadingSizes;
    other->m_cascadingSizes = NULL;
    self->m_hiddenCount = other->m_hiddenCount;
    other->m_hiddenCount = 0;
    self->m_sectionsClickable = other->m_sectionsClickable;
    self->m_sectionsMovable = other->m_sectionsMovable;
    self->m_sortIndicatorShown = other->m_sortIndicatorShown;
    self->m_sortIndicatorSection = other->m_sortIndicatorSection;
    self->m_sortIndicatorOrder = other->m_sortIndicatorOrder;
    self->m_sortIndicatorClearable = other->m_sortIndicatorClearable;
    self->m_minimumSectionSize = other->m_minimumSectionSize;
    self->m_maximumSectionSize = other->m_maximumSectionSize;
    self->m_resizeMode = other->m_resizeMode;
    self->m_firstSectionMovable = other->m_firstSectionMovable;
    self->m_defaultAlignment = other->m_defaultAlignment;
    self->m_highlightSections = other->m_highlightSections;
    self->m_cascadingResizes = other->m_cascadingResizes;
    self->m_resizeContentsPrecision = other->m_resizeContentsPrecision;
    self->m_sectionsMovedFlag = other->m_sectionsMovedFlag;
    other->m_count = 0;
    self->m_offset = other->m_offset;
    /* m_model 为借用指针，移动即转移（源对象解除关联）。 */
    self->m_model = other->m_model;
    other->m_model = NULL;
    /* [K-73] 引擎态随移动整体转移，源端清账。 */
    self->m_lastSectionSize = other->m_lastSectionSize;
    self->m_lastSectionLogical = other->m_lastSectionLogical;
    self->m_stretchViewportLen = other->m_stretchViewportLen;
    self->m_offsetViewportLen = other->m_offsetViewportLen;
    other->m_lastSectionSize = -1;
    other->m_lastSectionLogical = -1;
    other->m_stretchViewportLen = -1;
    other->m_offsetViewportLen = -1;
}

/* [表头带事件转发入口 2026-11-10] XTableView/XTableWidget 把表头子件
 * 隐藏、表头带由表格自绘 ⇒ 真实鼠标事件先落在表格上。表格在命中于表头
 * 带（y < headerHeight）时负责把事件**换算成表头局部坐标**再调这三口
 * （等价 Qt 里 QTableView::viewportEvent 对 header 的转发路径）。
 * 只做直呼转发：判型/按键/命中/发射全在表头自己的虚槽里（与真实事件
 * 走同一套代码，不另立第二套语义）。 */
void XHeaderView_forwardPress(XHeaderView* self, XEvent* event)
{
    if (!self || !event) return;
    VXHeaderView_mousePressEvent((XWidget*)self, event);
}

void XHeaderView_forwardRelease(XHeaderView* self, XEvent* event)
{
    if (!self || !event) return;
    VXHeaderView_mouseReleaseEvent((XWidget*)self, event);
}

void XHeaderView_forwardDoubleClick(XHeaderView* self, XEvent* event)
{
    if (!self || !event) return;
    VXHeaderView_mouseDoubleClickEvent((XWidget*)self, event);
}

/* ==================== 保护接口（XHeaderView_Protected.h 2026-11-14） ==================== */

/* [K-68] 移动段转发：与上方 forwardPress/forwardRelease/forwardDoubleClick
 * 同族——判型/按键/手势机算全在表头自己的鼠标移动虚槽里（与真实事件
 * 同一套语义），宿主视图只负责带内命中与坐标换算后调用本口。 */
void XHeaderView_forwardMove(XHeaderView* self, XEvent* event)
{
    if (!self || !event) return;
    VXHeaderView_mouseMoveEvent((XWidget*)self, event);
}

/* [K-68] 手势活动查询：宿主视图在表头带外收到 release/move 时凭它决定
 * 是否仍转发（对标 Qt 表头 grabMouse 后事件直达表头；本表头是隐藏子件、
 * grab 无效——XWidget_grabMouse 对 !m_visible 直接返回，转发由视图承担）。 */
bool XHeaderView_gestureActive(const XHeaderView* self)
{
    const XhvResizeState* resizeGesture = xhv_resizeGet(self);
    const XhvMoveState* moveGesture;
    if (resizeGesture && resizeGesture->m_active) return true;
    moveGesture = xhv_moveGet(self);
    return moveGesture && moveGesture->m_active;
}

/* [K-69] 按压段查询（XHeaderViewPrivate::pressed 对标，绘制路径用它给
 * 按压段补 State_Sunken，qheaderview.cpp:2870-2872）。 */
int XHeaderView_pressedSection(const XHeaderView* self)
{
    return xhv_pressGet(self);
}

/* [K-73] setStretchViewportLength 定义在 stretchLastSection 引擎块。 */

/* ==================== §B3 表头自绘 / 内容测算虚槽（2026-11-15） ====================
 * [为什么] 台账 §B3（:5358）登记：XHeaderView 不覆写 PaintEvent，
 * 独立摆放不可见；sectionSizeHint 恒缺省（无内容感知）。本批按 Qt 6.8
 * QHeaderView::paintSection（qheaderview.cpp:2958-2982）与
 * sectionSizeFromContents（:2995-3020）补两个 protected 虚函数的 _base
 * 公开入口（风格文档 §497 条款 1-7）：对外唯一入口 *_base 做
 * 空/虚表守卫 + 一次 XClassGetVirtualFunc 查槽分派，业务在 static
 * VXHeaderView_* 实现、经 XVTABLE_OVERLOAD_DEFAULT 挂表。派生表头可直接
 * 覆写两个槽自绘/自测内容。 */

/** @brief 取某段的表头文本（借用模型 headerData_2 的 UTF-8 缓存）。
 *  @note  XSTYLE_ON 关时仍要能画段文本，故文本读取不依赖样式层。
 *         返回借用指针（未设/越界返回空串 ""），不释放。 */
static const char* xhv_sectionText(const XHeaderView* self, int logicalIndex)
{
    if (!self || !self->m_model) return "";
    return XAbstractItemModel_headerData_2(
        (const XAbstractItemModel*)self->m_model, logicalIndex,
        self->m_orientation);
}

#if XSTYLE_ON
/** @brief 构造某段的表头样式选项（对标 Qt initStyleOption/
 *         initStyleOptionForIndex，qheaderview.cpp:2860-2878）。
 *  @note  XStyleOptionHeader 的段语义字段（m_textElideMode 等）在基类
 *         选项之外，整份用 XStyleOptionHeader_init 清零后逐字段填；
 *         m_text 是借用模型 headerData_2 的 UTF-8 缓存，活到调用返回。 */
static void xhv_initSectionOption(XHeaderView* self,
                                  XStyleOptionHeader* opt,
                                  const XRect* rect, int logicalIndex)
{
    XFont font;
    XStyleFontMetrics fm;
    const char* text;
    if (!opt) return;
    XStyleOptionHeader_init(opt, XStyleCE_Header);
    opt->m_base.m_rect = *rect;
    opt->m_base.m_state = (uint32_t)XStyleState_Enabled |
                          (uint32_t)XStyleState_Raised;
    if (self->m_orientation == 0)
        opt->m_base.m_state |= (uint32_t)XStyleState_Horizontal;
#if XPALETTE_ON
    opt->m_base.m_palette = XWidget_palette((const XWidget*)self);
#endif
    opt->m_section = logicalIndex;
    opt->m_orientation = self->m_orientation;
    /* 模型表头文本（借用 UTF-8；headerData_2 越界/未设返回空串 ""）。
     * @note **必须两处都写**：XStyleOptionHeader::m_text（派生，:1255）与
     * XStyleOption::m_text（基类，:948）是两个独立成员。样式侧消费点读的
     * 是**基类**字段——xcs_drawHeader 整份拷贝后走 sub.m_base.m_text、
     * xcs_drawHeaderLabel 读 option->m_text（XCommonStyle.c:3092）、
     * CT_HeaderSection 读 option->m_text（XCommonStyle.c:3669）——只写派生
     * 字段会导致「底画了、字一个没有」且内容测算 textW 恒 0。视图侧同一
     * 写法见 XTableWidget.c:4577/4582（两处都写）。 */
    text = xhv_sectionText(self, logicalIndex);
    if (text && text[0]) {
        opt->m_base.m_text = text;
        opt->m_text = text;
    }
    /* 排序指示器（对标 Qt sectionSizeFromContents：显示指示器时按
     * SortDown 计入，qheaderview.cpp:3017-3018）。 */
    if (self->m_sortIndicatorShown &&
        self->m_sortIndicatorSection == logicalIndex)
        opt->m_sortIndicator = self->m_sortIndicatorOrder ==
            (int)XHeaderViewSortOrder_Descending
                ? (int)XStyleOptionHeaderSortIndicator_SortDown
                : (int)XStyleOptionHeaderSortIndicator_SortUp;
    /* 字体度量：按当前控件字体（加粗）取真实文本宽/高，交给样式
     * CT_HeaderSection（X 侧 fontMetrics 是值结构，见 XStyleOption.h:676）。 */
    font = XWidget_fontMetrics((const XWidget*)self);
    XFont_setBold(&font, true);
    fm.m_height = XPainter_textHeight(&font);
    fm.m_ascent = XPainter_textAscent(&font);
    fm.m_descent = XPainter_textDescent(&font);
    fm.m_lineSpacing = fm.m_height;
    fm.m_averageCharWidth = XPainter_textWidth(&font, "M");
    fm.m_spaceWidth = XPainter_textWidth(&font, " ");
    XStyleOption_setFontMetrics(&opt->m_base, &fm);
    XClassDeinit(&font);
    /* 按压态（对标 K-69 的 initStyleOptionForIndex，qheaderview.cpp:
     * 2868-2872：clickable 开启时按压段补 State_Sunken）。 */
    if (self->m_sectionsClickable && xhv_pressGet(self) == logicalIndex)
        opt->m_base.m_state |= (uint32_t)XStyleState_Sunken;
}
#endif /* XSTYLE_ON */

/** @brief 段内容尺寸测算默认实现（对标 QHeaderView::
 *         sectionSizeFromContents）。
 *  @details 取模型 headerData 文本 + 当前字体（加粗）度量 + 排序指示器，
 *           交样式 XStyleCT_HeaderSection::sizeFromContents（Qt 口径：
 *           qheaderview.cpp:2995-3020 → qcommonstyle.cpp:4988-5004），
 *           水平取宽、垂直取高。无模型或样式模块关闭时回落
 *           defaultSectionSize（避免给纯几何表头返回 0 尺寸）。 */
static XSize VXHeaderView_sectionSizeFromContents(XHeaderView* self,
                                                  int logicalIndex)
{
    XSize fallback;
    XSize result;
    if (!self) { XSize_init(&fallback, 0, 0); return fallback; }
    XSize_init(&fallback, self->m_defaultSize, self->m_defaultSize);
    if (logicalIndex < 0 || logicalIndex >= self->m_count) {
        XSize_init(&result, 0, 0);
        return result;
    }
    if (!self->m_model) return fallback;   /* 无内容 ⇒ 回落缺省。 */
#if XSTYLE_ON
    {
        XStyleOptionHeader opt;
        XRect r;
        XStyle* style;
        XRect_init(&r, 0, 0, 0, 0);
        xhv_initSectionOption(self, &opt, &r, logicalIndex);
        style = XWidget_style((const XWidget*)self);
        if (!style) return fallback;
        XSize_init(&result, 0, 0);
        result = XStyle_sizeFromContents_base(style, XStyleCT_HeaderSection,
                                             &opt.m_base, result);
        return result;
    }
#else
    return fallback;
#endif
}

/** @brief 绘制单个表头段的默认实现（对标 QHeaderView::paintSection）。
 *  @details 样式模块开启时构造表头选项、交样式 XStyleCE_Header 一次绘制
 *           （Qt qheaderview.cpp:2958-2982 → QCommonStyle CE_Header）；
 *           样式关闭时回落到「底 + 居中文本 + 分隔线」的等价原始自绘。
 *           无效矩形早退（同 Qt `if (!rect.isValid()) return;`）。 */
static void VXHeaderView_paintSection(XHeaderView* self, XPainter* painter,
                                      const XRect* rect, int logicalIndex)
{
    if (!self || !painter || !rect) return;
    if (rect->width <= 0 || rect->height <= 0) return;  /* Qt !isValid */
#if XSTYLE_ON
    {
        XStyleOptionHeader opt;
        XStyle* style;
        xhv_initSectionOption(self, &opt, rect, logicalIndex);
        style = XWidget_style((const XWidget*)self);
        if (style) {
            XStyle_drawControl_base(style, XStyleCE_Header, &opt.m_base,
                                    painter, (const XWidget*)self);
            return;
        }
    }
#endif
    /* 无样式引擎：等价的最低限度自绘（底 + 文本 + 右/底分隔线）。 */
    {
        const char* text = xhv_sectionText(self, logicalIndex);
        XPainter_fillRect(painter, rect, 0xFFF0F0F0u);
        if (text && text[0]) {
            const XFont* font = XPainter_font(painter);
            int tw = XPainter_textWidth(font, text);
            int th = XPainter_textHeight(font);
            if (th < 14) th = 14;
            XPainter_drawText(painter,
                              rect->x + (rect->width - tw) / 2,
                              rect->y + (rect->height - th) / 2 + th - 4,
                              text, 0xFF444444u);
        }
        XPainter_drawLine(painter, rect->x + rect->width - 1, rect->y,
                          rect->x + rect->width - 1,
                          rect->y + rect->height - 1);
        XPainter_drawLine(painter, rect->x, rect->y + rect->height - 1,
                          rect->x + rect->width - 1,
                          rect->y + rect->height - 1);
    }
}

/** @brief 表头本体绘制（对标 QHeaderView::paintEvent，独立摆放可见）。
 *  @details 按视觉序遍历可见段，逐段以 sectionPosition − offset 为起点
 *           构造段矩形，调用 XHeaderView_paintSection_base（派生可覆写）。
 *           PAINT 区域裁剪 + translate（与 XPushButton 同款），只画与
 *           PAINT 矩形相交的段（AGENTS「paintEvent 要消费 PAINT 矩形」）。
 *           表头带由 XTableView 自绘时本控件被隐藏 ⇒ 本路径不跑，零
 *           视觉漂移；独立摆放时由本路径把段带画出来。 */
static void VXHeaderView_paintEvent(XWidget* self, XEvent* event)
{
    XHeaderView* header = (XHeaderView*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect clip;
    int v;
    int pos;
    if (!header || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    clip = XPaintEvent_rect((const XPaintEvent*)event);
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_ReplaceClip);
    pos = 0;
    for (v = 0; v < header->m_count; ++v) {
        int logical = XHeaderView_logicalIndex(header, v);
        int size;
        XRect sec;
        if (logical < 0 || XHeaderView_isSectionHidden(header, logical))
            continue;
        size = XHeaderView_sectionSize(header, logical);
        if (header->m_orientation == 0)
            XRect_init(&sec, pos - header->m_offset, 0, size,
                       clip.height > 0 ? clip.height : 0);
        else
            XRect_init(&sec, 0, pos - header->m_offset,
                       clip.width > 0 ? clip.width : 0, size);
        /* 交叉轴取控件真实高/宽（PAINT 区域只作脏区、不作段厚）。 */
        if (header->m_orientation == 0)
            sec.height = XWidget_height(self);
        else
            sec.width = XWidget_width(self);
        pos += size;
        if (sec.x + sec.width <= clip.x || sec.x >= clip.x + clip.width ||
            sec.y + sec.height <= clip.y || sec.y >= clip.y + clip.height)
            continue;   /* 与 PAINT 区域不相交 ⇒ 跳过。 */
        XHeaderView_paintSection_base(header, &painter, &sec, logical);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief paintSection 虚分派唯一公开入口（_base 契约）。
 *  @note  只有空/虚表守卫 + 一次 XClassGetVirtualFunc 查槽分派，
 *         无业务、无同名无后缀公开函数、无二级转发桥。 */
void XHeaderView_paintSection_base(XHeaderView* self, XPainter* painter,
                                   const XRect* rect, int logicalIndex)
{
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return;
    XClassGetVirtualFunc(self, EXHeaderView_PaintSection,
                         void (*)(XHeaderView*, XPainter*, const XRect*,
                                  int))(self, painter, rect, logicalIndex);
}

/** @brief sectionSizeFromContents 虚分派唯一公开入口（_base 契约）。 */
XSize XHeaderView_sectionSizeFromContents_base(XHeaderView* self,
                                               int logicalIndex)
{
    XSize empty;
    XSize_init(&empty, 0, 0);
    if (ISNULL(self, "") || ISNULL(XClassGetVtable(self), "")) return empty;
    return XClassGetVirtualFunc(self, EXHeaderView_SectionSizeFromContents,
                                XSize (*)(XHeaderView*, int))(self,
                                                              logicalIndex);
}

XVtable* XHeaderView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XHeaderView)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXHeaderView_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXHeaderView_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXHeaderView_move);
    /* 对标 Qt QHeaderView 的鼠标事件承接（qheaderview.cpp:2505/:2544/
     * :2668）：分隔线拖拽调宽手势 + 段按压/释放驱动
     * sectionPressed/sectionClicked 发射（两者命中互斥）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXHeaderView_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXHeaderView_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXHeaderView_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXHeaderView_mouseDoubleClickEvent);
    /* [§B3 2026-11-15] 表头自绘与内容测算：独立摆放可见 + 派生可覆写。
     * PaintEvent 覆写让 XHeaderView 自己画段带（表格自绘表头时表头子件
     * 被隐藏、本路径不跑）；两个 protected 虚函数按 _base 契约挂虚表。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VXHeaderView_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXHeaderView_PaintSection,
                             VXHeaderView_paintSection);
    XVTABLE_OVERLOAD_DEFAULT(EXHeaderView_SectionSizeFromContents,
                             VXHeaderView_sectionSizeFromContents);
    return XVTABLE_DEFAULT;
}

/** @brief 刷新尺寸提示（对标 Qt QHeaderView）。
 *  @details Qt 6.8.3（qheaderview.cpp）：sizeHint = 全部可见段的尺寸
 *           沿主轴累加、交叉轴取最大段尺寸（本类段高固定为表头行高，
 *           交叉轴取段尺寸的较大者，保持与段内容自洽）。 */
static void xheaderview_updateSizeHint(XHeaderView* self)
{
    XSize hint;
    int count;
    int i;
    int mainAcc = 0;
    int crossMax = 0;
    bool horizontal;
    if (!self) return;
    horizontal = self->m_orientation == 1;
    count = XHeaderView_count(self);
    for (i = 0; i < count; ++i) {
        int sz = XHeaderView_sectionSize(self, i);
        if (sz <= 0) continue;
        mainAcc += sz;
        if (sz > crossMax) crossMax = sz;
    }
    if (horizontal) XSize_init(&hint, mainAcc, crossMax);
    else XSize_init(&hint, crossMax, mainAcc);
    XWidget_setSizeHint((XWidget*)self, &hint);
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
}

void XHeaderView_init(XHeaderView* self, XWidget* parent, XWidgetFlags flags,
                      int orientation)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XHeaderView);
    self->m_orientation = (orientation == 1) ? 1 : 0;
    self->m_count = 0;
    /* 对标 Qt 6.8.0 实测默认段尺寸：水平 100 / 垂直 30（按方向取常量，
     * 见 XHEADERVIEW_DEFAULT_SECTION_SIZE_H/V）。 */
    self->m_defaultSize =
        (int16_t)xhv_defaultSectionSizeFor(self->m_orientation);
    self->m_stretchLast = false;
    self->m_sectionMovedFrom = -1;
    self->m_offset = 0;
    self->m_model = NULL;
    self->m_sections = XVector_Create(int);
    self->m_sectionModes = XVector_Create(int);
    self->m_hidden = XVector_Create(bool);
    /* 视觉序映射：空表＝无移动（visualIndex 恒等返回；对标 Qt
     * visualIndices.isEmpty() 分支）。moveSection/swapSections 时建表。 */
    self->m_visualToLogical = XVector_Create(int);
    self->m_hiddenCount = 0;
    /* 对标 Qt 6.8.0 实测：**独立** QHeaderView 的 sectionsClickable 缺省
     * false（QHeaderViewPrivate::setDefaultValues 的 clickableSections
     * 初值 false）；QTableView/QTableWidget 在自建表头上显式
     * setSectionsClickable(true)（qtableview.cpp:582-588），该路径由
     * XTableView_init 负责，本类保持 Qt 独立表头的缺省。 */
    self->m_sectionsClickable = false;
    self->m_lastDblSection = -1;
    self->m_lastEnteredSection = -1;
    self->m_lastDblPressMs = 0;
    self->m_sectionsMovable = false;
    self->m_sortIndicatorShown = false;
    self->m_sortIndicatorSection = -1;
    self->m_sortIndicatorOrder = 0;
    self->m_sortIndicatorClearable = false;
    /* 对标 Qt 6.8.0 实测：首段可动缺省 true（存储真正的缺省，由
     * QHeaderViewPrivate::setDefaultValues 置 firstSectionMovable=true；
     * QTreeView 另行置 false，见 qtreeview.cpp）。
     * 注意：独立表头实测 firstSectionMovable()==true，与视图无关。 */
    self->m_firstSectionMovable = true;
    self->m_minimumSectionSize =
        xhv_defaultMinimumSectionSizeFor(self->m_orientation);
    self->m_maximumSectionSize = XHEADERVIEW_SECTION_SIZE_LIMIT;
    self->m_resizeMode = (int)XHeaderViewResizeMode_Interactive;
    /* 对标 Qt setDefaultValues：水平头居中，垂直头左对齐+垂直居中。 */
    self->m_defaultAlignment = (self->m_orientation == 0)
        ? (int)XAlignment_Center
        : ((int)XAlignment_Left | (int)XAlignment_VCenter);
    /* 对标 Qt 6.8.0 实测：**独立** QHeaderView 的 highlightSections 缺省
     * false（QHeaderViewPrivate::setDefaultValues: highlightSelected(false)）；
     * QTableView/QTableWidget 的自建表头在 qtableview.cpp:583/588 显式
     * setHighlightSections(true)——该路径由 XTableView_init 负责。 */
    self->m_highlightSections = false;
    self->m_cascadingResizes = false;
    /* 对标 Qt sectionsMoved：未发生过移动时为 false。 */
    self->m_sectionsMovedFlag = 0;
    self->m_resizeContentsPrecision =
        XHEADERVIEW_DEFAULT_RESIZE_CONTENTS_PRECISION;
    /* [K-73] stretchLastSection 引擎态初值：无末段快照、视口长未推送
     * （推送由宿主视图经 XHeaderView_Protected.h 入口承担）。 */
    self->m_lastSectionSize = -1;
    self->m_lastSectionLogical = -1;
    self->m_stretchViewportLen = -1;
    /* [K-64 §B3 2026-11-15] 视口宽未推送：offset 分支按 0 参与
     * （setOffsetToLastSection 退化为 length 的旧口径保持可用）。 */
    self->m_offsetViewportLen = -1;
    xheaderview_updateSizeHint(self);

}

XHeaderView* XHeaderView_create_ex(XMemoryType memory, XWidget* parent,
                                   XWidgetFlags flags, int orientation)
{
    XHeaderView* self = (XHeaderView*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XHeaderView_init(self, parent, flags, orientation);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

int XHeaderView_orientation(const XHeaderView* self)
{ return self ? self->m_orientation : 0; }

int XHeaderView_count(const XHeaderView* self)
{ return self ? self->m_count : 0; }

/** @brief 取视觉映射表的有效长度（表缺失按 0＝恒等）。 */
static int xhv_mapCount(const XHeaderView* self)
{
    if (!self || !self->m_visualToLogical) return 0;
    return (int)XVector_size_base((const XContainer*)self->m_visualToLogical);
}

/**
 * @brief 让视觉映射表与段数对齐（追加恒等项 / 缩容保留相对视觉序）。
 *
 *        对标 Qt QHeaderViewPrivate::sectionsInserted 的映射维护
 *        （qheaderview.cpp:1904-1921）：新增段以恒等项插入末尾；删段时
 *        移除对应逻辑号、其余段保持原有相对视觉序；不足部分补恒等项，
 *        保证本表始终是 0..count-1 的排列（visualIndex/logicalIndex
 *        互逆的前提）。
 *
 * @param self 目标表头。
 * @param count 目标段数（>=0）。
 * @return 无返回值。
 */
static void xhv_mapSyncCount(XHeaderView* self, int count)
{
    int cur;
    int i;
    if (!self || !self->m_visualToLogical || count < 0) return;
    cur = xhv_mapCount(self);
    if (count > cur) {
        for (i = cur; i < count; ++i)
            XVector_push_back_1_base(self->m_visualToLogical, &i);
        return;
    }
    if (count < cur) {
        /* 缩容：按原有视觉序保留逻辑号 < count 的项（Qt sectionsRemoved
         * 的同款语义），存活项数必等于 count（旧表是 0..cur-1 的排列）。 */
        XVector* keep = XVector_Create(int);
        int k;
        if (!keep) return;
        for (i = 0; i < cur; ++i) {
            int logical = XVector_At_Base(self->m_visualToLogical,
                                          (int64_t)i, int);
            if (logical >= 0 && logical < count)
                XVector_push_back_1_base(keep, &logical);
        }
        /* 防御：映射若已被外部破坏则补恒等项到 count 个。 */
        for (k = 0; k < count &&
                    (int)XVector_size_base((const XContainer*)keep) < count;
             ++k) {
            int found = 0;
            int j;
            int kn = (int)XVector_size_base((const XContainer*)keep);
            for (j = 0; j < kn; ++j) {
                if (XVector_At_Base(keep, (int64_t)j, int) == k) {
                    found = 1;
                    break;
                }
            }
            if (!found) XVector_push_back_1_base(keep, &k);
        }
        XVector_clear_base(self->m_visualToLogical);
        for (i = 0; i < (int)XVector_size_base((const XContainer*)keep); ++i) {
            int v = XVector_At_Base(keep, (int64_t)i, int);
            XVector_push_back_1_base(self->m_visualToLogical, &v);
        }
        XClassDelete(keep);
    }
}

void XHeaderView_setCount(XHeaderView* self, int count)
{
    int i;
    int n;
    int oldCount;
    if (!self || !self->m_sections || count < 0) return;
    oldCount = self->m_count;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (count > n) {
        for (i = n; i < count; ++i) {
            int v = self->m_defaultSize;
            int mv = -1;
            bool h = false;
            XVector_push_back_1_base(self->m_sections, &v);
            if (self->m_sectionModes)
                XVector_push_back_1_base(self->m_sectionModes, &mv);
            if (self->m_hidden)
                XVector_push_back_1_base(self->m_hidden, &h);
        }
    } else if (count < n) {
        for (i = count; i < n; ++i) {
            if (self->m_hidden &&
                XVector_At_Base(self->m_hidden, (int64_t)i, bool))
                --self->m_hiddenCount;
        }
        XVector_remove_base(self->m_sections, (int64_t)count,
                            (int64_t)(n - count));
        if (self->m_sectionModes)
            XVector_remove_base(self->m_sectionModes, (int64_t)count,
                                (int64_t)(n - count));
        if (self->m_hidden)
            XVector_remove_base(self->m_hidden, (int64_t)count,
                                (int64_t)(n - count));
    }
    /* 视觉映射随段数对齐（追加恒等项/缩容保留相对视觉序）；先于早退执行
     * 使映射长度恒等于段数（copy/restore 直接写入表的场景也自愈）。 */
    xhv_mapSyncCount(self, count);
    if (count == oldCount) return; /* 对标 Qt：段数未变不发信号。 */
    self->m_count = count;
    /* 对标 Qt emitSectionCountChanged + updateGeometries→geometriesChanged。 */
    xhv_emitInt2(self, (size_t)XHeaderView_sectionCountChanged_signal(
                      self, oldCount, count), oldCount, count);
    xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    /* [K-73] 段数变化随动（对标 Qt sectionsInserted/Removed→
     * doDelayedResizeSections；本库同步，见引擎块头注）。 */
    xhv_stretchMaybe(self);
}

/** @brief 按当前 min/max 上下限钳制尺寸（对标 Qt qBound(minimumSectionSize, size, maximumSectionSize)）。 */
static int xhv_clampSize(const XHeaderView* self, int size)
{
    if (size < self->m_minimumSectionSize) return self->m_minimumSectionSize;
    if (size > self->m_maximumSectionSize) return self->m_maximumSectionSize;
    return size;
}

void XHeaderView_resizeSection(XHeaderView* self, int logicalIndex, int size)
{
    int n;
    if (!self || !self->m_sections || logicalIndex < 0 || size < 0) return;
    if (size > XHEADERVIEW_SECTION_SIZE_LIMIT) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (logicalIndex >= n) return;
    /* 对标 Qt 6.8 resizeSection（qheaderview.cpp:822-830）：
     *  - size < 0 与 size > maxSizeSection(1048575) 拒绝；
     *  - size > 0 才钳制到 [minimumSectionSize, maximumSectionSize]；
     *  - **size == 0 原样存储**（Qt 实测 resizeSection(0,0) 后
     *    sectionSize()==0、sectionPosition(1)==0；文档建议改用
     *    hideSection，但不拒绝）。旧实现按 >0 约束拒绝 0，gap 155。
     * 随后走 setSectionSize 统一发射 sectionResized 与 geometriesChanged
     * （隐藏段在本实现仍持有尺寸表项，直接改写该表项即可）。 */
    XHeaderView_setSectionSize(self, logicalIndex,
                               size > 0 ? xhv_clampSize(self, size) : 0);
    xheaderview_updateSizeHint((XHeaderView*)self);
    /* [K-73] 对标 Qt resizeSection 的末段快照维护（qheaderview.cpp:851-852
     * `if (stretchLastSection() && logical == d->lastSectionLogicalIdx)
     * d->lastSectionSize = size;`）：程序化调宽落在末段时刷新快照——
     * 该值是下一次重排里末段的下限（本机探针：setColumnWidth(3,222) 后
     * 末段恒 >= 222，改宽它列后余量 >= 222 时末段回吃余量）。 */
    if (self->m_stretchLast && logicalIndex == self->m_lastSectionLogical)
        self->m_lastSectionSize =
            size > 0 ? xhv_clampSize(self, size) : 0;
    /* 动作尾部即时重排（对标 Qt resizeSection 尾部
     * hasAutoResizeSections ⇒ doDelayedResizeSections——stretchLastSection
     * 计入 hasAutoResizeSections（qheaderview_p.h:165-167），Qt 延迟一拍
     * 到下一轮事件循环，本库收敛为同步，见引擎块头注）。 */
    xhv_stretchMaybe(self);
}

/** @brief 查询区间是否隐藏（对标 isSectionHidden）。 */
bool XHeaderView_isSectionHidden(const XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_hidden || section < 0) return false;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (section >= n) return false;
    return XVector_At_Base(self->m_hidden, (int64_t)section, bool);
}

void XHeaderView_hideSection(XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_hidden || section < 0) return;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (section >= n) return;
    if (!XVector_At_Base(self->m_hidden, (int64_t)section, bool)) {
        XVector_At_Base(self->m_hidden, (int64_t)section, bool) = true;
        ++self->m_hiddenCount;
        /* 隐藏影响表头几何（对标 Qt updateGeometries→geometriesChanged）。 */
        xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
        /* [K-73] 隐藏末段 ⇒ 末段身份变化（对标 Qt hideSection→
         * maybeRestorePrevLastSectionAndStretchLast）：引擎先按快照复原
         * 旧末段、再拉伸新末段（本机探针：hide col3 后 col2 立即吃满余量）。 */
        xhv_stretchMaybe(self);
    }
}

void XHeaderView_showSection(XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_hidden || section < 0) return;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (section >= n) return;
    if (XVector_At_Base(self->m_hidden, (int64_t)section, bool)) {
        XVector_At_Base(self->m_hidden, (int64_t)section, bool) = false;
        --self->m_hiddenCount;
        /* 显示影响表头几何（对标 Qt updateGeometries→geometriesChanged）。 */
        xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
        /* [K-73] 显示的段若成为新末段 ⇒ 复原旧末段并拉伸新末段（同
         * hideSection 的 K-73 注）。 */
        xhv_stretchMaybe(self);
    }
}

int XHeaderView_hiddenSectionCount(const XHeaderView* self)
{ return self ? self->m_hiddenCount : 0; }

void XHeaderView_setSectionHidden(XHeaderView* self, int section, bool hide)
{
    /* 对标 Qt：setSectionHidden 为原始入口（本实现反向转发便捷接口，
     * 信号语义与 hideSection/showSection 一致）。 */
    if (hide) XHeaderView_hideSection(self, section);
    else XHeaderView_showSection(self, section);
}

void XHeaderView_setSectionsClickable(XHeaderView* self, bool clickable)
{ if (self) self->m_sectionsClickable = clickable; }

bool XHeaderView_sectionsClickable(const XHeaderView* self)
{ return self ? self->m_sectionsClickable : false; }

void XHeaderView_setSectionsMovable(XHeaderView* self, bool movable)
{ if (self) self->m_sectionsMovable = movable; }

bool XHeaderView_sectionsMovable(const XHeaderView* self)
{ return self ? self->m_sectionsMovable : false; }

void XHeaderView_swapSections(XHeaderView* self, int first, int second)
{
    int n;
    int tmp;
    int firstLogical;
    int secondLogical;
    if (!self || !self->m_sections || first < 0 || second < 0) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (first >= n || second >= n || first == second) return;
    /* 对标 Qt 6.8 swapSections（qheaderview.cpp:761-807）：参数是**视觉**
     * 序号；尺寸/隐藏/模式随视觉槽交换（createSectionItems 两段互换），
     * 逻辑↔视觉映射两个方向互换。载荷：sectionMoved(原 first 号段的
     * 逻辑号, first, second) 与 sectionMoved(原 second 号段的逻辑号,
     * second, first)——逻辑号取**交换前**该视觉槽上的段（Qt 实测 swap
     * 后两条载荷为 (1,0,1)/(2,1,0)）。 */
    firstLogical = XHeaderView_logicalIndex(self, first);
    secondLogical = XHeaderView_logicalIndex(self, second);
    if (firstLogical < 0 || secondLogical < 0) return;
    /* 尺寸表按逻辑段号存储 ⇒ 交换视觉序即可让尺寸随逻辑段（Qt 实测
     * sectionSize(logical) 在 swap 后保持）。 */
    xhv_mapSyncCount(self, n);
    tmp = XVector_At_Base(self->m_visualToLogical, (int64_t)first, int);
    XVector_At_Base(self->m_visualToLogical, (int64_t)first, int) =
        XVector_At_Base(self->m_visualToLogical, (int64_t)second, int);
    XVector_At_Base(self->m_visualToLogical, (int64_t)second, int) = tmp;
    self->m_sectionMovedFrom = first;
    /* 对标 Qt sectionsMoved：发生过实际移动后恒为 true。 */
    self->m_sectionsMovedFlag = 1;
    xhv_emitInt3(self, (size_t)XHeaderView_sectionMoved_signal(
                      self, firstLogical, first, second), firstLogical, first,
                  second);
    xhv_emitInt3(self, (size_t)XHeaderView_sectionMoved_signal(
                      self, secondLogical, second, first), secondLogical,
                  second, first);
    xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    /* [K-73] 对标 Qt swapSections 尾部的末段随动（同 moveSection 的
     * K-73 注；swap 若把末段换出，引擎统一复原/重排）。 */
    xhv_stretchMaybe(self);
}

void XHeaderView_moveSection(XHeaderView* self, int from, int to)
{
    int i;
    int n;
    int logical;
    if (!self || !self->m_sections || from < 0 || to < 0) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (from >= n || to >= n) return;
    xhv_mapSyncCount(self, n);
    if (from == to) {
        /* 对标 Qt：同位置只刷新该段（updateSection(logical)），不发
         * sectionMoved。 */
        logical = XHeaderView_logicalIndex(self, from);
        if (logical >= 0) XWidget_update((XWidget*)self);
        return;
    }
    /* 对标 Qt 6.8 moveSection（qheaderview.cpp:699-759）：参数是**视觉**
     * 序号（文档 "Moves the section at visual index from to occupy visual
     * index to"）；尺寸/隐藏/模式按逻辑段号存放，移动只改映射。载荷
     * sectionMoved(该段逻辑号, from, to)——Qt 实测 moveSection(0,2) 在
     * 恒等映射上发 (0,0,2)。 */
    logical = XVector_At_Base(self->m_visualToLogical, (int64_t)from, int);
    if (from < to) {
        for (i = from; i < to; ++i)
            XVector_At_Base(self->m_visualToLogical, (int64_t)i, int) =
                XVector_At_Base(self->m_visualToLogical, (int64_t)(i + 1),
                                int);
    } else {
        for (i = from; i > to; --i)
            XVector_At_Base(self->m_visualToLogical, (int64_t)i, int) =
                XVector_At_Base(self->m_visualToLogical, (int64_t)(i - 1),
                                int);
    }
    XVector_At_Base(self->m_visualToLogical, (int64_t)to, int) = logical;
    self->m_sectionMovedFrom = from;
    /* 对标 Qt sectionsMoved：发生过实际移动后恒为 true。 */
    self->m_sectionsMovedFlag = 1;
    xhv_emitInt3(self, (size_t)XHeaderView_sectionMoved_signal(
                      self, logical, from, to), logical, from, to);
    xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    /* [K-73] 对标 Qt moveSection 尾部的
     * maybeRestorePrevLastSectionAndStretchLast（qheaderview.cpp:757-759，
     * from/to 撞末段时旧末段复原、新末段吃满余量）：末段身份的比对与
     * 重排在引擎内统一判定。 */
    xhv_stretchMaybe(self);
}

void XHeaderView_setSortIndicator(XHeaderView* self, int section, int order)
{
    if (!self) return;
    if (section < -1) section = -1;
    if (self->m_sortIndicatorSection == section &&
        self->m_sortIndicatorOrder == order)
        return;
    self->m_sortIndicatorSection = section;
    self->m_sortIndicatorOrder = order;
    /* [Qt 6.8 对齐 2026-11-24] 只落段号/方向，**不改变可见性**。
     * Qt 源码（qheaderview.cpp）中 sortIndicatorShown 仅出现在
     * setSortIndicatorShown/isSortIndicatorShown/initStyleOptionForIndex
     * 三处：setSortIndicator 与点击链的 flipSortIndicator 都不点它；可见性
     * 由应用显式 setSortIndicatorShown(true) 打开（QTableView/QTreeView
     * 的 setSortingEnabled(true) 就是先点它）。此前这里顺带置 true ⇒
     * 任何可点表头被单击后都会"冒出"排序箭头，即使表格根本没启用排序
     * （HsfVfdProbe 主界面设备表「单击表头无排序指示」由此恒红）。 */
    xhv_emitInt(self, (size_t)XHeaderView_sortIndicatorChanged_signal(
                          self, section, order), section);
}

int XHeaderView_sortIndicatorSection(const XHeaderView* self)
{ return self ? self->m_sortIndicatorSection : -1; }

int XHeaderView_sortIndicatorOrder(const XHeaderView* self)
{ return self ? self->m_sortIndicatorOrder : 0; }

void XHeaderView_setSortIndicatorShown(XHeaderView* self, bool shown)
{ if (self) self->m_sortIndicatorShown = shown; }

bool XHeaderView_isSortIndicatorShown(const XHeaderView* self)
{ return self ? self->m_sortIndicatorShown : false; }

void XHeaderView_setSortIndicatorClearable(XHeaderView* self, bool clearable)
{
    if (!self || self->m_sortIndicatorClearable == clearable) return;
    self->m_sortIndicatorClearable = clearable;
    /* 对标 Qt：状态实际变化时发射 sortIndicatorClearableChanged。 */
    xhv_emitBool(self, (size_t)XHeaderView_sortIndicatorClearableChanged_signal(
                          self, clearable), clearable);
}

bool XHeaderView_isSortIndicatorClearable(const XHeaderView* self)
{ return self ? self->m_sortIndicatorClearable : false; }

/* ==================== 尺寸上下限/模式与状态（对标 QHeaderView min/max/resizeMode 族） ==================== */

void XHeaderView_setMaximumSectionSize(XHeaderView* self, int size)
{
    int n;
    int i;
    if (!self) return;
    if (size == -1) {
        /* Qt 语义：-1 表示重置为段尺寸硬上限。 */
        self->m_maximumSectionSize = XHEADERVIEW_SECTION_SIZE_LIMIT;
        return;
    }
    if (size < 0 || size > XHEADERVIEW_SECTION_SIZE_LIMIT) return;
    /* 上限落库（此前漏写：仅钳制既有段/抬高下限，maximumSectionSize
       查询恒返回缺省值，resizeSection 的钳制随之失效）。 */
    self->m_maximumSectionSize = size;
    /* 新上限低于既有限制时，Qt 同步抬高最小值，维持 min<=max 不变式。 */
    if (self->m_minimumSectionSize > size)
        self->m_minimumSectionSize = size;
    if (!self->m_sections) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(self->m_sections, (int64_t)i, int);
        if (v > size)
            XVector_At_Base(self->m_sections, (int64_t)i, int) = size;
    }
}

int XHeaderView_maximumSectionSize(const XHeaderView* self)
{ return self ? self->m_maximumSectionSize : 0; }

void XHeaderView_setMinimumSectionSize(XHeaderView* self, int size)
{
    int n;
    int i;
    if (!self || size < 0 || size > XHEADERVIEW_SECTION_SIZE_LIMIT) return;
    /* 新下限超过既有限制时，Qt 同步抬高最大值，维持 min<=max 不变式。 */
    if (size > self->m_maximumSectionSize)
        self->m_maximumSectionSize = size;
    if (!self->m_sections) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        int v = XVector_At_Base(self->m_sections, (int64_t)i, int);
        if (v < size)
            XVector_At_Base(self->m_sections, (int64_t)i, int) = size;
    }
}

int XHeaderView_minimumSectionSize(const XHeaderView* self)
{ return self ? self->m_minimumSectionSize : 0; }

void XHeaderView_setSectionResizeMode(XHeaderView* self, int mode)
{
    int n;
    int i;
    if (!self) return;
    if (mode < (int)XHeaderViewResizeMode_Interactive ||
        mode > (int)XHeaderViewResizeMode_ResizeToContents)
        return;
    self->m_resizeMode = mode;
    /* 对标 Qt setGlobalHeaderResizeMode：全局模式覆写全部段的单段
     * 模式；本实现以清除全部单段覆写（复位 -1=跟随全局）达成等价
     * 语义。 */
    if (self->m_sectionModes) {
        n = (int)XVector_size_base((const XContainer*)self->m_sectionModes);
        for (i = 0; i < n; ++i)
            XVector_At_Base(self->m_sectionModes, (int64_t)i, int) = -1;
    }
    /* 对标 Qt setSectionResizeMode(ResizeMode)：设置全局模式后，模式为
     * Stretch/ResizeToContents（hasAutoResizeSections）时调度
     * doDelayedResizeSections（等效 resizeSections(mode)）；本实现收敛
     * 为同步调用——Stretch 以当前 length 充当可用空间在可见段间均分
     * （几何实际变化时发射 geometriesChanged，见 resizeSections 的批量
     * 路径 @note），其余模式内部仅调度全量重绘。 */
    XHeaderView_resizeSections(self, mode);
}

int XHeaderView_sectionResizeMode(const XHeaderView* self)
{
    return self ? self->m_resizeMode
                : (int)XHeaderViewResizeMode_Interactive;
}

void XHeaderView_resizeSections(XHeaderView* self, int mode)
{
    int n;
    int i;
    int total = 0;
    int visible = 0;
    int stretchLen = 0;
    int reminder = 0;
    int snapLogical = -1;
    int snapSize = -1;
    bool changed = false;
    if (!self || !self->m_sections) return;
    if (mode < (int)XHeaderViewResizeMode_Interactive ||
        mode > (int)XHeaderViewResizeMode_ResizeToContents)
        return;
    if (mode != (int)XHeaderViewResizeMode_Stretch) {
        /* Interactive/Fixed/ResizeToContents 保现尺寸（Qt 对
         * ResizeToContents 走内容测算，本库已由 sectionSizeFromContents
         * 虚槽承载但批量路径不改写段宽，仅调度全量重绘，见头文件
         * @note）。 */
        XWidget_update((XWidget*)self);
        return;
    }
    /* [§B3 2026-11-15] Stretch 全局池：对齐 Qt
     * QHeaderViewPrivate::resizeSections（qheaderview.cpp:3529 函数体，
     * 池计算 :3588-3632，useGlobalMode=true）与 K-73 引擎 / XTableWidget
     * 视图侧分摊器 xtw_stretchColumnsToViewport 同一口径：
     *  ① lengthToStretch = **宿主推送的视口内容区可用长**
     *     （m_stretchViewportLen，由 setStretchViewportLength/
     *     syncViewportLength 落地，XTableWidget 在 resize/行号列显隐处推
     *     真实视口宽−行号列宽）；**未推送（<0）才回落到「可见段现宽和」**
     *     ——纯表头独立使用时没有视口，用现宽和保持既有可用值（Qt 取
     *     viewport->width()，纯表头场景在 Qt 中对应自持视口，语义等价）；
     *  ② 余量 <= 0 ⇒ 整体跳过、各段保现宽（:3591 门）；
     *  ③ stretchLen = qMax(余量/可见段数, minimumSectionSize)（:3592-3593）；
     *  ④ pixelReminder = 余量 % 可见段数，按**视觉序前置分摊**
     *     逐可见段 +1（:3624-3626）——旧口径 `total/visible` 整段取整、
     *     余数整份丢弃（且不给段发 sectionResized ⇒ 视图侧 m_colWidths
     *     与表头段「暂歧」），本批两处一并消除；
     *  ⑤ 末段 qMax(stretchLen, lastSectionSize)（:3619-3623，快照仅
     *     stretchLastSection 开启时受信任，qheaderview_p.h:71）；
     *  ⑥ 逐段钳制到 [min,max]，且**逐段发 sectionResized** 让视图侧
     *     xtv_hHeaderSectionResizedSlot 即时镜像 m_colWidths（切模式后
     *     不再暂歧）。几何真变时补发一次 geometriesChanged。
     *  [为何逐段发信号（推翻旧的"批量只整体刷新"先例）] 旧注释以
     *  restoreState 的批量口径为由不逐段发；但那会让视图侧 m_colWidths
     *  停在旧值（镜像只挂 sectionResized）⇒ 切模式后表头段与单元格列宽
     *  分叉，正是本单要消灭的「暂歧」。Qt 于此逐段发射
     *  （:3642-3643 `newSectionLength != oldSectionLength` 门），故按 Qt 改，
     *  并把 geometriesChanged 收敛为一次（AGENTS 重绘纪律：不做信号风暴）。 */
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        if (XHeaderView_isSectionHidden(self, i)) continue;
        total += XVector_At_Base(self->m_sections, (int64_t)i, int);
        ++visible;
    }
    if (visible <= 0) return;
    /* ① 可用长：宿主推送的视口长优先（未推送 <0 才用现宽和）。 */
    if (self->m_stretchViewportLen > 0)
        total = self->m_stretchViewportLen;
    if (total <= 0) return;              /* Qt :3591 门：无余量保现宽。 */
    stretchLen = total / visible;
    if (stretchLen < self->m_minimumSectionSize)
        stretchLen = self->m_minimumSectionSize;
    reminder = total % visible;
    XHeaderView_lastSectionSnapshot(self, &snapLogical, &snapSize);
    /* 按视觉序应用（对标 Qt :3604-3648 外层视觉序循环）。逐段直写尺寸表
     * 并发 sectionResized（视图侧 xtv_hHeaderSectionResizedSlot 借此镜像
     * m_colWidths），绕开 resizeSection 的末段快照钩（Qt 池应用走
     * createSectionItems、不经 resizeSection）；尺寸真变只补发一次
     * geometriesChanged，避免逐段发射造成信号风暴（AGENTS 重绘纪律）。 */
    for (i = 0; i < n; ++i) {
        int logical = XHeaderView_logicalIndex(self, i);
        int oldSize;
        int newSize;
        if (logical < 0 || XHeaderView_isSectionHidden(self, logical))
            continue;
        newSize = stretchLen;
        if (logical == snapLogical && snapSize > newSize)
            newSize = snapSize;            /* :3619-3623 末段快照下限。 */
        if (reminder > 0) { ++newSize; --reminder; } /* 余数前置分摊。 */
        newSize = xhv_clampSize(self, newSize);
        oldSize = XVector_At_Base(self->m_sections, (int64_t)logical, int);
        if (newSize == oldSize) continue;
        XVector_At_Base(self->m_sections, (int64_t)logical, int) = newSize;
        xhv_emitInt3(self, (size_t)XHeaderView_sectionResized_signal(
                          self, logical, oldSize, newSize),
                     logical, oldSize, newSize);
        changed = true;
    }
    if (changed)
        xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    xheaderview_updateSizeHint((XHeaderView*)self);
    XWidget_update((XWidget*)self);
}

void XHeaderView_setSectionResizeModeAt(XHeaderView* self, int section,
                                        int mode)
{
    int n;
    if (!self || !self->m_sections || section < 0) return;
    /* -1 哨兵=清除覆写（读侧以越界值判"未设置"回落全局模式），其余
       非法模式值仍拒绝。 */
    if (mode != -1 &&
        (mode < (int)XHeaderViewResizeMode_Interactive ||
         mode > (int)XHeaderViewResizeMode_ResizeToContents))
        return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (section >= n || !self->m_sectionModes) return;
    XVector_At_Base(self->m_sectionModes, (int64_t)section, int) = mode;
    /* [K-73] 对标 Qt setSectionResizeMode(int,int) 尾部的
     * doDelayedResizeSections（qheaderview.cpp:1218）：单段模式变化
     * 影响末段的池归属（Interactive ⇄ Stretch），随动重排末段。 */
    xhv_stretchMaybe(self);
}

int XHeaderView_sectionResizeModeAt(const XHeaderView* self, int logicalIndex)
{
    int n;
    int mode;
    if (!self) return (int)XHeaderViewResizeMode_Interactive;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (logicalIndex >= 0 && logicalIndex < n && self->m_sectionModes) {
        mode = XVector_At_Base(self->m_sectionModes,
                               (int64_t)logicalIndex, int);
        if (mode >= (int)XHeaderViewResizeMode_Interactive &&
            mode <= (int)XHeaderViewResizeMode_ResizeToContents)
            return mode;
    } else if (n > 0) {
        /* 越界：对齐 Qt「visualIndex == -1 ⇒ 返回 Fixed（the default
         * value）」（qheaderview.cpp:1230-1237；本机实测 3 段表
         * sectionResizeMode(5)=2=Fixed）。无段表（count=0）时 Qt 同样
         * 落此分支，但全局模式尚无段可派发，保留全局模式读取。 */
        return (int)XHeaderViewResizeMode_Fixed;
    }
    /* 无覆写（-1）：返回全局模式。 */
    return self->m_resizeMode;
}

void XHeaderView_setFirstSectionMovable(XHeaderView* self, bool movable)
{ if (self) self->m_firstSectionMovable = movable; }

bool XHeaderView_isFirstSectionMovable(const XHeaderView* self)
{
    /* 对标 QHeaderView::isFirstSectionMovable：返回**存储标志本身**
     * （qheaderview.cpp:1120-1124 `return d->allowUserMoveOfSection0;`），
     * 不参与 movable 门控——"true 还需 setSectionsMovable(true) 才生效"
     * 是行为约束，不是读取语义（Qt 文档 :1108-1109）。 */
    return self ? (self->m_firstSectionMovable != 0) : false;
}

void XHeaderView_reset(XHeaderView* self)
{
    int n;
    int i;
    if (!self || !self->m_sections) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        XVector_At_Base(self->m_sections, (int64_t)i, int) =
            self->m_defaultSize;
        if (self->m_sectionModes)
            XVector_At_Base(self->m_sectionModes, (int64_t)i, int) = -1;
        if (self->m_hidden)
            XVector_At_Base(self->m_hidden, (int64_t)i, bool) = false;
    }
    self->m_hiddenCount = 0;
    self->m_sectionMovedFrom = -1;
    /* 视觉序复位为单位映射（Qt QHeaderViewPrivate::clear 清 visualIndices/
     * logicalIndices ⇒ sectionsMoved 回到 false）。 */
    for (i = 0; i < n; ++i) {
        if (self->m_visualToLogical)
            XVector_At_Base(self->m_visualToLogical, (int64_t)i, int) = i;
    }
    self->m_sectionsMovedFlag = 0;
    /* 清空排序指示器（静默复位，不发信号；Qt reset 亦不发射）。 */
    self->m_sortIndicatorShown = false;
    self->m_sortIndicatorSection = -1;
    self->m_sortIndicatorOrder = 0;
    if (n > 0) {
        /* 尺寸与隐藏状态的恢复影响表头几何，发 geometriesChanged。 */
        xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
        /* [K-73] 恢复缺省后末段随 stretchLastSection 重排（Qt reset 走
         * initializeSections 的延迟重排，本库同步）。 */
        xhv_stretchMaybe(self);
    }
}

void XHeaderView_setDefaultAlignment(XHeaderView* self, int alignment)
{ if (self) self->m_defaultAlignment = alignment; }

int XHeaderView_defaultAlignment(const XHeaderView* self)
{ return self ? self->m_defaultAlignment : 0; }

int XHeaderView_stretchSectionCount(const XHeaderView* self)
{
    int i;
    int count = 0;
    if (!self) return 0;
    /* [P0-77 2026-11-12] Qt 口径：生效模式为 Stretch 的段数
     * （qheaderview.cpp:1291-1295 直接返回 d->stretchSections；该计数在
     * setSectionResizeMode(ResizeMode) 置 `mode == Stretch ? count() : 0`
     * （:1178）、单段设置时按**新旧生效模式**增减（:1209/:1213）、段数
     * 变化时跟随（:1899/:2304）），**不含** stretchLastSection。此前返回
     * `m_stretchLast ? 1 : 0` 并把 Qt 口径写反 ⇒ 全局 Stretch 的表
     * （如数据表 HsfDataTable.c:179）返回 0，Qt 返回列数。
     *
     * 逐段取**生效模式**（单段覆写优先、否则全局，正是 Qt 计数器的
     * 语义），与 XHeaderView_sectionResizeModeAt 同一判定入口。 */
    for (i = 0; i < self->m_count; ++i) {
        if (XHeaderView_sectionResizeModeAt(self, i) ==
            (int)XHeaderViewResizeMode_Stretch)
            ++count;
    }
    return count;
}

bool XHeaderView_sectionsHidden(const XHeaderView* self)
{ return self ? (self->m_hiddenCount > 0) : false; }

bool XHeaderView_sectionsMoved(const XHeaderView* self)
{ return self ? (self->m_sectionsMovedFlag != 0) : false; }

void XHeaderView_setHighlightSections(XHeaderView* self, bool highlight)
{ if (self) self->m_highlightSections = highlight; }

bool XHeaderView_highlightSections(const XHeaderView* self)
{ return self ? self->m_highlightSections : false; }

void XHeaderView_setCascadingSectionResizes(XHeaderView* self, bool enable)
{ if (self) self->m_cascadingResizes = enable; }

bool XHeaderView_cascadingSectionResizes(const XHeaderView* self)
{ return self ? self->m_cascadingResizes : false; }

void XHeaderView_setResizeContentsPrecision(XHeaderView* self, int precision)
{ if (self) self->m_resizeContentsPrecision = precision; }

int XHeaderView_resizeContentsPrecision(const XHeaderView* self)
{ return self ? self->m_resizeContentsPrecision : 0; }

/* ==================== 布局与状态持久化（对标 doItemsLayout/saveState/restoreState） ==================== */

void XHeaderView_doItemsLayout(XHeaderView* self)
{
    if (!self) return;
    /* 对标 Qt doItemsLayout：Qt 会重算段几何并驱动一次模型布局；本项目
     * 段几何即时维护、表头渲染由 XTableView 统一完成，此处仅调度整控件
     * 全量重绘（等效"全量 update"，见头文件 @note）。 */
    XWidget_update((XWidget*)self);
}

#if XByteArray_ON

/** @brief 状态序列化格式版本（XHV 文本格式 v2：v1 追加视觉序映射；字段结构变更时递增）。 */
#define XHEADERVIEW_STATE_VERSION "002"
/** @brief 兼容读取的 v1 格式版本（无视觉序映射，恢复为单位映射）。 */
#define XHEADERVIEW_STATE_VERSION_V1 "001"
/** @brief 状态序列化整数字段位宽（含符号位；可表示 -999999..9999999）。 */
#define XHEADERVIEW_STATE_INT_WIDTH 7

/** @brief 追加 1 位数字字段（布尔 0/1 或方向 0/1）。 */
static bool xhv_stateWriteDigit(XByteArray* out, int value)
{
    char buf[2];
    if (!out || value < 0 || value > 9) return false;
    buf[0] = (char)('0' + value);
    buf[1] = '\0';
    return XByteArray_append_utf8(out, buf);
}

/** @brief 追加固定位宽整数字段（%0*d 语义；负数占符号位）。 */
static bool xhv_stateWriteInt(XByteArray* out, int value)
{
    char buf[16];
    if (!out) return false;
    if (XSnprintf(buf, sizeof(buf), "%0*d",
                  XHEADERVIEW_STATE_INT_WIDTH, value) !=
        XHEADERVIEW_STATE_INT_WIDTH)
        return false;
    return XByteArray_append_utf8(out, buf);
}

/** @brief 追加单段模式字段：'0'..'3' 为显式模式，'g' 为跟随全局（-1）。 */
static bool xhv_stateWriteMode(XByteArray* out, int mode)
{
    char buf[2];
    if (!out) return false;
    if (mode >= (int)XHeaderViewResizeMode_Interactive &&
        mode <= (int)XHeaderViewResizeMode_ResizeToContents)
        buf[0] = (char)('0' + mode);
    else if (mode == -1)
        buf[0] = 'g';
    else
        return false;
    buf[1] = '\0';
    return XByteArray_append_utf8(out, buf);
}

/** @brief 读取固定位宽整数字段（支持前导负号；宽度不足或含非数字失败）。 */
static bool xhv_stateReadInt(const char** cursor, const char* end, int* out)
{
    const char* p;
    int sign = 1;
    int value = 0;
    int consumed = 0;
    if (!cursor || !*cursor || !out) return false;
    p = *cursor;
    if (end - p < XHEADERVIEW_STATE_INT_WIDTH) return false;
    /* 跳过前导空格(WriteInt 的 %0*d 对负数产生空格前缀)。 */
    while (p < end && *p == ' ') ++p;
    if (*p == '-') {
        sign = -1;
        ++p;
        ++consumed;
    }
    for (; consumed < XHEADERVIEW_STATE_INT_WIDTH; ++consumed, ++p) {
        if (*p < '0' || *p > '9') return false;
        value = value * 10 + (*p - '0');
    }
    *out = sign * value;
    *cursor = p;
    return true;
}

/** @brief 读取 1 字符字段（越界失败）。 */
static bool xhv_stateReadChar(const char** cursor, const char* end, char* out)
{
    if (!cursor || !*cursor || !out) return false;
    if (end - *cursor < 1) return false;
    *out = **cursor;
    ++(*cursor);
    return true;
}

/** @brief 释放 restoreState 校验阶段的段字段暂存数组（三指针可为 NULL）。 */
static void xhv_freeStateArrays(int* sizes, bool* hiddens, int* modes)
{
    if (sizes) XFree_System(sizes);
    if (hiddens) XFree_System(hiddens);
    if (modes) XFree_System(modes);
}

XByteArray* XHeaderView_saveState(const XHeaderView* self)
{
    XByteArray* out;
    int n;
    int i;
    bool ok;
    if (!self) return NULL;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    out = XByteArray_create();
    if (!out) return NULL;
    /* 字段顺序必须与 XHeaderView_restoreState 的解析严格对称（格式见
     * 头文件 @note；对标 Qt QHeaderViewPrivate::write 的状态集合）。 */
    ok = XByteArray_append_utf8(out, "XHV" XHEADERVIEW_STATE_VERSION) &&
         xhv_stateWriteInt(out, self->m_orientation) &&
         xhv_stateWriteInt(out, n) &&
         xhv_stateWriteInt(out, self->m_defaultSize) &&
         xhv_stateWriteInt(out, self->m_minimumSectionSize) &&
         xhv_stateWriteInt(out, self->m_maximumSectionSize) &&
         xhv_stateWriteDigit(out, self->m_stretchLast ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_sortIndicatorShown ? 1 : 0) &&
         xhv_stateWriteInt(out, self->m_sortIndicatorSection) &&
         xhv_stateWriteDigit(out,
                             self->m_sortIndicatorOrder ==
                                     (int)XHeaderViewSortOrder_Descending
                                 ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_sortIndicatorClearable ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_sectionsClickable ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_sectionsMovable ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_firstSectionMovable ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_highlightSections ? 1 : 0) &&
         xhv_stateWriteDigit(out, self->m_cascadingResizes ? 1 : 0) &&
         xhv_stateWriteInt(out, self->m_resizeContentsPrecision) &&
         xhv_stateWriteInt(out, self->m_defaultAlignment);
    for (i = 0; ok && i < n; ++i) {
        int mode = self->m_sectionModes
                ? XVector_At_Base(self->m_sectionModes, (int64_t)i, int)
                : -1;
        ok = xhv_stateWriteInt(out, XVector_At_Base(self->m_sections,
                                                    (int64_t)i, int)) &&
             xhv_stateWriteDigit(out,
                                 (self->m_hidden &&
                                  XVector_At_Base(self->m_hidden,
                                                  (int64_t)i, bool))
                                     ? 1 : 0) &&
             xhv_stateWriteMode(out, mode);
    }
    /* v2：视觉序映射（按视觉序写 n 个逻辑段号；对齐 Qt
     * QHeaderViewPrivate::write 持久化 visualIndices 的语义）。 */
    for (i = 0; ok && i < n; ++i) {
        int logical = (self->m_visualToLogical &&
                       i < xhv_mapCount(self))
                ? XVector_At_Base(self->m_visualToLogical, (int64_t)i, int)
                : i;
        ok = xhv_stateWriteInt(out, logical);
    }
    if (!ok) {
        XClassDelete(out);
        return NULL;
    }
    return out;
}

bool XHeaderView_restoreState(XHeaderView* self, const XByteArray* state)
{
    const char* data;
    const char* cur;
    const char* end;
    int orientation;
    int count;
    int defaultSize;
    int minSize;
    int maxSize;
    int stretchLast;
    int sortShown;
    int sortSection;
    int sortOrder;
    int sortClearable;
    int clickable;
    int movable;
    int firstMovable;
    int highlight;
    int cascading;
    int precision;
    int alignment;
    int* sizes = NULL;
    bool* hiddens = NULL;
    int* modes = NULL;
    int* visual = NULL;   /**< v2：视觉序映射暂存（按视觉序的逻辑段号）。 */
    bool hasMapping = false;
    bool isV2;
    int i;
    bool changed;
    if (!self || !state) return false;
    data = (const char*)XByteArray_constData((XByteArray*)state); /* 只读借用。 */
    if (!data) return false;
    end = data + (int)XByteArray_size_base((const XContainer*)state);
    cur = data;
    /* 校验先行：任何字段非法即整体拒绝（不做部分恢复）；失败路径
     * 静默（生产路径无诊断输出，§8.0c2/R4 静默化纪律——此前 25 处
     * [RS-fail] fprintf 已移除）。 */
    if (end - cur < 6 || XStrncmp(cur, "XHV", 3) != 0)
        return false;
    isV2 = XStrncmp(cur + 3, XHEADERVIEW_STATE_VERSION, 3) == 0;
    if (!isV2 && XStrncmp(cur + 3, XHEADERVIEW_STATE_VERSION_V1, 3) != 0)
        return false;
    cur += 6;
    if (!xhv_stateReadInt(&cur, end, &orientation) ||
        (orientation != 0 && orientation != 1) ||
        orientation != self->m_orientation)
        return false;
    if (!xhv_stateReadInt(&cur, end, &count) || count < 0 ||
        count > XHEADERVIEW_SECTION_SIZE_LIMIT)
        return false;
    if (!xhv_stateReadInt(&cur, end, &defaultSize) || defaultSize <= 0)
        return false;
    if (!xhv_stateReadInt(&cur, end, &minSize) || minSize < 0 ||
        minSize > XHEADERVIEW_SECTION_SIZE_LIMIT)
        return false;
    if (!xhv_stateReadInt(&cur, end, &maxSize) || maxSize <= 0 ||
        maxSize > XHEADERVIEW_SECTION_SIZE_LIMIT || minSize > maxSize)
        return false;
    {
        char flag;
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        stretchLast = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        sortShown = (flag == '1');
        if (!xhv_stateReadInt(&cur, end, &sortSection) ||
            sortSection < -1 || sortSection >= count)
            return false;
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        sortOrder = (flag == '1')
                ? (int)XHeaderViewSortOrder_Descending
                : (int)XHeaderViewSortOrder_Ascending;
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        sortClearable = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        clickable = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        movable = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        firstMovable = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        highlight = (flag == '1');
        if (!xhv_stateReadChar(&cur, end, &flag) ||
            (flag != '0' && flag != '1'))
            return false;
        cascading = (flag == '1');
    }
    if (!xhv_stateReadInt(&cur, end, &precision) || precision < -1)
        return false;
    if (!xhv_stateReadInt(&cur, end, &alignment) || alignment < 0)
        return false;
    /* 每段尺寸/隐藏/模式字段；校验同时暂存（应用阶段使用）。 */
    if (count > 0) {
        sizes = (int*)XMalloc_System(sizeof(int) * (size_t)count);
        hiddens = (bool*)XMalloc_System(sizeof(bool) * (size_t)count);
        modes = (int*)XMalloc_System(sizeof(int) * (size_t)count);
        if (!sizes || !hiddens || !modes) {
            XFree_System(sizes);
            XFree_System(hiddens);
            XFree_System(modes);
            return false;
        }
    }
    for (i = 0; i < count; ++i) {
        char hidden;
        char mode;
        int size;
        if (!xhv_stateReadInt(&cur, end, &size) || size < 0 ||
            size > XHEADERVIEW_SECTION_SIZE_LIMIT) {
            xhv_freeStateArrays(sizes, hiddens, modes);
            return false;
        }
        if (!xhv_stateReadChar(&cur, end, &hidden) ||
            (hidden != '0' && hidden != '1')) {
            xhv_freeStateArrays(sizes, hiddens, modes);
            return false;
        }
        if (!xhv_stateReadChar(&cur, end, &mode) ||
            (mode != 'g' &&
             (mode < '0' ||
              mode > '0' + (int)XHeaderViewResizeMode_ResizeToContents))) {
            xhv_freeStateArrays(sizes, hiddens, modes);
            return false;
        }
        sizes[i] = size;
        hiddens[i] = (hidden == '1');
        modes[i] = (mode == 'g')
                ? -1
                : (int)(mode - '0');
    }
    /* v2：视觉序映射（n 个逻辑段号，按视觉序；必须是 0..count-1 的排列）。 */
    if (isV2 && count > 0) {
        int j;
        visual = (int*)XMalloc_System(sizeof(int) * (size_t)count);
        if (!visual) {
            xhv_freeStateArrays(sizes, hiddens, modes);
            return false;
        }
        for (i = 0; i < count; ++i) {
            int logical;
            if (!xhv_stateReadInt(&cur, end, &logical) || logical < 0 ||
                logical >= count) {
                XFree_System(visual);
                xhv_freeStateArrays(sizes, hiddens, modes);
                return false;
            }
            visual[i] = logical;
        }
        for (i = 0; i < count; ++i) {
            int seen = 0;
            for (j = 0; j < count; ++j) {
                if (visual[j] == i) ++seen;
            }
            if (seen != 1) {
                XFree_System(visual);
                xhv_freeStateArrays(sizes, hiddens, modes);
                return false;
            }
        }
        hasMapping = true;
    }
    if (cur != end) {
        XFree_System(visual);
        xhv_freeStateArrays(sizes, hiddens, modes);
        return false;
    }
    /* ==================== 应用阶段 ==================== */
    /* 段数变化经 setCount 发射 sectionCountChanged/geometriesChanged。 */
    XHeaderView_setCount(self, count);
    /* 视觉序映射：v2 照抄暂存表；v1（无映射字段）恢复单位映射
     * （对标 Qt 恢复状态后 visualIndices 与状态一致）。 */
    if (self->m_visualToLogical) {
        xhv_mapSyncCount(self, count);
        self->m_sectionsMovedFlag = 0; /* 按恢复后的映射重算 */
        for (i = 0; i < count; ++i) {
            int logical = hasMapping ? visual[i] : i;
            XVector_At_Base(self->m_visualToLogical, (int64_t)i, int) =
                logical;
            if (logical != i) self->m_sectionsMovedFlag = 1;
        }
    }
    XFree_System(visual);
    visual = NULL;
    XHeaderView_setMaximumSectionSize(self, maxSize);
    XHeaderView_setMinimumSectionSize(self, minSize);
    XHeaderView_setDefaultSectionSize(self, defaultSize);
    self->m_stretchLast = stretchLast;
    XHeaderView_setSectionsClickable(self, clickable);
    XHeaderView_setSectionsMovable(self, movable);
    XHeaderView_setFirstSectionMovable(self, firstMovable);
    XHeaderView_setHighlightSections(self, highlight);
    XHeaderView_setCascadingSectionResizes(self, cascading);
    self->m_resizeContentsPrecision = precision;
    XHeaderView_setDefaultAlignment(self, alignment);
    XHeaderView_setSortIndicatorClearable(self, sortClearable);
    /* 恢复排序指示器：setSortIndicator 在状态实际变化时发射
     * sortIndicatorChanged（Qt 恢复后无条件发射，此处收敛为变化才发）。 */
    XHeaderView_setSortIndicator(self, sortSection, sortOrder);
    XHeaderView_setSortIndicatorShown(self, sortShown);
    /* 逐段直写尺寸/隐藏/模式：不逐段发 sectionResized（对标 Qt 恢复
     * 路径仅整体刷新），几何有实际变化时追加一次 geometriesChanged。 */
    changed = false;
    for (i = 0; i < count && self->m_sections; ++i) {
        int newSize = sizes[i];
        int oldSize;
        if (self->m_sectionModes)
            XVector_At_Base(self->m_sectionModes, (int64_t)i, int) =
                modes[i];
        newSize = xhv_clampSize(self, newSize);
        oldSize = XVector_At_Base(self->m_sections, (int64_t)i, int);
        if (newSize != oldSize) {
            XVector_At_Base(self->m_sections, (int64_t)i, int) = newSize;
            changed = true;
        }
        if (self->m_hidden) {
            bool cur = XVector_At_Base(self->m_hidden, (int64_t)i, bool);
            if (hiddens[i] != cur) {
                if (hiddens[i] && !cur) ++self->m_hiddenCount;
                else if (!hiddens[i] && cur) --self->m_hiddenCount;
                XVector_At_Base(self->m_hidden, (int64_t)i, bool) =
                    hiddens[i];
                changed = true;
            }
        }
    }
    xhv_freeStateArrays(sizes, hiddens, modes);
    if (changed)
        xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    return true;
}

#endif /* XByteArray_ON */

int XHeaderView_sectionSize(const XHeaderView* self, int section)
{
    int n;
    if (!self || !self->m_sections || section < 0) return 0;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    /* 对标 Qt 6.8 sectionSize（qheaderview.cpp:600-614）：越界段返回
     * **0**（旧实现回落 defaultSectionSize，gap 156；Qt 实测 4 段表
     * sectionSize(9)==0、sectionSize(-1)==0）。 */
    if (section >= n) return 0;
    /* 隐藏段返回 0（尺寸本身仍留在尺寸表里，show 后原值恢复）。 */
    if (XHeaderView_isSectionHidden(self, section)) return 0;
    return XVector_At_Base(self->m_sections, (int64_t)section, int);
}

void XHeaderView_setSectionSize(XHeaderView* self, int section, int size)
{
    int n;
    int oldSize;
    if (!self || !self->m_sections || section < 0 || size < 0) return;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    if (section >= n) return;
    oldSize = XVector_At_Base(self->m_sections, (int64_t)section, int);
    XVector_At_Base(self->m_sections, (int64_t)section, int) = size;
    if (size == oldSize) return; /* 对标 Qt：尺寸未变不发信号。 */
    /* 对标 Qt emitSectionResized + updateGeometries→geometriesChanged。 */
    xhv_emitInt3(self, (size_t)XHeaderView_sectionResized_signal(
                      self, section, oldSize, size), section, oldSize, size);
    xhv_emitVoid(self, (size_t)XHeaderView_geometriesChanged_signal(self));
    xheaderview_updateSizeHint((XHeaderView*)self);
}

int XHeaderView_defaultSectionSize(const XHeaderView* self)
{ return self ? self->m_defaultSize : 0; }

void XHeaderView_setDefaultSectionSize(XHeaderView* self, int size)
{
    if (self && size > 0) self->m_defaultSize = size;
}

void XHeaderView_resetDefaultSectionSize(XHeaderView* self)
{
    /* 对标 Qt resetDefaultSectionSize：恢复为样式/字体测算的缺省值；
     * 本库按方向取 Qt 6.8.0 实测常量（水平 100 / 垂直 30），已有段尺寸
     * 不受影响（见 XHEADERVIEW_DEFAULT_SECTION_SIZE_H/V）。 */
    if (self)
        self->m_defaultSize =
            (int16_t)xhv_defaultSectionSizeFor(self->m_orientation);
}

int XHeaderView_sectionPosition(const XHeaderView* self, int section)
{
    int pos;
    int v;
    int vis;
    if (!self || !self->m_sections || section < 0) return -1;
    if (section >= xhv_mapCount(self)) return -1;
    /* 对标 QHeaderView::sectionPosition：隐藏段返回 -1（本项目口径，
     * 见头文件 @note）；可见段位置按**视觉序**累加本次实现
     * （sectionSize 已按隐藏段计 0），与 Qt 的 logicalIndex→visualIndex
     * →headerSectionPosition 同构。 */
    if (XHeaderView_isSectionHidden(self, section)) return -1;
    vis = XHeaderView_visualIndex(self, section);
    if (vis < 0) return -1;
    pos = 0;
    for (v = 0; v < vis; ++v) {
        int logical = XVector_At_Base(self->m_visualToLogical,
                                      (int64_t)v, int);
        pos += XHeaderView_sectionSize(self, logical);
    }
    return pos;
}

int XHeaderView_sectionViewportPosition(const XHeaderView* self, int section)
{
    int position;
    /* [K-64 2026-11-15] 对齐 Qt 6.8 sectionViewportPosition
     * （qheaderview.cpp:645-656）：越界（>=count）返回 -1；隐藏段
     * sectionPosition 返回 -1 时原样回传；否则 position − offset。
     * offset==0 时与 sectionPosition 恒等（旧口径）。 */
    if (!self || section >= self->m_count) return -1;
    position = XHeaderView_sectionPosition(self, section);
    if (position < 0) return position;   /* 隐藏段（Qt「was hidden」分支）。 */
    return position - self->m_offset;
}

/* ==================== stretchLastSection 引擎（K-73 2026-11-14） ==================== */

/* 对标 Qt QHeaderViewPrivate::resizeSections 的 stretchLastSection 分支
 * （qheaderview.cpp:3547-3562 池统计、:3619-3623 末段
 * qMax(stretchSectionLength, lastSectionSize)）。Qt 的重排经
 * doDelayedResizeSections 延迟到下一轮事件循环；本实现收敛为**同步**
 * 重排（与 setSectionResizeMode 全局重载的既有同步化先例同口径），
 * 触发点（resizeSection/hideSection/showSection/setCount/setter/视口长
 * 推送）都在动作尾部即时生效。本机 Qt 6.8.3 offscreen 探针（2026-11-14，
 * /tmp/qsprobe）：
 *  - Interactive 4 段 100/150/200 + stretchLast ⇒ 638 视口末段 188、
 *    798 视口末段 348、498 视口末段夹到快照 100（qMax(leftover, 快照)）；
 *  - global Stretch + stretchLast ⇒ 四段等分 160/160/159/159（末段不特判
 *    ——qheaderview.cpp:3555-3560 池分配把 globalMode 写进每段生效模式）；
 *  - 程序化 resizeSection(col3,222) ⇒ 快照抬到 222，改宽 col0 后末段
 *    回吃余量 238（快照作下限）；
 *  - hide col3 ⇒ col2 立即吃满余量 418。 */

/** @brief 末个可见段的视觉序号（对标 QHeaderViewPrivate::
 *         lastVisibleVisualIndex，qheaderview.cpp:3468-3477；无可见段 -1）。 */
static int xhv_lastVisibleVisualIndex(const XHeaderView* self)
{
    int i;
    if (!self || !self->m_sections) return -1;
    for (i = self->m_count - 1; i >= 0; --i)
        if (!XHeaderView_isSectionHidden(self, i)) return i;
    return -1;
}

/** @brief 把「末段身份」切到当前末段并立快照（对标 setNewLastSection，
 *         qheaderview.cpp:3490-3496）。 */
static void xhv_setNewLastSection(XHeaderView* self, int lastVisual)
{
    int logical;
    if (!self) return;
    self->m_lastSectionSize = -1;
    if (lastVisual < 0) {
        self->m_lastSectionLogical = -1;
        return;
    }
    logical = XHeaderView_logicalIndex(self, lastVisual);
    self->m_lastSectionLogical = logical;
    if (logical >= 0)
        self->m_lastSectionSize = XHeaderView_sectionSize(self, logical);
}

/** @brief 旧末段不再居末时按快照复原（对标 restoreSizeOnPrevLastSection，
 *         qheaderview.cpp:3480-3487）；快照清账防重入。 */
static void xhv_restorePrevLastSection(XHeaderView* self)
{
    int logical;
    int size;
    if (!self || self->m_lastSectionLogical < 0 || self->m_lastSectionSize < 0)
        return;
    logical = self->m_lastSectionLogical;
    size = self->m_lastSectionSize;
    self->m_lastSectionLogical = -1;
    self->m_lastSectionSize = -1;
    XHeaderView_resizeSection(self, logical, size);
}

/** @brief stretchLastSection 末段重排（引擎主体；可用长未推送时不跑）。
 *
 *  对标 Qt QHeaderViewPrivate::resizeSections 延迟重排路径（useGlobalMode=
 *  false，qheaderview.cpp:3553-3640）的**池**口径：
 *  - 末段**强制**视为 Stretch 进池（:3555-3560
 *    `resizeMode = (i == stretchSection ? Stretch : headerSectionResizeMode(i))`）；
 *    其它生效模式为 Stretch 的可见段同池（本机 6.8.3 探针 P1-2：中段
 *    per-section Stretch + Interactive 末段 + stretchLast ⇒ 池 {1,3} 均分
 *    各 194）。
 *  - 非池可见段按**现宽**从可用长中扣除（Qt 另按 [min,max] 钳制并把钳制值
 *    回写段宽；本库 0 宽段是 resizeSection(0,0) 的存储语义（gap 155），
 *    按现宽计入、不回写——登记为有意的存储口径差异）。
 *  - 可用长扣除后余量 <= 0 ⇒ 整体跳过、池成员一律保现宽（:3591
 *    `lengthToStretch > 0` 门；探针 P1-4b/c：视口 298 下末段保 448，
 *    setColumnWidth 迫使重排也不缩）。
 *  - 余量 > 0 ⇒ stretchLen = qMax(余量/池成员数, minimumSectionSize)，余数
 *    按视觉序逐池成员 +1（pixelReminder，:3624-3626）；末段再 qMax 末段
 *    快照宽（:3619-3623，快照经 resizeSection 的 K-73 钩维护）。
 *  全局 Stretch 模式（m_resizeMode==Stretch，与视图侧分发器
 *  xtw_stretchColumnsToViewport 的触发条件同一字段）的池分配在视图侧
 *  （Qt 池口径：余数按视觉序前置分摊、快照末段 qMax 下限——2026-11-14
 *  K-68 残留 c 对齐，探针 A），引擎不重复分配——复核意见 P1 的 per-section
 *  Stretch 末段场景即本函数的池路径（此前对「末段生效模式==Stretch」
 *  一律 return，全局非 Stretch 时末段两头都无人管，已按池口径承接）。 */
static void xhv_stretchApply(XHeaderView* self)
{
    int lastVisual;
    int lastLogical;
    int available;
    int others;
    int poolCount;
    int poolTotal;
    int leftover;
    int stretchLen;
    int reminder;
    int i;
    if (!self || !self->m_sections || !self->m_stretchLast ||
        self->m_stretchViewportLen < 0)
        return;
    available = self->m_stretchViewportLen;
    lastVisual = xhv_lastVisibleVisualIndex(self);
    if (lastVisual < 0) return;
    lastLogical = XHeaderView_logicalIndex(self, lastVisual);
    if (lastLogical < 0) return;
    /* 全局 Stretch：池分配在视图侧（同一 m_resizeMode 字段判定），跳过。 */
    if (self->m_resizeMode == (int)XHeaderViewResizeMode_Stretch) return;
    /* 末段身份变化 ⇒ 先按快照复原旧末段、再以新末段现宽立快照
     * （对标 maybeRestorePrevLastSectionAndStretchLast，qheaderview.cpp:
     * 3502-3516）。 */
    if (self->m_lastSectionLogical != lastLogical)
        xhv_restorePrevLastSection(self);
    if (self->m_lastSectionLogical != lastLogical)
        xhv_setNewLastSection(self, lastVisual);
    /* 非池可见段现宽合计 + 池成员计数（末段以外的池成员 = 生效模式为
     * Stretch 的可见段；隐藏段不参与）。 */
    others = 0;
    poolCount = 0;
    for (i = 0; i < self->m_count; ++i) {
        int logicalI;
        if (i == lastVisual) continue;
        logicalI = XHeaderView_logicalIndex(self, i);
        if (logicalI < 0 || XHeaderView_isSectionHidden(self, logicalI))
            continue;
        if (XHeaderView_sectionResizeModeAt(self, logicalI) ==
            (int)XHeaderViewResizeMode_Stretch)
            ++poolCount;
        else
            others += XHeaderView_sectionSize(self, logicalI);
    }
    poolTotal = poolCount + 1;              /* 末段强制进池 */
    leftover = available - others;
    if (leftover <= 0) return;              /* Qt :3591 门：无余量保现宽 */
    stretchLen = leftover / poolTotal;
    if (stretchLen < self->m_minimumSectionSize)
        stretchLen = self->m_minimumSectionSize;
    reminder = leftover % poolTotal;
    /* 按视觉序应用池成员；非池成员跳过（余数只落池成员，:3624-3626）。 */
    for (i = 0; i < self->m_count; ++i) {
        int logicalI;
        int target;
        if (i == lastVisual) {
            /* 末段：先 qMax 末段快照宽（:3619-3623），再吃余数。 */
            target = (stretchLen > self->m_lastSectionSize)
                         ? stretchLen : self->m_lastSectionSize;
        } else {
            logicalI = XHeaderView_logicalIndex(self, i);
            if (logicalI < 0 ||
                XHeaderView_isSectionHidden(self, logicalI) ||
                XHeaderView_sectionResizeModeAt(self, logicalI) !=
                    (int)XHeaderViewResizeMode_Stretch)
                continue;
            target = stretchLen;
        }
        if (reminder > 0) { ++target; --reminder; }
        if (target > self->m_maximumSectionSize)
            target = self->m_maximumSectionSize;
        if (i == lastVisual) {
            if (target != XHeaderView_sectionSize(self, lastLogical))
                XHeaderView_setSectionSize(self, lastLogical, target);
        } else if (target != XHeaderView_sectionSize(self, logicalI)) {
            /* 直走 setSectionSize：绕开 resizeSection 的末段快照维护（Qt
             * 内部池应用走 createSectionItems、不经 resizeSection ⇒ 快照
             * 不被拉伸值覆写，:3630-3640）；发射集与 resizeSection 相同
             * （sectionResized + geometriesChanged）。 */
            XHeaderView_setSectionSize(self, logicalI, target);
        }
    }
}

/** @brief stretchLastSection 触发闸（各动作尾部共用）。 */
static void xhv_stretchMaybe(XHeaderView* self)
{
    if (!self || !self->m_stretchLast || self->m_stretchViewportLen < 0)
        return;
    xhv_stretchApply(self);
}

void XHeaderView_setStretchViewportLength(XHeaderView* self, int length)
{
    if (!self) return;
    self->m_stretchViewportLen = length;
    /* [K-64 §B3 2026-11-15] 同一「视口内容区可用长」事实也供 offset
     * 分支（setOffsetToLastSection 的 length − 视口宽）消费：两个字段
     * 同源落地，避免宿主再推第二份（现宿主只经本入口/syncViewportLength
     * 推一次）。stretch 引擎只在 stretchLast 开启时消费 m_stretchViewportLen。 */
    self->m_offsetViewportLen = length;
    /* 对标 Qt：表头 Resize 事件 ⇒ resizeSections()（qheaderview.cpp:
     * 2817-2827），视口长落地即重排。 */
    xhv_stretchMaybe(self);
}

void XHeaderView_syncViewportLength(XHeaderView* self, int length)
{
    /* [K-73a 2026-11-14] 视口长同值闸：滚动条显隐等视口几何变化点的
     * 重推入口只在**长度真变**（含 <0 未推送的首推）时才走
     * setStretchViewportLength（落值 + 重排）——同值推送不触发引擎，
     * 宿主的冗余事件/重复推送在此收敛（XHeaderView_Protected.h 注）。 */
    if (!self || self->m_stretchViewportLen == length) return;
    XHeaderView_setStretchViewportLength(self, length);
}

/* [K-68c 2026-11-14] 全局 Stretch 池的视图侧分配器（XTableWidget）要按
 * Qt resizeSections 的视觉末段下限 qMax(stretchSectionLength, lastSectionSize)
 * 取同一事实源（qheaderview.cpp:3619-3623）；该快照只在 stretchLastSection
 * 开启时有效（qheaderview_p.h:70-71「Only trust when we stretch last
 * section」）。声明见 XHeaderView_Protected.h，引擎态字段由
 * setStretchLastSection→setNewLastSection 立账、resizeSection 的 K-73 钩维护。 */
bool XHeaderView_lastSectionSnapshot(const XHeaderView* self,
                                     int* outLogical, int* outSize)
{
    if (outLogical) *outLogical = -1;
    if (outSize) *outSize = -1;
    if (!self || !self->m_stretchLast) return false;
    if (outLogical) *outLogical = self->m_lastSectionLogical;
    if (outSize) *outSize = self->m_lastSectionSize;
    return self->m_lastSectionLogical >= 0;
}

void XHeaderView_setStretchLastSection(XHeaderView* self, bool stretch)
{
    if (!self || self->m_stretchLast == stretch) return;
    self->m_stretchLast = stretch;
    /* 对标 Qt setStretchLastSection（qheaderview.cpp:1459-1467）：开 ⇒
     * setNewLastSection(最后可见段) + resizeSections()；关 ⇒
     * restoreSizeOnPrevLastSection()（旧末段按快照复原，撤销拉伸）。 */
    if (stretch) {
        xhv_setNewLastSection(self, xhv_lastVisibleVisualIndex(self));
        xhv_stretchMaybe(self);
    } else {
        xhv_restorePrevLastSection(self);
    }
}

bool XHeaderView_stretchLastSection(const XHeaderView* self)
{
    /* 对标 Qt 属性读取器 stretchLastSection（2026-11-13 P2-40：is 前缀
     * 别名 isStretchLastSection 已删，Qt 名即唯一读取口）。 */
    return self ? self->m_stretchLast : false;
}

/* ==================== 视觉序与位置反查（对标 QHeaderView visualIndex 族） ==================== */

int XHeaderView_visualIndex(const XHeaderView* self, int logicalIndex)
{
    int n;
    int secCount;
    int i;
    if (!self || logicalIndex < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (logicalIndex >= n) return -1;
    /* 对标 QHeaderView::visualIndex（qheaderview.cpp:1024-1041）：
     * 映射表未建/未移动时恒等；建表后按 visual→logical 反查。 */
    secCount = n;
    if (!self->m_visualToLogical) return logicalIndex;
    n = xhv_mapCount(self);
    for (i = 0; i < n; ++i) {
        if (XVector_At_Base(self->m_visualToLogical, (int64_t)i, int) ==
            logicalIndex)
            return i;
    }
    return logicalIndex < secCount ? logicalIndex : -1;
}

int XHeaderView_visualIndexAt(const XHeaderView* self, int position)
{
    /* 对标 QHeaderView::visualIndexAt：位置反查视觉序，跳过隐藏段
     * （qheaderview.cpp:551-583 的 while (isVisualIndexHidden) ++visual）。
     * [K-64 2026-11-15] 对齐 Qt :563-571：入参先加 headerOffset 再反查
     * （vposition += d->headerOffset），并校验 vposition > length ⇒ -1。
     * offset 缺省 0 ⇒ 无推送视口的旧调用逐位不变。 */
    int n;
    int i;
    if (!self || position < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    position += self->m_offset;
    if (position > XHeaderView_length(self)) return -1;
    for (i = 0; i < n; ++i) {
        int logical = XHeaderView_logicalIndex(self, i);
        int size;
        if (logical < 0) return -1;
        if (XHeaderView_isSectionHidden(self, logical)) continue;
        size = XHeaderView_sectionSize(self, logical);
        if (position < size) return i;
        position -= size;
    }
    return -1;
}

int XHeaderView_logicalIndex(const XHeaderView* self, int visualIndex)
{
    int n;
    if (!self || visualIndex < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    if (visualIndex >= n) return -1;
    /* 对标 QHeaderView::logicalIndex：视觉序 → 逻辑段号（不受隐藏影响）。 */
    if (!self->m_visualToLogical ||
        visualIndex >= xhv_mapCount(self))
        return visualIndex;
    return XVector_At_Base(self->m_visualToLogical, (int64_t)visualIndex, int);
}

int XHeaderView_logicalIndexAt(const XHeaderView* self, int position)
{
    /* 对标 QHeaderView::logicalIndexAt = visualIndexAt + logicalIndex
     * （qheaderview.cpp:585-591）。 */
    int visual = XHeaderView_visualIndexAt(self, position);
    if (visual < 0) return -1;
    return XHeaderView_logicalIndex(self, visual);
}

int XHeaderView_resizeHandleSectionAt(const XHeaderView* self, int position)
{
    int n;
    int v;
    int pos;
    if (!self || position < 0) return -1;
    n = self->m_sections
            ? (int)XVector_size_base((const XContainer*)self->m_sections)
            : 0;
    /* 对标 QHeaderViewPrivate::sectionHandleAt（qheaderview.cpp:3307）：
     * 命中线为「段 i 尾分隔线」（本库绘制口径 x+w-1，与段矩形同一
     * 坐标系）；隐藏段不作为手柄目标且**不占位**。段按**视觉序**遍历，
     * 返回值是逻辑段号（与 Qt sectionHandleAt 返回 logical 一致）。
     * [K-64 2026-11-15] Qt 的 sectionHandleAt 经 visualIndexAt
     * （加 headerOffset）与 sectionViewportPosition（减 headerOffset）在
     * 视口坐标系比较；本实现的累加器 pos 是表头坐标系 ⇒ 入参先补
     * offset 才与累加值同域（offset 缺省 0 ⇒ 旧口径逐位不变）。 */
    position += self->m_offset;
    pos = 0;
    for (v = 0; v < n; ++v) {
        int logical = XHeaderView_logicalIndex(self, v);
        int size;
        int sep;
        if (logical < 0 || XHeaderView_isSectionHidden(self, logical))
            continue;
        size = XHeaderView_sectionSize(self, logical);
        pos += size;
        sep = pos - 1;
        if (position >= sep - XHEADERVIEW_HANDLE_HIT_MARGIN &&
            position <= sep + XHEADERVIEW_HANDLE_HIT_MARGIN)
            return logical;
    }
    return -1;
}

int XHeaderView_sectionSizeHint(const XHeaderView* self, int logicalIndex)
{
    XSize size;
    int hint;
    /* 对标 Qt 6.8 sectionSizeHint（qheaderview.cpp:527-540）：隐藏段 0、
     * 越界 -1；其余按内容测算（本库模型不提供 SizeHintRole ⇒ 直接走
     * sectionSizeFromContents）再钳制到 [min,max]。无模型时回落缺省
     * 段尺寸，保持纯几何表头的可用值。 */
    if (!self) return 0;
    if (logicalIndex < 0 || logicalIndex >= self->m_count) return -1;
    if (XHeaderView_isSectionHidden(self, logicalIndex)) return 0;
    if (!self->m_model) return self->m_defaultSize;
    size = XHeaderView_sectionSizeFromContents_base(
        (XHeaderView*)self, logicalIndex);
    hint = (self->m_orientation == 0) ? size.width : size.height;
    if (hint < self->m_minimumSectionSize) hint = self->m_minimumSectionSize;
    if (hint > self->m_maximumSectionSize) hint = self->m_maximumSectionSize;
    return hint;
}

int XHeaderView_length(const XHeaderView* self)
{
    int n;
    int i;
    int total = 0;
    if (!self || !self->m_sections) return 0;
    n = (int)XVector_size_base((const XContainer*)self->m_sections);
    for (i = 0; i < n; ++i) {
        if (self->m_hidden &&
            XVector_At_Base(self->m_hidden, (int64_t)i, bool))
            continue;
        total += XVector_At_Base(self->m_sections, (int64_t)i, int);
    }
    return total;
}

void XHeaderView_setOffset(XHeaderView* self, int offset)
{
    /* [K-64 2026-11-15] 对标 Qt setOffset（qheaderview.cpp:401-421）：
     * 同值早退（Qt 首行 `if (d->headerOffset == newOffset) return`），
     * 真变时落值并调度整控件重绘。Qt 还驱动视口 scroll——本表头为隐藏
     * 子件、表头带由表格自绘，视口平移由宿主视图承担；此处只承载几何量
     * 并经 sectionViewportPosition/visualIndexAt 参与位置计算。 */
    if (!self || self->m_offset == offset) return;
    self->m_offset = offset;
    XWidget_update((XWidget*)self);
}

int XHeaderView_offset(const XHeaderView* self)
{
    return self ? self->m_offset : 0;
}

void XHeaderView_setOffsetToLastSection(XHeaderView* self)
{
    int off;
    if (!self) return;
    /* [K-64 2026-11-15] 对齐 Qt（qheaderview.cpp:446-452）：
     * offset = length − 视口宽，可为负（不再钳制 >=0）。视口宽与
     * stretch 引擎共用宿主经 setStretchViewportLength/syncViewportLength
     * 推送的内容区可用长（m_offsetViewportLen）；未推送（<0）按 0 参与
     * ⇒ 退化为 length（旧口径保持可用）。 */
    off = XHeaderView_length(self);
    if (self->m_offsetViewportLen > 0)
        off -= self->m_offsetViewportLen;
    XHeaderView_setOffset(self, off);
}

void XHeaderView_setOffsetToSectionPosition(XHeaderView* self, int index)
{
    int pos;
    if (!self) return;
    pos = XHeaderView_sectionPosition(self, index);
    /* 越界（sectionPosition 返回 -1）忽略，同 Qt 先校验区间号有效。 */
    if (pos < 0) return;
    XHeaderView_setOffset(self, pos);
}

void XHeaderView_setModel(XHeaderView* self, void* model)
{
    /* 对标 Qt QHeaderView::setModel 的信号接线：模型 headerDataChanged
     * 信号 → 表头 headerDataChanged 槽（驱动段文本变化后的重绘）。模型
     * 仍为借用指针（表头不拥有）；model 须为 XAbstractItemModel 或其
     * 派生（供信号接线），NULL 解除关联。信号标识经 XSignal 取令牌
     * （NULL 发送方不触发发射）。 */
    if (!self) return;
    if (self->m_model && self->m_model != model) {
        XObject_disconnect(
            (XObject*)self->m_model,
            XSignal(XAbstractItemModel_headerDataChanged_signal),
            (XObject*)self, xhv_modelHeaderDataChangedSlot);
    }
    self->m_model = model;
    if (model) {
        /* 发送方信号槽管理器由 connect 内部懒建（XObject_connect_1），
         * 不以其是否已存在为门（新建模型尚未连接任何信号时同样要接）。 */
        XObject_connect(
            (XObject*)model,
            XSignal(XAbstractItemModel_headerDataChanged_signal),
            (XObject*)self, xhv_modelHeaderDataChangedSlot,
            XConnectionType_Direct);
    }
}

XWidget* XHeaderView_viewport(XHeaderView* self)
{
    /* 表头自绘无独立视口（Qt 视口由 QAbstractScrollArea 基类持有），
     * 收敛为返回自身，等效"整个表头即视口"。 */
    return (XWidget*)self;
}

void* XHeaderView_sectionClicked_signal(XHeaderView* self, int section)
{
    /* 真实发射点：VXHeaderView_mouseReleaseEvent（clickable 且释放于
     * 按压段，对标 Qt mouseReleaseEvent 尾段；连接方亦可调用本句柄
     * 手动发射）。 */
    xhv_emitInt(self, (size_t)XHeaderView_sectionClicked_signal, section);
    return (void*)(size_t)XHeaderView_sectionClicked_signal;
}

void* XHeaderView_sectionPressed_signal(XHeaderView* self, int section)
{
    /* 真实发射点：VXHeaderView_mousePressEvent（clickable 且按压于
     * 段上，对标 Qt mousePressEvent 的 sectionPressed 分支）。 */
    xhv_emitInt(self, (size_t)XHeaderView_sectionPressed_signal, section);
    return (void*)(size_t)XHeaderView_sectionPressed_signal;
}

void* XHeaderView_sortIndicatorChanged_signal(XHeaderView* self,
                                              int logicalIndex, int order)
{
    (void)logicalIndex; (void)order;
    return (void*)(size_t)XHeaderView_sortIndicatorChanged_signal;
}

void* XHeaderView_sortIndicatorClearableChanged_signal(XHeaderView* self,
                                                      bool clearable)
{
    (void)clearable;
    return (void*)(size_t)XHeaderView_sortIndicatorClearableChanged_signal;
}

void* XHeaderView_sectionDoubleClicked_signal(XHeaderView* self, int section)
{
    /* 真实发射点（双通道）：① 主通道 VXHeaderView_mouseDoubleClickEvent
     * （DBL 事件无条件发射，对齐 qheaderview.cpp:2759）；② 兼容通道
     * VXHeaderView_mousePressEvent（同一段 400ms 内的第二次按压，兼容
     * 不产 DBL 事件类型的输入后端）。两条通道与头文件 @note、
     * README.md「双通道发射」同口径。2026-11-09 接入：此前本句柄只有
     * 声明没有发射点 ⇒ 页面侧「双击列标题排序」接了也收不到。 */
    xhv_emitInt(self, (size_t)XHeaderView_sectionDoubleClicked_signal, section);
    return (void*)(size_t)XHeaderView_sectionDoubleClicked_signal;
}

void* XHeaderView_sectionEntered_signal(XHeaderView* self, int section)
{
    /* 真实发射点：VXHeaderView_mouseMoveEvent（悬停进入新段）。 */
    xhv_emitInt(self, (size_t)XHeaderView_sectionEntered_signal, section);
    return (void*)(size_t)XHeaderView_sectionEntered_signal;
}


void* XHeaderView_sectionHandleDoubleClicked_signal(XHeaderView* self,
                                                    int section)
{
    /* [发射点 2026-11-13 补齐] 对标 Qt 6.8 mouseDoubleClickEvent
     * （qheaderview.cpp:2741-2760）：双击落在 Interactive 模式的分隔线
     * 手柄 ⇒ `emit sectionHandleDoubleClicked(handle)`（:2747）；否则走
     * sectionDoubleClicked。本库的该条件判定已在
     * VXHeaderView_mouseDoubleClickEvent 的手柄分支内，此处
     * 补上真实发射（与其他 int 段信号同款 xhv_emitInt），此前本句柄只
     * (void)section 空转 ⇒ 条件命中也收不到信号。连接方亦可调用本句柄
     * 手动发射；self=NULL 时短路，仅作为 signalIdOf 取址用。 */
    xhv_emitInt(self, (size_t)XHeaderView_sectionHandleDoubleClicked_signal,
                section);
    return (void*)(size_t)XHeaderView_sectionHandleDoubleClicked_signal;
}

void* XHeaderView_sectionMoved_signal(XHeaderView* self, int logicalIndex,
                                      int oldVisualIndex, int newVisualIndex)
{
    (void)logicalIndex; (void)oldVisualIndex; (void)newVisualIndex;
    return (void*)(size_t)XHeaderView_sectionMoved_signal;
}

void* XHeaderView_sectionResized_signal(XHeaderView* self, int logicalIndex,
                                        int oldSize, int newSize)
{
    (void)logicalIndex; (void)oldSize; (void)newSize;
    return (void*)(size_t)XHeaderView_sectionResized_signal;
}

void* XHeaderView_sectionCountChanged_signal(XHeaderView* self, int oldCount,
                                             int newCount)
{
    (void)oldCount; (void)newCount;
    return (void*)(size_t)XHeaderView_sectionCountChanged_signal;
}

void* XHeaderView_geometriesChanged_signal(XHeaderView* self)
{
    return (void*)(size_t)XHeaderView_geometriesChanged_signal;
}

/** @brief 模型 headerDataChanged(orientation,first,last) 信号槽：转发
 *         表头同名句柄（载荷收敛为单段号）并请求重绘（对标 Qt
 *         QHeaderView::headerDataChanged 槽的段重绘效果）。 */
static void xhv_modelHeaderDataChangedSlot(XObject* receiver, XVarList* args)
{
    XHeaderView* header = (XHeaderView*)receiver;
    int orientation;
    int first;
    int last;
    if (!header || !args) return;
    XVarList_start(args);
    orientation = XVarList_arg(args, int);
    first = XVarList_arg(args, int);
    last = XVarList_arg(args, int);
    /* 对标 Qt 槽体：段区间失效重绘（本库无文本承载，收敛为区间一次
     * 整段重绘请求）；载荷经既有单 int 句柄转发（单段口径取 first）。 */
    (void)orientation;
    (void)last;
    XHeaderView_headerDataChanged_signal(header, first);
}

void* XHeaderView_headerDataChanged_signal(XHeaderView* self, int section)
{
    /* 真实发射点：XHeaderView_setModel 接线的模型 headerDataChanged
     * 转发槽（连接方亦可调用本句柄手动发射；对标 Qt 同名公开槽，本
     * 项目载荷收敛为单 int）。NULL 发送方不发射（XSignal 令牌取值
     * 以 NULL 调用本句柄，须短路）。 */
    if (self) {
        xhv_emitInt(self, (size_t)XHeaderView_headerDataChanged_signal,
                    section);
        XWidget_update((XWidget*)self);
    }
    return (void*)(size_t)XHeaderView_headerDataChanged_signal;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON */

#endif /* XGUI_ON */
