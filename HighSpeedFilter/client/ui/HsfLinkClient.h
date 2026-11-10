
/**
 * @file       HsfLinkClient.h
 * @brief      客户端 XLink 连接封装：登录/遥测接收/控制下发（进程级单例）。
 * @details    包一个 XLinkClient：内建挑战应答登录（setCredential）、
 *             自动重连、BSON 遥测接收（变频器经 XVfdCodec_statusFromBson
 *             解码进 XVfdStatus 快照；模拟量逐通道解码）、控制命令
 *             Request/Response 闭环。生命周期独立于页面，与 HsfVfdProvider
 *             同款「回调注册」接缝：登录窗与远端 provider 各自注册回调，
 *             互不持有指针。
 *
 *             [遥测接收] Publish 线上载荷 = [u32 topicId][BSON 文档]，
 *             本模块剥前缀后按主题分派（XLinkProto.h 线格式）。
 *             [角色] 客户端本地不做权限判断（服务端强制），admin 发控制
 *             会得到 ok=false 的「权限不足」应答，经 commandResult 回调
 *             上抛给界面提示。
 */
#ifndef HSFLINKCLIENT_H
#define HSFLINKCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "HsfLinkHistory.h"
#include "HsfLinkConfig.h"
#include "XIndustry_config.h"
#include "XhsfLinkProto.h"   /* HSF_VALVE_COUNT / HSF_DIGITAL_CHANNELS 等常量。 */
#include <stdbool.h>
#include <stdint.h>

#if XVFD_ON
#include "XVfdDevice.h"
#include "XVfdCodec.h"
#endif

/** @brief 客户端侧模拟量通道上限（与服务端 HSF_AI_CHANNELS 对齐）。 */
#define HSF_LINK_AI_CH_MAX   8

/* ==================== 遥测自愈补拍（快照命令兜底通道） ==================== */

/**
 * @brief 同一类遥测两次**补拍请求**之间的最小间隔（毫秒）。
 * @details 快照命令是周期推送的兜底通道：服务端节拍停摆、推送失步队列满被拒、
 *          订阅登记丢失时，客户端靠「请求-应答」自己把数据要回来。但兜底不能
 *          变成轮询风暴——取 1000ms：比任何推送周期（100~500ms）都慢一个量级，
 *          请求量可忽略；又比陈旧阈值（3.5s）快，应答能在判负之前回来。
 * @note  与 HSF_LINK_TEMPHUM_RESYNC_MS 同值同理由（温湿度是最先做自愈的
 *        通道，两处口径必须一致，否则同一拍里两类数据的恢复节奏打架）。
 */
#define HSF_LINK_SNAPSHOT_RESYNC_MS 1000u

/**
 * @brief 补拍的**预取**阈值（毫秒）：剩余新鲜度不足一个补拍周期就要一拍。
 * @details 值 = 各遥测主题共同的陈旧阈值 3500ms − 补拍间隔 1000ms。等到真的
 *          过期（界面已落成「--」）才补会让界面先闪一次无数据再写回真值
 *          （温湿度实测每 3.5s 闪一次，见 HSF_LINK_TEMPHUM_PREFETCH_MS）。
 */
#define HSF_LINK_SNAPSHOT_PREFETCH_MS 2500u

/**
 * @brief 模拟量数据陈旧阈值（毫秒）：超过即视为断流（服务端推送 100ms/拍）。
 * @note  取 3.5s（35 个推送周期）已足够宽松：既不会把偶发丢帧判成断流，
 *        又能让「推送彻底停摆」在几秒内被界面/自愈识别。
 */
#define HSF_LINK_AI_STALE_MS  3500u

/** @brief 客户端支持的侧数（与服务端 HsfSide 对齐：0=A 1=B）。 */
#define HSF_LINK_SIDE_COUNT  2

/**
 * @brief 温湿度遥测快照（客户端口径）。
 * @details 值单位 = 0.1（温度 0.1℃，可负；湿度 0.1%RH），与服务端
 *          XTempHumTransmitterCodec 的定点口径一致，界面按 /10 显示即可。
 *          质量位由服务端按 Hub 数据源新鲜度判定，不自算。
 *          [新鲜度] m_lastMs 是客户端收到该侧推送的本地时刻；界面应经
 *          HsfLinkClient_tempHumFresh 判定而非直读 m_received——断线后
 *          推送停止，m_received 仍为 true，只靠它会把旧值当成实时数据
 *          一直显示（2026-11-04 实锅）。
 */
typedef struct HsfLinkTempHum {
    int32_t m_tempC10;      /**< 温度（0.1℃；无效时 0）。 */
    int32_t m_humPct10;     /**< 湿度（0.1%RH；无效时 0）。 */
    bool m_tempValid;       /**< 温度质量（服务端口径）。 */
    bool m_humValid;        /**< 湿度质量（服务端口径）。 */
    bool m_received;        /**< 本侧收到过至少一拍推送（历史事实，不复位）。 */
    uint64_t m_lastMs;      /**< 本侧最近一次收到推送的本地时刻（单调毫秒）。 */
} HsfLinkTempHum;

/** @brief 温湿度数据陈旧阈值（毫秒）：超过即视为断流，界面显示无数据。 */
#define HSF_LINK_TEMPHUM_STALE_MS  3500u

/**
 * @brief 温湿度补拍（向服务端要一拍快照）的最小间隔（毫秒）。
 * @details 自愈用的兜底通道不能变成轮询风暴：两次补拍至少隔这么久。
 *          取 1000ms：既能在陈旧阈值（3.5s）内恢复，又不产生可观的请求量。
 */
#define HSF_LINK_TEMPHUM_RESYNC_MS 1000u

/**
 * @brief 温湿度补拍的**预取**阈值（毫秒）：剩余新鲜度不足一个补拍周期就补。
 * @details 若等到数据真的过期（HSF_LINK_TEMPHUM_STALE_MS）才补，界面会先
 *          落成「--」再等应答回来写回数值——推送停摆期间每 3.5s 闪一次
 *          「--℃ / --%」（2026-11-09 实测：模拟推送停摆 25s，标题栏在
 *          「--」与真值之间闪了 7 次）。预取阈值取「过期阈值 − 补拍节流」，
 *          保证应答到达之前新鲜度判定不会翻假，界面不再闪。
 * @note  必须满足 HSF_LINK_TEMPHUM_STALE_MS > HSF_LINK_TEMPHUM_RESYNC_MS
 *        （当前 3500 > 1000）。
 */
#define HSF_LINK_TEMPHUM_PREFETCH_MS \
    (HSF_LINK_TEMPHUM_STALE_MS - HSF_LINK_TEMPHUM_RESYNC_MS)

/**
 * @brief 电磁流量计遥测快照（客户端口径）。
 * @details 物理量为 double（服务端按 IEEE754 解出的原值直传）：瞬时流量按
 *          m_flowUnit 给出的单位解释，累积量可到千万级，定点整数装不下。
 *          [新鲜度] m_lastMs 是客户端收到该侧推送的本地时刻；界面应经
 *          HsfLinkClient_flowMeterFresh 判定而非直读 m_received——断线后
 *          推送停止，m_received 仍为 true。
 */
