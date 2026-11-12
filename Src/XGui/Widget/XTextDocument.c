#if XGUI_ON

#include "XTextDocument.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XObject.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include <stdarg.h>
#include <string.h>

#if XTEXTDOCUMENT_ON

static void xtd_formatAssign(XTDCharFormat* dst, const XTDCharFormat* src);
static void xtd_formatClear(XTDCharFormat* fmt);
static bool xtd_formatEqual(const XTDCharFormat* a, const XTDCharFormat* b);
static void xtd_fragClear(XTDFragment* frag);
static const char* xtd_fragText(const XTDFragment* frag);
static void xtd_notifyUndoRedo(XTextDocument* self);
static int xtd_enforceMaximumBlockCount(XTextDocument* self);
static void xtd_clearBlocksOnly(XTextDocument* self);
static void xtd_setPlainTextNoHistory(XTextDocument* self, const char* utf8);
static void xtd_saveSnapshot(XTextDocument* self);
static void xtd_changedCore(XTextDocument* self, bool markModified);
static void xtd_clearUndoRedoArrays(XTextDocument* self);
static int xtd_blockCharCount(const XTDBlock* blk);
static void xtd_setPreciseChange(XTextDocument* self, int from, int removed,
                                 int added);
/* 列表格式纯写层（HTML 导入在 xtd_parseHtml 内直接调用，定义在文件后段）。 */
static void xtd_blockApplyListFormat(XTDBlock* blk, int style, int start,
                                     int suffix, bool fresh);

/* ---- §B16 文本格式池（片段内联格式的影子去重层；渲染路径仍读内联
 *      fmt 字段逐字等价，池只承载「文档用到哪些格式」的集合语义） ---- */
static XVector* xtd_poolEnsure(XTextDocument* self);
static void xtd_poolClear(XTextDocument* self);
static int xtd_poolUpsert(XTextDocument* self, const XTDCharFormat* fmt);
static void xtd_fragPoolSync(XTextDocument* self, int blockIndex,
                             int fragIndex);
static void xtd_poolRebuild(XTextDocument* self);

/* ==================== 生命周期 ==================== */

static void VX_td_deinit(XTextDocument* self);

XVtable* XTextDocument_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextDocument)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_td_deinit);
    return XVTABLE_DEFAULT;
}

