/******************************************************************************
 * @file       XFontOutline_Xfo.c
 * @brief      XFO1 轮廓数据的内存解析 helper 实现。
 ******************************************************************************/
#include "XFontOutline_Xfo.h"
#include <limits.h>
#include <string.h>

#if XFONT_OUTLINE_ON

enum { XFO_HEADER = 36, XFO_CMAP = 8, XFO_GLYPH = 20 };

static uint16_t xfo16(const unsigned char* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
static int16_t xfo16s(const unsigned char* p) { return (int16_t)xfo16(p); }
static uint32_t xfo32(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool xfoHeader(const unsigned char* d, size_t n,
                      XFontOutlineInfo* info, uint16_t* cmapCount,
                      uint16_t* glyphCount, size_t* cmapOffset,
                      size_t* glyphOffset, size_t* commandOffset,
                      size_t* commandSize)
{
    uint32_t cmap, glyph, commands, length;
    uint16_t cc, gc;
    if (!d || n < XFO_HEADER || memcmp(d, "XFO1", 4) != 0 ||
        xfo16(d + 4) != 1u)
        return false;
    cc = xfo16(d + 16); gc = xfo16(d + 18);
    cmap = xfo32(d + 20); glyph = xfo32(d + 24);
    commands = xfo32(d + 28); length = xfo32(d + 32);
    if (!cc || !gc || cmap < XFO_HEADER || glyph < cmap || commands < glyph ||
        cmap > n || glyph > n || commands > n || length > n - commands ||
        (size_t)cc > (n - cmap) / XFO_CMAP ||
        (size_t)gc > (n - glyph) / XFO_GLYPH ||
        cmap + (size_t)cc * XFO_CMAP > glyph ||
        glyph + (size_t)gc * XFO_GLYPH > commands ||
        xfo16(d + 8) == 0u || xfo16s(d + 10) < 0 ||
        xfo16s(d + 12) < 0 || xfo16s(d + 14) < 0)
        return false;
    if (info)
    {
        info->unitsPerEm = (int)xfo16(d + 8);
        info->ascent = (int)xfo16s(d + 10);
        info->descent = (int)xfo16s(d + 12);
        info->lineGap = (int)xfo16s(d + 14);
    }
    if (cmapCount) *cmapCount = cc;
    if (glyphCount) *glyphCount = gc;
    if (cmapOffset) *cmapOffset = (size_t)cmap;
    if (glyphOffset) *glyphOffset = (size_t)glyph;
    if (commandOffset) *commandOffset = (size_t)commands;
    if (commandSize) *commandSize = (size_t)length;
    return true;
}

static bool xfoFind(const unsigned char* d, size_t offset, uint16_t count,
                    uint16_t glyphCount, uint32_t cp, uint16_t* id)
{
    int lo;
    int hi;
    if (!d || !id) return false;
    /* Built-in XFO1 maps are sorted by codepoint, so use logarithmic lookup
       for the common path.  Keep a linear fallback for externally supplied
       files whose cmap does not follow that ordering. */
    lo = 0;
    hi = (int)count - 1;
    while (lo <= hi)
    {
        int mid = lo + (hi - lo) / 2;
        const unsigned char* e = d + offset + (size_t)mid * XFO_CMAP;
        uint32_t value = xfo32(e);
        if (value == cp)
        {
            *id = xfo16(e + 4);
            return *id < glyphCount;
        }
        if (value < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    {
        uint16_t i;
        for (i = 0; i < count; ++i)
        {
            const unsigned char* e = d + offset + (size_t)i * XFO_CMAP;
            if (xfo32(e) == cp)
            {
                *id = xfo16(e + 4);
                return *id < glyphCount;
            }
        }
    }
    return false;
}

static int16_t xfoDelta(const unsigned char* d, size_t* pos, size_t end,
                        bool* ok)
{
    int16_t value;
    if (!d || !pos || !ok || *pos > end || end - *pos < 2u)
    {
        if (ok) *ok = false;
        return 0;
    }
    value = xfo16s(d + *pos); *pos += 2u; return value;
}

static bool xfoEmit(const unsigned char* d, size_t start, size_t end,
                    uint16_t count, const XFontOutlineSink* sink)
{
    size_t pos = start;
    int32_t x = 0, y = 0;
    uint16_t i;
    bool ok = true;
    if (!d || start > end || count > XFONT_OUTLINE_MAX_COMMANDS) return false;
    /* A NULL sink is the metrics-only query used by text layout.  The record
       bounds and command count have already been validated by the caller;
       decoding every delta here would only repeat work before the draw pass. */
    if (!sink) return true;
    for (i = 0; i < count; ++i)
    {
        unsigned op;
        if (pos >= end) return false;
        op = d[pos++];
        if (op == XFontOutline_MoveTo || op == XFontOutline_LineTo)
        {
            int16_t dx = xfoDelta(d, &pos, end, &ok);
            int16_t dy = xfoDelta(d, &pos, end, &ok);
            if (!ok) return false;
            x += dx; y += dy;
            if (sink)
            {
                if (op == XFontOutline_MoveTo)
                {
                    if (sink->moveTo && !sink->moveTo(sink->userData,
                                                       (float)x, (float)y))
                        return false;
                }
                else if (sink->lineTo && !sink->lineTo(sink->userData,
                                                       (float)x, (float)y))
                    return false;
            }
        }
        else if (op == XFontOutline_QuadTo)
        {
#if XFONT_OUTLINE_QUADRATIC_ON
            int16_t a = xfoDelta(d, &pos, end, &ok);
            int16_t b = xfoDelta(d, &pos, end, &ok);
            int16_t c = xfoDelta(d, &pos, end, &ok);
            int16_t e = xfoDelta(d, &pos, end, &ok);
            int32_t cx, cy;
            if (!ok) return false;
            cx = x + a; cy = y + b; x = cx + c; y = cy + e;
            if (sink && sink->quadTo && !sink->quadTo(sink->userData,
                    (float)cx, (float)cy, (float)x, (float)y)) return false;
#else
            return false;
#endif
        }
        else if (op == XFontOutline_CubicTo)
        {
#if XFONT_OUTLINE_CUBIC_ON
            int16_t a = xfoDelta(d, &pos, end, &ok);
            int16_t b = xfoDelta(d, &pos, end, &ok);
            int16_t c = xfoDelta(d, &pos, end, &ok);
            int16_t e = xfoDelta(d, &pos, end, &ok);
            int16_t f = xfoDelta(d, &pos, end, &ok);
            int16_t g = xfoDelta(d, &pos, end, &ok);
            int32_t c1x, c1y, c2x, c2y;
            if (!ok) return false;
            c1x = x + a; c1y = y + b; c2x = c1x + c; c2y = c1y + e;
            x = c2x + f; y = c2y + g;
            if (sink && sink->cubicTo && !sink->cubicTo(sink->userData,
                    (float)c1x, (float)c1y, (float)c2x, (float)c2y,
                    (float)x, (float)y)) return false;
#else
            return false;
#endif
        }
        else if (op == XFontOutline_Close)
        {
            if (sink && sink->close && !sink->close(sink->userData))
                return false;
        }
        else return false;
    }
    return pos <= end;
}

bool XFontOutline_Xfo_info(const unsigned char* data, size_t size,
                           XFontOutlineInfo* info)
{
    return info && xfoHeader(data, size, info, NULL, NULL, NULL, NULL, NULL,
                             NULL);
}

bool XFontOutline_Xfo_loadGlyph(const unsigned char* data, size_t size,
                                uint32_t codepoint,
                                XFontOutlineGlyphMetrics* metrics,
                                const XFontOutlineSink* sink)
{
    size_t cmapOffset, glyphOffset, commandOffset, commandSize;
    uint16_t cmapCount, glyphCount, glyphId, commandCount;
    const unsigned char* e;
    uint32_t relative;
    if (!metrics || !xfoHeader(data, size, NULL, &cmapCount, &glyphCount,
                               &cmapOffset, &glyphOffset, &commandOffset,
                               &commandSize) ||
        !xfoFind(data, cmapOffset, cmapCount, glyphCount, codepoint, &glyphId))
        return false;
    e = data + glyphOffset + (size_t)glyphId * XFO_GLYPH;
    if (xfo32(e) > (uint32_t)INT_MAX) return false;
    metrics->advance = (int)xfo32(e);
    metrics->xMin = xfo16s(e + 4); metrics->yMin = xfo16s(e + 6);
    metrics->xMax = xfo16s(e + 8); metrics->yMax = xfo16s(e + 10);
    relative = xfo32(e + 12); commandCount = xfo16(e + 16);
    if (relative > commandSize || commandCount > XFONT_OUTLINE_MAX_COMMANDS)
        return false;
    return xfoEmit(data, commandOffset + relative,
                   commandOffset + commandSize, commandCount, sink);
}

#else
bool XFontOutline_Xfo_info(const unsigned char* data, size_t size,
                           XFontOutlineInfo* info)
{ (void)data; (void)size; (void)info; return false; }
bool XFontOutline_Xfo_loadGlyph(const unsigned char* data, size_t size,
                                uint32_t codepoint,
                                XFontOutlineGlyphMetrics* metrics,
                                const XFontOutlineSink* sink)
{ (void)data; (void)size; (void)codepoint; (void)metrics; (void)sink; return false; }
#endif
