/**
 * @file       XGuiServer.h
 * @brief      XGuiServer GUI 服务器契约(冻结头, 只声明 API 不含实现)。
 * @details    远程窗口服务端: 镜像本机顶层控件画面(共存模式), 或在无屏
 *             设备上以纯软件虚拟帧缓冲承载 GUI(headless 模式——部署形态,
 *             非代码分支, 见 XGuiRemote.md §6.7)。接受多客户端会话:
 *             推送增量画面(FB_UPDATE), 接收远端输入并经
 *             XWindowSystemInterface_handle* 注入本机事件系统(双向闭环)。
 *             线程约定:
 *               - 本对象与全部公共 API 仅限 GUI 线程使用(XObject, 事件
 *                 循环驱动);
 *               - 每会话内部持有独立编码线程与有界队列(实现私有, 不入
 *                 本契约); 设备读写全部收敛在 GUI 线程(poll 回调驱动);
 *               - 信号均在 GUI 线程发射。
 *             生命周期: 堆对象 create_ex+init / XClass_delete_base 立即
 *             释放, 或 XObject_deleteLater 延迟释放(仓库通用约定);
 *             析构内部保证: 解除 present 回调登记(恢复旧回调)、逐会话
 *             停机编码线程并断开设备。
 * @note       模块开关 XGUI_REMOTE_ON 见 XGuiRemoteProto.h(本头随其裁空)。
 * @author     XinYueC 团队
 */
#ifndef XGUISERVER_H
#define XGUISERVER_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XObject.h"
#include "XGuiRemoteProto.h"

#if XGUI_REMOTE_ON

/** @brief XWidget 前置声明(被镜像的顶层控件; 完整定义见 XWidget.h)。 */
typedef struct XWidget XWidget;

/** @brief XGuiServer 虚函数枚举(继承 XObject, 无新增槽位)。 */
XCLASS_DEFINE_BEGING(XGuiServer)
XCLASS_DEFINE_EXTEND_END(XGuiServer, XObject)

/**
 * @brief      XGuiServer 服务对象; m_class 必须为第一个成员。
 * @details    会话/影子缓冲/编码线程等实现细节封装在私有块 m_d 中
 *             (实现文件内定义), 冻结契约不暴露。
 */
typedef struct XGuiServer {
    XObject m_class;            /**< 基类成员; 必须是第一个。 */
    void*   m_d;                /**< 私有实现块(拥有; 实现文件内定义)。 */
} XGuiServer;

/* ==================== 生命周期 ==================== */

/** @brief 初始化类虚函数表, 返回共享 XVtable 指针。 */
XVtable* XGuiServer_class_init(void);

/**
 * @brief  初始化服务对象(对标构造函数)。
 * @param  self   尚未初始化的对象。
 * @param  parent 父对象(借用; 可为 NULL, 为 NULL 时调用方负责 delete)。
 */
void XGuiServer_init(XGuiServer* self, XObject* parent);

/** @brief 默认内存类型创建便捷宏(仓库惯例, 见 XSplitter.h)。 */
#define XGuiServer_create(parent) \
    XGuiServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent))
/**
 * @brief  按指定内存类型创建服务对象。
 * @return 新实例; 分配失败返回 NULL(需 XClass_delete_base /
 *         XObject_deleteLater 释放)。
 */
XGuiServer* XGuiServer_create_ex(XMemoryType memory, XObject* parent);

/** @brief 析构/反初始化映射(仓库惯例)。 */
/** @brief 延迟释放别名(事件循环内安全自删)。 */
#define XGuiServer_deleteLater       XObject_deleteLater

/* ==================== 镜像目标(共存模式, 需求 7) ==================== */

/**
 * @brief      绑定要镜像的顶层控件并登记 present 回调。
 * @details    内部取链 XWidget_backingStore → XBackingStore_handle →
 *             XPlatformBackingStore_setPresentCallback; 采取"包装转发"
 *             策略: 保存并链式调用既有回调, unhost 时原样恢复(单槽共存,
 *             见 XGuiRemote.md §6.2)。伤害采集在 present 回调(GUI 线程)
 *             内完成——回调返回后后备缓冲翻转, 必须回调内拷贝, 此为红线。
 * @param      topLevel 顶层控件(借用); 不可为 NULL, 必须是顶层
 *             (XWidget_isWindow 为 true), 否则返回 false。
 * @return     true 绑定成功; false 已有绑定目标/控件非法/无后备存储。
 * @note       绑定期间 topLevel 销毁前必须先 unhost(否则悬挂)。
 */
