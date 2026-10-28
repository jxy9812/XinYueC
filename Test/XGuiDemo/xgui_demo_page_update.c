/******************************************************************************
 * @file       xgui_demo_page_update.c
 * @brief      XGuiWindowDemo「软件更新」页（系统设置：关于→软件更新）。
 * @details    契约见 xgui_demo_pages.h（demo_page_update_build /
 *             demo_page_update_autotest / demo_page_update_adapt /
 *             demo_page_update_shutdown）。
 *
 *             页面内容（2026-10-07 随 XUpgrade M2「设置页 UI 接入」新增）：
 *              - 当前组件版本列表（app/fontres）：经 XUpgradeStoreDir 目录
 *                后端只读指针文件（pointers/<comp>），引擎未启动也可展示；
 *              - 手动检查按钮（checkNow）：首次点击懒启动引擎
 *                （XUpgrade_init 带 autoCheck=false，仅手动检查；安装根取
 *                系统临时目录下 xgui_demo_update，绝不写 demo 资源/安装
 *                目录，不影响 demo 启动路径与速度）；
 *              - 策略显示：最近一次生效策略/灰度判定 + 当前维护窗口与
 *                强制宽限期（XUpgrade_applyWindow/forcedGraceHours 回显）；
 *              - 四段进度条（下载/校验/解包/激活）：Progress 事件驱动，
 *                页面只如实反映 m_phase/m_percent，不做阶段推断；
 *              - 「重启以完成升级」提示（ReadyNeedRestart 事件）+ 立即
 *                应用入口（XUpgrade_applyNow）；
 *              - 回滚故障提示（RolledBack 事件，含 m_detail）；
 *              - CheckFailed（含灰度未命中的「暂无更新」语义）静默显示于
 *                状态行——引擎 URL 缺省空串，检查必失败，按契约只在手动
 *                检查时提示（本页检查均为手动触发）。
 *
 *             定位：纯观察者 + 用户决策入口（检查/升级/跳过/应用四钮），
 *             引擎逻辑零复制（设计文档 XUpgrade.md §12 UI 集成表）。
 *
 *             线程模型：XUpgrade listener 在引擎后台线程回调（XUpgrade.h
 *             契约注明）——本页 listener 只把事件快照压入互斥保护的 8 槽
 *             环形队列（满则丢最旧，进度类事件保持流动），UI 线程经
 *             120ms XTimer 泵出并落控件；定时器仅在引擎懒启动成功后开启，
 *             页面构建路径零引擎调用（版本列表只读两次指针文件，微秒级）。
 *
 *             autotest 口径：全程非阻塞、不触网络（不发起检查）；事件
 *             路径经真实 listener 入队 + 真实泵出回调验证——与引擎线程
 *             共用同一段队列/落控件代码，仅免去线程时序；引擎懒启动走
 *             真实 XUpgrade_init（工作区在临时目录，占用时按失败口径）。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include "XGuiConfig.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XWidget.h"
#include "XBoxLayout.h" /* 页内布局容器化：根垂直盒+行/列嵌套盒统一分配几何。 */
#include "XLabel.h"
#include "XComboBox.h"
#include "XLineEdit.h"
#include "XMessageBox.h"
#include "XDialogButtonBox.h"
#include "XPushButton.h"
#include "XProgressBar.h"
#include "XTimer.h"
#include "XMutex.h"
#include "XCoreApplication.h" /* sendPostedEvents：定向冲刷 DeferredDelete */
#include "XProcess.h"        /* startDetached：重启弹窗「是」分离拉起新实例。 */
#include "XSystem.h"      /* XSystem_environment: 环境变量唯一入口。 */
#include "XString.h"      /* 版本列表文本断言（XLabel_text 读法）。 */
#include "XUpgrade.h"
#include "XUpgradeManifest.h" /* XUPGRADE_CATALOG_MAX：远程版本下拉容量。 */
#include "XUpgradeStore.h"    /* 布局常量 + currentVersion 只读查询。 */
#include "XUpgradeStoreDir.h" /* 目录后端工厂（版本列表只读实例）。 */

/* apply 桩入口（无公共头文件；extern 原型口径同 main.c）：演示页「立即应用」
 * 在本进程内完成桩的指针切换（测试负载不覆盖运行中自身文件，安全）。 */
extern int XUpgradeApplyStub_main(int argc, char* argv[]);
#include "xgui_demo_pages.h"

/*
 * 控件段宏守卫：引擎依赖 XUPGRADE_ON，页面控件依赖 Label/PushButton/
 * ProgressBar。任一被裁剪时降级为契约桩（build 返回 NULL、autotest 返回
 * -1、adapt/shutdown 零操作），主文件可无条件链接。
 */
#if XUPGRADE_ON && XWIDGET_ON && XLABEL_ON && XPUSHBUTTON_ON && XPROGRESSBAR_ON

/* ==================== 布局常量 ==================== */

/* 页根装配基线常量（UPD_FIELD_X/UPD_BAR_X/UPD_ROW_PITCH/UPD_URL_Y/
 * UPD_BAR_Y0/UPD_BTN_Y）随 2026-10-17 页内布局容器化退场：几何分配
 * 全权移交页根 XBoxLayout（嵌套行/列盒边距复刻 776x458 设计稿间距）。 */

/** @brief 事件队列容量（listener 后台线程生产、UI 泵消费；满丢最旧）。 */
#define UPD_QUEUE_CAP  8
/** @brief UI 泵周期（毫秒）：语义事件到控件的典型时延上界。 */
#define UPD_PUMP_MS    120

/** @brief 窄窗分档阈值（页根宽，逻辑像素）：决策行单行装配内容 694 + 根盒
 *         左右边距 24 ≈ 718，低于它的竖屏/窄分栏单行必然水平溢出——adapt
 *         按此分档切「决策双行 + 40 触控行高 + 提示折行」版式。 */
#define UPD_COMPACT_MAX_W 720

/** @brief 提示行文案（宽档单行，复刻原稿口径）。 */
static const char UPD_HINT_WIDE[] =
    "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8\xE5\x9C\xB0"
    "\xE5\x9D\x80\xE5\x9C\xA8\xE8\xBE\x93\xE5\x85\xA5"
    "\xE8\xA1\x8C\xE8\xAE\xBE\xE7\xBD\xAE\xEF\xBC\x88"
    "\xE9\xA6\x96\xE6\xAC\xA1\xE5\x90\xAF\xE5\x8A\xA8"
    "\xE5\x90\x8E\xE9\x94\x81\xE5\xAE\x9A\xEF\xBC\x89"
    "\xEF\xBC\x9B\xE5\xBC\x95\xE6\x93\x8E\xE6\x87\x92"
    "\xE5\x90\xAF\xE5\x8A\xA8\xEF\xBC\x8C\xE5\xB7\xA5"
    "\xE4\xBD\x9C\xE5\x8C\xBA\xE5\x9C\xA8\xE7\xB3\xBB"
    "\xE7\xBB\x9F\xE4\xB8\xB4\xE6\x97\xB6\xE7\x9B\xAE"
    "\xE5\xBD\x95";
    /* 服务器地址在输入行设置（首次启动后锁定）；引擎懒启动，工作区在系统临时目录 */
/** @brief 提示行文案（窄档在分号处手动折两行）：固定高标签不参与自动换行，
 *         单行约 460 逻辑宽在 400 宽竖屏必被右缘裁切，且尾部正落右下角
 *         性能悬浮层覆盖区；折行后两行最长约 240 逻辑宽，均避开右缘。 */
static const char UPD_HINT_COMPACT[] =
    "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8\xE5\x9C\xB0"
    "\xE5\x9D\x80\xE5\x9C\xA8\xE8\xBE\x93\xE5\x85\xA5"
    "\xE8\xA1\x8C\xE8\xAE\xBE\xE7\xBD\xAE\xEF\xBC\x88"
    "\xE9\xA6\x96\xE6\xAC\xA1\xE5\x90\xAF\xE5\x8A\xA8"
    "\xE5\x90\x8E\xE9\x94\x81\xE5\xAE\x9A\xEF\xBC\x89"
    "\x0A"
    "\xE5\xBC\x95\xE6\x93\x8E\xE6\x87\x92\xE5\x90\xAF"
    "\xE5\x8A\xA8\xEF\xBC\x8C\xE5\xB7\xA5\xE4\xBD\x9C"
    "\xE5\x8C\xBA\xE5\x9C\xA8\xE7\xB3\xBB\xE7\xBB\x9F"
    "\xE4\xB8\xB4\xE6\x97\xB6\xE7\x9B\xAE\xE5\xBD\x95";
    /* 服务器地址在输入行设置（首次启动后锁定）\n引擎懒启动，工作区在系统临时目录 */

/* ==================== 页面内部控件登记（demo 单实例 static 自持） ==== */

/** @brief 软件更新页内部登记表。 */
static struct
{
    XWidget*         m_root;        /**< 页面根控件（契约返回值；堆对象）。 */
    DemoPageStatusFn m_status;      /**< 主窗口状态栏反馈回调（借用）。 */
    void*            m_user;        /**< 回调上下文（主窗口指针，借用）。 */
    char             m_lastStatus[192]; /**< 最近一次反馈文本（自测断言用）。 */
    bool             m_ready;       /**< build 已完成且控件指针有效。 */

    /* ---- 展示区 ---- */
    XLabel*    m_titleLabel;   /**< 页标题。 */
    XLabel*    m_progVerLabel; /**< 程序版本（XCoreApplication applicationVersion）。 */
    XLabel*    m_capVersion;   /**< 「当前版本」标题。 */
    XLabel*    m_versionLabel; /**< 组件版本列表（app 行；2026-10-27 拆单行）。 */
    XLabel*    m_versionFontLabel; /**< 组件版本 fontres 行（拆单行缓解
                                    *   XLabel 多行渲染残影，2026-10-07 立案项）。 */
    XLabel*    m_capPolicy;    /**< 「策略」标题。 */
    XLabel*    m_policyLabel;  /**< 生效策略行（2026-10-27 拆单行）。 */
    XLabel*    m_policyGrayLabel; /**< 灰度行（拆单行缓解 XLabel 多行渲染
                                    *   残影，2026-10-07 立案项）。 */
    XLabel*    m_policyWindowLabel; /**< 维护窗口/强制宽限行。 */

    /* ---- 服务器地址行（引擎启动前可编辑；启动后锁定） ---- */
    XLabel*    m_urlCap;       /**< 「服务器」标题。 */
    XLineEdit* m_urlEdit;      /**< 更新服务器 URL 模板（{channel} 占位）。 */

    /* ---- 四段进度条（Progress 事件驱动；下标=XUpgradePhase） ---- */
    XProgressBar* m_phaseBar[4]; /**< 下载/校验/解包/激活。 */
    XLabel*       m_phaseCap[4]; /**< 各段标题。 */

    /* ---- 决策按钮行 ---- */
    XPushButton* m_checkBtn;   /**< 手动检查（checkNow）。 */
    XPushButton* m_skipBtn;    /**< 跳过此版本（optional 待决策且有版本号时启用）。 */
    /* 安装入口统一收敛到「安装所选版本」（m_installBtn）：检查后下拉预选
     * 最新版，升级=装最新、降级=装旧版，无需并排的「立即升级/立即应用」。 */

    /* ---- 多版本选择行（schema 2 目录；安装任意目录版本含降级） ---- */
    XLabel*      m_remoteCap;  /**< 「远程版本」标题。 */
    XComboBox*   m_remoteCombo;/**< 远程版本下拉（检查后回填目录条目）。 */
    XPushButton* m_installBtn; /**< 安装所选版本（installVersion 显式降级/重装）。 */

    /* ---- 提示 + 状态行（容器化后由根盒垂直弹簧钉底） ---- */
    XLabel*    m_hintLabel;    /**< 演示口径说明行。 */
    XLabel*    m_statusLabel;  /**< 事件/结果反馈行。 */

    /* ---- 页内布局容器（2026-10-17 容器化：几何分配全权移交布局器；
     *      盒对象为值成员随页常驻——XWidget_setLayout 按借用挂根不取
     *      所有权，包裹盒池容量按装配上限预留，池满静默丢弃） ---- */
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    XBoxLayout m_rootLayout;   /**< 页根垂直盒（统一分配全页几何）。 */
    XBoxLayout m_titleRow;     /**< 标题行（标题+弹簧+程序版本右锚定）。 */
    XBoxLayout m_versionRow;   /**< 当前版本行（标题列+列表伸展）。 */
    XBoxLayout m_policyRow;    /**< 策略行（标题列+策略文本伸展）。 */
    XBoxLayout m_policyTextCol; /**< 策略文本三单行纵排（2026-10-27）。 */
    XBoxLayout m_urlRow;       /**< 服务器行（标题列+地址输入伸展）。 */
    XBoxLayout m_barsCol;      /**< 四段进度条列。 */
    XBoxLayout m_barRows[4];   /**< 各段行盒（标题列+进度条伸展）。 */
    XBoxLayout m_buttonRow;    /**< 决策+安装单行。 */
    XBoxLayout m_buttonRow2;   /**< 决策续行盒：与决策行同位预挂根盒；宽档恒为
                                * 空盒（无条目不占尺寸、不参与行距），窄档由
                                * adapt 注入决策行让位的右半段。 */
    XBoxLayout m_wrapPool[14]; /**< 单条目包裹盒池（盒边距复刻原手写间距/行内错位下沉）。 */
    int        m_wrapCount;    /**< 包裹盒池游标。 */
    XBoxLayout* m_remoteWrap;  /**< 「远程版本」标题包裹盒句柄（窄档搬移用：
                                * 子布局对象即布局条目，takeAt/addLayout
                                * 即可零堆分配在两行间搬移）。 */
    XBoxLayout* m_comboWrap;   /**< 远程版本下拉包裹盒句柄（窄档搬移用）。 */
    XBoxLayout* m_installWrap; /**< 安装钮包裹盒句柄（窄档搬移用）。 */
    bool       m_compact;      /**< 窄档生效中（adapt 宽度分档状态：只在翻转
                                * 时搬移条目，防 resize 反复重排抖动）。 */
    XWidget*   m_titleSpring;  /**< 标题行水平弹簧（程序版本推右缘）。 */
    XWidget*   m_barsGap;      /**< 进度条列与决策行间固定空隙（16px，复刻原间距）。 */
    XWidget*   m_vSpring;      /**< 根垂直弹簧（吃掉多余高度，钉底两行）。 */
    XWidget*   m_hintGap;      /**< 提示行与状态行间固定空隙（2px，复刻原行距）。 */
#endif

