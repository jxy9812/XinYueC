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
 *             - toPlainText 导出纯文本；
 *             - 对标 QTextDocument 核心公共 API 全部方法。
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
    int m_modified;        /**< 修改计数。 */
    int m_cursorPosition;  /**< 光标位置（字符索引；默认 0）。 */
} XTextDocument;

XVtable* XTextDocument_class_init(void);
void XTextDocument_init(XTextDocument* self);
#define XTextDocument_create() XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
XTextDocument* XTextDocument_create_ex(XMemoryType memory);
#define XTextDocument_delete_base(self) XClass_delete_base((XClass*)(self))

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

/* ===== 撤销/重做 ===== */
void XTextDocument_setUndoRedoEnabled(XTextDocument* self, bool enable);
bool XTextDocument_isUndoRedoEnabled(const XTextDocument* self);
bool XTextDocument_isUndoAvailable(const XTextDocument* self);
bool XTextDocument_isRedoAvailable(const XTextDocument* self);

/* ===== 默认格式 ===== */
void XTextDocument_setDefaultFormat(XTextDocument* self, const XTDCharFormat* fmt);
const XTDCharFormat* XTextDocument_defaultFormat(const XTextDocument* self);

/* ==================== 信号 ==================== */

void* XTextDocument_contentsChanged_signal(XTextDocument* self);
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
/** @brief 定位查找文本（对标 QTextDocument::find）。
 * @param self 目标文档。
 * @param text UTF-8 查找串；不能为 NULL。
 * @return 首个匹配的字符位置；未命中 -1。
 */
int XTextDocument_find(const XTextDocument* self, const char* text);
/** @brief 读取指定字符（对标 QTextDocument::characterAt）。
 * @param self 目标文档。
 * @param position 字符索引。
 * @return UTF-8 字符（单字符缓冲）；越界返回 '\0'。
 */
char XTextDocument_characterAt(const XTextDocument* self, int position);
void XTextDocument_undo(XTextDocument* self);
void XTextDocument_redo(XTextDocument* self);
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
void XTextDocument_setHtmlEnhanced(XTextDocument* self, const char* html);
char* XTextDocument_toHtmlEnhanced(const XTextDocument* self);
#endif /* XTEXTDOCUMENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTEXTDOCUMENT_H */