/**
 * @file XModbusDeviceIdentification.h
 * @brief Modbus设备标识核心头文件（纯C风格，对齐Qt6 QModbusDeviceIdentification）
 * @details 封装了Modbus设备标识（Read Device Identification，FC43/MEI 0x0E）功能所需的数据结构和操作。
 *          用于读取和存储Modbus设备的制造商、产品代码、版本号等信息。
 *
 * @par 功能特性
 * - 支持基本、常规、扩展三种符合性等级
 * - 支持单个对象ID读取（Individual Access）
 * - 自动管理对象ID到数据的映射
 * - 支持从原始字节数组解析设备标识
 *
 * @par 符合性等级说明
 * | 等级 | 值 | 说明 |
 * |------|------|------|
 * | Basic | 0x01 | 基本标识（必须）：厂商名、产品代码、版本号 |
 * | Regular | 0x02 | 常规标识（可选）：URL、产品名、型号等 |
 * | Extended | 0x03 | 扩展标识（可选）：产品相关数据 |
 * | Basic Individual | 0x81 | 基本标识单个对象访问 |
 * | Regular Individual | 0x82 | 常规标识单个对象访问 |
 * | Extended Individual | 0x83 | 扩展标识单个对象访问 |
 *
 * @par 使用示例
 * @code
 * // 创建设备标识
 * XModbusDeviceIdentification* id = XModbusDeviceIdentification_create();
 *
 * // 设置基本标识信息
 * XModbusDeviceIdentification_insert(id, XModbusDeviceIdentification_VendorNameObjectId,
 *     (const uint8_t*)"MyCompany", 9);
 * XModbusDeviceIdentification_insert(id, XModbusDeviceIdentification_ProductCodeObjectId,
 *     (const uint8_t*)"MPC-3000", 8);
 * XModbusDeviceIdentification_insert(id, XModbusDeviceIdentification_MajorMinorRevisionObjectId,
 *     (const uint8_t*)"V1.0", 4);
 *
 * // 检查有效性
 * if (XModbusDeviceIdentification_isValid(id)) {
 *     // 设备标识完整
 * }
 *
 * // 获取厂商名
 * XByteArray* vendor = XModbusDeviceIdentification_value(id,
 *     XModbusDeviceIdentification_VendorNameObjectId);
 * XClassDelete(vendor);
 *
 * // 清理
 * XClassDelete(id);
 * @endcode
 */

#ifndef XMODBUSDEVICEIDENTIFICATION_H
#define XMODBUSDEVICEIDENTIFICATION_H

#include "XModbus_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_CORE_ON

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XTypes.h"


/**
 * @brief 对象ID枚举（对齐 QModbusDeviceIdentification::ObjectId）
 * @details 定义Modbus设备标识中的对象ID，按符合性等级分类：
 *          - Basic mandatory: 基本必须项（0x00-0x02）
 *          - Regular optional: 常规可选项（0x03-0x07）
 *          - Extended optional: 扩展可选项（0x80起）
 *
 * @par 对象ID说明
 * | ID | 名称 | 类型 | 说明 |
 * |-----|------|------|------|
 * | 0x00 | VendorName | 基本必须 | 厂商名称 |
 * | 0x01 | ProductCode | 基本必须 | 产品代码 |
 * | 0x02 | MajorMinorRevision | 基本必须 | 主版本号.次版本号 |
 * | 0x03 | VendorUrl | 常规可选 | 厂商URL |
 * | 0x04 | ProductName | 常规可选 | 产品名称 |
 * | 0x05 | ModelName | 常规可选 | 型号名称 |
 * | 0x06 | UserApplicationName | 常规可选 | 用户应用名称 |
 * | 0x80+ | ProductDependent | 扩展可选 | 产品相关数据 |
 */
typedef enum {
    /* Basic mandatory */
    XModbusDeviceIdentification_VendorNameObjectId = 0x00,            ///< 厂商名称（基本必须）
    XModbusDeviceIdentification_ProductCodeObjectId = 0x01,           ///< 产品代码（基本必须）
    XModbusDeviceIdentification_MajorMinorRevisionObjectId = 0x02,    ///< 主版本号.次版本号（基本必须）

    /* Regular optional */
    XModbusDeviceIdentification_VendorUrlObjectId = 0x03,             ///< 厂商URL（常规可选）
    XModbusDeviceIdentification_ProductNameObjectId = 0x04,           ///< 产品名称（常规可选）
    XModbusDeviceIdentification_ModelNameObjectId = 0x05,             ///< 型号名称（常规可选）
    XModbusDeviceIdentification_UserApplicationNameObjectId = 0x06,   ///< 用户应用名称（常规可选）
    XModbusDeviceIdentification_ReservedObjectId = 0x07,              ///< 保留对象ID

    /* Extended optional */
    XModbusDeviceIdentification_ProductDependentObjectId = 0x80,      ///< 产品相关数据起始ID（扩展可选）

    XModbusDeviceIdentification_UndefinedObjectId = 0x100             ///< 未定义对象ID
} XModbusDeviceIdentification_ObjectId;

