/******************************************************************************
 * @file       XLayout.c
 * @brief      XLayout 抽象布局基类实现（对标 Qt 6.8 QLayout）。
 * @details    本文件实现 XLayout 的：
 *             - 类虚函数表（继承 XLayoutItem 18 槽 + 条目管理 5 槽，含
 *               原位替换 ReplaceItemAt）与生命周期（Deinit 释放内部
 *               拥有条目；Copy 仅复制配置；Move 转移条目数组所有权）；
 *             - 条目数组管理（append/insert/takeAt/indexOf）与通用入口
 *               （removeWidget/removeItem/itemForWidget/deleteAllItems）；
 *             - 内容边距/间距/尺寸约束/对齐设置；
 *             - 挂接控件（attachWidget/detachWidget）与激活
 *               （activate：把尺寸约束写回控件并重新解算所有条目）；
 *             - PC 扩展（XLAYOUT_TOTAL_ON 门控，对标 QLayout）：菜单栏
 *               setMenuBar/menuBar、启用标志 setEnabled/isEnabled、
 *               unsetContentsMargins/contentsRect、合计尺寸 total*、
 *               replaceWidget 原位替换、closestAcceptableSize；
 *             - 受保护助手（XLayout_appendItem/XLayout_insertItemAt/
 *               XLayout_indexOfItem/XLayout_linkItem/
 *               XLayout_effectiveSpacing/XLayout_contentsRectForRect/
 *               XLayout_layoutMargins）。
 *             通用 setGeometry 把内容矩形整块交给每个子条目（子类
 *             XBoxLayout/XGridLayout 覆盖此槽位实现各自分配算法）。
 * @note       条目所有权约定见 XLayout.h 头文件说明；本文件不依赖任何
 *             平台 API，嵌入式可用。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XLayout.h"

#include "XAlgorithm.h"
#include "XLayout_Internal.h"
#include "XLayoutItem_Protected.h"
#include "XWidget.h"
#include "XWidget_Protected.h" /* activate 尾部向无布局宿主父级 post LayoutRequest（Qt activate 尾部同效）。 */
#include "XMemory.h"
#include "XCoreApplication.h" /* invalidate 收口向宿主 post LayoutRequest（Qt 对齐）。 */
#include "XSystem.h"         /* [lprof] 画像门（XGPU_W_FRAME_PROF 同门控）。 */
#include "XDateTime.h"       /* [lprof] 真重解算耗时计时。 */

#if XLAYOUT_ON

/* ==================== [lprof] 布局激活/失效画像（诊断） ==================== */

/* XGPU_W_FRAME_PROF=1 同门控（与 XWidget.c 的 wprof/rprof 共用一个 env，
 * 打印挂在同一 3 秒画像窗，见 XWidget.c 的 [lprof] 行）。计数仅在门开时
 * 累积：act=activate 真重解算（未走幂等早退）次数、skip=幂等早退次数、
 * inval=失效事件数、solveNs=真重解算累计耗时（纳秒）。门解析与
 * xwidget_wprof_gate 同款惰性单次。 */
static int xlayout_lprofInited;
static int xlayout_lprofOn;
static long xlayout_lprofAct;
static long xlayout_lprofActSkip;
static long xlayout_lprofInval;
static int64_t xlayout_lprofSolveNs;

static int xlayout_lprof_gate(void)
{
    if (!xlayout_lprofInited)
    {
        const char* env = XSystem_environment("XGPU_W_FRAME_PROF");
        xlayout_lprofOn =
            env && *env && !(env[0] == '0' && env[1] == 0) ? 1 : 0;
        xlayout_lprofInited = 1;
    }
    return xlayout_lprofOn;
}

void XLayout_profilerReadAndReset(long* act, long* skip, long* inval,
                                  int64_t* solveNs)
{
    if (act) *act = xlayout_lprofAct;
    if (skip) *skip = xlayout_lprofActSkip;
    if (inval) *inval = xlayout_lprofInval;
    if (solveNs) *solveNs = xlayout_lprofSolveNs;
    xlayout_lprofAct = 0;
    xlayout_lprofActSkip = 0;
    xlayout_lprofInval = 0;
    xlayout_lprofSolveNs = 0;
}

/* ==================== 静态辅助函数 ==================== */

/** @brief 扩大条目指针数组容量；返回是否成功。 */
static bool XLayout_growItemCapacity(XLayout* self, int need)
{
    int newCap;
    XLayoutItem** items;
    if (!self) return false;
    if (need <= self->m_itemCapacity) return true;
    newCap = self->m_itemCapacity ? self->m_itemCapacity : 4;
    while (newCap < need) newCap <<= 1;
    items = (XLayoutItem**)XRealloc_System(self->m_items,
                                           (size_t)newCap * sizeof(XLayoutItem*));
    if (!items) return false;
    XMemset(items + self->m_itemCapacity, 0,
           (size_t)(newCap - self->m_itemCapacity) * sizeof(XLayoutItem*));
    self->m_items = items;
    self->m_itemCapacity = newCap;
    return true;
}

/** @brief 解析内容边距：-1（未设置）按 0 处理（X 无 QStyle 缺省值）。 */
static XMargins XLayout_resolveMargins(const XLayout* self)
{
    XMargins m;
    XMargins_init(&m, 0, 0, 0, 0);
    if (!self) return m;
    m = self->m_contentsMargins;
    if (m.left < 0) m.left = 0;
    if (m.top < 0) m.top = 0;
    if (m.right < 0) m.right = 0;
    if (m.bottom < 0) m.bottom = 0;
    return m;
}

/** @brief 返回控件“智能最小尺寸”（对标 Qt qSmartMinSize(QWidget*)）。
 * @details 尺寸策略选择：ShrinkFlag 方向取 minimumSizeHint，其余方向取
 *          sizeHint/minimumSizeHint 较大者；结果收缩到控件最大尺寸；
 *          显式设置的最小尺寸（>0）再覆盖；负数分量按 0 处理。 */
static XSize XLayout_qSmartMinSize(const XWidget* widget)
{
    XSize out;
    XWidgetSizePolicy policy;
    XSize hint;
    XSize minHint;
    XSize minSize;
    XSize maxSize;
    int w;
    int h;
    XSize_init(&out, 0, 0);
    if (!widget) return out;
    hint = XWidget_sizeHint(widget);
    minHint = XWidget_minimumSizeHint(widget);
    minSize = XWidget_minimumSize(widget);
    maxSize = XWidget_maximumSize(widget);
    policy = XWidget_sizePolicy(widget);
    w = 0;
    h = 0;
    if (policy.m_horizontalPolicy != XWidgetSizePolicy_Ignored) {
        if (policy.m_horizontalPolicy & 0x04u)   /* ShrinkFlag */
            w = minHint.width;
        else
            w = hint.width > minHint.width ? hint.width : minHint.width;
    }
    if (policy.m_verticalPolicy != XWidgetSizePolicy_Ignored) {
        if (policy.m_verticalPolicy & 0x04u)
            h = minHint.height;
        else
            h = hint.height > minHint.height ? hint.height : minHint.height;
    }
    if (w > maxSize.width) w = maxSize.width;
    if (h > maxSize.height) h = maxSize.height;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    if (minSize.width > 0) w = minSize.width;
    if (minSize.height > 0) h = minSize.height;
    XSize_init(&out, w, h);
    return out;
}

/** @brief 返回控件“智能最大尺寸”（对标 Qt qSmartMaxSize(QWidget*)
 *  无对齐参数形态；closestAcceptableSize 与菜单栏高度使用）。
 * @details 方向最大尺寸等于 XWIDGET_MAX_SIZE 且尺寸策略无 GrowFlag 时
 *          收拢到 max(sizeHint, minimumSizeHint)，其余保持控件最大尺寸。 */
