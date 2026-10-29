/**
 * @file       XModbusHub.h
 * @brief      Modbus 聚合设备：多从站虚拟合一（RTU/TCP × 串口/网口透明）。
 * @details    [XModbus 聚合设备 2026-10-26] 持有 N 个底层 XModbusClient（TCP
 *             或 RTU 串口由调用方构造后注入，所有权转移 Hub），经
 *             XModbusMapping 把统一虚拟地址空间翻译到底层设备：
 *               - 输入区（离散输入/输入寄存器，可扩到全部四类）按设备周期
 *                 自动轮询，回填统一缓存（单飞原则：设备有在途应答跳过本拍）；
 *               - 输出区（线圈/保持寄存器）按需写入：乐观更新本地缓存后
 *                 下发，失败/超时/离线滞留重发队列周期重试，成功即出队停发；
 *               - 底层断线自动重连复用 XModbusClient_setAutoReconnect，
 *                 设备恢复后自动恢复轮询并补写滞留写，对上层透明；
 *               - 信号面：设备状态变化 / 值变化（type 字段区分来源寄存器
 *                 类别）/ 写完成 / 从站失联恢复（unitLost/unitAlive，
 *                 2026-10-28 单元退避）/ 写滞留提示（writeStalled，
 *                 2026-10-28 写滞留可见性）。
 *             [2026-10-28 单元退避] 轮询健康按 (设备, 从站) 粒度簿记
 *             （XPoll/XPollScheduler，公共计划表组件）：连续失败达阈值
 *             （setUnitBackoff，默认 3）的从站静默退避（默认 5000ms），
 *             静默期间其映射段不再向 client 提交——RTU 共口多从站时把
 *             总线让给活从站，静默到期自动探活恢复。
 *             Hub 是组合器不是协议设备（继承 XObject 而非 XModbusDevice），
 *             不发起未被映射驱动的 Modbus 事务。线程模型：单线程事件循环，
 *             零锁；计时一律 XDateTime_currentMSecsSinceEpoch()。
 *             定时器一律 XObject 自带机制（startTimer_ms/killTimer +
 *             EXObject_TimerEvent 重载分发），不内嵌独立 XTimer 实例
 *             （约束文档「定时器使用规范 2026-10-26 裁定」）。
 */
#ifndef XMODBUSHUB_H
#define XMODBUSHUB_H
#include "XModbus_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON
#if XMODBUS_HUB_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XObject.h"
#include "XTimer.h"
#include "XModbusClient.h"
#include "XModbusMapping.h"
#include "XPollScheduler.h"

/** @brief 单条写意图内联值容量上限（FC16 单帧协议上限 123 寄存器，
 *  聚合场景单点/短块写为主；超限写入直接拒绝）。 */
#define XMODBUS_HUB_WRITE_MAX_VALUES 32

/**
 * @brief 写重发队列条目：一次输出写意图（成功前持续存活）。
 */
typedef struct XModbusHubWriteIntent {
    int deviceId;                       /**< 目标底层设备句柄。 */
    int unitId;                         /**< 目标从站地址（映射段携带）。 */
    uint16_t type;                      /**< XModbusRegisterType（仅 Coils/HoldingRegisters）。 */
    uint16_t virtualStart;              /**< 统一空间起点（信号与缓存回填用）。 */
    uint16_t remoteStart;               /**< 远端起点。 */
    uint16_t count;                     /**< 点数（1..XMODBUS_HUB_WRITE_MAX_VALUES）。 */
    uint16_t values[XMODBUS_HUB_WRITE_MAX_VALUES]; /**< 待写值。 */
    uint32_t attempts;                  /**< 已尝试次数（含失败重发）。 */
    bool stallNotified;                 /**< writeStalled 提示已发（每意图至多一次）。 */
} XModbusHubWriteIntent;

/**
 * @brief 每设备运行时槽：client 所有权 + 轮询计划 + 单飞在途 + 统计。
 */