/**
 * @brief 读取设备ID码（对齐 QModbusDeviceIdentification::ReadDeviceIdCode）
 * @details 定义读取设备标识时的读取类型
 *
 * @par 读取码说明
 * | 码值 | 名称 | 说明 |
 * |------|------|------|
 * | 0x01 | Basic | 读取基本标识（0x00-0x02） |
 * | 0x02 | Regular | 读取常规标识（0x00-0x07） |
 * | 0x03 | Extended | 读取扩展标识（0x00-0xFF） |
 * | 0x04 | Individual | 读取单个对象ID |
 */
typedef enum {
    XModbusDeviceIdentification_BasicReadDeviceIdCode = 0x01,          ///< 读取基本标识
    XModbusDeviceIdentification_RegularReadDeviceIdCode = 0x02,        ///< 读取常规标识
    XModbusDeviceIdentification_ExtendedReadDeviceIdCode = 0x03,       ///< 读取扩展标识
    XModbusDeviceIdentification_IndividualReadDeviceIdCode = 0x04      ///< 读取单个对象ID
} XModbusDeviceIdentification_ReadDeviceIdCode;

/**
 * @brief 符合性等级（对齐 QModbusDeviceIdentification::ConformityLevel）
 * @details 定义设备支持的符合性等级
 */
typedef enum {
    XModbusDeviceIdentification_BasicConformityLevel = 0x01,                    ///< 基本符合性
    XModbusDeviceIdentification_RegularConformityLevel = 0x02,                  ///< 常规符合性
    XModbusDeviceIdentification_ExtendedConformityLevel = 0x03,                 ///< 扩展符合性
    XModbusDeviceIdentification_BasicIndividualConformityLevel = 0x81,          ///< 基本符合性（单个对象访问）
    XModbusDeviceIdentification_RegularIndividualConformityLevel = 0x82,        ///< 常规符合性（单个对象访问）
    XModbusDeviceIdentification_ExtendedIndividualConformityLevel = 0x83        ///< 扩展符合性（单个对象访问）
} XModbusDeviceIdentification_ConformityLevel;

/**
 * @struct XModbusDeviceIdentification
 * @brief Modbus设备标识核心结构体（继承自 XClass）
 * @details 封装Modbus设备标识所需的数据结构和操作。
 *          使用XMap存储对象ID到数据的映射关系。
 */
XCLASS_DEFINE_BEGING(XModbusDeviceIdentification)
XCLASS_DEFINE_EXTEND_END(XModbusDeviceIdentification, XClass)

typedef struct XModbusDeviceIdentification {
    XClass m_class;                                                         /**< 第一个成员，由 XClass 管理，禁止手工修改。*/
    XMap* m_objects;                                                        /**< 对象ID到数据的映射（键=int，值=XByteArray*）；
                                                                                 对象拥有、deinit 时整表释放；init 后非 NULL
                                                                                 （除非创建失败）。 */
    XModbusDeviceIdentification_ConformityLevel m_conformityLevel;          /**< 符合性等级（init 后默认 BasicConformityLevel）。 */
} XModbusDeviceIdentification;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化XModbusDeviceIdentification的虚函数表（单例，进程内一次）。
 * @return 类虚函数表指针；不可释放。
 * @note 仅重载 deinit（析构时整表释放 m_objects）；未重载 copy/move。
 */
XVtable* XModbusDeviceIdentification_class_init(void);

/**
 * @brief 在堆上创建一个空的设备标识对象（无对象条目、符合性等级 Basic）。
 * @param memory 内存分配器类型（XMemoryType；分配与所有权标记按同一类型登记）。
 * @return 新对象（调用方负责XClassDelete释放）；内存分配失败返回NULL。
 */
XModbusDeviceIdentification* XModbusDeviceIdentification_create_ex(XMemoryType memory);

/**
 * @brief 栈上/就地初始化设备标识对象（空映射、符合性等级 Basic）。
 * @param id 目标对象；NULL 时不执行任何操作；可为未初始化内存。
 * @return 无。
 * @note 仅用于未初始化内存（对已初始化对象重复调用会覆盖 m_objects
 *       属主指针导致泄漏）；内部创建 int→XByteArray 映射表（值元素的
 *       拷贝/移动/析构经 XClass 多态入口分派）；完成后与
 *       XClassDeinit/XClassDelete 成对使用。
 */
void XModbusDeviceIdentification_init(XModbusDeviceIdentification* id);

/******************************************************************************************
 * 核心查询与操作接口（对齐 QModbusDeviceIdentification）
 ******************************************************************************************/

