/**
 * @file       XGuiClient.h
 * @brief      XGuiClient 远程视图控件契约(冻结头, 只声明 API 不含实现)。
 * @details    XWidget 派生控件: 连接 XGuiServer, 把远端窗口画面经
 *             FB_UPDATE 增量解码进本地 backbuffer(XImage)合成显示
 *             (paintEvent 经 XPainter_drawImage 绘出, 损伤区走
 *             XWidget_updateRegion 增量上屏); 把本地用户对本控件的
 *             鼠标/滚轮/键盘/IME 操作按需求 9 口径直接捕获转发远端:
 *               - 无本地外挂虚拟指针/软键盘控件; 远端自绘光标与 IME;
 *               - 键盘仅在本控件持有焦点时转发(StrongFocus, 首次点击
 *                 由框架标准逻辑自然夺焦);
 *               - 按下期间 grabMouse 抓取, 拖拽越界仍持续转发至释放;
 *               - 坐标为控件局部坐标(1:1 模式下即远端窗口局部坐标)。
 *             断线自动重连(指数退避), 重连成功自动全量刷新。
 *             线程约定: 控件与全部公共 API 仅限 GUI 线程使用; 会话读泵
 *             经事件循环 poll 回调驱动, 无内部线程。
 * @note       模块开关 XGUI_REMOTE_ON 见 XGuiRemoteProto.h; TLS 档 API
 *             额外受 XGUI_REMOTE_TLS_ON 门控。
 * @author     XinYueC 团队
 */
#ifndef XGUICLIENT_H
#define XGUICLIENT_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XWidget.h"
#include "XGuiRemoteProto.h"

#if XGUI_REMOTE_ON

/** @brief XIODevice 前置声明(自定义传输)。 */
typedef struct XIODevice XIODevice;
/** @brief 私有实现块(实现文件内定义; 冻结契约不暴露)。 */
typedef struct XGuiClientPrivate XGuiClientPrivate;

/** @brief XGuiClient 虚函数枚举(继承 XWidget, 无新增公共槽)。 */
XCLASS_DEFINE_BEGING(XGuiClient)
XCLASS_DEFINE_EXTEND_END(XGuiClient, XWidget)

/**
 * @brief      XGuiClient 控件对象; m_base 必须是第一个成员。
 */
typedef struct XGuiClient {
    XWidget            m_base;   /**< 基类成员; 必须是第一个。 */
    XGuiClientPrivate* m_d;      /**< 私有实现块(拥有)。 */
} XGuiClient;

/**
 * @brief 会话级统计(XGuiClient_statistics 输出)。
 */
typedef struct XGuiRemoteStats {
    uint64_t bytesSent;       /**< 累计发送字节(含协议头)。 */
    uint64_t bytesReceived;   /**< 累计接收字节(含协议头)。 */
    uint32_t updateCount;     /**< 收到 FB_UPDATE 帧数。 */
    uint32_t tileCount;       /**< 收到 tile 总数。 */
    uint32_t fpsMilli;        /**< 实际刷新率×1000(滑动窗口估计)。 */
    uint32_t rttMs;           /**< 最近一次 PING 往返毫秒(未测得为 0)。 */
} XGuiRemoteStats;

/* ---- 演示页统计扩展(XGuiWindowDemo 远程客户端页; 2026-10-02 加法式
 *      补充, 不动上方既有字段语义) ---- */
/** @brief 扩展会话统计(XGuiClient_statisticsExtended 输出; 基础段字段
 *         与 XGuiRemoteStats 语义一致)。 */
typedef struct XGuiRemoteStatsExtended {
    uint64_t bytesSent;       /**< 同 XGuiRemoteStats.bytesSent。 */
    uint64_t bytesReceived;   /**< 同 XGuiRemoteStats.bytesReceived。 */
    uint32_t updateCount;     /**< 同 XGuiRemoteStats.updateCount。 */
    uint32_t tileCount;       /**< 同 XGuiRemoteStats.tileCount。 */
    uint32_t fpsMilli;        /**< 同 XGuiRemoteStats.fpsMilli。 */
    uint32_t rttMs;           /**< 同 XGuiRemoteStats.rttMs。 */
    uint32_t reconnectCount;  /**< 自动重连已尝试次数(会话累计, 连接达成
                               *   不清零——供 UI 显示「重连 N 次」)。 */
} XGuiRemoteStatsExtended;

