/**
 * @file       XVfdDevice_Protected.h
 * @brief      变频器抽象父类内部接口（仅供子类与子系统内部使用，不对外公开）。
 * @details    集中放置品牌子类需要的：快照回填、信号发射、能力位图、语义槽
 *             读写、动作单飞与监督、通用解码与标度换算、故障表查找入口。
 *             公开头 XVfdDevice.h 不传递引入本文件。
 *             约束：本头自包含（显式 include 自己的公开头与依赖），可单独包含。
 */
#ifndef XVFDDEVICE_PROTECTED_H
#define XVFDDEVICE_PROTECTED_H

#include "XVfdDevice.h"

#if XVFD_ON
#if XVFD_DEVICE_ON

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                      快照与信号（子类回填用）                              */
/* ========================================================================== */

/**
 * @brief 获取可变状态快照（子类解码路径回填写入）。
 * @param dev 设备实例；不能为 NULL。
 * @return 内部快照指针；禁释放。
 */
XVfdStatus* XVfdDevice_status(XVfdDevice* dev);

/**
 * @brief 置快照脏标记（解码路径每次写入后调用；发布定时器据此发信号）。
 * @param dev 设备实例；不能为 NULL。
 * @return 无。
 */
void XVfdDevice_markDirty(XVfdDevice* dev);

/**
 * @brief 立即发布快照并发射 statusUpdated 信号（不等定时器）。
 * @param dev 设备实例；不能为 NULL。
 * @return 无。
 */
void XVfdDevice_emitStatus(XVfdDevice* dev);

/**
 * @brief 发射动作完成信号并清设备级忙标志。
 * @param dev 设备实例；不能为 NULL。
 * @param op 动作类型。
 * @param ok 是否成功。
 * @return 无。
 */
void XVfdDevice_emitDone(XVfdDevice* dev, XVfdOperation op, bool ok);

/**
 * @brief 发射错误信号（message 须为静态常量文本，禁止传栈上文本）。
 * @param dev 设备实例；不能为 NULL。
 * @param message 错误描述（UTF-8 静态常量）。
 * @return 无。
 */
void XVfdDevice_emitError(XVfdDevice* dev, const char* message);

/**
 * @brief 记录失败原因（供 lastError 查询；不发射信号）。
 * @param dev 设备实例；不能为 NULL。
 * @param error 错误码。
 * @return 无。
 */
void XVfdDevice_setLastError(XVfdDevice* dev, XVfdError error);

/**
 * @brief 设置能力位（品牌 init 时逐位置位）。
 * @param dev 设备实例；不能为 NULL。
 * @param cap 能力位（XVfdCapability）。
 * @return 无。
 */
void XVfdDevice_setCapability(XVfdDevice* dev, uint32_t cap);

/**
 * @brief 设置参数描述表（品牌 init 时调用；表为静态 const，借用不拥有）。
 * @param dev 设备实例；不能为 NULL。
 * @param table 参数描述表；可为 NULL（=无参数通道）。
 * @param count 条目数（<=0 视为无参数通道）。
 * @return 无。
 */
void XVfdDevice_setParamTable(XVfdDevice* dev, const XVfdParamDesc* table, int count);

/**
 * @brief 预设最大频率（相对口径频率换算基准）。
 * @param dev 设备实例；不能为 NULL。
 * @param hz 最大频率（Hz；须 >0）。
 * @return 无。
 * @note 解码路径读到 MaxFreq 槽时也会自动更新基准；本接口供品牌 init 时
 *       按手册缺省值预设（从站尚未上电时也能正确换算）。
 */
void XVfdDevice_setMaxFrequency(XVfdDevice* dev, double hz);

/* ========================================================================== */
/*                      语义槽读写（子类动作实现用）                          */
/* ========================================================================== */

/**
 * @brief 读某语义槽的当前缓存值（经点 ReadRaw，不发起传输）。
 * @param dev 设备实例；不能为 NULL。
 * @param role 语义角色。
 * @param outValue 输出原生值（点单位）；不能为 NULL。
 * @return 取到有效值返回 true；槽未绑定/无数据返回 false。
 * @note 数据新鲜度取决于数据源（如 Hub 轮询周期），本层不做同步等待。
 */
bool XVfdDevice_readRole(XVfdDevice* dev, XVfdPointRole role, int32_t* outValue);

/**
 * @brief 写某语义槽（经组写通道下沉到点，不参与动作单飞）。
 * @param dev 设备实例；不能为 NULL。
 * @param role 语义角色。
 * @param nativeValue 原生值（点单位；工程值须先按规格除率换算）。
 * @return 通道受理返回 true；槽未绑定/类型不符/点拒绝返回 false。
 */