/**
 * @brief 检查设备标识是否有效（基本标识三要素齐全）。
 * @param id 目标对象；NULL 或内部映射为空返回 false。
 * @return VendorName (0x00)、ProductCode (0x01)、MajorMinorRevision (0x02)
 *         三个对象均存在且数据非空返回 true；否则返回 false。
 * @note 实现经 value() 逐项取深拷贝比较后释放，存在三次堆分配开销，
 *       不宜在热路径高频调用。
 */
bool XModbusDeviceIdentification_isValid(const XModbusDeviceIdentification* id);

/**
 * @brief 获取所有对象ID的列表。
 * @param id 目标对象；NULL 或内部映射为空返回 NULL。
 * @return 新 XVector 对象（元素为 int 型对象 ID 键，调用方 XClassDelete
 *         释放）；无任何对象返回 NULL 或空表。
 */
XVector* XModbusDeviceIdentification_objectIds(const XModbusDeviceIdentification* id);

/**
 * @brief 从映射中移除指定的对象ID。
 * @param id 目标对象；NULL 或内部映射为空时不执行任何操作。
 * @param objectId 要移除的对象ID（映射键原值匹配）；不存在时无任何效果。
 * @return 无。
 */
void XModbusDeviceIdentification_remove(XModbusDeviceIdentification* id, int objectId);

/**
 * @brief 检查是否包含指定的对象ID。
 * @param id 目标对象；NULL 或内部映射为空返回 false。
 * @param objectId 要检查的对象ID。
 * @return 映射中存在该键返回 true；否则返回 false。
 */
bool XModbusDeviceIdentification_contains(const XModbusDeviceIdentification* id, int objectId);

/**
 * @brief 插入（或覆盖）一个对象ID及其数据。
 * @param id 目标对象。
 * @param objectId 对象ID（int 键；≥0x100 拒绝，负值不被拦截——协议上
 *        无意义，调用方自行保证取值 0x00-0xFF）。
 * @param data 对象数据（借用，内部深拷贝、不取得所有权；NULL 拒绝；
 *        size==0 时允许传非 NULL 指针存空对象）。
 * @param size 数据字节数（上限 245 字节，超过拒绝）。
 * @return 插入成功返回 true（同 ID 旧值被替换）；id/内部映射/data 为
 *         NULL、size 超 245 或 objectId ≥ 0x100 返回 false，对象保持
 *         不变。
 * @note 同一 ID 重复插入等价于先 remove 再插入新值。
 */
bool XModbusDeviceIdentification_insert(XModbusDeviceIdentification* id, int objectId, const uint8_t* data, size_t size);

/**
 * @brief 获取指定对象ID的数据拷贝。
 * @param id 目标对象；NULL 或内部映射为空返回 NULL。
 * @param objectId 对象ID。
 * @return 新 XByteArray 对象（存储数据的深拷贝，调用方 XClassDelete
 *         释放）；该ID不存在返回 NULL。
 */
XByteArray* XModbusDeviceIdentification_value(const XModbusDeviceIdentification* id, int objectId);

/**
 * @brief 获取符合性等级。
 * @param id 目标对象；NULL 返回 XModbusDeviceIdentification_BasicConformityLevel。
 * @return 当前符合性等级（ConformityLevel 枚举值，协议原值）。
 */
XModbusDeviceIdentification_ConformityLevel XModbusDeviceIdentification_conformityLevel(const XModbusDeviceIdentification* id);

/**
 * @brief 设置符合性等级。
 * @param id 目标对象；NULL 时不执行任何操作。
 * @param level 新的符合性等级（原样存储，不校验取值）。
 * @return 无。
 */
void XModbusDeviceIdentification_setConformityLevel(XModbusDeviceIdentification* id, XModbusDeviceIdentification_ConformityLevel level);

/**
 * @brief 从原始字节数组解析出设备标识（Read Device Identification 响应）。
 * @param data 原始响应数据（借用；NULL 或 size < 7 返回 NULL）。
 *        期望布局：[MEI类型][ReadDevId码][符合性等级][MoreFollows]
 *        [NextObjId][对象数][ObjId][长度][数据]...
 * @param size 数据字节数。
 * @return 新对象（调用方 XClassDelete 释放）；数据不足、内存分配失败
 *         或对象条目数据被截断返回 NULL。
 * @note 只处理单帧响应（不追踪 MoreFollows 后续帧）；不校验 data[0]/
 *       data[1]（MEI 类型与 ReadDevId 码，调用方保证）；符合性等级取
 *       data[2]，对象条目数取 data[5]；单个对象条目超过 245 字节会被
 *       insert 静默丢弃（不影响其余条目与返回值）。
 */
XModbusDeviceIdentification* XModbusDeviceIdentification_fromByteArray(const uint8_t* data, size_t size);


#endif /* XMODBUS_CORE_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusDeviceIdentification_create
#define XModbusDeviceIdentification_create() XModbusDeviceIdentification_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMODBUSDEVICEIDENTIFICATION_H
