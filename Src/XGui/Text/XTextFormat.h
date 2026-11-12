/**
 * @file       XTextFormat.h
 * @brief      XTextFormat 文本格式属性池值对象（对标 Qt 6.8 QTextFormat
 *             及其类型化派生 QTextCharFormat/QTextBlockFormat/QTextListFormat/
 *             QTextImageFormat）。
 * @details    Qt 的 QTextFormat 是「format_type + 属性键值表」的值类型
 *             （qtextformat.h:89-372）：属性存于 QTextFormatPrivate::props
 *             （QList<Property>，qtextformat.cpp:146-217，线性按键查重
 *             覆盖/追加）。本类同构：
 *             - `m_type`：对标 format_type（FormatType，数值逐一对齐）；
 *             - `m_props`：XVector 线性属性表，元素 XTextFormatProperty
 *               {key, XVariant value}——Qt 的 QList<Property> 同构；元素
 *               拷贝/析构回调注册在容器上（XContainerSetDataCopyMethod/
 *               SetDataDeinitMethod），整容器深拷贝与清空自动深拷贝/
 *               释放每条属性载荷；
 *             - 属性值承载用框架 XVariant（Qt 的 QVariant 同位）：
 *               int→XVariantType_Int、double→Double、bool→Bool、
 *               string→String（内嵌 XString）、color→Uint32（ARGB，
 *               XTDCharFormat.fgColor 同口径——Qt 的 QBrush/QColor 载荷
 *               在本子集收敛为 ARGB 单值）。
 *
 *             **Qt 6.8 语义逐条对齐**（行号为 v6.8.3 原文）：
 *             - setProperty 无条件存（含无效值，qtextformat.cpp:1216-1227
 *               + QTextFormatPrivate::insertProperty :166-183）；getter
 *               严格按属性值类型取数，类型不符回落缺省（intProperty
 *               :1059-1075 特例 LayoutDirection 缺省 LayoutDirectionAuto
 *               =3，其余缺省 0；doubleProperty :1080-1093 接受 Double/
 *               Float；boolProperty :1042-1055；stringProperty
 *               :1098-1111；colorProperty :1116-1129）；
 *             - merge(other)：type 不同不动；self 空属性全量接收；other
 *               空属性不动；冲突 other 优先、无效值清键（:940-965）；
 *             - equal：type 相同 + 属性表逐键逐值相等（operator==，
 *               :1362-1380；空属性表与无表等价）；
 *             - isEmpty = propertyCount()==0（qtextformat.h:310）；
 *             - 缺省值表：fontWeight 未设回落 QFont::Normal=400
 *               （:451）；superScriptBaseline 未设 50.0、subScriptBaseline
 *               未设 100.0/6.0、baselineOffset 未设 0.0（:548/:553/:558）；
 *               blockFormat alignment 未设回落 AlignLeft=0x01（:622-624）；
 *               indent 未设 0（:636-637）；listStyle 未设
 *               ListStyleUndefined=0（:751-752）；
 *             - setFontFamily 单族写 FontFamilies 键（Qt 6.8 语义：
 *               qtextformat.h:417-419，setProperty(FontFamilies,
 *               QStringList{family})）；setFontUnderline 写
 *               TextUnderlineStyle（:473-474，SingleUnderline/NoUnderline），
 *               fontUnderline() 先看 TextUnderlineStyle 再回落 FontUnderline
 *               兼容键（qtextformat.cpp:1638-1643）。
 *
 *             **取证取舍**（qtextformat.h Property 全表 vs 本子集）：
 *             - TextOutline(0x2022) **不做**：Qt 载荷是 QPen，框架无
 *               QPen 值类型映射、XVariant 亦无画笔类型——不留半成品入口；
 *             - FontFamily(0x2000) 旧键**不落表**：Qt 6.8 的 setter 全写
 *               FontFamilies(0x1FE7)（引擎消费同键），本类同口径；
 *             - ListStyle 族完整做（style/indent/prefix/suffix/start，
 *               qtextformat.h:723-763）；ImageFormat 三基础属性做
 *               （ImageName/ImageWidth/ImageHeight，:788-810）。
 *
 *             XTextCharFormat/XTextBlockFormat/XTextListFormat/XTextImageFormat
 *             是同一属性池结构在 C 侧的类型化包装（typedef 同一类型 +
 *             type 位固定的 init/convert 入口；Qt 的派生类不增属性存储，
 *             只是 type 位 + 便捷入口集合，同构成立）。纯值对象，无虚表、
 *             无 _base 契约；持有堆资源（属性表），init/copy/move/deinit
 *             四件套配对。
 * @note       本头随 XTEXTDOCUMENT_ON（XTextDocument.h）使用；无独立开关
 *             （同 XTextOption 先例：QtGui 纯值类型）。
 * @warning    新增字段一律**尾部追加**并用 init 显式清零（结构体初值铁律：
 *             MSVC /RTC1 下未清零字段读栈垃圾）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTEXTFORMAT_H
#define XTEXTFORMAT_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XString.h"
#include "XVector.h"
#include "XVariant.h"

#if XTEXTDOCUMENT_ON

/** @brief 格式类型（对标 QTextFormat::FormatType，数值一致；
 *         qtextformat.h:93-101）。 */
