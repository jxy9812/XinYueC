/**
 * @file       XHttpHeaders.h
 * - @brief      HTTP 字段集合公开 API。
 * - @details    对齐 Qt 6.8 QHttpHeaders。字段按插入顺序保存，允许同名字段重复；
 *             名称比较遵循 HTTP 的 ASCII 大小写不敏感规则。本模块只依赖
 *             XinYueC 容器和内存接口，不调用 Win32、POSIX、Qt 或其他平台 API。
 */

#ifndef XHTTPHEADERS_H
#define XHTTPHEADERS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XClass.h"
#include "XByteArray.h"
#include <stdbool.h>
#include <stddef.h>

/* XHttpHeaders 只重载 XClass 的生命周期/值语义函数，不增加新的虚函数槽位。 */
XCLASS_DEFINE_BEGING(XHttpHeaders)
XCLASS_DEFINE_EXTEND_END(XHttpHeaders, XClass)

/**
 * - @brief Qt QHttpHeaders::WellKnownHeader 的完整字段枚举。
 * - @details 枚举顺序与 Qt 6.8 和 IANA HTTP Field Name Registry 保持一致；
 *          最后的 Count 仅用于 C API 边界检查，不对应真实 HTTP 字段。
 */
typedef enum XHttpHeaders_WellKnownHeader {
    XHttpHeaders_WellKnownHeader_AIM,
    XHttpHeaders_WellKnownHeader_Accept,
    XHttpHeaders_WellKnownHeader_AcceptAdditions,
    XHttpHeaders_WellKnownHeader_AcceptCH,
    XHttpHeaders_WellKnownHeader_AcceptDatetime,
    XHttpHeaders_WellKnownHeader_AcceptEncoding,
    XHttpHeaders_WellKnownHeader_AcceptFeatures,
    XHttpHeaders_WellKnownHeader_AcceptLanguage,
    XHttpHeaders_WellKnownHeader_AcceptPatch,
    XHttpHeaders_WellKnownHeader_AcceptPost,
    XHttpHeaders_WellKnownHeader_AcceptRanges,
    XHttpHeaders_WellKnownHeader_AcceptSignature,
    XHttpHeaders_WellKnownHeader_AccessControlAllowCredentials,
    XHttpHeaders_WellKnownHeader_AccessControlAllowHeaders,
    XHttpHeaders_WellKnownHeader_AccessControlAllowMethods,
    XHttpHeaders_WellKnownHeader_AccessControlAllowOrigin,
    XHttpHeaders_WellKnownHeader_AccessControlExposeHeaders,
    XHttpHeaders_WellKnownHeader_AccessControlMaxAge,
    XHttpHeaders_WellKnownHeader_AccessControlRequestHeaders,
    XHttpHeaders_WellKnownHeader_AccessControlRequestMethod,
    XHttpHeaders_WellKnownHeader_Age,
    XHttpHeaders_WellKnownHeader_Allow,
    XHttpHeaders_WellKnownHeader_ALPN,
    XHttpHeaders_WellKnownHeader_AltSvc,
    XHttpHeaders_WellKnownHeader_AltUsed,
    XHttpHeaders_WellKnownHeader_Alternates,
    XHttpHeaders_WellKnownHeader_ApplyToRedirectRef,
    XHttpHeaders_WellKnownHeader_AuthenticationControl,
    XHttpHeaders_WellKnownHeader_AuthenticationInfo,
    XHttpHeaders_WellKnownHeader_Authorization,
    XHttpHeaders_WellKnownHeader_CacheControl,
    XHttpHeaders_WellKnownHeader_CacheStatus,
    XHttpHeaders_WellKnownHeader_CalManagedID,
    XHttpHeaders_WellKnownHeader_CalDAVTimezones,
    XHttpHeaders_WellKnownHeader_CapsuleProtocol,
    XHttpHeaders_WellKnownHeader_CDNCacheControl,
    XHttpHeaders_WellKnownHeader_CDNLoop,
    XHttpHeaders_WellKnownHeader_CertNotAfter,
    XHttpHeaders_WellKnownHeader_CertNotBefore,
    XHttpHeaders_WellKnownHeader_ClearSiteData,
    XHttpHeaders_WellKnownHeader_ClientCert,
    XHttpHeaders_WellKnownHeader_ClientCertChain,
    XHttpHeaders_WellKnownHeader_Close,
    XHttpHeaders_WellKnownHeader_Connection,
    XHttpHeaders_WellKnownHeader_ContentDigest,
    XHttpHeaders_WellKnownHeader_ContentDisposition,
    XHttpHeaders_WellKnownHeader_ContentEncoding,
    XHttpHeaders_WellKnownHeader_ContentID,
    XHttpHeaders_WellKnownHeader_ContentLanguage,
    XHttpHeaders_WellKnownHeader_ContentLength,
    XHttpHeaders_WellKnownHeader_ContentLocation,
    XHttpHeaders_WellKnownHeader_ContentRange,
    XHttpHeaders_WellKnownHeader_ContentSecurityPolicy,
    XHttpHeaders_WellKnownHeader_ContentSecurityPolicyReportOnly,
    XHttpHeaders_WellKnownHeader_ContentType,
    XHttpHeaders_WellKnownHeader_Cookie,
    XHttpHeaders_WellKnownHeader_CrossOriginEmbedderPolicy,
    XHttpHeaders_WellKnownHeader_CrossOriginEmbedderPolicyReportOnly,
    XHttpHeaders_WellKnownHeader_CrossOriginOpenerPolicy,
    XHttpHeaders_WellKnownHeader_CrossOriginOpenerPolicyReportOnly,
    XHttpHeaders_WellKnownHeader_CrossOriginResourcePolicy,
    XHttpHeaders_WellKnownHeader_DASL,
    XHttpHeaders_WellKnownHeader_Date,
    XHttpHeaders_WellKnownHeader_DAV,
    XHttpHeaders_WellKnownHeader_DeltaBase,
    XHttpHeaders_WellKnownHeader_Depth,
    XHttpHeaders_WellKnownHeader_Destination,
    XHttpHeaders_WellKnownHeader_DifferentialID,
    XHttpHeaders_WellKnownHeader_DPoP,
    XHttpHeaders_WellKnownHeader_DPoPNonce,
    XHttpHeaders_WellKnownHeader_EarlyData,
    XHttpHeaders_WellKnownHeader_ETag,
    XHttpHeaders_WellKnownHeader_Expect,
    XHttpHeaders_WellKnownHeader_ExpectCT,
    XHttpHeaders_WellKnownHeader_Expires,
    XHttpHeaders_WellKnownHeader_Forwarded,
    XHttpHeaders_WellKnownHeader_From,
    XHttpHeaders_WellKnownHeader_Hobareg,
    XHttpHeaders_WellKnownHeader_Host,
    XHttpHeaders_WellKnownHeader_If,
    XHttpHeaders_WellKnownHeader_IfMatch,
    XHttpHeaders_WellKnownHeader_IfModifiedSince,
    XHttpHeaders_WellKnownHeader_IfNoneMatch,
    XHttpHeaders_WellKnownHeader_IfRange,
    XHttpHeaders_WellKnownHeader_IfScheduleTagMatch,
    XHttpHeaders_WellKnownHeader_IfUnmodifiedSince,
    XHttpHeaders_WellKnownHeader_IM,
    XHttpHeaders_WellKnownHeader_IncludeReferredTokenBindingID,
    XHttpHeaders_WellKnownHeader_KeepAlive,
    XHttpHeaders_WellKnownHeader_Label,
    XHttpHeaders_WellKnownHeader_LastEventID,
    XHttpHeaders_WellKnownHeader_LastModified,
    XHttpHeaders_WellKnownHeader_Link,
    XHttpHeaders_WellKnownHeader_Location,
    XHttpHeaders_WellKnownHeader_LockToken,
    XHttpHeaders_WellKnownHeader_MaxForwards,
    XHttpHeaders_WellKnownHeader_MementoDatetime,
    XHttpHeaders_WellKnownHeader_Meter,
    XHttpHeaders_WellKnownHeader_MIMEVersion,
    XHttpHeaders_WellKnownHeader_Negotiate,
    XHttpHeaders_WellKnownHeader_NEL,
    XHttpHeaders_WellKnownHeader_ODataEntityId,
    XHttpHeaders_WellKnownHeader_ODataIsolation,
    XHttpHeaders_WellKnownHeader_ODataMaxVersion,
    XHttpHeaders_WellKnownHeader_ODataVersion,
    XHttpHeaders_WellKnownHeader_OptionalWWWAuthenticate,
    XHttpHeaders_WellKnownHeader_OrderingType,
    XHttpHeaders_WellKnownHeader_Origin,
    XHttpHeaders_WellKnownHeader_OriginAgentCluster,
    XHttpHeaders_WellKnownHeader_OSCORE,
    XHttpHeaders_WellKnownHeader_OSLCCoreVersion,
    XHttpHeaders_WellKnownHeader_Overwrite,
    XHttpHeaders_WellKnownHeader_PingFrom,
    XHttpHeaders_WellKnownHeader_PingTo,
    XHttpHeaders_WellKnownHeader_Position,
    XHttpHeaders_WellKnownHeader_Prefer,
    XHttpHeaders_WellKnownHeader_PreferenceApplied,
    XHttpHeaders_WellKnownHeader_Priority,
    XHttpHeaders_WellKnownHeader_ProxyAuthenticate,
    XHttpHeaders_WellKnownHeader_ProxyAuthenticationInfo,
    XHttpHeaders_WellKnownHeader_ProxyAuthorization,
    XHttpHeaders_WellKnownHeader_ProxyStatus,
    XHttpHeaders_WellKnownHeader_PublicKeyPins,
    XHttpHeaders_WellKnownHeader_PublicKeyPinsReportOnly,
    XHttpHeaders_WellKnownHeader_Range,
    XHttpHeaders_WellKnownHeader_RedirectRef,
    XHttpHeaders_WellKnownHeader_Referer,
    XHttpHeaders_WellKnownHeader_Refresh,
    XHttpHeaders_WellKnownHeader_ReplayNonce,
    XHttpHeaders_WellKnownHeader_ReprDigest,
    XHttpHeaders_WellKnownHeader_RetryAfter,
    XHttpHeaders_WellKnownHeader_ScheduleReply,
    XHttpHeaders_WellKnownHeader_ScheduleTag,
    XHttpHeaders_WellKnownHeader_SecPurpose,
    XHttpHeaders_WellKnownHeader_SecTokenBinding,
    XHttpHeaders_WellKnownHeader_SecWebSocketAccept,
    XHttpHeaders_WellKnownHeader_SecWebSocketExtensions,
    XHttpHeaders_WellKnownHeader_SecWebSocketKey,
    XHttpHeaders_WellKnownHeader_SecWebSocketProtocol,
    XHttpHeaders_WellKnownHeader_SecWebSocketVersion,
    XHttpHeaders_WellKnownHeader_Server,
    XHttpHeaders_WellKnownHeader_ServerTiming,
    XHttpHeaders_WellKnownHeader_SetCookie,
    XHttpHeaders_WellKnownHeader_Signature,
    XHttpHeaders_WellKnownHeader_SignatureInput,
    XHttpHeaders_WellKnownHeader_SLUG,
    XHttpHeaders_WellKnownHeader_SoapAction,
    XHttpHeaders_WellKnownHeader_StatusURI,
    XHttpHeaders_WellKnownHeader_StrictTransportSecurity,
    XHttpHeaders_WellKnownHeader_Sunset,
    XHttpHeaders_WellKnownHeader_SurrogateCapability,
    XHttpHeaders_WellKnownHeader_SurrogateControl,
    XHttpHeaders_WellKnownHeader_TCN,
    XHttpHeaders_WellKnownHeader_TE,
    XHttpHeaders_WellKnownHeader_Timeout,
    XHttpHeaders_WellKnownHeader_Topic,
    XHttpHeaders_WellKnownHeader_Traceparent,
    XHttpHeaders_WellKnownHeader_Tracestate,
    XHttpHeaders_WellKnownHeader_Trailer,
    XHttpHeaders_WellKnownHeader_TransferEncoding,
    XHttpHeaders_WellKnownHeader_TTL,
    XHttpHeaders_WellKnownHeader_Upgrade,
    XHttpHeaders_WellKnownHeader_Urgency,
    XHttpHeaders_WellKnownHeader_UserAgent,
    XHttpHeaders_WellKnownHeader_VariantVary,
    XHttpHeaders_WellKnownHeader_Vary,
    XHttpHeaders_WellKnownHeader_Via,
    XHttpHeaders_WellKnownHeader_WantContentDigest,
    XHttpHeaders_WellKnownHeader_WantReprDigest,
    XHttpHeaders_WellKnownHeader_WWWAuthenticate,
    XHttpHeaders_WellKnownHeader_XContentTypeOptions,
    XHttpHeaders_WellKnownHeader_XFrameOptions,
    XHttpHeaders_WellKnownHeader_AcceptCharset,
    XHttpHeaders_WellKnownHeader_CPEPInfo,
    XHttpHeaders_WellKnownHeader_Pragma,
    XHttpHeaders_WellKnownHeader_ProtocolInfo,
    XHttpHeaders_WellKnownHeader_ProtocolQuery,
    XHttpHeaders_WellKnownHeader_Count
} XHttpHeaders_WellKnownHeader;

