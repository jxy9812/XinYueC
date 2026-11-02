/**
 * @file       XVfdCiA402.h
 * @brief      通用 CiA402 / Drivecom 状态机骨架基类公开 API。
 * @details    一部分变频器走 CiA402（Drivecom）状态机：命令字按
 *              「Shutdown → Switch on → Enable operation」逐级推进，
 *              状态字按状态域回报当前档位。本类把这条状态迁移骨架上提为
 *              **profile 表驱动**的通用基类，品牌子类只需给出一张
 *              XVfdCiA402Profile（状态掩码/状态码/命令字/状态位/时序），
 *              即可获得统一的启动序列、停机族命令、故障复位序列、
 *              状态字解码与状态→XVfdRunState 映射。
 * @note       与通用离散命令枚举骨架（XVfdCommandEnum）是**并列**的两条命令
 *              模型分支：CiA402 是「状态迁移序列」，CommandEnum 是「单字枚举」。
 *              两者都直接继承 XVfdDevice，语义层（快照/能力位图/信号/参数
 *              通道）完全共用。
 */
#ifndef XVFDCIA402_H
#define XVFDCIA402_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XVfd_config.h"

#if XVFD_ON
#if XVFD_CIA402_ON

#include <stdint.h>
#include <stdbool.h>
#include "XVfdDevice.h"

/* ========================================================================== */
/*                CiA402 状态码槽位（profile m_states 数组下标）              */
/* ========================================================================== */

/** @brief 状态码槽位：Switch on disabled（未上电/被禁止）。 */
#define XVFD_CIA402_STATE_DISABLED  0
/** @brief 状态码槽位：Ready to switch on（就绪）。 */
#define XVFD_CIA402_STATE_READY     1
/** @brief 状态码槽位：Switched on（已合闸，未使能运行）。 */
#define XVFD_CIA402_STATE_SWITCHED  2
/** @brief 状态码槽位：Operation enabled（运行中）。 */
#define XVFD_CIA402_STATE_RUNNING   3
/** @brief 状态码槽位：Quick stop active（快速停机中）。 */
#define XVFD_CIA402_STATE_QUICKSTOP 4
/** @brief 状态码槽位：Fault（故障）。 */
#define XVFD_CIA402_STATE_FAULT     5
/** @brief 状态码槽位数（m_states 数组长度）。 */
#define XVFD_CIA402_STATE_COUNT     6

/* ========================================================================== */
/*                类型定义                                                    */
/* ========================================================================== */

/**
 * @brief CiA402 品牌画像表：状态机骨架的全部品牌知识入口。
 * @details 骨架本身不含任何品牌常量：状态域掩码、六个状态码、命令字取值、
 *          状态位、序列时序全部由本表给出。品牌子类给一张静态 const 表
 *          （借用，不拥有）即可驱动通用骨架。
 * @note 数值语义与 CiA402 标准一致（状态域掩码 + 状态码；命令字位定义）。
 */
typedef struct XVfdCiA402Profile {
    uint16_t m_stateMask;      /**< 状态字的状态域掩码（取出状态码用）。 */
    uint16_t m_states[XVFD_CIA402_STATE_COUNT]; /**< 六档状态码（顺序见
                                     XVFD_CIA402_STATE_* 下标）。 */
    uint16_t m_cmdShutdown;    /**< 命令字：Shutdown（→ Ready）。 */
    uint16_t m_cmdSwitchOn;    /**< 命令字：Switch on（→ Switched）。 */
    uint16_t m_cmdRun;         /**< 命令字：Enable operation（→ Running）。 */
    uint16_t m_cmdDisableOp;   /**< 命令字：Disable operation（受控停机）。 */
    uint16_t m_cmdDisableVoltage; /**< 命令字：Disable voltage（自由停车）。 */
    uint16_t m_cmdQuickStop;   /**< 命令字：Quick stop（快速停机）。 */
    uint16_t m_cmdFaultReset;  /**< 命令字：Fault reset（上升沿）。 */
    uint16_t m_cmdReverse;     /**< 命令字方向位（1 = 反转；按位或）。 */
    uint16_t m_bitRunning;     /**< 状态位：运行中。 */
    uint16_t m_bitFault;       /**< 状态位：故障。 */
    uint16_t m_bitWarning;     /**< 状态位：警告。 */
    uint16_t m_bitRemote;      /**< 状态位：命令来自总线。 */
    uint16_t m_bitTargetReached; /**< 状态位：已达到给定。 */
    uint16_t m_bitReverse;     /**< 状态位：反转方向。 */
    uint8_t  m_maxStep;        /**< 启动序列最大步数（超过判超时）。 */
    uint8_t  m_stepDelayMs;    /**< 写落定后的步进延时（毫秒）。 */
    uint16_t m_pollLimit;      /**< 期望态轮询拍数上限（超过判超时）。 */
    uint16_t m_pollIntervalMs; /**< 期望态轮询间隔（毫秒）。 */
} XVfdCiA402Profile;

