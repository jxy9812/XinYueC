#ifndef XSTYLESHEETSTYLE_H
#define XSTYLESHEETSTYLE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWindowsStyle.h"
#include "XCssStyleSheet.h"

#if XSTYLE_ON

typedef struct XWidget XWidget; /**< 前置声明（接口签名用，同 XStyle.h 口径）。 */

XCLASS_DEFINE_BEGING(XStyleSheetStyle)
XCLASS_DEFINE_EXTEND_END(XStyleSheetStyle, XWindowsStyle)

/**
 * @brief 控件级样式表源的解析缓存条目（级联多源：沿 XObject_parent 链的
 *        控件自身与祖先 styleSheet 文本各占一条）。
 */
typedef struct XStyleSheetStyleSource
{
    XString* m_text;        /**< 源文本深拷贝（对象拥有；缓存键——以内容
                                 等值判定，杜绝原串释放后地址复用导致的
                                 误命中）。 */
    XCssStyleSheet m_sheet; /**< 解析后的规则集（对象拥有）。 */
    bool m_inUse;           /**< 条目是否有效。 */
} XStyleSheetStyleSource;

/**
 * @brief 样式表风格（对标 Qt 6.8 QStyleSheetStyle : QWindowsStyle）。
 *
 *        持有解析后的 XCssStyleSheet 规则表；绘制时按控件类名
 *        （对标 elementName）、objectName（对标 #id）与伪类状态
 *        （全映射见 xsss_statePseudos）匹配规则，命中声明覆盖颜色/
 *        边框并回落底层 XWindowsStyle/XFusionStyle。未命中或未设置
 *        样式表时行为与底层样式完全一致。
 */
typedef struct XStyleSheetStyle
{
    XWindowsStyle m_base;      /**< 基类成员；必须是第一个。 */
    XCssStyleSheet m_sheet;    /**< 解析后的规则表（对象拥有；级联中的
                                    应用级源——经 XStyle_installStyleSheet
                                    装在全局默认样式对象里时即全局表）。 */
    XStyle* m_source;          /**< 底层样式（NULL=用全局默认）。 */
    bool m_sourceOwned;        /**< m_source 是否由本对象拥有（析构时释放）。 */
    /* ---- 级联多源解析缓存（控件自身 styleSheet + 祖先 sheets 逐串
     *      解析为规则集的缓存；键=源文本内容等值（深拷贝比对），淘汰=
     *      环形替换且替换时失效渲染规则缓存（m_cacheRule 指进条目规则
     *      数组）。失效时机：应用级 setStyleSheet 与控件级
     *      XWidget_setStyleSheet 均失效渲染缓存（后者经
     *      XStyle_invalidateStyleSheetRenderCache，deferred#4）；
     *      源条目自身随文本内容变化自然失配、随环形替换淘汰，随本对象
     *      析构释放 ---- */
    XStyleSheetStyleSource* m_sources; /**< 源缓存条目数组（对象拥有）。 */
    int m_sourceCount;         /**< 已占用条目数。 */
    int m_sourceCapacity;      /**< 条目容量。 */
    int m_sourceCursor;        /**< 环形替换游标。 */
    /* ---- 渲染规则缓存（对标 QStyleSheetStylePrivate::renderRules 的
     *      单槽近似：按 (对象指针,状态) 缓存级联裁决的命中规则。
     *      !important 语义对齐 Qt 6.8.3：只解析标志不参与裁决，权重 =
     *      rule.order + specificity*0x100 + (origin+depth)*0x100000
     *      （matchRule，qcssparser.cpp；无 importance 轨道），故「缓存
     *      规则=最高权重且同权取后」与完整级联在任一属性上的裁决等价，
     *      命中直返规则内声明即可 ---- */
    const XObject* m_cacheObj;      /**< 缓存对象指针。 */
    uint32_t m_cacheState;          /**< 缓存状态位。 */
    const XCssStyleRule* m_cacheRule; /**< 缓存命中规则。 */
    const XCssSelector* m_cacheSel;  /**< 缓存命中选择器。 */
    uint32_t m_cacheWeight;         /**< 缓存命中权重（order+spec*0x100+
                                         (origin+depth)*0x100000）。 */
    bool m_cacheValid;              /**< 缓存是否有效。 */
} XStyleSheetStyle;

XVtable* XStyleSheetStyle_class_init(void);

/**
 * @brief 初始化嵌入式样式表风格。
 *
 * @param self 目标样式指针，不能为空。
 * @return 无返回值。
 */
void XStyleSheetStyle_init(XStyleSheetStyle* self);

/**
 * @brief 堆上创建样式表风格。
 *
 * @param memory 内存类型。
 * @return 样式指针；分配失败返回 NULL。
 */
