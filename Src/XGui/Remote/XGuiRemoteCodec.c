/**
 * @file       XGuiRemoteCodec.c
 * @brief      XGuiRemote tile 像素编解码实现(RAW/RLE/zlib + 像素格式转换 + FNV-1a)。
 * @details    实现 XGuiRemoteCodec.h 冻结契约; RLE 线上格式冻结于 XGuiRemote.md §5.4:
 *             控制字节 c<0x80 为字面量段(后随 c+1 个像素单元), c>=0x80 为重复段
 *             (后随 1 个单元, 重复 c-0x80+2 次), 单元 = 每像素字节数(2 或 4)。
 *             全部函数为纯函数: 无全局状态、无锁、可重入, 编码线程可直接调用。
 *             唯一的临时缓冲来自调用栈上的定长 scratch(超出走系统堆, 仅在
 *             源/线上格式不一致或 zlib 需要连续源时才触碰), 不引入任何模块级状态。
 * @author     XinYueC 团队
 */
#include "XGuiRemoteCodec.h"

#if XGUI_REMOTE_ON

#include <string.h>
#include <limits.h>
#include "XMemory.h" /* XMalloc_System/XFree_System, 仓库统一分配口 */
#include "XGuiRemoteCodecNeon.h" /* [perf9 路4] RLE 游标扫描 NEON 内核
                                  * (无 NEON 产物整头裁空, 恒标量)。 */

#if XGUI_REMOTE_ZLIB_ON
#include "zlib.h" /* 先例: Src/XData/XExcel/XZipReader.c:6, compress2/uncompress */
#endif

/* ==================== 常量与局部辅助 ==================== */

/** @brief 栈上 scratch 字节数(固定上限)。128x128 RGB565 tile(32KB)以内免堆。 */
#define XRC_SCRATCH_BYTES (32 * 1024)

/** @brief ZLIB 预算附加余量(帧/tile 记录头级别的保守常数)。 */
#define XRC_ZLIB_MARGIN 16

/**
 * @brief 局部 scratch: 小缓冲走栈, 超上限走系统堆(纯函数语义不受影响)。
 */
typedef struct XrcScratch {
    uint8_t  stack[XRC_SCRATCH_BYTES]; /**< 栈上固定上限缓冲。 */
    uint8_t* heap;                     /**< 超上限时的堆缓冲(NULL=未用堆)。 */
    uint8_t* buf;                      /**< 实际可用缓冲(栈或堆)。 */
} XrcScratch;

/** @brief 取得 bytes 字节临时缓冲; 堆分配失败返回 NULL。 */
static uint8_t* xrc_scratch_acquire(XrcScratch* s, size_t bytes)
{
    s->heap = NULL;
    s->buf  = s->stack;
    if (bytes > sizeof(s->stack)) {
        s->heap = (uint8_t*)XMalloc_System(bytes);
        s->buf  = s->heap;
    }
    return s->buf;
}

/** @brief 释放 scratch(仅堆路径有实质动作)。 */
static void xrc_scratch_release(XrcScratch* s)
{
    if (s->heap != NULL) {
        XFree_System(s->heap);
        s->heap = NULL;
    }
}

/** @brief 线上格式每像素字节数; 非法格式返回 0。 */
static int xrc_bpp(XGuiRemotePixelFormat fmt)
{
    switch (fmt) {
    case XGUI_REMOTE_PF_ARGB32: return 4;
    case XGUI_REMOTE_PF_RGB565: return 2;
    default:                    return 0;
    }
}

/** @brief 像素格式合法且 codec 已编译时的统一参数闸门。 */
static bool xrc_params_ok(const void* p1, const void* p2,
                          int w, int h, int sb, int db)
{
    return p1 != NULL && p2 != NULL && w > 0 && h > 0 && sb > 0 && db > 0;
}

/* ==================== hasCodec ==================== */

bool XGuiRemoteCodec_hasCodec(XGuiRemoteCodecId codec)
{
    switch (codec) {
    case XGUI_REMOTE_CODEC_RAW:
    case XGUI_REMOTE_CODEC_RLE:
        return true;
    case XGUI_REMOTE_CODEC_ZLIB:
#if XGUI_REMOTE_ZLIB_ON
        return true;
#else
        return false;
#endif
    default:
        return false;
    }
}

