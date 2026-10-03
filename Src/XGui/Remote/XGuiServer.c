/******************************************************************************
 * @file       XGuiServer.c
 * @brief      XGuiServer GUI 服务器实现(XGuiRemote.md §6 全部小节 +
 *             §3.7 握手时序 + §12 评审处置意见 1/3)。
 * @details    线程模型(§6.5):
 *               - GUI 线程: present 包装回调采集脏矩形入会话影子缓冲并标脏
 *                 tile(红线: 回调返回后后备缓冲翻转, 必须回调内拷贝, §6.2);
 *                 poll 回调做帧泵读/输入注入(§6.6)/按 txBudgetBytes 限预算
 *                 写出(§6.4);
 *               - 每会话独立编码线程: maxFps 认领门控(距上次认领
 *                 < 1000/maxFps 毫秒本轮不认领, 脏位保留零丢失仅延后,
 *                 §12 意见 3 冻结执行点; 框架呈现限频闸默认 0=不限,
 *                 XWidget.c:7310-7317, 不可依赖)→ 加锁逐 tile 认领拷出 →
 *                 无锁 FNV 去重/编码 → 有界队列(满丢最旧整批并记
 *                 sessionError);
 *               - 帧尾待写缓冲状态机(§12 意见 1): XGuiRemoteProto_writeFrame
 *                 部分写的余量入待写缓冲, poll 回调每圈按 txBudgetBytes 用
 *                 XIODevice_write_1 直写续传; 待写缓冲存在期间队列水位反压
 *                 编码线程(暂停认领); 拆帧纪律: 单帧 tile 载荷合计
 *                 ≤ txBudgetBytes, 单个超预算大 tile 独立成帧。
 *             已知边界(如实注记, 非静默绕过):
 *               - present 回调旧登记读取口缺失: XPlatformBackingStore 句柄
 *                 不透明且仅有 setPresentCallback(无 getter,
 *                 XPlatformBackingStore.h:319, 结构体定义私有的
 *                 XPlatformBackingStore.c:213)。经 grep 核实全树无既有
 *                 登记者, 故链调对(chainCb/chainData)在 V1 恒为
 *                 (NULL,NULL), host 语义="接管登记", unhost 语义="取消
 *                 登记"——链调结构与恢复逻辑完整保留, 平台层提供 getter
 *                 后一行接入即可。
 *               - TLS 服务端升级经 XTcpServer_setIncomingSocketFactory 在
 *                 描述符首次绑定前构造 XSslSocket(仓内设计注释明确以工厂
 *                 构造"最终派生套接字", 避免第二个对象接管已绑定描述符,
 *                 XTcpServer.c:149-168)——XSslSocket_create_ex /
 *                 XAbstractSocket_setSocketDescriptor_base(工厂路径内部
 *                 调用, XTcpServer.c:166-169)/ XSslSocket_startServerEncryption
 *                 三 API 依次生效于同一对象; 策略经环境变量
 *                 XGUI_REMOTE_TLS=1 开启(冻结头无运行期 setter)。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "XGuiServer.h"

#if XGUI_REMOTE_ON

#include "XGuiRemoteCodec.h"
#include "XMemory.h"
#include "XString.h"
#include "XDateTime.h"
#include "XAtomic.h"
#include "XThread.h"
#include "XMutex.h"
#include "XAbstractEventDispatcher.h"
#include "XAbstractNetIoRing.h" /* 基准优化: 编码入队后唤醒主循环等待(2026-10-02)。 */
#include "XTimer.h"             /* 帧泵兜底定时器(基准优化 2026-10-02)。 */
#include "XRandomGenerator.h"
#include "XCryptographicHash.h"
#include "XImage.h"
#include "XImageFormat.h"
#include <stdio.h>  /* snprintf: 标题定版缓存写入(2026-10-02 标题跟随修复)。 */
#include <string.h> /* strcmp: 标题变化比对(同修复)。 */
#include "XGeometry.h"
#include "XWidget.h"
#include "XBackingStore.h"
#include "XPlatformBackingStore.h"
#include "XWindow.h"

/* [2026-10-03 互联测试指挥官] 会话关闭逐次追踪(XGS_CLOSE_DEBUG=1 编译期
 * 启用): 真机会话周期性断链定位用, 生产零成本。 */
#ifndef XGS_CLOSE_DEBUG
#define XGS_CLOSE_DEBUG 0
#endif
#include "XWindowSystemInterface.h"
#include "XSystem.h"
#include <string.h>

#if XNETWORK_ON && XNETWORK_TCPSERVER_ON
#include "XTcpServer.h"
#include "XTcpSocket.h"
#endif /* XNETWORK_ON && XNETWORK_TCPSERVER_ON */

#if XGUI_REMOTE_TLS_ON && XNETWORK_ON && XNETWORK_TCPSERVER_ON
#include "XSslSocket.h"
#endif /* XGUI_REMOTE_TLS_ON && XNETWORK_ON && XNETWORK_TCPSERVER_ON */

/* ==================== 私有常量 ==================== */

/** @brief 默认最大并发会话数(冻结头约定)。 */
#define XGS_DEFAULT_MAX_SESSIONS   4
/** @brief poll 回调单圈单会话读块字节数。 */
#define XGS_READ_CHUNK             16384
/** @brief poll 回调单圈单会话最大读块数(防单会话饿死)。 */
#define XGS_READ_CHUNKS_PER_ROUND  4
/** @brief 编码线程空闲/反压轮询步长(毫秒)。 */
#define XGS_WORKER_IDLE_MS         2
/** @brief 会话关闭时编码线程 join 超时(毫秒)。 */
#define XGS_WORKER_JOIN_MS         3000
/** @brief 服务端名称(HELLO_ACK 携带)。 */
#define XGS_SERVER_NAME            "XGuiServer"
/** @brief TLS 服务端策略环境变量(恰为 "1" 时 accept 后升级 TLS)。 */
#define XGS_ENV_TLS                "XGUI_REMOTE_TLS"

/* ==================== 私有类型 ==================== */

/** @brief 编码队列节点: 一帧 FB_UPDATE 的完整负载(帧级头+tile 记录)。 */
typedef struct XgsQueueNode {
    struct XgsQueueNode* next;      /**< 链尾。 */
    uint32_t             epoch;     /**< 认领时的计划代际(换档/换尺寸后作废)。 */
    size_t               len;       /**< 负载字节数。 */
    /* uint8_t data[len] 负载紧跟其后(节点按 len 变长分配)。 */
} XgsQueueNode;

/** @brief 节点负载首址。 */
#define XGS_NODE_DATA(node) ((uint8_t*)((node) + 1))

/**
 * @brief 单条会话实现体(冻结头不暴露)。
 * @details 锁约定: m_qMutex 保护 影子缓冲/脏位图/计划参数/队列/停机与
 *          背压标志/代际; tile 哈希表为编码线程私有(免锁, 见
 *          xgs_workerMain); 其余字段单属主线程访问(GUI 线程为主)。
 */
typedef struct XgsSession {
    struct XgsSession*   next;              /**< 会话链(仅 GUI 线程)。 */
    int                  id;                /**< 会话 id(>0, 单调)。 */
    XGuiServer*          owner;             /**< 归属服务器(借用)。 */
    XIODevice*           device;            /**< 传输设备(借用或拥有)。 */
    bool                 ownDevice;         /**< true=拥有设备(close+delete)。 */
    bool                 closed;            /**< 关闭中/已关闭(仅 GUI 线程)。 */
    bool                 threadStarted;     /**< 编码线程曾成功启动。 */
    XGuiRemoteSessionState state;           /**< 协议状态机(仅 GUI 线程)。 */

    XGuiRemoteFrameReader reader;           /**< 增量帧泵(GUI 线程)。 */
    uint8_t              bannerBuf[XGUI_REMOTE_BANNER_BYTES]; /**< 横幅攒字节。 */
    int                  bannerGot;         /**< 已攒横幅字节数。 */

    uint32_t             caps;              /**< 协商后的能力交集(GUI 写)。 */
    uint8_t              nonce[XGUI_REMOTE_AUTH_NONCE_BYTES]; /**< 挑战 nonce。 */

    /* ---- 计划参数/影子/队列(m_qMutex) ---- */
    XMutex*              m_qMutex;          /**< 会话锁(堆对象, XMutex 结构不透明)。 */
    XGuiRemoteProfile    profile;           /**< 本会话生效档位(已按能力夹取)。 */
    XGuiRemoteProfileId  profileId;         /**< 生效档位 id。 */
    uint32_t             epoch;             /**< 计划代际(换档/换尺寸 +1)。 */
    bool                 stopThread;        /**< 编码线程停机标志。 */
    bool                 backpressure;      /**< 帧尾待写缓冲存在(GUI 写, 线程读)。 */
    int64_t              lastClaimMs;       /**< 上次认领时刻(maxFps 门控基准)。 */
    bool                 allTilesDirty;     /**< 全量刷新请求(FB_REQUEST/换档/resize)。 */
    XAtomic_bool         hasDamage;         /**< 有新伤害(采集置位, 线程认领清零)。 */
    XgsQueueNode*        qHead;             /**< 编码队列头(最旧)。 */
    XgsQueueNode*        qTail;             /**< 编码队列尾(最新)。 */
    size_t               qBytes;            /**< 队列当前字节水位。 */
    int                  droppedBatches;    /**< 队列满丢批计数。 */

    XImage               shadow;            /**< 影子缓冲(档位线上格式)。 */
    bool                 shadowInited;      /**< shadow 已分配。 */
    int                  shadowW;           /**< 影子宽。 */
    int                  shadowH;           /**< 影子高。 */
    uint8_t*             dirtyGrid;         /**< 脏 tile 位图(gridW*gridH 位)。 */
    int                  gridW;             /**< tile 列数。 */
    int                  gridH;             /**< tile 行数。 */

    /* ---- GUI 线程私有 ---- */
    bool                 metaPending;       /**< 待发 FB_META(尺寸/档位变化)。 */
    bool                 switchPending;     /**< 待生效档位切换。 */
    XGuiRemoteProfile    pendingProfile;    /**< 切换目标档位。 */
    XGuiRemoteProfileId  pendingProfileId;  /**< 切换目标档位 id。 */
    int                  lastSeenDropped;   /**< 已上报的丢批计数。 */
    uint8_t*             tailBuf;           /**< 帧尾待写缓冲(精确余量大小)。 */
    size_t               tailLen;           /**< 待写字节数。 */
    int64_t              lastPeerActivityMs;/**< 最近对端字节时刻(保活)。 */
    int64_t              lastPingSentMs;    /**< 最近 PING 时刻。 */
    char                 lastTitle[XGUI_REMOTE_MAX_NAME_BYTES + 1];
                                            /**< 已定版标题(FB_META 跟随判据;
                                             *   2026-10-02 修复: 标题变化
                                             *   原不触发 metaPending, 客户端
                                             *   标题跟随失效——E2E 键盘往返
                                             *   字符串断言依赖此路径)。 */

    /* ---- 编码线程 ---- */
    XThread*             thread;            /**< 线程对象(拥有)。 */
    XVarList*            threadArgs;        /**< 线程参数(close 在 join 后统一释放)。 */
} XgsSession;

/** @brief XGuiServer 私有实现块(冻结头不暴露)。 */
typedef struct XGuiServerPrivate
{
    XWidget*               hosted;          /**< 镜像目标顶层控件(借用)。 */
    XPlatformBackingStore* store;           /**< 目标平台后备存储(借用)。 */
    XPlatformBackingStorePresentFn chainCb; /**< 被包装的旧 present 回调(V1 恒 NULL, 见文件头注记)。 */
    void*                  chainData;       /**< 旧回调用户数据(V1 恒 NULL)。 */
    XHandle                pumpHandle;      /**< poll 回调登记句柄。 */
    XTimer*                pumpTimer;       /**< 帧泵兜底定时器(基准优化 2026-10-02:
                                                 事件循环空闲时 poll 回调的驱动间隔
                                                 受网络完成事件唤醒可靠性影响, 实测
                                                 resource 档 send 间隔与收侧延迟散布
                                                 52~170ms; 8ms 兜底定时器把泵圈间隔
                                                 钳到 ≤8ms。仅 STREAMING 会话存在
                                                 期间开销, 空闲回调近零成本)。 */
    XgsSession*            sessionHead;     /**< 活跃会话链(仅 GUI 线程)。 */
    int                    sessionCount;    /**< 活跃会话数。 */
    int                    maxSessions;     /**< 最大并发会话数。 */
    int                    nextSessionId;   /**< 会话 id 分配器。 */
    XGuiRemoteAuthMethod   authMethod;      /**< 认证方法(默认 NONE)。 */
    uint8_t                passwordHash[XGUI_REMOTE_AUTH_RESPONSE_BYTES]; /**< 口令 SHA-256(原文不留存)。 */
    bool                   hasPassword;     /**< 已设口令。 */
    XGuiRemoteProfile      profile;         /**< 服务器档位参数。 */
    XGuiRemoteProfileId    profileId;       /**< 服务器档位 id。 */
    bool                   allowClientProfile; /**< 是否允许客户端切档(默认 false)。 */
    bool                   tlsAccept;       /**< listen 时定版的 TLS 策略。 */
    int                    tlsOverride;     /**< 运行期 TLS 显式覆盖(-1=未设→
                                                 回退环境变量, 0=关, 1=开;
                                                 加法式扩展, 未设时与原
                                                 env 口径逐字节一致)。 */
    char*                  tlsCertFile;     /**< TLS 证书文件路径 utf8 副本(拥有; NULL=未设)。 */
    char*                  tlsKeyFile;      /**< TLS 私钥文件路径 utf8 副本(拥有; NULL=未设)。 */
    bool                   metaBroadcastPending; /**< 本泵圈有 FB_META 发出(信号延后)。 */
#if XNETWORK_ON && XNETWORK_TCPSERVER_ON
    XTcpServer*            tcpServer;       /**< TCP 监听器(拥有, parent=self)。 */
#endif
} XGuiServerPrivate;

/* ==================== 前向声明 ==================== */

static void xgs_sessionClose(XGuiServer* self, XgsSession* s,
                             int byeReason, int emitReason);
static XWindow* xgs_targetWindow(XGuiServer* self);

