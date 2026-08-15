/**
 * @file       XNetworkCookie.h
 * - @brief      HTTP Cookie 与 CookieJar 类型，对标 Qt 6.8 QNetworkCookie。
 * - @details    Cookie 只使用 XinYueC 容器和时间接口，不直接调用平台 API。
 */

#ifndef XNETWORKCOOKIE_H
#define XNETWORKCOOKIE_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XByteArray.h"
#include "XHttpHeaders.h"
#include "XObject.h"
#include "XUrl.h"
#include "XVector.h"
#include <stdbool.h>
#include <stdint.h>

XCLASS_DEFINE_BEGING(XNetworkCookie)
XCLASS_DEFINE_EXTEND_END(XNetworkCookie, XClass)

/** @brief Cookie 原始文本输出形式。 */
typedef enum XNetworkCookie_RawForm {
    XNetworkCookie_NameAndValueOnly = 0, /**< 只输出 name=value。 */
    XNetworkCookie_Full              = 1  /**< 输出属性。 */
} XNetworkCookie_RawForm;

/** @brief SameSite 策略，对标 QNetworkCookie::SameSite。 */
typedef enum XNetworkCookie_SameSite {
    XNetworkCookie_SameSiteDefault = 0, /**< 未指定。 */
    XNetworkCookie_SameSiteNone,         /**< None。 */
    XNetworkCookie_SameSiteLax,          /**< Lax。 */
    XNetworkCookie_SameSiteStrict        /**< Strict。 */
} XNetworkCookie_SameSite;

/**
 * - @brief 一个 HTTP Cookie。
 * - @details name/value/domain/path 均由对象拥有；过期时间使用 Unix 秒，负值表示未设置。
 */
typedef struct XNetworkCookie {
    XClass m_class;                       /**< 第一个成员，继承 XClass。 */
    XByteArray* m_name;                  /**< Cookie 名称；对象拥有。 */
    XByteArray* m_value;                 /**< Cookie 值；对象拥有。 */
    XByteArray* m_domain;                /**< Domain；对象拥有。 */
    XByteArray* m_path;                  /**< Path；对象拥有。 */
    int64_t m_expirationSecs;            /**< Expires Unix 秒；负值表示会话 Cookie。 */
    int64_t m_maxAge;                    /**< Max-Age 秒；负值表示未设置。 */
    bool m_secure;                       /**< 是否仅允许 HTTPS。 */
    bool m_httpOnly;                     /**< 是否标记 HttpOnly。 */
    XNetworkCookie_SameSite m_sameSite; /**< SameSite 策略。 */
} XNetworkCookie;

/**
 * - @brief 初始化 HTTP Cookie 虚函数表。
 * - @return Cookie 虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XNetworkCookie_class_init(void);

/**
 * - @brief 创建 Cookie。
 * - @param name 名称；借用，创建时深拷贝；可为空以创建空对象。
 * - @param value 值；借用，创建时深拷贝；可为 NULL。
 * - @return 新 Cookie；调用者必须使用 XNetworkCookie_delete_base 释放。
 */
XNetworkCookie* XNetworkCookie_create_ex(XMemoryType memory,  const XByteArray* name, const XByteArray* value);

/**
 * - @brief 创建空 HTTP Cookie。
 * - @return 新建 Cookie；调用者必须使用 XNetworkCookie_delete_base 释放，分配失败返回 NULL。
 */
XNetworkCookie* XNetworkCookie_create_empty(void);

/**
 * - @brief 深拷贝 Cookie。
 * - @param other 源 Cookie；不能为 NULL。
 * - @return 新 Cookie；失败返回 NULL。
 */
XNetworkCookie* XNetworkCookie_create_copy(const XNetworkCookie* other);

/**
 * - @brief 移动创建 Cookie。
 * - @param other 源 Cookie；成功后拥有成员被移走。
 * - @return 新 Cookie；失败返回 NULL。
 */
XNetworkCookie* XNetworkCookie_create_move(XNetworkCookie* other);

/**
 * - @brief 初始化 Cookie。
 * - @param self 待初始化对象；不能为 NULL。
 */
void XNetworkCookie_init(XNetworkCookie* self);

/** @brief Cookie 生命周期与值语义入口。 */
#define XNetworkCookie_deinit_base XClass_deinit_base
#define XNetworkCookie_delete_base XClass_delete_base
#define XNetworkCookie_copy_base XClass_copy_base
#define XNetworkCookie_move_base XClass_move_base

