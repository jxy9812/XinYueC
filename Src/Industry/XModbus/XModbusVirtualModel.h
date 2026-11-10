/**
 * @file       XModbusVirtualModel.h
 * @brief      虚拟 Modbus 设备模型：只实现"寄存器读写字"，不含任何业务语义。
 * @details    [分层] 模型层只回答两个问题：
 *               · 读这个区/地址/数量 ⇒ 返回什么原始字；
 *               · 写这个区/地址 ⇒ 模型内部状态怎么变。
 *             **不解释功能码语义、不做单位换算、不解码状态字**——那些是真实
 *             设备类（XVfdM512 / XFlowMeter / XTempHum …）的职责，只有一份。
 *             模型只负责"电信号"。
 *
 *             [为什么是函数指针表而不是一堆 if] 现场有 6 类从站（变频器、
 *             数字量、模拟量、温湿度、流量计），每类的寄存器地图完全不同。
 *             用统一的 read/write 两个虚函数 + 各自私有状态，新增一类设备
 *             只需再写一个模型实现，主站侧（XModbusVirtualClient）零改动。
 *
 *             [与 Python virtualdev.py 的对应]
 *               VirtualVfd      ↔ XVirtualModelVfd
 *               VirtualDigital  ↔ XVirtualModelDigital
 *               VirtualAnalog   ↔ XVirtualModelAnalog
 *               VirtualTempHum  ↔ XVirtualModelTempHum
 *                   （流量计在 Python 侧无模型；C 侧一并提供）
 *
 *             线程模型：单线程，与框架一致（模型的 read/write 会被主站的
 *             同步应答路径直接调用，不得阻塞）。
 */
#ifndef XMODBUSVIRTUALMODEL_H
#define XMODBUSVIRTUALMODEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XModbusPdu.h"
#include <stdbool.h>
#include <stdint.h>

#if XMODBUS_ON && XMODBUS_CLIENT_ON

/**
 * @brief 模型读结果。
 */
typedef enum XModbusVirtualResult {
    XModbusVirtual_Ok = 0,          /**< 正常应答。 */
    XModbusVirtual_IllegalFunction, /**< 不支持的功能码（EXC01）。 */
    XModbusVirtual_IllegalAddress,  /**< 地址越界/未分配（EXC02）。 */
    XModbusVirtual_IllegalValue     /**< 数量/取值非法（EXC03）。 */
} XModbusVirtualResult;

/**
 * @brief 设备模型虚表。
 * @details 两个函数都必须可重入且无阻塞；返回 Illegal* 时由客户端翻译成
 *          标准异常应答（功能码 | 0x80，异常码 01/02/03）。
 */
typedef struct XModbusVirtualModelVtable {
    /**
     * @brief 读。
     * @param self 模型对象。
     * @param fc 功能码（**原始值**，可能含 0x80 异常位；实现应先屏蔽）。
     * @param addr 起始地址。
     * @param count 数量（位区=位数，寄存器区=字数）。
     * @param out 输出缓冲（寄存器区=每字一个 uint16_t；位区=已压好的位图，
     *            按 Modbus 口径低字节在前、逐位对应 addr+i）。
     * @param outCap 输出容量（字数或字节数，由实现按区解释）。
     * @param outLen 实际写入长度（字数 / 字节数）。
     * @return 结果（Ok 或异常）。
     */
    XModbusVirtualResult (*read)(void* self, uint8_t fc, uint16_t addr,
                                 uint16_t count, uint16_t* out,
                                 uint16_t outCap, uint16_t* outLen);
    /**
     * @brief 写（FC05 单线圈 / FC06 单寄存器 / FC0F / FC10）。
     * @param self 模型对象。
     * @param fc 功能码。
     * @param addr 起始地址。
     * @param count 数量（FC05/FC06 恒 1）。
     * @param values 写入值（位区=每点一个 0/1；寄存器区=每字一个 uint16_t）。
     * @return 结果。
     */
    XModbusVirtualResult (*write)(void* self, uint8_t fc, uint16_t addr,
                                  uint16_t count, const uint16_t* values);
    /** @brief 释放模型私有资源（可为 NULL）。 */
    void (*destroy)(void* self);
    /**
     * @brief 应答功能码覆盖（可选；NULL = 应答功能码与请求相同）。
     * @param self 模型对象。
     * @param requestFc 请求功能码（原始值，不含异常位）。
     * @return 应答帧应使用的功能码。
     * @details [为什么需要] 个别真机从站有"应答功能码 ≠ 请求功能码"的怪癖：
     *          站 2 ZHEYI-5100ZDY 对 FC04 请求也受理，但应答功能码恒回 FC03
     *          （2026-11-09 板端实测，docs/HSF板端ttyS4三设备取数实测总表
     *          §5.5 附录 A）。按"应答功能码 == 请求功能码"校验的主站会误判
     *          ——模型必须能把这一怪癖如实带上线上。
     */
    uint8_t (*responseFunction)(void* self, uint8_t requestFc);
} XModbusVirtualModelVtable;

