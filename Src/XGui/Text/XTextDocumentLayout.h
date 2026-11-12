/**
 * @file       XTextDocumentLayout.h
 * @brief      XTextDocumentLayout 文档打印分页布局器（对标 Qt 6.8
 *             QAbstractTextDocumentLayout + QTextDocument::print 的分页
 *             子集；§B16/W3-b 打印域）。
 * @details    Qt 语义取证（Qt 6.8.3 原文 + 本机 Qt 6.8.0 offscreen 探针，
 *             探针 /tmp/w3b_probe/probe2.cpp + report2.txt，关键数值已
 *             抄入 XTextPrintAlignTest.c 断言注释）：
 *             - **分页 = 连续布局 + 按页高竖切**：文档整体排成一列，
 *               页数 = ceil(documentSize().height() / pageSize().height())
 *               （qtextdocumentlayout.cpp:3963-3969 dynamicPageCount）；
 *               页高 < 0 ⇒ 1 页（:3966-3967）。探针 para40@300x160 边距
 *               10：docH=300.000 ⇒ ceil(1.875)=2 页；tiny@100x12：docH=
 *               28.000 ⇒ ceil(2.33)=3 页（行留在页 0 不换页，见下）。
 *             - **行级断页**（qtextdocumentlayout.cpp:3604-3605，非懒
 *               布局路径）：`absoluteY + lineBreakHeight > pageBottom`
 *               ⇒ newPage()——该行整体挪到新页顶，**行永不劈开**（块
 *               内行序列中间可以断页；「块不可跨页整块下移」不是 Qt
 *               行为）。newPage 口径（:121-123）：页游标 += pageHeight，
 *               y = qMax(y, 新页顶 + topMargin)——**y 只进不退**（守卫
 *               挡下后 y 可能已深入后页，后续短行换页不得回退；「行放
 *               不下一页高」的文档里被挡行之后短行的落点与 Qt 同式）。
 *               探针 longwords8@200x90 边距 6：行 3 挪 y=96（页 1 顶
 *               90+6）、行 6 挪 y=186，逐行吻合。
 *             - **页放不下一行**（:3557-3558 contentHeight >=
 *               lineBreakHeight 守卫）：行高超过页内容高（页高-上下
 *               边距）时**不换页**，行留在原处（探针 tiny：行 0 绝对
 *               y=2 留页 0，总高 28，页数仍按除法 ceil(28/12)=3——
 *               布局换页与页数除法解耦，照抄）。
 *             - **pageBottom**（:3023）= (currentPage+1)*pageHeight -
 *               bottomMargin；首行顶 = topMargin（nomargin 探针首行
 *               y=0、para40 首行 y=10）。块间距 0（layoutBlock 的
 *               topMargin/bottomMargin 合并式 :2905-2912 在本子集块
 *               格式无 margins 字段 ⇒ 恒 0；探针 multi 四块紧接）。
 *             - **documentSize**（:3990-3996）= rootFrame 总尺寸：
 *               高 = 末行底 + bottomMargin（探针 empty=44=10+24+10、
 *               para40=300=290+10 逐值吻合）；宽 = max(可用宽, 最宽行
 *               右缘) + 左右边距（Qt fd->size.width =
 *               qMax(newContentsWidth, contentsWidth) + marginWidth，
 *               :3044/:3057——**真 max**：正常断行下行不越可用宽 ⇒
 *               探针恒页宽；病态组合「页宽>0 且左右边距之和 >= 页宽」
 *               （可用宽<=0）下宽度随内容走——本类同口径。子集边界：
 *               可用宽<=0 时本类**不软换行**（W1 引擎 setLineWidth<=0
 *               单行溢出），Qt 的负宽布局另有钳制路径仍逐字软换行
 *               （本机探针 /tmp/w3b_probe/probe3 实测：页宽 100 边距
 *               60/60 ⇒ Qt 断 2 行 docW=273，本类单行 docW=行宽+120）
 *               ——病态换行行为不复刻，仅宽度公式对齐；页宽<=0 时取
 *               内容宽 + 右边距）。
 *             - **逐页绘制**（qtextdocument.cpp:1923-1950 printPage）：
 *               第 k 页 = 连续文档空间 [k*pageH, (k+1)*pageH) 的竖切，
 *               drawPage 以**页面坐标**（原点=页左上）绘制该页内容
 *               （Qt print 的 translate(body.left, body.top-(k-1)*H)
 *               + clip 同构；本类手动偏移，不依赖 painter 状态机）。
 *               draw() 为整文档连续绘制（QAbstractTextDocumentLayout::
 *               draw 语义最小版，clipRect 只做行带过滤）。
 *             - **页码/页眉页脚**：Qt qtbase 无 %p/%P 占位语法（全树
 *               grep 仅 QProgressBar 的 "%p" 进度语义）；QTextDocument::
 *               print 的页码是 QString::number(index) **纯数字**画在
 *               底部边距区右侧（qtextdocument.cpp:1935-1944）。本类
 *               提供页眉/页脚单行文本（setHeaderText/setFooterText），
 *               文本内支持占位 **%p=当前页号（1 起）、%P=总页数**——
 *               这是本类对页眉页脚文本的扩展约定（打印页脚惯例），
 *               setFooterText("%p") 即等价 Qt print 的页码行为（纯
 *               当前页号画在底边距区）。页眉画在上边距区、页脚画在
 *               下边距区（行高超过边距区时按边距区顶部/底部落笔，
 *               可侵入正文区——子集边界）。
 *             - **块/片段格式消费**：首行缩进/对齐/制表位/断行规则
 *               全由 W1 的 XTextLayout + XTextOption 兑现（本类逐块
 *               一个 XTextLayout：块文本 + 块级对齐覆盖选项水平位 +
 *               可用宽，行表直读——不重复实现断行）。内联片段格式
 *               （色/粗斜下删/字号/族）在绘制段逐片段应用；**片段
 *               字号/族差异不改行宽**（XTextLayout 单字体度量——
 *               排版用基础字体，子集边界；图片片段按 0 宽占位跳绘）。
 *             - **行距**：Qt 6.8 无文档级行距入口（QTextLayout/
 *               QAbstractTextDocumentLayout 均无 setLineSpacing；行距
 *               属 QTextBlockFormat::lineHeight，本子集块格式无该字
 *               段）⇒ 本类**不设 setLineSpacing**（任务书「按 Qt 取证
 *               取舍」的取证结论：不落地），行高恒 XTextLayout 字体
 *               行高（ascent+descent）。
 *             - 与 Qt 的登记差异：文本为 UTF-8 字节偏移（框架全树口
 *               径）；documentMargin（Qt 缺省 4px 的文档内边距）不在
 *               本类承载（页边距即排版区边界；探针已按
 *               setDocumentMargin(0) 干净配对）；表格/图片对象、
 *               浮动元素、惰性布局不在子集。
 *
 * @note       模块依赖 XTEXTDOCUMENT_ON（文档模型类型）；随 XGUI_ON
 *             与 XTEXTDOCUMENT_ON 双开关裁剪（同 XTextEdit.h 口径）。
 *             本类无新增虚槽（继承 XObject，仅重载 Deinit），故无
 *             _base 虚入口（§497 条款 5：无虚槽新增即无 _base 面）。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTEXTDOCUMENTLAYOUT_H
#define XTEXTDOCUMENTLAYOUT_H

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
#include "XTextLayout.h"
#include "XGeometry.h"
#include "XGuiConfig.h"
#include "XImage.h"

#if XTEXTDOCUMENT_ON

#include "XTextDocument.h"

/* XPainter 前置声明（draw/drawPage 形参；定义见 Graphics/XPainter.h）。 */
typedef struct XPainter XPainter;

/* 块布局上下文（.c 内定义的 opaque 结构：每块一个 XTextLayout + 块拼接
 * 文本 + 片段起始表——绘制时行字节区间映射回片段的依据）。 */
struct XtdlBlockCtx;

/**
 * @brief      全局行表条目（分页布局产物；纯值投影）。
 * @details    m_line 为块内 XTextLayout 行结果直读：字节区间是**块内
 *             相对偏移**（XTextLayout 单块文本口径）、m_x 行起笔已含
 *             对齐偏移（相对可用区左缘）、m_y 为块内布局 y（**全局
 *             位置以 m_yAbs 为准**）；m_yAbs = 文档绝对行顶 y（含页
 *             跳空隙，页 k 的行满足 [k*pageH, (k+1)*pageH)）。
 */
typedef struct XTextDocLayoutRow
{
    int m_blockIndex;       /**< 所属块下标（文档块数组 0 起）。 */
    int m_rowInBlock;       /**< 块内行下标（该块 XTextLayout 行表 0 起）。 */
    int m_yAbs;             /**< 文档绝对行顶 y px（含页跳空隙）。 */
    int m_pageIndex;        /**< 所在页（0 起）。 */
    XTextLayoutLine m_line; /**< 行结果（块内字节区间/自然宽/高/基线）。 */
} XTextDocLayoutRow;

