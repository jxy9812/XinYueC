/******************************************************************************
 * @file       XFont.h
 * @brief      XFont 字体类（对标 Qt 6.8 QFont）
 * @author     XinYueC 团队
 * @note       提供字体家族、大小、字重、样式、拉伸等属性管理
 ******************************************************************************/
#ifndef XFONT_H
#define XFONT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XFont_config.h"
#include "XString.h"

/** @brief XFont 点阵字库的行度量及缺省字形度量，单位均为像素。 */
typedef struct XFontBitmapInfo
{
    int m_width;       /**< 字形宽度；不得超过 XFONT_BITMAP_MAX_WIDTH。 */
    int m_height;      /**< 字形行数；不得超过 XFONT_BITMAP_MAX_HEIGHT。 */
    int m_ascent;      /**< 基线以上高度；范围为 0 到 m_height。 */
    int m_descent;     /**< 基线以下高度；范围为 0 到 m_height。 */
    int m_rowBytes;    /**< 每行字节数；由宽度和 bpp 决定。 */
    int m_bpp;         /**< 每像素位数；按 LVGL fmt_txt 支持 1/2/4/8bpp。 */
} XFontBitmapInfo;

/**
 * @brief LVGL fmt_txt 字形描述的无依赖镜像。
 * @details 字段名称、顺序和位宽与 LVGL 的
 *          lv_font_fmt_txt_glyph_dsc_t 保持一致，XFont 不需要包含 LVGL
 *          头文件。bitmap_index 指向 glyph_bitmap，adv_w 为 8.4 定点数。
 */
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
    /* Keep the enum member, alignment, and size identical to LVGL. */
    XFontCmapType type;
} XFontCmap;

/**
 * @brief LVGL fmt_txt 字体数据描述的无依赖镜像。
 * @details 可将 LVGL 生成的 glyph_bitmap、glyph_dsc 和 cmaps 指针直接
 *          填入该结构；bitmap_format 目前支持 LVGL 的 plain(0) 格式。
 */
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

/**
 * @brief XFont 点阵字库 provider。
 * @details m_family 为静态存储期的家族名；注册表只保存该指针，不复制
 *          字符串。m_data 非空时使用 LVGL fmt_txt 数据；m_loadGlyph 仅
 *          用于文件字库和需要动态取字形的后端。二者都不需要 LVGL 运行时。
 */
typedef struct XFontBitmapProvider
{
    const char* m_family;
    XFontBitmapInfo m_info;
    const XFontBitmapData* m_data;
    bool (*m_loadGlyph)(uint32_t cp, unsigned char* out, size_t outSize);
} XFontBitmapProvider;

/** @brief 把 LVGL 的 lv_font_t 描述转换为 XFont provider 初始化项。 */
#define XFONT_BITMAP_PROVIDER_FROM_LVGL(familyName, lvglFontPtr, glyphWidth, glyphRowBytes, glyphBpp) \
    { (familyName), \
      { (glyphWidth), (lvglFontPtr)->line_height, \
        (lvglFontPtr)->line_height - (lvglFontPtr)->base_line, \
        (lvglFontPtr)->base_line, (glyphRowBytes), (glyphBpp) }, \
      (const XFontBitmapData*)(lvglFontPtr)->dsc, NULL }

/* ========== XFont 虚函数表枚举 ========== */
XCLASS_DEFINE_BEGING(XFont)
XCLASS_DEFINE_EXTEND_END(XFont, XClass)

/* ========== 枚举定义（对标 Qt 6.8 QFont） ========== */

/**
 * @brief      字体样式提示枚举
 * @note       指定字体的风格偏好
 */
typedef enum XFont_StyleHint
{
    XFont_Helvetica = 0,        /**< 无衬线字体（Helvetica/SansSerif） */
    XFont_SansSerif = 0,        /**< 无衬线字体 */
    XFont_Times = 1,            /**< 衬线字体（Times/Serif） */
    XFont_Serif = 1,            /**< 衬线字体 */
    XFont_Courier = 2,          /**< 等宽字体（Courier/TypeWriter） */
    XFont_TypeWriter = 2,       /**< 打字机字体 */
    XFont_OldEnglish = 3,       /**< 古英语字体（Decorative） */
    XFont_Decorative = 3,       /**< 装饰字体 */
    XFont_System = 4,           /**< 系统字体 */
    XFont_AnyStyle = 5,         /**< 任意风格 */
    XFont_Cursive = 6,          /**< 手写体 */
    XFont_Monospace = 7,        /**< 等宽字体 */
    XFont_Fantasy = 8           /**< 幻想字体 */
} XFont_StyleHint;

