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
    XMap* m_properties;         /**< 属性映射（int->
XVariant） */
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

/**
 * @brief      获取数字格式索引
 * @param self 指针
 * @return     格式索引
 */
int XFormat_numberFormatIndex(const XFormat* self);

/**
 * @brief      设置数字格式索引
 * @param self   指针
 * @param format 格式索引
 */
void XFormat_setNumberFormatIndex(XFormat* self, int format);

/**
 * @brief      获取数字格式字符串
 * @param self 指针
 * @return     格式字符串
 */
const XString* XFormat_numberFormat(const XFormat* self);

/**
 * @brief      设置数字格式
 * @param self   指针
 * @param format 格式字符串
 */
void XFormat_setNumberFormat(XFormat* self, const XString* format);

/**
 * @brief      设置数字格式（带ID）
 * @param self   指针
 * @param id    格式ID
 * @param format 格式字符串
 */
void XFormat_setNumberFormat_ex(XFormat* self, int id, const XString* format);

/**
 * @brief      判断是否为日期时间格式
 * @param self 指针
 * @return     是日期时间格式返回 true
 */
bool XFormat_isDateTimeFormat(const XFormat* self);

/* ========== 字体属性 ========== */

/**
 * @brief      获取字体大小
 * @param self 指针
 * @return     字体大小
 */
int XFormat_fontSize(const XFormat* self);

/**
 * @brief      设置字体大小
 * @param self  指针
 * @param size  字体大小
 */
void XFormat_setFontSize(XFormat* self, int size);

/**
 * @brief      获取斜体状态
 * @param self 指针
 * @return     斜体返回 true
 */
bool XFormat_fontItalic(const XFormat* self);

/**
 * @brief      设置斜体
 * @param self   指针
 * @param italic 是否斜体
 */
void XFormat_setFontItalic(XFormat* self, bool italic);

/**
 * @brief      获取删除线状态
 * @param self 指针
 * @return     删除线返回 true
 */
bool XFormat_fontStrikeOut(const XFormat* self);

/**
 * @brief      设置删除线
 * @param self      指针
 * @param strikeOut 是否删除线
 */
void XFormat_setFontStrikeOut(XFormat* self, bool strikeOut);

/**
 * @brief      获取字体颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_fontColor(const XFormat* self);

/**
 * @brief      设置字体颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setFontColor(XFormat* self, const XColor* color);

/**
 * @brief      获取粗体状态
 * @param self 指针
 * @return     粗体返回 true
 */
bool XFormat_fontBold(const XFormat* self);

/**
 * @brief      设置粗体
 * @param self  指针
 * @param bold  是否粗体
 */
void XFormat_setFontBold(XFormat* self, bool bold);

/**
 * @brief      获取字体脚本
 * @param self 指针
 * @return     字体脚本
 */
XFormat_FontScript XFormat_fontScript(const XFormat* self);

/**
 * @brief      设置字体脚本
 * @param self   指针
 * @param script 脚本类型
 */
void XFormat_setFontScript(XFormat* self, XFormat_FontScript script);

/**
 * @brief      获取字体下划线
 * @param self 指针
 * @return     下划线类型
 */
XFormat_FontUnderline XFormat_fontUnderline(const XFormat* self);

/**
 * @brief      设置字体下划线
 * @param self      指针
 * @param underline 下划线类型
 */
void XFormat_setFontUnderline(XFormat* self, XFormat_FontUnderline underline);

/**
 * @brief      获取轮廓状态
 * @param self 指针
 * @return     轮廓返回 true
 */
bool XFormat_fontOutline(const XFormat* self);

/**
 * @brief      设置轮廓
 * @param self   指针
 * @param outline 是否轮廓
 */
void XFormat_setFontOutline(XFormat* self, bool outline);

/**
 * @brief      获取字体名称
 * @param self 指针
 * @return     字体名称
 */
const XString* XFormat_fontName(const XFormat* self);

/**
 * @brief      设置字体名称
 * @param self 指针
 * @param name 字体名称
 */
void XFormat_setFontName(XFormat* self, const XString* name);
/**
 * @brief 使用 UTF-8 文本设置字体名称。
 * @param self 格式对象指针。
 * @param name UTF-8 字体名称，可为 NULL 清除名称。
 */
void XFormat_setFontName_utf8(XFormat* self, const char* name);

/**
 * @brief 获取字体名称的 UTF-8 文本。
 * @param self 格式对象指针。
 * @return UTF-8 字体名称；未设置或失败时返回 NULL。返回指针属于对象内部，不可释放。
 */
