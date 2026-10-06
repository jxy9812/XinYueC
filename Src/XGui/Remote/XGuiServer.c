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

#include "XGuiRemoteAuth.h"
#include "XGuiRemoteCodec.h"
#include "XGuiRemoteAdapt.h" /* [perf9 路4] scrcpy 式自适应降档阶梯(纯函数
                              * 状态机, 编码线程私有状态; XGuiRemoteAdapt_Test
                              * 直含同头单测)。 */
#include "XMemory.h"
#include "XString.h"
#include "XDateTime.h"
#include "XAtomic.h"
#include "XThread.h"
#include "XMutex.h"
#include "XWaitCondition.h" /* [perf9 路3] 双核并行编码 helper 同步(仓内 XSync 件)。 */
#include "XAbstractEventDispatcher.h"
#include "XAbstractNetIoRing.h" /* 基准优化: 编码入队后唤醒主循环等待(2026-10-02)。 */
#include "XTimer.h"             /* 帧泵兜底定时器(基准优化 2026-10-02)。 */
#include "XRandomGenerator.h"
/* XCryptographicHash 直用已收编 XGuiRemoteAuth.c(2026-10-04 认证收口)。 */
#include "XImage.h"
#include "XImageFormat.h"
#include <stdio.h>  /* snprintf: 标题定版缓存写入(2026-10-02 标题跟随修复)。 */
#include <string.h> /* strcmp: 标题变化比对(同修复)。 */
#include <stdlib.h> /* strtol: MemAvailable 解析(2026-10-06 OOM 根修)。 */
#include "XGeometry.h"
#include "XWidget.h"
#include "XWidget_Protected.h" /* widgetForWindow: 弹层顶层控件反查(2026-10-06)。 */
#include "XGuiApplication.h" /* topLevelAt: 注入按 global 命中顶层(弹层/多窗)。 */
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
#include "XHostAddress.h" /* UDP 旁路: TCP 对端地址 → 回送目的(设计稿 §1)。 */
#include "XPrintf.h"      /* UDP 旁路建链/降级诊断(stdout 行缓冲, 联调 grep 锚)。 */
#endif /* XNETWORK_ON && XNETWORK_TCPSERVER_ON */

#if XGUI_REMOTE_TLS_ON && XNETWORK_ON && XNETWORK_TCPSERVER_ON
#include "XSslSocket.h"
#endif /* XGUI_REMOTE_TLS_ON && XNETWORK_ON && XNETWORK_TCPSERVER_ON */

/* UDP 低延迟旁路通道(2026-10-04 加法式; 设计稿 out/mcgs-campaign/
 * udp-design.md)。通道实现收编于 XGuiRemoteUdpChannel.*, 本文件只留
 * 挂接行(会话字段/收发分流/协商消息/静默降级), 降低合并冲突面。 */
#include "XGuiRemoteUdpChannel.h"

/* UDP 旁路静默判定(毫秒): 激活会话无合法数据报超时即回落 TCP(§4.3)。 */
#define XGS_UDP_SILENCE_MS 3000

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
/** @brief FB_REQUEST 全量刷新服务端限频(毫秒): 窗口内重复请求合并为一轮
 *         全量——防 15fps 节拍外请求风暴(2026-10-05 方案③); 全量交付节拍
 *         另由 maxFps 认领门控限速, 双层限速。 */
#define XGS_FB_REQUEST_MIN_MS      1000

/* [perf9 路3 输入驱动认领门+双核并行编码(2026-10-05)] 缺省值(env 可调,
 * 全部经 xgs_envInt 读取一次缓存, 与 xgs_latencyRleFallbackPct 同纪律):
 *   - 认领门: 近 XGS_GATE_ACTIVE_MS_DEFAULT 毫秒内有远端输入注入 → 豁免
 *     maxFps 门控立即认领(TurboVNC continuous updates 同款思路, 打掉
 *     账本定谳的门控等待主犯); 无输入从交互档位(30/60fps 联动)按 ×2
 *     指数衰减回基础帧率(plan.maxFps), 静默期 CPU 不高于原静态门控。
 *   - 并行编码: 认领 tile 数 ≥ XGS_ENC_SPLIT_MIN_DEFAULT 才劈两半交双
 *     worker(A33 双核), 小批恒单线程零额外内存。 */
#define XGS_GATE_ACTIVE_MS_DEFAULT 500
#define XGS_ENC_SPLIT_MIN_DEFAULT  16
/* [交付修复路 2026-10-06] 空闲 scratch 裁剪: 会话无伤害持续 ≥ 本值毫秒,
 * 编码线程释放兆级线程私有 scratch(workBuf/encBuf/encBufB/batchBuf/
 * outA/B.stream), 静默期 MemAvailable 谷值抬高——真机 A33 之上
 * performance 档 scratch ~2.6MB 常驻是测量定谳内存低谷(5008-6864kB,
 * 距历史临界 3908kB 仅 ~1.1MB)与 tmpfs OOM 击杀事件的主可修项。
 * 零客户端可见变化: tile 哈希去重态(hashTab/hashValid)保留不裁, 唤醒
 * 后首轮经 xgs_ensureBufLocked/growBufLocked 原路重配(每轮整写缓冲,
 * 无陈旧读); 分配失败走既有「全量标志恢复重试」安全网(树内 2026-10-05
 * 停摆根修同款)。0=禁用(一键回退)。 */
#define XGS_BUF_TRIM_IDLE_MS_DEFAULT 5000
/* 大认领阈值: 单轮认领 tile 数 ≥ 本值才算「忙碌」刷新裁剪基准。
 * [2026-10-06 第二轮修正 16→40] 标准镜像会话实测(bufbusy 序列+buftrim=0,
 * RSS 13s 恒平): FPS HUD(悬浮层 180×87px=6×3 tile)每 1.8s 恰产 18 tile
 * 涓流 ≥ 旧阈值 16 → 基准被永久刷新, 裁剪成死码。40=「整页级认领」下界
 * (真机整页切换 35-475 tile), 高于全部已知常驻涓流(HUD 18/秒时钟 6/滑杆
 * 点击 6-18); env XGUI_REMOTE_TRIM_ACTIVE_TILES 可调。误判无碍: workBuf
 * 已按本轮脏 tile 数配额, 涓流期 cap 仅 KB 级。 */
#define XGS_TRIM_ACTIVE_TILES 40

/* ==================== 私有类型 ==================== */

/** @brief [2026-10-06 弹层合成] 可见独立顶层(对话框/弹层)快照条目。
 *  @details 弹窗是对话框/弹层等独立顶层窗口, 不在宿主后备存储内——
 *           远端合成按「宿主存储 + 弹层存储按全局几何叠加」双源贴装。 */
typedef struct XgsPopupEntry {
    XWidget* top;              /**< 弹层顶层控件(身份键, 借用)。 */
    XPlatformBackingStore* store; /**< 弹层平台后备存储(借用)。 */
    XRect    rect;             /**< 弹层矩形(主窗坐标域, 已换算)。 */
} XgsPopupEntry;

/** @brief 编码队列节点: 一帧 FB_UPDATE 的完整负载(帧级头+tile 记录)。 */
typedef struct XgsQueueNode {
    struct XgsQueueNode* next;      /**< 链尾。 */
    uint32_t             epoch;     /**< 认领时的计划代际(换档/换尺寸后作废)。 */
    size_t               len;       /**< 负载字节数。 */
    bool                 forceTcp;  /**< [2026-10-06 首帧全量根修] 全量轮批次
                                         *   强制 TCP: UDP 数据报静默丢失无
                                         *   重传(小批 tile 丢=区域恒黑, 真机
                                         *   实测换档后顶部行永不补齐), 全量
                                         *   轮是低频一次性交付, 走 TCP 保
                                         *   完整; 稳态伤害批照走 UDP。 */
    /* [stage] 探针戳(XGUI_REMOTE_STAGE_PROF; 关闭恒 0=段采样跳过):
     * claimUs=本轮认领始, enqueueUs=本批入队时刻(同进程单调钟 µs)。 */
    uint64_t             stageClaimUs;
    uint64_t             stageEnqueueUs;
    /* uint8_t data[len] 负载紧跟其后(节点按 len 变长分配)。 */
} XgsQueueNode;

/** @brief 节点负载首址。 */
#define XGS_NODE_DATA(node) ((uint8_t*)((node) + 1))

/* [perf9 路3 双核并行编码(2026-10-05)] 认领 tile 集按发送序劈两半:
 * 主 worker 编 [0,split), helper 编 [split,claimed), 归并后由主 worker
 * 统一重放拆帧 fold——单核语义逐字节等价(硬断言, env
 * XGUI_REMOTE_ENC_PARANOID=1 每轮自比 对)。半区产物: 编码载荷流 + 逐
 * tile 长度(-1=去重跳过/编码失败) + 延议哈希更新表(哈希位图字节级
 * RMW 不耐并发, 归并后由主 worker 单写回填, 语义与就地更新等价:
 * 去重只比对上轮值, 轮内无跨 tile 读)。 */
/** @brief 半区编码产物(缓冲由 worker 持有, ensureBuf 增长)。 */
typedef struct XgsHalfOut {
    uint8_t* stream;        /**< 编码载荷流(本区逐 tile 顺序拼接)。 */
    size_t   streamCap;
    size_t   streamLen;     /**< 本区已写流字节。 */
    int*     tileLen;       /**< 逐 tile 编码字节数(-1=跳过), 下标=发送序。 */
    size_t   tileLenCap;
    int*     pendIdx;       /**< 延议哈希更新(tile 序号)。 */
    size_t   pendIdxCap;
    uint32_t* pendHash;     /**< 延议哈希更新(FNV 值)。 */
    size_t   pendHashCap;
    int      pendCount;     /**< 延议更新条数(≤ 区间 tile 数)。 */
    uint8_t* tileCodec;     /**< 逐 tile 实际编码器(下标=发送序; [路3×路5
                             * 并集] 产线携带逐 tile 择码结果供 fold 如实
                             * 记录; tileLen<0 时值无意义)。 */
    size_t   tileCodecCap;
    int      pickRle;       /**< [路5] 本区 RLE 胜出 tile 数(trace 汇总)。 */
} XgsHalfOut;

/** @brief 一次半区编码作业描述(worker 填, helper 只读; 生命周期=join 前)。 */
typedef struct XgsEncJob {
    const uint8_t* workBuf;     /**< 认领 tile 槽区(worker 私有, 只读)。 */
    size_t   slotBytes;
    const int* idxBuf;          /**< 槽区序 → tile 序号。 */
    const int* orderBuf;        /**< 发送序排列(NULL=认领序)。 */
    const uint32_t* hashTab;    /**< 上轮哈希表(去重比对; 只读)。 */
    const uint8_t* hashValid;   /**< 上轮哈希有效位图(只读)。 */
    int      begin, end;        /**< 本区发送序区间 [begin,end)。 */
    int      bpp, tw, th, gridW, shadowW, shadowH;
    int      wireFormat;        /**< XGuiRemotePixelFormat(计划快照)。 */
    int      codec;             /**< XGuiRemoteCodecId(计划快照)。 */
    bool     tilePick;          /**< [路5] 逐 tile 动态 raw/RLE 择小(仅
                                 * RAW 档开; 产线内单/双核同路径)。 */
    int      zlibLevel;
    bool     full;              /**< 本轮全量语义(去重豁免)。 */
    uint8_t* encScratch;        /**< 本区私有编码输出缓冲。 */
    size_t   encScratchCap;
    XgsHalfOut* out;            /**< 产物落点(worker 分配, 区间私有)。 */
} XgsEncJob;

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

    /* ---- UDP 旁路(2026-10-04 加法式; 仅 GUI 线程访问) ---- */
    XGuiRemoteUdpChannel* udp;              /**< 通道(借用; 绑定后非 NULL)。 */
    uint32_t             udpToken;          /**< 会话凭据(OFFER 时定版)。 */
    bool                 udpActive;         /**< 帧/输入当前走 UDP。 */
    size_t               udpSplitCap;       /**< 编码拆帧帽(SIZE_MAX=不限;
                                               UDP 激活=数据报负载帽)。 */

    XGuiRemoteFrameReader reader;           /**< 增量帧泵(GUI 线程)。 */
    uint8_t              bannerBuf[XGUI_REMOTE_BANNER_BYTES]; /**< 横幅攒字节。 */
    int                  bannerGot;         /**< 已攒横幅字节数。 */

    uint32_t             caps;              /**< 协商后的能力交集(GUI 写)。 */
    uint8_t              nonce[XGUI_REMOTE_AUTH_NONCE_BYTES]; /**< 挑战 nonce。 */
    bool                 authRequired;      /**< 本连接要求挑战应答认证
                                                 (HELLO_ACK 时定版)。 */
    bool                 authOk;            /**< 挑战应答已通过(匿名接入恒
                                                 false, 与 authRequired 区分
                                                 形态)。 */
    int                  authFailures;      /**< 本连接认证失败累计(对照
                                                 failureLimit 判 RETRY/REJECT)。 */

    /* ---- 计划参数/影子/队列(m_qMutex) ---- */
    XMutex*              m_qMutex;          /**< 会话锁(堆对象, XMutex 结构不透明)。 */
    XGuiRemoteProfile    profile;           /**< 本会话生效档位(已按能力夹取)。 */
    XGuiRemoteProfileId  profileId;         /**< 生效档位 id。 */
    uint32_t             epoch;             /**< 计划代际(换档/换尺寸 +1)。 */
    bool                 stopThread;        /**< 编码线程停机标志。 */
    bool                 backpressure;      /**< 帧尾待写缓冲存在(GUI 写, 线程读)。 */
    int64_t              lastClaimMs;       /**< 上次认领时刻(maxFps 门控基准)。 */
    XGuiRemoteAdapt      adapt;             /**< [perf9 路4] 自适应降档阶梯
                                             * 状态(编码线程私有, 无锁; 门控
                                             * 取 min(档位, 阶梯值), 换档即
                                             * 重置——与 PROFILE_SET 并集)。 */
    bool                 allTilesDirty;     /**< 全量刷新请求(FB_REQUEST/换档/resize)。
                                                 *   [2026-10-06 首帧全量根修] 触发后
                                                 *   转为 fullActive 分轮扫, 不再一轮
                                                 *   全格认领——见 fullActive 注。 */
    bool                 fullActive;        /**< 全量轮进行中(2026-10-06 首帧根修):
                                                 *   连接/换档/全量请求的全格补发按
                                                 *   fullCursor 分轮认领, 每轮入队字节
                                                 *   有帽(XGS_FULL_PACE_BYTES)——一轮
                                                 *   突发全帧曾把 resource 256KB 编码
                                                 *   队列打满丢最旧批/UDP 数据报突发
                                                 *   超客户端收包缓冲, 丢批 tile 脏位
                                                 *   已消费且无重挂=「没变化的区域恒
                                                 *   黑」(用户真机两路实测)。 */
    int                  fullCursor;        /**< 全量轮下一待扫 tile 下标
                                                 *   (fullActive 时有效; m_qMutex)。 */
    int64_t              fullReArmMs;       /**< 上次因丢批重启全量轮时刻
                                                 *   (500ms 冷却防热旋; m_qMutex)。 */
    bool                 shadowDumped;      /**< [debug] 影子已落盘过一次。 */
    bool                 nextFrameTcp;      /**< [2026-10-06] 泵发送下帧强制
                                             *   TCP(全量轮批; 单帧标志, 泵
                                             *   发送后清零; GUI 线程单写)。 */
    XAtomic_bool         hasDamage;         /**< 有新伤害(采集置位, 线程认领清零)。 */
    uint64_t             stageCaptureUs;    /**< [stage] 最近采集完成戳(m_qMutex;
                                                 0=无; 编码线程读算 srv-lat)。 */
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
    uint32_t*            tileChg;           /**< 逐 tile 距上次交付新增伤害覆盖
                                                 字节累计(tile 优先级排序键,
                                                 2026-10-05 方案④; 认领即清零;
                                                 gridW*gridH 项; m_qMutex)。 */
    int16_t              viewX, viewY;      /**< 客户端可见视口左上(远端画面
                                                 坐标=影子坐标; FB_REQUEST
                                                 通告; 2026-10-05; m_qMutex)。 */
    int16_t              viewW, viewH;      /**< 视口宽高(0=未通告=整幅;
                                                 resize 后作废待重通告)。 */
    int64_t              lastFullReqMs;     /**< 上次接受的全量刷新请求时刻
                                                 (FB_REQUEST 限频基准;
                                                 0=无; m_qMutex)。 */
    bool                 needFirstCapture;  /**< 首帧根修(2026-10-05): 有宿主
                                                 后备存储时, 首次真实采集落地
                                                 前编码线程暂停认领——防首轮
                                                 全量编码从未采集的黑影子。 */
    bool                 capturedOnce;      /**< 影子已被真实 present 采集过
                                                 至少一次(captureForSession
                                                 置位; m_qMutex)。 */
    /* ---- [perf9 路3] 输入驱动认领门(m_qMutex) ---- */
    int64_t              lastInputMs;       /**< 最近远端输入注入时刻(GUI 线程
                                                 dispatch 写; 0=无; 豁免窗 T 基准)。 */
    int64_t              lastDamageMs;      /**< 最近脏区采集落地时刻(present
                                                 回调写; 0=无; 衰减观测用)。 */
    int64_t              gateDecayMs;       /**< 静默期认领间隔衰减状态
                                                 (0=未起步; 从交互档位 ×2
                                                 指数涨回基础间隔)。 */

    /* ---- [perf9 路3] 双核并行编码 helper(与编码线程同生命周期) ---- */
    XThread*             encHelper;         /**< helper 线程对象(拥有; 可为
                                                 NULL=创建失败恒单线程)。 */
    XVarList*            encHelperArgs;     /**< helper 参数块(close 释放)。 */
    XMutex*              encMutex;          /**< helper 握手锁(可为 NULL=禁用)。 */
    XWaitCondition*      encCond;           /**< helper 条件变量(可为 NULL)。 */
    int                  encState;          /**< 0=idle 1=作业在身 2=完成
                                                 3=停机(encMutex)。 */
    XgsEncJob*           encJob;            /**< 在途作业指针(worker 填,
                                                 helper 只读; encMutex 护)。 */

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
    uint64_t               stageInputUs;    /**< [stage] 最近输入注入戳(仅 GUI 线程
                                                 写读; 0=无; srv-in2fb 打点基准)。 */
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
    XGuiRemoteAuthServerStore auth;         /**< 认证凭据与失败策略(2026-10-04
                                                 收编原 authMethod/passwordHash/
                                                 hasPassword 三散字段; 实现主体
                                                 见 XGuiRemoteAuth.c, 访问口令
                                                 单旋钮语义见 XGuiServer.h 加法
                                                 式扩展节)。 */
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
    bool                   pumpWalking;     /**< 泵遍历在途(重入守卫; 2026-10-05
                                                 sessionClose→设备 delete 重入
                                                 事件循环嵌套泵崩潰根修)。 */
    /* ---- [2026-10-06 弹层合成] 独立顶层(对话框/弹层)跟踪(GUI 线程) ---- */
    XgsPopupEntry          lastPopups[8];   /**< 上一轮已合成弹层集(恢复用)。 */
    int                    lastPopupCount;  /**< lastPopups 有效个数。 */
    bool                   udpEnabled;      /**< UDP 旁路开关(默认开;
                                                 加法式运行期扩展)。 */
    XGuiRemoteUdpChannel*  udpChannel;      /**< 共享 UDP 通道(NULL=未建;
                                                 单绑定会话, 设计稿 §2)。 */
#if XNETWORK_ON && XNETWORK_TCPSERVER_ON
    XTcpServer*            tcpServer;       /**< TCP 监听器(拥有, parent=self)。 */
#endif
} XGuiServerPrivate;

/* ==================== 前向声明 ==================== */

static void xgs_sessionClose(XGuiServer* self, XgsSession* s,
                             int byeReason, int emitReason);
static XWindow* xgs_targetWindow(XGuiServer* self);
static void xgs_sendUdpOffer(XGuiServer* self, XgsSession* s);

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

