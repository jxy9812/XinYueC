/**
 * @file       XTextDocumentLayout.c
 * @brief      XTextDocumentLayout 文档打印分页布局器实现（对标 Qt 6.8
 *             QAbstractTextDocumentLayout + QTextDocument::print 分页
 *             子集；行为对齐依据见同名头文件 @details，探针报告
 *             /tmp/w3b_probe/report2.txt）。
 * @author     XinYueC 团队
 */

#if XGUI_ON

#include "XTextDocumentLayout.h"

#if XTEXTDOCUMENT_ON

#include "XMemory.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XPainter.h"
#include "XTextUtf8.h"

#include <string.h>

/* ==================== 常量 ==================== */

/* 水平对齐掩码（XTDAlignment 水平子集 = Qt::AlignHorizontal_Mask 0x000F；
 * XTextLayout.c 同值常量本地承载——该文件归 Text 域静态符号不外引）。 */
#define XTDL_ALIGN_HORIZONTAL_MASK 0x000F
/* 片段绘制文本的栈缓冲（超出降级堆；XTextEdit.c xte_paintFrag 同款）。 */
#define XTDL_SEG_STACK_BUF 512
/* 页眉/页脚占位展开输出上限（超出截断——页眉页脚是单行装饰文本）。 */
#define XTDL_HF_MAX 256
/* 缺省前景（黑；XTextEdit 片段缺省 fg 同款）。 */
#define XTDL_DEFAULT_FG 0xFF000000u

/** @brief 反初始化虚槽前置声明（class_init 槽位引用；XTextLayout 同款）。 */
static void VXTextDocumentLayout_deinit(XTextDocumentLayout* self);

/* ==================== 块布局上下文 ==================== */

/**
 * @brief      单块的布局上下文（常驻至下次重建；头文件以 opaque 前置
 *             声明承载公开字段 m_blockLayouts）。
 * @details    m_text = 块各文本片段 toUtf8 顺序拼接（图片片段无文本、
 *             不占字节）；m_fragOff[i] = 片段 i 首字节（末片段尾 = 文本
 *             长）——绘制时把行字节区间映射回片段的依据。
 */
typedef struct XtdlBlockCtx
{
    XTextLayout* m_lay;   /**< 块布局器（create_ex 堆实例，拥有）。 */
    char* m_text;         /**< 块拼接文本 UTF-8（拥有）。 */
    int* m_fragOff;       /**< 片段起始字节表（拥有；条目 = 片段数）。 */
    int m_fragCount;      /**< 片段数（= m_fragOff 条目）。 */
    int m_textLen;        /**< 块拼接文本字节长（不含 NUL）。 */
} XtdlBlockCtx;

/* ==================== 片段字体（XTextEdit 同口径） ==================== */

/**
 * @brief      片段生效字体：基础字体 + 内联字号/斜体修饰。
 * @details    fontPointSize > 0 时按框架唯一换算点转像素字号（对标
 *             QTextCharFormat 字号；XTextEdit.c xte_fragPixelSize 同款
 *             ——上下标缩放不在此做，打印子集不消费上/下标基线偏移）。
 *             粗体是**伪粗体**（绘制段 +1px 双描边，位图/轮廓字库无
 *             粗体字形时的加粗近似，XTextEdit 同款），不落字体。
 * @param      out 输出字体（可为未 init 栈壳——本函数先 XFont_init 给
 *             生命周期再 XClassCopy（XTextLayout_font 同款配对）；**不
 *             init 直拷会炸**：未初始化栈壳的 vtable 是随机垃圾，
 *             VXFont_copy 判「已有生命周期」走释放旧串路径（2026-11-16
 *             XTextPrint.drawContinuousAndFragments 段错误实锅，
 *             XClass_delete_base 释放野指针 0x3d0910）。
 */
static void xtdl_fragFont(const XFont* base, const XTDFragment* f,
                          XFont* out)
{
    if (!out) return;
    XFont_init(out);
    if (!base) return;
    XClassCopy(out, (const XClass*)base);
    if (f && f->fmt.fontPointSize > 0) {
        int px = XPainter_fontPixelSize(f->fmt.fontPointSize);
        if (px > 0) XFont_setPixelSize(out, px);
    }
    if (f && f->fmt.italic) XFont_setItalic(out, true);
}

/* ==================== 页眉页脚占位展开 ==================== */

