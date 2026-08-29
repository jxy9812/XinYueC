/*****************************************************************************/
/**
 * @file       XImageCodecGif.c
 * @brief      XImageCodec GIF87a/GIF89a 格式独立实现。
 * @note       受 XIMAGECODEC_GIF_ON 开关控制，可通过 XImageCodec_config.h
 *              单独裁剪。
 *              基础解码：全局/局部调色板、透明色、交错图、首帧绘制；多帧
 *              动画（GCE 延迟/透明色/处置方式、Netscape 循环计数）受
 *              XIMAGECODEC_GIF_ANIM_ON 开关控制；编码输出 8 位全局调色板
 *              单帧 GIF89a。
 *              只通过 XImageCodecInternal_decodeGif / encodeGif /
 *              XImageCodecInternal_decodeGifFrames 对外暴露，统一由
 *              XImageCodec.c 的 XImageCodec_decode / encode / decodeAnimation
 *              分发。
 */
#include "XImageCodec_config.h"
#include "XImageCodecInternal.h"
#include "XMemory.h"
#include <stdint.h>
#include <string.h>

#if XIMAGECODEC_ON
#if XIMAGECODEC_GIF_ON

/* 动画帧数量上限（嵌入式内存防护；正常动画远小于该值）。 */
#define XIMAGECODEC_GIF_ANIM_MAX_FRAMES 1024

/* GIF LZW 位读取器。 */
typedef struct GifBitReader
{
    const uint8_t* m_data;
    size_t m_size;
    size_t m_bit;
} GifBitReader;

static int gifReadCode(GifBitReader* reader, int bits)
{
    int value = 0;
    if (!reader || bits <= 0 || bits > 12 ||
        reader->m_bit + (size_t)bits > reader->m_size * 8u)
        return -1;
    for (int i = 0; i < bits; ++i)
        value |= ((reader->m_data[(reader->m_bit + (size_t)i) >> 3] >>
                    ((reader->m_bit + (size_t)i) & 7u)) & 1u) << i;
    reader->m_bit += (size_t)bits;
    return value;
}

/* GIF LZW 解压到调色板索引数组（自然行序）。 */
static bool gifDecodeLzw(const uint8_t* data, size_t size, int minCodeSize,
                         size_t pixelCount, uint8_t* pixels)
{
    uint16_t prefix[4096];
    uint8_t suffix[4096], stack[4096];
    GifBitReader reader = {data, size, 0};
    int clear, end, next, codeSize, old = -1, first = 0;
    size_t output = 0;
    if (!data || !pixels || minCodeSize < 2 || minCodeSize > 8) return false;
    clear = 1 << minCodeSize;
    end = clear + 1;
    next = clear + 2;
    codeSize = minCodeSize + 1;
    while (output < pixelCount) {
        int code = gifReadCode(&reader, codeSize);
        int inCode, top = 0;
        if (code < 0) return false;
        if (code == clear) {
            next = clear + 2; codeSize = minCodeSize + 1;
            old = -1; continue;
        }
        if (code == end) break;
        if (code >= next && old < 0) return false;
        inCode = code;
        if (code == next) {
            if (old < 0 || top >= 4096) return false;
            stack[top++] = (uint8_t)first;
            code = old;
        }
        while (code >= clear) {
            if (code >= 4096 || top >= 4096) return false;
            stack[top++] = suffix[code];
            code = prefix[code];
        }
        if (code < 0 || code >= clear || top >= 4096) return false;
        first = code;
        stack[top++] = (uint8_t)code;
        while (top && output < pixelCount) pixels[output++] = stack[--top];
        if (old >= 0 && next < 4096) {
            prefix[next] = (uint16_t)old;
            suffix[next] = (uint8_t)first;
            ++next;
            if (next > (1 << codeSize) - 1 && codeSize < 12) ++codeSize;
        }
        old = inCode;
    }
    return output == pixelCount;
}

/* 跳过连续子块（遇到 0 终止）；出错返回 SIZE_MAX。 */
static size_t gifSkipSubBlocks(const uint8_t* data, size_t size, size_t pos)
{
    while (pos < size) {
        uint8_t n = data[pos++];
        if (n == 0) return pos;
        if (n > size - pos) return (size_t)-1;
        pos += n;
    }
    return (size_t)-1;
}

