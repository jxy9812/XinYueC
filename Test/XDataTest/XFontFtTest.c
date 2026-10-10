/**
 * @file       XFontFtTest.c
 * @brief      XFontFt（阶段二 FT 文件后端）专项测试。
 * @details    设计 §8 P2.2 门禁：注入程序化构造的最小 TrueType（7 表：
 *             cmap/glyf/head/hhea/hmtx/loca/maxp，文件不入库，§7.1 允许
 *             程序化构造），覆盖——
 *               1) px 闭合公式两档（FT 合法域上界 upem=16384 与常规
 *                  upem=1000；捆绑 FreeType 2.13 对 upem>16384 在
 *                  sfobjs.c:906-912 直接 Invalid_Table 拒载，65535 档
 *                  按解析失败锁存路径实测，见 t6）；
 *               2) 缺字形 false；
 *               3) 家族槽满回退（第 5 家族判失败，槽内家族不受扰）；
 *               4) 损坏字体家族锁存（同家族换好文件仍拒；缺文件不锁存
 *                  的时序自愈对照）；
 *               5) 字号交错循环下家族 face 常驻（遗留-1 P0 重构不变量：
 *                  字号切换零 face 重建，计数器断言；ASan 构建下 blob/
 *                  face 生命周期错位仍实爆）；
 *               5b) 小字号 hint 分档（遗留-2）：px<XFONT_FT_HINT_MIN_PX
 *                  走 NO_HINTING 只落 load_flags——noHint 计数子集、跨
 *                  阈值零 face 重建（参考 face 加载旗标按 px 镜像同档，
 *                  xfontft_refLoadFlags）；
 *               6) FT_Outline_Decompose→XFontOutlineSink 回路（五回调
 *                  计数 + 设计单位坐标 × scale 与 FT 参考 26.6 逐点比对）。
 *             P2.3 追加：XFontOutlineFace copy/move 后 kind/后端分支
 *               （m_ft）逐位保持断言（face_copy_move_branches；原
 *               provider/file 两档中的 file 档已随 XFO1 实现删除），
 *               FT 档另加拷贝/移动体经新旧两槽位包装函数的分发行为级
 *               反证（ft_face_copy_move）。
 *             闭合公式参考值由测试独立经 FT 装载同一字体实测取得（只取
 *             26.6 原始量；复用 XFontFt_library 是为遵守"全工程零
 *             FT_Done_FreeType 调用点"红线，测试不自有库）。
 */
#include "XFontFtTest.h"
#if DEMOTEST
#include "XTestMenu.h"
#include "XAction.h"
#include "XVariant.h"
#endif
#include "XFontFt.h"
#include "XFont.h"
#include "XFontFace.h"
#include "XFontOutlineFace.h"
#include "XFontFace_Protected.h"
#include "XFile.h"
#include "XByteArray.h"
#include "XCoreApplication.h"
#include "XPrintf.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if XFONT_FT_FACE_ON
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_IMAGE_H
#endif

/* ==================== 测试辅助宏（照 XVariantTest.c 模式） ==================== */
#define TEST_PASS(name) XPrintf("[PASS] %s\n", name)
#define TEST_FAIL(name, reason) XPrintf("[FAIL] %s: %s\n", name, reason)

/* ==================== 最小 TrueType 构造器 ==================== */
/* 大端写入；表目录 12 + 7*16 = 124 字节，表体 4 字节对齐。FreeType 装载
   不校验表校验和（全 0 合法），head 魔数 0x5F0F3CF5 为必需。 */
#define XFONTFT_TTF_CAP 2048u

static void xfontft_wr16(unsigned char* p, unsigned v)
{
    p[0] = (unsigned char)((v >> 8) & 0xFFu);
    p[1] = (unsigned char)(v & 0xFFu);
}

static void xfontft_wr16s(unsigned char* p, int v)
{
    xfontft_wr16(p, (unsigned)v & 0xFFFFu);
}

static void xfontft_wr32(unsigned char* p, unsigned long v)
{
    p[0] = (unsigned char)((v >> 24) & 0xFFu);
    p[1] = (unsigned char)((v >> 16) & 0xFFu);
    p[2] = (unsigned char)((v >> 8) & 0xFFu);
    p[3] = (unsigned char)(v & 0xFFu);
}

/**
 * @brief 构造含单三角形轮廓（(0,0)-(triW,0)-(0,triH)）与单 cmap 段
 *        （'A'=0x41 → glyph 1，其余全部缺字）的最小 TrueType。
 * @param upem   head.unitsPerEm（注意：捆绑 FT 仅接受 [16,16384]）。
 * @param triW/triH 三角形直角边（font units，int16 域内）。
 * @return 写入字节数；0 = 缓冲不足。
 */