/* ==================== 小工具 ==================== */

static int xgs_minInt(int a, int b) { return a < b ? a : b; }
static int xgs_maxInt(int a, int b) { return a > b ? a : b; }
static size_t xgs_minSize(size_t a, size_t b) { return a < b ? a : b; }
static size_t xgs_maxSize(size_t a, size_t b) { return a > b ? a : b; }

/** @brief 单调毫秒时钟(保活/maxFps 门控共用; 全库统一计时源)。 */
static int64_t xgs_nowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/** @brief 线上格式字节宽。 */
static int xgs_bppOf(XGuiRemotePixelFormat pf)
{
    return (pf == XGUI_REMOTE_PF_RGB565) ? 2 : 4;
}

/** @brief 线上格式 → 影子缓冲 XImageFormat。 */
static XImageFormat xgs_imgFormatOf(XGuiRemotePixelFormat pf)
{
    return (pf == XGUI_REMOTE_PF_RGB565) ? XImageFormat_RGB16
                                         : XImageFormat_ARGB32;
}

/** @brief 后备表面格式 → 线上格式(§10 风险表: 采集链不假设源格式;
 *        转换矩阵覆盖 ARGB32/RGB565; 预乘 ARGB32 按直读 ARGB32 处理,
 *        控件像素以不透明为主, 半透明像素色彩与直读口径一致)。 */
static XGuiRemotePixelFormat xgs_pfOfImageFormat(XImageFormat f)
{
    if (f == XImageFormat_RGB16) return XGUI_REMOTE_PF_RGB565;
    return XGUI_REMOTE_PF_ARGB32;
}

/** @brief 服务器广播能力位(§3.5)。 */
static uint32_t xgs_serverCaps(void)
{
    uint32_t caps = (uint32_t)XGUI_REMOTE_CAP_RGB565 |
                    (uint32_t)XGUI_REMOTE_CAP_TOUCH |
                    (uint32_t)XGUI_REMOTE_CAP_IME |
                    (uint32_t)XGUI_REMOTE_CAP_PROFILE_SET;
    if (XGuiRemoteCodec_hasCodec(XGUI_REMOTE_CODEC_ZLIB))
        caps |= (uint32_t)XGUI_REMOTE_CAP_ZLIB;
#if XGUI_REMOTE_TLS_ON
    caps |= (uint32_t)XGUI_REMOTE_CAP_TLS;
#endif
    return caps;
}

/** @brief 读取 TLS 服务端策略(listen 时定版)。 */
static bool xgs_tlsPolicyFromEnv(void)
{
#if XGUI_REMOTE_TLS_ON
    const char* v = XSystem_environment(XGS_ENV_TLS);
    return (v != NULL && v[0] == '1' && v[1] == '\0');
#else
    (void)0;
    return false;
#endif
}

/** @brief 解析生效 TLS 策略: 运行期显式设置(setTlsEnabled)优先,
 *         从未设置时回退环境变量(原口径, 行为不变)。 */
static bool xgs_tlsPolicyOf(const XGuiServerPrivate* d)
{
    if (d && d->tlsOverride >= 0) return d->tlsOverride == 1;
    return xgs_tlsPolicyFromEnv();
}

#if XGUI_REMOTE_TLS_ON
/** @brief 试装载校验: 证书/私钥文件能否被 XSslSocket 成功装载(PEM)。 */
static bool xgs_tlsTryLoadFiles(const char* certPath, const char* keyPath)
{
    XSslSocket* probe;
    XString* certStr;
    XString* keyStr;
    bool ok = false;
    probe = XSslSocket_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (!probe) return false;
    certStr = XString_create_utf8(certPath);
    keyStr = keyPath ? XString_create_utf8(keyPath) : NULL;
    if (certStr && keyStr) {
        XSslSocket_setLocalCertificate_2(probe, certStr, XSSL_Pem);
        XSslSocket_setPrivateKey_2(probe, keyStr, XSSL_KeyAlgorithm_Rsa,
                                   XSSL_Pem, NULL);
        ok = XSslSocket_localCertificate(probe) != NULL &&
             XSslSocket_privateKey(probe) != NULL;
    }
    if (certStr) XClassDelete((XClass*)certStr);
    if (keyStr) XClassDelete((XClass*)keyStr);
    XClassDelete((XClass*)probe);
    return ok;
}
#endif /* XGUI_REMOTE_TLS_ON */

/* ==================== 影子帧缓冲与脏位图 ==================== */

/** @brief 按 rect(影子/缓冲坐标)标记覆盖的脏 tile; 调用方持有 m_qMutex。 */
static void xgs_markDirtyRect(XgsSession* s, int bx, int by, int bw, int bh)
{
    int tw = xgs_maxInt(s->profile.tileWidth, 1);
    int th = xgs_maxInt(s->profile.tileHeight, 1);
    int gx0, gy0, gx1, gy1, gx, gy;
    if (!s->dirtyGrid || bw <= 0 || bh <= 0) return;
    gx0 = xgs_maxInt(bx, 0) / tw;
    gy0 = xgs_maxInt(by, 0) / th;
    gx1 = xgs_minInt((bx + bw - 1) / tw, s->gridW - 1);
    gy1 = xgs_minInt((by + bh - 1) / th, s->gridH - 1);
    for (gy = gy0; gy <= gy1; ++gy) {
        for (gx = gx0; gx <= gx1; ++gx) {
            int i = gy * s->gridW + gx;
            s->dirtyGrid[i >> 3] |= (uint8_t)(1u << (i & 7));
        }
    }
}

/**
 * @brief  (重)分配会话影子缓冲为指定尺寸/当前计划格式。
 * @param  convertOld 同尺寸且格式变化时逐行转换保留旧内容(档位切换);
 *                    尺寸变化时内容作废(resize 帧天然全量, §6.2)。
 * @return true 成功; 失败保持原状。
 * @note   成功后置 allTilesDirty + metaPending + 代际 +1(在途批次作废)。
 *         调用方持有 m_qMutex。
 */
static bool xgs_sessionReallocShadowLocked(XgsSession* s, int w, int h,
                                           bool convertOld)
{
    XGuiRemotePixelFormat pf = s->profile.wireFormat;
    XImageFormat imgFmt = xgs_imgFormatOf(pf);
    int tw = xgs_maxInt(s->profile.tileWidth, 1);
    int th = xgs_maxInt(s->profile.tileHeight, 1);
    int gridW, gridH, tiles, gridBytes;
    bool formatChanged;
    if (w <= 0 || h <= 0 || w > 0xFFFF || h > 0xFFFF) return false;

    gridW = (w + tw - 1) / tw;
    gridH = (h + th - 1) / th;
    tiles = gridW * gridH;
    gridBytes = (tiles + 7) / 8;
    formatChanged = s->shadowInited &&
                    (xgs_pfOfImageFormat(XImage_format(&s->shadow)) != pf);

    if (s->shadowInited && s->shadowW == w && s->shadowH == h &&
        !formatChanged) {
        return true; /* 尺寸与格式均已一致。 */
    }

    if (s->shadowInited && convertOld && formatChanged &&
        s->shadowW == w && s->shadowH == h) {
        /* 同尺寸换档: 逐行转换旧内容, 保留最后一帧画面。 */
        XGuiRemotePixelFormat oldPf =
            xgs_pfOfImageFormat(XImage_format(&s->shadow));
        int srcStride = XImage_bytesPerLine(&s->shadow);
        XImage converted;
        XImage_init(&converted);
        if (XImage_reinit_ex(&converted, w, h, imgFmt)) {
            int y;
            int dstStride = XImage_bytesPerLine(&converted);
            for (y = 0; y < h; ++y) {
                XGuiRemoteCodec_convertPixels(
                    XImage_bits(&s->shadow) + (size_t)y * (size_t)srcStride,
                    oldPf,
                    XImage_bits(&converted) + (size_t)y * (size_t)dstStride,
                    pf, w);
            }
            XClassDeinit(&s->shadow);
            s->shadow = converted;
        } else {
            XClassDeinit(&converted);
        }
    } else if (s->shadowInited) {
        XClassDeinit(&s->shadow);
        XImage_init(&s->shadow);
    }

    if (!XImage_reinit_ex(&s->shadow, w, h, imgFmt)) {
        s->shadowInited = false;
        s->shadowW = 0;
        s->shadowH = 0;
        return false;
    }
    s->shadowInited = true;
    s->shadowW = w;
    s->shadowH = h;

    if (s->dirtyGrid) XFree_System(s->dirtyGrid);
    s->dirtyGrid = (uint8_t*)XMalloc_System((size_t)gridBytes);
    s->gridW = gridW;
    s->gridH = gridH;
    if (s->dirtyGrid) XMemset(s->dirtyGrid, 0, (size_t)gridBytes);

    s->epoch++;              /* 在途批次作废, 防新旧格式帧混流。 */
    s->allTilesDirty = true; /* 新缓冲按全量刷新兜底(最终一致性)。 */
    s->metaPending = true;   /* 尺寸/格式参数已变化, FB_META 全量跟随。 */
    return true;
}

/* ==================== 会话帧发送(GUI 线程) ==================== */

/**
 * @brief  帧尾待写缓冲: 把完整帧从 accepted 起的余量挂入 tail(精确尺寸)。
 * @note   frameBytes 为完整帧(5 字节帧头+负载); 余量丢失仅发生于分配
 *         失败(极端), 不影响协议其余部分。
 */
static void xgs_stashTail(XgsSession* s, const uint8_t* frameBytes,
                          size_t total, size_t accepted)
{
    size_t rem = total - accepted;
    uint8_t* buf;
    if (rem == 0) return;
    buf = (uint8_t*)XMalloc_System(rem);
    if (!buf) return;
    memcpy(buf, frameBytes + accepted, rem);
    if (s->tailBuf) XFree_System(s->tailBuf);
    s->tailBuf = buf;
    s->tailLen = rem;
}

/**
 * @brief  组帧发送: 返回 false=链路硬错误(调用方断链)。
 * @note   部分写为背压: 余量入帧尾待写缓冲(§12 意见 1 冻结契约);
 *         待写缓冲未清空时不组新帧(背压纪律)。
 */
static bool xgs_sessionSendFrame(XgsSession* s, XGuiRemoteMsgType type,
                                 const void* payload, size_t payloadBytes)
{
    int64_t accepted;
    size_t total;
    if (s->tailBuf) return true;
    accepted = XGuiRemoteProto_writeFrame(s->device, type, payload,
                                          payloadBytes);
    if (accepted < 0) return false;
    total = (size_t)XGUI_REMOTE_FRAME_HEADER_BYTES + payloadBytes;
    if ((size_t)accepted < total) {
        /* 余量重建: [u32 len][u8 type][payload] 自 accepted 截取。 */
        uint8_t* frame = (uint8_t*)XMalloc_System(total);
        if (frame) {
            XGuiRemoteProto_putU32(frame, (uint32_t)payloadBytes);
            frame[4] = (uint8_t)type;
            if (payloadBytes != 0)
                memcpy(frame + XGUI_REMOTE_FRAME_HEADER_BYTES, payload,
                       payloadBytes);
            xgs_stashTail(s, frame, total, (size_t)accepted);
            XFree_System(frame);
        }
    }
    return true;
}

/** @brief 发送本会话横幅(8B; 部分写余量入待写缓冲续传, §3.3/§3.7)。 */
static bool xgs_sessionSendBanner(XgsSession* s)
{
    uint8_t banner[XGUI_REMOTE_BANNER_BYTES];
    int64_t accepted;
    XGuiRemoteProto_makeBanner(banner);
    accepted = XIODevice_write_1(s->device, (const char*)banner,
                                 (int64_t)XGUI_REMOTE_BANNER_BYTES);
    if (accepted < 0) return false;
    if ((size_t)accepted < XGUI_REMOTE_BANNER_BYTES)
        xgs_stashTail(s, banner, XGUI_REMOTE_BANNER_BYTES, (size_t)accepted);
    return true;
}

/** @brief 发 BYE(尽力而为; 关闭路径不再维护待写缓冲)。 */
static void xgs_sendBye(XgsSession* s, XGuiRemoteByeReason reason,
                        const char* text)
{
    XGuiRemoteMsgBye bye;
    uint8_t buf[XGUI_REMOTE_MAX_MSG_BYTES + 16];
    size_t len;
    size_t n;
    XMemset(&bye, 0, sizeof(bye));
    bye.reason = (uint8_t)reason;
    if (text) {
        len = strlen(text);
        if (len > XGUI_REMOTE_MAX_MSG_BYTES) len = XGUI_REMOTE_MAX_MSG_BYTES;
        memcpy(bye.text, text, len);
        bye.textBytes = (uint16_t)len;
    }
    n = XGuiRemoteProto_encBye(buf, sizeof(buf), &bye);
    if (n > 0) {
        (void)XGuiRemoteProto_writeFrame(s->device, XGUI_REMOTE_MSG_BYE,
                                         buf, n);
    }
}

/** @brief 构建 FB_META(尺寸/格式/tile/档位/标题; §6.2 标题口径)。 */
static void xgs_buildFbMeta(XGuiServer* self, XgsSession* s,
                            XGuiRemoteMsgFbMeta* meta)
{
    XWindow* win;
    XString* title;
    XMemset(meta, 0, sizeof(*meta));
    XMutex_lock(s->m_qMutex);
    meta->width = (uint16_t)xgs_maxInt(s->shadowW, 0);
    meta->height = (uint16_t)xgs_maxInt(s->shadowH, 0);
    meta->format = (uint8_t)s->profile.wireFormat;
    meta->tileWidth = (uint16_t)xgs_maxInt(s->profile.tileWidth, 1);
    meta->tileHeight = (uint16_t)xgs_maxInt(s->profile.tileHeight, 1);
    meta->profileId = (uint8_t)s->profileId;
    meta->flags = 0x01u; /* bit0=标题字段有效。 */
    XMutex_unlock(s->m_qMutex);
#if defined(XGS_CLOSE_DEBUG) && XGS_CLOSE_DEBUG
    fprintf(stderr, "XGS_FBMETA %ux%u tile=%ux%u fmt=%u\n",
            (unsigned)meta->width, (unsigned)meta->height,
            (unsigned)meta->tileWidth, (unsigned)meta->tileHeight,
            (unsigned)meta->format);
#endif
    win = xgs_targetWindow(self);
    title = win ? XWindow_title(win) : NULL;
    if (title) {
        const char* utf8 = XString_toUtf8(title);
        size_t len = utf8 ? strlen(utf8) : 0;
        if (len > XGUI_REMOTE_MAX_NAME_BYTES) len = XGUI_REMOTE_MAX_NAME_BYTES;
        memcpy(meta->title, utf8, len);
        meta->titleBytes = (uint16_t)len;
        XClassDelete(title);
    }
}