static void VX_td_deinit(XTextDocument* self)
{
    int i, j;
    if (!self) return;
    if (self->m_blocks) {
        for (i = 0; i < self->m_blockCount; ++i) {
            for (j = 0; j < self->m_blocks[i].fragmentCount; ++j)
                xtd_fragClear(&self->m_blocks[i].fragments[j]);
            if (self->m_blocks[i].blockFormat) {
                XClassDelete(self->m_blocks[i].blockFormat);
                self->m_blocks[i].blockFormat = NULL;
            }
        }
        XFree_System(self->m_blocks);
        self->m_blocks = NULL;
    }
    if (self->m_title) {
        XClassDelete(self->m_title);
        self->m_title = NULL;
    }
    if (self->m_url) {
        XClassDelete(self->m_url);
        self->m_url = NULL;
    }
    if (self->m_cssMedia) {
        XClassDelete(self->m_cssMedia);
        self->m_cssMedia = NULL;
    }
    if (self->m_frontMatter) {
        XClassDelete(self->m_frontMatter);
        self->m_frontMatter = NULL;
    }
    if (self->m_baseUrl) {
        XClassDelete(self->m_baseUrl);
        self->m_baseUrl = NULL;
    }
    if (self->m_defaultStyleSheet) {
        XClassDelete(self->m_defaultStyleSheet);
        self->m_defaultStyleSheet = NULL;
    }
    xtd_formatClear(&self->m_defaultFormat);
    for (i = 0; i < self->m_resourceCount; ++i) {
        XTDResource* res = &self->m_resources[i];
        if (res->name) {
            XClassDelete(res->name);
            res->name = NULL;
        }
        if (res->text) {
            XClassDelete(res->text);
            res->text = NULL;
        }
        if (res->image) {
            XClassDelete((XClass*)res->image);
            res->image = NULL;
        }
    }
    self->m_resourceCount = 0;
    /* §B16 文本格式池：逐条 XTextFormat_deinit 后释放容器。 */
    xtd_poolClear(self);
    /* 撤销/重做快照串由本对象 malloc 入栈，析构必须清栈，
       否则挂起的整篇文本拷贝随对象销毁失联。 */
    xtd_clearUndoRedoArrays(self);
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

void XTextDocument_init(XTextDocument* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init(&self->m_base);
    XClassSetVtable(self, XTextDocument);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
#ifndef XTEXTDOCUMENT_INIT_CAPACITY
#define XTEXTDOCUMENT_INIT_CAPACITY 16
#endif
    /* 撤销/重做块表初始容量：桌面默认 16；嵌入式可经编译选项缩小
     * （XTextDocument 每文档 XTDBlock*sizeof 的常驻开销随之下降）。 */
    self->m_capacity = XTEXTDOCUMENT_INIT_CAPACITY;
    self->m_blocks = (XTDBlock*)XMalloc_System(
        sizeof(XTDBlock) * (size_t)self->m_capacity);
    if (self->m_blocks) XMemset(self->m_blocks, 0,
        sizeof(XTDBlock) * (size_t)self->m_capacity);
    self->m_blockCount = 1; /* 至少一个空块。 */
    self->m_undoRedoEnabled = true;
    self->m_title = XString_create();
    self->m_url = XString_create();
    self->m_cssMedia = XString_create();
    self->m_frontMatter = XString_create();
    self->m_baseUrl = XString_create();
    self->m_defaultStyleSheet = XString_create();
    /* m_modified/m_modifiedFlag/撤销重做栈/资源表/m_defaultFormat 已随
     * XMemset 清零；发射去重基准取初始块数（blockCountChanged 不误发）。 */
    self->m_emittedBlockCount = 1;
    self->m_emittedCharCount = 0;
    self->m_maximumBlockCount = 0;
    self->m_changeFrom = 0;
    self->m_changeRemoved = 0;
    self->m_changeAdded = 0;
    self->m_changeValid = 0u;
    /* 文档级标量属性默认值（本机 Qt 6.8 探针：documentMargin 4.0、
     * indentWidth 40.0、textWidth -1.0、useDesignMetrics false、
     * isLayoutEnabled true、defaultStyleSheet 空串、
     * defaultCursorMoveStyle LogicalMoveStyle=0）。 */
    self->m_documentMargin = 4.0;
    self->m_indentWidth = 40.0;
    self->m_textWidth = -1.0; /* 即 pageSize().width()（Qt 单源：textWidth()
                                   就是 pageSize.width 的读数）。 */
    self->m_layoutEnabled = 1;
    self->m_defaultCursorMoveStyle = 0;
    /* §B16 有界子集：defaultTextOption 缺省 = QTextOption() 后按
     * QTextDocumentPrivate 构造改设（qtextdocument_p.cpp 构造函数：
     * defaultTextOption.setTabStopDistance(80)、setWrapMode(
     * WrapAtWordBoundaryOrAnywhere)；本机 Qt 6.8.0 探针文档缺省选项
     * align=1/wrap=4/tab=80/design=0/flags=0/dir=2）。 */
    self->m_textOption = XTextOption_create();
    self->m_textOption.m_wrapMode = (int)XTextOptionWrap_WordBoundaryOrAnywhere;
    self->m_textOption.m_tabStopDistance = 80.0;
    /* 基线三属性缺省 = QTextCharFormat 属性缺省（qtextformat.h
     * superScriptBaseline/subScriptBaseline/baselineOffset 的「属性未设」
     * 分支：50.0 / 100.0/6.0 / 0.0；本机 Qt 6.8.0 探针 50/16.6667/0 同值）。 */
    self->m_superScriptBaseline = 50.0;
    self->m_subScriptBaseline = 100.0 / 6.0;
    self->m_baselineOffset = 0.0;
    self->m_pageHeight = -1.0; /* Qt pageSize 缺省 QSizeF(-1,-1)（QSizeF
                                  默认构造 wd=ht=-1）。 */
    /* §B16 文本格式池与内容脏标记：池惰性创建（首片段入池时建）；
     * 脏区无 → docChangeFrom 对齐 Qt 缺省 -1（qtextdocument_p.cpp 构造）。 */
    self->m_formatPool = NULL;
    self->m_dirtyFrom = -1;
    self->m_dirtyOldLength = 0;
    self->m_dirtyLength = 0;
}

XTextDocument* XTextDocument_create_ex(XMemoryType memory)
{
    XTextDocument* self = (XTextDocument*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTextDocument_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 内部工具 ==================== */

static void xtd_ensureCapacity(XTextDocument* self, int minCap)
{
    int newCap;
    XTDBlock* p;
    if (!self || minCap <= self->m_capacity) return;
    newCap = self->m_capacity * 2;
    if (newCap < minCap) newCap = minCap;
    p = (XTDBlock*)XRealloc_System(self->m_blocks,
        sizeof(XTDBlock) * (size_t)newCap);
    if (!p) return;
    self->m_blocks = p;
    self->m_capacity = newCap;
}

static void xtd_emitVoid(XTextDocument* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xtd_emitBool(XTextDocument* self, size_t signal, bool value)
{
    XVarList* args = XVarList_Create(XVar(bool, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xtd_emitInt(XTextDocument* self, size_t signal, int value)
{
    XVarList* args = XVarList_Create(XVar(int, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief contentsChange(from, removed, added) 三参信号发射。 */
static void xtd_emitContentsChange(XTextDocument* self, int from,
                                   int removed, int added)
{
    XVarList* args = XVarList_Create(XVar(int, from), XVar(int, removed),
                                     XVar(int, added));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XTextDocument_contentsChange_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 登记本次变更的精确 contentsChange 区间（对标 Qt documentChange）。
 * @details 调用契约：**模型已改完、xtd_changed/xtd_changedCore 之前**登记；
 *          xtd_changedCore 发射后自动清零。未登记的变更按全量汇总口径发射
 *          （见 XTextDocument_contentsChange_signal 的 @note）。
 */
static void xtd_setPreciseChange(XTextDocument* self, int from, int removed,
                                 int added)
{
    if (!self) return;
    self->m_changeFrom = from;
    self->m_changeRemoved = removed;
    self->m_changeAdded = added;
    self->m_changeValid = 1u;
}

/** @brief 撤销/重做可用性边沿检测发射（对标 QTextDocumentPrivate 的
 *         emitUndoAvailable/emitRedoAvailable：仅在可用性翻转时发射）。 */
static void xtd_notifyUndoRedo(XTextDocument* self)
{
    bool undoAvail;
    bool redoAvail;
    if (!self) return;
    undoAvail = self->m_undoRedoEnabled && self->m_undoTop > 0;
    redoAvail = self->m_undoRedoEnabled && self->m_redoTop > 0;
    if (undoAvail != self->m_wasUndoAvailable) {
        self->m_wasUndoAvailable = undoAvail;
        xtd_emitBool(self, (size_t)XTextDocument_undoAvailable_signal,
                     undoAvail);
    }
    if (redoAvail != self->m_wasRedoAvailable) {
        self->m_wasRedoAvailable = redoAvail;
        xtd_emitBool(self, (size_t)XTextDocument_redoAvailable_signal,
                     redoAvail);
    }
}

/** @brief 清空撤销/重做快照并释放内存（供 clear/禁用撤销/显式清栈共用）。 */
static void xtd_clearUndoRedoArrays(XTextDocument* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_undoTop; ++i) XFree_System(self->m_undoStack[i]);
    self->m_undoTop = 0;
    for (i = 0; i < self->m_redoTop; ++i) XFree_System(self->m_redoStack[i]);
    self->m_redoTop = 0;
}

/** @brief 块数上限立即生效：超出部分从文档开头移除（对标
 *         QTextDocumentPrivate::ensureMaximumBlockCount 的"从头移除"语义；
 *         不发射信号——发射由调用方 xtd_changed/setMaximumBlockCount 统一）。
 * @return 被移除的**文档字符数** = Σ(块文本字节 + 1 个块分隔符)；
 *         未裁剪返回 0。Qt 在 ensureMaximumBlockCount 里用
 *         `cursor.selectionEnd() - cursor.selectionStart()` 得到同一口径
 *         （qtextdocument_p.cpp:1696-1700），并在自己的编辑块里发射
 *         contentsChange(0, 该字符数, 0)。 */
static int xtd_enforceMaximumBlockCount(XTextDocument* self)
{
    int remove;
    int i;
    int j;
    int removedChars = 0;
    if (!self || !self->m_blocks || self->m_maximumBlockCount <= 0) return 0;
    if (self->m_blockCount <= self->m_maximumBlockCount) return 0;
    remove = self->m_blockCount - self->m_maximumBlockCount;
    for (i = 0; i < remove; ++i) {
        XTDBlock* blk = &self->m_blocks[i];
        /* 块文本字节 + 块分隔符（QTextBlock::length() 口径）。 */
        removedChars += xtd_blockCharCount(blk) + 1;
        for (j = 0; j < blk->fragmentCount; ++j)
            xtd_fragClear(&blk->fragments[j]);
        if (blk->blockFormat) {
            XClassDelete(blk->blockFormat);
            blk->blockFormat = NULL;
        }
    }
    /* 剩余块整体前移（块为值语义结构，memmove 安全）。 */
    XMemmove(self->m_blocks, self->m_blocks + remove,
             sizeof(XTDBlock) * (size_t)self->m_maximumBlockCount);
    /* memmove 之后尾部 [max, 旧块数) 是幸存块的**重复副本**：不清零会留下
     * 同一片 XString 的第二个指针，下次 clear 只释放 [0, m_blockCount) 的
     * 活块、随后的 setPlainText 填充又复用尾部槽位 ⇒ 二次释放（ASan 实测
     * heap-use-after-free：clear → xtd_fillPlainTextBlocks 的
     * `XClassDelete(frag->text)`）。裁剪槽位由本函数负责清零。 */
    XMemset(self->m_blocks + self->m_maximumBlockCount, 0,
            sizeof(XTDBlock) * (size_t)remove);
    self->m_blockCount = self->m_maximumBlockCount;
    return removedChars;
}

/** @brief 仅清块内容（不动撤销/重做栈、元信息、modified 与信号——
 *         Qt clear 的块级子集；setPlainText/undo 快照恢复共用）。 */
static void xtd_clearBlocksOnly(XTextDocument* self)
{
    int b;
    int j;
    if (!self || !self->m_blocks) return;
    /* 释放全部已用块（含 setHtml 填充的多块），不止 block0：
       此前只清 block0 导致多块 fragment text 泄漏（Phase 3.2）。 */
    for (b = 0; b < self->m_blockCount && b < self->m_capacity; ++b) {
        XTDBlock* blk = &self->m_blocks[b];
        for (j = 0; j < blk->fragmentCount; ++j)
            xtd_fragClear(&blk->fragments[j]);
        if (blk->blockFormat) {
            XClassDelete(blk->blockFormat);
            blk->blockFormat = NULL;
        }
        XMemset(blk, 0, sizeof(XTDBlock));
    }
    self->m_blockCount = 1;
    /* 块全清 ⇒ 片段引用全失 ⇒ 池随之清空（新内容入池时重建）。
     * Qt clear 亦重置 formats 集合（qtextdocument_p.cpp init）。 */
    xtd_poolClear(self);
}

/** @brief 清空按名资源表并释放载荷（对标 Qt clear 的 d->resources.clear()）。 */
static void xtd_clearResources(XTextDocument* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_resourceCount; ++i) {
        XTDResource* res = &self->m_resources[i];
        if (res->name) {
            XClassDelete(res->name);
            res->name = NULL;
        }
        if (res->text) {
            XClassDelete(res->text);
            res->text = NULL;
        }
        if (res->image) {
            XClassDelete((XClass*)res->image);
            res->image = NULL;
        }
    }
    self->m_resourceCount = 0;
}

/** @brief 深拷贝字符格式（含字符串字段）。 */
static void xtd_formatAssign(XTDCharFormat* dst, const XTDCharFormat* src)
{
    if (!dst || !src || dst == src) return;
    if (dst->fontFamily && src->fontFamily)
        XString_assign(dst->fontFamily, src->fontFamily);
    else if (!dst->fontFamily && src->fontFamily)
        dst->fontFamily = XString_create_copy(src->fontFamily);
    if (dst->anchorHref && src->anchorHref)
        XString_assign(dst->anchorHref, src->anchorHref);
    else if (!dst->anchorHref && src->anchorHref)
        dst->anchorHref = XString_create_copy(src->anchorHref);
    dst->bold = src->bold;
    dst->italic = src->italic;
    dst->underline = src->underline;
    dst->strikeOut = src->strikeOut;
    dst->fgColor = src->fgColor;
    dst->bgColor = src->bgColor;
    dst->fontPointSize = src->fontPointSize;
    dst->superScript = src->superScript;
    dst->subScript = src->subScript;
}

/** @brief 释放字符格式字符串字段。 */
static void xtd_formatClear(XTDCharFormat* fmt)
{
    if (!fmt) return;
    if (fmt->fontFamily) {
        XClassDelete(fmt->fontFamily);
        fmt->fontFamily = NULL;
    }
    if (fmt->anchorHref) {
        XClassDelete(fmt->anchorHref);
        fmt->anchorHref = NULL;
    }
}

/** @brief 格式相等比较（对标 memcmp 语义，字符串按内容比较）。 */
static bool xtd_formatEqual(const XTDCharFormat* a, const XTDCharFormat* b)
{
    if (!a || !b) return a == b;
    if (a->bold != b->bold || a->italic != b->italic ||
        a->underline != b->underline || a->strikeOut != b->strikeOut ||
        a->fgColor != b->fgColor || a->bgColor != b->bgColor ||
        a->fontPointSize != b->fontPointSize ||
        a->superScript != b->superScript || a->subScript != b->subScript)
        return false;
    if ((a->fontFamily == NULL) != (b->fontFamily == NULL)) return false;
    if (a->fontFamily && !XString_equals(a->fontFamily, b->fontFamily,
                                         XChar_CaseSensitive)) return false;
    if ((a->anchorHref == NULL) != (b->anchorHref == NULL)) return false;
    if (a->anchorHref && !XString_equals(a->anchorHref, b->anchorHref,
                                         XChar_CaseSensitive)) return false;
    return true;
}

/** @brief 释放片段全部字符串与图片并清零。 */
static void xtd_fragClear(XTDFragment* frag)
{
    if (!frag) return;
    if (frag->text) {
        XClassDelete(frag->text);
        frag->text = NULL;
    }
    if (frag->image) {
        XClassDelete((XClass*)frag->image);
        frag->image = NULL;
    }
    xtd_formatClear(&frag->fmt);
}

/** @brief 读取片段文本（空串安全）。 */
static const char* xtd_fragText(const XTDFragment* frag)
{
    const char* t;
    if (!frag || !frag->text) return "";
    t = XString_toUtf8(frag->text);
    return t ? t : "";
}

/* ==================== §B16 文本格式池 ==================== */

/* 池元素（XTextFormat）的容器回调：XVector 按 typeSize 裸字节管理元素，
 * XTextFormat 内含属性表 XVector（堆资源）——必须挂深拷贝/移动/析构三件，
 * 否则 push 共享载荷、释放漏析构。 */

static void xtd_poolElemDeinitCb(void* data)
{
    if (data) XTextFormat_deinit((XTextFormat*)data);
}

static void xtd_poolElemCopyCb(void* data, const void* sData)
{
    XTextFormat* d;
    const XTextFormat* s;
    if (!data || !sData || data == sData) return;
    d = (XTextFormat*)data;
    s = (const XTextFormat*)sData;
    memset(d, 0, sizeof(XTextFormat)); /* 新槽清零（虚表空 → copy 走 init+深拷贝） */
    XTextFormat_copy(d, s);
}

static void xtd_poolElemMoveCb(void* data, const void* sData)
{
    XTextFormat* d;
    XTextFormat* s;
    if (!data || !sData || data == sData) return;
    d = (XTextFormat*)data;
    s = (XTextFormat*)sData;
    memset(d, 0, sizeof(XTextFormat));
    *d = *s;                        /* 结构浅转：属性表缓冲随之转移 */
    memset(s, 0, sizeof(XTextFormat)); /* 源置空 */
}

/** @brief 惰性建池（元素=XTextFormat 值；useCow=false——元素含堆资源）。 */
static XVector* xtd_poolEnsure(XTextDocument* self)
{
    if (!self) return NULL;
    if (self->m_formatPool) return self->m_formatPool;
    self->m_formatPool = XVector_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           sizeof(XTextFormat), false);
    if (!self->m_formatPool) return NULL;
    XContainerSetDataCopyMethod(self->m_formatPool, xtd_poolElemCopyCb);
    XContainerSetDataMoveMethod(self->m_formatPool, xtd_poolElemMoveCb);
    XContainerSetDataDeinitMethod(self->m_formatPool, xtd_poolElemDeinitCb);
    return self->m_formatPool;
}

/** @brief 清空并释放池（条目逐条 XTextFormat_deinit；NULL 幂等）。 */
static void xtd_poolClear(XTextDocument* self)
{
    if (!self || !self->m_formatPool) return;
    XClassDelete((XClass*)self->m_formatPool); /* 堆容器：析构(逐条回调)+free */
    self->m_formatPool = NULL;
}

/** @brief 池条目数。 */
static int xtd_poolCount(const XVector* pool)
{
    if (!pool) return 0;
    return (int)XVector_size_base((const XContainer*)pool);
}

/**
 * @brief  内联格式入池（去重 upsert；对标 QTextFormatCollection 的
 *         indexForFormat：等价格式复用索引，否则追加新条目）。
 * @return 池索引；建池失败 -1（影子层失效不碍渲染——片段 m_fmtPoolIndex
 *         留 -1 语义）。
 */
static int xtd_poolUpsert(XTextDocument* self, const XTDCharFormat* fmt)
{
    XVector* pool;
    XTextFormat candidate;
    int i;
    int n;
    pool = xtd_poolEnsure(self);
    if (!pool) return -1;
    XTextDocument_charFormatToTextFormat(&candidate, fmt);
    n = xtd_poolCount(pool);
    for (i = 0; i < n; ++i) {
        const XTextFormat* f =
            (const XTextFormat*)XVector_at_base(pool, i);
        if (f && XTextFormat_equal(f, &candidate)) {
            XTextFormat_deinit(&candidate);
            return i;
        }
    }
    XVector_push_back_1_base(pool, &candidate); /* 回调深拷贝进池 */
    XTextFormat_deinit(&candidate);
    return n;
}

/** @brief 片段格式同步进池（影子层登记；渲染读内联字段不受影响）。 */
static void xtd_fragPoolSync(XTextDocument* self, int blockIndex,
                             int fragIndex)
{
    XTDFragment* frag;
    if (!self || !self->m_blocks) return;
    if (blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    if (fragIndex < 0 || fragIndex >= self->m_blocks[blockIndex].fragmentCount)
        return;
    frag = &self->m_blocks[blockIndex].fragments[fragIndex];
    frag->m_fmtPoolIndex = xtd_poolUpsert(self, &frag->fmt);
}

/** @brief 全文档片段重新入池（clone 后/池清空后重建）。 */
static void xtd_poolRebuild(XTextDocument* self)
{
    int b;
    int j;
    xtd_poolClear(self);
    if (!self || !self->m_blocks) return;
    for (b = 0; b < self->m_blockCount; ++b)
        for (j = 0; j < self->m_blocks[b].fragmentCount; ++j)
            xtd_fragPoolSync(self, b, j);
}

/** @brief 内容变化统一出口（对标 Qt finishEdit 的信号编排子集）：
 *         maximumBlockCount 钳位 → [markModified 时 revision 递增与
 *         modified 翻转发射 modificationChanged(true)] →
 *         contentsChange（精确区间优先，未登记则全量汇总）→
 *         [裁剪发生则补一条精确 (0, 被裁字符数, 0)] → contentsChanged →
 *         blockCountChanged（去重）。
 *
 *         发射顺序对齐 Qt `QTextDocumentPrivate::finishEdit`
 *         （qtextdocument_p.cpp:1180-1223：contentsChange → contentsChanged
 *         → blockCountChanged）。
 *
 *         clear 走 markModified=false（Qt：clear 私有直置 modified=false，
 *         不发 modificationChanged，且清空后文档保持未修改态）。 */
static void xtd_changedCore(XTextDocument* self, bool markModified)
{
    if (self) {
        int nowChars;
        int preTrimChars;
        int trimmedChars;
        if (markModified) {
            self->m_modified++;
            if (!self->m_modifiedFlag) {
                self->m_modifiedFlag = true;
                xtd_emitBool(self,
                             (size_t)XTextDocument_modificationChanged_signal,
                             true);
            }
        }
        /* 先记裁剪前字符数：汇总口径的 added 按「裁剪前」算，裁剪本身
         * 另发一条精确区间（Qt：ensureMaximumBlockCount 是独立编辑块）。 */
        preTrimChars = XTextDocument_characterCount(self);
        trimmedChars = xtd_enforceMaximumBlockCount(self);
        /* 变更本身的 contentsChange：登记过精确区间就按登记的 from/removed/
         * added 发射（Qt documentChange 子集）；否则退回全量汇总
         * (0, 上次发射后的字符数, 裁剪前字符数)。 */
        if (self->m_changeValid) {
            xtd_emitContentsChange(self, self->m_changeFrom,
                                   self->m_changeRemoved,
                                   self->m_changeAdded);
        } else {
            xtd_emitContentsChange(self, 0, self->m_emittedCharCount,
                                   preTrimChars);
        }
        self->m_changeValid = 0u;   /* 登记一次性消费，异常路径也不残留。 */
        /* 块数上限裁剪：Qt 在自己的编辑块里发 contentsChange(0, 被裁字符数, 0)
         * （qtextdocument_p.cpp:1694-1707）。 */
        if (trimmedChars > 0)
            xtd_emitContentsChange(self, 0, trimmedChars, 0);
        nowChars = XTextDocument_characterCount(self);
        self->m_emittedCharCount = nowChars;
        xtd_emitVoid(self, (size_t)XTextDocument_contentsChanged_signal);
        if (self->m_blockCount != self->m_emittedBlockCount) {
            self->m_emittedBlockCount = self->m_blockCount;
            xtd_emitInt(self,
                        (size_t)XTextDocument_blockCountChanged_signal,
                        self->m_blockCount);
        }
    }
}

static void xtd_changed(XTextDocument* self)
{
    xtd_changedCore(self, true);
}

/* ==================== 块操作 ==================== */

void XTextDocument_clear(XTextDocument* self)
{
    int oldChars;
    if (!self) return;
    /* 对标 QTextDocument::clear（d->clear()）：清块内容 + 清标题元信息
     * （title.clear()）+ 清资源表（resources.clear()）+ 清撤销/重做栈
     * （UndoAndRedoStacks）+ modified 复位（私有直置，不发
     * modificationChanged——Qt 同口径）；修订计数随 init 复位（Qt
     * QTextDocumentPrivate::init 的 `revision = -1`，随后初始块插入使
     * revision 归 0，qtextdocument_p.cpp:162）。
     *
     * **信号口径**：Qt 的私有 clear 在 `inContentsChange` 保护下直接发
     * 一条 `contentsChange(0, len, 0)`（len = 清空前的文档字符数，
     * qtextdocument_p.cpp:245-248；本机探针 clear "hello" ⇒ (0,6,0)），
     * **不经 finishEdit ⇒ 不发 contentsChanged / blockCountChanged**
     * （QTextEdit::clear 探针里额外的 changed/BCC 来自控制层，不是文档）。
     * 本函数按同一口径发射，并把去重/汇总基准同步到「1 个空块」。 */
    oldChars = XTextDocument_characterCount(self);
    xtd_clearBlocksOnly(self);
    if (self->m_title) XString_assign_utf8(self->m_title, "");
    xtd_clearResources(self);
    xtd_clearUndoRedoArrays(self);
    self->m_modifiedFlag = false;
    self->m_modified = 0;
    xtd_notifyUndoRedo(self);
    xtd_emitContentsChange(self, 0, oldChars, 0);
    self->m_emittedCharCount = XTextDocument_characterCount(self);
    self->m_emittedBlockCount = self->m_blockCount;
    self->m_changeValid = 0u;
}

bool XTextDocument_isEmpty(const XTextDocument* self)
{
    int i, j;
    if (!self) return true;
    for (i = 0; i < self->m_blockCount; ++i) {
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j) {
            if (xtd_fragText(&self->m_blocks[i].fragments[j])[0]) return false;
        }
    }
    return true;
}

int XTextDocument_blockCount(const XTextDocument* self)
{
    return self ? self->m_blockCount : 0;
}

int XTextDocument_characterCount(const XTextDocument* self)
{
    int i, j, total = 0;
    if (!self) return 0;
    for (i = 0; i < self->m_blockCount; ++i) {
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j)
            total += (int)XStrlen(xtd_fragText(&self->m_blocks[i].fragments[j]));
        /* 块分隔符：Qt 的 QTextBlock::length() = 文本长 + 1（块分隔符
           U+2029），QTextDocument::characterCount() 为各块 length() 之和
           （本机 Qt 6.8 探针：空文档 1、"abc" 4、"a\nb" 4 ⇒ 即
           文本字符数 + 块数）。 */
        ++total;
    }
    return total;
}

/* ==================== 文本导出 ==================== */

char* XTextDocument_toPlainText(const XTextDocument* self)
{
    int i, j, total = 1;
    char* out;
    size_t o = 0;
    if (!self) return NULL;
    for (i = 0; i < self->m_blockCount; ++i)
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j)
            total += (int)XStrlen(xtd_fragText(&self->m_blocks[i].fragments[j]));
    total += self->m_blockCount; /* 换行符。 */
    out = (char*)XMalloc_System((size_t)total);
    if (!out) return NULL;
    out[0] = '\0';
    for (i = 0; i < self->m_blockCount; ++i) {
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j) {
            const char* t = xtd_fragText(&self->m_blocks[i].fragments[j]);
            size_t len = XStrlen(t);
            XMemcpy(out + o, t, len); o += len;
        }
        if (i + 1 < self->m_blockCount) out[o++] = '\n';
    }
    out[o] = '\0';
    return out;
}

/** @brief 按 '\n' 拆分填充块（setPlainText 与撤销快照恢复共用）。
 * @details 容量增长前补零新增槽位（realloc 不清零，旧实现直接抬
 *          m_blockCount 会让析构/clear 读到未初始化块的垃圾 fragment
 *          指针——越界读修复，见历史注）。 */
static void xtd_fillPlainTextBlocks(XTextDocument* self, const char* utf8)
{
    const char* p;
    int blockIdx = 0;
    p = utf8;
    while (*p) {
        const char* nl = XStrchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : XStrlen(p);
        if (blockIdx >= XTD_MAX_BLOCKS) break;
        if (blockIdx >= self->m_capacity) {
            int bi;
            int oldCap = self->m_capacity;
            xtd_ensureCapacity(self, blockIdx + 1);
            if (blockIdx >= self->m_capacity) break; /* 分配失败，截断 */
            for (bi = oldCap; bi < self->m_capacity; ++bi)
                XMemset(&self->m_blocks[bi], 0, sizeof(XTDBlock));
        }
        {
            XTDFragment* frag = &self->m_blocks[blockIdx].fragments[0];
            if (frag->text) XClassDelete(frag->text);
            frag->text = (len > 0)
                ? XString_create_with_length_utf8(p, len)
                : XString_create_utf8("");
            frag->m_fmtPoolIndex = -1; /* 未入池哨兵（块槽位 XMemset 残 0
                                          是假索引——池查询会错位取条目） */
            self->m_blocks[blockIdx].fragmentCount = 1;
        }
        blockIdx++;
        if (!nl) break;
        p = nl + 1;
    }
    if (blockIdx > self->m_blockCount) self->m_blockCount = blockIdx;
    /* 装入片段统一入池（调用方 setPlainText/undo 快照恢复均先经
     * xtd_clearBlocksOnly 清池——此处重建即全量登记）。 */
    xtd_poolRebuild(self);
}

/** @brief 快照恢复专用：仅重建块内容（不清撤销/重做栈、不发
 *         documentLayoutChanged——undo/redo 内部路径，Qt 的 undo 亦不经
 *         setPlainText 公共口）。 */
static void xtd_setPlainTextNoHistory(XTextDocument* self, const char* utf8)
{
    int oldChars;
    if (!self) return;
    oldChars = XTextDocument_characterCount(self);
    xtd_clearBlocksOnly(self);
    if (utf8 && utf8[0]) xtd_fillPlainTextBlocks(self, utf8);
    /* 快照恢复整体重装：removed = 恢复前字符数、added = 恢复后字符数
     * （setPlainText「清空 + 装入」两段等效的精确合并区间）。 */
    xtd_setPreciseChange(self, 0, oldChars,
                         XTextDocument_characterCount(self));
    xtd_changed(self);
}

void XTextDocument_setPlainText(XTextDocument* self, const char* utf8)
{
    int newChars;
    int oldBlocks;
    if (!self) return;
    /* Qt 的 setPlainText = 清空 + 装入（同一编辑块）⇒ blockCountChanged
     * 只沿「替换前 → 替换后」发一次（本机探针：2 块被 "q" 替换 ⇒ 只有
     * 末尾的 blockCountChanged(1)）。clear() 会把去重基准同步成空文档的
     * 1 块，这里先存替换前块数、装入后回填基准，末次漏斗比较即得 Qt 口径。 */
    oldBlocks = self->m_blockCount;
    /* 对标 QTextDocument::setPlainText：撤销/重做历史随 clear 复位
     * （Qt 文档注记 "The undo/redo history is reset"）。 */
    XTextDocument_clear(self);
    xtd_emitVoid(self, (size_t)XTextDocument_documentLayoutChanged_signal);
    xtd_emitVoid(self, (size_t)XTextDocument_undoCommandAdded_signal);
    if (!utf8 || !utf8[0]) return;
    xtd_fillPlainTextBlocks(self, utf8);
    /* Qt 的 setPlainText = clear 后 `QTextCursor(this).insertText(text)`：
     * 装入段以 (0, 0, 新 characterCount) 发射（本机探针 setPlainText("q")
     * 覆盖旧内容 ⇒ (0,6,0) + (0,0,2)；四块文本 ⇒ (0,0,8)）。 */
    newChars = XTextDocument_characterCount(self);
    xtd_setPreciseChange(self, 0, 0, newChars);
    self->m_emittedBlockCount = oldBlocks;
    xtd_changed(self);
}

char* XTextDocument_toRawText(const XTextDocument* self)
{
    /* 对标 toRawText：子集内与 toPlainText 等价（NBSP 已在解析期折叠、
     * 无分隔符概念），见头文件注。 */
    return XTextDocument_toPlainText(self);
}


/* ==================== HTML 解析（富文本渲染子集） ==================== */

/* 内联标签嵌套上限两层：栈深 3（如 <b><i><u> 三层格式同时生效），更深
 * 层的开启标签整体忽略，其闭合标签按溢出计数平衡丢弃（任务边界）。 */
#define XTD_INLINE_STACK 3

/* 嵌套列表深度上限（§8.0g5）：超深 li 钳制在本层渲染（有界简化）。 */
#define XTD_LIST_MAX_DEPTH 4

/** @brief 列表上下文（嵌套栈元素：该层的 QTextListFormat 子集）。
 *  @details `type`/`start` 属性照 Qt QTextHtmlParser::applyAttributes 的
 *           Html_ul/Html_ol 分支落栈（qtexthtmlparser.cpp:1720-1747）：
 *           `type` 对 ul/ol 同一套映射（1/a/A/i/I 与 square/disc/circle/
 *           none），`start` 为列表起始序号。缺省：ul=Disc、ol=Decimal
 *           （qtexthtmlparser.cpp:2171-2177 的缺省 CSS list-style）。 */
typedef struct XTDListCtx
{
    bool ordered;
    int style;    /**< XTDListStyle（ul 缺省 Disc / ol 缺省 Decimal）。 */
    int start;    /**< 列表起始序号（缺省 1）。 */
    char suffix;  /**< 序号后缀（HTML 无此属性 ⇒ 恒 '.'，Qt 缺省）。 */
} XTDListCtx;

/** @brief HTML <font size> 1-7 标准磅值表（对标 Qt QTextHtmlParser 的
 *         字号梯度近似：8/10/12/14/18/24/36 磅）。 */
static const int xtd_fontSizeTable[7] = { 8, 10, 12, 14, 18, 24, 36 };

/** @brief 大小写不敏感匹配 p 处的标签/属性名（名称边界：后随字符须非
 *         字母数字，防止 "br" 被 "b" 误配；对标 HTML 标签大小写不敏感）。 */
static bool xtd_nameAt(const char* p, const char* name)
{
    while (*name) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != *name) return false;
        ++p;
        ++name;
    }
    if (((*p >= 'a') && (*p <= 'z')) || ((*p >= 'A') && (*p <= 'Z')) ||
        ((*p >= '0') && (*p <= '9')))
        return false;
    return true;
}

/** @brief 定位标签结束 '>'（引号内的 '>' 不算；对标属性值含 '>' 场景）。 */
static const char* xtd_tagEnd(const char* p)
{
    char quote = 0;
    while (*p) {
        if (quote) {
            if (*p == quote) quote = 0;
        } else if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '>') {
            return p;
        }
        ++p;
    }
    return p;
}

/** @brief 在标签体内读取属性值（双引号/单引号/无引号；属性值不做实体
 *         展开——子集边界）。 */
static bool xtd_attrValue(const char* begin, const char* end,
                          const char* name, char* out, size_t cap)
{
    size_t nlen = XStrlen(name);
    const char* q;
    if (cap == 0 || !begin || !end || nlen == 0) return false;
    out[0] = '\0';
    q = begin;
    while (q < end) {
        /* 对齐到属性名起点（跳过空白）。 */
        while (q < end && (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n'))
            ++q;
        if (q >= end) break;
        if ((size_t)(end - q) > nlen && XStrncmp(q, name, nlen) == 0) {
            const char* v = q + nlen;
            bool hit = false;
            if (*v == '=') {
                hit = true;
            } else if (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n') {
                while (v < end && (*v == ' ' || *v == '\t' ||
                                   *v == '\r' || *v == '\n'))
                    ++v;
                hit = (v < end && *v == '=');
            }
            if (hit && v < end) {
                char quote = 0;
                const char* s;
                size_t n;
                ++v;
                while (v < end && (*v == ' ' || *v == '\t')) ++v;
                if (v < end && (*v == '"' || *v == '\'')) quote = *v++;
                s = v;
                while (v < end) {
                    if (quote) {
                        if (*v == quote) break;
                    } else if (*v == ' ' || *v == '\t' ||
                               *v == '\r' || *v == '\n') {
                        break;
                    }
                    ++v;
                }
                n = (size_t)(v - s);
                if (n >= cap) n = cap - 1;
                if (n > 0) XMemcpy(out, s, n);
                out[n] = '\0';
                return true;
            }
        }
        /* 跳过当前 token。 */
        while (q < end && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n')
            ++q;
    }
    return false;
}

/** @brief style 属性串中取指定 CSS 属性值（分号分隔 name:value 对，
 *         名比较大小写不敏感；值截到分号/空白；§8.0g5 span 子集）。 */
static bool xtd_styleValue(const char* style, const char* prop,
                           char* out, int outCap)
{
    int plen;
    const char* p;
    if (!style || !prop || !out || outCap <= 0) return false;
    plen = (int)XStrlen(prop);
    p = style;
    while (*p) {
        const char* q;
        while (*p == ' ' || *p == '\t' || *p == ';') ++p;
        if (!*p) break;
        q = p;
        while (*q && *q != ':') ++q;
        if (*q != ':') break;
        if ((int)(q - p) == plen) {
            bool match = true;
            int i;
            for (i = 0; i < plen; ++i) {
                char c = p[i], d = prop[i];
                if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
                if (d >= 'A' && d <= 'Z') d = (char)(d - 'A' + 'a');
                if (c != d) { match = false; break; }
            }
            if (match) {
                const char* v = q + 1;
                const char* e;
                int n;
                while (*v == ' ' || *v == '\t') ++v;
                e = v;
                while (*e && *e != ';' && *e != ' ' && *e != '\t') ++e;
                n = (int)(e - v);
                if (n > 0) {
                    if (n >= outCap) n = outCap - 1;
                    XMemcpy(out, v, (size_t)n);
                    out[n] = '\0';
                    return true;
                }
            }
        }
        p = (*q) ? q + 1 : q;
    }
    return false;
}

/** @brief 大小写不敏感字符串比较（HTML 属性值用；对标 HTML 大小写
 *         不敏感语义）。 */
static bool xtd_ieqStr(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/** @brief 颜色字面量解析：#RGB/#RRGGBB 与常用命名色（对标 Qt 颜色名
 *         解析的常量子集；解析失败返回 false 保持默认色）。 */
static bool xtd_parseColor(const char* s, uint32_t* out)
{
    static const struct XTDNamedColor
    {
        const char* name;
        uint32_t argb;
    } named[] = {
        { "black",   0xFF000000u }, { "white",   0xFFFFFFFFu },
        { "red",     0xFFFF0000u }, { "green",   0xFF008000u },
        { "blue",    0xFF0000FFu }, { "yellow",  0xFFFFFF00u },
        { "cyan",    0xFF00FFFFu }, { "magenta", 0xFFFF00FFu },
        { "gray",    0xFF808080u }, { "grey",    0xFF808080u },
        { "orange",  0xFFFFA500u }, { "purple",  0xFF800080u }
    };
    int i;
    if (!s || !s[0] || !out) return false;
    for (i = 0; i < (int)(sizeof(named) / sizeof(named[0])); ++i) {
        if (xtd_ieqStr(s, named[i].name)) {
            *out = named[i].argb;
            return true;
        }
    }
    if (*s == '#') {
        uint32_t r = 0, g = 0, b = 0;
        const char* hx = s + 1;
        int n = 0;
        while (n < 6 && hx[n]) {
            int d;
            char c = hx[n];
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            if (n < 2) r = r * 16 + (uint32_t)d;
            else if (n < 4) g = g * 16 + (uint32_t)d;
            else b = b * 16 + (uint32_t)d;
            ++n;
        }
        if (n == 6) {
            *out = 0xFF000000u | (r << 16) | (g << 8) | b;
            return true;
        }
        if (n == 3) {
            /* #RGB 短格式：半字节展开（对标 HTML 颜色简写）。 */
            r = (r << 4) | r;
            g = (g << 4) | g;
            b = (b << 4) | b;
            *out = 0xFF000000u | (r << 16) | (g << 8) | b;
            return true;
        }
    }
    return false;
}

/** @brief <font size> 值应用：1-7 查表（对标 Qt HTML 字号梯度）；带
 *         +/- 号为相对调整（以当前字号在表中的就近档位为基准）；超出
 *         1-7 的纯数字按磅值直接承载（与 toHtml 导出口径互逆）。 */
static void xtd_applyFontSize(XTDCharFormat* fmt, const char* val)
{
    const char* q = val;
    int n = 0;
    int sign = 0;
    int i;
    if (!fmt || !val || !val[0]) return;
    if (*q == '+') { sign = 1; ++q; }
    else if (*q == '-') { sign = -1; ++q; }
    while (*q >= '0' && *q <= '9') {
        n = n * 10 + (*q - '0');
        ++q;
    }
    if (q == val || (sign != 0 && q == val + 1)) return; /* 无数字。 */
    if (sign != 0) {
        int base = 0;
        i = 0;
        for (i = 0; i < 7; ++i) {
            if (xtd_fontSizeTable[i] <= fmt->fontPointSize) base = i;
        }
        i = base + sign * n;
        if (i < 0) i = 0;
        if (i > 6) i = 6;
        fmt->fontPointSize = xtd_fontSizeTable[i];
    } else if (n >= 1 && n <= 7) {
        fmt->fontPointSize = xtd_fontSizeTable[n - 1];
    } else {
        if (n < 1) n = 1;
        if (n > 96) n = 96;
        fmt->fontPointSize = n;
    }
}

/** @brief align 属性词映射（对标 Qt::Alignment 数值承载：Left=1/
 *         Right=2/HCenter=4/Justify=8）。 */
static int xtd_parseAlign(const char* val)
{
    if (!val || !val[0]) return 0;
    if (xtd_ieqStr(val, "center") || xtd_ieqStr(val, "middle"))
        return (int)XTDAlignment_HCenter;
    if (xtd_ieqStr(val, "right")) return (int)XTDAlignment_Right;
    if (xtd_ieqStr(val, "justify")) return (int)XTDAlignment_Justify;
    return (int)XTDAlignment_Left;
}

/**
 * @brief  `<ul type>`/`<ol type>` 属性值 → XTDListStyle。
 * @details 逐值照 Qt QTextHtmlParser::applyAttributes 的 Html_ol/Html_ul
 *          分支（qtexthtmlparser.cpp:1720-1744）：先按单字符罗马/字母档
 *          精确匹配（1/a/A/i/I），再转小写匹配 square/disc/circle/none。
 *          **未匹配返回 false 且不改 style**——Qt 同款（缺分支即保持该
 *          标签的缺省 Disc/Decimal，而不是落 Undefined）。
 * @param val 属性值（UTF-8，大小写敏感——'a' 与 'A' 是两个档）。
 * @param style [out] 命中的 XTDListStyle。
 * @return 命中返回 true；未识别返回 false。
 */
static bool xtd_parseListType(const char* val, int* style)
{
    if (!val || !val[0] || !style) return false;
    if (val[1] == '\0') {
        switch (val[0]) {
        case '1': *style = (int)XTDListStyle_Decimal; return true;
        case 'a': *style = (int)XTDListStyle_LowerAlpha; return true;
        case 'A': *style = (int)XTDListStyle_UpperAlpha; return true;
        case 'i': *style = (int)XTDListStyle_LowerRoman; return true;
        case 'I': *style = (int)XTDListStyle_UpperRoman; return true;
        default: break;
        }
    }
    if (xtd_ieqStr(val, "square")) {
        *style = (int)XTDListStyle_Square;
        return true;
    }
    if (xtd_ieqStr(val, "disc")) {
        *style = (int)XTDListStyle_Disc;
        return true;
    }
    if (xtd_ieqStr(val, "circle")) {
        *style = (int)XTDListStyle_Circle;
        return true;
    }
    if (xtd_ieqStr(val, "none")) {
        *style = (int)XTDListStyle_Undefined; /* Qt ListStyleUndefined。 */
        return true;
    }
    return false;
}

/**
 * @brief  整数属性解析（对标 Qt setIntAttribute / QString::toInt 的宽松口
 *         径：允许前后空白与正负号，非数字尾部截断，全无数字回落 def）。
 * @param val 属性值串（UTF-8）。
 * @param def 无有效数字时的回落值。
 * @return 解析值。
 */
static int xtd_parseIntAttr(const char* val, int def)
{
    const char* s = val;
    int sign = 1;
    long v = 0;
    int digits = 0;
    if (!s) return def;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    if (*s == '+' || *s == '-') {
        if (*s == '-') sign = -1;
        ++s;
    }
    while (*s >= '0' && *s <= '9') {
        if (v < 214748364L) v = v * 10 + (*s - '0');
        ++s;
        ++digits;
    }
    if (digits == 0) return def;
    v *= sign;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int)v;
}

/** @brief 向当前块追加 len 字节（ASCII 时 len=1；UTF-8 多字节序列须
 *         整序列写入——XString 的 UTF-8 流转换对单字节残序列拒收）：
 *         同格式并入末片段；格式变化新建片段；末片段为图片片段时不可
 *         并入（文本新开片段——图片是原子承载）；片段槽满时一律并入
 *         末片段（含尾片段为图片的情形——格式差异与图片原子性丢弃、
 *         文本不丢——子集边界）。 */
static void xtd_appendSpan(XTextDocument* self, int blockIdx,
                           const char* s, int len,
                           const XTDCharFormat* cur)
{
    XTDBlock* blk;
    int fi;
    if (!self || !self->m_blocks || blockIdx < 0 ||
        blockIdx >= XTD_MAX_BLOCKS || len <= 0)
        return;
    xtd_ensureCapacity(self, blockIdx + 1);
    if (blockIdx >= self->m_capacity) return; /* 扩容失败：截断。 */
    if (blockIdx >= self->m_blockCount) self->m_blockCount = blockIdx + 1;
    blk = &self->m_blocks[blockIdx];
    fi = blk->fragmentCount;
    if (fi < 0) fi = 0; /* 防御边界：脏负计数钳回合法区，防负下标越界。 */
    if (fi >= XTD_MAX_FRAGMENTS_PER_BLOCK) {
        /* P0 根因修复（钳位）：旧实现池满且尾片段为图片时绕过合并分支
         * 直落新建片段分支，&blk->fragments[fi]（= fragmentCount 标量区
         * 起点）被 XMemset/formatAssign 越界写覆写，并置
         * fragmentCount=fi+1 级联越界；触发链：256 块上限致
         * advanceBlock 拒绝推进后同块继续追加。对标同文件
         * addFragment/insertImage 的池满钳位：满池不再新建片段，一律
         * 并入末片段保文本不丢（图片片段 text 本为空串，并入即顺延
         * 追加）。 */
        XTDFragment* pf =
            &blk->fragments[XTD_MAX_FRAGMENTS_PER_BLOCK - 1];
        if (!pf->text) pf->text = XString_create();
        if (pf->text) XString_append_with_length_utf8(pf->text, s, (size_t)len);
        return;
    }
    if (fi > 0 && !blk->fragments[fi - 1].image &&
        xtd_formatEqual(&blk->fragments[fi - 1].fmt, cur)) {
        XTDFragment* pf = &blk->fragments[fi - 1];
        if (!pf->text) pf->text = XString_create();
        if (pf->text) XString_append_with_length_utf8(pf->text, s, (size_t)len);
        return;
    }
    {
        /* 新建片段分支：fi < XTD_MAX_FRAGMENTS_PER_BLOCK 由上方钳位保证。 */
        XTDFragment* nf = &blk->fragments[fi];
        XMemset(nf, 0, sizeof(XTDFragment));
        nf->m_fmtPoolIndex = -1; /* 未入池哨兵（池索引 0 起） */
        xtd_formatAssign(&nf->fmt, cur);
        nf->text = XString_create();
        if (nf->text) XString_append_with_length_utf8(nf->text, s, (size_t)len);
        blk->fragmentCount = fi + 1;
        xtd_fragPoolSync(self, blockIdx, fi); /* 影子池登记 */
    }
}

/** @brief 向当前块追加一个 ASCII 字节（xtd_appendSpan 的单字节形态）。 */
static void xtd_appendByte(XTextDocument* self, int blockIdx, char ch,
                           const XTDCharFormat* cur)
{
    xtd_appendSpan(self, blockIdx, &ch, 1, cur);
}

/** @brief p 处一个 UTF-8 序列的字节长度（前导字节定长；残序列/游离
 *         续字节按 1 处理——XString 校验拒收后丢弃，与既有口径一致）；
 *         序列越过多字节尾则截到 NUL 为止。 */
static int xtd_utf8SeqLen(const char* p)
{
    unsigned char c = (unsigned char)p[0];
    int seq = 1;
    if (c >= 0xC2 && c < 0xE0) seq = 2;
    else if (c >= 0xE0 && c < 0xF0) seq = 3;
    else if (c >= 0xF0 && c < 0xF5) seq = 4;
    while (seq > 1 && p[seq - 1] == '\0') --seq;
    return seq;
}

/** @brief 提交源空白折叠产生的待定空格：块级边界（文档起始/块级标签
 *         之后）的行首空白整段剥离，其余折叠为单个空格；随后清除块
 *         起始态，后续空格按词间隔保留。对标 QTextHtmlParser 的空白
 *         处理子集（连续空白折叠 + 块界换行剥离）；<pre> 内容区的空白
 *         保真由解析主循环的 inPre 分支逐字节直载，不经过本折叠路径。 */
static void xtd_flushPendingSpace(XTextDocument* self, int blockIdx,
                                  const XTDCharFormat* cur,
                                  bool* pendingSpace, bool* atBlockStart)
{
    if (*pendingSpace) {
        if (!*atBlockStart)
            xtd_appendByte(self, blockIdx, ' ', cur);
        *pendingSpace = false;
    }
    *atBlockStart = false;
}

/** @brief 另起新块（容量钳位；容量不足时保持原块不换——子集边界）。 */
static void xtd_advanceBlock(XTextDocument* self, int* blockIdx,
                             int alignment, int headingLevel,
                             bool listItem, bool ordered)
{
    if (!self || !blockIdx || !self->m_blocks) return;
    if (*blockIdx + 1 >= XTD_MAX_BLOCKS) return;
    xtd_ensureCapacity(self, *blockIdx + 2);
    if (*blockIdx + 1 >= self->m_capacity) return; /* 扩容失败。 */
    (*blockIdx)++;
    if (*blockIdx >= self->m_blockCount) self->m_blockCount = *blockIdx + 1;
    XMemset(&self->m_blocks[*blockIdx], 0, sizeof(XTDBlock));
    self->m_blocks[*blockIdx].alignment = alignment;
    self->m_blocks[*blockIdx].headingLevel = headingLevel;
    self->m_blocks[*blockIdx].isListItem = listItem;
    self->m_blocks[*blockIdx].isOrdered = ordered;
}

/** @brief h1-h6 标签名识别：命中返回级别 1-6，否则 0。 */
static int xtd_headingName(const char* p)
{
    static const char* const names[6] = { "h1", "h2", "h3", "h4", "h5", "h6" };
    int i;
    for (i = 0; i < 6; ++i) {
        if (xtd_nameAt(p, names[i])) return i + 1;
    }
    return 0;
}

/** @brief 开启内联标签：栈未满压栈（浅拷贝：标量复制+对象指针共享）后
 *         由调用方应用变更；超出嵌套上限两层则溢出计数（标签整体忽略）。 */
static bool xtd_openInline(XTDCharFormat* cur, XTDCharFormat* stack,
                           int* depth, int* overflow)
{
    if (!cur || !stack || !depth || !overflow) return false;
    if (*depth >= XTD_INLINE_STACK) {
        (*overflow)++;
        return false;
    }
    stack[(*depth)++] = *cur;
    return true;
}

/** @brief 闭合内联标签：溢出优先消耗；否则弹栈恢复整个格式状态。
 *         <a> 新建的 href 对象在恢复点释放（片段已深拷贝，不受影响）；
 *         弹栈恢复的指针若与当前相同则不释放（别名共享，避免双 freeing）。 */
static void xtd_closeInline(XTDCharFormat* cur, XTDCharFormat* stack,
                            int* depth, int* overflow)
{
    XTDCharFormat top;
    if (!cur || !stack || !depth || !overflow) return;
    if (*overflow > 0) {
        (*overflow)--;
        return;
    }
    if (*depth <= 0) return;
    top = stack[--(*depth)];
    if (cur->anchorHref != top.anchorHref && cur->anchorHref)
        XClassDelete(cur->anchorHref);
    *cur = top;
}

/** @brief 文档起始的空首块占用（对标 Qt：文档起始的 <p>/<li> 直接使用
 *         首个空块，不产生前置空行；返回 true 表示已占用，调用方不再
 *         另起新块）。 */
static bool xtd_claimStartBlock(XTextDocument* self, int* blockIdx,
                                int alignment, int headingLevel,
                                bool listItem, bool ordered,
                                bool* startClaimed)
{
    if (!self || !self->m_blocks || !blockIdx || !startClaimed) return false;
    if (*startClaimed || *blockIdx != 0 || self->m_blockCount <= 0 ||
        self->m_capacity <= 0)
        return false;
    if (self->m_blocks[0].fragmentCount != 0 ||
        self->m_blocks[0].headingLevel != 0 || self->m_blocks[0].isListItem)
        return false;
    self->m_blocks[0].alignment = alignment;
    self->m_blocks[0].headingLevel = headingLevel;
    self->m_blocks[0].isListItem = listItem;
    self->m_blocks[0].isOrdered = ordered;
    *startClaimed = true;
    (void)blockIdx;
    return true;
}

/** @brief HTML 解析核心（setHtml 全量替换 / appendHtml 尾部追加共用）。
 * @details 渲染子集：b/strong、i/em、u、s/strike/del、sup/sub、
 *          font(color/size)、span(style background-color)、
 *          br、p/div(align)、h1-h6、ul/ol/li（嵌套分级，§8.0g5）、pre、
 *          a(href)；实体
 *          &amp;/&lt;/&gt;/&quot;/&apos;/&#39;/&nbsp;/&#NN;。嵌套上限
 *          两层（栈深 3，三层格式同时生效），更深层忽略并平衡闭合。源空白
 *          按 HTML 语义折叠（对标 QTextHtmlParser）：空格/制表/回车/
 *          换行的连续串折叠为单个空格，块级标签（p/div/h1-h6/li/br）
 *          边界的换行与缩进整段剥离（延迟提交实现：段尾/块界前未
 *          提交的空白自然丢弃）；<pre> 例外：内容区空白逐字节保真
 *          （不折叠、不剥离），字体族切为 monospace（对标
 *          QTextHtmlParser 的 Html_pre：WhiteSpacePre + monospace 字族，
 *          字体层不可解析时按默认字体回退），退出 pre 恢复折叠。 */
static void xtd_parseHtml(XTextDocument* self, const char* html,
                          bool clearFirst)
{
    const char* p;
    XTDCharFormat cur;
    XTDCharFormat stack[XTD_INLINE_STACK];
    int depth = 0;
    int overflow = 0;
    int blockIdx;
    XTDListCtx listStack[XTD_LIST_MAX_DEPTH]; /* 嵌套列表上下文栈。 */
    int listDepth = 0;
    int freshAtLevel = 0; /* 最近一次列表开标签所在层（li 序号重起标记）。 */
    bool startClaimed = false; /* 文档起始空首块已被块级标签占用。 */
    bool pendingSpace = false; /* 源空白延迟提交：连续空白折叠为单空格。 */
    bool atBlockStart = true;  /* 块级边界后（文档起始亦算）：行首空白剥离。 */
    bool inPre = false;        /* <pre> 内容区：空白逐字节保真（不折叠）。 */
    XString* preSavedFamily = NULL; /* 进入 pre 前的字体族（退出恢复用）。 */

    if (!self || !html || !self->m_blocks) return;
    XMemset(&cur, 0, sizeof(cur));
    if (clearFirst) {
        XTextDocument_clear(self);
        blockIdx = 0;
    } else {
        xtd_ensureCapacity(self, self->m_blockCount > 0 ? self->m_blockCount : 1);
        blockIdx = self->m_blockCount - 1;
        if (blockIdx < 0) blockIdx = 0;
        if (blockIdx >= self->m_capacity) return;
        /* 末块非空：另起新段追加（对标 QTextDocument::appendHtml）。 */
        if (self->m_blocks[blockIdx].fragmentCount > 0)
            xtd_advanceBlock(self, &blockIdx, 0, 0, false, false);
    }
    p = html;
    while (*p && blockIdx < XTD_MAX_BLOCKS) {
        if (*p == '<') {
            const char* tagEnd;
            ++p;
            if (*p == '/') {
                ++p;
                if (xtd_nameAt(p, "b") || xtd_nameAt(p, "strong") ||
                    xtd_nameAt(p, "i") || xtd_nameAt(p, "em") ||
                    xtd_nameAt(p, "u") || xtd_nameAt(p, "s") ||
                    xtd_nameAt(p, "strike") || xtd_nameAt(p, "del") ||
                    xtd_nameAt(p, "font") || xtd_nameAt(p, "a") ||
                    xtd_nameAt(p, "sup") || xtd_nameAt(p, "sub")) {
                    xtd_closeInline(&cur, stack, &depth, &overflow);
                } else if (xtd_nameAt(p, "ul") || xtd_nameAt(p, "ol")) {
                    /* 嵌套列表逐层弹栈（§8.0g5；越界闭合钳在 0）。 */
                    if (listDepth > 0) --listDepth;
                } else if (xtd_nameAt(p, "span")) {
                    xtd_closeInline(&cur, stack, &depth, &overflow);
                } else if (xtd_nameAt(p, "pre")) {
                    if (inPre) {
                        inPre = false;
                        /* 恢复进入 pre 前的字体族；monospace 串若仍被
                         * 内联栈条目别名共享则不释放（同 anchorHref 的
                         * 别名防双释放口径，宁漏不悬）。 */
                        if (cur.fontFamily != preSavedFamily) {
                            int si;
                            bool aliased = false;
                            for (si = 0; si < depth; ++si) {
                                if (stack[si].fontFamily == cur.fontFamily) {
                                    aliased = true;
                                    break;
                                }
                            }
                            if (!aliased && cur.fontFamily)
                                XClassDelete(cur.fontFamily);
                            cur.fontFamily = preSavedFamily;
                        }
                        preSavedFamily = NULL;
                    }
                    pendingSpace = false; /* 块界：待定空白不带入后续文本。 */
                    atBlockStart = true;  /* 退出 pre 恢复折叠，行首空白剥离。 */
                }
                tagEnd = xtd_tagEnd(p);
                p = (*tagEnd) ? tagEnd + 1 : tagEnd;
            } else if (*p == '!' || *p == '?') {
                /* 注释/处理指令：跳到 '>'（不含 --> 特判——子集边界）。 */
                tagEnd = xtd_tagEnd(p);
                p = (*tagEnd) ? tagEnd + 1 : tagEnd;
            } else {
                const char* content = p;
                tagEnd = xtd_tagEnd(p);
                if (xtd_nameAt(p, "b") || xtd_nameAt(p, "strong")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.bold = true;
                } else if (xtd_nameAt(p, "i") || xtd_nameAt(p, "em")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.italic = true;
                } else if (xtd_nameAt(p, "u")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.underline = true;
                } else if (xtd_nameAt(p, "s") || xtd_nameAt(p, "strike") ||
                           xtd_nameAt(p, "del")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.strikeOut = true;
                } else if (xtd_nameAt(p, "sup")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.superScript = true;
                } else if (xtd_nameAt(p, "sub")) {
                    if (xtd_openInline(&cur, stack, &depth, &overflow))
                        cur.subScript = true;
                } else if (xtd_nameAt(p, "font")) {
                    char buf[64];
                    if (xtd_openInline(&cur, stack, &depth, &overflow)) {
                        uint32_t color;
                        if (xtd_attrValue(content, tagEnd, "color", buf,
                                          sizeof(buf)) &&
                            xtd_parseColor(buf, &color))
                            cur.fgColor = color;
                        if (xtd_attrValue(content, tagEnd, "size", buf,
                                          sizeof(buf)))
                            xtd_applyFontSize(&cur, buf);
                    }
                } else if (xtd_nameAt(p, "a")) {
                    char buf[512];
                    if (xtd_openInline(&cur, stack, &depth, &overflow)) {
                        if (xtd_attrValue(content, tagEnd, "href", buf,
                                          sizeof(buf)) &&
                            buf[0]) {
                            /* href 对象独立新建。压栈为浅拷贝：旧 href 若
                             * 仍被内联栈条目别名共享（嵌套 <a>）则不释放
                             * （宁漏不悬，同 </pre> 字族恢复口径），弹栈
                             * 恢复时仍指向有效对象，避免双释放。 */
                            if (cur.anchorHref) {
                                int si;
                                bool aliased = false;
                                for (si = 0; si < depth; ++si) {
                                    if (stack[si].anchorHref ==
                                        cur.anchorHref) {
                                        aliased = true;
                                        break;
                                    }
                                }
                                if (!aliased)
                                    XClassDelete(cur.anchorHref);
                            }
                            cur.anchorHref = XString_create_utf8(buf);
                        }
                    }
                } else if (xtd_nameAt(p, "br")) {
                    /* br 继承当前段对齐/标题（对标 Qt：同段内软换行）。 */
                    int align = 0;
                    int heading = 0;
                    if (blockIdx < self->m_blockCount &&
                        blockIdx < self->m_capacity) {
                        align = self->m_blocks[blockIdx].alignment;
                        heading = self->m_blocks[blockIdx].headingLevel;
                    }
                    xtd_advanceBlock(self, &blockIdx, align, heading,
                                     false, false);
                    atBlockStart = true; /* 块界：剥离后续行首空白。 */
                } else if (xtd_nameAt(p, "p") || xtd_nameAt(p, "div")) {
                    char buf[32];
                    int align = 0;
                    if (xtd_attrValue(content, tagEnd, "align", buf,
                                      sizeof(buf)))
                        align = xtd_parseAlign(buf);
                    if (!(clearFirst &&
                          xtd_claimStartBlock(self, &blockIdx, align, 0,
                                              false, false, &startClaimed)))
                        xtd_advanceBlock(self, &blockIdx, align, 0,
                                         false, false);
                    atBlockStart = true; /* 块界：剥离后续行首空白。 */
                } else if (xtd_headingName(p) > 0) {
                    char buf[32];
                    int level = xtd_headingName(p);
                    int align = 0;
                    if (xtd_attrValue(content, tagEnd, "align", buf,
                                      sizeof(buf)))
                        align = xtd_parseAlign(buf);
                    if (!(clearFirst &&
                          xtd_claimStartBlock(self, &blockIdx, align, level,
                                              false, false, &startClaimed)))
                        xtd_advanceBlock(self, &blockIdx, align, level,
                                         false, false);
                    atBlockStart = true; /* 块界：剥离后续行首空白。 */
                } else if (xtd_nameAt(p, "ul")) {
                    /* 嵌套列表（§8.0g5）：上下文栈逐层下压（超深钳制
                     * 在上限层——与内联栈同口径的有界简化）。type/start
                     * 属性按 Qt QTextHtmlParser 同口径落栈。 */
                    if (listDepth < XTD_LIST_MAX_DEPTH) {
                        char buf[32];
                        listStack[listDepth].ordered = false;
                        listStack[listDepth].style = (int)XTDListStyle_Disc;
                        listStack[listDepth].start = 1;
                        listStack[listDepth].suffix = '.';
                        if (xtd_attrValue(content, tagEnd, "type", buf,
                                          sizeof(buf))) {
                            int st;
                            if (xtd_parseListType(buf, &st))
                                listStack[listDepth].style = st;
                        }
                        /* 无 type 属性 ⇒ **恒 Disc**（含嵌套层）：Qt 6.8 在
                           initNode 里给 ul/ol 注入缺省 `list-style` 声明
                           （qtexthtmlparser.cpp:2169-2180 →
                           applyCssDeclarations → setListStyle
                           :1118-1138 置 hasOwnListStyle=true）⇒
                           qtextdocumentfragment.cpp:686 的 nextListStyle
                           分支（Disc→Circle→Square 逐级推进）在启用 CSS
                           的构建里是**死代码**。本机 Qt 6.8.0 offscreen 探针
                           /tmp/b5rev_probe.cpp 实测：三层嵌套 <ul> ⇒
                           style=-1/-1/-1；<ul type="square"> 内嵌无 type
                           的 <ul> ⇒ 子层 -1。故此处不推进样式。 */
                        if (xtd_attrValue(content, tagEnd, "start", buf,
                                          sizeof(buf)))
                            listStack[listDepth].start = xtd_parseIntAttr(buf, 1);
                        ++listDepth;
                        freshAtLevel = listDepth;
                    }
                } else if (xtd_nameAt(p, "ol")) {
                    if (listDepth < XTD_LIST_MAX_DEPTH) {
                        char buf[32];
                        listStack[listDepth].ordered = true;
                        listStack[listDepth].style = (int)XTDListStyle_Decimal;
                        listStack[listDepth].start = 1;
                        listStack[listDepth].suffix = '.';
                        if (xtd_attrValue(content, tagEnd, "type", buf,
                                          sizeof(buf))) {
                            int st;
                            if (xtd_parseListType(buf, &st))
                                listStack[listDepth].style = st;
                        }
                        if (xtd_attrValue(content, tagEnd, "start", buf,
                                          sizeof(buf)))
                            listStack[listDepth].start = xtd_parseIntAttr(buf, 1);
                        ++listDepth;
                        freshAtLevel = listDepth;
                    }
                } else if (xtd_nameAt(p, "li")) {
                    bool liOrdered = listDepth > 0 &&
                                     listStack[listDepth - 1].ordered;
                    if (!(clearFirst &&
                          xtd_claimStartBlock(self, &blockIdx, 0, 0,
                                              listDepth > 0, liOrdered,
                                              &startClaimed)))
                        xtd_advanceBlock(self, &blockIdx, 0, 0,
                                         listDepth > 0, liOrdered);
                    if (listDepth > 0 && blockIdx < self->m_capacity) {
                        const XTDListCtx* lc = &listStack[listDepth - 1];
                        self->m_blocks[blockIdx].indentLevel = listDepth;
                        /* 列表格式三属性 + 「本层列表首个 li」序号重起位
                         * （渲染侧分级计数用）——与 Markdown 导入共用同一
                         * 写层 xtd_blockApplyListFormat（不双份）；本路径是
                         * setHtml 批量导入，不逐块压快照/发信号（Qt 用单个
                         * 编辑块收口，末尾统一 xtd_changed 同口径）。 */
                        xtd_blockApplyListFormat(
                            &self->m_blocks[blockIdx], lc->style, lc->start,
                            lc->suffix, freshAtLevel == listDepth ? true
                                                                  : false);
                    }
                    freshAtLevel = 0;
                    atBlockStart = true; /* 块界：剥离后续行首空白。 */
                } else if (xtd_nameAt(p, "span")) {
                    /* span 子集（§8.0g5）：仅解析 style 的
                     * background-color 片段背景；其余样式静默。恒入内联
                     * 栈保持闭合配对平衡。 */
                    char sbuf[128];
                    char cbuf[64];
                    if (xtd_openInline(&cur, stack, &depth, &overflow)) {
                        if (xtd_attrValue(content, tagEnd, "style", sbuf,
                                          sizeof(sbuf)) &&
                            xtd_styleValue(sbuf, "background-color", cbuf,
                                           sizeof(cbuf))) {
                            uint32_t bg;
                            if (xtd_parseColor(cbuf, &bg))
                                cur.bgColor = bg;
                        }
                    }
                } else if (xtd_nameAt(p, "pre")) {
                    /* pre 为块级元素（对标 Qt：Html_pre 建块）；开标签前
                     * 的待定空白按块界丢弃。 */
                    if (!(clearFirst &&
                          xtd_claimStartBlock(self, &blockIdx, 0, 0,
                                              false, false, &startClaimed)))
                        xtd_advanceBlock(self, &blockIdx, 0, 0,
                                         false, false);
                    pendingSpace = false;
                    atBlockStart = true;
                    if (!inPre) {
                        /* 字体族切为 monospace（对标 Qt Html_pre；字体层
                         * 解析不到时按默认字体回退）；原字体族留存退出
                         * 恢复。串创建失败则保持原字体（仅失等宽）。 */
                        preSavedFamily = cur.fontFamily;
                        cur.fontFamily = XString_create_utf8("monospace");
                        inPre = true;
                    }
                }
                /* 其余标签（span/img/table/hr 等）整体跳过——子集边界。 */
                p = (*tagEnd) ? tagEnd + 1 : tagEnd;
            }
        } else if (*p == '&') {
            /* 实体产物均为可见字符：先提交待定空格（词间隔语义；
             * 与实体展开、嵌套钳制互不影响）。 */
            xtd_flushPendingSpace(self, blockIdx, &cur,
                                  &pendingSpace, &atBlockStart);
            if (XStrncmp(p, "&amp;", 5) == 0) {
                xtd_appendByte(self, blockIdx, '&', &cur);
                p += 5;
            } else if (XStrncmp(p, "&lt;", 4) == 0) {
                xtd_appendByte(self, blockIdx, '<', &cur);
                p += 4;
            } else if (XStrncmp(p, "&gt;", 4) == 0) {
                xtd_appendByte(self, blockIdx, '>', &cur);
                p += 4;
            } else if (XStrncmp(p, "&quot;", 6) == 0) {
                xtd_appendByte(self, blockIdx, '"', &cur);
                p += 6;
            } else if (XStrncmp(p, "&apos;", 6) == 0) {
                xtd_appendByte(self, blockIdx, '\'', &cur);
                p += 6;
            } else if (XStrncmp(p, "&#39;", 5) == 0) {
                xtd_appendByte(self, blockIdx, '\'', &cur);
                p += 5;
            } else if (XStrncmp(p, "&nbsp;", 6) == 0) {
                /* nbsp 折叠为普通空格（对标 Qt 富文本行为）。 */
                xtd_appendByte(self, blockIdx, ' ', &cur);
                p += 6;
            } else if (p[1] == '#') {
                /* 数字字符引用 &#NN;：十进制 ASCII 范围直载（多字节
                 * 码点合成不做——子集边界）。 */
                int code = 0;
                int digits = 0;
                const char* q = p + 2;
                while (*q >= '0' && *q <= '9') {
                    code = code * 10 + (*q - '0');
                    ++digits;
                    ++q;
                    if (code > 0x10FFFF) break;
                }
                if (digits > 0 && *q == ';' && code > 0 && code < 128) {
                    xtd_appendByte(self, blockIdx, (char)code, &cur);
                    p = q + 1;
                } else {
                    xtd_appendByte(self, blockIdx, '&', &cur);
                    ++p;
                }
            } else {
                /* 未知实体：'&' 按字面量透传（后续字符正常解析）。 */
                xtd_appendByte(self, blockIdx, *p, &cur);
                ++p;
            }
        } else if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            if (inPre) {
                /* pre 内容区例外（对标 QTextHtmlParser 的 WhiteSpacePre）：
                 * 空白逐字节保真——不折叠、不剥离，缩进/换行原样承载
                 * （空白均为单字节 ASCII，直接整段承载）。 */
                xtd_appendSpan(self, blockIdx, p, 1, &cur);
                ++p;
            } else {
                /* 源空白折叠（对标 QTextHtmlParser 空白处理）：空格/制表/
                 * 回车/换行的连续串延迟合并为单个空格，待下一可见字符
                 * 提交——块界后剥离（atBlockStart）、段尾未提交自然丢弃；
                 * 行内标签两侧的单空格词间隔语义保留（不跨标签丢弃）。 */
                pendingSpace = true;
                ++p;
            }
        } else {
            xtd_flushPendingSpace(self, blockIdx, &cur,
                                  &pendingSpace, &atBlockStart);
            if (inPre) {
                /* pre 内容区：UTF-8 序列整段保真承载（多字节字符按序列
                 * 推进；等价逐字节不折叠——残序列单字节会被 XString 的
                 * UTF-8 流转换拒收，见 xtd_appendSpan 注）。 */
                int seq = xtd_utf8SeqLen(p);
                xtd_appendSpan(self, blockIdx, p, seq, &cur);
                p += seq;
            } else {
                /* 可见文本与 pre 同口径：UTF-8 序列整段承载。此前逐字节
                 * xtd_appendByte 会把多字节字符拆成残序列，XChar_
                 * fromUtf8Stream 拒收导致 setHtml/appendHtml 静默丢弃
                 * 全部非 ASCII 字符（P0：富文本预览只剩 ASCII 子串）。 */
                int seq = xtd_utf8SeqLen(p);
                xtd_appendSpan(self, blockIdx, p, seq, &cur);
                p += seq;
            }
        }
    }
    /* 文末仍未闭合的 <pre> 按自动闭合处理：恢复进入 pre 前的字体族，
     * 并释放 monospace 串（别名防双释放口径与 </pre> 分支一致：仍被
     * 内联栈条目共享则不释放，宁漏不悬）。 */
    if (inPre) {
        if (cur.fontFamily != preSavedFamily) {
            int si;
            bool aliased = false;
            for (si = 0; si < depth; ++si) {
                if (stack[si].fontFamily == cur.fontFamily) {
                    aliased = true;
                    break;
                }
            }
            if (!aliased && cur.fontFamily)
                XClassDelete(cur.fontFamily);
            cur.fontFamily = preSavedFamily;
        }
        preSavedFamily = NULL;
    }
    /* 收尾：释放解析器仍持有的 href 对象（片段已深拷贝，不受影响）。 */
    if (cur.anchorHref) XClassDelete(cur.anchorHref);
    /* 块数钳位：保证 m_blockCount 不越过容量（此前的 <br> 连发路径存在
     * 越界读隐患，见 setPlainText 容量补零修复的同族问题）。 */
    if (self->m_blockCount < 1) self->m_blockCount = 1;
    if (self->m_blockCount > self->m_capacity)
        self->m_blockCount = self->m_capacity;
    xtd_changed(self);
}

/** @brief 解析 HTML 到块+片段结构（全量替换；对标 QTextDocument::setHtml
 *         的子集：b/i/u/s、font color/size、br、p align、a href；源空白
 *         按 HTML 语义折叠——连续空白并作单空格、块界换行剥离）。 */
void XTextDocument_setHtml(XTextDocument* self, const char* html)
{
    xtd_parseHtml(self, html, true);
}

/* ==================== HTML 生成 ==================== */

/** @brief 追加格式化片段到生成缓冲（snprintf 语义；写入越界时钳位，
 *         避免 o 越过容量造成 size_t 下溢）。 */
static void xtd_hPut(char* out, size_t cap, size_t* o, const char* fmt, ...)
{
    va_list ap;
    int n;
    if (!out || !o || cap < 2 || *o + 1 >= cap) return;
    va_start(ap, fmt);
    n = vsnprintf(out + *o, cap - *o, fmt, ap);
    va_end(ap);
    if (n > 0) *o += (size_t)n;
    if (*o >= cap) *o = cap - 1;
}

/** @brief 文本转义写入（& < > 与可选引号；对标 toHtml 实体转义）。 */
static void xtd_hEscape(char* out, size_t cap, size_t* o, const char* s,
                        bool escapeQuote)
{
    for (; s && *s; ++s) {
        switch (*s) {
        case '<':
            xtd_hPut(out, cap, o, "&lt;");
            break;
        case '>':
            xtd_hPut(out, cap, o, "&gt;");
            break;
        case '&':
            xtd_hPut(out, cap, o, "&amp;");
            break;
        case '"':
            if (escapeQuote) xtd_hPut(out, cap, o, "&quot;");
            else xtd_hPut(out, cap, o, "%c", *s);
            break;
        default:
            xtd_hPut(out, cap, o, "%c", *s);
            break;
        }
    }
}

char* XTextDocument_toHtml(const XTextDocument* self)
{
    int i, j;
    size_t cap = 256, o = 0;
    char* out;
    if (!self) return NULL;
    for (i = 0; i < self->m_blockCount; ++i) {
        cap += 96; /* 块级标签 + 对齐属性余量。 */
        for (j = 0; j < self->m_blocks[i].fragmentCount; ++j)
            cap += XStrlen(xtd_fragText(&self->m_blocks[i].fragments[j])) * 6
                   + 160; /* 内联标签 + 转义膨胀余量。 */
    }
    out = (char*)XMalloc_System(cap);
    if (!out) return NULL;
    o = (size_t)XSnprintf(out, cap, "<html><body>");
    for (i = 0; i < self->m_blockCount; ++i) {
        const XTDBlock* blk = &self->m_blocks[i];
        /* 块级开标签(先写名称与属性,末尾统一补 '>'): */
        if (blk->headingLevel >= 1 && blk->headingLevel <= 6)
            xtd_hPut(out, cap, &o, "<h%d", blk->headingLevel);
        else
            xtd_hPut(out, cap, &o, "<p");
        /* 对齐属性（与 xtd_parseAlign 映射互逆；left 为默认不写出）。 */
        if (blk->alignment & (int)XTDAlignment_HCenter)
            xtd_hPut(out, cap, &o, " align=\"center\"");
        else if (blk->alignment & (int)XTDAlignment_Right)
            xtd_hPut(out, cap, &o, " align=\"right\"");
        else if (blk->alignment & (int)XTDAlignment_Justify)
            xtd_hPut(out, cap, &o, " align=\"justify\"");
        xtd_hPut(out, cap, &o, ">");
        if (blk->isListItem) xtd_hPut(out, cap, &o, "<li>");
        for (j = 0; j < blk->fragmentCount; ++j) {
            const XTDFragment* f = &blk->fragments[j];
            const char* href = (f->fmt.anchorHref)
                                   ? XString_toUtf8(f->fmt.anchorHref)
                                   : NULL;
            if (f->image) {
                /* 图片片段：仅按原尺寸导出 <img>（无 src——按名取图的
                 * 资源体系未建，回灌降级为空，见 insertImage 注释）。 */
                xtd_hPut(out, cap, &o, "<img width=\"%d\" height=\"%d\">",
                         XImage_width(f->image), XImage_height(f->image));
                continue;
            }
            if (f->fmt.bgColor != 0u)
                xtd_hPut(out, cap, &o,
                         "<span style=\"background-color:#%06x\">",
                         (unsigned)(f->fmt.bgColor & 0xFFFFFFu));
            if (f->fmt.bold) xtd_hPut(out, cap, &o, "<b>");
            if (f->fmt.italic) xtd_hPut(out, cap, &o, "<i>");
            if (f->fmt.underline) xtd_hPut(out, cap, &o, "<u>");
            if (f->fmt.strikeOut) xtd_hPut(out, cap, &o, "<s>");
            if (f->fmt.superScript) xtd_hPut(out, cap, &o, "<sup>");
            if (f->fmt.subScript) xtd_hPut(out, cap, &o, "<sub>");
            if (f->fmt.fgColor != 0u && f->fmt.fgColor != 0xFF000000u)
                xtd_hPut(out, cap, &o, "<font color=\"#%06x\"",
                         (unsigned)(f->fmt.fgColor & 0xFFFFFFu));
            if (f->fmt.fontPointSize > 0) {
                /* 字号以磅直写（解析侧 1-7 之外按磅承载，互逆）。 */
                if (f->fmt.fgColor != 0u && f->fmt.fgColor != 0xFF000000u)
                    xtd_hPut(out, cap, &o, " size=\"%d\"",
                             f->fmt.fontPointSize);
                else
                    xtd_hPut(out, cap, &o, "<font size=\"%d\">",
                             f->fmt.fontPointSize);
            }
            if (f->fmt.fgColor != 0u && f->fmt.fgColor != 0xFF000000u)
                xtd_hPut(out, cap, &o, ">");
            if (href && href[0]) {
                xtd_hPut(out, cap, &o, "<a href=\"");
                xtd_hEscape(out, cap, &o, href, true);
                xtd_hPut(out, cap, &o, "\">");
            }
            xtd_hEscape(out, cap, &o, xtd_fragText(f), false);
            if (href && href[0]) xtd_hPut(out, cap, &o, "</a>");
            if ((f->fmt.fgColor != 0u && f->fmt.fgColor != 0xFF000000u) ||
                f->fmt.fontPointSize > 0)
                xtd_hPut(out, cap, &o, "</font>");
            if (f->fmt.strikeOut) xtd_hPut(out, cap, &o, "</s>");
            if (f->fmt.subScript) xtd_hPut(out, cap, &o, "</sub>");
            if (f->fmt.superScript) xtd_hPut(out, cap, &o, "</sup>");
            if (f->fmt.underline) xtd_hPut(out, cap, &o, "</u>");
            if (f->fmt.italic) xtd_hPut(out, cap, &o, "</i>");
            if (f->fmt.bold) xtd_hPut(out, cap, &o, "</b>");
            if (f->fmt.bgColor != 0u) xtd_hPut(out, cap, &o, "</span>");
        }
        if (blk->isListItem) xtd_hPut(out, cap, &o, "</li>");
        if (blk->headingLevel >= 1 && blk->headingLevel <= 6)
            xtd_hPut(out, cap, &o, "</h%d>", blk->headingLevel);
        else
            xtd_hPut(out, cap, &o, "</p>");
    }
    xtd_hPut(out, cap, &o, "</body></html>");
    out[o < cap ? o : cap - 1] = '\0';
    return out;
}

/* ==================== 块级格式 ==================== */

void XTextDocument_setBlockAlignment(XTextDocument* self, int blockIndex, int alignment)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    self->m_blocks[blockIndex].alignment = alignment;
    xtd_changed(self);
}

int XTextDocument_blockAlignment(const XTextDocument* self, int blockIndex)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return 0;
    return self->m_blocks[blockIndex].alignment;
}

void XTextDocument_setBlockHeadingLevel(XTextDocument* self, int blockIndex, int level)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    self->m_blocks[blockIndex].headingLevel = level;
    xtd_changed(self);
}

/* ==================== 片段操作 ==================== */

/** @brief 块 bi 的文本起点（文档字符位置；每块各占 1 个块分隔符）。
 *  @details = Σ_{i<bi}(块文本字节 + 1)，与 characterCount 的「每块 +1」
 *           口径同源（对标 Qt 的 QTextCursor 位置口径）。越界块（bi ≥ 块数）
 *           返回文档尾 = Σ 全部块 = characterCount（调用方均先校验范围）。
 */
static int xtd_blockTextStart(const XTextDocument* self, int blockIndex)
{
    int i;
    int pos = 0;
    if (!self || !self->m_blocks || blockIndex <= 0) return 0;
    for (i = 0; i < blockIndex && i < self->m_blockCount; ++i)
        pos += xtd_blockCharCount(&self->m_blocks[i]) + 1;
    return pos;
}

int XTextDocument_addFragment(XTextDocument* self, int blockIndex,
                              const char* text, const XTDCharFormat* fmt)
{
    XTDBlock* blk;
    int fi;
    int insertAt;
    int addedLen;
    if (!self || !text || blockIndex < 0 || blockIndex >= self->m_blockCount) return -1;
    blk = &self->m_blocks[blockIndex];
    fi = blk->fragmentCount;
    if (fi >= XTD_MAX_FRAGMENTS_PER_BLOCK) return -1;
    /* 精确区间先取样：插入点是该块**文本尾**（不含块分隔符），
     * added 为插入文本的字节长（Qt 探针："ab|" 尾部 insertText("cd")
     * ⇒ contentsChange(3,0,2)）。 */
    insertAt = xtd_blockTextStart(self, blockIndex) + xtd_blockCharCount(blk);
    addedLen = (int)XStrlen(text);
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈（快照制撤销接线）。 */
    XMemset(&blk->fragments[fi], 0, sizeof(XTDFragment));
    blk->fragments[fi].m_fmtPoolIndex = -1; /* 未入池哨兵（池索引 0 起） */
    blk->fragments[fi].text = XString_create_utf8(text);
    if (fmt) xtd_formatAssign(&blk->fragments[fi].fmt, fmt);
    blk->fragmentCount = fi + 1;
    xtd_fragPoolSync(self, blockIndex, fi); /* 影子池登记（等价去重） */
    xtd_setPreciseChange(self, insertAt, 0, addedLen);
    xtd_changed(self);
    return fi;
}

int XTextDocument_fragmentCount(const XTextDocument* self, int blockIndex)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return 0;
    return self->m_blocks[blockIndex].fragmentCount;
}

