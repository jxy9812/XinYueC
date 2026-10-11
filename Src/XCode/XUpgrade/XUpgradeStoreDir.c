/**
 * @file       XUpgradeStoreDir.c
 * @brief      升级存储目录后端实现：versions/<comp>/<ver>/ 落盘 + 指针文件原子替换。
 * @details    XUpgradeStore 契约（Src/XPlatform/XUpgrade/XUpgradeStore.h）的
 *             目录后端实现（设计文档 XUpgrade.md §2/§7），纯库内实现，无
 *             Drive 平台代码，Win32/Linux/带 SD 的嵌入式共用。文件与目录
 *             操作一律走 XFile/XDir/XFileInfo/XSaveFile 族（禁 stdio/remove）：
 *             - staging：stageBegin 在引擎工作区建临时目录
 *               <root>/update/staging/<comp>/<ver>/（update/ 任意时刻可整目录
 *               删除重来，不损坏已安装版本）；stageWrite 把数据流经 XSaveFile
 *               写入目录内 payload.staged（写临时文件，未定稿不对外可见）；
 *               stageEnd 先 commit 定稿 payload，再把临时目录原子改名落位为
 *               版本目录 versions/<comp>/<ver>/。逐条目解包落盘与哈希校验由
 *               引擎驱动（引擎可按本文件公开的确定性路径直接向临时目录写
 *               条目文件，stageWrite 是单流后备通道），存储层只管目录与文件
 *               写入原语。
 *             - activate/rollback：经 XSaveFile 原子写指针文件
 *               <root>/pointers/<comp>（单行文本，无扩展名，不用 symlink）。
 *               commit 走 XDeviceFile_renameReplace 替换语义（目标已存在即
 *               原子覆盖），旧版"先删后改"的断电缺失窗口已随该根修移除。
 *             - currentVersion：读指针 + 校验版本目录存在；读失败/内容非法
 *               视作未激活（调用方据此走启动自检回滚路径）。
 *             - listVersions：枚举 versions/<comp>/ 子目录，顺序未定义。
 */

#include "XUpgradeStoreDir.h"

#include "xupgrade_shared.h" /* 模块内部共享小工具（xups_*） */
#include <string.h>

#include "XMemory.h"
#include "XString.h"
#include "XStringList.h"
#include "XByteArray.h"
#include "XIODevice.h"
#include "XFile.h"
#include "XFileInfo.h"
#include "XDir.h"
#include "XSaveFile.h"
#include "XStorageInfo.h"

/* ========================================================================== */
/*                        目录后端内部布局常量                                 */
/* ========================================================================== */

/* staging 临时目录名已上移契约头（XUPGRADE_STOREDIR_DIR_STAGING，
 * XUpgradeStoreDir.h）：引擎按确定性路径直接向临时目录写条目文件。 */

/** @brief staging 目录内数据流文件名（stageWrite 写入、stageEnd 定稿）。 */
#define XUPGRADE_STOREDIR_STAGE_PAYLOAD "payload.staged"

/* ========================================================================== */
/*                        派生结构体与虚函数表枚举                              */
/* ========================================================================== */

/**
 * @brief 目录后端派生结构体（实现私有）。
 * @details 首成员为基类 XUpgradeStore；staging 上下文由本结构维护，一个实例
 *          同一时间只允许一路 staging 流（契约约束，见 XUpgradeStore.h）。
 */
typedef struct XUpgradeStoreDir {
    XUpgradeStore m_parent;        /**< 基类，第一个成员，由 XObject 管理；禁止手工修改。 */
    XString* m_installRoot;        /**< 安装根目录完整路径（UTF-8 内部存储，create 时复制）。 */
    bool m_stageActive;            /**< 是否处于 staging 流中（stageBegin→stageEnd 之间）。 */
    XString* m_stageComponent;     /**< 当前 staging 组件 id 副本；不在流中时为 NULL。 */
    XString* m_stageVersion;       /**< 当前 staging 版本串副本；不在流中时为 NULL。 */
    XSaveFile* m_stageFile;        /**< 当前 staging 数据流文件（payload.staged）；未写时为 NULL。 */
} XUpgradeStoreDir;