/** @brief 发送 FB_META; 返回 false=链路硬错误。 */
static bool xgs_sessionSendFbMeta(XGuiServer* self, XgsSession* s)
{
    XGuiRemoteMsgFbMeta meta;
    uint8_t buf[XGUI_REMOTE_MAX_NAME_BYTES + 16];
    size_t n;
    xgs_buildFbMeta(self, s, &meta);
    n = XGuiRemoteProto_encFbMeta(buf, sizeof(buf), &meta);
    if (n == 0) {
        s->metaPending = false;
        return true; /* 编码失败防御: 不致命。 */
    }
    if (!xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_FB_META, buf, n))
        return false;
    s->metaPending = false;
    return true;
}

/* ==================== 输入注入(§6.6) ==================== */

/** @brief 目标窗口 = 镜像顶层控件窗口句柄(XWidget.h:1125); 无句柄丢弃。 */
static XWindow* xgs_targetWindow(XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d) || !d->hosted)
        return NULL;
    return XWidget_windowHandle(d->hosted);
}

/** @brief i16 坐标裁剪到窗口客户区边界(§7.4: 注入前裁剪)。 */
static void xgs_clampToWindow(XGuiServer* self, int16_t x, int16_t y,
                              int* outX, int* outY)
{
    XGuiServerPrivate* d;
    int w = 1, h = 1;
    if (self && (d = (XGuiServerPrivate*)self->m_d) && d->hosted) {
        w = xgs_maxInt(XWidget_width(d->hosted), 1);
        h = xgs_maxInt(XWidget_height(d->hosted), 1);
    }
    *outX = xgs_minInt(xgs_maxInt((int)x, 0), w - 1);
    *outY = xgs_minInt(xgs_maxInt((int)y, 0), h - 1);
}

static void xgs_injectPointer(XGuiServer* self,
                              const XGuiRemoteMsgInputPointer* m)
{
    XWindow* win = xgs_targetWindow(self);
    XEventType type;
    XPoint pos;
    int cx, cy;
    if (!win || !m) return;
    switch (m->action) {
    case XGUI_REMOTE_PTR_PRESS:     type = XEVENT_TYPE_MOUSE_BUTTON_PRESS; break;
    case XGUI_REMOTE_PTR_RELEASE:   type = XEVENT_TYPE_MOUSE_BUTTON_RELEASE; break;
    case XGUI_REMOTE_PTR_DBL_CLICK: type = XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK; break;
    case XGUI_REMOTE_PTR_MOVE:      type = XEVENT_TYPE_MOUSE_MOVE; break;
    default: return; /* 封闭枚举: dec 已拦截, 双重防御。 */
    }
    xgs_clampToWindow(self, m->x, m->y, &cx, &cy);
    pos.x = cx;
    pos.y = cy;
    /* 坐标为窗口客户区逻辑坐标, globalPosition 传 NULL, 不补 DPR。 */
    (void)XWindowSystemInterface_handleMouseEvent_ex(
        win, type, (XMouseButton)m->button, (XMouseButton)m->buttons,
        (XKeyboardModifiers)m->modifiers, pos, NULL, m->timestampMs);
}

static void xgs_injectKey(XGuiServer* self, const XGuiRemoteMsgInputKey* m)
{
    XWindow* win = xgs_targetWindow(self);
    XEventType type;
    if (!win || !m) return;
    if (m->action == XGUI_REMOTE_KEY_PRESS) type = XEVENT_TYPE_KEY_PRESS;
    else if (m->action == XGUI_REMOTE_KEY_RELEASE) type = XEVENT_TYPE_KEY_RELEASE;
    else return;
    (void)XWindowSystemInterface_handleKeyEvent_ex(
        win, type, (int)m->key, (XKeyboardModifiers)m->modifiers,
        false, m->nativeScanCode, m->timestampMs);
}

static void xgs_injectWheel(XGuiServer* self, const XGuiRemoteMsgInputWheel* m)
{
    XWindow* win = xgs_targetWindow(self);
    XPoint pos;
    XPoint angle;
    int cx, cy;
    if (!win || !m) return;
    xgs_clampToWindow(self, m->x, m->y, &cx, &cy);
    pos.x = cx;
    pos.y = cy;
    angle.x = m->angleX;
    angle.y = m->angleY;
    (void)XWindowSystemInterface_handleWheelEvent(
        win, (XMouseButton)m->buttons, (XKeyboardModifiers)m->modifiers,
        pos, &angle);
}

static void xgs_injectTouch(XGuiServer* self, const XGuiRemoteMsgInputTouch* m)
{
    XWindow* win = xgs_targetWindow(self);
    XTouchPoint points[XGUI_REMOTE_MAX_TOUCH_POINTS];
    XEventType type;
    int count, i, cx, cy;
    if (!win || !m || m->pointCount <= 0) return;
    count = xgs_minInt(m->pointCount, XGUI_REMOTE_MAX_TOUCH_POINTS);
    switch (m->action) {
    case XGUI_REMOTE_TOUCH_BEGIN:  type = XEVENT_TYPE_TOUCH_BEGIN; break;
    case XGUI_REMOTE_TOUCH_UPDATE: type = XEVENT_TYPE_TOUCH_UPDATE; break;
    case XGUI_REMOTE_TOUCH_END:    type = XEVENT_TYPE_TOUCH_END; break;
    case XGUI_REMOTE_TOUCH_CANCEL: type = XEVENT_TYPE_TOUCH_CANCEL; break;
    default: return;
    }
    for (i = 0; i < count; ++i) {
        const XGuiRemoteMsgTouchPoint* p = &m->points[i];
        xgs_clampToWindow(self, p->x, p->y, &cx, &cy);
        points[i].m_id = (int32_t)p->id;
        points[i].m_state = (int)p->state;
        points[i].m_position.x = cx;
        points[i].m_position.y = cy;
        points[i].m_globalPosition.x = cx;
        points[i].m_globalPosition.y = cy;
        points[i].m_pressure = (float)p->pressureQ8 / 255.0f;
    }
    /* 主点放 points[0](对齐注入契约, §6.6)。 */
    (void)XWindowSystemInterface_handleTouchPoints_ex(win, type, points,
                                                      count, m->timestampMs);
}

static void xgs_injectIme(XGuiServer* self, const XGuiRemoteMsgInputIme* m)
{
    XWindow* win = xgs_targetWindow(self);
    char preedit[XGUI_REMOTE_MAX_TEXT_BYTES + 1];
    char commit[XGUI_REMOTE_MAX_TEXT_BYTES + 1];
    if (!win || !m) return;
    memcpy(preedit, m->preedit, (size_t)m->preeditBytes);
    preedit[m->preeditBytes] = '\0';
    memcpy(commit, m->commit, (size_t)m->commitBytes);
    commit[m->commitBytes] = '\0';
    (void)XWindowSystemInterface_handleInputMethodEvent(
        win, preedit, commit, m->replacementStart, m->replacementLength,
        m->cursorPosition, m->anchorPosition);
}

/* ==================== present 采集(GUI 线程, 红线路径) ==================== */

/* 基准追踪(XGUI_REMOTE_BENCH_TRACE=1 启用; 2026-10-02 延迟基准诊断用,
 * stderr 单行, 未启用时一次 getenv 判断零成本——与演示页采样器同纪律)。 */
static int xgs_benchTrace = -1;
#define XGS_BENCH_TRACE_ON() \
    (xgs_benchTrace < 0 ? (xgs_benchTrace = \
        (XSystem_environment("XGUI_REMOTE_BENCH_TRACE") != NULL)) \
                        : xgs_benchTrace)

/**
 * @brief  把 flushedRegion(窗口坐标)逐矩形转格式拷入会话影子缓冲并标脏。
 * @note   在 present 包装回调内调用——回调返回后后备缓冲翻转, 必须回调内
 *         拷贝(设计红线 §6.2)。锁内完成本会话全部矩形拷贝与标脏。
 */
static void xgs_captureForSession(XgsSession* s, XImage* img,
                                  const XRegion* region,
                                  const XPoint* offset)
{
    int w = XImage_width(img);
    int h = XImage_height(img);
    int srcStride = XImage_bytesPerLine(img);
    XGuiRemotePixelFormat srcPf = xgs_pfOfImageFormat(XImage_format(img));
    int srcBpp = xgs_bppOf(srcPf);
    const uint8_t* srcBits = XImage_bits(img);
    int ox = offset ? offset->x : 0;
    int oy = offset ? offset->y : 0;
    int dstBpp, dstStride, r;

    XMutex_lock(s->m_qMutex);
    if (!s->shadowInited || s->shadowW != w || s->shadowH != h) {
        /* 首帧建缓冲 / 尺寸变化(§6.2): 重分配, FB_META 全量跟随。 */
        if (!xgs_sessionReallocShadowLocked(s, w, h, false)) {
            XMutex_unlock(s->m_qMutex);
            return;
        }
    }
    dstBpp = xgs_bppOf(s->profile.wireFormat);
    dstStride = XImage_bytesPerLine(&s->shadow);

    for (r = 0; r < region->count; ++r) {
        XRect rect = region->rects[r];
        /* 窗口坐标 → 缓冲坐标(offset 为后备存储原点相对窗口的偏移)。 */
        int bx = rect.x - ox;
        int by = rect.y - oy;
        int bw = rect.width;
        int bh = rect.height;
        int y;
        if (bx < 0) { bw += bx; bx = 0; }
        if (by < 0) { bh += by; by = 0; }
        if (bx + bw > w) bw = w - bx;
        if (by + bh > h) bh = h - by;
        if (bw <= 0 || bh <= 0 || !srcBits) continue;
        for (y = 0; y < bh; ++y) {
            const uint8_t* srcRow = srcBits +
                    (size_t)(by + y) * (size_t)srcStride +
                    (size_t)bx * (size_t)srcBpp;
            uint8_t* dstRow = XImage_bits(&s->shadow) +
                    (size_t)(by + y) * (size_t)dstStride +
                    (size_t)bx * (size_t)dstBpp;
            XGuiRemoteCodec_convertPixels(srcRow, srcPf, dstRow,
                                          s->profile.wireFormat, bw);
        }
        xgs_markDirtyRect(s, bx, by, bw, bh);
    }
    XAtomic_store_bool(&s->hasDamage, true, XAtomic_MemoryOrder_Release);
    XMutex_unlock(s->m_qMutex);
    if (XGS_BENCH_TRACE_ON())
        fprintf(stderr, "XGS_TRACE present t=%llu tiles=%d\n",
                (unsigned long long)(uint64_t)xgs_nowMs(), region->count);
}

/** @brief present 包装回调: 采集(红线内)→ 链调被包装的旧回调。 */
static void xgs_presentWrapper(void* userData, XPlatformBackingStore* store,
                               const XRegion* flushedRegion,
                               const XPoint* offset)
{
    XGuiServer* self = (XGuiServer*)userData;
    XGuiServerPrivate* d;
    XgsSession* s;
    XImage* img;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    if (store != d->store || !flushedRegion || flushedRegion->count <= 0) {
        if (d->chainCb) d->chainCb(d->chainData, store, flushedRegion, offset);
        return;
    }
    img = XPlatformBackingStore_paintDevice(store);
    if (img) {
        s = d->sessionHead;
        while (s) {
            if (!s->closed && s->state == XGUI_REMOTE_STATE_STREAMING)
                xgs_captureForSession(s, img, flushedRegion, offset);
            s = s->next;
        }
    }
    if (d->chainCb) d->chainCb(d->chainData, store, flushedRegion, offset);
}

/* ==================== 编码线程(每会话 1 个, §6.4) ==================== */

/** @brief 线程私有缓冲容量确保(调用方持有 m_qMutex; 失败保持原状)。 */
static bool xgs_ensureBufLocked(void** buf, size_t* cap, size_t need)
{
    void* nb;
    if (*buf && *cap >= need) return true;
    nb = XMalloc_System(need ? need : 1);
    if (!nb) return false;
    if (*buf) XFree_System(*buf);
    *buf = nb;
    *cap = need;
    return true;
}

/** @brief 冲一批入有界队列(调用方持有 m_qMutex; 满丢最旧整批并计数)。 */
static void xgs_flushBatchLocked(XgsSession* s, const uint8_t* batchBuf,
                                 size_t batchLen, uint32_t epoch)
{
    XgsQueueNode* node;
    if (batchLen <= (size_t)XGUI_REMOTE_FB_UPDATE_HEADER_BYTES) return;
    node = (XgsQueueNode*)XMalloc_System(sizeof(XgsQueueNode) + batchLen);
    if (!node) return;
    node->next = NULL;
    node->epoch = epoch;
    node->len = batchLen;
    memcpy(XGS_NODE_DATA(node), batchBuf, batchLen);
    if (s->qTail) s->qTail->next = node;
    else s->qHead = node;
    s->qTail = node;
    s->qBytes += node->len;
    /* 有界队列: 满丢最旧整批并记 sessionError(计数由 GUI 泵上报, §6.4)。 */
    while (s->qHead && s->qHead->next && s->qBytes > s->profile.encodeQueueBytes) {
        XgsQueueNode* dead = s->qHead;
        s->qHead = dead->next;
        s->qBytes -= dead->len;
        XFree_System(dead);
        s->droppedBatches++;
    }
}

/**
 * @brief  编码线程主循环: maxFps 认领门控 → 加锁逐 tile 认领 → 无锁
 *         FNV 去重/编码 → 拆帧组批 → 有界队列。
 * @note   tile 哈希表为线程私有: 与 GUI 的代际(epoch)比对检测换档/
 *         resize, 代际变化即整表作废(免锁且无与 GUI 的哈希表竞争)。
 */
