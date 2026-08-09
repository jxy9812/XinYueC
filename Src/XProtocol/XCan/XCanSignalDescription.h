#ifndef XCANSIGNALDESCRIPTION_H
#define XCANSIGNALDESCRIPTION_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "XCanCommonDefinitions.h"
#include "XString.h"
#include "XMap.h"
#include "XVariant.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XCanSignalDescription.h
 * @brief CAN 信号描述头文件（对齐 Qt6 QCanSignalDescription）
 * @details 定义 CAN 总线信号的完整描述信息，包括信号名称、物理单位、
 *          接收节点、数据源、字节序、数据格式、起始位、位长度、
 *          缩放因子、偏移量、取值范围、多路复用状态等。
 *          这是一个值类型结构体，不继承 XClass。
 *
 * @par 功能特性
 * - 信号基本属性（名称、单位、接收节点、注释）
 * - 数据编码属性（数据源、字节序、数据格式）
 * - 位布局（起始位、位长度）
 * - 物理转换（因子、偏移量、缩放）
 * - 取值范围（最小值、最大值）
 * - 多路复用支持
 *
 * @par 使用示例
 * @code
 * XCanSignalDescription sig;
 * XCanSignalDescription_init(&sig);
 * XCanSignalDescription_setName(&sig, "EngineSpeed");
 * XCanSignalDescription_setStartBit(&sig, 0);
 * XCanSignalDescription_setBitLength(&sig, 16);
 * XCanSignalDescription_setFactor(&sig, 0.125);
 * XCanSignalDescription_setOffset(&sig, 0.0);
 * XCanSignalDescription_setRange(&sig, 0.0, 8000.0);
 * // 使用完毕后清理
 * XCanSignalDescription_deinit(&sig);
 * @endcode
 */

/******************************************************************************************
 * 多路复用值范围结构体
 ******************************************************************************************/

/**
 * @brief 多路复用值范围结构体
 * @details 定义多路复用信号的值范围，包含最小值和最大值。
 *          对齐 Qt6 QCanSignalDescription::MultiplexValueRange。
 */
typedef struct XCanSignalDescription_MultiplexValueRange {
    XVariant* m_minimum;    ///< 最小值
    XVariant* m_maximum;    ///< 最大值
} XCanSignalDescription_MultiplexValueRange;

/******************************************************************************************
 * CAN 信号描述结构体
 ******************************************************************************************/

/**
 * @brief CAN 信号描述结构体
 * @details 封装 CAN 总线信号的完整描述信息，包括编码方式、物理转换、
 *          位布局和多路复用属性等。
 */
typedef struct XCanSignalDescription {
    XString* m_name;                    ///< 信号名称
    XString* m_physicalUnit;            ///< 物理单位（如 "rpm"、"km/h"）
    XString* m_receiver;                ///< 接收节点
    XString* m_comment;                 ///< 注释

    XCanBus_DataSource m_dataSource;    ///< 数据源（Payload 或 FrameId）
    uint8_t m_dataEndian;               ///< 字节序（0=小端, 1=大端）
    XCanBus_DataFormat m_dataFormat;    ///< 数据格式（有符号/无符号/浮点等）

    uint16_t m_startBit;                ///< 起始位位置
    uint16_t m_bitLength;               ///< 位长度

    double m_factor;                    ///< 缩放因子
    double m_offset;                    ///< 偏移量
    double m_scaling;                   ///< 缩放比例（用于显示）

    double m_minimum;                   ///< 物理最小值
    double m_maximum;                   ///< 物理最大值

    XCanBus_MultiplexState m_multiplexState;    ///< 多路复用状态

    XMap* m_multiplexSignals;           ///< 多路复用信号映射表
} XCanSignalDescription;

/******************************************************************************************
 * 初始化与清理
 ******************************************************************************************/

/**
 * @brief 初始化信号描述结构体
 * @param sig 待初始化的信号描述指针（不可为 NULL）
 */
void XCanSignalDescription_init(XCanSignalDescription* sig);

/**
 * @brief 销毁信号描述（释放内部资源）
 * @param sig 待销毁的信号描述指针（可为 NULL）
 */
void XCanSignalDescription_deinit(XCanSignalDescription* sig);

/**
 * @brief 复制信号描述（深拷贝）
 * @param dest 目标信号描述指针（不可为 NULL，需已初始化）
 * @param src 源信号描述指针（不可为 NULL）
 */
void XCanSignalDescription_copy(XCanSignalDescription* dest, const XCanSignalDescription* src);