/**
 * - @brief 设置 Cookie 名称。
 * - @param self 目标 Cookie；不能为 NULL。
 * - @param name 名称；借用，函数执行时深拷贝；不能为空且不能含控制分隔符。
 * - @return 成功返回 true；参数非法或内存不足返回 false。
 */
bool XNetworkCookie_setName(XNetworkCookie* self, const XByteArray* name);
/**
 * - @brief 获取名称借用指针。
 * - @param self Cookie；可为 NULL。
 * - @return 内部名称；对象修改或销毁后失效，调用者不得释放。
 */
const XByteArray* XNetworkCookie_name_const(const XNetworkCookie* self);
/**
 * - @brief 设置 Cookie 值。
 * - @param self 目标 Cookie；不能为 NULL。
 * - @param value 值；借用，函数执行时深拷贝；NULL 表示空值。
 * - @return 成功返回 true；内存不足返回 false。
 */
bool XNetworkCookie_setValue(XNetworkCookie* self, const XByteArray* value);
/**
 * - @brief 获取值借用指针。
 * - @param self Cookie；可为 NULL。
 * - @return 内部值；调用者不得释放。
 */
const XByteArray* XNetworkCookie_value_const(const XNetworkCookie* self);
/**
 * - @brief 设置 Domain。
 * - @param self 目标 Cookie；不能为 NULL。
 * - @param domain 域名；借用，函数执行时深拷贝；NULL 表示清除。
 * - @return 成功返回 true；内存不足返回 false。
 */
bool XNetworkCookie_setDomain(XNetworkCookie* self, const XByteArray* domain);
/**
 * - @brief 获取 Cookie Domain。
 * - @param self Cookie 对象；可为 NULL。
 * - @return 内部 Domain 借用指针；未设置或 self 为空时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XNetworkCookie_domain_const(const XNetworkCookie* self);
/**
 * - @brief 设置 Path。
 * - @param self 目标 Cookie；不能为 NULL。
 * - @param path 路径；借用，函数执行时深拷贝；NULL 表示清除。
 * - @return 成功返回 true；内存不足返回 false。
 */
bool XNetworkCookie_setPath(XNetworkCookie* self, const XByteArray* path);
/**
 * - @brief 获取 Cookie Path。
 * - @param self Cookie 对象；可为 NULL。
 * - @return 内部 Path 借用指针；未设置或 self 为空时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XNetworkCookie_path_const(const XNetworkCookie* self);
/**
 * - @brief 设置 Secure 标志。
 * - @param self 目标 Cookie；可为 NULL，NULL 时不执行。
 * - @param enabled true 表示仅 HTTPS 发送，false 表示允许 HTTP。
 */
void XNetworkCookie_setSecure(XNetworkCookie* self, bool enabled);
/**
 * - @brief 判断 Cookie 是否设置 Secure 标志。
 * - @param self Cookie 对象；可为 NULL。
 * - @return 设置 Secure 标志时返回 true；self 为空时返回 false。
 */
bool XNetworkCookie_isSecure(const XNetworkCookie* self);
/**
 * - @brief 设置 HttpOnly 标志。
 * - @param self 目标 Cookie；可为 NULL，NULL 时不执行。
 * - @param enabled true 表示设置 HttpOnly 属性。
 */
void XNetworkCookie_setHttpOnly(XNetworkCookie* self, bool enabled);
/**
 * - @brief 判断 Cookie 是否设置 HttpOnly 标志。
 * - @param self Cookie 对象；可为 NULL。
 * - @return 设置 HttpOnly 标志时返回 true；self 为空时返回 false。
 */
bool XNetworkCookie_isHttpOnly(const XNetworkCookie* self);
/**
 * - @brief 设置 SameSite 策略。
 * - @param self 目标 Cookie；可为 NULL，NULL 时不执行。
 * - @param policy SameSite 策略；无效值按 Default 处理。
 */
void XNetworkCookie_setSameSitePolicy(XNetworkCookie* self, XNetworkCookie_SameSite policy);
/**
 * - @brief 获取 Cookie SameSite 策略。
 * - @param self Cookie 对象；可为 NULL。
 * - @return SameSite 策略；self 为空时返回 XNetworkCookie_SameSiteDefault。
 */
