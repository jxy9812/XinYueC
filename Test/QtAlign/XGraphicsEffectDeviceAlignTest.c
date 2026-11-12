/**
 * @file       XGraphicsEffectDeviceAlignTest.c
 * @brief      XGraphicsEffect 设备坐标系取源 + 图形项效果链与 Qt 6.8 对齐契约测试。
 * @details    §B17/W4 effect 域验收（任务书九项 ①~⑨ + 复核补强四项 ⑩~⑬）：
 *             ⑩ 设备源位图**内容**采样（图像素 ↔ 项本地坐标）：自定义效果
 *                真正消费 `sourcePixmap_2(Device)` 时内容必须与项位置对齐
 *                （复核阻断项回归锁：少乘画布原点平移 ⇒ 内容整体平移项
 *                场景位置）。`deviceSourceContentPlacement` 对 dpr=1/2 的
 *                全部 56x46 / 112x92 像素逐一比对；`deviceSourceBoundsOriginShared`
 *                锁「补边与内容原点共用同一个 ctx->m_sourceBounds」——后者
 *                在修复前把内容画在 (0,0) 而非 sourceBounds.topLeft，
 *                正是复核 gdb 实测的 (10,20) 偏移形态；
 *             ⑪ 设备源**端到端回贴**：只画设备源位图的出图与逻辑源路径
 *                逐像素全等（dpr=1/2 两档，项 @(0,0) 与 @(10,20) 两种位置）；
 *             ⑫ 源重绘**继承外层 renderHints**（Qt qgraphicsitem.cpp:11114）；
 *             ⑬ 效果换宿主发 **SourceDetached**（Qt qgraphicseffect_p.h:
 *                121-134，项→项、控件→项两条路径），并覆盖 `setGraphicsEffect(NULL)`
 *                卸载与换装时旧效果同样收到 Detached（此前该路径静默断开）；
 *             ⑭ Device 源**补边不随视图缩放放大**（B10）：pad 在设备空间求
 *                （Qt `paddedEffectRect(Device, …)` 的实参已是
 *                `worldTransform.mapRect(rect)`，qgraphicsitem.cpp:11015-11018 /
 *                11054-11055）；XGui 用 ctx->m_devicePixelRatio 把折进
 *                deviceTransform 的 dpr 还原回"设备逻辑空间"再补边。
 *                `devicePadIndependentOfViewScale` 锁 viewScale=1/2 × dpr=1/2：
 *                外扩环恒为 pad×dpr（像素级全量扫描 + 采样点），
 *                viewScale=2 下不再变成 2×pad；
 *             ① `XGraphicsEffect_sourcePixmap_2(Logical)` 与既有
 *                `XGraphicsEffect_sourcePixmap(PadToEffectiveBoundingRect)`
 *                逐字段一致（回归锁：老 API 行为不变）；
 *             ② Device 路径在 dpr=1（恒等/纯平移设备变换）下与 Logical 结果
 *                对齐（补边、整数对齐、offset 全部相同）；
 *             ③ dpr=2 时 Device 包围盒与 offset **精确成 2 倍**（物理像素口径），
 *                且图像内容按 2 倍放大落位（像素级断言）；
 *             ④ 图形项挂效果后 `XGraphicsScene_render` 出图与无效果有像素差异，
 *                且效果类型正确：模糊**变糊**（边缘 alpha 下降 + 外扩环非零）、
 *                不透明度**变淡**（中心 alpha ≈ 128）；
 *             ⑤ 图形项析构删效果（存活计数归零；运行器 --leak-tolerance=0
 *                另有堆计数兜底）；
 *             ⑥ 重复 setGraphicsEffect 删旧（旧效果析构计数 +1），同一效果挂到
 *                第二个项时从原项摘下（原项槽位变 NULL）；
 *             ⑦ null / 空场景安全（NULL self、无绘制上下文、空场景 render、
 *                自 NULL 项挂效果不吞所有权）；
 *             ⑧ 既有 XGraphicsEffect / XGraphicsBlurEffect /
 *                XGraphicsOpacityEffect / XGraphicsDropShadowEffect /
 *                XGraphicsItem / XGraphicsScene / XGraphicsView 套件全绿（由
 *                `--test qt-align --filter=XGraphics` 整跑取证，非本文件断言）；
 *             ⑨ Qt 6.8.0 offscreen 探针对拍：`QGraphicsEffect` 挂在
 *                QGraphicsRectItem(0,0,40,30)@pos(10,20) 上，效果
 *                `boundingRectFor(r) = r.adjusted(-8,-8,8,8)`，dpr=1/2 实测：
 *        @code
 *          === dpr=1.0 ===
 *           item sceneBoundingRect=(10.0,20.0 40.0x30.0)
 *           draw Logical/PadEff  size=56x46  dpr=1.0 offset=(-8,-8)
 *           draw Device/PadEff   size=56x46  dpr=1.0 offset=(2,12)
 *           draw Device/NoPad    size=40x30  dpr=1.0 offset=(10,20)
 *           draw sourceBoundingRect log=(0.0,0.0 40.0x30.0) dev=(10.0,20.0 40.0x30.0)
 *          === dpr=2.0 ===
 *           draw Logical/PadEff  size=112x92 dpr=2.0 offset=(-8,-8)
 *           draw Device/PadEff   size=112x92 dpr=2.0 offset=(2,12)
 *           draw Device/NoPad    size=80x60  dpr=2.0 offset=(10,20)
 *           draw sourceBoundingRect log=(0.0,0.0 40.0x30.0) dev=(10.0,20.0 40.0x30.0)
 *        @endcode
 *        Qt 的 Device 取源以「逻辑设备坐标 + 位图 dpr 元数据」表达：offset 不随
 *        dpr 变、位图**物理**尺寸乘 dpr（qgraphicsitem.cpp:11110-11111
 *        `QPixmap pixmap(QRectF(effectRectF.topLeft(), effectRectF.size() * dpr)...)`）。XGui 把 dpr 折进 deviceTransform ⇒
 *        offset/尺寸直接是物理像素；两者经 `physical = logical × dpr` 一一对应。
 *        本用例按该换算逐项对拍（见 qtProbeParity 用例内的字面量断言）。
 *
 * @note       探针源码不在仓库内（`/tmp/w4probe/probe.cpp`，Qt 6.8.0
 *             `QT_QPA_PLATFORM=offscreen`）：它只导出数值，断言全部落在本文件。
 *             探针的 `QGraphicsRectItem` 用 `Qt::NoPen`，与 XGui 探针图元的
 *             纯 40x30 包围盒同参。
 * @author     XinYueC 团队
 */
#include "xq_align.h"

#include "XGraphicsEffect.h"
#include "XGraphicsBlurEffect.h"
#include "XGraphicsOpacityEffect.h"
#include "XGraphicsDropShadowEffect.h"
#include "XGraphicsItem.h"
#include "XGraphicsScene.h"
#include "XObject.h"
#include "XClass.h"
#include "XMemory.h"
#include "XPainter.h"
#include "XImage.h"
#include "XVector.h"
#include "XWidget.h"

/* ==================== 探针：效果（可配外扩量 + Draw 记录 + 存活计数） ==== */

/*
 * 自定义效果子类（与 XGraphicsEffectAlignTest 的 XGraphicsEffectProbe 同款
 * 写法）：BoundingRectFor 按固定 pad 外扩（对拍 Qt 探针的 adjusted(-8,...)），
 * Draw 里经 sourcePixmap_2 取两种坐标系的源并记录尺寸/offset，再把源原样
 * 绘入画布（等价基类缺省 Draw，保证场景出图可做像素断言）。
 */
XCLASS_DEFINE_BEGING(XGraphicsEffectDeviceProbe)
XCLASS_DEFINE_EXTEND_END(XGraphicsEffectDeviceProbe, XGraphicsEffect)

typedef struct XGraphicsEffectDeviceProbe
{
    XGraphicsEffect m_base;   /**< 基类；必须第一个。 */
    float m_pad;              /**< boundingRectFor 四周外扩量（像素）。 */
    int m_drawCount;          /**< Draw 派发次数。 */
    int m_logW;               /**< 最近一次 Logical 取源：宽。 */
    int m_logH;               /**< 最近一次 Logical 取源：高。 */
    int m_logX;               /**< 最近一次 Logical 取源：offset.x。 */
    int m_logY;               /**< 最近一次 Logical 取源：offset.y。 */
    int m_devW;               /**< 最近一次 Device 取源：宽（物理像素）。 */
    int m_devH;               /**< 最近一次 Device 取源：高。 */
    int m_devX;               /**< 最近一次 Device 取源：offset.x（物理像素）。 */
    int m_devY;               /**< 最近一次 Device 取源：offset.y。 */
    bool m_logValid;          /**< Logical 取源是否成功。 */
    bool m_devValid;          /**< Device 取源是否成功。 */
    int m_devSourceBoundsW;   /**< Device 取源时 sourceBoundingRect_2(Device) 宽。 */
    int m_devSourceBoundsH;   /**< 同上：高。 */
    int m_devSourceBoundsX;   /**< 同上：x（设备像素）。 */
    int m_devSourceBoundsY;   /**< 同上：y。 */
    int m_lastFlags;          /**< 最近一次 sourceChanged 的 flags。 */
    int m_detachedCount;      /**< SourceDetached 次数。 */
    int m_attachedCount;      /**< SourceAttached 次数。 */
    bool m_useDeviceOutput;   /**< true=Draw 真正消费 Device 源位图并回贴
                                   （否则整个源原样 drawSource）。 */
    /* ---- 全像素内容落位扫描（复核阻断项：设备源内容偏移 (10,20)。
     * 逐像素比对"图像素 → 项本地坐标 → 期望颜色"，任何整体平移/缩放
     * 错位都会让 m_scanBad 非零——比固定采样点更难被巧合掩盖。 ---- */
    bool m_scanContent;       /**< true=本次 Draw 对设备源位图做全像素扫描。 */
    int m_scanW;              /**< 期望内容（项本地）宽。 */
    int m_scanH;              /**< 期望内容（项本地）高。 */
    int m_scanPad;            /**< 期望补边（源本地单位；物理补边 = pad×dpr）。 */
    float m_scanScale;        /**< 图像素/项本地单位（= dpr × 视图缩放）。 */
    uint32_t m_scanColor;     /**< 内容期望像素（项内）。 */
    int m_scanChecked;        /**< 扫描过的像素数。 */
    int m_scanBad;            /**< 与期望不符的像素数。 */
    int m_scanFirstBadX;      /**< 首个不符像素 x（无则 -1）。 */
    int m_scanFirstBadY;      /**< 首个不符像素 y。 */
    float m_scanDpr;          /**< 扫描时的设备像素比（物理内容原点 = pad×dpr）。 */
} XGraphicsEffectDeviceProbe;

static int s_effectAlive = 0; /**< 探针效果存活数（析构计数）。 */
static int s_itemAlive = 0;   /**< 探针图元存活数。 */
static int s_detachEvents = 0; /**< 全进程 SourceDetached 事件数（效果可能
                                    在发出后立即被删，故用独立计数取证）。 */

/* 设备源位图的固定采样点（由用例在渲染前设定；探针 Draw 内按设备图像素
 * 坐标取样，越界记 0xDEADBEEF 以便用例分辨"未采到"与"透明"）。 */
static XPoint s_devSamplePts[6];
static int s_devSampleCount = 0;
static uint32_t s_devSamplePx[6];

/** @brief 设定 Device 源位图采样点（NULL/0 清空）。 */
static void xq_devSetSamples(const XPoint* pts, int count)
{
    int i;
    if (!pts || count < 0) count = 0;
    if (count > 6) count = 6;
    for (i = 0; i < count; ++i) s_devSamplePts[i] = pts[i];
    s_devSampleCount = count;
    for (i = 0; i < 6; ++i) s_devSamplePx[i] = 0xDEADBEEFu;
}

static XRectF xq_devProbeBoundingRectFor(const XGraphicsEffect* base,
                                         const XRectF* sourceRect)
{
    const XGraphicsEffectDeviceProbe* probe =
        (const XGraphicsEffectDeviceProbe*)base;
    XRectF out;
    if (!sourceRect) {
        out.x = 0.0f;
        out.y = 0.0f;
        out.width = 0.0f;
        out.height = 0.0f;
        return out;
    }
    /* 对标 Qt 探针 `r.adjusted(-8,-8,8,8)`（外扩量由 m_pad 配）。 */
    out.x = sourceRect->x - probe->m_pad;
    out.y = sourceRect->y - probe->m_pad;
    out.width = sourceRect->width + 2.0f * probe->m_pad;
    out.height = sourceRect->height + 2.0f * probe->m_pad;
    return out;
}

