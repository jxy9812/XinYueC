/*****************************************************************************/
/**
 * @file       XImageCodecSvg.c
 * @brief      XImageCodec SVG 格式独立实现。
 * @note       受 XIMAGECODEC_SVG_ON 开关控制，可通过 XImageCodec_config.h 单独裁剪。
 * @note       解码支持三种形态：
 *             1. data:image/png;base64 内嵌位图（编码路径的产物，逐像素还原）；
 *             2. 矢量渲染（受 XIMAGECODEC_SVG_VECTOR_ON 开关控制）：自包含的
 *                轻量 SVG 渲染器，支持 svg/g/defs/rect/circle/ellipse/line/
 *                polyline/polygon/path/text、linearGradient/radialGradient、
 *                viewBox、transforms、填充/描边/透明度，零第三方依赖；
 *             3. 纯色矩形栅格化（历史兼容路径，矢量渲染失败或关闭时使用）。
 *             编码输出内嵌 PNG 的 SVG。Base64 编解码复用库内 XBase64 模块。
 *              只通过 XImageCodecInternal_decodeSvg/encodeSvg 对外暴露，
 *              统一由 XImageCodec.c 的 XImageCodec_decode/encode 分发。
 * @author     XinYueC 团队
 */
#include "XImageCodec_config.h"
#include "XImageCodecInternal.h"
#include "XMemory.h"
#include "XBase64.h"
#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if XIMAGECODEC_ON
#if XIMAGECODEC_SVG_ON

/* SVG 尺寸属性取值（十进制非负整数）。 */
static int svgNumber(const uint8_t* data, size_t size,
                     const char* key, int fallback)
{
    const uint8_t* p = (const uint8_t*)strstr((const char*)data, key);
    int value = 0;
    bool found = false;
    if (!p || (size_t)(p - data) >= size) return fallback;
    p += strlen(key);
    while ((size_t)(p - data) < size &&
           (*p == '"' || *p == '\'' || *p == '=' || *p == ' '))
        ++p;
    while ((size_t)(p - data) < size && *p >= '0' && *p <= '9') {
        found = true;
        value = value * 10 + (*p - '0');
        ++p;
    }
    return found ? value : fallback;
}

/* SVG fill 属性取值（#RRGGBB 或 #AARRGGBB）。 */
static uint32_t svgColor(const uint8_t* data, size_t size)
{
    const uint8_t* p = (const uint8_t*)strstr((const char*)data, "fill");
    unsigned value = 0;
    int digits = 0;
    if (!p || (size_t)(p - data) >= size) return 0xff000000u;
    p = (const uint8_t*)strchr((const char*)p, '#');
    if (!p || (size_t)(p - data) >= size) return 0xff000000u;
    ++p;
    while ((size_t)(p - data) < size && digits < 8) {
        int v = p[0] >= '0' && p[0] <= '9' ? p[0] - '0'
              : p[0] >= 'a' && p[0] <= 'f' ? p[0] - 'a' + 10
              : p[0] >= 'A' && p[0] <= 'F' ? p[0] - 'A' + 10 : -1;
        if (v < 0) break;
        value = (value << 4) | (unsigned)v;
        ++digits;
        ++p;
    }
    if (digits == 6) return 0xff000000u | value;
    if (digits == 8) return value;
    return 0xff000000u;
}

/* Base64 解码为新的 XByteArray；剔除空白后交由 XBase64 处理。 */
static XByteArray* svgBase64Decode(const uint8_t* data, size_t size)
{
    uint8_t* clean;
    size_t cleanSize = 0;
    size_t capacity, actual;
    XByteArray* out;
    int result;
    if (!data || !size) return NULL;
    clean = (uint8_t*)XMalloc_System(size);
    if (!clean) return NULL;
    for (size_t i = 0; i < size; ++i) {
        if (!isspace(data[i])) clean[cleanSize++] = data[i];
    }
    if (!cleanSize) {
        XFree_System(clean);
        return NULL;
    }
    capacity = XBase64_decoded_size((const char*)clean, cleanSize);
    out = XByteArray_create();
    if (!out) {
        XFree_System(clean);
        return NULL;
    }
    if (capacity && !XByteArray_resize_base((XVector*)out, capacity)) {
        XByteArray_delete_base((XClass*)out);
        XFree_System(clean);
        return NULL;
    }
    actual = capacity;
    result = XBase64_decode((const char*)clean, cleanSize,
                            XByteArray_data(out), &actual);
    if (result != 0 ||
        (actual != capacity &&
         !XByteArray_resize_base((XVector*)out, actual))) {
        XByteArray_delete_base((XClass*)out);
        XFree_System(clean);
        return NULL;
    }
    XFree_System(clean);
    return out;
}

/* 将二进制数据 Base64 编码后追加到字节数组（不写结尾 NUL）。 */
static bool svgBase64Append(XByteArray* out, const uint8_t* data, size_t size)
{
    size_t capacity, written;
    char* encoded;
    bool ok = false;
    if (!out || (!data && size)) return false;
    capacity = XBase64_encoded_size(size);
    encoded = (char*)XMalloc_System(capacity);
    if (!encoded) return false;
    written = capacity;
    if (XBase64_encode(data, size, encoded, &written) != 0) goto done;
    ok = XImageCodecInternal_appendBytes(out, encoded,
                                         written > 0 ? written - 1 : 0);
done:
    XFree_System(encoded);
    return ok;
}

#if XIMAGECODEC_SVG_VECTOR_ON
/* ===================================================================== */
/* SVG 矢量渲染器（自包含、零第三方依赖）                                  */
/* ===================================================================== */
/* 支持的元素：svg/g/defs/rect/circle/ellipse/line/polyline/polygon/path/
 * text/linearGradient/radialGradient/stop。
 * 支持的属性：fill、fill-opacity、stroke、stroke-width、stroke-opacity、
 * opacity、points、d、transform（translate/scale/rotate/skewX/skewY/matrix）、
 * gradientUnits、gradientTransform、spreadMethod、viewBox、preserveAspectRatio、
 * font-size、text-anchor。
 * 光栅化：偶奇规则逐扫描线多边形填充、Bresenham 逐线段加宽描边、
 * 圆/椭圆多边形化、贝塞尔递归细分、SVG 椭圆弧（端点转中心参数）、
 * 逐像素线性/径向渐变（source-over 合成）、viewBox(xMidYMid meet/none 等)、
 * ASCII 文字（内置 5x7 位图字体）。
 */

#ifndef M_PI
#define M_PI 3.14159265358979323846264338327950288
#endif

#define SVG_DEPTH_MAX      128
#define SVG_GRADIENT_MAX   256
#define SVG_STOPS_MAX      64
#define SVG_HREF_DEPTH     8
#define SVG_FLAT_TOLERANCE 0.25
#define SVG_ARENA_BLOCK    (8u * 1024u)
#define SVG_POINT_CHUNK    64

/* ===================================================================== */
/* 3x2 仿射矩阵（用户坐标 -> 设备坐标，列向量约定 p' = M * p）               */
/* ===================================================================== */
typedef struct SvgMatrix
{
    double m_a; /**< a 系数（x 放大/旋转）。 */
    double m_b; /**< b 系数。 */
    double m_c; /**< c 系数。 */
    double m_d; /**< d 系数（y 放大/旋转）。 */
    double m_e; /**< e 平移 x。 */
    double m_f; /**< f 平移 y。 */
} SvgMatrix;

static void svgMatrixIdentity(SvgMatrix* m)
{
    m->m_a = 1.0; m->m_b = 0.0; m->m_c = 0.0;
    m->m_d = 1.0; m->m_e = 0.0; m->m_f = 0.0;
}

/* out = a * b（先应用 b，再应用 a）。 */
static void svgMatrixMul(SvgMatrix* out, const SvgMatrix* a, const SvgMatrix* b)
{
    SvgMatrix r;
    r.m_a = a->m_a * b->m_a + a->m_c * b->m_b;
    r.m_b = a->m_b * b->m_a + a->m_d * b->m_b;
    r.m_c = a->m_a * b->m_c + a->m_c * b->m_d;
    r.m_d = a->m_b * b->m_c + a->m_d * b->m_d;
    r.m_e = a->m_a * b->m_e + a->m_c * b->m_f + a->m_e;
    r.m_f = a->m_b * b->m_e + a->m_d * b->m_f + a->m_f;
    *out = r;
}

static void svgMatrixApply(const SvgMatrix* m, double x, double y,
                           double* ox, double* oy)
{
    *ox = m->m_a * x + m->m_c * y + m->m_e;
    *oy = m->m_b * x + m->m_d * y + m->m_f;
}

/* 求逆；不可逆（行列式接近 0）返回 false。 */
static bool svgMatrixInvert(const SvgMatrix* m, SvgMatrix* inv)
{
    double det = m->m_a * m->m_d - m->m_b * m->m_c;
    if (det > -1e-12 && det < 1e-12) return false;
    inv->m_a = m->m_d / det;
    inv->m_b = -m->m_b / det;
    inv->m_c = -m->m_c / det;
    inv->m_d = m->m_a / det;
    inv->m_e = -(inv->m_a * m->m_e + inv->m_c * m->m_f);
    inv->m_f = -(inv->m_b * m->m_e + inv->m_d * m->m_f);
    return true;
}

/* 平均轴向放大因子，用于把“用户单位”描边宽度换算为“设备单位”。 */
static double svgMatrixScale(const SvgMatrix* m)
{
    double sx = sqrt(m->m_a * m->m_a + m->m_b * m->m_b);
    double sy = sqrt(m->m_c * m->m_c + m->m_d * m->m_d);
    return (sx + sy) * 0.5;
}

/* ===================================================================== */
/* 内存竞技场（分段分配，指针稳定，供 DOM 使用）                            */
/* ===================================================================== */
typedef struct SvgArenaBlock
{
    struct SvgArenaBlock* m_next;
    size_t m_used;
    size_t m_cap;
    uint8_t* m_data;
} SvgArenaBlock;

typedef struct SvgArena
{
    SvgArenaBlock* m_head;
} SvgArena;

static void svgArenaInit(SvgArena* a)
{
    a->m_head = NULL;
}

static void svgArenaCleanup(SvgArena* a)
{
    SvgArenaBlock* b = a->m_head;
    while (b) {
        SvgArenaBlock* next = b->m_next;
        XFree_System(b);
        b = next;
    }
    a->m_head = NULL;
}

static void* svgArenaAlloc(SvgArena* a, size_t size, bool* ok)
{
    SvgArenaBlock* b;
    size_t blockSize;
    if (size == 0) size = 1;
    b = a->m_head;
    if (b && b->m_cap - b->m_used >= size) {
        uint8_t* p = b->m_data + b->m_used;
        b->m_used += size;
        return p;
    }
    blockSize = size > SVG_ARENA_BLOCK ? size : SVG_ARENA_BLOCK;
    b = (SvgArenaBlock*)XMalloc_System(sizeof(SvgArenaBlock) + blockSize);
    if (!b) {
        if (ok) *ok = false;
        return NULL;
    }
    b->m_next = a->m_head;
    b->m_used = size;
    b->m_cap = blockSize;
    b->m_data = (uint8_t*)(b + 1);
    a->m_head = b;
    if (ok) *ok = true;
    return b->m_data;
}

static const char* svgArenaCopyN(SvgArena* a, const char* src, size_t n, bool* ok)
{
    char* out = (char*)svgArenaAlloc(a, n + 1, ok);
    if (!out) return NULL;
    memcpy(out, src, n);
    out[n] = '\0';
    return out;
}

/* ===================================================================== */
/* 轻量 DOM                                                               */
/* ===================================================================== */
typedef struct SvgAttr
{
    const char* m_name;
    const char* m_value;
} SvgAttr;

typedef struct SvgNode
{
    const char* m_name;      /**< 标签名。 */
    const char* m_text;      /**< 文本内容（无则 NULL）。 */
    struct SvgNode* m_parent;
    struct SvgNode* m_first;
    struct SvgNode* m_last;
    struct SvgNode* m_next;
    SvgAttr* m_attrs;
    int m_attrCount;
    int m_attrCap;
} SvgNode;

static const char* svgNodeAttr(const SvgNode* n, const char* name)
{
    int i;
    if (!n || !name) return NULL;
    for (i = 0; i < n->m_attrCount; ++i) {
        if (strcmp(n->m_attrs[i].m_name, name) == 0)
            return n->m_attrs[i].m_value;
    }
    return NULL;
}

/* 对字符串做 XML 实体反转义（原地，只缩短不增长）。 */
static void svgUnescape(char* s)
{
    char* r = s;
    char* w = s;
    while (*r) {
        if (*r == '&') {
            char* semi = strchr(r + 1, ';');
            int c = -1;
            if (semi && (size_t)(semi - r) <= 10) {
                if (r[1] == '#') {
                    char* np = r + 2;
                    int base = 10;
                    if (*np == 'x' || *np == 'X') { ++np; base = 16; }
                    char* nend = NULL;
                    long v = strtol(np, &nend, base);
                    if (nend == semi && v >= 0 && v <= 0x7f) c = (int)v;
                } else if (semi - r == 3 && memcmp(r, "&lt", 3) == 0) c = '<';
                else if (semi - r == 3 && memcmp(r, "&gt", 3) == 0) c = '>';
                else if (semi - r == 4 && memcmp(r, "&amp", 4) == 0) c = '&';
                else if (semi - r == 5 && memcmp(r, "&quot", 5) == 0) c = '"';
                else if (semi - r == 5 && memcmp(r, "&apos", 5) == 0) c = '\'';
                else if (semi - r == 5 && memcmp(r, "&nbsp", 5) == 0) c = ' ';
                if (c >= 0) {
                    *w++ = (char)c;
                    r = semi + 1;
                    continue;
                }
            }
        }
        *w++ = *r++;
    }
    *w = '\0';
}

static bool svgNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == ':' || c == '_' ||
           c == '-' || c == '.';
}

typedef struct SvgParser
{
    char* m_pos;
    char* m_end;
    SvgArena* m_arena;
    SvgNode* m_root;
    SvgNode* m_cur;
    int m_depth;
} SvgParser;

static bool svgNodeAddAttr(SvgParser* p, SvgNode* n,
                           const char* name, const char* value)
{
    bool ok = true;
    if (n->m_attrCount == n->m_attrCap) {
        int newCap = n->m_attrCap ? n->m_attrCap * 2 : 8;
        SvgAttr* na = (SvgAttr*)svgArenaAlloc(
            p->m_arena, (size_t)newCap * sizeof(SvgAttr), &ok);
        if (!na) return false;
        if (n->m_attrCount)
            memcpy(na, n->m_attrs, (size_t)n->m_attrCount * sizeof(SvgAttr));
        n->m_attrs = na;
        n->m_attrCap = newCap;
    }
    n->m_attrs[n->m_attrCount].m_name = name;
    n->m_attrs[n->m_attrCount].m_value = value;
    ++n->m_attrCount;
    return true;
}

static const char* svgTextAppend(SvgParser* p, const char* old, const char* add)
{
    size_t olen = old ? strlen(old) : 0;
    size_t alen = strlen(add);
    bool ok = true;
    char* out;
    if (alen == 0) return old;
    out = (char*)svgArenaAlloc(p->m_arena, olen + alen + 1, &ok);
    if (!out) return old;
    if (olen) memcpy(out, old, olen);
    memcpy(out + olen, add, alen + 1);
    return out;
}

