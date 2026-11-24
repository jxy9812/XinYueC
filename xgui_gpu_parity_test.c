/******************************************************************************
 * @file       xgui_gpu_parity_test.c
 * @brief      GPU 后端 vs 软件光栅 逐像素一致性对照工具。
 * @details    同一套图元场景分别在各后端渲染，把结果以**原始 ARGB32
 *             像素**（不是 PNG/JPEG，避免有损编码掩盖差异）dump 到文件，
 *             由两次进程运行（环境变量选后端）产出的 dump 逐像素比对：
 *
 *                 xgui_gpu_parity_test --dump sw.raw        # 软件
 *                 set XGUI_RENDER_BACKEND=vulkan && ... --dump vk.raw
 *                 xgui_gpu_parity_test --diff sw.raw vk.raw  # 比对
 *
 *             为什么要分进程：后端选择走环境变量且进程内**一次性缓存**
 *             （XGpuRenderBackend_requested 的 static cached），同一个
 *             进程里切不干净——两次进程运行是唯一无污染的口径。
 *
 *             diff 输出：不一致像素数、最大通道差、逐场景分项统计、
 *             前 N 个差异点的坐标与两侧颜色，并按「差异是否落在 AA 边缘」
 *             归类（GPU 与软件光栅在 AA 覆盖度上允许 ±1 的量化差，
 *             硬边/纯色区则必须逐位相同）。
 * @note       退出码：0=一致（或无差异）；1=存在超容忍差异；2=参数/IO 错。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImage.h"
#include "XPainter.h"
#include "XGpuRenderBackend.h"
#include "XDateTime.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define setenv(name, value, overwrite) _putenv_s((name), (value))
#endif

/* ==================== dump 文件格式 ==================== */

/* 固定头：magic + 版本 + 宽高 + 场景数；随后每个场景一段像素。
 * 全部小端，ARGB32 预乘（与 XImage_bits 的内存布局一致）。 */
#define XPARTY_MAGIC   0x59545250u /* "PRTY" 小端 */
#define XPARTY_VERSION 1u
#define XPARTY_WIDTH   96
#define XPARTY_HEIGHT  96

typedef struct XParityHeader
{
    uint32_t m_magic;
    uint32_t m_version;
    uint32_t m_width;
    uint32_t m_height;
    uint32_t m_sceneCount;
} XParityHeader;

/* 场景枚举：每个场景一个独立的 XImage（互不干扰，差异可归因）。 */
typedef enum XParityScene
{
    XParityScene_Fill = 0,      /**< 不透明 fillRect。 */
    XParityScene_FillTranslucent, /**< 半透明 fillRect + opacity。 */
    XParityScene_Image,         /**< drawImage 1:1 贴图。 */
    XParityScene_Text,          /**< drawText 字形。 */
    XParityScene_AaPolygon,     /**< AA 填充三角形。 */
    XParityScene_AxisLine,      /**< 轴对齐线 + 矩形边框。 */
    XParityScene_Gradient,      /**< 线性渐变填充。 */
    XParityScene_ImageRect,     /**< drawImageRect 源子矩形缩放贴图。 */
    XParityScene_ImageRectSmooth, /**< 平滑提示下的缩放贴图（双线性）。 */
    XParityScene_PolyBatch,     /**< 多笔非 AA 填充 + 中间原语（跨批顺序）。 */
    XParityScene_ClipPathAA,    /**< 路径裁剪下的 AA 填充 + AA 线（掩码烘焙）。 */
    XParityScene_ClipRegionQuads, /**< 多矩形区域裁剪下的实心 quad/线/点。 */
    XParityScene_Points,        /**< 零长度点标记（点 quad 快速路径）。 */
    XParityScene_TileImage,     /**< 裁剪矩形内重复贴图（QSS 背景平铺）。 */
    XParityScene_ImageRectScaled, /**< 缩放变换下的贴图（软件暂存画布腿）。 */
    XParityScene_Count         /**< 场景总数哨兵。 */
} XParityScene;

static const char* xpartySceneName(int scene)
{
    switch (scene)
    {
    case XParityScene_Fill: return "fill-opaque";
    case XParityScene_FillTranslucent: return "fill-translucent";
    case XParityScene_Image: return "draw-image";
    case XParityScene_Text: return "draw-text";
    case XParityScene_AaPolygon: return "aa-polygon";
    case XParityScene_AxisLine: return "axis-line";
    case XParityScene_Gradient: return "gradient";
    case XParityScene_ImageRect: return "draw-image-rect";
    case XParityScene_ImageRectSmooth: return "draw-image-smooth";
    case XParityScene_PolyBatch: return "poly-batch";
    case XParityScene_ClipPathAA: return "clip-path-aa";
    case XParityScene_ClipRegionQuads: return "clip-region-quads";
    case XParityScene_Points: return "points";
    case XParityScene_TileImage: return "tile-image";
    case XParityScene_ImageRectScaled: return "draw-image-rect-scaled";
    default: return "unknown";
    }
}

/* ==================== 场景渲染 ==================== */

/**
 * @brief      把指定场景画进一张新 XImage（深灰底，便于看出未绘制区域）。
 * @details    painter 的后端由环境变量在进程启动时决定，本函数不关心。
 * @return     true 绘制成功（含后端降级——降级本身不是本工具的判据，
 *             逐像素差异才是）。
 */
