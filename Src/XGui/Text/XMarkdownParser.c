/**
 * @file       XMarkdownParser.c
 * @brief      XMarkdownParser Markdown 解析落文档实现（对标 Qt 6.8
 *             QTextMarkdownImporter；语义取证与差异契约见 XMarkdownParser.h
 *             头注释——本文件不重复，只补实现级细节）。
 * @details    单遍两级行解析：
 *             - 块级（行循环）：前缀剥离（引用 `>` 逐字符 / 列表内容列）→
 *               行分类（Setext/水平线/ATX/围栏/列表项/缩进代码/段落）→ 经
 *               XTextCursor 落块（openBlock 复用空文档块 0 的 Qt isEmpty
 *               分支、否则 insertBlock；qtmi.cpp insertBlock():618-626）；
 *             - 行内（段缓冲）：段落内容先按行累计进段缓冲（软/硬换行以
 *               哨兵字节 0x01/0x02 承载，哨兵只在行接缝产生），段末统一过
 *               行内扫描器（强调/行内代码/链接/自动链接/实体/转义），以格
 *               式栈（qtmi.cpp m_spanFormatStack:384-386 同构）驱动
 *               XTextCursor_insertTextFormat 落片段；链接文本经子区间重入
 *               扫描（行内状态入栈保存，链接 span 作子扫描基格式）；
 *             - 撤销口径：XTextDocument_clear（清撤销史）→
 *               beginEditBlock … endEditBlock（块内只压一次快照 ⇒ 整次
 *               导入恰好一步撤销；Qt import():162-164 同构，探针证）。
 * @note       依赖面仅限 XTextDocument/XTextCursor/XTextFormat **公共 API**
 *             （含列表格式入口 XTextDocument_setBlockListItem /
 *             setBlockListFormat——与 HTML `<ul type>/<ol start>` 管线共用
 *             同一套块字段，见 setBlockListFormat 注释）；不改这些文件。C99。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XMarkdownParser.h"

#if XGUI_ON && XTEXTDOCUMENT_ON

#include "XTextCursor.h"
#include "XTextFormat.h"
#include "XStringUtils.h"
#include "XMemory.h"
#include "XByteArray.h"

/* ==================== 常量 ==================== */

/** @brief 列表嵌套深度上限（渲染层 listCount[8] 同口径，XTextEdit.c:551）。 */
#define XMD_MAX_LIST_DEPTH 8
/** @brief 行内格式栈深度上限（强调/链接嵌套；越界按普通文本落）。 */
#define XMD_MAX_SPAN_DEPTH 16
/** @brief 链接 href 池容量上限（同次导入链接数；超出按普通文本落）。 */
#define XMD_MAX_HREFS 64
/** @brief 未配对定界符字面化重启上限（病态输入防回环；超限按现样落）。 */
#define XMD_LIT_MAX 32
/** @brief 单轮「被 closer 顶掉的未配对开符」记录上限（跨轮并入字面化集合；
 *         与 XMD_LIT_MAX 同量级——超限由有界钳位口径兜底，尾段发射前
 *         格式栈回滚到入口栈基，样式不会漏到行尾。 */
#define XMD_DROP_MAX 48
/** @brief 软换行哨兵（段缓冲行接缝；源文控制字节不产哨兵）。 */
#define XMD_SOFTBREAK ((char)0x01)
/** @brief 硬换行哨兵（行尾 ≥2 空格或行尾奇数 '\\'；落文档时拆块）。 */
#define XMD_HARDBREAK ((char)0x02)

/* ==================== 类型 ==================== */

/** @brief 列表层级状态（内容列按引用剥离后的行内坐标计）。 */
typedef struct XmdListLevel
{
    int ordered;        /**< 有序列表。 */
    char marker;        /**< 无序标记字符（'-'/'*'/'+'）；有序存分隔符。 */
    int contentIndent;  /**< 内容起始列（标记后首空白末的列；span 坐标）。 */
    int markerIndent;   /**< 标记列（span 坐标）。 */
    int start;          /**< 该层列表的起始序号（有序=首个标记的数字，无序=1；
                             同列表后续项不覆盖——Qt 探针 `1. a\n\n5. b` ⇒
                             同一 QTextList start=1、第二项 itemText「2.」）。
                             尾部追加（结构体初值铁律；XmdParser 整体 memset）。 */
} XmdListLevel;

/** @brief 行内格式状态（落文档时转 XTextCharFormat）。 */
typedef struct XmdSpan
{
    unsigned bold : 1;
    unsigned italic : 1;
    unsigned underline : 1;
    unsigned strikeOut : 1;
    unsigned mono : 1;
    unsigned link : 1;  /**< 锚点标志（href 可为空串：`[x]()` 的 Qt 探针
                             为 A:（isAnchor 且空 href），空串无法经
                             href 指针表达——XString 空串 toUtf8 返回 NULL）。 */
    const char* href;   /**< 链接目标（借用 href 池；NULL=无 href）。 */
} XmdSpan;

/** @brief 强调定界符开栈条目（配对消费用）。 */
typedef struct XmdDelim
{
    char ch;            /**< 定界字符（'*'/'_'）。 */
    int use;            /**< 配对消费长度（=开 run 原长；1=em、2=strong、
                             3=strong+em 同点闭合——探针 ***x*** 三星全收）。 */
    int spanDepth;      /**< 压入时的格式栈深（弹出回到该深）。 */
    int runStart;       /**< 该定界符 run 在段缓冲中的起点（未配对按字面落）。 */
    int runLen;         /**< run 长度。 */
    int dead;           /**< 本次扫描已判不可配对（closer 不足跳过）。 */
} XmdDelim;

/** @brief 字面化定界符位置（未配对重启扫描的跳表条目；单字符粒度）。 */
typedef struct XmdLitPos
{
    int start;
} XmdLitPos;

/** @brief 行切分条目（Setext 前瞻用：主循环先成表再逐行处理）。 */
typedef struct XmdLine
{
    int start;   /**< 行首字节下标（源串内）。 */
    int len;     /**< 行长（不含行终止符；\r\n 已剥 \r）。 */
} XmdLine;

/** @brief 解析器状态（单次 parse_utf8 的栈上载体；无残留）。 */
typedef struct XmdParser
{
    /* ---- 目标 ---- */
    XTextDocument* doc;
    XTextCursor cur;
    int features;

    /* ---- 共享载荷（运行期建、收尾释放） ---- */
    XString* hrefs[XMD_MAX_HREFS];  /**< href 池（行内链接借用；收尾释放）。 */
    int hrefCount;

    /* ---- 块级状态 ---- */
    int quoteDepth;                 /**< 引用嵌套深度（0=不在引用）。 */
    int listDepth;                  /**< 列表嵌套深度（0=不在列表）。 */
    XmdListLevel lists[XMD_MAX_LIST_DEPTH];
    int inFence;                    /**< 1=围栏代码块中；2=缩进代码块中。 */
    char fenceChar;                 /**< 围栏字符（'`'/'~'；缩进代码 0）。 */
    int fenceLen;                   /**< 开栏定界符长度（闭栏需 ≥）。 */
    int codeBlanks;                 /**< 代码块内挂起空行数（qtmi.cpp:524-530
                                         延后发射口径——栏尾空行不落）。 */

    /* ---- 段缓冲 ---- */
    int paraOpen;                   /**< 当前块是可续接段落（含列表项文本）。 */
    int pendingBreak;               /**< 0=无 1=软换行 2=硬换行（上一行尾判定）。 */
    XByteArray* buf;                /**< 段缓冲（框架 XByteArray；哨兵承载换行）。
                                         铁律「框架有的绝不手搓」：原先手写
                                         XMalloc 倍增字节缓冲已按 AGENTS.md
                                         改为复用 XByteArray。 */

    /* ---- 行内状态 ---- */
    XmdSpan spans[XMD_MAX_SPAN_DEPTH];   /**< 格式栈（0 号=块基格式）。 */
    int spanTop;                         /**< 栈深（≥1）。 */
    XmdDelim delims[XMD_MAX_SPAN_DEPTH]; /**< 强调开定界符栈。 */
    int delimTop;
    int dry;   /**< 1=干跑（只探未配对定界符，零发射零入池）——扫描协议
                    为「干跑找字面化集合 → 实跑单遍」，杜绝重启重发射。 */
    int afterBlank; /**< 上一行是空行（惰性续行只允许在无空行时接排——
                         空行后非缩进行结束列表项/引用段，CommonMark 同款；
                         探针 mixed：列表后空行 + "para after list" 成独立
                         段落非列表项）。**尾部追加**（结构体初值铁律）。 */
    int fenceIndent; /**< 开栏围栏的行首缩进列（围栏内容按 min(fenceIndent,
                          行缩进) 剥离——Qt 探针 fence-indent2/1：开栏缩进 2
                          时内容 "  a"→«a»、"   b"→« b»）。 */
    int skipNextLine; /**< 本轮把「下一行」消费掉（Setext 下划线前瞻用：
                          当前段行 + 下一行下划线 ⇒ 本行按标题落、跳过下划线
                          行——Qt 的 H 块在文本写入前就带标题格式，
                          qtextmarkdownimporter.cpp:194-208）。 */
    XmdLitPos drops[XMD_DROP_MAX]; /**< 干跑本轮「被 closer 越过顶掉」的未配对
                                       开符位置（逐字符；并入字面化集合）。
                                       尾部追加（结构体初值铁律）。 */
    int dropCount; /**< drops 有效项数（每轮干跑清零）。尾部追加。 */
    int listOpenDepth; /**< **本行列表层收敛之前**的 listDepth（尾部追加）。
                        用于判定「本行的列表项所在层是否还是同一个列表」：
                        收敛只降 listDepth、旧层条目留着，光看条目无法区分
                        「同列表兄弟项」与「旧列表已被关闭后重开」——
                        `1. a\n\n    5. b\n2. c\n\n    7. d` 里 d 与 b 的
                        层条目同为「Decimal+'.'」但 Qt 中 d 属**新**嵌套列表
                        （探针 /tmp/b5rev_probe2.cpp：I 用例 d='7.'）。 */
} XmdParser;

/* ==================== 前置声明 ==================== */

static void xmd_scanInline(XmdParser* p);
static void xmd_scanBufOnce(XmdParser* p, const XmdLitPos* lit, int litCount);
static void xmd_scanBufFull(XmdParser* p);
static int xmd_indentCols(const char* line, int len, int* col);
static bool xmd_setextLevelAt(const char* line, int i, int len, int* level);
static void xmd_classifyRest(XmdParser* p, const char* line, int len,
                             const char* next, int nextLen, int i, int col);
static int xmd_skipColsInSpan(const char* line, int i, int len, int curCol,
                              int targetCol, int* outCol);

/* ==================== 小工具 ==================== */

/** @brief ASCII 空白判定（空格/制表）。 */
static bool xmd_isSpaceCh(char c)
{
    return c == ' ' || c == '\t';
}

