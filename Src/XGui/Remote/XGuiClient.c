/**
 * @file       XGuiClient.c
 * @brief      XGuiClient 远程视图控件实现(画面合成 + 输入转发 + 断线重连)。
 * @details    契约见冻结头 XGuiClient.h, 设计依据仓库根 XGuiRemote.md §7:
 *               - 画面: FB_UPDATE 逐 tile 经 XGuiRemoteCodec_decodeTile 直写
 *                 本地 backbuffer(XImage, 尺寸=远端 FB_META), tile 矩形并入
 *                 损伤 XRegion 后 XWidget_updateRegion 增量上屏; paintEvent
 *                 虚槽 XPainter_drawImage 整幅绘出; 未连接画占位文本, 不画
 *                 陈旧帧;
 *               - I/O: 全部在 GUI 线程, 事件循环 poll 回调驱动帧泵; 发送侧
 *                 按 XGuiRemoteProto_writeFrame 返回值处理部分写(帧尾待写
 *                 outbox, 每圈按档位 txBudgetBytes 限预算续传);
 *               - 输入转发(需求 9 口径, XGuiRemote.md §7.4 逐条落实): 鼠标
 *                 移动/按下/释放/双击/滚轮 + 键盘按下/释放 + IME 文本经
 *                 INPUT_* 消息直发远端; 键盘槽 hasFocus 门控; 按下抓取
 *                 (grabMouse)拖拽越界按 i16(±32767 饱和)编码不裁剪, 远端
 *                 负责裁剪; PRESS/RELEASE 成对纪律(断链清集合并补发
 *                 RELEASE); resource 档鼠标移动合并窗口, 按下/释放/双击/
 *                 滚轮永不合并; 无本地虚拟指针/软键盘控件, 远端自绘光标
 *                 与 IME;
 *               - 重连: XTimer 单次触发指数退避(基础 intervalMs, 上限 30s),
 *                 重连成功(FB_META)自动 FB_REQUEST 全量;
 *               - 保活: 按档位间隔发 PING, 超时未收对端帧判死链转重连;
 *                 PONG 回显测 rtt;
 *               - 统计: 累加计数, 断链不清零。
 *             线程约定: 控件与全部公共 API 仅限 GUI 线程; 设备读写与帧泵
 *             全在 GUI 线程(poll 回调), 无内部线程。
 * @author     XinYueC 团队
 */
#include "XGuiClient.h"
#include <stdio.h>   /* fprintf: 基准追踪 stderr(XGUI_REMOTE_BENCH_TRACE)。 */
#if XGUI_REMOTE_ON

#include "XGuiRemoteAuth.h"
#include "XGuiRemoteCodec.h"
#include "XGuiRemoteUdpChannel.h" /* UDP 低延迟旁路通道(2026-10-04 加法式;
                                  * 设计稿 out/mcgs-campaign/udp-design.md)。 */
#include "XWidget_Protected.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XPainter.h"
#include "XImage.h"
#include "XString.h"
#include "XVarList.h"
#include "XMemory.h"
#include "XAbstractEventDispatcher.h"
#include "XAbstractNetIoRing.h" /* [wake] 探针: 载体读完成时戳查询。 */
#include "XTcpSocket.h"
#include "XTimer.h"
#include "XDateTime.h"
/* XCryptographicHash 直用已收编 XGuiRemoteAuth.c(2026-10-04 认证收口)。 */
#include "XSystem.h" /* XSystem_environment: TLS 自签部署环境口(唯一环境入口)。 */
#if defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
#include "XSslSocket.h"
#endif
#include <string.h>

/* ==================== 私有常量 ==================== */

/** @brief 发送 outbox 字节帽: 客户端帧小(≤2KiB 级), 慢链路下保护内存
 *         有界; 溢出丢弃新帧并记 BUFFER_OVERFLOW 错误(不丢已排队帧,
 *         保 PRESS/RELEASE 顺序)。 */
#define XGC_OUTBOX_CAP (256u * 1024u)
/** @brief 每圈 poll 读泵字节预算(防单圈长任务饿死 GUI)。 */
#define XGC_READ_BUDGET_PER_POLL (256u * 1024u)
/** @brief 每圈 poll 读块栈缓冲字节数。 */
#define XGC_READ_CHUNK (16u * 1024u)
/** @brief 指数退避上限(毫秒, XGuiRemote.md §7.3)。 */
#define XGC_RECONNECT_MAX_MS 30000u
/** @brief 重连基础间隔缺省值(XGuiClient_setAutoReconnect intervalMs=0)。 */
#define XGC_RECONNECT_DEFAULT_MS 1000u
/** @brief fpsMilli 滑动窗口宽度(毫秒)。 */
#define XGC_FPS_WINDOW_MS 1000u
/** @brief 镜像损伤合并上屏: 距最近一帧到达的静默阈值(毫秒)。
 *  突发期(切页 5-9 帧连到)每帧即刷会把 GUI 圈钳到近平幅 paint 的
 *  代价上(mcgs round3 实测: cli-udp 突发窗 p95 恒 24.6-26.0ms、
 *  cli-kern 内核收包→派发 p95_MAX=117ms=帧在套接字侧排队), 静默
 *  合并后每突发只上屏 1-3 次, 圈速释放、收侧队列不再淤积。 */
#define XGC_PRESENT_QUIET_MS 6

/* [perf9 路2 任务4 2026-10-05] 未交付 tile 占位视觉:
 * - XGUI_REMOTE_TILE_PLACEHOLDER(env, 缺省开): backbuffer 重建底色=
 *   深灰(XGC_PLACEHOLDER_RGB)而非零值黑——未交付/换页未到区呈「待
 *   接收」深灰, 已交付区正常; 关闭回退历史纯黑语义。
 * - XGUI_REMOTE_CLIENT_HINT(env, 缺省关): META 后首 tile 到达前在
 *   画面中央绘「正在接收画面…」提示文本。 */
#define XGC_PLACEHOLDER_ENV "XGUI_REMOTE_TILE_PLACEHOLDER"
#define XGC_HINT_ENV "XGUI_REMOTE_CLIENT_HINT"
#define XGC_PLACEHOLDER_RGB 0x26u, 0x26u, 0x26u /* 深灰(≠内容黑/桌面灰). */

/** @brief 占位视觉开关(缓存 env; true=深灰占位, false=历史纯黑)。 */
static bool xgc_placeholderOn(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* v = XSystem_environment(XGC_PLACEHOLDER_ENV);
        cached = !(v != NULL && v[0] == '0' && v[1] == '\0');
    }
    return cached != 0;
}

/** @brief 接收提示开关(缓存 env; true=首 tile 前绘提示文本)。 */
static bool xgc_hintOn(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* v = XSystem_environment(XGC_HINT_ENV);
        cached = v != NULL && !(v[0] == '0' && v[1] == '\0');
    }
    return cached != 0;
}
/** @brief 镜像损伤合并上屏: 最老未上屏损伤的最大时延上限(毫秒)。
 *  连续流(静默永不到来)下保证至多 ~20ms 一刷, 帧节奏不退化。 */
#define XGC_PRESENT_MAX_AGE_MS 20

/* [perf9 路5 任务2 2026-10-05] 首伤即现 + 自适应合并窗:
 * - 首伤即现: FB_UPDATE 批解码完成且当前无待上屏损伤(=突发/稳态首块)
 *   时立即就地 present, 不等泵圈窗——首现延迟从「静默 6ms/上限 20ms」
 *   砍到 ~0; 后续帧仍由泵圈按窗合并(突发期 5-9 帧只多上屏 1-3 次
 *   的原语义不变)。env XGUI_REMOTE_PRESENT_IMMEDIATE=0 整体回退旧行为。
 * - 自适应窗宽: 本端最近转发输入距今 < ACTIVE_MS 判交互期——窗取基值
 *   一半(下限 1ms), 新内容更快上屏; 静止期用原基值(突发合并收益不丢)。
 *   env XGUI_REMOTE_PRESENT_QUIET_MS/XGUI_REMOTE_PRESENT_MAX_MS 覆盖
 *   静止期基值; XGUI_REMOTE_PRESENT_ACTIVE_MS 调交互窗宽(0=关自适应)。 */
#define XGC_PRESENT_IMMEDIATE_ENV "XGUI_REMOTE_PRESENT_IMMEDIATE"
#define XGC_PRESENT_QUIET_ENV "XGUI_REMOTE_PRESENT_QUIET_MS"
#define XGC_PRESENT_MAX_ENV "XGUI_REMOTE_PRESENT_MAX_MS"
#define XGC_PRESENT_ACTIVE_ENV "XGUI_REMOTE_PRESENT_ACTIVE_MS"
/** @brief 交互期判定窗宽(毫秒): 最近一次转发输入距今小于该值。 */
#define XGC_PRESENT_ACTIVE_MS 500

/** @brief 首伤即现开关(缓存 env; true=首块损伤立即 present)。 */
static bool xgc_presentImmediateOn(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* v = XSystem_environment(XGC_PRESENT_IMMEDIATE_ENV);
        cached = !(v != NULL && v[0] == '0' && v[1] == '\0');
    }
    return cached != 0;
}

/** @brief 静止期合并窗静默阈值(毫秒; env 覆盖, 缺省=XGC_PRESENT_QUIET_MS)。 */
static int xgc_presentQuietBaseMs(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* v = XSystem_environment(XGC_PRESENT_QUIET_ENV);
        int n = 0;
        cached = XGC_PRESENT_QUIET_MS;
        if (v != NULL) {
            const char* p = v;
            while (*p >= '0' && *p <= '9') {
                n = n * 10 + (*p - '0');
                if (n > 1000) { n = 1000; break; }
                ++p;
            }
            if (p != v) cached = n;
        }
    }
    return cached;
}

/** @brief 静止期合并窗最老损伤上限(毫秒; env 覆盖, 缺省=MAX_AGE_MS)。 */
static int xgc_presentMaxBaseMs(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* v = XSystem_environment(XGC_PRESENT_MAX_ENV);
        int n = 0;
        cached = XGC_PRESENT_MAX_AGE_MS;
        if (v != NULL) {
            const char* p = v;
            while (*p >= '0' && *p <= '9') {
                n = n * 10 + (*p - '0');
                if (n > 1000) { n = 1000; break; }
                ++p;
            }
            if (p != v) cached = n;
        }
    }
    return cached;
}

/** @brief 交互期判定窗宽(毫秒; env 覆盖, 0=关自适应)。 */
static int xgc_presentActiveWinMs(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* v = XSystem_environment(XGC_PRESENT_ACTIVE_ENV);
        int n = 0;
        cached = XGC_PRESENT_ACTIVE_MS;
        if (v != NULL) {
            const char* p = v;
            while (*p >= '0' && *p <= '9') {
                n = n * 10 + (*p - '0');
                if (n > 10000) { n = 10000; break; }
                ++p;
            }
            if (p != v) cached = n;
        }
    }
    return cached;
}
/** @brief 主机名/对端名缓冲上限。 */
#define XGC_HOST_CAP 128
/** @brief 视口通告检测节流(毫秒): 布局漂移期合并通告(方案④, 2026-10-05)。 */
#define XGC_VIEW_REPORT_MS 250

/* ==================== 私有实现块 ==================== */

/** @brief 客户端私有会话状态(冻结契约不暴露, 见 XGuiClient.h m_d)。 */
typedef struct XGuiClientPrivate {
    /* ---- 传输(需求 2: 任意 XIODevice) ---- */
    XIODevice* m_dev;            /**< 当前传输设备(借用或自有, 见下)。 */
    bool       m_devOwned;       /**< true=本控件创建的 socket(拆除时销毁)。 */
    bool       m_devIsSsl;       /**< true=设备为 XSslSocket(TLS 档)。 */
    bool       m_tlsWanted;      /**< 网络档: 请求 TLS 连接。 */
    bool       m_sessionReady;   /**< 设备可用已确认(明文 connected/TLS 加密
                                  *   完成), 横幅已可发。 */
    bool       m_bannerSent;     /**< 本端 8 字节横幅已足额写出。 */
    bool       m_bannerGot;      /**< 对端横幅已收全并校验。 */
    uint8_t    m_bannerBuf[XGUI_REMOTE_BANNER_BYTES]; /**< 对端横幅攒字节。 */
    int        m_bannerGotBytes; /**< 对端横幅已攒字节数。 */

    /* ---- 连接意图与重连(§7.3) ---- */
    char     m_host[XGC_HOST_CAP];        /**< 目标主机(UTF-8)。 */
    uint16_t m_port;                      /**< 目标端口。 */
    char     m_peerName[XGC_HOST_CAP];    /**< TLS 校验对端名(可空)。 */
    bool     m_wantConnect;               /**< 用户请求过连接(网络档);
                                           *   setTransport 档恒 false。 */
    bool     m_autoReconnect;             /**< 断线自动重连开关。 */
    uint32_t m_reconnectBaseMs;           /**< 基础重连间隔。 */
    uint32_t m_reconnectAttempts;         /**< 连续重连次数(指数退避指数)。 */
    XTimer*  m_reconnectTimer;            /**< 单次触发重连定时器(拥有)。 */

    /* ---- 会话状态机(XGuiRemoteSessionState) ---- */
    XGuiRemoteSessionState m_state;
    XGuiRemoteFrameReader  m_reader;         /**< 增量帧泵。 */
    uint32_t m_localCaps;                    /**< 本端能力位(HELLO 携带)。 */
    uint32_t m_peerCaps;                     /**< 协商后能力交集。 */
    uint16_t m_negotiatedVersion;            /**< 协商版本(取双方较小)。 */
    XGuiRemoteAuthMethod m_authMethod;       /**< 服务端选定认证法。 */
    char     m_password[256];  /**< 口令原文(仅驻留至认证完成, 冻结头注)。 */
    XGuiRemoteMsgAuthChallenge m_challenge;  /**< 当前认证挑战。 */
    bool     m_challengeValid;               /**< 挑战在途。 */

    /* ---- 远端画面(§7.2) ---- */
    XImage  m_fb;                /**< 本地 backbuffer(值成员; 空图=未就绪)。 */
    bool    m_fbValid;          /**< backbuffer 与远端 FB_META 一致。 */
    uint16_t m_fbWidth;         /**< 远端窗口宽(FB_META)。 */
    uint16_t m_fbHeight;        /**< 远端窗口高。 */
    uint8_t m_fbFormat;         /**< 线上像素格式(XGuiRemotePixelFormat)。 */
    uint16_t m_tileWidth;       /**< 远端 tile 宽。 */
    uint16_t m_tileHeight;      /**< 远端 tile 高。 */
    uint8_t m_profileId;        /**< 远端当前档位 id。 */
    char    m_title[XGUI_REMOTE_MAX_NAME_BYTES + 1]; /**< 远端标题(终止保证)。 */
    int     m_titleBytes;       /**< 标题字节数(≤MAX_NAME)。 */
    XRegion m_damage;           /**< 本批 FB_UPDATE 损伤累积(复用容量)。 */
    int64_t m_presentFirstMs;   /**< 合并上屏: 最老未上屏损伤到达时刻
                                     (MONOTONIC ms; 0=无待上屏损伤)。 */
    int64_t m_presentLastMs;    /**< 合并上屏: 最近一帧损伤到达时刻。 */
    uint64_t m_presentFirstUs;  /**< [stage] 同上时刻的单调 µs 戳(perf9
                                      路5; cli-merg=首现延迟口径, 立即
                                      路径也采样; 0=探针关/无待上屏)。 */
    uint64_t m_stagePaintFromUs; /**< [stage] 上屏触发戳(探针单调 µs;
                                      cli-paint=至 paintEvent 派发)。 */
    bool m_stagePaintPend;       /**< [stage] 待画镜像损伤(下个 paintEvent
                                      采样 cli-paint/cli-blit 后清)。 */

    /* ---- 保活与统计(§7.5) ---- */
    XGuiRemoteProfile m_profile;      /**< 本端档位参数副本(§5.2 各自持有)。 */
    uint64_t m_lastPeerFrameMs;       /**< 最近一次收到对端任何帧(单调 ms)。 */
    uint64_t m_lastPingSentMs;        /**< 最近一次发出 PING(单调 ms)。 */
    uint64_t m_pingSentTs;            /**< 在途 PING 携带的时间戳(回显匹配)。 */
    bool     m_pingAwait;             /**< 有在途 PING。 */
    uint64_t m_fpsWindowStartMs;      /**< fps 滑动窗口起点。 */
    uint32_t m_fpsWindowUpdates;      /**< 窗口内 FB_UPDATE 帧数。 */
    XGuiRemoteStats m_stats;          /**< 累加统计(断链不清零)。 */
    uint32_t m_reconnectCountTotal;   /**< 累计重连尝试(连接达成不清零;
                                       *   2026-10-02 加法式: 演示页实时
                                       *   统计「重连 N 次」口径)。 */

    /* ---- 输入转发(需求 9, §7.4) ---- */
    bool     m_fwdPointer;            /**< 鼠标/滚轮转发开关(默认开)。 */
    bool     m_fwdKeyboard;           /**< 键盘转发开关(默认开)。 */
    bool     m_fwdIme;                /**< IME 转发开关(默认开)。 */
    bool     m_fwdTouch;              /**< 触摸转发开关(默认关; 2026-10-02
                                       *   加法式: 演示页触摸直发通道配套
                                       *   的本地 TOUCH_* 事件转发门)。 */
    uint16_t m_pressedButtons;        /**< 已转发 PRESS 未配对 RELEASE 的键
                                       *   集合(XGUI_REMOTE_BTN_* 掩码)。 */
    uint32_t* m_pressedKeys;          /**< 已转发 PRESS 未配对 RELEASE 的键
                                       *   码集合(XKey 码位)。 */
    int      m_pressedKeyCount;
    int      m_pressedKeyCap;
    XGuiRemoteMsgInputPointer m_pendingMove; /**< 合并中的鼠标移动(resource 档)。 */
    bool     m_hasPendingMove;        /**< 有待发移动。 */
    uint64_t m_lastMoveSentMs;        /**< 最近一次移动实际发出(单调 ms)。 */
    uint64_t m_lastInputFwdMs;        /**< 最近一次转发任意输入(单调 ms;
                                       *   perf9 路5 合并窗自适应交互判据)。 */

    /* ---- 视图适配缩放(2026-10-04 加法式: contain 信箱式, 见
     *      XGuiClient_setViewFitMode) ---- */
    XGuiClientViewFitMode m_viewFit;  /**< 适配模式(默认 FIT)。 */
    float    m_viewScale;             /**< 当前缩放系数(FIT=contain, 1:1=1)。 */
    int      m_viewOffX;              /**< 信箱左偏移(控件局部)。 */
    int      m_viewOffY;              /**< 信箱上偏移(控件局部)。 */
    int      m_viewDstW;              /**< 信箱内容目标宽(round(fbW*scale)
                                       *   钳位视图; 缓存与上屏共用)。 */
    int      m_viewDstH;              /**< 信箱内容目标高。 */

    /* ---- FIT 增量缩放缓存(perf9 路5 任务3 2026-10-05) ---- */
    XImage   m_scaled;                /**< 已缩视图帧缓存(尺寸=信箱目标,
                                       *   格式=m_fb 本地格式; 拥有)。 */
    bool     m_scaledValid;           /**< 缓存内容与变换+fb 几何一致。 */
    float    m_scaledScale;           /**< 缓存生成时变换快照(变化检测)。 */
    int      m_scaledOffX;            /**< 缓存生成时信箱偏移快照。 */
    int      m_scaledOffY;
    int*     m_scaledMap;             /**< 列映射表 dx→sx(拥有; 重算于
                                       *   每次重缩放, 容量随 dstW 增长)。 */
    int      m_scaledMapCap;          /**< 列映射表容量(int 个数)。 */

    /* ---- 发送 outbox(部分写续传, §6.4 纪律的客户端侧) ---- */
    uint8_t* m_outbox;               /**< 待写帧字节队列(线性)。 */
    size_t   m_outboxCap;            /**< 队列存储容量。 */
    size_t   m_outboxLen;            /**< 队列有效字节总数。 */
    size_t   m_outboxOff;            /**< 队头已写出偏移。 */
    bool     m_overflowNotified;     /**< 本会话已发过 BUFFER_OVERFLOW 错误。 */

    /* ---- poll 回调句柄 ---- */
    XHandle m_pollHandle;            /**< 事件循环轮询回调句柄(未注册 NULL)。 */
    XTimer* m_pumpTimer;             /**< 会话泵兜底定时器(基准优化 2026-10-02:
                                          空闲事件循环对网络完成事件的唤醒存在
                                          实测 ~170ms 级散布(resource 档收侧);
                                          8ms 周期直呼 pollPump 把收发延迟钳到
                                          ≤8ms。与 poll 回调同口, 空转近零成本)。 */

    /* ---- UDP 低延迟旁路通道(2026-10-04 加法式; 全部 GUI 线程) ---- */
    XGuiRemoteUdpChannel* m_udp;     /**< 通道(拥有; 会话级复用)。 */
    bool       m_udpWanted;          /**< 用户开关(默认开)。 */
    uint32_t   m_udpToken;           /**< OFFER 携带的会话凭据。 */
    int        m_udpState;           /**< 状态镜像(XGuiRemoteUdpState)。 */
    int64_t    m_udpRetryMs;         /**< 下次自动重建请求时刻(5s 冷却)。 */
    int64_t    m_udpLastFrameMs;     /**< 最近 FRAME 收下(丢帧恢复判据)。 */
    int64_t    m_udpFullReqMs;       /**< 上次断流全量刷新请求时刻。 */

    /* ---- 首帧 FB_REQUEST 与视口通告(2026-10-05 方案③④; 全部 GUI 线程) ---- */
    bool     m_fbReqSent;          /**< 本连接已发过首个 FB_REQUEST(发过即止;
                                       *   重连=新连接自然重发; teardown 复位)。 */
    int16_t  m_viewRepX, m_viewRepY; /**< 已通告视口(变化检测)。 */
    int16_t  m_viewRepW, m_viewRepH;
    int64_t  m_viewRepMs;          /**< 上次视口检测时刻(XGC_VIEW_REPORT_MS 节流)。 */

    /* ---- 首帧完整度探针(XGUI_REMOTE_FIRSTFRAME_PROF=1 才启用; 探针纪律:
     *      env 门控交付前可关, stderr 单行, 连接级状态 teardown 释放) ---- */
    bool     m_ffOn;               /**< 探针开关(构造时 env 定版)。 */
    uint8_t* m_ffMap;              /**< tile 覆盖位图(拥有; teardown 释放)。 */
    uint32_t m_hintBaselineTiles;  /**< [perf9 任务4] META 时刻 tile 计数
                                     基线(接收提示绘至首新 tile 到达)。 */
    int      m_ffGridW, m_ffGridH; /**< tile 网格(FB_META 派生)。 */
    int      m_ffGot, m_ffTotal;   /**< 已收/全部 tile 数(去重覆盖)。 */
    int      m_ffVpGot, m_ffVpTotal; /**< 视口内已收/全部(方案④度量)。 */
    int      m_ffVpX, m_ffVpY, m_ffVpW, m_ffVpH; /**< 视口矩形(远端坐标)。 */
    int64_t  m_ffT0Ms;             /**< 本连接首个 FB_META 时刻。 */
    bool     m_ffFullPrinted;      /**< full 行已打印(每连接一次)。 */
    bool     m_ffVpPrinted;        /**< vpfull 行已打印(每连接一次)。 */
} XGuiClientPrivate;

/* ==================== 前置声明(实现互调) ==================== */

