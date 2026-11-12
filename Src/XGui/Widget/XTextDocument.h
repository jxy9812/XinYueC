/**
 * @file       XTextDocument.h
 * @brief      XTextDocument 富文本文档模型（对标 Qt 6.8 QTextDocument）。
 * @details    块（block）+ 片段（fragment）二级结构：
 *             - 每个 block = 一段文本 + 段落格式（对齐/缩进/**列表级别与
 *               列表三属性**：style/start/numberSuffix——对标 QTextListFormat，
 *               见 XTDListStyle 与块内 listStyleCode/listStart/listSuffixParen）；
 *             - 每个 fragment = 字符范围内联格式（粗/斜/下划线/删除线/
 *               字色/背景色/字体族/字号/上标/下标）；
 *             - setHtml 解析 HTML 渲染子集（内联嵌套上限两层：栈深 3，
 *               三层格式如 <b><i><u> 同时生效）：b/strong、i/em、u、
 *               s/strike/del、
 *               font(color/size)、br（继承对齐）、p/div(align)、
 *               h1-h6、ul/ol/li、a(href)；实体 &amp;/&lt;/&gt;/&quot;/
 *               &apos;/&#39;/&nbsp;/&#NN;；appendHtml 尾部追加同口径；
 *             - toHtml 生成对应 HTML；
 *             - toPlainText/toRawText 导出纯文本（子集内两者等价）；
 *             - 对标 QTextDocument 核心公共 API 全部方法（Qt 6.8）：
 *               modified/revision、metaInformation（含 CssMedia/
 *               FrontMatter 四类）、find（from+FindFlags）、块查询
 *               （firstBlock/lastBlock/findBlock 族）、
 *               maximumBlockCount、clearUndoRedoStacks、
 *               availableUndo/RedoSteps、baseUrl、clone、资源表
 *               （resource/addResource，对标 ResourceType）、
 *               mightBeRichText/convertFromPlainText 命名空间函数
 *               子集，信号含 contentsChange(from,removed,added)。
 * @note       模块总开关 XTEXTDOCUMENT_ON 定义于 XGuiConfig.h。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTEXTDOCUMENT_H
#define XTEXTDOCUMENT_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XString.h"
#include "XImage.h"
#include "XTextOption.h"
#include "XTextFormat.h"

#if XTEXTDOCUMENT_ON

#define XTD_MAX_FRAGMENTS_PER_BLOCK 64
#define XTD_MAX_BLOCKS 256
#define XTD_MAX_RESOURCES 16     /**< 按名资源表容量上限（有界子集）。 */
#define XTD_MAX_UNDO_STEPS 50    /**< 撤销/重做快照栈深（每栈）。 */

/** @brief 内联字符格式（对标 QTextCharFormat）。 */
typedef struct XTDCharFormat
{
    XString* fontFamily;     /**< 字体族（对象拥有；空=默认）。 */
    XString* anchorHref;   /**< 超链接（对象拥有；空=无链接）。 */
    uint32_t fgColor;      /**< ARGB。 */
    uint32_t bgColor;      /**< ARGB；0=无背景。 */
    /* [内存治理 2026-10-11] 标志位字：6 个 bool 原分散占 6B（含对齐洞），
       合并后 1 字；字号经 xtd_parseFontSize 钳制 ≤96，uint16_t 足够。 */
    uint32_t bold : 1;
    uint32_t italic : 1;
    uint32_t underline : 1;
    uint32_t strikeOut : 1;
    uint32_t superScript : 1;
    uint32_t subScript : 1;
    uint32_t charFormatFlagsUnused : 26; /**< 保留位。 */
    uint16_t fontPointSize;     /**< 字号（磅），0=默认；解析钳制 ≤96。 */
} XTDCharFormat;

/** @brief 文本片段（连续相同格式的字符范围）。 */
typedef struct XTDFragment
{
    XString* text;        /**< UTF-8 文本（对象拥有）；图片片段为空串。 */
    XTDCharFormat fmt;
    XImage* image;        /**< 行内图片（对象拥有深拷贝；NULL=文本片段，
                               经 insertImage 编程接口写入——<img> 按名取
                               图的资源体系未建，见该接口注释）。 */
    /* [§B16 文本格式池 2026-11-16 尾部追加] 片段格式在文档级格式池中的
     * 索引（影子层，渲染路径仍读上方内联 fmt 字段，逐字等价）；
     * -1=未入池（片段新建后由 xtd_fragPoolSync 登记或 clone 重建）。 */
    int32_t m_fmtPoolIndex;
} XTDFragment;

/** @brief 段落对齐（对标 Qt::Alignment）。 */
typedef enum XTDAlignment
{
    XTDAlignment_Left    = 0x01,
    XTDAlignment_Right   = 0x02,
    XTDAlignment_HCenter = 0x04,
    XTDAlignment_Justify = 0x08,
    XTDAlignment_Top     = 0x20,
    XTDAlignment_VCenter = 0x40,
    XTDAlignment_Bottom  = 0x80
} XTDAlignment;

/**
 * @brief 列表样式（对标 QTextListFormat::Style，**数值逐值一致**）。
 * @details 只承载 QTextListFormat 的 Style 枚举（qtextformat.h:730-740：
 *          ListDisc=-1 … ListUpperRoman=-8、ListStyleUndefined=0）。渲染层
 *          按 Qt QTextDocumentLayout::drawList 的同一套分支消费
 *          （qtextdocumentlayout.cpp:2130-2235）：Disc/Circle/Square 画几何
 *          标记（尺寸=行距/3），其余五种画序号文本（QTextList::itemText）。
 * @note    本框架列表按**块**承载（无 QTextList 对象），故 list style 与
 *          「有序位」isOrdered 是两个槽：isOrdered 决定「画序号还是画标记」，
 *          style 决定「序号用哪种进制/标记用哪种形状」。导入器（Markdown /
 *          HTML）两侧恒成对写入，语义与 Qt 的单一 style 等价。
 */
typedef enum XTDListStyle
{
    XTDListStyle_Disc       = -1, /**< 实心圆点（Qt ListDisc）。 */
    XTDListStyle_Circle     = -2, /**< 空心圆圈（Qt ListCircle）。 */
    XTDListStyle_Square     = -3, /**< 实心方块（Qt ListSquare）。 */
    XTDListStyle_Decimal    = -4, /**< 阿拉伯数字（Qt ListDecimal）。 */
    XTDListStyle_LowerAlpha = -5, /**< 小写字母（Qt ListLowerAlpha）。 */
    XTDListStyle_UpperAlpha = -6, /**< 大写字母（Qt ListUpperAlpha）。 */
    XTDListStyle_LowerRoman = -7, /**< 小写罗马数字（Qt ListLowerRoman）。 */
    XTDListStyle_UpperRoman = -8, /**< 大写罗马数字（Qt ListUpperRoman）。 */
    XTDListStyle_Undefined  = 0   /**< 未设（Qt ListStyleUndefined）。 */
} XTDListStyle;

/** @brief XTDBlock::listStyleCode 的两个特殊码（其余 1..8 = |Style|）。 */
#define XTD_LIST_STYLE_CODE_UNSET     0u /**< 未设：渲染层按 isOrdered 回落。 */
#define XTD_LIST_STYLE_CODE_UNDEFINED 9u /**< 显式 ListStyleUndefined：
                                              Qt 语义「该列表无标记」
                                              （qtextdocumentlayout.cpp:
                                              2152-2154 直接 return）。 */

/** @brief 段落（对标 QTextBlock）。 */
typedef struct XTDBlock
{
    XTDFragment fragments[XTD_MAX_FRAGMENTS_PER_BLOCK];
    XString* blockFormat;  /**< 附加块级格式（对象拥有；CSS 类名等）。 */
    int alignment;         /**< XTDAlignment 位组合（位集至 0x80；setter
                                往返任意组合，保留 int）。 */
    /* [内存治理 2026-10-11] 窄整型：片段数 ≤XTD_MAX_FRAGMENTS_PER_BLOCK=64、
       标题级 0..6（h1-h6）——均 ≤255 收 uint8_t；列表缩进级别按 HTML
       嵌套深度，保守保留 int16_t。 */
    int16_t indentLevel;       /**< 列表缩进级别（0=非列表，1 起）。 */
    uint8_t fragmentCount;
    uint8_t headingLevel;      /**< h1-h6，0=普通段落。 */
    /* [内存治理 2026-10-11] 标志位字：列表归属/有序位/新列表位与
       [B5] 列表样式编码/start 有效位/后缀位合并 1 字（9 位已用、23 位保留）。 */
    uint32_t isListItem : 1;       /**< 是否为列表项。 */
    uint32_t isOrdered : 1;        /**< 有序列表（<ol>）。 */
    uint32_t listFresh : 1;        /**< 本层列表的首个列表项（§8.0g5：同层新列表
                                序号重起标记——</ul><ul> 相邻两列表靠它
                                区别于同列表兄弟项；Qt 对应「新 QTextList
                                对象」，渲染层据此把该层序号拉回 start）。 */
    /* [B5 列表属性 2026-11-17] QTextListFormat 的 Style/Start/NumberSuffix
       落进既有保留位（不新增大字段）：Style 取**绝对值**编码
       （|ListDisc|=1 … |ListUpperRoman|=8，0=未设）——Qt 的枚举是 -1..-8
       连续 8 档，**3 bit 装不下「8 档 + 未设」共 9 态**，故收 4 bit；
       Start 的有效位单列 1 bit，因为 0 是合法起始序号（Qt 6.8.0 探针
       `0. a` ⇒ ListStart=0、首项 itemText「0.」），不能用 0 当「未设」。 */
    uint32_t listStyleCode : 4;    /**< XTD_LIST_STYLE_CODE_*：0=未设、
                                        9=显式 Undefined（无标记）、
                                        1=Disc 2=Circle 3=Square 4=Decimal
                                        5=LowerAlpha 6=UpperAlpha
                                        7=LowerRoman 8=UpperRoman。 */
    uint32_t listStartSet : 1;     /**< listStart 有效位（0=未设 ⇒ 取 1）。 */
    uint32_t listSuffixParen : 1;  /**< 有序序号后缀：0='.'（Qt 缺省），
                                        1=')'（Markdown `1)`）。 */
    uint32_t blockFlagsUnused : 23; /**< 保留位（3+4+1+1=9 位已用）。 */
    /* [B5 列表属性 2026-11-17] 尾部追加（结构体初值铁律）：列表起始序号
       （对标 QTextListFormat::Start，Qt 6.6+）。listStartSet=0 时无意义。 */
    int16_t listStart;         /**< 列表起始序号（listStartSet 有效时）。 */
} XTDBlock;