static void xgs_workerMain(XThread* thread, XVarList* arguments)
{
    XgsSession* s = NULL;
    uint8_t* workBuf = NULL;   /* 认领 tile 槽区(线程私有)。 */
    size_t workCap = 0;
    int* idxBuf = NULL;        /* 认领 tile 索引(线程私有)。 */
    size_t idxCap = 0;
    uint8_t* encBuf = NULL;    /* 单 tile 编码输出(线程私有)。 */
    size_t encCap = 0;
    uint8_t* batchBuf = NULL;  /* 批组帧负载(线程私有)。 */
    size_t batchCap = 0;
    uint32_t* hashTab = NULL;  /* 上一轮 tile 哈希(线程私有)。 */
    size_t hashCap = 0;
    uint8_t* hashValid = NULL; /* 哈希表有效位图(防内容恰哈希 0 的假跳过)。 */
    size_t hashValidCap = 0;
    uint32_t lastEpoch = 0;    /* 哈希表所属代际。 */
    uint32_t fbSequence = 0;   /* FB_UPDATE 序号(单调递增, 回绕允许)。 */

    (void)thread;
    if (!arguments) return;
    XVarList_args_1(arguments, void*, sessionPtr);
    s = (XgsSession*)sessionPtr;
    if (!s) return;

    for (;;) {
        XGuiRemoteProfile plan;
        uint32_t epoch;
        int64_t now;
        int64_t minIntervalMs;
        int bpp, tw, th, gridW, gridH, shadowW, shadowH;
        bool full;
        int claimed = 0;
        size_t slotBytes;
        int srcStride;
        const uint8_t* srcBits;
        int gx, gy, i;
        size_t payloadBudget;
        size_t batchLen;
        int batchTiles;
        int gx0, gy0, tw0, th0;

        XMutex_lock(s->m_qMutex);
        if (s->stopThread) {
            XMutex_unlock(s->m_qMutex);
            break;
        }
        if (s->backpressure ||
            (!XAtomic_load_bool(&s->hasDamage, XAtomic_MemoryOrder_Acquire) &&
             !s->allTilesDirty)) {
            /* 队列水位反压(待写缓冲存在, §12 意见 1)或无伤害: 暂停认领。 */
            XMutex_unlock(s->m_qMutex);
            XThread_msleep(XGS_WORKER_IDLE_MS);
            continue;
        }
        plan = s->profile;
        epoch = s->epoch;
        now = xgs_nowMs();
        /* maxFps 认领门控(§12 意见 3 冻结执行点): 距上次认领不足
         * 1000/maxFps 毫秒本轮不认领——脏位保留零丢失仅延后。 */
        minIntervalMs = (plan.maxFps > 0) ? (1000 / plan.maxFps) : 0;
        if (s->lastClaimMs != 0 && minIntervalMs > 0 &&
            now - s->lastClaimMs < minIntervalMs) {
            int64_t rest = minIntervalMs - (now - s->lastClaimMs);
            XMutex_unlock(s->m_qMutex);
            XThread_msleep((uint32_t)xgs_maxInt((int)rest, 1));
            continue;
        }

        bpp = xgs_bppOf(plan.wireFormat);
        tw = xgs_maxInt(plan.tileWidth, 1);
        th = xgs_maxInt(plan.tileHeight, 1);
        gridW = s->gridW;
        gridH = s->gridH;
        shadowW = s->shadowW;
        shadowH = s->shadowH;
        full = s->allTilesDirty;
        s->allTilesDirty = false;
        s->lastClaimMs = now;
        if (!s->shadowInited || !s->dirtyGrid || gridW <= 0 || gridH <= 0 ||
            shadowW <= 0 || shadowH <= 0) {
            XAtomic_store_bool(&s->hasDamage, false,
                               XAtomic_MemoryOrder_Release);
            XMutex_unlock(s->m_qMutex);
            continue;
        }
        srcStride = XImage_bytesPerLine(&s->shadow);
        srcBits = XImage_bits(&s->shadow);
        slotBytes = (size_t)tw * (size_t)th * (size_t)bpp;

        /* ---- 加锁逐 tile 认领(短临界区拷出, §6.4) ---- */
        if (!xgs_ensureBufLocked((void**)&workBuf, &workCap,
                                 slotBytes * (size_t)gridW * (size_t)gridH) ||
            !xgs_ensureBufLocked((void**)&idxBuf, &idxCap,
                                 sizeof(int) * (size_t)gridW *
                                     (size_t)gridH) ||
            !xgs_ensureBufLocked((void**)&hashTab, &hashCap,
                                 sizeof(uint32_t) * (size_t)gridW *
                                     (size_t)gridH) ||
            !xgs_ensureBufLocked((void**)&hashValid, &hashValidCap,
                                 ((size_t)gridW * (size_t)gridH + 7u) / 8u)) {
            XMutex_unlock(s->m_qMutex);
            XThread_msleep(XGS_WORKER_IDLE_MS);
            continue;
        }
        if (epoch != lastEpoch) {
            /* 换档/resize: 哈希表作废, 本轮按全量语义编码。 */
            XMemset(hashTab, 0, sizeof(uint32_t) * (size_t)gridW *
                                    (size_t)gridH);
            XMemset(hashValid, 0,
                    ((size_t)gridW * (size_t)gridH + 7u) / 8u);
            lastEpoch = epoch;
            full = true;
        }
        for (gy = 0; gy < gridH; ++gy) {
            for (gx = 0; gx < gridW; ++gx) {
                int ti = gy * gridW + gx;
                bool dirty = full ||
                    (s->dirtyGrid[ti >> 3] & (1u << (ti & 7))) != 0;
                int cw, ch, y;
                if (!dirty) continue;
                cw = xgs_minInt(tw, shadowW - gx * tw);
                ch = xgs_minInt(th, shadowH - gy * th);
                if (cw <= 0 || ch <= 0 || !srcBits) continue;
                for (y = 0; y < ch; ++y) {
                    memcpy(workBuf + (size_t)claimed * slotBytes +
                               (size_t)y * (size_t)cw * (size_t)bpp,
                           srcBits +
                               (size_t)(gy * th + y) * (size_t)srcStride +
                               (size_t)gx * tw * (size_t)bpp,
                           (size_t)cw * (size_t)bpp);
                }
                s->dirtyGrid[ti >> 3] &= (uint8_t)~(1u << (ti & 7));
                idxBuf[claimed] = ti;
                ++claimed;
            }
        }
        XAtomic_store_bool(&s->hasDamage, false, XAtomic_MemoryOrder_Release);
        XMutex_unlock(s->m_qMutex);
        if (claimed <= 0) continue;
        if (XGS_BENCH_TRACE_ON())
            fprintf(stderr, "XGS_TRACE claim t=%llu tiles=%d\n",
                    (unsigned long long)(uint64_t)now, claimed);

        /* ---- 无锁编码: FNV 去重 → 影子已为线上格式零转换 → RLE/zlib;
         *      拆帧纪律组批: 首留 8 字节帧级头, tile 记录顺序追加, 单帧
         *      tile 载荷合计 ≤ txBudgetBytes, 单个超预算大 tile 独立成帧;
         *      收尾统一补写帧级头并入队 ---- */
        if (!xgs_ensureBufLocked((void**)&encBuf, &encCap,
                 XGuiRemoteCodec_maxEncodedSize(plan.codec, tw, th, bpp) +
                     16u) ||
            !xgs_ensureBufLocked((void**)&batchBuf, &batchCap,
                 (size_t)XGUI_REMOTE_FB_UPDATE_HEADER_BYTES +
                     xgs_maxSize(plan.txBudgetBytes,
                         XGuiRemoteCodec_maxEncodedSize(plan.codec, tw, th,
                                                        bpp) + 12u) +
                     16u)) {
            continue; /* 分配失败: 本批放弃(脏位已清, 下一伤害帧兜底)。 */
        }
        payloadBudget = batchCap - 16u;
        batchLen = (size_t)XGUI_REMOTE_FB_UPDATE_HEADER_BYTES;
        batchTiles = 0;
        for (i = 0; i < claimed; ++i) {
            int ti = idxBuf[i];
            int tcw = xgs_minInt(tw, shadowW - (ti % gridW) * tw);
            int tch = xgs_minInt(th, shadowH - (ti / gridW) * th);
            const uint8_t* slot = workBuf + (size_t)i * slotBytes;
            uint32_t hash = XGuiRemoteCodec_tileHash(
                slot, (size_t)tcw * (size_t)tch * (size_t)bpp);
            int encoded;
            size_t recBytes;
            XGuiRemoteMsgFbTile rec;
            size_t w2;
            gx0 = (ti % gridW) * tw;
            gy0 = (ti / gridW) * th;
            tw0 = tcw;
            th0 = tch;
            /* 上一轮同格 tile 哈希相等则跳过(全量刷新/换档首轮不去重)。 */
            if (!full && hashValid[ti >> 3] & (1u << (ti & 7)) &&
                hashTab[ti] == hash)
                continue;
            encoded = XGuiRemoteCodec_encodeTile(
                slot, tcw * bpp, plan.wireFormat, tcw, tch, plan.wireFormat,
                plan.codec, plan.zlibLevel, encBuf, encCap);
            if (encoded < 0) continue;
            hashTab[ti] = hash;
            hashValid[ti >> 3] |= (uint8_t)(1u << (ti & 7));
            recBytes = 12u + (size_t)encoded;
            if (batchTiles > 0 &&
                batchLen + recBytes > xgs_minSize(plan.txBudgetBytes,
                                                  payloadBudget)) {
                /* 超帧预算: 冲上一批(单个超预算 tile 由此独立成帧)。 */
                XGuiRemoteMsgFbUpdate hdr;
                hdr.sequence = fbSequence;
                hdr.tileCount = (uint16_t)batchTiles;
                hdr.format = (uint8_t)plan.wireFormat;
                hdr.flags = XGUI_REMOTE_TILE_FLAG_NONE;
                if (XGuiRemoteProto_encFbUpdate(batchBuf, batchCap, &hdr) != 0)
                    fbSequence++;
                XMutex_lock(s->m_qMutex);
                xgs_flushBatchLocked(s, batchBuf, batchLen, epoch);
                XMutex_unlock(s->m_qMutex);
                batchLen = (size_t)XGUI_REMOTE_FB_UPDATE_HEADER_BYTES;
                batchTiles = 0;
            }
            rec.x = (uint16_t)gx0;
            rec.y = (uint16_t)gy0;
            rec.w = (uint16_t)tw0;
            rec.h = (uint16_t)th0;
            rec.codec = (uint8_t)plan.codec;
            rec.flags = XGUI_REMOTE_TILE_FLAG_NONE;
            rec.payload = NULL;
            rec.payloadBytes = (uint32_t)encoded;
            w2 = XGuiRemoteProto_encFbTile(batchBuf + batchLen,
                                           batchCap - batchLen, &rec, encBuf);
            if (w2 == 0) continue; /* 容量恒足量, 防御性丢弃。 */
            batchLen += w2;
            ++batchTiles;
        }
        if (batchTiles > 0) {
            XGuiRemoteMsgFbUpdate hdr;
            hdr.sequence = fbSequence;
            hdr.tileCount = (uint16_t)batchTiles;
            hdr.format = (uint8_t)plan.wireFormat;
            hdr.flags = XGUI_REMOTE_TILE_FLAG_NONE;
            if (XGuiRemoteProto_encFbUpdate(batchBuf, batchCap, &hdr) != 0)
                fbSequence++;
            XMutex_lock(s->m_qMutex);
            xgs_flushBatchLocked(s, batchBuf, batchLen, epoch);
            XMutex_unlock(s->m_qMutex);
            /* 基准优化(2026-10-02): 编码批已入队, GUI 泵的写出要等主循环
             * 下一圈 poll 回调——空闲时主循环阻塞在 ioRing 等待上直至最近
             * 定时器(实测 resource 档 present→send 散布 31~52ms)。从编码
             * 线程写 wakeFd 打断该等待, 唤醒后泵圈即刻把帧写出。
             * wakeUp 仅 write(eventfd), 线程安全; 无环时为空操作。 */
            {
                XAbstractNetIoRing* ring = XAbstractNetIoRing_global();
                if (ring) XAbstractNetIoRing_wakeUp_base(ring);
            }
        }
    }

    if (workBuf) XFree_System(workBuf);
    if (idxBuf) XFree_System(idxBuf);
    if (encBuf) XFree_System(encBuf);
    if (batchBuf) XFree_System(batchBuf);
    if (hashTab) XFree_System(hashTab);
    if (hashValid) XFree_System(hashValid);
}

/* ==================== 握手与帧派发(GUI 线程) ==================== */

/**
 * @brief  进入流送态: 建影子缓冲(按协商档位+当前 store 尺寸) → FB_META →
 *         发 clientConnected 信号(握手+认证完成, §6.1)。
 */
static void xgs_enterStreaming(XGuiServer* self, XgsSession* s)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    int w = 0, h = 0;
    if (d->hosted && d->store) {
        XImage* img = XPlatformBackingStore_paintDevice(d->store);
        if (img) {
            w = XImage_width(img);
            h = XImage_height(img);
        }
    }
    XMutex_lock(s->m_qMutex);
    if (w > 0 && h > 0) (void)xgs_sessionReallocShadowLocked(s, w, h, false);
    XMutex_unlock(s->m_qMutex);
    s->state = XGUI_REMOTE_STATE_STREAMING;
    s->metaPending = true; /* 即便 store 不可用也发 META(0×0 占位语义)。 */
    (void)xgs_sessionSendFbMeta(self, s);
    d->metaBroadcastPending = true;
    XGuiServer_clientConnected_signal(self, s->id);
}

/** @brief 协议错误统一处置: ERROR + BYE(PROTOCOL_ERROR) + sessionError + 断链。 */
static void xgs_protocolError(XGuiServer* self, XgsSession* s)
{
    XGuiRemoteMsgError err;
    uint8_t buf[XGUI_REMOTE_MAX_MSG_BYTES + 16];
    size_t n;
    XMemset(&err, 0, sizeof(err));
    err.code = (uint16_t)XGUI_REMOTE_ERR_PROTOCOL;
    n = XGuiRemoteProto_encError(buf, sizeof(buf), &err);
    if (n > 0) {
        (void)XGuiRemoteProto_writeFrame(s->device, XGUI_REMOTE_MSG_ERROR,
                                         buf, n);
    }
    xgs_sendBye(s, XGUI_REMOTE_BYE_PROTOCOL_ERROR, "protocol error");
    XGuiServer_sessionError_signal(self, s->id,
                                   (int)XGUI_REMOTE_ERR_PROTOCOL);
    xgs_sessionClose(self, s, -1, (int)XGUI_REMOTE_BYE_PROTOCOL_ERROR);
}