typedef enum XTextFormatType
{
    XTextFormatType_InvalidFormat = -1, /**< 无效/未定型。 */
    XTextFormatType_BlockFormat   = 1,  /**< 段落格式。 */
    XTextFormatType_CharFormat    = 2,  /**< 字符格式。 */
    XTextFormatType_ListFormat    = 3,  /**< 列表格式。 */
    XTextFormatType_FrameFormat   = 5,  /**< 框架格式。 */
    XTextFormatType_UserFormat    = 100 /**< 用户自定义起点。 */
} XTextFormatType;

/**
 * @brief 属性键（对标 QTextFormat::Property，**数值逐一对齐**；
 *         qtextformat.h:104-256）。只列本子集入口真正落表的键；
 *         未列出的键（TabPositions/FrameBorder/Table* 等表/框架族）
 *         经 setProperty_value 仍可存取（Qt 属性表对键开放，UserProperty
 *         起为用户区）——不设枚举名只是不提供便捷入口，不留半成品。
 */
typedef enum XTextFormatPropertyId
{
    XTextFormatProperty_ObjectIndex             = 0x0,     /**< 对象索引。 */
    XTextFormatProperty_LayoutDirection         = 0x0801,  /**< 布局方向（intProperty 特例：未设回落 3=LayoutDirectionAuto）。 */
    XTextFormatProperty_BackgroundBrush         = 0x0820,  /**< 背景（ARGB；Qt 载荷 QBrush）。 */
    XTextFormatProperty_ForegroundBrush         = 0x0821,  /**< 前景（ARGB；Qt 载荷 QBrush）。 */
    XTextFormatProperty_BlockAlignment          = 0x1010,  /**< 段落对齐（int 位组合，XTDAlignment 同值空间）。 */
    XTextFormatProperty_BlockTopMargin          = 0x1030,  /**< 段上边距（double）。 */
    XTextFormatProperty_BlockBottomMargin       = 0x1031,  /**< 段下边距（double）。 */
    XTextFormatProperty_BlockLeftMargin         = 0x1032,  /**< 段左边距（double）。 */
    XTextFormatProperty_BlockRightMargin        = 0x1033,  /**< 段右边距（double）。 */
    XTextFormatProperty_TextIndent              = 0x1034,  /**< 首行缩进（double）。 */
    XTextFormatProperty_BlockIndent             = 0x1040,  /**< 段缩进级（int）。 */
    XTextFormatProperty_LineHeight              = 0x1048,  /**< 行高（double，语义随 LineHeightType）。 */
    XTextFormatProperty_LineHeightType          = 0x1049,  /**< 行高类型（int，0..4）。 */
    XTextFormatProperty_HeadingLevel            = 0x1070,  /**< 标题级（int）。 */
    XTextFormatProperty_FontFamilies            = 0x1FE7,  /**< 字体族（String，单族承载 Qt 的 QStringList{family}）。 */
    XTextFormatProperty_FontPointSize           = 0x2001,  /**< 字号（double，磅）。 */
    XTextFormatProperty_FontWeight              = 0x2003,  /**< 字重（int，400=Normal/700=Bold）。 */
    XTextFormatProperty_FontItalic              = 0x2004,  /**< 斜体（bool）。 */
    XTextFormatProperty_FontUnderline           = 0x2005,  /**< 下划线兼容键（bool；Qt 6.8 setter 写 TextUnderlineStyle，读路径回落本键）。 */
    XTextFormatProperty_FontOverline            = 0x2006,  /**< 上划线（bool）。 */
    XTextFormatProperty_FontStrikeOut           = 0x2007,  /**< 删除线（bool）。 */
    XTextFormatProperty_FontFixedPitch          = 0x2008,  /**< 等宽（bool）。 */
    XTextFormatProperty_FontPixelSize           = 0x2009,  /**< 像素字号（int）。 */
    XTextFormatProperty_TextUnderlineColor      = 0x2020,  /**< 下划线色（ARGB）。 */
    XTextFormatProperty_TextVerticalAlignment   = 0x2021,  /**< 垂直对齐（int，XTextFormatVerticalAlignment）。 */
    XTextFormatProperty_TextUnderlineStyle      = 0x2023,  /**< 下划线风格（int，XTextFormatUnderlineStyle）。 */
    XTextFormatProperty_TextToolTip             = 0x2024,  /**< 工具提示（String）。 */
    XTextFormatProperty_TextSuperScriptBaseline = 0x2025,  /**< 上标基线%（double，未设 50.0）。 */
    XTextFormatProperty_TextSubScriptBaseline   = 0x2026,  /**< 下标基线%（double，未设 100.0/6.0）。 */
    XTextFormatProperty_TextBaselineOffset      = 0x2027,  /**< 基线偏移%（double，未设 0.0）。 */
    XTextFormatProperty_IsAnchor                = 0x2030,  /**< 锚点标志（bool）。 */
    XTextFormatProperty_AnchorHref              = 0x2031,  /**< 锚点 href（String）。 */
    XTextFormatProperty_AnchorName              = 0x2032,  /**< 锚点名（String，Qt 为 StringList——本子集单值）。 */
    XTextFormatProperty_ObjectType              = 0x2f00,  /**< 对象类型（int，Qt ObjectTypes）。 */
    XTextFormatProperty_ListStyle               = 0x3000,  /**< 列表风格（int，XTextFormatListStyle）。 */
    XTextFormatProperty_ListIndent              = 0x3001,  /**< 列表缩进（int）。 */
    XTextFormatProperty_ListNumberPrefix        = 0x3002,  /**< 序号前缀（String）。 */
    XTextFormatProperty_ListNumberSuffix        = 0x3003,  /**< 序号后缀（String）。 */
    XTextFormatProperty_ListStart               = 0x3004,  /**< 列表起始号（int）。 */
    XTextFormatProperty_ImageName               = 0x5000,  /**< 图片资源名（String）。 */
    XTextFormatProperty_ImageWidth              = 0x5010,  /**< 图片显示宽（double）。 */
    XTextFormatProperty_ImageHeight             = 0x5011,  /**< 图片显示高（double）。 */
    XTextFormatProperty_UserProperty            = 0x100000 /**< 用户属性区起点。 */
} XTextFormatPropertyId;