static size_t xfontft_build_ttf(unsigned upem, unsigned triW, unsigned triH,
                                unsigned char* buf, size_t cap)
{
    const int numTables = 7;
    size_t off = (size_t)(12u + (unsigned)numTables * 16u);
    size_t cmapOff, glyfOff, headOff, hheaOff, hmtxOff, locaOff, maxpOff;
    size_t glyfLen = 30u; /* 10 头 + 2 endPts + 2 instr + 3 flags + 12 坐标，补 1 对齐 */
    int i;
    if (cap < off + 256u)
        return 0u;
    memset(buf, 0, cap);
    /* sfnt 头：版本 1.0 + 表目录参数（FT 不校验，按规范填正）。 */
    xfontft_wr32(buf, 0x00010000ul);
    xfontft_wr16(buf + 4, (unsigned)numTables);
    xfontft_wr16(buf + 6, 64u);   /* searchRange = 16 * 4（最大 2^2 次幂表数） */
    xfontft_wr16(buf + 8, 2u);    /* entrySelector */
    xfontft_wr16(buf + 10, (unsigned)numTables * 16u - 64u); /* rangeShift */
    off = (off + 3u) & ~(size_t)3u;
    /* cmap（44B）：头 12B + format4 32B，platform 3/encoding 1。 */
    cmapOff = off;
    xfontft_wr16(buf + cmapOff, 0u);            /* version */
    xfontft_wr16(buf + cmapOff + 2, 1u);        /* numTables */
    xfontft_wr16(buf + cmapOff + 4, 3u);        /* platformID */
    xfontft_wr16(buf + cmapOff + 6, 1u);        /* encodingID */
    xfontft_wr32(buf + cmapOff + 8, 12ul);      /* subtable offset */
    {
        unsigned char* st = buf + cmapOff + 12;
        xfontft_wr16(st, 4u);                   /* format */
        xfontft_wr16(st + 2, 32u);              /* length */
        xfontft_wr16(st + 4, 0u);               /* language */
        xfontft_wr16(st + 6, 4u);               /* segCountX2（2 段） */
        xfontft_wr16(st + 8, 4u);               /* searchRange */
        xfontft_wr16(st + 10, 1u);              /* entrySelector */
        xfontft_wr16(st + 12, 0u);              /* rangeShift */
        xfontft_wr16(st + 14, 0x41u);           /* endCode[0] = 'A' */
        xfontft_wr16(st + 16, 0xFFFFu);         /* endCode[1] */
        xfontft_wr16(st + 18, 0u);              /* reservedPad */
        xfontft_wr16(st + 20, 0x41u);           /* startCode[0] */
        xfontft_wr16(st + 22, 0xFFFFu);         /* startCode[1] */
        xfontft_wr16s(st + 24, 1 - 0x41);       /* idDelta[0]：'A' → glyph 1 */
        xfontft_wr16(st + 26, 1u);              /* idDelta[1]：0xFFFF+1 → 0 */
        xfontft_wr16(st + 28, 0u);              /* idRangeOffset[0] */
        xfontft_wr16(st + 30, 0u);              /* idRangeOffset[1] */
    }
    off = cmapOff + 44u;
    off = (off + 3u) & ~(size_t)3u;
    /* glyf（30B）：glyph 0 空（.notdef），glyph 1 = 单轮廓三角形。 */
    glyfOff = off;
    {
        unsigned char* g = buf + glyfOff;
        xfontft_wr16s(g, 1);                    /* numberOfContours */
        xfontft_wr16s(g + 2, 0);                /* xMin */
        xfontft_wr16s(g + 4, 0);                /* yMin */
        xfontft_wr16s(g + 6, (int)triW);        /* xMax */
        xfontft_wr16s(g + 8, (int)triH);        /* yMax */
        xfontft_wr16(g + 10, 2u);               /* endPtsOfContours[0] = 2 */
        xfontft_wr16(g + 12, 0u);               /* instructionLength */
        g[14] = 0x01; g[15] = 0x01; g[16] = 0x01; /* flags：on-curve，int16 坐标 */
        xfontft_wr16s(g + 17, 0);               /* x[0] = 0 */
        xfontft_wr16s(g + 19, (int)triW);       /* x[1] = +triW */
        xfontft_wr16s(g + 21, -(int)triW);      /* x[2] = -triW */
        xfontft_wr16s(g + 23, 0);               /* y[0] = 0 */
        xfontft_wr16s(g + 25, 0);               /* y[1] = 0 */
        xfontft_wr16s(g + 27, (int)triH);       /* y[2] = +triH */
    }
    off = glyfOff + glyfLen;
    off = (off + 3u) & ~(size_t)3u;
    /* head（54B）。 */
    headOff = off;
    {
        unsigned char* h = buf + headOff;
        xfontft_wr32(h, 0x00010000ul);          /* version */
        xfontft_wr32(h + 4, 0x00010000ul);      /* fontRevision */
        xfontft_wr32(h + 8, 0ul);               /* checkSumAdjustment */
        xfontft_wr32(h + 12, 0x5F0F3CF5ul);     /* magicNumber */
        xfontft_wr16(h + 16, 0x000Bu);          /* flags */
        xfontft_wr16(h + 18, upem);             /* unitsPerEm */
        /* created/modified 16B 全 0 */
        xfontft_wr16s(h + 36, 0);               /* xMin */
        xfontft_wr16s(h + 38, 0);               /* yMin */
        xfontft_wr16s(h + 40, (int)triW);       /* xMax */
        xfontft_wr16s(h + 42, (int)triH);       /* yMax */
        xfontft_wr16(h + 44, 0u);               /* macStyle */
        xfontft_wr16(h + 46, 8u);               /* lowestRecPPEM */
        xfontft_wr16s(h + 48, 2);               /* fontDirectionHint */
        xfontft_wr16s(h + 50, 0);               /* indexToLocFormat = 短 */
        xfontft_wr16s(h + 52, 0);               /* glyphDataFormat */
    }
    off = headOff + 54u;
    off = (off + 3u) & ~(size_t)3u;
    /* hhea（36B）。 */
    hheaOff = off;
    {
        unsigned char* h = buf + hheaOff;
        xfontft_wr32(h, 0x00010000ul);          /* version */
        xfontft_wr16s(h + 4, (int)(upem / 2u));         /* ascender */
        xfontft_wr16s(h + 6, -(int)(upem / 4u));        /* descender */
        xfontft_wr16s(h + 8, 0);                /* lineGap */
        xfontft_wr16(h + 10, upem);             /* advanceWidthMax */
        xfontft_wr16s(h + 12, 0);               /* minLeftSideBearing */
        xfontft_wr16s(h + 14, 0);               /* minRightSideBearing */
        xfontft_wr16s(h + 16, (int)triW);       /* xMaxExtent */
        xfontft_wr16s(h + 18, 1);               /* caretSlopeRise */
        xfontft_wr16s(h + 20, 0);               /* caretSlopeRun */
        xfontft_wr16s(h + 22, 0);               /* caretOffset */
        for (i = 0; i < 4; ++i)                 /* reserved ×4 */
            xfontft_wr16s(h + 24 + 2 * i, 0);
        xfontft_wr16s(h + 32, 0);               /* metricDataFormat */
        xfontft_wr16(h + 34, 1u);               /* numberOfHMetrics */
    }
    off = hheaOff + 36u;
    off = (off + 3u) & ~(size_t)3u;
    /* hmtx（4B）：numberOfHMetrics=1，glyph 0/1 共用该 advance=upem。 */
    hmtxOff = off;
    xfontft_wr16(buf + hmtxOff, upem);          /* advanceWidth */
    xfontft_wr16s(buf + hmtxOff + 2, 0);        /* lsb */
    off = hmtxOff + 4u;
    off = (off + 3u) & ~(size_t)3u;
    /* loca（6B，短格式 /2）：glyph0 空 [0,0]，glyph1 长 glyfLen。 */
    locaOff = off;
    xfontft_wr16(buf + locaOff, 0u);
    xfontft_wr16(buf + locaOff + 2, 0u);
    xfontft_wr16(buf + locaOff + 4, (unsigned)(glyfLen / 2u));
    off = locaOff + 6u;
    off = (off + 3u) & ~(size_t)3u;
    /* maxp（32B，v1.0）。 */
    maxpOff = off;
    xfontft_wr32(buf + maxpOff, 0x00010000ul);
    xfontft_wr16(buf + maxpOff + 4, 2u);        /* numGlyphs */
    xfontft_wr16(buf + maxpOff + 6, 3u);        /* maxPoints */
    xfontft_wr16(buf + maxpOff + 8, 1u);        /* maxContours */
    xfontft_wr16(buf + maxpOff + 14, 2u);       /* maxZones */
    off = maxpOff + 32u;
    /* 表目录记录（按字母序 cmap/glyf/head/hhea/hmtx/loca/maxp）。 */
    {
        struct { const char* tag; size_t offset; size_t length; } tables[7] = {
            { "cmap", cmapOff, 44u },
            { "glyf", glyfOff, glyfLen },
            { "head", headOff, 54u },
            { "hhea", hheaOff, 36u },
            { "hmtx", hmtxOff, 4u },
            { "loca", locaOff, 6u },
            { "maxp", maxpOff, 32u }
        };
        for (i = 0; i < numTables; ++i)
        {
            unsigned char* r = buf + 12u + (size_t)i * 16u;
            memcpy(r, tables[i].tag, 4u);
            xfontft_wr32(r + 4, 0ul);                    /* 校验和（FT 不校验） */
            xfontft_wr32(r + 8, (unsigned long)tables[i].offset);
            xfontft_wr32(r + 12, (unsigned long)tables[i].length);
        }
    }
    return off;
}

/* 经 XFile 写测试字体（与读侧同一 FS 抽象；WriteOnly|Truncate 即 "wb"）。 */
static bool xfontft_write_file(const char* path, const unsigned char* data,
                               size_t n)
{
    XString* name = NULL;
    XFile* file = NULL;
    bool ok = false;
    if (!path || !data || !n)
        return false;
    name = XString_create_utf8(path);
    file = name ? XFile_create() : NULL;
    if (!name || !file)
        goto out;
    XFile_setFileName(file, name);
    if (!XFile_open_2(file, XIODevice_WriteOnly | XIODevice_Truncate, 0))
        goto out;
    ok = XIODevice_write_1((XIODevice*)file, (const char*)data,
                           (int64_t)n) == (int64_t)n;
    XIODevice_close_base((XIODevice*)file);
out:
    if (file) XClassDelete((XClass*)file);
    if (name) XClassDelete((XClass*)name);
    return ok;
}