XCLASS_DEFINE_BEGING(XUpgradeStoreDir)
XCLASS_DEFINE_EXTEND_END(XUpgradeStoreDir, XUpgradeStore)

static XVtable* XUpgradeStoreDir_class_init(void);

/* ========================================================================== */
/*                        内部静态函数声明                                     */
/* ========================================================================== */

static void VXUpgradeStoreDir_deinit(XUpgradeStoreDir* store);
static XUpgradeError VXUpgradeStoreDir_stageBegin(XUpgradeStore* self, const char* componentId,
                                                  const char* version, uint64_t expectedSize,
                                                  const char** errText);
static XUpgradeError VXUpgradeStoreDir_stageWrite(XUpgradeStore* self, const void* data, uint32_t len);
static XUpgradeError VXUpgradeStoreDir_stageEnd(XUpgradeStore* self, const char** errText);
static XUpgradeError VXUpgradeStoreDir_activate(XUpgradeStore* self, const char* componentId,
                                                const char* version, const char** errText);
static bool VXUpgradeStoreDir_currentVersion(XUpgradeStore* self, const char* componentId,
                                             char* out, uint32_t cap);
static XUpgradeError VXUpgradeStoreDir_rollback(XUpgradeStore* self, const char* componentId,
                                                const char* version, const char** errText);
static XUpgradeError VXUpgradeStoreDir_listVersions(XUpgradeStore* self, const char* componentId,
                                                    XUpgradeStoreEmitVersion emit, void* userData);

/* ========================================================================== */
/*                        内部工具函数                                         */
/* ========================================================================== */

/* setErr/cstrLen/validName/buildPath/extractVersion 已收敛至
 * xupgrade_shared.h（xups_* 前缀，本 TU 直接调用）。 */

/**
 * @brief 丢弃当前 staging 上下文并清理未定稿的临时数据流文件。
 * @param store 目录后端对象；可为 NULL。
 * @note 临时 staging 目录本身保留在 update/ 工作区内（可整目录删除重来），
 *       下一次 stageBegin 会先递归删除再重建；此处只负责关闭数据流文件
 *       （未 commit 时其析构会删除 ".XXXXXX" 临时文件）与释放串副本。
 */
static void xupgrade_storedir_abortStage(XUpgradeStoreDir* store)
{
    if (!store) return;
    if (store->m_stageFile) {
        XSaveFile_cancelWriting(store->m_stageFile);
        XClassDelete(store->m_stageFile);
        store->m_stageFile = NULL;
    }
    if (store->m_stageComponent) {
        XClassDelete(store->m_stageComponent);
        store->m_stageComponent = NULL;
    }
    if (store->m_stageVersion) {
        XClassDelete(store->m_stageVersion);
        store->m_stageVersion = NULL;
    }
    store->m_stageActive = false;
}

/**
 * @brief 计算组件的最终版本目录路径 <root>/versions/<comp>/<ver>。
 * @return 新路径字符串（堆上 XString，调用方 XClassDelete 释放）；分配失败返回 NULL。
 */
static XString* xupgrade_storedir_versionDirPath(const XUpgradeStoreDir* store,
                                                 const char* componentId, const char* version)
{
    return xups_build_path(store->m_installRoot, XUPGRADE_DIR_VERSIONS,
                                       componentId, version, NULL);
}

/**
 * @brief 计算组件的临时 staging 目录路径 <root>/update/staging/<comp>/<ver>。
 * @return 新路径字符串（堆上 XString，调用方 XClassDelete 释放）；分配失败返回 NULL。
 */
static XString* xupgrade_storedir_stageDirPath(const XUpgradeStoreDir* store,
                                               const char* componentId, const char* version)
{
    return xups_build_path(store->m_installRoot, XUPGRADE_DIR_UPDATE,
                                       XUPGRADE_STOREDIR_DIR_STAGING, componentId, version);
}