/* ==================== maxEncodedSize ==================== */

size_t XGuiRemoteCodec_maxEncodedSize(XGuiRemoteCodecId codec,
                                      int w, int h, int bytesPerPixel)
{
    if (w <= 0 || h <= 0 || bytesPerPixel <= 0) {
        return 0;
    }
    switch (codec) {
    case XGUI_REMOTE_CODEC_RAW:
    case XGUI_REMOTE_CODEC_RLE:
    case XGUI_REMOTE_CODEC_ZLIB:
        break;
    default:
        return 0;
    }

    uint64_t units = (uint64_t)(unsigned int)w * (uint64_t)(unsigned int)h;
    uint64_t raw   = units * (uint64_t)(unsigned int)bytesPerPixel;
    if (raw > (uint64_t)SIZE_MAX) {
        return 0; /* 该尺寸 tile 在本平台不可能分配, 视为参数非法。 */
    }

    if (codec == XGUI_REMOTE_CODEC_RAW) {
        return (size_t)raw;
    }
    if (codec == XGUI_REMOTE_CODEC_RLE) {
        /* 最坏 = 全部单元散成字面量: 每满 128 单元多 1 控制字节。
         * 预算按字节数取上界 ceil(units*bpp/128) >= 真实控制字节数
         * ceil(units/128)(bpp>=2 恒成立), 偏保守不漏界。 */
        return (size_t)(raw + (raw + 127u) / 128u);
    }

    /* ZLIB: zlib 文档上界(源 + 源/1000 + 12)与 RAW 较大者, 再加帧余量。
     * 该式恒 >= compressBound()(真式系数和约 0.000306 < 0.001, 13 < 28),
     * 保证 encodeTile 的 zlib 产物必然装得进按此分配的缓冲。 */
    uint64_t zbound = raw + raw / 1000u + 12u + (uint64_t)XRC_ZLIB_MARGIN;
    uint64_t worst  = (zbound > raw) ? zbound : raw;
    if (worst > (uint64_t)SIZE_MAX) {
        return 0;
    }
    return (size_t)worst;
}

/* ==================== convertPixels ==================== */

void XGuiRemoteCodec_convertPixels(const uint8_t* src,
                                   XGuiRemotePixelFormat srcFormat,
                                   uint8_t* dst,
                                   XGuiRemotePixelFormat dstFormat,
                                   int pixelCount)
{
    if (src == NULL || dst == NULL || pixelCount <= 0) {
        return;
    }
    int sb = xrc_bpp(srcFormat);
    int db = xrc_bpp(dstFormat);
    if (sb == 0 || db == 0) {
        return; /* 线上枚举仅两个值, 非法组合(不存在)按防御性空操作。 */
    }
    if (srcFormat == dstFormat) {
        memcpy(dst, src, (size_t)pixelCount * (size_t)sb);
        return;
    }

    if (srcFormat == XGUI_REMOTE_PF_ARGB32) {
        /* ARGB32(内存序 B,G,R,A) -> RGB565(小端)。逐字节访问, 免对齐与别名问题。 */
        for (int i = 0; i < pixelCount; ++i) {
            const uint8_t* p = src + (size_t)i * 4u;
            uint8_t b = p[0];
            uint8_t g = p[1];
            uint8_t r = p[2];
            uint16_t v = (uint16_t)(((uint16_t)(r >> 3) << 11) |
                                    ((uint16_t)(g >> 2) << 5)  |
                                    (uint16_t)(b >> 3));
            dst[(size_t)i * 2u]      = (uint8_t)(v & 0xFFu);
            dst[(size_t)i * 2u + 1u] = (uint8_t)(v >> 8);
        }
    } else {
        /* RGB565 -> ARGB32。高位位复制展开(r5<<3|r5>>2 等), 使
         * "不透明像素往返转换位相等"(验收断言)对全部 565 输入成立。 */
        for (int i = 0; i < pixelCount; ++i) {
            uint16_t v  = (uint16_t)((uint16_t)src[(size_t)i * 2u] |
                                     (uint16_t)((uint16_t)src[(size_t)i * 2u + 1u] << 8));
            uint8_t  r5 = (uint8_t)((v >> 11) & 0x1Fu);
            uint8_t  g6 = (uint8_t)((v >> 5) & 0x3Fu);
            uint8_t  b5 = (uint8_t)(v & 0x1Fu);
            uint8_t* p  = dst + (size_t)i * 4u;
            p[0] = (uint8_t)((b5 << 3) | (b5 >> 2)); /* B */
            p[1] = (uint8_t)((g6 << 2) | (g6 >> 4)); /* G */
            p[2] = (uint8_t)((r5 << 3) | (r5 >> 2)); /* R */
            p[3] = 0xFFu;                            /* A(不透明) */
        }
    }
}

