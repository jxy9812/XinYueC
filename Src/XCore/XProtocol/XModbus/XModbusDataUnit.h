/**
* @file XModbusDataUnit.h
* @brief Modbus数据单元核心头文件
* @details 定义Modbus数据单元的枚举类型、核心结构体及操作接口，
*          兼容Modbus协议中不同类型寄存器的数据存储与访问，
*          基于XClass实现面向对象特性，XVector作为通用数据容器。
*          位类区（离散输入/线圈）用 XBitArray 存储，寄存器类区
*          （输入/保持寄存器）用 XVector(uint16_t) 存储。
*          释放约定：继承 XClass（非 XObject），统一 XClassDelete/
*          XClassDeinit。
*/

#ifndef XMODBUSDATAUNIT_H
#define XMODBUSDATAUNIT_H

#include "XModbus_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if XPROTOCOL_ON
#if XMODBUS_ON
#if XMODBUS_CORE_ON

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XClass.h"
#include "XBitArray.h"
#include "XVector.h"
#include "XMap.h"

/**
* @brief Modbus寄存器类型枚举（对标QModbusDataUnit::RegisterType）
* @details 区分Modbus协议中不同类型的寄存器，决定数据存储的类型和读写权限。
*          实值 1..4 依枚举顺序递增；XModbusInvalid=0 是无效/初始哨兵值，
*          各枚举值不按位组合。
*/
typedef enum {
    XModbusInvalid,          /**< 无效寄存器类型（=0，初始/清空后的哨兵值）。 */
    XModbusDiscreteInputs,   /**< 离散输入寄存器（=1）：只读，1位数据，位容器（XBitArray）。 */
    XModbusCoils,            /**< 线圈寄存器（=2）：读/写，1位数据，位容器（XBitArray）。 */
    XModbusInputRegisters,   /**< 输入寄存器（=3）：只读，16位数据，字容器（XVector uint16_t）。 */
    XModbusHoldingRegisters  /**< 保持寄存器（=4）：读/写，16位数据，字容器（XVector uint16_t）。 */
} XModbusRegisterType;

/**
* @brief Modbus数据单元核心结构体
* @details 根据寄存器类型自动选择最优存储容器：
*       - XModbusDiscreteInputs/XModbusCoils → 使用XBitArray存储，节省8倍内存
*       - XModbusInputRegisters/XModbusHoldingRegisters → 使用XVector存储uint16_t
*/
XCLASS_DEFINE_BEGING(XModbusDataUnit)
XCLASS_DEFINE_EXTEND_END(XModbusDataUnit, XClass)

typedef struct XModbusDataUnit
{
    XClass m_class;                           /**< 第一个成员，由 XClass 管理，禁止手工修改。*/
    uint16_t /*XModbusRegisterType*/ m_type;  /**< 寄存器类型（XModbusRegisterType；init 后为 XModbusInvalid）。 */
    uint16_t m_startAddress;                  /**< 寄存器起始地址（16 位协议地址；0xFFFF 为无效哨兵，会使 isValid 失败）。 */
    union {
        void* m_data;                         /**< 通用数据指针：对象拥有、deinit 时释放；
                                                   位类型解释为 m_bitArray，寄存器类型解释为 m_vector，
                                                   三者是同一存储的三种视图。 */
        struct XBitArray* m_bitArray;         /**< 位数据容器视图（XModbusDiscreteInputs/XModbusCoils 时有效）。 */
        struct XVector* m_vector;             /**< 寄存器数据容器视图（XModbusInputRegisters/XModbusHoldingRegisters
                                                   时有效，元素 uint16_t）。 */
    };
    //size_t m_valueCount;                      ///< 数据数量
} XModbusDataUnit;

/**
* @brief 初始化XModbusDataUnit的虚函数表（单例，进程内一次）。
* @return 类虚函数表指针；不可释放。
* @details 重载XClass的拷贝、移动、析构等虚函数，实现XModbusDataUnit的面向对象特性：
*          copy 深拷贝容器、move 转移容器所有权并清空源、deinit 释放容器。
*/
XVtable* XModbusDataUnit_class_init();