static int xpartyRenderScene(XParityScene scene, XImage* out)
{
    XPainter painter;
    int ok = 1;

    XImage_init_ex(out, XPARTY_WIDTH, XPARTY_HEIGHT, XImageFormat_ARGB32);
    XImage_fillRect(out, NULL, 0xff202020u);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, out)) return 0;

    switch (scene)
    {
    case XParityScene_Fill:
        {
            XRect r = { 8, 8, 80, 80 };
            if (!XPainter_fillRect(&painter, &r, 0xffc04020u)) ok = 0;
        }
        break;
    case XParityScene_FillTranslucent:
        {
            XRect r = { 8, 8, 80, 80 };
            XPainter_setOpacity(&painter, 0.5f);
            if (!XPainter_fillRect(&painter, &r, 0x80406080u)) ok = 0;
            XPainter_setOpacity(&painter, 1.0f);
        }
        break;
    case XParityScene_Image:
        {
            XImage tile;
            int ty;
            int tx;
            XImage_init_ex(&tile, 8, 8, XImageFormat_ARGB32);
            /* 不透明块状渐变：alpha 必须放在最高字节（ARGB32 语义），
               否则源图全透明、drawImage 什么都不画（软件侧正确跳过，
               GPU 侧若仍画出内容即为「忽略源 alpha」缺陷）。 */
            for (ty = 0; ty < 8; ++ty)
                for (tx = 0; tx < 8; ++tx)
                    XImage_setPixel(&tile, tx, ty,
                                    0xff000000u |
                                    ((uint32_t)(tx * 32) << 16) |
                                    ((uint32_t)(ty * 32) << 8) |
                                    (uint32_t)(tx * 16 + ty * 8));
            if (!XPainter_drawImage(&painter, &tile, 20, 20)) ok = 0;
            XClassDeinit(&tile);
        }
        break;
    case XParityScene_Text:
        if (!XPainter_drawText(&painter, 8, 40, "Ag8", 0xffffffffu)) ok = 0;
        break;
    case XParityScene_AaPolygon:
        {
            XPainterPath path;
            XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing,
                                   true);
            XPainter_setBrush(&painter, 0xff00ff00u);
            XPainterPath_init(&path);
            XPainterPath_moveTo(&path, 10.0f, 80.0f);
            XPainterPath_lineTo(&path, 48.0f, 12.0f);
            XPainterPath_lineTo(&path, 86.0f, 80.0f);
            XPainterPath_closeSubpath(&path);
            if (!XPainter_fillPath(&painter, &path)) ok = 0;
            XPainterPath_deinit(&path);
        }
        break;
    case XParityScene_AxisLine:
        if (!XPainter_drawLine(&painter, 10, 12, 86, 12)) ok = 0;
        if (!XPainter_drawLine(&painter, 10, 20, 10, 84)) ok = 0;
        if (!XPainter_drawRect(&painter, &(XRect){30, 30, 40, 40})) ok = 0;
        break;
    case XParityScene_Gradient:
        {
            XPainterGradient gradient;
            XRect r = { 8, 8, 80, 80 };
            XPainterGradient_initLinear(&gradient, 8.0f, 8.0f, 88.0f, 8.0f);
            XPainterGradient_addStop(&gradient, 0.0f, 0xffff0000u);
            XPainterGradient_addStop(&gradient, 1.0f, 0xff0000ffu);
            XPainter_setBrushGradient(&painter, &gradient);
            if (!XPainter_fillRect_2(&painter, &r)) ok = 0;
        }
        break;
    case XParityScene_ImageRect:
        {
            /* 源子矩形 4x4 放大 12 倍到 48x48：覆盖「源矩形 → UV 子区间」
               的换算与最近邻采样取整。源块颜色在 4x4 内四色分区，缩放后
               每个色块边界位置对取整误差敏感，能直接暴露 UV 半像素偏移。 */
            XImage tile;
            int ty;
            int tx;
            XRect source = { 1, 1, 4, 4 };
            XRect target = { 20, 20, 48, 48 };
            XImage_init_ex(&tile, 8, 8, XImageFormat_ARGB32);
            for (ty = 0; ty < 8; ++ty)
                for (tx = 0; tx < 8; ++tx)
                {
                    uint32_t c = ((tx ^ ty) & 1) ? 0xffe0c040u : 0xff2050e0u;
                    if (tx == 1 || ty == 1) c = 0xff40e060u;
                    XImage_setPixel(&tile, tx, ty, c);
                }
            if (!XPainter_drawImageRect(&painter, &target, &tile, &source))
                ok = 0;
            XClassDeinit(&tile);
        }
        break;
    case XParityScene_ImageRectSmooth:
        {
            /* 平滑提示 + 非整数缩放：软件走双线性，GPU 走线性采样器。
               两者算法同为 2x2 加权，允许 ±1~2 量化差（感知判据），
               用于锁住「M2b 缩放贴图不再掉软件局部提交」。 */
            XImage tile;
            int ty;
            int tx;
            XRect source = { 0, 0, 8, 8 };
            XRect target = { 20, 20, 51, 47 };
            XImage_init_ex(&tile, 8, 8, XImageFormat_ARGB32);
            for (ty = 0; ty < 8; ++ty)
                for (tx = 0; tx < 8; ++tx)
                    XImage_setPixel(&tile, tx, ty,
                                    0xff000000u |
                                    ((uint32_t)(tx * 28) << 16) |
                                    ((uint32_t)(ty * 24) << 8) |
                                    (uint32_t)(tx * 12 + ty * 20));
            XPainter_setRenderHint(&painter,
                                   XPainterRenderHint_SmoothPixmapTransform,
                                   true);
            if (!XPainter_drawImageRect(&painter, &target, &tile, &source))
                ok = 0;
            XPainter_setRenderHint(&painter,
                                   XPainterRenderHint_SmoothPixmapTransform,
                                   false);
            XClassDeinit(&tile);
        }
        break;
    case XParityScene_PolyBatch:
        {
            /* 三笔顺序：实色三角形 → 矩形 → 半透明三角形。非 AA 路径填充
               在 GPU 会话下走批画布跨批（P2-3）；若批在中间原语前没有
               正确落盘，或最后一笔跨越前两笔，逐像素对照会直接爆量。
               用途：锁住「跨批存续 + 原语间顺序」。 */
            XPainterPath path;
            XRect rect = { 20, 20, 40, 40 };
            XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing,
                                   false);
            XPainter_setBrush(&painter, 0xff00c040u);
            XPainterPath_init(&path);
            XPainterPath_moveTo(&path, 10.0f, 10.0f);
            XPainterPath_lineTo(&path, 50.0f, 10.0f);
            XPainterPath_lineTo(&path, 30.0f, 50.0f);
            XPainterPath_closeSubpath(&path);
            if (!XPainter_fillPath(&painter, &path)) ok = 0;
            XPainterPath_deinit(&path);
            XPainter_setBrush(&painter, 0xffe02020u);
            if (!XPainter_fillRect(&painter, &rect, 0xffe02020u)) ok = 0;
            XPainterPath_init(&path);
            XPainterPath_moveTo(&path, 40.0f, 40.0f);
            XPainterPath_lineTo(&path, 86.0f, 40.0f);
            XPainterPath_lineTo(&path, 60.0f, 80.0f);
            XPainterPath_closeSubpath(&path);
            XPainter_setOpacity(&painter, 0.5f);
            XPainter_setBrush(&painter, 0xff2040ffu);
            if (!XPainter_fillPath(&painter, &path)) ok = 0;
            XPainter_setOpacity(&painter, 1.0f);
            XPainterPath_deinit(&path);
        }
        break;
    case XParityScene_ClipPathAA:
        {
            /* 路径裁剪 + AA：锁住「裁剪掩码烘进覆盖率 → GPU 直绘」通道。
               菱形裁剪内画 AA 三角形与 AA 斜线；掩码乘法口径若与软件
               的 painterMul255 不一致，边缘会出现成片 >3/chan 差异。 */
            XPainterPath clip;
            XPainterPath shape;
            XPainterPath_init(&clip);
            XPainterPath_moveTo(&clip, 48.0f, 6.0f);
            XPainterPath_lineTo(&clip, 90.0f, 48.0f);
            XPainterPath_lineTo(&clip, 48.0f, 90.0f);
            XPainterPath_lineTo(&clip, 6.0f, 48.0f);
            XPainterPath_closeSubpath(&clip);
            XPainter_setClipPath(&painter, &clip,
                                 XPainterClipOperation_ReplaceClip);
            XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing,
                                   true);
            XPainter_setBrush(&painter, 0xff20c060u);
            XPainterPath_init(&shape);
            XPainterPath_moveTo(&shape, 10.0f, 80.0f);
            XPainterPath_lineTo(&shape, 48.0f, 12.0f);
            XPainterPath_lineTo(&shape, 86.0f, 80.0f);
            XPainterPath_closeSubpath(&shape);
            if (!XPainter_fillPath(&painter, &shape)) ok = 0;
            XPainterPath_deinit(&shape);
            XPainter_setPen(&painter, 0xffe0a020u);
            XPainter_setPenWidth(&painter, 3);
            if (!XPainter_drawLine(&painter, 12, 18, 84, 78)) ok = 0;
            XPainter_setClipPath(&painter, &clip,
                                 XPainterClipOperation_NoClip);
            XPainterPath_deinit(&clip);
        }
        break;
    case XParityScene_ClipRegionQuads:
        {
            /* 多矩形区域裁剪下的实心 quad / 轴对齐线 / 点：锁住
               「逐区域矩形 scissor 直绘同一 quad」通道（区域互不重叠
               ⇒ 混合次数不变）。区域外像素必须保持背景。 */
            XRegion region;
            XRect r1 = { 6, 6, 36, 60 };
            XRect r2 = { 52, 26, 38, 62 };
            XRect bg = { 0, 0, 96, 96 };
            XRegion_init(&region);
            XRegion_addRect(&region, &r1);
            XRegion_addRect(&region, &r2);
            XPainter_setClipRegion(&painter, &region,
                                   XPainterClipOperation_ReplaceClip);
            if (!XPainter_fillRect(&painter, &bg, 0xff403020u)) ok = 0;
            XPainter_setPen(&painter, 0xff40a0ffu);
            XPainter_setPenWidth(&painter, 1);
            if (!XPainter_drawLine(&painter, 8, 20, 40, 20)) ok = 0;
            if (!XPainter_drawLine(&painter, 8, 20, 8, 60)) ok = 0;
            /* 只用轴对齐线：斜线的 GPU quad 与软件 Bresenham 覆盖本就
               非逐位一致（既有口径，XGPU_FASTPATH_EXT 注释），会让本场景
               的像素锁失真；斜线专属场景由 poly-batch/aa-polygon 覆盖。 */
            if (!XPainter_drawLine(&painter, 60, 32, 60, 70)) ok = 0;
            XPainter_setPenWidth(&painter, 3);
            if (!XPainter_drawLine(&painter, 60, 40, 60, 40)) ok = 0;
            if (!XPainter_drawLine(&painter, 70, 50, 70, 50)) ok = 0;
            XPainter_setClipRegion(&painter, &region,
                                   XPainterClipOperation_NoClip);
            XRegion_deinit(&region);
        }
        break;
    case XParityScene_TileImage:
        {
            /* QSS background-image 同形：裁剪矩形内按 8px 网格重复贴同一
               张 8x8 不透明图（连续同源 quad + 裁剪）。 */
            XImage tile;
            XRect clipRect;
            int ty;
            int tx;
            XImage_init_ex(&tile, 8, 8, XImageFormat_ARGB32);
            for (ty = 0; ty < 8; ++ty)
                for (tx = 0; tx < 8; ++tx)
                    XImage_setPixel(&tile, tx, ty,
                                    (tx == 0 || ty == 0) ? 0xff104010u
                                                         : 0xff00ff00u);
            clipRect.x = 12;
            clipRect.y = 12;
            clipRect.width = 72;
            clipRect.height = 48;
            XPainter_save(&painter);
            XPainter_setClipRect(&painter, &clipRect,
                                 XPainterClipOperation_IntersectClip);
            for (ty = 12; ty < 60; ty += 8)
                for (tx = 12; tx < 84; tx += 8)
                    if (!XPainter_drawImage(&painter, &tile, tx, ty)) ok = 0;
            XPainter_restore(&painter);
            XClassDeinit(&tile);
        }
        break;
    case XParityScene_ImageRectScaled:
        {
            /* 非轴对齐（缩放）变换下的贴图：GPU 侧 drawImageUv 只接受
               单位/平移，必落「软件暂存画布 + 脏区区域上传」腿。锁
               xvkl_copy_rows_for_upload 的行距契约（2026-11-23 实锤：
               该腿调用传 stride=0，被当日防御守卫判非法后整段零拷贝 ⇒
               离屏 VK 缩放图/九宫 border-image 内容全丢；软件参考走同
               一条光栅代码，逐像素应完全一致）。 */
            XImage tile;
            int ty;
            int tx;
            XRect source = { 0, 0, 8, 8 };
            XRect target = { 6, 6, 40, 30 };
            XImage_init_ex(&tile, 8, 8, XImageFormat_ARGB32);
            for (ty = 0; ty < 8; ++ty)
                for (tx = 0; tx < 8; ++tx)
                    XImage_setPixel(&tile, tx, ty,
                                    0xff000000u |
                                    ((uint32_t)(tx * 32) << 16) |
                                    ((uint32_t)(ty * 32) << 8) |
                                    (uint32_t)(200 - tx * 16));
            XPainter_scale(&painter, 2.0f, 2.0f);
            if (!XPainter_drawImageRect(&painter, &target, &tile, &source))
                ok = 0;
            XPainter_scale(&painter, 0.5f, 0.5f);
            XClassDeinit(&tile);
        }
        break;
    case XParityScene_Points:
        {
            /* 零长度点标记（drawLine(x,y,x,y)）：宽度方块像素范围必须与
               软件逐位一致（M4c 点 quad 快速路径的像素锁）。覆盖
               width=1 与 width=3 两档、不同颜色与位置。 */
            int i;
            XPoint pts[12] = {
                { 10, 12 }, { 26, 12 }, { 42, 12 }, { 58, 12 },
                { 74, 12 }, { 10, 34 }, { 26, 34 }, { 42, 34 },
                { 58, 34 }, { 74, 34 }, { 10, 58 }, { 26, 58 }
            };
            XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing,
                                   false);
            XPainter_setPen(&painter, 0xff30d0a0u);
            XPainter_setPenWidth(&painter, 1);
            for (i = 0; i < 6; ++i)
                if (!XPainter_drawLine(&painter, pts[i].x, pts[i].y,
                                       pts[i].x, pts[i].y))
                    ok = 0;
            XPainter_setPen(&painter, 0xffd05030u);
            XPainter_setPenWidth(&painter, 3);
            for (i = 6; i < 12; ++i)
                if (!XPainter_drawLine(&painter, pts[i].x, pts[i].y,
                                       pts[i].x, pts[i].y))
                    ok = 0;
        }
        break;
    default:
        break;
    }

    if (!XPainter_end(&painter)) ok = 0;
    XPainter_deinit(&painter);
    return ok;
}

