/**
 * @file       XItemDelegatePaintAlignTest.c
 * @brief      XItemDelegate §B5 绘制面（paint / sizeHint / clipping /
 *             itemEditorFactory / 绘制面查委托 P3-46）× Qt 6.8 对齐契约测试。
 * @details    对齐依据（Qt 6.8.3 源码
 *             /home/xinyue/Qt-src/6.8.3/Src/qtbase/src/widgets/itemviews/）：
 *             - qitemdelegate.cpp:45 `QItemDelegatePrivate() : f(nullptr),
 *               clipPainting(true) {}` 与 :69 `bool clipPainting;`——裁剪
 *               缺省开启、编辑器工厂缺省空；
 *             - qitemdelegate.cpp:325-334 `hasClipping()` 返回
 *               `d->clipPainting`、`setClipping(clip)` 仅写该位；
 *             - qitemdelegate.cpp:366-434 `paint(painter, option, index)`：
 *               `painter->save(); if (d->clipPainting)
 *               painter->setClipRect(opt.rect); … drawBackground/drawCheck/
 *               drawDecoration/drawDisplay/drawFocus … painter->restore();`
 *               ——裁剪作用域严格限定在本次绘制内；
 *             - qitemdelegate.cpp:565-581 `itemEditorFactory()` 返回 `d->f`、
 *               `setItemEditorFactory(f)` 仅写指针（借用，不销毁）；
 *             - qstyleditemdelegate.cpp:391-403 `sizeHint` 无 SizeHintRole
 *               时回落 `style->sizeFromContents(CT_ItemViewItem, …)`（恒返回
 *               真值；本框架缺省返回 0×0 作「无意见」哨兵，见
 *               XItemDelegate.h 的有意偏离说明）。
 *
 *             P3-46（台账 :554「委托绘制面仍不查委托，本行不承诺绘制改变」）
 *             的兑现口径在本文件锁三件事：
 *             ① `XItemDelegate_paintOverridden` 精确判定「是否覆写 Paint」
 *                （缺省委托 false；仅覆写 createEditor 的子类仍 false；
 *                覆写 Paint 的子类 true）；
 *             ② 视图绘制面经 `XAbstractItemView_resolveDelegate` 解析生效
 *                委托并按上述判定分流——两视图接入形态不同（2026-11-15
 *                复核订正）：XTableView 覆写者**接管内容自绘**（跳过
 *                xtv_drawCellContent，底色/网格线仍视图画）、XTableWidget
 *                覆写者在视图既有 item role 自绘上**叠加**（非完全接管），
 *                缺省者两视图均回落自绘（像素零漂移）；
 *             ③ `paint_base` 的裁剪确实按 `hasClipping()` 生效，且
 *                restore 后不留残影（Qt 的 save/restore 语义）。
 *
 * @note       C99；所有对象释放，漏删即运行器判 `[LEAK]`。绘制一律走
 *             `XImage_init_ex` + `XPainter_begin_image` 离屏路径，不依赖
 *             任何窗口平台。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGuiConfig.h"

#if XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON

#include "XItemDelegate.h"
#include "XTableView.h"
#include "XTableWidget.h"
#include "XAbstractItemModel.h"
#include "XAbstractItemView.h"
#include "XStyleOption.h"
#include "XImage.h"
#include "XPainter.h"
#include "XClass.h"
#include "XWidget.h"

/* ==================== 绘制探针：记录 Paint/SizeHint 的调用事实 ==================== */

/** @brief Paint 虚槽调用记录（跨用例清零）。 */
typedef struct XQIdPaintProbe
{
    int paintCalls;          /**< Paint 被调用次数。 */
    int lastRow;             /**< 最近一次行号。 */
    int lastCol;             /**< 最近一次列号。 */
    XRect lastRect;          /**< 最近一次的条目矩形。 */
    int32_t lastState;       /**< 最近一次的状态位。 */
    const char* lastText;    /**< 最近一次收到的显示文本（借用，不释放）。 */
    bool clipAtEntry;        /**< 进入 Paint 时绘制器已有裁剪。 */
    XRect clipAtEntryRect;   /**< 进入 Paint 时的裁剪包围盒。 */
    int sizeHintCalls;       /**< SizeHint 被调用次数。 */
    XSize hintToReturn;      /**< SizeHint 的返回值（用量宽试验）。 */
    bool fillPixel;          /**< 是否在条目矩形内填一个像素（验证裁剪）。 */
    XRect firstRect;         /**< 第一次 Paint 的条目矩形（左上格；⑦ 像素
                                  取证定位 (0,0) 格用）。尾部追加。 */
    bool hasFirstRect;       /**< firstRect 是否有效（paintCalls≥1 且 option
                                  非 NULL 后置位；XMemset 清零即复位）。 */
} XQIdPaintProbe;

static XQIdPaintProbe g_idProbe;

/**
 * @brief 探针委托：派生自 XItemDelegate，同时覆写 Paint 与 SizeHint。
 *
 * 结构体尾部追加字段（结构体初值铁律），m_base 必为首成员。
 */