    /* ---- 跨线程事件队列（listener 后台线程 → UI 泵） ---- */
    XMutex*    m_queueMutex;   /**< 队列互斥（XLock_NonRecursive）。 */
    XUpgradeEvent m_queue[UPD_QUEUE_CAP]; /**< 环形队列槽。 */
    int        m_queueHead;    /**< 队头下标。 */
    int        m_queueCount;   /**< 队列长度。 */

    /* ---- UI 泵定时器（懒启动成功后才 start） ---- */
    XTimer*    m_pumpTimer;    /**< UI 线程泵（UPD_PUMP_MS 周期）。 */
    bool       m_engineStarted;/**< 引擎已懒启动（幂等门）。 */
    bool       m_dialogSuppress; /**< 自测抑制弹窗（autotest 注入 ReadyNeedRestart 时置位防模态挂死）。 */

    /* ---- 最近一次语义事件快照（决策入口启用与策略显示） ---- */
    XUpgradePolicy m_lastPolicy;    /**< UpdateAvailable 携带的生效策略。 */
    XUpgradeGrayDecision m_lastGray;/**< UpdateAvailable 携带的灰度判定。 */
    char       m_lastVersion[XUPGRADE_VERSION_MAX];      /**< 目标版本。 */
    char       m_lastComponent[XUPGRADE_COMPONENT_ID_MAX]; /**< 目标组件。 */
    bool       m_available;    /**< optional 待决策（升级/跳过钮启用）。 */
    bool       m_readyRestart; /**< ReadyNeedRestart 待重启（应用钮启用）。 */

    /* ---- 组件版本缓存（版本列表只读实例回填） ---- */
    char       m_versionApp[XUPGRADE_VERSION_MAX];
    char       m_versionFont[XUPGRADE_VERSION_MAX];
} s_upd;

/* ==================== 内部辅助 ==================== */

/** @brief 向页面状态行与主窗状态栏反馈并登记最近文本（自测断言用）。 */
static void upd_report(const char* fmt, const char* detail)
{
    char buf[192];
    snprintf(buf, sizeof(buf), fmt, detail ? detail : "");
    snprintf(s_upd.m_lastStatus, sizeof(s_upd.m_lastStatus), "%s", buf);
    if (s_upd.m_statusLabel)
        XLabel_setText_2(s_upd.m_statusLabel, buf);
    if (s_upd.m_status)
        s_upd.m_status(s_upd.m_user, buf);
}

/** @brief 退出清理前向声明（build 重复装配路径先收口上一实例）。 */
void demo_page_update_shutdown(void);

/** @brief demo 更新工作区安装根：系统临时目录下 xgui_demo_update。
 *  @details 口径同主文件 demo_autotest_tmpdir：Windows 取 TEMP/TMP，
 *           其余取 /tmp；全缺省返回 false（引擎保持未启动，页面如实
 *           反馈）。update/ 工作区与 device.key 均落该根下，与 demo
 *           资源/安装目录隔离。 */
static bool upd_demoRoot(char* root, size_t cap)
{
    const char* tmp;
#ifdef _WIN32
    tmp = XSystem_environment("TEMP");
    if (!tmp || !tmp[0]) tmp = XSystem_environment("TMP");
#else
    tmp = "/tmp";
#endif
    if (!tmp || !tmp[0]) return false;
    return snprintf(root, cap, "%s/xgui_demo_update", tmp) < (int)cap;
}

/** @brief 引擎状态 → 中文名。 */
static const char* upd_stateName(XUpgradeState state)
{
    switch (state) {
    case XUpgradeState_Idle:           return "\xE7\xA9\xBA\xE9\x97\xB2";             /* 空闲 */
    case XUpgradeState_Checking:       return "\xE6\xA3\x80\xE6\x9F\xA5\xE4\xB8\xAD"; /* 检查中 */
    case XUpgradeState_Downloading:    return "\xE4\xB8\x8B\xE8\xBD\xBD\xE4\xB8\xAD"; /* 下载中 */
    case XUpgradeState_Verifying:      return "\xE6\xA0\xA1\xE9\xAA\x8C\xE4\xB8\xAD"; /* 校验中 */
    case XUpgradeState_Staging:        return "\xE8\xA7\xA3\xE5\x8C\x85\xE4\xB8\xAD"; /* 解包中 */
    case XUpgradeState_Ready:          return "\xE5\xBE\x85\xE5\xBA\x94\xE7\x94\xA8"; /* 待应用 */
    case XUpgradeState_Applying:       return "\xE5\xBA\x94\xE7\x94\xA8\xE4\xB8\xAD"; /* 应用中 */
    case XUpgradeState_BootConfirming: return "\xE8\x87\xAA\xE6\xA3\x80\xE4\xB8\xAD"; /* 自检中 */
    case XUpgradeState_Active:         return "\xE5\xB7\xB2\xE7\x94\x9F\xE6\x95\x88"; /* 已生效 */
    case XUpgradeState_RolledBack:     return "\xE5\xB7\xB2\xE5\x9B\x9E\xE6\xBB\x9A"; /* 已回滚 */
    case XUpgradeState_CheckFailed:    return "\xE6\xA3\x80\xE6\x9F\xA5\xE5\xA4\xB1\xE8\xB4\xA5"; /* 检查失败 */
    case XUpgradeState_DownloadFailed: return "\xE4\xB8\x8B\xE8\xBD\xBD\xE5\xA4\xB1\xE8\xB4\xA5"; /* 下载失败 */
    default:                           return "?";
    }
}

/** @brief 升级策略 → 中文名。 */
static const char* upd_policyName(XUpgradePolicy policy)
{
    switch (policy) {
    case XUpgradePolicy_Forced:   return "\xE5\xBC\xBA\xE5\x88\xB6";             /* 强制 */
    case XUpgradePolicy_Optional: return "\xE5\x8F\xAF\xE8\xB7\xB3\xE8\xBF\x87"; /* 可跳过 */
    case XUpgradePolicy_Silent:   return "\xE9\x9D\x99\xE9\xBB\x98";             /* 静默 */
    default:                      return "?";
    }
}

/** @brief 灰度判定 → 中文名。 */
static const char* upd_grayName(XUpgradeGrayDecision gray)
{
    switch (gray) {
    case XUpgradeGrayDecision_NotEvaluated: return "\xE6\x9C\xAA\xE5\x88\xA4\xE5\xAE\x9A";     /* 未判定 */
    case XUpgradeGrayDecision_AllowList:    return "\xE5\x90\x8D\xE5\x8D\x95\xE5\x91\xBD\xE4\xB8\xAD"; /* 名单命中 */
    case XUpgradeGrayDecision_Hit:          return "\xE5\x91\xBD\xE4\xB8\xAD";                 /* 命中 */
    case XUpgradeGrayDecision_Miss:         return "\xE6\x9C\xAA\xE5\x91\xBD\xE4\xB8\xAD";     /* 未命中 */
    case XUpgradeGrayDecision_DenyList:     return "\xE6\x8B\x92\xE7\xBB\x9D\xE5\x90\x8D\xE5\x8D\x95"; /* 拒绝名单 */
    default:                                return "?";
    }
}

/** @brief 进度阶段 → 中文名（下标=XUpgradePhase）。 */
static const char* upd_phaseName(int phase)
{
    switch (phase) {
    case XUpgradePhase_Download: return "\xE4\xB8\x8B\xE8\xBD\xBD"; /* 下载 */
    case XUpgradePhase_Verify:   return "\xE6\xA0\xA1\xE9\xAA\x8C"; /* 校验 */
    case XUpgradePhase_Stage:    return "\xE8\xA7\xA3\xE5\x8C\x85"; /* 解包 */
    case XUpgradePhase_Activate: return "\xE6\xBF\x80\xE6\xB4\xBB"; /* 激活 */
    default:                     return "?";
    }
}

/** @brief 决策按钮可用性随最近事件快照刷新。 */
static void upd_refreshButtons(void)
{
    if (s_upd.m_skipBtn)
        XWidget_setEnabled((XWidget*)s_upd.m_skipBtn,
                           s_upd.m_available && s_upd.m_lastVersion[0]);
    /* 安装所选版本：目录缓存有远程条目即可用（多版本选择安装，§4 扩展）。 */
    if (s_upd.m_installBtn)
        XWidget_setEnabled((XWidget*)s_upd.m_installBtn,
                           XUpgrade_remoteVersionCount() > 0);
}

/** @brief 回填远程版本下拉（schema 2 目录缓存；检查完成后调用）。 */
static void upd_refreshRemoteList(void)
{
    uint32_t count;
    uint32_t i;
    if (!s_upd.m_remoteCombo) return;
    XComboBox_clear(s_upd.m_remoteCombo);
    count = XUpgrade_remoteVersionCount();
    for (i = 0; i < count && i < XUPGRADE_CATALOG_MAX; ++i) {
        char ver[32];
        if (!XUpgrade_remoteVersionAt(i, ver, sizeof(ver))) break;
        XComboBox_addItem_2(s_upd.m_remoteCombo, ver);
    }
    if (count > 0)
        XComboBox_setCurrentIndex(s_upd.m_remoteCombo, 0);
    upd_refreshButtons();
}

/** @brief 四段进度条全部归零（新一轮检查/升级起点的展示复位）。 */
static void upd_resetBars(void)
{
    int i;
    for (i = 0; i < 4; ++i)
        if (s_upd.m_phaseBar[i])
            XProgressBar_reset(s_upd.m_phaseBar[i]);
}

/** @brief 应用元数据同步：XCoreApplication applicationVersion 跟随引擎激活
 *  版本（真实产品升级=换二进制，编译期版本随激活版本走；demo 二进制固定，
 *  故由引擎指针回写模拟，标题行「程序版本」与「当前版本」两处口径一致）。
 *  指针为空（未安装）时回退出厂版本宏。 */
static void upd_syncAppVersion(void)
{
    const char* ver = s_upd.m_versionApp[0] ? s_upd.m_versionApp
                                            : XUPGRADE_PRODUCT_VERSION;
    XString* s = XString_create_utf8(ver);
    if (s) {
        XCoreApplication_setApplicationVersion(s);
        XClassDelete((XClass*)s);
    }
}

/** @brief 标题行程序版本标签回填（名称 · 程序版本）。 */
static void upd_refreshProgVer(void)
{
    const XString* appVer = XCoreApplication_applicationVersion();
    const XString* appName = XCoreApplication_applicationName();
    const char* verUtf8 = appVer ? XString_toUtf8(appVer) : NULL;
    const char* nameUtf8 = appName ? XString_toUtf8(appName) : NULL;
    char progText[128];
    if (!s_upd.m_progVerLabel) return;
    if (!verUtf8 || verUtf8[0] == '\0')
        verUtf8 = XUPGRADE_PRODUCT_VERSION;
    if (nameUtf8 && nameUtf8[0])
        snprintf(progText, sizeof(progText),
                 "%s \xC2\xB7 \xE7\xA8\x8B\xE5\xBA\x8F\xE7\x89\x88\xE6"
                 "\x9C\xAC: %s", nameUtf8, verUtf8); /* x · 程序版本: y */
    else
        snprintf(progText, sizeof(progText),
                 "\xE7\xA8\x8B\xE5\xBA\x8F\xE7\x89\x88\xE6\x9C\xAC: %s",
                 verUtf8); /* 程序版本: */
    XLabel_setText_2(s_upd.m_progVerLabel, progText);
}

/** @brief 版本列表回填（目录后端只读指针文件；引擎未启动亦可）。 */
static void upd_refreshVersions(void)
{
    static const char fontresId[] = "fontres";
    char root[1024];
    XUpgradeStore* store;
    if (!s_upd.m_versionLabel) return;
    s_upd.m_versionApp[0] = '\0';
    s_upd.m_versionFont[0] = '\0';
    if (upd_demoRoot(root, sizeof(root)) &&
        (store = XUpgradeStoreDir_create(root)) != NULL) {
        XUpgradeStore_currentVersion_base(store, "app",
                                          s_upd.m_versionApp,
                                          sizeof(s_upd.m_versionApp));
        XUpgradeStore_currentVersion_base(store, fontresId,
                                          s_upd.m_versionFont,
                                          sizeof(s_upd.m_versionFont));
        XClassDelete((XClass*)store);
    }
    {
        char appLine[96];
        /* app 行携带远程对照（检查到新版本时）：「本地 x / 远程 y」。
         * 版本对照放本 label——policyLabel 的多行动态文本存在渲染残影
         * （XLabel 局部重绘家族，2026-10-07 立案），本 label 渲染可靠。 */
        if (s_upd.m_lastVersion[0] &&
            (s_upd.m_lastComponent[0] == '\0' ||
             strcmp(s_upd.m_lastComponent, "app") == 0))
            snprintf(appLine, sizeof(appLine),
                     "\xE6\x9C\xAC\xE5\x9C\xB0 %s / \xE8\xBF\x9C\xE7\xA8\x8B %s",
                     /* 本地 x / 远程 y */
                     s_upd.m_versionApp[0] ? s_upd.m_versionApp
                                           : "\xE6\x9C\xAA\xE5\xAE\x89\xE8\xA3\x85",
                     s_upd.m_lastVersion);
        else
            snprintf(appLine, sizeof(appLine), "%s",
                     s_upd.m_versionApp[0] ? s_upd.m_versionApp
                                           : "\xE6\x9C\xAA\xE5\xAE\x89\xE8\xA3\x85");
        {
            char appRow[112];
            snprintf(appRow, sizeof(appRow), "app: %s", appLine);
            XLabel_setText_2(s_upd.m_versionLabel, appRow);
        }
        XLabel_setText_2(s_upd.m_versionFontLabel,
                         s_upd.m_versionFont[0]
                             ? (snprintf(appLine, sizeof(appLine), "fontres: %s",
                                         s_upd.m_versionFont), appLine)
                             : "fontres: \xE6\x9C\xAA\xE5\xAE\x89\xE8\xA3\x85"); /* fontres: 未安装 */
    }
    /* 应用元数据与激活版本同步（标题行「程序版本」同口径显示）。 */
    upd_syncAppVersion();
    upd_refreshProgVer();
}

