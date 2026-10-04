/******************************************************************************
 * @file       xgui_font_scan_tool.c
 * @brief      XGui offscreen full-font glyph inspection tool (SW-AA outline).
 * @details    Renders ASCII 0x20-0x7E plus the GB2312 hanzi set (qu 16-87,
 *             wei 1-94, formed as GBK double bytes and converted through the
 *             existing XChar_fromGbkStream entry; falls back to a direct
 *             Unicode scan when the GBK conversion cannot map the probe
 *             glyph) with a 16px outline XFont into a 32x32 offscreen ARGB32
 *             XImage (white background, black ink) via XPainter_begin_image.
 *             That is the XPainterDevice_Image software-AA glyph pipeline
 *             (painterDrawOutlineGlyphSoftwareAA including the
 *             painterOutlineGridFitStrokesY stroke grid fit) - the same path
 *             a window backing store uses.
 *
 *             Codepoints absent from the face are skipped before judging
 *             (cmap probe through XFontFace_loadOutlineGlyph_base with a
 *             NULL sink = metrics-only query; the GBK codepage maps five
 *             extension slots to U+E810-E814 which the built-in outline
 *             face does not cover): criterion A stays about real blank
 *             regressions of glyphs the font actually has.
 *
 *             Judgements per glyph (luma = Rec.601 of the ARGB32 pixel):
 *               A) zero ink: no pixel with luma < 100;
 *               B) collapsed ink: dark (luma < 200) pixel count below
 *                  max(3, inkBboxW*inkBboxH/12) -- scaled to the rendered
 *                  glyph size so thin 1px punctuation (hyphen, underscore,
 *                  colon, quotes, comma, backtick) is not flagged while a
 *                  hollow full-size glyph still needs 12+ px of ink
 *                  (relaxed to at most 6 for the sparse glyphs
 *                  U+4E00/U+4E8C/U+4E09);
 *               C) horizontal-band collapse: rows carrying >= 0.7 * bboxW
 *                  dark pixels are "band rows" (vertical-bar rows hold
 *                  ~1-2 px and never qualify), consecutive band rows
 *                  pairwise identical in all 32 luma values fold into one
 *                  equal-run; a run of thickness >= 3 fails -- legal
 *                  gridfit output tops out at 2px-thick strokes
 *                  (XPainter.c painterOutlineGridFitStrokesY width is
 *                  max(1, round(w)), one design stroke per band at 16px
 *                  Normal), so thickness 3 can only come from three design
 *                  horizontal strokes collapsed onto the same rows.  The
 *                  v1 "band pair count >= 2" rule was retired: the legal
 *                  glyph U+81EA alone contains two separate 2px equal
 *                  bands, and separating "two/three" (U+4E8C/U+4E09) from
 *                  a collapsed duplicate stroke is impossible in the
 *                  bitmap domain alone.
 *             First failing criterion in A/B/C order is reported.
 *
 *             Output: out/scan_bad_1.txt ("U+XXXX <letter>" per bad glyph),
 *             out/scan_bad/U+XXXX.png (ink bbox + 2px margin crop),
 *             stdout summary.  Exit code = min(bad, 250).
 * @note       The output root resolves to <exe dir>/../out so the tool can
 *             be launched from any working directory.  U+0020 (space) is
 *             rendered-counted but exempt from judging (structurally blank).
 *             GBK slots that do not map to a codepoint (U+FFFD) are skipped.
 * @author     XinYueC team
 ******************************************************************************/
#include "XImage.h"
#include "XPainter.h"
#include "XChar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define SCAN_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define SCAN_MKDIR(p) mkdir(p, 0777)
#endif

#define SCAN_CANVAS         32    /* per-glyph offscreen canvas (square)  */
#define SCAN_ORIGIN_X       8     /* draw x: centers a 16px em in 32px    */
#define SCAN_BASELINE_Y     24    /* baseline leaves ascent+descender room*/
#define SCAN_FONT_PIXELS    16    /* outline font pixel size              */
#define SCAN_LUMA_INK       100   /* criterion A darkness threshold       */
#define SCAN_LUMA_DARK      200   /* criterion B/C darkness threshold     */
#define SCAN_MIN_INK_FLOOR  3     /* criterion B floor (thin punctuation) */
#define SCAN_MIN_INK_DIV    12    /* criterion B: bbox area / 12          */
#define SCAN_MIN_INK_SPARSE 6     /* criterion B relaxed for sparse glyphs*/
#define SCAN_BAND_ROW_NUM   7     /* criterion C: band row >= bboxW*7/10  */
#define SCAN_BAND_ROW_DEN   10
#define SCAN_BAND_MIN_W     4     /* criterion C needs a wide-enough glyph*/
#define SCAN_BAND_BAD_THICK 3     /* equal-run thickness to fail          */
#define SCAN_CP_MAX         24576
#define SCAN_PATH_MAX       700
#define SCAN_FIRST_MAX      20