/** @brief 垂直对齐（对标 QTextCharFormat::VerticalAlignment，数值一致）。 */
typedef enum XTextFormatVerticalAlignment
{
    XTextFormatVerticalAlign_Normal      = 0, /**< 常规基线。 */
    XTextFormatVerticalAlign_SuperScript = 1, /**< 上标。 */
    XTextFormatVerticalAlign_SubScript   = 2, /**< 下标。 */
    XTextFormatVerticalAlign_Middle      = 3, /**< 居中。 */
    XTextFormatVerticalAlign_Top         = 4, /**< 顶对齐。 */
    XTextFormatVerticalAlign_Bottom      = 5, /**< 底对齐。 */
    XTextFormatVerticalAlign_Baseline    = 6  /**< 基线对齐。 */
} XTextFormatVerticalAlignment;

/** @brief 下划线风格（对标 QTextCharFormat::UnderlineStyle，数值一致；
 *         qtextformat.h:401-411，与 Qt::PenStyle 同值）。 */
typedef enum XTextFormatUnderlineStyle
{
    XTextFormatUnderline_NoUnderline        = 0, /**< 无。 */
    XTextFormatUnderline_SingleUnderline    = 1, /**< 单线。 */
    XTextFormatUnderline_DashUnderline      = 2, /**< 虚线。 */
    XTextFormatUnderline_DotLine            = 3, /**< 点线。 */
    XTextFormatUnderline_DashDotLine        = 4, /**< 点划线。 */
    XTextFormatUnderline_DashDotDotLine     = 5, /**< 双点划线。 */
    XTextFormatUnderline_WaveUnderline      = 6, /**< 波浪线。 */
    XTextFormatUnderline_SpellCheckUnderline = 7 /**< 拼写检查线。 */
} XTextFormatUnderlineStyle;

/** @brief 列表风格（对标 QTextListFormat::Style，数值一致；
 *         qtextformat.h:733-743）。 */
typedef enum XTextFormatListStyle
{
    XTextFormatList_Disc               = -1, /**< 实心圆点。 */
    XTextFormatList_Circle             = -2, /**< 空心圆点。 */
    XTextFormatList_Square             = -3, /**< 方块。 */
    XTextFormatList_Decimal            = -4, /**< 十进制数字。 */
    XTextFormatList_LowerAlpha         = -5, /**< 小写拉丁。 */
    XTextFormatList_UpperAlpha         = -6, /**< 大写拉丁。 */
    XTextFormatList_LowerRoman         = -7, /**< 小写罗马。 */
    XTextFormatList_UpperRoman         = -8, /**< 大写罗马。 */
    XTextFormatList_StyleUndefined     = 0   /**< 未定义。 */
} XTextFormatListStyle;

/** @brief 行高类型（对标 QTextBlockFormat::LineHeightTypes，数值一致）。 */
typedef enum XTextFormatLineHeightType
{
    XTextFormatLineHeight_SingleHeight      = 0, /**< 单倍。 */
    XTextFormatLineHeight_ProportionalHeight = 1, /**< 比例（LineHeight 为 %）。 */
    XTextFormatLineHeight_FixedHeight       = 2, /**< 固定（LineHeight 为像素×scaling）。 */
    XTextFormatLineHeight_MinimumHeight     = 3, /**< 最小。 */
    XTextFormatLineHeight_LineDistanceHeight = 4 /**< 行距。 */
} XTextFormatLineHeightType;

/**
 * @brief      属性条目（对标 QTextFormatPrivate::Property，qtextformat.cpp
 *             :134-141：key + QVariant value 线性表元素）。
 * @warning    value 是内嵌 XVariant 值（载荷堆分配）；条目随 XVector
 *             存储时由容器回调（XTextFormat 的 copy/deinit 元素回调）负责
 *             深拷贝/释放，外部不得对条目做浅拷贝后混用。
 */
typedef struct XTextFormatProperty
{
    int      key;    /**< 属性键（XTextFormatPropertyId）。 */
    XVariant value;  /**< 属性值（XVariant 值形态，载荷堆分配）。 */
} XTextFormatProperty;

/**
 * @brief      文本格式属性池值对象（对标 QTextFormat）。
 * @warning    值对象但持有堆资源（m_props 载荷）：init/copy/move/deinit
 *             四件套必须配对；栈实例 deinit 后即为空壳可复用。
 *             新增字段一律**尾部追加** + init 显式清零（结构体初值铁律）。
 */
