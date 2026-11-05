/**
 * @file       HsfServerIoTurbidity.h
 * @brief      服务端浊度仪站：从站约束 + 映射段 + 通道组 + XTurbidityMeter 设备。
 * @details    在线浊度仪（上仪 WZT-701A 一类）挂在**路 B** 的 485 总线上，
 *             与电磁流量计同一条线、不同站号（现场：流量计 5、浊度仪 1/3）。
 *             做成独立模块而不是塞进 HsfServerIo，因为它的装配知识与其它站
 *             不同：
 *
 *             [协议口径来自真机手册]
 *               - FC03（**读保持寄存器**）读 0x0000 起 **2 个寄存器**；
 *               - 两个寄存器组成 32 位量，**低字在前（CDAB）**，IEEE754 float32；
 *               - 值即 NTU（两位小数），不再除 100。
 *             见 WZT-701A 说明书 4.3.10.2 的可复现帧：
 *                 发送 01 03 00 00 00 02 C4 0B
 *                 回复 01 03 04 D7 0A 41 1F 92 1D  →  9.99 NTU
 *             [与流量计的差别] 流量计是 FC04 + 高字在前（ABCD）+ float/long
 *             混合；浊度仪是 FC03 + 低字在前（CDAB）+ 纯 float32。**两者不能
 *             共用同一套点装配参数**，故各成一个模块。
 *
 *             [32 位量的拼装归属] 拼装与换算由 **XTurbidityMeter 设备层**完成
 *             （字序/格式/除数都是它的配置项）。本站只负责：
 *                 role 0/1 ↔ 虚拟地址 vbase+0 / vbase+1
 *             两个点**必须恒等标度**（1/1/0、无符号），否则标度运算会破坏
 *             IEEE754 位模式——这是本站装配的硬约束。
 *
 *             [为什么一次喂两个字] 32 位量由相邻两字拼成。若只喂"变化的那个"，
 *             高字到达时低字仍是上一帧值，会解出错误的中间态。故任何一次
 *             Hub 区间信号都触发**整段重喂**（与流量计站同款）。
 *
 *             [质量口径] 与流量计站同款：不用设备层的陈旧计时（水样浊度
 *             长期稳定，feedRole「值未变则静默」，设备的最近一拍会停在很久
 *             以前而被误判陈旧），而取 Hub 块级新鲜度
 *             （XModbusHub_pointValid）——这才是"这一拍确实从链路读回来了"。
 *
 *             [只读设备] 本站无控制面：仪表的标定/偏移量/清洗触发/报警限值
 *             都在仪表面板上做（手册 4.3.3 / 4.3.5 / 4.3.8），对外通讯只
 *             暴露测量值。故不提供任何写 API——设备语义的自然结果。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERIOTURBIDITY_H
#define HSFSERVERIOTURBIDITY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "XObject.h"

struct HsfServerBus;
struct XIoChannelGroup;
struct XTurbidityMeter;
struct XModbusMapping;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(HsfServerIoTurbidity)
XCLASS_DEFINE_EXTEND_END(HsfServerIoTurbidity, XObject)

/**
 * @brief 一台浊度仪站（本模块拥有其内部资源）。
 * @details 成员禁止直接访问，一律经 API 操作。总线为借用：调用方保证
 *          其生命周期覆盖本站，并在总线销毁前先 unmount/析构本站。
 */
typedef struct HsfServerIoTurbidity {
    XObject m_class;                    /**< 第一成员：基类 XObject（信号接收者身份）。 */
    struct HsfServerBus* m_bus;         /**< 所属总线（借用；NULL=未挂）。 */
    struct XIoChannelGroup* m_group;    /**< 本站通道组（自有；点随组 autoDelete）。 */
    struct XTurbidityMeter* m_dev;      /**< 浊度仪设备抽象（自有）。 */
    struct XModbusMapping* m_map;       /**< 装配中的映射表（借用，提交给总线后作废）。 */
    XConnection* m_conn;                /**< Hub valueChanged 订阅（唯一一条）。 */
    XTimerId m_sampleTimer;             /**< 采样定时器（周期拍；静止量刷新用）。 */
    int m_unitId;                       /**< 从站号（仪表"机号"）。 */
    int m_index;                        /**< 台号（0/1；名字与显示顺序用）。 */
    uint16_t m_vbase;                   /**< 本站寄存器在总线虚拟空间的起点。 */
    bool m_mounted;                     /**< 装配完成标志（幂等与诊断）。 */
} HsfServerIoTurbidity;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 HsfServerIoTurbidity 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* HsfServerIoTurbidity_class_init(void);

