/**
 * @file       XUpgradeApplyStub.c
 * @brief      apply 桩入口：--xupgrade-apply 自重启分支（设计文档 §8.1）。
 * @details    主程序以退出码 74 退出后，launcher 用 --xupgrade-apply 重启自身，
 *             main 入口最早处把参数分流到 XUpgradeApplyStub_main（不初始化任何
 *             GUI/XCoreApplication/引擎线程）。动作顺序固定（§8.1 步骤 3）：
 *             读 state.json 确认 Ready → 逐组件 activate（原子写 pointers）→
 *             清 staged 标记 → 状态置 BootConfirming → 返回 0，由 launcher 再
 *             次启动正式版本（其 XUpgrade_selfCheck 完成确认/回滚）。
 *             幂等：state 非 Ready/Applying 直接返回 0（断电矩阵 7）；activate
 *             前的 activate 失败保持 Ready 供重试。进程单实例由 launcher 串行
 *             调度保证；此分支绝不写 update/partial、绝不删除版本目录。
 * @note       本文件不提供头文件（任务限定三个 .c），集成方在 main.c 处以
 *             extern 原型声明调用；返回值即进程退出码（0=成功）。
 */
#include "XUpgradeEngine.h"

#include "xupgrade_shared.h" /* 模块内部共享小工具（xups_*）：串/版本/根推导 */
#include "XUpgradeStore.h"
#include "XUpgradeStoreDir.h"
#include "XUpgradeIdentity.h"

#include "XMemory.h"
#include "XSystem.h"
#include "XPrintf.h"

#if XUPGRADE_ON

/* ========================================================================== */
/*                                  内部常量                                   */
/* ========================================================================== */

/** @brief 参数/状态错误：用法不符或 state.json 不可读。 */
#define XUPGRADE_STUB_EXIT_USAGE 2
/** @brief state.json 不可读或 staged 标记缺失（无事可做按 0，损坏按 3）。 */
#define XUPGRADE_STUB_EXIT_STATE 3
/** @brief activate 失败：保持 Ready 供 launcher 重试。 */
#define XUPGRADE_STUB_EXIT_ACTIVATE 4

/* 串/版本/trim/根推导小工具已收敛至 xupgrade_shared.h（xups_* 前缀）。 */

/**
 * @brief apply 桩主入口（--xupgrade-apply 分支；不初始化 GUI）。
 * @param argc 命令行参数个数。
 * @param argv 命令行参数数组；须含 "--xupgrade-apply"，可含 "--root <路径>"
 *             显式指定安装根（缺省由可执行文件路径推导）。
 * @return 进程退出码：0=切换完成/无事可做；2=用法错误；3=状态损坏；
 *         4=activate 失败（保持 Ready，可重试）。
 */