XCLASS_DEFINE_BEGING(XQIdProbeDelegate)
XCLASS_DEFINE_EXTEND_END(XQIdProbeDelegate, XItemDelegate)

/* 第二套虚表（只覆写 SizeHint）需要自己的枚举块，否则
 * XVTABLE_INIT_DEFAULT(XQIdHintOnlyDelegate) 展开的 EXQIdHintOnlyDelegate_*
 * 符号不存在。 */
XCLASS_DEFINE_BEGING(XQIdHintOnlyDelegate)
XCLASS_DEFINE_EXTEND_END(XQIdHintOnlyDelegate, XItemDelegate)

typedef struct XQIdProbeDelegate
{
    XItemDelegate m_base;    /**< 基类；必须是第一个。 */
    bool m_paint;            /**< 是否覆写 Paint（false 时回落缺省槽）。 */
    bool m_sizeHint;         /**< 是否覆写 SizeHint。 */
} XQIdProbeDelegate;

/** @brief 探针 Paint：记录调用事实并（可选）在条目矩形内画一点。 */
static void xqId_probePaint(XItemDelegate* self, XPainter* painter,
                            XStyleOptionViewItem* option,
                            XAbstractItemView* view, int row, int col)
{
    XQIdProbeDelegate* probe = (XQIdProbeDelegate*)self;

    (void)view;
    ++g_idProbe.paintCalls;
    g_idProbe.lastRow = row;
    g_idProbe.lastCol = col;
    g_idProbe.lastText = option ? option->m_base.m_text : NULL;
    if (option) {
        g_idProbe.lastRect = option->m_base.m_rect;
        g_idProbe.lastState = option->m_base.m_state;
        /* 第一格（左上，通常即 (0,0)）的条目矩形：⑦ 叠加像素取证要按格
         * 坐标取像素，lastRect 是最后一格不可用。 */
        if (g_idProbe.paintCalls == 1) {
            g_idProbe.firstRect = g_idProbe.lastRect;
            g_idProbe.hasFirstRect = true;
        }
    }
    /* 进入时是否已有裁剪（paint_base 按 hasClipping 施加，Qt
     * qitemdelegate.cpp:376-377 的 save + setClipRect 语义）。 */
    if (painter) {
        g_idProbe.clipAtEntry = XPainter_hasClipping(painter);
        if (g_idProbe.clipAtEntry)
            XPainter_clipBoundingRect(painter, &g_idProbe.clipAtEntryRect);
        if (probe && probe->m_paint && g_idProbe.fillPixel && option) {
            /* 条目矩形**左半**填黑（不盖满整格）：⑥ 用它验证落笔必在
             * 条目矩形内（裁剪生效则必然落在图内）；⑦ 用它做「叠加」像素
             * 取证——委托只画左半，右半仍露出视图画的 item 背景 ⇒ 一次
             * grab 同时证明「委托绘制执行」与「视图自绘未被替换」两件事。 */
            XRect half = option->m_base.m_rect;
            half.width /= 2;
            XPainter_fillRect(painter, &half, 0xFF000000u);
        }
    }
}

/** @brief 探针 SizeHint：记录调用并返回预设尺寸。 */
static XSize xqId_probeSizeHint(XItemDelegate* self,
                                XStyleOptionViewItem* option,
                                XAbstractItemView* view, int row, int col)
{
    (void)self;
    (void)option;
    (void)view;
    (void)row;
    (void)col;
    ++g_idProbe.sizeHintCalls;
    return g_idProbe.hintToReturn;
}

/* 两套虚表：只覆写 SizeHint（Paint 留缺省）与同时覆写 Paint+SizeHint，
 * 用于区分「覆写别的槽不算接管绘制」的精确判定。这两个 class_init 的
 * 名字必须与 XClassSetVtable 展开的符号逐字一致。 */
static XVtable* XQIdHintOnlyDelegate_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQIdHintOnlyDelegate)
    XVTABLE_INHERIT_XCLASS(XItemDelegate);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_SizeHint, xqId_probeSizeHint);
    return XVTABLE_DEFAULT;
}

static XVtable* XQIdProbeDelegate_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XQIdProbeDelegate)
    XVTABLE_INHERIT_XCLASS(XItemDelegate);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_Paint, xqId_probePaint);
    XVTABLE_OVERLOAD_DEFAULT(EXItemDelegate_SizeHint, xqId_probeSizeHint);
    return XVTABLE_DEFAULT;
}

/** @brief 初始化探针委托（栈对象；析构用 XClassDeinit）。 */
static void xqId_probeInit(XQIdProbeDelegate* self, bool paint, bool hint)
{
    if (!self) return;
    XItemDelegate_init(&self->m_base);
    XClassSetVtable(self, XQIdProbeDelegate);
    self->m_paint = paint;
    self->m_sizeHint = hint;
}

/** @brief 把探针的 SizeHint 槽换成「只覆写 SizeHint」的虚表。 */
static void xqId_probeHintOnly(XQIdProbeDelegate* self)
{
    if (!self) return;
    XClassSetVtable(self, XQIdHintOnlyDelegate);
}