/* ==================== tileHash ==================== */

uint32_t XGuiRemoteCodec_tileHash(const uint8_t* pixels, size_t bytes)
{
    uint32_t h = 2166136261u; /* FNV-1a 32 位偏移初值。 */
    if (pixels == NULL) {
        return h;
    }
    for (size_t i = 0; i < bytes; ++i) {
        h ^= (uint32_t)pixels[i];
        h *= 16777619u; /* FNV-1a 32 位素数。 */
    }
    return h;
}

/* ==================== RAW 编码(行读取, 收敛行填充) ==================== */

static int xrc_raw_encode(const uint8_t* wire, int wireStride,
                          int w, int h, int bpp,
                          uint8_t* dst, size_t dstCap)
{
    size_t rowBytes = (size_t)(unsigned int)w * (size_t)(unsigned int)bpp;
    size_t total    = rowBytes * (size_t)(unsigned int)h;
    if (dstCap < total) {
        return -XGUI_REMOTE_CODEC_ERR_DST_TOO_SMALL;
    }
    for (int y = 0; y < h; ++y) {
        memcpy(dst + (size_t)y * rowBytes,
               wire + (size_t)y * (size_t)(unsigned int)wireStride,
               rowBytes);
    }
    return (int)total;
}

/* ==================== RLE 编码(贪婪: >=3 重复用重复段) ==================== */

/** @brief 带容量闸门的字节汇。 */
typedef struct XrcRleSink {
    uint8_t* dst;
    size_t   cap;
    size_t   pos;
    bool     overflow;
} XrcRleSink;

static void xrc_rle_put(XrcRleSink* s, const uint8_t* bytes, size_t n)
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
static bool xrc_px_eq(const uint8_t* a, const uint8_t* b, int bpp)
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
static void xrc_rle_flush_literals(XrcRleSink* s, const uint8_t* wire, int stride,
                                   int w, int bpp, size_t start, size_t count)
{
    if (count == 0 || s->overflow) {
        return;
    }
    uint8_t ctrl = (uint8_t)(count - 1u); /* c<0x80: 字面量段 c+1 单元。 */
    xrc_rle_put(s, &ctrl, 1u);
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
            xrc_rle_put(s, p, chunk * (size_t)(unsigned int)bpp);
            col += chunk;
            if (col == (size_t)(unsigned int)w) {
                col = 0;
                ++row;
            }
            count -= chunk;
        }
    }
}

