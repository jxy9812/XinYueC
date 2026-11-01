/**
 * @file       XIoChannelGroup.h
 * @brief      工业通道组：纯「点管理 + 信号分发」（对标 QButtonGroup 的
 *             「组持有共享机制、成员退化为主题记录」形态）。
 * @details    [2026-11-01 轻量化] 完整 XObject 基类 136B/对象，其中父子
 *             关系、动态属性、事件过滤器、对象名等成员对 IO 通道终生无用；
 *             且 N 个通道各自连接数据源信号 = N 条连接 + O(N) 分发。本类把
 *             「对象性」整体上收：全组只有一个 XObject（本组），通道退化为
 *             48~64B 轻量点（XIoPoint / XIoAnalogPoint，继承 XClass 而非
 *             XObject）。
 *             [职责边界] 组只做两件事：
 *               1) 管理登记（addPoint/remove/pointAt）；
 *               2) 信号分发（点子类数据到达时经 xIoPointFeedValue 喂入 →
 *                  发射 inputChanged；写落定经 xIoPointWriteDone 转发为
 *                  writeDone）。
 *              **组不知道任何具体传输、也不含定时器**——数据到达节奏由点
 *             子类自行驱动（如 XModbusPoint 订阅 Hub 缓存信号推送；见
 *             XModbusPoint.h）。
 *             [寻址] 一律以 XIoPoint* 直传（指针即身份，O(1) 校验
 *             point->m_group==self），无 id 遍历。
 *             [所有权] 点由调用方创建/释放（XClassDelete）；组登记/摘除
 *             仅维护注册表，不接管所有权。
 *             [初值] 点子类在数据首拍到时推送初值（调用方此刻可能尚未
 *             连接，初值亦经 read 获取）。
 *             线程模型：与框架一致单线程事件循环。
 */
#ifndef XIOCHANNELGROUP_H
#define XIOCHANNELGROUP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#include "XIoPoint.h"
#include "XIoAnalogPoint.h"
#include <stdint.h>
#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XIoChannelGroup)
XCLASS_DEFINE_EXTEND_END(XIoChannelGroup, XObject)

/**
 * @brief XIoChannelGroup 工业通道组（全组唯一的 XObject）。
 * @details 持有点注册表；组级信号带点指针。无定时器：数据到达节奏由点
 *          子类自行驱动（推送制）。
 */
typedef struct XIoChannelGroup {
    XObject m_class;              /**< 第一个成员，基类 XObject。 */
    XVector* m_records;           /**< 登记点表（XIoPoint*，按添加序；不接管所有权）。 */
} XIoChannelGroup;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XIoChannelGroup 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XIoChannelGroup_class_init(void);

/**
 * @brief 栈上初始化通道组（空表）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存（内部先清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；堆上一步创建用 XIoChannelGroup_create。
 */
void XIoChannelGroup_init(XIoChannelGroup* obj);

/**
 * @brief 堆上创建通道组。
 * @return 新通道组；内存分配失败返回 NULL。不再使用时必须调用 XClassDelete
 *         释放（析构清注册表；点为调用方所有，不释放）。
 */
XIoChannelGroup* XIoChannelGroup_create(void);

/* ==================== 点管理 ==================== */

/**
 * @brief 登记一个点（组回填点内 m_group）。
 * @param self 目标通道组；传入 NULL 返回 false。
 * @param point 点对象（调用方创建：XModbusPoint_createXxx 等子类工厂）；
 *              不能为 NULL，且须未登记过其他组。
 * @return 登记成功 true；参数非法/已登记于其他组/入表失败返回 false。
 */
bool XIoChannelGroup_addPoint(XIoChannelGroup* self, XIoPoint* point,
                              bool autoDelete);

/**
 * @brief 登记一个点（组回填点内 m_group）。
 * @param self 目标通道组；传入 NULL 返回 false。
 * @param point 点对象（调用方创建：XModbusPoint_createXxx 等子类工厂）；
 *              不能为 NULL，且须未登记过其他组。
 * @param autoDelete 自动内存管理：true=内存交由组自动释放（remove/组析构
 *              时 XClassDelete，调用方不得再删）；false=调用方自管（缺省）。
 * @return 登记成功 true；参数非法/已登记于其他组/入表失败返回 false。
 */
bool XIoChannelGroup_addPoint(XIoChannelGroup* self, XIoPoint* point,
                              bool autoDelete);

