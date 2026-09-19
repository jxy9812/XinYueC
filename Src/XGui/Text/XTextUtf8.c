/**
 * @file       XTextUtf8.c
 * @brief      XGui 文本工具共享层：UTF-8 码点边界与序列长度计算实现。
 * @details    语义基准为两套既有实现：XLineEdit.c 的
 *             xlineedit_prevBoundary/xlineedit_nextBoundary（续字节
 *             0x80..0xBF 感知的双向边界扫描）与 XPlainTextEdit.c 的
 *             xpe_utf8SeqLen/xpe_prevBoundary（remain 上限钳位与列
 *             钳位）。全部为纯函数：无动态分配、无平台依赖，边界
 *             扫描行为与基准逐点一致；仅 seqLen 对 NULL/remain<=0
 *             按共享层契约返回 0。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XTextUtf8.h"
#include "XGuiConfig.h"

#if XTEXTUTF8_ON

/* ==================== 序列长度 ==================== */

int XTextUtf8_seqLen(const char* s, int remain)
{
    unsigned char c0;
    int need = 1;
    if (!s || remain <= 0) return 0;
    c0 = (unsigned char)s[0];
    if (c0 < 0x80u) return 1;                 /* ASCII 单字节。 */
    if ((c0 & 0xE0u) == 0xC0u) need = 2;      /* 110xxxxx：双字节。 */
    else if ((c0 & 0xF0u) == 0xE0u) need = 3; /* 1110xxxx：三字节。 */
    else if ((c0 & 0xF8u) == 0xF0u) need = 4; /* 11110xxx：四字节。 */
    else return 1;                            /* 续字节或非法首字节：按 1 字节推进。 */
    if (need > remain) need = remain;         /* 钳位：序列不越界（remain>0 恒 >=1）。 */
    return need;
}

/* ==================== 码点边界 ==================== */

size_t XTextUtf8_prevBoundary(const char* s, size_t pos)
{
    if (!s || pos == 0) return 0;
    --pos;
    while (pos > 0 && ((unsigned char)s[pos] & 0xC0u) == 0x80u) --pos;
    return pos;
}

size_t XTextUtf8_nextBoundary(const char* s, size_t pos)
{
    if (!s || !s[pos]) return pos;
    ++pos;
    while (s[pos] && ((unsigned char)s[pos] & 0xC0u) == 0x80u) ++pos;
    return pos;
}

#endif /* XTEXTUTF8_ON */