/** @brief 通用 CiA402 骨架类型前置声明（供下方回调类型引用）。 */
struct XVfdCiA402;

XCLASS_DEFINE_BEGING(XVfdCiA402)
XCLASS_DEFINE_ENUM(XVfdCiA402, DecodeBrand) = XCLASS_VTABLE_GET_SIZE(XVfdDevice),
XCLASS_DEFINE_END(XVfdCiA402)

/**
 * @brief 通用 CiA402 状态机骨架基类结构体（继承 XVfdDevice）。
 * @details 品牌子类把本结构作为第一成员；profile 为静态 const 借用。
 */
typedef struct XVfdCiA402 {
    XVfdDevice m_base;                   /**< 基类子对象（第一成员）。 */
    const XVfdCiA402Profile* m_profile;   /**< 品牌画像表（静态 const，借用；禁释放）。 */
    int m_step;                          /**< 启动序列步进计数。 */
    bool m_reverse;                      /**< 启动方向（true=反转）。 */
    bool m_waitingWrite;                 /**< 已发命令字、等写落定。 */
    bool m_pendingReset;                 /**< 故障复位序列：待写归位命令。 */
    uint16_t m_pendingCmd;               /**< 待写命令字。 */
    uint16_t m_etaCache;                 /**< 最近状态字原始值。 */
    uint16_t m_auxCache;                 /**< 最近辅助状态字原始值。 */
    uint16_t m_expectEta;                /**< 当前步期望的状态码。 */
    int m_waitPolls;                     /**< 当前步等待状态迁移的轮询次数。 */
    uint32_t m_stepDelayMs;              /**< 序列步进延时（毫秒；默认取 profile）。 */
} XVfdCiA402;

/**
 * @brief 品牌重载解码虚槽函数类型。
 * @param dev 设备实例（XVfdCiA402*；不能为 NULL）。
 * @param status 状态字原始值（掩码前）。
 * @param aux 辅助状态字原始值（品牌辅助位；未接入时为 0）。
 * @param st 可变状态快照（子类可改写运行态与品牌专有字段）。
 * @return 无。
 * @note 通用骨架已完成「运行/故障/警告/反转/到位/远端」与「状态→XVfdRunState」
 *       映射；本槽供品牌补齐自有的辅助位语义（如加速中/减速中/直流制动）。
 */
typedef void (*XVfdCiA402DecodeBrandFunc)(XVfdCiA402* dev, uint16_t status,
                                          uint16_t aux, XVfdStatus* st);

/* ==================== 类生命周期 ==================== */

/**
 * @brief 获取 XVfdCiA402 类虚函数表。
 * @return 指向初始化完成的 XVtable 的指针
 */
XVtable* XVfdCiA402_class_init(void);

/**
 * @brief 初始化实例（栈上使用；与 XClassDeinit 成对）。
 * @param dev 实例（不能为 NULL）
 * @note 初始化后 m_profile 为 NULL；品牌子类须紧接着调用
 *       XVfdCiA402_setProfile 挂画像表（否则骨架无状态域可用）。
 */
void XVfdCiA402_init(XVfdCiA402* dev);

/* ==================== 画像表与配置 ==================== */

/**
 * @brief 挂品牌画像表（静态 const，借用不拥有）。
 * @param dev 实例；不能为 NULL。
 * @param profile 画像表；不能为 NULL（传 NULL 视为解除挂接）。
 * @return 无。
 * @note 挂接时把步进延时初始化为 profile 的缺省值（可再用
 *       XVfdCiA402_setStepDelay 覆盖）。
 */
void XVfdCiA402_setProfile(XVfdCiA402* dev, const XVfdCiA402Profile* profile);

/**
 * @brief 取品牌画像表。
 * @param dev 实例（可 NULL）。
 * @return 画像表指针（静态常量，禁释放）；未挂接返回 NULL。
 */
const XVfdCiA402Profile* XVfdCiA402_profile(const XVfdCiA402* dev);

/**
 * @brief 设置启动序列步进延时（每写一条命令字后的等待）。
 * @param dev 实例；不能为 NULL。
 * @param delayMs 延时毫秒（0 = 回到画像表缺省值）。
 */
void XVfdCiA402_setStepDelay(XVfdCiA402* dev, uint32_t delayMs);

/**
 * @brief 取启动序列步进延时。
 * @param dev 实例（可 NULL，返回 0）。
 * @return 延时毫秒
 */
uint32_t XVfdCiA402_stepDelay(const XVfdCiA402* dev);

#endif /* XVFD_CIA402_ON */
#endif /* XVFD_ON */

#ifdef __cplusplus
}
#endif

#endif /* XVFDCIA402_H */