/* ==================== 写 dump ==================== */

/* 调试用场景过滤（--scene N 可重复/逗号分隔）：只渲染并落盘指定
 * 场景，便于对 VK 专有像素差做快速迭代（整包 14 场景逐次渲染太慢）。
 * 空列表 = 全部场景。 */
static int g_xpartySceneList[32];
static int g_xpartySceneListCount;

static int xpartyDump(const char* path)
{
    FILE* fp;
    XParityHeader header;
    int i;
    int ok = 1;
    int filtered = g_xpartySceneListCount > 0;

    fp = fopen(path, "wb");
    if (!fp)
    {
        fprintf(stderr, "parity: cannot open %s for write\n", path);
        return 2;
    }
    header.m_magic = XPARTY_MAGIC;
    header.m_version = XPARTY_VERSION;
    header.m_width = XPARTY_WIDTH;
    header.m_height = XPARTY_HEIGHT;
    header.m_sceneCount = filtered
        ? (uint32_t)g_xpartySceneListCount : (uint32_t)XParityScene_Count;
    fwrite(&header, sizeof(header), 1, fp);

    for (i = 0; i < (filtered ? g_xpartySceneListCount
                              : (int)XParityScene_Count); ++i)
    {
        int scene = filtered ? g_xpartySceneList[i] : i;
        XImage frame;
        const uint8_t* bits;
        int bpl;
        int y;
        const char* backendName;

        if (!xpartyRenderScene((XParityScene)scene, &frame))
            ok = 0;
        backendName = (XImage_width(&frame) == 0) ? "?" : "";
        if (i == 0)
            fprintf(stderr, "parity: backend=%s\n", backendName);
        bits = XImage_bits(&frame);
        bpl = XImage_bytesPerLine(&frame);
        if (!bits || bpl < XPARTY_WIDTH * 4)
        {
            fprintf(stderr, "parity: scene %d no pixel buffer\n", scene);
            XClassDeinit(&frame);
            fclose(fp);
            return 2;
        }
        for (y = 0; y < XPARTY_HEIGHT; ++y)
            fwrite(bits + (size_t)y * (size_t)bpl, 4, XPARTY_WIDTH, fp);
        XClassDeinit(&frame);
    }
    fclose(fp);
    fprintf(stderr, "parity: dumped %d scenes to %s (%s)\n",
            (int)XParityScene_Count, path, ok ? "ok" : "with draw failures");
    return 0;
}

