#ifndef XCSSSTYLESHEET_H
#define XCSSSTYLESHEET_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XString.h"

#if XSTYLE_ON

/**
 * @brief      CSS 属性 ID（对标 Qt 6.8.3 qcssparser_p.h 的 enum Property 全集；
 *             枚举数值为本库自有序，与 Qt 数值不对应；CSS 属性名 → 枚举的
 *             映射名表见 XCssStyleSheet.c 的 k_propNames，其内容=6.8.3
 *             qcssparser.cpp properties[] 全集的逐名转录（120 名，含 -qt-*
 *             私有项；口径=排序去重表、精确匹配——6.8.3 原表为字母序去重
 *             表、Qt 查找为大小写敏感精确匹配，本表同序同集、线性扫大小写
 *             不敏感为已声明偏差）。
 *             首批既有枚举值保持原序不变，新增枚举全部尾部追加）。
 */
typedef enum XCssProperty
{
    XCssProperty_Unknown = 0, /**< 未识别属性（对标 UnknownProperty；原名存
                                   XCssDeclaration::m_propertyName）。 */
    XCssProperty_Color,
    XCssProperty_BackgroundColor,
    XCssProperty_Border,
    XCssProperty_BorderColor,
    XCssProperty_BorderWidth,
    XCssProperty_BorderStyle,
    XCssProperty_BorderRadius,
    XCssProperty_Padding,
    XCssProperty_PaddingLeft,
    XCssProperty_PaddingRight,
    XCssProperty_PaddingTop,
    XCssProperty_PaddingBottom,
    XCssProperty_Margin,
    XCssProperty_Font,
    XCssProperty_FontFamily,
    XCssProperty_FontSize,
    XCssProperty_FontWeight,
    XCssProperty_FontStyle,
    XCssProperty_MinWidth,
    XCssProperty_MinHeight,
    XCssProperty_MaxWidth,
    XCssProperty_MaxHeight,
    XCssProperty_Width,
    XCssProperty_Height,
    XCssProperty_Background,
    XCssProperty_TextDecoration,
    /* ---- 以下为 Qt 6.8.3 属性名表对齐新增（尾部追加，不改既有值） ---- */
    XCssProperty_Float,
    XCssProperty_MarginTop,
    XCssProperty_MarginBottom,
    XCssProperty_MarginLeft,
    XCssProperty_MarginRight,
    XCssProperty_BorderBottomColor,
    XCssProperty_BorderBottomLeftRadius,
    XCssProperty_BorderBottomRightRadius,
    XCssProperty_BorderBottomStyle,
    XCssProperty_BorderBottomWidth,
    XCssProperty_BorderImage,
    XCssProperty_BorderLeftColor,
    XCssProperty_BorderLeftStyle,
    XCssProperty_BorderLeftWidth,
    XCssProperty_BorderRightColor,
    XCssProperty_BorderRightStyle,
    XCssProperty_BorderRightWidth,
    XCssProperty_BorderTopColor,
    XCssProperty_BorderTopLeftRadius,
    XCssProperty_BorderTopRightRadius,
    XCssProperty_BorderTopStyle,
    XCssProperty_BorderTopWidth,
    XCssProperty_BorderBottom,
    XCssProperty_BorderLeft,
    XCssProperty_BorderRight,
    XCssProperty_BorderTop,
    XCssProperty_BorderCollapse,
    XCssProperty_BackgroundImage,
    XCssProperty_BackgroundRepeat,
    XCssProperty_BackgroundPosition,
    XCssProperty_BackgroundAttachment,
    XCssProperty_BackgroundClip,
    XCssProperty_BackgroundOrigin,
    XCssProperty_TextIndent,
    XCssProperty_TextUnderlineStyle,
    XCssProperty_VerticalAlignment,
    XCssProperty_Whitespace,
    XCssProperty_Left,
    XCssProperty_Right,
    XCssProperty_Top,
    XCssProperty_Bottom,
    XCssProperty_Position,
    XCssProperty_Outline,
    XCssProperty_OutlineColor,
    XCssProperty_OutlineOffset,
    XCssProperty_OutlineStyle,
    XCssProperty_OutlineRadius,
    XCssProperty_OutlineWidth,
    XCssProperty_OutlineBottomLeftRadius,
    XCssProperty_OutlineBottomRightRadius,
    XCssProperty_OutlineTopLeftRadius,
    XCssProperty_OutlineTopRightRadius,
    XCssProperty_ListStyle,
    XCssProperty_ListStyleType,
    XCssProperty_ListStyleImage,
    XCssProperty_ListStylePosition,
    XCssProperty_Alignment,
    XCssProperty_Image,
    XCssProperty_QtImage,
    XCssProperty_IconSize,
    XCssProperty_QtIcon,
    XCssProperty_GridlineColor,
    XCssProperty_SelectionBackgroundColor,
    XCssProperty_SelectionColor,
    XCssProperty_DialogButtonButtonIcons,
    XCssProperty_DialogButtonButtonLayoutPolicy,
    XCssProperty_TransparentButton,
    XCssProperty_AlternateBackgroundColor,
    XCssProperty_ShowDecorationSelected,
    XCssProperty_Title,
    XCssProperty_Icon,
    XCssProperty_TextAlignment,
    XCssProperty_QtAlignment,
    XCssProperty_QtBackgroundRole,
    XCssProperty_FontVariant,
    XCssProperty_TextTransform,
    XCssProperty_LineHeight,
    XCssProperty_FontKerning,
    XCssProperty_LetterSpacing,
    XCssProperty_WordSpacing,
    XCssProperty_QtStrokeBrush,
    XCssProperty_QtStrokePen,
    XCssProperty_QtForeground,
    XCssProperty_QtAccent,
    XCssProperty_QtPlaceholderTextColor,
    /* ---- -qt-* 富文本私有属性（对标 properties[] 名表 -qt-* 项） ---- */
    XCssProperty_QtBlockIndent,
    XCssProperty_QtListIndent,
    XCssProperty_QtListNumberPrefix,
    XCssProperty_QtListNumberSuffix,
    XCssProperty_QtParagraphType,
    XCssProperty_QtStyle,
    XCssProperty_QtTableType,
    XCssProperty_QtUserState,
    /* ---- 6.8.3 属性名表对齐第二批新增（尾部追加，不改既有值） ---- */
    XCssProperty_TextDecorationColor,
    XCssProperty_PageBreakAfter,
    XCssProperty_PageBreakBefore,
    XCssProperty_QtStrokeColor,
    XCssProperty_QtStrokeWidth,
    XCssProperty_QtStrokeDashArray,
    XCssProperty_QtStrokeDashOffset,
    XCssProperty_QtStrokeLineCap,
    XCssProperty_QtStrokeLineJoin,
    XCssProperty_QtStrokeMiterLimit,
    XCssProperty_QtStyleFeatures,
    XCssProperty_QtForegroundTextureCacheKey,
    XCssProperty_QtLineHeightType,
    XCssProperty_QtImageAlignment,
    XCssProperty_QtSpacing,
    XCssProperty_QtOrigin,
    XCssProperty_QtPosition
} XCssProperty;