/* ==================== 生命周期 ==================== */

/** @brief 初始化类虚函数表, 返回共享 XVtable 指针。 */
XVtable* XGuiClient_class_init(void);

/**
 * @brief  初始化控件(对标构造函数)。
 * @details 默认 StrongFocus、启用本地 IME 提示; 内部会话未连接。
 */
void XGuiClient_init(XGuiClient* self, XWidget* parent, XWidgetFlags flags);

/** @brief 默认内存类型创建便捷宏(仓库惯例)。 */
#define XGuiClient_create(parent, flags) \
    XGuiClient_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
/**
 * @brief  按指定内存类型创建控件实例。
 * @return 新控件; 分配失败返回 NULL(XClass_delete_base /
 *         XObject_deleteLater 释放)。
 */
XGuiClient* XGuiClient_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags);

/** @brief 析构/反初始化映射(仓库惯例)。 */
/** @brief 延迟释放别名(事件循环内安全自删)。 */
#define XGuiClient_deleteLater       XObject_deleteLater

/* ==================== 连接(需求 2: 传输无关) ==================== */

/**
 * @brief      经 TCP 连接远端 XGuiServer 并启动会话。
 * @details    异步: 返回后状态经 state()/connected 信号跟进。重复调用
 *             先静默断开旧连接。hostUtf8 为点分 IPv4 或主机名。
 */
void XGuiClient_connectToHost(XGuiClient* self, const char* hostUtf8,
                              uint16_t port);

#if XGUI_REMOTE_TLS_ON
/**
 * @brief      经 TLS 连接远端(XSslSocket; 协议不感知加密)。
 * @param      peerNameUtf8 证书校验对端名; NULL 跳过名校验(不推荐)。
 */
void XGuiClient_connectToHostEncrypted(XGuiClient* self, const char* hostUtf8,
                                       uint16_t port,
                                       const char* peerNameUtf8);
#endif /* XGUI_REMOTE_TLS_ON */

/**
 * @brief      直挂自定义传输(任意已连通 XIODevice*; 串口桥/管道/回环设备)。
 * @details    device 借用, 生命周期归调用方(本控件不销毁它);
 *             disconnectFromServer 或控件析构后调用方方可复用/销毁。
 *             传入后状态直接进入握手阶段。
 */
void XGuiClient_setTransport(XGuiClient* self, XIODevice* device);

/** @brief 断开当前会话(发 BYE(NORMAL); 不触发自动重连)。 */
void XGuiClient_disconnectFromServer(XGuiClient* self);

/**
 * @brief      断线自动重连(默认关)。
 * @param      enabled    是否启用。
 * @param      intervalMs 基础重连间隔(0 取 1000; 指数退避上限 30s)。
 */
void XGuiClient_setAutoReconnect(XGuiClient* self, bool enabled,
                                 uint32_t intervalMs);

/** @brief 当前会话状态(XGuiRemoteSessionState)。 */
XGuiRemoteSessionState XGuiClient_state(const XGuiClient* self);

/* ==================== 认证与档位 ==================== */

/** @brief 设置口令(明文仅驻留至认证完成; SHA256_CHALLENGE 应答用)。 */
void XGuiClient_setPassword(XGuiClient* self, const char* passwordUtf8);

/* ---- 访问口令(2026-10-04 加法式扩展, 实现主体 XGuiRemoteAuth.c) ---- */
/**
 * @brief      设置访问口令(与 setPassword 同语义的对称命名; 明文仅驻留
 *             至认证完成即焚)。
 * @details    连接流程自动适配: 服务端 HELLO_ACK 选定挑战应答才走认证,
 *             选定 NONE 则直连(本端口令仅为应答备料, 不改变协商)。
 *             服务端要求认证而本端口令为空时, 客户端快速失败断链
 *             (BYE AUTH_FAILED / ERR AUTH), 不发送必错应答。
 */