typedef struct XModbusHubDevice {
    XModbusClient* client;              /**< 底层客户端（Hub 拥有，析构 deleteLater）。 */
    int id;                             /**< 设备句柄（Hub 内自增，不随删除复用）。 */
    struct XString* name;               /**< 显示名（可空）。 */
    XModbusDevice_State state;          /**< 最近状态（stateChanged 缓存）。 */
    size_t pollIntervalMs;              /**< 轮询间隔毫秒（0=该设备不轮询）。 */
    XTimerId pollTimer;                 /**< 轮询周期定时器（startPolling 拉起）。 */
    XModbusReply* inflight;             /**< 单飞在途应答（轮询/写共用）。 */
    uint8_t inflightKind;               /**< 0=无 1=轮询 2=写（XModbusHubInflightKind）。 */
    size_t inflightSegIndex;            /**< 在途轮询对应的计划段下标。 */
    XModbusHubWriteIntent inflightWrite; /**< 在途写意图快照（队列重排不影响收口）。 */
    struct XVector* plan;               /**< 轮询计划（XModbusMapSegment，attach/refresh 重建）。 */
    size_t planCursor;                  /**< 下一待发段下标（在途期间不动）。 */
    uint32_t pollOkCount;               /**< 累计轮询成功段数。 */
    uint32_t pollErrCount;              /**< 累计轮询失败段数。 */
    uint32_t reconnectCount;            /**< 累计重连成功次数（离线→在线跃迁）。 */
    uint64_t lastSuccessMs;             /**< 最后一次成功应答时刻（单调毫秒）。 */
    XPollScheduler unitSched;           /**< 从站失联退避表（XPoll，按 unitId 簿记；
                                         *   计划重建时随映射段去重重建）。 */
} XModbusHubDevice;

/** @brief 在途请求类别。 */
typedef enum {
    XModbusHubInflight_None = 0,
    XModbusHubInflight_Poll = 1,
    XModbusHubInflight_Write = 2
} XModbusHubInflightKind;

XCLASS_DEFINE_BEGING(XModbusHub)
XCLASS_DEFINE_EXTEND_END(XModbusHub, XObject)

/**
 * @brief Modbus 聚合设备结构体（继承 XObject；组合 N 个 XModbusClient）。
 */
typedef struct XModbusHub {
    XObject m_class;                    /**< 基类（第一成员）。 */
    XModbusMapping* m_mapping;          /**< 映射管理器（setMapping 转移所有权）。 */
    struct XVector* m_devices;          /**< 设备槽表（XModbusHubDevice 值数组）。 */
    int m_nextDeviceId;                 /**< 设备句柄自增器（不复用删除设备的 id）。 */
    XModbusDataUnit* m_cache[4];        /**< 统一缓存（每类型一个，覆盖 m_cacheStart 起连续区）。 */
    uint16_t m_cacheStart[4];           /**< 缓存对应虚拟起点。 */
    uint16_t m_cacheCount[4];           /**< 缓存点数（0=未建）。 */
    size_t m_pollIntervalDefaultMs;     /**< 新设备缺省轮询间隔。 */
    unsigned m_pollAreas;               /**< 轮询区掩码（bit 1<<XModbusRegisterType 枚举值：DI=bit1 Coils=bit2 IR=bit3 HR=bit4；默认 DI|IR）。 */
    bool m_polling;                     /**< 轮询总闸（startPolling/stopPolling）。 */
    struct XVector* m_writeQueue;       /**< 写重发队列（XModbusHubWriteIntent）。 */
    XTimerId m_retryTimer;              /**< 写重发周期定时器。 */
    size_t m_writeRetryIntervalMs;      /**< 写重发间隔毫秒。 */
    bool m_changedOnlySignals;          /**< valueChanged 仅在值确实变化时发射（默认 true）。 */
    uint32_t m_unitMaxFails;            /**< 从站失联退避连续失败阈值（默认 3；setUnitBackoff）。 */
    uint32_t m_unitMuteMs;              /**< 从站失联退避静默时长毫秒（默认 5000）。 */
    int m_writeStallNoticeAt;           /**< 写滞留提示阈值 attempts（<1 关闭；默认 3）。 */
} XModbusHub;

/* ==================== 类初始化 / 构造 ==================== */
XVtable* XModbusHub_class_init(void);
void XModbusHub_init(XModbusHub* hub);
XModbusHub* XModbusHub_create_ex(XMemoryType memory);
#define XModbusHub_create() XModbusHub_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 设备管理 ==================== */
/**
 * @brief 注入一个底层客户端（所有权转移 Hub；内部接 stateChanged 跟踪）。
 * @param client 已构造的 XModbusTcpClient/XModbusRtuSerialClient（连接参数
 *        由调用方先行设置；Hub 统一置 autoReconnect=true）。
 * @param name 显示名（可 NULL）。
 * @return 设备句柄（>0）；失败返回 -1（不接管 client）。
 */