typedef struct HsfLinkFlowMeter {
    double   m_flow;        /**< 瞬时流量（单位由 m_flowUnit 给出）。 */
    double   m_speed;       /**< 瞬时流速（m/s）。 */
    double   m_forwardTotal;/**< 正向累积量（单位由 m_totalUnit 给出）。 */
    double   m_reverseTotal;/**< 反向累积量。 */
    int      m_flowUnit;    /**< 瞬时流量单位枚举（XFlowMeterFlowUnit）。 */
    int      m_totalUnit;   /**< 累积总量单位枚举（XFlowMeterTotalUnit）。 */
    int      m_alarms;      /**< 报警位（XFlowMeterAlarm 位掩码）。 */
    bool     m_valid;       /**< 服务端数据质量。 */
    bool     m_received;    /**< 本侧收到过至少一拍推送（历史事实，不复位）。 */
    uint64_t m_lastMs;      /**< 本侧最近一次收到推送的本地时刻（单调毫秒）。 */
} HsfLinkFlowMeter;

/* ==================== 浊度仪（两台合成一帧） ==================== */

/**
 * @brief 单台在线浊度仪快照。
 * @details NTU 值口径：**线上是 NTU×100 的定点整数**（服务端协议层口径，
 *          与 Python 侧 proto.KEY_TURB_NTU 同值），客户端显示时 ÷100。
 */
typedef struct HsfLinkTurbidity {
    char   m_name[32];    /**< 仪器名（服务端给；客户端不猜原水/出水）。 */
    int    m_ntu100;      /**< 浊度值 ×100（定点整数）。 */
    int    m_raw;         /**< 32 位原始寄存器位模式（排查真相用）。 */
    bool   m_online;      /**< 该台在线（已读到且未陈旧）。 */
    int    m_temp100;     /**< 水温 ×100（℃；**m_tempValid=false 时无意义**）。 */
    bool   m_tempValid;   /**< 该台带温度且这一拍解出了（缺键=该机型无温度）。 */
} HsfLinkTurbidity;

/**
 * @brief 浊度仪组快照（两台合成一帧；台数由帧里的 count 决定）。
 */
typedef struct HsfLinkTurbiditySet {
    HsfLinkTurbidity m_items[HSF_TURBIDITY_COUNT]; /**< 各台。 */
    int      m_count;      /**< 台数。 */
    bool     m_received;   /**< 收到过至少一拍。 */
    uint64_t m_lastMs;     /**< 最近一次收到推送的本地时刻（单调毫秒）。 */
} HsfLinkTurbiditySet;

/** @brief 浊度仪数据陈旧阈值（毫秒）。 */
#define HSF_LINK_TURBIDITY_STALE_MS  3500u

/* ==================== 进水泵（只有输出、无反馈） ==================== */

/**
 * @brief 单台泵快照。
 * @details [泵没有反馈] 现场只有接触器输出，写了就是命令 ⇒ m_on 是**命令
 *          状态**（我方最后写的值，经 DO 回显校准），不代表泵确实在转。
 */
typedef struct HsfLinkPump {
    char   m_name[32];    /**< 泵名（服务端给）。 */
    bool   m_on;          /**< 命令状态（输出已开）。 */
    bool   m_ready;       /**< 输出是否可控（点位已配且模块在线）。 */
    bool   m_valid;       /**< 数据有效。 */
    char   m_text[64];    /**< 说明（未配置点位/模块离线…）。 */
} HsfLinkPump;

/**
 * @brief 泵组快照（当前 1 台；结构上留多台位置）。
 */
typedef struct HsfLinkPumps {
    HsfLinkPump m_items[HSF_PUMP_COUNT]; /**< 各台。 */
    int      m_count;      /**< 台数。 */
    bool     m_received;   /**< 收到过至少一拍。 */
    uint64_t m_lastMs;     /**< 最近一次收到推送的本地时刻。 */
} HsfLinkPumps;

/** @brief 泵数据陈旧阈值（毫秒）。 */
#define HSF_LINK_PUMP_STALE_MS  3500u

/** @brief 流量计数据陈旧阈值（毫秒）：超过即视为断流，界面显示无数据。 */
#define HSF_LINK_FLOWMETER_STALE_MS  3500u

/**
 * @brief 通知回调（无参；具体事件由注册槽位区分）。
 * @param userData 注册时传入的令牌（原样回传）。
 */
typedef void (*HsfLinkNotifyFn)(void* userData);

/**
 * @brief 提示回调（带一条 UTF-8 文本；状态栏类反馈用）。
 * @param userData 注册令牌（原样回传）。
 * @param textUtf8 提示文本（借用；回调内可即刻使用）。
 * @details [用途] 自动重连期间「认证被服务端瞬时拒绝（账户可能已在线）」
 *          这类**不弹登录窗、但要让人看见**的状态。与 fail 回调的区别：
 *          fail 是登录窗语义（首登失败/永久失败），notice 是主界面状态栏
 *          语义（正在重试，无需人工干预）。2026-11-09 根修。
 */
typedef void (*HsfLinkNoticeFn)(void* userData, const char* textUtf8);

/**
 * @brief 控制应答回调。
 * @param userData 注册令牌；op 动作（XVfdOperation）；ok 受理成功；
 * @param errUtf8 失败原因（UTF-8，借用；成功时为空串）。
 */
typedef void (*HsfLinkCommandFn)(void* userData, int op, bool ok, const char* errUtf8);

/**
 * @brief 打开连接并发起登录（幂等：已有连接先关闭重开）。
 * @param host 服务器地址（UTF-8；"127.0.0.1"）。
 * @param port 服务器端口（0 取 HSF_XLINK_DEFAULT_PORT）。
 * @param user 用户名（UTF-8）。
 * @param password 密码（UTF-8；仅本次连接使用，不落盘）。
 * @return 连接已发起返回 true（结果异步：ready/fail 回调）；参数非法
 *         或客户端创建失败返回 false。
 * @note  自动重连开启：断线后按 2s 周期重连，重新就绪会再次触发 ready。
 */
bool HsfLinkClient_open(const char* host, uint16_t port,
                        const char* user, const char* password);

/**
 * @brief 关闭连接并释放实例（幂等）。
 */
void HsfLinkClient_close(void);

/**
 * @brief 连接是否已打开（含连接中；页面据此选择远端/本地 provider）。
 */
bool HsfLinkClient_isOpen(void);

/**
 * @brief 连接是否就绪（已连接且认证通过，可收发业务消息）。
 */
bool HsfLinkClient_isReady(void);

/**
 * @brief 是否处于「认证被瞬时拒绝、框架正在退避重连」的状态。
 * @return 正在重试返回 true；未连接/已就绪/永久失败返回 false。
 * @details [无头登录用它决定"等还是退] 首登撞上服务端尚未回收的旧会话时，
 *          框架按 XLINK_AUTH_REJECT_RETRY_LIMIT 退避重试（几秒后旧会话被
 *          回收即自动登录成功）。`--login` 无头路径原先收到第一次 authFailed
 *          就退出进程，等于把这段自动恢复砍掉；改为「仍在重试就继续等」。
 *          2026-11-09 根修。
 */
bool HsfLinkClient_isReconnecting(void);

/**
 * @brief 当前登录用户名（借用；未就绪为空串）。
 */
const char* HsfLinkClient_user(void);

/**
 * @brief 当前会话认证角色（XLinkAuthRole；未就绪/匿名返回 0）。
 * @note  仅用于界面侧"能否操作"的**提示**判定；权威判定始终在服务端。
 * @warning 角色值**不单调**（操作者=3 > 管理员=2），禁止写 role>=x 比较；
 *          需要"能不能做"一律用 HsfLinkClient_can(HSF_PERM_*)。
 */
int HsfLinkClient_role(void);

/**
 * @brief 界面侧权限判定（服务端同源缺省表的镜像，用于灰化按钮/入口）。
 * @param perm 权限位（HSF_PERM_*，与 XAccessPerm 同值）。
 * @return 当前会话角色持有该位返回 true；未登录按匿名处理。
 * @note  [镜像而非权威] 本函数只决定"按钮能不能点/入口是否隐藏"，
 *        真正的门在服务端（每帧校验）；两边口径必须同步（改一侧要改
 *        另一侧 + 服务端 XPermPolicy 缺省表）。
 */
bool HsfLinkClient_can(uint32_t perm);

/* ==================== 回调注册（先注册后 open 才不漏首拍） ==================== */

/**
 * @brief 注册就绪态回调（连接+认证成功或断开/重连翻转时触发）。
 * @note  回调内可查 HsfLinkClient_isReady 区分方向；重复注册覆盖旧者。
 */
void HsfLinkClient_setReadyCallback(HsfLinkNotifyFn cb, void* userData);

/**
 * @brief 注册失败回调（永久认证失败或首连不可达；其后连接停止重连，
 *        须重新 open）。
 * @note  瞬时拒绝（服务端策略拒绝：账户已在线/会话数上限）**不走**本回调，
 *        走 setNoticeCallback 并在后台退避重试（2026-11-09 根修）。
 */
void HsfLinkClient_setFailCallback(HsfLinkNotifyFn cb, void* userData);

/**
 * @brief 注册状态栏提示回调（自动重连被瞬时拒绝等「重试中」反馈）。
 * @param cb   提示回调（NULL 摘除）。
 * @param userData 回调令牌。
 * @note  与 fail 回调不同：本回调**不会**让连接收手，仅用于让人看见
 *        「账户可能已在线，正在重试」。可在建主窗时注册一次，生命周期
 *        与主窗一致（close 不清，重连后照常复用）。
 */
void HsfLinkClient_setNoticeCallback(HsfLinkNoticeFn cb, void* userData);

/**
 * @brief 最近一次认证失败原因（XLinkClientAuthFailure；0=无）。
 * @details 登录窗据此把「口令错」与「服务端拒绝（账户已在线）」分开显示
 *          （失败回调本身不带参数，2026-11-09 根修）。
 */
int HsfLinkClient_lastAuthFailure(void);

/**
 * @brief 注册变频器遥测更新回调（每拍推送后触发）。
 */
void HsfLinkClient_setVfdCallback(HsfLinkNotifyFn cb, void* userData);

/**
 * @brief 注册模拟量遥测更新回调（每拍推送后触发）。
 */
void HsfLinkClient_setAiCallback(HsfLinkNotifyFn cb, void* userData);

/**
 * @brief 注册温湿度遥测更新回调（任一册推送后触发）。
 * @note  回调内用 HsfLinkClient_tempHum(side) 取具体侧的最近快照。
 */
void HsfLinkClient_setTempHumCallback(HsfLinkNotifyFn cb, void* userData);

/**
 * @brief 注册电磁流量计遥测更新回调（任一册推送后触发）。
 * @note  回调内用 HsfLinkClient_flowMeter(side) 取具体侧的最近快照。
 */
void HsfLinkClient_setFlowMeterCallback(HsfLinkNotifyFn cb, void* userData);

/**
 * @brief 注册控制应答回调（Request/Response 闭环）。
 */
void HsfLinkClient_setCommandCallback(HsfLinkCommandFn cb, void* userData);

/* ==================== 变频器遥测（只读快照） ==================== */

#if XVFD_ON
/**
 * @brief 取最近变频器遥测快照（借用；无遥测时 m_valid=false）。
 * @warning 返回内部存储，禁释放；内容随下一拍刷新。
 */
const XVfdStatus* HsfLinkClient_vfdStatus(void);
#endif

/**
 * @brief 遥测是否已收到过至少一拍。
 */
bool HsfLinkClient_vfdValid(void);

/* ==================== 模拟量遥测 ==================== */

/**
 * @brief 取最近模拟量遥测（工程值 μA 定点 + 有效掩码）。
 * @param outUa 输出数组（调用方提供，容量 ≥ HSF_LINK_AI_CH_MAX；可为 NULL）。
 * @param outMask 输出有效位掩码（bit i=通道 i 有效；可为 NULL）。
 * @return 实际通道数（0=无遥测）。
 */
int HsfLinkClient_aiValues(int32_t* outUa, uint32_t* outMask);

/**
 * @brief 取模拟量通道名（服务端全量快照下发；未收到返回 NULL）。
 * @param index 通道号（0..HSF_LINK_AI_CH_MAX-1）。
 * @return 通道名借用指针（UTF-8；随下一帧全量刷新失效）；无名字返回 NULL。
 * @note  客户端**不写死**通道名：名字的唯一事实源在服务端
 *        （HsfServerConfig.h 的 HSF_AI_NAMES 宏）。名字变化时服务端会主动
 *        下发新名字，本接口读到的就是最新值。
 */
const char* HsfLinkClient_aiName(int index);

/**
 * @brief 取模拟量通道的**标定工程量**（服务端算，与 Python 侧同键同义）。
 * @param index 通道号。
 * @param outValue1000 输出工程量（×1000 定点整数；如 0.499MPa ⇒ 499）；可 NULL。
 * @param outUnit 输出单位（UTF-8，如 "MPa"；可 NULL）。
 * @param unitCap outUnit 缓冲容量。
 * @param outNote 输出越限说明（UTF-8；空=正常；可 NULL）。
 * @return 该通道有标定工程量返回 true（未标定/越界返回 false）。
 * @note  标定只在服务端做（量程是设备铭牌口径）：C 侧标定表见
 *        HsfServerConfig.h 的 HSF_AI_SCALES，Python 侧见 config.analog.scales。
 */
bool HsfLinkClient_aiEngineering(int index, int32_t* outValue1000,
                                 char* outUnit, int unitCap,
                                 const char** outNote);

/**
 * @brief 取模拟量通道的原始寄存器值（毫伏口径；排查真相用）。
 * @param index 通道号。
 * @return 毫伏值；越界返回 0。
 */
int32_t HsfLinkClient_aiRawMv(int index);

/**
 * @brief 模拟量数据是否**当前新鲜**（收到过且未超时）。
 * @return 新鲜返回 true；从未收到或距今超过 HSF_LINK_AI_STALE_MS 返回 false。
 * @note  与其余遥测同口径：推送停摆后 m_aiValid 仍是 true（只在换连接时
 *        复位），故判断实时性必须走本函数而不是直读有效位。
 */
bool HsfLinkClient_aiFresh(void);

/**
 * @brief 请求一拍模拟量**全量**快照（0x0103；连上时由就绪流程自动发一次）。
 * @return 已入发送队列返回 true；连接未就绪返回 false。
 * @note  全量帧才带通道名与标定工程量（增量推送只带数值）。应答走与推送帧
 *        同一个解析器（hsfLcParseAi）并广播订阅者。
 */
bool HsfLinkClient_aiSnapshotRequest(void);

/**
 * @brief 取数字量通道名（服务端全量快照下发）。
 * @param index 通道号。
 * @param isDo  false=DI、true=DO。
 * @return 通道名借用指针；未收到/越界返回 NULL。
 */
const char* HsfLinkClient_digitalName(int index, bool isDo);

/* ==================== 温湿度遥测 ==================== */

/**
 * @brief 取某侧最近温湿度快照（借用；未收到过该侧推送时 m_received=false）。
 * @param side 侧号（0=A 1=B）；越界返回 NULL。
 * @warning 返回内部存储，禁释放；内容随下一拍刷新。
 */
const HsfLinkTempHum* HsfLinkClient_tempHum(int side);

