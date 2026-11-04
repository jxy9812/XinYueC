
/**
 * @file       HsfServerIoFlowMeter.h
 * @brief      服务端电磁流量计站：从站约束 + 映射段 + 通道组 + XFlowMeter 设备。
 * @details    电磁流量计（上自仪 LD 系列 / L_mag B 系列转换器）挂在**路 B**
 *             的独立 485 总线上，与路 A 的变频器/温湿度完全隔离。做成独立
 *             模块而不是塞进 HsfServerIo，因为它的装配知识与前两者不同：
 *
 *             [变量区一帧读完]（实测，见 docs/LD电磁流量计_RS485通讯实测与使用指南_20261104.md）
 *               - 变量区 0x1010~0x1025 共 **22 个寄存器**，FC04 一条帧读回；
 *               - 连读上限实测 125（协议规范上限）也 OK，22 个远在能力内；
 *               - 参数区不支持连读（恒只返回 1 个），但本站只读变量区，不受影响。
 *
 *             [32 位量的拼装归属] 瞬时流量/流速/百分比/电导比/正反向累积都是
 *             32 位量（占连续两个寄存器，高字在前）。拼装由 **XFlowMeter 设备层**
 *             完成，本站只负责把 22 个寄存器按 role 顺序喂进通道组：
 *                 role 0..21 ↔ 虚拟地址 vbase+0..vbase+21
 *                 role 0/1 = 瞬时流量 HI/LO，2/3 = 流速 HI/LO，…，16/17 = 单位
 *             32 位字**必须恒等标度**（1/1/0、无符号），否则标度运算会破坏
 *             IEEE754 位模式——这是本站装配的硬约束。
 *
 *             [为什么不用 XModbusLink] 与温湿度站同理：通用链路按 Hub 信号
 *             逐点喂值，而这里 22 个寄存器是同一条映射段、同一次回填。本站
 *             按 valueChanged 的区间逐地址重取（信号只给区间不给值，与
 *             XModbusLink / 温湿度站同款口径），一次喂全 22 个 role，
 *             让设备层一次整拍重算——避免「高字到、低字还没到」的中间态。
 *
 *             [质量口径] 不用设备层的陈旧计时（管道静止时数值恒定，
 *             feedRole「值未变则静默」，设备的最近一拍会停在很久以前而被
 *             误判陈旧）。真正的数据源新鲜度取 Hub 块级 lastOkMs，
 *             经 XModbusHub_pointValid 判定——与温湿度站同款。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERIOFLOWMETER_H
#define HSFSERVERIOFLOWMETER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "XObject.h"

struct HsfServerBus;
struct XIoChannelGroup;
struct XFlowMeter;
struct XModbusMapping;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(HsfServerIoFlowMeter)
XCLASS_DEFINE_EXTEND_END(HsfServerIoFlowMeter, XObject)

/**
 * @brief 一路电磁流量计站（每路总线最多一个；本模块拥有其内部资源）。
 * @details 成员禁止直接访问，一律经 API 操作。总线为借用：调用方保证
 *          其生命周期覆盖本站，并在总线销毁前先 unmount/析构本站。
 */
typedef struct HsfServerIoFlowMeter {
    XObject m_class;                        /**< 第一成员：基类 XObject（信号接收者身份）。 */
    struct HsfServerBus* m_bus;             /**< 所属总线（借用；NULL=未挂）。 */
    struct XIoChannelGroup* m_group;        /**< 本站通道组（自有；点随组 autoDelete）。 */
    struct XFlowMeter* m_dev;               /**< 流量计设备抽象（自有）。 */
    struct XModbusMapping* m_map;           /**< 装配中的映射表（借用，提交给总线后作废）。 */
    XConnection* m_conn;                    /**< Hub valueChanged 订阅（唯一一条）。 */
    XTimerId m_sampleTimer;                 /**< 采样定时器（周期拍；静止量刷新用）。 */
    int m_unitId;                           /**< 从站号。 */
    uint16_t m_vbase;                       /**< 本站寄存器在总线虚拟空间的起点。 */
    bool m_mounted;                         /**< 装配完成标志（幂等与诊断）。 */
} HsfServerIoFlowMeter;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 HsfServerIoFlowMeter 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* HsfServerIoFlowMeter_class_init(void);

/**
 * @brief 栈上初始化流量计站（未装配）。
 * @param st 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 */
