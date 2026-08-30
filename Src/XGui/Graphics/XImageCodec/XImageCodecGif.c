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
    size_t byteOffset;
    size_t bitOffset;
    size_t bytesNeeded;
    if (!reader || !reader->m_data || bits <= 0 || bits > 12) return -1;
    if (reader->m_bit > (size_t)-1 - (size_t)bits) return -1;
    /* 先按字节计算剩余容量，避免 size*8 或 bit+bits 溢出。 */
    byteOffset = reader->m_bit >> 3;
    bitOffset = reader->m_bit & 7u;
    bytesNeeded = (bitOffset + (size_t)bits + 7u) >> 3;
    if (byteOffset > reader->m_size ||
        bytesNeeded > reader->m_size - byteOffset)
        return -1;
    for (int i = 0; i < bits; ++i)
        value |= ((reader->m_data[(reader->m_bit + (size_t)i) >> 3] >>
                    ((reader->m_bit + (size_t)i) & 7u)) & 1u) << i;
    reader->m_bit += (size_t)bits;
    return value;
}

/* Qt QGIFFormat 的图像分配上限：拒绝过大的逻辑屏幕面积。 */
static bool gifWithinSizeLimit(uint16_t width, uint16_t height)
{
    return (uint64_t)width * (uint64_t)height < 16384ull * 16384ull;
}