/**
 * @brief 某侧温湿度数据是否**当前新鲜**（收到过且未超时）。
 * @param side 侧号（0=A 1=B）；越界返回 false。
 * @return 新鲜返回 true；从未收到或距今超过 HSF_LINK_TEMPHUM_STALE_MS
 *         返回 false（界面应显示无数据）。
 * @note  断线后推送停止，m_received 仍是 true——**必须**用本函数判断
 *        实时性，直读 m_received 会把旧值当实时数据显示。
 */
bool HsfLinkClient_tempHumFresh(int side);

/**
 * @brief 请求一拍温湿度快照（载荷带 side；异步回包不重建快照，仅触发回调）。
 * @param side 侧号（0=A 1=B）。
 * @return 已入发送队列返回 true；连接未就绪或侧号非法返回 false。
 * @note  正常数据由服务端周期推送驱动；本接口只用于「立即要一拍」的场合。
 * @note  [2026-11-09] 回包**会**重建快照（走与推送帧同一个解析器）——本条
 *        注释是旧口径；当时的实现没有处理应答，等于这条路是死的，现已补上。
 */
bool HsfLinkClient_tempHumSnapshotRequest(int side);

/**
 * @brief 温湿度自愈：剩余新鲜度不足预取阈值（或收到但无有效值）时补要一拍。
 * @param side 侧号（0=A 1=B）；越界返回 false。
 * @return 本次确实发出了补拍请求返回 true；数据仍够新/未就绪/节流中返回 false。
 * @details [为什么需要] 周期推送是单向通道：服务端节拍停摆、推送失步队列满
 *          被拒、或订阅登记丢失时，客户端收不到任何帧，而**服务端的数据一直
 *          有**（现场：服务端 history 行 temp=26.5 恒有值，客户端标题栏却恒显
 *          「--℃ / --%」）。只靠推送，界面只能停在「无数据」。本接口用**快照
 *          命令**（请求-应答）做一条兜底通道：应答与推送帧同构，解析后刷新同一
 *          份 model，标题栏/首页卡片同拍恢复。
 * @note  内部自带节流（HSF_LINK_TEMPHUM_RESYNC_MS），可被**每拍**调用；
 *        判据是 HSF_LINK_TEMPHUM_PREFETCH_MS 而不是过期阈值——等到过期才补
 *        会让界面先闪一次「--」。
 */
bool HsfLinkClient_tempHumEnsureFresh(int side);

/* ==================== 电磁流量计遥测 ==================== */

/**
 * @brief 取某侧最近电磁流量计快照（借用；未收到过该侧推送时 m_received=false）。
 * @param side 侧号（0=A 1=B）；越界返回 NULL。
 * @warning 返回内部存储，禁释放；内容随下一拍刷新。
 */
const HsfLinkFlowMeter* HsfLinkClient_flowMeter(int side);

/**
 * @brief 某侧流量计数据是否**当前新鲜**（收到过且未超时）。
 * @param side 侧号（0=A 1=B）；越界返回 false。
 * @return 新鲜返回 true；从未收到或距今超过 HSF_LINK_FLOWMETER_STALE_MS
 *         返回 false（界面应显示无数据）。
 * @note  断线后推送停止，m_received 仍是 true——**必须**用本函数判断
 *        实时性，直读 m_received 会把旧值当实时数据显示。
 */
bool HsfLinkClient_flowMeterFresh(int side);

/**
 * @brief 取服务端最近一次报告的流量计所在侧（= 设备实际装在哪条总线上）。
 * @return 侧号（0=A 1=B）；从未收到过流量计数据时返回缺省侧（A）。
 * @note  [侧号是服务端的事实] 客户端**不点名**侧号：请求里不带 side，
 *        服务端按自己的装配位置回答并在帧里带上 side ⇒ 客户端据此知道
 *        数据来自哪条总线，但自己不需要知道现场怎么接线（现场流量计换到
 *        另一条 485 上，客户端零改动）。
 */
int HsfLinkClient_flowMeterSide(void);

/**
 * @brief 取流量计快照（借用；按服务端报告的活动侧取）。
 * @return 快照指针（内部存储，禁释放）；从未收到过数据时 m_received=false。
 * @note  想要指定侧的原始数据用 HsfLinkClient_flowMeter(side)。
 */
const HsfLinkFlowMeter* HsfLinkClient_flowMeterActive(void);

/**
 * @brief 活动侧流量计数据是否**当前新鲜**（收到过且未超时）。
 * @return 新鲜返回 true。
 */
bool HsfLinkClient_flowMeterActiveFresh(void);

/**
 * @brief 请求一拍电磁流量计快照（**载荷不带 side**；异步回包触发回调）。
 * @return 已入发送队列返回 true；连接未就绪返回 false。
 * @note  正常数据由服务端周期推送驱动；本接口只用于「立即要一拍」的场合。
 *        [为什么请求里没有 side] 设备挂在哪条总线是**服务端的装配事实**，
 *        客户端不该知道；服务端按装配位置回答并在应答里带上 side。
 */
bool HsfLinkClient_flowMeterSnapshotRequest(void);

/**
 * @brief 注入一帧流量计遥测 BSON（离线回放 / 单测用）。
 * @param data BSON 文档字节（键名同服务端 hsfAppFillFlowMeter：
 *             side/flow/speed/forwardTotal/reverseTotal/flowUnit/totalUnit/
 *             alarms/valid）。
 * @param size 字节数。
 * @return 解析并写入快照成功返回 true；字节非法/侧号越界返回 false。
 * @note  正常数据由服务端周期推送驱动，本接口不经网络、直接走与推送
 *        完全相同的解析路径（hsfLcParseFlowMeter），故可用于回放抓包
 *        与单元测试。注入后 m_lastMs 置为当前时刻，新鲜度从头计。
 */
bool HsfLinkClient_flowMeterInject(const uint8_t* data, uint32_t size);

/* ---- 浊度仪 / 泵 ---- */

/** @brief 取浊度仪组快照（借用；禁释放）。 */
const HsfLinkTurbiditySet* HsfLinkClient_turbidity(void);
/** @brief 浊度仪数据是否当前新鲜。 */
bool HsfLinkClient_turbidityFresh(void);
/** @brief 请求浊度仪快照（0x0112）。 */
bool HsfLinkClient_turbiditySnapshotRequest(void);
/** @brief 注册浊度仪更新回调。 */
void HsfLinkClient_setTurbidityCallback(HsfLinkNotifyFn cb, void* userData);

/** @brief 取泵组快照（借用；禁释放）。 */
const HsfLinkPumps* HsfLinkClient_pumps(void);
/** @brief 泵数据是否当前新鲜。 */
bool HsfLinkClient_pumpsFresh(void);
/** @brief 请求泵快照（0x0113）。 */
bool HsfLinkClient_pumpSnapshotRequest(void);
/** @brief 控制泵（0x0114；index 从 0 起，on=true 启动）。 */
bool HsfLinkClient_pumpControl(int index, bool on);
/** @brief 注册泵更新回调。 */
void HsfLinkClient_setPumpCallback(HsfLinkNotifyFn cb, void* userData);

/* ==================== 自愈补拍（页面激活 / 数据过期时的兜底取数） ==================== */

/**
 * @brief 电磁流量计自愈：活动侧数据不新鲜（或从未收到）时补要一拍。
 * @return 本次确实发出了补拍请求返回 true；数据够新/未就绪/节流中返回 false。
 * @details 流量计页只在**建页时**要过一次快照（provider 的 requestSnapshot），
 *          之后完全依赖推送；推送停摆时页面就停在最后一帧。本接口按预取阈值
 *          补拍，内部节流 HSF_LINK_SNAPSHOT_RESYNC_MS，可被每拍调用。
 * @note  [不点名侧号] 活动侧由服务端在帧里告知，客户端不猜现场接线。
 */