/**
* @brief 在堆上创建Modbus数据单元实例。
* @param memory 内存分配器类型（XMemoryType）。
* @param type 初始寄存器类型；传入XModbusInvalid时按默认值初始化
*        （类型 Invalid、起始地址 0xFFFF、无容器）。
* @param startAddress 起始地址；type为XModbusInvalid时忽略。
* @param valueCount 初始数据数量（单位=点数：位区=位，寄存器区=字，
*        容器按该长度创建）；type为XModbusInvalid时忽略。
* @return 新对象（调用方负责XClassDelete释放），内存分配失败返回NULL。
* @details 按类型创建存储容器：位类区建 XBitArray(count)，
*          寄存器类区建 XVector(uint16_t) 并 resize 到 count。
*          容器创建失败时对象仍返回，但 m_data 为 NULL（后续读写按
*          空容器处理）。
*/
XModbusDataUnit* XModbusDataUnit_create_ex(XMemoryType memory, XModbusRegisterType type, uint16_t startAddress, size_t valueCount);
/**
* @brief 创建Modbus数据单元的深拷贝
* @param unit 源数据单元指针（借用，不取得所有权）；NULL时返回NULL
* @return 成功返回新创建的XModbusDataUnit实例指针（新对象，调用方负责XClassDelete释放），失败返回NULL
* @details 源对象可为任意已初始化实例；拷贝不修改源对象，源对象生命周期与释放方式不变
*/
XModbusDataUnit* XModbusDataUnit_create_copy(const XModbusDataUnit* unit);
/**
* @brief 通过移动语义创建Modbus数据单元实例
* @param unit 源数据单元指针；NULL时返回NULL（签名中的const为历史遗留，
*             实现会转移并清空源对象，禁止传入声明为const的共享对象）
* @return 成功返回新创建的XModbusDataUnit实例指针（新对象，调用方负责XClassDelete释放），失败返回NULL
* @warning 移动后源对象被清空（容器被接管，类型复位为XModbusInvalid），
*          源对象仍由其创建者按原分配方式释放（栈对象XClassDeinit、堆对象XClassDelete）
*/
XModbusDataUnit* XModbusDataUnit_create_move(const XModbusDataUnit* unit);
/**
 * @brief 栈上/就地初始化数据单元（类型 Invalid、起始地址 0xFFFF、无容器）。
 * @param unit 待初始化的数据单元指针；NULL 时不执行任何操作；
 *        可为未初始化内存（派生字段内部清零）。
 * @return 无。
 * @note 仅用于未初始化内存（对已初始化对象重复调用会覆盖 m_data 属主
 *       指针导致泄漏）；完成后与 XClassDeinit/XClassDelete 成对使用。
 */
void XModbusDataUnit_init(XModbusDataUnit* unit);
/**
 * @brief 初始化数据单元（带参数）。
 * @param unit 待初始化的数据单元指针；NULL 时安全返回（不执行任何操作）。
 * @param type 寄存器类型。
 * @param startAddress 起始地址。
 * @param valueCount 初始数据数量（单位=点数：位区=位，寄存器区=字）。
 * @return 无。
 * @note 先按 init 语义整体复位，再按类型创建 valueCount 长度的容器
 *       （位区 XBitArray / 寄存器区 XVector(uint16_t)）；容器创建失败时
 *       m_data 保持 NULL，无错误返回值。前置条件同 XModbusDataUnit_init。
 */
void XModbusDataUnit_init_ex(XModbusDataUnit* unit, XModbusRegisterType type, uint16_t startAddress, size_t valueCount);

/**
* @brief 设置数据单元的寄存器类型。
* @param unit XModbusDataUnit实例指针；NULL 时不执行任何操作。
* @param type 新的寄存器类型（XModbusRegisterType枚举值）。
* @return 无。
* @note 仅当容器类别切换（位区 ↔ 寄存器区）时重建容器：新容器与旧容器
*       等长（长度保留）但内容不保留；同类别内切换（如 Coils ↔
*       DiscreteInputs、Holding ↔ InputRegisters）只改类型标记，
*       数据原样保留。
*/
void XModbusDataUnit_setRegisterType(XModbusDataUnit* unit, XModbusRegisterType type);