static int xrc_rle_encode(const uint8_t* wire, int wireStride,
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
    /* 行游标直走(2026-10-04 mcgs round5): 旧实现对每个单元做
     * div/mod 寻址 + 逐像素 memcmp 调用(475 tile 实测 ~69ms)。行游标
     * 版地址经 (row,col) 增量推进+跨行整跳, 像素等值走 xrc_px_eq 展开
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
            /* 候选单元 = i+run(自"上次已比单元 i+run-1"进一格), 与标量
             * 路径同一推进算式; 循环不变量: 进入循环体时 scan/(srow,scol)
             * = 单元 i+run-1, 退出时 = 单元 i+run(封顶/到尾退出允许悬空
             * 至 tile 末尾后一格——不 dereference, 外层随即退出)。 */
            size_t ncol = scol + 1;
            size_t nrow = srow;
            const uint8_t* cand;
            size_t maxAdd;
            if (ncol == (size_t)(unsigned int)w) {
                ncol = 0;
                ++nrow;
            }
            cand = (ncol != 0)
                       ? scan + (size_t)(unsigned int)bpp
                       : wire + nrow * (size_t)(unsigned int)wireStride;
            maxAdd = 129u - run;
            if (maxAdd > unitCount - i - run) {
                maxAdd = unitCount - i - run; /* 到尾夹取。 */
            }
            if (maxAdd > (size_t)(unsigned int)w - ncol) {
                /* 行内连续段夹取: NEON 批只比当前行内单元——跨行
                 * stride 跳变不连续, 行尾/尾块/封顶全部回标量单步
                 * 路径(未初始化安全红线: 批读不越过行尾字节)。 */
                maxAdd = (size_t)(unsigned int)w - ncol;
            }
#if XRC_NEON_ON
            if (maxAdd >= ((bpp == 2) ? 8u : (bpp == 4 ? 4u : 129u))) {
                size_t add = xrc_neon_run_count(cand, u, bpp, maxAdd);
                run += add;
                if (add < maxAdd) {
                    /* 批内首异: 游标停首个异值单元(= i+run), 同标量
                     * break 口径(下述出口分支判"异值退出"不再进格)。 */
                    scan = cand + add * (size_t)(unsigned int)bpp;
                    srow = nrow;
                    scol = ncol + add;
                    break;
                }
                /* 整批等值: 游标停"末已比等值单元"(= i+run-1, 必在本行
                 * 内——add ≤ 行内余量), 与标量路径出口约定一致; 封顶/
                 * 到尾交给下方统一"再进一格"对齐, 行尾则换行续比。 */
                scan = cand + (add - 1u) * (size_t)(unsigned int)bpp;
                srow = nrow;
                scol = ncol + add - 1u;
                continue;
            }
#endif
            scan = cand;
            srow = nrow;
            scol = ncol;
            if (!xrc_px_eq(scan, u, bpp)) {
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
            xrc_rle_flush_literals(&s, wire, wireStride, w, bpp, litStart, litCount);
            litStart = i + run;
            litCount = 0;
            uint8_t ctrl = (uint8_t)(0x80u + (run - 2u));
            xrc_rle_put(&s, &ctrl, 1u);
            xrc_rle_put(&s, u, (size_t)(unsigned int)bpp);
        } else {
            if (litCount == 0u) {
                litStart = i;
            }
            litCount += run;
            while (litCount >= 128u) { /* 字面量段单元上限 128。 */
                xrc_rle_flush_literals(&s, wire, wireStride, w, bpp, litStart, 128u);
                litStart += 128u;
                litCount -= 128u;
            }
        }
        i += run;
        cur = scan;
        row = srow;
        col = scol;
    }
    xrc_rle_flush_literals(&s, wire, wireStride, w, bpp, litStart, litCount);
    if (s.overflow) {
        return -XGUI_REMOTE_CODEC_ERR_DST_TOO_SMALL;
    }
    return (int)s.pos;
}

/* ==================== tile 写出器(带 stride 与按需转换) ==================== */

/**
 * @brief 把线上格式单元流按 (row,col) 游标写入带 stride 的目标缓冲,
 *        途中完成线上格式 -> 目标格式转换; 越过 tile 边界即报越界。
 */
typedef struct XrcTileWriter {
    uint8_t*              dst;
    int                   stride;
    int                   w;
    int                   h;
    int                   sb;      /* 线上格式字节数。 */
    int                   db;      /* 目标格式字节数。 */
    XGuiRemotePixelFormat srcFmt;
    XGuiRemotePixelFormat dstFmt;
    int                   row;
    int                   col;
} XrcTileWriter;

/** @brief 写入 n 个连续线上格式单元; 越界返回 false。 */
static bool xrc_writer_write(XrcTileWriter* t, const uint8_t* u, size_t n)
{
    while (n > 0u) {
        if (t->row >= t->h) {
            return false; /* 流越过 tile 末尾: 越界。 */
        }
        int    room = t->w - t->col;
        size_t take = (n < (size_t)room) ? n : (size_t)room;
        uint8_t* out = t->dst + (size_t)t->row * (size_t)(unsigned int)t->stride +
                       (size_t)t->col * (size_t)(unsigned int)t->db;
        XGuiRemoteCodec_convertPixels(u, t->srcFmt, out, t->dstFmt, (int)take);
        t->col += (int)take;
        if (t->col >= t->w) {
            t->col = 0;
            ++t->row;
        }
        u += (size_t)take * (size_t)(unsigned int)t->sb;
        n -= take;
    }
    return true;
}