/** @brief 清零探针记录。 */
static void xqId_probeReset(void)
{
    XMemset(&g_idProbe, 0, sizeof(g_idProbe));
}

/** @brief 建一个离屏绘制器（调用方负责 XPainter_end/deinit + 删图）。 */
static bool xqId_beginImage(XPainter* painter, XImage* image,
                            int w, int h)
{
    if (!painter || !image) return false;
    XImage_init_ex(image, w, h, XImageFormat_ARGB32);
    if (XImage_isNull(image)) return false;
    XImage_fill(image, 0u);
    XPainter_init(painter, NULL);
    if (!XPainter_begin_image(painter, image)) {
        XPainter_deinit(painter);
        return false;
    }
    return true;
}

/* ==================== ① hasClipping / setClipping（qitemdelegate.cpp:325-334） ==================== */

/** @brief 裁剪开关缺省 true、可写回、空参安全。 */
XQ_TEST(XItemDelegatePaint, clippingDefaultsAndSetter)
{
    XItemDelegate* d = XItemDelegate_create();
    XQ_REQUIRE(d != NULL);

    /* Qt QItemDelegatePrivate() : f(nullptr), clipPainting(true) {}。 */
    XQ_CHECK(XItemDelegate_hasClipping(d));
    XItemDelegate_setClipping(d, false);
    XQ_CHECK(!XItemDelegate_hasClipping(d));
    XItemDelegate_setClipping(d, true);
    XQ_CHECK(XItemDelegate_hasClipping(d));

    /* 空参安全：查询 NULL 返回 false（不是 true），设置 NULL 无影响——
     * 后一件事用**真实委托的状态未变**来证明（而不是 XQ_CHECK(true)）。 */
    XQ_CHECK(!XItemDelegate_hasClipping(NULL));
    XItemDelegate_setClipping(NULL, true);   /* 只可能崩或误改别的对象。 */
    XQ_CHECK(!XItemDelegate_hasClipping(NULL));
    XQ_CHECK(XItemDelegate_hasClipping(d));  /* 上面的 NULL 调用没写坏 d。 */

    XClassDelete((XClass*)d);
}

/* ==================== ② itemEditorFactory / setItemEditorFactory
   （qitemdelegate.cpp:565-581；仅承载借用） ==================== */

