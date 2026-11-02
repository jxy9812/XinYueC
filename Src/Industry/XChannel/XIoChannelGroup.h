/**
 * @file       XIoChannelGroup.h
 * @brief      工业通道组：点管理 + 信号分发 + 链路挂接（全组唯一 XObject）。
 * @details    [2026-11-02 XChannel 重写] 本类是全组**唯一**的 XObject：
 *             点退化为 48~64 字节的纯数据（XClass 直派生），N 个点不再各自
 *             订阅链路信号，而由本组持有一个 XIoLink 并**只订阅一次**。
 *
 *             [零遍历：三处都是数组下标]
 *               1) 点存放：m_points[role]，role 即下标（role < ROLE_CAP）；
 *               2) 点查找：contains/读/写直接取 m_points[role]，O(1)；
 *               3) 信号分发：链路按 role 回调 feedRole，直接落到 m_points[role]。
 *             旧实现的「每点各订阅一次 Hub 信号、广播后各自比对地址丢弃」
 *             （M512 单设备 66 条连接、每次值变化 66 次回调）不复存在。
 *
 *             [职责边界]
 *               组管：点的登记/摘除、按 role 取值取值、信号发射、链路挂接；
 *               组不管：任何具体协议（那是 XIoLink 子类的事）、定时器、
 *                        链路地址解析。
 *
 *             [写完成的两种形态]
 *               同步链路（GPIO 写电平、进程内假从站）：WriteRole 返回前就已
 *                 调 completeWrite，组照常发射 writeDone；
 *               异步链路（Modbus 等应答）：应答到达后由链路调 completeWrite。
 *               上层用 XIoLink_isAsync_base 判断要不要 arm 动作监督定时器。
 *
 *             [所有权] 点由调用方创建；autoDelete=1 时组在摘除/析构时释放。
 *             链路是**借用**：由调用方创建并保证生命周期覆盖本组。
 *
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
#include "XIoLink.h"
#include <stdint.h>

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/* ==================== 类定义 ==================== */

XCLASS_DEFINE_BEGING(XIoChannelGroup)
XCLASS_DEFINE_EXTEND_END(XIoChannelGroup, XObject)

/**
 * @brief 工业通道组（全组唯一 XObject；点表按 role 下标密集存放）。
 */
typedef struct XIoChannelGroup {
    XObject m_class;                          /**< 第一成员：基类 XObject。 */
    XIoPoint* m_points[XIOCHANNELGROUP_ROLE_CAP]; /**< 点表（role 作下标；NULL=空位）。 */
    XIoLink* m_link;                          /**< 挂接的链路（借用；NULL=未挂接）。 */
    int m_pointCount;                         /**< 已登记点数（O(1) 查询，免遍历）。 */
} XIoChannelGroup;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XIoChannelGroup 类的虚函数表（单例）。
 * @return 指向初始化完成的 XVtable；不可释放。
 */
XVtable* XIoChannelGroup_class_init(void);

/**
 * @brief 栈上初始化通道组（空表）。
 * @param obj 目标对象；不能为 NULL，可为未初始化内存。
 * @return 无。
 */
void XIoChannelGroup_init(XIoChannelGroup* obj);

/**
 * @brief 堆上创建通道组（不再使用时 XClassDelete）。
 * @return 新通道组；分配失败返回 NULL。
 */
XIoChannelGroup* XIoChannelGroup_create(void);

/* ==================== 链路挂接 ==================== */

/**
 * @brief 挂接链路（装配期调用；链路为借用，调用方保证生命周期）。
 * @param self 目标通道组；传入 NULL 返回 false。
 * @param link 链路；传入 NULL 表示解绑。
 * @return 成功 true。
 * @note 本函数只记录指针并回写 link->m_group，不订阅任何信号——
 *       订阅由链路子类在自己的 start 里完成（它才知道要订阅什么）。
 */
bool XIoChannelGroup_setLink(XIoChannelGroup* self, XIoLink* link);

/**
 * @brief 读挂接的链路（借用）。
 * @param self 目标通道组；NULL 返回 NULL。
 * @return 链路指针；未挂接返回 NULL。
 */
XIoLink* XIoChannelGroup_link(const XIoChannelGroup* self);

/* ==================== 点管理 ==================== */

/**
 * @brief 登记一个点（按点的 role 放入对应下标位）。
 * @param self 目标通道组；传入 NULL 返回 false。
 * @param point 点对象（调用方创建）；不能为 NULL。
 * @param autoDelete true=内存交由组释放（摘除/析构时）。
 * @return 成功 true；role 越界、该 role 已被占用、参数非法返回 false。
 * @note role 位冲突一律拒绝——不做"自动找空位"，因为 role 就是语义身份，
 *       两个点抢同一个 role 是装配错误，必须暴露而不是悄悄换位置。
 */
bool XIoChannelGroup_addPoint(XIoChannelGroup* self, XIoPoint* point,
                              bool autoDelete);

/**
 * @brief 摘除登记（不释放）。点从表里摘出、m_group 清空，所有权移交调用方。
 * @param self 目标通道组；NULL 返回 NULL。
 * @param point 待摘除的点。
 * @return 摘出的点（所有权移交调用方）；未登记返回 NULL。
 */
XIoPoint* XIoChannelGroup_takePoint(XIoChannelGroup* self, XIoPoint* point);