const XTDFragment* XTextDocument_fragment(const XTextDocument* self, int blockIndex, int fragIndex)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return NULL;
    if (fragIndex < 0 || fragIndex >= self->m_blocks[blockIndex].fragmentCount) return NULL;
    return &self->m_blocks[blockIndex].fragments[fragIndex];
}

/* ==================== 文本插入 ==================== */

/* 【冗余 API 清理 2026-11-11】insertText 已删除（仅转调 addFragment，
 * fragIndex/charOffset 从未生效；Qt 6.8 QTextDocument 无此入口）。 */

void XTextDocument_appendBlock(XTextDocument* self, const XTDCharFormat* fmt)
{
    int fromPos;
    bool appended = false;
    if (!self) return;
    /* 精确区间：在文档尾插入一个块分隔符 ⇒ (旧 characterCount - 1, 0, 1)
     * （Qt 探针："ab" 后 insertBlock ⇒ contentsChange(2,0,1)）。 */
    fromPos = XTextDocument_characterCount(self) - 1;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    xtd_ensureCapacity(self, self->m_blockCount + 1);
    if (self->m_blockCount < XTD_MAX_BLOCKS) {
        XMemset(&self->m_blocks[self->m_blockCount], 0, sizeof(XTDBlock));
        self->m_blockCount++;
        appended = true;
    }
    (void)fmt;
    if (appended) xtd_setPreciseChange(self, fromPos, 0, 1);
    /* 块数变化经统一出口发射 blockCountChanged（Qt 信号语义）。 */
    xtd_changed(self);
}

