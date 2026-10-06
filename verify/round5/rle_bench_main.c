/* rle_bench_main.c — round5 RLE 编码根修真基准(2026-10-05 mcgs perf8 战役)。
 * 数据源: mcgs 真机 fb 抓帧(1024x600 RGB565, stride 2048, 铁证基线
 * out/mcgs-campaign/verify 下 .raw 样帧); tile 集按 resource 档 32x32 网格提取,
 * 槽内行紧排(与 XGuiServer 编码线程 workBuf 拷出口径一致, tile stride=64)。
 * 对比: 旧算法(逐单元 div/mod 寻址+逐像素 memcmp, round4 ~69ms/475tile
 * 主热点) vs round5(行游标+xrc_px_eq 展开+按行分块冲刷, 树内现行) vs
 * 纯 memcpy 上限(线上 raw 直拷地板)。附带旧/新逐字节等价自证(全真机
 * tile + 4 组合成模式)。
 * 输出: 每帧一行汇总(tiles/原始字节/压缩比/各实现 ns每tile·MB/s/加速比/
 * 240·475·608 tile 整页投影 ms); 汇总行恒定前缀 [bench] 供落盘归档。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include "rle_impl.h" /* o_rle_encode(旧) + n_rle_encode(round5, 树内现行) */

#define FB_W 1024
#define FB_H 600
#define FB_STRIDE (FB_W * 2)
#define TW 32
#define TH 32
#define BPP 2
#define GW ((FB_W + TW - 1) / TW)
#define GH ((FB_H + TH - 1) / TH)
#define NTILES (GW * GH)
#define TILE_SLOT (TW * TH * BPP)
#define DST_CAP (TILE_SLOT + 256) /* 最坏全字面量=2048+控制字节, 余量充分。 */

static uint8_t* g_tiles;  /* NTILES 槽, 槽内紧排(TILE_SLOT)。 */
static int      g_tch[NTILES]; /* 各 tile 实际高(末行裁剪)。 */
static uint8_t  g_dstA[DST_CAP], g_dstB[DST_CAP], g_dstM[TILE_SLOT];

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/** @brief 从帧缓冲按网格提取 tile(槽内行紧排; 末行/末列裁剪)。 */
static int extract_tiles(const uint8_t* fb)
{
    int gx, gy, n = 0;
    for (gy = 0; gy < GH; ++gy) {
        for (gx = 0; gx < GW; ++gx) {
            int ch = FB_H - gy * TH; if (ch > TH) ch = TH;
            int cw = FB_W - gx * TW; if (cw > TW) cw = TW;
            int y;
            uint8_t* dst = g_tiles + (size_t)n * TILE_SLOT;
            for (y = 0; y < ch; ++y) {
                memcpy(dst + (size_t)y * (size_t)cw * BPP,
                       fb + (size_t)(gy * TH + y) * FB_STRIDE +
                           (size_t)gx * TW * BPP,
                       (size_t)cw * BPP);
            }
            g_tch[n] = ch;
            ++n;
        }
    }
    return n;
}

static volatile uint64_t g_sink;

/** @brief 对全部 tile 跑一遍 enc, 返回累计输出字节(顺带校验)。 */
static uint64_t pass_encode(int n,
                            int (*enc)(const uint8_t*, int, int, int, int,
                                       uint8_t*, size_t))
{
    uint64_t out = 0;
    int t;
    for (t = 0; t < n; ++t) {
        int w = (t % GW) == GW - 1 ? (FB_W - (GW - 1) * TW) : TW;
        int rc = enc(g_tiles + (size_t)t * TILE_SLOT, w * BPP, w, g_tch[t],
                     BPP, g_dstA, DST_CAP);
        if (rc > 0) out += (uint64_t)rc;
        g_sink += (uint64_t)rc;
    }
    return out;
}

static void pass_memcpy(int n)
{
    int t;
    for (t = 0; t < n; ++t) {
        int w = (t % GW) == GW - 1 ? (FB_W - (GW - 1) * TW) : TW;
        memcpy(g_dstM, g_tiles + (size_t)t * TILE_SLOT,
               (size_t)w * (size_t)g_tch[t] * BPP);
        g_sink += g_dstM[0];
    }
}