static bool xgc_pollPump(void* userData);
/** @brief 会话泵兜底定时回调(基准优化 2026-10-02): 直呼 pollPump。 */
static void xgc_pumpTimerCb(void* userData, XTimerData* timer)
{
    (void)timer;
    xgc_pollPump((XGuiClient*)userData);
}
static void xgc_teardown(XGuiClient* self, XGuiRemoteByeReason reason,
                         XGuiRemoteError errCode, bool sendBye, bool silent);
static bool xgc_sendFrame(XGuiClient* self, XGuiRemoteMsgType type,
                          const void* payload, size_t payloadBytes);
static void xgc_connectNetwork(XGuiClient* self);
static void xgc_onSocketConnected(XObject* receiver, XVarList* args);
static void xgc_onSocketDisconnected(XObject* receiver, XVarList* args);
static void xgc_onSocketError(XObject* receiver, XVarList* args);
/* UDP 旁路回调(2026-10-04; 定义见 dispatch 一节之后)。 */
static void xgc_udpRecv(void* user, uint8_t channel, uint8_t msgType,
                        uint32_t seq, const uint8_t* payload, size_t len);
static void xgc_udpStateChanged(void* user, int state);
static void xgc_udpReadyRead(XObject* receiver, XVarList* args);
/* 首帧 FB_REQUEST/视口/探针(2026-10-05 方案③④; 定义见视图适配一节后)。 */
static bool xgc_fbReqAllowed(const XGuiClientPrivate* d);
static void xgc_viewportRect(XGuiClient* self, int* ox, int* oy,
                             int* ow, int* oh);
static void xgc_sendFbRequest(XGuiClient* self, uint8_t mode,
                              int vx, int vy, int vw, int vh);
static void xgc_ffStart(XGuiClient* self, int vx, int vy, int vw, int vh);
static void xgc_ffMarkTile(XGuiClient* self, int tx, int ty, int tw, int th);
static void xgc_ffReset(XGuiClientPrivate* d, const char* why);

/* ==================== 内部工具 ==================== */

/** @brief 单调毫秒时钟(Src 统一时间源: XDateTime 已是 CLOCK_MONOTONIC,
 *         先例 XFileDialog.c xff_nowMs)。 */
static int64_t xgc_nowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/** @brief 当前合并窗静默阈值(毫秒): 交互期=基值减半(下限 1), 静止期=基值。 */
static int xgc_presentQuietMs(const XGuiClientPrivate* d, int64_t now)
{
    int base = xgc_presentQuietBaseMs();
    int win = xgc_presentActiveWinMs();
    if (win > 0 && d->m_lastInputFwdMs != 0 &&
        now - (int64_t)d->m_lastInputFwdMs < win) {
        int a = base / 2;
        return a < 1 ? 1 : a;
    }
    return base;
}

/** @brief 当前合并窗最老损伤上限(毫秒): 交互期=基值减半(下限=静默+1)。 */
static int xgc_presentMaxMs(const XGuiClientPrivate* d, int64_t now)
{
    int base = xgc_presentMaxBaseMs();
    int win = xgc_presentActiveWinMs();
    if (win > 0 && d->m_lastInputFwdMs != 0 &&
        now - (int64_t)d->m_lastInputFwdMs < win) {
        int a = base / 2;
        int quiet = xgc_presentQuietMs(d, now);
        if (a < quiet + 1) a = quiet + 1;
        return a;
    }
    return base;
}

/** @brief i16 饱和(±32767; §3.2 坐标口径: 越界不裁剪, 远端负责)。 */
static int16_t xgc_satI16(int v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

/** @brief XMouseButton → 线上鼠标键位(XEvent.h 位定义原值透传)。 */
static uint8_t xgc_buttonBit(XMouseButton button)
{
    return (uint8_t)(button & 0xFFu);
}

/* ==================== 视图适配缩放(2026-10-04 加法式) ==================== */

/** @brief 重算视图变换: FIT=contain 信箱式(scale=min(视图/远端), 居中
 *         留黑边), 1:1=恒等。fb 未就绪或控件尺寸无效时保持恒等(占位绘
 *         制不依赖变换)。调用点=FB_META 尺寸变化/resizeEvent/模式切换。
 *         [perf9 路5] 同时快照信箱目标尺寸(m_viewDstW/H, 钳位后)——缓存
 *         缩放帧与 paintEvent 上屏共用同一几何真源。 */
static void xgc_updateViewTransform(XGuiClient* self)
{
    XGuiClientPrivate* d = (XGuiClientPrivate*)self->m_d;
    int vw, vh, fw, fh, dstW, dstH;
    float sx, sy, s;
    d->m_viewScale = 1.0f;
    d->m_viewOffX = 0;
    d->m_viewOffY = 0;
    d->m_viewDstW = 0;
    d->m_viewDstH = 0;
    if (d->m_viewFit != XGUI_CLIENT_VIEW_FIT || !d->m_fbValid) return;
    vw = XWidget_width((XWidget*)self);
    vh = XWidget_height((XWidget*)self);
    fw = (int)d->m_fbWidth;
    fh = (int)d->m_fbHeight;
    if (vw <= 0 || vh <= 0 || fw <= 0 || fh <= 0) return;
    sx = (float)vw / (float)fw;
    sy = (float)vh / (float)fh;
    s = sx < sy ? sx : sy;
    if (!(s > 0.0f)) return;
    dstW = (int)((float)fw * s + 0.5f);
    dstH = (int)((float)fh * s + 0.5f);
    if (dstW > vw) dstW = vw; /* 浮点取整钳位: 目标不溢出视图。 */
    if (dstH > vh) dstH = vh;
    d->m_viewScale = s;
    d->m_viewDstW = dstW;
    d->m_viewDstH = dstH;
    d->m_viewOffX = (vw - dstW) / 2;
    d->m_viewOffY = (vh - dstH) / 2;
}

/** @brief 控件局部坐标 → 远端像素坐标(§7.4 输入逆映射红线: 「看到的
 *         点」=「点到的点」; FIT 逆变换 (v-off)/scale, 1:1 直通)。 */
static void xgc_viewToRemote(const XGuiClientPrivate* d, int vx, int vy,
                             int16_t* rx, int16_t* ry)
{
    if (d->m_viewFit == XGUI_CLIENT_VIEW_FIT && d->m_viewScale > 0.0f) {
        vx = (int)(((float)(vx - d->m_viewOffX)) / d->m_viewScale);
        vy = (int)(((float)(vy - d->m_viewOffY)) / d->m_viewScale);
    }
    *rx = xgc_satI16(vx);
    *ry = xgc_satI16(vy);
}

/* ==================== FIT 增量缩放缓存(perf9 路5 任务3 2026-10-05) ==================== */

/** @brief 线上格式 → 本地 XImage 格式(实现见格式工具一节; 自检前置)。 */
static XImageFormat xgc_localImageFormat(uint8_t wireFormat);

/** @brief 丢弃缩放缓存(重连重建 fb/teardown/断言失败等; 幂等)。 */
static void xgc_scaledCacheReset(XGuiClientPrivate* d)
{
    d->m_scaledValid = false;
    d->m_scaledScale = 0.0f;
    d->m_scaledOffX = 0;
    d->m_scaledOffY = 0;
    XClassDeinit(&d->m_scaled);
    if (d->m_scaledMap) {
        XMemory_free(d->m_scaledMap, XCLASS_DEFAULT_MEMORY_TYPE);
        d->m_scaledMap = NULL;
    }
    d->m_scaledMapCap = 0;
}

/**
 * @brief  重算列映射 dx→sx(与 painter 最近邻采样逐位同式)。
 * @details 采样式复刻 painterRaster_drawImageRect(XPainter.c:4947-4959):
 *          tx=(dx+0.5)/dstW, fx=tx*fbW, sx=floor(fx)。fx 恒 ≥0, (int)
 *          截断即 floor。painter 全量路径 targetX=信箱偏移在逆变换中对消
 *          (ux-targetX=dx+0.5), 故缓存坐标系(dx,dy)公式与之逐位一致。
 */
static bool xgc_scaledColMapBuild(XGuiClientPrivate* d, int dstW, int fw)
{
    int dx;
    if (d->m_scaledMapCap < dstW) {
        int* p = (int*)XRealloc_System(d->m_scaledMap,
                                       (size_t)dstW * sizeof(int));
        if (!p) return false;
        d->m_scaledMap = p;
        d->m_scaledMapCap = dstW;
    }
    for (dx = 0; dx < dstW; ++dx) {
        float tx = ((float)dx + 0.5f) / (float)dstW;
        d->m_scaledMap[dx] = (int)(tx * (float)fw); /* fx≥0: 截断=floor。 */
    }
    return true;
}

/**
 * @brief  对远端坐标矩形增量重缩放进缓存。
 * @details 目标域矩形按视图变换外扩 ±1px(最近邻映射护栏, 只多不少——
 *          多刷的像素用未变源重算=逐位同值, 缺刷才破坏等价)。缓存恒
 *          ARGB32(paint 1:1 直绘吃 blitImageRegion 行级快车道; RGB16
 *          源按 XImage_expand5/6 位复制展开——与 painter 逐像素路径读
 *          RGB16 的规范化展开逐位同式, 565→8888 往返无损); ARGB32 源
 *          4B 直拷。
 */
static void xgc_scaledRescaleRect(XGuiClientPrivate* d,
                                  int rx, int ry, int rw, int rh)
{
    float s = d->m_viewScale;
    int dstW = d->m_viewDstW;
    int dstH = d->m_viewDstH;
    int fw = (int)d->m_fbWidth;
    int fh = (int)d->m_fbHeight;
    int dx0, dy0, dx1, dy1, dy;
    int bppIn;
    if (!(s > 0.0f) || dstW <= 0 || dstH <= 0 || rw <= 0 || rh <= 0 ||
        XImage_isNull(&d->m_fb) || XImage_isNull(&d->m_scaled) ||
        !d->m_scaledValid)
        return;
    if (!xgc_scaledColMapBuild(d, dstW, fw)) return;
    dx0 = (int)((float)rx * s) - 1;
    dy0 = (int)((float)ry * s) - 1;
    dx1 = (int)((float)(rx + rw) * s + 0.5f) + 1;
    dy1 = (int)((float)(ry + rh) * s + 0.5f) + 1;
    if (dx0 < 0) dx0 = 0;
    if (dy0 < 0) dy0 = 0;
    if (dx1 > dstW) dx1 = dstW;
    if (dy1 > dstH) dy1 = dstH;
    if (dx0 >= dx1 || dy0 >= dy1) return;
    bppIn = (XImage_format(&d->m_fb) == XImageFormat_RGB16) ? 2 : 4;
    for (dy = dy0; dy < dy1; ++dy) {
        float ty = ((float)dy + 0.5f) / (float)dstH;
        int sy = (int)(ty * (float)fh); /* fy≥0: 截断=floor。 */
        uint32_t* dstRow;
        if (sy >= fh) sy = fh - 1;
        dstRow = (uint32_t*)(void*)XImage_scanLine(&d->m_scaled, dy);
        if (!dstRow) continue;
        {
            int dx;
            const uint8_t* fbBits = XImage_bits(&d->m_fb);
            size_t fbStride = (size_t)XImage_bytesPerLine(&d->m_fb);
            const uint8_t* srcRow = fbBits + (size_t)sy * fbStride;
            if (bppIn == 2) {
                /* RGB16 → ARGB32: 位复制展开(XImage_expand5/6 同式:
                 * expand5(v)=(v<<3|v>>2), expand6(v)=(v<<2|v>>4))。 */
                for (dx = dx0; dx < dx1; ++dx) {
                    int sx = d->m_scaledMap[dx];
                    uint16_t v;
                    unsigned b5, g6, r5;
                    if (sx >= fw) sx = fw - 1;
                    memcpy(&v, srcRow + (size_t)sx * 2u, sizeof(v));
                    b5 = (unsigned)(v & 0x1Fu);
                    g6 = (unsigned)((v >> 5) & 0x3Fu);
                    r5 = (unsigned)((v >> 11) & 0x1Fu);
                    dstRow[dx] = 0xFF000000u |
                                 ((((r5 << 3) | (r5 >> 2)) & 0xFFu) << 16) |
                                 ((((g6 << 2) | (g6 >> 4)) & 0xFFu) << 8) |
                                 (((b5 << 3) | (b5 >> 2)) & 0xFFu);
                }
            } else {
                for (dx = dx0; dx < dx1; ++dx) {
                    int sx = d->m_scaledMap[dx];
                    if (sx >= fw) sx = fw - 1;
                    memcpy(&dstRow[dx], srcRow + (size_t)sx * 4u, 4u);
                }
            }
        }
    }
}

/** @brief FIT 缓存开关(env XGUI_REMOTE_FIT_CACHE, 缺省开; 0=回退旧
 *         全量缩放路径——内存受限设备可关, 省 2.4MB 级缓存)。 */
static bool xgc_fitCacheOn(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* v = XSystem_environment("XGUI_REMOTE_FIT_CACHE");
        cached = !(v != NULL && v[0] == '0' && v[1] == '\0');
    }
    return cached != 0;
}

/**
 * @brief  确保缩放缓存与当前变换/fb 几何一致(不一致即整幅重建一次)。
 * @return true 缓存可用(或本调用刚全量重建); false 不可用(paint 回退
 *         旧 drawImage_3 全量缩放路径, 语义不变)。
 */
static bool xgc_scaledCacheEnsure(XGuiClient* self, XGuiClientPrivate* d)
{
    XImageFormat fmt;
    (void)self;
    if (!xgc_fitCacheOn()) return false;
    if (!d->m_fbValid || XImage_isNull(&d->m_fb)) {
        if (d->m_scaledValid) xgc_scaledCacheReset(d);
        return false;
    }
    if (d->m_scaledValid &&
        d->m_scaledScale == d->m_viewScale &&
        d->m_scaledOffX == d->m_viewOffX &&
        d->m_scaledOffY == d->m_viewOffY &&
        XImage_width(&d->m_scaled) == d->m_viewDstW &&
        XImage_height(&d->m_scaled) == d->m_viewDstH &&
        XImage_format(&d->m_scaled) == XImageFormat_ARGB32)
        return true;
    xgc_scaledCacheReset(d);
    fmt = XImage_format(&d->m_fb);
    if ((fmt != XImageFormat_ARGB32 && fmt != XImageFormat_RGB16) ||
        d->m_viewDstW <= 0 || d->m_viewDstH <= 0)
        return false;
    if (!XImage_reinit_ex(&d->m_scaled, d->m_viewDstW, d->m_viewDstH,
                          XImageFormat_ARGB32))
        return false;
    d->m_scaledScale = d->m_viewScale;
    d->m_scaledOffX = d->m_viewOffX;
    d->m_scaledOffY = d->m_viewOffY;
    d->m_scaledValid = true;
    /* 全量首建(仅 META/resize/换档时一次; 稳态走增量路径)。 */
    xgc_scaledRescaleRect(d, 0, 0, (int)d->m_fbWidth, (int)d->m_fbHeight);
    return true;
}

/**
 * @brief  像素正确性离屏断言(env XGUI_REMOTE_FIT_SCALE_SELFTEST=1)。
 * @details 三重断言(ARGB32/RGB16 两格式): ①随机损伤带序列增量重缩放 ==
 *          全量重算逐字节(增量记账不缺列/行); ②缓存全量重算 ==
 *          XPainter_drawImage_3 painter 全量路径逐字节(可见语义不漂移)。
 *          stderr 单行 PASS/FAIL(探针纪律: 交付前可关, 缺省不跑)。
 * @note    XImage 为值语义共享 m_data: 代理私有块仅做结构体头拷贝借用
 *          bits, 清理只 deinit 原头对象, 代理侧不重复 deinit。
 */
static bool xgc_fitScaleSelfTest(void)
{
    static const int kW = 253, kH = 181; /* 质数尺寸: 逼非整比缩放。 */
    static const uint8_t wires[2] = { (uint8_t)XGUI_REMOTE_PF_ARGB32,
                                      (uint8_t)XGUI_REMOTE_PF_RGB565 };
    int ci;
    for (ci = 0; ci < 2; ++ci) {
        XImage fb, cacheA, cacheB, painted;
        XGuiClientPrivate inc, full; /* 两个独立代理私有块。 */
        XPainter painter;
        int pass = 1;
        int bpp, dstW, dstH;
        unsigned seed;
        int y;
        XMemset(&inc, 0, sizeof(inc));
        XMemset(&full, 0, sizeof(full));
        XImage_init(&fb);
        XImage_init(&cacheA);
        XImage_init(&cacheB);
        XImage_init(&painted);
        if (!XImage_reinit_ex(&fb, kW, kH, xgc_localImageFormat(wires[ci])))
            pass = 0;
        /* 随机图案(确定种子): 逐像素写原始值(满幅, 无未初始化缝隙)。 */
        if (pass) {
            uint8_t* bits = XImage_bits(&fb);
            size_t stride = (size_t)XImage_bytesPerLine(&fb);
            int x;
            bpp = (XImage_format(&fb) == XImageFormat_RGB16) ? 2 : 4;
            for (y = 0; y < kH && pass; ++y) {
                for (x = 0; x < kW; ++x) {
                    uint32_t v = (uint32_t)(x * 7 + y * 13 + ((x * y) % 31));
                    uint8_t* p = bits + (size_t)y * stride +
                                 (size_t)x * (size_t)bpp;
                    if (bpp == 2) {
                        uint16_t p5 = (uint16_t)(((v & 0x1Fu) << 11) |
                                                 ((v & 0x3Fu) << 5) |
                                                 (v & 0x1Fu));
                        p[0] = (uint8_t)(p5 & 0xFF);
                        p[1] = (uint8_t)(p5 >> 8);
                    } else {
                        p[0] = (uint8_t)(v * 3);
                        p[1] = (uint8_t)(v * 5);
                        p[2] = (uint8_t)(v * 7);
                        p[3] = 0xFF;
                    }
                }
            }
        }
        /* 公共视图变换: scale=0.5, 偏移 (3,5), dst=127×91(非整比)。 */
        dstW = (int)((float)kW * 0.5f + 0.5f);
        dstH = (int)((float)kH * 0.5f + 0.5f);
        /* ① 增量侧(cacheA): 12 条伪随机损伤带逐条重缩放, 末补全幅带。
         * 缓存恒 ARGB32(与 Ensure 建缓存策略一致; RGB16 fb 用例同时
         * 对拍跨格式展开)。 */
        if (pass) {
            seed = 12345u;
            y = 0;
            if (!XImage_reinit_ex(&cacheA, dstW, dstH,
                                  XImageFormat_ARGB32))
                pass = 0;
            inc.m_fb = fb; /* 头拷贝借用(仅本测试作用域内使用)。 */
            inc.m_fbValid = true;
            inc.m_fbWidth = (uint16_t)kW;
            inc.m_fbHeight = (uint16_t)kH;
            inc.m_viewScale = 0.5f;
            inc.m_viewOffX = 3;
            inc.m_viewOffY = 5;
            inc.m_viewDstW = dstW;
            inc.m_viewDstH = dstH;
            inc.m_scaled = cacheA;
            inc.m_scaledScale = inc.m_viewScale;
            inc.m_scaledOffX = inc.m_viewOffX;
            inc.m_scaledOffY = inc.m_viewOffY;
            inc.m_scaledValid = true;
            while (pass && y < kH) {
                int h = (int)(seed % 17u) + 1;
                int x0 = (int)((seed >> 5) % 97u);
                int w = (int)((seed >> 11) % 131u) + 1;
                if (h > kH - y) h = kH - y;
                if (x0 >= kW) x0 = kW - 1;
                if (x0 + w > kW) w = kW - x0;
                xgc_scaledRescaleRect(&inc, x0, y, w, h);
                y += 11;
                seed = seed * 1103515245u + 12345u;
            }
            if (pass)
                xgc_scaledRescaleRect(&inc, 0, 0, kW, kH); /* 完整终态。 */
        }
        /* ② 全量侧(cacheB): 同变换一次全幅重缩放。 */
        if (pass) {
            if (!XImage_reinit_ex(&cacheB, dstW, dstH,
                                  XImageFormat_ARGB32))
                pass = 0;
            full = inc; /* 头拷贝; m_scaledMap 下方独立处置。 */
            full.m_scaledMap = NULL;
            full.m_scaledMapCap = 0;
            full.m_scaled = cacheB;
            full.m_scaledValid = true;
            if (pass)
                xgc_scaledRescaleRect(&full, 0, 0, kW, kH);
        }
        /* 断言①: 增量终态 == 全量重算(逐字节)。 */
        if (pass &&
            (XImage_bytesPerLine(&cacheA) != XImage_bytesPerLine(&cacheB) ||
             XImage_width(&cacheA) != XImage_width(&cacheB) ||
             XImage_height(&cacheA) != XImage_height(&cacheB) ||
             memcmp(XImage_bits(&cacheA), XImage_bits(&cacheB),
                    (size_t)XImage_bytesPerLine(&cacheA) *
                        (size_t)XImage_height(&cacheA)) != 0))
            pass = 0;
        /* 断言②: 缓存缩放 == painter 全量路径(painted)。painter 侧
         * 无平移且目标原点 (0,0), 采样式与缓存坐标系公式逐位一致。 */
        if (pass && XImage_reinit_ex(&painted, dstW, dstH,
                                     XImageFormat_ARGB32)) {
            XPainter_init(&painter, NULL);
            if (XPainter_begin_image(&painter, &painted)) {
                XPainter_drawImage_3(&painter, 0, 0, dstW, dstH,
                                     &fb, 0, 0, kW, kH);
                XPainter_end(&painter);
            } else {
                pass = 0;
            }
            XPainter_deinit(&painter);
            if (pass &&
                (XImage_bytesPerLine(&painted) !=
                     XImage_bytesPerLine(&cacheA) ||
                 memcmp(XImage_bits(&painted), XImage_bits(&cacheA),
                        (size_t)XImage_bytesPerLine(&cacheA) *
                            (size_t)XImage_height(&cacheA)) != 0))
                pass = 0;
        } else if (pass) {
            pass = 0;
        }
        XClassDeinit(&cacheA);
        XClassDeinit(&cacheB);
        XClassDeinit(&painted);
        XClassDeinit(&fb);
        if (inc.m_scaledMap)
            XMemory_free(inc.m_scaledMap, XCLASS_DEFAULT_MEMORY_TYPE);
        if (full.m_scaledMap)
            XMemory_free(full.m_scaledMap, XCLASS_DEFAULT_MEMORY_TYPE);
        if (!pass) return false;
    }
    return true;
}

/** @brief 损伤区增量上屏(坐标域按模式映射): 1:1 tile 矩形即控件坐标
 *         直用; FIT 远端 tile 矩形按变换映射到控件域(±1px 取整护栏,
 *         最近邻采样边界不缺列), updateRegion 后清损伤。
 *         [perf9 路5] FIT 分支先把损伤矩形增量重缩放进缓存(失败=缓存
 *         未就绪, paint 回退全量路径), 上屏区域映射逻辑不变。 */