/**
 * @brief      展开页眉/页脚文本占位（%p=当前页号 1 起、%P=总页数）。
 * @param      dst 输出缓冲（容量 XTDL_HF_MAX+1；NULL 忽略）。
 * @param      src 模板文本（借用；NULL 按空串）。
 * @param      page 当前页号（0 起；展开为 page+1）。
 * @param      pageCount 总页数（>=1）。
 * @return     dst（方便链式取 strlen；dst NULL 返回 NULL）。
 */
static char* xtdl_expandPlaceholders(char* dst, const char* src, int page,
                                     int pageCount)
{
    int out = 0;
    if (!dst) return NULL;
    if (!src) src = "";
    for (; *src && out < XTDL_HF_MAX; ++src) {
        if (src[0] == '%' && (src[1] == 'p' || src[1] == 'P')) {
            int v = src[1] == 'p' ? page + 1 : pageCount;
            /* 整数十进制展开（上限内截断由循环条件兜住）。 */
            int digits = 1;
            int t = v / 10;
            while (t > 0) { ++digits; t /= 10; }
            t = v;
            {
                char num[16];
                int i;
                for (i = digits - 1; i >= 0; --i) {
                    num[i] = (char)('0' + (t % 10));
                    t /= 10;
                }
                num[digits] = '\0';
                for (i = 0; num[i] && out < XTDL_HF_MAX; ++i)
                    dst[out++] = num[i];
            }
            ++src; /* 吃掉占位第二字符。 */
        } else {
            dst[out++] = src[0];
        }
    }
    dst[out] = '\0';
    return dst;
}

/* ==================== 行表登记 ==================== */

/** @brief 全局行表条目入表（容量倍增；失败 false 保持脏）。 */
static bool xtdl_pushRow(XTextDocumentLayout* self, int blockIndex,
                         int rowInBlock, int yAbs, int pageIndex,
                         const XTextLayoutLine* line)
{
    if (self->m_rowCount >= self->m_rowCap) {
        int newCap = self->m_rowCap > 0 ? self->m_rowCap * 2 : 32;
        XTextDocLayoutRow* grown = (XTextDocLayoutRow*)XRealloc_System(
            self->m_rows, (size_t)newCap * sizeof(XTextDocLayoutRow));
        if (!grown) return false;
        self->m_rows = grown;
        self->m_rowCap = newCap;
    }
    {
        XTextDocLayoutRow* r = &self->m_rows[self->m_rowCount];
        r->m_blockIndex = blockIndex;
        r->m_rowInBlock = rowInBlock;
        r->m_yAbs = yAbs;
        r->m_pageIndex = pageIndex;
        r->m_line = *line;
        ++self->m_rowCount;
    }
    return true;
}

/* ==================== 块上下文管理 ==================== */

/** @brief 释放一个块上下文载荷（实例与文本；数组壳由调用方管）。 */
static void xtdl_freeBlockCtx(XtdlBlockCtx* ctx)
{
    if (!ctx) return;
    if (ctx->m_lay) XClassDelete((XClass*)ctx->m_lay);
    ctx->m_lay = NULL;
    if (ctx->m_text) XFree_System(ctx->m_text);
    ctx->m_text = NULL;
    if (ctx->m_fragOff) XFree_System(ctx->m_fragOff);
    ctx->m_fragOff = NULL;
    ctx->m_fragCount = 0;
    ctx->m_textLen = 0;
}

/**
 * @brief      重建全部块上下文（块文本拼接 + 每块 XTextLayout 装配）。
 * @details    装配口径（任务书「逐块用 W1 的 XTextLayout」）：块文本
 *             setText_utf8、setFont 基础字体、setTextOption **块级对齐
 *             覆盖**（XTDBlock.alignment 水平位非 0 时替换选项水平位，
 *             垂直位不消费）、setLineWidth 可用宽（页宽-左右边距，
 *             <=0 = 不软换行，XTextLayout 同语义）。断行/制表/对齐/
 *             justify 全由 XTextLayout 兑现（任务书第 5 条）。
 * @return     成功 true；分配失败 false（保持脏）。
 */
