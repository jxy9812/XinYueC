
/**
 * @file       HsfServerSettings.h
 * @brief      服务端设置持久化门面（XSettings 单例封装）。
 * @details    对齐客户端 HsfSettingsStore 的数据目录口径：配置落在
 *             「程序运行根目录」——优先 <程序目录>/cfg/hsf_server.ini，
 *             建不了子目录时退 <程序目录>/hsf_server.ini。布尔一律按
 *             int 0/1 落盘（INI 后端类型退化坑，见客户端门面注释）。
 *             同目录另存 XLink 账户库 JSON（hsf_users.json，由
 *             HsfServerApp 经 XLinkUserStore 落盘）。
 */
#ifndef HSFSERVERSETTINGS_H
#define HSFSERVERSETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XhsfLinkProto.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 打开配置文件（幂等；重复调用只返回首次结果）。
 * @return 成功返回 true；失败返回 false——读写退化为默认值/丢弃。
 * @note  必须在 XCoreApplication 创建之后调用（路径推导用
 *        XCoreApplication_applicationDirPath）。
 */
bool HsfServerSettings_open(void);

/**
 * @brief 关闭配置文件（落盘并释放实例；幂等）。
 */
void HsfServerSettings_close(void);

/**
 * @brief 配置文件是否已打开（诊断用）。
 */
bool HsfServerSettings_isOpen(void);

/**
 * @brief 当前生效的配置文件路径（借用指针；未打开时为空串）。
 */
const char* HsfServerSettings_path(void);

/**
 * @brief 数据目录（配置文件所在目录；账户库 JSON 也落这里）。
 * @return 借用指针；未打开时返回程序目录（仍可落账户库）。
 */
const char* HsfServerSettings_dataDir(void);

/**
 * @brief 判断键是否存在（用于「缺省值首次落盘」这类播种场景）。
 * @param key 键名（可含 '/'）。
 * @return 存在返回 true；配置文件未打开或参数非法返回 false。
 */
bool HsfServerSettings_contains(const char* key);

/**
 * @brief 读取整型键（键不存在返回 def）。
 * @param key 键名（可含 '/'，框架自动落成 INI 分节）。
 */
int HsfServerSettings_getInt(const char* key, int def);

/**
 * @brief 写整型键（内存态；落盘随 close/sync）。
 */
void HsfServerSettings_setInt(const char* key, int value);

/* ==================== 监听端口（server/port） ==================== */

/** @brief 监听端口键名（INI 落成 [server] port=）。 */
#define HSF_SRV_KEY_PORT            "server/port"

/**
 * @brief 取生效监听端口：配置文件 server/port → 缺省 HSF_XLINK_DEFAULT_PORT。
 * @return 端口号（1~65535）；配置文件不可用或值非法时返回缺省端口并打日志。
 * @note  必须在 HsfServerSettings_open 之后调用。
 *        配置文件**不存在该键**时（首次运行）自动把缺省端口落盘，现场之后
 *        只需改这一个键。
 */
uint16_t HsfServerSettings_resolvePort(void);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERSETTINGS_H */