void HsfServerIoFlowMeter_init(HsfServerIoFlowMeter* st);

/**
 * @brief 堆上创建流量计站。
 * @return 新对象；分配失败返回 NULL。不再使用时 XClassDelete。
 */
HsfServerIoFlowMeter* HsfServerIoFlowMeter_create(void);

/* ==================== 装配 ==================== */

/**
 * @brief 在指定总线上装配本站（映射段 + 组 + 设备 + Hub 订阅 + 采样拍）。
 * @param st 目标站；不能为 NULL。
 * @param bus 所属总线（借用；须已 open）。
 * @param map 总线的映射表（借用；本站往里面登记 22 个寄存器的输入寄存器段）。
 * @param unitId 从站站号（本机实测 5）。
 * @param vbase 本站寄存器在总线虚拟空间的起点（每路总线独立编址）。
 * @param staleMs 数据陈旧判定阈值（毫秒；0=不判陈旧）。
 * @return 全部装配成功返回 true；任一步失败返回 false（已建部分自动回收）。
 * @note 必须在 HsfServerBus_start **之前**调用（那段映射要随映射表一起
 *       提交给 Hub）。必须在 Hub 首轮询**之前**订阅值变化——管道静止时
 *       数值恒定，Hub「变化才发」，订阅晚了就永远拿不到初值。
 */
bool HsfServerIoFlowMeter_mount(HsfServerIoFlowMeter* st, struct HsfServerBus* bus,
                                struct XModbusMapping* map, int unitId,
                                uint16_t vbase, uint32_t staleMs);

/**
 * @brief 拆除本站（幂等；不触碰总线）。
 * @param st 目标站。
 * @note **必须在总线释放之前调用**：本站订阅了 Hub 信号，Hub 先死则
 *       退订会碰悬垂指针。
 */
void HsfServerIoFlowMeter_unmount(HsfServerIoFlowMeter* st);

/* ==================== 查询 ==================== */

/**
 * @brief 取流量计设备（借用；遥测取值/编码走它）。
 * @return 设备指针；未装配返回 NULL。@warning 禁释放。
 */
struct XFlowMeter* HsfServerIoFlowMeter_device(const HsfServerIoFlowMeter* st);

/**
 * @brief 读流量计快照（物理量 + 单位代码 + 报警位 + 质量）。
 * @param st 目标站；可为 NULL。
 * @param outFlow 瞬时流量输出（物理量单位由 outFlowUnit 给出）；可为 NULL。
 * @param outSpeed 瞬时流速输出（m/s）；可为 NULL。
 * @param outForwardTotal 正向累积量输出；可为 NULL。
 * @param outReverseTotal 反向累积量输出；可为 NULL。
 * @param outFlowUnit 瞬时流量单位枚举（XFlowMeterFlowUnit）；可为 NULL。
 * @param outTotalUnit 累积总量单位枚举（XFlowMeterTotalUnit）；可为 NULL。
 * @param outAlarms 报警位（XFlowMeterAlarm 位掩码）；可为 NULL。
 * @param outValid 数据质量输出；可为 NULL。
 * @return 至少有一项有效数据返回 true；未装配或全部无效返回 false。
 * @note 质量取 Hub 块级新鲜度（见文件头说明），不取设备层陈旧计时。
 */
bool HsfServerIoFlowMeter_values(const HsfServerIoFlowMeter* st,
                                 double* outFlow, double* outSpeed,
                                 double* outForwardTotal, double* outReverseTotal,
                                 int* outFlowUnit, int* outTotalUnit,
                                 int* outAlarms, bool* outValid);

/**
 * @brief 本站是否已装配完成。
 */
bool HsfServerIoFlowMeter_isMounted(const HsfServerIoFlowMeter* st);

/**
 * @brief 立即从 Hub 缓存采一拍（把 22 个寄存器重喂进通道组）。
 * @param st 目标站；未装配返回 false。
 * @return 喂进了至少一个字返回 true。
 * @note  正常节拍由 Hub 的 valueChanged 与站内采样定时器驱动；本接口供
 *        「要立即刷新」的场合（如请求快照前强制同步）与单测使用。
 *        组内 feedRole 自带「值未变则静默」，重复调用无副作用。
 */
bool HsfServerIoFlowMeter_sampleNow(HsfServerIoFlowMeter* st);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERIOFLOWMETER_H */
