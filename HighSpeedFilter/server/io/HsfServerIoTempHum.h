
/**
 * @file       HsfServerIoTempHum.h
 * @brief      服务端温湿度站：从站约束 + 映射段 + 通道组 + 设备抽象。
 * @details    温湿度模块（中盛 ZS-SHT30-1T-1H-5-36V-RS485）与变频器
 *             **共用同一条 485 总线**，只是从站号不同（4 vs 2）。做成独立
 *             模块而不是塞进 HsfServerIo，是因为它的装配知识与变频器完全
 *             不同（见 docs/温湿度采集模块_探测与验证_20261104.md）：
 *
 *             [从站协议约束]（实测）
 *               - 取数只用 FC04 读输入寄存器 0000H × 2（RTT p50 5.8ms）；
 *               - **块读上限 20**（非 Modbus 通用 125）⇒ 必须经
 *                 XModbusHub_setUnitLimits 声明，否则 Hub 会规划超限读帧；
 *               - 不支持 FC10（写多寄存器）⇒ singleWriteOnly；
 *               - 非法请求静默丢弃（无 0x80 异常码）⇒ 失败只能靠超时判定，
 *                 故本站只读不写，映射段也只登记这一段输入寄存器。
 *
 *             [为什么不用 XModbusLink] 通用链路把 Hub 的原生值直通喂给通道
 *             点，而本模块的温度有**折返编码**：`raw >= 10000` 表示负温，
 *             真值 = -(raw-10000)×0.1（例 10250 → -25.0℃）。折返不是线性
 *             标度，XIoAnalogPoint 的乘加表达不了，故本站自己订阅 Hub 的
 *             valueChanged，解码后再喂通道组——多一层订阅换来编码正确。
 *
 *             [工程值口径] 定点「0.1 单位」：温度 219 = 21.9℃、湿度
 *             645 = 64.5%RH、负温 -250 = -25.0℃。通道层标度与设备层
 *             第二级标度都恒为 1/1/0 直通；要换分辨率只改 setScale。
 *
 *             线程模型：单线程事件循环；全部 API 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERIOTEMPHUM_H
#define HSFSERVERIOTEMPHUM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

#include "XObject.h"

struct HsfServerBus;
struct XIoChannelGroup;
struct XTempHumTransmitter;
struct XModbusMapping;

/** @brief 温度通道在组内的 role（固定：与站点内部地址偏移一一对应）。 */
#define HSF_TEMPHUM_ROLE_TEMP   0u
/** @brief 湿度通道在组内的 role。 */
#define HSF_TEMPHUM_ROLE_HUM    1u
/** @brief 本站寄存器数量（温度 + 湿度各一字）。 */
#define HSF_TEMPHUM_REG_COUNT   2u

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(HsfServerIoTempHum)
XCLASS_DEFINE_EXTEND_END(HsfServerIoTempHum, XObject)

/**
 * @brief 一路温湿度站（每路总线最多一个；本模块拥有其内部资源）。
 * @details 成员禁止直接访问，一律经 API 操作。总线为借用：调用方保证
 *          其生命周期覆盖本站，并在总线销毁前先 unmount/析构本站。
 */
typedef struct HsfServerIoTempHum {
    XObject m_class;                        /**< 第一成员：基类 XObject（信号接收者身份）。 */
    struct HsfServerBus* m_bus;             /**< 所属总线（借用；NULL=未挂）。 */
    struct XIoChannelGroup* m_group;        /**< 本站通道组（自有；点随组 autoDelete）。 */
    struct XTempHumTransmitter* m_dev;      /**< 温湿度设备抽象（自有）。 */
    struct XModbusMapping* m_map;           /**< 装配中的映射表（借用，提交给总线后作废）。 */
    XConnection* m_conn;                    /**< Hub valueChanged 订阅（唯一一条）。 */
    XTimerId m_sampleTimer;                 /**< 采样定时器（周期拍；见实现说明）。 */
    int m_unitId;                           /**< 从站号。 */
    uint16_t m_vbase;                       /**< 本站寄存器在总线虚拟空间的起点。 */
    bool m_mounted;                         /**< 装配完成标志（幂等与诊断）。 */
} HsfServerIoTempHum;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 HsfServerIoTempHum 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* HsfServerIoTempHum_class_init(void);

