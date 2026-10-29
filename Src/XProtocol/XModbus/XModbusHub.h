/**
 * @file       XModbusHub.h
 * @brief      Modbus 聚合设备：多从站虚拟合一（RTU/TCP × 串口/网口透明）。
 * @details    [XModbus 聚合设备 2026-10-26] 持有 N 个底层 XModbusClient（TCP
 *             或 RTU 串口由调用方构造后注入，所有权转移 Hub），经
 *             XModbusMapping 把统一虚拟地址空间翻译到底层设备。
 *             [2026-10-29 缓存层重构] 统一缓存从「每类型一张按虚拟地址铺满
 *             的连续图」改为「每设备若干按远端地址组织的缓存块」：
 *               - 块 = 同 (寄存器区, 从站) 下远端连续的映射段合并（块内无
 *                 洞——写安全的前提）；虚拟地址读取经「视图切片」二分翻译；
 *               - 读：每设备一张合并后的读组表（gap ≤ setReadMergeGap 的
 *                 相邻块并一帧，按协议上限切分），设备周期定时器逐组单飞；
 *               - 写：先写块缓存（乐观回显、微秒级受理）并标脏区间，再按
 *                 设备把脏区间合并成写组（FC15/FC16 多写一帧）下发；失败
 *                 保留脏区间周期重发，达阈值发一次失败沿/滞留提示；
 *               - 质量：块级 lastOkMs 支持 valueAge/pointValid 点级新鲜度；
 *               - 底层断线自动重连复用 XModbusClient_setAutoReconnect，
 *                 设备恢复后自动恢复轮询并补写滞留写，对上层透明；
 *               - 信号面：设备状态变化 / 值变化（type 字段区分来源寄存器
 *                 类别）/ 写完成（含失败沿）/ 从站失联恢复（unitLost/
 *                 unitAlive）/ 写滞留提示（writeStalled）。
 *             [2026-10-28 单元退避] 轮询健康按 (设备, 从站) 粒度簿记
 *             （XPoll/XPollScheduler）：连续失败达阈值（setUnitBackoff，
 *             默认 3）的从站静默退避（默认 5000ms），静默期间其读组不再
 *             提交——RTU 共口多从站时把总线让给活从站，静默到期自动探活。
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

/** @brief 单帧写寄存器上限（FC16：123 寄存器 = 246 字节数据 + 9 帧头）。 */
#define XMODBUS_HUB_WRITE_MAX_REGISTERS 123
/** @brief 单帧写线圈上限（FC15：1968 位 = 246 字节数据 + 9 帧头）。 */
#define XMODBUS_HUB_WRITE_MAX_BITS 1968
/** @brief 单帧写点数总上限（两类取大者；按区实际上限见上面两个宏）。 */
#define XMODBUS_HUB_WRITE_MAX_VALUES XMODBUS_HUB_WRITE_MAX_BITS

/** @brief 单帧读上限：寄存器区 125，位区 2000（对齐 Modbus 协议约束）。 */
#define XMODBUS_HUB_READ_MAX_REGISTERS 125
#define XMODBUS_HUB_READ_MAX_BITS 2000

/**
 * @brief 缓存块内的映射切片：把块内远端区间翻译回统一虚拟地址。
 * @details 一个块由同 (区, 从站) 下远端连续的若干映射段合并而成，切片是
 *          块内一段与某条映射段的交集——回填事件按切片切片发射（上层看到
 *          的 valueChanged 粒度仍是「每条映射段一次」）。
 */
typedef struct XModbusHubCacheSeg {
    uint16_t virtualStart;      /**< 切片在统一空间的起点。 */
    uint16_t remoteStart;       /**< 切片在远端的起点。 */
    uint16_t count;             /**< 切片长度（点数/位）。 */
} XModbusHubCacheSeg;

/**
 * @brief 块内待写脏区间（块内偏移，半开区间）。
 * @details 同一块内相邻/重叠的写会合并成一条（合并窗口内的多次写只留
 *          最终值）；成功收口按「缓存值 vs 已发快照」逐点比较，未变的区间
 *          出队、变了的区间保留重发。
 */