/**
 * @brief 栈上初始化浊度仪站（未装配）。
 * @param st 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 */
void HsfServerIoTurbidity_init(HsfServerIoTurbidity* st);

/**
 * @brief 堆上创建浊度仪站。
 * @return 新对象；分配失败返回 NULL。@warning 用完须 XClassDelete。
 */
HsfServerIoTurbidity* HsfServerIoTurbidity_create(void);

/* ==================== 装配 ==================== */

/**
 * @brief 装配一台浊度仪站并启动采样。
 * @param st 目标站；不能为 NULL。
 * @param bus 所属总线；借用，不能为 NULL。
 * @param map 映射表（装配期提交给总线）；借用，不能为 NULL。
 * @param unitId 从站号（仪表"机号设置"里那个值）。
 * @param index 台号（0/1；用于显示名与诊断）。
 * @param vbase 本站寄存器在本路虚拟空间的起点（两台不能重叠，各占 2 个）。
 * @param staleMs 陈旧判定阈值（毫秒）。
 * @return 装配成功返回 true。
 *
 * @details [装配顺序] 映射段 → 从站约束 → 通道组 + 2 个点 → 设备 → Hub 订阅
 *          → 采样定时器。**订阅必须在 HsfServerBus_start 之前**：Hub 的
 *          valueChanged 是"变化才发"，静态值在首拍之后不再发——订阅晚于
 *          首拍就拿不到初值（流量计站同一个坑）。
 * @note 幂等：已装配时返回 false（不重复装配）。
 */
bool HsfServerIoTurbidity_mount(HsfServerIoTurbidity* st, struct HsfServerBus* bus,
                                struct XModbusMapping* map, int unitId,
                                int index, uint16_t vbase, uint32_t staleMs);

/**
 * @brief 拆除并释放全部内部资源（幂等；之后可重新 mount）。
 * @param st 目标站；NULL 安全。
 */
void HsfServerIoTurbidity_unmount(HsfServerIoTurbidity* st);

/* ==================== 查询 ==================== */

/**
 * @brief 读当前浊度（NTU）。
 * @param st 目标站；NULL/未装配返回 false。
 * @param outNtu 输出浊度（调用方提供；可为 NULL——此时仅作有效判定）。
 * @return 数据有效返回 true 并写 outNtu；否则 false（outNtu 写 0）。
 */
bool HsfServerIoTurbidity_ntu(const HsfServerIoTurbidity* st, double* outNtu);

/**
 * @brief 读 32 位原始寄存器位模式（诊断/口径反推用）。
 * @param st 目标站。
 * @param outBits 输出位模式（可为 NULL）。
 * @return 有数据返回 true；否则 false（outBits 写 0）。
 */
bool HsfServerIoTurbidity_raw(const HsfServerIoTurbidity* st, uint32_t* outBits);

/**
 * @brief 该台当前是否在线（已读到 且 链路未陈旧）。
 * @param st 目标站；NULL/未装配返回 false。
 */
bool HsfServerIoTurbidity_isOnline(const HsfServerIoTurbidity* st);

/**
 * @brief 查是否装配完成。
 */
bool HsfServerIoTurbidity_isMounted(const HsfServerIoTurbidity* st);

/** @brief 从站号（仪表机号）。 */
int HsfServerIoTurbidity_unitId(const HsfServerIoTurbidity* st);

/** @brief 台号（0/1）。 */
int HsfServerIoTurbidity_index(const HsfServerIoTurbidity* st);

/** @brief 取通道组（借用；诊断用）。@warning 禁释放。 */
struct XIoChannelGroup* HsfServerIoTurbidity_group(const HsfServerIoTurbidity* st);

/** @brief 取设备对象（借用；诊断/装实验旋钮用）。@warning 禁释放。 */
struct XTurbidityMeter* HsfServerIoTurbidity_device(const HsfServerIoTurbidity* st);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERIOTURBIDITY_H */