/* ==================== XFontOutlineFace copy/move 分支保持（P2.3 门禁） ====================
   不依赖 FT 与运行期开关，常跑。断言：拷贝/移动后 kind 与后端位
   （m_ft）逐位保持；移动出侧按 move 语义复位
   （provider 清零、m_ft=false、kind=None、family=NULL）。
   [已移除] 原 file（XFO1）分支用例随 m_file 位删除。 */

/* provider 分支桩：合法度量 + 可识别指针即可，不参与实际加载。 */
static bool xfontft_faceProviderStub(uint32_t codepoint,
                                     XFontOutlineGlyphMetrics* metrics,
                                     const XFontOutlineSink* sink,
                                     void* userData)
{
    (void)codepoint; (void)metrics; (void)sink; (void)userData;
    return false;
}

/* 两个 face 的后端位/kind/family/provider 逐字段一致（拷贝保持口径）。 */
static bool xfontft_faceFieldsEqual(const XFontOutlineFace* dst,
                                    const XFontOutlineFace* src)
{
    return dst->m_class.m_kind == src->m_class.m_kind &&
           dst->m_ft == src->m_ft &&
           dst->m_class.m_family == src->m_class.m_family &&
           dst->m_provider.m_loadGlyph == src->m_provider.m_loadGlyph &&
           dst->m_provider.m_info.unitsPerEm ==
               src->m_provider.m_info.unitsPerEm &&
           dst->m_provider.m_info.ascent ==
               src->m_provider.m_info.ascent &&
           dst->m_provider.m_info.descent ==
               src->m_provider.m_info.descent &&
           dst->m_provider.m_info.lineGap == src->m_provider.m_info.lineGap;
}

static bool test_face_copy_move_branches(void)
{
    static const XFontOutlineProvider provider =
    {
        "xfontft_copysrc_provider",
        { 1000, 500, 250, 0 },
        xfontft_faceProviderStub,
        NULL
    };
    XFontOutlineFace providerFace;
    XFontOutlineFace dest;
    bool ok = true;
    /* ---- provider 分支 ---- */
    XFontOutlineFace_init(&providerFace, &provider);
    memset(&dest, 0, sizeof(dest));
    XClassCopy(&dest, &providerFace);
    if (!XClassGetVtable(&dest.m_class) ||
        !xfontft_faceFieldsEqual(&dest, &providerFace) ||
        dest.m_class.m_kind != XFontFace_Outline || dest.m_ft)
    {
        TEST_FAIL("face_copy_move", "provider 分支拷贝字段丢失");
        ok = false;
    }
    {
        XFontOutlineFace moved;
        memset(&moved, 0, sizeof(moved));
        XClassMove(&moved, &dest);
        if (!XClassGetVtable(&moved.m_class) ||
            !xfontft_faceFieldsEqual(&moved, &providerFace))
        {
            TEST_FAIL("face_copy_move", "provider 分支移动后字段丢失");
            ok = false;
        }
        if (dest.m_ft ||
            dest.m_class.m_kind != XFontFace_None ||
            dest.m_class.m_family != NULL ||
            dest.m_provider.m_loadGlyph != NULL)
        {
            TEST_FAIL("face_copy_move", "provider 分支移动出侧未复位");
            ok = false;
        }
    }
    /* [已移除 2026-10-07] 原 file（XFO1）分支用例：m_file 位与
       XFontOutlineFace_initFile 随自研轮廓字实现删除。 */
    if (ok)
        TEST_PASS("face_copy_move_branches");
    return ok;
}

#if XFONT_FT_FACE_ON
/* ==================== sink 捕获 ==================== */
typedef struct XFontFtSinkCapture
{
    int m_moves, m_lines, m_quads, m_cubics, m_closes;
    double m_xs[16];
    double m_ys[16];
    int m_count;
} XFontFtSinkCapture;

static bool xfontft_capMove(void* ud, float x, float y)
{
    XFontFtSinkCapture* c = (XFontFtSinkCapture*)ud;
    c->m_moves++;
    if (c->m_count < 16)
    {
        c->m_xs[c->m_count] = (double)x;
        c->m_ys[c->m_count] = (double)y;
        c->m_count++;
    }
    return true;
}

static bool xfontft_capLine(void* ud, float x, float y)
{
    XFontFtSinkCapture* c = (XFontFtSinkCapture*)ud;
    c->m_lines++;
    if (c->m_count < 16)
    {
        c->m_xs[c->m_count] = (double)x;
        c->m_ys[c->m_count] = (double)y;
        c->m_count++;
    }
    return true;
}

static bool xfontft_capQuad(void* ud, float cx, float cy, float x, float y)
{
    XFontFtSinkCapture* c = (XFontFtSinkCapture*)ud;
    (void)cx; (void)cy; (void)x; (void)y;
    c->m_quads++;
    return true;
}

static bool xfontft_capCubic(void* ud, float c1x, float c1y, float c2x,
                             float c2y, float x, float y)
{
    XFontFtSinkCapture* c = (XFontFtSinkCapture*)ud;
    (void)c1x; (void)c1y; (void)c2x; (void)c2y; (void)x; (void)y;
    c->m_cubics++;
    return true;
}

static bool xfontft_capClose(void* ud)
{
    ((XFontFtSinkCapture*)ud)->m_closes++;
    return true;
}

static void xfontft_sinkInit(XFontOutlineSink* sink, XFontFtSinkCapture* cap)
{
    memset(cap, 0, sizeof(*cap));
    sink->userData = cap;
    sink->moveTo = xfontft_capMove;
    sink->lineTo = xfontft_capLine;
    sink->quadTo = xfontft_capQuad;
    sink->cubicTo = xfontft_capCubic;
    sink->close = xfontft_capClose;
}

/* ==================== 独立 FT 参考（只取 26.6 原始量） ==================== */
/* 复用 XFontFt_library 而非自有库：全工程零 FT_Done_FreeType 调用点红线
 * （§2.2）——测试不产生第二个库实例，也就无需 Done 任何库。 */
static FT_Face xfontft_refFace(const unsigned char* data, size_t size,
                               uint32_t px26_6)
{
    FT_Library library = XFontFt_library();
    FT_Face face = NULL;
    if (!library)
        return NULL;
    if (FT_New_Memory_Face(library, (const FT_Byte*)data, (FT_Long)size, 0,
                           &face) != 0 || !face)
        return NULL;
    if (FT_Set_Char_Size(face, 0, (FT_F26Dot6)px26_6, 72, 72) != 0)
    {
        FT_Done_Face(face);
        return NULL;
    }
    return face;
}

/* 与实现同档位的加载旗标（autohint 档随 XFONT_FT_AUTOHINT env，缺省开；
 * 小字号 NO_HINTING 分档随 XFONT_FT_HINT_MIN_PX env，缺省 13——阈值
 * 解析与宏名与 XFontFt.c「小字号 hint 分档」节逐字同款，两处必须同步
 * 改；实现侧为 static 首读，此处每次解析，测试低频无碍）。 */
#ifndef XFONT_FT_HINT_MIN_PX
#define XFONT_FT_HINT_MIN_PX 13
#endif

static long xfontft_hintMinPxMirror(void)
{
    const char* h = getenv("XFONT_FT_HINT_MIN_PX");
    long value = -1;
    if (h && h[0])
    {
        char* end = NULL;
        value = strtol(h, &end, 10);
        if (end == h || *end != '\0' || value < 0 || value > 1024)
            value = -1;
    }
    return value >= 0 ? value : (long)XFONT_FT_HINT_MIN_PX;
}