typedef struct XModbusHubDirtyRange {
    uint16_t offset;            /**< 块内起始偏移。 */
    uint16_t count;             /**< 点数。 */
    uint32_t attempts;          /**< 已下发次数（含失败重发）。 */
    bool stallNotified;         /**< writeStalled 已发（每意图至多一次）。 */
    bool failNotified;          /**< writeCompleted(ok=false) 已发（每意图至多一次）。 */
} XModbusHubDirtyRange;

/**
 * @brief 每设备缓存块：同 (寄存器区, 从站) 下远端连续的一段。
 * @details 块内不允许有未映射洞（合并前已按远端起止切分）；块持有缓存
 *          数据本体（位区 XBitArray 紧凑、寄存器区 2B/点），读写均落块。
 */
typedef struct XModbusHubCacheBlock {
    uint16_t type;              /**< XModbusRegisterType（块所属寄存器区）。 */
    int unitId;                 /**< 从站地址。 */
    uint16_t remoteStart;       /**< 块在远端的起点。 */
    uint16_t count;             /**< 块长（点数/位）。 */
    XModbusDataUnit* data;      /**< 缓存数据（位区 XBitArray / 寄存器区 2B/点）。 */
    struct XVector* segs;       /**< 覆盖的映射切片（XModbusHubCacheSeg，按虚拟地址升序）。 */
    struct XVector* dirty;      /**< 待写脏区间（XModbusHubDirtyRange，按偏移升序）。 */
    uint64_t lastOkMs;          /**< 整块最近一次读回成功时刻（单调毫秒；0=从未）。 */
    uint32_t failCount;         /**< 连续读失败次数（质量观测；成功清零）。 */
    bool refreshRequested;      /**< 已被 requestRefresh 标脏，下一拍优先读。 */
} XModbusHubCacheBlock;

/**
 * @brief 读组：合并后的一次轮询读帧（同区、同从站、远端连续或小空洞）。
 * @details 由该设备的块按 (区, 从站) 分组后，gap ≤ readMergeGap 的相邻块
 *          合并，再按协议上限切分。块表有序（区, 从站, 远端起点），故组内
 *          块一定连续排列 [firstBlock, firstBlock+blockCount)。
 */
typedef struct XModbusHubReadGroup {
    uint16_t type;              /**< 寄存器区。 */
    int unitId;                 /**< 从站地址。 */
    uint16_t remoteStart;       /**< 读帧远端起点。 */
    uint16_t count;             /**< 读帧点数。 */
    uint32_t firstBlock;        /**< 组内首块在设备块表中的下标。 */
    uint32_t blockCount;        /**< 组内块数。 */
} XModbusHubReadGroup;

/**
 * @brief 视图切片：统一虚拟地址 → 缓存块的反向翻译项。
 * @details Hub 每寄存器类型维护一张按 virtualStart 升序的切片表，二分定位
 *          后即可直接读块内数据；切片不拥有块（块被重建时整表同步重建）。
 */
typedef struct XModbusHubCacheSlice {
    uint16_t virtualStart;      /**< 切片在统一空间的起点。 */
    uint16_t count;             /**< 切片长度。 */
    int deviceId;               /**< 所属设备句柄。 */
    int unitId;                 /**< 从站地址。 */
    uint16_t blockOffset;       /**< 切片起点对应块内偏移。 */
    XModbusHubCacheBlock* block;/**< 所属缓存块（借用）。 */
} XModbusHubCacheSlice;

/**
 * @brief 在途写快照（收口时用于逐点比较并决定脏区间去留）。
 * @details 快照独立于脏区间表：在途期间上层还可继续写同一点，合并/扩展
 *          脏区间不影响本次收口的判定基线。坐标用远端绝对地址而非块
 *          指针——映射重建会整表换块，收口按 (区, 从站, 远端地址) 重新
 *          定位，找不到即视为块已不存在（只发完成信号，不再动脏区间）。
 */