/**
 * @brief      伪类（对标 Pseudo：pseudoClass 位子集，映射 State 位）。
 *
 *             位语义逐项对标 Qt 6.8.3 qstylesheetstyle.cpp 的
 *             pseudoClass(QStyle::State)（State 位 → 伪类位的派生）与
 *             qcssparser.cpp pseudos[] 名表（伪类名 → 位）。Qt 名表
 *             "on"/"off" 无独立 On/Off 位（"on"→Checked、"off"→
 *             Unchecked），故本枚举不设 On/Off 位；可由
 *             xsss_statePseudos 从 XStyleState 位派生的位全集如下
 *             （尾部追加，既有值不变）。
 */
typedef enum XCssPseudoClass
{
    XCssPseudo_None = 0x0,
    XCssPseudo_Enabled = 0x1,       /**< :enabled（State_Enabled）。 */
    XCssPseudo_Disabled = 0x2,      /**< :disabled（!State_Enabled）。 */
    XCssPseudo_Hover = 0x4,         /**< :hover（State_MouseOver，Qt 语义
                                         仅 Enabled 分支内派生）。 */
    XCssPseudo_Pressed = 0x8,       /**< :pressed（State_Sunken）。 */
    XCssPseudo_Focus = 0x10,        /**< :focus（State_HasFocus）。 */
    XCssPseudo_Checked = 0x20,      /**< :checked（State_On；Qt 名 "on" 同入位）。 */
    XCssPseudo_Selected = 0x40,     /**< :selected（State_Selected）。 */
    XCssPseudo_ReadOnly = 0x80,     /**< :read-only（State_ReadOnly；Qt 名
                                         read-only，兼容旧拼写 readonly）。 */
    /* ---- 以下为伪类全映射对齐新增（尾部追加，不改既有值） ---- */
    XCssPseudo_Active = 0x100,      /**< :active（State_Active，Qt 名
                                         "active"→PseudoClass_Active）。 */
    XCssPseudo_Window = 0x200,      /**< :window（State_Window）。 */
    XCssPseudo_Unchecked = 0x400,   /**< :unchecked/:off（State_Off；Qt 名
                                         off/unchecked 均→Unchecked）。 */
    XCssPseudo_Indeterminate = 0x800, /**< :indeterminate（State_NoChange）。 */
    XCssPseudo_Open = 0x1000,       /**< :open（State_Open|On|Sunken 任一）。 */
    XCssPseudo_Closed = 0x2000,     /**< :closed（上述三位的显式负态：
                                         不满足即无条件发 Closed，Qt
                                         pseudoClass 的 else 分支）。 */
    XCssPseudo_Children = 0x4000,   /**< :has-children（State_Children）。 */
    XCssPseudo_Sibling = 0x8000,    /**< :has-siblings（State_Sibling）。 */
    XCssPseudo_Item = 0x10000,      /**< :item/:adjoins-item（State_Item）。 */
    XCssPseudo_Horizontal = 0x20000,  /**< :horizontal（State_Horizontal）。 */
    XCssPseudo_Vertical = 0x40000   /**< :vertical（Qt：!State_Horizontal
                                         时无条件派生，即缺省垂直）。 */
} XCssPseudoClass;

