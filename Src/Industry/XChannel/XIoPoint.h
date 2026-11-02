/**
 * @file       XIoPoint.h
 * @brief      工业 IO 点：纯数据容器（无虚槽、无链路指针）。
 * @details    [2026-11-02 XChannel 重写] 本类退化为**纯数据容器**，只承载
 *             「语义槽号 + 值与状态 + 标度」，不知道自己连的是 Modbus、
 *             CAN、EtherCAT 还是 GPIO。
 *
 *             [为什么这样切] 旧实现让点自己持 XModbusHub* 并实现
 *             ReadRaw/WriteRaw 虚槽，于是每加一条链路就要新写一个点类
 *             （XModbusPoint / XCanPoint / XEtherCatPoint × 四种数据类型），
 *             点类数量随链路数线性膨胀。现在链路能力全部收进 XIoLink，
 *             点只存数据 ⇒ **加链路时点类一行不改**。
 *
 *             [零遍历] 点的身份是 m_role（语义槽号，0 基连续）。通道组按
 *             role 作下标密集存放点指针，设备层按 role 作下标取槽，
 *             两个方向都是 O(1) 数组取址，没有线性查找。
 *
 *             [寻址] 点不携带链路地址。地址由 XIoLink 在装配期按
 *             (role → 链路地址) 登记，读写时由 link 自行解析。
 *
 *             [所有权] m_autoDelete=0（缺省）由调用方创建/释放；
 *             =1 内存交由登记的 XIoChannelGroup 自动释放。
 *
 *             [标度] AI: 工程值 = 原生值 * m_scaleNum / m_scaleDen + m_offset
 *                    AO: 原生值 = (工程值 - m_offset) * m_scaleDen / m_scaleNum
 *             m_scaleDen/m_scaleNum 为 0 时按 1 处理。m_deadband 仅 AI 消费。
 *
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

/** @brief 通道组支持的 role 上界（role 作下标 ⇒ 容量上界）。
 *  @note  XVfd 当前用到 68 个语义角色，此处留足余量；role >= 本值一律拒绝。 */
#ifndef XIOCHANNELGROUP_ROLE_CAP
#define XIOCHANNELGROUP_ROLE_CAP 256
#endif

/**
 * @brief 点类型（决定数据语义与链路层区的解释）。
 */
typedef enum {
    XIoPoint_DigitalInput = 1,    /**< 离散量输入（只读；值 0/1）。 */
    XIoPoint_DigitalOutput = 2,   /**< 离散量输出（可写；值 0/1）。 */
    XIoPoint_AnalogInput = 3,     /**< 寄存器输入（只读；值为定点工程值）。 */
    XIoPoint_AnalogOutput = 4     /**< 寄存器输出（可写；值为定点工程值）。 */
} XIoPointType;

/* 类虚表声明：本类无自定义虚槽（纯数据容器），只继承 XClass 基段。 */
XCLASS_DEFINE_BEGING(XIoPoint)
XCLASS_DEFINE_EXTEND_END(XIoPoint, XClass)

/**
 * @brief 工业 IO 点（纯数据容器；无虚槽）。
 * @details 成员禁止直接访问，一律经 XIoPoint_* 与 XIoChannelGroup_* API。
 *          仅 XIoChannelGroup 与 XIoLink 的实现文件可经 XIoPoint_Protected.h
 *          的助手函数写入值/状态。
 */
typedef struct XIoPoint {
    XClass m_class;            /**< 第一成员：继承 XClass（vtable + 堆标记）。 */
    int32_t m_value;           /**< 当前值：DI/DO 取 0/1；AI/AO 为定点工程值。 */
    const char* m_tag;         /**< 点名（借用，不拥有；如 "DI3"）。可为 NULL。 */
    int32_t m_scaleNum;        /**< 标度分子（0 按 1）。 */
    int32_t m_scaleDen;        /**< 标度分母（0 按 1）。 */
    int32_t m_offset;          /**< 工程值偏移量。 */
    int32_t m_deadband;        /**< AI 变化死区（工程值）；负值按 0；AO 不消费。 */
    uint16_t m_role;           /**< 语义槽号（组内下标；决定 O(1) 存放位置）。 */
    uint16_t m_type;           /**< 点类型（XIoPointType）。 */
    union {
        uint16_t m_raw;        /**< 位域原始值。 */
        struct {
            uint16_t m_valid : 1;      /**< 数据有效：推送/拉取到首值或写落定后置位。 */
            uint16_t m_valueB : 1;     /**< 当前逻辑值位（DI/DO；AI/AO 不用）。 */
            uint16_t m_hasValue : 1;   /**< 是否已推送过初值。 */
            uint16_t m_useHolding : 1; /**< AI 区选：1=HR；0=IR。 */
            uint16_t m_autoDelete : 1; /**< 自动内存管理：内存交由组释放。 */
            uint16_t m_signed : 1;     /**< 原生量为 16 位有符号（补码）：写链路前
                                        *   负值按 0xFFFF 归一，而不是钳成 0。 */
            uint16_t m_reserved : 10;  /**< 保留位，必须为 0。 */
        } bits;
    } m_flags;                 /**< 状态位域。 */
} XIoPoint;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief 初始化 XIoPoint 类的虚函数表（单例）。
 * @return 指向初始化完成的 XVtable；不可释放。
 */
XVtable* XIoPoint_class_init(void);

/**
 * @brief 栈上初始化 IO 点（清零 + 设虚表 + 默认状态）。
 * @param point 目标点；不能为 NULL，可为未初始化内存。
 * @return 无。
 * @note 与 XClass_Deinit 成对出现。
 */
void XIoPoint_init(XIoPoint* point);