/**
 * @brief 栈上初始化温湿度站（未装配）。
 * @param st 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 */
void HsfServerIoTempHum_init(HsfServerIoTempHum* st);

/**
 * @brief 堆上创建温湿度站。
 * @return 新对象；分配失败返回 NULL。不再使用时 XClassDelete。
 */
HsfServerIoTempHum* HsfServerIoTempHum_create(void);

/* ==================== 装配 ==================== */

/**
 * @brief 在指定总线上装配本站（映射段 + 组 + 设备 + Hub 订阅）。
 * @param st 目标站；不能为 NULL。
 * @param bus 所属总线（借用；须已 open）。
 * @param map 总线的映射表（借用；本站往里面登记一段输入寄存器）。
 * @param unitId 从站站号（实测 4）。
 * @param vbase 本站寄存器在总线虚拟空间的起点（每路总线独立编址）。
 * @param staleMs 数据陈旧判定阈值（毫秒；0=不判陈旧）。
 * @return 全部装配成功返回 true；任一步失败返回 false（已建部分自动回收）。
 * @note 必须在 HsfServerBus_start **之前**调用（那段映射要随映射表一起
 *       提交给 Hub）。本站的量程/分辨率走设备层第二级标度，默认直通。
 */
bool HsfServerIoTempHum_mount(HsfServerIoTempHum* st, struct HsfServerBus* bus,
                              struct XModbusMapping* map, int unitId,
                              uint16_t vbase, uint32_t staleMs);

/**
 * @brief 拆除本站（幂等；不触碰总线）。
 * @param st 目标站。
 * @note **必须在总线释放之前调用**：本站订阅了 Hub 信号，Hub 先死则
 *       退订会碰悬垂指针。
 */
void HsfServerIoTempHum_unmount(HsfServerIoTempHum* st);

/* ==================== 查询 ==================== */

/**
 * @brief 取温湿度设备（借用；遥测取值/编码走它）。
 * @return 设备指针；未装配返回 NULL。@warning 禁释放。
 */
struct XTempHumTransmitter* HsfServerIoTempHum_device(const HsfServerIoTempHum* st);

/**
 * @brief 读温湿度（0.1 单位定点）。
 * @param st 目标站；可为 NULL。
 * @param outTempC10 温度输出（0.1℃，可为负）；可为 NULL。
 * @param outHumPct10 湿度输出（0.1%RH）；可为 NULL。
 * @param outTempValid/outHumValid 质量输出（可为 NULL）。
 * @return 至少一项有有效数据返回 true；未装配或两路均无效返回 false。
 */
bool HsfServerIoTempHum_values(const HsfServerIoTempHum* st,
                               int32_t* outTempC10, int32_t* outHumPct10,
                               bool* outTempValid, bool* outHumValid);

/**
 * @brief 本站是否已装配完成。
 */
bool HsfServerIoTempHum_isMounted(const HsfServerIoTempHum* st);

/* ==================== 纯解码助手（协议口径，供测试与诊断复用） ==================== */

/**
 * @brief 温度原生字 → 有符号 0.1℃ 定点（处理 `raw>=10000` 的负温折返）。
 * @param raw 输入寄存器 0000H 原值。
 * @return 有符号 0.1℃ 定点（10250 → -250 即 -25.0℃；219 → 219 即 21.9℃）。
 * @note 手册 2.1 的唯一负数表达方式；不实现则 -1℃ 会解成 999.9℃。
 */
int32_t HsfServerIoTempHum_decodeTemp(uint16_t raw);

/**
 * @brief 湿度原生字 → 0.1%RH 定点（无负数分支）。
 * @param raw 输入寄存器 0001H 原值。
 * @return 0.1%RH 定点（645 → 64.5%RH）。
 */
int32_t HsfServerIoTempHum_decodeHum(uint16_t raw);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERIOTEMPHUM_H */