void XTextDocument_appendText(XTextDocument* self, const char* text,
                              const XTDCharFormat* fmt)
{
    XTextDocument_addFragment(self, self->m_blockCount - 1, text, fmt);
}

void XTextDocument_appendHtml(XTextDocument* self, const char* html)
{
    /* 对标 QTextDocument::appendHtml：以既有解析器在文档尾部追加，
     * 不清空既有块（末块非空时另起新段）；子集口径与 setHtml 一致。 */
    if (!self) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈（快照制撤销接线）。 */
    xtd_parseHtml(self, html, false);
}

/* ==================== 图片片段（最小子集） ==================== */

int XTextDocument_insertImage(XTextDocument* self, const XImage* image)
{
    XTDBlock* blk;
    XTDFragment* frag;
    XRect full;
    int bi;
    int fi;
    if (!self || !self->m_blocks || !image) return -1;
    if (XImage_width(image) <= 0 || XImage_height(image) <= 0) return -1;
    bi = self->m_blockCount - 1;
    if (bi < 0) bi = 0;
    xtd_ensureCapacity(self, bi + 1);
    if (bi >= self->m_capacity) return -1; /* 扩容失败：截断。 */
    if (bi >= self->m_blockCount) self->m_blockCount = bi + 1;
    blk = &self->m_blocks[bi];
    fi = blk->fragmentCount;
    if (fi >= XTD_MAX_FRAGMENTS_PER_BLOCK) return -1;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈（快照制撤销接线）。 */
    frag = &blk->fragments[fi];
    XMemset(frag, 0, sizeof(*frag));
    frag->m_fmtPoolIndex = -1; /* 未入池哨兵（池索引 0 起） */
    /* 图片片段：text 恒为空串（toPlainText/编辑器同步不含图片——子集
     * 边界），image 为文档持有的深拷贝（调用方保留原对象所有权）。 */
    frag->text = XString_create_utf8("");
    frag->image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!frag->text || !frag->image) {
        xtd_fragClear(frag);
        return -1;
    }
    XRect_init(&full, 0, 0, XImage_width(image), XImage_height(image));
    XImage_copyRect(image, &full, frag->image);
    if (XImage_isNull(frag->image)) {
        xtd_fragClear(frag);
        return -1;
    }
    blk->fragmentCount = fi + 1;
    xtd_fragPoolSync(self, bi, fi); /* 影子池登记（图片片段=零内联格式） */
    xtd_changed(self);
    return fi;
}

/* ==================== 元信息 ==================== */

void XTextDocument_setMetaInformation(XTextDocument* self, int info, const char* value)
{
    XString** slot = NULL;
    if (!self || !value) return;
    /* 类别数值对标 QTextDocument::MetaInformation（0-3）。 */
    if (info == (int)XTDMetaInformation_DocumentTitle) slot = &self->m_title;
    else if (info == (int)XTDMetaInformation_DocumentUrl) slot = &self->m_url;
    else if (info == (int)XTDMetaInformation_CssMedia) slot = &self->m_cssMedia;
    else if (info == (int)XTDMetaInformation_FrontMatter) slot = &self->m_frontMatter;
    if (slot) {
        if (!*slot) *slot = XString_create();
        if (*slot) XString_assign_utf8(*slot, value);
    }
}

const char* XTextDocument_metaInformation(const XTextDocument* self, int info)
{
    XString* field = NULL;
    const char* text;
    if (!self) return "";
    if (info == (int)XTDMetaInformation_DocumentTitle) field = self->m_title;
    else if (info == (int)XTDMetaInformation_DocumentUrl) field = self->m_url;
    else if (info == (int)XTDMetaInformation_CssMedia) field = self->m_cssMedia;
    else if (info == (int)XTDMetaInformation_FrontMatter) field = self->m_frontMatter;
    if (!field) return "";
    text = XString_toUtf8(field);
    return text ? text : "";
}

/* ==================== 撤销/重做 ==================== */

void XTextDocument_setUndoRedoEnabled(XTextDocument* self, bool enable)
{
    if (!self) return;
    /* 对标 QTextDocumentPrivate::enableUndoRedo：块数上限生效期间禁止
     * 重新启用；禁用时清空撤销栈与重做栈并发射不可用信号。 */
    if (enable && self->m_maximumBlockCount > 0) return;
    self->m_undoRedoEnabled = enable;
    if (!enable) xtd_clearUndoRedoArrays(self);
    xtd_notifyUndoRedo(self);
}
bool XTextDocument_isUndoRedoEnabled(const XTextDocument* self)
{ return self ? self->m_undoRedoEnabled : false; }
bool XTextDocument_isUndoAvailable(const XTextDocument* self)
{
    /* 对标 Qt：undoEnabled 且撤销栈非空（此前误用修改计数）。 */
    return self ? (self->m_undoRedoEnabled && self->m_undoTop > 0) : false;
}
bool XTextDocument_isRedoAvailable(const XTextDocument* self)
{
    /* 对标 Qt：undoEnabled 且重做栈非空（此前恒 false）。 */
    return self ? (self->m_undoRedoEnabled && self->m_redoTop > 0) : false;
}

void XTextDocument_clearUndoRedoStacks(XTextDocument* self, int stacks)
{
    bool changed = false;
    if (!self) return;
    /* 对标 QTextDocument::clearUndoRedoStacks：按 Stacks 位组合清栈；
     * 清空导致可用性变化时发射不可用信号（Qt 同语义）。 */
    if (stacks & (int)XTDStacks_UndoStack) {
        int i;
        for (i = 0; i < self->m_undoTop; ++i)
            XFree_System(self->m_undoStack[i]);
        self->m_undoTop = 0;
        changed = true;
    }
    if (stacks & (int)XTDStacks_RedoStack) {
        int i;
        for (i = 0; i < self->m_redoTop; ++i)
            XFree_System(self->m_redoStack[i]);
        self->m_redoTop = 0;
        changed = true;
    }
    if (changed) xtd_notifyUndoRedo(self);
}

int XTextDocument_availableUndoSteps(const XTextDocument* self)
{
    /* 对标 QTextDocumentPrivate::availableUndoSteps：禁用时 0。 */
    return (self && self->m_undoRedoEnabled) ? self->m_undoTop : 0;
}

int XTextDocument_availableRedoSteps(const XTextDocument* self)
{
    /* 对标 QTextDocumentPrivate::availableRedoSteps：禁用时 0。 */
    return (self && self->m_undoRedoEnabled) ? self->m_redoTop : 0;
}

/* ==================== 默认格式 ==================== */

void XTextDocument_setDefaultFormat(XTextDocument* self, const XTDCharFormat* fmt)
{
    /* 每文档独立存储（对标 Qt defaultFont 属性）；此前为全局静态，
     * 多文档互相污染（与撤销栈同族问题）。 */
    if (self && fmt) xtd_formatAssign(&self->m_defaultFormat, fmt);
}
const XTDCharFormat* XTextDocument_defaultFormat(const XTextDocument* self)
{ return self ? &self->m_defaultFormat : NULL; }

/* ==================== §B16 文本格式池公共 API ==================== */

int XTextDocument_allFormats(const XTextDocument* self,
                             const XTextFormat** out, int cap)
{
    int i;
    int n;
    if (!self || !self->m_formatPool) return 0;
    n = xtd_poolCount(self->m_formatPool);
    if (out) {
        int m = n < cap ? n : cap;
        for (i = 0; i < m; ++i) {
            out[i] = (const XTextFormat*)
                XVector_at_base(self->m_formatPool, i);
        }
    }
    return n;
}

int XTextDocument_formatPoolCount(const XTextDocument* self)
{
    return XTextDocument_allFormats(self, NULL, 0);
}

int XTextDocument_fragmentPoolIndex(const XTextDocument* self,
                                    int blockIndex, int fragIndex)
{
    const XTDFragment* f;
    if (!self || !self->m_blocks) return -1;
    if (blockIndex < 0 || blockIndex >= self->m_blockCount) return -1;
    if (fragIndex < 0 || fragIndex >= self->m_blocks[blockIndex].fragmentCount)
        return -1;
    f = &self->m_blocks[blockIndex].fragments[fragIndex];
    return (int)f->m_fmtPoolIndex;
}

/* ==================== §B16 内联格式 ⇄ 属性格式转换桥 ==================== */

void XTextDocument_charFormatToTextFormat(XTextFormat* dst,
                                          const XTDCharFormat* src)
{
    if (!dst) return;
    /* CharFormat 起步、全量重落（确定性映射：同一内联格式恒同属性格式） */
    XTextFormat_init(dst, XTextFormatType_CharFormat);
    if (!src) return;
    if (src->fontFamily) {
        const char* fam = XString_toUtf8(src->fontFamily);
        if (fam && fam[0])
            XTextFormat_setProperty_string(
                dst, XTextFormatProperty_FontFamilies, fam);
    }
    if (src->anchorHref) {
        const char* href = XString_toUtf8(src->anchorHref);
        if (href && href[0])
            XTextFormat_setProperty_string(
                dst, XTextFormatProperty_AnchorHref, href);
    }
    /* bool 组恒落表（false 也落）：内联格式的「显式普通」是确定值。
     * bold→FontWeight：QFont::Normal=400 / QFont::Bold=700。 */
    XTextFormat_setProperty_int(dst, XTextFormatProperty_FontWeight,
                                src->bold ? 700 : 400);
    XTextFormat_setProperty_bool(dst, XTextFormatProperty_FontItalic,
                                 src->italic != 0u);
    /* 下划线落兼容键 FontUnderline（0x2005）——QTextCharFormat::
     * fontUnderline（qtextformat.cpp:1638-1643）的读路径回落分支消费它。 */
    XTextFormat_setProperty_bool(dst, XTextFormatProperty_FontUnderline,
                                 src->underline != 0u);
    XTextFormat_setProperty_bool(dst, XTextFormatProperty_FontStrikeOut,
                                 src->strikeOut != 0u);
    if (src->superScript) {
        XTextFormat_setProperty_int(
            dst, XTextFormatProperty_TextVerticalAlignment,
            XTextFormatVerticalAlign_SuperScript);
    } else if (src->subScript) {
        XTextFormat_setProperty_int(
            dst, XTextFormatProperty_TextVerticalAlignment,
            XTextFormatVerticalAlign_SubScript);
    }
    /* 色 0=未落（渲染口径：fgColor!=0 才用，同 XTDFragment.fmt 语义） */
    if (src->fgColor != 0u)
        XTextFormat_setProperty_color(dst, XTextFormatProperty_ForegroundBrush,
                                      src->fgColor);
    if (src->bgColor != 0u)
        XTextFormat_setProperty_color(dst, XTextFormatProperty_BackgroundBrush,
                                      src->bgColor);
    if (src->fontPointSize > 0)
        XTextFormat_setProperty_double(dst, XTextFormatProperty_FontPointSize,
                                       src->fontPointSize);
}

void XTextDocument_textFormatToCharFormat(XTDCharFormat* dst,
                                          const XTextFormat* src)
{
    if (!dst) return;
    xtd_formatClear(dst); /* 旧字符串字段先释放（fontFamily/anchorHref 所有者） */
    XMemset(dst, 0, sizeof(*dst));
    if (!src) return;
    {
        const char* fam = XTextCharFormat_fontFamily(src);
        if (fam && fam[0]) dst->fontFamily = XString_create_utf8(fam);
    }
    {
        const char* href =
            XTextFormat_stringProperty(src, XTextFormatProperty_AnchorHref);
        if (href && href[0]) dst->anchorHref = XString_create_utf8(href);
    }
    /* 字重→bold：≥500 视为粗体（QFont::setBold 口径：weight>QFont::Normal） */
    dst->bold = (XTextCharFormat_fontWeight(src) >= 500) ? 1u : 0u;
    dst->italic = XTextCharFormat_fontItalic(src) ? 1u : 0u;
    /* 下划线：TextUnderlineStyle 优先、FontUnderline 兼容键回落——逐字对齐
     * QTextCharFormat::fontUnderline 读序（qtextformat.cpp:1638-1643）。 */
    dst->underline = XTextCharFormat_fontUnderline(src) ? 1u : 0u;
    dst->strikeOut = XTextCharFormat_fontStrikeOut(src) ? 1u : 0u;
    {
        int va = XTextCharFormat_verticalAlignment(src);
        dst->superScript =
            (va == XTextFormatVerticalAlign_SuperScript) ? 1u : 0u;
        dst->subScript =
            (va == XTextFormatVerticalAlign_SubScript) ? 1u : 0u;
    }
    dst->fgColor = XTextCharFormat_foreground(src);
    dst->bgColor = XTextCharFormat_background(src);
    {
        double ps = XTextCharFormat_fontPointSize(src);
        if (ps > 0.0) {
            int v = (int)(ps + 0.5);
            if (v < 1) v = 1;
            if (v > 96) v = 96; /* xtd_parseFontSize 同款钳制口径 */
            dst->fontPointSize = (uint16_t)v;
        }
    }
}

/* ==================== §B16 内容脏标记（markContentsDirty 族） ==================== */

void XTextDocument_markContentsDirty(XTextDocument* self, int from,
                                     int length)
{
    if (!self) return;
    if (self->m_dirtyFrom < 0) {
        /* 首次登记（qtextdocument_p.cpp:1232-1237） */
        self->m_dirtyFrom = from;
        self->m_dirtyOldLength = length;
        self->m_dirtyLength = length;
        return;
    }
    {
        /* 区间合并（qtextdocument_p.cpp:1238-1243 逐字移植） */
        int start = (from < self->m_dirtyFrom) ? from : self->m_dirtyFrom;
        int endOld = self->m_dirtyFrom + self->m_dirtyLength;
        int endNew = from + length;
        int end = (endNew > endOld) ? endNew : endOld;
        int diff = end - start - self->m_dirtyLength;
        if (diff < 0) diff = 0;
        self->m_dirtyFrom = start;
        self->m_dirtyOldLength += diff;
        self->m_dirtyLength += diff;
    }
    /* 本库无布局消费方（§B16 布局引擎长期项）：区间保留待查询，不伪造
     * Qt 的 lout->documentChanged 通知。 */
}

bool XTextDocument_isContentsDirty(const XTextDocument* self)
{
    return self && self->m_dirtyFrom >= 0;
}

int XTextDocument_contentsDirtyFrom(const XTextDocument* self)
{
    return self ? self->m_dirtyFrom : -1;
}

int XTextDocument_contentsDirtyLength(const XTextDocument* self)
{
    return self ? self->m_dirtyLength : 0;
}

void XTextDocument_resetContentsDirty(XTextDocument* self)
{
    if (!self) return;
    /* 对齐 Qt 布局器 documentChanged 回调后的复位（qtextdocument.cpp:623） */
    self->m_dirtyFrom = -1;
    self->m_dirtyOldLength = 0;
    self->m_dirtyLength = 0;
}

/* ==================== W2-a 文档级编辑（对标 QTextCursor 的文档侧承载） === */

/** @brief 撤销/重做元数据槽位清成「无元数据」（既有入口的纯文本快照口径）。 */
static void xtd_metaClear(int* from, int* removed, int* added, uint8_t* op)
{
    if (from) *from = 0;
    if (removed) *removed = 0;
    if (added) *added = 0;
    if (op) *op = (uint8_t)XTDUndoOp_None;
}

/** @brief 向撤销栈顶快照补写元数据（快照与元数据平行数组，槽位=undoTop-1；
 *         插入/删除两侧口径分挂——一步替换在 Qt 是两条命令各带口径，undo/
 *         redo 按命令序两次调整光标，见结构体注。撤销禁用/无快照时静默——
 *         undo 对 None 槽不动光标，旧行为不变）。 */
static void xtd_metaFillUndo(XTextDocument* self, int from, int removed,
                             int added, int opIns, int opRem)
{
    int top;
    if (!self || !self->m_undoRedoEnabled) return;
    top = self->m_undoTop;
    if (top <= 0 || top > XTD_MAX_UNDO_STEPS) return;
    top -= 1;
    self->m_undoMetaFrom[top] = from;
    self->m_undoMetaRemoved[top] = removed;
    self->m_undoMetaAdded[top] = added;
    self->m_undoMetaOpIns[top] = (uint8_t)opIns;
    self->m_undoMetaOpRem[top] = (uint8_t)opRem;
}