/**
 * @brief 计算组件指针文件路径 <root>/pointers/<comp>（单行文本，无扩展名）。
 * @return 新路径字符串（堆上 XString，调用方 XClassDelete 释放）；分配失败返回 NULL。
 */
static XString* xupgrade_storedir_pointerPath(const XUpgradeStoreDir* store,
                                              const char* componentId)
{
    return xups_build_path(store->m_installRoot, XUPGRADE_DIR_POINTERS,
                                       componentId, NULL, NULL);
}

/**
 * @brief 校验指定组件的版本目录是否存在。
 * @return 目录存在返回 true；不存在或路径拼装失败返回 false。
 */
static bool xupgrade_storedir_versionDirExists(const XUpgradeStoreDir* store,
                                               const char* componentId, const char* version)
{
    XString* path = xupgrade_storedir_versionDirPath(store, componentId, version);
    bool exists = false;
    if (!path) return false;
    exists = XFileInfo_exists_static(path);
    XClassDelete(path);
    return exists;
}

/**
 * @brief 原子写组件指针文件（activate/rollback 共用）。
 * @param store 目录后端对象；不能为 NULL。
 * @param componentId 组件 id；已由调用方校验白名单。
 * @param version 目标版本串；已由调用方校验白名单。
 * @param errText 输出错误说明；语义同契约（失败时静态借用文本，成功时 NULL）。
 * @return 成功返回 true；指针目录创建或指针写入失败返回 false。
 * @note 指针文件经 XSaveFile（临时文件+commit 原子替换）落盘，内容为
 *       版本串+换行；commit 走 XDeviceFile_renameReplace 替换语义，任意
 *       时刻断电要么旧指针要么新指针（不变式 2）。
 */
static bool xupgrade_storedir_writePointer(XUpgradeStoreDir* store, const char* componentId,
                                           const char* version, const char** errText)
{
    XString* pointerPath = NULL;
    XString* rootRel = NULL;
    XDir* rootDir = NULL;
    XSaveFile* save = NULL;
    size_t versionLen = xups_cstr_len(version);
    bool result = false;

    /* 1. 按需创建 pointers/ 目录（经安装根相对路径，避免绝对/相对歧义）。 */
    rootRel = xups_build_path(NULL, XUPGRADE_DIR_POINTERS, NULL, NULL, NULL);
    if (!rootRel) {
        xups_set_err(errText, "out of memory");
        return false;
    }
    rootDir = XDir_create_2(store->m_installRoot);
    if (!rootDir || !XDir_mkpath(rootDir, rootRel)) {
        xups_set_err(errText, "create pointers directory failed");
        goto cleanup;
    }

    /* 2. XSaveFile 原子写：写临时文件，commit 经 XDeviceFile_renameReplace
     *    原子替换为指针文件（目标已存在即覆盖，不变式 2 无断电缺失窗口）。 */
    pointerPath = xupgrade_storedir_pointerPath(store, componentId);
    if (!pointerPath) {
        xups_set_err(errText, "out of memory");
        goto cleanup;
    }

    save = XSaveFile_create_2(pointerPath);
    if (!save) {
        xups_set_err(errText, "out of memory");
        goto cleanup;
    }
    if (!XIODevice_open_base((XIODevice*)save, XIODevice_WriteOnly)) {
        xups_set_err(errText, "open pointer file failed");
        goto cleanup;
    }
    if (XIODevice_write_1((XIODevice*)save, version, (int64_t)versionLen) != (int64_t)versionLen
        || XIODevice_write_1((XIODevice*)save, "\n", 1) != 1) {
        xups_set_err(errText, "write pointer content failed");
        goto cleanup;
    }
    if (!XSaveFile_commit(save)) {
        xups_set_err(errText, "commit pointer file failed");
        goto cleanup;
    }
    result = true;
    xups_set_err(errText, NULL);

cleanup:
    if (save) XClassDelete(save);
    if (pointerPath) XClassDelete(pointerPath);
    if (rootDir) XClassDelete(rootDir);
    XClassDelete(rootRel);
    return result;
}

