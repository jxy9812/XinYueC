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
 *               E) horizontal-band fade-out (2026-10-05, dual rule): the
 *                  LITERAL rule folds consecutive band rows (>= 0.7 * bboxW
 *                  pixels luma < 200) into a band group and requires one
 *                  row per group with luma < 100 pixel count >= 0.5 * bboxW
 *                  -- an all-gray group is a stroke that lost its grid-fit
 *                  snap and stayed at ~50% coverage.  The DASHED auxiliary
 *                  rule fires when a row holds TWO OR MORE gray runs
 *                  (100 <= luma < 200) each flanked by solid columns
 *                  (luma < 100) -- the "stroke vanishes between black
 *                  anchors" signature (#++#++# / ######++++++++#).  The
 *                  calibration sets are not separable by the literal rule
 *                  alone (chu's faded fringe rows are pixel-identical to
 *                  zi/kou's legal uniform fringes; shi U+89C6 carries no
 *                  16px bitmap signature at all), so the gate verdict =
 *                  literal OR dashed, uniform single-run fringes never
 *                  fire, and jian U+4EF6 / shi U+89C6 are knowingly left
 *                  to the post-fix page visual recheck.  Bold mode is
 *                  exempt together with C.
 *             First failing criterion in A/B/C/E order is reported (D
 *             needs the gridfit-off reference table and is applied in
 *             main() when A/B/C/E all pass).
 *
 *             Output: out/scan_bad_1.txt ("U+XXXX <letter>" per bad glyph),
 *             out/scan_bad/U+XXXX.png (ink bbox + 2px margin crop),
 *             stdout summary.  Exit code = min(bad, 250).
 *
 *             Provider mode ([collapsed 2026-10-07]): the xfo1/ft2/both
 *             tri-mode is gone with the XFO1 implementation -- FreeType
 *             (XFontFt) is the only outline provider, so the scan always
 *             exercises the FT chain and the legacy output names
 *             (scan_bad_1.txt, scan_bad/, scan_ref.txt) apply unchanged.
 *             --family=<name|path> (or env SCAN_FONT_FAMILY) feeds that
 *             family to XFont_create_ex -- the desktop gate passes the
 *             fc-match-resolved system Noto path (design §7.1: direct-path
 *             family, file never enters the repo); a missing font file
 *             falls back to the registered bitmap face with a one-line
 *             library warning.  --max=N caps the scan set after the
 *             codepoint list is built (fast smoke; a criterion-D table
 *             must be rebuilt at the same cap or the count check disarms
 *             D).
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
#include <process.h> /* _spawnv: --provider=both child runs */
#define SCAN_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>  /* fork/execv/_exit: --provider=both child runs */
#include <sys/wait.h>
#include <errno.h>
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
#define SCAN_BAND_DARK_NUM  5     /* criterion E: dark row >= bboxW*5/10  */
#define SCAN_BAND_DARK_DEN  10
#define SCAN_BAND_BAD_THICK 3     /* equal-run thickness to fail @16px    */
#define SCAN_CANVAS_MAX     64    /* canvas upper bound (px<=30 -> 60)    */
#define SCAN_CP_MAX         24576
#define SCAN_PATH_MAX       700
#define SCAN_FIRST_MAX      20

/* Multi-size support (2026-10-05): the page fonts render at many pixel
   sizes (nav ~13-14px, titles ~12px, body 14-16px) while the scan ran a
   fixed 16px em -- the size where criterion D was calibrated.  --px=N
   re-runs the whole pipeline at N px: canvas 2N, origin N/2, baseline
   1.5N, and the criterion-C legal-thickness cap scaled to the grid-fit
   rounding (max(1, round(w)) grows with the em).  px=16 keeps the legacy
   output names so existing gates stay byte-compatible. */
static int g_px = SCAN_FONT_PIXELS;

static int scanCanvas(void)
{
    return g_px * 2;
}

static int scanOriginX(void)
{
    return g_px / 2;
}

static int scanBaseY(void)
{
    return g_px + g_px / 2;
}

/* Equal-run thickness that fails criterion C at this em: a legal grid-fit
   stroke is at most max(1, round(w)) rows thick with w ~ px/8 design
   strokes -> 16px:2, 12px:2, 24px:3, 32px:4.  One thicker can only be
   multiple design strokes collapsed onto the same rows, so the failing
   threshold is ceiling+1 (16px -> 3, matching the historical constant). */
static int scanBandBadThick(void)
{
    int cap = (g_px + 7) / 8;
    if (cap < 2) cap = 2;
    return cap + 1;
}

