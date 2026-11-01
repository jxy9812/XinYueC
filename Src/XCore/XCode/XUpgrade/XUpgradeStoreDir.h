/**
 * @file       XUpgradeStoreDir.h
 * @brief      升级存储目录后端：versions/&lt;comp&gt;/&lt;ver&gt;/ + 指针文件（三平台统一基线）。
 * @details    XUpgradeStore 契约的目录后端实现，纯库内实现（XSaveFile/XDir/
 *             XFile 族），无 Drive 平台代码，Win32/Linux/带 SD 的嵌入式共用
 *             （设计文档 §7）。M1 唯一编入的目录后端；A/B 后端
 *             （XUpgradeStoreAb，M3）入口见 XUpgradeStore.h 的
 *             XUpgradeStoreAb_create_ex（XUPGRADE_AB_ON 门控）。目录布局常量
 *             见 XUpgradeStore.h；指针文件统一为文本，经 XSaveFile 原子写
 *             （不用 symlink：Fatfs 没有且 Windows symlink 要特权）。
 */

#ifndef XUPGRADESTOREDIR_H
#define XUPGRADESTOREDIR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "XUpgrade_config.h"

#if XUPGRADE_ON

#include <stdint.h>

#include "XMemory.h"
#include "XUpgradeStore.h"

/* ========================================================================== */
/*                        目录后端策略常量                                     */
/* ========================================================================== */

/** @brief GC 保留的历史版本数（含激活版本）：自检通过后引擎延迟清理更旧目录（§2）。 */
#ifndef XUPGRADE_STORE_DIR_KEEP_VERSIONS
#define XUPGRADE_STORE_DIR_KEEP_VERSIONS 2u
#endif
/** @brief 版本目录内关键文件抽查清单文件名：staging 时写入，自检按其逐条复核哈希（§8.3）。 */
#ifndef XUPGRADE_STORE_DIR_VERIFY_FILE
#define XUPGRADE_STORE_DIR_VERIFY_FILE ".verify"
#endif
/** @brief 引擎工作区内 staging 临时目录名：<root>/update/staging/<comp>/<ver>/。
 *  @note 引擎按本文件头注明的"确定性路径直接向临时目录写条目文件"（契约头
 *        公开布局常量，非 API 签名）；stageEnd 把该目录整体改名落位。 */
#ifndef XUPGRADE_STOREDIR_DIR_STAGING
#define XUPGRADE_STOREDIR_DIR_STAGING "staging"
#endif

/**
 * @brief 创建目录后端存储对象。
 * @param memory 内存池类型；决定对象分配与释放所用的分配器。
 * @param installRootUtf8 安装根目录完整路径；UTF-8 借用，不能为 NULL，函数
 *                        内部复制。目录不存在时在 stageBegin/activate 等
 *                        首次使用处按需创建。
 * @return 新后端对象（以基类 XUpgradeStore 指针返回），调用者必须使用
 *         XClassDelete 释放；分配失败返回 NULL。
 * @note 实现要点（实现方遵守）：派生结构体与
 *       static XVtable* XUpgradeStoreDir_class_init(void) 定义在
 *       XUpgradeStoreDir.c（模式同 XMySqlDriver.c:54-69）；class_init 按标准
 *       五步：XVTABLE_INIT_DEFAULT(XUpgradeStoreDir) →
 *       XVTABLE_INHERIT_XCLASS(XUpgradeStore) → 重载 EXClass_Deinit →
 *       逐槽重载 StageBegin/StageWrite/StageEnd/Activate/CurrentVersion/
 *       Rollback/ListVersions/ConfirmBoot（幂等 Ok）→ return XVTABLE_DEFAULT。
 *       禁用 stdio/remove 族，一律 XFile/XDir/XSaveFile。
 */
XUpgradeStore* XUpgradeStoreDir_create_ex(XMemoryType memory, const char* installRootUtf8);

#endif /* XUPGRADE_ON */

#ifdef __cplusplus
}
#endif


/* XClass create API default-memory wrappers. */
#undef XUpgradeStoreDir_create
#define XUpgradeStoreDir_create(...) XUpgradeStoreDir_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, __VA_ARGS__)

#endif /* XUPGRADESTOREDIR_H */
