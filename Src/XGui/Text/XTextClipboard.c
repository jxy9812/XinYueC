/**
 * @file       XTextClipboard.c
 * @brief      XTextClipboard 文本剪贴板往返服务实现。
 * @details    写入/读取路径与 XLineEdit.c、XPlainTextEdit.c 的两套基准
 *             实现逐点对齐（取并集）：优先 XGuiApplication_clipboard 的
 *             XClipboard（XClipboardMode_Clipboard，UTF-8 文本，读取经
 *             XString_toUtf8 做内部编码到 UTF-8 的转换）；剪贴板不可用
 *             （XCLIPBOARD_ON/XGUIAPPLICATION_ON 关闭或应用未创建）时
 *             回退内部静态缓冲。原基准中"读取返回堆拷贝/NULL"的形态在
 *             服务层收敛为"内部缓冲承载的借用指针/空串"，判定语义不变。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XTextClipboard.h"

#if XTEXTCLIPBOARD_ON

#include "XMemory.h"
#include "XStringUtils.h"
#include "XString.h"
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#include "XClipboard.h"
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */

/* ==================== 内部静态缓冲 ==================== */

/** @brief 回退存储与取文本缓存共用的服务自有字符串；服务生命周期持有。 */
static XString s_text;
/** @brief s_text 是否已完成初始化（静态零初值不等于已 init）。 */
static bool s_inited = false;

/**
 * @brief      取服务自有字符串的 UTF-8 转换缓存借用指针。
 * @return     缓存借用指针（'\0' 结尾的 UTF-8）；从未写入、内容为空或
 *             转换缓存分配失败（OOM）时返回空串 ""。
 */
static const char* xtextclipboard_cachedText(void)
{
    const char* utf8;
    if (!s_inited || XString_isEmpty_base(&s_text)) return "";
    utf8 = XString_toUtf8(&s_text);
    return utf8 ? utf8 : "";
}

/**
 * @brief      把指定长度的 UTF-8 内容写入服务自有 XString（替换旧内容）。
 * @param      src 源内容借用指针；可为 NULL（等价空内容）。
 * @param      len 源内容字节数（不含结尾 '\0'）。
 * @return     成功返回 true；分配/转换失败返回 false（此时 XString 按
 *             assign 语义保持清空状态，与读取路径 OOM 退化为空串的
 *             约定一致）。
 */
static bool xtextclipboard_store(const char* src, size_t len)
{
    if (!s_inited) {
        XString_init(&s_text);
        s_inited = true;
    }
    return XString_assign_with_length_utf8(&s_text, src ? src : "", len);
}

/* ==================== 公开接口 ==================== */

void XTextClipboard_setText(const char* text)
{
    if (!text) return;
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    {
        /* 基准路径一：进程内剪贴板可用时独占使用（与 XLineEdit::
           xlineedit_setClipboardText / XPlainTextEdit_copy 一致，
           此时不同步回退缓冲）。 */
        XClipboard* cb = XGuiApplication_clipboard();
        if (cb) {
            XString* str = XString_create_utf8(text);
            if (str) {
                XClipboard_setText(cb, str, XClipboardMode_Clipboard);
                XClassDelete((XClass*)str);
            }
            return;
        }
    }
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
    {
        /* 基准路径二：剪贴板不可用时回退内部缓冲（原 m_clipboardText
           的服务化替代存储）。 */
        (void)xtextclipboard_store(text, XStrlen(text));
    }
}

const char* XTextClipboard_getText(void)
{
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
    {
        /* 基准路径一：进程内剪贴板可用时取 XString 并转 UTF-8 后缓存
           （原 XClipboard_text + XString_toUtf8/toUtf8_length 转换与
           拷贝行为；借用返回要求结果落在服务自有缓冲上）。 */
        XClipboard* cb = XGuiApplication_clipboard();
        if (cb) {
            XString* str = XClipboard_text(cb, XClipboardMode_Clipboard);
            if (str) {
                const char* utf8 = XString_toUtf8(str);
                size_t len = XString_toUtf8_length(str);
                bool stored = xtextclipboard_store(utf8, len);
                XClassDelete((XClass*)str);
                if (stored) return xtextclipboard_cachedText();
                return ""; /* 扩容失败（OOM）退化为空串。 */
            }
            return ""; /* 剪贴板无文本（基准返回 NULL 的等价形态）。 */
        }
    }
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
    /* 基准路径二：回退内部缓冲；从未写入过返回空串。 */
    return xtextclipboard_cachedText();
}

#endif /* XTEXTCLIPBOARD_ON */