const char* XFormat_fontName_utf8(const XFormat* self);

/**
 * @brief      获取字体对象
 * @param self 指针
 * @return     新创建的字体对象，调用者负责调用 XFont_delete_base 释放；失败返回 NULL
 */
XFont* XFormat_font(const XFormat* self);

/**
 * @brief      设置字体
 * @param self 指针
 * @param font 字体指针；属性会被复制，调用者保留所有权
 */
void XFormat_setFont(XFormat* self, const XFont* font);

/* ========== 对齐属性 ========== */

/**
 * @brief      获取水平对齐
 * @param self 指针
 * @return     水平对齐方式
 */
XFormat_HorizontalAlignment XFormat_horizontalAlignment(const XFormat* self);

/**
 * @brief      设置水平对齐
 * @param self  指针
 * @param align 对齐方式
 */
void XFormat_setHorizontalAlignment(XFormat* self, XFormat_HorizontalAlignment align);

/**
 * @brief      获取垂直对齐
 * @param self 指针
 * @return     垂直对齐方式
 */
XFormat_VerticalAlignment XFormat_verticalAlignment(const XFormat* self);

/**
 * @brief      设置垂直对齐
 * @param self  指针
 * @param align 对齐方式
 */
void XFormat_setVerticalAlignment(XFormat* self, XFormat_VerticalAlignment align);

/**
 * @brief      获取文本换行状态
 * @param self 指针
 * @return     换行返回 true
 */
bool XFormat_textWrap(const XFormat* self);

/**
 * @brief      设置文本换行
 * @param self      指针
 * @param textWrap 是否换行
 */
void XFormat_setTextWrap(XFormat* self, bool textWrap);

/**
 * @brief      获取旋转角度
 * @param self 指针
 * @return     角度
 */
int XFormat_rotation(const XFormat* self);

/**
 * @brief      设置旋转角度
 * @param self     指针
 * @param rotation 角度
 */
void XFormat_setRotation(XFormat* self, int rotation);

/**
 * @brief      获取缩进级别
 * @param self 指针
 * @return     缩进级别
 */
int XFormat_indent(const XFormat* self);

/**
 * @brief      设置缩进级别
 * @param self  指针
 * @param indent 缩进级别
 */
void XFormat_setIndent(XFormat* self, int indent);

/**
 * @brief      获取缩小填充状态
 * @param self 指针
 * @return     缩小填充返回 true
 */
bool XFormat_shrinkToFit(const XFormat* self);

/**
 * @brief      设置缩小填充
 * @param self  指针
 * @param shrink 是否缩小填充
 */
void XFormat_setShrinkToFit(XFormat* self, bool shrink);

/* ========== 边框属性 ========== */

/**
 * @brief      设置所有边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setBorderStyle(XFormat* self, XFormat_BorderStyle style);

/**
 * @brief      设置所有边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setBorderColor(XFormat* self, const XColor* color);

/**
 * @brief      获取左边框样式
 * @param self 指针
 * @return     边框样式
 */
XFormat_BorderStyle XFormat_leftBorderStyle(const XFormat* self);

/**
 * @brief      设置左边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setLeftBorderStyle(XFormat* self, XFormat_BorderStyle style);

/**
 * @brief      获取左边框颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_leftBorderColor(const XFormat* self);

/**
 * @brief      设置左边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setLeftBorderColor(XFormat* self, const XColor* color);

/**
 * @brief      获取右边框样式
 * @param self 指针
 * @return     边框样式
 */
XFormat_BorderStyle XFormat_rightBorderStyle(const XFormat* self);

/**
 * @brief      设置右边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setRightBorderStyle(XFormat* self, XFormat_BorderStyle style);

/**
 * @brief      获取右边框颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_rightBorderColor(const XFormat* self);

/**
 * @brief      设置右边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setRightBorderColor(XFormat* self, const XColor* color);

/**
 * @brief      获取上边框样式
 * @param self 指针
 * @return     边框样式
 */
XFormat_BorderStyle XFormat_topBorderStyle(const XFormat* self);

/**
 * @brief      设置上边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setTopBorderStyle(XFormat* self, XFormat_BorderStyle style);

/**
 * @brief      获取上边框颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_topBorderColor(const XFormat* self);

/**
 * @brief      设置上边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setTopBorderColor(XFormat* self, const XColor* color);

/**
 * @brief      获取下边框样式
 * @param self 指针
 * @return     边框样式
 */
XFormat_BorderStyle XFormat_bottomBorderStyle(const XFormat* self);