static void xq_devProbeDraw(XGraphicsEffect* base,
                            XGraphicsEffectDrawContext* ctx)
{
    XGraphicsEffectDeviceProbe* probe = (XGraphicsEffectDeviceProbe*)base;
    XPainter painter;
    XImage* dev;
    XPoint devOffset;
    if (!probe || !ctx) return;
    ++probe->m_drawCount;
    devOffset.x = 0;
    devOffset.y = 0;
    dev = XGraphicsEffect_sourcePixmap_2(
        base, NULL, &devOffset, XGraphicsEffectCoordinateSystem_Device);
    if (dev) {
        int i;
        XRectF devBounds;
        probe->m_devValid = true;
        probe->m_devW = XImage_width(dev);
        probe->m_devH = XImage_height(dev);
        probe->m_devX = devOffset.x;
        probe->m_devY = devOffset.y;
        /* 逐点取样：内容错位（少乘画布原点平移）会在这里暴露。 */
        for (i = 0; i < s_devSampleCount; ++i) {
            int sx = s_devSamplePts[i].x;
            int sy = s_devSamplePts[i].y;
            if (sx < 0 || sy < 0 || sx >= probe->m_devW ||
                sy >= probe->m_devH) {
                s_devSamplePx[i] = 0xDEADBEEFu;
                continue;
            }
            s_devSamplePx[i] = (uint32_t)XImage_pixel(dev, sx, sy);
        }
        /* 同一 Draw 上下文里再取设备系源包围盒（sourceBoundingRect_2）。 */
        devBounds = XGraphicsEffect_sourceBoundingRect_2(
            base, NULL, XGraphicsEffectCoordinateSystem_Device);
        probe->m_devSourceBoundsX = (int)devBounds.x;
        probe->m_devSourceBoundsY = (int)devBounds.y;
        probe->m_devSourceBoundsW = (int)devBounds.width;
        probe->m_devSourceBoundsH = (int)devBounds.height;
        /* 全像素内容落位扫描：图像素中心 → 项本地坐标
         *   local = (q + 0.5 − pad×dpr) / (dpr×视图缩放)
         * 落在 [0,w)×[0,h) 即应为内容色，否则应透明。若源内容整体平移
         * 了项的场景位置（复核阻断项 (10,20)），边界处会出现成批不符。
         * 物理补边 = pad×dpr（dpr 放大位图，视图缩放**不**放大 pad）。 */
        if (probe->m_scanContent) {
            float scale = (probe->m_scanScale > 0.0f)
                              ? probe->m_scanScale : 1.0f;
            float dpr = (probe->m_scanDpr > 0.0f) ? probe->m_scanDpr : 1.0f;
            float origin = (float)probe->m_scanPad * dpr;
            int qx;
            int qy;
            probe->m_scanChecked = 0;
            probe->m_scanBad = 0;
            probe->m_scanFirstBadX = -1;
            probe->m_scanFirstBadY = -1;
            for (qy = 0; qy < probe->m_devH; ++qy) {
                for (qx = 0; qx < probe->m_devW; ++qx) {
                    float lx = (((float)qx + 0.5f) - origin) / scale;
                    float ly = (((float)qy + 0.5f) - origin) / scale;
                    bool inside = (lx >= 0.0f &&
                                   lx < (float)probe->m_scanW &&
                                   ly >= 0.0f &&
                                   ly < (float)probe->m_scanH);
                    uint32_t want = inside ? probe->m_scanColor : 0u;
                    uint32_t got = (uint32_t)XImage_pixel(dev, qx, qy);
                    ++probe->m_scanChecked;
                    if (got != want) {
                        if (probe->m_scanBad == 0) {
                            probe->m_scanFirstBadX = qx;
                            probe->m_scanFirstBadY = qy;
                        }
                        ++probe->m_scanBad;
                    }
                }
            }
        }
    }
    {
        XPoint offset;
        XImage* log = XGraphicsEffect_sourcePixmap_2(
            base, NULL, &offset, XGraphicsEffectCoordinateSystem_Logical);
        if (log) {
            probe->m_logValid = true;
            probe->m_logW = XImage_width(log);
            probe->m_logH = XImage_height(log);
            probe->m_logX = offset.x;
            probe->m_logY = offset.y;
            XClassDelete(log);
        }
    }
    XPainter_init(&painter, NULL);
    if (XPainter_begin_image(&painter, ctx->m_dest)) {
        if (probe->m_useDeviceOutput && dev) {
            /* 真正**消费**设备源位图（自定义效果的 Device 取源回贴形态，
             * 对标 QGraphicsColorizeEffect/DropShadow 的
             * `painter->setWorldTransform(QTransform())` + drawPixmap）：
             * 设备像素 → 画布坐标用 ctx->m_deviceTransform 的逆，再按
             * 设备 offset 落位（画布像素原点 = destRect.topLeft，与本画布
             * 的用户坐标一致）。 */
            XImageTransform inv;
            if (XGraphicsItem_matrixInvert(&ctx->m_deviceTransform, &inv)) {
                XPainter_setWorldTransform(&painter, &inv, false);
                XPainter_drawImage(&painter, dev, devOffset.x, devOffset.y);
            } else {
                XGraphicsEffect_drawSource(base, &painter);
            }
        } else {
            /* 缺省：画布输出 = 源原样（与基类缺省 Draw 同语义）。 */
            XGraphicsEffect_drawSource(base, &painter);
        }
        XPainter_end(&painter);
    }
    XPainter_deinit(&painter);
    if (dev) XClassDelete(dev);
}

static void xq_devProbeDeinit(XGraphicsEffect* base)
{
    --s_effectAlive;
    /* 回落父类 Deinit：摘源槽位（控件/图形项）+ XObject 清尾。 */
    XClass_Deinit_Parent(XGraphicsEffect, base);
}

/** @brief SourceChanged 槽：记账 flags（自定义效果按 SourceDetached 清缓存，
 *         故换宿主时必须能观测到它——对标 Qt qgraphicseffect_p.h:119-134）。 */
static void xq_devProbeSourceChanged(XGraphicsEffect* base, int flags)
{
    XGraphicsEffectDeviceProbe* probe = (XGraphicsEffectDeviceProbe*)base;
    if (!probe) return;
    probe->m_lastFlags = flags;
    if (flags & XGraphicsEffectChange_SourceDetached) {
        ++probe->m_detachedCount;
        ++s_detachEvents;
    }
    if (flags & XGraphicsEffectChange_SourceAttached) ++probe->m_attachedCount;
    /* 刻意不转 update()：本探针不依赖脏区（场景渲染是全量重绘）。 */
}

static XVtable* XGraphicsEffectDeviceProbe_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsEffectDeviceProbe)
    XVTABLE_INHERIT_XCLASS(XGraphicsEffect);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xq_devProbeDeinit);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_BoundingRectFor,
                             xq_devProbeBoundingRectFor);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_Draw, xq_devProbeDraw);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsEffect_SourceChanged,
                             xq_devProbeSourceChanged);
    return XVTABLE_DEFAULT;
}

/** @brief 建探针效果（pad 像素外扩）；所有权随后交挂接方。 */
static XGraphicsEffectDeviceProbe* xq_devProbeCreate(float pad)
{
    XGraphicsEffectDeviceProbe* probe =
        (XGraphicsEffectDeviceProbe*)XMemory_malloc(sizeof(*probe),
                                                    XCLASS_DEFAULT_MEMORY_TYPE);
    if (!probe) return NULL;
    XGraphicsEffect_init(&probe->m_base);
    XClassSetVtable(probe, XGraphicsEffectDeviceProbe);
    Set_Class_Memory(probe, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(probe, true);
    probe->m_pad = pad;
    probe->m_drawCount = 0;
    probe->m_logValid = false;
    probe->m_devValid = false;
    probe->m_lastFlags = 0;
    probe->m_detachedCount = 0;
    probe->m_attachedCount = 0;
    probe->m_devSourceBoundsW = 0;
    probe->m_devSourceBoundsH = 0;
    probe->m_devSourceBoundsX = 0;
    probe->m_devSourceBoundsY = 0;
    probe->m_useDeviceOutput = false;
    probe->m_scanContent = false;
    probe->m_scanW = 0;
    probe->m_scanH = 0;
    probe->m_scanPad = 0;
    probe->m_scanScale = 1.0f;
    probe->m_scanDpr = 1.0f;
    probe->m_scanColor = 0xFFFF0000u;
    probe->m_scanChecked = 0;
    probe->m_scanBad = 0;
    probe->m_scanFirstBadX = -1;
    probe->m_scanFirstBadY = -1;
    ++s_effectAlive;
    return probe;
}

/* ==================== 探针：图元（纯色矩形 + 存活计数） ==================== */

XCLASS_DEFINE_BEGING(XGraphicsEffectDeviceRectItem)
XCLASS_DEFINE_EXTEND_END(XGraphicsEffectDeviceRectItem, XGraphicsItem)

typedef struct XGraphicsEffectDeviceRectItem
{
    XGraphicsItem m_base; /**< 基类；必须第一个。 */
    float m_w;            /**< 包围盒宽 (0,0,m_w,m_h)。 */
    float m_h;            /**< 包围盒高。 */
    uint32_t m_brush;     /**< Paint 填色（0xAARRGGBB）。 */
    int m_paintCount;     /**< Paint 派发次数（"子项不得被画两遍"判据）。 */
    uint32_t m_lastHints; /**< 最近一次 Paint 看到的渲染提示（XPainterRenderHints；
                               用于断言源重绘继承外层 hint）。 */
} XGraphicsEffectDeviceRectItem;

static XRectF xq_devItemBoundingRect(const XGraphicsItem* base)
{
    const XGraphicsEffectDeviceRectItem* item =
        (const XGraphicsEffectDeviceRectItem*)base;
    XRectF out;
    out.x = 0.0f;
    out.y = 0.0f;
    out.width = item ? item->m_w : 0.0f;
    out.height = item ? item->m_h : 0.0f;
    return out;
}

static void xq_devItemPaint(XGraphicsItem* base, XPainter* painter)
{
    XGraphicsEffectDeviceRectItem* item = (XGraphicsEffectDeviceRectItem*)base;
    XRect r;
    if (!item || !painter) return;
    ++item->m_paintCount;
    item->m_lastHints = (uint32_t)XPainter_renderHints(painter);
    r.x = 0;
    r.y = 0;
    r.width = (int)item->m_w;
    r.height = (int)item->m_h;
    XPainter_fillRect(painter, &r, item->m_brush);
}

static void xq_devItemDeinit(XGraphicsItem* base)
{
    --s_itemAlive;
    /* 回落父类 Deinit：级联删子 + 场景注销 + **删自己拥有的效果** + 父类清尾。 */
    XClass_Deinit_Parent(XGraphicsItem, base);
}

static XVtable* XGraphicsEffectDeviceRectItem_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGraphicsEffectDeviceRectItem)
    XVTABLE_INHERIT_XCLASS(XGraphicsItem);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, xq_devItemDeinit);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_BoundingRect,
                             xq_devItemBoundingRect);
    XVTABLE_OVERLOAD_DEFAULT(EXGraphicsItem_Paint, xq_devItemPaint);
    return XVTABLE_DEFAULT;
}

/** @brief 建探针图元（堆对象，位置由调用方 setPos）。 */
static XGraphicsEffectDeviceRectItem* xq_devItemCreate(float w, float h,
                                                       uint32_t brush)
{
    XGraphicsEffectDeviceRectItem* item =
        (XGraphicsEffectDeviceRectItem*)XMemory_malloc(sizeof(*item),
                                                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!item) return NULL;
    XGraphicsItem_init(&item->m_base);
    XClassSetVtable(item, XGraphicsEffectDeviceRectItem);
    Set_Class_Memory(item, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(item, true);
    item->m_w = w;
    item->m_h = h;
    item->m_brush = brush;
    item->m_paintCount = 0;
    item->m_lastHints = 0;
    ++s_itemAlive;
    return item;
}

/* ==================== 渲染/像素助手 ==================== */

/** @brief 渲染场景到新建图像（目标矩形 (0,0,w,h)；dpr>1 时物理放大并挂 dpr；
 *         viewScale≠1 时在绘制器世界变换上叠加视图缩放——等价
 *         QGraphicsView::scale，deviceTransform 因此含视图缩放）。 */
static XImage* xq_devRenderSceneScaled(XGraphicsScene* scene, int w, int h,
                                       float dpr, float viewScale)
{
    XImage* img;
    XPainter painter;
    XRectF target;
    int pw;
    int ph;
    if (!scene || w <= 0 || h <= 0) return NULL;
    if (!(dpr > 0.0f)) dpr = 1.0f;
    if (!(viewScale > 0.0f)) viewScale = 1.0f;
    pw = (int)((float)w * dpr + 0.5f);
    ph = (int)((float)h * dpr + 0.5f);
    if (pw <= 0 || ph <= 0) return NULL;
    img = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!img) return NULL;
    if (!XImage_reinit_ex(img, pw, ph, XImageFormat_ARGB32_Premultiplied)) {
        XClassDelete(img);
        return NULL;
    }
    XImage_setDevicePixelRatio(img, dpr);
    XImage_fillRect(img, NULL, 0u);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, img)) {
        XPainter_deinit(&painter);
        XClassDelete(img);
        return NULL;
    }
    if (viewScale != 1.0f)
        XPainter_scale(&painter, viewScale, viewScale);
    target.x = 0.0f;
    target.y = 0.0f;
    target.width = (float)w;
    target.height = (float)h;
    XGraphicsScene_render(scene, &painter, &target);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    return img;
}

/** @brief 渲染场景到新建图像（无视图缩放，viewScale=1 的常用形态）。 */
static XImage* xq_devRenderScene(XGraphicsScene* scene, int w, int h, float dpr)
{
    return xq_devRenderSceneScaled(scene, w, h, dpr, 1.0f);
}

/** @brief 同上，但先给绘制器置渲染提示（hints=0 表示不置；用于断言源重绘
 *         继承外层 hint）。 */
static XImage* xq_devRenderSceneHints(XGraphicsScene* scene, int w, int h,
                                      float dpr, XPainterRenderHints hints)
{
    XImage* img;
    XPainter painter;
    XRectF target;
    int pw;
    int ph;
    if (!scene || w <= 0 || h <= 0) return NULL;
    if (!(dpr > 0.0f)) dpr = 1.0f;
    pw = (int)((float)w * dpr + 0.5f);
    ph = (int)((float)h * dpr + 0.5f);
    if (pw <= 0 || ph <= 0) return NULL;
    img = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!img) return NULL;
    if (!XImage_reinit_ex(img, pw, ph, XImageFormat_ARGB32_Premultiplied)) {
        XClassDelete(img);
        return NULL;
    }
    XImage_setDevicePixelRatio(img, dpr);
    XImage_fillRect(img, NULL, 0u);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, img)) {
        XPainter_deinit(&painter);
        XClassDelete(img);
        return NULL;
    }
    if (hints != 0u)
        XPainter_setRenderHints(&painter, hints, true);
    target.x = 0.0f;
    target.y = 0.0f;
    target.width = (float)w;
    target.height = (float)h;
    XGraphicsScene_render(scene, &painter, &target);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    return img;
}

/** @brief 像素 alpha（0xAARRGGBB 高 8 位；与既有效果用例同口径）。 */
static uint32_t xq_devAlphaAt(const XImage* img, int x, int y)
{
    if (!img) return 0u;
    return ((uint32_t)XImage_pixel(img, x, y)) >> 24;
}