static bool xtdl_rebuildBlockCtxs(XTextDocumentLayout* self)
{
    XTextDocument* doc = self->m_document;
    int n = doc ? XTextDocument_blockCount(doc) : 0;
    int i;
    int availW = (int)(self->m_pageWidth - self->m_marginLeft -
                       self->m_marginRight);

    /* 释放多余上下文（块数变少 / 文档切换）。 */
    for (i = n; i < self->m_blockLayoutCount; ++i)
        xtdl_freeBlockCtx(&self->m_blockLayouts[i]);
    self->m_blockLayoutCount = n > 0 ? n : 0;

    for (i = 0; i < n; ++i) {
        XtdlBlockCtx* ctx = &self->m_blockLayouts[i];
        const XTDBlock* blk = XTextDocument_findBlockByNumber(doc, i);
        const XTDFragment* frags = blk ? blk->fragments : NULL;
        int fragCount = blk ? (int)blk->fragmentCount : 0;
        int len = 0;
        int j;

        if (!ctx->m_lay) {
            ctx->m_lay = XTextLayout_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            if (!ctx->m_lay) return false;
        }
        /* 块文本拼接 + 片段起始表（先量后拷，两遍成零 realloc 抖动）。 */
        for (j = 0; j < fragCount; ++j) {
            const char* t = XString_toUtf8(frags[j].text);
            len += t ? (int)XStrlen(t) : 0;
        }
        if (!ctx->m_text || len + 1 > ctx->m_textLen + 1) {
            char* grown = (char*)XRealloc_System(
                ctx->m_text, (size_t)(len + 1 > 64 ? len + 1 : 64));
            if (!grown) return false;
            ctx->m_text = grown;
        }
        if (fragCount != ctx->m_fragCount) {
            int* grown = (int*)XRealloc_System(
                ctx->m_fragOff,
                (size_t)(fragCount > 0 ? (size_t)fragCount : 1) *
                    sizeof(int));
            if (!grown) return false;
            ctx->m_fragOff = grown;
            ctx->m_fragCount = fragCount;
        }
        {
            int off = 0;
            for (j = 0; j < fragCount; ++j) {
                const char* t = XString_toUtf8(frags[j].text);
                int tl = t ? (int)XStrlen(t) : 0;
                ctx->m_fragOff[j] = off;
                if (tl > 0 && !frags[j].image) {
                    XMemcpy(ctx->m_text + off, t, (size_t)tl);
                    off += tl;
                }
            }
            ctx->m_text[off] = '\0';
            ctx->m_textLen = off;
        }
        /* 装配块布局器（块级对齐覆盖水平位）。 */
        XTextLayout_setText_utf8(ctx->m_lay, ctx->m_text, ctx->m_textLen);
        XTextLayout_setFont(ctx->m_lay, &self->m_font);
        {
            XTextOption opt = self->m_option;
            int blkAlign = blk ? blk->alignment : 0;
            blkAlign &= XTDL_ALIGN_HORIZONTAL_MASK;
            if (blkAlign != 0) {
                opt.m_alignment &= ~XTDL_ALIGN_HORIZONTAL_MASK;
                opt.m_alignment |= blkAlign;
            }
            XTextLayout_setTextOption(ctx->m_lay, &opt);
        }
        XTextLayout_setLineWidth(ctx->m_lay, availW);
    }
    return true;
}

/** @brief 块上下文存储扩容（容量倍增；新增槽位清零，/RTC1 铁律）。 */
static bool xtdl_ensureBlockCtxCap(XTextDocumentLayout* self, int n)
{
    int oldCap;
    int newCap;
    XtdlBlockCtx* grown;
    if (n <= self->m_blockLayoutCap) return true;
    oldCap = self->m_blockLayoutCap;
    newCap = oldCap > 0 ? oldCap : 16;
    while (newCap < n) newCap *= 2;
    grown = (XtdlBlockCtx*)XRealloc_System(
        self->m_blockLayouts, (size_t)newCap * sizeof(XtdlBlockCtx));
    if (!grown) return false;
    self->m_blockLayouts = grown;
    self->m_blockLayoutCap = newCap;
    XMemset(self->m_blockLayouts + oldCap, 0,
            sizeof(XtdlBlockCtx) * (size_t)(newCap - oldCap));
    return true;
}

/* ==================== 行表重建（分页主循环） ==================== */

