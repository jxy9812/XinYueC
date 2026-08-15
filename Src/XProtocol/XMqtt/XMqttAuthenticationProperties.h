#ifndef XMQTTAUTHENTICATIONPROPERTIES_H
#define XMQTTAUTHENTICATIONPROPERTIES_H

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"
#include "XByteArray.h"
#include "XMqttType.h"

#ifdef __cplusplus
extern "C" {
#endif

XCLASS_DEFINE_BEGING(XMqttAuthenticationProperties)
XCLASS_DEFINE_EXTEND_END(XMqttAuthenticationProperties, XClass)

/**
 * @file XMqttAuthenticationProperties.h
 * @brief MQTT 认证属性（对齐 Qt 6.8 QMqttAuthenticationProperties）
 */

/**
 * @brief 认证属性结构体
 * @details 对齐 Qt 6.8 QMqttAuthenticationProperties
 */
typedef struct XMqttAuthenticationProperties {
    XClass m_class;                       ///< 基类
    XString* m_authenticationMethod;      ///< 认证方法
    XByteArray* m_authenticationData;     ///< 认证数据
    XString* m_reason;                    ///< 原因字符串
    XMqttUserProperties* m_userProperties; ///< 用户属性
} XMqttAuthenticationProperties;

/**
 * @brief 初始化 XMqttAuthenticationProperties 的虚函数表
 * @return 指向初始化完成的 XVtable 指针
 */
XVtable* XMqttAuthenticationProperties_class_init(void);

/**
 * @brief 创建认证属性实例
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttAuthenticationProperties* XMqttAuthenticationProperties_create_ex(XMemoryType memory);

/**
 * @brief 创建认证属性的深拷贝
 * @param other 被拷贝的实例指针
 * @return 新创建的拷贝实例指针，失败返回 NULL
 */
XMqttAuthenticationProperties* XMqttAuthenticationProperties_create_copy(const XMqttAuthenticationProperties* other);

/**
 * @brief 初始化认证属性实例
 * @param prop 待初始化的实例指针（非 NULL）
 */
void XMqttAuthenticationProperties_init(XMqttAuthenticationProperties* prop);

/**
 * @brief 获取认证方法（常量引用）
 * @param prop 实例指针
 * @return 认证方法字符串（常量引用，调用者不应释放）
 */
const XString* XMqttAuthenticationProperties_authenticationMethod_const(const XMqttAuthenticationProperties* prop);

/**
 * @brief 获取认证方法（深拷贝）
 * @param prop 实例指针
 * @return 认证方法字符串的深拷贝，调用者负责释放
 */
XString* XMqttAuthenticationProperties_authenticationMethod(const XMqttAuthenticationProperties* prop);

/**
 * @brief 设置认证方法
 * @param prop 实例指针（非 NULL）
 * @param method 认证方法字符串（UTF-8）
 */
void XMqttAuthenticationProperties_setAuthenticationMethod(XMqttAuthenticationProperties* prop, const char* method);

/**
 * @brief 获取认证数据（常量引用）
 * @param prop 实例指针
 * @return 认证数据（常量引用，调用者不应释放）
 */
const XByteArray* XMqttAuthenticationProperties_authenticationData_const(const XMqttAuthenticationProperties* prop);

/**
 * @brief 获取认证数据（深拷贝）
 * @param prop 实例指针
 * @return 认证数据的深拷贝，调用者负责释放
 */
XByteArray* XMqttAuthenticationProperties_authenticationData(const XMqttAuthenticationProperties* prop);

/**
 * @brief 设置认证数据
 * @param prop 实例指针（非 NULL）
 * @param data 认证数据缓冲区
 * @param len 认证数据长度
 */
void XMqttAuthenticationProperties_setAuthenticationData(XMqttAuthenticationProperties* prop, const uint8_t* data, size_t len);

/**
 * @brief 获取原因字符串（常量引用）
 * @param prop 实例指针
 * @return 原因字符串（常量引用，调用者不应释放）
 */
const XString* XMqttAuthenticationProperties_reason_const(const XMqttAuthenticationProperties* prop);

/**
 * @brief 获取原因字符串（深拷贝）
 * @param prop 实例指针
 * @return 原因字符串的深拷贝，调用者负责释放
 */
XString* XMqttAuthenticationProperties_reason(const XMqttAuthenticationProperties* prop);

/**
 * @brief 设置原因字符串
 * @param prop 实例指针（非 NULL）
 * @param r 原因字符串（UTF-8）
 */
void XMqttAuthenticationProperties_setReason(XMqttAuthenticationProperties* prop, const char* r);

/**
 * @brief 获取用户属性（常量引用）
 * @param prop 实例指针
 * @return 用户属性列表（常量引用，调用者不应释放）
 */
const XMqttUserProperties* XMqttAuthenticationProperties_userProperties_const(const XMqttAuthenticationProperties* prop);

/**
 * @brief 获取用户属性（深拷贝）
 * @param prop 实例指针
 * @return 用户属性列表的深拷贝，调用者负责释放
 */
XMqttUserProperties* XMqttAuthenticationProperties_userProperties(const XMqttAuthenticationProperties* prop);

/**
 * @brief 设置用户属性
 * @param prop 实例指针（非 NULL）
 * @param user 用户属性列表
 */
void XMqttAuthenticationProperties_setUserProperties(XMqttAuthenticationProperties* prop, const XMqttUserProperties* user);

#define XMqttAuthenticationProperties_copy_base   XClass_copy_base
#define XMqttAuthenticationProperties_move_base   XClass_move_base
#define XMqttAuthenticationProperties_deinit_base XClass_deinit_base
#define XMqttAuthenticationProperties_delete_base XClass_delete_base

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XMqttAuthenticationProperties_create
#define XMqttAuthenticationProperties_create() XMqttAuthenticationProperties_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMQTTAUTHENTICATIONPROPERTIES_H