/**
 * @brief      字体样式策略枚举
 */
typedef enum XFont_StyleStrategy
{
    XFont_PreferDefault          = 0x0001, /**< 默认偏好 */
    XFont_PreferBitmap           = 0x0002, /**< 优先位图 */
    XFont_PreferDevice           = 0x0004, /**< 优先设备字体 */
    XFont_PreferOutline          = 0x0008, /**< 优先轮廓字体 */
    XFont_ForceOutline           = 0x0010, /**< 强制轮廓字体 */
    XFont_PreferMatch            = 0x0020, /**< 优先匹配 */
    XFont_PreferQuality          = 0x0040, /**< 优先质量 */
    XFont_PreferAntialias        = 0x0080, /**< 优先抗锯齿 */
    XFont_NoAntialias            = 0x0100, /**< 无抗锯齿 */
    XFont_NoSubpixelAntialias    = 0x0800, /**< 无子像素抗锯齿 */
    XFont_PreferNoShaping        = 0x1000, /**< 优先不整形 */
    XFont_ContextFontMerging     = 0x2000, /**< 上下文字体合并 */
    XFont_PreferTypoLineMetrics  = 0x4000, /**< 优先排版行度量 */
    XFont_NoFontMerging          = 0x8000  /**< 禁止字体合并 */
} XFont_StyleStrategy;

/**
 * @brief      字体提示偏好枚举
 */
typedef enum XFont_HintingPreference
{
    XFont_PreferDefaultHinting   = 0, /**< 默认提示 */
    XFont_PreferNoHinting        = 1, /**< 无提示 */
    XFont_PreferVerticalHinting  = 2, /**< 垂直提示 */
    XFont_PreferFullHinting      = 3  /**< 完全提示 */
} XFont_HintingPreference;

/**
 * @brief      字体字重枚举
 */
typedef enum XFont_Weight
{
    XFont_Thin       = 100, /**< 极细 */
    XFont_ExtraLight = 200, /**< 特细 */
    XFont_Light      = 300, /**< 细体 */
    XFont_Normal     = 400, /**< 正常 */
    XFont_Medium     = 500, /**< 中等 */
    XFont_DemiBold   = 600, /**< 半粗 */
    XFont_Bold       = 700, /**< 粗体 */
    XFont_ExtraBold  = 800, /**< 特粗 */
    XFont_Black      = 900  /**< 极粗 */
} XFont_Weight;

/**
 * @brief      字体样式枚举
 */
typedef enum XFont_Style
{
    XFont_StyleNormal  = 0, /**< 正常 */
    XFont_StyleItalic  = 1, /**< 斜体 */
    XFont_StyleOblique = 2  /**< 倾斜 */
} XFont_Style;

/**
 * @brief      字体拉伸枚举
 */
typedef enum XFont_Stretch
{
    XFont_AnyStretch     = 0,   /**< 任意拉伸 */
    XFont_UltraCondensed = 50,  /**< 极浓缩 */
    XFont_ExtraCondensed = 62,  /**< 特浓缩 */
    XFont_Condensed      = 75,  /**< 浓缩 */
    XFont_SemiCondensed  = 87,  /**< 半浓缩 */
    XFont_Unstretched    = 100, /**< 正常 */
    XFont_SemiExpanded   = 112, /**< 半扩展 */
    XFont_Expanded       = 125, /**< 扩展 */
    XFont_ExtraExpanded  = 150, /**< 特扩展 */
    XFont_UltraExpanded  = 200  /**< 极扩展 */
} XFont_Stretch;

/**
 * @brief      大小写枚举
 */
typedef enum XFont_Capitalization
{
    XFont_MixedCase     = 0, /**< 混合大小写 */
    XFont_AllUppercase  = 1, /**< 全大写 */
    XFont_AllLowercase  = 2, /**< 全小写 */
    XFont_SmallCaps     = 3, /**< 小型大写 */
    XFont_Capitalize    = 4  /**< 首字母大写 */
} XFont_Capitalization;

/**
 * @brief      间距类型枚举
 */
typedef enum XFont_SpacingType
{
    XFont_PercentageSpacing = 0, /**< 百分比间距 */
    XFont_AbsoluteSpacing   = 1  /**< 绝对间距 */
} XFont_SpacingType;