static void xgc_presentDamage(XGuiClient* self, XGuiClientPrivate* d)
{
    if (XRegion_isEmpty(&d->m_damage)) return;
    /* [stage] cli-merg=首块损伤到达→present 调用(µs 单调戳; perf9 路5
     * 改口径: 立即路径也采样, 首现延迟 p50/p95 直接可比)。
     * 同时登记上屏触发戳, paintEvent 派发后算 cli-paint/cli-blit。 */
    if (d->m_presentFirstUs != 0) {
        uint64_t nowUs = XGuiRemoteUdp_stageProfNowUs();
        if (nowUs > d->m_presentFirstUs)
            XGuiRemoteUdp_stageProfSpan("cli-merg", d->m_presentFirstUs,
                                        nowUs);
    }
    d->m_stagePaintFromUs = XGuiRemoteUdp_stageProfNowUs();
    d->m_stagePaintPend = d->m_stagePaintFromUs != 0;
    if (d->m_viewFit == XGUI_CLIENT_VIEW_FIT && d->m_viewScale > 0.0f &&
        d->m_fbValid) {
        XRegion out;
        /* [perf9 路5 任务3] 损伤矩形先增量重缩放进缓存(缓存未就绪/重建
         * 失败时 m_scaledValid=false, paint 回退旧 drawImage_3 全量缩放
         * 路径, 正确性不依赖缓存)。 */
        (void)xgc_scaledCacheEnsure(self, d);
        if (d->m_scaledValid) {
            int ri;
            for (ri = 0; ri < d->m_damage.count; ++ri) {
                const XRect* r = &d->m_damage.rects[ri];
                xgc_scaledRescaleRect(d, r->x, r->y, r->width, r->height);
            }
        }
        float s = d->m_viewScale;
        int i;
        XRegion_init(&out);
        for (i = 0; i < d->m_damage.count; ++i) {
            const XRect* r = &d->m_damage.rects[i];
            int x0 = d->m_viewOffX + (int)((float)r->x * s) - 1;
            int y0 = d->m_viewOffY + (int)((float)r->y * s) - 1;
            int x1 = d->m_viewOffX + (int)((float)(r->x + r->width) * s) + 1;
            int y1 = d->m_viewOffY + (int)((float)(r->y + r->height) * s) + 1;
            XRect rr;
            XRect_init(&rr, x0, y0, x1 - x0, y1 - y0);
            XRegion_addRect(&out, &rr);
        }
        XWidget_updateRegion((XWidget*)self, &out);
        XRegion_deinit(&out);
    } else {
        XWidget_updateRegion((XWidget*)self, &d->m_damage);
    }
    XRegion_clear(&d->m_damage);
}

/** @brief FIT 模式整控件置脏(变换生效面=整幅信箱); 1:1 旧行为=整控件
 *         update 语义不变。 */
static void xgc_invalidateWhole(XGuiClient* self)
{
    XWidget_update((XWidget*)self);
}

/** @brief 调色板取色(XPALETTE_ON 关闭时回退深灰, 占位画面可辨)。 */
static uint32_t xgc_paletteColor(const XGuiClient* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF202020u;
#endif /* XPALETTE_ON */
}

/** @brief 信号发射(m_signalSlot 未挂时丢弃 args 防泄漏; 同 XMovie_emit)。 */
static void xgc_emit(XGuiClient* self, size_t signal, XVarList* args)
{
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else if (args) {
        XVarList_delete(args);
    }
}

/** @brief 发 errorOccurred(XVar 需左值, 统一经本助手落局部变量)。 */
static void xgc_emitError(XGuiClient* self, int code)
{
    xgc_emit(self, (size_t)XGuiClient_errorOccurred_signal,
             XVarList_Create(XVar(int, code)));
}

/** @brief 画面是否可用(Streaming 且 backbuffer 就绪; 决定画帧还是占位)。 */
static bool xgc_hasPicture(const XGuiClient* self)
{
    const XGuiClientPrivate* d;
    if (!self || !self->m_d) return false;
    d = (const XGuiClientPrivate*)self->m_d;
    return d->m_state == XGUI_REMOTE_STATE_STREAMING && d->m_fbValid &&
           !XImage_isNull(&d->m_fb);
}

/** @brief 输入转发通道是否就绪(转发开 + Streaming + 远端元信息已知)。 */
static bool xgc_inputChannelReady(const XGuiClient* self, bool enabled)
{
    const XGuiClientPrivate* d;
    if (!enabled || !self || !self->m_d) return false;
    d = (const XGuiClientPrivate*)self->m_d;
    return d->m_state == XGUI_REMOTE_STATE_STREAMING && d->m_fbValid;
}

/* ==================== 发送链(outbox + writeFrame 纪律) ==================== */

/*
 * 客户端发送帧小(输入/请求 ≤2KiB 级), 一般无部分写; 但仍按冻结纪律处理:
 * writeFrame 返回 < 总帧长时把余量入 outbox, poll 回调每圈按档位
 * txBudgetBytes 续传; outbox 非空时新帧整帧入队保序(不再直写)。
 */

/** @brief outbox 入队(容量帽 XGC_OUTBOX_CAP; 返回 false=溢出丢弃)。 */
static bool xgc_outboxEnqueue(XGuiClientPrivate* d,
                              const uint8_t* bytes, size_t count)
{
    size_t need;
    if (d->m_outboxOff > 0) {
        /* 压实: 已写出前缀丢弃, 余量搬到队头。 */
        size_t pending = d->m_outboxLen - d->m_outboxOff;
        if (pending > 0) {
            memmove(d->m_outbox, d->m_outbox + d->m_outboxOff, pending);
        }
        d->m_outboxLen = pending;
        d->m_outboxOff = 0;
    }
    need = d->m_outboxLen + count;
    if (need > XGC_OUTBOX_CAP) return false;
    if (need > d->m_outboxCap) {
        size_t cap = d->m_outboxCap ? d->m_outboxCap : 4096;
        uint8_t* p;
        while (cap < need) cap *= 2;
        p = (uint8_t*)XRealloc_System(d->m_outbox, cap);
        if (!p) return false;
        d->m_outbox = p;
        d->m_outboxCap = cap;
    }
    memcpy(d->m_outbox + d->m_outboxLen, bytes, count);
    d->m_outboxLen += count;
    return true;
}

/**
 * @brief  发送一帧(writeFrame 冻结纪律: 返回值≥0, 短写余量入 outbox)。
 * @return true 已被设备/队列接受(整帧); false 硬错误或溢出丢弃。
 */
static bool xgc_sendFrame(XGuiClient* self, XGuiRemoteMsgType type,
                          const void* payload, size_t payloadBytes)
{
    XGuiClientPrivate* d;
    /* 客户端消息上限自守: IME 双文本满载(2+1024+2+1024+16)亦远小于此。 */
    uint8_t frame[XGUI_REMOTE_FRAME_HEADER_BYTES +
                  2u * XGUI_REMOTE_MAX_TEXT_BYTES + 32u];
    size_t total;
    int64_t accepted;
    if (!self || !self->m_d) return false;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev) return false;
    /* [perf9 路5] 输入转发统一出口: 记最近转发时刻(合并窗自适应交互判据;
     * UDP/TCP 两路径同盖)。 */
    if (type == XGUI_REMOTE_MSG_INPUT_KEY ||
        type == XGUI_REMOTE_MSG_INPUT_POINTER ||
        type == XGUI_REMOTE_MSG_INPUT_WHEEL ||
        type == XGUI_REMOTE_MSG_INPUT_TOUCH ||
        type == XGUI_REMOTE_MSG_INPUT_IME) {
        d->m_lastInputFwdMs = (uint64_t)xgc_nowMs();
    }
    /* UDP 旁路(2026-10-04, 设计稿 §4.2): 输入消息走数据报(通道内序号+
     * NACK 重传=可靠有序); 通道未激活/未递交时落回下方 TCP 原路径。 */
    if (d->m_udp &&
        d->m_state == XGUI_REMOTE_STATE_STREAMING &&
        XGuiRemoteUdpChannel_state(d->m_udp) ==
            XGUI_REMOTE_UDP_STATE_ACTIVE &&
        (type == XGUI_REMOTE_MSG_INPUT_KEY ||
         type == XGUI_REMOTE_MSG_INPUT_POINTER ||
         type == XGUI_REMOTE_MSG_INPUT_WHEEL ||
         type == XGUI_REMOTE_MSG_INPUT_TOUCH ||
         type == XGUI_REMOTE_MSG_INPUT_IME)) {
        uint8_t dg[1 + 2u * XGUI_REMOTE_MAX_TEXT_BYTES + 32u];
        if (payloadBytes + 1 <= sizeof(dg)) {
            dg[0] = (uint8_t)type;
            if (payloadBytes != 0)
                memcpy(dg + 1, payload, payloadBytes);
            if (XGuiRemoteUdpChannel_sendInput(d->m_udp, dg,
                                               payloadBytes + 1)) {
                d->m_stats.bytesSent +=
                    (uint64_t)(XGUI_REMOTE_UDP_HEADER_BYTES +
                               payloadBytes + 1);
                return true;
            }
        }
    }
    if (payloadBytes > sizeof(frame) - XGUI_REMOTE_FRAME_HEADER_BYTES) {
        return false;
    }
    total = XGUI_REMOTE_FRAME_HEADER_BYTES + payloadBytes;
    XGuiRemoteProto_putU32(frame, (uint32_t)payloadBytes);
    frame[4] = (uint8_t)type;
    if (payloadBytes != 0) {
        memcpy(frame + XGUI_REMOTE_FRAME_HEADER_BYTES, payload, payloadBytes);
    }

    if (d->m_outboxLen > d->m_outboxOff) {
        /* 有待写余量: 整帧入队保序(冻结纪律: 待写清空前不直写新帧)。 */
        if (!xgc_outboxEnqueue(d, frame, total)) {
            if (!d->m_overflowNotified) {
                d->m_overflowNotified = true;
                xgc_emitError(self, (int)XGUI_REMOTE_ERR_BUFFER_OVERFLOW);
            }
            return false;
        }
        return true;
    }

    accepted = XGuiRemoteProto_writeFrame(d->m_dev, type, payload, payloadBytes);
    if (accepted < 0) return false; /* 设备硬错误, 上层按断链处理。 */
    d->m_stats.bytesSent += (uint64_t)accepted;
    if ((size_t)accepted < total) {
        /* 部分写=背压: 余量入 outbox 留待下圈续传。 */
        if (!xgc_outboxEnqueue(d, frame + (size_t)accepted,
                               total - (size_t)accepted)) {
            if (!d->m_overflowNotified) {
                d->m_overflowNotified = true;
                xgc_emitError(self, (int)XGUI_REMOTE_ERR_BUFFER_OVERFLOW);
            }
            return false;
        }
    }
    return true;
}

/** @brief poll 回调内续传 outbox 余量(限档位 txBudgetBytes 预算)。 */
static void xgc_flushOutbox(XGuiClient* self)
{
    XGuiClientPrivate* d;
    size_t budget;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || d->m_outboxLen <= d->m_outboxOff) return;
    budget = d->m_profile.txBudgetBytes;
    if (budget == 0) budget = XGC_OUTBOX_CAP; /* 档位 0=不限(防御死循环)。 */
    while (d->m_outboxLen > d->m_outboxOff && budget > 0) {
        size_t remain = d->m_outboxLen - d->m_outboxOff;
        size_t want = remain < budget ? remain : budget;
        int64_t written = XIODevice_write_1(d->m_dev,
                (const char*)(d->m_outbox + d->m_outboxOff), (int64_t)want);
        if (written < 0) return;             /* 硬错误: 交由断链路径处理。 */
        if (written == 0) break;             /* 设备暂不可写, 下圈再试。 */
        d->m_outboxOff += (size_t)written;
        d->m_stats.bytesSent += (uint64_t)written;
        budget -= ((size_t)written < budget) ? (size_t)written : budget;
    }
    if (d->m_outboxOff >= d->m_outboxLen) {
        d->m_outboxOff = 0;
        d->m_outboxLen = 0;
    }
}

/* ==================== 键盘按下集合(PRESS/RELEASE 成对纪律) ==================== */

static bool xgc_pressedKeysAdd(XGuiClientPrivate* d, uint32_t key)
{
    int i;
    for (i = 0; i < d->m_pressedKeyCount; ++i) {
        if (d->m_pressedKeys[i] == key) return true; /* 已在集合(幂等)。 */
    }
    if (d->m_pressedKeyCount >= d->m_pressedKeyCap) {
        int cap = d->m_pressedKeyCap ? d->m_pressedKeyCap * 2 : 8;
        uint32_t* p = (uint32_t*)XRealloc_System(d->m_pressedKeys,
                                                 (size_t)cap * sizeof(uint32_t));
        if (!p) return false;
        d->m_pressedKeys = p;
        d->m_pressedKeyCap = cap;
    }
    d->m_pressedKeys[d->m_pressedKeyCount++] = key;
    return true;
}

static bool xgc_pressedKeysRemove(XGuiClientPrivate* d, uint32_t key)
{
    int i;
    for (i = 0; i < d->m_pressedKeyCount; ++i) {
        if (d->m_pressedKeys[i] == key) {
            int j;
            for (j = i + 1; j < d->m_pressedKeyCount; ++j) {
                d->m_pressedKeys[j - 1] = d->m_pressedKeys[j];
            }
            --d->m_pressedKeyCount;
            return true;
        }
    }
    return false;
}

/* ==================== 会话建立与拆除 ==================== */

/** @brief 发 HELLO(版本/能力/认证建议/端名, §3.4; BANNER_WAIT 后调用)。 */
static void xgc_sendHello(XGuiClient* self)
{
    XGuiClientPrivate* d;
    XGuiRemoteMsgHello hello;
    const char* name = "XGuiClient";
    size_t n;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev) return;
    XMemset(&hello, 0, sizeof(hello));
    hello.protocolVersion = XGUI_REMOTE_PROTOCOL_VERSION;
    hello.capabilities = d->m_localCaps;
    /* UDP 旁路按用户开关宣告(2026-10-04): 关=不宣告, 服务端永不发 OFFER。 */
    if (!d->m_udpWanted)
        hello.capabilities &= ~(uint32_t)XGUI_REMOTE_CAP_UDP;
    hello.authMethod = (uint8_t)(d->m_password[0] != '\0'
            ? XGUI_REMOTE_AUTH_SHA256_CHALLENGE : XGUI_REMOTE_AUTH_NONE);
    n = strlen(name);
    if (n > XGUI_REMOTE_MAX_NAME_BYTES) n = XGUI_REMOTE_MAX_NAME_BYTES;
    hello.nameBytes = (uint16_t)n;
    memcpy(hello.name, name, n);
    /* 冻结契约(XGuiRemoteProto.h "严禁把结构体整块 cast 上网"): 先经 enc*
     * 显式小端序列化再上网(修复: 原直发 sizeof(struct) 含 padding 且字段
     * 错位, 服务端严格 dec(off==len) 必拒 → 握手必败)。 */
    {
        uint8_t buf[XGUI_REMOTE_MAX_NAME_BYTES + 16];
        size_t len = XGuiRemoteProto_encHello(buf, sizeof(buf), &hello);
        if (len > 0)
            xgc_sendFrame(self, XGUI_REMOTE_MSG_HELLO, buf, len);
    }
}

/** @brief 重连定时器回调(指数退避重连; §7.3)。 */
static void xgc_reconnectFire(void* userData, XTimerData* timer)
{
    XGuiClient* self = (XGuiClient*)userData;
    XGuiClientPrivate* d;
    (void)timer;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (d->m_state != XGUI_REMOTE_STATE_DISCONNECTED || !d->m_wantConnect ||
        d->m_dev != NULL) {
        return;
    }
    xgc_connectNetwork(self); /* 复用网络连接路径(重试计数继续累加)。 */
}

/** @brief 安排下一次重连(基础间隔 ×2^n, 上限 30s)。 */
static void xgc_scheduleReconnect(XGuiClient* self)
{
    XGuiClientPrivate* d;
    uint32_t delay;
    uint32_t shift;
    uint32_t i;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_autoReconnect || !d->m_wantConnect || !d->m_reconnectTimer) {
        return;
    }
    ++d->m_reconnectAttempts;
    ++d->m_reconnectCountTotal; /* 演示页统计: 累计口径, 达成不清零。 */
    delay = d->m_reconnectBaseMs;
    shift = d->m_reconnectAttempts;
    if (shift > 16) shift = 16;
    /* delay = base × 2^(attempts-1), 逐次翻倍防溢出。 */
    for (i = 1; i < shift && delay < XGC_RECONNECT_MAX_MS; ++i) {
        delay *= 2u;
    }
    if (delay > XGC_RECONNECT_MAX_MS) delay = XGC_RECONNECT_MAX_MS;
    XTimer_setTimeout(d->m_reconnectTimer, delay);
    XTimer_start_base(d->m_reconnectTimer);
}

/**
 * @brief  拆除当前会话(协议断开/传输丢失/静默换接/析构共用)。
 * @param  reason   断开原因(disconnected 信号载荷)。
 * @param  errCode  会话错误码(NONE=不发 errorOccurred)。
 * @param  sendBye  true=设备仍开且会话就绪时先补发 RELEASE 纪律帧+BYE。
 * @param  silent   true=静默拆除(不发 disconnected/errorOccurred/不重连,
 *                  connectToHost 重入与析构用)。
 */
static void xgc_teardown(XGuiClient* self, XGuiRemoteByeReason reason,
                         XGuiRemoteError errCode, bool sendBye, bool silent)
{
    XGuiClientPrivate* d;
    XIODevice* dev;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    dev = d->m_dev;
    if (dev == NULL && d->m_state == XGUI_REMOTE_STATE_DISCONNECTED) {
        return; /* 已拆除, 幂等。 */
    }
#if defined(XGC_TILE_DEBUG) && XGC_TILE_DEBUG
    fprintf(stderr, "XGC_TEARDOWN reason=%d err=%d sendBye=%d state=%d\n",
            (int)reason, (int)errCode, (int)sendBye, (int)d->m_state);
#endif

    /* PRESS/RELEASE 成对纪律(§7.4): 断链前向远端补发全部 RELEASE,
     * 防服务端 grabMouse/按键滞留; 设备已死则直接清集合。 */
    if (dev != NULL && sendBye && d->m_sessionReady) {
        int i;
        for (i = 0; i < d->m_pressedKeyCount; ++i) {
            XGuiRemoteMsgInputKey key;
            uint8_t kbuf[24];
            size_t klen;
            XMemset(&key, 0, sizeof(key));
            key.action = XGUI_REMOTE_KEY_RELEASE;
            key.key = d->m_pressedKeys[i];
            key.timestampMs = (uint32_t)xgc_nowMs();
            /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
            klen = XGuiRemoteProto_encInputKey(kbuf, sizeof(kbuf), &key);
            if (klen > 0)
                xgc_sendFrame(self, XGUI_REMOTE_MSG_INPUT_KEY, kbuf, klen);
        }
        if (d->m_pressedButtons != 0) {
            int bit;
            for (bit = 0; bit < 16; ++bit) {
                uint16_t mask = (uint16_t)(1u << bit);
                if (d->m_pressedButtons & mask) {
                    XGuiRemoteMsgInputPointer ptr;
                    uint8_t pbuf[24];
                    size_t plen;
                    XMemset(&ptr, 0, sizeof(ptr));
                    ptr.action = XGUI_REMOTE_PTR_RELEASE;
                    ptr.button = (uint8_t)mask;
                    ptr.buttons = 0;
                    ptr.timestampMs = (uint32_t)xgc_nowMs();
                    plen = XGuiRemoteProto_encInputPointer(pbuf, sizeof(pbuf),
                                                           &ptr);
                    if (plen > 0)
                        xgc_sendFrame(self, XGUI_REMOTE_MSG_INPUT_POINTER,
                                      pbuf, plen);
                }
            }
        }
        {
            XGuiRemoteMsgBye bye;
            uint8_t bbuf[XGUI_REMOTE_MAX_MSG_BYTES + 16];
            size_t blen;
            XMemset(&bye, 0, sizeof(bye));
            bye.reason = (uint8_t)reason;
            blen = XGuiRemoteProto_encBye(bbuf, sizeof(bbuf), &bye);
            if (blen > 0)
                xgc_sendFrame(self, XGUI_REMOTE_MSG_BYE, bbuf, blen);
        }
        xgc_flushOutbox(self); /* BYE/RELEASE 余量尽力而出(一圈内)。 */
    }
    d->m_pressedButtons = 0;
    d->m_pressedKeyCount = 0;
    d->m_hasPendingMove = false;
    d->m_pingAwait = false;
    d->m_challengeValid = false;

    /* 画面作废(§7.3): 不绘制陈旧帧, backbuffer 待下次 FB_META 重建。 */
    d->m_fbValid = false;
    /* 待上屏损伤一并作废(陈旧几何域不再派发)。 */
    d->m_presentFirstMs = 0;
    d->m_presentLastMs = 0;
    d->m_presentFirstUs = 0;
    XRegion_clear(&d->m_damage);

    /* outbox 是会话级余量缓冲, 断链即弃(统计保留累加值)。 */
    d->m_outboxLen = 0;
    d->m_outboxOff = 0;
    d->m_overflowNotified = false;
    d->m_sessionReady = false;
    d->m_bannerSent = false;
    d->m_bannerGot = false;
    d->m_bannerGotBytes = 0;
    d->m_state = XGUI_REMOTE_STATE_DISCONNECTED;
    /* UDP 旁路: 会话级解绑(通道对象保留复用; 重连后 OFFER 重走建链,
     * 2026-10-04)。 */
    if (d->m_udp) {
        XGuiRemoteUdpChannel_unbindSession(d->m_udp);
        d->m_udpState = (int)XGUI_REMOTE_UDP_STATE_OFF;
    }
    d->m_udpToken = 0;
    d->m_udpRetryMs = 0;
    d->m_udpLastFrameMs = INT64_MAX;

    /* 方案③(2026-10-05): 会话级 FB_REQUEST 状态复位——重连=新连接,
     * 首个 FB_META 自然重发; 视口通告基线一并复位; 首帧探针收尾
     * (未齐打印 partial 行供断链归因)并释放位图。 */
    d->m_fbReqSent = false;
    d->m_viewRepX = 0;
    d->m_viewRepY = 0;
    d->m_viewRepW = 0;
    d->m_viewRepH = 0;
    d->m_viewRepMs = 0;
    xgc_ffReset(d, "teardown");

    /* 帧 reader 复位(内部缓冲保留复用, 析构时统一释放)。 */
    XGuiRemoteFrameReader_deinit(&d->m_reader);
    XGuiRemoteFrameReader_init(&d->m_reader);

    /* 设备处置: 自有 socket 中止+延迟销毁; 借用设备仅解除引用。
     * 先清 m_dev 再 abort——socket 的 disconnected 信号回入本控件时
     * 以 m_dev==NULL 判定幂等, 不重复断链。 */
    d->m_dev = NULL;
    if (dev != NULL) {
        if (d->m_devOwned) {
            XAbstractSocket_abort((XAbstractSocket*)dev);
            /* [2026-10-06 用户裁定简化] 统一走内建异步删除
             * XObject_deleteLater: 设备信号槽内同步删除有发射方槽返回后
             * 续读 sock 的 UAF(memhunt F2 受控对练 ASan 实证), 异步删除
             * 天然规避; 泄漏疑虑已由 VXAbstractSocket_event 的
             * DeferredDelete 放行修(XGUI_REMOTE 根修, fd 失效不再吞 DD)
             * 消除——延迟删除必然兑现, XObject_deinit 即时撤回挂起事件与
             * 定时器(XObject.c:585-590), 会话设备树不漏
             * (memhunt F2 的 57.5KB/164objs 泄漏不复现)。m_pumpTimer
             * 先例维持同步删除不变: 定时器事件派发不复查停态的 UAF 属
             * XTimer 消费侧, 与本设备无关。 */
            XObject_deleteLater((XObject*)dev);
            d->m_devOwned = false;
        }
        d->m_devIsSsl = false;
    }

    /* poll 回调注销(无设备即无泵)。 */
    if (d->m_pollHandle) {
        XAbstractEventDispatcher_removePollCallback(d->m_pollHandle);
        d->m_pollHandle = NULL;
    }
    if (d->m_pumpTimer) { /* 兜底定时器随会话同生死(基准优化 2026-10-02)。 */
        XTimer_stop_base(d->m_pumpTimer);
        /* [perf9 路2 修复 2026-10-05] 同步删除而非 deleteLater:
         * deleteLater 前, 已投递的定时器事件先于删除事件被派发,
         * XTimerData_out 不复查停态即回调 m_userData(已随析构释放)
         * ——UAF(离屏 R5 串行拆客户端 gdb 栈实证: xgc_pollPump 读
         * 释放后 m_d)。同步删除令 XObject_deinit 即时撤回本对象的
         * 挂起事件, 窗口闭合。重连定时器(析构路径)同款。 */
        XClassDelete((XClass*)d->m_pumpTimer);
        d->m_pumpTimer = NULL;
    }

    if (silent) return;
    if (errCode != XGUI_REMOTE_ERR_NONE) {
        xgc_emitError(self, (int)errCode);
    }
    xgc_emit(self, (size_t)XGuiClient_disconnected_signal,
             XVarList_Create(XVar(int, reason)));
    xgc_scheduleReconnect(self);
}