void XGuiClient_setAccessPassword(XGuiClient* self, const char* passwordUtf8);

/** @brief 清除访问口令(零化缓冲; 之后连接按匿名适配)。 */
void XGuiClient_clearAccessPassword(XGuiClient* self);

/**
 * @brief      请求切换档位(需求 6: 运行时切换; 需服务端允许)。
 * @details    异步: 结果以远端 FB_META(参数生效)或会话维持原档为准。
 */
void XGuiClient_requestProfileId(XGuiClient* self, XGuiRemoteProfileId id);

/**
 * @brief      输入转发开关(默认全开)。
 * @param      pointer  鼠标移动/按下/释放/双击/滚轮。
 * @param      keyboard 键盘按下/释放(仍受焦点门控)。
 * @param      ime      IME 文本。
 */
void XGuiClient_setInputForwardingEnabled(XGuiClient* self, bool pointer,
                                          bool keyboard, bool ime);

/* ==================== 查询 ==================== */

/** @brief 远端画面尺寸(FB_META 驱动; 未连接时输出 0×0)。 */
void XGuiClient_remoteSize(const XGuiClient* self, XSize* out);

/**
 * @brief      远端窗口标题。
 * @param      buffer 输出缓冲(调用方提供); 可为 NULL(此时仅查询长度)。
 * @param      cap    缓冲容量。
 * @return     标题 UTF-8 字节数(不含终止符, 已写入 buffer 的字节数≤cap-1
 *             且保证终止); 未连接返回 0。
 */
int XGuiClient_remoteTitle(const XGuiClient* self, char* buffer, int cap);

/** @brief 会话是否处于 Streaming(画面可用)。 */
bool XGuiClient_isRemoteAlive(const XGuiClient* self);

/** @brief 读取会话统计(累加计数, 断链不清零; self 可为 NULL)。 */
void XGuiClient_statistics(const XGuiClient* self, XGuiRemoteStats* out);

/** @brief 读取扩展会话统计(含自动重连次数; 2026-10-02 加法式补充:
 *         XGuiWindowDemo 远程客户端页实时统计区需要「重连 N 次」;
 *         self 可为 NULL)。 */
void XGuiClient_statisticsExtended(const XGuiClient* self,
                                   XGuiRemoteStatsExtended* out);

/**
 * @brief      触摸转发开关(2026-10-02 加法式补充)。
 * @details    默认关——既有鼠标路径(vmouse系+touch→mouse 仿真)已覆盖
 *             单点触摸语义, 置开后本控件的 XEVENT_TYPE_TOUCH_* 事件经
 *             INPUT_TOUCH 原样多点转发远端(桌面演示页「模拟触摸」按钮
 *             经 XGuiClient_sendTouchFrame 直发, 不依赖本开关)。
 */
void XGuiClient_setForwardTouch(XGuiClient* self, bool enabled);

/**
 * @brief      直发一帧 INPUT_TOUCH 多点触摸(2026-10-02 加法式补充;
 *             演示页「模拟触摸」按钮的程序化触摸注入通道)。
 * @param      msg 触摸帧(action/pointCount/points 由调用方组装;
 *             pointCount==0 或无会话时为无操作)。
 */
void XGuiClient_sendTouchFrame(XGuiClient* self,
                               const XGuiRemoteMsgInputTouch* msg);

/* ==================== 视图适配缩放(2026-10-04 加法式补充) ==================== */

/** @brief 视图适配模式(对标向日葵/浏览器远程协助「适配窗口」)。 */
typedef enum XGuiClientViewFitMode {
    XGUI_CLIENT_VIEW_FIT = 0, /**< 适配缩放(默认): contain 信箱式——
                               *   scale=min(视图宽/远端宽, 视图高/远端高),
                               *   居中留黑边; 控件保持宿主给定几何不再
                               *   setFixedSize(远端尺寸)。 */
    XGUI_CLIENT_VIEW_1TO1 = 1 /**< 1:1 原尺寸(V1 历史行为): FB_META 后
                               *   setFixedSize(远端尺寸), 像素级直绘;
                               *   视图小于远端时溢出裁剪(无滚动)。 */
} XGuiClientViewFitMode;