/** @brief 策略区回填：最近事件快照 + 引擎侧窗口/宽限期回显。 */
static void upd_refreshPolicy(void)
{
    XUpgradeApplyWindow window;
    char line[192];
    if (!s_upd.m_policyLabel) return;
    window = XUpgrade_applyWindow();
    if (window.m_enabled)
        snprintf(line, sizeof(line),
                 "%02u:%02u-%02u:%02u",
                 (unsigned)window.m_startHour, (unsigned)window.m_startMinute,
                 (unsigned)window.m_endHour, (unsigned)window.m_endMinute);
    else
        snprintf(line, sizeof(line), "%s",
                 "\xE5\x85\xB3\xE9\x97\xAD"); /* 关闭 */
    {
        /* 策略区三行固定口径，拆单行后逐行 setText（本地/远程版本对照放
         * 「当前版本」块——多行动态文本渲染残影已由拆单行缓解）。 */
        char windowLine[96];
        snprintf(windowLine, sizeof(windowLine),
                 "\xE7\xBB\xB4\xE6\x8A\xA4\xE7\xAA\x97\xE5\x8F\xA3: %s  " /* 维护窗口: */
                 "\xE5\xBC\xBA\xE5\x88\xB6\xE5\xAE\xBD\xE9\x99\x90: %u \xE5\xB0\x8F\xE6\x97\xB6", /* 强制宽限: N 小时 */
                 line,
                 (unsigned)XUpgrade_forcedGraceHours());
        XLabel_setText_2(s_upd.m_policyLabel,
                         s_upd.m_lastVersion[0]
                             ? upd_policyName(s_upd.m_lastPolicy)
                             : "\xE6\x9C\xAA\xE6\xA3\x80\xE6\x9F\xA5"); /* 未检查 */
        XLabel_setText_2(s_upd.m_policyGrayLabel,
                         upd_grayName(XUpgrade_grayDecision()));
        XLabel_setText_2(s_upd.m_policyWindowLabel, windowLine);
    }
}

/* ==================== 引擎懒启动 ==================== */

static void upd_listener(void* userData, const XUpgradeEvent* event);

/** @brief demo 缺省更新服务器 URL（{channel} 占位，引擎按渠道替换）。 */
static const char UPD_DEFAULT_URL[] = "http://192.168.1.3/xupgrade/{channel}/manifest.json";

/** @brief 解析更新服务器地址：环境变量 XUPGRADE_DEMO_URL 优先（借用到下次
 *         调用前有效），否则回退 UPD_DEFAULT_URL。
 *  @return UTF-8 URL 串；outBuf/cap 非法返回 NULL（引擎按 URL 模板无效
 *          拒绝，页面状态行如实显示）。 */
static const char* upd_serverUrl(char* outBuf, size_t cap)
{
    const char* env = XSystem_environment("XUPGRADE_DEMO_URL");
    size_t len;
    if (!outBuf || cap == 0) return NULL;
    if (env && env[0] != '\0') {
        len = 0;
        while (env[len] != '\0') ++len;
        if (len >= cap) return NULL;
        memcpy(outBuf, env, len + 1);
        return outBuf;
    }
    return UPD_DEFAULT_URL;
}

/** @brief 懒启动升级引擎（首次决策入口点击时；幂等）。
 *  @details autoCheck=false 仅手动检查；安装根=临时目录 xgui_demo_update
 *           （工作区不落 demo 目录）；URL 取界面输入行当前文本（预填
 *           XUPGRADE_DEMO_URL 环境变量或缺省部署地址；引擎 URL 在 init
 *           时固定，启动成功后输入行锁定）。启动成功才开 UI 泵定时器
 *           （后台线程事件 → 控件）。 */
/** @brief 真实自替换残留清理：<exe>.old 保身副本。旧进程退出前删除必败
 *  （Windows 禁删运行中映像，且改名后旧进程仍从 .old 运行），故构建时与
 *  引擎懒启动时各试一次——旧进程退出后的任意时刻删除即可成功。 */
static void upd_cleanupOldImage(void)
{
    char exePath[XSYSTEM_EXECUTABLE_PATH_MAX];
    char oldPath[1280];
    if (!XSystem_executableFilePath(exePath, sizeof(exePath)) ||
        snprintf(oldPath, sizeof(oldPath), "%s.old", exePath) >=
            (int)sizeof(oldPath))
        return;
    {
        XString* oldFile = XString_create_utf8(oldPath);
        if (oldFile) {
            XFile_remove_static(oldFile);
            XClassDelete(oldFile);
        }
    }
}

static bool upd_ensureStarted(void)
{
    XUpgradeConfig config;
    char root[1024];
    char url[512];
    const char* urlText;
    size_t urlLen;
    if (s_upd.m_engineStarted) return true;
    upd_cleanupOldImage(); /* 上一轮真实升级的保身副本（旧进程已退出） */
    if (!upd_demoRoot(root, sizeof(root))) {
        upd_report("\xE5\x8D\x87\xE7\xBA\xA7\xE5\xBC\x95\xE6\x93\x8E\xE6\x9C"
                   "\xAA\xE5\x90\xAF\xE5\x8A\xA8: \xE6\x97\xA0\xE4\xB8\xB4"
                   "\xE6\x97\xB6\xE7\x9B\xAE\xE5\xBD\x95", NULL);
                   /* 升级引擎未启动: 无临时目录 */
        return false;
    }
    /* URL 以界面输入为准：空串按契约"URL 模板无效"由引擎拒绝，状态行如实显示。 */
    urlText = s_upd.m_urlEdit ? XLineEdit_text(s_upd.m_urlEdit) : NULL;
    urlLen = urlText ? strlen(urlText) : 0;
    if (urlLen >= sizeof(url)) urlLen = sizeof(url) - 1u;
    if (urlLen > 0) {
        memcpy(url, urlText, urlLen);
    }
    url[urlLen] = '\0';
    XUpgrade_setListener(upd_listener, NULL); /* 可先于 init 注册（契约）。 */
    memset(&config, 0, sizeof(config));
    config.m_updateUrl = url;             /* 界面输入行当前值（可含 {channel}）。 */
    config.m_channel = "stable";
#ifdef _WIN32
    config.m_targetOs = "win64";
#else
    config.m_targetOs = "linux64";
#endif
    config.m_installRoot = root;
    config.m_checkIntervalHours = 0;      /* 0=不按周期自动检查。 */
    config.m_autoCheck = false;           /* 懒启动：仅手动检查。 */
    config.m_applyWindow.m_enabled = false;
    if (!XUpgrade_init(&config)) {
        upd_report("\xE5\x8D\x87\xE7\xBA\xA7\xE5\xBC\x95\xE6\x93\x8E\xE5\x88"
                   "\x9D\xE5\xA7\x8B\xE5\x8C\x96\xE5\xA4\xB1\xE8\xB4\xA5"
                   "\xEF\xBC\x88\xE5\x8D\xA0\xE7\x94\xA8\xE6\x88\x96\xE6"
                   "\xA0\xB9\xE4\xB8\x8D\xE5\x8F\xAF\xE7\x94\xA8\xEF\xBC\x89",
                   NULL); /* 升级引擎初始化失败（占用或根不可用） */
        return false;
    }
    s_upd.m_engineStarted = true;
    if (s_upd.m_urlEdit) {
        /* 引擎 URL 在 init 时固定：启动成功后锁定输入行（改地址需重启 demo）。 */
        XWidget_setEnabled((XWidget*)s_upd.m_urlEdit, false);
    }
    if (s_upd.m_pumpTimer) {
        XTimer_setInterval(s_upd.m_pumpTimer, UPD_PUMP_MS);
        XTimer_start_base(s_upd.m_pumpTimer);
    }
    upd_refreshVersions();
    upd_refreshPolicy();
    upd_report("\xE5\x8D\x87\xE7\xBA\xA7\xE5\xBC\x95\xE6\x93\x8E\xE5\xB7"
               "\xB2\xE5\xB0\xB1\xE7\xBB\xAA\xEF\xBC\x88\xE6\x89\x8B\xE5"
               "\x8A\xA8\xE6\xA3\x80\xE6\x9F\xA5\xE6\xA8\xA1\xE5\xBC\x8F"
               "\xEF\xBC\x89", NULL); /* 升级引擎已就绪（手动检查模式） */
    return true;
}

/* ==================== 事件队列（listener 生产 / UI 泵消费） ============ */

/** @brief 引擎事件观察者（后台线程上下文）：快照入队后立即返回。
 *  @details 队列满丢最旧（进度/状态类后到事件更新，保持 UI 流动）；
 *           回调内不做任何 UI 操作、不调用引擎控制 API（XUpgrade.h 契约）。 */
static void upd_listener(void* userData, const XUpgradeEvent* event)
{
    (void)userData;
    if (!event || !s_upd.m_queueMutex) return;
    XMutex_lock(s_upd.m_queueMutex);
    if (s_upd.m_queueCount >= UPD_QUEUE_CAP) {
        s_upd.m_queueHead = (s_upd.m_queueHead + 1) % UPD_QUEUE_CAP;
        --s_upd.m_queueCount;
    }
    s_upd.m_queue[(s_upd.m_queueHead + s_upd.m_queueCount) % UPD_QUEUE_CAP] =
        *event;
    ++s_upd.m_queueCount;
    XMutex_unlock(s_upd.m_queueMutex);
}

static void upd_pumpCb(void* userData, XTimerData* timer);
static bool upd_applyAndSwitch(void); /* 弹窗「是」分支先于定义调用（前向声明） */