/** @brief 服务器广播能力位(§3.5; UDP 通道就绪时加告 CAP_UDP)。 */
static uint32_t xgs_serverCaps(const XGuiServerPrivate* d)
{
    uint32_t caps = (uint32_t)XGUI_REMOTE_CAP_RGB565 |
                    (uint32_t)XGUI_REMOTE_CAP_TOUCH |
                    (uint32_t)XGUI_REMOTE_CAP_IME |
                    (uint32_t)XGUI_REMOTE_CAP_PROFILE_SET |
                    (uint32_t)XGUI_REMOTE_CAP_LATENCY | /* latency 档
                        (2026-10-05); 未宣告 peer 的会话逐会话降级 resource,
                        夹取点见 xgs_handleHello/applyServerProfile。 */
                    (uint32_t)XGUI_REMOTE_CAP_FB_REQUEST;
    if (XGuiRemoteCodec_hasCodec(XGUI_REMOTE_CODEC_ZLIB))
        caps |= (uint32_t)XGUI_REMOTE_CAP_ZLIB;
    if (d && d->udpChannel &&
        XGuiRemoteUdpChannel_socketReady(d->udpChannel)) {
        caps |= (uint32_t)XGUI_REMOTE_CAP_UDP;
    }
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

/**
 * @brief  按 rect(影子/缓冲坐标)标记覆盖的脏 tile; 调用方持有 m_qMutex。
 * @note   同时把伤害覆盖字节数(rect∩tile 面积×线上 bpp)累计进 tileChg[ti]
 *         ——tile 优先级排序键(方案④: 覆盖字节与"变化大先看见"单调一致;
 *         真像素差需留前帧副本, A33 内存不可承, 覆盖字节为零内存代理)。
 *         认领即清零, 多次 present 跨圈自然累加。
 */
static void xgs_markDirtyRect(XgsSession* s, int bx, int by, int bw, int bh)
{
    int tw = xgs_maxInt(s->profile.tileWidth, 1);
    int th = xgs_maxInt(s->profile.tileHeight, 1);
    int bpp = xgs_bppOf(s->profile.wireFormat);
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
            if (s->tileChg) {
                int ox = xgs_maxInt(bx, gx * tw);
                int oy = xgs_maxInt(by, gy * th);
                int ow = xgs_minInt(bx + bw, (gx + 1) * tw) - ox;
                int oh = xgs_minInt(by + bh, (gy + 1) * th) - oy;
                if (ow > 0 && oh > 0)
                    s->tileChg[i] += (uint32_t)ow * (uint32_t)oh *
                                     (uint32_t)bpp;
            }
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

    bool convertedOk = false;
    if (s->shadowInited && convertOld && formatChanged &&
        s->shadowW == w && s->shadowH == h) {
        /* 同尺寸换档: 逐行转换旧内容, 保留最后一帧画面。
         * [perf9 路2 根修 2026-10-05] 转换成功必须跳过下方统一 reinit:
         * XImage_reinit_ex 语义=换新分配(旧缓冲释放, 新缓冲零化),
         * 原 fall-through reinit 把刚转换好的整幅画面抹成全零——
         * 换档全量刷新编码全零影子, 客户端整幅黑(真机 performance↔
         * resource 热切换黑屏的根因; 离屏 R4 实证 resource tile
         * RLE 载荷仅 12B=纯色)。 */
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
            convertedOk = true;
        } else {
            XClassDeinit(&converted);
        }
    } else if (s->shadowInited) {
        XClassDeinit(&s->shadow);
        XImage_init(&s->shadow);
    }

    if (!convertedOk &&
        !XImage_reinit_ex(&s->shadow, w, h, imgFmt)) {
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

    if (s->tileChg) XFree_System(s->tileChg);
    s->tileChg = (uint32_t*)XMalloc_System(sizeof(uint32_t) * (size_t)tiles);
    if (s->tileChg)
        XMemset(s->tileChg, 0, sizeof(uint32_t) * (size_t)tiles);
    s->viewW = 0; /* 几何已变: 旧视口作废, 客户端 FB_META 后重通告。 */
    s->viewH = 0;

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
 *         UDP 旁路(2026-10-04): FB_UPDATE 且通道激活时帧改走数据报
 *         (最新帧优先, 设计稿 §4.1); 未递交(未绑定/无目的)回落 TCP 本帧。
 */
static bool xgs_sessionSendFrame(XgsSession* s, XGuiRemoteMsgType type,
                                 const void* payload, size_t payloadBytes)
{
    int64_t accepted;
    size_t total;
    if (s->tailBuf) return true;
    if (!s->nextFrameTcp && s->udpActive && s->udp &&
        type == XGUI_REMOTE_MSG_FB_UPDATE &&
        payloadBytes <= XGUI_REMOTE_UDP_MAX_PAYLOAD) {
        if (XGuiRemoteUdpChannel_sendFrame(s->udp, (const uint8_t*)payload,
                                           payloadBytes, xgs_nowMs())) {
            return true;
        }
        /* 回落 TCP: 帧序(fbSequence)单调, 客户端按序应用, 会话不中断。 */
    }
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

/** @brief 键盘/IME 目标窗口 = 镜像顶层控件窗口句柄(XWidget.h:1125);
 *         无句柄丢弃。指针类注入不走本函数: 走 xgs_mapInjectTarget
 *         按坐标命中(弹层/多顶层语义)。 */
static XWindow* xgs_targetWindow(XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d) || !d->hosted)
        return NULL;
    return XWidget_windowHandle(d->hosted);
}

/** @brief 指针类协议坐标(镜像帧)→注入靶统一换算。
 *  @details 三步, 与 XPlatformFbInput 本地触摸路径逐口径对齐:
 *           ① 坐标钳制进镜像客户区(§7.4: 注入前裁剪);
 *           ② 补 global=宿主窗原点+客户区坐标([2026-10-03 互联测试
 *              指挥官修复归因] 真机 fbdev 实证: NULL global 致注入事件
 *              无人接收——WSI 事件派发的命中链按 global 查找, fbinput
 *              本地路径传非 NULL global 且实测可达控件);
 *           ③ 按 global 命中实际顶层(XGuiApplication_topLevelAt, 后登
 *              记在上)——组合框弹层等 Popup 子窗与本地触摸同样可达,
 *              命中失败回退宿主窗(点仍在镜像客户区内, 理论不可达, 双
 *              重防御)。局部坐标按命中顶层几何换算(命中弹层时=弹层局
 *              部)。
 *  @return  false=无宿主窗不可注入; true=*outWin/*outLocal/*outGlobal
 *           有效(outGlobal 屏幕帧, outLocal 命中顶层局部帧)。 */
static bool xgs_mapInjectTarget(XGuiServer* self, int16_t x, int16_t y,
                                XWindow** outWin, XPoint* outLocal,
                                XPoint* outGlobal)
{
    XGuiServerPrivate* d;
    XWindow* hosted;
    XWindow* hit;
    int cx, cy;
    XPoint g;
    XRect wg;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d) || !d->hosted)
        return false;
    hosted = XWidget_windowHandle(d->hosted);
    if (!hosted) return false;
    cx = xgs_minInt(xgs_maxInt((int)x, 0),
                    xgs_maxInt(XWidget_width(d->hosted), 1) - 1);
    cy = xgs_minInt(xgs_maxInt((int)y, 0),
                    xgs_maxInt(XWidget_height(d->hosted), 1) - 1);
    wg = XWindow_geometry(hosted);
    g.x = wg.x + cx;
    g.y = wg.y + cy;
    hit = XGuiApplication_topLevelAt(&g);
    if (!hit) hit = hosted;
    wg = XWindow_geometry(hit);
    outLocal->x = g.x - wg.x;
    outLocal->y = g.y - wg.y;
    *outGlobal = g;
    *outWin = hit;
    return true;
}

static void xgs_injectPointer(XGuiServer* self,
                              const XGuiRemoteMsgInputPointer* m)
{
    XWindow* win;
    XEventType type;
    XPoint pos;
    XPoint g;
    if (!m) return;
#if defined(XGS_CLOSE_DEBUG) && XGS_CLOSE_DEBUG
    fprintf(stderr, "XGS_INJECT action=%d xy=(%d,%d)\n",
            (int)m->action, (int)m->x, (int)m->y);
#endif
    switch (m->action) {
    case XGUI_REMOTE_PTR_PRESS:     type = XEVENT_TYPE_MOUSE_BUTTON_PRESS; break;
    case XGUI_REMOTE_PTR_RELEASE:   type = XEVENT_TYPE_MOUSE_BUTTON_RELEASE; break;
    case XGUI_REMOTE_PTR_DBL_CLICK: type = XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK; break;
    case XGUI_REMOTE_PTR_MOVE:      type = XEVENT_TYPE_MOUSE_MOVE; break;
    default: return; /* 封闭枚举: dec 已拦截, 双重防御。 */
    }
    if (!xgs_mapInjectTarget(self, m->x, m->y, &win, &pos, &g)) return;
    /* 时间戳用服务器本地单调钟([2026-10-03 归因] m->timestampMs 携带
     * 客户端时钟基准, 跨机透传属无定义语义; fbinput 本地注入路径以自
     * 身读取时刻打戳, 本注入对齐该语义)。 */
    (void)XWindowSystemInterface_handleMouseEvent_ex(
        win, type, (XMouseButton)m->button, (XMouseButton)m->buttons,
        (XKeyboardModifiers)m->modifiers, pos, &g, xgs_nowMs());
}

static void xgs_injectKey(XGuiServer* self, const XGuiRemoteMsgInputKey* m)
{
    XWindow* win = xgs_targetWindow(self);
    XEventType type;
    if (!win || !m) return;
    if (m->action == XGUI_REMOTE_KEY_PRESS) type = XEVENT_TYPE_KEY_PRESS;
    else if (m->action == XGUI_REMOTE_KEY_RELEASE) type = XEVENT_TYPE_KEY_RELEASE;
    else return;
    /* [2026-10-03 互联测试指挥官修复归因] 键注入时间戳同 pointer 对齐
     * 服务器本地单调钟(跨机时间戳透传无定义语义, 见 xgs_injectPointer
     * 处归因)。 */
    (void)XWindowSystemInterface_handleKeyEvent_ex(
        win, type, (int)m->key, (XKeyboardModifiers)m->modifiers,
        false, m->nativeScanCode, xgs_nowMs());
}

static void xgs_injectWheel(XGuiServer* self, const XGuiRemoteMsgInputWheel* m)
{
    XWindow* win;
    XPoint pos;
    XPoint g;
    XPoint angle;
    if (!m) return;
    if (!xgs_mapInjectTarget(self, m->x, m->y, &win, &pos, &g)) return;
    angle.x = m->angleX;
    angle.y = m->angleY;
    /* WSI 滚轮入口无 global/timestamp 完整负载版(既有签名): 命中用局部
     * 坐标(pos), 语义完整; 时间戳缺失属入口能力边界, 非注入缺陷。 */
    (void)XWindowSystemInterface_handleWheelEvent(
        win, (XMouseButton)m->buttons, (XKeyboardModifiers)m->modifiers,
        pos, &angle);
}

static void xgs_injectTouch(XGuiServer* self, const XGuiRemoteMsgInputTouch* m)
{
    XGuiServerPrivate* d;
    XWindow* hit;
    XTouchPoint points[XGUI_REMOTE_MAX_TOUCH_POINTS];
    XEventType type;
    XRect wg;
    int w, h;
    int count, i, cx, cy;
    XPoint g0;
    XPoint p0; /* [perf8 路 B 修复(2026-10-05)] 本路径不消费 outLocal, 但
                * mapInjectTarget 契约恒写之(原传 NULL 在 :676 解引用即崩
                * ——基线即崩, 触摸注入自快照以来不可用; 补占位恢复契约)。 */
    if (!m || m->pointCount <= 0) return;
    d = (XGuiServerPrivate*)(self ? self->m_d : NULL);
    if (!d || !d->hosted) return;
    /* 目标顶层按主点(列表首点)命中(fbinput 同口径), 整帧同窗注入。 */
    if (!xgs_mapInjectTarget(self, m->points[0].x, m->points[0].y,
                             &hit, &p0, &g0))
        return;
    switch (m->action) {
    case XGUI_REMOTE_TOUCH_BEGIN:  type = XEVENT_TYPE_TOUCH_BEGIN; break;
    case XGUI_REMOTE_TOUCH_UPDATE: type = XEVENT_TYPE_TOUCH_UPDATE; break;
    case XGUI_REMOTE_TOUCH_END:    type = XEVENT_TYPE_TOUCH_END; break;
    case XGUI_REMOTE_TOUCH_CANCEL: type = XEVENT_TYPE_TOUCH_CANCEL; break;
    default: return;
    }
    wg = XWindow_geometry(hit);
    w = xgs_maxInt(XWidget_width(d->hosted), 1);
    h = xgs_maxInt(XWidget_height(d->hosted), 1);
    count = xgs_minInt(m->pointCount, XGUI_REMOTE_MAX_TOUCH_POINTS);
    for (i = 0; i < count; ++i) {
        const XGuiRemoteMsgTouchPoint* p = &m->points[i];
        XPoint gi;
        /* 各点钳制(§7.4)→global(宿主原点+局部, 同 pointer 归因)→按命
         * 中顶层几何换回局部: global/local 两字段自此语义分离(fbinput
         * 同款——此前 global 填客户区坐标属错位)。 */
        cx = xgs_minInt(xgs_maxInt((int)p->x, 0), w - 1);
        cy = xgs_minInt(xgs_maxInt((int)p->y, 0), h - 1);
        gi.x = wg.x + cx;
        gi.y = wg.y + cy;
        points[i].m_id = (int32_t)p->id;
        points[i].m_state = (int)p->state;
        points[i].m_position.x = gi.x - wg.x;
        points[i].m_position.y = gi.y - wg.y;
        points[i].m_globalPosition = gi;
        points[i].m_pressure = (float)p->pressureQ8 / 255.0f;
    }
    /* 时间戳同 pointer 对齐服务器本地单调钟(跨机透传无定义语义)。 */
    (void)XWindowSystemInterface_handleTouchPoints_ex(hit, type, points,
                                                      count, xgs_nowMs());
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

/* 首帧/优先级探针(XGUI_REMOTE_FIRSTFRAME_PROF=1 启用; 2026-10-05 方案③④
 * 交付度量用, stderr 单行, 同上纪律——交付前可关)。 */
static int xgs_ffProf = -1;
#define XGS_FF_PROF_ON() \
    (xgs_ffProf < 0 ? (xgs_ffProf = \
        (XSystem_environment("XGUI_REMOTE_FIRSTFRAME_PROF") != NULL)) \
                    : xgs_ffProf)
/* tile 优先级旋钮(XGUI_REMOTE_TILE_PRIORITY=0 关, 缺省/其他值开;
 * 方案④ A/B 测量用, 关闭即回 grid 扫描序, 行为与历史版本一致)。 */
static int xgs_tilePrio = -1;
#define XGS_TILE_PRIO_ON() \
    (xgs_tilePrio < 0 ? (xgs_tilePrio = \
        (XSystem_environment("XGUI_REMOTE_TILE_PRIORITY") == NULL || \
         XSystem_environment("XGUI_REMOTE_TILE_PRIORITY")[0] != '0')) \
                      : xgs_tilePrio)

/* ==================== [perf9 路3] 输入驱动认领门旋钮 ====================
 * 全部 env 读取一次缓存(与 xgs_latencyRleFallbackPct 同纪律):
 *   XGUI_REMOTE_GATE_OFF      =1 恢复原静态 maxFps 门控(一键回退)。
 *   XGUI_REMOTE_GATE_ACTIVE_MS 豁免窗 T(缺省 500, 0..10000): 近 T 毫秒内
 *                             有远端输入注入 → 豁免门控立即认领。
 *   XGUI_REMOTE_GATE_BOOST_FPS 交互档位联动(缺省 0=自动: plan.maxFps≥60
 *                             →60, 否则 30); 亦为静默衰减起点。
 *   XGUI_REMOTE_GATE_TRACE    =1 逐认领打印门控决策(衰减可观测性红线)。
 *   XGUI_REMOTE_ENC_WORKERS   并行编码 worker 数(缺省 2=A33 双核; 1=单核)。
 *   XGUI_REMOTE_ENC_SPLIT_MIN 劈半认领 tile 数下限(缺省 16)。
 *   XGUI_REMOTE_ENC_PARANOID  =1 每轮并行 vs 单核逐字节自比(不等即
 *                             abort 硬断言; 验收用, 有性能代价)。 */
static int xgs_envInt(const char* name, int dflt, int lo, int hi)
{
    const char* env = XSystem_environment(name);
    int v = dflt;
    if (env && env[0] >= '0' && env[0] <= '9') {
        int i;
        v = 0;
        for (i = 0; i < 6 && env[i] >= '0' && env[i] <= '9'; ++i)
            v = v * 10 + (env[i] - '0');
    }
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v;
}

static int xgs_gateOffEnv(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = xgs_envInt("XGUI_REMOTE_GATE_OFF", 0, 0, 1);
    return cached;
}

/** @brief 全量轮单轮入队字节帽(2026-10-06 首帧全量根修)。
 *  @details XGUI_REMOTE_FULL_PACE_BYTES, 缺省 128KB: 连接/换档的全格
 *           补发按此分轮(resource 64 tile/轮 ≈ 10 轮填满 1024x600,
 *           30fps 门控下 ~0.3s; performance 2 tile/轮)。0=关停分轮
 *           (回退一轮突发旧行为, 度量 A/B 用)。帽须 ≤ 编码队列半幅
 *           (resource 256KB), RLE 最坏微膨胀亦有余量。 */
static int xgs_fullPaceBytesEnv(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = xgs_envInt("XGUI_REMOTE_FULL_PACE_BYTES", 128 * 1024,
                            0, 8 * 1024 * 1024);
    return cached;
}

/** @brief [2026-10-06 OOM 根修] ARGB32 档内存门: MemAvailable 不足时降级。
 *  @details performance 档会话成本=ARGB32 影子 2.4MB+编码队列 2MB+
 *           scratch——A33 真机(56MB 无 swap, 空闲可用 ~11MB)实测切档即
 *           OOM-kill(内核杀进程=客户端「连接中断」+ 服务端监听消失,
 *           fg 复现 EXIT=137)。读 /proc/meminfo MemAvailable, 低于
 *           XGUI_REMOTE_ARGB32_MIN_AVAIL_KB(缺省 12288)则降级 resource
 *           (RGB565/32tile/256KB 队列, RLE 对 UI 内容足够); 解析失败
 *           (非 Linux/受限 fs)=放行不阻塞。scrcpy 式「能力不可用自动
 *           降档」映射, 设计稿 GitHub 清单 ②。 */
static bool xgs_argb32MemOk(void)
{
    int minKb = xgs_envInt("XGUI_REMOTE_ARGB32_MIN_AVAIL_KB", 12288,
                           0, 1 << 20);
    int maxTotalMb = xgs_envInt("XGUI_REMOTE_ARGB32_MAX_TOTAL_MB", 256,
                                0, 1 << 20);
    FILE* f;
    char line[128];
    long avail = -1;
    long total = -1;
    if (minKb == 0 && maxTotalMb == 0)
        return true; /* 旋钮全置 0=关停内存门(度量 A/B 用)。 */
    f = fopen("/proc/meminfo", "r");
    if (!f) return true;
    while (fgets(line, sizeof(line), f)) {
        if (avail < 0 && strncmp(line, "MemAvailable:", 13) == 0)
            avail = strtol(line + 13, NULL, 10);
        else if (total < 0 && strncmp(line, "MemTotal:", 9) == 0)
            total = strtol(line + 9, NULL, 10);
        if (avail >= 0 && total >= 0) break;
    }
    fclose(f);
    if (avail < 0 && total < 0) return true;
    /* [2026-10-06] 双门: 瞬时可用不足 OR 小内存板(总计低于阈值——
     * performance 档成本 ~4.5MB 起步, A33 56MB 板运行期挤兑实证,
     * 确定性钳住: 小板恒 resource, 大板看瞬时可用)。 */
    if (maxTotalMb > 0 && total >= 0 &&
        total < (long)maxTotalMb * 1024L)
        return false;
    if (minKb > 0 && avail >= 0 && avail < (long)minKb)
        return false;
    return true;
}

static int xgs_gateActiveMsEnv(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = xgs_envInt("XGUI_REMOTE_GATE_ACTIVE_MS",
                            XGS_GATE_ACTIVE_MS_DEFAULT, 0, 10000);
    return cached;
}

static int xgs_gateBoostFpsEnv(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = xgs_envInt("XGUI_REMOTE_GATE_BOOST_FPS", 0, 0, 240);
    return cached;
}

static int xgs_gateTraceEnv(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = xgs_envInt("XGUI_REMOTE_GATE_TRACE", 0, 0, 1);
    return cached;
}

static int xgs_encWorkersEnv(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = xgs_envInt("XGUI_REMOTE_ENC_WORKERS", 2, 1, 2);
    return cached;
}

static int xgs_encSplitMinEnv(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = xgs_envInt("XGUI_REMOTE_ENC_SPLIT_MIN",
                            XGS_ENC_SPLIT_MIN_DEFAULT, 1, 4096);
    return cached;
}

/** @brief 空闲 scratch 裁剪阈值(env XGUI_REMOTE_BUF_TRIM_IDLE_MS, 毫秒;
 *         0=禁用回退)。仅编码线程在无伤害暂停分支消费, 每圈一次比较。 */
static int xgs_bufTrimIdleMsEnv(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = xgs_envInt("XGUI_REMOTE_BUF_TRIM_IDLE_MS",
                            XGS_BUF_TRIM_IDLE_MS_DEFAULT, 0, 600000);
    return cached;
}

/** @brief 大认领阈值(env XGUI_REMOTE_TRIM_ACTIVE_TILES, tile 数; 缺省 40,
 *         高于全部已知常驻涓流——HUD 18/秒时钟 6/滑杆点击 6-18, 低于整页
 *         切换 35-475 下界之外的杂区)。仅认领后刷新基准处消费一次。 */
static int xgs_trimActiveTilesEnv(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = xgs_envInt("XGUI_REMOTE_TRIM_ACTIVE_TILES",
                            XGS_TRIM_ACTIVE_TILES, 1, 4096);
    return cached;
}

static int xgs_encParanoidEnv(void)
{
    static int cached = -1;
    if (cached < 0)
        cached = xgs_envInt("XGUI_REMOTE_ENC_PARANOID", 0, 0, 1);
    return cached;
}
/* [perf9 路4 自动降档(2026-10-05)] XGUI_REMOTE_ADAPT_FPS=0 关自适应阶梯
 * (恒按档位 maxFps 门控, 历史行为); 缺省/其他值开。开启后门控帧率 =
 * min(档位 maxFps, 阶梯值), 编码轮耗时/队列水位超预算沿 60→30→15 阶梯
 * 下行、富余迟滞回升——A33 performance 档 29.21ms 单轮 > 16.7ms 预算崩塌
 * 至 0.05fps 的对症根修(硬钉高帧率只积压+丢批)。同 getenv 纪律。 */
static int xgs_adaptOn = -1;
#define XGS_ADAPT_ON() \
    (xgs_adaptOn < 0 ? (xgs_adaptOn = \
        (XSystem_environment("XGUI_REMOTE_ADAPT_FPS") == NULL || \
         XSystem_environment("XGUI_REMOTE_ADAPT_FPS")[0] != '0')) \
                     : xgs_adaptOn)

/* [perf9 路4] XGUI_REMOTE_ADAPT_BUDGET_MS: 单轮(认领→入队)预算 ms 显式
 * 覆盖; 缺省 0=派生(1000/当前阶梯 fps, 即 60→16.7ms/30→33.3ms/15→66.7ms)。
 * 显式>0 供压测/量化工具强制阶梯行为。同 getenv 纪律。 */
static int xgs_adaptBudget = -1;
static int xgs_adaptBudgetMs(void)
{
    if (xgs_adaptBudget < 0) {
        const char* env = XSystem_environment("XGUI_REMOTE_ADAPT_BUDGET_MS");
        int v = 0;
        if (env && env[0] >= '0' && env[0] <= '9') {
            int i;
            for (i = 0; i < 5 && env[i] >= '0' && env[i] <= '9'; ++i) {
                v = v * 10 + (env[i] - '0');
            }
        }
        if (v < 0) v = 0;
        xgs_adaptBudget = v;
    }
    return xgs_adaptBudget;
}

/* [perf9 路4] 阶梯移动探针(XGUI_REMOTE_ADAPT_PROF=1 启用; stderr 单行,
 * 量化/联调用, 同 BENCH_TRACE 纪律)。 */
static int xgs_adaptProf = -1;
#define XGS_ADAPT_PROF_ON() \
    (xgs_adaptProf < 0 ? (xgs_adaptProf = \
        (XSystem_environment("XGUI_REMOTE_ADAPT_PROF") != NULL)) \
                       : xgs_adaptProf)

/**
 * @brief  把 flushedRegion(窗口坐标)逐矩形转格式拷入会话影子缓冲并标脏。
 * @note   在 present 包装回调内调用——回调返回后后备缓冲翻转, 必须回调内
 *         拷贝(设计红线 §6.2)。锁内完成本会话全部矩形拷贝与标脏。
 */
static void xgs_captureForSession(XgsSession* s, XImage* img,
                                  const XRegion* region,
                                  const XPoint* offset)
{
    /* [2026-10-06 修 ASan stack-use-after-scope] 整幅采集的宿主矩形
     * 声明提升到函数作用域——指针经 region 逃逸 if 块后在块外消费,
     * 块内声明即 use-after-scope(ASan 必现 ABORT, memhunt leak9 实证)。 */
    XRect wholeRect;     /* 全幅采集矩形(窗口坐标域)。 */
    XRegion wholeRegion; /* 单矩形区域视图。 */
    int w = XImage_width(img);
    int h = XImage_height(img);
    int srcStride = XImage_bytesPerLine(img);
    XGuiRemotePixelFormat srcPf = xgs_pfOfImageFormat(XImage_format(img));
    int srcBpp = xgs_bppOf(srcPf);
    const uint8_t* srcBits = XImage_bits(img);
    int ox = offset ? offset->x : 0;
    int oy = offset ? offset->y : 0;
    int dstBpp, dstStride, r;
    bool dumpPending = false; /* [debug] 首次整幅采集后落盘影子。 */
    uint64_t stageT0 = XGuiRemoteUdp_stageProfNowUs(); /* [stage] 关闭恒 0。 */
    uint64_t capBytes = 0; /* [stage] 采集写影线格式字节(探针开才累计)。 */
    bool profOn = XGuiRemoteUdp_stageProfOn();

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

    /* [2026-10-06 首帧全量根修·设备侧] 全量轮期间整幅采集: 影子只被
     * flushedRegion(伤害带)刷新, 「自会话建立以来从未变化的区域」
     * (fbdev 静态标题栏/品牌条/页面静态段)影子恒为零——全量轮把
     * 零影子编成黑 tile 下发, 客户端恒黑(真机两路实测: 恰好只缺
     * 顶部静态 2 行 tile, 桌面 X11 因同步整窗 repaint 的 flush 域
     * =整窗而无此症)。全量标志在挂起或分轮进行中时, 以整幅矩形
     * 替代 flushedRegion 拷贝(后备图像即当前完整帧), 任何陈旧带
     * 一次补齐; 非全量会话不受影响。 */
    if (s->allTilesDirty || s->fullActive) {
        XRect_init(&wholeRect, ox, oy, w, h); /* 窗口坐标, 同 region 域。 */
        wholeRegion.rects = &wholeRect;
        wholeRegion.count = 1;
        wholeRegion.capacity = 1;
        region = &wholeRegion;
        if (!s->shadowDumped &&
            XSystem_environment("XGUI_REMOTE_SHADOW_DUMP") != NULL)
            dumpPending = true; /* [debug] env 门控影子落盘(根修取证)。 */
        if (XGS_BENCH_TRACE_ON())
            fprintf(stderr, "XGS_TRACE wholecap s=%p img=%p %dx%d "
                    "stride=%d pf=%d ox=%d,oy=%d\n",
                    (void*)s, (void*)srcBits, w, h, srcStride,
                    (int)srcPf, ox, oy);
    }

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
        if (profOn) capBytes += (uint64_t)(unsigned)bw * (unsigned)bh *
                               (unsigned)dstBpp;
    }
    XAtomic_store_bool(&s->hasDamage, true, XAtomic_MemoryOrder_Release);
    s->lastDamageMs = xgs_nowMs(); /* [路3] 脏区产生时刻(门控衰减观测)。 */
    if (!s->capturedOnce) {
        s->capturedOnce = true; /* 首帧根修: 真像素已落地, 放行认领。 */
        if (XGS_FF_PROF_ON())
            fprintf(stderr, "XGS_FF firstcap t=%lld tiles=%d\n",
                    (long long)xgs_nowMs(), region->count);
    }
    /* [stage] 采集完成戳(锁内写, 编码线程锁内读算 srv-lat); 探针关恒 0。 */
    s->stageCaptureUs = XGuiRemoteUdp_stageProfNowUs();
    if (dumpPending) {
        s->shadowDumped = true;
        XImage_save_2(&s->shadow, "/root/xguidemo/shadow_dump.png", "PNG", 95);
    }
    XMutex_unlock(s->m_qMutex);
    if (stageT0) XGuiRemoteUdp_stageProfSample("srv-cap", stageT0);
    if (stageT0 && capBytes > 0 &&
        capBytes <= 0xFFFFFFFFull)
        XGuiRemoteUdp_stageProfBytes("srv-cap", (uint32_t)capBytes);
    if (XGS_BENCH_TRACE_ON())
        fprintf(stderr, "XGS_TRACE present t=%llu tiles=%d\n",
                (unsigned long long)(uint64_t)xgs_nowMs(), region->count);
}