/** @brief 活动光标映射（Qt QTextCursorPrivate::adjustPosition 的
 *         qtextcursor.cpp:57-94 逐条移植，单光标版）：
 *         pos < from 不动；pos == from 且 KeepCursor 不动；删除方向
 *         (delta<0) 时 pos < from+|delta| 夹到 from，否则随 delta 平移；
 *         插入方向随 delta 平移。映射后钳位并按既有 setCursorPosition
 *         口径（同值早退 + cursorPositionChanged）落盘。 */
static void xtd_adjustActiveCursor(XTextDocument* self, int from, int delta,
                                   int op)
{
    int pos;
    int maxPos;
    if (!self || delta == 0) return;
    pos = self->m_cursorPosition;
    if (pos < from) return;
    if (pos == from && op == (int)XTDUndoOp_KeepCursor) return;
    if (delta < 0 && pos < from - delta)
        pos = from;
    else
        pos += delta;
    maxPos = XTextDocument_characterCount(self) - 1;
    if (maxPos < 0) maxPos = 0;
    if (pos > maxPos) pos = maxPos;
    if (pos < 0) pos = 0;
    XTextDocument_setCursorPosition(self, pos);
}

/**
 * @brief  文档位置 → (块号, 块内字节偏移)。
 * @details 偏移可达「块文本字节长」（= 落在本块分隔符上的块尾光标位，
 *          Qt positionInBlock == length()-1 同口径）；末块分隔符位
 *          = 文档尾（characterCount-1）。越界钳末块块尾。
 */
static void xtd_posLocate(const XTextDocument* self, int pos, int* bi,
                          int* offset)
{
    int i;
    int acc = 0;
    if (bi) *bi = 0;
    if (offset) *offset = 0;
    if (!self || !self->m_blocks || self->m_blockCount <= 0) return;
    if (pos < 0) pos = 0;
    for (i = 0; i < self->m_blockCount; ++i) {
        int n = xtd_blockCharCount(&self->m_blocks[i]);
        if (pos < acc + n + 1) {
            if (bi) *bi = i;
            if (offset) *offset = pos - acc;
            return;
        }
        acc += n + 1;
    }
    /* 越过末块分隔符位：钳末块块尾。 */
    i = self->m_blockCount - 1;
    if (bi) *bi = i;
    if (offset) *offset = xtd_blockCharCount(&self->m_blocks[i]);
}

/** @brief 块内字节偏移 → 片段下标（偏移恰在片段边界 = 后一片段下标；
 *         偏移 == 块长返回 fragmentCount；空块恒 0）。 */
static int xtd_fragAtOffset(const XTDBlock* blk, int offset)
{
    int fi;
    int acc = 0;
    if (!blk || blk->fragmentCount <= 0) return 0;
    for (fi = 0; fi < blk->fragmentCount; ++fi) {
        acc += (int)XStrlen(xtd_fragText(&blk->fragments[fi]));
        if (offset < acc) return fi;
        if (offset == acc) return fi + 1;
    }
    return blk->fragmentCount;
}

/** @brief 相邻同格式文本片段合并（图片片段与空串片段不合——图片是原子
 *         承载；对标 Qt unite 的同格式合并）。池条目不回收（Qt 空洞口径），
 *         合并后末片段重新入池。 */
static void xtd_blockCoalesce(XTextDocument* self, int bi)
{
    XTDBlock* blk;
    int fi;
    int w = 0;
    if (!self || !self->m_blocks) return;
    if (bi < 0 || bi >= self->m_blockCount) return;
    blk = &self->m_blocks[bi];
    for (fi = 0; fi < blk->fragmentCount; ++fi) {
        XTDFragment* cur = &blk->fragments[fi];
        if (w > 0) {
            XTDFragment* prev = &blk->fragments[w - 1];
            if (!prev->image && !cur->image && cur->text &&
                xtd_formatEqual(&prev->fmt, &cur->fmt)) {
                const char* t = xtd_fragText(cur);
                if (!prev->text) prev->text = XString_create();
                if (prev->text) {
                    XString_append_with_length_utf8(prev->text, t,
                                                    (size_t)XStrlen(t));
                    xtd_fragClear(cur);
                    XMemset(cur, 0, sizeof(XTDFragment));
                    continue; /* cur 已并入 prev，不前移写口 */
                }
            }
        }
        if (w != fi) blk->fragments[w] = blk->fragments[fi];
        ++w;
    }
    if (w != blk->fragmentCount) {
        /* 尾部残留槽清零（防析构/清块读到重复指针——裁剪槽位清零同口径）。 */
        XMemset(&blk->fragments[w], 0,
                sizeof(XTDFragment) * (size_t)(blk->fragmentCount - w));
        blk->fragmentCount = (uint8_t)w;
        if (w > 0) xtd_fragPoolSync(self, bi, w - 1);
    }
}

/**
 * @brief  保证块内偏移是片段边界（字节级插删的公共前置：片段横跨 offset
 *         时劈成两段）。旧的「整片段粒度」插删会把新文本插到片段头、把
 *         偏移区间内的片段整段放过——实测 "abcdef" 中缝插 "XY" 得
 *         "XYabcdef"（P0，本轮根修）。
 * @details 劈分走「先建尾、再建头、后换指针」三步，全程只读原缓冲、
 *          不原地改写（XString_assign 同缓冲读写有 realloc 悬挂风险）。
 *          片段槽满时先合并同格式邻段腾位；仍满则保持原边界（格式边界
 *          粗化保文本——有界子集，64 个互异格式片段的单块才触发）。
 */
static void xtd_blockEnsureFragBoundary(XTextDocument* self, int bi,
                                        int offset)
{
    XTDBlock* blk;
    int fi;
    int acc = 0;
    if (!self || !self->m_blocks) return;
    if (bi < 0 || bi >= self->m_blockCount) return;
    blk = &self->m_blocks[bi];
    if (offset < 0) offset = 0;
    if (offset > xtd_blockCharCount(blk)) offset = xtd_blockCharCount(blk);
    if (offset == 0) return; /* 块首恒为边界。 */
    for (fi = 0; fi < blk->fragmentCount; ++fi) {
        int flen = (int)XStrlen(xtd_fragText(&blk->fragments[fi]));
        if (offset > acc && offset < acc + flen) {
            int left = offset - acc;
            int i;
            XTDFragment* tail;
            const char* t;
            XString* head;
            if (blk->fragmentCount >= XTD_MAX_FRAGMENTS_PER_BLOCK) {
                /* 槽满：先合并同格式邻段腾位，再重扫。 */
                int before = blk->fragmentCount;
                xtd_blockCoalesce(self, bi);
                if (blk->fragmentCount == before) return; /* 仍满：粗化。 */
                fi = -1;
                acc = 0;
                continue; /* 重扫（continue 后 for 的 ++fi 回 0）。 */
            }
            /* 劈开片段 fi：[0,left) 留 fi、[left,flen) 移 fi+1。 */
            for (i = blk->fragmentCount; i > fi + 1; --i)
                blk->fragments[i] = blk->fragments[i - 1];
            tail = &blk->fragments[fi + 1];
            XMemset(tail, 0, sizeof(XTDFragment));
            tail->m_fmtPoolIndex = -1;
            xtd_formatAssign(&tail->fmt, &blk->fragments[fi].fmt);
            t = xtd_fragText(&blk->fragments[fi]);
            tail->text = XString_create_with_length_utf8(t + left,
                                                         (size_t)(flen - left));
            head = XString_create_with_length_utf8(t, (size_t)left);
            if (blk->fragments[fi].text)
                XClassDelete(blk->fragments[fi].text); /* 旧缓冲整体退役。 */
            blk->fragments[fi].text = head;
            blk->fragmentCount = (uint8_t)(blk->fragmentCount + 1);
            xtd_fragPoolSync(self, bi, fi);
            xtd_fragPoolSync(self, bi, fi + 1);
            return;
        }
        acc += flen;
        if (offset == acc) return; /* 已是边界。 */
    }
}

/**
 * @brief  块内偏移处插入一段同格式文本。
 * @details 先保证 offset 是片段边界（片段内拆分），再按格式择路：
 *          前邻同格式文本片段 → 直接并入（Qt unite 的插入合并，探针 P2
 *          「AxB 单片段」口径）；否则新建片段插入并触发一次合并扫描。
 *          片段槽满（XTD_MAX_FRAGMENTS_PER_BLOCK）时并入前邻片段保文本
 *          （格式边界丢弃，与解析器池满钳位同口径）。
 * @return 实际写入的字节数；参数非法返回 0。
 */
static int xtd_blockInsertRun(XTextDocument* self, int bi, int offset,
                              const char* utf8, int len,
                              const XTDCharFormat* fmt)
{
    XTDBlock* blk;
    int insertIdx;
    if (!self || !self->m_blocks || !utf8 || len <= 0) return 0;
    if (bi < 0 || bi >= self->m_blockCount) return 0;
    blk = &self->m_blocks[bi];
    if (offset < 0) offset = 0;
    if (offset > xtd_blockCharCount(blk)) offset = xtd_blockCharCount(blk);
    xtd_blockEnsureFragBoundary(self, bi, offset); /* 字节偏移→片段边界。 */
    insertIdx = xtd_fragAtOffset(blk, offset);
    if (insertIdx > 0) {
        XTDFragment* prev = &blk->fragments[insertIdx - 1];
        if (!prev->image && fmt && xtd_formatEqual(&prev->fmt, fmt)) {
            /* 前邻同格式：直接并入（不经新片段）。 */
            if (!prev->text) prev->text = XString_create();
            if (prev->text) {
                XString_append_with_length_utf8(prev->text, utf8, (size_t)len);
                xtd_fragPoolSync(self, bi, insertIdx - 1);
                xtd_blockCoalesce(self, bi); /* 后邻同格式段合拢（Qt unite）。 */
                return len;
            }
        }
    }
    if (blk->fragmentCount >= XTD_MAX_FRAGMENTS_PER_BLOCK) {
        /* 槽满：并入前邻（保文本，格式边界丢弃——池满钳位同口径）。 */
        XTDFragment* pf;
        int pi = (insertIdx > 0) ? insertIdx - 1 : 0;
        if (blk->fragmentCount <= 0) return 0;
        pf = &blk->fragments[pi];
        if (!pf->text) pf->text = XString_create();
        if (pf->text) {
            XString_append_with_length_utf8(pf->text, utf8, (size_t)len);
            xtd_fragPoolSync(self, bi, pi);
            return len;
        }
        return 0;
    }
    {
        int i;
        /* 片段数组腾位（值语义结构，memmove 安全；尾部腾出的空槽本就无载荷）。 */
        for (i = blk->fragmentCount; i > insertIdx; --i)
            blk->fragments[i] = blk->fragments[i - 1];
        XMemset(&blk->fragments[insertIdx], 0, sizeof(XTDFragment));
        blk->fragments[insertIdx].m_fmtPoolIndex = -1;
        blk->fragments[insertIdx].text = XString_create();
        if (blk->fragments[insertIdx].text)
            XString_append_with_length_utf8(blk->fragments[insertIdx].text,
                                            utf8, (size_t)len);
        if (fmt) xtd_formatAssign(&blk->fragments[insertIdx].fmt, fmt);
        blk->fragmentCount = (uint8_t)(blk->fragmentCount + 1);
        xtd_fragPoolSync(self, bi, insertIdx);
        xtd_blockCoalesce(self, bi);
    }
    return len;
}

/**
 * @brief  在块内偏移处插入块分隔符（原块拆成两块，新块=bi+1）。
 * @details 尾部片段 [xtd_fragAtOffset(bi, offset)..) 整体移入新块（指针
 *          所有权随结构拷贝转移，源槽清零防双释放）；新块段属性继承原块
 *          （Qt 探针 N3b：中缝 insertBlock 两块对齐一致）；CSS 类串深拷贝。
 *          块数达上限（XTD_MAX_BLOCKS / 容量）时拒绝拆分（返回 false，
 *          调用方按「分隔符丢弃」口径继续——有界子集）。
 */
static bool xtd_blockSplitAt(XTextDocument* self, int bi, int offset)
{
    XTDBlock* blk;
    XTDBlock* nb;
    int insertIdx;
    int moved;
    if (!self || !self->m_blocks) return false;
    if (bi < 0 || bi >= self->m_blockCount) return false;
    if (self->m_blockCount >= XTD_MAX_BLOCKS) return false;
    xtd_ensureCapacity(self, self->m_blockCount + 1);
    if (self->m_blockCount >= self->m_capacity) return false;
    blk = &self->m_blocks[bi];
    if (offset < 0) offset = 0;
    if (offset > xtd_blockCharCount(blk)) offset = xtd_blockCharCount(blk);
    xtd_blockEnsureFragBoundary(self, bi, offset); /* 偏移落成片段边界。 */
    insertIdx = xtd_fragAtOffset(blk, offset);
    /* 1. 先把老块 [bi+1, blockCount) 整体后移到 [bi+2, blockCount+1)
     *    （腾出新块槽；必须在填新块**之前**——否则新块会被当作源一起挪走、
     *    老块既丢载荷又泄漏，实测 P9 段）。 */
    if (bi + 1 < self->m_blockCount) {
        XMemmove(&self->m_blocks[bi + 2], &self->m_blocks[bi + 1],
                 sizeof(XTDBlock) * (size_t)(self->m_blockCount - bi - 1));
    }
    /* 2. 新块槽（此时 [bi+1, capacity) 的旧内容已让位——清零后填）。 */
    nb = &self->m_blocks[bi + 1];
    XMemset(nb, 0, sizeof(XTDBlock));
    nb->alignment = blk->alignment;
    nb->indentLevel = blk->indentLevel;
    nb->headingLevel = blk->headingLevel;
    nb->isListItem = blk->isListItem;
    nb->isOrdered = blk->isOrdered;
    nb->listFresh = blk->listFresh;
    /* 列表格式三属性随块继承（拆块后的后半段仍属同一列表项：start/suffix/
       style 必须一致，否则渲染层序号/标记跳变）。 */
    nb->listStyleCode = blk->listStyleCode;
    nb->listStartSet = blk->listStartSet;
    nb->listSuffixParen = blk->listSuffixParen;
    nb->listStart = blk->listStart;
    if (blk->blockFormat)
        nb->blockFormat = XString_create_copy(blk->blockFormat);
    moved = blk->fragmentCount - insertIdx;
    if (moved > 0) {
        XMemcpy(nb->fragments, &blk->fragments[insertIdx],
                sizeof(XTDFragment) * (size_t)moved);
        XMemset(&blk->fragments[insertIdx], 0,
                sizeof(XTDFragment) * (size_t)moved);
    }
    nb->fragmentCount = (uint8_t)moved;
    blk->fragmentCount = (uint8_t)insertIdx;
    self->m_blockCount++;
    /* 新块片段 m_fmtPoolIndex 随结构拷贝仍有效（池条目不复回收）；原块
     * 尾片段若被拆分（insertIdx 处于片段内），前半段格式未变、池索引不变。 */
    return true;
}

/** @brief 块内删除 [off1, off2)（片段边界先拆后删；池条目不回收）。 */
static void xtd_blockDeleteSpan(XTextDocument* self, int bi, int off1, int off2)
{
    XTDBlock* blk;
    int i1;
    int i2;
    int del;
    int i;
    if (!self || !self->m_blocks) return;
    if (bi < 0 || bi >= self->m_blockCount) return;
    blk = &self->m_blocks[bi];
    if (off1 < 0) off1 = 0;
    if (off2 > xtd_blockCharCount(blk)) off2 = xtd_blockCharCount(blk);
    if (off1 >= off2) return;
    /* 字节偏移先落成片段边界（同插入口径——否则区间落在片段内时会整段
     * 放过，实测 "abcd" 删 [1,2) 无效）。 */
    xtd_blockEnsureFragBoundary(self, bi, off1);
    xtd_blockEnsureFragBoundary(self, bi, off2);
    i1 = xtd_fragAtOffset(blk, off1);
    i2 = xtd_fragAtOffset(blk, off2);
    for (i = i1; i < i2; ++i)
        xtd_fragClear(&blk->fragments[i]);
    del = i2 - i1;
    if (del > 0) {
        for (i = i2; i < blk->fragmentCount; ++i)
            blk->fragments[i - del] = blk->fragments[i];
        XMemset(&blk->fragments[blk->fragmentCount - del], 0,
                sizeof(XTDFragment) * (size_t)del);
        blk->fragmentCount = (uint8_t)(blk->fragmentCount - del);
    }
    xtd_blockCoalesce(self, bi);
}

/** @brief 块尾截断到 off（[off, 块长) 删除）。 */
static void xtd_blockTruncateTail(XTextDocument* self, int bi, int off)
{
    if (!self || !self->m_blocks) return;
    if (bi < 0 || bi >= self->m_blockCount) return;
    xtd_blockDeleteSpan(self, bi, off, xtd_blockCharCount(&self->m_blocks[bi]));
}

/** @brief 块头截断到 off（[0, off) 删除）。 */
static void xtd_blockTruncateHead(XTextDocument* self, int bi, int off)
{
    if (!self || !self->m_blocks) return;
    if (bi < 0 || bi >= self->m_blockCount) return;
    xtd_blockDeleteSpan(self, bi, 0, off);
}

/**
 * @brief  编辑前压快照（统一入口）。
 * @details 块外：压一步快照（元数据在编辑登记时回填——钳位后的实际区间
 *          才是 undo 映射需要的）；块内：本块首编辑压一次（占位 None，
 *          endEditBlock 回填），后续编辑不再压——「块内只压一次栈」。
 */
static void xtd_editBeforeChange(XTextDocument* self)
{
    int topBefore;
    if (!self) return;
    if (self->m_editBlockDepth > 0) {
        if (self->m_editBlockSnapshot) return;
        topBefore = self->m_undoTop;
        xtd_saveSnapshot(self);
        self->m_editBlockSnapshot = (self->m_undoTop > topBefore) ? 1 : 0;
        if (self->m_editBlockSnapshot) {
            xtd_metaFillUndo(self, 0, 0, 0, (int)XTDUndoOp_None,
                             (int)XTDUndoOp_None);
        }
        return;
    }
    xtd_saveSnapshot(self);
}

/**
 * @brief  编辑登记（统一出口）：块外=补元数据+精确区间发射；块内=挂起合并
 *         （Qt adjustDocumentChangesAndCursors 的 documentChange 合并公式
 *         qtextdocument_p.cpp:1268-1291 逐条移植），endEditBlock 统一发射。
 * @param  opIns 插入侧光标口径（added>0 时生效）；opRem 删除侧口径
 *         （removed>0 时生效）。
 */
static void xtd_editAfterChange(XTextDocument* self, int from, int removed,
                                int added, int opIns, int opRem)
{
    if (!self) return;
    if (self->m_editBlockDepth > 0) {
        int delta = added - removed;
        if (!self->m_editBlockPending) {
            self->m_editBlockFrom = from;
            self->m_editBlockOldLen = removed;
            self->m_editBlockLen = added;
            self->m_editBlockPending = 1;
        } else {
            int addedN = (delta > 0) ? delta : 0;
            int removedN = (delta < 0) ? -delta : 0;
            int diff = 0;
            if (from + removedN < self->m_editBlockFrom)
                diff = self->m_editBlockFrom - from - removedN;
            else if (from > self->m_editBlockFrom + self->m_editBlockLen)
                diff = from - (self->m_editBlockFrom + self->m_editBlockLen);
            {
                int overlapStart = (from > self->m_editBlockFrom)
                                       ? from : self->m_editBlockFrom;
                int overlapEnd = (from + removedN <
                                  self->m_editBlockFrom + self->m_editBlockLen)
                                     ? from + removedN
                                     : self->m_editBlockFrom + self->m_editBlockLen;
                int removedInside = overlapEnd - overlapStart;
                if (removedInside < 0) removedInside = 0;
                removedN -= removedInside;
                self->m_editBlockFrom = (self->m_editBlockFrom < from)
                                            ? self->m_editBlockFrom : from;
                self->m_editBlockOldLen += removedN + diff;
                self->m_editBlockLen += addedN - removedInside + diff;
            }
        }
        self->m_editBlockEdits++;
        if (removed > 0 && self->m_editBlockOpRem == (int)XTDUndoOp_None)
            self->m_editBlockOpRem = opRem;
        if (added > 0 && self->m_editBlockOpIns == (int)XTDUndoOp_None)
            self->m_editBlockOpIns = opIns;
        XTextDocument_markContentsDirty(self, from,
                                        removed > 0 ? removed : added);
        return;
    }
    /* 块外分支同款标脏（[复核 2026-11-16 P2] 与块内分支统一——Qt 的
     * insert/remove 无条件 documentChange（qtextdocument_p.cpp
     * adjustDocumentChanges→documentChanged 路径），「是否标脏」不得取决于
     * 是否包在编辑块里；W3 布局引擎消费脏区时两条路径等价）。
     * 口径即契约：本函数覆盖的新式编辑入口（insertTextAt 族、removeRange
     * 族、insertBlockAt、setCharFormatRange、setBlockFormatRange）
     * 无条件标脏；既有编辑入口（setPlainText、setFragment 系、appendHtml
     * 等）维持既有不标脏口径，两类入口不混用同一断言。 */
    xtd_metaFillUndo(self, from, removed, added, opIns, opRem);
    XTextDocument_markContentsDirty(self, from,
                                    removed > 0 ? removed : added);
    xtd_setPreciseChange(self, from, removed, added);
    xtd_changed(self);
}

/** @brief 含字节偏移的片段下标（严格包含；偏移在片段边界时=前一片段——
 *         「前一个字符所在片段」语义；空块回 0）。与 xtd_fragAtOffset 的
 *         「边界取后」语义不同，勿混用。 */
static int xtd_fragContaining(const XTDBlock* blk, int byteOffset)
{
    int fi;
    int acc = 0;
    if (!blk || blk->fragmentCount <= 0) return 0;
    for (fi = 0; fi < blk->fragmentCount; ++fi) {
        int flen = (int)XStrlen(xtd_fragText(&blk->fragments[fi]));
        if (byteOffset < acc + flen) return fi;
        acc += flen;
    }
    return blk->fragmentCount - 1;
}

/** @brief 插入格式继承（Qt charFormat() 派生规则，qtextcursor.cpp:1857-1889；
 *         空块的前块末字符口径 = Qt 分隔符片段格式的等价承载）。 */
static void xtd_inheritInsertFormat(const XTextDocument* self, int pos,
                                    XTDCharFormat* out)
{
    int bi = 0;
    int off = 0;
    int len;
    const XTDBlock* blk;
    int fi;
    XMemset(out, 0, sizeof(*out));
    if (!self || !self->m_blocks || self->m_blockCount <= 0) return;
    xtd_posLocate(self, pos, &bi, &off);
    blk = &self->m_blocks[bi];
    len = xtd_blockCharCount(blk);
    if (off == 0 && len > 0) {
        /* 块首且块非空：继承块首字符（= 片段 0）。 */
        if (blk->fragmentCount > 0)
            xtd_formatAssign(out, &blk->fragments[0].fmt);
        return;
    }
    if (off > 0) {
        /* 块中/块尾：继承前一个字符所在片段（含其片段边界情形）。 */
        int want = xtd_fragContaining(blk, off - 1);
        if (want >= 0 && want < blk->fragmentCount)
            xtd_formatAssign(out, &blk->fragments[want].fmt);
        return;
    }
    /* 空块：继承前一块最后字符的格式（Qt 分隔符片段格式）；前块也无字符
     * （或已是首块）→ 文档缺省格式。 */
    if (bi > 0) {
        const XTDBlock* prev = &self->m_blocks[bi - 1];
        int pfi = prev->fragmentCount;
        for (fi = pfi - 1; fi >= 0; --fi) {
            if (XStrlen(xtd_fragText(&prev->fragments[fi])) > 0 ||
                prev->fragments[fi].image) {
                xtd_formatAssign(out, &prev->fragments[fi].fmt);
                return;
            }
        }
    }
    xtd_formatAssign(out, &self->m_defaultFormat);
}

/** @brief 文档位置处一个 UTF-8 序列的字节长（码点步进口径；分隔符位与
 *         残序列按 1 处理——xtd_utf8SeqLen 的文档位置版）。 */
static int xtd_seqLenAt(const XTextDocument* self, int pos)
{
    int bi = 0;
    int off = 0;
    const XTDBlock* blk;
    int fi;
    int acc = 0;
    int blockLen;
    const char* text;
    int flen;
    unsigned char c;
    int seq = 1;
    if (!self || !self->m_blocks || self->m_blockCount <= 0 || pos < 0)
        return 1;
    xtd_posLocate(self, pos, &bi, &off);
    blk = &self->m_blocks[bi];
    blockLen = xtd_blockCharCount(blk);
    if (off >= blockLen) return 1; /* 分隔符位 = 单字节口径。 */
    for (fi = 0; fi < blk->fragmentCount; ++fi) {
        text = xtd_fragText(&blk->fragments[fi]);
        flen = (int)XStrlen(text);
        if (off < acc + flen) {
            c = (unsigned char)text[off - acc];
            if (c >= 0xC2 && c < 0xE0) seq = 2;
            else if (c >= 0xE0 && c < 0xF0) seq = 3;
            else if (c >= 0xF0 && c < 0xF5) seq = 4;
            /* 序列越过块尾截断为 1（残序列口径）。 */
            if (off + seq > blockLen) seq = 1;
            return seq;
        }
        acc += flen;
    }
    return 1;
}

