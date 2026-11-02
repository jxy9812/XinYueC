/**
 * @file       XVfdCommandEnum.h
 * @brief      离散命令枚举通用基类（变频器命令字 = 单字枚举码的家族）。
 * @details    一部分变频器没有 CiA402 状态机：命令是「写一个字 = 一个动作」
 *              （如某系列 2000H 写 1=正转 2=反转 3=正点动 4=反点动 5=自由停机
 *              6=减速停机 7=故障复位）。本类把这类「命令码表」上提为通用骨架，
 *              品牌子类只需给出命令码表（XVfdCommandEnumItem[]）与点表，
 *              即可获得统一的命令接口与单飞/落定语义。
 * @note       与通用 CiA402 骨架（XVfdCiA402）是**并列**的两条命令模型分支：
 *              CiA402 是「状态迁移序列」，本类是「单字枚举」。两者都直接继承
 *              XVfdDevice，语义层（快照/能力位图/信号/参数通道）完全共用。
 */
#ifndef XVFDCOMMANDENUM_H
#define XVFDCOMMANDENUM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_COMMANDENUM_ON

#include <stdint.h>
#include <stdbool.h>
#include "XVfdDevice.h"

/**
 * @brief 离散命令枚举项：品牌命令码 ↔ 语义动作。
 */
typedef struct XVfdCommandEnumItem {
    int16_t m_code;       /**< 品牌原生命令码（写命令字/命令寄存器的值）。 */
    uint16_t m_op;        /**< 对应 XVfdOperation（动作语义；决定 commandCompleted 携带值）。 */
    bool m_reverse;       /**< 是否为反转方向的命令（正/反成对时区分）。 */
} XVfdCommandEnumItem;

XCLASS_DEFINE_BEGING(XVfdCommandEnum)
XCLASS_DEFINE_EXTEND_END(XVfdCommandEnum, XVfdDevice)

/**
 * @brief 离散命令枚举基类结构体（继承 XVfdDevice）。
 * @details 品牌子类把本结构作为第一成员；命令码表为静态 const 借用。
 */
typedef struct XVfdCommandEnum {
    XVfdDevice m_base;                    /**< 基类子对象（第一成员）。 */
    const XVfdCommandEnumItem* m_items;   /**< 命令码表（静态 const，借用；禁释放）。 */
    int m_itemCount;                      /**< 命令码表条目数（0=无命令表）。 */
} XVfdCommandEnum;

/* ==================== 类生命周期 ==================== */

/**
 * @brief 获取 XVfdCommandEnum 类虚函数表。
 * @return 指向初始化完成的 XVtable 的指针
 */
XVtable* XVfdCommandEnum_class_init(void);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param dev 实例（不能为 NULL）
 */
void XVfdCommandEnum_init(XVfdCommandEnum* dev);

/* ==================== 命令码表 ==================== */

/**
 * @brief 挂品牌命令码表（静态 const，借用不拥有）。
 * @param dev 实例；不能为 NULL。
 * @param items 命令码表（可为 NULL = 无命令表）。
 * @param count 条目数（<=0 视为无命令表）。
 * @return 无。
 */
void XVfdCommandEnum_setCommands(XVfdCommandEnum* dev, const XVfdCommandEnumItem* items,
                                 int count);

/**
 * @brief 按动作与方向查命令码。
 * @param dev 实例（可 NULL，返回 false）。
 * @param op 动作类型（XVfdOperation）。
 * @param reverse 是否反转方向。
 * @param outCode 输出命令码（不能为 NULL）。
 * @return 命中返回 true；未命中返回 false。
 */
bool XVfdCommandEnum_findCode(const XVfdCommandEnum* dev, uint16_t op, bool reverse,
                               int16_t* outCode);

/* ==================== 配置 ==================== */

/**
 * @brief 取命令码表（装配期/诊断用）。
 * @param dev 实例（可 NULL）。
 * @param count 输出条目数（可为 NULL）。
 * @return 表格首地址（静态常量，禁释放）；无表返回 NULL。
 */
const XVfdCommandEnumItem* XVfdCommandEnum_items_const(const XVfdCommandEnum* dev,
                                                        int* count);

#endif /* XVFD_COMMANDENUM_ON */
#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif

#endif /* XVFDCOMMANDENUM_H */
