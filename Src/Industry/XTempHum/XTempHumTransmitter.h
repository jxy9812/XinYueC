
/**
 * @file       XTempHumTransmitter.h
 * @brief      温湿度变送器设备抽象公开 API。
 * @details    对齐 Qt 无直接对应类（语义对标工业现场温湿度变送器，典型
 *             4-20mA 双通道输出）。设计完全对齐 XSolenoidValve（2026-11-01
 *             通道组重构形态）：纯业务层设备，只依赖通道接口族（通道组
 *             登记的模拟量输入点，全部 XIoPoint* 直传，信号经 XIoChannelGroup
 *             分发按 role 过滤），**不关心数据来自何处**——GPIO、Modbus 或
 *             未来任何传输都经 Industry/XChannel 的适配器在装配期注入
 *             （attach，借用不拥有）。本类永不 include 任何具体传输头文件。
 *
 *             [两级标度] 通道层（XIoAnalogPoint）负责「寄存器原始值 → 通道
 *             工程值」（如 mV→μA）；本设备负责第二级「通道工程值 → 物理量」
 *             （如 μA→0.01°C 定点：eng = chEng * num / den + offset，int64
 *             中间量防溢出）。默认 1/1/0 直通。
 *
 *             [质量口径] 数据质量 = 已收到至少一拍 && 未陈旧。陈旧 =
 *             距最近一拍超过 staleMs（0=不判陈旧）。质量巡检用 XObject
 *             自带定时器周期评估，翻转沿发 qualityChanged（对齐电磁阀的
 *             监督定时器角色，含「触发≠注销」_fd 泄漏教训的规避写法）。
 *
 *             设备对象为句柄性质：禁止拷贝/移动语义；对象可能在自身信号
 *             调用栈上被删除时，调用方必须使用 XObject_deleteLater。
 */
#ifndef XTEMPHUMTRANSMITTER_H
#define XTEMPHUMTRANSMITTER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#include "XChannel/XIoChannelGroup.h"

#if XINDUSTRY_ON && XTEMP_HUM_ON

/**
 * @brief 通道号（温湿度变送器的两个物理通道）。
 */
typedef enum XTempHumChannel {
    XTempHumChannel_Temperature = 0,    /**< 温度通道。 */
    XTempHumChannel_Humidity = 1        /**< 湿度通道。 */
} XTempHumChannel;

/**
 * @brief 温湿度变送器错误码。
 */
typedef enum XTempHumError {
    XTempHumError_None = 0,             /**< 无错误。 */
    XTempHumError_NotAttached,          /**< 通道未挂接，操作被拒绝。 */
    XTempHumError_Internal              /**< 内部资源失败（如质量巡检定时器启动失败）。 */
} XTempHumError;

/* ==================== 虚函数表定义（继承 XObject，不新增虚槽） ==================== */
XCLASS_DEFINE_BEGING(XTempHumTransmitter)
XCLASS_DEFINE_EXTEND_END(XTempHumTransmitter, XObject)

/**
 * @brief XTempHumTransmitter 温湿度变送器。
 * @details 成员禁止直接访问，一律经 API 操作。通道组与输入点均为借用，
 *          不拥有：调用方保证其生命周期覆盖变送器，并在其销毁前 detach
 *          或先删变送器。
 */
typedef struct XTempHumTransmitter {
    XObject m_class;                    /**< 第一个成员，基类 XObject，由 XClass 管理，禁止手工修改。 */
    XIoChannelGroup* m_group;           /**< 通道组（借用，可为 NULL=未挂接）；数据与信号经组分发。 */
    XIoPoint* m_pts[2];                 /**< 输入点[通道]（组内 AnalogInput 点；NULL=该通道未挂接）。 */
    int32_t m_scaleNum[2];              /**< 第二级标度分子[通道]：eng = chEng*num/den + offset。 */
    int32_t m_scaleDen[2];              /**< 第二级标度分母[通道]；0 非法（set 侧拒绝）。 */
    int32_t m_scaleOffset[2];           /**< 第二级标度偏移[通道]（与 eng 同定点单位）。 */
    int32_t m_value[2];                 /**< 最近工程值[通道]（定点；未收到数据为 0）。 */
    bool m_received[2];                 /**< 该通道是否收到过至少一拍。 */
    uint64_t m_lastChangeMs[2];         /**< 最近一拍时刻（单调毫秒；XDateTime 口径）。 */
    uint32_t m_staleMs;                 /**< 陈旧判定阈值（毫秒）；0=不判陈旧。默认 5000。 */
    bool m_signalAttached;              /**< 组 inputChanged 是否已订阅（全设备一次）。 */
    XTimerId m_qualityTimer;            /**< 质量巡检定时器 id；无效时为 XTIMER_INVALID_ID。 */
    bool m_quality[2];                  /**< 上次发布的质量[通道]（翻转沿才发信号）。 */
} XTempHumTransmitter;

/* ==================== 虚函数表初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XTempHumTransmitter 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XTempHumTransmitter_class_init(void);

/**
 * @brief 栈上初始化温湿度变送器。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；初始未挂接、标度 1/1/0 直通、陈旧阈值 5000ms。
 */
void XTempHumTransmitter_init(XTempHumTransmitter* obj);

/**
 * @brief 堆上创建温湿度变送器。
 * @return 新对象；内存分配失败返回 NULL。不再使用时必须 XClassDelete；
 *         对象可能仍被信号链引用时改用 XObject_deleteLater。
 */
XTempHumTransmitter* XTempHumTransmitter_create(void);

/* ==================== 装配（借用注入，解耦点） ==================== */

