/******************************************************************************
 * @file       XFont.c
 * @brief      XFont 字体类实现（对标 Qt 6.8 QFont）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XFont.h"
#if XFONT_FILE_ON && (XFONT_LVGL8_FILE_ON || XFONT_FT_FACE_ON)
#include "XFile.h"
#include "XByteArray.h"
#include "XMemory.h"
#endif /* XFONT_FILE_ON && (XFONT_LVGL8_FILE_ON || XFONT_FT_FACE_ON) */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ========== 默认字体家族共享实例（软件渲染帧率根修，2026-09-25） ==========
   背景：XFont_init() 原先为默认家族每次新建一个 XString（两趟 UTF-8 解码 +
   堆分配），而 XPainter 的 init/end 各触发一次 painterDefaultState ——
   XGuiDemo 软件增量基准分段实测：XFont_setFamily 单项就占 5 秒基准总运行
   时间约 15%（726ms/5000ms），加上配套释放合计约占每帧 50µs（帧时 226µs
   的 22%），是当时软件渲染帧率的第一大固定开销。
   方案：默认家族是不可变常量，进程内共享同一 XString 实例；所有权归本
   模块，所有释放点（deinit/copy/move/setFamily）一律跳过共享实例，
   拷贝/移动语义与原先完全一致。修复后该路径归零，单独收益约 +35%
   （默认页 4430→5969 FPS）。
   线程说明：实例为惰性创建 + 仅在 GUI/字体使用线程读写，与本模块其它
   静态缓存（字形缓存等）同一套单写者约定。 */
static XString* g_xfontSharedDefaultFamily = NULL;

static XString* XFont_sharedDefaultFamily(void)
{
    if (!g_xfontSharedDefaultFamily && XFONT_DEFAULT_FAMILY[0] != '\0')
        g_xfontSharedDefaultFamily =
            XString_create_utf8(XFONT_DEFAULT_FAMILY);
    return g_xfontSharedDefaultFamily;
}

static bool XFont_isSharedFamily(const XString* family)
{
    return family != NULL && family == g_xfontSharedDefaultFamily;
}

static int XFont_glyphRowBytes(const XFontGlyphDsc* dsc, int bpp)
{
    if (!dsc || dsc->box_w == 0 || dsc->box_h == 0 ||
        (bpp != 1 && bpp != 2 && bpp != 3 && bpp != 4 && bpp != 8))
        return 0;
    return ((int)dsc->box_w * bpp + 7) / 8;
}

/* LVGL fmt_txt stores a glyph as one continuous MSB-first bit stream. */
static void XFont_writePackedPixel(uint8_t* data, size_t bit, int bpp,
                                   unsigned value)
{
    int i;
    for (i = 0; i < bpp; ++i)
    {
        size_t current = bit + (size_t)i;
        uint8_t mask = (uint8_t)(1u << (7u - (unsigned)(current % 8u)));
        if ((value & (1u << (bpp - 1 - i))) != 0u)
            data[current / 8u] |= mask;
    }
}

#if XFONT_FILE_ON && XFONT_LVGL8_FILE_ON
static bool XFont_hasBinSuffix(const char* name)
{
    size_t length;
    if (!name)
        return false;
    length = strlen(name);
    if (length < 4u)
        return false;
    return (name[length - 4u] == '.') &&
           (name[length - 3u] == 'b' || name[length - 3u] == 'B') &&
           (name[length - 2u] == 'i' || name[length - 2u] == 'I') &&
           (name[length - 1u] == 'n' || name[length - 1u] == 'N');
}

static bool XFont_isPathName(const char* name)
{
    const char* p;
    if (!name)
        return false;
    for (p = name; *p; ++p)
        if (*p == '/' || *p == '\\' || *p == ':')
            return true;
    return false;
}

static bool XFont_externalBinPath(const char* family, char* path,
                                  size_t pathSize)
{
    const char* directory = XFONT_EXTERNAL_FONT_DIR;
    size_t directoryLength;
    int written;
    bool directPath;

    if (!family || !family[0] || !path || pathSize == 0u ||
        !directory)
        return false;
    directPath = XFont_isPathName(family) || XFont_hasBinSuffix(family);
    if (directPath)
    {
        written = snprintf(path, pathSize, "%s%s", family,
                           XFont_hasBinSuffix(family) ? "" : ".bin");
        if (written < 0 || (size_t)written >= pathSize)
        {
            path[0] = '\0';
            return false;
        }
        return true;
    }
    directoryLength = strlen(directory);
    written = snprintf(path, pathSize, "%s%s%s.bin", directory,
                       directoryLength > 0u &&
                           (directory[directoryLength - 1u] == '/' ||
                            directory[directoryLength - 1u] == '\\')
                           ? "" : "/",
                       family);
    if (written < 0 || (size_t)written >= pathSize)
    {
        path[0] = '\0';
        return false;
    }
    return true;
}
#endif /* XFONT_FILE_ON && XFONT_LVGL8_FILE_ON */

#if XFONT_FT_FACE_ON && XFONT_FILE_ON
/* FT 文件后端（XFontFt.c）共用的唯一读文件通道：自带 "../" 前缀剥离回退
   语义；声明在 XFontFace_Protected.h。
   [已移除 2026-10-07] 自研轮廓字（XFO1）外挂装载链——XFont_xfoHeader/
   XFont_loadXfo1Glyph/XFont_outlineFamilyLoad/XFont_outlinePathBuild/
   XFont_outlineFileBlob（含负缓存）与 .inc 十六进制文本解析——按
   "FT 唯一轮廓字实现"裁定整体删除；运行时缺字库的行为 = FT 探测失败
   落位图回退（XFontFace.c），不崩溃。 */