bool HsfLinkClient_flowMeterEnsureFresh(void);

/** @brief 浊度仪自愈补拍（判据/节流同上；数据够新时立即早退）。 */
bool HsfLinkClient_turbidityEnsureFresh(void);

/** @brief 进水泵自愈补拍（判据/节流同上；数据够新时立即早退）。 */
bool HsfLinkClient_pumpEnsureFresh(void);

/** @brief 变频器遥测自愈补拍（判据/节流同上；数据够新时立即早退）。 */
bool HsfLinkClient_vfdEnsureFresh(void);

/** @brief 模拟量自愈补拍（判据/节流同上；数据够新时立即早退）。 */
bool HsfLinkClient_aiEnsureFresh(void);

/**
 * @brief 按「当前页正在订阅的主题集合」为不新鲜的遥测各补一拍。
 * @param topicMask 主题位掩码（bit i = HsfLinkTopic i；HsfUi 传 m_subTopics）。
 * @details 页面激活时的订阅会让服务端**立即定向补发一拍**，但那条路只覆盖
 *          「订阅帧真的送到」的情形；订阅帧丢失或推送通道整体停摆时，界面
 *          没有任何自我恢复能力。本接口是那条路的兜底：由主窗 1Hz 时钟逐拍
 *          调用，每个主题各自判预取阈值 + 各自节流（HsfLinkClient 内部记账），
 *          数据正常时零请求。
 * @note  温湿度不在本表：它是常驻订阅，补拍入口是
 *        HsfLinkClient_tempHumEnsureFresh（标题栏刷新逐拍调用）。
 */
void HsfLinkClient_ensureFreshTopics(uint32_t topicMask);

/* ==================== 控制下发 ==================== */

#if XVFD_ON
/**
 * @brief 下发变频器控制命令（异步；结果经 commandResult 回调）。
 * @param req 控制请求（借用）。
 * @return 命令已入发送队列返回 true（受理 ≠ 执行成功）；连接未就绪或
 *         编码失败返回 false。
 */
bool HsfLinkClient_vfdControl(const XVfdControlRequest* req);

/**
 * @brief 抑制**下一条**命令应答的错误提示（失败不写错误文本）。
 * @details 用于"启动前预置给定"这一步：服务端有停机禁给定闸（宁可拒绝
 *          一次调速，也不在不确定状态下改运行参数），故这一步在停机态
 *          必然被拒；用户看到"点启动先弹一条失败"只会困惑。对齐 Python
 *          客户端的静默口径（两条状态互相冲掉，只留最终那条启动结果）。
 * @note  只作用于下一条应答（成败都清除）；无远端时为空操作。
 */
void HsfLinkClient_suppressNextCommandError(void);

/**
 * @brief 点动释放（松手即停；op=101，不等 600ms 看门狗）。
 * @details 与 Python 客户端 {"op":101} 同口径；服务端据此立即停机并清
 *          点动手持槽。
 */
bool HsfLinkClient_vfdJogRelease(void);
#endif

/**
 * @brief 请求一拍变频器遥测快照（Response 走 vfd 回调；连接未就绪返回 false）。
 */
bool HsfLinkClient_vfdSnapshotRequest(void);

/**
 * @brief 请求一页历史（0x0118；与 Python 客户端同口径）。
 * @param offset 起始偏移（按 ts 倒序数；0=最新那行）。
 * @param limit 本页行数（服务端夹到 1..50）。
 * @return 已发出请求返回 true（连接未就绪/参数非法返回 false）。
 * @details 结果落在 HsfLinkClient_history() 里（页面拉取）。
 */
bool HsfLinkClient_historyPage(int offset, int limit);

/**
 * @brief 历史分页（带导出意图）：exportMode=true 时服务端验 Download 权限。
 * @param exportMode true=导出/下载（需普通用户+）；false=普通浏览。
 * @return 已发出请求返回 true。
 * @note  即使客户端界面已灰按钮，本参数仍由服务端强制验权（绕过界面
 *        直接发帧同样被拒）——这是"下载权限实际生效"的协议落点。
 */
bool HsfLinkClient_historyPageEx(int offset, int limit, bool exportMode);

/**
 * @brief 历史分页（带导出意图 + 关键字筛选，与 Python 客户端同口径）。
 * @param offset/limit 分页参数。
 * @param exportMode true=导出（服务端验 Download）。
 * @param kw 关键字（utf8；NULL/空=不筛）——**下推服务端**，与 Python
 *           HIST_KEY_KW 同名同义。
 * @param kwCols 要比对的显示列下标（0..14）；NULL/0 项=全部列。
 * @param kwColCount 下标个数。
 * @return 已发出请求返回 true。
 */
bool HsfLinkClient_historyQuery(int offset, int limit, bool exportMode,
                                const char* kw,
                                const uint32_t* kwCols, int kwColCount);

/**
 * @brief 历史分页（带时间窗 + 关键字）：loTs/hiTs 下推服务端 WHERE。
 * @param loTs 窗口起点（epoch 毫秒；<=0=不设下界）。
 * @param hiTs 窗口终点（epoch 毫秒；<=0=不设上界，服务端仍按 beforeTs
 *             与"现在"取交集）。
 * @details 与 Python 客户端同键名同语义（HIST_KEY_LO_TS/HI_TS）；其余
 *          参数同 HsfLinkClient_historyQuery。时间范围是**服务端筛**，
 *          因此 total/分页口径与页内容一致（不是客户端过滤当前页）。
 * @return 已发出请求返回 true。
 */
bool HsfLinkClient_historyQueryWindow(int offset, int limit, bool exportMode,
                                      const char* kw,
                                      const uint32_t* kwCols, int kwColCount,
                                      int64_t loTs, int64_t hiTs);

/**
 * @brief 最近一次历史分页结果（借用指针；未收到过应答时 m_valid=false）。
 */
const HsfHistoryPage* HsfLinkClient_history(void);

/* ==================== 运行日志分页（0x0119；C 端扩展命令） ==================== */

/** @brief 运行日志一行（服务端 logs 表一行；category 口径 conn/ctrl/
 *         alarm/auto/auth，见 XhsfLinkProto.h HSF_CMD_LOG_PAGE 注释）。 */
typedef struct HsfLinkLogRow {
    int64_t m_ts;          /**< 事件时刻（epoch 毫秒，墙钟）。 */
    char m_category[16];   /**< 类别（utf8）。 */
    char m_text[224];      /**< 事件文本（utf8）。 */
} HsfLinkLogRow;

/**
 * @brief 运行日志分页应答（0x0119 解析产物）。
 * @note  [应答身份] 与 HsfHistoryPage 同款局限：HsfLinkClient 未把帧头
 *        m_requestId 交给页面，页面只能按服务端回显的 offset/limit 认领
 *        （本命令只有一个消费者＝日志页，键冲突面比 0x0118 小得多）。
 */
typedef struct HsfLinkLogPage {
    bool m_valid;              /**< 已解析过应答（含否定应答）。 */
    bool m_ok;                 /**< 服务端业务结果（ok=false 时看 m_err）。 */
    char m_err[96];            /**< 失败原因（utf8；仅 m_ok=false 时有意义）。 */
    uint32_t m_seq;            /**< 应答序号（每次成功解析 +1）。 */
    int m_total;               /**< 本窗口命中总行数（分页总数按它算）。 */
    int m_offset;              /**< 本页起始偏移（服务端原样回显）。 */
    int m_limit;               /**< 本页行数上限（服务端原样回显）。 */
    int m_count;               /**< 本页实际行数。 */
    HsfLinkLogRow m_rows[HSF_LOG_PAGE_MAX]; /**< 本页行（协议上限 50）。 */
} HsfLinkLogPage;

