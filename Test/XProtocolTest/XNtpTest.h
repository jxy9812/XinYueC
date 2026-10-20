/** @file       XNtpTest.h
 * @brief      XNtp 客户端测试的公开入口声明（接入 Test 菜单/命令行）。
 * @details    与 Test/XProtocolTest 同族测试一致的注册约定。
 */
#ifndef XNTPTEST_H
#define XNTPTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief XTestMenu 类型前置声明（避免本头文件拖入 GUI 依赖）。 */
struct XTestMenu;
typedef struct XTestMenu XTestMenu;

/**
 * @brief  运行全部 XNtp 测试（编解码/校验闸门/端到端/超时/拒绝/权限/泄漏）。
 * @return  失败计数；0 表示全部通过。
 */
int XNtpTest_run(void);

/**
 * @brief  真实网络校时联调（对 ntp.aliyun.com 发真实请求，逐步打印
 *         DNS 解析/发送/应答各环节，定位「校时没反应」卡在哪）。
 * @return  0=同步成功；1=失败。需要出网（UDP 123）。
 */
int XNtpRealTest_run(void);

/**
 * @brief  全量回归菜单动作（忽略返回码，仅打印结果）。
 */
void XNtpTest(void);

/**
 * @brief  构建并注册 XNtp 测试子菜单到 Test 菜单「协议栈」下。
 * @param  root 父菜单（Test 菜单的「协议栈」节点），可为 NULL。
 */
void XTestMenu_XNtpTest(XTestMenu* root);

#ifdef __cplusplus
}
#endif
#endif /* XNTPTEST_H */