/**
 * - @brief HTTP 单个字段的内部存储项。
 * - @details `m_name` 与 `m_value` 均由所属 XHttpHeaders 拥有；调用者不得直接释放、
 *          替换或修改它们。名称规范化为 ASCII 小写，值去除首尾空白。
 */
typedef struct XHttpHeaderField {
    XByteArray* m_name;   /**< 字段名称；由所属 XHttpHeaders 拥有，不能为空。 */
    XByteArray* m_value;  /**< 字段值；由所属 XHttpHeaders 拥有，可以为空。 */
} XHttpHeaderField;

/**
 * - @brief HTTP 字段集合。
 * - @details 对齐 QHttpHeaders 的有序、多值语义。`m_class` 必须为第一个成员，
 *          由 XClass 管理，禁止调用者手工修改；其余成员仅供实现使用。
 */
typedef struct XHttpHeaders {
    XClass m_class;                /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XHttpHeaderField* m_fields;   /**< 内部字段数组；仅供实现使用，由对象拥有。 */
    size_t m_size;                 /**< 有效字段数量。 */
    size_t m_capacity;             /**< 已分配字段槽位数量；仅供实现使用。 */
} XHttpHeaders;

/**
 * - @brief 初始化 XHttpHeaders 的虚函数表。
 * - @return 成功时返回共享虚函数表；失败时返回 NULL。
 */
XVtable* XHttpHeaders_class_init(void);

/**
 * - @brief 初始化一个栈上或已分配的 HTTP 字段集合。
 * - @param self 待初始化对象；不能为 NULL。
 * - @return 无。初始化后对象为空，可配对调用 XHttpHeaders_deinit_base。
 */
void XHttpHeaders_init(XHttpHeaders* self);

/**
 * - @brief 在堆上创建一个空 HTTP 字段集合。
 * - @return 新对象，调用者必须使用 XHttpHeaders_delete_base 释放；分配失败返回 NULL。
 */
XHttpHeaders* XHttpHeaders_create(void);

/**
 * - @brief 深拷贝创建 HTTP 字段集合。
 * - @param other 源字段集合；NULL 时返回 NULL。
 * - @return 新对象，调用者必须使用 XHttpHeaders_delete_base 释放；失败返回 NULL。
 */
XHttpHeaders* XHttpHeaders_create_copy(const XHttpHeaders* other);

/**
 * - @brief 移动创建 HTTP 字段集合。
 * - @param other 源字段集合；不能为 NULL。成功后源对象保持已初始化但为空的状态。
 * - @return 新对象，调用者必须使用 XHttpHeaders_delete_base 释放；失败返回 NULL。
 */
XHttpHeaders* XHttpHeaders_create_move(XHttpHeaders* other);