/** @brief 事件 → 控件落地（仅 UI 线程调用；纯观察口径）。 */
static void upd_applyEvent(const XUpgradeEvent* event)
{
    char text[224];
    switch (event->m_type) {
    case XUpgradeEventType_UpdateAvailable:
        /* 新一轮升级起点：快照策略/灰度/版本并复位四段进度条。 */
        s_upd.m_lastPolicy = event->m_policy;
        s_upd.m_lastGray = event->m_gray;
        snprintf(s_upd.m_lastVersion, sizeof(s_upd.m_lastVersion), "%s",
                 event->m_version);
        snprintf(s_upd.m_lastComponent, sizeof(s_upd.m_lastComponent), "%s",
                 event->m_component);
        s_upd.m_available = (event->m_policy == XUpgradePolicy_Optional);
        s_upd.m_readyRestart = false;
        upd_resetBars();
        upd_refreshButtons();
        upd_refreshVersions(); /* 版本块刷新出「本地 x / 远程 y」对照。 */
        upd_refreshRemoteList(); /* 远程版本下拉回填（多版本选择安装）。 */
        upd_refreshPolicy();
        snprintf(text, sizeof(text),
                 "\xE5\x8F\x91\xE7\x8E\xB0\xE6\x96\xB0\xE7\x89\x88\xE6\x9C"
                 "\xAC: %s\xEF\xBC\x88\xE7\xAD\x96\xE7\x95\xA5: %s\xEF\xBC\x89",
                 /* 发现新版本: x（策略: y） */
                 event->m_version, upd_policyName(event->m_policy));
        upd_report("%s", text);
        break;
    case XUpgradeEventType_Progress:
        /* 四段进度条如实反映 m_phase/m_percent（不做阶段推断）。 */
        if (event->m_phase >= XUpgradePhase_Download &&
            event->m_phase <= XUpgradePhase_Activate &&
            s_upd.m_phaseBar[event->m_phase]) {
            int percent = event->m_percent;
            if (percent < 0) percent = 0;
            if (percent > 100) percent = 100;
            XProgressBar_setValue(s_upd.m_phaseBar[event->m_phase], percent);
        }
        snprintf(text, sizeof(text), "%s %d%%",
                 upd_phaseName((int)event->m_phase), event->m_percent);
        upd_report("%s", text);
        break;
    case XUpgradeEventType_ReadyNeedRestart:
        s_upd.m_readyRestart = true;
        upd_refreshButtons();
        /* 快照 staged 版本：真实自替换按此定位 versions/app/<ver>/bin/
         * <自身文件名>（安装所选版本的 staged 版本可与上次公告不同）。 */
        if (event->m_version[0]) {
            snprintf(s_upd.m_lastVersion, sizeof(s_upd.m_lastVersion), "%s",
                     event->m_version);
            snprintf(s_upd.m_lastComponent, sizeof(s_upd.m_lastComponent),
                     "%s", event->m_component);
        }
        upd_report("%s",
                   "\xE5\x8D\x87\xE7\xBA\xA7\xE5\xB7\xB2\xE5\xB0\xB1\xE7"
                   "\xBB\xAA: \xE9\x87\x8D\xE5\x90\xAF\xE4\xBB\xA5\xE5\xAE"
                   "\x8C\xE6\x88\x90\xE5\x8D\x87\xE7\xBA\xA7");
                   /* 升级已就绪: 重启以完成升级 */
        /* 安装完成弹窗：询问是否立即完成升级（重启后运行新版本）。
         * 自测（dialogSuppress）跳过模态弹窗防挂死。 */
        if (!s_upd.m_dialogSuppress) {
            XMessageBox* box = XMessageBox_create(s_upd.m_root, 0);
            if (box) {
                int clicked;
                XMessageBox_setText(box,
                    "\xE5\x8D\x87\xE7\xBA\xA7\xE5\x8C\x85\xE5\xB7\xB2\xE5"
                    "\xB0\xB1\xE7\xBB\xAA\xEF\xBC\x8C\xE6\x98\xAF\xE5\x90"
                    "\xA6\xE7\xAB\x8B\xE5\x8D\xB3\xE5\xAE\x8C\xE6\x88\x90"
                    "\xE5\x8D\x87\xE7\xBA\xA7\xEF\xBC\x9F"); /* 升级包已就绪，是否立即完成升级？ */
                XMessageBox_setStandardButtons(box,
                    XDialogButtonBoxStandard_Yes | XDialogButtonBoxStandard_No);
                clicked = XMessageBox_exec(box);
                XClassDelete((XClass*)box);
                if (clicked & (int)XDialogButtonBoxStandard_Yes) {
                    /* 是：就地切指针 → 分离拉起新实例 → 退出本进程（新实例
                     * 启动自检转正，运行新版本）。全程走抽象层（XProcess/
                     * XCoreApplication/XSystem），页内零平台调用。 */
                    char exePath[XSYSTEM_EXECUTABLE_PATH_MAX];
                    char upgradeNote[224];
                    if (!upd_applyAndSwitch() ||
                        !XSystem_executableFilePath(exePath, sizeof(exePath)))
                        break;
                    {
                        XString* program = XString_create_utf8(exePath);
                        if (program) {
                            XProcess_startDetached_static(program, NULL, NULL, NULL);
                            XClassDelete(program);
                        }
                    }
                    snprintf(upgradeNote, sizeof(upgradeNote),
                             "\xE5\xB7\xB2\xE5\x88\x87\xE6\x8D\xA2\xE8\x87\xB3 %s"
                             "\xEF\xBC\x8C\xE6\xAD\xA3\xE5\x9C\xA8\xE9\x87\x8D"
                             "\xE5\x90\xAF\xE7\xA8\x8B\xE5\xBA\x8F\xE2\x80\xA6",
                             /* 已切换至 x，正在重启程序… */
                             s_upd.m_lastVersion);
                    upd_report("%s", upgradeNote);
                    XCoreApplication_quit(); /* 退出本进程；新实例自检转正后运行新版本 */
                    break;
                }
                upd_report("%s",
                    "\xE5\xB7\xB2\xE9\x80\x89\xE6\x8B\xA9\xE7\xA8\x8D\xE5"
                    "\x90\x8E\xE5\xAE\x8C\xE6\x88\x90\xEF\xBC\x8C\xE4\xB8"
                    "\x8B\xE6\xAC\xA1\xE5\x90\xAF\xE5\x8A\xA8\xE6\x97\xB6"
                    "\xE5\xB0\x86\xE5\x86\x8D\xE6\xAC\xA1\xE8\xAF\xA2\xE9"
                    "\x97\xAE"); /* 已选择稍后完成，下次启动时将再次询问 */
            }
        }
        break;
    case XUpgradeEventType_ReadyAutoApply:
        upd_report("%s",
                   "\xE6\x8C\x89\xE7\xAD\x96\xE7\x95\xA5\xE8\x87\xAA\xE5"
                   "\x8A\xA8\xE5\xBA\x94\xE7\x94\xA8\xE5\x8D\x87\xE7\xBA"
                   "\xA7\xE2\x80\xA6"); /* 按策略自动应用升级… */
        break;
    case XUpgradeEventType_RolledBack:
        s_upd.m_readyRestart = false;
        upd_refreshButtons();
        snprintf(text, sizeof(text),
                 "\xE5\xB7\xB2\xE5\x9B\x9E\xE6\xBB\x9A: %s", /* 已回滚: */
                 event->m_detail);
        upd_report("%s", text);
        break;
    case XUpgradeEventType_CheckFailed:
        /* 检查失败/暂无更新（含灰度 Miss/DenyList）：状态行静默显示。
         * 远程版本下拉照常回填——"客户端已是最新"也允许多版本选择安装。 */
        upd_refreshRemoteList();
        if (event->m_detail[0])
            snprintf(text, sizeof(text),
                     "\xE6\xA3\x80\xE6\x9F\xA5\xE5\xA4\xB1\xE8\xB4\xA5: %s",
                     event->m_detail); /* 检查失败: */
        else
            snprintf(text, sizeof(text), "%s",
                     "\xE6\x9A\x82\xE6\x97\xA0\xE6\x9B\xB4\xE6\x96\xB0"); /* 暂无更新 */
        upd_report("%s", text);
        upd_refreshPolicy();
        break;
    case XUpgradeEventType_StateChanged:
    default:
        if (event->m_state == XUpgradeState_Checking)
            upd_resetBars(); /* 新一轮检查：进度展示复位。 */
        if (event->m_detail[0])
            upd_report("%s", event->m_detail); /* 引擎说明优先（如下载失败：partial 已保留供续传）。 */
        else
            snprintf(text, sizeof(text),
                     "\xE5\xBC\x95\xE6\x93\x8E\xE7\x8A\xB6\xE6\x80\x81: %s",
                     upd_stateName(event->m_state)); /* 引擎状态: */
        if (!event->m_detail[0]) upd_report("%s", text);
        break;
    }
}

/** @brief UI 泵（120ms 周期）：把队列里的事件逐条落地（UI 线程上下文）。 */
static void upd_pumpCb(void* userData, XTimerData* timer)
{
    (void)userData;
    (void)timer;
    if (!s_upd.m_ready || !s_upd.m_queueMutex) return;
    for (;;) {
        XUpgradeEvent event;
        XMutex_lock(s_upd.m_queueMutex);
        if (s_upd.m_queueCount <= 0) {
            XMutex_unlock(s_upd.m_queueMutex);
            break;
        }
        event = s_upd.m_queue[s_upd.m_queueHead];
        s_upd.m_queueHead = (s_upd.m_queueHead + 1) % UPD_QUEUE_CAP;
        --s_upd.m_queueCount;
        XMutex_unlock(s_upd.m_queueMutex);
        upd_applyEvent(&event);
    }
}

/* ==================== 信号槽（用户决策入口） ==================== */

/** @brief 手动检查：懒启动引擎 → checkNow（结果经事件异步返回）。 */
static void upd_checkSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_upd.m_ready) return;
    if (!upd_ensureStarted()) return;
    if (!XUpgrade_checkNow())
        upd_report("\xE6\xA3\x80\xE6\x9F\xA5\xE6\x9C\xAA\xE5\x8F\x97\xE7"
                   "\x90\x86\xEF\xBC\x88\xE5\xBC\x95\xE6\x93\x8E\xE5\xBF"
                   "\x99\xEF\xBC\x89", NULL); /* 检查未受理（引擎忙） */
    else
        upd_report("%s",
                   "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\xA3\x80\xE6\x9F\xA5\xE6"
                   "\x9B\xB4\xE6\x96\xB0\xE2\x80\xA6"); /* 正在检查更新… */
}

/** @brief 跳过此版本（optional 策略待决策入口）。 */
static void upd_skipSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_upd.m_ready) return;
    if (!upd_ensureStarted()) return;
    XUpgrade_skipVersion(s_upd.m_lastComponent, s_upd.m_lastVersion);
    upd_report("\xE5\xB7\xB2\xE8\xB7\xB3\xE8\xBF\x87\xE7\x89\x88\xE6\x9C"
               "\xAC: %s", s_upd.m_lastVersion); /* 已跳过版本: */
}

/** @brief 受理应用并完成落位（applyNow + 真实自替换/模拟收口双口径）。
 *  @details XUpgrade_applyNow 受理后引擎内完成 staged 指针切换（Applying→
 *           BootConfirming 落盘）。落位分两路：
 *           ①真实升级——staged 负载含与运行中 exe 同名的条目
 *           （versions/app/<ver>/bin/<自身文件名>）：自身改名 .old（Windows
 *           允许改名运行中的 exe、不允许覆盖）→ 复制新 exe 到原路径。调用
 *           方随后分离拉起「原路径」=真·新版本二进制，编译期版本与激活
 *           指针天然一致；残留 .old 由新实例启动时清走。
 *           ②模拟口径（旧 fake 包无 exe 条目，如 1.2.8/1.2.9 演示包）——
 *           apply 桩直调幂等收口，仅指针切换，不替换文件。
 *  @return 落位成功（指针已指向新版本）返回 true。 */
static bool upd_applyAndSwitch(void)
{
    char root[1024];
    char exePath[XSYSTEM_EXECUTABLE_PATH_MAX];
    char stagedExe[1280];
    const char* selfName;
    size_t selfLen;
    size_t exeLen;
    if (!XUpgrade_applyNow()) return false;
    if (!upd_demoRoot(root, sizeof(root))) return false;
    if (!XSystem_executableFilePath(exePath, sizeof(exePath))) return false;
    exeLen = strlen(exePath);
    selfName = exePath + exeLen; /* 取自身文件名 */
    while (selfName > exePath && *(selfName - 1) != '/' &&
           *(selfName - 1) != '\\')
        --selfName;
    selfLen = exeLen - (size_t)(selfName - exePath);
    /* staged 同名 exe 条目存在 → 真实自替换；否则模拟口径。 */
    if (snprintf(stagedExe, sizeof(stagedExe),
                 "%s/versions/app/%s/bin/%.*s", root, s_upd.m_lastVersion,
                 (int)selfLen, selfName) < (int)sizeof(stagedExe)) {
        XString* pProbe = XString_create_utf8(stagedExe);
        bool realPayload = pProbe && XFileInfo_exists_static(pProbe);
        if (pProbe) XClassDelete(pProbe);
        if (realPayload) {
            char oldPath[1280];
            XString* p1;
            XString* p2;
            if (snprintf(oldPath, sizeof(oldPath), "%s.old", exePath) >=
                (int)sizeof(oldPath))
                return false;
            p1 = XString_create_utf8(oldPath);
            if (p1) { XFile_remove_static(p1); XClassDelete(p1); } /* 清残留 */
            p1 = XString_create_utf8(exePath);
            p2 = XString_create_utf8(oldPath);
            if (!p1 || !p2 || !XFile_rename_static(p1, p2)) {
                if (p1) XClassDelete(p1);
                if (p2) XClassDelete(p2);
                return false; /* 改名失败（盘/权限）：不替换，走失败上报 */
            }
            XClassDelete(p1);
            XClassDelete(p2);
            p1 = XString_create_utf8(stagedExe);
            p2 = XString_create_utf8(exePath);
            if (!p1 || !p2 || !XFile_copy_static(p1, p2)) {
                /* 复制失败：回滚改名，保住旧 exe 可继续运行 */
                if (p1) XClassDelete(p1);
                if (p2) XClassDelete(p2);
                p1 = XString_create_utf8(oldPath);
                p2 = XString_create_utf8(exePath);
                if (p1 && p2) (void)XFile_rename_static(p1, p2);
                if (p1) XClassDelete(p1);
                if (p2) XClassDelete(p2);
                return false;
            }
            XClassDelete(p1);
            XClassDelete(p2);
            return true; /* 新 exe 已就位：调用方拉起原路径即真·新版本 */
        }
    }
    {
        /* 模拟口径：apply 桩直调（fake 包无 exe 条目，仅指针切换）。 */
        char* fakeArgv[4];
        int rc;
        fakeArgv[0] = (char*)"XGuiWindowDemo";
        fakeArgv[1] = (char*)"--xupgrade-apply";
        fakeArgv[2] = (char*)"--root";
        fakeArgv[3] = root;
        rc = XUpgradeApplyStub_main(4, fakeArgv);
        return rc == 0;
    }
}

/** @brief 安装所选远程版本（schema 2 目录；显式降级/重装，rollback_to 窗口
 *         仍约束降级深度）。 */
static void upd_installSelSlot(XObject* receiver, XVarList* args)
{
    XString* text;
    const char* utf8;
    bool accepted;
    (void)receiver; (void)args;
    if (!s_upd.m_ready) return;
    if (!s_upd.m_remoteCombo) return;
    if (!upd_ensureStarted()) return;
    text = XComboBox_currentText(s_upd.m_remoteCombo);
    if (!text) return;
    utf8 = XString_toUtf8(text);
    if (!utf8 || utf8[0] == '\0') { XClassDelete(text); return; }
    accepted = XUpgrade_installVersion(utf8);
    XClassDelete(text);
    upd_report(accepted ? "\xE5\xAE\x89\xE8\xA3\x85\xE5\xB7\xB2\xE5\x8F\x97"
                          "\xE7\x90\x86\xEF\xBC\x9A\xE5\xAE\x8C\xE6\x88\x90"
                          "\xE5\x90\x8E\xE5\x9C\xA8\xE3\x80\x8C\xE5\x8D\x87"
                          "\xE7\xBA\xA7\xE5\x8C\x85\xE5\xB7\xB2\xE5\xB0\xB1"
                          "\xE7\xBB\xAA\xE3\x80\x8D\xE5\xBC\xB9\xE7\xAA\x97"
                          "\xE7\xA1\xAE\xE8\xAE\xA4\xE9\x87\x8D\xE5\x90\xAF"
                        : "\xE5\xAE\x89\xE8\xA3\x85\xE6\x9C\xAA\xE5\x8F\x97"
                          "\xE7\x90\x86\xEF\xBC\x88\xE7\x89\x88\xE6\x9C\xAC"
                          "\xE4\xB8\x8D\xE5\x9C\xA8\xE7\x9B\xAE\xE5\xBD\x95"
                          "\xE6\x88\x96\xE5\xBC\x95\xE6\x93\x8E\xE5\xBF\x99"
                          "\xEF\xBC\x89",
               NULL); /* 安装已受理：完成后在「升级包已就绪」弹窗确认重启 / 安装未受理（版本不在目录或引擎忙） */
}

/* ==================== 装配 ==================== */

#if XLAYOUT_ON && XLAYOUT_BOX_ON
/** @brief 条目包裹盒：把控件装入单条目水平盒（四边边距=原手写排布的
 *  右侧间距与行内错位下沉：条目落 x=0、顶边距即下沉量、右边距即与后
 *  随条目的间距），再按序挂入父布局器。
 *  @details 盒方向取水平：盒式布局器尺寸协商对垂直盒把左右边距计入
 *           主轴（XBoxLayout_calcMetrics），水平包裹盒的宽提示才等于
 *           「控件宽+右边距」，行内水平间距不占行高。盒对象为页内静
 *           态池的值成员（XBoxLayout_addLayout 借用不取所有权，须随页
 *           常驻）；池满静默丢弃。
 *  @return 包裹盒借用指针（adapt 窄档分档搬移的句柄）；父/控件为空或
 *          池满返回 NULL（调用方据此跳过搬移）。 */