bool XVfdDevice_writeRole(XVfdDevice* dev, XVfdPointRole role, int32_t nativeValue);

/* ========================================================================== */
/*                      动作单飞与监督                                        */
/* ========================================================================== */

/**
 * @brief 开始一个动作（置忙并记录动作类型）。
 * @param dev 设备实例；不能为 NULL。
 * @param op 动作类型。
 * @return 受理返回 true；未挂接返回 false（NotAttached）；已有动作在途
 *         返回 false（Busy）。
 */
bool XVfdDevice_beginAction(XVfdDevice* dev, XVfdOperation op);

/**
 * @brief 收口当前动作（清忙、停监督定时器、发射 commandCompleted/paramXxxDone）。
 * @param dev 设备实例；不能为 NULL。
 * @param ok 是否成功。
 * @return 无。无在途动作时静默返回。
 */
void XVfdDevice_finishAction(XVfdDevice* dev, bool ok);

/**
 * @brief 取当前在途动作类型。
 * @param dev 设备实例；NULL 返回 XVfdOperation_None。
 * @return 动作类型。
 */
XVfdOperation XVfdDevice_currentOp(const XVfdDevice* dev);

/**
 * @brief 取当前在途动作的参数下标（仅参数读写动作有意义）。
 * @param dev 设备实例；NULL 返回 0。
 * @return 参数描述表下标。
 */
uint16_t XVfdDevice_currentParamId(const XVfdDevice* dev);

/**
 * @brief 重启动作监督定时器（单写动作=写落定超时；品牌多步序列=步进延时）。
 * @param dev 设备实例；不能为 NULL。
 * @param delayMs 延时毫秒（0 按 1）。
 * @return 无。
 */
void XVfdDevice_armActionTimer(XVfdDevice* dev, uint32_t delayMs);

/**
 * @brief 停止动作监督定时器。
 * @param dev 设备实例；可为 NULL。
 * @return 无。
 */
void XVfdDevice_stopActionTimer(XVfdDevice* dev);

/**
 * @brief 单写动作：受理检查 + 写入语义槽 + 挂监督，落定后自动收口。
 * @param dev 设备实例；不能为 NULL。
 * @param op 动作类型（会记录在 commandCompleted 中）。
 * @param role 目标语义槽。
 * @param nativeValue 原生值（点单位）。
 * @return 受理返回 true；未挂接/在途/写被拒返回 false（已按需收口并置错误）。
 * @note 写落定由 ActionWriteDone 虚槽分派，缺省实现即"成功收口"；
 *       品牌子类重写该槽即可在同一次写落定后推进下一步（如 CiA402 序列）。
 */
bool XVfdDevice_actionWrite(XVfdDevice* dev, XVfdOperation op,
                            XVfdPointRole role, int32_t nativeValue);

/* ========================================================================== */
/*                      标度换算与故障表                                      */
/* ========================================================================== */

/**
 * @brief 原生值 → 工程值（按规格除率）。
 * @param raw 原生值。
 * @param div 除率（0 视为 1）。
 * @return 工程值（double）。
 */
double XVfdDevice_rawToEngineering(int32_t raw, int32_t div);

/**
 * @brief 工程值 → 原生值（按规格除率，四舍五入并钳位）。
 * @param value 工程值。
 * @param div 除率（0 视为 1）。
 * @param minRaw 原生值下限。
 * @param maxRaw 原生值上限。
 * @return 原生值（int32）。
 */
int32_t XVfdDevice_engineeringToRaw(double value, int32_t div, int32_t minRaw, int32_t maxRaw);

/**
 * @brief 在品牌故障表中按码线性查找条目。
 * @param table 故障表（const，借用）；可 NULL。
 * @param count 条目数。
 * @param code 故障码。
 * @return 命中条目指针（静态常量）；未命中返回 NULL。
 */
const XVfdFaultEntry* XVfdDevice_faultEntry_const(const XVfdFaultEntry* table, int count,
                                                  uint16_t code);

/**
 * @brief 通用遥测解码（基类实现；品牌子类在自己的 DecodePoint 里对未知角色
 *        经父类链回调本入口）。
 * @param dev 设备实例；不能为 NULL。
 * @param spec 点规格（提供角色与除率）；不能为 NULL。
 * @param raw 原生值（点单位）。
 * @return 无。
 */
void XVfdDevice_decodePoint_base(XVfdDevice* dev, const XVfdPointSpec* spec, int32_t raw);

#ifdef __cplusplus
}
#endif

#endif /* XVFD_DEVICE_ON */
#endif /* XVFD_ON */
#endif /* XVFDDEVICE_PROTECTED_H */