/** @brief 声明 XTextDocumentLayout 类虚函数枚举：继承 XObject（无新增
 *         虚槽，仅重载 Deinit——故无 _base 虚入口）。 */
XCLASS_DEFINE_BEGING(XTextDocumentLayout)
XCLASS_DEFINE_EXTEND_END(XTextDocumentLayout, XObject)

/**
 * @brief      XTextDocumentLayout 类结构体。
 * @note       字段增删一律**尾部追加**并同步 init 清零（MSVC /RTC1，
 *             AGENTS.md 结构体铁律）；本结构 init 用 XMemset 整清。
 */
typedef struct XTextDocumentLayout
{
    XObject m_base;        /**< 继承 XObject。 */

    /* ---- 8 字节对齐组：指针 / 值 ---- */
    XTextDocument* m_document;  /**< 文档（**借用**，不拥有；NULL=空）。 */
    XFont m_font;               /**< 度量/绘制基础字体（深拷贝，拥有）。 */
    XTextOption m_option;       /**< 基础文本选项（值拷贝；块级对齐
                                     在排块时覆盖水平位）。 */
    struct XtdlBlockCtx* m_blockLayouts; /**< 每块一个布局上下文（拥有
                                              实例数组；行表/列宽绘制
                                              共用）。 */
    XTextDocLayoutRow* m_rows;  /**< 全局行表（拥有）。 */
    char* m_headerText;         /**< 页眉单行文本 UTF-8（拥有；NULL=空）。 */
    char* m_footerText;         /**< 页脚单行文本 UTF-8（拥有；NULL=空）。 */

    /* ---- 标量组 ---- */
    int m_blockLayoutCount;     /**< m_blockLayouts 条目数（= 块数）。 */
    int m_blockLayoutCap;       /**< m_blockLayouts 容量。 */
    int m_rowCount;             /**< 全局行表条目数。 */
    int m_rowCap;               /**< 全局行表容量。 */
    int m_contentWidth;         /**< documentSize 宽 px。 */
    int m_contentHeight;        /**< documentSize 高 px。 */
    int m_pageCount;            /**< 页数（页高<=0 恒 1）。 */
    double m_pageWidth;         /**< 页宽（double 存，XSizeF float 边界
                                     转换；Qt 缺省 -1）。 */
    double m_pageHeight;        /**< 页高（<=0 = 不分页，对标 Qt）。 */
    double m_marginLeft;        /**< 页左边距（排版区左缘）。 */
    double m_marginTop;         /**< 页上边距（首行顶/页眉区）。 */
    double m_marginRight;       /**< 页右边距。 */
    double m_marginBottom;      /**< 页下边距（页底界/页脚区）。 */
    int m_lastRevision;         /**< 布局时的文档修订号（revision() 比
                                     对——文档内容变化自动失效行表）。 */

    /* ---- 标志位域组 ---- */
    uint8_t m_dirty : 1;        /**< 行表脏标记（文档/字体/选项/页面参数
                                     变更或文档修订号前进后置位；
                                     ensureLayout 惰性重建）。 */
} XTextDocumentLayout;

/* ========================================================================== */
/*                       生命周期（对标构造/析构）                             */
/* ========================================================================== */

XVtable* XTextDocumentLayout_class_init(void);

/**
 * @brief      初始化布局器（对标 QAbstractTextDocumentLayout(doc) 的
 *             缺省态：文档 NULL、页尺寸 (-1,-1)（Qt QTextDocument 缺省
 *             pageSize 同值）、边距全 0、字体/选项缺省、行表空脏）。
 * @param      self 调用方提供的未初始化存储；不可为 NULL。
 */
void XTextDocumentLayout_init(XTextDocumentLayout* self);

/** @brief 堆上创建（对标 new；调用方 XClassDelete 释放）。 */
#define XTextDocumentLayout_create() \
    XTextDocumentLayout_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
XTextDocumentLayout* XTextDocumentLayout_create_ex(XMemoryType memory);

/* ========================================================================== */
/*                           输入面（对标 setDocument/...）                    */
/* ========================================================================== */

/**
 * @brief      绑定文档（对标 QAbstractTextDocumentLayout 构造入参）。
 * @details    **借用语义**：布局器不拥有文档、不释放；文档内容变化经
 *             revision() 比对在 ensureLayout 时自动感知（对标 Qt
 *             documentChanged 通知的收敛版）。NULL 允许（清空绑定，
 *             行表/尺寸归零、页数 1）。
 * @param      self 目标对象；NULL 无操作。
 * @param      doc 文档指针（借用）；NULL 清空。
 */