static XBoxLayout* upd_wrapAdd(XBoxLayout* parent, XWidget* w,
                               int l, int t, int r, int b)
{
    XBoxLayout* box;
    if (!parent || !w) return NULL;
    if (s_upd.m_wrapCount >= (int)(sizeof(s_upd.m_wrapPool) /
                                   sizeof(s_upd.m_wrapPool[0])))
        return NULL;
    box = &s_upd.m_wrapPool[s_upd.m_wrapCount++];
    XBoxLayout_init(box, XBoxLayoutDirection_LeftToRight);
    XLayout_setContentsMargins((XLayout*)box, l, t, r, b);
    XBoxLayout_addWidget(box, w);
    XBoxLayout_addLayout(parent, (XLayout*)box);
    return box;
}
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */

XWidget* demo_page_update_build(XWidget* parent,
                                DemoPageStatusFn status, void* user)
{
    static const char* const kPhaseCaps[4] = {
        "\xE4\xB8\x8B\xE8\xBD\xBD", /* 下载 */
        "\xE6\xA0\xA1\xE9\xAA\x8C", /* 校验 */
        "\xE8\xA7\xA3\xE5\x8C\x85", /* 解包 */
        "\xE6\xBF\x80\xE6\xB4\xBB"  /* 激活 */
    };
    int i;
    /* demo 单实例：重复 build 前先收口上一实例的非控件资源（定时器/
     * 互斥/引擎无父，不随父链级联析构），再清空登记表。 */
    demo_page_update_shutdown();
    memset(&s_upd, 0, sizeof(s_upd));
    s_upd.m_status = status;
    s_upd.m_user = user;
    if (!parent) return (XWidget*)0;
    s_upd.m_root = XWidget_create(parent, 0);
    if (!s_upd.m_root) return (XWidget*)0;

    /* ---- 标题 ---- */
    s_upd.m_titleLabel = XLabel_create(s_upd.m_root, 0);
    if (!s_upd.m_titleLabel) return s_upd.m_root;
    XLabel_setText_2(s_upd.m_titleLabel,
                     "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0");
                     /* 软件更新 */
    XLabel_setTextPixelSize(s_upd.m_titleLabel, 15);
    /* 原手写 (12,8,300,24)→固定尺寸复刻，原点交标题行布局器。 */
    XWidget_setFixedWidth((XWidget*)s_upd.m_titleLabel, 300);
    XWidget_setFixedHeight((XWidget*)s_upd.m_titleLabel, 24);
    XWidget_show((XWidget*)s_upd.m_titleLabel);

    /* ---- 程序版本（标题行右侧；XCoreApplication applicationName/
     *     applicationVersion。版本文本由 upd_refreshVersions 末尾统一
     *     回填——applicationVersion 跟随引擎激活版本同步，与「当前版本」
     *     两处口径一致（真实产品升级换二进制即换编译期版本，demo 由引擎
     *     指针回写模拟）。应用名称仅作展示：升级引擎协议面无名称概念） ---- */
    s_upd.m_progVerLabel = XLabel_create(s_upd.m_root, 0);
    if (s_upd.m_progVerLabel) {
        XLabel_setTextPixelSize(s_upd.m_progVerLabel, 12);
        /* 原手写 (320,10,332,20)→adapt 右锚定 (rootW-344,10)：固定尺寸
         * 复刻，原点交标题行布局器（行弹簧推右缘，行高 24 居中 → y=10）。 */
        XWidget_setFixedWidth((XWidget*)s_upd.m_progVerLabel, 332);
        XWidget_setFixedHeight((XWidget*)s_upd.m_progVerLabel, 20);
        XWidget_show((XWidget*)s_upd.m_progVerLabel);
    }

    /* ---- 真实自替换残留清理：见 upd_cleanupOldImage（懒启动再试一次） ---- */
    upd_cleanupOldImage();

    /* ---- 当前组件版本列表（app/fontres；目录后端只读指针文件） ---- */
    s_upd.m_capVersion = XLabel_create(s_upd.m_root, 0);
    if (s_upd.m_capVersion) {
        XLabel_setText_2(s_upd.m_capVersion,
                         "\xE5\xBD\x93\xE5\x89\x8D\xE7\x89\x88\xE6\x9C\xAC");
                         /* 当前版本 */
        /* 原手写 (12,44,96,22)→固定尺寸复刻，原点交版本行标题列。 */
        XWidget_setFixedWidth((XWidget*)s_upd.m_capVersion, 96);
        XWidget_setFixedHeight((XWidget*)s_upd.m_capVersion, 22);
        XWidget_show((XWidget*)s_upd.m_capVersion);
    }
    s_upd.m_versionLabel = XLabel_create(s_upd.m_root, 0);
    if (!s_upd.m_versionLabel) return s_upd.m_root;
    XLabel_setTextPixelSize(s_upd.m_versionLabel, 13);
    XLabel_setAlignment(s_upd.m_versionLabel,
                        XAlignment_Left | XAlignment_Top);
    /* 原手写 (116,42,640,40)→行内伸展铺宽（adapt 同语义：宽随根收放）。
     * [拆单行 2026-10-27 视觉走查 P1] 版本两行拆单行标签（h_04 滚动内容
     * 先例）：XLabel 多行动态文本渲染残影（2026-10-07 立案）在 app/
     * fontres 两行上表现为行贴行叠印，拆单行+盒距根治观感。 */
    XWidget_setFixedHeight((XWidget*)s_upd.m_versionLabel, 20);
    XWidget_show((XWidget*)s_upd.m_versionLabel);
    s_upd.m_versionFontLabel = XLabel_create(s_upd.m_root, 0);
    if (!s_upd.m_versionFontLabel) return s_upd.m_root;
    XLabel_setTextPixelSize(s_upd.m_versionFontLabel, 13);
    XLabel_setAlignment(s_upd.m_versionFontLabel,
                        XAlignment_Left | XAlignment_Top);
    XWidget_setFixedHeight((XWidget*)s_upd.m_versionFontLabel, 20);
    XWidget_show((XWidget*)s_upd.m_versionFontLabel);

    /* ---- 策略显示（最近策略/灰度 + 窗口/宽限期回显） ---- */
    s_upd.m_capPolicy = XLabel_create(s_upd.m_root, 0);
    if (s_upd.m_capPolicy) {
        XLabel_setText_2(s_upd.m_capPolicy,
                         "\xE7\xAD\x96\xE7\x95\xA5"); /* 策略 */
        /* 原手写 (12,92,96,22)→固定尺寸复刻，原点交策略行标题列。 */
        XWidget_setFixedWidth((XWidget*)s_upd.m_capPolicy, 96);
        XWidget_setFixedHeight((XWidget*)s_upd.m_capPolicy, 22);
        XWidget_show((XWidget*)s_upd.m_capPolicy);
    }
    /* [拆单行 2026-10-27 视觉走查 P1] 策略三行拆单行标签（同版本行）：
     * 「策略：未检查」与「灰度：未判定」行距小于字高叠印（横竖屏截图
     * 实证），拆单行+盒距根治。 */
    s_upd.m_policyLabel = XLabel_create(s_upd.m_root, 0);
    if (!s_upd.m_policyLabel) return s_upd.m_root;
    XLabel_setTextPixelSize(s_upd.m_policyLabel, 13);
    XLabel_setAlignment(s_upd.m_policyLabel,
                        XAlignment_Left | XAlignment_Top);
    XWidget_setFixedHeight((XWidget*)s_upd.m_policyLabel, 20);
    XWidget_show((XWidget*)s_upd.m_policyLabel);
    s_upd.m_policyGrayLabel = XLabel_create(s_upd.m_root, 0);
    if (!s_upd.m_policyGrayLabel) return s_upd.m_root;
    XLabel_setTextPixelSize(s_upd.m_policyGrayLabel, 13);
    XLabel_setAlignment(s_upd.m_policyGrayLabel,
                        XAlignment_Left | XAlignment_Top);
    XWidget_setFixedHeight((XWidget*)s_upd.m_policyGrayLabel, 20);
    XWidget_show((XWidget*)s_upd.m_policyGrayLabel);
    s_upd.m_policyWindowLabel = XLabel_create(s_upd.m_root, 0);
    if (!s_upd.m_policyWindowLabel) return s_upd.m_root;
    XLabel_setTextPixelSize(s_upd.m_policyWindowLabel, 13);
    XLabel_setAlignment(s_upd.m_policyWindowLabel,
                        XAlignment_Left | XAlignment_Top);
    XWidget_setFixedHeight((XWidget*)s_upd.m_policyWindowLabel, 20);
    XWidget_show((XWidget*)s_upd.m_policyWindowLabel);

    /* ---- 服务器地址行（引擎启动前可编辑，{channel} 占位） ---- */
    s_upd.m_urlCap = XLabel_create(s_upd.m_root, 0);
    if (s_upd.m_urlCap) {
        XLabel_setText_2(s_upd.m_urlCap,
                         "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8"); /* 服务器 */
        /* 原手写 (12,UPD_URL_Y+3,96,22)→固定尺寸复刻（顶边距 +3 下沉
         * 交包裹盒），原点交服务器行标题列。 */
        /* [列对齐 2026-10-27 视觉走查 P3-H] 标题列 96→76 与四段进度条
         * 行（phaseCap 56+边距）对齐：值列左缘参差（进度条 x≈88 vs
         * 服务器框 x≈120）收口。 */
        XWidget_setFixedWidth((XWidget*)s_upd.m_urlCap, 76);
        XWidget_setFixedHeight((XWidget*)s_upd.m_urlCap, 22);
        XWidget_show((XWidget*)s_upd.m_urlCap);
    }
    s_upd.m_urlEdit = XLineEdit_create(s_upd.m_root, 0);
    if (!s_upd.m_urlEdit) return s_upd.m_root;
    {
        char urlDefault[512];
        const char* initial = upd_serverUrl(urlDefault, sizeof(urlDefault));
        if (initial) {
            XLineEdit_setText(s_upd.m_urlEdit, initial);
        }
    }
    /* 原手写 (116,UPD_URL_Y,640,24)→行内伸展铺宽（宽随根收放），行高
     * 24 固定，原点交服务器行布局器。 */
    XWidget_setFixedHeight((XWidget*)s_upd.m_urlEdit, 24);
    XWidget_show((XWidget*)s_upd.m_urlEdit);

    /* ---- 四段进度条（下载/校验/解包/激活；Progress 事件驱动） ---- */
    for (i = 0; i < 4; ++i) {
        s_upd.m_phaseCap[i] = XLabel_create(s_upd.m_root, 0);
        if (s_upd.m_phaseCap[i]) {
            XLabel_setText_2(s_upd.m_phaseCap[i], kPhaseCaps[i]);
            /* 原手写 (12,y+3,56,20)→固定尺寸复刻（顶边距 +3 下沉交包裹
             * 盒），原点交各段行盒标题列。 */
            XWidget_setFixedWidth((XWidget*)s_upd.m_phaseCap[i], 56);
            XWidget_setFixedHeight((XWidget*)s_upd.m_phaseCap[i], 20);
            XWidget_show((XWidget*)s_upd.m_phaseCap[i]);
        }
        s_upd.m_phaseBar[i] = XProgressBar_create(s_upd.m_root, 0);
        if (s_upd.m_phaseBar[i]) {
            XProgressBar_setRange(s_upd.m_phaseBar[i], 0, 100);
            XProgressBar_setValue(s_upd.m_phaseBar[i], 0);
            XProgressBar_setTextVisible(s_upd.m_phaseBar[i], true);
            /* 原手写 (76,y,680,22)→行内伸展铺宽（adapt 同语义：宽随根
             * 收放），行高 22 固定，原点交各段行盒布局器。 */
            XWidget_setFixedHeight((XWidget*)s_upd.m_phaseBar[i], 22);
            XWidget_show((XWidget*)s_upd.m_phaseBar[i]);
        }
    }

    /* ---- 决策+安装单行（检查/跳过/远程版本/安装所选版本同排；2026-10-08
     *      用户裁定合并——原两行排布视觉冗余） ---- */
    s_upd.m_checkBtn = XPushButton_create(s_upd.m_root, 0);
    if (s_upd.m_checkBtn) {
        XPushButton_setText_2(s_upd.m_checkBtn,
                              "\xE6\xA3\x80\xE6\x9F\xA5\xE6\x9B\xB4\xE6"
                              "\x96\xB0"); /* 检查更新 */
        /* 原手写 (12,UPD_BTN_Y,104,30)→固定尺寸复刻，原点交决策行布局器。 */
        XWidget_setFixedWidth((XWidget*)s_upd.m_checkBtn, 104);
        XWidget_setFixedHeight((XWidget*)s_upd.m_checkBtn, 30);
        XObject_connect_2((XObject*)s_upd.m_checkBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          upd_checkSlot);
        XWidget_show((XWidget*)s_upd.m_checkBtn);
    }
    s_upd.m_skipBtn = XPushButton_create(s_upd.m_root, 0);
    if (s_upd.m_skipBtn) {
        XPushButton_setText_2(s_upd.m_skipBtn,
                              "\xE8\xB7\xB3\xE8\xBF\x87\xE6\xAD\xA4\xE7"
                              "\x89\x88\xE6\x9C\xAC"); /* 跳过此版本 */
        /* 原手写 (124,UPD_BTN_Y,96,30)→固定尺寸复刻，原点交决策行布局器
         * （与检查钮 8px 间距交包裹盒右边距）。 */
        XWidget_setFixedWidth((XWidget*)s_upd.m_skipBtn, 96);
        XWidget_setFixedHeight((XWidget*)s_upd.m_skipBtn, 30);
        XObject_connect_2((XObject*)s_upd.m_skipBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          upd_skipSlot);
        XWidget_setEnabled((XWidget*)s_upd.m_skipBtn, false);
        XWidget_show((XWidget*)s_upd.m_skipBtn);
    }

    /* ---- 多版本选择（schema 2 目录；含降级安装）——接在同排右侧 ---- */
    s_upd.m_remoteCap = XLabel_create(s_upd.m_root, 0);
    if (s_upd.m_remoteCap) {
        XLabel_setText_2(s_upd.m_remoteCap,
                         "\xE8\xBF\x9C\xE7\xA8\x8B\xE7\x89\x88\xE6\x9C"
                         "\xAC"); /* 远程版本 */
        /* 原手写 (230,UPD_BTN_Y+5,64,20)→固定尺寸复刻（顶边距 +5 下沉
         * 交包裹盒），原点交决策行布局器。 */
        XWidget_setFixedWidth((XWidget*)s_upd.m_remoteCap, 64);
        XWidget_setFixedHeight((XWidget*)s_upd.m_remoteCap, 20);
        XWidget_show((XWidget*)s_upd.m_remoteCap);
    }
    s_upd.m_remoteCombo = XComboBox_create(s_upd.m_root, 0);
    if (!s_upd.m_remoteCombo) return s_upd.m_root;
    /* 原手写 (300,UPD_BTN_Y+2,268,26)→固定尺寸复刻（顶边距 +2 下沉交
     * 包裹盒），原点交决策行布局器。 */
    XWidget_setFixedWidth((XWidget*)s_upd.m_remoteCombo, 268);
    XWidget_setFixedHeight((XWidget*)s_upd.m_remoteCombo, 26);
    XWidget_show((XWidget*)s_upd.m_remoteCombo);
    s_upd.m_installBtn = XPushButton_create(s_upd.m_root, 0);
    if (s_upd.m_installBtn) {
        XPushButton_setText_2(s_upd.m_installBtn,
                              "\xE5\xAE\x89\xE8\xA3\x85\xE6\x89\x80\xE9\x80"
                              "\x89\xE7\x89\x88\xE6\x9C\xAC"); /* 安装所选版本 */
        /* 原手写 (578,UPD_BTN_Y,128,30)→固定尺寸复刻，原点交决策行布局器
         * （行尾余量留主轴末尾，同原固定排布）。 */
        XWidget_setFixedWidth((XWidget*)s_upd.m_installBtn, 128);
        XWidget_setFixedHeight((XWidget*)s_upd.m_installBtn, 30);
        XObject_connect_2((XObject*)s_upd.m_installBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          upd_installSelSlot);
        XWidget_setEnabled((XWidget*)s_upd.m_installBtn, false);
        XWidget_show((XWidget*)s_upd.m_installBtn);
    }

    /* ---- 提示 + 状态行（adapt 钉底；口径固定文案） ---- */
    s_upd.m_hintLabel = XLabel_create(s_upd.m_root, 0);
    if (s_upd.m_hintLabel) {
        XLabel_setTextPixelSize(s_upd.m_hintLabel, 12);
    XLabel_setText_2(s_upd.m_hintLabel, UPD_HINT_WIDE);
                     /* 服务器地址在输入行设置（首次启动后锁定）；引擎懒启动，工作区在系统临时目录 */
        /* 原手写 (12,414,752,20)→根盒铺宽（宽随根收放）+ 垂直弹簧钉底，
         * 行高 20 固定（底边距 2 复刻与状态行行距）。 */
        XWidget_setFixedHeight((XWidget*)s_upd.m_hintLabel, 20);
        XWidget_show((XWidget*)s_upd.m_hintLabel);
    }
    s_upd.m_statusLabel = XLabel_create(s_upd.m_root, 0);
    if (!s_upd.m_statusLabel) return s_upd.m_root;
    XLabel_setText_2(s_upd.m_statusLabel,
                     "\xE5\xB0\xB1\xE7\xBB\xAA"); /* 就绪 */
    /* 原手写 (12,436,752,22)→根盒末条目铺宽贴底（adapt rootH-22 同语义，
     * 根盒底边距 0），行高 22 固定。 */
    XWidget_setFixedHeight((XWidget*)s_upd.m_statusLabel, 22);
    XWidget_show((XWidget*)s_upd.m_statusLabel);

#if XLAYOUT_ON && XLAYOUT_BOX_ON
    /* ---- 页内布局容器化（2026-10-17）：页根挂垂直盒布局器统一分配
     * 几何（原页内 setGeometry 手写排布全部退场）。观感对齐改造前
     * 截图（776x458 内容区设计稿）：根边距 (12,8,12,0) 复刻原手写
     * 原点 (12,8) 与状态行贴底；行距用水平行盒底边距近似（advanced
     * 页同款定式）：标题行底 10、版本行底 8、策略行底 4、服务器行底
     * 12、进度条行距 8（原 30 行距-22 条高）；进度条列与决策行间距
     * 16、提示/状态行距 2 用固定高空隙条复刻（垂直盒的底边距按
     * XBoxLayout_calcMetrics 计入宽提示，不用作行距）；信息标题列
     * （96/56 宽）+8px 包裹盒右边距复刻信息列 x=116/进度条 x=76；标
     * 题行水平弹簧把程序版本推右缘（复刻 adapt 右锚定 rootW-344）；
     * 根盒垂直弹簧吃掉多余高度把提示/状态两行钉底（复刻 adapt
     * rootH-44 / rootH-22）；决策行水平间距 8/10/6/10 与行内错位下沉
     * （远程版本 +5、下拉 +2）经水平包裹盒边距复刻。挂页根即激活
     * （attachWidget→activate），此后随 XStackedLayout 重贴根几何经
     * resize→XLayout_activate 自动重排。 ---- */
    XBoxLayout_init(&s_upd.m_rootLayout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_upd.m_rootLayout, 12, 8, 12, 0);
    /* 标题行：标题 300x24 + 弹簧 + 程序版本 332x20（行高 24 居中下沉
     * 2px 复刻原 y=10）。 */
    XBoxLayout_init(&s_upd.m_titleRow, XBoxLayoutDirection_LeftToRight);
    XBoxLayout_addWidget(&s_upd.m_titleRow, (XWidget*)s_upd.m_titleLabel);
    s_upd.m_titleSpring = XWidget_create(s_upd.m_root, 0);
    if (s_upd.m_titleSpring) {
        XWidget_show(s_upd.m_titleSpring); /* 隐藏件会被布局按空条目跳过。 */
        XBoxLayout_addWidget(&s_upd.m_titleRow, s_upd.m_titleSpring);
        XBoxLayout_setStretch(&s_upd.m_titleRow, 1, 1);
    }
    XBoxLayout_addWidget(&s_upd.m_titleRow, (XWidget*)s_upd.m_progVerLabel);
    XLayout_setContentsMargins((XLayout*)&s_upd.m_titleRow, 0, 0, 0, 10);
    /* 当前版本行：标题列（22+顶 2+底 16=行高 40：交叉轴装满行盒才不
     * 触发居中，顶边距 +2 即复刻原 y=+2 顶对齐列表首行）+ 列表伸展。 */
    XBoxLayout_init(&s_upd.m_versionRow, XBoxLayoutDirection_LeftToRight);
    upd_wrapAdd(&s_upd.m_versionRow, (XWidget*)s_upd.m_capVersion,
                0, 2, 8, 16);
    XBoxLayout_addWidget(&s_upd.m_versionRow, (XWidget*)s_upd.m_versionLabel);
    XBoxLayout_setStretch(&s_upd.m_versionRow, 1, 1);
    XBoxLayout_addSpacing(&s_upd.m_versionRow, 4);
    XBoxLayout_addWidget(&s_upd.m_versionRow,
                         (XWidget*)s_upd.m_versionFontLabel);
    XLayout_setContentsMargins((XLayout*)&s_upd.m_versionRow, 0, 0, 0, 8);
    /* 策略行：同版本行定式（标题列 22+2+30=行高 54=策略三行）。 */
    XBoxLayout_init(&s_upd.m_policyRow, XBoxLayoutDirection_LeftToRight);
    upd_wrapAdd(&s_upd.m_policyRow, (XWidget*)s_upd.m_capPolicy,
                0, 2, 8, 30);
    {
        /* 文本列：三单行纵排（成员存储布局器），行距 4。 */
        XBoxLayout_init(&s_upd.m_policyTextCol,
                        XBoxLayoutDirection_TopToBottom);
        XLayout_setSpacing((XLayout*)&s_upd.m_policyTextCol, 4);
        XBoxLayout_addWidget(&s_upd.m_policyTextCol,
                             (XWidget*)s_upd.m_policyLabel);
        XBoxLayout_addWidget(&s_upd.m_policyTextCol,
                             (XWidget*)s_upd.m_policyGrayLabel);
        XBoxLayout_addWidget(&s_upd.m_policyTextCol,
                             (XWidget*)s_upd.m_policyWindowLabel);
        XBoxLayout_addLayout(&s_upd.m_policyRow,
                             (XLayout*)&s_upd.m_policyTextCol);
    }
    XBoxLayout_setStretch(&s_upd.m_policyRow, 1, 1);
    XLayout_setContentsMargins((XLayout*)&s_upd.m_policyRow, 0, 0, 0, 4);
    /* 服务器行：标题列（顶边距 +3 复刻原 UPD_URL_Y+3 下沉）+ 输入行伸展。 */
    XBoxLayout_init(&s_upd.m_urlRow, XBoxLayoutDirection_LeftToRight);
    upd_wrapAdd(&s_upd.m_urlRow, (XWidget*)s_upd.m_urlCap, 0, 3, 8, 0);
    XBoxLayout_addWidget(&s_upd.m_urlRow, (XWidget*)s_upd.m_urlEdit);
    XBoxLayout_setStretch(&s_upd.m_urlRow, 1, 1);
    XLayout_setContentsMargins((XLayout*)&s_upd.m_urlRow, 0, 0, 0, 12);
    /* 四段进度条列：各段行盒=标题列（顶边距 +3 同原 y+3 下沉）+ 条伸展；
     * 行距复刻原 30（标题列下沉后 23 高 + 行盒底 7）。 */
    XBoxLayout_init(&s_upd.m_barsCol, XBoxLayoutDirection_TopToBottom);
    for (i = 0; i < 4; ++i) {
        XBoxLayout_init(&s_upd.m_barRows[i], XBoxLayoutDirection_LeftToRight);
        upd_wrapAdd(&s_upd.m_barRows[i], (XWidget*)s_upd.m_phaseCap[i],
                    0, 3, 8, 0);
        XBoxLayout_addWidget(&s_upd.m_barRows[i],
                             (XWidget*)s_upd.m_phaseBar[i]);
        XBoxLayout_setStretch(&s_upd.m_barRows[i], 1, 1);
        XLayout_setContentsMargins((XLayout*)&s_upd.m_barRows[i],
                                   0, 0, 0, i < 3 ? 7 : 0);
        XBoxLayout_addLayout(&s_upd.m_barsCol, (XLayout*)&s_upd.m_barRows[i]);
    }
    /* 决策+安装单行：水平间距 8/10/6/10 与行内错位下沉经包裹盒边距
     * 复刻（远程版本 20+5+5=30、下拉 26+2+2=30 装满行高防居中）；
     * 安装钮后余量留主轴末尾（同原固定排布不右锚定）。安装钮也入零边
     * 距包裹盒（几何不变）：窄档分档要把右半段三个条目整体搬去续行盒，
     * 子布局对象即布局条目，持句柄即可 takeAt/addLayout 零堆分配搬移。 */
    XBoxLayout_init(&s_upd.m_buttonRow, XBoxLayoutDirection_LeftToRight);
    upd_wrapAdd(&s_upd.m_buttonRow, (XWidget*)s_upd.m_checkBtn, 0, 0, 8, 0);
    upd_wrapAdd(&s_upd.m_buttonRow, (XWidget*)s_upd.m_skipBtn, 0, 0, 10, 0);
    s_upd.m_remoteWrap = upd_wrapAdd(&s_upd.m_buttonRow,
                                     (XWidget*)s_upd.m_remoteCap,
                                     0, 5, 6, 5);
    s_upd.m_comboWrap = upd_wrapAdd(&s_upd.m_buttonRow,
                                    (XWidget*)s_upd.m_remoteCombo,
                                    0, 2, 10, 2);
    s_upd.m_installWrap = upd_wrapAdd(&s_upd.m_buttonRow,
                                      (XWidget*)s_upd.m_installBtn,
                                      0, 0, 0, 0);
    /* 决策续行盒：紧随决策行同位预挂根盒——空盒无条目不占尺寸也不参与
     * 行距，宽档（含桌面 1000x700）观感零变化；窄档由 adapt 注入右半段
     * 成「检查/跳过」+「远程版本/下拉/安装」双行，避免单行 718 逻辑宽
     * 在竖屏溢出。 */
    XBoxLayout_init(&s_upd.m_buttonRow2, XBoxLayoutDirection_LeftToRight);
    /* 根装配：以上行/列按序入根盒；进度条列→决策行间 16px、提示→状态
     * 行间 2px 用固定高空隙条复刻；垂直弹簧吃掉全部多余高度，提示/状
     * 态两行钉底。 */
    XBoxLayout_addLayout(&s_upd.m_rootLayout, (XLayout*)&s_upd.m_titleRow);
    XBoxLayout_addLayout(&s_upd.m_rootLayout, (XLayout*)&s_upd.m_versionRow);
    XBoxLayout_addLayout(&s_upd.m_rootLayout, (XLayout*)&s_upd.m_policyRow);
    XBoxLayout_addLayout(&s_upd.m_rootLayout, (XLayout*)&s_upd.m_urlRow);
    XBoxLayout_addLayout(&s_upd.m_rootLayout, (XLayout*)&s_upd.m_barsCol);
    s_upd.m_barsGap = XWidget_create(s_upd.m_root, 0);
    if (s_upd.m_barsGap) {
        XWidget_show(s_upd.m_barsGap); /* 隐藏件会被布局按空条目跳过。 */
        XWidget_setFixedHeight(s_upd.m_barsGap, 16);
        XBoxLayout_addWidget(&s_upd.m_rootLayout, s_upd.m_barsGap);
    }
    XBoxLayout_addLayout(&s_upd.m_rootLayout, (XLayout*)&s_upd.m_buttonRow);
    XBoxLayout_addLayout(&s_upd.m_rootLayout, (XLayout*)&s_upd.m_buttonRow2);
    s_upd.m_vSpring = XWidget_create(s_upd.m_root, 0);
    if (s_upd.m_vSpring) {
        XWidget_show(s_upd.m_vSpring); /* 隐藏件会被布局按空条目跳过。 */
        XBoxLayout_addWidget(&s_upd.m_rootLayout, s_upd.m_vSpring);
        /* 弹簧因子下标随续行盒入列顺延（决策行 6、续行 7、弹簧 8）。 */
        XBoxLayout_setStretch(&s_upd.m_rootLayout, 8, 1);
    }
    XBoxLayout_addWidget(&s_upd.m_rootLayout, (XWidget*)s_upd.m_hintLabel);
    s_upd.m_hintGap = XWidget_create(s_upd.m_root, 0);
    if (s_upd.m_hintGap) {
        XWidget_show(s_upd.m_hintGap); /* 隐藏件会被布局按空条目跳过。 */
        XWidget_setFixedHeight(s_upd.m_hintGap, 2);
        XBoxLayout_addWidget(&s_upd.m_rootLayout, s_upd.m_hintGap);
    }
    XBoxLayout_addWidget(&s_upd.m_rootLayout, (XWidget*)s_upd.m_statusLabel);
    XWidget_setLayout(s_upd.m_root, (XLayout*)&s_upd.m_rootLayout);
#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */

    /* ---- 跨线程事件队列 + UI 泵定时器（懒启动成功才 start） ---- */
    s_upd.m_queueMutex = XMutex_create(XLock_NonRecursive);
    s_upd.m_pumpTimer = XTimer_create();
    if (s_upd.m_pumpTimer) {
        XTimer_setTimerCallback(s_upd.m_pumpTimer, upd_pumpCb);
        XTimer_setUserData(s_upd.m_pumpTimer, NULL);
    }

    /* ---- 版本列表与策略区首轮回填（只读指针文件，零引擎调用） ---- */
    upd_refreshVersions();
    upd_refreshPolicy();

    s_upd.m_ready = true;
    return s_upd.m_root;
}