static uint32_t g_cps[SCAN_CP_MAX];
static int      g_cpCount  = 0;
static int      g_gbkPath  = 0;   /* 1 = GBK conversion, 0 = direct Unicode */
static int      g_unmapped = 0;   /* GBK slots without a codepoint          */
static int      g_pngFails = 0;
static int      g_boldMode = 0;   /* 1 = judging the bold pipeline        */

static bool scanAddCp(uint32_t cp)
{
    if (g_cpCount >= SCAN_CP_MAX) return false;
    g_cps[g_cpCount++] = cp;
    return true;
}

/**
 * @brief Build the scan codepoint list.
 * @details Primary scope: ASCII 0x20-0x7E plus GB2312 hanzi (qu 16-87,
 *          wei 1-94, qu+0xA0/wei+0xA0 as GBK double bytes) converted via the
 *          existing XChar_fromGbkStream entry.  The conversion is probed with
 *          qu16/wei1 (GBK 0xB0A1 = the first GB2312 hanzi "a", U+554A -- the
 *          GB2312 hanzi block is NOT ordered by Unicode, U+4E00 sits at
 *          qu50/wei27 = GBK 0xD2BB); when the probe fails the tool falls back
 *          to scanning Unicode 0x4E00-0x9FA5 plus 0x3000-0x303F and
 *          0xFF00-0xFF5E directly.
 */
static void scanBuildCodepoints(void)
{
    char gbk[3];
    XChar out[4];
    int64_t n;
    uint32_t qu, wei, cp;

    gbk[0] = (char)0xB0;
    gbk[1] = (char)0xA1;
    gbk[2] = '\0';
    n = XChar_fromGbkStream(gbk, 0, out, 4);
    if (n >= 1 && (uint32_t)out[0] == 0x554Au)
        g_gbkPath = 1;

    for (cp = 0x20u; cp <= 0x7Eu; ++cp)
        (void)scanAddCp(cp);

    if (g_gbkPath)
    {
        for (qu = 16; qu <= 87; ++qu)
        {
            for (wei = 1; wei <= 94; ++wei)
            {
                gbk[0] = (char)((qu + 0xA0) & 0xFF);
                gbk[1] = (char)((wei + 0xA0) & 0xFF);
                gbk[2] = '\0';
                n = XChar_fromGbkStream(gbk, 0, out, 4);
                if (n >= 1 && (uint32_t)out[0] != 0u &&
                    (uint32_t)out[0] != 0xFFFDu)
                    (void)scanAddCp((uint32_t)out[0]);
                else
                    ++g_unmapped;
            }
        }
    }
    else
    {
        for (cp = 0x4E00u; cp <= 0x9FA5u; ++cp) (void)scanAddCp(cp);
        for (cp = 0x3000u; cp <= 0x303Fu; ++cp) (void)scanAddCp(cp);
        for (cp = 0xFF00u; cp <= 0xFF5Eu; ++cp) (void)scanAddCp(cp);
    }
}

static int scanLuma(uint32_t argb)
{
    int r = (int)((argb >> 16) & 0xFFu);
    int g = (int)((argb >> 8) & 0xFFu);
    int b = (int)(argb & 0xFFu);
    return (r * 299 + g * 587 + b * 114) / 1000;
}

/* Criterion B relaxation list (sparse-stroke hanzi). */
static bool scanCpSparse(uint32_t cp)
{
    return cp == 0x4E00u || cp == 0x4E8Cu || cp == 0x4E09u;
}

/**
 * @brief Criterion B ink threshold scaled to the rendered glyph size.
 * @details max(3, area/12): a 4x1 hyphen (area 4, ink 4) passes at floor 3,
 *          an 8x1 underscore (area 8, ink 8) passes, while a full-size
 *          hollow glyph keeps the original ~12-px strength (area >= 144).
 *          The sparse relaxation stays capped at 6.
 */
