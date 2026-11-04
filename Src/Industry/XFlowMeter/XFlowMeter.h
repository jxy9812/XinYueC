
/**
 * @file       XFlowMeter.h
 * @brief      电磁流量计设备抽象公开 API。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场电磁流量计，如上自仪
 *             LD 系列 / L_mag B 系列转换器，Modbus-RTU 输出）。设计完全对齐
 *             XTempHumTransmitter / XDiffPressureGauge（2026-11-01 通道组
 *             重构形态）：纯业务层设备，只依赖通道接口族（通道组登记的点，
 *             XIoPoint* 直传，信号经 XIoChannelGroup 分发按 role 过滤），
 *             **不关心数据来自何处**——Modbus RTU / Modbus TCP / CAN / 任何
 *             未来传输都经 Industry/XChannel 的适配器在装配期注入（attach，
 *             借用不拥有）。本类永不 include 任何具体传输头文件。
 *
 *             [多字字段：32 位量跨两个 16 位寄存器]
 *             通道层的点是 16 位原生值单位，而本设备的核心量（瞬时流量、
 *             瞬时流速、流量百分比、电导比、正/反向累积量的整数与小数部分）
 *             都是 32 位（IEEE754 float 或 32 位整型），在总线上占**连续两个
 *             寄存器**。本类按「高字在前、大端拼接」把它们合成 32 位：
 *
 *                 bits = ((uint32_t)hiRaw << 16) | loRaw;
 *
 *             这是**协议知识**，故落在设备层而不在点层（点只是数据容器，
 *             不知道自己是不是某个 32 位量的一半）。换一条总线时，只要那条
 *             链路仍按同样的 role 顺序把两个字喂进来，本类一行不改。
 *
 *             [一致性：为什么读链路而不是读点缓存]
 *             一帧应答由 Hub 整块回填、整块发信号，故**同一帧内的所有寄存器
 *             是同时刻快照**。据此本类的多字字段**直接经 XIoLink_readRole_base
 *             读取两个字的原始值**再拼接——保证拼出来的 32 位来自同一帧，
 *             绝不会出现「新高位 + 旧低位」的中间态。单字字段（单位/报警）
 *             走组缓存即可。
 *             （若逐字读点缓存，块回填是地址升序，收到高字事件时低字可能还是
 *              上一帧的值——这正是必须避开的那一拍。）
 *
 *             [值类型] 全部物理量为 double：累积量可到千万级，定点 int32
 *             装不下；且与 XVfdDevice 的频率/电压等量口径一致。
 *
 *             [质量口径] 与 XTempHumTransmitter 完全一致：已收到至少一拍
 *             && 未陈旧（staleMs，0=不判陈旧），巡检定时器翻转沿发
 *             qualityChanged。
 *
 *             [只读设备] 本类无控制面：不提供写参数 / 累积量清零。设备语义
 *             的自然结果（对齐 XDiffPressureGauge）。若将来需要写参数
 *             （0x003F 密码门 + 0x0047 清零魔数），按 XVfdDevice 的参数
 *             读写形态另开一组 API，不在本类里混进链路知识。
 *
 *             设备对象为句柄性质：禁止拷贝/移动语义；对象可能在自身信号
 *             调用栈上被删除时，调用方必须使用 XObject_deleteLater。
 */
#ifndef XFLOWMETER_H
#define XFLOWMETER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#include "XChannel/XIoChannelGroup.h"

#if XINDUSTRY_ON && XFLOW_METER_ON