/** @brief 元信息类别（对标 QTextDocument::MetaInformation，数值一致）。 */
typedef enum XTDMetaInformation
{
    XTDMetaInformation_DocumentTitle = 0, /**< 文档标题。 */
    XTDMetaInformation_DocumentUrl   = 1, /**< 文档源 URL。 */
    XTDMetaInformation_CssMedia      = 2, /**< CSS '@media' 选择（Qt 6.3+）。 */
    XTDMetaInformation_FrontMatter   = 3  /**< 前置元材料（Qt 6.8+）。 */
} XTDMetaInformation;

/** @brief 查找选项（对标 QTextDocument::FindFlag，数值一致）。 */
typedef enum XTDFindFlag
{
    XTDFindFlag_Backward         = 0x00001, /**< 反向查找。 */
    XTDFindFlag_CaseSensitively  = 0x00002, /**< 区分大小写（默认不区分）。 */
    XTDFindFlag_WholeWords       = 0x00004  /**< 仅整词匹配。 */
} XTDFindFlag;

/** @brief 撤销/重做栈选择（对标 QTextDocument::Stacks，数值一致）。 */
typedef enum XTDStacks
{
    XTDStacks_UndoStack         = 0x01, /**< 撤销栈。 */
    XTDStacks_RedoStack         = 0x02, /**< 重做栈。 */
    XTDStacks_UndoAndRedoStacks = 0x03  /**< 双栈（默认）。 */
} XTDStacks;

/** @brief 资源类别（对标 QTextDocument::ResourceType，数值一致）。 */
typedef enum XTDResourceType
{
    XTDResourceType_UnknownResource  = 0,
    XTDResourceType_HtmlResource     = 1,
    XTDResourceType_ImageResource    = 2,
    XTDResourceType_StyleSheetResource = 3,
    XTDResourceType_MarkdownResource = 4,
    XTDResourceType_UserResource     = 100
} XTDResourceType;

/** @brief 纯文本转 HTML 的空白模式（对标 Qt::WhiteSpaceMode，数值一致）。 */
typedef enum XTDWhiteSpaceMode
{
    XTDWhiteSpaceMode_Normal = 0, /**< 对标 Qt::WhiteSpaceNormal。 */
    XTDWhiteSpaceMode_Pre    = 1  /**< 对标 Qt::WhiteSpacePre
                                       （convertFromPlainText 默认）。 */
} XTDWhiteSpaceMode;

/** @brief 按名资源条目（resource/addResource 的存储子集）。 */
typedef struct XTDResource
{
    int type;        /**< XTDResourceType；0=空槽。 */
    XString* name;   /**< 资源名（UTF-8 URL；对象拥有）。 */
    XString* text;   /**< 文本型资源内容（对象拥有；图片资源为 NULL）。 */
    XImage* image;   /**< 图片资源（对象拥有深拷贝；文本资源为 NULL）。 */
} XTDResource;

/**
 * @brief      资源提供者回调（对标 QTextDocument::resourceProvider() 承载的
 *             QAbstractTextDocumentLayout::ResourceProvider 函数对象；Qt 以
 *             QVariant 携带，C 侧收成普通函数指针 + userData）。
 *
 * @param userData 注册时携带的用户数据（原样回传；本层不拥有、不释放）。
 * @param type XTDResourceType（与 XTextDocument_resource 的 type 同值）。
 * @param url 请求的资源名（UTF-8；非 NULL 且非空串）。
 * @return ImageResource 返回 const XImage*（**借用**：即用即取，不转交
 *         所有权、**不写入文档资源表**，随提供者自己的生命周期失效）；
 *         文本型资源返回 const char*（借用，UTF-8）；无法提供返回 NULL。
 */
typedef const void* (*XTDResourceProviderFunc)(void* userData, int type,
                                               const char* url);

XCLASS_DEFINE_BEGING(XTextDocument)
XCLASS_DEFINE_EXTEND_END(XTextDocument, XObject)

typedef struct XTextDocument
{
    XObject m_base;        /**< 基类成员；必须是第一个。 */

    /* ---- 8 字节对齐组：指针 / 快照栈 / 值类型（对齐分组前移，消除原
     *      布局 0x81/0x21c/0x3b4/0x3e7/0x3f4 空洞；成员访问一律按名，
     *      无位置初始化/offsetof 依赖） ---- */
    XTDBlock* m_blocks;    /**< 块数组（堆分配）。 */
    /* 撤销/重做栈（实例持有；此前为全局静态，多文档互相污染） */
    char* m_undoStack[50];
    char* m_redoStack[50];
    XString* m_title;      /**< 文档标题（对象拥有；metaInformation 0）。 */
    XString* m_url;        /**< 文档源 URL（对象拥有；metaInformation 1）。 */
    XString* m_cssMedia;   /**< CSS '@media' 选择（对象拥有；metaInformation 2）。 */
    XString* m_frontMatter;/**< 前置元材料（对象拥有；metaInformation 3）。 */
    XString* m_baseUrl;    /**< baseUrl 属性（对象拥有；独立于 DocumentUrl
                                元信息，对标 Qt d->baseUrl 分立存储）。 */
    XString* m_defaultStyleSheet; /**< defaultStyleSheet 属性（对象拥有；
                                       缺省空串；对标 setDefaultStyleSheet）。 */
    XTDCharFormat m_defaultFormat; /**< 每文档默认字符格式（对标 defaultFont
                                        属性的字符格式子集；此前为全局静态，
                                        多文档互相污染）。 */
    XTDResource m_resources[XTD_MAX_RESOURCES]; /**< 按名资源表（对标
                                                     addResource/resource）。 */
    XTDResourceProviderFunc m_resourceProvider; /**< 每文档资源提供者（借用
                                                     回调；查表未命中时回落；
                                                     NULL=不回落本层）。 */
    void* m_resourceProviderUserData; /**< 资源提供者用户数据（原样回传；
                                           本层不拥有、不释放）。 */

    /* ---- 4 字节对齐组：计数与游标 ---- */
    int m_blockCount;      /**< 块数。 */
    int m_capacity;        /**< 块数组容量。 */
    int   m_undoTop;
    int   m_redoTop;
    int m_modified;        /**< 修订计数（对标 revision()；每次内容变化递增）。 */
    int m_maximumBlockCount;   /**< 块数上限（0=不限，对标 maximumBlockCount 默认 0）。 */
    int m_emittedBlockCount;   /**< blockCountChanged 去重发射的上次块数。 */
    int m_emittedCharCount;    /**< contentsChange removed/added 计算基准。 */
    /* ---- 本次变更的精确 contentsChange 区间（对标 Qt finishEdit 的
       documentChange）——由知道确切区间的入口在「模型已改、发射前」登记
       （appendBlock/addFragment/clear/setPlainText/块数上限裁剪）；未登记
       的入口（HTML 解析、撤销快照恢复、格式变更）退回全量汇总口径。 ---- */
    int m_changeFrom;          /**< 精确变更起点（文档字符位置）。 */
    int m_changeRemoved;       /**< 精确移除字符数。 */
    int m_changeAdded;         /**< 精确新增字符数。 */
    int m_resourceCount;       /**< 资源表占用数。 */
    int m_cursorPosition;  /**< 光标位置（字符索引；默认 0）。 */
    int m_defaultCursorMoveStyle; /**< defaultCursorMoveStyle（0=LogicalMoveStyle
                                       缺省 / 1=VisualMoveStyle；对标 Qt 枚举值）。 */
    double m_documentMargin;   /**< documentMargin（默认 4.0；负值原样保存，
                                    本机 Qt 探针 -3 往返成立）。 */
    double m_indentWidth;      /**< indentWidth（默认 40.0；负值原样保存）。 */
    double m_textWidth;        /**< pageSize().width() 存储（Qt：textWidth() ==
                                    pageSize().width()，见 qtextdocument.cpp 的
                                    textWidth/setTextWidth；默认 -1.0 = 未设置）。 */

    /* ---- 1 字节标志组（尾部填充）[内存治理 2026-10-11] 4 个 bool 并入
       1 个 uint32_t 字（对标 XObject 位域风格） ---- */
    uint32_t m_undoRedoEnabled : 1;
    uint32_t m_modifiedFlag : 1;   /**< modified 属性（对标 isModified；默认 false）。 */
    uint32_t m_wasUndoAvailable : 1;   /**< undoAvailable 边沿检测（Qt wasUndoAvailable）。 */
    uint32_t m_wasRedoAvailable : 1;   /**< redoAvailable 边沿检测（Qt wasRedoAvailable）。 */
    uint32_t m_layoutEnabled : 1;      /**< layoutEnabled（默认 true；对标
                                            QTextDocument::isLayoutEnabled 缺省）。 */
    uint32_t m_changeValid : 1;        /**< 本次变更是否登记了精确 contentsChange 区间。 */
    /* useDesignMetrics 位已并入 m_textOption.m_useDesignMetrics（Qt 的
       useDesignMetrics 就是 defaultTextOption 的 design 位，单源化，
       qtextdocument.cpp useDesignMetrics/setUseDesignMetrics）。 */
    uint32_t m_textDocFlagsUnused : 26; /**< 保留位。 */

    /* ---- §B16 有界子集（2026-11-15，尾部追加；本结构无位置初始化器依赖） ---- */
    XTextOption m_textOption;   /**< defaultTextOption（对标 Qt d->defaultTextOption；
                                     缺省 = QTextOption() 后按 QTextDocumentPrivate
                                     构造改设 tabStopDistance=80、wrapMode=
                                     WrapAtWordBoundaryOrAnywhere）。 */
    double m_pageHeight;        /**< pageSize().height()（Qt 缺省 QSizeF(-1,-1)）。 */
    double m_superScriptBaseline; /**< superScriptBaseline（% 字高；缺省 50.0）。 */
    double m_subScriptBaseline;   /**< subScriptBaseline（缺省 100.0/6.0）。 */
    double m_baselineOffset;      /**< baselineOffset（缺省 0.0）。 */
    /* ---- §B16 文本格式池与内容脏标记（2026-11-16，尾部追加） ---- */
    XVector* m_formatPool;      /**< 片段格式池（惰性创建；元素 XTextFormat
                                     值对象，等价 Qt QTextFormatCollection 的
                                     去重格式集合——片段按 m_fmtPoolIndex
                                     复用同一条目；池条目一旦分配不复回收，
                                     对齐 Qt formats[] 的「空洞保留」口径）。 */
    int m_dirtyFrom;            /**< 内容脏区起点（对标 Qt docChangeFrom；
                                     -1=无脏区）。 */
    int m_dirtyOldLength;       /**< 脏区旧长度（对标 docChangeOldLength）。 */
    int m_dirtyLength;          /**< 脏区长度（对标 docChangeLength）。 */

    /* ---- W2-a 光标编辑与撤销元数据（2026-11-16，尾部追加；本结构无位置
     *      初始化器依赖，init 的整块 XMemset 已显式清零——MSVC /RTC1 铁律） ---- */
    int m_editBlockDepth;       /**< 编辑块嵌套深度（对标 Qt
                                     QTextDocumentPrivate::editBlock 层级）。 */
    int m_editBlockFrom;        /**< 块内挂起变更合并区间起点（块内多次编辑按
                                     Qt documentChange 公式合并；endEditBlock
                                     时作为一次 contentsChange 发出）。 */
    int m_editBlockOldLen;      /**< 块内挂起变更旧长（对标 docChangeOldLength）。 */
    int m_editBlockLen;         /**< 块内挂起变更新长（对标 docChangeLength）。 */
    int m_editBlockPending;     /**< 挂起区间有效标志（0=块内尚无编辑）。 */
    int m_editBlockEdits;       /**< 本编辑块内的编辑次数（0=空块——endEditBlock
                                     不发信号、不占撤销步，Qt 同口径）。 */
    int m_editBlockOpIns;       /**< 本编辑块的插入侧光标口径（XTDUndoOp；
                                     首个插入编辑的口径；纯删除块不用）。 */
    int m_editBlockOpRem;       /**< 本编辑块的删除侧光标口径（首个删除编辑
                                     的口径；纯插入块不用）。 */
    int m_editBlockSnapshot;    /**< 块开始时是否压了占位快照（0=没压——
                                     撤销禁用或栈满降级）。 */
    /* 撤销/重做元数据：每步快照的变更区间与光标口径（快照本身只有文本，
     * 光标映射需要「这步改了哪、方向、光标归属」——Qt 由命令自带，快照制
     * 在此补挂平行数组）。一步编辑在 Qt 是至多两条命令（Removed+Inserted，
     * 各带自己的 Operation），undo/redo 按命令序做**两次**光标调整
     * （undo：先摘除新增（opIns）、再回填删除（opRem）；redo 对称）——
     * 本机 Qt 6.8.0 探针 R1/R2（替换选区两方向）实测一致。op = None(0xFF)
     * 的槽位（既有入口的纯文本快照）不动光标——旧行为不变。 */
    int m_undoMetaFrom[XTD_MAX_UNDO_STEPS];
    int m_undoMetaRemoved[XTD_MAX_UNDO_STEPS];
    int m_undoMetaAdded[XTD_MAX_UNDO_STEPS];
    uint8_t m_undoMetaOpIns[XTD_MAX_UNDO_STEPS];
    uint8_t m_undoMetaOpRem[XTD_MAX_UNDO_STEPS];
    int m_redoMetaFrom[XTD_MAX_UNDO_STEPS];
    int m_redoMetaRemoved[XTD_MAX_UNDO_STEPS];
    int m_redoMetaAdded[XTD_MAX_UNDO_STEPS];
    uint8_t m_redoMetaOpIns[XTD_MAX_UNDO_STEPS];
    uint8_t m_redoMetaOpRem[XTD_MAX_UNDO_STEPS];
} XTextDocument;

/**
 * @brief 撤销/重做元数据的光标操作口径（**内部口径**；语义对标
 *        QTextUndoCommand::Operation——Qt 6.8.3 原文在
 *        qtextdocument_p.h:105-108：`KeepCursor = 0, MoveCursor = 1`，
 *        **值序与本枚举相反**。本枚举的值只与本文件自己的常量比较
 *        （None=0xFF 独立哨兵），不与 Qt 数值互通，故维持可读值序）。
 */
typedef enum XTDUndoOp
{
    XTDUndoOp_MoveCursor = 0, /**< 光标在变更尾时随变更走（选区删除、
                                   退格、普通插入；语义同 Qt MoveCursor）。 */
    XTDUndoOp_KeepCursor  = 1, /**< 光标在变更起点时保持（前向删除
                                    deleteChar；语义同 Qt KeepCursor）。 */
    XTDUndoOp_None        = 0xFF /**< 无元数据（既有入口的纯文本快照）：
                                      undo/redo 不动光标。 */
} XTDUndoOp;

XVtable* XTextDocument_class_init(void);
void XTextDocument_init(XTextDocument* self);
#define XTextDocument_create() XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
XTextDocument* XTextDocument_create_ex(XMemoryType memory);

/* ===== 块操作 ===== */
void XTextDocument_clear(XTextDocument* self);
bool XTextDocument_isEmpty(const XTextDocument* self);
int XTextDocument_blockCount(const XTextDocument* self);
/**
 * @brief      文档字符数（对标 QTextDocument::characterCount）。
 * @details    Qt 语义：各块 `QTextBlock::length()` 之和，每块 = 文本字符数
 *             + 1（块分隔符 U+2029）——空文档为 1、`"abc"` 为 4、
 *             `"a\nb"` 为 4（本机 Qt 6.8 探针实测）。
 * @note       单位差异：Qt 按 QChar（UTF-16 码元）计数，本库文本缓冲为
 *             UTF-8，故**文本部分按字节计**（"中文" 记 6 而非 2）；
 *             每块 +1 的分隔符语义与 Qt 一致。
 * @param      self 目标文档；NULL 返回 0。
 * @return     字符数（字节口径 + 块分隔符）。
 */
int XTextDocument_characterCount(const XTextDocument* self);

/* ===== 文本导出 ===== */
char* XTextDocument_toPlainText(const XTextDocument* self);
/** @brief 设置纯文本（对标 QTextDocument::setPlainText）。
 * @details 先按 `clear()` 清空（发射一条 (0, 旧 characterCount, 0) 的
 *          contentsChange），再装入新文本并以 **(0, 0, 新 characterCount)**
 *          的精确区间发射第二条——与 Qt 6.8 的发射口径逐值一致（本机探针
 *          setPlainText("q") 覆盖 "xy\nzw" ⇒ contentsChange(0,6,0) +
 *          contentsChange(0,0,2)）。空串时只有 clear 那一条。
 */
void XTextDocument_setPlainText(XTextDocument* self, const char* utf8);
char* XTextDocument_toHtml(const XTextDocument* self);
/** @brief 设置 HTML 内容（渲染子集；嵌套上限一层）。
 * @param self 目标文档。
 * @param html UTF-8 HTML 文本；NULL 忽略。
 * @return 无返回值（解析完成发射 contentsChanged）。
 */
void XTextDocument_setHtml(XTextDocument* self, const char* html);
/** @brief 导出原始纯文本（对标 QTextDocument::toRawText）。
 * @details Qt 中 toPlainText 会在 toRawText 基础上把 NBSP/分隔符替换为
 *          ASCII 等价物；本库解析时已将 &nbsp; 折叠为普通空格且无分隔符
 *          概念，两者在子集内等价——独立成 API 以对齐调用面。
 * @param self 目标文档。
 * @return 堆分配的 UTF-8 文本（调用方 XFree_System 释放）；失败 NULL。
 */
char* XTextDocument_toRawText(const XTextDocument* self);
/** @brief 深拷贝文档（对标 QTextDocument::clone）。
 * @details 拷贝块/片段（含图片深拷贝）、块级格式、title/url/cssMedia
 *          元信息、默认格式与资源表；不拷贝撤销/重做历史与 baseUrl
 *          （与 Qt clone 同口径——Qt 6.8 clone 亦不拷贝这两者）。
 * @param self 源文档；NULL 返回 NULL。
 * @return 新文档（堆分配，调用方释放）；失败 NULL。
 */
XTextDocument* XTextDocument_clone(const XTextDocument* self);

/* ===== 块级格式 ===== */
void XTextDocument_setBlockAlignment(XTextDocument* self, int blockIndex, int alignment);
int XTextDocument_blockAlignment(const XTextDocument* self, int blockIndex);
void XTextDocument_setBlockHeadingLevel(XTextDocument* self, int blockIndex, int level);

/* ===== 片段操作 ===== */
/** @brief 在指定块尾部追加一个片段（对标 QTextCursor::insertText 于块尾）。
 * @details contentsChange 登记精确区间 `(块文本尾, 0, 片段字节长)`（Qt 探针
 *          尾部 insertText("cd") ⇒ contentsChange(3,0,2)）。
 * @return 新片段下标；失败 -1。
 */
int XTextDocument_addFragment(XTextDocument* self, int blockIndex,
                              const char* text, const XTDCharFormat* fmt);
int XTextDocument_fragmentCount(const XTextDocument* self, int blockIndex);
const XTDFragment* XTextDocument_fragment(const XTextDocument* self, int blockIndex, int fragIndex);

/* ===== 文本插入 ===== */
/* 【冗余 API 清理 2026-11-11】原 XTextDocument_insertText 已删除：实现仅
 * 转调 XTextDocument_addFragment，且 fragIndex/charOffset 两个形参被
 * (void) 弃用（从未生效）；Qt 6.8 的 QTextDocument 也没有 insertText
 * （该名属 QTextCursor::insertText，qtextcursor.h）。全仓库零调用，需要
 * 按块追加请直接走主 API XTextDocument_addFragment。 */
/** @brief 追加一个空块（对标文档尾插入块分隔符）。
 * @details contentsChange 登记精确区间 `(旧 characterCount - 1, 0, 1)`
 *          （Qt 探针 "ab" 后 insertBlock ⇒ contentsChange(2,0,1)）。
 */
void XTextDocument_appendBlock(XTextDocument* self, const XTDCharFormat* fmt);
void XTextDocument_appendText(XTextDocument* self, const char* text,
                              const XTDCharFormat* fmt);
/** @brief 尾部追加 HTML（渲染子集；末块非空时另起新段）。
 * @param self 目标文档。
 * @param html UTF-8 HTML 片段；NULL 忽略。
 * @return 无返回值（解析完成发射 contentsChanged）。
 */