static FT_Int32 xfontft_refLoadFlags(uint32_t px26_6)
{
    const char* v = getenv("XFONT_FT_AUTOHINT");
    long hintMinPx = xfontft_hintMinPxMirror();
    FT_Int32 flags = FT_LOAD_DEFAULT | FT_LOAD_NO_BITMAP;
    if (hintMinPx != 0 && px26_6 < ((uint32_t)hintMinPx << 6))
        return flags | FT_LOAD_NO_HINTING; /* 小字号档：与实现同走无 hint。 */
    flags |= FT_LOAD_TARGET_LIGHT;
    if (v && v[0] == '0' && v[1] == '\0')
        return flags;
    return flags | FT_LOAD_FORCE_AUTOHINT;
}

/* 读取测试字体文件字节（参考值与实现各自持有一份内存副本）。 */
static bool xfontft_read_file(const char* path, unsigned char* buf,
                              size_t cap, size_t* outSize)
{
    XByteArray* bytes = NULL;
    size_t size;
    if (!XFont_readFileBytes(path, &bytes) || !bytes)
        return false;
    size = XByteArray_size_base((XContainer*)bytes);
    if (size > cap)
    {
        XClassDelete((XClass*)bytes);
        return false;
    }
    memcpy(buf, XByteArray_data(bytes), size);
    *outSize = size;
    XClassDelete((XClass*)bytes);
    return true;
}

/* exe 目录锚定的测试字体路径（CWD 无关，读侧 ../ 剥离回退可命中）。
   applicationDirPath 每次返回调用方拥有的新 XString（XCoreApplication.c:275/292，
   XFont_exeDir 同款用后即删），utf8 为串内借用缓存，取值后必须释放 dir。 */
static bool xfontft_fontPath(const char* fileName, char* buf, size_t cap)
{
    const XString* dir = XCoreApplication_applicationDirPath();
    const char* utf8;
    size_t len;
    bool ok = false;
    if (!dir)
        return false;
    utf8 = XString_toUtf8(dir);
    if (utf8 && utf8[0])
    {
        len = strlen(utf8);
        if (len + 1u + strlen(fileName) + 1u <= cap)
        {
            memcpy(buf, utf8, len);
            if (buf[len - 1u] != '/' && buf[len - 1u] != '\\')
                buf[len++] = '/';
            memcpy(buf + len, fileName, strlen(fileName) + 1u);
            ok = true;
        }
    }
    XClassDelete((XClass*)dir);
    return ok;
}

/* ==================== px 闭合公式（单字体单字号） ==================== */
/* 断言：provider 设计单位输出 × scale 复原 FT 实测 hinted 像素几何
   （advance / xMax / yMax 三向，容差含设计字段 int 取整 ±0.5 设计单位）。 */
static bool xfontft_checkClosedForm(const XFont* font, const char* tag,
                                    const unsigned char* data, size_t size,
                                    float scale, uint32_t px)
{
    XFontOutlineGlyphMetrics m;
    FT_Face ref;
    double refAdvPx, refXMaxPx, refYMaxPx;
    double gotAdvPx, gotXMaxPx, gotYMaxPx;
    FT_UInt gindex;
    bool ok = true;
    if (!XFontFt_fileLoadGlyphScaled(font, scale, 0x41u, &m, NULL))
    {
        TEST_FAIL(tag, "fileLoadGlyphScaled 失败");
        return false;
    }
    ref = xfontft_refFace(data, size, px << 6);
    if (!ref)
    {
        TEST_FAIL(tag, "FT 参考 face 建立失败");
        return false;
    }
    gindex = FT_Get_Char_Index(ref, 0x41u);
    if (gindex == 0 ||
        FT_Load_Glyph(ref, gindex, xfontft_refLoadFlags(px << 6)) != 0)
    {
        FT_Done_Face(ref);
        TEST_FAIL(tag, "FT 参考字形加载失败");
        return false;
    }
    refAdvPx = (double)ref->glyph->advance.x / 64.0;
    refXMaxPx = ((double)ref->glyph->metrics.horiBearingX +
                 (double)ref->glyph->metrics.width) / 64.0;
    refYMaxPx = (double)ref->glyph->metrics.horiBearingY / 64.0;
    FT_Done_Face(ref);
    gotAdvPx = (double)m.advance * (double)scale;
    gotXMaxPx = (double)m.xMax * (double)scale;
    gotYMaxPx = (double)m.yMax * (double)scale;
    if (fabs(gotAdvPx - refAdvPx) > 0.03 ||
        fabs(gotXMaxPx - refXMaxPx) > 0.03 ||
        fabs(gotYMaxPx - refYMaxPx) > 0.03)
    {
        XPrintf("[FAIL] %s: 闭合回路偏差 adv %.4f vs %.4f, "
                "xMax %.4f vs %.4f, yMax %.4f vs %.4f\n",
                tag, gotAdvPx, refAdvPx, gotXMaxPx, refXMaxPx,
                gotYMaxPx, refYMaxPx);
        ok = false;
    }
    return ok;
}
#endif /* XFONT_FT_FACE_ON */

/* ==================== 用例 ==================== */
#if XFONT_FT_FACE_ON

/* 用例 1：px 闭合公式两档（upem=1000 常规档 + upem=16384=FT 合法上界档；
   两字体 advance=upem 的全 em 宽字形，x 方向 LIGHT 不 hint → 设计 advance
   恰为 upem，跨字体同目标 px 的像素 advance 必须一致）。 */
static bool test_closed_form_two_tiers(void)
{
    const unsigned pxs[] = { 8u, 13u, 16u, 24u };
    const int pxCount = (int)(sizeof(pxs) / sizeof(pxs[0]));
    struct
    {
        const char* m_file;
        unsigned m_upem;
        unsigned m_tri;
        unsigned char m_bytes[XFONTFT_TTF_CAP];
        size_t m_size;
        char m_path[512];
        XFont m_font;
    } tiers[2] = {
        { "xfontft_upem1000.ttf", 1000u, 500u, { 0 }, 0u, { 0 }, { { 0 } } },
        { "xfontft_upem16384.ttf", 16384u, 8192u, { 0 }, 0u, { 0 }, { { 0 } } }
    };
    double advPx[2][4];
    bool ok = true;
    int t;
    memset(advPx, 0, sizeof(advPx));
    for (t = 0; t < 2; ++t)
    {
        XFontOutlineInfo info;
        int p;
        if (!xfontft_fontPath(tiers[t].m_file, tiers[t].m_path,
                              sizeof(tiers[t].m_path)))
            return false;
        tiers[t].m_size = xfontft_build_ttf(tiers[t].m_upem,
                                            tiers[t].m_tri, tiers[t].m_tri,
                                            tiers[t].m_bytes,
                                            XFONTFT_TTF_CAP);
        if (!tiers[t].m_size ||
            !xfontft_write_file(tiers[t].m_path, tiers[t].m_bytes,
                                tiers[t].m_size))
        {
            TEST_FAIL("closed_form", "测试字体写入失败");
            return false;
        }
        XFont_init(&tiers[t].m_font);
        XFont_setFamily(&tiers[t].m_font, tiers[t].m_path);
        if (!XFontFt_fileInfo(&tiers[t].m_font, &info) ||
            info.unitsPerEm != (int)tiers[t].m_upem)
        {
            TEST_FAIL("closed_form", "fileInfo 缺失或 upem 不符");
            XClassDeinit((XClass*)&tiers[t].m_font);
            return false;
        }
        for (p = 0; p < pxCount; ++p)
        {
            char tag[64];
            float scale = (float)pxs[p] / (float)tiers[t].m_upem;
            XFontOutlineGlyphMetrics m;
            memset(&m, 0, sizeof(m));
            snprintf(tag, sizeof(tag), "closed_form(upem=%u,px=%u)",
                     tiers[t].m_upem, pxs[p]);
            if (!xfontft_checkClosedForm(&tiers[t].m_font, tag,
                                         tiers[t].m_bytes, tiers[t].m_size,
                                         scale, pxs[p]))
                ok = false;
            /* 全 em 宽字形：x 方向无 hint → 设计 advance 恰为 upem。 */
            if (XFontFt_fileLoadGlyphScaled(&tiers[t].m_font, scale, 0x41u,
                                            &m, NULL) &&
                (m.advance < (int)tiers[t].m_upem - 1 ||
                 m.advance > (int)tiers[t].m_upem + 1))
            {
                XPrintf("[FAIL] closed_form: upem=%u 设计 advance=%d 偏离\n",
                        tiers[t].m_upem, m.advance);
                ok = false;
            }
            advPx[t][p] = (double)m.advance * (double)scale;
        }
        XClassDeinit((XClass*)&tiers[t].m_font);
    }
    /* 跨 upem 档一致性：同一目标 px 的像素 advance 两档必须一致。 */
    {
        int p;
        for (p = 0; p < pxCount; ++p)
            if (fabs(advPx[0][p] - advPx[1][p]) > 0.03)
            {
                XPrintf("[FAIL] closed_form: 跨 upem 档 px=%u advance "
                        "%.4f vs %.4f\n", pxs[p], advPx[0][p], advPx[1][p]);
                ok = false;
            }
    }
    if (ok)
        TEST_PASS("closed_form_two_tiers");
    return ok;
}

