/******************************************************************************
 * @file       XConditionalFormatting.h
 * @brief      XConditionalFormatting 条件格式类（对标 QXlsx::ConditionalFormatting）
 * @author     XinYueC 团队
 * @note       提供条件格式管理，包括高亮规则、数据条、色阶等。
 *             对齐 QXlsx::ConditionalFormatting 全部功能
 ******************************************************************************/
#ifndef XCONDITIONALFORMATTING_H
#define XCONDITIONALFORMATTING_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XString.h"
#include "XVector.h"
#include "XColor.h"
#include "XFormat.h"
#include "XCellRange.h"
#include "XCellReference.h"

/** @brief 高亮规则类型枚举 */
typedef enum XConditionalFormatting_HighlightRuleType {
    XCF_Highlight_LessThan,            /**< 小于 */
    XCF_Highlight_LessThanOrEqual,     /**< 小于等于 */
    XCF_Highlight_Equal,               /**< 等于 */
    XCF_Highlight_NotEqual,            /**< 不等于 */
    XCF_Highlight_GreaterThanOrEqual,  /**< 大于等于 */
    XCF_Highlight_GreaterThan,         /**< 大于 */
    XCF_Highlight_Between,             /**< 介于 */
    XCF_Highlight_NotBetween,          /**< 不介于 */
    XCF_Highlight_ContainsText,        /**< 包含文本 */
    XCF_Highlight_NotContainsText,     /**< 不包含文本 */
    XCF_Highlight_BeginsWith,          /**< 开头是 */
    XCF_Highlight_EndsWith,            /**< 结尾是 */
    XCF_Highlight_TimePeriod,          /**< 时间周期 */
    XCF_Highlight_Duplicate,           /**< 重复值 */
    XCF_Highlight_Unique,              /**< 唯一值 */
    XCF_Highlight_Blanks,              /**< 空值 */
    XCF_Highlight_NoBlanks,            /**< 非空值 */
    XCF_Highlight_Errors,              /**< 错误值 */
    XCF_Highlight_NoErrors,            /**< 非错误值 */
    XCF_Highlight_Top,                 /**< 前N项 */
    XCF_Highlight_TopPercent,          /**< 前百分比 */
    XCF_Highlight_Bottom,              /**< 后N项 */
    XCF_Highlight_BottomPercent,       /**< 后百分比 */
    XCF_Highlight_AboveAverage,        /**< 高于平均值 */
    XCF_Highlight_AboveOrEqualAverage, /**< 高于或等于平均值 */
    XCF_Highlight_AboveStdDev1,        /**< 高于1个标准差 */
    XCF_Highlight_AboveStdDev2,        /**< 高于2个标准差 */
    XCF_Highlight_AboveStdDev3,        /**< 高于3个标准差 */
    XCF_Highlight_BelowAverage,        /**< 低于平均值 */
    XCF_Highlight_BelowOrEqualAverage, /**< 低于或等于平均值 */
    XCF_Highlight_BelowStdDev1,        /**< 低于1个标准差 */
    XCF_Highlight_BelowStdDev2,        /**< 低于2个标准差 */
    XCF_Highlight_BelowStdDev3,        /**< 低于3个标准差 */
    XCF_Highlight_Expression           /**< 表达式 */
} XConditionalFormatting_HighlightRuleType;

/** @brief 数值对象类型枚举 */
typedef enum XConditionalFormatting_ValueObjectType {
    XCF_VOT_Formula,     /**< 公式 */
    XCF_VOT_Max,         /**< 最大值 */
    XCF_VOT_Min,         /**< 最小值 */
    XCF_VOT_Num,         /**< 数值 */
    XCF_VOT_Percent,     /**< 百分比 */
    XCF_VOT_Percentile   /**< 百分位 */
} XConditionalFormatting_ValueObjectType;