/**
 * @brief 挂接输入通道（温度/湿度两输入点，可任一为 NULL）。
 * @param self 目标变送器；不能为 NULL。
 * @param group 通道组；借用，不能为 NULL。
 * @param tempPt 温度输入点（组内已登记的 AnalogInput 点）；借用，可为 NULL。
 * @param humPt 湿度输入点；借用，可为 NULL。
 * @return 至少一点非 NULL 且信号订阅成功返回 true；两点均为 NULL 或订阅
 *         失败返回 false，订阅失败的点不生效。
 * @note 重复挂接先自动解挂旧输入。挂接成功后立即同步读取各点当前缓存值
 *       播种快照（有数据即发 valueChanged/qualityChanged）。
 */
bool XTempHumTransmitter_attach(XTempHumTransmitter* self, XIoChannelGroup* group,
                                XIoPoint* tempPt, XIoPoint* humPt);

/**
 * @brief 解挂全部输入并断开组信号订阅。
 * @param self 目标变送器；不能为 NULL。
 * @return 解挂了任一输入返回 true；原本未挂接返回 false。
 * @note 解挂后各通道质量转无效（发 qualityChanged(false)），缓存值保留。
 */
bool XTempHumTransmitter_detach(XTempHumTransmitter* self);

/* ==================== 配置 ==================== */

/**
 * @brief 设置通道第二级标度（通道工程值 → 物理量定点值）。
 * @param self 目标变送器；不能为 NULL。
 * @param channel 通道号。
 * @param num 分子；den 分母（0 拒绝）；offset 偏移（与 eng 同定点单位）。
 * @return 设置成功返回 true；参数非法返回 false，标度保持不变。
 * @note 立即生效：用新标度重算已缓存值，值变化则重发 valueChanged。
 */
bool XTempHumTransmitter_setScale(XTempHumTransmitter* self, XTempHumChannel channel,
                                  int32_t num, int32_t den, int32_t offset);

/**
 * @brief 读取通道第二级标度。
 * @param self 目标变送器；可为 NULL（全 0 出参）。
 * @param channel 通道号。
 * @param outNum/outDen/outOffset 输出（调用方提供；可为 NULL 忽略）。
 * @return 无。
 */
void XTempHumTransmitter_scale(const XTempHumTransmitter* self, XTempHumChannel channel,
                               int32_t* outNum, int32_t* outDen, int32_t* outOffset);

/**
 * @brief 设置陈旧判定阈值。
 * @param self 目标变送器；不能为 NULL。
 * @param staleMs 阈值毫秒；0=不判陈旧（数据收到过即恒有效）。
 * @return 设置成功返回 true。
 * @note 立即生效：下次质量评估按新阈值（质量翻转经 qualityChanged 发布）。
 */
bool XTempHumTransmitter_setStaleMs(XTempHumTransmitter* self, uint32_t staleMs);

/**
 * @brief 读取陈旧判定阈值。
 * @param self 目标变送器；传入 NULL 返回 0。
 * @return 当前阈值毫秒；0=不判陈旧。
 */
uint32_t XTempHumTransmitter_staleMs(const XTempHumTransmitter* self);

/* ==================== 查询 ==================== */

/**
 * @brief 读通道工程值（定点）。
 * @param self 目标变送器；可为 NULL。
 * @param channel 通道号。
 * @param outEng 输出工程值（调用方提供；可为 NULL——此时仅作有效判定）。
 * @return 数据有效（已收到且未陈旧）返回 true 并写 outEng；否则返回 false
 *         （outEng 被写 0）。
 * @note 只读查询，无副作用、不发信号。
 */
bool XTempHumTransmitter_value(const XTempHumTransmitter* self, XTempHumChannel channel,
                               int32_t* outEng);

/**
 * @brief 查通道数据质量（等价 value 的返回值）。
 * @param self 目标变送器；可为 NULL。
 * @param channel 通道号。
 * @return 有效返回 true。
 */
bool XTempHumTransmitter_isValid(const XTempHumTransmitter* self, XTempHumChannel channel);

/**
 * @brief 查通道最近一拍时刻。
 * @param self 目标变送器；可为 NULL。
 * @param channel 通道号。
 * @param outMs 输出单调毫秒（调用方提供；可为 NULL）。
 * @return 收到过数据返回 true；从未收到返回 false（outMs 写 0）。
 */
bool XTempHumTransmitter_lastUpdateMs(const XTempHumTransmitter* self,
                                      XTempHumChannel channel, uint64_t* outMs);

/* ==================== 信号 ==================== */

/**
 * @brief valueChanged 信号：通道工程值更新（含挂接播种）。
 * @param self 发射信号的变送器；经 XSignal() 宏取地址连接，禁止直调。
 * @param channel 通道号（XTempHumChannel）。
 * @param engValue 工程值（定点，标度后口径）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XTempHumTransmitter_valueChanged_signal(XTempHumTransmitter* self,
                                              int channel, int32_t engValue);

/**
 * @brief qualityChanged 信号：通道质量翻转（有效 ⇄ 无效/陈旧）。
 * @param self 发射信号的变送器；经 XSignal() 宏取地址连接，禁止直调。
 * @param channel 通道号。
 * @param valid 翻转后的质量。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 * @note 质量评估在数据拍与巡检定时器两处触发，仅翻转沿发射。
 */
void* XTempHumTransmitter_qualityChanged_signal(XTempHumTransmitter* self,
                                                int channel, bool valid);

/**
 * @brief errorOccurred 信号：变送器发生错误。
 * @param self 发射信号的变送器；经 XSignal() 宏取地址连接，禁止直调。
 * @param error 错误码（XTempHumError 枚举值）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XTempHumTransmitter_errorOccurred_signal(XTempHumTransmitter* self, int error);

#endif /* XINDUSTRY_ON && XTEMP_HUM_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTEMPHUMTRANSMITTER_H */
