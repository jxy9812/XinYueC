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
    bool bold;
    bool italic;
    bool underline;
    bool strikeOut;
    uint32_t fgColor;      /**< ARGB。 */
    uint32_t bgColor;      /**< ARGB；0=无背景。 */
    XString* fontFamily;     /**< 字体族（对象拥有；空=默认）。 */
    int fontPointSize;     /**< 字号（磅），0=默认。 */
    bool superScript;
    bool subScript;
    XString* anchorHref;   /**< 超链接（对象拥有；空=无链接）。 */
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
    int fragmentCount;
    int alignment;         /**< XTDAlignment 位组合。 */
    int indentLevel;       /**< 列表缩进级别（0=非列表，1 起）。 */
    bool isListItem;       /**< 是否为列表项。 */
    bool isOrdered;        /**< 有序列表（<ol>）。 */
    bool listFresh;        /**< 本层列表的首个列表项（§8.0g5：同层新列表
                                序号重起标记——</ul><ul> 相邻两列表靠它
                                区别于同列表兄弟项）。 */
    int headingLevel;      /**< h1-h6，0=普通段落。 */
    XString* blockFormat;  /**< 附加块级格式（对象拥有；CSS 类名等）。 */
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

XCLASS_DEFINE_BEGING(XTextDocument)
XCLASS_DEFINE_EXTEND_END(XTextDocument, XObject)

typedef struct XTextDocument
{
    XObject m_base;        /**< 基类成员；必须是第一个。 */
    XTDBlock* m_blocks;    /**< 块数组（堆分配）。 */
    int m_blockCount;      /**< 块数。 */
    int m_capacity;        /**< 块数组容量。 */
    bool m_undoRedoEnabled;
    /* 撤销/重做栈（实例持有；此前为全局静态，多文档互相污染） */
    char* m_undoStack[50];
    int   m_undoTop;
    char* m_redoStack[50];
    int   m_redoTop;
    XString* m_title;      /**< 文档标题（对象拥有；metaInformation 0）。 */
    XString* m_url;        /**< 文档源 URL（对象拥有；metaInformation 1）。 */
    XString* m_cssMedia;   /**< CSS '@media' 选择（对象拥有；metaInformation 2）。 */
    XString* m_frontMatter;/**< 前置元材料（对象拥有；metaInformation 3）。 */
    XString* m_baseUrl;    /**< baseUrl 属性（对象拥有；独立于 DocumentUrl
                                元信息，对标 Qt d->baseUrl 分立存储）。 */
    int m_modified;        /**< 修订计数（对标 revision()；每次内容变化递增）。 */
    bool m_modifiedFlag;   /**< modified 属性（对标 isModified；默认 false）。 */
    bool m_wasUndoAvailable;   /**< undoAvailable 边沿检测（Qt wasUndoAvailable）。 */
    bool m_wasRedoAvailable;   /**< redoAvailable 边沿检测（Qt wasRedoAvailable）。 */
    int m_maximumBlockCount;   /**< 块数上限（0=不限，对标 maximumBlockCount 默认 0）。 */
    int m_emittedBlockCount;   /**< blockCountChanged 去重发射的上次块数。 */
    int m_emittedCharCount;    /**< contentsChange removed/added 计算基准。 */
    XTDCharFormat m_defaultFormat; /**< 每文档默认字符格式（对标 defaultFont
                                        属性的字符格式子集；此前为全局静态，
                                        多文档互相污染）。 */
    XTDResource m_resources[XTD_MAX_RESOURCES]; /**< 按名资源表（对标
                                                     addResource/resource）。 */
    int m_resourceCount;       /**< 资源表占用数。 */
    int m_cursorPosition;  /**< 光标位置（字符索引；默认 0）。 */
} XTextDocument;

XVtable* XTextDocument_class_init(void);
void XTextDocument_init(XTextDocument* self);
#define XTextDocument_create() XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
XTextDocument* XTextDocument_create_ex(XMemoryType memory);

