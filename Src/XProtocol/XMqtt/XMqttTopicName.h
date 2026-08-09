#ifndef XMQTTTOPICNAME_H
#define XMQTTTOPICNAME_H

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"
#include "XVector.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttTopicName.h
 * @brief MQTT 主题名称（对齐 Qt 6.8 QMqttTopicName）
 * @details 表示一个 MQTT 主题名称，提供有效性验证、层级解析等功能。
 */

XCLASS_DEFINE_BEGING(XMqttTopicName)
XCLASS_DEFINE_EXTEND_END(XMqttTopicName, XClass)

/**
 * @brief MQTT 主题名称结构体
 * @details 对齐 Qt 6.8 QMqttTopicName，基于 XClass 实现值类型语义。
 */
typedef struct XMqttTopicName {
    XClass m_class;              ///< 基类
    XString* m_name;             ///< 主题名称字符串
} XMqttTopicName;

/**
 * @brief 初始化 XMqttTopicName 的虚函数表
 */
XVtable* XMqttTopicName_class_init(void);

/**
 * @brief 创建 XMqttTopicName 实例
 * @param name 主题名称（UTF-8 字符串，可为 NULL 表示空主题）
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttTopicName* XMqttTopicName_create(const char* name);

/**
 * @brief 创建 XMqttTopicName 的深拷贝
 * @param other 被拷贝的实例指针
 * @return 新创建的拷贝实例指针，失败返回 NULL
 */
XMqttTopicName* XMqttTopicName_create_copy(const XMqttTopicName* other);

/**
 * @brief 初始化 XMqttTopicName 实例
 * @param name 待初始化的实例指针（非 NULL）
 * @param topic 主题名称（UTF-8 字符串，可为 NULL）
 */
void XMqttTopicName_init(XMqttTopicName* name, const char* topic);

/**
 * @brief 获取主题名称字符串
 * @param name 实例指针
 * @return 主题名称字符串（常量引用，调用者不应释放）
 */
const XString* XMqttTopicName_name_const(const XMqttTopicName* name);

/**
 * @brief 获取主题名称字符串（深拷贝）
 * @param name 实例指针
 * @return 主题名称字符串的深拷贝，调用者负责释放
 */
XString* XMqttTopicName_name(const XMqttTopicName* name);

/**
 * @brief 设置主题名称
 * @param name 实例指针（非 NULL）
 * @param topic 新的主题名称（UTF-8 字符串）
 */
void XMqttTopicName_setName(XMqttTopicName* name, const char* topic);

/**
 * @brief 判断主题名称是否有效
 * @param name 实例指针
 * @return 有效返回 true，无效返回 false
 * @details 有效的主题名称不能包含通配符（+、#），且不能为空。
 */
bool XMqttTopicName_isValid(const XMqttTopicName* name);

/**
 * @brief 获取主题层级数量
 * @param name 实例指针
 * @return 层级数量（以 '/' 分隔的段数）
 */
int XMqttTopicName_levelCount(const XMqttTopicName* name);

/**
 * @brief 获取主题层级列表
 * @param name 实例指针
 * @return 层级字符串列表（XVector<XString*>），调用者负责释放
 */
XVector* XMqttTopicName_levels(const XMqttTopicName* name);

/**
 * @brief 比较两个主题名称是否相等
 * @param a 第一个实例指针
 * @param b 第二个实例指针
 * @return 相等返回 true
 */
bool XMqttTopicName_equal(const XMqttTopicName* a, const XMqttTopicName* b);

/**
 * @brief 比较两个主题名称的大小（用于排序）
 * @param a 第一个实例指针
 * @param b 第二个实例指针
 * @return a < b 返回 true
 */
bool XMqttTopicName_less(const XMqttTopicName* a, const XMqttTopicName* b);

/**
 * @brief 计算主题名称的哈希值
 * @param name 实例指针
 * @return 哈希值
 */
size_t XMqttTopicName_hash(const XMqttTopicName* name, size_t seed);

#define XMqttTopicName_copy_base   XClass_copy_base
#define XMqttTopicName_move_base   XClass_move_base
#define XMqttTopicName_deinit_base XClass_deinit_base
#define XMqttTopicName_delete_base XClass_delete_base

#ifdef __cplusplus
}
#endif

#endif // XMQTTTOPICNAME_H