/* Qt 的 Q_TRANSPARENT 为 0x00ffffff；调色板透明色保留 RGB 分量。 */
static uint32_t gifPaletteColor(const uint8_t* palette, size_t count,
                                uint8_t index, bool transparent)
{
    uint32_t color;
    if (palette && (size_t)index < count)
        color = 0xff000000u |
            ((uint32_t)palette[(size_t)index * 3u] << 16) |
            ((uint32_t)palette[(size_t)index * 3u + 1u] << 8) |
            palette[(size_t)index * 3u + 2u];
    else
        /* QGIFFormat::color()（qgifhandler.cpp:1023-1031）在没有任何
           调色板时从空 map 得到 0；已有调色板但索引越界时才返回
           Q_TRANSPARENT。两种情况必须分开，才能保留 Qt 对无色表 GIF
           的兼容读取行为。 */
        color = count ? 0x00ffffffu : 0u;
    if (transparent) color &= 0x00ffffffu;
    return color;
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
        if (code > next) return false;
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
    size_t required;
    if (!data || !pos || !outSize || *pos > size) return NULL;
    while (*pos < size) {
        uint8_t n = data[(*pos)++];
        bool partialBlock = false;
        if (n == 0) break;
        if ((size_t)n > size - *pos) {
            /* Qt QGIFFormat 在 ImageDataBlockSize/ImageDataBlock 状态
               （qgifhandler.cpp:445-452）会保留已经解出的像素，即使
               输入在当前子块中提前到达 EOF；QGifHandler::read() 随后
               在 qgifhandler.cpp:1097-1120 以 partialNewFrame && atEnd()
               接受这类完整像素流。保留实际剩余字节，让后面的 LZW
               解码器决定像素是否完整；若像素不足，仍会按损坏输入拒绝。
               n <= 255，因此这里的剩余长度可安全转换回 uint8_t。 */
            n = (uint8_t)(size - *pos);
            partialBlock = true;
        }
        /* 子块总长度来自输入数据；先检查 used+n，避免恶意数据在
           size_t 边界回绕后绕过容量判断。 */
        if ((size_t)n > (size_t)-1 - used) {
            if (buf) XFree_System(buf);
            return NULL;
        }
        required = used + (size_t)n;
        if (required > capacity) {
            size_t nextCap = capacity
                ? (capacity > (size_t)-1 / 2u ? required : capacity * 2u)
                : (size_t)256u;
            uint8_t* next;
            while (nextCap < required) {
                /* 容量翻倍接近 SIZE_MAX 时直接采用所需长度，避免
                   nextCap 溢出为零并进入死循环。 */
                if (nextCap > (size_t)-1 / 2u) {
                    nextCap = required;
                    break;
                }
                nextCap *= 2;
            }
            if (nextCap < required) {
                if (buf) XFree_System(buf);
                return NULL;
            }
            next = (uint8_t*)XRealloc_System(buf, nextCap);
            if (!next) { if (buf) XFree_System(buf); return NULL; }
            buf = next;
            capacity = nextCap;
        }
        memcpy(buf + used, data + *pos, n);
        used = required;
        *pos += n;
        if (partialBlock) break;
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
    if (!data || !pos || !palette || !count || *pos > size) return false;
    if (!(packed & 0x80u)) return true;
    paletteCount = (size_t)1u << ((packed & 7u) + 1u);
    if (paletteCount > 256 || *pos > size ||
        paletteCount > (size - *pos) / 3u) return false;
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
    /* Qt QGifHandler 构造函数（qgifhandler.cpp:1037-1044）把
       nextDelay 初始化为 100ms；没有 GCE 的帧也必须继承这个默认值。 */
    int pendingDelayMs = 100;
    int pendingDisposal = 0;
    bool ok = false;
    bool seen = false;
    bool lastFrameComplete = false;
    bool cleanEofAfterFrame = false;

    if (!data || size < 13 || (!singleOut && (!frames || maxFrames <= 0)) ||
        (memcmp(data, "GIF87a", 6) && memcmp(data, "GIF89a", 6)))
        return false;
    screenW = XImageCodecInternal_readU16LE(data + 6);
    screenH = XImageCodecInternal_readU16LE(data + 8);
    packed = data[10];
    backgroundIndex = data[11];
    if (!screenW || !screenH) return false;
    if (!gifWithinSizeLimit(screenW, screenH)) return false;
    if (!gifReadPalette(data, size, &pos, packed, globalPalette,
                        &globalCount))
        return false;
    if (globalCount)
    {
        /* Qt QGIFFormat 将 bgcol 保存为原始索引，并由 color() 在索引
           超过调色板时返回 Q_TRANSPARENT，而不是回退到画布首像素。
           gifPaletteColor() 对合法和越界索引都复现该规则。 */
        backgroundColor = gifPaletteColor(globalPalette, globalCount,
                                           backgroundIndex, false);
        hasBackgroundColor = true;
    }

    /* Qt qgifhandler.cpp:317-330 延迟到首个图像描述符才分配画布，
       并按该帧此前是否出现透明 GCE 选择 RGB32 或 ARGB32。格式是
       对外可观察的 QImage 属性，不能为简化而始终使用 ARGB32；因此
       这里先保留空画布，在进入首个图像描述符时按 pendingTransparency
       完成同样的选择。 */
    XImage_init(&canvas);
    XImage_init(&previous);

    while (pos < size) {
        /* 只允许在刚完成一帧且扫描器确实已耗尽输入时走 EOF
           兼容分支；若完整帧后还遇到未知标记，不能把它伪装成
           Qt 的 partialNewFrame。 */
        cleanEofAfterFrame = false;
        uint8_t marker = data[pos++];
        if (marker == 0x3b) { ok = seen; break; }
        if (marker == 0x21) {
            uint8_t label;
            if (pos >= size) goto done;
            label = data[pos++];
            if (label == 0xf9) {
                /* 图形控制扩展：Qt QGIFFormat 在
                   qgifhandler.cpp:609-627 按 hold[0] 指定的块长度
                   读取字段，并在之后统一走 SkipBlockSize；它不会把
                   合法的扩展块硬编码为长度 4 或强制终止字节为 0。
                   这里至少要求 4 个字段字节，避免短块访问未定义数据，
                   对更长块保留 Qt 使用的前五个字段并跳过其余内容。 */
                size_t blockSize;
                uint8_t flags;
                if (pos >= size)
                    goto done;
                blockSize = data[pos++];
                if (blockSize < 4 || blockSize > size - pos)
                    goto done;
                flags = data[pos];
                pendingTransparency = (flags & 1u) != 0;
                pendingTransparentIndex = data[pos + 3];
                {
                    uint16_t delay = (uint16_t)(data[pos + 1] |
                                                ((uint16_t)data[pos + 2] << 8));
                    /* Qt QGIFFormat 将过短延迟钳制为 10 个百分之一秒。 */
                    pendingDelayMs = (int)(delay < 2 ? 10 : delay) * 10;
                }
                pendingDisposal = ((flags >> 2) & 7u);
                if (pendingDisposal > 3) pendingDisposal = 0;
                pos += blockSize;
                pos = gifSkipSubBlocks(data, size, pos);
                if (pos == (size_t)-1) goto done;
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
            lastFrameComplete = false;
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
            if (!width || !height)
                goto done;
            /* Qt QGIFFormat 在 qgifhandler.cpp:348-349 只把 right/bottom
               截到逻辑画布，并不会因为图像描述超出画布而拒绝整帧；
               out_of_bounds 状态随后只禁止越界像素写入。这里同样接受
               部分或全部位于画布外的帧，保持局部动画的可观察结果一致。
               索引缓冲仍受 Qt 同一 16384*16384 面积上限约束，避免嵌入式
               版本为恶意 16 位宽高分配未受控内存。 */
            if (!gifWithinSizeLimit(width, height))
                goto done;
            if (imagePacked & 0x80u) {
                if (!gifReadPalette(data, size, &pos, imagePacked, palette,
                                    &paletteCount))
                    goto done;
            } else {
                memcpy(palette, globalPalette, globalCount * 3);
                paletteCount = globalCount;
            }
            if (XImage_isNull(&canvas)) {
                XImage_init_ex(&canvas, screenW, screenH,
                               pendingTransparency ? XImageFormat_ARGB32
                                                    : XImageFormat_RGB32);
                if (XImage_isNull(&canvas)) {
                    XFree_System(compressed);
                    goto done;
                }
                /* Qt 先 memset(bits(), 0, sizeInBytes())；RGB32 的
                   pixel() 会强制 Alpha=0xff，故 XImage_fill 的黑色
                   结果与 Qt 的全零存储完全一致。 */
                XImage_fill(&canvas, 0x00000000u);
            }
            if (pos >= size) goto done;
            minCode = data[pos++];
            if (minCode < 2 || minCode > 8) goto done;
            compressed = gifCollectSubBlocks(data, size, &pos, &compressedSize);
            if (!compressed) goto done;

            /* 首帧为局部图像时，Qt 先按逻辑屏幕背景初始化剩余画布。 */
            if (!seen && (left || top || width < screenW || height < screenH)) {
                /* qgifhandler.cpp:365-374 仅在存在透明索引或逻辑屏幕
                   背景索引时填充首帧剩余区域；没有全局背景调色板时，Qt
                   保留 qgifhandler.cpp:317-330 清零 RGB32 后得到的不透明
                   黑色，不能把未知背景误写成 ARGB32 的透明零值。 */
                if (pendingTransparency)
                    XImage_fill(&canvas,
                                gifPaletteColor(palette, paletteCount,
                                                pendingTransparentIndex, true));
                else if (hasBackgroundColor)
                    XImage_fill(&canvas, backgroundColor);
            }

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

            if ((size_t)width > (size_t)-1 / (size_t)height)
                goto done;
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
                    if (pendingTransparency && index == pendingTransparentIndex) {
                        /* Qt 首帧把透明索引写成透明调色板色；后续帧保留底图。 */
                        if (seen) continue;
                        XImage_setPixel(&canvas, left + x, top + y,
                                        gifPaletteColor(palette, paletteCount,
                                                        index, true));
                        continue;
                    }
                    XImage_setPixel(&canvas, left + x, top + y,
                                    gifPaletteColor(palette, paletteCount,
                                                    index, false));
                }
            }
            XFree_System(indices);
            XFree_System(compressed);
            seen = true;
            lastFrameComplete = true;
            cleanEofAfterFrame = pos >= size;

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
                    /* Qt QGIFFormat::disposePrevious()（qgifhandler.cpp:164-170）
                       对 RestoreBackground 固定填充 Q_TRANSPARENT，即
                       0x00ffffff；这里不能改用透明索引对应的调色板 RGB。 */
                    disposalColor = 0x00ffffffu;
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
            /* 透明索引和处置方式只影响紧随其后的图像描述。Qt 的
               nextDelay 则由 QGifHandler 持有，只有再次遇到 GCE 才更新；
               因而保留 pendingDelayMs，使无 GCE 帧继续得到 100ms 默认值
               或前一个 GCE 设置的延时。 */
            pendingTransparency = false;
            pendingTransparentIndex = 0;
            pendingDisposal = 0;
            continue;
        }
        goto done;
    }
done:
    /* QGifHandler::read()（qgifhandler.cpp:1097-1120）在完整像素流
       已产生但输入于图像数据块末尾 EOF 时，以
       partialNewFrame && device()->atEnd() 接受当前帧。单帧路径在
       捕获首帧后已经提前结束；动画路径则需要在扫描器到达 EOF 时
       将同一规则映射为成功，不能因缺少子块终止字节或 GIF trailer
       再丢弃已经解码的帧。LZW 解码仍要求完整像素数量，因此真正
       截断的像素流不会被此兼容分支放行。 */
    if (!ok && seen && lastFrameComplete && cleanEofAfterFrame && pos >= size)
        ok = true;
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