/** @brief 条件格式规则类型枚举 */
typedef enum XConditionalFormatting_RuleType {
    XCF_Rule_HighlightCellsRule,  /**< 高亮单元格规则 */
    XCF_Rule_DataBar,             /**< 数据条 */
    XCF_Rule_ColorScale2,         /**< 双色阶 */
    XCF_Rule_ColorScale3          /**< 三色阶 */
} XConditionalFormatting_RuleType;

/** @brief 条件格式规则结构体 */
typedef struct XConditionalFormatting_Rule {
    XConditionalFormatting_RuleType m_ruleType;      /**< 规则类型 */
    XConditionalFormatting_HighlightRuleType m_highlightType; /**< 高亮类型 */
    XString* m_formula1;                              /**< 公式1 */
    XString* m_formula2;                              /**< 公式2 */
    XFormat* m_format;                                /**< 格式 */
    XColor m_color1;                                  /**< 颜色1（色阶/数据条） */
    XColor m_color2;                                  /**< 颜色2 */
    XColor m_color3;                                  /**< 颜色3 */
    XConditionalFormatting_ValueObjectType m_valType1; /**< 值类型1 */
    XConditionalFormatting_ValueObjectType m_valType2; /**< 值类型2 */
    bool m_showData;                                   /**< 是否显示数据条 */
    bool m_stopIfTrue;                                 /**< 如果为真则停止 */
} XConditionalFormatting_Rule;

/** @brief XConditionalFormatting 条件格式结构体 */
typedef struct XConditionalFormatting {
    XVector* m_rules;  /**< 规则列表，包含 XConditionalFormatting_Rule */
    XVector* m_ranges; /**< 范围列表，包含 XCellRange */
} XConditionalFormatting;

/* ========== 创建与初始化 ========== */
XConditionalFormatting* XConditionalFormatting_create(void);
XConditionalFormatting* XConditionalFormatting_copy(const XConditionalFormatting* other);
void XConditionalFormatting_delete(XConditionalFormatting* self);

/* ========== 规则添加 ========== */
bool XConditionalFormatting_addHighlightCellsRule(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const XFormat* format, bool stopIfTrue);
bool XConditionalFormatting_addHighlightCellsRule2(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const char* formula1,
    const XFormat* format, bool stopIfTrue);
bool XConditionalFormatting_addHighlightCellsRule3(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const char* formula1, const char* formula2,
    const XFormat* format, bool stopIfTrue);
bool XConditionalFormatting_addDataBarRule(XConditionalFormatting* self,
    const XColor* color, bool showData, bool stopIfTrue);
bool XConditionalFormatting_addDataBarRuleEx(XConditionalFormatting* self,
    const XColor* color, XConditionalFormatting_ValueObjectType type1, const char* val1,
    XConditionalFormatting_ValueObjectType type2, const char* val2,
    bool showData, bool stopIfTrue);
bool XConditionalFormatting_add2ColorScaleRule(XConditionalFormatting* self,
    const XColor* minColor, const XColor* maxColor, bool stopIfTrue);
bool XConditionalFormatting_add3ColorScaleRule(XConditionalFormatting* self,
    const XColor* minColor, const XColor* midColor, const XColor* maxColor, bool stopIfTrue);

/* ========== 范围管理 ========== */
XCellRange* XConditionalFormatting_ranges(const XConditionalFormatting* self, int* count);
void XConditionalFormatting_addCell(XConditionalFormatting* self, const XCellReference* cell);
void XConditionalFormatting_addCellRc(XConditionalFormatting* self, int row, int col);
void XConditionalFormatting_addRange(XConditionalFormatting* self, int firstRow, int firstCol, int lastRow, int lastCol);
void XConditionalFormatting_addRangeEx(XConditionalFormatting* self, const XCellRange* range);

/* ========== 内部接口 ========== */
int XConditionalFormatting_rulesCount(const XConditionalFormatting* self);
XConditionalFormatting_Rule* XConditionalFormatting_rule(const XConditionalFormatting* self, int index);

#ifdef __cplusplus
}
#endif
#endif /* XCONDITIONALFORMATTING_H */