/* ==================== 弹层合成(2026-10-06, GUI 线程) ==================== */

/** @brief 收集与宿主窗相交的可见独立顶层(对话框/弹层, ≤cap)。
 *  @details 弹窗类是对话框/弹层等独立顶层窗口, 有自己的后备存储——
 *           宿主存储从不包含其像素, 远端镜像必须双源贴装。按窗口注册
 *           表序合成(后登记=更上层, 与 topLevelAt 同口径)。 */
static int xgs_collectPopups(XGuiServer* self, XgsPopupEntry* out, int cap)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    XVector* tops;
    XRect hostGeo;
    size_t i;
    size_t count;
    int n = 0;
    if (!d || !d->hosted || !out || cap <= 0) return 0;
    tops = XGuiApplication_topLevelWindows();
    if (!tops) return 0;
    hostGeo = XWidget_geometry(d->hosted);
    count = XVector_size_base((const XContainer*)tops);
    for (i = 0; i < count && n < cap; ++i) {
        XWindow* w = XVector_At_Base(tops, (int64_t)i, XWindow*);
        XWidget* top;
        XBackingStore* bs;
        XRect g;
        if (!w || !XWindow_isVisible(w)) continue;
        top = XWidget_widgetForWindow(w);
        if (!top || top == d->hosted) continue; /* 宿主窗走既有采集路径。 */
        bs = XWidget_backingStore(top);
        if (!bs || !XBackingStore_handle(bs)) continue; /* 未建存储跳过。 */
        g = XWindow_geometry(w);
        g.x -= hostGeo.x; /* 全局域 → 主窗坐标域。 */
        g.y -= hostGeo.y;
        if (g.x + g.width <= 0 || g.y + g.height <= 0 ||
            g.x >= hostGeo.width || g.y >= hostGeo.height)
            continue; /* 与宿主不相交(纯屏外窗)不合成。 */
        out[n].top = top;
        out[n].store = XBackingStore_handle(bs);
        out[n].rect = g;
        ++n;
    }
    XClassDelete((XClass*)tops);
    return n;
}

/** @brief 从平台存储拷一个矩形进会话影子并标脏(调用方持有 m_qMutex;
 *         dx/dy=存储原点在影子坐标域的位置; rect 影子坐标域, 函数内
 *         双向夹取防越界)。 */
static void xgs_blitStoreRectLocked(XgsSession* s,
                                    XPlatformBackingStore* store,
                                    int dx, int dy, XRect rect)
{
    XImage* img;
    int iw;
    int ih;
    int srcStride;
    int srcBpp;
    XGuiRemotePixelFormat srcPf;
    const uint8_t* srcBits;
    int dstStride;
    int dstBpp;
    int y;
    if (!s || !store) return;
    if (rect.x < 0) { rect.width += rect.x; rect.x = 0; }
    if (rect.y < 0) { rect.height += rect.y; rect.y = 0; }
    if (rect.x + rect.width > s->shadowW)
        rect.width = s->shadowW - rect.x;
    if (rect.y + rect.height > s->shadowH)
        rect.height = s->shadowH - rect.y;
    if (rect.width <= 0 || rect.height <= 0) return;
    img = XPlatformBackingStore_paintDevice(store);
    if (!img) return;
    iw = XImage_width(img);
    ih = XImage_height(img);
    srcStride = XImage_bytesPerLine(img);
    srcBits = XImage_bits(img);
    /* 存储原点(dx,dy) 之上的影子区无源像素, 夹掉。 */
    if (rect.x < dx) { rect.width += rect.x - dx; rect.x = dx; }
    if (rect.y < dy) { rect.height += rect.y - dy; rect.y = dy; }
    if (rect.width <= 0 || rect.height <= 0) return;
    if (rect.x - dx + rect.width > iw) rect.width = iw - (rect.x - dx);
    if (rect.y - dy + rect.height > ih) rect.height = ih - (rect.y - dy);
    if (rect.width <= 0 || rect.height <= 0 || !srcBits) return;
    srcPf = xgs_pfOfImageFormat(XImage_format(img));
    srcBpp = xgs_bppOf(srcPf);
    dstStride = XImage_bytesPerLine(&s->shadow);
    dstBpp = xgs_bppOf(s->profile.wireFormat);
    for (y = 0; y < rect.height; ++y) {
        const uint8_t* srcRow = srcBits +
                (size_t)(rect.y - dy + y) * (size_t)srcStride +
                (size_t)(rect.x - dx) * (size_t)srcBpp;
        uint8_t* dstRow = XImage_bits(&s->shadow) +
                (size_t)(rect.y + y) * (size_t)dstStride +
                (size_t)rect.x * (size_t)dstBpp;
        XGuiRemoteCodec_convertPixels(srcRow, srcPf, dstRow,
                                      s->profile.wireFormat, rect.width);
    }
    xgs_markDirtyRect(s, rect.x, rect.y, rect.width, rect.height);
}

/** @brief 弹层合成轮: 消失弹层从宿主存储恢复, 现存弹层整幅贴装, 全部
 *         标脏(编码侧哈希去重兜底, 未变化 tile 不上线)。
 *  @details 任意存储 present(宿主或弹层自身 flush)都会跑本轮——弹层
 *           打开的首个 flush 通常是整幅, 覆盖贴装即完备; 静止期无
 *           present 不空转, 宿主兜底定时器/涓流伤害驱动恢复。 */
static void xgs_popupCompositePass(XGuiServer* self)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    XgsSession* s;
    XgsPopupEntry cur[8];
    int n;
    int i;
    int j;
    if (!self || !d || !d->hosted || !d->store) return;
    n = xgs_collectPopups(self, cur, 8);
    if (n == 0 && d->lastPopupCount == 0) return; /* 常态零开销短路。 */
    s = d->sessionHead;
    while (s) {
        if (!s->closed && s->state == XGUI_REMOTE_STATE_STREAMING &&
            s->shadowInited) {
            bool touched = false;
            XMutex_lock(s->m_qMutex);
            if (s->shadowInited) {
                /* 消失弹层: 从宿主存储恢复原内容(宿主存储恒正确)。 */
                for (j = 0; j < d->lastPopupCount; ++j) {
                    bool gone = true;
                    for (i = 0; i < n; ++i) {
                        if (cur[i].top == d->lastPopups[j].top) {
                            gone = false;
                            break;
                        }
                    }
                    if (gone) {
                        xgs_blitStoreRectLocked(s, d->store, 0, 0,
                                                d->lastPopups[j].rect);
                        touched = true;
                    }
                }
                /* 现存弹层: 整幅贴装(全局几何已换算主窗域)。 */
                for (i = 0; i < n; ++i) {
                    xgs_blitStoreRectLocked(s, cur[i].store,
                                            cur[i].rect.x, cur[i].rect.y,
                                            cur[i].rect);
                    touched = true;
                }
            }
            XMutex_unlock(s->m_qMutex);
            if (touched)
                XAtomic_store_bool(&s->hasDamage, true,
                                   XAtomic_MemoryOrder_Release);
        }
        s = s->next;
    }
    d->lastPopupCount = n;
    for (i = 0; i < n; ++i) d->lastPopups[i] = cur[i];
}

/** @brief 全局 present 通知入口: 非宿主存储(弹层/对话框 flush)只跑
 *         弹层合成轮(宿主存储由既有每存储回调路径处理)。 */
