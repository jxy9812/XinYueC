/**
 * @file       XMarkdownParser.h
 * @brief      XMarkdownParser Markdown 解析落文档（对标 Qt 6.8
 *             QTextMarkdownImporter + QTextDocument::setMarkdown）。
 * @details    Qt 侧 Markdown 是「md4c 事件回调 → QTextCursor 写入文档」的
 *             导入器（qtextmarkdownimporter.cpp:57-85 CbEnterBlock/CbLeaveBlock/
 *             CbEnterSpan/CbLeaveSpan/CbText 五回调；import() :122-165 先
 *             doc->clear() 再 beginEditBlock + md_parse + endEditBlock）。
 *             本类同构：纯函数族（无 XObject——导入器无信号槽/子对象，Qt 的
 *             QTextMarkdownImporter 也是无 QObject 基类的普通类），
 *             `XMarkdownParser_parse_utf8(md, doc, features)` 一次调用完成：
 *             - **先清空文档**：`XTextDocument_clear`（对标 doc->clear()
 *               :138——撤销/重做历史随 clear 复位；本机 Qt 6.8.0 offscreen
 *               探针 setmd-undo：setPlainText 种子 + setMarkdown 后 undo 一次
 *               ⇒ toPlainText ""——导入本身成为第一步撤销，导入前历史被
 *               clear 清掉，探针取证）；
 *             - 同一编辑块内逐块写入（beginEditBlock/endEditBlock——Qt
 *               :162-164 同构；块内只压一次快照 ⇒ 整次导入恰好一步撤销，
 *               undo 后回到空文档，与 Qt 探针一致）；
 *             - 写入一律经 `XTextCursor`（insertTextFormat/insertBlock/
 *               setBlockFormat）——不绕过 cursor/格式池（W1 XTextFormat 池
 *               与 W2 XTextCursor 契约照常生效）。
 *
 *             **块级语法 → 文档承载**（Qt 表示法经探针逐项取证）：
 *             - ATX 标题 `#`~`######`：BlockFormat 记 heading level
 *               （qtextmarkdownimporter.cpp:201 blockFmt.setHeadingLevel）+
 *               字符格式 Bold（:200）。任务书称「Qt 不自动改字号」与原文
 *               冲突：Qt 实际写 FontSizeAdjustment(4-level)=+3..-2（:198-199，
 *               探针 headings：h1 frag {bold sizeAdj=3}）。本子集内联格式无
 *               相对字号槽位（XTDCharFormat 只有绝对 fontPointSize），而
 *               渲染层按 headingLevel 自带梯度（Src/XGui/Widget/XTextEdit.c
 *               的 xte_headingPermille：2000/1500/1300/1000/900/800‰）——
 *               该梯度与 Qt qtextformat.cpp:295 的 scaleFactors
 *               （2.0/1.5/1.2/1.0/0.8/0.7）**近似而不相等**（h3 1.3 vs 1.2、
 *               h5 0.9 vs 0.8；h1/h2/h4/h6 一致）。故本类只落
 *               headingLevel+Bold、不写绝对字号（写了会与渲染层双重缩放）；
 *               字号偏差由渲染层既有梯度承担（逐级递减的观感一致）；
 *             - 段落：空行分隔（每段一块）；段内软换行（单 `\n`）→ 单空格
 *               接排（探针 softbreak："soft\nwrap" ⇒ 一块 «soft wrap»）；
 *               硬换行（行尾 ≥2 空格，或行尾 `\`）→ 拆块（探针 hardbreak/
 *               backslash-break：各自两块，qtmi.cpp:472-477 MD_TEXT_BR→
 *               "\n"、MD_TEXT_SOFTBR→" "）；
 *             - 列表：`-`/`*`/`+` 无序、`1.`/`1)` 有序（ListFormat style
 *               逐字符映射 qtmi.cpp:228-238：`-`=Disc `*`=Circle `+`=Square；
 *               :249-251 OL=Decimal+Suffix(delimiter)+Start）。文档模型承载
 *               口径与既有 HTML 管线一致：indentLevel=嵌套深度（1 起）、
 *               setBlockListItem(isListItem,ordered)（XTextDocument.c 的
 *               `<li>` 同款；探针 listInd=1/2/3）。ListFormat 的
 *               style/suffix/start 在文档模型无槽位（XTDBlock 无对应字段，
 *               不得改只读文件）——子集边界，见 @warning；
 *             - 引用块 `>`：深度记 BlockIndent（Qt 落 BlockQuoteLevel 属性
 *               + 左右 margin qtmi.cpp:596-599，属性/margin 在块模型无槽位
 *               ——子集边界；探针 quote=1 取证）。惰性续行（无 `>` 的段落
 *               行接排进引用段，探针 quote：「quoted more after」一块
 *               quoteLevel=1）；
 *             - 围栏代码块 ```/~~~：逐行一块、原文空白保留、等宽（字族
 *               "monospace"——Qt 用 QFontDatabase::systemFont(FixedFont)，
 *               本机探针 fam=monospace；与既有 HTML `<pre>` 管线同字族）；
 *               缩进代码块（4 空格）同承载、无 NonBreakableLines（Qt
 *               :603-606 仅围栏设，探针 indented-code 无 nonBreakable）；
 *               Setext 标题（`===`/`---` 下划线式）：段落已写入后补记
 *               heading level + Bold（探针 setext：before h=1 bold）；
 *             - 水平线 `---`/`***`/`___`：空块（Qt 用
 *               BlockTrailingHorizontalRulerWidth 属性标记 qtmi.cpp:297-302，
 *               块模型无槽位——子集边界；toPlainText 形态一致：探针 hr
 *               "t\n---\n***\na" ⇒ "t\n\n\na"）；
 *             - 前置元材料 `---\n…\n---`（GitHub 档）：setMetaInformation
 *               (FrontMatter)（qtmi.cpp:146-160；探针 frontmatter=«title: x
 *               \nlayout: y\n»）。开/闭标记按**整行**匹配（可有行尾空白；
 *               qtmi.cpp:147 用 indexOf("---") 任意位置搜索，本实现不取
 *               该宽松匹配——"---- x" 类行会被 Qt 误吞）；**所有扫描以
 *               [0,len) 为硬边界**（绝对下标 + 显式行尾，任何分支不得越界
 *               读；内存读越界由 ASan 复跑把关，普通套件只锁语义）；闭标记
 *               行整行消费（`\r\n` 两字节一并跳过），**未闭标记 ⇒ 整文按普通
 *               Markdown 落**（不置 FrontMatter），**零内容前置段拒绝**：
 *               等价 qtmi.cpp:147-148 的 `endMarkerPos > 4`——闭标记必须落在
 *               偏移 5 之后，故 `---\n---`（闭标记恰在 4）不是前置段，落成
 *               两条水平线（本机 Qt 6.8.0 探针 /tmp/qtmdprobe/fm3.cpp：
 *               fm.size=0、blocks=3、plain "\\n\\n"；与 6.8.3 原文同判）。
 *               **与 6.8.0 二进制的已知差异**（6.8.3 原文为准，逐条登记）：
 *               ① 文档恰在闭标记处结束（`---\ntitle: x\n---`，闭标记后无
 *                  换行；含内容仅空行、闭标记即文档尾的情形）—— 6.8.3 原文
 *                  :147-160 无「闭标记后必须有换行」的条件 ⇒ 抽取；本机
 *                  6.8.0 探针不抽取（blocks=3）。另：内容仅空行时正文值本
 *                  实现取 «\n»（与 6.8.0 探针同），6.8.3 的 slicer 同情形为
 *                  «»——仅值不同、抽取与否一致；
 *               ② CRLF 前置段 —— 6.8.3 原文 :150/155 同款跳过 `\n` 与 `\r`
 *                  ⇒ 抽取；6.8.0 探针不抽取。两处均按原文实现并在
 *                  Test/QtAlign/XMarkdownAlignTest.c 的用例注释里写明出处。
 *
 *             **行内语法 → 片段格式**（qtmi.cpp cbEnterSpan :382-430）：
 *             粗体 `**x**`/`__x__`（FontWeight 700）、斜体 `*x*`/`_x_`
 *             （FontItalic）、粗斜体（栈合嵌，探针 nested：三段
 *             bold/bold+italic/bold）、行内代码 `` `x` ``（FontFamilies=
 *             monospace，Qt 另设 FontFixedPitch——内联格式无该位，字族承载）、
 *             链接 `[text](url)`（IsAnchor+AnchorHref 属性 qtmi.cpp:397-407；
 *             Qt 另落调色板链接色/tooltip——渲染层按 anchorHref 自推导链接
 *             呈现（XTextEdit.c:812 isLink→XTE_LINK_COLOR、:831 恒下划线），
 *             与既有 HTML `<a>` 管线同口径，不落 fgColor）、删除线 `~~x~~`
 *             （FontStrikeOut，FeatureStrikeThrough 档）、转义 `\*` 等标点
 *             （md4c 口径：仅 ASCII 标点可转义）、嵌套按格式栈（Qt
 *             m_spanFormatStack :384-386 同构）。实体 `&amp;` 等解码（Qt 走
 *             insertHtml 全表 :482-487；本类内置常用命名实体 + 数字/十六
 *             进制实体，属可取证的常用档）。
 *             **未配对定界符一律按字面**（Qt/md4c 口径：md4c 只对**成对**
 *             span 发 cbEnterSpan/cbLeaveSpan（qtmi.cpp:382-441），未配对的
 *             `**`/`_`/`` ` ``/`~~` 作为 MD_TEXT 原样交给 CbText ⇒ 字面文本、
 *             不带任何样式）：段末仍挂着的开符、以及**被某个 closer 越过
 *             顶掉的未配对开符**都记入字面化集合，实跑时按普通文本发射，
 *             其格式不得延续到后续片段（探针 `**a _b**` ⇒ «a _b»{B}、
 *             `~~a *b~~ c*` ⇒ «a *b»{S}« c*»{}）；扫描终点仍挂着 span 时
 *             先回滚到入口栈基再落尾段（字面化集合有界钳位时的安全态）。
 *
 *             **档位语义**（features 位值逐一对齐 QTextMarkdownImporter::
 *             Feature，static_assert qtmi.cpp:36-52）：
 *             - DialectCommonMark(0)：纯 CommonMark；
 *             - DialectGitHub(**0x104F0C**)：+ 删除线/任务列表/下划线（`_x_`
 *               →下划线非斜体，探针 gh-underline：«em»{underline}）/
 *               前置元材料/宽容自动链接（裸 www./http(s):///邮箱 → 链接，
 *               探针 bare-autolink-gh：www.example.com ⇒ href=
 *               http://www.example.com）。数值取证：QTextDocument::
 *               MarkdownDialectGitHub = 0x0400|0x0800|0x0100|0x0200|0x4000|
 *               0x100000 = **0x104F0C**（qtextdocument.h:119 +
 *               qtextmarkdownimporter_p.h:49；本机 Qt 6.8.0 探针
 *               /tmp/qtprobe_w3a.cpp 打印 feat=0x104F0C；本文件枚举同值）。
 *             两档差异至少覆盖：删除线、下划线、任务列表标记、前置元材料、
 *             宽容自动链接。**不做**（登记为子集边界，不落代码）：表格
 *             （MD_FLAG_TABLES——块模型无表格，GitHub 档位里虽置位但不解
 *               析，管道行按普通段落落）、HTML 块/行内（无 HTML 解析，
 *               原样落文本）、CollapseWhitespace 档（两缺省档都不含）。
 *
 *             **标题字号与内联格式**（Qt 原文 + 探针逐片对拍）：
 *             - 字号：Qt importer 在标题上写 FontSizeAdjustment(4-level)
 *               （qtmi.cpp:198-199，h1..h6 = +3..-2）；本子集内联格式无
 *               相对字号槽位，渲染层按 headingLevel 自带梯度
 *               （Src/XGui/Widget/XTextEdit.c:72-83 xte_headingPermille =
 *               2000/1500/1300/1000/900/800‰）。与 Qt
 *               qtextformat.cpp:295 的 scaleFactors（2.0/1.5/1.2/1.0/0.8/
 *               0.7）**近似而不相等**：h1/h2/h4/h6 一致，h3 偏 +8.3%
 *               （1.3 vs 1.2）、h5 偏 +12.5%（0.9 vs 0.8）；故本类**只落
 *               headingLevel+Bold、不写绝对字号**（写了会与渲染层双重缩放），
 *               偏差由渲染层既有梯度承担。
 *             - 块基 Bold 的作用边界（Qt 语义，探针 /tmp/qtprobe_w3a2.cpp）：
 *               标题粗体只覆盖**尚未开 span** 的文本（cbEnterSpan 从空栈起
 *               :384-386、cbLeaveSpan 回落空格式 :432-441）——
 *               `# plain **b** tail` ⇒ «plain »{B}«b»{B}« tail»{}、
 *               `# a *i* b` ⇒ «a »{B}«i»{I}« b»{}、`a [x](u) *em*\n===` ⇒
 *               «a »{B}«x»{A:u}« »{}«em»{I}。本类按此实现（首个 span 压栈时
 *               块基清零；Setext 用「下一行前瞻」在写文本前就落到标题块，
 *               **不做**事后整块改格式——那会抹掉 href/italic）。
 *
 *             **与 Qt 的已知差异**（模型边界，头注释即契约）：
 *             - 块级 margin（段落 2/3 字号上下边距、引用 40px 侧距）不落
 *               （模型无边距槽位；对排版无影响的纯文本对拍不受影响）；
 *             - 列表 margin/序号前缀不落（模型无边距与 numberPrefix 槽位）；
 *               **列表三属性已落（2026-11-17，B5）**：QTextListFormat 的
 *               Style/Start/NumberSuffix 落 `XTDBlock` 保留位 + 尾部
 *               `listStart`——UL 按标记字符映射（`'*'`→Circle、`'+'`→Square、
 *               缺省含 `'-'`→Disc；qtmi.cpp:222-231 + 探针
 *               /tmp/qtprobe_list.cpp 逐档实测），OL 恒 Decimal +
 *               NumberSuffix=分隔符（`.`/`)`）+ Start=起始号
 *               （qtmi.cpp:242-250）。`listFresh`（同层新列表序号重起）
 *               按 Qt/md4c 的「同层标记字符/有序分隔符变化 ⇒ 新列表」判定
 *               （编号不参与：`1. a\n\n5. b` 是同一列表、第二项 itemText
 *               「2.」，探针 /tmp/qtprobe_list4.cpp）；文档级入口
 *               `XTextDocument_setBlockListFormat` 与 HTML `<ul type>/
 *               <ol start>` 管线共用同一套块字段；
 *             - 水平线 Qt 落 BlockTrailingHorizontalRulerWidth 属性、围栏代码
 *               Qt 落 BlockCodeLanguage/BlockCodeFence/NonBreakableLines
 *               （探针 code-fence/fence-indent2 均可见），块模型无对应槽位 ⇒
 *               只落空块/等宽片段（纯文本形态两侧一致）；
 *             - 图片 `![](...)`：文档模型无按名取图资源体系（XTDFragment
 *               .image 只收编程接口的 XImage 深拷贝），Qt 落 QTextImageFormat
 *               + toPlainText 出 U+FFFC（探针 image）；本类整体跳过图片
 *               （不落 alt 文本——Qt 的纯文本里也没有 alt）；
 *             - **空 href 锚点**：`[x]()` 在 Qt 是「文本 x + A:（isAnchor 且
 *               href 空）」（探针 link-empty）。内联模型只有 anchorHref 一个
 *               锚点槽位、且空串 XString 的 toUtf8 返回 NULL ⇒ 空 href 锚点
 *               落不进片段：本类按 Qt 消费语法（文本为 «x»、不是字面
 *               "[x]()"），但片段无锚点位（渲染不出链接样式）；尖括号目的地
 *               `[x](<u>)` 剥 <> 后正常（探针 link-angle ⇒ href «u»）；
 *             - Setext 仅支持单行（`一行\n===`）；多行 Setext 段落按普通段落落
 *               （未覆盖子集）；
 *             - md4c 的 flanking/intraword 规则按探针可证档实现（`*` 允许
 *               词内、`_` 不允许——探针 intraword：a_b_c 原样、a*b*c 斜体）。
 *             - **部分消费未实现**（closer 比 opener 短/长时 md4c 按
 *               min 逐字符消费）：本类只按**开 run 全量**配对，配对失败或
 *               越过的定界符一律字面落，故 `**a*`（Qt «*»«a»{I}）、
 *               `*a **b* c**`（Qt 全斜体）、`__a _b__`（Qt «_»«a b»{U}）
 *               这类交叉/半开的**切分**与 Qt 不同——但两侧都不会把样式
 *               泄漏到未配对片段之外（本类 `**a*` ⇒ 全字面、
 *               `*a **b* c**` ⇒ «a **b»{I}« c**»{}，均无格式污染）。
 *               良构输入（成对、正常嵌套）逐片段对拍一致（探针 inline/
 *               nested/three-three 等）。
 *             - 相邻同格式片段会被**文档模型合拢**（XTextDocument 的
 *               xtd_blockInsertRun 等格式即并入；HTML 管线同口径），而 Qt
 *               importer 会留下相邻同格式片段（探针 atx-pre-span-post 为
 *               3 片段）。渲染等价，对齐断言按「文本 + 格式位」对拍。
 *
 * @note       模块无独立开关宏：随 XGUI_ON / XTEXTDOCUMENT_ON 裁剪
 *             （XTextCursor.h 同款）。纯函数族，无状态残留。C99。
 *             段缓冲复用框架 XByteArray（铁律「框架有的绝不手搓」）。
 * @warning    **XTextControl 侧入口已接线（2026-11-17）**：`Src/XGui/Text/
 *             XTextControl.c` 的 `XTextControl_setMarkdown` 走本解析器真解析
 *             落内部文档镜像（GitHub 档，同 Qt 缺省），行模型取文档纯文本；
 *             `XTextControl_toMarkdown` 按 `XTextEdit_toMarkdown` 同口径承载
 *             原文（本框架无 Markdown 写出器）。
 *             **不得把这两个入口当 X 独有 API 删除**：Qt 6.8 的
 *             `QWidgetTextControl` 本身就有它们
 *             （`qwidgettextcontrol_p.h:166` setMarkdown / `:95` toMarkdown，
 *             实现 `qwidgettextcontrol.cpp:1193-1196` / `:3188-3190`），而
 *             XTextControl 的头注释即声明「对标 Qt 6.8 QWidgetTextControl」。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XMARKDOWNPARSER_H
#define XMARKDOWNPARSER_H

/* 开关头**先于**门控包含：XGUI_ON/XTEXTDOCUMENT_ON 由 XGuiConfig.h（经
 * CXinYueConfig.h 给 XGUI_ON 缺省）定义。若先 `#if XGUI_ON` 再包含，则
 * 依赖命令行 -DXGUI_ON 的构型下本头会整体消失（消费方的
 * `#if XTEXTDOCUMENT_ON` 仍成立 ⇒ 隐式声明）；关断档（-DXGUI_ON=0 /
 * -DXTEXTDOCUMENT_ON=0）也必须能干净编译（整块实现随门归零）。 */