/**
 * @brief 运行日志分页（带时间窗，照 HsfLinkClient_historyQueryWindow 契约）。
 * @param offset 起始偏移（按 ts 倒序数；0=最新那条）。
 * @param limit 本页行数（服务端夹到 1..HSF_LOG_PAGE_MAX）。
 * @param beforeTs 窗口锚点（epoch 毫秒；<=0=客户端取"现在"，翻页原样
 *                 回传 ⇒ 整段分页钉在同一批数据上，0x0118 同理）。
 * @param loTs/hiTs 时间窗（epoch 毫秒；<=0=该端不限，服务端取交集）。
 * @param category 类别过滤（utf8；NULL/空=全部）。
 * @param kw 关键字（utf8；NULL/空=不筛；下推服务端）。
 * @return 已发出请求返回 true（连接未就绪返回 false）。
 * @details 结果落在 HsfLinkClient_logPage() 里（页面轮询拉取）。
 */
bool HsfLinkClient_logQueryWindow(int offset, int limit, int64_t beforeTs,
                                  int64_t loTs, int64_t hiTs,
                                  const char* category, const char* kw);

/** @brief 最近一次运行日志分页结果（借用指针；未收到过应答时 m_valid=false）。 */
const HsfLinkLogPage* HsfLinkClient_logPage(void);

/**
 * @brief 设备健康状态中文（与服务端 HsfDeviceHealth 对齐）。
 * @param health 健康枚举值（0 正常 / 1 告警 / 2 数据过期 / 3 离线 / 4 故障）。
 * @return 静态文案指针（借用；不释放）。
 */
const char* HsfLinkClient_healthText(int health);

/**
 * @brief 设备类别中文（与 HsfDeviceKind / Python KIND_TEXT 对齐）。
 * @param kind 类别枚举值。
 * @return 静态文案指针（借用；不释放）。
 */
const char* HsfLinkClient_kindText(int kind);

/**
 * @brief 请求服务端配置快照（0x0116；结果落在 HsfLinkClient_config()）。
 * @return 已发出请求返回 true（未连接返回 false）。
 */
bool HsfLinkClient_configGet(void);

/**
 * @brief 写服务端落盘配置（0x0117 的 storage 节；运行期生效）。
 * @param enabled 落盘开关。
 * @param sampleMs 采样周期（毫秒；服务端夹到 ≥1000）。
 * @return 已发出请求返回 true。
 */
bool HsfLinkClient_configSetStorage(bool enabled, int sampleMs);

/**
 * @brief 写服务端落盘配置（全字段；与 Python RemoteSettingsPage 同键名）。
 * @param enabled 落盘开关。
 * @param path 库文件路径（NULL=不改；空串=程序目录）。
 * @param sampleMs 入库周期（毫秒；服务端夹到 ≥1000）。
 * @param maxMb 容量上限 MB（0=不限；-1=不改）。
 * @param policy 到限策略（"ring"/"stop"/"rotate"；NULL/空=不改）。
 * @param keepFiles 保留分段数（rotate 时有效；-1=不改）。
 * @return 已发出请求返回 true。
 * @note 应答 ok/err 经 HsfLinkClient_configOk/configError 取回（序号
 *       HsfLinkClient_configSeq 递增）；服务端可能夹取数值，以随后 GET 为准。
 */
bool HsfLinkClient_configSetStorageEx(bool enabled, const char* path,
                                      int sampleMs, int maxMb,
                                      const char* policy, int keepFiles);

/**
 * @brief 写采集配置（0x0117 的 collect 节；需管理员）。
 * @param collect 采集开关 + 周期（与 Python 同键）。
 * @return 已发出请求返回 true。
 */
bool HsfLinkClient_configSetCollect(const HsfConfigCollect* collect);

/**
 * @brief 写登录/账户策略（0x0117 的 login 节；需管理员 AccountAdmin）。
 * @param login 匿名/同账并发/会话上限（与 Python 同键）。
 * @return 已发出请求返回 true。
 * @note  服务端立即对**下一次登录**生效并落盘 hsf_login_policy.json；
 *        存量会话不受影响（关并发不会踢人）。
 */
bool HsfLinkClient_configSetLogin(const HsfConfigLogin* login);

/** @brief 最近一次配置写是否成功（写应答到达后更新）。 */
bool HsfLinkClient_configOk(void);
/** @brief 最近一次配置写失败原因（UTF-8；空=无/成功）。 */
const char* HsfLinkClient_configError(void);
/** @brief 配置快照/写应答序号（递增 ⇒ 页面据此回填/提示）。 */
int HsfLinkClient_configSeq(void);
/** @brief 配置快照序号（仅 GET 应答递增；页面按它回填控件）。 */
int HsfLinkClient_configGetSeq(void);

/**
 * @brief 最近一次服务端配置快照（借用指针；未收到过应答时 m_valid=false）。
 */
const HsfConfig* HsfLinkClient_config(void);

/**
 * @brief 当前连接的服务端地址（"host:port"；借用；未连接为空串）。
 */
const char* HsfLinkClient_serverAddress(void);

/**
 * @brief 当前会话角色中文（匿名/普通用户/操作者/管理员；借用静态串）。
 */
const char* HsfLinkClient_roleName(void);

/**
 * @brief 自动运行控制（0x0115；需 root + 已处自动模式）。
 * @param op 操作字：1 启动 / 2 停止 / 3 急停 / 4 复位。
 * @return 已发出请求返回 true。
 */
bool HsfLinkClient_autoControl(int op);

/**
 * @brief 最近一次控制应答的失败原因（借用；成功为空串）。
 */
const char* HsfLinkClient_lastCommandError(void);

/* ==================== 服务端运行模式 ==================== */

/**
 * @brief 查询服务端运行模式（发 RUN_MODE 无 mode 键请求）。
 * @return 请求已入队返回 true；连接未就绪返回 false。
 * @note  结果异步：应答到达后 mode/auto 字段更新，并经 modeCallback 通知。
 *        服务端在自动模式下拒绝全部控制命令（遥测不受影响）。
 */
bool HsfLinkClient_queryRunMode(void);

/**
 * @brief 设置服务端运行模式（需 root 角色）。
 * @param autoMode true=自动（拒绝远程控制）false=手动（接受远程控制）。
 * @return 请求已入队返回 true；连接未就绪返回 false。
 * @note  结果异步；失败原因（权限不足等）经 modeCallback 的 err 上抛。
 */
bool HsfLinkClient_setRunMode(bool autoMode);

/**
 * @brief 读最近一次查询/设置得到的服务端模式。
 * @param outAuto 输出是否自动模式（调用方提供；可为 NULL）。
 * @return 已知模式（收到过应答）返回 true；从未收到返回 false。
 */
bool HsfLinkClient_runMode(int* outAuto);

/**
 * @brief 自动控制器是否真的在跑（与"是否自动模式"是两件事）。
 * @param outRunning 输出：1=运行中、0=已停止、-1=未知（服务端未上报）。
 * @return 是否已知（false ⇒ outRunning 被置为 -1）。
 * @note  服务端 0x0105 应答带 autoRunning 键；缺失时必须保持"未知"，
 *       不得折成 false（那会把"服务端没上报"说成"已停止"）。
 */
bool HsfLinkClient_autoRunning(int* outRunning);

