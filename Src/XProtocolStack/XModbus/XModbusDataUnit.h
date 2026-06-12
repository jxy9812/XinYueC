#ifndef XMODBUSDATAUNIT_H
#define XMODBUSDATAUNIT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XBitArray.h"
#include "XVector.h"
#include "XMap.h"
/**
* @file XModbusDataUnit.h
* @brief Modbus数据单元核心头文件
* @details 定义Modbus数据单元的枚举类型、核心结构体及操作接口，
*          兼容Modbus协议中不同类型寄存器的数据存储与访问，
*          基于XClass实现面向对象特性，XVector作为通用数据容器。
*/

/**
* @brief Modbus寄存器类型枚举（对标QModbusDataUnit::RegisterType）
* @details 区分Modbus协议中不同类型的寄存器，决定数据存储的类型和读写权限
*/
typedef enum {
    XModbusInvalid,          ///< 无效寄存器类型（默认初始值）
    XModbusDiscreteInputs,   ///< 离散输入寄存器：只读，1位数据，存储bool类型
    XModbusCoils,            ///< 线圈寄存器：读/写，1位数据，存储bool类型
    XModbusInputRegisters,   ///< 输入寄存器：只读，16位数据，存储uint16_t类型
    XModbusHoldingRegisters  ///< 保持寄存器：读/写，16位数据，存储uint16_t类型
} XModbusRegisterType;

/**
* @brief Modbus数据单元核心结构体
* @details 根据寄存器类型自动选择最优存储容器：
*       - XModbusDiscreteInputs/XModbusCoils → 使用XBitArray存储，节省8倍内存
*       - XModbusInputRegisters/XModbusHoldingRegisters → 使用XVector存储uint16_t
*/
typedef struct XModbusDataUnit
{
    XClass m_class;                           ///< 基类信息
    uint16_t /*XModbusRegisterType*/ m_type;               ///< 寄存器类型
    uint16_t m_startAddress;                  ///< 寄存器起始地址
    union {
        void* m_data;                         ///< 通用数据指针
        struct XBitArray* m_bitArray;         ///< 位数据容器
        struct XVector* m_vector;             ///< 寄存器数据容器
    };
    //size_t m_valueCount;                      ///< 数据数量
} XModbusDataUnit;

/**
* @brief 初始化XModbusDataUnit的虚函数表
* @return 初始化完成的虚函数表指针，失败返回NULL
* @details 重载XClass的拷贝、移动、析构等虚函数，实现XModbusDataUnit的面向对象特性
*/
XVtable* XModbusDataUnit_class_init();

/**
* @brief 创建Modbus数据单元实例
* @return 成功返回XModbusDataUnit实例指针，失败返回NULL
* @details 动态分配内存并初始化数据单元，默认初始值：
*          - 寄存器类型：XModbusInvalid
*          - 起始地址：0xFFFF（无效地址）
*          - 数据容器：创建空的int16_t类型XVector
*          - 数据数量：0
*/
XModbusDataUnit* XModbusDataUnit_create();
XModbusDataUnit* XModbusDataUnit_create_ex(XModbusRegisterType type, uint16_t startAddress, size_t valueCount);
XModbusDataUnit* XModbusDataUnit_create_copy(const XModbusDataUnit* unit);
XModbusDataUnit* XModbusDataUnit_create_move(const XModbusDataUnit* unit);
void XModbusDataUnit_init(XModbusDataUnit* unit);
void XModbusDataUnit_init_ex(XModbusDataUnit* unit, XModbusRegisterType type, uint16_t startAddress, size_t valueCount);

/**
* @brief 设置数据单元的寄存器类型
* @param unit XModbusDataUnit实例指针（非NULL）
* @param type 新的寄存器类型（XModbusRegisterType枚举值）
* @note 类型切换后会清空原有数据（不同类型寄存器的数据存储格式不兼容）
*/
void XModbusDataUnit_setRegisterType(XModbusDataUnit* unit, XModbusRegisterType type);

/**
* @brief 设置数据单元的寄存器起始地址
* @param unit XModbusDataUnit实例指针（非NULL）
* @param startAddress 新的起始地址（Modbus协议地址，建议符合协议规范：0/1起始）
*/
void XModbusDataUnit_setStartAddress(XModbusDataUnit* unit, uint16_t startAddress);

/**
* @brief 设置指定索引的寄存器数据
* @param unit XModbusDataUnit实例指针（非NULL）
* @param index 数据索引（从0开始，需小于m_valueCount）
* @param value 新的数值（int16_t兼容bool/uint16_t，根据寄存器类型自动适配）
* @return 成功返回true，失败返回false
* @failure 失败场景：
*          1. unit为NULL
*          2. 数据容器m_values为空或为空容器
*          3. index超出有效范围（index >= m_valueCount）
*          4. 数值类型与寄存器类型不匹配
*/
bool XModbusDataUnit_setValue(XModbusDataUnit* unit, size_t index, uint16_t value);

