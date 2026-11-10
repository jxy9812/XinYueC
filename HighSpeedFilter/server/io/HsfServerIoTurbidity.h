/**
 * @file       HsfServerIoTurbidity.h
 * @brief      服务端浊度仪站：从站约束 + 映射段 + 通道组 + XTurbidityMeter 设备。
 * @details    在线浊度仪（上仪 WZT-701A 一类）挂在**路 B** 的 485 总线上，
 *             与电磁流量计同一条线、不同站号（现场：流量计 5、浊度仪 1/3）。
 *             做成独立模块而不是塞进 HsfServerIo，因为它的装配知识与其它站
 *             不同：
 *
 *             [协议口径来自真机手册 + 2026-11-09 板端实测]
 *               - FC03（**读保持寄存器**）读 0x0000 起每台各自的寄存器数
 *                 （站 1 WZT-701A=×2 仅浊度；第二台 ZHEYI-5100ZDY=×4 浊度+温度
 *                 同刻，实测文档 §5.2/§5.4）；
 *               - 浊度 32 位量按**逐台配置的字序**拼装：站 1=CDAB（低字在前，
 *                 手册 4.3.10.2）、第二台=DCBA（整字节反序=小端 float32，
 *                 实测文档 §5.3——旧口径按 CDAB 解该表会得出 1.1e-5 的假数
 *                 且不报错）。字序/寄存器数都是 mount 参数（来自
 *                 HSF_TURBIDITYx_WORD_ORDER / HSF_TURBIDITYx_REG_COUNT）；
 *               - 值即 NTU（两位小数），不再除 100。
 *             见 WZT-701A 说明书 4.3.10.2 的可复现帧：
 *                 发送 01 03 00 00 00 02 C4 0B
 *                 回复 01 03 04 D7 0A 41 1F 92 1D  →  9.99 NTU
 *             [与流量计的差别] 流量计是 FC04 + 高字在前（ABCD）+ float/long
 *             混合；浊度仪是 FC03 + 逐台字序 + 纯 float32。**两者不能
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
    /** @brief 陈旧阈值（毫秒；装配/应用时按**当前采集周期**算出的有效值）。
     *  @details [为什么必须记住装配值 2026-11-10 现场实锅] 质量判定原先直接用
     *           编译期宏（5000）。采集周期一旦被用户调大，就会出现「阈值 < 周期
     *           ⇒ 两拍之间必判陈旧 ⇒ 界面 "--"/"数据过期"，下一拍又恢复」的
     *           **来回抖动**（温湿度站 10000ms 周期上必然发生）。
     *           口径：有效阈值 = max(宏基线, 采集周期 × 3)，由 HsfServerIo.c
     *           的 hsfIoStaleMs 算出并随 mount/apply 传入本站。 */
    uint32_t m_staleMs;
    int m_regCount;                     /**< 本站一条帧的寄存器数（2=仅浊度；4=浊度+温度）。 */
    bool m_tempLogged;                  /**< 温度首拍是否已记日志（一次性事件，防刷屏）。 */
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
 * @param vbase 本站寄存器在本路虚拟空间的起点（两台不能重叠，逐台按
 *              各自 HSF_TURBIDITYx_REG_COUNT 顺排）。
 * @param staleMs 陈旧判定阈值（毫秒）。
 * @param regCount 本站一条帧读的寄存器数（浊度台=2 仅浊度；第二台=4 浊度+温度
 *                 同刻，HSF_TURBIDITYx_REG_COUNT）。
 * @param wordOrder 32 位量字序（XTurbidityMeterWordOrder 枚举值：0=CDAB、
 *                   1=ABCD、2=DCBA 整字节反序；逐台配 HSF_TURBIDITYx_WORD_ORDER
 *                   ——2026-11-09 实测两台表字节序不同，站 1=CDAB、第二台=DCBA）。
 * @param txQuietMs 发帧前总线静默期（毫秒；0=不设闸；配
 *                  HSF_TURBIDITYx_TX_QUIET_MS——站 1 在流量计 49 字节长帧后
 *                  需 ≥200ms 恢复期，ttyS4 实测 §8；经 Hub 从站约束
 *                  m_preTxQuietMs 生效）。
 * @return 装配成功返回 true。
 *
 * @details [装配顺序] 映射段 → 从站约束 → 通道组 + regCount 个点 → 设备
 *          （先配字序）→ Hub 订阅 → 采样定时器。**订阅必须在
 *          HsfServerBus_start 之前**：Hub 的 valueChanged 是"变化才发"，
 *          静态值在首拍之后不再发——订阅晚于首拍就拿不到初值（流量计站
 *          同一个坑）。
 * @note 幂等：已装配时返回 false（不重复装配）。
 * @note regCount 合法域 [2,4]；越界按装配失败处理（映射段是按它登记的，
 *       乱值会静默改写从站约束，宁可早失败）。
 */
bool HsfServerIoTurbidity_mount(HsfServerIoTurbidity* st, struct HsfServerBus* bus,
                                struct XModbusMapping* map, int unitId,
                                int index, uint16_t vbase, uint32_t staleMs,
                                int regCount, int wordOrder, uint32_t txQuietMs);

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
 * @brief 读当前水温（℃；仅带温度的机型，当前=站 2 ZHEYI-5100ZDY）。
 * @param st 目标站；NULL/未装配/机型无温度返回 false（outC 写 0）。
 * @param outC 输出温度（调用方提供；可为 NULL——此时仅作有无判定）。
 * @return 该站新鲜（Hub 块级口径，与 ntu 同一闸）且设备解出过温度返回
 *         true 并写 outC。
 * @note [与浊度同刻] 站 2 按 0x0000×4 一帧读浊度+温度（实测文档 §5.2），
 *       两者的缓存来自同一帧回填。
 * @note [只进设备层/日志] 温度**不进**遥测推送帧与历史行（0x00010009
 *       形状与列集是铁律）；本接口供日志/诊断/将来按需扩展用。
 */
bool HsfServerIoTurbidity_tempC(const HsfServerIoTurbidity* st, double* outC);

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