/* ==================== 读 dump 并比对 ==================== */

typedef struct XParityDiffStat
{
    long long m_pixels;      /**< 该场景像素总数。 */
    long long m_diff;        /**< 不一致像素数。 */
    int m_maxChannelDelta;   /**< 单通道最大绝对差。 */
    long long m_hardDiff;    /**< 差异且两侧都不像 AA 边缘的数量。 */
} XParityDiffStat;

/* 判定一个像素是否"像 AA 边缘"：它的颜色既非两侧纯色，也不是纯色区
   常见的整数倍。这里用更稳的口径——只要两侧通道差 <= 3 就视为量化差。 */
static int xpartyIsQuantDiff(uint32_t a, uint32_t b)
{
    int da = (int)((a >> 24) & 0xff) - (int)((b >> 24) & 0xff);
    int dr = (int)((a >> 16) & 0xff) - (int)((b >> 16) & 0xff);
    int dg = (int)((a >> 8) & 0xff) - (int)((b >> 8) & 0xff);
    int db = (int)(a & 0xff) - (int)(b & 0xff);
    if (da < 0) da = -da;
    if (dr < 0) dr = -dr;
    if (dg < 0) dg = -dg;
    if (db < 0) db = -db;
    return (da <= 3 && dr <= 3 && dg <= 3 && db <= 3);
}

static int xpartyMaxChannelDelta(uint32_t a, uint32_t b)
{
    int m = 0;
    int i;
    for (i = 0; i < 4; ++i)
    {
        int d = (int)((a >> (i * 8)) & 0xff) - (int)((b >> (i * 8)) & 0xff);
        if (d < 0) d = -d;
        if (d > m) m = d;
    }
    return m;
}