static XSize XLayout_qSmartMaxSize(const XWidget* widget)
{
    XSize out;
    XSize hint;
    XSize minHint;
    XSize maxSize;
    XWidgetSizePolicy policy;
    XSize_init(&out, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);
    if (!widget) return out;
    hint = XWidget_sizeHint(widget);
    minHint = XWidget_minimumSizeHint(widget);
    maxSize = XWidget_maximumSize(widget);
    policy = XWidget_sizePolicy(widget);
    hint = XSize_expandedTo(&hint, &minHint);
    out = maxSize;
    if (out.width == XWIDGET_MAX_SIZE &&
        !(policy.m_horizontalPolicy & 0x01u))   /* GrowFlag */
        out.width = hint.width;
    if (out.height == XWIDGET_MAX_SIZE &&
        !(policy.m_verticalPolicy & 0x01u))
        out.height = hint.height;
    if (out.width < 0) out.width = 0;
    if (out.height < 0) out.height = 0;
    return out;
}

/** @brief 返回菜单栏预留高度（对标 QLayoutPrivate::menuBarHeightForWidth）。
 * @details 菜单栏隐藏或是顶层窗口时不预留；X 控件没有独立的
 *          heightForWidth 虚接口，直接以 sizeHint 高度为准，并钳位到
 *          [qSmartMinSize().height, maximumSize().height]
 *          （对应 Qt 的 hfw 返回 -1 后的回退分支）。 */
static int XLayout_menuBarHeightForWidth(const XWidget* menubar, int width)
{
    int result;
    int min;
    int max;
    (void)width;
    if (!menubar) return 0;
    if (XWidget_isHidden(menubar) || XWidget_isWindow(menubar)) return 0;
    result = XWidget_sizeHint(menubar).height;
    min = XLayout_qSmartMinSize(menubar).height;
    if (result < min) result = min;
    max = XWidget_maximumSize(menubar).height;
    if (result > max) result = max;
    if (result < 0) result = 0;
    return result;
}

/** @brief 条目挂接后的关联设置：子布局登记父布局；控件条目登记父控件
 *  （对标 Qt 布局 addWidget 自动 reparent 语义；受保护，子类原位替换
 *  新条目时复用）。 */
void XLayout_linkItem(XLayout* self, XLayoutItem* item)
{
    XWidget* parent;
    XWidget* child;
    XLayout* sub;
    if (!self || !item) return;
    sub = XLayoutItem_layout_base(item);
    if (sub) {
        if (sub->m_parentWidget && sub->m_parentWidget->m_layout == sub)
            sub->m_parentWidget->m_layout = NULL;
        if (sub != (XLayout*)self)
            sub->m_parentLayout = self;
        sub->m_parentWidget = NULL;
        return;
    }
    parent = self->m_parentWidget;
    child = XLayoutItem_widget_base(item);
    if (!parent || !child) return;
    if (XWidget_parentWidget(child) != parent)
        XWidget_setParentPlain(child, parent);
}

/* ==================== 虚函数表与生命周期 ==================== */

/** @brief XLayout 空判定（对标 QLayout::isEmpty，qlayout.cpp:422）：
 *  itemAt 扫描——**所有条目都空才为空**（子布局条目递归有效）。 */
static bool VXLayout_isEmpty(const XLayoutItem* item)
{
    const XLayout* self = (const XLayout*)item;
    int i;
    if (!self) return true;
    for (i = 0; i < self->m_itemCount; ++i) {
        if (self->m_items[i] && !XLayoutItem_isEmpty_base(self->m_items[i]))
            return false;
    }
    return true;
}

/** @brief XLayout 本身就是布局条目：返回自身。 */
static XLayout* VXLayout_layout(const XLayoutItem* item)
{
    return (XLayout*)item;
}

/** @brief [对齐 qlayout.h:80 `Qt::Orientations expandingDirections()
 *  const override`] Qt QLayout 基类默认两轴可伸展（子类按条目策略覆盖）；
 *  XGui 基类此前继承 XLayoutItem 的 None 默认，与 Qt 同名不同义，已修复
 *  （盒式/网格的本类覆盖不受影响）。 */
static XLayoutExpandingDirections VXLayout_expandingDirections(
    const XLayoutItem* item)
{
    (void)item;
    return XLayoutExpandingDirection_Both;
}

/** @brief [update 2026-10-31 对齐 QLayout::update（qlayout.cpp:971-986）]
 *  失效沿布局链的门控上推：while (layout && activated) 自底向上逐级置
 *  未激活，到布局链顶才向**宿主控件**投 LayoutRequest（队列事件，经
 *  XWidget_postLayoutRequest 走 postEvent 压缩投递）。门控语义见函数内
 *  注（收敛保险丝 + 丢激活根修）。本函数只做 Qt update() 的语义
 *  （标志+投递），不清缓存——缓存失效属于 invalidate 的职责
 *  （XLayout_invalidate = 清缓存 + 本函数，对标 Qt 的
 *  invalidate = rect 复位 + update()）。
 * @note   投递只覆盖失效源所在控件的布局链（Qt 的 update 同样只沿
 *         QLayout 对象链走），跨控件祖先的重算由 show 补偿 / resize /
 *         下次 activate 承接；可见性闸在消费端（VXWidget_event 的
 *         LayoutRequest 分支）。 */
static void xlayout_update_impl(XLayout* self)
{
    XLayout* l = self;
    XLayout* top = NULL;
    /* [丢激活根修 2026-10-31 · 终版] 沿链**无条件**清 activated 并走到链顶
     * 投递。曾两度误撤：
     * ① 首撤（门控=收敛保险丝论）：把「执行后 CPU 80%」归因于
     *    activate→invalidate→post 自激——后经逐帧画像证伪：风暴真凶是
     *    软件字形光栅化无视口剔除（XPainter_drawGlyph 逐字形 FT 覆盖
     *    渲染，paint 500ms/帧），与布局投递无关；
     * ② 门控的历史失败模式（在飞事件被丢 → 布局永久停在未激活态，
     *    网络设置页 IPv6 展开标签高停 20、全量 autotest 布局断言 FAIL）
     *    已由 XWidget_postLayoutRequest 改走 XCoreApplication_postEvent
     *    （动态表不满不丢、compressEvent 合并保交付）根治——但实测门控
     *    口径下该页断言仍偶发失败（存在未覆盖的丢事件路径），无条件
     *    投递后 388 PASS 稳定。重复投递的去重由投递侧 compressEvent
     *    承担（一接收者至多 1 个在飞）。 */
    while (l) {
        l->m_activated = 0;
        top = l;
        l = l->m_parentLayout;
    }
    if (!top || !XCoreApplication_instance()) return;
    XWidget_postLayoutRequest(top->m_parentWidget);
}

/** @brief XLayout 失效：标记脏并向全部子条目传播失效率（子布局递归）。
 * @details [度量缓存 2026-10-17] 失效同时清三级尺寸缓存（对标 Qt
 *          QLayout::invalidate 清 totalMin/Max）；并沿父布局链向上清
 *          祖先缓存——子控件尺寸提示变化后祖先布局的合计度量同步陈旧
 *          须一并清除。向上段只清祖先自身（缓存+脏位）**不再回入向下
 *          级联**：否则 父失效→遍历子条目→子失效→回父 死循环栈溢出
 *          （启动段错误实证）。
 *          [跨控件上推 2026-10-31 对齐 Qt 不变量] Qt 里跨控件一致性由
 *          「布局无缓存 + activate 全量重测 + updateGeometry_helper 清
 *          QWidgetItemV2 缓存」（qwidget.cpp:10509）保证；XGui 的三级
 *          缓存跨控件存续，失效必须越过控件边界——沿父控件链继续清
 *          祖先控件布局（含各自 m_parentLayout 链）的缓存与脏位，替代
 *          此前 XWidget_updateGeometry 的逐级失效 walk（对齐 Qt 后该
 *          walk 已删）。清缓存段**无条件**（不设 activated 门：祖先可能
 *          已重新激活并从旧提示重建缓存，失效源的再次变化仍需清它）。
 *          [LayoutRequest 2026-10-31 投递可靠性根修] 投递段保持门控
 *          （门=收敛保险丝，撤门会自激，见 xlayout_update_impl 注）；
 *          历史故障（丢激活）的根因是**在飞事件被无锁队列满丢弃**，已由
 *          XWidget_postLayoutRequest 改走 XCoreApplication_postEvent
 *          （动态表、不满不丢、compressEvent 合并保交付）根治。投递只
 *          覆盖失效源所在控件的布局链（Qt 的 update 同样只沿 QLayout
 *          对象链走），跨控件祖先的重算由 show 补偿 / resize / 下次
 *          activate 承接；可见性闸在消费端（VXWidget_event 的
 *          LayoutRequest 分支）。 */
