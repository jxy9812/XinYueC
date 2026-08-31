/******************************************************************************
 * @file       XFont.c
 * @brief      XFont 字体类实现（对标 Qt 6.8 QFont）
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XFont.h"
#if XFONT_FILE_ON && XFONT_LVGL8_FILE_ON
#include "XFile.h"
#include "XByteArray.h"
#endif /* XFONT_FILE_ON && XFONT_LVGL8_FILE_ON */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* 字库注册装配由 XFontRegistration.c 提供；不公开额外头文件。 */
void XFont_registerProviders(void);

static XFontBitmapProvider g_bitmapProviders[XFONT_MAX_BITMAP_PROVIDERS];
static size_t g_bitmapProviderCount;
static bool g_bitmapProvidersInitialized;

static int XFont_bitmapRowBytes(const XFontBitmapInfo* info)
{
    if (!info || info->m_width < 1 ||
        (info->m_bpp != 1 && info->m_bpp != 2 && info->m_bpp != 3 &&
         info->m_bpp != 4 && info->m_bpp != 8))
        return 0;
    return (info->m_width * info->m_bpp + 7) / 8;
}

static int XFont_glyphRowBytes(const XFontGlyphDsc* dsc, int bpp)
{
    if (!dsc || dsc->box_w == 0 || dsc->box_h == 0 ||
        (bpp != 1 && bpp != 2 && bpp != 3 && bpp != 4 && bpp != 8))
        return 0;
    return ((int)dsc->box_w * bpp + 7) / 8;
}

/* LVGL fmt_txt stores a glyph as one continuous MSB-first bit stream. */
static size_t XFont_glyphPackedBytes(const XFontGlyphDsc* dsc, int bpp)
{
    size_t bits;
    if (!dsc || dsc->box_w == 0 || dsc->box_h == 0 ||
        (bpp != 1 && bpp != 2 && bpp != 3 && bpp != 4 && bpp != 8))
        return 0;
    bits = (size_t)dsc->box_w * (size_t)dsc->box_h * (size_t)bpp;
    return (bits + 7u) / 8u;
}

static unsigned XFont_readPackedPixel(const uint8_t* data, size_t bit,
                                      int bpp)
{
    unsigned value = 0;
    int i;
    for (i = 0; i < bpp; ++i)
    {
        size_t current = bit + (size_t)i;
        value = (value << 1) |
                ((data[current / 8u] >> (7u - (unsigned)(current % 8u))) & 1u);
    }
    return value;
}

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

/* Expand LVGL's continuous stream to the row-aligned XFont bitmap contract. */
static void XFont_unpackGlyph(const uint8_t* source, uint8_t* dest,
                              const XFontGlyphDsc* dsc, int bpp,
                              int rowBytes)
{
    int y;
    memset(dest, 0, (size_t)rowBytes * (size_t)dsc->box_h);
    for (y = 0; y < dsc->box_h; ++y)
    {
        int x;
        for (x = 0; x < dsc->box_w; ++x)
        {
            size_t sourceBit = ((size_t)y * (size_t)dsc->box_w +
                                (size_t)x) * (size_t)bpp;
            size_t destBit = ((size_t)y * (size_t)rowBytes * 8u) +
                             (size_t)x * (size_t)bpp;
            XFont_writePackedPixel(dest, destBit, bpp,
                                   XFont_readPackedPixel(source, sourceBit,
                                                          bpp));
        }
    }
}

static void XFont_ensureBitmapProviders(void)
{
    if (g_bitmapProvidersInitialized)
        return;
    /* Mark ready before dispatch so provider registration cannot recurse. */
    g_bitmapProvidersInitialized = true;
    XFont_registerProviders();
}

static const XFontBitmapProvider* XFont_findBitmapProvider(const char* family)
{
    size_t i;
    if (!family || !family[0])
        return NULL;
    XFont_ensureBitmapProviders();
    for (i = 0; i < g_bitmapProviderCount; ++i)
    {
        if (strcmp(g_bitmapProviders[i].m_family, family) == 0)
            return &g_bitmapProviders[i];
    }
    return NULL;
}