/** @brief 把单个线上格式单元重复写 count 次(先转换一次再填充)。 */
static bool xrc_writer_repeat(XrcTileWriter* t, const uint8_t* unit, size_t count)
{
    uint8_t one[4]; /* 单元最大 4 字节(ARGB32)。 */
    XGuiRemoteCodec_convertPixels(unit, t->srcFmt, one, t->dstFmt, 1);
    while (count > 0u) {
        if (t->row >= t->h) {
            return false;
        }
        int    room = t->w - t->col;
        size_t take = (count < (size_t)room) ? count : (size_t)room;
        uint8_t* out = t->dst + (size_t)t->row * (size_t)(unsigned int)t->stride +
                       (size_t)t->col * (size_t)(unsigned int)t->db;
        for (size_t k = 0; k < take; ++k) {
            memcpy(out + k * (size_t)(unsigned int)t->db, one,
                   (size_t)(unsigned int)t->db);
        }
        t->col += (int)take;
        if (t->col >= t->w) {
            t->col = 0;
            ++t->row;
        }
        count -= take;
    }
    return true;
}

/* ==================== RLE 解码(流式, 严格截断/越界检查) ==================== */

static int xrc_rle_decode(const uint8_t* encoded, size_t encodedBytes,
                          XGuiRemotePixelFormat srcFormat,
                          uint8_t* dstPixels, int dstStrideBytes,
                          XGuiRemotePixelFormat dstFormat,
                          int w, int h, int sb, int db)
{
    XrcTileWriter t;
    t.dst = dstPixels;
    t.stride = dstStrideBytes;
    t.w = w;
    t.h = h;
    t.sb = sb;
    t.db = db;
    t.srcFmt = srcFormat;
    t.dstFmt = dstFormat;
    t.row = 0;
    t.col = 0;

    size_t unitCount = (size_t)(unsigned int)w * (size_t)(unsigned int)h;
    size_t written = 0;
    size_t i = 0;
    while (written < unitCount) {
        if (i >= encodedBytes) {
            return -XGUI_REMOTE_CODEC_ERR_CORRUPT; /* 截断: 控制字节缺失。 */
        }
        uint8_t c = encoded[i++];
        if (c < 0x80u) {
            size_t count = (size_t)c + 1u;
            if (count > unitCount - written) {
                return -XGUI_REMOTE_CODEC_ERR_CORRUPT; /* 越过 tile 边界。 */
            }
            if (i + count * (size_t)(unsigned int)sb > encodedBytes) {
                return -XGUI_REMOTE_CODEC_ERR_CORRUPT; /* 字面量段截断。 */
            }
            if (!xrc_writer_write(&t, encoded + i, count)) {
                return -XGUI_REMOTE_CODEC_ERR_CORRUPT;
            }
            i += count * (size_t)(unsigned int)sb;
            written += count;
        } else {
            size_t count = (size_t)c - 0x80u + 2u;
            if (count > unitCount - written) {
                return -XGUI_REMOTE_CODEC_ERR_CORRUPT;
            }
            if (i + (size_t)(unsigned int)sb > encodedBytes) {
                return -XGUI_REMOTE_CODEC_ERR_CORRUPT; /* 重复段单元截断。 */
            }
            if (!xrc_writer_repeat(&t, encoded + i, count)) {
                return -XGUI_REMOTE_CODEC_ERR_CORRUPT;
            }
            i += (size_t)(unsigned int)sb;
            written += count;
        }
    }
    if (i != encodedBytes) {
        return -XGUI_REMOTE_CODEC_ERR_CORRUPT; /* 尾部多余字节 = 非法流。 */
    }
    return 0;
}

/* ==================== encodeTile ==================== */