/** @brief 认证失败处置: AUTH_RESULT(0) + BYE(AUTH_FAILED) + sessionError + 断链。 */
static void xgs_authFailed(XGuiServer* self, XgsSession* s)
{
    XGuiRemoteMsgAuthResult result;
    uint8_t buf[XGUI_REMOTE_MAX_MSG_BYTES + 16];
    size_t n;
    const char* text = "authentication failed";
    size_t tlen = strlen(text);
    XMemset(&result, 0, sizeof(result));
    result.ok = 0;
    memcpy(result.text, text, tlen);
    result.textBytes = (uint16_t)tlen;
    n = XGuiRemoteProto_encAuthResult(buf, sizeof(buf), &result);
    if (n > 0) {
        (void)XGuiRemoteProto_writeFrame(s->device,
                                         XGUI_REMOTE_MSG_AUTH_RESULT, buf, n);
    }
    xgs_sendBye(s, XGUI_REMOTE_BYE_AUTH_FAILED, "auth failed");
    XGuiServer_sessionError_signal(self, s->id, (int)XGUI_REMOTE_ERR_AUTH);
    xgs_sessionClose(self, s, -1, (int)XGUI_REMOTE_BYE_AUTH_FAILED);
}

/** @brief 处理 HELLO: 版本/能力/认证协商(§3.5/§3.7/§3.8)。 */
static void xgs_handleHello(XGuiServer* self, XgsSession* s,
                            const uint8_t* payload, size_t len)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    XGuiRemoteMsgHello hello;
    XGuiRemoteMsgHello ack;
    uint8_t buf[XGUI_REMOTE_MAX_NAME_BYTES + 16];
    size_t n;
    if (!XGuiRemoteProto_decHello(payload, len, &hello)) {
        xgs_protocolError(self, s);
        return;
    }
    if (hello.protocolVersion < XGUI_REMOTE_PROTOCOL_VERSION) {
        xgs_sendBye(s, XGUI_REMOTE_BYE_VERSION, "version too old");
        xgs_sessionClose(self, s, -1, (int)XGUI_REMOTE_BYE_VERSION);
        return;
    }
    /* 能力交集; 会话档位按能力夹取(客户端不支持 RGB565/ZLIB 则回退)。 */
    s->caps = xgs_serverCaps() & hello.capabilities;
    XMutex_lock(s->m_qMutex);
    s->profile = d->profile;
    s->profileId = d->profileId;
    if (s->profile.wireFormat == XGUI_REMOTE_PF_RGB565 &&
        !(s->caps & (uint32_t)XGUI_REMOTE_CAP_RGB565)) {
        s->profile.wireFormat = XGUI_REMOTE_PF_ARGB32;
    }
    if (s->profile.codec == XGUI_REMOTE_CODEC_ZLIB &&
        !(s->caps & (uint32_t)XGUI_REMOTE_CAP_ZLIB)) {
        s->profile.codec = XGUI_REMOTE_CODEC_RLE;
    }
    XMutex_unlock(s->m_qMutex);

    XMemset(&ack, 0, sizeof(ack));
    ack.protocolVersion = XGUI_REMOTE_PROTOCOL_VERSION; /* 定版=min(双方)。 */
    ack.capabilities = s->caps;
    ack.authMethod = (uint8_t)d->authMethod;
    {
        size_t nlen = strlen(XGS_SERVER_NAME);
        memcpy(ack.name, XGS_SERVER_NAME, nlen);
        ack.nameBytes = (uint16_t)nlen;
    }
    n = XGuiRemoteProto_encHello(buf, sizeof(buf), &ack);
    if (n == 0 || !xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_HELLO_ACK, buf, n)) {
        xgs_protocolError(self, s);
        return;
    }
    if (d->authMethod == XGUI_REMOTE_AUTH_SHA256_CHALLENGE && d->hasPassword) {
        XGuiRemoteMsgAuthChallenge challenge;
        XMemset(&challenge, 0, sizeof(challenge));
        challenge.method = (uint8_t)XGUI_REMOTE_AUTH_SHA256_CHALLENGE;
        challenge.nonceBytes = (uint16_t)XGUI_REMOTE_AUTH_NONCE_BYTES;
        XRandomGenerator_fillSecure(challenge.nonce,
                                    XGUI_REMOTE_AUTH_NONCE_BYTES);
        memcpy(s->nonce, challenge.nonce, XGUI_REMOTE_AUTH_NONCE_BYTES);
        n = XGuiRemoteProto_encAuthChallenge(buf, sizeof(buf), &challenge);
        if (n == 0 ||
            !xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_AUTH_CHALLENGE, buf, n)) {
            xgs_protocolError(self, s);
            return;
        }
        s->state = XGUI_REMOTE_STATE_AUTHENTICATING;
    } else {
        xgs_enterStreaming(self, s);
    }
}

/** @brief 处理 AUTH_RESPONSE: response = SHA256(storedHash || nonce) 同式重算。 */
static void xgs_handleAuthResponse(XGuiServer* self, XgsSession* s,
                                   const uint8_t* payload, size_t len)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    XGuiRemoteMsgAuthResponse resp;
    XByteArrayView parts[2];
    uint8_t expected[XGUI_REMOTE_AUTH_RESPONSE_BYTES];
    XByteArrayView view;
    if (!XGuiRemoteProto_decAuthResponse(payload, len, &resp)) {
        xgs_protocolError(self, s);
        return;
    }
    parts[0].m_data = d->passwordHash;
    parts[0].m_size = (int64_t)XGUI_REMOTE_AUTH_RESPONSE_BYTES;
    parts[1].m_data = s->nonce;
    parts[1].m_size = (int64_t)XGUI_REMOTE_AUTH_NONCE_BYTES;
    XMemset(expected, 0, sizeof(expected));
    view = XCryptographicHash_hashInto_1((char*)expected, sizeof(expected),
                                         parts, 2, XCryptographicHash_Sha256);
    if (view.m_size != (int64_t)sizeof(expected) ||
        memcmp(expected, resp.response, XGUI_REMOTE_AUTH_RESPONSE_BYTES) != 0) {
        xgs_authFailed(self, s);
        return;
    }
    {
        XGuiRemoteMsgAuthResult result;
        uint8_t buf[XGUI_REMOTE_MAX_MSG_BYTES + 16];
        const char* text = "welcome";
        size_t tlen = strlen(text);
        size_t n;
        XMemset(&result, 0, sizeof(result));
        result.ok = 1;
        memcpy(result.text, text, tlen);
        result.textBytes = (uint16_t)tlen;
        n = XGuiRemoteProto_encAuthResult(buf, sizeof(buf), &result);
        if (n == 0 ||
            !xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_AUTH_RESULT, buf, n)) {
            xgs_protocolError(self, s);
            return;
        }
    }
    xgs_enterStreaming(self, s);
}

/** @brief FB_REQUEST(mode=1 全量): 置全量标志(§3.6; 去重由全量语义绕过)。 */
static void xgs_handleFbRequest(XgsSession* s, const uint8_t* payload,
                                size_t len)
{
    XGuiRemoteMsgFbRequest req;
    if (!XGuiRemoteProto_decFbRequest(payload, len, &req)) {
        xgs_protocolError(s->owner, s);
        return;
    }
    if (req.mode == 1) {
        XMutex_lock(s->m_qMutex);
        s->allTilesDirty = true;
        XAtomic_store_bool(&s->hasDamage, true, XAtomic_MemoryOrder_Release);
        XMutex_unlock(s->m_qMutex);
    }
}

/** @brief PROFILE_SET(受 allowClientProfile 门控, §5.3)。 */
static void xgs_handleProfileSet(XGuiServer* self, XgsSession* s,
                                 const uint8_t* payload, size_t len)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    XGuiRemoteMsgProfileSet set;
    XGuiRemoteMsgProfileResult result;
    uint8_t buf[8];
    size_t n;
    if (!XGuiRemoteProto_decProfileSet(payload, len, &set)) {
        xgs_protocolError(self, s);
        return;
    }
    XMemset(&result, 0, sizeof(result));
    if (!d->allowClientProfile) {
        result.accepted = 0;
        XMutex_lock(s->m_qMutex);
        result.profileId = (uint8_t)s->profileId;
        XMutex_unlock(s->m_qMutex);
    } else if (set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_PERFORMANCE ||
               set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_RESOURCE ||
               set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_AUTO) {
        XGuiRemoteProfile preset;
        if (set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_PERFORMANCE)
            XGuiRemoteProfile_initPerformance(&preset);
        else if (set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_RESOURCE)
            XGuiRemoteProfile_initResource(&preset);
        else
            XGuiRemoteProfile_initAuto(&preset);
        /* 帧边界生效(§5.3): 挂 pending, 队列/待写缓冲清空后应用。 */
        XMutex_lock(s->m_qMutex);
        s->pendingProfile = preset;
        s->pendingProfileId = (XGuiRemoteProfileId)set.profileId;
        s->switchPending = true;
        XMutex_unlock(s->m_qMutex);
        result.accepted = 1;
        result.profileId = set.profileId;
    } else {
        result.accepted = 0;
        XMutex_lock(s->m_qMutex);
        result.profileId = (uint8_t)s->profileId;
        XMutex_unlock(s->m_qMutex);
    }
    n = XGuiRemoteProto_encProfileResult(buf, sizeof(buf), &result);
    if (n > 0) (void)xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_PROFILE_RESULT,
                                          buf, n);
}

/** @brief 派发一帧(GUI 线程; 未知名节由帧层跳过, 到此均为已知类型)。 */
static void xgs_dispatchFrame(XGuiServer* self, XgsSession* s,
                              XGuiRemoteMsgType type,
                              const uint8_t* payload, size_t len)
{
    uint64_t stamp = 0;
    switch (type) {
    case XGUI_REMOTE_MSG_HELLO:
        if (s->state != XGUI_REMOTE_STATE_HANDSHAKING) break;
        xgs_handleHello(self, s, payload, len);
        return;
    case XGUI_REMOTE_MSG_AUTH_RESPONSE:
        if (s->state != XGUI_REMOTE_STATE_AUTHENTICATING) break;
        xgs_handleAuthResponse(self, s, payload, len);
        return;
    case XGUI_REMOTE_MSG_FB_REQUEST:
        if (s->state != XGUI_REMOTE_STATE_STREAMING) break;
        xgs_handleFbRequest(s, payload, len);
        return;
    case XGUI_REMOTE_MSG_INPUT_KEY: {
        XGuiRemoteMsgInputKey m;
        if (s->state != XGUI_REMOTE_STATE_STREAMING) break;
        if (!XGuiRemoteProto_decInputKey(payload, len, &m)) break;
        if (XGS_BENCH_TRACE_ON())
            fprintf(stderr, "XGS_TRACE keyin t=%llu\n",
                    (unsigned long long)(uint64_t)xgs_nowMs());
        xgs_injectKey(self, &m);
        return;
    }
    case XGUI_REMOTE_MSG_INPUT_POINTER: {
        XGuiRemoteMsgInputPointer m;
        if (s->state != XGUI_REMOTE_STATE_STREAMING) break;
        if (!XGuiRemoteProto_decInputPointer(payload, len, &m)) break;
        if (XGS_BENCH_TRACE_ON() && m.action != XGUI_REMOTE_PTR_MOVE)
            fprintf(stderr, "XGS_TRACE inject t=%llu action=%u\n",
                    (unsigned long long)(uint64_t)xgs_nowMs(),
                    (unsigned)m.action);
        xgs_injectPointer(self, &m);
        return;
    }
    case XGUI_REMOTE_MSG_INPUT_WHEEL: {
        XGuiRemoteMsgInputWheel m;
        if (s->state != XGUI_REMOTE_STATE_STREAMING) break;
        if (!XGuiRemoteProto_decInputWheel(payload, len, &m)) break;
        xgs_injectWheel(self, &m);
        return;
    }
    case XGUI_REMOTE_MSG_INPUT_TOUCH: {
        XGuiRemoteMsgInputTouch m;
        if (s->state != XGUI_REMOTE_STATE_STREAMING) break;
        if (!XGuiRemoteProto_decInputTouch(payload, len, &m)) break;
        xgs_injectTouch(self, &m);
        return;
    }
    case XGUI_REMOTE_MSG_INPUT_IME: {
        XGuiRemoteMsgInputIme m;
        if (s->state != XGUI_REMOTE_STATE_STREAMING) break;
        if (!XGuiRemoteProto_decInputIme(payload, len, &m)) break;
        xgs_injectIme(self, &m);
        return;
    }
    case XGUI_REMOTE_MSG_PROFILE_SET:
        if (s->state != XGUI_REMOTE_STATE_STREAMING) break;
        xgs_handleProfileSet(self, s, payload, len);
        return;
    case XGUI_REMOTE_MSG_PING:
        if (!XGuiRemoteProto_decPing(payload, len, &stamp)) break;
        (void)xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_PONG, &stamp,
                                   sizeof(stamp));
        return;
    case XGUI_REMOTE_MSG_PONG:
        /* 回显帧: 服务端无统计口径, 仅刷新保活(读侧已记活动时刻)。 */
        (void)XGuiRemoteProto_decPing(payload, len, &stamp);
        return;
    case XGUI_REMOTE_MSG_BYE: {
        XGuiRemoteMsgBye bye;
        uint8_t reason = (uint8_t)XGUI_REMOTE_BYE_NORMAL;
        if (XGuiRemoteProto_decBye(payload, len, &bye))
            reason = bye.reason; /* 开放字段: 未知原因原样透传。 */
        xgs_sessionClose(self, s, -1, (int)reason);
        return;
    }
    case XGUI_REMOTE_MSG_ERROR:
        /* 对端错误通告: 开放字段不校验, 会话继续(保活照常)。 */
        return;
    default:
        break;
    }
    /* S→C 方向消息(HELLO_ACK/AUTH_CHALLENGE/AUTH_RESULT/FB_META/
     * FB_UPDATE/PROFILE_RESULT)与 FB_ACK(V1 不发不收)出现在服务端 =
     * 协议错误。 */
    xgs_protocolError(self, s);
}