/** @brief 等价自证: 全 tile 旧/新输出逐字节一致(真机内容)。 */
static int verify_equiv(int n)
{
    int t, bad = 0;
    for (t = 0; t < n; ++t) {
        int w = (t % GW) == GW - 1 ? (FB_W - (GW - 1) * TW) : TW;
        int la = o_rle_encode(g_tiles + (size_t)t * TILE_SLOT, w * BPP, w,
                              g_tch[t], BPP, g_dstA, DST_CAP);
        int lb = n_rle_encode(g_tiles + (size_t)t * TILE_SLOT, w * BPP, w,
                              g_tch[t], BPP, g_dstB, DST_CAP);
        if (la != lb || (la > 0 && memcmp(g_dstA, g_dstB, (size_t)la) != 0))
            ++bad;
    }
    return bad;
}

static uint32_t lcg = 0xC0FFEEu;
static uint32_t nx(void) { lcg = lcg * 1664525u + 1013904223u; return lcg >> 16; }

/** @brief 组合模式等价自证(骨架 run_case 同款 4+1 模式, 32x32/128x128)。 */
static int verify_patterns(void)
{
    static uint8_t buf[128 * 128 * 2];
    static uint8_t a[128 * 128 * 2 + 1024], b[128 * 128 * 2 + 1024];
    static const struct { int w, h; } dims[] = {{32, 32}, {128, 128}, {32, 24}};
    int d, p, bad = 0;
    for (d = 0; d < 3; ++d) {
        int w = dims[d].w, h = dims[d].h;
        size_t units = (size_t)w * h, i;
        for (p = 0; p < 5; ++p) {
            for (i = 0; i < units; ++i) {
                uint32_t v;
                size_t off = i * 2;
                switch (p) {
                case 0: v = 0x12345678; break;
                case 1: v = nx(); break;
                case 2: v = (uint32_t)(i * 7) << 8; break;
                case 3: v = (uint32_t)((i / (size_t)w) * 991) << 8; break;
                default: v = (nx() & 7) ? 0x00AA : nx(); break;
                }
                buf[off] = (uint8_t)(v & 0xFF);
                buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
            }
            {
                int la = o_rle_encode(buf, w * 2, w, h, 2, a, sizeof(a));
                int lb = n_rle_encode(buf, w * 2, w, h, 2, b, sizeof(b));
                if (la != lb || (la > 0 && memcmp(a, b, (size_t)la) != 0))
                    ++bad;
            }
        }
    }
    return bad;
}