int XGuiRemoteCodec_encodeTile(const uint8_t* srcPixels, int srcStrideBytes,
                               XGuiRemotePixelFormat srcFormat,
                               int w, int h,
                               XGuiRemotePixelFormat dstFormat,
                               XGuiRemoteCodecId codec, int zlibLevel,
                               uint8_t* dst, size_t dstCap)
{
    int sb = xrc_bpp(srcFormat);
    int db = xrc_bpp(dstFormat);
    if (!xrc_params_ok(srcPixels, dst, w, h, sb, db)) {
        return -XGUI_REMOTE_CODEC_ERR_BAD_PARAM;
    }
    if (srcStrideBytes < w * sb) {
        return -XGUI_REMOTE_CODEC_ERR_BAD_PARAM; /* stride 容不下整行。 */
    }
    if (!XGuiRemoteCodec_hasCodec(codec)) {
        return -XGUI_REMOTE_CODEC_ERR_UNSUPPORTED;
    }
    size_t tileBytes = (size_t)(unsigned int)w * (size_t)(unsigned int)h *
                       (size_t)(unsigned int)db;
    if (tileBytes > (size_t)INT_MAX) {
        return -XGUI_REMOTE_CODEC_ERR_BAD_PARAM; /* 契约返回 int, 超界 tile 拒绝。 */
    }

    /* 归一化"线上源": 需要时转换格式/收敛行填充到连续缓冲。 */
    XrcScratch sc;
    bool       haveScratch = false;
    const uint8_t* wire    = srcPixels;
    int            wireStride = srcStrideBytes;
    if (srcFormat != dstFormat) {
        uint8_t* tmp = xrc_scratch_acquire(&sc, tileBytes);
        if (tmp == NULL) {
            return -XGUI_REMOTE_CODEC_ERR_BAD_PARAM; /* 临时缓冲不足(堆耗尽)。 */
        }
        haveScratch = true;
        for (int y = 0; y < h; ++y) {
            XGuiRemoteCodec_convertPixels(srcPixels + (size_t)y * (size_t)(unsigned int)srcStrideBytes,
                                          srcFormat,
                                          tmp + (size_t)y * (size_t)(unsigned int)w * (size_t)(unsigned int)db,
                                          dstFormat, w);
        }
        wire = tmp;
        wireStride = w * db;
    } else if (codec == XGUI_REMOTE_CODEC_ZLIB && srcStrideBytes != w * sb) {
        uint8_t* tmp = xrc_scratch_acquire(&sc, tileBytes);
        if (tmp == NULL) {
            return -XGUI_REMOTE_CODEC_ERR_BAD_PARAM;
        }
        haveScratch = true;
        for (int y = 0; y < h; ++y) { /* zlib 需连续源: 去行填充。 */
            memcpy(tmp + (size_t)y * (size_t)(unsigned int)w * (size_t)(unsigned int)sb,
                   srcPixels + (size_t)y * (size_t)(unsigned int)srcStrideBytes,
                   (size_t)(unsigned int)w * (size_t)(unsigned int)sb);
        }
        wire = tmp;
        wireStride = w * sb;
    }

    int rc;
    switch (codec) {
    case XGUI_REMOTE_CODEC_RAW:
        rc = xrc_raw_encode(wire, wireStride, w, h, db, dst, dstCap);
        break;
    case XGUI_REMOTE_CODEC_RLE:
        rc = xrc_rle_encode(wire, wireStride, w, h, db, dst, dstCap);
        break;
    case XGUI_REMOTE_CODEC_ZLIB: {
#if XGUI_REMOTE_ZLIB_ON
        if (zlibLevel < 1) { zlibLevel = 1; } /* 契约 1..9, 防御性夹取。 */
        if (zlibLevel > 9) { zlibLevel = 9; }
        uLongf destLen = (uLongf)dstCap;
        int zrc = compress2(dst, &destLen, wire, (uLong)tileBytes, zlibLevel);
        if (zrc == Z_OK) {
            rc = (int)destLen;
        } else if (zrc == Z_BUF_ERROR) {
            rc = -XGUI_REMOTE_CODEC_ERR_DST_TOO_SMALL;
        } else {
            rc = -XGUI_REMOTE_CODEC_ERR_BAD_PARAM; /* Z_MEM_ERROR 等资源类失败。 */
        }
#else
        (void)zlibLevel;
        rc = -XGUI_REMOTE_CODEC_ERR_UNSUPPORTED; /* 本产物未编译 zlib。 */
#endif
        break;
    }
    default:
        rc = -XGUI_REMOTE_CODEC_ERR_UNSUPPORTED;
        break;
    }

    if (haveScratch) {
        xrc_scratch_release(&sc);
    }
    return rc;
}

/* ==================== decodeTile ==================== */