int XModbusHub_addDevice(XModbusHub* hub, XModbusClient* client, const char* name);
/** @brief 移除并释放设备（滞留写意图一并作废并回 writeCompleted(false)）。 */
bool XModbusHub_removeDevice(XModbusHub* hub, int deviceId);
/** @brief 设备数。 */
int XModbusHub_deviceCount(const XModbusHub* hub);
/** @brief 按句柄取底层 client（借用；Hub 仍拥有）。 */
XModbusClient* XModbusHub_deviceClient(const XModbusHub* hub, int deviceId);
/** @brief 按句柄取设备名（借用，可能为 NULL）。 */
const char* XModbusHub_deviceName_const(const XModbusHub* hub, int deviceId);
/** @brief 设备最近状态（XModbusDevice_State）。 */
XModbusDevice_State XModbusHub_deviceState(const XModbusHub* hub, int deviceId);
/** @brief 聚合在线：至少一台设备且全部 Connected。 */
bool XModbusHub_isOnline(const XModbusHub* hub);
/** @brief 全部设备执行 connectDevice（返回全部已发起）。 */
bool XModbusHub_connectAll(XModbusHub* hub);
/** @brief 全部设备执行 disconnectDevice 并停轮询/停重发。 */
void XModbusHub_disconnectAll(XModbusHub* hub);

/* ==================== 映射 ==================== */
/** @brief 注入映射管理器（所有权转移；内部重建缓存与各设备轮询计划）。 */
void XModbusHub_setMapping(XModbusHub* hub, XModbusMapping* ownedMapping);
/** @brief 取映射管理器（借用）。 */
XModbusMapping* XModbusHub_mapping(const XModbusHub* hub);
/**
 * @brief 映射在 attach 后又被直接修改（addRange/removeRange）时调用：
 *        重建统一缓存与全部设备轮询计划（缓存值尽量保留交集）。
 */
void XModbusHub_refreshMapping(XModbusHub* hub);

/* ==================== 轮询（输入区自动更新） ==================== */
/** @brief 新设备缺省轮询间隔毫秒（默认 1000）。 */
void XModbusHub_setPollIntervalDefault(XModbusHub* hub, size_t ms);
/** @brief 覆盖某设备轮询间隔；0=该设备不轮询。轮询中即时生效。 */
bool XModbusHub_setPollInterval(XModbusHub* hub, int deviceId, size_t ms);
/** @brief 轮询区掩码（位 1<<XModbusRegisterType 枚举值：DI=bit1 Coils=bit2
 *  IR=bit3 HR=bit4；默认仅 DI|IR 输入区）。 */
void XModbusHub_setPollAreas(XModbusHub* hub, unsigned areaMask);
/**
 * @brief 从站失联退避配置：连续失败达阈值进入静默（不再提交该从站轮询段，
 *        RTU 共口多从站时把总线让给活从站），静默到期自动探活；跃迁发射
 *        unitLost/unitAlive 信号。[2026-10-28 单元退避]
 * @param maxFails 连续失败阈值（>=1 生效；默认 3）。
 * @param muteMs 静默时长毫秒（>=1000 生效；默认 5000）。
 * @note 已登记设备即时同步生效；映射刷新重建退避表时沿用本配置。
 */
void XModbusHub_setUnitBackoff(XModbusHub* hub, uint32_t maxFails, uint32_t muteMs);
/** @brief 启动全部设备轮询（含写重发定时器）。 */
void XModbusHub_startPolling(XModbusHub* hub);
/** @brief 停止全部轮询与写重发（滞留写意图保留，重启后续发）。 */
void XModbusHub_stopPolling(XModbusHub* hub);
/** @brief 轮询总闸状态。 */
bool XModbusHub_isPolling(const XModbusHub* hub);

/* ==================== 统一缓存读写 ==================== */
/**
 * @brief 读统一缓存一个点（四类区通用；位区返回 0/1）。
 * @return true=命中映射且缓存已建；false=未映射/越界/缓存未建。
 */
bool XModbusHub_value(const XModbusHub* hub, XModbusRegisterType type,
                      uint16_t virtualAddr, uint16_t* outValue);
/** @brief 批量读统一缓存（区间内未映射点写 0xFFFF 哨兵并返回 false）。 */
bool XModbusHub_values(const XModbusHub* hub, XModbusRegisterType type,
                       uint16_t virtualStart, uint16_t count, uint16_t* outValues);
/** @brief 该类型缓存覆盖范围（未建返回 0）。 */
uint16_t XModbusHub_cacheCount(const XModbusHub* hub, XModbusRegisterType type);
/** @brief 该类型最后一次成功回填/写回显时刻（单调毫秒；从未=0）。 */
uint64_t XModbusHub_lastUpdateMs(const XModbusHub* hub, XModbusRegisterType type);

/* ==================== 输出写（按需 + 失败重发） ==================== */
/**
 * @brief 写单个点（Coils/HoldingRegisters）：乐观更新缓存→入重发队列→即时下发。
 * @return true=意图已受理（不代表写成功；结果经 writeCompleted 信号）；
 *         false=参数/映射/容量拒绝（未受理）。
 */
