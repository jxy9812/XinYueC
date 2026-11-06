
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
 * @brief 当前登录用户名（借用；未就绪为空串）。
 */
const char* HsfLinkClient_user(void);

/**
 * @brief 当前会话认证角色（XLinkAuthRole；未就绪/匿名返回 0）。
 * @note  仅用于界面侧"能否操作"的**提示**判定；权威判定始终在服务端。
 */
int HsfLinkClient_role(void);

/* ==================== 回调注册（先注册后 open 才不漏首拍） ==================== */

/**
 * @brief 注册就绪态回调（连接+认证成功或断开/重连翻转时触发）。
 * @note  回调内可查 HsfLinkClient_isReady 区分方向；重复注册覆盖旧者。
 */
void HsfLinkClient_setReadyCallback(HsfLinkNotifyFn cb, void* userData);

/**
 * @brief 注册失败回调（认证失败；回调后连接停止重连，须重新 open）。
 */
void HsfLinkClient_setFailCallback(HsfLinkNotifyFn cb, void* userData);

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
 */
bool HsfLinkClient_tempHumSnapshotRequest(int side);

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
 * @brief 请求一拍电磁流量计快照（载荷带 side；异步回包触发回调）。
 * @param side 侧号（0=A 1=B）。
 * @return 已入发送队列返回 true；连接未就绪或侧号非法返回 false。
 * @note  正常数据由服务端周期推送驱动；本接口只用于「立即要一拍」的场合。
 */
bool HsfLinkClient_flowMeterSnapshotRequest(int side);

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
 * @brief 最近一次历史分页结果（借用指针；未收到过应答时 m_valid=false）。
 */
const HsfHistoryPage* HsfLinkClient_history(void);

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
 * @brief 最近一次服务端配置快照（借用指针；未收到过应答时 m_valid=false）。
 */
const HsfConfig* HsfLinkClient_config(void);

/**
 * @brief 当前连接的服务端地址（"host:port"；借用；未连接为空串）。
 */
const char* HsfLinkClient_serverAddress(void);

/**
 * @brief 当前会话角色中文（管理员/运行员/观察；借用静态串）。
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

#ifdef __cplusplus
}
#endif
#endif /* HSFLINKCLIENT_H */