/**
 * @brief      设置下边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setBottomBorderStyle(XFormat* self, XFormat_BorderStyle style);

/**
 * @brief      获取下边框颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_bottomBorderColor(const XFormat* self);

/**
 * @brief      设置下边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setBottomBorderColor(XFormat* self, const XColor* color);

/**
 * @brief      获取对角线边框样式
 * @param self 指针
 * @return     边框样式
 */
XFormat_BorderStyle XFormat_diagonalBorderStyle(const XFormat* self);

/**
 * @brief      设置对角线边框样式
 * @param self  指针
 * @param style 边框样式
 */
void XFormat_setDiagonalBorderStyle(XFormat* self, XFormat_BorderStyle style);

/**
 * @brief      获取对角线边框类型
 * @param self 指针
 * @return     对角线类型
 */
XFormat_DiagonalBorderType XFormat_diagonalBorderType(const XFormat* self);

/**
 * @brief      设置对角线边框类型
 * @param self  指针
 * @param type  对角线类型
 */
void XFormat_setDiagonalBorderType(XFormat* self, XFormat_DiagonalBorderType type);

/**
 * @brief      获取对角线边框颜色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_diagonalBorderColor(const XFormat* self);

/**
 * @brief      设置对角线边框颜色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setDiagonalBorderColor(XFormat* self, const XColor* color);

/* ========== 填充属性 ========== */

/**
 * @brief      获取填充图案
 * @param self 指针
 * @return     填充图案
 */
XFormat_FillPattern XFormat_fillPattern(const XFormat* self);

/**
 * @brief      设置填充图案
 * @param self    指针
 * @param pattern 填充图案
 */
void XFormat_setFillPattern(XFormat* self, XFormat_FillPattern pattern);

/**
 * @brief      获取图案前景色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_patternForegroundColor(const XFormat* self);

/**
 * @brief      设置图案前景色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setPatternForegroundColor(XFormat* self, const XColor* color);

/**
 * @brief      获取图案背景色
 * @param self 指针
 * @return     颜色
 */
XColor XFormat_patternBackgroundColor(const XFormat* self);

/**
 * @brief      设置图案背景色
 * @param self  指针
 * @param color 颜色
 */
void XFormat_setPatternBackgroundColor(XFormat* self, const XColor* color);

/* ========== 保护属性 ========== */

/**
 * @brief      获取锁定状态
 * @param self 指针
 * @return     锁定返回 true
 */
bool XFormat_locked(const XFormat* self);

/**
 * @brief      设置锁定
 * @param self    指针
 * @param locked 是否锁定
 */
void XFormat_setLocked(XFormat* self, bool locked);

/**
 * @brief      获取隐藏状态
 * @param self 指针
 * @return     隐藏返回 true
 */
bool XFormat_hidden(const XFormat* self);

/**
 * @brief      设置隐藏
 * @param self   指针
 * @param hidden 是否隐藏
 */
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

/**
 * @brief      判断是否有数字格式数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasNumFmtData(const XFormat* self);

/**
 * @brief      判断是否有字体数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasFontData(const XFormat* self);

/**
 * @brief      判断是否有填充数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasFillData(const XFormat* self);

/**
 * @brief      判断是否有边框数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasBorderData(const XFormat* self);

/**
 * @brief      判断是否有对齐数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasAlignmentData(const XFormat* self);

/**
 * @brief      判断是否有保护数据
 * @param self 指针
 * @return     有返回 true
 */
bool XFormat_hasProtectionData(const XFormat* self);

/**
 * @brief      判断字体索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_fontIndexValid(const XFormat* self);

/**
 * @brief      获取字体索引
 * @param self 指针
 * @return     字体索引
 */
int XFormat_fontIndex(const XFormat* self);

/**
 * @brief      判断边框索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_borderIndexValid(const XFormat* self);

/**
 * @brief      获取边框索引
 * @param self 指针
 * @return     边框索引
 */
int XFormat_borderIndex(const XFormat* self);

/**
 * @brief      判断填充索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_fillIndexValid(const XFormat* self);

/**
 * @brief      获取填充索引
 * @param self 指针
 * @return     填充索引
 */
int XFormat_fillIndex(const XFormat* self);

/**
 * @brief      判断 XF 索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_xfIndexValid(const XFormat* self);

/**
 * @brief      获取 XF 索引
 * @param self 指针
 * @return     XF 索引
 */
int XFormat_xfIndex(const XFormat* self);

/**
 * @brief      判断 DXF 索引是否有效
 * @param self 指针
 * @return     有效返回 true
 */