/**
 * @brief 注册运行模式变化回调（应答到达时触发）。
 * @param userData 注册令牌。
 * @note  回调内可用 HsfLinkClient_runMode 取当前值、lastModeError 取失败原因。
 */
void HsfLinkClient_setModeCallback(HsfLinkNotifyFn cb, void* userData);

/**
 * @brief 最近一次模式设置的失败原因（借用；成功为空串）。
 */
const char* HsfLinkClient_lastModeError(void);

/**
 * @brief 最近一次「设置运行模式」失败的原文（UTF-8，借用）。
 * @return 客户端内部定长缓冲的**借用指针**（调用方**不得释放**、不得
 *         长期持有）；无失败原因时为空串 ""（不是 NULL）。
 * @note  与 HsfLinkClient_lastModeError **同源同义**（同一缓冲）：本名是
 *        界面侧的语义化入口，旧名保留给既有调用方。内容在每条 0x0105
 *        应答解析前清空、ok=false 时写入服务端 err 原文，故「成功 ⇒ 空串」；
 *        重连时由 hsfLcResetConnectionState 一并清空。
 */
const char* HsfLinkClient_runModeError(void);

/* ==================== 设备运行状态与错误 ==================== */

/** @brief 客户端侧设备健康条目上限。 */
#define HSF_LINK_DEV_MAX 16

/**
 * @brief 客户端设备健康条目（值语义；名字/文本为定长缓冲）。
 */
typedef struct HsfLinkDeviceHealth {
    char m_name[32];    /**< 设备显示名（UTF-8）。 */
    int  m_kind;        /**< 设备类别（HsfDeviceKind）。 */
    int  m_side;        /**< 所在侧（HsfSide）。 */
    int  m_health;      /**< 健康状态（HsfDeviceHealth）。 */
    int  m_error;       /**< 错误码/故障码；0=无。 */
    int  m_runState;    /**< 运行态（XVfdRunState；非设备类 0）。 */
    bool m_online;      /**< 通讯在线。 */
    char m_text[48];    /**< 错误说明（可空）。 */
    char m_addr[64];    /**< 连接地址文案（可空；服务端下发）。 */
} HsfLinkDeviceHealth;

/**
 * @brief 注册设备健康更新回调（收到健康推送/查询应答时触发）。
 */
void HsfLinkClient_setHealthCallback(HsfLinkNotifyFn cb, void* userData);

/**
 * @brief 读最近一次收到的设备健康快照。
 * @param out 输出数组（调用方提供；可为 NULL 只取数量）。
 * @param capacity out 容量。
 * @param outSeq 输出变化序号（可为 NULL）。
 * @return 实际条目数（0=尚未收到）。
 * @note 服务端事件驱动推送：状态变化才推，客户端首连/首订阅会立即收到一份。
 */
int HsfLinkClient_deviceHealth(HsfLinkDeviceHealth* out, int capacity,
                               int* outSeq);

/**
 * @brief 主动拉取一次设备健康快照（断线重连后对账用）。
 */
bool HsfLinkClient_queryDeviceHealth(void);

/**
 * @brief 汇总健康：是否存在故障/离线设备。
 * @param outFault 输出故障数（可为 NULL）；outOffline 输出离线数（可为 NULL）。
 * @return 有任一故障或离线返回 true。
 */
bool HsfLinkClient_healthHasProblem(int* outFault, int* outOffline);

/**
 * @brief 控制应答序号（每收到一拍 VFD_CONTROL 应答自增 1；从未收到为 0）。
 * @return 当前序号。
 * @note  与 HsfLinkClient_lastCommandError 配套用于增量观测：页面记住
 *        上次看到的序号，序号变化即有一拍新应答——err 非空为被拒（界面
 *        提示），空为成功（提示回落）。轮询口径，无需注册回调。
 */
int HsfLinkClient_commandSeq(void);

/* ==================== 数字量（16DI/16DO） ==================== */

/**
 * @brief 数字量模块快照（客户端口径）。
 * @details 位图口径与服务端一致：bit n = 通道 n。
 *          [名字只由服务端给] 客户端**不写死**点数与通道名：首次连接时
 *          拉一次全量快照（服务端带全部名字），之后周期推送只带状态/数值；
 *          名字变化时服务端会主动下发一帧只含名字的快照，本模块就地更新。
 *          [新鲜度] m_received 是**历史事实**（收到过就恒 true）；判实时性
 *          必须用 HsfLinkClient_digitalFresh——断线后推送停止，m_received
 *          仍为 true，只判它会把最后一拍的旧值当实时数据显示。
 */
typedef struct HsfLinkDigital {
    uint32_t m_di;          /**< DI 位图（1=通）。 */
    uint32_t m_do;          /**< DO 位图（1=通）。 */
    int      m_count;       /**< 点数（服务端下发）。 */
    bool     m_valid;       /**< 服务端数据质量。 */
    bool     m_full;        /**< 最近一帧是否全量（含名字）。 */
    char     m_diName[HSF_DIGITAL_CHANNELS][32]; /**< DI 通道名（服务端给）。 */
    char     m_doName[HSF_DIGITAL_CHANNELS][32]; /**< DO 通道名（服务端给）。 */
    bool     m_received;    /**< 收到过至少一拍（历史事实，不复位）。 */
    uint64_t m_lastMs;      /**< 最近一次收到推送的本地时刻（单调毫秒）。 */
} HsfLinkDigital;

/** @brief 数字量数据陈旧阈值（毫秒）；超过即视为断流。 */
#define HSF_LINK_DIGITAL_STALE_MS   3000u

/**
 * @brief 取最近数字量快照（借用；从未收到时 m_received=false）。
 * @warning 返回内部存储，禁释放；内容随下一拍刷新。
 */
const HsfLinkDigital* HsfLinkClient_digital(void);

/** @brief 数字量数据是否**当前新鲜**（收到过且未超时）。 */
bool HsfLinkClient_digitalFresh(void);

/**
 * @brief 下发单路 DO 控制（异步；结果经 commandResult 回调）。
 * @param channel DO 通道号（0..count-1）。
 * @param on true=通电。
 * @return 已入发送队列返回 true；未就绪/参数非法返回 false。
 */
bool HsfLinkClient_digitalControl(int channel, bool on);

/** @brief 请求一拍数字量快照（Response 走 digital 回调）。 */
bool HsfLinkClient_digitalSnapshotRequest(void);

/**
 * @brief 注册数字量遥测更新回调（每拍推送/应答后触发）。
 */
void HsfLinkClient_setDigitalCallback(HsfLinkNotifyFn cb, void* userData);

/* ==================== 电磁阀（6 路） ==================== */

/**
 * @brief 单路电磁阀快照。
 */
typedef struct HsfLinkValveItem {
    char m_name[48];    /**< 阀名（UTF-8）。 */
    int  m_state;       /**< 阀位状态（HsfValveState）。 */
    bool m_openCmd;     /**< 最近命令意图（true=开）。 */
    bool m_openFb;      /**< 开到位反馈。 */
    bool m_closeFb;     /**< 关到位反馈。 */
    bool m_ready;       /**< 点位是否配置齐全。 */
    bool m_valid;       /**< 数据有效。 */
    char m_text[96];    /**< 状态说明（故障原因/排查提示，可空）。 */
} HsfLinkValveItem;

/**
 * @brief 电磁阀组快照（定长数组，零分配）。
 */