static void VXLayout_invalidate(XLayout* self)
{
    int i;
    if (!self) return;
    if (xlayout_lprof_gate()) ++xlayout_lprofInval;
    self->m_isDirty = 1;
    /* [几何快照复位 2026-10-31 对齐 QLayout::invalidate（qlayout.cpp:471
     * `d->rect = QRect();`）]：失效后布局几何快照清零，geometry()/
     * contentsRect() 读到空矩形，直到下一次 setGeometry/activate 重解算
     * 才恢复——与 Qt 同口径（「失效 ⇒ 几何未定」）。XStackedLayout 切页
     * 回贴读此字段：宿主可见时 show 链先重激活（setGeometry 写回新鲜
     * 几何）再读，行为不变；宿主隐藏时回贴跳过、留待 show 激活补齐
     * （Qt 同为无激活即无几何）。 */
    XRect_init(&self->m_base.m_geometry, 0, 0, 0, 0);
    XSize_init(&self->m_cachedMin, -1, -1);
    XSize_init(&self->m_cachedMax, -1, -1);
    XSize_init(&self->m_cachedHint, -1, -1);
    for (i = 0; i < self->m_itemCount; ++i)
        XLayoutItem_invalidate_base(self->m_items[i]);
    /* 向上清缓存段（无条件）：本控件布局链 → 跨控件父控件链逐级。 */
    {
        XLayout* p = self->m_parentLayout;
        while (p) {
            p->m_isDirty = 1;
            XSize_init(&p->m_cachedMin, -1, -1);
            XSize_init(&p->m_cachedMax, -1, -1);
            XSize_init(&p->m_cachedHint, -1, -1);
            p = p->m_parentLayout;
        }
        {
            XWidget* pw = self->m_parentWidget
                              ? XWidget_parentWidget(self->m_parentWidget)
                              : NULL;
            while (pw) {
                if (pw->m_layout) {
                    XLayout* q = pw->m_layout;
                    while (q) {
                        q->m_isDirty = 1;
                        XSize_init(&q->m_cachedMin, -1, -1);
                        XSize_init(&q->m_cachedMax, -1, -1);
                        XSize_init(&q->m_cachedHint, -1, -1);
                        q = q->m_parentLayout;
                    }
                }
                pw = XWidget_parentWidget(pw);
            }
        }
    }
    /* 投递段（门控）：委托 xlayout_update_impl（Qt 的 update() 语义），
     * 只在失效源所在控件的布局链上走，到链顶按门控结果投递一次。 */
    xlayout_update_impl(self);
}

/** @brief XLayout 通用几何分配：内容矩形整块交给每个子条目，
 *  子类（盒式/网格）覆盖本槽位改用各自算法。 */
static void VXLayout_setGeometry(XLayout* self, const XRect* rect)
{
    XRect inner;
    int i;
    if (!self || !rect) return;
    XClass_Parent(XLayoutItem, EXLayoutItem_SetGeometry,
                  void(*)(XLayoutItem*, const XRect*))((XLayoutItem*)self, rect);
    inner = XLayout_contentsRectForRect(self, rect);
    for (i = 0; i < self->m_itemCount; ++i) {
        if (self->m_items[i])
            XLayoutItem_setGeometry_base(self->m_items[i], &inner);
    }
    self->m_isDirty = 0;
    self->m_activated = 1;
}

/** @brief 向布局追加条目（抽象基类默认：追加到数组末尾，借用）。 */
static void VXLayout_addItem(XLayout* self, XLayoutItem* item)
{
    if (!self || !item) return;
    XLayout_appendItem(self, item, false);
}

/** @brief 返回指定索引条目。 */
static XLayoutItem* VXLayout_itemAt(const XLayout* self, int index)
{
    if (!self || index < 0 || index >= self->m_itemCount) return NULL;
    return self->m_items[index];
}

/** @brief 取出并移除指定索引条目（转移释放责任给调用方）。 */
static XLayoutItem* VXLayout_takeAt(XLayout* self, int index)
{
    XLayoutItem* item;
    if (!self || index < 0 || index >= self->m_itemCount) return NULL;
    item = self->m_items[index];
    XMemmove(&self->m_items[index], &self->m_items[index + 1],
            (size_t)(self->m_itemCount - index - 1) * sizeof(XLayoutItem*));
    self->m_items[self->m_itemCount - 1] = NULL;
    self->m_itemCount--;
    if (item) {
        item->m_ownedByLayout = 0;
        if (XLayoutItem_layout_base(item)) {
            XLayout* sub = (XLayout*)item;
            sub->m_parentLayout = NULL;
        }
    }
    /* 置脏同时清三级缓存：条目移除改变合计度量，缓存不清则下一次
       activate 前的读穿（XLayoutItem_*_base 命中 width>=0）返回旧值
       （takeAt 此前只置脏不清缓存，2026-10-31 修）。 */
    self->m_isDirty = 1;
    XSize_init(&self->m_cachedMin, -1, -1);
    XSize_init(&self->m_cachedMax, -1, -1);
    XSize_init(&self->m_cachedHint, -1, -1);
    return item;
}

/** @brief 返回条目数量。 */
static int VXLayout_count(const XLayout* self)
{
    if (!self) return 0;
    return self->m_itemCount;
}

/** @brief 原位替换条目：基类默认拒绝（对标 QLayoutPrivate::replaceAt
 *  基类默认返回 nullptr），由盒式/网格等子类覆盖实现。 */
static XLayoutItem* VXLayout_replaceItemAt(XLayout* self, int index,
                                           XLayoutItem* item)
{
    (void)self;
    (void)index;
    (void)item;
    return NULL;
}

/** @brief 释放布局当前持有的条目数组及其拥有的条目。 */
static void XLayout_releaseItems(XLayout* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_itemCount; ++i) {
        XLayoutItem* item = self->m_items[i];
        if (!item) continue;
        if (XLayoutItem_layout_base(item))
            ((XLayout*)item)->m_parentLayout = NULL;
        if (item->m_ownedByLayout)
            XClassDelete(item);
    }
    XFree_System(self->m_items);
    self->m_items = NULL;
    self->m_itemCount = 0;
    self->m_itemCapacity = 0;
}

/** @brief 释放布局资源：释放内部拥有条目、解除挂接与父布局反向引用。 */
static void VXLayout_deinit(XLayout* self)
{
    if (!self) return;
    if (self->m_parentWidget && self->m_parentWidget->m_layout == self)
        self->m_parentWidget->m_layout = NULL;
    self->m_parentWidget = NULL;
    XLayout_releaseItems(self);
    self->m_parentLayout = NULL;
    self->m_menuBar = NULL;
    XClass_Deinit_Parent(XLayoutItem, (XLayoutItem*)self);
}