/* 用例 2：缺字形 false（cmap 只映射 'A'；'B'/汉字无映射），含旧槽与
   scaled 槽两路；'A' 旧槽（scale=1.0 语义）应成功。 */
static bool test_missing_glyph_false(void)
{
    char pathA[512];
    XFont font;
    XFontOutlineGlyphMetrics m;
    bool ok = true;
    float scale16;
    if (!xfontft_fontPath("xfontft_upem1000.ttf", pathA, sizeof(pathA)))
        return false;
    XFont_init(&font);
    XFont_setFamily(&font, pathA);
    scale16 = 16.0f / 1000.0f;
    if (XFontFt_fileLoadGlyphScaled(&font, scale16, 0x42u, &m, NULL))
    {
        TEST_FAIL("missing_glyph", "'B' 不应命中字形");
        ok = false;
    }
    if (XFontFt_fileLoadGlyphScaled(&font, scale16, 0x4E2Cu, &m, NULL))
    {
        TEST_FAIL("missing_glyph", "汉字码位不应命中字形");
        ok = false;
    }
    if (XFontFt_fileLoadGlyph(&font, 0x4E2Cu, &m, NULL))
    {
        TEST_FAIL("missing_glyph", "旧槽汉字码位不应命中字形");
        ok = false;
    }
    if (!XFontFt_fileLoadGlyph(&font, 0x41u, &m, NULL))
    {
        TEST_FAIL("missing_glyph", "旧槽 'A'（scale 1.0 语义）应成功");
        ok = false;
    }
    XClassDeinit((XClass*)&font);
    if (ok)
        TEST_PASS("missing_glyph_false");
    return ok;
}

/* 用例 3：家族槽满回退——4 槽占满后第 5 家族判失败，槽内家族不受扰。 */
static bool test_family_slots_full_fallback(void)
{
    const char* files[] = { "xfontft_familyC.ttf", "xfontft_familyD.ttf",
                            "xfontft_familyE.ttf" };
    unsigned upems[] = { 512u, 1024u, 2048u };
    bool ok = true;
    int i;
    /* C/D 入槽（连同用例 1 的 A/B 两族 = 4 槽满）；E 为第 5 族必须判失败。 */
    for (i = 0; i < 2; ++i)
    {
        unsigned char bytes[XFONTFT_TTF_CAP];
        size_t size = xfontft_build_ttf(upems[i], upems[i] / 2u,
                                        upems[i] / 2u, bytes,
                                        XFONTFT_TTF_CAP);
        char path[512];
        XFont font;
        XFontOutlineInfo info;
        if (!size || !xfontft_fontPath(files[i], path, sizeof(path)) ||
            !xfontft_write_file(path, bytes, size))
        {
            TEST_FAIL("family_slots", "测试字体写入失败");
            return false;
        }
        XFont_init(&font);
        XFont_setFamily(&font, path);
        if (!XFontFt_fileInfo(&font, &info))
        {
            XPrintf("[FAIL] family_slots: 第 %d 个家族应可装载\n", i + 3);
            ok = false;
        }
        XClassDeinit((XClass*)&font);
    }
    /* 第 5 家族 E：槽满即 FT 判失败（XFont_face 落位图回退）。 */
    {
        unsigned char bytes[XFONTFT_TTF_CAP];
        size_t size = xfontft_build_ttf(upems[2], upems[2] / 2u,
                                        upems[2] / 2u, bytes,
                                        XFONTFT_TTF_CAP);
        char path[512];
        XFont fontE;
        XFontOutlineInfo info;
        if (!size || !xfontft_fontPath(files[2], path, sizeof(path)) ||
            !xfontft_write_file(path, bytes, size))
        {
            TEST_FAIL("family_slots", "第 5 家族字体写入失败");
            return false;
        }
        XFont_init(&fontE);
        XFont_setFamily(&fontE, path);
        if (XFontFt_fileInfo(&fontE, &info))
        {
            TEST_FAIL("family_slots", "第 5 家族应判失败回退");
            ok = false;
        }
        XClassDeinit((XClass*)&fontE);
    }
    /* 槽内家族不受扰：A/B 仍可查。 */
    {
        XFont fontA;
        XFontOutlineInfo info;
        char pathA[512];
        xfontft_fontPath("xfontft_upem1000.ttf", pathA, sizeof(pathA));
        XFont_init(&fontA);
        XFont_setFamily(&fontA, pathA);
        if (!XFontFt_fileInfo(&fontA, &info))
        {
            TEST_FAIL("family_slots", "槽内家族 A 应保持可用");
            ok = false;
        }
        XClassDeinit((XClass*)&fontA);
    }
    if (ok)
        TEST_PASS("family_slots_full_fallback");
    return ok;
}

/* 用例 4：损坏字体家族锁存 + 缺文件不锁存（时序自愈对照）。 */
static bool test_corrupt_family_latch(void)
{
    unsigned char bytes[XFONTFT_TTF_CAP];
    size_t size;
    char pathD[512];
    char pathL[512];
    bool ok = true;
    XFont fontL;
    XFontOutlineInfo info;
    if (!xfontft_fontPath("xfontft_familyD.ttf", pathD, sizeof(pathD)) ||
        !xfontft_fontPath("xfontft_latch.ttf", pathL, sizeof(pathL)))
        return false;
    /* 前置（时序自愈对照）：同一路径先缺文件判负，落好文件后转正——
       "缺文件"负结果不入缓（§3.1）。fontB 装载即为此路径（用例 1）。 */
    /* 损坏 = 保留 4 字节魔数 + 截断表区：魔数通过、FT 拒载 → 锁存。 */
    size = xfontft_build_ttf(1024u, 512u, 512u, bytes, XFONTFT_TTF_CAP);
    if (!size)
        return false;
    if (!xfontft_write_file(pathL, bytes, 60u))
    {
        TEST_FAIL("corrupt_latch", "损坏字体写入失败");
        return false;
    }
    XFont_init(&fontL);
    XFont_setFamily(&fontL, pathL);
    if (XFontFt_fileInfo(&fontL, &info))
    {
        TEST_FAIL("corrupt_latch", "损坏字体首次探测应判失败");
        ok = false;
    }
    /* 同一路径换成完好字节（内容=已验证可用的 familyD 字体）：锁存后
       不再触碰文件，必须仍判失败——锁存语义的直接证据。 */
    size = xfontft_build_ttf(1024u, 512u, 512u, bytes, XFONTFT_TTF_CAP);
    if (!size || !xfontft_write_file(pathL, bytes, size))
    {
        TEST_FAIL("corrupt_latch", "完好字体覆写失败");
        ok = false;
    }
    if (XFontFt_fileInfo(&fontL, &info))
    {
        TEST_FAIL("corrupt_latch", "锁存后同路径好文件仍应判失败");
        ok = false;
    }
    XClassDeinit((XClass*)&fontL);
    /* 锁存按家族隔离：同字节的 familyD 不受扰。 */
    {
        XFont fontD;
        xfontft_fontPath("xfontft_familyD.ttf", pathD, sizeof(pathD));
        XFont_init(&fontD);
        XFont_setFamily(&fontD, pathD);
        if (!XFontFt_fileInfo(&fontD, &info))
        {
            TEST_FAIL("corrupt_latch", "锁存不应波及同字节其它家族");
            ok = false;
        }
        XClassDeinit((XClass*)&fontD);
    }
    if (ok)
        TEST_PASS("corrupt_family_latch");
    return ok;
}

/* 用例 5：字号交错循环——家族 face 常驻（遗留-1 P0 重构后的不变量）。
   8 次加载跨 6 个字号交错（含旧字号回取），每次度量一致；计数器断言
   全程零 FT_New_Memory_Face（字号切换不重建 face，家族 face 进程期
   常驻）且加载次数全部落在 Set/Skip 两去路。原用例 5 断言的"LRU 逐出
   后同 blob 重建"路径已随重构整体删除。ASan 构建下 blob/face 生命周期
   错位仍会实爆（face 与 blob 同槽进程期同存同亡）。 */
static bool test_size_cycle_family_face(void)
{
    /* 6 字号 > 原 LRU 4 槽：旧模型下从第 5 次起必然连续逐出重建。 */
    const unsigned pxs[] = { 8u, 13u, 16u, 24u, 32u, 40u, 8u, 13u };
    const int count = (int)(sizeof(pxs) / sizeof(pxs[0]));
    char pathA[512];
    XFont font;
    XFontFtStats st0, st1;
    bool ok = true;
    int i;
    if (!xfontft_fontPath("xfontft_upem1000.ttf", pathA, sizeof(pathA)))
        return false;
    XFont_init(&font);
    XFont_setFamily(&font, pathA);
    XFontFt_stats(&st0);
    for (i = 0; i < count; ++i)
    {
        XFontOutlineGlyphMetrics m;
        float scale = (float)pxs[i] / 1000.0f;
        if (!XFontFt_fileLoadGlyphScaled(&font, scale, 0x41u, &m, NULL))
        {
            XPrintf("[FAIL] size_cycle: px=%u 第 %d 次加载失败\n",
                    pxs[i], i);
            ok = false;
            continue;
        }
        /* 全 em 宽 advance：设计值恒 ≈ upem，字号来回切换后不得漂移。 */
        if (m.advance < 999 || m.advance > 1001)
        {
            XPrintf("[FAIL] size_cycle: px=%u advance=%d 漂移\n",
                    pxs[i], m.advance);
            ok = false;
        }
    }
    XFontFt_stats(&st1);
    /* 家族 face 常驻：交错循环全程零 face 重建（旧模型此处 ≥4 次）。 */
    if (st1.m_faceCreates != st0.m_faceCreates)
    {
        XPrintf("[FAIL] size_cycle: 字号切换重建 face（%llu → %llu）\n",
                (unsigned long long)st0.m_faceCreates,
                (unsigned long long)st1.m_faceCreates);
        ok = false;
    }
    if (st1.m_glyphLoads - st0.m_glyphLoads != (uint64_t)count)
    {
        XPrintf("[FAIL] size_cycle: glyphLoads 增量 %llu != %d\n",
                (unsigned long long)(st1.m_glyphLoads - st0.m_glyphLoads),
                count);
        ok = false;
    }
    if (st1.m_charSizeSets + st1.m_charSizeSkips -
        (st0.m_charSizeSets + st0.m_charSizeSkips) != (uint64_t)count)
    {
        XPrintf("[FAIL] size_cycle: 字号切换两去路增量 %llu != %d\n",
                (unsigned long long)(st1.m_charSizeSets +
                                     st1.m_charSizeSkips -
                                     (st0.m_charSizeSets +
                                      st0.m_charSizeSkips)),
                count);
        ok = false;
    }
    XClassDeinit((XClass*)&font);
    if (ok)
        TEST_PASS("size_cycle_family_face_resident");
    return ok;
}

/* 用例 5b：小字号 hint 分档（遗留-2）——px < XFONT_FT_HINT_MIN_PX 走
   FT_LOAD_NO_HINTING，且分档只落 load_flags 位：计数器断言 noHint 子集
   计数恰好落在阈值以下字号、跨阈值切换零 face 重建、字号 8→13→8 恰
   3 次全走 Set（无重建无额外字号状态）。期望 noHint 数按 env/宏镜像
   计算（xfontft_hintMinPxMirror，与实现 XFontFt_hintMinPx 同款解析），
   任何 env 档（含 "0" 关分档）下自洽。 */
static bool test_hint_tier_small_px(void)
{
    char pathA[512];
    XFont font;
    XFontFtStats st0, st1;
    const unsigned pxs[3] = { 8u, 13u, 8u };
    const long hintMinPx = xfontft_hintMinPxMirror();
    unsigned expectNoHint = 0u;
    bool ok = true;
    int i;
    if (!xfontft_fontPath("xfontft_upem1000.ttf", pathA, sizeof(pathA)))
        return false;
    for (i = 0; i < 3; ++i)
        if (hintMinPx != 0 && (long)pxs[i] < hintMinPx)
            ++expectNoHint;
    XFont_init(&font);
    XFont_setFamily(&font, pathA);
    XFontFt_stats(&st0);
    for (i = 0; i < 3; ++i)
    {
        XFontOutlineGlyphMetrics m;
        float scale = (float)pxs[i] / 1000.0f;
        if (!XFontFt_fileLoadGlyphScaled(&font, scale, 0x41u, &m, NULL))
        {
            XPrintf("[FAIL] hint_tier: px=%u 加载失败\n", pxs[i]);
            ok = false;
            continue;
        }
        /* 全 em 宽字形 advance 与 hint 档无关（x 方向 LIGHT 与无 hint
           都不动力）：设计值恒 ≈ upem。 */
        if (m.advance < 999 || m.advance > 1001)
        {
            XPrintf("[FAIL] hint_tier: px=%u advance=%d 漂移\n",
                    pxs[i], m.advance);
            ok = false;
        }
    }
    XFontFt_stats(&st1);
    if (st1.m_glyphLoads - st0.m_glyphLoads != (uint64_t)3)
    {
        XPrintf("[FAIL] hint_tier: glyphLoads 增量 %llu != 3\n",
                (unsigned long long)(st1.m_glyphLoads - st0.m_glyphLoads));
        ok = false;
    }
    if (st1.m_glyphNoHintLoads - st0.m_glyphNoHintLoads !=
        (uint64_t)expectNoHint)
    {
        XPrintf("[FAIL] hint_tier: noHint 增量 %llu != %u（阈值=%ld）\n",
                (unsigned long long)(st1.m_glyphNoHintLoads -
                                     st0.m_glyphNoHintLoads),
                expectNoHint, hintMinPx);
        ok = false;
    }
    /* 分档只落 load_flags：跨阈值切换零 face 重建；字号 8→13→8 每次
       都变，恰 3 次 Set、0 次 Skip。 */
    if (st1.m_faceCreates != st0.m_faceCreates)
    {
        XPrintf("[FAIL] hint_tier: 分档切换重建 face（%llu → %llu）\n",
                (unsigned long long)st0.m_faceCreates,
                (unsigned long long)st1.m_faceCreates);
        ok = false;
    }
    if (st1.m_charSizeSets - st0.m_charSizeSets != (uint64_t)3 ||
        st1.m_charSizeSkips != st0.m_charSizeSkips)
    {
        XPrintf("[FAIL] hint_tier: 字号切换 Set+%llu Skip+%llu != 3/0\n",
                (unsigned long long)(st1.m_charSizeSets - st0.m_charSizeSets),
                (unsigned long long)(st1.m_charSizeSkips - st0.m_charSizeSkips));
        ok = false;
    }
    XClassDeinit((XClass*)&font);
    if (ok)
        TEST_PASS("hint_tier_small_px");
    return ok;
}

