/**
 * @file       XIoLink.h
 * @brief      链路连接抽象基类：与外部总线通信的唯一入口。
 * @details    [2026-11-02 XChannel 重写] 这是「链路」与「通道」之间**唯一**
 *             的边界。凡是需要跟外部世界交换数据的实现（Modbus 主站、
 *             CAN 总线、EtherCAT 主站、GPIO 干接点、甚至进程内假从站），
 *             都实现本类的虚槽；通道组只认 XIoLink*，不认识任何具体协议。
 *
 *             [设计目标：加一条链路只写一个子类]
 *             新增 XCanLink / XEtherCatLink / XGpioLink 时：
 *               1) 继承 XIoLink，重载 BindRole/ReadRole/WriteRole/Start/Stop；
 *               2) 把「role → 该链路的原生地址」映射登记在自己内部；
 *               3) 数据到达后调 XIoChannelGroup_feedRole 推送值、写落定后调
 *                  XIoChannelGroup_completeWrite。
 *             **不需要新写点类、不需要改通道组、不需要改设备层。**
 *
 *             [为何 role 而不是点指针] 旧设计让点自己持链路对象并实现读写
 *             虚槽，导致点类数量 = 链路数 × 数据类型数。现在读写以 role 为
 *             键：链路内部用 role 查自己的地址表（O(1)），通道组用 role 查
 *             点（O(1)），设备层用 role 查语义槽（O(1)），三处都是数组下标。
 *
 *             [同步与异步完成] 部分链路当场完成写（GPIO 写电平、进程内假从站），
 *             部分链路要等应答（Modbus 要等 FC06 回应）。IsAsync 让上层知道
 *             该不该 arm 动作监督定时器：同步链路在 WriteRole 返回前就已调
 *             completeWrite，设备层不得再启动超时等待，否则必然误判超时。
 *
 *             线程模型：与框架一致单线程事件循环。
 */
#ifndef XIOLINK_H
#define XIOLINK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XObject.h"
#include <stdint.h>

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/* ==================== 类定义 ==================== */

XCLASS_DEFINE_BEGING(XIoLink)
XCLASS_DEFINE_ENUM(XIoLink, BindRole) = XCLASS_VTABLE_GET_SIZE(XObject),
XCLASS_DEFINE_ENUM(XIoLink, ReadRole),
XCLASS_DEFINE_ENUM(XIoLink, WriteRole),
XCLASS_DEFINE_ENUM(XIoLink, IsAsync),
XCLASS_DEFINE_ENUM(XIoLink, Start),
XCLASS_DEFINE_ENUM(XIoLink, Stop),
XCLASS_DEFINE_ENUM(XIoLink, RoleFresh),
XCLASS_DEFINE_END(XIoLink)

/**
 * @brief 链路连接抽象基类（XObject 派生）。
 * @details 子类持有具体协议对象（如 XModbusHub*），并在内部维护
 *          「role → 原生地址」的 O(1) 映射表。
 */
typedef struct XIoLink {
    XObject m_class;           /**< 第一成员：基类。 */
    void* m_group;             /**< 绑定的通道组（XIoChannelGroup* 借用）；NULL=未绑定。 */
} XIoLink;

/* ==================== 类初始化 ==================== */

/**
 * @brief 初始化 XIoLink 类的虚函数表（单例）。
 * @return 指向初始化完成的 XVtable；不可释放。
 */
XVtable* XIoLink_class_init(void);

/**
 * @brief 栈上初始化链路连接（基段初始化 + 设虚表）。
 * @param link 目标对象；不能为 NULL。
 * @return 无。
 */
void XIoLink_init(XIoLink* link);

/* ==================== 虚槽直调入口 ==================== */

/**
 * @brief 登记 role → 链路原生地址的绑定。
 * @param link 链路；不能为 NULL。
 * @param role 语义槽号。
 * @param nativeAddr 链路原生地址（Modbus 区+地址、CAN 帧 ID+位域、GPIO 行号等，
 *                   具体编码由子类定义；0 亦为合法地址）。
 * @param aux 子类定义的辅助参数（如 Modbus 的区类型/位宽、CAN 的位偏移）。
 * @return 登记成功 true；不支持或参数非法返回 false。
 * @note 装配期调用；子类须在这步建立自己的 O(1) 索引。
 */
