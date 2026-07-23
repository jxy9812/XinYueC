/******************************************************************************
 * @file       XFormat.h
 * @brief      XFormat 单元格格式类（对标 QXlsx::Format）
 * @author     XinYueC 团队
 * @note       提供单元格格式管理，包括数字格式、字体、边框、填充、对齐和保护属性。
 *             使用属性映射存储格式属性，支持 QXlsx::Format 全部功能对齐。
 ******************************************************************************/
#ifndef XFORMAT_H
#define XFORMAT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XString.h"
#include "XByteArray.h"
#include "XColor.h"
#include "XFont.h"
#include "XMap.h"
#include "XSharedData.h"

/* ========== 枚举定义（对标 QXlsx::Format） ========== */

/**
 * @brief      字体脚本枚举
 */
typedef enum XFormat_FontScript
{
    XFormat_FontScriptNormal = 0,  /**< 正常脚本 */
    XFormat_FontScriptSuper = 1,   /**< 上标 */
    XFormat_FontScriptSub = 2      /**< 下标 */
} XFormat_FontScript;

/**
 * @brief      字体下划线枚举
 */
typedef enum XFormat_FontUnderline
{
    XFormat_FontUnderlineNone = 0,              /**< 无下划线 */
    XFormat_FontUnderlineSingle = 1,            /**< 单下划线 */
    XFormat_FontUnderlineDouble = 2,            /**< 双下划线 */
    XFormat_FontUnderlineSingleAccounting = 3,  /**< 会计单下划线 */
    XFormat_FontUnderlineDoubleAccounting = 4   /**< 会计双下划线 */
} XFormat_FontUnderline;

/**
 * @brief      水平对齐枚举
 */
typedef enum XFormat_HorizontalAlignment
{
    XFormat_AlignHGeneral = 0,      /**< 常规对齐 */
    XFormat_AlignLeft = 1,          /**< 左对齐 */
    XFormat_AlignHCenter = 2,       /**< 水平居中 */
    XFormat_AlignRight = 3,         /**< 右对齐 */
    XFormat_AlignHFill = 4,         /**< 填充 */
    XFormat_AlignHJustify = 5,      /**< 两端对齐 */
    XFormat_AlignHMerge = 6,        /**< 跨列居中 */
    XFormat_AlignHDistributed = 7   /**< 分散对齐 */
} XFormat_HorizontalAlignment;

/**
 * @brief      垂直对齐枚举
 */
typedef enum XFormat_VerticalAlignment
{
    XFormat_AlignTop = 0,           /**< 顶部对齐 */
    XFormat_AlignVCenter = 1,       /**< 垂直居中 */
    XFormat_AlignBottom = 2,        /**< 底部对齐 */
    XFormat_AlignVJustify = 3,      /**< 垂直两端对齐 */
    XFormat_AlignVDistributed = 4   /**< 垂直分散对齐 */
} XFormat_VerticalAlignment;

/**
 * @brief      边框样式枚举
 */
typedef enum XFormat_BorderStyle
{
    XFormat_BorderNone = 0,                /**< 无边框 */
    XFormat_BorderThin = 1,                /**< 细边框 */
    XFormat_BorderMedium = 2,              /**< 中等边框 */
    XFormat_BorderDashed = 3,              /**< 虚线边框 */
    XFormat_BorderDotted = 4,              /**< 点线边框 */
    XFormat_BorderThick = 5,               /**< 粗边框 */
    XFormat_BorderDouble = 6,              /**< 双线边框 */
    XFormat_BorderHair = 7,                /**< 极细边框 */
    XFormat_BorderMediumDashed = 8,        /**< 中等虚线 */
    XFormat_BorderDashDot = 9,             /**< 点划线 */
    XFormat_BorderMediumDashDot = 10,      /**< 中等点划线 */
    XFormat_BorderDashDotDot = 11,         /**< 双点划线 */
    XFormat_BorderMediumDashDotDot = 12,   /**< 中等双点划线 */
    XFormat_BorderSlantDashDot = 13        /**< 倾斜点划线 */
} XFormat_BorderStyle;

/**
 * @brief      对角线边框类型枚举
 */
typedef enum XFormat_DiagonalBorderType
{
    XFormat_DiagonalBorderNone = 0,  /**< 无对角线 */
    XFormat_DiagonalBorderDown = 1,  /**< 对角线向下（\） */
    XFormat_DiagonalBorderUp = 2,    /**< 对角线向上（/） */
    XFormat_DiagonalBorderBoth = 3   /**< 双向对角线（X） */
} XFormat_DiagonalBorderType;

/**
 * @brief      填充图案枚举
 */
typedef enum XFormat_FillPattern
{
    XFormat_PatternNone = 0,                  /**< 无填充 */
    XFormat_PatternSolid = 1,                 /**< 实心填充 */
    XFormat_PatternMediumGray = 2,            /**< 中灰色 */
    XFormat_PatternDarkGray = 3,              /**< 深灰色 */
    XFormat_PatternLightGray = 4,             /**< 浅灰色 */
    XFormat_PatternDarkHorizontal = 5,        /**< 深色水平条纹 */
    XFormat_PatternDarkVertical = 6,          /**< 深色垂直条纹 */
    XFormat_PatternDarkDown = 7,              /**< 深色向下斜纹 */
    XFormat_PatternDarkUp = 8,                /**< 深色向上斜纹 */
    XFormat_PatternDarkGrid = 9,              /**< 深色网格 */
    XFormat_PatternDarkTrellis = 10,          /**< 深色格子 */
    XFormat_PatternLightHorizontal = 11,      /**< 浅色水平条纹 */
    XFormat_PatternLightVertical = 12,        /**< 浅色垂直条纹 */
    XFormat_PatternLightDown = 13,            /**< 浅色向下斜纹 */
    XFormat_PatternLightUp = 14,              /**< 浅色向上斜纹 */
    XFormat_PatternLightTrellis = 15,         /**< 浅色格子 */
    XFormat_PatternGray125 = 16,              /**< 12.5%灰度 */
    XFormat_PatternGray0625 = 17,             /**< 6.25%灰度 */
    XFormat_PatternLightGrid = 18             /**< 浅色网格 */
} XFormat_FillPattern;

/* ========== 格式属性 ID 枚举（内部使用） ========== */

/**
 * @brief      格式属性 ID 枚举
 * @note       内部使用，用于属性映射的键
 */
typedef enum XFormat_PropertyId
{
    XFormat_P_NumFmt_Id = 1,
    XFormat_P_NumFmt_FormatCode,
    /* 字体属性 */
    XFormat_P_Font_Size,
    XFormat_P_Font_Italic,
    XFormat_P_Font_StrikeOut,
    XFormat_P_Font_Color,
    XFormat_P_Font_Bold,
    XFormat_P_Font_Script,
    XFormat_P_Font_Underline,
    XFormat_P_Font_Outline,
    XFormat_P_Font_Shadow,
    XFormat_P_Font_Name,
    XFormat_P_Font_Family,
    XFormat_P_Font_Charset,
    XFormat_P_Font_Scheme,
    XFormat_P_Font_Condense,
    XFormat_P_Font_Extend,
    /* 边框属性 */
    XFormat_P_Border_LeftStyle,
    XFormat_P_Border_RightStyle,
    XFormat_P_Border_TopStyle,
    XFormat_P_Border_BottomStyle,
    XFormat_P_Border_DiagonalStyle,
    XFormat_P_Border_LeftColor,
    XFormat_P_Border_RightColor,
    XFormat_P_Border_TopColor,
    XFormat_P_Border_BottomColor,
    XFormat_P_Border_DiagonalColor,
    XFormat_P_Border_DiagonalType,
    /* 填充属性 */
    XFormat_P_Fill_Pattern,
    XFormat_P_Fill_BgColor,
    XFormat_P_Fill_FgColor,
    /* 对齐属性 */
    XFormat_P_Alignment_AlignH,
    XFormat_P_Alignment_AlignV,
    XFormat_P_Alignment_Wrap,
    XFormat_P_Alignment_Rotation,
    XFormat_P_Alignment_Indent,
    XFormat_P_Alignment_ShinkToFit,
    /* 保护属性 */
    XFormat_P_Protection_Locked,
    XFormat_P_Protection_Hidden
} XFormat_PropertyId;

/* ========== XFormat 结构体 ========== */

/**
 * @brief      XFormat 单元格格式结构体
 * @note       使用属性映射存储格式属性，支持 QXlsx::Format 全部功能对齐。
 *             包含数字格式、字体、边框、填充、对齐和保护等属性。
 */
typedef struct XFormat
{
    XMap* m_properties;         /**< 属性映射（int->XVariant） */
    int m_fontIndex;            /**< 字体索引 */
    int m_borderIndex;          /**< 边框索引 */
    int m_fillIndex;            /**< 填充索引 */
    int m_xfIndex;              /**< 格式索引 */
    int m_dxfIndex;             /**< 差分格式索引 */
    int m_theme;                /**< 主题颜色索引 */
    bool m_fontIndexValid;      /**< 字体索引是否有效 */
    bool m_borderIndexValid;    /**< 边框索引是否有效 */
    bool m_fillIndexValid;      /**< 填充索引是否有效 */
    bool m_xfIndexValid;        /**< 格式索引是否有效 */
    bool m_dxfIndexValid;       /**< 差分格式索引是否有效 */
    bool m_isDxfFormat;         /**< 是否为差分格式 */
    bool m_dirty;               /**< 是否需要重新生成键 */
} XFormat;

/* ========== 创建与初始化 ========== */

/**
 * @brief      创建一个空的 XFormat 对象
 * @return     指向新创建的 XFormat 的指针，失败返回 NULL
 */
XFormat* XFormat_create(void);

/**
 * @brief      复制 XFormat 对象
 * @param self 目标指针
 * @param other 源指针
 */
void XFormat_copy(XFormat* self, const XFormat* other);

/**
 * @brief      在堆上删除 XFormat 实例
 * @param self 待删除的指针
 */
void XFormat_delete(XFormat* self);

/* ========== 数字格式 ========== */

int XFormat_numberFormatIndex(const XFormat* self);
void XFormat_setNumberFormatIndex(XFormat* self, int format);
const char* XFormat_numberFormat(const XFormat* self);
void XFormat_setNumberFormat(XFormat* self, const char* format);
void XFormat_setNumberFormat_ex(XFormat* self, int id, const char* format);
bool XFormat_isDateTimeFormat(const XFormat* self);

/* ========== 字体属性 ========== */

int XFormat_fontSize(const XFormat* self);
void XFormat_setFontSize(XFormat* self, int size);
bool XFormat_fontItalic(const XFormat* self);
void XFormat_setFontItalic(XFormat* self, bool italic);
bool XFormat_fontStrikeOut(const XFormat* self);
void XFormat_setFontStrikeOut(XFormat* self, bool strikeOut);
XColor XFormat_fontColor(const XFormat* self);
void XFormat_setFontColor(XFormat* self, const XColor* color);
bool XFormat_fontBold(const XFormat* self);
void XFormat_setFontBold(XFormat* self, bool bold);
XFormat_FontScript XFormat_fontScript(const XFormat* self);
void XFormat_setFontScript(XFormat* self, XFormat_FontScript script);
XFormat_FontUnderline XFormat_fontUnderline(const XFormat* self);
void XFormat_setFontUnderline(XFormat* self, XFormat_FontUnderline underline);
bool XFormat_fontOutline(const XFormat* self);
void XFormat_setFontOutline(XFormat* self, bool outline);
const char* XFormat_fontName(const XFormat* self);
void XFormat_setFontName(XFormat* self, const char* name);
XFont* XFormat_font(const XFormat* self);
void XFormat_setFont(XFormat* self, const XFont* font);

/* ========== 对齐属性 ========== */

XFormat_HorizontalAlignment XFormat_horizontalAlignment(const XFormat* self);
void XFormat_setHorizontalAlignment(XFormat* self, XFormat_HorizontalAlignment align);
XFormat_VerticalAlignment XFormat_verticalAlignment(const XFormat* self);
void XFormat_setVerticalAlignment(XFormat* self, XFormat_VerticalAlignment align);
bool XFormat_textWrap(const XFormat* self);
void XFormat_setTextWrap(XFormat* self, bool textWrap);
int XFormat_rotation(const XFormat* self);
void XFormat_setRotation(XFormat* self, int rotation);
int XFormat_indent(const XFormat* self);
void XFormat_setIndent(XFormat* self, int indent);
bool XFormat_shrinkToFit(const XFormat* self);
void XFormat_setShrinkToFit(XFormat* self, bool shrink);

/* ========== 边框属性 ========== */

void XFormat_setBorderStyle(XFormat* self, XFormat_BorderStyle style);
void XFormat_setBorderColor(XFormat* self, const XColor* color);
XFormat_BorderStyle XFormat_leftBorderStyle(const XFormat* self);
void XFormat_setLeftBorderStyle(XFormat* self, XFormat_BorderStyle style);
XColor XFormat_leftBorderColor(const XFormat* self);
void XFormat_setLeftBorderColor(XFormat* self, const XColor* color);
XFormat_BorderStyle XFormat_rightBorderStyle(const XFormat* self);
void XFormat_setRightBorderStyle(XFormat* self, XFormat_BorderStyle style);
XColor XFormat_rightBorderColor(const XFormat* self);
void XFormat_setRightBorderColor(XFormat* self, const XColor* color);
XFormat_BorderStyle XFormat_topBorderStyle(const XFormat* self);
void XFormat_setTopBorderStyle(XFormat* self, XFormat_BorderStyle style);
XColor XFormat_topBorderColor(const XFormat* self);
void XFormat_setTopBorderColor(XFormat* self, const XColor* color);
XFormat_BorderStyle XFormat_bottomBorderStyle(const XFormat* self);
void XFormat_setBottomBorderStyle(XFormat* self, XFormat_BorderStyle style);
XColor XFormat_bottomBorderColor(const XFormat* self);
void XFormat_setBottomBorderColor(XFormat* self, const XColor* color);
XFormat_BorderStyle XFormat_diagonalBorderStyle(const XFormat* self);
void XFormat_setDiagonalBorderStyle(XFormat* self, XFormat_BorderStyle style);
XFormat_DiagonalBorderType XFormat_diagonalBorderType(const XFormat* self);
void XFormat_setDiagonalBorderType(XFormat* self, XFormat_DiagonalBorderType type);
XColor XFormat_diagonalBorderColor(const XFormat* self);
void XFormat_setDiagonalBorderColor(XFormat* self, const XColor* color);

/* ========== 填充属性 ========== */

XFormat_FillPattern XFormat_fillPattern(const XFormat* self);
void XFormat_setFillPattern(XFormat* self, XFormat_FillPattern pattern);
XColor XFormat_patternForegroundColor(const XFormat* self);
void XFormat_setPatternForegroundColor(XFormat* self, const XColor* color);
XColor XFormat_patternBackgroundColor(const XFormat* self);
void XFormat_setPatternBackgroundColor(XFormat* self, const XColor* color);

/* ========== 保护属性 ========== */

bool XFormat_locked(const XFormat* self);
void XFormat_setLocked(XFormat* self, bool locked);
bool XFormat_hidden(const XFormat* self);
void XFormat_setHidden(XFormat* self, bool hidden);

/* ========== 格式操作 ========== */

/**
 * @brief      合并格式（将 modifier 中设置的属性应用到当前格式）
 * @param self     目标格式
 * @param modifier 源格式
 */
void XFormat_mergeFormat(XFormat* self, const XFormat* modifier);

/**
 * @brief      判断格式是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_isValid(const XFormat* self);

/**
 * @brief      判断格式是否为空（无属性设置）
 * @param self 指针
 * @return     为空返回 true
 */
bool XFormat_isEmpty(const XFormat* self);

/* ========== 属性访问（通用） ========== */

/**
 * @brief      获取属性值
 * @param self         指针
 * @param propertyId   属性 ID
 * @param defaultValue 默认值
 * @return     属性值
 */
void* XFormat_property(const XFormat* self, int propertyId);

/**
 * @brief      设置属性值
 * @param self        指针
 * @param propertyId  属性 ID
 * @param value       值
 */
void XFormat_setProperty(XFormat* self, int propertyId, void* value);

/**
 * @brief      清除属性
 * @param self        指针
 * @param propertyId  属性 ID
 */
void XFormat_clearProperty(XFormat* self, int propertyId);

/**
 * @brief      判断是否有指定属性
 * @param self        指针
 * @param propertyId  属性 ID
 * @return     有返回 true
 */
bool XFormat_hasProperty(const XFormat* self, int propertyId);

/* ========== 键/索引管理 ========== */

bool XFormat_hasNumFmtData(const XFormat* self);
bool XFormat_hasFontData(const XFormat* self);
bool XFormat_hasFillData(const XFormat* self);
bool XFormat_hasBorderData(const XFormat* self);
bool XFormat_hasAlignmentData(const XFormat* self);
bool XFormat_hasProtectionData(const XFormat* self);

bool XFormat_fontIndexValid(const XFormat* self);
int XFormat_fontIndex(const XFormat* self);
bool XFormat_borderIndexValid(const XFormat* self);
int XFormat_borderIndex(const XFormat* self);
bool XFormat_fillIndexValid(const XFormat* self);
int XFormat_fillIndex(const XFormat* self);
bool XFormat_xfIndexValid(const XFormat* self);
int XFormat_xfIndex(const XFormat* self);
bool XFormat_dxfIndexValid(const XFormat* self);
int XFormat_dxfIndex(const XFormat* self);

void XFormat_setFontIndex(XFormat* self, int index);
void XFormat_setBorderIndex(XFormat* self, int index);
void XFormat_setFillIndex(XFormat* self, int index);
void XFormat_setXfIndex(XFormat* self, int index);
void XFormat_setDxfIndex(XFormat* self, int index);

#ifdef __cplusplus
}
#endif
#endif /* XFORMAT_H */
