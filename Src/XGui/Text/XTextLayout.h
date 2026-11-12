/**
 * @file       XTextLayout.h
 * @brief      XTextLayout 行级文本布局引擎（对标 Qt 6.8 QTextLayout 的
 *             API/功能/行为三层，有界子集）。
 * @details    QTextLayout 是 QtGui 的单段文本布局引擎（qtextlayout.h）：
 *             给定文本/字体/QTextOption/可用宽，产出行表（每行起止字符
 *             偏移、自然宽、行高、基线），并承载 行↔光标位置 双向映射
 *             （cursorToX/xToCursor）。本类以 XObject 为基类（对标
 *             QObject 继承），一次 ensureLayout() 惰性重建整表，映射
 *             Qt「beginLayout + createLine + setLineWidth 循环」的产物，
 *             并把 C 端消费形态收敛为**行表直读**（对标
 *             QTextLayout::lineCount/lineAt/at()）。
 *
 *             位置坐标系（全类统一）：**文档绝对 UTF-8 字节偏移**
 *             （与 XTextControl.h 头注的平铺行模型同一口径：行内容 +
 *             行间 '\n' 各占 1 字节）；列（col）= 行内字节偏移
 *             [0, 行长]（col==len 即行尾光标位）；坐标（x/y）= 布局
 *             内容坐标像素。与 Qt 的已知差异：
 *             - Qt 位置是 UTF-16 码元下标、本类是 UTF-8 字节偏移
 *               （框架全树口径，见 XTextControl.h @details）；
 *             - '\n' 的落位：Qt 的 QTextLayout 不拆 '\n'（它是
 *               QTextDocument 按 block 边界喂段落的，qtextengine.cpp
 *               itemize 只对 U+2028/U+2029 打 LineOrParagraphSeparator
 *               标记，裸 '\n' 是零宽普通字符）；本类按框架平铺行模型
 *               **把 '\n' 当段落分隔符拆行**（该 '\n' 字节不属于任何
 *               行区间）——即「QTextDocument 的 block 边界进入
 *               QTextLayout」在本引擎内建；
 *             - WordWrap 下超长不可断词**溢出不硬断**（qtextlayout.cpp
 *               :1600-1606 setLineWidth 文档「filled with additional
 *               characters to the next whitespace or end of the text」；
 *               本机 Qt 6.8.0 探针："ABCDEFGHIJKLMNOP"@40px 单行
 *               natW=161.109）。这是 QTextOption::WordWrap 的语义，
 *               与 XTextControl（编辑器腔，长词硬断）**有意不同**；
 *             - WrapAtWordBoundaryOrAnywhere 逐行两段式（qtextlayout.cpp
 *               :2158-2167：行宽溢出即以 WrapAnywhere 重排该行）；
 *             - RTL/bidi 不在本子集（textDirection 仅承载不消费，
 *               恒按 LTR 排；Qt bidiReorder 路径无对应）。
 *
 *             行结果口径（对标 QTextLine）：
 *             - m_width = naturalTextWidth()：**不含软断点处行尾空白
 *               串**（qtextlayout.cpp:2157-2193 trailingSpace 仅在
 *               QTextOption::IncludeTrailingSpaces 时计入）；段落末行
 *               的行尾空白计入（Qt 布局终点 checkFullOtherwiseExtend
 *               提交 :1767-1795）；
 *             - m_height/m_baseline：本轮无内联对象 ⇒ 取字体行高
 *               （ascent+descent，与 XTextControl 行带同式）；
 *               TODO(§B16 后续)：QTextInlineObject 内联图片/对象抬高
 *               行高（qtextlayout.cpp:1942-1965 maxInlineObjectHeight）
 *               待 W3 打印分页接入图片时补；
 *             - m_x = 行起笔 x（对齐偏移已计入，对标 line.x +
 *               QTextEngine::alignLine，qtextengine.cpp:3396-3411）；
 *               m_y = 行顶 y（等高逐行累进）；
 *             - Justify：词距分摊在行内各列 x 上兑现（复用
 *               XTextControl.c xtc_rowJustifyOf 的整像素分摊算法——
 *               那是 Qt QTextEngine::justify（qtextengine.cpp:2432-2619）
 *               的整像素等价式；本类按值重录，不改 XTextControl）。
 *
 *             度量/字形推进：一律走框架 FT 链 XPainter_textWidthRange
 *             （XPainter 内部步进缓存，本类**不自造字形缓存**）；
 *             useDesignMetrics 按 XTextOption 承载与消费——本框架度量
 *             链单源（设计单位线性缩放，XFontFt LIGHT 档水平步进不因
 *             hint 变化），Qt 在 HintLight 下同口径（qfontengine_ft.cpp
 *             :2573-2578 shouldUseDesignMetrics 对 HintLight 恒真；
 *             探针实测 useDesignMetrics 开/关 natW 均 87.203）⇒ 该
 *             旋钮在本引擎为**等值消费**（参与脏标记与选项存取，
 *             不产生度量差）。
 *
 * @note       模块无独立开关宏：随 XGUI_ON 裁剪（同 XTextOption.h）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTEXTLAYOUT_H
#define XTEXTLAYOUT_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "XObject.h"
#include "XFont.h"
#include "XTextOption.h"

/* XString 前置声明（setText 承载；定义见 XContainer/XString）。 */
typedef struct XString XString;

