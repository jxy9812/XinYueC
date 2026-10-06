#define XGUI_REMOTE_CODEC_ERR_DST_TOO_SMALL 3

/* ---- 旧实现(git HEAD, xrc_→o_) ---- */
typedef struct XrcRleSink {
    uint8_t* dst;
    size_t   cap;
    size_t   pos;
    bool     overflow;
} XrcRleSink;

static void o_rle_put(XrcRleSink* s, const uint8_t* bytes, size_t n)
{
    if (s->overflow) {
        return;
    }
    if (s->pos + n > s->cap) {
        s->overflow = true;
        return;
    }
    memcpy(s->dst + s->pos, bytes, n);
    s->pos += n;
}

/** @brief 线上行主序(带 stride)取第 idx 个单元的地址。 */
static const uint8_t* o_wire_unit(const uint8_t* wire, int stride, int w,
                                    size_t idx, int bpp)
{
    return wire + (size_t)(idx / (size_t)w) * (size_t)(unsigned int)stride +
           (idx % (size_t)w) * (size_t)(unsigned int)bpp;
}

/** @brief 冲刷一段待定字面量[start, start+count), 补控制字节后逐单元原样输出。 */
static void o_rle_flush_literals(XrcRleSink* s, const uint8_t* wire, int stride,
                                   int w, int bpp, size_t start, size_t count)
{
    if (count == 0 || s->overflow) {
        return;
    }
    uint8_t ctrl = (uint8_t)(count - 1u); /* c<0x80: 字面量段 c+1 单元。 */
    o_rle_put(s, &ctrl, 1u);
    for (size_t k = 0; k < count; ++k) {
        o_rle_put(s, o_wire_unit(wire, stride, w, start + k, bpp),
                    (size_t)(unsigned int)bpp);
    }
}

static int o_rle_encode(const uint8_t* wire, int wireStride,
                          int w, int h, int bpp,
                          uint8_t* dst, size_t dstCap)
{
    XrcRleSink s;
    s.dst = dst;
    s.cap = dstCap;
    s.pos = 0;
    s.overflow = false;

    size_t unitCount = (size_t)(unsigned int)w * (size_t)(unsigned int)h;
    size_t litStart  = 0; /* 待定字面量段起始单元下标。 */
    size_t litCount  = 0; /* 待定字面量段单元数(<=127 常态)。 */
    size_t i = 0;
    while (i < unitCount) {
        const uint8_t* u = o_wire_unit(wire, wireStride, w, i, bpp);
        size_t run = 1; /* 自 i 起的重复长度, 封顶 129(重复段上限)。 */
        while (run < 129u && i + run < unitCount &&
               memcmp(o_wire_unit(wire, wireStride, w, i + run, bpp),
                      u, (size_t)(unsigned int)bpp) == 0) {
            ++run;
        }
        if (run >= 3u) {
            /* 贪婪: >=3 单元重复用重复段(c-0x80+2 次), 先冲刷字面量。 */
            o_rle_flush_literals(&s, wire, wireStride, w, bpp, litStart, litCount);
            litStart = i + run;
            litCount = 0;
            uint8_t ctrl = (uint8_t)(0x80u + (run - 2u));
            o_rle_put(&s, &ctrl, 1u);
            o_rle_put(&s, u, (size_t)(unsigned int)bpp);
        } else {
            if (litCount == 0u) {
                litStart = i;
            }
            litCount += run;
            while (litCount >= 128u) { /* 字面量段单元上限 128。 */
                o_rle_flush_literals(&s, wire, wireStride, w, bpp, litStart, 128u);
                litStart += 128u;
                litCount -= 128u;
            }
        }
        i += run;
    }
    o_rle_flush_literals(&s, wire, wireStride, w, bpp, litStart, litCount);
    if (s.overflow) {
        return -XGUI_REMOTE_CODEC_ERR_DST_TOO_SMALL;
    }
    return (int)s.pos;
}

/* ---- 新实现(隔离树 XGuiRemoteCodec.c:228-390 逐字提取, xrc_→n_) ---- */
/** @brief 带容量闸门的字节汇。 */
typedef struct NrcRleSink {
    uint8_t* dst;
    size_t   cap;
    size_t   pos;
    bool     overflow;
} NrcRleSink;

static void n_rle_put(NrcRleSink* s, const uint8_t* bytes, size_t n)
{
    if (s->overflow) {
        return;
    }
    if (s->pos + n > s->cap) {
        s->overflow = true;
        return;
    }
    memcpy(s->dst + s->pos, bytes, n);
    s->pos += n;
}

/** @brief 像素单元等值(bpp 2/4 展开比较, 其余 memcmp 兜底; 替代逐单元
 *         memcmp 调用——mcgs round4 实测 RLE 编码 475 tile ~69ms 主热点)。 */
static bool n_px_eq(const uint8_t* a, const uint8_t* b, int bpp)
{
    if (bpp == 2) {
        return a[0] == b[0] && a[1] == b[1];
    }
    if (bpp == 4) {
        return a[0] == b[0] && a[1] == b[1] &&
               a[2] == b[2] && a[3] == b[3];
    }
    return memcmp(a, b, (size_t)bpp) == 0;
}

