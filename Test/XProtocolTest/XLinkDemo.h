/** @file       XLinkDemo.h
 * @brief      XLink 实战案例公共头：演示业务协议、日志宏与角色入口。
 * @details    一个"远程电机监控"的完整故事，演示全异步用法的真实形态，
 *             服务端与客户端程序分立两个 .c 便于阅读：
 *               - XLinkDemoServer.c  服务端程序（XLinkServer 监听、异步执行
 *                 命令、周期推送状态、自动回收会话）；
 *               - XLinkDemoClient.c  客户端程序（XLinkClient 包装主动连接，
 *                 跑完命令/推送/订阅全流程）。
 *
 *             演示业务协议（3 条命令 + 2 个 topic，全部小端逐字段序列化）：
 *               CMD_PING(1)  —— 回显 pong（最小请求-应答）；
 *               CMD_ADD(2)   —— 载荷 [u32 a][u32 b]，回 [u32 a+b]（序列化示范）；
 *               CMD_SLOW(3)  —— 模拟真实设备 IO：600ms 后异步补回执，
 *                                 同时推一条 ALARM（异步应答 + 事件通知）；
 *               TOPIC_STATE  —— 服务端每 200ms 推 [u32 tick][u32 counter]；
 *               TOPIC_ALARM  —— 事件通知，产生即推。
 *
 *             登录认证 + 文件传输剧情（v1.3 起）：
 *               服务端开启登录认证（账户 demo / demo123，见下方常量）并挂接
 *               文件服务端（沙箱根目录 = cwd 下 xlink_demo_files/，启动时建、
 *               退出清理；审计回调逐条打日志）；客户端 setCredential 后连接
 *               即自动登录（握手 -> Authenticating -> Ready），Ready 后生成
 *               一个模式化测试文件（256KB）上传、再下载回来逐字节比对——
 *               演示"先登录、后传文件"的真实业务形态。
 *
 *             运行入口：
 *               1. `--test xlink-demo-server`  双进程联调·服务端
 *                  （XLINK_DEMO_BIND 可设监听地址，跨机时设 0.0.0.0）；
 *               2. `--test xlink-demo-client`  双进程联调·客户端
 *                  （XLINK_DEMO_HOST 可设对端 IP，跨机时设服务端地址）；
 *               3. `--test xlink-demo`         本进程联调（CI 友好）
 *                  同一进程内同时扮演两个角色跑真实 TCP 环回。
 *
 *             事件模型（本案例的教学要点）：周期业务注册 XTimer（回调经
 *             timerEvent 在事件循环线程触发，可安全调用会话接口）；主流程
 *             进 XEventLoop_exec 由框架内部驱动，条件满足/超时由定时器
 *             回调退出循环——全程没有手写的 while+sleep 轮询。
 *
 * @author      XinYueC 团队
 */
#ifndef XLINKDEMO_H
#define XLINKDEMO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XLink_config.h"
#include "XPrintf.h"
#include "XDateTime.h"

/* ==================== 演示业务协议 ==================== */

#define XLINK_DEMO_CMD_PING  1u  /**< 回显 pong：最小请求-应答。 */
#define XLINK_DEMO_CMD_ADD   2u  /**< [u32 a][u32 b] -> [u32 a+b]。 */
#define XLINK_DEMO_CMD_SLOW  3u  /**< 600ms 后异步回执 + 推 ALARM。 */

#define XLINK_DEMO_TOPIC_STATE (XLINK_TOPIC_USER_BASE + 1u) /**< 状态快照。 */
#define XLINK_DEMO_TOPIC_ALARM (XLINK_TOPIC_USER_BASE + 2u) /**< 事件通知。 */

#define XLINK_DEMO_PORT          45200u  /**< 双进程联调的固定端口。 */
#define XLINK_DEMO_STATE_MS      200u    /**< 状态推送周期。 */
#define XLINK_DEMO_SLOW_MS       600u    /**< 慢命令的模拟设备 IO 时长。 */
#define XLINK_DEMO_DEADLINE_MS   30000u  /**< 服务端最长等待（联调失败兜底）。 */
#define XLINK_DEMO_CLIENT_WAIT_MS 10000u /**< 客户端连不上的重试上限。 */

/* ==================== 登录认证 + 文件传输剧情 ==================== */

/** @brief 演示账户（服务端 addUser / 客户端 setCredential 同源，改一处即可）。 */
#define XLINK_DEMO_USER          "demo"
#define XLINK_DEMO_PASSWORD      "demo123"

/** @brief 服务端文件沙箱：cwd 下唯一子目录，启动时建、退出时整体清理。 */
#define XLINK_DEMO_FILE_DIR      "xlink_demo_files"
/** @brief 上传到沙箱的远端文件名（相对沙箱根目录）。 */
#define XLINK_DEMO_FILE_NAME     "demo_upload.bin"
/** @brief 客户端本地测试文件（cwd 下，流程结束即删，不进沙箱）。 */
#define XLINK_DEMO_LOCAL_UPLOAD  "xlink_demo_upload.bin"
#define XLINK_DEMO_LOCAL_DOWNLOAD "xlink_demo_download.bin"
/** @brief 模式化测试文件大小（256KB：跨 64 个 4KB 分块、8 个流控窗口）。 */
#define XLINK_DEMO_FILE_BYTES    (256u * 1024u)
/** @brief 单向传输的等待上限（上传走客户端内建 10ms 节拍、下载受服务端
 *         200ms tick 窗口推进，环回实测秒级，20s 已含充分余量）。 */
#define XLINK_DEMO_FILE_WAIT_MS  20000u

/* 跨机联调开关（缺省保持本机环回）：
   XLINK_DEMO_BIND  服务端监听地址，跨机当服务端时设为 0.0.0.0；
   XLINK_DEMO_HOST  客户端连接目标，跨机当客户端时设为对端 IP。 */
#define XLINK_DEMO_BIND_ENV  "XLINK_DEMO_BIND"
#define XLINK_DEMO_HOST_ENV  "XLINK_DEMO_HOST"

/* ==================== 案例日志 ==================== */

/** @brief 统一日志：[毫秒][角色] 内容（毫秒取单调时钟低位便于对轴）。
 *  @note  案例仅桌面构建编译（嵌入式不编 Test/）。MSVC 传统预处理器
 *         不支持 GNU `##__VA_ARGS__` 空参吞逗号，故按参数个数拆成两个宏：
 *         DEMO_LOG 带格式参数、DEMO_LOG0 无格式参数。 */
#define DEMO_LOG(who, fmt, ...) \
    XPrintf("[%9u][demo-" who "] " fmt "\n", \
            (unsigned)(XDateTime_currentMSecsSinceEpoch() % 1000000000u), \
            __VA_ARGS__)
#define DEMO_LOG0(who, fmt) \
    XPrintf("[%9u][demo-" who "] " fmt "\n", \
            (unsigned)(XDateTime_currentMSecsSinceEpoch() % 1000000000u))

/* ==================== 角色组合接口（本进程联调用） ==================== */

/**
 * @brief 客户端角色完整故事：连接（自动登录）-> 等 Ready -> 三条命令 ->
 *        收推送 -> 文件上传/下载逐字节校验 -> 推送与订阅全景 -> Goodbye。
 * @param port 服务端端口。
 * @return 全部步骤通过返回 0；任一步失败返回 1（已打印现场）。
 * @note  由 XLinkDemoClient_run 直接调用；本进程联调（XLinkDemo_run）经
 *        此入口把客户端角色接进同一事件循环。应用学写客户端照抄本函数：
 *        建链/握手/登录经 XLinkClient（setCredential + connectToServer +
 *        stateChanged 信号），文件传输经 XLinkFileClient（upload/download
 *        + progress/finished 信号，纯事件循环驱动、不手动 pump），Goodbye
 *        排空收尾由 disconnectFromServer 承担。
 * @note  等待一律"注册检查定时器 + 嵌套 XEventLoop_exec"：条件满足或超时
 *        由定时器回调退出循环，不手写 while+sleep 轮询。服务端的周期
 *        业务同样注册在 XTimer 上，exec 期间自动触发——两个角色在同一个
 *        事件循环里各得其所。
 */
int XLinkDemo_client_flow(uint16_t port);

/**
 * @brief 取跨机联调地址：环境变量优先，缺省本机环回。
 * @param env 环境变量名（XLINK_DEMO_BIND_ENV / XLINK_DEMO_HOST_ENV）。
 * @param fallback 缺省值（"127.0.0.1"）。
 * @return 地址串（借用，内部静态或环境块，调用方不得释放）。
 */
const char* XLinkDemo_envOr(const char* env, const char* fallback);

/* ==================== 运行入口 ==================== */

/** @brief 双进程联调·服务端：监听 45200 等客户端，交互完整或超时退出。
 *  @return 客户端正常离开且全部交互完成返回 0；否则 1。 */
int XLinkDemoServer_run(void);

/** @brief 双进程联调·客户端：连接 45200 跑完全部步骤。
 *  @return 全部步骤通过返回 0；任一步失败返回 1。 */
int XLinkDemoClient_run(void);

/** @brief 本进程联调：服务端角色 + 客户端角色同进程跑真实 TCP 环回。
 *  @return 客户端流程与服务端交互统计全部达标返回 0；否则 1。 */
int XLinkDemo_run(void);

/** @brief 菜单动作包装（Test 菜单「协议栈 → XLink → 实战案例」）。 */
void XLinkDemoServer(void);
void XLinkDemoClient(void);
void XLinkDemo(void);

#ifdef __cplusplus
}
#endif
#endif /* XLINKDEMO_H */