/**
* @brief 设置数据单元的寄存器起始地址。
* @param unit XModbusDataUnit实例指针；NULL 时不执行任何操作。
* @param startAddress 新的起始地址（16 位协议地址；设 0xFFFF 会被
*        isValid 判为无效）。
* @return 无。
*/
void XModbusDataUnit_setStartAddress(XModbusDataUnit* unit, uint16_t startAddress);

/**
 * @brief 设置指定索引的值。
 * @param unit 数据单元指针。
 * @param index 值索引（从 0 开始，需小于容器长度，见 valueCount）。
 * @param value 要设置的值：位区按 value!=0 写入位；寄存器区按 uint16_t
 *        原样写入。
 * @return 设置成功返回true；unit 为 NULL、容器为空（m_data NULL）或
 *         index 越界返回 false，对象保持不变。
 */
bool XModbusDataUnit_setValue(XModbusDataUnit* unit, size_t index, uint16_t value);

/**
* @brief 设置数据单元的有效数据数量。
* @param unit XModbusDataUnit实例指针；NULL 时不执行任何操作。
* @param newCount 新的有效数据数量（单位=点数：位区=位，寄存器区=字）。
* @return 无。
* @details 容器不存在时按当前类型创建 newCount 长度的新容器；已存在则
*          resize 到 newCount（放大或缩小）。调整失败（内存分配）时
*          静默保持原容器，无错误返回值。
*/
void XModbusDataUnit_setValueCount(XModbusDataUnit* unit, size_t newCount);

/**
 * @brief 批量替换寄存器区的值（仅寄存器类型有效）。
 * @param unit 数据单元指针。
 * @param values 源值列表（借用，内部拷贝、不取得所有权；元素按 uint16_t
 *        使用，应为 XVector_Create(uint16_t) 创建的容器）；unit 或
 *        values 为 NULL 返回 false。
 * @return 拷贝成功返回true；unit/values 为 NULL 或 unit 为位类型返回
 *         false（位类型用 setBitArray），对象保持不变。
 * @note 内部容器为空时整表拷贝 values；非空时清空后逐元素追加。
 */
bool XModbusDataUnit_setValues(XModbusDataUnit* unit, XVector* values);

/**
 * @brief 判断数据单元是否有效。
 * @param unit 待检查的数据单元指针；NULL 返回 false。
 * @return 寄存器类型非 XModbusInvalid 且起始地址非 0xFFFF（无效哨兵）
 *         时返回 true；否则返回 false。
 * @note 不校验数据容器是否存在/非空。
 */
bool XModbusDataUnit_isValid(const XModbusDataUnit* unit);

/**
* @brief 获取数据单元的寄存器类型。
* @param unit XModbusDataUnit实例指针（const，不修改实例）；NULL 返回
*        XModbusInvalid。
* @return 寄存器类型枚举值。
*/
XModbusRegisterType XModbusDataUnit_registerType(const XModbusDataUnit* unit);

/**
* @brief 获取数据单元的寄存器起始地址。
* @param unit XModbusDataUnit实例指针（const，不修改实例）；NULL 返回 -1。
* @return 起始地址值（0-65535；0xFFFF 亦可能是真实存储值，语义上与
*         无效哨兵重合）。
*/
int XModbusDataUnit_startAddress(const XModbusDataUnit* unit);

/**
 * @brief 获取指定索引的值。
 * @param unit 数据单元指针；NULL 返回 0。
 * @param index 值索引（从 0 开始）。
 * @return 指定索引的值：位区返回 0/1，寄存器区返回 uint16_t 原值；
 *         unit 为 NULL、容器为空或索引越界返回 0（与合法值 0 无法区分，
 *         越界检查用 valueCount）。
 */
uint16_t XModbusDataUnit_value(const XModbusDataUnit* unit, size_t index);

/**
* @brief 获取数据单元的有效数据数量。
* @param unit XModbusDataUnit实例指针（const，不修改实例）；NULL 或容器
*        为空返回 0。
* @return 容器长度（单位=点数：位区=位数，寄存器区=uint16_t 个数）。
*/
size_t XModbusDataUnit_valueCount(const XModbusDataUnit* unit);