typedef struct HsfLinkValves {
    HsfLinkValveItem m_items[HSF_VALVE_COUNT]; /**< 各路条目。 */
    int      m_count;      /**< 实际路数。 */
    bool     m_received;   /**< 收到过至少一拍（历史事实，不复位）。 */
    uint64_t m_lastMs;     /**< 最近一次收到推送的本地时刻（单调毫秒）。 */
} HsfLinkValves;

/** @brief 电磁阀数据陈旧阈值（毫秒）。 */
#define HSF_LINK_VALVE_STALE_MS     3000u

/* ==================== 阀告警事件（0x00010007 告警帧） ==================== */

/**
 * @brief 单条阀告警事件。
 * @details [帧判型] 阀状态主题上的帧带 "ac" 键即为告警帧（键空间 a{i}_*
 *          与状态帧 v{i}_* 不相交）——解析入口必须**先认 ac 再认状态**，
 *          否则告警帧会被当状态帧把阀表清空（顶层无 count ⇒ count=0 ⇒
 *          全部按钮死），与 Python 客户端 model.apply 的同一条口径。
 *          [文案口径] m_text 是**服务端原文**，客户端不自行拼文案。
 */
typedef struct HsfLinkValveAlarm {
    char m_name[48];    /**< 阀名（a{i}_vn 原文）。 */
    char m_text[128];   /**< 告警文字（a{i}_t 原文）。 */
    int  m_kind;        /**< 类型（1 超时 / 2 冲突 / 3 恢复，HSF_VALVE_ALARM_*）。 */
    int  m_index;       /**< 路号（a{i}_vi；-1=未提供）。 */
    int64_t m_atMs;     /**< 服务端发生时刻（a{i}_at，毫秒）。 */
} HsfLinkValveAlarm;

/** @brief 阀告警类型（与 Python proto.VALVE_ALARM_* / 服务端 HsfValveAlarmKind 同值）。 */
#define HSF_VALVE_ALARM_TIMEOUT    1  /**< 动作超时未到位。 */
#define HSF_VALVE_ALARM_CONFLICT   2  /**< 开/关到位信号同时有效。 */
#define HSF_VALVE_ALARM_CLEARED    3  /**< 恢复（客户端据此走"已恢复"文案）。 */

/** @brief 阀告警回调：每条告警逐条调用（主事件循环内直呼，可直改 UI）。 */
typedef void (*HsfLinkValveAlarmFn)(void* userData,
                                    const HsfLinkValveAlarm* alarm);

/**
 * @brief 注册阀告警回调（单槽，后注册覆盖前者；cb=NULL 注销）。
 */
void HsfLinkClient_setValveAlarmCallback(HsfLinkValveAlarmFn cb, void* userData);

/**
 * @brief 取最近电磁阀快照（借用；从未收到时 m_received=false）。
 * @warning 返回内部存储，禁释放；内容随下一拍刷新。
 */
const HsfLinkValves* HsfLinkClient_valves(void);

/** @brief 电磁阀数据是否**当前新鲜**。 */
bool HsfLinkClient_valvesFresh(void);

/**
 * @brief 最近一次阀控制的处置结果（借用；空串=暂无/未知）。
 * @details 取值：issued（新动作）/ repeated（同目标在途重发，计时不重置）/
 *          reversed（在途反向）/ ignored（已到位同目标，幂等忽略）/
 *          external（DO 被外部改动，服务端接管）。随控制应答到达时更新。
 */
const char* HsfLinkClient_lastValveCmdResult(void);

/**
 * @brief 下发单路阀控制（异步；结果经 commandResult 回调）。
 * @param index 路序（0..count-1）。
 * @param wantOpen true=开阀、false=关阀。
 * @return 已入发送队列返回 true。
 */
bool HsfLinkClient_valveControl(int index, bool wantOpen);

/** @brief 请求一拍电磁阀快照（Response 走 valve 回调）。 */
bool HsfLinkClient_valveSnapshotRequest(void);

/**
 * @brief 注册电磁阀遥测更新回调（每拍推送/应答后触发）。
 */
void HsfLinkClient_setValveCallback(HsfLinkNotifyFn cb, void* userData);

/* ==================== 按需订阅（页面显示期订阅，隐藏期退订） ==================== */

/** @brief 可单独开关的遥测主题（与 HSF_TOPIC_* 一一对应）。 */
typedef enum HsfLinkTopic {
    HsfLinkTopic_Vfd       = 0,  /**< 变频器遥测。 */
    HsfLinkTopic_Ai        = 1,  /**< 模拟量遥测。 */
    HsfLinkTopic_TempHum   = 2,  /**< 温湿度遥测。 */
    HsfLinkTopic_FlowMeter = 3,  /**< 电磁流量计遥测。 */
    HsfLinkTopic_Digital   = 4,  /**< 数字量（16DI/16DO）遥测。 */
    HsfLinkTopic_Valve     = 5,  /**< 电磁阀 6 路状态。 */
    HsfLinkTopic_Turbidity = 6,  /**< 在线浊度仪（两台合成一帧）。 */
    HsfLinkTopic_Pump      = 7,  /**< 进水泵状态。 */
    HsfLinkTopic_Count     = 8
} HsfLinkTopic;

/**
 * @brief 增加某主题的订阅引用（引用计数；首个引用才真正发订阅帧）。
 * @param topic 主题枚举。
 * @return 已就绪并订阅成功（或引用已计数）返回 true；未登录返回 false。
 * @note  服务端对**新订阅**会立即定向补发一拍，故本调用返回后无需等
 *        周期即会有首帧到达。引用计数支持多页共用同一主题。
 */
bool HsfLinkClient_subscribe(HsfLinkTopic topic);

/**
 * @brief 释放某主题的订阅引用（计数归零才真正发退订帧）。
 * @param topic 主题枚举。
 * @note  隐藏的页面退订后服务端不再投递，省带宽与解码开销。
 */
void HsfLinkClient_unsubscribe(HsfLinkTopic topic);

/**
 * @brief 幂等补订：把「引用>0 但订阅帧没发出去」的主题补发一次。
 * @return 链路就绪并执行了补订流程返回 true；未就绪返回 false。
 * @note  只补发、**不改引用计数**（引用仍由 subscribe/unsubscribe 记账），
 *        故可在页面每次激活时无条件调用。已订阅的主题由会话层幂等短路，
 *        不会重复发帧。用途：页面集合未变时 hsf_pageTopicSwitch 走快路径
 *        返回，之前发送失败的订阅就没有第二次机会（界面表现为某几张卡
 *        永久「--」），本函数补上这条重试路径。
 */
bool HsfLinkClient_ensureSubscriptions(void);

/* ==================== 白盒测试钩子（生产路径请勿调用） ==================== */

/**
 * @brief 把一条构造好的 **Response** 业务帧喂给业务分发（白盒测试专用）。
 * @param msgCode 业务命令码（HSF_CMD_*）。
 * @param payload 载荷字节（BSON；借用，调用期间有效即可）。
 * @param len 载荷长度。
 * @return 参数非法（len>0 而 payload 为 NULL）返回 false，其余返回 true。
 * @details 快照命令的应答分支是「请求-应答」通道，离屏测试收不到真实应答；
 *          本钩子直接调用线上同一条分发函数（hsfLcDispatch），从而是"应答
 *          →解析→刷新 model→通知订阅者"全链路的可断言入口。
 *          当前用例：Test/XProtocolTest/hsf_regression_test.c §I。
 * @warning 绕过会话层的 pending 登记/去重/应答认领，**只能**在测试里用。
 */
bool HsfLinkClient_testDispatchResponse(uint16_t msgCode,
                                        const uint8_t* payload, uint32_t len);

#ifdef __cplusplus
}
#endif
#endif /* HSFLINKCLIENT_H */