/** @brief 工厂指针缺省 NULL、原样往返、空参安全、借用不析构。 */
XQ_TEST(XItemDelegatePaint, itemEditorFactoryRoundTrip)
{
    XItemDelegate* d = XItemDelegate_create();
    /* 堆哨兵块：委托若误「接管并析构」工厂，这里的内容/可读性会被破坏
     * 或被释放（用内容不变 + 可回读来证明只是借用）。 */
    static const int kSentinel[4] = { 0x5A5A1234, -7, 0x00C0FFEE, 42 };
    int* factory = (int*)XMemory_malloc(sizeof(kSentinel),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    void* got;

    XQ_REQUIRE(d != NULL);
    XQ_REQUIRE(factory != NULL);
    XMemcpy(factory, kSentinel, sizeof(kSentinel));
    /* 缺省 NULL（Qt :45 `f(nullptr)`）。 */
    XQ_CHECK(XItemDelegate_itemEditorFactory(d) == NULL);

    XItemDelegate_setItemEditorFactory(d, factory);
    got = XItemDelegate_itemEditorFactory(d);
    XQ_CHECK(got == (void*)factory);

    /* NULL 清除 ⇒ 回落内嵌行编辑器缺省路径。 */
    XItemDelegate_setItemEditorFactory(d, NULL);
    XQ_CHECK(XItemDelegate_itemEditorFactory(d) == NULL);

    /* 空参安全：查询 NULL 返回 NULL；设置 NULL 不得改动其它委托。 */
    XQ_CHECK(XItemDelegate_itemEditorFactory(NULL) == NULL);
    XItemDelegate_setItemEditorFactory(d, factory);
    XItemDelegate_setItemEditorFactory(NULL, NULL);
    XQ_CHECK(XItemDelegate_itemEditorFactory(d) == (void*)factory);

    /* 真正证明「借用」：委托析构后哨兵块内容逐字节未变（仍可安全读）。 */
    XClassDelete((XClass*)d);
    XQ_EQ_I64(factory[0], kSentinel[0]);
    XQ_EQ_I64(factory[1], kSentinel[1]);
    XQ_EQ_I64(factory[2], kSentinel[2]);
    XQ_EQ_I64(factory[3], kSentinel[3]);
    XMemory_free(factory, XCLASS_DEFAULT_MEMORY_TYPE);
}

/* ==================== ③ 缺省 Paint/SizeHint 是「无意见」哨兵 ==================== */

/** @brief 缺省委托：paintOverridden=false、paint_base 无操作、sizeHint 0×0。 */
XQ_TEST(XItemDelegatePaint, defaultDelegateIsNoOp)
{
    XItemDelegate* d = XItemDelegate_create();
    XImage image;
    XPainter painter;
    XStyleOptionViewItem opt;
    XSize sh;
    bool began;

    XQ_REQUIRE(d != NULL);
    /* 缺省委托未覆写 Paint ⇒ 绘制归属仍归视图（P3-46 的零漂移前提）。 */
    XQ_CHECK(!XItemDelegate_paintOverridden(d));

    xqId_probeReset();
    began = xqId_beginImage(&painter, &image, 32, 16);
    XQ_CHECK(began);   /* 失败不早退：后续仍要走到末尾的 XClassDeinit(&image)。 */
    if (began) {
        XStyleOptionViewItem_init(&opt, XStyleCE_ItemViewItem);
        opt.m_base.m_rect = (XRect){ 2, 3, 20, 10 };
        opt.m_base.m_state = XStyleState_Enabled | XStyleState_Item;
        /* 缺省 Paint 无操作：不改裁剪、不绘制（对比 Qt 缺省实现会画整套
         * 条目——本框架缺省槽是「无意见」哨兵，视图自绘承担，见头文件
         * 的有意偏离说明）。 */
        XItemDelegate_paint_base(d, &painter, &opt, NULL, 0, 0);
        XQ_CHECK(!XPainter_hasClipping(&painter));
        XPainter_end(&painter);
        XPainter_deinit(&painter);
    } else {
        XStyleOptionViewItem_init(&opt, XStyleCE_ItemViewItem);
    }
    /* XImage 是本类对象（XImage_init_ex 分配 XImageData + 像素缓冲），
     * XPainter_end/deinit 只收绘制器自身状态、不释放目标图 ⇒ 必须
     * XClassDeinit（同 XLabelAlignTest.c:363 写法）。 */
    XClassDeinit((XClass*)&image);

    /* 缺省 SizeHint = 0×0 ⇒ 视图侧 `if (sh.width > content)` 不干预。 */
    sh = XItemDelegate_sizeHint_base(d, &opt, NULL, 0, 0);
    XQ_EQ_I64(sh.width, 0);
    XQ_EQ_I64(sh.height, 0);

    /* 空参：paint_base 不崩、sizeHint_base 返回 0×0。 */
    XItemDelegate_paint_base(NULL, NULL, NULL, NULL, 0, 0);
    sh = XItemDelegate_sizeHint_base(NULL, NULL, NULL, 0, 0);
    XQ_EQ_I64(sh.width, 0);
    XQ_EQ_I64(sh.height, 0);

    XClassDelete((XClass*)d);
}

/* ==================== ④ paintOverridden 的精确判定（不误判别的槽） ==================== */

/** @brief 只覆写 SizeHint 的子类不得被判为「接管绘制」。 */
XQ_TEST(XItemDelegatePaint, overriddenDetectsPaintSlotOnly)
{
    XQIdProbeDelegate hintOnly;
    XQIdProbeDelegate both;
    XImage image;
    XPainter painter;
    XStyleOptionViewItem opt;
    bool began;

    xqId_probeReset();

    /* 子类 A：只覆写 SizeHint。 */
    xqId_probeInit(&hintOnly, false, true);
    xqId_probeHintOnly(&hintOnly);
    XQ_CHECK(!XItemDelegate_paintOverridden(&hintOnly.m_base));

    /* 子类 B：同时覆写 Paint + SizeHint。 */
    xqId_probeInit(&both, true, true);
    XQ_CHECK(XItemDelegate_paintOverridden(&both.m_base));

    began = xqId_beginImage(&painter, &image, 40, 20);
    XQ_CHECK(began);   /* 失败不早退：末尾仍要 XClassDeinit(&image)。 */
    if (began) {
        XStyleOptionViewItem_init(&opt, XStyleCE_ItemViewItem);
        opt.m_base.m_rect = (XRect){ 4, 4, 24, 10 };
        opt.m_base.m_text = "格";
        /* 子类 B 的 Paint 实际被分派到（调用事实）。 */
        XItemDelegate_paint_base(&both.m_base, &painter, &opt, NULL, 2, 3);
        XQ_EQ_I64(g_idProbe.paintCalls, 1);
        XQ_EQ_I64(g_idProbe.lastRow, 2);
        XQ_EQ_I64(g_idProbe.lastCol, 3);
        XQ_EQ_STR(g_idProbe.lastText, "格");
        XQ_EQ_I64(g_idProbe.lastRect.x, 4);
        XQ_EQ_I64(g_idProbe.lastRect.width, 24);

        /* 子类 A 的 paint_base 落到缺省槽（无调用、无裁剪）。 */
        XItemDelegate_paint_base(&hintOnly.m_base, &painter, &opt, NULL, 2, 3);
        XQ_EQ_I64(g_idProbe.paintCalls, 1);
        XQ_CHECK(!XPainter_hasClipping(&painter));

        XPainter_end(&painter);
        XPainter_deinit(&painter);
    }
    /* XImage 须显式析构（XPainter_end 不释放目标图）。 */
    XClassDeinit((XClass*)&image);

    XClassDeinit((XClass*)&hintOnly);
    XClassDeinit((XClass*)&both);
}

/* ==================== ⑤ paint_base 的裁剪作用域（qitemdelegate.cpp:375-377/434） ==================== */

/** @brief hasClipping=true 时进入 Paint 已有条目矩形裁剪；false 时没有。 */
XQ_TEST(XItemDelegatePaint, paintBaseAppliesClippingScope)
{
    XQIdProbeDelegate probe;
    XImage image;
    XPainter painter;
    XStyleOptionViewItem opt;
    bool began;

    xqId_probeReset();
    xqId_probeInit(&probe, true, false);
    began = xqId_beginImage(&painter, &image, 64, 32);
    XQ_CHECK(began);   /* 失败不早退：末尾仍要 XClassDeinit(&image)。 */
    if (began) {
        XStyleOptionViewItem_init(&opt, XStyleCE_ItemViewItem);
        opt.m_base.m_rect = (XRect){ 8, 6, 20, 12 };

        /* 开启裁剪：Paint 进入时已有裁剪，包围盒 = 条目矩形。 */
        XItemDelegate_setClipping(&probe.m_base, true);
        XItemDelegate_paint_base(&probe.m_base, &painter, &opt, NULL, 0, 0);
        XQ_CHECK(g_idProbe.clipAtEntry);
        XQ_EQ_I64(g_idProbe.clipAtEntryRect.x, 8);
        XQ_EQ_I64(g_idProbe.clipAtEntryRect.y, 6);
        XQ_EQ_I64(g_idProbe.clipAtEntryRect.width, 20);
        XQ_EQ_I64(g_idProbe.clipAtEntryRect.height, 12);
        /* save/restore 成对：返回后裁剪已撤销，不泄漏给后续格/网格线。 */
        XQ_CHECK(!XPainter_hasClipping(&painter));

        /* 关闭裁剪：Paint 进入时无裁剪。 */
        g_idProbe.clipAtEntry = true;
        XItemDelegate_setClipping(&probe.m_base, false);
        XItemDelegate_paint_base(&probe.m_base, &painter, &opt, NULL, 0, 0);
        XQ_CHECK(!g_idProbe.clipAtEntry);
        XQ_CHECK(!XPainter_hasClipping(&painter));

        /* 调用次数累计两次（两档都真调了 Paint）。 */
        XQ_EQ_I64(g_idProbe.paintCalls, 2);

        XPainter_end(&painter);
        XPainter_deinit(&painter);
    }
    /* XImage 须显式析构（XPainter_end 不释放目标图）。 */
    XClassDeinit((XClass*)&image);
    XClassDeinit((XClass*)&probe);
}

/* ==================== ⑥ P3-46：视图绘制面真的查委托（XTableView） ==================== */

/**
 * @brief 覆写 Paint 的委托接管 XTableView 的单元格**内容**自绘
 *        （P3-46 接线取证；选中/交替行底色与网格线仍由视图画，
 *        见 paintOverridden @note——不是像素级完全接管）。
 *
 * 取证方式：离屏 `XWidget_grab` 触发一次真实 paintEvent，断言委托 Paint
 * 被实际调用（次数 > 0）、收到的条目矩形非空、显示文本来自模型（模型 →
 * 视图 → 委托三段贯通）；随后把视图委托置 NULL（回落内建缺省委托）再
 * 渲染一次，Paint 调用数归零 ⇒ 绘制归属确实由委托解析结果决定，而非
 * 无条件调用。
 */
XQ_TEST(XItemDelegatePaint, tableViewPaintConsultsDelegate)
{
    /* 视图挂在宿主控件下 ⇒ m_isWindow=0 ⇒ XWidget_grab 必走「同步重绘」
     * 路径（子控件场景），绘制面真的被派发，不依赖后备存储是否已存在。 */
    XWidget* host = XWidget_create(NULL, 0);
    XTableView* view;
    XAbstractItemModel* model = NULL;
    XQIdProbeDelegate probe;
    XImage* shot = NULL;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 200, 100);
    view = XTableView_create(host, 0);
    XQ_REQUIRE(view != NULL);
    XWidget_setGeometry((XWidget*)view, 0, 0, 160, 60);
    model = XAbstractItemModel_create();
    if (model) {
        XAbstractItemModel_setDimension(model, 2, 2);
        XAbstractItemModel_setData_2(model, 0, 0, "A");
        XAbstractItemModel_setData_2(model, 0, 1, "B");
        XAbstractItemModel_setData_2(model, 1, 0, "C");
        XAbstractItemModel_setData_2(model, 1, 1, "D");
        XAbstractItemView_setModel_base(&view->m_base, model);
    }

    xqId_probeReset();
    xqId_probeInit(&probe, true, false);
    g_idProbe.fillPixel = true;
    XAbstractItemView_setItemDelegate(&view->m_base, &probe.m_base);

    /* 视图绘制面必须解析到这个委托（行级/列级都未设 ⇒ 回落视图默认）。 */
    XQ_CHECK(XAbstractItemView_resolveDelegate(&view->m_base, 0, 0) ==
             &probe.m_base);
    XQ_CHECK(XItemDelegate_paintOverridden(
        XAbstractItemView_resolveDelegate(&view->m_base, 0, 0)));

    /* 第一轮：带接管委托渲染 → 委托 Paint 被调用。 */
    g_idProbe.lastText = NULL;
    shot = XWidget_grab((const XWidget*)view);
    XQ_CHECK(g_idProbe.paintCalls > 0);
    XQ_CHECK(g_idProbe.lastRect.width > 0);
    XQ_CHECK(g_idProbe.lastRect.height > 0);
    /* 选项确实带上了该格的显示文本（模型 → 视图 → 委托三段贯通）。
     * 模型 2x2 四格全为 A/B/C/D，均在可见区内 ⇒ 最后收到的文本必落在这
     * 4 个值上（不是空串，也不是视图凭空造的文本）。 */
    XQ_CHECK(g_idProbe.lastText != NULL);
    if (g_idProbe.lastText) {
        const char* t = g_idProbe.lastText;
        XQ_CHECK_MSG((t[0] == 'A' && t[1] == 0) ||
                     (t[0] == 'B' && t[1] == 0) ||
                     (t[0] == 'C' && t[1] == 0) ||
                     (t[0] == 'D' && t[1] == 0),
                     "委托收到的显示文本 = \"%s\"（应为 A/B/C/D 之一）", t);
    }
    if (shot) XClassDelete((XClass*)shot);

    /* 第二轮：换回缺省委托（无覆写）渲染 → Paint 不再被调用。 */
    XAbstractItemView_setItemDelegate(&view->m_base, NULL);
    {
        XItemDelegate* dflt = XAbstractItemView_resolveDelegate(&view->m_base,
                                                                0, 0);
        /* 解析结果必非 NULL（懒创建内建默认委托，对标 Qt 视图恒持有
         * 缺省 QStyledItemDelegate）且未覆写 Paint。 */
        XQ_CHECK_PTR(dflt);
        if (dflt) XQ_CHECK(!XItemDelegate_paintOverridden(dflt));
    }
    g_idProbe.paintCalls = 0;
    shot = XWidget_grab((const XWidget*)view);
    XQ_EQ_I64(g_idProbe.paintCalls, 0);
    if (shot) XClassDelete((XClass*)shot);

    XClassDeinit((XClass*)&probe);
    if (model) XClassDelete((XClass*)model);
    XClassDelete((XClass*)host);   /* 级联释放 view。 */
}

