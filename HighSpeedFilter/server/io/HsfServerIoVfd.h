
/**
 * @file       HsfServerIoVfd.h
 * @brief      服务端变频器站：品牌装配（M512 绑定表）+ 通道组 + 设备抽象。
 * @details    每路总线最多一个变频器站。装配知识全部来自品牌层：
 *               - 点规格表 XVfdM512_pointSpecs（语义 role/type/tag/signed）；
 *               - 绑定表 XVfdM512_modbusBinding（role → 远端寄存器 + 通信组
 *                 + 从站协议约束）。
 *             本模块只做「翻译与挂接」：
 *               通信组 → 映射段（恒等映射：虚拟地址 = 远端地址）；
 *               点规格 → 通道点（XIoAnalogPoint / XIoPoint）+ role 绑定；
 *               品牌绑定表 → XVfdDevice_bindPoint；role → 链路地址；
 *               品牌从站约束 → XModbusHub_setUnitLimits。
 *
 *             [恒等映射的前提] 每路总线各有一个 Hub，本路虚拟地址空间由
 *             变频器独占 0x0000..0xFFFF 中的品牌地址段；同一条 485 上的
 *             其它从站（温湿度）用避开这些地址的独立 vbase——见
 *             HsfServerIo 的编排（温湿度默认落在 0x0100 起始，不与 M512
 *             的 0x1000/0x3000/0xF0xx 等段重叠）。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERIOVFD_H
#define HSFSERVERIOVFD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "XObject.h"

struct HsfServerBus;
struct XModbusMapping;
struct XIoChannelGroup;
struct XModbusLink;
struct XVfdM512;

/** @brief 变频器站保留的虚拟地址空间容量（诊断/冲突排查用）。 */
#define HSF_VFD_VADDR_CAP  0x10000u

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(HsfServerIoVfd)
XCLASS_DEFINE_EXTEND_END(HsfServerIoVfd, XObject)

/**
 * @brief 一路变频器站的运行态（每路总线最多一个；本模块拥有其内部资源）。
 * @details 成员禁止直接访问，一律经 API 操作。总线为借用：调用方保证
 *          其生命周期覆盖本站，并在总线销毁前先 unmount/析构本站。
 */
typedef struct HsfServerIoVfd {
    XObject m_class;                        /**< 第一成员：基类 XObject。 */
    struct HsfServerBus* m_bus;             /**< 所属总线（借用）。 */
    struct XModbusMapping* m_map;           /**< 装配中的映射表（借用）。 */
    struct XIoChannelGroup* m_group;        /**< 站通道组（自有）。 */
    struct XModbusLink* m_link;             /**< 站链路（自有）。 */
    struct XVfdM512* m_dev;                 /**< 变频器设备（品牌层；自有）。 */
    int m_unitId;                           /**< 从站号。 */
    uint16_t m_vbase[48];                   /**< 各通信组的虚拟起点（惰性建段结果）。 */
    uint8_t  m_vbaseBuilt[48];              /**< 该组是否已建段。 */
    int m_groupCount;                       /**< 本站通信组数（一圈轮转发读组的组数）。 */
    uint32_t m_staleMs;                     /**< 生效的快照陈旧阈值（随轮转周期算）。 */
    bool m_mounted;                         /**< 装配完成标志。 */
} HsfServerIoVfd;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 HsfServerIoVfd 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* HsfServerIoVfd_class_init(void);

/**
 * @brief 栈上初始化变频器站（未装配）。
 * @param st 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 */
void HsfServerIoVfd_init(HsfServerIoVfd* st);

/* ==================== 装配 ==================== */

/**
 * @brief 在指定总线上装配变频器站（映射段 + 点 + 链路 + 品牌绑定）。
 * @param st 目标站（调用方提供存储；内部先清零）。
 * @param bus 所属总线（借用；须已 open）。
 * @param map 总线的映射表（借用；本模块往里登记各通信组段）。
 * @param unitId 从站站号（面板 PD-02=2）。
 * @param actionTimeoutMs 动作监督超时（毫秒）。
 * @param statusIntervalMs 状态快照发布周期（毫秒）。
 * @return 全部成功返回 true；任一步失败返回 false（st 已清理）。
 * @note 必须在 HsfServerBus_start 之前调用（映射段要随映射表一起提交）。
 */
bool HsfServerIoVfd_mount(HsfServerIoVfd* st, struct HsfServerBus* bus,
                          struct XModbusMapping* map, int unitId,
                          int actionTimeoutMs, int statusIntervalMs);

/**
 * @brief 拆除变频器站（幂等；不触碰总线）。
 * @param st 目标站。
 * @note **必须在总线释放之前调用**（链路订阅 Hub 信号）。
 */
void HsfServerIoVfd_unmount(HsfServerIoVfd* st);

/* ==================== 查询 ==================== */

/**
 * @brief 取变频器设备（借用；控制/状态/编码走它）。
 * @return 设备指针；未装配返回 NULL。@warning 禁释放。
 */
struct XVfdM512* HsfServerIoVfd_device(const HsfServerIoVfd* st);

/**
 * @brief 本站是否已装配完成。
 */
bool HsfServerIoVfd_isMounted(const HsfServerIoVfd* st);

/**
 * @brief 按当前轮询节拍重算并下发快照陈旧阈值。
 * @param st 变频器站；不能为 NULL。
 * @param pollMs 该总线的轮询节拍（毫秒；Hub 每拍发一个读组）。
 * @return 生效的阈值毫秒（0 = 未装配）。
 * @details [为什么阈值要跟着「轮转周期」走 2026-11-10 现场"变频器 --- 循环"]
 *          Hub 每拍只发**一个**读组 ⇒ 一圈要把本站的组轮完 ⇒ 数据刷新
 *          节拍 = 组数 × 拍长（现场 14 组 × 200ms = 2.8s）。设备层缺省
 *          阈值「发布周期 × 5」=1s **盖不住一圈** ⇒ 每隔两拍判一次陈旧、
 *          掩码清零 ⇒ 客户端变频器在「---」与真值之间来回闪。
 *          口径：有效阈值 = max(基线, 轮转周期 × 2)（容一轮的抖动）。
 *          采集周期改了要重算（调用点在 HsfServerIo.c 的采集节拍应用处）。
 */
uint32_t HsfServerIoVfd_applyStaleMs(HsfServerIoVfd* st, int pollMs);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERIOVFD_H */