/**
 * - @brief 反初始化、删除、深拷贝与移动的 XClass 调度入口。
 * - @details 对齐 QHttpHeaders 的值语义；copy/move 支持未初始化目标并处理自赋值。
 */
#define XHttpHeaders_deinit_base XClass_deinit_base
#define XHttpHeaders_delete_base XClass_delete_base
#define XHttpHeaders_copy_base XClass_copy_base
#define XHttpHeaders_move_base XClass_move_base

/**
 * - @brief 以字节数组形式追加一个 HTTP 字段。
 * - @param self 目标字段集合；不能为 NULL。
 * - @param name 字段名称；借用且不能为空，必须是 HTTP token。
 * - @param value 字段值；借用且不能为空，可为空字节数组，不能包含非法控制字符。
 * - @return 成功返回 true；参数非法或内存不足时返回 false，原对象保持不变。
 */
bool XHttpHeaders_append(XHttpHeaders* self, const XByteArray* name, const XByteArray* value);

/**
 * - @brief 以 UTF-8 C 字符串形式追加一个 HTTP 字段。
 * - @param self 目标字段集合；不能为 NULL。
 * - @param name 字段名称；借用且不能为空，必须是 HTTP token。
 * - @param value 字段值；借用，NULL 等价于空字段值。
 * - @return 成功返回 true；失败时原对象保持不变。
 */
