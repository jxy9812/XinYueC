/**
 * @file       HsfWebServer.h
 * @brief      网页服务（webui）随客户端起停 + 端口设置（app 层，桌面端）。
 * @details    [为什么要有这一层] 网页版（HighSpeedFilter/webui_py/hsfweb）是
 *             **独立进程**（纯标准库 Python 服务），此前只能手工起、端口写死
 *             在命令行里；桌面上「客户端和网页同时用」是常态 ⇒ 起停与
 *             端口必须**在客户端里可配**，不能靠人记命令。
 *
 *             [职责边界]
 *               ① 持久化两个设置：`WebServer/Enabled`（是否随客户端启动）、
 *                  `WebServer/Port`（网页监听端口）；
 *               ② 拉起/停止子进程 `python -m hsfweb --server <host>
 *                  --port <上游端口> --web-port <网页端口>`（工作目录 =
 *                  网页端包目录，stdout/stderr 落 cfg/hsf_web.log）；
 *               ③ 暴露状态（运行中/已停止/启动失败原因）给界面显示。
 *             **不**负责网页端自身逻辑（那是 webui_py 的事）；**不**代理
 *             浏览器侧会话（浏览器自己登录上游）。
 *
 *             [上游是谁] 网页端连的是**同一个 XLink 服务端**（不是客户端）：
 *             网页端自己拿 root 主链路连上游、浏览器再各自登录。所以上游
 *             地址取客户端当前连的那台（HsfLinkClient_serverAddress），
 *             未登录时回落 127.0.0.1:6100（本地缺省）。
 *
 *             [端口缺省为什么不是 8080] 网页端缺省端口是 8080，但**本机
 *             8080 常被别的程序占着**（实测：另一个常驻 GUI 监听 8080）⇒
 *             缺省取 8085，且启动前会探端口；被占用时状态行如实报「端口
 *             被占用」并把日志尾巴给出来（不静默失败）。
 *
 *             [平台] 安卓端**不支持**（无外部进程与 python）：start() 直接
 *             返回 false、状态行说明原因，不影响其余功能。
 */
#ifndef HSFWEBSERVER_H
#define HSFWEBSERVER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/** @brief 网页端口缺省（**不是 8080**：本机 8080 常被别的程序占用）。 */
#define HSF_WEB_DEFAULT_PORT 8085
/** @brief 网页端口下限（1024 以下属特权端口，不给用户选）。 */
#define HSF_WEB_MIN_PORT     1024
/** @brief 网页端口上限。 */
#define HSF_WEB_MAX_PORT     65535

/**
 * @brief 是否随客户端启动网页服务（持久化设置，缺省**开**）。
 */
bool HsfWebServer_enabled(void);

/**
 * @brief 设置「随客户端启动网页服务」并**即时生效**（开⇒拉起，关⇒停）。
 * @param on true=开。
 * @note  落盘（WebServer/Enabled，int 0/1）+ 立即起停；页面上改完立刻见效。
 */
void HsfWebServer_setEnabled(bool on);

/**
 * @brief 网页监听端口（持久化设置；缺省 HSF_WEB_DEFAULT_PORT）。
 * @return 端口号（越界值已夹到合法区间）。
 */
int HsfWebServer_port(void);

/**
 * @brief 设置网页端口并**即时生效**（端口变了要重启子进程才换监听口）。
 * @param port 端口号（越界会被夹到 [HSF_WEB_MIN_PORT, HSF_WEB_MAX_PORT]）。
 */
void HsfWebServer_setPort(int port);

/**
 * @brief 拉起网页服务子进程。
 * @return 拉起成功返回 true；失败返回 false 并写好失败原因（见 status）。
 * @note  已在跑时直接返回 true（幂等）。
 */
bool HsfWebServer_start(void);

/**
 * @brief 停止网页服务子进程（幂等；未在跑时是空操作）。
 */
void HsfWebServer_stop(void);

/**
 * @brief 重启网页服务子进程（换上游/换端口后用）。
 * @return 重启后是否在跑。
 * @note  登录成功后用：此刻才知道真正的上游地址（登录前只能拿
 *        「上次连过的地址」兜底）⇒ 重启一次让网页端连对机器。
 */
bool HsfWebServer_restart(void);

/**
 * @brief 网页服务当前是否在运行（子进程活着）。
 */
bool HsfWebServer_running(void);

/**
 * @brief 网页服务状态文案（给界面状态行显示）。
 * @param buf 输出缓冲；cap 容量。
 * @return buf（永远写出一个 NUL 结尾的串）。
 * @details 形如「运行中 · http://127.0.0.1:8085」「已停止」
 *          「启动失败：端口 8085 被占用」。
 */
const char* HsfWebServer_status(char* buf, int cap);

/**
 * @brief 网页访问地址（形如 http://127.0.0.1:8085；借用静态串）。
 */
const char* HsfWebServer_url(void);

/**
 * @brief 推进子进程状态机（帧泵调用；子进程退出即被察觉）。
 * @details 抽出来是为了让**调用方决定节奏**（客户端帧泵每拍调一次即可）。
 */
void HsfWebServer_poll(void);

#ifdef __cplusplus
}
#endif
#endif /* HSFWEBSERVER_H */