/**
 * @brief 通用模型句柄（各具体模型以它为第一成员）。
 */
typedef struct XModbusVirtualModel {
    const XModbusVirtualModelVtable* m_vtable;  /**< 虚表（各模型给静态表）。 */
    void* m_self;                               /**< 具体模型对象（多数情况=本指针）。 */
} XModbusVirtualModel;

/* ==================== 模型 → 客户端的转发入口 ==================== */

/**
 * @brief 按虚表读（供 XModbusVirtualClient 调用）。
 * @return 结果；model 为 NULL 时返回 IllegalAddress。
 */
XModbusVirtualResult XModbusVirtualModel_read(struct XModbusVirtualModel* model,
                                              uint8_t fc, uint16_t addr,
                                              uint16_t count, uint16_t* out,
                                              uint16_t outCap, uint16_t* outLen);

/**
 * @brief 按虚表写。
 */
XModbusVirtualResult XModbusVirtualModel_write(struct XModbusVirtualModel* model,
                                               uint8_t fc, uint16_t addr,
                                               uint16_t count,
                                               const uint16_t* values);

/**
 * @brief 取该模型应答帧应使用的功能码（模型未提供覆盖时 = 请求功能码）。
 * @param model 模型（可为 NULL）。
 * @param requestFc 请求功能码。
 * @return 应答功能码。
 */
uint8_t XModbusVirtualModel_responseFunction(
    const struct XModbusVirtualModel* model, uint8_t requestFc);

/* ==================== 各设备模型（实现见 XModbusVirtualModels.c） ==================== */

struct XVirtualModelVfd;      /**< M512/ATV61 变频器。 */
struct XVirtualModelDigital;  /**< MT3A-IO1632 数字量（16 或 32 点）。 */
struct XVirtualModelAnalog;   /**< 中盛 8 路 4-20mA 模拟量。 */
struct XVirtualModelTempHum;  /**< SHT30 温湿度变送器。 */
struct XVirtualModelFlow;     /**< LD 电磁流量计。 */
struct XVirtualModelTurbidity;/**< 在线浊度仪（站1 WZT CDAB / 站2 ZHEYI DCBA 浮点，逐台字序）。 */

/** @brief 建变频器模型（maxHz 为最大频率，缺省 50）。 */
struct XVirtualModelVfd* XVirtualModelVfd_create(double maxHz);
/** @brief 变频器模型：设置设定频率（Hz）。 */
void XVirtualModelVfd_setFrequency(struct XVirtualModelVfd* m, double hz);
/**
 * @brief 变频器模型：推进一拍（输出频率向目标线性爬坡 ±2.5Hz）。
 * @details [心跳驱动] 推进必须只由统一心跳拍调用（HsfServerIoSim_tick，
 *          100ms ⇒ 25Hz/s），不要挂在读路径上——一次扫描多个读组会推进
 *          多次，轮询节拍一变加速时间跟着变（Python on_access 同口径）。
 */