/** @brief ASCII 字母判定。 */
static bool xmd_isAlphaCh(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/** @brief ASCII 数字判定。 */
static bool xmd_isDigitCh(char c)
{
    return c >= '0' && c <= '9';
}

/** @brief ASCII 字母数字判定。 */
static bool xmd_isAlnumCh(char c)
{
    return xmd_isAlphaCh(c) || xmd_isDigitCh(c);
}

/** @brief ASCII 标点判定（CommonMark 可转义/词分隔集合）。 */
static bool xmd_isPunctCh(char c)
{
    switch (c) {
    case '!': case '"': case '#': case '$': case '%': case '&':
    case '\'': case '(': case ')': case '*': case '+': case ',':
    case '-': case '.': case '/': case ':': case ';': case '<':
    case '=': case '>': case '?': case '@': case '[': case '\\':
    case ']': case '^': case '_': case '`': case '{': case '|':
    case '}': case '~':
        return true;
    default:
        return false;
    }
}

/** @brief 换行判定（\n / \r）。 */
static bool xmd_isNlCh(char c)
{
    return c == '\n' || c == '\r';
}

/** @brief UTF-8 序列长度（首字节；非法按 1——逐字节直落不拆散多字节）。 */
static int xmd_utf8SeqLen(const char* s)
{
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

/**
 * @brief  段缓冲追加（按需倍增；分配失败静默丢段——有界子集钳位口径）。
 */
/**
 * @brief  段缓冲追加（框架 XByteArray；首个字节触发建容器）。
 * @details 铁律「框架有的绝不手搓」：环/字节容器一律用框架件——此处原为
 *          手写 XMalloc 倍增缓冲，现复用 XByteArray（XContainer/XByteArray，
 *          与 AGENTS.md 的容器清单同源）。分配失败由 push_back_1 返回 false
 *          静默丢该段（有界钳位口径）。
 */
static void xmd_bufAppend(XmdParser* p, const char* bytes, int len)
{
    int i;
    if (!p || !bytes || len <= 0) return;
    if (!p->buf) {
        p->buf = XByteArray_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, false);
        if (!p->buf) return;
    }
    for (i = 0; i < len; ++i) {
        if (!XByteArray_push_back_1(p->buf, (uint8_t)bytes[i])) return;
    }
}

/** @brief 段缓冲有效长度（无缓冲 0）。 */
static int xmd_bufLen(const XmdParser* p)
{
    if (!p || !p->buf) return 0;
    return (int)XByteArray_size_base((const XContainer*)p->buf);
}

/** @brief 段缓冲字节视图（无缓冲 NULL；非 NUL 结尾——发射路径按长度拷贝）。 */
static const char* xmd_bufData(const XmdParser* p)
{
    if (!p || !p->buf) return NULL;
    return (const char*)XByteArray_data(p->buf);
}

/** @brief 清空段缓冲（保留容器——clear 只清元素保容量）。 */
static void xmd_bufReset(XmdParser* p)
{
    if (!p || !p->buf) return;
    XByteArray_clear_base((XContainer*)p->buf);
}

/** @brief 追加单个字节（软/硬换行哨兵与逐字符内容）。 */
static void xmd_bufAppendCh(XmdParser* p, char c)
{
    xmd_bufAppend(p, &c, 1);
}

/** @brief href 入池（返回池内借用指针；满池/失败 NULL——链接降级普通文本；
 *         干跑入占位指针——真实入池留给实跑）。 */
static const char* xmd_hrefIntern(XmdParser* p, const char* utf8, int len)
{
    XString* s;
    const char* u8;
    if (!p || !utf8 || len <= 0) return NULL;
    if (p->dry) return (const char*)p; /* 干跑：非 NULL 占位，不建串 */
    if (p->hrefCount >= XMD_MAX_HREFS) return NULL;
    s = XString_create();
    if (!s) return NULL;
    XString_assign_with_length_utf8(s, utf8, (size_t)len);
    u8 = XString_toUtf8(s);
    if (!u8) {
        XClassDelete((XClass*)s);
        return NULL;
    }
    p->hrefs[p->hrefCount++] = s;
    return u8;
}

/** @brief 拼串入池（prefix+body；www./mailto 补前缀用——池内借用指针）。 */
static const char* xmd_hrefIntern2(XmdParser* p, const char* prefix,
                                   const char* body, int bodyLen)
{
    XString* s;
    const char* u8;
    if (!p || !body || bodyLen <= 0) return NULL;
    if (p->dry) return (const char*)p; /* 干跑占位（同 hrefIntern） */
    if (p->hrefCount >= XMD_MAX_HREFS) return NULL;
    s = XString_create();
    if (!s) return NULL;
    XString_assign_utf8(s, prefix ? prefix : "");
    XString_append_with_length_utf8(s, body, (size_t)bodyLen);
    u8 = XString_toUtf8(s);
    if (!u8) {
        XClassDelete((XClass*)s);
        return NULL;
    }
    p->hrefs[p->hrefCount++] = s;
    return u8;
}

/* ==================== 格式栈与落盘 ==================== */

/**
 * @brief  当前格式栈顶 → 属性字符格式（qtmi.cpp cbEnterSpan 栈顶继承
 *         :384-386 的等价承载）。
 * @details 字重恒落（700/400——textFormatToCharFormat 的 bold 判定 ≥500，
 *          恒落更确定）；italic/underline/strikeOut 真值才落（false 不落
 *          表，转内联同为 0）。
 */
static void xmd_spanToFormat(const XmdSpan* s, XTextCharFormat* out)
{
    XTextCharFormat_init(out);
    if (!s) return;
    XTextCharFormat_setFontWeight(out, s->bold ? 700 : 400);
    if (s->italic) XTextCharFormat_setFontItalic(out, true);
    if (s->underline) XTextCharFormat_setFontUnderline(out, true);
    if (s->strikeOut) XTextCharFormat_setFontStrikeOut(out, true);
    if (s->mono) XTextCharFormat_setFontFamily(out, "monospace");
    if (s->link) {
        /* IsAnchor 恒落（含空 href 的 `[x]()`）；href 非空才落字符串。
           内联模型只有 anchorHref 一个锚点槽位（XTDCharFormat），空 href
           锚点落不进片段——见 XMarkdownParser.h 的已知差异。 */
        XTextCharFormat_setAnchor(out, true);
        if (s->href && s->href[0])
            XTextCharFormat_setAnchorHref(out, s->href);
    }
}

/**
 * @brief  插入一段「本函数自建的 NUL 结尾串」（栈或堆）到光标处。
 * @details XTextCursor_insertTextFormat 的文本参数是 NUL 结尾 C 串；代码
 *          行/强调文本等**源文切片**不是独立串，直接传会把剩余源文一并
 *          写入（实测：10 字节代码行写进 44 字符）。此处统一补 NUL：短串
 *          走栈缓冲、长串走堆（失败静默丢该段——有界钳位口径）。
 */
static void xmd_insertSlice(XmdParser* p, const char* bytes, int len,
                            const XTextCharFormat* fmt)
{
    char stackBuf[256];
    char* tmp = stackBuf;
    if (!p || !bytes || len <= 0 || p->dry) return;
    if (len + 1 > (int)sizeof(stackBuf)) {
        tmp = (char*)XMalloc_System((size_t)len + 1);
        if (!tmp) return;
    }
    XMemcpy(tmp, bytes, (size_t)len);
    tmp[len] = '\0';
    XTextCursor_insertTextFormat(&p->cur, tmp, fmt);
    if (tmp != stackBuf) XFree_System(tmp);
}

/** @brief 以当前栈顶格式插入 UTF-8 文本（原始发射，无自动链接切分；
 *         干跑零发射——发射只在实跑发生，杜绝重启重发射）。
 * @details 文本参数经 xmd_insertSlice 统一补 NUL 后下发：XTextCursor_-
 *          insertTextFormat 收的是 NUL 结尾 C 串，段缓冲切片不是独立串，
 *          直接传会把剩余缓冲一并写入（实测：发 "soft" 却写入了整段
 *          "soft\\1wrap"）。改为「按长度拷进临时串」后该类隐患整体消失，
 *          也不再需要往缓冲里塞临时 NUL。 */
static void xmd_emitRaw(XmdParser* p, const char* bytes, int len)
{
    XTextCharFormat fmt;
    if (!p || !bytes || len <= 0 || p->dry) return;
    /* 栈实例先整清再交框架（AGENTS.md：栈上局部结构体交给框架一律先
       memset——XTextFormat_init 以「内嵌 XVector 虚表像不像 XVector」
       判重复 init，栈残值恰好是上一轮同址格式对象时会被误判成已初始化
       而走 deinit ⇒ 段错误）。 */
    XMemset(&fmt, 0, sizeof(fmt));
    xmd_spanToFormat(&p->spans[p->spanTop - 1], &fmt);
    xmd_insertSlice(p, bytes, len, &fmt);
    XTextFormat_deinit(&fmt);
}

/** @brief 压入一格格式栈（越界忽略——上限外按普通文本落）。 */
/**
 * @brief  压入一格格式栈。
 * @details **Qt 6.8 语义的关键一步**（qtextmarkdownimporter.cpp:384-386
 *          cbEnterSpan 从 `m_spanFormatStack` 栈顶起、块首该栈为空；
 *          :432-441 cbLeaveSpan 回落栈顶、栈空回落**空格式**）：标题块的
 *          「块基 Bold」只作用于**尚未开过任何 span** 的文本；一旦开了
 *          span，块基即被 span 格式替换，span 关闭后回落空格式。
 *          本机 Qt 6.8.0 offscreen 探针（/tmp/qtprobe_w3a2.cpp）：
 *          `# plain **b** tail` ⇒ «plain »{B}«b»{B}« tail»{}；
 *          `# a *i* b` ⇒ «a »{B}«i»{I}« b»{}；
 *          `a [x](u) *em*\n===` ⇒ «a »{B}«x»{A:u}« »{}«em»{I}。
 *          故在 spanTop==1（本块首个 span）时先把块基清零再继承。
 */
static void xmd_pushSpan(XmdParser* p, const XmdSpan* mod)
{
    XmdSpan top;
    if (!p) return;
    if (p->spanTop >= XMD_MAX_SPAN_DEPTH) return;
    if (p->spanTop <= 1)
        XMemset(&p->spans[0], 0, sizeof(p->spans[0])); /* 块基失效（Qt 同） */
    top = p->spans[p->spanTop - 1];
    if (mod->bold) top.bold = 1;
    if (mod->italic) top.italic = 1;
    if (mod->underline) top.underline = 1;
    if (mod->strikeOut) top.strikeOut = 1;
    if (mod->mono) top.mono = 1;
    if (mod->link) top.link = 1;
    if (mod->href) top.href = mod->href;
    p->spans[p->spanTop++] = top;
}

static void xmd_popSpan(XmdParser* p)
{
    if (p && p->spanTop > 1) p->spanTop--;
}

/**
 * @brief  记一个「被 closer 顶掉的未配对开符」的逐字符位置（干跑用）。
 * @details 标准强调算法里 closer 配对到**更里层之外**的开符时，夹在中间
 *          的开符会从定界符栈上删除——它们没有闭合，按 Qt/md4c 口径必须
 *          按**字面文本**输出（md4c 只对成对 span 发 cbEnterSpan/
 *          cbLeaveSpan，qtextmarkdownimporter.cpp:382-441；未配对定界符
 *          走 MD_TEXT 原样落）。不记录则这些字符被静默吞掉，且其格式在
 *          实跑里根本不存在——文本样式随配对的另一开符走样（实测
 *          `**a _b**` 曾落成 «a »{B}«b»{B,U}，Qt 为 «a _b»{B}）。
 * @note   只记位置不贪心：真正字面化由 xmd_scanBufFull 的干跑收敛循环完成。
 */
static void xmd_recordDrop(XmdParser* p, const XmdDelim* op)
{
    int k;
    if (!p || !op || op->runLen <= 0) return;
    for (k = 0; k < op->runLen && p->dropCount < XMD_DROP_MAX; ++k) {
        p->drops[p->dropCount].start = op->runStart + k;
        p->dropCount++;
    }
}

/** @brief 释放解析器运行期载荷（段缓冲/href 池；幂等）。 */
static void xmd_parserClear(XmdParser* p)
{
    int i;
    if (!p) return;
    if (p->buf) {
        XClassDelete((XClass*)p->buf);
        p->buf = NULL;
    }
    for (i = 0; i < p->hrefCount; ++i) {
        if (p->hrefs[i]) XClassDelete((XClass*)p->hrefs[i]);
        p->hrefs[i] = NULL;
    }
    p->hrefCount = 0;
}

/* ==================== 块写入 ==================== */

/**
 * @brief  开新块（对标 qtmi.cpp insertBlock() 助手 :583-634）。
 * @details 结构判空（blockCount==1——Qt isEmpty 是结构判据 length()<=1，
 *          文本判空会把「块 0 是水平线」误判成可复用；探针 thematic-variants
 *          证实水平线也建块）⇒ 复用块 0 直接设块格式（:618-620 分支），
 *          否则 insertBlock（光标落新块首）。块格式整值替换（BlockIndent/
 *          HeadingLevel；alignment 不设=缺省 AlignLeft）——拆块继承的残值
 *          同拍清掉。列表项另经 setBlockListItem 记归属+有序位（与 HTML
 *          `<li>` 管线同承载）；列表三属性（style/start/NumberSuffix）与
 *          「同层新列表」位由调用方在本函数之后经
 *          XTextDocument_setBlockListFormat 落块（列表分支内联，保持
 *          openBlock 的签名不变）。
 * @param  heading 0=非标题，1..6=ATX/Setext 级。
 * @param  listItem 是否列表项；ordered 有序位。
 * @param  indentLevel 段缩进（列表项=深度[+引用深度]；非列表=引用深度）。
 * @param  forceNewBlock 水平线路径恒建新块（qtmi.cpp:297-302 HR 分支无判空
 *         直接 insertBlock——探针 thematic-variants 首线也建空块）。
 */
static void xmd_openBlock(XmdParser* p, int heading, bool listItem,
                          bool ordered, int indentLevel, bool forceNewBlock)
{
    XTextBlockFormat bfmt;
    if (!p) return;
    /* Qt isEmpty 口径（length()<=1）：块 0 已落文本即非空——复用分支只在
       「结构上只有初始块且无任何文本」时成立（水平线先建块 ⇒ blockCount=2
       ⇒ 不复用，探针 thematic-variants 首线也建空块）。 */
    if (forceNewBlock || p->doc->m_blockCount != 1 ||
        !XTextDocument_isEmpty(p->doc))
        XTextCursor_insertBlock(&p->cur);
    /* 栈实例先整清再交框架（AGENTS.md 栈结构体铁律；XTextFormat_init 的
       「重复 init 先释放」探测在内嵌 XVector 虚表槽恰好是上一轮同址格式
       对象的残值时会把垃圾判成已初始化 ⇒ deinit 崩）。 */
    XMemset(&bfmt, 0, sizeof(bfmt));
    XTextFormat_init(&bfmt, XTextFormatType_BlockFormat);
    if (indentLevel > 0) XTextBlockFormat_setIndent(&bfmt, indentLevel);
    if (heading > 0) XTextBlockFormat_setHeadingLevel(&bfmt, heading);
    XTextCursor_setBlockFormat(&p->cur, &bfmt);
    XTextFormat_deinit(&bfmt);
    if (listItem) {
        XTextDocument_setBlockListItem(
            p->doc, XTextCursor_blockNumber(&p->cur), true, ordered);
    } else {
        /* 拆块会继承原块的列表标志（xtd_blockSplitAt 深拷 isListItem/
           isOrdered）——非列表块必须显式清回（否则列表后空行段落被标成
           列表项；探针 mixed："para after list" 非列表项）。仅在确有残值时
           清一次，避免同值 setBlockListItem 白压快照。 */
        const XTDBlock* blk =
            XTextDocument_findBlockByNumber(p->doc,
                                            XTextCursor_blockNumber(&p->cur));
        if (blk && (blk->isListItem || blk->isOrdered))
            XTextDocument_setBlockListItem(
                p->doc, XTextCursor_blockNumber(&p->cur), false, false);
    }
    /* 块基格式复位（标题块基=粗体——qtmi.cpp:200 MD_BLOCK_H 落 Bold）。 */
    p->spans[0].bold = (heading > 0) ? 1u : 0u;
    p->spans[0].italic = 0;
    p->spans[0].underline = 0;
    p->spans[0].strikeOut = 0;
    p->spans[0].mono = 0;
    p->spans[0].href = NULL;
    p->spanTop = 1;
    p->delimTop = 0;
    p->paraOpen = 1;
    p->pendingBreak = 0;
    p->afterBlank = 0;
    xmd_bufReset(p);
}

/**
 * @brief  关段：段缓冲行内扫描落盘并复位段态。
 * @details 挂起的软/硬换行在段终点丢弃（行尾标记只对「后随同段行」生效——
 *          探针 hardbreak：末行 "break" 干净无尾块）。
 */
static void xmd_flushParagraph(XmdParser* p)
{
    if (!p) return;
    if (xmd_bufLen(p) > 0) {
        xmd_scanInline(p);
        xmd_bufReset(p);
    }
    p->paraOpen = 0;
    p->pendingBreak = 0;
    p->spanTop = 1;
    p->delimTop = 0;
}

/**
 * @brief  Setext 下划线行判定（前瞻用）：≤3 缩进 + 单一 '='/'-' run + 尾空白。
 * @details 与 xmd_setextLevelAt 的区别：**输入是整行**（含缩进），供主循环
 *          在「当前段行的下一行」上做前瞻——Qt 的 H 块在写文本前就带标题
 *          格式（qtextmarkdownimporter.cpp:194-208 的 `insertBlock(blockFmt,
 *          charFmt)`），因此 Setext 不能事后改格式（事后整块 setCharFormat
 *          会抹掉行内 span 的 href/italic——本机 Qt 探针
 *          `a [x](u) *em*\n===` ⇒ «a »{B}«x»{A:u}« »{}«em»{I}）。
 */
static bool xmd_setextUnderlineLine(const char* line, int len, int* level)
{
    int col;
    int i;
    if (!line) return false;
    i = xmd_indentCols(line, len, &col);
    if (col > 3) return false;
    return xmd_setextLevelAt(line, i, len, level);
}

/**
 * @brief  代码行落盘（逐行一块、原文空白保留、等宽；qtmi.cpp:607
 *         charFormat.setFont(m_monoFont) 口径——字族 monospace）。
 * @details 不经段缓冲（无行内语法）；空行落空块（探针 code-fence：两个空
 *          行各成空块）。
 */
static void xmd_emitCodeLine(XmdParser* p, const char* bytes, int len)
{
    XTextCharFormat fmt;
    int indent;
    if (!p) return;
    /* 列表项内代码块：块归属该列表项（Qt 探针 fence-in-list：代码块在
       列表内、listInd=1——块模型承载口径与 `<li><pre>` 同）。 */
    indent = (p->listDepth > 0)
                 ? ((p->quoteDepth > 0 ? p->quoteDepth : 0) + p->listDepth)
                 : 0;
    xmd_openBlock(p, 0, p->listDepth > 0, false, indent, false);
    p->paraOpen = 0;
    if (len <= 0) return;
    XMemset(&fmt, 0, sizeof(fmt)); /* 栈实例先整清（同 xmd_emitRaw 注）。 */
    XTextCharFormat_init(&fmt);
    XTextCharFormat_setFontWeight(&fmt, 400);
    XTextCharFormat_setFontFamily(&fmt, "monospace");
    xmd_insertSlice(p, bytes, len, &fmt); /* 源文切片：补 NUL 再插 */
    XTextFormat_deinit(&fmt);
}

/** @brief 水平线落盘：恒建新块（空块；qtmi.cpp:297-302 无判空直接
 *         insertBlock——探针 thematic-variants 首线也建空块）。 */
static void xmd_emitHr(XmdParser* p)
{
    if (!p) return;
    xmd_openBlock(p, 0, false, false, 0, true);
    p->paraOpen = 0;
}

/** @brief 代码块内挂起空行落盘（下一内容行到来时冲刷）。 */
static void xmd_flushCodeBlanks(XmdParser* p)
{
    while (p && p->codeBlanks > 0) {
        xmd_emitCodeLine(p, "", 0);
        p->codeBlanks--;
    }
}

/* ==================== 行分类原语 ==================== */

/**
 * @brief  行首缩进列数（tab=4 列；CommonMark 列口径）。
 * @return 首个非空白字节下标；[out] col=缩进列。
 */
static int xmd_indentCols(const char* line, int len, int* col)
{
    int i = 0;
    int c = 0;
    while (i < len) {
        if (line[i] == ' ') { c++; i++; }
        else if (line[i] == '\t') { c += 4 - (c % 4); i++; }
        else break;
    }
    *col = c;
    return i;
}

/** @brief 从 [i,len) 起是否全空白。 */
static bool xmd_isBlankFrom(const char* line, int i, int len)
{
    while (i < len) {
        if (!xmd_isSpaceCh(line[i])) return false;
        i++;
    }
    return true;
}

/**
 * @brief  引用标记剥离（原始位）：0-3 空白 + '>'（可叠层，每层后随一个
 *         空白并入）。
 * @return 剥离层数；[out] ni=标记后位置（**未跳后续空白**——围栏内容按列
 *         精确剥离要用原始位）、ncol=该位置的列（span 坐标）。
 */
static int xmd_stripQuoteRaw(const char* line, int len, int* ni, int* ncol)
{
    int depth = 0;
    int j = 0;
    int col = 0;
    for (;;) {
        int k;
        int c;
        k = xmd_indentCols(line + j, len - j, &c);
        if (col + c > 3) break;
        k += j;
        if (k >= len || line[k] != '>') break;
        k++;
        if (k < len && line[k] == ' ') k++;
        else if (k < len && line[k] == '\t') k++;
        depth++;
        col += c + 1;
        j = k;
    }
    *ni = j;
    *ncol = col;
    return depth;
}

/**
 * @brief  引用标记剥离（跳到首个非空白）：普通行路径用。
 * @return 剥离层数；[out] ni=首个非空白下标、ncol=该处列（span 坐标）。
 */
static int xmd_stripQuote(const char* line, int len, int* ni, int* ncol)
{
    int depth;
    int ri;
    int rc;
    int extra;
    int col;
    depth = xmd_stripQuoteRaw(line, len, &ri, &rc);
    extra = xmd_indentCols(line + ri, len - ri, &col);
    *ni = ri + extra;
    *ncol = rc + col;
    return depth;
}

/**
 * @brief  在剥引用后的行内坐标里按列跳前导空白（列表内容列/代码列剥离）。
 * @return 新下标；[out] outCol=新列。已在目标列或遇非空白则原样返回。
 */
static int xmd_skipColsInSpan(const char* line, int i, int len, int curCol,
                              int targetCol, int* outCol)
{
    int c = curCol;
    while (i < len && c < targetCol) {
        if (line[i] == ' ') { c++; i++; }
        else if (line[i] == '\t') {
            int adv = 4 - (c % 4);
            if (c + adv > targetCol) break; /* tab 越目标列：不剥（内容缩进） */
            c += adv;
            i++;
        } else break;
    }
    *outCol = c;
    return i;
}

/**
 * @brief  从 i 起最多剥 n 列行首空白（空格/制表，tab 按 4 列展开；不足 n
 *         列时剥到首个非空白）。
 * @return 新下标。用于围栏内容的「开栏缩进」剥离（Qt 探针 fence-indent*）。
 */
static int xmd_stripUpToCols(const char* line, int i, int len, int n)
{
    int col = 0;
    while (i < len && col < n) {
        if (line[i] == ' ') { col++; i++; }
        else if (line[i] == '\t') {
            int adv = 4 - (col % 4);
            if (col + adv > n) break; /* tab 跨过目标列：不剥（保留内容空白） */
            col += adv;
            i++;
        } else break;
    }
    return i;
}

/**
 * @brief  ATX 标题判定：缩进 0-3 + 1-6 个 '#' +（空白/EOL 或宽容档任意）。
 * @return [out] level=级、contentStart=内容起点（标记后空白已跳）。
 */
static bool xmd_atxMatch(const char* line, int i, int len, int col,
                         int features, int* level, int* contentStart)
{
    int hashes = 0;
    int j;
    if (col > 3) return false;
    j = i;
    while (j < len && line[j] == '#') { hashes++; j++; }
    if (hashes < 1 || hashes > 6) return false;
    if (j < len && !xmd_isSpaceCh(line[j])) {
        if (!(features & (int)XMarkdownFeature_PermissiveATXHeaders))
            return false;
    }
    *level = hashes;
    while (j < len && xmd_isSpaceCh(line[j])) j++;
    *contentStart = j;
    return true;
}

/** @brief 围栏开栏判定：0-3 缩进 + ≥3 同字符（`/~）；` 围栏信息串禁 `。 */
static bool xmd_fenceOpenAt(const char* line, int i, int len, int col,
                            char* fenceChar, int* fenceLen)
{
    char ch;
    int n = 0;
    int j;
    if (col > 3) return false;
    if (i >= len) return false;
    ch = line[i];
    if (ch != '`' && ch != '~') return false;
    j = i;
    while (j < len && line[j] == ch) { n++; j++; }
    if (n < 3) return false;
    if (ch == '`') {
        int k;
        for (k = j; k < len; ++k)
            if (line[k] == '`') return false;
    }
    *fenceChar = ch;
    *fenceLen = n;
    return true;
}

/** @brief 围栏闭栏判定：0-3 缩进 + ≥开栏长度同字符 + 余下全空白。 */
static bool xmd_fenceCloseAt(const char* line, int i, int len, int col,
                             char fenceChar, int fenceLen)
{
    int n = 0;
    int j;
    if (col > 3) return false;
    if (fenceChar == 0) return false;
    if (i >= len || line[i] != fenceChar) return false;
    j = i;
    while (j < len && line[j] == fenceChar) { n++; j++; }
    if (n < fenceLen) return false;
    return xmd_isBlankFrom(line, j, len);
}

/** @brief 水平线判定：≥3 同字符（- *_），字符间允许空白，无其它内容。 */
static bool xmd_isThematicAt(const char* line, int i, int len)
{
    char ch = 0;
    int n = 0;
    int j = i;
    while (j < len) {
        char c = line[j];
        if (c == '-' || c == '*' || c == '_') {
            if (ch && c != ch) return false;
            ch = c;
            n++;
        } else if (!xmd_isSpaceCh(c)) {
            return false;
        }
        j++;
    }
    return n >= 3;
}

/**
 * @brief  Setext 下划线判定：单一 '='（一级）或单一 '-'（二级）连续 run
 *         （中间不得夹空白——"- - -" 是水平线不是 Setext）+ 尾空白。
 */
static bool xmd_setextLevelAt(const char* line, int i, int len, int* level)
{
    char ch;
    int n = 0;
    int j = i;
    if (i >= len) return false;
    ch = line[i];
    if (ch != '=' && ch != '-') return false;
    while (j < len && line[j] == ch) { n++; j++; }
    if (n < 1) return false;
    if (!xmd_isBlankFrom(line, j, len)) return false;
    *level = (ch == '=') ? 1 : 2;
    return true;
}

/**
 * @brief  列表项标记判定（无序 - * +；有序 1-9 位数字 + . 或 ))。
 * @return 1=可识别；[out] contentStart=内容起点、ci=内容起始列（span 坐标）、
 *         ordered、start（有序起始号）、delim（有序分隔符）、ul（无序符）。
 * @details 标记后需空白或 EOL（CommonMark；"-" 单独成空项）；标记列上限
 *          7（嵌套 8 层的内容列已超 7 时按普通段落落——有界钳制）。
 */
static bool xmd_listMarkerAt(const char* line, int i, int len, int col,
                             int* contentStart, int* ci, bool* ordered,
                             int* start, char* delim, char* ul)
{
    int j = i;
    if (col > 7) return false;
    if (j < len && (line[j] == '-' || line[j] == '*' || line[j] == '+')) {
        char m = line[j];
        j++;
        if (j < len && !xmd_isSpaceCh(line[j])) return false;
        *ordered = false;
        *start = 1;
        *delim = 0;
        *ul = m;
        while (j < len && xmd_isSpaceCh(line[j])) j++;
        *contentStart = j;
        *ci = col + (j - i);
        return true;
    }
    {
        int digits = 0;
        long val = 0;
        while (j < len && xmd_isDigitCh(line[j]) && digits < 9) {
            val = val * 10 + (line[j] - '0');
            digits++;
            j++;
        }
        if (digits < 1) return false;
        if (j >= len || (line[j] != '.' && line[j] != ')')) return false;
        j++;
        if (j < len && !xmd_isSpaceCh(line[j])) return false;
        *ordered = true;
        *start = (int)val;
        *delim = line[j - 1];
        *ul = 0;
        while (j < len && xmd_isSpaceCh(line[j])) j++;
        *contentStart = j;
        *ci = col + (j - i);
        return true;
    }
}

/** @brief 任务列表标记剥除（GitHub 档）：内容以 "[ ] "/"[x] "/"[X] " 开头
 *         （标记本身无模型槽位——Qt 落 BlockMarker 属性；剥除后纯文本与
 *         Qt 探针 tasklist-github 的 toPlainText 一致）。 */
static int xmd_stripTaskMarker(const char* line, int i, int len, int features)
{
    if (!(features & (int)XMarkdownFeature_Tasklists)) return i;
    if (i + 2 < len && line[i] == '[' &&
        (line[i + 1] == ' ' || line[i + 1] == 'x' || line[i + 1] == 'X') &&
        line[i + 2] == ']' && i + 3 < len && xmd_isSpaceCh(line[i + 3])) {
        int j = i + 3;
        while (j < len && xmd_isSpaceCh(line[j])) j++;
        return j;
    }
    return i;
}

/**
 * @brief  惰性续行判定：行不是任何「能打断段落」的新块启动子（标题/围栏/
 *         水平线/Setext/列表标记均打断；缩进代码打不断段——CommonMark）。
 */
static bool xmd_lazyJoinable(const char* line, int i, int len, int col,
                             int features)
{
    int lvl;
    int cs;
    bool ordered;
    int start;
    char delim;
    char ul;
    char fc;
    int fl;
    if (xmd_atxMatch(line, i, len, col, features, &lvl, &cs)) return false;
    if (xmd_fenceOpenAt(line, i, len, col, &fc, &fl)) return false;
    if (xmd_isThematicAt(line, i, len)) return false;
    if (xmd_setextLevelAt(line, i, len, &lvl)) return false;
    if (xmd_listMarkerAt(line, i, len, col, &cs, &lvl, &ordered, &start,
                         &delim, &ul)) return false;
    return true;
}

/**
 * @brief  段缓冲追加一行内容（裁两端空白 + 判定行尾换行档位）。
 * @details 行尾 ≥2 空格 ⇒ 硬换行（探针 hardbreak）；行尾奇数个 '\\' ⇒
 *          硬换行且最后一个反斜杠不落（探针 backslash-break；偶数个=
 *          成对转义字面）；否则软换行（探针 softbreak → 单空格）。档位记
 *          入 pendingBreak，在下一行续接时补写哨兵、段终点（flush）丢弃。
 */
static void xmd_appendParaLine(XmdParser* p, const char* line, int i, int len)
{
    int end = len;
    int tailSpaces;
    int backslashes = 0;
    bool hard = false;
    /* 挂起换行补写（上一行的行尾档位）。 */
    if (p->pendingBreak == 2) xmd_bufAppendCh(p, XMD_HARDBREAK);
    else if (p->pendingBreak == 1) xmd_bufAppendCh(p, XMD_SOFTBREAK);
    p->pendingBreak = 0;
    /* 行尾空白数与反斜杠数（对原始行尾判定）。 */
    while (end > 0 && xmd_isSpaceCh(line[end - 1])) end--;
    tailSpaces = len - end;
    while (end - backslashes > i && line[end - 1 - backslashes] == '\\')
        backslashes++;
    if (tailSpaces >= 2) hard = true;
    else if ((backslashes & 1) == 1) hard = true;
    if ((backslashes & 1) == 1) end--; /* 硬换行反斜杠不落 */
    while (i < len && xmd_isSpaceCh(line[i])) i++; /* 内容首空白剥除 */
    if (end > i) xmd_bufAppend(p, line + i, end - i);
    p->pendingBreak = hard ? 2 : 1;
    p->afterBlank = 0; /* 段落有续行 ⇒ 空行隔离解除 */
}

/* ==================== 实体解码 ==================== */

/** @brief 常用命名实体（HTML5 全表 2000+ 的常用档；Qt 走 insertHtml 全表
 *         qtmi.cpp:482-487——本子集可取证的常用档）。 */
typedef struct XmdEntity
{
    const char* name;
    const char* utf8;
} XmdEntity;

static const XmdEntity xmd_entities[] = {
    { "amp", "&" },   { "lt", "<" },    { "gt", ">" },    { "quot", "\"" },
    { "apos", "'" },  { "nbsp", "\xC2\xA0" },             { "copy", "\xC2\xA9" },
    { "reg", "\xC2\xAE" },              { "trade", "\xE2\x84\xA2" },
    { "hellip", "\xE2\x80\xA6" },       { "mdash", "\xE2\x80\x94" },
    { "ndash", "\xE2\x80\x93" },        { "lsquo", "\xE2\x80\x98" },
    { "rsquo", "\xE2\x80\x99" },        { "ldquo", "\xE2\x80\x9C" },
    { "rdquo", "\xE2\x80\x9D" },        { "laquo", "\xC2\xAB" },
    { "raquo", "\xC2\xBB" },            { "deg", "\xC2\xB0" },
    { "plusmn", "\xC2\xB1" },           { "times", "\xC3\x97" },
    { "divide", "\xC3\xB7" },           { "middot", "\xC2\xB7" },
    { "sect", "\xC2\xA7" },             { "para", "\xC2\xB6" },
    { "euro", "\xE2\x82\xAC" },         { "bull", "\xE2\x80\xA2" },
    { "sup2", "\xC2\xB2" },             { "sup3", "\xC2\xB3" },
    { "frac12", "\xC2\xBD" },           { "frac14", "\xC2\xBC" },
    { "frac34", "\xC2\xBE" },           { "dagger", "\xE2\x80\xA0" },
    { "Dagger", "\xE2\x80\xA1" },       { "permil", "\xE2\x80\xB0" },
    { "prime", "\xE2\x80\xB2" },        { "Prime", "\xE2\x80\xB3" },
    { "larr", "\xE2\x86\x90" },         { "rarr", "\xE2\x86\x92" },
    { "uarr", "\xE2\x86\x91" },         { "darr", "\xE2\x86\x93" },
    { "harr", "\xE2\x86\x94" },         { NULL, NULL }
};

/** @brief 命名/数字实体解码（非法序列返回 0——调用方按字面 '&' 落）。 */
static int xmd_decodeEntity(const char* s, int len, char out[8])
{
    int i = 1;
    int n;
    if (len < 3 || s[0] != '&') return 0;
    if (s[i] == '#') {
        unsigned long cp = 0;
        int hex = 0;
        i++;
        if (i < len && (s[i] == 'x' || s[i] == 'X')) { hex = 1; i++; }
        n = 0;
        while (i < len && s[i] != ';' && n < 8) {
            char c = s[i];
            unsigned long d;
            if (hex) {
                if (xmd_isDigitCh(c)) d = (unsigned long)(c - '0');
                else if (c >= 'a' && c <= 'f') d = (unsigned long)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') d = (unsigned long)(c - 'A' + 10);
                else return 0;
            } else {
                if (!xmd_isDigitCh(c)) return 0;
                d = (unsigned long)(c - '0');
            }
            cp = cp * (hex ? 16u : 10u) + d;
            if (cp > 0x10FFFFul) return 0;
            i++;
            n++;
        }
        if (i >= len || s[i] != ';' || n == 0) return 0;
        if (cp == 0 || (cp >= 0xD800ul && cp <= 0xDFFFul)) return 0;
        if (cp < 0x80ul) {
            out[0] = (char)cp;
            out[1] = '\0'; /* NUL 结尾（调用方按 C 串发射） */
            return 1;
        } else if (cp < 0x800ul) {
            out[0] = (char)(0xC0 | (cp >> 6));
            out[1] = (char)(0x80 | (cp & 0x3F));
            out[2] = '\0';
            return 2;
        } else if (cp < 0x10000ul) {
            out[0] = (char)(0xE0 | (cp >> 12));
            out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[2] = (char)(0x80 | (cp & 0x3F));
            out[3] = '\0';
            return 3;
        }
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        out[4] = '\0';
        return 4;
    }
    {
        const XmdEntity* e;
        char name[32];
        n = 0;
        while (i < len && s[i] != ';' && n < 31) {
            if (!xmd_isAlphaCh(s[i]) && !xmd_isDigitCh(s[i])) return 0;
            name[n++] = s[i];
            i++;
        }
        if (i >= len || s[i] != ';' || n == 0) return 0;
        name[n] = '\0';
        for (e = xmd_entities; e->name; ++e) {
            if (XStrcmp(name, e->name) == 0) {
                int l = XStrlen(e->utf8);
                if (l > 6) return 0;
                XMemcpy(out, e->utf8, (size_t)l);
                out[l] = '\0'; /* NUL 结尾（调用方按 C 串发射） */
                return l;
            }
        }
    }
    return 0;
}

/* ==================== 宽容自动链接 ==================== */

/** @brief 词边界判定（自动链接起点；前序字节为空白/括号/行首）。 */
static bool xmd_autoLinkBoundary(const char* s, int i)
{
    if (i == 0) return true;
    return xmd_isSpaceCh(s[i - 1]) || s[i - 1] == '(' || s[i - 1] == '>';
}

/**
 * @brief  宽容自动链接切分发射（GitHub 档；qtmi 旗标 0x4/0x8/0x400）。
 * @details http(s):// 与 www. 起点（词边界）即切；URL 段取到空白/尖括号/
 *          行尾；www. 的 href 补 http:// 前缀（探针 bare-autolink-gh）；
 *          邮箱为 local@domain.tld 简化形（href 补 mailto: 前缀）。
 */
static void xmd_emitPlainAutoLink(XmdParser* p, const char* s, int len)
{
    int i = 0;
    int run = 0;
    int features;
    bool permUrl;
    bool permWww;
    bool permMail;
    if (!p || len <= 0) return;
    features = p->features;
    permUrl = (features & (int)XMarkdownFeature_PermissiveURLAutoLinks) != 0;
    permWww = (features & (int)XMarkdownFeature_PermissiveWWWAutoLinks) != 0;
    permMail = (features & (int)XMarkdownFeature_PermissiveMailAutoLinks) != 0;
    if (!permUrl && !permWww && !permMail) {
        xmd_emitRaw(p, s, len);
        return;
    }
    while (i < len) {
        int tokLen = 0;
        int kind = 0; /* 1=scheme url 2=www 3=mail */
        if (permUrl && xmd_autoLinkBoundary(s, i)) {
            if (i + 7 <= len && XStrncmp(s + i, "http://", 7) == 0) kind = 1;
            else if (i + 8 <= len && XStrncmp(s + i, "https://", 8) == 0)
                kind = 1;
        }
        if (kind == 0 && permWww && xmd_autoLinkBoundary(s, i) &&
            i + 5 <= len && XStrncmp(s + i, "www.", 4) == 0 &&
            xmd_isAlnumCh(s[i + 4])) {
            kind = 2;
        }
        if (kind == 0 && permMail && xmd_autoLinkBoundary(s, i)) {
            int j = i;
            int at = -1;
            while (j < len && !xmd_isSpaceCh(s[j]) && s[j] != '<' &&
                   s[j] != '>') {
                if (s[j] == '@' && at < 0) at = j;
                j++;
            }
            if (at > i && at + 2 < j && xmd_isAlnumCh(s[at + 1]) &&
                xmd_isAlnumCh(s[j - 1])) {
                int k;
                int dotAfter = 0;
                for (k = at + 1; k < j; ++k)
                    if (s[k] == '.') dotAfter = 1;
                if (dotAfter) {
                    kind = 3;
                    tokLen = j - i;
                }
            }
        }
        if (kind == 0) {
            i += xmd_utf8SeqLen(s + i);
            continue;
        }
        if (kind != 3) {
            /* URL 段：到空白/尖括号/行尾。 */
            int k = i;
            while (k < len && !xmd_isSpaceCh(s[k]) && s[k] != '<' &&
                   s[k] != '>') {
                k++;
            }
            tokLen = k - i;
        }
        if (tokLen <= 0) {
            i++;
            continue;
        }
        if (i > run) xmd_emitRaw(p, s + run, i - run);
        {
            const char* href = NULL;
            XmdSpan link;
            if (kind == 2) href = xmd_hrefIntern2(p, "http://", s + i, tokLen);
            else if (kind == 3)
                href = xmd_hrefIntern2(p, "mailto:", s + i, tokLen);
            else href = xmd_hrefIntern(p, s + i, tokLen);
            if (href) {
                XMemset(&link, 0, sizeof(link));
                link.link = 1;
                link.href = href;
                xmd_pushSpan(p, &link);
                xmd_emitRaw(p, s + i, tokLen);
                xmd_popSpan(p);
            } else {
                xmd_emitRaw(p, s + i, tokLen);
            }
        }
        i += tokLen;
        run = i;
    }
    if (run < len) xmd_emitRaw(p, s + run, len - run);
}

/** @brief 普通文本发射（宽容自动链接切分统一入口；非宽容档直落）。 */
static void xmd_emitPlain(XmdParser* p, const char* s, int len)
{
    if (!p || len <= 0) return;
    if (p->features & (int)XMarkdownFeature_PermissiveAutoLinks)
        xmd_emitPlainAutoLink(p, s, len);
    else
        xmd_emitRaw(p, s, len);
}

/* ==================== 行内扫描器 ==================== */

/** @brief 字面化位置命中判定（未配对重启扫描的跳表）。 */
static bool xmd_litHit(const XmdLitPos* lit, int litCount, int pos)
{
    int k;
    for (k = 0; k < litCount; ++k)
        if (lit[k].start == pos) return true;
    return false;
}

/**
 * @brief  行内扫描单遍（段缓冲 → 片段；发射随 p->dry 门控）。
 * @details 未配对定界符留在 p->delimTop 供调用方字面化；被 closer 越过顶掉的
 *          开符记入 p->drops（见 xmd_recordDrop）。**不做重启**——重启重发射
 *          是段重复的根源（发射只在实跑，见 xmd_scanBufFull）。
 * @note    `spanBase` = 进入时的格式栈深（块基=1 / 链接 span 基）；扫描终点
 *          仍挂着的 span 必是未配对开符，**尾段发射前回滚到该栈基**——字面化
 *          集合有界钳位时也不让样式漏到行尾。
 */
static void xmd_scanBufOnce(XmdParser* p, const XmdLitPos* lit, int litCount)
{
    const char* s;
    int len;
    int i;
    int runStart;
    int spanBase;

    if (!p || !p->buf || xmd_bufLen(p) <= 0) return;
    s = xmd_bufData(p);
    len = xmd_bufLen(p);
    spanBase = p->spanTop;
    i = 0;
    runStart = 0;
    while (i < len) {
            char c = s[i];
            if (c == '\\' && i + 1 < len && xmd_isPunctCh(s[i + 1])) {
                /* 可转义标点：反斜杠剥除、标点字面落（探针 escapes/
                   escapes2："\\*not em\\*" → 字面）。 */
                xmd_emitPlain(p, s + runStart, i - runStart);
                xmd_emitRaw(p, s + i + 1, 1);
                i += 2;
                runStart = i;
                continue;
            }
            if (c == '\\' && i + 1 < len) {
                /* 不可转义：反斜杠字面（多字节整段推进）。 */
                i += 1 + xmd_utf8SeqLen(s + i + 1);
                continue;
            }
            if (c == XMD_SOFTBREAK || c == XMD_HARDBREAK) {
                /* 软换行→单空格、硬换行→"\n"（拆块，qtmi.cpp:472-477）。 */
                xmd_emitPlain(p, s + runStart, i - runStart);
                if (c == XMD_SOFTBREAK) xmd_emitRaw(p, " ", 1);
                else xmd_emitRaw(p, "\n", 1);
                i++;
                runStart = i;
                continue;
            }
            if (c == '`') {
                /* 行内代码：等长反引号 run 配对（更长 run 不配对，跳过）。 */
                int n = 0;
                int j = i;
                while (j < len && s[j] == '`') { n++; j++; }
                {
                    int k = j;
                    int found = -1;
                    while (k < len) {
                        if (s[k] == '`') {
                            int m = 0;
                            while (k + m < len && s[k + m] == '`') m++;
                            if (m == n) { found = k; break; }
                            k += m;
                        } else {
                            k++;
                        }
                    }
                    if (found >= 0) {
                        int cs = j;
                        int ce = found;
                        /* 两侧各一格空格且内容非全空白 → 各剥一格。 */
                        if (ce - cs >= 2 && s[cs] == ' ' && s[ce - 1] == ' ') {
                            int allSpace = 1;
                            int q;
                            for (q = cs; q < ce; ++q)
                                if (!xmd_isSpaceCh(s[q])) { allSpace = 0; break; }
                            if (!allSpace) { cs++; ce--; }
                        }
                        xmd_emitPlain(p, s + runStart, i - runStart);
                        {
                            XmdSpan mono;
                            XMemset(&mono, 0, sizeof(mono));
                            mono.mono = 1;
                            xmd_pushSpan(p, &mono);
                            {
                                int q = cs;
                                while (q < ce) {
                                    int seg = q;
                                    while (q < ce && s[q] != XMD_SOFTBREAK &&
                                           s[q] != XMD_HARDBREAK) q++;
                                    if (q > seg) xmd_emitRaw(p, s + seg, q - seg);
                                    if (q < ce) { xmd_emitRaw(p, " ", 1); q++; }
                                }
                            }
                            xmd_popSpan(p);
                        }
                        i = found + n;
                        runStart = i;
                        continue;
                    }
                    i = j; /* 无配对：反引号按字面 */
                    continue;
                }
            }
            if ((c == '*' || c == '_' || c == '~') &&
                !xmd_litHit(lit, litCount, i)) {
                /* 强调/删除线定界符 run + flanking 判定。'~' 仅 GitHub 档
                   的 Strikethrough（qtmi 旗标 0x200；探针 gh strike）；非该
                   档时 '~' 按字面。 */
                int n = 0;
                int j = i;
                bool strikePair;
                while (j < len && s[j] == c) { n++; j++; }
                strikePair = (c == '~') && n == 2 &&
                             (p->features &
                              (int)XMarkdownFeature_StrikeThrough) != 0;
                if (c == '~' && !strikePair) {
                    i = j; /* 非删除线档/非双波浪：字面 */
                    continue;
                }
                {
                    char prevC = (i > 0) ? s[i - 1] : ' ';
                    char nextC = (j < len) ? s[j] : ' ';
                    bool prevWs = (i == 0) ? true : xmd_isSpaceCh(prevC);
                    bool nextWs = (j >= len) ? true : xmd_isSpaceCh(nextC);
                    bool prevPunct = (i == 0) ? false : xmd_isPunctCh(prevC);
                    bool nextPunct = (j >= len) ? false : xmd_isPunctCh(nextC);
                    bool leftFlank = !nextWs && (!nextPunct || prevWs || prevPunct);
                    bool rightFlank = !prevWs && (!prevPunct || nextWs || nextPunct);
                    bool canOpen;
                    bool canClose;
                    bool ghUnder;
                    if (c == '_') {
                        /* '_' 受限 flanking（词内不成样式——探针 intraword：
                           a_b_c 原样落；GitHub 下划线档同规则）。 */
                        canOpen = leftFlank && (!rightFlank || prevPunct);
                        canClose = rightFlank && (!leftFlank || nextPunct);
                    } else {
                        canOpen = leftFlank;
                        canClose = rightFlank;
                    }
                    ghUnder = (p->features &
                               (int)XMarkdownFeature_Underline) != 0 &&
                              c == '_';
                    if (strikePair) {
                        /* 删除线只认成对双波浪：不进 multi-level 配对，开处压
                           一格 strikeOut、闭处弹回。**就近同字符开符**：夹在
                           中间的开符（任意字符）被越过——按字面落（记入
                           p->drops），不得让它们的样式继续覆盖后续文本。
                           例（Qt 探针 `~~a *b~~ c*` ⇒ «a *b»{S}« c*»{}）：
                           `*` 未配对必须是字面且不能把删除线段落吞掉。 */
                        if (canClose) {
                            int d;
                            for (d = p->delimTop - 1; d >= 0; --d) {
                                if (p->delims[d].ch != '~' ||
                                    p->delims[d].dead)
                                    continue;
                                break;
                            }
                            if (d >= 0) {
                                XmdDelim* op = &p->delims[d];
                                int dd;
                                for (dd = d + 1; dd < p->delimTop; ++dd)
                                    xmd_recordDrop(p, &p->delims[dd]);
                                xmd_emitPlain(p, s + runStart, i - runStart);
                                runStart = i;
                                while (p->spanTop > op->spanDepth)
                                    xmd_popSpan(p);
                                p->delimTop = d;
                                i = j;
                                runStart = i;
                                continue;
                            }
                        }
                        if (canOpen && p->delimTop < XMD_MAX_SPAN_DEPTH &&
                            p->spanTop < XMD_MAX_SPAN_DEPTH) {
                            XmdSpan del;
                            XmdDelim* op;
                            xmd_emitPlain(p, s + runStart, i - runStart);
                            runStart = i;
                            XMemset(&del, 0, sizeof(del));
                            del.strikeOut = 1;
                            op = &p->delims[p->delimTop++];
                            op->ch = '~';
                            op->use = 2;
                            op->spanDepth = p->spanTop;
                            op->runStart = i;
                            op->runLen = 2;
                            op->dead = 0;
                            xmd_pushSpan(p, &del);
                            i = j;
                            runStart = i;
                            continue;
                        }
                        i = j; /* flanking 不成立：字面 */
                        continue;
                    }
                    if (canClose && p->delimTop > 0) {
                        /* 就近配对（自顶向下）：消费格数=开 run 原长
                           （***x*** 三星全收——strong(2)+em(1) 同点闭合）。 */
                        int d = p->delimTop - 1;
                        int remain = n;
                        int closedAny = 0;
                        while (d >= 0 && remain > 0) {
                            XmdDelim* op = &p->delims[d];
                            int use;
                            if (op->ch != c || op->dead) { d--; continue; }
                            use = op->use;
                            if (remain < use) {
                                op->dead = 1; /* closer 不足：跳过此开符 */
                                d--;
                                continue;
                            }
                            xmd_emitPlain(p, s + runStart, i - runStart);
                            runStart = i;
                            while (p->spanTop > op->spanDepth) xmd_popSpan(p);
                            remain -= use;
                            closedAny = 1;
                            {
                                /* 本次闭合越过顶掉的未配对开符 ⇒ 字面化
                                   （Qt/md4c「未配对即字面」：md4c 只对成对
                                   span 发 cbEnterSpan/cbLeaveSpan
                                   qtmi.cpp:382-441，其余按 MD_TEXT 落）。 */
                                int dd;
                                for (dd = d + 1; dd < p->delimTop; ++dd)
                                    xmd_recordDrop(p, &p->delims[dd]);
                            }
                            p->delimTop = d;
                            d--;
                        }
                        if (closedAny) {
                            i += n - remain;
                            if (remain > 0) {
                                /* closer 余量按字面。 */
                                xmd_emitRaw(p, s + i, remain);
                                i += remain;
                            }
                            runStart = i;
                            continue;
                        }
                    }
                    if (canOpen) {
                        xmd_emitPlain(p, s + runStart, i - runStart);
                        runStart = i;
                        if (p->delimTop < XMD_MAX_SPAN_DEPTH &&
                            p->spanTop < XMD_MAX_SPAN_DEPTH) {
                            XmdDelim* op = &p->delims[p->delimTop++];
                            op->ch = c;
                            op->use = n;
                            op->spanDepth = p->spanTop;
                            op->runStart = i;
                            op->runLen = n;
                            op->dead = 0;
                            if (ghUnder) {
                                /* GitHub 下划线档：任意 '_' run → underline
                                   （探针 gh-underline：单双下划线均下划线）。 */
                                XmdSpan u;
                                XMemset(&u, 0, sizeof(u));
                                u.underline = 1;
                                xmd_pushSpan(p, &u);
                            } else {
                                /* strong(≥2) + em(奇数余 1)——探针 inline：
                                   «both»{bold italic}。 */
                                if (n >= 2) {
                                    XmdSpan st;
                                    XMemset(&st, 0, sizeof(st));
                                    st.bold = 1;
                                    xmd_pushSpan(p, &st);
                                }
                                if (n % 2 == 1) {
                                    XmdSpan em;
                                    XMemset(&em, 0, sizeof(em));
                                    em.italic = 1;
                                    xmd_pushSpan(p, &em);
                                }
                            }
                        }
                        i = j;
                        runStart = i;
                        continue;
                    }
                    i = j; /* 不可开不可关：字面 */
                    continue;
                }
            }
            if (c == '!' && i + 1 < len && s[i + 1] == '[') {
                /* 图片：整体跳过（模型无按名取图资源；Qt 落 U+FFFC——
                   两侧纯文本都不含 alt 文本，差异仅为 FFFC 占位符）。 */
                int j = i + 2;
                while (j < len && s[j] != ']') {
                    if (s[j] == '\\' && j + 1 < len) { j += 2; continue; }
                    if (s[j] == '`') {
                        int m = 0;
                        while (j + m < len && s[j + m] == '`') m++;
                        j += m;
                        continue;
                    }
                    j++;
                }
                if (j < len && j + 1 < len && s[j + 1] == '(') {
                    int k = j + 2;
                    int depth = 1;
                    while (k < len && depth > 0) {
                        if (s[k] == '(') depth++;
                        else if (s[k] == ')') depth--;
                        else if (s[k] == '\\' && k + 1 < len) k++;
                        k++;
                    }
                    if (depth == 0) {
                        xmd_emitPlain(p, s + runStart, i - runStart);
                        i = k;
                        runStart = i;
                        continue;
                    }
                }
                i++;
                continue;
            }
            if (c == '[') {
                /* 链接 [text](dest "title")；文本经子区间重入扫描（允许
                   行内嵌套——探针 link-nested-text）；标题（tooltip）不落
                   （模型无槽位）；目的地按原文承载。 */
                int j = i + 1;
                int found = -1;
                while (j < len && s[j] != ']') {
                    if (s[j] == '\\' && j + 1 < len) { j += 2; continue; }
                    if (s[j] == '`') {
                        int m = 0;
                        while (j + m < len && s[j + m] == '`') m++;
                        j += m;
                        continue;
                    }
                    j++;
                }
                if (j < len) found = j;
                if (found >= 0 && found + 1 < len && s[found + 1] == '(') {
                    int k = found + 2;
                    int depth = 1;
                    int destStart = k;
                    int destEnd = -1;
                    while (k < len) {
                        char kc = s[k];
                        if (kc == '(') depth++;
                        else if (kc == ')') {
                            depth--;
                            if (depth == 0) { destEnd = k; break; }
                        } else if (xmd_isSpaceCh(kc)) {
                            /* 尝试标题：引号串 + 配平 ')'；无标题则目的地
                               止于空白。 */
                            int t = k;
                            while (t < len && xmd_isSpaceCh(s[t])) t++;
                            if (t < len && (s[t] == '"' || s[t] == '\'')) {
                                char q = s[t];
                                int u = t + 1;
                                while (u < len && s[u] != q) {
                                    if (s[u] == '\\' && u + 1 < len) u++;
                                    u++;
                                }
                                if (u < len) {
                                    int v = u + 1;
                                    int d2 = 1;
                                    while (v < len && d2 > 0) {
                                        if (s[v] == '(') d2++;
                                        else if (s[v] == ')') d2--;
                                        v++;
                                    }
                                    if (d2 == 0) { destEnd = v - 1; break; }
                                }
                            }
                            destEnd = k;
                            break;
                        } else if (kc == '\\' && k + 1 < len) {
                            k++;
                        }
                        k++;
                    }
                    if (destEnd >= 0) {
                        /* href 载荷 = 目的地本身：首个空白起是可选标题
                           （tooltip 无槽位），不并入 href（探针 link-title：
                           href=u、标题 title 只影响 tooltip）。尖括号包裹的
                           目的地剥 <>（Qt 探针 /tmp/qtprobe_w3a.cpp：
                           `[x](<u>)` ⇒ href «u»、`[x](<> "t")` ⇒ href «»）；
                           空目的地 `[x]()` 是合法链接（Qt 同探针 A: 空 href）。 */
                        int hrefStart = destStart;
                        int hrefEnd = destStart;
                        const char* href = NULL;
                        int linkOk = 1;
                        while (hrefEnd < destEnd &&
                               !xmd_isSpaceCh(s[hrefEnd]))
                            hrefEnd++;
                        if (hrefEnd - hrefStart >= 2 &&
                            s[hrefStart] == '<' && s[hrefEnd - 1] == '>') {
                            hrefStart++;
                            hrefEnd--;
                        } else if (hrefEnd > hrefStart &&
                                   (s[hrefStart] == '<' ||
                                    s[hrefEnd - 1] == '>')) {
                            linkOk = 0; /* 单边尖括号：非法目的地，按字面 */
                        }
                        if (linkOk && hrefEnd > hrefStart)
                            href = xmd_hrefIntern(p, s + hrefStart,
                                                  hrefEnd - hrefStart);
                        if (linkOk) {
                            xmd_emitPlain(p, s + runStart, i - runStart);
                            runStart = i;
                            {
                                XmdSpan link;
                                XmdSpan savedSpans[XMD_MAX_SPAN_DEPTH];
                                XmdDelim savedDelims[XMD_MAX_SPAN_DEPTH];
                                int savedSpanTop;
                                int savedDelimTop;
                                int savedDropCount;
                                XByteArray* savedBuf;
                                XMemset(&link, 0, sizeof(link));
                                link.link = 1;      /* 锚点（含空 href） */
                                link.href = href;   /* 空目的地为 NULL */
                                xmd_pushSpan(p, &link);
                                /* 行内状态入栈保存（链接 span 为子扫描基）。 */
                                XMemcpy(savedSpans, p->spans, sizeof(savedSpans));
                                savedSpanTop = p->spanTop;
                                XMemcpy(savedDelims, p->delims, sizeof(savedDelims));
                                savedDelimTop = p->delimTop;
                                savedDropCount = p->dropCount;
                                p->delimTop = 0;
                                p->dropCount = 0; /* 子缓冲坐标，勿混入父轮 */
                                /* 段缓冲暂换子区间重入扫描（换容器不自毁）。 */
                                savedBuf = p->buf;
                                p->buf = XByteArray_create_ex(
                                    XCLASS_DEFAULT_MEMORY_TYPE, false);
                                xmd_bufAppend(p, s + i + 1, found - i - 1);
                                xmd_scanBufFull(p); /* 完整协议（干跑+实跑） */
                                if (p->buf) XClassDelete((XClass*)p->buf);
                                p->buf = savedBuf;
                                /* 行内状态出栈恢复（span 栈顶=链接 span）。 */
                                XMemcpy(p->spans, savedSpans, sizeof(savedSpans));
                                p->spanTop = savedSpanTop;
                                XMemcpy(p->delims, savedDelims, sizeof(savedDelims));
                                p->delimTop = savedDelimTop;
                                p->dropCount = savedDropCount;
                                xmd_popSpan(p);
                            }
                            i = destEnd + 1;
                            runStart = i;
                            continue;
                        }
                    }
                }
                i++; /* 非法链接：'[' 按字面 */
                continue;
            }
            if (c == '<') {
                /* 尖括号自动链接 <scheme:...>（两档皆可——CommonMark 核心，
                   探针 autolink-cm）。 */
                int j = i + 1;
                int scheme = 0;
                if (j < len && xmd_isAlphaCh(s[j])) {
                    while (j < len && (xmd_isAlphaCh(s[j]) ||
                                       xmd_isDigitCh(s[j]) || s[j] == '+' ||
                                       s[j] == '.' || s[j] == '-')) {
                        scheme++;
                        j++;
                    }
                }
                if (scheme >= 1 && scheme <= 32 && j < len && s[j] == ':') {
                    int k = j + 1;
                    int ok = 0;
                    while (k < len && s[k] != '>') {
                        if (xmd_isSpaceCh(s[k]) || s[k] == '<' ||
                            (unsigned char)s[k] < 0x20) {
                            ok = 0;
                            break;
                        }
                        ok = 1;
                        k++;
                    }
                    if (ok && k < len) {
                        const char* href = xmd_hrefIntern(
                            p, s + i + 1, k - i - 1);
                        if (href) {
                            xmd_emitPlain(p, s + runStart, i - runStart);
                            runStart = i;
                            {
                                XmdSpan link;
                                XMemset(&link, 0, sizeof(link));
                                link.link = 1;
                                link.href = href;
                                xmd_pushSpan(p, &link);
                                xmd_emitRaw(p, s + i + 1, k - i - 1);
                                xmd_popSpan(p);
                            }
                            i = k + 1;
                            runStart = i;
                            continue;
                        }
                    }
                }
                i++;
                continue;
            }
            if (c == '&') {
                /* 实体：命名 + 数字（非法按字面 '&'——Qt 走 insertHtml）。 */
                char dec[8];
                int n = xmd_decodeEntity(s + i, len - i, dec);
                if (n > 0) {
                    int consumed = 1;
                    while (i + consumed < len && s[i + consumed - 1] != ';')
                        consumed++;
                    xmd_emitPlain(p, s + runStart, i - runStart);
                    xmd_emitRaw(p, dec, n);
                    i += consumed;
                    runStart = i;
                    continue;
                }
                i++;
                continue;
            }
            i += xmd_utf8SeqLen(s + i);
        }
        /* 尾段发射：扫描终点仍挂着的 span 必是未配对开符（字面化集合有界
           钳位时干跑收敛不到零）——先回滚到入口栈基再落尾段，格式不得
           延续到行尾（块末 xmd_flushParagraph 还会把 spanTop 复位）。 */
        while (p->spanTop > spanBase) xmd_popSpan(p);
        if (runStart < len) xmd_emitPlain(p, s + runStart, len - runStart);
}

/**
 * @brief  行内扫描完整协议：干跑循环找未配对定界符 → 字面化 → 实跑单遍。
 * @details CommonMark：不成对的强调标记按字面落。干跑零发射零入池（发射
 *          与 href 均随 p->dry 门控），只探 p->delimTop / p->drops；每轮把
 *          未配对 run 与「被 closer 越过顶掉」的开符**逐字符**加入字面化
 *          集合（嵌套未配对逐层暴露，至多 XMD_LIT_MAX 轮，有界收敛），末轮
 *          实跑只发生一次——杜绝「重启重发射」。
 */
static void xmd_scanBufFull(XmdParser* p)
{
    XmdLitPos lit[XMD_LIT_MAX];
    int litCount = 0;
    int guard;
    int spanBase;
    XmdSpan savedBase[XMD_MAX_SPAN_DEPTH];

    if (!p || !p->buf || xmd_bufLen(p) <= 0) return;
    /* 本轮扫描的格式栈基（块基或链接 span 基）。干跑虽零发射，但
       xmd_scanBufOnce 仍会压/弹 span 栈（含 pushSpan 对块基的失效处理），
       **每轮必须把基恢复成同一快照**，否则上一轮对块基的改动（标题块基
       Bold 被清）残留到实跑（实测：`# plain **b** tail` 的「plain 」丢
       Bold；`*abc and **def` 实跑片段带 I+B——Qt 探针分别为 {B}/{纯字面}）。 */
    spanBase = p->spanTop;
    XMemcpy(savedBase, p->spans, sizeof(XmdSpan) * (size_t)spanBase);
    if (p->dry) {
        /* 干跑上下文（链接文本子扫描）：单遍即可——发射全被门控，未配对
           定界符由外层实跑的协议处理。子扫描的 p->drops 位置是**子缓冲
           坐标**，对父缓冲无意义 ⇒ 进出各存/还一次（父轮收集不得读错位）。 */
        int savedDrops = p->dropCount;
        XMemcpy(p->spans, savedBase, sizeof(XmdSpan) * (size_t)spanBase);
        p->spanTop = spanBase;
        p->dropCount = 0;
        xmd_scanBufOnce(p, lit, litCount);
        p->dropCount = savedDrops;
        XMemcpy(p->spans, savedBase, sizeof(XmdSpan) * (size_t)spanBase);
        p->spanTop = spanBase;
        return;
    }
    /* 实跑前的干跑收敛协议（**单次发射**，绝不重发射）：
     *   每轮干跑（零发射零入池）收集两类「未配对开符」——
     *   ① 段末仍挂在 p->delims 上的；
     *   ② 被某个 closer 越过、从定界符栈顶掉的（p->drops）；
     *   逐字符并入字面化集合 lit 后重跑；**本轮没有新增字面化**即收敛
     *   （此时实跑结构与末轮干跑逐支同构 ⇒ 不会留未配对样式）。
     *   注意不能只判 `delimTop <= 0`：closer 顶掉开符时 delimTop 已经回落，
     *   但被顶掉的开符仍需字面化（`**a _b**` 的 `_`——否则标记字符丢失、
     *   样式污染后续片段，Qt 探针为 «a _b»{B}）。 */
    for (guard = 0; guard < XMD_LIT_MAX; ++guard) {
        int before = litCount;
        int d;
        int k;
        p->delimTop = 0;
        p->dropCount = 0;
        XMemcpy(p->spans, savedBase, sizeof(XmdSpan) * (size_t)spanBase);
        p->spanTop = spanBase; /* 关键复位（上轮/上干跑残留清零） */
        p->dry = 1;
        xmd_scanBufOnce(p, lit, litCount); /* 干跑：零发射零入池 */
        for (d = 0; d < p->delimTop && litCount < XMD_LIT_MAX; ++d) {
            for (k = 0;
                 k < p->delims[d].runLen && litCount < XMD_LIT_MAX; ++k) {
                int pos = p->delims[d].runStart + k;
                if (xmd_litHit(lit, litCount, pos)) continue;
                lit[litCount].start = pos;
                litCount++;
            }
        }
        for (d = 0; d < p->dropCount && litCount < XMD_LIT_MAX; ++d) {
            if (xmd_litHit(lit, litCount, p->drops[d].start)) continue;
            lit[litCount].start = p->drops[d].start;
            litCount++;
        }
        if (litCount == before) break;      /* 本轮无新增：结构已收敛 */
        if (litCount >= XMD_LIT_MAX) break; /* 有界钳位（尾段回滚兜底） */
    }
    /* 实跑：唯一发射遍（干跑只探形，不落盘）。 */
    p->dry = 0;
    p->delimTop = 0;
    p->dropCount = 0;
    XMemcpy(p->spans, savedBase, sizeof(XmdSpan) * (size_t)spanBase);
    p->spanTop = spanBase;
    xmd_scanBufOnce(p, lit, litCount);
    XMemcpy(p->spans, savedBase, sizeof(XmdSpan) * (size_t)spanBase);
    p->spanTop = spanBase; /* 交还调用方的基（块基=1 / 链接 span 基） */
}

/** @brief 行内扫描入口（复位格式栈到块基后按完整协议扫描段缓冲）。 */
static void xmd_scanInline(XmdParser* p)
{
    if (!p) return;
    p->spanTop = 1;
    p->delimTop = 0;
    xmd_scanBufFull(p);
}

/* ==================== 前置元材料 ==================== */

/**
 * @brief  行尾下标（从 pos 起首个 \r/\n；无换行则返回 len）。
 * @note   **绝对下标**：md[0..len) 是硬边界，任何分支不得越过 len 读。
 */
static int xmd_lineEndAt(const char* md, int pos, int len)
{
    int e = pos;
    while (e < len && !xmd_isNlCh(md[e])) e++;
    return e;
}

/** @brief 跳过 pos 起的连续换行符（\r\n / \n / \r 混合同口径），返回首个
 *         非换行下标（越界返回 len）。 */
static int xmd_skipNlAt(const char* md, int pos, int len)
{
    int i = pos;
    while (i < len && xmd_isNlCh(md[i])) i++;
    return i;
}

/**
 * @brief  GitHub 档前置元材料（qtmi.cpp:146-160：文首 `---` … `---` →
 *         setMetaInformation(FrontMatter)，正文从闭标记后装载）。
 * @return 消费字节数（0=无前置段）。开/闭标记=整行 "---"（可带尾空白）；
 *         Qt 原文用 indexOf("---") 任意位置匹配，本实现按行匹配（良构
 *         输入两口径一致；Qt 的宽松匹配对 "---- x" 类行会误吞，不取）。
 * @details **边界约定（边界语义回归锁；内存越界由 ASan 复跑把关）**：全过程
 *          只用 [0,len) 内的绝对下标——行尾由 xmd_lineEndAt 给出，空白判定
 *          xmd_isBlankFrom(md, 绝对起, 绝对止)（两者都是绝对下标；曾把行首
 *          指针与绝对列混加读过串尾，ASan global-buffer-overflow 实测）；
 *          闭标记行**整行消费**：`\r\n` 两字节一并跳过（旧实现只 +1，CRLF
 *          下把 '\n' 留成正文首个空行；该差异在公开 API 上不可观测——空行
 *          不建块，见测试注释），文档恰好在闭标记处结束（le==len）时按 le
 *          返回（正文为空）。**零内容前置段拒绝**（Qt 6.8.3 qtmi.cpp:147-148
 *          的 `endMarkerPos > 4` 等价：闭行行首必须 > 4）。未闭标记（扫到
 *          len 仍无闭行）返回 0 ⇒ 整文按普通 Markdown 落。
 */
static int xmd_tryFrontMatter(const char* md, int len, XmdParser* p)
{
    int i;
    int lineEnd;
    int contentStart;
    int contentEnd;
    if (!p || !md || len < 4) return 0;
    if (XStrncmp(md, "---", 3) != 0) return 0;
    /* 【Qt 6.8.0 offscreen 探针裁定（2026-11-17，/tmp/fmprobe/p2.cpp/p3.cpp）】
     * ① 开标记必须恰好是 `---\n`（LF 结尾）：`---\r\n…` 的 CRLF 前置段
     *    Qt **不认**（fm 为空、整文按普通 Markdown）；
     * ② 闭标记同理必须 `---\n`（LF 结尾）——文档恰在闭标记处结束
     *    （`---\ntitle: x\n---` 无尾换行）Qt 也**不认**；
     * ③ 闭标记整行必须恰为 3 个短横（`----` 不认，探针 close-4dash）；
     * ④ 内容为开标记行后的原字节到闭行行首（含换行）：空白行内容
     *    （`---\n\n---\n`）fm=«\n»、空格行 fm=« » 均被接受，
     *    故不跳过开标记后的空行、不做 trim。
     * 旧实现允许 CRLF/EOF 收尾并跳过首空行，与本机 Qt 三处相反（复核
     * 2026-11-17 指出「边界用例把与 Qt 相反的行为断言成通过」）。 */
    if (md[3] != '\n') return 0;
    contentStart = 4;
    i = 4;
    while (i < len) {
        int ls = i;
        int le = xmd_lineEndAt(md, i, len);
        /* 闭行判定（绝对边界）：整行恰 "---"、后随 LF、且行首 >4
           （闭行紧贴开标记即零内容前置段 ⇒ Qt 拒绝，等价 qtmi.cpp:147
           的 `endMarkerPos > 4`；探针 `---\n---\n` fm 为空）。 */
        if (le - ls == 3 && ls > 4 && le < len && md[le] == '\n' &&
            XStrncmp(md + ls, "---", 3) == 0) {
            XString* fm;
            contentEnd = ls;
            fm = XString_create();
            if (fm) {
                if (contentEnd > contentStart)
                    XString_assign_with_length_utf8(
                        fm, md + contentStart,
                        (size_t)(contentEnd - contentStart));
                else
                    XString_assign_utf8(fm, "");
                XTextDocument_setMetaInformation(
                    p->doc, (int)XTDMetaInformation_FrontMatter,
                    XString_toUtf8(fm) ? XString_toUtf8(fm) : "");
                XClassDelete((XClass*)fm);
            }
            /* 闭标记行整行消费到 LF 之后（CRLF 已在上面被排除）。 */
            return le + 1;
        }
        i = xmd_skipNlAt(md, le, len);
    }
    return 0; /* 无闭标记：不按前置段（整文按普通 Markdown） */
}

/* ==================== 主循环 ==================== */

/**
 * @brief  无序标记字符 → XTDListStyle（对标 Qt QTextMarkdownImporter）。
 * @details Qt 源码逐值（qtextmarkdownimporter.cpp:222-231 的 MD_BLOCK_UL
 *          分支）：`'*'`→ListCircle、`'+'`→ListSquare、**default 分支
 *          （含 `'-'`）→ListDisc**；本机 Qt 6.8.0 offscreen 探针
 *          /tmp/qtprobe_list.cpp 实测 "ul-dash" style=Disc(-1)、
 *          "ul-star" style=Circle(-2)、"ul-plus" style=Square(-3)。
 * @param mark 标记字符（'-'/'*'/'+'）。
 * @return XTDListStyle 值。
 */
static int xmd_ulStyle(char mark)
{
    switch (mark) {
    case '*': return (int)XTDListStyle_Circle;
    case '+': return (int)XTDListStyle_Square;
    default: return (int)XTDListStyle_Disc; /* 含 '-'（Qt default 分支）。 */
    }
}

/**
 * @brief  一行按当前容器上下文分类并落盘（主循环行处理）。
 * @details 顺序：围栏分支（原始位按列精确剥容器——内容空白保留）→ 引用
 *          前缀剥离 → 空行（引用内/纯）→ 引用收敛与惰性续行 → 列表层收敛
 *          （内容列；惰性续行同规则）→ 分类落盘（xmd_classifyRest）。
 */
static void xmd_processLine(XmdParser* p, const char* line, int len,
                            const char* next, int nextLen)
{
    int i;
    int col;
    int newQuote;
    int ni;
    int ncol;

    if (!p || !line) return;

    /* ---- 围栏/缩进代码块中：容器前缀按列精确剥离（内容空白保留） ---- */
    if (p->inFence) {
        int ri;
        int rc;
        int base;
        int c2;
        int j2;
        (void)xmd_stripQuoteRaw(line, len, &ri, &rc);
        /* 列表容器内容列剥离（栏在列表项内开——探针 fence-in-list）。 */
        base = (p->listDepth > 0)
                   ? p->lists[p->listDepth - 1].contentIndent
                   : 0;
        if (base > rc) ri = xmd_skipColsInSpan(line, ri, len, rc, base, &rc);
        /* 行内缩进（围栏允许 0-3、缩进代码按列 4 剥）。 */
        j2 = xmd_indentCols(line + ri, len - ri, &c2);
        if (p->inFence == 1) {
            if (p->fenceChar &&
                xmd_fenceCloseAt(line, ri + j2, len, c2, p->fenceChar,
                                 p->fenceLen)) {
                /* 闭栏：栏尾挂起空行不落（qtmi.cpp:524-530 延后发射口径）。 */
                p->inFence = 0;
                p->fenceChar = 0;
                p->fenceLen = 0;
                p->fenceIndent = 0;
                p->codeBlanks = 0;
                return;
            }
            /* 内容按「开栏缩进」剥离（Qt 探针 /tmp/qtprobe_w3a.cpp 的
               fence-indent2：开栏缩进 2 ⇒ "  a"→«a»、"   b"→« b»；
               fence-indent1：开栏缩进 1 ⇒ " a"→«a»、「  b」→« b»）。
               剥列 = min(开栏缩进, 本行缩进)。 */
            i = xmd_stripUpToCols(line, ri, len, j2 < p->fenceIndent ? j2
                                                                    : p->fenceIndent);
            if (xmd_isBlankFrom(line, i, len)) {
                p->codeBlanks++;
                return;
            }
            xmd_flushCodeBlanks(p);
            xmd_emitCodeLine(p, line + i, len - i);
            return;
        }
        /* 缩进代码块：内容=容器列 + 4 起（余下空白原样保留）；缩进不足
           4 列的非空行 ⇒ 块收、整行重分类（递归一层，inFence 已清）。 */
        if (xmd_isBlankFrom(line, ri, len)) {
            p->codeBlanks++;
            return;
        }
        if (c2 < 4) {
            p->inFence = 0;
            p->codeBlanks = 0;
            xmd_processLine(p, line, len, next, nextLen);
            return;
        }
        i = xmd_skipColsInSpan(line, ri, len, rc, rc + 4, &col);
        xmd_flushCodeBlanks(p);
        xmd_emitCodeLine(p, line + i, len - i);
        return;
    }

    /* ---- 引用前缀剥离（跳到首个非空白） ---- */
    newQuote = xmd_stripQuote(line, len, &ni, &ncol);
    i = ni;
    col = ncol;

    /* ---- 空行（剥引用后判定：`>` 行的空白=引用内空行） ---- */
    if (xmd_isBlankFrom(line, i, len)) {
        if (newQuote == 0 && p->quoteDepth > 0) {
            /* 纯空行：引用结束、引用内列表一并收（惰性不跨空行）。 */
            p->quoteDepth = 0;
            p->listDepth = 0;
        }
        xmd_flushParagraph(p); /* 列表保留（loose——探针 loose-ul） */
        p->afterBlank = 1;     /* 空行后不允许惰性续行（结束列表/引用段） */
        return;
    }

    /* ---- 引用层级收敛与惰性续行 ---- */
    if (newQuote == 0 && p->quoteDepth > 0) {
        if (p->paraOpen && !p->afterBlank &&
            xmd_lazyJoinable(line, i, len, col, p->features)) {
            /* 惰性续行：无 `>` 的段落行接排进引用段（探针 quote：
               "quoted more after" 一块 quoteLevel=1）。 */
            xmd_appendParaLine(p, line, i, len);
            return;
        }
        p->quoteDepth = 0;
        p->listDepth = 0;
        i = xmd_indentCols(line, len, &col); /* 顶层重分类 */
    } else {
        p->quoteDepth = newQuote;
    }

    /* ---- 列表层收敛（内容列比较；惰性续行同规则） ---- */
    {
        int keep = 0;
        /* 收敛前的层数：分类阶段据此判定「本行列表项所在层是否还开着」
           （见 XmdParser.listOpenDepth 注）。 */
        p->listOpenDepth = p->listDepth;
        while (keep < p->listDepth) {
            if (col >= p->lists[keep].contentIndent) { keep++; continue; }
            break;
        }
        if (keep < p->listDepth) {
            if (p->paraOpen && !p->afterBlank &&
                xmd_lazyJoinable(line, i, len, col, p->features)) {
                xmd_appendParaLine(p, line, i, len);
                return;
            }
            p->listDepth = keep;
        }
    }

    xmd_classifyRest(p, line, len, next, nextLen, i, col);
}

/**
 * @brief  剥容器后的行分类落盘。
 * @details 优先序：Setext（段开，优先于水平线/列表——CommonMark 同款优先级，
 *          探针 setext/setext-after-heading）→ 水平线 → ATX → 围栏开栏 →
 *          列表项 → 缩进代码（≥4 列；打不断段；嵌套列表内不启用——模型
 *          边界简化）→ 段落。
 * @param  rawI/rawCol 引用标记后原始位（列表项内容围栏的列剥离备用；本路径
 *         未用，保留签名对称）。
 */
static void xmd_classifyRest(XmdParser* p, const char* line, int len,
                             const char* next, int nextLen, int i, int col)
{
    int lvl;
    int cs;
    if (!p) return;

    /* Setext 下划线行本身：已被前瞻消费（skipNextLine），此处仅兜底——
       若走到这里说明上一行不是合格段行，则按水平线/普通行处理。 */
    if (xmd_isThematicAt(line, i, len)) {
        xmd_flushParagraph(p);
        xmd_emitHr(p);
        return;
    }
    if (xmd_atxMatch(line, i, len, col, p->features, &lvl, &cs)) {
        int ce;
        int k;
        int hashes;
        xmd_flushParagraph(p);
        xmd_openBlock(p, lvl, false, false,
                      p->quoteDepth > 0 ? p->quoteDepth : 0, false);
        /* 内容裁剪：闭尾 '#' 串（须空白前置或整段皆 #，CommonMark）+ 尾空白。 */
        ce = len;
        while (ce > cs && xmd_isSpaceCh(line[ce - 1])) ce--;
        k = ce;
        hashes = 0;
        while (k > cs && line[k - 1] == '#') { hashes++; k--; }
        if (hashes > 0 && (k == cs || line[k - 1] == ' ')) ce = k;
        while (ce > cs && xmd_isSpaceCh(line[ce - 1])) ce--;
        if (ce > cs) xmd_bufAppend(p, line + cs, ce - cs);
        xmd_flushParagraph(p); /* ATX 单行：立即落 */
        return;
    }
    if (xmd_fenceOpenAt(line, i, len, col, &p->fenceChar, &p->fenceLen)) {
        xmd_flushParagraph(p);
        p->inFence = 1;
        p->fenceIndent = col; /* 内容剥列基准（Qt 探针 fence-indent*） */
        p->codeBlanks = 0;
        return;
    }
    {
        bool ordered = false;
        int start = 1;
        char delim = 0;
        char ul = 0;
        int ci = 0;
        if (xmd_listMarkerAt(line, i, len, col, &cs, &ci, &ordered, &start,
                             &delim, &ul)) {
            XmdListLevel* nl;
            bool sameList;
            int itemCol;
            char fc;
            int fl;
            xmd_flushParagraph(p);
            if (p->listDepth >= XMD_MAX_LIST_DEPTH)
                p->listDepth = XMD_MAX_LIST_DEPTH - 1;
            nl = &p->lists[p->listDepth];
            /* 同层「新列表」判定（对标 Qt/md4c）：
               ① 本项所在层（1 起的 listDepth+1）在**本行收敛前**是否还开着
                  （level ≤ p->listOpenDepth）——关过再开就是新列表；
               ② 该层标记字符/有序分隔符变了 ⇒ 新列表（`- a\n+ b`、
                  `1. a\n1) b`）；
               ③ **编号不参与**：`1. a\n\n5. b` 仍是同一列表，第二项
                  itemText「2.」（探针 /tmp/qtprobe_list4.cpp）；空行也不
                  结束列表；**列表项内部的续段**（缩进到内容列的段落）同样
                  不结束列表——`1. a\n\n   para\n1. b` 的第二项是「2.」
                  （探针 /tmp/b5rev_probe2.cpp G/F），而段落回到外层列
                  （`1. a\n\npara\n1. b`）则前后是两个列表（同探针 D/E）。
               层收敛只降 listDepth、不清条目 ⇒ 旧条目 + listOpenDepth
               合起来才够判「同列表兄弟项 vs 旧列表关闭后重开」。 */
            sameList = (p->listDepth + 1 <= p->listOpenDepth) &&
                       nl->ordered == (ordered ? 1 : 0) &&
                       nl->marker == (ordered ? delim : ul);
            if (!sameList) nl->start = start; /* 同列表后续项不覆盖起始号。 */
            nl->ordered = ordered ? 1 : 0;
            nl->marker = ordered ? delim : ul;
            nl->contentIndent = ci > 0 ? ci : col + 1;
            nl->markerIndent = col;
            p->listDepth++;
            cs = xmd_stripTaskMarker(line, cs, len, p->features);
            /* 项内容是围栏开栏 ⇒ 栏在项内开（探针 fence-in-list）。**不建
               空项块**：Qt 该例总块数=段落+代码（2 块），项块与代码块合一；
               首行代码由 emitCodeLine 以列表缩进建块。 */
            itemCol = nl->contentIndent;
            if (cs < len &&
                xmd_fenceOpenAt(line, cs, len, itemCol, &fc, &fl)) {
                p->inFence = 1;
                p->fenceChar = fc;
                p->fenceLen = fl;
                p->fenceIndent = 0; /* 项内栏：内容剥列基准=项内容列（已由
                                       listDepth 的 contentIndent 处理） */
                p->codeBlanks = 0;
                p->paraOpen = 0;
                return;
            }
            /* 列表项块：缩进=深度；引用内再叠加引用深度（可见缩进承载）。 */
            xmd_openBlock(p, 0, true, ordered,
                          p->quoteDepth > 0 ? p->quoteDepth + p->listDepth
                                            : p->listDepth,
                          false);
            /* 列表格式三属性落块（与 HTML `<ul type>/<ol start>` 管线共用
               XTextDocument_setBlockListFormat 单一入口——不双份）：
               UL 按标记字符映射样式（'*'→Circle、'+'→Square、缺省含 '-'→
               Disc；qtmi.cpp:222-231），OL 恒 Decimal + NumberSuffix=分隔符
               + Start=起始号（qtmi.cpp:242-250）。 */
            XTextDocument_setBlockListFormat(
                p->doc, XTextCursor_blockNumber(&p->cur),
                ordered ? (int)XTDListStyle_Decimal : xmd_ulStyle(ul),
                nl->start, ordered ? delim : '.', !sameList);
            if (cs < len) xmd_appendParaLine(p, line, cs, len);
            return;
        }
    }
    if (col >= 4 && p->listDepth == 0 && !p->paraOpen &&
        !(p->features & (int)XMarkdownFeature_NoIndentedCodeBlocks)) {
        p->inFence = 2;
        p->fenceChar = 0;
        p->fenceLen = 0;
        p->codeBlanks = 0;
        /* 缩进代码内容 = 行首按列 4 剥（列 4 起原样保留——探针
           indented-code："    indented code" → «indented code»）。 */
        i = xmd_skipColsInSpan(line, 0, len, 0, 4, &col);
        xmd_emitCodeLine(p, line + i, len - i);
        return;
    }
    if (!p->paraOpen) {
        /* Setext 标题前瞻：本行起段、下一行是下划线 ⇒ 本行按标题落并消费
           下划线行（Qt 的 H 块在文本写入前就带标题格式，qtmi:194-208；
           本机 Qt 探针 /tmp/qtprobe_w3a.cpp 的 setext-inline 逐片对拍）。
           仅支持「单行 Setext」（多行 Setext 段落属未覆盖子集，见头注释）。 */
        if (p->listDepth == 0 && next &&
            xmd_setextUnderlineLine(next, nextLen, &lvl)) {
            xmd_openBlock(p, lvl, false, false,
                          p->quoteDepth > 0 ? p->quoteDepth : 0, false);
            xmd_appendParaLine(p, line, i, len);
            xmd_flushParagraph(p);
            p->skipNextLine = 1;
            return;
        }
        xmd_openBlock(p, 0, false, false,
                      p->quoteDepth > 0 ? p->quoteDepth : 0, false);
    }
    xmd_appendParaLine(p, line, i, len);
}

/* ==================== 公共入口 ==================== */

void XMarkdownParser_parse_utf8(const char* md, XTextDocument* doc,
                                int features)
{
    const char* p8;
    int len;
    XmdParser p;
    int i;
    XmdLine* lines = NULL;
    int lineCount = 0;
    int k;

    if (!doc) return;
    XMemset(&p, 0, sizeof(p)); /* 栈实例整清（结构体初值铁律） */
    p.doc = doc;
    p.features = features;

    /* ---- Qt import()：doc->clear()（清撤销史）→ 单编辑块导入 ---- */
    XTextDocument_clear(doc);
    p8 = md ? md : "";
    len = (int)XStrlen(p8);

    XTextDocument_beginEditBlock(doc);
    XTextCursor_init(&p.cur, doc);

    if (features & (int)XMarkdownFeature_FrontMatter) {
        int used = xmd_tryFrontMatter(p8, len, &p);
        if (used > 0) {
            p8 += used;
            len -= used;
        }
    }

    /* ---- 行切分（预先成表：Setext 前瞻要「下一行」；一次扫描建表，
     *      空行照旧成条目——空行必须是可处理的一行，不得被跳过 ---- */
    if (len > 0) {
        i = 0;
        while (i < len) {
            int le = i;
            while (le < len && !xmd_isNlCh(p8[le])) le++;
            if (le > i && p8[le - 1] == '\r') le--; /* \r\n 剥 \r */
            lineCount++;
            i = le;
            if (i < len && xmd_isNlCh(p8[i])) {
                if (p8[i] == '\r' && i + 1 < len && p8[i + 1] == '\n') i += 2;
                else i += 1;
            }
        }
        lines = (XmdLine*)XMalloc_System(sizeof(XmdLine) * (size_t)lineCount);
        if (!lines) lineCount = 0;
    }
    if (lines) {
        i = 0;
        k = 0;
        while (i < len && k < lineCount) {
            int le = i;
            while (le < len && !xmd_isNlCh(p8[le])) le++;
            if (le > i && p8[le - 1] == '\r') le--;
            lines[k].start = i;
            lines[k].len = le - i;
            k++;
            i = le;
            if (i < len && xmd_isNlCh(p8[i])) {
                if (p8[i] == '\r' && i + 1 < len && p8[i + 1] == '\n') i += 2;
                else i += 1;
            }
        }
        lineCount = k;
    }

    for (k = 0; k < lineCount; ++k) {
        const char* cur = p8 + lines[k].start;
        int curLen = lines[k].len;
        const char* next = (k + 1 < lineCount) ? p8 + lines[k + 1].start
                                               : NULL;
        int nextLen = (k + 1 < lineCount) ? lines[k + 1].len : 0;
        p.skipNextLine = 0;
        xmd_processLine(&p, cur, curLen, next, nextLen);
        if (p.skipNextLine) {
            /* 前瞻已把下一行（Setext 下划线）消费掉。 */
            p.skipNextLine = 0;
            k++;
        }
    }
    if (lines) XFree_System(lines);

    /* 收尾：段冲刷 + 栏内挂起空行丢弃（未闭栏按已闭收口）。 */
    xmd_flushParagraph(&p);
    p.codeBlanks = 0;
    p.inFence = 0;

    XTextCursor_deinit(&p.cur);
    XTextDocument_endEditBlock(doc);
    xmd_parserClear(&p);
}

#endif /* XGUI_ON && XTEXTDOCUMENT_ON */