XNetworkCookie_SameSite XNetworkCookie_sameSitePolicy(const XNetworkCookie* self);
/**
 * - @brief 设置 Expires Unix 秒。
 * - @param self 目标 Cookie；可为 NULL，NULL 时不执行。
 * - @param seconds Unix UTC 秒；负值表示清除 Expires。
 */
void XNetworkCookie_setExpirationSecs(XNetworkCookie* self, int64_t seconds);
/**
 * - @brief 获取 Cookie Expires 时间。
 * - @param self Cookie 对象；可为 NULL。
 * - @return Unix UTC 秒时间戳；未设置或 self 为空时返回 -1。
 */
int64_t XNetworkCookie_expirationSecs(const XNetworkCookie* self);
/**
 * - @brief 设置 Max-Age 秒。
 * - @param self 目标 Cookie；可为 NULL，NULL 时不执行。
 * - @param seconds Max-Age 秒数；负值表示未设置，零表示立即删除。
 */
void XNetworkCookie_setMaxAge(XNetworkCookie* self, int64_t seconds);
/**
 * - @brief 获取 Cookie Max-Age。
 * - @param self Cookie 对象；可为 NULL。
 * - @return Max-Age 秒数；未设置或 self 为空时返回 -1，0 表示立即删除。
 */
int64_t XNetworkCookie_maxAge(const XNetworkCookie* self);
/**
 * - @brief 判断是否为会话 Cookie。
 * - @param self Cookie 对象；可为 NULL。
 * - @return 未设置 Expires 和 Max-Age 时返回 true；self 为空时返回 false。
 */
bool XNetworkCookie_isSessionCookie(const XNetworkCookie* self);

/**
 * - @brief 输出 Cookie 原始文本。
 * - @param self Cookie；不能为 NULL。
 * - @param form 输出 NameAndValueOnly 或 Full。
 * - @return 新字节数组；调用者必须使用 XClass_delete_base 释放。
 */
XByteArray* XNetworkCookie_toRawForm(const XNetworkCookie* self, XNetworkCookie_RawForm form);

/**
 * - @brief 解析一个 Set-Cookie 字段值。
 * - @param cookieString 字段值；借用，不包含字段名。
 * - @return 新 Cookie 指针列表；列表和元素均由调用者释放，失败返回 NULL。
 */
XVector* XNetworkCookie_parseCookies(const XByteArray* cookieString);

/**
 * - @brief 判断两个 Cookie 是否具有相同 name/domain/path 标识。
 * - @param self 第一个 Cookie；不能为 NULL。
 * - @param other 第二个 Cookie；不能为 NULL。
 * - @return 标识相同返回 true。
 */
bool XNetworkCookie_hasSameIdentifier(const XNetworkCookie* self, const XNetworkCookie* other);

/**
 * - @brief 根据 URL 补全缺省 Domain 和 Path。
 * - @param self Cookie；不能为 NULL。
 * - @param url 来源 URL；借用，不能为 NULL。
 * - @return 成功返回 true；URL 无效或内存不足返回 false。
 */
bool XNetworkCookie_normalize(XNetworkCookie* self, const XUrl* url);

XCLASS_DEFINE_BEGING(XNetworkCookieJar)
XCLASS_DEFINE_EXTEND_END(XNetworkCookieJar, XObject)

/**
 * - @brief 内存 Cookie 容器，对标 QNetworkCookieJar。
 * - @details Jar 拥有全部 Cookie，并按域名、路径、安全属性过滤返回结果。
 */
typedef struct XNetworkCookieJar {
    XObject m_class;          /**< 第一个成员，继承 XObject。 */
    XVector* m_cookies;      /**< XNetworkCookie* 列表，由 Jar 拥有。 */
} XNetworkCookieJar;

/**
 * - @brief 初始化 CookieJar 虚函数表。
 * - @return CookieJar 虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XNetworkCookieJar_class_init(void);
/**
 * - @brief 创建空 CookieJar。
 * - @return 新建 CookieJar；调用者必须使用 XNetworkCookieJar_delete_base 释放，分配失败返回 NULL。
 */
XNetworkCookieJar* XNetworkCookieJar_create_ex(XMemoryType memory);
/**
 * - @brief 初始化 CookieJar。
 * - @param self 待初始化的 CookieJar；不能为 NULL。
 */
