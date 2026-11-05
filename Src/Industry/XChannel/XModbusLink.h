/**
 * @file       XModbusLink.h
 * @brief      Modbus 链路连接（XIoLink 的 Modbus 实现）。
 * @details    [2026-11-02 XChannel 重写] 本类是「链路层」在通道侧的**唯一**
 *             入口。它把 XModbusHub 的 (区, 虚拟地址) 寻址翻译成 role 键，
 *             并**只订阅一次** Hub 信号（旧实现是每个点各订阅一次，
 *             M512 单设备 66 条连接、每次值变化 66 次回调）。
 *
 *             [零遍历：基址偏移密集表]
 *             查询「某地址属于哪个 role」是数据到达路径上的最高频操作，
 *             故用**直接映射**而非二分：每个区维护一张 uint16_t 密集表
 *             m_roleByAddr[area]，下标 = addr - m_base[area]。
 *             一次减法 + 一次判界 + 一次数组取址 = O(1)。
 *
 *             内存：按实际映射区间分配（不是 0..65535 全域）。M512 实测
 *             22 段共约 200 个地址，每设备约 1KB 量级。
 *             表项值 XMODBUSLINK_ROLE_NONE（0xFFFF）表示该地址未绑定。
 *
 *             [数据流]
 *             Hub valueChanged(type, vStart, count)
 *               → 本类按区取表，连续区间顺序回填
 *               → 命中 role 则 XIoChannelGroup_feedRole(group, role, raw)
 *             Hub writeCompleted(type, vStart, count, ok)
 *               → 同样按表查 role
 *               → XIoChannelGroup_completeWrite(group, role, ok)
 *
 *             [约束翻译] XVfdUnitProtocol（单帧读上限/组边界/只 FC06）由
 *             装配层翻译成 XModbusHubUnitLimits 后经 setUnitLimits 登记，
 *             本类不参与——它是装配知识，不是运行时知识。
 */
#ifndef XMODBUSLINK_H
#define XMODBUSLINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XIoLink.h"
#include "XModbusHub.h"
#include <stdint.h>

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON

/** @brief 密集表空位标记（role 上界 256，故 0xFFFF 不会与合法 role 冲突）。 */
#define XMODBUSLINK_ROLE_NONE ((uint16_t)0xFFFFu)

/** @brief 链路管理的区数（DI/DO/AI/AO）。 */
#define XMODBUSLINK_AREA_COUNT 4

/**
 * @brief 点级陈旧门限（毫秒）：超过这么久没从从站读回 ⇒ 该点判不可信。
 * @details [取值依据] 必须比最慢的正常轮询周期大一个量级，又不能大到让
 *          现场等太久才看到"离线"。现场最慢的从站节拍是 500ms（温湿度 /
 *          流量计），最慢的扫描周期是 20ms；5000ms 给足 10 次重试余量，
 *          同时现场掉线 5 秒内必被察觉——运维能接受。
 *          [为什么不做成参数] 它属于"链路可信度"的行业常识口径，不是每个
 *          部署各不相同的现场参数；真要按部署调，改本宏并重编即可（与
 *          HsfServerConfig 里那些点位/网络参数同性质）。
 */
#ifndef XMODBUSLINK_STALE_MS
#define XMODBUSLINK_STALE_MS 5000
#endif

XCLASS_DEFINE_BEGING(XModbusLink)
XCLASS_DEFINE_ENUM(XModbusLink, BindRole) = XCLASS_VTABLE_GET_SIZE(XIoLink),
XCLASS_DEFINE_ENUM(XModbusLink, ReadRole),
XCLASS_DEFINE_ENUM(XModbusLink, WriteRole),
XCLASS_DEFINE_ENUM(XModbusLink, IsAsync),
XCLASS_DEFINE_ENUM(XModbusLink, Start),
XCLASS_DEFINE_ENUM(XModbusLink, Stop),
XCLASS_DEFINE_ENUM(XModbusLink, RoleFresh),
XCLASS_DEFINE_END(XModbusLink)

/**
 * @brief 单个区的基址偏移密集表。
 */
