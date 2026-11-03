/* @file       mcgs_fb_cmp.c
 *  @brief      屏上 fbsnap 帧逐像素比对器（昆仑通态 A33 板侧运行）。
 *  @details    连续拖动验证的判据工具：两帧 fbsnap 原始输出（1024x600
 *              RGB565、stride=2048、bpp=16、无文件头）逐像素比较，扣除
 *              指定矩形（浮动面板足迹等「两帧本就该不同」的区域）后，
 *              剩余任何失配像素=陈旧像素（残影）。
 *
 *              用法：mcgs_fb_cmp <a.raw> <b.raw>
 *                               [exX exY exW exH]...（最多 4 组）
 *              输出：CMP total=<N> first=(x,y) a=0x%04x b=0x%04x
 *              退出码：0=无失配；1=有失配；2=用法/IO 错误。
 *
 *              性能：614k 像素线性扫描 + 至多 4 个矩形剔除，A33 上
 *              ~10ms 量级，可逐步调用（拖动验证每步一对帧）。
 *  @author     XinYueC 团队
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FB_W     1024
#define FB_H     600
#define FB_STRIDE 2048
#define FB_BPP    2
#define MAX_EX    4

static unsigned short fbPx(const unsigned char* fb, int x, int y)
{
    size_t off = (size_t)y * FB_STRIDE + (size_t)x * FB_BPP;
    return (unsigned short)(fb[off] | (fb[off + 1] << 8));
}

static unsigned char* fbLoad(const char* path)
{
    FILE* f = fopen(path, "rb");
    unsigned char* buf;
    size_t got;
    if (!f) return NULL;
    buf = (unsigned char*)malloc((size_t)FB_STRIDE * FB_H);
    if (!buf) { fclose(f); return NULL; }
    got = fread(buf, 1, (size_t)FB_STRIDE * FB_H, f);
    fclose(f);
    if (got < (size_t)FB_STRIDE * FB_H) { free(buf); return NULL; }
    return buf;
}

int main(int argc, char** argv)
{
    unsigned char* a;
    unsigned char* b;
    int ex[MAX_EX][4];
    int exN = 0;
    int x;
    int y;
    long total = 0;
    int fx = -1;
    int fy = -1;
    unsigned short fa = 0;
    unsigned short fbv = 0;
    int i;
    if (argc < 3) {
        fprintf(stderr, "usage: %s <a.raw> <b.raw> [exX exY exW exH]... (<=4)\n",
                argv[0]);
        return 2;
    }
    for (i = 3; i + 4 <= argc && exN < MAX_EX; i += 4, ++exN) {
        ex[exN][0] = atoi(argv[i]);
        ex[exN][1] = atoi(argv[i + 1]);
        ex[exN][2] = atoi(argv[i + 2]);
        ex[exN][3] = atoi(argv[i + 3]);
    }
    a = fbLoad(argv[1]);
    b = fbLoad(argv[2]);
    if (!a || !b) {
        fprintf(stderr, "CMP_ERR load %s/%s\n", argv[1], argv[2]);
        free(a);
        free(b);
        return 2;
    }
    for (y = 0; y < FB_H; ++y) {
        for (x = 0; x < FB_W; ++x) {
            int k;
            int skip = 0;
            unsigned short pa;
            unsigned short pb;
            for (k = 0; k < exN; ++k) {
                if (x >= ex[k][0] && x < ex[k][0] + ex[k][2] &&
                    y >= ex[k][1] && y < ex[k][1] + ex[k][3]) {
                    skip = 1;
                    break;
                }
            }
            if (skip) continue;
            pa = fbPx(a, x, y);
            pb = fbPx(b, x, y);
            if (pa != pb) {
                if (total == 0) {
                    fx = x;
                    fy = y;
                    fa = pa;
                    fbv = pb;
                }
                ++total;
            }
        }
    }
    if (fx >= 0)
        printf("CMP total=%ld first=(%d,%d) a=0x%04x b=0x%04x\n",
               total, fx, fy, (unsigned)fa, (unsigned)fbv);
    else
        printf("CMP total=0\n");
    free(a);
    free(b);
    return total == 0 ? 0 : 1;
}
