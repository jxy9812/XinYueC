/** @file       XLinkTest.h
 * @brief      XLink 私有应用层协议测试的公开入口声明（接入 Test 菜单/命令行）。
 * @details    与 Test/XProtocolTest 同族测试（XMqtt/XModbus/XCan）一致的注册
 *             约定：对外暴露 `void XxxTest(void)` 菜单动作函数，由
 *             XTestMenu_setActionFunction 绑定；另保留 `_run` 形式供命令行
 *             分派与自动化脚本按返回码判定成败。
 */
#ifndef XLINKTEST_H
#define XLINKTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief XTestMenu 类型前置声明（避免本头文件拖入 GUI 依赖）。 */
struct XTestMenu;
typedef struct XTestMenu XTestMenu;

/**
 * @brief  运行全部 XLink 测试（编解码/会话/服务端与客户端包装全部分组）。
 * @return  失败计数；0 表示全部通过。
 * @note    供命令行分派与自动化回归使用，返回码可直接作为成败判据。
 */
int XLinkTest_run(void);
/**
 * @brief  全量回归菜单动作（忽略返回码，仅打印结果）。
 * @note   菜单入口签名与 XMqttPublicApiTest 等同族测试一致。
 */
void XLinkTest(void);

/**
 * @brief  仅运行编解码层测试。
 * @note   编解码为纯函数、无外部依赖，可脱离传输单独快速回归。
 */
void XLinkCodecTest(void);

/**
 * @brief  仅运行会话层测试（生命周期/状态机/握手/请求应答/推送/订阅/超时/
 *         心跳/防御性路径）。
 */
void XLinkSessionTest(void);

/**
 * @brief  仅运行服务端包装测试（监听接入/模板下发/自动握手/请求应答/
 *         会话回收）。
 * @note   依赖真实环回 TCP 与事件处理；网络环境受限时可单独跳过本项。
 */
void XLinkServerTest(void);

/**
 * @brief  仅运行客户端包装测试（NULL 防御/默认值与生命周期/正常链路/踢线
 *         自动重连/重连关停/手动断开/最大重连次数）。
 * @return  失败计数；0 表示全部通过。
 * @note   即 XLinkTest.c 分组十四（原 XLinkClientTest.c 已整体并入）；
 *         依赖真实环回 TCP（127.0.0.1:47321）与事件处理，命令行入口
 *         `--test xlink-client` / Test 命令 `Test xlink-client`。
 */
int XLinkClientTest_run(void);

/**
 * @brief  客户端包装测试菜单动作（忽略返回码，仅打印结果）。
 * @note   菜单入口签名与 XLinkServerTest 等同族测试一致。
 */
void XLinkClientTest(void);

/**
 * @brief  构建并注册 XLink 测试子菜单到 Test 菜单「协议栈」下。
 * @param  root 父菜单（Test 菜单的「协议栈」节点），可为 NULL。
 * @note   菜单项：全量回归 / 编解码层 / 服务端包装 / 客户端包装 /
 *         实战案例（本进程联调）/ 实战案例·服务端 / 实战案例·客户端 /
 *         会话层 / 质量门禁（泄漏+性能+边界）。
 */
void XTestMenu_XLinkTest(XTestMenu* root);

/**
 * @brief  运行 XLink 质量门禁：内存泄漏 / 性能基准 / 边界与对抗。
 * @return  失败计数；0 表示全部通过。
 * @note    与 XLinkTest_run 的功能覆盖互补：验证「可靠 + 无泄漏 + 高性能」。
 */
int XLinkQualityTest_run(void);

/**
 * @brief  质量门禁菜单动作（打印泄漏统计与性能基准数据）。
 */
void XLinkQualityTest(void);

#ifdef __cplusplus
}
#endif
#endif /* XLINKTEST_H */