void XTextDocument_appendHtml(XTextDocument* self, const char* html);
/** @brief 末块尾部插入行内图片（图片片段最小子集）。
 * @details 所有权：文档对 image 做 XImage_copyRect 深拷贝并持有，调用方
 *          传入后自行管理原对象生命周期；图片按原尺寸承载（width/height
 *          缩放属性不做——子集边界）。渲染侧（XTextEdit 预览）按图片
 *          原尺寸占一个不可断行的原子片段，底边贴基线绘制。
 * @param self 目标文档。
 * @param image 源图片；NULL 或空图忽略。
 * @return 新片段在末块内的索引；失败返回 -1。
 */
int XTextDocument_insertImage(XTextDocument* self, const XImage* image);

/* ===== 元信息 ===== */
void XTextDocument_setMetaInformation(XTextDocument* self, int info, const char* value);
const char* XTextDocument_metaInformation(const XTextDocument* self, int info);

/* ===== 修改状态（对标 modified 属性族） ===== */
/** @brief 查询修改状态（对标 isModified；默认 false）。
 * @param self 目标文档。
 * @return 自上次 setModified(false)/clear 后内容有变化返回 true。
 */
bool XTextDocument_isModified(const XTextDocument* self);
/** @brief 设置修改状态（对标 setModified 槽；状态翻转发射 modificationChanged）。
 * @param self 目标文档。
 * @param m true=标记已修改；false=复位（保存后调用）。
 */
void XTextDocument_setModified(XTextDocument* self, bool m);
/** @brief 查询修订计数（对标 revision()；每次内容变化递增）。
 * @details Qt 语义（qtextdocument_p.cpp:1256 adjustDocumentChangesAndCursors
 *          的 `++revision`）：按内容编辑次数递增，setModified 不影响；
 *          clear() 经 init 复位到 0（qtextdocument_p.cpp:162）。Qt 文档
 *          另注明仅在撤销启用时保证递增；本库快照制撤销恒递增——简化。
 * @param self 目标文档。
 * @return 修订计数；clear 后归 0。
 */
int XTextDocument_revision(const XTextDocument* self);
/** @brief 查询行数（对标 lineCount）。
 * @details 子集无换行布局（无折行），每块即一行：lineCount == blockCount。
 * @param self 目标文档。
 * @return 行数。
 */
int XTextDocument_lineCount(const XTextDocument* self);

/* ===== 块查询（对标 findBlock 族） ===== */
/** @brief 定位包含字符位置 position 的块（对标 findBlock）。
 * @param self 目标文档。
 * @param position 字符索引；负值取首块，越过文档尾取末块（光标钳位语义）。
 * @return 块指针（指向内部数组，随文档修改可能失效）；失败 NULL。
 */
const XTDBlock* XTextDocument_findBlock(const XTextDocument* self, int position);
/** @brief 按块号取块（对标 findBlockByNumber）。越界返回 NULL。 */
const XTDBlock* XTextDocument_findBlockByNumber(const XTextDocument* self, int blockNumber);
/** @brief 按行号取块（对标 findBlockByLineNumber）。
 * @details 子集无换行布局：每块即一行，行号与块号一一对应。
 */
const XTDBlock* XTextDocument_findBlockByLineNumber(const XTextDocument* self, int lineNumber);
/** @brief 首块（对标 firstBlock）。 */
const XTDBlock* XTextDocument_firstBlock(const XTextDocument* self);
/** @brief 末块（对标 lastBlock）。 */
const XTDBlock* XTextDocument_lastBlock(const XTextDocument* self);
/**
 * @brief 首块（对标 QTextDocument::begin()，qtextdocument.h）。
 * @details Qt 的 begin() 返回指向首块的 QTextBlock 迭代器起点（与
 *          firstBlock() 同值）；本库块模型是平铺数组、无迭代器对象，
 *          QTextBlock 以借用指针 `const XTDBlock*` 承载 ⇒ 本入口就是
 *          firstBlock 的同义转发（命名对齐 Qt 的 begin/end 迭代协议）。
 * @param self 目标文档；可为 NULL（返回 NULL，等价无效 QTextBlock）。
 * @return 首块借用指针；文档恒有至少 1 块（空文档也是 1 空块），故正常
 *         情况下非 NULL。指针随文档修改可能失效（不得跨修改缓存）。
 */
const XTDBlock* XTextDocument_begin(const XTextDocument* self);
/**
 * @brief 过尾哨兵（对标 QTextDocument::end()，qtextdocument.h）。
 * @details Qt 的 end() 返回 `isValid() == false` 的 QTextBlock（迭代
 *          终止条件）。本库以块借用指针承载 QTextBlock，"无效块" 的
 *          自然表达是 **NULL**：本函数恒返回 NULL，头注释即契约
 *          「NULL = 过尾哨兵」。调用方按 `block != XTextDocument_end(doc)`
 *          结束遍历（lastBlock 是最后一个**有效**块，不是过尾）。
 * @param self 目标文档；形参保留以对齐 Qt 的成员函数形态（本实现不读取）。
 * @return 恒 NULL（过尾哨兵，不可解引用）。
 */
const XTDBlock* XTextDocument_end(const XTextDocument* self);

/* ===== 块数上限（对标 maximumBlockCount 属性族） ===== */
/** @brief 查询块数上限（0=不限；对标 maximumBlockCount 默认 0）。 */
int XTextDocument_maximumBlockCount(const XTextDocument* self);
/** @brief 设置块数上限（对标 setMaximumBlockCount）。
 * @details 上限立即生效：超出部分从文档开头移除（Qt 同语义），并以
 *          **精确区间 (0, 被裁块字符数, 0)** 发射一条 contentsChange
 *          （被裁块字符数 = Σ(块文本字节 + 1 个块分隔符)；Qt 探针
 *          maximumBlockCount=2 裁掉 "1"/"2" ⇒ contentsChange(0,4,0)）。
 *          设置同时禁用撤销/重做历史（Qt 同语义）。负值原样保存但按
 *          「不限」处理（不裁剪）。
 * @note   **未发生裁剪时不发射任何内容信号**（Qt 同口径：本机探针在块数
 *          未超限时 setMaximumBlockCount 不产生 contentsChange/
 *          contentsChanged/blockCountChanged）。
 * @param self 目标文档。
 * @param maximum 块数上限；<=0 取消限制。
 */
void XTextDocument_setMaximumBlockCount(XTextDocument* self, int maximum);

/* ===== baseUrl 属性 ===== */
/** @brief 查询 baseUrl（相对资源 URL 的解析基准；对标 baseUrl 属性）。 */
const char* XTextDocument_baseUrl(const XTextDocument* self);
/** @brief 设置 baseUrl（变化时发射 baseUrlChanged；对标 setBaseUrl）。 */
void XTextDocument_setBaseUrl(XTextDocument* self, const char* url);

/* ===== 文档级标量属性（对标 qtextdocument.h:231-268 的轻量公有面） ===== */
/** @brief 查询文档页边距（对标 documentMargin；默认 4.0，负值原样保存）。 */
double XTextDocument_documentMargin(const XTextDocument* self);
/** @brief 设置文档页边距（对标 setDocumentMargin；原样保存，含负值——
 *         本机 Qt 6.8 探针 setDocumentMargin(-3) 后返回 -3）。 */
void XTextDocument_setDocumentMargin(XTextDocument* self, double margin);
/** @brief 查询缩进宽度（对标 indentWidth；默认 40.0，负值原样保存）。 */
double XTextDocument_indentWidth(const XTextDocument* self);
/** @brief 设置缩进宽度（对标 setIndentWidth；探针 setIndentWidth(-1) 返回 -1）。 */
void XTextDocument_setIndentWidth(XTextDocument* self, double width);
/** @brief 查询 textWidth（对标 textWidth；默认 -1 = 未设置）。 */
double XTextDocument_textWidth(const XTextDocument* self);
/** @brief 设置 textWidth（对标 setTextWidth；原样保存；布局计算未建，见 gaps）。 */
void XTextDocument_setTextWidth(XTextDocument* self, double width);
/** @brief 查询 useDesignMetrics（默认 false）。 */
bool XTextDocument_useDesignMetrics(const XTextDocument* self);
/** @brief 设置 useDesignMetrics（布局度量开关；本库无排版引擎，仅属性承载）。 */
void XTextDocument_setUseDesignMetrics(XTextDocument* self, bool enable);
/** @brief 查询 layoutEnabled（对标 isLayoutEnabled；默认 true）。 */
bool XTextDocument_isLayoutEnabled(const XTextDocument* self);
/** @brief 设置 layoutEnabled（对标 setLayoutEnabled；仅属性承载）。 */
void XTextDocument_setLayoutEnabled(XTextDocument* self, bool enable);
/** @brief 查询 defaultStyleSheet（对标 defaultStyleSheet；默认空串）。 */
const char* XTextDocument_defaultStyleSheet(const XTextDocument* self);
/** @brief 设置 defaultStyleSheet（对标 setDefaultStyleSheet；深拷贝保存）。 */
void XTextDocument_setDefaultStyleSheet(XTextDocument* self, const char* sheet);
/** @brief 查询 defaultCursorMoveStyle（0=LogicalMoveStyle 缺省 / 1=VisualMoveStyle）。 */
int XTextDocument_defaultCursorMoveStyle(const XTextDocument* self);
/** @brief 设置 defaultCursorMoveStyle（原样保存）。 */
void XTextDocument_setDefaultCursorMoveStyle(XTextDocument* self, int style);

/* ===== §B16 有界子集：页面尺寸 / 默认文本选项 / 基线偏移（对标
 *      qtextdocument.h 的 pageSize/defaultTextOption 与三个基线偏移属性；
 *      纯状态承载——布局引擎属 §B16 长期项，本组入口不消费布局） ===== */

/**
 * @brief      查询页面尺寸（对标 QTextDocument::pageSize）。
 * @details    Qt 缺省 QSizeF(-1,-1)（QSizeF 默认构造；本机 Qt 6.8.0 探针
 *             pageSize=-1,-1、isValid=false、isNull=false）。宽度与
 *             XTextDocument_textWidth 同源（Qt：textWidth() ==
 *             pageSize().width()）。返回类型 XSizeF 为 float（对标 QSizeF
 *             的框架既有映射，内部以 double 存储，边界处有精度映射）。
 * @param      self 目标文档；NULL 返回 (-1,-1)。
 * @return     页面尺寸值。
 */
XSizeF XTextDocument_pageSize(const XTextDocument* self);

/**
 * @brief      设置页面尺寸（对标 QTextDocument::setPageSize）。
 * @details    Qt 语义：只改 d->pageSize（宽度同时是 textWidth 的读数——
 *             本机 Qt 6.8.0 探针：setPageSize(300,400) 后 textWidth()==300）；
 *             无信号、无 contentsChange（Qt 仅在有布局时通知 relayout）。
 * @param      self 目标文档；NULL 忽略。
 * @param      size 页面尺寸（宽度/高度原样保存，含负值与 0——Qt 探针
 *             pageSize(0,0) 后 textWidth()==0、isNull==true）。
 * @return     无返回值。
 */
void XTextDocument_setPageSize(XTextDocument* self, XSizeF size);

/**
 * @brief      查询默认文本选项（对标 QTextDocument::defaultTextOption）。
 * @details    文档缺省 = QTextOption() 后按 QTextDocumentPrivate 构造改设
 *             tabStopDistance=80、wrapMode=WrapAtWordBoundaryOrAnywhere(4)
 *             （qtextdocument_p.cpp 构造；本机 Qt 6.8.0 探针：align=1、
 *             wrap=4、tab=80、design=0、flags=0、dir=2/Auto）。
 * @param      self 目标文档；NULL 返回 XTextOption_create()（缺省构造值）。
 * @return     选项值拷贝（纯值，无所有权）。
 */
XTextOption XTextDocument_defaultTextOption(const XTextDocument* self);

/**
 * @brief      设置默认文本选项（对标 QTextDocument::setDefaultTextOption）。
 * @details    Qt 语义：整值替换 d->defaultTextOption（本机探针：设入
 *             tab=-1 的裸 QTextOption 后文档选项 tab 变 -1，无字段合并）；
 *             无信号（Qt 仅在有布局时 relayout）。**useDesignMetrics 单源**：
 *             文档的 useDesignMetrics 就是本选项的 design 位
 *             （qtextdocument.cpp useDesignMetrics/setUseDesignMetrics；
 *             本机探针：setDefaultTextOption(design=1) 后 useDesignMetrics()
 *             ==true；setUseDesignMetrics(true) 后选项 design==true）。
 * @param      self 目标文档；NULL 忽略。
 * @param      option 选项值（借用，整值拷贝存储；NULL 忽略）。
 * @return     无返回值。
 */
void XTextDocument_setDefaultTextOption(XTextDocument* self,
                                        const XTextOption* option);

/**
 * @brief      查询默认上标基线（% 字高；对标 QTextDocument::
 *             superScriptBaseline）。
 * @details    Qt 缺省 50.0（QTextCharFormat::superScriptBaseline 的
 *             「属性未设」分支，qtextformat.h；探针同值）。纯标量承载，
 *             无布局消费端（§B16 布局引擎长期项）。
 * @param      self 目标文档；NULL 返回 50.0。
 * @return     基线百分数。
 */
double XTextDocument_superScriptBaseline(const XTextDocument* self);

/** @brief 设置默认上标基线（对标 setSuperScriptBaseline；原样保存，无信号）。 */
void XTextDocument_setSuperScriptBaseline(XTextDocument* self, double baseline);

/**
 * @brief      查询默认下标基线（% 字高；对标 QTextDocument::
 *             subScriptBaseline）。
 * @details    Qt 缺省 100.0/6.0 ≈ 16.6667（QTextCharFormat::
 *             subScriptBaseline 的「属性未设」分支；探针 16.6667 同值）。
 * @param      self 目标文档；NULL 返回 100.0/6.0。
 * @return     基线百分数。
 */
double XTextDocument_subScriptBaseline(const XTextDocument* self);

/** @brief 设置默认下标基线（对标 setSubScriptBaseline；原样保存，无信号）。 */
void XTextDocument_setSubScriptBaseline(XTextDocument* self, double baseline);

/**
 * @brief      查询基线偏移（% 字高；对标 QTextDocument::baselineOffset）。
 * @details    Qt 缺省 0.0；正值上移、负值下移（qtextformat.h 文档口径）。
 * @param      self 目标文档；NULL 返回 0.0。
 * @return     基线偏移百分数。
 */
double XTextDocument_baselineOffset(const XTextDocument* self);

/** @brief 设置基线偏移（对标 setBaselineOffset；原样保存，无信号）。 */
void XTextDocument_setBaselineOffset(XTextDocument* self, double offset);

/* ===== 按名资源表（对标 resource/addResource/ResourceType） ===== */
/** @brief 注册按名资源（对标 addResource）。
 * @details ImageResource：resource 为 const XImage*（深拷贝持有）；
 *          Html/StyleSheet/Markdown/UserResource：resource 为 const char*
 *          （深拷贝持有）；同 type+name 已存在则替换；resource 为 NULL
 *          时移除同名条目；表满（XTD_MAX_RESOURCES）时忽略——有界子集。
 * @param self 目标文档。
 * @param type XTDResourceType；UnknownResource 忽略。
 * @param name 资源名（UTF-8 URL）；NULL/空串忽略。
 * @param resource 资源载荷（语义按 type，见 @details）。
 */
void XTextDocument_addResource(XTextDocument* self, int type,
                               const char* name, const void* resource);
/** @brief 查询按名资源（对标 resource）。
 * @details 查表顺序（对齐 Qt 6.8 qtextdocument.cpp:2151-2168）：资源表 →
 *          文档级 resourceProvider → 进程级 defaultResourceProvider
 *          （后两级择一：设了文档级就不再问默认级）。
 * @param self 目标文档。
 * @param type XTDResourceType。
 * @param name 资源名（UTF-8 URL）。
 * @return ImageResource 返回 const XImage*（借用，勿释放）；文本型返回
 *         const char*（借用）；未命中 NULL。经提供者取得的载荷同样只是
 *         借用、不入表（见 XTDResourceProviderFunc）。
 */
const void* XTextDocument_resource(const XTextDocument* self, int type,
                                   const char* name);

/* ---- 资源提供者（对标 resourceProvider/setResourceProvider 与
 *      静态 defaultResourceProvider/setDefaultResourceProvider，
 *      qtextdocument.h。Qt 用 QVariant 携带函数对象；C 侧以
 *      「函数指针 + userData 双出/双入」表达同一对值） ---- */

/** @brief 查询本文档的资源提供者（对标 QTextDocument::resourceProvider()）。
 * @param self 目标文档；可为 NULL（返回 NULL）。
 * @param outUserData 输出注册时携带的用户数据（可空）。
 * @return 提供者回调；未设置返回 NULL。
 */
XTDResourceProviderFunc XTextDocument_resourceProvider(
    const XTextDocument* self, void** outUserData);
/** @brief 设置本文档的资源提供者（对标 setResourceProvider）。
 * @details 只影响本文档：`XTextDocument_resource` 查表未命中时先问它；
 *          **设了它就不再问**进程级默认提供者（Qt 同：
 *          qtextdocument.cpp:2156-2165 的 `else if (defaultProvider)`；
 *          即使本回调回 NULL 也不落默认提供者）。回调是**借用**，本层不
 *          拥有、不释放 userData；提供者返回的镜像/文本同样只是借用，
 *          **不写入**本文档资源表。
 * @param self 目标文档；可为 NULL（无操作）。
 * @param provider 提供者回调；NULL 表示清除（此时回落默认提供者）。
 * @param userData 用户数据（原样回传，可为 NULL）。
 */
void XTextDocument_setResourceProvider(XTextDocument* self,
                                       XTDResourceProviderFunc provider,
                                       void* userData);
/** @brief 查询进程级默认资源提供者（对标 static defaultResourceProvider）。
 * @param outUserData 输出注册时携带的用户数据（可空）。
 * @return 默认提供者回调；未设置返回 NULL。
 */
XTDResourceProviderFunc XTextDocument_defaultResourceProvider(
    void** outUserData);
/** @brief 设置进程级默认资源提供者（对标 static setDefaultResourceProvider）。
 * @details 进程级单例（对齐 QStyle_defaultStyle 一类的「静态默认」先例）；
 *          只被**未设**文档级提供者的文档消费。回调与 userData 均为借用，
 *          本层不拥有；传 NULL 清除。
 * @param provider 提供者回调；NULL 清除。
 * @param userData 用户数据（原样回传，可为 NULL）。
 */
void XTextDocument_setDefaultResourceProvider(XTDResourceProviderFunc provider,
                                              void* userData);

/* ===== 撤销/重做 ===== */
void XTextDocument_setUndoRedoEnabled(XTextDocument* self, bool enable);
bool XTextDocument_isUndoRedoEnabled(const XTextDocument* self);
bool XTextDocument_isUndoAvailable(const XTextDocument* self);
bool XTextDocument_isRedoAvailable(const XTextDocument* self);
/** @brief 清空指定撤销/重做栈（对标 clearUndoRedoStacks）。
 * @details 清空导致可用性变化时发射 undoAvailable(false)/redoAvailable(false)
 *          （Qt 同语义）。
 * @param self 目标文档。
 * @param stacks XTDStacks 位组合（默认双栈）。
 */
void XTextDocument_clearUndoRedoStacks(XTextDocument* self, int stacks);
/** @brief 撤销步数（对标 availableUndoSteps；禁用时 0）。 */
int XTextDocument_availableUndoSteps(const XTextDocument* self);
/** @brief 重做步数（对标 availableRedoSteps；禁用时 0）。 */
int XTextDocument_availableRedoSteps(const XTextDocument* self);

/* ===== 默认格式 ===== */
/** @brief 设置本文档默认字符格式（对标 defaultFont 属性的格式子集）。
 * @details 每文档独立存储（对标 Qt defaultFont 属性）；深拷贝字符串字段。
 */
void XTextDocument_setDefaultFormat(XTextDocument* self, const XTDCharFormat* fmt);
/** @brief 查询本文档默认字符格式（借用指针；self 为 NULL 返回 NULL）。 */
const XTDCharFormat* XTextDocument_defaultFormat(const XTextDocument* self);

/* ===== §B16 文本格式池（对标 Qt QTextFormatCollection 的去重格式集合） ===== */

/**
 * @brief      枚举文档用到的格式（对标 Qt formatCollection() 的格式集合；
 *         去重枚举：同格式的多个片段共享池中同一条目）。
 * @details    池条目按「XTextFormat_equal 逐属性相等」去重；条目一旦分配
 *         不复回收（Qt formats[] 的空洞保留同口径——片段格式被改后旧条目
 *         仍留在池中，只是不再有片段指向它）。返回值为池条目数。
 * @param      self 目标文档；NULL 返回 0。
 * @param      out 输出数组（借用指针填入；可为 NULL=只取数量）。池条目
 *         为文档内部存储借用，随文档修改/释放失效，禁止释放。
 * @param      cap out 数组容量；out 为 NULL 时忽略。
 * @return     池条目数（去重格式总数）。
 */
int XTextDocument_allFormats(const XTextDocument* self,
                             const XTextFormat** out, int cap);

/**
 * @brief      查询池条目数（allFormats 的数量口径便捷版）。
 */
int XTextDocument_formatPoolCount(const XTextDocument* self);

/**
 * @brief      查询片段格式在池中的索引（未入池/越界返回 -1）。
 * @param      self 目标文档。
 * @param      blockIndex、fragIndex 片段定位。
 * @return     池索引（0 起）；片段不存在或未入池 -1。
 */
int XTextDocument_fragmentPoolIndex(const XTextDocument* self,
                                    int blockIndex, int fragIndex);

/* ===== §B16 内联字符格式 ⇄ 属性格式转换桥 ===== */

/**
 * @brief      内联字符格式 → 属性格式（XTDCharFormat 字段逐项落表）。
 * @details    字段映射（XTextFormatPropertyId 键）：fontFamily→FontFamilies
 *         （Qt 6.8 setFontFamily 同款落表键）、anchorHref→AnchorHref、
 *         bold→FontWeight(400/700)、italic→FontItalic、underline→
 *         FontUnderline（兼容键，fontUnderline() 读路径的回落分支消费）、
 *         strikeOut→FontStrikeOut、superScript/subScript→
 *         TextVerticalAlignment、fgColor→ForegroundBrush、bgColor→
 *         BackgroundBrush、fontPointSize(>0)→FontPointSize。字段「零值」
 *         的落表规则是确定性的（bool=false 也落表、色 0=未落），同一内联
 *         格式恒映射同一属性格式——池去重依赖该确定性。
 * @param      dst 目标属性格式；先释放旧载荷再接收。
 * @param      src 内联格式；NULL 视为空格式（dst 清空为 Invalid）。
 */
void XTextDocument_charFormatToTextFormat(XTextFormat* dst,
                                          const XTDCharFormat* src);

/**
 * @brief      属性格式 → 内联字符格式（转换桥反向；渲染等价性验证用）。
 * @details    只认 CharFormat 关心的键：FontFamilies（String 单族）、
 *         AnchorHref、FontWeight(≥500 视为 bold)、FontItalic、
 *         FontUnderline（兼容键；TextUnderlineStyle 优先，对齐
 *         QTextCharFormat::fontUnderline 的读序）、FontStrikeOut、
 *         TextVerticalAlignment（SuperScript/SubScript）、ForegroundBrush、
 *         BackgroundBrush、FontPointSize（>0 落 uint16，越界钳制 ≤96——
 *         xtd_parseFontSize 同口径）。
 * @param      dst 输出内联格式；**字符串字段（fontFamily/anchorHref）由本
 *         函数新建**，调用方对旧实例须先 xtd 同款清理（或提供空壳）；
 *         dst 为 NULL 忽略。
 * @param      src 属性格式；NULL 视为空格式。
 */
void XTextDocument_textFormatToCharFormat(XTDCharFormat* dst,
                                          const XTextFormat* src);

/* ===== 内容脏标记（对标 QTextDocument::markContentsDirty） ===== */

/**
 * @brief      标记内容脏（对标 markContentsDirty(from, length)，
 *         qtextdocument.cpp:615-625：登记 documentChange 区间并通知布局
 *         重排）。本库无布局消费方（§B16 布局引擎长期项），**区间登记后
 *         保留待查询**，不伪造布局回调。
 * @details    区间合并逐字对齐 QTextDocumentPrivate::documentChange
 *         （qtextdocument_p.cpp:1229-1244）：首次直接登记；后续
 *         start=min(from, dirtyFrom)、end=max(from+length, dirtyFrom+
 *         dirtyLength)、diff=max(0, end-start-dirtyLength)，From=start、
 *         两长度 += diff。
 * @param      self 目标文档；NULL 忽略。
 * @param      from 变更起点（负值原样登记，Qt 不钳）。
 * @param      length 变更长度。
 */
void XTextDocument_markContentsDirty(XTextDocument* self, int from,
                                     int length);

/** @brief 是否有未收口的内容脏区（对标 docChangeFrom >= 0 的判定）。 */
bool XTextDocument_isContentsDirty(const XTextDocument* self);

/** @brief 查询脏区起点（无脏区返回 -1）。 */
int XTextDocument_contentsDirtyFrom(const XTextDocument* self);

/** @brief 查询脏区长度（无脏区返回 0）。 */
int XTextDocument_contentsDirtyLength(const XTextDocument* self);

/**
 * @brief      复位内容脏区（布局消费方收口动作；对标 Qt 布局器在
 *         documentChanged 回调后 `docChangeFrom = -1` 的复位，
 *         qtextdocument.cpp:621-623）。W3 打印/Markdown 消费方接入后
 *         由其布局收口调用。
 */
void XTextDocument_resetContentsDirty(XTextDocument* self);

/* ==================== W2-a 文档级编辑 API（对标 QTextCursor 的
 *    文档侧承载；XTextCursor 与 W3 Markdown/打印消费） ==================== */

/**
 * @brief      在文档位置 pos 插入 UTF-8 文本（对标 QTextCursor::insertText
 *             的无格式重载）。
 * @details    - 位置口径与光标一致：pos ∈ [0, characterCount-1]，越界钳位；
 *               pos 落在块分隔符上（= 块文本尾）视为块尾插入位。
 *             - **格式继承**（qtextcursor.cpp:1857-1889 charFormat 规则）：
 *               块中间/尾部继承**前一个字符**的片段格式；块首且块非空继承
 *               **块首字符**的格式；空块继承**前一块最后一个字符**的格式
 *               （Qt 的分隔符片段格式口径）；文档首空块继承文档缺省格式。
 *             - 文本中的 '\n'、U+2029（UTF-8 E2 80 A9）、'\r'（'\r\n' 合并
 *               一个）按块分隔符处理——拆块插入（qtextcursor.cpp:1441-1457）；
 *               块数达 XTD_MAX_BLOCKS 时多余分隔符丢弃、文本并入当前块
 *               （有界子集，同解析器口径）。
 *             - 与其它编辑入口同账：撤销快照（带元数据）、m_formatPool 影子、
 *               markContentsDirty 区间、精确 contentsChange 区间
 *               (插入起点, 0, 新增字节数)——编辑块内挂起合并（见
 *               beginEditBlock）。
 * @param      self 目标文档；NULL 或 utf8 为 NULL/空串时无操作（返回 0）。
 * @param      pos 文档字符位置（字节口径；越界钳位）。
 * @param      utf8 UTF-8 文本。
 * @return     实际新增的文档字符数（含新块分隔符）；无操作返回 0。
 */
int XTextDocument_insertTextAt(XTextDocument* self, int pos,
                               const char* utf8);

/**
 * @brief      在文档位置 pos 插入带显式内联格式的 UTF-8 文本
 *             （insertTextAt 的格式重载，对标 insertText(text, format)）。
 * @param      self 目标文档。
 * @param      pos 文档字符位置。
 * @param      utf8 UTF-8 文本；NULL/空串无操作（仅按 insertTextAt 口径
 *             返回 0，不额外删选区——选区语义属光标层）。
 * @param      fmt 显式内联格式；NULL 等价 insertTextAt（继承格式）。
 * @return     实际新增的文档字符数；无操作返回 0。
 */
int XTextDocument_insertTextAtFmt(XTextDocument* self, int pos,
                                  const char* utf8, const XTDCharFormat* fmt);

/**
 * @brief      删除文档区间 [from, from+len)（对标 QTextCursorPrivate::remove
 *             的文档承载；removeSelectedText/deleteChar 消费）。
 * @details    - 跨块删除按 Qt 口径合并：**被删区间起点所在块保留**（其块
 *               属性与剩余文本不变），中间整块删除，终点块剩余文本并入
 *               保留块尾部，后续块前移（本机 Qt 6.8.0 探针 N1/N1c：跨块
 *               删除后单块、保留起点块对齐）。
 *             - 区间含文档末尾分隔符时文档收拢为一块（Qt select(Document)+
 *               removeSelectedText 探针口径：删空后恒剩 1 空块）。
 *             - 同账事项同 insertTextAt：撤销快照（带元数据）、池影子、
 *               markContentsDirty、精确 contentsChange (from, len, 0)。
 * @param      self 目标文档。
 * @param      from 删除起点（负值按 0）。
 * @param      len 删除字符数（越尾钳位；<=0 无操作）。
 * @return     实际删除的文档字符数；无操作返回 0。
 */
int XTextDocument_removeRange(XTextDocument* self, int from, int len);

/**
 * @brief      removeRange 的光标操作口径版（XTextCursor 消费）。
 * @param      op XTDUndoOp：KeepCursor=删除起点光标保持（deleteChar 族，
 *             undo 后光标原位，Qt 探针 N2c）；MoveCursor=删除尾随变更
 *             （选区删除时 position 在选区尾/回格，undo 后光标落删除起点，
 *             Qt 探针 N1/N2d）。
 */
int XTextDocument_removeRangeOp(XTextDocument* self, int from, int len,
                                int op);

/**
 * @brief      读取文档位置 pos 的字符格式（内联口径；对标
 *             QTextCursor::charFormat() 的派生规则，qtextcursor.cpp
 *             :1857-1889）：块中/尾继承前一字符的片段格式；块首且块非空
 *             继承块首字符；空块继承前块末字符（Qt 分隔符片段格式口径）；
 *             文档首空块继承文档缺省格式。XTextCursor 的格式读取与插入
 *             继承、W3 直取均走本入口（单一定义点）。
 * @param      self 目标文档。
 * @param      pos 文档字符位置（越界钳位）。
 * @param      out 输出内联格式（先清旧字符串字段再接收；NULL 忽略）。
 */
void XTextDocument_charFormatAt(const XTextDocument* self, int pos,
                                XTDCharFormat* out);