int XTextDocument_insertTextAtFmt(XTextDocument* self, int pos,
                                  const char* utf8, const XTDCharFormat* fmt)
{
    XTDCharFormat inherited;
    const XTDCharFormat* useFmt;
    int bi;
    int off;
    int added = 0;
    const char* p;
    if (!self || !self->m_blocks || !utf8) return 0;
    if (self->m_blockCount <= 0) return 0;
    if (!utf8[0]) return 0;
    xtd_posLocate(self, pos, &bi, &off);
    if (fmt) {
        useFmt = fmt;
    } else {
        xtd_inheritInsertFormat(self, pos, &inherited);
        useFmt = &inherited;
    }
    xtd_editBeforeChange(self);
    p = utf8;
    while (*p) {
        if (*p == '\n' || *p == '\r' ||
            (*p == '\xE2' && p[1] == '\x80' && p[2] == '\xA9')) {
            /* 块分隔符（'\n' / '\r\n' 合并 / U+2029；孤立 '\r' 同 Qt
             * qtextcursor.cpp:1434-1445 口径）；run 已在文本分支即时提交，
             * 此处仅拆块推进。 */
            int seq = (*p == '\xE2') ? 3
                                     : ((*p == '\r' && p[1] == '\n') ? 2 : 1);
            if (xtd_blockSplitAt(self, bi, off)) {
                bi += 1;
                off = 0;
                added += 1;
            }
            /* 拆分失败（块数上限）：分隔符丢弃，文本并入当前块（解析器
             * advanceBlock 拒绝推进的同口径）。 */
            p += seq;
            continue;
        }
        {
            /* UTF-8 序列整段累积到本 run（多字节不拆散）。 */
            int start = (int)(p - utf8);
            int runLen;
            while (*p && *p != '\n' && *p != '\r' &&
                   !(*p == '\xE2' && p[1] == '\x80' && p[2] == '\xA9')) {
                p += xtd_utf8SeqLen(p);
            }
            runLen = (int)(p - utf8) - start;
            if (runLen > 0) {
                xtd_blockInsertRun(self, bi, off, utf8 + start, runLen, useFmt);
                off += runLen;
                added += runLen;
            }
        }
    }
    /* 清理：继承槽是本栈帧资产（xtd_inheritInsertFormat → xtd_formatAssign
     * 对 fontFamily/anchorHref 深拷贝建串）——片段虽已深拷贝，栈槽自身的
     * 字符串必须在此释放（[复核 2026-11-16 P1 实测 200 轮带 fontFamily 插入
     * 泄漏 ≈106B/轮]）；显式 fmt 是调用方借用，不在本函数清理范围。 */
    if (!fmt) xtd_formatClear(&inherited);
    if (added > 0) {
        /* 插入全部发生在同一增长沿：终点位 - added = 起点（编辑登记 from）。 */
        xtd_editAfterChange(self, xtd_blockTextStart(self, bi) + off - added,
                            0, added, (int)XTDUndoOp_MoveCursor,
                            (int)XTDUndoOp_KeepCursor);
    }
    /* added == 0（纯分隔符被块数上限吞掉）：占位快照弹回，不占撤销步。 */
    else if (self->m_editBlockDepth == 0 && self->m_undoTop > 0) {
        XFree_System(self->m_undoStack[self->m_undoTop - 1]);
        self->m_undoStack[self->m_undoTop - 1] = NULL;
        self->m_undoTop -= 1;
        xtd_notifyUndoRedo(self);
    }
    return added;
}

int XTextDocument_insertTextAt(XTextDocument* self, int pos, const char* utf8)
{
    return XTextDocument_insertTextAtFmt(self, pos, utf8, NULL);
}

/** @brief removeRange 的光标口径版（op 决定 undo 后活动光标落点：
 *         KeepCursor=原位（deleteChar）；MoveCursor=落删除起点（选区删除
 *         时 position 在选区尾 / 回格）。 */
static int xtd_removeRangeOp(XTextDocument* self, int from, int len, int op)
{
    int bi1;
    int off1;
    int end;
    int bi2;
    int off2;
    int charCount;
    int mid;
    int removed;
    if (!self || !self->m_blocks || self->m_blockCount <= 0) return 0;
    if (len <= 0) return 0;
    charCount = XTextDocument_characterCount(self);
    if (from < 0) from = 0;
    if (from > charCount - 1) from = charCount - 1;
    end = from + len;
    if (end > charCount) end = charCount;
    if (end <= from) return 0;
    removed = end - from;
    xtd_posLocate(self, from, &bi1, &off1);
    if (end >= charCount) {
        /* 越过末尾：终点块=末块、块尾（含末分隔符）。 */
        bi2 = self->m_blockCount - 1;
        off2 = xtd_blockCharCount(&self->m_blocks[bi2]);
    } else {
        xtd_posLocate(self, end, &bi2, &off2);
    }
    xtd_editBeforeChange(self);
    if (bi2 == bi1) {
        /* 同块删除。 */
        xtd_blockDeleteSpan(self, bi1, off1, off2);
    } else {
        /* 跨块：起点块截尾 → 中间整块删除 → 终点块截头并入起点块 →
         * 终点块槽删除（探针 N1/N1c：起点块保留、文本拼接、后续前移）。 */
        int mergedInto;
        xtd_blockTruncateTail(self, bi1, off1);
        mid = bi2 - bi1 - 1;
        if (mid > 0) {
            int i;
            int j;
            for (i = bi1 + 1; i < bi1 + 1 + mid; ++i) {
                for (j = 0; j < self->m_blocks[i].fragmentCount; ++j)
                    xtd_fragClear(&self->m_blocks[i].fragments[j]);
                if (self->m_blocks[i].blockFormat) {
                    XClassDelete(self->m_blocks[i].blockFormat);
                    self->m_blocks[i].blockFormat = NULL;
                }
            }
            XMemmove(&self->m_blocks[bi1 + 1], &self->m_blocks[bi1 + 1 + mid],
                     sizeof(XTDBlock) * (size_t)(self->m_blockCount - bi1 - 1 - mid));
            XMemset(&self->m_blocks[self->m_blockCount - mid], 0,
                    sizeof(XTDBlock) * (size_t)mid);
            self->m_blockCount -= mid;
            bi2 -= mid;
        }
        xtd_blockTruncateHead(self, bi2, off2);
        /* 终点块剩余片段并入起点块（片段指针转移；槽满时并入末片段保文本）。 */
        mergedInto = bi1;
        {
            XTDBlock* dst = &self->m_blocks[mergedInto];
            XTDBlock* src = &self->m_blocks[bi2];
            int k;
            for (k = 0; k < src->fragmentCount; ++k) {
                if (dst->fragmentCount >= XTD_MAX_FRAGMENTS_PER_BLOCK) {
                    /* 槽满：文本并入末片段（格式边界丢弃——池满钳位同口径）。 */
                    XTDFragment* pf =
                        &dst->fragments[XTD_MAX_FRAGMENTS_PER_BLOCK - 1];
                    const char* t = xtd_fragText(&src->fragments[k]);
                    if (!pf->text) pf->text = XString_create();
                    if (pf->text)
                        XString_append_with_length_utf8(
                            pf->text, t, (size_t)XStrlen(t));
                    xtd_fragClear(&src->fragments[k]);
                    XMemset(&src->fragments[k], 0, sizeof(XTDFragment));
                } else {
                    dst->fragments[dst->fragmentCount] = src->fragments[k];
                    XMemset(&src->fragments[k], 0, sizeof(XTDFragment));
                    dst->fragmentCount = (uint8_t)(dst->fragmentCount + 1);
                }
            }
            src->fragmentCount = 0;
        }
        /* 删除终点块槽（载荷已清，块结构整体前移；尾部槽清零防重复指针）。 */
        {
            int i;
            if (self->m_blocks[bi2].blockFormat) {
                XClassDelete(self->m_blocks[bi2].blockFormat);
                self->m_blocks[bi2].blockFormat = NULL;
            }
            for (i = bi2 + 1; i < self->m_blockCount; ++i)
                self->m_blocks[i - 1] = self->m_blocks[i];
            XMemset(&self->m_blocks[self->m_blockCount - 1], 0,
                    sizeof(XTDBlock));
            self->m_blockCount--;
        }
        if (self->m_blockCount < 1) self->m_blockCount = 1;
        xtd_blockCoalesce(self, mergedInto);
    }
    xtd_editAfterChange(self, from, removed, 0,
                        (int)XTDUndoOp_KeepCursor, op);
    return removed;
}

int XTextDocument_removeRange(XTextDocument* self, int from, int len)
{
    /* 文档级缺省口径 = KeepCursor（deleteChar 族：undo 后光标原位，
     * Qt 探针 N2c）；选区删除的 MoveCursor 口径走 xtd_removeRangeOp。 */
    return xtd_removeRangeOp(self, from, len, (int)XTDUndoOp_KeepCursor);
}

int XTextDocument_removeRangeOp(XTextDocument* self, int from, int len, int op)
{
    return xtd_removeRangeOp(self, from, len, op);
}

int XTextDocument_setCharFormatRange(XTextDocument* self, int from, int len,
                                     const XTextCharFormat* fmt, bool merge)
{
    int bi1;
    int off1;
    int end;
    int bi2;
    int off2;
    int charCount;
    int bi;
    int applied = 0;
    XTDCharFormat converted;
    if (!self || !self->m_blocks || self->m_blockCount <= 0 || !fmt) return 0;
    if (len <= 0) return 0;
    charCount = XTextDocument_characterCount(self);
    if (from < 0) from = 0;
    if (from > charCount - 1) from = charCount - 1;
    end = from + len;
    if (end > charCount - 1) end = charCount - 1;
    if (end <= from) return 0;
    xtd_posLocate(self, from, &bi1, &off1);
    xtd_posLocate(self, end, &bi2, &off2);
    xtd_editBeforeChange(self);
    /* 属性格式先转内联（SetFormat 整段替换载荷；栈值先清零——转换入口
     * 会清旧字符串字段，垃圾指针必炸）。 */
    XMemset(&converted, 0, sizeof(converted));
    XTextDocument_textFormatToCharFormat(&converted, fmt);
    for (bi = bi1; bi <= bi2; ++bi) {
        XTDBlock* blk = &self->m_blocks[bi];
        int fi;
        int startFi = 0;
        int endFi = blk->fragmentCount;
        if (bi == bi1) {
            xtd_blockEnsureFragBoundary(self, bi1, off1);
            startFi = xtd_fragAtOffset(blk, off1);
        }
        if (bi == bi2) {
            xtd_blockEnsureFragBoundary(self, bi2, off2);
            endFi = xtd_fragAtOffset(blk, off2);
        }
        if (bi1 == bi2 && endFi < startFi) endFi = startFi;
        for (fi = startFi; fi < endFi && fi < blk->fragmentCount; ++fi) {
            XTDFragment* frag = &blk->fragments[fi];
            if (merge) {
                /* 属性空间合并（Qt MergeFormat）：内联→属性表→merge→回。 */
                XTextFormat asFmt;
                XTextDocument_charFormatToTextFormat(&asFmt, &frag->fmt);
                XTextFormat_merge(&asFmt, fmt);
                xtd_formatClear(&frag->fmt);
                XMemset(&frag->fmt, 0, sizeof(XTDCharFormat));
                XTextDocument_textFormatToCharFormat(&frag->fmt, &asFmt);
                XTextFormat_deinit(&asFmt);
            } else {
                xtd_formatClear(&frag->fmt);
                XMemset(&frag->fmt, 0, sizeof(XTDCharFormat));
                xtd_formatAssign(&frag->fmt, &converted);
            }
            frag->m_fmtPoolIndex = -1;
            xtd_fragPoolSync(self, bi, fi);
            applied++;
        }
    }
    xtd_formatClear(&converted);
    xtd_editAfterChange(self, from, end - from, end - from,
                        (int)XTDUndoOp_KeepCursor, (int)XTDUndoOp_KeepCursor);
    return applied;
}

void XTextDocument_charFormatAt(const XTextDocument* self, int pos,
                                XTDCharFormat* out)
{
    if (!out) return;
    xtd_inheritInsertFormat(self, pos, out);
}

int XTextDocument_setBlockFormatRange(XTextDocument* self, int from,
                                      int len, int alignment, int indent,
                                      int heading)
{
    int bi1;
    int off1;
    int end;
    int bi2;
    int off2;
    int bi;
    int changed = 0;
    if (!self || !self->m_blocks || self->m_blockCount <= 0) return 0;
    if (len <= 0) len = 1;
    {
        int charCount = XTextDocument_characterCount(self);
        if (from < 0) from = 0;
        if (from > charCount - 1) from = charCount - 1;
        end = from + len;
        if (end > charCount) end = charCount;
    }
    xtd_posLocate(self, from, &bi1, &off1);
    xtd_posLocate(self, end, &bi2, &off2);
    if (indent < -32768) indent = -32768;
    if (indent > 32767) indent = 32767;
    if (heading < 0 || heading > 6) heading = 0;
    /* 先查变化（无变化不压快照不发信号——Qt 同值不动不进撤销栈口径）。 */
    for (bi = bi1; bi <= bi2; ++bi) {
        XTDBlock* blk = &self->m_blocks[bi];
        if (blk->alignment != alignment || blk->indentLevel != (int16_t)indent ||
            blk->headingLevel != (uint8_t)heading)
            changed++;
    }
    if (changed <= 0) return 0;
    xtd_editBeforeChange(self);
    for (bi = bi1; bi <= bi2; ++bi) {
        XTDBlock* blk = &self->m_blocks[bi];
        blk->alignment = alignment;
        blk->indentLevel = (int16_t)indent;
        blk->headingLevel = (uint8_t)heading;
    }
    /* Qt BlockFormatChanged：documentChange(块位置, 块长度) →
       contentsChange(from, 长, 长)（qtextdocument_p.cpp:922-944）。 */
    xtd_editAfterChange(self, xtd_blockTextStart(self, bi1),
                        xtd_blockTextStart(self, bi2) +
                            xtd_blockCharCount(&self->m_blocks[bi2]) -
                            xtd_blockTextStart(self, bi1),
                        xtd_blockTextStart(self, bi2) +
                            xtd_blockCharCount(&self->m_blocks[bi2]) -
                            xtd_blockTextStart(self, bi1),
                        (int)XTDUndoOp_KeepCursor, (int)XTDUndoOp_KeepCursor);
    return changed;
}

int XTextDocument_insertBlockAt(XTextDocument* self, int pos)
{
    int bi;
    int off;
    int at;
    if (!self || !self->m_blocks || self->m_blockCount <= 0) return 0;
    xtd_posLocate(self, pos, &bi, &off);
    at = xtd_blockTextStart(self, bi) + off;
    xtd_editBeforeChange(self);
    if (!xtd_blockSplitAt(self, bi, off)) {
        /* 块数上限：占位快照弹回（同 insertTextAt 的 added==0 口径）。 */
        if (self->m_editBlockDepth == 0 && self->m_undoTop > 0) {
            XFree_System(self->m_undoStack[self->m_undoTop - 1]);
            self->m_undoStack[self->m_undoTop - 1] = NULL;
            self->m_undoTop -= 1;
            xtd_notifyUndoRedo(self);
        }
        return 0;
    }
    xtd_editAfterChange(self, at, 0, 1, (int)XTDUndoOp_MoveCursor,
                        (int)XTDUndoOp_KeepCursor);
    return 1;
}

void XTextDocument_beginEditBlock(XTextDocument* self)
{
    if (!self) return;
    if (self->m_editBlockDepth == 0) {
        /* 顶层块开始：首编辑时压占位快照（空块不占撤销步——Qt 无命令
         * 不入栈口径），区间挂起清零；两侧光标口径以 None 为「未定」哨兵，
         * 首个对应方向的编辑回填。 */
        self->m_editBlockPending = 0;
        self->m_editBlockFrom = 0;
        self->m_editBlockOldLen = 0;
        self->m_editBlockLen = 0;
        self->m_editBlockEdits = 0;
        self->m_editBlockOpIns = (int)XTDUndoOp_None;
        self->m_editBlockOpRem = (int)XTDUndoOp_None;
        self->m_editBlockSnapshot = 0;
    }
    self->m_editBlockDepth++;
}

void XTextDocument_endEditBlock(XTextDocument* self)
{
    if (!self) return;
    if (self->m_editBlockDepth <= 0) return;
    self->m_editBlockDepth--;
    if (self->m_editBlockDepth > 0) return; /* 嵌套层：不关块。 */
    if (self->m_editBlockEdits <= 0) {
        /* 空块：占位快照弹回（Qt：无命令不占撤销步），不发信号。 */
        if (self->m_editBlockSnapshot && self->m_undoTop > 0) {
            XFree_System(self->m_undoStack[self->m_undoTop - 1]);
            self->m_undoStack[self->m_undoTop - 1] = NULL;
            self->m_undoTop -= 1;
            xtd_notifyUndoRedo(self);
        }
    } else {
        /* 非空块：撤销栈顶（本块唯一快照）回填合并元数据（未定侧回
         * KeepCursor——纯插入块无删除口径、反之亦然），按合并区间发射一次
         * （Qt finishEdit 的编辑块口径；探针 N8 一次 contentsChange）。 */
        if (self->m_editBlockOpIns == (int)XTDUndoOp_None)
            self->m_editBlockOpIns = (int)XTDUndoOp_KeepCursor;
        if (self->m_editBlockOpRem == (int)XTDUndoOp_None)
            self->m_editBlockOpRem = (int)XTDUndoOp_KeepCursor;
        xtd_metaFillUndo(self, self->m_editBlockFrom, self->m_editBlockOldLen,
                         self->m_editBlockLen, self->m_editBlockOpIns,
                         self->m_editBlockOpRem);
        xtd_setPreciseChange(self, self->m_editBlockFrom,
                             self->m_editBlockOldLen, self->m_editBlockLen);
        xtd_changed(self);
    }
    self->m_editBlockPending = 0;
    self->m_editBlockEdits = 0;
    self->m_editBlockSnapshot = 0;
}

bool XTextDocument_isInEditBlock(const XTextDocument* self)
{
    return self ? self->m_editBlockDepth > 0 : false;
}


/* ==================== 信号 ==================== */

void* XTextDocument_contentsChanged_signal(XTextDocument* self)
{ (void)self; return (void*)(size_t)XTextDocument_contentsChanged_signal; }
void* XTextDocument_contentsChange_signal(XTextDocument* self, int from,
                                          int charsRemoved, int charsAdded)
{
    (void)self; (void)from; (void)charsRemoved; (void)charsAdded;
    return (void*)(size_t)XTextDocument_contentsChange_signal;
}
void* XTextDocument_blockCountChanged_signal(XTextDocument* self, int newCount)
{ (void)self; (void)newCount; return (void*)(size_t)XTextDocument_blockCountChanged_signal; }
void* XTextDocument_modificationChanged_signal(XTextDocument* self, bool modified)
{ (void)self; (void)modified; return (void*)(size_t)XTextDocument_modificationChanged_signal; }
void* XTextDocument_baseUrlChanged_signal(XTextDocument* self)
{ (void)self; return (void*)(size_t)XTextDocument_baseUrlChanged_signal; }
void* XTextDocument_cursorPositionChanged_signal(XTextDocument* self)
{ (void)self; return (void*)(size_t)XTextDocument_cursorPositionChanged_signal; }
void* XTextDocument_documentLayoutChanged_signal(XTextDocument* self)
{ (void)self; return (void*)(size_t)XTextDocument_documentLayoutChanged_signal; }
void* XTextDocument_redoAvailable_signal(XTextDocument* self, bool available)
{ (void)self; (void)available; return (void*)(size_t)XTextDocument_redoAvailable_signal; }
void* XTextDocument_undoAvailable_signal(XTextDocument* self, bool available)
{ (void)self; (void)available; return (void*)(size_t)XTextDocument_undoAvailable_signal; }
void* XTextDocument_undoCommandAdded_signal(XTextDocument* self)
{ (void)self; return (void*)(size_t)XTextDocument_undoCommandAdded_signal; }


/* ==================== 撤销/重做栈 ==================== */

#define XTD_MAX_UNDO XTD_MAX_UNDO_STEPS /* 栈深常量已上收到头文件。 */

static void xtd_saveSnapshot(XTextDocument* self)
{
    char* snap;
    if (!self || !self->m_undoRedoEnabled) return;
    snap = XTextDocument_toPlainText(self);
    if (!snap) return;
    if (self->m_undoTop < XTD_MAX_UNDO) {
        /* 新槽位元数据先清成「无元数据」（编辑登记随后回填——若调用方
         * 是既有入口则保持 None，undo 不动光标）。 */
        self->m_undoMetaFrom[self->m_undoTop] = 0;
        self->m_undoMetaRemoved[self->m_undoTop] = 0;
        self->m_undoMetaAdded[self->m_undoTop] = 0;
        self->m_undoMetaOpIns[self->m_undoTop] = (uint8_t)XTDUndoOp_None;
        self->m_undoMetaOpRem[self->m_undoTop] = (uint8_t)XTDUndoOp_None;
        self->m_undoStack[self->m_undoTop++] = snap;
    } else {
        int i;
        XFree_System(self->m_undoStack[0]);
        for (i = 0; i < XTD_MAX_UNDO - 1; ++i) {
            self->m_undoStack[i] = self->m_undoStack[i + 1];
            /* 元数据平行数组随栈整体移位（槽位与快照一一对应）。 */
            self->m_undoMetaFrom[i] = self->m_undoMetaFrom[i + 1];
            self->m_undoMetaRemoved[i] = self->m_undoMetaRemoved[i + 1];
            self->m_undoMetaAdded[i] = self->m_undoMetaAdded[i + 1];
            self->m_undoMetaOpIns[i] = self->m_undoMetaOpIns[i + 1];
            self->m_undoMetaOpRem[i] = self->m_undoMetaOpRem[i + 1];
        }
        self->m_undoStack[XTD_MAX_UNDO - 1] = snap;
        self->m_undoMetaFrom[XTD_MAX_UNDO - 1] = 0;
        self->m_undoMetaRemoved[XTD_MAX_UNDO - 1] = 0;
        self->m_undoMetaAdded[XTD_MAX_UNDO - 1] = 0;
        self->m_undoMetaOpIns[XTD_MAX_UNDO - 1] = (uint8_t)XTDUndoOp_None;
        self->m_undoMetaOpRem[XTD_MAX_UNDO - 1] = (uint8_t)XTDUndoOp_None;
    }
    /* 新变更使既有重做历史失效（Qt 同语义）；此前直接置 redoTop=0
     * 未释放重做快照——泄漏修复。 */
    {
        int i;
        for (i = 0; i < self->m_redoTop; ++i)
            XFree_System(self->m_redoStack[i]);
        self->m_redoTop = 0;
    }
    xtd_notifyUndoRedo(self);
}

static void xtd_restoreSnapshot(XTextDocument* self, const char* text)
{
    /* 内部路径：不经公共 setPlainText（否则 clear 会清空刚压入的重做
     * 快照，撤销链断裂）；恢复仅承载纯文本（快照制子集，格式不回放）。 */
    xtd_setPlainTextNoHistory(self, text);
}

/* ==================== 列表格式（QTextListFormat 子集） ==================== */

/**
 * @brief  QTextListFormat::Style → 块内 4 bit 编码（取绝对值）。
 * @details Qt 枚举为 -1..-8 连续 8 档 + 0（Undefined）（qtextformat.h:
 *          730-740）⇒ 编码 1..8；3 bit 只装得下 8 态，「8 档 + Undefined」
 *          共 9 态 ⇒ 收 4 bit（标志位字共 32 位，已用 3+4+1+1=9 位）。
 *          **0 = 未设**（渲染层按 isOrdered 回落 Disc/Decimal）、
 *          **9 = 显式 ListStyleUndefined**（Qt 语义：该列表无标记——
 *          qtextdocumentlayout.cpp:2152-2154 `case ListStyleUndefined:
 *          return;`；来自 HTML `<ul type="none">`）。两者在公开 getter 上
 *          都读 0，渲染层读块字段区分。
 * @param style XTDListStyle 值。
 * @return 0/1..8/9；其它非法值按「未设」返回 0。
 */
static uint32_t xtd_listStyleToCode(int style)
{
    if (style == (int)XTDListStyle_Undefined) return XTD_LIST_STYLE_CODE_UNDEFINED;
    if (style < 0 && style >= (int)XTDListStyle_UpperRoman)
        return (uint32_t)(-style);
    return 0u;
}

/** @brief 清块内列表位与格式（非列表块用；isListItem/isOrdered 由调用方写）。 */
static void xtd_blockClearListFormat(XTDBlock* blk)
{
    if (!blk) return;
    blk->listFresh = 0u;
    blk->listStyleCode = XTD_LIST_STYLE_CODE_UNSET;
    blk->listStartSet = 0u;
    blk->listSuffixParen = 0u;
    blk->listStart = 0;
}

/**
 * @brief  罗马数字（对标 QTextList::itemText 的 ListLower/UpperRoman 分支，
 *         qtextlist.cpp:181-231）。
 * @details 符号表与切片算法逐行照搬 Qt：“iiivixxxlxcccdcmmmm” 的
 *          startDigit = i + (i+3)/4、4/9/40/90/400/900 取 2 位、5/50/500
 *          取 1 位、1/10/100/1000 取 q 位。number<1 回落十进制、number
 *          ≥5000 返回 "?"、大写由小写符号位运算得来（Qt 用两份表）。
 * @return 写入字节数（不含 NUL）。
 */
static int xtd_romanNumber(int number, bool upper, char* out, int cap)
{
    static const char sym[] = "iiivixxxlxcccdcmmmm";
    static const int c[13] = {1, 4, 5, 9, 10, 40, 50, 90, 100, 400, 500,
                              900, 1000};
    char buf[32];
    int n = 0;
    int i;
    int v = number;
    if (!out || cap <= 0) return 0;
    if (number < 1) return XSnprintf(out, (size_t)cap, "%d", number);
    if (number >= 5000) return XSnprintf(out, (size_t)cap, "?");
    for (i = 12; i >= 0; v %= c[i], --i) {
        int q = v / c[i];
        int startDigit;
        int numDigits;
        int k;
        if (q <= 0) continue;
        startDigit = i + (i + 3) / 4;
        if (i % 4) numDigits = ((i - 2) % 4) ? 2 : 1;
        else numDigits = q;
        for (k = 0; k < numDigits && n < (int)sizeof(buf) - 1; ++k) {
            char ch = sym[startDigit + k];
            buf[n++] = upper ? (char)(ch - 'a' + 'A') : ch;
        }
    }
    buf[n] = '\0';
    return XSnprintf(out, (size_t)cap, "%s", buf);
}