XStyleSheetStyle* XStyleSheetStyle_create_ex(XMemoryType memory);
#define XStyleSheetStyle_create() \
    XStyleSheetStyle_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/** @brief 析构入口（查表分派父类析构）。 */

/** @brief 删除堆上样式（查表分派析构并释放内存）。 */

/**
 * @brief 设置样式表文本（对标 QStyleSheetStyle::setStyleSheet）。
 *
 * @param self 目标样式指针。
 * @param css UTF-8 CSS 文本（NULL/空清空规则）。
 * @return 解析成功返回 true。
 */
bool XStyleSheetStyle_setStyleSheet(XStyleSheetStyle* self, const char* css);

/**
 * @brief 失效渲染规则缓存（不触动规则表与控件源缓存）。
 *
 *        单槽缓存按 (对象,状态) 复用且 m_cacheRule 可指进控件源缓存
 *        条目内规则；控件级 setStyleSheet 改变级联输入后必须废弃，
 *        否则同对象同状态的连续第二次绘制按旧规则取色（deferred#4）。
 *
 * @param self 目标样式指针。
 * @return 无返回值。
 */
void XStyleSheetStyle_invalidateRenderCache(XStyleSheetStyle* self);

/**
 * @brief 设置底层样式（借用；NULL=回落全局默认样式）。
 *
 * @param self 目标样式指针。
 * @param source 底层样式指针（借用；本对象不取得所有权）。
 * @return 无返回值。
 */
void XStyleSheetStyle_setSourceStyle(XStyleSheetStyle* self, XStyle* source);
/** @brief 设置底层样式并转移所有权（析构时释放 source；用于 installStyleSheet 接管默认样式）。
 * @param self 目标样式指针。
 * @param source 底层样式指针（所有权转移给 self；可为 NULL）。
 * @return 无返回值。
 */
void XStyleSheetStyle_setSourceStyle_move(XStyleSheetStyle* self, XStyle* source);

/**
 * @brief 查询当前规则数（测试/诊断）。
 *
 * @param self 目标样式指针。
 * @return 规则数。
 */
int XStyleSheetStyle_ruleCount(const XStyleSheetStyle* self);

/**
 * @brief 按伪元素/子控件名查询级联命中规则（对标 Qt 6.8.3
 *        QStyleSheetStyle::styleRule(w, pseudoElement)）。
 *
 *        级联口径与属性查询一致（应用级 + 控件链多源、matchRule 权重
 *        取最高、同权取后），仅要求命中选择器末段的伪元素名与入参等值
 *        （大小写不敏感；Qt 的 QSS 子控件名约定全小写，对标
 *        Selector::pseudoElement() 读末段）。查询走即时全源扫描，不经
 *        渲染规则缓存。
 *
 *        注：绘制端按子控件分派（对标 renderRule(w, opt, pseudoElement)
 *        把伪元素规则画到子控件上）不在本批，deferred——本接口先供
 *        查询/诊断与后续分派接入使用。
 *
 * @param self 目标样式指针（源解析缓存按需填充，同查询路径的非 const 约定）。
 * @param widget 目标控件（级联源收集沿其 parent 链）。
 * @param state 控件状态位（伪类派生输入）。
 * @param pseudoElement 伪元素/子控件名（UTF-8；NULL/空等价于属性查询
 *        的普通路径——要求末段无伪元素）。
 * @return 命中的最优先规则（借用指针，指向源缓存或应用级表，样式对象
 *         存活期内有效；样式表/源缓存变更后失效）；无命中返回 NULL。
 */
const XCssStyleRule* XStyleSheetStyle_styleRuleForPseudoElement(
    XStyleSheetStyle* self, const XWidget* widget, uint32_t state,
    const char* pseudoElement);

/**
 * @brief 是否存在命中指定伪元素/子控件的规则（对标
 *        QStyleSheetStyle::hasStyleRule；即
 *        XStyleSheetStyle_styleRuleForPseudoElement 的布尔版）。
 *
 * @param self 目标样式指针。
 * @param widget 目标控件。
 * @param state 控件状态位。
 * @param pseudoElement 伪元素/子控件名（UTF-8）。
 * @return 存在命中返回 true。
 */
bool XStyleSheetStyle_hasStyleRuleForPseudoElement(
    XStyleSheetStyle* self, const XWidget* widget, uint32_t state,
    const char* pseudoElement);

/**
 * @brief 解析颜色值（输出统一 ARGB：0xAARRGGBB）。
 *
 *        对齐 Qt 6.8.3 QColor::setNamedColor / qcssparser
 *        parseColorValue 口径：
 *        - 具名色：SVG 1.0 全表 + transparent（对标 qcolor.cpp
 *          rgbTbl 148 项），大小写不敏感且忽略名字内部空白；
 *        - 十六进制：#RGB/#RRGGBB/#AARRGGBB（Qt get_hex_rgb len==8
 *          为 alpha 在前口径，与既有断言一致）+ #RGBA（CSS Colors 4
 *          新增，alpha 收尾；Qt 无 4 位分支）+ #RRRGGGBBB/#RRRRGGGGBBBB
 *          （Qt len==9/12 高位通道扩展）；#RRGGBBAA 与 #AARRGGBB 同为
 *          8 位、按 Qt 口径保留 alpha 在前；
 *        - 函数式：rgb()/rgba() 支持百分比与浮点分量；hsl()/hsla()/
 *          hsv()/hsva() 自实现换算（对标 qcolor.cpp 的 HSL/HSV→RGB
 *          算法与 parseColorValue 的量程：色相百分比 ×359/100）。
 *
 * @param value CSS 颜色值文本。
 * @param out 输出 ARGB。
 * @return 解析成功返回 true。
 */
bool XCssParseColor(const char* value, uint32_t* out);

/**
 * @brief 限长颜色 token 解析（供 border 简写拆分）。
 *
 * @param value 颜色 token 起始指针。
 * @param len token 长度。
 * @param out 输出 ARGB。
 * @return 解析成功返回 true。
 */
bool XCssParseColorToken(const char* value, size_t len, uint32_t* out);

/**
 * @brief      CSS 长度单位（对标 qcssparser_p.h LengthData 匿名 Unit
 *             枚举 { None, Px, Ex, Em, Percent }，扩 Pt：Qt 的 LengthData
 *             无 Pt——font-size 的 "pt" 走 setPointSizeF 点值路径、盒
 *             路径 "pt" 不被 lengthValue 识别；此处按 CSS 口径统一入
 *             枚举，换算交由消费端）。
 */
typedef enum XCssLengthUnit
{
    XCssLengthUnit_None = 0,   /**< 无效/未解析（解析失败的输出态）。 */
    XCssLengthUnit_Px,         /**< 像素（"12px" 或裸数字 "12"）。 */
    XCssLengthUnit_Pt,         /**< 点（"12pt"；CSS：px = pt × 96/72）。 */
    XCssLengthUnit_Em,         /**< em（相对参考字体高，对标 LengthData::Em）。 */
    XCssLengthUnit_Ex,         /**< ex（相对 x 高，对标 LengthData::Ex）。 */
    XCssLengthUnit_Percent     /**< 百分比（"50%"，m_value 不含 % 号）。 */
} XCssLengthUnit;

/**
 * @brief 带单位长度（对标 qcssparser_p.h LengthData { qreal number;
 *        unit }）。
 */
typedef struct XCssLength
{
    double m_value;        /**< 数值（"1.5em" → 1.5；百分比不含 % 号）。 */
    XCssLengthUnit m_unit; /**< 单位；None 表示不是有效长度。 */
} XCssLength;

/**
 * @brief 解析带单位长度（"12px"/"1.5em"/"12pt"/"50%"/裸数字 "12"）。
 *
 *        对标 Qt 6.8.3 qcssparser.cpp ValueExtractor::lengthValue：
 *        px/pt/em/ex 后缀截去后取 double，百分比剥 %，裸数字按 Px
 *        （Qt 落 LengthData::None，经 lengthValueFromData scale=1 等
 *        效 px）；大小写不敏感；数字与单位间空白容忍；尾部垃圾
 *        （"12abc"）整串拒绝——不再沿用旧前缀解析的静默截断。
 *
 * @param value CSS 长度文本。
 * @param out 输出（解析失败时 m_unit=None）。
 * @return 解析成功返回 true。
 */
bool XCssParseLengthEx(const char* value, XCssLength* out);

/**
 * @brief 解析长度值为像素（兼容签名；"12px"/"12" → 12）。
 *
 *        兼容换算口径：Px 舍入；Pt × 96/72（CSS pt→px）；Em/Ex 无
 *        字体上下文，按 16px 基准（ex 取其半——与带上下文消费端的
 *        x 高口径一致；Qt LengthData 换算需 QFontMetrics，此签名不
 *        可得）；Percent 无矩形上下文按 0（旧实现 "50%"→50px 属静
 *        默错值，此签名宁可 0 也不给错值，带上下文消费端走
 *        xsss_lengthPx 内部换算）；解析失败返回 false。
 *
 * @param value CSS 长度文本。
 * @param out 输出像素值。
 * @return 解析成功返回 true。
 */
bool XCssParseLength(const char* value, int* out);

#endif /* XSTYLE_ON */
#ifdef __cplusplus
}
#endif
#endif /* XSTYLESHEETSTYLE_H */
