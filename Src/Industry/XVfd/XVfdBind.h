/**
 * @file       XVfdBind.h
 * @brief      变频器**绑定层**：品牌语义 ↔ 具体总线地址的对应关系。
 * @details    [2026-11-02 分层收口] 本文件是「链路知识」在变频器侧的**唯一**
 *             落点。语义层（XVfdDevice）与品牌层（XVfdM512/XVfdAtv61 的语义表）
 *             都不含任何总线地址；换一条总线只需新增一个 `*_<Bus>.c`
 *             提供新的绑定表，品牌主文件一行不用改。
 *
 *             [三层职责]
 *               XVfdDevice        只认 role，按 role 作下标取语义槽（O(1)）；
 *               品牌主文件        只给语义：role / type / div / caps / signed / tag；
 *               本层（绑定表）    给地址：role → 总线原生地址 + 通信组 + 约束；
 *               XChannel/XIoLink  执行：把地址翻译成实际读写。
 *
 *             [为什么地址不属于语义]
 *             `XVfdPointSpec.m_div` 是「原生值 → 工程值」的换算率，属于设备语义
 *             （任何总线都是 0.01Hz 口径）；而 `m_address` 是「数据在 Modbus 的
 *             哪个寄存器」，属于绑定。两者混在一张表里，接 CAN 时就只能整表重写。
 *
 *             [一个品牌可以有多份绑定]
 *             M512 走 Modbus 有 `XVfdM512_modbusBinding()`；将来走 EtherCAT 再加
 *             `XVfdM512_ethercatBinding()`，两份并存互不干扰。
 */
#ifndef XVFDBIND_H
#define XVFDBIND_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XVfd_config.h"

#if XVFD_ON

#include <stdint.h>
#include <stdbool.h>

/* ========================================================================== */
/*                      点绑定                                                */
/* ========================================================================== */

/**
 * @brief 一条点绑定：语义角色 → 总线原生地址。
 * @details `m_role` 与品牌语义表里的 role 一一对应（装配期据此把两者配对）。
 *          地址的具体编码由链路定义（Modbus 是寄存器号，CAN 是帧 ID+位偏移）。
 */
typedef struct XVfdPointBinding {
    uint16_t m_role;        /**< 语义槽号（必须与语义表中的 role 对上）。 */
    uint16_t m_address;     /**< 总线原生地址（Modbus 虚拟地址 / CAN ID / GPIO 行号）。 */
    uint8_t  m_bitOffset;   /**< 位域起始位（0=整字）。 */
    uint8_t  m_bitWidth;    /**< 位域宽度（0=整字）。 */
} XVfdPointBinding;

/**
 * @brief 一条参数绑定：参数表下标 → 总线原生地址。
 * @details 参数与语义点的区别：参数是低频按需读写，不进常规轮询组。
 */
typedef struct XVfdParamBinding {
    uint16_t m_paramId;     /**< 参数描述表下标（0 基，与 XVfdParamDesc 数组同序）。 */
    uint16_t m_addr;        /**< 参数的原生地址。 */
    uint16_t m_addrRam;     /**< 「只改 RAM 不落盘」地址变体（0=无变体）。 */
} XVfdParamBinding;

/* ========================================================================== */
/*                      通信组与从站约束                                      */
/* ========================================================================== */

/**
 * @brief 通信组：从站「功能码组」的远端地址区间（品牌手册知识）。
 * @details 部分变频器只支持 FC03/FC06，且规定「一帧读不得跨过本功能码组的
 *          最后一个功能码」——功能码组是**手册条目**，不是地址连续性。
 *          装配层按组切映射段，协议层据此规划读帧——读帧因此天然不跨组。
 * @note 组内允许一次读帧跨多个组内切分；组间绝不合并。
 */
typedef struct XVfdCommGroup {
    uint16_t m_remoteStart; /**< 组起始远端地址。 */
    uint16_t m_count;       /**< 组长度（点数；寄存器区=字，位区=位）。 */
    uint8_t  m_area;        /**< 组所属区（XIoPointType 口径）。 */
} XVfdCommGroup;

/**
 * @brief 从站协议约束（中立表达；装配层翻译成传输层配置）。
 * @details 描述「这个从站比通用 Modbus 更严」的哪些方面。本结构刻意**不含
 *          任何传输层类型**，设备层因此仍不认识 Modbus。
 * @note 作用域是**从站**不是链路：一条 RTU 串口下可挂多个从站，各从站能力
 *       互不相同，约束必须与映射目标一一对应。
 */
typedef struct XVfdUnitProtocol {
    uint16_t m_maxReadRegisters; /**< 单次读帧寄存器数上限（0=不限）。 */
    bool     m_rangeBarrier;     /**< 一帧读不得跨通信组边界。 */
    bool     m_singleWriteOnly;  /**< 只支持单寄存器写。 */
} XVfdUnitProtocol;

/* ========================================================================== */
/*                      绑定表                                                */
/* ========================================================================== */

/**
 * @brief 一份完整的绑定表（一个品牌在一条总线上的全部地址知识）。
 * @details 由品牌子类的 `*_<Bus>.c` 导出，装配层消费。所有指针均为静态
 *          const 借用，调用方不得释放。
 */
typedef struct XVfdBindTable {
    const XVfdPointBinding* m_points;  /**< 点绑定表。 */
    int m_pointCount;                  /**< 点绑定条目数。 */
    const XVfdParamBinding* m_params;  /**< 参数绑定表；可为 NULL。 */
    int m_paramCount;                  /**< 参数绑定条目数。 */
    const XVfdCommGroup* m_groups;     /**< 通信组表；可为 NULL。 */
    int m_groupCount;                  /**< 通信组数。 */
    const XVfdUnitProtocol* m_protocol;/**< 从站约束；可为 NULL=无特殊约束。 */
} XVfdBindTable;

/**
 * @brief 按 role 在绑定表中查地址（线性查，装配期一次性使用）。
 * @param bind 绑定表；不能为 NULL。
 * @param role 语义角色。
 * @param outBinding 输出绑定条目；不能为 NULL。
 * @return 找到返回 true。
 * @note 装配是启动期一次性动作，此处线性查可接受；运行期数据路径
 *       走 XIoLink 的 O(1) 映射，与本函数无关。
 */
bool XVfdBind_findPoint(const XVfdBindTable* bind, uint16_t role,
                        XVfdPointBinding* outBinding);

/**
 * @brief 按 paramId 在绑定表中查地址（线性查，装配期一次性使用）。
 * @param bind 绑定表；不能为 NULL。
 * @param paramId 参数表下标。
 * @param outBinding 输出绑定条目；不能为 NULL。
 * @return 找到返回 true。
 */
bool XVfdBind_findParam(const XVfdBindTable* bind, uint16_t paramId,
                        XVfdParamBinding* outBinding);

#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVFDBIND_H */