bool XFont_readFileBytes(const char* filePath, XByteArray** outBytes)
{
    XString* path = NULL;
    XFile* file = NULL;
    XByteArray* bytes = NULL;
    if (!filePath || !filePath[0] || !outBytes)
        return false;
    *outBytes = NULL;
    path = XString_create_utf8(filePath);
    file = path ? XFile_create() : NULL;
    if (!path || !file)
        goto failed;
    XFile_setFileName(file, path);
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0))
    {
        if (filePath[0] == '.' && filePath[1] == '.' &&
            (filePath[2] == '/' || filePath[2] == '\\'))
        {
            const char* alternatePath = filePath + 3;
            XClassDelete((XClass*)file);
            XClassDelete((XClass*)path);
            file = NULL;
            path = XString_create_utf8(alternatePath);
            file = path ? XFile_create() : NULL;
            if (!path || !file)
                goto failed;
            XFile_setFileName(file, path);
            if (!XFile_open_2(file, XIODevice_ReadOnly, 0))
                goto failed;
        }
        else
            goto failed;
    }
    bytes = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    if (!bytes)
        goto failed;
    *outBytes = bytes;
    XClassDelete((XClass*)file);
    XClassDelete((XClass*)path);
    return true;
failed:
    if (bytes) XClassDelete((XClass*)bytes);
    if (file) XClassDelete((XClass*)file);
    if (path) XClassDelete((XClass*)path);
    return false;
}
#endif /* XFONT_FT_FACE_ON && XFONT_FILE_ON */

/* ========== 内部静态虚函数实现 ========== */

static void VXFont_deinit(XFont* self)
{
    if (!self) return;
    if (!XFont_isSharedFamily(self->m_family))
        XClassDelete((XClass*)self->m_family);
    self->m_family = NULL;
    XClassDelete((XClass*)self->m_styleName);
    self->m_styleName = NULL;
}

static void VXFont_copy(XFont* dest, const XFont* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(src)) return;
    if (XClassIsVtableNull(dest))
        XFont_init(dest);
    /* copy_base 要求目标已有生命周期。先复制源、再释放旧字符串：
       目标与源可能共享同一 XString（浅值拷贝共享 refcount），若先释放
       旧字符串会令 src 悬空（Task 2.18 排查 XStyleSheetStyle UAF）。 */
    {
        XString* oldFamily = dest->m_family;
        XString* oldStyle = dest->m_styleName;
        /* 默认家族共享实例不可复制（进程内唯一，复制会破坏释放配对）。 */
        if (XFont_isSharedFamily(src->m_family))
            dest->m_family = src->m_family;
        else
            dest->m_family = src->m_family
                                 ? XString_create_copy(src->m_family) : NULL;
        dest->m_styleName = src->m_styleName
                                ? XString_create_copy(src->m_styleName)
                                : NULL;
        if (oldFamily && oldFamily != dest->m_family &&
            !XFont_isSharedFamily(oldFamily))
            XClassDelete((XClass*)oldFamily);
        if (oldStyle && oldStyle != dest->m_styleName)
            XClassDelete((XClass*)oldStyle);
    }
    /* 复制值字段 */
    dest->m_pointSizeF = src->m_pointSizeF;
    dest->m_pixelSize = src->m_pixelSize;
    dest->m_weight = src->m_weight;
    dest->m_style = src->m_style;
    dest->m_stretch = src->m_stretch;
    dest->m_underline = src->m_underline;
    dest->m_strikeOut = src->m_strikeOut;
    dest->m_overline = src->m_overline;
    dest->m_fixedPitch = src->m_fixedPitch;
    dest->m_kerning = src->m_kerning;
    dest->m_capitalization = src->m_capitalization;
    dest->m_letterSpacing = src->m_letterSpacing;
    dest->m_wordSpacing = src->m_wordSpacing;
    dest->m_styleHint = src->m_styleHint;
    dest->m_styleStrategy = src->m_styleStrategy;
    dest->m_hintingPreference = src->m_hintingPreference;
    dest->m_letterSpacingValue = src->m_letterSpacingValue;
    dest->m_wordSpacingValue = src->m_wordSpacingValue;
    dest->m_letterSpacingType = src->m_letterSpacingType;
    dest->m_resolveMask = src->m_resolveMask;
}

static void VXFont_move(XFont* dest, XFont* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(src)) return;
    if (XClassIsVtableNull(dest))
        XFont_init(dest);
    /* 目标先释放原有字符串，再转移源字符串所有权。 */
    if (!XFont_isSharedFamily(dest->m_family))
        XClassDelete((XClass*)dest->m_family);
    XClassDelete((XClass*)dest->m_styleName);
    dest->m_family = src->m_family;
    dest->m_styleName = src->m_styleName;
    src->m_family = NULL;
    src->m_styleName = NULL;
    /* 移动值字段；不能在清空源指针后再调用 copy。 */
    dest->m_pointSizeF = src->m_pointSizeF;
    dest->m_pixelSize = src->m_pixelSize;
    dest->m_weight = src->m_weight;
    dest->m_style = src->m_style;
    dest->m_stretch = src->m_stretch;
    dest->m_underline = src->m_underline;
    dest->m_strikeOut = src->m_strikeOut;
    dest->m_overline = src->m_overline;
    dest->m_fixedPitch = src->m_fixedPitch;
    dest->m_kerning = src->m_kerning;
    dest->m_capitalization = src->m_capitalization;
    dest->m_letterSpacing = src->m_letterSpacing;
    dest->m_wordSpacing = src->m_wordSpacing;
    dest->m_styleHint = src->m_styleHint;
    dest->m_styleStrategy = src->m_styleStrategy;
    dest->m_hintingPreference = src->m_hintingPreference;
    dest->m_letterSpacingValue = src->m_letterSpacingValue;
    dest->m_wordSpacingValue = src->m_wordSpacingValue;
    dest->m_letterSpacingType = src->m_letterSpacingType;
    dest->m_resolveMask = src->m_resolveMask;
    /* 清零源对象 */
    src->m_pointSizeF = 0.0;
    src->m_pixelSize = 0;
    src->m_weight = XFont_Normal;
    src->m_style = XFont_StyleNormal;
    src->m_stretch = XFont_Unstretched;
    src->m_underline = 0;
    src->m_strikeOut = 0;
    src->m_overline = 0;
    src->m_fixedPitch = 0;
    src->m_kerning = 1;
    src->m_capitalization = XFont_MixedCase;
    src->m_letterSpacing = 0;
    src->m_wordSpacing = 0;
    src->m_styleHint = XFont_AnyStyle;
    src->m_styleStrategy = XFont_PreferDefault;
    src->m_hintingPreference = XFont_PreferDefaultHinting;
    src->m_letterSpacingValue = 0.0f;
    src->m_wordSpacingValue = 0.0f;
    src->m_letterSpacingType = XFont_PercentageSpacing;
    src->m_resolveMask = 0;
}