/**
 * @brief 移动信号描述（转移所有权）
 * @param dest 目标信号描述指针（不可为 NULL，需已初始化）
 * @param src 源信号描述指针（不可为 NULL，移动后源对象被清空）
 */
void XCanSignalDescription_move(XCanSignalDescription* dest, XCanSignalDescription* src);

/**
 * @brief 检查信号描述是否有效
 * @param sig 信号描述指针（不可为 NULL）
 * @return 有效返回 true，否则返回 false
 */
bool XCanSignalDescription_isValid(const XCanSignalDescription* sig);

/******************************************************************************************
 * 属性访问（对齐 QCanSignalDescription）
 ******************************************************************************************/

/**
 * @brief 获取信号名称
 * @param sig 信号描述指针（不可为 NULL）
 * @return 信号名称字符串的深拷贝，调用者负责释放
 */
XString* XCanSignalDescription_name(const XCanSignalDescription* sig);

/**
 * @brief 设置信号名称
 * @param sig 信号描述指针（不可为 NULL）
 * @param name 信号名称
 */
void XCanSignalDescription_setName(XCanSignalDescription* sig, const char* name);

/**
 * @brief 获取物理单位
 * @param sig 信号描述指针（不可为 NULL）
 * @return 物理单位字符串的深拷贝，调用者负责释放
 */
XString* XCanSignalDescription_physicalUnit(const XCanSignalDescription* sig);

/**
 * @brief 设置物理单位
 * @param sig 信号描述指针（不可为 NULL）
 * @param unit 物理单位
 */
void XCanSignalDescription_setPhysicalUnit(XCanSignalDescription* sig, const char* unit);

/**
 * @brief 获取接收节点
 * @param sig 信号描述指针（不可为 NULL）
 * @return 接收节点字符串的深拷贝，调用者负责释放
 */
XString* XCanSignalDescription_receiver(const XCanSignalDescription* sig);

/**
 * @brief 设置接收节点
 * @param sig 信号描述指针（不可为 NULL）
 * @param receiver 接收节点
 */
void XCanSignalDescription_setReceiver(XCanSignalDescription* sig, const char* receiver);

/**
 * @brief 获取注释
 * @param sig 信号描述指针（不可为 NULL）
 * @return 注释字符串的深拷贝，调用者负责释放
 */
XString* XCanSignalDescription_comment(const XCanSignalDescription* sig);

/**
 * @brief 设置注释
 * @param sig 信号描述指针（不可为 NULL）
 * @param text 注释文本
 */
void XCanSignalDescription_setComment(XCanSignalDescription* sig, const char* text);

/**
 * @brief 获取数据源
 * @param sig 信号描述指针（不可为 NULL）
 * @return 数据源枚举值
 */
XCanBus_DataSource XCanSignalDescription_dataSource(const XCanSignalDescription* sig);

/**
 * @brief 设置数据源
 * @param sig 信号描述指针（不可为 NULL）
 * @param source 数据源
 */
void XCanSignalDescription_setDataSource(XCanSignalDescription* sig, XCanBus_DataSource source);

/**
 * @brief 获取字节序
 * @param sig 信号描述指针（不可为 NULL）
 * @return 字节序（0=小端, 1=大端）
 */
uint8_t XCanSignalDescription_dataEndian(const XCanSignalDescription* sig);

/**
 * @brief 设置字节序
 * @param sig 信号描述指针（不可为 NULL）
 * @param endian 字节序（0=小端, 1=大端）
 */
void XCanSignalDescription_setDataEndian(XCanSignalDescription* sig, uint8_t endian);

/**
 * @brief 获取数据格式
 * @param sig 信号描述指针（不可为 NULL）
 * @return 数据格式枚举值
 */
XCanBus_DataFormat XCanSignalDescription_dataFormat(const XCanSignalDescription* sig);

/**
 * @brief 设置数据格式
 * @param sig 信号描述指针（不可为 NULL）
 * @param format 数据格式
 */
void XCanSignalDescription_setDataFormat(XCanSignalDescription* sig, XCanBus_DataFormat format);

/**
 * @brief 获取起始位
 * @param sig 信号描述指针（不可为 NULL）
 * @return 起始位位置
 */
uint16_t XCanSignalDescription_startBit(const XCanSignalDescription* sig);

/**
 * @brief 设置起始位
 * @param sig 信号描述指针（不可为 NULL）
 * @param bit 起始位位置
 */
void XCanSignalDescription_setStartBit(XCanSignalDescription* sig, uint16_t bit);

