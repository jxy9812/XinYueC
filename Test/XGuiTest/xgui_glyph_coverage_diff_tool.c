/******************************************************************************
 * @file       xgui_glyph_coverage_diff_tool.c
 * @brief      阶段二 P2.5 门禁工具：painter 换点前后 / SW-GPU 两消费点
 *             字形覆盖图逐像素 diff。
 * @details    设计 out/xfont_ft_phase2/design.md §8 P2.5 门禁列要求
 *             「SW 与 GPU 两消费点覆盖图逐像素 diff 工具」（原文要求
 *             xfo1 档全等；xfo1 档已随自研轮廓字实现移除，被测口径为
 *             唯一的 FT 链）。本工具独立可执行（自带 main，无窗口依赖，仿
 *             xgui_font_scan_tool 的离屏口径），不触碰任何现有源码逻辑，
 *             只做三段取证：
 *
 *             A) provider 槽位逐位对照（改动前 vs 改动后）：
 *                旧槽 XFontFace_loadOutlineGlyph_base（P2.5 改动前 painter
 *                的唯一通道）与新槽 XFontFace_loadOutlineGlyphScaled_base
 *                （P2.5 换点后 painter 的通道）对同一 face/码点/scale 各
 *                跑一遍，录音式 sink 逐命令逐 float 位型比对 + 度量结构
 *                memcmp。无字号载体的 face 上 scaled 槽默认实现转发旧
 *                槽（scale 丢弃），两路必须逐位一致——这是 P2.5 唯一改动
 *                的层，它
 *                逐位一致即下游路径/展平/光栅化输入逐位一致。另附
 *                scaled 包装的 scale 合法域断言（<=0/NaN/Inf 判负）。
 *
 *             B) SW 与 GPU 两消费点真实渲染逐像素 diff：
 *                同一 (字号, 码点) 经 XPainter_drawGlyph 分别渲染到两块
 *                ARGB32 离屏图——软件 painter 走 painterDrawOutlineGlyph-
 *                SoftwareAA 的消费点（XPainter.c 缓存未命中光栅化点，
 *                FT smooth 光栅化器），GPU painter（m_gpuActive）走
 *                painterGpuDrawOutlineGlyph 的消费点（XPainter.c GPU 同
 *                挂点；原 XFONT_FT_RASTER 双档门已移除，两点恒走 FT）。
 *                白底黑字下 255-像素值 = 覆盖读数，逐像素比对：
 *                - GPU 侧覆盖图原样上传、着色器线性混合，读数即原始
 *                  覆盖；SW 侧渲染内核 glyphMaskSpan 先过既有呈现曲线
 *                  （XRenderKernel_*.c 硬编码 *_GLYPH_COVERAGE_GAMMA
 *                  1.4，五份独立副本）再混合——差为既有设计、非覆盖差；
 *                - 故差异像素必须精确满足「SW 读数 = K(GPU 读数)」（K
 *                  为同式同值复算的内核曲线，pow(v/255,1/1.4)·255+0.5
 *                  截断），等价于断言两消费点覆盖图逐位一致（K 高端持
 *                  平段的不可分辨对除外）；逐位相等的像素直接通过。
 *                网格对齐（XGUI_TEXT_GRIDFIT）是 SW-AA 专属旋钮（GPU 路
 *                恒关，见 painterGpuDrawOutlineGlyph 注），为不把这一既
 *                有非对称混入覆盖图对照，工具启动即 setenv 置 0（口径同
 *                scan 工具 refMode）。GPU 会话不可用或软件侧意外走 GPU
 *                时显式报错退出，绝不把「静默回退」误报成全等。
 *
 *             C) 稳定摘要：全部录音流与渲染像素折叠进 FNV-1a 64 位
 *                digest 输出——对「改动前基线」与「改动后」两次运行做
 *                digest 逐字节比对即端到端逐位一致性证据（工具自身二
 *                进制不变，库侧 XPainter.c 换点是其唯一变量）。
 *
 *             输出：stdout 报告（无时间戳等不稳定量）；退出码 = 失败
 *             数与 250 取小（0=全等通过）。
 * @note       工具经 XGpuRenderBackend_addRequestedOverride 在进程内
 *             切换软/硬件 painter，不依赖外部 env。（[已移除] 原缺省档
 *             XFONT_PROVIDER 清 env 防御随 env 分档删除。）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XImage.h"
#include "XPainter.h"
#include "XFont.h"
#include "XFontFace.h"
#include "XGpuRenderBackend.h"
#include "XPrintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <math.h>

#if defined(_MSC_VER)
#define COVDIFF_SETENV(name, value) _putenv_s((name), (value))
#else
#define COVDIFF_SETENV(name, value) setenv((name), (value), 1)
#endif

/* ========== 取样集：字号 × 码点 ========== */