/* ==================== 自适应重排（xgui_demo_pages.h 契约） ============ */

#if XLAYOUT_ON && XLAYOUT_BOX_ON

/** @brief 窄档触控行高（逻辑像素）：手机触控目标建议下限；桌面 30px 单行
 *         口径只在宽档保持。 */
#define UPD_COMPACT_ROW_H 40

/** @brief 把包裹盒从行盒 from 搬到行盒 to。
 *  @details takeAt 只把条目摘出布局并解除父子关系（不析构对象；包裹盒
 *           为页内静态池值成员），addLayout 重新登记进目标行——搬移零堆
 *           分配、可逆；包裹盒不在 from 内（已就位/装配期未入池）时零操作。 */
static void upd_wrapMove(XBoxLayout* from, XBoxLayout* to, XBoxLayout* wrap)
{
    int idx;
    if (!from || !to || !wrap || from == to) return;
    idx = XLayout_indexOfItem((XLayout*)from, (XLayoutItem*)wrap);
    if (idx < 0) return;
    if (XLayout_takeAt_base((XLayout*)from, idx))
        XBoxLayout_addLayout(to, (XLayout*)wrap);
}

/** @brief 窄/宽档互斥切换（仅分档翻转时调用一次；几何值与 build 复刻的
 *         桌面口径一一对应，来回切换无漂移）。 */
