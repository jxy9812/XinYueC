/**
 * @file       XGuiRemoteTest.h
 * @brief      XGuiRemote 远程窗口回归测试（协议/编解码/回环会话/伤害管线）。
 * @details    覆盖 XGuiRemote.md §9 测试方案 1-3 的单进程自动化部分：
 *               - 协议层（XGuiRemoteProto）：20 条消息 enc→dec 逐字段回环、
 *                 帧泵跨包/粘包/半帧/超 16MiB 拒绝、banner memcmp 口径、
 *                 档位 enc/dec+sanitize 夹取、封闭枚举非法值 dec 拒绝；
 *               - 编解码层（XGuiRemoteCodec）：RLE 冻结格式向量（§5.4）、
 *                 zlib 回环、ARGB32↔RGB565 不透明像素往返位相等、tileHash；
 *               - 回环端到端（零网络）：LoopbackDevice 对 + XGuiServer
 *                 attachTransport + XGuiClient setTransport，握手→FB_META→
 *                 全量 FB_UPDATE（小环容量逼出全有全无写+帧尾待写缓冲
 *                 续传）→客户端 backbuffer 像素抽样==源像素→注入
 *                 INPUT_POINTER 点击远端响应（含抓取越界 i16 负坐标）→BYE；
 *               - 伤害管线：内容未变 tile 哈希去重、maxFps 认领门控节流、
 *                 有界队列水位丢批计数。
 *             开关矩阵：XGUI_REMOTE_ON=0 时模块整体裁空，本套件静默通过
 *             （XKeyboardTest 同款 stub 惯例）；默认（=1）路径全量覆盖。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XGUIREMOTETEST_H
#define XGUIREMOTETEST_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

/** @brief 运行 XGuiRemote 全部回归用例。
 *  @return 全部通过返回 true；任一断言失败返回 false。 */
bool XGuiRemoteTest_runAll(void);

#ifdef __cplusplus
}
#endif
#endif /* XGUIREMOTETEST_H */
