#ifndef XMQTTTYPE_H
#define XMQTTTYPE_H

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttType.h
 * @brief MQTT 基础类型定义（对齐 Qt 6.8 QMqttStringPair / QMqttUserProperties）
 * @details 定义 MQTT 协议中使用的键值对字符串和用户属性列表。
 */

/* ---------- XMqttStringPair ---------- */

/**
 * @brief MQTT 字符串键值对结构体
 * @details 对齐 Qt 6.8 QMqttStringPair，用于 MQTT 5.0 属性中的用户属性。
 */
typedef struct XMqttStringPair {
    XClass m_class;           ///< 基类
    XString m_name;           ///< 键名
    XString m_value;          ///< 键值
} XMqttStringPair;

/**
 * @brief 初始化 XMqttStringPair 的虚函数表
 */
XVtable* XMqttStringPair_class_init(void);

/**
 * @brief 创建 XMqttStringPair 实例
 * @param name 键名
 * @param value 键值
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttStringPair* XMqttStringPair_create(const char* name, const char* value);

/**
 * @brief 初始化 XMqttStringPair 实例
 * @param pair 待初始化的实例指针（非 NULL）
 * @param name 键名
 * @param value 键值
 */
void XMqttStringPair_init(XMqttStringPair* pair, const char* name, const char* value);

/**
 * @brief 获取键名
 * @param pair 实例指针
 * @return 键名字符串（常量引用，调用者不应释放）
 */
const XString* XMqttStringPair_name_const(const XMqttStringPair* pair);

/**
 * @brief 获取键名（深拷贝）
 * @param pair 实例指针
 * @return 键名字符串的深拷贝，调用者负责释放
 */
XString* XMqttStringPair_name(const XMqttStringPair* pair);

/**
 * @brief 设置键名
 * @param pair 实例指针（非 NULL）
 * @param n 键名
 */
void XMqttStringPair_setName(XMqttStringPair* pair, const char* n);

/**
 * @brief 获取键值
 * @param pair 实例指针
 * @return 键值字符串（常量引用，调用者不应释放）
 */
const XString* XMqttStringPair_value_const(const XMqttStringPair* pair);

/**
 * @brief 获取键值（深拷贝）
 * @param pair 实例指针
 * @return 键值字符串的深拷贝，调用者负责释放
 */
XString* XMqttStringPair_value(const XMqttStringPair* pair);

/**
 * @brief 设置键值
 * @param pair 实例指针（非 NULL）
 * @param v 键值
 */
void XMqttStringPair_setValue(XMqttStringPair* pair, const char* v);

/**
 * @brief 比较两个键值对是否相等
 * @param a 第一个实例指针
 * @param b 第二个实例指针
 * @return 相等返回 true
 */
bool XMqttStringPair_equal(const XMqttStringPair* a, const XMqttStringPair* b);

#define XMqttStringPair_copy_base   XClass_copy_base
#define XMqttStringPair_move_base   XClass_move_base
#define XMqttStringPair_deinit_base XClass_deinit_base
#define XMqttStringPair_delete_base XClass_delete_base

/* ---------- XMqttUserProperties ---------- */

/**
 * @brief MQTT 用户属性列表
 * @details 对齐 Qt 6.8 QMqttUserProperties，继承自 XVector<XMqttStringPair>。
 */
typedef XVector XMqttUserProperties;

/**
 * @brief 创建用户属性列表
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttUserProperties* XMqttUserProperties_create(void);

#define XMqttUserProperties_delete_base XVector_delete_base

#ifdef __cplusplus
}
#endif

#endif // XMQTTTYPE_H