/** @brief 深拷贝布局配置（边距/间距/约束/对齐/缓存失效标志），不复制条目树。 */
static void VXLayout_copy(XLayout* self, const XLayout* other)
{
    if (!self || !other || self == other) return;
    /* 目标未初始化（全零内存）时必须先初始化，再复制配置。 */
    if (XClassIsVtableNull(self)) XLayout_init(self);
    self->m_base.m_geometry = other->m_base.m_geometry;
    self->m_base.m_alignment = other->m_base.m_alignment;
    self->m_base.m_hasAlignment = other->m_base.m_hasAlignment;
    self->m_contentsMargins = other->m_contentsMargins;
    self->m_spacing = other->m_spacing;
    self->m_sizeConstraint = other->m_sizeConstraint;
    self->m_menuBar = other->m_menuBar;
    self->m_enabled = other->m_enabled;
    self->m_isDirty = 1;
    self->m_activated = 0;
}

/** @brief 移动布局：转移条目数组/配置/挂接，并修正控件与子布局反向引用。 */
static void VXLayout_move(XLayout* self, XLayout* other)
{
    int i;
    if (!self || !other || self == other) return;
    /* 目标未初始化（全零内存）时必须先初始化，再移动条目。 */
    if (XClassIsVtableNull(self)) XLayout_init(self);
    /* 移动前必须释放目标原有的拥有条目；只释放数组会遗留条目对象。 */
    XLayout_releaseItems(self);
    self->m_items = other->m_items;
    self->m_itemCount = other->m_itemCount;
    self->m_itemCapacity = other->m_itemCapacity;
    self->m_contentsMargins = other->m_contentsMargins;
    self->m_spacing = other->m_spacing;
    self->m_sizeConstraint = other->m_sizeConstraint;
    self->m_base.m_geometry = other->m_base.m_geometry;
    self->m_base.m_alignment = other->m_base.m_alignment;
    self->m_base.m_hasAlignment = other->m_base.m_hasAlignment;
    self->m_parentWidget = other->m_parentWidget;
    self->m_parentLayout = other->m_parentLayout;
    self->m_menuBar = other->m_menuBar;
    self->m_enabled = other->m_enabled;
    for (i = 0; i < self->m_itemCount; ++i) {
        XLayoutItem* item = self->m_items[i];
        if (item && XLayoutItem_layout_base(item))
            ((XLayout*)item)->m_parentLayout = self;
    }
    if (self->m_parentWidget && self->m_parentWidget->m_layout == other)
        self->m_parentWidget->m_layout = self;
    if (self->m_parentLayout) {
        /* 把父布局数组中的 other 条目替换为 self */
        int idx = XLayout_indexOfItem(self->m_parentLayout, (XLayoutItem*)other);
        if (idx >= 0)
            self->m_parentLayout->m_items[idx] = (XLayoutItem*)self;
        else
            self->m_parentLayout = NULL;
    }
    other->m_items = NULL;
    other->m_itemCount = 0;
    other->m_itemCapacity = 0;
    other->m_parentWidget = NULL;
    other->m_parentLayout = NULL;
    other->m_menuBar = NULL;
    other->m_isDirty = 1;
    self->m_isDirty = 1;
    self->m_activated = 0;
}

XVtable* XLayout_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLayout)
    XVTABLE_INHERIT_XCLASS(XLayoutItem);
    void* table[] = {
        VXLayout_addItem,      /* EXLayout_AddItem */
        VXLayout_itemAt,       /* EXLayout_ItemAt */
        VXLayout_takeAt,       /* EXLayout_TakeAt */
        VXLayout_count,        /* EXLayout_Count */
        VXLayout_replaceItemAt /* EXLayout_ReplaceItemAt */
    };
    XVTABLE_ADD_FUNC_LIST_DEFAULT(table);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_IsEmpty, VXLayout_isEmpty);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_Layout, VXLayout_layout);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_ExpandingDirections,
                             VXLayout_expandingDirections);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_SetGeometry, VXLayout_setGeometry);
    XVTABLE_OVERLOAD_DEFAULT(EXLayoutItem_Invalidate, VXLayout_invalidate);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLayout_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXLayout_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXLayout_move);
    return XVTABLE_DEFAULT;
}

void XLayout_init(XLayout* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(XLayout));
    XLayoutItem_init((XLayoutItem*)self);
    XClassSetVtable(self, XLayout);
    XMargins_init(&self->m_contentsMargins, -1, -1, -1, -1);
    self->m_spacing = -1;
    self->m_sizeConstraint = XLayoutSizeConstraint_SetDefault;
    self->m_menuBar = NULL;
    self->m_enabled = 1;
    XRect_init(&self->m_alignmentRectCache, 0, 0, 0, 0);
    XSize_init(&self->m_cachedMin, -1, -1);
    XSize_init(&self->m_cachedMax, -1, -1);
    XSize_init(&self->m_cachedHint, -1, -1);
    XRect_init(&self->m_activateRect, 0, 0, 0, 0);
    self->m_isDirty = 1;
}

/* ==================== 条目管理虚函数入口（对外调度） ==================== */

void XLayout_addItem_base(XLayout* self, XLayoutItem* item)
{
    if (!self || !item || !XClassGetVtable(self)) return;
    XClassGetVirtualFunc(self, EXLayout_AddItem,
                         void(*)(XLayout*, XLayoutItem*))(self, item);
}

XLayoutItem* XLayout_itemAt_base(const XLayout* self, int index)
{
    if (!self || !XClassGetVtable(self)) return NULL;
    return XClassGetVirtualFunc(self, EXLayout_ItemAt,
                                XLayoutItem*(*)(const XLayout*, int))(self, index);
}

XLayoutItem* XLayout_takeAt_base(XLayout* self, int index)
{
    if (!self || !XClassGetVtable(self)) return NULL;
    return XClassGetVirtualFunc(self, EXLayout_TakeAt,
                                XLayoutItem*(*)(XLayout*, int))(self, index);
}

int XLayout_count_base(const XLayout* self)
{
    if (!self || !XClassGetVtable(self)) return 0;
    return XClassGetVirtualFunc(self, EXLayout_Count,
                                int(*)(const XLayout*))(self);
}

XLayoutItem* XLayout_replaceItemAt_base(XLayout* self, int index,
                                        XLayoutItem* item)
{
    if (!self || !XClassGetVtable(self)) return NULL;
    return XClassGetVirtualFunc(self, EXLayout_ReplaceItemAt,
                                XLayoutItem*(*)(XLayout*, int, XLayoutItem*))(
        self, index, item);
}

/* ==================== 受保护助手（供子类与控件接入） ==================== */

int XLayout_appendItem(XLayout* self, XLayoutItem* item, bool owned)
{
    if (!self || !item) return -1;
    if (!XLayout_growItemCapacity(self, self->m_itemCount + 1)) return -1;
    if (owned) item->m_ownedByLayout = 1;
    self->m_items[self->m_itemCount++] = item;
    XLayout_linkItem(self, item);
    self->m_isDirty = 1;
    if (self->m_activated && XLayout_parentWidget(self))
        XLayout_activate(self);
    return self->m_itemCount - 1;
}

int XLayout_insertItemAt(XLayout* self, int index, XLayoutItem* item, bool owned)
{
    int idx;
    if (!self || !item) return -1;
    if (index < 0) index = self->m_itemCount;
    if (index > self->m_itemCount) index = self->m_itemCount;
    if (!XLayout_growItemCapacity(self, self->m_itemCount + 1)) return -1;
    idx = index;
    XMemmove(&self->m_items[idx + 1], &self->m_items[idx],
            (size_t)(self->m_itemCount - idx) * sizeof(XLayoutItem*));
    if (owned) item->m_ownedByLayout = 1;
    self->m_items[idx] = item;
    self->m_itemCount++;
    XLayout_linkItem(self, item);
    self->m_isDirty = 1;
    if (self->m_activated && XLayout_parentWidget(self))
        XLayout_activate(self);
    return idx;
}

int XLayout_indexOfItem(XLayout* self, const XLayoutItem* item)
{
    int i;
    int n;
    if (!self || !item) return -1;
    n = XLayout_count_base(self);
    for (i = 0; i < n; ++i)
        if (XLayout_itemAt_base(self, i) == item)
            return i;
    return -1;
}