typedef struct XTextFormat
{
    int      m_type;   /**< 格式类型（XTextFormatType；对标 format_type）。 */
    XVector  m_props;  /**< 属性表（线性按键查重；元素 XTextFormatProperty，
                            容器挂元素深拷贝/析构回调——对标
                            QTextFormatPrivate::props 的 QList<Property>）。 */
} XTextFormat;

/** @brief 字符格式（对标 QTextCharFormat：同一属性池、type=CharFormat；
 *         Qt 派生类不增存储只是 type 位 + 便捷入口，C 侧 typedef 同构）。 */
typedef XTextFormat XTextCharFormat;
/** @brief 段落格式（对标 QTextBlockFormat）。 */
typedef XTextFormat XTextBlockFormat;
/** @brief 列表格式（对标 QTextListFormat）。 */
typedef XTextFormat XTextListFormat;
/** @brief 图片格式（对标 QTextImageFormat：type=CharFormat 且
 *         objectType=ImageObject——Qt isImageFormat 的判定口径）。 */
typedef XTextFormat XTextImageFormat;

/* ==================== 生命周期（值对象四件套） ==================== */

/**
 * @brief      初始化格式（对标 QTextFormat(int type) 构造，qtextformat.cpp
 *             :884-887——**不设任何缺省属性**，isEmpty 恒 true 直到首个
 *             setProperty）。
 * @details    防呆：未清零栈实例（vtable 不匹配）先整体清零再建属性表
 *             （MSVC /RTC1 与栈垃圾双防，XVariant_init 同款套路）；已初始化
 *             实例重复 init 先释放旧属性表（防泄漏契约）。
 * @param      self 目标格式（栈实例或堆内嵌成员均可）；NULL 忽略。
 * @param      type 格式类型（XTextFormatType；负值原样保存——Qt 的
 *             format_type 亦为裸 int 存储）。
 */
void XTextFormat_init(XTextFormat* self, int type);

/** @brief 释放属性表全部载荷并复位空壳（type 清为 Invalid）。
 *         self 为 NULL 忽略；重复调用安全（幂等）。 */
void XTextFormat_deinit(XTextFormat* self);

/** @brief 深拷贝（对标 QTextFormat 拷贝构造：type + 属性表逐条深拷贝）。
 * @param dst 目标；先释放自身旧载荷再接收（可重复赋值）。
 * @param src 源；NULL 视为空格式（dst 变为 Invalid 空壳）。
 */
void XTextFormat_copy(XTextFormat* dst, const XTextFormat* src);

/** @brief 移动（对标 QTextFormat swap/移动语义：属性表载荷转移，
 *         源复位为 Invalid 空壳——空壳仍由调用方按分配方式释放）。
 * @param dst 目标；先释放自身旧载荷再接收。
 * @param src 源；转移后为空壳。NULL 忽略。
 */
void XTextFormat_move(XTextFormat* dst, XTextFormat* src);

/** @brief 值返回便利构造（type 起步的空格式；失败/非法参数返回
 *         InvalidFormat 空值）。 */
XTextFormat XTextFormat_make(int type);

/* ==================== 类型与状态 ==================== */

/** @brief 格式类型（对标 type()，qtextformat.cpp:972-976）。NULL 返回
 *         InvalidFormat。 */
int XTextFormat_type(const XTextFormat* self);

/** @brief 是否有效（对标 isValid：type != InvalidFormat）。 */
bool XTextFormat_isValid(const XTextFormat* self);

/** @brief 是否字符格式（对标 isCharFormat：type == CharFormat）。 */
bool XTextFormat_isCharFormat(const XTextFormat* self);
/** @brief 是否段落格式（对标 isBlockFormat）。 */
bool XTextFormat_isBlockFormat(const XTextFormat* self);
/** @brief 是否列表格式（对标 isListFormat）。 */
bool XTextFormat_isListFormat(const XTextFormat* self);
/** @brief 是否框架格式（对标 isFrameFormat）。 */
bool XTextFormat_isFrameFormat(const XTextFormat* self);
/** @brief 是否图片格式（对标 isImageFormat：type==CharFormat 且
 *         objectType 属性 == ImageObject(1)，qtextformat.h:330）。 */
bool XTextFormat_isImageFormat(const XTextFormat* self);

/** @brief 是否空属性表（对标 isEmpty：propertyCount()==0；NULL 恒 true）。 */
bool XTextFormat_isEmpty(const XTextFormat* self);

/** @brief 属性条数（对标 propertyCount）。NULL 返回 0。 */
int XTextFormat_propertyCount(const XTextFormat* self);

/* ==================== 属性存取 ==================== */

/**
 * @brief      属性是否已设置（对标 hasProperty：键在线性表中即已设，
 *             qtextformat.cpp:197-199）。
 * @param      self 目标格式；NULL 返回 false。
 * @param      propertyId 属性键。
 */
bool XTextFormat_hasProperty(const XTextFormat* self, int propertyId);

/**
 * @brief      取属性值（对标 property()，qtextformat.cpp:1206-1210）。
 * @warning    返回**借用**指针（内部属性表条目载荷；随本格式的任何写操作
 *             失效），缺失返回 NULL——禁止释放。Qt 返回无效 QVariant，
 *             C 借用 NULL 表达同一「无值」语义。
 * @param      self 目标格式；NULL 返回 NULL。
 * @param      propertyId 属性键。
 * @return     属性值借用；缺失 NULL。
 */
const XVariant* XTextFormat_property(const XTextFormat* self, int propertyId);

