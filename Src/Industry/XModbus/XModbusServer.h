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
* @details 提供Modbus服务器端的核心功能，包括数据存储、请求处理等。
*          内置 processRequest 已覆盖 FC01-08/11/12/15/16/17/22/23/24/43；
*          其中 FC11（事件计数）/FC12（事件日志）/FC24（FIFO 队列，恒空）/
*          FC43 MEI 0x0E（设备标识，固定返回 XLib/X001/1.0.0）为硬编码桩
*          ——不读选项或数据存储，需要定制由子类重载该虚函数。
*          信号面刻意不对称：dataRead 仅网络读成功发射（本地 data1/data2
*          不发），dataWritten 任何写路径（含本地 setData）都发射——见两
*          信号注释。
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
    int m_serverAddress;            ///< 服务器地址（默认 1）
    XModbusDataUnitMap* m_dataMap;      ///< 数据映射表 (XModbusRegisterType -> XModbusDataUnit)；对象拥有，析构释放
    XMap* m_options;                    ///< 选项映射表 (int -> XVariant)；对象拥有，析构释放
} XModbusServer;

/**
* @brief 初始化XModbusServer的虚函数表
* @return 初始化完成的虚函数表指针；不可释放，多次调用返回同一实例
* @note 本框架为单线程事件循环模型：class_init 无锁保护，
*       须在事件循环启动前（或同一线程内）调用，通常由 create 接口自动完成
*/
XVtable* XModbusServer_class_init(void);

/**
* @brief 创建Modbus服务器实例
* @param memory 内存类型（XCLASS_DEFAULT_MEMORY_TYPE 使用系统默认内存）
* @return 成功返回指向新分配XModbusServer对象的指针（调用方经
*         XModbusServer_deleteLater/XClassDelete 释放），失败返回NULL
*/
XModbusServer* XModbusServer_create_ex(XMemoryType memory);

/**
* @brief 初始化Modbus服务器实例
* @param server 待初始化的XModbusServer实例指针；NULL 时不执行任何操作
* @return 无。
* @note 初始化基类、服务器地址=1、创建空数据映射表与选项表（对象拥有、
*       析构释放）；与 XClassDeinit/XClassDelete 成对出现
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
* @return 无。server 为 NULL 时不执行任何操作；本函数不做取值范围校验
*/
void XModbusServer_setServerAddress(XModbusServer* server, int address);

/**
* @brief 读取数据到XModbusDataUnit（经 readData 虚函数取数）
* @param server XModbusServer实例指针；NULL 返回 false
* @param unit 用于存储读取数据的XModbusDataUnit指针（调用方提供，函数
*        只借用并回写其值区）
* @return 成功返回true，失败（server/unit 为 NULL、未 setMap、类型未登记
*         或地址越界）返回false
* @note 本地读取不发射 dataRead 信号——该信号语义=网络读事件
*/
bool XModbusServer_data1(const XModbusServer* server, XModbusDataUnit* unit);

/**
* @brief 写入数据（经 writeData 虚函数落库）
* @param server XModbusServer实例指针；NULL 返回 false
* @param unit 要写入的数据单元（调用方提供，函数只借用）
* @return 成功返回true，失败（server/unit 为 NULL、未 setMap、类型未登记
*         或地址越界）返回false，数据存储保持不变
* @note 写成功即发射 dataWritten 信号——本地写与网络写口径一致（与
*       dataRead 的「仅网络读」刻意不对称）
*/
bool XModbusServer_setData1(XModbusServer* server, const XModbusDataUnit* unit);

/**
* @brief 设置单个寄存器的值
* @param server XModbusServer实例指针；NULL 返回 false
* @param table 寄存器类型
* @param address 寄存器地址
* @param value 要设置的值
* @return 成功返回true，失败（server 为 NULL、临时单元创建失败或地址越界）
*         返回false，数据存储保持不变
* @note 等价于单点 setData1；写成功即发射 dataWritten 信号
*/
bool XModbusServer_setData2(XModbusServer* server, XModbusRegisterType table, 
                                 uint16_t address, uint16_t value);

/**
* @brief 获取单个寄存器的值
* @param server XModbusServer实例指针；NULL 返回 false
* @param table 寄存器类型
* @param address 寄存器地址
* @param value 用于存储读取值的指针；NULL 返回 false
* @return 成功返回true（*value 已回写），失败返回false（*value 不写）
* @note 本地读取不发射 dataRead 信号
*/
bool XModbusServer_data2(const XModbusServer* server, XModbusRegisterType table, 
                                 uint16_t address, uint16_t* value);