void XLayout_attachWidget(XLayout* self, XWidget* widget)
{
    if (!self) return;
    if (self->m_parentLayout) {
        XLayout_removeItem(self->m_parentLayout, (XLayoutItem*)self);
        self->m_parentLayout = NULL;
    }
    self->m_parentWidget = widget;
    if (self->m_parentWidget && self->m_parentWidget->m_layout != self)
        self->m_parentWidget->m_layout = self;
    self->m_isDirty = 1;
    if (widget)
        XLayout_activate(self);
}

void XLayout_detachWidget(XLayout* self)
{
    if (!self) return;
    if (self->m_parentWidget && self->m_parentWidget->m_layout == self)
        self->m_parentWidget->m_layout = NULL;
    self->m_parentWidget = NULL;
}

int XLayout_effectiveSpacing(const XLayout* self)
{
    const XLayout* cur;
    if (!self) return 0;
    cur = self;
    while (cur) {
        if (cur->m_spacing >= 0)
            return cur->m_spacing;
        cur = cur->m_parentLayout;
    }
    return 0;
}

XRect XLayout_contentsRectForRect(const XLayout* self, const XRect* rect)
{
    XMargins m;
    XRect out;
    XRect_init(&out, 0, 0, 0, 0);
    if (!self || !rect)
        return rect ? *rect : out;
    m = XLayout_resolveMargins(self);
    out = XRect_adjusted(rect, m.left, m.top, -m.right, -m.bottom);
    if (out.width < 0) out.width = 0;
    if (out.height < 0) out.height = 0;
    return out;
}

/* [已删除 2026-10-31] XLayout_layoutMargins：Qt 无此 API 且全仓零调用，
 * 定义随之移除（内容边距读取走 XLayout_contentsMargins/resolveMargins）。 */

/* ==================== 通用条目管理（对标 QLayout） ==================== */

void XLayout_removeWidget(XLayout* self, XWidget* widget)
{
    int idx;
    XLayoutItem* item;
    bool owned;
    if (!self || !widget) return;
    idx = XLayout_indexOf(self, widget);
    if (idx < 0) return;
    /* takeAt 会清零 m_ownedByLayout（VXLayout_takeAt），属主必须在取出
       前判定（与 XLayout_releaseItems 同判据）：本入口创建的内部条目
       （addWidget 包装，owned=1）取出后就地释放，杜绝丢弃句柄造成的
       泄漏；经 addItem 显式传入的借用条目释放责任仍在调用方。 */
    item = self->m_items[idx];
    owned = item && item->m_ownedByLayout;
    item = XLayout_takeAt_base(self, idx);
    if (item && owned)
        XClassDelete(item);
}

void XLayout_removeItem(XLayout* self, XLayoutItem* item)
{
    int idx;
    if (!self || !item) return;
    idx = XLayout_indexOfItem(self, item);
    if (idx >= 0)
        XLayout_takeAt_base(self, idx);
}

int XLayout_indexOf(const XLayout* self, const XWidget* widget)
{
    int i;
    if (!self || !widget) return -1;
    for (i = 0; i < self->m_itemCount; ++i) {
        XLayoutItem* item = self->m_items[i];
        if (item && XLayoutItem_widget_base(item) == widget)
            return i;
    }
    return -1;
}

/* [setAlignment 2026-10-31 对齐 qlayout.h:58-59 `bool setAlignment(
 * QWidget, Alignment)` / `bool setAlignment(QLayout, Alignment)`]：返回
 * 是否找到条目并设置成功（Qt 语义）；此前返回 void 且经已删除的
 * itemForWidget 间接寻址，现内联 indexOf 查找。Qt 无「任意条目」公开
 * 重载，setAlignmentItem 已随 itemForWidget 一并删除（零调用方）。 */
bool XLayout_setAlignmentWidget(XLayout* self, XWidget* widget,
                                XLayoutAlignments alignment)
{
    int idx;
    if (!self || !widget) return false;
    idx = XLayout_indexOf(self, widget);
    if (idx < 0) return false;
    XLayoutItem_setAlignment(self->m_items[idx], alignment);
    return true;
}

bool XLayout_setAlignmentLayout(XLayout* self, XLayout* child,
                                XLayoutAlignments alignment)
{
    int idx;
    if (!self || !child) return false;
    idx = XLayout_indexOfItem(self, (const XLayoutItem*)child);
    if (idx < 0) return false;
    XLayoutItem_setAlignment(self->m_items[idx], alignment);
    return true;
}

/* [删除 2026-10-31] XLayout_deleteAllItems：Qt QLayout 无此 API（doc 曾
 * 错标「对标 QLayout::deleteAllItems」——Qt 无此公开接口），且全仓零调用；
 * 条目清理走 releaseItems（deinit）或 takeAt 逐条取出。 */

/* ==================== 内容边距 / 间距 / 对齐矩形 ==================== */

void XLayout_setContentsMargins(XLayout* self, int left, int top,
                                int right, int bottom)
{
    if (!self) return;
    self->m_contentsMargins.left = left;
    self->m_contentsMargins.top = top;
    self->m_contentsMargins.right = right;
    self->m_contentsMargins.bottom = bottom;
    XLayout_invalidate(self);
}

XMargins XLayout_contentsMargins(const XLayout* self)
{
    XMargins out;
    XMargins_init(&out, 0, 0, 0, 0);
    if (!self) return out;
    return XLayout_resolveMargins(self);
}

void XLayout_getContentsMargins(const XLayout* self,
                                int* left, int* top, int* right, int* bottom)
{
    XMargins m;
    if (!self) return;
    m = XLayout_resolveMargins(self);
    if (left) *left = m.left;
    if (top) *top = m.top;
    if (right) *right = m.right;
    if (bottom) *bottom = m.bottom;
}

#if XLAYOUT_TOTAL_ON

/** @brief 清除自定义内容边距，回到默认（对标 QLayout::unsetContentsMargins）。
 * @details 等效 setContentsMargins(-1,-1,-1,-1)；之后 contentsMargins/
 *          getContentsMargins 把 -1 解析为 0（X 无 QStyle 缺省值）。
 * @param  self 目标布局；可为 NULL。 */
void XLayout_unsetContentsMargins(XLayout* self)
{
    XLayout_setContentsMargins(self, -1, -1, -1, -1);
}

/** @brief 返回去除内容边距后的布局几何（对标 QLayout::contentsRect 无参接口）。
 * @details 以布局存储的几何（最近一次 setGeometry 结果）为基准，向内部
 *         收拢 left/top/right/bottom 内容边距；负宽高钳到 0。与受保护
 *         助手 XLayout_contentsRectForRect(rect) 区分：后者对任意给定
 *         矩形收拢，不依赖存储几何。
 * @param  self 目标布局；可为 NULL。
 * @return 内容矩形；失败返回 (0,0,0,0)。 */
XRect XLayout_contentsRect(const XLayout* self)
{
    XMargins m;
    XRect r;
    XRect out;
    XRect_init(&r, 0, 0, 0, 0);
    XRect_init(&out, 0, 0, 0, 0);
    if (!self) return out;
    r = XLayoutItem_geometry_base((XLayoutItem*)self);
    m = XLayout_resolveMargins(self);
    out = XRect_adjusted(&r, m.left, m.top, -m.right, -m.bottom);
    if (out.width < 0) out.width = 0;
    if (out.height < 0) out.height = 0;
    return out;
}
#endif /* XLAYOUT_TOTAL_ON */

void XLayout_setSpacing(XLayout* self, int spacing)
{
    if (!self) return;
    self->m_spacing = spacing;
    XLayout_invalidate(self);
}