static SvgNode* svgNewNode(SvgParser* p, const char* name)
{
    bool ok = true;
    SvgNode* n = (SvgNode*)svgArenaAlloc(p->m_arena, sizeof(SvgNode), &ok);
    if (!n) return NULL;
    memset(n, 0, sizeof(SvgNode));
    n->m_name = name;
    n->m_parent = p->m_cur;
    if (p->m_cur) {
        if (p->m_cur->m_last)
            p->m_cur->m_last->m_next = n;
        else
            p->m_cur->m_first = n;
        p->m_cur->m_last = n;
    }
    return n;
}

/* 解析一个开始标签（<name attr=...> / <name .../>）。 */
static bool svgParseOpen(SvgParser* p)
{
    char* pos = p->m_pos + 1;
    char* nameStart;
    SvgNode* node;
    const char* name;
    bool ok = true;
    bool selfClosed = false;
    if (pos >= p->m_end) { return false; }
    nameStart = pos;
    while (pos < p->m_end && svgNameChar(*pos)) ++pos;
    if (pos == nameStart) { return false; }
    name = svgArenaCopyN(p->m_arena, nameStart, (size_t)(pos - nameStart), &ok);
    if (!name) return false;
    node = svgNewNode(p, name);
    if (!node) { return false; }
    while (pos < p->m_end) {
        while (pos < p->m_end &&
               (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n'))
            ++pos;
        if (pos >= p->m_end) { return false; }
        if (*pos == '>') { ++pos; break; }
        if (*pos == '/') {
            ++pos;
            if (pos < p->m_end && *pos == '>') ++pos;
            selfClosed = true;
            break;
        }
        {
            char* an = pos;
            const char* aname;
            while (pos < p->m_end && svgNameChar(*pos)) ++pos;
            if (pos == an) { return false; }
            aname = svgArenaCopyN(p->m_arena, an, (size_t)(pos - an), &ok);
            if (!aname) return false;
            while (pos < p->m_end &&
                   (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n'))
                ++pos;
            if (pos >= p->m_end || *pos != '=') { return false; }
            ++pos;
            while (pos < p->m_end &&
                   (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n'))
                ++pos;
            if (pos >= p->m_end) { return false; }
            if (*pos != '"' && *pos != '\'') { return false; }
            {
                char quote = *pos;
                char *vs, *v;
                ++pos; /* 跳过起始引号 */
                vs = pos;
                if (pos >= p->m_end) { return false; }
                while (pos < p->m_end && *pos != quote) ++pos;
                if (pos >= p->m_end) { return false; }
                v = (char*)svgArenaAlloc(p->m_arena,
                                         (size_t)(pos - vs) + 1, &ok);
                if (!v) return false;
                memcpy(v, vs, (size_t)(pos - vs));
                v[pos - vs] = '\0';
                svgUnescape(v);
                ++pos;
                if (!svgNodeAddAttr(p, node, aname, v)) return false;
            }
        }
    }
    if (!p->m_root) p->m_root = node;
    p->m_pos = pos; /* 推进解析游标，否则主循环会原地打转 */
    if (!selfClosed) {
        if (++p->m_depth > SVG_DEPTH_MAX) return false;
        p->m_cur = node;
    }
    return true;
}

/* 解析结束标签（</name>），弹出当前元素。 */
static bool svgParseClose(SvgParser* p)
{
    char* pos = p->m_pos + 2;
    while (pos < p->m_end && svgNameChar(*pos)) ++pos;
    while (pos < p->m_end &&
           (*pos == ' ' || *pos == '\t' || *pos == '\r' || *pos == '\n'))
        ++pos;
    if (pos >= p->m_end || *pos != '>') { return false; }
    if (!p->m_cur) { return false; }
    p->m_pos = pos + 1; /* 跳过结束标签 */
    p->m_cur = p->m_cur->m_parent;
    --p->m_depth;
    return true;
}

/* 解析整棵 SVG DOM。返回根节点（arena 内，随 arena 一起释放）。 */
static SvgNode* svgParseDom(const char* text, size_t size, SvgArena* arena)
{
    SvgParser p;
    char* buf;
    bool ok = false;
    if (!text || !size || size > (size_t)INT_MAX) return NULL;
    buf = (char*)svgArenaAlloc(arena, size + 1, &ok);
    if (!buf) return NULL;
    memcpy(buf, text, size);
    buf[size] = '\0';
    memset(&p, 0, sizeof(p));
    p.m_pos = buf;
    p.m_end = buf + size;
    p.m_arena = arena;
    while (p.m_pos < p.m_end) {
        char c = *p.m_pos;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++p.m_pos;
            continue;
        }
        if (c == '<') {
            if (p.m_end - p.m_pos >= 4 && p.m_pos[1] == '!' &&
                p.m_pos[2] == '-' && p.m_pos[3] == '-') {
                char* q = strstr(p.m_pos + 4, "-->");
                if (!q) return NULL;
                p.m_pos = q + 3;
            } else if (p.m_end - p.m_pos >= 2 && p.m_pos[1] == '?') {
                char* q = strstr(p.m_pos + 2, "?>");
                if (!q) return NULL;
                p.m_pos = q + 2;
            } else if (p.m_end - p.m_pos >= 2 && p.m_pos[1] == '!') {
                char* q = strchr(p.m_pos, '>');
                if (!q) return NULL;
                p.m_pos = q + 1;
            } else if (p.m_end - p.m_pos >= 2 && p.m_pos[1] == '/') {
                if (!svgParseClose(&p)) return NULL;
            } else {
                if (!svgParseOpen(&p)) return NULL;
            }
        } else {
            /* 文本内容：只保留在已知可含文本的元素内。 */
            const char* name = p.m_cur ? p.m_cur->m_name : NULL;
            bool keep = name && strcmp(name, "text") == 0;
            char* start = p.m_pos;
            while (p.m_pos < p.m_end && *p.m_pos != '<') ++p.m_pos;
            if (p.m_pos >= p.m_end) return NULL;
            if (keep) {
                char saved = *p.m_pos;
                char* t = start;
                char* tend;
                *p.m_pos = '\0';
                while (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n')
                    ++t;
                tend = t + strlen(t);
                while (tend > t &&
                       (tend[-1] == ' ' || tend[-1] == '\t' ||
                        tend[-1] == '\r' || tend[-1] == '\n'))
                    --tend;
                *tend = '\0';
                if (*t) {
                    svgUnescape(t);
                    p.m_cur->m_text = svgTextAppend(&p, p.m_cur->m_text, t);
                }
                *p.m_pos = saved; /* 恢复 '<'，由主循环处理结束标签 */
            }
            /* 其它元素内的文本直接丢弃；主循环继续处理 '<'。 */
        }
    }
    if (!p.m_root || strcmp(p.m_root->m_name, "svg") != 0) return NULL;
    return p.m_root;
}

/* ===================================================================== */
/* 数值与颜色解析                                                          */
/* ===================================================================== */

/* 读取一个数字（支持符号、整数、小数、指数）；成功返回 true 并推进 *pp。 */
static bool svgNumberScan(const char** pp, const char* end, double* out)
{
    const char* p = *pp;
    double sign = 1.0;
    double val = 0.0;
    int exp = 0;
    bool expNeg = false;
    bool got = false;
    if (p >= end) return false;
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                       *p == '\n' || *p == ','))
        ++p;
    if (p >= end) return false;
    if (*p == '-') { sign = -1.0; ++p; }
    else if (*p == '+') { ++p; }
    while (p < end && *p >= '0' && *p <= '9') {
        val = val * 10.0 + (double)(*p - '0');
        got = true;
        ++p;
    }
    if (p < end && *p == '.') {
        double div = 10.0;
        ++p;
        while (p < end && *p >= '0' && *p <= '9') {
            val += (double)(*p - '0') / div;
            div *= 10.0;
            got = true;
            ++p;
        }
    }
    if (!got) return false;
    if (p < end && (*p == 'e' || *p == 'E')) {
        const char* save = p;
        ++p;
        if (p < end && (*p == '-' || *p == '+')) {
            expNeg = (*p == '-');
            ++p;
        }
        got = false;
        while (p < end && *p >= '0' && *p <= '9') {
            exp = exp * 10 + (*p - '0');
            got = true;
            ++p;
        }
        if (!got) {
            p = save;
        } else {
            if (expNeg) exp = -exp;
            val *= pow(10.0, (double)exp);
        }
    }
    *out = sign * val;
    *pp = p;
    return true;
}

/* 解析长度：跳过空白后取数字并忽略合法单位（px/pt/em/%/ex 等）。 */
static bool svgParseLength(const char* s, double* out)
{
    double v;
    if (!s || !out) return false;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    {
        const char* end = s + strlen(s);
        if (!svgNumberScan(&s, end, &v)) return false;
    }
    *out = v;
    return true;
}

/* 解析 1..max 个数字（逗号/空白分隔），返回实际数量。 */
static bool svgParseNumberList(const char* s, double* values,
                               int maxCount, int* actualCount)
{
    int n = 0;
    const char* p;
    const char* end;
    if (!s || !values || !actualCount) return false;
    p = s;
    end = s + strlen(s);
    for (;;) {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                           *p == '\n' || *p == ','))
            ++p;
        if (p >= end) break;
        if (!svgNumberScan(&p, end, &values[n])) break;
        ++n;
        if (n == maxCount) break;
    }
    *actualCount = n;
    return n > 0;
}

/* 命名颜色（常用子集）。 */
typedef struct SvgNamedColor
{
    const char* m_name;
    uint32_t m_rgb;
} SvgNamedColor;

static const SvgNamedColor kSvgNamedColors[] = {
    {"black", 0x000000u}, {"white", 0xffffffu}, {"red", 0xff0000u},
    {"green", 0x008000u}, {"blue", 0x0000ffu}, {"yellow", 0xffff00u},
    {"cyan", 0x00ffffu}, {"aqua", 0x00ffffu}, {"magenta", 0xff00ffu},
    {"fuchsia", 0xff00ffu}, {"gray", 0x808080u}, {"grey", 0x808080u},
    {"silver", 0xc0c0c0u}, {"maroon", 0x800000u}, {"olive", 0x808000u},
    {"lime", 0x00ff00u}, {"teal", 0x008080u}, {"navy", 0x000080u},
    {"purple", 0x800080u}, {"orange", 0xffa500u}, {"brown", 0xa52a2au},
    {"pink", 0xffc0cbu}, {"gold", 0xffd700u}, {"violet", 0xee82eeu},
    {"indigo", 0x4b0082u}, {"coral", 0xff7f50u}, {"salmon", 0xfa8072u},
    {"tan", 0xd2b48cu}, {"khaki", 0xf0e68cu}, {"tomato", 0xff6347u}
};

/* 解析 SVG 颜色值（#rgb/#rrggbb/#rgba/#rrggbbaa、rgb()、命名颜色）。
 * 成功返回 true 并输出 ARGB（alpha 位来自 #aarrggbb 或 rgba 后缀或 none）。 */
static bool svgParseColor(const char* s, uint32_t* out)
{
    unsigned v = 0;
    int n;
    int i;
    if (!s || !out) return false;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    if (*s == '\0') return false;
    if (strcmp(s, "none") == 0 || strcmp(s, "transparent") == 0) {
        *out = 0x00000000u;
        return true;
    }
    if (*s == '#') {
        ++s;
        n = 0;
        for (i = 0; i < 8 && s[i]; ++i) {
            char c = s[i];
            int d = c >= '0' && c <= '9' ? c - '0'
                  : c >= 'a' && c <= 'f' ? c - 'a' + 10
                  : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (d < 0) break;
            v = (v << 4) | (unsigned)d;
            ++n;
        }
        if (n == 3) {
            unsigned r = (v >> 8) & 0xfu, g = (v >> 4) & 0xfu, b = v & 0xfu;
            v = 0xff000000u | (r * 0x11u) << 16 | (g * 0x11u) << 8 |
                (b * 0x11u);
            *out = v;
            return true;
        }
        if (n == 4) {
            unsigned r = (v >> 12) & 0xfu, g = (v >> 8) & 0xfu;
            unsigned b = (v >> 4) & 0xfu, a = v & 0xfu;
            *out = (a * 0x11u) << 24 | (r * 0x11u) << 16 |
                   (g * 0x11u) << 8 | (b * 0x11u);
            return true;
        }
        if (n == 6) {
            *out = 0xff000000u | v;
            return true;
        }
        if (n == 8) {
            unsigned b = v & 0xffu, a = (v >> 8) & 0xffu;
            unsigned g = (v >> 16) & 0xffu, r = (v >> 24) & 0xffu;
            *out = (a << 24) | (r << 16) | (g << 8) | b;
            return true;
        }
        return false;
    }
    if (strncmp(s, "rgb(", 4) == 0) {
        const char* p = s + 4;
        const char* end = s + strlen(s);
        double values[3];
        double c[3];
        int count = 0;
        p = end; /* 简化：直接使用逗号分隔解析 */
        p = s + 4;
        for (count = 0; count < 3; ++count) {
            while (p < end && (*p == ' ' || *p == ',' )) ++p;
            if (!svgNumberScan(&p, end, &values[count])) return false;
            while (p < end && *p != ',' && *p != ')') ++p;
        }
        while (p < end && *p != ')') ++p;
        if (p >= end) return false;
        for (i = 0; i < 3; ++i) {
            c[i] = values[i] < 0.0 ? 0.0 : values[i] > 100.0 ? 100.0
                                                            : values[i];
            c[i] = c[i] / 255.0;
            if (c[i] > 1.0) c[i] = 1.0;
        }
        *out = 0xff000000u |
               ((uint32_t)(c[0] * 255.0 + 0.5) << 16) |
               ((uint32_t)(c[1] * 255.0 + 0.5) << 8) |
               ((uint32_t)(c[2] * 255.0 + 0.5));
        return true;
    }
    for (i = 0; i < (int)(sizeof(kSvgNamedColors) /
                          sizeof(kSvgNamedColors[0])); ++i) {
        if (strcmp(s, kSvgNamedColors[i].m_name) == 0) {
            *out = 0xff000000u | kSvgNamedColors[i].m_rgb;
            return true;
        }
    }
    return false;
}

static bool svgParseOpacity(const char* s, double* out)
{
    double v;
    if (!svgParseLength(s, &v)) return false;
    if (v < 0.0) v = 0.0;
    if (v > 1.0) v = 1.0;
    *out = v;
    return true;
}

/* ===================================================================== */
/* 几何：点集、贝塞尔扁平化、SVG 弧、path 解析                              */
/* ===================================================================== */
typedef struct SvgShape
{
    double* m_x;
    double* m_y;
    int* m_subStart;
    bool* m_subClosed;
    int m_count;
    int m_cap;
    int m_subCount;
    int m_subCap;
} SvgShape;

static void svgShapeInit(SvgShape* s)
{
    memset(s, 0, sizeof(SvgShape));
}

static void svgShapeCleanup(SvgShape* s)
{
    if (s->m_x) XFree_System(s->m_x);
    if (s->m_y) XFree_System(s->m_y);
    if (s->m_subStart) XFree_System(s->m_subStart);
    if (s->m_subClosed) XFree_System(s->m_subClosed);
    memset(s, 0, sizeof(SvgShape));
}

static bool svgShapeAppendPoint(SvgShape* s, double x, double y)
{
    if (s->m_count == s->m_cap) {
        int newCap = s->m_cap ? s->m_cap * 2 : SVG_POINT_CHUNK;
        double* nx = (double*)XRealloc_System(s->m_x,
                                              (size_t)newCap * sizeof(double));
        double* ny;
        if (!nx) return false;
        ny = (double*)XRealloc_System(s->m_y, (size_t)newCap * sizeof(double));
        if (!ny) {
            XFree_System(nx);
            return false;
        }
        s->m_x = nx;
        s->m_y = ny;
        s->m_cap = newCap;
    }
    s->m_x[s->m_count] = x;
    s->m_y[s->m_count] = y;
    ++s->m_count;
    return true;
}

static bool svgShapeBeginSub(SvgShape* s)
{
    if (s->m_subCount == s->m_subCap) {
        int newCap = s->m_subCap ? s->m_subCap * 2 : 16;
        int* ns = (int*)XRealloc_System(s->m_subStart,
                                        (size_t)newCap * sizeof(int));
        bool* nc;
        if (!ns) return false;
        nc = (bool*)XRealloc_System(s->m_subClosed,
                                    (size_t)newCap * sizeof(bool));
        if (!nc) {
            XFree_System(ns);
            return false;
        }
        s->m_subStart = ns;
        s->m_subClosed = nc;
        s->m_subCap = newCap;
    }
    s->m_subStart[s->m_subCount] = s->m_count;
    s->m_subClosed[s->m_subCount] = false;
    ++s->m_subCount;
    return true;
}

/* 把当前最后一个子路径标记为闭合。 */
static bool svgShapeCloseSub(SvgShape* s)
{
    if (s->m_subCount == 0) return true;
    s->m_subClosed[s->m_subCount - 1] = true;
    return true;
}

static void svgShapeBBox(const SvgShape* s, double box[4])
{
    int i;
    double minX = s->m_count ? s->m_x[0] : 0.0;
    double minY = s->m_count ? s->m_y[0] : 0.0;
    double maxX = minX, maxY = minY;
    for (i = 1; i < s->m_count; ++i) {
        if (s->m_x[i] < minX) minX = s->m_x[i];
        if (s->m_x[i] > maxX) maxX = s->m_x[i];
        if (s->m_y[i] < minY) minY = s->m_y[i];
        if (s->m_y[i] > maxY) maxY = s->m_y[i];
    }
    box[0] = minX; box[1] = minY;
    box[2] = maxX - minX; box[3] = maxY - minY;
}

/* 三次贝塞尔扁平化：递归细分直至控制点接近弦。 */
static bool svgFlattenCubicInto(SvgShape* s, double x0, double y0,
                                double c1x, double c1y,
                                double c2x, double c2y,
                                double x3, double y3, int depth)
{
    double dx = x3 - x0, dy = y3 - y0;
    double len = sqrt(dx * dx + dy * dy);
    double d1, d2;
    double mx, my, qx, qy, rx, ry, wx, wy, vx, vy, ix, iy;
    if (depth >= 16) return svgShapeAppendPoint(s, x3, y3);
    if (len > 1e-9) {
        d1 = fabs((c1x - x3) * dy - (c1y - y3) * dx) / len;
        d2 = fabs((c2x - x3) * dy - (c2y - y3) * dx) / len;
    } else {
        d1 = fabs(c1x - x0) + fabs(c1y - y0);
        d2 = fabs(c2x - x0) + fabs(c2y - y0);
    }
    if (d1 < SVG_FLAT_TOLERANCE && d2 < SVG_FLAT_TOLERANCE)
        return svgShapeAppendPoint(s, x3, y3);
    mx = (x0 + c1x) * 0.5; my = (y0 + c1y) * 0.5;
    qx = (c1x + c2x) * 0.5; qy = (c1y + c2y) * 0.5;
    rx = (c2x + x3) * 0.5; ry = (c2y + y3) * 0.5;
    wx = (mx + qx) * 0.5; wy = (my + qy) * 0.5;
    vx = (qx + rx) * 0.5; vy = (qy + ry) * 0.5;
    ix = (wx + vx) * 0.5; iy = (wy + vy) * 0.5;
    if (!svgFlattenCubicInto(s, x0, y0, mx, my, wx, wy, ix, iy, depth + 1))
        return false;
    return svgFlattenCubicInto(s, ix, iy, vx, vy, rx, ry, x3, y3, depth + 1);
}

/* 二次贝塞尔扁平化（转三次控制点）。 */
static bool svgFlattenQuadInto(SvgShape* s, double x0, double y0,
                               double qx, double qy,
                               double x2, double y2, int depth)
{
    double c1x = x0 + (qx - x0) * 2.0 / 3.0;
    double c1y = y0 + (qy - y0) * 2.0 / 3.0;
    double c2x = x2 + (qx - x2) * 2.0 / 3.0;
    double c2y = y2 + (qy - y2) * 2.0 / 3.0;
    return svgFlattenCubicInto(s, x0, y0, c1x, c1y, c2x, c2y, x2, y2, depth);
}

/* SVG 椭圆弧：端点参数转中心参数后按角度采样（W3C SVG 1.1 F.6.5/F.6.6）。 */
static bool svgFlattenArcInto(SvgShape* s, double x1, double y1,
                              double rx, double ry, double phiDeg,
                              bool largeArc, bool sweep,
                              double x2, double y2, int depth)
{
    double phi = phiDeg * M_PI / 180.0;
    double cosP = cos(phi), sinP = sin(phi);
    double dx2, dy2;
    double x1p, y1p, cx, cy, cx_, cy_;
    double coef, ux, uy, vx, vy;
    double theta1, delta;
    double n, dt;
    int i;
    if (depth > SVG_HREF_DEPTH * 4) return svgShapeAppendPoint(s, x2, y2);
    if (x1 == x2 && y1 == y2) return true;
    rx = fabs(rx);
    ry = fabs(ry);
    if (rx < 1e-9 || ry < 1e-9)
        return svgShapeAppendPoint(s, x2, y2);
    dx2 = (x1 - x2) * 0.5;
    dy2 = (y1 - y2) * 0.5;
    x1p = cosP * dx2 + sinP * dy2;
    y1p = -sinP * dx2 + cosP * dy2;
    {
        double l = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry);
        if (l > 1.0) {
            double k = sqrt(l);
            rx *= k;
            ry *= k;
        }
    }
    ux = rx * rx * y1p * y1p;
    vy = ry * ry * x1p * x1p;
    coef = (rx * rx * ry * ry - ux - vy) / (ux + vy);
    if (coef < 0.0) coef = 0.0;
    coef = sqrt(coef);
    if (largeArc == sweep) coef = -coef;
    cx_ = coef * rx * y1p / ry;
    cy_ = -coef * ry * x1p / rx;
    cx = cosP * cx_ - sinP * cy_ + (x1 + x2) * 0.5;
    cy = sinP * cx_ + cosP * cy_ + (y1 + y2) * 0.5;
    ux = (x1p - cx_) / rx;
    uy = (y1p - cy_) / ry;
    vx = (-x1p - cx_) / rx;
    vy = (-y1p - cy_) / ry;
    theta1 = atan2(uy, ux);
    delta = atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (!sweep && delta > 0.0) delta -= 2.0 * M_PI;
    if (sweep && delta < 0.0) delta += 2.0 * M_PI;
    n = fabs(delta) / (M_PI / 32.0);
    if (n < 1.0) n = 1.0;
    n = floor(n);
    dt = delta / n;
    for (i = 1; i <= (int)n; ++i) {
        double t = theta1 + dt * i;
        double px = cx + rx * cos(t) * cosP - ry * sin(t) * sinP;
        double py = cy + rx * cos(t) * sinP + ry * sin(t) * cosP;
        if (!svgShapeAppendPoint(s, px, py)) return false;
    }
    return true;
}

/* 把一个椭圆扇形点的折线追加进形状（0 度指向 +x 轴，角度递增）。 */
static bool svgAppendEllipseSector(SvgShape* s, double cx, double cy,
                                   double rx, double ry,
                                   double a0Deg, double a1Deg, int n)
{
    int i;
    double a0 = a0Deg * M_PI / 180.0;
    double a1 = a1Deg * M_PI / 180.0;
    if (n < 1) n = 1;
    for (i = 1; i <= n; ++i) {
        double t = a0 + (a1 - a0) * (double)i / (double)n;
        if (!svgShapeAppendPoint(s, cx + rx * cos(t), cy + ry * sin(t)))
            return false;
    }
    return true;
}

/* path 数据上下文。 */
typedef struct SvgPathCtx
{
    SvgShape m_shape;
    double m_cx, m_cy;
    double m_sx, m_sy;
    double m_ccx, m_ccy;
    double m_qcx, m_qcy;
    int m_lastCmd;
    bool m_needStart;
} SvgPathCtx;

static bool svgPathEnsureStarted(SvgPathCtx* ctx)
{
    if (!ctx->m_needStart) return true;
    if (!svgShapeBeginSub(&ctx->m_shape)) return false;
    if (!svgShapeAppendPoint(&ctx->m_shape, ctx->m_cx, ctx->m_cy))
        return false;
    ctx->m_needStart = false;
    return true;
}

static void svgPathMoveTo(SvgPathCtx* ctx, double x, double y)
{
    ctx->m_cx = x;
    ctx->m_cy = y;
    ctx->m_sx = x;
    ctx->m_sy = y;
    ctx->m_needStart = true;
}

static bool svgPathLineTo(SvgPathCtx* ctx, double x, double y,
                          const char** pp, const char* end, int depth)
{
    (void)depth;
    if (!svgPathEnsureStarted(ctx)) return false;
    if (!svgShapeAppendPoint(&ctx->m_shape, x, y)) return false;
    ctx->m_cx = x;
    ctx->m_cy = y;
    (void)pp;
    (void)end;
    return true;
}

static bool svgPathCubicTo(SvgPathCtx* ctx, double c1x, double c1y,
                           double c2x, double c2y, double x, double y)
{
    if (!svgPathEnsureStarted(ctx)) return false;
    if (!svgFlattenCubicInto(&ctx->m_shape, ctx->m_cx, ctx->m_cy,
                             c1x, c1y, c2x, c2y, x, y, 0))
        return false;
    ctx->m_ccx = c2x;
    ctx->m_ccy = c2y;
    ctx->m_cx = x;
    ctx->m_cy = y;
    return true;
}

static bool svgPathQuadTo(SvgPathCtx* ctx, double qx, double qy,
                          double x, double y)
{
    if (!svgPathEnsureStarted(ctx)) return false;
    if (!svgFlattenQuadInto(&ctx->m_shape, ctx->m_cx, ctx->m_cy,
                            qx, qy, x, y, 0))
        return false;
    ctx->m_qcx = qx;
    ctx->m_qcy = qy;
    ctx->m_cx = x;
    ctx->m_cy = y;
    return true;
}

static bool svgPathArcTo(SvgPathCtx* ctx, double rx, double ry, double phiDeg,
                         bool largeArc, bool sweep, double x, double y)
{
    if (!svgPathEnsureStarted(ctx)) return false;
    if (!svgFlattenArcInto(&ctx->m_shape, ctx->m_cx, ctx->m_cy,
                           rx, ry, phiDeg, largeArc, sweep, x, y, 0))
        return false;
    ctx->m_cx = x;
    ctx->m_cy = y;
    return true;
}

/* 解析 path 数据到形状。 */
static bool svgParsePathData(const char* d, SvgShape* out)
{
    SvgPathCtx ctx;
    const char* p;
    const char* end;
    int lastCmd = 0;
    bool first = true;
    memset(&ctx, 0, sizeof(ctx));
    ctx.m_lastCmd = 0;
    if (!d) return false;
    p = d;
    end = d + strlen(d);
    while (p < end) {
        int cmd;
        bool rel;
        int base;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                           *p == '\n' || *p == ','))
            ++p;
        if (p >= end) break;
        if (isalpha((unsigned char)*p)) {
            cmd = (unsigned char)*p++;
            lastCmd = cmd;
        } else {
            if (lastCmd == 0 || lastCmd == 'Z' || lastCmd == 'z') {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            cmd = lastCmd;
        }
        rel = cmd >= 'a';
        base = rel ? cmd - 'a' + 'A' : cmd;
        switch (base) {
        case 'M': {
            double x, y;
            if (!svgNumberScan(&p, end, &x) || !svgNumberScan(&p, end, &y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            if (rel) { x += ctx.m_cx; y += ctx.m_cy; }
            if (first) {
                svgPathMoveTo(&ctx, x, y);
                first = false;
                for (;;) {
                    const char* save = p;
                    double x2, y2;
                    if (!svgNumberScan(&p, end, &x2) ||
                        !svgNumberScan(&p, end, &y2)) {
                        p = save;
                        break;
                    }
                    if (rel) { x2 += ctx.m_cx; y2 += ctx.m_cy; }
                    if (!svgPathLineTo(&ctx, x2, y2, &p, end, 0)) {
                        svgShapeCleanup(&ctx.m_shape);
                        return false;
                    }
                }
            } else {
                /* 之后的 M 在隐式 repeat 时应视为合法（规格要求首个必须 M） */
                svgPathMoveTo(&ctx, x, y);
                for (;;) {
                    const char* save = p;
                    double x2, y2;
                    if (!svgNumberScan(&p, end, &x2) ||
                        !svgNumberScan(&p, end, &y2)) {
                        p = save;
                        break;
                    }
                    if (rel) { x2 += ctx.m_cx; y2 += ctx.m_cy; }
                    if (!svgPathLineTo(&ctx, x2, y2, &p, end, 0)) {
                        svgShapeCleanup(&ctx.m_shape);
                        return false;
                    }
                }
            }
            break;
        }
        case 'L': {
            double x, y;
            if (!svgNumberScan(&p, end, &x) || !svgNumberScan(&p, end, &y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            if (rel) { x += ctx.m_cx; y += ctx.m_cy; }
            if (!svgPathLineTo(&ctx, x, y, &p, end, 0)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            break;
        }
        case 'H': {
            double x;
            if (!svgNumberScan(&p, end, &x)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            if (rel) x += ctx.m_cx;
            if (!svgPathLineTo(&ctx, x, ctx.m_cy, &p, end, 0)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            break;
        }
        case 'V': {
            double y;
            if (!svgNumberScan(&p, end, &y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            if (rel) y += ctx.m_cy;
            if (!svgPathLineTo(&ctx, ctx.m_cx, y, &p, end, 0)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            break;
        }
        case 'C': {
            double c1x, c1y, c2x, c2y, x, y;
            if (!svgNumberScan(&p, end, &c1x) || !svgNumberScan(&p, end, &c1y) ||
                !svgNumberScan(&p, end, &c2x) || !svgNumberScan(&p, end, &c2y) ||
                !svgNumberScan(&p, end, &x) || !svgNumberScan(&p, end, &y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            if (rel) {
                c1x += ctx.m_cx; c1y += ctx.m_cy;
                c2x += ctx.m_cx; c2y += ctx.m_cy;
                x += ctx.m_cx; y += ctx.m_cy;
            }
            if (!svgPathCubicTo(&ctx, c1x, c1y, c2x, c2y, x, y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            break;
        }
        case 'S': {
            double c2x, c2y, x, y;
            double c1x, c1y;
            if (!svgNumberScan(&p, end, &c2x) || !svgNumberScan(&p, end, &c2y) ||
                !svgNumberScan(&p, end, &x) || !svgNumberScan(&p, end, &y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            if (lastCmd == 'C' || lastCmd == 'c' || lastCmd == 'S' ||
                lastCmd == 's') {
                c1x = 2.0 * ctx.m_cx - ctx.m_ccx;
                c1y = 2.0 * ctx.m_cy - ctx.m_ccy;
            } else {
                c1x = ctx.m_cx;
                c1y = ctx.m_cy;
            }
            if (rel) {
                c2x += ctx.m_cx; c2y += ctx.m_cy;
                x += ctx.m_cx; y += ctx.m_cy;
            }
            if (!svgPathCubicTo(&ctx, c1x, c1y, c2x, c2y, x, y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            break;
        }
        case 'Q': {
            double qx, qy, x, y;
            if (!svgNumberScan(&p, end, &qx) || !svgNumberScan(&p, end, &qy) ||
                !svgNumberScan(&p, end, &x) || !svgNumberScan(&p, end, &y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            if (rel) { qx += ctx.m_cx; qy += ctx.m_cy;
                       x += ctx.m_cx; y += ctx.m_cy; }
            if (!svgPathQuadTo(&ctx, qx, qy, x, y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            break;
        }
        case 'T': {
            double x, y;
            double qx, qy;
            if (!svgNumberScan(&p, end, &x) || !svgNumberScan(&p, end, &y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            if (lastCmd == 'Q' || lastCmd == 'q' || lastCmd == 'T' ||
                lastCmd == 't') {
                qx = 2.0 * ctx.m_cx - ctx.m_qcx;
                qy = 2.0 * ctx.m_cy - ctx.m_qcy;
            } else {
                qx = ctx.m_cx;
                qy = ctx.m_cy;
            }
            if (rel) { x += ctx.m_cx; y += ctx.m_cy; }
            if (!svgPathQuadTo(&ctx, qx, qy, x, y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            break;
        }
        case 'A': {
            double rx, ry, rot, x, y;
            double fa, fs;
            if (!svgNumberScan(&p, end, &rx) || !svgNumberScan(&p, end, &ry) ||
                !svgNumberScan(&p, end, &rot) ||
                !svgNumberScan(&p, end, &fa) || !svgNumberScan(&p, end, &fs) ||
                !svgNumberScan(&p, end, &x) || !svgNumberScan(&p, end, &y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            if (rel) { x += ctx.m_cx; y += ctx.m_cy; }
            if (!svgPathArcTo(&ctx, rx, ry, rot, fa > 0.5, fs > 0.5, x, y)) {
                svgShapeCleanup(&ctx.m_shape);
                return false;
            }
            break;
        }
        case 'Z':
            if (!ctx.m_needStart && ctx.m_shape.m_subCount > 0) {
                if (!svgShapeCloseSub(&ctx.m_shape)) {
                    svgShapeCleanup(&ctx.m_shape);
                    return false;
                }
                ctx.m_needStart = true;
            }
            ctx.m_cx = ctx.m_sx;
            ctx.m_cy = ctx.m_sy;
            break;
        default:
            svgShapeCleanup(&ctx.m_shape);
            return false;
        }
    }
    if (first || ctx.m_shape.m_count == 0) {
        svgShapeCleanup(&ctx.m_shape);
        return false;
    }
    if (ctx.m_needStart && ctx.m_shape.m_subCount > 0) {
        /* 只有 moveTo 没有绘制内容时，删除空子路径；Z 已闭合的真实子路径保留。 */
        int lastStart = ctx.m_shape.m_subStart[ctx.m_shape.m_subCount - 1];
        if (ctx.m_shape.m_count - lastStart < 2)
            --ctx.m_shape.m_subCount;
    }
    *out = ctx.m_shape;
    return true;
}

/* points 属性解析（polyline/polygon）。 */
static bool svgParsePoints(const char* pts, SvgShape* shape, bool closed)
{
    double values[2];
    int count = 0;
    const char* p;
    const char* end;
    bool firstPair = true;
    if (!pts || !shape) return false;
    p = pts;
    end = pts + strlen(pts);
    for (;;) {
        int n;
        double x, y;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                           *p == '\n' || *p == ','))
            ++p;
        if (p >= end) break;
        n = 0;
        if (!svgNumberScan(&p, end, &values[0])) break;
        n = 1;
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                           *p == '\n' || *p == ','))
            ++p;
        if (p >= end || !svgNumberScan(&p, end, &values[1])) break;
        n = 2;
        (void)count;
        x = values[0];
        y = values[1];
        if (firstPair) {
            if (!svgShapeBeginSub(shape)) return false;
            firstPair = false;
        }
        if (!svgShapeAppendPoint(shape, x, y)) return false;
        (void)n;
    }
    if (firstPair) return false;
    if (closed) svgShapeCloseSub(shape);
    return true;
}

/* 圆角矩形几何。 */
static bool svgBuildRoundedRect(SvgShape* shape, double x, double y,
                                double w, double h, double rx, double ry)
{
    if (rx < 0.0) rx = 0.0;
    if (ry < 0.0) ry = 0.0;
    if (rx * 2.0 > w) rx = w * 0.5;
    if (ry * 2.0 > h) ry = h * 0.5;
    if (rx <= 0.0 || ry <= 0.0) {
        if (!svgShapeBeginSub(shape)) return false;
        if (!svgShapeAppendPoint(shape, x, y)) return false;
        if (!svgShapeAppendPoint(shape, x + w, y)) return false;
        if (!svgShapeAppendPoint(shape, x + w, y + h)) return false;
        if (!svgShapeAppendPoint(shape, x, y + h)) return false;
        svgShapeCloseSub(shape);
        return true;
    }
    if (!svgShapeBeginSub(shape)) return false;
    if (!svgShapeAppendPoint(shape, x + rx, y)) return false;
    if (!svgShapeAppendPoint(shape, x + w - rx, y)) return false;
    if (!svgAppendEllipseSector(shape, x + w - rx, y + ry, rx, ry,
                                -90.0, 0.0, 6))
        return false;
    if (!svgShapeAppendPoint(shape, x + w, y + h - ry)) return false;
    if (!svgAppendEllipseSector(shape, x + w - rx, y + h - ry, rx, ry,
                                0.0, 90.0, 6))
        return false;
    if (!svgShapeAppendPoint(shape, x + rx, y + h)) return false;
    if (!svgAppendEllipseSector(shape, x + rx, y + h - ry, rx, ry,
                                90.0, 180.0, 6))
        return false;
    if (!svgShapeAppendPoint(shape, x, y + ry)) return false;
    if (!svgAppendEllipseSector(shape, x + rx, y + ry, rx, ry,
                                180.0, 270.0, 6))
        return false;
    svgShapeCloseSub(shape);
    return true;
}

/* ===================================================================== */
/* transform 属性解析                                                     */
/* ===================================================================== */
static bool svgParseTransform(const char* s, SvgMatrix* out)
{
    SvgMatrix total;
    const char* p;
    const char* end;
    svgMatrixIdentity(&total);
    if (!s || !out) return false;
    p = s;
    end = s + strlen(s);
    for (;;) {
        SvgMatrix local;
        char fn[16];
        int fnLen = 0;
        svgMatrixIdentity(&local);
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                           *p == '\n' || *p == ','))
            ++p;
        if (p >= end) break;
        while (p < end && isalpha((unsigned char)*p) && fnLen < 15)
            fn[fnLen++] = *p++;
        fn[fnLen] = '\0';
        while (p < end && (*p == ' ' || *p == '\t')) ++p;
        if (p >= end || *p != '(') return false;
        ++p;
        {
            double vals[8];
            int n = 0;
            while (n < 8) {
                while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' ||
                                   *p == '\n' || *p == ','))
                    ++p;
                if (p < end && *p == ')') break;
                {
                    const char* save = p;
                    if (!svgNumberScan(&p, end, &vals[n])) {
                        p = save;
                        break;
                    }
                    ++n;
                }
            }
            while (p < end && *p != ')') ++p;
            if (p >= end || *p != ')') return false;
            ++p;
            if (strcmp(fn, "matrix") == 0) {
                if (n < 6) return false;
                local.m_a = vals[0]; local.m_b = vals[1];
                local.m_c = vals[2]; local.m_d = vals[3];
                local.m_e = vals[4]; local.m_f = vals[5];
            } else if (strcmp(fn, "translate") == 0) {
                if (n < 1) return false;
                local.m_e = vals[0];
                local.m_f = n >= 2 ? vals[1] : 0.0;
            } else if (strcmp(fn, "scale") == 0) {
                if (n < 1) return false;
                local.m_a = vals[0];
                local.m_d = n >= 2 ? vals[1] : vals[0];
            } else if (strcmp(fn, "rotate") == 0) {
                double ang;
                if (n < 1) return false;
                ang = vals[0] * M_PI / 180.0;
                if (n >= 3) {
                    SvgMatrix t1, t2, t3, m;
                    svgMatrixIdentity(&t1); t1.m_e = vals[1]; t1.m_f = vals[2];
                    svgMatrixIdentity(&t2);
                    t2.m_a = cos(ang); t2.m_b = sin(ang);
                    t2.m_c = -sin(ang); t2.m_d = cos(ang);
                    svgMatrixIdentity(&t3); t3.m_e = -vals[1]; t3.m_f = -vals[2];
                    svgMatrixMul(&m, &t1, &t2);
                    svgMatrixMul(&local, &m, &t3);
                } else {
                    local.m_a = cos(ang); local.m_b = sin(ang);
                    local.m_c = -sin(ang); local.m_d = cos(ang);
                }
            } else if (strcmp(fn, "skewX") == 0) {
                double ang;
                if (n < 1) return false;
                ang = vals[0] * M_PI / 180.0;
                local.m_c = tan(ang);
            } else if (strcmp(fn, "skewY") == 0) {
                double ang;
                if (n < 1) return false;
                ang = vals[0] * M_PI / 180.0;
                local.m_b = tan(ang);
            } else {
                return false;
            }
        }
        svgMatrixMul(&total, &total, &local);
    }
    *out = total;
    return true;
}

/* ===================================================================== */
/* 渐变定义                                                               */
/* ===================================================================== */
typedef struct SvgGradientStop
{
    double m_offset;
    uint32_t m_color;
} SvgGradientStop;

typedef struct SvgGradient
{
    const char* m_id;
    const char* m_href;
    int m_kind;             /* 0=线性 1=径向 */
    bool m_hasX1, m_hasY1, m_hasX2, m_hasY2;
    bool m_hasCx, m_hasCy, m_hasR, m_hasFx, m_hasFy;
    bool m_hasUnits, m_hasSpread, m_hasTransform;
    double m_x1, m_y1, m_x2, m_y2;
    double m_cx, m_cy, m_r, m_fx, m_fy;
    bool m_userSpace;
    const char* m_spread;
    SvgMatrix m_transform;
    SvgGradientStop m_stops[SVG_STOPS_MAX];
    int m_stopCount;
} SvgGradient;

/* ===================================================================== */
/* 样式                                                                   */
/* ===================================================================== */
typedef struct SvgStyle
{
    bool m_fillSet;
    uint32_t m_fillColor;
    const char* m_fillUrl;
    bool m_strokeSet;
    uint32_t m_strokeColor;
    const char* m_strokeUrl;
    bool m_strokeWidthSet;
    double m_strokeWidth;
    double m_fillOpacity;
    double m_strokeOpacity;
    double m_opacity;
} SvgStyle;

static void svgStyleInit(SvgStyle* st)
{
    st->m_fillSet = true;
    st->m_fillColor = 0xff000000u;
    st->m_fillUrl = NULL;
    st->m_strokeSet = false;
    st->m_strokeColor = 0xff000000u;
    st->m_strokeUrl = NULL;
    st->m_strokeWidthSet = false;
    st->m_strokeWidth = 1.0;
    st->m_fillOpacity = 1.0;
    st->m_strokeOpacity = 1.0;
    st->m_opacity = 1.0;
}

/* 把父样式与节点自身属性合并成有效样式。 */
static void svgStyleResolve(const SvgNode* n, const SvgStyle* parent,
                            SvgStyle* out)
{
    const char* v;
    uint32_t c;
    *out = *parent;
    if (!n) return;
    v = svgNodeAttr(n, "fill");
    if (v && strcmp(v, "inherit") != 0) {
        if (strncmp(v, "url(", 4) == 0) {
            out->m_fillUrl = v;
            out->m_fillSet = true;
        } else if (strcmp(v, "none") == 0) {
            out->m_fillSet = false;
        } else if (svgParseColor(v, &c)) {
            out->m_fillColor = c;
            out->m_fillUrl = NULL;
            out->m_fillSet = true;
        }
    }
    v = svgNodeAttr(n, "fill-opacity");
    if (v) svgParseOpacity(v, &out->m_fillOpacity);
    v = svgNodeAttr(n, "stroke");
    if (v && strcmp(v, "inherit") != 0) {
        if (strncmp(v, "url(", 4) == 0) {
            out->m_strokeUrl = v;
            out->m_strokeSet = true;
        } else if (strcmp(v, "none") == 0) {
            out->m_strokeSet = false;
        } else if (svgParseColor(v, &c)) {
            out->m_strokeColor = c;
            out->m_strokeUrl = NULL;
            out->m_strokeSet = true;
        }
    }
    v = svgNodeAttr(n, "stroke-width");
    if (v) {
        double w;
        if (svgParseLength(v, &w)) {
            out->m_strokeWidth = w < 0.0 ? 0.0 : w;
            out->m_strokeWidthSet = true;
        }
    }
    v = svgNodeAttr(n, "stroke-opacity");
    if (v) svgParseOpacity(v, &out->m_strokeOpacity);
    v = svgNodeAttr(n, "opacity");
    if (v) svgParseOpacity(v, &out->m_opacity);
}

/* ===================================================================== */
/* 渐变收集与解析                                                         */
/* ===================================================================== */
typedef struct SvgRenderer
{
    XImage* m_image;
    int m_width;
    int m_height;
    SvgNode* m_root;
    SvgGradient m_gradients[SVG_GRADIENT_MAX];
    int m_gradientCount;
    int m_depth;
} SvgRenderer;

static bool svgParseGradient(SvgRenderer* r, SvgNode* n);

static bool svgCollectGradients(SvgRenderer* r, SvgNode* n)
{
    SvgNode* c;
    if (!n) return true;
    if ((strcmp(n->m_name, "linearGradient") == 0 ||
         strcmp(n->m_name, "radialGradient") == 0) &&
        svgNodeAttr(n, "id")) {
        if (!svgParseGradient(r, n)) return false;
    }
    for (c = n->m_first; c; c = c->m_next) {
        if (!svgCollectGradients(r, c)) return false;
    }
    return true;
}

static bool svgParseGradient(SvgRenderer* r, SvgNode* n)
{
    SvgGradient* g;
    SvgNode* c;
    int i, j;
    const char* v;
    if (r->m_gradientCount >= SVG_GRADIENT_MAX) return false;
    g = &r->m_gradients[r->m_gradientCount];
    memset(g, 0, sizeof(*g));
    g->m_id = svgNodeAttr(n, "id");
    g->m_kind = strcmp(n->m_name, "radialGradient") == 0 ? 1 : 0;
    v = svgNodeAttr(n, "href");
    if (!v) v = svgNodeAttr(n, "xlink:href");
    if (v && strncmp(v, "#", 1) == 0) g->m_href = v + 1;
    else g->m_href = v;
    v = svgNodeAttr(n, "gradientUnits");
    if (v) {
        g->m_hasUnits = true;
        g->m_userSpace = strcmp(v, "userSpaceOnUse") == 0;
    }
    v = svgNodeAttr(n, "spreadMethod");
    if (v) {
        g->m_hasSpread = true;
        if (strcmp(v, "reflect") == 0 || strcmp(v, "repeat") == 0)
            g->m_spread = v;
        else
            g->m_spread = "pad";
    }
    v = svgNodeAttr(n, "gradientTransform");
    if (v) {
        g->m_hasTransform = true;
        if (!svgParseTransform(v, &g->m_transform)) {
            svgMatrixIdentity(&g->m_transform);
        }
    }
    if (g->m_kind == 0) {
        v = svgNodeAttr(n, "x1");
        if (v && svgParseLength(v, &g->m_x1)) { g->m_hasX1 = true; }
        v = svgNodeAttr(n, "y1");
        if (v && svgParseLength(v, &g->m_y1)) { g->m_hasY1 = true; }
        v = svgNodeAttr(n, "x2");
        if (v && svgParseLength(v, &g->m_x2)) { g->m_hasX2 = true; }
        v = svgNodeAttr(n, "y2");
        if (v && svgParseLength(v, &g->m_y2)) { g->m_hasY2 = true; }
    } else {
        v = svgNodeAttr(n, "cx");
        if (v && svgParseLength(v, &g->m_cx)) { g->m_hasCx = true; }
        v = svgNodeAttr(n, "cy");
        if (v && svgParseLength(v, &g->m_cy)) { g->m_hasCy = true; }
        v = svgNodeAttr(n, "r");
        if (v && svgParseLength(v, &g->m_r)) { g->m_hasR = true; }
        v = svgNodeAttr(n, "fx");
        if (v && svgParseLength(v, &g->m_fx)) { g->m_hasFx = true; }
        v = svgNodeAttr(n, "fy");
        if (v && svgParseLength(v, &g->m_fy)) { g->m_hasFy = true; }
    }
    for (c = n->m_first; c; c = c->m_next) {
        uint32_t color = 0xff000000u;
        double offset = 0.0;
        if (strcmp(c->m_name, "stop") != 0) continue;
        if (g->m_stopCount >= SVG_STOPS_MAX) break;
        v = svgNodeAttr(c, "offset");
        if (v) svgParseLength(v, &offset);
        if (!svgNodeAttr(c, "stop-color"))
            svgParseColor("black", &color);
        else if (!svgParseColor(svgNodeAttr(c, "stop-color"), &color))
            color = 0xff000000u;
        v = svgNodeAttr(c, "stop-opacity");
        if (v) {
            double op = 1.0;
            if (svgParseOpacity(v, &op))
                color = (uint32_t)(op * 255.0 + 0.5) << 24 |
                        (color & 0x00ffffffu);
        }
        g->m_stops[g->m_stopCount].m_offset = offset;
        g->m_stops[g->m_stopCount].m_color = color;
        ++g->m_stopCount;
    }
    /* 按 offset 稳定排序 */
    for (i = 1; i < g->m_stopCount; ++i) {
        SvgGradientStop key = g->m_stops[i];
        for (j = i - 1; j >= 0 && g->m_stops[j].m_offset > key.m_offset; --j)
            g->m_stops[j + 1] = g->m_stops[j];
        g->m_stops[j + 1] = key;
    }
    ++r->m_gradientCount;
    return true;
}

static const SvgGradient* svgGradientFind(SvgRenderer* r, const char* id)
{
    int i;
    if (!id) return NULL;
    for (i = 0; i < r->m_gradientCount; ++i) {
        if (r->m_gradients[i].m_id &&
            strcmp(r->m_gradients[i].m_id, id) == 0)
            return &r->m_gradients[i];
    }
    return NULL;
}

/* 从 "url(#id)" 形式的引用的 # 与 ')' 之间提取 id 并查找渐变。 */
static const SvgGradient* svgGradientFindUrl(SvgRenderer* r,
                                             const char* url)
{
    const char* p = url ? strchr(url, '#') : NULL;
    size_t n;
    int i;
    if (!p) return NULL;
    ++p;
    n = strcspn(p, ") \t\r\n");
    for (i = 0; i < r->m_gradientCount; ++i) {
        const char* gid = r->m_gradients[i].m_id;
        if (gid && strlen(gid) == n && strncmp(gid, p, n) == 0)
            return &r->m_gradients[i];
    }
    return NULL;
}

/* 解析 href 引用链并补齐默认值；返回解析结果结构。 */
static bool svgGradientResolve(SvgRenderer* r, const SvgGradient* base,
                               SvgGradient* out, int depth)
{
    if (depth == 0) memset(out, 0, sizeof(*out));
    if (depth > SVG_HREF_DEPTH) return false;
    if (base->m_href && depth < SVG_HREF_DEPTH) {
        const SvgGradient* parent = svgGradientFind(r, base->m_href);
        if (parent && !svgGradientResolve(r, parent, out, depth + 1))
            return false;
    }
    out->m_kind = base->m_kind;
    out->m_id = base->m_id;
    if (base->m_hasX1) { out->m_x1 = base->m_x1; out->m_hasX1 = true; }
    if (base->m_hasY1) { out->m_y1 = base->m_y1; out->m_hasY1 = true; }
    if (base->m_hasX2) { out->m_x2 = base->m_x2; out->m_hasX2 = true; }
    if (base->m_hasY2) { out->m_y2 = base->m_y2; out->m_hasY2 = true; }
    if (base->m_hasCx) { out->m_cx = base->m_cx; out->m_hasCx = true; }
    if (base->m_hasCy) { out->m_cy = base->m_cy; out->m_hasCy = true; }
    if (base->m_hasR) { out->m_r = base->m_r; out->m_hasR = true; }
    if (base->m_hasFx) { out->m_fx = base->m_fx; out->m_hasFx = true; }
    if (base->m_hasFy) { out->m_fy = base->m_fy; out->m_hasFy = true; }
    if (base->m_hasUnits) { out->m_userSpace = base->m_userSpace;
                            out->m_hasUnits = true; }
    if (base->m_hasSpread) { out->m_spread = base->m_spread;
                             out->m_hasSpread = true; }
    if (base->m_hasTransform) { out->m_transform = base->m_transform;
                                out->m_hasTransform = true; }
    if (base->m_stopCount) {
        memcpy(out->m_stops, base->m_stops,
               (size_t)base->m_stopCount * sizeof(SvgGradientStop));
        out->m_stopCount = base->m_stopCount;
    }
    return true;
}

static void svgGradientDefaults(SvgGradient* g)
{
    if (g->m_kind == 0) {
        if (!g->m_hasX1) g->m_x1 = 0.0;
        if (!g->m_hasY1) g->m_y1 = 0.0;
        if (!g->m_hasX2) g->m_x2 = 1.0;
        if (!g->m_hasY2) g->m_y2 = 0.0;
    } else {
        if (!g->m_hasCx) g->m_cx = 0.5;
        if (!g->m_hasCy) g->m_cy = 0.5;
        if (!g->m_hasR) g->m_r = 0.5;
        if (!g->m_hasFx) { g->m_fx = g->m_cx; g->m_hasFx = true; }
        if (!g->m_hasFy) { g->m_fy = g->m_cy; g->m_hasFy = true; }
    }
    if (!g->m_hasTransform) svgMatrixIdentity(&g->m_transform);
    if (!g->m_spread) g->m_spread = "pad";
}


/* 反射模式的实现：t' = |t| 折叠到 [0,1] */
static double svgGradientReflect(double t)
{
    t = fabs(t);
    t = t - floor(t);
    return t;
}

static void svgGradientSample(const SvgGradient* g, double t, uint32_t* out)
{
    int n = g->m_stopCount;
    int i;
    if (n == 0) {
        *out = 0x00000000u;
        return;
    }
    if (n == 1) {
        *out = g->m_stops[0].m_color;
        return;
    }
    if (t <= g->m_stops[0].m_offset) { *out = g->m_stops[0].m_color; return; }
    if (t >= g->m_stops[n - 1].m_offset) {
        *out = g->m_stops[n - 1].m_color;
        return;
    }
    for (i = 0; i < n - 1; ++i) {
        double o0 = g->m_stops[i].m_offset;
        double o1 = g->m_stops[i + 1].m_offset;
        double f;
        uint32_t c0 = g->m_stops[i].m_color;
        uint32_t c1 = g->m_stops[i + 1].m_color;
        unsigned a0, r0, g0, b0, a1, r1, g1, b1;
        if (t < o0 || t > o1) continue;
        if (o1 <= o0) f = 0.0;
        else f = (t - o0) / (o1 - o0);
        if (f < 0.0) f = 0.0;
        if (f > 1.0) f = 1.0;
        a0 = (c0 >> 24) & 0xffu; r0 = (c0 >> 16) & 0xffu;
        g0 = (c0 >> 8) & 0xffu; b0 = c0 & 0xffu;
        a1 = (c1 >> 24) & 0xffu; r1 = (c1 >> 16) & 0xffu;
        g1 = (c1 >> 8) & 0xffu; b1 = c1 & 0xffu;
        *out = ((uint32_t)(a0 + (a1 - a0) * f) << 24) |
               ((uint32_t)(r0 + (r1 - r0) * f) << 16) |
               ((uint32_t)(g0 + (g1 - g0) * f) << 8) |
               (uint32_t)(b0 + (b1 - b0) * f);
        return;
    }
    *out = g->m_stops[n - 1].m_color;
}

/* ===================================================================== */
/* 渲染器                                                                */
/* ===================================================================== */
/* SvgRenderer 完整定义见上方（矢量渲染器状态）。 */


static int svgClip(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* source-over 合成一个 ARGB 像素。 */
static uint32_t svgBlendPixel(uint32_t dst, uint32_t src)
{
    unsigned sa = (src >> 24) & 0xffu;
    unsigned da, oa;
    unsigned sr, sg, sb, dr, dg, db;
    unsigned t;
    if (sa == 0) return dst;
    if (sa == 255) return src;
    da = (dst >> 24) & 0xffu;
    oa = sa + da * (255u - sa) / 255u;
    if (oa == 0) return 0;
    sr = (src >> 16) & 0xffu; sg = (src >> 8) & 0xffu; sb = src & 0xffu;
    dr = (dst >> 16) & 0xffu; dg = (dst >> 8) & 0xffu; db = dst & 0xffu;
    t = 255u - sa;
    return (oa << 24) |
           (((sr * sa + dr * da * t / 255u) / oa) << 16) |
           (((sg * sa + dg * da * t / 255u) / oa) << 8) |
           ((sb * sa + db * da * t / 255u) / oa);
}

static void svgPutPixel(SvgRenderer* r, int x, int y,
                        uint32_t color, bool blend)
{
    uint32_t dst;
    if (x < 0 || y < 0 || x >= r->m_width || y >= r->m_height) return;
    if (!blend) {
        XImage_setPixel(r->m_image, x, y, color);
        return;
    }
    dst = XImage_pixel(r->m_image, x, y);
    XImage_setPixel(r->m_image, x, y, svgBlendPixel(dst, color));
}

static void svgGradientColorAt(SvgRenderer* r, const SvgGradient* grad,
                               double px, double py,
                               const SvgMatrix* inv, const double* bbox,
                               uint32_t* out)
{
    double ux, uy;
    (void)r;
    double t;
    double ex, ey;
    if (!grad) {
        *out = 0xff000000u;
        return;
    }
    ux = px + 0.5;
    uy = py + 0.5;
    if (inv) svgMatrixApply(inv, ux, uy, &ux, &uy);
    if (!grad->m_userSpace && bbox && bbox[2] > 1e-9 && bbox[3] > 1e-9) {
        ux = (ux - bbox[0]) / bbox[2];
        uy = (uy - bbox[1]) / bbox[3];
    }
    if (grad->m_hasTransform)
        svgMatrixApply(&grad->m_transform, ux, uy, &ux, &uy);
    ex = ux; ey = uy;
    if (grad->m_kind == 0) {
        double vx = grad->m_x2 - grad->m_x1;
        double vy = grad->m_y2 - grad->m_y1;
        double len2 = vx * vx + vy * vy;
        t = len2 > 1e-12 ? ((ex - grad->m_x1) * vx + (ey - grad->m_y1) * vy) /
                               len2 : 0.0;
    } else {
        double dx = ex - grad->m_fx;
        double dy = ey - grad->m_fy;
        double rr = grad->m_r;
        double a, b, c, disc, lam;
        if (rr <= 1e-9) {
            t = 0.0;
        } else if (dx == 0.0 && dy == 0.0) {
            t = 0.0;
        } else {
            a = dx * dx + dy * dy;
            b = 2.0 * ((grad->m_fx - grad->m_cx) * dx +
                       (grad->m_fy - grad->m_cy) * dy);
            c = (grad->m_fx - grad->m_cx) * (grad->m_fx - grad->m_cx) +
                (grad->m_fy - grad->m_cy) * (grad->m_fy - grad->m_cy) -
                rr * rr;
            disc = b * b - 4.0 * a * c;
            if (disc < 0.0) disc = 0.0;
            lam = (-b + sqrt(disc)) / (2.0 * a);
            if (lam <= 1e-9) t = 0.0;
            else t = 1.0 / lam;
        }
    }
    /* spreadMethod 处理 */
    if (grad->m_spread && strcmp(grad->m_spread, "reflect") == 0)
        t = svgGradientReflect(t);
    else if (grad->m_spread && strcmp(grad->m_spread, "repeat") == 0 &&
             fabs(t) > 1.0 && t != 0.0)
        t = t - floor(t);
    svgGradientSample(grad, t, out);
}

/* 偶奇规则逐扫描线填充 device 空间点集。 */
static bool svgFillDevice(SvgRenderer* r,
                          const double* dx, const double* dy, int count,
                          int subCount, const int* subStart,
                          bool useGradient, const SvgGradient* grad,
                          uint32_t color, double alphaMul,
                          const SvgMatrix* inv, const double* bbox)
{
    double* crosses;
    double minY, maxY;
    int y0, y1, y;
    int i, s;
    if (count < 3) return true;
    minY = dy[0]; maxY = dy[0];
    for (i = 1; i < count; ++i) {
        if (dy[i] < minY) minY = dy[i];
        if (dy[i] > maxY) maxY = dy[i];
    }
    y0 = (int)ceil(minY - 0.5);
    y1 = (int)floor(maxY + 0.5);
    if (y0 < 0) y0 = 0;
    if (y1 > r->m_height) y1 = r->m_height;
    if (y0 >= y1) return true;
    crosses = (double*)XMalloc_System((size_t)(count + subCount) *
                                      sizeof(double));
    if (!crosses) return false;
    for (y = y0; y < y1; ++y) {
        int n = 0;
        double yc = (double)y + 0.5;
        for (s = 0; s < subCount; ++s) {
            /* subStart 为空表示整条轮廓为单一子路径（描边四边形等）。 */
            int start = subStart ? subStart[s] : 0;
            int end = (s + 1 < subCount) ? subStart[s + 1] : count;
            int j;
            if (end - start < 2) continue;
            for (j = start; j < end - 1; ++j) {
                double y0e = dy[j], y1e = dy[j + 1];
                double x0e = dx[j], x1e = dx[j + 1];
                if (y0e == y1e) continue;
                if ((y0e <= yc && y1e > yc) || (y1e <= yc && y0e > yc)) {
                    crosses[n++] = x0e + (yc - y0e) * (x1e - x0e) /
                                           (y1e - y0e);
                }
            }
            /* 隐式闭合边（even-odd 填充对开放轮廓同样闭合） */
            {
                double y0e = dy[end - 1], y1e = dy[start];
                double x0e = dx[end - 1], x1e = dx[start];
                if (y0e != y1e &&
                    ((y0e <= yc && y1e > yc) || (y1e <= yc && y0e > yc))) {
                    crosses[n++] = x0e + (yc - y0e) * (x1e - x0e) /
                                           (y1e - y0e);
                }
            }
        }
        if (n < 2) continue;
        /* 插入排序（裁切到范围后扫描线穿过的边通常很少） */
        for (i = 1; i < n; ++i) {
            double key = crosses[i];
            int j = i - 1;
            while (j >= 0 && crosses[j] > key) {
                crosses[j + 1] = crosses[j];
                --j;
            }
            crosses[j + 1] = key;
        }
        for (i = 0; i + 1 < n; i += 2) {
            double xa = crosses[i];
            double xb = crosses[i + 1];
            int px0 = (int)ceil(xa - 0.5);
            int px1 = (int)floor(xb - 0.5);
            int px;
            if (px0 < 0) px0 = 0;
            if (px1 >= r->m_width) px1 = r->m_width - 1;
            for (px = px0; px <= px1; ++px) {
                uint32_t col = color;
                if (useGradient) {
                    uint32_t gc;
                    svgGradientColorAt(r, grad, (double)px, (double)y,
                                       inv, bbox, &gc);
                    col = gc;
                }
                {
                    unsigned a = (col >> 24) & 0xffu;
                    a = (unsigned)((double)a * alphaMul + 0.5);
                    if (a > 255) a = 255;
                    col = (col & 0x00ffffffu) | ((uint32_t)a << 24);
                    if (a == 255 && !useGradient)
                        svgPutPixel(r, px, y, col, false);
                    else
                        svgPutPixel(r, px, y, col, true);
                }
            }
        }
    }
    XFree_System(crosses);
    (void)subStart;
    return true;
}

/* 圆盘（用于描边端点/拐角圆头）。 */
static void svgBuildDisk(double* ox, double* oy, int* n,
                         double cx, double cy, double radius)
{
    int i;
    int steps = 16;
    *n = steps;
    for (i = 0; i < steps; ++i) {
        double t = (double)i * 2.0 * M_PI / (double)steps;
        ox[i] = cx + radius * cos(t);
        oy[i] = cy + radius * sin(t);
    }
}

/* 逐段加宽描边（quad + 顶点圆盘近似圆头连接）。 */
static bool svgStrokeDevice(SvgRenderer* r,
                            const double* dx, const double* dy, int count,
                            int subCount, const int* subStart,
                            const bool* subClosed,
                            double width, uint32_t color, double alphaMul,
                            const SvgGradient* grad,
                            const SvgMatrix* inv, const double* bbox)
{
    int s, i;
    double radius = width * 0.5;
    uint32_t col = color;
    if (width <= 0.0) return true;
    for (s = 0; s < subCount; ++s) {
        int start = subStart[s];
        int end = (s + 1 < subCount) ? subStart[s + 1] : count;
        int last = end - 1;
        bool closed = subClosed ? subClosed[s] : false;
        if (last < start) continue;
        for (i = start; i < last; ++i) {
            double segDx = dx[i + 1] - dx[i];
            double segDy = dy[i + 1] - dy[i];
            double len = sqrt(segDx * segDx + segDy * segDy);
            double nx, ny;
            double quad[8], qy[8];
            int qcount = 4;
            if (len < 1e-9) continue;
            nx = -segDy / len;
            ny = segDx / len;
            quad[0] = dx[i] + nx * radius; qy[0] = dy[i] + ny * radius;
            quad[1] = dx[i] - nx * radius; qy[1] = dy[i] - ny * radius;
            quad[2] = dx[i + 1] - nx * radius; qy[2] = dy[i + 1] - ny * radius;
            quad[3] = dx[i + 1] + nx * radius; qy[3] = dy[i + 1] + ny * radius;
            svgFillDevice(r, quad, qy, qcount, 1, NULL, grad != NULL,
                          grad, col, alphaMul, inv, bbox);
        }
        if (closed && last > start) {
            double segDx = dx[start] - dx[last];
            double segDy = dy[start] - dy[last];
            double len = sqrt(segDx * segDx + segDy * segDy);
            double nx, ny;
            double quad[8], qy[8];
            if (len > 1e-9) {
                int qcount = 4;
                nx = -segDy / len;
                ny = segDx / len;
                quad[0] = dx[last] + nx * radius; qy[0] = dy[last] + ny * radius;
                quad[1] = dx[last] - nx * radius; qy[1] = dy[last] - ny * radius;
                quad[2] = dx[start] - nx * radius; qy[2] = dy[start] - ny * radius;
                quad[3] = dx[start] + nx * radius; qy[3] = dy[start] + ny * radius;
                svgFillDevice(r, quad, qy, qcount, 1, NULL, grad != NULL,
                              grad, col, alphaMul, inv, bbox);
            }
        }
    }
    /* 顶点圆盘（圆头端点与圆角连接） */
    for (s = 0; s < subCount; ++s) {
        int start = subStart[s];
        int end = (s + 1 < subCount) ? subStart[s + 1] : count;
        int last = end - 1;
        for (i = start; i <= last; ++i) {
            double ox[16], oy[16];
            int n;
            if (radius <= 0.9 && i > start && i < last &&
                count > 2) {
                /* 特小线宽且非端点：跳过圆盘避免在折点叠加过暗 */
                if (width < 1.0 && i > start && i < last)
                    continue;
            }
            svgBuildDisk(ox, oy, &n, dx[i], dy[i], radius);
            svgFillDevice(r, ox, oy, n, 1, NULL, grad != NULL,
                          grad, col, alphaMul, inv, bbox);
        }
    }
    return true;
}

/* 通用形状：变换到设备空间后填充 + 描边。 */
static bool svgRenderShape(SvgRenderer* r, const SvgShape* shape,
                           const SvgStyle* st, const SvgMatrix* ctm)
{
    double* dx;
    double* dy;
    double userBBox[4];
    SvgMatrix inv;
    bool useGradFill = false, useGradStroke = false;
    SvgGradient gradFill, gradStroke;
    int i, s;
    double alphaMulFill, alphaMulStroke;
    if (!shape || shape->m_count < 2) return true;
    dx = (double*)XMalloc_System((size_t)shape->m_count * sizeof(double));
    dy = (double*)XMalloc_System((size_t)shape->m_count * sizeof(double));
    if (!dx || !dy) {
        if (dx) XFree_System(dx);
        if (dy) XFree_System(dy);
        return false;
    }
    svgShapeBBox(shape, userBBox);
    for (i = 0; i < shape->m_count; ++i)
        svgMatrixApply(ctm, shape->m_x[i], shape->m_y[i], &dx[i], &dy[i]);
    if (!svgMatrixInvert(ctm, &inv)) {
        svgMatrixIdentity(&inv);
    }
    alphaMulFill = st->m_fillOpacity * st->m_opacity;
    alphaMulStroke = st->m_strokeOpacity * st->m_opacity;
    if (st->m_fillUrl) {
        const SvgGradient* g0 = svgGradientFindUrl(r, st->m_fillUrl);
        if (g0 && svgGradientResolve(r, g0, &gradFill, 0)) {
            svgGradientDefaults(&gradFill);
            useGradFill = true;
        }
    }
    if (st->m_strokeUrl) {
        const SvgGradient* g0 = svgGradientFindUrl(r, st->m_strokeUrl);
        if (g0 && svgGradientResolve(r, g0, &gradStroke, 0)) {
            svgGradientDefaults(&gradStroke);
            useGradStroke = true;
        }
    }
    if (useGradFill) {
        svgFillDevice(r, dx, dy, shape->m_count, shape->m_subCount,
                      shape->m_subStart, true, &gradFill, 0,
                      alphaMulFill, &inv, userBBox);
    } else if (st->m_fillSet) {
        uint32_t col = st->m_fillColor;
        svgFillDevice(r, dx, dy, shape->m_count, shape->m_subCount,
                      shape->m_subStart, false, NULL, col,
                      alphaMulFill, NULL, NULL);
    }
    if (st->m_strokeSet && st->m_strokeWidth > 0.0) {
        double width = st->m_strokeWidth * svgMatrixScale(ctm);
        uint32_t col = st->m_strokeColor;
        svgStrokeDevice(r, dx, dy, shape->m_count, shape->m_subCount,
                        shape->m_subStart, shape->m_subClosed,
                        width, col, alphaMulStroke,
                        useGradStroke ? &gradStroke : NULL,
                        useGradStroke ? &inv : NULL,
                        useGradStroke ? userBBox : NULL);
    }
    XFree_System(dx);
    XFree_System(dy);
    (void)s;
    return true;
}

/* ===================================================================== */
/* 元素渲染                                                               */
/* ===================================================================== */
static bool svgRenderRect(SvgRenderer* r, SvgNode* n,
                          const SvgStyle* st, const SvgMatrix* ctm)
{
    SvgShape shape;
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0, rx = 0.0, ry = 0.0;
    const char* v;
    v = svgNodeAttr(n, "x"); if (v) svgParseLength(v, &x);
    v = svgNodeAttr(n, "y"); if (v) svgParseLength(v, &y);
    v = svgNodeAttr(n, "width"); if (v) svgParseLength(v, &w);
    v = svgNodeAttr(n, "height"); if (v) svgParseLength(v, &h);
    v = svgNodeAttr(n, "rx"); if (v) svgParseLength(v, &rx);
    v = svgNodeAttr(n, "ry"); if (v) svgParseLength(v, &ry);
    if (w <= 0.0 || h <= 0.0) return true;
    svgShapeInit(&shape);
    if (!svgBuildRoundedRect(&shape, x, y, w, h, rx, ry)) {
        svgShapeCleanup(&shape);
        return false;
    }
    svgRenderShape(r, &shape, st, ctm);
    svgShapeCleanup(&shape);
    return true;
}

static bool svgRenderCircle(SvgRenderer* r, SvgNode* n,
                            const SvgStyle* st, const SvgMatrix* ctm)
{
    SvgShape shape;
    double cx = 0.0, cy = 0.0, rad = 0.0;
    const char* v;
    v = svgNodeAttr(n, "cx"); if (v) svgParseLength(v, &cx);
    v = svgNodeAttr(n, "cy"); if (v) svgParseLength(v, &cy);
    v = svgNodeAttr(n, "r"); if (v) svgParseLength(v, &rad);
    if (rad <= 0.0) return true;
    svgShapeInit(&shape);
    if (!svgShapeBeginSub(&shape)) { svgShapeCleanup(&shape); return false; }
    if (!svgShapeAppendPoint(&shape, cx + rad, cy)) {
        svgShapeCleanup(&shape);
        return false;
    }
    if (!svgAppendEllipseSector(&shape, cx, cy, rad, rad, 0.0, 360.0, 48)) {
        svgShapeCleanup(&shape);
        return false;
    }
    svgShapeCloseSub(&shape);
    svgRenderShape(r, &shape, st, ctm);
    svgShapeCleanup(&shape);
    return true;
}

static bool svgRenderEllipse(SvgRenderer* r, SvgNode* n,
                             const SvgStyle* st, const SvgMatrix* ctm)
{
    SvgShape shape;
    double cx = 0.0, cy = 0.0, rx = 0.0, ry = 0.0;
    const char* v;
    v = svgNodeAttr(n, "cx"); if (v) svgParseLength(v, &cx);
    v = svgNodeAttr(n, "cy"); if (v) svgParseLength(v, &cy);
    v = svgNodeAttr(n, "rx"); if (v) svgParseLength(v, &rx);
    v = svgNodeAttr(n, "ry"); if (v) svgParseLength(v, &ry);
    if (rx <= 0.0 || ry <= 0.0) return true;
    svgShapeInit(&shape);
    if (!svgShapeBeginSub(&shape)) { svgShapeCleanup(&shape); return false; }
    if (!svgShapeAppendPoint(&shape, cx + rx, cy)) {
        svgShapeCleanup(&shape);
        return false;
    }
    if (!svgAppendEllipseSector(&shape, cx, cy, rx, ry, 0.0, 360.0, 48)) {
        svgShapeCleanup(&shape);
        return false;
    }
    svgShapeCloseSub(&shape);
    svgRenderShape(r, &shape, st, ctm);
    svgShapeCleanup(&shape);
    return true;
}

static bool svgRenderLine(SvgRenderer* r, SvgNode* n,
                          const SvgStyle* st, const SvgMatrix* ctm)
{
    SvgShape shape;
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;
    const char* v;
    if (!st->m_strokeSet || st->m_strokeWidth <= 0.0) return true;
    v = svgNodeAttr(n, "x1"); if (v) svgParseLength(v, &x1);
    v = svgNodeAttr(n, "y1"); if (v) svgParseLength(v, &y1);
    v = svgNodeAttr(n, "x2"); if (v) svgParseLength(v, &x2);
    v = svgNodeAttr(n, "y2"); if (v) svgParseLength(v, &y2);
    svgShapeInit(&shape);
    if (!svgShapeBeginSub(&shape)) { svgShapeCleanup(&shape); return false; }
    if (!svgShapeAppendPoint(&shape, x1, y1) ||
        !svgShapeAppendPoint(&shape, x2, y2)) {
        svgShapeCleanup(&shape);
        return false;
    }
    svgRenderShape(r, &shape, st, ctm);
    svgShapeCleanup(&shape);
    return true;
}

static bool svgRenderPoints(SvgRenderer* r, SvgNode* n,
                            const SvgStyle* st, const SvgMatrix* ctm,
                            bool closed)
{
    const char* pts = svgNodeAttr(n, "points");
    SvgShape shape;
    if (!pts) return true;
    svgShapeInit(&shape);
    if (!svgParsePoints(pts, &shape, closed)) {
        svgShapeCleanup(&shape);
        return true;
    }
    svgRenderShape(r, &shape, st, ctm);
    svgShapeCleanup(&shape);
    return true;
}

static bool svgRenderPath(SvgRenderer* r, SvgNode* n,
                          const SvgStyle* st, const SvgMatrix* ctm)
{
    const char* d = svgNodeAttr(n, "d");
    SvgShape shape;
    if (!d) return true;
    svgShapeInit(&shape);
    if (!svgParsePathData(d, &shape)) {
        svgShapeCleanup(&shape);
        return true;
    }
    svgRenderShape(r, &shape, st, ctm);
    svgShapeCleanup(&shape);
    return true;
}

/* 内置 5x7 位图字体（ASCII 0x20..0x7E，每字形 5 字节，bit 低位为顶行）。
 * 数据源自 Adafruit GFX 经典 5x7 字体表（BSD-3-Clause, Copyright (c) 2012
 * Adafruit Industries），仅作为字形数据内置，不构成第三方运行库。 */
static const uint8_t kSvgFont5x7[95][5] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 }, /* 32 0x20   */
    { 0x00, 0x00, 0x5f, 0x00, 0x00 }, /* 33 0x21 ! */
    { 0x00, 0x07, 0x00, 0x07, 0x00 }, /* 34 0x22 " */
    { 0x14, 0x7f, 0x14, 0x7f, 0x14 }, /* 35 0x23 # */
    { 0x24, 0x2a, 0x7f, 0x2a, 0x12 }, /* 36 0x24 $ */
    { 0x23, 0x13, 0x08, 0x64, 0x62 }, /* 37 0x25 % */
    { 0x36, 0x49, 0x56, 0x20, 0x50 }, /* 38 0x26 & */
    { 0x00, 0x08, 0x07, 0x03, 0x00 }, /* 39 0x27 ' */
    { 0x00, 0x1c, 0x22, 0x41, 0x00 }, /* 40 0x28 ( */
    { 0x00, 0x41, 0x22, 0x1c, 0x00 }, /* 41 0x29 ) */
    { 0x2a, 0x1c, 0x7f, 0x1c, 0x2a }, /* 42 0x2A * */
    { 0x08, 0x08, 0x3e, 0x08, 0x08 }, /* 43 0x2B + */
    { 0x00, 0x80, 0x70, 0x30, 0x00 }, /* 44 0x2C , */
    { 0x08, 0x08, 0x08, 0x08, 0x08 }, /* 45 0x2D - */
    { 0x00, 0x00, 0x60, 0x60, 0x00 }, /* 46 0x2E . */
    { 0x20, 0x10, 0x08, 0x04, 0x02 }, /* 47 0x2F / */
    { 0x3e, 0x51, 0x49, 0x45, 0x3e }, /* 48 0x30 0 */
    { 0x00, 0x42, 0x7f, 0x40, 0x00 }, /* 49 0x31 1 */
    { 0x72, 0x49, 0x49, 0x49, 0x46 }, /* 50 0x32 2 */
    { 0x21, 0x41, 0x49, 0x4d, 0x33 }, /* 51 0x33 3 */
    { 0x18, 0x14, 0x12, 0x7f, 0x10 }, /* 52 0x34 4 */
    { 0x27, 0x45, 0x45, 0x45, 0x39 }, /* 53 0x35 5 */
    { 0x3c, 0x4a, 0x49, 0x49, 0x31 }, /* 54 0x36 6 */
    { 0x41, 0x21, 0x11, 0x09, 0x07 }, /* 55 0x37 7 */
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, /* 56 0x38 8 */
    { 0x46, 0x49, 0x49, 0x29, 0x1e }, /* 57 0x39 9 */
    { 0x00, 0x00, 0x14, 0x00, 0x00 }, /* 58 0x3A : */
    { 0x00, 0x40, 0x34, 0x00, 0x00 }, /* 59 0x3B ; */
    { 0x00, 0x08, 0x14, 0x22, 0x41 }, /* 60 0x3C < */
    { 0x14, 0x14, 0x14, 0x14, 0x14 }, /* 61 0x3D = */
    { 0x00, 0x41, 0x22, 0x14, 0x08 }, /* 62 0x3E > */
    { 0x02, 0x01, 0x59, 0x09, 0x06 }, /* 63 0x3F ? */
    { 0x3e, 0x41, 0x5d, 0x59, 0x4e }, /* 64 0x40 @ */
    { 0x7c, 0x12, 0x11, 0x12, 0x7c }, /* 65 0x41 A */
    { 0x7f, 0x49, 0x49, 0x49, 0x36 }, /* 66 0x42 B */
    { 0x3e, 0x41, 0x41, 0x41, 0x22 }, /* 67 0x43 C */
    { 0x7f, 0x41, 0x41, 0x41, 0x3e }, /* 68 0x44 D */
    { 0x7f, 0x49, 0x49, 0x49, 0x41 }, /* 69 0x45 E */
    { 0x7f, 0x09, 0x09, 0x09, 0x01 }, /* 70 0x46 F */
    { 0x3e, 0x41, 0x41, 0x51, 0x73 }, /* 71 0x47 G */
    { 0x7f, 0x08, 0x08, 0x08, 0x7f }, /* 72 0x48 H */
    { 0x00, 0x41, 0x7f, 0x41, 0x00 }, /* 73 0x49 I */
    { 0x20, 0x40, 0x41, 0x3f, 0x01 }, /* 74 0x4A J */
    { 0x7f, 0x08, 0x14, 0x22, 0x41 }, /* 75 0x4B K */
    { 0x7f, 0x40, 0x40, 0x40, 0x40 }, /* 76 0x4C L */
    { 0x7f, 0x02, 0x1c, 0x02, 0x7f }, /* 77 0x4D M */
    { 0x7f, 0x04, 0x08, 0x10, 0x7f }, /* 78 0x4E N */
    { 0x3e, 0x41, 0x41, 0x41, 0x3e }, /* 79 0x4F O */
    { 0x7f, 0x09, 0x09, 0x09, 0x06 }, /* 80 0x50 P */
    { 0x3e, 0x41, 0x51, 0x21, 0x5e }, /* 81 0x51 Q */
    { 0x7f, 0x09, 0x19, 0x29, 0x46 }, /* 82 0x52 R */
    { 0x26, 0x49, 0x49, 0x49, 0x32 }, /* 83 0x53 S */
    { 0x03, 0x01, 0x7f, 0x01, 0x03 }, /* 84 0x54 T */
    { 0x3f, 0x40, 0x40, 0x40, 0x3f }, /* 85 0x55 U */
    { 0x1f, 0x20, 0x40, 0x20, 0x1f }, /* 86 0x56 V */
    { 0x3f, 0x40, 0x38, 0x40, 0x3f }, /* 87 0x57 W */
    { 0x63, 0x14, 0x08, 0x14, 0x63 }, /* 88 0x58 X */
    { 0x03, 0x04, 0x78, 0x04, 0x03 }, /* 89 0x59 Y */
    { 0x61, 0x59, 0x49, 0x4d, 0x43 }, /* 90 0x5A Z */
    { 0x00, 0x7f, 0x41, 0x41, 0x41 }, /* 91 0x5B [ */
    { 0x02, 0x04, 0x08, 0x10, 0x20 }, /* 92 0x5C \ */
    { 0x00, 0x41, 0x41, 0x41, 0x7f }, /* 93 0x5D ] */
    { 0x04, 0x02, 0x01, 0x02, 0x04 }, /* 94 0x5E ^ */
    { 0x40, 0x40, 0x40, 0x40, 0x40 }, /* 95 0x5F _ */
    { 0x00, 0x03, 0x07, 0x08, 0x00 }, /* 96 0x60 ` */
    { 0x20, 0x54, 0x54, 0x78, 0x40 }, /* 97 0x61 a */
    { 0x7f, 0x28, 0x44, 0x44, 0x38 }, /* 98 0x62 b */
    { 0x38, 0x44, 0x44, 0x44, 0x28 }, /* 99 0x63 c */
    { 0x38, 0x44, 0x44, 0x28, 0x7f }, /* 100 0x64 d */
    { 0x38, 0x54, 0x54, 0x54, 0x18 }, /* 101 0x65 e */
    { 0x00, 0x08, 0x7e, 0x09, 0x02 }, /* 102 0x66 f */
    { 0x18, 0xa4, 0xa4, 0x9c, 0x78 }, /* 103 0x67 g */
    { 0x7f, 0x08, 0x04, 0x04, 0x78 }, /* 104 0x68 h */
    { 0x00, 0x44, 0x7d, 0x40, 0x00 }, /* 105 0x69 i */
    { 0x20, 0x40, 0x40, 0x3d, 0x00 }, /* 106 0x6A j */
    { 0x7f, 0x10, 0x28, 0x44, 0x00 }, /* 107 0x6B k */
    { 0x00, 0x41, 0x7f, 0x40, 0x00 }, /* 108 0x6C l */
    { 0x7c, 0x04, 0x78, 0x04, 0x78 }, /* 109 0x6D m */
    { 0x7c, 0x08, 0x04, 0x04, 0x78 }, /* 110 0x6E n */
    { 0x38, 0x44, 0x44, 0x44, 0x38 }, /* 111 0x6F o */
    { 0xfc, 0x18, 0x24, 0x24, 0x18 }, /* 112 0x70 p */
    { 0x18, 0x24, 0x24, 0x18, 0xfc }, /* 113 0x71 q */
    { 0x7c, 0x08, 0x04, 0x04, 0x08 }, /* 114 0x72 r */
    { 0x48, 0x54, 0x54, 0x54, 0x24 }, /* 115 0x73 s */
    { 0x04, 0x04, 0x3f, 0x44, 0x24 }, /* 116 0x74 t */
    { 0x3c, 0x40, 0x40, 0x20, 0x7c }, /* 117 0x75 u */
    { 0x1c, 0x20, 0x40, 0x20, 0x1c }, /* 118 0x76 v */
    { 0x3c, 0x40, 0x30, 0x40, 0x3c }, /* 119 0x77 w */
    { 0x44, 0x28, 0x10, 0x28, 0x44 }, /* 120 0x78 x */
    { 0x4c, 0x90, 0x90, 0x90, 0x7c }, /* 121 0x79 y */
    { 0x44, 0x64, 0x54, 0x4c, 0x44 }, /* 122 0x7A z */
    { 0x00, 0x08, 0x36, 0x41, 0x00 }, /* 123 0x7B { */
    { 0x00, 0x00, 0x77, 0x00, 0x00 }, /* 124 0x7C | */
    { 0x00, 0x41, 0x36, 0x08, 0x00 }, /* 125 0x7D } */
    { 0x02, 0x01, 0x02, 0x04, 0x02 }, /* 126 0x7E ~ */
};

/* 文本渲染：5x7 点阵位图按 font-size 缩放到设备空间。 */
static bool svgRenderText(SvgRenderer* r, SvgNode* n,
                          const SvgStyle* st, const SvgMatrix* ctm)
{
    const char* text = n->m_text;
    const char* v;
    double x = 0.0, y = 0.0, fontSize = 16.0;
    double step, adv;
    size_t len;
    size_t i;
    SvgShape shape;
    if (!text || !*text) return true;
    v = svgNodeAttr(n, "x"); if (v) svgParseLength(v, &x);
    v = svgNodeAttr(n, "y"); if (v) svgParseLength(v, &y);
    v = svgNodeAttr(n, "font-size");
    if (v) {
        double fs;
        if (svgParseLength(v, &fs)) fontSize = fs;
    }
    if (fontSize <= 0.0) return true;
    step = fontSize / 7.0;
    len = strlen(text);
    adv = 0.0;
    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 0x20 && c <= 0x7e) adv += 6.0 * step;
    }
    v = svgNodeAttr(n, "text-anchor");
    if (v) {
        if (strcmp(v, "middle") == 0) x -= adv * 0.5;
        else if (strcmp(v, "end") == 0) x -= adv;
    }
    svgShapeInit(&shape);
    for (i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        int col, row;
        if (c < 0x20 || c > 0x7e) continue;
        for (col = 0; col < 5; ++col) {
            uint8_t byte = kSvgFont5x7[c - 0x20][col];
            for (row = 0; row < 7; ++row) {
                double px, py;
                if (!(byte & (1u << row))) continue;
                px = x + (double)col * step;
                py = y + (double)row * step;
                if (!svgShapeBeginSub(&shape)) {
                    svgShapeCleanup(&shape);
                    return false;
                }
                if (!svgShapeAppendPoint(&shape, px, py) ||
                    !svgShapeAppendPoint(&shape, px + step, py) ||
                    !svgShapeAppendPoint(&shape, px + step, py + step) ||
                    !svgShapeAppendPoint(&shape, px, py + step)) {
                    svgShapeCleanup(&shape);
                    return false;
                }
                svgShapeCloseSub(&shape);
            }
        }
        x += 6.0 * step;
    }
    if (shape.m_count > 0) svgRenderShape(r, &shape, st, ctm);
    svgShapeCleanup(&shape);
    return true;
}

static bool svgRenderNode(SvgRenderer* r, SvgNode* n,
                          const SvgStyle* parentStyle,
                          const SvgMatrix* parentCtm)
{
    SvgStyle st;
    SvgMatrix ctm;
    const char* name;
    const char* tf;
    SvgNode* c;
    if (++r->m_depth > SVG_DEPTH_MAX) {
        --r->m_depth;
        return false;
    }
    svgStyleResolve(n, parentStyle, &st);
    ctm = *parentCtm;
    tf = svgNodeAttr(n, "transform");
    if (tf) {
        SvgMatrix local;
        if (svgParseTransform(tf, &local))
            svgMatrixMul(&ctm, &ctm, &local);
    }
    name = n->m_name;
    if (strcmp(name, "g") == 0 || strcmp(name, "a") == 0 ||
        strcmp(name, "svg") == 0) {
        for (c = n->m_first; c; c = c->m_next) {
            if (!svgRenderNode(r, c, &st, &ctm)) {
                --r->m_depth;
                return false;
            }
        }
    } else if (strcmp(name, "defs") == 0) {
        /* 渐变已在预扫描时收集，defs 内容不参与渲染 */
    } else if (strcmp(name, "rect") == 0) {
        svgRenderRect(r, n, &st, &ctm);
    } else if (strcmp(name, "circle") == 0) {
        svgRenderCircle(r, n, &st, &ctm);
    } else if (strcmp(name, "ellipse") == 0) {
        svgRenderEllipse(r, n, &st, &ctm);
    } else if (strcmp(name, "line") == 0) {
        svgRenderLine(r, n, &st, &ctm);
    } else if (strcmp(name, "polyline") == 0) {
        svgRenderPoints(r, n, &st, &ctm, false);
    } else if (strcmp(name, "polygon") == 0) {
        svgRenderPoints(r, n, &st, &ctm, true);
    } else if (strcmp(name, "path") == 0) {
        svgRenderPath(r, n, &st, &ctm);
    } else if (strcmp(name, "text") == 0) {
        svgRenderText(r, n, &st, &ctm);
    }
    /* title/desc/metadata/style/image 等元素在此静默跳过 */
    --r->m_depth;
    return true;
}

/* viewBox 与 preserveAspectRatio 计算根变换。 */
static bool svgRootTransform(SvgRenderer* r, SvgNode* root,
                             double width, double height, SvgMatrix* ctm)
{
    const char* vb = svgNodeAttr(root, "viewBox");
    (void)r;
    const char* par = svgNodeAttr(root, "preserveAspectRatio");
    double values[4];
    int count = 0;
    int ax = 1, ay = 1;
    bool slice = false;
    bool stretch = false;
    svgMatrixIdentity(ctm);
    if (vb) {
        svgParseNumberList(vb, values, 4, &count);
        if (count < 4) return false;
    }
    if (!vb) return true;
    if (par) {
        if (strstr(par, "none")) {
            stretch = true;
        } else {
            if (strstr(par, "xMin")) ax = 0;
            else if (strstr(par, "xMax")) ax = 2;
            if (strstr(par, "YMin")) ay = 0;
            else if (strstr(par, "YMax")) ay = 2;
            if (strstr(par, "slice")) slice = true;
        }
    }
    {
        double minX = values[0], minY = values[1];
        double vbW = values[2], vbH = values[3];
        double scale;
        double tx, ty;
        if (vbW <= 0.0 || vbH <= 0.0) return false;
        if (stretch) {
            double sx = width / vbW;
            double sy = height / vbH;
            tx = -minX * sx;
            ty = -minY * sy;
            ctm->m_a = sx;
            ctm->m_d = sy;
            ctm->m_e = tx;
            ctm->m_f = ty;
            return true;
        }
        if (slice)
            scale = fmax(width / vbW, height / vbH);
        else
            scale = fmin(width / vbW, height / vbH);
        tx = -minX * scale + (width - vbW * scale) * (double)ax * 0.5;
        ty = -minY * scale + (height - vbH * scale) * (double)ay * 0.5;
        ctm->m_a = scale;
        ctm->m_d = scale;
        ctm->m_e = tx;
        ctm->m_f = ty;
    }
    return true;
}

/* SVG 矢量解码主体。 */
static bool svgVectorDecode(const char* text, size_t size, XImage* out)
{
    SvgArena arena;
    SvgNode* root;
    SvgRenderer r;
    SvgMatrix rootCtm;
    SvgStyle rootStyle;
    double width = 0.0, height = 0.0;
    const char* v;
    double dims[2] = {0.0, 0.0};
    int okCount = 0;
    SvgNode* c;
    memset(&r, 0, sizeof(r));
    svgArenaInit(&arena);
    root = svgParseDom(text, size, &arena);
    if (!root) {
        svgArenaCleanup(&arena);
        return false;
    }
    v = svgNodeAttr(root, "width");
    if (v && svgParseLength(v, &dims[0]) && dims[0] > 0.0) width = dims[0];
    v = svgNodeAttr(root, "height");
    if (v && svgParseLength(v, &dims[1]) && dims[1] > 0.0) height = dims[1];
    if (width <= 0.0 || height <= 0.0) {
        /* 缺失宽/高时以 viewBox 尺寸兜底。 */
        const char* vb = svgNodeAttr(root, "viewBox");
        double values[4];
        int n = 0;
        if (vb && svgParseNumberList(vb, values, 4, &n) && n == 4 &&
            values[2] > 0.0 && values[3] > 0.0) {
            if (width <= 0.0) width = values[2];
            if (height <= 0.0) height = values[3];
        }
    }
    if (width <= 0.0 || height <= 0.0) {
        svgArenaCleanup(&arena);
        return false;
    }
    r.m_width = svgClip((int)(width + 0.5), 1, 16384);
    r.m_height = svgClip((int)(height + 0.5), 1, 16384);
    XImage_init_ex(out, r.m_width, r.m_height, XImageFormat_ARGB32);
    if (XImage_isNull(out)) {
        svgArenaCleanup(&arena);
        return false;
    }
    r.m_image = out;
    r.m_root = root;
    if (!svgCollectGradients(&r, root)) {
        svgArenaCleanup(&arena);
        XImage_deinit_base(out);
        return false;
    }
    if (!svgRootTransform(&r, root, r.m_width, r.m_height, &rootCtm)) {
        svgArenaCleanup(&arena);
        XImage_deinit_base(out);
        return false;
    }
    svgStyleInit(&rootStyle);
    for (c = root->m_first; c; c = c->m_next) {
        if (!svgRenderNode(&r, c, &rootStyle, &rootCtm)) {
            svgArenaCleanup(&arena);
            XImage_deinit_base(out);
            return false;
        }
    }
    svgArenaCleanup(&arena);
    return true;
}

#endif /* XIMAGECODEC_SVG_VECTOR_ON */

#if !XIMAGECODEC_SVG_VECTOR_ON
/* 基础 SVG 尺寸字符串解析：供裁剪掉矢量渲染后的尺寸探测/解码使用。 */
static bool svgParseLengthSimple(const char* s, double* out)
{
    double value = 0.0;
    const char* p = s;
    bool digits = false;
    bool dot = false;
    bool quoted = false;
    if (!s || !out) return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p == '=' || *p == ':') {
        ++p;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    }
    if (*p == '"' || *p == '\'') {
        quoted = true;
        ++p;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10.0 + (double)(*p - '0');
        digits = true;
        ++p;
    }
    if (*p == '.') {
        double scale = 0.1;
        ++p; dot = true;
        while (*p >= '0' && *p <= '9') {
            value += (double)(*p - '0') * scale;
            scale *= 0.1;
            digits = true;
            ++p;
        }
    }
    if (!digits) return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (*p == 'p' && p[1] == 'x') p += 2;
    if (*p == '%' || *p == 'e' || *p == 'm' || *p == 'p' ||
        *p == 'c' || *p == 'v' || *p == 'i' || *p == 'q') return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (quoted && (*p == '"' || *p == '\'')) ++p;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    *out = value;
    return true;
}

static bool svgParseNumberListSimple(const char* s, double* values,
                                     int count, int* outCount)
{
    int n = 0;
    if (!s || !values || !outCount) return false;
    for (;;) {
        double value = 0.0;
        double scale = 0.1;
        bool digits = false;
        bool dot = false;
        const char* p = s;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' ||
               *p == ',') ++p;
        if (*p == '=' || *p == ':') {
            ++p;
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        }
        if (*p == '"' || *p == '\'') ++p;
        if (!*p) break;
        s = p;
        while (*p && ((*p >= '0' && *p <= '9') || *p == '.')) {
            if (*p == '.') {
                if (dot) break;
                dot = true;
            } else {
                digits = true;
                if (!dot) {
                    value = value * 10.0 + (double)(*p - '0');
                } else {
                    value += (double)(*p - '0') * scale;
                    scale *= 0.1;
                }
            }
            ++p;
        }
        if (!digits) break;
        if (n >= count) break;
        values[n++] = value;
        while (*s && *s != ',' && *s != ' ' && *s != '\t' &&
               *s != '\r' && *s != '\n') ++s;
    }
    *outCount = n;
    if (n < count) return false;
    return true;
}

static int svgClipSimple(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
#endif /* !XIMAGECODEC_SVG_VECTOR_ON */

/* ===================================================================== */
/* SVG 默认尺寸探测                                                        */
/* ===================================================================== */
/**
 * @brief 从 SVG 数据中探测默认宽与高。
 * @param data   输入 SVG 数据；不能为 NULL。
 * @param size   输入数据字节数。
 * @param width  输出宽度；成功后大于 0。
 * @param height 输出高度；成功后大于 0。
 * @return 成功返回 true；无宽高/viewBox 或解析失败返回 false。
 * @note 优先读取根 svg 的 width/height 属性，缺失时回退 viewBox 尺寸，
 *       与解密路径的尺寸选择一致；不创建图像、不解码完整像素。
 */
bool XImageCodecInternal_probeSvgSize(const uint8_t* data, size_t size,
                                      int* width, int* height)
{
#if XIMAGECODEC_SVG_VECTOR_ON
    SvgArena arena;
    SvgNode* root;
    const char* v;
    double dims[2] = {0.0, 0.0};
    double widthD = 0.0, heightD = 0.0;
    int ok = 0;
    if (!data || !size || !width || !height) return false;
    svgArenaInit(&arena);
    root = svgParseDom((const char*)data, size, &arena);
    if (!root || strcmp(root->m_name, "svg") != 0) {
        svgArenaCleanup(&arena);
        return false;
    }
    v = svgNodeAttr(root, "width");
    if (v && svgParseLength(v, &dims[0]) && dims[0] > 0.0) widthD = dims[0];
    v = svgNodeAttr(root, "height");
    if (v && svgParseLength(v, &dims[1]) && dims[1] > 0.0) heightD = dims[1];
    if (widthD <= 0.0 || heightD <= 0.0) {
        const char* vb = svgNodeAttr(root, "viewBox");
        double values[4];
        int n = 0;
        if (vb && svgParseNumberList(vb, values, 4, &n) && n == 4 &&
            values[2] > 0.0 && values[3] > 0.0) {
            if (widthD <= 0.0) widthD = values[2];
            if (heightD <= 0.0) heightD = values[3];
        }
    }
    if (widthD <= 0.0 || heightD <= 0.0 || widthD > (double)INT_MAX ||
        heightD > (double)INT_MAX) {
        svgArenaCleanup(&arena);
        return false;
    }
    *width = svgClip((int)(widthD + 0.5), 1, 16384);
    *height = svgClip((int)(heightD + 0.5), 1, 16384);
    ok = 1;
    svgArenaCleanup(&arena);
    return ok != 0;
#else
    char* text;
    const char* widthAttr = NULL;
    const char* heightAttr = NULL;
    const char* viewBox = NULL;
    double widthD = 0.0, heightD = 0.0;
    if (!data || !size || !width || !height) return false;
    text = (char*)XMalloc_System(size + 1);
    if (!text) return false;
    memcpy(text, data, size);
    text[size] = '\0';
    widthAttr = strstr(text, "width");
    heightAttr = strstr(text, "height");
    viewBox = strstr(text, "viewBox");
    if (widthAttr &&
        (size_t)(widthAttr - text) < size) {
        double v;
        if (svgParseLengthSimple(widthAttr + 5, &v) && v > 0.0) widthD = v;
    }
    if (heightAttr &&
        (size_t)(heightAttr - text) < size) {
        double v;
        if (svgParseLengthSimple(heightAttr + 6, &v) && v > 0.0) heightD = v;
    }
    if ((widthD <= 0.0 || heightD <= 0.0) && viewBox &&
        (size_t)(viewBox - text) < size) {
        double values[4] = {0.0, 0.0, 0.0, 0.0};
        int n = 0;
        if (svgParseNumberListSimple(viewBox + 7, values, 4, &n) &&
            n == 4 && values[2] > 0.0 && values[3] > 0.0) {
            if (widthD <= 0.0) widthD = values[2];
            if (heightD <= 0.0) heightD = values[3];
        }
    }
    XFree_System(text);
    if (widthD <= 0.0 || heightD <= 0.0 || widthD > (double)INT_MAX ||
        heightD > (double)INT_MAX)
        return false;
    *width = svgClipSimple((int)(widthD + 0.5), 1, 16384);
    *height = svgClipSimple((int)(heightD + 0.5), 1, 16384);
    return true;
#endif
}

/* ===================================================================== */
/* SVG 统一解码入口                                                        */
/* ===================================================================== */
/**
 * @brief 解码 SVG 数据到 XImage。
 * @note  解码顺序（按优先级）：
 *        1. 内嵌 PNG（data:image/png;base64）逐像素还原（编码路径的产物）；
 *        2. 矢量渲染（受 XIMAGECODEC_SVG_VECTOR_ON 开关控制，见本文件头部注释）；
 *        3. 纯色矩形栅格化（历史兼容回退，矢量渲染失败或关闭时使用）。
 * @param data  输入 SVG 数据；不能为 NULL。
 * @param size  数据字节数。
 * @param out   输出图像对象，成功后由调用者负责释放。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_decodeSvg(const uint8_t* data, size_t size, XImage* out)
{
    const char* marker = "data:image/png;base64,";
    const uint8_t* p;
    char* text;
    if (!data || !out || !size) return false;
    text = (char*)XMalloc_System(size + 1);
    if (!text) return false;
    memcpy(text, data, size);
    text[size] = '\0';

    /* 形态 1：内嵌 PNG 位图（编码路径的产物，逐像素还原）。 */
    p = (const uint8_t*)strstr(text, marker);
    if (p) {
        const uint8_t* end;
        XByteArray* encoded;
        bool result;
        p += strlen(marker);
        end = (const uint8_t*)strchr((const char*)p, '"');
        if (!end) end = (const uint8_t*)text + size;
        encoded = svgBase64Decode(p, (size_t)(end - p));
        result = encoded &&
                 XImageCodecInternal_decodePng(
                     XByteArray_data(encoded),
                     XByteArray_size_base((const XContainer*)encoded), out);
        if (encoded) XByteArray_delete_base((XClass*)encoded);
        XFree_System(text);
        return result;
    }

#if XIMAGECODEC_SVG_VECTOR_ON
    /* 形态 2：矢量渲染。 */
    if (svgVectorDecode(text, size, out)) {
        XFree_System(text);
        return true;
    }
#endif /* XIMAGECODEC_SVG_VECTOR_ON */

    /* 形态 3：纯色矩形栅格化（回退路径，保证关闭矢量渲染后仍可用）。 */
    {
        int width, height;
        uint32_t color;
        XImage temp;
        width = svgNumber((const uint8_t*)text, size, "width", 0);
        height = svgNumber((const uint8_t*)text, size, "height", 0);
        if (width <= 0 || height <= 0 || width > INT_MAX || height > INT_MAX) {
            XFree_System(text);
            return false;
        }
        color = svgColor((const uint8_t*)text, size);
        XImage_init_ex(&temp, width, height, XImageFormat_ARGB32);
        if (XImage_isNull(&temp)) {
            XFree_System(text);
            return false;
        }
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                XImage_setPixel(&temp, x, y, color);
        XImage_deinit_base(out);
        out->m_data = temp.m_data;
        temp.m_data = NULL;
    }
    XFree_System(text);
    return true;
}

/* ===================================================================== */
/* SVG 统一编码入口                                                        */
/* ===================================================================== */
/**
 * @brief 将 XImage 编码为 SVG 数据（内嵌 data:image/png;base64 位图）。
 * @param image  输入图像；不能为 NULL。
 * @param out    输出字节数组；不能为 NULL。
 * @return 成功返回 true。
 */
bool XImageCodecInternal_encodeSvg(const XImage* image, XByteArray* out)
{
    XByteArray* png = XByteArray_create();
    int width, height;
    char header[160];
    int length;
    if (!png || !image || !out || XImage_isNull(image)) {
        if (png) XByteArray_delete_base((XClass*)png);
        return false;
    }
    if (!XImageCodecInternal_encodePng(image, png)) {
        XByteArray_delete_base((XClass*)png);
        return false;
    }
    width = XImage_width(image);
    height = XImage_height(image);
    length = snprintf(header, sizeof(header),
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\">"
        "<image width=\"%d\" height=\"%d\" href=\"data:image/png;base64,",
        width, height, width, height);
    if (length < 0 || !XByteArray_resize_base((XVector*)out, 0) ||
        !XImageCodecInternal_appendBytes(out, header, (size_t)length) ||
        !svgBase64Append(out, XByteArray_data(png),
                         XByteArray_size_base((const XContainer*)png)) ||
        !XImageCodecInternal_appendBytes(out, "\"/></svg>", 9)) {
        XByteArray_delete_base((XClass*)png);
        return false;
    }
    XByteArray_delete_base((XClass*)png);
    return true;
}

#endif /* XIMAGECODEC_SVG_ON */
#endif /* XIMAGECODEC_ON */
