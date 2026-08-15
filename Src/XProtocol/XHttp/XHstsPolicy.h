/**
 * @file       XHstsPolicy.h
 * - @brief      HTTP Strict Transport Security 策略对象，对标 Qt 6.8 QHstsPolicy。
 * - @details    策略使用 Unix 毫秒时间戳保存过期时间；所有字符串由对象拥有，
 *             实现只使用 XinYueC 分配器和容器。
 */

#ifndef XHSTSPOLICY_H
#define XHSTSPOLICY_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XClass.h"
#include "XByteArray.h"
#include "XVector.h"
#include <stdbool.h>
#include <stdint.h>

XCLASS_DEFINE_BEGING(XHstsPolicy)
XCLASS_DEFINE_EXTEND_END(XHstsPolicy, XClass)

/** @brief QHstsPolicy::IncludeSubDomains 对应的策略标志。 */
#define XHstsPolicy_IncludeSubDomains UINT32_C(1)

/**
 * - @brief HSTS 策略值对象。
 * - @details 空主机或负过期时间表示无效/已过期策略；主机比较不区分 ASCII 大小写。
 */
typedef struct XHstsPolicy {
    XClass m_class;              /**< 第一个成员，继承 XClass。 */
    XByteArray* m_host;          /**< 主机名；对象拥有，保存为小写。 */
    int64_t m_expiryMSecs;       /**< Unix UTC 毫秒；负值表示无效过期时间。 */
    uint32_t m_flags;            /**< 策略标志。 */
} XHstsPolicy;

/**
 * - @brief 初始化 HSTS 策略虚函数表。
 * - @return HSTS 策略虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHstsPolicy_class_init(void);

/**
 * - @brief 初始化 HSTS 策略。
 * - @param self 待初始化对象；不能为 NULL。
 * - @return 无；初始化为空主机、已过期策略。
 */
void XHstsPolicy_init(XHstsPolicy* self);

/**
 * - @brief 创建空 HSTS 策略。
 * - @return 新对象；调用者必须使用 XHstsPolicy_delete_base 释放，失败返回 NULL。
 */
/**
 * - @brief 创建 HSTS 策略。
 * - @param host 主机名；借用，创建时深拷贝；NULL 等价于空主机。
 * - @param expiryMSecs Unix UTC 毫秒过期时间；负值表示已过期。
 * - @param flags 标志，可使用 XHstsPolicy_IncludeSubDomains。
 * - @return 新对象；调用者必须释放，失败返回 NULL。
 */
XHstsPolicy* XHstsPolicy_create_ex(XMemoryType memory, const XByteArray* host, int64_t expiryMSecs, uint32_t flags);

/**
 * - @brief 深拷贝创建 HSTS 策略。
 * - @param other 源策略；不能为 NULL。
 * - @return 新对象；调用者必须释放，失败返回 NULL。
 */
XHstsPolicy* XHstsPolicy_create_copy(const XHstsPolicy* other);

/**
 * - @brief 移动创建 HSTS 策略。
 * - @param other 源策略；成功后资源移出但仍保持可释放状态。
 * - @return 新对象；调用者必须释放，失败返回 NULL。
 */
XHstsPolicy* XHstsPolicy_create_move(XHstsPolicy* other);

/** @brief HSTS 策略生命周期和值语义入口。 */
#define XHstsPolicy_deinit_base XClass_deinit_base
#define XHstsPolicy_delete_base XClass_delete_base
#define XHstsPolicy_copy_base XClass_copy_base
#define XHstsPolicy_move_base XClass_move_base

/**
 * - @brief 设置策略主机。
 * - @param self 策略对象；不能为 NULL。
 * - @param host 主机名；借用，函数执行时深拷贝；NULL 清空主机。
 * - @return 成功返回 true；内存不足返回 false。
 */
bool XHstsPolicy_setHost(XHstsPolicy* self, const XByteArray* host);

/**
 * - @brief 获取策略主机。
 * - @param self 策略对象；可为 NULL。
 * - @return 内部小写主机名；调用者不得释放或修改。
 */
const XByteArray* XHstsPolicy_host_const(const XHstsPolicy* self);

/**
 * - @brief 设置过期时间。
 * - @param self 策略对象；可为 NULL。
 * - @param expiryMSecs Unix UTC 毫秒；负值表示无效策略。
 * - @return 无。
 */
void XHstsPolicy_setExpiryMSecs(XHstsPolicy* self, int64_t expiryMSecs);

/**
 * - @brief 获取过期时间。
 * - @param self 策略对象；可为 NULL。
 * - @return Unix UTC 毫秒；NULL 或未设置时返回 -1。
 */
int64_t XHstsPolicy_expiryMSecs(const XHstsPolicy* self);

/**
 * - @brief 设置是否包含子域名。
 * - @param self 策略对象；可为 NULL。
 * - @param include true 同时约束子域名，false 仅约束当前主机。
 * - @return 无。
 */
void XHstsPolicy_setIncludesSubDomains(XHstsPolicy* self, bool include);

/**
 * - @brief 获取是否包含子域名。
 * - @param self 策略对象；可为 NULL。
 * - @return 包含返回 true；NULL 返回 false。
 */
bool XHstsPolicy_includesSubDomains(const XHstsPolicy* self);

/**
 * - @brief 判断策略是否已经过期或无效。
 * - @param self 策略对象；可为 NULL。
 * - @return 已过期、空主机或 NULL 返回 true。
 */
bool XHstsPolicy_isExpired(const XHstsPolicy* self);

/**
 * - @brief 判断两个策略是否完全相同。
 * - @param lhs 左策略；可为 NULL。
 * - @param rhs 右策略；可为 NULL。
 * - @return 两者都存在且主机、过期时间、标志相同返回 true。
 */
bool XHstsPolicy_equals(const XHstsPolicy* lhs, const XHstsPolicy* rhs);

/**
 * - @brief 交换两个策略的内容。
 * - @param lhs 左策略；不能为 NULL。
 * - @param rhs 右策略；不能为 NULL。
 * - @return 无；XClass 生命周期元数据不交换。
 */
void XHstsPolicy_swap(XHstsPolicy* lhs, XHstsPolicy* rhs);

/**
 * - @brief 释放 HSTS 策略指针列表。
 * - @param policies XVector，元素类型为 XHstsPolicy*；可为 NULL。
 * - @return 无；同时释放列表中的每个策略。
 */
void XHstsPolicy_list_free(XVector* policies);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XHstsPolicy_create
#define XHstsPolicy_create() \
	XHstsPolicy_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, -1, 0)

#endif /* XHSTSPOLICY_H */
