#ifndef XMQTTSUBSCRIPTIONPROPERTIES_H
#define XMQTTSUBSCRIPTIONPROPERTIES_H

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XMqttType.h"

#ifdef __cplusplus
extern "C" {
#endif

XCLASS_DEFINE_BEGING(XMqttSubscriptionProperties)
XCLASS_DEFINE_EXTEND_END(XMqttSubscriptionProperties, XClass)

/**
 * @file XMqttSubscriptionProperties.h
 * @brief MQTT 订阅/取消订阅属性（对齐 Qt 6.8 QMqttSubscriptionProperties / QMqttUnsubscriptionProperties）
 */

/* ---------- XMqttSubscriptionProperties ---------- */

/**
 * @brief 订阅属性结构体
 * @details 对齐 Qt 6.8 QMqttSubscriptionProperties
 */
typedef struct XMqttSubscriptionProperties {
    XClass m_class;                       ///< 基类
    XMqttUserProperties* m_userProperties; ///< 用户属性
    uint32_t m_subscriptionIdentifier;    ///< 订阅标识符
    bool m_noLocal;                       ///< 是否不接收本地发布的消息
} XMqttSubscriptionProperties;

/**
 * @brief 初始化 XMqttSubscriptionProperties 的虚函数表
 * @return 指向初始化完成的 XVtable 指针
 */
XVtable* XMqttSubscriptionProperties_class_init(void);

/**
 * @brief 创建订阅属性实例
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttSubscriptionProperties* XMqttSubscriptionProperties_create_ex(XMemoryType memory);

/**
 * @brief 创建订阅属性的深拷贝
 * @param other 被拷贝的实例指针
 * @return 新创建的拷贝实例指针，失败返回 NULL
 */
XMqttSubscriptionProperties* XMqttSubscriptionProperties_create_copy(const XMqttSubscriptionProperties* other);

/**
 * @brief 初始化订阅属性实例
 * @param prop 待初始化的实例指针（非 NULL）
 */
void XMqttSubscriptionProperties_init(XMqttSubscriptionProperties* prop);

/**
 * @brief 获取用户属性（常量引用）
 * @param prop 实例指针
 * @return 用户属性列表（常量引用，调用者不应释放）
 */
const XMqttUserProperties* XMqttSubscriptionProperties_userProperties_const(const XMqttSubscriptionProperties* prop);

/**
 * @brief 获取用户属性（深拷贝）
 * @param prop 实例指针
 * @return 用户属性列表的深拷贝，调用者负责释放
 */
XMqttUserProperties* XMqttSubscriptionProperties_userProperties(const XMqttSubscriptionProperties* prop);

/**
 * @brief 设置用户属性
 * @param prop 实例指针（非 NULL）
 * @param user 用户属性列表
 */
void XMqttSubscriptionProperties_setUserProperties(XMqttSubscriptionProperties* prop, const XMqttUserProperties* user);

/**
 * @brief 获取订阅标识符
 * @param prop 实例指针
 * @return 订阅标识符
 */
uint32_t XMqttSubscriptionProperties_subscriptionIdentifier(const XMqttSubscriptionProperties* prop);

/**
 * @brief 设置订阅标识符
 * @param prop 实例指针（非 NULL）
 * @param id 订阅标识符
 */
void XMqttSubscriptionProperties_setSubscriptionIdentifier(XMqttSubscriptionProperties* prop, uint32_t id);

/**
 * @brief 是否不接收本地发布的消息
 * @param prop 实例指针
 * @return 不接收本地消息返回 true
 */
bool XMqttSubscriptionProperties_noLocal(const XMqttSubscriptionProperties* prop);

/**
 * @brief 设置是否不接收本地发布的消息
 * @param prop 实例指针（非 NULL）
 * @param noloc 是否不接收本地消息
 */
void XMqttSubscriptionProperties_setNoLocal(XMqttSubscriptionProperties* prop, bool noloc);

#define XMqttSubscriptionProperties_copy_base   XClass_copy_base
#define XMqttSubscriptionProperties_move_base   XClass_move_base
#define XMqttSubscriptionProperties_deinit_base XClass_deinit_base
#define XMqttSubscriptionProperties_delete_base XClass_delete_base

/* ---------- XMqttUnsubscriptionProperties ---------- */

/**
 * @brief 取消订阅属性结构体
 * @details 对齐 Qt 6.8 QMqttUnsubscriptionProperties
 */
XCLASS_DEFINE_BEGING(XMqttUnsubscriptionProperties)
XCLASS_DEFINE_EXTEND_END(XMqttUnsubscriptionProperties, XClass)

typedef struct XMqttUnsubscriptionProperties {
    XClass m_class;                       ///< 基类
    XMqttUserProperties* m_userProperties; ///< 用户属性
} XMqttUnsubscriptionProperties;

/**
 * @brief 初始化 XMqttUnsubscriptionProperties 的虚函数表
 * @return 指向初始化完成的 XVtable 指针
 */
XVtable* XMqttUnsubscriptionProperties_class_init(void);

/**
 * @brief 创建取消订阅属性实例
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttUnsubscriptionProperties* XMqttUnsubscriptionProperties_create_ex(XMemoryType memory);

/**
 * @brief 创建取消订阅属性的深拷贝
 * @param other 被拷贝的实例指针
 * @return 新创建的拷贝实例指针，失败返回 NULL
 */
XMqttUnsubscriptionProperties* XMqttUnsubscriptionProperties_create_copy(const XMqttUnsubscriptionProperties* other);

/**
 * @brief 初始化取消订阅属性实例
 * @param prop 待初始化的实例指针（非 NULL）
 */
void XMqttUnsubscriptionProperties_init(XMqttUnsubscriptionProperties* prop);

/**
 * @brief 获取用户属性（常量引用）
 * @param prop 实例指针
 * @return 用户属性列表（常量引用，调用者不应释放）
 */
const XMqttUserProperties* XMqttUnsubscriptionProperties_userProperties_const(const XMqttUnsubscriptionProperties* prop);

/**
 * @brief 获取用户属性（深拷贝）
 * @param prop 实例指针
 * @return 用户属性列表的深拷贝，调用者负责释放
 */
XMqttUserProperties* XMqttUnsubscriptionProperties_userProperties(const XMqttUnsubscriptionProperties* prop);

/**
 * @brief 设置用户属性
 * @param prop 实例指针（非 NULL）
 * @param user 用户属性列表
 */
void XMqttUnsubscriptionProperties_setUserProperties(XMqttUnsubscriptionProperties* prop, const XMqttUserProperties* user);

#define XMqttUnsubscriptionProperties_copy_base   XClass_copy_base
#define XMqttUnsubscriptionProperties_move_base   XClass_move_base
#define XMqttUnsubscriptionProperties_deinit_base XClass_deinit_base
#define XMqttUnsubscriptionProperties_delete_base XClass_delete_base

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XMqttSubscriptionProperties_create
#define XMqttSubscriptionProperties_create() XMqttSubscriptionProperties_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)
#undef XMqttUnsubscriptionProperties_create
#define XMqttUnsubscriptionProperties_create() XMqttUnsubscriptionProperties_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

#endif // XMQTTSUBSCRIPTIONPROPERTIES_H