int XLayout_spacing(const XLayout* self)
{
    /* [行为修复 2026-10-31 对齐 QLayout::spacing（qlayout.cpp:255）]：
     * Qt 从不返回 -1——未显式设置时解出样式默认间距（X 无样式 → 0，
     * 沿父布局链回退为 XGui 的等效解析）。此前返回原始 -1「未设置」
     * 哨兵，与 Qt 同名不同义；需要原始值读 m_spacing 字段。 */
    return XLayout_effectiveSpacing(self);
}

XRect XLayout_alignmentRect(const XLayout* self, const XRect* rect)
{
    XRect r;
    XRect out;
    XSize pref;
    XLayoutAlignments align;
    XRect_init(&r, 0, 0, 0, 0);
    if (rect) r = *rect;
    if (!self) return r;
    align = XLayoutItem_alignment((XLayoutItem*)self);
    pref = XLayoutItem_sizeHint_base((XLayoutItem*)self);
    out = r;
    if (pref.width >= 0 && pref.width < r.width)
        out.width = pref.width;
    if (pref.height >= 0 && pref.height < r.height)
        out.height = pref.height;
    /* 对标 Qt qlayout.cpp alignmentRect：无显式对齐时按默认居中
       排布（Qt 的 QLayoutItem::alignment 默认 AlignLeft|AlignTop 时
       不改变位置；无对齐位时保持原始矩形——本项目按 Qt 默认
       无对齐位居中语义补齐：X 居中、Y 居中）。 */
    if (!align) {
        out.x = r.x + (r.width - out.width) / 2;
        out.y = r.y + (r.height - out.height) / 2;
        return out;
    }
    if (align & XLayoutAlignment_Left)
        out.x = r.x;
    else if (align & XLayoutAlignment_Right)
        out.x = r.x + r.width - out.width;
    else if (align & XLayoutAlignment_HCenter)
        out.x = r.x + (r.width - out.width) / 2;
    if (align & XLayoutAlignment_Top)
        out.y = r.y;
    else if (align & XLayoutAlignment_Bottom)
        out.y = r.y + r.height - out.height;
    else if (align & XLayoutAlignment_VCenter)
        out.y = r.y + (r.height - out.height) / 2;
        ((XLayout*)self)->m_alignmentRectCache = out;
    return out;
}

/* ==================== 尺寸约束 ==================== */

void XLayout_setSizeConstraint(XLayout* self, XLayoutSizeConstraint constraint)
{
    if (!self) return;
    self->m_sizeConstraint = constraint;
    XLayout_invalidate(self);
}

XLayoutSizeConstraint XLayout_sizeConstraint(const XLayout* self)
{
    if (!self) return XLayoutSizeConstraint_SetDefault;
    return self->m_sizeConstraint;
}

#if XLAYOUT_TOTAL_ON

/* ==================== PC 扩展（XLAYOUT_TOTAL_ON 门控，对标 QLayout） ==================== */

/** @brief 设置菜单栏控件（对标 QLayout::setMenuBar）。
 * @details 菜单栏不计入布局条目，但 total* 合计尺寸会把菜单栏高度附加
 *         到首选/最小/最大高度之上，使顶层控件为菜单栏预留空间。X 的
 *         布局没有 Qt 的 addChildWidget 语义，控件仍需调用方管理可见性
 *         与几何；本实现仅保证菜单栏被重设为挂接控件的子控件
 *         （parentWidget 存在且不一致时）并使布局失效。
 * @param  self   目标布局；可为 NULL。
 * @param  widget 菜单栏控件借用指针；可为 NULL（清除）。 */
void XLayout_setMenuBar(XLayout* self, XWidget* widget)
{
    XWidget* pw;
    if (!self) return;
    pw = XLayout_parentWidget(self);
    if (widget && pw && XWidget_parentWidget(widget) != pw)
        XWidget_setParentPlain(widget, pw);
    self->m_menuBar = widget;
    XLayout_invalidate(self);
}

/** @brief 返回布局菜单栏（对标 QLayout::menuBar）。
 * @param  self 目标布局；可为 NULL。
 * @return 菜单栏借用指针；未设置或失败返回 NULL。 */
XWidget* XLayout_menuBar(const XLayout* self)
{
    if (!self) return NULL;
    return self->m_menuBar;
}

/** @brief 设置布局启用标志（对标 QLayout::setEnabled）。
 * @details 与 Qt 一致：只保存启用标志（默认 true），供事件处理等场景
 *         查询使用；布局算法不会因禁用而隐藏条目或停止几何解算。
 * @param  self   目标布局；可为 NULL。
 * @param  enable 是否启用。 */
void XLayout_setEnabled(XLayout* self, bool enable)
{
    if (!self) return;
    self->m_enabled = enable ? 1u : 0u;
}

/** @brief 查询布局启用标志（对标 QLayout::isEnabled）。
 * @param  self 目标布局；可为 NULL。
 * @return 启用返回 true；失败返回 false。 */
bool XLayout_isEnabled(const XLayout* self)
{
    if (!self) return false;
    return self->m_enabled ? true : false;
}

/** @brief 返回包含菜单栏高度的合计最小尺寸（对标 QLayout::totalMinimumSize）。
 * @details Qt 在顶层布局上附加控件 frame margins，X 无 QStyle/frame
 *         概念故 side/top 恒为 0，仅附加菜单栏高度。
 * @param  self 目标布局；可为 NULL。
 * @return 合计最小尺寸；失败返回 (0,0)。 */
XSize XLayout_totalMinimumSize(const XLayout* self)
{
    XSize s;
    int top;
    XSize_init(&s, 0, 0);
    if (!self) return s;
    s = XLayoutItem_minimumSize_base((XLayoutItem*)self);
    top = XLayout_menuBarHeightForWidth(self->m_menuBar, s.width);
    s.height += top;
    return s;
}

/** @brief 返回包含菜单栏高度的合计首选尺寸（对标 QLayout::totalSizeHint）。
 * @details 布局支持 heightForWidth 时高度按首选宽度对应的 hfw 取值；
 *         顶层 frame margins 计为 0（X 无 QStyle）。
 * @param  self 目标布局；可为 NULL。
 * @return 合计首选尺寸；失败返回 (0,0)。 */
XSize XLayout_totalSizeHint(const XLayout* self)
{
    XSize s;
    int top;
    XSize_init(&s, 0, 0);
    if (!self) return s;
    s = XLayoutItem_sizeHint_base((XLayoutItem*)self);
    if (XLayoutItem_hasHeightForWidth_base((XLayoutItem*)self))
        s.height = XLayoutItem_heightForWidth_base((XLayoutItem*)self, s.width);
    top = XLayout_menuBarHeightForWidth(self->m_menuBar, s.width);
    s.height += top;
    return s;
}

/** @brief 返回包含菜单栏高度的合计最大尺寸（对标 QLayout::totalMaximumSize）。
 * @details 顶层布局按 Qt 语义把附加宽度/高度钳位到 XWIDGET_MAX_SIZE；
 *         X 无 frame margins 故仅菜单栏高度参与钳位。
 * @param  self 目标布局；可为 NULL。
 * @return 合计最大尺寸；失败返回 (XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE)。 */
XSize XLayout_totalMaximumSize(const XLayout* self)
{
    XSize s;
    int top;
    XSize_init(&s, XWIDGET_MAX_SIZE, XWIDGET_MAX_SIZE);
    if (!self) return s;
    s = XLayoutItem_maximumSize_base((XLayoutItem*)self);
    top = XLayout_menuBarHeightForWidth(self->m_menuBar, s.width);
    if (self->m_parentWidget) {
        int w = s.width;
        int h = s.height + top;
        if (w > XWIDGET_MAX_SIZE) w = XWIDGET_MAX_SIZE;
        if (h > XWIDGET_MAX_SIZE) h = XWIDGET_MAX_SIZE;
        s.width = w;
        s.height = h;
    } else {
        s.height += top;
    }
    return s;
}