/* ==================== ⑦ P3-46：XTableWidget 绘制面查委托（叠加口径） ==================== */

/**
 * @brief XTableWidget 的绘制面解析并调用生效委托的 Paint，且**不**替换
 *        视图自绘——委托叠加在 item role 自绘之上（非完全接管；
 *        完全接管需先把 XTableWidgetItem role 桥接进 XStyleOptionViewItem，
 *        登记专项未落，见 XItemDelegate.h 文件头 @details）。
 *
 * 叠加像素取证：item(0,0) 设红背景（走视图自绘路径），委托只画条目矩形
 * **左半**黑（探针 fillPixel）⇒ 同一张 grab 图里左半 = 委托黑（委托执行）、
 * 右下 = 视图画的红背景（视图自绘未被委托替换）——两件事实并存即叠加
 * 口径；若哪天视图改成「覆写者接管」（跳过 item role 自绘），右下红背景
 * 断言会立刻变红。
 */
XQ_TEST(XItemDelegatePaint, tableWidgetPaintConsultsDelegate)
{
    /* 同 tableViewPaintConsultsDelegate：挂宿主下强制走同步重绘路径。 */
    XWidget* host = XWidget_create(NULL, 0);
    XTableWidget* view;
    XQIdProbeDelegate probe;
    XImage* shot = NULL;
    XTableWidgetItem* cell00 = NULL;
    uint32_t bgPixel = 0u;

    XQ_REQUIRE(host != NULL);
    XWidget_resize(host, 200, 100);
    view = XTableWidget_create(host, 0);
    XQ_REQUIRE(view != NULL);
    XWidget_setGeometry((XWidget*)view, 0, 0, 160, 60);
    XTableWidget_setColumnCount(view, 2);
    XTableWidget_setRowCount(view, 2);
    {
        XTableWidgetItem* it = XTableWidgetItem_create_utf8("格一", 0);
        if (it) {
            /* (0,0) 红背景：叠加取证里「视图自绘仍执行」的那一半证据。 */
            XColor bg = XColor_create_rgb(255, 0, 0, 255);
            XTableWidgetItem_setBackground(it, &bg);
            bgPixel = XColor_rgba(&bg);
            cell00 = it;
            XTableWidget_setItem(view, 0, 0, it);
        }
        it = XTableWidgetItem_create_utf8("格二", 0);
        if (it) XTableWidget_setItem(view, 0, 1, it);
    }

    xqId_probeReset();
    xqId_probeInit(&probe, true, false);
    g_idProbe.fillPixel = true;   /* 委托画条目矩形左半（叠加取证）。 */
    /* XTableWidget.m_base 是 XTableView，再取其 m_base 才是 XAbstractItemView。 */
    XAbstractItemView_setItemDelegate(&view->m_base.m_base, &probe.m_base);

    XQ_CHECK(XAbstractItemView_resolveDelegate(&view->m_base.m_base, 0, 0) ==
             &probe.m_base);

    shot = XWidget_grab((const XWidget*)view);
    /* 绘制面查委托：Paint 至少被调用一次，且行/列号落在视图内。 */
    XQ_CHECK(g_idProbe.paintCalls > 0);
    XQ_CHECK(g_idProbe.lastRow >= 0);
    XQ_CHECK(g_idProbe.lastCol >= 0);
    XQ_CHECK(g_idProbe.lastRect.width > 0);
    /* 叠加像素取证（见 doc）：(0,0) 格左半 = 委托黑；右下 = 视图红背景。
     * 取点在格底部 2px（避开文本区）、距格边 2px（避开网格线）。 */
    XQ_CHECK(shot != NULL);
    XQ_CHECK(g_idProbe.hasFirstRect);
    if (shot && g_idProbe.hasFirstRect && cell00) {
        XRect r = g_idProbe.firstRect;
        XQ_EQ_I64(XImage_pixel(shot, r.x + r.width / 4,
                               r.y + r.height - 2), (long long)0xFF000000u);
        XQ_EQ_I64(XImage_pixel(shot, r.x + r.width - 2,
                               r.y + r.height - 2), (long long)bgPixel);
    }
    if (shot) XClassDelete((XClass*)shot);

    /* 缺省委托回落：Paint 不再被调用（视图自绘仍照常）。 */
    XAbstractItemView_setItemDelegate(&view->m_base.m_base, NULL);
    g_idProbe.paintCalls = 0;
    shot = XWidget_grab((const XWidget*)view);
    XQ_EQ_I64(g_idProbe.paintCalls, 0);
    if (shot) XClassDelete((XClass*)shot);

    /* paintOverridden 闸（性能契约）：只覆写 SizeHint 的子类**不得**进入
     * paint_base ——否则每格都要白付一次 952 字节选项清零 + 712 字节调色板
     * 拷贝（AGENTS.md「帧耗时 0.0x~2ms」口径）。 */
    {
        XQIdProbeDelegate hintOnly;
        xqId_probeInit(&hintOnly, false, true);
        xqId_probeHintOnly(&hintOnly);
        XAbstractItemView_setItemDelegate(&view->m_base.m_base,
                                          &hintOnly.m_base);
        g_idProbe.paintCalls = 0;
        shot = XWidget_grab((const XWidget*)view);
        XQ_EQ_I64(g_idProbe.paintCalls, 0);
        if (shot) XClassDelete((XClass*)shot);
        XAbstractItemView_setItemDelegate(&view->m_base.m_base, NULL);
        XClassDeinit((XClass*)&hintOnly);
    }

    XClassDeinit((XClass*)&probe);
    XClassDelete((XClass*)host);   /* 级联释放 view。 */
}