static void xgs_anyPresentWrapper(void* userData, XPlatformBackingStore* store,
                                  const XRegion* flushedRegion,
                                  const XPoint* offset)
{
    XGuiServer* self = (XGuiServer*)userData;
    XGuiServerPrivate* d;
    (void)flushedRegion;
    (void)offset;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    if (store == d->store) return; /* 宿主存储: 每存储回调路径已处理。 */
    xgs_popupCompositePass(self);
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
    /* [2026-10-06 弹层合成] 宿主区更新后重贴装弹层(拷底可能擦除弹层
     * 覆盖带)并处理弹层关闭恢复。 */
    xgs_popupCompositePass(self);
    if (d->chainCb) d->chainCb(d->chainData, store, flushedRegion, offset);
    /* [stage] 注入→fb 翻转前缘(设备本地单钟, srv-in2fb): 输入注入后
     * 首个呈现回调端≈fb 可见(chainCb 翻转 memcpy 在前, sub-ms 级);
     * 陈旧戳(>1s)作废, 采样后清戳(每输入至多一样本)。 */
    if (d->stageInputUs) {
        uint64_t nowUs = XGuiRemoteUdp_stageProfNowUs();
        if (nowUs && (uint64_t)(nowUs - d->stageInputUs) < 1000000ull)
            XGuiRemoteUdp_stageProfSpan("srv-in2fb", d->stageInputUs, nowUs);
        d->stageInputUs = 0;
    }
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

/**
 * @brief  [perf9 路3] 内容保留式增长(追加流专用; ensure 不拷贝旧内容,
 *         对"每轮整写"缓冲正确, 对跨轮/轮内累积流会丢前缀——流必须用本
 *         函数: 新块拷贝旧前 min(cap,need) 字节再释放旧块)。
 */
static bool xgs_growBufLocked(void** buf, size_t* cap, size_t need)
{
    void* nb;
    if (*buf && *cap >= need) return true;
    nb = XMalloc_System(need ? need : 1);
    if (!nb) return false;
    if (*buf) {
        memcpy(nb, *buf, *cap < need ? *cap : need);
        XFree_System(*buf);
    }
    *buf = nb;
    *cap = need;
    return true;
}

/**
 * @brief  冲一批入有界队列(调用方持有 m_qMutex; 满丢最旧整批并计数)。
 * @param  claimUs   [stage] 本轮认领始戳(探针关恒 0)。
 * @param  encEndUs  [stage] 本批入队时刻戳(探针关恒 0; 双非 0 时采样
 *                   srv-enc=认领→入队, 并随节点传递给泵算 srv-q)。
 */
static void xgs_flushBatchLocked(XgsSession* s, const uint8_t* batchBuf,
                                 size_t batchLen, uint32_t epoch,
                                 uint64_t claimUs, uint64_t encEndUs,
                                 bool forceTcp)
{
    XgsQueueNode* node;
    if (batchLen <= (size_t)XGUI_REMOTE_FB_UPDATE_HEADER_BYTES) return;
    node = (XgsQueueNode*)XMalloc_System(sizeof(XgsQueueNode) + batchLen);
    if (!node) return;
    node->next = NULL;
    node->epoch = epoch;
    node->forceTcp = forceTcp; /* 全量轮批走 TCP(UDP 静默丢=恒黑根修)。 */
    node->len = batchLen;
    node->stageClaimUs = claimUs;
    node->stageEnqueueUs = encEndUs;
    if (claimUs && encEndUs)
        XGuiRemoteUdp_stageProfSpan("srv-enc", claimUs, encEndUs);
    /* [stage] srv-enc 字节列=编码输出批字节(与 srv-cap 采集字节比=压缩比)。 */
    if (claimUs && encEndUs && batchLen <= 0xFFFFFFFFull)
        XGuiRemoteUdp_stageProfBytes("srv-enc", (uint32_t)batchLen);
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

/* [latency 档混合回退阈值(2026-10-05)] env XGUI_REMOTE_LATENCY_RLE_FALLBACK_PCT:
 * RAW 档认领批的脏占比(认领 tile 数/网格总数)达到该百分位时, 本批改走 RLE
 * (tile 记录自带 codec 字节, 客户端逐 tile 解码, 协议支持同帧混装)。
 * [perf8 第 3 轮改缺省 40(2026-10-05)]: 真机 A33 链路实测, 纯 raw 页切突发
 * = ~150 个 8KB UDP 数据报背靠背 → 收侧缓冲溢出丢报 → 丢帧 tile 成洞,
 * 只能靠 3s 静默兜底全量刷新愈合(日志 fallback silence>3000ms 实锤)——
 * 整页交付劣化 +42%(1774 vs 1250ms)、镜像碎片化、兜底循环反复冲高队列
 * 与 RSS(round2 OOM 33MB 同族)。脏占比≥40% 的批(页切/全量刷新)转 RLE
 * 后线载 1.2MB→~100KB(ratio 0.056-0.081, ledger §4), 无损且不成突发;
 * 小变化(<40%)仍 raw 直拷保低延迟。env 显式 0=关(恢复恒 raw),
 * 1~100=自定义阈值; 进程内读一次缓存, 与 BENCH_TRACE 同纪律。
 * 数据 out/perf8/round3/(镜像碎片样张 clt_mirror_nav.png)。 */
static int xgs_latencyRleFallbackPct(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* env = XSystem_environment(
            "XGUI_REMOTE_LATENCY_RLE_FALLBACK_PCT");
        int v = 40; /* 缺省 40: 页切/全量突发转 RLE, 小变化保 raw。 */
        if (env && env[0] >= '0' && env[0] <= '9') {
            int i;
            v = 0;
            for (i = 0; i < 3 && env[i] >= '0' && env[i] <= '9'; ++i)
                v = v * 10 + (env[i] - '0');
            if (v <= 0)
                v = 0; /* 显式 0=关闭恒 raw(覆盖缺省)。 */
        }
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        cached = v;
    }
    return cached;
}

/* [perf9 路5 任务4 逐 tile 动态 raw/RLE 2026-10-05] env
 * XGUI_REMOTE_LATENCY_TILE_PICK(缺省开): RAW 档(latency)且批级回退未
 * 触发时, 认领批内每个 tile 先 RLE 编码, 产物不小于 raw 尺寸(不可压)
 * 即改发 raw——逐 tile 取 min(raw, RLE)。记录级 codec 字节本就逐 tile,
 * 客户端逐 tile 解码(批级混装同机制), 全程 in-band, FB_META/CAP_LATENCY
 * 协商零改动, 老 peer 兼容。直击稳态小变化 7.7KB/tile raw 线载
 * (文本 UI RLE ratio 0.056-0.081, ledger §4)。env=0 回退恒 raw(现行为)。
 * 设计稿 out/perf9/latency-tile-design.md §3。 */
static bool xgs_latencyTilePickOn(void)
{
    static int cached = -1;
    if (cached < 0) {
        const char* env = XSystem_environment("XGUI_REMOTE_LATENCY_TILE_PICK");
        cached = !(env && env[0] == '0' && env[1] == '\0');
    }
    return cached != 0;
}

/* ==================== [perf9 路3] 输入驱动认领门 ==================== */

/**
 * @brief  计算本轮认领门控有效间隔(调用方持有 m_qMutex)。
 * @param  baseMs 原静态门控间隔=1000/plan.maxFps(≤0=档位不限频, 门控不设)。
 * @return 本轮有效最小认领间隔(毫秒; 0=豁免, 立即认领)。
 * @note   语义(设计稿 out/perf9/gate-design.md):
 *           - 近 XGUI_REMOTE_GATE_ACTIVE_MS 毫秒内有远端输入注入 → 豁免
 *             (eff=0), 打掉账本定谳的门控等待主犯; 同时衰减状态复位。
 *           - 无输入: 有效间隔从交互档位(30/60fps 联动, env 可调)按 ×2
 *             指数衰减回基础帧率(=plan.maxFps, 即原静态门控)——静默期
 *             CPU 不高于原实现(红线); 「脏区产生」经由衰减起点参与:
 *             无输入的本地伤害流首认领即交互档间隔, 而后回落基础档。
 *           - 红线对应: 纯静默(无输入无伤害)不产生认领, 2ms 轮询步长
 *             与原实现一致, 零额外常驻 CPU。
 */
static int64_t xgs_gateIntervalMsLocked(XgsSession* s, int64_t now,
                                        int64_t baseMs)
{
    int64_t boostMs, eff;
    int boostFps;
    if (baseMs <= 0) return 0;            /* 档位不限频: 原语义门控不设。 */
    if (xgs_gateOffEnv()) return baseMs;  /* env 一键回退: 原静态门控。 */
    boostFps = xgs_gateBoostFpsEnv();
    if (boostFps <= 0)                    /* 交互档位联动: ≥60fps 档配 60,
                                           * 其余(resource)配 30。 */
        boostFps = (baseMs <= 1000 / 60) ? 60 : 30;
    boostMs = 1000 / (int64_t)boostFps;
    if (s->lastInputMs != 0 &&
        now - s->lastInputMs < (int64_t)xgs_gateActiveMsEnv()) {
        s->gateDecayMs = boostMs; /* 活跃: 衰减复位, 静默后自交互档起步。 */
        eff = 0;                  /* 豁免: 立即认领(伤害/背压天然限速)。 */
    } else {
        /* 无输入: ×2 指数衰减, 封顶基础间隔(≥基础即恒基础)。 */
        if (s->gateDecayMs <= 0 || s->gateDecayMs < boostMs)
            s->gateDecayMs = boostMs;
        eff = s->gateDecayMs;
        if (eff > baseMs) eff = baseMs;
        s->gateDecayMs = s->gateDecayMs * 2;
        /* [交付修复路 2026-10-06] 状态封顶(测量第三轮升级告急): eff 已按
         * 基础间隔钳制, 但状态原样 ×2 无上界——GATE_TRACE 实证普通交互
         * 会话 156 次评估达 ≈2^62, 再一次 ×2 即 int64 溢出(UB, 交互越密
         * 翻倍越快)。钳到 baseMs 后 eff 输出逐位不变(本就 min(state,
         * base)), 仅状态从此有界 ∈ [boostMs, baseMs]。 */
        if (s->gateDecayMs > baseMs) s->gateDecayMs = baseMs;
    }
    if (xgs_gateTraceEnv())
        fprintf(stderr,
                "XGS_TRACE gate t=%lld exempt=%d eff=%lld base=%lld "
                "decay=%lld inAge=%lld dmgAge=%lld\n",
                (long long)now, eff == 0 ? 1 : 0, (long long)eff,
                (long long)baseMs, (long long)s->gateDecayMs,
                s->lastInputMs ? (long long)(now - s->lastInputMs) : -1LL,
                s->lastDamageMs ? (long long)(now - s->lastDamageMs) : -1LL);
    return eff;
}

/* ==================== [perf9 路3] 双核并行编码 ==================== */

/**
 * @brief  对发送序区间 [begin,end) 做 FNV 去重+编码(单核/半区共用产线)。
 * @details 产物: 编码载荷流(send 序拼接, 仅未跳过 tile)+逐 tile 长度
 *          (-1=去重跳过/编码失败/流增长失败)+延议哈希更新表。逻辑与
 *          原单核循环逐语句等价, 仅哈希回填延后至归并点(位图字节 RMW
 *          不耐双线程并发; 轮内无跨 tile 读, 语义等价见 XgsEncJob 注)。
 * @note   缓冲均为 worker 私有(ensureBuf 增长), helper 只触自己区间
 *         的产物槽, 免锁; 调用方保证 job 全字段有效。
 */
static void xgs_encodeRange(const XgsEncJob* job)
{
    XgsHalfOut* out = job->out;
    int i;
    out->streamLen = 0;
    out->pendCount = 0;
    out->pickRle = 0;
    for (i = job->begin; i < job->end; ++i) {
        int src = job->orderBuf ? job->orderBuf[i] : i;
        int ti = job->idxBuf[src];
        int tcw = xgs_minInt(job->tw,
                             job->shadowW - (ti % job->gridW) * job->tw);
        int tch = xgs_minInt(job->th,
                             job->shadowH - (ti / job->gridW) * job->th);
        const uint8_t* slot = job->workBuf + (size_t)src * job->slotBytes;
        uint32_t hash = XGuiRemoteCodec_tileHash(
            slot, (size_t)tcw * (size_t)tch * (size_t)job->bpp);
        int len = -1;
        /* 上一轮同格 tile 哈希相等则跳过(全量刷新/换档首轮不去重)。 */
        if (!job->full && (job->hashValid[ti >> 3] & (1u << (ti & 7))) &&
            job->hashTab[ti] == hash) {
            /* 去重跳过: 与单核同判。 */
        } else {
            /* [perf9 路5×路3 并集] 逐 tile 动态 raw/RLE 下沉产线(单/双核
             * 同路径): 先 RLE, 产物 ≥ raw 尺寸(不可压)即改发 raw(直拷);
             * RLE 失败一律落 raw 不劣于现状。恒 min(raw, RLE) ≤ 任一单
             * 策略线载; 择码逐 tile 确定 ⇒ PARANOID 双核=单核仍逐字节。 */
            int encoded;
            int useCodec = (int)job->codec;
            if (job->tilePick) {
                encoded = XGuiRemoteCodec_encodeTile(
                    slot, tcw * job->bpp,
                    (XGuiRemotePixelFormat)job->wireFormat, tcw, tch,
                    (XGuiRemotePixelFormat)job->wireFormat,
                    XGUI_REMOTE_CODEC_RLE, job->zlibLevel,
                    job->encScratch, job->encScratchCap);
                if (encoded >= 0 &&
                    (size_t)encoded <
                        (size_t)tcw * (size_t)tch * (size_t)job->bpp) {
                    useCodec = (int)XGUI_REMOTE_CODEC_RLE;
                    ++out->pickRle;
                } else {
                    encoded = XGuiRemoteCodec_encodeTile(
                        slot, tcw * job->bpp,
                        (XGuiRemotePixelFormat)job->wireFormat, tcw, tch,
                        (XGuiRemotePixelFormat)job->wireFormat,
                        XGUI_REMOTE_CODEC_RAW, job->zlibLevel,
                        job->encScratch, job->encScratchCap);
                    useCodec = (int)XGUI_REMOTE_CODEC_RAW;
                }
            } else {
                encoded = XGuiRemoteCodec_encodeTile(
                    slot, tcw * job->bpp,
                    (XGuiRemotePixelFormat)job->wireFormat, tcw, tch,
                    (XGuiRemotePixelFormat)job->wireFormat,
                    (XGuiRemoteCodecId)job->codec, job->zlibLevel,
                    job->encScratch, job->encScratchCap);
            }
            if (encoded >= 0 &&
                xgs_growBufLocked((void**)&out->stream, &out->streamCap,
                                  out->streamLen + (size_t)encoded)) {
                memcpy(out->stream + out->streamLen, job->encScratch,
                       (size_t)encoded);
                out->streamLen += (size_t)encoded;
                len = encoded;
                out->tileCodec[i - job->begin] = (uint8_t)useCodec;
                out->pendIdx[out->pendCount] = ti;
                out->pendHash[out->pendCount] = hash;
                ++out->pendCount;
            }
            /* 编码失败或流增长失败: len=-1 本轮放弃该 tile(与原单核
             * encoded<0 continue 同处置; 不回填哈希)。 */
        }
        out->tileLen[i - job->begin] = len;
    }
}

/** @brief 双核 helper 线程主循环: 常驻待命(XSync 件), 作业即编即报。 */
static void xgs_encHelperMain(XThread* thread, XVarList* arguments)
{
    XgsSession* s = NULL;
    (void)thread;
    if (!arguments) return;
    XVarList_args_1(arguments, void*, sessionPtr);
    s = (XgsSession*)sessionPtr;
    if (!s) return;
    for (;;) {
        XgsEncJob* job;
        XMutex_lock(s->encMutex);
        while (s->encState != 1 && s->encState != 3)
            XWaitCondition_wait(s->encCond, s->encMutex, 100);
        if (s->encState == 3) {
            XMutex_unlock(s->encMutex);
            break;
        }
        job = s->encJob;
        XMutex_unlock(s->encMutex);
        if (job) xgs_encodeRange(job); /* 作业数据为 worker 私有, 免锁。 */
        XMutex_lock(s->encMutex);
        s->encState = 2; /* 完成(异常消亡由 worker 超时+isFinished 兜底)。 */
        XWaitCondition_wakeAll(s->encCond);
        XMutex_unlock(s->encMutex);
    }
}

/**
 * @brief  编码线程主循环: 输入驱动认领门控(maxFps 基础+近输入豁免, 路3)
 *         → 加锁逐 tile 认领 → 无锁 FNV 去重/编码([perf9 路3] 可双核:
 *         按发送序劈两半 helper 并行, 归并重放拆帧 fold 字节等价) →
 *         拆帧组批 → 有界队列。
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
    uint32_t* chgBuf = NULL;   /* 认领 tile 变化量快照(线程私有; 优先级键)。 */
    size_t chgCap = 0;
    uint8_t* vpBuf = NULL;     /* 认领 tile 视口内标志(线程私有)。 */
    size_t vpCap = 0;
    uint8_t* orderBuf = NULL;  /* [perf9] 发送序排列(线程私有; 元素 int)。
                                * 2026-10-06 修: 原声明在循环体内, 每轮重置
                                * NULL+ensure 重配=旧块每轮孤儿化(ASan 实证
                                * 50min 浸泡 3527 块/497KB), 提升到线程作用
                                * 域跨轮复用。 */
    size_t orderCap = 0;
    /* [perf9 路3] 双核并行编码缓冲(线程私有, 跨轮持久容量)。 */
    XgsHalfOut outA;           /* A 半/单核产物。 */
    XgsHalfOut outB;           /* B 半产物(helper 写, 仅作业期间)。 */
    uint8_t* encBufB = NULL;   /* B 半编码 scratch(helper 触)。 */
    size_t encCapB = 0;
    uint32_t lastEpoch = 0;    /* 哈希表所属代际。 */
    uint32_t fbSequence = 0;   /* FB_UPDATE 序号(单调递增, 回绕允许)。 */
    int64_t lastBusyMs = 0;    /* 上次成功认领时刻(空闲 scratch 裁剪基准)。 */

    XMemset(&outA, 0, sizeof(outA));
    XMemset(&outB, 0, sizeof(outB));

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
        uint64_t capUs = 0;   /* [stage] 锁内读的最近采集完成戳。 */
        uint64_t claimUs = 0; /* [stage] 本轮认领始戳(探针关恒 0)。 */
        uint64_t scanT0 = 0;  /* [stage] 脏扫描+拷出起/止戳(srv-scan)。 */
        uint64_t scanEndUs = 0;
        int bpp, tw, th, gridW, gridH, shadowW, shadowH;
        bool full;
        bool passing = false;   /* [2026-10-06] 本轮属分轮全量窗口。 */
        int paceBytes = 0;      /* [2026-10-06] 单轮入队字节帽(env)。 */
        int scanFrom = 0, scanTo = 0; /* [2026-10-06] 本轮全量扫描窗口。 */
        int drops0 = 0;         /* [2026-10-06] 轮初丢批快照(重挂判定)。 */
        int claimed = 0;
        size_t slotBytes;
        int srcStride;
        const uint8_t* srcBits;
        int gx, gy, i, ti;
        size_t payloadBudget;
        size_t batchLen;
        int batchTiles;
        int vpx = 0, vpy = 0, vpw = 0, vph = 0; /* 客户端视口快照(锁内读)。 */
        size_t udpSplitCap; /* UDP 激活=数据报负载帽(SIZE_MAX=不限)。 */
        XGuiRemoteCodecId effCodec; /* [latency] 本批实际编码器(RAW 回退=RAW)。 */
        int rleFallbackPct;         /* [latency] 混合回退阈值(env, 0=关)。 */
        bool tilePickOn = false;    /* [perf9 路5] 逐 tile 动态 raw/RLE。 */
        int pickRleCount = 0;       /* [perf9 路5] 本批 RLE 胜出 tile 数。 */
        bool orderValid = false;    /* 本轮已按 order 发送。 */
        /* [perf9 路3] 双核并行编码轮内状态(缓冲为函数级持久)。 */
        XgsEncJob jobA, jobB;       /* 半区作业描述。 */
        int splitAt = 0;            /* 劈半点(发送序)。 */
        bool useHelper = false;     /* 本轮是否双核。 */

        XMutex_lock(s->m_qMutex);
        if (s->stopThread) {
            XMutex_unlock(s->m_qMutex);
            break;
        }
        if (s->backpressure ||
            (s->needFirstCapture && !s->capturedOnce) ||
            s->metaPending ||
            (!XAtomic_load_bool(&s->hasDamage, XAtomic_MemoryOrder_Acquire) &&
             !s->allTilesDirty && !s->fullActive)) {
            /* 队列水位反压(待写缓冲存在, §12 意见 1)、首采集未落地
             * (首帧根修: 全量标志等真像素, 防编码黑影子)、FB_META 未
             * 发出([perf9 路2 换档黑屏根修 2026-10-05]: 换档帧边界
             * 重分配+metaPending 后, 若 worker 抢在泵发 META 前认领,
             * 新格式 tile 先于 META 到客户端, 被格式守卫整帧丢弃,
             * 而全量标志已消费、静态屏无后续 present——镜像黑屏不愈
             * 合, 离屏 R4 实证)或无伤害: 暂停认领(脏位保留零丢失)。
             * [2026-10-06 首帧全量根修] fullActive(分轮全量进行中)
             * 同样维持唤醒——静态屏的补发轮不依赖新伤害驱动。 */
            XMutex_unlock(s->m_qMutex);
            /* [交付修复路 2026-10-06] 空闲 scratch 裁剪: 兆级线程私有
             * scratch(workBuf/encBuf/encBufB/batchBuf/outA/B.stream)在
             * 距上次「大认领」(≥XGS_TRIM_ACTIVE_TILES)耐久 trimMs 后
             * 释放, 抬高空闲 MemAvailable 谷值(测量定谳 performance×TCP
             * 谷值 5008kB, 距历史临界 3908kB 仅 1.1MB)。触发键在大认领
             * 而非零伤害: 真机 UI 有秒时钟/悬浮层等 1-3 tile 涓流伤害,
             * 零伤害静默在实机不存在; 涓流认领的下一轮重配属幂等慢循环
             * (mmap µs 级+仅触用页 fault)。
             * 安全性: 本分支仅在无认领在途时到达(helper 已 join, 作业
             * 描述即弃), 缓冲为本线程私有、免锁释放; 全部为「每轮整写」
             * 缓冲, 重配后无陈旧读; tile 哈希去重态保留→零客户端可见
             * 变化。 */
            {
                int trimMs = xgs_bufTrimIdleMsEnv();
                if (trimMs > 0 && (workBuf || encBuf || encBufB ||
                                   batchBuf || outA.stream || outB.stream)) {
                    int64_t idleNow = xgs_nowMs();
                    if (idleNow - lastBusyMs >= (int64_t)trimMs) {
                        if (workBuf) { XFree_System(workBuf); workBuf = NULL; workCap = 0; }
                        if (encBuf) { XFree_System(encBuf); encBuf = NULL; encCap = 0; }
                        if (encBufB) { XFree_System(encBufB); encBufB = NULL; encCapB = 0; }
                        if (batchBuf) { XFree_System(batchBuf); batchBuf = NULL; batchCap = 0; }
                        if (outA.stream) { XFree_System(outA.stream); outA.stream = NULL; outA.streamCap = 0; }
                        if (outB.stream) { XFree_System(outB.stream); outB.stream = NULL; outB.streamCap = 0; }
                        if (orderBuf) { XFree_System(orderBuf); orderBuf = NULL; orderCap = 0; }
                        /* 重置基准: 涓流认领的重配自下一轮起再守满 trimMs
                         * 才准再裁——把 free/realloc 空转钳到每周期一次。 */
                        lastBusyMs = idleNow;
                        if (xgs_gateTraceEnv())
                            fprintf(stderr, "XGS_TRACE buftrim t=%lld\n",
                                    (long long)idleNow);
                    }
                }
            }
            XThread_msleep(XGS_WORKER_IDLE_MS);
            continue;
        }
        plan = s->profile;
        epoch = s->epoch;
        udpSplitCap = s->udpSplitCap;
        now = xgs_nowMs();
        /* [perf9 路4] 自适应降档(2026-10-05): 档位 maxFps 变化(PROFILE_SET
         * 运行期换档)即重置阶梯——与既有逐会话降级语义取并集, 老 peer 兼容
         * (纯服务端认领节流, 无线上语义变化)。 */
        if (s->adapt.baseFps != plan.maxFps) {
            xgui_remote_adapt_reset(&s->adapt, plan.maxFps);
        }
        /* maxFps 认领门控(§12 意见 3 冻结执行点): 距上次认领不足
         * 1000/maxFps 毫秒本轮不认领——脏位保留零丢失仅延后。
         * [perf9 路3 输入驱动豁免(2026-10-05)]: 近 T 毫秒内有远端输入
         * 注入 → 豁免门控立即认领(脏位保留零丢失语义不变, 仅解除等待);
         * 无输入自交互档位(30/60fps 联动)按 ×2 指数衰减回基础帧率,
         * 静默期 CPU 不高于原静态门控(红线); XGUI_REMOTE_GATE_OFF=1
         * 一键回退原语义。
         * [perf9 路4] 自适应阶梯开启时基础帧率再取 min(档位, 阶梯值):
         * resource(15fps)平阶梯零影响, performance(60fps)超预算自动
         * 60→30→15 下行; 路3 豁免/衰减包装作用其上(阶梯值≤档位值,
         * 静默 CPU 红线不破)。设计稿 out/perf9/gate-design.md。 */
        {
            int gateFps = plan.maxFps;
            if (XGS_ADAPT_ON()) {
                gateFps = xgui_remote_adapt_fps(&s->adapt);
            }
            minIntervalMs = (gateFps > 0) ? (1000 / gateFps) : 0;
        }
        minIntervalMs = xgs_gateIntervalMsLocked(s, now, minIntervalMs);
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
        /* [2026-10-06 首帧全量根修] 全格认领改分轮: 新请求重置游标,
         * 进行中的轮按游标继续——单轮突发全帧曾把 resource 256KB 队列
         * 打满丢最旧批(top-of-screen tile 恒黑)/UDP 数据报突发超客户端
         * 收包缓冲, 脏位已消费无重挂=「没变化的区域恒黑」(真机两路
         * 实测)。每轮入队 ≤ xgs_fullPaceBytesEnv()(0=关停回退旧行为)。 */
        paceBytes = xgs_fullPaceBytesEnv();
        passing = false;
        if (s->fullActive && paceBytes <= 0) {
            /* 度量旋钮关停分轮: 残留轮态就地收清(唤醒谓词含 fullActive,
             * 不清则空转)。 */
            s->fullActive = false;
            s->fullCursor = 0;
        }
        if (full && paceBytes > 0) {
            s->fullActive = true;
            s->fullCursor = 0;
        }
        if (s->fullActive && paceBytes > 0 && gridW > 0 && gridH > 0) {
            int total = gridW * gridH;
            int perRound;
            passing = true;
            full = true;
            perRound = (paceBytes > 0)
                           ? (int)((size_t)paceBytes /
                                   ((size_t)tw * (size_t)th * (size_t)bpp))
                           : 0;
            if (perRound < 1) perRound = 1;
            scanFrom = s->fullCursor;
            if (scanFrom < 0 || scanFrom >= total) scanFrom = 0;
            scanTo = scanFrom + perRound;
            if (scanTo > total) scanTo = total;
        } else {
            scanFrom = 0;
            scanTo = gridW * gridH;
        }
        if (!s->shadowInited || !s->dirtyGrid || gridW <= 0 || gridH <= 0 ||
            shadowW <= 0 || shadowH <= 0) {
            /* [perf9 停摆根修 2026-10-05] 同下: 影子未就绪(换档重分配失败
             * /裸服务器未采集)不得吞掉已消费的全量标志——恢复重试。
             * [2026-10-06] 分轮语义下 fullActive 天然持久(游标不动),
             * 唤醒谓词含 fullActive 自会重试, 无需回写标志。 */
            XAtomic_store_bool(&s->hasDamage, false,
                               XAtomic_MemoryOrder_Release);
            XMutex_unlock(s->m_qMutex);
            XThread_msleep(XGS_WORKER_IDLE_MS);
            continue;
        }
        srcStride = XImage_bytesPerLine(&s->shadow);
        srcBits = XImage_bits(&s->shadow);
        slotBytes = (size_t)tw * (size_t)th * (size_t)bpp;

        /* 客户端视口快照(方案④; FB_REQUEST 通告, m_qMutex 保护)并夹取进
         * 影子边界; (0,0)=未通告=整幅(优先级退化纯变化量降序)。 */
        vpx = s->viewX; vpy = s->viewY; vpw = s->viewW; vph = s->viewH;
        if (vpw > 0 && vph > 0) {
            if (vpx < 0) { vpw += vpx; vpx = 0; }
            if (vpy < 0) { vph += vpy; vpy = 0; }
            if (vpx + vpw > shadowW) vpw = shadowW - vpx;
            if (vpy + vph > shadowH) vph = shadowH - vpy;
            if (vpw <= 0 || vph <= 0) { vpw = 0; vph = 0; }
        }

        /* ---- 加锁逐 tile 认领(短临界区拷出, §6.4) ----
         * [交付修复路 2026-10-06] workBuf 按本轮脏 tile 数配额(扫描前
         * 先数脏位; 槽区按发送序紧凑排布, 编码只读 [0,claimed) 槽, 与
         * 原满格配额逐位等价): 涓流认领(秒时钟 1-3 tile)不再整格驻留
         * ——配合空闲裁剪, 静默期 scratch 真正缩到 KB 级。 */
        {
            int nDirty;
            if (passing) {
                nDirty = scanTo - scanFrom; /* 分轮: 只配本轮窗口。 */
            } else if (full) {
                nDirty = gridW * gridH;
            } else {
                nDirty = 0;
                for (i = 0; i < gridW * gridH; ++i)
                    nDirty += (s->dirtyGrid[i >> 3] >> (i & 7)) & 1;
            }
            if (!xgs_ensureBufLocked((void**)&workBuf, &workCap,
                                     slotBytes * (size_t)(nDirty > 0
                                                              ? nDirty
                                                              : 1)) ||
            !xgs_ensureBufLocked((void**)&idxBuf, &idxCap,
                                 sizeof(int) * (size_t)gridW *
                                     (size_t)gridH) ||
            !xgs_ensureBufLocked((void**)&hashTab, &hashCap,
                                 sizeof(uint32_t) * (size_t)gridW *
                                     (size_t)gridH) ||
            !xgs_ensureBufLocked((void**)&hashValid, &hashValidCap,
                                 ((size_t)gridW * (size_t)gridH + 7u) / 8u) ||
            !xgs_ensureBufLocked((void**)&chgBuf, &chgCap,
                                 sizeof(uint32_t) * (size_t)gridW *
                                     (size_t)gridH) ||
            !xgs_ensureBufLocked((void**)&vpBuf, &vpCap,
                                 (size_t)gridW * (size_t)gridH) ||
            !xgs_ensureBufLocked((void**)&orderBuf, &orderCap,
                                 sizeof(int) * (size_t)gridW *
                                     (size_t)gridH)) {
            /* [perf9 停摆根修 2026-10-05] 分配失败不得吞全量请求:
             * performance 档线程缓冲 ~2.3MB(workBuf 128×128×4×grid 为主),
             * 真机内存临界(MemAvailable ~3.9MB)下失败——原实现此处丢了
             * 已消费的 allTilesDirty, 静态屏无后续伤害即永久停摆(真机
             * performance×UDP/TCP 首帧怠速停摆伺服侧根因)。恢复标志,
             * 下一唤醒重试, 内存到位即自愈。
             * [2026-10-06 分轮] 分轮窗口 retry 由 fullActive 持久承载
             * (游标未动, 唤醒谓词自会重试); 仅一轮突发旧行为需回写。 */
            if (full && !passing) s->allTilesDirty = true;
            XMutex_unlock(s->m_qMutex);
            XThread_msleep(XGS_WORKER_IDLE_MS);
            continue;
            }
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
        scanT0 = XGuiRemoteUdp_stageProfNowUs(); /* [stage] 关闭恒 0。 */
        drops0 = s->droppedBatches; /* 轮初快照(丢批重挂判定, 锁内)。 */
        for (ti = scanFrom; ti < scanTo; ++ti) {
                bool dirty = full ||
                    (s->dirtyGrid[ti >> 3] & (1u << (ti & 7))) != 0;
                int cw, ch, y;
                if (!dirty) continue;
                gx = ti % gridW;
                gy = ti / gridW;
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
                /* 优先级键随认领快照并清零(markDirtyRect 侧累计)。 */
                chgBuf[claimed] = s->tileChg ? s->tileChg[ti] : 0;
                if (s->tileChg) s->tileChg[ti] = 0;
                vpBuf[claimed] = (uint8_t)(vpw > 0 && vph > 0 &&
                    gx * tw < vpx + vpw && vpx < gx * tw + cw &&
                    gy * th < vpy + vph && vpy < gy * th + ch);
                ++claimed;
        }
        /* [2026-10-06 首帧全量根修] 游标推进(仍持锁): 窗口扫完即翻页,
         * 全格扫完收轮。窗口外新伤害位保留, 轮后正常脏扫补 delivery。 */
        if (passing) {
            s->fullCursor = scanTo;
            if (scanTo >= gridW * gridH) {
                s->fullActive = false;
                s->fullCursor = 0;
            }
        }
        XAtomic_store_bool(&s->hasDamage, false, XAtomic_MemoryOrder_Release);
        capUs = s->stageCaptureUs; /* [stage] 锁内读采集戳(srv-lat 基准)。 */
        scanEndUs = XGuiRemoteUdp_stageProfNowUs(); /* [stage] 关闭恒 0。 */
        XMutex_unlock(s->m_qMutex);
        /* [交付修复路 2026-10-06] 大认领才刷新裁剪基准(claimed 此处已定
         * 值): HUD 18/秒时钟 6/滑杆点击 6-18 tile 级涓流不顶住 scratch 的
         * 大用量占用, 整页级认领(≥阈值, 缺省 40)才刷新——涓流识别口径
         * 见第二轮测量死码定谳(buftrim=0+RSS 恒平实证)。 */
        if (claimed >= xgs_trimActiveTilesEnv()) {
            lastBusyMs = now;
            if (xgs_gateTraceEnv())
                fprintf(stderr, "XGS_TRACE bufbusy t=%lld claimed=%d\n",
                        (long long)now, claimed);
        }
        if (claimed <= 0) continue;
        claimUs = XGuiRemoteUdp_stageProfNowUs();
        /* [stage] srv-scan=锁内脏扫描+tile 拷出时长(采样在锁外, 防汇总
         * fprintf 落在临界区内); srv-lat=采集端→认领始(maxFps 门控+线程
         * 唤醒等待+扫描, 两者以 srv-scan 收窄); 陈旧戳(全量刷新无新采集,
         * >2s)防御性丢弃。 */
        if (scanT0 && scanEndUs)
            XGuiRemoteUdp_stageProfSpan("srv-scan", scanT0, scanEndUs);
        if (claimUs && capUs && claimUs > capUs &&
            claimUs - capUs < 2000000ull)
            XGuiRemoteUdp_stageProfSpan("srv-lat", capUs, claimUs);
        if (XGS_BENCH_TRACE_ON())
            fprintf(stderr, "XGS_TRACE claim t=%llu tiles=%d\n",
                    (unsigned long long)(uint64_t)now, claimed);

        /* ---- 方案④(2026-10-05): 交付优先级排序 ----
         * 视口内 tile 先行(1:1 视口=客户端 FB_REQUEST 通告; 未通告/FIT=
         * 整幅, 退化为纯变化量序), 组内按距上次交付变化量(伤害覆盖字节,
         * markDirtyRect 累计)降序, 同量按 tile 序号稳定。插入排序: 单轮
         * 认领 ≤ grid 规模(resource 608), 最坏 ~0.2M 次整数比较, 可承。
         * 只重排本轮已认领 tile 的发送次序——不改帧序号/拆帧预算/队列
         * FIFO, UDP 最新帧优先(seq 去重)语义不受影响。
         * [perf9 路2 根修 2026-10-05] 原实现对 idxBuf/chgBuf/vpBuf 三组
         * 元数据就地插入排序, 而 workBuf 槽区保持认领序——编码循环
         * `slot = workBuf + i*slotBytes` 与 `ti = idxBuf[i]` 从此错配:
         * 第 i 个发送的 tile 坐标=重排序第 i 项, 像素=认领序第 i 项,
         * **像素与坐标系统性错配**(乱序 mosaic, 真机 performance 档
         * 「白块碎片/叠影」的直接来源; 离屏回环实证, 关闭
         * XGUI_REMOTE_TILE_PRIORITY 即恢复)。修法=阶排列: 排序
         * order[](0..claimed-1 的排列), 编码循环按 order 取
         * (槽, 坐标, 键)三元组, 槽内容零搬移。 */
        if (XGS_TILE_PRIO_ON() && claimed > 1) {
            for (i = 0; i < claimed; ++i)
                ((int*)orderBuf)[i] = i; /* orderBuf 元素为 int。 */
            for (i = 1; i < claimed; ++i) {
                int oi = ((int*)orderBuf)[i];
                uint32_t tc = chgBuf[oi];
                uint8_t tv = vpBuf[oi];
                int ti = idxBuf[oi];
                int j = i - 1;
                while (j >= 0) {
                    int oj = ((int*)orderBuf)[j];
                    if (!(vpBuf[oj] < tv ||
                          (vpBuf[oj] == tv &&
                           (chgBuf[oj] < tc ||
                            (chgBuf[oj] == tc && idxBuf[oj] > ti)))))
                        break;
                    ((int*)orderBuf)[j + 1] = ((int*)orderBuf)[j];
                    --j;
                }
                ((int*)orderBuf)[j + 1] = oi;
            }
            orderValid = true;
        }
        if (XGS_FF_PROF_ON()) {
            int k;
            int m = claimed < 8 ? claimed : 8;
            fprintf(stderr,
                    "XGS_FF claim t=%lld tiles=%d full=%d vp=%d "
                    "first=%d,%d chg=%u order=",
                    (long long)now, claimed, full ? 1 : 0,
                    (vpw > 0 && vph > 0) ? 1 : 0,
                    (idxBuf[0] % gridW) * tw, (idxBuf[0] / gridW) * th,
                    (unsigned)chgBuf[0]);
            for (k = 0; k < m; ++k) {
                int oi = orderValid ? ((int*)orderBuf)[k] : k;
                fprintf(stderr, "%d,%d/%u%s", (idxBuf[oi] % gridW) * tw,
                        (idxBuf[oi] / gridW) * th, (unsigned)chgBuf[oi],
                        k + 1 < m ? " " : "\n");
            }
        }
        /* [latency 档混合回退(2026-10-05)] RAW 认领批脏占比 ≥ 阈值时本批
         * 改走 RLE; 认脏/哈希去重不动(判脏≠编码), 编码与记录头换 effCodec,
         * 客户端逐 tile 按记录 codec 解码。阈值缺省 0=恒 raw。 */
        effCodec = plan.codec;
        rleFallbackPct = xgs_latencyRleFallbackPct();
        if (effCodec == XGUI_REMOTE_CODEC_RAW && rleFallbackPct > 0) {
            size_t gridTotal = (size_t)gridW * (size_t)gridH;
            if (gridTotal > 0 &&
                (size_t)claimed * 100u >= (size_t)rleFallbackPct * gridTotal) {
                effCodec = XGUI_REMOTE_CODEC_RLE;
                if (XGS_BENCH_TRACE_ON())
                    fprintf(stderr, "XGS_TRACE rle-fallback tiles=%d/%u\n",
                            claimed, (unsigned)gridTotal);
            }
        }
        /* [perf9 路5] 逐 tile 动态 raw/RLE: 仅批级回退未触发(仍 RAW)时
         * 生效——批级已转 RLE 的页切/全量批维持原整批 RLE 语义。 */
        if (effCodec == XGUI_REMOTE_CODEC_RAW && xgs_latencyTilePickOn())
            tilePickOn = true;

        /* ---- [perf9 路3] 无锁编码(可双核): 产线 xgs_encodeRange 对发送
         *      序区间做 FNV 去重 → 影子已为线上格式零转换 → RLE/zlib;
         *      单核=单区间内联, 双核=劈两半(前半主 worker/后半 helper,
         *      XSync 件握手)后主 worker 单点回填延议哈希, 再统一重放
         *      拆帧 fold: 首留 8 字节帧级头, tile 记录顺序追加, 单帧
         *      tile 载荷合计 ≤ txBudgetBytes, 单个超预算大 tile 独立成
         *      帧; 收尾统一补写帧级头并入队——双核与单核逐字节等价
         *      (XGUI_REMOTE_ENC_PARANOID=1 每轮自比, 不等即 abort)。---- */
        {
            size_t maxEnc = XGuiRemoteCodec_maxEncodedSize(plan.codec, tw, th,
                                                           bpp);
            /* [perf9 路5 并集] 逐 tile 动态 raw/RLE 开时缓冲按 max(RAW, RLE)
             * 上界分配(RLE 最坏=raw+raw/128, XGuiRemoteCodec.c)。 */
            if (plan.codec == XGUI_REMOTE_CODEC_RAW && tilePickOn) {
                size_t rleNeed = XGuiRemoteCodec_maxEncodedSize(
                    XGUI_REMOTE_CODEC_RLE, tw, th, bpp);
                if (rleNeed > maxEnc) maxEnc = rleNeed;
            }
            size_t tiles = (size_t)claimed;
            useHelper = (xgs_encWorkersEnv() >= 2 && claimed > 1 &&
                         claimed >= xgs_encSplitMinEnv() &&
                         s->encHelper != NULL && s->encMutex != NULL &&
                         s->encCond != NULL);
            splitAt = useHelper ? (claimed + 1) / 2 : claimed;
            /* A 半(或单核全区)缓冲必配; B 半仅双核配, 失败回退单核。 */
            if (useHelper &&
                (!xgs_ensureBufLocked((void**)&encBufB, &encCapB,
                                      maxEnc + 16u) ||
                 !xgs_ensureBufLocked((void**)&outB.tileLen,
                                      &outB.tileLenCap,
                                      sizeof(int) * tiles) ||
                 !xgs_ensureBufLocked((void**)&outB.pendIdx,
                                      &outB.pendIdxCap,
                                      sizeof(int) * tiles) ||
                 !xgs_ensureBufLocked((void**)&outB.pendHash,
                                      &outB.pendHashCap,
                                      sizeof(uint32_t) * tiles) ||
                 !xgs_ensureBufLocked((void**)&outB.tileCodec,
                                      &outB.tileCodecCap,
                                      sizeof(uint8_t) * tiles)))
                useHelper = false;
            splitAt = useHelper ? (claimed + 1) / 2 : claimed;
            if (!xgs_ensureBufLocked((void**)&encBuf, &encCap,
                                     maxEnc + 16u) ||
                !xgs_ensureBufLocked((void**)&batchBuf, &batchCap,
                     (size_t)XGUI_REMOTE_FB_UPDATE_HEADER_BYTES +
                         xgs_maxSize(plan.txBudgetBytes, maxEnc + 12u) +
                         16u) ||
                !xgs_ensureBufLocked((void**)&outA.tileLen, &outA.tileLenCap,
                                     sizeof(int) * tiles) ||
                !xgs_ensureBufLocked((void**)&outA.pendIdx, &outA.pendIdxCap,
                                     sizeof(int) * tiles) ||
                !xgs_ensureBufLocked((void**)&outA.pendHash, &outA.pendHashCap,
                                     sizeof(uint32_t) * tiles) ||
                !xgs_ensureBufLocked((void**)&outA.tileCodec,
                                     &outA.tileCodecCap,
                                     sizeof(uint8_t) * tiles)) {
                /* [perf9 停摆根修 2026-10-05] 分配失败: 本批放弃(脏位已清)。
                 * 全量刷新语义必须自愈——原实现此处终止后静态屏再无伤害帧,
                 * 全量轮永不补发(增量 tile 才是「下一伤害帧兜底」)。恢复
                 * allTilesDirty 整轮回扫(超集补齐), 睡眠限速防热旋。
                 * [2026-10-06 分轮] 窗口 retry 由 fullActive 持久承载
                 * (脏位虽清, 窗口内 tile 按 full 语义重扫交付); 仅一轮
                 * 突发旧行为需回写标志。认领门控自限速, 无热旋。 */
                if (XGS_BENCH_TRACE_ON())
                    fprintf(stderr, "XGS_TRACE allocfail-A claimed=%d\n",
                            claimed);
                if (full && !passing) {
                    XMutex_lock(s->m_qMutex);
                    s->allTilesDirty = true;
                    XMutex_unlock(s->m_qMutex);
                    XThread_msleep(XGS_WORKER_IDLE_MS);
                }
                continue;
            }
            jobA.workBuf = workBuf;
            jobA.slotBytes = slotBytes;
            jobA.idxBuf = idxBuf;
            jobA.orderBuf = orderValid ? (const int*)orderBuf : NULL;
            jobA.hashTab = hashTab;
            jobA.hashValid = hashValid;
            jobA.begin = 0;
            jobA.end = useHelper ? splitAt : claimed;
            jobA.bpp = bpp;
            jobA.tw = tw;
            jobA.th = th;
            jobA.gridW = gridW;
            jobA.shadowW = shadowW;
            jobA.shadowH = shadowH;
            jobA.wireFormat = (int)plan.wireFormat;
            jobA.codec = (int)effCodec; /* [latency] 混合回退与单核同源。 */
            jobA.tilePick = tilePickOn; /* [路5] 产线内逐 tile 择码(下行)。 */
            jobA.zlibLevel = plan.zlibLevel;
            jobA.full = full;
            jobA.encScratch = encBuf;
            jobA.encScratchCap = encCap;
            jobA.out = &outA;
            if (useHelper) {
                jobB = jobA;
                jobB.begin = splitAt;
                jobB.end = claimed;
                jobB.encScratch = encBufB;
                jobB.encScratchCap = encCapB;
                jobB.out = &outB;
                /* helper 作业派发(XSync 件握手): 指针双方在 join 前有效。 */
                XMutex_lock(s->encMutex);
                s->encJob = &jobB;
                s->encState = 1;
                XWaitCondition_wakeAll(s->encCond);
                XMutex_unlock(s->encMutex);
            }
            xgs_encodeRange(&jobA); /* 主 worker 编前半(或全区)。 */
            if (useHelper) {
                bool helperDone = false;
                XMutex_lock(s->encMutex);
                while (s->encState == 1) {
                    if (XThread_isFinished(s->encHelper)) break;
                    XWaitCondition_wait(s->encCond, s->encMutex, 100);
                }
                helperDone = (s->encState == 2);
                s->encState = 0;
                s->encJob = NULL;
                XMutex_unlock(s->encMutex);
                if (!helperDone)
                    xgs_encodeRange(&jobB); /* helper 消亡兜底: 单核补齐。 */
            }
            if (xgs_encParanoidEnv()) {
                /* 硬断言: 双核产物与单核参照逐字节等价。必须在延议哈希
                 * 回填**之前**跑——参照的去重判据须与双核同源(上轮值)。 */
                XgsHalfOut ref;
                XgsEncJob refJob = jobA;
                size_t offR = 0, offA2 = 0, offB2 = 0;
                int refBad = 0;
                XMemset(&ref, 0, sizeof(ref));
                refJob.begin = 0;
                refJob.end = claimed;
                refJob.out = &ref;
                if (xgs_ensureBufLocked((void**)&ref.tileLen, &ref.tileLenCap,
                                        sizeof(int) * tiles) &&
                    xgs_ensureBufLocked((void**)&ref.pendIdx, &ref.pendIdxCap,
                                        sizeof(int) * tiles) &&
                    xgs_ensureBufLocked((void**)&ref.pendHash,
                                        &ref.pendHashCap,
                                        sizeof(uint32_t) * tiles) &&
                    xgs_ensureBufLocked((void**)&ref.tileCodec,
                                        &ref.tileCodecCap,
                                        sizeof(uint8_t) * tiles)) {
                    xgs_encodeRange(&refJob);
                    for (i = 0; i < claimed && !refBad; ++i) {
                        int src = orderValid ? ((const int*)orderBuf)[i] : i;
                        /* [路3] 与 fold 同口径: tileLen/stream 按发送序 i 取。 */
                        int lenP = (i < splitAt) ? outA.tileLen[i]
                                                 : outB.tileLen[i - splitAt];
                        int lenR = ref.tileLen[i];
                        if (lenP != lenR) {
                            fprintf(stderr,
                                    "XGS_ENC_PARANOID: tile send=%d slot=%d "
                                    "ti=%d lenP=%d lenR=%d\n",
                                    i, src, idxBuf[src], lenP, lenR);
                            refBad = 1;
                            break;
                        }
                        if (lenP < 0) continue;
                        if (memcmp((i < splitAt) ? outA.stream + offA2
                                                 : outB.stream + offB2,
                                   ref.stream + offR, (size_t)lenP) != 0) {
                            int zb;
                            const uint8_t* pp = (i < splitAt)
                                ? outA.stream + offA2 : outB.stream + offB2;
                            for (zb = 0; zb < lenP; ++zb)
                                if (pp[zb] != ref.stream[offR + zb])
                                    break;
                            fprintf(stderr,
                                    "XGS_ENC_PARANOID: tile send=%d slot=%d "
                                    "ti=%d len=%d first diff at %d "
                                    "(P=%02x R=%02x)\n",
                                    i, src, idxBuf[src], lenP, zb, pp[zb],
                                    ref.stream[offR + zb]);
                            refBad = 1;
                            break;
                        }
                        if (i < splitAt) offA2 += (size_t)lenP;
                        else offB2 += (size_t)lenP;
                        offR += (size_t)lenP;
                    }
                    if (!refBad &&
                        outA.pendCount + outB.pendCount != ref.pendCount) {
                        fprintf(stderr,
                                "XGS_ENC_PARANOID: pend count A=%d B=%d "
                                "ref=%d\n",
                                outA.pendCount, outB.pendCount, ref.pendCount);
                        refBad = 1;
                    }
                } else {
                    fprintf(stderr, "XGS_ENC_PARANOID: ref buffer alloc fail\n");
                    refBad = 1; /* 参照缓冲欠配: 按不等处置(验收环境内存足)。 */
                }
                if (refBad) {
                    fprintf(stderr,
                            "XGS_ENC_PARANOID: dual-core != single-core "
                            "claimed=%d split=%d full=%d\n",
                            claimed, splitAt, full ? 1 : 0);
                    abort();
                }
                if (ref.stream) XFree_System(ref.stream);
                if (ref.tileLen) XFree_System(ref.tileLen);
                if (ref.pendIdx) XFree_System(ref.pendIdx);
                if (ref.pendHash) XFree_System(ref.pendHash);
                if (ref.tileCodec) XFree_System(ref.tileCodec);
            }
            /* 延议哈希回填(A 后 B; 序号不相交, 单写免竞争, 语义与就地
             * 更新等价——去重只读上轮值, 轮内无跨 tile 读)。 */
            {
                int k;
                for (k = 0; k < outA.pendCount; ++k) {
                    int ti = outA.pendIdx[k];
                    hashTab[ti] = outA.pendHash[k];
                    hashValid[ti >> 3] |= (uint8_t)(1u << (ti & 7));
                }
                for (k = 0; k < outB.pendCount; ++k) {
                    int ti = outB.pendIdx[k];
                    hashTab[ti] = outB.pendHash[k];
                    hashValid[ti >> 3] |= (uint8_t)(1u << (ti & 7));
                }
            }
        }
        payloadBudget = batchCap - 16u;
        batchLen = (size_t)XGUI_REMOTE_FB_UPDATE_HEADER_BYTES;
        batchTiles = 0;
        pickRleCount = outA.pickRle + outB.pickRle; /* [路5] 半区择码汇总。 */
        {
            size_t offA2 = 0, offB2 = 0;
            for (i = 0; i < claimed; ++i) {
                /* [perf9] 发送序 = order 排列(槽/坐标/键同源同项, 根修
                 * 像素-坐标错配); 无排列(优先级关)即认领序。
                 * [路3] tileLen/stream 按发送序下标 i 取(半区产线以发送序
                 * 写出), src 仅用于坐标换算; len<0 = 去重跳过/编码失败,
                 * 与单核 continue 同型。 */
                int len = (i < splitAt) ? outA.tileLen[i]
                                        : outB.tileLen[i - splitAt];
                /* [路5 并集] 产线逐 tile 择码结果, 记录如实(混批语义)。 */
                int tileCodec = (i < splitAt) ? outA.tileCodec[i]
                                              : outB.tileCodec[i - splitAt];
                const uint8_t* payload;
                int ti, tcw, tch;
                size_t recBytes, w2;
                XGuiRemoteMsgFbTile rec;
                if (len < 0) continue;
                payload = (i < splitAt) ? outA.stream + offA2
                                        : outB.stream + offB2;
                if (i < splitAt) offA2 += (size_t)len;
                else offB2 += (size_t)len;
                {
                    int src = orderValid ? ((const int*)orderBuf)[i] : i;
                    ti = idxBuf[src];
                }
                tcw = xgs_minInt(tw, shadowW - (ti % gridW) * tw);
                tch = xgs_minInt(th, shadowH - (ti / gridW) * th);
                recBytes = 12u + (size_t)len;
                /* 拆帧预算 = min(txBudget, udpSplitCap, payloadBudget):
                 * UDP 激活时单批 ≤ 数据报负载帽(单报整帧, 设计稿 §5)。 */
                if (batchTiles > 0 &&
                    batchLen + recBytes >
                        xgs_minSize(plan.txBudgetBytes,
                                    xgs_minSize(udpSplitCap,
                                                payloadBudget))) {
                    /* 超帧预算: 冲上一批(单个超预算 tile 由此独立成帧)。 */
                    XGuiRemoteMsgFbUpdate hdr;
                    hdr.sequence = fbSequence;
                    hdr.tileCount = (uint16_t)batchTiles;
                    hdr.format = (uint8_t)plan.wireFormat;
                    hdr.flags = XGUI_REMOTE_TILE_FLAG_NONE;
                    if (XGuiRemoteProto_encFbUpdate(batchBuf, batchCap,
                                                    &hdr) != 0)
                        fbSequence++;
                    XMutex_lock(s->m_qMutex);
                    xgs_flushBatchLocked(s, batchBuf, batchLen, epoch, claimUs,
                                         XGuiRemoteUdp_stageProfNowUs(),
                                         passing || full);
                    XMutex_unlock(s->m_qMutex);
                    batchLen = (size_t)XGUI_REMOTE_FB_UPDATE_HEADER_BYTES;
                    batchTiles = 0;
                }
                rec.x = (uint16_t)((ti % gridW) * tw);
                rec.y = (uint16_t)((ti / gridW) * th);
                rec.w = (uint16_t)tcw;
                rec.h = (uint16_t)tch;
                rec.codec = (uint8_t)tileCodec; /* [路5] 逐 tile 如实(全 RAW 批=RLE 胜出 tile 亦可混)。 */
                rec.flags = XGUI_REMOTE_TILE_FLAG_NONE;
                rec.payload = NULL;
                rec.payloadBytes = (uint32_t)len;
                w2 = XGuiRemoteProto_encFbTile(batchBuf + batchLen,
                                               batchCap - batchLen, &rec,
                                               payload);
                if (w2 == 0) continue; /* 容量恒足量, 防御性丢弃。 */
                batchLen += w2;
                ++batchTiles;
            }
        }
        if (tilePickOn && XGS_BENCH_TRACE_ON())
            fprintf(stderr, "XGS_TRACE tile-pick claimed=%d rle=%d raw=%d\n",
                    claimed, pickRleCount, claimed - pickRleCount);
        if (batchTiles > 0) {
            XGuiRemoteMsgFbUpdate hdr;
            hdr.sequence = fbSequence;
            hdr.tileCount = (uint16_t)batchTiles;
            hdr.format = (uint8_t)plan.wireFormat;
            hdr.flags = XGUI_REMOTE_TILE_FLAG_NONE;
            if (XGuiRemoteProto_encFbUpdate(batchBuf, batchCap, &hdr) != 0)
                fbSequence++;
            XMutex_lock(s->m_qMutex);
            xgs_flushBatchLocked(s, batchBuf, batchLen, epoch, claimUs,
                                 XGuiRemoteUdp_stageProfNowUs(),
                                 passing || full);
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
        /* ---- [2026-10-06 首帧全量根修] 丢批自愈: 队列满丢最旧(编码线程
         * flushBatchLocked)或下游 UDP 递交失败都会让「脏位已消费」的 tile
         * 永不补发(静态屏恒黑, 用户真机两路实测)。轮初快照 vs 现值有增量
         * 即重启全量轮(游标归零重扫, 超集补齐); 500ms 冷却防持续过载下的
         * 热旋——过载期丢批→重扫会加剧积压, 冷却让增量交付先行消化。 ---- */
        {
            int dropsNow;
            XMutex_lock(s->m_qMutex);
            dropsNow = s->droppedBatches;
            if (dropsNow != drops0 && now - s->fullReArmMs >= 500) {
                s->fullReArmMs = now;
                if (paceBytes > 0) {
                    s->fullActive = true;
                    s->fullCursor = 0;
                } else {
                    s->allTilesDirty = true; /* 旧行为: 一轮突发重扫。 */
                }
            }
            XMutex_unlock(s->m_qMutex);
            if (dropsNow != drops0 && XGS_BENCH_TRACE_ON())
                fprintf(stderr, "XGS_TRACE drop-rearm t=%lld drops=%d\n",
                        (long long)now, dropsNow - drops0);
        }
        /* ---- [perf9 路4] 自适应降档喂状态(2026-10-05) ----
         * roundMs= 认领起至入队止(扫描+拷出+编码+拆帧全包, 不含门控等待);
         * 队列水位= 高于容量半满或背压标志置位即算高。阶梯只压本会话认领
         * 节拍, 脏位零丢失仅延后; 与能力协商/PROFILE_SET 并集(基准变化即
         * 重置)。老 peer 兼容: 无线上语义变化。 */
        if (XGS_ADAPT_ON()) {
            int64_t roundEndMs = xgs_nowMs();
            int64_t roundMs = (roundEndMs > now) ? (roundEndMs - now) : 0;
            int qHigh;
            size_t qBytesNow;
            size_t qCapNow;
            XMutex_lock(s->m_qMutex);
            qBytesNow = s->qBytes;
            qCapNow = s->profile.encodeQueueBytes;
            qHigh = s->backpressure ||
                    (qCapNow > 0 && qBytesNow * 2u > qCapNow);
            XMutex_unlock(s->m_qMutex);
            {
                int fpsBefore = xgui_remote_adapt_fps(&s->adapt);
                int fpsAfter = xgui_remote_adapt_feed(
                    &s->adapt, plan.maxFps, (int)roundMs,
                    xgs_adaptBudgetMs(), qHigh ? 1 : 0, roundEndMs);
                if (XGS_ADAPT_PROF_ON() && fpsAfter != fpsBefore) {
                    fprintf(stderr,
                            "XGS_ADAPT t=%lld base=%d step=%d fps=%d->%d "
                            "roundMs=%lld qHigh=%d q=%zu/%zu\n",
                            (long long)roundEndMs, plan.maxFps,
                            s->adapt.step, fpsBefore, fpsAfter,
                            (long long)roundMs, qHigh, qBytesNow, qCapNow);
                }
            }
        }
    }

    if (workBuf) XFree_System(workBuf);
    if (idxBuf) XFree_System(idxBuf);
    if (encBuf) XFree_System(encBuf);
    if (encBufB) XFree_System(encBufB);
    if (batchBuf) XFree_System(batchBuf);
    if (hashTab) XFree_System(hashTab);
    if (hashValid) XFree_System(hashValid);
    if (chgBuf) XFree_System(chgBuf);
    if (vpBuf) XFree_System(vpBuf);
    if (outA.stream) XFree_System(outA.stream);
    if (outA.tileLen) XFree_System(outA.tileLen);
    if (outA.pendIdx) XFree_System(outA.pendIdx);
    if (outA.pendHash) XFree_System(outA.pendHash);
    if (outB.stream) XFree_System(outB.stream);
    if (outB.tileLen) XFree_System(outB.tileLen);
    if (outB.pendIdx) XFree_System(outB.pendIdx);
    if (outB.pendHash) XFree_System(outB.pendHash);
}

/* ==================== 握手与帧派发(GUI 线程) ==================== */

/**
 * @brief  发 UDP_OFFER(流送建立/UDP_MODE(1) 重建请求时; 设计稿 §3)。
 * @note   token 为会话级随机凭据(数据报身份唯一依据, 服务端 RECV 不取
 *         源地址——设计稿 §1 缺陷 2); 通道未就绪则回 UDP_RESULT(0)。
 */
static void xgs_sendUdpOffer(XGuiServer* self, XgsSession* s)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    uint8_t rb[4];
    if (!d->udpChannel ||
        !XGuiRemoteUdpChannel_socketReady(d->udpChannel)) {
        XGuiRemoteMsgUdpResult result;
        uint8_t buf[8];
        size_t n;
        XMemset(&result, 0, sizeof(result));
        result.active = 0;
        n = XGuiRemoteProto_encUdpResult(buf, sizeof(buf), &result);
        if (n > 0) (void)xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_UDP_RESULT,
                                              buf, n);
        return;
    }
    do {
        XRandomGenerator_fillSecure(rb, sizeof(rb));
        s->udpToken = XGuiRemoteProto_getU32(rb);
    } while (s->udpToken == 0); /* 0=通道未绑定哨兵值, 规避。 */
    {
        XGuiRemoteMsgUdpOffer offer;
        uint8_t buf[16];
        size_t n;
        XMemset(&offer, 0, sizeof(offer));
        offer.udpPort = XGuiRemoteUdpChannel_localPort(d->udpChannel);
        offer.sessionToken = s->udpToken;
        offer.maxPayloadBytes = (uint16_t)XGUI_REMOTE_UDP_MAX_PAYLOAD;
        n = XGuiRemoteProto_encUdpOffer(buf, sizeof(buf), &offer);
        if (n > 0) (void)xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_UDP_OFFER,
                                              buf, n);
        XPrintf("XGuiWindowDemo: remote-udp offer session=%d port=%u\n",
                s->id, (unsigned)offer.udpPort);
    }
}

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
    /* 首帧根修(2026-10-05 方案③配套): 建缓冲置的全量标志此前会让编码线程
     * 立刻编码一份从未被采集过的影子(全零=黑块), 只能等下一次真实 present
     * 纠正——静态屏(fbdev 空闲不呈现)下即「接入后长时间无 tile/黑块」根因。
     * 处置: ① 记 needFirstCapture, 首次真实采集落地前编码线程不认领
     * (realloc 全量/FB_REQUEST 全量都等真像素); ② 强制宿主整幅重绘一次,
     * present 包装回调即刻采集当前真实画面(不经 present 架构, 只发常规
     * repaint)。无宿主/无 store 的裸服务器(单测回环)不门控不重绘。
     * [perf9 路2 收口 2026-10-05] ② 的「发常规 repaint」是投递式
     * XWidget_update——PAINT 单槽去重(m_paintEventPosted CAS)下, 若已有
     * 小区域 PAINT 排队, 整窗 update 被吞进旧事件的旧区域: 首采集=局部
     * 包围盒, 影子缺区, FB_REQUEST 全量刷新把缺区编成黑 tile 下发
     * (回环离屏实证: 镜像顶部条带永黑+真机「大面黑」同谱)。改为
     * update+repaint 同步整窗重绘: repaintRegion 同步 flush, present
     * 包装回调在本调用栈内即采集整幅, 与排队事件解耦, 确定性完备。 */
    s->needFirstCapture = (d->hosted != NULL && d->store != NULL);
    XMutex_unlock(s->m_qMutex);
    /* [perf9 停摆根修 2026-10-05] 状态序即首采集序: present 包装回调只对
     * STREAMING 会话采集(XGuiServer.c xgs_presentWrapper), 原实现把
     * state=STREAMING 放在同步 repaint 之后——②的「repaint 本调用栈内
     * 即采集」对本会话恒不发生, capturedOnce 落不了地, 首采集门
     * (needFirstCapture)恒闭, 交付被押后到 FB_REQUEST 同步 repaint(真机
     * resource 168-253ms 的隐藏代价); FB_REQUEST 一旦不达/其全量轮因
     * 内存临界被弃, 静态屏(fbdev 空闲)再无 present 即成「首帧怠速交付
     * 停摆」(真机 performance 档 80/475+0.01fps 定谳机理)。改为:
     * 先入流式态+先发 META(客户端可先行建流), 再同步 repaint——present
     * 包装本调用栈内即采集, capturedOnce 确定性落地, 首帧不再依赖
     * FB_REQUEST。首采集门天然串行: 采集完成前 capturedOnce=0, worker
     * 不认领, 零值影子不会外泄。 */
    s->state = XGUI_REMOTE_STATE_STREAMING;
    s->metaPending = true; /* 即便 store 不可用也发 META(0×0 占位语义)。 */
    (void)xgs_sessionSendFbMeta(self, s);
    if (d->hosted) {
        /* 整窗矩形而非 contentsRect: 装饰(CSD 标题栏)占用的顶部条带在
         * contentsRect 之外——按 contentsRect 标脏会让首采集/全量刷新
         * 永缺标题栏条带(离屏实证: 镜像顶部 ~28px 恒缺)。 */
        XRect whole = XWidget_rect(d->hosted);
        XWidget_updateRect(d->hosted, &whole);
        XWidget_repaint(d->hosted); /* 同步整窗 paint→flush→present→采集。 */
    }
    /* UDP 旁路报价(能力交集含 CAP_UDP 才发——老客户端永不见新消息,
     * 向后兼容红线, 设计稿 §6)。 */
    if (s->caps & (uint32_t)XGUI_REMOTE_CAP_UDP)
        xgs_sendUdpOffer(self, s);
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