/**
 * @brief activate/rollback 共用实现：校验版本目录存在后原子写指针。
 * @return 成功返回 XUpgradeError_Ok；参数非法返回 InvalidArgument；版本目录
 *         缺失或指针写入失败返回 Io。
 */
static XUpgradeError xupgrade_storedir_activateImpl(XUpgradeStoreDir* store,
                                                    const char* componentId,
                                                    const char* version, const char** errText)
{
    xups_set_err(errText, NULL);
    if (!store || !store->m_installRoot) {
        xups_set_err(errText, "store not initialized");
        return XUpgradeError_InvalidArgument;
    }
    if (!xups_valid_name(componentId, XUPGRADE_COMPONENT_ID_MAX)) {
        xups_set_err(errText, "invalid component id");
        return XUpgradeError_InvalidArgument;
    }
    if (!xups_valid_name(version, XUPGRADE_VERSION_MAX)) {
        xups_set_err(errText, "invalid version");
        return XUpgradeError_InvalidArgument;
    }
    /* 回滚/激活都要求目标版本目录仍完好（旧版本目录从不删除，天然免费）。 */
    if (!xupgrade_storedir_versionDirExists(store, componentId, version)) {
        xups_set_err(errText, "target version directory not found");
        return XUpgradeError_Io;
    }
    if (!xupgrade_storedir_writePointer(store, componentId, version, errText)) {
        return XUpgradeError_Io;
    }
    return XUpgradeError_Ok;
}

/* ========================================================================== */
/*                        虚函数实现（static，仅供 class_init 注册）            */
/* ========================================================================== */

static void VXUpgradeStoreDir_deinit(XUpgradeStoreDir* store)
{
    if (!store) return;
    xupgrade_storedir_abortStage(store);
    if (store->m_installRoot) {
        XClassDelete(store->m_installRoot);
        store->m_installRoot = NULL;
    }
    XClass_Deinit_Parent(XUpgradeStore, (XUpgradeStore*)store);
}

