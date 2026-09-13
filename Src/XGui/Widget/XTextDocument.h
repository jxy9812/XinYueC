/**
 * @file       XTextDocument.h
 * @brief      XTextDocument 富文本文档模型（对标 Qt 6.8 QTextDocument）。
 * @details    块（block）+ 片段（fragment）二级结构：
 *             - 每个 block = 一段文本 + 段落格式（对齐/缩进/列表级别）；
 *             - 每个 fragment = 字符范围内联格式（粗/斜/下划线/删除线/
 *               字色/背景色/字体族/字号/上标/下标）；
 *             - setHtml 解析 HTML 子集（b/i/u/s/br/p/div/h1-h6/
 *               font/ul/ol/li/a/span/img 13 类标签 + style 属性）；
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
    char fontFamily[64];
    int fontPointSize;     /**< 字号（磅），0=默认。 */
    bool superScript;
    bool subScript;
    char anchorHref[256];  /**< 超链接（空=无链接）。 */
} XTDCharFormat;

/** @brief 文本片段（连续相同格式的字符范围）。 */
typedef struct XTDFragment
{
    char text[256];       /**< UTF-8 文本。 */
    XTDCharFormat fmt;
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
    int indentLevel;       /**< 列表缩进级别（0=非列表）。 */
    bool isListItem;       /**< 是否为列表项。 */
    bool isOrdered;        /**< 有序列表（<ol>）。 */
    int headingLevel;      /**< h1-h6，0=普通段落。 */
    char blockFormat[64];  /**< 附加块级格式（CSS 类名等）。 */
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
    char m_title[256];
    char m_url[256];
    int m_modified;        /**< 修改计数。 */
} XTextDocument;

XVtable* XTextDocument_class_init(void);
void XTextDocument_init(XTextDocument* self);
#define XTextDocument_create() XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
XTextDocument* XTextDocument_create_ex(XMemoryType memory);
#define XTextDocument_delete_base(self) XObject_delete_base((XObject*)(self))

/* ===== 块操作 ===== */
void XTextDocument_clear(XTextDocument* self);
bool XTextDocument_isEmpty(const XTextDocument* self);
int XTextDocument_blockCount(const XTextDocument* self);
int XTextDocument_characterCount(const XTextDocument* self);

/* ===== 文本导出 ===== */
char* XTextDocument_toPlainText(const XTextDocument* self);
void XTextDocument_setPlainText(XTextDocument* self, const char* utf8);
char* XTextDocument_toHtml(const XTextDocument* self);
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
void XTextDocument_appendHtml(XTextDocument* self, const char* html);

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
void XTextDocument_undo(XTextDocument* self);
void XTextDocument_redo(XTextDocument* self);
void XTextDocument_setHtmlEnhanced(XTextDocument* self, const char* html);
char* XTextDocument_toHtmlEnhanced(const XTextDocument* self);
#endif /* XTEXTDOCUMENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTEXTDOCUMENT_H */