/* ==================== ⑧ sizeHint 参与列宽测算（resizeColumnToContents） ==================== */

/**
 * @brief 自定义委托的 SizeHint 真参与 resizeColumnToContents 的列宽。
 *
 * 口径（XTableView.c 的 sizeHint 消费段）：
 * `if (sh.width > content) content = sh.width;` ⇒ 委托返回比文本更宽的
 * 尺寸时列宽必须变宽；返回 0 时列宽与「无委托」完全一致（零漂移）。
 */
XQ_TEST(XItemDelegatePaint, sizeHintDrivesColumnResize)
{
    XTableView* view = XTableView_create(NULL, 0);
    XAbstractItemModel* model = NULL;
    XQIdProbeDelegate probe;
    int widthNoDelegate;
    int widthWithHint;
    int widthZeroHint;

    XQ_REQUIRE(view != NULL);
    XWidget_setGeometry((XWidget*)view, 0, 0, 400, 80);
    model = XAbstractItemModel_create();
    XQ_REQUIRE(model != NULL);
    XAbstractItemModel_setDimension(model, 3, 1);
    XAbstractItemModel_setData_2(model, 0, 0, "x");
    XAbstractItemModel_setData_2(model, 1, 0, "y");
    XAbstractItemModel_setData_2(model, 2, 0, "z");
    XAbstractItemView_setModel_base(&view->m_base, model);

    /* 基线：无自定义委托，纯文本测算。 */
    XTableView_resizeColumnToContents(view, 0);
    widthNoDelegate = XTableView_columnWidth(view, 0);
    XQ_CHECK(widthNoDelegate > 0);

    /* 委托返回一个远大于文本的宽（137）⇒ 列宽必须被测到。 */
    xqId_probeReset();
    xqId_probeInit(&probe, false, true);
    xqId_probeHintOnly(&probe);
    g_idProbe.hintToReturn = (XSize){ 137, 11 };
    XAbstractItemView_setItemDelegate(&view->m_base, &probe.m_base);
    XTableView_resizeColumnToContents(view, 0);
    widthWithHint = XTableView_columnWidth(view, 0);
    /* 3 个可见行各问一次。 */
    XQ_EQ_I64(g_idProbe.sizeHintCalls, 3);
    XQ_CHECK(widthWithHint > widthNoDelegate);
    XQ_CHECK(widthWithHint >= 137);

    /* 委托返回 0×0 ⇒ 与无委托完全一致（缺省哨兵零漂移）。 */
    g_idProbe.hintToReturn = (XSize){ 0, 0 };
    g_idProbe.sizeHintCalls = 0;
    XTableView_resizeColumnToContents(view, 0);
    widthZeroHint = XTableView_columnWidth(view, 0);
    XQ_EQ_I64(g_idProbe.sizeHintCalls, 3);
    XQ_EQ_I64(widthZeroHint, widthNoDelegate);

    XClassDeinit((XClass*)&probe);
    XClassDelete((XClass*)model);
    XClassDelete((XClass*)view);
}

