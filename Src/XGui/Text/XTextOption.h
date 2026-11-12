/**
 * @file       XTextOption.h
 * @brief      XTextOption 文本选项值类型（对标 Qt 6.8 QTextOption）。
 * @details    Qt 的 QTextOption 是 QtGui 的纯值类型（qtextoption.h）：承载
 *             对齐、文本方向、断行规则、绘制标志、制表位距离与制表位数组，
 *             并作为 QTextDocument::defaultTextOption 的载荷。本类把 Qt 的
 *             位域收成平铺字段（align:9/wrap:4/design:1/direction:2/f/tab ⇒
 *             int/int/int/uint32/int/double），数值与 Qt 逐一对应：
 *             - alignment 用 int 承载 Qt::Alignment 位组合（XTDAlignment
 *               同值空间，见 Widget/XTextDocument.h）；
 *             - textDirection 用 int 承载 Qt::LayoutDirection（0=LTR、
 *               1=RTL、2=Auto；XGuiLayoutDirection 同值空间）；
 *             - 制表位列表以**定容数组**承载（XTEXTOPTION_MAX_TABS，超出
 *               截断）——有界子集登记：Qt 为 QList&lt;Tab&gt; 无上限；
 *             - 本类是纯值：无堆指针，可整体赋值/按值返回，无析构面。
 * @note       无排版引擎消费端：XTextDocument 把本类作为 defaultTextOption
 *             载荷（对标 qtextdocument.cpp 的 d->defaultTextOption），布局
 *             引擎属 §B16 长期项（台账），属性本身只承载不消费。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTEXTOPTION_H
#define XTEXTOPTION_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/** @brief 断行规则（对标 QTextOption::WrapMode，数值一致；与
 *         XTextControlWrapMode 同值空间——两者都是 QTextOption::WrapMode
 *         的镜像，见 Text/XTextControl.h）。 */
typedef enum XTextOptionWrapMode
{
    XTextOptionWrap_NoWrap = 0,                  /**< 不换行。 */
    XTextOptionWrap_WordWrap = 1,                /**< 词界断行（QTextOption 默认）。 */
    XTextOptionWrap_ManualWrap = 2,              /**< 仅手动换行。 */
    XTextOptionWrap_Anywhere = 3,                /**< 任意点断行。 */
    XTextOptionWrap_WordBoundaryOrAnywhere = 4   /**< 词界优先、超宽词内任意断
                                                      （QTextDocument 默认）。 */
} XTextOptionWrapMode;

/** @brief 制表位类型（对标 QTextOption::TabType，数值一致）。 */
typedef enum XTextOptionTabType
{
    XTextOptionTab_Left = 0,      /**< 左对齐制表位（Tab 默认）。 */
    XTextOptionTab_Right = 1,     /**< 右对齐制表位。 */
    XTextOptionTab_Center = 2,    /**< 居中制表位。 */
    XTextOptionTab_Delimiter = 3  /**< 分隔符制表位（delimiter 生效）。 */
} XTextOptionTabType;

/** @brief 绘制标志位（对标 QTextOption::Flag，数值一致；IncludeTrailingSpaces
 *         占符号位，故用 uint32 位组合承载）。 */
typedef enum XTextOptionFlag
{
    XTextOptionFlag_ShowTabsAndSpaces = 0x1u,                   /**< 显示空白。 */
    XTextOptionFlag_ShowLineAndParagraphSeparators = 0x2u,      /**< 显示分隔符。 */
    XTextOptionFlag_AddSpaceForLineAndParagraphSeparators = 0x4u, /**< 分隔符计空格。 */
    XTextOptionFlag_SuppressColors = 0x8u,                      /**< 抑制颜色。 */
    XTextOptionFlag_ShowDocumentTerminator = 0x10u,             /**< 显示文档终止符。 */
    XTextOptionFlag_IncludeTrailingSpaces = 0x80000000u         /**< 计入行尾空白。 */
} XTextOptionFlag;

/** @brief 制表位容量上限（有界子集；Qt 为 QList 无上限，超出 setTabs/
 *         setTabArray 按**截断**处理并在返回值体现）。 */
#define XTEXTOPTION_MAX_TABS 16