bool XIoLink_bindRole_base(XIoLink* link, uint16_t role, uint32_t nativeAddr,
                           uint32_t aux);

/**
 * @brief 读一个 role 的当前值（从链路缓存/设备读取，点单位=原生值）。
 * @param link 链路；不能为 NULL。
 * @param role 语义槽号。
 * @param outRaw 输出原生值；不能为 NULL。
 * @return 取到有效值 true。
 * @note 不得发起阻塞传输；只读本地缓存或已就绪的数据。
 */
bool XIoLink_readRole_base(XIoLink* link, uint16_t role, int32_t* outRaw);

/**
 * @brief 写一个 role（原生值口径）。
 * @param link 链路；不能为 NULL。
 * @param role 语义槽号。
 * @param raw 原生值。
 * @return 受理成功 true（不等于已落定）。
 * @note 异步链路须在落定时调 XIoChannelGroup_completeWrite(group, role, ok)；
 *       同步链路应在返回前就调完（见 isAsync）。
 */
bool XIoLink_writeRole_base(XIoLink* link, uint16_t role, int32_t raw);

/**
 * @brief 查询该链路是否异步完成写。
 * @param link 链路；不能为 NULL。
 * @return true=写要等应答（须 arm 动作监督定时器）；
 *         false=当场完成（不得 arm 定时器，否则误判超时）。
 * @note 基类默认 true（保守：未知链路按需要等待处理）。
 */
bool XIoLink_isAsync_base(const XIoLink* link);

/**
 * @brief 判断某个 role 的缓存值是否**仍然可信**（未陈旧）。
 * @param link 链路；不能为 NULL。
 * @param role 语义槽号。
 * @return true=值仍可用；false=已陈旧（从站久未应答/帧流中断）。
 *
 * @details [为什么这是虚槽而不是上层自己算] 新鲜度的判据完全取决于链路
 *          的传输语义：
 *            · Modbus 主站：看该点所在缓存块最近一次成功读回距今多久；
 *            · 进程内假从站：同步链路恒新鲜；
 *            · CAN/EtherCAT：看周期帧是否还在到。
 *          上层（设备层）只该问"这个值还能用吗"，不该知道判据。
 *
 *          [为什么必须有这个槽] 点级 m_valid 是**单向**的（推送/写落定置 1，
 *          从不清零）。没有本槽时，从站掉线后缓存值会永远以"有效"姿态被
 *          上层读到 ⇒ 现场表现为**阀掉线了但界面显示一切正常**，比报错危险。
 *          有本槽后，上层对"是否可信"才有权威判据。
 *
 * @note 基类默认恒 true（保守：不认识的链路不擅自判陈旧——宁可让上层多用
 *       一次可能过期的值，也不要把"我不了解这条链路"表达成"数据已失效"）。
 */
bool XIoLink_roleFresh_base(const XIoLink* link, uint16_t role);

/**
 * @brief 启动链路服务（开始轮询/使能中断/打开端口）。
 * @param link 链路；不能为 NULL。
 * @return 启动成功 true。
 */
bool XIoLink_start_base(XIoLink* link);

/**
 * @brief 停止链路服务（释放资源；不要求可重入）。
 * @param link 链路；不能为 NULL。
 * @return 无。
 */
void XIoLink_stop_base(XIoLink* link);

/* ==================== 组绑定（内部用） ==================== */

/**
 * @brief 把链路绑到通道组（装配期由组调用）。
 * @param link 链路；不能为 NULL。
 * @param group 通道组（借用）；NULL=解绑。
 * @return 无。
 */
void XIoLink_setGroup(XIoLink* link, void* group);

/**
 * @brief 读绑定的通道组。
 * @param link 链路；NULL 返回 NULL。
 * @return 通道组指针（借用）。
 */
void* XIoLink_group(const XIoLink* link);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIOLINK_H */