/** @brief 组合「先平移、后缩放」的设备变换（= painter 世界变换 ∘ dpr）。 */
static void xq_devDeviceTransform(XImageTransform* out, float tx, float ty,
                                  float dpr)
{
    XImageTransform translate;
    XImageTransform scale;
    XGraphicsItem_matrixFromTranslate(&translate, tx, ty);
    XGraphicsItem_matrixIdentity(&scale);
    scale.m11 = dpr;
    scale.m22 = dpr;
    /* 对乘法口径：(t*s).map(p) = s.map(t.map(p))——先平移、再乘 dpr
     * （与 Qt QPainterPrivate::updateMatrix 的 world × hidpiScale 同序）。 */
    XGraphicsItem_matrixMultiply(out, &translate, &scale);
}

/* ==================== ① ② ③：sourcePixmap_2 双坐标系 ==================== */

/**
 * @brief sourcePixmap_2：Logical 回归、dpr=1 对齐、dpr=2 精确成倍。
 * @details 用控件源（40x30）驱动，上下文为"Draw 期间"的合成形态
 *          （m_drawCtx 手工挂接，与 XGraphicsEffectAlignTest 的
 *          sourcePixmapPadModes 同款手法），探针效果外扩 8：
 *            Logical            → 56x46 / offset(-8,-8)
 *            Device(恒等)        → 56x46 / offset(-8,-8)   （dpr=1 与 Logical 同）
 *            Device(平移 10,20) → 56x46 / offset(2,12)
 *            Device(平移+dpr=2) → 112x92 / offset(4,24)   （精确 2 倍）
 *          像素：dpr=2 图像把源放大 2 倍落位（源 (0,0) 红 → 图像 (16,16) 红）。
 */
XQ_TEST(XGraphicsEffectDevice, sourcePixmapCoordinates)
{
    XWidget* widget = XWidget_create(NULL, 0);
    XGraphicsEffectDeviceProbe* probe = xq_devProbeCreate(8.0f);
    XImage* snapshot = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XImage* dest = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XGraphicsEffectDrawContext ctx;
    XImageTransform dt;
    XPoint off;
    XImage* got;
    XImage* ref;

    XQ_REQUIRE(widget != NULL && probe != NULL && snapshot != NULL &&
               dest != NULL);
    /* 枚举值与 Qt 6.8 qnamespace.h:1602-1605 逐项一致：
     *   enum CoordinateSystem { DeviceCoordinates, LogicalCoordinates }; */
    XQ_EQ_I64((long long)XGraphicsEffectCoordinateSystem_Device, 0);
    XQ_EQ_I64((long long)XGraphicsEffectCoordinateSystem_Logical, 1);

    XWidget_resize(widget, 40, 30);
    XQ_REQUIRE(XImage_reinit_ex(snapshot, 40, 30,
                                XImageFormat_ARGB32_Premultiplied));
    XQ_REQUIRE(XImage_reinit_ex(dest, 40, 30,
                                XImageFormat_ARGB32_Premultiplied));
    XImage_fillRect(snapshot, NULL, 0xFFFF0000u); /* 全幅不透明红 */
    XImage_fillRect(dest, NULL, 0u);
    /* 效果挂到控件（所有权转移给 widget；末尾随 widget 析构删）。 */
    XWidget_setGraphicsEffect(widget, (XGraphicsEffect*)probe);

    /* 无绘制上下文：Device 路径必须 NULL（对标 Qt 无 info 上下文告警返回空）。 */
    off.x = 7;
    off.y = 9;
    XQ_CHECK(XGraphicsEffect_sourcePixmap_2(
                 (XGraphicsEffect*)probe, NULL, &off,
                 XGraphicsEffectCoordinateSystem_Device) == NULL);
    XQ_CHECK(off.x == 0 && off.y == 0);
    /* 同一口径的包围盒入口：无上下文 ⇒ 空矩形（qgraphicsitem.cpp:11002-11008）。 */
    {
        XRectF brDev = XGraphicsEffect_sourceBoundingRect_2(
            (XGraphicsEffect*)probe, NULL,
            XGraphicsEffectCoordinateSystem_Device);
        XQ_CHECK(brDev.width == 0.0f && brDev.height == 0.0f);
    }
    /* NULL 效果：NULL（offset 归零）。 */
    off.x = 3;
    XQ_CHECK(XGraphicsEffect_sourcePixmap_2(
                 NULL, NULL, &off, XGraphicsEffectCoordinateSystem_Logical) == NULL);
    XQ_CHECK(off.x == 0 && off.y == 0);

    /* 合成 Draw 期上下文（新字段经 init 清零，避免栈垃圾）。 */
    XGraphicsEffectDrawContext_init(&ctx);
    ctx.m_source = snapshot;
    ctx.m_sourceRect.x = 0;
    ctx.m_sourceRect.y = 0;
    ctx.m_sourceRect.width = 40;
    ctx.m_sourceRect.height = 30;
    ctx.m_dest = dest;
    ctx.m_destRect.x = 0;
    ctx.m_destRect.y = 0;
    ctx.m_destRect.width = 40;
    ctx.m_destRect.height = 30;
    ((XGraphicsEffect*)probe)->m_drawCtx = &ctx;

    /* ① Logical 与既有 sourcePixmap 逐字段一致（回归锁）。 */
    ref = XGraphicsEffect_sourcePixmap(
        (XGraphicsEffect*)probe, &off, XGraphicsEffectPadMode_PadToEffectiveBoundingRect);
    XQ_REQUIRE(ref != NULL);
    XQ_EQ_I64(XImage_width(ref), 56);
    XQ_EQ_I64(XImage_height(ref), 46);
    XQ_EQ_I64(off.x, -8);
    XQ_EQ_I64(off.y, -8);
    {
        XPoint off2;
        got = XGraphicsEffect_sourcePixmap_2(
            (XGraphicsEffect*)probe, NULL, &off2,
            XGraphicsEffectCoordinateSystem_Logical);
        XQ_REQUIRE(got != NULL);
        XQ_EQ_I64(XImage_width(got), XImage_width(ref));
        XQ_EQ_I64(XImage_height(got), XImage_height(ref));
        XQ_EQ_I64(off2.x, off.x);
        XQ_EQ_I64(off2.y, off.y);
        XClassDelete(got);
    }
    XClassDelete(ref);

    /* ①-b CoordinateSystem 参数：Logical（显式）与既有 sourceBoundingRect
     *     同值；非 Device 值一律按 Logical（Qt 缺省形参口径）。 */
    {
        XRectF brLog = XGraphicsEffect_sourceBoundingRect(
            (XGraphicsEffect*)probe);
        XRectF brExplicit = XGraphicsEffect_sourceBoundingRect_2(
            (XGraphicsEffect*)probe, NULL,
            XGraphicsEffectCoordinateSystem_Logical);
        XRectF brInvalid = XGraphicsEffect_sourceBoundingRect_2(
            (XGraphicsEffect*)probe, NULL, 99);
        XQ_EQ_I64((int)brLog.width, 40);
        XQ_EQ_I64((int)brLog.height, 30);
        XQ_CHECK(brExplicit.x == brLog.x && brExplicit.y == brLog.y &&
                 brExplicit.width == brLog.width &&
                 brExplicit.height == brLog.height);
        XQ_CHECK(brInvalid.x == brLog.x && brInvalid.y == brLog.y &&
                 brInvalid.width == brLog.width &&
                 brInvalid.height == brLog.height);
    }

    /* ② dpr=1：Device(恒等) 与 Logical 完全对齐；
     *    Device(平移) 只把 offset 平移，尺寸不变。 */
    {
        XPoint off2;
        got = XGraphicsEffect_sourcePixmap_2(
            (XGraphicsEffect*)probe, NULL, &off2,
            XGraphicsEffectCoordinateSystem_Device);
        XQ_REQUIRE(got != NULL);
        XQ_EQ_I64(XImage_width(got), 56);
        XQ_EQ_I64(XImage_height(got), 46);
        XQ_EQ_I64(off2.x, -8);
        XQ_EQ_I64(off2.y, -8);
        XClassDelete(got);

        xq_devDeviceTransform(&dt, 10.0f, 20.0f, 1.0f);
        got = XGraphicsEffect_sourcePixmap_2(
            (XGraphicsEffect*)probe, &dt, &off2,
            XGraphicsEffectCoordinateSystem_Device);
        XQ_REQUIRE(got != NULL);
        XQ_EQ_I64(XImage_width(got), 56);
        XQ_EQ_I64(XImage_height(got), 46);
        XQ_EQ_I64(off2.x, 2);  /* Qt 6.8.0 探针 dpr=1：Device/PadEff offset=(2,12) */
        XQ_EQ_I64(off2.y, 12);
        /* 源内容随平移落位：源 (0,0) → 设备 (10,20) → 图像 (10-2, 20-12)。 */
        XQ_CHECK(XImage_pixel(got, 8, 8) == 0xFFFF0000u);
        XClassDelete(got);
    }

    /* ③ dpr=2：包围盒与 offset 精确 2 倍，内容按 2 倍放大。 */
    {
        XPoint off2;
        XImage* dev1;
        xq_devDeviceTransform(&dt, 10.0f, 20.0f, 1.0f);
        dev1 = XGraphicsEffect_sourcePixmap_2(
            (XGraphicsEffect*)probe, &dt, &off2,
            XGraphicsEffectCoordinateSystem_Device);
        XQ_REQUIRE(dev1 != NULL);
        {
            int w1 = XImage_width(dev1);
            int h1 = XImage_height(dev1);
            int x1 = off2.x;
            int y1 = off2.y;
            xq_devDeviceTransform(&dt, 10.0f, 20.0f, 2.0f);
            /* deviceTransform 折了 dpr ⇒ 上下文必须声明同一个 dpr：
             * sourcePixmap_2 先 ÷dpr 回设备逻辑空间补边、再 ×dpr 回物理
             * （Qt qgraphicsitem.cpp:11109-11111 同口径）。 */
            ctx.m_devicePixelRatio = 2.0f;
            got = XGraphicsEffect_sourcePixmap_2(
                (XGraphicsEffect*)probe, &dt, &off2,
                XGraphicsEffectCoordinateSystem_Device);
            ctx.m_devicePixelRatio = 1.0f;
            XQ_REQUIRE(got != NULL);
            XQ_EQ_I64(XImage_width(got), w1 * 2);
            XQ_EQ_I64(XImage_height(got), h1 * 2);
            XQ_EQ_I64(off2.x, x1 * 2);
            XQ_EQ_I64(off2.y, y1 * 2);
            /* 源 (0,0) → 设备 (2*0+20, 2*0+40) = (20,40) → 图像 (20-4, 40-24)。 */
            XQ_CHECK(XImage_pixel(got, 16, 16) == 0xFFFF0000u);
            XQ_CHECK(XImage_pixel(got, 17, 17) == 0xFFFF0000u);
            /* 图像右上角超出源范围的内容必须仍是透明（无越界读/错位填充）。 */
            XQ_CHECK(xq_devAlphaAt(got, XImage_width(got) - 1, 0) == 0u);
            XClassDelete(got);
        }
        XClassDelete(dev1);
    }

    /* Device 上下文自动取 ctx.m_deviceTransform（deviceTransform 传 NULL）。 */
    {
        XPoint off2;
        xq_devDeviceTransform(&dt, 10.0f, 20.0f, 2.0f);
        ctx.m_deviceTransform = dt;
        ctx.m_hasDeviceTransform = true;
        ctx.m_devicePixelRatio = 2.0f;
        got = XGraphicsEffect_sourcePixmap_2(
            (XGraphicsEffect*)probe, NULL, &off2,
            XGraphicsEffectCoordinateSystem_Device);
        XQ_REQUIRE(got != NULL);
        XQ_EQ_I64(off2.x, 4);
        XQ_EQ_I64(off2.y, 24);
        XClassDelete(got);
        ctx.m_hasDeviceTransform = false;
        ctx.m_devicePixelRatio = 1.0f;
    }

    ((XGraphicsEffect*)probe)->m_drawCtx = NULL;
    XClassDelete((XClass*)widget); /* 级联删效果（s_effectAlive 归零） */
    XQ_CHECK(s_effectAlive == 0);
    XClassDelete((XClass*)snapshot);
    XClassDelete((XClass*)dest);
}

/* ==================== ④ ⑤ ⑥ ⑦：图形项效果链 ==================== */

/**
 * @brief 图形项挂效果：scene render 出图（模糊变糊 / 不透明度变淡）。
 * @details 项 = 60x50 纯红 @pos(20,20)，渲染目标 120x120：
 *          - 无效果：项内 (50,45) 全不透明红、项外 (10,10) 透明；
 *          - 模糊(3)：外扩环 (14,14) alpha>0、项边缘 (20,20) alpha<255、
 *            中心 (50,45) alpha==255（Qt `QGraphicsBlurEffect` 的
 *            source→filter→draw 语义）；
 *          - 不透明度(0.5)：中心 alpha 落在 100..160（≈128），且明显小于
 *            无效果的 255；
 *          - ⑤ 项析构删效果（用探针效果计数取证）。
 */
