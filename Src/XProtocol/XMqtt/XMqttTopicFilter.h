#ifndef XMQTTTOPICFILTER_H
#define XMQTTTOPICFILTER_H

#include <stdint.h>
#include <stdbool.h>
#include "XClass.h"
#include "XString.h"
#include "XMqttTopicName.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file XMqttTopicFilter.h
 * @brief MQTT 主题过滤器（对齐 Qt 6.8 QMqttTopicFilter）
 * @details 表示一个 MQTT 主题过滤器，支持通配符（+、#）匹配。
 */

/**
 * @brief 主题过滤器匹配选项
 * @details 对齐 Qt 6.8 QMqttTopicFilter::MatchOption
 */
typedef enum {
    XMqttTopicFilter_NoMatchOption = 0x0000,                            ///< 无特殊选项
    XMqttTopicFilter_WildcardsDontMatchDollarTopicMatchOption = 0x0001  ///< 通配符不匹配 $ 开头的主题
} XMqttTopicFilter_MatchOption;

XCLASS_DEFINE_BEGING(XMqttTopicFilter)
XCLASS_DEFINE_EXTEND_END(XMqttTopicFilter, XClass)

/**
 * @brief MQTT 主题过滤器结构体
 * @details 对齐 Qt 6.8 QMqttTopicFilter，基于 XClass 实现值类型语义。
 */
typedef struct XMqttTopicFilter {
    XClass m_class;                ///< 基类
    XString* m_filter;             ///< 过滤器字符串
} XMqttTopicFilter;

/**
 * @brief 初始化 XMqttTopicFilter 的虚函数表
 */
XVtable* XMqttTopicFilter_class_init(void);

/**
 * @brief 创建 XMqttTopicFilter 实例
 * @param filter 过滤器字符串（UTF-8，可为 NULL）
 * @return 新创建的实例指针，失败返回 NULL
 */
XMqttTopicFilter* XMqttTopicFilter_create_ex(XMemoryType memory,  const char* filter);

/**
 * @brief 创建深拷贝
 * @param other 被拷贝的实例指针
 * @return 新创建的拷贝实例指针，失败返回 NULL
 */
XMqttTopicFilter* XMqttTopicFilter_create_copy(const XMqttTopicFilter* other);

/**
 * @brief 初始化 XMqttTopicFilter 实例
 * @param filter 待初始化的实例指针（非 NULL）
 * @param f 过滤器字符串（UTF-8，可为 NULL）
 */
void XMqttTopicFilter_init(XMqttTopicFilter* filter, const char* f);

/**
 * @brief 获取过滤器字符串
 * @param filter 实例指针
 * @return 过滤器字符串（常量引用，调用者不应释放）
 */
const XString* XMqttTopicFilter_filter_const(const XMqttTopicFilter* filter);

/**
 * @brief 获取过滤器字符串（深拷贝）
 * @param filter 实例指针
 * @return 过滤器字符串的深拷贝，调用者负责释放
 */
XString* XMqttTopicFilter_filter(const XMqttTopicFilter* filter);

/**
 * @brief 设置过滤器字符串
 * @param filter 实例指针（非 NULL）
 * @param f 新的过滤器字符串（UTF-8）
 */
void XMqttTopicFilter_setFilter(XMqttTopicFilter* filter, const char* f);

/**
 * @brief 获取共享订阅名称
 * @param filter 实例指针
 * @return 共享订阅名称字符串（深拷贝），非共享订阅返回 NULL，调用者负责释放
 * @details 共享订阅格式：$share/<ShareName>/<filter>
 */
XString* XMqttTopicFilter_sharedSubscriptionName(const XMqttTopicFilter* filter);

/**
 * @brief 判断过滤器是否有效
 * @param filter 实例指针
 * @return 有效返回 true
 */
bool XMqttTopicFilter_isValid(const XMqttTopicFilter* filter);

/**
 * @brief 判断主题名称是否匹配此过滤器
 * @param filter 实例指针
 * @param name 要匹配的主题名称
 * @param matchOptions 匹配选项
 * @return 匹配返回 true
 */
bool XMqttTopicFilter_match(const XMqttTopicFilter* filter, const XMqttTopicName* name, XMqttTopicFilter_MatchOption matchOptions);

/**
 * @brief 比较两个过滤器是否相等
 */
bool XMqttTopicFilter_equal(const XMqttTopicFilter* a, const XMqttTopicFilter* b);

/**
 * @brief 比较两个过滤器的大小（用于排序）
 */
bool XMqttTopicFilter_less(const XMqttTopicFilter* a, const XMqttTopicFilter* b);

/**
 * @brief 计算过滤器的哈希值
 */
size_t XMqttTopicFilter_hash(const XMqttTopicFilter* filter, size_t seed);

#define XMqttTopicFilter_deinit_base XClass_deinit_base
#define XMqttTopicFilter_delete_base XClass_delete_base

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XMqttTopicFilter_create
#define XMqttTopicFilter_create(...) XMqttTopicFilter_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif // XMQTTTOPICFILTER_H