/* ==================== ⑨ sizeHint 参与行高测算（resizeRowToContents） ==================== */

/** @brief 自定义委托的 SizeHint 高度真参与 resizeRowToContents 的行高。 */
XQ_TEST(XItemDelegatePaint, sizeHintDrivesRowResize)
{
    XTableView* view = XTableView_create(NULL, 0);
    XAbstractItemModel* model = NULL;
    XQIdProbeDelegate probe;
    int heightNoDelegate;
    int heightWithHint;

    XQ_REQUIRE(view != NULL);
    XWidget_setGeometry((XWidget*)view, 0, 0, 300, 200);
    model = XAbstractItemModel_create();
    XQ_REQUIRE(model != NULL);
    XAbstractItemModel_setDimension(model, 2, 2);
    XAbstractItemModel_setData_2(model, 0, 0, "a");
    XAbstractItemModel_setData_2(model, 0, 1, "b");
    XAbstractItemView_setModel_base(&view->m_base, model);

    XTableView_resizeRowToContents(view, 0);
    heightNoDelegate = XTableView_rowHeight(view, 0);
    XQ_CHECK(heightNoDelegate > 0);

    xqId_probeReset();
    xqId_probeInit(&probe, false, true);
    xqId_probeHintOnly(&probe);
    g_idProbe.hintToReturn = (XSize){ 0, 91 };
    XAbstractItemView_setItemDelegate(&view->m_base, &probe.m_base);
    XTableView_resizeRowToContents(view, 0);
    heightWithHint = XTableView_rowHeight(view, 0);
    XQ_CHECK(g_idProbe.sizeHintCalls >= 1);
    XQ_CHECK(heightWithHint > heightNoDelegate);

    XClassDeinit((XClass*)&probe);
    XClassDelete((XClass*)model);
    XClassDelete((XClass*)view);
}