XQ_TEST(XGraphicsEffectDevice, itemEffectRender)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* item = xq_devItemCreate(60.0f, 50.0f,
                                                          0xFFFF0000u);
    XPointF pos;
    XImage* base;
    XImage* shot;
    uint32_t baseAlpha;

    XQ_REQUIRE(scene != NULL && item != NULL);
    pos.x = 20.0f;
    pos.y = 20.0f;
    XGraphicsItem_setPos(&item->m_base, &pos);
    XGraphicsScene_addItem(scene, &item->m_base);

    /* 基线（无效果）。 */
    base = xq_devRenderScene(scene, 120, 120, 1.0f);
    XQ_REQUIRE(base != NULL);
    XQ_CHECK(XImage_pixel(base, 50, 45) == 0xFFFF0000u);
    XQ_EQ_I64(baseAlpha = xq_devAlphaAt(base, 50, 45), 255);
    XQ_EQ_I64(xq_devAlphaAt(base, 10, 10), 0);

    /* ④-a 模糊：挂上 XGraphicsBlurEffect(6) 后逐点断言。 */
    {
        XGraphicsBlurEffect* blur = XGraphicsBlurEffect_create();
        int ringHits = 0;
        int x;
        XQ_REQUIRE(blur != NULL);
        XGraphicsBlurEffect_setBlurRadius(blur, 6.0f);
        XQ_CHECK(XGraphicsItem_graphicsEffect_const(&item->m_base) == NULL);
        XGraphicsItem_setGraphicsEffect(&item->m_base,
                                        (XGraphicsEffect*)blur);
        XQ_CHECK(XGraphicsItem_graphicsEffect_const(&item->m_base) ==
                 (XGraphicsEffect*)blur);
        XQ_CHECK(XGraphicsEffect_isEnabled((XGraphicsEffect*)blur));
        shot = xq_devRenderScene(scene, 120, 120, 1.0f);
        XQ_REQUIRE(shot != NULL);
        /* 外扩环：项左边缘（x=20，项纵向中点 y=45）之外的 10px 带内必须
         * 出现"非零但不透明"的衰减像素（基线该带全 0）。 */
        XQ_CHECK_MSG(xq_devAlphaAt(shot, 19, 45) > 0u,
                     "模糊外扩环应溢到项外 1px（boundingRectFor pad=1+2.5r）");
        XQ_CHECK_MSG(xq_devAlphaAt(shot, 19, 45) < 255u,
                     "外扩环必须是衰减的，不能是不透明块");
        for (x = 10; x < 20; ++x) {
            uint32_t a = xq_devAlphaAt(shot, x, 45);
            if (a > 0u && a < 255u) ++ringHits;
        }
        XQ_CHECK_MSG(ringHits > 0, "项外 10px 带内应有半透明模糊像素");
        /* 边缘变糊：原本 alpha=255 的左上角像素被摊开。 */
        XQ_CHECK_MSG(xq_devAlphaAt(shot, 20, 20) < 255u,
                     "模糊后项边缘 alpha 应下降");
        XQ_CHECK_MSG(xq_devAlphaAt(shot, 50, 45) >= 250u,
                     "均匀色块中心经指数模糊仍应基本不透明（实得 %u）",
                     xq_devAlphaAt(shot, 50, 45));
        /* 与基线逐点不同（效果确实参与了合成）。 */
        XQ_CHECK(XImage_pixel(shot, 19, 45) != XImage_pixel(base, 19, 45));
        XClassDelete((XClass*)shot);
        /* 卸载（删效果）。 */
        XGraphicsItem_setGraphicsEffect(&item->m_base, NULL);
        XQ_CHECK(XGraphicsItem_graphicsEffect_const(&item->m_base) == NULL);
    }

    /* ④-b 不透明度 0.5：中心变淡。 */
    {
        XGraphicsOpacityEffect* opacity = XGraphicsOpacityEffect_create();
        uint32_t alpha;
        XQ_REQUIRE(opacity != NULL);
        XGraphicsOpacityEffect_setOpacity(opacity, 0.5f);
        XGraphicsItem_setGraphicsEffect(&item->m_base,
                                        (XGraphicsEffect*)opacity);
        shot = xq_devRenderScene(scene, 120, 120, 1.0f);
        XQ_REQUIRE(shot != NULL);
        alpha = xq_devAlphaAt(shot, 50, 45);
        XQ_CHECK_MSG(alpha > 100u && alpha < 160u,
                     "opacity=0.5 中心 alpha 应 ≈128（实得 %u）", alpha);
        XQ_CHECK_MSG(alpha < baseAlpha, "不透明度效果必须比基线更淡");
        XClassDelete((XClass*)shot);
    }

    /* ④-c 投影：外扩环出现在 offset 侧（第三条既有效果同链验证；投影的
     * 绘制实现直接按 destRect 索引源位图，能过说明源画布原点对齐正确）。 */
    {
        XGraphicsDropShadowEffect* shadow =
            XGraphicsDropShadowEffect_create();
        XPointF shadowOffset;
        XQ_REQUIRE(shadow != NULL);
        shadowOffset.x = 10.0f;
        shadowOffset.y = 10.0f;
        XGraphicsDropShadowEffect_setBlurRadius(shadow, 4.0f);
        XGraphicsDropShadowEffect_setOffset(shadow, shadowOffset);
        XGraphicsItem_setGraphicsEffect(&item->m_base,
                                        (XGraphicsEffect*)shadow);
        shot = xq_devRenderScene(scene, 120, 120, 1.0f);
        XQ_REQUIRE(shot != NULL);
        /* 项右下角外侧（+x/+y 偏移方向）应出现投影衰减像素；左上侧不应有
         * 明显外扩（offset 指右下）。 */
        XQ_CHECK_MSG(xq_devAlphaAt(shot, 82, 72) > 0u,
                     "投影应在 +offset 侧溢出（实得 %u）",
                     xq_devAlphaAt(shot, 82, 72));
        XQ_CHECK(xq_devAlphaAt(shot, 82, 72) < 255u);
        XQ_EQ_I64(xq_devAlphaAt(shot, 12, 12), 0);
        XClassDelete((XClass*)shot);
    }

    /* ⑤ 项析构删效果：换成可计数的探针效果，删项后存活计数归零。 */
    {
        XGraphicsEffectDeviceProbe* probe = xq_devProbeCreate(4.0f);
        XQ_REQUIRE(probe != NULL);
        XGraphicsItem_setGraphicsEffect(&item->m_base, (XGraphicsEffect*)probe);
        XQ_CHECK(s_effectAlive == 1);
        XGraphicsScene_removeItem(scene, &item->m_base); /* 所有权回调用方 */
        XClassDelete(&item->m_base);
        XQ_CHECK_MSG(s_effectAlive == 0, "图形项析构必须删除自己拥有的效果");
        XQ_CHECK(s_itemAlive == 0);
    }

    XClassDelete((XClass*)base);
    XGraphicsScene_clear(scene);
    XClassDelete((XClass*)scene);
}

/**
 * @brief ⑥ 重复挂接删旧 + 同一效果被第二项夺走；⑦ null/空场景安全。
 */
XQ_TEST(XGraphicsEffectDevice, effectOwnershipAndNullSafety)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* a = xq_devItemCreate(20.0f, 20.0f,
                                                        0xFF00FF00u);
    XGraphicsEffectDeviceRectItem* b = xq_devItemCreate(20.0f, 20.0f,
                                                        0xFF0000FFu);
    XGraphicsEffectDeviceProbe* p1 = xq_devProbeCreate(4.0f);
    XGraphicsEffectDeviceProbe* p2;
    XGraphicsEffectDrawContext ctx;
    XPoint off;

    XQ_REQUIRE(scene != NULL && a != NULL && b != NULL && p1 != NULL);
    XGraphicsScene_addItem(scene, &a->m_base);
    XGraphicsScene_addItem(scene, &b->m_base);

    /* ⑦ self==NULL：不接管、不释放（调用方仍持有）。 */
    XGraphicsItem_setGraphicsEffect(NULL, (XGraphicsEffect*)p1);
    XQ_CHECK(s_effectAlive == 1);
    XQ_CHECK(XGraphicsEffect_itemSource((XGraphicsEffect*)p1) == NULL);

    /* ⑥-a 重复 setGraphicsEffect：旧效果被删。 */
    XGraphicsItem_setGraphicsEffect(&a->m_base, (XGraphicsEffect*)p1);
    XQ_CHECK(XGraphicsItem_graphicsEffect_const(&a->m_base) ==
             (XGraphicsEffect*)p1);
    XQ_CHECK(XGraphicsEffect_itemSource((XGraphicsEffect*)p1) ==
             &a->m_base);
    p2 = xq_devProbeCreate(4.0f);
    XQ_REQUIRE(p2 != NULL);
    XQ_CHECK(s_effectAlive == 2);
    XGraphicsItem_setGraphicsEffect(&a->m_base, (XGraphicsEffect*)p2);
    XQ_CHECK_MSG(s_effectAlive == 1, "重复 setGraphicsEffect 必须删除旧效果");
    XQ_CHECK(XGraphicsItem_graphicsEffect_const(&a->m_base) ==
             (XGraphicsEffect*)p2);

    /* ⑥-b 同一效果挂到第二个项：从原项摘下（原项槽位 NULL）。 */
    XGraphicsItem_setGraphicsEffect(&b->m_base, (XGraphicsEffect*)p2);
    XQ_CHECK_MSG(XGraphicsItem_graphicsEffect_const(&a->m_base) == NULL,
                 "效果被第二项夺走时原项槽位必须摘空");
    XQ_CHECK(XGraphicsItem_graphicsEffect_const(&b->m_base) ==
             (XGraphicsEffect*)p2);
    XQ_CHECK(XGraphicsEffect_itemSource((XGraphicsEffect*)p2) == &b->m_base);

    /* ⑥-c 同值早退：重复挂同一效果不删。 */
    XGraphicsItem_setGraphicsEffect(&b->m_base, (XGraphicsEffect*)p2);
    XQ_CHECK(s_effectAlive == 1);

    /* ⑦ 空场景 render：不崩、图像保持全透明。 */
    {
        XGraphicsScene* empty = XGraphicsScene_create();
        XImage* img;
        XQ_REQUIRE(empty != NULL);
        img = xq_devRenderScene(empty, 20, 20, 1.0f);
        XQ_REQUIRE(img != NULL);
        XQ_EQ_I64(xq_devAlphaAt(img, 5, 5), 0);
        XClassDelete((XClass*)img);
        XGraphicsScene_render(NULL, NULL, NULL); /* 空参安全 */
        XGraphicsScene_clear(NULL);
        XClassDelete((XClass*)empty);
    }

    /* ⑦ 直接删除效果：图形项槽位被摘空（对标 Qt ~QGraphicsEffect）。 */
    {
        XGraphicsEffect* raw = XGraphicsItem_graphicsEffect_const(&b->m_base);
        XQ_REQUIRE(raw != NULL);
        XClassDelete(raw);
        XQ_CHECK_MSG(XGraphicsItem_graphicsEffect_const(&b->m_base) == NULL,
                     "直接删效果后项槽位必须摘空（防悬垂）");
        XQ_CHECK(s_effectAlive == 0);
    }

    /* ⑦ 空/无上下文取源：全部 NULL 且不崩。
     *    取源对象必须是**存活**的效果：p1 已在 ⑥-a 被重复 setGraphicsEffect
     *    按 Qt 语义删除（qgraphicsitem.cpp:2905-2908），p2 已在上一段直删
     *    —— 复用 p1/p2 即 use-after-free（ASan 实测 heap-use-after-free
     *    @ XGraphicsEffect.c:819）。 */
    {
        XGraphicsEffectDeviceProbe* probe = xq_devProbeCreate(0.0f);
        XQ_REQUIRE(probe != NULL);
        XGraphicsEffectDrawContext_init(&ctx);
        XQ_CHECK(XGraphicsEffect_sourcePixmap_2(
                     (XGraphicsEffect*)probe, NULL, &off,
                     XGraphicsEffectCoordinateSystem_Device) == NULL);
        XQ_CHECK(XGraphicsEffect_sourcePixmap_2(
                     (XGraphicsEffect*)probe, NULL, &off,
                     XGraphicsEffectCoordinateSystem_Logical) == NULL);
        XQ_CHECK(XGraphicsEffect_sourcePixmap_2(
                     (XGraphicsEffect*)probe, NULL, NULL, 99) == NULL);
        XClassDelete((XClass*)probe);
    }

    XGraphicsScene_clear(scene); /* 删 a、b（均无效果） */
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_itemAlive == 0);
    XQ_CHECK(s_effectAlive == 0);
}

/**
 * @brief 自定义效果**真正消费** Device 源位图时，出图必须与逻辑源一致。
 * @details 复核阻断项的端到端闭口：此前探针只记录设备源位图的尺寸/offset
 *          后丢弃，逻辑源出图正常也掩盖了内容错位。本用例让探针 Draw 走
 *          「设备源 → 回贴画布」的真实消费形态（对标 QGraphicsColorizeEffect/
 *          QGraphicsDropShadowEffect 的 deviceCoordinates 分支：worldTransform
 *          换成设备逆变换后 drawPixmap），再与同一场景的「逻辑源」出图逐像素
 *          对比：pad=0、项 40x30 @pos(0,0) ⇒ 设备源位图 = 项框本身，
 *          dpr=1/2 两档的整幅出图都应与逻辑源路径**全等**。
 *          内容一旦错位（少乘画布原点平移），整幅图会与逻辑源路径不同。
 */