/**
 * @brief 语义槽号（role）。
 * @details 与 Modbus 变量区 0x1010~0x1025 共 22 个寄存器一一对应，按下表
 *          顺序连续编号（role 即组内下标，故必须 0 基连续）：
 *
 *          | role  | 寄存器  | 格式        | 内容                     |
 *          |-------|---------|-------------|--------------------------|
 *          | 0/1   | 0x1010  | Float  HI/LO| 瞬时流量                 |
 *          | 2/3   | 0x1012  | Float  HI/LO| 瞬时流速                 |
 *          | 4/5   | 0x1014  | Float  HI/LO| 流量百分比               |
 *          | 6/7   | 0x1016  | Float  HI/LO| 流体电导比               |
 *          | 8/9   | 0x1018  | Long   HI/LO| 正向累积数值整数部分     |
 *          | 10/11 | 0x101A  | Float  HI/LO| 正向累积数值小数部分     |
 *          | 12/13 | 0x101C  | Long   HI/LO| 反向累积数值整数部分     |
 *          | 14/15 | 0x101E  | Float  HI/LO| 反向累积数值小数部分     |
 *          | 16    | 0x1020  | U16         | 瞬时流量单位（见下枚举） |
 *          | 17    | 0x1021  | U16         | 累积总量单位（见下枚举） |
 *          | 18    | 0x1022  | U16         | 上限报警（1=报警）       |
 *          | 19    | 0x1023  | U16         | 下限报警                 |
 *          | 20    | 0x1024  | U16         | 空管报警                 |
 *          | 21    | 0x1025  | U16         | 系统报警                 |
 *
 *          装配层按本枚举绑定地址；**两个字的 role 必须相邻且高字在前**，
 *          本类据此按 (role, role+1) 配成一对读取。
 */
typedef enum XFlowMeterRole {
    XFlowMeterRole_InstantFlowHi = 0,  /**< 0x1010 瞬时流量 高字。 */
    XFlowMeterRole_InstantFlowLo = 1,  /**< 0x1011 瞬时流量 低字。 */
    XFlowMeterRole_InstantSpeedHi = 2, /**< 0x1012 瞬时流速 高字。 */
    XFlowMeterRole_InstantSpeedLo = 3, /**< 0x1013 瞬时流速 低字。 */
    XFlowMeterRole_FlowPercentHi = 4,  /**< 0x1014 流量百分比 高字。 */
    XFlowMeterRole_FlowPercentLo = 5,  /**< 0x1015 流量百分比 低字。 */
    XFlowMeterRole_ConductivityHi = 6, /**< 0x1016 流体电导比 高字。 */
    XFlowMeterRole_ConductivityLo = 7, /**< 0x1017 流体电导比 低字。 */
    XFlowMeterRole_ForwardIntHi = 8,   /**< 0x1018 正向累积整数 高字。 */
    XFlowMeterRole_ForwardIntLo = 9,   /**< 0x1019 正向累积整数 低字。 */
    XFlowMeterRole_ForwardFracHi = 10, /**< 0x101A 正向累积小数 高字。 */
    XFlowMeterRole_ForwardFracLo = 11, /**< 0x101B 正向累积小数 低字。 */
    XFlowMeterRole_ReverseIntHi = 12,  /**< 0x101C 反向累积整数 高字。 */
    XFlowMeterRole_ReverseIntLo = 13,  /**< 0x101D 反向累积整数 低字。 */
    XFlowMeterRole_ReverseFracHi = 14, /**< 0x101E 反向累积小数 高字。 */
    XFlowMeterRole_ReverseFracLo = 15, /**< 0x101F 反向累积小数 低字。 */
    XFlowMeterRole_InstantFlowUnit = 16, /**< 0x1020 瞬时流量单位（XFlowMeterFlowUnit）。 */
    XFlowMeterRole_TotalUnit = 17,     /**< 0x1021 累积总量单位（XFlowMeterTotalUnit）。 */
    XFlowMeterRole_UpperAlarm = 18,    /**< 0x1022 上限报警（1=报警）。 */
    XFlowMeterRole_LowerAlarm = 19,    /**< 0x1023 下限报警。 */
    XFlowMeterRole_EmptyAlarm = 20,    /**< 0x1024 空管报警。 */
    XFlowMeterRole_SystemAlarm = 21,   /**< 0x1025 系统报警。 */
    XFlowMeterRole_Count = 22          /**< 槽位总数（装配用上界，非真实槽）。 */
} XFlowMeterRole;

/**
 * @brief 报文中的物理量（供统一查询/信号用；一量对应两个 role）。
 */
typedef enum XFlowMeterQuantity {
    XFlowMeterQuantity_InstantFlow = 0, /**< 瞬时流量。 */
    XFlowMeterQuantity_InstantSpeed,    /**< 瞬时流速。 */
    XFlowMeterQuantity_FlowPercent,     /**< 流量百分比。 */
    XFlowMeterQuantity_Conductivity,    /**< 流体电导比。 */
    XFlowMeterQuantity_ForwardTotal,    /**< 正向累积量（整数部分 + 小数部分）。 */
    XFlowMeterQuantity_ReverseTotal,    /**< 反向累积量（整数部分 + 小数部分）。 */
    XFlowMeterQuantity_Count            /**< 量总数（非真实量）。 */
} XFlowMeterQuantity;

/**
 * @brief 报警位（四类告警各占一位）。
 */
typedef enum XFlowMeterAlarm {
    XFlowMeterAlarm_None = 0,          /**< 无报警。 */
    XFlowMeterAlarm_Upper = 1 << 0,    /**< 上限报警。 */
    XFlowMeterAlarm_Lower = 1 << 1,    /**< 下限报警。 */
    XFlowMeterAlarm_EmptyPipe = 1 << 2,/**< 空管报警。 */
    XFlowMeterAlarm_System = 1 << 3    /**< 系统报警。 */
} XFlowMeterAlarm;

/**
 * @brief 瞬时流量单位（报文 0x1020 代码表）。
 * @note 代码来自协议表 3-1；0x1020 是**仪表侧可改**的，故必须跟随数据读上来
 *       做标度，不能用编译期常量假设。
 */
typedef enum XFlowMeterFlowUnit {
    XFlowMeterFlowUnit_LPS = 0,  /**< L/S。 */
    XFlowMeterFlowUnit_LPM = 1,  /**< L/M。 */
    XFlowMeterFlowUnit_LPH = 2,  /**< L/H。 */
    XFlowMeterFlowUnit_M3S = 3,  /**< M3/S。 */
    XFlowMeterFlowUnit_M3M = 4,  /**< M3/M。 */
    XFlowMeterFlowUnit_M3H = 5,  /**< M3/H。 */
    XFlowMeterFlowUnit_TS = 6,   /**< T/S。 */
    XFlowMeterFlowUnit_TM = 7,   /**< T/M。 */
    XFlowMeterFlowUnit_TH = 8,   /**< T/H。 */
    XFlowMeterFlowUnit_GPS = 9,  /**< GPS（加仑/秒）。 */
    XFlowMeterFlowUnit_GPM = 10, /**< GPM（加仑/分）。 */
    XFlowMeterFlowUnit_GPH = 11, /**< GPH（加仑/时）。 */
    XFlowMeterFlowUnit_Unknown = 0xFF /**< 未知代码（保留位/超界）。 */
} XFlowMeterFlowUnit;

/**
 * @brief 累积总量单位（报文 0x1021 代码表）。
 * @note B 型及 511 型转换器为 0~3；C 型转换器为 0~11（含精度档）。代码 4~11
 *       在 C 型上表示 M3/T/USG 的不同小数位，本枚举按 B 型口径给出基础单位，
 *       精度档由调用方按 XFlowMeter_totalUnitCode 自行解释。
 */
typedef enum XFlowMeterTotalUnit {
    XFlowMeterTotalUnit_L = 0,   /**< L。 */
    XFlowMeterTotalUnit_M3 = 1,  /**< M3。 */
    XFlowMeterTotalUnit_T = 2,   /**< T。 */
    XFlowMeterTotalUnit_USG = 3, /**< USG（美制加仑）。 */
    XFlowMeterTotalUnit_Unknown = 0xFF /**< 未知代码。 */
} XFlowMeterTotalUnit;

/**
 * @brief 电磁流量计错误码。
 */
typedef enum XFlowMeterError {
    XFlowMeterError_None = 0,          /**< 无错误。 */
    XFlowMeterError_NotAttached,       /**< 输入未挂接，操作被拒绝。 */
    XFlowMeterError_Internal           /**< 内部资源失败（如质量巡检定时器启动失败）。 */
} XFlowMeterError;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XFlowMeter)
XCLASS_DEFINE_EXTEND_END(XFlowMeter, XObject)

/**
 * @brief XFlowMeter 电磁流量计。
 * @details 成员禁止直接访问，一律经 API 操作。通道组与输入点均为借用，
 *          不拥有：调用方保证其生命周期覆盖流量计，并在其销毁前 detach
 *          或先删流量计。
 */