bool XGuiServer_host(XGuiServer* self, XWidget* topLevel);

/** @brief 解除绑定并恢复原 present 回调; 会话转入"无画面"等待态。 */
void XGuiServer_unhost(XGuiServer* self);

/** @brief 当前镜像目标(借用); 未绑定返回 NULL。 */
XWidget* XGuiServer_hostedWidget(const XGuiServer* self);

/* ==================== 监听与接入(需求 2: 传输无关) ==================== */

/**
 * @brief      开始监听所有本机地址的 port 端口(TCP; TLS 档由 policy 决定
 *             accept 后是否升级, 见 XGuiRemote.md §4.3)。
 * @return     false 端口占用/已监听/参数非法。
 */
bool XGuiServer_listen(XGuiServer* self, uint16_t port);

/**
 * @brief      监听指定地址(点分 IPv4 文本或主机名; NULL 等价所有地址)。
 */
bool XGuiServer_listen_2(XGuiServer* self, const char* addressUtf8,
                         uint16_t port);

/** @brief 停止监听并断开全部会话(发 BYE, 原因 SERVER_SHUTDOWN)。 */
void XGuiServer_close(XGuiServer* self);

/** @brief 是否正在监听。 */
bool XGuiServer_isListening(const XGuiServer* self);

/** @brief 实际监听端口(listen 传 0 由系统分配时尤为有用); 未监听返回 0。 */
uint16_t XGuiServer_serverPort(const XGuiServer* self);

/** @brief 设置最大并发会话数(默认 4; 超限接入被 BYE(SESSION_LIMIT) 拒绝)。 */
void XGuiServer_setMaxSessions(XGuiServer* self, int maxSessions);

/** @brief 当前活跃会话数。 */
int XGuiServer_sessionCount(const XGuiServer* self);

/**
 * @brief      直挂自定义传输(需求 2: 任意 XIODevice; 嵌入式管道/测试)。
 * @details    device 必须已连通且为读写打开(借用, 生命周期由调用方管理,
 *             detachTransport 或 close 前不得销毁)。每调用建立一条会话。
 * @return     新会话 id(>0); 失败返回 -1(会话已满/设备非法)。
 */
int XGuiServer_attachTransport(XGuiServer* self, XIODevice* device);

/**
 * @brief      摘除并结束直挂传输会话(发 BYE; 设备本身不销毁)。
 * @param      sessionId attachTransport 返回的会话 id。
 */
void XGuiServer_detachTransport(XGuiServer* self, int sessionId);

/* ==================== 认证(XGuiRemote.md §3.8) ==================== */

/** @brief 设置认证方法(默认 NONE)。切为 SHA256_CHALLENGE 前必须 setPassword。 */
void XGuiServer_setAuthMethod(XGuiServer* self, XGuiRemoteAuthMethod method);

/** @brief 查询认证方法。 */
XGuiRemoteAuthMethod XGuiServer_authMethod(const XGuiServer* self);

/**
 * @brief      设置口令(内部即刻计算 SHA-256 存储, 原文不留存)。
 * @param      passwordUtf8 口令原文; 不得为 NULL, 空串等价 clearPassword。
 * @return     true 已设置; false 参数非法/哈希失败。
 */
bool XGuiServer_setPassword(XGuiServer* self, const char* passwordUtf8);

/** @brief 清除口令(此后 SHA256_CHALLENGE 方法将拒绝所有认证)。 */
void XGuiServer_clearPassword(XGuiServer* self);

/** @brief 是否已设口令。 */
bool XGuiServer_hasPassword(const XGuiServer* self);

/* ==================== 访问口令(加法式运行期扩展, 实现主体
 * ==================== XGuiRemoteAuth.c, 用户裁定语义 2026-10-04) ======== */