/* ==================== ⑩ 解析优先级：行级 → 列级 → 默认（对标 Qt itemDelegate(index)） ==================== */

/** @brief paint 面与编辑面共用同一解析入口（行级优先于列级、再回落默认）。 */
XQ_TEST(XItemDelegatePaint, resolveDelegatePrecedence)
{
    XTableView* view = XTableView_create(NULL, 0);
    XQIdProbeDelegate rowDelegate;
    XQIdProbeDelegate colDelegate;
    XQIdProbeDelegate defaultDelegate;

    XQ_REQUIRE(view != NULL);
    XWidget_setGeometry((XWidget*)view, 0, 0, 200, 80);
    xqId_probeReset();
    xqId_probeInit(&rowDelegate, true, false);
    xqId_probeInit(&colDelegate, false, true);
    xqId_probeInit(&defaultDelegate, false, true);

    XAbstractItemView_setItemDelegate(&view->m_base, &defaultDelegate.m_base);
    XAbstractItemView_setItemDelegateForColumn(&view->m_base, 1,
                                               &colDelegate.m_base);
    XAbstractItemView_setItemDelegateForRow(&view->m_base, 0,
                                            &rowDelegate.m_base);

    /* 行级命中（0,0）与（0,1）都先行级。 */
    XQ_CHECK(XAbstractItemView_resolveDelegate(&view->m_base, 0, 0) ==
             &rowDelegate.m_base);
    XQ_CHECK(XAbstractItemView_resolveDelegate(&view->m_base, 0, 1) ==
             &rowDelegate.m_base);
    /* 行级未设的第 1 行：列级优先于默认。 */
    XQ_CHECK(XAbstractItemView_resolveDelegate(&view->m_base, 1, 1) ==
             &colDelegate.m_base);
    /* 行列都没设：回落视图默认委托。 */
    XQ_CHECK(XAbstractItemView_resolveDelegate(&view->m_base, 1, 0) ==
             &defaultDelegate.m_base);
    /* NULL 视图安全。 */
    XQ_CHECK(XAbstractItemView_resolveDelegate(NULL, 0, 0) == NULL);

    XClassDeinit((XClass*)&rowDelegate);
    XClassDeinit((XClass*)&colDelegate);
    XClassDeinit((XClass*)&defaultDelegate);
    XClassDelete((XClass*)view);
}

#endif /* XGUI_ON && XWIDGET_ON && XTABLEWIDGET_ON */