static uint32_t g_cps[SCAN_CP_MAX];
static int      g_cpCount  = 0;
static int      g_gbkPath  = 0;   /* 1 = GBK conversion, 0 = direct Unicode */
static int      g_unmapped = 0;   /* GBK slots without a codepoint          */
static int      g_pngFails = 0;
static int      g_boldMode = 0;   /* 1 = judging the bold pipeline        */
/* Which sub-rule produced the last 'E': 'L' = literal band-group rule,
   'D' = dashed fade-out auxiliary rule (read by main right after a
   scanJudgeBitmap call that returned 'E' for the dual-count report). */
static int      g_eSource = 0;

/* Provider mode ([collapsed 2026-10-07]): the AMBIENT/XFO1/FT2/BOTH
   four-state machine is gone with the XFO1 implementation -- FT is the
   only outline provider, no env pin and no child orchestration needed.
   [Removed] g_provider / SCAN_PROVIDER_* / scanRunProviderChildren /
   scanReportProviderDiff / scanProviderTag. */
static char      g_family[SCAN_PATH_MAX]; /* --family=/SCAN_FONT_FAMILY    */
static int       g_maxCps   = SCAN_CP_MAX; /* --max=N scan-set cap         */

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
                            int* minX, int* minY, int* maxX, int* maxY,
                            int* inkAreaOut)
{
    int luma[SCAN_CANVAS_MAX][SCAN_CANVAS_MAX];
    int rowInk[SCAN_CANVAS_MAX];
    int rowInk100[SCAN_CANVAS_MAX];
    int canvas = scanCanvas();
    int x, y, dark100, dark200, bands, lox, loy, hix, hiy, bboxW, bboxH;
    int areaSum;
    char letter = 0;

    dark100 = 0;
    dark200 = 0;
    areaSum = 0;
    bands = 0;
    lox = canvas; loy = canvas;
    hix = -1;     hiy = -1;
    for (y = 0; y < canvas; ++y)
    {
        rowInk[y] = 0;
        rowInk100[y] = 0;
        for (x = 0; x < canvas; ++x)
        {
            int l = scanLuma(XImage_pixel(img, x, y));
            luma[y][x] = l;
            areaSum += 255 - l;
            if (l < SCAN_LUMA_DARK)
            {
                ++rowInk[y];
                ++dark200;
                if (x < lox) lox = x;
                if (x > hix) hix = x;
                if (y < loy) loy = y;
                if (y > hiy) hiy = y;
            }
            if (l < SCAN_LUMA_INK)
            {
                ++rowInk100[y];
                ++dark100;
            }
        }
    }
    *ink100 = dark100;
    *ink200 = dark200;
    /* Equivalent solid pixels: coverage area survives grid-fit's edge
       concentration (a 1.08px stem straddling two columns renders 2 dark
       pixels; snapped to 1 solid column it renders 1 - pixel counts halve
       while area drops only the true ~7% width rounding), so criterion D
       gates ink loss on AREA, not pixel counts. */
    *inkAreaOut = (areaSum + 127) / 255;
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
        int badThick = scanBandBadThick();
        for (y = 0; y < canvas; ++y)
        {
            bool rowIdentical;
            if (SCAN_BAND_ROW_DEN * rowInk[y] <
                SCAN_BAND_ROW_NUM * bboxW)
            {
                run = 0; /* a non-band row breaks the run */
                continue;
            }
            rowIdentical = (y + 1 < canvas);
            for (x = 0; rowIdentical && x < canvas; ++x)
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
        if (bands >= badThick) letter = 'C';
    }

    /* Criterion E: horizontal-band fade-out.  Same band rows as C (>= 0.7
       * bboxW pixels luma < 200); consecutive band rows fold into a band
       group, and the group must contain at least one row whose solid-ink
       (luma < 100) pixel count reaches 0.5 * bboxW.  A group where EVERY
       row is mid-gray means the whole horizontal stroke lost its grid-fit
       snap and stayed at ~50% coverage -- visually a dashed/faded stroke
       between the black verticals even though the stroke "is there" (the
       16px chu U+7840 / kan U+63A7 class: A/B/C/D all pass it because the
       band count never changes).  A snapped stroke renders at least one
       solid black row (dark100 = full width), so legal grid-fit output
       passes.  Narrow glyphs (< SCAN_BAND_MIN_W) cannot distinguish bands
       from bars, and bold mode is exempt exactly like C. */
    if (letter == 0 && !g_boldMode && bboxW >= SCAN_BAND_MIN_W)
    {
        int inGroup = 0;
        int groupDark = 0;
        for (y = 0; y < canvas; ++y)
        {
            bool isBand = (SCAN_BAND_ROW_DEN * rowInk[y] >=
                           SCAN_BAND_ROW_NUM * bboxW);
            if (!isBand)
            {
                if (inGroup && !groupDark)
                {
                    letter = 'E';
                    g_eSource = 'L'; /* literal band-group rule */
                    break;
                }
                inGroup = 0;
                groupDark = 0;
                continue;
            }
            if (!inGroup)
            {
                inGroup = 1;
                groupDark = 0;
            }
            if (SCAN_BAND_DARK_DEN * rowInk100[y] >=
                SCAN_BAND_DARK_NUM * bboxW)
                groupDark = 1;
        }
        if (letter == 0 && inGroup && !groupDark)
        {
            letter = 'E';
            g_eSource = 'L';
        }
    }

    /* Criterion E (auxiliary): dashed fade-out.  A row where TWO OR MORE
       gray runs (100 <= luma < 200) are each flanked on BOTH sides by
       solid columns (luma < 100) is the "stroke vanish between anchors"
       signature: gray dashes alternating with black stems/ends inside one
       stroke line (chu U+7840 inner bar `#++#++#`, kan U+63A7 long bar
       `######++++++++#` + the `#+#+#` stem rows).  Calibration constraint
       (2026-10-05): the must-report set (chu/kan/tu) and the must-NOT
       report set (yi/er/san/zi/kou/'A') are NOT separable by the literal
       band-group rule alone -- chu's faded fringe rows are pixel-pattern
       identical to zi/kou's legal inner-bar fringes (uniform gray between
       solid walls, one flanked run per row) -- so the auxiliary rule counts
       flanked gray RUNS per row and fires only at >= 2, which the uniform
       fringe rows (exactly one run) never reach.  Measured 16px: jian
       U+4EF6 / shi U+89C6 carry no bitmap-domain signature at all (every
       band group already holds a solid row, <= 1 flanked run per row) and
       are deliberately NOT forced -- they stay covered by the post-fix
       page visual recheck, not by this deterministic gate. */
    if (letter == 0 && !g_boldMode && bboxW >= SCAN_BAND_MIN_W)
    {
        for (y = loy; y <= hiy && letter == 0; ++y)
        {
            int runs = 0;
            int x = lox + 1;
            while (x < hix)
            {
                if (luma[y][x] >= SCAN_LUMA_INK &&
                    luma[y][x] < SCAN_LUMA_DARK)
                {
                    int s = x;
                    while (x <= hix &&
                           luma[y][x] >= SCAN_LUMA_INK &&
                           luma[y][x] < SCAN_LUMA_DARK)
                        ++x;
                    /* run [s..x-1]; interior only when both flanks are
                       solid ink columns inside the bbox */
                    if (s - 1 >= lox && x <= hix &&
                        luma[y][s - 1] < SCAN_LUMA_INK &&
                        luma[y][x] < SCAN_LUMA_INK)
                    {
                        ++runs;
                        if (runs >= 2)
                        {
                            letter = 'E';
                            g_eSource = 'D'; /* dashed auxiliary rule */
                            break;
                        }
                    }
                }
                else
                    ++x;
            }
        }
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
    int canvas = scanCanvas();
    int bw = maxX - minX + 1;
    int bands = 0;
    int run = 0;
    bool prevIdentical = false;
    int prevRow[SCAN_CANVAS_MAX];
    if (bw < SCAN_BAND_MIN_W) return 0;
    for (y = 0; y < canvas; ++y)
    {
        int cnt = 0;
        bool isBand;
        bool identical = true;
        for (x = 0; x < canvas; ++x)
        {
            int l = scanLuma(XImage_pixel(img, x, y));
            if (l < SCAN_LUMA_DARK) ++cnt;
            if (y > 0 && l != prevRow[x]) identical = false;
        }
        for (x = 0; x < canvas; ++x)
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
 *          (providers fail directly for codepoints absent from the face
 *          cmap).  Used to skip GBK slots whose system
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
    if (!XPainter_drawText(painter, scanOriginX(), scanBaseY(), utf8,
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
            int ink100 = 0, ink200 = 0, bands = 0, inkArea = 0;
            int mnx = 0, mny = 0, mxx = 0, mxy = 0;
            if (n <= 0) continue;
            u8[n] = '\0';
            if (!scanRender(painter, img, font, (const char*)u8)) continue;
            (void)scanJudgeBitmap(img, cp, &ink100, &ink200, &bands,
                                  &mnx, &mny, &mxx, &mxy, &inkArea);
            {
                char path[SCAN_PATH_MAX];
                XImage crop;
                int x0, y0, x1, y1, w, h, x, y;
                int canvas = scanCanvas();
                if (mxx < mnx || mxy < mny)
                {
                    x0 = 0; y0 = 0;
                    x1 = canvas - 1; y1 = canvas - 1;
                }
                else
                {
                    x0 = (mnx - 2 < 0) ? 0 : mnx - 2;
                    y0 = (mny - 2 < 0) ? 0 : mny - 2;
                    x1 = (mxx + 2 > canvas - 1) ? canvas - 1 : mxx + 2;
                    y1 = (mxy + 2 > canvas - 1) ? canvas - 1 : mxy + 2;
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
                        "font-scan: dump U+%04X ink100=%d ink200=%d area=%d bands=%d "
                        "bandRows=%d -> %s\n", (unsigned)cp, ink100, ink200, inkArea,
                        bands, scanCountBands(img, mnx, mxx), path);
                /* Per-row dark counts inside the ink bbox, printed as
                   "y:dark200/dark100": dark200 shows WHERE band rows went
                   when grid-fit changes the glyph (merged-into-slab vs
                   vanished-stroke read directly), and the dark100 split is
                   the criterion-E evidence -- a band row whose dark100
                   stays near zero while dark200 is full width is a faded
                   half-coverage stroke.  Per-column counts pinpoint a
                   collapsed vertical stroke (its column dark count drops
                   to ~0). */
                {
                    int ry, rx, darkCnt, darkCnt100;
                    fprintf(stderr, "font-scan: rows y:dark200/dark100");
                    for (ry = mny; ry <= mxy; ++ry)
                    {
                        darkCnt = 0;
                        darkCnt100 = 0;
                        for (rx = mnx; rx <= mxx; ++rx)
                        {
                            int l = scanLuma(XImage_pixel(img, rx, ry));
                            if (l < SCAN_LUMA_DARK) ++darkCnt;
                            if (l < SCAN_LUMA_INK) ++darkCnt100;
                        }
                        fprintf(stderr, " %d:%d/%d", ry, darkCnt, darkCnt100);
                    }
                    fprintf(stderr, "\n");
                    fprintf(stderr, "font-scan: cols x:dark");
                    for (rx = mnx; rx <= mxx; ++rx)
                    {
                        darkCnt = 0;
                        for (ry = mny; ry <= mxy; ++ry)
                            if (scanLuma(XImage_pixel(img, rx, ry)) <
                                SCAN_LUMA_DARK)
                                ++darkCnt;
                        fprintf(stderr, " %d:%d", rx, darkCnt);
                    }
                    fprintf(stderr, "\n");
                }
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
    int canvas = scanCanvas();
    bool ok;

    if (maxX < minX || maxY < minY)
    {
        x0 = 0; y0 = 0;
        x1 = canvas - 1; y1 = canvas - 1;
    }
    else
    {
        x0 = (minX - 2 < 0) ? 0 : minX - 2;
        y0 = (minY - 2 < 0) ? 0 : minY - 2;
        x1 = (maxX + 2 > canvas - 1) ? canvas - 1 : maxX + 2;
        y1 = (maxY + 2 > canvas - 1) ? canvas - 1 : maxY + 2;
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
static void scanHistEmit(const char* line)
{
    fprintf(stderr, "%s\n", line);
}

static int scanCriteriaSelfTest(void)
{
    XImage img;
    int failures = 0;
    int ink100, ink200, bands, inkArea, mnx, mny, mxx, mxy;
    int cap = scanBandBadThick();

    /* Blank canvas -> A (no pixel below luma 100). */
    XImage_init_ex(&img, scanCanvas(), scanCanvas(), XImageFormat_ARGB32);
    XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
    if (scanJudgeBitmap(&img, 0x4E00u, &ink100, &ink200, &bands,
                        &mnx, &mny, &mxx, &mxy, &inkArea) != 'A')
        ++failures;

    /* 2x1 dot -> B (area 2 -> floor threshold 3, ink 2 < 3); the 3x3 dot
       (ink 9 >= 3) and the 4x1 hyphen (ink 4 >= 3) must pass. */
    XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
    {
        XRect dot = { 10, 10, 2, 1 };
        XImage_fillRect(&img, &dot, 0xFF000000u);
    }
    if (scanJudgeBitmap(&img, 0x4E00u, &ink100, &ink200, &bands,
                        &mnx, &mny, &mxx, &mxy, &inkArea) != 'B')
        ++failures;
    {
        XRect hyphen = { 10, 10, 4, 1 };
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        XImage_fillRect(&img, &hyphen, 0xFF000000u);
        if (scanJudgeBitmap(&img, 0x002Du, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy, &inkArea) != 0)
            ++failures;
    }

    /* cap+1 identical band rows -> C (thicker than any legal single
       grid-fit stroke at this em; 3 rows at the 16px default). */
    {
        XRect bar = { 4, 0, 16, 1 };
        int row;
        bar.width = g_px;
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        for (row = 0; row <= cap; ++row)
        {
            bar.y = 8 + row;
            XImage_fillRect(&img, &bar, 0xFF000000u);
        }
        if (scanJudgeBitmap(&img, 0x4E00u, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy, &inkArea) != 'C')
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
                            &mnx, &mny, &mxx, &mxy, &inkArea) != 0)
            ++failures;
    }
    /* Vertical bar (1px column, 20 rows tall) -> pass (never a band row). */
    {
        XRect column = { 12, 2, 1, 20 };
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        XImage_fillRect(&img, &column, 0xFF000000u);
        if (scanJudgeBitmap(&img, 0x4E28u, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy, &inkArea) != 0)
            ++failures;
    }
    /* Criterion E: a wide mid-gray band (luma 145 = the half-coverage
       read) crossed by one black stem is a band group with NO row at
       0.5 * bboxW solid pixels -> E (the 16px chu U+7840 class that
       A/B/C/D all pass).  The same geometry with the band solid black
       has its full-width dark row -> pass. */
    {
        XRect band = { 4, 12, 25, 2 };
        XRect stem = { 12, 4, 2, 24 };
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        XImage_fillRect(&img, &band, 0xFF919191u);
        XImage_fillRect(&img, &stem, 0xFF000000u);
        if (scanJudgeBitmap(&img, 0x7840u, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy, &inkArea) != 'E')
            ++failures;
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        XImage_fillRect(&img, &band, 0xFF000000u);
        XImage_fillRect(&img, &stem, 0xFF000000u);
        if (scanJudgeBitmap(&img, 0x7840u, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy, &inkArea) != 0)
            ++failures;
    }
    /* Criterion E dashed auxiliary: `#+#+#` (two solid-flanked gray runs
       in the same row, the chu/kan vanish signature) -> E, while one
       flanked run over a solid row (`#+++#` above `#####`, the legal
       zi/kou inner-bar fringe) -> pass.  Only 2 rows so criterion C's
       identical-run fold stays below its threshold. */
    {
        XRect col = { 10, 12, 1, 1 };
        int pass = 0;
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        for (pass = 0; pass < 5; ++pass)
        {
            col.x = 10 + pass;
            XImage_fillRect(&img, &col,
                            (pass % 2 == 0) ? 0xFF000000u : 0xFF919191u);
            col.y = 13;
            XImage_fillRect(&img, &col, 0xFF000000u);
            col.y = 12;
        }
        if (scanJudgeBitmap(&img, 0x7840u, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy, &inkArea) != 'E')
            ++failures;
        XImage_fillRect(&img, NULL, 0xFFFFFFFFu);
        col.y = 12;
        for (pass = 0; pass < 5; ++pass)
        {
            col.x = 10 + pass;
            XImage_fillRect(&img, &col,
                            (pass == 0 || pass == 4) ? 0xFF000000u
                                                     : 0xFF919191u);
            col.y = 13;
            XImage_fillRect(&img, &col, 0xFF000000u);
            col.y = 12;
        }
        if (scanJudgeBitmap(&img, 0x7840u, &ink100, &ink200, &bands,
                            &mnx, &mny, &mxx, &mxy, &inkArea) != 0)
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

/* ==================== Output paths ====================
   [Collapsed 2026-10-07] 原 provider 三档基础设施
   （scanProviderTag/scanCountBadList/scanReportProviderDiff/
   scanRunProviderChildren——--provider=xfo1|ft2|both 双跑+差分）随 XFO1
   实现删除：FT 为唯一轮廓 provider，输出恒用 legacy 名（无 _ft2 后缀），
   单进程单档，不再 pin env、不再 spawn 子进程。 */

/* [死码清理] scanBadListPath 已删除：全仓无调用点（见审计清单）。
 */

static void scanRefPath(char* buf, size_t cap, const char* outDir,
                        const char* tag)
{
    if (g_px == SCAN_FONT_PIXELS)
        snprintf(buf, cap, "%s/scan_ref%s.txt", outDir, tag);
    else
        snprintf(buf, cap, "%s/scan_ref%s_px%d.txt", outDir, tag, g_px);
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
    int eLitCount = 0, eDashCount = 0;
    int first20Count = 0;
    int exitCode;
    /* Criterion D (stroke-loss vs design) plumbing: "--ref" mode renders
       with XGUI_TEXT_GRIDFIT=0 (the runtime escape hatch in XPainter)
       and records each glyph's band-row count, near-black ink100 and
       coverage area (equivalent solid px) to scan_ref.txt; the default
       mode loads that table and fails any glyph whose aligned render has
       >= 2 fewer band rows than the unaligned reference AND >=
       SCAN_D_MIN_AREA_LOSS less coverage-area pixels - the signature of
       a horizontal stroke swallowed by grid-fit (yan 0x6F14, zui
       0x6700: interior bars vanished while total ink stayed above
       criterion B's floor).  The ink gate measures AREA (not dark pixel
       counts): grid-fit's legitimate crisping - edge de-smear and
       straddle concentration - halves dark pixel counts while area
       tracks the true stroke, so pixel-count gates mass-fire D. */
    int refMode = 0;
    int boldMode = 0;
    int argi;
    for (argi = 1; argi < argc; ++argi)
    {
        if (strcmp(argv[argi], "--ref") == 0) refMode = 1;
        else if (strcmp(argv[argi], "--bold") == 0) boldMode = 1;
        /* [Collapsed 2026-10-07] 原 --provider=xfo1|ft2|both 解析分支随
           XFO1 实现删除：FT 为唯一轮廓 provider，该旗标不再有意义
           （传入时被本轮循环静默忽略，与其它未知旗标同待遇）。 */
        else if (strncmp(argv[argi], "--family=", 9) == 0)
            snprintf(g_family, sizeof(g_family), "%s", argv[argi] + 9);
        else if (strncmp(argv[argi], "--max=", 6) == 0)
        {
            int n = atoi(argv[argi] + 6);
            if (n < 1) n = 1;
            if (n > SCAN_CP_MAX) n = SCAN_CP_MAX;
            g_maxCps = n;
        }
        else if (strncmp(argv[argi], "--px=", 5) == 0)
        {
            int px = atoi(argv[argi] + 5);
            if (px < 8) px = 8;
            if (px > 30) px = 30; /* canvas 60 <= SCAN_CANVAS_MAX 64 */
            g_px = px;
        }
    }
    FILE* refFile = NULL;
    static int refBands[SCAN_CP_MAX];
    static int refInk100[SCAN_CP_MAX];
    static int refArea[SCAN_CP_MAX];

    for (i = 0; i < SCAN_CP_MAX; ++i)
    {
        refBands[i] = -1;
        refInk100[i] = -1;
        refArea[i] = -1;
    }

    scanResolveOutDir(outDir, sizeof(outDir));
    if (g_px == SCAN_FONT_PIXELS)
    {
        /* legacy names: the 16px gates and existing tooling key on them
           (single provider since the XFO1 removal -- no _ft2 suffix). */
        snprintf(badDir, sizeof(badDir), "%s/scan_bad", outDir);
        snprintf(listPath, sizeof(listPath), "%s/scan_bad_1.txt", outDir);
    }
    else
    {
        snprintf(badDir, sizeof(badDir), "%s/scan_bad_px%d", outDir, g_px);
        snprintf(listPath, sizeof(listPath), "%s/scan_bad_px%d.txt",
                 outDir, g_px);
    }
    SCAN_MKDIR(outDir);
    SCAN_MKDIR(badDir);
    /* Reference mode renders the design geometry: kill grid-fit before
       the first glyph render caches XPainter's one-shot override.
       （_putenv 为 MSVC 专属，posix 走 setenv——2026-10-05 可移植修。） */
#if defined(_WIN32)
    if (refMode) _putenv("XGUI_TEXT_GRIDFIT=0");
#else
    if (refMode) setenv("XGUI_TEXT_GRIDFIT", "0", 1);
#endif
    /* [Collapsed 2026-10-07] 原 XFONT_PROVIDER pin 块随 env 分档删除：
       FT 为唯一轮廓 provider，无需 pin。 */

    scanBuildCodepoints();
    /* --max smoke cap: applied AFTER the list is built -- every downstream
       consumer (scan loop, ref-table count check, summaries) keys on
       g_cpCount, so one cap keeps them consistent.  A criterion-D ref
       table must be rebuilt at the same --max (count mismatch disarms D). */
    if (g_maxCps < g_cpCount)
    {
        fprintf(stderr, "font-scan: --max=%d caps the scan set (%d -> %d)\n",
                g_maxCps, g_cpCount, g_maxCps);
        g_cpCount = g_maxCps;
    }
    fprintf(stderr, "font-scan: codepoints=%d path=%s unmappedGbkSlots=%d\n",
            g_cpCount,
            g_gbkPath ? "GBK(XChar_fromGbkStream)" : "direct-unicode-fallback",
            g_unmapped);

    /* Family override (P2.6 §7.3 desktop leg): --family wins over the
       SCAN_FONT_FAMILY env; the fc-match-resolved system Noto path is fed
       as a direct-path family (design §7.1, file never enters the repo). */
    if (!g_family[0])
    {
        const char* envFamily = getenv("SCAN_FONT_FAMILY");
        if (envFamily && envFamily[0])
            snprintf(g_family, sizeof(g_family), "%s", envFamily);
    }
    font = XFont_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                           g_family[0] ? g_family : NULL, -1, -1, false);
    if (!font)
    {
        fprintf(stderr, "font-scan: XFont_create_ex failed\n");
        return 2;
    }
    XFont_setPixelSize(font, g_px);
    if (boldMode)
    {
        XFont_setBold(font, true);
        g_boldMode = 1;
    }
    fprintf(stderr, "font-scan: family=%s pixelSize=%d bold=%d\n",
            XFont_family(font), XFont_pixelSize(font),
            boldMode ? XFont_bold(font) : 0);

    XImage_init_ex(&img, scanCanvas(), scanCanvas(), XImageFormat_ARGB32);
    XPainter_init(&painter, NULL);

    /* Smoke probe: 'A' must produce ink, proving the outline face loaded
       (exe-dir anchored ../Library/XFont lookup) and the SW-AA path ran. */
    {
        char aUtf8[2];
        int ink100 = 0, ink200 = 0, bands = 0, inkArea = 0;
        int mnx = 0, mny = 0, mxx = 0, mxy = 0;
        aUtf8[0] = 'A';
        aUtf8[1] = '\0';
        if (!scanRender(&painter, &img, font, aUtf8))
        {
            fprintf(stderr, "font-scan: smoke render failed\n");
            return 2;
        }
        (void)scanJudgeBitmap(&img, 0x41u, &ink100, &ink200, &bands,
                              &mnx, &mny, &mxx, &mxy, &inkArea);
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
        scanRefPath(refPath, sizeof(refPath), outDir, "");
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
        scanRefPath(refPath, sizeof(refPath), outDir, "");
        refFile = fopen(refPath, "r");
        if (refFile)
        {
            int idx = 0;
            int value = 0;
            int ink = 0;
            int area = 0;
            /* Rows are "bands ink100 inkArea" triples, keyed by g_cps index;
               skipped codepoints store "-1 -1 -1". */
            while (idx < SCAN_CP_MAX &&
                   fscanf(refFile, "%d %d %d", &value, &ink, &area) == 3)
            {
                refBands[idx] = value;
                refInk100[idx] = ink;
                refArea[idx] = area;
                ++idx;
            }
            fclose(refFile);
            if (idx != g_cpCount)
            {
                int k;
                fprintf(stderr,
                        "font-scan: ref table %d entries != %d cps, "
                        "criterion D disarmed\n",
                        idx, g_cpCount);
                /* Disarm for real: the warning used to print but refBands
                   stayed armed, so a stale/misaligned table kept feeding
                   criterion D and mass-produced false positives. */
                for (k = 0; k < SCAN_CP_MAX; ++k) refBands[k] = -1;
            }
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
        int ink100 = 0, ink200 = 0, bands = 0, inkArea = 0;
        int mnx = 0, mny = 0, mxx = 0, mxy = 0;

        /* Space is structurally blank in any font: render-count it, but do
           not judge it (it would flag criterion A on every run). */
        if (cp == 0x20u)
        {
            ++blankSkipped;
            /* Ref table rows are keyed by g_cps index: skipped codepoints
               must still emit a placeholder row, or every entry after the
               skip shifts by one and criterion D ends up comparing each
               glyph against its neighbor's band count (one pass produced
               1432 false D flags this way - every flagged ASCII was an
               adjacent pair whose successor has >= 2 more band rows). */
            if (refMode) fprintf(refFile, "-1 -1 -1\n");
            continue;
        }
        /* Codepoints the face does not cover: the GBK codepage maps some
           slots (E810-E814 PUA etc.) outside the face cmap.  Skipping them
           keeps criterion A about real blank regressions of glyphs the
           font actually has.  Same placeholder rule as the space skip. */
        if (!scanFaceHasGlyph(font, cp))
        {
            ++noGlyphSkipped;
            if (refMode) fprintf(refFile, "-1 -1 -1\n");
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
                                 &mnx, &mny, &mxx, &mxy, &inkArea);
        if (refMode)
        {
            fprintf(refFile, "%d %d %d\n",
                    scanCountBands(&img, mnx, mxx), ink100,
                    inkArea);
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
               acceptance on the UI pages.
               Two more gates keep the verdict on the charter (a stroke
               vanishing) instead of grid-fit's inherent width-rounding
               lottery (each edge rounds independently by up to ~0.4px,
               so a whole glyph's strokes shift weight a few percent in
               either direction - measured fu U+670D: band rows 4->2 with
               bars still present at 0.64*bw dark vs the 0.7 threshold,
               area 104->96):
               1. band ROWS are threshold-cliffed at 0.7*bw - a bar
                  thinning 0.74->0.64 flips a row off with no stroke
                  gone, so the ink gate must not trust the row count
                  alone;
               2. ink loss is measured in coverage AREA and must reach
                  TWO full band rows (2 * 0.7 * bboxW) - the ink of the
                  strokes that supposedly vanished.  Concentration
                  (straddling stem -> one solid column) conserves area;
                  thinning below threshold loses a few percent; a real
                  swallow loses the full rows. */
            int areaGate = (14 * (mxx - mnx + 1)) / 10;
            if (refBands[i] - loBands >= 2 &&
                refArea[i] - inkArea >= areaGate)
                letter = 'D';
        }
        if (letter == 0) continue;

        ++badCount;
        if (letter == 'E')
        {
            /* Dual-count disclosure (2026-10-05 gate ruling): the literal
               band-group rule is the transparency metric, the literal +
               dashed combination is the gate verdict written to the list. */
            if (g_eSource == 'L') ++eLitCount;
            else ++eDashCount;
        }
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
        printf("font-scan: reference pass done, %d rows written "
               "(%d judged, %d placeholder)\n",
               g_cpCount, scanned, g_cpCount - scanned);
        return 0;
    }
    printf("font-scan: codepoint path=%s unmappedGbkSlots=%d blankSkipped=%d "
           "noGlyphSkipped=%d\n",
           g_gbkPath ? "GBK" : "direct-unicode", g_unmapped, blankSkipped,
           noGlyphSkipped);
    printf("font-scan: scanned=%d bad=%d (E-literal=%d E-dashed=%d) "
           "pngFails=%d\n",
           scanned, badCount, eLitCount, eDashCount, g_pngFails);
    printf("font-scan: bad list=%s\n", listPath);
    printf("font-scan: bad samples=%s\n", badDir);
    printf("font-scan: first %d bad:", first20Count);
    for (i = 0; i < first20Count; ++i)
        printf(" %s", first20[i]);
    printf("\n");

    exitCode = (badCount < 250) ? badCount : 250;
    if (scanned == 0)
    {
        /* Vacuous-pass guard (2026-10-07): a run that judged nothing
           (e.g. a .ttf path family whose file is missing falls through
           to the bitmap fallback face, whose outline cmap probe answers
           false for every codepoint) must not report a green gate. */
        fprintf(stderr, "font-scan: no glyph judged (scanned=0) - "
                        "vacuous pass rejected\n");
        exitCode = 3;
    }
    {
        int selfTest = scanCriteriaSelfTest();
        printf("font-scan: criteria self-test failures=%d\n", selfTest);
        if (selfTest > 0 && exitCode == 0) exitCode = 250;
    }
    {
        long c = 0, b = 0, p = 0;
        XFontFt_memStat(&c, &b, &p);
        fprintf(stderr, "FT-mem: allocCount=%ld allocBytes=%ld peakLive~=%ld\n",
                c, b, p);
        {
            long tc = 0, tb = 0, ts = 0, tl = 0;
            XFontFt_memStatSplit(&tc, &tb, &ts, &tl);
            fprintf(stderr, "FT-mem: temp(FT_Load_Glyph scratch) count=%ld "
                            "bytes=%ld small<=256B=%ld largeBytes=%ld\n",
                    tc, tb, ts, tl);
        }
        XFontFt_histReport(&scanHistEmit);
    }
    return exitCode;
}