/**
* @brief 检查是否处理广播（虚函数）
* @param server XModbusServer实例指针；NULL 返回 false
* @return 子类声明处理广播返回true；基类默认实现返回 false（不处理广播）
*/
bool XModbusServer_processesBroadcast(const XModbusServer* server);
/**
* @brief 设置数据映射表（Copy 语义：深拷贝 map 进服务器，源表仍归调用方）
* @param server XModbusServer实例指针；NULL 或虚表未初始化返回 false
* @param map 源数据映射表（只借用，不取得所有权；NULL 拒绝）
* @return 成功返回true；参数非法或拷贝失败返回false（原映射表保持不变）
* @note 移动/接管语义用 setMap_move_base / setMap_ref_base
*/
bool XModbusServer_setMap_base(XModbusServer* server, const XModbusDataUnitMap* map);
/**
* @brief 设置数据映射表（Move 语义：资源转移进服务器，源表变空壳仍归调用方释放）
* @param server XModbusServer实例指针；NULL 或虚表未初始化返回 false
* @param map 源数据映射表；成功后其资源归服务器（NULL 拒绝）
* @return 成功返回true；参数非法返回false（map 所有权不变）
*/
bool XModbusServer_setMap_move_base(XModbusServer* server,XModbusDataUnitMap* map);
/**
* @brief 设置数据映射表（Ref 语义：服务器直接接管 map 的所有权）
* @param server XModbusServer实例指针；NULL 或虚表未初始化返回 false（不接管）
* @param map 数据映射表；成功后所有权归服务器（析构随对象释放），
*        调用方不得再释放；NULL 拒绝
* @return 成功返回true（旧映射表被释放替换）；参数非法返回false
*/
bool XModbusServer_setMap_ref_base(XModbusServer* server,XModbusDataUnitMap* map);
/**
* @brief 检查是否处理广播（虚函数基类实现）
* @param server XModbusServer实例指针；NULL 返回 false
* @return 子类重载声明处理广播返回true；基类默认返回 false
*/
bool XModbusServer_processesBroadcast_base(const XModbusServer* server);

/**
* @brief 获取选项值（Copy：返回新副本，调用方负责释放）
* @param server XModbusServer实例指针；NULL 或虚表未初始化返回 NULL
* @param option 选项（XModbusServer_Option 或用户自定义 ≥ UserOption）
* @return 新 XVariant 副本（调用方 XClassDelete 释放）；未设置该选项返回 NULL
*/
XVariant* XModbusServer_value_base(const XModbusServer* server, int option);
/**
* @brief 获取选项值（Ref：借用内部存储，禁止释放）
* @param server XModbusServer实例指针；NULL 或虚表未初始化返回 NULL
* @param option 选项（XModbusServer_Option 或用户自定义 ≥ UserOption）
* @return 内部只读借用指针；下一次 setValue 同选项或对象析构后失效；
*         未设置该选项返回 NULL
*/
const XVariant* XModbusServer_value_const_base(const XModbusServer* server, int option);
/**
* @brief 设置选项值（Copy 语义：拷贝 value 进选项表）
* @param server XModbusServer实例指针；NULL 或虚表未初始化返回 false
* @param option 选项（XModbusServer_Option 或用户自定义 ≥ UserOption）
* @param value 选项值（只借用，不取得所有权；NULL 拒绝）
* @return 成功返回true（已存在的同选项值被替换）；失败返回false，选项表保持原状
*/
bool XModbusServer_setValue_base(XModbusServer* server, int option, const XVariant* value);
/**
* @brief 设置选项值（Move 语义：value 资源转移进选项表，成功后源变空壳
*        仍归调用方释放）
* @param server XModbusServer实例指针；NULL 或虚表未初始化返回 false
* @param option 选项（XModbusServer_Option 或用户自定义 ≥ UserOption）
* @param value 选项值；成功后其资源归服务器（NULL 拒绝）
* @return 成功返回true（已存在的同选项值被替换）；失败返回false
*/
bool XModbusServer_setValue_move_base(XModbusServer* server, int option, XVariant* value);
/**
* @brief 数据写入信号
* @details 语义=**任何写路径**：网络写（FC05/06/15/16/22 经 writeData）
*          与本地写（setData1/setData2 同经 writeData）都发射——与
*          dataRead 的「仅网络读」刻意不对称；写失败（地址越界等）不发射。
* @param server XModbusServer实例指针
* @param table 寄存器类型
* @param address 起始地址
* @param size 数据大小（点数）
*/
void* XModbusServer_dataWritten_signal(XModbusServer* server, XModbusRegisterType table, int address, int size);
/**
* @brief 数据读取信号（网络读请求成功取数后发出）
* @details 语义=**网络读事件**：仅在 processRequest 处理 FC01/FC02/FC03/FC04
*          且取数成功（未走异常应答）时发出。本地读取接口
*          （XModbusServer_data1/data2 → readData）**不发**此信号——否则
*          上层用 data2 轮询回显会自造幽灵计数（写事件同口径见
*          dataWritten 的无条件发出 + 调用侧抑制标志）。
* @param server XModbusServer实例指针
* @param table 寄存器类型
* @param address 起始地址
* @param size 读取数量
*/
void* XModbusServer_dataRead_signal(XModbusServer* server, XModbusRegisterType table, int address, int size);

// =============== 内存管理宏 ===============

#define XModbusServer_deinitLater   XModbusDevice_deinitLater
#define XModbusServer_deleteLater   XModbusDevice_deleteLater

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XModbusServer_create
#define XModbusServer_create() XModbusServer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMODBUSSERVER_H