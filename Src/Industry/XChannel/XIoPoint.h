/**
 * @file       XIoPoint.h
 * @brief      轻量 IO 点基类（XClass 直派生，非 XObject；数据来源经虚槽由
 *             子类实现）。
 * @details    [2026-11-01 轻量化] 完整 XObject 基类 136B/对象，其中父子
 *             关系、动态属性、事件过滤器、对象名等成员对 IO 点终生无用。
 *             本类继承 XClass（16B：vtable + 内存标记 + is_heap），获得
 *             vtable 多态能力与 XClassDelete 统一释放。
 *             [数据来源契约] 点的数据来源由**子类重写虚槽**实现：
 *               - ReadRaw：拉取当前点值（子类定义来源：读缓存/读寄存器）；
 *               - WriteRaw：写受理（子类定义传输动作）。
 *             值口径 = 点单位（DI/DO: 0/1；AI/AO: 定点工程值），与 m_value
 *             同语义——组 tick 拉取与子类推送共用同一口径，天然静默去重。
 *             组（XIoChannelGroup）周期调 ReadRaw 拉取做变化检测；推送型
 *             子类（如 Hub 点订阅缓存信号）经保护 API xIoPointFeedValue
 *             直接喂新值（见 XIoPoint_Protected.h）。写落定经
 *             xIoPointWriteDone 转发组 writeDone 信号。
 *             [生命周期] 两档：m_autoDelete=0（缺省）由调用方创建/释放；
 *             m_autoDelete=1 内存交由登记的 XIoChannelGroup 自动释放
 *             （remove/组析构时 XClassDelete，调用方不得再删）。
 *             [地址] 点不携带地址：地址属传输子类（如 XModbusPoint 的
 *             Hub 映射区地址），基点只承载值与状态。
 *             线程模型：与框架一致单线程事件循环。
 */
#ifndef XIOPOINT_H
#define XIOPOINT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XIndustry_config.h"
#include "XClass.h"
#include <stdint.h>

#if XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON

/* 前向声明（组与点互相引用）。 */
typedef struct XIoChannelGroup XIoChannelGroup;

/**
 * @brief 点类型（决定数据语义与组分发的值解释）。
 */
typedef enum {
    XIoPoint_DigitalInput = 1,    /**< 离散量输入（只读；值 0/1）。 */
    XIoPoint_DigitalOutput = 2,   /**< 离散量输出（可写；值 0/1）。 */
    XIoPoint_AnalogInput = 3,     /**< 寄存器输入（只读；值为定点工程值）。 */
    XIoPoint_AnalogOutput = 4     /**< 寄存器输出（可写；值为定点工程值）。 */
} XIoPointType;

/* 点类虚槽枚举（首槽从父类槽位数起编号）。 */
XCLASS_DEFINE_BEGING(XIoPoint)
XCLASS_DEFINE_ENUM(XIoPoint, ReadRaw) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XIoPoint, WriteRaw),
XCLASS_DEFINE_END(XIoPoint)

/**
 * @brief XIoPoint 轻量 IO 点基类（48B）。
 * @details DI/DO 直接用本类；AI/AO 用派生类 XIoAnalogPoint（头嵌入 + 标度）。
 */
typedef struct XIoPoint {
    XClass m_class;         /**< 第一成员：继承 XClass（vtable + 堆标记），XClassDelete 统一释放。 */
    XIoChannelGroup* m_group; /**< 登记的所属组（借用；addPoint 时回填，NULL=未登记）。仅供实现使用。 */
    const char* m_tag;      /**< 点名（借用，不拥有；如 "DI3"、"温度AI1"）。可为 NULL。仅供实现使用。 */
    int32_t m_value;        /**< 当前值：DI/DO 取 0/1；AI/AO 为定点工程值（AI 兼死区比较基准）。仅供实现使用。 */
    uint16_t m_type;        /**< 点类型（XIoPointType）。仅供实现使用。 */
    union {
        uint16_t m_raw;     /**< 位域原始值。 */
        struct {
            uint16_t m_valid : 1;     /**< 数据有效：推送/拉取到首值或写落定后置位。 */
            uint16_t m_valueB : 1;    /**< 当前逻辑值位（DI/DO；AI/AO 不用）。 */
            uint16_t m_hasValue : 1;  /**< 是否已推送过初值。 */
            uint16_t m_useHolding : 1;/**< AI 区选（子类语义）：1=HR；0=IR。 */
            uint16_t m_autoDelete : 1;/**< 自动内存管理：开启后内存交由登记的 XIoChannelGroup 释放（remove/组析构时 XClassDelete），调用方不得再删。 */
            uint16_t m_reserved : 11; /**< 保留位，必须为 0。 */
        } bits;
    } m_flags;              /**< 状态位域。仅供实现使用。 */
    uint16_t m_reserved;    /**< 对齐保留。仅供实现使用。 */
} XIoPoint;