/**
 * @brief      设属性值（对标 setProperty(id, QVariant) + insertProperty，
 *             qtextformat.cpp:166-183/1216-1227：已有同键**覆盖**、否则
 *             追加；**无效值（XVariantType_NULL）也照存**——与 Qt 一致，
 *             merge 时对无效值走清键分支）。
 * @param      self 目标格式；NULL 忽略。
 * @param      propertyId 属性键。
 * @param      value 属性值（**借用**：内部深拷贝；NULL 等价无效值）。
 *             **须为已初始化 XVariant**（XVariant_init / XVariant_create_*
 *             构造，虚表有效）——拷贝按源虚表多态分发，裸 memset 载体
 *             （虚表 NULL）会被静默跳过（XClass_copy_base 对 src 虚表
 *             NULL 直接返回）。
 */
void XTextFormat_setProperty_value(XTextFormat* self, int propertyId,
                                   const XVariant* value);

/** @brief 设 int 属性（XVariantType_Int 承载）。NULL 忽略。 */
void XTextFormat_setProperty_int(XTextFormat* self, int propertyId, int value);

/** @brief 设 double 属性（XVariantType_Double 承载）。NULL 忽略。 */
void XTextFormat_setProperty_double(XTextFormat* self, int propertyId,
                                    double value);

/** @brief 设 bool 属性（XVariantType_Bool 承载）。NULL 忽略。 */
void XTextFormat_setProperty_bool(XTextFormat* self, int propertyId,
                                  bool value);

/** @brief 设 string 属性（XVariantType_String 承载，内嵌 XString 深拷贝
 *         UTF-8 文本；NULL/空串存空 XString）。NULL 忽略。 */
void XTextFormat_setProperty_string(XTextFormat* self, int propertyId,
                                    const char* utf8);

/** @brief 设 color 属性（XVariantType_Uint32 承载 ARGB；Qt 的
 *         QColor/QBrush 载荷在本子集收敛为 ARGB 单值）。NULL 忽略。 */
void XTextFormat_setProperty_color(XTextFormat* self, int propertyId,
                                   uint32_t argb);

/**
 * @brief      清属性（对标 clearProperty：键不存在不动，qtextformat.cpp
 *             :186-192）。NULL 忽略。
 */
void XTextFormat_clearProperty(XTextFormat* self, int propertyId);

/**
 * @brief      取 int 属性（对标 intProperty，qtextformat.cpp:1059-1075）：
 *             未设或**属性值类型不是 Int** 回落缺省——缺省恒 0，唯
 *             LayoutDirection(0x801) 特例回落 3（LayoutDirectionAuto）。
 */
int XTextFormat_intProperty(const XTextFormat* self, int propertyId);

/**
 * @brief      取 double 属性（对标 doubleProperty，qtextformat.cpp
 *             :1080-1093）：未设或类型不是 Double/Float 回落 0.0。
 */
double XTextFormat_doubleProperty(const XTextFormat* self, int propertyId);

/**
 * @brief      取 bool 属性（对标 boolProperty，qtextformat.cpp:1042-1055）：
 *             未设或类型不是 Bool 回落 false。
 */
bool XTextFormat_boolProperty(const XTextFormat* self, int propertyId);

/**
 * @brief      取 string 属性（对标 stringProperty，qtextformat.cpp
 *             :1098-1111）：未设或类型不是 String 回落**空串**。
 * @return     借用 UTF-8 指针（内部 XString 缓存；随本格式写操作失效），
 *             缺失返回共享 ""（非 NULL）。NULL self 同 ""。
 */
const char* XTextFormat_stringProperty(const XTextFormat* self, int propertyId);

/**
 * @brief      取 color 属性（对标 colorProperty 的子集口径）：未设或类型
 *             不是 Uint32 回落 0（=全透明，等价 Qt 的无效 QColor）。
 * @return     ARGB。
 */
uint32_t XTextFormat_colorProperty(const XTextFormat* self, int propertyId);

/* ==================== merge / equal ==================== */

/**
 * @brief      合并 other（对标 QTextFormat::merge，qtextformat.cpp:940-965）：
 *             - type 不同**直接返回**（Qt 同：format_type 不等即 return）；
 *             - self 空属性表 → 全量接收 other（Qt：d = other.d 共享，
 *               值语义下等价深拷贝）；
 *             - other 空属性表 → 不动；
 *             - 逐键：other 值有效 → 覆盖（冲突 other 优先）；other 值
 *               无效（XVariantType_NULL）→ 清除本键（Qt 同款分支）。
 * @param      self 目标格式；NULL 忽略。
 * @param      other 源格式（借用）；NULL 不动。
 */
void XTextFormat_merge(XTextFormat* self, const XTextFormat* other);

/**
 * @brief      相等比较（对标 operator==，qtextformat.cpp:1362-1380）：
 *             type 不同不等；双方属性表全空（含都无条目）相等；否则
 *             逐条 key+值（类型与载荷）相等。
 * @param      a、b 参与比较的格式；NULL 按「Invalid 空格式」参与。
 */
bool XTextFormat_equal(const XTextFormat* a, const XTextFormat* b);

/* ==================== 类型化包装：CharFormat ==================== */