/**
 * @brief  列表序号文本（对标 QTextList::itemText 的 switch，qtextlist.cpp:
 *         157-232）：按 style 生成序号本体，不含后缀。
 * @return 写入字节数；无序三态返回 0（调用方画几何标记）。
 */
static int xtd_formatListNumber(int style, int number, char* out, int cap)
{
    int len = 0;
    if (!out || cap <= 0) return 0;
    switch (style) {
    case XTDListStyle_Decimal:
        len = XSnprintf(out, (size_t)cap, "%d", number);
        break;
    case XTDListStyle_LowerAlpha:
    case XTDListStyle_UpperAlpha: {
        /* Qt：低位取模 26 前插（1..26 ⇒ a..z、27 ⇒ aa、28 ⇒ ab）；
           itemNumber<1 回落十进制（qtextlist.cpp:167-171）。 */
        char tmp[24];
        int n = 0;
        int c = number;
        char base = (style == XTDListStyle_UpperAlpha) ? 'A' : 'a';
        if (number < 1) {
            len = XSnprintf(out, (size_t)cap, "%d", number);
            break;
        }
        while (c > 0 && n < (int)sizeof(tmp)) {
            c--;
            tmp[n++] = (char)(base + (c % 26));
            c /= 26;
        }
        while (n > 0 && len < cap - 1) out[len++] = tmp[--n];
        out[len] = '\0';
        break;
    }
    case XTDListStyle_LowerRoman:
        len = xtd_romanNumber(number, false, out, cap);
        break;
    case XTDListStyle_UpperRoman:
        len = xtd_romanNumber(number, true, out, cap);
        break;
    default:
        /* Disc/Circle/Square（无序三态）与 0（未设）：无序号文本。 */
        out[0] = '\0';
        return 0;
    }
    return len > 0 ? len : 0;
}

/**
 * @brief  列表格式字段写入（**无快照、无变更通知**的纯写层）。
 * @details 公开 setter 与 HTML 导入共用本函数（不双份）：HTML 导入是
 *          setHtml 的批量路径，Qt 用单个编辑块收口
 *          （qtextdocumentfragment.cpp:462/569），本实现在 xtd_parseHtml
 *          末尾统一 xtd_changed——故导入路径只落字段，不逐块压快照/发信号
 *          （逐 li 压快照会让一次 setHtml 产生 N 步撤销，且老实现本就是
 *          直写字段）。
 * @param blk 目标块（非 NULL）。
 * @param style/start/suffix 见 setBlockListFormat。
 * @param fresh 新列表首项位。
 */
static void xtd_blockApplyListFormat(XTDBlock* blk, int style, int start,
                                     int suffix, bool fresh)
{
    if (!blk) return;
    if (start < -32768) start = -32768;
    if (start > 32767) start = 32767;
    blk->listStyleCode = xtd_listStyleToCode(style);
    blk->listStart = (int16_t)start;
    blk->listStartSet = 1u;
    blk->listSuffixParen = (suffix == ')') ? 1u : 0u;
    blk->listFresh = fresh ? 1u : 0u;
}

/* ==================== 块级格式 API ==================== */

void XTextDocument_setBlockIndentLevel(XTextDocument* self, int blockIndex, int level)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    self->m_blocks[blockIndex].indentLevel = level;
    xtd_changed(self);
}

int XTextDocument_blockIndentLevel(const XTextDocument* self, int blockIndex)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return 0;
    return self->m_blocks[blockIndex].indentLevel;
}

void XTextDocument_setBlockListItem(XTextDocument* self, int blockIndex, bool isItem, bool ordered)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    /* 编辑块内只压一次栈：Markdown/HTML 导入逐列表项调用本入口，直接
       xtd_saveSnapshot 会为每项各压一步（实测 4 项列表 ⇒ 5 步撤销，
       且单次撤销只回到「第 1 项之后」的中途态）。块外语义不变。 */
    xtd_editBeforeChange(self);
    self->m_blocks[blockIndex].isListItem = isItem;
    self->m_blocks[blockIndex].isOrdered = ordered;
    /* 非列表块：列表格式三属性一并清回（拆块/复用会继承旧列表标志，
       残留 style/start/fresh 会被下一次列表项误认；渲染层虽只在
       isListItem 时消费，清干净仍是零残留口径）。 */
    if (!isItem) xtd_blockClearListFormat(&self->m_blocks[blockIndex]);
    xtd_changed(self);
}

/* ==================== 列表格式三属性（QTextListFormat 子集） ==================== */

void XTextDocument_setBlockListStyle(XTextDocument* self, int blockIndex, int style)
{
    uint32_t code;
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    code = xtd_listStyleToCode(style);
    if (self->m_blocks[blockIndex].listStyleCode == code) return; /* 同值早退。 */
    xtd_editBeforeChange(self); /* 编辑块内只压一次栈（导入路径同源）。 */
    self->m_blocks[blockIndex].listStyleCode = code;
    xtd_changed(self);
}

int XTextDocument_blockListStyle(const XTextDocument* self, int blockIndex)
{
    uint32_t code;
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return 0;
    code = self->m_blocks[blockIndex].listStyleCode;
    /* 1..8 → -1..-8；0（未设）与 9（显式 Undefined）都读 Undefined(0)
       ——需要区分「无标记」的消费方读块字段 listStyleCode。 */
    return (code >= 1u && code <= 8u) ? -(int)code : 0;
}

void XTextDocument_setBlockListStart(XTextDocument* self, int blockIndex, int start)
{
    XTDBlock* blk;
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    if (start < -32768) start = -32768;
    if (start > 32767) start = 32767;
    blk = &self->m_blocks[blockIndex];
    if (blk->listStartSet && (int)blk->listStart == start) return; /* 同值早退。 */
    xtd_editBeforeChange(self);
    blk->listStart = (int16_t)start;
    blk->listStartSet = 1;
    xtd_changed(self);
}

int XTextDocument_blockListStart(const XTextDocument* self, int blockIndex)
{
    const XTDBlock* blk;
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return 1;
    blk = &self->m_blocks[blockIndex];
    return blk->listStartSet ? (int)blk->listStart : 1; /* Qt 缺省 start=1。 */
}

void XTextDocument_setBlockListSuffix(XTextDocument* self, int blockIndex, int suffix)
{
    XTDBlock* blk;
    uint32_t paren;
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    paren = (suffix == ')') ? 1u : 0u; /* 只承载 '.'/')' 两档（其余按 '.'）。 */
    blk = &self->m_blocks[blockIndex];
    if (blk->listSuffixParen == paren) return; /* 同值早退。 */
    xtd_editBeforeChange(self);
    blk->listSuffixParen = paren;
    xtd_changed(self);
}

char XTextDocument_blockListSuffix(const XTextDocument* self, int blockIndex)
{
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return '.';
    return self->m_blocks[blockIndex].listSuffixParen ? ')' : '.';
}

void XTextDocument_setBlockListFormat(XTextDocument* self, int blockIndex,
                                      int style, int start, int suffix,
                                      bool fresh)
{
    XTDBlock* blk;
    uint32_t code;
    uint32_t paren;
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return;
    if (start < -32768) start = -32768;
    if (start > 32767) start = 32767;
    code = xtd_listStyleToCode(style);
    paren = (suffix == ')') ? 1u : 0u;
    blk = &self->m_blocks[blockIndex];
    if (blk->listStyleCode == code && blk->listStartSet &&
        (int)blk->listStart == start && blk->listSuffixParen == paren &&
        blk->listFresh == (fresh ? 1u : 0u))
        return; /* 同值早退。 */
    xtd_editBeforeChange(self);
    xtd_blockApplyListFormat(blk, style, start, suffix, fresh);
    xtd_changed(self);
}

int XTextDocument_listItemText(const XTextDocument* self, int blockIndex,
                               int itemIndex, char* out, int cap)
{
    const XTDBlock* blk;
    int style;
    int number;
    int len;
    if (out && cap > 0) out[0] = '\0';
    if (!self || blockIndex < 0 || blockIndex >= self->m_blockCount) return 0;
    blk = &self->m_blocks[blockIndex];
    /* 显式 Undefined（Qt ListStyleUndefined）= 无标记（Qt 渲染层对
       ListStyleUndefined 直接 return，qtextdocumentlayout.cpp:2152）。 */
    if (blk->listStyleCode == XTD_LIST_STYLE_CODE_UNDEFINED) return 0;
    style = blk->listStyleCode ? -(int)blk->listStyleCode
                               : (int)XTDListStyle_Decimal;
    number = itemIndex + (blk->listStartSet ? (int)blk->listStart : 1) - 1;
    len = xtd_formatListNumber(style, number, out, cap);
    if (len <= 0) return 0; /* 无序三态：渲染层画几何标记。 */
    /* 后缀（QTextList::itemText 末尾 numberPrefix+result+numberSuffix 的
       子集：无 prefix 属性；超长按截断，恒 NUL 结尾）。 */
    if (out && cap > 0 && len < cap - 1) {
        out[len++] = blk->listSuffixParen ? ')' : '.';
        out[len] = '\0';
    }
    /* 返回值按**实际写入**计：xtd_formatListNumber 取 XSnprintf 的 snprintf
       语义（返回「应有长度」，截断时大于缓冲），直接透出会与
       「写入的字节数（不含 NUL）」的契约矛盾（复核实测 cap=2、序号 123 ⇒
       返回 3 而缓冲只有 "1"）。此处以 strlen 收口。 */
    return (int)XStrlen(out ? out : "");
}

/* ==================== 片段格式 API ==================== */

void XTextDocument_setFragmentBold(XTextDocument* self, int bi, int fi, bool bold)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    self->m_blocks[bi].fragments[fi].fmt.bold = bold;
    xtd_fragPoolSync(self, bi, fi); /* 影子池登记 */
    xtd_changed(self);
}

void XTextDocument_setFragmentItalic(XTextDocument* self, int bi, int fi, bool italic)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    self->m_blocks[bi].fragments[fi].fmt.italic = italic;
    xtd_fragPoolSync(self, bi, fi); /* 影子池登记 */
    xtd_changed(self);
}

void XTextDocument_setFragmentUnderline(XTextDocument* self, int bi, int fi, bool underline)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    self->m_blocks[bi].fragments[fi].fmt.underline = underline;
    xtd_fragPoolSync(self, bi, fi); /* 影子池登记 */
    xtd_changed(self);
}

void XTextDocument_setFragmentStrikeOut(XTextDocument* self, int bi, int fi, bool strikeOut)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    self->m_blocks[bi].fragments[fi].fmt.strikeOut = strikeOut;
    xtd_fragPoolSync(self, bi, fi); /* 影子池登记 */
    xtd_changed(self);
}

void XTextDocument_setFragmentFgColor(XTextDocument* self, int bi, int fi, uint32_t color)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    self->m_blocks[bi].fragments[fi].fmt.fgColor = color;
    xtd_fragPoolSync(self, bi, fi); /* 影子池登记 */
    xtd_changed(self);
}

void XTextDocument_setFragmentBgColor(XTextDocument* self, int bi, int fi, uint32_t color)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    self->m_blocks[bi].fragments[fi].fmt.bgColor = color;
    xtd_fragPoolSync(self, bi, fi); /* 影子池登记 */
    xtd_changed(self);
}

void XTextDocument_setFragmentFontFamily(XTextDocument* self, int bi, int fi, const char* family)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    if (!family) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    if (!self->m_blocks[bi].fragments[fi].fmt.fontFamily)
        self->m_blocks[bi].fragments[fi].fmt.fontFamily = XString_create();
    if (self->m_blocks[bi].fragments[fi].fmt.fontFamily)
        XString_assign_utf8(self->m_blocks[bi].fragments[fi].fmt.fontFamily,
                            family);
    xtd_fragPoolSync(self, bi, fi); /* 影子池登记 */
    xtd_changed(self);
}

void XTextDocument_setFragmentFontSize(XTextDocument* self, int bi, int fi, int size)
{
    if (!self || bi < 0 || bi >= self->m_blockCount) return;
    if (fi < 0 || fi >= self->m_blocks[bi].fragmentCount) return;
    xtd_saveSnapshot(self); /* 变更前快照入撤销栈。 */
    self->m_blocks[bi].fragments[fi].fmt.fontPointSize = size;
    xtd_fragPoolSync(self, bi, fi); /* 影子池登记 */
    xtd_changed(self);
}

/* ==================== 撤销/重做公共 API ==================== */

void XTextDocument_setCursorPosition(XTextDocument* self, int position)
{
    if (!self) return;
    if (position < 0) position = 0;
    if (self->m_cursorPosition == position) return;
    self->m_cursorPosition = position;
    xtd_emitVoid(self, (size_t)XTextDocument_cursorPositionChanged_signal);
}

int XTextDocument_cursorPosition(const XTextDocument* self)
{ return self ? self->m_cursorPosition : 0; }

/** @brief 词内字符判定（WholeWords 边界用）：ASCII 字母数字 +
 *         UTF-8 多字节序列（>=0x80 视为词内字符——CJK 词不误断）。 */