void XVirtualModelVfd_tick(struct XVirtualModelVfd* m);
/** @brief 变频器模型：当前输出频率（Hz）。 */
double XVirtualModelVfd_outputFrequency(const struct XVirtualModelVfd* m);
/** @brief 变频器模型：是否在运行。 */
bool XVirtualModelVfd_running(const struct XVirtualModelVfd* m);

/** @brief 建数字量模型（channels=16 或 32）。 */
struct XVirtualModelDigital* XVirtualModelDigital_create(int channels);
/** @brief 数字量模型：设 DI 位图（bit n = 通道 n）。 */
void XVirtualModelDigital_setDi(struct XVirtualModelDigital* m, uint32_t mask);
/**
 * @brief 数字量模型：设 DO 初值位图（bit n = 通道 n）。
 * @note  与 Python VirtualDigital 的 do_init 同口径：开机即有的输出位
 *        （现场部分继电器常带电）在装配期由工厂注入；阀区（联动开启时）
 *        的 DO 仍按写线圈路径驱动行程。
 */
void XVirtualModelDigital_setDo(struct XVirtualModelDigital* m, uint32_t mask);
/** @brief 数字量模型：取 DO 位图。 */
uint32_t XVirtualModelDigital_doMask(const struct XVirtualModelDigital* m);
/**
 * @brief 数字量模型：取 DI 位图（**按当前行程状态推导**）。
 * @note  阀区（DO 通道 n → 关到位 DI 2n / 开到位 DI 2n+1）的值取决于
 *        动作是否已到位，不是静态位图。
 */
uint32_t XVirtualModelDigital_diMask(const struct XVirtualModelDigital* m);
/**
 * @brief 数字量模型：阀到位联动开关（缺省开）。
 * @details [为什么要联动] 电磁阀每路 1 DO + 2 DI 反馈。关掉联动后写 DO
 *          不会改变 DI，于是阀永远停在"动作中"，超时告警路径可被验证；
 *          打开联动则按现场真实时序（写 DO → 行程计时 → 到位）走。
 * @param enable true=联动（缺省）。
 */
void XVirtualModelDigital_setValveInterlock(struct XVirtualModelDigital* m,
                                            bool enable);
/**
 * @brief 数字量模型：本模块内的阀路数（缺省 6）。
 * @details [为什么要有这个边界] 阀区 = DO 通道 0..count-1，其对应
 *          关到位 DI(2n) / 开到位 DI(2n+1) 由行程动态推导；**阀区之外**
 *          的通道（进水泵 DO、按钮灯 DO、按钮输入 DI）不参与行程。
 *          少了这个边界，写泵/写灯会顺手把按钮组的输入位拉成 0。
 * @param count 阀路数（0 = 本模块无阀；超过模型容量自动收敛）。
 */
void XVirtualModelDigital_setValveChannels(struct XVirtualModelDigital* m,
                                           int count);
/**
 * @brief 数字量模型：阀机械行程时间（毫秒；缺省 10000）。
 * @details [为什么建模行程] 写 DO 后 DI 立刻跟着变的话，「命令」与「到位」
 *          是同一瞬间，上层阀状态机永远看不到「动作中」，20s 超时告警分支
 *          成为死代码——模拟联调会给人"超时逻辑已验证"的错觉。
 *          现场满开满关约 10s，故缺省同值。
 *
 *          常用取值（用来验证服务端的不同故障判读）：
 *            · 10000        正常阀：10s 到位（缺省）
 *            · 25000        行程过长：20s 判据时必然超时（验证告警）
 *            · UINT32_MAX   阀卡死：永远不到位
 * @param moveMs 行程毫秒（0 忽略，保持原值）。
 */
void XVirtualModelDigital_setMoveMs(struct XVirtualModelDigital* m,
                                    uint32_t moveMs);
/**
 * @brief 数字量模型：模拟"行程开关没接线"（DI 恒 0）。
 * @note  与 moveMs=UINT32_MAX 的区别只在服务端的文案分支：
 *        没接线 → "无到位反馈（检查行程开关接线）"；
 *        卡死   → "动作超时未到位"。两者都验证，才能覆盖两条告警路径。
 */