/**
 * @brief 获取位长度
 * @param sig 信号描述指针（不可为 NULL）
 * @return 位长度
 */
uint16_t XCanSignalDescription_bitLength(const XCanSignalDescription* sig);

/**
 * @brief 设置位长度
 * @param sig 信号描述指针（不可为 NULL）
 * @param length 位长度
 */
void XCanSignalDescription_setBitLength(XCanSignalDescription* sig, uint16_t length);

/**
 * @brief 获取缩放因子
 * @param sig 信号描述指针（不可为 NULL）
 * @return 缩放因子
 */
double XCanSignalDescription_factor(const XCanSignalDescription* sig);

/**
 * @brief 设置缩放因子
 * @param sig 信号描述指针（不可为 NULL）
 * @param factor 缩放因子
 */
void XCanSignalDescription_setFactor(XCanSignalDescription* sig, double factor);

/**
 * @brief 获取偏移量
 * @param sig 信号描述指针（不可为 NULL）
 * @return 偏移量
 */
double XCanSignalDescription_offset(const XCanSignalDescription* sig);

/**
 * @brief 设置偏移量
 * @param sig 信号描述指针（不可为 NULL）
 * @param offset 偏移量
 */
void XCanSignalDescription_setOffset(XCanSignalDescription* sig, double offset);

/**
 * @brief 获取缩放比例
 * @param sig 信号描述指针（不可为 NULL）
 * @return 缩放比例
 */
double XCanSignalDescription_scaling(const XCanSignalDescription* sig);

/**
 * @brief 设置缩放比例
 * @param sig 信号描述指针（不可为 NULL）
 * @param scaling 缩放比例
 */
void XCanSignalDescription_setScaling(XCanSignalDescription* sig, double scaling);

/**
 * @brief 获取物理最小值
 * @param sig 信号描述指针（不可为 NULL）
 * @return 最小值
 */
double XCanSignalDescription_minimum(const XCanSignalDescription* sig);

/**
 * @brief 获取物理最大值
 * @param sig 信号描述指针（不可为 NULL）
 * @return 最大值
 */
double XCanSignalDescription_maximum(const XCanSignalDescription* sig);

/**
 * @brief 设置物理取值范围
 * @param sig 信号描述指针（不可为 NULL）
 * @param minimum 最小值
 * @param maximum 最大值
 */
void XCanSignalDescription_setRange(XCanSignalDescription* sig, double minimum, double maximum);

/**
 * @brief 获取多路复用状态
 * @param sig 信号描述指针（不可为 NULL）
 * @return 多路复用状态
 */
XCanBus_MultiplexState XCanSignalDescription_multiplexState(const XCanSignalDescription* sig);

/**
 * @brief 设置多路复用状态
 * @param sig 信号描述指针（不可为 NULL）
 * @param state 多路复用状态
 */
void XCanSignalDescription_setMultiplexState(XCanSignalDescription* sig, XCanBus_MultiplexState state);

/**
 * @brief 获取多路复用信号映射表
 * @param sig 信号描述指针（不可为 NULL）
 * @return 多路复用信号映射表的深拷贝，调用者负责释放
 */
XMap* XCanSignalDescription_multiplexSignals(const XCanSignalDescription* sig);

/**
 * @brief 清除多路复用信号
 * @param sig 信号描述指针（不可为 NULL）
 */
void XCanSignalDescription_clearMultiplexSignals(XCanSignalDescription* sig);

/**
 * @brief 设置多路复用信号映射表
 * @param sig 信号描述指针（不可为 NULL）
 * @param multiplexorSignals 多路复用信号映射表
 */
void XCanSignalDescription_setMultiplexSignals(XCanSignalDescription* sig, const XMap* multiplexorSignals);

/**
 * @brief 添加多路复用信号（带值范围）
 * @param sig 信号描述指针（不可为 NULL）
 * @param name 信号名称
 * @param ranges 值范围列表（XVector<XCanSignalDescription_MultiplexValueRange>*）
 */
void XCanSignalDescription_addMultiplexSignal(XCanSignalDescription* sig,
    const char* name, const XVector* ranges);

/**
 * @brief 添加多路复用信号（单值）
 * @param sig 信号描述指针（不可为 NULL）
 * @param name 信号名称
 * @param value 值
 */
void XCanSignalDescription_addMultiplexSignal_value(XCanSignalDescription* sig,
    const char* name, const XVariant* value);

#ifdef __cplusplus
}
#endif

#endif // XCANSIGNALDESCRIPTION_H