void XNetworkCookieJar_init(XNetworkCookieJar* self);
/** @brief CookieJar 生命周期入口。 */
#define XNetworkCookieJar_deinit_base XClass_deinit_base
#define XNetworkCookieJar_delete_base XClass_delete_base
#define XNetworkCookieJar_deleteLater XObject_deleteLater

/**
 * - @brief 插入 Cookie。
 * - @param self Jar；不能为 NULL。
 * - @param cookie Cookie；借用，函数执行时深拷贝；同标识会替换，过期值会删除。
 * - @return 成功返回 true；参数或内存无效返回 false。
 */
bool XNetworkCookieJar_insertCookie(XNetworkCookieJar* self, const XNetworkCookie* cookie);
/**
 * - @brief 更新 Cookie。
 * - @param self CookieJar；不能为 NULL。
 * - @param cookie Cookie；借用且不能为 NULL，执行时深拷贝，同标识 Cookie 会被替换，过期值会删除。
 * - @return 更新成功返回 true；参数无效或内存不足返回 false。
 */
bool XNetworkCookieJar_updateCookie(XNetworkCookieJar* self, const XNetworkCookie* cookie);
/**
 * - @brief 删除同标识 Cookie。
 * - @param self CookieJar；不能为 NULL。
 * - @param cookie Cookie 标识来源；借用且不能为 NULL，按 name、domain、path 匹配。
 * - @return 找到并删除 Cookie 返回 true；参数无效或未找到返回 false。
 */
bool XNetworkCookieJar_deleteCookie(XNetworkCookieJar* self, const XNetworkCookie* cookie);
/**
 * - @brief 获取匹配 URL 的 Cookie 副本列表。
 * - @param self CookieJar；可为 NULL。
 * - @param url 请求 URL；借用且不能为 NULL，用于 Domain、Path 和 Secure 过滤。
 * - @return 新建 Cookie 列表，元素类型为 XNetworkCookie*；调用者负责释放元素和列表，参数无效或分配失败返回 NULL。
 */
XVector* XNetworkCookieJar_cookiesForUrl(const XNetworkCookieJar* self, const XUrl* url);
/**
 * - @brief 解析并保存响应 Set-Cookie 字段值。
 * - @param self CookieJar；不能为 NULL。
 * - @param setCookieValues Set-Cookie 字段值列表；借用，元素类型为 XByteArray*。
 * - @param url 响应来源 URL；借用且不能为 NULL，用于补全缺省 Domain 和 Path。
 * - @return 全部可解析 Cookie 成功保存返回 true；参数无效或出现内存错误返回 false。
 */
bool XNetworkCookieJar_setCookiesFromUrl(XNetworkCookieJar* self, const XVector* setCookieValues,
                                         const XUrl* url);
/**
 * - @brief 从响应头解析并保存全部 Set-Cookie 字段。
 * - @param self CookieJar；不能为 NULL。
 * - @param headers 响应头；借用且不能为 NULL。
 * - @param url 响应来源 URL；借用且不能为 NULL，用于补全缺省 Domain 和 Path。
 * - @return 全部可解析 Cookie 成功保存返回 true；参数无效或出现内存错误返回 false。
 */
bool XNetworkCookieJar_setCookiesFromHeaders(XNetworkCookieJar* self, const XHttpHeaders* headers,
                                             const XUrl* url);
/**
 * - @brief 生成请求 Cookie 头字段值。
 * - @param self CookieJar；可为 NULL。
 * - @param url 请求 URL；借用且不能为 NULL，用于 Domain、Path 和 Secure 过滤。
 * - @return 新建 Cookie 头值；调用者必须使用 XByteArray_delete_base 释放，参数无效或分配失败返回 NULL。
 */
XByteArray* XNetworkCookieJar_cookieHeader(const XNetworkCookieJar* self, const XUrl* url);
/**
 * - @brief 获取 CookieJar 中的 Cookie 数量。
 * - @param self CookieJar；可为 NULL。
 * - @return Cookie 数量；self 为空时返回 0。
 */
size_t XNetworkCookieJar_size(const XNetworkCookieJar* self);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XNetworkCookie_create
#define XNetworkCookie_create(...) XNetworkCookie_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)
#undef XNetworkCookieJar_create
#define XNetworkCookieJar_create(...) XNetworkCookieJar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif
