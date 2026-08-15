#ifndef XMODBUSSERVER_H
#define XMODBUSSERVER_H

#include "XModbusDevice.h"
#include "XModbusDataUnit.h"
#include "XModbusPdu.h"
#include "XVariant.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
* @file XModbusServer.h
* @brief Modbus服务器基类（对齐Qt6 QModbusServer）
* @details 提供Modbus服务器端的核心功能，包括数据存储、请求处理等
*/

/**
* @brief Modbus服务器选项枚举（对齐QModbusServer::Option）
* @details 定义服务器可配置的选项类型
*/
typedef enum {
    XModbusServer_DiagnosticRegister = 0,      ///< 诊断寄存器
    XModbusServer_ExceptionStatusOffset,       ///< 异常状态偏移
    XModbusServer_DeviceBusy,                  ///< 设备忙状态
    XModbusServer_AsciiInputDelimiter,         ///< ASCII输入分隔符
    XModbusServer_ListenOnlyMode,              ///< 仅监听模式
    XModbusServer_ServerIdentifier,            ///< 服务器标识符
    XModbusServer_RunIndicatorStatus,          ///< 运行指示状态
    XModbusServer_AdditionalData,              ///< 附加数据
    XModbusServer_DeviceIdentification,        ///< 设备标识
    XModbusServer_UserOption = 0x100           ///< 用户自定义选项起始值
} XModbusServer_Option;

// =============== 虚函数表枚举 ===============
XCLASS_DEFINE_BEGING(XModbusServer)
XCLASS_DEFINE_ENUM(XModbusServer, ProcessRequest) = XCLASS_VTABLE_GET_SIZE(XModbusDevice),
XCLASS_DEFINE_ENUM(XModbusServer, ProcessPrivateRequest),
XCLASS_DEFINE_ENUM(XModbusServer, WriteData),
XCLASS_DEFINE_ENUM(XModbusServer, ReadData),
XCLASS_DEFINE_ENUM(XModbusServer, SetMap),
XCLASS_DEFINE_ENUM(XModbusServer, ProcessesBroadcast),
XCLASS_DEFINE_ENUM(XModbusServer, Value),
XCLASS_DEFINE_ENUM(XModbusServer, SetValue),
XCLASS_DEFINE_END(XModbusServer)

/**
* @brief Modbus服务器结构体
* @details 继承自XModbusDevice，提供服务器端数据存储和请求处理功能
*/
typedef struct XModbusServer 
{
    XModbusDevice m_base;           ///< 继承自XModbusDevice
    int m_serverAddress;            ///< 服务器地址
    XModbusDataUnitMap* m_dataMap;      ///< 数据映射表 (XModbusRegisterType -> XModbusDataUnit)
    XMap* m_options;                    ///< 选项映射表 (int -> XVariant)
} XModbusServer;

/**
* @brief 初始化XModbusServer的虚函数表
* @return 初始化完成的虚函数表指针，失败返回NULL
*/
XVtable* XModbusServer_class_init(void);

/**
* @brief 创建Modbus服务器实例
* @return 成功返回XModbusServer实例指针，失败返回NULL
*/
XModbusServer* XModbusServer_create_ex(XMemoryType memory);

/**
* @brief 初始化Modbus服务器实例
* @param server 待初始化的XModbusServer实例指针（非NULL）
*/
void XModbusServer_init(XModbusServer* server);

/**
* @brief 获取服务器地址
* @param server XModbusServer实例指针
* @return 返回服务器地址，server为NULL时返回-1
*/
int XModbusServer_serverAddress(const XModbusServer* server);

/**
* @brief 设置服务器地址
* @param server XModbusServer实例指针（非NULL）
* @param address 新的服务器地址
*/
void XModbusServer_setServerAddress(XModbusServer* server, int address);

/**
* @brief 读取数据到XModbusDataUnit
* @param server XModbusServer实例指针（非NULL）
* @param unit 用于存储读取数据的XModbusDataUnit指针
* @return 成功返回true，失败返回false
*/
bool XModbusServer_data1(const XModbusServer* server, XModbusDataUnit* unit);

/**
* @brief 写入数据（虚函数）
* @param server XModbusServer实例指针（非NULL）
* @param unit 要写入的数据单元
* @return 成功返回true，失败返回false
*/
bool XModbusServer_setData1(XModbusServer* server, const XModbusDataUnit* unit);

/**
* @brief 设置单个寄存器的值
* @param server XModbusServer实例指针（非NULL）
* @param table 寄存器类型
* @param address 寄存器地址
* @param value 要设置的值
* @return 成功返回true，失败返回false
*/
bool XModbusServer_setData2(XModbusServer* server, XModbusRegisterType table, 
                                 uint16_t address, uint16_t value);

/**
* @brief 获取单个寄存器的值
* @param server XModbusServer实例指针（非NULL）
* @param table 寄存器类型
* @param address 寄存器地址
* @param value 用于存储读取值的指针
* @return 成功返回true，失败返回false
*/
bool XModbusServer_data2(const XModbusServer* server, XModbusRegisterType table, 
                                 uint16_t address, uint16_t* value);
/**
* @brief 检查是否处理广播（虚函数）
* @param server XModbusServer实例指针
* @return 如果处理广播返回true，否则返回false
*/
bool XModbusServer_processesBroadcast(const XModbusServer* server);
/**
* @brief 设置数据映射表（虚函数基类实现）
*/
bool XModbusServer_setMap_base(XModbusServer* server, const XModbusDataUnitMap* map);
bool XModbusServer_setMap_move_base(XModbusServer* server,XModbusDataUnitMap* map);
bool XModbusServer_setMap_ref_base(XModbusServer* server,XModbusDataUnitMap* map);
/**
* @brief 检查是否处理广播（虚函数基类实现）
*/
bool XModbusServer_processesBroadcast_base(const XModbusServer* server);

/**
* @brief 获取选项值（虚函数基类实现）
*/
XVariant* XModbusServer_value_base(const XModbusServer* server, int option);
const XVariant* XModbusServer_value_const_base(const XModbusServer* server, int option);
/**
* @brief 设置选项值（虚函数基类实现）
*/
bool XModbusServer_setValue_base(XModbusServer* server, int option, const XVariant* value);
bool XModbusServer_setValue_move_base(XModbusServer* server, int option, XVariant* value);
/**
* @brief 数据写入信号
* @param server XModbusServer实例指针
* @param table 寄存器类型
* @param address 起始地址
* @param size 数据大小
*/
void* XModbusServer_dataWritten_signal(XModbusServer* server, XModbusRegisterType table, int address, int size);

// =============== 内存管理宏 ===============
//#define XModbusServer_copy_base     XModbusDevice_copy_base
//#define XModbusServer_move_base     XModbusDevice_move_base

#define XModbusServer_deinitLater   XModbusDevice_deinitLater
#define XModbusServer_deleteLater   XModbusDevice_deleteLater

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusServer_create
#define XModbusServer_create(...) XModbusServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif // XMODBUSSERVER_H