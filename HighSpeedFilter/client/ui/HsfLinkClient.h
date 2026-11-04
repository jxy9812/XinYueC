
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
#include "XIndustry_config.h"
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

/* ==================== 控制下发 ==================== */

#if XVFD_ON
/**
 * @brief 下发变频器控制命令（异步；结果经 commandResult 回调）。
 * @param req 控制请求（借用）。
 * @return 命令已入发送队列返回 true（受理 ≠ 执行成功）；连接未就绪或
 *         编码失败返回 false。
 */
bool HsfLinkClient_vfdControl(const XVfdControlRequest* req);
#endif

/**
 * @brief 请求一拍变频器遥测快照（Response 走 vfd 回调；连接未就绪返回 false）。
 */
bool HsfLinkClient_vfdSnapshotRequest(void);

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

/* ==================== 按需订阅（页面显示期订阅，隐藏期退订） ==================== */

/** @brief 可单独开关的遥测主题（与 HSF_TOPIC_* 一一对应）。 */
typedef enum HsfLinkTopic {
    HsfLinkTopic_Vfd       = 0,  /**< 变频器遥测。 */
    HsfLinkTopic_Ai        = 1,  /**< 模拟量遥测。 */
    HsfLinkTopic_TempHum   = 2,  /**< 温湿度遥测。 */
    HsfLinkTopic_FlowMeter = 3,  /**< 电磁流量计遥测。 */
    HsfLinkTopic_Count     = 4
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
