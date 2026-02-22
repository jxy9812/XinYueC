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
     * @details 封装了Modbus设备标识（Read Device Identification）功能所需的数据结构和操作。
     */

     /**
      * @brief 对象ID枚举 (对齐 QModbusDeviceIdentification::ObjectId)
      */
    typedef enum {
        /* Basic mandatory */
        XModbusDeviceIdentification_VendorNameObjectId = 0x00,
        XModbusDeviceIdentification_ProductCodeObjectId = 0x01,
        XModbusDeviceIdentification_MajorMinorRevisionObjectId = 0x02,

        /* Regular optional */
        XModbusDeviceIdentification_VendorUrlObjectId = 0x03,
        XModbusDeviceIdentification_ProductNameObjectId = 0x04,
        XModbusDeviceIdentification_ModelNameObjectId = 0x05,
        XModbusDeviceIdentification_UserApplicationNameObjectId = 0x06,
        XModbusDeviceIdentification_ReservedObjectId = 0x07,

        /* Extended optional */
        XModbusDeviceIdentification_ProductDependentObjectId = 0x80,

        XModbusDeviceIdentification_UndefinedObjectId = 0x100
    } XModbusDeviceIdentification_ObjectId;

    /**
     * @brief 读取设备ID码 (对齐 QModbusDeviceIdentification::ReadDeviceIdCode)
     */
    typedef enum {
        XModbusDeviceIdentification_BasicReadDeviceIdCode = 0x01,
        XModbusDeviceIdentification_RegularReadDeviceIdCode = 0x02,
        XModbusDeviceIdentification_ExtendedReadDeviceIdCode = 0x03,
        XModbusDeviceIdentification_IndividualReadDeviceIdCode = 0x04
    } XModbusDeviceIdentification_ReadDeviceIdCode;

    /**
     * @brief 符合性等级 (对齐 QModbusDeviceIdentification::ConformityLevel)
     */
    typedef enum {
        XModbusDeviceIdentification_BasicConformityLevel = 0x01,
        XModbusDeviceIdentification_RegularConformityLevel = 0x02,
        XModbusDeviceIdentification_ExtendedConformityLevel = 0x03,
        XModbusDeviceIdentification_BasicIndividualConformityLevel = 0x81,
        XModbusDeviceIdentification_RegularIndividualConformityLevel = 0x82,
        XModbusDeviceIdentification_ExtendedIndividualConformityLevel = 0x83
    } XModbusDeviceIdentification_ConformityLevel;

    /**
     * @struct XModbusDeviceIdentification
     * @brief Modbus设备标识核心结构体 (继承自 XClass)
     */
    typedef struct XModbusDeviceIdentification {
        XClass m_class; ///< 继承自 XClass
        XMap* m_objects; ///< 存储对象ID到数据的映射 (XMap<int, XByteArray>)
        XModbusDeviceIdentification_ConformityLevel m_conformityLevel; ///< 符合性等级
    } XModbusDeviceIdentification;

    /******************************************************************************************
     * 类初始化/实例创建接口
     ******************************************************************************************/

    XVtable* XModbusDeviceIdentification_class_init(void);
    XModbusDeviceIdentification* XModbusDeviceIdentification_create(void);
    void XModbusDeviceIdentification_init(XModbusDeviceIdentification* id);

    /******************************************************************************************
     * 核心查询与操作接口 (对齐 QModbusDeviceIdentification)
     ******************************************************************************************/

     /**
      * @brief 检查设备标识是否有效
      * @note 有效条件: VendorName, ProductCode, MajorMinorRevision 均存在且非空
      */
    bool XModbusDeviceIdentification_isValid(const XModbusDeviceIdentification* id);

    /**
     * @brief 获取所有对象ID的列表
     * @return 返回包含所有对象ID的 XVector (uint8_t 类型)，调用者需释放
     */
    XVector* XModbusDeviceIdentification_objectIds(const XModbusDeviceIdentification* id);

    /**
     * @brief 从映射中移除指定的对象ID
     */
    void XModbusDeviceIdentification_remove(XModbusDeviceIdentification* id, int objectId);

    /**
     * @brief 检查是否包含指定的对象ID
     */
    bool XModbusDeviceIdentification_contains(const XModbusDeviceIdentification* id, int objectId);

    /**
     * @brief 插入一个对象ID及其数据
     * @param id 设备标识实例
     * @param objectId 对象ID
     * @param data 数据指针
     * @param size 数据大小
     * @return 成功返回 true，失败（数据过大或ID无效）返回 false
     */
    bool XModbusDeviceIdentification_insert(XModbusDeviceIdentification* id, int objectId, const uint8_t* data, size_t size);

    /**
     * @brief 获取指定对象ID的数据拷贝
     * @return 返回 XByteArray* 拷贝，若不存在则返回 NULL，调用者需释放
     */
    XByteArray* XModbusDeviceIdentification_value(const XModbusDeviceIdentification* id, int objectId);

    /**
     * @brief 获取符合性等级
     */
    XModbusDeviceIdentification_ConformityLevel XModbusDeviceIdentification_conformityLevel(const XModbusDeviceIdentification* id);

    /**
     * @brief 设置符合性等级
     */
    void XModbusDeviceIdentification_setConformityLevel(XModbusDeviceIdentification* id, XModbusDeviceIdentification_ConformityLevel level);

    /**
     * @brief 从原始字节数组解析出设备标识
     * @param data 原始响应数据
     * @param size 数据大小
     * @return 解析成功返回新创建的 XModbusDeviceIdentification 实例，失败返回 NULL
     */
    XModbusDeviceIdentification* XModbusDeviceIdentification_fromByteArray(const uint8_t* data, size_t size);

    /******************************************************************************************
     * 内存管理宏
     ******************************************************************************************/
#define XModbusDeviceIdentification_copy_base XClass_copy_base
#define XModbusDeviceIdentification_move_base XClass_move_base
#define XModbusDeviceIdentification_deinit_base XClass_deinit_base
#define XModbusDeviceIdentification_delete_base XClass_delete_base

#ifdef __cplusplus
}
#endif

#endif // XMODBUSDEVICEIDENTIFICATION_H