/** @brief 返回给定宽度下、包含菜单栏高度的合计最小高度（对标
 *         QLayout::totalMinimumHeightForWidth）。
 * @param  self  目标布局；可为 NULL。
 * @param  width 宽度（像素）。
 * @return 合计最小高度；失败返回 0。 */
int XLayout_totalMinimumHeightForWidth(const XLayout* self, int width)
{
    int h;
    if (!self) return 0;
    h = XLayoutItem_minimumHeightForWidth_base((XLayoutItem*)self, width);
    h += XLayout_menuBarHeightForWidth(self->m_menuBar, width);
    return h;
}

/** @brief 返回给定宽度下、包含菜单栏高度的合计首选高度（对标
 *         QLayout::totalHeightForWidth）。
 * @param  self  目标布局；可为 NULL。
 * @param  width 宽度（像素）。
 * @return 合计首选高度；失败返回 0。 */
int XLayout_totalHeightForWidth(const XLayout* self, int width)
{
    int h;
    if (!self) return 0;
    h = XLayoutItem_heightForWidth_base((XLayoutItem*)self, width);
    h += XLayout_menuBarHeightForWidth(self->m_menuBar, width);
    return h;
}

/** @brief 用 to 控件替换布局中 from 控件的条目（对标 QLayout::replaceWidget）。
 * @details 行为与 Qt 6.8 一致：
 *          - from 与 to 相同或无 to/from 时返回 NULL（所有权仍归布局）；
 *          - 只扫描本布局条目；recursive 为 true 时递归到子布局查找；
 *          - 找到后新建控件条目（对齐沿用旧条目），经 ReplaceItemAt 槽
 *            原位替换；子类（盒式/网格）保留该位置的伸展因子/单元格；
 *          - 成功时返回旧条目（所有权转移给调用方，旧控件不再受管理；
 *            经子类 ReplaceItemAt 清空 old 的 owned 标志并把 owned 置给
 *            新条目）；失败返回 NULL 并销毁新建条目。
 * @param  self      目标布局；可为 NULL。
 * @param  from      被替换控件借用指针。
 * @param  to        新控件借用指针。
 * @param  recursive 是否递归查找子布局（true=FindChildrenRecursively）。
 * @return 旧条目（调用方负责释放）；未找到或失败返回 NULL。 */
XLayoutItem* XLayout_replaceWidget(XLayout* self, XWidget* from,
                                   XWidget* to, bool recursive)
{
    int index;
    int n;
    int u;
    XLayoutItem* item;
    XLayoutItem* old;
    XLayoutItem* newItem;
    XWidget* pw;
    if (!self || !from || !to) return NULL;
    if (from == to) return NULL;   /* 所有权仍归布局，不做任何修改 */
    index = -1;
    n = XLayout_count_base(self);
    for (u = 0; u < n; ++u) {
        item = XLayout_itemAt_base(self, u);
        if (!item) continue;
        if (XLayoutItem_widget_base(item) == from) {
            index = u;
            break;
        }
        if (recursive) {
            XLayout* sub = XLayoutItem_layout_base(item);
            XLayoutItem* r;
            if (!sub) continue;
            r = XLayout_replaceWidget(sub, from, to, true);
            if (r) return r;
        }
    }
    if (index < 0) return NULL;
    /* 对标 Qt addChildWidget：把 to 挂到本布局的父控件下。 */
    pw = XLayout_parentWidget(self);
    if (pw && XWidget_parentWidget(to) != pw)
        XWidget_setParentPlain(to, pw);
    newItem = XLayoutItem_createWidgetItem(to);
    if (!newItem) return NULL;
    XLayoutItem_setAlignment(newItem, XLayoutItem_alignment(item));
    old = XLayout_replaceItemAt_base(self, index, newItem);
    if (!old) {
        XClassDelete(newItem);
        return NULL;
    }
    XLayout_invalidate(self);
    return old;
}

/** @brief 返回满足控件全部尺寸约束且尽可能接近 size 的尺寸（对标
 *         Qt 6.8 QLayout::closestAcceptableSize 静态接口）。
 * @details result = size 收缩到控件最大尺寸、再展开到最小尺寸；布局有
 *         heightForWidth 且 result 高度小于 minimumHeightForWidth
 *         (result.width) 时，按 Qt 原算法补高（常数 hfw 与“当前尺寸
 *         不满足”用 newHfw；否则逐宽二分）。
 * @param  widget 目标控件借用指针；可为 NULL。
 * @param  size   期望尺寸。
 * @return 满足约束的尺寸；widget 为 NULL 时返回 (0,0)。 */
XSize XLayout_closestAcceptableSize(const XWidget* widget, XSize size)
{
    XLayout* l;
    XSize result;
    XSize max;
    XSize min;
    XSize current;
    int currentHfw;
    int newHfw;
    int maxw;
    int maxh;
    int minw;
    int minh;
    int minhfw;
    int maxhfw;
    XSize_init(&result, 0, 0);
    if (!widget) return result;
    max = XLayout_qSmartMaxSize(widget);
    min = XLayout_qSmartMinSize(widget);
    result = XSize_boundedTo(&size, &max);
    result = XSize_expandedTo(&result, &min);
    l = XWidget_layout((XWidget*)widget);
    if (l && XLayoutItem_hasHeightForWidth_base((XLayoutItem*)l) &&
        result.height <
            XLayoutItem_minimumHeightForWidth_base((XLayoutItem*)l, result.width)) {
        current = XWidget_size((const XWidget*)widget);
        currentHfw = XLayoutItem_minimumHeightForWidth_base(
            (XLayoutItem*)l, current.width);
        newHfw = XLayoutItem_minimumHeightForWidth_base(
            (XLayoutItem*)l, result.width);
        if (current.height < currentHfw || currentHfw == newHfw) {
            /* 常数 hfw、垂直收缩与“当前尺寸不正确”统一取 newHfw。 */
            result.height = newHfw;
        } else {
            /* 假设 hfw 随宽度单调不增，逐宽二分逼近。 */
            maxw = XWidget_width((const XWidget*)widget);
            if (result.width > maxw) maxw = result.width;
            maxh = XWidget_height((const XWidget*)widget);
            if (result.height > maxh) maxh = result.height;
            minw = XWidget_width((const XWidget*)widget);
            if (result.width < minw) minw = result.width;
            minh = XWidget_height((const XWidget*)widget);
            if (result.height < minh) minh = result.height;
            minhfw = XLayoutItem_minimumHeightForWidth_base(
                (XLayoutItem*)l, minw);
            maxhfw = XLayoutItem_minimumHeightForWidth_base(
                (XLayoutItem*)l, maxw);
            while (minw < maxw) {
                if (minhfw > maxh) {       /* 假定 hfw 递减 */
                    minw = maxw - (maxw - minw) / 2;
                    minhfw = XLayoutItem_minimumHeightForWidth_base(
                        (XLayoutItem*)l, minw);
                } else if (maxhfw < minh) {  /* 假定 hfw 递减 */
                    maxw = minw + (maxw - minw) / 2;
                    maxhfw = XLayoutItem_minimumHeightForWidth_base(
                        (XLayoutItem*)l, maxw);
                } else {
                    break;
                }
            }
            XSize_init(&max, minw, minhfw);
            result = XSize_expandedTo(&result, &max);
        }
    }
    return result;
}
#endif /* XLAYOUT_TOTAL_ON */

/* ==================== 挂接与激活 ==================== */

XWidget* XLayout_parentWidget(const XLayout* self)
{
    const XLayout* cur;
    if (!self) return NULL;
    cur = self;
    while (cur) {
        if (cur->m_parentWidget)
            return cur->m_parentWidget;
        cur = cur->m_parentLayout;
    }
    return NULL;
}

