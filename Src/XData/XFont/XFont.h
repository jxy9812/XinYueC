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
#include "XString.h"

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
    XClass   m_class;      /**< 继承的基类成员 */
    XString* m_family;     /**< 字体家族名称 */
    XString* m_styleName;  /**< 样式名称 */
    double   m_pointSizeF; /**< 点大小（浮点） */
    int      m_pixelSize;  /**< 像素大小（-1 表示未设置） */
    int      m_weight;     /**< 字重（XFont_Weight 枚举值） */
    int      m_style;      /**< 样式（XFont_Style 枚举值） */
    int      m_stretch;    /**< 拉伸（XFont_Stretch 枚举值） */
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
 * @brief      在堆上创建 XFont 实例
 * @return     指向新创建的 XFont 对象的指针，失败返回 NULL
 */
/**
 * @brief      在堆上创建 XFont 实例（含家族和大小）
 * @param family   字体家族名称
 * @param pointSize 点大小（-1 表示默认）
 * @param weight   字重（-1 表示默认）
 * @param italic   是否斜体
 * @return     指向新创建的 XFont 对象的指针，失败返回 NULL
 */
XFont* XFont_create_ex(XMemoryType memory, const char* family, int pointSize, int weight, bool italic);

/**
 * @brief      初始化 XFont 实例
 * @param self 待初始化的 XFont 对象指针
 */
void XFont_init(XFont* self);

/**
 * @brief      初始化 XFont 实例（含家族和大小）
 * @param self      待初始化的 XFont 对象指针
 * @param family    字体家族名称
 * @param pointSize 点大小（-1 表示默认）
 * @param weight    字重（-1 表示默认）
 * @param italic    是否斜体
 */
void XFont_init_ex(XFont* self, const char* family, int pointSize, int weight, bool italic);

/**
 * @brief      复制构造函数
 * @param self 目标 XFont 对象指针
 * @param other 源 XFont 对象指针
 */
void XFont_copy(XFont* self, const XFont* other);

/**
 * @brief      移动构造函数
 * @param self 目标 XFont 对象指针
 * @param other 源 XFont 对象指针（移动后源对象变为空）
 */
void XFont_move(XFont* self, XFont* other);

/**
 * @brief      释放 XFont 资源
 * @param self 待释放的 XFont 对象指针
 */
void XFont_deinit(XFont* self);

/**
 * @brief      在堆上删除 XFont 实例
 * @param self 待删除的 XFont 对象指针
 */
void XFont_delete(XFont* self);

/* ========== 虚函数调度 ========== */

void XFont_copy_base(XFont* dest, const XFont* src);
void XFont_move_base(XFont* dest, XFont* src);
void XFont_deinit_base(XFont* self);
void XFont_delete_base(XFont* self);

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
 * @param family 字体家族名称
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

/* ========== 点阵字体整倍缩放算法（内置位图字体共用小工具） ========== */

/**
 * @brief      读取字体像素字号，未设置（<=0）时回落默认值。
 * @param self 目标 XFont 对象指针；NULL 返回默认值。
 * @param defaultPixelSize 未设置字号时的回落像素高度。
 * @return 生效的像素字号。
 */
int XFont_bitmapPixelSize(const XFont* self, int defaultPixelSize);

/**
 * @brief      计算内置点阵字体的整倍缩放系数（最近邻）。
 * @details    给定基准字体高度与目标字号高度，返回
 *             scale = max(1, ceil(target / base))，使任何位图字体
 *             （8x16、6x13、12x24…）都可按统一算法整倍放大：
 *             有效高 = base x scale、有效宽 = 对应原始度量 x scale。
 * @param basePixelHeight 基准点阵字体高度（<=0 按 1 处理）。
 * @param targetPixelHeight 目标像素高度（<=0 视为 base 本身）。
 * @return 整倍缩放系数（>=1）。
 */
int XFont_bitmapScale(int basePixelHeight, int targetPixelHeight);

/**
 * @brief      计算整倍缩放后的尺寸。
 * @param baseSize 原始某一度量（字宽/行高/基线高/字间距…）。
 * @param scale    缩放系数（<1 按 1 处理）。
 * @return baseSize x max(1, scale)。
 */
int XFont_bitmapScaledSize(int baseSize, int scale);

/**
 * @brief      按字体像素字号计算键入内置位图字体的整倍缩放系数。
 * @param self 目标 XFont 对象指针；NULL 返回 1。
 * @param basePixelHeight 该位图字体的基准高度（如 16）。
 * @return 整倍缩放系数（>=1）。
 */
int XFont_bitmapScaleForFont(const XFont* self, int basePixelHeight);

/**
 * @brief      按字体像素字号计算内建位图字体的有效像素高。
 * @param self 目标 XFont 对象指针；NULL 返回 basePixelHeight。
 * @param basePixelHeight 该位图字体的基准高度（如 16）。
 * @return 有效像素高度 = basePixelHeight x scale。
 */
int XFont_bitmapHeightForFont(const XFont* self, int basePixelHeight);

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