/**
 * @brief      重建全局行表 + 页数/尺寸（脏标记置位时的唯一入口）。
 * @details    分页主循环（Qt qtextdocumentlayout.cpp:3604-3605 非懒路径
 *             断页判定 + newPage :121-123 的逐值复刻，探针 report2.txt
 *             逐行配对）：
 *             - 首行顶 = 上边距；块间距 0（块格式无 margins 字段，
 *               layoutBlock 的合并式 :2905-2912 在本子集恒 0）；
 *             - 换页判定：`y + 行高 > pageBottom 且 页内容高 >= 行高`
 *               ⇒ 行挪新页顶（**行永不劈开**——行级断页）；页内容高
 *               不足一行高时**不换页**（:3557-3558 contentHeight >=
 *               lineBreakHeight 守卫，探针 tiny 实测行留页 0）；
 *             - newPage 的 y **只进不退**（Qt :122 `y = qMax(y, …)`）：
 *               守卫挡下后 y 可能已越过 pageBottom 深入后页，后续短行
 *               换页时 max 保证不回退到新页顶（子集边界登记：「行放不
 *               下一页高」的文档里，被挡行之后的短行的落点与 Qt 同式
 *               ——探针 tiny 单行无后续行，docH=28 两侧一致）；
 *             - 页数与布局解耦（Qt dynamicPageCount :3963-3969）：
 *               ceil(文档高 / 页高)，页高 <=0 恒 1——「行放不下一页
 *               高」时布局不换页而页数仍按除法（探针 tiny：docH=28、
 *               页高 12 ⇒ 3 页，行只在页 0）；
 *             - 文档高 = 末行底 + 下边距（含页跳空隙）；文档宽 =
 *               max(可用宽, 最宽行右缘) + 左右边距（Qt fd->size.width =
 *               qMax(newContentsWidth, contentsWidth) + marginWidth，
 *               :3044/:3057；页宽 <=0 取后者）。
 * @return     成功 true；分配失败 false（保持脏）。
 */
static bool xtdl_rebuild(XTextDocumentLayout* self)
{
    XTextDocument* doc = self->m_document;
    double pageH = self->m_pageHeight;
    bool paginated = pageH > 0.0;
    int mtI = (int)self->m_marginTop;
    int mbI = (int)self->m_marginBottom;
    int mlI = (int)self->m_marginLeft;
    int mrI = (int)self->m_marginRight;
    int availW = (int)(self->m_pageWidth - self->m_marginLeft -
                       self->m_marginRight); /* 与 rebuildBlockCtxs 同式。 */
    int pageHI = (int)pageH;
    int contentHPage = paginated ? pageHI - mtI - mbI : 0;
    int pageBottom = paginated ? pageHI - mbI : 0;
    int page = 0;
    int y = mtI;
    int maxRightInner = 0; /* 最宽行右缘（frame 内容区坐标，不含左右边距
                              ——Qt contentsWidth 口径 :3597）。 */
    int n;
    int b;
    int i;

    if (!xtdl_ensureBlockCtxCap(self, doc ? XTextDocument_blockCount(doc)
                                          : 0))
        return false;
    if (!xtdl_rebuildBlockCtxs(self)) return false;

    self->m_rowCount = 0;
    n = self->m_blockLayoutCount;
    for (b = 0; b < n; ++b) {
        XtdlBlockCtx* ctx = &self->m_blockLayouts[b];
        int lines;
        if (!XTextLayout_ensureLayout(ctx->m_lay)) return false;
        lines = XTextLayout_lineCount(ctx->m_lay);
        for (i = 0; i < lines; ++i) {
            XTextLayoutLine line;
            int rowH;
            if (!XTextLayout_lineAt(ctx->m_lay, i, &line)) return false;
            rowH = line.m_height > 0 ? line.m_height : 1;
            /* 换页判定（行级断页 :3604-3605；行高超过页内容高时不换页
             * 守卫 :3557-3558）。newPage 的 y **只进不退**（qMax，
             * qtextdocumentlayout.cpp:122）：守卫挡下后 y 可能已越过
             * pageBottom 深入后页，后续短行换页时不得回退到新页顶。 */
            while (paginated && y + rowH > pageBottom &&
                   contentHPage >= rowH) {
                int newTop;
                ++page;
                newTop = page * pageHI + mtI;
                if (newTop > y) y = newTop;
                pageBottom = (page + 1) * pageHI - mbI;
            }
            if (!xtdl_pushRow(self, b, i, y, paginated ? page : 0, &line))
                return false;
            {
                int right = line.m_x + line.m_width;
                if (right > maxRightInner) maxRightInner = right;
            }
            y += rowH;
        }
    }
    /* 尺寸（Qt documentSize 口径，探针逐值吻合）。 */
    self->m_contentHeight = y + (mbI > 0 ? mbI : 0);
    if (self->m_pageWidth > 0.0) {
        /* 宽 = max(可用宽, 最宽行右缘) + 左右边距（Qt
         * fd->size.width = qMax(newContentsWidth, contentsWidth) +
         * marginWidth，qtextdocumentlayout.cpp:3044/:3057——**真 max**：
         * 病态组合「页宽>0 且左右边距之和 >= 页宽」（availW<=0 ⇒ 不软
         * 换行 ⇒ 行可越页宽）下宽度随内容走，与 Qt 同口径。 */
        int contentW = availW > 0 ? availW : 0;
        int inner = maxRightInner > contentW ? maxRightInner : contentW;
        self->m_contentWidth = inner + mlI + mrI;
    } else {
        self->m_contentWidth = maxRightInner + (self->m_marginRight > 0.0
                                                ? (int)self->m_marginRight
                                                : 0);
    }
    /* 页数 = ceil(文档高 / 页高)，页高 <=0 恒 1。 */
    if (paginated) {
        double q = (double)self->m_contentHeight / pageH;
        self->m_pageCount = (int)(q + 0.999999);
        if (self->m_pageCount < 1) self->m_pageCount = 1;
    } else {
        self->m_pageCount = 1;
    }
    self->m_lastRevision =
        doc ? XTextDocument_revision(doc) : 0;
    return true;
}

