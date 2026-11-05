/**
 * @file       HsfServerIoValve.h
 * @brief      服务端电磁阀组：6 路阀（每路 1 DO + 2 DI 反馈）状态判读与动作。
 * @details    与 Python 侧 `hsfsrv/valves.py` 的 `ValveBank` 同口径：
 *               · 阀状态**只由两个到位反馈推导**，不看命令回显；
 *               · 动作超时未到位 → 故障（但要区分"阀卡住"与"行程开关没接"）；
 *               · 点位全部来自配置（哪个 DO、哪个 DI），未配置的路显示
 *                 "未配置"且控制被拒；
 *               · 单点写走数字量站的 FC05 路径。
 *
 *             [为什么状态不信命令回显] 写了 1 只代表"命令发出去了"，阀有没有
 *             真的动要靠行程开关说话。工业现场最常见的故障就是"命令发了、
 *             阀卡住了"——若拿命令回显当状态，这种情况会被显示成"已开"。
 *
 *             [为什么区分"卡住"与"没接反馈"] 两个到位反馈都为 0 有两种含义：
 *               a) 刚下过命令、还在动作中（正常，几秒）；
 *               b) 行程开关没接/没供电，永远读不到到位信号。
 *             超时后若**历史上从未出现过任何到位信号**，更可能是 (b)，
 *             报"故障"会天天误报、现场会直接忽略告警 ⇒ 报"未知 + 排查提示"。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERIOVALVE_H
#define HSFSERVERIOVALVE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "XObject.h"
#include "XhsfLinkProto.h"   /* HSF_VALVE_COUNT / HsfValveState。 */

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(HsfServerIoValve)
XCLASS_DEFINE_EXTEND_END(HsfServerIoValve, XObject)

/**
 * @brief 单路电磁阀运行态。
 * @details 成员禁止直接访问，一律经 API 操作。
 */
typedef struct HsfServerIoValveItem {
    char m_name[48];        /**< 显示名（UTF-8）。 */
    int  m_do;              /**< 动作出口 DO 通道号；-1=未配置。 */
    int  m_openDi;          /**< 开到位 DI 通道号；-1=未配置。 */
    int  m_closeDi;         /**< 关到位 DI 通道号；-1=未配置。 */
    bool m_openCmd;         /**< 最近一次命令意图（true=开）。 */
    bool m_openFb;          /**< 开到位反馈（本拍）。 */
    bool m_closeFb;         /**< 关到位反馈（本拍）。 */
    int  m_state;           /**< 阀位状态（HsfValveState）。 */
    bool m_valid;           /**< 数据有效（反馈读到且未陈旧）。 */
    bool m_online;          /**< 反馈源模块本拍在线且数据新鲜。 */
    bool m_everFeedback;    /**< 历史上是否出现过任何到位信号。 */
    uint64_t m_cmdAtMs;     /**< 下发命令的本地时刻（超时判定；0=无）。 */
    bool m_pendingCmd;      /**< 有命令待记时刻（setOutput 置位，poll 消费）。 */
    /**
     * @brief 本次动作是否已经报过超时（**保证一次动作只告警一次**）。
     * @details [为什么需要] 超时判据每拍都成立（now - cmdAt > timeout），
     *          没有这个位就会每 200ms 推一条告警，客户端被刷屏。
     */
    bool m_timeoutReported;
    /** @brief 本次是否已报过互斥故障（同上，防刷屏）。 */
    bool m_conflictReported;
    /** @brief 本拍新产生、待上层推送的告警（HsfValveAlarmKind；0=无）。 */
    int  m_pendingAlarmKind;
    /** @brief 待推送告警的文字（m_pendingAlarmKind 非 0 时有效）。 */
    char m_pendingAlarmText[128];
    char m_text[96];        /**< 状态说明（故障原因/排查提示，可空）。 */
} HsfServerIoValveItem;

/**
 * @brief 电磁阀组运行态（本模块拥有其内部资源）。
 * @details 成员禁止直接访问，一律经 API 操作。
 */
typedef struct HsfServerIoValve {
    XObject m_class;                     /**< 第一成员：基类 XObject。 */
    HsfServerIoValveItem m_items[HSF_VALVE_COUNT]; /**< 各路运行态。 */
    int  m_count;                        /**< 实际路数（0..HSF_VALVE_COUNT）。 */
    int  m_moveTimeoutMs;                /**< 动作超时（毫秒）。 */
    bool m_enabled;                      /**< 是否启用。 */
    bool m_ready;                        /**< 至少一路配好点位。 */
} HsfServerIoValve;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 HsfServerIoValve 类的虚函数表（单例）。
 * @return 类虚拟函数表指针；不可释放。
 */
XVtable* HsfServerIoValve_class_init(void);

/**
 * @brief 栈上初始化电磁阀组（空配置，未启用）。
 * @param st 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 */
void HsfServerIoValve_init(HsfServerIoValve* st);

/**
 * @brief 单路点位装配入参（与配置层解耦：配置结构体由调用方转换后传入）。
 */
typedef struct HsfServerIoValveCfg {
    const char* m_name;   /**< 显示名（UTF-8；借用，内部深拷贝；NULL=缺省名）。 */
    int m_do;             /**< 动作出口 DO 通道号；-1=未配置。 */
    int m_openDi;         /**< 开到位 DI 通道号；-1=未配置。 */
    int m_closeDi;        /**< 关到位 DI 通道号；-1=未配置。 */
} HsfServerIoValveCfg;

/**
 * @brief 按配置装配电磁阀组（不触碰硬件；DO/DI 翻译由调用方负责）。
 * @param st 目标阀组。
 * @param items 点位配置数组（借用，本函数深拷贝名字）。
 * @param count 路数（0..HSF_VALVE_COUNT）。
 * @param moveTimeoutMs 动作超时（毫秒；<500 夹到 500）。
 * @param enabled 是否启用。
 * @return 装配成功（含"全部未配置"这种合法情形）返回 true。
 */
bool HsfServerIoValve_configure(HsfServerIoValve* st,
                                const HsfServerIoValveCfg* items, int count,
                                int moveTimeoutMs, bool enabled);

/**
 * @brief 清空（幂等）。
 */
void HsfServerIoValve_clear(HsfServerIoValve* st);

/* ==================== 动作 ==================== */

/**
 * @brief 下发开关命令。
 * @param st 目标阀组。
 * @param index 路序（0..count-1）。
 * @param wantOpen true=开阀、false=关阀。
 * @param outErr 失败原因输出（UTF-8，容量 cap；可为 NULL）。
 * @param cap 失败原因缓冲容量。
 * @return 命令已下发返回 true；未配置/越界/未启用返回 false（原因写 outErr）。
 * @note  受理 ≠ 到位：到位经反馈判读（HsfServerIoValve_poll 后状态变化）。
 */
bool HsfServerIoValve_setOutput(HsfServerIoValve* st, int index, bool wantOpen,
                                char* outErr, int cap);

/* ==================== 反馈更新 / 状态判读 ==================== */

/**
 * @brief 用一帧数字量反馈刷新全部阀的状态（含动作超时判定）。
 * @param st 目标阀组。
 * @param diMask DI 位图（bit n = DI 通道 n）。
 * @param diValid DI 数据是否有效（false ⇒ 全路 valid=false，状态保持）。
 * @param diFreshMask **按通道**的新鲜度位图（bit n = DI 通道 n 现在仍可信）。
 * @param nowMs 当前单调毫秒（由调用方给，便于测试注入）。
 *
 * @details [为什么 diFreshMask 是位图而不是一个 bool]
 *          32 点 DI 是**两台模块聚合**出来的（.12 提供 0..15、.13 提供
 *          16..31），两台的在线状态互相独立。电磁阀可能一部分挂在 .12 那半区、
 *          一部分挂在 .13 那半区。若只给一个整组 bool，就会出现
 *          「.12 正常 ⇒ 整组算在线 ⇒ .13 上那几路读到恒 0 却仍算有效」
 *          ⇒ 那几路显示"无到位反馈（检查行程开关接线）"，而真相是模块掉线。
 *          按通道给新鲜度后，每路用自己的两个 DI 通道判定，
 *          .13 掉线时只有挂在它上面那几路报"模块离线，无法控制"。
 *
 * @note diValid=false（从没读到过）与 diFreshMask 里该位=0（曾经读到过、
 *       但现在已经过期）是两种不同现场情形，提示文案必须不同。
 *       调用方没有新鲜度信息时传 0xFFFFFFFFu（视为全部新鲜）。
 */
void HsfServerIoValve_poll(HsfServerIoValve* st, uint32_t diMask, bool diValid,
                           uint32_t diFreshMask, uint64_t nowMs);

/* ==================== 查询 ==================== */

/** @brief 路数。 */
int HsfServerIoValve_count(const HsfServerIoValve* st);

/** @brief 至少一路配好点位。 */
bool HsfServerIoValve_ready(const HsfServerIoValve* st);

/** @brief 已配置点位的路数。 */
int HsfServerIoValve_readyCount(const HsfServerIoValve* st);

/**
 * @brief 取某一路只读条目。
 * @return 借用指针；越界返回 NULL。
 */
const HsfServerIoValveItem* HsfServerIoValve_item(const HsfServerIoValve* st,
                                                  int index);

/**
 * @brief 取某一路**本拍新产生**的告警并清除它（取走即消费）。
 * @param st 目标阀组。
 * @param index 路序（0..count-1）。
 * @param outKind 输出告警类型（HsfValveAlarmKind）；无告警时不改。
 * @param outText 输出告警文字缓冲；可为 NULL。
 * @param cap outText 容量。
 * @return 有告警返回 true（并已清除该路待推标志）；无告警返回 false。
 *
 * @details [为什么是"取走即消费"而不是查询后另清] 告警是**事件**，一条只该
 *          推一次。若拆成"查询 + 显式清除"，调用方漏清就会每拍重复推；
 *          取走语义天然防重复，也让 poll 可以无条件每拍调用。
 *
 * @note 每次 poll 至多产生一条告警（同拍内真值与超时不会同时新出现）。
 */
bool HsfServerIoValve_takeAlarm(HsfServerIoValve* st, int index, int* outKind,
                                char* outText, int cap);

/** @brief 故障路数。 */
int HsfServerIoValve_faultCount(const HsfServerIoValve* st);

/**
 * @brief 动作超时（毫秒）。
 * @param st 目标阀组；NULL 返回 0。
 * @return 当前生效的动作超时（已按 ≥500ms 夹紧）。
 * @note 告警帧里的"超时秒数"取自本值，保证推给客户端的是服务端真实口径。
 */
int HsfServerIoValve_moveTimeoutMs(const HsfServerIoValve* st);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERIOVALVE_H */