/**
* @brief 以 XVector(uint16_t) 形式取出全部数据（新拷贝）。
* @param unit XModbusDataUnit实例指针（const，不修改实例）。
* @return 新 XVector 对象（元素 uint16_t，调用方 XClassDelete 释放）：
*         位区数据逐位转换为 0/1，寄存器区为深拷贝；
*         unit 为 NULL、容器为空或长度 0 返回 NULL。
* @note 与 values1_const 的差异：本函数总是返回新拷贝（含位区转换），
*       可安全在对象变更后继续使用。
*/
XVector* XModbusDataUnit_values1(const XModbusDataUnit* unit);
/**
* @brief 获取寄存器数据的内部只读容器（借用指针）
* @param unit XModbusDataUnit实例指针（const，不修改实例）
* @return 返回内部XVector的借用指针（禁止释放、禁止修改），
*         unit为NULL、位类型或容器为空时返回NULL
*/
const XVector* XModbusDataUnit_values1_const(const XModbusDataUnit* unit);
/**
* @brief 以 XBitArray 形式取出位区数据（新拷贝，仅位类型有效）。
* @param unit XModbusDataUnit实例指针（const，不修改实例）。
* @return 新 XBitArray 对象（内部位数据的深拷贝，调用方 XClassDelete
*         释放）；unit 为 NULL 或类型非位区（Coils/DiscreteInputs）
*         返回 NULL。
*/
XBitArray* XModbusDataUnit_values2(const XModbusDataUnit* unit);
/**
* @brief 获取位数据的内部只读容器（借用指针）
* @param unit XModbusDataUnit实例指针（const，不修改实例）
* @return 返回内部XBitArray的借用指针（禁止释放、禁止修改），
*         unit为NULL、非位类型或容器为空时返回NULL
*/
const XBitArray* XModbusDataUnit_values2_const(const XModbusDataUnit* unit);
/**
* @brief 替换位区数据（仅对Coils/DiscreteInputs有效）。
* @param unit XModbusDataUnit实例指针。
* @param bits 源位数据（借用，内部深拷贝、不取得所有权）。
* @return 成功返回true；unit为NULL、bits为NULL或类型非位类型返回false，
*         对象保持不变。
* @note 整体替换内部位容器（旧容器释放）；新容器长度即 bits 的长度。
*/
bool XModbusDataUnit_setBitArray(XModbusDataUnit* unit, const XBitArray* bits);



/* 释放：XModbusDataUnit 继承 XClass（非 XObject），栈/就地对象统一
 * XClassDeinit（经虚表分派释放容器），堆对象统一 XClassDelete；
 * 不提供独立的 *_deinit/*_delete 宏或函数。 */

/**
* @brief Modbus服务器数据映射表
* @details 存储所有寄存器类型的数据范围
*/
typedef XMap XModbusDataUnitMap;
/**
* @brief 创建Modbus服务器数据映射表（键=XModbusRegisterType，值=XModbusDataUnit）。
* @details 存储所有寄存器类型的数据范围，元素拷贝/移动/析构经XClass多态入口分派
* @return 成功返回新创建的XModbusDataUnitMap实例指针（新对象，调用方负责XClassDelete释放），失败返回NULL
* @note 值对象存取为多态深拷贝/移动/析构（XClass_copy_base/move_base/
*       deinit_base），存入的数据单元由映射表接管释放责任。
*/
XModbusDataUnitMap* XModbusDataUnitMap_create();

#endif /* XMODBUS_CORE_ON */
#endif /* XMODBUS_ON */
#endif /* XPROTOCOL_ON */

#ifdef __cplusplus
}
#endif

/* XClass create API default-memory wrappers. */
/** @brief 默认内存分配器的堆创建（create_ex 便捷宏：Invalid 类型、地址 0xFFFF、无容器）。 */
#undef XModbusDataUnit_create
#define XModbusDataUnit_create() \
	XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, XModbusInvalid, 0xFFFF, 0)

#endif // XMODBUSDATAUNIT_H