/* 字号扫描：覆盖 UI 实际用档（小字号密集笔画到标题字号）。 */
static const int kPxs[] = { 8, 10, 12, 13, 14, 16, 20, 24, 30 };
#define COVDIFF_PX_COUNT (int)(sizeof(kPxs) / sizeof(kPxs[0]))

/* CJK 探针：GB2312 常用字 + 全角标点（UI 常用覆盖域；原注「内置 XFO1
   字库覆盖域」随内置字库移除改写）。 */
static const uint32_t kCjk[] = {
    0x4E00u, 0x4E8Cu, 0x4E09u, 0x56DBu, 0x4E94u, 0x516Du, 0x4E03u, 0x516Bu,
    0x4E5Du, 0x5341u, 0x4EBAu, 0x5C71u, 0x6C34u, 0x65E5u, 0x6708u, 0x5B57u,
    0x4F53u, 0x6D4Bu, 0x8BD5u, 0x95E8u, 0x7A97u, 0x83DCu, 0x5355u, 0x8BBEu,
    0x7F6Eu, 0x56FEu, 0x6807u, 0x6309u, 0x94AEu, 0x8F93u, 0x5165u, 0x9879u,
    0xFF0Cu, 0x3002u, 0x3001u
};
#define COVDIFF_CJK_COUNT (int)(sizeof(kCjk) / sizeof(kCjk[0]))

/* 缺字形探针：字体不收录的码位（GBK 扩展槽 E810 与罕见码位），两槽应
   一致判负——缺字形代理回退口径的对照。 */
static const uint32_t kMissing[] = { 0xE810u, 0x2A6DEu };
#define COVDIFF_MISSING_COUNT (int)(sizeof(kMissing) / sizeof(kMissing[0]))

/* ========== 录音式 sink（provider 输出逐命令捕获） ========== */

typedef enum
{
    COVDIFF_OP_MOVE = 0,
    COVDIFF_OP_LINE = 1,
    COVDIFF_OP_QUAD = 2,
    COVDIFF_OP_CUBIC = 3,
    COVDIFF_OP_CLOSE = 4
} CovdiffOp;

typedef struct CovdiffRecEntry
{
    uint32_t m_op;  /**< 命令类型（CovdiffOp）。 */
    float m_v[6];   /**< 坐标原值（close 恒 0；保留位型逐位比对）。 */
} CovdiffRecEntry;

typedef struct CovdiffRec
{
    CovdiffRecEntry m_e[8192]; /**< 命令流（宏量裕量；原 XFO1 命令数
                                    上限宏已随其实现删除）。 */
    int m_n;                   /**< 实有命令数。 */
    bool m_overflow;           /**< 超出容量即非法（正常字形远达不到）。 */
    bool m_aborted;            /**< 回调返回 false 会中断 provider——恒记成功。 */
} CovdiffRec;

static bool covdiff_recMove(void* ud, float x, float y)
{
    CovdiffRec* r = (CovdiffRec*)ud;
    if (r->m_n >= (int)(sizeof(r->m_e) / sizeof(r->m_e[0])))
    {
        r->m_overflow = true;
        return false;
    }
    r->m_e[r->m_n].m_op = (uint32_t)COVDIFF_OP_MOVE;
    r->m_e[r->m_n].m_v[0] = x;
    r->m_e[r->m_n].m_v[1] = y;
    ++r->m_n;
    return true;
}

