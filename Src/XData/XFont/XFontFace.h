/******************************************************************************
 * @file       XFontFace.h
 * @brief      XFont 字库后端抽象类及公共字库数据定义。
 * @author     XinYueC 团队
 * @note       XFont 只保存字体属性；实际字形数据由 XFontFace 及其派生类提供。
 ******************************************************************************/
#ifndef XFONTFACE_H
#define XFONTFACE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "XClass.h"
#include "XFont_config.h"

struct XFont;
typedef struct XFont XFont;

/** @brief 点阵字库的全局度量，单位为字体设计像素。 */
typedef struct XFontBitmapInfo
{
    int m_width;       /**< 缺省字形宽度。 */
    int m_height;      /**< 字库行高。 */
    int m_ascent;      /**< 基线以上高度。 */
    int m_descent;     /**< 基线以下高度。 */
    int m_rowBytes;    /**< 每行存储字节数。 */
    int m_bpp;         /**< 每像素位数，支持 1、2、3、4、8。 */
} XFontBitmapInfo;

/** @brief LVGL fmt_txt 字形描述的无依赖镜像。 */
typedef struct XFontGlyphDsc
{
#if XFONT_LVGL_FMT_TXT_LARGE
    uint32_t bitmap_index;
    uint32_t adv_w;
    uint16_t box_w;
    uint16_t box_h;
    int16_t ofs_x;
    int16_t ofs_y;
#else
    uint32_t bitmap_index : 20;
    uint32_t adv_w : 12;
    uint8_t box_w;
    uint8_t box_h;
    int8_t ofs_x;
    int8_t ofs_y;
#endif
} XFontGlyphDsc;

/** @brief LVGL fmt_txt cmap 类型。数值与 LVGL 保持一致。 */
typedef enum XFontCmapType
{
    XFontCmapFormat0Full = 0,
    XFontCmapSparseFull = 1,
    XFontCmapFormat0Tiny = 2,
    XFontCmapSparseTiny = 3
} XFontCmapType;

/** @brief LVGL fmt_txt 字符映射描述。 */
typedef struct XFontCmap
{
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    const uint16_t* unicode_list;
    const void* glyph_id_ofs_list;
    uint16_t list_length;
    XFontCmapType type;
} XFontCmap;

/** @brief LVGL fmt_txt 字体数据描述的无依赖镜像。 */
typedef struct XFontBitmapData
{
    const uint8_t* glyph_bitmap;
    const XFontGlyphDsc* glyph_dsc;
    const XFontCmap* cmaps;
    const void* kern_dsc;
    uint16_t kern_scale;
    uint16_t cmap_num : 9;
    uint16_t bpp : 4;
    uint16_t kern_classes : 1;
    uint16_t bitmap_format : 2;
} XFontBitmapData;

/** @brief 点阵字库 provider；所有指针均为借用引用。 */
typedef struct XFontBitmapProvider
{
    const char* m_family;
    XFontBitmapInfo m_info;
    const XFontBitmapData* m_data;
    bool (*m_loadGlyph)(uint32_t cp, unsigned char* out, size_t outSize);
} XFontBitmapProvider;

/** @brief 轮廓字库的全局度量，坐标单位为字体设计单位。 */
typedef struct XFontOutlineInfo
{
    int unitsPerEm;
    int ascent;
    int descent;
    int lineGap;
} XFontOutlineInfo;

/** @brief 单个轮廓字形的度量，y 坐标以基线为零且向上为正。 */
typedef struct XFontOutlineGlyphMetrics
{
    int advance;
    int xMin;
    int yMin;
    int xMax;
    int yMax;
} XFontOutlineGlyphMetrics;

/** @brief 轮廓命令接收器；provider 依次调用这些回调输出路径。 */
typedef struct XFontOutlineSink
{
    void* userData;
    bool (*moveTo)(void* userData, float x, float y);
    bool (*lineTo)(void* userData, float x, float y);
    bool (*quadTo)(void* userData, float cx, float cy, float x, float y);
    bool (*cubicTo)(void* userData, float c1x, float c1y,
                    float c2x, float c2y, float x, float y);
    bool (*close)(void* userData);
} XFontOutlineSink;

/** @brief 轮廓字库 provider；provider 只提供设计单位轮廓和度量。 */
typedef struct XFontOutlineProvider
{
    const char* m_family;
    XFontOutlineInfo m_info;
    bool (*m_loadGlyph)(uint32_t codepoint,
                        XFontOutlineGlyphMetrics* metrics,
                        const XFontOutlineSink* sink,
                        void* userData);
    void* m_userData;
} XFontOutlineProvider;

/** @brief XFO1 轮廓命令 opcode。坐标均使用相对前一点的 int16 delta。 */
typedef enum XFontOutlineCommandType
{
    XFontOutline_MoveTo = 0,
    XFontOutline_LineTo = 1,
    XFontOutline_QuadTo = 2,
    XFontOutline_CubicTo = 3,
    XFontOutline_Close = 4
} XFontOutlineCommandType;

/**
 * @brief XFO1 版本 1 文件布局说明。
 * @details 文件使用小端序，头部固定 36 字节，依次包含 magic、版本、
 *          flags、unitsPerEm、ascent、descent、lineGap、cmapCount、
 *          glyphCount，以及 cmap/glyph/command 的偏移和命令区长度。
 *          cmap 项为 codepoint、glyphId 和保留字段；glyph 项为 advance、
 *          四个边界、命令相对偏移、命令数和保留字段。命令数据不含颜色。
 */

/** @brief 字库后端种类。 */
typedef enum XFontFaceKind
{
    XFontFace_None = 0,
    XFontFace_Bitmap = 1,
    XFontFace_Outline = 2
} XFontFaceKind;

/** @brief 字库公共度量输出；kind 决定使用 bitmap 或 outline 成员。 */
typedef struct XFontFaceInfo
{
    XFontFaceKind m_kind;
    XFontBitmapInfo m_bitmap;
    XFontOutlineInfo m_outline;
} XFontFaceInfo;

/* ========== XFontFace 虚函数表 ========== */
XCLASS_DEFINE_BEGING(XFontFace)
XCLASS_DEFINE_ENUM(XFontFace, Info) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XFontFace, LoadBitmapGlyph),
XCLASS_DEFINE_ENUM(XFontFace, LoadOutlineGlyph),
XCLASS_DEFINE_ENUM(XFontFace, BitmapGlyphRowBytes),
XCLASS_DEFINE_END(XFontFace)

/** @brief 字库后端抽象基类；m_class 必须是第一个成员。 */
typedef struct XFontFace
{
    XClass m_class;          /**< XClass 基类成员，必须位于第一位。 */
    XFontFaceKind m_kind;    /**< 当前 face 的后端种类。 */
    const char* m_family;    /**< 家族名借用指针；注册后必须保持有效。 */
} XFontFace;

/** @brief 初始化 XFontFace 基类对象。 */
void XFontFace_init(XFontFace* self);

/** @brief 初始化 XFontFace 类虚函数表。 */
XVtable* XFontFace_class_init(void);

/** @brief 通过 XClass 虚表反初始化基类对象。 */
#define XFontFace_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 通过 XClass 虚表删除堆对象。 */
#define XFontFace_delete_base(self) XClass_delete_base((XClass*)(self))

/**
 * @brief 注册一个具有静态存储期的字库 face。
 * @param face 已初始化的点阵或轮廓派生对象；注册表不取得其所有权。
 * @return 注册或同名替换成功返回 true，否则返回 false。
 */
bool XFontFace_register(XFontFace* face);

/**
 * @brief 按 XFont 属性解析实际字库 face。
 * @param font 字体属性对象；可为 NULL，此时使用默认字体。
 * @return 可用字库 face；没有匹配后端时返回 NULL。
 */
const XFontFace* XFont_face(const XFont* font);

/** @brief 查询 face 的后端种类。 */
XFontFaceKind XFontFace_kind(const XFontFace* self);

/** @brief 调用 face 的公共度量虚函数。 */
bool XFontFace_info_base(const XFontFace* self, const XFont* font,
                         XFontFaceInfo* info);

/** @brief 调用 face 的点阵字形虚函数。 */
bool XFontFace_loadBitmapGlyph_base(const XFontFace* self, const XFont* font,
                                    uint32_t codepoint, XFontGlyphDsc* dsc,
                                    unsigned char* out, size_t outSize);

/** @brief 调用 face 的轮廓字形虚函数。 */
bool XFontFace_loadOutlineGlyph_base(const XFontFace* self, const XFont* font,
                                     uint32_t codepoint,
                                     XFontOutlineGlyphMetrics* metrics,
                                     const XFontOutlineSink* sink);

/** @brief 调用 face 的点阵行跨度虚函数。 */
int XFontFace_bitmapGlyphRowBytes_base(const XFontFace* self,
                                       const XFont* font,
                                       const XFontGlyphDsc* dsc, int bpp);

/** @brief 从 LVGL 的 lv_font_t 描述生成 provider 初始化项。 */
#define XFONT_BITMAP_PROVIDER_FROM_LVGL(familyName, lvglFontPtr, glyphWidth, glyphRowBytes, glyphBpp) \
    { (familyName), \
      { (glyphWidth), (lvglFontPtr)->line_height, \
        (lvglFontPtr)->line_height - (lvglFontPtr)->base_line, \
        (lvglFontPtr)->base_line, (glyphRowBytes), (glyphBpp) }, \
      (const XFontBitmapData*)(lvglFontPtr)->dsc, NULL }

#ifdef __cplusplus
}
#endif

#endif /* XFONTFACE_H */
