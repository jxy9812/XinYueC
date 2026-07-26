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
    XString* m_formula3;                              /**< 公式3/三色阶最大阈值 */
    XString* m_text;                                  /**< 文本匹配规则的文本 */
    XString* m_timePeriod;                            /**< 时间周期规则名称 */
    int m_rank;                                       /**< Top/Bottom 规则数量 */
    int m_stdDev;                                     /**< 平均值规则标准差数量 */
    XFormat* m_format;                                /**< 格式 */
    XColor m_color1;                                  /**< 颜色1（色阶/数据条） */
    XColor m_color2;                                  /**< 颜色2 */
    XColor m_color3;                                  /**< 颜色3 */
    XConditionalFormatting_ValueObjectType m_valType1; /**< 值类型1 */
    XConditionalFormatting_ValueObjectType m_valType2; /**< 值类型2 */
    XConditionalFormatting_ValueObjectType m_valType3; /**< 值类型3（三色阶） */
    bool m_showData;                                   /**< 是否显示数据条 */
    bool m_stopIfTrue;                                 /**< 如果为真则停止 */
} XConditionalFormatting_Rule;

/** @brief XConditionalFormatting 条件格式结构体 */
typedef struct XConditionalFormatting {
    XVector* m_rules;  /**< 规则列表，包含 XConditionalFormatting_Rule */
    XVector* m_ranges; /**< 范围列表，包含 XCellRange */
} XConditionalFormatting;

/* ========== 创建与初始化 ========== */

/**
 * @brief      创建条件格式对象
 * @return     新创建的条件格式对象指针
 */
XConditionalFormatting* XConditionalFormatting_create(void);

/**
 * @brief      复制条件格式对象
 * @param other 源条件格式对象
 * @return     新创建的条件格式对象副本
 */
XConditionalFormatting* XConditionalFormatting_copy(const XConditionalFormatting* other);

/**
 * @brief      销毁条件格式对象并释放资源
 * @param self 条件格式对象指针
 */
void XConditionalFormatting_delete(XConditionalFormatting* self);

/* ========== 规则添加 ========== */

/**
 * @brief      添加高亮单元格规则（简单版）
 * @param self        条件格式对象指针
 * @param type        高亮规则类型
 * @param format      格式
 * @param stopIfTrue  如果为真则停止
 * @return            成功返回 true
 */
bool XConditionalFormatting_addHighlightCellsRule(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const XFormat* format, bool stopIfTrue);

/**
 * @brief      添加高亮单元格规则（单公式版）
 * @param self        条件格式对象指针
 * @param type        高亮规则类型
 * @param formula1    公式1
 * @param format      格式
 * @param stopIfTrue  如果为真则停止
 * @return            成功返回 true
 */
bool XConditionalFormatting_addHighlightCellsRule2(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const XString* formula1,
    const XFormat* format, bool stopIfTrue);

/**
 * @brief      添加高亮单元格规则（双公式版）
 * @param self        条件格式对象指针
 * @param type        高亮规则类型
 * @param formula1    公式1
 * @param formula2    公式2
 * @param format      格式
 * @param stopIfTrue  如果为真则停止
 * @return            成功返回 true
 */
bool XConditionalFormatting_addHighlightCellsRule3(XConditionalFormatting* self,
    XConditionalFormatting_HighlightRuleType type, const XString* formula1, const XString* formula2,
    const XFormat* format, bool stopIfTrue);

/**
 * @brief      添加数据条规则（简单版）
 * @param self        条件格式对象指针
 * @param color       数据条颜色
 * @param showData    是否显示数据值
 * @param stopIfTrue  如果为真则停止
 * @return            成功返回 true
 */
bool XConditionalFormatting_addDataBarRule(XConditionalFormatting* self,
    const XColor* color, bool showData, bool stopIfTrue);

/**
 * @brief      添加数据条规则（完整版）
 * @param self        条件格式对象指针
 * @param color       数据条颜色
 * @param type1       值类型1
 * @param val1        值1
 * @param type2       值类型2
 * @param val2        值2
 * @param showData    是否显示数据值
 * @param stopIfTrue  如果为真则停止
 * @return            成功返回 true
 */
bool XConditionalFormatting_addDataBarRuleEx(XConditionalFormatting* self,
    const XColor* color, XConditionalFormatting_ValueObjectType type1, const XString* val1,
    XConditionalFormatting_ValueObjectType type2, const XString* val2,
    bool showData, bool stopIfTrue);

/**
 * @brief      添加双色阶规则
 * @param self        条件格式对象指针
 * @param minColor    最小值颜色
 * @param maxColor    最大值颜色
 * @param stopIfTrue  如果为真则停止
 * @return            成功返回 true
 */
bool XConditionalFormatting_add2ColorScaleRule(XConditionalFormatting* self,
    const XColor* minColor, const XColor* maxColor, bool stopIfTrue);

/**
 * @brief      添加三色阶规则
 * @param self        条件格式对象指针
 * @param minColor    最小值颜色
 * @param midColor    中间值颜色
 * @param maxColor    最大值颜色
 * @param stopIfTrue  如果为真则停止
 * @return            成功返回 true
 */
bool XConditionalFormatting_add3ColorScaleRule(XConditionalFormatting* self,
    const XColor* minColor, const XColor* midColor, const XColor* maxColor, bool stopIfTrue);

/* ========== 范围管理 ========== */

/**
 * @brief      获取所有验证范围
 * @param self  条件格式对象指针
 * @param count [out] 输出范围数量
 * @return     范围数组指针
 */
XCellRange* XConditionalFormatting_ranges(const XConditionalFormatting* self, int* count);

/**
 * @brief      添加单元格到条件格式范围
 * @param self 条件格式对象指针
 * @param cell 单元格引用
 */
void XConditionalFormatting_addCell(XConditionalFormatting* self, const XCellReference* cell);

/**
 * @brief      通过行列号添加单元格到条件格式范围
 * @param self 条件格式对象指针
 * @param row  行号（从1开始）
 * @param col  列号（从1开始）
 */
void XConditionalFormatting_addCellRc(XConditionalFormatting* self, int row, int col);

/**
 * @brief      添加矩形区域到条件格式范围
 * @param self      条件格式对象指针
 * @param firstRow  起始行号（从1开始）
 * @param firstCol  起始列号（从1开始）
 * @param lastRow   结束行号（从1开始）
 * @param lastCol   结束列号（从1开始）
 */
void XConditionalFormatting_addRange(XConditionalFormatting* self, int firstRow, int firstCol, int lastRow, int lastCol);

/**
 * @brief      添加单元格范围对象到条件格式范围
 * @param self 条件格式对象指针
 * @param range 单元格范围对象
 */
void XConditionalFormatting_addRangeEx(XConditionalFormatting* self, const XCellRange* range);

/* ========== 内部接口 ========== */

/**
 * @brief      获取规则数量
 * @param self 条件格式对象指针
 * @return     规则数量
 */
int XConditionalFormatting_rulesCount(const XConditionalFormatting* self);

/**
 * @brief      获取指定索引的规则
 * @param self  条件格式对象指针
 * @param index 规则索引
 * @return     规则指针，无效索引返回 NULL
 */
XConditionalFormatting_Rule* XConditionalFormatting_rule(const XConditionalFormatting* self, int index);

#ifdef __cplusplus
}
#endif
#endif /* XCONDITIONALFORMATTING_H */