static void upd_applyCompact(bool compact)
{
    if (compact) {
        /* 窄档：决策行右半段让位续行（远程版本→下拉→安装，追加次序即
         * 装配次序），行高统一抬到触控下限，标题列居中下沉随之改写。 */
        upd_wrapMove(&s_upd.m_buttonRow, &s_upd.m_buttonRow2,
                     s_upd.m_remoteWrap);
        upd_wrapMove(&s_upd.m_buttonRow, &s_upd.m_buttonRow2,
                     s_upd.m_comboWrap);
        upd_wrapMove(&s_upd.m_buttonRow, &s_upd.m_buttonRow2,
                     s_upd.m_installWrap);
        if (s_upd.m_checkBtn)
            XWidget_setFixedHeight((XWidget*)s_upd.m_checkBtn,
                                   UPD_COMPACT_ROW_H);
        if (s_upd.m_skipBtn)
            XWidget_setFixedHeight((XWidget*)s_upd.m_skipBtn,
                                   UPD_COMPACT_ROW_H);
        if (s_upd.m_remoteCombo)
            XWidget_setFixedHeight((XWidget*)s_upd.m_remoteCombo,
                                   UPD_COMPACT_ROW_H);
        if (s_upd.m_installBtn)
            XWidget_setFixedHeight((XWidget*)s_upd.m_installBtn,
                                   UPD_COMPACT_ROW_H);
        if (s_upd.m_remoteWrap)
            XLayout_setContentsMargins((XLayout*)s_upd.m_remoteWrap,
                                       0, 10, 6, 10);
        if (s_upd.m_comboWrap)
            XLayout_setContentsMargins((XLayout*)s_upd.m_comboWrap,
                                       0, 0, 10, 0);
        /* 两行决策行间留 8px 行缝（宽档行缝由空续行盒承担=0）。 */
        XLayout_setContentsMargins((XLayout*)&s_upd.m_buttonRow, 0, 0, 0, 8);
        /* 标题行让宽：300+332 固定宽在竖屏必把程序版本标签右缘硬裁——
         * 标题列收窄到信息列口径，程序版本占满余宽（随根宽在 adapt 末尾
         * 重算）。 */
        if (s_upd.m_titleLabel)
            XWidget_setFixedWidth((XWidget*)s_upd.m_titleLabel, 96);
        /* 提示行折两行（窄档文案在分号处手动分行）：单行约 460 逻辑宽竖屏
         * 必溢出且尾部落右下性能悬浮层覆盖区。 */
        if (s_upd.m_hintLabel) {
            XLabel_setText_2(s_upd.m_hintLabel, UPD_HINT_COMPACT);
            XWidget_setFixedHeight((XWidget*)s_upd.m_hintLabel, 34);
        }
    } else {
        /* 宽档：整体还原装配期复刻的桌面口径（值同 build）。 */
        upd_wrapMove(&s_upd.m_buttonRow2, &s_upd.m_buttonRow,
                     s_upd.m_remoteWrap);
        upd_wrapMove(&s_upd.m_buttonRow2, &s_upd.m_buttonRow,
                     s_upd.m_comboWrap);
        upd_wrapMove(&s_upd.m_buttonRow2, &s_upd.m_buttonRow,
                     s_upd.m_installWrap);
        if (s_upd.m_checkBtn)
            XWidget_setFixedHeight((XWidget*)s_upd.m_checkBtn, 30);
        if (s_upd.m_skipBtn)
            XWidget_setFixedHeight((XWidget*)s_upd.m_skipBtn, 30);
        if (s_upd.m_remoteCombo)
            XWidget_setFixedHeight((XWidget*)s_upd.m_remoteCombo, 26);
        if (s_upd.m_installBtn)
            XWidget_setFixedHeight((XWidget*)s_upd.m_installBtn, 30);
        if (s_upd.m_remoteWrap)
            XLayout_setContentsMargins((XLayout*)s_upd.m_remoteWrap,
                                       0, 5, 6, 5);
        if (s_upd.m_comboWrap)
            XLayout_setContentsMargins((XLayout*)s_upd.m_comboWrap,
                                       0, 2, 10, 2);
        XLayout_setContentsMargins((XLayout*)&s_upd.m_buttonRow, 0, 0, 0, 0);
        if (s_upd.m_titleLabel)
            XWidget_setFixedWidth((XWidget*)s_upd.m_titleLabel, 300);
        if (s_upd.m_progVerLabel)
            XWidget_setFixedWidth((XWidget*)s_upd.m_progVerLabel, 332);
        if (s_upd.m_remoteCombo)
            XWidget_setFixedWidth((XWidget*)s_upd.m_remoteCombo, 268);
        if (s_upd.m_hintLabel) {
            XLabel_setText_2(s_upd.m_hintLabel, UPD_HINT_WIDE);
            XWidget_setFixedHeight((XWidget*)s_upd.m_hintLabel, 20);
        }
    }
}

/** @brief 窄档随根宽重算固定宽（每次 adapt 都执行：同档内窗口宽度仍会
 *         变，如旋转/分栏拖拽）。 */
static void upd_adaptCompactWidths(int rootW)
{
    /* 下拉占续行余宽：标题列 64 + 标题距 6 + 安装钮前距 10 + 安装钮 128；
     * 程序版本占标题行余宽：标题列 96。逐项钳 120 下限（极窄窗宁可挤压
     * 也不给负宽/零宽），分档口径本就瞄准 ≥376 的竖屏（400 目标机）。 */
    int comboW = rootW - 24 - 208;
    int progW = rootW - 24 - 96;
    if (comboW < 120) comboW = 120;
    if (progW < 120) progW = 120;
    if (s_upd.m_remoteCombo)
        XWidget_setFixedWidth((XWidget*)s_upd.m_remoteCombo, comboW);
    if (s_upd.m_progVerLabel)
        XWidget_setFixedWidth((XWidget*)s_upd.m_progVerLabel, progW);
}

#endif /* XLAYOUT_ON && XLAYOUT_BOX_ON */

/** @brief 自适应重排（xgui_demo_pages.h 契约）：按页根宽度分档。
 *  @details 2026-10-17 页内布局容器化后常规几何随根盒自动重排，但决策行
 *           「单行装配」的固定宽内容（约 718 逻辑宽）、标题行「300+332」
 *           与固定高 20 的单行提示在窄窗/竖屏必水平溢出或右缘硬裁——本
 *           函数按 UPD_COMPACT_MAX_W 分档：窄档把决策行右半段搬入续行盒、
 *           行高抬到触控下限、提示行折行、标题行让宽；宽档（含桌面
 *           1000x700）与 build 装配完全一致零改动。搬移只在分档翻转时
 *           执行一次，同档内仅重算随根宽的固定宽；主文件
 *           demo_layout_content 调用点保持不动。 */
void demo_page_update_adapt(XWidget* page)
{
#if XLAYOUT_ON && XLAYOUT_BOX_ON
    bool compact;
    int w;
    if (!page || page != s_upd.m_root || !s_upd.m_ready) return;
    w = XWidget_width(page);
    if (w <= 0) return; /* 首帧几何未落：等下次 resize/布局再分档。 */
    compact = w < UPD_COMPACT_MAX_W;
    if (compact != s_upd.m_compact) {
        s_upd.m_compact = compact;
        upd_applyCompact(compact);
    }
    if (compact)
        upd_adaptCompactWidths(w);
#else
    (void)page;
#endif
}

/* ==================== 退出清理（xgui_demo_pages.h 契约） ============== */

void demo_page_update_shutdown(void)
{
    if (!s_upd.m_ready) return; /* 页面未构建：零操作（含裁剪配置）。 */
    /* 顺序：先停泵（UI 侧不再消费）→ 停引擎（join 后台线程，listener
       不再入队）→ 回收队列互斥与定时器。UI 线程调用 deinit 合法（契约
       只禁回调内调用）。 */
    if (s_upd.m_pumpTimer) {
        XTimer_stop_base(s_upd.m_pumpTimer);
        XObject_deleteLater((XObject*)s_upd.m_pumpTimer);
        /* 唯一生产调用方（xgui_window_demo）在事件循环退出（exec 返回）
         * 之后才走到本收口，延迟删除事件此后永不再被投递——此处按接收者
         * 定向冲刷，立即完成回收；循环期的防御性调用（页面重建）下同样
         * 安全：sendPostedEvents 按投递次序处理，本对象在途事件先于删除
         * 事件送达，且只冲刷本对象，不影响其他 deleteLater 的既有时序。 */
        XCoreApplication_sendPostedEvents((XObject*)s_upd.m_pumpTimer,
                                          XEVENT_TYPE_DEFERRED_DELETE);
        s_upd.m_pumpTimer = NULL;
    }
    if (s_upd.m_engineStarted)
        XUpgrade_deinit(); /* 未启动时为空操作（契约）。 */
    s_upd.m_engineStarted = false;
    if (s_upd.m_queueMutex) {
        XMutex_delete(s_upd.m_queueMutex);
        s_upd.m_queueMutex = NULL;
    }
}

/* ==================== autotest ==================== */

int demo_page_update_autotest(XWidget* page)
{
    int failures = 0;
    XUpgradeEvent event;
    s_upd.m_dialogSuppress = true; /* 注入事件会发 ReadyNeedRestart：抑制模态弹窗防自测挂死 */
#define UPD_EXPECT(cond, what) \
    do { if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
         else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } } while (0)