static int scanMinInk(uint32_t cp, int bboxW, int bboxH)
{
    int area = bboxW * bboxH;
    int scaled = area / SCAN_MIN_INK_DIV;
    int limit = (scaled > SCAN_MIN_INK_FLOOR) ? scaled : SCAN_MIN_INK_FLOOR;
    if (scanCpSparse(cp) && limit > SCAN_MIN_INK_SPARSE)
        limit = SCAN_MIN_INK_SPARSE;
    return limit;
}

/**
 * @brief Judge one rendered 32x32 canvas.
 * @param img      rendered canvas (white background, black ink)
 * @param cp       codepoint under judgement (drives the B relaxation)
 * @param ink100   out: pixels with luma < 100
 * @param ink200   out: pixels with luma < 200
 * @param bandsOut out: max thickness of an equal horizontal band run
 * @param bbox     out: ink bbox over luma<200 pixels (min/max x/y)
 * @return 'A'/'B'/'C' for the first failing criterion, 0 when the glyph ok.
 */
static char scanJudgeBitmap(const XImage* img, uint32_t cp,
                            int* ink100, int* ink200, int* bandsOut,
                            int* minX, int* minY, int* maxX, int* maxY)
{
    int luma[SCAN_CANVAS][SCAN_CANVAS];
    int rowInk[SCAN_CANVAS];
    int x, y, dark100, dark200, bands, lox, loy, hix, hiy, bboxW, bboxH;
    char letter = 0;

    dark100 = 0;
    dark200 = 0;
    bands = 0;
    lox = SCAN_CANVAS; loy = SCAN_CANVAS;
    hix = -1;          hiy = -1;
    for (y = 0; y < SCAN_CANVAS; ++y)
    {
        rowInk[y] = 0;
        for (x = 0; x < SCAN_CANVAS; ++x)
        {
            int l = scanLuma(XImage_pixel(img, x, y));
            luma[y][x] = l;
            if (l < SCAN_LUMA_DARK)
            {
                ++rowInk[y];
                ++dark200;
                if (x < lox) lox = x;
                if (x > hix) hix = x;
                if (y < loy) loy = y;
                if (y > hiy) hiy = y;
            }
            if (l < SCAN_LUMA_INK) ++dark100;
        }
    }
    *ink100 = dark100;
    *ink200 = dark200;
    *bandsOut = bands;
    *minX = lox; *minY = loy; *maxX = hix; *maxY = hiy;

    if (dark100 == 0)
        letter = 'A';
    else
    {
        bboxW = (hix >= lox) ? (hix - lox + 1) : 0;
        bboxH = (hiy >= loy) ? (hiy - loy + 1) : 0;
        if (dark200 < scanMinInk(cp, bboxW, bboxH))
            letter = 'B';
    }

    /* Criterion C: horizontal-band collapse.  A band row carries at least
       0.7 * bboxW dark pixels, so 1-2px vertical-bar rows never qualify;
       consecutive pairwise-identical band rows fold into one equal run and
       only a run >= 3 rows thick fails (legal gridfit strokes are at most
       2px thick at 16px; three design strokes on one y is the collapse
       signature).  Glyphs narrower than SCAN_BAND_MIN_W cannot distinguish
       bands from bars and are exempt (the v1 pair-count rule flagged the
       pure vertical glyphs 丨/刂 and the letter-form i/l). */
    bboxW = (hix >= lox) ? (hix - lox + 1) : 0;
    /* Bold mode: the lift LUT flattens sub-row AA differences into
       identical solid rows, so small full-stroke glyphs (* , ; ? { and
       dense CJK radicals) legitimately produce >= 3 identical rows -
       that is the bold read, not a collapse. Criterion C only judges
       the regular weight; bold runs rely on A/B plus visual check. */
    if (letter == 0 && !g_boldMode && bboxW >= SCAN_BAND_MIN_W)
    {
        int run = 0;
        for (y = 0; y < SCAN_CANVAS; ++y)
        {
            bool rowIdentical;
            if (SCAN_BAND_ROW_DEN * rowInk[y] <
                SCAN_BAND_ROW_NUM * bboxW)
            {
                run = 0; /* a non-band row breaks the run */
                continue;
            }
            rowIdentical = (y + 1 < SCAN_CANVAS);
            for (x = 0; rowIdentical && x < SCAN_CANVAS; ++x)
                if (luma[y][x] != luma[y + 1][x])
                    rowIdentical = false;
            if (rowIdentical)
            {
                ++run;
                if (run + 1 > bands) bands = run + 1;
            }
            else
                run = 0;
        }
        *bandsOut = bands;
        if (bands >= SCAN_BAND_BAD_THICK) letter = 'C';
    }
    return letter;
}

/**
 * @brief Count distinct horizontal band rows (the criterion-D metric).
 * @details A band row carries >= 0.7 * bboxW dark pixels; vertically
 *          adjacent band rows whose pixels are all identical fold into
 *          one band (a 2px-thick snapped stroke is ONE band, not two).
 *          Used for both the gridfit-off reference pass and the aligned
 *          pass so the counts are comparable; a design stroke lost to
 *          alignment removes one band from the count.
 */
static int scanCountBands(const XImage* img, int minX, int maxX)
{
    int y, x;
    int bw = maxX - minX + 1;
    int bands = 0;
    int run = 0;
    bool prevIdentical = false;
    int prevRow[SCAN_CANVAS];
    if (bw < SCAN_BAND_MIN_W) return 0;
    for (y = 0; y < SCAN_CANVAS; ++y)
    {
        int cnt = 0;
        bool isBand;
        bool identical = true;
        for (x = 0; x < SCAN_CANVAS; ++x)
        {
            int l = scanLuma(XImage_pixel(img, x, y));
            if (l < SCAN_LUMA_DARK) ++cnt;
            if (y > 0 && l != prevRow[x]) identical = false;
        }
        for (x = 0; x < SCAN_CANVAS; ++x)
            prevRow[x] = scanLuma(XImage_pixel(img, x, y));
        isBand = (SCAN_BAND_ROW_DEN * cnt >= SCAN_BAND_ROW_NUM * bw);
        if (isBand && prevIdentical && run > 0)
        {
            /* same band continues across an identical row boundary */
        }
        else if (isBand)
        {
            if (run == 0) ++bands;
            ++run;
        }
        if (!isBand) run = 0;
        prevIdentical = identical;
    }
    return bands;
}

/**
 * @brief True when the loaded face actually contains a glyph for cp.
 * @details Metrics-only cmap probe: XFontFace_loadOutlineGlyph_base with a
 *          NULL sink returns the glyph metrics without building a path
 *          (XFontOutline_Xfo_loadGlyph fails directly for codepoints absent
 *          from the face cmap).  Used to skip GBK slots whose system
 *          codepage maps them to codepoints the outline face does not
 *          cover (e.g. U+E810-E814) instead of misreporting them as
 *          criterion-A blanks.
 */
static bool scanFaceHasGlyph(const XFont* font, uint32_t cp)
{
    const XFontFace* face = XFont_face(font);
    XFontOutlineGlyphMetrics metrics;
    if (!face) return false;
    XMemset(&metrics, 0, sizeof(metrics));
    return XFontFace_loadOutlineGlyph_base(face, font, cp, &metrics, NULL);
}

/**
 * @brief Render one glyph into the canvas (white fill + black drawText).
 * @return false when the painter bind/draw/end chain fails.
 */
static bool scanRender(XPainter* painter, XImage* img, XFont* font,
                       const char* utf8)
{
    XImage_fillRect(img, NULL, 0xFFFFFFFFu);
    if (!XPainter_begin_image(painter, img)) return false;
    XPainter_setFont(painter, font);
    if (!XPainter_drawText(painter, SCAN_ORIGIN_X, SCAN_BASELINE_Y, utf8,
                           0xFF000000u))
        return false;
    if (!XPainter_end(painter)) return false;
    return true;
}

/**
 * @brief Debug aid: SCAN_DUMP="U+4E00,U+9879,..." saves those canvases to
 *        out/scan_dump/U+XXXX.png regardless of the verdict (attribution
 *        helper; not part of the pass/fail pipeline).
 */
static void scanDumpRequested(XPainter* painter, XImage* img, XFont* font,
                              const char* outDir)
{
    const char* spec;
    char dumpDir[SCAN_PATH_MAX];
    spec = getenv("SCAN_DUMP");
    if (!spec || !spec[0]) return;
    snprintf(dumpDir, sizeof(dumpDir), "%s/scan_dump", outDir);
    SCAN_MKDIR(dumpDir);
    while (*spec)
    {
        char token[16];
        uint32_t cp = 0;
        size_t t = 0;
        while (*spec == ',' || *spec == ' ' || *spec == ';') ++spec;
        while (*spec && *spec != ',' && *spec != ';' && *spec != ' ' &&
               t + 1 < sizeof(token))
            token[t++] = *spec++;
        token[t] = '\0';
        if (t >= 5 && (token[0] == 'U' || token[0] == 'u') && token[1] == '+')
            cp = (uint32_t)strtoul(token + 2, NULL, 16);
        if (cp == 0) continue;
        {
            XChar xc = (XChar)cp;
            uint8_t u8[8];
            int64_t n = XChar_toUtf8Stream(&xc, 1, u8, (size_t)(sizeof(u8) - 1));
            int ink100 = 0, ink200 = 0, bands = 0;
            int mnx = 0, mny = 0, mxx = 0, mxy = 0;
            if (n <= 0) continue;
            u8[n] = '\0';
            if (!scanRender(painter, img, font, (const char*)u8)) continue;
            (void)scanJudgeBitmap(img, cp, &ink100, &ink200, &bands,
                                  &mnx, &mny, &mxx, &mxy);
            {
                char path[SCAN_PATH_MAX];
                XImage crop;
                int x0, y0, x1, y1, w, h, x, y;
                if (mxx < mnx || mxy < mny)
                {
                    x0 = 0; y0 = 0;
                    x1 = SCAN_CANVAS - 1; y1 = SCAN_CANVAS - 1;
                }
                else
                {
                    x0 = (mnx - 2 < 0) ? 0 : mnx - 2;
                    y0 = (mny - 2 < 0) ? 0 : mny - 2;
                    x1 = (mxx + 2 > SCAN_CANVAS - 1) ? SCAN_CANVAS - 1 : mxx + 2;
                    y1 = (mxy + 2 > SCAN_CANVAS - 1) ? SCAN_CANVAS - 1 : mxy + 2;
                }
                w = x1 - x0 + 1;
                h = y1 - y0 + 1;
                XImage_init_ex(&crop, w, h, XImageFormat_ARGB32);
                for (y = 0; y < h; ++y)
                    for (x = 0; x < w; ++x)
                        XImage_setPixel(&crop, x, y,
                                        XImage_pixel(img, x0 + x, y0 + y));
                snprintf(path, sizeof(path), "%s/U+%04X.png", dumpDir,
                         (unsigned)cp);
                if (!XImage_save_2(&crop, path, "PNG", -1)) ++g_pngFails;
                fprintf(stderr,
                        "font-scan: dump U+%04X ink100=%d ink200=%d bands=%d "
                        "-> %s\n", (unsigned)cp, ink100, ink200, bands, path);
                XClassDeinit(&crop);
            }
        }
    }
}

/**
 * @brief Save a bad glyph sample cropped to the ink bbox expanded by 2px.
 */
static bool scanSaveBadPng(const XImage* img, uint32_t cp,
                           int minX, int minY, int maxX, int maxY,
                           const char* badDir)
{
    XImage crop;
    char path[SCAN_PATH_MAX];
    int x0, y0, x1, y1, w, h, x, y;
    bool ok;

    if (maxX < minX || maxY < minY)
    {
        x0 = 0; y0 = 0;
        x1 = SCAN_CANVAS - 1; y1 = SCAN_CANVAS - 1;
    }
    else
    {
        x0 = (minX - 2 < 0) ? 0 : minX - 2;
        y0 = (minY - 2 < 0) ? 0 : minY - 2;
        x1 = (maxX + 2 > SCAN_CANVAS - 1) ? SCAN_CANVAS - 1 : maxX + 2;
        y1 = (maxY + 2 > SCAN_CANVAS - 1) ? SCAN_CANVAS - 1 : maxY + 2;
    }
    w = x1 - x0 + 1;
    h = y1 - y0 + 1;
    XImage_init_ex(&crop, w, h, XImageFormat_ARGB32);
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            XImage_setPixel(&crop, x, y, XImage_pixel(img, x0 + x, y0 + y));
    snprintf(path, sizeof(path), "%s/U+%04X.png", badDir, (unsigned)cp);
    ok = XImage_save_2(&crop, path, "PNG", -1);
    if (!ok) ++g_pngFails;
    XClassDeinit(&crop);
    return ok;
}