/* 用例 6：FT_Outline_Decompose→sink 回路 + upem=65535 档实测。
   三角形轮廓五回调计数（1 move + 2 line + 补 1 close）；设计坐标 × scale
   与 FT 参考 26.6 逐点比对（同一 hint 档，容差只含 int 取整）。
   upem=65535 档：捆绑 FT（sfobjs.c:906-912）对 upem∉[16,16384] 拒载 →
   实测 = 解析失败家族锁存（真值见日志，与设计 §9"待实测"项闭环）。 */
static bool test_sink_roundtrip_and_big_upem(void)
{
    char pathA[512];
    XFont font;
    XFontOutlineSink sink;
    XFontFtSinkCapture cap;
    XFontOutlineGlyphMetrics m;
    float scale = 16.0f / 1000.0f;
    bool ok = true;
    if (!xfontft_fontPath("xfontft_upem1000.ttf", pathA, sizeof(pathA)))
        return false;
    XFont_init(&font);
    XFont_setFamily(&font, pathA);
    xfontft_sinkInit(&sink, &cap);
    if (!XFontFt_fileLoadGlyphScaled(&font, scale, 0x41u, &m, &sink))
    {
        TEST_FAIL("sink_roundtrip", "带 sink 加载失败");
        XClassDeinit((XClass*)&font);
        return false;
    }
    /* FT 分解语义：TrueType 轮廓隐式闭合，FT_Outline_Decompose 在轮廓末
     * 补发一条回到起点的 lineTo（闭合边），随后实现补 close——三角形
     * = 1 move + 3 line（2 条边 + 1 条闭合边）+ 1 close，共 4 点。 */
    if (cap.m_moves != 1 || cap.m_lines != 3 || cap.m_quads != 0 ||
        cap.m_cubics != 0 || cap.m_closes != 1 || cap.m_count != 4)
    {
        XPrintf("[FAIL] sink_roundtrip: 回调计数 move=%d line=%d quad=%d "
                "cubic=%d close=%d pts=%d\n", cap.m_moves, cap.m_lines,
                cap.m_quads, cap.m_cubics, cap.m_closes, cap.m_count);
        ok = false;
    }
    else if (fabs(cap.m_xs[3] - cap.m_xs[0]) > 1e-6 ||
             fabs(cap.m_ys[3] - cap.m_ys[0]) > 1e-6)
    {
        TEST_FAIL("sink_roundtrip", "闭合边未回到起点");
        ok = false;
    }
    /* 逐点比对 FT 参考（同字号同 hint 档）。 */
    {
        unsigned char data[XFONTFT_TTF_CAP];
        size_t size = 0u;
        FT_Face ref;
        int i;
        if (!xfontft_read_file(pathA, data, sizeof(data), &size))
        {
            TEST_FAIL("sink_roundtrip", "参考字体读回失败");
            XClassDeinit((XClass*)&font);
            return false;
        }
        ref = xfontft_refFace(data, size, 16u << 6);
        if (!ref || FT_Get_Char_Index(ref, 0x41u) == 0 ||
            FT_Load_Glyph(ref, FT_Get_Char_Index(ref, 0x41u),
                          xfontft_refLoadFlags(16u << 6)) != 0)
        {
            if (ref) FT_Done_Face(ref);
            TEST_FAIL("sink_roundtrip", "FT 参考加载失败");
            XClassDeinit((XClass*)&font);
            return false;
        }
        if (ref->glyph->outline.n_points != 3 ||
            ref->glyph->outline.n_contours != 1)
        {
            TEST_FAIL("sink_roundtrip", "参考轮廓点数不符");
            ok = false;
        }
        else
        {
            for (i = 0; i < 3; ++i)
            {
                double refX = (double)ref->glyph->outline.points[i].x / 64.0;
                double refY = (double)ref->glyph->outline.points[i].y / 64.0;
                double gotX = cap.m_xs[i] * (double)scale;
                double gotY = cap.m_ys[i] * (double)scale;
                if (fabs(gotX - refX) > 0.03 || fabs(gotY - refY) > 0.03)
                {
                    XPrintf("[FAIL] sink_roundtrip: 点 %d (%.4f,%.4f) vs "
                            "参考 (%.4f,%.4f)\n", i, gotX, gotY, refX, refY);
                    ok = false;
                }
            }
        }
        FT_Done_Face(ref);
    }
    XClassDeinit((XClass*)&font);
    if (ok)
        TEST_PASS("sink_roundtrip");
    /* ---- upem=65535 档实测（设计 §9 待实测项） ---- */
    {
        unsigned char bytes[XFONTFT_TTF_CAP];
        size_t size = xfontft_build_ttf(65535u, 32767u, 32767u, bytes,
                                        XFONTFT_TTF_CAP);
        char path[512];
        XFont fontBig;
        XFontOutlineInfo info;
        bool rejected = false;
        if (!size || !xfontft_fontPath("xfontft_upem65535.ttf", path,
                                       sizeof(path)) ||
            !xfontft_write_file(path, bytes, size))
        {
            TEST_FAIL("big_upem_65535", "测试字体写入失败");
            return false;
        }
        XFont_init(&fontBig);
        XFont_setFamily(&fontBig, path);
        if (!XFontFt_fileInfo(&fontBig, &info))
        {
            rejected = true;
            /* 锁存后复探仍负（同用例 4 语义）。 */
            if (XFontFt_fileInfo(&fontBig, &info))
            {
                TEST_FAIL("big_upem_65535", "拒载后应锁存");
                ok = false;
            }
        }
        else
        {
            /* 若此 FT 版本放行 65535：闭合公式须在大 upem 档成立。 */
            float s = 16.0f / (float)info.unitsPerEm;
            if (info.unitsPerEm != 65535)
            {
                XPrintf("big_upem_65535: FT 归一 upem=%d（非 65535）\n",
                        info.unitsPerEm);
            }
            if (!xfontft_checkClosedForm(&fontBig, "big_upem_65535", bytes,
                                         size, s, 16u))
                ok = false;
        }
        XClassDeinit((XClass*)&fontBig);
        XPrintf("big_upem_65535 实测: %s\n",
                rejected ? "捆绑 FT 拒载（sfobjs.c upem∈[16,16384] 门），"
                         "按解析失败家族锁存处理"
                         : "FT 接受，闭合公式验证见上");
        if (!rejected)
            TEST_PASS("big_upem_65535");
        else
            TEST_PASS("big_upem_65535(拒载→锁存)");
    }
    return ok;
}