#define UPD_INJECT(type_, state_, phase_, percent_, policy_, gray_, ver_) \
    do { memset(&event, 0, sizeof(event)); \
         event.m_type = (type_); event.m_state = (state_); \
         event.m_phase = (phase_); event.m_percent = (percent_); \
         event.m_policy = (policy_); event.m_gray = (gray_); \
         snprintf(event.m_version, sizeof(event.m_version), "%s", (ver_)); \
         upd_listener(NULL, &event); upd_pumpCb(NULL, NULL); } while (0)

    if (!page || !s_upd.m_ready || page != s_upd.m_root)
        return -1;

    /* ---- 控件登记完整 ---- */
    UPD_EXPECT(s_upd.m_titleLabel && s_upd.m_progVerLabel &&
               s_upd.m_versionLabel &&
               s_upd.m_policyLabel && s_upd.m_phaseBar[0] &&
               s_upd.m_phaseBar[1] && s_upd.m_phaseBar[2] &&
               s_upd.m_phaseBar[3] && s_upd.m_checkBtn &&
               s_upd.m_skipBtn &&
               s_upd.m_hintLabel && s_upd.m_statusLabel &&
               s_upd.m_queueMutex && s_upd.m_pumpTimer,
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE6\x8E\xA7\xE4\xBB\xB6\xE5\x85\xA8\xE9\x83\xA8\xE7\x99\xBB\xE8\xAE\xB0(\xE5\x90\xAB\xE5\x9B\x9B\xE6\xAE\xB5\xE8\xBF\x9B\xE5\xBA\xA6\xE6\x9D\xA1\xE4\xB8\x8E\xE9\x98\x9F\xE5\x88\x97/\xE6\xB3\xB5)");

    /* ---- 程序版本行：应用名称+版本经 XCoreApplication 接线；版本随引擎
     *      激活版本同步（指针为空回退出厂宏），两处显示口径一致 ---- */
    {
        const char* expectVer = s_upd.m_versionApp[0] ? s_upd.m_versionApp
                                                      : XUPGRADE_PRODUCT_VERSION;
        const XString* progText = s_upd.m_progVerLabel
                                      ? XLabel_text(s_upd.m_progVerLabel)
                                      : NULL;
        UPD_EXPECT(progText && XString_toUtf8(progText) &&
                       strstr(XString_toUtf8(progText), "XGuiWindowDemo") != NULL &&
                       strstr(XString_toUtf8(progText), expectVer) != NULL,
                   "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE7\xA8\x8B\xE5\xBA\x8F\xE7\x89\x88\xE6\x9C\xAC\xE8\xA1\x8C\xE5\xB8\xA6\xE5\xBA\x94\xE7\x94\xA8\xE5\x90\x8D\xE7\xA7\xB0\xE4\xB8\x8E\xE6\xBF\x80\xE6\xB4\xBB\xE7\x89\x88\xE6\x9C\xAC");
                   /* 软件更新页: 程序版本行带应用名称与激活版本 */
    }

    /* ---- 版本列表回填（组件行齐全；未激活显示「未记录」） ---- */
    {
        /* [2026-10-27 拆单行] app/fontres 两行分属两个单行标签，断言随
         * 拆分迁移（标签各自携带行前缀）。 */
        const XString* appText = XLabel_text(s_upd.m_versionLabel);
        const XString* fontText = XLabel_text(s_upd.m_versionFontLabel);
        UPD_EXPECT(appText && XString_toUtf8(appText) &&
                       strstr(XString_toUtf8(appText), "app:") != NULL,
                   "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE7\x89\x88\xE6\x9C\xAC\xE5\x88\x97\xE8\xA1\xA8\xE5\x9B\x9E\xE5\xA1\xAB app \xE8\xA1\x8C");
        UPD_EXPECT(fontText && XString_toUtf8(fontText) &&
                       strstr(XString_toUtf8(fontText), "fontres:") != NULL,
                   "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE7\x89\x88\xE6\x9C\xAC\xE5\x88\x97\xE8\xA1\xA8\xE5\x9B\x9E\xE5\xA1\xAB fontres \xE8\xA1\x8C");
    }
    UPD_EXPECT(XProgressBar_value(s_upd.m_phaseBar[0]) == 0 &&
                   XProgressBar_value(s_upd.m_phaseBar[3]) == 0,
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE5\x9B\x9B\xE6\xAE\xB5\xE8\xBF\x9B\xE5\xBA\xA6\xE6\x9D\xA1\xE5\x88\x9D\xE5\xA7\x8B\xE5\xBD\x92\xE9\x9B\xB6");

    /* ---- 决策按钮初始可用性（未检查前仅「检查更新」可用；目录未回填
     *      「安装所选版本」亦不可用） ---- */
    UPD_EXPECT(XWidget_isEnabled((XWidget*)s_upd.m_checkBtn) &&
                   !XWidget_isEnabled((XWidget*)s_upd.m_skipBtn) &&
                   !XWidget_isEnabled((XWidget*)s_upd.m_installBtn),
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE5\x88\x9D\xE5\xA7\x8B\xE4\xBB\x85\xE6\xA3\x80\xE6\x9F\xA5\xE6\x9B\xB4\xE6\x96\xB0\xE5\x8F\xAF\xE7\x94\xA8");

    /* ---- Progress 事件驱动四段进度条（真实 listener 入队 + 泵出） ---- */
    UPD_INJECT(XUpgradeEventType_Progress, XUpgradeState_Downloading,
               XUpgradePhase_Download, 45, XUpgradePolicy_Optional,
               XUpgradeGrayDecision_NotEvaluated, "");
    UPD_EXPECT(XProgressBar_value(s_upd.m_phaseBar[0]) == 45,
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: Progress(\xE4\xB8\x8B\xE8\xBD\xBD,45%) \xE8\x90\xBD\xE7\xAC\xAC\xE4\xB8\x80\xE6\xAE\xB5\xE8\xBF\x9B\xE5\xBA\xA6\xE6\x9D\xA1");
    UPD_INJECT(XUpgradeEventType_Progress, XUpgradeState_Verifying,
               XUpgradePhase_Verify, 100, XUpgradePolicy_Optional,
               XUpgradeGrayDecision_NotEvaluated, "");
    UPD_EXPECT(XProgressBar_value(s_upd.m_phaseBar[1]) == 100 &&
                   XProgressBar_value(s_upd.m_phaseBar[2]) == 0,
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: Progress(\xE6\xA0\xA1\xE9\xAA\x8C,100%) \xE5\x8F\xAA\xE8\x90\xBD\xE5\xAF\xB9\xE5\xBA\x94\xE6\xAE\xB5(\xE4\xB8\x8D\xE5\x81\x9A\xE9\x98\xB6\xE6\xAE\xB5\xE6\x8E\xA8\xE6\x96\xAD)");

    /* ---- UpdateAvailable(optional)：策略/灰度快照 + 决策钮启用 + 进度复位 ---- */
    UPD_INJECT(XUpgradeEventType_UpdateAvailable, XUpgradeState_Idle,
               XUpgradePhase_Download, -1, XUpgradePolicy_Optional,
               XUpgradeGrayDecision_Hit, "9.9.9");
    UPD_EXPECT(s_upd.m_available &&
                   XWidget_isEnabled((XWidget*)s_upd.m_skipBtn),
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: optional \xE7\xAD\x96\xE7\x95\xA5\xE5\xBE\x85\xE5\x86\xB3\xE7\xAD\x96\xE5\x90\xAF\xE7\x94\xA8\xE8\xB7\xB3\xE8\xBF\x87\xE9\x92\xAE");
    UPD_EXPECT(XProgressBar_value(s_upd.m_phaseBar[0]) == -1 &&
                   XProgressBar_value(s_upd.m_phaseBar[1]) == -1,
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE6\x96\xB0\xE7\x89\x88\xE6\x9C\xAC\xE5\x85\xAC\xE5\x91\x8A\xE5\xA4\x8D\xE4\xBD\x8D\xE5\x9B\x9B\xE6\xAE\xB5\xE8\xBF\x9B\xE5\xBA\xA6\xE6\x9D\xA1");
    UPD_EXPECT(strcmp(s_upd.m_lastVersion, "9.9.9") == 0 &&
                   s_upd.m_lastPolicy == XUpgradePolicy_Optional &&
                   s_upd.m_lastGray == XUpgradeGrayDecision_Hit,
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE7\xAD\x96\xE7\x95\xA5/\xE7\x81\xB0\xE5\xBA\xA6/\xE7\x89\x88\xE6\x9C\xAC\xE5\xBF\xAB\xE7\x85\xA7\xE9\x9A\x8F\xE4\xBA\x8B\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0");

    /* ---- ReadyNeedRestart：待应用态置位 + 重启提示 ---- */
    UPD_INJECT(XUpgradeEventType_ReadyNeedRestart, XUpgradeState_Ready,
               XUpgradePhase_Activate, -1, XUpgradePolicy_Optional,
               XUpgradeGrayDecision_NotEvaluated, "");
    UPD_EXPECT(s_upd.m_readyRestart &&
                   strstr(s_upd.m_lastStatus,
                          "\xE9\x87\x8D\xE5\x90\xAF") != NULL,
                   /* 重启 */
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: ReadyNeedRestart \xE7\xBD\xAE\xE5\xBE\x85\xE5\xBA\x94\xE7\x94\xA8\xE6\x80\x81\xE5\xB9\xB6\xE6\x8F\x90\xE7\xA4\xBA\xE9\x87\x8D\xE5\x90\xAF");

    /* ---- RolledBack：故障提示（含回滚语义） ---- */
    UPD_INJECT(XUpgradeEventType_RolledBack, XUpgradeState_RolledBack,
               XUpgradePhase_Activate, -1, XUpgradePolicy_Optional,
               XUpgradeGrayDecision_NotEvaluated, "");
    UPD_EXPECT(strstr(s_upd.m_lastStatus,
                      "\xE5\x9B\x9E\xE6\xBB\x9A") != NULL,
                   /* 回滚 */
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: RolledBack \xE6\x95\x85\xE9\x9A\x9C\xE6\x8F\x90\xE7\xA4\xBA\xE8\x90\xBD\xE7\x8A\xB6\xE6\x80\x81\xE8\xA1\x8C");
    UPD_EXPECT(!s_upd.m_readyRestart,
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE5\x9B\x9E\xE6\xBB\x9A\xE5\x90\x8E\xE5\xBE\x85\xE5\xBA\x94\xE7\x94\xA8\xE6\x80\x81\xE6\x94\xB6\xE5\x8F\xA3");

    /* ---- CheckFailed：状态行静默显示（URL 空检查失败口径） ---- */
    memset(&event, 0, sizeof(event));
    event.m_type = XUpgradeEventType_CheckFailed;
    event.m_state = XUpgradeState_CheckFailed;
    upd_listener(NULL, &event);
    upd_pumpCb(NULL, NULL);
    UPD_EXPECT(strstr(s_upd.m_lastStatus, "\xE6\x9A\x82\xE6\x97\xA0\xE6\x9B\xB4\xE6\x96\xB0") != NULL,
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: CheckFailed \xE6\x97\xA0\xE8\xAF\xB4\xE6\x98\x8E\xE8\x90\xBD\xE3\x80\x8C\xE6\x9A\x82\xE6\x97\xA0\xE6\x9B\xB4\xE6\x96\xB0\xE3\x80\x8D"); /* 暂无更新 */
    memset(&event, 0, sizeof(event));
    event.m_type = XUpgradeEventType_CheckFailed;
    event.m_state = XUpgradeState_CheckFailed;
    snprintf(event.m_detail, sizeof(event.m_detail), "%s", "url empty");
    upd_listener(NULL, &event);
    upd_pumpCb(NULL, NULL);
    UPD_EXPECT(strstr(s_upd.m_lastStatus,
                      "\xE6\xA3\x80\xE6\x9F\xA5\xE5\xA4\xB1\xE8\xB4\xA5") !=
                   NULL,
                   /* 检查失败 */
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: CheckFailed \xE7\x8A\xB6\xE6\x80\x81\xE8\xA1\x8C\xE9\x9D\x99\xE9\xBB\x98\xE6\x98\xBE\xE7\xA4\xBA");

    /* ---- StateChanged(Checking)：新一轮检查复位进度 ---- */
    UPD_INJECT(XUpgradeEventType_Progress, XUpgradeState_Downloading,
               XUpgradePhase_Download, 30, XUpgradePolicy_Optional,
               XUpgradeGrayDecision_NotEvaluated, "");
    UPD_INJECT(XUpgradeEventType_StateChanged, XUpgradeState_Checking,
               XUpgradePhase_Download, -1, XUpgradePolicy_Optional,
               XUpgradeGrayDecision_NotEvaluated, "");
    UPD_EXPECT(XProgressBar_value(s_upd.m_phaseBar[0]) == -1 &&
                   strstr(s_upd.m_lastStatus,
                          "\xE6\xA3\x80\xE6\x9F\xA5\xE4\xB8\xAD") != NULL,
                   /* 检查中 */
               "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE6\x96\xB0\xE4\xB8\x80\xE8\xBD\xAE\xE6\xA3\x80\xE6\x9F\xA5\xE5\xA4\x8D\xE4\xBD\x8D\xE8\xBF\x9B\xE5\xBA\xA6\xE5\xB9\xB6\xE6\x98\xBE\xE7\xA4\xBA\xE5\xBC\x95\xE6\x93\x8E\xE7\x8A\xB6\xE6\x80\x81");

    /* ---- 懒启动真实路径：init 成功即就绪；被占用/根不可用按失败口径
     *      （不发起检查——检查链路由 XUpgradeE2E_Test 覆盖）。 ---- */
    {
        bool started = upd_ensureStarted();
        UPD_EXPECT(started ||
                       strstr(s_upd.m_lastStatus,
                              "\xE5\x88\x9D\xE5\xA7\x8B\xE5\x8C\x96\xE5"
                              "\xA4\xB1\xE8\xB4\xA5") != NULL,
                       /* 初始化失败 */
                   "\xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5: \xE5\xBC\x95\xE6\x93\x8E\xE6\x87\x92\xE5\x90\xAF\xE5\x8A\xA8\xE6\x94\xB6\xE5\x8F\xA3(\xE5\xB0\xB1\xE7\xBB\xAA\xE6\x88\x96\xE5\xA6\x82\xE5\xAE\x9E\xE5\x8F\x8D\xE9\xA6\x88\xE5\x8D\xA0\xE7\x94\xA8)");
    }

#undef UPD_INJECT
#undef UPD_EXPECT
    /* 抑制标志复位：autotest 后 demo 继续截图/交互流程（不退出），不复位
     * 会令此后真实 ReadyNeedRestart 的模态弹窗对整个会话失效。 */
    s_upd.m_dialogSuppress = false;
    XPrintf("XGuiAutoTest: \xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5 %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}

#else /* 裁剪: 契约桩（符号常在, 主文件可无条件链接） */

XWidget* demo_page_update_build(XWidget* parent,
                                DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    XPrintf("XGuiAutoTest: [FAIL] \xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5\xE4\xBE\x9D\xE8\xB5\x96 XUpgrade/\xE6\x8E\xA7\xE4\xBB\xB6\xE8\xA2\xAB\xE8\xA3\x81\xE5\x89\xAA\xEF\xBC\x8C"
            "\xE6\x97\xA0\xE6\xB3\x95\xE6\x9E\x84\xE5\xBB\xBA\n");
    return (XWidget*)0;
}

int demo_page_update_autotest(XWidget* page)
{
    (void)page;
    XPrintf("XGuiAutoTest: [FAIL] \xE8\xBD\xAF\xE4\xBB\xB6\xE6\x9B\xB4\xE6\x96\xB0\xE9\xA1\xB5\xE8\xA2\xAB\xE8\xA3\x81\xE5\x89\xAA\xEF\xBC\x8C\xE6\x97\xA0\xE6\xB3\x95\xE8\x87\xAA\xE6\xB5\x8B\n");
    return -1;
}

void demo_page_update_adapt(XWidget* page)
{
    (void)page;
}

void demo_page_update_shutdown(void)
{
}

#endif /* XUPGRADE_ON && 控件段 */