/**
 * @brief Self-test: feed synthetic canvases through scanJudgeBitmap and
 *        require every criterion to fire.  Guards against silent criterion
 *        regressions (the v2 ratio expression was first written inverted,
 *        which made criterion C unreachable while still reporting bad=0).
 * @return number of failed self-checks (0 = all criteria armed).
 */
static int scanCriteriaSelfTest(void)
{
    XImage img;
    int failures = 0;
    int ink100, ink200, bands, mnx, mny, mxx, mxy;

    /* Blank canvas -> A (no pixel below luma 100). */
    XImage_init_ex(&img, SCAN_CANVAS, SCAN_CANVAS, XImageFormat_ARGB32);
    XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
    if (scanJudgeBitmap(&img, 0x4E00u, &ink100, &ink200, &bands,
                        &mnx, &mny, &mxx, &mxy) != 'A')
        ++failures;

    /* 2x1 dot -> B (area 2 -> floor threshold 3, ink 2 < 3); the 3x3 dot
       (ink 9 >= 3) and the 4x1 hyphen (ink 4 >= 3) must pass. */
    XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
    {
        XRect dot = { 10, 10, 2, 1 };
        XImage_fillRect(&img, &dot, 0xFF000000u);
    }
    if (scanJudgeBitmap(&img, 0x4E00u, &ink100, &ink200, &bands,
                        &mnx, &mny, &mxx, &mxy) != 'B')
        ++failures;
    {
        XRect hyphen = { 10, 10, 4, 1 };
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        XImage_fillRect(&img, &hyphen, 0xFF000000u);
        if (scanJudgeBitmap(&img, 0x002Du, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy) != 0)
            ++failures;
    }

    /* Three identical 16px band rows -> C (thickness 3). */
    {
        XRect bar = { 4, 0, 16, 1 };
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        bar.y = 8;
        XImage_fillRect(&img, &bar, 0xFF000000u);
        bar.y = 9;
        XImage_fillRect(&img, &bar, 0xFF000000u);
        bar.y = 10;
        XImage_fillRect(&img, &bar, 0xFF000000u);
        if (scanJudgeBitmap(&img, 0x4E00u, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy) != 'C')
            ++failures;
    }
    /* Two identical band rows (legal gridfit 2px stroke) -> pass. */
    {
        XRect bar = { 4, 0, 16, 1 };
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        bar.y = 8;
        XImage_fillRect(&img, &bar, 0xFF000000u);
        bar.y = 9;
        XImage_fillRect(&img, &bar, 0xFF000000u);
        if (scanJudgeBitmap(&img, 0x4E00u, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy) != 0)
            ++failures;
    }
    /* Vertical bar (1px column, 20 rows tall) -> pass (never a band row). */
    {
        XRect column = { 12, 2, 1, 20 };
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        XImage_fillRect(&img, &column, 0xFF000000u);
        if (scanJudgeBitmap(&img, 0x4E28u, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy) != 0)
            ++failures;
    }
    XClassDeinit(&img);
    return failures;
}

/**
 * @brief Resolve the output root as <exe dir>/../out (repo layout), so the
 *        tool writes the repo out/ tree regardless of the launch directory.
 */
static void scanResolveOutDir(char* buf, size_t cap)
{
#ifdef _WIN32
    char exe[SCAN_PATH_MAX];
    DWORD n = GetModuleFileNameA(NULL, exe, (DWORD)sizeof(exe));
    char* slash;
    exe[sizeof(exe) - 1] = '\0';
    slash = (n > 0 && n < (DWORD)sizeof(exe)) ? strrchr(exe, '\\') : NULL;
    if (!slash) slash = (n > 0 && n < (DWORD)sizeof(exe)) ? strrchr(exe, '/') : NULL;
    if (slash)
    {
        *slash = '\0';
        snprintf(buf, cap, "%s/../out", exe);
        return;
    }
#endif
    snprintf(buf, cap, "out");
}