/**
* @brief 设置数据单元的有效数据数量
* @param unit XModbusDataUnit实例指针（非NULL）
* @param newCount 新的有效数据数量（需>=0）
* @details 同步调整数据容器m_values的大小，确保容器容量适配新的数据数量
*/
void XModbusDataUnit_setValueCount(XModbusDataUnit* unit, size_t newCount);

/**
* @brief 批量设置数据单元的寄存器数据
* @param unit XModbusDataUnit实例指针（非NULL）
* @param values 新的数据容器（XVector类型，数据类型需与寄存器类型匹配）
* @return 成功返回true，失败返回false
* @failure 失败场景：
*          1. unit或values为NULL
*          2. values的数据类型与寄存器类型不匹配
*/
bool XModbusDataUnit_setValues(XModbusDataUnit* unit, XVector* values);

/**
* @brief 检查数据单元是否有效
* @param unit 待检查的XModbusDataUnit实例指针（const，不修改实例）
* @return 有效返回true，无效返回false
* @details 有效条件：
*           1. unit非NULL
*           2. 寄存器类型非XModbusInvalid
*           3. 起始地址非0xFFFF（无效地址）
*/
bool XModbusDataUnit_isValid(const XModbusDataUnit* unit);

/**
* @brief 获取数据单元的寄存器类型
* @param unit XModbusDataUnit实例指针（const，不修改实例）
* @return 返回寄存器类型枚举值，unit为NULL时返回XModbusInvalid
*/
XModbusRegisterType XModbusDataUnit_registerType(const XModbusDataUnit* unit);

/**
* @brief 获取数据单元的寄存器起始地址
* @param unit XModbusDataUnit实例指针（const，不修改实例）
* @return 返回起始地址值，unit为NULL时返回-1
*/
int XModbusDataUnit_startAddress(const XModbusDataUnit* unit);

/**
* @brief 获取指定索引的寄存器数据
* @param unit XModbusDataUnit实例指针（const，不修改实例）
* @param index 数据索引（从0开始）
* @return 返回指定索引的数值（int16_t兼容bool/uint16_t），失败返回0
* @failure 失败场景：
*          1. unit或m_values为NULL
*          2. index超出有效范围
*/
uint16_t XModbusDataUnit_value(const XModbusDataUnit* unit, size_t index);

/**
* @brief 获取数据单元的有效数据数量
* @param unit XModbusDataUnit实例指针（const，不修改实例）
* @return 返回有效数据数量，unit为NULL时返回0
*/
size_t XModbusDataUnit_valueCount(const XModbusDataUnit* unit);

/**
* @brief 获取寄存器数据容器（仅当类型为Holding/InputRegisters时有效）
* @param unit XModbusDataUnit实例指针（const，不修改实例）
* @return 返回数据容器的拷贝指针，unit为NULL时返回NULL
* @note 返回的是拷贝后的XVector，需手动释放，避免内存泄漏
*/
XVector* XModbusDataUnit_values1(const XModbusDataUnit* unit);
const XVector* XModbusDataUnit_values1_const(const XModbusDataUnit* unit);
/**
* @brief 获取位数据容器（仅当类型为Coils/DiscreteInputs时有效）
* @return 返回内部XBitArray指针，其他类型返回NULL
*/
XBitArray* XModbusDataUnit_values2(const XModbusDataUnit* unit);
const XBitArray* XModbusDataUnit_values2_const(const XModbusDataUnit* unit);
/**
* @brief 设置位数据（仅对Coils/DiscreteInputs有效）
*/
bool XModbusDataUnit_setBitArray(XModbusDataUnit* unit, const XBitArray* bits);

/**
* @brief 基类拷贝宏（继承自XClass）
* @details 复用XClass的拷贝基础逻辑，实现XModbusDataUnit的拷贝特性
*/
#define XModbusDataUnit_copy_base			XClass_copy_base

/**
* @brief 基类移动宏（继承自XClass）
* @details 复用XClass的移动基础逻辑，实现XModbusDataUnit的移动特性
*/
#define XModbusDataUnit_move_base			XClass_move_base

/**
* @brief 基类析构宏（继承自XClass）
* @details 复用XClass的析构基础逻辑，实现XModbusDataUnit的资源释放
*/
#define XModbusDataUnit_deinit_base		    XClass_deinit_base

/**
* @brief 基类删除宏（继承自XClass）
* @details 复用XClass的删除基础逻辑，释放XModbusDataUnit实例的内存
*/
#define XModbusDataUnit_delete_base		    XClass_delete_base

/**
* @brief Modbus服务器数据映射表
* @details 存储所有寄存器类型的数据范围
*/
typedef XMap XModbusDataUnitMap;
XModbusDataUnitMap* XModbusDataUnitMap_create();
#define XModbusDataUnitMap_delete_base XMapBase_delete_base
#endif // XMODBUSDATAUNIT_H