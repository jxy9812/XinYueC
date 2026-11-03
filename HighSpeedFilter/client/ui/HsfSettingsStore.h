/**
 * @file       HsfSettingsStore.h
 * @brief      高速过滤器客户端界面设置持久化门面（XSettings 单例封装）。
 * @details    底层是框架的 XSettings（对标 Qt 6.8 QSettings，INI 文件后端）。
 *             本门面只做三件事：
 *               1) 持**唯一**一个 XSettings 实例（XSettings 无内部锁，多实例
 *                  各持独立内存态、写入互不可见 → 必须单例）；
 *               2) 定配置文件路径（程序目录 cfg/ 子目录 → 程序目录 → 用户
 *                  主目录，逐级回落）；
 *               3) 提供按类型的读写，并处理 INI 后端的**类型退化**坑。
 *
 * @note 布尔值一律按 int 0/1 落盘，**不要**用 XVariant_create_bool：
 *       INI 是无类型文本格式，bool/int 变体落盘后读回都是 String
 *       （见 Test/XCodeTest/XSettingsTest.c 的落盘/重开用例），而
 *       XVariant_toBool 对 String **无回退**（XVariant.c:326-331 直接落
 *       default 返回 false）→ 按 Qt 的 value(k,false).toBool() 写法读回来
 *       恒为 false。改用 int 后 XVariant_toInt 对 String 有解析回退
 *       （XVariant.c:366-367），读回正确。
 */
#ifndef HSFSETTINGSSTORE_H
#define HSFSETTINGSSTORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 打开配置文件（幂等；重复调用只返回首次结果）。
 * @return 成功返回 true；失败返回 false——此时全部读写退化为「返回默认值
 *         / 丢弃写入」，程序按出厂默认运行（不崩、不静默改行为）。
 * @note  必须在 XGuiApplication 创建之后调用（路径推导用
 *        XCoreApplication_applicationDirPath）。
 */
bool HsfSettingsStore_open(void);

/**
 * @brief 关闭配置文件（落盘并释放实例；幂等）。
 */
void HsfSettingsStore_close(void);

/**
 * @brief 配置文件是否已打开（诊断用）。
 */
bool HsfSettingsStore_isOpen(void);

/**
 * @brief 当前生效的配置文件路径（借用指针；未打开时为空串）。
 */
const char* HsfSettingsStore_path(void);

/**
 * @brief 读取整型键（键不存在返回 def）。
 * @param key 键名（可含 '/'，框架自动落成 INI 分节）。
 */
int HsfSettingsStore_getInt(const char* key, int def);

/**
 * @brief 读取 64 位整型键（键不存在返回 def）。
 */
int64_t HsfSettingsStore_getInt64(const char* key, int64_t def);

/**
 * @brief 读取布尔键（按 int 0/1 落盘，见文件头说明）。
 */
bool HsfSettingsStore_getBool(const char* key, bool def);

/**
 * @brief 写入整型键（不落盘；由 sync 或 close 落盘）。
 */
void HsfSettingsStore_setInt(const char* key, int value);

/**
 * @brief 写入 64 位整型键（不落盘）。
 */
void HsfSettingsStore_setInt64(const char* key, int64_t value);

/**
 * @brief 写入布尔键（按 int 0/1 落盘，不落盘）。
 */
void HsfSettingsStore_setBool(const char* key, bool value);

/**
 * @brief 落盘（原子写：临时文件 + 重命名；无修改不写）。
 * @note  XSettings 无「事件循环空闲自动落盘」钩子（与 Qt 的差别，见
 *        交接说明），故由调用方在改动点显式落盘。
 */
void HsfSettingsStore_sync(void);

/**
 * @brief 开关持久化（--no-settings：跳过加载与保存，供 CI 截图取确定性基线）。
 */
void HsfSettingsStore_setEnabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif /* HSFSETTINGSSTORE_H */