int XGuiRemoteCodec_decodeTile(const uint8_t* encoded, size_t encodedBytes,
                               XGuiRemoteCodecId codec,
                               XGuiRemotePixelFormat srcFormat,
                               uint8_t* dstPixels, int dstStrideBytes,
                               XGuiRemotePixelFormat dstFormat,
                               int w, int h)
{
    int sb = xrc_bpp(srcFormat);
    int db = xrc_bpp(dstFormat);
    if (!xrc_params_ok(encoded, dstPixels, w, h, sb, db)) {
        return -XGUI_REMOTE_CODEC_ERR_BAD_PARAM;
    }
    if (dstStrideBytes < w * db) {
        return -XGUI_REMOTE_CODEC_ERR_BAD_PARAM; /* 目标 stride 容不下整行。 */
    }
    if (!XGuiRemoteCodec_hasCodec(codec)) {
        return -XGUI_REMOTE_CODEC_ERR_UNSUPPORTED;
    }

    switch (codec) {
    case XGUI_REMOTE_CODEC_RAW: {
        size_t tileBytes = (size_t)(unsigned int)w * (size_t)(unsigned int)h *
                           (size_t)(unsigned int)sb;
        if (encodedBytes < tileBytes) {
            return -XGUI_REMOTE_CODEC_ERR_CORRUPT; /* 载荷截断。 */
        }
        for (int y = 0; y < h; ++y) {
            XGuiRemoteCodec_convertPixels(encoded + (size_t)y * (size_t)(unsigned int)w * (size_t)(unsigned int)sb,
                                          srcFormat,
                                          dstPixels + (size_t)y * (size_t)(unsigned int)dstStrideBytes,
                                          dstFormat, w);
        }
        return 0;
    }
    case XGUI_REMOTE_CODEC_RLE:
        return xrc_rle_decode(encoded, encodedBytes, srcFormat,
                              dstPixels, dstStrideBytes, dstFormat, w, h, sb, db);
    case XGUI_REMOTE_CODEC_ZLIB: {
#if XGUI_REMOTE_ZLIB_ON
        size_t tileBytes = (size_t)(unsigned int)w * (size_t)(unsigned int)h *
                           (size_t)(unsigned int)sb;
        if (tileBytes > (size_t)ULONG_MAX) {
            return -XGUI_REMOTE_CODEC_ERR_BAD_PARAM; /* 超出 uLong 计数域。 */
        }
        /* 线上格式即目标格式且目标无行填充时, 直接解压进 backbuffer, 免临时。 */
        bool    direct = (srcFormat == dstFormat) &&
                         (dstStrideBytes == w * db);
        XrcScratch sc;
        uint8_t* tmp;
        if (direct) {
            tmp = dstPixels;
        } else {
            tmp = xrc_scratch_acquire(&sc, tileBytes);
            if (tmp == NULL) {
                return -XGUI_REMOTE_CODEC_ERR_BAD_PARAM; /* 临时缓冲不足(堆耗尽)。 */
            }
        }
        uLongf destLen = (uLongf)tileBytes;
        int    rc;
        if (uncompress((Bytef*)tmp, &destLen, encoded, (uLong)encodedBytes) != Z_OK ||
            destLen != (uLongf)tileBytes) {
            rc = -XGUI_REMOTE_CODEC_ERR_CORRUPT; /* 解压失败或还原字节数不符。 */
        } else if (direct) {
            rc = 0;
        } else {
            rc = 0;
            for (int y = 0; y < h; ++y) { /* 临时 -> 带 stride 目标, 按需转换。 */
                XGuiRemoteCodec_convertPixels(tmp + (size_t)y * (size_t)(unsigned int)w * (size_t)(unsigned int)sb,
                                              srcFormat,
                                              dstPixels + (size_t)y * (size_t)(unsigned int)dstStrideBytes,
                                              dstFormat, w);
            }
        }
        if (!direct) {
            xrc_scratch_release(&sc);
        }
        return rc;
#else
        (void)encodedBytes;
        return -XGUI_REMOTE_CODEC_ERR_UNSUPPORTED; /* 本产物未编译 zlib。 */
#endif
    }
    default:
        return -XGUI_REMOTE_CODEC_ERR_UNSUPPORTED;
    }
}

#endif /* XGUI_REMOTE_ON */
