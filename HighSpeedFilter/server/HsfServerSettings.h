
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

/* ==================== 字符串键（网络地址/名称等） ==================== */

/**
 * @brief 读取字符串键到调用方缓冲。
 * @param key 键名（可含 '/'）。
 * @param def 缺省值（键不存在时拷贝它；可为 NULL=空串）。
 * @param buf 输出缓冲（不能为 NULL）。
 * @param cap 缓冲容量（含结尾 NUL；必须 > 0）。
 * @return 无；永远写出一个 NUL 结尾的串。
 * @note  [类型退化] INI 后端读回的变体不一定真是 String——现场把 host
 *        写成数字、或把数字写成字符串都会发生。非 String 类型时按数值
 *        转文本，绝不返回空串（否则"配置明明写了却没生效"）。
 */
void HsfServerSettings_getString(const char* key, const char* def,
                                 char* buf, int cap);

/**
 * @brief 写字符串键（内存态 + 立即落盘）。
 * @param key 键名；value 为 UTF-8，NULL 视为空串。
 * @note  落盘口径同 setInt（写完立即 sync，避免进程退出丢配置）。
 */
void HsfServerSettings_setString(const char* key, const char* value);

/**
 * @brief 缺省值播种：键不存在时写入缺省值（存在则不动）。
 * @return 本次写入了缺省值（=键此前不存在）返回 true。
 * @note  用于"首次运行生成完整配置骨架"，让现场打开文件就能改全部参数。
 */
bool HsfServerSettings_seedInt(const char* key, int def);
bool HsfServerSettings_seedString(const char* key, const char* def);

/* ==================== 数字量 / 电磁阀：参数在头文件 ==================== */

/*
 * [2026-11-05 决策] 数字量模块与电磁阀的**部署参数**
 * （IP/端口/Unit ID/点数/起始地址/轮询与超时/阀点位）
 * 不再走 INI，统一写在服务端配置头文件 HsfServerConfig.h
 * 的宏里：它们是现场接线/部署的一部分，与设备一起版本化。
 * 运行期真正需要现场可调的只有监听端口（server/port）。
 *
 * 本头不再提供 digital/*、valve/* 的读写门面（旧接口已删），
 * 上层直接引 HsfServerConfig.h。
 */

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