/**
 * @note    本节为认证的**加法式运行期扩展**: 不改动上方既有声明与语义
 *          (setAuthMethod/setPassword 双旋钮口径不变, 含 §6.8
 *          "SHA256+无口令 listen 拒绝"契约)。新 API 以口令为单旋钮:
 *            - 未设口令 = 匿名可连(默认行为, 完全向后兼容);
 *            - 已设口令 = 此后接入的**新会话**必须通过 SHA-256 挑战应答
 *              认证(nonce 每连接随机, 防重放; 口令明文禁止过网/入日志);
 *            - 对已有会话无影响(设口令后存量会话不断; 清口令即回匿名);
 *            - 认证与 TLS 正交(开不开 TLS 口令语义不变; TLS 开时挑战
 *              仍走, 防应用层裸奔)。
 */

/**
 * @brief      设置访问口令并启用挑战应答(运行期即时生效于新会话)。
 * @details    内部即刻计算 SHA-256 存储, 原文不留存; 认证方法旋钮随之
 *             置 SHA256_CHALLENGE。已有会话不断线。空串等价
 *             clearAccessPassword。
 * @return     true 已设置; false 参数非法/哈希失败。
 */
bool XGuiServer_setAccessPassword(XGuiServer* self, const char* passwordUtf8);

/**
 * @brief      查询访问口令状态(掩码形态, 不明文回吐)。
 * @param      maskedOutUtf8 掩码输出缓冲; 可为 NULL(仅查询是否已设)。
 *             已设时写入定长掩码 "********"(不泄露真实长度), 未设写入
 *             空串。
 * @param      cap 缓冲容量(maskedOutUtf8 非 NULL 时须 >0)。
 * @return     是否已设口令。
 */
bool XGuiServer_accessPassword(const XGuiServer* self, char* maskedOutUtf8,
                               size_t cap);

/** @brief 清除访问口令并回匿名模式(运行期即时生效于新会话; 已有会话
 *         不断线)。 */
void XGuiServer_clearAccessPassword(XGuiServer* self);

/**
 * @brief  设置单连接认证失败断链上限(默认 1=错 1 次即断, 历史口径;
 *         设 N>1 时单连接内前 N-1 次错误回 AUTH_RESULT(0) 可重答,
 *         第 N 次错误 AUTH_RESULT(0)+BYE(AUTH_FAILED) 断链)。
 */
void XGuiServer_setAuthFailureLimit(XGuiServer* self, int maxFailures);

/** @brief 当前单连接认证失败断链上限。 */
int XGuiServer_authFailureLimit(const XGuiServer* self);

/**
 * @brief  会话认证形态(接入会话的握手结论)。
 * @return 1=经挑战应答认证准入; 0=匿名接入; -1=会话不存在。
 */
int XGuiServer_sessionAuthState(const XGuiServer* self, int sessionId);

/* ==================== TLS 策略(加法式运行期扩展) ==================== */
/**
 * @note    本节为运行期 TLS 策略的**加法式扩展**(原契约仅有环境变量
 *          XGUI_REMOTE_TLS=1 在 listen 时定版, 冻结头无 setter; 见
 *          XGuiRemote.md §4.3)。不改动任何既有声明与语义: 未调用
 *          setTlsEnabled 时行为与原口径逐字节一致(回退环境变量)。
 *          策略在 XGuiServer_listen 调用时定版, 改动后须重启监听
 *          (close→listen)方对新建会话生效。
 */

/**
 * @brief      显式开启/关闭服务端 TLS(accept 后升级), 覆盖环境变量。
 * @param      enabled true=TLS 档; false=明文。此后环境变量不再参与。
 */
void XGuiServer_setTlsEnabled(XGuiServer* self, bool enabled);

/**
 * @brief      当前 TLS 策略(监听中=已定版的实际策略; 未监听=待生效
 *             策略——显式设置优先, 未设置时回退环境变量)。
 */
bool XGuiServer_tlsEnabled(const XGuiServer* self);

/**
 * @brief      设置 TLS 本地证书与私钥文件路径(PEM; 私钥按 RSA 装载)。
 * @details    服务端握手必须持有本地证书与私钥(mbedTLS 要求; 实现经
 *             XSslSocket_setLocalCertificate_2/_setPrivateKey_2 在每个
 *             accept 套接字上装载, 对改动后的新建会话生效)。路径副本
 *             由对象持有; 传 NULL/空串清除。
 * @return     true 已应用; false 参数非法/装载失败(证书不可读或私钥
 *             解析失败——以临时 XSslSocket 试装载校验, 失败不改动
 *             既有路径)。
 */