/**
 * @brief 声明（对标 Declaration：propertyId + important + 值字符串）。
 */
typedef struct XCssDeclaration
{
    XCssProperty m_propertyId; /**< 属性 ID。 */
    bool m_important;          /**< !important。 */
    XString* m_value;          /**< 原始值（对象拥有；如 "#FF0000"、"12px"）。 */
    XString* m_propertyName;   /**< 属性原名（对象拥有；对标 Declaration::property
                                    始终保留原名，未知属性时配
                                    XCssProperty_Unknown 使用）。 */
} XCssDeclaration;

/**
 * @brief      选择器关系（对标 Qt BasicSelector::Relation；Qt 存于前段的
 *             relationToNext，本库等价存于后段 m_relationToPrev）。
 */
typedef enum XCssRelation
{
    XCssRelation_None = 0,      /**< 无（单选择器或末段）。 */
    XCssRelation_Ancestor,      /**< 后代（空格分隔：A B 匹配 B 且 A 为祖先，
                                     对标 MatchNextSelectorIfAncestor）。 */
    XCssRelation_Parent,        /**< 子代（> 分隔：A > B 匹配 B 且 A 为直接父，
                                     对标 MatchNextSelectorIfParent）。 */
    XCssRelation_DirectAdjacent,/**< 相邻兄弟（+ 分隔，对标
                                     MatchNextSelectorIfDirectAdjecent）。 */
    XCssRelation_IndirectAdjacent /**< 通用兄弟（~ 分隔，对标
                                     MatchNextSelectorIfIndirectAdjecent）。 */
} XCssRelation;