/** @brief 编码 AUTH_RESULT 负载(ok/text; 返回 0=编码失败)。 */
static size_t xgs_encAuthResult(int ok, const char* text,
                                uint8_t* buf, size_t cap)
{
    XGuiRemoteMsgAuthResult result;
    size_t tlen;
    XMemset(&result, 0, sizeof(result));
    result.ok = (uint8_t)(ok ? 1 : 0);
    tlen = strlen(text);
    if (tlen > sizeof(result.text)) tlen = sizeof(result.text);
    memcpy(result.text, text, tlen);
    result.textBytes = (uint16_t)tlen;
    return XGuiRemoteProto_encAuthResult(buf, cap, &result);
}

/** @brief 认证失败处置: AUTH_RESULT(0) + BYE(AUTH_FAILED) + sessionError + 断链。 */
static void xgs_authFailed(XGuiServer* self, XgsSession* s)
{
    uint8_t buf[XGUI_REMOTE_MAX_MSG_BYTES + 16];
    size_t n = xgs_encAuthResult(0, "authentication failed",
                                 buf, sizeof(buf));
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
    s->caps = xgs_serverCaps(d) & hello.capabilities;
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
    if (s->profileId == XGUI_REMOTE_PROFILE_LATENCY &&
        !(s->caps & (uint32_t)XGUI_REMOTE_CAP_LATENCY)) {
        /* 老 peer 逐会话降级(2026-10-05): dec 层档位 id 封闭校验是冻结
         * 契约(XGuiRemoteTest "档位 id 非法 dec 拒绝"), 未宣告 CAP_LATENCY
         * 的 peer 收到 FB_META(id=3) 必按协议错误断链——同 RGB565/ZLIB
         * 夹取先例, 此处换 resource 预设, FB_META 宣告 resource, 老客户端
         * 正常建流。 */
        XGuiRemoteProfile_initResource(&s->profile);
        s->profileId = XGUI_REMOTE_PROFILE_RESOURCE;
    }
    XMutex_unlock(s->m_qMutex);

    XMemset(&ack, 0, sizeof(ack));
    ack.protocolVersion = XGUI_REMOTE_PROTOCOL_VERSION; /* 定版=min(双方)。 */
    ack.capabilities = s->caps;
    ack.authMethod = (uint8_t)d->auth.authMethod;
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
    if (d->auth.authMethod == XGUI_REMOTE_AUTH_SHA256_CHALLENGE &&
        d->auth.hasPassword) {
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
        s->authRequired = true;
        s->state = XGUI_REMOTE_STATE_AUTHENTICATING;
    } else {
        xgs_enterStreaming(self, s);
    }
}