bool XGuiServer_setTlsCertificateFiles(XGuiServer* self,
                                       const char* certPemPathUtf8,
                                       const char* keyPemPathUtf8);

/** @brief 当前证书文件路径(借用; 未设返回 NULL)。 */
const char* XGuiServer_tlsCertFile(const XGuiServer* self);

/** @brief 当前私钥文件路径(借用; 未设返回 NULL)。 */
const char* XGuiServer_tlsKeyFile(const XGuiServer* self);

/* ==================== 档位(需求 6, XGuiRemote.md §5) ==================== */

/** @brief 设置档位预设(对既有会话在帧边界生效并广播 FB_META)。 */
void XGuiServer_setProfileId(XGuiServer* self, XGuiRemoteProfileId id);

/**
 * @brief      设置自定义档位参数(广播 FB_META 携带新 tile/格式参数)。
 * @details    profile 会被 sanitize 夹取后生效; 后续以 Custom 档运行。
 */
void XGuiServer_setProfile(XGuiServer* self, const XGuiRemoteProfile* profile);

/**
 * @brief      是否允许客户端经 PROFILE_SET 切档(默认 false; 拒绝时回
 *             PROFILE_RESULT(accepted=0), 会话维持服务端档位)。
 */
void XGuiServer_setAllowClientProfile(XGuiServer* self, bool allow);

/** @brief 当前档位 id(自定义档返回 CUSTOM)。 */
XGuiRemoteProfileId XGuiServer_profileId(const XGuiServer* self);

/* ==================== UDP 低延迟旁路通道(2026-10-04 加法式扩展) ==================== */
/**
 * @note    本节为 UDP 旁路通道的运行期策略扩展(设计稿
 *          out/mcgs-campaign/udp-design.md): TCP 会话锚不变, UDP 仅承载
 *          FB_UPDATE 帧 tile(S→C, 最新帧优先丢旧不追)与 INPUT_*(C→S,
 *          序号+NACK 可靠有序)。默认开启: 双端能力位协商门控, 老客户端
 *          (不宣告 CAP_UDP)行为逐字节不变。绑定端口=TCP 端口+1 起顺延
 *          试绑, 全占用静默纯 TCP。
 */

/**
 * @brief      设置 UDP 通道开关(默认开)。监听中改动即时生效:
 *             开=按当前 TCP 端口顺延试绑(既有 UDP 请求会话重新获 OFFER);
 *             关=停用通道, 激活会话静默回落 TCP(会话不断)。
 */
void XGuiServer_setUdpEnabled(XGuiServer* self, bool enabled);

/** @brief UDP 通道是否启用(用户策略, 未必已绑定)。 */
bool XGuiServer_udpEnabled(const XGuiServer* self);

/** @brief UDP 通道实际绑定端口(未绑定返回 0)。 */
uint16_t XGuiServer_udpPort(const XGuiServer* self);

/* ==================== 会话控制 ==================== */

/** @brief 踢出指定会话(发 BYE(NORMAL) 后断开; id 非法为 no-op)。 */
void XGuiServer_kickSession(XGuiServer* self, int sessionId);

/* ==================== 信号(GUI 线程发射) ==================== */

/** @brief 信号: 客户端会话建立(握手+认证完成后)。 */
void* XGuiServer_clientConnected_signal(XGuiServer* self, int sessionId);
/** @brief 信号: 客户端会话结束(reason 为 XGuiRemoteByeReason)。 */
void* XGuiServer_clientDisconnected_signal(XGuiServer* self, int sessionId,
                                           int reason);
/** @brief 信号: 会话级错误(协议错/队列溢出等; code 为 XGuiRemoteError)。 */
void* XGuiServer_sessionError_signal(XGuiServer* self, int sessionId,
                                     int errorCode);
/** @brief 信号: 画面元信息变化并已向全部会话广播(尺寸/档位/标题)。 */
void* XGuiServer_fbMetaChanged_signal(XGuiServer* self);

#endif /* XGUI_REMOTE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUISERVER_H */