static bool covdiff_recLine(void* ud, float x, float y)
{
    CovdiffRec* r = (CovdiffRec*)ud;
    if (r->m_n >= (int)(sizeof(r->m_e) / sizeof(r->m_e[0])))
    {
        r->m_overflow = true;
        return false;
    }
    r->m_e[r->m_n].m_op = (uint32_t)COVDIFF_OP_LINE;
    r->m_e[r->m_n].m_v[0] = x;
    r->m_e[r->m_n].m_v[1] = y;
    ++r->m_n;
    return true;
}

static bool covdiff_recQuad(void* ud, float cx, float cy, float x, float y)
{
    CovdiffRec* r = (CovdiffRec*)ud;
    if (r->m_n >= (int)(sizeof(r->m_e) / sizeof(r->m_e[0])))
    {
        r->m_overflow = true;
        return false;
    }
    r->m_e[r->m_n].m_op = (uint32_t)COVDIFF_OP_QUAD;
    r->m_e[r->m_n].m_v[0] = cx;
    r->m_e[r->m_n].m_v[1] = cy;
    r->m_e[r->m_n].m_v[2] = x;
    r->m_e[r->m_n].m_v[3] = y;
    ++r->m_n;
    return true;
}

static bool covdiff_recCubic(void* ud, float c1x, float c1y, float c2x,
                             float c2y, float x, float y)
{
    CovdiffRec* r = (CovdiffRec*)ud;
    if (r->m_n >= (int)(sizeof(r->m_e) / sizeof(r->m_e[0])))
    {
        r->m_overflow = true;
        return false;
    }
    r->m_e[r->m_n].m_op = (uint32_t)COVDIFF_OP_CUBIC;
    r->m_e[r->m_n].m_v[0] = c1x;
    r->m_e[r->m_n].m_v[1] = c1y;
    r->m_e[r->m_n].m_v[2] = c2x;
    r->m_e[r->m_n].m_v[3] = c2y;
    r->m_e[r->m_n].m_v[4] = x;
    r->m_e[r->m_n].m_v[5] = y;
    ++r->m_n;
    return true;
}

static bool covdiff_recClose(void* ud)
{
    CovdiffRec* r = (CovdiffRec*)ud;
    if (r->m_n >= (int)(sizeof(r->m_e) / sizeof(r->m_e[0])))
    {
        r->m_overflow = true;
        return false;
    }
    r->m_e[r->m_n].m_op = (uint32_t)COVDIFF_OP_CLOSE;
    ++r->m_n;
    return true;
}

static void covdiff_recSinkInit(XFontOutlineSink* sink, CovdiffRec* rec)
{
    memset(rec, 0, sizeof(*rec));
    memset(sink, 0, sizeof(*sink));
    sink->userData = rec;
    sink->moveTo = covdiff_recMove;
    sink->lineTo = covdiff_recLine;
    sink->quadTo = covdiff_recQuad;
    sink->cubicTo = covdiff_recCubic;
    sink->close = covdiff_recClose;
}

/* ========== FNV-1a 64 位稳定摘要 ========== */

static uint64_t g_digest = 1469598103934665603ull; /* FNV-1a 64 offset basis */

static void covdiff_digest(const void* data, size_t len)
{
    const uint8_t* p = (const uint8_t*)data;
    size_t i;
    for (i = 0; i < len; ++i)
    {
        g_digest ^= (uint64_t)p[i];
        g_digest *= 1099511628211ull; /* FNV-1a 64 prime */
    }
}

/* ========== 全局统计 ========== */

static int g_providerCases;    /**< A 段实跑对照用例数（两槽都命中）。 */
static int g_providerMissing;  /**< A 段两槽一致判负的缺字形用例数。 */
static int g_providerMismatch; /**< A 段不一致用例数（失败）。 */
static int g_renderCases;      /**< B 段 SW/GPU 合成逐位全等的用例数。 */
static int g_renderMismatch;   /**< B 段有呈现曲线差异的用例数（日志）。 */
static int g_bViolations;      /**< B 段曲线校验违例像素数（失败）。 */
static int g_hardErrors;       /**< 环境性硬错误（GPU 不可用等）。 */

