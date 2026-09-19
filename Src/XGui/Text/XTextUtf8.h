/**
 * @file       XTextUtf8.h
 * @brief      XGui 文本工具共享层：UTF-8 码点边界与序列长度计算。
 * @details    抽取 XLineEdit（xlineedit_prevBoundary/
 *             xlineedit_nextBoundary）与 XPlainTextEdit（xpe_utf8SeqLen/
 *             xpe_prevBoundary）两套重复的 UTF-8 码点边界实现为纯函数
 *             共享层：光标/选区/列偏移的字节位置恒定落在字符边界上，
 *             依赖续字节（0x80..0xBF）感知的双向扫描回退/前进；序列
 *             长度按首字节前缀判别 1~4 字节并以剩余字节上限钳位，
 *             保证只推进不越界、不切断多字节字符。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTEXTUTF8_H
#define XTEXTUTF8_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

/**
 * @brief      计算 s 起始 UTF-8 序列的字节长度（用于按码点推进列偏移）。
 * @details    按首字节前缀判别 1~4 字节序列（0xxxxxxx=1、110xxxxx=2、
 *             1110xxxx=3、11110xxx=4）；续字节或非法首字节按 1 字节
 *             返回（与 XPainter 解码口径一致：只推进不越界）；序列
 *             超出剩余字节上限时钳位为该上限（对标 XPlainTextEdit 的
 *             xpe_utf8SeqLen 钳位语义）。
 * @param      s 缓冲区当前字节指针（s[0] 可读）；NULL 视为越界。
 * @param      remain 自 s 起的剩余字节上限。
 * @return     该码点的字节长度（1~4，钳位后）；s 为 NULL 或
 *             remain<=0 返回 0。
 */
int XTextUtf8_seqLen(const char* s, int remain);

/**
 * @brief      从字节偏移 pos 向前回退到 UTF-8 字符边界。
 * @details    自 pos-1 起连续跳过 UTF-8 续字节（0x80..0xBF），停在
 *             首字节或缓冲区起点（对标 XLineEdit 的
 *             xlineedit_prevBoundary 与 XPlainTextEdit 的
 *             xpe_prevBoundary 反向扫描）。
 * @param      s UTF-8 文本缓冲区（NUL 结尾）；可为 NULL。
 * @param      pos 当前字节偏移。
 * @return     回退后的字符边界偏移；s 为 NULL 或 pos==0 返回 0。
 */
size_t XTextUtf8_prevBoundary(const char* s, size_t pos);

/**
 * @brief      从字节偏移 pos 向后前进到 UTF-8 字符边界。
 * @details    若 s[pos] 为非 NUL 字节则先跨过该字节，再连续跳过
 *             UTF-8 续字节（0x80..0xBF），停在下一首字节或 NUL 终止
 *             处（对标 XLineEdit 的 xlineedit_nextBoundary）。
 * @param      s UTF-8 文本缓冲区（NUL 结尾）；可为 NULL。
 * @param      pos 当前字节偏移。
 * @return     前进后的字符边界偏移；s 为 NULL 或 s[pos] 为 NUL
 *             （已到末尾）返回 pos 原值。
 */
size_t XTextUtf8_nextBoundary(const char* s, size_t pos);

#ifdef __cplusplus
}
#endif
#endif /* XTEXTUTF8_H */
