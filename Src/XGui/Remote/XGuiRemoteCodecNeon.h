/**
 * @file       XGuiRemoteCodecNeon.h
 * @brief      XGuiRemote 编解码热循环 NEON 加速内核(内部头, 仅本模块包含)。
 * @details    [perf9 路4 NEON SIMD 2026-10-05] mcgs A33 定谳: 现网 armel 交叉
 *             构建默认 -mfpu=vfp, 整 TU 零向量指令; 加 -mfpu=neon 后 gcc7
 *             自动向量化只命中 convertPixels 两循环, RLE 游标扫描三热点
 *             (px_eq 等值/脏游标扫描/按行字面量冲刷)全部 "control flow in
 *             loop" 拒绝——本头以 intrinsic 显式向量化, 三条红线:
 *               1. 输出逐字节等价: 比较语义与标量 xrc_px_eq 完全一致, 供
 *                  rle_bench NEON 版 0 mismatch 自证(verify/perf9);
 *               2. 未初始化安全: NEON 批只读当前行内 maxUnits*bpp 字节,
 *                  尾块(<一批)/零长度/封顶退出全部回标量单步路径, 绝不
 *                  越过 tile 行尾读未初始化内存;
 *               3. 编译开关: 仅当编译器已定义 __ARM_NEON__ 且未显式
 *                  XRC_NEON_FORCE_OFF 才启用; 桌面 x86/无 NEON 产物自动
 *                  全标量(CMake 只对 arm 交叉目标给 XGuiRemoteCodec.c 加
 *                  -mfpu=neon, ABI 恒 softfp 不变)。
 *             全部函数 static inline 纯函数: 无全局状态、可重入。
 * @author     XinYueC 团队
 */
#ifndef XGUI_REMOTE_CODEC_NEON_H
#define XGUI_REMOTE_CODEC_NEON_H

#include <stdint.h>
#include <string.h>

#if defined(__ARM_NEON__) && !defined(XRC_NEON_FORCE_OFF)
#include <arm_neon.h>
#define XRC_NEON_ON 1
#else
#define XRC_NEON_ON 0
#endif

#if XRC_NEON_ON

/**
 * @brief  横向压缩 8 个 0/1 字节为 u16 掩码(bit k = 字节 k)。
 * @note   vpadd 链 8→4→2→1; 输入恒 0/1, 和 ≤255 无溢出。
 */
static inline uint16_t xrc_neon_pack8(uint8x8_t nz)
{
    /* 逐字节移位权重 {0,1,2,3,4,5,6,7}(小端字节序, gcc7 vector 初始化
     * 支持差, 用 vcreate 常量装载)。 */
    int8x8_t shiftsInit = vreinterpret_s8_u8(vcreate_u8(0x0706050403020100ULL));
    uint8x8_t shifted = vshl_u8(nz, shiftsInit); /* 1<<k, 位级权重。 */
    uint8x8_t a = vpadd_u8(shifted, shifted);
    a = vpadd_u8(a, a);
    a = vpadd_u8(a, a);
    return (uint16_t)vget_lane_u8(a, 0);
}

/**
 * @brief  u16x8 等值掩码(0xFFFF/0x0000)压缩为 8 位(每 lane 1 位)。
 */
static inline unsigned xrc_neon_mask8_u16(uint16x8_t eq)
{
    uint16x8_t one = vshrq_n_u16(eq, 15); /* 0x0001/0x0000。 */
    return (unsigned)xrc_neon_pack8(vmovn_u16(one));
}

/**
 * @brief  u32x4 等值掩码压缩为 4 位(bit k = lane k)。
 */
static inline unsigned xrc_neon_mask4_u32(uint32x4_t eq)
{
    int8x8_t shiftsInit = vreinterpret_s8_u8(vcreate_u8(0x0000000003020100ULL)); /* {0..3,0..} */
    uint32x4_t one = vshrq_n_u32(eq, 31); /* 0x00000001/0。 */
    uint8x8_t nz = vmovn_u16(vcombine_u16(vmovn_u32(one),
                                          vdup_n_u16(0)));
    uint8x8_t shifted = vshl_u8(nz, shiftsInit);
    uint8x8_t a = vpadd_u8(shifted, shifted);
    a = vpadd_u8(a, a);
    return (unsigned)vget_lane_u8(a, 0) & 0xFu;
}

/**
 * @brief  NEON 批量等值游标: 自 scan 起逐单元与 u 比较, 返回前导等值
 *         单元数(≤ maxUnits)。bpp 仅支持 2/4; 调用方必须保证
 *         scan[0 .. maxUnits*bpp-1] 全部在当前 tile 行内(本函数按
 *         整批 8/4 单元读 16B, 调用方以 rowLeft 下夹保证不越界)。
 * @note   等值语义与标量 xrc_px_eq 逐字节一致: 单元所有字节相等才计数。
 *         尾块(maxUnits < 一批)回标量展开比较——零长度(不可达, 防御
 *         直接返回 0)与任意奇地址均安全(vld1q 无对齐要求)。
 */
static inline size_t xrc_neon_run_count(const uint8_t* scan,
                                        const uint8_t* u,
                                        int bpp, size_t maxUnits)
{
    size_t done = 0;
    if (maxUnits == 0) {
        return 0; /* 零长度: 红线回退口(防御)。 */
    }
    if (bpp == 2) {
        uint16_t uv;
        memcpy(&uv, u, 2u); /* 单元广播(奇地址安全, 免对齐/别名)。 */
        while (maxUnits - done >= 8u) {
            uint16x8_t x = vreinterpretq_u16_u8(vld1q_u8(scan));
            uint16x8_t eq = vceqq_u16(x, vdupq_n_u16(uv));
            unsigned m = xrc_neon_mask8_u16(eq);
            if (m != 0xFFu) {
                return done + (size_t)__builtin_ctz(~m & 0xFFu);
            }
            done += 8u;
            scan += 16;
        }
        while (done < maxUnits) { /* 尾块标量(<8 单元)。 */
            uint16_t xv;
            memcpy(&xv, scan, 2u);
            if (xv != uv) {
                break;
            }
            ++done;
            scan += 2;
        }
        return done;
    }
    if (bpp == 4) {
        uint32_t uv;
        memcpy(&uv, u, 4u);
        while (maxUnits - done >= 4u) {
            uint32x4_t x = vreinterpretq_u32_u8(vld1q_u8(scan));
            uint32x4_t eq = vceqq_u32(x, vdupq_n_u32(uv));
            unsigned m = xrc_neon_mask4_u32(eq);
            if (m != 0xFu) {
                return done + (size_t)__builtin_ctz(~m & 0xFu);
            }
            done += 4u;
            scan += 16;
        }
        while (done < maxUnits) { /* 尾块标量(<4 单元)。 */
            uint32_t xv;
            memcpy(&xv, scan, 4u);
            if (xv != uv) {
                break;
            }
            ++done;
            scan += 4;
        }
        return done;
    }
    return 0; /* 其他 bpp: 调用方走标量 memcmp 兜底路径。 */
}

#endif /* XRC_NEON_ON */

#endif /* XGUI_REMOTE_CODEC_NEON_H */