bool XHttpHeaders_append_utf8(XHttpHeaders* self, const char* name, const char* value);

/**
 * - @brief 追加一个 Qt 已知 HTTP 字段。
 * - @param self 目标字段集合；不能为 NULL。
 * - @param name 已知字段枚举值；必须小于 Count。
 * - @param value 字段值；借用，不能包含非法控制字符，首尾空白会被去除。
 * - @return 成功返回 true；参数非法或内存不足返回 false。
 */
bool XHttpHeaders_appendKnown(XHttpHeaders* self,
                              XHttpHeaders_WellKnownHeader name,
                              const XByteArray* value);

/**
 * - @brief 在指定位置插入一个 HTTP 字段。
 * - @param self 目标字段集合；不能为 NULL。
 * - @param index 插入索引，范围为 0 到 size；size 表示尾部追加。
 * - @param name 字段名称；借用且必须为 HTTP token。
 * - @param value 字段值；借用，可为空，不能包含非法控制字符。
 * - @return 成功返回 true；失败时字段顺序与内容保持不变。
 */
bool XHttpHeaders_insert(XHttpHeaders* self, size_t index, const XByteArray* name, const XByteArray* value);

/**
 * - @brief 在指定位置插入一个 Qt 已知 HTTP 字段。
 * - @param self 目标字段集合；不能为 NULL。
 * - @param index 插入索引，范围为 0 到 size。
 * - @param name 已知字段枚举值；必须小于 Count。
 * - @param value 字段值；借用，不能包含非法控制字符，首尾空白会被去除。
 * - @return 成功返回 true；参数非法或内存不足返回 false。
 */
bool XHttpHeaders_insertKnown(XHttpHeaders* self,
                              size_t index,
                              XHttpHeaders_WellKnownHeader name,
                              const XByteArray* value);

/**
 * - @brief 替换指定索引处字段的名称和值。
 * - @param self 目标字段集合；不能为 NULL。
 * - @param index 要替换的索引；超出范围时失败。
 * - @param name 新字段名称；借用且必须为 HTTP token。
 * - @param value 新字段值；借用，可为空，不能包含非法控制字符。
 * - @return 成功返回 true；失败时旧字段保持不变。
 */
bool XHttpHeaders_replace(XHttpHeaders* self, size_t index, const XByteArray* name, const XByteArray* value);

/**
 * - @brief 替换指定位置字段为 Qt 已知 HTTP 字段。
 * - @param self 目标字段集合；不能为 NULL。
 * - @param index 要替换的索引；超出范围时失败。
 * - @param name 已知字段枚举值；必须小于 Count。
 * - @param value 新字段值；借用，不能包含非法控制字符，首尾空白会被去除。
 * - @return 成功返回 true；失败时旧字段保持不变。
 */
bool XHttpHeaders_replaceKnown(XHttpHeaders* self,
                               size_t index,
                               XHttpHeaders_WellKnownHeader name,
                               const XByteArray* value);

/**
 * - @brief 替换首个同名字段，或在不存在时追加字段。
 * - @param self 目标字段集合；不能为 NULL。
 * - @param name 字段名称；借用且必须为 HTTP token，比较时忽略 ASCII 大小写。
 * - @param value 字段值；借用，可为空，不能包含非法控制字符，首尾空白会被去除。
 * - @return 成功返回 true；失败时原对象保持不变。
 * - @note 如果存在多个同名字段，只保留被替换的首个字段并删除其余同名字段。
 */
bool XHttpHeaders_replaceOrAppend(XHttpHeaders* self, const XByteArray* name, const XByteArray* value);

/**
 * - @brief 替换或追加一个 Qt 已知 HTTP 字段。
 * - @param self 目标字段集合；不能为 NULL。
 * - @param name 已知字段枚举值；必须小于 Count。
 * - @param value 新字段值；借用，不能包含非法控制字符，首尾空白会被去除。
 * - @return 成功返回 true；失败返回 false；重复同名字段只保留首个。
 */
bool XHttpHeaders_replaceOrAppendKnown(XHttpHeaders* self,
                                       XHttpHeaders_WellKnownHeader name,
                                       const XByteArray* value);

/**
 * - @brief 判断是否存在指定名称的字段。
 * - @param self 字段集合；NULL 时返回 false。
 * - @param name 要查找的名称；借用，NULL 时返回 false。
 * - @return 存在至少一个同名字段返回 true；比较忽略 ASCII 大小写。
 */