/* ========== XFont 结构体 ========== */

/**
 * @brief      XFont 字体结构体（对标 Qt 6.8 QFont）
 * @note       继承自 XClass，包含字体家族、大小、字重等属性
 */
typedef struct XFont
{
    XClass   m_class;      /**< 第一个成员；由 XClass 管理，调用者禁止手工修改。 */
    XString* m_family;     /**< 字体家族名称；对象拥有，可为 NULL，不可直接修改。 */
    XString* m_styleName;  /**< 样式名称；对象拥有，可为 NULL，不可直接修改。 */
    double   m_pointSizeF; /**< 点大小（point）；由属性 API 修改，不限制为整数。 */
    int      m_pixelSize;  /**< 像素大小；<=0 表示未设置，由属性 API 修改。 */
    int      m_weight;     /**< 字重；通常为 100 到 900，遵循 XFont_Weight。 */
    int      m_style;      /**< 样式；取 XFont_Style 枚举值。 */
    int      m_stretch;    /**< 拉伸；取 XFont_Stretch 枚举值。 */
    uint32_t m_underline       : 1; /**< 是否有下划线 */
    uint32_t m_strikeOut       : 1; /**< 是否有删除线 */
    uint32_t m_overline        : 1; /**< 是否有上划线 */
    uint32_t m_fixedPitch      : 1; /**< 是否等宽字体 */
    uint32_t m_kerning         : 1; /**< 是否启用字距调整 */
    uint32_t m_capitalization  : 3; /**< 大小写（XFont_Capitalization） */
    uint32_t m_letterSpacing   : 1; /**< 是否有字母间距 */
    uint32_t m_wordSpacing     : 1; /**< 是否有单词间距 */
    uint32_t m_styleHint       : 4; /**< 样式提示（XFont_StyleHint） */
    uint32_t m_styleStrategy   : 16;/**< 样式策略（XFont_StyleStrategy） */
    uint32_t m_hintingPreference : 2; /**< 提示偏好（XFont_HintingPreference） */
    float    m_letterSpacingValue; /**< 字母间距值 */
    float    m_wordSpacingValue;   /**< 单词间距值 */
    int      m_letterSpacingType;  /**< 字母间距类型（XFont_SpacingType） */
    uint32_t m_resolveMask;        /**< 已解析属性掩码 */
} XFont;

/* ========== 虚函数表初始化 ========== */

/**
 * @brief      初始化 XFont 类的虚函数表
 * @return     指向初始化完成的 XVtable 的指针
 */
XVtable* XFont_class_init(void);

/* ========== 创建与初始化 ========== */

/**
 * @brief      在堆上创建 XFont 实例。
 * @details    新对象使用 XFONT_DEFAULT_* 配置的默认字体参数；调用方必须用
 *             XFont_delete_base 释放返回对象。
 * @return     指向新创建的 XFont 对象的指针，失败返回 NULL
 */
/**
 * @brief      在堆上创建 XFont 实例（含家族和大小）。
 * @param memory     对象使用的 XMemory 类型。
 * @param family   字体家族名称，或 LVGL --format bin 字库文件路径
 * @param pointSize 点大小（-1 表示默认）
 * @param weight   字重（-1 表示默认）
 * @param italic   是否斜体
 * @return     指向新创建的 XFont 对象的指针，失败返回 NULL
 */
XFont* XFont_create_ex(XMemoryType memory, const char* family, int pointSize, int weight, bool italic);

/**
 * @brief      初始化 XFont 实例。
 * @details    self 必须指向调用方提供的未初始化存储；重复初始化前应先调用
 *             XFont_deinit_base。默认参数来自 XFONT_DEFAULT_* 配置。
 * @param self 待初始化的 XFont 对象指针；NULL 时不执行任何操作。
 * @return     无；self 为 NULL 时对象状态不变。
 */
void XFont_init(XFont* self);

/**
 * @brief      初始化 XFont 实例（含家族和大小）。
 * @param self      待初始化的 XFont 对象指针
 * @param family    字体家族名称；NULL 使用 XFONT_DEFAULT_FAMILY，不取得调用者所有权。
 * @param pointSize 点大小（<=0 使用 XFONT_DEFAULT_POINT_SIZE）。
 * @param weight    字重（<=0 使用 XFONT_DEFAULT_WEIGHT）。
 * @param italic    是否斜体；true 时启用，false 使用 XFONT_DEFAULT_ITALIC。
 * @return          无；参数非法时保持已初始化对象的默认值。
 */