/** @brief 处理 AUTH_RESPONSE: 策略内核在 XGuiRemoteAuth(同式重算+常量
 *         时间比对+失败上限); 本函数只做判定分派。 */
static void xgs_handleAuthResponse(XGuiServer* self, XgsSession* s,
                                   const uint8_t* payload, size_t len)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    XGuiRemoteMsgAuthResponse resp;
    XGuiRemoteAuthVerdict verdict;
    if (!XGuiRemoteProto_decAuthResponse(payload, len, &resp)) {
        xgs_protocolError(self, s);
        return;
    }
    verdict = XGuiRemoteAuth_serverVerifyResponse(
        &d->auth, s->nonce, XGUI_REMOTE_AUTH_NONCE_BYTES,
        resp.response, (size_t)resp.responseBytes, &s->authFailures);
    if (verdict == XGUI_REMOTE_AUTH_VERDICT_OK) {
        uint8_t buf[XGUI_REMOTE_MAX_MSG_BYTES + 16];
        size_t n = xgs_encAuthResult(1, "welcome", buf, sizeof(buf));
        if (n == 0 ||
            !xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_AUTH_RESULT, buf, n)) {
            xgs_protocolError(self, s);
            return;
        }
        s->authOk = true;
        xgs_enterStreaming(self, s);
        return;
    }
    if (verdict == XGUI_REMOTE_AUTH_VERDICT_RETRY) {
        /* 未达失败上限: 回 AUTH_RESULT(0) 留在 AUTHENTICATING 可再答。 */
        uint8_t buf[XGUI_REMOTE_MAX_MSG_BYTES + 16];
        size_t n = xgs_encAuthResult(0, "authentication failed, retry",
                                     buf, sizeof(buf));
        if (n > 0) (void)xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_AUTH_RESULT,
                                              buf, n);
        return;
    }
    xgs_authFailed(self, s); /* 达上限: RESULT(0)+BYE(AUTH_FAILED)+断链。 */
}