static const XFontBitmapProvider* XFont_firstBitmapProvider(void)
{
    XFont_ensureBitmapProviders();
    return g_bitmapProviderCount > 0 ? &g_bitmapProviders[0] : NULL;
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

bool XFont_registerBitmapProvider(const XFontBitmapProvider* provider)
{
    size_t i;
    if (!provider || !provider->m_family || !provider->m_family[0] ||
        (!provider->m_loadGlyph && !provider->m_data) ||
        provider->m_info.m_width < 1 ||
        provider->m_info.m_width > XFONT_BITMAP_MAX_WIDTH ||
        provider->m_info.m_height < 1 ||
        provider->m_info.m_height > XFONT_BITMAP_MAX_HEIGHT ||
        provider->m_info.m_ascent < 0 || provider->m_info.m_descent < 0 ||
        (provider->m_info.m_bpp != 1 && provider->m_info.m_bpp != 2 &&
         provider->m_info.m_bpp != 3 && provider->m_info.m_bpp != 4 &&
         provider->m_info.m_bpp != 8) ||
        provider->m_info.m_ascent + provider->m_info.m_descent >
            provider->m_info.m_height)
        return false;
    if (provider->m_info.m_rowBytes != XFont_bitmapRowBytes(&provider->m_info) ||
        provider->m_info.m_rowBytes < 1 ||
        provider->m_info.m_rowBytes > XFONT_BITMAP_MAX_ROW_BYTES)
        return false;
    if (provider->m_data &&
        (provider->m_data->bpp != (uint16_t)provider->m_info.m_bpp ||
         provider->m_data->bitmap_format != 0 || !provider->m_data->glyph_dsc ||
         !provider->m_data->cmaps || provider->m_data->cmap_num == 0))
        return false;
    XFont_ensureBitmapProviders();
    for (i = 0; i < g_bitmapProviderCount; ++i)
    {
        if (strcmp(g_bitmapProviders[i].m_family, provider->m_family) == 0)
        {
            g_bitmapProviders[i] = *provider;
            return true;
        }
    }
    if (g_bitmapProviderCount >= XFONT_MAX_BITMAP_PROVIDERS)
        return false;
    g_bitmapProviders[g_bitmapProviderCount++] = *provider;
    return true;
}

/* ========== 内部静态虚函数实现 ========== */

static void VXFont_deinit(XFont* self)
{
    if (!self) return;
    XString_delete_base((XClass*)self->m_family);
    self->m_family = NULL;
    XString_delete_base((XClass*)self->m_styleName);
    self->m_styleName = NULL;
}

static void VXFont_copy(XFont* dest, const XFont* src)
{
    if (!dest || !src || dest == src) return;
    if (XClassIsVtableNull(src)) return;
    if (XClassIsVtableNull(dest))
        XFont_init(dest);
    /* copy_base 要求目标已有生命周期；复制前释放目标的字符串资源。 */
    XString_delete_base((XClass*)dest->m_family);
    XString_delete_base((XClass*)dest->m_styleName);
    dest->m_family = NULL;
    dest->m_styleName = NULL;
    /* 复制家族字符串 */
    if (src->m_family) {
        dest->m_family = XString_create_copy(src->m_family);
    } else {
        dest->m_family = NULL;
    }
    /* 复制样式名称 */
    if (src->m_styleName) {
        dest->m_styleName = XString_create_copy(src->m_styleName);
    } else {
        dest->m_styleName = NULL;
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
    XString_delete_base((XClass*)dest->m_family);
    XString_delete_base((XClass*)dest->m_styleName);
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
            XClass_delete_base((XClass*)file);
            XClass_delete_base((XClass*)path);
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
    XClass_delete_base((XClass*)bytes);
    XClass_delete_base((XClass*)file);
    XClass_delete_base((XClass*)path);
    return true;
failed:
    if (bytes) XClass_delete_base((XClass*)bytes);
    if (file) XClass_delete_base((XClass*)file);
    if (path) XClass_delete_base((XClass*)path);
    return false;
}
#endif /* XFONT_FILE_ON && XFONT_LVGL8_FILE_ON */

bool XFont_bitmapFontInfo(const XFont* self, XFontBitmapInfo* info)
{
    const char* family = self ? XFont_family(self) : XFONT_DEFAULT_FAMILY;
    const XFontBitmapProvider* provider;
    if (!info)
        return false;
    if (!family || !family[0])
        family = XFONT_DEFAULT_FAMILY;
    provider = XFont_findBitmapProvider(family);
    if (provider)
    {
        *info = provider->m_info;
        return true;
    }
#if XFONT_FILE_ON && XFONT_LVGL8_FILE_ON
    if (family[0])
    {
        char filePath[XFONT_EXTERNAL_FONT_PATH_MAX];
        XFontBitmapInfo fileInfo;
        memset(&fileInfo, 0, sizeof(fileInfo));
        if (XFont_externalBinPath(family, filePath, sizeof(filePath)) &&
            XFont_load_lvgl_bin_glyph(filePath, 0u, &fileInfo, NULL, NULL, 0))
        {
            *info = fileInfo;
            return true;
        }
    }
#endif /* XFONT_FILE_ON && XFONT_LVGL8_FILE_ON */
    provider = XFont_firstBitmapProvider();
    if (!provider)
        return false;
    *info = provider->m_info;
    return true;
}

static bool XFont_findGlyphInData(const XFontBitmapData* data, uint32_t cp,
                                  XFontGlyphDsc* dsc)
{
    uint16_t i;
    if (!data || !data->glyph_dsc || !data->cmaps || !dsc ||
        data->bitmap_format != 0)
        return false;
    for (i = 0; i < data->cmap_num; ++i)
    {
        const XFontCmap* cmap = &data->cmaps[i];
        uint32_t relative;
        uint32_t glyphId;
        if (cp < cmap->range_start ||
            cp - cmap->range_start >= cmap->range_length)
            continue;
        relative = cp - cmap->range_start;
        switch (cmap->type)
        {
        case XFontCmapFormat0Full:
            glyphId = (uint32_t)cmap->glyph_id_start + relative;
            if (cmap->glyph_id_ofs_list)
                glyphId += ((const uint8_t*)cmap->glyph_id_ofs_list)[relative];
            break;
        case XFontCmapFormat0Tiny:
            glyphId = (uint32_t)cmap->glyph_id_start + relative;
            if (cmap->glyph_id_ofs_list)
                glyphId += ((const uint8_t*)cmap->glyph_id_ofs_list)[relative];
            break;
        case XFontCmapSparseFull:
        case XFontCmapSparseTiny:
        {
            uint16_t lo = 0;
            uint16_t hi = cmap->list_length;
            const uint16_t* list = cmap->unicode_list;
            while (lo < hi)
            {
                uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);
                if (list[mid] < relative) lo = (uint16_t)(mid + 1u);
                else hi = mid;
            }
            if (lo >= cmap->list_length || list[lo] != relative)
                continue;
            glyphId = (uint32_t)cmap->glyph_id_start + lo;
            if (cmap->glyph_id_ofs_list)
                glyphId += (cmap->type == XFontCmapSparseTiny
                                ? ((const uint8_t*)cmap->glyph_id_ofs_list)[lo]
                                : ((const uint16_t*)cmap->glyph_id_ofs_list)[lo]);
            break;
        }
        default:
            continue;
        }
        if (glyphId == 0u)
            return false;
        *dsc = data->glyph_dsc[glyphId];
        return true;
    }
    return false;
}

int XFont_bitmapGlyphRowBytes(const XFontGlyphDsc* dsc, int bpp)
{
    return XFont_glyphRowBytes(dsc, bpp);
}

static bool XFont_findBitmapGlyphDsc(const XFont* self, uint32_t cp,
                                     XFontGlyphDsc* dsc)
{
    const char* family = self ? XFont_family(self) : XFONT_DEFAULT_FAMILY;
    const XFontBitmapProvider* provider;
    if (!dsc) return false;
    if (!family || !family[0]) family = XFONT_DEFAULT_FAMILY;
    provider = XFont_findBitmapProvider(family);
    if (provider && provider->m_data &&
        XFont_findGlyphInData(provider->m_data, cp, dsc))
        return true;
    /* Callback providers expose a regular cell. Their data can still be
       consumed through the LVGL-shaped API by using the provider metrics. */
    if (provider && provider->m_loadGlyph)
    {
        dsc->bitmap_index = 0;
        dsc->adv_w = (uint32_t)provider->m_info.m_width * 16u;
        dsc->box_w = (uint8_t)provider->m_info.m_width;
        dsc->box_h = (uint8_t)provider->m_info.m_height;
        dsc->ofs_x = 0;
        dsc->ofs_y = (int8_t)-provider->m_info.m_descent;
        return true;
    }
#if XFONT_FILE_ON && XFONT_LVGL8_FILE_ON
    if (provider == NULL && family[0])
    {
        char filePath[XFONT_EXTERNAL_FONT_PATH_MAX];
        XFontBitmapInfo fileInfo;
        memset(&fileInfo, 0, sizeof(fileInfo));
        if (XFont_externalBinPath(family, filePath, sizeof(filePath)) &&
            XFont_load_lvgl_bin_glyph(filePath, cp, &fileInfo, dsc, NULL, 0))
        {
            return true;
        }
    }
#endif
    provider = XFont_firstBitmapProvider();
    if (!provider) return false;
    if (provider->m_data)
        return XFont_findGlyphInData(provider->m_data, cp, dsc);
    if (provider->m_loadGlyph)
    {
        dsc->bitmap_index = 0;
        dsc->adv_w = (uint32_t)provider->m_info.m_width * 16u;
        dsc->box_w = (uint8_t)provider->m_info.m_width;
        dsc->box_h = (uint8_t)provider->m_info.m_height;
        dsc->ofs_x = 0;
        dsc->ofs_y = (int8_t)-provider->m_info.m_descent;
        return true;
    }
    return false;
}

bool XFont_bitmapLoadGlyph(const XFont* self, uint32_t cp,
                           XFontGlyphDsc* dsc, unsigned char* out,
                           size_t outSize)
{
    XFontBitmapInfo info;
    const char* family = self ? XFont_family(self) : XFONT_DEFAULT_FAMILY;
    const XFontBitmapProvider* provider;
    size_t rowBytes;
    size_t bitmapSize;
    if (!dsc || (out && outSize == 0) || (!out && outSize != 0) ||
        !XFont_bitmapFontInfo(self, &info) ||
        !XFont_findBitmapGlyphDsc(self, cp, dsc))
        return false;
    if (!out)
        return true;
    provider = XFont_findBitmapProvider(family);
    if (provider && provider->m_data &&
        XFont_findGlyphInData(provider->m_data, cp, dsc))
    {
        rowBytes = (size_t)XFont_glyphRowBytes(dsc, info.m_bpp);
        bitmapSize = rowBytes * (size_t)dsc->box_h;
        if (!provider->m_data->glyph_bitmap || outSize < bitmapSize)
            return false;
        if (XFont_glyphPackedBytes(dsc, info.m_bpp) == bitmapSize)
            memcpy(out, provider->m_data->glyph_bitmap + dsc->bitmap_index,
                   bitmapSize);
        else
            XFont_unpackGlyph(provider->m_data->glyph_bitmap +
                                  dsc->bitmap_index,
                              out, dsc, info.m_bpp, (int)rowBytes);
        if (bitmapSize < outSize) memset(out + bitmapSize, 0,
                                         outSize - bitmapSize);
        return true;
    }
    rowBytes = (size_t)info.m_rowBytes;
    bitmapSize = rowBytes * (size_t)info.m_height;
    if (outSize < bitmapSize)
        return false;
    if (provider && provider->m_loadGlyph &&
        provider->m_loadGlyph(cp, out, outSize))
        return true;
#if XFONT_FILE_ON && XFONT_LVGL8_FILE_ON
    if (!provider && family[0])
    {
        char filePath[XFONT_EXTERNAL_FONT_PATH_MAX];
        if (XFont_externalBinPath(family, filePath, sizeof(filePath)) &&
            XFont_load_lvgl_bin_glyph(filePath, cp, &info, dsc, out, outSize))
            return true;
    }
#endif /* XFONT_FILE_ON && XFONT_LVGL8_FILE_ON */
    provider = XFont_firstBitmapProvider();
    if (provider && provider->m_data)
    {
        rowBytes = (size_t)XFont_glyphRowBytes(dsc, info.m_bpp);
        bitmapSize = rowBytes * (size_t)dsc->box_h;
        if (!provider->m_data->glyph_bitmap || outSize < bitmapSize)
            return false;
        if (XFont_glyphPackedBytes(dsc, info.m_bpp) == bitmapSize)
            memcpy(out, provider->m_data->glyph_bitmap + dsc->bitmap_index,
                   bitmapSize);
        else
            XFont_unpackGlyph(provider->m_data->glyph_bitmap +
                                  dsc->bitmap_index,
                              out, dsc, info.m_bpp, (int)rowBytes);
        if (bitmapSize < outSize) memset(out + bitmapSize, 0,
                                         outSize - bitmapSize);
        return true;
    }
    return provider && provider->m_loadGlyph &&
           provider->m_loadGlyph(cp, out, outSize);
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
    if (self->m_family) XString_delete_base((XClass*)self->m_family);
    if (family && family[0]) {
        self->m_family = XString_create_utf8(family);
    } else {
        self->m_family = NULL;
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
    if (self->m_styleName) XString_delete_base((XClass*)self->m_styleName);
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
    return self ? (bool)self->m_kerning : false;
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