/* ========== 虚函数表初始化 ========== */

XVtable* XFont_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XFont)
    XVTABLE_INHERIT_XCLASS(XClass);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXFont_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXFont_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXFont_deinit);
    return XVTABLE_DEFAULT;
}

/* ========== 创建与初始化 ========== */

XFont* XFont_create_ex(XMemoryType memory, const char* family, int pointSize, int weight, bool italic)
{
    XFont* self = (XFont*)XMemory_malloc(sizeof(XFont), memory);
    if (!self) return NULL;
    XFont_init(self);
    if (family && family[0]) XFont_setFamily(self, family);
    if (pointSize > 0) XFont_setPointSize(self, pointSize);
    if (weight > 0) XFont_setWeight(self, weight);
    if (italic) XFont_setItalic(self, true);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

void XFont_init(XFont* self)
{
    if (!self) return;
    memset(self, 0, sizeof(XFont));
    XClass_init((XClass*)self);
    XClassSetVtable(self, XFont);
    /* 默认值 */
    self->m_weight = XFONT_DEFAULT_WEIGHT;
    self->m_style = XFONT_DEFAULT_ITALIC ? XFont_StyleItalic : XFont_StyleNormal;
    self->m_stretch = XFont_Unstretched;
    self->m_kerning = 1;
    self->m_capitalization = XFont_MixedCase;
    self->m_styleHint = XFont_AnyStyle;
    self->m_styleStrategy = XFont_PreferDefault;
    self->m_hintingPreference = XFont_PreferDefaultHinting;
    self->m_pointSizeF = XFONT_DEFAULT_POINT_SIZE;
    self->m_pixelSize = XFONT_DEFAULT_PIXEL_SIZE;
    self->m_letterSpacingType = XFont_PercentageSpacing;
    if (XFONT_DEFAULT_FAMILY[0] != '\0')
        XFont_setFamily(self, XFONT_DEFAULT_FAMILY);
}

void XFont_init_ex(XFont* self, const char* family, int pointSize, int weight, bool italic)
{
    XFont_init(self);
    if (family && family[0]) XFont_setFamily(self, family);
    if (pointSize > 0) XFont_setPointSize(self, pointSize);
    if (weight > 0) XFont_setWeight(self, weight);
    if (italic) XFont_setItalic(self, true);
}

#if XFONT_FILE_ON && XFONT_LVGL8_FILE_ON
typedef struct XFontFileBitReader
{
    const unsigned char* m_data;
    size_t m_size;
    size_t m_bit;
} XFontFileBitReader;

typedef struct XFontFileRleReader
{
    XFontFileBitReader m_bits;
    unsigned m_bpp;
    unsigned m_prev;
    unsigned m_count;
    unsigned m_state;
    bool m_first;
} XFontFileRleReader;

static uint16_t XFont_fileLe16(const unsigned char* data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static int16_t XFont_fileLe16s(const unsigned char* data)
{
    return (int16_t)XFont_fileLe16(data);
}

static uint32_t XFont_fileLe32(const unsigned char* data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static bool XFont_fileReadBits(XFontFileBitReader* reader, unsigned count,
                               uint32_t* value)
{
    uint32_t result = 0;
    unsigned i;
    if (!reader || !value || count > 32u ||
        reader->m_bit > reader->m_size * 8u ||
        count > reader->m_size * 8u - reader->m_bit)
        return false;
    for (i = 0; i < count; ++i)
    {
        result = (result << 1) |
                 ((reader->m_data[reader->m_bit / 8u] >>
                   (7u - (unsigned)(reader->m_bit % 8u))) & 1u);
        ++reader->m_bit;
    }
    *value = result;
    return true;
}

static bool XFont_fileReadSignedBits(XFontFileBitReader* reader,
                                     unsigned count, int32_t* value)
{
    uint32_t raw;
    if (!value || count == 0u || count > 31u ||
        !XFont_fileReadBits(reader, count, &raw))
        return false;
    if (raw & (1u << (count - 1u)))
        raw |= ~0u << count;
    *value = (int32_t)raw;
    return true;
}

/* This is the RLE bit stream used by LVGL's compressed fmt_txt fonts. */
static bool XFont_fileRleNext(XFontFileRleReader* reader, unsigned* value)
{
    uint32_t raw;
    if (!reader || !value)
        return false;
    if (reader->m_state == 0u)
    {
        if (!XFont_fileReadBits(&reader->m_bits, reader->m_bpp, &raw))
            return false;
        *value = (unsigned)raw;
        if (!reader->m_first && reader->m_prev == *value)
        {
            reader->m_count = 0u;
            reader->m_state = 1u;
        }
        reader->m_first = false;
        reader->m_prev = *value;
        return true;
    }
    if (reader->m_state == 1u)
    {
        if (!XFont_fileReadBits(&reader->m_bits, 1u, &raw))
            return false;
        ++reader->m_count;
        if (raw != 0u)
        {
            *value = reader->m_prev;
            if (reader->m_count == 11u)
            {
                if (!XFont_fileReadBits(&reader->m_bits, 6u, &raw))
                    return false;
                reader->m_count = (unsigned)raw;
                reader->m_state = reader->m_count != 0u ? 2u : 0u;
                if (reader->m_state == 0u)
                {
                    if (!XFont_fileReadBits(&reader->m_bits, reader->m_bpp,
                                             &raw))
                        return false;
                    reader->m_prev = (unsigned)raw;
                    *value = reader->m_prev;
                }
            }
            return true;
        }
        if (!XFont_fileReadBits(&reader->m_bits, reader->m_bpp, &raw))
            return false;
        reader->m_prev = (unsigned)raw;
        reader->m_state = 0u;
        *value = reader->m_prev;
        return true;
    }
    *value = reader->m_prev;
    if (reader->m_count > 0u)
        --reader->m_count;
    if (reader->m_count == 0u)
    {
        if (!XFont_fileReadBits(&reader->m_bits, reader->m_bpp, &raw))
            return false;
        reader->m_prev = (unsigned)raw;
        reader->m_state = 0u;
    }
    return true;
}

static bool XFont_fileSection(const unsigned char* data, size_t size,
                              size_t start, const char label[4],
                              size_t* length)
{
    uint32_t sectionLength;
    if (!data || !label || !length || start > size || size - start < 8u)
        return false;
    sectionLength = XFont_fileLe32(data + start);
    if (sectionLength < 8u || sectionLength > size - start ||
        memcmp(data + start + 4u, label, 4u) != 0)
        return false;
    *length = (size_t)sectionLength;
    return true;
}

static bool XFont_load_lvgl_bin_glyph(const char* filePath, uint32_t cp,
                                      XFontBitmapInfo* info,
                                      XFontGlyphDsc* dsc,
                                      unsigned char* out, size_t outSize)
{
    XString* path = NULL;
    XFile* file = NULL;
    XByteArray* bytes = NULL;
    const unsigned char* data;
    size_t size;
    size_t headLength, cmapLength, locaLength, glyfLength;
    size_t cmapStart, locaStart, glyfStart;
    uint16_t fontSize, ascent;
    int16_t descent;
    unsigned bpp, compression, xyBits, whBits, advanceBits;
    uint32_t cmapCount, locaCount, glyphId = 0u;
    size_t cmapTableStart, i;
    bool found = false;
    uint32_t glyphOffset, nextOffset;
    size_t recordStart, recordEnd;
    XFontFileBitReader reader;
    uint32_t raw;
    int32_t signedValue;
    unsigned descriptorBits;
    size_t bitmapBits;
    int rowBytes;

    if (!filePath || !filePath[0] || !info ||
        (out && outSize == 0u) || (!out && outSize != 0u))
        return false;
    path = XString_create_utf8(filePath);
    file = path ? XFile_create() : NULL;
    if (!path || !file)
        goto failed;
    XFile_setFileName(file, path);
    if (!XFile_open_2(file, XIODevice_ReadOnly, 0))
    {
        /* 回归程序既可从仓库根目录直接运行，也可由 CTest 从 build/
           目录运行。配置的 ../Library/XFont 在后者有效，前者需要去掉
           前导 ../；仅在首个打开失败且确实为相对上级路径时尝试该回退。 */
        if (filePath[0] == '.' && filePath[1] == '.' &&
            (filePath[2] == '/' || filePath[2] == '\\'))
        {
            const char* alternatePath = filePath + 3;
            XClassDelete((XClass*)file);
            XClassDelete((XClass*)path);
            file = NULL;
            path = XString_create_utf8(alternatePath);
            file = path ? XFile_create() : NULL;
            if (!path || !file)
                goto failed;
            XFile_setFileName(file, path);
            if (!XFile_open_2(file, XIODevice_ReadOnly, 0))
                goto failed;
        }
        else
            goto failed;
    }
    bytes = XIODevice_readAll_3((XIODevice*)file);
    XIODevice_close_base((XIODevice*)file);
    if (!bytes)
        goto failed;
    size = XByteArray_size_base((XContainer*)bytes);
    data = XByteArray_data(bytes);

    if (!XFont_fileSection(data, size, 0u, "head", &headLength) ||
        headLength < 44u)
        goto failed;
    fontSize = XFont_fileLe16(data + 14u);
    ascent = XFont_fileLe16(data + 16u);
    descent = XFont_fileLe16s(data + 18u);
    bpp = data[37u];
    xyBits = data[38u];
    whBits = data[39u];
    advanceBits = data[40u];
    compression = data[41u];
    if (fontSize == 0u || fontSize > XFONT_BITMAP_MAX_WIDTH ||
        ascent == 0u || descent >= 0 ||
        (uint32_t)ascent - (int32_t)descent > XFONT_BITMAP_MAX_HEIGHT ||
        (bpp != 1u && bpp != 2u && bpp != 3u && bpp != 4u && bpp != 8u) ||
        compression > 1u || xyBits == 0u || whBits == 0u ||
        xyBits > 31u || whBits > 31u || advanceBits > 32u ||
        (compression != 0u && (bpp == 1u || bpp == 8u)))
        goto failed;
    info->m_width = (int)fontSize;
    info->m_height = (int)((uint32_t)ascent - (int32_t)descent);
    info->m_ascent = (int)ascent;
    info->m_descent = -(int)descent;
    info->m_bpp = (int)bpp;
    info->m_rowBytes = (info->m_width * info->m_bpp + 7) / 8;
    if (info->m_rowBytes < 1 || info->m_rowBytes > XFONT_BITMAP_MAX_ROW_BYTES)
        goto failed;
    if (!dsc)
        goto success;

    cmapStart = headLength;
    if (!XFont_fileSection(data, size, cmapStart, "cmap", &cmapLength) ||
        cmapLength < 12u)
        goto failed;
    cmapCount = XFont_fileLe32(data + cmapStart + 8u);
    cmapTableStart = cmapStart + 12u;
    if (cmapCount == 0u || cmapCount > (cmapLength - 12u) / 16u)
        goto failed;
    for (i = 0u; i < cmapCount; ++i)
    {
        const unsigned char* table = data + cmapTableStart + i * 16u;
        uint32_t dataOffset = XFont_fileLe32(table);
        uint32_t rangeStart = XFont_fileLe32(table + 4u);
        uint16_t rangeLength = XFont_fileLe16(table + 8u);
        uint16_t glyphStartId = XFont_fileLe16(table + 10u);
        uint16_t entries = XFont_fileLe16(table + 12u);
        unsigned format = table[14u];
        uint32_t relative;
        if (cp < rangeStart || cp - rangeStart >= rangeLength)
            continue;
        relative = cp - rangeStart;
        if (format == XFontCmapFormat0Tiny)
        {
            glyphId = (uint32_t)glyphStartId + relative;
            found = true;
        }
        else if (format == XFontCmapFormat0Full)
        {
            if (relative >= entries || dataOffset > cmapLength ||
                relative >= cmapLength - dataOffset)
                goto failed;
            glyphId = (uint32_t)glyphStartId +
                      data[cmapStart + dataOffset + relative];
            found = true;
        }
        else if (format == XFontCmapSparseTiny ||
                 format == XFontCmapSparseFull)
        {
            size_t listStart;
            uint16_t lo = 0u, hi = entries;
            if (dataOffset > cmapLength ||
                (size_t)entries > (cmapLength - dataOffset) / 2u)
                goto failed;
            listStart = cmapStart + dataOffset;
            while (lo < hi)
            {
                uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
                uint16_t item = XFont_fileLe16(data + listStart +
                                                (size_t)mid * 2u);
                if (item < relative) lo = (uint16_t)(mid + 1u);
                else hi = mid;
            }
            if (lo < entries && XFont_fileLe16(data + listStart +
                                                (size_t)lo * 2u) == relative)
            {
                glyphId = glyphStartId + lo;
                if (format == XFontCmapSparseFull)
                {
                    size_t offsetStart = listStart + (size_t)entries * 2u;
                    if (offsetStart > cmapStart + cmapLength ||
                        (size_t)entries >
                            (cmapStart + cmapLength - offsetStart) / 2u ||
                        offsetStart + (size_t)lo * 2u + 2u >
                            cmapStart + cmapLength)
                        goto failed;
                    glyphId += XFont_fileLe16(data + offsetStart +
                                               (size_t)lo * 2u);
                }
                found = true;
            }
        }
        if (found) break;
    }
    if (!found || glyphId == 0u)
        goto failed;

    locaStart = cmapStart + cmapLength;
    if (!XFont_fileSection(data, size, locaStart, "loca", &locaLength) ||
        locaLength < 12u)
        goto failed;
    locaCount = XFont_fileLe32(data + locaStart + 8u);
    if (glyphId >= locaCount || locaCount == 0u)
        goto failed;
    {
        unsigned indexFormat = data[34u];
        size_t itemSize = indexFormat == 0u ? 2u : 4u;
        size_t itemStart = locaStart + 12u + (size_t)glyphId * itemSize;
        size_t nextStart = itemStart + itemSize;
        if ((indexFormat != 0u && indexFormat != 1u) ||
            locaCount > (locaLength - 12u) / itemSize ||
            itemStart + itemSize > locaStart + locaLength)
            goto failed;
        glyphOffset = indexFormat == 0u ? XFont_fileLe16(data + itemStart)
                                        : XFont_fileLe32(data + itemStart);
        if (glyphId + 1u < locaCount)
            nextOffset = indexFormat == 0u ? XFont_fileLe16(data + nextStart)
                                           : XFont_fileLe32(data + nextStart);
        else
            nextOffset = 0u;
    }
    glyfStart = locaStart + locaLength;
    if (!XFont_fileSection(data, size, glyfStart, "glyf", &glyfLength) ||
        glyphOffset < 8u || glyphOffset >= glyfLength)
        goto failed;
    if (glyphId + 1u >= locaCount)
        nextOffset = (uint32_t)glyfLength;
    if (nextOffset <= glyphOffset || nextOffset > glyfLength)
        goto failed;
    recordStart = glyfStart + (size_t)glyphOffset;
    recordEnd = glyfStart + (size_t)nextOffset;
    reader.m_data = data;
    reader.m_size = size;
    reader.m_bit = recordStart * 8u;
    descriptorBits = advanceBits + 2u * xyBits + 2u * whBits;
    if (descriptorBits > (recordEnd - recordStart) * 8u)
        goto failed;
    memset(dsc, 0, sizeof(*dsc));
    if (advanceBits == 0u)
        dsc->adv_w = XFont_fileLe16(data + 30u);
    else if (!XFont_fileReadBits(&reader, advanceBits, &raw))
        goto failed;
    else
        dsc->adv_w = raw;
    if (advanceBits != 0u && data[36u] == 0u)
        dsc->adv_w *= 16u;
    if (!XFont_fileReadSignedBits(&reader, xyBits, &signedValue)) goto failed;
#if XFONT_LVGL_FMT_TXT_LARGE
    dsc->ofs_x = (int16_t)signedValue;
#else
    if (signedValue < -128 || signedValue > 127) goto failed;
    dsc->ofs_x = (int8_t)signedValue;
#endif
    if (!XFont_fileReadSignedBits(&reader, xyBits, &signedValue)) goto failed;
#if XFONT_LVGL_FMT_TXT_LARGE
    dsc->ofs_y = (int16_t)signedValue;
#else
    if (signedValue < -128 || signedValue > 127) goto failed;
    dsc->ofs_y = (int8_t)signedValue;
#endif
    if (!XFont_fileReadBits(&reader, whBits, &raw)) goto failed;
#if XFONT_LVGL_FMT_TXT_LARGE
    dsc->box_w = (uint16_t)raw;
#else
    if (raw > 255u) goto failed;
    dsc->box_w = (uint8_t)raw;
#endif
    if (!XFont_fileReadBits(&reader, whBits, &raw)) goto failed;
#if XFONT_LVGL_FMT_TXT_LARGE
    dsc->box_h = (uint16_t)raw;
#else
    if (raw > 255u) goto failed;
    dsc->box_h = (uint8_t)raw;
#endif
    dsc->bitmap_index = 0u;
    if (dsc->box_w > XFONT_BITMAP_MAX_WIDTH ||
        dsc->box_h > XFONT_BITMAP_MAX_HEIGHT)
        goto failed;
    if (!out)
        goto success;
    rowBytes = XFont_glyphRowBytes(dsc, (int)bpp);
    bitmapBits = (size_t)dsc->box_w * (size_t)dsc->box_h * (size_t)bpp;
    if (rowBytes < 1 || outSize < (size_t)rowBytes * dsc->box_h ||
        reader.m_bit > recordEnd * 8u ||
        (compression == 0u && bitmapBits > recordEnd * 8u - reader.m_bit))
        goto failed;
    memset(out, 0, (size_t)rowBytes * dsc->box_h);
    if (compression == 0u)
    {
        int y, x;
        for (y = 0; y < dsc->box_h; ++y)
            for (x = 0; x < dsc->box_w; ++x)
            {
                if (!XFont_fileReadBits(&reader, bpp, &raw)) goto failed;
                XFont_writePackedPixel(out,
                    ((size_t)y * (size_t)rowBytes + (size_t)x * bpp / 8u) * 8u +
                    ((size_t)x * bpp % 8u), bpp, raw);
            }
    }
    else
    {
        uint8_t pixels[XFONT_BITMAP_MAX_WIDTH * XFONT_BITMAP_MAX_HEIGHT];
        XFontFileRleReader rle;
        size_t pixelCount = (size_t)dsc->box_w * dsc->box_h;
        size_t p;
        memset(&rle, 0, sizeof(rle));
        rle.m_bits = reader;
        rle.m_bpp = bpp;
        for (p = 0u; p < pixelCount; ++p)
        {
            if (!XFont_fileRleNext(&rle, &raw)) goto failed;
            pixels[p] = (uint8_t)raw;
        }
        for (p = (size_t)dsc->box_w; p < pixelCount; ++p)
            pixels[p] = (uint8_t)(pixels[p] ^ pixels[p - dsc->box_w]);
        for (p = 0u; p < pixelCount; ++p)
            XFont_writePackedPixel(out,
                (p / (size_t)dsc->box_w * (size_t)rowBytes * 8u) +
                (p % (size_t)dsc->box_w) * bpp, bpp, pixels[p]);
    }
success:
    XClassDelete((XClass*)bytes);
    XClassDelete((XClass*)file);
    XClassDelete((XClass*)path);
    return true;
failed:
    if (bytes) XClassDelete((XClass*)bytes);
    if (file) XClassDelete((XClass*)file);
    if (path) XClassDelete((XClass*)path);
    return false;
}
#endif /* XFONT_FILE_ON && XFONT_LVGL8_FILE_ON */

#if XFONT_FILE_ON && XFONT_LVGL8_FILE_ON
/* .bin 存在性探测缓存（按家族名，负结果同样入缓）：XFont_face 对未注册
   家族每次都探测位图外挂文件，轮廓字库改走外挂加载后默认家族不再注册，
   无缓存则每帧数千次失败 open。进程期内文件视为不可变。 */
typedef struct XFontBinProbeEntry
{
    char m_family[XFONT_EXTERNAL_FONT_PATH_MAX];
    XFontBitmapInfo m_info;
    bool m_present;
    uint32_t m_stamp;
} XFontBinProbeEntry;
static XFontBinProbeEntry g_binProbe[4];
static uint32_t g_binProbeStamp;
#endif /* XFONT_FILE_ON && XFONT_LVGL8_FILE_ON */

bool XFontBitmapFace_fileInfo(const XFont* self, XFontBitmapInfo* info)
{
    const char* family = self ? XFont_family(self) : XFONT_DEFAULT_FAMILY;
#if XFONT_FILE_ON && XFONT_LVGL8_FILE_ON
    char filePath[XFONT_EXTERNAL_FONT_PATH_MAX];
    XFontBitmapInfo fileInfo;
    XFontBinProbeEntry* slot = NULL;
    int i;
    if (!info)
        return false;
    if (!family || !family[0])
        family = XFONT_DEFAULT_FAMILY;
    if (strlen(family) < sizeof(g_binProbe[0].m_family))
    {
        for (i = 0; i < 4; ++i)
        {
            XFontBinProbeEntry* e = &g_binProbe[i];
            if (e->m_family[0] && strcmp(e->m_family, family) == 0)
            {
                e->m_stamp = ++g_binProbeStamp;
                if (e->m_present)
                    *info = e->m_info;
                return e->m_present;
            }
            if (!slot && !e->m_family[0])
                slot = e;
        }
        if (!slot)
        {
            uint32_t oldest = 0xFFFFFFFFu;
            for (i = 0; i < 4; ++i)
                if (g_binProbe[i].m_stamp < oldest)
                {
                    oldest = g_binProbe[i].m_stamp;
                    slot = &g_binProbe[i];
                }
        }
    }
    memset(&fileInfo, 0, sizeof(fileInfo));
    if (XFont_externalBinPath(family, filePath, sizeof(filePath)) &&
        XFont_load_lvgl_bin_glyph(filePath, 0u, &fileInfo, NULL, NULL, 0u))
    {
        *info = fileInfo;
        if (slot)
        {
            memcpy(slot->m_family, family, strlen(family) + 1u);
            slot->m_info = fileInfo;
            slot->m_present = true;
            slot->m_stamp = ++g_binProbeStamp;
        }
        return true;
    }
    if (slot)
    {
        memcpy(slot->m_family, family, strlen(family) + 1u);
        slot->m_present = false;
        slot->m_stamp = ++g_binProbeStamp;
    }
    return false;
#else
    (void)family;
    (void)info;
    return false;
#endif
}

bool XFontBitmapFace_fileLoadGlyph(const XFont* self, uint32_t codepoint,
                                   XFontGlyphDsc* dsc, unsigned char* out,
                                   size_t outSize)
{
    const char* family = self ? XFont_family(self) : XFONT_DEFAULT_FAMILY;
#if XFONT_FILE_ON && XFONT_LVGL8_FILE_ON
    char filePath[XFONT_EXTERNAL_FONT_PATH_MAX];
    XFontBitmapInfo fileInfo;
    if (!dsc || (out && outSize == 0u) || (!out && outSize != 0u))
        return false;
    if (!family || !family[0])
        family = XFONT_DEFAULT_FAMILY;
    memset(&fileInfo, 0, sizeof(fileInfo));
    return XFont_externalBinPath(family, filePath, sizeof(filePath)) &&
           XFont_load_lvgl_bin_glyph(filePath, codepoint, &fileInfo, dsc,
                                     out, outSize);
#else
    (void)family;
    (void)codepoint;
    (void)dsc;
    (void)out;
    (void)outSize;
    return false;
#endif
}

/* ========== 属性访问 ========== */

const char* XFont_family(const XFont* self)
{
    if (!self) return "";
    return self->m_family ? XString_toUtf8(self->m_family) : "";
}

void XFont_setFamily(XFont* self, const char* family)
{
    if (!self) return;
    if (!XFont_isSharedFamily(self->m_family))
        XClassDelete((XClass*)self->m_family);
    self->m_family = NULL;
    if (family && family[0])
    {
        /* 默认家族走进程内共享实例：XFont_init 的热路径（XPainter 每帧
           init/end 各一次）不再重复建 XString。 */
        if (XFONT_DEFAULT_FAMILY[0] != '\0' &&
            strcmp(family, XFONT_DEFAULT_FAMILY) == 0)
            self->m_family = XFont_sharedDefaultFamily();
        else
            self->m_family = XString_create_utf8(family);
    }
}

const char* XFont_styleName(const XFont* self)
{
    if (!self) return "";
    return self->m_styleName ? XString_toUtf8(self->m_styleName) : "";
}

void XFont_setStyleName(XFont* self, const char* styleName)
{
    if (!self) return;
    if (self->m_styleName) XClassDelete((XClass*)self->m_styleName);
    if (styleName && styleName[0]) {
        self->m_styleName = XString_create_utf8(styleName);
    } else {
        self->m_styleName = NULL;
    }
}

int XFont_pointSize(const XFont* self)
{
    if (!self) return -1;
    return (int)(self->m_pointSizeF + 0.5);
}

void XFont_setPointSize(XFont* self, int pointSize)
{
    if (self) self->m_pointSizeF = (double)pointSize;
}

double XFont_pointSizeF(const XFont* self)
{
    return self ? self->m_pointSizeF : 0.0;
}

void XFont_setPointSizeF(XFont* self, double pointSize)
{
    if (self) self->m_pointSizeF = pointSize;
}

int XFont_pixelSize(const XFont* self)
{
    return self ? self->m_pixelSize : -1;
}

void XFont_setPixelSize(XFont* self, int pixelSize)
{
    if (self) self->m_pixelSize = pixelSize;
}

int XFont_bitmapPixelSize(const XFont* self, int defaultPixelSize)
{
    if (self && self->m_pixelSize > 0)
        return self->m_pixelSize;
    return defaultPixelSize;
}

int XFont_weight(const XFont* self)
{
    return self ? self->m_weight : XFont_Normal;
}

void XFont_setWeight(XFont* self, int weight)
{
    if (self) self->m_weight = weight;
}

bool XFont_bold(const XFont* self)
{
    return self ? (self->m_weight > XFont_Medium) : false;
}

void XFont_setBold(XFont* self, bool bold)
{
    if (self) self->m_weight = bold ? XFont_Bold : XFont_Normal;
}

int XFont_style(const XFont* self)
{
    return self ? self->m_style : XFont_StyleNormal;
}

void XFont_setStyle(XFont* self, int style)
{
    if (self) self->m_style = style;
}

bool XFont_italic(const XFont* self)
{
    return self ? (self->m_style != XFont_StyleNormal) : false;
}

void XFont_setItalic(XFont* self, bool italic)
{
    if (self) self->m_style = italic ? XFont_StyleItalic : XFont_StyleNormal;
}

bool XFont_underline(const XFont* self)
{
    return self ? (bool)self->m_underline : false;
}

void XFont_setUnderline(XFont* self, bool underline)
{
    if (self) self->m_underline = underline ? 1 : 0;
}

bool XFont_strikeOut(const XFont* self)
{
    return self ? (bool)self->m_strikeOut : false;
}

void XFont_setStrikeOut(XFont* self, bool strikeOut)
{
    if (self) self->m_strikeOut = strikeOut ? 1 : 0;
}

bool XFont_overline(const XFont* self)
{
    return self ? (bool)self->m_overline : false;
}

void XFont_setOverline(XFont* self, bool overline)
{
    if (self) self->m_overline = overline ? 1 : 0;
}

bool XFont_fixedPitch(const XFont* self)
{
    return self ? (bool)self->m_fixedPitch : false;
}

void XFont_setFixedPitch(XFont* self, bool fixedPitch)
{
    if (self) self->m_fixedPitch = fixedPitch ? 1 : 0;
}

bool XFont_kerning(const XFont* self)
{
    /* [文档勘误 2026-10-07] 缺省 true 与 Qt 一致，但当前实现尚未应用
     * kern 表（无 FT_Get_Kerning/整形步进点），本开关暂无效——如实
     * 标注避免缺省值误导调用方预期。 */
    return self ? !!self->m_kerning : false;
}

void XFont_setKerning(XFont* self, bool kerning)
{
    if (self) self->m_kerning = kerning ? 1 : 0;
}

int XFont_capitalization(const XFont* self)
{
    return self ? (int)self->m_capitalization : XFont_MixedCase;
}

void XFont_setCapitalization(XFont* self, int capitalization)
{
    if (self) self->m_capitalization = (uint32_t)capitalization;
}

int XFont_stretch(const XFont* self)
{
    return self ? self->m_stretch : XFont_Unstretched;
}

void XFont_setStretch(XFont* self, int stretch)
{
    if (self) self->m_stretch = stretch;
}

int XFont_styleHint(const XFont* self)
{
    return self ? (int)self->m_styleHint : XFont_AnyStyle;
}

void XFont_setStyleHint(XFont* self, int styleHint)
{
    if (self) self->m_styleHint = (uint32_t)styleHint;
}

int XFont_styleStrategy(const XFont* self)
{
    return self ? (int)self->m_styleStrategy : XFont_PreferDefault;
}

void XFont_setStyleStrategy(XFont* self, int styleStrategy)
{
    if (self) self->m_styleStrategy = (uint32_t)styleStrategy;
}

int XFont_hintingPreference(const XFont* self)
{
    return self ? (int)self->m_hintingPreference : XFont_PreferDefaultHinting;
}

void XFont_setHintingPreference(XFont* self, int hintingPreference)
{
    if (self) self->m_hintingPreference = (uint32_t)hintingPreference;
}

float XFont_letterSpacing(const XFont* self)
{
    return self ? self->m_letterSpacingValue : 0.0f;
}

void XFont_setLetterSpacing(XFont* self, float spacing)
{
    if (self) {
        self->m_letterSpacing = 1;
        self->m_letterSpacingValue = spacing;
    }
}

int XFont_letterSpacingType(const XFont* self)
{
    return self ? self->m_letterSpacingType : XFont_PercentageSpacing;
}

void XFont_setLetterSpacingType(XFont* self, int type)
{
    if (self) self->m_letterSpacingType = type;
}

float XFont_wordSpacing(const XFont* self)
{
    return self ? self->m_wordSpacingValue : 0.0f;
}

void XFont_setWordSpacing(XFont* self, float spacing)
{
    if (self) {
        self->m_wordSpacing = 1;
        self->m_wordSpacingValue = spacing;
    }
}

uint32_t XFont_resolveMask(const XFont* self)
{
    return self ? self->m_resolveMask : 0;
}

void XFont_setResolveMask(XFont* self, uint32_t mask)
{
    if (self) self->m_resolveMask = mask;
}

/* ========== 工具方法 ========== */

bool XFont_equals(const XFont* a, const XFont* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if (strcmp(XFont_family(a), XFont_family(b)) != 0) return false;
    if (a->m_pointSizeF != b->m_pointSizeF) return false;
    if (a->m_weight != b->m_weight) return false;
    if (a->m_style != b->m_style) return false;
    if (a->m_underline != b->m_underline) return false;
    if (a->m_strikeOut != b->m_strikeOut) return false;
    if (a->m_fixedPitch != b->m_fixedPitch) return false;
    if (a->m_stretch != b->m_stretch) return false;
    if (a->m_kerning != b->m_kerning) return false;
    if (a->m_capitalization != b->m_capitalization) return false;
    return true;
}

XString* XFont_toString(const XFont* self)
{
    if (!self) return NULL;
    char buf[256];
    const char* family = XFont_family(self);
    if (family[0] == '\0') family = "Unknown";
    snprintf(buf, sizeof(buf), "%s,%d,%d,%d", family,
             (int)self->m_pointSizeF, self->m_weight, self->m_style);
    return XString_create_utf8(buf);
}

bool XFont_fromString(XFont* out, const char* str)
{
    if (!out || !str) return false;
    char family[128];
    int pointSize = 12, weight = XFont_Normal, style = XFont_StyleNormal;
    if (sscanf(str, "%127[^,],%d,%d,%d", family, &pointSize, &weight, &style) >= 1) {
        XFont_setFamily(out, family);
        XFont_setPointSize(out, pointSize);
        XFont_setWeight(out, weight);
        XFont_setStyle(out, style);
        return true;
    }
    return false;
}

void XFont_swap(XFont* a, XFont* b)
{
    if (!a || !b) return;
    XFont tmp = *a;
    *a = *b;
    *b = tmp;
    /* 交换后 vtable 要保持正确，所以需要重新设置 */
    XClassSetVtable(a, XFont);
    XClassSetVtable(b, XFont);
}
