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
/**
 * @brief 映射段属性标志（XModbusMapRange::flags 位掩码）。
 */
typedef enum XModbusMapRangeFlag {
    /** 只写段：远端该段可写不可读（厂商从站的命令字/参数写入区）。
     *  写路径依赖该段的映射与缓存块，故段仍要建；但**不参与轮询**——
     *  读它只会让从站回错误帧，白白赔一个超时窗口并抬高轮询失败计数
     *  （失败计数会触发退避，把写回确认饿死）。 */
    XModbusMapRange_WriteOnly = 0x01
} XModbusMapRangeFlag;

typedef struct XModbusMapRange {
    uint16_t virtualStart;      /**< 统一虚拟空间起始地址。 */
    uint16_t count;             /**< 段长（点数；位区=位，寄存器区=字）。 */
    int deviceId;               /**< 底层设备句柄（XModbusHub_addDevice 返回值）。 */
    uint16_t unitId;            /**< 底层从站地址（Modbus unit id；uint16 收紧段表布局）。 */
    uint16_t remoteStart;       /**< 远端起始地址。 */
    uint8_t  flags;             /**< 段属性标志（XModbusMapRangeFlag 位掩码）：
                                     XMODBUS_MAP_RANGE_WRITE_ONLY = 只写段
                                     （可写不可读：写路径需要它，但**不参与轮询**
                                      ——厂商从站的写区读了只会回错误帧）。 */
    uint16_t defValue;          /**< 段默认值（输出上线对齐用）：块首次物化时
                                     播种进缓存（仅未初始化点），故首启缓存
                                     即默认值、上线下发即默认值清残留；位区
                                     非 0 视为 1，addRange 末参指定，缺省 0。
                                     重连默认走缓存回灌（Hub setOutputSyncMode
                                     可改默认值覆盖）。 */
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
/**
 * @brief 初始化 XModbusMapping 类的虚函数表（单例）。
 * @return 类虚函数表指针；不可释放。
 * @note 本类无新增虚函数：仅继承 XClass 并重载 deinit（析构时释放四张
 *       段表容器）。
 */
XVtable* XModbusMapping_class_init(void);

/**
 * @brief 栈上初始化映射管理器（四类段表为空）。
 * @param map 目标对象；不能为 NULL，可为未初始化内存（派生字段内部清零）。
 * @return 无。参数为 NULL 时不执行任何操作。
 * @note 与 XClassDeinit 成对出现；四张段表容器在本函数内创建
 *       （值语义、COW 关闭——翻译表无共享需求）。
 */
void XModbusMapping_init(XModbusMapping* map);

/**
 * @brief 堆上创建映射管理器。
 * @param memory 内存分配器类型（XMemoryType 枚举；分配与所有权标记按
 *               同一类型登记）。
 * @return 新映射管理器；内存分配失败返回 NULL。不再使用时必须调用
 *         XClassDelete 释放。
 */
XModbusMapping* XModbusMapping_create_ex(XMemoryType memory);
/** @brief 默认内存分配器的堆创建（create_ex 便捷宏）。 */
#define XModbusMapping_create() XModbusMapping_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/* ==================== 段表管理 ==================== */
/**
 * @brief 增加一条映射段（同类型内虚拟区间与既有段重叠则拒绝）。
 * @param map 目标映射管理器；NULL 或未初始化（虚表为空）拒绝。
 * @param type 寄存器区（XModbusDiscreteInputs..XModbusHoldingRegisters）；
 *        XModbusInvalid 等非法值拒绝。
 * @param virtualStart 统一虚拟空间起始地址。
 * @param count 段长（点数；位区=位，寄存器区=字）；0 拒绝。
 * @param deviceId 底层设备句柄（XModbusHub_addDevice 返回值）；负值拒绝。
 *        本层不校验设备是否已登记——可先建映射后加设备，Hub 重建时物化。
 * @param unitId 底层从站地址（Modbus unit id）；0..65535 之外的值拒绝
 *        （本层按 uint16 存储，广播等语义由上层约束）。
 * @param remoteStart 远端起始地址。
 * @param defaultValue 段默认值：块首次物化时播种进缓存（仅未初始化点），
 *        故首启上线下发的缓存即默认值，清掉外部残留（输出上线对齐，
 *        2026-11-01；重连缺省回灌缓存最后写入值，Hub setOutputSyncMode
 *        可改默认值覆盖模式）；位区非 0 视为 1。每段独立——同一映射里
 *        不同设备可设不同默认值；输入区不消费该值，传 0 即可。
 * @return 成功 true；参数非法 / count==0 / deviceId<0 / 虚拟或远端区间
 *         跨 0xFFFF 回绕（段须完整落在 16 位地址空间内）/ 与同类型既有
 *         段重叠 false，段表保持原状（失败路径零副作用）。
 * @note 段按 virtualStart 升序有序插入，重叠校验与插入定位一次线性扫描
 *       完成；两端（虚拟、远端）都做回绕校验。
 */
bool XModbusMapping_addRange(XModbusMapping* map, XModbusRegisterType type,
                             uint16_t virtualStart, uint16_t count,
                             int deviceId, int unitId, uint16_t remoteStart,
                             uint16_t defaultValue);

/**
 * @brief 增加一条映射段（同 addRange，但可带段属性标志）。
 * @param map 目标映射管理器；NULL 或未初始化拒绝。
 * @param type 寄存器区；非法值拒绝。
 * @param virtualStart 统一虚拟空间起始地址。
 * @param count 段长（点数）；0 拒绝。
 * @param deviceId 底层设备句柄；负值拒绝。
 * @param unitId 底层从站地址。
 * @param remoteStart 远端起始地址。
 * @param defaultValue 段默认值（语义同 addRange）。
 * @param flags 段属性标志（XModbusMapRangeFlag 位掩码；0=普通可读段）。
 * @return 成功 true；参数非法 / 与既有段重叠 false。
 * @note addRange 等价于本函数的 flags=0 版本（既有调用点不受影响）。
 */
bool XModbusMapping_addRangeEx(XModbusMapping* map, XModbusRegisterType type,
                               uint16_t virtualStart, uint16_t count,
                               int deviceId, int unitId, uint16_t remoteStart,
                               uint16_t defaultValue, uint8_t flags);
/**
 * @brief 删除虚拟起点为 virtualStart 的段。
 * @param map 目标映射管理器；NULL 拒绝。
 * @param type 寄存器区；XModbusInvalid 等非法值拒绝。
 * @param virtualStart 目标段的虚拟起始地址（起点精确匹配，非包含查询）。
 * @return 删除成功 true；段表无此起点 / 参数非法 false。
 * @note addRange 已拒绝重叠段，同一起点在同类型内唯一，命中即删；
 *       仅摘除元素、保留容器容量。已发出的 rangeAt_const 借用指针失效。
 */
bool XModbusMapping_removeRange(XModbusMapping* map, XModbusRegisterType type,
                                uint16_t virtualStart);
/**
 * @brief 清空指定类型的段表。
 * @param map 目标映射管理器；NULL 时不执行任何操作。
 * @param type 寄存器区；传 XModbusInvalid 清空全部四类；其余非法值
 *        不执行任何操作。
 * @return 无。
 * @note 仅清元素、保留容器容量；已发出的 rangeAt_const 等借用指针失效。
 */
void XModbusMapping_clearAreas(XModbusMapping* map, XModbusRegisterType type);
/**
 * @brief 指定类型的段数。
 * @param map 目标映射管理器；NULL 返回 0。
 * @param type 寄存器区；XModbusInvalid 等非法值返回 0。
 * @return 该类型段表当前段数。
 */
size_t XModbusMapping_rangeCount_const(const XModbusMapping* map, XModbusRegisterType type);
/**
 * @brief 按下标取段。
 * @param map 目标映射管理器；NULL 返回 NULL。
 * @param type 寄存器区；非法值返回 NULL。
 * @param index 段表下标（段表按 virtualStart 升序）；越界返回 NULL。
 * @return 只读借用指针，禁止释放或修改；段表任何变更（增/删/清）后失效。
 */
const XModbusMapRange* XModbusMapping_rangeAt_const(const XModbusMapping* map,
                                                    XModbusRegisterType type, size_t index);
/**
 * @brief 查包含虚拟地址 virtualStart 的段（二分）。
 * @param map 目标映射管理器；NULL 返回 NULL。
 * @param type 寄存器区；非法值返回 NULL。
 * @param virtualStart 统一虚拟地址。
 * @return 命中段的只读借用指针（命中条件：virtualStart ∈ [段起点,
 *         段起点+段长)），禁止释放或修改；无覆盖段 / 参数非法返回 NULL。
 * @note 是「包含查询」而非「起点精确匹配」：落在段中部/尾部的地址同样
 *       命中该段；起点精确匹配用 removeRange。
 */
const XModbusMapRange* XModbusMapping_rangeAtVirtual_const(const XModbusMapping* map,
                                                           XModbusRegisterType type,
                                                           uint16_t virtualStart);

/* ==================== 翻译 ==================== */
/**
 * @brief 把虚拟区间 [virtualStart, virtualStart+count) 沿段表拆分为切片。
 * @param map 目标映射管理器；NULL、out 为 NULL 或 count==0 返回 0。
 * @param type 寄存器区；非法值返回 0。
 * @param virtualStart 统一虚拟区间起点。
 * @param count 区间点数（可跨多条段；段间洞自动跳过）。
 * @param out 切片输出数组（调用方提供栈/堆数组；容量不足时仅前 outCap
 *        个被写入，写入的每个切片均完整有效）。
 * @param outCap out 容量。
 * @return 写入的切片数；区间无任何映射返回 0；outCap 不足返回
 *         -需要的容量（按需重试约定：调用方据此扩容后重调）。
 * @note 切片按虚拟地址升序；切片的 range 成员是命中段完整拷贝，
 *       virtualStart/remoteStart/count 是区间与段的交集信息。
 *       区间中存在「洞」（未映射地址）时跳过洞继续——洞不对应用答数据，
 *       由调用方决定语义（Hub 的缓存读对洞返回 false）。
 */
int XModbusMapping_resolveSegments_const(const XModbusMapping* map,
                                         XModbusRegisterType type,
                                         uint16_t virtualStart, uint16_t count,
                                         XModbusMapSegment* out, int outCap);
/**
 * @brief 收集某设备在某类型下的全部段（轮询计划 / 按设备操作用）。
 * @param map 目标映射管理器；NULL、out 为 NULL 或 deviceId<0 返回 0。
 * @param deviceId 底层设备句柄（XModbusHub_addDevice 返回值）。
 * @param type 寄存器区；非法值返回 0。
 * @param out 段输出数组（调用方提供栈/堆数组；容量不足时仅前 outCap
 *        个被写入，写入的每段均完整有效）。
 * @param outCap out 容量。
 * @return 写入的段数；该设备在该类型下无任何段返回 0；outCap 不足
 *         返回 -需要的数量（按需重试约定同 resolveSegments_const）。
 * @note 输出按 virtualStart 升序（段表序）；输出是整段拷贝，非交集
 *       切片——需要「虚拟区间 ↔ 远端」的交集拆分用 resolveSegments_const。
 */
int XModbusMapping_segmentsForDevice_const(const XModbusMapping* map,
                                           int deviceId, XModbusRegisterType type,
                                           XModbusMapRange* out, int outCap);
/**
 * @brief 某设备在某类型下是否至少有一条段（小开销存在性查询）。
 * @param map 目标映射管理器；NULL 返回 false。
 * @param deviceId 底层设备句柄；负值返回 false。
 * @param type 寄存器区；非法值返回 false。
 * @return 至少一条段属于该设备 true；否则 false。
 * @note 线性扫描短路返回；Hub 重建块表前用它跳过无映射设备。
 */
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