static int xpartyDiff(const char* pathA, const char* pathB)
{
    FILE* fa;
    FILE* fb;
    XParityHeader ha;
    XParityHeader hb;
    XParityDiffStat stats[XParityScene_Count];
    int scene;
    int printed = 0;
    int hardTotal = 0;
    long long diffTotal = 0;

    fa = fopen(pathA, "rb");
    fb = fopen(pathB, "rb");
    if (!fa || !fb)
    {
        fprintf(stderr, "parity: cannot open dumps\n");
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        return 2;
    }
    if (fread(&ha, sizeof(ha), 1, fa) != 1 ||
        fread(&hb, sizeof(hb), 1, fb) != 1)
    {
        fprintf(stderr, "parity: truncated header\n");
        fclose(fa);
        fclose(fb);
        return 2;
    }
    if (ha.m_magic != XPARTY_MAGIC || hb.m_magic != XPARTY_MAGIC)
    {
        fprintf(stderr, "parity: bad magic\n");
        fclose(fa);
        fclose(fb);
        return 2;
    }
    if (ha.m_width != hb.m_width || ha.m_height != hb.m_height ||
        ha.m_sceneCount != hb.m_sceneCount)
    {
        fprintf(stderr, "parity: geometry/scene count mismatch\n");
        fclose(fa);
        fclose(fb);
        return 2;
    }
    /* 尺寸上限：dump 是本地测试产物，但损坏文件不得驱动超大分配。 */
    if (ha.m_width == 0 || ha.m_height == 0 ||
        ha.m_width > 16384u || ha.m_height > 16384u ||
        ha.m_sceneCount > (uint32_t)XParityScene_Count)
    {
        fprintf(stderr, "parity: unsupported dump geometry\n");
        fclose(fa);
        fclose(fb);
        return 2;
    }
    memset(stats, 0, sizeof(stats));

    printf("parity: comparing A=%s  B=%s  (%ux%u, %u scenes)\n",
           pathA, pathB, (unsigned)ha.m_width, (unsigned)ha.m_height,
           (unsigned)ha.m_sceneCount);
    printf("%-18s %10s %10s %12s %8s\n",
           "scene", "diffPx", "hardDiff", "maxChanD", "total");

    {
        /* 行缓冲按 dump 头宽动态分配：页面级 A/B（静态层门禁）宽度远超
           固定的 96px parity 场景；固定数组会越界写栈。 */
        uint32_t* rowA = (uint32_t*)malloc((size_t)ha.m_width * 4u);
        uint32_t* rowB = (uint32_t*)malloc((size_t)ha.m_width * 4u);
        if (!rowA || !rowB)
        {
            fprintf(stderr, "parity: row buffer allocation failed\n");
            free(rowA);
            free(rowB);
            fclose(fa);
            fclose(fb);
            return 2;
        }
    for (scene = 0; scene < (int)ha.m_sceneCount; ++scene)
    {
        const char* label = (ha.m_sceneCount == 1) ? "page"
                                                   : xpartySceneName(scene);
        int y;
        int x;

        for (y = 0; y < (int)ha.m_height; ++y)
        {
            if (fread(rowA, 4, ha.m_width, fa) != ha.m_width ||
                fread(rowB, 4, ha.m_width, fb) != ha.m_width)
            {
                fprintf(stderr, "parity: truncated pixel data at scene %d\n",
                        scene);
                free(rowA);
                free(rowB);
                fclose(fa);
                fclose(fb);
                return 2;
            }
            for (x = 0; x < (int)ha.m_width; ++x)
            {
                uint32_t a = rowA[x];
                uint32_t b = rowB[x];
                int delta;
                ++stats[scene].m_pixels;
                if (a == b) continue;
                ++stats[scene].m_diff;
                ++diffTotal;
                delta = xpartyMaxChannelDelta(a, b);
                if (delta > stats[scene].m_maxChannelDelta)
                    stats[scene].m_maxChannelDelta = delta;
                if (!xpartyIsQuantDiff(a, b))
                {
                    ++stats[scene].m_hardDiff;
                    ++hardTotal;
                    /* 诊断明细上限：覆盖前两个大场景的差异点，便于一次
                       定位多点回归；汇总表仍是判据。 */
                    if (printed < 300)
                    {
                        printf("  ! %-16s (%2d,%2d) A=%08x B=%08x d=%d\n",
                               label, x, y,
                               (unsigned)a, (unsigned)b, delta);
                        ++printed;
                    }
                }
            }
        }
        printf("%-18s %10lld %10lld %12d %8lld\n",
               label, stats[scene].m_diff,
               stats[scene].m_hardDiff, stats[scene].m_maxChannelDelta,
               stats[scene].m_pixels);
    }
    free(rowA);
    free(rowB);
    }
    fclose(fa);
    fclose(fb);

    printf("\nparity: total diff px=%lld, hard(>3/chan) px=%d\n",
           diffTotal, hardTotal);
    if (hardTotal == 0)
    {
        printf("parity: PASS (only <=3/chan quantization differences)\n");
        return 0;
    }
    printf("parity: FAIL (%d pixels exceed 3/chan tolerance)\n", hardTotal);
    return 1;
}

/* ==================== 性能基准 ==================== */

/**
 * @brief      循环渲染全部场景 N 轮并报告吞吐。
 * @details    口径：每轮把 7 个场景各渲一遍（含每场景的 XImage 建立 +
 *             begin/end + 帧末读回），统计总耗时与每秒场景数。用于同一
 *             后端在不同优化档之间的相对比较（绝对值受机器/驱动影响，
 *             不作跨机判据）。取 3 轮中位以降噪。
 */
static int xpartyBench(int rounds)
{
    int round;
    double best = 0.0;
    double total = 0.0;
    int okRounds = 0;

    if (rounds <= 0) rounds = 30;
    printf("parity: bench %d rounds x %d scenes\n", rounds,
           (int)XParityScene_Count);
    for (round = 0; round < rounds; ++round)
    {
        int scene;
        double elapsed;
        int64_t begin = XDateTime_currentMSecsSinceEpoch();
        for (scene = 0; scene < (int)XParityScene_Count; ++scene)
        {
            XImage frame;
            if (!xpartyRenderScene((XParityScene)scene, &frame))
                return 2;
            XClassDeinit(&frame);
        }
        elapsed = (double)(XDateTime_currentMSecsSinceEpoch() - begin);
        total += elapsed;
        if (elapsed > 0.0)
        {
            double rate = (double)XParityScene_Count * 1000.0 / elapsed;
            if (rate > best) best = rate;
            /* 首轮含会话建立/纹理首次分配，不计入稳定态均值。 */
            if (round > 0) ++okRounds;
        }
    }
    printf("parity: total=%.0fms avg=%.2fms/round  best=%.1f scenes/s"
           "  (%d rounds)\n",
           total, total / (double)rounds, best, rounds);
    (void)okRounds;
    return 0;
}