void XTextDocumentLayout_setDocument(XTextDocumentLayout* self,
                                     XTextDocument* doc);

/** @brief 读取绑定文档（借用指针；NULL 对象返回 NULL）。 */
XTextDocument* XTextDocumentLayout_document(const XTextDocumentLayout* self);

/**
 * @brief      设置页面尺寸（对标 QTextDocument::setPageSize 语义：原样
 *             保存含负值与 0；**高 <=0 = 不分页**（1 页），宽 <=0 =
 *             不软换行（行单行溢出））。失效行表。
 */
void XTextDocumentLayout_setPageSize(XTextDocumentLayout* self, XSizeF size);

/** @brief 读取页面尺寸（NULL 对象返回 (-1,-1) 缺省）。 */
XSizeF XTextDocumentLayout_pageSize(const XTextDocumentLayout* self);

/**
 * @brief      设置页边距（对标 Qt print 的 rootFrame 四边 margins——
 *             排版区 = 页矩形内缩四边距；首行顶 = 上边距、页底界 =
 *             (页+1)*页高 - 下边距）。负值原样保存（Qt frame margins
 *             同不钳位）。失效行表。
 * @param      self 目标对象；NULL 无操作。
 * @param      left、top、right、bottom 四边边距（px）。
 */
void XTextDocumentLayout_setPageMargins(XTextDocumentLayout* self,
                                        double left, double top,
                                        double right, double bottom);

/** @brief 读取页边距（四出参，均可 NULL；NULL 对象全 0）。 */
void XTextDocumentLayout_pageMargins(const XTextDocumentLayout* self,
                                     double* left, double* top,
                                     double* right, double* bottom);

/**
 * @brief      设置度量/绘制基础字体（深拷贝，失效行表；NULL 回落缺省
 *             字体，XTextLayout_setFont 同口径）。块片段的字号/族/斜体
 *             在绘制段按内联格式修饰；**行宽度量恒用本字体**（子集边
 *             界见头注）。页眉/页脚/页码文本也用本字体绘制。
 */
void XTextDocumentLayout_setFont(XTextDocumentLayout* self, const XFont* font);

/** @brief 读取基础字体（深拷贝到 *out；用完 XClassDeinit 释放）。 */
void XTextDocumentLayout_font(const XTextDocumentLayout* self, XFont* out);

/**
 * @brief      设置基础文本选项（对标 QTextDocument::defaultTextOption；
 *             值拷贝，失效行表）。断行规则/制表/对齐缺省由此承载，
 *             块级对齐（XTDBlock.alignment 水平位）排块时覆盖。
 */
void XTextDocumentLayout_setTextOption(XTextDocumentLayout* self,
                                       const XTextOption* option);

/** @brief 读取基础文本选项（值拷贝到 *out；NULL 对象按缺省选项拷出）。 */
void XTextDocumentLayout_textOption(const XTextDocumentLayout* self,
                                    XTextOption* out);

/**
 * @brief      设置页眉单行文本（UTF-8 深拷贝；NULL/空串清除）。
 * @details    支持占位 %p=当前页号（1 起）、%P=总页数（扩展约定，见
 *             头注「页码/页眉页脚」取证）；绘制在上边距区、左对齐
 *             页左边距，基线 = 页顶 + 字体 ascent。
 */
void XTextDocumentLayout_setHeaderText_utf8(XTextDocumentLayout* self,
                                            const char* utf8);

/** @brief 读取页眉文本（借用 UTF-8 视图；NULL 对象/未设返回 NULL）。 */
const char* XTextDocumentLayout_headerText_const(
    const XTextDocumentLayout* self);

/**
 * @brief      设置页脚单行文本（UTF-8 深拷贝；NULL/空串清除）。
 * @details    占位同页眉；绘制在下边距区、**右对齐页右缘**（Qt print
 *             页码右对齐同款，qtextdocument.cpp:1941-1943），基线 =
 *             页底界（(页+1)*页高 - 下边距）+ 字体 ascent。
 */
void XTextDocumentLayout_setFooterText_utf8(XTextDocumentLayout* self,
                                            const char* utf8);

/** @brief 读取页脚文本（借用 UTF-8 视图；NULL 对象/未设返回 NULL）。 */
const char* XTextDocumentLayout_footerText_const(
    const XTextDocumentLayout* self);