static XUpgradeError VXUpgradeStoreDir_stageBegin(XUpgradeStore* self, const char* componentId,
                                                  const char* version, uint64_t expectedSize,
                                                  const char** errText)
{
    XUpgradeStoreDir* store = (XUpgradeStoreDir*)self;
    XString* finalDirPath = NULL;
    XString* stageDirPath = NULL;
    XString* stagingRel = NULL;
    XDir* rootDir = NULL;
    XUpgradeError result = XUpgradeError_Io;

    xups_set_err(errText, NULL);
    if (!store || !store->m_installRoot) {
        xups_set_err(errText, "store not initialized");
        return XUpgradeError_InvalidArgument;
    }
    if (!xups_valid_name(componentId, XUPGRADE_COMPONENT_ID_MAX)) {
        xups_set_err(errText, "invalid component id");
        return XUpgradeError_InvalidArgument;
    }
    if (!xups_valid_name(version, XUPGRADE_VERSION_MAX)) {
        xups_set_err(errText, "invalid version");
        return XUpgradeError_InvalidArgument;
    }
    if (store->m_stageActive) {
        xups_set_err(errText, "another staging flow is active");
        return XUpgradeError_Busy;
    }

    /* 1. 最终版本目录已存在=上次 staging 半成品未清理（或同版本重装），
     *    按契约拒绝，由引擎决定清理时机（旧版本目录不归本后端删除）。 */
    finalDirPath = xupgrade_storedir_versionDirPath(store, componentId, version);
    if (!finalDirPath) {
        xups_set_err(errText, "out of memory");
        return XUpgradeError_Io;
    }
    if (XFileInfo_exists_static(finalDirPath)) {
        xups_set_err(errText, "version directory already exists (stale staging not cleaned)");
        goto cleanup;
    }

    /* 2. 磁盘余量预检（expectedSize=manifest 组件 m_size；0 表示未知）。
     *    XStorageInfo 构造为惰性（不自动取数），必须显式 refresh 后
     *    isValid/bytesAvailable 才有值——2026-10-08 评审 P1：原代码漏调
     *    refresh，预检恒不生效（"磁盘不足"死分支）。 */
    if (expectedSize > 0) {
        XStorageInfo* storage = XStorageInfo_create_3(XString_toUtf8(store->m_installRoot));
        if (storage) {
            XStorageInfo_refresh(storage);
            if (XStorageInfo_isValid(storage)
                && (uint64_t)XStorageInfo_bytesAvailable(storage) < expectedSize) {
                xups_set_err(errText, "insufficient disk space");
                XClassDelete(storage);
                goto cleanup;
            }
            XClassDelete(storage);
        }
    }

    /* 3. 清理上次崩溃遗留的临时 staging 目录并重建（update/ 任意可删）。 */
    stageDirPath = xupgrade_storedir_stageDirPath(store, componentId, version);
    stagingRel = xups_build_path(NULL, XUPGRADE_DIR_UPDATE,
                                             XUPGRADE_STOREDIR_DIR_STAGING, componentId, version);
    if (!stageDirPath || !stagingRel) {
        xups_set_err(errText, "out of memory");
        goto cleanup;
    }
    if (XFileInfo_exists_static(stageDirPath)) {
        XDir* stale = XDir_create_2(stageDirPath);
        if (stale) {
            XDir_removeRecursively(stale);
            XClassDelete(stale);
        }
    }
    rootDir = XDir_create_2(store->m_installRoot);
    if (!rootDir || !XDir_mkpath(rootDir, stagingRel)) {
        xups_set_err(errText, "create staging directory failed");
        goto cleanup;
    }

    /* 4. 记录 staging 上下文；写入槽=<root>/update/staging/<comp>/<ver>/。 */
    store->m_stageComponent = XString_create_utf8(componentId);
    store->m_stageVersion = XString_create_utf8(version);
    if (!store->m_stageComponent || !store->m_stageVersion) {
        xups_set_err(errText, "out of memory");
        xupgrade_storedir_abortStage(store);
        goto cleanup;
    }
    store->m_stageActive = true;
    result = XUpgradeError_Ok;

cleanup:
    if (rootDir) XClassDelete(rootDir);
    if (stagingRel) XClassDelete(stagingRel);
    if (stageDirPath) XClassDelete(stageDirPath);
    if (finalDirPath) XClassDelete(finalDirPath);
    return result;
}

static XUpgradeError VXUpgradeStoreDir_stageWrite(XUpgradeStore* self, const void* data, uint32_t len)
{
    XUpgradeStoreDir* store = (XUpgradeStoreDir*)self;
    XString* payloadPath = NULL;
    XString* stageDirPath = NULL;
    int64_t written;

    if (!store || !store->m_installRoot) return XUpgradeError_InvalidArgument;
    if (!store->m_stageActive || !store->m_stageComponent || !store->m_stageVersion) {
        return XUpgradeError_NotReady;
    }
    if (len == 0) return XUpgradeError_Ok;
    if (!data) return XUpgradeError_InvalidArgument;

    /* 数据流文件懒打开：XSaveFile 只支持 WriteOnly（临时文件机制），
     * stageEnd 才 commit 定稿，未定稿对外不可见。 */
    if (!store->m_stageFile) {
        stageDirPath = xupgrade_storedir_stageDirPath(store,
                                                      XString_toUtf8(store->m_stageComponent),
                                                      XString_toUtf8(store->m_stageVersion));
        payloadPath = stageDirPath ? xups_build_path(stageDirPath,
                                                                 XUPGRADE_STOREDIR_STAGE_PAYLOAD,
                                                                 NULL, NULL, NULL) : NULL;
        XClassDelete(stageDirPath);
        if (!payloadPath) return XUpgradeError_Io;
        store->m_stageFile = XSaveFile_create_2(payloadPath);
        if (!store->m_stageFile
            || !XIODevice_open_base((XIODevice*)store->m_stageFile, XIODevice_WriteOnly)) {
            if (store->m_stageFile) {
                XClassDelete(store->m_stageFile);
                store->m_stageFile = NULL;
            }
            XClassDelete(payloadPath);
            return XUpgradeError_Io;
        }
        XClassDelete(payloadPath);
    }

    written = XIODevice_write_1((XIODevice*)store->m_stageFile, (const char*)data, (int64_t)len);
    if (written != (int64_t)len) {
        /* 写失败（磁盘满等）：staging 流作废，需重新 stageBegin（契约）。 */
        xupgrade_storedir_abortStage(store);
        return XUpgradeError_Io;
    }
    return XUpgradeError_Ok;
}