bool XModbusHub_writeValue(XModbusHub* hub, XModbusRegisterType type,
                           uint16_t virtualAddr, uint16_t value);
/** @brief 批量写（区间须完整落在单条映射段内；v1 拒绝跨段写）。 */
bool XModbusHub_writeValues(XModbusHub* hub, XModbusRegisterType type,
                            uint16_t virtualStart, uint16_t count,
                            const uint16_t* values);
/** @brief 当前滞留写意图数（0=全部落定）。 */
int XModbusHub_pendingWriteCount(const XModbusHub* hub);
/** @brief 写重发间隔毫秒（默认 1000）。 */
void XModbusHub_setWriteRetryInterval(XModbusHub* hub, size_t ms);
/**
 * @brief 写滞留提示阈值：写意图下发次数 attempts 首次达到该值时发射一次
 *        writeStalled 信号（每意图至多一次）。[2026-10-28 写滞留可见性]
 * @param attempts 阈值（<1 关闭提示；默认 3）。
 * @note 只增可见性不改重发语义：失败仍静默留队周期重试直至成功。
 */
void XModbusHub_setWriteStallNotice(XModbusHub* hub, int attempts);

/* ==================== 统计 ==================== */
/** @brief 设备轮询成功段数。 */
uint32_t XModbusHub_pollOkCount(const XModbusHub* hub, int deviceId);
/** @brief 设备轮询失败段数。 */
uint32_t XModbusHub_pollErrCount(const XModbusHub* hub, int deviceId);
/** @brief 设备重连成功次数。 */
uint32_t XModbusHub_reconnectCount(const XModbusHub* hub, int deviceId);
/** @brief 设备最后一次成功应答时刻（单调毫秒；从未=0）。 */
uint64_t XModbusHub_deviceLastSuccessMs(const XModbusHub* hub, int deviceId);

/* ==================== 信号 ==================== */
/**
 * @brief 设备状态变化信号（任一底层设备 stateChanged 转发）。
 * @param state 新状态（XModbusDevice_State：0=Unconnected 1=Connecting 2=Connected）。
 */
void* XModbusHub_deviceStateChanged_signal(XModbusHub* hub, int deviceId, int state);
/**
 * @brief 统一缓存值变化信号（type 字段区分来源寄存器类别：轮询回填或写回显）。
 * @details changedOnly 模式（默认）下仅当回填/写导致值确实变化时发射，
 *          区间为发生变化的段切片。
 */
void* XModbusHub_valueChanged_signal(XModbusHub* hub, int type,
                                     int virtualStart, int count);
/**
 * @brief 写意图落定信号（成功才发；失败静默重发直至成功或设备被移除）。
 * @param ok 恒 true（失败不发射）；保留参数便于调用方槽复用。
 */
void* XModbusHub_writeCompleted_signal(XModbusHub* hub, int type,
                                       int virtualStart, int count, bool ok);
/** @brief 设备统计快照信号（可选挂接；每次 pollOk/pollErr/重连后发射）。 */
void* XModbusHub_deviceStatsChanged_signal(XModbusHub* hub, int deviceId);
/**
 * @brief 从站失联信号：unitLost(int deviceId, int unitId)。
 * @details [2026-10-28 单元退避] 某底层设备的某从站连续轮询失败达阈值
 *          （setUnitBackoff）进入静默退避时发射一次（跃迁沿，静默期内
 *          不重复发）；静默期间该从站的轮询段不再提交，静默到期自动探活。
 */
void* XModbusHub_unitLost_signal(XModbusHub* hub, int deviceId, int unitId);
/**
 * @brief 从站恢复信号：unitAlive(int deviceId, int unitId)。
 * @details [2026-10-28 单元退避] 失联（静默中或静默到期探活在途）的从站
 *          收到首个成功应答时发射一次。
 */
void* XModbusHub_unitAlive_signal(XModbusHub* hub, int deviceId, int unitId);
/**
 * @brief 写滞留提示信号：writeStalled(int type, int virtualStart, int count,
 *        int attempts)。
 * @details [2026-10-28 写滞留可见性] 写意图下发次数 attempts 首次达到
 *          阈值（setWriteStallNotice，缺省 3）时发射一次；仅提升可见性，
 *          重发语义不变（失败静默重发直至成功）。
 */
void* XModbusHub_writeStalled_signal(XModbusHub* hub, int type,
                                     int virtualStart, int count, int attempts);

#define XModbusHub_deleteLater     XObject_deleteLater
#define XModbusHub_deinitLater     XObject_deinitLater

#endif /* XMODBUS_HUB_ON */
#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XMODBUSHUB_H */
