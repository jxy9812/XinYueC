/******************************************************************************
 * @file       XLabel.c
 * @brief      XLabel 标签控件实现（对标 Qt 6.8 QLabel 全部公开 API 的 C 适配）。
 * @details    实现要点：
 *             - 继承 XFrame（QLabel : QFrame）；生命周期走 XClass 体系：
 *               XLabel_init/create_ex 挂 XLabel 虚表，重载 Event /
 *               PaintEvent / ChangeEvent / Mouse* / Focus* / Copy / Move /
 *               Deinit；
 *             - 文本：m_text 保存原始文本，setText/setNum 写入；按
 *               textFormat 解释（AutoText 自动检测 HTML）生成显示文本
 *               m_displayText 与链接表 m_links（富文本最小子集：剥标签/
 *               标记、5 种 HTML 实体、<a href> 与 [label](url) 链接、
 *               <br>/<p> 与 Markdown 段落换行）；
 *             - 尺寸：sizeHint / minimumSizeHint / heightForWidth 全部按
 *               Qt 6.8 QLabelPrivate::sizeForWidth 语义实现（文本 =
 *               (文本宽高 + 2*margin ± indent) + 边框 contentsMargins；
 *               wordWrap 时高度随宽度变化，含 golden-ratio 收缩启发式）；
 *             - 绘制：drawContents 先画 XFrame 边框，再在客户区（减
 *               margin/indent）内绘制文本/像素图/绘图记录/影片；文本经
 *               内置 8x16 点阵字体（XPainter 文本原语逐字形输出），
 *               支持选中段高亮着色与链接着色/下划线；
 *             - 交互：LinksAccessibleByMouse 下悬停发 linkHovered、
 *               按下再释放于同一链接发 linkActivated；openExternalLinks
 *               置真时仍只发信号（无平台浏览器 API）；
 *             - 选择：setSelection 为程序化子集（UTF-16 码元偏移，仅
 *               影响绘制）；mouse 拖选与键盘选择为受限未实现项；
 *             - 复用：几何/尺寸策略/焦点/光标/调色板/事件分发等全部走
 *               XWidget / XFrame 公共 API 与虚槽；容器、字符串、像素图、
 *               绘图记录、影片、信号机制均复用库内既有实现，不重复
 *               造轮子（对齐项目风格文档「优先复用库内已有实现」）。
 *             本模块不依赖任何平台 API，嵌入式可用；绘制全部走 XPainter。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XLabel.h"
#include "XFont.h"
#include "XMemory.h"
#include "XEventType.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XPicture.h"
#include "XMovie.h"
#include "XCursor.h"
#include "XFont8x16.h"
#include <string.h>

#if XWIDGET_ON && XFRAME_ON && XLABEL_ON

/* ==================== 内部类型与工具 ==================== */

/** @brief 一行文本的布局记录（UTF-8 字节区间 + 像素宽度）。 */
typedef struct LabelLine
{
    int m_start;   /**< 起始字节偏移（含）。 */
    int m_end;     /**< 结束字节偏移（排他）。 */
    int m_width;   /**< 本行像素宽度。 */
} LabelLine;

/** @brief 文本块布局结果（行数组 + 对齐后的绘制矩形）。 */
typedef struct LabelLayout
{
    LabelLine* m_lines;   /**< 行数组（调用方负责释放）。 */
    int m_lineCount;      /**< 行数。 */
    XRect m_rect;         /**< 绘制矩形（含垂直对齐偏移）。 */
} LabelLayout;

/* ==================== 内部函数声明 ==================== */

static void label_updateLabel(XLabel* self);
static void label_updateMouseTracking(XLabel* self);
static void label_freeLinks(XLabel* self);
static void label_rebuildDisplay(XLabel* self);
static int label_hitLinkAt(const XLabel* self, const XPoint* pos);
static int label_posToUtf16(const XLabel* self, const XPoint* pos);
static void label_selectFromAnchor(XLabel* self, int pos);
static int label_currentCursor(const XLabel* self);

/** @brief 按控件布局方向转换逻辑水平对齐（对标 QStyle::visualAlignment）。 */
static XAlignments label_visualAlignment(const XLabel* self,
                                         XAlignments alignment)
{
    XWidgetLayoutDirection direction;
    if (!(alignment & XAlignment_HorizontalMask))
        alignment |= XAlignment_Left;
    if (alignment & XAlignment_Absolute)
        return alignment;
    if (!(alignment & (XAlignment_Left | XAlignment_Right)))
        return alignment;
    direction = XWidget_layoutDirection((const XWidget*)self);
    if (direction == XWidgetLayoutDirection_RightToLeft)
        alignment ^= (XAlignment_Left | XAlignment_Right);
    alignment |= XAlignment_Absolute;
    return alignment;
}

/* ==================== UTF-8 / UTF-16 长度小工具（复用 XChar 语义） ==================== */

