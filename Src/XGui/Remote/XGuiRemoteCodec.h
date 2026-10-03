/**
 * @file       XGuiRemoteCodec.h
 * @brief      XGuiRemote tile 像素编解码契约(冻结头, 只声明 API 不含实现)。
 * @details    面向 FB_UPDATE tile 载荷的三类纯函数:
 *             - tile 编码/解码: RAW(直拷) / RLE(零依赖 PackBits, 必选基线) /
 *               ZLIB(编译开关 XGUI_REMOTE_ZLIB_ON, 见 XGuiRemoteProto.h);
 *             - 像素格式转换: 线上 ARGB32 ↔ RGB565(快径自实现), 其它组合
 *               委托 XImage_convertToFormat(XImage.h:624);
 *             - tile 内容哈希(FNV-1a 32 位): 编码线程用于跳过内容未变的 tile。
 *             全部函数无全局状态、无锁、可重入, 编码线程可直接调用。
 *             RLE 线上格式(像素对齐 PackBits)冻结于 XGuiRemote.md §5.4:
 *               控制字节 c<0x80: 字面量段, 后随 (c+1) 个像素单元原样字节;
 *               控制字节 c>=0x80: 重复段, 后随 1 个像素单元, 重复 (c-0x80+2) 次。
 * @note       本头依赖 XGuiRemoteProto.h 仅取线上枚举(XGuiRemotePixelFormat/
 *             XGuiRemoteCodecId)——枚举数值单一来源在协议头。
 * @author     XinYueC 团队
 */
#ifndef XGUIREMOTECODEC_H
#define XGUIREMOTECODEC_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiRemoteProto.h"

#if XGUI_REMOTE_ON

/**
 * @brief 编解码结果错误码(encode/decode 负返回值的绝对值)。
 */
typedef enum XGuiRemoteCodecError {
    XGUI_REMOTE_CODEC_ERR_UNSUPPORTED  = 1, /**< 编解码器不可用(未编译/未知 id)。 */
    XGUI_REMOTE_CODEC_ERR_DST_TOO_SMALL = 2,/**< 输出缓冲不足。 */
    XGUI_REMOTE_CODEC_ERR_CORRUPT      = 3, /**< 输入数据截断/越界/非法。 */
    XGUI_REMOTE_CODEC_ERR_BAD_PARAM    = 4  /**< 空指针/非法尺寸/非法格式。 */
} XGuiRemoteCodecError;

/**
 * @brief      查询编译进本产物的编解码器。
 * @param      codec 编解码器 id。
 * @return     RAW/RLE 恒 true; ZLIB 取决于 XGUI_REMOTE_ZLIB_ON。
 */
bool XGuiRemoteCodec_hasCodec(XGuiRemoteCodecId codec);

/**
 * @brief      tile 编码后的最坏字节数(调用方按此分配输出缓冲)。
 * @param      codec         编解码器。
 * @param      w, h          tile 尺寸(像素)。
 * @param      bytesPerPixel 线上格式字节数(ARGB32=4, RGB565=2)。
 * @return     最坏编码字节数; 参数非法返回 0。
 * @note       RAW = w*h*bytesPerPixel; RLE 最坏 = 字面量段每 128 单元多 1
 *             控制字节; ZLIB 对不可压数据可能略膨胀, 上限取 zlib 文档
 *             (源 + 源/1000 + 12)与 RAW 的较大者再加帧余量。
 */
size_t XGuiRemoteCodec_maxEncodedSize(XGuiRemoteCodecId codec,
                                      int w, int h, int bytesPerPixel);

/**
 * @brief      编码一个 tile: 读源像素 → (按需)转线上格式 → 压缩。
 * @details    典型调用方: 服务器编码线程(纯计算, 不触设备)。
 *             源格式与线上格式一致时不发生转换(直读压缩); 不一致时先转。
 * @param      srcPixels  源像素首址(借用; 行主序)。
 * @param      srcStrideBytes 源每行字节数(≥ w*srcBpp; 允许行对齐余量)。
 * @param      srcFormat  源像素格式(XGuiRemotePixelFormat)。
 * @param      w, h       tile 尺寸(>0)。
 * @param      dstFormat  线上目标格式。
 * @param      codec      编解码器。
 * @param      zlibLevel  codec==ZLIB 时 1..9, 其它忽略。
 * @param      dst        输出缓冲(调用方分配, 容量 ≥ maxEncodedSize)。
 * @param      dstCap     输出缓冲容量。
 * @return     ≥0 编码后字节数; <0 为 -XGuiRemoteCodecError。
 */
int XGuiRemoteCodec_encodeTile(const uint8_t* srcPixels, int srcStrideBytes,
                               XGuiRemotePixelFormat srcFormat,
                               int w, int h,
                               XGuiRemotePixelFormat dstFormat,
                               XGuiRemoteCodecId codec, int zlibLevel,
                               uint8_t* dst, size_t dstCap);

/**
 * @brief      解码一个 tile 到目标缓冲: 解压 → (按需)转目标格式 → 写出。
 * @details    典型调用方: 客户端 GUI 线程(解码直写本地 backbuffer)。
 * @param      encoded      编码数据(借用)。
 * @param      encodedBytes 编码数据字节数。
 * @param      codec        编解码器(必须与编码侧一致)。
 * @param      srcFormat    编码内像素格式(线上格式)。
 * @param      dstPixels    目标缓冲首址(行主序; 通常为 backbuffer 行指针)。
 * @param      dstStrideBytes 目标每行字节数。
 * @param      dstFormat    目标像素格式。
 * @param      w, h         tile 尺寸(>0; 须与编码侧一致, 解码器按此边界
 *                          写出, 不越行)。
 * @return     0 成功; <0 为 -XGuiRemoteCodecError(解压失败/数据截断)。
 */
int XGuiRemoteCodec_decodeTile(const uint8_t* encoded, size_t encodedBytes,
                               XGuiRemoteCodecId codec,
                               XGuiRemotePixelFormat srcFormat,
                               uint8_t* dstPixels, int dstStrideBytes,
                               XGuiRemotePixelFormat dstFormat,
                               int w, int h);

/**
 * @brief      像素格式转换(pixelCount 个像素, 连续无 stride)。
 * @details    ARGB32↔RGB565 为快径; 相同格式为直拷; 其它合法组合逐像素
 *             经通用转换。不透明像素(A=255)往返转换位相等(验收断言依据)。
 */
void XGuiRemoteCodec_convertPixels(const uint8_t* src,
                                   XGuiRemotePixelFormat srcFormat,
                                   uint8_t* dst,
                                   XGuiRemotePixelFormat dstFormat,
                                   int pixelCount);

/**
 * @brief      tile 内容哈希(FNV-1a 32 位, 与内容布局相关)。
 * @details    编码线程用于"伤害归并后二次去重": 与上一轮同位置 tile 哈希
 *             相等则跳过编码与发送。空指针/零长度返回偏移初值 2166136261u。
 */
uint32_t XGuiRemoteCodec_tileHash(const uint8_t* pixels, size_t bytes);

#endif /* XGUI_REMOTE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUIREMOTECODEC_H */