/* ==================== 渲染到纹理自检 ==================== */

/**
 * @brief      渲染到纹理原语（P3）确定性自检。
 * @details    同一组图元画两遍：一遍直接画进会话渲染目标（参考），一遍
 *             先 pushRenderTarget 画进离屏颜色纹理、pop 后再用
 *             drawRenderTarget 回贴到会话目标。两份各 readback 成
 *             XImage，逐像素比对——覆盖 FBO/纹理创建、目标切换、视口/
 *             裁剪恢复、以及 drawRenderTarget 的 V 翻转与混合。
 *             仅 GPU 后端有意义：软件/未请求 GPU 时打印 SKIP 并返回 0。
 * @return     0=一致或跳过；1=存在超容忍差异/原语失败；2=会话创建失败。
 */
static int xpartySelfTestRenderTarget(void)
{
    XGpuRenderBackend* backend;
    XImage ref;
    XImage got;
    XRect rect;
    XGpuRenderTargetId target = XGPU_RENDER_TARGET_NONE;
    int x;
    int y;
    int hard = 0;
    int maxDelta = 0;
    if (!XGpuRenderBackend_requested())
    {
        printf("rt-test: SKIP (GPU backend not requested)\n");
        return 0;
    }
    backend = XGpuRenderBackend_create(XPARTY_WIDTH, XPARTY_HEIGHT);
    if (!backend)
    {
        fprintf(stderr, "rt-test: backend create failed\n");
        return 2;
    }
    if (!XGpuRenderBackend_supportsRenderTargets(backend))
    {
        printf("rt-test: SKIP (driver has no render-target procs)\n");
        XGpuRenderBackend_destroy(backend);
        return 0;
    }
    XImage_init_ex(&ref, XPARTY_WIDTH, XPARTY_HEIGHT, XImageFormat_ARGB32);
    XImage_init_ex(&got, XPARTY_WIDTH, XPARTY_HEIGHT, XImageFormat_ARGB32);

    /* 参考：直接画进会话目标。 */
    if (!XGpuRenderBackend_beginFrame(backend))
    {
        XGpuRenderBackend_destroy(backend);
        XClassDeinit(&ref);
        XClassDeinit(&got);
        return 2;
    }
    XGpuRenderBackend_clear(backend, 0xff202020u);
    rect.x = 8; rect.y = 8; rect.width = 60; rect.height = 40;
    XGpuRenderBackend_fillRect(backend, &rect, 0xff00c040u, 1.0f, true);
    rect.x = 30; rect.y = 24; rect.width = 50; rect.height = 44;
    XGpuRenderBackend_fillRect(backend, &rect, 0x804060ffu, 1.0f, true);
    {
        /* 读回前必须清帧内批；readback 自带冲批，直接调用即可。 */
        bool ok = XGpuRenderBackend_readback(backend, &ref);
        XGpuRenderBackend_endFrame(backend);
        if (!ok)
        {
            fprintf(stderr, "rt-test: reference readback failed\n");
            XGpuRenderBackend_destroy(backend);
            XClassDeinit(&ref);
            XClassDeinit(&got);
            return 1;
        }
    }

    /* 渲染到纹理：同样图元先画进离屏目标，pop 后回贴。 */
    if (!XGpuRenderBackend_beginFrame(backend))
    {
        XGpuRenderBackend_destroy(backend);
        XClassDeinit(&ref);
        XClassDeinit(&got);
        return 2;
    }
    XGpuRenderBackend_clear(backend, 0xff202020u);
    if (!XGpuRenderBackend_pushRenderTarget(backend, XPARTY_WIDTH,
                                            XPARTY_HEIGHT,
                                            XGPU_RENDER_TARGET_NONE, &target) ||
        target == XGPU_RENDER_TARGET_NONE)
    {
        fprintf(stderr, "rt-test: pushRenderTarget failed\n");
        XGpuRenderBackend_endFrame(backend);
        XGpuRenderBackend_destroy(backend);
        XClassDeinit(&ref);
        XClassDeinit(&got);
        return 1;
    }
    XGpuRenderBackend_clear(backend, 0xff202020u);
    rect.x = 8; rect.y = 8; rect.width = 60; rect.height = 40;
    XGpuRenderBackend_fillRect(backend, &rect, 0xff00c040u, 1.0f, true);
    rect.x = 30; rect.y = 24; rect.width = 50; rect.height = 44;
    XGpuRenderBackend_fillRect(backend, &rect, 0x804060ffu, 1.0f, true);
    if (!XGpuRenderBackend_popRenderTarget(backend))
    {
        fprintf(stderr, "rt-test: popRenderTarget failed\n");
        XGpuRenderBackend_endFrame(backend);
        XGpuRenderBackend_destroy(backend);
        XClassDeinit(&ref);
        XClassDeinit(&got);
        return 1;
    }
    if (!XGpuRenderBackend_drawRenderTarget(backend, target, 0, 0,
                                            XPARTY_WIDTH, XPARTY_HEIGHT,
                                            1.0f, true))
    {
        fprintf(stderr, "rt-test: drawRenderTarget failed\n");
        XGpuRenderBackend_endFrame(backend);
        XGpuRenderBackend_destroy(backend);
        XClassDeinit(&ref);
        XClassDeinit(&got);
        return 1;
    }
    {
        bool ok = XGpuRenderBackend_readback(backend, &got);
        XGpuRenderBackend_endFrame(backend);
        if (!ok)
        {
            fprintf(stderr, "rt-test: result readback failed\n");
            XGpuRenderBackend_destroy(backend);
            XClassDeinit(&ref);
            XClassDeinit(&got);
            return 1;
        }
    }

    for (y = 0; y < XPARTY_HEIGHT; ++y)
        for (x = 0; x < XPARTY_WIDTH; ++x)
        {
            uint32_t a = XImage_pixel(&ref, x, y);
            uint32_t b = XImage_pixel(&got, x, y);
            int delta = xpartyMaxChannelDelta(a, b);
            if (delta > maxDelta) maxDelta = delta;
            if (delta > 3) ++hard;
        }
    printf("rt-test: %s  hard(>3/chan)=%d  maxChanD=%d\n",
           hard == 0 ? "PASS" : "FAIL", hard, maxDelta);
    XGpuRenderBackend_destroy(backend);
    XClassDeinit(&ref);
    XClassDeinit(&got);
    return hard == 0 ? 0 : 1;
}