XQ_TEST(XGraphicsEffectDevice, deviceSourceCompositedEndToEnd)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* item =
        xq_devItemCreate(40.0f, 30.0f, 0xFF00FF00u);
    XGraphicsEffectDeviceProbe* probe = xq_devProbeCreate(0.0f);
    XPointF pos;
    XImage* logical;
    XImage* device;
    int x;
    int y;
    int diff;

    XQ_REQUIRE(scene != NULL && item != NULL && probe != NULL);
    pos.x = 0.0f;
    pos.y = 0.0f;
    XGraphicsItem_setPos(&item->m_base, &pos);
    XGraphicsScene_addItem(scene, &item->m_base);
    XGraphicsItem_setGraphicsEffect(&item->m_base, (XGraphicsEffect*)probe);

    /* 基线：逻辑源（drawSource）。 */
    logical = xq_devRenderScene(scene, 60, 50, 1.0f);
    XQ_REQUIRE(logical != NULL);
    XQ_EQ_I64(XImage_pixel(logical, 20, 15), 0xFF00FF00u);
    XQ_EQ_I64(xq_devAlphaAt(logical, 50, 45), 0);

    /* dpr=1：设备源消费路径逐像素与逻辑源全等。 */
    probe->m_useDeviceOutput = true;
    device = xq_devRenderScene(scene, 60, 50, 1.0f);
    XQ_REQUIRE(device != NULL);
    diff = 0;
    for (y = 0; y < 50; ++y)
        for (x = 0; x < 60; ++x)
            if (XImage_pixel(device, x, y) != XImage_pixel(logical, x, y))
                ++diff;
    XQ_CHECK_MSG(diff == 0,
                 "dpr=1 设备源出图应与逻辑源全等（差异像素 %d）", diff);
    XClassDelete((XClass*)device);
    XClassDelete((XClass*)logical);

    /* dpr=2：同一断言（物理分辨率翻倍，回贴按 dpr 放大）。 */
    logical = xq_devRenderScene(scene, 60, 50, 2.0f);
    XQ_REQUIRE(logical != NULL);
    device = xq_devRenderScene(scene, 60, 50, 2.0f);
    XQ_REQUIRE(device != NULL);
    diff = 0;
    for (y = 0; y < 100; ++y)
        for (x = 0; x < 120; ++x)
            if (XImage_pixel(device, x, y) != XImage_pixel(logical, x, y))
                ++diff;
    XQ_CHECK_MSG(diff == 0,
                 "dpr=2 设备源出图应与逻辑源全等（差异像素 %d）", diff);
    /* 项内容位置正确（物理 (40,30) = 逻辑 (20,15)）。 */
    XQ_CHECK(XImage_pixel(device, 40, 30) == 0xFF00FF00u);
    XQ_EQ_I64(xq_devAlphaAt(device, 110, 90), 0);
    XClassDelete((XClass*)device);
    XClassDelete((XClass*)logical);

    /* ---- 非零项位置 (10,20)：设备源消费路径的落位不得被项场景位置平移 ----
     * 复核阻断项 (10,20) 的端到端形态：之前本用例把项摆在 (0,0)，项位置
     * 有关的错位被掩盖。现在同一断言在 (10,20) 重跑，dpr=1/2 两档与
     * 逻辑源路径逐像素全等；源内容若整体多平移一次 (10,20)，差异立刻非零。 */
    pos.x = 10.0f;
    pos.y = 20.0f;
    XGraphicsItem_setPos(&item->m_base, &pos);
    {
        logical = xq_devRenderScene(scene, 80, 70, 1.0f);
        XQ_REQUIRE(logical != NULL);
        device = xq_devRenderScene(scene, 80, 70, 1.0f);
        XQ_REQUIRE(device != NULL);
        diff = 0;
        for (y = 0; y < 70; ++y)
            for (x = 0; x < 80; ++x)
                if (XImage_pixel(device, x, y) != XImage_pixel(logical, x, y))
                    ++diff;
        XQ_CHECK_MSG(diff == 0,
                     "dpr=1 项 @(10,20) 设备源出图应与逻辑源全等（差异 %d）",
                     diff);
        /* 项内容落位：项内 (30,35) 绿、项外 (5,5) 透明。 */
        XQ_EQ_I64(XImage_pixel(device, 30, 35), 0xFF00FF00u);
        XQ_EQ_I64(xq_devAlphaAt(device, 5, 5), 0);
        XClassDelete((XClass*)device);
        XClassDelete((XClass*)logical);

        logical = xq_devRenderScene(scene, 80, 70, 2.0f);
        XQ_REQUIRE(logical != NULL);
        device = xq_devRenderScene(scene, 80, 70, 2.0f);
        XQ_REQUIRE(device != NULL);
        diff = 0;
        for (y = 0; y < 140; ++y)
            for (x = 0; x < 160; ++x)
                if (XImage_pixel(device, x, y) != XImage_pixel(logical, x, y))
                    ++diff;
        XQ_CHECK_MSG(diff == 0,
                     "dpr=2 项 @(10,20) 设备源出图应与逻辑源全等（差异 %d）",
                     diff);
        /* 项内容物理落位：逻辑 (30,35) ⇒ 物理 (60,70)；逻辑 (5,5) 透明。 */
        XQ_EQ_I64(XImage_pixel(device, 60, 70), 0xFF00FF00u);
        XQ_EQ_I64(xq_devAlphaAt(device, 10, 10), 0);
        XClassDelete((XClass*)device);
        XClassDelete((XClass*)logical);
    }

    XGraphicsScene_clear(scene);
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_itemAlive == 0);
    XQ_CHECK(s_effectAlive == 0);
}

/**
 * @brief 源重绘必须继承外层渲染提示（对标 Qt qgraphicsitem.cpp:11114）。
 * @details Qt 在建源 pixmap 时 `pixmapPainter.setRenderHints(info ?
 *          info->painter->renderHints() : QPainter::TextAntialiasing)`：视图
 *          开 Antialiasing/SmoothPixmapTransform 时，挂效果项的源内容必须用
 *          同一套 hint 渲染，否则与无效果路径出图不一致（边缘锯齿/点采样）。
 *          本用例用图元 Paint 里记录的 hint 取证：
 *            无效果 + AA 开 → 图元看到 AA 位；
 *            有效果 + AA 开 → 源重绘（离屏）里图元仍看到 AA 位（**继承**）；
 *            有效果 + AA 关 → 源重绘里看不到 AA 位（不凭空加 hint）。
 */
XQ_TEST(XGraphicsEffectDevice, renderHintsInheritedIntoSource)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* item =
        xq_devItemCreate(40.0f, 30.0f, 0xFFFF0000u);
    XGraphicsOpacityEffect* opacity;
    XPointF pos;
    XImage* shot;

    XQ_REQUIRE(scene != NULL && item != NULL);
    pos.x = 10.0f;
    pos.y = 10.0f;
    XGraphicsItem_setPos(&item->m_base, &pos);
    XGraphicsScene_addItem(scene, &item->m_base);

    /* 无效果 + AA：图元看到 AA 位。 */
    item->m_lastHints = 0;
    shot = xq_devRenderSceneHints(scene, 80, 60, 1.0f,
                                  XPainterRenderHint_Antialiasing);
    XQ_REQUIRE(shot != NULL);
    XQ_CHECK_MSG((item->m_lastHints & XPainterRenderHint_Antialiasing) != 0u,
                 "无效果路径应继承外层 AA（实得 0x%X）", item->m_lastHints);
    XClassDelete((XClass*)shot);

    /* 有效果 + AA：源重绘必须继承 AA（修复前这里只有缺省 TextAntialiasing）。 */
    opacity = XGraphicsOpacityEffect_create();
    XQ_REQUIRE(opacity != NULL);
    XGraphicsOpacityEffect_setOpacity(opacity, 0.8f);
    XGraphicsItem_setGraphicsEffect(&item->m_base, (XGraphicsEffect*)opacity);
    item->m_lastHints = 0;
    shot = xq_devRenderSceneHints(scene, 80, 60, 1.0f,
                                  XPainterRenderHint_Antialiasing);
    XQ_REQUIRE(shot != NULL);
    XQ_CHECK_MSG((item->m_lastHints & XPainterRenderHint_Antialiasing) != 0u,
                 "效果源重绘必须继承外层 AA（实得 0x%X）", item->m_lastHints);
    XClassDelete((XClass*)shot);

    /* 有效果 + 不给 AA：源重绘不得凭空加 AA。 */
    item->m_lastHints = 0;
    shot = xq_devRenderSceneHints(scene, 80, 60, 1.0f, 0u);
    XQ_REQUIRE(shot != NULL);
    XQ_CHECK_MSG((item->m_lastHints & XPainterRenderHint_Antialiasing) == 0u,
                 "外层未开 AA 时源重绘不应加 AA（实得 0x%X）", item->m_lastHints);
    XClassDelete((XClass*)shot);

    XGraphicsScene_clear(scene);
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_itemAlive == 0);
    XQ_CHECK(s_effectAlive == 0);
}

/**
 * @brief 效果换宿主必须发 SourceDetached（对标 Qt qgraphicseffect_p.h:119-134）。
 * @details Qt 的 QGraphicsEffectPrivate::setGraphicsEffectSource 在**同一次**
 *          sourceChanged 里带 SourceDetached|SourceAttached：
 *          `if (source) { flags |= SourceDetached; … detach(); delete source; }
 *           source = newSource; if (newSource) flags |= SourceAttached;
 *           q_func()->sourceChanged(flags);`
 *          仓库控件路径同样显式发 Detached（Src/XGui/Widget/XWidget.c:14779-14806）。
 *          自定义效果按 SourceDetached 清缓存（XGraphicsEffectAlignTest 已锁
 *          控件路径），故图形项从别的项/控件夺走效果时**不能**只发 Attached。
 *          本用例覆盖两条夺源路径（项→项、控件→项）。
 */
XQ_TEST(XGraphicsEffectDevice, sourceChangedOnRetarget)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* a = xq_devItemCreate(20.0f, 20.0f,
                                                        0xFFFFFFFFu);
    XGraphicsEffectDeviceRectItem* b = xq_devItemCreate(20.0f, 20.0f,
                                                        0xFFFFFFFFu);
    XGraphicsEffectDeviceProbe* probe = xq_devProbeCreate(2.0f);
    XWidget* widget;

    XQ_REQUIRE(scene != NULL && a != NULL && b != NULL && probe != NULL);
    XGraphicsScene_addItem(scene, &a->m_base);
    XGraphicsScene_addItem(scene, &b->m_base);

    /* 首次挂接：只有 Attached。 */
    XGraphicsItem_setGraphicsEffect(&a->m_base, (XGraphicsEffect*)probe);
    XQ_EQ_I64(probe->m_attachedCount, 1);
    XQ_EQ_I64(probe->m_detachedCount, 0);

    /* 项 → 项：Detached + Attached 各一次。 */
    XGraphicsItem_setGraphicsEffect(&b->m_base, (XGraphicsEffect*)probe);
    XQ_CHECK_MSG(probe->m_detachedCount == 1,
                 "效果被另一项夺走必须发 SourceDetached（实得 %d 次）",
                 probe->m_detachedCount);
    XQ_EQ_I64(probe->m_attachedCount, 2);
    XQ_CHECK_MSG((probe->m_lastFlags & XGraphicsEffectChange_SourceAttached) != 0,
                 "最后一次通知应含 SourceAttached（flags=0x%X）",
                 probe->m_lastFlags);

    /* 控件 → 项：同样必须发 Detached，且控件槽位摘空。 */
    widget = XWidget_create(NULL, 0);
    XQ_REQUIRE(widget != NULL);
    XWidget_resize(widget, 20, 20);
    XWidget_setGraphicsEffect(widget, (XGraphicsEffect*)probe);
    XQ_EQ_I64(probe->m_attachedCount, 3);
    XQ_EQ_I64(probe->m_detachedCount, 2); /* 项 b 被夺走 ⇒ +1 */
    XQ_CHECK(XWidget_graphicsEffect(widget) != NULL);
    XGraphicsItem_setGraphicsEffect(&a->m_base, (XGraphicsEffect*)probe);
    XQ_CHECK_MSG(probe->m_detachedCount == 3,
                 "效果被图形项从控件夺走必须发 SourceDetached（实得 %d 次）",
                 probe->m_detachedCount);
    XQ_EQ_I64(probe->m_attachedCount, 4);
    XQ_CHECK_MSG(XWidget_graphicsEffect(widget) == NULL,
                 "被夺走后控件槽位必须摘空");
    XQ_CHECK(XGraphicsItem_graphicsEffect_const(&b->m_base) == NULL);

    /* ⑬「被摘」也必须发 SourceDetached（复核补强 2026-11-17）：
     * setGraphicsEffect(item, NULL) 卸载 → 旧效果收到 Detached 后删除。
     * Qt 由 ~QGraphicsEffect → setGraphicsEffectSource(nullptr) 发
     * （qgraphicseffect.cpp:379-383）；修复前本路径静默断开（复核缺发点）。
     * 卸载即删（qgraphicsitem.cpp:2905-2908 `delete d_ptr->graphicsEffect`）
     * ⇒ 此后 probe 已释放，m_detachedCount 读不得（ASan 实测本处
     * heap-use-after-free）。改用全进程计数 s_detachEvents 的增量取证
     * （理由同文件头 135 行独立计数）。 */
    {
        int d0 = s_detachEvents;
        XGraphicsItem_setGraphicsEffect(&a->m_base, NULL);
        XQ_CHECK_MSG(s_detachEvents == d0 + 1,
                     "图形项卸载效果必须发 SourceDetached（+%d 次）",
                     s_detachEvents - d0);
    }
    XQ_CHECK(XGraphicsItem_graphicsEffect_const(&a->m_base) == NULL);
    XQ_CHECK(s_effectAlive == 0);

    /* 换装同一槽位同样按「摘旧」发 Detached：挂 p1 → 用 p2 替换时 p1 的
     * SourceChanged 槽必须先收到 Detached（p1 随即被删，故用全局计数取证）。 */
    {
        XGraphicsEffectDeviceProbe* p1 = xq_devProbeCreate(2.0f);
        XGraphicsEffectDeviceProbe* p2 = xq_devProbeCreate(2.0f);
        int d0;
        XQ_REQUIRE(p1 != NULL && p2 != NULL);
        XQ_EQ_I64(p1->m_detachedCount, 0);
        XGraphicsItem_setGraphicsEffect(&a->m_base, (XGraphicsEffect*)p1);
        XQ_EQ_I64(p1->m_attachedCount, 1);
        d0 = s_detachEvents;
        XGraphicsItem_setGraphicsEffect(&a->m_base, (XGraphicsEffect*)p2);
        XQ_CHECK_MSG(s_detachEvents == d0 + 1,
                     "换装必须让旧效果收到 SourceDetached（+%d 次）",
                     s_detachEvents - d0);
        XQ_CHECK_MSG(s_effectAlive == 1,
                     "换装后旧效果必须被删除（存活 %d）", s_effectAlive);
        XQ_CHECK(XGraphicsItem_graphicsEffect_const(&a->m_base) ==
                 (XGraphicsEffect*)p2);
        {
            int d1 = s_detachEvents;
            XGraphicsItem_setGraphicsEffect(&a->m_base, NULL);
            XQ_CHECK_MSG(s_detachEvents == d1 + 1,
                         "卸载 p2 必须再发一次 SourceDetached（+%d 次）",
                         s_detachEvents - d1);
        }
        XQ_CHECK(s_effectAlive == 0);
    }

    /* 收尾：直接删效果 ⇒ 项槽摘空；控件无效果可删。 */
    {
        XGraphicsEffectDeviceProbe* p3 = xq_devProbeCreate(2.0f);
        XQ_REQUIRE(p3 != NULL);
        XGraphicsItem_setGraphicsEffect(&a->m_base, (XGraphicsEffect*)p3);
        XClassDelete((XClass*)XGraphicsItem_graphicsEffect_const(&a->m_base));
        XQ_CHECK(XGraphicsItem_graphicsEffect_const(&a->m_base) == NULL);
        XQ_CHECK(s_effectAlive == 0);
    }
    XClassDelete((XClass*)widget);
    XGraphicsScene_clear(scene);
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_itemAlive == 0);
}