/**
 * @brief 堆上创建 IO 点（调用方负责 XClassDelete）。
 * @param memory 内存类型。
 * @return 新点；分配失败返回 NULL。
 */
XIoPoint* XIoPoint_create_ex(XMemoryType memory);
#define XIoPoint_create() XIoPoint_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 成员访问 ==================== */

/**
 * @brief 设置语义槽号。
 * @param point 目标点；NULL 时无操作。
 * @param role 语义槽号；须 < XIOCHANNELGROUP_ROLE_CAP。
 * @return 设置成功 true；role 越界返回 false。
 * @note 组登记的 O(1) 位置由 role 决定；已在组内的点改 role 会破坏索引，
 *       必须先摘除再改。
 */
bool XIoPoint_setRole(XIoPoint* point, uint16_t role);

/**
 * @brief 读语义槽号。
 * @param point 目标点；NULL 返回 0。
 * @return 语义槽号。
 */
uint16_t XIoPoint_role(const XIoPoint* point);

/**
 * @brief 设置点类型。
 * @param point 目标点；NULL 时无操作。
 * @param type 点类型。
 * @return 无。
 */
void XIoPoint_setType(XIoPoint* point, XIoPointType type);

/**
 * @brief 读点类型。
 * @param point 目标点；NULL 返回 DigitalInput。
 * @return 点类型。
 */
XIoPointType XIoPoint_type(const XIoPoint* point);

/**
 * @brief 设置点名（借用，不拷贝）。
 * @param point 目标点；NULL 时无操作。
 * @param tag 点名；可为 NULL。
 * @return 无。
 */
void XIoPoint_setTag(XIoPoint* point, const char* tag);

/**
 * @brief 读点名（借用）。
 * @param point 目标点；NULL 返回 NULL。
 * @return 点名；未设置返回 NULL。
 */
const char* XIoPoint_tag(const XIoPoint* point);

/**
 * @brief 设置标度四件套。
 * @param point 目标点；NULL 时无操作。
 * @param scaleNum 分子（0 按 1）。
 * @param scaleDen 分母（0 按 1）。
 * @param offset 偏移。
 * @param deadband 死区（负值按 0；仅 AI 消费）。
 * @return 无。
 */
void XIoPoint_setScale(XIoPoint* point, int32_t scaleNum, int32_t scaleDen,
                       int32_t offset, int32_t deadband);

/**
 * @brief 设置有符号标记（16 位补码语义）。
 * @param point 目标点；NULL 时无操作。
 * @param signedValue true=负值走补码归一。
 * @return 无。
 */
void XIoPoint_setSigned(XIoPoint* point, bool signedValue);

/**
 * @brief 查询有符号标记。
 * @param point 目标点；NULL 返回 false。
 * @return true=有符号。
 */
bool XIoPoint_isSigned(const XIoPoint* point);

/**
 * @brief 设置 AI 区选（HR/IR）。
 * @param point 目标点；NULL 时无操作。
 * @param useHolding true=HR 区；false=IR 区。
 * @return 无。
 */
void XIoPoint_setUseHolding(XIoPoint* point, bool useHolding);

/**
 * @brief 查询 AI 区选。
 * @param point 目标点；NULL 返回 false。
 * @return true=HR 区。
 */
bool XIoPoint_useHolding(const XIoPoint* point);

/**
 * @brief 设置自动内存管理。
 * @param point 目标点；NULL 时无操作。
 * @param autoDelete true=组托管释放。
 * @return 无。
 */
void XIoPoint_setAutoDelete(XIoPoint* point, bool autoDelete);

/**
 * @brief 查询自动内存管理。
 * @param point 目标点；NULL 返回 false。
 * @return true=组托管。
 */
bool XIoPoint_autoDelete(const XIoPoint* point);

/* ==================== 值查询（工程值口径） ==================== */

/**
 * @brief 读当前工程值（最近一次喂入/拉取的结果）。
 * @param point 目标点；NULL 返回 false。
 * @param outValue 输出工程值；不能为 NULL。
 * @return 有数据返回 true；无数据（从未喂入）返回 false。
 */
bool XIoPoint_value(const XIoPoint* point, int32_t* outValue);

/**
 * @brief 查询数据有效标志（推送/回填/写落定后置位）。
 * @param point 目标点；NULL 返回 false。
 * @return true=数据可信。
 */
bool XIoPoint_isValid(const XIoPoint* point);

/* ==================== 标度换算 ==================== */

/**
 * @brief 原生值 → 工程值：raw * scaleNum / scaleDen + offset。
 * @param point 目标点（提供标度）；不能为 NULL。
 * @param raw 原生值（链路层口径）。
 * @return 定点工程值（向零取整；scaleDen 0 按 1）。
 */
int32_t XIoPoint_toEngineering(const XIoPoint* point, int32_t raw);

/**
 * @brief 工程值 → 原生值：(value - offset) * scaleDen / scaleNum。
 * @param point 目标点（提供标度）；不能为 NULL。
 * @param value 定点工程值。
 * @return 原生值。**有符号点返回 16 位补码原码**（如 -2000 → 0xF830 的低 16 位）；
 *         无符号点负值钳 0、上限 65535。
 * @note [2026-11-02 根修] 旧实现先算绝对值再补符号、随后无条件清零，导致
 *       所有负值被静默钳成 0（负频率给定写不下去）。现按 m_signed 分流，
 *       符号语义只在本函数内定义一次。
 */
uint16_t XIoPoint_toRaw(const XIoPoint* point, int32_t value);

#endif /* XINDUSTRY_ON && XINDUSTRY_CHANNEL_ON */

#ifdef __cplusplus
}
#endif
#endif /* XIOPOINT_H */