static bool xtd_findIsWordChar(char c)
{
    unsigned char u = (unsigned char)c;
    return (u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') ||
           (u >= 'a' && u <= 'z') || u >= 0x80;
}

/** @brief pos 处是否命中 needle（大小写与整词选项生效）。 */
static bool xtd_findMatchAt(const char* hay, int hayLen, int pos,
                            const char* needle, int needleLen, int flags)
{
    int i;
    if (pos < 0 || pos + needleLen > hayLen) return false;
    if (flags & (int)XTDFindFlag_CaseSensitively) {
        if (XStrncmp(hay + pos, needle, (size_t)needleLen) != 0) return false;
    } else {
        /* 默认大小写不敏感（Qt 同语义；ASCII 折叠，非 ASCII 原样）。 */
        for (i = 0; i < needleLen; ++i) {
            char a = hay[pos + i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) return false;
        }
    }
    if (flags & (int)XTDFindFlag_WholeWords) {
        /* 词边界：两侧均不得为词内字符（对标 Qt isLetterOrNumber 子集）。 */
        if (pos > 0 && xtd_findIsWordChar(hay[pos - 1])) return false;
        if (pos + needleLen < hayLen &&
            xtd_findIsWordChar(hay[pos + needleLen])) return false;
    }
    return true;
}

int XTextDocument_find_ex(const XTextDocument* self, const char* text,
                          int from, int flags)
{
    /* 便利口径：返回匹配起点（= findCursor 的 anchor）。 */
    return XTextDocument_findCursor(self, text, from, flags).anchor;
}

XTDTextCursor XTextDocument_findCursor(const XTextDocument* self,
                                       const char* text, int from, int flags)
{
    XTDTextCursor cursor;
    char* plain;
    int len;
    int needleLen;
    int start = -1;
    cursor.position = -1;
    cursor.anchor = -1;
    cursor.hasSelection = false;
    if (!self || !text || !text[0]) return cursor;
    plain = XTextDocument_toPlainText(self);
    if (!plain) return cursor;
    len = (int)XStrlen(plain);
    needleLen = (int)XStrlen(text);
    if (from < 0) from = 0;
    if (from > len) from = len;
    if (flags & (int)XTDFindFlag_Backward) {
        /* Qt 口径（本机 Qt 6.8 探针）：取「起点严格小于 from 的最后一个
         * 匹配」。探针 aXbXc：from=3 -> 1..2、from=4 -> 3..4；
         * Hello World hello：from=12 -> 0..5、from=13 -> 12..17。 */
        int s;
        int last = -1;
        for (s = 0; s < from && s + needleLen <= len; ++s) {
            if (xtd_findMatchAt(plain, len, s, text, needleLen, flags))
                last = s;
        }
        start = last;
    } else {
        int s;
        for (s = from; s + needleLen <= len; ++s) {
            if (xtd_findMatchAt(plain, len, s, text, needleLen, flags)) {
                start = s;
                break;
            }
        }
    }
    XFree_System(plain);
    if (start >= 0) {
        cursor.position = start + needleLen; /* 对标 Qt：position 落在匹配末尾。 */
        cursor.anchor = start;               /* 选区锚点=匹配起点。 */
        cursor.hasSelection = true;
    }
    return cursor;
}

int XTextDocument_find(const XTextDocument* self, const char* text)
{
    return XTextDocument_find_ex(self, text, 0, 0);
}

char XTextDocument_characterAt(const XTextDocument* self, int position)
{
    char* plain;
    char result;
    if (!self || position < 0) return '\0';
    plain = XTextDocument_toPlainText(self);
    if (!plain) return '\0';
    if (position >= (int)XStrlen(plain)) {
        XFree_System(plain);
        return '\0';
    }
    result = plain[position];
    XFree_System(plain);
    return result;
}

void XTextDocument_undo(XTextDocument* self)
{
    int top;
    int metaFrom;
    int metaRemoved;
    int metaAdded;
    int metaOpIns;
    int metaOpRem;
    if (!self || !self->m_undoRedoEnabled || self->m_undoTop == 0) return;
    top = self->m_undoTop - 1;
    metaFrom = self->m_undoMetaFrom[top];
    metaRemoved = self->m_undoMetaRemoved[top];
    metaAdded = self->m_undoMetaAdded[top];
    metaOpIns = (int)self->m_undoMetaOpIns[top];
    metaOpRem = (int)self->m_undoMetaOpRem[top];
    {
        char* snap = self->m_undoStack[top];
        self->m_undoStack[top] = NULL;
        self->m_undoTop = top;
        /* 重做侧压当前文本 + **同一**变更元数据（redo 重放同一变更，
         * 光标口径随命令——Qt undoRedo 的命令栈双向同构）。 */
        if (self->m_redoTop < XTD_MAX_UNDO) {
            int rtop = self->m_redoTop;
            self->m_redoStack[rtop] = XTextDocument_toPlainText(self);
            if (self->m_redoStack[rtop]) {
                self->m_redoMetaFrom[rtop] = metaFrom;
                self->m_redoMetaRemoved[rtop] = metaRemoved;
                self->m_redoMetaAdded[rtop] = metaAdded;
                self->m_redoMetaOpIns[rtop] = (uint8_t)metaOpIns;
                self->m_redoMetaOpRem[rtop] = (uint8_t)metaOpRem;
                self->m_redoTop = rtop + 1;
            }
        }
        xtd_restoreSnapshot(self, snap);
        XFree_System(snap);
        /* 活动光标按 Qt undoRedo 的命令序映射（qtextdocument_p.cpp:866-881
         * + qtextcursor.cpp:57-94）：undo 先摘新增（Inserted 命令的反向，
         * 口径 opIns）、再回填删除（Removed 命令的反向，口径 opRem）；
         * 探针 R1/R2（替换两方向）/P7/N1/N2 系全对上。元数据缺失（None）
         * 不动——旧行为。 */
        if (metaOpIns != (int)XTDUndoOp_None &&
            metaOpRem != (int)XTDUndoOp_None) {
            if (metaAdded > 0)
                xtd_adjustActiveCursor(self, metaFrom, -metaAdded, metaOpIns);
            if (metaRemoved > 0)
                xtd_adjustActiveCursor(self, metaFrom, metaRemoved, metaOpRem);
        }
        xtd_notifyUndoRedo(self);
    }
}

void XTextDocument_redo(XTextDocument* self)
{
    int top;
    int metaFrom;
    int metaRemoved;
    int metaAdded;
    int metaOpIns;
    int metaOpRem;
    if (!self || !self->m_undoRedoEnabled || self->m_redoTop == 0) return;
    top = self->m_redoTop - 1;
    metaFrom = self->m_redoMetaFrom[top];
    metaRemoved = self->m_redoMetaRemoved[top];
    metaAdded = self->m_redoMetaAdded[top];
    metaOpIns = (int)self->m_redoMetaOpIns[top];
    metaOpRem = (int)self->m_redoMetaOpRem[top];
    {
        char* snap = self->m_redoStack[top];
        self->m_redoStack[top] = NULL;
        self->m_redoTop = top;
        /* 撤销侧回压当前文本 + 同一元数据（对称口径）。 */
        if (self->m_undoTop < XTD_MAX_UNDO) {
            int utop = self->m_undoTop;
            self->m_undoStack[utop] = XTextDocument_toPlainText(self);
            if (self->m_undoStack[utop]) {
                self->m_undoMetaFrom[utop] = metaFrom;
                self->m_undoMetaRemoved[utop] = metaRemoved;
                self->m_undoMetaAdded[utop] = metaAdded;
                self->m_undoMetaOpIns[utop] = (uint8_t)metaOpIns;
                self->m_undoMetaOpRem[utop] = (uint8_t)metaOpRem;
                self->m_undoTop = utop + 1;
            }
        }
        xtd_restoreSnapshot(self, snap);
        XFree_System(snap);
        /* redo 命令序（Qt undoRedo(false) 正向重放已翻转的命令栈）：栈首
         * 原 Removed 命令（undo 已翻成 Inserted）→ redo 再**摘除**回填的
         * 文本：adjust(-removed, opRem)；栈尾原 Inserted（已翻成 Removed）
         * → redo 再**回插**：adjust(+added, opIns)。探针 R6 afterRedo=1
         * （undo 2 → redo 1）即由第一支兑现。 */
        if (metaOpIns != (int)XTDUndoOp_None &&
            metaOpRem != (int)XTDUndoOp_None) {
            if (metaRemoved > 0)
                xtd_adjustActiveCursor(self, metaFrom, -metaRemoved, metaOpRem);
            if (metaAdded > 0)
                xtd_adjustActiveCursor(self, metaFrom, metaAdded, metaOpIns);
        }
        xtd_notifyUndoRedo(self);
    }
}

/* ==================== 修改状态（对标 modified 属性族） ==================== */

bool XTextDocument_isModified(const XTextDocument* self)
{ return self ? self->m_modifiedFlag : false; }

void XTextDocument_setModified(XTextDocument* self, bool m)
{
    /* 对标 QTextDocumentPrivate::setModified：仅状态翻转时发射
     * modificationChanged；复位即记撤销基准（快照制无基准——简化）。 */
    if (!self || m == self->m_modifiedFlag) return;
    self->m_modifiedFlag = m;
    xtd_emitBool(self, (size_t)XTextDocument_modificationChanged_signal, m);
}

int XTextDocument_revision(const XTextDocument* self)
{ return self ? self->m_modified : 0; }

int XTextDocument_lineCount(const XTextDocument* self)
{
    /* 子集无换行布局（无折行）：每块即一行。 */
    return self ? self->m_blockCount : 0;
}

/* ==================== 块查询（对标 findBlock 族） ==================== */

/** @brief 单块字符数（不含块分隔符；空片段安全）。 */
static int xtd_blockCharCount(const XTDBlock* blk)
{
    int j;
    int total = 0;
    if (!blk) return 0;
    for (j = 0; j < blk->fragmentCount; ++j)
        total += (int)XStrlen(xtd_fragText(&blk->fragments[j]));
    return total;
}

const XTDBlock* XTextDocument_firstBlock(const XTextDocument* self)
{
    if (!self || !self->m_blocks || self->m_blockCount <= 0) return NULL;
    return &self->m_blocks[0];
}

const XTDBlock* XTextDocument_lastBlock(const XTextDocument* self)
{
    if (!self || !self->m_blocks || self->m_blockCount <= 0) return NULL;
    return &self->m_blocks[self->m_blockCount - 1];
}

const XTDBlock* XTextDocument_begin(const XTextDocument* self)
{
    /* 对标 QTextDocument::begin()：迭代起点即首块（qtextdocument.h 的
       begin() 与 firstBlock() 同值；本库块借用指针即 QTextBlock 承载，
       故直接转发）。 */
    return XTextDocument_firstBlock(self);
}

const XTDBlock* XTextDocument_end(const XTextDocument* self)
{
    /* 对标 QTextDocument::end()：返回 isValid()==false 的过尾块。本库以
       NULL 表达无效块 ⇒ 恒返回 NULL（头文件契约「NULL = 过尾哨兵」）。
       形参保留以对齐 Qt 的成员函数形态，本实现不读取。 */
    (void)self;
    return NULL;
}

const XTDBlock* XTextDocument_findBlockByNumber(const XTextDocument* self, int blockNumber)
{
    if (!self || !self->m_blocks || blockNumber < 0 ||
        blockNumber >= self->m_blockCount) return NULL;
    return &self->m_blocks[blockNumber];
}

const XTDBlock* XTextDocument_findBlockByLineNumber(const XTextDocument* self, int lineNumber)
{
    /* 子集无换行布局：每块即一行，行号与块号一一对应。 */
    return XTextDocument_findBlockByNumber(self, lineNumber);
}

const XTDBlock* XTextDocument_findBlock(const XTextDocument* self, int position)
{
    int i;
    int acc = 0;
    if (!self || !self->m_blocks || self->m_blockCount <= 0) return NULL;
    if (position < 0) return XTextDocument_firstBlock(self);
    for (i = 0; i < self->m_blockCount; ++i) {
        int n = xtd_blockCharCount(&self->m_blocks[i]);
        if (position < acc + n) return &self->m_blocks[i];
        acc += n;
    }
    /* 越过文档尾：钳到末块（Qt 光标语义）。 */
    return XTextDocument_lastBlock(self);
}

/* ==================== 块数上限（对标 maximumBlockCount） ==================== */

int XTextDocument_maximumBlockCount(const XTextDocument* self)
{ return self ? self->m_maximumBlockCount : 0; }

void XTextDocument_setMaximumBlockCount(XTextDocument* self, int maximum)
{
    int trimmed;
    if (!self) return;
    /* Qt 6.8（qtextdocument.cpp:518-524；本机探针 setMaximumBlockCount(-4)
     * 后 maximumBlockCount()==-4、undoRedoEnabled()==0）：值原样保存
     * （负值不按不限折叠），>0 才裁剪旧块，撤销/重做**无条件**关闭。 */
    self->m_maximumBlockCount = maximum;
    /* 上限立即生效（超出从文档开头移除，Qt 同语义）。裁剪本身是独立编辑块：
     * 精确区间 (0, 被裁字符数, 0)（Qt 探针：3 块裁到 2 ⇒ contentsChange(0,2,0)
     * 后 contentsChanged 再 blockCountChanged(2)）。**没有裁剪就不发任何
     * 内容信号**（Qt 同口径，探针在未超限时零信号）。 */
    trimmed = xtd_enforceMaximumBlockCount(self);
    if (trimmed > 0) {
        xtd_setPreciseChange(self, 0, trimmed, 0);
        xtd_changedCore(self, false);
        self->m_changeValid = 0u;
    }
    /* 设置上限同时禁用撤销/重做历史（Qt setMaximumBlockCount 同语义）。 */
    XTextDocument_setUndoRedoEnabled(self, false);
}

/* ==================== baseUrl 属性 ==================== */

const char* XTextDocument_baseUrl(const XTextDocument* self)
{
    const char* text;
    if (!self || !self->m_baseUrl) return "";
    text = XString_toUtf8(self->m_baseUrl);
    return text ? text : "";
}

void XTextDocument_setBaseUrl(XTextDocument* self, const char* url)
{
    const char* cur;
    if (!self || !url) return;
    if (!self->m_baseUrl) {
        self->m_baseUrl = XString_create();
        if (!self->m_baseUrl) return;
    }
    /* 对标 Qt setBaseUrl：值未变化不发射。 */
    cur = XString_toUtf8(self->m_baseUrl);
    if (cur && XStrcmp(cur, url) == 0) return;
    XString_assign_utf8(self->m_baseUrl, url);
    xtd_emitVoid(self, (size_t)XTextDocument_baseUrlChanged_signal);
}

/* ==================== 文档级标量属性（对标 qtextdocument.h:231-268） ==== */

double XTextDocument_documentMargin(const XTextDocument* self)
{ return self ? self->m_documentMargin : 4.0; }

void XTextDocument_setDocumentMargin(XTextDocument* self, double margin)
{
    /* Qt 原样保存（探针 setDocumentMargin(-3) 后返回 -3）。 */
    if (self) self->m_documentMargin = margin;
}

double XTextDocument_indentWidth(const XTextDocument* self)
{ return self ? self->m_indentWidth : 40.0; }

void XTextDocument_setIndentWidth(XTextDocument* self, double width)
{
    /* Qt 原样保存（探针 setIndentWidth(-1) 后返回 -1）。 */
    if (self) self->m_indentWidth = width;
}

double XTextDocument_textWidth(const XTextDocument* self)
{ return self ? self->m_textWidth : -1.0; }

void XTextDocument_setTextWidth(XTextDocument* self, double width)
{
    if (!self) return;
    /* Qt setTextWidth：pageSize = (width, -1)（qtextdocument.cpp
     * setTextWidth：sz.setWidth(width); sz.setHeight(-1); setPageSize(sz);
     * 本机探针 setTextWidth(120) 后 pageSize==120x-1）。宽度即 textWidth
     * 的读数（同函数 textWidth() 返回 pageSize.width）。 */
    self->m_textWidth = width;
    self->m_pageHeight = -1.0;
}

bool XTextDocument_useDesignMetrics(const XTextDocument* self)
{
    /* Qt：useDesignMetrics() 返回 defaultTextOption.useDesignMetrics()
     * （qtextdocument.cpp），单源于 defaultTextOption。 */
    return self ? self->m_textOption.m_useDesignMetrics != 0 : false;
}

void XTextDocument_setUseDesignMetrics(XTextDocument* self, bool enable)
{
    int flag = enable ? 1 : 0;
    if (!self) return;
    /* Qt setUseDesignMetrics：同值早退后写入 defaultTextOption
     * （qtextdocument.cpp setUseDesignMetrics）。 */
    if (self->m_textOption.m_useDesignMetrics == flag) return;
    self->m_textOption.m_useDesignMetrics = flag;
}

bool XTextDocument_isLayoutEnabled(const XTextDocument* self)
{ return self ? self->m_layoutEnabled != 0u : false; }

void XTextDocument_setLayoutEnabled(XTextDocument* self, bool enable)
{ if (self) self->m_layoutEnabled = enable ? 1u : 0u; }

const char* XTextDocument_defaultStyleSheet(const XTextDocument* self)
{
    const char* text;
    if (!self || !self->m_defaultStyleSheet) return "";
    text = XString_toUtf8(self->m_defaultStyleSheet);
    return text ? text : "";
}

void XTextDocument_setDefaultStyleSheet(XTextDocument* self, const char* sheet)
{
    if (!self) return;
    if (!self->m_defaultStyleSheet)
        self->m_defaultStyleSheet = XString_create();
    if (self->m_defaultStyleSheet)
        XString_assign_utf8(self->m_defaultStyleSheet, sheet ? sheet : "");
}

int XTextDocument_defaultCursorMoveStyle(const XTextDocument* self)
{ return self ? self->m_defaultCursorMoveStyle : 0; }

void XTextDocument_setDefaultCursorMoveStyle(XTextDocument* self, int style)
{ if (self) self->m_defaultCursorMoveStyle = style; }

/* ==================== §B16 有界子集：页面尺寸 / 默认文本选项 / 基线偏移 === */

XSizeF XTextDocument_pageSize(const XTextDocument* self)
{
    XSizeF size;
    size.width = (float)(self ? self->m_textWidth : -1.0);
    size.height = (float)(self ? self->m_pageHeight : -1.0);
    return size;
}

void XTextDocument_setPageSize(XTextDocument* self, XSizeF size)
{
    if (!self) return;
    /* Qt setPageSize：只改 d->pageSize，宽度同时是 textWidth 的读数
     * （本机 Qt 6.8.0 探针：setPageSize(300,400) 后 textWidth()==300、
     * pageCount 仍由布局决定）；无信号（Qt 仅在有布局时 relayout）。 */
    self->m_textWidth = (double)size.width;
    self->m_pageHeight = (double)size.height;
}

XTextOption XTextDocument_defaultTextOption(const XTextDocument* self)
{
    if (!self) return XTextOption_create();
    return self->m_textOption;
}

void XTextDocument_setDefaultTextOption(XTextDocument* self,
                                        const XTextOption* option)
{
    if (!self || !option) return;
    /* Qt setDefaultTextOption：整值替换 d->defaultTextOption（本机探针：
     * 设入 tab=-1 的裸 QTextOption 后文档选项 tab=-1，无字段合并）；
     * 无信号（Qt 仅在有布局时 relayout）。 */
    self->m_textOption = *option;
}

double XTextDocument_superScriptBaseline(const XTextDocument* self)
{
    return self ? self->m_superScriptBaseline : 50.0;
}

void XTextDocument_setSuperScriptBaseline(XTextDocument* self, double baseline)
{
    /* Qt setSuperScriptBaseline：写 defaultTextFormat 属性，无信号；
     * 本库把文档级缺省基线收成三个标量（XTDCharFormat 为片段级内联结构，
     * 不承载基线百分比——有界子集登记）。 */
    if (self) self->m_superScriptBaseline = baseline;
}

double XTextDocument_subScriptBaseline(const XTextDocument* self)
{
    return self ? self->m_subScriptBaseline : 100.0 / 6.0;
}

void XTextDocument_setSubScriptBaseline(XTextDocument* self, double baseline)
{
    if (self) self->m_subScriptBaseline = baseline;
}

double XTextDocument_baselineOffset(const XTextDocument* self)
{
    return self ? self->m_baselineOffset : 0.0;
}

void XTextDocument_setBaselineOffset(XTextDocument* self, double offset)
{
    if (self) self->m_baselineOffset = offset;
}

/* ==================== 按名资源表（对标 resource/addResource） ==================== */

/** @brief 释放单个资源条目载荷（不移动数组）。 */
static void xtd_resourceEntryClear(XTDResource* res)
{
    if (!res) return;
    if (res->name) {
        XClassDelete(res->name);
        res->name = NULL;
    }
    if (res->text) {
        XClassDelete(res->text);
        res->text = NULL;
    }
    if (res->image) {
        XClassDelete((XClass*)res->image);
        res->image = NULL;
    }
    res->type = 0;
}

void XTextDocument_addResource(XTextDocument* self, int type,
                               const char* name, const void* resource)
{
    XTDResource* slot = NULL;
    int i;
    if (!self || !name || !name[0] ||
        type == (int)XTDResourceType_UnknownResource) return;
    /* 同 type+name 已存在则替换（对标 QMap 覆盖语义）。 */
    for (i = 0; i < self->m_resourceCount; ++i) {
        XTDResource* res = &self->m_resources[i];
        const char* rn = res->name ? XString_toUtf8(res->name) : NULL;
        if (res->type == type && rn && XStrcmp(rn, name) == 0) {
            if (!resource) {
                /* NULL 载荷：移除条目（压缩数组）。 */
                xtd_resourceEntryClear(res);
                for (; i < self->m_resourceCount - 1; ++i)
                    self->m_resources[i] = self->m_resources[i + 1];
                XMemset(&self->m_resources[self->m_resourceCount - 1], 0,
                        sizeof(XTDResource));
                self->m_resourceCount--;
            } else {
                slot = res;
                xtd_resourceEntryClear(slot);
                slot->type = type;
            }
            break;
        }
    }
    if (!resource) return;
    if (!slot) {
        if (self->m_resourceCount >= XTD_MAX_RESOURCES) return; /* 表满：忽略。 */
        slot = &self->m_resources[self->m_resourceCount++];
        XMemset(slot, 0, sizeof(XTDResource));
        slot->type = type;
    }
    slot->name = XString_create_utf8(name);
    if (type == (int)XTDResourceType_ImageResource) {
        const XImage* img = (const XImage*)resource;
        if (XImage_width(img) > 0 && XImage_height(img) > 0) {
            XRect full;
            slot->image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            if (slot->image) {
                XRect_init(&full, 0, 0, XImage_width(img), XImage_height(img));
                XImage_copyRect(img, &full, slot->image);
                if (XImage_isNull(slot->image)) {
                    XClassDelete((XClass*)slot->image);
                    slot->image = NULL;
                }
            }
        }
    } else {
        slot->text = XString_create_utf8((const char*)resource);
    }
    /* 载荷失败：条目仅剩 name 的残槽不判定命中（resource 查询按
     * text/image 非空返回），clear/析构统一回收。 */
}

/** @brief 进程级默认资源提供者（对标 static defaultResourceProvider；
 *         进程级单例，借用回调用同一 userData，本层不拥有）。 */
static XTDResourceProviderFunc s_defaultResourceProvider = NULL;
static void* s_defaultResourceProviderUserData = NULL;

const void* XTextDocument_resource(const XTextDocument* self, int type,
                                   const char* name)
{
    int i;
    const void* provided;
    if (!self || !name || !name[0]) return NULL;
    for (i = 0; i < self->m_resourceCount; ++i) {
        const XTDResource* res = &self->m_resources[i];
        const char* rn = res->name ? XString_toUtf8(res->name) : NULL;
        if (res->type == type && rn && XStrcmp(rn, name) == 0) {
            if (type == (int)XTDResourceType_ImageResource)
                return res->image ? (const void*)res->image : NULL;
            return res->text ? (const void*)XString_toUtf8(res->text) : NULL;
        }
    }
    /* 查表未命中 ⇒ 回落资源提供者（对标 QTextDocument::resource，
       qtextdocument.cpp:2156-2165）：
       `if (d->resourceProvider) r = d->resourceProvider(url);
        else if (auto defaultProvider = defaultResourceProvider()) ...`
       ——文档级提供者存在时**不再问**进程级默认提供者（哪怕它回了空值）；
       只有本文档没设提供者才落到默认提供者。提供者返回的镜像/文本只是
       **借用**视图：不写入资源表、不转移所有权（见 XTDResourceProviderFunc）。 */
    if (self->m_resourceProvider) {
        provided = self->m_resourceProvider(
            self->m_resourceProviderUserData, type, name);
    } else if (s_defaultResourceProvider) {
        provided = s_defaultResourceProvider(s_defaultResourceProviderUserData,
                                             type, name);
    } else {
        provided = NULL;
    }
    return provided;
}

/* ==================== 资源提供者（对标 resourceProvider 族） ==================== */

XTDResourceProviderFunc XTextDocument_resourceProvider(
    const XTextDocument* self, void** outUserData)
{
    if (outUserData) *outUserData = NULL;
    if (!self || !self->m_resourceProvider) return NULL;
    if (outUserData) *outUserData = self->m_resourceProviderUserData;
    return self->m_resourceProvider;
}

void XTextDocument_setResourceProvider(XTextDocument* self,
                                       XTDResourceProviderFunc provider,
                                       void* userData)
{
    if (!self) return;
    /* Qt 的 setResourceProvider 同值不重设；C 侧以函数指针+userData 对
       比较（同值早退避免无意义的状态变更；本属性无 NOTIFY 信号）。 */
    if (self->m_resourceProvider == provider &&
        self->m_resourceProviderUserData == userData)
        return;
    self->m_resourceProvider = provider;
    self->m_resourceProviderUserData = userData;
}

XTDResourceProviderFunc XTextDocument_defaultResourceProvider(
    void** outUserData)
{
    if (outUserData) *outUserData = s_defaultResourceProviderUserData;
    return s_defaultResourceProvider;
}

void XTextDocument_setDefaultResourceProvider(XTDResourceProviderFunc provider,
                                              void* userData)
{
    s_defaultResourceProvider = provider;
    s_defaultResourceProviderUserData = userData;
}

/* ==================== 深拷贝（对标 clone） ==================== */

XTextDocument* XTextDocument_clone(const XTextDocument* self)
{
    XTextDocument* doc;
    int i;
    int j;
    if (!self) return NULL;
    doc = XTextDocument_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!doc) return NULL;
    /* 块+片段深拷贝（含图片深拷贝与块级格式字段）。 */
    for (i = 0; i < self->m_blockCount; ++i) {
        const XTDBlock* src = &self->m_blocks[i];
        XTDBlock* dst;
        xtd_ensureCapacity(doc, i + 1);
        if (i >= doc->m_capacity) break; /* 扩容失败：截断。 */
        if (i >= doc->m_blockCount) doc->m_blockCount = i + 1;
        dst = &doc->m_blocks[i];
        XMemset(dst, 0, sizeof(XTDBlock));
        dst->alignment = src->alignment;
        dst->indentLevel = src->indentLevel;
        dst->isListItem = src->isListItem;
        dst->isOrdered = src->isOrdered;
        dst->listFresh = src->listFresh;
        /* 列表格式三属性随深拷贝继承（clone 必须逐字段同值）。 */
        dst->listStyleCode = src->listStyleCode;
        dst->listStartSet = src->listStartSet;
        dst->listSuffixParen = src->listSuffixParen;
        dst->listStart = src->listStart;
        dst->headingLevel = src->headingLevel;
        if (src->blockFormat)
            dst->blockFormat = XString_create_copy(src->blockFormat);
        for (j = 0; j < src->fragmentCount; ++j) {
            const XTDFragment* sf = &src->fragments[j];
            XTDFragment* df = &dst->fragments[j];
            XMemset(df, 0, sizeof(XTDFragment));
            df->m_fmtPoolIndex = -1; /* 未入池哨兵；循环后统一重建 */
            xtd_formatAssign(&df->fmt, &sf->fmt);
            if (sf->text) df->text = XString_create_copy(sf->text);
            if (sf->image) {
                XRect full;
                df->image = XImage_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
                if (df->image) {
                    XRect_init(&full, 0, 0, XImage_width(sf->image),
                               XImage_height(sf->image));
                    XImage_copyRect(sf->image, &full, df->image);
                    if (XImage_isNull(df->image)) {
                        XClassDelete((XClass*)df->image);
                        df->image = NULL;
                    }
                }
            }
            dst->fragmentCount = j + 1;
        }
    }
    /* 元信息/默认格式/资源表（对标 Qt clone：title/url/cssMedia/
     * resources/defaultFont；baseUrl 与 frontMatter 不拷贝——Qt 6.8
     * clone 同口径）。 */
    XTextDocument_setMetaInformation(doc, (int)XTDMetaInformation_DocumentTitle,
                                     XTextDocument_metaInformation(
                                         self, (int)XTDMetaInformation_DocumentTitle));
    XTextDocument_setMetaInformation(doc, (int)XTDMetaInformation_DocumentUrl,
                                     XTextDocument_metaInformation(
                                         self, (int)XTDMetaInformation_DocumentUrl));
    XTextDocument_setMetaInformation(doc, (int)XTDMetaInformation_CssMedia,
                                     XTextDocument_metaInformation(
                                         self, (int)XTDMetaInformation_CssMedia));
    xtd_formatAssign(&doc->m_defaultFormat, &self->m_defaultFormat);
    for (i = 0; i < self->m_resourceCount; ++i) {
        const XTDResource* res = &self->m_resources[i];
        const char* rn = res->name ? XString_toUtf8(res->name) : NULL;
        if (!rn) continue;
        if (res->image)
            XTextDocument_addResource(doc, res->type, rn, res->image);
        else if (res->text)
            XTextDocument_addResource(doc, res->type, rn,
                                      XString_toUtf8(res->text));
    }
    /* 文档级标量属性随 clone 复制（Qt clone 复制文档属性；撤销历史与
     * baseUrl 不复制同 Qt）。pageSize 与 defaultTextOption 也随 clone
     * 复制（qtextdocument.cpp clone：priv->pageSize/priv->defaultTextOption；
     * 本机 Qt 6.8.0 探针：clone 后 pageSize(100,50) 与选项 design/tab/wrap
     * 原样带过去）。**基线三属性不复制**（Qt clone 只回填 defaultFont，
     * defaultTextFormat 的基线属性随 formats 集合重置为本类缺省——本机
     * 探针：源 70/20/-5，clone 50/16.6667/0）。 */
    doc->m_documentMargin = self->m_documentMargin;
    doc->m_indentWidth = self->m_indentWidth;
    doc->m_textWidth = self->m_textWidth;
    doc->m_pageHeight = self->m_pageHeight;
    doc->m_textOption = self->m_textOption;
    doc->m_layoutEnabled = self->m_layoutEnabled;
    doc->m_defaultCursorMoveStyle = self->m_defaultCursorMoveStyle;
    if (self->m_defaultStyleSheet && doc->m_defaultStyleSheet) {
        XString_assign(doc->m_defaultStyleSheet, self->m_defaultStyleSheet);
    }
    doc->m_maximumBlockCount = self->m_maximumBlockCount;
    /* 副本内容由上面的块/片段拷贝直接落盘（不经 xtd_changed 漏斗）⇒ 把
     * contentsChange/blockCountChanged 的去重与汇总基准同步为副本当前内容：
     * 否则副本第一次变更会拿初始的 0/1 当基准（全量汇总口径 removed=0、
     * 或对未变化的块数误发 blockCountChanged）。 */
    doc->m_emittedCharCount = XTextDocument_characterCount(doc);
    doc->m_emittedBlockCount = doc->m_blockCount;
    /* 格式池重建（Qt clone 亦经 formatCollection 重登记副本格式）。 */
    xtd_poolRebuild(doc);
    return doc;
}

/* ==================== 命名空间函数子集（对标 Qt::mightBeRichText/
 *                    Qt::convertFromPlainText） ==================== */

/** @brief 已知 HTML 元素名表（mightBeRichText 判定用：本库解析子集 +
 *         常用块/表格/元数据元素，对标 QTextHtmlParser::lookupElement
 *         的常量子集）。 */
static const char* const xtd_htmlElementNames[] = {
    "html", "body", "head", "title", "meta", "style", "script", "link",
    "p", "div", "br", "hr", "span", "font", "a", "img",
    "b", "strong", "i", "em", "u", "s", "strike", "del", "ins",
    "sub", "sup", "small", "big", "code", "tt", "kbd", "samp",
    "var", "cite", "q", "dfn", "abbr", "nobr", "center",
    "h1", "h2", "h3", "h4", "h5", "h6",
    "ul", "ol", "li", "dl", "dt", "dd", "pre", "blockquote",
    "table", "thead", "tbody", "tfoot", "tr", "td", "th", "caption"
};

bool XTextDocument_mightBeRichText(const char* text)
{
    const char* p = text;
    /* 启发式子集（对标 mightBeRichTextImpl）：跳过行首空白与
     * <?xml ...?> 前缀；"<!doc" 判真；首个 '\n' 前找 '<'，'<..>' 之间
     * 为纯字母数字且命中元素名表判真；"&lt;" 实体判真。 */
    if (!p || !p[0]) return false;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    if (XStrncmp(p, "<?xml", 5) == 0) {
        while (*p) {
            if (p[0] == '?' && p[1] == '>') { p += 2; break; }
            ++p;
        }
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
    }
    if (p[0] == '<' && p[1] == '!') {
        /* 大小写不敏感的 "<!doc" 前缀判定（对标 text.mid(start,5)
         * .compare("<!doc", CaseInsensitive)）。 */
        static const char prefix[5] = { '<', '!', 'd', 'o', 'c' };
        int i;
        bool hit = true;
        for (i = 0; i < 5; ++i) {
            char c = p[i];
            char d = prefix[i];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c != d) { hit = false; break; }
        }
        if (hit) return true;
    }
    {
        const char* q = p;
        while (*q && *q != '<' && *q != '\n') {
            if (q[0] == '&' && XStrncmp(q + 1, "lt;", 3) == 0)
                return true; /* 用户力图显示 '<' 的转义文本。 */
            ++q;
        }
        if (*q == '<') {
            const char* close = XStrchr(q, '>');
            if (close) {
                bool haveTag = false;
                bool isTag = true;
                const char* r;
                for (r = q + 1; r < close; ++r) {
                    char c = *r;
                    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                        (c >= 'a' && c <= 'z')) {
                        haveTag = true;
                    } else if (haveTag && (c == ' ' || c == '\t')) {
                        break; /* 标签名后跟属性：名字已收齐。 */
                    } else if (haveTag && c == '/' && r + 1 == close) {
                        break; /* 自闭合尾斜杠。 */
                    } else if (c != ' ' && c != '\t' && c != '!') {
                        isTag = false; /* 非标签形态。 */
                        break;
                    }
                }
                if (isTag && haveTag) {
                    int i;
                    char name[16];
                    size_t n = 0;
                    for (r = q + 1; r < close && n + 1 < sizeof(name); ++r) {
                        char c = *r;
                        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                            (c >= 'a' && c <= 'z')) {
                            if (c >= 'A' && c <= 'Z')
                                c = (char)(c - 'A' + 'a');
                            name[n++] = c;
                        } else if (n > 0) {
                            break;
                        }
                    }
                    name[n] = '\0';
                    if (n > 0) {
                        for (i = 0;
                             i < (int)(sizeof(xtd_htmlElementNames) /
                                       sizeof(xtd_htmlElementNames[0]));
                             ++i) {
                            if (XStrcmp(name, xtd_htmlElementNames[i]) == 0)
                                return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

/** @brief convertFromPlainText 输出槽（counting=true 仅统计字节数）。 */
typedef struct XTDCfptBuffer
{
    size_t size;  /**< 目标总字节数（含 NUL）。 */
    size_t pos;   /**< 已写字节数。 */
    char* data;   /**< 写入目标；NULL=仅统计。 */
} XTDCfptBuffer;

static void xtd_cfptPut(XTDCfptBuffer* b, const char* s, size_t n)
{
    if (!b) return;
    if (!b->data) { b->size += n; return; }
    if (b->pos + n > b->size - 1) n = b->size - 1 - b->pos; /* 防御钳位。 */
    if (n > 0) {
        XMemcpy(b->data + b->pos, s, n);
        b->pos += n;
    }
}

static void xtd_cfptPutStr(XTDCfptBuffer* b, const char* s)
{
    if (s) xtd_cfptPut(b, s, XStrlen(s));
}

char* XTextDocument_convertFromPlainText(const char* plain, int mode)
{
    /* 逐字符移植 Qt::convertFromPlainText（两遍式：先统计后填充）：
     * "<p>" 起；'\n' 单个转 "<br>\n"、连串折叠为段落分隔；Pre 模式
     * 空白以 &nbsp; 承载、制表符展开到 8 列（与 Qt 同口径的列计数，
     * 含制表符二次 ++col 的既有行为）；& < > 转义；col!=0 时 "</p>" 收尾。 */
    XTDCfptBuffer buf;
    int pass;
    if (!plain) return NULL;
    buf.size = 0;
    buf.pos = 0;
    buf.data = NULL;
    for (pass = 0; pass < 2; ++pass) {
        int col = 0;
        const char* p;
        if (pass == 1) {
            buf.size += 1; /* NUL。 */
            buf.data = (char*)XMalloc_System(buf.size);
            if (!buf.data) return NULL;
            buf.pos = 0;
        }
        xtd_cfptPutStr(&buf, "<p>");
        p = plain;
        while (*p) {
            if (*p == '\n') {
                int c = 1;
                while (p[1] == '\n') { ++p; ++c; }
                if (c == 1) {
                    xtd_cfptPutStr(&buf, "<br>\n");
                } else {
                    xtd_cfptPutStr(&buf, "</p>\n");
                    while (--c > 1) xtd_cfptPutStr(&buf, "<br>\n");
                    xtd_cfptPutStr(&buf, "<p>");
                }
                col = 0;
            } else {
                if (mode == (int)XTDWhiteSpaceMode_Pre && *p == '\t') {
                    xtd_cfptPutStr(&buf, "&nbsp;");
                    ++col;
                    while (col % 8) {
                        xtd_cfptPutStr(&buf, "&nbsp;");
                        ++col;
                    }
                } else if (mode == (int)XTDWhiteSpaceMode_Pre &&
                           (*p == ' ' || *p == '\r' || *p == '\v' ||
                            *p == '\f')) {
                    xtd_cfptPutStr(&buf, "&nbsp;");
                } else if (*p == '<') {
                    xtd_cfptPutStr(&buf, "&lt;");
                } else if (*p == '>') {
                    xtd_cfptPutStr(&buf, "&gt;");
                } else if (*p == '&') {
                    xtd_cfptPutStr(&buf, "&amp;");
                } else {
                    /* 非 ASCII UTF-8 序列整段透传（多字节不拆散）。 */
                    int seq = xtd_utf8SeqLen(p);
                    xtd_cfptPut(&buf, p, (size_t)seq);
                    p += seq - 1;
                }
                ++col;
            }
            ++p;
        }
        if (col != 0) xtd_cfptPutStr(&buf, "</p>");
    }
    buf.data[buf.pos < buf.size ? buf.pos : buf.size - 1] = '\0';
    return buf.data;
}

#endif /* XTEXTDOCUMENT_ON */

#endif /* XGUI_ON */