static XUpgradeError VXUpgradeStoreDir_stageEnd(XUpgradeStore* self, const char** errText)
{
    XUpgradeStoreDir* store = (XUpgradeStoreDir*)self;
    XDir* rootDir = NULL;
    XString* sourceRel = NULL;
    XString* targetRel = NULL;
    XString* versionsCompRel = NULL;
    XUpgradeError result = XUpgradeError_Io;

    xups_set_err(errText, NULL);
    if (!store || !store->m_installRoot || !store->m_stageActive
        || !store->m_stageComponent || !store->m_stageVersion) {
        xups_set_err(errText, "no active staging flow");
        return XUpgradeError_NotReady;
    }

    /* 1. 定稿数据流文件：flush→关闭→临时文件原子改名（XSaveFile_commit）。 */
    if (store->m_stageFile) {
        if (!XSaveFile_commit(store->m_stageFile)) {
            xups_set_err(errText, "commit staging payload failed");
            XClassDelete(store->m_stageFile);
            store->m_stageFile = NULL;
            /* 临时目录保留，供引擎诊断；重新 stageBegin 会清掉重来。 */
            xupgrade_storedir_abortStage(store);
            return XUpgradeError_Io;
        }
        XClassDelete(store->m_stageFile);
        store->m_stageFile = NULL;
    }

    /* 2. versions/<comp> 链按需创建，再把临时目录原子改名为版本目录。 */
    rootDir = XDir_create_2(store->m_installRoot);
    versionsCompRel = xups_build_path(NULL, XUPGRADE_DIR_VERSIONS,
                                                  XString_toUtf8(store->m_stageComponent),
                                                  NULL, NULL);
    sourceRel = xups_build_path(NULL, XUPGRADE_DIR_UPDATE,
                                            XUPGRADE_STOREDIR_DIR_STAGING,
                                            XString_toUtf8(store->m_stageComponent),
                                            XString_toUtf8(store->m_stageVersion));
    targetRel = xups_build_path(NULL, XUPGRADE_DIR_VERSIONS,
                                            XString_toUtf8(store->m_stageComponent),
                                            XString_toUtf8(store->m_stageVersion), NULL);
    if (!rootDir || !versionsCompRel || !sourceRel || !targetRel) {
        xups_set_err(errText, "out of memory");
        goto finish_flow;
    }
    if (!XDir_mkpath(rootDir, versionsCompRel)) {
        xups_set_err(errText, "create versions directory failed");
        goto finish_flow;
    }
    /* XDir_rename 底层同 XDeviceFile_rename；目标版本目录在 stageBegin 已
     * 确认不存在，改名即原子落位（同卷内目录改名）。 */
    if (!XDir_rename(rootDir, sourceRel, targetRel)) {
        xups_set_err(errText, "publish version directory failed");
        goto finish_flow;
    }
    result = XUpgradeError_Ok;

finish_flow:
    if (targetRel) XClassDelete(targetRel);
    if (sourceRel) XClassDelete(sourceRel);
    if (versionsCompRel) XClassDelete(versionsCompRel);
    if (rootDir) XClassDelete(rootDir);
    xupgrade_storedir_abortStage(store);
    return result;
}