bool XHttpHeaders_contains(const XHttpHeaders* self, const XByteArray* name);

/**
 * - @brief 判断是否存在 Qt 已知 HTTP 字段。
 * - @param self 字段集合；NULL 时返回 false。
 * - @param name 已知字段枚举值；非法值返回 false。
 * - @return 存在返回 true。
 */
bool XHttpHeaders_containsKnown(const XHttpHeaders* self, XHttpHeaders_WellKnownHeader name);

/**
 * - @brief 获取首个同名字段值的深拷贝。
 * - @param self 字段集合；NULL 时返回 NULL。
 * - @param name 要查找的名称；借用，NULL 时返回 NULL。
 * - @return 新建 XByteArray，调用者必须使用 XByteArray_delete_base 释放；未找到返回 NULL。
 */
XByteArray* XHttpHeaders_value(const XHttpHeaders* self, const XByteArray* name);

/**
 * - @brief 获取 Qt 已知字段的首个值深拷贝。
 * - @param self 字段集合；可为 NULL。
 * - @param name 已知字段枚举值；非法值返回 NULL。
 * - @return 新建值对象，调用者必须释放；未找到或失败返回 NULL。
 */
XByteArray* XHttpHeaders_valueKnown(const XHttpHeaders* self,
                                    XHttpHeaders_WellKnownHeader name);

/**
 * - @brief 获取首个同名字段值，不存在时返回默认值的深拷贝。
 * - @param self 字段集合；可为 NULL。
 * - @param name 要查找的名称；借用，NULL 时返回 defaultValue 的拷贝。
 * - @param defaultValue 未找到时使用的默认值；借用，NULL 等价于空字节数组。
 * - @return 新建 XByteArray，调用者必须使用 XByteArray_delete_base 释放；分配失败返回 NULL。
 */
XByteArray* XHttpHeaders_value_or(const XHttpHeaders* self,
                                  const XByteArray* name,
                                  const XByteArray* defaultValue);

/**
 * - @brief 获取 Qt 已知字段的首个值，不存在时返回默认值深拷贝。
 * - @param self 字段集合；可为 NULL。
 * - @param name 已知字段枚举值；非法值按未找到处理。
 * - @param defaultValue 默认值；借用，NULL 等价于空字节数组。
 * - @return 新建值对象，调用者必须释放；分配失败返回 NULL。
 */
XByteArray* XHttpHeaders_valueKnownOr(const XHttpHeaders* self,
                                      XHttpHeaders_WellKnownHeader name,
                                      const XByteArray* defaultValue);

/**
 * - @brief 获取同名字段值列表。
 * - @param self 字段集合；可为 NULL。
 * - @param name 要查找的名称；借用，NULL 时返回空列表。
 * - @return 新建 XVector，元素类型为 XByteArray*；每个元素及向量均由调用者释放，失败返回 NULL。
 */
XVector* XHttpHeaders_values(const XHttpHeaders* self, const XByteArray* name);

/**
 * - @brief 获取 Qt 已知字段的全部值。
 * - @param self 字段集合；可为 NULL。
 * - @param name 已知字段枚举值；非法值返回空列表。
 * - @return 新建 XVector，元素为 XByteArray*；使用 XHttpHeaders_values_free 释放。
 */
XVector* XHttpHeaders_valuesKnown(const XHttpHeaders* self,
                                  XHttpHeaders_WellKnownHeader name);

/**
 * - @brief 释放 XHttpHeaders_values 返回的值列表。
 * - @param values 值列表；可为 NULL，函数会释放其中所有 XByteArray 及列表。
 * - @return 无。
 */
void XHttpHeaders_values_free(XVector* values);

/**
 * - @brief 获取首个同名字段值的借用只读指针。
 * - @param self 字段集合；NULL 时返回 NULL。
 * - @param name 要查找的名称；借用，NULL 时返回 NULL。
 * - @return 对象内部值的只读指针；集合被修改或销毁后失效，调用者不得释放或修改。
 */
const XByteArray* XHttpHeaders_value_const(const XHttpHeaders* self, const XByteArray* name);