/**
 * @brief      对选区内所有块整值替换段落格式标量（对标
 *             QTextCursor::setBlockFormat 的块遍历，qtextcursor.cpp
 *             :743-784 + 探针 P12；XTextCursor_setBlockFormat 消费）。
 * @details    [from, from+len) 覆盖的每块：alignment/indentLevel/
 *             headingLevel 逐块替换；列表归属与 CSS 类串不在属性承载内
 *             原样保留。同账：撤销快照（op=KeepCursor）、
 *             markContentsDirty、精确 contentsChange (首块起点, 跨块长,
 *             跨块长)（Qt BlockFormatChanged 的 documentChange 口径）。
 * @param      self 目标文档。
 * @param      from 区间起点；len 区间长度（纯定位块范围）。
 * @param      alignment XTDAlignment 位组合（直接落块）。
 * @param      indent 段缩进级（int16_t 范围钳位）。
 * @param      heading 标题级（0-6 之外按 0）。
 * @return     被改写的块数（无变化返回 0）。
 */
int XTextDocument_setBlockFormatRange(XTextDocument* self, int from,
                                      int len, int alignment, int indent,
                                      int heading);

/**
 * @brief      对文档区间 [from, from+len) 应用字符格式（对标
 *             QTextDocumentPrivate::setCharFormat 的 SetFormat/MergeFormat
 *             两档；XTextCursor_setCharFormat/mergeCharFormat 消费）。
 * @details    区间端点先按字节位拆片段（片段边界外的半覆盖片段劈开），
 *             区间内片段整段替换（SetFormat）或在属性空间合并（MergeFormat
 *             ——XTextFormat_merge，冲突 modifier 优先）；逐片段经格式池
 *             upsert 同步影子；快照 + 精确 contentsChange (from, len, len)
 *             （Qt CharFormatChanged 的 documentChange 口径；undo 元数据
 *             delta=0 不动光标）。
 * @param      self 目标文档。
 * @param      from 区间起点（负值按 0）。
 * @param      len 区间长度（越尾钳位；<=0 无操作返回 0）。
 * @param      fmt 字符格式（属性格式口径）；NULL 忽略。
 * @param      merge true=MergeFormat（合并）；false=SetFormat（整段替换）。
 * @return     被应用的片段数；无操作返回 0。
 */
int XTextDocument_setCharFormatRange(XTextDocument* self, int from, int len,
                                     const XTextCharFormat* fmt, bool merge);

/**
 * @brief      在文档位置 pos 插入一个块分隔符（对标 QTextCursor::insertBlock
 *             的文档承载）：pos 所在块按 pos 拆成两块，**两块均继承原块的
 *             段属性**（对齐/缩进/标题级——本机 Qt 6.8.0 探针 N3b：中缝
 *             insertBlock 后两块对齐一致）。
 * @details    同账事项同 insertTextAt：精确 contentsChange (pos, 0, 1)、
 *             撤销快照（带元数据）、池影子（片段拆分后重登记）。
 * @param      self 目标文档。
 * @param      pos 文档字符位置（块首插入=在 pos 前另起新块语义；块尾插入
 *             =原块保持、新空块跟随；越界钳位）。
 * @return     新增字符数（成功 1；块数达 XTD_MAX_BLOCKS 或无效参数返回 0）。
 */
int XTextDocument_insertBlockAt(XTextDocument* self, int pos);

/**
 * @brief      开启编辑块（对标 QTextCursor::beginEditBlock →
 *             QTextDocumentPrivate::beginEditBlock）：块内多次编辑合并为
 *             **一步撤销**——撤销快照只在块开始压一次，信号在 endEditBlock
 *             按合并区间发一次（Qt finishEdit 的编辑块口径；本机探针 N8）。
 *             嵌套调用按深度计数（Qt 同：内层 begin/end 不关块）。
 * @param      self 目标文档；NULL 忽略。
 */
void XTextDocument_beginEditBlock(XTextDocument* self);

/**
 * @brief      关闭编辑块（对标 endEditBlock）：顶层块关闭时——
 *             - 块内有过编辑：按合并区间发一条 contentsChange + 一次
 *               contentsChanged / blockCountChanged（去重），撤销栈里该步
 *               的元数据回填为合并区间；
 *             - 块内零编辑：撤销占位快照弹回（Qt：无命令不占撤销步），
 *               不发任何内容信号。
 * @param      self 目标文档；NULL 忽略。
 */
void XTextDocument_endEditBlock(XTextDocument* self);

/** @brief 当前是否处于编辑块内（对标 isInEditBlock；嵌套期间恒真）。 */
bool XTextDocument_isInEditBlock(const XTextDocument* self);

/* ==================== 信号 ==================== */

void* XTextDocument_contentsChanged_signal(XTextDocument* self);
/** @brief contentsChange(from, charsRemoved, charsAdded) 信号。
 * @details 先于 contentsChanged 与 blockCountChanged 发射（对标 Qt
 *          `QTextDocumentPrivate::finishEdit` 的发射顺序，qtextdocument_p.cpp
 *          :1180-1223：contentsChange → contentsChanged → blockCountChanged）。
 *
 *          **精确区间**（本轮对齐）：知道确切区间的入口登记 from/removed/
 *          added 后按 Qt 口径发射——
 *          - `appendBlock`：(末块文本尾, 0, 1)（新增块的块分隔符；本机 Qt
 *            6.8 探针 "ab" 后 insertBlock ⇒ contentsChange(2,0,1)）；
 *          - `addFragment`：(该块文本尾, 0, 文本字节长)（探针尾部 insertText
 *            ("cd") ⇒ contentsChange(3,0,2)）；
 *          - `clear`：(0, 旧 characterCount, 0)（qtextdocument_p.cpp:245-248
 *            的 `emit q->contentsChange(0, len, 0)`；探针 clear 后
 *            contentsChange(0,6,0)）；
 *          - `setPlainText`：先 clear 一条，再 (0, 0, 新 characterCount)
 *            （探针 setPlainText("q") 覆盖旧内容 ⇒ (0,6,0) + (0,0,2)）；
 *          - **块数上限裁剪**：独立补一条 (0, 被裁块字符数, 0)，被裁块字符
 *            数 = Σ(块文本字节 + 1 个块分隔符)（探针 maximumBlockCount=2
 *            裁掉 "1"/"2" 两块 ⇒ contentsChange(0,4,0)；再裁 "3" ⇒ (0,2,0)）。
 *
 * @note       未登记精确区间的入口（HTML 解析、撤销/重做恢复、格式类变更）
 *             退回**全量汇总**口径：(0, 上次发射后的字符数, 本次裁剪前字符数)。
 *             也即 from 恒 0，removed/added 是整文档字符数口径，不是 Qt 的
 *             字符级差异定位——这是快照制文档的已知偏差，见对齐台账。
 * @note       裁剪与产生它的变更在同一漏斗里连续发两条 contentsChange（Qt 是
 *             两个独立编辑块：中途还各有一条 contentsChanged/blockCountChanged）。
 *             本库收尾只发**一次** contentsChanged/blockCountChanged（最终态）；
 *             需要逐编辑块时序的消费方见对齐台账「deferred」。
 */
void* XTextDocument_contentsChange_signal(XTextDocument* self, int from,
                                          int charsRemoved, int charsAdded);
void* XTextDocument_blockCountChanged_signal(XTextDocument* self, int newCount);
void* XTextDocument_modificationChanged_signal(XTextDocument* self, bool modified);

void XTextDocument_setBlockIndentLevel(XTextDocument* self, int blockIndex, int level);
int XTextDocument_blockIndentLevel(const XTextDocument* self, int blockIndex);
void XTextDocument_setBlockListItem(XTextDocument* self, int blockIndex, bool isItem, bool ordered);

/* ---- 列表格式三属性（对标 QTextListFormat::Style/Start/NumberSuffix）----
 * 与 setBlockListItem 同族：块级标量、非列表块也可写（渲染层只在
 * isListItem 时消费）；三个 setter 各自压一步撤销快照并发一次变更。 */
/**
 * @brief 设置块的列表样式（对标 QTextListFormat::setStyle）。
 * @param self 目标文档。
 * @param blockIndex 块序号（越界忽略）。
 * @param style XTDListStyle（-1..-8 为具体样式；0 = 显式 ListStyleUndefined
 *        —— Qt 语义「该列表无标记」，HTML `<ul type="none">` 走这里）；
 *        其它非法值按「未设」清零（渲染层按 isOrdered 回落 Disc/Decimal）。
 */
void XTextDocument_setBlockListStyle(XTextDocument* self, int blockIndex, int style);
/** @brief 读取块的列表样式（对标 QTextListFormat::style）。 @return
 *         XTDListStyle；未设返回 XTDListStyle_Undefined(0)。 */
int XTextDocument_blockListStyle(const XTextDocument* self, int blockIndex);
/**
 * @brief 设置列表起始序号（对标 QTextListFormat::setStart，Qt 6.6+）。
 * @details 显式记「已设」位——0 是合法起始序号（Qt 6.8.0 探针 `0. a`
 *          ⇒ start=0、首项「0.」），不能与「未设」共用 0。
 * @param start 起始序号（钳到 int16 范围）。
 */
void XTextDocument_setBlockListStart(XTextDocument* self, int blockIndex, int start);
/** @brief 读取列表起始序号（对标 QTextListFormat::start）。 @return
 *         已设则返回设置值；未设返回 1（Qt QTextListFormat 构造即 setStart(1)）。 */
int XTextDocument_blockListStart(const XTextDocument* self, int blockIndex);
/**
 * @brief 设置有序序号后缀字符（对标 QTextListFormat::setNumberSuffix）。
 * @details 本框架只承载 Markdown/HTML 会产生的两档：'.'（Qt 缺省）与
 *          ')'（Markdown `1)`）；其它字符按 '.' 落。
 * @param suffix 后缀字符（'.' 或 ')'）。
 */