bool XFormat_dxfIndexValid(const XFormat* self);

/**
 * @brief      获取 DXF 索引
 * @param self 指针
 * @return     DXF 索引
 */
int XFormat_dxfIndex(const XFormat* self);

/**
 * @brief      设置字体索引
 * @param self  指针
 * @param index 索引
 */
void XFormat_setFontIndex(XFormat* self, int index);

/**
 * @brief      设置边框索引
 * @param self  指针
 * @param index 索引
 */
void XFormat_setBorderIndex(XFormat* self, int index);

/**
 * @brief      设置填充索引
 * @param self  指针
 * @param index 索引
 */
void XFormat_setFillIndex(XFormat* self, int index);

/**
 * @brief      设置 XF 索引
 * @param self  指针
 * @param index 索引
 */
void XFormat_setXfIndex(XFormat* self, int index);

/**
 * @brief      设置 DXF 索引
 * @param self  指针
 * @param index 索引
 */
void XFormat_setDxfIndex(XFormat* self, int index);

/* ========== 键生成（用于样式去重） ========== */

/**
 * @brief      生成字体属性的唯一键
 * @param self 指针
 * @param outKey 输出键数据
 * @param outLen 输出键长度
 */
void XFormat_fontKey(const XFormat* self, uint8_t** outKey, size_t* outLen);

/**
 * @brief      生成边框属性的唯一键
 * @param self 指针
 * @param outKey 输出键数据
 * @param outLen 输出键长度
 */
void XFormat_borderKey(const XFormat* self, uint8_t** outKey, size_t* outLen);

/**
 * @brief      生成填充属性的唯一键
 * @param self 指针
 * @param outKey 输出键数据
 * @param outLen 输出键长度
 */
void XFormat_fillKey(const XFormat* self, uint8_t** outKey, size_t* outLen);

/**
 * @brief      生成完整格式的唯一键
 * @param self 指针
 * @param outKey 输出键数据
 * @param outLen 输出键长度
 */
void XFormat_formatKey(const XFormat* self, uint8_t** outKey, size_t* outLen);

/* ========== 数字格式修正 ========== */

/**
 * @brief      修正数字格式（设置自定义格式 ID 和格式代码）
 * @param self   指针
 * @param id     格式 ID
 * @param format 格式代码
 */
void XFormat_fixNumberFormat(XFormat* self, int id, const XString* format);

/* ========== 主题 ========== */

/**
 * @brief      获取主题颜色索引
 * @param self 指针
 * @return     主题颜色索引
 */
int XFormat_theme(const XFormat* self);

/* ========== 类型化属性访问 ========== */

/**
 * @brief      获取布尔类型属性
 * @param self         指针
 * @param propertyId   属性ID
 * @param defaultValue 默认值
 * @return             属性值
 */
bool XFormat_boolProperty(const XFormat* self, int propertyId, bool defaultValue);

/**
 * @brief      获取整数类型属性
 * @param self         指针
 * @param propertyId   属性ID
 * @param defaultValue 默认值
 * @return             属性值
 */
int XFormat_intProperty(const XFormat* self, int propertyId, int defaultValue);

/**
 * @brief      获取双精度类型属性
 * @param self         指针
 * @param propertyId   属性ID
 * @param defaultValue 默认值
 * @return             属性值
 */
double XFormat_doubleProperty(const XFormat* self, int propertyId, double defaultValue);

/**
 * @brief      获取字符串类型属性
 * @param self         指针
 * @param propertyId   属性ID
 * @param defaultValue 默认值
 * @return             属性值
 */
const XString* XFormat_stringProperty(const XFormat* self, int propertyId, const XString* defaultValue);

/**
 * @brief      获取颜色类型属性
 * @param self         指针
 * @param propertyId   属性ID
 * @param defaultValue 默认值
 * @return             属性值
 */
XColor XFormat_colorProperty(const XFormat* self, int propertyId, const XColor* defaultValue);

/* ========== 比较运算符 ========== */

/**
 * @brief      判断两个格式是否相等
 * @param a 格式 A
 * @param b 格式 B
 * @return    相等返回 true
 */
bool XFormat_equals(const XFormat* a, const XFormat* b);

/**
 * @brief      判断两个格式是否不相等
 * @param a 格式 A
 * @param b 格式 B
 * @return    不相等返回 true
 */
bool XFormat_notEquals(const XFormat* a, const XFormat* b);

#ifdef __cplusplus
}
#endif
#endif /* XFORMAT_H */