typedef struct XModbusLinkAreaMap {
    uint16_t* m_roleByAddr;   /**< 密集表：下标 = addr - m_base；NULL=未建。 */
    uint16_t m_base;          /**< 表基址（该区已绑定地址的最小值）。 */
    uint32_t m_span;          /**< 表长度（覆盖 [m_base, m_base+span)）。 */
} XModbusLinkAreaMap;

/**
 * @brief Modbus 链路连接（持 Hub，按区建密集表 + role 反向表）。
 * @details 正向表 m_maps[area] 负责「数据到达：地址 → role」（O(1)）；
 *          反向表 m_roleArea/m_roleAddr 负责「上层读写：role → 地址」（O(1)）。
 *          两张表在 BindRole 时同步维护，读写路径上都没有查找循环。
 */
typedef struct XModbusLink {
    XIoLink m_base;                              /**< 基类（第一成员）。 */
    XModbusHub* m_hub;                           /**< 数据源 Hub（借用，不拥有）。 */
    XModbusLinkAreaMap m_maps[XMODBUSLINK_AREA_COUNT]; /**< 各区 role 反查表。 */
    uint16_t* m_roleArea;                        /**< role → 区下标（ROLE_CAP 长）。 */
    uint16_t* m_roleAddr;                        /**< role → 虚拟地址（ROLE_CAP 长）。 */
    XConnection* m_valueConn;                    /**< Hub valueChanged 订阅（唯一一条）。 */
    XConnection* m_doneConn;                     /**< Hub writeCompleted 订阅（唯一一条）。 */
} XModbusLink;

/* ==================== 类初始化 / 生命周期 ==================== */

/**
 * @brief 初始化 XModbusLink 类的虚函数表（单例）。
 * @return 指向初始化完成的 XVtable；不可释放。
 */
XVtable* XModbusLink_class_init(void);

/**
 * @brief 堆上创建 Modbus 链路。
 * @param hub 数据源 Hub（借用，调用方保证生命周期覆盖本链路）；不能为 NULL。
 * @return 新链路；失败返回 NULL。
 * @note 订阅在 Start 里建立（见 XModbusLink_start），构造时只记录指针。
 */
XModbusLink* XModbusLink_create(XModbusHub* hub);

/**
 * @brief 栈上初始化 Modbus 链路（基段初始化 + 清密集表）。
 * @param link 目标对象；不能为 NULL。
 * @param hub 数据源 Hub（借用）；不能为 NULL。
 * @return 无。
 */
void XModbusLink_init(XModbusLink* link, XModbusHub* hub);

/* ==================== 绑定与统计 ==================== */

/**
 * @brief 读某 role 当前绑定的 (区, 地址)。
 * @param link 链路；不能为 NULL。
 * @param role 语义槽号。
 * @param outArea 输出区下标（0..3，见 XModbusLink_areaOfType）；可为 NULL。
 * @param outAddr 输出虚拟地址；可为 NULL。
 * @return 已绑定 true；未绑定返回 false。
 * @note 供测试与诊断使用（如原来直访 XModbusPoint::m_address 的地方）。
 */
bool XModbusLink_queryBinding(const XModbusLink* link, uint16_t role,
                              int* outArea, uint16_t* outAddr);

/**
 * @brief 本链路已绑定 role 数。
 * @param link 链路；NULL 返回 0。
 * @return 绑定数。
 */
int XModbusLink_boundCount(const XModbusLink* link);

/**
 * @brief 订阅数（Hub 信号连接条数）。
 * @param link 链路；NULL 返回 0。
 * @return 已建立的连接数（正常应为 0 或 2：valueChanged + writeCompleted）。
 * @note 性能断言的取证口：M512 单设备不论挂多少点，本值恒为 2。
 */
int XModbusLink_subscriptionCount(const XModbusLink* link);

/**
 * @brief 把点类型映射为区下标。
 * @param type 点类型。
 * @param useHolding AI 区选（true=HR；false=IR）。
 * @return 区下标（0..3）；未知类型返回 -1。
 */
int XModbusLink_areaOfType(int type, bool useHolding);

/**
 * @brief 把区下标映射为 Hub 寄存器类型。
 * @param area 区下标（0..3）。
 * @return XModbusRegisterType。
 */
XModbusRegisterType XModbusLink_hubAreaType(int area);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON && XINDUSTRY_MODBUS_ON && XMODBUS_HUB_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMODBUSLINK_H */