/* ==================== AA 线段（M4-1）感知对照 ==================== */

/**
 * @brief 渲染 AA 线段场景（M4-1 GPU 解析 AA 与 CPU 覆盖率的感知对照）。
 * @details 几何覆盖真实用法：斜线 1/2/3px、Flat/Square 端帽、以及一条
 *          由 16 段短斜线拼成的圆弧（曲线描边 = 逐段四边形，专锁接缝
 *          与顶点序）。轴向线用既有硬边快路（两后端逐位一致），也放两条
 *          作为对照。AA 提示开的软件后端走 CPU 4x4 面积采样；GPU 后端在
 *          XGPU_AA_LINE=1 时走驱动 drawAaLine（片元覆盖率），否则与
 *          CPU 同路径。
 */
static int xpartyRenderAaLines(XImage* out)
{
    XPainter painter;
    int i;
    if (!out) return 0;
    XImage_init_ex(out, XPARTY_WIDTH, XPARTY_HEIGHT, XImageFormat_ARGB32);
    XImage_fillRect(out, NULL, 0xffffffffu); /* 白底：覆盖度差最易观察。 */
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, out)) return 0;
    XPainter_setRenderHint(&painter, XPainterRenderHint_Antialiasing, true);
    XPainter_setPen(&painter, 0xff2f6fbfu);
    XPainter_setPenCapStyle(&painter, XPainterPenCapStyle_SquareCap);
    XPainter_setPenWidth(&painter, 1);
    XPainter_drawLine(&painter, 8, 8, 88, 60);
    XPainter_setPenWidth(&painter, 2);
    XPainter_drawLine(&painter, 6, 20, 86, 72);
    XPainter_setPenWidth(&painter, 3);
    XPainter_drawLine(&painter, 12, 84, 84, 12);
    XPainter_setPenWidth(&painter, 2);
    XPainter_setPenCapStyle(&painter, XPainterPenCapStyle_FlatCap);
    XPainter_drawLine(&painter, 10, 40, 86, 44);
    /* 圆弧描边：16 段短斜线（每段单独 AA 四边形，接缝/顶点序回归锁）。 */
    XPainter_setPenWidth(&painter, 1);
    for (i = 0; i < 16; ++i)
    {
        double a0 = (double)i * 3.14159265358979 * 2.0 / 16.0;
        double a1 = (double)(i + 1) * 3.14159265358979 * 2.0 / 16.0;
        int x0 = 48 + (int)(20.0 * (double)cos(a0) + 0.5);
        int y0 = 48 + (int)(20.0 * (double)sin(a0) + 0.5);
        int x1 = 48 + (int)(20.0 * (double)cos(a1) + 0.5);
        int y1 = 48 + (int)(20.0 * (double)sin(a1) + 0.5);
        XPainter_drawLine(&painter, x0, y0, x1, y1);
    }
    XPainter_setPenWidth(&painter, 1);
    XPainter_drawLine(&painter, 8, 92, 88, 92); /* 轴向对照（硬边快路）。 */
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    return 1;
}

/** @brief 落盘 AA 线段场景（parity 头格式，单场景）。 */
static int xpartyDumpAaLines(const char* path)
{
    FILE* fp;
    XParityHeader header;
    XImage frame;
    const uint8_t* bits;
    int bpl;
    int y;
    if (!path) return 2;
    if (!xpartyRenderAaLines(&frame))
    {
        fprintf(stderr, "aa-lines: render failed\n");
        return 2;
    }
    fp = fopen(path, "wb");
    if (!fp)
    {
        fprintf(stderr, "aa-lines: cannot open %s for write\n", path);
        XClassDeinit(&frame);
        return 2;
    }
    header.m_magic = XPARTY_MAGIC;
    header.m_version = XPARTY_VERSION;
    header.m_width = XPARTY_WIDTH;
    header.m_height = XPARTY_HEIGHT;
    header.m_sceneCount = 1u;
    if (fwrite(&header, sizeof(header), 1, fp) != 1)
    {
        fclose(fp);
        XClassDeinit(&frame);
        return 2;
    }
    bits = XImage_bits(&frame);
    bpl = XImage_bytesPerLine(&frame);
    if (!bits || bpl < XPARTY_WIDTH * 4)
    {
        fclose(fp);
        XClassDeinit(&frame);
        return 2;
    }
    for (y = 0; y < XPARTY_HEIGHT; ++y)
        fwrite(bits + (size_t)y * (size_t)bpl, 4, XPARTY_WIDTH, fp);
    fclose(fp);
    XClassDeinit(&frame);
    fprintf(stderr, "aa-lines: dumped to %s\n", path);
    return 0;
}

/**
 * @brief 感知口径对照（AA 算法替换专用）：逐通道 >limit 的像素占比上限。
 * @details AA 由 CPU 4x4 面积采样换成 GPU 解析覆盖率后，边缘像素值必然
 *          有量级差（逐位口径不再适用）。判据=「逐通道差 >XPARTY_PERC_CHAN
 *          （缺省 8）的像素占比 ≤ XPARTY_PERC_MAX_PCT（缺省 1.5%）」；
 *          逐位差(>0)、最大通道差同时打印供归因。默认阈值来自客户端
 *          首页实测（0.79%），门禁留 ~2 倍余量。
 */