void XTextDocument_setBlockListSuffix(XTextDocument* self, int blockIndex, int suffix);
/** @brief 读取有序序号后缀字符。 @return '.'（缺省）或 ')'。 */
char XTextDocument_blockListSuffix(const XTextDocument* self, int blockIndex);
/**
 * @brief 一次写入列表格式（style/start/suffix/listFresh）的原子入口。
 * @details 与逐属性 setter 语义相同，但只压一步快照、只发一次变更——
 *          Markdown 导入逐项调用逐属性 setter 会为同一块产生 4 次全文
 *          快照与 4 条 contentsChanged。HTML 导入共用同一**写层**但不经
 *          本入口（setHtml 是批量路径：不逐块压快照/发信号，末尾统一
 *          xtd_changed，同 Qt 单编辑块收口）。
 * @param style/suffix 同逐属性 setter。
 * @param start 起始序号（恒为「已设」）。
 * @param fresh 该块是否为所在层新列表的首项（序号重起标记）。
 */
void XTextDocument_setBlockListFormat(XTextDocument* self, int blockIndex,
                                      int style, int start, int suffix,
                                      bool fresh);
/**
 * @brief 列表项标记文本（对标 QTextList::itemText，qtextlist.cpp:134-235）。
 * @details 按块自身的 style/start/suffix 生成序号文本：itemNumber =
 *          itemIndex + start - 1；Decimal 十进制、Alpha 26 进制（越界回落
 *          十进制，同 Qt）、Roman 罗马数字（<5000；≥5000 与 Qt 一样返回
 *          "?"，itemNumber<1 回落十进制）。无序三态（Disc/Circle/Square）
 *          返回空串——Qt 渲染层对无序列表画几何标记、从不调用 itemText
 *          （qtextdocumentlayout.cpp:2144-2146/2217-2225）；Qt 的
 *          QTextList::itemText 对无序列表返回裸后缀 "." 是该 switch 的
 *          不可达 default 分支，不作为契约。
 * @param self 目标文档（借用）。
 * @param blockIndex 块序号（越界返回 0）。
 * @param itemIndex 列表内从 **1 起**的序号（渲染层逐层计数同口径）。
 * @param out 输出缓冲；NULL 或 cap<=0 时不做度量、直接返回 0。
 * @param cap 缓冲容量（含 NUL）。
 * @return **实际写入**的字节数（不含 NUL；缓冲不足时按截断后的实际长度
 *         返回，恒 NUL 结尾；无序列表/参数非法返回 0）。
 */
int XTextDocument_listItemText(const XTextDocument* self, int blockIndex,
                               int itemIndex, char* out, int cap);
void XTextDocument_setFragmentBold(XTextDocument* self, int bi, int fi, bool bold);
void XTextDocument_setFragmentItalic(XTextDocument* self, int bi, int fi, bool italic);
void XTextDocument_setFragmentUnderline(XTextDocument* self, int bi, int fi, bool underline);
void XTextDocument_setFragmentStrikeOut(XTextDocument* self, int bi, int fi, bool strikeOut);
void XTextDocument_setFragmentFgColor(XTextDocument* self, int bi, int fi, uint32_t color);
void XTextDocument_setFragmentBgColor(XTextDocument* self, int bi, int fi, uint32_t color);
void XTextDocument_setFragmentFontFamily(XTextDocument* self, int bi, int fi, const char* family);
void XTextDocument_setFragmentFontSize(XTextDocument* self, int bi, int fi, int size);
/** @brief 设置光标位置（QTextCursor 语义简化）。
 * @param self 目标文档。
 * @param position 字符索引（>=0；越界钳位）。
 * @return 无返回值（变化时发射 cursorPositionChanged）。
 */
void XTextDocument_setCursorPosition(XTextDocument* self, int position);
/** @brief 查询光标位置。 @param self 目标文档。 @return 字符索引。 */
int XTextDocument_cursorPosition(const XTextDocument* self);
/** @brief 查找结果（QTextCursor 的 C 承载子集：匹配区间 + 命中标志）。
 * @details 对标 QTextDocument::find 返回的 QTextCursor（本机 Qt 6.8 探针：
 *          find("hello") 命中 "Hello World hello" 的首个匹配 ⇒
 *          position=5（匹配末尾，不含）、anchor=0（匹配起点）、
 *          selectedText="Hello"；反向查找同样 position=末尾、anchor=起点）。
 *          Qt 用 QTextCursor 承载选区与字符格式，本库无光标子系统，故只
 *          复制 find 消费得到的区间信息。
 */
typedef struct XTDTextCursor
{
    int position;      /**< 光标位置 = 匹配末尾（字符索引，不含）；未命中 -1。 */
    int anchor;        /**< 锚点 = 匹配起点（字符索引）；未命中 -1。 */
    bool hasSelection; /**< 命中（等价 Qt cursor.hasSelection()）；未命中 false。 */
} XTDTextCursor;

/** @brief 定位查找文本（对标 QTextDocument::find(subString, 0)）。
 * @details 对齐 Qt 语义：默认大小写不敏感（与旧实现的 XStrstr 直配不同，
 *          见 find_ex）。等价于 find_ex(text, 0, 0)。
 * @note  返回值是本库的「匹配起点」便利口径（不是 Qt 的 cursor.position()）；
 *        需要 Qt 的 QTextCursor 语义（末尾位置 + 选区）请用
 *        XTextDocument_findCursor。
 * @param self 目标文档。
 * @param text UTF-8 查找串；NULL/空串返回 -1。
 * @return 首个匹配的起始字符位置；未命中 -1。
 */
int XTextDocument_find(const XTextDocument* self, const char* text);
/** @brief 带选项查找（对标 QTextDocument::find(subString, from, options)）。
 * @details Qt 返回 QTextCursor；本库 find/find_ex 返回匹配**起始**字符位置
 *          （-1=未命中）的便利口径，QTextCursor 语义见 findCursor。
 *          CaseSensitively 未置时按 ASCII 大小写折叠比较（非 ASCII 字节按
 *          原样比较）；WholeWords 的词边界判定为 ASCII 字母数字（UTF-8
 *          多字节序列视为词内字符）。
 *          反向查找按 Qt 口径取「起点严格小于 from 的最后一个匹配」
 *          （本机 Qt 6.8 探针：aXbXc 反向 from=3 命中 1..2、from=4 命中
 *          3..4；Hello World hello 反向 from=12 命中 0..5、from=13 命中
 *          12..17）。
 * @param self 目标文档。
 * @param text UTF-8 查找串；NULL/空串返回 -1。
 * @param from 起始字符位置；负值按 0，越界按文档尾。
 * @param flags XTDFindFlag 位组合（Backward 时自 from 向前搜索）。
 * @return 匹配起始位置；未命中 -1。
 */
int XTextDocument_find_ex(const XTextDocument* self, const char* text,
                          int from, int flags);
/** @brief 查找并返回 QTextCursor 语义的匹配区间（对标 QTextDocument::find 的
 *         QTextCursor 返回）。
 * @details 正向：自 from 起首个命中；反向：起点严格小于 from 的最后一个
 *          命中（Qt 口径，见 find_ex）。命中时 position=匹配末尾、
 *          anchor=匹配起点、hasSelection=true；未命中三者分别为 -1/-1/false
 *          （等价 Qt 返回 null QTextCursor）。Qt 在跨块（含 '\n'）文本上的
 *          find 不命中（块内查找）；本库按整篇纯文本查找，属既有平铺模型
 *          简化，见 gaps。
 * @param self 目标文档；NULL 视为未命中。
 * @param text UTF-8 查找串；NULL/空串视为未命中。
 * @param from 起始字符位置（负值按 0，越界按文档尾）。
 * @param flags XTDFindFlag 位组合。
 * @return 匹配区间结构（按值返回）。
 */
XTDTextCursor XTextDocument_findCursor(const XTextDocument* self,
                                       const char* text, int from, int flags);
/** @brief 读取指定字符（对标 QTextDocument::characterAt）。
 * @param self 目标文档。
 * @param position 字符索引。
 * @return UTF-8 字符（单字符缓冲）；越界返回 '\0'。
 */
char XTextDocument_characterAt(const XTextDocument* self, int position);
void XTextDocument_undo(XTextDocument* self);
void XTextDocument_redo(XTextDocument* self);
/** @brief 富文本可能性判定（对标 Qt::mightBeRichText 启发式子集）。
 * @details 跳过行首空白与 &lt;?xml ...?&gt; 前缀；"&lt;!doc" 直接判定；
 *          首个换行前查找 '&lt<'，'<' 到 '>' 之间为纯字母数字且命中已知
 *          HTML 元素名表（本库解析子集 + 常用块/表格元素）则判定富文本；
 *          "&lt;" 实体亦判定。
 * @param text UTF-8 文本；NULL/空串返回 false。
 * @return 可能为富文本返回 true。
 */
bool XTextDocument_mightBeRichText(const char* text);
/** @brief 纯文本转 HTML 段落（对标 Qt::convertFromPlainText 子集）。
 * @details 逐字符移植 Qt 实现：'&lt;' '&gt;' '&' 转义；单个 '\n' 转
 *          "&lt;br&gt;\n"、连续 '\n' 折叠为段落分隔（"&lt;/p&gt;"+br*+
 *          "&lt;p&gt;"）；Pre 模式空白以 &amp;nbsp; 承载、制表符展开到
 *          8 列（与 Qt 同口径的列计数）；非 ASCII UTF-8 序列原样透传。
 * @param plain UTF-8 纯文本；NULL 返回 NULL。
 * @param mode XTDWhiteSpaceMode（Pre 为 Qt 默认参数值）。
 * @return 堆分配 HTML（调用方 XFree_System 释放）；失败 NULL。
 */
char* XTextDocument_convertFromPlainText(const char* plain, int mode);
/** @brief baseUrlChanged() 信号（文档源 URL 变化时发射）。 */
void* XTextDocument_baseUrlChanged_signal(XTextDocument* self);
/** @brief cursorPositionChanged() 信号（光标移动时发射）。 */
void* XTextDocument_cursorPositionChanged_signal(XTextDocument* self);
/** @brief documentLayoutChanged() 信号（文档结构变化时发射）。 */
void* XTextDocument_documentLayoutChanged_signal(XTextDocument* self);
/** @brief redoAvailable(bool) 信号（重做可用性变化时发射）。 */
void* XTextDocument_redoAvailable_signal(XTextDocument* self, bool available);
/** @brief undoAvailable(bool) 信号（撤销可用性变化时发射）。 */
void* XTextDocument_undoAvailable_signal(XTextDocument* self, bool available);
/** @brief undoCommandAdded() 信号（新增撤销命令时发射）。 */
void* XTextDocument_undoCommandAdded_signal(XTextDocument* self);
#endif /* XTEXTDOCUMENT_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XTEXTDOCUMENT_H */