typedef struct XModbusHubInflightWrite {
    uint16_t type;              /**< 寄存器区。 */
    int unitId;                 /**< 从站地址。 */
    uint16_t remoteStart;       /**< 远端起点（块内绝对地址）。 */
    uint16_t count;             /**< 点数。 */
    uint16_t virtualStart;      /**< 统一空间起点（信号用）。 */
    uint16_t virtualCount;      /**< 统一空间点数。 */
    uint32_t attempts;          /**< 本次下发时的累计尝试次数。 */
    uint16_t* values;           /**< 已发值快照（堆；收口释放）。 */
} XModbusHubInflightWrite;

/**
 * @brief 每设备运行时槽：client 所有权 + 缓存块表 + 读组表 + 单飞在途。
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
    XModbusHubReadGroup inflightGroup;  /**< 在途读组快照（映射重建不影响收口）。 */
    XModbusHubInflightWrite inflightWrite; /**< 在途写快照（收口后释放快照内存）。 */
    struct XVector* blocks;             /**< 缓存块表（XModbusHubCacheBlock*，按 区/从站/远端 升序）。 */
    struct XVector* groups;             /**< 读组表（XModbusHubReadGroup，按构建顺序）。 */
    size_t groupCursor;                 /**< 下一待发读组下标（在途期间不动）。 */
    uint32_t pollOkCount;               /**< 累计轮询成功组数。 */
    uint32_t pollErrCount;              /**< 累计轮询失败组数。 */
    uint32_t reconnectCount;            /**< 累计重连成功次数（离线→在线跃迁）。 */
    uint64_t lastSuccessMs;             /**< 最后一次成功应答时刻（单调毫秒）。 */
    XPollScheduler unitSched;           /**< 从站失联退避表（XPoll，按 unitId 簿记；
                                         *   块表重建时随块去重重建）。 */
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
    struct XVector* m_view[4];          /**< 每类型虚拟视图切片表（XModbusHubCacheSlice，按 virtualStart 升序）。 */
    uint16_t m_spanStart[4];            /**< 该类型映射虚拟起点（无映射=0）。 */
    uint16_t m_spanCount[4];            /**< 该类型映射虚拟跨度（0=未映射；cacheCount 口径）。 */
    size_t m_pollIntervalDefaultMs;     /**< 新设备缺省轮询间隔。 */
    unsigned m_pollAreas;               /**< 轮询区掩码（bit 1<<XModbusRegisterType 枚举值：DI=bit1 Coils=bit2 IR=bit3 HR=bit4；默认 DI|IR）。 */
    bool m_polling;                     /**< 轮询总闸（startPolling/stopPolling）。 */
    XTimerId m_retryTimer;              /**< 写重发周期定时器。 */
    size_t m_writeRetryIntervalMs;      /**< 写重发间隔毫秒。 */
    bool m_changedOnlySignals;          /**< valueChanged 仅在值确实变化时发射（默认 true）。 */
    uint32_t m_unitMaxFails;            /**< 从站失联退避连续失败阈值（默认 3；setUnitBackoff）。 */
    uint32_t m_unitMuteMs;              /**< 从站失联退避静默时长毫秒（默认 5000）。 */
    int m_writeStallNoticeAt;           /**< 写滞留提示/失败沿阈值 attempts（<1 关闭；默认 3）。 */
    uint16_t m_readMergeGap;            /**< 读组合并允许的最大空洞点数（默认 0=不跨洞）。 */
    size_t m_writeCoalesceMs;           /**< 写合并窗口毫秒（0=不等窗口，受理即下发）。 */
    XTimerId m_flushTimer;              /**< 合并窗口到期刷写定时器（窗口内首笔写拉起）。 */
    uint64_t m_lastWriteMs;             /**< 最近一次写受理时刻（窗口起点；单调毫秒）。 */
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
/** @brief 注入映射管理器（所有权转移；内部重建设备缓存块与视图/读组）。 */
void XModbusHub_setMapping(XModbusHub* hub, XModbusMapping* ownedMapping);
/** @brief 取映射管理器（借用）。 */
XModbusMapping* XModbusHub_mapping(const XModbusHub* hub);
/**
 * @brief 映射在 attach 后又被直接修改（addRange/removeRange）时调用：
 *        重建全部设备缓存块、虚拟视图与读组（块缓存尽量保留交集值）。
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
 * @brief 读合并空洞容限：同从站同区相邻块之间远端间隔 ≤ gap 时并进同一
 *        读帧（多读几个点换少一帧）。默认 0=严格不跨洞。
 * @note  只影响读；写绝不跨洞（块内无洞是写安全前提）。
 */
void XModbusHub_setReadMergeGap(XModbusHub* hub, uint16_t gap);
/**
 * @brief 从站失联退避配置：连续失败达阈值进入静默（不再提交该从站读组，
 *        RTU 共口多从站时把总线让给活从站），静默到期自动探活；跃迁发射
 *        unitLost/unitAlive 信号。[2026-10-28 单元退避]
 * @param maxFails 连续失败阈值（>=1 生效；默认 3）。
 * @param muteMs 静默时长毫秒（>=1000 生效；默认 5000）。
 * @note 已登记设备即时同步生效；块表重建时沿用本配置。
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
 * @return true=命中映射；false=未映射/越界。
 * @note 命中但从未回填时返回该区初始哨兵（寄存器 0xFFFF / 位 0），
 *       需要区分「有没有数据」用 pointValid/valueAge。
 */
bool XModbusHub_value(const XModbusHub* hub, XModbusRegisterType type,
                      uint16_t virtualAddr, uint16_t* outValue);
/** @brief 批量读统一缓存（区间内未映射点写 0xFFFF 哨兵并返回 false）。 */
bool XModbusHub_values(const XModbusHub* hub, XModbusRegisterType type,
                       uint16_t virtualStart, uint16_t count, uint16_t* outValues);
/** @brief 该类型映射虚拟跨度（未建返回 0）。 */
uint16_t XModbusHub_cacheCount(const XModbusHub* hub, XModbusRegisterType type);
/** @brief 该类型最后一次成功读回时刻（单调毫秒；从未=0）。 */
uint64_t XModbusHub_lastUpdateMs(const XModbusHub* hub, XModbusRegisterType type);

/* ==================== 点级质量（缓存块 lastOkMs） ==================== */
/**
 * @brief 查询某映射点的数据年龄。
 * @param ageMs 输出年龄毫秒（调用方提供；未映射时不写）。
 * @return true=命中映射（*ageMs 有效：从未成功读回时置 UINT64_MAX）；
 *         false=未映射/越界。
 */
bool XModbusHub_valueAge(const XModbusHub* hub, XModbusRegisterType type,
                         uint16_t virtualAddr, uint64_t* ageMs);
/**
 * @brief 某映射点是否「新鲜」：命中映射 且 最近成功读回距今 ≤ maxAgeMs。
 * @param maxAgeMs 新鲜度门限（毫秒）。
 */
bool XModbusHub_pointValid(const XModbusHub* hub, XModbusRegisterType type,
                           uint16_t virtualAddr, uint64_t maxAgeMs);
/**
 * @brief 请求立即刷新一段虚拟区间（把覆盖块标为待读，下一拍优先读）。
 * @details 上层要「刚写完立刻读回」或按需读时用，不必自己发 Modbus 事务。
 * @return true=至少命中一个块（已标待读并立即泵设备）。
 */
bool XModbusHub_requestRefresh(XModbusHub* hub, XModbusRegisterType type,
                               uint16_t virtualStart, uint16_t count);

/* ==================== 输出写（缓存层 + 合并 + 失败重发） ==================== */
/**
 * @brief 写单个点（Coils/HoldingRegisters）：立即写块缓存（乐观回显）→
 *        标脏区间 → 按设备合并下发。
 * @return true=意图已受理（内存写，不代表上总线成功；结果经 writeCompleted
 *         信号，失败沿 ok=false）；false=参数/映射/越界拒绝（未受理）。
 */
