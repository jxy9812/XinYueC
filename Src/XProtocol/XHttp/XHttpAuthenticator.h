/**
 * @file       XHttpAuthenticator.h
 * - @brief      HTTP 认证凭据值对象，对齐 Qt 6.8 QAuthenticator。
 * - @details    认证器保存服务端或代理认证挑战的方法、realm 和调用方填写的用户名密码；
 *             仅依赖 XinYueC 容器和内存抽象，不调用 Win32、POSIX、Qt 或其他平台 API。
 */

#ifndef XHTTPAUTHENTICATOR_H
#define XHTTPAUTHENTICATOR_H
#include "XHttp_config.h"

#ifdef __cplusplus
extern "C" {
#endif
#if XPROTOCOL_ON
#if XHTTP_ON

#include "XByteArray.h"
#include "XClass.h"
#include <stdbool.h>

XCLASS_DEFINE_BEGING(XHttpAuthenticator)
XCLASS_DEFINE_EXTEND_END(XHttpAuthenticator, XClass)

/**
 * - @brief HTTP 认证方案。
 * - @details 枚举值对齐 QAuthenticator 的内部认证方法；当前 HTTP 管理器可重发 Basic
 *          和 Digest 挑战，并可完成可移植的 NTLMv2 两阶段挑战；Negotiate 需要平台相关
 *          的 SSPI/GSSAPI 协商能力，当前无此抽象时由调用方通过 Authorization 头处理。
 */
typedef enum XHttpAuthenticator_Method {
    XHttpAuthenticator_None = 0, /**< 尚未解析到支持的认证挑战。 */
    XHttpAuthenticator_Basic,    /**< RFC 7617 Basic 认证。 */
    XHttpAuthenticator_Digest,   /**< RFC 7616 Digest 认证。 */
    XHttpAuthenticator_Ntlm,     /**< 可移植 NTLMv2 协商认证。 */
    XHttpAuthenticator_Negotiate /**< Negotiate/Kerberos 认证；不由 HTTP 管理器自动重发。 */
} XHttpAuthenticator_Method;

/**
 * - @brief HTTP 认证挑战与凭据。
 * - @details 所有字节数组由认证器拥有；认证信号中传入的是借用指针，槽函数只能在同步
 *          调用期间填写用户名和密码，不能保存指针到异步任务中。
 */
typedef struct XHttpAuthenticator {
    XClass m_class;                         /**< 第一个成员，由 XClass 管理，禁止手工修改。 */
    XByteArray* m_user;                     /**< 用户名 UTF-8 字节；由对象拥有，可为 NULL。 */
    XByteArray* m_password;                 /**< 密码 UTF-8 字节；由对象拥有，可为 NULL。 */
    XByteArray* m_realm;                    /**< 服务端声明的 realm；由对象拥有，可为 NULL。 */
    XHttpAuthenticator_Method m_method;     /**< 当前认证方法。 */
} XHttpAuthenticator;

/**
 * - @brief 初始化 HTTP 认证器虚函数表。
 * - @return 共享认证器虚函数表；初始化失败返回 NULL，不由调用者释放。
 */
XVtable* XHttpAuthenticator_class_init(void);
/**
 * - @brief 初始化空认证器。
 * - @param self 待初始化对象；不能为 NULL。
 * - @return 无；初始化后不拥有凭据，认证方法为 None。
 */
void XHttpAuthenticator_init(XHttpAuthenticator* self);
/**
 * - @brief 创建空认证器。
 * - @return 新对象，调用者必须使用 XHttpAuthenticator_delete_base 释放；内存不足返回 NULL。
 */
XHttpAuthenticator* XHttpAuthenticator_create_ex(XMemoryType memory);
/**
 * - @brief 深拷贝创建认证器。
 * - @param other 源认证器；借用，不能为 NULL。
 * - @return 新对象，调用者必须释放；参数无效或内存不足返回 NULL。
 */
XHttpAuthenticator* XHttpAuthenticator_create_copy(const XHttpAuthenticator* other);
/**
 * - @brief 移动创建认证器。
 * - @param other 源认证器；不能为 NULL，成功后恢复为空认证器。
 * - @return 新对象，调用者必须释放；内存不足返回 NULL。
 */
XHttpAuthenticator* XHttpAuthenticator_create_move(XHttpAuthenticator* other);

#define XHttpAuthenticator_deinit_base XClass_deinit_base
#define XHttpAuthenticator_delete_base XClass_delete_base
#define XHttpAuthenticator_copy_base XClass_copy_base
#define XHttpAuthenticator_move_base XClass_move_base

/**
 * - @brief 设置服务端解析出的认证挑战。
 * - @param self 认证器；不能为 NULL。
 * - @param method 认证方法；必须是 XHttpAuthenticator_Method 的有效值。
 * - @param realm realm UTF-8 字节；借用，可为 NULL 清除 realm。
 * - @return 成功返回 true；参数无效或内存不足返回 false，原状态保持不变。
 */
bool XHttpAuthenticator_setChallenge(XHttpAuthenticator* self,
                                     XHttpAuthenticator_Method method,
                                     const XByteArray* realm);
/**
 * - @brief 获取认证方法。
 * - @param self 认证器；可为 NULL。
 * - @return 当前方法；self 为 NULL 返回 XHttpAuthenticator_None。
 */
XHttpAuthenticator_Method XHttpAuthenticator_method(const XHttpAuthenticator* self);
/**
 * - @brief 获取 realm 副本。
 * - @param self 认证器；可为 NULL。
 * - @return 新字节数组，调用者必须释放；没有 realm 时返回空数组，内存不足返回 NULL。
 */
XByteArray* XHttpAuthenticator_realm(const XHttpAuthenticator* self);
/**
 * - @brief 获取 realm 借用指针。
 * - @param self 认证器；可为 NULL。
 * - @return 内部只读 realm；没有 realm 时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XHttpAuthenticator_realm_const(const XHttpAuthenticator* self);
/**
 * - @brief 设置用户名。
 * - @param self 认证器；不能为 NULL。
 * - @param user UTF-8 用户名字节；借用，NULL 清除用户名。
 * - @return 成功返回 true；内存不足或 self 为 NULL 返回 false，旧值保持不变。
 */
bool XHttpAuthenticator_setUser(XHttpAuthenticator* self, const XByteArray* user);
/**
 * - @brief 用 UTF-8 文本设置用户名。
 * - @param self 认证器；不能为 NULL。
 * - @param user UTF-8 用户名；借用，NULL 清除用户名。
 * - @return 成功返回 true；内存不足或 self 为 NULL 返回 false。
 */
bool XHttpAuthenticator_setUser_utf8(XHttpAuthenticator* self, const char* user);
/**
 * - @brief 获取用户名副本。
 * - @param self 认证器；可为 NULL。
 * - @return 新字节数组，调用者必须释放；没有用户名时返回空数组，内存不足返回 NULL。
 */
XByteArray* XHttpAuthenticator_user(const XHttpAuthenticator* self);
/**
 * - @brief 获取用户名借用指针。
 * - @param self 认证器；可为 NULL。
 * - @return 内部只读用户名；没有用户名时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XHttpAuthenticator_user_const(const XHttpAuthenticator* self);
/**
 * - @brief 设置密码。
 * - @param self 认证器；不能为 NULL。
 * - @param password UTF-8 密码字节；借用，NULL 清除密码。
 * - @return 成功返回 true；内存不足或 self 为 NULL 返回 false，旧值保持不变。
 */
bool XHttpAuthenticator_setPassword(XHttpAuthenticator* self, const XByteArray* password);
/**
 * - @brief 用 UTF-8 文本设置密码。
 * - @param self 认证器；不能为 NULL。
 * - @param password UTF-8 密码；借用，NULL 清除密码。
 * - @return 成功返回 true；内存不足或 self 为 NULL 返回 false。
 */
bool XHttpAuthenticator_setPassword_utf8(XHttpAuthenticator* self, const char* password);
/**
 * - @brief 获取密码副本。
 * - @param self 认证器；可为 NULL。
 * - @return 新字节数组，调用者必须释放；没有密码时返回空数组，内存不足返回 NULL。
 */
XByteArray* XHttpAuthenticator_password(const XHttpAuthenticator* self);
/**
 * - @brief 获取密码借用指针。
 * - @param self 认证器；可为 NULL。
 * - @return 内部只读密码；没有密码时返回 NULL，调用者不得释放或修改。
 */
const XByteArray* XHttpAuthenticator_password_const(const XHttpAuthenticator* self);
/**
 * - @brief 判断认证器是否已由认证槽填写可用凭据。
 * - @param self 认证器；可为 NULL。
 * - @return 用户名和密码均已提供且方法不是 None 时返回 true；否则返回 false。
 */
bool XHttpAuthenticator_hasCredentials(const XHttpAuthenticator* self);

#endif // XHTTP_ON
#endif /* XPROTOCOL_ON */
#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XHttpAuthenticator_create
#define XHttpAuthenticator_create(...) XHttpAuthenticator_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, ##__VA_ARGS__)

#endif /* XHTTPAUTHENTICATOR_H */