/** @brief 单个制表位（对标 QTextOption::Tab；缺省 position=80、type=Left、
 *         delimiter=0，对标 Tab 构造 `Tab() : position(80), type(LeftTab)`）。 */
typedef struct XTextOptionTab
{
    double position;    /**< 制表位位置（绘制单位）。 */
    int type;           /**< XTextOptionTabType。 */
    uint32_t delimiter; /**< DelimiterTab 的分隔符（Unicode 码点；QChar 承载）。 */
} XTextOptionTab;

/**
 * @brief      文本选项值类型（对标 QTextOption）。
 * @details    纯值语义：无堆资源，可整体赋值/按值返回；字段均为平铺标量。
 *             缺省值 = QTextOption() 默认构造（qtextoption.cpp：委托
 *             QTextOption(Qt::AlignLeft) 后把 direction 覆写为 Auto）：
 *             alignment=AlignLeft(0x1)、wrapMode=WordWrap(1)、
 *             textDirection=Auto(2)、flags=0、tabStopDistance=-1.0、
 *             useDesignMetrics=false、tabCount=0。
 */
typedef struct XTextOption
{
    int m_alignment;          /**< Qt::Alignment 位组合（XTDAlignment 同值空间）。 */
    int m_wrapMode;           /**< XTextOptionWrapMode。 */
    int m_textDirection;      /**< Qt::LayoutDirection：0=LTR、1=RTL、2=Auto。 */
    uint32_t m_flags;         /**< XTextOptionFlag 位组合。 */
    int m_useDesignMetrics;   /**< bool：是否用字体设计度量（缺省 false）。 */
    double m_tabStopDistance; /**< 缺省制表距离；QTextOption() 缺省 -1.0
                                   （QTextDocument 会改设 80，见该类注释）。 */
    int m_tabCount;           /**< 实际制表位数（≤ XTEXTOPTION_MAX_TABS）。 */
    XTextOptionTab m_tabs[XTEXTOPTION_MAX_TABS]; /**< 制表位（定容，超出截断）。 */
} XTextOption;

/**
 * @brief      按 QTextOption() 默认构造初始化（对标默认构造）。
 * @param      self 目标选项；NULL 忽略。
 * @return     无返回值。
 */
void XTextOption_init(XTextOption* self);

/**
 * @brief      按 QTextOption(Qt::Alignment) 构造初始化（对标单参构造）。
 * @details    与默认构造的差异仅在 direction：单参构造取 QGuiApplication::
 *             layoutDirection()（XGUIAPPLICATION_ON 关闭或无应用时按 LTR=0，
 *             与 Qt「无应用回落 LTR」同口径）；默认构造在委托后覆写为 Auto。
 * @param      self 目标选项；NULL 忽略。
 * @param      alignment Qt::Alignment 位组合（XTDAlignment 同值空间）。
 * @return     无返回值。
 */
void XTextOption_init_alignment(XTextOption* self, int alignment);

/** @brief 值构造（等价 init）；返回按 QTextOption() 默认构造的选项值。 */
XTextOption XTextOption_create(void);

/** @brief 值构造（等价 init_alignment）；direction 取应用布局方向。 */
XTextOption XTextOption_create_alignment(int alignment);

/**
 * @brief      设置对齐（对标 QTextOption::setAlignment；存 Qt::Alignment
 *             位组合，Qt 以 9 位承载、本类存 int 全宽——往返无损）。
 * @param      self 目标选项；NULL 忽略。
 * @param      alignment 对齐位组合（XTDAlignment 同值空间）。
 * @return     无返回值。
 */
void XTextOption_setAlignment(XTextOption* self, int alignment);

/** @brief 读取对齐位组合；self 为 NULL 返回 AlignLeft(0x1)（缺省值）。 */
int XTextOption_alignment(const XTextOption* self);

/** @brief 设置文本方向（对标 setTextDirection；0=LTR、1=RTL、2=Auto）。 */
void XTextOption_setTextDirection(XTextOption* self, int direction);

/** @brief 读取文本方向；self 为 NULL 返回 Auto(2)（缺省值）。 */
int XTextOption_textDirection(const XTextOption* self);