/** @brief 冲刷一段待定字面量[start, start+count), 补控制字节后按行分块
 *        原样输出(单元流=行主序, 行间 stride 填充不参与; 与旧逐单元
 *        xrc_wire_unit 寻址逐字节同输出, 仅去掉每单元 div/mod+调用)。 */
static void n_rle_flush_literals(NrcRleSink* s, const uint8_t* wire, int stride,
                                   int w, int bpp, size_t start, size_t count)
{
    if (count == 0 || s->overflow) {
        return;
    }
    uint8_t ctrl = (uint8_t)(count - 1u); /* c<0x80: 字面量段 c+1 单元。 */
    n_rle_put(s, &ctrl, 1u);
    {
        size_t row = start / (size_t)(unsigned int)w;
        size_t col = start % (size_t)(unsigned int)w;
        while (count > 0 && !s->overflow) {
            const uint8_t* p = wire +
                row * (size_t)(unsigned int)stride + col * (size_t)(unsigned int)bpp;
            size_t chunk = (size_t)(unsigned int)w - col;
            if (chunk > count) {
                chunk = count;
            }
            n_rle_put(s, p, chunk * (size_t)(unsigned int)bpp);
            col += chunk;
            if (col == (size_t)(unsigned int)w) {
                col = 0;
                ++row;
            }
            count -= chunk;
        }
    }
}

static int n_rle_encode(const uint8_t* wire, int wireStride,
                          int w, int h, int bpp,
                          uint8_t* dst, size_t dstCap)
{
    NrcRleSink s;
    s.dst = dst;
    s.cap = dstCap;
    s.pos = 0;
    s.overflow = false;

    size_t unitCount = (size_t)(unsigned int)w * (size_t)(unsigned int)h;
    size_t litStart  = 0; /* 待定字面量段起始单元下标。 */
    size_t litCount  = 0; /* 待定字面量段单元数(<=127 常态)。 */
    size_t i = 0;
    /* 行游标直走(2026-10-04 mcgs round5): 旧实现对每个单元做
     * div/mod 寻址 + 逐像素 memcmp 调用(475 tile 实测 ~69ms)。行游标
     * 版地址经 (row,col) 增量推进+跨行整跳, 像素等值走 n_px_eq 展开
     * 比较; 贪婪语义(≥3 重复段封顶 129/字面量段 ≤128/控制字节编码)
     * 与单元流(行主序、跨行连续)逐字节保持一致。 */
    size_t row = 0;
    size_t col = 0;
    const uint8_t* cur = wire; /* 单元 i 的地址(与 (row,col) 同步)。 */

    while (i < unitCount) {
        const uint8_t* u = cur;
        size_t run = 1; /* 自 i 起的重复长度, 封顶 129(重复段上限)。 */
        const uint8_t* scan = cur;
        size_t srow = row;
        size_t scol = col;
        while (run < 129u && i + run < unitCount) {
            size_t ncol = scol + 1;
            size_t nrow = srow;
            if (ncol == (size_t)(unsigned int)w) {
                ncol = 0;
                ++nrow;
            }
            scan = (ncol != 0)
                       ? scan + (size_t)(unsigned int)bpp
                       : wire + nrow * (size_t)(unsigned int)wireStride;
            srow = nrow;
            scol = ncol;
            if (!n_px_eq(scan, u, bpp)) {
                break; /* scan 已停在首个异值单元(= i+run, 游标即对)。 */
            }
            ++run;
        }
        if (run < 129u && i + run < unitCount) {
            /* 异值退出: scan= i+run。 */
        } else {
            /* 封顶/到尾退出: scan 停在末等值单元(i+run-1), 再进一格对齐
             * i+run(越过 tile 末尾的悬空地址不会被解引用——外层随即
             * 因 i==unitCount 退出)。 */
            size_t ncol = scol + 1;
            size_t nrow = srow;
            if (ncol == (size_t)(unsigned int)w) {
                ncol = 0;
                ++nrow;
            }
            scan = (ncol != 0)
                       ? scan + (size_t)(unsigned int)bpp
                       : wire + nrow * (size_t)(unsigned int)wireStride;
            srow = nrow;
            scol = ncol;
        }
        if (run >= 3u) {
            /* 贪婪: >=3 单元重复用重复段(c-0x80+2 次), 先冲刷字面量。 */
            n_rle_flush_literals(&s, wire, wireStride, w, bpp, litStart, litCount);
            litStart = i + run;
            litCount = 0;
            uint8_t ctrl = (uint8_t)(0x80u + (run - 2u));
            n_rle_put(&s, &ctrl, 1u);
            n_rle_put(&s, u, (size_t)(unsigned int)bpp);
        } else {
            if (litCount == 0u) {
                litStart = i;
            }
            litCount += run;
            while (litCount >= 128u) { /* 字面量段单元上限 128。 */
                n_rle_flush_literals(&s, wire, wireStride, w, bpp, litStart, 128u);
                litStart += 128u;
                litCount -= 128u;
            }
        }
        i += run;
        cur = scan;
        row = srow;
        col = scol;
    }
    n_rle_flush_literals(&s, wire, wireStride, w, bpp, litStart, litCount);
    if (s.overflow) {
        return -XGUI_REMOTE_CODEC_ERR_DST_TOO_SMALL;
    }
    return (int)s.pos;
}