/* 收集连续子块数据为连续缓冲区；出错返回 NULL。 */
static uint8_t* gifCollectSubBlocks(const uint8_t* data, size_t size,
                                    size_t* pos, size_t* outSize)
{
    uint8_t* buf = NULL;
    size_t capacity = 0, used = 0;
    while (*pos < size) {
        uint8_t n = data[(*pos)++];
        if (n == 0) break;
        if (n > size - *pos) { if (buf) XFree_System(buf); return NULL; }
        if (used + n > capacity) {
            size_t nextCap = capacity ? capacity * 2 : 256;
            uint8_t* next;
            while (nextCap < used + n) nextCap *= 2;
            next = (uint8_t*)XRealloc_System(buf, nextCap);
            if (!next) { if (buf) XFree_System(buf); return NULL; }
            buf = next;
            capacity = nextCap;
        }
        memcpy(buf + used, data + *pos, n);
        used += n;
        *pos += n;
    }
    *outSize = used;
    return buf;
}

/* 读取全局/局部调色板；packed 无调色板标志时返回 true 且 count 不变。 */
static bool gifReadPalette(const uint8_t* data, size_t size, size_t* pos,
                           uint8_t packed, uint8_t palette[768],
                           size_t* count)
{
    size_t paletteCount;
    if (!(packed & 0x80u)) return true;
    paletteCount = (size_t)1u << ((packed & 7u) + 1u);
    if (paletteCount > 256 || *pos + paletteCount * 3 > size) return false;
    memcpy(palette, data + *pos, paletteCount * 3);
    *pos += paletteCount * 3;
    *count = paletteCount;
    return true;
}

/* LZW 索引展开：支持交错图（GIF 四趟行序还原为自然行序）。 */
static bool gifExpandIndices(const uint8_t* compressed, size_t compressedSize,
                             int minCode, int width, int height,
                             uint8_t* indices, bool interlace)
{
    size_t pixelCount = (size_t)width * height;
    if (!interlace)
        return gifDecodeLzw(compressed, compressedSize, minCode,
                            pixelCount, indices);
    {
        uint8_t* ordered = (uint8_t*)XMalloc_System(pixelCount);
        size_t p = 0;
        if (!ordered) return false;
        if (!gifDecodeLzw(compressed, compressedSize, minCode, pixelCount,
                          ordered)) {
            XFree_System(ordered);
            return false;
        }
        for (int pass = 0; pass < 4; ++pass) {
            int start = pass == 0 ? 0 : pass == 1 ? 4 : pass == 2 ? 2 : 1;
            int step = pass >= 2 ? (pass == 2 ? 4 : 2) : 8;
            for (int y = start; y < height; y += step) {
                memcpy(indices + (size_t)y * width, ordered + p,
                       (size_t)width);
                p += (size_t)width;
            }
        }
        XFree_System(ordered);
        return p == pixelCount;
    }
}

/* ====================================================================== */
/* 核心扫描器：单帧输出(singleOut != NULL) 或 多帧合成(frames) 两种模式。    */
/* ====================================================================== */
typedef struct XImageCodecFrame XImageCodecFrame;

/**
 * @brief 核心多帧/单帧解码。
 * @param data       输入 GIF 数据。
 * @param size       数据字节数。
 * @param singleOut  单帧输出模式：非 NULL 时只解码首帧并停止。
 * @param frames     多帧输出缓冲区（singleOut 为 NULL 时使用）。
 * @param maxFrames  缓冲区可容纳的最大帧数。
 * @param outCount   输出实际帧数（失败时也输出已捕获帧数）。
 * @param outLoop    输出循环次数（0=播放一次，-1=无限）。
 */