/* ==================== poll 泵(GUI 线程, §4.4/§6.4) ==================== */

/**
 * @brief  帧尾待写缓冲直写续传 + 从编码队列组装 FB_UPDATE 限预算写出。
 * @return false=链路硬错误(调用方断链)。
 */
static bool xgs_pumpWrite(XgsSession* s)
{
    XGuiRemoteProfile plan;
    size_t budget;
    XMutex_lock(s->m_qMutex);
    plan = s->profile;
    XMutex_unlock(s->m_qMutex);
    budget = plan.txBudgetBytes ? plan.txBudgetBytes : (size_t)65536;

    /* 1) 按 txBudgetBytes 直写续传帧尾待写缓冲余量(部分写=背压非链路
     *    错误, XAbstractSocket.c:631-635 / XIODevice.c:283-303)。 */
    while (s->tailBuf && budget > 0) {
        size_t want = xgs_minSize(s->tailLen, budget);
        int64_t w = XIODevice_write_1(s->device, (const char*)s->tailBuf,
                                      (int64_t)want);
        if (w < 0) return false;
        if (w == 0) break; /* 设备暂不可写: 留待下圈。 */
        if ((size_t)w < s->tailLen)
            memmove(s->tailBuf, s->tailBuf + w, s->tailLen - (size_t)w);
        s->tailLen -= (size_t)w;
        budget -= (size_t)w;
        if (s->tailLen == 0) {
            XFree_System(s->tailBuf);
            s->tailBuf = NULL;
        }
    }

    /* 2) 待写缓冲清空后从编码队列取整批组装 FB_UPDATE; 拆帧纪律下
     *    单批 tile 载荷 ≤ txBudgetBytes。基准优化(2026-10-02): 每圈循环
     *    写出多帧直至预算用尽/队列空——原「每圈至多一帧」在 resource 档
     *    把相邻编码批拆成 6-14KB 小帧串行发出(帧间隔=事件循环节奏),
     *    客户端 ioRing 读侧在小帧流下读完成延迟实测 ~170ms/帧(平台层
     *    既有行为); 大帧(rb 分块)路径实测 0ms。多帧连发聚成大流, 收侧
     *    延迟随之消失; 背压纪律不变(tailBuf 存在即停, 预算封顶)。 */
    if (!s->tailBuf) {
        XgsQueueNode* node;
        uint32_t curEpoch;
        for (;;) {
            XMutex_lock(s->m_qMutex);
            node = s->qHead;
            curEpoch = s->epoch;
            if (node) {
                s->qHead = node->next;
                if (!s->qHead) s->qTail = NULL;
                s->qBytes -= node->len;
            }
            XMutex_unlock(s->m_qMutex);
            if (!node) break;
            if (node->epoch == curEpoch) {
                if (XGS_BENCH_TRACE_ON())
                    fprintf(stderr, "XGS_TRACE send t=%llu bytes=%zu\n",
                            (unsigned long long)(uint64_t)xgs_nowMs(),
                            node->len);
                if (!xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_FB_UPDATE,
                                          XGS_NODE_DATA(node), node->len)) {
                    XFree_System(node);
                    return false;
                }
                if (s->tailBuf) {
                    /* 背压: 本帧余量已截入 tailBuf(§12 意见 1 契约),
                     * node 消费完毕; 后续帧留队列, 下圈 tailBuf 续传
                     * 清空后再发(背压纪律: 待写未清不组新帧)。 */
                    XFree_System(node);
                    break;
                }
            }
            XFree_System(node); /* 代际过期批次: 丢弃(全量刷新兜底)。 */
        }
    }

    /* 3) 背压水位: 待写缓冲存在期间反压编码线程(暂停认领, §6.4)。 */
    XMutex_lock(s->m_qMutex);
    s->backpressure = (s->tailBuf != NULL);
    XMutex_unlock(s->m_qMutex);
    return true;
}

/** @brief 单会话保活(档位 pingIntervalMs/pingTimeoutMs, §3.7)。 */
static bool xgs_pumpKeepalive(XgsSession* s)
{
    XGuiRemoteProfile plan;
    int64_t now = xgs_nowMs();
    uint64_t stamp;
    XMutex_lock(s->m_qMutex);
    plan = s->profile;
    XMutex_unlock(s->m_qMutex);
    if (plan.pingTimeoutMs > 0 &&
        now - s->lastPeerActivityMs > (int64_t)plan.pingTimeoutMs) {
        xgs_sendBye(s, XGUI_REMOTE_BYE_TIMEOUT, "ping timeout");
        return false;
    }
    if (plan.pingIntervalMs > 0 &&
        now - s->lastPingSentMs >= (int64_t)plan.pingIntervalMs) {
        stamp = (uint64_t)now;
        if (!xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_PING, &stamp,
                                  sizeof(stamp)))
            return false;
        s->lastPingSentMs = now;
    }
    return true;
}

/** @brief 应用待生效档位切换(帧边界: 队列与待写缓冲均空, §5.3)。 */
static void xgs_pumpApplySwitch(XgsSession* s)
{
    bool apply = false;
    XGuiRemoteProfile pending;
    XGuiRemoteProfileId pendingId = XGUI_REMOTE_PROFILE_CUSTOM;
    XMutex_lock(s->m_qMutex);
    if (s->switchPending && !s->tailBuf && s->qBytes == 0) {
        pending = s->pendingProfile;
        pendingId = s->pendingProfileId;
        s->switchPending = false;
        apply = true;
    }
    XMutex_unlock(s->m_qMutex);
    if (!apply) return;
    XMutex_lock(s->m_qMutex);
    s->profile = pending;
    s->profileId = pendingId;
    s->metaPending = true; /* 参数已变化, FB_META 全量跟随(§5.3)。 */
    /* 影子按新计划重分配(同尺寸则转换保留内容); 失败时下帧采集重试。 */
    (void)xgs_sessionReallocShadowLocked(s, s->shadowW, s->shadowH, true);
    XMutex_unlock(s->m_qMutex);
}

/** @brief 单会话每圈处理: 读→切档→FB_META→保活→写→丢批上报。 */
static void xgs_pumpSession(XGuiServer* self, XgsSession* s)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    int round;

#if defined(XGUI_REMOTE_TLS_ON) && XGUI_REMOTE_TLS_ON && \
    defined(XNETWORK_SSL_ON) && XNETWORK_SSL_ON
    /* TLS 握手推进: 握手仅由读写路径驱动(xssl_v_writeData/readData 每
     * 调必泵), 而加密完成前 bytesAvailable 恒 0, 下方读循环永不启动——
     * 显式 0 长写泵之: 不产生密文记录, 握手若在本圈内完成也不吞掉对端
     * 横幅首字节(读路径探测的缺陷, 已弃用)。E2E 实测根因, 2026-10-02:
     * 缺此泵则双端各持半程握手互等。 */
    if (d->tlsAccept && !XSslSocket_isEncrypted((XSslSocket*)s->device)) {
        (void)XIODevice_write_1(s->device, "", 0);
        return; /* 加密完成前无会话帧可读, 本圈到此。 */
    }
#endif

    /* ---- 读: banner 攒字节(§3.3) → 帧泵 → 派发(输入即注入, §6.6) ---- */
    for (round = 0; round < XGS_READ_CHUNKS_PER_ROUND; ++round) {
        uint8_t chunk[XGS_READ_CHUNK];
        int64_t avail = XIODevice_bytesAvailable_base(s->device);
        int64_t n;
        const uint8_t* p;
        size_t left;
        int r;
        if (avail <= 0) break;
        n = XIODevice_read_1(s->device, (char*)chunk,
                             (int64_t)xgs_minSize((size_t)avail,
                                                  sizeof(chunk)));
        if (n <= 0) break;
        s->lastPeerActivityMs = xgs_nowMs();
        p = chunk;
        left = (size_t)n;
        if (s->bannerGot < XGUI_REMOTE_BANNER_BYTES) {
            /* 横幅阶段: 先攒 8B 校验魔数+版本(§3.3/§3.7); 同块余量交帧泵。 */
            size_t need = (size_t)(XGUI_REMOTE_BANNER_BYTES - s->bannerGot);
            size_t take = xgs_minSize(need, left);
            memcpy(s->bannerBuf + s->bannerGot, p, take);
            s->bannerGot += (int)take;
            p += take;
            left -= take;
            if (s->bannerGot == XGUI_REMOTE_BANNER_BYTES) {
                if (!XGuiRemoteProto_bannerIsValid(s->bannerBuf) ||
                    XGuiRemoteProto_bannerVersion(s->bannerBuf) <
                        XGUI_REMOTE_PROTOCOL_VERSION) {
                    xgs_protocolError(self, s);
                    return;
                }
                s->state = XGUI_REMOTE_STATE_HANDSHAKING;
            }
        }
        if (left > 0) {
            r = XGuiRemoteFrameReader_feed(&s->reader, p, left);
            while (r > 0) {
                xgs_dispatchFrame(self, s, s->reader.type, s->reader.payload,
                                  s->reader.payloadLen);
                if (s->closed) return; /* 派发内可能已断链。 */
                r = XGuiRemoteFrameReader_feed(&s->reader, NULL, 0);
            }
            if (r < 0) {
                xgs_protocolError(self, s);
                return;
            }
        }
        if (s->closed) return;
    }

    /* ---- 档位切换(帧边界) ---- */
    xgs_pumpApplySwitch(s);

    /* ---- 标题变化检测(2026-10-02 修复): 窗口标题 ≠ 已定版标题 →
     *     metaPending, FB_META 全量跟随(§6.2 标题口径)。原先标题变化
     *     不触发 FB_META(仅尺寸/档位触发), 客户端标题跟随失效——E2E
     *     键盘往返字符串断言依赖此路径。空标题(窗口未就绪)不比对。 ---- */
    {
        XWindow* twin = xgs_targetWindow(self);
        if (twin && s->state == XGUI_REMOTE_STATE_STREAMING) {
            XString* t = XWindow_title(twin);
            const char* utf8 = t ? XString_toUtf8(t) : NULL;
            const char* cur = (utf8 && utf8[0]) ? utf8 : NULL;
            if (cur && strcmp(cur, s->lastTitle) != 0) {
                snprintf(s->lastTitle, sizeof(s->lastTitle), "%s", cur);
                s->metaPending = true;
            }
            if (t) XClassDelete((XClass*)t);
        }
    }

    /* ---- FB_META 跟随(尺寸/档位/标题变化, §5.3/§6.2) ---- */
    if (s->metaPending && s->state == XGUI_REMOTE_STATE_STREAMING) {
        if (!xgs_sessionSendFbMeta(self, s)) {
            xgs_sessionClose(self, s, -1,
                             (int)XGUI_REMOTE_BYE_PROTOCOL_ERROR);
            return;
        }
        d->metaBroadcastPending = true;
    }

    /* ---- 保活 ---- */
    if (!xgs_pumpKeepalive(s)) {
        xgs_sessionClose(self, s, -1, (int)XGUI_REMOTE_BYE_TIMEOUT);
        return;
    }

    /* ---- 写: 待写缓冲续传 + 队列出帧(限预算) ---- */
    if (!xgs_pumpWrite(s)) {
        xgs_sessionClose(self, s, -1, (int)XGUI_REMOTE_BYE_PROTOCOL_ERROR);
        return;
    }

    /* ---- 编码线程丢批上报(sessionError, GUI 线程发射, §6.4) ---- */
    {
        int dropped;
        XMutex_lock(s->m_qMutex);
        dropped = s->droppedBatches;
        XMutex_unlock(s->m_qMutex);
        if (dropped != s->lastSeenDropped) {
            s->lastSeenDropped = dropped;
            XGuiServer_sessionError_signal(
                self, s->id, (int)XGUI_REMOTE_ERR_BUFFER_OVERFLOW);
        }
    }
}

/**
 * @brief  GUI 线程 poll 回调(每圈事件循环): 逐会话泵 + FB_META 变更信号。
 * @note   遍历期间会话可能被派发内断链摘除——next 指针先取后用。
 */
static bool xgs_pumpCallback(void* userData)
{
    XGuiServer* self = (XGuiServer*)userData;
    XGuiServerPrivate* d;
    XgsSession* s;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return true;
    if (XGS_BENCH_TRACE_ON())
        fprintf(stderr, "XGS_TRACE pump t=%llu\n",
                (unsigned long long)(uint64_t)xgs_nowMs());
    s = d->sessionHead;
    while (s) {
        XgsSession* next = s->next;
        if (!s->closed) xgs_pumpSession(self, s);
        s = next;
    }
    if (d->metaBroadcastPending) {
        d->metaBroadcastPending = false;
        XGuiServer_fbMetaChanged_signal(self);
    }
    return true;
}

/** @brief 帧泵兜底定时回调(基准优化 2026-10-02): 直呼 xgs_pumpCallback。
 *  @details poll 回调依赖每圈事件循环; 空闲时圈的间隔受网络完成事件唤醒
 *           可靠性影响(实测 resource 档 send 间隔/收侧延迟散布 52~170ms)。
 *           8ms 周期兜底把泵圈间隔钳到 ≤8ms; 会话回调自身在无 I/O 时近零
 *           成本(bytesAvailable==0 即返), 常态 CPU 开销可忽略。 */
static void xgs_pumpTimerCb(void* userData, XTimerData* timer)
{
    (void)timer;
    xgs_pumpCallback((XGuiServer*)userData);
}

/** @brief 确保 GUI 线程 poll 回调已登记(无事件调度器时失败)。 */
static bool xgs_ensurePump(XGuiServer* self)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    if (d->pumpHandle) return true;
    d->pumpHandle = XAbstractEventDispatcher_addPollCallback(xgs_pumpCallback,
                                                             self);
    if (!d->pumpHandle) return false;
    /* 兜底定时器(基准优化): 与 poll 回调同口, 仅在会话活跃期消费。 */
    if (!d->pumpTimer) {
        d->pumpTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (d->pumpTimer) {
            XTimer_setInterval(d->pumpTimer, 8);
            XTimer_setTimerCallback(d->pumpTimer, xgs_pumpTimerCb);
            XTimer_setUserData(d->pumpTimer, self);
            XTimer_start_base(d->pumpTimer);
        }
    }
    return true;
}

