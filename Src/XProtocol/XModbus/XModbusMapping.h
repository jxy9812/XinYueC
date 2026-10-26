/**
 * @file       XModbusMapping.h
 * @brief      Modbus 聚合设备映射管理器（统一虚拟地址空间 ↔ 多底层设备翻译）。
 * @details    [XModbus 聚合设备 2026-10-26] 把多个 Modbus 从站的地址空间映射到
 *             一套统一虚拟地址上：每条映射段描述「虚拟地址 vStart..+count-1
 *             ↔ 设备 deviceId 的 unitId 从站的远端类型 remoteStart..+count-1」。
 *             四类寄存器区（离散输入/线圈/输入寄存器/保持寄存器）各自独立
 *             编址、独立段表。职责只有翻译与段表管理：重叠校验、按虚拟区间
 *             拆分切片（跨设备读/写拆分）、按设备收集段（轮询计划）。
 *             任何 IO 与定时器都不在本类——见 XModbusHub。
 *             线程模型：与库一致的单线程事件循环，零锁。
 */
#ifndef XMODBUSMAPPING_H
#define XMODBUSMAPPING_H
#include "XModbus_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XMODBUS_ON
#if XMODBUS_CLIENT_ON
#if XMODBUS_MAPPING_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XObject.h"
#include "XModbusDataUnit.h"

/**
 * @brief 一条映射段：统一虚拟地址空间的一段 ↔ 某设备某从站的同宽远端段。
 */
typedef struct XModbusMapRange {
    uint16_t virtualStart;      /**< 统一虚拟空间起始地址。 */
    uint16_t count;             /**< 段长（点数；位区=位，寄存器区=字）。 */
    int deviceId;               /**< 底层设备句柄（XModbusHub_addDevice 返回值）。 */
    int unitId;                 /**< 底层从站地址（Modbus unit id）。 */
    uint16_t remoteStart;       /**< 远端起始地址。 */
} XModbusMapRange;

/**
 * @brief 一次虚拟区间拆分出的切片：段 + 区间与段的交集信息。
 * @details 虚拟区间 [reqStart, reqStart+reqCount) 与段交集的本地(虚拟)起点、
 *          长度、以及对应的远端起点，调用方无需再做区间运算。
 *          type 携带切片所属寄存器类别（Hub 轮询计划/回填按类型分派）。
 */
typedef struct XModbusMapSegment {
    uint16_t type;              /**< XModbusRegisterType（切片所属寄存器类别）。 */
    XModbusMapRange range;      /**< 命中的映射段（完整拷贝）。 */
    uint16_t virtualStart;      /**< 交集在统一空间的起点。 */
    uint16_t remoteStart;       /**< 交集在远端的起点。 */
    uint16_t count;             /**< 交集长度。 */
} XModbusMapSegment;

XCLASS_DEFINE_BEGING(XModbusMapping)
XCLASS_DEFINE_EXTEND_END(XModbusMapping, XClass)

/**
 * @brief Modbus 映射管理器结构体（继承 XClass 值语义容器——纯翻译表，
 *        无信号/定时器需求，不背 XObject 的信号机制开销；
 *        需要信号与定时器的聚合主体见 XModbusHub（XObject））。
 */
typedef struct XModbusMapping {
    XClass m_class;                     /**< 基类（第一成员）。 */
    struct XVector* m_ranges[4];        /**< 每寄存器类型一张段表（XModbusMapRange，按 virtualStart 升序）。 */
} XModbusMapping;

/* ==================== 类初始化 / 构造 ==================== */
XVtable* XModbusMapping_class_init(void);
void XModbusMapping_init(XModbusMapping* map);
XModbusMapping* XModbusMapping_create_ex(XMemoryType memory);
#define XModbusMapping_create() XModbusMapping_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 段表管理 ==================== */
/**
 * @brief 增加一条映射段（同类型内虚拟区间与既有段重叠则拒绝）。
 * @return 成功 true；参数非法 / count==0 / 虚拟区间重叠 / 跨 0xFFFF 回绕 false。
 */
bool XModbusMapping_addRange(XModbusMapping* map, XModbusRegisterType type,
                             uint16_t virtualStart, uint16_t count,
                             int deviceId, int unitId, uint16_t remoteStart);
/** @brief 删除虚拟起点为 virtualStart 的段（存在返回 true）。 */
bool XModbusMapping_removeRange(XModbusMapping* map, XModbusRegisterType type,
                                uint16_t virtualStart);
/** @brief 清空指定类型段表；type 传 XModbusInvalid 清空全部四类。 */
void XModbusMapping_clearAreas(XModbusMapping* map, XModbusRegisterType type);
/** @brief 指定类型段数；type 非法返回 0。 */
size_t XModbusMapping_rangeCount_const(const XModbusMapping* map, XModbusRegisterType type);
/** @brief 按下标取段（越界返回 NULL；只读借用，段表变更后失效）。 */
const XModbusMapRange* XModbusMapping_rangeAt_const(const XModbusMapping* map,
                                                    XModbusRegisterType type, size_t index);
/** @brief 按虚拟起点查段（二分；未命中返回 NULL）。 */
const XModbusMapRange* XModbusMapping_rangeAtVirtual_const(const XModbusMapping* map,
                                                           XModbusRegisterType type,
                                                           uint16_t virtualStart);

/* ==================== 翻译 ==================== */
/**
 * @brief 把虚拟区间 [virtualStart, virtualStart+count) 沿段表拆分为切片。
 * @param out 切片输出数组（调用方提供栈/堆数组）
 * @param outCap out 容量
 * @return 写入的切片数；区间无任何映射返回 0；outCap 不足返回 -需要的容量。
 * @note 区间中存在「洞」（未映射地址）时跳过洞继续——洞不对应用答数据，
 *       由调用方决定语义（Hub 的缓存读对洞返回 false）。
 */
int XModbusMapping_resolveSegments_const(const XModbusMapping* map,
                                         XModbusRegisterType type,
                                         uint16_t virtualStart, uint16_t count,
                                         XModbusMapSegment* out, int outCap);
/**
 * @brief 收集某设备在某类型下的全部段（轮询计划 / 按设备操作用）。
 * @return 写入的段数；outCap 不足返回 -需要的数量。
 */
int XModbusMapping_segmentsForDevice_const(const XModbusMapping* map,
                                           int deviceId, XModbusRegisterType type,
                                           XModbusMapRange* out, int outCap);
/** @brief 某设备在某类型下是否至少有一条段（小开销存在性查询）。 */
bool XModbusMapping_deviceHasArea_const(const XModbusMapping* map,
                                        int deviceId, XModbusRegisterType type);

/* 释放：XModbusMapping 继承 XClass（非 XObject），统一 XClassDelete/XClassDeinit。 */

#endif /* XMODBUS_MAPPING_ON */
#endif /* XMODBUS_CLIENT_ON */
#endif /* XMODBUS_ON */
#ifdef __cplusplus
}
#endif
#endif /* XMODBUSMAPPING_H */
