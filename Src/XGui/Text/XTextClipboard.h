/**
 * @file       XTextClipboard.h
 * @brief      XTextClipboard 文本剪贴板往返服务（XGui 文本共享服务层）。
 * @details    把 XLineEdit 与 XPlainTextEdit 各自维护的剪贴板读写收敛为
 *             控件无关的文本服务，行为为两套基准实现的并集：
 *             - 写入：优先使用 XGuiApplication_clipboard() 的进程内
 *               XClipboard（XClipboardMode_Clipboard，文本按 UTF-8 承载，
 *               与两套控件基准实现一致）；GUI 应用或剪贴板子模块不可用
 *               （XGUIAPPLICATION_ON/XCLIPBOARD_ON 关闭、应用未创建）时
 *               回退本服务内部静态缓冲（原 m_clipboardText 控件成员的
 *               服务化替代）。
 *             - 读取：优先从进程内 XClipboard 取 XString，并经
 *               XString_toUtf8 完成内部编码（UTF-16）到 UTF-8 的转换，
 *               结果缓存进内部静态缓冲后以借用指针返回；系统剪贴板无
 *               文本或整体不可用时返回内部缓冲内容，从未写入过则返回
 *               空串 ""（对接两版基准中"无文本返回 NULL"的调用方判定，
 *               语义等价：空串即无文本）。
 * @note       模块开关 XTEXTCLIPBOARD_ON 由构建配置定义；关闭时本头文件
 *             全部声明被裁剪。getText 返回的是服务内部缓冲的借用指针，
 *             仅保证到本服务下一次任意接口调用前有效；调用方不得释放
 *             或修改。本服务无对象句柄，全部接口线程约定与 XGui 一致：
 *             在 GUI 线程调用。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XTEXTCLIPBOARD_H
#define XTEXTCLIPBOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XGuiConfig.h"

#if XTEXTCLIPBOARD_ON

/**
 * @brief      把 UTF-8 文本写入剪贴板（对标两套控件基准的
 *             setClipboardText 路径）。
 * @details    进程内剪贴板可用时经 XClipboard_setText 写入
 *             XClipboardMode_Clipboard 模式（深拷贝，调用方保留所有权）；
 *             不可用时回退内部静态缓冲。text 为 NULL 时不执行任何操作
 *             （与基准实现前置判空一致），不会清空既有剪贴板内容。
 * @param      text 以 '\0' 结尾的 UTF-8 文本；可为 NULL（无操作）。
 * @return     无返回值。
 */
void XTextClipboard_setText(const char* text);

/**
 * @brief      读取剪贴板 UTF-8 文本（对标两套控件基准的
 *             getClipboardText 路径）。
 * @details    进程内剪贴板可用时经 XClipboard_text 取 XString 并做
 *             UTF-16 到 UTF-8 的转换，结果缓存进内部静态缓冲后返回借用
 *             指针；剪贴板无文本或不可用时返回内部缓冲内容（回退写入
 *             路径的结果），从未写入过则返回空串 ""。
 * @return     服务内部缓冲的借用指针（'\0' 结尾的 UTF-8）；空时返回
 *             ""。指针仅保证到本服务下一次任意接口调用前有效，调用方
 *             不得释放或修改。
 */
const char* XTextClipboard_getText(void);

#endif /* XTEXTCLIPBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTEXTCLIPBOARD_H */