/**
 * @brief      值匹配准则（对标 AttributeSelector::ValueMatchType）。
 */
typedef enum XCssValueMatch
{
    XCssValueMatch_NoMatch = 0,     /**< 无匹配要求（[attr] 存在判定）。 */
    XCssValueMatch_Equal,           /**< [attr=value]。 */
    XCssValueMatch_Includes,        /**< [attr~=value] 空格分词包含。 */
    XCssValueMatch_DashMatch,       /**< [attr|=value] 前缀或 value- 开头。 */
    XCssValueMatch_BeginsWith,      /**< [attr^=value]。 */
    XCssValueMatch_EndsWith,        /**< [attr$=value]。 */
    XCssValueMatch_Contains         /**< [attr*=value]。 */
} XCssValueMatch;

/**
 * @brief 属性选择器（对标 AttributeSelector）。
 */
typedef struct XCssAttributeSelector
{
    XString* m_name;   /**< 属性名（对象拥有）。 */
    XString* m_value;  /**< 属性值（对象拥有；NULL=仅要求属性存在）。 */
    XCssValueMatch m_match; /**< 匹配准则（默认 NoMatch）。 */
} XCssAttributeSelector;

/**
 * @brief 基础选择器（对标 BasicSelector：元素名/ID/伪类/伪元素/属性/关系）。
 *
 *        多 ID（#a#b）与多属性（[a][b]）对标 Qt 的 ids/attributeSelectors
 *        列表全量存储：首 ID 存 m_id、首属性存 m_attribute（与旧版一致，
 *        供既有消费端使用），第 2 个起分别进 m_ids/m_attributes。
 */
typedef struct XCssBasicSelector
{
    XString* m_elementName;   /**< 元素名（对象拥有；如 "XLineEdit"；空=通配）。 */
    XString* m_id;            /**< 首个 ID 选择器（对象拥有；#objectName）。 */
    uint32_t m_pseudoClasses; /**< XCssPseudoClass 位组合。 */
    XCssAttributeSelector m_attribute; /**< 首个属性选择器（对标 attributeSelectors[0]）。 */
    XCssRelation m_relationToPrev; /**< 与前一段的关系（首段为 None）。 */
    XString* m_pseudoElement;  /**< 伪元素/子控件名（对象拥有；'::name' 双冒号
                                    记录；NULL=无；对标 Qt Selector::pseudoElement
                                    ——名字未入伪类名表的伪记为伪元素/子控件）。 */
    XString** m_ids;           /**< 第 2..n 个 ID 选择器（对象拥有数组与元素）。 */
    int m_idCount;             /**< m_ids 元素数。 */
    XCssAttributeSelector* m_attributes; /**< 第 2..n 个属性选择器
                                             （对象拥有数组与内部字符串）。 */
    int m_attributeCount;      /**< m_attributes 元素数。 */
} XCssBasicSelector;

/**
 * @brief 选择器（对标 Selector：基础选择器链 + specificity）。
 *
 *        支持逗号分隔的多选择器与关系链（"A B" 后代 / "A > B" 子代 /
 *        "A + B" 相邻 / "A ~ B" 通用兄弟），匹配沿 XObject parent 链
 *        逐段验证、'+'/'~' 经 XObject_previousSibling 兄弟导航落地
 *        （Qt 6.8.3 的 QStyleSheetStyleSelector::previousSiblingNode
 *        为恒空桩，'+'/'~' 在 Qt QSS 中永不命中；本库按 QCss 遍历
 *        意图提供真实导航，超出而非偏离 Qt）。特异度对标 Selector::
 *        specificity()：#id（含多 ID 逐个）各 0x100、伪类/属性/.class
 *        各 0x10、elementName 1。已声明偏差：伪元素不计权——Qt 6.8.3
 *        将伪元素存 pseudos[0] 并计 pseudos.size()×0x10，本库伪元素
 *        独立存储（m_pseudoElement）不参与计权；查询按伪元素分流，
 *        仅影响伪元素规则之间的相对位阶。
 */
typedef struct XCssSelector
{
    XCssBasicSelector* m_basics; /**< 基础选择器链（堆；对象拥有）。 */
    int m_basicCount;            /**< 链长（>=1）。 */
    int m_specificity;           /**< 特异度（逐段累加：#id=0x100、伪类/属性/.class 各 0x10、element=1；伪元素不计权，见类型注释；对标 Qt 6.8.3 qcssparser Selector::specificity）。 */
} XCssSelector;

/**
 * @brief 样式规则（对标 StyleRule：选择器 + 声明表）。
 */
typedef struct XCssStyleRule
{
    XCssSelector* m_selectors;    /**< 选择器数组（堆；对象拥有）。 */
    int m_selectorCount;          /**< 选择器数。 */
    XCssDeclaration* m_declarations; /**< 声明数组（堆；对象拥有）。 */
    int m_declarationCount;       /**< 声明数。 */
} XCssStyleRule;

/**
 * @brief 引入规则（对标 ImportRule：@import href + 媒体列表；仅解析存储，
 *        不发起加载，对标 QSS 消费端行为）。
 */
typedef struct XCssImportRule
{
    XString* m_href;   /**< 引入目标（对象拥有；"..." 字符串或 url(...) 剥壳后）。 */
    XString** m_media; /**< 媒体名列表（对象拥有数组与元素；对标 ImportRule::media）。 */
    int m_mediaCount;  /**< 媒体名数。 */
} XCssImportRule;

/**
 * @brief 媒体规则（对标 MediaRule：@media 名单 + 内嵌规则集；QSS 消费端
 *        不执行 media 判定，解析存储即对齐——定性证据（v6.8.3 原文）：
 *        评估入口仅 qcssparser.cpp:2232-2240 styleRulesForNode 的
 *        `if (!medium.isEmpty())` 分支，medium 为 StyleSelector 公有
 *        成员（qcssparser_p.h:661，默认空串）且 widget 侧
 *        qstylesheetstyle.cpp 全文零处赋值（QStyleSheetStyleSelector
 *        :1545-1651 未触），故 widget 口径恒不评估；详见 XGui.md G5 条目）。
 */
typedef struct XCssMediaRule
{
    XString** m_media;       /**< 媒体名列表（对象拥有数组与元素）。 */
    int m_mediaCount;        /**< 媒体名数。 */
    XCssStyleRule* m_rules;  /**< 内嵌规则集数组（堆；对象拥有）。 */
    int m_ruleCount;         /**< 内嵌规则数。 */
} XCssMediaRule;

/**
 * @brief 分页规则（对标 PageRule：@page 选择器 + 声明表）。
 */
typedef struct XCssPageRule
{
    XString* m_selector;             /**< 页选择器名（对象拥有；@page 后的可选
                                          名与 :pseudoPage，对标 PageRule::selector）。 */
    XCssDeclaration* m_declarations; /**< 声明数组（堆；对象拥有）。 */
    int m_declarationCount;          /**< 声明数。 */
} XCssPageRule;

/**
 * @brief 样式表（对标 StyleSheet：规则 + at 规则集合）。
 */
typedef struct XCssStyleSheet
{
    XCssStyleRule* m_rules;   /**< 规则数组（堆；对象拥有）。 */
    int m_ruleCount;          /**< 规则数。 */
    int m_ruleCapacity;       /**< 容量。 */
    XCssImportRule* m_imports;    /**< @import 规则数组（堆；对象拥有）。 */
    int m_importCount;            /**< @import 规则数。 */
    XCssMediaRule* m_mediaRules;  /**< @media 规则数组（堆；对象拥有）。 */
    int m_mediaRuleCount;         /**< @media 规则数。 */
    XCssPageRule* m_pageRules;    /**< @page 规则数组（堆；对象拥有）。 */
    int m_pageRuleCount;          /**< @page 规则数。 */
} XCssStyleSheet;

/**
 * @brief 解析错误信息（对标 QCss::Parser 的 errorIndex/errorSymbol）。
 *
 *        m_message 指向静态字符串（静态存储期，调用方无需释放）；
 *        偏移坐标与 Qt 一致取词法解码后的缓冲（Qt 的 Symbol::start 亦为
 *        Scanner::preprocess 之后的坐标）。
 */
typedef struct XCssParseError
{
    const char* m_message; /**< 错误描述（静态存储期）；成功时为 NULL。 */
    int m_offset;          /**< 出错字节偏移（0 起；解码后缓冲坐标）。 */
    int m_line;            /**< 出错行号（1 起；按解码缓冲内换行计数）。 */
} XCssParseError;

/**
 * @brief 初始化样式表。
 *
 * @param sheet 目标样式表指针，不能为空。
 * @return 无返回值。
 */
void XCssStyleSheet_init(XCssStyleSheet* sheet);

/**
 * @brief 释放样式表（清空全部规则与 at 规则）。
 *
 * @param sheet 目标样式表指针。
 * @return 无返回值。
 */
void XCssStyleSheet_clear(XCssStyleSheet* sheet);

/**
 * @brief 解析 CSS 文本（对标 QCss::Parser::parseStyleSheet 规则子集）。
 *
 *        支持：selector { prop: value; }、逗号组合选择器、元素名/#ID/
 *        .Class/:伪类/::伪元素(子控件)、#a#b 多 ID、[a][b] 多属性、
 *        关系链 "A B"/"A > B"/"A + B"/"A ~ B"、注释、!important、
 *        \\XXXXXX 十六进制转义与 \\c 字面转义、@charset/@import/@media/
 *        @page（解析存储；未识别 at 关键字按 Qt 跳过）。
 *        属性名不识别时按 Qt 存为 XCssProperty_Unknown 并保留原名。
 *
 *        错误契约（对标 Qt 容忍度）：结构性错误返回 false——花括号失衡、
 *        选择器段无法归属的垃圾（段首关系符/空选择器/未闭合 '[' 等，
 *        对标 Qt parseSelector/parseSimpleSelector 的 minCount 语义）、
 *        at 规则结构错误（@charset 形态错误、@import/@media/@page
 *        解析失败）；声明块内部的坏声明按 Qt parseRuleset 容忍度跳过。
 *        解析失败时样式表被清空（对标 Qt：解析失败整表弃用）。
 *
 * @param css UTF-8 CSS 文本（可空，空串/纯空白视为成功空表）。
 * @param sheet 输出样式表（先 clear 再填充）。
 * @return 解析成功返回 true；结构性语法错误返回 false。
 */
bool XCssStyleSheet_parse(XCssStyleSheet* sheet, const char* css);

/**
 * @brief 解析 CSS 文本（parse 的扩展版，带错误出参）。
 *
 * @param sheet 输出样式表（先 clear 再填充；失败时被清空）。
 * @param css UTF-8 CSS 文本（可空）。
 * @param err 错误出参（可空；成功时 m_message 置 NULL）。
 * @return 解析成功返回 true；结构性语法错误返回 false 并填写 err
 *         （错误描述 + 字节偏移 + 行号）。
 */
bool XCssStyleSheet_parse_ex(XCssStyleSheet* sheet, const char* css,
                             XCssParseError* err);

/**
 * @brief 查询属性 ID 对应名（解析用）。
 *
 * @param id 属性 ID。
 * @return 属性名（小写 CSS 名）；未知返回 NULL。
 */
const char* XCssProperty_name(XCssProperty id);

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif
#endif /* XCSSSTYLESHEET_H */