/**
 * @brief 按 role 摘除登记。autoDelete 点随摘除一并释放。
 * @param self 目标通道组；NULL 返回 false。
 * @param role 语义槽号。
 * @return 摘除成功 true；该 role 无点返回 false。
 * @warning autoDelete=1 时点被销毁，调用方手里的指针随即悬垂；本函数按值
 *          收角色、不涉及调用方的点指针，故没有旧 remove 的指针悬垂陷阱。
 */
bool XIoChannelGroup_removeRole(XIoChannelGroup* self, uint16_t role);

/**
 * @brief 登记点数（O(1)，读计数器）。
 * @param self 目标通道组；NULL 返回 0。
 * @return 当前登记点数。
 */
int XIoChannelGroup_recordCount(const XIoChannelGroup* self);

/**
 * @brief 按 role 取点（借用；O(1)）。
 * @param self 目标通道组；NULL 返回 NULL。
 * @param role 语义槽号；越界返回 NULL。
 * @return 点基指针；该 role 无点返回 NULL。
 */
XIoPoint* XIoChannelGroup_pointAt(const XIoChannelGroup* self, uint16_t role);

/**
 * @brief 查询点是否已登记于本组（O(1)）。
 * @param self 目标通道组；NULL 返回 false。
 * @param point 待查点；NULL 返回 false。
 * @return 已登记 true。
 */
bool XIoChannelGroup_contains(const XIoChannelGroup* self, const XIoPoint* point);

/* ==================== 读写（全 O(1)） ==================== */

/**
 * @brief 读离散量输入点当前值（最近一次推送/回填）。
 * @param self 目标通道组；NULL 返回 false。
 * @param role 语义槽号。
 * @param active 输出逻辑值；不能为 NULL。
 * @return true=读取成功；false=该 role 无点/类型不符/尚无数据。
 */
bool XIoChannelGroup_readDigitalInput(XIoChannelGroup* self, uint16_t role, bool* active);

/**
 * @brief 写离散量输出点（经链路 WriteRole）。
 * @param self 目标通道组；NULL 返回 false。
 * @param role 语义槽号。
 * @param active 目标逻辑值。
 * @return true=写意图已受理；false=该 role 无点/类型不符/链路拒绝。
 * @note 落定经 writeDone(role, ok) 冒泡；同步链路在返回前就已落定。
 */
bool XIoChannelGroup_writeDigitalOutput(XIoChannelGroup* self, uint16_t role, bool active);

/**
 * @brief 读寄存器输入点当前工程值（最近一次推送/回填）。
 * @param self 目标通道组；NULL 返回 false。
 * @param role 语义槽号。
 * @param value 输出定点工程值；不能为 NULL。
 * @return true=读取成功。
 */
bool XIoChannelGroup_readAnalogInput(XIoChannelGroup* self, uint16_t role, int32_t* value);

/**
 * @brief 写寄存器输出点（经链路 WriteRole；标度换算在本层完成）。
 * @param self 目标通道组；NULL 返回 false。
 * @param role 语义槽号。
 * @param value 定点工程值。
 * @return true=写意图已受理。
 * @note 有符号点的负值按 16 位补码下发（见 XIoPoint_toRaw）。
 */
bool XIoChannelGroup_writeAnalogOutput(XIoChannelGroup* self, uint16_t role, int32_t value);

/**
 * @brief 查询点数据有效标志（O(1)）。
 * @param self 目标通道组；NULL 返回 false。
 * @param role 语义槽号。
 * @return true=数据可信。
 */
bool XIoChannelGroup_isValid(const XIoChannelGroup* self, uint16_t role);

/* ==================== 链路 → 组的数据入口 ==================== */

/**
 * @brief 链路喂入一个点的新值（链路子类数据到达时调用）。
 * @param self 目标通道组；NULL 返回 false。
 * @param role 语义槽号。
 * @param raw 原生值（链路口径）。本函数按点标度换算为工程值后存储。
 * @return true=值有变化并已发射 inputChanged；false=无点/值未变/参数非法。
 * @note 值未变时静默（不发射信号），与既有「变化才通知」契约一致。
 */
bool XIoChannelGroup_feedRole(XIoChannelGroup* self, uint16_t role, int32_t raw);

/**
 * @brief 链路报告一次写落定（链路子类收到应答时调用）。
 * @param self 目标通道组；NULL 返回 false。
 * @param role 语义槽号。
 * @param ok true=写入确认；false=失败沿。
 * @return true=已发射 writeDone；false=无点/参数非法。
 * @note 同步链路应在 WriteRole 返回前就调完本函数。
 */
bool XIoChannelGroup_completeWrite(XIoChannelGroup* self, uint16_t role, bool ok);

/* ==================== 信号 ==================== */

/**
 * @brief inputChanged 信号：登记的输入点值变化时发射（播种不发射）。
 * @param self 发射信号的通道组；经 XSignal() 宏取地址连接，禁止直调。
 * @param role 变化的语义槽号。
 * @param value 变化后的定点工程值（DI 取 0/1）。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XIoChannelGroup_inputChanged_signal(XIoChannelGroup* self, uint16_t role,
                                          int32_t value);

/**
 * @brief writeDone 信号：登记的输出点写意图落定时发射。
 * @param self 发射信号的通道组；经 XSignal() 宏取地址连接，禁止直调。
 * @param role 落定的语义槽号。
 * @param ok true=已确认写入；false=失败沿。
 * @return 信号函数地址；仅供 XSignal() 宏取地址使用。
 */
void* XIoChannelGroup_writeDone_signal(XIoChannelGroup* self, uint16_t role, bool ok);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIOCHANNELGROUP_H */