void XFont_init_ex(XFont* self, const char* family, int pointSize, int weight, bool italic);

/* ========== XClass 生命周期与虚函数调度 ========== */

/**
 * @brief 深拷贝字体资源到目标对象。
 * @param self 目标对象；未初始化时虚函数会先调用 XFont_init。
 * @param other 源对象；只读借用，不取得所有权。
 * @return 无；self 与 other 相同或任一指针为 NULL 时不执行。
 */
#define XFont_copy_base XClass_copy_base
/**
 * @brief 转移字体资源到目标对象并清空源对象。
 * @param self 目标对象；未初始化时虚函数会先调用 XFont_init。
 * @param other 源对象；移动后仍需调用 XFont_deinit_base，家族和样式指针为空。
 * @return 无；self 与 other 相同或任一指针为 NULL 时不执行。
 */
#define XFont_move_base XClass_move_base
/**
 * @brief 反初始化字体对象并释放其拥有的字符串资源。
 * @param self 已由 XFont_init 初始化的栈对象；NULL 时不执行。
 * @return 无；反初始化后必须重新 init 才能使用。
 */
#define XFont_deinit_base XClass_deinit_base
/**
 * @brief 反初始化并释放堆上的 XFont 对象。
 * @param self 由 XFont_create_ex 创建的堆对象；NULL 时不执行。
 * @return 无；不能用于栈对象。
 */
#define XFont_delete_base XClass_delete_base

/* ========== 属性访问 ========== */

/**
 * @brief      获取字体家族名称
 * @param self 目标 XFont 对象指针
 * @return     字体家族名称字符串
 */
const char* XFont_family(const XFont* self);

/**
 * @brief      设置字体家族名称
 * @param self   目标 XFont 对象指针
 * @param family 字体家族名称（例如 "XFont8x16" 使用已注册字库）；若名称未注册，
 *               则按 XFONT_EXTERNAL_FONT_DIR/<family>.bin 查找外挂字库；也可
 *               传入外挂字库完整路径（可带或不带 ".bin" 后缀）。
 */
void XFont_setFamily(XFont* self, const char* family);

/**
 * @brief      获取样式名称
 * @param self 目标 XFont 对象指针
 * @return     样式名称字符串
 */
const char* XFont_styleName(const XFont* self);

/**
 * @brief      设置样式名称
 * @param self      目标 XFont 对象指针
 * @param styleName 样式名称
 */
void XFont_setStyleName(XFont* self, const char* styleName);

/**
 * @brief      获取点大小（整数）
 * @param self 目标 XFont 对象指针
 * @return     点大小（-1 表示未设置）
 */
int XFont_pointSize(const XFont* self);

/**
 * @brief      设置点大小（整数）
 * @param self      目标 XFont 对象指针
 * @param pointSize 点大小
 */
void XFont_setPointSize(XFont* self, int pointSize);

/**
 * @brief      获取点大小（浮点）
 * @param self 目标 XFont 对象指针
 * @return     点大小（浮点）
 */
double XFont_pointSizeF(const XFont* self);

/**
 * @brief      设置点大小（浮点）
 * @param self      目标 XFont 对象指针
 * @param pointSize 点大小（浮点）
 */
void XFont_setPointSizeF(XFont* self, double pointSize);

/**
 * @brief      获取像素大小
 * @param self 目标 XFont 对象指针
 * @return     像素大小（-1 表示未设置）
 */
int XFont_pixelSize(const XFont* self);

/**
 * @brief      设置像素大小
 * @param self      目标 XFont 对象指针
 * @param pixelSize 像素大小
 */
void XFont_setPixelSize(XFont* self, int pixelSize);

/**
 * @brief 查询 XFont 当前选择的点阵字库。
 * @param self 字体对象；NULL 使用默认字库。
 * @param info 调用方提供的度量输出空间。
 * @return 找到可用字库返回 true，否则返回 false，info 保持不变。
 */
bool XFont_bitmapFontInfo(const XFont* self, XFontBitmapInfo* info);

/**
 * @brief 查找字形并按需读取其 LVGL 紧凑位图。
 * @details dsc 必须非空；out 可为 NULL，此时只查询 descriptor，不复制
 *          位图。LVGL 的连续 bit 流会被展开为每行对齐的输出，行跨度为
 *          ceil(box_w * bpp / 8)，不是字体的最大宽度。这样查询和加载共用
 *          一个入口，避免重复 API。
 */