/** @brief 初始化字符格式（type=CharFormat 的薄包装）。 */
void XTextCharFormat_init(XTextCharFormat* self);
/** @brief 构造字符格式值（type=CharFormat 空属性表）。 */
XTextCharFormat XTextCharFormat_make(void);
/** @brief 从 XTextFormat 转换（对标 QTextCharFormat(const QTextFormat&)：
 *         整值拷贝 + type 改写为 CharFormat）。dst 先释放旧载荷。 */
void XTextCharFormat_convert(XTextCharFormat* dst, const XTextFormat* src);

/**
 * @brief      设字体族（对标 setFontFamily，qtextformat.h:417-419：写
 *             FontFamilies(0x1FE7)，Qt 6.8 存 QStringList{family}——本库
 *             单族承载同一键，值类型 String）。
 * @param      self 目标格式；NULL 忽略。
 * @param      family UTF-8 字体族；NULL 按空串。
 */
void XTextCharFormat_setFontFamily(XTextCharFormat* self, const char* family);

/**
 * @brief      取字体族（对标 fontFamily，qtextformat.h:419-420：读
 *             FontFamilies 的**首族**；Qt 在属性缺失时行为未定义（空
 *             QStringList::first），本类明确回落 NULL）。
 * @return     借用 UTF-8 指针（随写操作失效）；未设 NULL。NULL self 同。
 */
const char* XTextCharFormat_fontFamily(const XTextCharFormat* self);

/** @brief 设字号（磅；对标 setFontPointSize→FontPointSize double）。 */
void XTextCharFormat_setFontPointSize(XTextCharFormat* self, double size);
/** @brief 取字号（对标 fontPointSize：doubleProperty，未设 0.0）。 */
double XTextCharFormat_fontPointSize(const XTextCharFormat* self);

/** @brief 设字重（对标 setFontWeight→FontWeight int）。 */
void XTextCharFormat_setFontWeight(XTextCharFormat* self, int weight);
/** @brief 取字重（对标 fontWeight：未设回落 QFont::Normal=400，
 *         qtextformat.h:451）。 */
int XTextCharFormat_fontWeight(const XTextCharFormat* self);

/** @brief 设斜体（对标 setFontItalic→FontItalic bool）。 */
void XTextCharFormat_setFontItalic(XTextCharFormat* self, bool italic);
/** @brief 取斜体（对标 fontItalic：boolProperty）。 */
bool XTextCharFormat_fontItalic(const XTextCharFormat* self);

/**
 * @brief      设下划线（对标 setFontUnderline，qtextformat.h:473-474：写
 *             TextUnderlineStyle(0x2023)=Single/NoUnderline——Qt 6.8
 *             **不再写** FontUnderline(0x2005)）。
 */
void XTextCharFormat_setFontUnderline(XTextCharFormat* self, bool underline);

/**
 * @brief      取下划线（对标 fontUnderline，qtextformat.cpp:1638-1643：
 *             TextUnderlineStyle 已设按 style==SingleUnderline 判，否则
 *             回落 FontUnderline(0x2005) 兼容键的 bool）。
 */
bool XTextCharFormat_fontUnderline(const XTextCharFormat* self);

/** @brief 设下划线风格（对标 setUnderlineStyle→TextUnderlineStyle int）。 */
void XTextCharFormat_setUnderlineStyle(XTextCharFormat* self, int style);
/** @brief 取下划线风格（对标 underlineStyle：intProperty，未设 0=No）。 */
int XTextCharFormat_underlineStyle(const XTextCharFormat* self);

/** @brief 设上划线（对标 setFontOverline→FontOverline bool）。 */
void XTextCharFormat_setFontOverline(XTextCharFormat* self, bool overline);
/** @brief 取上划线（对标 fontOverline）。 */
bool XTextCharFormat_fontOverline(const XTextCharFormat* self);

/** @brief 设删除线（对标 setFontStrikeOut→FontStrikeOut bool）。 */
void XTextCharFormat_setFontStrikeOut(XTextCharFormat* self, bool strikeOut);
/** @brief 取删除线（对标 fontStrikeOut）。 */
bool XTextCharFormat_fontStrikeOut(const XTextCharFormat* self);

/** @brief 设等宽（对标 setFontFixedPitch→FontFixedPitch bool）。 */
void XTextCharFormat_setFontFixedPitch(XTextCharFormat* self, bool fixedPitch);
/** @brief 取等宽（对标 fontFixedPitch）。 */
bool XTextCharFormat_fontFixedPitch(const XTextCharFormat* self);

/** @brief 设像素字号（对标 setFontPixelSize→FontPixelSize int）。 */
void XTextCharFormat_setFontPixelSize(XTextCharFormat* self, int pixels);
/** @brief 取像素字号（对标 fontPixelSize：intProperty）。 */
int XTextCharFormat_fontPixelSize(const XTextCharFormat* self);

/** @brief 设垂直对齐（对标 setVerticalAlignment→TextVerticalAlignment）。 */
void XTextCharFormat_setVerticalAlignment(XTextCharFormat* self, int alignment);
/** @brief 取垂直对齐（对标 verticalAlignment：intProperty，未设
 *         AlignNormal=0）。 */
int XTextCharFormat_verticalAlignment(const XTextCharFormat* self);

/** @brief 设上标基线%（对标 setSuperScriptBaseline→0x2025）。 */
void XTextCharFormat_setSuperScriptBaseline(XTextCharFormat* self, double baseline);
/** @brief 取上标基线%（未设回落 50.0，qtextformat.h:548）。 */
double XTextCharFormat_superScriptBaseline(const XTextCharFormat* self);

/** @brief 设下标基线%（对标 setSubScriptBaseline→0x2026）。 */
void XTextCharFormat_setSubScriptBaseline(XTextCharFormat* self, double baseline);
/** @brief 取下标基线%（未设回落 100.0/6.0，qtextformat.h:553）。 */
double XTextCharFormat_subScriptBaseline(const XTextCharFormat* self);

/** @brief 设基线偏移%（对标 setBaselineOffset→0x2027）。 */
void XTextCharFormat_setBaselineOffset(XTextCharFormat* self, double offset);
/** @brief 取基线偏移%（未设回落 0.0，qtextformat.h:558）。 */
double XTextCharFormat_baselineOffset(const XTextCharFormat* self);

/** @brief 设锚点标志（对标 setAnchor→IsAnchor bool）。 */
void XTextCharFormat_setAnchor(XTextCharFormat* self, bool anchor);
/** @brief 取锚点标志（对标 isAnchor）。 */
bool XTextCharFormat_isAnchor(const XTextCharFormat* self);

/** @brief 设锚点 href（对标 setAnchorHref→AnchorHref String）。 */
void XTextCharFormat_setAnchorHref(XTextCharFormat* self, const char* value);
/**
 * @brief 取锚点 href（对标 anchorHref：stringProperty）。
 * @return 借用 UTF-8（随写操作失效）；未设 ""。NULL self 同。
 */
const char* XTextCharFormat_anchorHref(const XTextCharFormat* self);

/** @brief 设前景色（对标 setForeground→ForegroundBrush；ARGB 承载）。 */
void XTextCharFormat_setForeground(XTextCharFormat* self, uint32_t argb);
/** @brief 取前景色（对标 foreground：0=无效色，Qt 的 NoBrush 口径）。 */
uint32_t XTextCharFormat_foreground(const XTextCharFormat* self);
/** @brief 清前景（对标 clearForeground→clearProperty）。 */
void XTextCharFormat_clearForeground(XTextCharFormat* self);

/** @brief 设背景色（对标 setBackground→BackgroundBrush；ARGB 承载）。 */
void XTextCharFormat_setBackground(XTextCharFormat* self, uint32_t argb);
/** @brief 取背景色（对标 background：0=无效色）。 */
uint32_t XTextCharFormat_background(const XTextCharFormat* self);
/** @brief 清背景（对标 clearBackground→clearProperty）。 */
void XTextCharFormat_clearBackground(XTextCharFormat* self);

/** @brief 设下划线色（对标 setUnderlineColor→TextUnderlineColor）。 */
void XTextCharFormat_setUnderlineColor(XTextCharFormat* self, uint32_t argb);
/** @brief 取下划线色（对标 underlineColor：0=无效色）。 */
uint32_t XTextCharFormat_underlineColor(const XTextCharFormat* self);

/* ==================== 类型化包装：BlockFormat ==================== */

/** @brief 初始化段落格式（type=BlockFormat 的薄包装）。 */
void XTextBlockFormat_init(XTextBlockFormat* self);
/** @brief 构造段落格式值。 */
XTextBlockFormat XTextBlockFormat_make(void);
/** @brief 从 XTextFormat 转换（type 改写为 BlockFormat）。 */
void XTextBlockFormat_convert(XTextBlockFormat* dst, const XTextFormat* src);

/**
 * @brief      设段落对齐（对标 setAlignment→BlockAlignment int 位组合；
 *         XTDAlignment 同值空间，见 Widget/XTextDocument.h）。
 */
void XTextBlockFormat_setAlignment(XTextBlockFormat* self, int alignment);

/**
 * @brief      取段落对齐（对标 alignment，qtextformat.h:622-624：intProperty
 *         读数，**0 回落 AlignLeft=0x01**——Qt 缺省口径）。
 */
int XTextBlockFormat_alignment(const XTextBlockFormat* self);

/** @brief 设段上边距（对标 setTopMargin→BlockTopMargin double）。 */
void XTextBlockFormat_setTopMargin(XTextBlockFormat* self, double margin);
/** @brief 取段上边距（未设 0.0）。 */
double XTextBlockFormat_topMargin(const XTextBlockFormat* self);
/** @brief 设段下边距（对标 setBottomMargin）。 */
void XTextBlockFormat_setBottomMargin(XTextBlockFormat* self, double margin);
/** @brief 取段下边距（未设 0.0）。 */
double XTextBlockFormat_bottomMargin(const XTextBlockFormat* self);
/** @brief 设段左边距（对标 setLeftMargin）。 */
void XTextBlockFormat_setLeftMargin(XTextBlockFormat* self, double margin);
/** @brief 取段左边距（未设 0.0）。 */
double XTextBlockFormat_leftMargin(const XTextBlockFormat* self);
/** @brief 设段右边距（对标 setRightMargin）。 */
void XTextBlockFormat_setRightMargin(XTextBlockFormat* self, double margin);
/** @brief 取段右边距（未设 0.0）。 */
double XTextBlockFormat_rightMargin(const XTextBlockFormat* self);

/** @brief 设首行缩进（对标 setTextIndent→TextIndent double）。 */
void XTextBlockFormat_setTextIndent(XTextBlockFormat* self, double indent);
/** @brief 取首行缩进（未设 0.0）。 */
double XTextBlockFormat_textIndent(const XTextBlockFormat* self);