/** @brief 对端横幅校验通过(§3.3): 魔数 memcmp + 版本下限。 */
static void xgc_bannerComplete(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    d->m_bannerGot = true;
    if (!XGuiRemoteProto_bannerIsValid(d->m_bannerBuf)) {
        xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                     XGUI_REMOTE_ERR_PROTOCOL, true, false);
        return;
    }
    if (XGuiRemoteProto_bannerVersion(d->m_bannerBuf) <
        XGUI_REMOTE_PROTOCOL_VERSION) {
        /* 对端版本低于本端可支持最小版本 → BYE(版本不匹配), §3.5。 */
        xgc_teardown(self, XGUI_REMOTE_BYE_VERSION,
                     XGUI_REMOTE_ERR_VERSION, true, false);
        return;
    }
    if (d->m_bannerSent) {
        d->m_state = XGUI_REMOTE_STATE_HANDSHAKING;
        xgc_sendHello(self); /* 双方横幅齐 → 我方 HELLO(§3.7)。 */
    }
}

/** @brief 对端横幅收齐且本端横幅已发足 → 发 HELLO。 */
static void xgc_sendHelloIfBannersDone(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (d->m_bannerSent && d->m_bannerGot &&
        d->m_state == XGUI_REMOTE_STATE_BANNER_WAIT) {
        d->m_state = XGUI_REMOTE_STATE_HANDSHAKING;
        xgc_sendHello(self);
    }
}

/** @brief 本端横幅未发足时补发(横幅 8 字节级, TCP 几乎不会部分写)。 */
static void xgc_ensureBannerSent(XGuiClient* self)
{
    XGuiClientPrivate* d;
    uint8_t banner[XGUI_REMOTE_BANNER_BYTES];
    int64_t written;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || !d->m_sessionReady || d->m_bannerSent) return;
    XGuiRemoteProto_makeBanner(banner);
    written = XIODevice_write_1(d->m_dev, (const char*)banner,
                                (int64_t)XGUI_REMOTE_BANNER_BYTES);
    if (written > 0) d->m_stats.bytesSent += (uint64_t)written;
    d->m_bannerSent = (written == (int64_t)XGUI_REMOTE_BANNER_BYTES);
    xgc_sendHelloIfBannersDone(self);
}

/** @brief 设备就绪(明文 connected / TLS 加密完成): 发横幅进入 BANNER_WAIT。 */
static void xgc_sessionDeviceReady(XGuiClient* self)
{
    XGuiClientPrivate* d;
    uint8_t banner[XGUI_REMOTE_BANNER_BYTES];
    int64_t written;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || d->m_sessionReady) return;
    if (!XIODevice_isOpen(d->m_dev)) return;

    /* 注册 poll 回调(无事件循环时句柄为 NULL, 会话无法泵帧——部署形态
     * 限制, 下次连接重试)。 */
    if (!d->m_pollHandle) {
        d->m_pollHandle = XAbstractEventDispatcher_addPollCallback(
            xgc_pollPump, self);
    }
    /* 兜底定时器(基准优化 2026-10-02): 直呼 pollPump, 把空闲事件循环下的
     * 收发延迟钳到 ≤8ms(实测 resource 档收侧 ~170ms 散布的平台唤醒问题,
     * 在 Remote 层内兜底; 会话断开即停, 空转近零成本)。 */
    if (d->m_pollHandle && !d->m_pumpTimer) {
        d->m_pumpTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (d->m_pumpTimer) {
            XTimer_setInterval(d->m_pumpTimer, 8);
            XTimer_setTimerCallback(d->m_pumpTimer, xgc_pumpTimerCb);
            XTimer_setUserData(d->m_pumpTimer, self);
            XTimer_start_base(d->m_pumpTimer);
        }
    }

    d->m_sessionReady = true;
    d->m_state = XGUI_REMOTE_STATE_BANNER_WAIT;
    d->m_lastPeerFrameMs = (uint64_t)xgc_nowMs();
    d->m_lastPingSentMs = d->m_lastPeerFrameMs;
    d->m_fpsWindowStartMs = d->m_lastPeerFrameMs;
    d->m_fpsWindowUpdates = 0;

    XGuiRemoteProto_makeBanner(banner);
    written = XIODevice_write_1(d->m_dev, (const char*)banner,
                                (int64_t)XGUI_REMOTE_BANNER_BYTES);
    if (written > 0) d->m_stats.bytesSent += (uint64_t)written;
    d->m_bannerSent = (written == (int64_t)XGUI_REMOTE_BANNER_BYTES);
    xgc_sendHelloIfBannersDone(self);
}

/* ==================== FB_META / FB_UPDATE(画面合成, §7.2) ==================== */

/* 基准追踪(XGUI_REMOTE_BENCH_TRACE=1 启用; 2026-10-02 延迟基准诊断,
 * stderr 单行, 未启用零成本)。 */
/* [2026-10-03 互联测试指挥官] tile 逐枚踪(XGC_TILE_DEBUG=1 编译期启用):
 * 真机镜像碎片/黑洞定位用, 生产零成本。 */
#ifndef XGC_TILE_DEBUG
#define XGC_TILE_DEBUG 0
#endif

/* 档位变化逐次踪(XGC_PROFILE_DEBUG=1 编译期启用): 真机互联切档取证用,
 * 生产零成本。 */
#ifndef XGC_PROFILE_DEBUG
#define XGC_PROFILE_DEBUG 0
#endif

static int xgc_benchTrace = -1;
static int xgc_benchTraceOn(void)
{
    if (xgc_benchTrace < 0)
        xgc_benchTrace =
            (XSystem_environment("XGUI_REMOTE_BENCH_TRACE") != NULL);
    return xgc_benchTrace;
}

/** @brief 线上格式 → 本地 XImage 格式(1:1 解至本地, 无转换绘制)。 */
static XImageFormat xgc_localImageFormat(uint8_t wireFormat)
{
    if (wireFormat == (uint8_t)XGUI_REMOTE_PF_RGB565) {
        return XImageFormat_RGB16;
    }
    return XImageFormat_ARGB32; /* ARGB32(未知值按基线兜底)。 */
}

/* ==================== 首帧 FB_REQUEST 与视口通告(2026-10-05 方案③④) ==================== */

/**
 * @brief  FB_REQUEST 发送许可: 对端能力交集含 CAP_FB_REQUEST 且未被 A/B
 *         旋钮关闭。XGUI_REMOTE_FB_REQUEST_OFF=1 为度量 B 侧专用(模拟旧
 *         客户端「从不发送」行为), 部署态恒开。
 */
static bool xgc_fbReqAllowed(const XGuiClientPrivate* d)
{
    const char* off;
    if (!d || !(d->m_peerCaps & (uint32_t)XGUI_REMOTE_CAP_FB_REQUEST))
        return false;
    off = XSystem_environment("XGUI_REMOTE_FB_REQUEST_OFF");
    return !(off != NULL && off[0] == '1');
}

/**
 * @brief  计算客户端可见视口(远端画面坐标; 方案④)。
 * @details FIT=整幅可见→(0,0,0,0)(=未通告, 服务端按整幅处理); 1:1=控件
 *          矩形(=远端尺寸)逐级与祖先矩形求交——与页面裁剪(XWidget_paintTree
 *          祖先矩形交集)同界, 结果即镜像可见裁剪窗。完全不可见返回 0 尺寸
 *          (服务端按未通告处理)。
 */
static void xgc_viewportRect(XGuiClient* self, int* ox, int* oy,
                             int* ow, int* oh)
{
    XGuiClientPrivate* d = (XGuiClientPrivate*)self->m_d;
    int x = 0, y = 0, w, h;
    XWidget* p;
    *ox = 0; *oy = 0; *ow = 0; *oh = 0;
    if (d->m_viewFit == XGUI_CLIENT_VIEW_FIT) return; /* 整幅可见。 */
    /* 起点=控件自身当前矩形∩远端尺寸(1:1 setFixedSize 后=远端尺寸;
     * 布局过渡期控件可能尚小于远端——可见域以小者为准)。 */
    w = XWidget_width((XWidget*)self);
    h = XWidget_height((XWidget*)self);
    if (w <= 0 || h <= 0) return;
    if (w > (int)d->m_fbWidth) w = (int)d->m_fbWidth;
    if (h > (int)d->m_fbHeight) h = (int)d->m_fbHeight;
    p = XWidget_parentWidget((XWidget*)self);
    while (p) {
        int pw = XWidget_width(p);
        int ph = XWidget_height(p);
        XWidget* g;
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > pw) w = pw - x;
        if (y + h > ph) h = ph - y;
        if (w <= 0 || h <= 0) return; /* 被祖先完全裁掉。 */
        g = XWidget_parentWidget(p);
        if (g) {
            x += XWidget_x(p);
            y += XWidget_y(p);
        }
        p = g;
    }
    *ox = x; *oy = y; *ow = w; *oh = h;
}

/** @brief 发 FB_REQUEST(mode+视口; 许可判定归调用方)。 */
static void xgc_sendFbRequest(XGuiClient* self, uint8_t mode,
                              int vx, int vy, int vw, int vh)
{
    XGuiClientPrivate* d = (XGuiClientPrivate*)self->m_d;
    XGuiRemoteMsgFbRequest req;
    uint8_t rbuf[16];
    size_t rlen;
    XMemset(&req, 0, sizeof(req));
    req.mode = mode;
    req.x = (uint16_t)xgc_satI16(vx);
    req.y = (uint16_t)xgc_satI16(vy);
    req.w = (uint16_t)xgc_satI16(vw);
    req.h = (uint16_t)xgc_satI16(vh);
    /* 冻结契约: enc* 显式序列化后上网。 */
    rlen = XGuiRemoteProto_encFbRequest(rbuf, sizeof(rbuf), &req);
    if (rlen > 0)
        xgc_sendFrame(self, XGUI_REMOTE_MSG_FB_REQUEST, rbuf, rlen);
    if (d->m_ffOn)
        fprintf(stderr, "XGC_FF fbreq t=%lld mode=%u vp=%d,%d+%dx%d\n",
                (long long)xgc_nowMs(), (unsigned)mode, vx, vy, vw, vh);
}

/** @brief 首帧探针启表(本连接首个 FB_META; env 未开时零开销)。 */
static void xgc_ffStart(XGuiClient* self, int vx, int vy, int vw, int vh)
{
    XGuiClientPrivate* d = (XGuiClientPrivate*)self->m_d;
    int gw, gh, bytes;
    if (d->m_ffMap) {
        XFree_System(d->m_ffMap);
        d->m_ffMap = NULL;
    }
    d->m_ffGot = 0;  d->m_ffTotal = 0;
    d->m_ffVpGot = 0; d->m_ffVpTotal = 0;
    d->m_ffFullPrinted = false;
    d->m_ffVpPrinted = false;
    d->m_ffVpX = vx; d->m_ffVpY = vy; d->m_ffVpW = vw; d->m_ffVpH = vh;
    d->m_ffT0Ms = xgc_nowMs();
    if (d->m_tileWidth <= 0 || d->m_tileHeight <= 0) return;
    gw = ((int)d->m_fbWidth + d->m_tileWidth - 1) / d->m_tileWidth;
    gh = ((int)d->m_fbHeight + d->m_tileHeight - 1) / d->m_tileHeight;
    if (gw <= 0 || gh <= 0) return;
    d->m_ffGridW = gw;
    d->m_ffGridH = gh;
    d->m_ffTotal = gw * gh;
    bytes = (d->m_ffTotal + 7) / 8;
    d->m_ffMap = (uint8_t*)XMalloc_System((size_t)bytes);
    if (d->m_ffMap) XMemset(d->m_ffMap, 0, (size_t)bytes);
    if (vw > 0 && vh > 0) {
        int gx0 = vx / d->m_tileWidth, gy0 = vy / d->m_tileHeight;
        int gx1 = (vx + vw - 1) / d->m_tileWidth;
        int gy1 = (vy + vh - 1) / d->m_tileHeight;
        if (gx0 < 0) gx0 = 0;
        if (gy0 < 0) gy0 = 0;
        if (gx1 >= gw) gx1 = gw - 1;
        if (gy1 >= gh) gy1 = gh - 1;
        if (gx1 >= gx0 && gy1 >= gy0)
            d->m_ffVpTotal = (gx1 - gx0 + 1) * (gy1 - gy0 + 1);
    }
    fprintf(stderr,
            "XGC_FF start t=%lld grid=%dx%d total=%d vp=%d,%d+%dx%d "
            "vpTotal=%d\n",
            (long long)d->m_ffT0Ms, gw, gh, d->m_ffTotal,
            vx, vy, vw, vh, d->m_ffVpTotal);
}

/** @brief 首帧探针记 tile(解码成功后; 覆盖齐/视口齐各打印一次)。 */
static void xgc_ffMarkTile(XGuiClient* self, int tx, int ty, int tw, int th)
{
    XGuiClientPrivate* d = (XGuiClientPrivate*)self->m_d;
    int gx0, gy0, gx1, gy1, gx, gy;
    if (!d->m_ffOn || !d->m_ffMap || d->m_ffTotal <= 0) return;
    gx0 = tx / d->m_tileWidth;
    gy0 = ty / d->m_tileHeight;
    gx1 = (tx + tw - 1) / d->m_tileWidth;
    gy1 = (ty + th - 1) / d->m_tileHeight;
    if (gx1 >= d->m_ffGridW) gx1 = d->m_ffGridW - 1;
    if (gy1 >= d->m_ffGridH) gy1 = d->m_ffGridH - 1;
    for (gy = gy0; gy <= gy1; ++gy) {
        for (gx = gx0; gx <= gx1; ++gx) {
            int i = gy * d->m_ffGridW + gx;
            bool inVp;
            if (i < 0 || i >= d->m_ffTotal) continue;
            if (d->m_ffMap[i >> 3] & (1u << (i & 7))) continue;
            d->m_ffMap[i >> 3] |= (uint8_t)(1u << (i & 7));
            ++d->m_ffGot;
            inVp = d->m_ffVpTotal > 0 &&
                   gx * d->m_tileWidth < d->m_ffVpX + d->m_ffVpW &&
                   d->m_ffVpX < gx * d->m_tileWidth + d->m_tileWidth &&
                   gy * d->m_tileHeight < d->m_ffVpY + d->m_ffVpH &&
                   d->m_ffVpY < gy * d->m_tileHeight + d->m_tileHeight;
            if (inVp) ++d->m_ffVpGot;
        }
    }
    if (!d->m_ffFullPrinted && d->m_ffGot >= d->m_ffTotal) {
        d->m_ffFullPrinted = true;
        fprintf(stderr, "XGC_FF full t=%lld ms=%lld tiles=%d/%d\n",
                (long long)xgc_nowMs(),
                (long long)(xgc_nowMs() - d->m_ffT0Ms),
                d->m_ffGot, d->m_ffTotal);
    }
    if (!d->m_ffVpPrinted && d->m_ffVpTotal > 0 &&
        d->m_ffVpGot >= d->m_ffVpTotal) {
        d->m_ffVpPrinted = true;
        fprintf(stderr, "XGC_FF vpfull t=%lld ms=%lld tiles=%d/%d\n",
                (long long)xgc_nowMs(),
                (long long)(xgc_nowMs() - d->m_ffT0Ms),
                d->m_ffVpGot, d->m_ffVpTotal);
    }
}

/** @brief 首帧探针收尾(teardown; 未齐打印 partial 供断链归因)。 */
static void xgc_ffReset(XGuiClientPrivate* d, const char* why)
{
    if (d->m_ffOn && d->m_ffMap && !d->m_ffFullPrinted)
        fprintf(stderr, "XGC_FF partial why=%s got=%d/%d ms=%lld\n",
                why, d->m_ffGot, d->m_ffTotal,
                (long long)(xgc_nowMs() - d->m_ffT0Ms));
    if (d->m_ffMap) {
        XFree_System(d->m_ffMap);
        d->m_ffMap = NULL;
    }
    d->m_ffGot = 0;
    d->m_ffTotal = 0;
    d->m_ffVpGot = 0;
    d->m_ffVpTotal = 0;
    d->m_ffGridW = 0;
    d->m_ffGridH = 0;
    d->m_ffFullPrinted = false;
    d->m_ffVpPrinted = false;
}

/** @brief 线上格式像素字节数。 */
static int xgc_formatBpp(uint8_t wireFormat)
{
    return (wireFormat == (uint8_t)XGUI_REMOTE_PF_RGB565) ? 2 : 4;
}

/** @brief FB_META: 重建 backbuffer + setFixedSize(1:1) + 进入 Streaming。 */
static void xgc_handleFbMeta(XGuiClient* self, const XGuiRemoteMsgFbMeta* meta)
{
    XGuiClientPrivate* d;
    bool sizeChanged;
    if (!self || !self->m_d || !meta) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (meta->width == 0 || meta->height == 0) return; /* 非法元信息忽略。 */

    /* 重建条件须含线上格式变化(档位热切换 performance↔resource 同尺寸
     * 换格式, XGuiRemote.md §5.3): 只比尺寸会把 RGB565 tile 以 2B/px
     * 写进残留的 ARGB32(4B/px) backbuffer——水平 2:1 压缩+色道错乱
     * (E2E 资源档换档实测复现, 2026-10-02)。 */
    sizeChanged = !d->m_fbValid || (meta->width != d->m_fbWidth) ||
                  (meta->height != d->m_fbHeight) || XImage_isNull(&d->m_fb) ||
                  (XImage_format(&d->m_fb) !=
                   xgc_localImageFormat(meta->format));
    /* 档位变化诊断(仅变化时一行, 互联测试切档断言的 grep 证据锚;
     * FB_META 宣告档位 = 服务端协商生效的端到端事实, 2026-10-03)。 */
#if defined(XGC_PROFILE_DEBUG) && XGC_PROFILE_DEBUG
    if (meta->profileId != d->m_profileId) {
        fprintf(stderr, "XGuiClient: fb-meta profile-id=%u\n",
                (unsigned)meta->profileId);
    }
#endif
    d->m_fbWidth = meta->width;
    d->m_fbHeight = meta->height;
    d->m_fbFormat = meta->format;
    d->m_tileWidth = meta->tileWidth;
    d->m_tileHeight = meta->tileHeight;
    d->m_profileId = meta->profileId;

    /* 标题(flags bit0 有效)。 */
    if (meta->flags & 0x01u) {
        int n = meta->titleBytes;
        if (n > XGUI_REMOTE_MAX_NAME_BYTES) n = XGUI_REMOTE_MAX_NAME_BYTES;
        if (n > 0) memcpy(d->m_title, meta->title, (size_t)n);
        d->m_title[n] = '\0';
        d->m_titleBytes = n;
    } else {
        d->m_title[0] = '\0';
        d->m_titleBytes = 0;
    }

    /* 档位跟随(§5.2: 双方各自持有副本; FB_META 宣告档位, 本端本地参数
     * 跟随预设——移动合并窗口/保活间隔/写出预算随之生效; CUSTOM 保留现值;
     * 其余未知档回退 resource——向后兼容确定性退化, 协议新增档 id 时本端
     * 不断会话且副参数落到保守预设, 设计稿 out/perf8/
     * latency-profile-design.md §4)。 */
    if (d->m_profileId == (uint8_t)XGUI_REMOTE_PROFILE_PERFORMANCE) {
        XGuiRemoteProfile_initPerformance(&d->m_profile);
    } else if (d->m_profileId == (uint8_t)XGUI_REMOTE_PROFILE_RESOURCE ||
               d->m_profileId == (uint8_t)XGUI_REMOTE_PROFILE_AUTO) {
        XGuiRemoteProfile_initResource(&d->m_profile);
    } else if (d->m_profileId == (uint8_t)XGUI_REMOTE_PROFILE_LATENCY) {
        XGuiRemoteProfile_initLatency(&d->m_profile); /* 低延迟档(2026-10-05)。 */
    } else if (d->m_profileId != (uint8_t)XGUI_REMOTE_PROFILE_CUSTOM) {
        XGuiRemoteProfile_initResource(&d->m_profile); /* 未知档兜底(冻结)。 */
    }
    XGuiRemoteProfile_sanitize(&d->m_profile);

    if (sizeChanged) {
        /* [perf9 路5] fb 重建(尺寸/格式/重连): 缩放缓存作废——缓存内容
         * 属旧画面(重连同几何时防止陈旧帧顶替占位底色)。 */
        xgc_scaledCacheReset(d);
        if (!XImage_reinit_ex(&d->m_fb, (int)d->m_fbWidth, (int)d->m_fbHeight,
                              xgc_localImageFormat(d->m_fbFormat))) {
            d->m_fbValid = false; /* 分配失败: 下批 META 重试。 */
            return;
        }
        /* [perf9 路2 任务4] 未交付区占位: backbuffer 重建底色=深灰
         * (env 门控, 缺省开)。历史语义=零值黑——交付崩塌/换页补齐期
         * 未交付 tile 区呈纯黑, 与画面内容黑不可分, 用户观感「大面
         * 黑+白块碎片」; 深灰占位使「未交付」可辨且不刺眼。整幅底
         * 即占位, 已交付 tile 照常直写覆盖。 */
        if (xgc_placeholderOn()) {
            uint8_t rgb[3] = { XGC_PLACEHOLDER_RGB };
            int yy, ww = XImage_width(&d->m_fb);
            int hh = XImage_height(&d->m_fb);
            int bpl = XImage_bytesPerLine(&d->m_fb);
            bool rgb16 =
                (XImage_format(&d->m_fb) == XImageFormat_RGB16);
            for (yy = 0; yy < hh; ++yy) {
                uint8_t* row = XImage_bits(&d->m_fb) +
                               (size_t)yy * (size_t)bpl;
                int xx;
                for (xx = 0; xx < ww; ++xx) {
                    if (rgb16) {
                        unsigned v = (unsigned)(((rgb[0] >> 3) << 11) |
                                                ((rgb[1] >> 2) << 5) |
                                                (rgb[2] >> 3));
                        row[xx * 2] = (uint8_t)(v & 0xFF);
                        row[xx * 2 + 1] = (uint8_t)(v >> 8);
                    } else {
                        row[xx * 4 + 0] = rgb[2];
                        row[xx * 4 + 1] = rgb[1];
                        row[xx * 4 + 2] = rgb[0];
                        row[xx * 4 + 3] = 0xFF;
                    }
                }
            }
        }
    }
    d->m_fbValid = true;
    /* 提示基线: 本 META 后首 tile 到达前可绘「正在接收画面…」。 */
    d->m_hintBaselineTiles = d->m_stats.tileCount;

    /* 控件几何按模式: 1:1 沿用 setFixedSize(远端尺寸)(§7.2 V1 口径);
     * FIT 不改宿主给定几何, 按当前控件尺寸重算信箱变换(2026-10-04
     * 视图适配缩放)。两模式均整控件置脏——1:1 旧行为=尺寸变化自带
     * 重绘, FIT 补显式置脏防首帧残缺。 */
    if (d->m_viewFit == XGUI_CLIENT_VIEW_1TO1) {
        XWidget_setFixedSize((XWidget*)self, (int)d->m_fbWidth,
                             (int)d->m_fbHeight);
    }
    xgc_updateViewTransform(self);
    /* FB_META/几何变化: 旧几何域的待上屏损伤一并作废(整幅置脏覆盖)。 */
    d->m_presentFirstMs = 0;
    d->m_presentLastMs = 0;
    d->m_presentFirstUs = 0;
    xgc_invalidateWhole(self);

    if (d->m_state != XGUI_REMOTE_STATE_STREAMING) {
        d->m_state = XGUI_REMOTE_STATE_STREAMING;
        d->m_reconnectAttempts = 0; /* 连接达成, 退避计数复位。 */
        xgc_emit(self, (size_t)XGuiClient_connected_signal, NULL);
    }
    xgc_emit(self, (size_t)XGuiClient_remoteMetaChanged_signal, NULL);

    /* 全量刷新(§3.7 方案③ 2026-10-05 定版): 本连接首个 FB_META 且能力
     * 交集含 CAP_FB_REQUEST 时自动发 FB_REQUEST(mode=1, 搭车视口通告)
     * ——根修「接入后静态 tile 从不补发/黑块直到换页」与「新连接长时间
     * 无 tile」停滞; 发过即止(后续靠脏 tile), 重连=新连接自然重发;
     * 老服务端(未宣告该能力位)永不会收到, 向后兼容不变。档位热切换的
     * 重发 FB_META 不再触发请求(服务端换档代际自含全量语义)。
     * 度量旋钮 XGUI_REMOTE_FB_REQUEST_OFF=1 关闭发送(A 侧对照)。 */
    if (!d->m_fbReqSent) {
        d->m_fbReqSent = true;
        if (xgc_fbReqAllowed(d)) {
            int vx, vy, vw, vh;
            xgc_viewportRect(self, &vx, &vy, &vw, &vh);
            xgc_sendFbRequest(self, 1, vx, vy, vw, vh);
            d->m_viewRepX = (int16_t)vx;
            d->m_viewRepY = (int16_t)vy;
            d->m_viewRepW = (int16_t)vw;
            d->m_viewRepH = (int16_t)vh;
            d->m_viewRepMs = xgc_nowMs();
            if (d->m_ffOn) xgc_ffStart(self, vx, vy, vw, vh);
        } else if (d->m_ffOn) {
            /* B 侧度量: 探针照常启表, 记录无请求时 tile 到达情况。 */
            xgc_ffStart(self, 0, 0, 0, 0);
        }
    }
}