/**
 * @brief      单行布局结果（对标 QTextLine 的只读投影；纯值）。
 * @details    start/end 为文档绝对 UTF-8 字节偏移，end 排他且**不含**
 *             行间 '\n' 字节（与 Qt textLength 含分隔符的差异见头注）；
 *             width 为自然文本宽（不含软断点行尾空白，见头注）；
 *             baseline = 行内基线 y（== ascent）；x/y 为行内容坐标。
 */
typedef struct XTextLayoutLine
{
    int m_start;    /**< 行首文档绝对字节偏移（含）。 */
    int m_end;      /**< 行尾后字节偏移（排他；行间 '\n' 不计入）。 */
    int m_width;    /**< 自然文本宽 px（naturalTextWidth 口径，见头注）。 */
    int m_height;   /**< 行高 px（= ascent + descent；本轮恒字体行高）。 */
    int m_baseline; /**< 行内基线 y px（== m_ascent）。 */
    int m_ascent;   /**< 行 ascent px。 */
    int m_descent;  /**< 行 descent px。 */
    int m_x;        /**< 行起笔 x px（对齐偏移已计入；Justify 行恒 0）。 */
    int m_y;        /**< 行顶 y px（等高逐行 = index × m_height）。 */
    int m_index;    /**< 行下标（0 起）。 */
} XTextLayoutLine;

/** @brief 声明 XTextLayout 类虚函数枚举：继承 XObject（无新增虚槽）。 */
XCLASS_DEFINE_BEGING(XTextLayout)
XCLASS_DEFINE_EXTEND_END(XTextLayout, XObject)

/**
 * @brief      XTextLayout 类结构体（对标 QTextLayout + Private 平铺）。
 * @note       字段增删一律**尾部追加**并同步 init 清零（MSVC /RTC1，
 *             AGENTS.md 结构体初值铁律）；本结构 init 用 XMemset 整清。
 */
typedef struct XTextLayout
{
    XObject m_base;        /**< 继承 XObject（对标 QObject 基类）。 */

    /* ---- 8 字节对齐组：指针 / 值类型大对象 ---- */
    char* m_text;          /**< 布局文本（UTF-8 堆拷贝，拥有）。 */
    XFont m_font;          /**< 度量字体（深拷贝，拥有；对标 QFont）。 */
    XTextOption m_option;  /**< 文本选项（值拷贝；对标 QTextOption）。 */
    XTextLayoutLine* m_lines; /**< 行表数组（拥有；按 start 单调）。 */

    /* ---- 标量组 ---- */
    int m_textLen;         /**< 文本字节长（不含 NUL）。 */
    int m_textCap;         /**< 文本缓冲容量（字节，含 NUL 位）。 */
    int m_lineCount;       /**< 行表条目数。 */
    int m_lineCap;         /**< 行表数组容量。 */
    int m_lineWidth;       /**< 可用布局宽 px（<=0 = 不软换行；对标
                                QTextLine::setLineWidth 的实参）。 */
    int m_ascent;          /**< 字体 ascent px（setFont 时实测刷新）。 */
    int m_descent;         /**< 字体 descent px。 */
    int m_lineHeight;      /**< 字体行高 px（= m_ascent + m_descent）。 */

    /* ---- 标志位域组 ---- */
    uint8_t m_dirty : 1;   /**< 布局脏标记（文本/字体/选项/宽度变更后
                                置位；ensureLayout 惰性重建）。 */
} XTextLayout;

/* ========================================================================== */
/*                       生命周期（对标构造/析构）                             */
/* ========================================================================== */

XVtable* XTextLayout_class_init(void);

/**
 * @brief      初始化布局对象（对标 QTextLayout()；文本空、选项缺省
 *             QTextOption() 默认、宽度 0 = 不软换行、字体为缺省字体）。
 * @param      self 调用方提供的未初始化存储；不可为 NULL。
 */
void XTextLayout_init(XTextLayout* self);

/** @brief 堆上创建（对标 new QTextLayout；调用方 XClassDelete 释放）。 */
#define XTextLayout_create() XTextLayout_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
XTextLayout* XTextLayout_create_ex(XMemoryType memory);

/* ========================================================================== */
/*                        输入面（对标 setText/setFont/...）                   */
/* ========================================================================== */

/**
 * @brief      设置文本（对标 setText(const QString&)；深拷贝，失效布局）。
 * @param      self 目标对象；NULL 无操作。
 * @param      text 文本（内码缓冲；NULL 视为空文本——XString 是内码
 *                  缓冲，本函数经 XString_toUtf8 取 UTF-8）。
 */
void XTextLayout_setText(XTextLayout* self, const XString* text);

/**
 * @brief      设置文本（对标 setText；UTF-8 + 字节长重载）。
 * @param      self 目标对象；NULL 无操作。
 * @param      utf8 UTF-8 文本（借用；NULL 视为空文本）。
 * @param      len 字节长；<0 按 strlen 取。
 */
void XTextLayout_setText_utf8(XTextLayout* self, const char* utf8, int len);

/** @brief 读取布局文本（借用 UTF-8 视图；NULL 对象返回 NULL）。 */
const char* XTextLayout_text_const(const XTextLayout* self);

/** @brief 读取文本字节长（不含 NUL；NULL 对象返回 0）。 */
int XTextLayout_textLength(const XTextLayout* self);

/**
 * @brief      设置度量字体（对标 QFont 承载；深拷贝，失效布局）。
 * @param      font NULL 时回落缺省字体（同 XTextControl_setFont 口径）。
 */
void XTextLayout_setFont(XTextLayout* self, const XFont* font);

/** @brief 读取度量字体（深拷贝到 *out；用完 XClassDeinit 释放）。 */
void XTextLayout_font(const XTextLayout* self, XFont* out);

/**
 * @brief      设置文本选项（对标 setTextOption；值拷贝，失效布局）。
 * @details    选项全量消费：wrapMode 四档断行、tabStopDistance/制表位
 *             表（Qt calculateTabWidth 口径）、alignment（行起笔偏移 +
 *             Justify 词距分摊）、flags 的 IncludeTrailingSpaces
 *             （行尾空白计宽）、useDesignMetrics（等值消费，见头注）。
 */
void XTextLayout_setTextOption(XTextLayout* self, const XTextOption* option);

/** @brief 读取文本选项（值拷贝到 *out；NULL 对象按缺省选项拷出）。 */
void XTextLayout_textOption(const XTextLayout* self, XTextOption* out);

/**
 * @brief      设置可用布局宽（对标 QTextLine::setLineWidth 的实参；
 *             <=0 = 不软换行（每段落单行，长行溢出），失效布局）。
 */
void XTextLayout_setLineWidth(XTextLayout* self, int width);

/** @brief 读取可用布局宽（<=0 = 未启用软换行）。 */
int XTextLayout_lineWidth(const XTextLayout* self);

/* ========================================================================== */
/*                 行级结果（查询前惰性 ensureLayout）                         */
/* ========================================================================== */

/**
 * @brief      确保行表有效（脏标记惰性重建；查询族内部自动调用）。
 * @return     重建成功返回 true；内存失败保持脏并返回 false（行表可能
 *             为空，查询按 0 行/未命中口径返回）。
 */
bool XTextLayout_ensureLayout(XTextLayout* self);

/** @brief 行数（空文本 = 1 空行，对标 Qt 探针 lineCount=1）。 */
int XTextLayout_lineCount(XTextLayout* self);

/**
 * @brief      取单行结果拷贝（对标 QTextLayout::lineAt）。
 * @param      self 目标对象；NULL 返回 false。
 * @param      index 行下标（0 起）；越界返回 false。
 * @param      out 输出拷贝；可为 NULL（只做越界判定）。
 * @return     命中返回 true 并写 out（非 NULL）。
 */
bool XTextLayout_lineAt(XTextLayout* self, int index, XTextLayoutLine* out);

/**
 * @brief      文档位置 → 所在行（对标 lineForTextPosition 的行表版）。
 * @details    pos 钳位 [0, textLen]；落在行间 '\n' 字节上归属**前一
 *             行**（行尾光标位，与光标语义一致）。
 * @param      out 行结果拷贝；可为 NULL（只要行号）。
 * @return     行下标；self NULL/无行表返回 -1。
 */
int XTextLayout_lineForTextPosition(XTextLayout* self, int pos,
                                    XTextLayoutLine* out);

/**
 * @brief      文档位置 → (行下标, 行内字节列)（对标
 *             QTextCursor 双向映射的布局侧）。
 * @details    pos 钳位与 '\n' 归属同 lineForTextPosition；col 钳位
 *             [0, 行长]。line/col 均可 NULL。
 */
void XTextLayout_positionToLineCol(XTextLayout* self, int pos, int* line,
                                   int* col);

/**
 * @brief      (行下标, 行内字节列) → 文档位置（钳位逆映射）。
 * @details    col == 行长 合法（行尾光标位：落在行间 '\n' 字节或文末）。
 * @return     文档绝对字节位置；行越界按钳位处理；self NULL 返回 0。
 */
int XTextLayout_lineColToPosition(XTextLayout* self, int line, int col);

/**
 * @brief      行内列 → 内容 x（对标 QTextLine::cursorToX 的 LTR 子集）。
 * @details    x = 行对齐偏移 + 自然宽累进（'\t' 按 calculateTabWidth
 *             口径推进）+ Justify 词距分摊附加；col 向后吸附到码点
 *             边界（对标 graphemeBoundary 吸附，qtextlayout.cpp:2937）；
 *             软换行启用时结果钳到可用宽内（:3060-3063）。
 * @return     内容坐标 x px；行越界返回 0。
 */
int XTextLayout_colToX(XTextLayout* self, int line, int col);

/**
 * @brief      行内 x → 列（对标 QTextLine::xToCursor 的 LTR 最近边界
 *             口径，qtextlayout.cpp:3078-3268）。
 * @details    x 先扣行对齐偏移；行外钳位（左=0、右=行长）；行内取
 *             码点边界最近值（制表胞按半宽二分取前后沿，对标
 *             TabOrObject 分支 :3157-3165）。
 * @return     行内字节列；行越界返回 0。
 */
int XTextLayout_xToCol(XTextLayout* self, int line, int x);

/**
 * @brief      内容坐标命中测试（对标 QWidgetTextControl::hitTest 的
 *             布局侧承载；FuzzyHit 语义恒钳位命中）。
 * @param      self 目标对象；NULL 或 outPos 为 NULL 返回 false。
 * @param      x、y 内容坐标（y<0 钳首行、y>=总高钳末行）。
 * @param      outPos 输出文档绝对字节位置（= 行首 + 行内列）。
 * @return     命中返回 true（钳位语义下恒可命中）。
 */
bool XTextLayout_hitTest(XTextLayout* self, int x, int y, int* outPos);

/** @brief 总布局高度 px（行数 × 行高；self NULL 返回 0）。 */
int XTextLayout_totalHeight(XTextLayout* self);

#ifdef __cplusplus
}
#endif
#endif /* XGUI_ON */

#endif /* XTEXTLAYOUT_H */