static XUpgradeError VXUpgradeStoreDir_activate(XUpgradeStore* self, const char* componentId,
                                                const char* version, const char** errText)
{
    return xupgrade_storedir_activateImpl((XUpgradeStoreDir*)self, componentId, version, errText);
}

static bool VXUpgradeStoreDir_currentVersion(XUpgradeStore* self, const char* componentId,
                                             char* out, uint32_t cap)
{
    XUpgradeStoreDir* store = (XUpgradeStoreDir*)self;
    XString* pointerPath = NULL;
    XFile* file = NULL;
    XByteArray* content = NULL;
    char version[XUPGRADE_VERSION_MAX];
    bool result = false;

    if (!store || !store->m_installRoot || cap == 0) return false;
    if (!xups_valid_name(componentId, XUPGRADE_COMPONENT_ID_MAX)) return false;

    /* 读指针文件：文件缺失/打不开=未激活（设计文档 §2）。 */
    pointerPath = xupgrade_storedir_pointerPath(store, componentId);
    file = pointerPath ? XFile_create_2(pointerPath) : NULL;
    if (file && XFile_open_2(file, XIODevice_ReadOnly, 0)) {
        content = XIODevice_readAll_3((XIODevice*)file);
        XIODevice_close_base((XIODevice*)file);
    }
    if (file) XClassDelete(file);
    if (pointerPath) XClassDelete(pointerPath);
    if (!content) return false;

    /* 内容非法（空白串/超长/白名单外字符）同样视作未激活。 */
    if (!xups_extract_version(XByteArray_data(content),
                                          XContainer_size_base((const XContainer*)content),
                                          version, (uint32_t)sizeof(version))) {
        XClassDelete(content);
        return false;
    }
    XClassDelete(content);

    /* 指针必须指向真实存在的版本目录，否则视作未激活（走自检回滚）。 */
    if (!xupgrade_storedir_versionDirExists(store, componentId, version)) return false;

    if ((uint32_t)xups_cstr_len(version) + 1 > cap) return false;
    {
        uint32_t i;
        for (i = 0; version[i]; ++i) out[i] = version[i];
        out[i] = '\0';
    }
    result = true;
    return result;
}

static XUpgradeError VXUpgradeStoreDir_rollback(XUpgradeStore* self, const char* componentId,
                                                const char* version, const char** errText)
{
    /* 回滚=指针写回：旧版本目录从不删除，与 activate 同一条原子写路径。 */
    return xupgrade_storedir_activateImpl((XUpgradeStoreDir*)self, componentId, version, errText);
}

static XUpgradeError VXUpgradeStoreDir_listVersions(XUpgradeStore* self, const char* componentId,
                                                    XUpgradeStoreEmitVersion emit, void* userData)
{
    XUpgradeStoreDir* store = (XUpgradeStoreDir*)self;
    XString* componentDirPath = NULL;
    XDir* componentDir = NULL;
    XStringList* names = NULL;
    XUpgradeError result = XUpgradeError_Ok;

    if (!store || !store->m_installRoot || !emit) return XUpgradeError_InvalidArgument;
    if (!xups_valid_name(componentId, XUPGRADE_COMPONENT_ID_MAX)) {
        return XUpgradeError_InvalidArgument;
    }

    componentDirPath = xups_build_path(store->m_installRoot, XUPGRADE_DIR_VERSIONS,
                                                   componentId, NULL, NULL);
    componentDir = componentDirPath ? XDir_create_2(componentDirPath) : NULL;
    if (!componentDir) {
        result = XUpgradeError_Io;
        goto cleanup;
    }
    /* 无该组件版本目录=空枚举，不算错误（组件从未安装）。 */
    if (!XDir_exists_1(componentDir)) goto cleanup;

    /* 枚举顺序未定义（契约），由引擎按 XUpgradeManifest_compareVersions 排序。 */
    names = XDir_entryList_1(componentDir, XDir_Dirs | XDir_NoDotAndDotDot, XDir_Unsorted);
    if (!names) {
        result = XUpgradeError_Io;   /* 目录存在但枚举失败（分配失败等）。 */
        goto cleanup;
    }
    {
        size_t count = XContainer_size_base((const XContainer*)names);
        size_t i;
        for (i = 0; i < count; ++i) {
            XString* name = (XString*)XVector_at_base((const XVector*)names, (int64_t)i);
            const char* utf8 = name ? XString_toUtf8(name) : NULL;
            int emitted;
            /* 跳过白名单外的杂项条目（理论上只有版本目录，防御半成品文件）。 */
            if (!utf8 || !xups_valid_name(utf8, XUPGRADE_VERSION_MAX)) continue;
            emitted = emit(userData, utf8);
            if (emitted != 0) {
                result = (XUpgradeError)emitted;
                break;
            }
        }
    }

cleanup:
    if (names) XClassDelete(names);
    if (componentDir) XClassDelete(componentDir);
    if (componentDirPath) XClassDelete(componentDirPath);
    return result;
}