static int xpartyDiffPerc(const char* pathA, const char* pathB)
{
    FILE* fa;
    FILE* fb;
    XParityHeader ha;
    XParityHeader hb;
    uint32_t* rowA;
    uint32_t* rowB;
    long long total = 0;
    long long diff = 0;
    long long over = 0;
    long long sumLumA = 0;
    long long sumLumB = 0;
    int maxd = 0;
    int limit = 8;
    double maxPct = 20.0; /* 边缘密集场景的算法差异占比上限（结构性兜底）。 */
    double meanMaxPct = 1.0; /* 平均墨量（亮度）相对差上限=真正的结构闸。 */
    int y;
    int x;
    {
        const char* v = getenv("XPARTY_PERC_CHAN");
        if (v && *v) limit = atoi(v);
        v = getenv("XPARTY_PERC_MAX_PCT");
        if (v && *v) maxPct = atof(v);
        v = getenv("XPARTY_MEAN_MAX_PCT");
        if (v && *v) meanMaxPct = atof(v);
    }
    fa = fopen(pathA, "rb");
    fb = fopen(pathB, "rb");
    if (!fa || !fb)
    {
        fprintf(stderr, "aa-lines: cannot open dumps\n");
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        return 2;
    }
    if (fread(&ha, sizeof(ha), 1, fa) != 1 ||
        fread(&hb, sizeof(hb), 1, fb) != 1 ||
        ha.m_magic != XPARTY_MAGIC || hb.m_magic != XPARTY_MAGIC ||
        ha.m_width != hb.m_width || ha.m_height != hb.m_height ||
        ha.m_sceneCount != hb.m_sceneCount || ha.m_sceneCount == 0 ||
        ha.m_width > 16384u || ha.m_height > 16384u)
    {
        fprintf(stderr, "aa-lines: bad dump header\n");
        fclose(fa);
        fclose(fb);
        return 2;
    }
    rowA = (uint32_t*)malloc((size_t)ha.m_width * 4u);
    rowB = (uint32_t*)malloc((size_t)ha.m_width * 4u);
    if (!rowA || !rowB)
    {
        free(rowA);
        free(rowB);
        fclose(fa);
        fclose(fb);
        return 2;
    }
    for (y = 0; y < (int)ha.m_height; ++y)
    {
        if (fread(rowA, 4, ha.m_width, fa) != ha.m_width ||
            fread(rowB, 4, ha.m_width, fb) != ha.m_width)
        {
            fprintf(stderr, "aa-lines: truncated pixel data\n");
            free(rowA);
            free(rowB);
            fclose(fa);
            fclose(fb);
            return 2;
        }
        for (x = 0; x < (int)ha.m_width; ++x)
        {
            uint32_t a = rowA[x];
            uint32_t b = rowB[x];
            int delta;
            /* 平均墨量（亮度，0..255）：AA 算法换实现时边缘覆盖分布
               会变，但「总墨量」必须守恒；描边断裂/整段缺失会令均值
               显著偏移（顶点序 bug 的缺失块即此）——比逐位占比更能抓
               结构性回归。 */
            sumLumA += (long long)(((a >> 16) & 0xffu) * 77u +
                                   ((a >> 8) & 0xffu) * 150u +
                                   (a & 0xffu) * 29u) >> 8;
            sumLumB += (long long)(((b >> 16) & 0xffu) * 77u +
                                   ((b >> 8) & 0xffu) * 150u +
                                   (b & 0xffu) * 29u) >> 8;
            ++total;
            if (a == b) continue;
            ++diff;
            delta = xpartyMaxChannelDelta(a, b);
            if (delta > maxd) maxd = delta;
            if (delta > limit) ++over;
        }
    }
    free(rowA);
    free(rowB);
    fclose(fa);
    fclose(fb);
    {
        double pct = total > 0 ? 100.0 * (double)over / (double)total : 0.0;
        double meanA = total > 0 ? (double)sumLumA / (double)total : 0.0;
        double meanB = total > 0 ? (double)sumLumB / (double)total : 0.0;
        double meanPct = 100.0 * fabs(meanA - meanB) / 255.0;
        int pass = pct <= maxPct && meanPct <= meanMaxPct;
        printf("aa-line-perc: total=%lld diffPx=%lld over%d=%lld (%.3f%%) "
               "maxChanD=%d meanLum=%.2f/%.2f dMean=%.3f%% "
               "percLimit=%.2f%% meanLimit=%.2f%% -> %s\n",
               total, diff, limit, over, pct, maxd, meanA, meanB, meanPct,
               maxPct, meanMaxPct, pass ? "PASS" : "FAIL");
        return pass ? 0 : 1;
    }
}

/* ==================== 入口 ==================== */

static void xpartyUsage(void)
{
    fprintf(stderr,
            "usage:\n"
            "  xgui_gpu_parity_test --dump <file.raw>\n"
            "  xgui_gpu_parity_test [--scene N[,M...]] --dump <file.raw>\n"
            "  xgui_gpu_parity_test --diff <a.raw> <b.raw>\n"
            "  xgui_gpu_parity_test --aa-lines-dump <file.raw>\n"
            "  xgui_gpu_parity_test --diff-perc <a.raw> <b.raw>\n"
            "  xgui_gpu_parity_test --bench [rounds]\n"
            "  xgui_gpu_parity_test --rt-test\n"
            "backend selected by XGUI_RENDER_BACKEND (gpu|vulkan|software)\n"
            "before the process starts (per-process one-shot cache)\n");
}

#if !XGPU_ON
int main(void)
{
    fprintf(stderr, "parity: XGPU_ON=0, skipped\n");
    return 0;
}
#else
int main(int argc, char** argv)
{
    /* 场景过滤（--scene N | N,M | 可重复下发）须在 --dump/--diff 之前
       生效，可与两者连用。 */
    while (argc >= 3 && strcmp(argv[1], "--scene") == 0)
    {
        const char* p = argv[2];
        while (*p && g_xpartySceneListCount <
                          (int)(sizeof(g_xpartySceneList) /
                                sizeof(g_xpartySceneList[0])))
        {
            char* endp = NULL;
            long v = strtol(p, &endp, 10);
            if (endp == p) break;
            if (v >= 0 && v < (long)XParityScene_Count)
                g_xpartySceneList[g_xpartySceneListCount++] = (int)v;
            p = (*endp == ',') ? endp + 1 : endp;
            if (*p == 0) break;
        }
        argv += 2;
        argc -= 2;
    }
    if (argc >= 3 && strcmp(argv[1], "--dump") == 0)
        return xpartyDump(argv[2]);
    if (argc >= 4 && strcmp(argv[1], "--diff") == 0)
        return xpartyDiff(argv[2], argv[3]);
    if (argc >= 3 && strcmp(argv[1], "--aa-lines-dump") == 0)
        return xpartyDumpAaLines(argv[2]);
    if (argc >= 4 && strcmp(argv[1], "--diff-perc") == 0)
        return xpartyDiffPerc(argv[2], argv[3]);
    if (argc >= 2 && strcmp(argv[1], "--bench") == 0)
        return xpartyBench(argc >= 3 ? atoi(argv[2]) : 30);
    if (argc >= 2 && strcmp(argv[1], "--rt-test") == 0)
        return xpartySelfTestRenderTarget();
    xpartyUsage();
    return 2;
}
#endif /* !XGPU_ON */