bool XLayout_activate(XLayout* self)
{
    XWidget* w;
    XRect r;
    if (!self) return false;
    /* [enabled 早退 2026-10-31 对齐 qlayout.cpp:1000 `if (!d->enabled ||
     * !parent()) return false;`]：禁用布局不重解算（Qt widgetEvent 同样
     * 在事件入口按 enabled 拦截，见 VXWidget_event 的 LayoutRequest 分支）。
     * m_enabled 默认 true，未调用 setEnabled 的布局行为不变。 */
    if (!self->m_enabled) return false;
    w = XLayout_parentWidget(self);
    if (!w) return false;
    /* [子布局上推委托 2026-10-31 对齐 qlayout.cpp:1002 `if (!d->topLevel)
     * return static_cast<QLayout*>(parent())->activate();`]：对子布局调
     * activate 即委托父布局激活（整树自顶重解算），子布局从不独立解算
     * ——此前本函数对子布局只做局部重解算，与 Qt 同名不同义，已修复。 */
    if (self->m_parentLayout)
        return XLayout_activate(self->m_parentLayout);
    /* 以控件客户区（自身坐标）为分配矩形。 */
    r = XWidget_geometry(w);
    r.x = 0;
    r.y = 0;
    /* [幂等早退 2026-10-17] 已激活+几何未变+未失效 → 跳过全量度量与
     * 分配（对标 Qt QLayout::activate 的 activated 早退）。一次窗口
     * resize 经根→中段→堆叠→页根级联，同一级布局被重复 activate 且
     * 传入相同矩形（demo_navUpdatePanel 双激活/updateGeometry 循环等），
     * 每次重复都全量重算尺寸度量——布局度量占拖拽 resize 步耗时大头
     * （堆叠布局一次 minimumSize 遍历全部 20 页根整树）。几何变化/
     * invalidate 均强制重解算（m_isDirty 由失效路径置位）。 */
    if (self->m_activated && !self->m_isDirty &&
        self->m_activateRect.x == r.x && self->m_activateRect.y == r.y &&
        self->m_activateRect.width == r.width &&
        self->m_activateRect.height == r.height)
    {
        if (xlayout_lprof_gate()) ++xlayout_lprofActSkip;
        return true;
    }
    /* [lprof] 真重解算计时：覆盖约束写回 + 整树度量 + 几何分配全程。 */
    {
        int64_t lprofStart = xlayout_lprof_gate()
                                 ? XDateTime_currentNSecsSinceEpoch() : 0;
        /* 按尺寸约束把布局尺寸写回挂接控件（对标 QLayout::activate）。 */
    switch (self->m_sizeConstraint) {
    case XLayoutSizeConstraint_SetDefault:
        /* [Qt 对齐 2026-10-17] SetDefault 不做尺寸回写——Qt
         * QLayout::activate 的 default 约束是 no-op（仅显式
         * SetMinimumSize/SetFixedSize 等才写回）。此前 SetDefault 与
         * SetMinimumSize 合并回写，级联后果：堆叠布局把 20 页最大
         * 合计最小尺寸写成 contentHost 显式 min → 中段布局 min 膨胀
         * → 根布局把 40+692+26=758 写成窗口 PMinSize → 平台侧
         * XWindow_setMinMaxSize 夹紧把活窗口撑到 758 → 根重分布时
         * mid 最小 692 顶穿可用 664，状态栏被挤出窗口、下停靠导航
         * 条盖住状态栏（DDE 探针链实证）。约束级联本就不该跨层
         * 传播：分配不足时由父布局分布裁决。 */
        break;
    case XLayoutSizeConstraint_SetMinimumSize: {
        XSize min = XLayoutItem_minimumSize_base((XLayoutItem*)self);
        XWidget_setMinimumSize(w, min.width, min.height);
        break;
    }
    case XLayoutSizeConstraint_SetFixedSize: {
        XSize hint = XLayoutItem_sizeHint_base((XLayoutItem*)self);
        XWidget_setMinimumSize(w, hint.width, hint.height);
        XWidget_setMaximumSize(w, hint.width, hint.height);
        break;
    }
    case XLayoutSizeConstraint_SetMaximumSize: {
        XSize max = XLayoutItem_maximumSize_base((XLayoutItem*)self);
        XWidget_setMaximumSize(w, max.width, max.height);
        break;
    }
    case XLayoutSizeConstraint_SetMinAndMaxSize: {
        XSize min = XLayoutItem_minimumSize_base((XLayoutItem*)self);
        XSize max = XLayoutItem_maximumSize_base((XLayoutItem*)self);
        XWidget_setMinimumSize(w, min.width, min.height);
        XWidget_setMaximumSize(w, max.width, max.height);
        break;
    }
    case XLayoutSizeConstraint_SetNoConstraint:
    default:
        break;
    }
    /* 按分配矩形重新解算（r 已在函数头取定）。 */
    XLayoutItem_setGeometry_base((XLayoutItem*)self, &r);
    self->m_activateRect = r;
    /* [Qt 对齐 2026-10-31 activate 尾部（qlayout.cpp:1063
     * `mw->updateGeometry()`）] 布局解算落地后宿主合计提示已变化：宿主父级
     * 有布局则 invalidate，无布局且可见则投 LayoutRequest——滚动区族
     * 内容件提示变化（标签文本增长）由此链通知滚动区重排版。
     * 早退路径不投（Qt activate 早退同样跳过尾部）防事件乒锯。 */
    {
        XWidget* pUp = XWidget_parentWidget(w);
        if (pUp) {
            if (pUp->m_layout)
                XLayout_invalidate(pUp->m_layout);
            else if (XWidget_isVisible(pUp) && !XWidget_isWindow(w))
                XWidget_postLayoutRequest(pUp);
        }
    }
        if (lprofStart)
        {
            ++xlayout_lprofAct;
            xlayout_lprofSolveNs +=
                XDateTime_currentNSecsSinceEpoch() - lprofStart;
        }
    }
    return true;
}

void XLayout_update(XLayout* self)
{
    /* [行为修复 2026-10-31 对齐 QLayout::update（qlayout.cpp:971）]：
     * Qt 的 update() 是**失效沿布局链的门控上推**（逐级置未激活 + 到链
     * 顶向宿主 post LayoutRequest），不是重画——此前本函数被错标成
     * 「重画所有条目」（走条目逐个 XWidget_update 的重绘遍历），与 Qt
     * 同名不同义，已按 Qt 语义修复。需要重画的调用方用
     * XWidget_update(宿主)；需要失效+重取度量的调用方用
     * XLayout_invalidate（Qt 惯用口令：改条目后 invalidate 再读
     * totalSizeHint）。 */
    if (!self) return;
    xlayout_update_impl(self);
}

void XLayout_invalidate(XLayout* self)
{
    if (!self) return;
    XLayoutItem_invalidate_base((XLayoutItem*)self);
}

void XLayout_addWidget(XLayout* self, XWidget* widget)
{
    XLayoutItem* item;
    int before;
    if (!self || !widget) return;
    item = XLayoutItem_createWidgetItem(widget);
    if (!item) return;
    /* 根因(R-92)：条目由本入口创建、调用方无法释放，须归布局所有
       （XLayout.h 所有权契约：addWidget 等自动创建的内部条目 deinit
       时自动释放）。基类/子类 addItem 虚路径只按 owned 入参置位、
       从不清除 m_ownedByLayout，故挂入前置位即可；子类 insert*
       路径本就传 owned=true，行为不变无回归。 */
    item->m_ownedByLayout = 1;
    before = self->m_itemCount;
    XLayout_addItem_base(self, item);
    if (self->m_itemCount == before) {
        /* 虚路径插入失败（扩容失败）：条目未入列、所有权未随挂接移出，
           由本入口就地释放防孤儿块（网格 ensureCells 失败路径经 takeAt
           移出后计数同样回落至此，一并覆盖）。 */
        XClassDelete(item);
    }
}

#endif /* XLAYOUT_ON */

#endif /* XGUI_ON */
