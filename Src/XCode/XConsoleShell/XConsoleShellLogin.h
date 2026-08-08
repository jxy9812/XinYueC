/**
 * @file XConsoleShellLogin.h
 * @brief XConsoleShell 的登录、用户管理和 Linux 风格账户权限 API。
 * @details
 * 账户数据只通过 XFileSystem 和 XJsonDocument 读写产品指定的 JSON 文件，
 * 不访问宿主机 /etc/passwd、Windows 注册表或其他平台账户数据库。用户记录
 * 保存用户名、UID、主 GID、附加组、Shell 权限位和加盐密码摘要；登录成功后
 * 写入当前会话，提示符可据此显示用户名。登录和改密密码通过下一行交互输入，
 * 不保存明文、不写入 Shell 历史；输入回显由 XConsoleShellIo 可选回调控制。
 */

#ifndef XCONSOLE_SHELL_LOGIN_H
#define XCONSOLE_SHELL_LOGIN_H

#include "XConsoleShellConfig.h"
#include "XConsoleShellCommand.h"
#include <stdbool.h>
#include <stdint.h>

#if XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && \
    XCONSOLE_SHELL_LOGIN_ON

#ifdef __cplusplus
extern "C" {
#endif

struct XConsoleShell;

/** @brief 登录模块等待的交互输入类型。 */
typedef enum XConsoleShellLoginInputMode {
    XConsoleShellLoginInput_None = 0, /**< 当前没有等待中的账户输入。 */
    XConsoleShellLoginInput_LoginUser = 1, /**< 等待 login 的用户名。 */
    XConsoleShellLoginInput_LoginPassword = 2, /**< 等待 login 的密码。 */
    XConsoleShellLoginInput_PasswdOldPassword = 3, /**< 等待 passwd 的当前密码。 */
    XConsoleShellLoginInput_PasswdNewPassword = 4, /**< 等待 passwd 的新密码。 */
    XConsoleShellLoginInput_PasswdConfirm = 5 /**< 等待 passwd 的确认密码。 */
} XConsoleShellLoginInputMode;

/**
 * @brief 获取当前会话的登录用户名。
 * @param shell Shell 对象；可为 NULL。
 * @return 已登录时返回会话内借用的 UTF-8 用户名；未登录或参数无效返回 NULL。
 * @note 返回指针由 Shell 所有，在下一次登录、注销或 Shell 销毁后失效。
 */
const char* XConsoleShellLogin_userName(const struct XConsoleShell* shell);

/**
 * @brief 获取当前 Shell 使用的用户 JSON 配置路径。
 * @param shell Shell 对象；可为 NULL。
 * @return 返回 Shell 内部借用的 UTF-8 路径；参数无效返回 NULL。
 */
const char* XConsoleShellLogin_databasePath(const struct XConsoleShell* shell);

/**
 * @brief 设置当前 Shell 的用户 JSON 配置路径。
 * @param shell Shell 对象；不能为空。
 * @param path UTF-8 配置路径；不能为空且长度必须小于容量。
 * @return 设置成功返回 true；参数非法或路径过长返回 false。
 * @note 函数只保存路径文本，不会立即读取或创建文件；已登录会话不会被强制注销。
 */
bool XConsoleShellLogin_setDatabasePath(struct XConsoleShell* shell,
                                        const char* path);

/**
 * @brief 校验用户名和密码并应用到指定会话。
 * @param shell Shell 对象；不能为空。
 * @param session 目标会话；不能为空。
 * @param user UTF-8 用户名；不能为空。
 * @param password UTF-8 密码；不能为空。
 * @return 用户名和密码匹配且账户未锁定、密码已设置时返回 true；
 *         参数非法、账户不存在、锁定或密码错误返回 false。
 * @note 供 SSH 等外部认证通道复用账户库，不写入 Shell 历史，不回显密码。
 */
bool XConsoleShellLogin_authenticateSession(struct XConsoleShell* shell,
                                            struct XConsoleShellSession* session,
                                            const char* user, const char* password);

/** @brief 登录命令静态描述。 */
extern const XConsoleCommand XConsoleShellLogin_command;
/** @brief 注销命令静态描述。 */
extern const XConsoleCommand XConsoleShellLogout_command;
/** @brief whoami 命令静态描述。 */
extern const XConsoleCommand XConsoleShellWhoami_command;
/** @brief id 命令静态描述。 */
extern const XConsoleCommand XConsoleShellId_command;
/** @brief groups 命令静态描述。 */
extern const XConsoleCommand XConsoleShellGroups_command;
/** @brief useradd 命令静态描述。 */
extern const XConsoleCommand XConsoleShellUserAdd_command;
/** @brief userdel 命令静态描述。 */
extern const XConsoleCommand XConsoleShellUserDel_command;
/** @brief usermod 命令静态描述。 */
extern const XConsoleCommand XConsoleShellUserMod_command;
/** @brief passwd/password 命令静态描述。 */
extern const XConsoleCommand XConsoleShellPasswd_command;
/** @brief users 命令静态描述。 */
extern const XConsoleCommand XConsoleShellUsers_command;
/** @brief userlist 命令静态描述，用于列出本地 JSON 账户库。 */
extern const XConsoleCommand XConsoleShellUserList_command;

#ifdef __cplusplus
}
#endif

#endif /* XCONSOLE_SHELL_ON && XCONSOLE_SHELL_COMMAND_ON && XCONSOLE_SHELL_IO_ON && XCONSOLE_SHELL_LOGIN_ON */
#endif /* XCONSOLE_SHELL_LOGIN_H */