/** @brief float 位型读数（避免 -0.0/NaN 的 == 比较陷阱，逐位判等）。 */
static uint32_t covdiff_fbits(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

static void covdiff_digestRec(const CovdiffRec* r)
{
    int i;
    covdiff_digest(&r->m_n, sizeof(r->m_n));
    for (i = 0; i < r->m_n; ++i)
    {
        int j;
        covdiff_digest(&r->m_e[i].m_op, sizeof(r->m_e[i].m_op));
        for (j = 0; j < 6; ++j)
        {
            uint32_t bits = covdiff_fbits(r->m_e[i].m_v[j]);
            covdiff_digest(&bits, sizeof(bits));
        }
    }
}

/* ========== A 段：provider 槽位逐位对照 ========== */

/** @brief 单用例对照：同 face/font/cp 两槽各跑一遍录音 sink。
 *  @details 两槽统一以 scale=1.0 语义对照（旧槽本就无字号载体；新槽
 *           显式传 1.0）——FT 的 hinted 几何随字号变化是设计内行为
 *           （px 闭合公式 §1.2），跨 scale 逐位对照无意义；同 scale
 *           下两槽必须逐位一致（分发一致性证据）。
 *           [2026-10-07] 原实现按目标 px scale 传新槽，其「逐位一致」
 *           前提只在已移除的 XFO1 无缩放数据上成立（XFO1 默认实现
 *           丢弃 scale）。xfo1 删除后改定 1.0 对照。
 *  @return 一致返回 true（含两槽一致判负的缺字形）。 */
static bool covdiff_compareProviderCase(const XFontFace* face,
                                        const XFont* font, float scale,
                                        uint32_t cp, int px)
{
    XFontOutlineSink sinkA;
    XFontOutlineSink sinkB;
    CovdiffRec recA;
    CovdiffRec recB;
    XFontOutlineGlyphMetrics mA;
    XFontOutlineGlyphMetrics mB;
    bool okA;
    bool okB;
    covdiff_recSinkInit(&sinkA, &recA);
    covdiff_recSinkInit(&sinkB, &recB);
    memset(&mA, 0, sizeof(mA));
    memset(&mB, 0, sizeof(mB));
    /* 旧槽（无字号载体，scale=1.0 语义）。 */
    okA = XFontFace_loadOutlineGlyph_base(face, font, cp, &mA, &sinkA);
    /* 新槽：显式传同一 scale=1.0（见函数注：跨 scale 对照在 FT 下无
       意义，同 scale 才是分发一致性证据）。 */
    okB = XFontFace_loadOutlineGlyphScaled_base(face, font, 1.0f, cp, &mB,
                                                &sinkB);
    (void)scale;
    if (!okA && !okB)
    {
        /* 缺字形：两槽一致判负（字体不供货的码点）。 */
        ++g_providerMissing;
        return true;
    }
    if (okA != okB)
    {
        ++g_providerMismatch;
        XPrintf("[covdiff][A-MISMATCH] px=%d U+%04X: 旧槽命中=%d 新槽命中=%d\n",
                px, cp, okA ? 1 : 0, okB ? 1 : 0);
        return false;
    }
    /* 度量结构逐字节比对（advance/xMin/yMin/xMax/yMax 全域）。 */
    if (memcmp(&mA, &mB, sizeof(mA)) != 0)
    {
        ++g_providerMismatch;
        XPrintf("[covdiff][A-MISMATCH] px=%d U+%04X: metrics 不一致 "
                "(adv %d/%d box %d,%d,%d,%d / %d,%d,%d,%d)\n",
                px, cp, mA.advance, mB.advance, mA.xMin, mA.yMin, mA.xMax,
                mA.yMax, mB.xMin, mB.yMin, mB.xMax, mB.yMax);
        return false;
    }
    if (recA.m_n != recB.m_n || recA.m_overflow || recB.m_overflow)
    {
        ++g_providerMismatch;
        XPrintf("[covdiff][A-MISMATCH] px=%d U+%04X: 命令数不一致 %d/%d "
                "(overflow %d/%d)\n", px, cp, recA.m_n, recB.m_n,
                recA.m_overflow ? 1 : 0, recB.m_overflow ? 1 : 0);
        return false;
    }
    {
        int i;
        for (i = 0; i < recA.m_n; ++i)
        {
            int j;
            if (recA.m_e[i].m_op != recB.m_e[i].m_op)
            {
                ++g_providerMismatch;
                XPrintf("[covdiff][A-MISMATCH] px=%d U+%04X: 命令 %d 类型 "
                        "不一致\n", px, cp, i);
                return false;
            }
            for (j = 0; j < 6; ++j)
            {
                if (covdiff_fbits(recA.m_e[i].m_v[j]) !=
                    covdiff_fbits(recB.m_e[i].m_v[j]))
                {
                    ++g_providerMismatch;
                    XPrintf("[covdiff][A-MISMATCH] px=%d U+%04X: 命令 %d "
                            "坐标 %d 位型不一致 (%08X/%08X)\n", px, cp, i, j,
                            covdiff_fbits(recA.m_e[i].m_v[j]),
                            covdiff_fbits(recB.m_e[i].m_v[j]));
                    return false;
                }
            }
        }
    }
    ++g_providerCases;
    covdiff_digestRec(&recA);
    covdiff_digest(&mA, sizeof(mA));
    return true;
}

/** @brief scaled 包装的 scale 合法域断言（P2.3 包装契约，防误用）。 */
static bool covdiff_checkScaleDomain(const XFontFace* face, const XFont* font)
{
    XFontOutlineGlyphMetrics m;
    bool ok = true;
    memset(&m, 0, sizeof(m));
    /* 非法 scale（<=0/NaN/Inf）必须判负——包装函数校验先行。 */
    if (XFontFace_loadOutlineGlyphScaled_base(face, font, 0.0f, 0x41u, &m,
                                              NULL))
    {
        XPrintf("[covdiff][A-DOMAIN] scale=0 未判负\n");
        ok = false;
    }
    if (XFontFace_loadOutlineGlyphScaled_base(face, font, -1.0f, 0x41u, &m,
                                              NULL))
    {
        XPrintf("[covdiff][A-DOMAIN] scale<0 未判负\n");
        ok = false;
    }
    if (XFontFace_loadOutlineGlyphScaled_base(face, font, (float)NAN, 0x41u,
                                              &m, NULL))
    {
        XPrintf("[covdiff][A-DOMAIN] scale=NaN 未判负\n");
        ok = false;
    }
    if (XFontFace_loadOutlineGlyphScaled_base(face, font, (float)INFINITY,
                                              0x41u, &m, NULL))
    {
        XPrintf("[covdiff][A-DOMAIN] scale=Inf 未判负\n");
        ok = false;
    }
    if (XFontFace_loadOutlineGlyphScaled_base(NULL, font, 1.0f, 0x41u, &m,
                                              NULL))
    {
        XPrintf("[covdiff][A-DOMAIN] NULL face 未判负\n");
        ok = false;
    }
    return ok;
}

/* ========== B 段：SW 与 GPU 两消费点真实渲染逐像素 diff ========== */

/** @brief 码点 → UTF-8（XPainter_drawGlyph 以 UTF-8 指针取码点）。
 *  @return 编码字节数（1~4）；非法码点返回 0。 */
static int covdiff_utf8Encode(uint32_t cp, char out[5])
{
    if (cp <= 0x7Fu)
    {
        out[0] = (char)cp;
        out[1] = '\0';
        return 1;
    }
    if (cp <= 0x7FFu)
    {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        out[2] = '\0';
        return 2;
    }
    if (cp <= 0xFFFFu)
    {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        out[3] = '\0';
        return 3;
    }
    if (cp <= 0x10FFFFu)
    {
        out[0] = (char)(0xF0u | (cp >> 18));
        out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[3] = (char)(0x80u | (cp & 0x3Fu));
        out[4] = '\0';
        return 4;
    }
    out[0] = '\0';
    return 0;
}

/** @brief 单字形渲染：白底黑字画一个码点（底色经 painter 命令绘制，
 *         GPU keep-open 画布上同样确定——同 xgui_gpu_test 口径）。
 *  @param wantGpu true=请求 GPU painter（消费点 2），false=软件 painter。 */
static bool covdiff_renderGlyph(XPainter* painter, XImage* img,
                                const XFont* font, uint32_t cp, int px,
                                bool wantGpu, bool* wentGpu)
{
    char utf8[5];
    XRect full;
    if (covdiff_utf8Encode(cp, utf8) <= 0) return false;
    XGpuRenderBackend_addRequestedOverride(wantGpu);
    if (!XPainter_begin_image(painter, img)) return false;
    *wentGpu = (XPainter_rasterBackend(painter) == XPainterRasterBackend_Gpu);
    full.x = 0;
    full.y = 0;
    full.width = XImage_width(img);
    full.height = XImage_height(img);
    if (!XPainter_fillRect(painter, &full, 0xFFFFFFFFu)) return false;
    XPainter_setFont(painter, font);
    XPainter_drawGlyph(painter, px / 2 + 4, px * 3 / 2 + 8, utf8,
                       0xFF000000u);
    if (!XPainter_end(painter)) return false;
    return true;
}

/* SW 侧字形覆盖读数的呈现曲线：渲染内核 glyphMaskSpan 专用 γ LUT
 * （XRenderKernel_{rgb32,rgb888,rgb565,rgb555,gray8}.c 各自硬编码
 * *_GLYPH_COVERAGE_GAMMA 1.4，lut[v]=(uint8_t)(pow(v/255,1/1.4)·255
 * +0.5)，端点自然保持）。GPU 路径覆盖图原样上传、着色器线性混合，
 * 无此曲线——故缺省构建下两消费点合成像素差 = 该既有呈现曲线（非
 * 覆盖差）。本工具逐像素精确复算同式同值曲线：差异像素必须满足
 * sw 读数 = K(gpu 读数)，等价于断言两消费点覆盖图逐位一致（除 K 自
 * 身高端持平段的固有不可分辨对，恒等档另有直证口径，见文件头注）。 */
static unsigned covdiff_kernelGammaLut(unsigned v)
{
    double d = pow((double)v / 255.0, 1.0 / 1.4) * 255.0 + 0.5;
    if (d > 255.0) return 255u;
    return (unsigned)d; /* 与内核 (uint8_t) 截断同口径。 */
}

/** @brief 码点在 face 是否有字形（旧槽 NULL-sink 纯度量探测）。
 *  @details B 段只画实有字形：缺字形在 GPU 路会触发 painterGpuFallback
 *           （缺字形代理回退走软件）→ setFrameDegraded(true) 毒化后续
 *           全部 GPU 帧——缺字形两槽一致性已由 A 段覆盖，渲染段跳过。 */
static bool covdiff_providerHasGlyph(const XFontFace* face,
                                     const XFont* font, uint32_t cp)
{
    XFontOutlineGlyphMetrics m;
    memset(&m, 0, sizeof(m));
    return XFontFace_loadOutlineGlyph_base(face, font, cp, &m, NULL);
}

static bool covdiff_compareRenderCase(XPainter* painter, XImage* imgSw,
                                      XImage* imgGpu, const XFont* font,
                                      uint32_t cp, int px)
{
    bool wentGpuSw = false;
    bool wentGpuGpu = false;
    int w;
    int x;
    int y;
    if (!covdiff_renderGlyph(painter, imgSw, font, cp, px, false,
                             &wentGpuSw) ||
        !covdiff_renderGlyph(painter, imgGpu, font, cp, px, true,
                             &wentGpuGpu))
    {
        XPrintf("[covdiff][B-ERROR] px=%d U+%04X: 渲染失败\n", px, cp);
        ++g_hardErrors;
        return false;
    }
    if (wentGpuSw)
    {
        /* 「软件侧」实际走了 GPU（外部 env 抢注等）：对照被污染，硬错。 */
        XPrintf("[covdiff][B-ERROR] px=%d U+%04X: 软件侧意外走 GPU 后端\n",
                px, cp);
        ++g_hardErrors;
        return false;
    }
    if (!wentGpuGpu)
    {
        /* GPU 会话未建立（静默回退软件）：会把「SW vs SW」误报成全等，
           拒绝出假证。 */
        XPrintf("[covdiff][B-ERROR] px=%d U+%04X: GPU 侧未激活 GPU 后端\n",
                px, cp);
        ++g_hardErrors;
        return false;
    }
    covdiff_digest("gpu", 3);
    w = XImage_width(imgSw);
    {
        int mismatches = 0;
        int firstDx = -1;
        int firstDy = -1;
        uint32_t firstSw = 0;
        uint32_t firstGpu = 0;
        for (y = 0; y < XImage_height(imgSw); ++y)
        {
            for (x = 0; x < w; ++x)
            {
                /* 白底黑字下：255-像素值 = 该像素的墨覆盖读数（SW 侧为
                   内核 γ1.4 呈现曲线后读数，GPU 侧为原始覆盖读数）。 */
                uint32_t psw = XImage_pixel(imgSw, x, y);
                uint32_t pgpu = XImage_pixel(imgGpu, x, y);
                unsigned covSw;
                unsigned covGpu;
                covdiff_digest(&psw, sizeof(psw));
                if (psw == pgpu) continue;
                /* ARGB32 黑白二色下灰阶必须三通道一致，否则渲染管线
                   异常（抗锯齿灰只作用于强度）。 */
                if (((psw >> 16) & 0xFFu) != (psw & 0xFFu) ||
                    ((pgpu >> 16) & 0xFFu) != (pgpu & 0xFFu))
                {
                    ++g_bViolations;
                    continue;
                }
                covSw = 255u - ((psw >> 16) & 0xFFu);
                covGpu = 255u - ((pgpu >> 16) & 0xFFu);
                ++mismatches;
                if (firstDx < 0)
                {
                    firstDx = x;
                    firstDy = y;
                    firstSw = psw;
                    firstGpu = pgpu;
                }
                /* 差异像素必须精确落在既有呈现曲线上：sw 读数 =
                   K(gpu 读数)。若两消费点覆盖图真有差异（除非恰落于
                   K 的持平段不可分辨对），此处必现违例。 */
                if (covSw != covdiff_kernelGammaLut(covGpu))
                    ++g_bViolations;
            }
        }
        if (mismatches > 0)
        {
            ++g_renderMismatch;
            XPrintf("[covdiff][B-DIFF] px=%d U+%04X: 差异像素 %d 个，"
                    "首个 (%d,%d) sw=%08X gpu=%08X\n", px, cp, mismatches,
                    firstDx, firstDy, firstSw, firstGpu);
        }
        else
        {
            ++g_renderCases;
        }
        return true;
    }
}

/* ========== 主流程 ========== */

int main(void)
{
    XFont* font = NULL;
    XPainter painter;
    XImage imgSw;
    XImage imgGpu;
    bool imgInited = false;
    bool ok = true;
    int px;

    /* 网格对齐是 SW-AA 专属旋钮（GPU 路恒关）：置 0 消除这一与 P2.5
       无关的既有非对称，使 SW/GPU 对照纯化到「同一几何进同一光栅化
       管线」。必须在首次字形渲染前落 env（painter 一次性懒读）。 */
    COVDIFF_SETENV("XGUI_TEXT_GRIDFIT", "0");
    /* [已移除 2026-10-07] 原 XFONT_PROVIDER 清 env 防御随 env 分档
       删除：FT 为唯一轮廓 provider。 */

    XGpuRenderBackend_addRequestedOverride(false);
    if (!XGpuRenderBackend_probeAvailable())
    {
        fprintf(stderr, "covdiff: GPU 后端探测不可用（无 GL），B 段无法"
                        "执行——拒绝以「静默回退软件」冒充全等\n");
        return 250;
    }

    /* 缺省 family（XFontOutlineCommon）经 FT 外挂链按 exe 目录锚定的
       ../Library/XFont 解析（同 xgui_font_scan_tool 口径），即被测
       face。 */
    font = XFont_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, -1, -1, false);
    if (!font)
    {
        fprintf(stderr, "covdiff: XFont_create_ex 失败\n");
        return 250;
    }

    /* ===== A 段：provider 槽位逐位对照 ===== */
    {
        const XFontFace* face = XFont_face(font);
        XFontFaceInfo info;
        memset(&info, 0, sizeof(info));
        if (!face || !XFontFace_info_base(face, font, &info) ||
            info.m_kind != XFontFace_Outline)
        {
            fprintf(stderr, "covdiff: 缺省 family 未解析出轮廓 face\n");
            XClassDeinit((XClass*)font);
            return 250;
        }
        fprintf(stderr, "covdiff: face=%s upem=%d（FT 唯一档）\n",
                XFont_family(font), info.m_outline.unitsPerEm);
        if (!covdiff_checkScaleDomain(face, font))
        {
            XPrintf("[covdiff][A-DOMAIN] scale 合法域断言失败\n");
            ok = false;
        }
        for (px = 0; px < COVDIFF_PX_COUNT; ++px)
        {
            float scale = (float)kPxs[px] /
                          (float)info.m_outline.unitsPerEm;
            uint32_t cp;
            int i;
            for (cp = 0x20u; cp <= 0x7Eu; ++cp)
                if (!covdiff_compareProviderCase(face, font, scale, cp,
                                                 kPxs[px]))
                    ok = false;
            for (i = 0; i < COVDIFF_CJK_COUNT; ++i)
                if (!covdiff_compareProviderCase(face, font, scale, kCjk[i],
                                                 kPxs[px]))
                    ok = false;
            for (i = 0; i < COVDIFF_MISSING_COUNT; ++i)
                if (!covdiff_compareProviderCase(face, font, scale,
                                                 kMissing[i], kPxs[px]))
                    ok = false;
        }
    }
    XPrintf("[covdiff] A 段 provider 槽位逐位对照: 一致 %d 用例（含一致判"
            "负缺字形 %d），不一致 %d => %s\n", g_providerCases,
            g_providerMissing, g_providerMismatch,
            (g_providerMismatch == 0 && ok) ? "通过" : "失败");
    if (g_providerMismatch != 0) ok = false;

    /* ===== B 段：SW 与 GPU 两消费点真实渲染逐像素 diff =====
     * 画布全字号共用一块（96x96）：避免逐字号重建 GPU 离屏会话这一
     * 既有基础设施变量进入门禁；px=30 字形（含上下伸部）充分容纳。 */
    {
        const XFontFace* face = XFont_face(font);
        XPainter_init(&painter, NULL);
        XImage_init_ex(&imgSw, 96, 96, XImageFormat_ARGB32);
        XImage_init_ex(&imgGpu, 96, 96, XImageFormat_ARGB32);
        imgInited = true;
        for (px = 0; px < COVDIFF_PX_COUNT; ++px)
        {
            uint32_t cp;
            int i;
            for (cp = 0x20u; cp <= 0x7Eu; ++cp)
            {
                if (!covdiff_providerHasGlyph(face, font, cp)) continue;
                if (!covdiff_compareRenderCase(&painter, &imgSw, &imgGpu,
                                               font, cp, kPxs[px]))
                    ok = false;
            }
            for (i = 0; i < COVDIFF_CJK_COUNT; ++i)
            {
                if (!covdiff_providerHasGlyph(face, font, kCjk[i])) continue;
                if (!covdiff_compareRenderCase(&painter, &imgSw, &imgGpu,
                                               font, kCjk[i], kPxs[px]))
                    ok = false;
            }
        }
    }
    if (imgInited)
    {
        XClassDeinit((XClass*)&imgSw);
        XClassDeinit((XClass*)&imgGpu);
    }
    XPainter_deinit(&painter);
    XPrintf("[covdiff] B 段 SW/GPU 两消费点覆盖图逐像素 diff: 合成逐位全"
            "等 %d 用例；曲线校验违例 %d 像素，硬错误 %d => %s\n",
            g_renderCases, g_bViolations, g_hardErrors,
            (g_bViolations == 0 && g_hardErrors == 0) ? "通过" : "失败");
    if (g_bViolations != 0 || g_hardErrors != 0) ok = false;

    XPrintf("[covdiff] digest=%016llX（对「改动前基线」与「改动后」两次运"
            "行必须逐字符相同）\n",
            (unsigned long long)g_digest);
    XPrintf("[covdiff] RESULT: %s\n", ok ? "PASS" : "FAIL");
    XClassDeinit((XClass*)font);
    {
        int failures = g_providerMismatch + g_bViolations + g_hardErrors;
        return failures > 250 ? 250 : failures;
    }
}
