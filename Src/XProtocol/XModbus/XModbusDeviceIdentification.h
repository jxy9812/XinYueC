#ifndef XMODBUSDEVICEIDENTIFICATION_H
#define XMODBUSDEVICEIDENTIFICATION_H

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XTypes.h" 

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XModbusDeviceIdentification.h
 * @brief Modbus设备标识核心头文件（纯C风格，对齐Qt6 QModbusDeviceIdentification）
 * @details 封装了Modbus设备标识（Read Device Identification，FC43/14）功能所需的数据结构和操作。
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
 *
 * // 清理
 * XModbusDeviceIdentification_delete_base(id);
 * @endcode
 */

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
    XClass m_class;                                                         ///< 继承自 XClass
    XMap* m_objects;                                                        ///< 存储对象ID到数据的映射 (XMap<int, XByteArray>)
    XModbusDeviceIdentification_ConformityLevel m_conformityLevel;          ///< 符合性等级
} XModbusDeviceIdentification;

/******************************************************************************************
 * 类初始化/实例创建接口
 ******************************************************************************************/

/**
 * @brief 初始化XModbusDeviceIdentification的虚函数表
 * @return 指向初始化完成的XVtable的指针
 */
XVtable* XModbusDeviceIdentification_class_init(void);

/**
 * @brief 在堆上创建并初始化一个XModbusDeviceIdentification实例
 * @return 成功返回XModbusDeviceIdentification指针，失败返回NULL
 */
XModbusDeviceIdentification* XModbusDeviceIdentification_create_ex(XMemoryType memory);

/**
 * @brief 初始化已分配的XModbusDeviceIdentification实例
 * @param id XModbusDeviceIdentification指针（非NULL）
 */
void XModbusDeviceIdentification_init(XModbusDeviceIdentification* id);

/******************************************************************************************
 * 核心查询与操作接口（对齐 QModbusDeviceIdentification）
 ******************************************************************************************/

/**
 * @brief 检查设备标识是否有效
 * @param id XModbusDeviceIdentification指针
 * @return 有效返回true，无效返回false
 * @note 有效条件: VendorName, ProductCode, MajorMinorRevision 均存在且非空
 */
bool XModbusDeviceIdentification_isValid(const XModbusDeviceIdentification* id);

/**
 * @brief 获取所有对象ID的列表
 * @param id XModbusDeviceIdentification指针
 * @return 返回包含所有对象ID的XVector (uint8_t 类型)，调用者需释放
 */
XVector* XModbusDeviceIdentification_objectIds(const XModbusDeviceIdentification* id);

/**
 * @brief 从映射中移除指定的对象ID
 * @param id XModbusDeviceIdentification指针（非NULL）
 * @param objectId 要移除的对象ID
 */
void XModbusDeviceIdentification_remove(XModbusDeviceIdentification* id, int objectId);

/**
 * @brief 检查是否包含指定的对象ID
 * @param id XModbusDeviceIdentification指针
 * @param objectId 要检查的对象ID
 * @return 包含返回true，否则返回false
 */
bool XModbusDeviceIdentification_contains(const XModbusDeviceIdentification* id, int objectId);

/**
 * @brief 插入一个对象ID及其数据
 * @param id XModbusDeviceIdentification指针（非NULL）
 * @param objectId 对象ID
 * @param data 数据指针
 * @param size 数据大小
 * @return 成功返回true，失败（数据过大或ID无效）返回false
 */
bool XModbusDeviceIdentification_insert(XModbusDeviceIdentification* id, int objectId, const uint8_t* data, size_t size);

/**
 * @brief 获取指定对象ID的数据拷贝
 * @param id XModbusDeviceIdentification指针
 * @param objectId 对象ID
 * @return 返回XByteArray*拷贝，若不存在则返回NULL，调用者需释放
 */
XByteArray* XModbusDeviceIdentification_value(const XModbusDeviceIdentification* id, int objectId);

/**
 * @brief 获取符合性等级
 * @param id XModbusDeviceIdentification指针
 * @return 符合性等级
 */
XModbusDeviceIdentification_ConformityLevel XModbusDeviceIdentification_conformityLevel(const XModbusDeviceIdentification* id);

/**
 * @brief 设置符合性等级
 * @param id XModbusDeviceIdentification指针（非NULL）
 * @param level 新的符合性等级
 */
void XModbusDeviceIdentification_setConformityLevel(XModbusDeviceIdentification* id, XModbusDeviceIdentification_ConformityLevel level);

/**
 * @brief 从原始字节数组解析出设备标识
 * @param data 原始响应数据
 * @param size 数据大小
 * @return 解析成功返回新创建的XModbusDeviceIdentification实例，失败返回NULL
 */
XModbusDeviceIdentification* XModbusDeviceIdentification_fromByteArray(const uint8_t* data, size_t size);

#define XModbusDeviceIdentification_deinit_base XClass_deinit_base
#define XModbusDeviceIdentification_delete_base XClass_delete_base

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusDeviceIdentification_create
#define XModbusDeviceIdentification_create() XModbusDeviceIdentification_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMODBUSDEVICEIDENTIFICATION_H