/* ==================== 虚函数表初始化 ==================== */

/**
 * @brief 初始化 XIoPoint 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 */
XVtable* XIoPoint_class_init(void);

/**
 * @brief 栈上初始化 IO 点（清尾段 + 设虚表 + 默认状态）。
 * @param point 目标点；不能为 NULL，可为未初始化内存。
 * @return 无。
 * @note 与 XClassDeinit 成对出现。
 */
void XIoPoint_init(XIoPoint* point);

/* ==================== 自动内存管理 ==================== */

/**
 * @brief 设置自动内存管理标志（位域 m_autoDelete）。
 * @param point 目标点；不能为 NULL。
 * @param autoDelete true=内存交由登记的 XIoChannelGroup 自动释放
 *                   （remove/组析构时 XClassDelete，调用方不得再删）；
 *                   false=调用方自管（缺省）。
 * @return 无。
 * @note 建议在 addPoint 前设置；登记后切换只影响其后的 remove/组析构。
 */
void XIoPoint_setAutoDelete(XIoPoint* point, bool autoDelete);

/**
 * @brief 查询自动内存管理标志。
 * @param point 目标点；NULL 返回 false。
 * @return true=组托管释放；false=调用方自管。
 */
bool XIoPoint_autoDelete(const XIoPoint* point);

/* ==================== 虚槽直调入口（组分发/业务调用） ==================== */

/**
 * @brief ReadRaw 虚槽直调：拉取当前点值（子类实现数据来源）。
 * @param self 目标点；不能为 NULL。
 * @param outValue 输出点值（点单位）；不能为 NULL。
 * @return true=取到有效值；false=无数据/未实现。
 * @note 基类默认实现返回 false。
 */
bool XIoPoint_readRaw_base(XIoPoint* self, int32_t* outValue);

/**
 * @brief WriteRaw 虚槽直调：写受理（子类实现传输动作）。
 * @param self 目标点；不能为 NULL。
 * @param value 目标点值（点单位）。
 * @return true=受理；false=拒绝/未实现。
 * @note 基类默认实现返回 false。
 */
bool XIoPoint_writeRaw_base(XIoPoint* self, int32_t value);

/* ==================== 成员访问 API ==================== */

/**
 * @brief 读点名（借用）。
 * @param point 目标点；NULL 返回 NULL。
 * @return 点名；未设置返回 NULL。
 */
const char* XIoPoint_tag(const XIoPoint* point);

/**
 * @brief 读点类型。
 * @param point 目标点；NULL 返回 DigitalInput。
 * @return 点类型。
 */
XIoPointType XIoPoint_type(const XIoPoint* point);

/**
 * @brief 读所属组（借用）。
 * @param point 目标点；NULL 返回 NULL。
 * @return 登记的通道组；未登记返回 NULL。
 */
XIoChannelGroup* XIoPoint_group(const XIoPoint* point);

/**
 * @brief 读当前点值（DI/DO 0/1；AI/AO 工程值）。
 * @param point 目标点；NULL 返回 0。
 * @return 当前缓存值。
 */
int32_t XIoPoint_value(const XIoPoint* point);

/**
 * @brief 读数据有效标志。
 * @param point 目标点；NULL 返回 false。
 * @return true=数据可信。
 */
bool XIoPoint_isValid(const XIoPoint* point);

/**
 * @brief 设置数据有效标志（一般由数据源子类维护，业务层罕用）。
 * @param point 目标点；不能为 NULL。
 * @param valid true=数据可信。
 * @return 无。
 */
void XIoPoint_setValid(XIoPoint* point, bool valid);

/**
 * @brief 读初值推送标志。
 * @param point 目标点；NULL 返回 false。
 * @return true=已推送过值。
 */
bool XIoPoint_hasValue(const XIoPoint* point);

/**
 * @brief 读 AI 区选（子类语义）。
 * @param point 目标点；NULL 返回 false。
 * @return true=HR 区；false=IR 区。
 */
bool XIoPoint_useHolding(const XIoPoint* point);

/**
 * @brief 设置 AI 区选（子类语义）。
 * @param point 目标点；不能为 NULL。
 * @param useHolding true=HR 区；false=IR 区。
 * @return 无。
 */
void XIoPoint_setUseHolding(XIoPoint* point, bool useHolding);

/* 组对接助手（xIoPointFeedValue / xIoPointWriteDone）为保护 API，
 * 声明见 XIoPoint_Protected.h——仅供点子类与组内部实现使用。 */

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIOPOINT_H */