/** @brief 单 tile 解码直写 backbuffer(带 stride), 成功并入损伤区。 */
static void xgc_decodeTile(XGuiClient* self, const XGuiRemoteMsgFbTile* tile,
                           uint8_t wireFormat)
{
    XGuiClientPrivate* d;
    int bpp = xgc_formatBpp(wireFormat);
    int tx;
    int ty;
    int tw;
    int th;
    int stride;
    uint8_t* row;
    XRect r;
    if (!self || !self->m_d || !tile) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (XImage_isNull(&d->m_fb) || bpp == 0) return;

    tx = tile->x;
    ty = tile->y;
    tw = tile->w;
    th = tile->h;
    /* tile 裁剪进远端画面边界(越界 tile 跳过, 不作协议错误断链)。 */
    if (tx >= (int)d->m_fbWidth || ty >= (int)d->m_fbHeight) return;
    if (tx + tw > (int)d->m_fbWidth) tw = (int)d->m_fbWidth - tx;
    if (ty + th > (int)d->m_fbHeight) th = (int)d->m_fbHeight - ty;
    if (tw <= 0 || th <= 0) return;

    row = XImage_scanLine(&d->m_fb, ty);
    if (!row) return;
    stride = XImage_bytesPerLine(&d->m_fb);
    {
        int rc = XGuiRemoteCodec_decodeTile(tile->payload,
                                            tile->payloadBytes,
                                            (XGuiRemoteCodecId)tile->codec,
                                            (XGuiRemotePixelFormat)wireFormat,
                                            row + (size_t)tx * (size_t)bpp,
                                            stride,
                                            (XGuiRemotePixelFormat)wireFormat,
                                            tw, th);
        if (rc != 0) {
            return; /* 解码失败(截断/编解码器未编译): 跳过该 tile。 */
        }
    }
    XRect_init(&r, tx, ty, tw, th);
    XRegion_addRect(&d->m_damage, &r);
    xgc_ffMarkTile(self, tx, ty, tw, th); /* 首帧探针(关=零成本)。 */
}

/** @brief FB_UPDATE: 帧级头 + 逐 tile(偏移自 FB_UPDATE_HEADER_BYTES)。 */
static void xgc_handleFbUpdate(XGuiClient* self, const uint8_t* payload,
                               size_t len)
{
    XGuiClientPrivate* d;
    XGuiRemoteMsgFbUpdate upd;
    size_t offset = XGUI_REMOTE_FB_UPDATE_HEADER_BYTES;
    uint16_t i;
    uint64_t stageT0 = XGuiRemoteUdp_stageProfNowUs(); /* [stage] 关=0。 */
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (len < XGUI_REMOTE_FB_UPDATE_HEADER_BYTES) {
        xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                     XGUI_REMOTE_ERR_PROTOCOL, true, false);
        return;
    }
    if (!XGuiRemoteProto_decFbUpdate(payload, len, &upd)) {
        xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                     XGUI_REMOTE_ERR_PROTOCOL, true, false);
        return;
    }
    if (upd.tileCount > 0 && !d->m_fbValid) {
        return; /* 元信息未就绪: 陈旧几何无意义, 丢帧等 META。 */
    }
    if (upd.tileCount > 0 && d->m_fbValid &&
        (uint8_t)upd.format != d->m_fbFormat) {
        /* [perf8 路 B 修复(2026-10-05)] 跨通道乱序防御: FB_UPDATE 走 UDP
         * 旁路(最新帧优先)而 FB_META 走 TCP, 换档瞬间新格式 tile 可能
         * 先于 META 抵达——按旧格式缓冲解码=convertPixels 越界写
         * (ASAN 实证 heap-buffer-overflow, latency→performance 热切换
         * 复现)。格式不符整帧丢弃, META 后的 FB_REQUEST 全量刷新兜底
         * (§3.7), 画面自收敛。 */
        return;
    }

    for (i = 0; i < upd.tileCount; ++i) {
        XGuiRemoteMsgFbTile tile;
        size_t next = 0;
        if (!XGuiRemoteProto_decFbTile(payload, len, offset, &tile, &next)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        xgc_decodeTile(self, &tile, upd.format);
#if defined(XGC_TILE_DEBUG) && XGC_TILE_DEBUG
        if (d->m_stats.tileCount < 4000u)
            fprintf(stderr, "XGC_TILE #%u t=(%u,%u %ux%u) fmt=%u len=%u\n",
                    (unsigned)d->m_stats.tileCount,
                    (unsigned)tile.x, (unsigned)tile.y,
                    (unsigned)tile.w, (unsigned)tile.h,
                    (unsigned)upd.format, (unsigned)tile.payloadBytes);
#endif
        offset = next;
        ++d->m_stats.tileCount;
    }
    ++d->m_stats.updateCount;
    /* [stage] cli-dec=单帧 FB_UPDATE 消息处理时长(帧头解析+逐 tile 解码
     * 直写 backbuffer+损伤并入; 收侧通道派发前段由 [wake] 行承载);
     * 字节列=本帧线载字节(与 srv-send 发侧对账)。 */
    if (stageT0) XGuiRemoteUdp_stageProfSample("cli-dec", stageT0);
    if (stageT0 && len <= 0xFFFFFFFFull)
        XGuiRemoteUdp_stageProfBytes("cli-dec", (uint32_t)len);

    /* fpsMilli 滑动窗口估计(§7.5)。 */
    {
        int64_t now = xgc_nowMs();
        int64_t elapsed = now - (int64_t)d->m_fpsWindowStartMs;
        ++d->m_fpsWindowUpdates;
        if (elapsed >= (int64_t)XGC_FPS_WINDOW_MS) {
            d->m_stats.fpsMilli = (uint32_t)
                ((uint64_t)d->m_fpsWindowUpdates * 1000u /
                 (uint64_t)(elapsed > 0 ? elapsed : 1));
            d->m_fpsWindowStartMs = (uint64_t)now;
            d->m_fpsWindowUpdates = 0;
        }
    }

    /* 损伤合并上屏(XGC_PRESENT_QUIET_MS): 此处只登记时刻, 由 poll 泵按
     * 「静默 6ms 或最老损伤 20ms」统一刷——突发期每帧即刷会让 GUI 圈
     * 被近平幅 paint 钳到 ~25ms/圈, 收侧套接字排队排空。解码仍逐帧同步进
     * backbuffer, 仅上屏时机合并。
     * [perf9 路5 任务2 首伤即现 2026-10-05] 待上屏损伤为空(本批=突发/
     * 稳态首块)时登记时刻后立即就地 present——首现延迟砍掉窗等待
     * (6~20ms→~0); 突发的后续帧 m_presentFirstMs!=0, 仍由泵圈按窗合并
     * (突发只多上屏 1-3 次的原语义不变)。env
     * XGUI_REMOTE_PRESENT_IMMEDIATE=0 整体回退旧行为。 */
    {
        int64_t now = xgc_nowMs();
        if (d->m_presentFirstMs == 0)
            d->m_presentFirstUs = XGuiRemoteUdp_stageProfNowUs();
        if (d->m_presentFirstMs == 0) d->m_presentFirstMs = now;
        d->m_presentLastMs = now;
        if (xgc_presentImmediateOn() && d->m_presentFirstMs == now) {
            xgc_presentDamage(self, d);
            d->m_presentFirstMs = 0;
            d->m_presentLastMs = 0;
            d->m_presentFirstUs = 0;
        }
    }
    if (xgc_benchTraceOn())
        fprintf(stderr, "XGC_TRACE fbup t=%llu tiles=%u\n",
                (unsigned long long)(uint64_t)xgc_nowMs(),
                (unsigned)upd.tileCount);
}

/* ==================== 认证(SHA256_CHALLENGE, §3.8) ==================== */

/** @brief 发 AUTH_RESPONSE: SHA256(SHA256(口令) || nonce)(计算内核在
 *         XGuiRemoteAuth_clientComputeResponse, 与服务端同式)。 */
static void xgc_sendAuthResponse(XGuiClient* self)
{
    XGuiClientPrivate* d;
    XGuiRemoteMsgAuthResponse resp;
    size_t nonceBytes;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_challengeValid) return;

    XMemset(&resp, 0, sizeof(resp));
    nonceBytes = d->m_challenge.nonceBytes;
    if (nonceBytes > XGUI_REMOTE_AUTH_NONCE_BYTES) {
        nonceBytes = XGUI_REMOTE_AUTH_NONCE_BYTES;
    }
    if (!XGuiRemoteAuth_clientComputeResponse(d->m_password,
                                              d->m_challenge.nonce,
                                              nonceBytes, resp.response)) {
        return; /* 哈希失败。 */
    }
    resp.responseBytes = XGUI_REMOTE_AUTH_RESPONSE_BYTES;
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
    {
        uint8_t abuf[XGUI_REMOTE_AUTH_RESPONSE_BYTES + 8];
        size_t alen = XGuiRemoteProto_encAuthResponse(abuf, sizeof(abuf),
                                                      &resp);
        if (alen > 0)
            xgc_sendFrame(self, XGUI_REMOTE_MSG_AUTH_RESPONSE, abuf, alen);
    }
}

/** @brief 认证/握手收尾: 口令即焚(冻结头: 明文仅驻留至认证完成)。 */
static void xgc_handshakeDone(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    XMemset(d->m_password, 0, sizeof(d->m_password));
    d->m_challengeValid = false;
    /* Streaming 状态等 FB_META 宣告画面参数后才进入(冻结头 connected
     * 信号口径: "首帧 FB_META 已收到"); 此处仅清认证在途态。 */
}

/* ==================== 帧分发(状态机) ==================== */

/** @brief 按会话状态分发一帧(未知类型跳过: 前向兼容, §3.4)。 */
static void xgc_dispatchFrame(XGuiClient* self, XGuiRemoteMsgType type,
                              const uint8_t* payload, size_t len)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;

    switch (type) {
    case XGUI_REMOTE_MSG_HELLO_ACK: {
        XGuiRemoteMsgHello ack;
        if (d->m_state != XGUI_REMOTE_STATE_HANDSHAKING) return;
        if (!XGuiRemoteProto_decHello(payload, len, &ack)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        if (ack.protocolVersion < XGUI_REMOTE_PROTOCOL_VERSION) {
            xgc_teardown(self, XGUI_REMOTE_BYE_VERSION,
                         XGUI_REMOTE_ERR_VERSION, true, false);
            return;
        }
        /* 版本协商取 min(双方版本)(§3.5); 本端恒支持版本 1。 */
        d->m_negotiatedVersion =
            ack.protocolVersion < XGUI_REMOTE_PROTOCOL_VERSION
                ? ack.protocolVersion : (uint16_t)XGUI_REMOTE_PROTOCOL_VERSION;
        d->m_peerCaps = ack.capabilities & d->m_localCaps;
        if ((XGuiRemoteAuthMethod)ack.authMethod ==
            XGUI_REMOTE_AUTH_NONE) {
            xgc_handshakeDone(self); /* 无认证: 等 FB_META。 */
        } else if ((XGuiRemoteAuthMethod)ack.authMethod ==
                   XGUI_REMOTE_AUTH_SHA256_CHALLENGE) {
            d->m_authMethod = XGUI_REMOTE_AUTH_SHA256_CHALLENGE;
            if (d->m_password[0] == '\0') {
                /* [2026-10-04 访问口令] 服务端要求认证而本端无口令:
                 * 快速失败断链, 不发送必错应答(连接流程自动适配;
                 * 信号口径与被服务端拒绝一致 BYE/ERR AUTH)。 */
                xgc_teardown(self, XGUI_REMOTE_BYE_AUTH_FAILED,
                             XGUI_REMOTE_ERR_AUTH, true, false);
                return;
            }
            d->m_state = XGUI_REMOTE_STATE_AUTHENTICATING;
        } else {
            /* 认证法为封闭枚举, 未知值=协议错误(§3.4 冻结口径)。 */
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
        }
        return;
    }
    case XGUI_REMOTE_MSG_AUTH_CHALLENGE: {
        if (d->m_state != XGUI_REMOTE_STATE_AUTHENTICATING) return;
        if (!XGuiRemoteProto_decAuthChallenge(payload, len, &d->m_challenge)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        d->m_challengeValid = true;
        xgc_sendAuthResponse(self);
        return;
    }
    case XGUI_REMOTE_MSG_AUTH_RESULT: {
        XGuiRemoteMsgAuthResult result;
        if (d->m_state != XGUI_REMOTE_STATE_AUTHENTICATING) return;
        if (!XGuiRemoteProto_decAuthResult(payload, len, &result)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        if (result.ok == 1) {
            xgc_handshakeDone(self);
        } else {
            /* 认证失败 → BYE(认证失败) → 断链(重连后仍需口令则再认证)。 */
            xgc_teardown(self, XGUI_REMOTE_BYE_AUTH_FAILED,
                         XGUI_REMOTE_ERR_AUTH, true, false);
        }
        return;
    }
    case XGUI_REMOTE_MSG_FB_META: {
        XGuiRemoteMsgFbMeta meta;
        if (!XGuiRemoteProto_decFbMeta(payload, len, &meta)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        xgc_handleFbMeta(self, &meta);
        return;
    }
    case XGUI_REMOTE_MSG_FB_UPDATE: {
        xgc_handleFbUpdate(self, payload, len);
        return;
    }
    case XGUI_REMOTE_MSG_PROFILE_RESULT: {
        XGuiRemoteMsgProfileResult result;
        if (!XGuiRemoteProto_decProfileResult(payload, len, &result)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        /* 接受与否均以远端 FB_META(新参数广播)为准(§5.3); 此处无状态。 */
        (void)result;
        return;
    }
    case XGUI_REMOTE_MSG_PING: {
        uint64_t ts = 0;
        if (XGuiRemoteProto_decPing(payload, len, &ts)) {
            /* PONG 回显对端时刻(双向保活, §3.4); 冻结契约: enc* 显式
             * 小端序列化后上网(修复: 结构体直发在大端机字节序颠倒)。 */
            uint8_t pbuf[8];
            size_t plen = XGuiRemoteProto_encPing(pbuf, sizeof(pbuf), ts);
            if (plen > 0)
                xgc_sendFrame(self, XGUI_REMOTE_MSG_PONG, pbuf, plen);
        }
        return;
    }
    case XGUI_REMOTE_MSG_PONG: {
        uint64_t ts = 0;
        if (XGuiRemoteProto_decPing(payload, len, &ts) && d->m_pingAwait &&
            ts == d->m_pingSentTs) {
            int64_t rtt = xgc_nowMs() - (int64_t)ts;
            d->m_stats.rttMs = rtt > 0 ? (uint32_t)rtt : 0;
            d->m_pingAwait = false;
        }
        return;
    }
    case XGUI_REMOTE_MSG_UDP_OFFER: {
        /* UDP 旁路报价(2026-10-04, 设计稿 §3): 预备套接字(bind 即在
         * XNetIoRing 挂 RECV) → TCP 自报 UDP_BIND(token+本地口)。 */
        XGuiRemoteMsgUdpOffer offer;
        if (!XGuiRemoteProto_decUdpOffer(payload, len, &offer)) return;
        if (!d->m_udpWanted || offer.udpPort == 0 || offer.sessionToken == 0)
            return;
        d->m_udpToken = offer.sessionToken;
        if (!d->m_udp) {
            XGuiRemoteUdpChannelConfig cfg;
            XObject* sockObj;
            XMemset(&cfg, 0, sizeof(cfg));
            cfg.role = XGUI_REMOTE_UDP_ROLE_CLIENT;
            cfg.user = self;
            cfg.onRecv = xgc_udpRecv;
            cfg.onStateChanged = xgc_udpStateChanged;
            d->m_udp = XGuiRemoteUdpChannel_create_ex(&cfg);
            if (!d->m_udp) return;
            sockObj = XGuiRemoteUdpChannel_socketObject(d->m_udp);
            if (sockObj) {
                (void)XObject_connect_1(sockObj,
                                        (size_t)XIODevice_readyRead_signal,
                                        (XObject*)self, xgc_udpReadyRead,
                                        XConnectionType_Direct);
            }
        }
        if (XGuiRemoteUdpChannel_clientPrepare(d->m_udp, d->m_host,
                                               offer.udpPort)) {
            XGuiRemoteMsgUdpBind bind;
            uint8_t bbuf[8];
            size_t blen;
            XGuiRemoteUdpChannel_bindSession(d->m_udp, offer.sessionToken,
                                             NULL, 0);
            XMemset(&bind, 0, sizeof(bind));
            bind.sessionToken = offer.sessionToken;
            bind.clientUdpPort = XGuiRemoteUdpChannel_localPort(d->m_udp);
            blen = XGuiRemoteProto_encUdpBind(bbuf, sizeof(bbuf), &bind);
            if (blen > 0)
                xgc_sendFrame(self, XGUI_REMOTE_MSG_UDP_BIND, bbuf, blen);
            d->m_udpState = (int)XGUI_REMOTE_UDP_STATE_TRYING;
            /* 建链在途冷却: BIND→RESULT 往返期内不重发 MODE(1)(防重复
             * OFFER/重绑竞态, 2026-10-04 稳态实测根修)。 */
            d->m_udpRetryMs = xgc_nowMs() + 5000;
            if (xgc_benchTraceOn())
                fprintf(stderr, "XGuiClient: udp bind sent server=%s:%u "
                        "local=%u\n", d->m_host, (unsigned)offer.udpPort,
                        (unsigned)bind.clientUdpPort);
        }
        return;
    }
    case XGUI_REMOTE_MSG_UDP_RESULT: {
        /* 建链结果: active=1 数据面激活(帧+输入走 UDP); 0=退回 TCP。 */
        XGuiRemoteMsgUdpResult result;
        if (!XGuiRemoteProto_decUdpResult(payload, len, &result)) return;
        if (!d->m_udp) return;
        if (xgc_benchTraceOn())
            fprintf(stderr, "XGuiClient: udp result active=%u\n",
                    (unsigned)result.active);
        if (result.active == 1) {
            XGuiRemoteUdpChannel_setActive(d->m_udp, true);
            d->m_udpLastFrameMs = xgc_nowMs();
        } else {
            XGuiRemoteUdpChannel_setActive(d->m_udp, false);
        }
        return;
    }
    case XGUI_REMOTE_MSG_BYE: {
        XGuiRemoteMsgBye bye;
        if (!XGuiRemoteProto_decBye(payload, len, &bye)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        /* 对端优雅断开: 不补发 BYE(否则双 BYE)。 */
        xgc_teardown(self, (XGuiRemoteByeReason)bye.reason,
                     XGUI_REMOTE_ERR_NONE, false, false);
        return;
    }
    case XGUI_REMOTE_MSG_ERROR: {
        XGuiRemoteMsgError err;
        if (!XGuiRemoteProto_decError(payload, len, &err)) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
        xgc_emitError(self, (int)err.code);
        return;
    }
    default:
        return; /* 未知名节/类型: 帧层前向兼容跳过(§3.4)。 */
    }
}

/* ==================== poll 帧泵(GUI 线程, §4.4) ==================== */

/* ---- UDP 旁路回调挂接(2026-10-04, 设计稿 §7) ---- */

/** @brief 通道业务负载回调: FRAME 帧进既有状态机分发(INPUT 不会到客户端)。 */
static void xgc_udpRecv(void* user, uint8_t channel, uint8_t msgType,
                        uint32_t seq, const uint8_t* payload, size_t len)
{
    XGuiClient* self = (XGuiClient*)user;
    XGuiClientPrivate* d;
    (void)seq;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (channel != XGUI_REMOTE_UDP_CHANNEL_FRAME) return;
    d->m_udpLastFrameMs = xgc_nowMs();
    {
        /* [wake] 段③载体戳: 通道读完成→解码入队完成(探针关闭=0 跳过)。 */
        uint64_t arriveUs = XGuiRemoteUdp_wakeProfOn()
                                ? XGuiRemoteUdpChannel_probeArrivalUs(d->m_udp)
                                : 0;
        xgc_dispatchFrame(self, (XGuiRemoteMsgType)msgType, payload, len);
        XGuiRemoteUdp_wakeProfSample("cli-udp", arriveUs);
    }
}

/** @brief 通道状态迁移: 镜像 + 诊断行(stderr, 联调 grep 锚)。 */
static void xgc_udpStateChanged(void* user, int state)
{
    XGuiClient* self = (XGuiClient*)user;
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (d->m_udpState == state) return;
    d->m_udpState = state;
    if (xgc_benchTraceOn())
        fprintf(stderr, "XGuiClient: udp state=%d\n", state);
}

/** @brief 通道 readyRead 直连槽: 数据报到达即排空(XNetIoRing 事件驱动)。 */
static void xgc_udpReadyRead(XObject* receiver, XVarList* args)
{
    XGuiClient* self = (XGuiClient*)receiver;
    XGuiClientPrivate* d;
    (void)args;
    if (!self || !self->m_d || !((XGuiClientPrivate*)self->m_d)->m_udp) return;
    d = (XGuiClientPrivate*)self->m_d;
    XGuiRemoteUdpChannel_drain(d->m_udp, xgc_nowMs());
}

/** @brief 读泵: 横幅攒字节 → 帧泵 → 状态机分发(单圈限预算)。 */
static void xgc_pumpRead(XGuiClient* self)
{
    XGuiClientPrivate* d;
    char buf[XGC_READ_CHUNK];
    size_t budget = XGC_READ_BUDGET_PER_POLL;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || !d->m_sessionReady) return;

    while (budget > 0) {
        int64_t avail = XIODevice_bytesAvailable_base(d->m_dev);
        int64_t want;
        int64_t got;
        const uint8_t* p;
        size_t n;
        int r;
        if (avail <= 0) break;
        want = avail < (int64_t)XGC_READ_CHUNK ? avail
                                               : (int64_t)XGC_READ_CHUNK;
        if ((size_t)want > budget) want = (int64_t)budget;
        got = XIODevice_read_1(d->m_dev, buf, want);
        if (got <= 0) break;
        d->m_stats.bytesReceived += (uint64_t)got;
        d->m_lastPeerFrameMs = (uint64_t)xgc_nowMs();
        budget -= (size_t)got;

        p = (const uint8_t*)buf;
        n = (size_t)got;
        /* 横幅攒字节(裸 8 字节, 非长度前缀帧)。 */
        while (!d->m_bannerGot && n > 0) {
            size_t need = (size_t)(XGUI_REMOTE_BANNER_BYTES -
                                   d->m_bannerGotBytes);
            size_t take = n < need ? n : need;
            memcpy(d->m_bannerBuf + d->m_bannerGotBytes, p, take);
            d->m_bannerGotBytes += (int)take;
            p += take;
            n -= take;
            if (d->m_bannerGotBytes == XGUI_REMOTE_BANNER_BYTES) {
                xgc_bannerComplete(self);
                if (!d->m_dev) return; /* 校验失败已断链。 */
            }
        }
        if (n == 0) continue;

        /* 帧泵: 凑满一帧交一帧(冻结头典型用法)。 */
        {
            /* [wake] 段③载体戳: TCP 设备最近一次读完成(探针关闭/借用设备
             * 无环登记=0 跳过; 多段凑帧取末段到达——段间组装属传输分片
             * 不计入)。 */
            uint64_t arriveUs = XGuiRemoteUdp_wakeProfOn()
                                    ? XAbstractNetIoRing_profLastRecvUs(
                                          (const void*)d->m_dev)
                                    : 0;
            r = XGuiRemoteFrameReader_feed(&d->m_reader, p, n);
            while (r > 0) {
                if (xgc_benchTraceOn() &&
                    d->m_reader.type == XGUI_REMOTE_MSG_FB_UPDATE)
                    fprintf(stderr, "XGC_TRACE recv t=%llu bytes=%zu\n",
                            (unsigned long long)(uint64_t)xgc_nowMs(),
                            d->m_reader.payloadLen + 5u);
                xgc_dispatchFrame(self, d->m_reader.type, d->m_reader.payload,
                                  d->m_reader.payloadLen);
                XGuiRemoteUdp_wakeProfSample("cli-tcp", arriveUs);
                if (!d->m_dev) return; /* 帧内触发断链。 */
                r = XGuiRemoteFrameReader_feed(&d->m_reader, NULL, 0);
            }
        }
        if (r < 0) {
            xgc_teardown(self, XGUI_REMOTE_BYE_PROTOCOL_ERROR,
                         XGUI_REMOTE_ERR_PROTOCOL, true, false);
            return;
        }
    }
}

/** @brief poll 回调: TLS 门控 → outbox 续传 → 横幅补发 → 保活 → 读泵。 */
static bool xgc_pollPump(void* userData)
{
    XGuiClient* self = (XGuiClient*)userData;
    XGuiClientPrivate* d;
    int64_t now;
    if (!self || !self->m_d) return true;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev) return true;

    /* 会话就绪检查: TLS 档加密完成前不泵会话(横幅必须走密文信道);
     * 明文档等 connected 信号(亦兜底 isOpen)。TLS 握手由读写路径驱动
     * (xssl_v_writeData/readData 每调必泵)——本圈一次 0 长写专用于推进
     * 握手: 0 长写不产生明文消费, 握手若在本圈内完成也不吞掉对端横幅
     * 首字节(读路径探测的缺陷, 已弃用)。E2E 实测根因(2026-10-02):
     * 握手仅在显式 start/同步 waitFor/读写路径推进, poll 泵若只等
     * isEncrypted 则双端互等永不握手。 */
    if (!d->m_sessionReady) {
#if defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
        if (d->m_devIsSsl) {
            if (!XSslSocket_isEncrypted((XSslSocket*)d->m_dev)) {
                (void)XIODevice_write_1(d->m_dev, "", 0);
                return true;
            }
        } else
#endif
        if (!XIODevice_isOpen(d->m_dev)) return true;
        xgc_sessionDeviceReady(self);
        if (!d->m_dev) return true;
    }

    xgc_flushOutbox(self);
    xgc_ensureBannerSent(self);
    /* 探针纪律(2026-10-04 round3 交付清理): 本行原本每圈必打, 空闲圈
     * avail=0 洪水 ~千行/s(实测 19s 126k 行)淹没日志; 改为仅在有数据
     * 圈打印——帧流节奏证据由 XGC_TRACE recv/fbup 行承载。 */
    if (xgc_benchTraceOn() && d->m_state == XGUI_REMOTE_STATE_STREAMING) {
        int64_t pumpAvail = d->m_dev ? XIODevice_bytesAvailable_base(d->m_dev)
                                     : 0;
        if (pumpAvail > 0)
            fprintf(stderr, "XGC_TRACE pump t=%llu avail=%lld\n",
                    (unsigned long long)(uint64_t)xgc_nowMs(),
                    (long long)pumpAvail);
    }

    now = xgc_nowMs();
    /* 镜像损伤合并上屏: 静默 XGC_PRESENT_QUIET_MS 或最老损伤
     * XGC_PRESENT_MAX_AGE_MS 到限即刷(突发期把每帧一刷的近平幅
     * paint 合并成 1-3 次, 释放 GUI 圈给收侧套接字排队排空)。
     * [perf9 路5] 窗宽自适应: 交互期(最近转发输入 < ACTIVE_MS)窗取基值
     * 一半——新内容更快上屏; 静止期原基值。首块损伤已在 FB_UPDATE
     * 处理时就地 present(首伤即现), 本判据只承担「窗内后续帧」。 */
    if (d->m_presentFirstMs != 0 &&
        (now - d->m_presentLastMs >= xgc_presentQuietMs(d, now) ||
         now - d->m_presentFirstMs >= xgc_presentMaxMs(d, now))) {
        xgc_presentDamage(self, d);
        d->m_presentFirstMs = 0;
        d->m_presentLastMs = 0;
        d->m_presentFirstUs = 0;
    }
    /* 视口通告跟随(方案④ 2026-10-05): 1:1 可见裁剪窗随宿主布局/resize
     * 变化经 FB_REQUEST(mode=0) 上报服务端 tile 优先级(250ms 节流, 变化
     * 才发); FIT=整幅可见恒(0,0,0,0) 不通告。计算零分配, 空闲圈近零成本。 */
    if (d->m_state == XGUI_REMOTE_STATE_STREAMING && d->m_fbValid &&
        d->m_viewFit == XGUI_CLIENT_VIEW_1TO1 &&
        now - d->m_viewRepMs >= XGC_VIEW_REPORT_MS) {
        int vx, vy, vw, vh;
        xgc_viewportRect(self, &vx, &vy, &vw, &vh);
        if ((vx != d->m_viewRepX || vy != d->m_viewRepY ||
             vw != d->m_viewRepW || vh != d->m_viewRepH)) {
            if (d->m_ffOn)
                fprintf(stderr,
                        "XGC_FF vppoll t=%lld vp=%d,%d+%dx%d\n",
                        (long long)now, vx, vy, vw, vh);
            if (xgc_fbReqAllowed(d)) {
                xgc_sendFbRequest(self, 0, vx, vy, vw, vh);
                d->m_viewRepX = (int16_t)vx;
                d->m_viewRepY = (int16_t)vy;
                d->m_viewRepW = (int16_t)vw;
                d->m_viewRepH = (int16_t)vh;
            }
        }
        d->m_viewRepMs = now;
    }
    /* 保活(§7.3): 档位间隔发 PING(横幅/握手完成起); 超时未收对端任何帧
     * 判死链转重连——BANNER_WAIT 一并纳管, 防对端横幅不至永久悬挂。 */
    if (d->m_state != XGUI_REMOTE_STATE_DISCONNECTED) {
        if (d->m_state == XGUI_REMOTE_STATE_STREAMING ||
            d->m_state == XGUI_REMOTE_STATE_HANDSHAKING ||
            d->m_state == XGUI_REMOTE_STATE_AUTHENTICATING) {
            if (!d->m_pingAwait &&
                (uint64_t)(now - (int64_t)d->m_lastPingSentMs) >=
                    d->m_profile.pingIntervalMs) {
                uint64_t ts = (uint64_t)now;
                uint8_t pbuf[8]; /* 冻结契约: enc* 显式小端序列化后上网。 */
                size_t plen = XGuiRemoteProto_encPing(pbuf, sizeof(pbuf), ts);
                if (plen > 0 &&
                    xgc_sendFrame(self, XGUI_REMOTE_MSG_PING, pbuf, plen)) {
                    d->m_pingSentTs = ts;
                    d->m_pingAwait = true;
                    d->m_lastPingSentMs = (uint64_t)now;
                }
            }
        }
        if ((uint64_t)(now - (int64_t)d->m_lastPeerFrameMs) >
                d->m_profile.pingTimeoutMs) {
            xgc_teardown(self, XGUI_REMOTE_BYE_TIMEOUT,
                         XGUI_REMOTE_ERR_TIMEOUT, false, false);
            return true;
        }
    }

    /* ---- UDP 旁路节拍(2026-10-04, 设计稿 §4.3/§4.1): tick(心跳/NACK/
     *      收兜底) + 静默降级(3s) + 重建请求(5s 冷却) + 单向丢帧恢复。 ---- */
    XGuiRemoteUdp_wakeProfPoll(); /* [wake] 探针 5s 末窗汇总落盘(流量停止后)。 */
    XGuiRemoteUdp_stageProfPoll(); /* [stage] 探针 5s 末窗汇总落盘(同纪律)。 */
    if (d->m_udp) {
        XGuiRemoteUdpChannel_tick(d->m_udp, now);
        if (d->m_state == XGUI_REMOTE_STATE_STREAMING) {
            int udpState = XGuiRemoteUdpChannel_state(d->m_udp);
            if (udpState == XGUI_REMOTE_UDP_STATE_ACTIVE) {
                int64_t silent = XGuiRemoteUdpChannel_msSinceRecv(d->m_udp,
                                                                  now);
                if (silent != INT64_MAX && silent > 3000) {
                    /* 静默降级: 数据面回落 TCP + 通告(对端即时跟随)。 */
                    XGuiRemoteUdpChannel_setActive(d->m_udp, false);
                    d->m_udpRetryMs = now + 5000;
                    {
                        XGuiRemoteMsgUdpMode mode;
                        uint8_t mbuf[8];
                        size_t mlen;
                        XMemset(&mode, 0, sizeof(mode));
                        mode.mode = 0;
                        mlen = XGuiRemoteProto_encUdpMode(mbuf, sizeof(mbuf),
                                                          &mode);
                        if (mlen > 0)
                            xgc_sendFrame(self, XGUI_REMOTE_MSG_UDP_MODE,
                                          mbuf, mlen);
                    }
                    if (xgc_benchTraceOn())
                        fprintf(stderr, "XGuiClient: udp fallback "
                                "(silence>3000ms)\n");
                } else {
                    /* 单向丢帧恢复: 服务端帧活性(PONG msAgo<2s)而本端
                     * FRAME 静默 >4s ⇒ FB_REQUEST 全量刷新(10s 限频)。 */
                    uint32_t srvMsAgo =
                        XGuiRemoteUdpChannel_pongFrameMsAgo(d->m_udp);
                    if (xgc_fbReqAllowed(d) &&
                        d->m_udpLastFrameMs != INT64_MAX &&
                        now - d->m_udpLastFrameMs > 4000 &&
                        srvMsAgo != 0xFFFFFFFFu && srvMsAgo < 2000 &&
                        now - d->m_udpFullReqMs > 10000) {
                        XGuiRemoteMsgFbRequest req;
                        uint8_t rbuf[16];
                        size_t rlen;
                        XMemset(&req, 0, sizeof(req));
                        req.mode = 1;
                        rlen = XGuiRemoteProto_encFbRequest(rbuf,
                                                            sizeof(rbuf),
                                                            &req);
                        if (rlen > 0) {
                            xgc_sendFrame(self, XGUI_REMOTE_MSG_FB_REQUEST,
                                          rbuf, rlen);
                        }
                        d->m_udpFullReqMs = now;
                        if (xgc_benchTraceOn())
                            fprintf(stderr, "XGuiClient: udp frame-loss "
                                    "recovery FB_REQUEST\n");
                    }
                }
            } else if (d->m_udpWanted &&
                       udpState != XGUI_REMOTE_UDP_STATE_ACTIVE &&
                       (d->m_peerCaps & (uint32_t)XGUI_REMOTE_CAP_UDP) &&
                       now >= d->m_udpRetryMs) {
                /* 重建请求(断流恢复/建链失败重试; 服务端回 OFFER)。 */
                XGuiRemoteMsgUdpMode mode;
                uint8_t mbuf[8];
                size_t mlen;
                XMemset(&mode, 0, sizeof(mode));
                mode.mode = 1;
                mlen = XGuiRemoteProto_encUdpMode(mbuf, sizeof(mbuf), &mode);
                if (mlen > 0)
                    xgc_sendFrame(self, XGUI_REMOTE_MSG_UDP_MODE, mbuf, mlen);
                d->m_udpRetryMs = now + 5000;
            }
        }
    }

    /* resource 档移动合并窗口到期: 发出合并中的最新移动(永不合并的
     * PRESS/RELEASE/双击/滚轮不经此处, §7.4)。 */
    if (d->m_hasPendingMove &&
        (uint64_t)(now - (int64_t)d->m_lastMoveSentMs) >=
            d->m_profile.mouseMoveThrottleMs) {
        XGuiRemoteMsgInputPointer move = d->m_pendingMove;
        uint8_t mbuf[24]; /* 冻结契约: enc* 显式序列化后上网(修复: 直发)。 */
        size_t mlen;
        d->m_hasPendingMove = false;
        move.timestampMs = (uint32_t)now;
        mlen = XGuiRemoteProto_encInputPointer(mbuf, sizeof(mbuf), &move);
        if (mlen > 0 &&
            xgc_sendFrame(self, XGUI_REMOTE_MSG_INPUT_POINTER, mbuf, mlen)) {
            d->m_lastMoveSentMs = (uint64_t)now;
        }
    }

    xgc_pumpRead(self);
    return true;
}

/* ==================== 网络连接路径(TCP/TLS, §4.1/§4.3) ==================== */

/** @brief socket 信号槽: connected → 明文档会话就绪(TLS 档交由 poll 泵
 *         在加密完成后就绪)。 */
static void xgc_onSocketConnected(XObject* receiver, XVarList* args)
{
    XGuiClient* self = (XGuiClient*)receiver;
    XGuiClientPrivate* d;
    (void)args;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || d->m_sessionReady) return;
#if defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
    if (d->m_devIsSsl) return; /* 加密完成后由 poll 泵就绪。 */
#endif
    xgc_sessionDeviceReady(self);
}

/** @brief socket 信号槽: disconnected → 传输丢失(断链可重连)。 */
static void xgc_onSocketDisconnected(XObject* receiver, XVarList* args)
{
    XGuiClient* self = (XGuiClient*)receiver;
    XGuiClientPrivate* d;
    (void)args;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev) return; /* 本端已拆除(abort 路径), 幂等。 */
    /* 传输层断开: 无 BYE 可发(对端已不可达)。本槽由 socket 自身事件
     * 派发(VXAbstractSocket_event→setSocketState 发射, 槽返回后发射方
     * 仍读 sock)——teardown 统一走 XObject_deleteLater(2026-10-06 用户
     * 裁定简化), 发射方生命周期天然安全。 */
    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_NONE,
                 false, false);
}

/** @brief socket 信号槽: errorOccurred → 记录错误并断链(可重连)。
 *  @note  与 colorSchemeChangedSlot 同款口径: 不解析 args, 发信号侧先落值
 *         后发射, 单线程模型下读回设备当前错误值即本次错误。 */
static void xgc_onSocketError(XObject* receiver, XVarList* args)
{
    XGuiClient* self = (XGuiClient*)receiver;
    XGuiClientPrivate* d;
    (void)args;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev) return;
    /* 传输级错误统一归化 INTERNAL(XGuiRemoteError 无传输错误专用枚举),
     * 随断链一并广播; errorOccurred 之后通常伴随 disconnected。
     * 本槽由设备事件派发——teardown 统一走 XObject_deleteLater,
     * 发射方生命周期天然安全(2026-10-06 用户裁定简化)。
     */
    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_INTERNAL,
                 false, false);
}

/** @brief 发起网络连接(明文 TCP 或 TLS; connectToHost 重连共用)。 */
static void xgc_connectNetwork(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (d->m_host[0] == '\0' || d->m_port == 0) return;

    /* 重复调用先静默断开旧连接(冻结头注)。 */
    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_NONE,
                 false, true);

#if XGUI_REMOTE_TLS_ON
#if defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
    if (d->m_tlsWanted) {
        XSslSocket* ssl = XSslSocket_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        XString* host;
        if (!ssl) {
            xgc_emitError(self, (int)XGUI_REMOTE_ERR_INTERNAL);
            return;
        }
        /* 自签证书部署口(与 XGuiServer 的环境策略口 xgs_tlsPolicyFromEnv
         * 同族): 环境变量 XGUI_REMOTE_TLS_INSECURE=1 时客户端只加密不校验
         * 对端证书链(QueryPeer——仍请求证书, 不强校验)。缺省不设, 行为
         * 与原口径逐字节一致(VerifyPeer 强校验)。E2E 联调实测根因: 默认
         * VERIFY_REQUIRED 下自签服务端证书握手必败且契约无配置口
         * (XSslSocket_ignoreSslErrors 为 MVP 记录位, 从不参与握手判定),
         * 2026-10-02。 */
        {
            const char* insecure = XSystem_environment("XGUI_REMOTE_TLS_INSECURE");
            if (insecure != NULL && insecure[0] == '1' && insecure[1] == '\0')
                XSslSocket_setPeerVerifyMode(ssl, XSSL_QueryPeer);
        }
        host = XString_create_utf8(d->m_host);
        if (!host) {
            /* [memhunt F2 同审 2026-10-06] 此处 ssl 尚未连接信号、未挂
             * m_dev，同步删除无任何时序依赖(deleteLater 徒增事件队列
             * 依赖，与上方 teardown 设备处置同规约)。 */
            XClassDelete((XClass*)ssl);
            xgc_emitError(self, (int)XGUI_REMOTE_ERR_INTERNAL);
            return;
        }
        if (d->m_peerName[0] != '\0') {
            XString* peer = XString_create_utf8(d->m_peerName);
            if (peer) {
                XSslSocket_connectToHostEncrypted_3(
                    ssl, host, d->m_port, peer, XIODevice_ReadWrite,
                    XAbstractSocket_AnyIPProtocol);
                XClassDelete((XClass*)peer);
            } else {
                XSslSocket_connectToHostEncrypted_2(
                    ssl, host, d->m_port, XIODevice_ReadWrite,
                    XAbstractSocket_AnyIPProtocol);
            }
        } else {
            XSslSocket_connectToHostEncrypted_2(
                ssl, host, d->m_port, XIODevice_ReadWrite,
                XAbstractSocket_AnyIPProtocol);
        }
        XClassDelete((XClass*)host);
        /* 异步握手启动(§4.3): 本仓 XSslSocket 后端的握手只在显式
         * startClientEncryption/同步 waitFor* 路径推进(connectToHost-
         * Encrypted 的信号异步路径不自动开跑)——缺此调用客户端永不发
         * ClientHello(TCP 连上后 tx=0 双端互等)。E2E 实测根因,
         * 2026-10-02。 */
        XSslSocket_startClientEncryption(ssl);
        d->m_dev = (XIODevice*)ssl;
        d->m_devOwned = true;
        d->m_devIsSsl = true;
    } else
#endif /* XNETWORK_SSL_ON */
#endif /* XGUI_REMOTE_TLS_ON */
    {
        XTcpSocket* tcp = XTcpSocket_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!tcp) {
            xgc_emitError(self, (int)XGUI_REMOTE_ERR_INTERNAL);
            return;
        }
        d->m_dev = (XIODevice*)tcp;
        d->m_devOwned = true;
        d->m_devIsSsl = false;
        XAbstractSocket_connectToHost_base((XAbstractSocket*)tcp, d->m_host,
                                           d->m_port, XIODevice_ReadWrite,
                                           XAbstractSocket_AnyIPProtocol);
    }

    /* 连接状态经信号驱动(冻结头: connected/disconnected/errorOccurred
     * XAbstractSocket.h:524/532/550)。 */
    XObject_connect_1((XObject*)d->m_dev,
                      (size_t)XAbstractSocket_connected_signal,
                      (XObject*)self, xgc_onSocketConnected,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)d->m_dev,
                      (size_t)XAbstractSocket_disconnected_signal,
                      (XObject*)self, xgc_onSocketDisconnected,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)d->m_dev,
                      (size_t)XAbstractSocket_errorOccurred_signal,
                      (XObject*)self, xgc_onSocketError,
                      XConnectionType_Direct);

    /* poll 泵在连接发起时即注册(TCP/TLS 一致): TLS 档握手由读路径驱动
     * (xgc_pollPump 加密前每圈 1 字节读泵之), 而 sessionDeviceReady 的
     * 注册点要到加密完成才可达——先注册后握手, 否则 TLS 双端互等
     * (E2E 实测根因, 2026-10-02)。sessionDeviceReady 内同名注册块保留
     * 为兜底(重复注册有 m_pollHandle 守卫)。 */
    if (!d->m_pollHandle) {
        d->m_pollHandle = XAbstractEventDispatcher_addPollCallback(
            xgc_pollPump, self);
    }
}

/* ==================== 虚槽: 画面绘制 ==================== */

/** @brief 占位画面: 按会话状态画背景与状态文本(不画陈旧帧, §7.3)。 */
static void xgc_paintPlaceholder(XGuiClient* self, XPainter* painter)
{
    XGuiClientPrivate* d;
    XRect rc;
    const char* text;
    uint32_t window;
    uint32_t windowText;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    XRect_init(&rc, 0, 0, XWidget_width((XWidget*)self),
               XWidget_height((XWidget*)self));
    window = xgc_paletteColor(self, XPaletteColorRole_Window);
    windowText = xgc_paletteColor(self, XPaletteColorRole_WindowText);
    XPainter_fillRect(painter, &rc, window);

    switch (d->m_state) {
    case XGUI_REMOTE_STATE_BANNER_WAIT:
    case XGUI_REMOTE_STATE_HANDSHAKING:
        text = "正在连接远端服务器…";
        break;
    case XGUI_REMOTE_STATE_AUTHENTICATING:
        text = "正在认证…";
        break;
    case XGUI_REMOTE_STATE_STREAMING:
        text = "等待远端画面…";
        break;
    case XGUI_REMOTE_STATE_DISCONNECTED:
    default:
        text = (d->m_autoReconnect && d->m_wantConnect)
            ? "连接中断, 正在重连…" : "未连接远端服务器";
        break;
    }
    XPainter_drawTextRect(painter, &rc,
                          XPAINTER_TEXT_ALIGN_HCENTER |
                          XPAINTER_TEXT_ALIGN_VCENTER,
                          text, windowText);
}

static void VX_xgc_paintEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XPainter painter;
    XImage* target;
    XPoint offset;
    XRect rc;
    uint64_t stageT0 = XGuiRemoteUdp_stageProfNowUs(); /* [stage] 关=0。 */
    if (!c || !c->m_d || !event) return;
    /* [stage] cli-paint=上屏触发→paintEvent 派发(事件排队/圈调度粒度);
     * 仅镜像损伤触发的下一 paint 采样, 采后清pend(其余重绘不打点)。 */
    if (((XGuiClientPrivate*)c->m_d)->m_stagePaintPend) {
        XGuiClientPrivate* d = (XGuiClientPrivate*)c->m_d;
        d->m_stagePaintPend = false;
        if (stageT0 && d->m_stagePaintFromUs && stageT0 > d->m_stagePaintFromUs)
            XGuiRemoteUdp_stageProfSpan("cli-paint", d->m_stagePaintFromUs,
                                        stageT0);
    }
    target = XWidget_paintImage(self);
    if (!target) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, target)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0) {
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    }
    if (xgc_hasPicture(c)) {
        XGuiClientPrivate* d = c->m_d;
        XRect_init(&rc, 0, 0, XWidget_width(self), XWidget_height(self));
        /* [perf9 路2 任务4 扩 2026-10-05] FIT 信箱底色同占位门: 真机
         * round1 定谳「信箱黑边 ~48% 面积=用户『大面黑』观感主要构成」
         * ——底色随 XGUI_REMOTE_TILE_PLACEHOLDER(缺省开)走深灰, 未交付
         * 与未覆盖域观感统一「待接收」; =0 回退历史纯黑。 */
        XPainter_fillRect(&painter, &rc, xgc_placeholderOn()
                                              ? 0xFF262626u
                                              : 0xFF000000u);
        if (d->m_viewFit == XGUI_CLIENT_VIEW_FIT && d->m_viewScale > 0.0f &&
            (d->m_viewOffX != 0 || d->m_viewOffY != 0 ||
             d->m_viewScale != 1.0f)) {
            /* FIT 信箱绘制(2026-10-04): 黑底已铺, 远端画面按 contain
             * 矩形缩放绘出。
             * [perf9 路5 任务3 增量缩放 2026-10-05] 缓存有效时 1:1 直绘
             * 已缩缓存(XPainter_drawImage 行 memcpy 快车道)——paint 成本
             * 从「整幅信箱逐像素逆映射」(painterRaster_drawImageRect 与
             * 损伤无关, 实测 cli-blit 18.9ms) 降为 O(信箱) memcpy; 损伤
             * 重缩放已在 presentDamage 增量完成。缓存不可用(分配失败/
             * 非常规格式)回退旧全量缩放路径, 语义不变。 */
#if XPAINTER_IMAGE_RECT_ON
            if (xgc_scaledCacheEnsure(c, d)) {
                XPainter_drawImage(&painter, &d->m_scaled,
                                   d->m_viewOffX, d->m_viewOffY);
            } else {
                XPainter_drawImage_3(&painter,
                                     d->m_viewOffX, d->m_viewOffY,
                                     (int)((float)d->m_fbWidth * d->m_viewScale + 0.5f),
                                     (int)((float)d->m_fbHeight * d->m_viewScale + 0.5f),
                                     &d->m_fb, 0, 0,
                                     (int)d->m_fbWidth, (int)d->m_fbHeight);
            }
#else
            /* 极简裁剪配置(无目标/源矩形绘制重载)降级: 按偏移直绘,
             * 超出部分被信箱外黑底语义裁剪——画面不伪影, 仅不缩小。 */
            XPainter_drawImage(&painter, &d->m_fb,
                               d->m_viewOffX, d->m_viewOffY);
#endif
        } else {
            /* 1:1 直绘(§7.2): backbuffer 与控件同尺寸。 */
            XPainter_drawImage(&painter, &d->m_fb, 0, 0);
        }
        /* [perf9 路2 任务4] 可选接收提示(env XGUI_REMOTE_CLIENT_HINT,
         * 缺省关): META 后首 tile 到达前居中绘状态文本, 覆盖在画面
         * 上(深灰占位区可辨, 提示「画面正在到达」)。 */
        if (xgc_hintOn() && d->m_stats.tileCount == d->m_hintBaselineTiles) {
            XRect rc;
            XRect_init(&rc, 0, 0, XWidget_width(self), XWidget_height(self));
            XPainter_drawTextRect(&painter, &rc,
                                  XPAINTER_TEXT_ALIGN_HCENTER |
                                  XPAINTER_TEXT_ALIGN_VCENTER,
                                  "正在接收画面…", 0xFFEFEFEFu);
        }
    } else {
        xgc_paintPlaceholder(c, &painter);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
    /* [stage] cli-blit=paintEvent 绘制时长(黑底填充+FIT 缩放 blit+
     * painter 收尾; 画镜像帧时采样, 占位绘制不打点)。 */
    if (stageT0 && xgc_hasPicture(c))
        XGuiRemoteUdp_stageProfSample("cli-blit", stageT0);
}

/* ==================== 虚槽: 鼠标/滚轮转发(需求 9) ==================== */

/** @brief 按下/双击共用: 抓取 + 置按下集合 + 发 PRESS/DBL_CLICK。 */
static void xgc_forwardPointerDown(XGuiClient* self, XMouseEvent* me,
                                   uint8_t action)
{
    XGuiClientPrivate* d;
    XGuiRemoteMsgInputPointer msg;
    if (!self || !self->m_d || !me) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!xgc_inputChannelReady(self, d->m_fwdPointer)) return;

    /* 按下期间抓取(§7.4): 拖拽越界事件仍派发到本控件至释放(Qt 同语义)。 */
    XWidget_grabMouse((XWidget*)self);

    XMemset(&msg, 0, sizeof(msg));
    msg.action = action;
    msg.button = xgc_buttonBit(me->m_button);
    msg.buttons = (uint16_t)me->m_buttons;
    d->m_pressedButtons |= (uint16_t)msg.button; /* 成对纪律: 置按下集合。 */
    msg.modifiers = (uint32_t)me->m_modifiers;
    /* 抓取期越界坐标不裁剪: i16 ±32767 饱和编码, 远端负责裁剪(§3.2)。
     * 坐标先按视图变换逆映射(FIT: (v-off)/scale; 2026-10-04 红线口径:
     * 「看到的点」=「点到的点」)。 */
    xgc_viewToRemote(d, me->m_position.x, me->m_position.y, &msg.x, &msg.y);
    msg.timestampMs = me->m_timestamp;
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
    {
        uint8_t pbuf[24];
        size_t plen = XGuiRemoteProto_encInputPointer(pbuf, sizeof(pbuf), &msg);
        if (plen > 0)
            xgc_sendFrame(self, XGUI_REMOTE_MSG_INPUT_POINTER, pbuf, plen);
    }
}

static void VX_xgc_mousePressEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    if (!c || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        return;
    }
    /* 点击夺焦(需求 9「首次点击由框架标准逻辑自然夺焦」的落地; 2026-10-02
     * 修复: 处理器此前不登记焦点——demo 设置页模式下控件被摆进 Tab 链外
     * (隐藏镜像视图), 键盘/IME 转发通道因 hasFocus 门控恒死。对标
     * XAbstractButton/XTabBar 的 XFocusReason_Mouse 点击夺焦惯例, 与
     * XLineEdit 点击聚焦同语义; 键盘转发侧另有 hasFocus 双保险门控
     * (VX_xgc_keyPressEvent), 无焦点时按键本就不达此处, 行为无回归面。 */
    XWidget_setFocusReason(self, XFocusReason_Mouse);
    xgc_forwardPointerDown(c, (XMouseEvent*)event, XGUI_REMOTE_PTR_PRESS);
    XEvent_accept(event);
}

static void VX_xgc_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    if (!c || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) {
        return;
    }
    /* 双击显式发 action=3(§6.6: 不依赖服务端平台阈值识别); 键集合语义
     * 同 PRESS(该键物理按下中, 后随 RELEASE 配对)。 */
    xgc_forwardPointerDown(c, (XMouseEvent*)event, XGUI_REMOTE_PTR_DBL_CLICK);
    XEvent_accept(event);
}

static void VX_xgc_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XGuiRemoteMsgInputPointer msg;
    XMouseEvent* me;
    if (!c || !c->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    me = (XMouseEvent*)event;
    if (XWidget_mouseGrabber() == self) {
        XWidget_releaseMouse(self); /* 释放抓取(§7.4)。 */
    }
    if (xgc_inputChannelReady(c, d->m_fwdPointer) && me) {
        XMemset(&msg, 0, sizeof(msg));
        msg.action = XGUI_REMOTE_PTR_RELEASE;
        msg.button = xgc_buttonBit(me->m_button);
        msg.buttons = (uint16_t)me->m_buttons;
        d->m_pressedButtons &= (uint16_t)(~msg.button); /* 成对纪律: 清集合。 */
        msg.modifiers = (uint32_t)me->m_modifiers;
        xgc_viewToRemote(d, me->m_position.x, me->m_position.y,
                         &msg.x, &msg.y);
        msg.timestampMs = me->m_timestamp;
        /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
        {
            uint8_t pbuf[24];
            size_t plen = XGuiRemoteProto_encInputPointer(pbuf, sizeof(pbuf),
                                                          &msg);
            if (plen > 0)
                xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_POINTER, pbuf, plen);
        }
    }
    XEvent_accept(event);
}

static void VX_xgc_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XMouseEvent* me;
    if (!c || !c->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    me = (XMouseEvent*)event;
    if (!xgc_inputChannelReady(c, d->m_fwdPointer) || !me) return;
    {
        XGuiRemoteMsgInputPointer msg;
        int64_t now = xgc_nowMs();
        XMemset(&msg, 0, sizeof(msg));
        msg.action = XGUI_REMOTE_PTR_MOVE;
        msg.button = xgc_buttonBit(me->m_button);
        msg.buttons = (uint16_t)me->m_buttons;
        msg.modifiers = (uint32_t)me->m_modifiers;
        /* 抓取期越界坐标: 同样 i16 饱和编码不裁剪(§3.2/§7.4); 视图逆
         * 映射同按下/释放口径(2026-10-04)。合并窗口内存的已是远端帧
         * 坐标, 到期直发路径无需二次换算。 */
        xgc_viewToRemote(d, me->m_position.x, me->m_position.y,
                         &msg.x, &msg.y);
        msg.timestampMs = me->m_timestamp;

        if (d->m_profile.mouseMoveThrottleMs > 0) {
            /* resource 档合并窗口: 窗口内仅保存最新移动, 到期由 poll 泵发出
             * (按下/释放/双击/滚轮永不合并, §7.4)。 */
            d->m_pendingMove = msg;
            d->m_hasPendingMove = true;
            if ((uint64_t)(now - (int64_t)d->m_lastMoveSentMs) >=
                d->m_profile.mouseMoveThrottleMs) {
                XGuiRemoteMsgInputPointer out = d->m_pendingMove;
                uint8_t obuf[24]; /* 冻结契约: enc* 序列化后上网(修复)。 */
                size_t olen;
                d->m_hasPendingMove = false;
                out.timestampMs = (uint32_t)now;
                olen = XGuiRemoteProto_encInputPointer(obuf, sizeof(obuf),
                                                       &out);
                if (olen > 0 &&
                    xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_POINTER,
                                  obuf, olen)) {
                    d->m_lastMoveSentMs = (uint64_t)now;
                }
            }
        } else {
            /* performance 档(0ms): 直传。 */
            uint8_t mbuf[24]; /* 冻结契约: enc* 序列化后上网(修复: 直发)。 */
            size_t mlen = XGuiRemoteProto_encInputPointer(mbuf, sizeof(mbuf),
                                                          &msg);
            if (mlen > 0 &&
                xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_POINTER, mbuf, mlen)) {
                d->m_lastMoveSentMs = (uint64_t)now;
            }
        }
    }
    XEvent_accept(event);
}

static void VX_xgc_wheelEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XWheelEvent* we;
    XGuiRemoteMsgInputWheel msg;
    if (!c || !c->m_d || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    we = (XWheelEvent*)event;
    if (!xgc_inputChannelReady(c, d->m_fwdPointer) || !we) return;
    /* 滚轮永不合并(§7.4); angleDelta ±120/格 透传(§6.6)。 */
    XMemset(&msg, 0, sizeof(msg));
    msg.angleX = xgc_satI16(we->m_angleDelta.x);
    msg.angleY = xgc_satI16(we->m_angleDelta.y);
    msg.pixelX = xgc_satI16(we->m_pixelDelta.x);
    msg.pixelY = xgc_satI16(we->m_pixelDelta.y);
    msg.buttons = (uint16_t)we->m_buttons;
    msg.modifiers = (uint32_t)we->m_modifiers;
    /* 滚轮坐标同视图逆映射(远端按局部坐标命中滚轮靶)。 */
    xgc_viewToRemote(d, we->m_position.x, we->m_position.y, &msg.x, &msg.y);
    msg.timestampMs = (uint32_t)xgc_nowMs();
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发 24B ≠ 线上 22B)。 */
    {
        uint8_t wbuf[32];
        size_t wlen = XGuiRemoteProto_encInputWheel(wbuf, sizeof(wbuf), &msg);
        if (wlen > 0)
            xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_WHEEL, wbuf, wlen);
    }
    XEvent_accept(event);
}

/* ==================== 虚槽: 键盘/IME 转发(需求 9) ==================== */

/** @brief 触摸转发(2026-10-02 加法式: XGuiClient_setForwardTouch 开启后
 *         生效; 主点必须放 points[0], 对齐服务端注入契约 §6.6)。 */
static void VX_xgc_touchEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XTouchEvent* te;
    XGuiRemoteMsgInputTouch msg;
    const XTouchPoint* pts;
    int count;
    int i;
    XEventType type = XEvent_type(event);
    if (!c || !c->m_d || event ||
        (type != XEVENT_TYPE_TOUCH_BEGIN && type != XEVENT_TYPE_TOUCH_UPDATE &&
         type != XEVENT_TYPE_TOUCH_END && type != XEVENT_TYPE_TOUCH_CANCEL)) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    te = (XTouchEvent*)event;
    /* 触摸属指针通道语义(§7.4 指针=指点类输入), 门控沿用 m_fwdPointer。 */
    if (!xgc_inputChannelReady(c, d->m_fwdPointer)) return;
    count = XTouchEvent_pointCount(te);
    if (count <= 0) return;
    if (count > XGUI_REMOTE_MAX_TOUCH_POINTS) count = XGUI_REMOTE_MAX_TOUCH_POINTS;
    pts = XTouchEvent_points(te);
    XMemset(&msg, 0, sizeof(msg));
    switch (type) {
    case XEVENT_TYPE_TOUCH_BEGIN:  msg.action = XGUI_REMOTE_TOUCH_BEGIN;  break;
    case XEVENT_TYPE_TOUCH_UPDATE: msg.action = XGUI_REMOTE_TOUCH_UPDATE; break;
    case XEVENT_TYPE_TOUCH_END:    msg.action = XGUI_REMOTE_TOUCH_END;    break;
    default:                       msg.action = XGUI_REMOTE_TOUCH_CANCEL; break;
    }
    msg.pointCount = (uint8_t)count;
    msg.timestampMs = (uint32_t)xgc_nowMs();
    for (i = 0; i < count; ++i) {
        XGuiRemoteMsgTouchPoint* dst = &msg.points[i];
        const XTouchPoint* src = pts ? &pts[i] : NULL;
        dst->id = src ? (uint32_t)src->m_id : 0u;
        /* 触点四态数值两端逐字对齐(QEventPoint::State), 原值透传。 */
        dst->state = src ? (uint8_t)src->m_state
                         : (uint8_t)XGUI_REMOTE_TP_PRESSED;
        dst->x = 0;
        dst->y = 0;
        xgc_viewToRemote(d, src ? src->m_position.x : 0,
                         src ? src->m_position.y : 0, &dst->x, &dst->y);
        /* 主点缺失(未分配列表)时以主点字段兜底。 */
        if (!src && i == 0) {
            XPoint p = XTouchEvent_position(te);
            xgc_viewToRemote(d, p.x, p.y, &dst->x, &dst->y);
        }
        dst->pressureQ8 = src ? (uint8_t)(src->m_pressure * 255.0f + 0.5f)
                              : 255u;
    }
    {
        uint8_t tbuf[6 + XGUI_REMOTE_MAX_TOUCH_POINTS * 11];
        size_t tlen = XGuiRemoteProto_encInputTouch(tbuf, sizeof(tbuf), &msg);
        if (tlen > 0)
            xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_TOUCH, tbuf, tlen);
    }
    XEvent_accept(event);
}

static void VX_xgc_keyPressEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XKeyEvent* ke;
    XGuiRemoteMsgInputKey msg;
    if (!c || !c->m_d || !event || XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    ke = (XKeyEvent*)event;
    /* 焦点门控(§7.4): 仅本控件持有焦点时转发(控件无焦点时框架本就不投递
     * 按键, 此为双保险)。 */
    if (!XWidget_hasFocus(self) || !d->m_fwdKeyboard || !ke) return;
    XMemset(&msg, 0, sizeof(msg));
    msg.action = XGUI_REMOTE_KEY_PRESS;
    msg.key = (uint32_t)ke->m_key;
    msg.modifiers = (uint32_t)ke->m_modifiers;
    msg.nativeScanCode = ke->m_nativeScanCode;
    msg.timestampMs = ke->m_timestamp;
    if (!ke->m_autoRepeat) {
        /* 成对纪律: 首次按下置集合(自动重复的重复 PRESS 不重复置)。 */
        xgc_pressedKeysAdd(d, msg.key);
    }
    /* autoRepeat 原样转发重复 PRESS(服务端 handleKeyEvent_ex 有该参数)。 */
    {
        uint8_t kbuf[24]; /* 冻结契约: enc* 显式序列化后上网(修复: 结构体
                           * 直发 20B ≠ 线上 17B, 服务端严格 dec 必拒)。 */
        size_t klen = XGuiRemoteProto_encInputKey(kbuf, sizeof(kbuf), &msg);
        if (klen > 0)
            xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_KEY, kbuf, klen);
    }
    XEvent_accept(event);
}

static void VX_xgc_keyReleaseEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XKeyEvent* ke;
    XGuiRemoteMsgInputKey msg;
    if (!c || !c->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_RELEASE) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    ke = (XKeyEvent*)event;
    if (!XWidget_hasFocus(self) || !d->m_fwdKeyboard || !ke) return;
    XMemset(&msg, 0, sizeof(msg));
    msg.action = XGUI_REMOTE_KEY_RELEASE;
    msg.key = (uint32_t)ke->m_key;
    msg.modifiers = (uint32_t)ke->m_modifiers;
    msg.nativeScanCode = ke->m_nativeScanCode;
    msg.timestampMs = ke->m_timestamp;
    xgc_pressedKeysRemove(d, msg.key); /* 成对纪律: 清集合。 */
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
    {
        uint8_t kbuf[24];
        size_t klen = XGuiRemoteProto_encInputKey(kbuf, sizeof(kbuf), &msg);
        if (klen > 0)
            xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_KEY, kbuf, klen);
    }
    XEvent_accept(event);
}

static void VX_xgc_inputMethodEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    XGuiClientPrivate* d;
    XInputMethodEvent* ime;
    XGuiRemoteMsgInputIme msg;
    const char* utf8;
    size_t n;
    if (!c || !c->m_d || !event ||
        XEvent_type(event) != XEVENT_TYPE_INPUT_METHOD) {
        return;
    }
    d = (XGuiClientPrivate*)c->m_d;
    ime = (XInputMethodEvent*)event;
    if (!XWidget_hasFocus(self) || !d->m_fwdIme || !ime) return;
    /* 原文透明转发(§7.4): 本地不做任何输入法逻辑; 超长按协议帽截断。 */
    XMemset(&msg, 0, sizeof(msg));
    if (ime->m_preeditString) {
        utf8 = XString_toUtf8(ime->m_preeditString);
        n = utf8 ? strlen(utf8) : 0;
        if (n > XGUI_REMOTE_MAX_TEXT_BYTES) n = XGUI_REMOTE_MAX_TEXT_BYTES;
        msg.preeditBytes = (uint16_t)n;
        if (n > 0) memcpy(msg.preedit, utf8, n);
    }
    if (ime->m_commitString) {
        utf8 = XString_toUtf8(ime->m_commitString);
        n = utf8 ? strlen(utf8) : 0;
        if (n > XGUI_REMOTE_MAX_TEXT_BYTES) n = XGUI_REMOTE_MAX_TEXT_BYTES;
        msg.commitBytes = (uint16_t)n;
        if (n > 0) memcpy(msg.commit, utf8, n);
    }
    msg.replacementStart = ime->m_replacementStart;
    msg.replacementLength = ime->m_replacementLength;
    msg.cursorPosition = ime->m_cursorPosition;
    msg.anchorPosition = ime->m_anchorPosition;
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发)。 */
    {
        uint8_t ibuf[2 * XGUI_REMOTE_MAX_TEXT_BYTES + 32];
        size_t ilen = XGuiRemoteProto_encInputIme(ibuf, sizeof(ibuf), &msg);
        if (ilen > 0)
            xgc_sendFrame(c, XGUI_REMOTE_MSG_INPUT_IME, ibuf, ilen);
    }
    XEvent_accept(event);
}

/* ==================== 生命周期 ==================== */

/** @brief 尺寸变化: FIT 按新几何重算信箱变换并整幅置脏(重算前后变换
 *         不同才需要, 但整幅 update 幂等且低频——resize 本身低频)。 */
static void VX_xgc_resizeEvent(XWidget* self, XEvent* event)
{
    XGuiClient* c = (XGuiClient*)self;
    if (!c || !c->m_d || !event) return;
    xgc_updateViewTransform(c);
    xgc_invalidateWhole(c);
}

