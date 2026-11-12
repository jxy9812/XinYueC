/**
 * @file       XTextDocument.h
 * @brief      XTextDocument 富文本文档模型（对标 Qt 6.8 QTextDocument）。
 * @details    块（block）+ 片段（fragment）二级结构：
 *             - 每个 block = 一段文本 + 段落格式（对齐/缩进/列表级别）；
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
    /* [内存治理 2026-10-11] 标志位字：3 个 bool 合并 1 字。 */
    uint32_t isListItem : 1;       /**< 是否为列表项。 */
    uint32_t isOrdered : 1;        /**< 有序列表（<ol>）。 */
    uint32_t listFresh : 1;        /**< 本层列表的首个列表项（§8.0g5：同层新列表
                                序号重起标记——</ul><ul> 相邻两列表靠它
                                区别于同列表兄弟项）。 */
    uint32_t blockFlagsUnused : 29; /**< 保留位。 */
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
    double m_textWidth;        /**< textWidth（默认 -1.0 = 未设置；对标
                                    QTextDocument 默认 textWidth()==-1）。 */

    /* ---- 1 字节标志组（尾部填充）[内存治理 2026-10-11] 4 个 bool 并入
       1 个 uint32_t 字（对标 XObject 位域风格） ---- */
    uint32_t m_undoRedoEnabled : 1;
    uint32_t m_modifiedFlag : 1;   /**< modified 属性（对标 isModified；默认 false）。 */
    uint32_t m_wasUndoAvailable : 1;   /**< undoAvailable 边沿检测（Qt wasUndoAvailable）。 */
    uint32_t m_wasRedoAvailable : 1;   /**< redoAvailable 边沿检测（Qt wasRedoAvailable）。 */
    uint32_t m_useDesignMetrics : 1;   /**< useDesignMetrics（默认 false）。 */
    uint32_t m_layoutEnabled : 1;      /**< layoutEnabled（默认 true；对标
                                            QTextDocument::isLayoutEnabled 缺省）。 */
    uint32_t m_changeValid : 1;        /**< 本次变更是否登记了精确 contentsChange 区间。 */
    uint32_t m_textDocFlagsUnused : 25; /**< 保留位。 */
} XTextDocument;

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