/**
 * @brief      设置视图适配模式(默认 FIT)。
 * @details    切换即时生效: FIT→1:1 恢复 setFixedSize(远端尺寸)+直绘;
 *             1:1→FIT 按当前控件几何重算缩放(宿主如需恢复视图区尺寸,
 *             自行 resize 后本控件经 resize 事件重算)。两种模式切换均
 *             整控件置脏重绘, 不残留旧帧。无会话时亦可调用(连接后按
 *             当前模式生效)。
 */
void XGuiClient_setViewFitMode(XGuiClient* self, XGuiClientViewFitMode mode);

/** @brief 读取当前视图适配模式。 */
XGuiClientViewFitMode XGuiClient_viewFitMode(const XGuiClient* self);

/**
 * @brief      读回当前视图变换(测试/宿主布局用)。
 * @param      scale   输出缩放系数(FIT=contain 比例, 1:1=1.0)。
 * @param      offsetX 输出信箱左偏移(控件局部; 1:1=0)。
 * @param      offsetY 输出信箱上偏移(控件局部; 1:1=0)。
 * @return     true=有画面几何可读(已收 FB_META 且控件尺寸有效);
 *             false=未就绪(输出全 0)。
 * @note       输入逆映射契约: 控件局部点 (vx,vy) 对应远端像素
 *             ((vx-offsetX)/scale, (vy-offsetY)/scale)——客户端全部
 *             指针/滚轮/触摸转发已按此式换算(§7.4「看到的点=点到的点」)。
 */
bool XGuiClient_viewTransform(const XGuiClient* self, float* scale,
                              int* offsetX, int* offsetY);
/* ==================== UDP 低延迟旁路通道(2026-10-04 加法式扩展) ==================== */
/**
 * @note    本节为 UDP 旁路通道的运行期策略扩展(设计稿
 *          out/mcgs-campaign/udp-design.md): TCP 会话锚不变; UDP 承载
 *          FB_UPDATE 帧(S→C, 最新帧优先)与 INPUT_*(C→S, 序号+NACK 可靠
 *          有序)。默认开: 服务端未宣告 CAP_UDP(老服务器)时自动纯 TCP,
 *          行为逐字节不变。
 */

/**
 * @brief      设置 UDP 通道开关(默认开; 运行期可切)。
 * @details    开: 会话中对端支持时发 UDP_MODE(1) 请求建链/重建;
 *             关: 发 UDP_MODE(0) 静默退回 TCP(会话不断)。
 */
void XGuiClient_setUdpEnabled(XGuiClient* self, bool enabled);

/** @brief UDP 数据面是否激活(帧+输入当前走 UDP)。 */
bool XGuiClient_udpActive(const XGuiClient* self);

/** @brief UDP 通道状态(XGuiRemoteUdpState, 见 XGuiRemoteUdpChannel.h;
 *         显示/断言口径)。 */
int XGuiClient_udpState(const XGuiClient* self);

/* ==================== 信号(GUI 线程发射) ==================== */

/** @brief 信号: 握手+认证完成, 进入 Streaming(首帧 FB_META 已收到)。 */
void* XGuiClient_connected_signal(XGuiClient* self);
/** @brief 信号: 会话断开(reason 为 XGuiRemoteByeReason; 自动重连前发射)。 */
void* XGuiClient_disconnected_signal(XGuiClient* self, int reason);
/** @brief 信号: 远端元信息变化(FB_META: 尺寸/档位/标题)。 */
void* XGuiClient_remoteMetaChanged_signal(XGuiClient* self);
/** @brief 信号: 会话错误(code 为 XGuiRemoteError; 不一定断链)。 */
void* XGuiClient_errorOccurred_signal(XGuiClient* self, int errorCode);

#endif /* XGUI_REMOTE_ON */

#ifdef __cplusplus
}
#endif

#endif /* XGUICLIENT_H */