/* ===== 块操作 ===== */
void XTextDocument_clear(XTextDocument* self);
bool XTextDocument_isEmpty(const XTextDocument* self);
int XTextDocument_blockCount(const XTextDocument* self);
int XTextDocument_characterCount(const XTextDocument* self);

/* ===== 文本导出 ===== */
char* XTextDocument_toPlainText(const XTextDocument* self);
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
int XTextDocument_addFragment(XTextDocument* self, int blockIndex,
                              const char* text, const XTDCharFormat* fmt);
int XTextDocument_fragmentCount(const XTextDocument* self, int blockIndex);
const XTDFragment* XTextDocument_fragment(const XTextDocument* self, int blockIndex, int fragIndex);

/* ===== 文本插入 ===== */
void XTextDocument_insertText(XTextDocument* self, int blockIndex,
                              int fragIndex, int charOffset,
                              const char* text, const XTDCharFormat* fmt);
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
 * @details Qt 仅在撤销启用时保证递增；本库快照制撤销恒递增——简化。
 * @param self 目标文档。
 * @return 修订计数。
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

/* ===== 块数上限（对标 maximumBlockCount 属性族） ===== */
/** @brief 查询块数上限（0=不限；对标 maximumBlockCount 默认 0）。 */
int XTextDocument_maximumBlockCount(const XTextDocument* self);
/** @brief 设置块数上限（对标 setMaximumBlockCount）。
 * @details 上限立即生效：超出部分从文档开头移除（Qt 同语义）；设置同时
 *          禁用撤销/重做历史（Qt 同语义）。负值按 0（不限）处理。
 * @param self 目标文档。
 * @param maximum 块数上限；<=0 取消限制。
 */
void XTextDocument_setMaximumBlockCount(XTextDocument* self, int maximum);

/* ===== baseUrl 属性 ===== */
/** @brief 查询 baseUrl（相对资源 URL 的解析基准；对标 baseUrl 属性）。 */
const char* XTextDocument_baseUrl(const XTextDocument* self);
/** @brief 设置 baseUrl（变化时发射 baseUrlChanged；对标 setBaseUrl）。 */
void XTextDocument_setBaseUrl(XTextDocument* self, const char* url);

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
 * @param self 目标文档。
 * @param type XTDResourceType。
 * @param name 资源名（UTF-8 URL）。
 * @return ImageResource 返回 const XImage*（借用，勿释放）；文本型返回
 *         const char*（借用）；未命中 NULL。
 */
const void* XTextDocument_resource(const XTextDocument* self, int type,
                                   const char* name);

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
 * @details 先于 contentsChanged 发射（Qt 同序）；from 子集恒为 0——
 *          快照制无字符级差异定位，removed/added 为全文档字符数差。
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
/** @brief 定位查找文本（对标 QTextDocument::find(subString, 0)）。
 * @details 对齐 Qt 语义：默认大小写不敏感（与旧实现的 XStrstr 直配不同，
 *          见 find_ex）。等价于 find_ex(text, 0, 0)。
 * @param self 目标文档。
 * @param text UTF-8 查找串；NULL/空串返回 -1。
 * @return 首个匹配的字符位置；未命中 -1。
 */
int XTextDocument_find(const XTextDocument* self, const char* text);
/** @brief 带选项查找（对标 QTextDocument::find(subString, from, options)）。
 * @details Qt 返回 QTextCursor；本库子集返回匹配起始字符位置（-1=未命中）
 *          ——既有简化口径。CaseSensitively 未置时按 ASCII 大小写折叠比较
 *          （非 ASCII 字节按原样比较）；WholeWords 的词边界判定为 ASCII
 *          字母数字（UTF-8 多字节序列视为词内字符）。
 * @param self 目标文档。
 * @param text UTF-8 查找串；NULL/空串返回 -1。
 * @param from 起始字符位置；负值按 0，越界按文档尾。
 * @param flags XTDFindFlag 位组合（Backward 时自 from 向前搜索）。
 * @return 匹配起始位置；未命中 -1。
 */
int XTextDocument_find_ex(const XTextDocument* self, const char* text,
                          int from, int flags);
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
#endif /* XTEXTDOCUMENT_H */