int main(int argc, char** argv)
{
    XFont* font;
    XImage img;
    XPainter painter;
    char outDir[SCAN_PATH_MAX];
    char badDir[SCAN_PATH_MAX];
    char listPath[SCAN_PATH_MAX];
    char first20[SCAN_FIRST_MAX][16];
    FILE* list;
    int i;
    int scanned = 0, badCount = 0, blankSkipped = 0, noGlyphSkipped = 0;
    int first20Count = 0;
    int exitCode;
    /* Criterion D (stroke-loss vs design) plumbing: "--ref" mode renders
       with XGUI_TEXT_GRIDFIT=0 (the runtime escape hatch in XPainter)
       and records each glyph's band-row count to scan_ref.txt; the
       default mode loads that table and fails any glyph whose aligned
       render has >= 2 fewer band rows than the unaligned reference -
       the signature of a horizontal stroke swallowed by grid-fit
       (yan 0x6F14, zui 0x6700: interior bars vanished while total ink
       stayed above criterion B's floor). */
    int refMode = (argc > 1 && strcmp(argv[1], "--ref") == 0);
    int boldMode = (argc > 1 && strcmp(argv[1], "--bold") == 0);
    FILE* refFile = NULL;
    static int refBands[SCAN_CP_MAX];

    for (i = 0; i < SCAN_CP_MAX; ++i) refBands[i] = -1;

    scanResolveOutDir(outDir, sizeof(outDir));
    snprintf(badDir, sizeof(badDir), "%s/scan_bad", outDir);
    snprintf(listPath, sizeof(listPath), "%s/scan_bad_1.txt", outDir);
    SCAN_MKDIR(outDir);
    SCAN_MKDIR(badDir);
    /* Reference mode renders the design geometry: kill grid-fit before
       the first glyph render caches XPainter's one-shot override. */
    if (refMode) _putenv("XGUI_TEXT_GRIDFIT=0");

    scanBuildCodepoints();
    fprintf(stderr, "font-scan: codepoints=%d path=%s unmappedGbkSlots=%d\n",
            g_cpCount,
            g_gbkPath ? "GBK(XChar_fromGbkStream)" : "direct-unicode-fallback",
            g_unmapped);

    font = XFont_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, -1, -1, false);
    if (!font)
    {
        fprintf(stderr, "font-scan: XFont_create_ex failed\n");
        return 2;
    }
    XFont_setPixelSize(font, SCAN_FONT_PIXELS);
    if (boldMode)
    {
        XFont_setBold(font, true);
        g_boldMode = 1;
    }
    fprintf(stderr, "font-scan: family=%s pixelSize=%d bold=%d\n",
            XFont_family(font), XFont_pixelSize(font),
            boldMode ? XFont_bold(font) : 0);

    XImage_init_ex(&img, SCAN_CANVAS, SCAN_CANVAS, XImageFormat_ARGB32);
    XPainter_init(&painter, NULL);

    /* Smoke probe: 'A' must produce ink, proving the outline face loaded
       (exe-dir anchored ../Library/XFont lookup) and the SW-AA path ran. */
    {
        char aUtf8[2];
        int ink100 = 0, ink200 = 0, bands = 0;
        int mnx = 0, mny = 0, mxx = 0, mxy = 0;
        aUtf8[0] = 'A';
        aUtf8[1] = '\0';
        if (!scanRender(&painter, &img, font, aUtf8))
        {
            fprintf(stderr, "font-scan: smoke render failed\n");
            return 2;
        }
        (void)scanJudgeBitmap(&img, 0x41u, &ink100, &ink200, &bands,
                              &mnx, &mny, &mxx, &mxy);
        fprintf(stderr, "font-scan: smoke 'A' ink100=%d ink200=%d\n",
                ink100, ink200);
        if (ink200 <= 0)
        {
            fprintf(stderr, "font-scan: outline font produced no ink "
                            "(face load failed?)\n");
            return 3;
        }
    }

    scanDumpRequested(&painter, &img, font, outDir);

    if (refMode)
    {
        char refPath[SCAN_PATH_MAX];
        snprintf(refPath, sizeof(refPath), "%s/scan_ref.txt", outDir);
        refFile = fopen(refPath, "w");
        if (!refFile)
        {
            fprintf(stderr, "font-scan: cannot open %s\n", refPath);
            return 2;
        }
    }
    else
    {
        char refPath[SCAN_PATH_MAX];
        snprintf(refPath, sizeof(refPath), "%s/scan_ref.txt", outDir);
        refFile = fopen(refPath, "r");
        if (refFile)
        {
            int idx = 0;
            int value = 0;
            while (idx < SCAN_CP_MAX && fscanf(refFile, "%d", &value) == 1)
                refBands[idx++] = value;
            fclose(refFile);
            if (idx != g_cpCount)
                fprintf(stderr,
                        "font-scan: ref table %d entries != %d cps, "
                        "criterion D disarmed\n",
                        idx, g_cpCount);
        }
        else
        {
            fprintf(stderr,
                    "font-scan: no ref table (run with --ref first), "
                    "criterion D disarmed\n");
        }
    }

    list = fopen(listPath, "w");
    if (!list)
    {
        fprintf(stderr, "font-scan: cannot open %s\n", listPath);
        return 2;
    }

    for (i = 0; i < g_cpCount; ++i)
    {
        uint32_t cp = g_cps[i];
        XChar xc;
        uint8_t u8[8];
        int64_t n;
        char letter;
        int ink100 = 0, ink200 = 0, bands = 0;
        int mnx = 0, mny = 0, mxx = 0, mxy = 0;

        /* Space is structurally blank in any font: render-count it, but do
           not judge it (it would flag criterion A on every run). */
        if (cp == 0x20u)
        {
            ++blankSkipped;
            continue;
        }
        /* Codepoints the face does not cover: the GBK codepage maps some
           slots (E810-E814 PUA etc.) outside the face cmap.  Skipping them
           keeps criterion A about real blank regressions of glyphs the
           font actually has. */
        if (!scanFaceHasGlyph(font, cp))
        {
            ++noGlyphSkipped;
            continue;
        }
        xc = (XChar)cp; /* the scan set is BMP-only (ASCII + GB2312) */
        n = XChar_toUtf8Stream(&xc, 1, u8, (size_t)(sizeof(u8) - 1));
        if (n <= 0)
        {
            fprintf(stderr, "font-scan: utf8 encode failed U+%04X\n",
                    (unsigned)cp);
            continue;
        }
        u8[n] = '\0';

        if (!scanRender(&painter, &img, font, (const char*)u8))
        {
            fprintf(stderr, "font-scan: render failed at U+%04X\n",
                    (unsigned)cp);
            fclose(list);
            return 2;
        }
        letter = scanJudgeBitmap(&img, cp, &ink100, &ink200, &bands,
                                 &mnx, &mny, &mxx, &mxy);
        if (refMode)
        {
            fprintf(refFile, "%d\n",
                    scanCountBands(&img, mnx, mxx));
            ++scanned;
            continue;
        }
        ++scanned;
        if (letter == 0 && refBands[i] >= 0)
        {
            int loBands = scanCountBands(&img, mnx, mxx);
            /* ±1 band-row wobble is normal sub-pixel AA refolding; only
               a >= 2 band loss (a whole stroke vanishing) is verdict
               material. Sub-one-stroke losses remain covered by visual
               acceptance on the UI pages. */
            if (refBands[i] - loBands >= 2) letter = 'D';
        }
        if (letter == 0) continue;

        ++badCount;
        fprintf(list, "U+%04X %c\n", (unsigned)cp, letter);
        if (first20Count < SCAN_FIRST_MAX)
        {
            snprintf(first20[first20Count], sizeof(first20[0]), "U+%04X %c",
                     (unsigned)cp, letter);
            ++first20Count;
        }
        (void)scanSaveBadPng(&img, cp, mnx, mny, mxx, mxy, badDir);
        if ((scanned % 1000) == 0)
            fprintf(stderr, "font-scan: %d scanned, %d bad\n", scanned,
                    badCount);
    }
    fclose(list);
    if (refMode && refFile) fclose(refFile);

    XPainter_deinit(&painter);
    XClassDeinit(&img);
    XClassDelete((XClass*)font);

    if (refMode)
    {
        printf("font-scan: reference pass done, %d band counts written\n",
               scanned);
        return 0;
    }
    printf("font-scan: codepoint path=%s unmappedGbkSlots=%d blankSkipped=%d "
           "noGlyphSkipped=%d\n",
           g_gbkPath ? "GBK" : "direct-unicode", g_unmapped, blankSkipped,
           noGlyphSkipped);
    printf("font-scan: scanned=%d bad=%d pngFails=%d\n",
           scanned, badCount, g_pngFails);
    printf("font-scan: bad list=%s\n", listPath);
    printf("font-scan: bad samples=%s\n", badDir);
    printf("font-scan: first %d bad:", first20Count);
    for (i = 0; i < first20Count; ++i)
        printf(" %s", first20[i]);
    printf("\n");

    exitCode = (badCount < 250) ? badCount : 250;
    {
        int selfTest = scanCriteriaSelfTest();
        printf("font-scan: criteria self-test failures=%d\n", selfTest);
        if (selfTest > 0 && exitCode == 0) exitCode = 250;
    }
    return exitCode;
}