int main(int argc, char** argv)
{
    int f, nTotal = 0, mismatch = 0;
    uint64_t rawPage;
    FILE* rpt = stdout;
    if (argc < 2) {
        fprintf(stderr, "usage: %s frame1.raw [frame2.raw ...]\n", argv[0]);
        return 1;
    }
    g_tiles = malloc((size_t)NTILES * TILE_SLOT);
    if (!g_tiles) return 1;
    rawPage = (uint64_t)FB_W * FB_H * BPP;
    fprintf(rpt, "[bench] header platform=desktop-arch-see-uname opt=-O2 "
            "tile=32x32x565 stride=64 grid=%dx%d tiles=%d rawPage=%lluB "
            "reps=200\n", GW, GH, NTILES,
            (unsigned long long)rawPage);
    mismatch += verify_patterns();
    fprintf(rpt, "[bench] equiv-synthetic patterns=15 mismatch=%d\n",
            mismatch);
    for (f = 1; f < argc; ++f) {
        static uint8_t fb[FB_STRIDE * FB_H];
        FILE* in = fopen(argv[f], "rb");
        double t0, t1, t2, t3;
        uint64_t outOld = 0, outNew = 0;
        int n, reps, bad;
        double nsOld, nsNew, nsMem, mbRaw, mbOld, mbMem;
        double eOld, eNew, eMem;
        if (!in) {
            fprintf(rpt, "[bench] frame %s OPEN-FAIL\n", argv[f]);
            continue;
        }
        if (fread(fb, 1, sizeof(fb), in) != sizeof(fb)) {
            fprintf(rpt, "[bench] frame %s SIZE-FAIL want=%zu\n", argv[f],
                    sizeof(fb));
            fclose(in);
            continue;
        }
        fclose(in);
        n = extract_tiles(fb);
        bad = verify_equiv(n);
        mismatch += bad;
        fprintf(rpt, "[bench] equiv %s tiles=%d mismatch=%d\n",
                argv[f], n, bad);
        /* 计时: 预热 1 圈后按实现各自定 reps(每实现 ≥0.2s 稳定读数),
         * best-of-3(取 3 轮最小总时, 抑制共享桌面机的调度噪声)。 */
        (void)pass_encode(n, o_rle_encode);
        (void)pass_encode(n, n_rle_encode);
        pass_memcpy(n);
        reps = 200;
        eOld = eNew = eMem = 1e9;
        for (int q = 0; q < 3; ++q) {
            double e;
            t0 = now_ms();
            for (int r = 0; r < reps; ++r)
                outOld = pass_encode(n, o_rle_encode);
            t1 = now_ms();
            for (int r = 0; r < reps; ++r)
                outNew = pass_encode(n, n_rle_encode);
            t2 = now_ms();
            for (int r = 0; r < reps; ++r) pass_memcpy(n);
            t3 = now_ms();
            e = t1 - t0; if (e < eOld) eOld = e;
            e = t2 - t1; if (e < eNew) eNew = e;
            e = t3 - t2; if (e < eMem) eMem = e;
        }
        nsOld = eOld * 1e6 / ((double)reps * n);
        nsNew = eNew * 1e6 / ((double)reps * n);
        nsMem = eMem * 1e6 / ((double)reps * n);
        mbRaw = (double)reps * n * (double)TILE_SLOT / (eNew / 1000.0) / 1e6;
        mbOld = (double)reps * n * (double)TILE_SLOT / (eOld / 1000.0) / 1e6;
        mbMem = (double)reps * n * (double)TILE_SLOT / (eMem / 1000.0) / 1e6;
        {
            double rawSum = 0;
            int t;
            for (t = 0; t < n; ++t) {
                int w = (t % GW) == GW - 1 ? (FB_W - (GW - 1) * TW) : TW;
                rawSum += (double)w * g_tch[t] * BPP;
            }
            fprintf(rpt,
                    "[bench] frame=%s tiles=%d raw=%.0fB encOld=%.1fB/t "
                    "encNew=%.1fB/t ratio=%.3f | old=%.0fns/t %.1fMB/s | "
                    "new=%.0fns/t %.1fMB/s (%.2fx) | memcpy=%.0fns/t "
                    "%.0fMB/s | "
                    "page240 old=%.2fms new=%.2fms memcpy=%.2fms | "
                    "page475 old=%.2fms new=%.2fms memcpy=%.2fms | "
                    "page%d old=%.2fms new=%.2fms memcpy=%.2fms\n",
                    argv[f], n, rawSum, (double)outOld / n,
                    (double)outNew / n,
                    (outNew > 0) ? (double)outNew / rawSum : 0.0,
                    nsOld, mbOld, nsNew, mbRaw,
                    (nsNew > 0) ? nsOld / nsNew : 0.0, nsMem, mbMem,
                    nsOld * 240 / 1e6, nsNew * 240 / 1e6, nsMem * 240 / 1e6,
                    nsOld * 475 / 1e6, nsNew * 475 / 1e6, nsMem * 475 / 1e6,
                    n, nsOld * n / 1e6, nsNew * n / 1e6, nsMem * n / 1e6);
            nTotal += n;
        }
    }
    fprintf(rpt, "[bench] done frames=%d tiles-total=%d equiv-mismatch=%d "
            "sink=%llu\n", argc - 1, nTotal, mismatch,
            (unsigned long long)g_sink);
    free(g_tiles);
    return mismatch ? 2 : 0;
}
