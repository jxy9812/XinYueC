/** @file       XDdnsTest.h
 * @brief      XDdns 动态域名解析测试的公开入口声明（接入 Test 菜单/命令行）。
 * @details    与 Test/XProtocolTest 同族测试一致的注册约定。
 */
#ifndef XDDNSTEST_H
#define XDDNSTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/** @brief XTestMenu 类型前置声明。 */
struct XTestMenu;
typedef struct XTestMenu XTestMenu;

/**
 * @brief  运行全部 XDdns 测试（签名确定性/应答解析/端到端/泄漏）。
 * @return  失败计数；0 表示全部通过。
 */
int XDdnsTest_run(void);

/**
 * @brief  真实网络全链路联调（真实探测源 + 真实阿里云 API）。
 * @return  0=链路通；1=失败。
 * @note    凭据经环境变量传入：XDNS_AKID / XDNS_SECRET（必需），
 *          XDNS_DOMAIN（默认 727370630.top）/ XDNS_RR（默认 www）可选。
 */
int XDdnsRealTest_run(void);

/**
 * @brief  全量回归菜单动作（忽略返回码，仅打印结果）。
 */
void XDdnsTest(void);

/**
 * @brief  构建并注册 XDdns 测试子菜单到 Test 菜单「协议栈」下。
 * @param  root 父菜单（Test 菜单的「协议栈」节点），可为 NULL。
 */
void XTestMenu_XDdnsTest(XTestMenu* root);

#ifdef __cplusplus
}
#endif
#endif /* XDDNSTEST_H */