static bool gifDecodeCore(const uint8_t* data, size_t size, XImage* singleOut,
                          XImageCodecFrame* frames, int maxFrames,
                          int* outCount, int* outLoop)
{
    uint16_t screenW, screenH;
    uint8_t packed;
    uint8_t backgroundIndex;
    size_t pos = 13;
    uint8_t globalPalette[768];
    size_t globalCount = 0;
    uint32_t backgroundColor = 0x00000000u;
    bool hasBackgroundColor = false;
    XImage canvas;
    XImage previous;
    int count = 0;
    /* Qt QGIFFormat 使用 -1 表示未出现 Netscape 扩展；对外映射为 0（播放一次）。 */
    int loop = -1;
    bool pendingTransparency = false;
    uint8_t pendingTransparentIndex = 0;
    int pendingDelayMs = 0;
    int pendingDisposal = 0;
    bool ok = false;
    bool seen = false;

    if (!data || size < 13 || (!singleOut && (!frames || maxFrames <= 0)) ||
        (memcmp(data, "GIF87a", 6) && memcmp(data, "GIF89a", 6)))
        return false;
    screenW = XImageCodecInternal_readU16LE(data + 6);
    screenH = XImageCodecInternal_readU16LE(data + 8);
    packed = data[10];
    backgroundIndex = data[11];
    if (!screenW || !screenH) return false;
    if (!gifReadPalette(data, size, &pos, packed, globalPalette,
                        &globalCount))
        return false;
    if (globalCount && backgroundIndex < globalCount)
    {
        backgroundColor = 0xff000000u |
            ((uint32_t)globalPalette[(size_t)backgroundIndex * 3] << 16) |
            ((uint32_t)globalPalette[(size_t)backgroundIndex * 3 + 1] << 8) |
            globalPalette[(size_t)backgroundIndex * 3 + 2];
        hasBackgroundColor = true;
    }

    XImage_init_ex(&canvas, screenW, screenH, XImageFormat_ARGB32);
    if (XImage_isNull(&canvas)) return false;
    XImage_fill(&canvas, 0x00000000u);
    XImage_init(&previous);

    while (pos < size) {
        uint8_t marker = data[pos++];
        if (marker == 0x3b) { ok = seen; break; }
        if (marker == 0x21) {
            uint8_t label;
            if (pos >= size) goto done;
            label = data[pos++];
            if (label == 0xf9) {
                /* 图形控制扩展：尺寸(1) flags(1) 延迟(2) 透明索引(1) 终止(1) */
                uint8_t flags;
                if (pos + 6 > size || data[pos] != 4 || data[pos + 5] != 0)
                    goto done;
                flags = data[pos + 1];
                pendingTransparency = (flags & 1u) != 0;
                pendingTransparentIndex = data[pos + 4];
                {
                    uint16_t delay = (uint16_t)(data[pos + 2] |
                                                ((uint16_t)data[pos + 3] << 8));
                    /* Qt QGIFFormat 将过短延迟钳制为 10 个百分之一秒。 */
                    pendingDelayMs = (int)(delay < 2 ? 10 : delay) * 10;
                }
                pendingDisposal = ((flags >> 2) & 7u);
                if (pendingDisposal > 3) pendingDisposal = 0;
                pos += 6;
                continue;
            }
            if (label == 0xff) {
                /* 应用扩展：Netscape 循环计数子块 03 01 lo hi */
                size_t len;
                if (pos >= size) goto done;
                len = data[pos++];
                if (len > size - pos) goto done;
                if (len >= 8 && !memcmp(data + pos, "NETSCAPE", 8)) {
                    pos += len;
                    if (pos + 4 <= size && data[pos] == 3 &&
                        data[pos + 1] == 1) {
                        loop = data[pos + 2] | ((int)data[pos + 3] << 8);
                        pos += 4;
                    }
                    pos = gifSkipSubBlocks(data, size, pos);
                } else {
                    pos += len;
                    pos = gifSkipSubBlocks(data, size, pos);
                }
                if (pos == (size_t)-1) goto done;
                continue;
            }
            /* 注释/纯文本/未知扩展：跳过 */
            pos = gifSkipSubBlocks(data, size, pos);
            if (pos == (size_t)-1) goto done;
            continue;
        }
        if (marker == 0x2c) {
            uint16_t left, top, width, height;
            uint8_t imagePacked;
            uint8_t palette[768];
            size_t paletteCount = 0;
            int minCode;
            size_t compressedSize = 0;
            uint8_t* compressed = NULL;
            uint8_t* indices = NULL;
            bool interlace;
            XRect rect;

            if (pos + 9 > size) goto done;
            left = XImageCodecInternal_readU16LE(data + pos);
            top = XImageCodecInternal_readU16LE(data + pos + 2);
            width = XImageCodecInternal_readU16LE(data + pos + 4);
            height = XImageCodecInternal_readU16LE(data + pos + 6);
            imagePacked = data[pos + 8];
            interlace = (imagePacked & 0x40u) != 0;
            pos += 9;
            if (!width || !height ||
                (uint32_t)left + width > screenW ||
                (uint32_t)top + height > screenH)
                goto done;
            if (imagePacked & 0x80u) {
                if (!gifReadPalette(data, size, &pos, imagePacked, palette,
                                    &paletteCount))
                    goto done;
            } else {
                if (!globalCount) goto done;
                memcpy(palette, globalPalette, globalCount * 3);
                paletteCount = globalCount;
            }
            if (pos >= size) goto done;
            minCode = data[pos++];
            if (minCode < 2 || minCode > 8) goto done;
            compressed = gifCollectSubBlocks(data, size, &pos, &compressedSize);
            if (!compressed) goto done;

            /* 首帧为局部图像时，Qt 先按逻辑屏幕背景初始化剩余画布。 */
            if (!seen && (left || top || width < screenW || height < screenH))
                XImage_fill(&canvas, pendingTransparency ? 0x00000000u :
                            backgroundColor);

            /* 记录当前帧绘制前的画布快照（RestorePrevious 使用）。 */
            {
                XImage snapshot;
                XImage_init(&snapshot);
                XImage_copy_base(&snapshot, &canvas);
                if (XImage_isNull(&snapshot)) {
                    XImage_deinit_base(&snapshot);
                    XFree_System(compressed);
                    goto done;
                }
                XImage_deinit_base(&previous);
                previous.m_data = snapshot.m_data;
                snapshot.m_data = NULL;
                XImage_deinit_base(&snapshot);
            }

            indices = (uint8_t*)XMalloc_System((size_t)width * height);
            if (!indices ||
                !gifExpandIndices(compressed, compressedSize, minCode,
                                  width, height, indices, interlace)) {
                if (indices) XFree_System(indices);
                XFree_System(compressed);
                goto done;
            }
            /* 按调色板与透明色合成到画布。 */
            for (uint16_t y = 0; y < height; ++y) {
                for (uint16_t x = 0; x < width; ++x) {
                    uint8_t index = indices[(size_t)y * width + x];
                    if (pendingTransparency && index == pendingTransparentIndex)
                        continue;
                    if (index >= paletteCount) {
                        XFree_System(indices);
                        XFree_System(compressed);
                        goto done;
                    }
                    XImage_setPixel(&canvas, left + x, top + y,
                                    0xff000000u |
                                    ((uint32_t)palette[index * 3] << 16) |
                                    ((uint32_t)palette[index * 3 + 1] << 8) |
                                    palette[index * 3 + 2]);
                }
            }
            XFree_System(indices);
            XFree_System(compressed);
            seen = true;

            if (singleOut) {
                /* 单帧模式：首帧已合成，捕获当前画布后结束。 */
                XImage snapshot;
                XImage_init(&snapshot);
                XImage_copy_base(&snapshot, &canvas);
                if (XImage_isNull(&snapshot)) {
                    XImage_deinit_base(&snapshot);
                    goto done;
                }
                XImage_deinit_base(singleOut);
                singleOut->m_data = snapshot.m_data;
                snapshot.m_data = NULL;
                XImage_deinit_base(&snapshot);
                ok = true;
                break;
            }
#if XIMAGECODEC_GIF_ANIM_ON
            /* 多帧模式：捕获当前画布帧。 */
            if (count < maxFrames) {
                XImageCodecFrame* frame = &frames[count];
                XImage_init(&frame->image);
                XImage_copy_base(&frame->image, &canvas);
                if (XImage_isNull(&frame->image)) {
                    XImage_deinit_base(&frame->image);
                    goto done;
                }
                frame->delayMs = pendingDelayMs;
                frame->disposal = (XImageCodecFrameDisposal)pendingDisposal;
                frame->left = left;
                frame->top = top;
                frame->width = width;
                frame->height = height;
                ++count;
            }
#endif /* XIMAGECODEC_GIF_ANIM_ON */
            /* 应用当前帧处置方式。 */
            rect.x = left; rect.y = top;
            rect.width = width; rect.height = height;
            if (pendingDisposal == 2) {
                /* GIF 处置 2 恢复逻辑屏幕背景；没有全局背景色时才使用透明色。 */
                uint32_t disposalColor = backgroundColor;
                if (pendingTransparency)
                    disposalColor = 0x00000000u;
                else if (!hasBackgroundColor)
                    disposalColor = XImage_pixel(&canvas, 0, 0);
                XImage_fillRect(&canvas, &rect, disposalColor);
            } else if (pendingDisposal == 3) {
                XImage snapshot;
                XImage_init(&snapshot);
                XImage_copy_base(&snapshot, &previous);
                if (XImage_isNull(&snapshot)) {
                    XImage_deinit_base(&snapshot);
                    goto done;
                }
                XImage_deinit_base(&canvas);
                canvas.m_data = snapshot.m_data;
                snapshot.m_data = NULL;
                XImage_deinit_base(&snapshot);
            }
            /* GCE 只作用于紧随其后的一个图像描述，不能泄漏到下一帧。 */
            pendingTransparency = false;
            pendingTransparentIndex = 0;
            pendingDelayMs = 0;
            pendingDisposal = 0;
            continue;
        }
        goto done;
    }
done:
    if (outCount) *outCount = count;
    if (outLoop)
        *outLoop = loop == 0 ? -1 : (loop < 0 ? 0 : loop);
    XImage_deinit_base(&canvas);
    XImage_deinit_base(&previous);
    return ok;
}