void XVirtualModelDigital_setNoFeedback(struct XVirtualModelDigital* m,
                                        bool noFeedback);

/** @brief 建模拟量模型（channels 路，毫伏口径）。 */
struct XVirtualModelAnalog* XVirtualModelAnalog_create(int channels);
/** @brief 模拟量模型：设某通道毫伏值。 */
void XVirtualModelAnalog_setMillivolts(struct XVirtualModelAnalog* m, int ch,
                                       uint16_t mv);

/** @brief 建温湿度模型（缺省 26.5℃ / 55.0%）。 */
struct XVirtualModelTempHum* XVirtualModelTempHum_create(void);
/** @brief 温湿度模型：设值（0.1 单位）。 */
void XVirtualModelTempHum_set(struct XVirtualModelTempHum* m, int temp_tenths,
                              int hum_tenths);

/** @brief 建流量计模型。 */
struct XVirtualModelFlow* XVirtualModelFlow_create(void);
/**
 * @brief 流量计模型：推进累积量积分（按真实经过的时间，重复推进安全）。
 * @details 读路径也会推进（dt 口径下只是把积分切细）；本入口供心跳拍在
 *          链路停摆时仍维持积分（现场不会因为上位机不在读就静止）。
 */
void XVirtualModelFlow_tick(struct XVirtualModelFlow* m);
/** @brief 流量计模型：设瞬时流量（m3/h；≥0 固定不波动，负数恢复波动）。 */
void XVirtualModelFlow_setInstantFlow(struct XVirtualModelFlow* m, float v);
/**
 * @brief 流量计模型：设波动基准流量（m3/h）并恢复 ±2% 正弦微波动。
 * @param base 波动基准（m3/h；≤0 忽略）。
 * @details [2026-11-09 ttyS4 实测口径] 现场灌水后瞬时流量约 3.6 m³/h、
 *          每 40s 观察窗内按正弦微动（模拟器口径 ±2%、周期 60s）——
 *          恒定值看不出"数据在刷新"。setInstantFlow 是"钉死"语义
 *          （测试断言精确值用）；本入口是"绕基准波动"语义，两者互补。
 */
void XVirtualModelFlow_setBaseFlow(struct XVirtualModelFlow* m, double base);

/**
 * @brief 浊度仪模型的寄存器字节序（**线上字序，两台真机不同**）。
 * @details 2026-11-09 板端实测（docs/HSF板端ttyS4三设备取数实测总表_20261109.md
 *          §5.3/§7）：站 1 WZT-701A = CDAB（低字在前）、站 2 ZHEYI-5100ZDY =
 *          DCBA（整字节反序 = 小端 float32）。同一帧按错的字序解会得到差
 *          4 个数量级的假数且不报错——模型必须能按台选择，与设备层
 *          XTurbidityMeter 的同名字序旋钮一一对映。
 */
typedef enum XVirtualModelTurbWordOrder {
    XVirtualModelTurbWordOrder_Cdab = 0, /**< 低字在前（reg0=低字；WZT-701A）。 */
    XVirtualModelTurbWordOrder_Dcba = 1  /**< 整字节反序=小端 float32（ZHEYI-5100ZDY）。 */
} XVirtualModelTurbWordOrder;

/**
 * @brief 建在线浊度仪模型（FC03 读 0x0000 起；缺省 CDAB 浮点）。
 * @param index 台号（0/1）：缺省基准与线上字序跟随**实测现场口径**——
 *              台 0 = 站 1 WZT-701A（CDAB，基准 2.29NTU）；台 1 = 站 2
 *              ZHEYI-5100ZDY（DCBA，基准 0.045NTU、带温度 19.3℃）。
 * @return 新模型；内存不足返回 NULL。
 * @note  站 1 换算口径来自**真机手册**（WZT-701A 4.3.10.2 的可复现帧：
 *        01 03 04 D7 0A 41 1F = 9.99NTU，即 CDAB 低字在前 + IEEE754 float32）；
 *        站 2 口径来自 2026-11-09 板端实测（4F DD 37 3D 按 DCBA = 0.0449NTU）。
 *        设备层 XTurbidityMeter 的对应旋钮与之一致。
 */