typedef struct XFlowMeter {
    XObject m_class;                    /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    XIoChannelGroup* m_group;           /**< 通道组（借用，可为 NULL=未挂接）；数据与信号经组分发。 */
    XIoPoint* m_points[XFlowMeterRole_Count]; /**< 点表（role 作下标；NULL=该 role 未登记）。 */
    bool m_pairBound[XFlowMeterQuantity_Count]; /**< 该量的两个 role 是否都已绑定（可解 32 位）。 */
    double m_values[XFlowMeterQuantity_Count];  /**< 最近物理量值[量]（double；未收到为 0）。 */
    uint16_t m_flowUnitCode;            /**< 瞬时流量单位代码（原样保留，便于诊断）。 */
    uint16_t m_totalUnitCode;           /**< 累积总量单位代码。 */
    uint8_t m_alarmBits;                /**< 报警位（XFlowMeterAlarm 位掩码）。 */
    bool m_received;                    /**< 是否收到过至少一拍。 */
    uint64_t m_lastChangeMs;            /**< 最近一拍时刻（单调毫秒；XDateTime 口径）。 */
    uint32_t m_staleMs;                 /**< 陈旧判定阈值（毫秒）；0=不判陈旧。默认 5000。 */
    bool m_signalAttached;              /**< 组 inputChanged 是否已订阅。 */
    XTimerId m_qualityTimer;            /**< 质量巡检定时器 id；无效时为 XTIMER_INVALID_ID。 */
    bool m_quality;                     /**< 上次发布的质量（翻转沿才发信号）。 */
    uint8_t m_publishedAlarms;          /**< 上次发布的报警位（翻转沿才发信号）。 */
} XFlowMeter;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XFlowMeter 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XFlowMeter_class_init(void);

/**
 * @brief 栈上初始化电磁流量计。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；初始未挂接、陈旧阈值 5000ms。
 */
void XFlowMeter_init(XFlowMeter* obj);

/**
 * @brief 堆上创建电磁流量计。
 * @return 新对象；内存分配失败返回 NULL。不再使用时必须 XClassDelete；
 *         对象可能仍被信号链引用时改用 XObject_deleteLater。
 */
XFlowMeter* XFlowMeter_create(void);

/* ==================== 装配（借用注入，解耦点） ==================== */

/**
 * @brief 挂接通道组（本设备按 role 从组内取全部点）。
 * @param self 目标流量计；不能为 NULL。
 * @param group 通道组（已装配好本设备的 22 个 role）；借用，不能为 NULL。
 * @return 订阅成功且至少绑定一个量返回 true；参数非法或订阅失败返回 false。
 * @note 重复挂接先自动解挂旧组。挂接成功后立即同步读一次播种快照
 *       （有数据即发 valueChanged/qualityChanged/alarmChanged）。
 * @note 多字量的 role 必须成对存在（hi 与 hi+1 都在组内）才可解；
 *       缺一个则该量视为未绑定，查询恒返回 false——装配错误应尽早暴露。
 */
bool XFlowMeter_attach(XFlowMeter* self, XIoChannelGroup* group);

/**
 * @brief 解挂全部输入并断开组信号订阅。
 * @param self 目标流量计；不能为 NULL。
 * @return 原本已挂接返回 true；未挂接返回 false。
 * @note 解挂后质量转无效（发 qualityChanged(false)）、报警清零
 *       （发 alarmChanged(0)），缓存物理量保留。
 */
bool XFlowMeter_detach(XFlowMeter* self);

/* ==================== 配置 ==================== */

/**
 * @brief 设置陈旧判定阈值。
 * @param self 目标流量计；不能为 NULL。
 * @param staleMs 阈值毫秒；0=不判陈旧（数据收到过即恒有效）。
 * @return 设置成功返回 true。
 * @note 立即生效：下次质量评估按新阈值（质量翻转经 qualityChanged 发布）。
 */
bool XFlowMeter_setStaleMs(XFlowMeter* self, uint32_t staleMs);

/**
 * @brief 读取陈旧判定阈值。
 * @param self 目标流量计；传入 NULL 返回 0。
 * @return 当前阈值毫秒；0=不判陈旧。
 */
uint32_t XFlowMeter_staleMs(const XFlowMeter* self);

/* ==================== 查询 ==================== */

/**
 * @brief 读一个物理量（double）。
 * @param self 目标流量计；可为 NULL。
 * @param quantity 量（XFlowMeterQuantity）。
 * @param outValue 输出物理量（调用方提供；可为 NULL——此时仅作有效判定）。
 * @return 数据有效（该量的两个字都已绑定、已收到且未陈旧）返回 true 并写
 *         outValue；否则返回 false（outValue 被写 0）。
 * @note 只读查询，无副作用、不发信号。
 */
bool XFlowMeter_value(const XFlowMeter* self, XFlowMeterQuantity quantity,
                      double* outValue);

/**
 * @brief 查整体数据质量（等价 value 的返回值口径，取「已收拍且未陈旧」）。
 * @param self 目标流量计；可为 NULL。
 * @return 有效返回 true。
 */
bool XFlowMeter_isValid(const XFlowMeter* self);

/**
 * @brief 查最近一拍时刻。
 * @param self 目标流量计；可为 NULL。
 * @param outMs 输出单调毫秒（调用方提供；可为 NULL）。
 * @return 收到过数据返回 true；从未收到返回 false（outMs 写 0）。
 */
bool XFlowMeter_lastUpdateMs(const XFlowMeter* self, uint64_t* outMs);

/**
 * @brief 读瞬时流量单位（已翻译为枚举）。
 * @param self 目标流量计；可为 NULL。
 * @return 单位枚举；未绑定/未知代码返回 XFlowMeterFlowUnit_Unknown。
 */
XFlowMeterFlowUnit XFlowMeter_flowUnit(const XFlowMeter* self);

/**
 * @brief 读累积总量单位（已翻译为枚举）。
 * @param self 目标流量计；可为 NULL。
 * @return 单位枚举；未绑定/未知代码返回 XFlowMeterTotalUnit_Unknown。
 */
XFlowMeterTotalUnit XFlowMeter_totalUnit(const XFlowMeter* self);

/**
 * @brief 读原始单位代码（诊断/未知单位时用）。
 * @param self 目标流量计；可为 NULL。
 * @param outFlowCode 输出瞬时流量单位代码（可为 NULL）。
 * @param outTotalCode 输出累积总量单位代码（可为 NULL）。
 * @return 无。
 */
void XFlowMeter_unitCodes(const XFlowMeter* self, uint16_t* outFlowCode,
                          uint16_t* outTotalCode);

/**
 * @brief 读报警位。
 * @param self 目标流量计；可为 NULL。
 * @return XFlowMeterAlarm 位掩码（0=无报警）。
 */
uint8_t XFlowMeter_alarms(const XFlowMeter* self);

/* ==================== 信号 ==================== */

/**
 * @brief valueChanged 信号：某物理量更新（含挂接播种）。
 * @param self 发射信号的流量计；经 XSignal() 宏取地址连接，禁止直调。
 * @param quantity 量（XFlowMeterQuantity）；**= XFlowMeterQuantity_Count 时
 *                 表示「整帧有量变化」**（一帧一次，不逐量重复发射）——
 *                 接收方据此按需经值查询 API 取全部量，避免一帧 22 个寄存器
 *                 触发数十条信号把参数池打满。
 * @param value 物理量；整帧通知时为 0（请用查询 API 取值）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XFlowMeter_valueChanged_signal(XFlowMeter* self, int quantity, double value);

/**
 * @brief qualityChanged 信号：整体质量翻转（有效 ⇄ 无效/陈旧）。
 * @param self 发射信号的流量计；经 XSignal() 宏取地址连接，禁止直调。
 * @param valid 翻转后的质量。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 质量评估在数据拍与巡检定时器两处触发，仅翻转沿发射。
 */
void* XFlowMeter_qualityChanged_signal(XFlowMeter* self, bool valid);

/**
 * @brief alarmChanged 信号：报警位变化（按位翻转沿发射，值未变不发）。
 * @param self 发射信号的流量计；经 XSignal() 宏取地址连接，禁止直调。
 * @param bits 变化后的 XFlowMeterAlarm 位掩码。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XFlowMeter_alarmChanged_signal(XFlowMeter* self, int bits);

/**
 * @brief errorOccurred 信号：流量计发生错误。
 * @param self 发射信号的流量计；经 XSignal() 宏取地址连接，禁止直调。
 * @param error 错误码（XFlowMeterError 枚举值）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XFlowMeter_errorOccurred_signal(XFlowMeter* self, int error);

#endif /* XINDUSTRY_ON && XFLOW_METER_ON */

#ifdef __cplusplus
}
#endif
#endif /* XFLOWMETER_H */