/* ==================== 会话生命周期 ==================== */

/** @brief 启动会话编码线程(func 模式; 参数块由 close 在 join 后释放)。 */
static bool xgs_sessionStartWorker(XgsSession* s)
{
    XgsSession* sp = s;
    s->threadArgs = XVarList_Create(XVar(void*, sp));
    if (!s->threadArgs) return false;
    s->thread = XThread_create_func(xgs_workerMain, s->threadArgs);
    if (!s->thread) {
        XVarList_delete(s->threadArgs);
        s->threadArgs = NULL;
        return false;
    }
    if (!XThread_start(s->thread)) {
        XClassDelete((XClass*)s->thread);
        s->thread = NULL;
        XVarList_delete(s->threadArgs);
        s->threadArgs = NULL;
        return false;
    }
    s->threadStarted = true;
    return true;
}

/** @brief 新建会话对象并挂入会话链(仅建结构; 失败返回 NULL)。 */
static XgsSession* xgs_sessionCreate(XGuiServer* self, XIODevice* device,
                                     bool ownDevice)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    XgsSession* s = (XgsSession*)XMalloc_System(sizeof(XgsSession));
    if (!s) return NULL;
    XMemset(s, 0, sizeof(XgsSession));
    s->id = d->nextSessionId++;
    s->owner = self;
    s->device = device;
    s->ownDevice = ownDevice;
    s->state = XGUI_REMOTE_STATE_BANNER_WAIT;
    XGuiRemoteFrameReader_init(&s->reader);
    s->m_qMutex = XMutex_create(XLock_NonRecursive);
    if (!s->m_qMutex) {
        XFree_System(s);
        return NULL;
    }
    XAtomic_init(s->hasDamage, false);
    XImage_init(&s->shadow);
    s->lastPeerActivityMs = xgs_nowMs();
    s->lastPingSentMs = 0;
    s->next = d->sessionHead;
    d->sessionHead = s;
    d->sessionCount++;
    return s;
}

/**
 * @brief  关闭会话: BYE → 停编码线程(置停机标志+join 语义收尾, §6.5) →
 *         释放资源 → 摘链 → 发 clientDisconnected。
 * @param  byeReason  ≥0 时先尽力发送 BYE; -1 跳过(调用方已发或不应发)。
 * @param  emitReason 断开信号携带的 XGuiRemoteByeReason。
 */
static void xgs_sessionClose(XGuiServer* self, XgsSession* s,
                             int byeReason, int emitReason)
{
#if defined(XGS_CLOSE_DEBUG) && XGS_CLOSE_DEBUG
    fprintf(stderr, "XGS_CLOSE bye=%d emit=%d\n", byeReason, emitReason);
#endif
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    XgsSession** pp;
    int id;
    if (!s || s->closed) return;
    s->closed = true;
    id = s->id;
    if (byeReason >= 0) xgs_sendBye(s, (XGuiRemoteByeReason)byeReason, NULL);
    /* 停编码线程: 置停机标志后 join 语义收尾。 */
    XMutex_lock(s->m_qMutex);
    s->stopThread = true;
    XMutex_unlock(s->m_qMutex);
    if (s->thread) {
        XThread_requestInterruption(s->thread);
        if (s->threadStarted) (void)XThread_wait(s->thread, XGS_WORKER_JOIN_MS);
        XClassDelete((XClass*)s->thread);
        s->thread = NULL;
    }
    if (s->threadArgs) {
        XVarList_delete(s->threadArgs);
        s->threadArgs = NULL;
    }
    /* 设备: 拥有(TCP accept)则关闭并销毁; 借用(attachTransport)不动。 */
    if (s->ownDevice && s->device) {
        XIODevice_close_base(s->device);
        XClassDelete((XClass*)s->device);
    }
    s->device = NULL;
    /* 队列与待写缓冲。 */
    XMutex_lock(s->m_qMutex);
    {
        XgsQueueNode* node = s->qHead;
        while (node) {
            XgsQueueNode* next = node->next;
            XFree_System(node);
            node = next;
        }
        s->qHead = NULL;
        s->qTail = NULL;
        s->qBytes = 0;
    }
    XMutex_unlock(s->m_qMutex);
    if (s->tailBuf) {
        XFree_System(s->tailBuf);
        s->tailBuf = NULL;
        s->tailLen = 0;
    }
    XGuiRemoteFrameReader_deinit(&s->reader);
    if (s->shadowInited) {
        XClassDeinit(&s->shadow);
        s->shadowInited = false;
    }
    if (s->dirtyGrid) XFree_System(s->dirtyGrid);
    s->dirtyGrid = NULL;
    XMutex_delete(s->m_qMutex);
    s->m_qMutex = NULL;
    /* 摘链。 */
    pp = &d->sessionHead;
    while (*pp && *pp != s) pp = &(*pp)->next;
    if (*pp) *pp = s->next;
    d->sessionCount--;
    XFree_System(s);
    XGuiServer_clientDisconnected_signal(self, id, emitReason);
}

/* ==================== 信号发射 ==================== */

void* XGuiServer_clientConnected_signal(XGuiServer* self, int sessionId)
{
    int sid = sessionId;
    XEmitSignal(self, XGuiServer_clientConnected_signal,
                XVarList_Create(XVar(int, sid)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XGuiServer_clientDisconnected_signal(XGuiServer* self, int sessionId,
                                           int reason)
{
    int sid = sessionId;
    int rsn = reason;
    XEmitSignal(self, XGuiServer_clientDisconnected_signal,
                XVarList_Create(XVar(int, sid), XVar(int, rsn)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XGuiServer_sessionError_signal(XGuiServer* self, int sessionId,
                                     int errorCode)
{
    int sid = sessionId;
    int code = errorCode;
    XEmitSignal(self, XGuiServer_sessionError_signal,
                XVarList_Create(XVar(int, sid), XVar(int, code)), NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

void* XGuiServer_fbMetaChanged_signal(XGuiServer* self)
{
    XEmitSignal(self, XGuiServer_fbMetaChanged_signal, NULL, NULL, NULL,
                XEVENT_PRIORITY_NORMAL);
}

/* ==================== 对象生命周期 ==================== */

/** @brief 释放 XGuiServer: 解除回调登记、停全部会话、销毁监听器。 */
static void VXGuiServer_deinit(XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self) return;
    d = (XGuiServerPrivate*)self->m_d;
    if (d) {
        /* unhost: 恢复旧 present 登记(单槽共存, §6.2; V1 语义=取消登记)。 */
        if (d->hosted && d->store) {
            XPlatformBackingStore_setPresentCallback(d->store, d->chainCb,
                                                     d->chainData);
        }
        d->hosted = NULL;
        d->store = NULL;
        d->chainCb = NULL;
        d->chainData = NULL;
        if (d->pumpHandle) {
            XAbstractEventDispatcher_removePollCallback(d->pumpHandle);
            d->pumpHandle = NULL;
        }
        if (d->pumpTimer) { /* 兜底定时器随泵同生死(基准优化 2026-10-02)。 */
            XTimer_stop_base(d->pumpTimer);
            XObject_deleteLater((XObject*)d->pumpTimer);
            d->pumpTimer = NULL;
        }
        while (d->sessionCount > 0 && d->sessionHead) {
            xgs_sessionClose(self, d->sessionHead,
                             (int)XGUI_REMOTE_BYE_SERVER_SHUTDOWN,
                             (int)XGUI_REMOTE_BYE_SERVER_SHUTDOWN);
        }
#if XNETWORK_ON && XNETWORK_TCPSERVER_ON
        if (d->tcpServer) {
            XTcpServer_close(d->tcpServer);
            XClassDelete((XClass*)d->tcpServer);
            d->tcpServer = NULL;
        }
#endif
        /* TLS 策略路径副本(加法式扩展, 拥有)。 */
        if (d->tlsCertFile) {
            XFree_System(d->tlsCertFile);
            d->tlsCertFile = NULL;
        }
        if (d->tlsKeyFile) {
            XFree_System(d->tlsKeyFile);
            d->tlsKeyFile = NULL;
        }
        XFree_System(d);
        self->m_d = NULL;
    }
    XClass_Deinit_Parent(XObject, (XObject*)self);
}

XVtable* XGuiServer_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XGuiServer)
    XVTABLE_INHERIT_XCLASS(XObject);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXGuiServer_deinit);
    return XVTABLE_DEFAULT;
}

void XGuiServer_init(XGuiServer* self, XObject* parent)
{
    XGuiServerPrivate* d;
    if (!self) return;
    XMemset(self, 0, sizeof(XGuiServer));
    XObject_init((XObject*)self);
    if (parent) XObject_setParent((XObject*)self, parent);
    XClassSetVtable(self, XGuiServer);
    d = (XGuiServerPrivate*)XMalloc_System(sizeof(XGuiServerPrivate));
    self->m_d = d;
    if (!d) return;
    XMemset(d, 0, sizeof(XGuiServerPrivate));
    d->maxSessions = XGS_DEFAULT_MAX_SESSIONS;
    d->nextSessionId = 1;
    d->authMethod = XGUI_REMOTE_AUTH_NONE;
    XGuiRemoteProfile_initPerformance(&d->profile);
    d->profileId = XGUI_REMOTE_PROFILE_PERFORMANCE;
    d->tlsOverride = -1; /* 未显式设置: listen 时回退环境变量(原口径)。 */
}

XGuiServer* XGuiServer_create_ex(XMemoryType memory, XObject* parent)
{
    XGuiServer* self = (XGuiServer*)XMemory_malloc(sizeof(XGuiServer), memory);
    if (!self) return NULL;
    XGuiServer_init(self, parent);
    if (!self->m_d) {
        XClassDelete((XClass*)self); /* 私有块失败: 回收半成品。 */
        return NULL;
    }
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 镜像目标(§6.2) ==================== */

bool XGuiServer_host(XGuiServer* self, XWidget* topLevel)
{
    XGuiServerPrivate* d;
    XBackingStore* bs;
    XPlatformBackingStore* pbs;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    if (d->hosted) return false; /* 已有绑定目标。 */
    if (!topLevel || !XWidget_isWindow(topLevel)) return false;
    bs = XWidget_backingStore(topLevel);
    if (!bs) return false;
    pbs = XBackingStore_handle(bs);
    if (!pbs) return false;
    d->hosted = topLevel;
    d->store = pbs;
    /* 旧回调读取口缺失(文件头注记): V1 全树无既有登记者, 链调对为
     * (NULL,NULL); unhost 按"取消登记"恢复, 语义正确。 */
    d->chainCb = NULL;
    d->chainData = NULL;
    XPlatformBackingStore_setPresentCallback(pbs, xgs_presentWrapper, self);
    return true;
}

void XGuiServer_unhost(XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    if (!d->hosted) return;
    XPlatformBackingStore_setPresentCallback(d->store, d->chainCb,
                                             d->chainData);
    d->hosted = NULL;
    d->store = NULL;
    d->chainCb = NULL;
    d->chainData = NULL;
}

XWidget* XGuiServer_hostedWidget(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return NULL;
    return d->hosted;
}

/* ==================== 监听与接入(§4.1/§4.3) ==================== */

void XGuiServer_setMaxSessions(XGuiServer* self, int maxSessions)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    if (maxSessions < 1) maxSessions = 1;
    d->maxSessions = maxSessions;
}

int XGuiServer_sessionCount(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return 0;
    return d->sessionCount;
}

int XGuiServer_attachTransport(XGuiServer* self, XIODevice* device)
{
    XGuiServerPrivate* d;
    XgsSession* s;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return -1;
    if (!device || !XIODevice_isOpen(device)) return -1;
    if (d->sessionCount >= d->maxSessions) return -1;
    if (!xgs_ensurePump(self)) return -1;
    s = xgs_sessionCreate(self, device, false);
    if (!s) return -1;
    if (!xgs_sessionStartWorker(s) || !xgs_sessionSendBanner(s)) {
        xgs_sessionClose(self, s, -1, (int)XGUI_REMOTE_BYE_PROTOCOL_ERROR);
        return -1;
    }
    return s->id;
}

void XGuiServer_detachTransport(XGuiServer* self, int sessionId)
{
    XGuiServerPrivate* d;
    XgsSession* s;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    s = d->sessionHead;
    while (s && s->id != sessionId) s = s->next;
    if (!s || s->closed || s->ownDevice) return; /* 仅直挂(借用)会话。 */
    xgs_sendBye(s, XGUI_REMOTE_BYE_NORMAL, NULL);
    xgs_sessionClose(self, s, -1, (int)XGUI_REMOTE_BYE_NORMAL);
}

/* ==================== TCP 监听 ==================== */

#if XNETWORK_ON && XNETWORK_TCPSERVER_ON

#if XGUI_REMOTE_TLS_ON
/** @brief TLS 档 accept 套接字创建器: 描述符首次绑定前构造 XSslSocket
 *         (仓内设计: 禁止第二个对象接管已绑定描述符, XTcpServer.c:149)。
 *         后续 setSocketDescriptor_base 由监听器在本对象上调用,
 *         startServerEncryption 在建会话时调用。 */
static XTcpSocket* xgs_tlsIncomingFactory(void* context)
{
    XSslSocket* ssl = XSslSocket_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (ssl) {
        /* 服务端 accept 不向客户端要证书(§3.8: 认证在协议层, TLS 只作
         * 信道加密): AutoVerifyPeer 映射 服务端=VERIFY_NONE/客户端=
         * VERIFY_REQUIRED。库默认 VerifyPeer 在服务端会强求客户端证书
         * ——mbedTLS -0x7480 "No client certification received..." fatal
         * alert 握手必败(E2E 实测根因, 2026-10-02)。 */
        XSslSocket_setPeerVerifyMode(ssl, XSSL_AutoVerifyPeer);
    }
    return (XTcpSocket*)ssl;
}
#endif /* XGUI_REMOTE_TLS_ON */

/**
 * @brief  newConnection 槽: 逐个收下待处理连接建会话(§6.5)。
 * @note   会话超限时 BYE(SESSION_LIMIT) 拒绝并关连接。
 */
static void xgs_slotNewConnection(XObject* receiver, XVarList* args)
{
    XGuiServer* self = (XGuiServer*)receiver;
    XGuiServerPrivate* d;
    XTcpSocket* sock;
    (void)args;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    if (!d->tcpServer) return;
    while ((sock = XTcpServer_nextPendingConnection_base(d->tcpServer)) !=
           NULL) {
        XIODevice* dev = (XIODevice*)sock;
        XgsSession* s;
        if (d->sessionCount >= d->maxSessions) {
            XGuiRemoteMsgBye bye;
            uint8_t buf[XGUI_REMOTE_MAX_MSG_BYTES + 16];
            const char* text = "session limit";
            size_t n;
            XMemset(&bye, 0, sizeof(bye));
            bye.reason = (uint8_t)XGUI_REMOTE_BYE_SESSION_LIMIT;
            memcpy(bye.text, text, strlen(text));
            bye.textBytes = (uint16_t)strlen(text);
            n = XGuiRemoteProto_encBye(buf, sizeof(buf), &bye);
            if (n > 0) {
                (void)XGuiRemoteProto_writeFrame(dev, XGUI_REMOTE_MSG_BYE,
                                                 buf, n);
            }
            XIODevice_close_base(dev);
            XClassDelete((XClass*)sock);
            continue;
        }
#if XGUI_REMOTE_TLS_ON
        if (d->tlsAccept) {
            /* 工厂已按策略产出 XSslSocket: 在明文连接上做服务端握手。 */
            /* 加法式扩展: 已配置证书/私钥文件路径时逐连接装载——
               mbedTLS 服务端握手必须持有本地证书与私钥(XSslSocket.c
               会话建立经 XSsl_sessionSetCertificate 接入); listen 后
               的路径改动对新建会话生效。 */
            if (d->tlsCertFile && d->tlsKeyFile) {
                XString* certStr = XString_create_utf8(d->tlsCertFile);
                XString* keyStr = XString_create_utf8(d->tlsKeyFile);
                if (certStr && keyStr) {
                    XSslSocket_setLocalCertificate_2((XSslSocket*)sock,
                                                     certStr, XSSL_Pem);
                    XSslSocket_setPrivateKey_2((XSslSocket*)sock, keyStr,
                                               XSSL_KeyAlgorithm_Rsa,
                                               XSSL_Pem, NULL);
                }
                if (certStr) XClassDelete((XClass*)certStr);
                if (keyStr) XClassDelete((XClass*)keyStr);
            }
            XSslSocket_startServerEncryption((XSslSocket*)sock);
        }
#endif
        s = xgs_sessionCreate(self, dev, true);
        if (!s || !xgs_sessionStartWorker(s) || !xgs_sessionSendBanner(s)) {
            if (s) xgs_sessionClose(self, s, -1, (int)XGUI_REMOTE_ERR_INTERNAL);
            else {
                XIODevice_close_base(dev);
                XClassDelete((XClass*)sock);
            }
            continue;
        }
    }
}

bool XGuiServer_listen_2(XGuiServer* self, const char* addressUtf8,
                         uint16_t port)
{
    XGuiServerPrivate* d;
    XHostAddress addr;
    const XHostAddress* addrPtr = NULL;
    bool ok;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    if (d->tcpServer && XTcpServer_isListening(d->tcpServer)) return false;
    /* §6.8: 方法=SHA256_CHALLENGE 但未设口令 → listen 报错拒绝。 */
    if (d->authMethod == XGUI_REMOTE_AUTH_SHA256_CHALLENGE && !d->hasPassword)
        return false;
    if (!xgs_ensurePump(self)) return false; /* 无调度器: 会话无法泵。 */
    if (!d->tcpServer) {
        d->tcpServer = XTcpServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (!d->tcpServer) return false;
        XObject_setParent((XObject*)d->tcpServer, (XObject*)self);
    }
#if XGUI_REMOTE_TLS_ON
    d->tlsAccept = xgs_tlsPolicyOf(d); /* 显式 setter 优先, 未设回退 env(原口径)。 */
    XTcpServer_setIncomingSocketFactory(
        d->tcpServer, d->tlsAccept ? xgs_tlsIncomingFactory : NULL, self);
#else
    d->tlsAccept = false;
#endif
    if (addressUtf8 && addressUtf8[0]) {
        XHostAddress_setAddress(&addr, addressUtf8);
        addrPtr = &addr;
    }
    ok = XTcpServer_listen(d->tcpServer, addrPtr, port);
    if (ok) {
        /* newConnection → GUI 线程建会话。 */
        (void)XObject_connect_1((XObject*)d->tcpServer,
                                (size_t)XTcpServer_newConnection_signal(NULL),
                                (XObject*)self, xgs_slotNewConnection,
                                XConnectionType_Direct);
    }
    return ok;
}

bool XGuiServer_listen(XGuiServer* self, uint16_t port)
{
    return XGuiServer_listen_2(self, NULL, port);
}

void XGuiServer_close(XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    while (d->sessionCount > 0 && d->sessionHead) {
        xgs_sessionClose(self, d->sessionHead,
                         (int)XGUI_REMOTE_BYE_SERVER_SHUTDOWN,
                         (int)XGUI_REMOTE_BYE_SERVER_SHUTDOWN);
    }
    if (d->tcpServer) XTcpServer_close(d->tcpServer);
}

bool XGuiServer_isListening(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    return d->tcpServer && XTcpServer_isListening(d->tcpServer);
}

uint16_t XGuiServer_serverPort(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return 0;
    return d->tcpServer ? XTcpServer_serverPort(d->tcpServer) : 0;
}

#else /* !(XNETWORK_ON && XNETWORK_TCPSERVER_ON) */

bool XGuiServer_listen_2(XGuiServer* self, const char* addressUtf8,
                         uint16_t port)
{
    (void)self; (void)addressUtf8; (void)port;
    return false;
}

bool XGuiServer_listen(XGuiServer* self, uint16_t port)
{
    (void)self; (void)port;
    return false;
}

void XGuiServer_close(XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    while (d->sessionCount > 0 && d->sessionHead) {
        xgs_sessionClose(self, d->sessionHead,
                         (int)XGUI_REMOTE_BYE_SERVER_SHUTDOWN,
                         (int)XGUI_REMOTE_BYE_SERVER_SHUTDOWN);
    }
}

bool XGuiServer_isListening(const XGuiServer* self)
{
    (void)self;
    return false;
}

uint16_t XGuiServer_serverPort(const XGuiServer* self)
{
    (void)self;
    return 0;
}

#endif /* XNETWORK_ON && XNETWORK_TCPSERVER_ON */

/* ==================== 认证(§3.8/§6.8) ==================== */

void XGuiServer_setAuthMethod(XGuiServer* self, XGuiRemoteAuthMethod method)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    if (method != XGUI_REMOTE_AUTH_NONE &&
        method != XGUI_REMOTE_AUTH_SHA256_CHALLENGE)
        return;
    d->authMethod = method;
}

XGuiRemoteAuthMethod XGuiServer_authMethod(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d))
        return XGUI_REMOTE_AUTH_NONE;
    return d->authMethod;
}

bool XGuiServer_setPassword(XGuiServer* self, const char* passwordUtf8)
{
    XGuiServerPrivate* d;
    XByteArray* digest;
    size_t len;
    const uint8_t* data;
    int64_t size;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    if (!passwordUtf8) return false;
    len = strlen(passwordUtf8);
    if (len == 0) {
        XGuiServer_clearPassword(self); /* 空串等价 clearPassword。 */
        return true;
    }
    /* 内部即刻哈希, 原文不留存(§6.8)。 */
    digest = XCryptographicHash_hash(passwordUtf8, len,
                                     XCryptographicHash_Sha256);
    if (!digest) return false;
    data = XByteArray_constData(digest);
    size = (int64_t)XByteArray_size_base(digest);
    if (!data || size != (int64_t)XGUI_REMOTE_AUTH_RESPONSE_BYTES) {
        XClassDelete(digest);
        return false;
    }
    memcpy(d->passwordHash, data, XGUI_REMOTE_AUTH_RESPONSE_BYTES);
    XClassDelete(digest);
    d->hasPassword = true;
    return true;
}

void XGuiServer_clearPassword(XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    XMemset(d->passwordHash, 0, sizeof(d->passwordHash));
    d->hasPassword = false;
}

bool XGuiServer_hasPassword(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    return d->hasPassword;
}

/* ==================== TLS 策略(加法式运行期扩展) ==================== */

void XGuiServer_setTlsEnabled(XGuiServer* self, bool enabled)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
#if XGUI_REMOTE_TLS_ON
    d->tlsOverride = enabled ? 1 : 0;
#else
    (void)enabled;
    d->tlsOverride = 0; /* 编译剔除: 显式设为明文, 不再回退 env。 */
#endif
}