/** @brief 返回 UTF-8 首字节对应的码点字节长度（1~4；无效序列按 1）。 */
static int label_utf8len(const char* utf8)
{
    unsigned char c;
    if (!utf8 || utf8[0] == '\0') return 0;
    c = (unsigned char)utf8[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/** @brief 返回 UTF-8 首字节对应码点在 UTF-16 中的码元数（1 或 2）。 */
static int label_utf16len(const char* utf8)
{
    unsigned char c;
    if (!utf8 || utf8[0] == '\0') return 0;
    c = (unsigned char)utf8[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 1;
    if ((c & 0xF0) == 0xE0) return 1;
    if ((c & 0xF8) == 0xF0) return 2;
    return 1;
}

/** @brief 计算 [start,end) 字节区间在 UTF-16 中的码元数。 */
static int label_utf16lenRange(const char* utf8, int start, int end)
{
    int p = start;
    int n = 0;
    while (p < end) {
        int l = label_utf8len(&utf8[p]);
        if (l <= 0) break;
        n += label_utf16len(&utf8[p]);
        p += l;
    }
    return n;
}

/** @brief 当前显示文本的 UTF-8 字节长度（供链接字节偏移定位）。 */
static int label_displayByteLen(const XString* display)
{
    return display ? (int)XString_toUtf8_length(display) : 0;
}

/* ==================== 点阵字号缩放小工具（复用 XWidget/XFont） ==================== */

/** @brief 从基类 XFont 读取像素字号；未设置（<=0）回退默认 16。 */
static int label_pixelSize(const XLabel* self)
{
    XFont f;
    int px;
    if (!self) return XFONT8X16_HEIGHT;
    f = XWidget_font((XWidget*)self);
    px = XFont_bitmapPixelSize(&f, XFONT8X16_HEIGHT);
    XFont_deinit(&f);
    return px;
}

/** @brief 返回整倍缩放系数（复用 XFont 的通用位图缩放算法）。 */
static int label_scale(const XLabel* self)
{
    XFont f;
    int sc;
    if (!self) return 1;
    f = XWidget_font((XWidget*)self);
    sc = XFont_bitmapScaleForFont(&f, XFONT8X16_HEIGHT);
    XFont_deinit(&f);
    return sc;
}

/** @brief 缩放后单字宽（= 8 x scale）。 */
static int label_advance(const XLabel* self)
{
    return XFONT8X16_WIDTH * label_scale(self);
}

/** @brief 缩放后行高（= 16 x scale）。 */
static int label_lineHeight(const XLabel* self)
{
    return XFONT8X16_HEIGHT * label_scale(self);
}

/** @brief 缩放后基线以上高度（= 13 x scale）。 */
static int label_ascent(const XLabel* self)
{
    return XFONT8X16_ASCENT * label_scale(self);
}

/** @brief 缩放后基线以下高度（= 3 x scale）。 */
static int label_descent(const XLabel* self)
{
    return XFONT8X16_DESCENT * label_scale(self);
}

/* ==================== 信号发射（复用 XObject 信号机制） ==================== */

/** @brief 链接信号参数释放回调：释放列表内拷贝的 XString。 */
static void label_linkSignal_del(XVarList* list)
{
    XVarList_args_1(list, XString*, link);
    if (link)
        XString_delete_base((XClass*)link);
}

/** @brief 发射携带 XString* 的链接信号；无接收者时释放参数列表。 */
static void label_emitLinkSignal(XLabel* self, size_t signal,
                                 const XString* link)
{
    XString* copy;
    XVarList* args;
    copy = link ? XString_create_copy(link) : XString_create();
    if (!copy) return;
    args = XVarList_Create(XVar(XString*, copy));
    if (!args) {
        XString_delete_base((XClass*)copy);
        return;
    }
    if (self && ((XObject*)self)->m_signalSlot)
        XObject_emitSignal((XObject*)self, signal, args,
                           label_linkSignal_del, NULL, XEVENT_PRIORITY_NORMAL);
    else {
        XVarList_setArgsDel(args, label_linkSignal_del);
        XVarList_delete(args);
    }
}

/** @brief 发射空字符串 linkHovered（表示离开所有链接）。 */
static void label_emitHoverLeave(XLabel* self)
{
    XString* empty;
    if (!self) return;
    empty = XString_create();
    if (!empty) return;
    label_emitLinkSignal(self, (size_t)XLabel_linkHovered_signal, empty);
    XString_delete_base((XClass*)empty);
}

/* ==================== 调色板前景角色（复用 XWidget 角色规则） ==================== */

/** @brief 返回绘制文本用的前景角色（对标 QWidget::foregroundRole 推断规则）。 */
static XPaletteColorRole label_foregroundRole(const XLabel* self)
{
    XPaletteColorRole role = XWidget_foregroundRole((XWidget*)self);
    if (role != XPaletteColorRole_NoRole)
        return role;
    switch (XWidget_backgroundRole((XWidget*)self)) {
    case XPaletteColorRole_Button:
        return XPaletteColorRole_ButtonText;
    case XPaletteColorRole_Base:
        return XPaletteColorRole_Text;
    case XPaletteColorRole_Dark:
    case XPaletteColorRole_Shadow:
        return XPaletteColorRole_Light;
    case XPaletteColorRole_Highlight:
        return XPaletteColorRole_HighlightedText;
    case XPaletteColorRole_ToolTipBase:
        return XPaletteColorRole_ToolTipText;
    default:
        return XPaletteColorRole_WindowText;
    }
}

/** @brief 取指定角色颜色为 ARGB32；无调色板能力时回退纯黑。 */
static uint32_t label_color(const XLabel* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/* ==================== 富文本 / Markdown 最小子集解析 ==================== */

/** @brief 判断文本是否“像 HTML”（对标 Qt::mightBeRichText 的实用近似）。 */
static bool label_mightBeRichText(const char* utf8)
{
    const char* lt;
    char next;
    if (!utf8 || !*utf8) return false;
    lt = strchr(utf8, '<');
    if (!lt) return false;
    next = lt[1];
    if (next == '&' || next == '<' || next == '>') return false;
    return strchr(lt, '>') != NULL;
}

/** @brief 标签首词是否等于 name（大小写不敏感，遇空格/斜杠/大于号结束）。 */
static bool label_tagIs(const char* tag, const char* name)
{
    int i = 0;
    while (tag[i] && tag[i] != ' ' && tag[i] != '/' && tag[i] != '>') {
        char c = tag[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (name[i] == '\0' || c != name[i]) return false;
        ++i;
    }
    return name[i] == '\0' &&
           (tag[i] == '\0' || tag[i] == ' ' || tag[i] == '/' || tag[i] == '>');
}

/** @brief 判断标签是否为 name 的闭合标签（< /name ...>）。 */
static bool label_tagIsClose(const char* tag, const char* name)
{
    return tag && tag[0] == '/' && label_tagIs(tag + 1, name);
}

/** @brief 从 <a ...> 标签文本中提取 href 属性值；无则返回 NULL。 */
static XString* label_extractHref(const char* tag)
{
    const char* h = strstr(tag, "href");
    const char* end;
    char quote;
    if (!h) return NULL;
    h += 4;
    while (*h == ' ' || *h == '\t') ++h;
    if (*h != '=') return NULL;
    ++h;
    while (*h == ' ' || *h == '\t') ++h;
    quote = *h;
    if (quote != '"' && quote != '\'') return NULL;
    ++h;
    end = strchr(h, quote);
    if (!end) return NULL;
    return XString_create_with_length_utf8(h, (size_t)(end - h));
}

/** @brief 解码 5 种 HTML 实体；返回消耗的字节数（含 ';'），0 表示非实体。 */
static int label_decodeEntity(const char* p, XChar* out)
{
    static const struct { const char* name; XChar ch; } kEntities[] = {
        { "amp", '&' }, { "lt", '<' }, { "gt", '>' },
        { "quot", '"' }, { "apos", '\'' }
    };
    const char* semi;
    int i, n;
    if (!p || p[0] != '&') return 0;
    semi = strchr(p, ';');
    if (!semi || semi - p > 8) return 0;
    n = (int)(semi - p) - 1; /* 不含 '&' 与 ';' */
    for (i = 0; i < (int)(sizeof(kEntities) / sizeof(kEntities[0])); ++i) {
        if ((int)strlen(kEntities[i].name) == n &&
            strncmp(p + 1, kEntities[i].name, (size_t)n) == 0) {
            if (out) *out = kEntities[i].ch;
            return n + 2;
        }
    }
    return 0;
}

/** @brief 关闭当前 <a> 链接并写入链接表（end<=start 或 href 空时丢弃）。 */
static void label_closeLink(XString* display, XLabelLinkRange** links,
                            int* count, int* cap, int* linkStart,
                            XString** href)
{
    XLabelLinkRange* arr;
    int end;
    if (!href || !*href || *linkStart < 0) {
        if (href && *href) { XString_delete_base((XClass*)*href); *href = NULL; }
        if (linkStart) *linkStart = -1;
        return;
    }
    end = label_displayByteLen(display);
    if (end > *linkStart) {
        if (*count >= *cap) {
            int ncap = *cap ? *cap * 2 : 4;
            arr = (XLabelLinkRange*)XMemory_realloc(
                *links, sizeof(XLabelLinkRange) * (size_t)ncap,
                XMEMORY_TYPE_MULTIPOOL);
            if (arr) { *links = arr; *cap = ncap; }
        }
        if (*count < *cap) {
            (*links)[*count].m_start = *linkStart;
            (*links)[*count].m_end = end;
            (*links)[*count].m_href = *href;
            (*href) = NULL;
            ++(*count);
        }
    }
    if (*href) { XString_delete_base((XClass*)*href); *href = NULL; }
    *linkStart = -1;
}

/** @brief 解析富文本（HTML 最小子集）生成显示文本与链接表。 */
static void label_parseHtml(XLabel* self, const char* utf8)
{
    XString* display = XString_create();
    XString* href = NULL;
    XLabelLinkRange* links = NULL;
    int linkCount = 0;
    int linkCap = 0;
    int linkStart = -1;
    const char* p = utf8;
    while (p && *p) {
        if (*p == '<') {
            const char* gt = strchr(p + 1, '>');
            char tag[64];
            int tlen;
            if (!gt) break; /* 无闭合 '>'：剩余按原文处理 */
            tlen = (int)(gt - (p + 1));
            if (tlen > 63) tlen = 63;
            memcpy(tag, p + 1, (size_t)tlen);
            tag[tlen] = '\0';
            if (label_tagIs(tag, "br") || label_tagIs(tag, "p") ||
                label_tagIsClose(tag, "p")) {
                XString_append_char(display, (XChar)'\n');
            } else if (label_tagIsClose(tag, "a")) {
                label_closeLink(display, &links, &linkCount, &linkCap,
                                &linkStart, &href);
            } else if (label_tagIs(tag, "a")) {
                XString* h = label_extractHref(tag);
                label_closeLink(display, &links, &linkCount, &linkCap,
                                &linkStart, &href);
                if (h) {
                    href = h;
                    linkStart = label_displayByteLen(display);
                }
            }
            /* 其余标签直接剥离，内容保留 */
            p = gt + 1;
        } else if (*p == '&') {
            XChar ch = 0;
            int consumed = label_decodeEntity(p, &ch);
            if (consumed > 0) {
                XString_append_char(display, ch);
                p += consumed;
            } else {
                XString_append_char(display, (XChar)'&');
                ++p;
            }
        } else {
            int len = label_utf8len(p);
            XString_append_with_length_utf8(display, p, (size_t)len);
            p += len;
        }
    }
    label_closeLink(display, &links, &linkCount, &linkCap, &linkStart, &href);
    if (self->m_displayText) XString_delete_base((XClass*)self->m_displayText);
    if (self->m_links) label_freeLinks(self);
    self->m_displayText = display;
    self->m_links = links;
    self->m_linkCount = linkCount;
    self->m_linkCapacity = linkCap;
}

/** @brief 解析 Markdown 最小子集（链接/强调/标题/换行）生成显示文本与链接表。 */
static void label_parseMarkdown(XLabel* self, const char* utf8)
{
    XString* display = XString_create();
    XString* href = NULL;
    XLabelLinkRange* links = NULL;
    int linkCount = 0;
    int linkCap = 0;
    int linkStart = -1;
    const char* p = utf8;
    bool atLineStart = true;
    while (p && *p) {
        if (*p == '\n') {
            XString_append_char(display, (XChar)'\n');
            ++p;
            atLineStart = true;
            continue;
        }
        if (atLineStart) {
            const char* q = p;
            int hashes = 0;
            while (*q == ' ') ++q;
            while (*q == '#') { ++hashes; ++q; }
            if (hashes > 0 && (*q == ' ' || *q == '\t' || *q == '\0')) {
                p = q;
                if (*p == ' ' || *p == '\t') ++p;
                atLineStart = false;
                continue;
            }
        }
        if (*p == '[') {
            const char* close = strchr(p + 1, ']');
            if (close && close[1] == '(') {
                const char* paren = strchr(close + 2, ')');
                if (paren) {
                    XString* url = XString_create_with_length_utf8(
                        close + 2, (size_t)(paren - close - 2));
                    label_closeLink(display, &links, &linkCount, &linkCap,
                                    &linkStart, &href);
                    if (url) {
                        href = url;
                        linkStart = label_displayByteLen(display);
                        XString_append_with_length_utf8(
                            display, p + 1, (size_t)(close - p - 1));
                        label_closeLink(display, &links, &linkCount, &linkCap,
                                        &linkStart, &href);
                    }
                    p = paren + 1;
                    atLineStart = false;
                    continue;
                }
            }
        }
        if (*p == '*' || *p == '_' || *p == '`') {
            int mlen = (p[1] == *p) ? 2 : 1;
            const char* close;
            if (mlen == 2) {
                char pair[3] = { *p, *p, '\0' };
                close = strstr(p + mlen, pair);
            } else {
                close = strchr(p + mlen, *p);
            }
            if (close) {
                /* 剥掉两端强调标记，内容原样保留 */
                XString_append_with_length_utf8(
                    display, p + mlen, (size_t)(close - p - mlen));
                p = close + mlen;
                atLineStart = false;
                continue;
            }
        }
        {
            int len = label_utf8len(p);
            XString_append_with_length_utf8(display, p, (size_t)len);
            p += len;
            atLineStart = false;
        }
    }
    label_closeLink(display, &links, &linkCount, &linkCap, &linkStart, &href);
    if (self->m_displayText) XString_delete_base((XClass*)self->m_displayText);
    if (self->m_links) label_freeLinks(self);
    self->m_displayText = display;
    self->m_links = links;
    self->m_linkCount = linkCount;
    self->m_linkCapacity = linkCap;
}

/** @brief 按生效文本格式重新生成显示文本与链接表。 */
static void label_rebuildDisplay(XLabel* self)
{
    const char* src;
    if (!self) return;
    if (!self->m_text || XString_isEmpty_base((const XContainer*)self->m_text)) {
        if (!self->m_displayText)
            self->m_displayText = XString_create();
        if (self->m_displayText)
            XString_assign_utf8(self->m_displayText, "");
        label_freeLinks(self);
        return;
    }
    src = XString_toUtf8(self->m_text);
    if (self->m_effectiveTextFormat == XLabelTextFormat_RichText)
        label_parseHtml(self, src);
    else if (self->m_effectiveTextFormat == XLabelTextFormat_MarkdownText)
        label_parseMarkdown(self, src);
    else {
        if (!self->m_displayText)
            self->m_displayText = XString_create();
        if (self->m_displayText)
            XString_assign_utf8(self->m_displayText, src);
        label_freeLinks(self);
    }
}

/* ==================== 内容清理（对标 QLabelPrivate::clearContents） ==================== */

/** @brief 释放全部显示内容并退出文本模式；m_text/m_displayText 保留空串。 */
static void label_clearContents(XLabel* self)
{
    if (!self) return;
    if (self->m_text) XString_assign_utf8(self->m_text, "");
    if (self->m_displayText) XString_assign_utf8(self->m_displayText, "");
    label_freeLinks(self);
    if (self->m_picture) {
        XPicture_delete_base(self->m_picture);
        self->m_picture = NULL;
    }
    XPixmap_deinit_base(&self->m_pixmap);
    XPixmap_init(&self->m_pixmap);
    self->m_movie = NULL;
    self->m_isTextLabel = false;
    self->m_selectionStart = -1;
    self->m_selectionLength = 0;
    self->m_pressedLink = -1;
    self->m_hoverLink = -1;
    self->m_textSelecting = false;
    self->m_selectionAnchor = -1;
}

/** @brief 释放链接数组及其 href 字符串。 */
void label_freeLinks(XLabel* self)
{
    int i;
    if (!self) return;
    if (self->m_links) {
        for (i = 0; i < self->m_linkCount; ++i) {
            if (self->m_links[i].m_href)
                XString_delete_base((XClass*)self->m_links[i].m_href);
        }
        XMemory_free(self->m_links, XMEMORY_TYPE_MULTIPOOL);
        self->m_links = NULL;
    }
    self->m_linkCount = 0;
    self->m_linkCapacity = 0;
}

/* ==================== 文本断行布局（复用 XPainter 区间测宽） ==================== */

/** @brief 把一个单词按可用宽度硬切为多行（等宽字体每字形 8*scale 像素）。 */
static void label_emitHardBreak(const char* utf8, int start, int end,
                                int availWidth, int scale, LabelLine* lines,
                                int* count, int cap)
{
    int glyphW;
    int maxGlyphs;
    int p = start;
    int lineStart = start;
    int glyphs = 0;
    if (scale < 1) scale = 1;
    glyphW = XFONT8X16_WIDTH * scale;
    maxGlyphs = availWidth / glyphW;
    if (maxGlyphs < 1) maxGlyphs = 1;
    while (p < end) {
        int gl = label_utf8len(&utf8[p]);
        if (gl <= 0) break;
        if (glyphs + 1 > maxGlyphs) {
            if (*count < cap) {
                lines[*count].m_start = lineStart;
                lines[*count].m_end = p;
                lines[*count].m_width = glyphs * glyphW;
                ++(*count);
            }
            lineStart = p;
            glyphs = 0;
        }
        ++glyphs;
        p += gl;
    }
    if (*count < cap && p > lineStart) {
        lines[*count].m_start = lineStart;
        lines[*count].m_end = p;
        lines[*count].m_width = glyphs * glyphW;
        ++(*count);
    }
}

/** @brief 对 [start,end) 段做贪心断行（按单词；单词超宽时硬切）。 */
static void label_layoutSegment(const char* utf8, int start, int end,
                                int availWidth, bool wrap, const XFont* font,
                                LabelLine* lines, int* count, int cap)
{
    int p = start;
    int lineStart = start;
    int lineWidth = 0;
    int lastWordEnd = start;
    bool haveWord = false;
    int scale = XFont_bitmapScaleForFont(font, XFONT8X16_HEIGHT);
    if (scale < 1) scale = 1;
    if (!wrap) {
        if (*count < cap) {
            lines[*count].m_start = start;
            lines[*count].m_end = end;
            lines[*count].m_width =
                XPainter_textWidthRange(font, utf8, start, end);
            ++(*count);
        }
        return;
    }
    if (availWidth <= 0) {
        /* 宽度 0：逐词一行（对标 Qt 宽度 0 时按词断行且不硬切长词）。 */
        p = start;
        lineStart = start;
        haveWord = false;
        while (p < end) {
            int wordStart, wordEnd, wordWidth;
            wordStart = p;
            while (wordStart < end && utf8[wordStart] == ' ') ++wordStart;
            if (wordStart >= end) break;
            wordEnd = wordStart;
            while (wordEnd < end && utf8[wordEnd] != ' ') ++wordEnd;
            wordWidth = XPainter_textWidthRange(font, utf8, wordStart,
                                               wordEnd);
            if (haveWord && *count < cap) {
                lines[*count].m_start = lineStart;
                lines[*count].m_end = lastWordEnd;
                lines[*count].m_width = lineWidth;
                ++(*count);
            }
            lineStart = wordStart;
            lineWidth = wordWidth;
            lastWordEnd = wordEnd;
            haveWord = true;
            p = wordEnd;
            while (p < end && utf8[p] == ' ') ++p;
        }
        if (haveWord) {
            if (*count < cap) {
                lines[*count].m_start = lineStart;
                lines[*count].m_end = lastWordEnd;
                lines[*count].m_width = lineWidth;
                ++(*count);
            }
        } else if (*count < cap) {
            lines[*count].m_start = lineStart;
            lines[*count].m_end = lineStart;
            lines[*count].m_width = 0;
            ++(*count);
        }
        return;
    }
    while (p < end) {
        int wordStart, wordEnd, wordWidth, spaceWidth;
        wordStart = p;
        while (wordStart < end && utf8[wordStart] == ' ') ++wordStart;
        if (wordStart >= end) break; /* 段尾空白 */
        wordEnd = wordStart;
        while (wordEnd < end && utf8[wordEnd] != ' ') ++wordEnd;
        wordWidth = XPainter_textWidthRange(font, utf8, wordStart,
                                           wordEnd);
        spaceWidth = (haveWord ? (wordStart - lastWordEnd) *
                                     XFONT8X16_WIDTH * scale : 0);
        if (!haveWord) {
            if (wordWidth > availWidth) {
                label_emitHardBreak(utf8, wordStart, wordEnd, availWidth,
                                    scale, lines, count, cap);
                lastWordEnd = wordEnd;
                lineStart = wordEnd;
                lineWidth = 0;
                haveWord = false;
            } else {
                lineStart = wordStart;
                lineWidth = wordWidth;
                lastWordEnd = wordEnd;
                haveWord = true;
            }
        } else if (lineWidth + spaceWidth + wordWidth > availWidth) {
            if (*count < cap) {
                lines[*count].m_start = lineStart;
                lines[*count].m_end = lastWordEnd;
                lines[*count].m_width = lineWidth;
                ++(*count);
            }
            lineStart = wordStart;
            lineWidth = wordWidth;
            lastWordEnd = wordEnd;
            haveWord = true;
        } else {
            lineWidth += spaceWidth + wordWidth;
            lastWordEnd = wordEnd;
        }
        p = wordEnd;
        while (p < end && utf8[p] == ' ') ++p;
    }
    if (haveWord) {
        if (*count < cap) {
            lines[*count].m_start = lineStart;
            lines[*count].m_end = lastWordEnd;
            lines[*count].m_width = lineWidth;
            ++(*count);
        }
    } else if (*count < cap) {
        lines[*count].m_start = lineStart;
        lines[*count].m_end = lineStart;
        lines[*count].m_width = 0;
        ++(*count);
    }
}

/** @brief 按可用宽度把显示文本切分为行（分配行数组，调用方负责释放）。 */
static int label_layout(const char* utf8, int availWidth, bool wrap,
                        const XFont* font, LabelLine** outLines)
{
    int len = (int)strlen(utf8);
    int cap = len + 1;
    int count = 0;
    int segStart = 0;
    LabelLine* lines;
    if (cap < 1) cap = 1;
    lines = (LabelLine*)XMemory_malloc(
        sizeof(LabelLine) * (size_t)cap, XMEMORY_TYPE_MULTIPOOL);
    if (!lines) { if (outLines) *outLines = NULL; return 0; }
    while (1) {
        int segEnd = segStart;
        while (segEnd < len && utf8[segEnd] != '\n') ++segEnd;
        label_layoutSegment(utf8, segStart, segEnd, availWidth, wrap,
                            font, lines, &count, cap);
        if (segEnd >= len) break;
        segStart = segEnd + 1;
    }
    if (count == 0) {
        lines[0].m_start = 0;
        lines[0].m_end = 0;
        lines[0].m_width = 0;
        count = 1;
    }
    if (outLines) *outLines = lines;
    return count;
}

/** @brief 计算文本块尺寸（max 行宽 x 行数*16）。 */
static void label_textBlockSize(const XLabel* self, int layoutWidth,
                                bool wrap, int* outWidth, int* outHeight)
{
    LabelLine* lines;
    int n, i, w = 0;
    const char* utf8;
    if (!self || !self->m_displayText) {
        if (outWidth) *outWidth = 0;
        if (outHeight) *outHeight = label_lineHeight(self);
        return;
    }
    utf8 = XString_toUtf8(self->m_displayText);
    {
        XFont font = XWidget_font((XWidget*)self);
        n = label_layout(utf8, layoutWidth, wrap, &font, &lines);
        XFont_deinit(&font);
    }
    for (i = 0; i < n; ++i)
        if (lines[i].m_width > w) w = lines[i].m_width;
    if (outWidth) *outWidth = w;
    if (outHeight) *outHeight = n * label_lineHeight(self);
    XMemory_free(lines, XMEMORY_TYPE_MULTIPOOL);
}

/* ==================== 尺寸计算（对标 QLabelPrivate::sizeForWidth） ==================== */

/** @brief 按 Qt 6.8 sizeForWidth 语义计算标签内容尺寸。 */
static XSize label_sizeForWidth(const XLabel* self, int w)
{
    XSize out;
    XMargins cm;
    int contentsMarginWidth, contentsMarginHeight;
    int hextra, vextra, m, maxW;
    XRect br;
    XAlignments align;
    bool wrap;
    if (!self) { XSize_init(&out, 0, 0); return out; }
    if (w >= 0 && XWidget_minimumWidth((XWidget*)self) > 0 &&
        w < XWidget_minimumWidth((XWidget*)self))
        w = XWidget_minimumWidth((XWidget*)self);
    cm = XWidget_contentsMargins((XWidget*)self);
    contentsMarginWidth = cm.left + cm.right;
    contentsMarginHeight = cm.top + cm.bottom;
    hextra = 2 * self->m_margin;
    vextra = hextra;
    XRect_init(&br, 0, 0, 0, 0);
    align = label_visualAlignment(self, self->m_alignment);
    if (!XPixmap_isNull(&self->m_pixmap)) {
        br.width = XPixmap_width(&self->m_pixmap);
        br.height = XPixmap_height(&self->m_pixmap);
    } else if (self->m_picture && !XPicture_isNull(self->m_picture)) {
        XPicture_boundingRect(self->m_picture, &br);
    } else if (self->m_movie) {
        XPixmap pm;
        XPixmap_init(&pm);
        XMovie_currentPixmap(self->m_movie, &pm);
        if (!XPixmap_isNull(&pm)) {
            br.width = XPixmap_width(&pm);
            br.height = XPixmap_height(&pm);
        }
        XPixmap_deinit_base(&pm);
    } else if (self->m_isTextLabel) {
        m = self->m_indent;
        if (m < 0 && XFrame_frameWidth((XFrame*)self))
            m = label_advance(self) - self->m_margin * 2; /* fm.horizontalAdvance('x') - margin*2 */
        if (m > 0) {
            if ((align & XAlignment_Left) || (align & XAlignment_Right))
                hextra += m;
            if ((align & XAlignment_Top) || (align & XAlignment_Bottom))
                vextra += m;
        }
        wrap = self->m_wordWrap != false;
        if (wrap) {
            int layoutWidth;
            if (w >= 0)
                layoutWidth = w - hextra - contentsMarginWidth;
            else {
                maxW = XWidget_maximumWidth((XWidget*)self);
                layoutWidth = (label_advance(self) * 80 < maxW)
                                  ? label_advance(self) * 80 : maxW;
                layoutWidth -= (hextra + contentsMarginWidth);
            }
            if (layoutWidth < 0) layoutWidth = 0;
            label_textBlockSize(self, layoutWidth, true, &br.width, &br.height);
            if (w < 0) {
                /* golden-ratio 收缩启发式（对标 Qt tryWidth 分支） */
                if (br.height < 4 * label_lineHeight(self) &&
                    br.width > layoutWidth / 2) {
                    int lw2 = layoutWidth / 2;
                    int w2, h2;
                    label_textBlockSize(self, lw2, true, &w2, &h2);
                    if (h2 < 2 * label_lineHeight(self) &&
                        w2 > lw2 / 2) {
                        label_textBlockSize(self, lw2 / 2, true,
                                            &br.width, &br.height);
                    } else {
                        br.width = w2;
                        br.height = h2;
                    }
                }
            }
        } else {
            label_textBlockSize(self, -1, false, &br.width, &br.height);
        }
    } else {
        /* 空标签：平均字宽 x 行高（含字号缩放） */
        br.width = label_advance(self);
        br.height = label_lineHeight(self);
    }
    out.width = br.width + hextra + contentsMarginWidth;
    out.height = br.height + vextra + contentsMarginHeight;
    {
        XSize mins = XWidget_minimumSize((XWidget*)self);
        if (out.width < mins.width) out.width = mins.width;
        if (out.height < mins.height) out.height = mins.height;
    }
    return out;
}

/* ==================== 绘制几何（对齐 + 缩进 + 换行） ==================== */

/** @brief 计算文本块绘制几何（供绘制与命中测试共用）。 */
static void label_computeLayout(const XLabel* self, const XRect* cr,
                                LabelLayout* out)
{
    int availWidth = cr->width;
    int indent, m;
    int textH;
    int yo;
    XAlignments align = label_visualAlignment(self, self->m_alignment);
    const char* utf8;
    if (!out) return;
    out->m_lines = NULL;
    out->m_lineCount = 0;
    XRect_init(&out->m_rect, cr->x, cr->y, cr->width, cr->height);
    if (!self->m_isTextLabel || !self->m_displayText) return;
    utf8 = XString_toUtf8(self->m_displayText);
    indent = self->m_indent;
    if (indent < 0 && XFrame_frameWidth((XFrame*)self))
        indent = label_advance(self) - self->m_margin * 2;
    m = indent;
    if (m > 0 && ((align & XAlignment_Left) || (align & XAlignment_Right)))
        availWidth -= m;
    if (availWidth < 0) availWidth = 0;
    {
        XFont font = XWidget_font((XWidget*)self);
        out->m_lineCount = label_layout(utf8,
                                        self->m_wordWrap ? availWidth : -1,
                                        self->m_wordWrap, &font,
                                        &out->m_lines);
        XFont_deinit(&font);
    }
    textH = out->m_lineCount * label_lineHeight(self);
    out->m_rect.x = cr->x;
    if (m > 0 && (align & XAlignment_Left)) out->m_rect.x += m;
    out->m_rect.width = availWidth;
    yo = 0;
    if (align & XAlignment_VCenter) {
        yo = (cr->height - textH) / 2;
        if (yo < 0) yo = 0;
    } else if (align & XAlignment_Bottom) {
        yo = cr->height - textH;
        if (yo < 0) yo = 0;
    }
    out->m_rect.y = cr->y + yo;
    out->m_rect.height = textH;
}

/** @brief 返回某一行的水平起点（按对齐计算）。 */
static int label_lineX(const LabelLayout* layout, int index, XAlignments align)
{
    int x = layout->m_rect.x;
    int lw = (index >= 0 && index < layout->m_lineCount)
                 ? layout->m_lines[index].m_width : 0;
    if (align & XAlignment_Right)
        x = layout->m_rect.x + layout->m_rect.width - lw;
    else if (align & XAlignment_HCenter)
        x = layout->m_rect.x + (layout->m_rect.width - lw) / 2;
    return x;
}

/* ==================== 逐字形绘制（选择高亮 / 链接着色） ==================== */

/** @brief 绘制一行文本：选中段高亮、链接着色/下划线，逐字形输出。 */
static void label_drawLine(XPainter* painter, const char* utf8,
                           int start, int end, int x, int baselineY,
                           int utf16Offset, int selStart, int selEnd,
                           const XLabelLinkRange* links, int linkCount,
                           uint32_t textColor, uint32_t hlBg, uint32_t hlFg,
                           uint32_t linkColor)
{
    const XFont* fnt;
    int p = start;
    int gx = x;
    int u16 = utf16Offset;
    int scale;
    int adv;
    fnt = XPainter_font(painter);
    scale = (fnt ? XFont_bitmapScaleForFont(fnt, XFONT8X16_HEIGHT) : 1);
    if (scale < 1) scale = 1;
    adv = XFONT8X16_WIDTH * scale;
    while (p < end) {
        int glyphLen = label_utf8len(&utf8[p]);
        int u16len;
        int li;
        bool selected;
        bool inLink;
        uint32_t color;
        if (glyphLen <= 0) break;
        u16len = label_utf16len(&utf8[p]);
        selected = (selStart >= 0) &&
                   (u16 < selEnd) && (u16 + u16len > selStart);
        inLink = false;
        for (li = 0; li < linkCount; ++li) {
            if (p >= links[li].m_start && p + glyphLen <= links[li].m_end) {
                inLink = true;
                break;
            }
        }
        if (selected) {
            XRect r = { gx, baselineY - XFONT8X16_ASCENT * scale,
                        adv, XFONT8X16_HEIGHT * scale };
            XPainter_fillRect(painter, &r, hlBg);
            color = hlFg;
        } else if (inLink) {
            color = linkColor;
        } else {
            color = textColor;
        }
        XPainter_drawGlyph(painter, gx, baselineY,
                          &utf8[p], color);
        gx += adv;
        p += glyphLen;
        u16 += u16len;
    }
}

/** @brief 在给定客户区绘制文本内容（对齐/缩进/换行/选择/链接）。 */
static void label_drawTextContent(XLabel* self, XPainter* painter,
                                  const XRect* cr)
{
    LabelLayout layout;
    const char* utf8;
    int i, utf16pos;
    int selStart, selEnd;
    uint32_t textColor, hlBg, hlFg, linkColor;
    if (!self->m_isTextLabel || !self->m_displayText) return;
    label_computeLayout(self, cr, &layout);
    if (layout.m_lineCount <= 0 || !layout.m_lines) return;
    utf8 = XString_toUtf8(self->m_displayText);
    selStart = self->m_selectionStart;
    selEnd = (selStart < 0) ? -1 : selStart + self->m_selectionLength;
    textColor = label_color(self, label_foregroundRole(self));
    hlBg = label_color(self, XPaletteColorRole_Highlight);
    hlFg = label_color(self, XPaletteColorRole_HighlightedText);
    linkColor = label_color(self, XPaletteColorRole_Link);
    {
        XFont font = XWidget_font((XWidget*)self);
        XPainter_setFont(painter, &font);
        XFont_deinit(&font);
    }
    utf16pos = 0;
    for (i = 0; i < layout.m_lineCount; ++i) {
        int x = label_lineX(&layout, i,
                            label_visualAlignment(self, self->m_alignment));
        int top = layout.m_rect.y + i * label_lineHeight(self);
        int baselineY = top + label_ascent(self);
        label_drawLine(painter, utf8,
                       layout.m_lines[i].m_start, layout.m_lines[i].m_end,
                       x, baselineY, utf16pos, selStart, selEnd,
                       self->m_links, self->m_linkCount,
                       textColor, hlBg, hlFg, linkColor);
        utf16pos += label_utf16lenRange(utf8,
                                        layout.m_lines[i].m_start,
                                        layout.m_lines[i].m_end);
    }
    XMemory_free(layout.m_lines, XMEMORY_TYPE_MULTIPOOL);
}

/* ==================== 链接命中测试 ==================== */

/** @brief 返回位置处命中的链接索引；未命中返回 -1。 */
static int label_hitLinkAt(const XLabel* self, const XPoint* pos)
{
    XRect cr;
    LabelLayout layout;
    int lineIndex, glyphIndex, byte, g;
    const char* utf8;
    int li;
    if (!self || !pos || !self->m_isTextLabel || !self->m_displayText ||
        self->m_linkCount <= 0)
        return -1;
    cr = XWidget_contentsRect((XWidget*)self);
    cr.x += self->m_margin;
    cr.y += self->m_margin;
    cr.width -= self->m_margin * 2;
    cr.height -= self->m_margin * 2;
    if (cr.width <= 0 || cr.height <= 0) return -1;
    label_computeLayout(self, &cr, &layout);
    if (layout.m_lineCount <= 0 || !layout.m_lines) return -1;
    if (pos->y < layout.m_rect.y ||
        pos->y >= layout.m_rect.y + layout.m_rect.height) {
        XMemory_free(layout.m_lines, XMEMORY_TYPE_MULTIPOOL);
        return -1;
    }
    {
        int lineHeight = label_lineHeight(self);
        lineIndex = lineHeight > 0
            ? (pos->y - layout.m_rect.y) / lineHeight : 0;
    }
    if (lineIndex < 0 || lineIndex >= layout.m_lineCount) {
        XMemory_free(layout.m_lines, XMEMORY_TYPE_MULTIPOOL);
        return -1;
    }
    {
        int x = label_lineX(&layout, lineIndex,
                            label_visualAlignment(self, self->m_alignment));
        int lw = layout.m_lines[lineIndex].m_width;
        if (pos->x < x || pos->x >= x + lw) {
            XMemory_free(layout.m_lines, XMEMORY_TYPE_MULTIPOOL);
            return -1;
        }
        {
            int advance = label_advance(self);
            glyphIndex = advance > 0 ? (pos->x - x) / advance : 0;
        }
    }
    utf8 = XString_toUtf8(self->m_displayText);
    byte = layout.m_lines[lineIndex].m_start;
    g = 0;
    while (g < glyphIndex && byte < layout.m_lines[lineIndex].m_end) {
        int l = label_utf8len(&utf8[byte]);
        if (l <= 0) break;
        byte += l;
        ++g;
    }
    for (li = 0; li < self->m_linkCount; ++li) {
        if (byte >= self->m_links[li].m_start &&
            byte < self->m_links[li].m_end) {
            XMemory_free(layout.m_lines, XMEMORY_TYPE_MULTIPOOL);
            return li;
        }
    }
    XMemory_free(layout.m_lines, XMEMORY_TYPE_MULTIPOOL);
    return -1;
}

/* ==================== 文本选择定位（鼠标拖选 / 键盘扩展选择） ==================== */

/** @brief 把自定义绘制几何中的起点转化为当前行的字形个数。 */
static int label_glyphCountForRange(const char* utf8, int start, int end)
{
    int p = start, n = 0;
    if (!utf8) return 0;
    while (p < end) {
        int l = label_utf8len(&utf8[p]);
        if (l <= 0 || p + l > end) break;
        ++n;
        p += l;
    }
    return n;
}

/** @brief 把字形位置转换为 UTF-16 码元偏移。 */
static int label_glyphIndexToUtf16(const char* utf8, int start, int glyphIndex)
{
    int p = start;
    int u16 = 0;
    int g = 0;
    if (!utf8) return 0;
    while (g < glyphIndex && utf8[p] != '\0') {
        int l = label_utf8len(&utf8[p]);
        if (l <= 0) break;
        u16 += label_utf16len(&utf8[p]);
        p += l;
        ++g;
    }
    return u16;
}

/** @brief 返回鼠标位置命中的 UTF-16 文本偏移（含行首前 / 行尾后裁剪）。 */
static int label_posToUtf16(const XLabel* self, const XPoint* pos)
{
    XRect cr;
    LabelLayout layout;
    int lineIndex, glyphIndex, lineGlyphs, total;
    int byte, x, lw, g;
    const char* utf8;
    if (!self || !pos || !self->m_isTextLabel || !self->m_displayText)
        return 0;
    if (XString_length_base((XContainer*)self->m_displayText) == 0)
        return 0;
    cr = XWidget_contentsRect((XWidget*)self);
    cr.x += self->m_margin;
    cr.y += self->m_margin;
    cr.width -= self->m_margin * 2;
    cr.height -= self->m_margin * 2;
    if (cr.width <= 0 || cr.height <= 0) return 0;
    label_computeLayout(self, &cr, &layout);
    utf8 = XString_toUtf8(self->m_displayText);
    if (layout.m_lineCount <= 0 || !layout.m_lines) {
        total = (int)XString_length_base((XContainer*)self->m_displayText);
        return total;
    }
    if (pos->y < layout.m_rect.y) {
        total = (int)XString_length_base((XContainer*)self->m_displayText);
        XMemory_free(layout.m_lines, XMEMORY_TYPE_HYBRID);
        return 0;
    }
    if (pos->y >= layout.m_rect.y + layout.m_rect.height) {
        total = (int)XString_length_base((XContainer*)self->m_displayText);
        XMemory_free(layout.m_lines, XMEMORY_TYPE_HYBRID);
        return total;
    }
    lineIndex = (pos->y - layout.m_rect.y) / label_lineHeight(self);
    if (lineIndex < 0 || lineIndex >= layout.m_lineCount) {
        total = (int)XString_length_base((XContainer*)self->m_displayText);
        XMemory_free(layout.m_lines, XMEMORY_TYPE_HYBRID);
        return total;
    }
    x = label_lineX(&layout, lineIndex,
                    label_visualAlignment(self, self->m_alignment));
    lw = layout.m_lines[lineIndex].m_width;
    lineGlyphs = label_glyphCountForRange(utf8,
                                          layout.m_lines[lineIndex].m_start,
                                          layout.m_lines[lineIndex].m_end);
    if (pos->x < x)
        glyphIndex = 0;
    else if (pos->x >= x + lw)
        glyphIndex = lineGlyphs;
    else {
        int adv = label_advance(self);
        glyphIndex = adv > 0 ? (pos->x - x) / adv : 0;
        if (glyphIndex > lineGlyphs) glyphIndex = lineGlyphs;
    }
    byte = layout.m_lines[lineIndex].m_start;
    g = 0;
    while (g < glyphIndex && byte < layout.m_lines[lineIndex].m_end) {
        int l = label_utf8len(&utf8[byte]);
        if (l <= 0) break;
        byte += l;
        ++g;
    }
    total = (int)XString_length_base((XContainer*)self->m_displayText);
    {
        int prefix = 0;
        int li;
        for (li = 0; li < lineIndex; ++li)
            prefix += label_utf16lenRange(utf8,
                                          layout.m_lines[li].m_start,
                                          layout.m_lines[li].m_end);
        prefix += label_glyphIndexToUtf16(utf8,
                                          layout.m_lines[lineIndex].m_start,
                                          g);
        XMemory_free(layout.m_lines, XMEMORY_TYPE_HYBRID);
        if (prefix > total) prefix = total;
        return prefix;
    }
}

/** @brief 依据锚点把当前 UTF-16 位置更新为选择范围。 */
static void label_selectFromAnchor(XLabel* self, int pos)
{
    int anchor, start, len, total;
    if (!self) return;
    anchor = self->m_selectionAnchor;
    if (anchor < 0) anchor = pos;
    total = (int)XString_length_base((XContainer*)self->m_displayText);
    if (pos < 0) pos = 0;
    if (pos > total) pos = total;
    if (anchor > total) anchor = total;
    if (pos < anchor) {
        start = pos;
        len = anchor - pos;
    } else {
        start = anchor;
        len = pos - anchor;
    }
    self->m_selectionAnchor = anchor;
    self->m_selectionStart = start;
    self->m_selectionLength = len;
    XWidget_update((XWidget*)self);
}

/** @brief 返回当前文本光标位置（选择区另一端点；未选择时按起点+长度）。 */
static int label_currentCursor(const XLabel* self)
{
    int start, len, anchor;
    if (!self || !self->m_isTextLabel || !self->m_displayText) return 0;
    start = self->m_selectionStart;
    len = self->m_selectionLength;
    if (start < 0) start = 0;
    if (len < 0) len = 0;
    anchor = self->m_selectionAnchor;
    if (anchor < 0)
        return start + len;
    if (start + len == anchor)
        return start;
    return start + len;
}

/* ==================== 内容绘制（像素图/绘图记录/影片/文本） ==================== */

/** @brief 在客户区按对齐绘制像素图（scaledContents 时拉伸填满）。 */
static void label_drawPixmap(XLabel* self, XPainter* painter,
                             const XPixmap* pm, const XRect* cr)
{
    XImage image;
    XPixmap scaledPm;
    int x, y, w, h;
    if (!self || !painter || !pm || !cr || XPixmap_isNull(pm)) return;
    w = XPixmap_width(pm);
    h = XPixmap_height(pm);
    if (w <= 0 || h <= 0) return;
    if (self->m_scaledContents) {
        if (cr->width <= 0 || cr->height <= 0) return;
        XPixmap_init(&scaledPm);
        XImage_init(&image);
        XPixmap_scaled(pm, cr->width, cr->height, 0, 0, &scaledPm);
        XPixmap_toImage(&scaledPm, &image);
        XPainter_drawImage(painter, &image, cr->x, cr->y);
        XImage_deinit_base(&image);
        XPixmap_deinit_base(&scaledPm);
        return;
    }
    x = cr->x;
    y = cr->y;
    XAlignments align = label_visualAlignment(self, self->m_alignment);
    if (align & XAlignment_Right)
        x = cr->x + cr->width - w;
    else if (align & XAlignment_HCenter)
        x = cr->x + (cr->width - w) / 2;
    if (align & XAlignment_Bottom)
        y = cr->y + cr->height - h;
    else if (align & XAlignment_VCenter)
        y = cr->y + (cr->height - h) / 2;
    XImage_init(&image);
    XPixmap_toImage(pm, &image);
    XPainter_drawImage(painter, &image, x, y);
    XImage_deinit_base(&image);
}

/** @brief 在客户区绘制像素图/绘图记录/影片/文本（对标 QLabel::paintEvent 内容部分）。 */
static void label_drawContent(XLabel* self, XPainter* painter)
{
    XRect cr;
    if (!self || !painter) return;
    cr = XWidget_contentsRect((XWidget*)self);
    cr.x += self->m_margin;
    cr.y += self->m_margin;
    cr.width -= self->m_margin * 2;
    cr.height -= self->m_margin * 2;
    if (self->m_movie) {
        XPixmap pm;
        XPixmap_init(&pm);
        XMovie_currentPixmap(self->m_movie, &pm);
        if (!XPixmap_isNull(&pm))
            label_drawPixmap(self, painter, &pm, &cr);
        XPixmap_deinit_base(&pm);
    } else if (!XPixmap_isNull(&self->m_pixmap)) {
        label_drawPixmap(self, painter, &self->m_pixmap, &cr);
    } else if (self->m_picture && !XPicture_isNull(self->m_picture)) {
        XPicture_play(self->m_picture, painter);
    } else if (self->m_isTextLabel) {
        label_drawTextContent(self, painter, &cr);
    }
}

/* ==================== 虚槽实现（对标 QLabel 事件/复制/移动/析构） ==================== */

/** @brief 事件入口：离开/隐藏时清空悬停链接状态，随后交父类处理。 */
static bool VXLabel_event(XWidget* self, XEvent* event)
{
    XLabel* label = (XLabel*)self;
    XEventType type = event ? XEvent_type(event) : XEVENT_TYPE_NONE;
    if (type == XEVENT_TYPE_LEAVE || type == XEVENT_TYPE_HIDE) {
        if (label) {
            if (label->m_hoverLink >= 0) {
                label->m_hoverLink = -1;
                label_emitHoverLeave(label);
                XWidget_unsetCursor((XWidget*)label);
            }
            label->m_pressedLink = -1;
        }
    }
    return XClass_Parent(XFrame, EXObject_Event,
                         bool(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 变更事件：字体变化重排文本，随后交父类处理。 */
static void VXLabel_changeEvent(XWidget* self, XEvent* event)
{
    XEventType type = event ? XEvent_type(event) : XEVENT_TYPE_NONE;
    if (type == XEVENT_TYPE_FONT_CHANGE)
        label_updateLabel((XLabel*)self);
    XClass_Parent(XFrame, EXWidget_ChangeEvent,
                  void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 绘制事件：在 paintDevice 上平移裁剪后调用 drawContents。 */
static void VXLabel_paintEvent(XWidget* self, XEvent* event)
{
#if !XPALETTE_ON || !XWINDOWEVENT_ON
    (void)self;
    (void)event;
    return;
#else
    XImage* image;
    XPoint offset;
    XPainter painter;
#if XPAINTER_CLIP_ON
    XPaintEvent* pe;
    XRect clip;
#endif
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
#if XPAINTER_CLIP_ON
    pe = (XPaintEvent*)event;
#endif
    image = XWidget_paintDevice(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPAINTER_CLIP_ON
    clip = XPaintEvent_rect(pe);
    XPainter_setClipRect(&painter, &clip, XPainterClipOperation_ReplaceClip);
#endif
    XLabel_drawContents((XLabel*)self, &painter);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
#endif /* XPALETTE_ON && XWINDOWEVENT_ON */
}

/** @brief 鼠标按下：可选中时开始拖选；命中链接时优先记录链接按下索引。 */
static void VXLabel_mousePressEvent(XWidget* self, XEvent* event)
{
    XLabel* label = (XLabel*)self;
    XMouseEvent* me;
    int link, pos;
    if (!self || !event) return;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    label->m_pressedLink = -1;
    if (label->m_isTextLabel &&
        (label->m_textInteractionFlags &
         XLabelTextInteraction_LinksAccessibleByMouse) &&
        label->m_linkCount > 0) {
        link = label_hitLinkAt(label, &me->m_position);
        if (link >= 0) {
            label->m_pressedLink = link;
            label->m_textSelecting = false;
            XEvent_accept(event);
            return;
        }
    }
    if (XMouseEvent_button(me) == XMouseButton_LeftButton &&
        label->m_isTextLabel &&
        (label->m_textInteractionFlags &
         XLabelTextInteraction_TextSelectableByMouse)) {
        pos = label_posToUtf16(label, &me->m_position);
        label->m_textSelecting = true;
        label->m_selectionAnchor = pos;
        label->m_selectionStart = pos;
        label->m_selectionLength = 0;
        XWidget_setFocus((XWidget*)label);
        XWidget_update((XWidget*)label);
        XEvent_accept(event);
        return;
    }
    label->m_textSelecting = false;
    XEvent_ignore(event);
}

/** @brief 鼠标释放：按下与释放命中同一链接时发 linkActivated。 */
static void VXLabel_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XLabel* label = (XLabel*)self;
    XMouseEvent* me;
    int link, pressed, pos;
    if (!self || !event) return;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    me = (XMouseEvent*)event;
    if (label->m_textSelecting &&
        (label->m_textInteractionFlags &
         XLabelTextInteraction_TextSelectableByMouse)) {
        pos = label_posToUtf16(label, &me->m_position);
        label_selectFromAnchor(label, pos);
        label->m_textSelecting = false;
        XEvent_accept(event);
        return;
    }
    pressed = label->m_pressedLink;
    label->m_pressedLink = -1;
    if (pressed >= 0 &&
        (label->m_textInteractionFlags &
         XLabelTextInteraction_LinksAccessibleByMouse)) {
        link = label_hitLinkAt(label, &me->m_position);
        if (link == pressed && link >= 0 && link < label->m_linkCount) {
            XLabel_linkActivated_signal(label,
                                        label->m_links[link].m_href);
            XEvent_accept(event);
            return;
        }
    }
    XEvent_ignore(event);
}

/** @brief 鼠标移动：拖选时扩展选择；否则处理链接触发与手型光标。 */
static void VXLabel_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XLabel* label = (XLabel*)self;
    XMouseEvent* me;
    int link, pos;
    if (!self || !event) return;
    if (XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    me = (XMouseEvent*)event;
    if (label->m_textSelecting &&
        (label->m_textInteractionFlags &
         XLabelTextInteraction_TextSelectableByMouse)) {
        pos = label_posToUtf16(label, &me->m_position);
        label_selectFromAnchor(label, pos);
        XEvent_accept(event);
        return;
    }
    if (!label->m_isTextLabel ||
        !(label->m_textInteractionFlags &
          XLabelTextInteraction_LinksAccessibleByMouse) ||
        label->m_linkCount <= 0) {
        XEvent_ignore(event);
        return;
    }
    link = label_hitLinkAt(label, &me->m_position);
    if (link != label->m_hoverLink) {
        label->m_hoverLink = link;
        if (link >= 0 && link < label->m_linkCount) {
            XCursor cursor;
            XLabel_linkHovered_signal(label,
                                      label->m_links[link].m_href);
            XCursor_init(&cursor);
            XCursor_setShape(&cursor, XCursor_PointingHand);
            XWidget_setCursor((XWidget*)label, &cursor);
        } else {
            label_emitHoverLeave(label);
            XWidget_unsetCursor((XWidget*)label);
        }
    }
    XEvent_accept(event);
}

/** @brief 鼠标双击：受限项，忽略（选择/链接双击语义未实现）。 */
static void VXLabel_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    (void)self;
    if (event) XEvent_ignore(event);
}

/** @brief 键盘选择：方向键/Home/End 移动，Shift 扩展，Ctrl+A 全选。 */
static void VXLabel_keyPressEvent(XWidget* self, XEvent* event)
{
    XLabel* label = (XLabel*)self;
    XKeyEvent* ke;
    int key, total, cursor, anchor, start, len;
    bool shift, control, handled;
    if (!self || !event) return;
    if (XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    ke = (XKeyEvent*)event;
    if (!label->m_isTextLabel ||
        !(label->m_textInteractionFlags &
          XLabelTextInteraction_TextSelectableByKeyboard)) {
        XEvent_ignore(event);
        return;
    }
    total = (int)XString_length_base((XContainer*)label->m_displayText);
    cursor = label_currentCursor(label);
    anchor = (label->m_selectionAnchor >= 0)
                 ? label->m_selectionAnchor : cursor;
    shift = (XKeyEvent_modifiers(ke) &
             XKeyboardModifier_ShiftModifier) != 0;
    control = (XKeyEvent_modifiers(ke) &
               XKeyboardModifier_ControlModifier) != 0;
    key = XKeyEvent_key(ke);
    handled = true;
    switch (key) {
    case XKey_Left:
        if (cursor > 0) cursor -= 1;
        break;
    case XKey_Right:
        if (cursor < total) cursor += 1;
        break;
    case XKey_Home:
        cursor = 0;
        break;
    case XKey_End:
        cursor = total;
        break;
    case 'A':
        if (!control) { handled = false; break; }
        anchor = 0;
        cursor = total;
        shift = true;
        break;
    default:
        handled = false;
        break;
    }
    if (!handled) {
        XEvent_ignore(event);
        return;
    }
    if (!shift) anchor = cursor;
    if (cursor < anchor) {
        start = cursor;
        len = anchor - cursor;
    } else {
        start = anchor;
        len = cursor - anchor;
    }
    label->m_selectionAnchor = anchor;
    label->m_selectionStart = start;
    label->m_selectionLength = len;
    label->m_textSelecting = false;
    XWidget_update((XWidget*)label);
    XEvent_accept(event);
}

/** @brief 焦点进入：交父类默认处理。 */
static void VXLabel_focusInEvent(XWidget* self, XEvent* event)
{
    if (self && event)
        XClass_Parent(XFrame, EXWidget_FocusInEvent,
                      void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 焦点离开：交父类默认处理。 */
static void VXLabel_focusOutEvent(XWidget* self, XEvent* event)
{
    if (self && event)
        XClass_Parent(XFrame, EXWidget_FocusOutEvent,
                      void(*)(XWidget*, XEvent*))((XWidget*)self, event);
}

/** @brief 深拷贝：父类拷贝后深拷文本/绘图记录/链接，像素图共享。 */
static void VXLabel_copy(XLabel* self, const XLabel* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XLabel_init(self, NULL, 0);
    XClass_Parent(XFrame, EXClass_Copy,
                  void(*)(XFrame*, const XFrame*))((XFrame*)self,
                                                   (const XFrame*)other);
    /* 释放目标已有的标签级资源 */
    if (self->m_text) { XString_delete_base((XClass*)self->m_text); self->m_text = NULL; }
    if (self->m_displayText) { XString_delete_base((XClass*)self->m_displayText); self->m_displayText = NULL; }
    label_freeLinks(self);
    if (self->m_picture) {
        XPicture_delete_base(self->m_picture);
        self->m_picture = NULL;
    }
    XPixmap_deinit_base(&self->m_pixmap);
    XPixmap_init(&self->m_pixmap);
    /* 深拷贝标签字段 */
    self->m_text = XString_create_copy(other->m_text);
    self->m_displayText = XString_create_copy(other->m_displayText);
    self->m_textFormat = other->m_textFormat;
    self->m_effectiveTextFormat = other->m_effectiveTextFormat;
    self->m_alignment = other->m_alignment;
    self->m_isTextLabel = other->m_isTextLabel;
    self->m_wordWrap = other->m_wordWrap;
    self->m_scaledContents = other->m_scaledContents;
    self->m_openExternalLinks = other->m_openExternalLinks;
    self->m_indent = other->m_indent;
    self->m_margin = other->m_margin;
    self->m_textInteractionFlags = other->m_textInteractionFlags;
    self->m_selectionStart = other->m_selectionStart;
    self->m_selectionLength = other->m_selectionLength;
    self->m_pressedLink = -1;
    self->m_hoverLink = -1;
    self->m_textSelecting = false;
    self->m_selectionAnchor = -1;
    self->m_resourceProvider = other->m_resourceProvider;
    self->m_resourceProviderUserData = other->m_resourceProviderUserData;
    if (other->m_picture)
        self->m_picture = XPicture_create_copy(other->m_picture,
                                               XCLASS_DEFAULT_MEMORY_TYPE);
    XPixmap_copy_base(&self->m_pixmap, &other->m_pixmap);
    self->m_movie = other->m_movie;
    self->m_buddy = other->m_buddy;
    if (other->m_linkCount > 0) {
        self->m_links = (XLabelLinkRange*)XMemory_malloc(
            sizeof(XLabelLinkRange) * (size_t)other->m_linkCount,
            XMEMORY_TYPE_MULTIPOOL);
        if (self->m_links) {
            for (i = 0; i < other->m_linkCount; ++i) {
                self->m_links[i].m_start = other->m_links[i].m_start;
                self->m_links[i].m_end = other->m_links[i].m_end;
                self->m_links[i].m_href =
                    XString_create_copy(other->m_links[i].m_href);
            }
            self->m_linkCount = other->m_linkCount;
            self->m_linkCapacity = other->m_linkCount;
        }
    }
}

/** @brief 移动语义：父类移动后转移资源所有权，源对象归默认值。 */
static void VXLabel_move(XLabel* self, XLabel* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XLabel_init(self, NULL, 0);
    XClass_Parent(XFrame, EXClass_Move,
                  void(*)(XFrame*, XFrame*))((XFrame*)self, (XFrame*)other);
    /* 释放目标已有标签级资源 */
    if (self->m_text) { XString_delete_base((XClass*)self->m_text); self->m_text = NULL; }
    if (self->m_displayText) { XString_delete_base((XClass*)self->m_displayText); self->m_displayText = NULL; }
    label_freeLinks(self);
    if (self->m_picture) {
        XPicture_delete_base(self->m_picture);
        self->m_picture = NULL;
    }
    XPixmap_deinit_base(&self->m_pixmap);
    XPixmap_init(&self->m_pixmap);
    /* 转移资源 */
    self->m_text = other->m_text; other->m_text = XString_create();
    self->m_displayText = other->m_displayText; other->m_displayText = XString_create();
    self->m_textFormat = other->m_textFormat;
    self->m_effectiveTextFormat = other->m_effectiveTextFormat;
    self->m_alignment = other->m_alignment;
    self->m_isTextLabel = other->m_isTextLabel;
    self->m_wordWrap = other->m_wordWrap;
    self->m_scaledContents = other->m_scaledContents;
    self->m_openExternalLinks = other->m_openExternalLinks;
    self->m_indent = other->m_indent;
    self->m_margin = other->m_margin;
    self->m_textInteractionFlags = other->m_textInteractionFlags;
    self->m_selectionStart = other->m_selectionStart;
    self->m_selectionLength = other->m_selectionLength;
    self->m_pressedLink = other->m_pressedLink;
    self->m_hoverLink = other->m_hoverLink;
    self->m_textSelecting = other->m_textSelecting;
    self->m_selectionAnchor = other->m_selectionAnchor;
    self->m_resourceProvider = other->m_resourceProvider;
    self->m_resourceProviderUserData = other->m_resourceProviderUserData;
    self->m_picture = other->m_picture; other->m_picture = NULL;
    XPixmap_move_base(&self->m_pixmap, &other->m_pixmap);
    XPixmap_init(&other->m_pixmap); /* 源像素图保留有效 vtable，可安全析构 */
    self->m_movie = other->m_movie; other->m_movie = NULL;
    self->m_buddy = other->m_buddy; other->m_buddy = NULL;
    self->m_links = other->m_links; other->m_links = NULL;
    self->m_linkCount = other->m_linkCount; other->m_linkCount = 0;
    self->m_linkCapacity = other->m_linkCapacity; other->m_linkCapacity = 0;
    /* 源对象归构造默认值 */
    other->m_isTextLabel = false;
    other->m_selectionStart = -1;
    other->m_selectionLength = 0;
    other->m_pressedLink = -1;
    other->m_hoverLink = -1;
    other->m_textSelecting = false;
    other->m_selectionAnchor = -1;
}

/** @brief 析构：释放文本/显示文本/链接/绘图记录/像素图后交父类。 */
static void VXLabel_deinit(XLabel* self)
{
    if (!self) return;
    label_clearContents(self);
    if (self->m_text) { XString_delete_base((XClass*)self->m_text); self->m_text = NULL; }
    if (self->m_displayText) { XString_delete_base((XClass*)self->m_displayText); self->m_displayText = NULL; }
    XClass_Deinit_Parent(XFrame, (XFrame*)self);
}

/* ==================== 刷新（对标 QLabelPrivate::updateLabel） ==================== */

/** @brief 刷新 sizeHint 失效、heightForWidth 与重绘（对标 updateLabel）。 */
static void label_updateLabel(XLabel* self)
{
    XWidgetSizePolicy policy;
    bool wrap;
    if (!self) return;
    policy = XWidget_sizePolicy((XWidget*)self);
    wrap = self->m_wordWrap != false;
    if (XWidgetSizePolicy_hasHeightForWidth(&policy) != wrap) {
        XWidgetSizePolicy_setHeightForWidth(&policy, wrap);
        XWidget_setSizePolicyFull((XWidget*)self, &policy);
    }
    XWidget_updateGeometry((XWidget*)self);
    XWidget_update((XWidget*)self);
}

/** @brief 按 Qt 规则更新鼠标跟踪（富文本时开启，切回纯文本不关闭）。 */
static void label_updateMouseTracking(XLabel* self)
{
    if (!self) return;
    /* QLabel::setText() 只在生效格式为富文本时调用 setMouseTracking(true)。
       Qt 明确保留从富文本切回纯文本后的跟踪状态，因此这里不能把它
       依据交互标志重新计算后关闭，也不能因默认 LinksAccessibleByMouse
       而额外打开。 */
    if (self->m_effectiveTextFormat != XLabelTextFormat_PlainText)
        XWidget_setMouseTracking((XWidget*)self, true);
}

/* ==================== 生命周期（对标 QLabel 构造/析构） ==================== */

XVtable* XLabel_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLabel)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_Event, VXLabel_event);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXLabel_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXLabel_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXLabel_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXLabel_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXLabel_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXLabel_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VXLabel_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusInEvent, VXLabel_focusInEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_FocusOutEvent, VXLabel_focusOutEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXLabel_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXLabel_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXLabel_deinit);
    return XVTABLE_DEFAULT;
}

void XLabel_init(XLabel* self, XWidget* parent, XWidgetFlags flags)
{
    XWidgetSizePolicy policy;
    if (!self) return;
    memset(self, 0, sizeof(XLabel));
    XFrame_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XLabel);
    self->m_text = XString_create();
    self->m_displayText = XString_create();
    self->m_textFormat = XLabelTextFormat_AutoText;
    self->m_effectiveTextFormat = XLabelTextFormat_PlainText;
    self->m_alignment = XAlignment_Left | XAlignment_VCenter;
    self->m_isTextLabel = false;
    self->m_wordWrap = false;
    self->m_scaledContents = false;
    self->m_openExternalLinks = false;
    self->m_indent = -1;
    self->m_margin = 0;
    self->m_textInteractionFlags =
        XLabelTextInteraction_LinksAccessibleByMouse;
    self->m_selectionStart = -1;
    self->m_selectionLength = 0;
    self->m_pressedLink = -1;
    self->m_hoverLink = -1;
    self->m_textSelecting = false;
    self->m_selectionAnchor = -1;
    XPixmap_init(&self->m_pixmap);
    policy = XWidgetSizePolicy_create_ex(XWidgetSizePolicy_Preferred,
                                         XWidgetSizePolicy_Preferred,
                                         XWidgetSizePolicyControl_Label);
    XWidget_setSizePolicyFull((XWidget*)self, &policy);
    XWidget_setForegroundRole((XWidget*)self, XPaletteColorRole_WindowText);
}

XLabel* XLabel_create_ex(XMemoryType memory, XWidget* parent,
                         XWidgetFlags flags)
{
    XLabel* self = (XLabel*)XMemory_malloc(sizeof(XLabel), memory);
    if (!self) return NULL;
    memset(self, 0, sizeof(XLabel));
    XLabel_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 文本（对标 QLabel text/setText/setNum） ==================== */

const XString* XLabel_text(const XLabel* self)
{
    if (!self) return NULL;
    return self->m_text; /* init/clearContents 后恒为非空空串 */
}

void XLabel_setText(XLabel* self, const XString* text)
{
    XString* copy;
    const char* t;
    if (!self) return;
    if (self->m_text && text &&
        XString_equals(self->m_text, text, XChar_CaseSensitive))
        return;
    copy = text ? XString_create_copy(text) : XString_create();
    if (!copy) return;
    label_clearContents(self);
    XString_assign(self->m_text, copy);
    XString_delete_base((XClass*)copy);
    self->m_isTextLabel = true;
    if (self->m_textFormat == XLabelTextFormat_AutoText) {
        t = XString_toUtf8(self->m_text);
        self->m_effectiveTextFormat = label_mightBeRichText(t)
            ? XLabelTextFormat_RichText : XLabelTextFormat_PlainText;
    } else {
        self->m_effectiveTextFormat = self->m_textFormat;
    }
    label_rebuildDisplay(self);
    label_updateMouseTracking(self);
    label_updateLabel(self);
}

void XLabel_setText_2(XLabel* self, const char* utf8)
{
    XString* s;
    if (!self) return;
    s = XString_create_utf8(utf8 ? utf8 : "");
    if (!s) return;
    XLabel_setText(self, s);
    XString_delete_base((XClass*)s);
}

XLabelTextFormat XLabel_textFormat(const XLabel* self)
{
    return self ? self->m_textFormat : XLabelTextFormat_AutoText;
}

void XLabel_setTextFormat(XLabel* self, XLabelTextFormat format)
{
    const char* t;
    if (!self || self->m_textFormat == format) return;
    self->m_textFormat = format;
    if (self->m_isTextLabel) {
        if (format == XLabelTextFormat_AutoText) {
            t = XString_toUtf8(self->m_text);
            self->m_effectiveTextFormat = label_mightBeRichText(t)
                ? XLabelTextFormat_RichText : XLabelTextFormat_PlainText;
        } else {
            self->m_effectiveTextFormat = format;
        }
        label_rebuildDisplay(self);
        label_updateMouseTracking(self);
        label_updateLabel(self);
    }
}

void XLabel_setNum(XLabel* self, int num)
{
    XString* s;
    if (!self) return;
    s = XString_create();
    if (!s) return;
    XString_setNum_int(s, num, 10);
    XLabel_setText(self, s);
    XString_delete_base((XClass*)s);
}

void XLabel_setNum_2(XLabel* self, double num)
{
    XString* s;
    if (!self) return;
    s = XString_create();
    if (!s) return;
    XString_setNum_double(s, num, 'g', 6);
    XLabel_setText(self, s);
    XString_delete_base((XClass*)s);
}

void XLabel_clear(XLabel* self)
{
    if (!self) return;
    label_clearContents(self);
    label_updateLabel(self);
}

/* ==================== 图像/绘图记录/影片（对标 QLabel） ==================== */

XPixmap XLabel_pixmap(const XLabel* self)
{
    XPixmap out;
    XPixmap_init(&out);
    if (self)
        XPixmap_copy_base(&out, &self->m_pixmap);
    return out;
}

void XLabel_setPixmap(XLabel* self, const XPixmap* pixmap)
{
    if (!self) return;
    if (pixmap && self->m_pixmap.m_data == pixmap->m_data)
        return; /* 同一共享数据：无操作 */
    label_clearContents(self);
    if (pixmap && !XPixmap_isNull(pixmap))
        XPixmap_copy_base(&self->m_pixmap, pixmap);
    label_updateLabel(self);
}

XPicture* XLabel_picture(const XLabel* self)
{
    return self ? self->m_picture : NULL;
}

void XLabel_setPicture(XLabel* self, const XPicture* picture)
{
    XPicture* copy;
    if (!self) return;
    label_clearContents(self);
    if (picture && !XPicture_isNull(picture)) {
        copy = XPicture_create_copy(picture, XCLASS_DEFAULT_MEMORY_TYPE);
        if (copy) self->m_picture = copy;
    }
    label_updateLabel(self);
}

XMovie* XLabel_movie(const XLabel* self)
{
    return self ? self->m_movie : NULL;
}

void XLabel_setMovie(XLabel* self, XMovie* movie)
{
    /* Qt QLabel::setMovie() always clears the previous contents, even when
       the same QMovie pointer is supplied again.  Keep that observable
       reset semantics; the movie itself remains borrowed by the label. */
    if (!self) return;
    label_clearContents(self);
    self->m_movie = movie;
    label_updateLabel(self);
}

XLabelResourceProvider XLabel_resourceProvider(const XLabel* self)
{
    return self ? self->m_resourceProvider : NULL;
}

void* XLabel_resourceProviderUserData(const XLabel* self)
{
    return self ? self->m_resourceProviderUserData : NULL;
}

void XLabel_setResourceProvider(XLabel* self, XLabelResourceProvider provider,
                                void* userData)
{
    if (!self) return;
    self->m_resourceProvider = provider;
    self->m_resourceProviderUserData = userData;
}

/* ==================== 布局属性（对标 QLabel） ==================== */

XAlignments XLabel_alignment(const XLabel* self)
{
    return self ? (self->m_alignment &
                   (XAlignment_VerticalMask | XAlignment_HorizontalMask))
                : (XAlignments)(XAlignment_Left | XAlignment_VCenter);
}

void XLabel_setAlignment(XLabel* self, XAlignments alignment)
{
    XAlignments masked;
    if (!self) return;
    masked = alignment & (XAlignment_VerticalMask | XAlignment_HorizontalMask);
    if (masked == (self->m_alignment &
                   (XAlignment_VerticalMask | XAlignment_HorizontalMask)))
        return;
    self->m_alignment =
        (self->m_alignment & ~(XAlignment_VerticalMask | XAlignment_HorizontalMask)) |
        masked;
    label_updateLabel(self);
}

bool XLabel_wordWrap(const XLabel* self)
{
    return self ? self->m_wordWrap : false;
}

void XLabel_setWordWrap(XLabel* self, bool on)
{
    if (!self || self->m_wordWrap == on) return;
    self->m_wordWrap = on;
    label_updateLabel(self);
}

int XLabel_indent(const XLabel* self)
{
    return self ? self->m_indent : -1;
}

void XLabel_setIndent(XLabel* self, int indent)
{
    if (!self || self->m_indent == indent) return;
    self->m_indent = indent;
    label_updateLabel(self);
}

int XLabel_margin(const XLabel* self)
{
    return self ? self->m_margin : 0;
}

void XLabel_setMargin(XLabel* self, int margin)
{
    if (!self || self->m_margin == margin) return;
    self->m_margin = margin;
    label_updateLabel(self);
}

int XLabel_textPixelSize(const XLabel* self)
{
    return label_pixelSize(self);
}

void XLabel_setTextPixelSize(XLabel* self, int pixelHeight)
{
    XFont source;
    XFont font;
    int h;
    if (!self) return;
    if (pixelHeight <= 0) pixelHeight = XFONT8X16_HEIGHT;
    h = (pixelHeight + XFONT8X16_HEIGHT - 1) / XFONT8X16_HEIGHT;
    if (h < 1) h = 1;
    /* 写入基类 XFont.pixelSize，再经 XWidget_setFont 统一刷新 */
    source = XWidget_font((XWidget*)self);
    XFont_init(&font);
    XFont_copy_base(&font, &source);
    XFont_deinit(&source);
    XFont_setPixelSize(&font, XFont_bitmapScaledSize(XFONT8X16_HEIGHT, h));
    XWidget_setFont((XWidget*)self, &font);
    XFont_deinit(&font);
}

bool XLabel_hasScaledContents(const XLabel* self)
{
    return self ? self->m_scaledContents : false;
}

void XLabel_setScaledContents(XLabel* self, bool on)
{
    if (!self || self->m_scaledContents == on) return;
    self->m_scaledContents = on;
    XWidget_update((XWidget*)self);
}

/* ==================== 尺寸（对标 QLabel sizeHint/minimumSizeHint/heightForWidth） ==================== */

XSize XLabel_sizeHint(const XLabel* self)
{
    XSize out;
    if (!self) { XSize_init(&out, 0, 0); return out; }
    return label_sizeForWidth(self, -1);
}

XSize XLabel_minimumSizeHint(const XLabel* self)
{
    XSize out;
    XSize sh;
    if (!self) { XSize_init(&out, 0, 0); return out; }
    sh = label_sizeForWidth(self, -1);
    if (!self->m_isTextLabel)
        return sh;
    out.height = label_sizeForWidth(self, XWIDGET_MAX_SIZE).height;
    out.width = label_sizeForWidth(self, 0).width;
    if (sh.height < out.height) out.height = sh.height;
    return out;
}

int XLabel_heightForWidth(const XLabel* self, int width)
{
    if (!self) return -1;
    if (self->m_isTextLabel)
        return label_sizeForWidth(self, width).height;
    return XWidget_heightForWidth((XWidget*)self, width);
}

/* ==================== 伙伴与链接（对标 QLabel） ==================== */

XWidget* XLabel_buddy(const XLabel* self)
{
    return self ? self->m_buddy : NULL;
}

void XLabel_setBuddy(XLabel* self, XWidget* buddy)
{
    if (!self) return;
    self->m_buddy = buddy;
}

bool XLabel_openExternalLinks(const XLabel* self)
{
    return self ? self->m_openExternalLinks : false;
}

void XLabel_setOpenExternalLinks(XLabel* self, bool open)
{
    if (!self || self->m_openExternalLinks == open) return;
    self->m_openExternalLinks = open;
}

XLabelTextInteractionFlags XLabel_textInteractionFlags(const XLabel* self)
{
    return self ? self->m_textInteractionFlags
                : (XLabelTextInteractionFlags)0;
}

void XLabel_setTextInteractionFlags(XLabel* self,
                                    XLabelTextInteractionFlags flags)
{
    XWidgetFocusPolicy policy;
    if (!self || self->m_textInteractionFlags == flags) return;
    self->m_textInteractionFlags = flags;
    if (flags & XLabelTextInteraction_LinksAccessibleByKeyboard)
        policy = XWidgetFocusPolicy_StrongFocus;
    else if (flags & (XLabelTextInteraction_TextSelectableByKeyboard |
                      XLabelTextInteraction_TextSelectableByMouse))
        policy = XWidgetFocusPolicy_ClickFocus;
    else
        policy = XWidgetFocusPolicy_NoFocus;
    XWidget_setFocusPolicy((XWidget*)self, policy);
    /* Qt destroys QLabel's text control when neither keyboard nor mouse
       selection remains enabled.  Any active cursor selection therefore
       becomes unavailable through hasSelectedText()/selectedText(). */
    if (self->m_effectiveTextFormat == XLabelTextFormat_PlainText &&
        (flags & (XLabelTextInteraction_TextSelectableByKeyboard |
                  XLabelTextInteraction_TextSelectableByMouse)) == 0u) {
        self->m_selectionStart = -1;
        self->m_selectionLength = 0;
        self->m_selectionAnchor = -1;
        self->m_textSelecting = false;
    }
    label_updateMouseTracking(self);
    XWidget_update((XWidget*)self);
}

/* ==================== 选择（对标 QLabel；程序化子集） ==================== */

void XLabel_setSelection(XLabel* self, int start, int length)
{
    int total;
    if (!self) return;
    /* QLabelPrivate::needTextControl() keeps a control for rich text, for
       selectable text, or whenever the label has a non-NoFocus policy (for
       example LinksAccessibleByKeyboard sets StrongFocus).  Mirror that
       condition so programmatic selection is not rejected merely because
       the selectable flags are absent. */
    if (self->m_effectiveTextFormat == XLabelTextFormat_PlainText &&
        (self->m_textInteractionFlags &
         (XLabelTextInteraction_TextSelectableByMouse |
          XLabelTextInteraction_TextSelectableByKeyboard)) == 0u &&
        XWidget_focusPolicy((XWidget*)self) == XWidgetFocusPolicy_NoFocus)
        return;
    self->m_textSelecting = false;
    if (start < 0 || length < 0) {
        self->m_selectionStart = -1;
        self->m_selectionLength = 0;
        self->m_selectionAnchor = -1;
        XWidget_update((XWidget*)self);
        return;
    }
    total = (int)XString_length_base((XContainer*)self->m_displayText);
    if (start > total) start = total;
    if (length > total - start) length = total - start;
    self->m_selectionStart = start;
    self->m_selectionLength = length;
    self->m_selectionAnchor = start;
    XWidget_update((XWidget*)self);
}

bool XLabel_hasSelectedText(const XLabel* self)
{
    return self && self->m_selectionStart >= 0 &&
           self->m_selectionLength > 0;
}

XString* XLabel_selectedText(const XLabel* self)
{
    int start, len;
    if (!self || self->m_selectionStart < 0 || self->m_selectionLength <= 0)
        return XString_create();
    start = self->m_selectionStart;
    len = self->m_selectionLength;
    return XString_mid(self->m_displayText, (size_t)start, (size_t)len);
}

int XLabel_selectionStart(const XLabel* self)
{
    if (!self || self->m_selectionStart < 0 || self->m_selectionLength <= 0)
        return -1;
    return self->m_selectionStart;
}

/* ==================== 信号（对标 QLabel::linkActivated/linkHovered） ==================== */

void* XLabel_linkActivated_signal(XLabel* self, const XString* link)
{
    if (!self) return (void*)(size_t)XLabel_linkActivated_signal;
    label_emitLinkSignal(self, (size_t)XLabel_linkActivated_signal, link);
    return (void*)(size_t)XLabel_linkActivated_signal;
}

void* XLabel_linkHovered_signal(XLabel* self, const XString* link)
{
    if (!self) return (void*)(size_t)XLabel_linkHovered_signal;
    label_emitLinkSignal(self, (size_t)XLabel_linkHovered_signal, link);
    return (void*)(size_t)XLabel_linkHovered_signal;
}

/* ==================== 离屏绘制（扩展入口） ==================== */

void XLabel_drawContents(XLabel* self, XPainter* painter)
{
    if (!self || !painter) return;
    {
        int saved = XPainter_save(painter);
        if (saved) {
            XFrame_drawFrame((XFrame*)self, painter);
            label_drawContent(self, painter);
            XPainter_restore(painter);
        }
    }
}

#endif /* XWIDGET_ON && XFRAME_ON && XLABEL_ON */