/**
 * @brief 摘除登记。autoDelete 点随摘除一并 XClassDelete 释放。
 * @param self 目标通道组；传入 NULL 返回 false。
 * @param point 待摘除的点。
 * @return 摘除成功 true；点未登记返回 false（autoDelete 点保持原状）。
 */
bool XIoChannelGroup_remove(XIoChannelGroup* self, XIoPoint* point);

/**
 * @brief 登记点数。
 * @param self 目标通道组；NULL 返回 0。
 * @return 当前登记点数。
 */
int XIoChannelGroup_recordCount(const XIoChannelGroup* self);

/**
 * @brief 按下标取点（借用）。
 * @param self 目标通道组；NULL 返回 NULL。
 * @param index 添加序下标；越界返回 NULL。
 * @return 点基指针；remove/组析构后失效。AI/AO 点可经
 *         (const XIoAnalogPoint*) 转取标度字段。
 */
const XIoPoint* XIoChannelGroup_pointAt(const XIoChannelGroup* self, int index);

/**
 * @brief 查询点是否已登记于本组。
 * @param self 目标通道组；NULL 返回 false。
 * @param point 待查点；NULL 返回 false。
 * @return 已登记 true。
 */
bool XIoChannelGroup_contains(const XIoChannelGroup* self, const XIoPoint* point);

/* ==================== 读写 ==================== */

/**
 * @brief 读离散量输入点当前值（最近一次推送/回填）。
 * @param self 目标通道组；传入 NULL 返回 false。
 * @param point 点（须为 DigitalInput 点且已登记本组）。
 * @param active 输出逻辑值；不能为 NULL。
 * @return true=读取成功（点存在且已有值）；false=未登记/类型不符/尚无数据。
 */
bool XIoChannelGroup_readDigitalInput(XIoChannelGroup* self, XIoPoint* point, bool* active);

/**
 * @brief 写离散量输出点（经点虚槽 WriteRaw 受理；落定经 writeDone 冒泡）。
 * @param self 目标通道组；传入 NULL 返回 false。
 * @param point 点（须为 DigitalOutput 点且已登记本组）。
 * @param active 目标逻辑值。
 * @return true=写意图已受理；false=未登记/类型不符/点拒绝。
 */
bool XIoChannelGroup_writeDigitalOutput(XIoChannelGroup* self, XIoPoint* point, bool active);

/**
 * @brief 读寄存器输入点当前工程值（最近一次推送/回填）。
 * @param self 目标通道组；传入 NULL 返回 false。
 * @param point 点（须为 AnalogInput 点且已登记本组）。
 * @param value 输出定点工程值；不能为 NULL。
 * @return true=读取成功；false=未登记/类型不符/尚无数据。
 */
bool XIoChannelGroup_readAnalogInput(XIoChannelGroup* self, XIoPoint* point, int32_t* value);

/**
 * @brief 写寄存器输出点（点值经点虚槽 WriteRaw 受理，子类负责标度换算）。
 * @param self 目标通道组；传入 NULL 返回 false。
 * @param point 点（须为 AnalogOutput 点且已登记本组）。
 * @param value 定点工程值。
 * @return true=写意图已受理；完成确认经 writeDone(point, ok) 异步冒泡。
 */
bool XIoChannelGroup_writeAnalogOutput(XIoChannelGroup* self, XIoPoint* point, int32_t value);

/**
 * @brief 查询点数据有效标志。
 * @param self 目标通道组；NULL 返回 false。
 * @param point 待查点。
 * @return true=数据可信（推送/回填/写落定后置位）；否则 false。
 */
bool XIoChannelGroup_isValid(const XIoChannelGroup* self, const XIoPoint* point);

/* ==================== 信号 ==================== */

/**
 * @brief inputChanged 信号：登记的输入点值变化时发射（播种不发射）。
 * @param self 发射信号的通道组；经 XSignal() 宏取地址连接，禁止直调。
 * @param point 变化的点指针（可读 tag 与值缓存）。
 * @param value 变化后的定点值（DI 取 0/1；AI 为工程值）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XIoChannelGroup_inputChanged_signal(XIoChannelGroup* self, XIoPoint* point, int32_t value);

/**
 * @brief writeDone 信号：登记的输出点写意图落定时发射。
 * @param self 发射信号的通道组；经 XSignal() 宏取地址连接，禁止直调。
 * @param point 落定的点指针。
 * @param ok true=写意图已确认写入；false=失败沿（重试达阈值，仍会继续重发）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XIoChannelGroup_writeDone_signal(XIoChannelGroup* self, XIoPoint* point, bool ok);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIOCHANNELGROUP_H */