bool XImageCodecInternal_decodeGif(const uint8_t* data, size_t size,
                                   XImage* out)
{
    /* 单帧解码：由核心扫描器完成首帧提取（无动画帧数组，裁剪动画内存）。 */
    return gifDecodeCore(data, size, out, NULL, 0, NULL, NULL);
}

#if XIMAGECODEC_GIF_ANIM_ON
bool XImageCodecInternal_decodeGifFrames(const uint8_t* data, size_t size,
                                         XImageCodecAnimation* animation)
{
    XImageCodecFrame* frames;
    int count = 0, loop = 0;
    bool ok;
    if (!animation) return false;
    memset(animation, 0, sizeof(*animation));
    frames = (XImageCodecFrame*)XMalloc_System(
        (size_t)XIMAGECODEC_GIF_ANIM_MAX_FRAMES * sizeof(XImageCodecFrame));
    if (!frames) return false;
    memset(frames, 0, (size_t)XIMAGECODEC_GIF_ANIM_MAX_FRAMES *
                       sizeof(XImageCodecFrame));
    ok = gifDecodeCore(data, size, NULL, frames,
                       XIMAGECODEC_GIF_ANIM_MAX_FRAMES, &count, &loop);
    if (!ok) {
        for (int i = 0; i < count; ++i)
            XImage_deinit_base(&frames[i].image);
        XFree_System(frames);
        return false;
    }
    animation->frameCount = count;
    animation->loopCount = loop;
    animation->frames = frames;
    return true;
}
#endif /* XIMAGECODEC_GIF_ANIM_ON */

/* 向 LZW 位流追加一个 9 位编码（固定码宽，GIF 解码兼容）。 */
static bool gifAppendCode(XByteArray* bytes, uint32_t* bitBuffer,
                          int* bitCount, int code)
{
    *bitBuffer |= (uint32_t)code << *bitCount;
    *bitCount += 9;
    while (*bitCount >= 8) {
        if (!XByteArray_push_back_1(bytes, (uint8_t)*bitBuffer)) return false;
        *bitBuffer >>= 8;
        *bitCount -= 8;
    }
    return true;
}

bool XImageCodecInternal_encodeGif(const XImage* image, XByteArray* out)
{
    int width, height;
    uint8_t palette[768];
    XByteArray* stream = NULL;
    XByteArray* block = NULL;
    uint32_t bitBuffer = 0;
    int bitCount = 0;
    if (!image || !out || XImage_isNull(image)) return false;
    width = XImage_width(image); height = XImage_height(image);
    if (width <= 0 || height <= 0 || width > 65535 || height > 65535)
        return false;
    /* 3-3-2 量化全局调色板（256 色）。 */
    for (int i = 0; i < 256; ++i) {
        palette[i * 3] = (uint8_t)(((i >> 5) & 7) * 255 / 7);
        palette[i * 3 + 1] = (uint8_t)(((i >> 2) & 7) * 255 / 7);
        palette[i * 3 + 2] = (uint8_t)((i & 3) * 255 / 3);
    }
    stream = XByteArray_create();
    block = XByteArray_create();
    if (!stream || !block) goto gif_encode_done;
    if (!gifAppendCode(stream, &bitBuffer, &bitCount, 256))
        goto gif_encode_done;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint32_t c = XImage_pixel(image, x, y);
            int r = (int)((c >> 16) & 255),
                g = (int)((c >> 8) & 255),
                b = (int)(c & 255);
            int index = ((r * 7 + 127) / 255 << 5) |
                        ((g * 7 + 127) / 255 << 2) |
                        ((b * 3 + 127) / 255);
            if ((x + y * width) % 200 == 0 &&
                !gifAppendCode(stream, &bitBuffer, &bitCount, 256))
                goto gif_encode_done;
            if (!gifAppendCode(stream, &bitBuffer, &bitCount, index))
                goto gif_encode_done;
        }
    }
    if (!gifAppendCode(stream, &bitBuffer, &bitCount, 257))
        goto gif_encode_done;
    if (bitCount && !XByteArray_push_back_1(stream, (uint8_t)bitBuffer))
        goto gif_encode_done;
    if (!XByteArray_resize_base((XVector*)out, 0) ||
        !XImageCodecInternal_appendBytes(out, "GIF89a", 6))
        goto gif_encode_done;
    {
        uint8_t screen[7];
        XImageCodecInternal_writeU16LE(screen, (uint16_t)width);
        XImageCodecInternal_writeU16LE(screen + 2, (uint16_t)height);
        screen[4] = 0xf7; screen[5] = 0; screen[6] = 0;
        if (!XImageCodecInternal_appendBytes(out, screen, sizeof(screen)) ||
            !XImageCodecInternal_appendBytes(out, palette, sizeof(palette)))
            goto gif_encode_done;
    }
    {
        uint8_t descriptor[9] = {0};
        XImageCodecInternal_writeU16LE(descriptor, 0);
        XImageCodecInternal_writeU16LE(descriptor + 2, 0);
        XImageCodecInternal_writeU16LE(descriptor + 4, (uint16_t)width);
        XImageCodecInternal_writeU16LE(descriptor + 6, (uint16_t)height);
        descriptor[8] = 0;
        if (!XImageCodecInternal_appendBytes(out, ",", 1) ||
            !XImageCodecInternal_appendBytes(out, descriptor,
                                             sizeof(descriptor)) ||
            !XImageCodecInternal_appendBytes(out, "\x08", 1))
            goto gif_encode_done;
    }
    {
        size_t streamSize =
            XByteArray_size_base((const XContainer*)stream);
        size_t offset = 0;
        while (offset < streamSize) {
            size_t count =
                streamSize - offset > 255 ? 255 : streamSize - offset;
            if (!XByteArray_push_back_1(out, (uint8_t)count) ||
                !XImageCodecInternal_appendBytes(out,
                    XByteArray_data(stream) + offset, count))
                goto gif_encode_done;
            offset += count;
        }
        if (!XByteArray_push_back_1(out, 0) ||
            !XByteArray_push_back_1(out, ';'))
            goto gif_encode_done;
    }
    XByteArray_delete_base((XClass*)stream);
    XByteArray_delete_base((XClass*)block);
    return true;
gif_encode_done:
    if (stream) XByteArray_delete_base((XClass*)stream);
    if (block) XByteArray_delete_base((XClass*)block);
    return false;
}


#endif /* XIMAGECODEC_GIF_ON */
#endif /* XIMAGECODEC_ON */
