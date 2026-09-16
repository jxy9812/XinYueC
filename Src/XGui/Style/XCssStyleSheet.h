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
 * @brief      CSS 属性 ID（对标 qcssparser_p.h 的 Property 枚举子集，
 *             覆盖控件样式常用属性；数值与 Qt 对齐处保持相邻语义）。
 */
typedef enum XCssProperty
{
    XCssProperty_Unknown = 0,
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
    XCssProperty_TextDecoration
} XCssProperty;

/**
 * @brief      伪类（对标 Pseudo：pseudoClass 位子集，映射 State 位）。
 */
typedef enum XCssPseudoClass
{
    XCssPseudo_None = 0x0,
    XCssPseudo_Enabled = 0x1,
    XCssPseudo_Disabled = 0x2,
    XCssPseudo_Hover = 0x4,
    XCssPseudo_Pressed = 0x8,
    XCssPseudo_Focus = 0x10,
    XCssPseudo_Checked = 0x20,
    XCssPseudo_Selected = 0x40,
    XCssPseudo_ReadOnly = 0x80
} XCssPseudoClass;

/**
 * @brief 声明（对标 Declaration：propertyId + important + 值字符串）。
 */
typedef struct XCssDeclaration
{
    XCssProperty m_propertyId; /**< 属性 ID。 */
    bool m_important;          /**< !important。 */
    XString* m_value;          /**< 原始值（对象拥有；如 "#FF0000"、"12px"）。 */
} XCssDeclaration;

/**
 * @brief      选择器关系（对标 BasicSelector::Relation 子集）。
 */
typedef enum XCssRelation
{
    XCssRelation_None = 0,      /**< 无（单选择器或末段）。 */
    XCssRelation_Ancestor,      /**< 后代（空格分隔：A B 匹配 B 且 A 为祖先）。 */
    XCssRelation_Parent         /**< 子代（> 分隔：A > B 匹配 B 且 A 为直接父）。 */
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
 * @brief 基础选择器（对标 BasicSelector：元素名/ID/伪类/属性/关系）。
 */
typedef struct XCssBasicSelector
{
    XString* m_elementName;   /**< 元素名（对象拥有；如 "XLineEdit"；空=通配）。 */
    XString* m_id;            /**< ID 选择器（对象拥有；#objectName）。 */
    uint32_t m_pseudoClasses; /**< XCssPseudoClass 位组合。 */
    XCssAttributeSelector m_attribute; /**< 属性选择器（对标 attributeSelectors）。 */
    XCssRelation m_relationToPrev; /**< 与前一段的关系（首段为 None）。 */
} XCssBasicSelector;

/**
 * @brief 选择器（对标 Selector：基础选择器链 + specificity）。
 *
 *        支持逗号分隔的多选择器与关系链（"A B" 后代 / "A > B" 子代），
 *        匹配沿 XObject parent 链逐段验证。
 */
typedef struct XCssSelector
{
    XCssBasicSelector* m_basics; /**< 基础选择器链（堆；对象拥有）。 */
    int m_basicCount;            /**< 链长（>=1）。 */
    int m_specificity;           /**< 特异度（id*100 + class*10 + element）。 */
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
 * @brief 样式表（对标 StyleSheet：规则集合）。
 */
typedef struct XCssStyleSheet
{
    XCssStyleRule* m_rules;   /**< 规则数组（堆；对象拥有）。 */
    int m_ruleCount;          /**< 规则数。 */
    int m_ruleCapacity;       /**< 容量。 */
} XCssStyleSheet;

/**
 * @brief 初始化样式表。
 *
 * @param sheet 目标样式表指针，不能为空。
 * @return 无返回值。
 */
void XCssStyleSheet_init(XCssStyleSheet* sheet);

/**
 * @brief 释放样式表（清空全部规则）。
 *
 * @param sheet 目标样式表指针。
 * @return 无返回值。
 */
void XCssStyleSheet_clear(XCssStyleSheet* sheet);

/**
 * @brief 解析 CSS 文本（对标 cssParser 的规则子集）。
 *
 *        支持：selector { prop: value; }、逗号组合选择器、元素名/
 *        #ID/:pseudo、注释 /* *\/、!important。属性名不识别时忽略。
 *
 * @param css UTF-8 CSS 文本（可空）。
 * @param sheet 输出样式表（先 clear 再填充）。
 * @return 解析成功返回 true；语法错误返回 false（已解析部分保留）。
 */
bool XCssStyleSheet_parse(XCssStyleSheet* sheet, const char* css);

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
