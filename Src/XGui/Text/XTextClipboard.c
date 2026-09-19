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
#if XCLIPBOARD_ON && XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#include "XClipboard.h"
#include "XString.h"
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */

/* ==================== 内部静态缓冲 ==================== */

/** @brief 回退存储与取文本缓存共用的内部缓冲；服务生命周期持有。 */
static char*  s_buffer   = NULL;
/** @brief s_buffer 的当前容量（字节，含结尾 '\0'）；避免反复扩容。 */
static size_t s_capacity = 0;

/**
 * @brief      把指定长度的 UTF-8 内容写入内部静态缓冲（按需扩容）。
 * @param      src 源内容借用指针；可为 NULL（等价空内容）。
 * @param      len 源内容字节数（不含结尾 '\0'）。
 * @return     成功返回缓冲借用指针；扩容失败返回 NULL（保留旧内容，
 *             与基准实现 realloc 失败回退行为一致）。
 */
static const char* xtextclipboard_store(const char* src, size_t len)
{
    char* updated;
    if (s_capacity < len + 1) {
        updated = (char*)XRealloc_System(s_buffer, len + 1);
        if (!updated) return NULL;
        s_buffer = updated;
        s_capacity = len + 1;
    }
    if (len > 0 && src) XMemcpy(s_buffer, src, len);
    s_buffer[len] = '\0';
    return s_buffer;
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
                XString_delete_base((XClass*)str);
            }
            return;
        }
    }
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
    {
        /* 基准路径二：剪贴板不可用时回退内部缓冲（原 m_clipboardText
           的 XRealloc_System + 拷贝行为）。 */
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
                const char* cached = xtextclipboard_store(utf8, len);
                XString_delete_base((XClass*)str);
                if (cached) return cached;
                return ""; /* 扩容失败（OOM）退化为空串。 */
            }
            return ""; /* 剪贴板无文本（基准返回 NULL 的等价形态）。 */
        }
    }
#endif /* XCLIPBOARD_ON && XGUIAPPLICATION_ON */
    /* 基准路径二：回退内部缓冲；从未写入过返回空串。 */
    return s_buffer ? s_buffer : "";
}

#endif /* XTEXTCLIPBOARD_ON */