/**
 * - @brief 合并所有同名字段值。
 * - @param self 字段集合；NULL 时返回 NULL。
 * - @param name 要查找的名称；借用，NULL 时返回 NULL。
 * - @return 以 `, ` 连接的字段值新对象；调用者必须使用 XByteArray_delete_base 释放；未找到返回 NULL。
 * - @note 对齐 QHttpHeaders::combinedValue；调用方应避免将 Set-Cookie 作为可逗号合并字段处理。
 */
XByteArray* XHttpHeaders_combinedValue(const XHttpHeaders* self, const XByteArray* name);

/**
 * - @brief 合并 Qt 已知字段的全部值。
 * - @param self 字段集合；可为 NULL。
 * - @param name 已知字段枚举值；非法值或未找到返回 NULL。
 * - @return 新建以逗号和空格连接的值；调用者必须释放。
 */
XByteArray* XHttpHeaders_combinedValueKnown(const XHttpHeaders* self,
                                            XHttpHeaders_WellKnownHeader name);

/**
 * - @brief 获取 Qt 已知字段的规范化小写名称。
 * - @param name 已知字段枚举值；非法值返回 NULL。
 * - @return 静态 UTF-8/ASCII 字符串；调用者不得释放或修改。
 */
const char* XHttpHeaders_wellKnownHeaderName(XHttpHeaders_WellKnownHeader name);

/**
 * - @brief 获取指定位置字段名称的借用只读指针。
 * - @param self 字段集合；NULL 时返回 NULL。
 * - @param index 字段索引；超出范围时返回 NULL。
 * - @return 规范化为 ASCII 小写的内部字段名称；调用者不得释放或修改。
 */
const XByteArray* XHttpHeaders_nameAt_const(const XHttpHeaders* self, size_t index);

/**
 * - @brief 获取指定位置字段值的借用只读指针。
 * - @param self 字段集合；NULL 时返回 NULL。
 * - @param index 字段索引；超出范围时返回 NULL。
 * - @return 内部字段值；调用者不得释放或修改。
 */
const XByteArray* XHttpHeaders_valueAt_const(const XHttpHeaders* self, size_t index);

/**
 * - @brief 删除所有指定名称的字段。
 * - @param self 目标字段集合；NULL 时不执行操作。
 * - @param name 要删除的名称；借用，NULL 时不执行操作。
 * - @return 无。字段相等比较忽略 ASCII 大小写。
 */
void XHttpHeaders_removeAll(XHttpHeaders* self, const XByteArray* name);

/**
 * - @brief 删除全部 Qt 已知字段。
 * - @param self 目标字段集合；NULL 时不执行。
 * - @param name 已知字段枚举值；非法值时不执行。
 * - @return 无。
 */
void XHttpHeaders_removeAllKnown(XHttpHeaders* self, XHttpHeaders_WellKnownHeader name);

/**
 * - @brief 删除指定位置的字段。
 * - @param self 目标字段集合；NULL 时不执行操作。
 * - @param index 要删除的索引；超出范围时不执行操作。
 * - @return 无。
 */
void XHttpHeaders_removeAt(XHttpHeaders* self, size_t index);

/**
 * - @brief 清空所有字段并保留已分配容量。
 * - @param self 目标字段集合；NULL 时不执行操作。
 * - @return 无。
 */
void XHttpHeaders_clear(XHttpHeaders* self);

/**
 * - @brief 获取字段数量。
 * - @param self 字段集合；NULL 时返回 0。
 * - @return 当前字段数量。
 */
size_t XHttpHeaders_size(const XHttpHeaders* self);

/**
 * - @brief 判断字段集合是否为空。
 * - @param self 字段集合；NULL 时返回 true。
 * - @return 不包含任何字段返回 true。
 */
bool XHttpHeaders_isEmpty(const XHttpHeaders* self);

/**
 * - @brief 预留字段存储容量。
 * - @param self 字段集合；不能为 NULL。
 * - @param capacity 至少可容纳的字段数量。
 * - @return 成功返回 true；内存不足或 self 为 NULL 返回 false。
 */
bool XHttpHeaders_reserve(XHttpHeaders* self, size_t capacity);

/**
 * - @brief 交换两个字段集合的字段存储。
 * - @param lhs 左字段集合；不能为 NULL。
 * - @param rhs 右字段集合；不能为 NULL。
 * - @return 无；两个对象的 XClass 生命周期元数据保持不变。
 */
void XHttpHeaders_swap(XHttpHeaders* lhs, XHttpHeaders* rhs);

#ifdef __cplusplus
}
#endif

#endif /* XHTTPHEADERS_H */