/* ========================================================================== */
/*                 布局结果（查询前惰性 ensureLayout）                         */
/* ========================================================================== */

/**
 * @brief      确保行表有效（脏标记/文档修订号前进时惰性重建）。
 * @return     重建成功返回 true；内存失败保持脏并返回 false（行表可能
 *             为空，查询按空口径返回）。
 */
bool XTextDocumentLayout_ensureLayout(XTextDocumentLayout* self);

/** @brief 页数 = ceil(文档高 / 页高)；页高 <=0 恒 1（Qt dynamicPageCount
 *         口径，含「行放不下一页高」时布局不换页而页数仍按除法的解耦
 *         行为——探针 tiny 实测）。 */
int XTextDocumentLayout_pageCount(XTextDocumentLayout* self);

/**
 * @brief      整文档排版后的总尺寸（Qt documentSize 语义）。
 * @details    高 = 末行底 + 下边距（含页跳空隙；空行表按 0）；宽 =
 *             max(可用宽, 最宽行右缘) + 左右边距（Qt fd->size.width =
 *             qMax(newContentsWidth, contentsWidth) + marginWidth，
 *             qtextdocumentlayout.cpp:3044/:3057）：正常断行下行不越
 *             可用宽 ⇒ 恒等于页宽；病态组合「页宽>0 且左右边距之和
 *             >= 页宽」（可用宽<=0 ⇒ 不软换行 ⇒ 行可越页宽）下宽度
 *             随内容走；页宽 <=0 时取 最宽行右缘 + 右边距。
 */
XSizeF XTextDocumentLayout_documentSize(XTextDocumentLayout* self);

/** @brief 全局行表条目数（self NULL 返回 0）。 */
int XTextDocumentLayout_rowCount(XTextDocumentLayout* self);

/**
 * @brief      取全局行表条目拷贝。
 * @param      index 行下标（0 起，按文档顺序）；越界返回 false。
 * @param      out 输出拷贝；可为 NULL（只做越界判定）。
 */
bool XTextDocumentLayout_rowAt(XTextDocumentLayout* self, int index,
                               XTextDocLayoutRow* out);

/* ========================================================================== */
/*                              绘制面                                         */
/* ========================================================================== */

/**
 * @brief      绘制一页（对标 QTextDocument::print 的 printPage 竖切）。
 * @details    坐标系固定为**页面坐标**（原点 = 页左上，宽高 = 页面
 *             尺寸——任务书 coordinateSystem 可选参的取证取舍：Qt
 *             printPage 无坐标系参数，print 的 translate+clip 由调用
 *             方做，本类把该变换内收，不设第三参）。绘制内容：正文
 *             （该页竖切内的行，逐片段格式）、页眉（上边距区）、页脚
 *             （下边距区）。裁剪由调用方管（本类不 setClip——与 Qt
 *             printPage 的 ctx.clip 由 print 侧设置同构）。
 * @param      self 目标对象；NULL 或 painter 为 NULL 无操作。
 * @param      pageIndex 页号（0 起；越界无操作）。
 * @param      painter 画笔（须已绑定设备）。
 */
void XTextDocumentLayout_drawPage(XTextDocumentLayout* self,
                                  int pageIndex, XPainter* painter);

/**
 * @brief      整文档连续绘制（对标 QAbstractTextDocumentLayout::draw
 *             的最小版：无分页变换，行按文档绝对 y 落笔）。
 * @param      self 目标对象；NULL 或 painter 为 NULL 无操作。
 * @param      clipRect 连续坐标过滤矩形（行带与矩形相交才画；NULL =
 *             全量）。
 */
void XTextDocumentLayout_draw(XTextDocumentLayout* self,
                              XPainter* painter, const XRect* clipRect);

/**
 * @brief      离屏出图一页（对标「打印到 QImage」；导出与测试用）。
 * @details    经 XPainter_begin_image 绑定 image 复用框架既有离屏
 *             路径：白底填充后按 drawPage 页面坐标绘制；页号越界或
 *             绑定失败返回 false（image 保持原状）。
 * @param      self 目标对象；NULL 返回 false。
 * @param      pageIndex 页号（0 起）。
 * @param      image 目标图（借用；调用方 init/释放）。
 * @return     成功 true。
 */
bool XTextDocumentLayout_drawToImage(XTextDocumentLayout* self,
                                     int pageIndex, XImage* image);

#endif /* XTEXTDOCUMENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XGUI_ON */

#endif /* XTEXTDOCUMENTLAYOUT_H */