/** @brief 设段缩进级（对标 setIndent→BlockIndent int）。 */
void XTextBlockFormat_setIndent(XTextBlockFormat* self, int indent);
/** @brief 取段缩进级（对标 indent：intProperty，未设 0）。 */
int XTextBlockFormat_indent(const XTextBlockFormat* self);

/** @brief 设标题级（对标 setHeadingLevel→HeadingLevel int）。 */
void XTextBlockFormat_setHeadingLevel(XTextBlockFormat* self, int level);
/** @brief 取标题级（对标 headingLevel：未设 0）。 */
int XTextBlockFormat_headingLevel(const XTextBlockFormat* self);

/** @brief 设行高与类型（对标 setLineHeight：双键同设）。 */
void XTextBlockFormat_setLineHeight(XTextBlockFormat* self, double height,
                                    int heightType);
/** @brief 取行高（对标 lineHeight()：doubleProperty，未设 0.0）。 */
double XTextBlockFormat_lineHeight(const XTextBlockFormat* self);
/** @brief 取行高类型（对标 lineHeightType：intProperty，未设 0）。 */
int XTextBlockFormat_lineHeightType(const XTextBlockFormat* self);

/* ==================== 类型化包装：ListFormat ==================== */

/**
 * @brief      初始化列表格式（type=ListFormat 的薄包装；对标
 *         QTextListFormat() 构造，qtextformat.cpp:2644-2648：**落两个缺省
 *         属性 setIndent(1)+setStart(1)**——与 BlockFormat 的零属性缺省
 *         不同， isEmpty 为 false）。
 */
void XTextListFormat_init(XTextListFormat* self);
/**
 * @brief      构造列表格式值（同 init 缺省口径：indent=1、start=1）。
 */
XTextListFormat XTextListFormat_make(void);
/** @brief 从 XTextFormat 转换（type 改写为 ListFormat；**不补**构造缺省
 *         属性——对齐 QTextListFormat(const QTextFormat&) 直取原属性）。 */
void XTextListFormat_convert(XTextListFormat* dst, const XTextFormat* src);

/** @brief 设列表风格（对标 setStyle→ListStyle int）。 */
void XTextListFormat_setStyle(XTextListFormat* self, int style);
/** @brief 取列表风格（对标 style：intProperty，未设 0=StyleUndefined）。 */
int XTextListFormat_style(const XTextListFormat* self);

/** @brief 设列表缩进（对标 setIndent→ListIndent int）。 */
void XTextListFormat_setIndent(XTextListFormat* self, int indent);
/** @brief 取列表缩进（对标 indent：未设 0）。 */
int XTextListFormat_indent(const XTextListFormat* self);

/** @brief 设序号前缀（对标 setNumberPrefix→ListNumberPrefix String）。 */
void XTextListFormat_setNumberPrefix(XTextListFormat* self, const char* prefix);
/** @brief 取序号前缀（借用 UTF-8；未设 ""）。 */
const char* XTextListFormat_numberPrefix(const XTextListFormat* self);

/** @brief 设序号后缀（对标 setNumberSuffix→ListNumberSuffix String）。 */
void XTextListFormat_setNumberSuffix(XTextListFormat* self, const char* suffix);
/** @brief 取序号后缀（借用 UTF-8；未设 ""）。 */
const char* XTextListFormat_numberSuffix(const XTextListFormat* self);

/** @brief 设列表起始号（对标 setStart→ListStart int）。 */
void XTextListFormat_setStart(XTextListFormat* self, int start);
/** @brief 取列表起始号（对标 start：未设 0）。 */
int XTextListFormat_start(const XTextListFormat* self);

/* ==================== 类型化包装：ImageFormat ==================== */

/** @brief 初始化图片格式（type=CharFormat + ObjectType=ImageObject——
 *         对标 QTextImageFormat 构造的 objectType 落表口径）。 */
void XTextImageFormat_init(XTextImageFormat* self);
/** @brief 构造图片格式值。 */
XTextImageFormat XTextImageFormat_make(void);
/** @brief 从 XTextFormat 转换（type=CharFormat + ObjectType=ImageObject）。 */
void XTextImageFormat_convert(XTextImageFormat* dst, const XTextFormat* src);

/** @brief 设图片资源名（对标 setName→ImageName String）。 */
void XTextImageFormat_setName(XTextImageFormat* self, const char* name);
/** @brief 取图片资源名（借用 UTF-8；未设 ""）。 */
const char* XTextImageFormat_name(const XTextImageFormat* self);

/** @brief 设图片显示宽（对标 setWidth→ImageWidth double）。 */
void XTextImageFormat_setWidth(XTextImageFormat* self, double width);
/** @brief 取图片显示宽（未设 0.0）。 */
double XTextImageFormat_width(const XTextImageFormat* self);

/** @brief 设图片显示高（对标 setHeight→ImageHeight double）。 */
void XTextImageFormat_setHeight(XTextImageFormat* self, double height);
/** @brief 取图片显示高（未设 0.0）。 */
double XTextImageFormat_height(const XTextImageFormat* self);

#ifdef __cplusplus
}
#endif

#endif /* XTEXTDOCUMENT_ON */

#endif /* XGUI_ON */
#endif /* XTEXTFORMAT_H */