bool XFont_bitmapLoadGlyph(const XFont* self, uint32_t cp,
                           XFontGlyphDsc* dsc, unsigned char* out,
                           size_t outSize);

/** @brief 计算 LVGL bpp 点阵字形每行的存储字节数。 */
int XFont_bitmapGlyphRowBytes(const XFontGlyphDsc* dsc, int bpp);

/**
 * @brief 注册一个静态点阵字库 provider。
 * @param provider provider 描述；其中 family 字符串必须保持有效。
 * @return 注册或替换成功返回 true；参数、度量或容量非法返回 false。
 * @note 同名 provider 会替换旧注册；调用应发生在多线程启动前。
 */
bool XFont_registerBitmapProvider(const XFontBitmapProvider* provider);

/* ========== 点阵字体显示尺寸算法（内置位图字体共用小工具） ========== */

/**
 * @brief      读取字体像素字号，未设置（<=0）时回落默认值。
 * @param self 目标 XFont 对象指针；NULL 返回默认值。
 * @param defaultPixelSize 未设置字号时的回落像素高度。
 * @return 生效的像素字号。
 */
int XFont_bitmapPixelSize(const XFont* self, int defaultPixelSize);

/**
 * @brief      获取字重
 * @param self 目标 XFont 对象指针
 * @return     字重枚举值
 */
int XFont_weight(const XFont* self);

/**
 * @brief      设置字重
 * @param self   目标 XFont 对象指针
 * @param weight 字重枚举值
 */
void XFont_setWeight(XFont* self, int weight);

/**
 * @brief      判断是否粗体
 * @param self 目标 XFont 对象指针
 * @return     粗体返回 true
 */
bool XFont_bold(const XFont* self);

/**
 * @brief      设置粗体
 * @param self  目标 XFont 对象指针
 * @param bold  是否粗体
 */
void XFont_setBold(XFont* self, bool bold);

/**
 * @brief      获取字体样式
 * @param self 目标 XFont 对象指针
 * @return     字体样式枚举值
 */
int XFont_style(const XFont* self);

/**
 * @brief      设置字体样式
 * @param self  目标 XFont 对象指针
 * @param style 字体样式枚举值
 */
void XFont_setStyle(XFont* self, int style);

/**
 * @brief      判断是否斜体
 * @param self 目标 XFont 对象指针
 * @return     斜体返回 true
 */
bool XFont_italic(const XFont* self);

/**
 * @brief      设置斜体
 * @param self    目标 XFont 对象指针
 * @param italic  是否斜体
 */
void XFont_setItalic(XFont* self, bool italic);

/**
 * @brief      判断是否有下划线
 * @param self 目标 XFont 对象指针
 * @return     有下划线返回 true
 */
bool XFont_underline(const XFont* self);

/**
 * @brief      设置下划线
 * @param self      目标 XFont 对象指针
 * @param underline 是否有下划线
 */
void XFont_setUnderline(XFont* self, bool underline);

/**
 * @brief      判断是否有删除线
 * @param self 目标 XFont 对象指针
 * @return     有删除线返回 true
 */
bool XFont_strikeOut(const XFont* self);

/**
 * @brief      设置删除线
 * @param self      目标 XFont 对象指针
 * @param strikeOut 是否有删除线
 */
void XFont_setStrikeOut(XFont* self, bool strikeOut);

/**
 * @brief      判断是否有上划线
 * @param self 目标 XFont 对象指针
 * @return     有上划线返回 true
 */
bool XFont_overline(const XFont* self);

/**
 * @brief      设置上划线
 * @param self     目标 XFont 对象指针
 * @param overline 是否有上划线
 */
void XFont_setOverline(XFont* self, bool overline);

/**
 * @brief      判断是否等宽字体
 * @param self 目标 XFont 对象指针
 * @return     等宽字体返回 true
 */
bool XFont_fixedPitch(const XFont* self);

/**
 * @brief      设置等宽字体
 * @param self       目标 XFont 对象指针
 * @param fixedPitch 是否等宽字体
 */
void XFont_setFixedPitch(XFont* self, bool fixedPitch);

/**
 * @brief      判断是否启用字距调整
 * @param self 目标 XFont 对象指针
 * @return     启用返回 true
 */
bool XFont_kerning(const XFont* self);

/**
 * @brief      设置字距调整
 * @param self    目标 XFont 对象指针
 * @param kerning 是否启用字距调整
 */
