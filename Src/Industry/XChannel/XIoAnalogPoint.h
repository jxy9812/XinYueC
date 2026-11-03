/**
 * @file       XIoAnalogPoint.h
 * @brief      工业模拟量 IO 点：XIoPoint 子类，承载标度四件套。
 * @details    [2026-11-03 数字/模拟拆分] XIoPoint 重写后是纯数据容器，但
 *             标度载荷（m_scaleNum/m_scaleDen/m_offset/m_deadband）只有
 *             AI/AO 消费，DI/DO 点白扛 16 字节。本类把模拟量载荷拆出来：
 *
 *               基类 XIoPoint（数字量即基类）：类信息 + 值/状态 + role/tag/type
 *               子类 XIoAnalogPoint（AI/AO 用）    ：基类 + 标度四件套
 *
 *             x64 上数字点 64→48 字节（-25%），模拟点保持 64 字节不变。
 *             本类仍是**纯数据子类**（零新虚槽、无链路指针），只把数据
 *             按消费方切开，不复活旧实现「点自己会读写」的行为点类；
 *             链路能力仍全部在 XIoLink（见 XChannel.md）。
 *
 *             [isa 判定] 本框架每个类持独立单例虚表，init 时写入实例。
 *             「点是不是模拟量点」用虚表指针比对即可靠判定
 *             （XIoPoint_toEngineering 分发、组登记闸口都用这一招）。
 *
 *             [所有权] 与基类一致：m_autoDelete=1 时内存交由登记的
 *             XIoChannelGroup 释放；多态删除经 XClassDelete 走虚表，
 *             组侧代码无需感知子类存在。
 *
 *             [标度] AI: 工程值 = 原生值 * m_scaleNum / m_scaleDen + m_offset
 *                    AO: 原生值 = (工程值 - m_offset) * m_scaleDen / m_scaleNum
 *             m_scaleDen/m_scaleNum 为 0 时按 1 处理。m_deadband 仅 AI 消费。
 *
 *             线程模型：与框架一致单线程事件循环。
 */
#ifndef XIOANALOGPOINT_H
#define XIOANALOGPOINT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XIoPoint.h"
#include <stdint.h>

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/* 类虚表声明：纯数据子类，无自定义虚槽，只继承 XIoPoint 基段。 */
XCLASS_DEFINE_BEGING(XIoAnalogPoint)
XCLASS_DEFINE_EXTEND_END(XIoAnalogPoint, XIoPoint)

/**
 * @brief 工业模拟量 IO 点（纯数据子类；无虚槽）。
 * @details 成员禁止直接访问，一律经 XIoAnalogPoint_* API。
 *          第一成员必须是基类 XIoPoint（上行转换按首地址直转）。
 */
typedef struct XIoAnalogPoint {
    XIoPoint m_base;           /**< 第一成员：基类（值/状态/role/type 等）。 */
    int32_t m_scaleNum;        /**< 标度分子（0 按 1）。 */
    int32_t m_scaleDen;        /**< 标度分母（0 按 1）。 */
    int32_t m_offset;          /**< 工程值偏移量。 */
    int32_t m_deadband;        /**< AI 变化死区（工程值）；负值按 0；AO 不消费。 */
} XIoAnalogPoint;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XIoAnalogPoint 类的虚函数表（单例）。
 * @return 指向初始化完成的 XVtable；不可释放。
 */
XVtable* XIoAnalogPoint_class_init(void);

/**
 * @brief 栈上初始化模拟量 IO 点（基类 init + 尾段清零 + 设虚表）。
 * @param point 目标点；不能为 NULL，可为未初始化内存。
 * @return 无。
 * @note 与 XClass_Deinit 成对出现。
 */
void XIoAnalogPoint_init(XIoAnalogPoint* point);

/**
 * @brief 堆上创建模拟量 IO 点（调用方负责 XClassDelete）。
 * @param memory 内存类型。
 * @return 新点；分配失败返回 NULL。
 * @note 多态删除安全：经 XClassDelete 走虚表反初始化，基类指针即可持有。
 */
XIoAnalogPoint* XIoAnalogPoint_create_ex(XMemoryType memory);
#define XIoAnalogPoint_create() XIoAnalogPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== isa 查询 ==================== */

/**
 * @brief 查询点是否为 XIoAnalogPoint 实例（虚表指针比对，O(1)）。
 * @param point 目标点；NULL 返回 false。
 * @return true=模拟量点（持有标度）。
 * @note 基类 XIoPoint 的数字点一律返回 false。
 */
bool XIoAnalogPoint_isInstance(const XIoPoint* point);

/* ==================== 成员访问 ==================== */

/**
 * @brief 设置标度四件套。
 * @param point 目标点；NULL 时无操作。
 * @param scaleNum 分子（0 按 1）。
 * @param scaleDen 分母（0 按 1）。
 * @param offset 偏移。
 * @param deadband 死区（负值按 0；仅 AI 消费）。
 * @return 无。
 */
void XIoAnalogPoint_setScale(XIoAnalogPoint* point, int32_t scaleNum,
                             int32_t scaleDen, int32_t offset, int32_t deadband);

/* ==================== 标度换算 ==================== */

/**
 * @brief 原生值 → 工程值：raw * scaleNum / scaleDen + offset。
 * @param point 目标点（提供标度）；不能为 NULL。
 * @param raw 原生值（链路层口径）。
 * @return 定点工程值（向零取整；scaleDen 0 按 1）。
 */
int32_t XIoAnalogPoint_toEngineering(const XIoAnalogPoint* point, int32_t raw);

/**
 * @brief 工程值 → 原生值：(value - offset) * scaleDen / scaleNum。
 * @param point 目标点（提供标度）；不能为 NULL。
 * @param value 定点工程值。
 * @return 原生值。**有符号点返回 16 位补码原码**（如 -2000 → 0xF830 的低 16 位）；
 *         无符号点负值钳 0、上限 65535。
 * @note [2026-11-02 根修] 符号语义只在本函数内定义一次：按 m_signed 分流，
 *       有符号点负值按 16 位补码归一而不是钳成 0。数字量点不走本函数
 *       （0/1 直通，无需换算）。
 */
uint16_t XIoAnalogPoint_toRaw(const XIoAnalogPoint* point, int32_t value);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIOANALOGPOINT_H */