/** @brief 设置断行规则（对标 setWrapMode；XTextOptionWrapMode）。 */
void XTextOption_setWrapMode(XTextOption* self, int wrapMode);

/** @brief 读取断行规则；self 为 NULL 返回 WordWrap(1)（缺省值）。 */
int XTextOption_wrapMode(const XTextOption* self);

/** @brief 设置绘制标志（对标 setFlags；XTextOptionFlag 位组合）。 */
void XTextOption_setFlags(XTextOption* self, uint32_t flags);

/** @brief 读取绘制标志；self 为 NULL 返回 0（缺省值）。 */
uint32_t XTextOption_flags(const XTextOption* self);

/** @brief 设置缺省制表距离（对标 setTabStopDistance；原样保存，Qt 亦不钳位）。 */
void XTextOption_setTabStopDistance(XTextOption* self, double distance);

/** @brief 读取缺省制表距离；self 为 NULL 返回 -1.0（QTextOption 缺省）。 */
double XTextOption_tabStopDistance(const XTextOption* self);

/** @brief 设置设计度量开关（对标 setUseDesignMetrics）。 */
void XTextOption_setUseDesignMetrics(XTextOption* self, bool enable);

/** @brief 读取设计度量开关；self 为 NULL 返回 false（缺省值）。 */
bool XTextOption_useDesignMetrics(const XTextOption* self);

/**
 * @brief      以位置数组设置制表位（对标 setTabArray(QList&lt;qreal&gt;)）。
 * @details    Qt 语义：把列表转成制表位（type=LeftTab、delimiter=0，其余
 *             字段取 Tab 缺省），**整体替换**现有制表位。超出
 *             XTEXTOPTION_MAX_TABS 截断（有界子集登记）。
 * @param      self 目标选项；NULL 忽略。
 * @param      positions 制表位位置数组（借用；NULL 按清空处理）。
 * @param      count 位置数；&lt;0 按 0。
 * @return     实际写入数（min(count, XTEXTOPTION_MAX_TABS)）。
 */
int XTextOption_setTabArray(XTextOption* self, const double* positions,
                            int count);

/**
 * @brief      导出制表位位置数组（对标 tabArray()）。
 * @param      self 目标选项；NULL 视为无制表位。
 * @param      out 输出缓冲（借用；可为 NULL，此时只查询不写入）。
 * @param      capacity 缓冲容量（按元素计）。
 * @return     实际写入数；out 为 NULL 或 capacity&lt;=0 返回 0。
 */
int XTextOption_tabArray(const XTextOption* self, double* out, int capacity);

/**
 * @brief      设置制表位列表（对标 setTabs(QList&lt;Tab&gt;)；整体替换，
 *             超出容量截断）。
 * @param      self 目标选项；NULL 忽略。
 * @param      tabs 制表位数组（借用；NULL 按清空处理）。
 * @param      count 数量；&lt;0 按 0。
 * @return     实际写入数（min(count, XTEXTOPTION_MAX_TABS)）。
 */
int XTextOption_setTabs(XTextOption* self, const XTextOptionTab* tabs,
                        int count);

/** @brief 读取制表位数；self 为 NULL 返回 0。 */
int XTextOption_tabCount(const XTextOption* self);

/**
 * @brief      按下标取制表位拷贝。
 * @param      self 目标选项；NULL 视为越界。
 * @param      index 下标（0 起）；越界返回 false。
 * @param      out 输出拷贝；可为 NULL（只做越界判定）。
 * @return     命中返回 true 并写 out（非 NULL）。
 */
bool XTextOption_tab(const XTextOption* self, int index, XTextOptionTab* out);

/**
 * @brief      比较两个制表位（对标 QTextOption::Tab::operator==）。
 * @details    Qt 口径：type 相等 && delimiter 相等 && position 按
 *             qFuzzyCompare 模糊相等（|a-b|*1e12 ≤ min(|a|,|b|)）。
 * @param      a、b 待比较制表位；NULL 视为不相等（同为 NULL 视为相等）。
 * @return     按 Qt 口径相等返回 true。
 */
bool XTextOption_tab_equals(const XTextOptionTab* a, const XTextOptionTab* b);

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XTEXTOPTION_H */