bool XModbusHub_writeValue(XModbusHub* hub, XModbusRegisterType type,
                           uint16_t virtualAddr, uint16_t value);
/**
 * @brief 批量写（区间可跨映射段，但须同设备同从站同区且远端连续；
 *        段间有洞一律拒绝——防把哨兵写进设备）。
 */
bool XModbusHub_writeValues(XModbusHub* hub, XModbusRegisterType type,
                            uint16_t virtualStart, uint16_t count,
                            const uint16_t* values);
/** @brief 当前滞留脏区间数（合并后；0=全部落定）。 */
int XModbusHub_pendingWriteCount(const XModbusHub* hub);
/** @brief 写重发间隔毫秒（默认 1000）。 */
void XModbusHub_setWriteRetryInterval(XModbusHub* hub, size_t ms);
/**
 * @brief 写合并窗口毫秒：窗口内对同块的多次写只留最终值，窗口到期统一
 *        刷写（高频滑动条场景把 N 帧压成 1 帧）。默认 0=受理即下发
 *        （行为与旧版一致）；Industry 通道建议 20~50ms。
 */
void XModbusHub_setWriteCoalesceMs(XModbusHub* hub, size_t ms);
/** @brief 立即结束合并窗口并下发全部滞留脏区间（不等定时器）。 */
void XModbusHub_flush(XModbusHub* hub);
/**
 * @brief 写滞留/失败可见性阈值：脏区间下发次数 attempts 达该值时发一次
 *        writeStalled 提示与一次 writeCompleted(ok=false) 失败沿（各自
 *        每意图至多一次，合并进新写时重置）。[2026-10-28 写滞留可见性 +
 *        2026-10-29 失败沿]
 * @param attempts 阈值（<1 关闭提示与失败沿；默认 3）。
 * @note 失败沿是 Industry 通道 XDigitalOutput_writeDone(false) 的故障判据；
 *       只增可见性不改重发语义（失败仍保留脏区间周期重试直至成功）。
 */
void XModbusHub_setWriteStallNotice(XModbusHub* hub, int attempts);

/* ==================== 统计 ==================== */
/** @brief 设备轮询成功组数。 */
uint32_t XModbusHub_pollOkCount(const XModbusHub* hub, int deviceId);
/** @brief 设备轮询失败组数。 */
uint32_t XModbusHub_pollErrCount(const XModbusHub* hub, int deviceId);
/** @brief 设备重连成功次数。 */
uint32_t XModbusHub_reconnectCount(const XModbusHub* hub, int deviceId);
/** @brief 设备最后一次成功应答时刻（单调毫秒；从未=0）。 */
uint64_t XModbusHub_deviceLastSuccessMs(const XModbusHub* hub, int deviceId);
/** @brief 设备读组数（合并后；诊断/回归用，未登记设备返回 0）。 */
int XModbusHub_readGroupCount(const XModbusHub* hub, int deviceId);

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
 * @brief 写意图落定信号：成功收口发 ok=true；下发次数达失败阈值
 *        （setWriteStallNotice）时发一次 ok=false（每意图至多一次）。
 * @param ok true=已确认写入从站；false=重试达阈值仍失败（仍会继续重发）。
 */
void* XModbusHub_writeCompleted_signal(XModbusHub* hub, int type,
                                       int virtualStart, int count, bool ok);
/** @brief 设备统计快照信号（可选挂接；每次 pollOk/pollErr/重连后发射）。 */
void* XModbusHub_deviceStatsChanged_signal(XModbusHub* hub, int deviceId);
/**
 * @brief 从站失联信号：unitLost(int deviceId, int unitId)。
 * @details [2026-10-28 单元退避] 某底层设备的某从站连续轮询失败达阈值
 *          （setUnitBackoff）进入静默退避时发射一次（跃迁沿，静默期内
 *          不重复发）；静默期间该从站的读组不再提交，静默到期自动探活。
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
 * @details [2026-10-28 写滞留可见性] 脏区间下发次数 attempts 首次达到
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