/* ========================================================================== */
/*                        虚函数表初始化（标准五步）                            */
/* ========================================================================== */

/**
 * @brief 确认新版本生效（ConfirmBoot 槽，目录后端契约口径：幂等 Ok）。
 * @details 目录后端的"激活"=pointers/<comp> 指针切换即生效标记，无独立
 *          确认步（XUpgradeStore.h:159/192 契约原文"重载为幂等 Ok"）；
 *          显式重载避免基类默认槽语义漂移（A/B 后端在此槽位做 §9.4 双写）。
 */
static XUpgradeError VXUpgradeStoreDir_confirmBoot(XUpgradeStore* self,
                                                   const char* componentId,
                                                   const char* version,
                                                   const char** errText)
{
    (void)self; (void)componentId; (void)version;
    if (errText) *errText = NULL;
    return XUpgradeError_Ok;
}

static XVtable* XUpgradeStoreDir_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XUpgradeStoreDir)
    XVTABLE_INHERIT_XCLASS(XUpgradeStore);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXUpgradeStoreDir_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_StageBegin, VXUpgradeStoreDir_stageBegin);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_StageWrite, VXUpgradeStoreDir_stageWrite);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_StageEnd, VXUpgradeStoreDir_stageEnd);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_Activate, VXUpgradeStoreDir_activate);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_CurrentVersion, VXUpgradeStoreDir_currentVersion);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_Rollback, VXUpgradeStoreDir_rollback);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_ListVersions, VXUpgradeStoreDir_listVersions);
    XVTABLE_OVERLOAD_DEFAULT(EXUpgradeStore_ConfirmBoot, VXUpgradeStoreDir_confirmBoot);
    return XVTABLE_DEFAULT;
}

/* ========================================================================== */
/*                        构造函数                                             */
/* ========================================================================== */

XUpgradeStore* XUpgradeStoreDir_create_ex(XMemoryType memory, const char* installRootUtf8)
{
    XUpgradeStoreDir* store;
    if (!installRootUtf8 || !installRootUtf8[0]) return NULL;
    store = (XUpgradeStoreDir*)XMemory_calloc(1, sizeof(XUpgradeStoreDir), memory);
    if (!store) return NULL;
    XUpgradeStore_init(&store->m_parent);      /* 基类 init：XObject + XUpgradeStore 虚表 */
    XClassSetVtable(store, XUpgradeStoreDir);  /* 覆盖为派生类虚表（重载 7 个业务槽+deinit） */
    store->m_installRoot = XString_create_utf8(installRootUtf8);
    if (!store->m_installRoot) {
        XClass_Deinit_Parent(XUpgradeStore, (XUpgradeStore*)store);
        XMemory_free(store, memory);
        return NULL;
    }
    Set_Class_Memory(store, memory);
    Set_Class_IsHeap(store, true);
    return &store->m_parent;
}