/**
 * @brief  FB_REQUEST(§3.6; 2026-10-05 方案③④语义定版):
 *           - mode=1 全量刷新: 置全量标志(去重由全量语义绕过)。服务端限频
 *             XGS_FB_REQUEST_MIN_MS 窗口内重复请求合并为一轮——全量交付节拍
 *             本就由 maxFps 认领门控压在 15fps 节拍内, 本限频防的是节拍外
 *             的重复全量请求风暴(客户端故障/重连风暴放大 CPU 与线缆)。
 *           - 视口通告(方案④): (w,h)≠0 时 (x,y,w,h) 为客户端可见视口
 *             (远端画面坐标), 随任一 mode 一并更新; mode=0+视口=纯通告
 *             (无刷新动作, 老服务端对 mode=0 本就零操作, 线上兼容)。
 */
static void xgs_handleFbRequest(XGuiServer* self, XgsSession* s,
                                const uint8_t* payload, size_t len)
{
    XGuiRemoteMsgFbRequest req;
    int64_t now = xgs_nowMs();
    bool fullAccepted = false;
    if (!XGuiRemoteProto_decFbRequest(payload, len, &req)) {
        xgs_protocolError(s->owner, s);
        return;
    }
    XMutex_lock(s->m_qMutex);
    if (req.w != 0 && req.h != 0) {
        s->viewX = (int16_t)req.x;
        s->viewY = (int16_t)req.y;
        s->viewW = (int16_t)req.w;
        s->viewH = (int16_t)req.h;
    }
    if (req.mode == 1 &&
        (s->lastFullReqMs == 0 ||
         now - s->lastFullReqMs >= XGS_FB_REQUEST_MIN_MS)) {
        s->lastFullReqMs = now;
        s->allTilesDirty = true;
        XAtomic_store_bool(&s->hasDamage, true, XAtomic_MemoryOrder_Release);
        fullAccepted = true;
    }
    XMutex_unlock(s->m_qMutex);
    /* [perf9 路2 收口 2026-10-05] 全量刷新的影子完备性: allTilesDirty 只
     * 放行认领, 不保证影子每格都是真像素——FLUSH 限绘下从未(重)呈现过
     * 的区域影子缺区, 全量刷新会把缺区编成黑 tile(attach 首采集同族,
     * 见 xgs_enterStreaming 注)。本函数在 GUI 泵线程: 限频通过后同步
     * 整窗重绘一轮, present 包装回调即刻采集整幅, 缺区自愈; 静态屏
     * (无后续 present)亦然。限频未通过的重复请求零重绘(A 侧风暴防)。 */
    if (fullAccepted && self) {
        XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
        if (d && d->hosted) {
            /* 整窗矩形(含 CSD 标题栏条带), 同 xgs_enterStreaming 注。 */
            XRect whole = XWidget_rect(d->hosted);
            XWidget_updateRect(d->hosted, &whole);
            XWidget_repaint(d->hosted);
        }
    }
    if (XGS_FF_PROF_ON())
        fprintf(stderr,
                "XGS_FF fbreq t=%lld mode=%u rect=%u,%u+%ux%u "
                "fullAccepted=%d\n",
                (long long)now, (unsigned)req.mode, (unsigned)req.x,
                (unsigned)req.y, (unsigned)req.w, (unsigned)req.h,
                fullAccepted ? 1 : 0);
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
    } else if (set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_LATENCY &&
               !(s->caps & (uint32_t)XGUI_REMOTE_CAP_LATENCY)) {
        /* 未宣告 CAP_LATENCY 的 peer 请求 latency: 拒绝并保持当前档
         * (PROFILE_RESULT 回当前档 id, 会话不断; 该 peer 的 dec 层拒
         * id=3, 拒绝路径回显当前档保证其可解析, 2026-10-05)。 */
        result.accepted = 0;
        XMutex_lock(s->m_qMutex);
        result.profileId = (uint8_t)s->profileId;
        XMutex_unlock(s->m_qMutex);
    } else if (set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_PERFORMANCE ||
               set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_RESOURCE ||
               set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_AUTO ||
               set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_LATENCY) {
        XGuiRemoteProfile preset;
        XGuiRemoteProfileId acceptedId = (XGuiRemoteProfileId)set.profileId;
        if (set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_PERFORMANCE)
            XGuiRemoteProfile_initPerformance(&preset);
        else if (set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_RESOURCE)
            XGuiRemoteProfile_initResource(&preset);
        else if (set.profileId == (uint8_t)XGUI_REMOTE_PROFILE_LATENCY)
            XGuiRemoteProfile_initLatency(&preset); /* 低延迟档(2026-10-05)。 */
        else
            XGuiRemoteProfile_initAuto(&preset);
        /* [2026-10-06 OOM 根修] ARGB32 档内存门: 可用内存不足时静默
         * 降级 resource(RESULT 回 resource=accepted, 客户端经 FB_META
         * 跟随; 语义同 CAP_LATENCY 缺失逐会话降级先例)。 */
        if ((acceptedId == XGUI_REMOTE_PROFILE_PERFORMANCE ||
             acceptedId == XGUI_REMOTE_PROFILE_LATENCY) &&
            !xgs_argb32MemOk()) {
            XGuiRemoteProfile_initResource(&preset);
            acceptedId = XGUI_REMOTE_PROFILE_RESOURCE;
            if (XGS_BENCH_TRACE_ON())
                fprintf(stderr, "XGS_TRACE memclamp perf->res\n");
        }
        /* 帧边界生效(§5.3): 挂 pending, 队列/待写缓冲清空后应用。 */
        XMutex_lock(s->m_qMutex);
        s->pendingProfile = preset;
        s->pendingProfileId = acceptedId;
        s->switchPending = true;
        XMutex_unlock(s->m_qMutex);
        result.accepted = 1;
        result.profileId = (uint8_t)acceptedId;
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
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    uint64_t stamp = 0;
    /* [stage] 输入注入戳(srv-in2fb 基准; GUI 线程写, 呈现回调端采样)。 */
    if (type == XGUI_REMOTE_MSG_INPUT_KEY ||
        type == XGUI_REMOTE_MSG_INPUT_POINTER ||
        type == XGUI_REMOTE_MSG_INPUT_WHEEL ||
        type == XGUI_REMOTE_MSG_INPUT_TOUCH ||
        type == XGUI_REMOTE_MSG_INPUT_IME) {
        if (XGuiRemoteUdp_stageProfOn())
            d->stageInputUs = XGuiRemoteUdp_stageProfNowUs();
        /* [perf9 路3] 输入驱动认领门: 注入时刻入会话(worker 锁内读,
         * 豁免窗 T 基准)。与注入是否产生伤害无关——近输入即豁免。 */
        XMutex_lock(s->m_qMutex);
        s->lastInputMs = xgs_nowMs();
        XMutex_unlock(s->m_qMutex);
    }
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
        xgs_handleFbRequest(self, s, payload, len);
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
    case XGUI_REMOTE_MSG_UDP_BIND: {
        /* UDP 旁路绑定(2026-10-04): 回送地址=TCP 对端 IP+自报口(RECV 路径
         * 不取源地址, 设计稿 §1/§3); token=会话凭据。重绑(断流重建)安全:
         * bindSession 复位双端序号空间。 */
        XGuiRemoteMsgUdpBind bind;
        XGuiRemoteMsgUdpResult result;
        uint8_t buf[8];
        size_t n;
        if (s->state != XGUI_REMOTE_STATE_STREAMING) break;
        if (!XGuiRemoteProto_decUdpBind(payload, len, &bind)) break;
        XMemset(&result, 0, sizeof(result));
        if (d->udpChannel &&
            XGuiRemoteUdpChannel_socketReady(d->udpChannel) &&
            s->udpToken == bind.sessionToken && bind.clientUdpPort != 0) {
            /* 会话 token 为唯一权威: 同会话重绑(建链竞态重试/断流重建)
             * 即替换旧绑定(bindSession 复位双端序号空间); 他会话 token
             * 不匹配自然拒绝(单绑定会话纪律, 设计稿 §2)。 */
            const XHostAddress* peer =
                XAbstractSocket_peerAddress((XAbstractSocket*)s->device);
            char ip[XGUI_REMOTE_MAX_NAME_BYTES + 1];
            XString* ipStr = peer ? XHostAddress_toString(peer) : NULL;
            const char* utf8 = ipStr ? XString_toUtf8(ipStr) : NULL;
            snprintf(ip, sizeof(ip), "%s",
                     (utf8 && utf8[0]) ? utf8 : "127.0.0.1");
            if (ipStr) XClassDelete((XClass*)ipStr);
            XGuiRemoteUdpChannel_setSessionUser(d->udpChannel, s);
            XGuiRemoteUdpChannel_bindSession(d->udpChannel, s->udpToken, ip,
                                             bind.clientUdpPort);
            /* [2026-10-04 根修] 通道数据面激活此前只在客户端置位——服务端
             * 通道 active 恒 false, sendFrame 恒失败, 全部帧静默回落 TCP
             * (表象: 客户端永收不到 ch=1 数据报)。BIND 接纳即激活;
             * 三处停用路径(mode=0/静默降级/解绑)对称置回 false。 */
            XGuiRemoteUdpChannel_setActive(d->udpChannel, true);
            s->udp = d->udpChannel;
            s->udpActive = true;
            s->udpSplitCap = XGUI_REMOTE_UDP_MAX_PAYLOAD;
            result.active = 1;
            XPrintf("XGuiWindowDemo: remote-udp session=%d active "
                    "peer=%s:%u\n", s->id, ip, (unsigned)bind.clientUdpPort);
        } else {
            result.active = 0;
            XPrintf("XGuiWindowDemo: remote-udp bind refused session=%d "
                    "(token/port/channel)\n", s->id);
        }
        n = XGuiRemoteProto_encUdpResult(buf, sizeof(buf), &result);
        if (n > 0) (void)xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_UDP_RESULT,
                                              buf, n);
        return;
    }
    case XGUI_REMOTE_MSG_UDP_MODE: {
        /* 运行期模式(2026-10-04): 0=退回 TCP(帧边界即回, 会话不断; 通道
         * 保持绑定——在途输入照收, 心跳照走); 1=建链/重建(重发 OFFER)。 */
        XGuiRemoteMsgUdpMode mode;
        if (s->state != XGUI_REMOTE_STATE_STREAMING) break;
        if (!XGuiRemoteProto_decUdpMode(payload, len, &mode)) break;
        if (mode.mode == 0) {
            if (s->udpActive) {
                s->udpActive = false;
                s->udpSplitCap = (size_t)-1;
                /* [2026-10-06] 回落即全量重挂(UDP 在途小批去向不明,
                 * 丢区永不自愈——见静默降级处同款注)。 */
                s->allTilesDirty = true;
                XAtomic_store_bool(&s->hasDamage, true,
                                   XAtomic_MemoryOrder_Release);
                if (d->udpChannel)
                    XGuiRemoteUdpChannel_setActive(d->udpChannel, false);
                XPrintf("XGuiWindowDemo: remote-udp session=%d deactivated "
                        "(mode=0)\n", s->id);
            }
            {
                XGuiRemoteMsgUdpResult result;
                uint8_t buf[8];
                size_t n;
                XMemset(&result, 0, sizeof(result));
                result.active = 0;
                n = XGuiRemoteProto_encUdpResult(buf, sizeof(buf), &result);
                if (n > 0) (void)xgs_sessionSendFrame(
                        s, XGUI_REMOTE_MSG_UDP_RESULT, buf, n);
            }
        } else {
            xgs_sendUdpOffer(self, s);
        }
        return;
    }
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

/* ==================== UDP 旁路回调挂接(2026-10-04) ==================== */

/**
 * @brief  UDP 通道业务负载回调(通道收泵 → 帧派发, GUI 线程)。
 * @note   user=XgsSession*(bindSession 时登记); 会话关闭必先 unbindSession
 *         (xgs_sessionClose), 故此处无悬垂; INPUT 之外通道不会回调服务端。
 */
static void xgs_udpRecv(void* user, uint8_t channel, uint8_t msgType,
                        uint32_t seq, const uint8_t* payload, size_t len)
{
    XgsSession* s = (XgsSession*)user;
    (void)seq;
    if (!s || s->closed || !s->owner) return;
    if (channel != XGUI_REMOTE_UDP_CHANNEL_INPUT) return;
    if (s->state != XGUI_REMOTE_STATE_STREAMING) return;
    {
        /* [wake] 段③载体戳: 通道读完成时刻(探针关闭/通道解绑=0 跳过)。 */
        XGuiServer* owner = (XGuiServer*)s->owner;
        XGuiServerPrivate* od = (XGuiServerPrivate*)owner->m_d;
        uint64_t arriveUs = XGuiRemoteUdp_wakeProfOn()
                                ? XGuiRemoteUdpChannel_probeArrivalUs(
                                      od ? od->udpChannel : NULL)
                                : 0;
        xgs_dispatchFrame(s->owner, s, (XGuiRemoteMsgType)msgType, payload, len);
        XGuiRemoteUdp_wakeProfSample("srv-udp", arriveUs);
    }
}

/** @brief UDP 通道 readyRead 直连槽: 数据报到达即排空(XNetIoRing 事件驱动
 *         收侧; tick 仅作兜底节拍, 设计稿 §7)。 */
static void xgs_udpReadyRead(XObject* receiver, XVarList* args)
{
    XGuiServer* self = (XGuiServer*)receiver;
    XGuiServerPrivate* d;
    (void)args;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d) || !d->udpChannel) {
        return;
    }
    XGuiRemoteUdpChannel_drain(d->udpChannel, xgs_nowMs());
}