bool XGuiServer_tlsEnabled(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    /* 监听中=已定版实际策略; 未监听=待生效策略(下个 listen 定版)。 */
    if (XGuiServer_isListening(self)) return d->tlsAccept;
    return xgs_tlsPolicyOf(d);
}

bool XGuiServer_setTlsCertificateFiles(XGuiServer* self,
                                       const char* certPemPathUtf8,
                                       const char* keyPemPathUtf8)
{
    XGuiServerPrivate* d;
    char* certCopy = NULL;
    char* keyCopy = NULL;
    size_t certLen;
    size_t keyLen;
    bool hasCert = certPemPathUtf8 && certPemPathUtf8[0];
    bool hasKey = keyPemPathUtf8 && keyPemPathUtf8[0];
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    if (!hasCert && !hasKey) {
        /* 空参=清除。 */
        if (d->tlsCertFile) {
            XFree_System(d->tlsCertFile);
            d->tlsCertFile = NULL;
        }
        if (d->tlsKeyFile) {
            XFree_System(d->tlsKeyFile);
            d->tlsKeyFile = NULL;
        }
        return true;
    }
    if (hasCert != hasKey) return false; /* 证书/私钥须成对。 */
#if XGUI_REMOTE_TLS_ON
    /* 先试装载校验(临时套接字), 失败不改动既有路径。 */
    if (!xgs_tlsTryLoadFiles(certPemPathUtf8, keyPemPathUtf8)) return false;
    certLen = strlen(certPemPathUtf8);
    keyLen = strlen(keyPemPathUtf8);
    certCopy = (char*)XMalloc_System(certLen + 1);
    keyCopy = (char*)XMalloc_System(keyLen + 1);
    if (!certCopy || !keyCopy) {
        if (certCopy) XFree_System(certCopy);
        if (keyCopy) XFree_System(keyCopy);
        return false;
    }
    memcpy(certCopy, certPemPathUtf8, certLen + 1);
    memcpy(keyCopy, keyPemPathUtf8, keyLen + 1);
    if (d->tlsCertFile) XFree_System(d->tlsCertFile);
    if (d->tlsKeyFile) XFree_System(d->tlsKeyFile);
    d->tlsCertFile = certCopy;
    d->tlsKeyFile = keyCopy;
    return true;
#else
    (void)certCopy;
    (void)keyCopy;
    return false; /* TLS 编译剔除: 无可生效路径。 */
#endif
}

const char* XGuiServer_tlsCertFile(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return NULL;
    return d->tlsCertFile;
}

const char* XGuiServer_tlsKeyFile(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return NULL;
    return d->tlsKeyFile;
}

/* ==================== 档位(§5.2/§5.3) ==================== */

/** @brief 换档公共尾: 更新服务器默认档 + 既有流送会话挂帧边界切换。 */
static void xgs_applyServerProfile(XGuiServer* self,
                                   const XGuiRemoteProfile* profile,
                                   XGuiRemoteProfileId id)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    XgsSession* s;
    d->profile = *profile;
    d->profileId = id;
    s = d->sessionHead;
    while (s) {
        if (!s->closed && s->state == XGUI_REMOTE_STATE_STREAMING) {
            XMutex_lock(s->m_qMutex);
            s->pendingProfile = *profile;
            s->pendingProfileId = id;
            s->switchPending = true;
            XMutex_unlock(s->m_qMutex);
        }
        s = s->next;
    }
}

void XGuiServer_setProfileId(XGuiServer* self, XGuiRemoteProfileId id)
{
    XGuiServerPrivate* d;
    XGuiRemoteProfile preset;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    switch (id) {
    case XGUI_REMOTE_PROFILE_PERFORMANCE:
        XGuiRemoteProfile_initPerformance(&preset);
        break;
    case XGUI_REMOTE_PROFILE_RESOURCE:
        XGuiRemoteProfile_initResource(&preset);
        break;
    case XGUI_REMOTE_PROFILE_AUTO:
        XGuiRemoteProfile_initAuto(&preset); /* V1 等价 resource。 */
        break;
    default:
        return; /* CUSTOM 无预设形态, 走 setProfile。 */
    }
    xgs_applyServerProfile(self, &preset, id);
}

void XGuiServer_setProfile(XGuiServer* self, const XGuiRemoteProfile* profile)
{
    XGuiServerPrivate* d;
    XGuiRemoteProfile sanitized;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d) || !profile) return;
    sanitized = *profile;
    XGuiRemoteProfile_sanitize(&sanitized);
    xgs_applyServerProfile(self, &sanitized, XGUI_REMOTE_PROFILE_CUSTOM);
}

void XGuiServer_setAllowClientProfile(XGuiServer* self, bool allow)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    d->allowClientProfile = allow;
}

XGuiRemoteProfileId XGuiServer_profileId(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d))
        return XGUI_REMOTE_PROFILE_PERFORMANCE;
    return d->profileId;
}

/* ==================== 会话控制 ==================== */

void XGuiServer_kickSession(XGuiServer* self, int sessionId)
{
    XGuiServerPrivate* d;
    XgsSession* s;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    s = d->sessionHead;
    while (s && s->id != sessionId) s = s->next;
    if (!s || s->closed) return; /* id 非法为 no-op。 */
    xgs_sendBye(s, XGUI_REMOTE_BYE_NORMAL, NULL);
    xgs_sessionClose(self, s, -1, (int)XGUI_REMOTE_BYE_NORMAL);
}

#endif /* XGUI_REMOTE_ON */