#include "XGuiConfig.h"

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XTextDocument.h"

#if XTEXTDOCUMENT_ON

/**
 * @brief 特性位（对标 QTextMarkdownImporter::Feature，数值逐一对齐；
 *        qtextmarkdownimporter_p.h:36-52——与 MD_FLAG_* 的 static_assert
 *        同源，位值即 md4c 旗标值）。
 */
typedef enum XMarkdownFeature
{
    XMarkdownFeature_CollapseWhitespace      = 0x0001,   /**< 未实现档（两缺省档不含；MD_FLAG_COLLAPSEWHITESPACE）。 */
    XMarkdownFeature_PermissiveATXHeaders    = 0x0002,   /**< `#标题` 无空格也认标题（MD_FLAG_PERMISSIVEATXHEADERS）。 */
    XMarkdownFeature_PermissiveURLAutoLinks  = 0x0004,   /**< 裸 http(s):// URL 自动链接。 */
    XMarkdownFeature_PermissiveMailAutoLinks = 0x0008,   /**< 裸邮箱自动链接（mailto: 补全）。 */
    XMarkdownFeature_NoIndentedCodeBlocks    = 0x0010,   /**< 禁用 4 空格缩进代码块。 */
    XMarkdownFeature_NoHTMLBlocks            = 0x0020,   /**< 无效应（本子集恒不解析 HTML，原样落文本）。 */
    XMarkdownFeature_NoHTMLSpans             = 0x0040,   /**< 无效应（同上）。 */
    XMarkdownFeature_Tables                  = 0x0100,   /**< 不做（块模型无表格；管道行按普通段落落）。 */
    XMarkdownFeature_StrikeThrough           = 0x0200,   /**< `~~x~~` 删除线（MD_FLAG_STRIKETHROUGH）。 */
    XMarkdownFeature_PermissiveWWWAutoLinks  = 0x0400,   /**< 裸 www. 域名自动链接（href 补 http://）。 */
    XMarkdownFeature_Tasklists               = 0x0800,   /**< `- [ ] `/`- [x] ` 任务标记剥除（标记本身无模型槽位，Qt 落 BlockMarker）。 */
    XMarkdownFeature_Underline               = 0x4000,   /**< `_x_`/`__x__` → 下划线非斜体（MD_FLAG_UNDERLINE）。 */
    XMarkdownFeature_FrontMatter             = 0x100000, /**< 文首 `---` 元材料段 → setMetaInformation(FrontMatter)。 */
    XMarkdownFeature_PermissiveAutoLinks =
        0x0004 | 0x0008 | 0x0400,                       /**< 三种宽容自动链接合成（Qt 同名合成位）。 */
    XMarkdownFeature_NoHTML = 0x0020 | 0x0040,          /**< HTML 双禁合成（Qt 同名；本子集恒等效）。 */

    XMarkdownDialect_CommonMark = 0,                    /**< 纯 CommonMark 档（=0）。 */
    XMarkdownDialect_GitHub = 0x0004u | 0x0008u | 0x0400u |
                              0x0100u | 0x0200u | 0x0800u |
                              0x4000u | 0x100000u       /**< GitHub 档（QTextDocument::MarkdownDialectGitHub 同值；表格位虽含但不做）。 */
} XMarkdownFeature;

/**
 * @brief      解析 Markdown 写入文档（对标 QTextDocument::setMarkdown 的
 *             导入半程：clear + 单编辑块内逐块写入）。
 * @details    先 `XTextDocument_clear`（清撤销/重做历史——Qt doc->clear()
 *             探针口径），再在同一编辑块内逐块写入；整次导入恰好一步撤销
 *             （undo 后回到空文档，Qt 探针一致）。写入一律经 XTextCursor
 *             （格式池影子照常登记）。
 * @param      md UTF-8 Markdown 源文；NULL 按空串（结果=清空后的空文档，
 *             modified 复位——Qt setMarkdown("") 同口径）。
 * @param      doc 目标文档（借用，调用方拥有）；NULL 忽略。
 * @param      features XMarkdownFeature 位组合或档位（XMarkdownDialect_*）。
 * @note       文档内容替换类语义：不保留旧内容、不保留旧撤销史（Qt 同）。
 */
void XMarkdownParser_parse_utf8(const char* md, XTextDocument* doc,
                                int features);

#ifdef __cplusplus
}
#endif

#endif /* XTEXTDOCUMENT_ON */

#endif /* XGUI_ON */
#endif /* XMARKDOWNPARSER_H */