/** @brief UDP 通道创建+绑定(TCP 端口+1 起顺延; 失败静默纯 TCP)。 */
static void xgs_udpEnsureChannel(XGuiServer* self)
{
    XGuiServerPrivate* d = (XGuiServerPrivate*)self->m_d;
    XGuiRemoteUdpChannelConfig cfg;
    uint16_t bound = 0;
    if (d->udpChannel) return;
    if (!d->udpEnabled) return;
    XMemset(&cfg, 0, sizeof(cfg));
    cfg.role = XGUI_REMOTE_UDP_ROLE_SERVER;
    cfg.user = NULL; /* 绑定会话时按会话登记。 */
    cfg.onRecv = xgs_udpRecv;
    cfg.onStateChanged = NULL;
    d->udpChannel = XGuiRemoteUdpChannel_create_ex(&cfg);
    if (!d->udpChannel) return;
    if (!XGuiRemoteUdpChannel_serverBind(
            d->udpChannel,
            (uint16_t)(XGuiServer_serverPort(self) + 1), &bound)) {
        /* 全占用/平台不支持: 静默纯 TCP(能力位不宣告, 老行为不变)。 */
        XGuiRemoteUdpChannel_delete(d->udpChannel);
        d->udpChannel = NULL;
        XPrintf("XGuiWindowDemo: remote-udp bind failed (tcp-only)\n");
        return;
    }
    /* readyRead → 即时排空(XNetIoRing 事件驱动收侧, 设计稿 §7)。 */
    {
        XObject* sockObj =
            XGuiRemoteUdpChannel_socketObject(d->udpChannel);
        if (sockObj) {
            (void)XObject_connect_1(sockObj,
                                    (size_t)XIODevice_readyRead_signal,
                                    (XObject*)self, xgs_udpReadyRead,
                                    XConnectionType_Direct);
        }
    }
    XPrintf("XGuiWindowDemo: remote-udp listening port=%u\n",
            (unsigned)bound);
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
                /* 全量轮批次强制 TCP(见 XgsQueueNode.forceTcp 注)。 */
                s->nextFrameTcp = node->forceTcp;
                /* [stage] srv-q=入队→泵发送始(唤醒/poll 圈粒度);
                 * srv-send=发送调用时长(sendto/写设备, 含 TCP 部分写截尾)。 */
                uint64_t sendT0 = XGuiRemoteUdp_stageProfNowUs();
                bool sent = xgs_sessionSendFrame(
                    s, XGUI_REMOTE_MSG_FB_UPDATE,
                    XGS_NODE_DATA(node), node->len);
                s->nextFrameTcp = false; /* 单帧标志即用即清。 */
                uint64_t sendT1 = sent ? XGuiRemoteUdp_stageProfNowUs() : 0;
                if (node->stageEnqueueUs && sendT0 && sendT1)
                    XGuiRemoteUdp_stageProfSpan("srv-q",
                                                node->stageEnqueueUs, sendT0);
                if (sendT0 && sendT1)
                    XGuiRemoteUdp_stageProfSpan("srv-send", sendT0, sendT1);
                /* [stage] srv-send 字节列=发出帧线载字节(与 cli-dec 收侧对账)。 */
                if (sendT0 && sendT1 && node->len <= 0xFFFFFFFFull)
                    XGuiRemoteUdp_stageProfBytes("srv-send",
                                                 (uint32_t)node->len);
                if (XGS_BENCH_TRACE_ON())
                    fprintf(stderr, "XGS_TRACE send t=%llu bytes=%zu\n",
                            (unsigned long long)(uint64_t)xgs_nowMs(),
                            node->len);
                if (!sent) {
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
    /* 影子按新计划重分配(同尺寸则转换保留内容); 失败时下帧采集重试。
     * [perf9 停摆根修 2026-10-05] 重分配失败(真机 ARGB32 影子 ~1.9MB,
     * 内存临界)时 realloc 内部的 allTilesDirty=true 不执行, 会话自此
     * 无伤害无全量——静态屏永停摆。此处显式补挂全量标志: 成功路径
     * 幂等(realloc 已置), 失败路径自愈(影子就绪的首个采集触发整轮)。 */
    (void)xgs_sessionReallocShadowLocked(s, s->shadowW, s->shadowH, true);
    s->allTilesDirty = true;
    XMutex_unlock(s->m_qMutex);
}

/** @brief 会话在链判定(xgs_pumpSession 派发后存活性探针)。
 *  @details xgs_sessionClose 对会话「先摘链后 XFree_System」(即时释放),
 *           故派发帧后再读 s->closed 属读已释放块(ASAN 实证 heap-use-
 *           after-free; 常规构建靠释放残值碰巧为 1 才未爆)。以在链性
 *           代替: 摘链后指针仅做值比较, 不解引用, 语义等价且无 UB。 */
static bool xgs_sessionLinked(const XGuiServer* self, const XgsSession* s)
{
    const XGuiServerPrivate* d = (const XGuiServerPrivate*)self->m_d;
    const XgsSession* it;
    if (!d) return false;
    for (it = d->sessionHead; it; it = it->next)
        if (it == s) return true;
    return false;
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
            /* [wake] 段③载体戳: 本会话 TCP 设备最近一次读完成(探针关闭/
             * 借用设备无环登记=0 跳过; 多段凑帧取末段到达——段间组装属
             * 传输分片, 不计入事件循环响应)。 */
            uint64_t arriveUs = XGuiRemoteUdp_wakeProfOn()
                                    ? XAbstractNetIoRing_profLastRecvUs(
                                          (const void*)s->device)
                                    : 0;
            r = XGuiRemoteFrameReader_feed(&s->reader, p, left);
            while (r > 0) {
                xgs_dispatchFrame(self, s, s->reader.type, s->reader.payload,
                                  s->reader.payloadLen);
                XGuiRemoteUdp_wakeProfSample("srv-tcp", arriveUs);
                /* 派发内可能已断链(xgs_sessionClose 先摘链后释放): 在链
                * 性判定代替读已释放块的 closed 标志(ASAN UAF 根修)。 */
                if (!xgs_sessionLinked(self, s)) return;
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

    /* ---- UDP 旁路静默降级(2026-10-04, 设计稿 §4.3): 激活会话 3s 无合法
     *      数据报 → 帧回落 TCP 并通告 UDP_RESULT(0); 会话不断; 客户端
     *      经 UDP_MODE(1) 5s 冷却自动重建。 ---- */
    if (s->udpActive && d->udpChannel) {
        int64_t silent = XGuiRemoteUdpChannel_msSinceRecv(d->udpChannel,
                                                          xgs_nowMs());
        if (silent != INT64_MAX && silent > (int64_t)XGS_UDP_SILENCE_MS) {
            s->udpActive = false;
            s->udpSplitCap = (size_t)-1;
            /* [2026-10-06 首帧全量根修] 回落即全量重挂: UDP 静默期发出的
             * 数据报(小批 tile 最易丢)去向不明, 脏位/哈希已消费=丢区永不
             * 补齐(真机换档后顶部行恒黑实证)。回落 TCP 是可靠重传的起点,
             * 全量轮补发一次(分轮限速+forceTcp 批), 代价可承。 */
            s->allTilesDirty = true;
            XAtomic_store_bool(&s->hasDamage, true,
                               XAtomic_MemoryOrder_Release);
            XGuiRemoteUdpChannel_setActive(d->udpChannel, false);
            {
                XGuiRemoteMsgUdpResult result;
                uint8_t buf[8];
                size_t n;
                XMemset(&result, 0, sizeof(result));
                result.active = 0;
                n = XGuiRemoteProto_encUdpResult(buf, sizeof(buf), &result);
                if (n > 0) {
                    (void)xgs_sessionSendFrame(s, XGUI_REMOTE_MSG_UDP_RESULT,
                                               buf, n);
                }
            }
            XPrintf("XGuiWindowDemo: remote-udp session=%d fallback "
                    "(silence>%dms)\n", s->id, XGS_UDP_SILENCE_MS);
        }
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
/* ---- 泵活性守卫(2026-10-04 合并收官加固) ----
 * 帧泵兜底 XTimer 的在途 posted 事件可在服务器删除后仍投递(对象删除不
 * 清事件队列), 回调 userData 悬垂——auth 矩阵测试 teardown 实证
 * SIGSEGV/ASAN heap-use-after-free(既有时效竞态, UDP 通道事件负载放大
 * 至常态复现)。以存活登记表在回调入口自证, 悬垂回调变无害 no-op。
 * 登记/注销/查询均 GUI 线程(泵回调同线程), 无需加锁。 */
#define XGS_LIVE_SERVER_MAX 16
static XGuiServer* xgs_liveServers[XGS_LIVE_SERVER_MAX];
static int xgs_liveServerCount = 0;

static void xgs_liveServerAdd(XGuiServer* self)
{
    if (xgs_liveServerCount < XGS_LIVE_SERVER_MAX)
        xgs_liveServers[xgs_liveServerCount++] = self;
}

static void xgs_liveServerRemove(const XGuiServer* self)
{
    int i;
    for (i = 0; i < xgs_liveServerCount; ++i) {
        if (xgs_liveServers[i] == self) {
            xgs_liveServers[i] = xgs_liveServers[--xgs_liveServerCount];
            return;
        }
    }
}

static bool xgs_serverIsLive(const XGuiServer* self)
{
    int i;
    /* 表满降级放行(容量 16 远超本模块 1 实例/应用的现实, 理论不可达)。 */
    if (xgs_liveServerCount >= XGS_LIVE_SERVER_MAX) return true;
    for (i = 0; i < xgs_liveServerCount; ++i)
        if (xgs_liveServers[i] == self) return true;
    return false;
}

static bool xgs_pumpCallback(void* userData)
{
    XGuiServer* self = (XGuiServer*)userData;
    XGuiServerPrivate* d;
    XgsSession* s;
    if (!self || !xgs_serverIsLive(self) ||
        !(d = (XGuiServerPrivate*)self->m_d)) return true;
    if (d->pumpWalking) return true; /* 重入守卫(2026-10-05): 见 walk 侧注。 */
    if (XGS_BENCH_TRACE_ON())
        fprintf(stderr, "XGS_TRACE pump t=%llu\n",
                (unsigned long long)(uint64_t)xgs_nowMs());
    /* UDP 旁路节拍(心跳/NACK 重发/收兜底; readyRead 为主路径)。 */
    if (d->udpChannel)
        XGuiRemoteUdpChannel_tick(d->udpChannel, xgs_nowMs());
    XGuiRemoteUdp_wakeProfPoll(); /* [wake] 探针 5s 末窗汇总落盘(流量停止后)。 */
    XGuiRemoteUdp_stageProfPoll(); /* [stage] 探针 5s 末窗汇总落盘(同纪律)。 */
    /* 重入守卫(2026-10-05 快速重连 SIGSEGV 根修): sessionClose 中的设备
     * XClassDelete 会经 XAbstractSocket_deinit 重入事件循环
     * (XAbstractSocket.c:218 processEvents), 嵌套再入本泵回调时外层链
     * 上还有半拆除会话/正在释放的会话, 嵌套遍历即踩悬垂链(SIGSEGV 实证,
     * 基线快照同炸)。泵遍历期间嵌套回调一律 no-op——外层遍历自持 next
     * 指针继续收尾, 语义不变。 */
    d->pumpWalking = true;
    s = d->sessionHead;
    while (s) {
        XgsSession* next = s->next;
        if (!s->closed) xgs_pumpSession(self, s);
        s = next;
    }
    d->pumpWalking = false;
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
    /* [perf9 路3] 双核并行编码 helper(与编码线程同生命周期; 创建/启动
     * 失败仅恒单核, 会话不受影响)。 */
    if (s->encMutex && s->encCond) {
        XgsSession* hp = s;
        s->encHelperArgs = XVarList_Create(XVar(void*, hp));
        if (s->encHelperArgs) {
            s->encHelper = XThread_create_func(xgs_encHelperMain,
                                               s->encHelperArgs);
            if (s->encHelper && !XThread_start(s->encHelper)) {
                XClassDelete((XClass*)s->encHelper);
                s->encHelper = NULL;
            }
        }
        if (!s->encHelper) {
            if (s->encHelperArgs) {
                XVarList_delete(s->encHelperArgs);
                s->encHelperArgs = NULL;
            }
        }
    }
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
    /* [perf9 路3] 双核并行编码 helper 同步件(失败仅降级单核, 会话存活)。 */
    s->encMutex = XMutex_create(XLock_NonRecursive);
    s->encCond = XWaitCondition_create();
    s->encState = 0;
    s->encJob = NULL;
    s->udpSplitCap = (size_t)-1; /* UDP 未激活: 拆帧帽不限(TCP 纪律)。 */
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
    /* 先摘链再拆除(2026-10-05 快速重连 SIGSEGV 根修之二): 下方设备
     * XClassDelete 会重入事件循环嵌套泵遍历——半拆除会话滞留链上即被
     * 嵌套遍历/再关闭。摘链先行(GUI 线程单写, 遍历同线程且嵌套被
     * pumpWalking 守卫), 此后任何遍历都到不了本会话。 */
    pp = &d->sessionHead;
    while (*pp && *pp != s) pp = &(*pp)->next;
    if (*pp) *pp = s->next;
    d->sessionCount--;
    /* UDP 旁路解绑先行(收回调以 s 为上下文, 悬垂防护, 2026-10-04)。 */
    if (s->udp) {
        XGuiRemoteUdpChannel_setActive(s->udp, false);
        XGuiRemoteUdpChannel_unbindSession(s->udp);
        s->udp = NULL;
        s->udpActive = false;
    }
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
    /* [perf9 路3] 停双核 helper: 主 worker 已停(不再派发), 置停机态后
     * 唤醒 join。helper 若正处作业中也会先行退出循环(作业数据 join 前
     * 有效, 无悬挂)。 */
    if (s->encHelper) {
        if (s->encMutex) {
            XMutex_lock(s->encMutex);
            s->encState = 3;
            XWaitCondition_wakeAll(s->encCond);
            XMutex_unlock(s->encMutex);
        }
        (void)XThread_wait(s->encHelper, XGS_WORKER_JOIN_MS);
        XClassDelete((XClass*)s->encHelper);
        s->encHelper = NULL;
    }
    if (s->encHelperArgs) {
        XVarList_delete(s->encHelperArgs);
        s->encHelperArgs = NULL;
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
    if (s->tileChg) XFree_System(s->tileChg);
    s->tileChg = NULL;
    /* [perf9 路3] 双核 helper 同步件收尾(helper 已停, 无竞争)。 */
    if (s->encMutex) {
        XMutex_delete(s->encMutex);
        s->encMutex = NULL;
    }
    if (s->encCond) {
        XWaitCondition_delete(s->encCond);
        s->encCond = NULL;
    }
    XMutex_delete(s->m_qMutex);
    s->m_qMutex = NULL;
    /* (摘链已前置到函数头, 2026-10-05 重入守卫配套。) */
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
    /* 先摘活性登记: 在途泵事件(timer/poll)此后一律视悬垂 no-op。 */
    xgs_liveServerRemove(self);
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
        /* UDP 旁路通道随对象收尾(会话链已清空, 绑定已解)。 */
        if (d->udpChannel) {
            XGuiRemoteUdpChannel_delete(d->udpChannel);
            d->udpChannel = NULL;
        }
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
    XGuiRemoteAuth_storeInit(&d->auth); /* 默认无口令=匿名可连(向后兼容)。 */
    XGuiRemoteProfile_initPerformance(&d->profile);
    d->profileId = XGUI_REMOTE_PROFILE_PERFORMANCE;
    d->tlsOverride = -1; /* 未显式设置: listen 时回退环境变量(原口径)。 */
    d->udpEnabled = true; /* UDP 旁路默认开(双端能力协商门控, 老端不受影响)。 */
    xgs_liveServerAdd(self); /* 泵活性守卫登记(见 xgs_pumpCallback 处注)。 */
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
    /* [2026-10-06 弹层合成] 全局 present 通知: 弹层/对话框存储的 flush
       也驱动会话影子合成(单槽, 与每存储回调互不影响)。 */
    XPlatformBackingStore_setGlobalPresentCallback(xgs_anyPresentWrapper,
                                                   self);
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
    if (d->auth.authMethod == XGUI_REMOTE_AUTH_SHA256_CHALLENGE &&
        !d->auth.hasPassword)
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
        /* [2026-10-04 认证路修复] addr 是栈对象且带 XClass 头, 必须先
         * init 定版虚表再 setAddress; 否则 XTcpServer_listen 内
         * XClassCopy 走未初始化虚表指针(实锅: 栈地址被当函数跳转)。 */
        XHostAddress_init(&addr);
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
        /* UDP 旁路通道(TCP 端口+1 起顺延试绑; 失败静默纯 TCP)。 */
        xgs_udpEnsureChannel(self);
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

/* ==================== 认证(§3.8/§6.8; 实现主体 XGuiRemoteAuth.c) ====== */
/* [2026-10-04] 遗留 API 语义不变(方法+口令双旋钮), 主体收编为存储委托;
 * 访问口价新语义(单旋钮/运行期/掩码查询/失败上限/会话认证形态)见下方
 * 加法式扩展节与 XGuiServer.h 对应节。 */

void XGuiServer_setAuthMethod(XGuiServer* self, XGuiRemoteAuthMethod method)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    if (method != XGUI_REMOTE_AUTH_NONE &&
        method != XGUI_REMOTE_AUTH_SHA256_CHALLENGE)
        return;
    d->auth.authMethod = method;
}

XGuiRemoteAuthMethod XGuiServer_authMethod(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d))
        return XGUI_REMOTE_AUTH_NONE;
    return d->auth.authMethod;
}

bool XGuiServer_setPassword(XGuiServer* self, const char* passwordUtf8)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    return XGuiRemoteAuth_storeSetPassword(&d->auth, passwordUtf8);
}

void XGuiServer_clearPassword(XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    XGuiRemoteAuth_storeClearPassword(&d->auth);
}

bool XGuiServer_hasPassword(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    return XGuiRemoteAuth_storeHasPassword(&d->auth);
}

/* ==================== 访问口令(加法式运行期扩展; 用户裁定语义) ======== */

bool XGuiServer_setAccessPassword(XGuiServer* self, const char* passwordUtf8)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    return XGuiRemoteAuth_serverSetAccessPassword(&d->auth, passwordUtf8);
}

bool XGuiServer_accessPassword(const XGuiServer* self, char* maskedOutUtf8,
                               size_t cap)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) {
        if (maskedOutUtf8 && cap > 0) maskedOutUtf8[0] = '\0';
        return false;
    }
    return XGuiRemoteAuth_serverAccessPassword(&d->auth, maskedOutUtf8, cap);
}

void XGuiServer_clearAccessPassword(XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    XGuiRemoteAuth_serverClearAccessPassword(&d->auth);
}

void XGuiServer_setAuthFailureLimit(XGuiServer* self, int maxFailures)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    XGuiRemoteAuth_serverSetFailureLimit(&d->auth, maxFailures);
}

int XGuiServer_authFailureLimit(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return 1;
    return XGuiRemoteAuth_serverFailureLimit(&d->auth);
}

int XGuiServer_sessionAuthState(const XGuiServer* self, int sessionId)
{
    XGuiServerPrivate* d;
    XgsSession* s;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return -1;
    for (s = d->sessionHead; s; s = s->next) {
        if (s->id == sessionId) {
            return s->authRequired ? (s->authOk ? 1 : 0) : 0;
        }
    }
    return -1;
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
            if (id == XGUI_REMOTE_PROFILE_LATENCY &&
                !(s->caps & (uint32_t)XGUI_REMOTE_CAP_LATENCY)) {
                /* 未宣告 CAP_LATENCY 的存量会话: 逐会话降级 resource
                 * (FB_META 宣告 resource, 会话不断; 依据同
                 * xgs_handleHello 会话夹取, 2026-10-05)。 */
                XGuiRemoteProfile_initResource(&s->pendingProfile);
                s->pendingProfileId = XGUI_REMOTE_PROFILE_RESOURCE;
            } else if ((id == XGUI_REMOTE_PROFILE_PERFORMANCE ||
                        id == XGUI_REMOTE_PROFILE_LATENCY) &&
                       !xgs_argb32MemOk()) {
                /* [2026-10-06 OOM 根修] ARGB32 档内存门: 可用内存不足
                 * 逐会话降级 resource(同 CAP_LATENCY 先例; 服务器页
                 * 组合框切 performance 在真机上曾直接 OOM-kill)。 */
                XGuiRemoteProfile_initResource(&s->pendingProfile);
                s->pendingProfileId = XGUI_REMOTE_PROFILE_RESOURCE;
                if (XGS_BENCH_TRACE_ON())
                    fprintf(stderr, "XGS_TRACE memclamp perf->res\n");
            } else {
                s->pendingProfile = *profile;
                s->pendingProfileId = id;
            }
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
    case XGUI_REMOTE_PROFILE_LATENCY:
        XGuiRemoteProfile_initLatency(&preset); /* 低延迟档(2026-10-05)。 */
        break;
    default:
        return; /* CUSTOM 无预设形态, 走 setProfile; 未知 id 静默保持
                 * 当前档(向后兼容确定性退化, 设计稿 §4)。 */
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

/* ==================== UDP 旁路通道(2026-10-04 加法式扩展) ==================== */

void XGuiServer_setUdpEnabled(XGuiServer* self, bool enabled)
{
    XGuiServerPrivate* d;
    XgsSession* s;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return;
    if (d->udpEnabled == enabled) return;
    d->udpEnabled = enabled;
    if (enabled) {
        /* 开: 按当前 TCP 端口顺延试绑; 监听中改动即时生效。 */
        if (XGuiServer_isListening(self)) xgs_udpEnsureChannel(self);
    } else {
        /* 关: 全部激活会话帧边界回落 TCP(UDP_RESULT(0) 通告), 通道拆除。 */
        s = d->sessionHead;
        while (s) {
            if (!s->closed && s->udp) {
                /* 会话与通道解绑先行(通道随即 delete, 悬垂防护);
                 * 帧边界回落 TCP 并通告 UDP_RESULT(0)。 */
                XGuiRemoteUdpChannel_setActive(d->udpChannel, false);
                XGuiRemoteUdpChannel_unbindSession(d->udpChannel);
                s->udp = NULL;
                s->udpActive = false;
                s->udpSplitCap = (size_t)-1;
                /* [2026-10-06] 回落即全量重挂(同静默降级处注)。 */
                s->allTilesDirty = true;
                XAtomic_store_bool(&s->hasDamage, true,
                                   XAtomic_MemoryOrder_Release);
                {
                    XGuiRemoteMsgUdpResult result;
                    uint8_t buf[8];
                    size_t n;
                    XMemset(&result, 0, sizeof(result));
                    result.active = 0;
                    n = XGuiRemoteProto_encUdpResult(buf, sizeof(buf),
                                                     &result);
                    if (n > 0) {
                        (void)xgs_sessionSendFrame(
                                s, XGUI_REMOTE_MSG_UDP_RESULT, buf, n);
                    }
                }
            }
            s = s->next;
        }
        if (d->udpChannel) {
            XGuiRemoteUdpChannel_delete(d->udpChannel);
            d->udpChannel = NULL;
        }
        XPrintf("XGuiWindowDemo: remote-udp disabled\n");
    }
}

bool XGuiServer_udpEnabled(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return false;
    return d->udpEnabled;
}

uint16_t XGuiServer_udpPort(const XGuiServer* self)
{
    XGuiServerPrivate* d;
    if (!self || !(d = (XGuiServerPrivate*)self->m_d)) return 0;
    return d->udpChannel ? XGuiRemoteUdpChannel_localPort(d->udpChannel) : 0;
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