int XUpgradeApplyStub_main(int argc, char* argv[])
{
    char root[1024];
    char updateDir[1200];
    XUpgradeStateFile state;
    XUpgradeStore* store = NULL;
    const char** errText;
    const char* err = NULL;
    uint32_t i;
    uint32_t stagedCount = 0;
    bool sawApplyFlag = false;
    int exitCode = 0;
    errText = &err;
    root[0] = '\0';
    updateDir[0] = '\0';
    if (argc <= 0 || !argv) return XUPGRADE_STUB_EXIT_USAGE;
    {
        int a;
        for (a = 1; a < argc; ++a) {
            if (!argv[a]) continue;
            if (xups_str_eq(argv[a], "--xupgrade-apply"))
                sawApplyFlag = true;
            else if (xups_str_eq(argv[a], "--root") && a + 1 < argc && argv[a + 1]) {
                (void)xups_str_copy(root, sizeof(root), argv[a + 1]);
                ++a;
            }
        }
    }
    if (!sawApplyFlag) return XUPGRADE_STUB_EXIT_USAGE;
    if (root[0] == '\0' && !xups_derive_root(root, sizeof(root)))
        return XUPGRADE_STUB_EXIT_USAGE;
    /* 拼工作区路径（char 直拼；load/save 内部自拼 state.json 文件名）。 */
    {
        size_t n = xups_cstr_len(root);
        if (n + 1 + xups_cstr_len(XUPGRADE_DIR_UPDATE) + 1 > sizeof(updateDir))
            return XUPGRADE_STUB_EXIT_USAGE;
        (void)xups_str_copy(updateDir, sizeof(updateDir), root);
        updateDir[n] = '/';
        (void)xups_str_copy(updateDir + n + 1,
                                sizeof(updateDir) - n - 1,
                                XUPGRADE_DIR_UPDATE);
    }
    /* §8.1 步骤 1：读 state.json 确认 Ready（Applying=上次桩中断续做）。 */
    if (!XUpgradeStateFile_load(&state, updateDir)) return XUPGRADE_STUB_EXIT_STATE;
    if (state.m_state != XUpgradeState_Ready && state.m_state != XUpgradeState_Applying)
        return 0; /* 幂等：无待应用内容（矩阵 7 口径） */
    for (i = 0; i < state.m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i)
        if (state.m_components[i].m_staged &&
            xups_version_valid(state.m_components[i].m_stagedVersion))
            ++stagedCount;
    if (stagedCount == 0) {
        /* staged 标记全无：无事发生（不变式 1 口径），收敛为 BootConfirming。 */
        state.m_state = XUpgradeState_BootConfirming;
        (void)XUpgradeStateFile_save(&state, updateDir);
        return 0;
    }
    /* §8.1 步骤 2 前置：先把 Applying 落盘（staged 标志保留作续做依据），
     * 再逐组件切指针。顺序修正（矩阵 6）：原顺序在"activate 完成后、
     * Applying 落盘前"留有断电窗口，盘上为"指针已切、state 仍 Ready"，自检
     * 按非确认窗口把它当外部改动回滚健康升级；先落 Applying 后该窗口落入
     * 确认路径，健康收敛 Active、损坏才回滚。Applying 落盘失败=盘上仍是
     * Ready 原样，按状态错误返回供 launcher 整体重试。 */
    state.m_state = XUpgradeState_Applying;
    if (!XUpgradeStateFile_save(&state, updateDir)) return XUPGRADE_STUB_EXIT_STATE;
    /* §8.1 步骤 2：逐组件 activate（DirStore=原子写 pointers/<comp>）。 */
    store = XUpgradeStoreDir_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, root);
    if (!store) {
        state.m_state = XUpgradeState_Ready;
        (void)XUpgradeStateFile_save(&state, updateDir);
        return XUPGRADE_STUB_EXIT_ACTIVATE;
    }
    for (i = 0; i < state.m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i) {
        XUpgradeStateComponent* c = &state.m_components[i];
        if (!c->m_staged || !xups_version_valid(c->m_stagedVersion)) continue;
        if (XUpgradeStore_activate_base(store, c->m_id, c->m_stagedVersion, errText)
            != XUpgradeError_Ok) {
            XPrintf("xupgrade-apply: activate %s@%s failed: %s\n",
                    c->m_id, c->m_stagedVersion, err ? err : "");
            state.m_state = XUpgradeState_Ready; /* 回退 Ready，launcher 可重试 */
            (void)XUpgradeStateFile_save(&state, updateDir);
            XClassDelete((XClass*)store);
            return XUPGRADE_STUB_EXIT_ACTIVATE;
        }
    }
    XClassDelete((XClass*)store);
    /* §8.1 步骤 3：清 staged 标记 → BootConfirming（Applying 已前置落盘，
     * 此处一次原子落盘收敛）。 */
    for (i = 0; i < state.m_componentCount && i < XUPGRADE_STATE_COMPONENT_MAX; ++i) {
        state.m_components[i].m_staged = false;
    }
    state.m_state = XUpgradeState_BootConfirming;
    if (!XUpgradeStateFile_save(&state, updateDir)) exitCode = XUPGRADE_STUB_EXIT_STATE;
    /* 返回 0 后由 launcher 启动正式版本，其自检完成 Active/RolledBack 收敛。 */
    return exitCode;
}

#endif /* XUPGRADE_ON */