/* ==================== ⑨ Qt 6.8.0 探针对拍 ==================== */
/**
 * @brief 与 Qt 6.8.0 offscreen 探针的 boundingRectFor / offset 逐项对拍。
 * @details 同参几何：项 40x30 @pos(10,20)、效果外扩 8、取源 pad 模式 =
 *          PadToEffectiveBoundingRect（Qt 函数缺省）。Qt 实测值见文件头
 *          @code 块。换算规则（见文件头 @note）：
 *            Qt Logical 尺寸/offset      ≡ X Logical 尺寸/offset
 *            Qt Device  **物理**尺寸      ≡ X Device 尺寸
 *            Qt Device  **物理** offset   ≡ X Device offset
 *          （Qt 的 offset 是逻辑坐标、位图物理尺寸 = 逻辑 × dpr
 *            ⇒ 物理 offset = Qt offset × dpr。）
 */
XQ_TEST(XGraphicsEffectDevice, qtProbeParity)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* item = xq_devItemCreate(40.0f, 30.0f,
                                                           0xFFFF0000u);
    XGraphicsEffectDeviceProbe* probe = xq_devProbeCreate(8.0f);
    XPointF pos;
    XImage* shot;

    XQ_REQUIRE(scene != NULL && item != NULL && probe != NULL);
    pos.x = 10.0f;
    pos.y = 20.0f;
    XGraphicsItem_setPos(&item->m_base, &pos);
    XGraphicsScene_addItem(scene, &item->m_base);
    XGraphicsItem_setGraphicsEffect(&item->m_base, (XGraphicsEffect*)probe);

    /* ---- dpr=1 ---- */
    /* Device 源位图内容采样点（图像素坐标 → 期望）：
     *  offset=(2,12) ⇒ 图像素 q ↔ 设备 (2+q.x, 12+q.y) ↔ 项本地 (device−(10,20))：
     *   (8,8)   → 项本地 (0,0)   → 项内红（**内容错位时这里会是 0**）
     *   (10,10) → 项本地 (2,2)   → 项内红
     *   (47,37) → 项本地 (39,29) → 项内红（右下内侧）
     *   (55,45) → 项本地 (47,37) → 项外透明
     *   (0,0)   → 项本地 (-8,-8) → 补边透明 */
    {
        XPoint pts[5];
        pts[0].x = 8;  pts[0].y = 8;
        pts[1].x = 10; pts[1].y = 10;
        pts[2].x = 47; pts[2].y = 37;
        pts[3].x = 55; pts[3].y = 45;
        pts[4].x = 0;  pts[4].y = 0;
        xq_devSetSamples(pts, 5);
    }
    shot = xq_devRenderScene(scene, 120, 120, 1.0f);
    XQ_REQUIRE(shot != NULL);
    XQ_CHECK_MSG(probe->m_drawCount > 0, "效果 Draw 必须由场景渲染派发");
    /* Qt dpr=1：Logical/PadEff size=56x46 offset=(-8,-8)。 */
    XQ_CHECK(probe->m_logValid);
    XQ_EQ_I64(probe->m_logW, 56);
    XQ_EQ_I64(probe->m_logH, 46);
    XQ_EQ_I64(probe->m_logX, -8);
    XQ_EQ_I64(probe->m_logY, -8);
    /* Qt dpr=1：Device/PadEff size=56x46 offset=(2,12)（逻辑=物理）。 */
    XQ_CHECK(probe->m_devValid);
    XQ_EQ_I64(probe->m_devW, 56);
    XQ_EQ_I64(probe->m_devH, 46);
    XQ_EQ_I64(probe->m_devX, 2);
    XQ_EQ_I64(probe->m_devY, 12);
    /* 源位图**内容**必须与设备的项位置对齐（复核阻断项回归锁）。 */
    XQ_CHECK_MSG(s_devSamplePx[0] == 0xFFFF0000u,
                 "Device 源位图 (8,8) 应为项内容（实得 0x%08X）",
                 s_devSamplePx[0]);
    XQ_CHECK_MSG(s_devSamplePx[1] == 0xFFFF0000u,
                 "Device 源位图 (10,10) 应为项内容（实得 0x%08X）",
                 s_devSamplePx[1]);
    XQ_EQ_I64(s_devSamplePx[2], 0xFFFF0000u);
    XQ_EQ_I64(s_devSamplePx[3], 0u);
    XQ_EQ_I64(s_devSamplePx[4], 0u);
    /* 设备系源包围盒：项本地 (0,0,40,30) 经设备变换 ⇒ (10,20,40,30)
     * （Qt 探针 dpr=1 实测 dev=(10.0,20.0 40.0x30.0)）。 */
    XQ_EQ_I64(probe->m_devSourceBoundsX, 10);
    XQ_EQ_I64(probe->m_devSourceBoundsY, 20);
    XQ_EQ_I64(probe->m_devSourceBoundsW, 40);
    XQ_EQ_I64(probe->m_devSourceBoundsH, 30);
    XClassDelete((XClass*)shot);

    /* ---- dpr=2 ---- */
    /* 采样点（图像素）：offset=(4,24)、dpr=2 ⇒ 图像素 q ↔ 项本地
     * ((4+q)−(20,40))/2：
     *   (16,16) → 项本地 (0,0)     → 红
     *   (17,17) → 项本地 (0.5,0.5) → 红
     *   (95,75) → 项本地 (39.5,29.5) → 红
     *   (111,91)→ 项本地 (47.5,37.5) → 透明
     *   (0,0)   → 项本地 (-8,-8)   → 透明 */
    {
        XPoint pts[5];
        pts[0].x = 16;  pts[0].y = 16;
        pts[1].x = 17;  pts[1].y = 17;
        pts[2].x = 95;  pts[2].y = 75;
        pts[3].x = 111; pts[3].y = 91;
        pts[4].x = 0;   pts[4].y = 0;
        xq_devSetSamples(pts, 5);
    }
    shot = xq_devRenderScene(scene, 120, 120, 2.0f);
    XQ_REQUIRE(shot != NULL);
    /* Qt dpr=2：Logical size=112x92（dpr=2.0 ⇒ 逻辑 56x46）、offset=(-8,-8)。 */
    XQ_EQ_I64(probe->m_logW, 56);
    XQ_EQ_I64(probe->m_logH, 46);
    XQ_EQ_I64(probe->m_logX, -8);
    XQ_EQ_I64(probe->m_logY, -8);
    /* Qt dpr=2：Device 物理 size=112x92、逻辑 offset=(2,12) ⇒ 物理 (4,24)。 */
    XQ_EQ_I64(probe->m_devW, 112);
    XQ_EQ_I64(probe->m_devH, 92);
    XQ_EQ_I64(probe->m_devX, 4);
    XQ_EQ_I64(probe->m_devY, 24);
    /* ③ 再锁一次"精确 2 倍"（同一效果在 dpr 1/2 两档的实测值）。 */
    XQ_EQ_I64(probe->m_devW, 56 * 2);
    XQ_EQ_I64(probe->m_devH, 46 * 2);
    XQ_EQ_I64(probe->m_devX, 2 * 2);
    XQ_EQ_I64(probe->m_devY, 12 * 2);
    /* 内容按 dpr 放大落位（与 dpr=1 同一批项本地采样点）。 */
    XQ_CHECK_MSG(s_devSamplePx[0] == 0xFFFF0000u,
                 "dpr=2 Device 源位图 (16,16) 应为项内容（实得 0x%08X）",
                 s_devSamplePx[0]);
    XQ_EQ_I64(s_devSamplePx[1], 0xFFFF0000u);
    XQ_EQ_I64(s_devSamplePx[2], 0xFFFF0000u);
    XQ_EQ_I64(s_devSamplePx[3], 0u);
    XQ_EQ_I64(s_devSamplePx[4], 0u);
    /* 设备系源包围盒同样按 2 倍（Qt dpr=2 探针的 dev 逻辑盒不变 ⇒ 物理 ×2）。 */
    XQ_EQ_I64(probe->m_devSourceBoundsX, 20);
    XQ_EQ_I64(probe->m_devSourceBoundsY, 40);
    XQ_EQ_I64(probe->m_devSourceBoundsW, 80);
    XQ_EQ_I64(probe->m_devSourceBoundsH, 60);
    /* dpr=2 出图：项占逻辑 (10..50, 20..50) ⇒ 物理 (20..100, 40..100)；
     * (60,70) 与 (40,50) 两个物理点都落在项内。 */
    XQ_CHECK(XImage_pixel(shot, 60, 70) == 0xFFFF0000u);
    XQ_CHECK(XImage_pixel(shot, 40, 50) == 0xFFFF0000u);
    XClassDelete((XClass*)shot);

    XGraphicsScene_clear(scene); /* 删项 ⇒ 删效果 */
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_itemAlive == 0);
    XQ_CHECK(s_effectAlive == 0);
}

/**
 * @brief 图形项 Device 源内容**逐像素**落位（复核阻断项回归锁）。
 * @details 项 = 40x30 纯红 @pos(10,20)、效果 pad=8：
 *          Device/PadEff 位图 56x46 @dpr=1（offset (2,12)）、112x92 @dpr=2
 *          （offset (4,24)，物理像素口径）。正确内容落位是
 *          `local = (q + 0.5 − pad×dpr) / dpr`：落进项框必为红、框外必透明。
 *          全部 56x46 / 112x92 像素逐一比对——内容一旦整体平移了项的
 *          场景位置 (10,20)（少乘画布原点平移，复核 gdb 实测的 (10,20)
 *          偏差），(8,8)/(16,16) 附近会成片透明、(18,28) 附近成片红，
 *          m_scanBad 立刻非零。固定采样点可能被巧合掩盖，全像素扫描不会。
 */
XQ_TEST(XGraphicsEffectDevice, deviceSourceContentPlacement)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* item =
        xq_devItemCreate(40.0f, 30.0f, 0xFFFF0000u);
    XGraphicsEffectDeviceProbe* probe = xq_devProbeCreate(8.0f);
    XPointF pos;
    XImage* shot;

    XQ_REQUIRE(scene != NULL && item != NULL && probe != NULL);
    pos.x = 10.0f;
    pos.y = 20.0f;
    XGraphicsItem_setPos(&item->m_base, &pos);
    XGraphicsScene_addItem(scene, &item->m_base);
    XGraphicsItem_setGraphicsEffect(&item->m_base, (XGraphicsEffect*)probe);

    /* 期望内容几何：项本地 (0,0,40,30)，补边 8。 */
    probe->m_scanContent = true;
    probe->m_scanW = 40;
    probe->m_scanH = 30;
    probe->m_scanPad = 8;
    probe->m_scanColor = 0xFFFF0000u;

    /* ---- dpr=1：位图 56x46、offset (2,12)、内容原点 (8,8)。 ---- */
    probe->m_scanDpr = 1.0f;
    probe->m_scanScale = 1.0f;
    shot = xq_devRenderScene(scene, 120, 120, 1.0f);
    XQ_REQUIRE(shot != NULL);
    XQ_REQUIRE(probe->m_scanChecked > 0);
    XQ_EQ_I64(probe->m_devW, 56);
    XQ_EQ_I64(probe->m_devH, 46);
    XQ_EQ_I64(probe->m_devX, 2);
    XQ_EQ_I64(probe->m_devY, 12);
    XQ_EQ_I64(probe->m_scanChecked, 56 * 46);
    XQ_CHECK_MSG(probe->m_scanBad == 0,
                 "dpr=1 Device 源内容落位不符 %d/%d 像素（首个 %d,%d）",
                 probe->m_scanBad, probe->m_scanChecked,
                 probe->m_scanFirstBadX, probe->m_scanFirstBadY);
    /* 场景出图同样按项位置落位（Device 源只是供效果消费，回贴仍走画布）。 */
    XQ_CHECK(XImage_pixel(shot, 30, 35) == 0xFFFF0000u);
    XQ_EQ_I64(xq_devAlphaAt(shot, 5, 5), 0);
    XClassDelete((XClass*)shot);

    /* ---- dpr=2：位图 112x92、offset (4,24)、物理内容原点 (16,16)。 ---- */
    probe->m_scanDpr = 2.0f;
    probe->m_scanScale = 2.0f;
    shot = xq_devRenderScene(scene, 120, 120, 2.0f);
    XQ_REQUIRE(shot != NULL);
    XQ_EQ_I64(probe->m_devW, 112);
    XQ_EQ_I64(probe->m_devH, 92);
    XQ_EQ_I64(probe->m_devX, 4);
    XQ_EQ_I64(probe->m_devY, 24);
    XQ_EQ_I64(probe->m_scanChecked, 112 * 92);
    XQ_CHECK_MSG(probe->m_scanBad == 0,
                 "dpr=2 Device 源内容落位不符 %d/%d 像素（首个 %d,%d）",
                 probe->m_scanBad, probe->m_scanChecked,
                 probe->m_scanFirstBadX, probe->m_scanFirstBadY);
    /* 设备系源包围盒（项本地框经设备变换）：逻辑 (10,20,40,30) ⇒ 物理 ×2。 */
    XQ_EQ_I64(probe->m_devSourceBoundsX, 20);
    XQ_EQ_I64(probe->m_devSourceBoundsY, 40);
    XQ_EQ_I64(probe->m_devSourceBoundsW, 80);
    XQ_EQ_I64(probe->m_devSourceBoundsH, 60);
    XQ_CHECK(XImage_pixel(shot, 60, 70) == 0xFFFF0000u);
    XQ_EQ_I64(xq_devAlphaAt(shot, 10, 10), 0);
    XClassDelete((XClass*)shot);

    probe->m_scanContent = false;
    XGraphicsScene_clear(scene); /* 删项 ⇒ 删效果 */
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_itemAlive == 0);
    XQ_CHECK(s_effectAlive == 0);
}

/* ==================== ⑭ Device pad 不随视图缩放 ==================== */
/**
 * @brief Device 源补边在**设备空间**求：视图缩放只放大内容、不放大 pad。
 * @details 项 40x30 @pos(10,20)、效果 pad=8、pad 模式 PadToEffectiveBoundingRect。
 *          Qt 6.8.0 offscreen 探针（QGraphicsView + QGraphicsEffect：sceneRect
 *          100x100、`view.scale(viewScale)`、`view.render(&painter)`，
 *          /tmp/b10pad-probe/probe.cpp）实测 Device 源：
 *            @code
 *            viewScale=1 dpr=1: size=56x46   off=(2,12)  src=(10,20 40x30)
 *            viewScale=2 dpr=1: size=96x76   off=(12,32) src=(20,40 80x60)
 *            viewScale=2 dpr=2: size=352x272 off=(32,72) src=(40,80 160x120)
 *            @endcode
 *          恒量：`sourceBoundingRect(Device).topLeft − effectRect.topLeft`
 *          在全部 (viewScale, dpr) 组合下都是 **8**（= pad 的设备逻辑单位，
 *          不随视图缩放放大；位图物理尺寸再 ×dpr —— Qt 的
 *          `QRectF(effectRectF.topLeft(), effectRectF.size() * dpr)`，
 *          qgraphicsitem.cpp:11109-11111）。
 *          **对拍只取该不变量**：Qt 的 widget 绘制把 dpr 也折进了
 *          worldTransform（探针里 dpr>1 那两次 src 已是 ×dpr 的坐标），
 *          XGui 走场景渲染口径（world 不含 dpr、dpr 由 painter 设备承载），
 *          物理尺寸沿用 qtProbeParity 锁定的 `逻辑 × dpr`（56x46 → 112x92）。
 *          XGui 的 deviceTransform 把 dpr
 *          折在末端 ⇒ 上下文携带 m_devicePixelRatio，sourcePixmap_2 先
 *          ÷dpr 回设备逻辑空间求补边、再 ×dpr 回物理像素。
 *          旧实现（源画布空间补边后整体乘 deviceTransform）在 viewScale=2
 *          时 pad 被放大到 16 —— 本用例 scale=2 段的数值断言即锁此回归。
 *          渲染目标与「补边与内容原点共用 bounds」不变量无关：内容原点
 *          仍在图像素 pad×dpr 处（全像素扫描断言）。
 */
XQ_TEST(XGraphicsEffectDevice, devicePadIndependentOfViewScale)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* item =
        xq_devItemCreate(40.0f, 30.0f, 0xFFFF0000u);
    XGraphicsEffectDeviceProbe* probe = xq_devProbeCreate(8.0f);
    XPointF pos;
    XImage* shot;
    int padBase;

    XQ_REQUIRE(scene != NULL && item != NULL && probe != NULL);
    pos.x = 10.0f;
    pos.y = 20.0f;
    XGraphicsItem_setPos(&item->m_base, &pos);
    XGraphicsScene_addItem(scene, &item->m_base);
    XGraphicsItem_setGraphicsEffect(&item->m_base, (XGraphicsEffect*)probe);

    probe->m_scanContent = true;
    probe->m_scanW = 40;
    probe->m_scanH = 30;
    probe->m_scanPad = 8;
    probe->m_scanColor = 0xFFFF0000u;

    /* ---- 基线 viewScale=1 / dpr=1：设备源 56x46、offset (2,12)。 ---- */
    probe->m_scanDpr = 1.0f;
    probe->m_scanScale = 1.0f;
    shot = xq_devRenderSceneScaled(scene, 120, 120, 1.0f, 1.0f);
    XQ_REQUIRE(shot != NULL);
    XQ_EQ_I64(probe->m_devW, 56);
    XQ_EQ_I64(probe->m_devH, 46);
    XQ_EQ_I64(probe->m_devX, 2);
    XQ_EQ_I64(probe->m_devY, 12);
    XQ_EQ_I64(probe->m_devSourceBoundsX, 10);
    XQ_EQ_I64(probe->m_devSourceBoundsY, 20);
    XQ_EQ_I64(probe->m_devSourceBoundsW, 40);
    XQ_EQ_I64(probe->m_devSourceBoundsH, 30);
    /* 外扩环（设备像素）= srcBounds.topLeft − offset = pad。 */
    padBase = probe->m_devSourceBoundsX - probe->m_devX;
    XQ_EQ_I64(padBase, 8);
    XQ_EQ_I64(probe->m_devSourceBoundsY - probe->m_devY, 8);
    XQ_EQ_I64(probe->m_devW - probe->m_devSourceBoundsW, 16);
    XQ_EQ_I64(probe->m_devH - probe->m_devSourceBoundsH, 16);
    XQ_EQ_I64(probe->m_scanChecked, 56 * 46);
    XQ_CHECK_MSG(probe->m_scanBad == 0,
                 "viewScale=1 dpr=1 内容落位不符 %d/%d 像素（首个 %d,%d）",
                 probe->m_scanBad, probe->m_scanChecked,
                 probe->m_scanFirstBadX, probe->m_scanFirstBadY);
    XClassDelete((XClass*)shot);

    /* ---- viewScale=1 / dpr=2：pad 按 dpr 放大（物理 16 = 8×2），
     *      设备逻辑 pad 仍 8 —— 与 viewScale=2/dpr=1 档同一判据。 ---- */
    probe->m_scanDpr = 2.0f;
    probe->m_scanScale = 2.0f; /* 图像素/项本地 = dpr × 视图缩放 */
    {
        /* 内容原点 = pad×dpr = (16,16)，内容 80x60 ⇒ [16,96)x[16,76)。 */
        XPoint pts[4];
        pts[0].x = 15; pts[0].y = 15;
        pts[1].x = 16; pts[1].y = 16;
        pts[2].x = 95; pts[2].y = 75;
        pts[3].x = 96; pts[3].y = 76;
        xq_devSetSamples(pts, 4);
    }
    shot = xq_devRenderScene(scene, 120, 120, 2.0f);
    XQ_REQUIRE(shot != NULL);
    XQ_EQ_I64(probe->m_devSourceBoundsX, 20);
    XQ_EQ_I64(probe->m_devSourceBoundsY, 40);
    XQ_EQ_I64(probe->m_devSourceBoundsW, 80);
    XQ_EQ_I64(probe->m_devSourceBoundsH, 60);
    XQ_EQ_I64(probe->m_devX, 4);
    XQ_EQ_I64(probe->m_devY, 24);
    XQ_EQ_I64(probe->m_devW, 112); /* 80 + 2×16 */
    XQ_EQ_I64(probe->m_devH, 92);  /* 60 + 2×16 */
    XQ_EQ_I64(probe->m_devSourceBoundsX - probe->m_devX, 2 * padBase);
    XQ_EQ_I64(probe->m_devSourceBoundsY - probe->m_devY, 2 * padBase);
    XQ_EQ_I64(probe->m_devW - probe->m_devSourceBoundsW, 4 * padBase);
    XQ_EQ_I64(probe->m_devH - probe->m_devSourceBoundsH, 4 * padBase);
    XQ_EQ_I64(probe->m_scanChecked, 112 * 92);
    XQ_CHECK_MSG(probe->m_scanBad == 0,
                 "viewScale=1 dpr=2 内容落位不符 %d/%d 像素（首个 %d,%d）",
                 probe->m_scanBad, probe->m_scanChecked,
                 probe->m_scanFirstBadX, probe->m_scanFirstBadY);
    XQ_EQ_I64(s_devSamplePx[0], 0u);
    XQ_EQ_I64(s_devSamplePx[1], 0xFFFF0000u);
    XQ_EQ_I64(s_devSamplePx[2], 0xFFFF0000u);
    XQ_EQ_I64(s_devSamplePx[3], 0u);
    /* 场景出图：项物理 (20,40)-(100,100)；项内红、项外透明。 */
    XQ_CHECK(XImage_pixel(shot, 60, 70) == 0xFFFF0000u);
    XQ_EQ_I64(xq_devAlphaAt(shot, 5, 5), 0);
    XClassDelete((XClass*)shot);

    /* ---- viewScale=2 / dpr=1：内容放大 2 倍（80x60），**pad 恒 8**。 ---- */
    probe->m_scanDpr = 1.0f;
    {
        /* 图像素：内容原点 = pad×dpr = (8,8)，内容 80x60 ⇒ [8,88)x[8,68)。 */
        XPoint pts[4];
        pts[0].x = 7;  pts[0].y = 7;   /* 外扩环内 ⇒ 透明 */
        pts[1].x = 8;  pts[1].y = 8;   /* 内容左上 */
        pts[2].x = 87; pts[2].y = 67;  /* 内容右下内侧 */
        pts[3].x = 88; pts[3].y = 68;  /* 内容外 ⇒ 透明 */
        xq_devSetSamples(pts, 4);
    }
    probe->m_scanScale = 2.0f; /* 图像素/项本地 = dpr × 视图缩放 */
    shot = xq_devRenderSceneScaled(scene, 160, 160, 1.0f, 2.0f);
    XQ_REQUIRE(shot != NULL);
    /* 设备系源包围盒 = 项在设备上的矩形：(20,40,80x60)。 */
    XQ_EQ_I64(probe->m_devSourceBoundsX, 20);
    XQ_EQ_I64(probe->m_devSourceBoundsY, 40);
    XQ_EQ_I64(probe->m_devSourceBoundsW, 80);
    XQ_EQ_I64(probe->m_devSourceBoundsH, 60);
    /* 补边仍是 8（Qt 探针同值），不是 16（旧实现的缩放放大形态）。 */
    XQ_EQ_I64(probe->m_devX, 12);
    XQ_EQ_I64(probe->m_devY, 32);
    XQ_EQ_I64(probe->m_devW, 96);  /* 80 + 2×8 */
    XQ_EQ_I64(probe->m_devH, 76);  /* 60 + 2×8 */
    XQ_EQ_I64(probe->m_devSourceBoundsX - probe->m_devX, padBase);
    XQ_EQ_I64(probe->m_devSourceBoundsY - probe->m_devY, padBase);
    XQ_EQ_I64(probe->m_devW - probe->m_devSourceBoundsW, 2 * padBase);
    XQ_EQ_I64(probe->m_devH - probe->m_devSourceBoundsH, 2 * padBase);
    XQ_EQ_I64(probe->m_scanChecked, 96 * 76);
    XQ_CHECK_MSG(probe->m_scanBad == 0,
                 "viewScale=2 dpr=1 内容落位不符 %d/%d 像素（首个 %d,%d）",
                 probe->m_scanBad, probe->m_scanChecked,
                 probe->m_scanFirstBadX, probe->m_scanFirstBadY);
    XQ_EQ_I64(s_devSamplePx[0], 0u);
    XQ_EQ_I64(s_devSamplePx[1], 0xFFFF0000u);
    XQ_EQ_I64(s_devSamplePx[2], 0xFFFF0000u);
    XQ_EQ_I64(s_devSamplePx[3], 0u);
    /* 场景出图：项占 (20,40)-(100,100)；项内红、项外透明。 */
    XQ_CHECK(XImage_pixel(shot, 60, 70) == 0xFFFF0000u);
    XQ_EQ_I64(xq_devAlphaAt(shot, 10, 10), 0);
    XClassDelete((XClass*)shot);

    /* ---- viewScale=2 / dpr=2：物理 pad = 8×dpr = 16（dpr 仍按 Qt 放大），
     *      设备逻辑 pad 仍 8（视图缩放不进 pad）。 ---- */
    probe->m_scanDpr = 2.0f;
    probe->m_scanScale = 4.0f; /* 图像素/项本地 = dpr × 视图缩放 */
    {
        /* 内容原点 = pad×dpr = (16,16)，内容 160x120 ⇒ [16,176)x[16,136)。 */
        XPoint pts[4];
        pts[0].x = 15;  pts[0].y = 15;
        pts[1].x = 16;  pts[1].y = 16;
        pts[2].x = 175; pts[2].y = 135;
        pts[3].x = 176; pts[3].y = 136;
        xq_devSetSamples(pts, 4);
    }
    shot = xq_devRenderSceneScaled(scene, 160, 160, 2.0f, 2.0f);
    XQ_REQUIRE(shot != NULL);
    XQ_EQ_I64(probe->m_devSourceBoundsX, 40);
    XQ_EQ_I64(probe->m_devSourceBoundsY, 80);
    XQ_EQ_I64(probe->m_devSourceBoundsW, 160);
    XQ_EQ_I64(probe->m_devSourceBoundsH, 120);
    XQ_EQ_I64(probe->m_devX, 24);
    XQ_EQ_I64(probe->m_devY, 64);
    XQ_EQ_I64(probe->m_devW, 192);  /* 160 + 2×16 */
    XQ_EQ_I64(probe->m_devH, 152);  /* 120 + 2×16 */
    /* 物理补边 = pad×dpr；设备逻辑补边（÷dpr）仍是 pad。 */
    XQ_EQ_I64(probe->m_devSourceBoundsX - probe->m_devX, 2 * padBase);
    XQ_EQ_I64(probe->m_devSourceBoundsY - probe->m_devY, 2 * padBase);
    XQ_EQ_I64(probe->m_devW - probe->m_devSourceBoundsW, 4 * padBase);
    XQ_EQ_I64(probe->m_devH - probe->m_devSourceBoundsH, 4 * padBase);
    XQ_EQ_I64(probe->m_scanChecked, 192 * 152);
    XQ_CHECK_MSG(probe->m_scanBad == 0,
                 "viewScale=2 dpr=2 内容落位不符 %d/%d 像素（首个 %d,%d）",
                 probe->m_scanBad, probe->m_scanChecked,
                 probe->m_scanFirstBadX, probe->m_scanFirstBadY);
    XQ_EQ_I64(s_devSamplePx[0], 0u);
    XQ_EQ_I64(s_devSamplePx[1], 0xFFFF0000u);
    XQ_EQ_I64(s_devSamplePx[2], 0xFFFF0000u);
    XQ_EQ_I64(s_devSamplePx[3], 0u);
    XQ_CHECK(XImage_pixel(shot, 120, 140) == 0xFFFF0000u);
    XQ_EQ_I64(xq_devAlphaAt(shot, 10, 10), 0);
    XClassDelete((XClass*)shot);

    xq_devSetSamples(NULL, 0);
    probe->m_scanContent = false;
    XGraphicsScene_clear(scene); /* 删项 ⇒ 删效果 */
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_itemAlive == 0);
    XQ_CHECK(s_effectAlive == 0);
}

/**
 * @brief 补边与内容原点共用同一个 `ctx->m_sourceBounds`（复核阻断项约束）。
 * @details 复核点的 Device 内容偏移 (10,20) 的根因形态：补边按
 *          `boundingRectFor(sourceBounds)` 算，而内容却从源画布 (0,0) 起画，
 *          两者用了不同原点 ⇒ 内容相对补边错开 sourceBounds.topLeft。
 *          本用例把**图形项源**的上下文摆成 Qt `boundingRect(Device)` 的
 *          真实形态——源内容占据画布 (10,20,40,30)（项场景位置 (10,20)）：
 *            pad=8 → effectRect (2,12,56,46)、offset (2,12)；
 *            内容原点必须是 sourceBounds.topLeft ⇒ 图像素 (8,8)。
 *          修复前（m_source 恒画在 0,0）内容落在 (0,0)，(8,8) 也会红但
 *          (0,0) 红、(47,37) 透明——角点断言即可钉死 (10,20) 偏移。
 */
XQ_TEST(XGraphicsEffectDevice, deviceSourceBoundsOriginShared)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* item =
        xq_devItemCreate(40.0f, 30.0f, 0xFFFF0000u);
    XGraphicsEffectDeviceProbe* probe = xq_devProbeCreate(8.0f);
    XGraphicsEffectDrawContext ctx;
    XImage* snapshot = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XImage* dest = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XImageTransform identity;
    XPoint off;
    XImage* dev;
    XRectF devBounds;

    XQ_REQUIRE(scene != NULL && item != NULL && probe != NULL &&
               snapshot != NULL && dest != NULL);
    XGraphicsScene_addItem(scene, &item->m_base);
    /* 图形项源（m_source 控件为 NULL）⇒ sourcePixmap_2 读 m_sourceBounds。 */
    XGraphicsItem_setGraphicsEffect(&item->m_base, (XGraphicsEffect*)probe);
    XQ_REQUIRE(XImage_reinit_ex(snapshot, 40, 30,
                                XImageFormat_ARGB32_Premultiplied));
    XQ_REQUIRE(XImage_reinit_ex(dest, 56, 46,
                                XImageFormat_ARGB32_Premultiplied));
    XImage_fillRect(snapshot, NULL, 0xFFFF0000u);
    XImage_fillRect(dest, NULL, 0u);

    XGraphicsEffectDrawContext_init(&ctx);
    ctx.m_source = snapshot;
    ctx.m_sourceRect.x = 0;
    ctx.m_sourceRect.y = 0;
    ctx.m_sourceRect.width = 40;
    ctx.m_sourceRect.height = 30;
    ctx.m_dest = dest;
    ctx.m_destRect.x = 0;
    ctx.m_destRect.y = 0;
    ctx.m_destRect.width = 56;
    ctx.m_destRect.height = 46;
    /* 源画布包围盒 = 源内容在画布里的**真实矩形**（非零原点）。 */
    ctx.m_sourceBounds.x = 10.0f;
    ctx.m_sourceBounds.y = 20.0f;
    ctx.m_sourceBounds.width = 40.0f;
    ctx.m_sourceBounds.height = 30.0f;
    XGraphicsItem_matrixIdentity(&identity);
    ctx.m_deviceTransform = identity;
    ctx.m_hasDeviceTransform = true;
    ((XGraphicsEffect*)probe)->m_drawCtx = &ctx;

    off.x = 0;
    off.y = 0;
    dev = XGraphicsEffect_sourcePixmap_2(
        (XGraphicsEffect*)probe, NULL, &off,
        XGraphicsEffectCoordinateSystem_Device);
    XQ_REQUIRE(dev != NULL);
    /* 补边：sourceBounds (10,20,40,30) ± 8 ⇒ (2,12,56,46)。 */
    XQ_EQ_I64(XImage_width(dev), 56);
    XQ_EQ_I64(XImage_height(dev), 46);
    XQ_EQ_I64(off.x, 2);
    XQ_EQ_I64(off.y, 12);
    /* 内容原点 = sourceBounds.topLeft − effectRect.topLeft = (8,8)。 */
    XQ_CHECK_MSG(XImage_pixel(dev, 8, 8) == 0xFFFF0000u,
                 "内容原点应在 (8,8)（实得 0x%08X）",
                 (uint32_t)XImage_pixel(dev, 8, 8));
    XQ_CHECK(XImage_pixel(dev, 47, 37) == 0xFFFF0000u);
    /* 修复前的错位形态：内容从 (0,0) 起画 ⇒ (0,0) 红、(47,37) 透明。 */
    XQ_CHECK_MSG(xq_devAlphaAt(dev, 0, 0) == 0u,
                 "补边左上角必须透明（内容整体平移 (10,20) 时会变红）");
    XQ_CHECK_MSG(xq_devAlphaAt(dev, 55, 45) == 0u,
                 "补边右下角必须透明");
    /* 设备系源包围盒 = sourceBounds 经 identity ⇒ (10,20,40,30)。 */
    devBounds = XGraphicsEffect_sourceBoundingRect_2(
        (XGraphicsEffect*)probe, NULL,
        XGraphicsEffectCoordinateSystem_Device);
    XQ_EQ_I64((int)devBounds.x, 10);
    XQ_EQ_I64((int)devBounds.y, 20);
    XQ_EQ_I64((int)devBounds.width, 40);
    XQ_EQ_I64((int)devBounds.height, 30);

    XClassDelete((XClass*)dev);
    ((XGraphicsEffect*)probe)->m_drawCtx = NULL;
    XGraphicsItem_setGraphicsEffect(&item->m_base, NULL); /* 删效果 */
    XClassDelete((XClass*)snapshot);
    XClassDelete((XClass*)dest);
    XGraphicsScene_clear(scene);
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_itemAlive == 0);
    XQ_CHECK(s_effectAlive == 0);
}

/* ==================== ⑧ 既有套件回归（由整跑取证；此处补守护断言） ==== */

/**
 * @brief 父项挂效果时子项不得被画两遍；父子各挂效果（嵌套）不递归失控。
 * @details 对标 Qt：drawItems 只遍历顶层项（qgraphicsscene.cpp:4610，
 *          取项经 estimateTopLevelItems :4624）、由 drawSubtreeRecursive
 *          （:4629）递归子树；本框架的项列表是扁平的
 *          （含子项），故渲染侧必须跳过"已随祖先效果一起画过"的项。
 *          用图元的 Paint 计数取证：无论父项有没有效果，每帧每个项
 *          只应被画 **1** 次。
 */
XQ_TEST(XGraphicsEffectDevice, nestedSubtreeNoDoublePaint)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* parent =
        xq_devItemCreate(80.0f, 60.0f, 0xFFFF0000u);
    XGraphicsEffectDeviceRectItem* child =
        xq_devItemCreate(20.0f, 20.0f, 0xFF0000FFu);
    XPointF pos;
    XImage* shot;
    int p0;
    int c0;

    XQ_REQUIRE(scene != NULL && parent != NULL && child != NULL);
    pos.x = 20.0f;
    pos.y = 20.0f;
    XGraphicsItem_setPos(&parent->m_base, &pos);
    pos.x = 10.0f;
    pos.y = 10.0f;
    XGraphicsItem_setPos(&child->m_base, &pos);
    XQ_REQUIRE(XGraphicsItem_setParentItem(&child->m_base, &parent->m_base));
    XGraphicsScene_addItem(scene, &parent->m_base);

    /* 无效果基线：父、子各画 1 次（扁平列表逐项绘制）。 */
    p0 = parent->m_paintCount;
    c0 = child->m_paintCount;
    shot = xq_devRenderScene(scene, 120, 120, 1.0f);
    XQ_REQUIRE(shot != NULL);
    XQ_EQ_I64(parent->m_paintCount - p0, 1);
    XQ_EQ_I64(child->m_paintCount - c0, 1);
    XClassDelete((XClass*)shot);

    /* 父挂不透明度 0.5：父+子离屏渲染各 1 次，外层列表必须跳过子。 */
    {
        XGraphicsOpacityEffect* opacity = XGraphicsOpacityEffect_create();
        XQ_REQUIRE(opacity != NULL);
        XGraphicsOpacityEffect_setOpacity(opacity, 0.5f);
        XGraphicsItem_setGraphicsEffect(&parent->m_base,
                                        (XGraphicsEffect*)opacity);
        p0 = parent->m_paintCount;
        c0 = child->m_paintCount;
        shot = xq_devRenderScene(scene, 120, 120, 1.0f);
        XQ_REQUIRE(shot != NULL);
        XQ_CHECK_MSG(child->m_paintCount - c0 == 1,
                     "挂效果的父项子树里子项只能画 1 次（实得 %d）",
                     child->m_paintCount - c0);
        XQ_EQ_I64(parent->m_paintCount - p0, 1);
        /* 父子都变半透明（α ≈ 128）；子区域仍是蓝（B > R）。 */
        XQ_CHECK_MSG(xq_devAlphaAt(shot, 50, 50) > 100u &&
                         xq_devAlphaAt(shot, 50, 50) < 160u,
                     "父区域应半透明（实得 %u）",
                     xq_devAlphaAt(shot, 50, 50));
        XQ_CHECK_MSG(xq_devAlphaAt(shot, 35, 35) > 100u &&
                         xq_devAlphaAt(shot, 35, 35) < 160u,
                     "子区域应半透明（实得 %u）",
                     xq_devAlphaAt(shot, 35, 35));
        {
            uint32_t px = (uint32_t)XImage_pixel(shot, 35, 35);
            XQ_CHECK_MSG((px & 0xFFu) > ((px >> 16) & 0xFFu),
                         "子项区域应是蓝色（pixel=0x%08X）", px);
        }
        XClassDelete((XClass*)shot);
    }

    /* 父子各挂一个效果（嵌套离屏渲染）：不崩、递归有界。 */
    {
        XGraphicsBlurEffect* blur = XGraphicsBlurEffect_create();
        XQ_REQUIRE(blur != NULL);
        XGraphicsBlurEffect_setBlurRadius(blur, 2.0f);
        XGraphicsItem_setGraphicsEffect(&child->m_base,
                                        (XGraphicsEffect*)blur);
        shot = xq_devRenderScene(scene, 120, 120, 1.0f);
        XQ_REQUIRE(shot != NULL);
        XQ_CHECK(xq_devAlphaAt(shot, 50, 50) > 0u);
        XClassDelete((XClass*)shot);
    }

    /* 卸载后回到基线（每帧各 1 次），且效果全部释放。 */
    XGraphicsItem_setGraphicsEffect(&parent->m_base, NULL);
    XGraphicsItem_setGraphicsEffect(&child->m_base, NULL);
    p0 = parent->m_paintCount;
    c0 = child->m_paintCount;
    shot = xq_devRenderScene(scene, 120, 120, 1.0f);
    XQ_REQUIRE(shot != NULL);
    XQ_EQ_I64(parent->m_paintCount - p0, 1);
    XQ_EQ_I64(child->m_paintCount - c0, 1);
    XClassDelete((XClass*)shot);

    XGraphicsScene_clear(scene); /* 删顶层父项 ⇒ 级联删子项 */
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_itemAlive == 0);
    XQ_CHECK(s_effectAlive == 0);
}

/**
 * @brief 图形项未挂效果时渲染路径逐像素不变（回归锁的用例内锚点）。
 * @details 对齐依据：XGraphicsScene_render 的常规分支在 W4 之后必须与之前
 *          完全一致——本用例用「同一场景两次渲染像素全等」+「项外全透明」
 *          锁住"无效果走原路径"。
 */
XQ_TEST(XGraphicsEffectDevice, noEffectPathUnchanged)
{
    XGraphicsScene* scene = XGraphicsScene_create();
    XGraphicsEffectDeviceRectItem* item = xq_devItemCreate(30.0f, 20.0f,
                                                           0xFF102030u);
    XPointF pos;
    XImage* first;
    XImage* second;
    int x;
    int y;
    int diff = 0;

    XQ_REQUIRE(scene != NULL && item != NULL);
    pos.x = 7.0f;
    pos.y = 9.0f;
    XGraphicsItem_setPos(&item->m_base, &pos);
    XGraphicsScene_addItem(scene, &item->m_base);
    first = xq_devRenderScene(scene, 60, 50, 1.0f);
    second = xq_devRenderScene(scene, 60, 50, 1.0f);
    XQ_REQUIRE(first != NULL && second != NULL);
    for (y = 0; y < 50; ++y)
        for (x = 0; x < 60; ++x)
            if (XImage_pixel(first, x, y) != XImage_pixel(second, x, y)) ++diff;
    XQ_EQ_I64(diff, 0);
    XQ_CHECK(XImage_pixel(first, 20, 15) == 0xFF102030u);
    XQ_EQ_I64(xq_devAlphaAt(first, 2, 2), 0);
    XQ_EQ_I64(xq_devAlphaAt(first, 40, 40), 0);
    XClassDelete((XClass*)first);
    XClassDelete((XClass*)second);
    XGraphicsScene_clear(scene);
    XClassDelete((XClass*)scene);
    XQ_CHECK(s_itemAlive == 0);
}