void XFont_setKerning(XFont* self, bool kerning);

/**
 * @brief      获取大小写
 * @param self 目标 XFont 对象指针
 * @return     大小写枚举值
 */
int XFont_capitalization(const XFont* self);

/**
 * @brief      设置大小写
 * @param self           目标 XFont 对象指针
 * @param capitalization 大小写枚举值
 */
void XFont_setCapitalization(XFont* self, int capitalization);

/**
 * @brief      获取拉伸
 * @param self 目标 XFont 对象指针
 * @return     拉伸枚举值
 */
int XFont_stretch(const XFont* self);

/**
 * @brief      设置拉伸
 * @param self    目标 XFont 对象指针
 * @param stretch 拉伸枚举值
 */
void XFont_setStretch(XFont* self, int stretch);

/**
 * @brief      获取样式提示
 * @param self 目标 XFont 对象指针
 * @return     样式提示枚举值
 */
int XFont_styleHint(const XFont* self);

/**
 * @brief      设置样式提示
 * @param self      目标 XFont 对象指针
 * @param styleHint 样式提示枚举值
 */
void XFont_setStyleHint(XFont* self, int styleHint);

/**
 * @brief      获取样式策略
 * @param self 目标 XFont 对象指针
 * @return     样式策略枚举值
 */
int XFont_styleStrategy(const XFont* self);

/**
 * @brief      设置样式策略
 * @param self          目标 XFont 对象指针
 * @param styleStrategy 样式策略枚举值
 */
void XFont_setStyleStrategy(XFont* self, int styleStrategy);

/**
 * @brief      获取提示偏好
 * @param self 目标 XFont 对象指针
 * @return     提示偏好枚举值
 */
int XFont_hintingPreference(const XFont* self);

/**
 * @brief      设置提示偏好
 * @param self               目标 XFont 对象指针
 * @param hintingPreference  提示偏好枚举值
 */
void XFont_setHintingPreference(XFont* self, int hintingPreference);

/**
 * @brief      获取字母间距
 * @param self 目标 XFont 对象指针
 * @return     字母间距值
 */
float XFont_letterSpacing(const XFont* self);

/**
 * @brief      设置字母间距
 * @param self  目标 XFont 对象指针
 * @param spacing 间距值
 */
void XFont_setLetterSpacing(XFont* self, float spacing);

/**
 * @brief      获取字母间距类型
 * @param self 目标 XFont 对象指针
 * @return     间距类型枚举值
 */
int XFont_letterSpacingType(const XFont* self);

/**
 * @brief      设置字母间距类型
 * @param self 目标 XFont 对象指针
 * @param type 间距类型枚举值
 */
void XFont_setLetterSpacingType(XFont* self, int type);

/**
 * @brief      获取单词间距
 * @param self 目标 XFont 对象指针
 * @return     单词间距值
 */
float XFont_wordSpacing(const XFont* self);

/**
 * @brief      设置单词间距
 * @param self    目标 XFont 对象指针
 * @param spacing 单词间距值
 */
void XFont_setWordSpacing(XFont* self, float spacing);

/**
 * @brief      获取解析掩码
 * @param self 目标 XFont 对象指针
 * @return     解析掩码值
 */
uint32_t XFont_resolveMask(const XFont* self);

/**
 * @brief      设置解析掩码
 * @param self 目标 XFont 对象指针
 * @param mask 解析掩码值
 */
void XFont_setResolveMask(XFont* self, uint32_t mask);

/* ========== 工具方法 ========== */

/**
 * @brief      判断两个字体是否相等
 * @param a 字体 A
 * @param b 字体 B
 * @return     相等返回 true
 */
bool XFont_equals(const XFont* a, const XFont* b);

/**
 * @brief      转换为字符串表示
 * @param self 目标 XFont 对象指针
 * @return     指向新 XString 的指针，调用者负责释放
 */
XString* XFont_toString(const XFont* self);

/**
 * @brief      从字符串解析字体
 * @param str 字体字符串
 * @param out 输出 XFont 对象指针
 * @return     解析成功返回 true
 */
bool XFont_fromString(XFont* out, const char* str);

/**
 * @brief      交换两个字体对象
 * @param a 字体 A
 * @param b 字体 B
 */
void XFont_swap(XFont* a, XFont* b);

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
#undef XFont_create
#define XFont_create() \
	XFont_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, -1, -1, false)

#endif /* XFONT_H */