static void VX_xgc_deinit(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) {
        XClass_Deinit_Parent(XWidget, (XWidget*)self);
        return;
    }
    d = (XGuiClientPrivate*)self->m_d;

    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_NONE,
                 true, true);
    if (d->m_reconnectTimer) {
        XTimer_stop_base(d->m_reconnectTimer);
        /* 同步删除: 理由同 m_pumpTimer(挂起定时事件 UAF 窗口闭合)。 */
        XClassDelete((XClass*)d->m_reconnectTimer);
        d->m_reconnectTimer = NULL;
    }
    XGuiRemoteFrameReader_deinit(&d->m_reader);
    XRegion_deinit(&d->m_damage);
    if (d->m_pressedKeys) {
        XFree_System(d->m_pressedKeys);
        d->m_pressedKeys = NULL;
    }
    if (d->m_outbox) {
        XFree_System(d->m_outbox);
        d->m_outbox = NULL;
    }
    XMemset(d->m_password, 0, sizeof(d->m_password)); /* 口令即焚。 */
    if (d->m_udp) { /* UDP 旁路通道随控件收尾(2026-10-04)。 */
        XGuiRemoteUdpChannel_delete(d->m_udp);
        d->m_udp = NULL;
    }
    XClassDeinit(&d->m_fb);
    xgc_scaledCacheReset(d); /* FIT 缩放缓存(perf9 路5): 图+列映射表。 */
    XFree_System(d);
    self->m_d = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XGuiClient_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGuiClient)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_xgc_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VX_xgc_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_xgc_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VX_xgc_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VX_xgc_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VX_xgc_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_TouchEvent, VX_xgc_touchEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VX_xgc_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyReleaseEvent, VX_xgc_keyReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_InputMethodEvent,
                             VX_xgc_inputMethodEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_xgc_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_xgc_deinit);
    return XVTABLE_DEFAULT;
}

void XGuiClient_init(XGuiClient* self, XWidget* parent, XWidgetFlags flags)
{
    XGuiClientPrivate* d;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XGuiClient);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);

    d = (XGuiClientPrivate*)XCalloc_System(1, sizeof(XGuiClientPrivate));
    self->m_d = d;
    if (!d) return;

    XImage_init(&d->m_fb);            /* 空 backbuffer, FB_META 重建。 */
    XImage_init(&d->m_scaled);        /* FIT 缩放缓存(perf9 路5)。 */
    XRegion_init(&d->m_damage);
    XGuiRemoteFrameReader_init(&d->m_reader);
    XGuiRemoteProfile_initPerformance(&d->m_profile);
    XGuiRemoteProfile_sanitize(&d->m_profile);
    d->m_state = XGUI_REMOTE_STATE_DISCONNECTED;
    d->m_fwdPointer = true;
    d->m_fwdKeyboard = true;
    d->m_fwdIme = true;
    d->m_fwdTouch = false; /* 默认关(2026-10-02): 单点触摸已由鼠标路径+仿真覆盖。 */
    d->m_reconnectBaseMs = XGC_RECONNECT_DEFAULT_MS;
    d->m_viewFit = XGUI_CLIENT_VIEW_FIT; /* 默认适配缩放(2026-10-04)。 */
    { /* 视图模式环境覆盖(测量/专项诊断用, 2026-10-05 方案④ A/B;
       * 缺省 FIT 不变; 值 "1to1"=1:1 裁剪视口)。 */
        const char* vm = XSystem_environment("XGUI_CLIENT_VIEW_MODE");
        if (vm && vm[0] == '1')
            d->m_viewFit = XGUI_CLIENT_VIEW_1TO1;
    }
    d->m_viewScale = 1.0f;
    /* [perf9 路5] FIT 增量缩放像素等价离屏断言(env 门控, 缺省不跑;
     * 探针纪律: stderr 单行 PASS/FAIL, 不依赖网络/屏幕)。 */
    if (XSystem_environment("XGUI_REMOTE_FIT_SCALE_SELFTEST") != NULL) {
        fprintf(stderr, "XGC_FIT_SCALE_SELFTEST %s\n",
                xgc_fitScaleSelfTest() ? "PASS" : "FAIL");
    }

    /* 能力位(§3.5): 按编译期真值宣告。UDP 旁路默认开(2026-10-04; 对端
     * 未宣告 CAP_UDP 时自动纯 TCP, 行为不变)。 */
    d->m_localCaps = XGUI_REMOTE_CAP_RGB565 | XGUI_REMOTE_CAP_IME |
                     XGUI_REMOTE_CAP_PROFILE_SET |
                     XGUI_REMOTE_CAP_UDP |
                     XGUI_REMOTE_CAP_FB_REQUEST | /* 首帧请求(方案③)。 */
                     XGUI_REMOTE_CAP_LATENCY; /* latency 档(2026-10-05)。 */
    d->m_udpWanted = true;
    d->m_udpState = (int)XGUI_REMOTE_UDP_STATE_OFF;
    d->m_udpLastFrameMs = INT64_MAX;
    /* 首帧 FB_REQUEST/视口通告(方案③④; 2026-10-05)。探针开关 env 定版
     * (XGUI_REMOTE_FIRSTFRAME_PROF=1; 探针纪律: 交付前可关)。 */
    d->m_fbReqSent = false;
    d->m_viewRepX = 0;
    d->m_viewRepY = 0;
    d->m_viewRepW = 0;
    d->m_viewRepH = 0;
    d->m_viewRepMs = 0;
    d->m_ffOn = (XSystem_environment("XGUI_REMOTE_FIRSTFRAME_PROF") != NULL);
    d->m_ffMap = NULL;
    d->m_ffGridW = 0;
    d->m_ffGridH = 0;
    d->m_ffGot = 0;
    d->m_ffTotal = 0;
    d->m_ffVpGot = 0;
    d->m_ffVpTotal = 0;
    d->m_ffVpX = 0;
    d->m_ffVpY = 0;
    d->m_ffVpW = 0;
    d->m_ffVpH = 0;
    d->m_ffT0Ms = 0;
    d->m_ffFullPrinted = false;
    d->m_ffVpPrinted = false;
#if XGUI_REMOTE_ZLIB_ON
    if (XGuiRemoteCodec_hasCodec(XGUI_REMOTE_CODEC_ZLIB)) {
        d->m_localCaps |= XGUI_REMOTE_CAP_ZLIB;
    }
#endif
#if XGUI_REMOTE_TLS_ON
#if defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
    d->m_localCaps |= XGUI_REMOTE_CAP_TLS;
#endif
#endif

    /* 需求 9: StrongFocus(首次点击由框架标准逻辑自然夺焦)+ 启用本地
     * IME 提示(WA_InputMethodEnabled)。 */
    XWidget_setFocusPolicy(&self->m_base, XWidgetFocusPolicy_StrongFocus);
    XWidget_setAttribute(&self->m_base,
                         XWidgetAttribute_InputMethodEnabled, true);
    XWidget_setInputMethodHints(&self->m_base, XInputMethodHint_None);
    /* 鼠标移动直捕转发需 tracking(无按键也收 Move)。 */
    XWidget_setMouseTracking(&self->m_base, true);

    /* 重连定时器(单次触发; 指数退避由 xgc_scheduleReconnect 驱动)。 */
    d->m_reconnectTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (d->m_reconnectTimer) {
        XTimer_setSingleShot(d->m_reconnectTimer, true);
        XTimer_setUserData(d->m_reconnectTimer, self);
        XTimer_setTimerCallback(d->m_reconnectTimer, xgc_reconnectFire);
    }
}

XGuiClient* XGuiClient_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XGuiClient* self = (XGuiClient*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XGuiClient_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 连接(需求 2: 传输无关) ==================== */

void XGuiClient_connectToHost(XGuiClient* self, const char* hostUtf8,
                              uint16_t port)
{
    XGuiClientPrivate* d;
    size_t n;
    if (!self || !self->m_d || !hostUtf8 || hostUtf8[0] == '\0') return;
    d = (XGuiClientPrivate*)self->m_d;
    n = strlen(hostUtf8);
    if (n >= XGC_HOST_CAP) n = XGC_HOST_CAP - 1;
    memcpy(d->m_host, hostUtf8, n);
    d->m_host[n] = '\0';
    d->m_port = port;
    d->m_peerName[0] = '\0';
    d->m_tlsWanted = false;
    d->m_wantConnect = true;
    d->m_reconnectAttempts = 0;
    xgc_connectNetwork(self);
}

#if XGUI_REMOTE_TLS_ON
void XGuiClient_connectToHostEncrypted(XGuiClient* self, const char* hostUtf8,
                                       uint16_t port,
                                       const char* peerNameUtf8)
{
    XGuiClientPrivate* d;
    size_t n;
    if (!self || !self->m_d || !hostUtf8 || hostUtf8[0] == '\0') return;
    d = (XGuiClientPrivate*)self->m_d;
    n = strlen(hostUtf8);
    if (n >= XGC_HOST_CAP) n = XGC_HOST_CAP - 1;
    memcpy(d->m_host, hostUtf8, n);
    d->m_host[n] = '\0';
    if (peerNameUtf8 && peerNameUtf8[0] != '\0') {
        n = strlen(peerNameUtf8);
        if (n >= XGC_HOST_CAP) n = XGC_HOST_CAP - 1;
        memcpy(d->m_peerName, peerNameUtf8, n);
        d->m_peerName[n] = '\0';
    } else {
        d->m_peerName[0] = '\0';
    }
    d->m_port = port;
    d->m_tlsWanted = true;
    d->m_wantConnect = true;
    d->m_reconnectAttempts = 0;
    xgc_connectNetwork(self);
}
#endif /* XGUI_REMOTE_TLS_ON */

void XGuiClient_setTransport(XGuiClient* self, XIODevice* device)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;

    /* 旧会话静默拆除(借用设备不销毁)。 */
    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_NONE,
                 false, true);
    d->m_host[0] = '\0';
    d->m_port = 0;
    d->m_tlsWanted = false;
    d->m_wantConnect = false; /* 自定义传输无自动重连语义。 */

    if (!device) return;
    d->m_dev = device;
    d->m_devOwned = false;
    d->m_devIsSsl = false;
    if (XIODevice_isOpen(device)) {
        /* 传入后状态直接进入握手阶段(冻结头注): 横幅即发。 */
        d->m_state = XGUI_REMOTE_STATE_BANNER_WAIT;
        xgc_sessionDeviceReady(self);
    }
}

void XGuiClient_disconnectFromServer(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    /* [2026-10-08 用户裁定: 断开=断干净] 幂等分支也要清自动重连状态:
     * 断线退避期（重连定时器 armed、wantConnect=true、state 已回
     * DISCONNECTED）点断开时, 旧实现直接 return, armed 的重连定时器
     * 到点即重连——表现为「断了还在自动重试」(真屏远程客户端页实测)。
     * 此处统一 wantConnect=false + 停定时器 + 清退避计数, 再走拆除。 */
    d->m_wantConnect = false;
    if (d->m_reconnectTimer) XTimer_stop_base(d->m_reconnectTimer);
    d->m_reconnectAttempts = 0;
    if (d->m_dev == NULL && d->m_state == XGUI_REMOTE_STATE_DISCONNECTED) {
        return; /* 已拆除, 幂等(重连状态已在上方清理)。 */
    }
    xgc_teardown(self, XGUI_REMOTE_BYE_NORMAL, XGUI_REMOTE_ERR_NONE,
                 true, false);
}

void XGuiClient_setAutoReconnect(XGuiClient* self, bool enabled,
                                 uint32_t intervalMs)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    d->m_autoReconnect = enabled;
    d->m_reconnectBaseMs = intervalMs > 0 ? intervalMs
                                          : XGC_RECONNECT_DEFAULT_MS;
    if (!enabled && d->m_reconnectTimer) {
        XTimer_stop_base(d->m_reconnectTimer);
    }
}

XGuiRemoteSessionState XGuiClient_state(const XGuiClient* self)
{
    if (!self || !self->m_d) return XGUI_REMOTE_STATE_DISCONNECTED;
    return ((const XGuiClientPrivate*)self->m_d)->m_state;
}

/* ==================== 认证与档位 ==================== */

void XGuiClient_setPassword(XGuiClient* self, const char* passwordUtf8)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    XGuiRemoteAuth_clientSetPassword(d->m_password, sizeof(d->m_password),
                                     passwordUtf8);
}

/* [2026-10-04 访问口令加法式扩展] 主体在 XGuiRemoteAuth.c; 此处仅挂接。 */
void XGuiClient_setAccessPassword(XGuiClient* self, const char* passwordUtf8)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    XGuiRemoteAuth_clientSetPassword(d->m_password, sizeof(d->m_password),
                                     passwordUtf8);
}

void XGuiClient_clearAccessPassword(XGuiClient* self)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    XGuiRemoteAuth_clientClearPassword(d->m_password, sizeof(d->m_password));
}

void XGuiClient_requestProfileId(XGuiClient* self, XGuiRemoteProfileId id)
{
    XGuiClientPrivate* d;
    XGuiRemoteMsgProfileSet ps;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (!d->m_dev || !d->m_sessionReady) return; /* 无会话: 无操作。 */
    if (id == XGUI_REMOTE_PROFILE_CUSTOM) {
        /* 自定义档需参数块, 冻结契约只暴露预设 id 请求; V1 拒绝并报错。 */
        xgc_emitError(self, (int)XGUI_REMOTE_ERR_PROTOCOL);
        return;
    }
    XMemset(&ps, 0, sizeof(ps));
    ps.profileId = (uint8_t)id;
    ps.hasCustomProfile = 0;
    /* 冻结契约: enc* 显式序列化后上网(修复: 结构体直发含对齐填充,
     * 服务端严格 dec 必拒)。 */
    {
        uint8_t pbuf[48];
        size_t plen = XGuiRemoteProto_encProfileSet(pbuf, sizeof(pbuf), &ps);
        if (plen > 0)
            xgc_sendFrame(self, XGUI_REMOTE_MSG_PROFILE_SET, pbuf, plen);
    }
}

void XGuiClient_setInputForwardingEnabled(XGuiClient* self, bool pointer,
                                          bool keyboard, bool ime)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    d->m_fwdPointer = pointer;
    d->m_fwdKeyboard = keyboard;
    d->m_fwdIme = ime;
}

/* ==================== 查询 ==================== */

void XGuiClient_remoteSize(const XGuiClient* self, XSize* out)
{
    const XGuiClientPrivate* d;
    if (!out) return;
    XSize_init(out, 0, 0);
    if (!self || !self->m_d) return;
    d = (const XGuiClientPrivate*)self->m_d;
    XSize_init(out, (int)d->m_fbWidth, (int)d->m_fbHeight);
}

int XGuiClient_remoteTitle(const XGuiClient* self, char* buffer, int cap)
{
    const XGuiClientPrivate* d;
    int n;
    int copy;
    if (!self || !self->m_d) return 0;
    d = (const XGuiClientPrivate*)self->m_d;
    n = d->m_titleBytes;
    if (!buffer || cap <= 0) return n;
    copy = n < cap - 1 ? n : cap - 1;
    if (copy > 0) memcpy(buffer, d->m_title, (size_t)copy);
    buffer[copy] = '\0';
    return n;
}

bool XGuiClient_isRemoteAlive(const XGuiClient* self)
{
    if (!self || !self->m_d) return false;
    return ((const XGuiClientPrivate*)self->m_d)->m_state ==
           XGUI_REMOTE_STATE_STREAMING;
}

void XGuiClient_statistics(const XGuiClient* self, XGuiRemoteStats* out)
{
    const XGuiClientPrivate* d;
    if (!out) return;
    XMemset(out, 0, sizeof(*out));
    if (!self || !self->m_d) return; /* self 可为 NULL(冻结头注)。 */
    d = (const XGuiClientPrivate*)self->m_d;
    *out = d->m_stats;
}

void XGuiClient_statisticsExtended(const XGuiClient* self,
                                   XGuiRemoteStatsExtended* out)
{
    const XGuiClientPrivate* d;
    if (!out) return;
    XMemset(out, 0, sizeof(*out));
    if (!self || !self->m_d) return; /* self 可为 NULL(与 statistics 同口径)。 */
    d = (const XGuiClientPrivate*)self->m_d;
    out->bytesSent = d->m_stats.bytesSent;
    out->bytesReceived = d->m_stats.bytesReceived;
    out->updateCount = d->m_stats.updateCount;
    out->tileCount = d->m_stats.tileCount;
    out->fpsMilli = d->m_stats.fpsMilli;
    out->rttMs = d->m_stats.rttMs;
    out->reconnectCount = d->m_reconnectCountTotal;
}

void XGuiClient_setForwardTouch(XGuiClient* self, bool enabled)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    d->m_fwdTouch = enabled;
    /* 开启时补 WA_AcceptTouchEvents(默认关时框架本不向本控件投 TOUCH_*,
     * 不打扰既有鼠标路径; 关闭仅断转发, 属性保持无害)。 */
    XWidget_setAttribute(&self->m_base, XWidgetAttribute_AcceptTouchEvents,
                         enabled);
}

/* ==================== UDP 旁路通道(2026-10-04 加法式扩展) ==================== */

void XGuiClient_setUdpEnabled(XGuiClient* self, bool enabled)
{
    XGuiClientPrivate* d;
    if (!self || !self->m_d) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (d->m_udpWanted == enabled) return;
    d->m_udpWanted = enabled;
    if (!enabled) {
        /* 关: 数据面即回 TCP + 通告(UDP_MODE(0); 会话不断)。 */
        if (d->m_udp) XGuiRemoteUdpChannel_setActive(d->m_udp, false);
        d->m_udpState = (int)XGUI_REMOTE_UDP_STATE_OFF;
        if (d->m_dev && d->m_sessionReady &&
            (d->m_peerCaps & (uint32_t)XGUI_REMOTE_CAP_UDP)) {
            XGuiRemoteMsgUdpMode mode;
            uint8_t mbuf[8];
            size_t mlen;
            XMemset(&mode, 0, sizeof(mode));
            mode.mode = 0;
            mlen = XGuiRemoteProto_encUdpMode(mbuf, sizeof(mbuf), &mode);
            if (mlen > 0)
                xgc_sendFrame(self, XGUI_REMOTE_MSG_UDP_MODE, mbuf, mlen);
        }
        if (xgc_benchTraceOn())
            fprintf(stderr, "XGuiClient: udp disabled by user\n");
    } else {
        /* 开: 会话中对端支持时请求建链/重建(UDP_MODE(1) → OFFER)。 */
        d->m_udpRetryMs = 0; /* 下一泵圈即试(冷却归零)。 */
        if (xgc_benchTraceOn())
            fprintf(stderr, "XGuiClient: udp enabled by user\n");
    }
}

bool XGuiClient_udpActive(const XGuiClient* self)
{
    const XGuiClientPrivate* d;
    if (!self || !self->m_d) return false;
    d = (const XGuiClientPrivate*)self->m_d;
    return d->m_udp &&
           XGuiRemoteUdpChannel_state(d->m_udp) ==
               XGUI_REMOTE_UDP_STATE_ACTIVE;
}

int XGuiClient_udpState(const XGuiClient* self)
{
    if (!self || !self->m_d) return (int)XGUI_REMOTE_UDP_STATE_OFF;
    return ((const XGuiClientPrivate*)self->m_d)->m_udpState;
}

void XGuiClient_sendTouchFrame(XGuiClient* self,
                               const XGuiRemoteMsgInputTouch* msg)
{
    XGuiClientPrivate* d;
    uint8_t tbuf[6 + XGUI_REMOTE_MAX_TOUCH_POINTS * 11];
    size_t tlen;
    if (!self || !self->m_d || !msg) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (msg->pointCount == 0) return;      /* 空帧: 无操作(防服务端拒)。 */
    if (!d->m_dev || !d->m_sessionReady) return; /* 无会话: 无操作。 */
    tlen = XGuiRemoteProto_encInputTouch(tbuf, sizeof(tbuf), msg);
    if (tlen > 0)
        xgc_sendFrame(self, XGUI_REMOTE_MSG_INPUT_TOUCH, tbuf, tlen);
}

/* ==================== 视图适配缩放(2026-10-04 加法式) ==================== */

void XGuiClient_setViewFitMode(XGuiClient* self, XGuiClientViewFitMode mode)
{
    XGuiClientPrivate* d;
    XGuiClientViewFitMode old;
    if (!self || !self->m_d) return;
    if (mode != XGUI_CLIENT_VIEW_FIT && mode != XGUI_CLIENT_VIEW_1TO1) return;
    d = (XGuiClientPrivate*)self->m_d;
    if (d->m_viewFit == mode) return;
    old = d->m_viewFit;
    d->m_viewFit = mode;
    if (mode == XGUI_CLIENT_VIEW_1TO1) {
        /* FIT→1:1: 恢复 V1 口径=控件采用远端尺寸(已收 FB_META 时);
         * setFixedSize 自触发 resize→重算+置脏, 不残留缩放旧帧。 */
        if (d->m_fbValid)
            XWidget_setFixedSize((XWidget*)self, (int)d->m_fbWidth,
                                 (int)d->m_fbHeight);
    } else if (old == XGUI_CLIENT_VIEW_1TO1 && d->m_fbValid) {
        /* 1:1→FIT: 控件可能停在远端尺寸——先解除 1:1 setFixedSize 留下
         * 的 min=max 钳位(否则 resize 被钳回远端尺寸, 信箱永不生效),
         * 再缩回父布局给定的可视尺寸(取父控件可用几何), 宿主可随后
         * 自行 resize 精调; resize 链完成重算+置脏。 */
        XWidget* p = XWidget_parentWidget((XWidget*)self);
        int pw = p ? XWidget_width(p) : (int)d->m_fbWidth;
        int ph = p ? XWidget_height(p) : (int)d->m_fbHeight;
        XWidget_setMinimumSize((XWidget*)self, 0, 0);
        XWidget_setMaximumSize((XWidget*)self, XWIDGET_MAX_SIZE,
                               XWIDGET_MAX_SIZE);
        if (pw > 0 && ph > 0)
            XWidget_resize((XWidget*)self, pw, ph);
    }
    xgc_updateViewTransform(self);
    xgc_invalidateWhole(self);
}

XGuiClientViewFitMode XGuiClient_viewFitMode(const XGuiClient* self)
{
    if (!self || !self->m_d) return XGUI_CLIENT_VIEW_FIT;
    return ((const XGuiClientPrivate*)self->m_d)->m_viewFit;
}

bool XGuiClient_viewTransform(const XGuiClient* self, float* scale,
                              int* offsetX, int* offsetY)
{
    const XGuiClientPrivate* d;
    float s;
    int ox, oy;
    if (scale) *scale = 0.0f;
    if (offsetX) *offsetX = 0;
    if (offsetY) *offsetY = 0;
    if (!self || !self->m_d) return false;
    d = (const XGuiClientPrivate*)self->m_d;
    if (!d->m_fbValid || XWidget_width((XWidget*)self) <= 0 ||
        XWidget_height((XWidget*)self) <= 0)
        return false;
    s = d->m_viewScale;
    ox = d->m_viewOffX;
    oy = d->m_viewOffY;
    if (!(s > 0.0f)) return false;
    if (scale) *scale = s;
    if (offsetX) *offsetX = ox;
    if (offsetY) *offsetY = oy;
    return true;
}

/* ==================== 信号(GUI 线程发射) ==================== */

void* XGuiClient_connected_signal(XGuiClient* self)
{
    (void)self;
    return (void*)(size_t)XGuiClient_connected_signal;
}

void* XGuiClient_disconnected_signal(XGuiClient* self, int reason)
{
    (void)self;
    (void)reason;
    return (void*)(size_t)XGuiClient_disconnected_signal;
}

void* XGuiClient_remoteMetaChanged_signal(XGuiClient* self)
{
    (void)self;
    return (void*)(size_t)XGuiClient_remoteMetaChanged_signal;
}

void* XGuiClient_errorOccurred_signal(XGuiClient* self, int errorCode)
{
    (void)self;
    (void)errorCode;
    return (void*)(size_t)XGuiClient_errorOccurred_signal;
}

#endif /* XGUI_REMOTE_ON */