struct XVirtualModelTurbidity* XVirtualModelTurbidity_create(int index);

/**
 * @brief 浊度仪模型：把读数固定成指定 NTU（测试/演示用；不波动）。
 * @param ntu 目标浊度（NTU）；负数被夹到 0。
 * @note  设了固定值就不再按正弦波动，便于断言精确数值。
 */
void XVirtualModelTurbidity_setNtu(struct XVirtualModelTurbidity* m, double ntu);

/** @brief 浊度仪模型：读当前 NTU（诊断/断言用）。 */
double XVirtualModelTurbidity_ntu(const struct XVirtualModelTurbidity* m);

/**
 * @brief 浊度仪模型：设**绝对幅度**的浊度微波动（NTU，绕基准正弦）。
 * @param ampNtu 波动幅度（NTU）：0 = 恒定（站 1 WZT 实测 20 轮完全不动）、
 *               >0 = 绕基准 ±amp 正弦缓慢波动（站 2 ZHEYI 实测 0.045±0.001）。
 * @details [三态语义] setNtu 是"钉死精确值"（测试断言用）；本入口设 0 也
 *          是恒定，但保留"绕基准"的语义位——之后想恢复波动传正幅值即可，
 *          不必重设基准。未调用过本入口的模型维持旧口径（相对 ±3% 波动，
 *          2026-11-09 之前的既定行为，一字不改）。
 */
void XVirtualModelTurbidity_setWobble(struct XVirtualModelTurbidity* m,
                                      double ampNtu);

/**
 * @brief 浊度仪模型：设水温缓慢漂移幅度（℃，绕基准正弦；0 = 恒温）。
 * @details 站 2 实测温度 19.25~19.3125℃（40s 窗，均值 19.27）——按
 *          19.3±0.05、周期 60s 缓慢漂移复现。缺省 0（恒温），setTempC
 *          的既有语义一字不改。
 */
void XVirtualModelTurbidity_setTempWobble(struct XVirtualModelTurbidity* m,
                                          double ampC);

/**
 * @brief 浊度仪模型：设线上寄存器字节序（缺省按 create 的台号：0=CDAB、1=DCBA）。
 * @param order XVirtualModelTurbWordOrder 枚举值；非法值忽略。
 * @note  模型给出的**寄存器字**必须与设备层解码字序配套：CDAB 模型配
 *        CDAB 解码、DCBA 模型配 DCBA 解码，读回值才保真（回归 §K 锁）。
 */
void XVirtualModelTurbidity_setWordOrder(struct XVirtualModelTurbidity* m, int order);

/**
 * @brief 浊度仪模型：设水温并打开温度寄存器（0x0002/0x0003；℃）。
 * @param tempC 目标温度；<0 = 关闭温度（0x0002/0x0003 恒 0，站 1 口径）。
 * @note  温度字与浊度字**同一字序**（同一家仪表的同一帧口径）；
 *        2026-11-09 实测站 2 温度字 `00 00 99 41` 按 DCBA 解 = 19.125℃。
 */
void XVirtualModelTurbidity_setTempC(struct XVirtualModelTurbidity* m, double tempC);

/** @brief 浊度仪模型：读当前温度（诊断/断言用；未开温度恒 0）。 */
double XVirtualModelTurbidity_tempC(const struct XVirtualModelTurbidity* m);

/** @brief 取模型的通用句柄（各 create 出来的对象可直接当 XModbusVirtualModel* 用）。 */
#define XVirtualModel_base(m) ((struct XModbusVirtualModel*)(m))

#endif /* XMODBUS_ON && XMODBUS_CLIENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XMODBUSVIRTUALMODEL_H */