/* ==================== 生命周期 ==================== */

XVtable* XTextDocumentLayout_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XTextDocumentLayout)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXTextDocumentLayout_deinit);
    return XVTABLE_DEFAULT;
}

void XTextDocumentLayout_init(XTextDocumentLayout* self)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XObject_init((XObject*)self);
    XClassSetVtable(self, XTextDocumentLayout);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    XFont_init(&self->m_font);
    XTextOption_init(&self->m_option);
    self->m_pageWidth = -1.0;  /* Qt QTextDocument 缺省 pageSize (-1,-1)。 */
    self->m_pageHeight = -1.0;
    self->m_dirty = true;
}

XTextDocumentLayout* XTextDocumentLayout_create_ex(XMemoryType memory)
{
    XTextDocumentLayout* self =
        (XTextDocumentLayout*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XTextDocumentLayout_init(self);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

static void VXTextDocumentLayout_deinit(XTextDocumentLayout* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_blockLayoutCount; ++i)
        xtdl_freeBlockCtx(&self->m_blockLayouts[i]);
    self->m_blockLayoutCount = 0;
    if (self->m_blockLayouts) XFree_System(self->m_blockLayouts);
    self->m_blockLayouts = NULL;
    self->m_blockLayoutCap = 0;
    if (self->m_rows) XFree_System(self->m_rows);
    self->m_rows = NULL;
    self->m_rowCount = 0;
    self->m_rowCap = 0;
    if (self->m_headerText) XFree_System(self->m_headerText);
    self->m_headerText = NULL;
    if (self->m_footerText) XFree_System(self->m_footerText);
    self->m_footerText = NULL;
    self->m_document = NULL;
    self->m_dirty = false;
    XClassDeinit((XClass*)&self->m_font);
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

/* ==================== 输入面 ==================== */

void XTextDocumentLayout_setDocument(XTextDocumentLayout* self,
                                     XTextDocument* doc)
{
    if (!self) return;
    if (self->m_document == doc) return;
    self->m_document = doc;
    self->m_dirty = true; /* 文档切换：块上下文全量重建。 */
}

XTextDocument* XTextDocumentLayout_document(const XTextDocumentLayout* self)
{
    return self ? self->m_document : NULL;
}

void XTextDocumentLayout_setPageSize(XTextDocumentLayout* self, XSizeF size)
{
    if (!self) return;
    self->m_pageWidth = (double)size.width;
    self->m_pageHeight = (double)size.height;
    self->m_dirty = true;
}

XSizeF XTextDocumentLayout_pageSize(const XTextDocumentLayout* self)
{
    XSizeF s;
    s.width = self ? (float)self->m_pageWidth : -1.0f;
    s.height = self ? (float)self->m_pageHeight : -1.0f;
    return s;
}

void XTextDocumentLayout_setPageMargins(XTextDocumentLayout* self,
                                        double left, double top,
                                        double right, double bottom)
{
    if (!self) return;
    self->m_marginLeft = left;
    self->m_marginTop = top;
    self->m_marginRight = right;
    self->m_marginBottom = bottom;
    self->m_dirty = true;
}

void XTextDocumentLayout_pageMargins(const XTextDocumentLayout* self,
                                     double* left, double* top,
                                     double* right, double* bottom)
{
    if (left) *left = self ? self->m_marginLeft : 0.0;
    if (top) *top = self ? self->m_marginTop : 0.0;
    if (right) *right = self ? self->m_marginRight : 0.0;
    if (bottom) *bottom = self ? self->m_marginBottom : 0.0;
}

void XTextDocumentLayout_setFont(XTextDocumentLayout* self, const XFont* font)
{
    XFont f;
    if (!self) return;
    if (font) {
        XMemcpy(&f, font, sizeof(XFont));
    } else {
        XFont_init(&f);
    }
    /* 深拷贝（XTextLayout_setFont 同款：XClassCopy 为 self 深拷贝新串）。 */
    XClassDeinit((XClass*)&self->m_font);
    XFont_init(&self->m_font);
    XClassCopy(&self->m_font, &f);
    self->m_dirty = true;
}

void XTextDocumentLayout_font(const XTextDocumentLayout* self, XFont* out)
{
    if (!out) return;
    if (!self) {
        XFont_init(out);
        return;
    }
    XFont_init(out);
    XClassCopy(out, (XClass*)&self->m_font);
}

void XTextDocumentLayout_setTextOption(XTextDocumentLayout* self,
                                       const XTextOption* option)
{
    if (!self) return;
    if (option) {
        self->m_option = *option;
    } else {
        XTextOption_init(&self->m_option);
    }
    self->m_dirty = true;
}

void XTextDocumentLayout_textOption(const XTextDocumentLayout* self,
                                    XTextOption* out)
{
    if (!out) return;
    if (!self) {
        XTextOption_init(out);
        return;
    }
    *out = self->m_option;
}

/** @brief 页眉/页脚文本公共落盘入口（拷贝 + 失效绘制；不改行表分页）。 */
static void xtdl_setHfText(char** slot, const char* utf8)
{
    int len;
    char* grown;
    if (!slot) return;
    len = utf8 ? (int)XStrlen(utf8) : 0;
    if (len <= 0) {
        if (*slot) XFree_System(*slot);
        *slot = NULL;
        return;
    }
    grown = (char*)XRealloc_System(*slot, (size_t)len + 1);
    if (!grown) return; /* 分配失败保持旧文本。 */
    XMemcpy(grown, utf8, (size_t)len);
    grown[len] = '\0';
    *slot = grown;
}

void XTextDocumentLayout_setHeaderText_utf8(XTextDocumentLayout* self,
                                            const char* utf8)
{
    if (!self) return;
    xtdl_setHfText(&self->m_headerText, utf8);
}

const char* XTextDocumentLayout_headerText_const(
    const XTextDocumentLayout* self)
{
    return self ? self->m_headerText : NULL;
}

void XTextDocumentLayout_setFooterText_utf8(XTextDocumentLayout* self,
                                            const char* utf8)
{
    if (!self) return;
    xtdl_setHfText(&self->m_footerText, utf8);
}

const char* XTextDocumentLayout_footerText_const(
    const XTextDocumentLayout* self)
{
    return self ? self->m_footerText : NULL;
}

/* ==================== 布局结果 ==================== */

bool XTextDocumentLayout_ensureLayout(XTextDocumentLayout* self)
{
    if (!self) return false;
    /* 文档修订号前进 ⇒ 行表失效（Qt documentChanged 通知的收敛版）。 */
    if (!self->m_dirty && self->m_document &&
        XTextDocument_revision(self->m_document) != self->m_lastRevision)
        self->m_dirty = true;
    if (!self->m_dirty) return true;
    if (!xtdl_rebuild(self)) return false; /* 失败保持脏（惰性重试）。 */
    self->m_dirty = false;
    return true;
}

int XTextDocumentLayout_pageCount(XTextDocumentLayout* self)
{
    if (!self) return 0;
    (void)XTextDocumentLayout_ensureLayout(self);
    return self->m_pageCount;
}

XSizeF XTextDocumentLayout_documentSize(XTextDocumentLayout* self)
{
    XSizeF s;
    s.width = 0.0f;
    s.height = 0.0f;
    if (!self) return s;
    (void)XTextDocumentLayout_ensureLayout(self);
    s.width = (float)self->m_contentWidth;
    s.height = (float)self->m_contentHeight;
    return s;
}

int XTextDocumentLayout_rowCount(XTextDocumentLayout* self)
{
    if (!self) return 0;
    (void)XTextDocumentLayout_ensureLayout(self);
    return self->m_rowCount;
}

bool XTextDocumentLayout_rowAt(XTextDocumentLayout* self, int index,
                               XTextDocLayoutRow* out)
{
    if (!self) return false;
    (void)XTextDocumentLayout_ensureLayout(self);
    if (index < 0 || index >= self->m_rowCount) return false;
    if (out) *out = self->m_rows[index];
    return true;
}

/* ==================== 绘制 ==================== */

/**
 * @brief      绘制一个片段段（行字节区间 × 片段区间的交集）。
 * @details    片段行内 x 经块布局器 colToX（对齐/justify/制表分摊已
 *             含——W1 行引擎单源）；格式应用：背景色铺行带、字色、
 *             伪粗体（+1px 双描边）、下划线（基线下 1px）、删除线
 *             （行高中线，XTextEdit.c xte_paintFrag 同款口径）。图片
 *             片段按 0 宽占位跳绘（子集边界，见头注）。
 */
static void xtdl_drawSeg(XTextDocumentLayout* self, XPainter* painter,
                         const XtdlBlockCtx* ctx, const XTDFragment* f,
                         int rowInBlock, const XTextLayoutLine* line,
                         int segStartByte, int segEndByte, int drawX,
                         int baselineY, int topY)
{
    const XTextLayout* lay = ctx->m_lay;
    int colStart = segStartByte - line->m_start;
    int colEnd = segEndByte - line->m_start;
    int x0;
    int x1;
    int w;
    int lineH = line->m_height > 0 ? line->m_height : 1;
    char buf[XTDL_SEG_STACK_BUF];
    char* heap = NULL;
    char* txt;
    int segLen;
    uint32_t fg;
    XFont font;

    if (!f || colEnd <= colStart) return;
    if (f->image) return; /* 图片片段：0 宽占位（子集边界）。 */
    x0 = XTextLayout_colToX((XTextLayout*)lay, rowInBlock, colStart);
    x1 = XTextLayout_colToX((XTextLayout*)lay, rowInBlock, colEnd);
    w = x1 - x0;
    if (w <= 0) return;
    drawX += x0;
    segLen = segEndByte - segStartByte;
    if (segLen < (int)sizeof(buf)) {
        XMemcpy(buf, ctx->m_text + segStartByte, (size_t)segLen);
        buf[segLen] = '\0';
        txt = buf;
    } else {
        heap = (char*)XMalloc_System((size_t)segLen + 1);
        if (!heap) return;
        XMemcpy(heap, ctx->m_text + segStartByte, (size_t)segLen);
        heap[segLen] = '\0';
        txt = heap;
    }
    if (f->fmt.bgColor != 0u) {
        XRect bg;
        bg.x = drawX;
        bg.y = topY;
        bg.width = w;
        bg.height = lineH;
        XPainter_fillRect(painter, &bg, f->fmt.bgColor);
    }
    fg = f->fmt.fgColor != 0 ? f->fmt.fgColor : XTDL_DEFAULT_FG;
    xtdl_fragFont(&self->m_font, f, &font);
    XPainter_setFont(painter, &font);
    XPainter_drawText(painter, drawX, baselineY, txt, fg);
    if (f->fmt.bold) {
        /* 伪粗体：同基线 +1px 双描边（XTextEdit 同款近似）。 */
        XPainter_drawText(painter, drawX + 1, baselineY, txt, fg);
    }
    if (f->fmt.underline) {
        int uy = baselineY + 1;
        if (uy > topY + lineH - 1) uy = topY + lineH - 1;
        XPainter_drawLine(painter, drawX, uy, drawX + w, uy);
    }
    if (f->fmt.strikeOut) {
        int sy = topY + lineH / 2;
        XPainter_drawLine(painter, drawX, sy, drawX + w, sy);
    }
    XClassDeinit((XClass*)&font);
    if (heap) XFree_System(heap);
}

/**
 * @brief      绘制一行（遍历块片段取与行字节区间的交集逐段画）。
 * @param      dy 文档绝对 y → 目标坐标的平移量（drawPage = -页顶；
 *             draw = 0）。x 无平移（页面坐标与文档坐标同 x）。
 */
static void xtdl_drawRow(XTextDocumentLayout* self, XPainter* painter,
                         const XTextDocLayoutRow* row, int dy)
{
    XTextDocument* doc = self->m_document;
    XtdlBlockCtx* ctx;
    const XTDBlock* blk;
    int fragCount;
    int j;
    int drawX = (int)self->m_marginLeft + row->m_line.m_x;
    int baselineY = row->m_yAbs + row->m_line.m_baseline + dy;
    int topY = row->m_yAbs + dy;

    if (!doc || row->m_blockIndex < 0 ||
        row->m_blockIndex >= self->m_blockLayoutCount)
        return;
    ctx = &self->m_blockLayouts[row->m_blockIndex];
    blk = XTextDocument_findBlockByNumber(doc, row->m_blockIndex);
    if (!blk || !ctx->m_lay) return;
    fragCount = (int)blk->fragmentCount;
    for (j = 0; j < fragCount; ++j) {
        int fragStart = ctx->m_fragOff[j];
        int fragEnd = j + 1 < fragCount ? ctx->m_fragOff[j + 1]
                                        : ctx->m_textLen;
        int segStart = fragStart > row->m_line.m_start ? fragStart
                                                       : row->m_line.m_start;
        int segEnd = fragEnd < row->m_line.m_end ? fragEnd
                                                 : row->m_line.m_end;
        if (segEnd > segStart)
            xtdl_drawSeg(self, painter, ctx, &blk->fragments[j],
                         row->m_rowInBlock, &row->m_line, segStart, segEnd,
                         drawX, baselineY, topY);
    }
}

/**
 * @brief      绘制页眉/页脚（页顶/页底界边距区；占位 %p/%P 展开）。
 * @details    页眉：上边距区、左对齐页左边距、基线 = 页顶 + ascent；
 *             页脚：下边距区、右对齐页右缘（Qt print 页码右对齐同款
 *             qtextdocument.cpp:1941-1943）、基线 = 页底界 + ascent。
 *             页矩形域：页宽/页高 <=0 时取内容尺寸（不分页 = 整文档
 *             一页）。行高超过边距区时侵入正文区（子集边界，见头注）。
 */
static void xtdl_drawHeaderFooter(XTextDocumentLayout* self,
                                  XPainter* painter, int pageIndex)
{
    /* 页宽/页高整型化与 xtdl_rebuild 同一口径（截断）——行表页归属与
     * 绘制平移必须同源，否则页内容整体错位 1px。 */
    int pageW = self->m_pageWidth > 0.0 ? (int)self->m_pageWidth
                                        : self->m_contentWidth;
    int pageH = self->m_pageHeight > 0.0 ? (int)self->m_pageHeight
                                         : self->m_contentHeight;
    int pageTop = self->m_pageHeight > 0.0 ? pageIndex * pageH : 0;
    int ml = (int)self->m_marginLeft;
    int mr = (int)self->m_marginRight;
    int mb = (int)self->m_marginBottom;
    int ascent = XPainter_textAscent(&self->m_font);
    char expanded[XTDL_HF_MAX + 1];
    const char* text;

    if (self->m_headerText) {
        text = xtdl_expandPlaceholders(
            expanded, self->m_headerText, pageIndex, self->m_pageCount);
        if (text && text[0])
            XPainter_drawText(painter, ml, pageTop + ascent, text,
                              XTDL_DEFAULT_FG);
    }
    if (self->m_footerText) {
        int w;
        text = xtdl_expandPlaceholders(
            expanded, self->m_footerText, pageIndex, self->m_pageCount);
        if (text && text[0]) {
            w = XPainter_textWidth(&self->m_font, text);
            XPainter_drawText(painter, pageW - mr - w,
                              pageTop + pageH - mb + ascent, text,
                              XTDL_DEFAULT_FG);
        }
    }
}

void XTextDocumentLayout_drawPage(XTextDocumentLayout* self,
                                  int pageIndex, XPainter* painter)
{
    int i;
    if (!self || !painter) return;
    if (!XTextDocumentLayout_ensureLayout(self)) return;
    if (pageIndex < 0 || pageIndex >= self->m_pageCount) return;
    for (i = 0; i < self->m_rowCount; ++i) {
        const XTextDocLayoutRow* row = &self->m_rows[i];
        if (row->m_pageIndex != pageIndex) continue;
        xtdl_drawRow(self, painter, row,
                     self->m_pageHeight > 0.0
                         ? -pageIndex * (int)self->m_pageHeight
                         : 0);
    }
    xtdl_drawHeaderFooter(self, painter, pageIndex);
}

void XTextDocumentLayout_draw(XTextDocumentLayout* self,
                              XPainter* painter, const XRect* clipRect)
{
    int i;
    if (!self || !painter) return;
    if (!XTextDocumentLayout_ensureLayout(self)) return;
    for (i = 0; i < self->m_rowCount; ++i) {
        const XTextDocLayoutRow* row = &self->m_rows[i];
        if (clipRect) {
            int rowH = row->m_line.m_height > 0 ? row->m_line.m_height : 1;
            if (row->m_yAbs + rowH <= clipRect->y) continue;
            if (row->m_yAbs >= clipRect->y + clipRect->height) continue;
        }
        xtdl_drawRow(self, painter, row, 0);
    }
}

bool XTextDocumentLayout_drawToImage(XTextDocumentLayout* self,
                                     int pageIndex, XImage* image)
{
    XPainter painter;
    if (!self || !image) return false;
    if (pageIndex < 0 || pageIndex >= XTextDocumentLayout_pageCount(self))
        return false;
    if (XImage_width(image) <= 0 || XImage_height(image) <= 0) return false;
    XImage_fill(image, 0xFFFFFFFFu);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return false;
    }
    XTextDocumentLayout_drawPage(self, pageIndex, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    return true;
}

#endif /* XTEXTDOCUMENT_ON */

#endif /* XGUI_ON */