/* 用例 7：FT face copy/move 后分支逐位保持 + 分发行为级验证（P2.3 门禁）。
   家族槽 face 拷贝/移动后 m_ft 必须保持——若丢失，两槽位分发静默落回
   provider 桩（恒 false），此处以"拷贝/移动体经包装函数仍能加载 'A'"
   直接反证。 */
static bool test_ft_face_copy_move_branch_preserved(void)
{
    char pathA[512];
    XFont font;
    XFontOutlineGlyphMetrics m;
    XFontOutlineInfo info;
    const XFontFace* src;
    XFontOutlineFace copied;
    XFontOutlineFace moved;
    float scale = 16.0f / 1000.0f;
    bool ok = true;
    if (!xfontft_fontPath("xfontft_upem1000.ttf", pathA, sizeof(pathA)))
        return false;
    XFont_init(&font);
    XFont_setFamily(&font, pathA);
    if (!XFontFt_fileInfo(&font, &info))
    {
        TEST_FAIL("ft_face_copy_move", "FT 家族解析失败");
        XClassDeinit((XClass*)&font);
        return false;
    }
    src = XFontFt_fileFace(XFont_family(&font));
    if (!src || src->m_kind != XFontFace_Outline ||
        ((const XFontOutlineFace*)src)->m_ft != true)
    {
        TEST_FAIL("ft_face_copy_move", "家族槽 face 后端位不符");
        XClassDeinit((XClass*)&font);
        return false;
    }
    /* 拷贝：字段逐位保持。 */
    memset(&copied, 0, sizeof(copied));
    XClassCopy(&copied, src);
    if (copied.m_class.m_kind != XFontFace_Outline || !copied.m_ft ||
        copied.m_class.m_family != src->m_family ||
        !XClassGetVtable(&copied.m_class))
    {
        TEST_FAIL("ft_face_copy_move", "拷贝后 FT 分支丢失");
        ok = false;
    }
    /* 拷贝体行为级：新槽/旧槽两路分发都必须仍走 m_ft 分支成功。 */
    if (!XFontFace_loadOutlineGlyphScaled_base(&copied.m_class, &font, scale,
                                               0x41u, &m, NULL))
    {
        TEST_FAIL("ft_face_copy_move", "拷贝体新槽分发失败（m_ft 丢失反证）");
        ok = false;
    }
    else if (m.advance < 999 || m.advance > 1001)
    {
        XPrintf("[FAIL] ft_face_copy_move: 拷贝体 advance=%d 漂移\n",
                m.advance);
        ok = false;
    }
    if (!XFontFace_loadOutlineGlyph_base(&copied.m_class, &font, 0x41u, &m,
                                         NULL))
    {
        TEST_FAIL("ft_face_copy_move", "拷贝体旧槽分发失败");
        ok = false;
    }
    /* 移动：目标保持 + 出侧复位。 */
    memset(&moved, 0, sizeof(moved));
    XClassMove(&moved, &copied);
    if (!moved.m_ft ||
        moved.m_class.m_kind != XFontFace_Outline ||
        moved.m_class.m_family != src->m_family)
    {
        TEST_FAIL("ft_face_copy_move", "移动后 FT 分支丢失");
        ok = false;
    }
    if (!XFontFace_loadOutlineGlyphScaled_base(&moved.m_class, &font, scale,
                                               0x41u, &m, NULL))
    {
        TEST_FAIL("ft_face_copy_move", "移动体新槽分发失败");
        ok = false;
    }
    if (copied.m_ft ||
        copied.m_class.m_kind != XFontFace_None ||
        copied.m_class.m_family != NULL ||
        copied.m_provider.m_loadGlyph != NULL)
    {
        TEST_FAIL("ft_face_copy_move", "移动出侧未复位");
        ok = false;
    }
    /* P2.4 接线正证：ft2 档 XFont_face 必须返回 FT 家族槽 face（指针
       同一性——painter 四类缓存以 face 指针为家族身份锚）。 */
    if (XFont_face(&font) != XFontFt_fileFace(XFont_family(&font)))
    {
        TEST_FAIL("ft_face_copy_move", "XFont_face 未命中 FT 家族槽 face");
        ok = false;
    }
    /* 回退链末层：无文件家族 → FT 层判负（进程期一行 stderr 警告）→
       兜底 first(Bitmap)=XFont8x16（非 FT face，家族槽无此家族）。 */
    {
        XFont fontBogus;
        const XFontFace* bogus;
        XFont_init(&fontBogus);
        XFont_setFamily(&fontBogus, "xfontft_no_such_family.ttf");
        bogus = XFont_face(&fontBogus);
        if (!bogus ||
            XFontFt_fileFace("xfontft_no_such_family.ttf") != NULL ||
            bogus == XFontFt_fileFace(XFont_family(&font)) ||
            XFontFace_kind(bogus) != XFontFace_Bitmap)
        {
            TEST_FAIL("ft_face_copy_move", "回退链末层未落到位图兜底");
            ok = false;
        }
        XClassDeinit((XClass*)&fontBogus);
    }
    XClassDeinit((XClass*)&font);
    if (ok)
        TEST_PASS("ft_face_copy_move_branch_preserved");
    return ok;
}
#endif /* XFONT_FT_FACE_ON */

/* ==================== 入口 ==================== */
int XFontFtTest_run(void)
{
    /* t7(provider/file 档)：XFontOutlineFace copy/move 分支保持——不依赖
       FT 与 env，全档常跑（P2.3 门禁）。 */
    if (!test_face_copy_move_branches())
        return 1;
#if !XFONT_FT_FACE_ON
    XPrintf("XFontFt 专项测试: XFONT_FT_FACE_ON=0，模块未编入，跳过 => 通过\n");
    return 0;
#else
    int result;
    /* [已移除 2026-10-07] 原 t0 env 首读语义用例（XFontFt_providerActive
     * 与 XFONT_PROVIDER 一致性）：env 选边随 XFO1 实现删除，FT 为唯一
     * 轮廓字实现，face 门禁用例无条件全跑。 */
    TEST_PASS("provider_env_removed_single_ft");
    {
        bool t1 = test_closed_form_two_tiers();
        bool t2 = test_missing_glyph_false();
        bool t3 = test_family_slots_full_fallback();
        bool t4 = test_corrupt_family_latch();
        bool t5 = test_size_cycle_family_face();
        bool t6 = test_sink_roundtrip_and_big_upem();
        bool t7 = test_ft_face_copy_move_branch_preserved();
        bool t8 = test_hint_tier_small_px();
        bool all = t1 && t2 && t3 && t4 && t5 && t6 && t7 && t8;
        XPrintf("XFontFt 专项测试: closed_form=%s missing_glyph=%s "
                "family_slots=%s corrupt_latch=%s size_cycle=%s sink=%s "
                "face_copy_move=%s hint_tier=%s => %s\n",
                t1 ? "通过" : "失败", t2 ? "通过" : "失败",
                t3 ? "通过" : "失败", t4 ? "通过" : "失败",
                t5 ? "通过" : "失败", t6 ? "通过" : "失败",
                t7 ? "通过" : "失败", t8 ? "通过" : "失败",
                all ? "通过" : "失败");
        result = all ? 0 : 1;
    }
    return result;
#endif
}

#if DEMOTEST
static void xfontft_test_wrapper(XVariant* data)
{
    (void)data;
    XFontFtTest_run();
}

void XTestMenu_XFontFtTest(XTestMenu* root)
{
    XTestMenu* menu = XTestMenu_create("XFontFtTest");
    XTestMenu_addMenu(root, menu);
    XAction* action = XTestMenu_addAction(menu, "XFontFt FT 文件后端专项测试");
    XTestMenu_setActionFunction(action, xfontft_test_wrapper);
}
#endif
