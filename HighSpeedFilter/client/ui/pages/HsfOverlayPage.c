/******************************************************************************
 * @file       xgui_demo_page_overlay_settings.c
 * @brief      XGuiWindowDemo「悬浮窗设置」页（系统设置：性能悬浮层属性）。
 * @details    契约见 xgui_demo_pages.h（hsf_page_overlay_build /
 *             hsf_page_overlay_autotest / adapt）。
 *
 *             页面内容（2026-10-06 随「控件/系统设置/远程」一级菜单
 *             整改新增，用户口径：悬浮窗各项属性作为二级菜单单独
 *             一个设置界面）：
 *              - 显示数据：FPS / 帧耗时 / CPU·GPU / 网络 / 内存 五行
 *                独立开关（XPerformanceOverlay_set*Visible，即时生效）；
 *              - 内存行：显示种类（准确数/百分比/两者）与数据来源
 *                （自动/系统/库内）两组下拉框；
 *              - 位置：九宫格预设位置（经 demo_main_overlay_applyPreset
 *                应用，主文件挂起/恢复 resize 自动重锚右下角行为）+
 *                固定位置 / 允许拖动 开关；
 *              - 外观：字号（12/13/14/16/18）与自适应尺寸开关；
 *              - 「复位默认」一键恢复（悬浮层 reset + 页面控件回读
 *                默认态 + 恢复自动重锚）。
 *
 *             悬浮层对象经 demo_main_overlay(user) 取主窗实例借用
 *             指针（本页不拥有）；XGUI_PERFORMANCE_OVERLAY_ON=0 时
 *             build 返回 NULL，主文件跳过注册。
 *
 *             autotest 口径：全程非阻塞，直呼槽/真实控件路径，逐项
 *             切换并断言悬浮层 getter 回读，结束恢复默认态。
 *
 *             竖屏窄宽分档（2026-10-25）：四条水平行按桌面定尺最宽需
 *             584，竖屏 400 逻辑宽放不下（「网络/内存」「数据来源」
 *             「允许拖动」裁出屏外），adapt 按页根宽 600 分档：窄档
 *             收缩定尺+可点控件抬到 40 高（触控目标），宽档回写桌面
 *             装配原值，1000x700 现状不动。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "XGuiConfig.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XWidget.h"
#include "XBoxLayout.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XComboBox.h"
#include "XPushButton.h"
#include "XPerformanceOverlay.h"
#include "HsfDemoCompat.h"
#include "HsfSettingsStore.h"

/*
 * 控件段宏守卫：悬浮层编译开关 + CheckBox/ComboBox/PushButton/Label；
 * 2026-10-17 容器化后排布由 XBoxLayout 承载，再一并依赖
 * XLAYOUT/XLAYOUT_BOX。任一被裁剪时降级为契约桩（build 返回 NULL、
 * autotest 返回 -1）。
 */
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON && \
    XCHECKBOX_ON && XCOMBOBOX_ON && XPUSHBUTTON_ON && XLAYOUT_ON && \
    XLAYOUT_BOX_ON

/* ==================== 持久化：键名与默认值 ==================== */

/* 键名（Qt 风格 group/key；框架自动落成 INI 分节 [Overlay]）。 */
#define OV_KEY_VISIBLE     "Overlay/Visible"       /* 总开关：显示悬浮窗 */
#define OV_KEY_FPS         "Overlay/ShowFps"       /* FPS 行 */
#define OV_KEY_FRAME_TIME  "Overlay/ShowFrameTime" /* 帧耗时行 */
#define OV_KEY_SYS_STAT    "Overlay/ShowSysStat"   /* CPU/GPU 行 */
#define OV_KEY_NETWORK     "Overlay/ShowNetwork"   /* 网络行 */
#define OV_KEY_MEMORY      "Overlay/ShowMemory"    /* 内存行 */
#define OV_KEY_MEM_DISPLAY "Overlay/MemoryDisplay" /* 内存行显示种类 */
#define OV_KEY_MEM_SOURCE  "Overlay/MemorySource"  /* 内存数据来源 */
#define OV_KEY_PRESET      "Overlay/PresetPosition"/* 九宫格预设位置 */
#define OV_KEY_FIXED       "Overlay/FixedPosition" /* 固定位置 */
#define OV_KEY_MOVABLE     "Overlay/Movable"       /* 允许拖动 */
#define OV_KEY_FONT_PX     "Overlay/FontPixelSize" /* 字号（像素） */
#define OV_KEY_AUTOFIT     "Overlay/AutoFitSize"   /* 自适应尺寸 */

/*
 * 出厂默认值 = **程序启动默认值**（2026-11-03 用户裁定统一口径）。
 * 与 HsfUi.c 建悬浮层时的初值一一对应：自适应尺寸开 / 固定位置开 /
 * 字号 12px / 可见；五个显示行全开、内存显示「两者」、数据来源「自动」、
 * 预设右下、允许拖动开。
 * 注：XPerformanceOverlay 自身的构造默认（自适应关 / 固定关）**不是**
 *     本应用的出厂默认——主窗建层时显式改过（见 HsfUi.c）。
 * 「复位默认」按钮也复位到这套值（此前复位只清采样数据，开关一个没复位）。
 */
#define OV_DEFAULT_VISIBLE      true
#define OV_DEFAULT_FPS          true
#define OV_DEFAULT_FRAME_TIME   true
#define OV_DEFAULT_SYS_STAT     true
#define OV_DEFAULT_NETWORK      true
#define OV_DEFAULT_MEMORY       true
#define OV_DEFAULT_MEM_DISPLAY  ((int)XPerformanceOverlayMemoryDisplay_Both)
#define OV_DEFAULT_MEM_SOURCE   ((int)XPerformanceOverlayMemorySource_Auto)
#define OV_DEFAULT_PRESET       ((int)XPerformanceOverlayPosition_BottomRight)
#define OV_DEFAULT_FIXED        true
#define OV_DEFAULT_MOVABLE      true
#define OV_DEFAULT_AUTOFIT      true
#define OV_DEFAULT_FONT_PX      12

/** @brief 字号档位表（下拉序 → 像素）。 */
static const int kOvFontSizes[5] = { 12, 13, 14, 16, 18 };

/** @brief 字号档位表条目数。 */
#define OV_FONT_SIZE_COUNT ((int)(sizeof(kOvFontSizes) / sizeof(kOvFontSizes[0])))

/** @brief 字号档位表条目数（宏展开的常量别名，保持旧引用可用）。 */
#define OV_FONT_SIZES_COUNT OV_FONT_SIZE_COUNT

/** @brief 像素字号 → 档位下标（无匹配回落默认档位）。 */
static int ov_fontIndexOf(int px)
{
    int i;
    for (i = 0; i < OV_FONT_SIZE_COUNT; i++)
        if (kOvFontSizes[i] == px) return i;
    return 0;
}

/* ==================== 页面内部控件登记（demo 单实例 static 自持） ==== */

/** @brief 悬浮窗设置页内部登记表。 */
static struct
{
    XWidget*         m_root;        /**< 页面根控件（契约返回值；堆对象）。 */
    DemoPageStatusFn m_status;      /**< 主窗口状态栏反馈回调（借用）。 */
    void*            m_user;        /**< 回调上下文（主窗口指针，借用）。 */
    char             m_lastStatus[160]; /**< 最近一次反馈文本（自测断言用）。 */
    bool             m_ready;       /**< build 已完成且控件指针有效。 */

    /* ---- 显示数据 ---- */
    XLabel*      m_titleLabel;   /**< 页标题。 */
    XLabel*      m_capRows;      /**< 「显示数据」分组标题。 */
    XCheckBox*   m_fpsCheck;     /**< FPS 行开关。 */
    XCheckBox*   m_frameCheck;   /**< 帧耗时行开关。 */
    XCheckBox*   m_sysCheck;     /**< CPU·GPU 行开关。 */
    XCheckBox*   m_netCheck;     /**< 网络行开关。 */
    XCheckBox*   m_memCheck;     /**< 内存行开关。 */

    /* ---- 内存行 ---- */
    XLabel*      m_capMem;       /**< 「内存行」分组标题。 */
    XLabel*      m_capMemDisp;   /**< 「显示种类」标题。 */
    XComboBox*   m_memDispCombo; /**< 准确数/百分比/两者。 */
    XLabel*      m_capMemSrc;    /**< 「数据来源」标题。 */
    XComboBox*   m_memSrcCombo;  /**< 自动/系统/库内。 */

    /* ---- 位置 ---- */
    XLabel*      m_capPos;       /**< 「位置」分组标题。 */
    XLabel*      m_capPreset;    /**< 「预设位置」标题。 */
    XComboBox*   m_presetCombo;  /**< 九宫格九项。 */
    XCheckBox*   m_fixedCheck;   /**< 固定位置开关。 */
    XCheckBox*   m_movableCheck; /**< 允许拖动开关。 */

    /* ---- 外观 ---- */
    XLabel*      m_capStyle;     /**< 「外观」分组标题。 */
    XLabel*      m_capFont;      /**< 「字号」标题。 */
    XComboBox*   m_fontCombo;    /**< 12/13/14/16/18。 */
    XCheckBox*   m_autofitCheck; /**< 自适应尺寸开关。 */

    /* ---- 操作与反馈 ---- */
    XCheckBox*   m_masterCheck;  /**< 「显示悬浮窗」总开关（整体显隐）。 */
    XPushButton* m_resetBtn;     /**< 复位默认。 */
    XLabel*      m_hintLabel;    /**< 说明行（钉底上方）。 */
    XLabel*      m_statusLabel;  /**< 操作结果行（钉底）。 */

    bool         m_syncing;      /**< 回读同步中（抑制状态行刷屏）。 */
    bool         m_tierNarrow;   /**< 窄宽档已生效（adapt 分档去重，免重排）。 */
    bool         m_persistReady; /**< 允许落盘（恢复完成后才置真）。 */

    /* ---- 布局器（2026-10-17 容器化；build 装配段挂根，静态自持） ---- */
    XBoxLayout   m_rootLayout;   /**< 根垂直布局器（页内唯一 setLayout）。 */
    XBoxLayout   m_masterRow;    /**< 「显示悬浮窗」总开关行（水平）。 */
    XBoxLayout   m_rowsRow;      /**< 「显示数据」五行开关行（水平）。 */
    XBoxLayout   m_memRow;       /**< 「内存行」两组下拉行（水平）。 */
    XBoxLayout   m_posRow;       /**< 「位置」预设+开关行（水平）。 */
    XBoxLayout   m_styleRow;     /**< 「外观」字号+开关行（水平）。 */
} s_ov;

/* ==================== 内部辅助 ==================== */

/** @brief 反馈到页面状态行与主窗状态栏（登记最近文本供自测断言）。 */
static void ov_report(const char* text)
{
    if (!text) return;
    snprintf(s_ov.m_lastStatus, sizeof(s_ov.m_lastStatus), "%s", text);
    if (s_ov.m_statusLabel)
        XLabel_setText_2(s_ov.m_statusLabel, text);
    if (s_ov.m_status)
        s_ov.m_status(s_ov.m_user, text);
}


/** @brief 目标悬浮层借用指针（user 透传链失守时 NULL）。 */
static XPerformanceOverlay* ov_target(void)
{
    return (XPerformanceOverlay*)demo_main_overlay(s_ov.m_user);
}

/** @brief 页面控件 ← 悬浮层当前态回读（build/复位共用）。 */
static void ov_syncFromOverlay(void)
{
    XPerformanceOverlay* overlay = ov_target();
    if (!overlay) return;
    s_ov.m_syncing = true;
    if (s_ov.m_fpsCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_fpsCheck,
                                   XPerformanceOverlay_isFpsVisible(overlay));
    if (s_ov.m_frameCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_frameCheck,
                                   XPerformanceOverlay_isFrameTimeVisible(
                                       overlay));
    if (s_ov.m_sysCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_sysCheck,
                                   XPerformanceOverlay_isSysStatVisible(
                                       overlay));
    if (s_ov.m_netCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_netCheck,
                                   XPerformanceOverlay_isNetworkVisible(
                                       overlay));
    if (s_ov.m_memCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_memCheck,
                                   XPerformanceOverlay_isMemoryVisible(
                                       overlay));
    if (s_ov.m_memDispCombo)
        XComboBox_setCurrentIndex(s_ov.m_memDispCombo,
                                  (int)XPerformanceOverlay_memoryDisplay(
                                      overlay));
    if (s_ov.m_memSrcCombo)
        XComboBox_setCurrentIndex(s_ov.m_memSrcCombo,
                                  (int)XPerformanceOverlay_memorySource(
                                      overlay));
    if (s_ov.m_fixedCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_fixedCheck,
                                   XPerformanceOverlay_isFixed(overlay));
    if (s_ov.m_movableCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_movableCheck,
                                   XPerformanceOverlay_isMovable(overlay));
    if (s_ov.m_autofitCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_autofitCheck,
                                   XPerformanceOverlay_isAutoFitSize(
                                       overlay));
    s_ov.m_syncing = false;
}

/* ==================== 持久化（保存 / 恢复） ==================== */

/**
 * @brief 把当前设置写入配置文件（全部 13 项）。
 * @details 真源分层取：有运行时 getter 的读运行时（悬浮层对象），框架层
 *          **没有 getter** 的两项（预设位置 / 字号）读页面控件——控件是
 *          这两项的唯一真源（悬浮层对象不存预设位置，字号只有 setter）。
 * @note  m_persistReady 门：建页期与恢复期的**编程**赋值也会触发 toggled
 *        槽（如建页时给「显示悬浮窗」总开关赋初值），若此时就落盘，会把
 *        配置文件**覆盖成启动默认值**——用户的设置在恢复前就被抹掉。
 *        故只在恢复完成后（m_persistReady=true）才允许落盘。
 */
static void ov_saveSettings(void)
{
    XPerformanceOverlay* overlay = ov_target();
    int preset = OV_DEFAULT_PRESET;
    int fontPx = OV_DEFAULT_FONT_PX;

    if (!overlay || !s_ov.m_persistReady) return;
    if (s_ov.m_presetCombo) {
        preset = XComboBox_currentIndex(s_ov.m_presetCombo);
        if (preset < 0 || preset > 8) preset = OV_DEFAULT_PRESET;
    }
    if (s_ov.m_fontCombo) {
        int idx = XComboBox_currentIndex(s_ov.m_fontCombo);
        if (idx < 0 || idx >= OV_FONT_SIZE_COUNT) idx = ov_fontIndexOf(OV_DEFAULT_FONT_PX);
        fontPx = kOvFontSizes[idx];
    }
    HsfSettingsStore_setBool(OV_KEY_VISIBLE,
                             !XWidget_isHidden((XWidget*)overlay));
    HsfSettingsStore_setBool(OV_KEY_FPS,
                             XPerformanceOverlay_isFpsVisible(overlay));
    HsfSettingsStore_setBool(OV_KEY_FRAME_TIME,
                             XPerformanceOverlay_isFrameTimeVisible(overlay));
    HsfSettingsStore_setBool(OV_KEY_SYS_STAT,
                             XPerformanceOverlay_isSysStatVisible(overlay));
    HsfSettingsStore_setBool(OV_KEY_NETWORK,
                             XPerformanceOverlay_isNetworkVisible(overlay));
    HsfSettingsStore_setBool(OV_KEY_MEMORY,
                             XPerformanceOverlay_isMemoryVisible(overlay));
    HsfSettingsStore_setInt(OV_KEY_MEM_DISPLAY,
                            (int)XPerformanceOverlay_memoryDisplay(overlay));
    HsfSettingsStore_setInt(OV_KEY_MEM_SOURCE,
                            (int)XPerformanceOverlay_memorySource(overlay));
    HsfSettingsStore_setInt(OV_KEY_PRESET, preset);
    HsfSettingsStore_setBool(OV_KEY_FIXED,
                             XPerformanceOverlay_isFixed(overlay));
    HsfSettingsStore_setBool(OV_KEY_MOVABLE,
                             XPerformanceOverlay_isMovable(overlay));
    HsfSettingsStore_setInt(OV_KEY_FONT_PX, fontPx);
    HsfSettingsStore_setBool(OV_KEY_AUTOFIT,
                             XPerformanceOverlay_isAutoFitSize(overlay));
    HsfSettingsStore_sync();
}

/**
 * @brief 从配置文件恢复设置：先落运行时，再回填控件。
 * @details 恢复时键不存在→取出厂默认（= 程序启动默认），故「无配置文件
 *          首次运行」与「有配置恢复」走同一条路径，行为一致。
 *          控件回填时全程持 m_syncing：setChecked 会触发 toggled 槽，
 *          不抑制会把恢复动作当成用户操作再写一遍配置。
 */
void hsf_page_overlay_restoreSettings(void)
{
    XPerformanceOverlay* overlay = ov_target();
    bool visible;
    int preset;
    int fontPx;
    int fontIdx;
    int memDisp;
    int memSrc;

    if (!overlay) return;

    visible = HsfSettingsStore_getBool(OV_KEY_VISIBLE, OV_DEFAULT_VISIBLE);
    preset = HsfSettingsStore_getInt(OV_KEY_PRESET, OV_DEFAULT_PRESET);
    fontPx = HsfSettingsStore_getInt(OV_KEY_FONT_PX, OV_DEFAULT_FONT_PX);
    memDisp = HsfSettingsStore_getInt(OV_KEY_MEM_DISPLAY, OV_DEFAULT_MEM_DISPLAY);
    memSrc = HsfSettingsStore_getInt(OV_KEY_MEM_SOURCE, OV_DEFAULT_MEM_SOURCE);
    /* 越界值（手改 ini / 旧版本档位变化）一律回出厂默认——枚举越界会把
       底层状态写成不可预期的档位。 */
    if (preset < 0 || preset > 8) preset = OV_DEFAULT_PRESET;
    if (memDisp < 0 || memDisp > 2) memDisp = OV_DEFAULT_MEM_DISPLAY;
    if (memSrc < 0 || memSrc > 2) memSrc = OV_DEFAULT_MEM_SOURCE;
    fontIdx = ov_fontIndexOf(fontPx);
    fontPx = kOvFontSizes[fontIdx];

    /* ---- 运行时（悬浮层对象） ---- */
    XPerformanceOverlay_setFpsVisible(
        overlay, HsfSettingsStore_getBool(OV_KEY_FPS, OV_DEFAULT_FPS));
    XPerformanceOverlay_setFrameTimeVisible(
        overlay,
        HsfSettingsStore_getBool(OV_KEY_FRAME_TIME, OV_DEFAULT_FRAME_TIME));
    XPerformanceOverlay_setSysStatVisible(
        overlay,
        HsfSettingsStore_getBool(OV_KEY_SYS_STAT, OV_DEFAULT_SYS_STAT));
    XPerformanceOverlay_setNetworkVisible(
        overlay,
        HsfSettingsStore_getBool(OV_KEY_NETWORK, OV_DEFAULT_NETWORK));
    XPerformanceOverlay_setMemoryVisible(
        overlay,
        HsfSettingsStore_getBool(OV_KEY_MEMORY, OV_DEFAULT_MEMORY));
    XPerformanceOverlay_setMemoryDisplay(
        overlay, (XPerformanceOverlayMemoryDisplay)memDisp);
    XPerformanceOverlay_setMemorySource(
        overlay, (XPerformanceOverlayMemorySource)memSrc);
    XPerformanceOverlay_setAutoFitSize(
        overlay,
        HsfSettingsStore_getBool(OV_KEY_AUTOFIT, OV_DEFAULT_AUTOFIT));
    XPerformanceOverlay_setMovable(
        overlay,
        HsfSettingsStore_getBool(OV_KEY_MOVABLE, OV_DEFAULT_MOVABLE));
    XPerformanceOverlay_setFixed(
        overlay,
        HsfSettingsStore_getBool(OV_KEY_FIXED, OV_DEFAULT_FIXED));
    XPerformanceOverlay_setTextPixelSize(overlay, fontPx);
    XWidget_setVisible((XWidget*)overlay, visible);
    /* 预设位置：经主窗应用（非右下预设会钉住、挂起 resize 自动重锚）。 */
    demo_main_overlay_applyPreset(s_ov.m_user, preset);

    /* ---- 控件回填（无 getter 的两项 + 总开关） ---- */
    s_ov.m_syncing = true;
    if (s_ov.m_presetCombo)
        XComboBox_setCurrentIndex(s_ov.m_presetCombo, preset);
    if (s_ov.m_fontCombo)
        XComboBox_setCurrentIndex(s_ov.m_fontCombo, fontIdx);
    if (s_ov.m_masterCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_masterCheck,
                                   visible);
    /* 其余项从运行时回读（内存显示/来源/固定/拖动/自适应/五个开关）。 */
    ov_syncFromOverlay();
    s_ov.m_syncing = false;
    /* 恢复完成 → 允许后续改动落盘（此前建页期的编程赋值不得写配置）。 */
    s_ov.m_persistReady = true;
    if (!HsfSettingsStore_isOpen()) {
        XPrintf("HsfClient: 未启用设置持久化（--no-settings 或配置不可用），"
                "悬浮窗设置按出厂默认运行\n");
        return;
    }
    XPrintf("HsfClient: 已恢复悬浮窗设置（可见=%d FPS=%d 帧耗时=%d CPU/GPU=%d"
            " 网络=%d 内存=%d 内存显示=%d 内存来源=%d 预设=%d 固定=%d 拖动=%d"
            " 字号=%d 自适应=%d）\n",
            visible ? 1 : 0,
            HsfSettingsStore_getBool(OV_KEY_FPS, OV_DEFAULT_FPS) ? 1 : 0,
            HsfSettingsStore_getBool(OV_KEY_FRAME_TIME, OV_DEFAULT_FRAME_TIME) ? 1 : 0,
            HsfSettingsStore_getBool(OV_KEY_SYS_STAT, OV_DEFAULT_SYS_STAT) ? 1 : 0,
            HsfSettingsStore_getBool(OV_KEY_NETWORK, OV_DEFAULT_NETWORK) ? 1 : 0,
            HsfSettingsStore_getBool(OV_KEY_MEMORY, OV_DEFAULT_MEMORY) ? 1 : 0,
            memDisp, memSrc, preset,
            HsfSettingsStore_getBool(OV_KEY_FIXED, OV_DEFAULT_FIXED) ? 1 : 0,
            HsfSettingsStore_getBool(OV_KEY_MOVABLE, OV_DEFAULT_MOVABLE) ? 1 : 0,
            fontPx,
            HsfSettingsStore_getBool(OV_KEY_AUTOFIT, OV_DEFAULT_AUTOFIT) ? 1 : 0);
}

/** @brief 「显示悬浮窗」总开关槽：整体显隐（各行开关只管行内内容）。
 *  @details demo 原页只有各行开关，**没有整体关闭入口**（全关也只是
 *           悬浮层空白、控件仍在）——本移植补总开关。 */
static void ov_masterSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay = ov_target();
    bool on;
    (void)receiver; (void)args;
    if (!overlay || !s_ov.m_masterCheck) return;
    if (s_ov.m_syncing) return; /* 恢复期编程回填：不做即时生效动作 */
    on = XAbstractButton_isChecked((XAbstractButton*)s_ov.m_masterCheck);
    XWidget_setVisible((XWidget*)overlay, on);
    s_ov.m_syncing = true;
    ov_syncFromOverlay();
    s_ov.m_syncing = false;
    /* [文案修复 2026-11-10] 原字符串是二次编码损坏的乱码
     * （"已打开展正"/"已滞已打开展"），按语义还原为「已打开悬浮窗/
     * 已关闭悬浮窗」。 */
    ov_report(on ? "已打开悬浮窗"
                  : "已关闭悬浮窗");
    ov_saveSettings();
}


/* ==================== 信号槽（改动即时生效） ==================== */

/** @brief FPS 行开关。 */
static void ov_fpsSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    bool checked;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_fpsCheck || s_ov.m_syncing) return;
    checked = XAbstractButton_isChecked((XAbstractButton*)s_ov.m_fpsCheck);
    XPerformanceOverlay_setFpsVisible(overlay, checked);
    ov_report(checked ? "显示 FPS"
                      : "隐藏 FPS"); /* 显示/隐藏 FPS */
    ov_saveSettings();
}

/** @brief 帧耗时行开关。 */
static void ov_frameSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    bool checked;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_frameCheck || s_ov.m_syncing) return;
    checked = XAbstractButton_isChecked((XAbstractButton*)s_ov.m_frameCheck);
    XPerformanceOverlay_setFrameTimeVisible(overlay, checked);
    ov_report(checked ? "显示帧耗"
                      : "隐藏帧耗");
                      /* 显示/隐藏帧耗时 */
    ov_saveSettings();
}

/** @brief CPU·GPU 行开关。 */
static void ov_sysSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    bool checked;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_sysCheck || s_ov.m_syncing) return;
    checked = XAbstractButton_isChecked((XAbstractButton*)s_ov.m_sysCheck);
    XPerformanceOverlay_setSysStatVisible(overlay, checked);
    ov_report(checked ? "显示 CPU/GPU"
                      : "隐藏 CPU/GPU");
    ov_saveSettings();
}

/** @brief 网络行开关。 */
static void ov_netSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    bool checked;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_netCheck || s_ov.m_syncing) return;
    checked = XAbstractButton_isChecked((XAbstractButton*)s_ov.m_netCheck);
    XPerformanceOverlay_setNetworkVisible(overlay, checked);
    ov_report(checked ? "显示网络"
                      : "隐藏网络");
                      /* 显示/隐藏网络 */
    ov_saveSettings();
}

/** @brief 内存行开关。 */
static void ov_memSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    bool checked;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_memCheck || s_ov.m_syncing) return;
    checked = XAbstractButton_isChecked((XAbstractButton*)s_ov.m_memCheck);
    XPerformanceOverlay_setMemoryVisible(overlay, checked);
    ov_report(checked ? "显示内存"
                      : "隐藏内存");
                      /* 显示/隐藏内存 */
    ov_saveSettings();
}

/** @brief 内存行显示种类。 */
static void ov_memDispSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    int index;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_memDispCombo || s_ov.m_syncing) return;
    index = XComboBox_currentIndex(s_ov.m_memDispCombo);
    if (index < 0) index = 0;
    XPerformanceOverlay_setMemoryDisplay(
        overlay, (XPerformanceOverlayMemoryDisplay)index);
    ov_report("内存行显示种类已更换"); /* 内存行显示种类已更换 */
    ov_saveSettings();
}

/** @brief 内存行数据来源。 */
static void ov_memSrcSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    int index;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_memSrcCombo || s_ov.m_syncing) return;
    index = XComboBox_currentIndex(s_ov.m_memSrcCombo);
    if (index < 0) index = 0;
    XPerformanceOverlay_setMemorySource(
        overlay, (XPerformanceOverlayMemorySource)index);
    ov_report("内存数据来源已更换");
              /* 内存数据来源已更换 */
    ov_saveSettings();
}

/** @brief 九宫格预设位置（经主文件应用，协商自动重锚行为）。 */
static void ov_presetSlot(XObject* receiver, XVarList* args)
{
    int index;
    (void)receiver; (void)args;
    if (!s_ov.m_presetCombo || s_ov.m_syncing) return;
    index = XComboBox_currentIndex(s_ov.m_presetCombo);
    if (index < 0) index = 8;
    demo_main_overlay_applyPreset(s_ov.m_user, index);
    ov_report("已应用预设位置");
    ov_saveSettings();
}

/** @brief 固定位置开关。 */
static void ov_fixedSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    bool checked;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_fixedCheck || s_ov.m_syncing) return;
    checked = XAbstractButton_isChecked((XAbstractButton*)s_ov.m_fixedCheck);
    XPerformanceOverlay_setFixed(overlay, checked);
    /* [文案修复 2026-11-10] 原字符串被二次编码损坏成乱码（"宸茶В闄"），
     * 按本行注释里的正确文案还原。 */
    ov_report(checked ? "已锁定位置"
                      : "已解除固定"); /* 已锁定位置/已解除固定 */
    ov_saveSettings();
}

/** @brief 允许拖动开关。 */
static void ov_movableSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    bool checked;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_movableCheck || s_ov.m_syncing) return;
    checked =
        XAbstractButton_isChecked((XAbstractButton*)s_ov.m_movableCheck);
    XPerformanceOverlay_setMovable(overlay, checked);
    ov_report(checked ? "已允许拖动" : "已禁止拖动"); /* 已允许拖动/已禁止拖动 */
    ov_saveSettings();
}

/** @brief 字号切换。 */
static void ov_fontSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    int index;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_fontCombo || s_ov.m_syncing) return;
    index = XComboBox_currentIndex(s_ov.m_fontCombo);
    if (index < 0 || index >= OV_FONT_SIZE_COUNT)
        index = ov_fontIndexOf(OV_DEFAULT_FONT_PX);
    XPerformanceOverlay_setTextPixelSize(overlay, kOvFontSizes[index]);
    /* [文案修复 2026-11-10] 原字符串被二次编码损坏成乱码
     * （"瀛楀彿宸叉洿鎹¢"），按本行注释里的正确文案还原。 */
    ov_report("字号已更换"); /* 字号已更换 */
    ov_saveSettings();
}

/** @brief 自适应尺寸开关。 */
static void ov_autofitSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    bool checked;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_autofitCheck || s_ov.m_syncing) return;
    checked =
        XAbstractButton_isChecked((XAbstractButton*)s_ov.m_autofitCheck);
    XPerformanceOverlay_setAutoFitSize(overlay, checked);
    ov_report(checked ? "已开启自"
                      "适应尺寸"
                      : "已关闭自"
                        "适应"); /* 已开启自适应尺寸/已关闭自适应 */
    ov_saveSettings();
}

/**
 * @brief 复位默认：悬浮层统计值复位 + **全部设置项**回出厂默认 + 控件回读。
 * @details 【复位补齐 2026-11-03】XPerformanceOverlay_reset 只清采样数据
 *          （FPS/帧耗时/CPU/内存等统计值），显示开关、内存显示种类、数据
 *          来源、固定位置、允许拖动、自适应尺寸**一项都没复位**——此前
 *          「复位默认」只清了统计值，界面开关保持用户值（与 Qt「恢复默认」
 *          语义不符）。叠加持久化后更严重：复位后再落盘会把**非默认值**
 *          写回配置，默认值永远存不回去。
 *          此处逐项显式复位到**程序启动默认值**（与 HsfUi.c 建层口径一致：
 *          自适应开 / 固定开 / 字号 12px / 可见 / 五个显示行全开 / 内存
 *          显示「两者」/ 来源「自动」/ 预设右下 / 允许拖动开）。
 */
static void ov_resetSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay) return;
    /* 统计值归零（FPS/帧耗时/CPU·GPU/内存快照）。 */
    XPerformanceOverlay_reset(overlay);
    /* 设置项逐项复位到出厂默认。 */
    XPerformanceOverlay_setFpsVisible(overlay, OV_DEFAULT_FPS);
    XPerformanceOverlay_setFrameTimeVisible(overlay, OV_DEFAULT_FRAME_TIME);
    XPerformanceOverlay_setSysStatVisible(overlay, OV_DEFAULT_SYS_STAT);
    XPerformanceOverlay_setNetworkVisible(overlay, OV_DEFAULT_NETWORK);
    XPerformanceOverlay_setMemoryVisible(overlay, OV_DEFAULT_MEMORY);
    XPerformanceOverlay_setMemoryDisplay(
        overlay, (XPerformanceOverlayMemoryDisplay)OV_DEFAULT_MEM_DISPLAY);
    XPerformanceOverlay_setMemorySource(
        overlay, (XPerformanceOverlayMemorySource)OV_DEFAULT_MEM_SOURCE);
    XPerformanceOverlay_setAutoFitSize(overlay, OV_DEFAULT_AUTOFIT);
    XPerformanceOverlay_setMovable(overlay, OV_DEFAULT_MOVABLE);
    XPerformanceOverlay_setFixed(overlay, OV_DEFAULT_FIXED);
    XPerformanceOverlay_setTextPixelSize(overlay, OV_DEFAULT_FONT_PX);
    XWidget_setVisible((XWidget*)overlay, OV_DEFAULT_VISIBLE);
    /* 位置：右下预设（并恢复自动重锚——非右下预设会钉住浮层）。 */
    demo_main_overlay_applyPreset(s_ov.m_user, OV_DEFAULT_PRESET);
    /* 控件回填（无 getter 的两项 + 总开关）；setChecked 会触发 toggled
       槽，全程持 m_syncing 抑制。 */
    s_ov.m_syncing = true;
    if (s_ov.m_presetCombo)
        XComboBox_setCurrentIndex(s_ov.m_presetCombo, OV_DEFAULT_PRESET);
    if (s_ov.m_fontCombo)
        XComboBox_setCurrentIndex(s_ov.m_fontCombo,
                                  ov_fontIndexOf(OV_DEFAULT_FONT_PX));
    if (s_ov.m_masterCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_masterCheck,
                                   OV_DEFAULT_VISIBLE);
    s_ov.m_syncing = false;
    ov_syncFromOverlay();
    /* 默认值一并落盘（否则配置里留着旧的非默认值，下次启动又被恢复回来）。 */
    ov_saveSettings();
    /* [文案修复 2026-11-10] 原字符串被二次编码损坏成乱码
     * （"宸叉仮澶嶉粯璁¤"），按本行注释里的正确文案还原。 */
    ov_report("已恢复默认"); /* 已恢复默认 */
}

/* ==================== 装配 ==================== */

/** @brief 组装一个分组标题（13px 小字；高度固定 20，宽度随文字）。 */
static XLabel* ov_buildCaption(const char* text)
{
    XLabel* label = XLabel_create(s_ov.m_root, 0);
    if (label) {
        XLabel_setText_2(label, text);
        XLabel_setTextPixelSize(label, 13);
        XWidget_setFixedHeight((XWidget*)label, 20);
        XWidget_show((XWidget*)label);
    }
    return label;
}

/** @brief 组装一个开关复选框（宽 w 高 22）。 */
static XCheckBox* ov_buildCheck(const char* text, int w, XSlotFunc2 slot)
{
    XCheckBox* check = XCheckBox_create(s_ov.m_root, 0);
    if (check) {
        XAbstractButton_setText_2((XAbstractButton*)check, text);
        XWidget_setFixedWidth((XWidget*)check, w);
        XWidget_setFixedHeight((XWidget*)check, 22);
        if (slot)
            XObject_connect_2((XObject*)check,
                              XSignal(XAbstractButton_toggled_signal), slot);
        XWidget_show((XWidget*)check);
    }
    return check;
}

XWidget* hsf_page_overlay_build(XWidget* parent,
                                          DemoPageStatusFn status, void* user)
{
    /* demo 单实例：重复 build 前清空登记表（旧页面随旧父链析构）。 */
    memset(&s_ov, 0, sizeof(s_ov));
    s_ov.m_status = status;
    s_ov.m_user = user;
    if (!parent) return (XWidget*)0;
    if (!demo_main_overlay(user)) {
        XPrintf("XGuiAutoTest: [FAIL] 悬浮窗设置页: 主窗悬浮层不可用"
                "（XGUI_PERFORMANCE_OVERLAY_ON=0？）\n");
        return (XWidget*)0;
    }
    s_ov.m_root = XWidget_create(parent, 0);
    if (!s_ov.m_root) return (XWidget*)0;

    /* ---- 标题 ---- */
    s_ov.m_titleLabel = XLabel_create(s_ov.m_root, 0);
    if (!s_ov.m_titleLabel) return s_ov.m_root;
    XLabel_setText_2(s_ov.m_titleLabel,
                     "悬浮窗设置"); /* 悬浮窗设置 */
    XLabel_setTextPixelSize(s_ov.m_titleLabel, 15);
    XWidget_setFixedHeight((XWidget*)s_ov.m_titleLabel, 24);
    XWidget_show((XWidget*)s_ov.m_titleLabel);

    /* ---- 显示数据 ---- */
    /* ---- 总开关行：显示悬浮窗（整体显隐） ---- */
    s_ov.m_masterCheck = ov_buildCheck(
        "显示悬窗簿", 120,
                                     ov_masterSlot);
    if (s_ov.m_masterCheck) {
        /* 初值按**显式隐藏位**判定：建页时父窗尚未显示，isVisible 的
           生效可见性恒为 false（会误显示为「未开启」）。 */
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_masterCheck,
                                   !XWidget_isHidden((XWidget*)ov_target()));
    }

    s_ov.m_capRows = ov_buildCaption(
        "显示数据");
    s_ov.m_fpsCheck = ov_buildCheck("FPS", 90, ov_fpsSlot);
    s_ov.m_frameCheck = ov_buildCheck(
        "帧耗时", 110,
        ov_frameSlot); /* 帧耗时 */
    s_ov.m_sysCheck = ov_buildCheck("CPU/GPU", 120, ov_sysSlot);
    s_ov.m_netCheck = ov_buildCheck(
        "网络", 100, ov_netSlot); /* 网络 */
    s_ov.m_memCheck = ov_buildCheck(
        "内存", 100, ov_memSlot); /* 内存 */

    /* ---- 内存行 ---- */
    s_ov.m_capMem = ov_buildCaption(
        "内存行"); /* 内存行 */
    s_ov.m_capMemDisp = XLabel_create(s_ov.m_root, 0);
    if (s_ov.m_capMemDisp) {
        XLabel_setText_2(s_ov.m_capMemDisp,
                         "显示种类"); /* 显示种类 */
        XWidget_setFixedWidth((XWidget*)s_ov.m_capMemDisp, 64);
        XWidget_setFixedHeight((XWidget*)s_ov.m_capMemDisp, 22);
        XWidget_show((XWidget*)s_ov.m_capMemDisp);
    }
    s_ov.m_memDispCombo = XComboBox_create(s_ov.m_root, 0);
    if (!s_ov.m_memDispCombo) return s_ov.m_root;
    XComboBox_addItem_2(s_ov.m_memDispCombo,
                        "准确数"); /* 准确数 */
    XComboBox_addItem_2(s_ov.m_memDispCombo,
                        "百分比"); /* 百分比 */
    XComboBox_addItem_2(s_ov.m_memDispCombo,
                        "两者都显示"); /* 两者都显示 */
    XWidget_setFixedWidth((XWidget*)s_ov.m_memDispCombo, 170);
    XWidget_setFixedHeight((XWidget*)s_ov.m_memDispCombo, 26);
    XObject_connect_2((XObject*)s_ov.m_memDispCombo,
                      XSignal(XComboBox_activated_signal), ov_memDispSlot);
    XWidget_show((XWidget*)s_ov.m_memDispCombo);
    s_ov.m_capMemSrc = XLabel_create(s_ov.m_root, 0);
    if (s_ov.m_capMemSrc) {
        XLabel_setText_2(s_ov.m_capMemSrc,
                         "数据来源"); /* 数据来源 */
        XWidget_setFixedWidth((XWidget*)s_ov.m_capMemSrc, 64);
        XWidget_setFixedHeight((XWidget*)s_ov.m_capMemSrc, 22);
        XWidget_show((XWidget*)s_ov.m_capMemSrc);
    }
    s_ov.m_memSrcCombo = XComboBox_create(s_ov.m_root, 0);
    if (!s_ov.m_memSrcCombo) return s_ov.m_root;
    XComboBox_addItem_2(s_ov.m_memSrcCombo,
                        "自动"); /* 自动 */
    XComboBox_addItem_2(s_ov.m_memSrcCombo,
                        "系统内存");
                        /* 系统内存 */
    XComboBox_addItem_2(s_ov.m_memSrcCombo,
                        "库内内存");
                        /* 库内内存 */
    XWidget_setFixedWidth((XWidget*)s_ov.m_memSrcCombo, 190);
    XWidget_setFixedHeight((XWidget*)s_ov.m_memSrcCombo, 26);
    XObject_connect_2((XObject*)s_ov.m_memSrcCombo,
                      XSignal(XComboBox_activated_signal), ov_memSrcSlot);
    XWidget_show((XWidget*)s_ov.m_memSrcCombo);

    /* ---- 位置 ---- */
    s_ov.m_capPos = ov_buildCaption(
        "位置"); /* 位置 */
    s_ov.m_capPreset = XLabel_create(s_ov.m_root, 0);
    if (s_ov.m_capPreset) {
        XLabel_setText_2(s_ov.m_capPreset,
                         "预设位置"); /* 预设位置 */
        XWidget_setFixedWidth((XWidget*)s_ov.m_capPreset, 64);
        XWidget_setFixedHeight((XWidget*)s_ov.m_capPreset, 22);
        XWidget_show((XWidget*)s_ov.m_capPreset);
    }
    s_ov.m_presetCombo = XComboBox_create(s_ov.m_root, 0);
    if (!s_ov.m_presetCombo) return s_ov.m_root;
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "左上");   /* 左上 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "上中");   /* 上中 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "右上");   /* 右上 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "左中");   /* 左中 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "居中");   /* 居中 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "右中");   /* 右中 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "左下");   /* 左下 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "下中");   /* 下中 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "右下");   /* 右下 */
    XComboBox_setCurrentIndex(s_ov.m_presetCombo, 8);
    XWidget_setFixedWidth((XWidget*)s_ov.m_presetCombo, 170);
    XWidget_setFixedHeight((XWidget*)s_ov.m_presetCombo, 26);
    XObject_connect_2((XObject*)s_ov.m_presetCombo,
                      XSignal(XComboBox_activated_signal), ov_presetSlot);
    XWidget_show((XWidget*)s_ov.m_presetCombo);
    s_ov.m_fixedCheck = ov_buildCheck(
        "固定位置", 110,
        ov_fixedSlot); /* 固定位置 */
    s_ov.m_movableCheck = ov_buildCheck(
        "允许拖动", 110,
        ov_movableSlot); /* 允许拖动 */

    /* ---- 外观 ---- */
    s_ov.m_capStyle = ov_buildCaption(
        "外观"); /* 外观 */
    s_ov.m_capFont = XLabel_create(s_ov.m_root, 0);
    if (s_ov.m_capFont) {
        XLabel_setText_2(s_ov.m_capFont,
                         "字号"); /* 字号 */
        XWidget_setFixedWidth((XWidget*)s_ov.m_capFont, 64);
        XWidget_setFixedHeight((XWidget*)s_ov.m_capFont, 22);
        XWidget_show((XWidget*)s_ov.m_capFont);
    }
    s_ov.m_fontCombo = XComboBox_create(s_ov.m_root, 0);
    if (!s_ov.m_fontCombo) return s_ov.m_root;
    XComboBox_addItem_2(s_ov.m_fontCombo, "12");
    XComboBox_addItem_2(s_ov.m_fontCombo, "13");
    XComboBox_addItem_2(s_ov.m_fontCombo, "14");
    XComboBox_addItem_2(s_ov.m_fontCombo, "16");
    XComboBox_addItem_2(s_ov.m_fontCombo, "18");
    XComboBox_setCurrentIndex(s_ov.m_fontCombo,
                              ov_fontIndexOf(OV_DEFAULT_FONT_PX));
    XWidget_setFixedWidth((XWidget*)s_ov.m_fontCombo, 120);
    XWidget_setFixedHeight((XWidget*)s_ov.m_fontCombo, 26);
    XObject_connect_2((XObject*)s_ov.m_fontCombo,
                      XSignal(XComboBox_activated_signal), ov_fontSlot);
    XWidget_show((XWidget*)s_ov.m_fontCombo);
    s_ov.m_autofitCheck = ov_buildCheck(
        "自适应尺寸",
        140, ov_autofitSlot); /* 自适应尺寸 */

    /* ---- 操作与反馈 ---- */
    s_ov.m_resetBtn = XPushButton_create(s_ov.m_root, 0);
    if (!s_ov.m_resetBtn) return s_ov.m_root;
    XPushButton_setText_2(s_ov.m_resetBtn,
                          "复位默认"); /* 复位默认 */
    XWidget_setFixedWidth((XWidget*)s_ov.m_resetBtn, 110);
    XWidget_setFixedHeight((XWidget*)s_ov.m_resetBtn, 30);
    XObject_connect_2((XObject*)s_ov.m_resetBtn,
                      XSignal(XAbstractButton_clicked_signal), ov_resetSlot);
    XWidget_show((XWidget*)s_ov.m_resetBtn);
    s_ov.m_hintLabel = XLabel_create(s_ov.m_root, 0);
    if (s_ov.m_hintLabel) {
        XLabel_setText_2(s_ov.m_hintLabel,
                         "改动即时生效；选预设位置后窗口缩放不再自动贴右下角");
                         /* 改动即时生效；选预设位置后窗口缩放不再自动贴右下角 */
        XLabel_setTextPixelSize(s_ov.m_hintLabel, 12);
        XWidget_setFixedHeight((XWidget*)s_ov.m_hintLabel, 20);
        XWidget_show((XWidget*)s_ov.m_hintLabel);
    }
    s_ov.m_statusLabel = XLabel_create(s_ov.m_root, 0);
    if (!s_ov.m_statusLabel) return s_ov.m_root;
    XLabel_setText_2(s_ov.m_statusLabel,
                     "就绪"); /* 就绪 */
    XWidget_setFixedHeight((XWidget*)s_ov.m_statusLabel, 22);
    XWidget_show((XWidget*)s_ov.m_statusLabel);

    /* ---- 布局装配（2026-10-17 容器化）：根垂直布局六组----
     * [标题][显示数据][内存行][位置][外观][复位+伸展+提示/状态贴底]；
     * 段间 addSpacing 近似原设计稿节奏（8/4/10/4/6/4/6/4/14），行内
     * 间距用行级 spacing/加间距，行高由各控件固定高承载。 */
    XBoxLayout_init(&s_ov.m_rootLayout, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&s_ov.m_rootLayout, 0);
    XLayout_setContentsMargins((XLayout*)&s_ov.m_rootLayout, 12, 8, 12, 8);
    XBoxLayout_addWidget(&s_ov.m_rootLayout, (XWidget*)s_ov.m_titleLabel);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 8);
    /* 总开关行：显示悬浮窗（行尾伸展）。 */
    XBoxLayout_init(&s_ov.m_masterRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_ov.m_masterRow, 8);
    XLayout_setContentsMargins((XLayout*)&s_ov.m_masterRow, 12, 0, 0, 0);
    if (s_ov.m_masterCheck)
        XBoxLayout_addWidget(&s_ov.m_masterRow,
                             (XWidget*)s_ov.m_masterCheck);
    XBoxLayout_addStretch(&s_ov.m_masterRow, 1);
    XBoxLayout_addLayout(&s_ov.m_rootLayout, (XLayout*)&s_ov.m_masterRow);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 8);
    XBoxLayout_addWidget(&s_ov.m_rootLayout, (XWidget*)s_ov.m_capRows);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 4);
    /* 显示数据行：行左缩进 12（对齐原 x=24 起排），五开关等距 10。 */
    XBoxLayout_init(&s_ov.m_rowsRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_ov.m_rowsRow, 10);
    XLayout_setContentsMargins((XLayout*)&s_ov.m_rowsRow, 12, 0, 0, 0);
    XBoxLayout_addWidget(&s_ov.m_rowsRow, (XWidget*)s_ov.m_fpsCheck);
    XBoxLayout_addWidget(&s_ov.m_rowsRow, (XWidget*)s_ov.m_frameCheck);
    XBoxLayout_addWidget(&s_ov.m_rowsRow, (XWidget*)s_ov.m_sysCheck);
    XBoxLayout_addWidget(&s_ov.m_rowsRow, (XWidget*)s_ov.m_netCheck);
    XBoxLayout_addWidget(&s_ov.m_rowsRow, (XWidget*)s_ov.m_memCheck);
    /* 行尾伸展：余量留行尾，保持行内左对齐原节奏。 */
    XBoxLayout_addStretch(&s_ov.m_rowsRow, 1);
    XBoxLayout_addLayout(&s_ov.m_rootLayout, (XLayout*)&s_ov.m_rowsRow);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 10);
    XBoxLayout_addWidget(&s_ov.m_rootLayout, (XWidget*)s_ov.m_capMem);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 4);
    /* 内存行：显示种类|6|下拉 · 32 · 数据来源|6|下拉（VC 对齐 26 行高）。 */
    XBoxLayout_init(&s_ov.m_memRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_ov.m_memRow, 0);
    XLayout_setContentsMargins((XLayout*)&s_ov.m_memRow, 12, 0, 0, 0);
    XBoxLayout_addWidget(&s_ov.m_memRow, (XWidget*)s_ov.m_capMemDisp);
    XBoxLayout_addSpacing(&s_ov.m_memRow, 6);
    XBoxLayout_addWidget(&s_ov.m_memRow, (XWidget*)s_ov.m_memDispCombo);
    XBoxLayout_addSpacing(&s_ov.m_memRow, 32);
    XBoxLayout_addWidget(&s_ov.m_memRow, (XWidget*)s_ov.m_capMemSrc);
    XBoxLayout_addSpacing(&s_ov.m_memRow, 6);
    XBoxLayout_addWidget(&s_ov.m_memRow, (XWidget*)s_ov.m_memSrcCombo);
    /* 行尾伸展：余量留行尾，保持行内左对齐原节奏。 */
    XBoxLayout_addStretch(&s_ov.m_memRow, 1);
    XBoxLayout_addLayout(&s_ov.m_rootLayout, (XLayout*)&s_ov.m_memRow);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 6);
    XBoxLayout_addWidget(&s_ov.m_rootLayout, (XWidget*)s_ov.m_capPos);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 4);
    /* 位置行：预设位置|6|下拉 · 36 · 固定|10 · 允许拖动。 */
    XBoxLayout_init(&s_ov.m_posRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_ov.m_posRow, 0);
    XLayout_setContentsMargins((XLayout*)&s_ov.m_posRow, 12, 0, 0, 0);
    XBoxLayout_addWidget(&s_ov.m_posRow, (XWidget*)s_ov.m_capPreset);
    XBoxLayout_addSpacing(&s_ov.m_posRow, 6);
    XBoxLayout_addWidget(&s_ov.m_posRow, (XWidget*)s_ov.m_presetCombo);
    XBoxLayout_addSpacing(&s_ov.m_posRow, 36);
    XBoxLayout_addWidget(&s_ov.m_posRow, (XWidget*)s_ov.m_fixedCheck);
    XBoxLayout_addSpacing(&s_ov.m_posRow, 10);
    XBoxLayout_addWidget(&s_ov.m_posRow, (XWidget*)s_ov.m_movableCheck);
    /* 行尾伸展：余量留行尾，保持行内左对齐原节奏。 */
    XBoxLayout_addStretch(&s_ov.m_posRow, 1);
    XBoxLayout_addLayout(&s_ov.m_rootLayout, (XLayout*)&s_ov.m_posRow);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 6);
    XBoxLayout_addWidget(&s_ov.m_rootLayout, (XWidget*)s_ov.m_capStyle);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 4);
    /* 外观行：字号|6|下拉 · 26 · 自适应尺寸。 */
    XBoxLayout_init(&s_ov.m_styleRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_ov.m_styleRow, 0);
    XLayout_setContentsMargins((XLayout*)&s_ov.m_styleRow, 12, 0, 0, 0);
    XBoxLayout_addWidget(&s_ov.m_styleRow, (XWidget*)s_ov.m_capFont);
    XBoxLayout_addSpacing(&s_ov.m_styleRow, 6);
    XBoxLayout_addWidget(&s_ov.m_styleRow, (XWidget*)s_ov.m_fontCombo);
    XBoxLayout_addSpacing(&s_ov.m_styleRow, 26);
    XBoxLayout_addWidget(&s_ov.m_styleRow,
                           (XWidget*)s_ov.m_autofitCheck);
    /* 行尾伸展：余量留行尾，保持行内左对齐原节奏。 */
    XBoxLayout_addStretch(&s_ov.m_styleRow, 1);
    XBoxLayout_addLayout(&s_ov.m_rootLayout, (XLayout*)&s_ov.m_styleRow);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 14);
    XBoxLayout_addWidget(&s_ov.m_rootLayout, (XWidget*)s_ov.m_resetBtn);
    XBoxLayout_addStretch(&s_ov.m_rootLayout, 1);
    XBoxLayout_addWidget(&s_ov.m_rootLayout, (XWidget*)s_ov.m_hintLabel);
    XBoxLayout_addSpacing(&s_ov.m_rootLayout, 6);
    XBoxLayout_addWidget(&s_ov.m_rootLayout,
                         (XWidget*)s_ov.m_statusLabel);
    XWidget_setLayout(s_ov.m_root, (XLayout*)&s_ov.m_rootLayout);

    /* ---- 控件初值 ← 悬浮层当前态 ---- */
    ov_syncFromOverlay();

    s_ov.m_ready = true;
    return s_ov.m_root;
}

/* ==================== 自适应重排（xgui_demo_pages.h 契约） ============ */

/*
 * 竖屏窄宽分档（2026-10-25 真机截图体检）：容器化后行内排布全交布局
 * 器，但四个水平行的子件都是固定定尺，桌面口径合计最宽 584（显示数据
 * 行：根边距 24 + 行左缩进 12 + 五开关 520 + 行距 40），竖屏 400 逻辑
 * 宽放不下——「网络/内存」开关、「数据来源」下拉、「允许拖动」开关整
 * 段落到屏外不可见不可点。以页根宽 600 分档：≥600 交回桌面定尺（最小
 * 需求 584 也放得下，两档拼接无死角），<600 走窄档收缩。窄档不动结构
 * （行条目仅原地改定尺/空白尺寸），只把四行算进 400 口径的可视宽内，
 * 并把可点控件高度统一抬到 40（手机触控目标下限；CheckBox 命中整个
 * 控件矩形，控件高即触控高）。竖向余量交页根既有竖排承载，不再手写
 * 几何。
 */

/** @brief 窄宽分档阈值：桌面档最宽行需 584，600 以下走窄档定尺。 */
#define OV_ADAPT_NARROW_WIDTH 600

/** @brief 窄档可点控件高度下限（手机触控目标，逻辑像素）。 */
#define OV_ADAPT_TOUCH_HEIGHT 40

/** @brief 把行内指定位置的固定空白换成新尺寸。
 *  @details 行条目顺序在 build 装配段定死（addSpacing 落位即索引）；
 *  take 出旧空白删除后原位补回，条目总数与次序不变，档位来回切可重入。 */
static void ov_setRowGap(XBoxLayout* row, int index, int gap)
{
    XLayoutItem* item;
    if (!row) return;
    item = XLayout_takeAt_base((XLayout*)row, index);
    if (!item) return;
    XClassDelete(item);
    XBoxLayout_insertSpacing(row, index, gap);
}

/** @brief 按档位落四条水平行的定尺与触控高度（narrow=竖屏窄档）。
 *  @details 窄档数值按 400 口径整算：行可用宽 = 页宽 - 根边距 24 -
 *  行左缩进 12 = 364，四行合计 350/358/362/266 全部落位；文字宽按最
 *  大 16px 位图字预留（下拉文本区 = 宽-24、复选文本自 x≈21 起），不
 *  赌具体字号。宽档数值与 build 装配段逐一对应，改装配须同步这里。 */
static void ov_applyTierGeometry(bool narrow)
{
    int chkH = narrow ? OV_ADAPT_TOUCH_HEIGHT : 22;
    int comboH = narrow ? OV_ADAPT_TOUCH_HEIGHT : 26;

    /* 「显示数据」行：五开关收窄+行距 10→6，合计 350 ≤ 364。 */
    if (s_ov.m_fpsCheck) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_fpsCheck, narrow ? 50 : 90);
        XWidget_setFixedHeight((XWidget*)s_ov.m_fpsCheck, chkH);
    }
    if (s_ov.m_frameCheck) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_frameCheck,
                              narrow ? 72 : 110);
        XWidget_setFixedHeight((XWidget*)s_ov.m_frameCheck, chkH);
    }
    if (s_ov.m_sysCheck) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_sysCheck, narrow ? 88 : 120);
        XWidget_setFixedHeight((XWidget*)s_ov.m_sysCheck, chkH);
    }
    if (s_ov.m_netCheck) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_netCheck, narrow ? 58 : 100);
        XWidget_setFixedHeight((XWidget*)s_ov.m_netCheck, chkH);
    }
    if (s_ov.m_memCheck) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_memCheck, narrow ? 58 : 100);
        XWidget_setFixedHeight((XWidget*)s_ov.m_memCheck, chkH);
    }
    XLayout_setSpacing((XLayout*)&s_ov.m_rowsRow, narrow ? 6 : 10);
    /* [窄档翻竖排 2026-10-27 视觉走查 P3-I] 五开关横排 400 宽装不下
     * （「CPU/GPU」与「网络」间距被压到 0），窄档纵列零拥挤。 */
    XBoxLayout_setDirection(&s_ov.m_rowsRow,
        narrow ? XBoxLayoutDirection_TopToBottom
               : XBoxLayoutDirection_LeftToRight);

    /* 「内存行」：中缝 32→12，两下拉 110/96，合计 358 ≤ 364。 */
    ov_setRowGap(&s_ov.m_memRow, 3, narrow ? 12 : 32);
    if (s_ov.m_memDispCombo) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_memDispCombo,
                              narrow ? 110 : 170);
        XWidget_setFixedHeight((XWidget*)s_ov.m_memDispCombo, comboH);
    }
    if (s_ov.m_memSrcCombo) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_memSrcCombo,
                              narrow ? 96 : 190);
        XWidget_setFixedHeight((XWidget*)s_ov.m_memSrcCombo, comboH);
    }

    /* 「位置」行：中缝 36→12、开关缝 10→8，预设下拉 100、两开关 86，
     * 合计 362 ≤ 364。 */
    ov_setRowGap(&s_ov.m_posRow, 3, narrow ? 12 : 36);
    ov_setRowGap(&s_ov.m_posRow, 5, narrow ? 8 : 10);
    if (s_ov.m_presetCombo) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_presetCombo,
                              narrow ? 100 : 170);
        XWidget_setFixedHeight((XWidget*)s_ov.m_presetCombo, comboH);
    }
    if (s_ov.m_fixedCheck) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_fixedCheck,
                              narrow ? 86 : 110);
        XWidget_setFixedHeight((XWidget*)s_ov.m_fixedCheck, chkH);
    }
    if (s_ov.m_movableCheck) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_movableCheck,
                              narrow ? 86 : 110);
        XWidget_setFixedHeight((XWidget*)s_ov.m_movableCheck, chkH);
    }

    /* 「外观」行：桌面口径在 ≥600 分档内本就落位，窄档同节奏收窄，
     * 与其余三行保持同一触控/密度口径。 */
    ov_setRowGap(&s_ov.m_styleRow, 3, narrow ? 12 : 26);
    if (s_ov.m_fontCombo) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_fontCombo, narrow ? 64 : 120);
        XWidget_setFixedHeight((XWidget*)s_ov.m_fontCombo, comboH);
    }
    if (s_ov.m_autofitCheck) {
        XWidget_setFixedWidth((XWidget*)s_ov.m_autofitCheck,
                              narrow ? 120 : 140);
        XWidget_setFixedHeight((XWidget*)s_ov.m_autofitCheck, chkH);
    }

    /* 复位钮两档同宽，窄档只抬高度凑触控目标。 */
    if (s_ov.m_resetBtn)
        XWidget_setFixedHeight((XWidget*)s_ov.m_resetBtn,
                               narrow ? OV_ADAPT_TOUCH_HEIGHT : 30);
}

void hsf_page_overlay_adapt(XWidget* page)
{
    int w;
    bool narrow;
    /* 容器化后常态排布仍全交根布局器，本入口只做窄宽分档：页根几何
     * 未定（首拍/非当前堆叠页宽为 0）不动定尺，保 build 桌面口径；
     * 档位未变免重排。定尺/空白改动后显式失效并立即重解算——留到
     * 下一拍事件激活会闪一帧旧几何。 */
    if (!page || page != s_ov.m_root || !s_ov.m_ready) return;
    w = (int)XWidget_width(page);
    if (w <= 0) return;
    narrow = w < OV_ADAPT_NARROW_WIDTH;
    if (narrow == s_ov.m_tierNarrow) return;
    s_ov.m_tierNarrow = narrow;
    ov_applyTierGeometry(narrow);
    XLayout_invalidate((XLayout*)&s_ov.m_rootLayout);
    XLayout_activate((XLayout*)&s_ov.m_rootLayout);
}

/* ==================== autotest ==================== */

int hsf_page_overlay_autotest(XWidget* page)
{
    int failures = 0;
    XPerformanceOverlay* overlay;
#define OV_EXPECT(cond, what) \
    do { if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
         else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } } while (0)

    if (!page || !s_ov.m_ready || page != s_ov.m_root)
        return -1;
    overlay = ov_target();
    OV_EXPECT(overlay != NULL, "悬浮窗设置页: 主窗悬浮层借用可用");

    /* ---- 控件登记完整 ---- */
    OV_EXPECT(s_ov.m_fpsCheck && s_ov.m_frameCheck && s_ov.m_sysCheck &&
              s_ov.m_netCheck && s_ov.m_memCheck && s_ov.m_memDispCombo &&
              s_ov.m_memSrcCombo && s_ov.m_presetCombo && s_ov.m_fixedCheck &&
              s_ov.m_movableCheck && s_ov.m_fontCombo &&
              s_ov.m_autofitCheck && s_ov.m_resetBtn,
              "悬浮窗设置页: 控件全部登记");

    /* ---- 行开关：翻转 → getter 回读 → 复原 ---- */
    if (overlay) {
        bool before = XPerformanceOverlay_isFpsVisible(overlay);
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_fpsCheck, !before);
        ov_fpsSlot(NULL, NULL);
        OV_EXPECT(XPerformanceOverlay_isFpsVisible(overlay) == !before,
                  "悬浮窗设置页: FPS 开关生效");
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_fpsCheck, before);
        ov_fpsSlot(NULL, NULL);
        OV_EXPECT(XPerformanceOverlay_isFpsVisible(overlay) == before,
                  "悬浮窗设置页: FPS 开关复原");

        before = XPerformanceOverlay_isMemoryVisible(overlay);
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_memCheck,
                                   !before);
        ov_memSlot(NULL, NULL);
        OV_EXPECT(XPerformanceOverlay_isMemoryVisible(overlay) == !before,
                  "悬浮窗设置页: 内存行开关生效");
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_memCheck, before);
        ov_memSlot(NULL, NULL);

        /* ---- 内存行两组枚举 ---- */
        XComboBox_setCurrentIndex(s_ov.m_memDispCombo, 1);
        ov_memDispSlot(NULL, NULL);
        OV_EXPECT(XPerformanceOverlay_memoryDisplay(overlay) ==
                      XPerformanceOverlayMemoryDisplay_PercentOnly,
                  "悬浮窗设置页: 内存显示种类切换百分比");
        XComboBox_setCurrentIndex(s_ov.m_memDispCombo, 2);
        ov_memDispSlot(NULL, NULL);
        OV_EXPECT(XPerformanceOverlay_memoryDisplay(overlay) ==
                      XPerformanceOverlayMemoryDisplay_Both,
                  "悬浮窗设置页: 内存显示种类切回两者");
        XComboBox_setCurrentIndex(s_ov.m_memSrcCombo, 2);
        ov_memSrcSlot(NULL, NULL);
        OV_EXPECT(XPerformanceOverlay_memorySource(overlay) ==
                      XPerformanceOverlayMemorySource_Library,
                  "悬浮窗设置页: 内存来源切库内");
        XComboBox_setCurrentIndex(s_ov.m_memSrcCombo, 0);
        ov_memSrcSlot(NULL, NULL);

        /* ---- 固定/拖动翻转 → 复原（demo 默认固定+可拖） ---- */
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_fixedCheck,
                                   !XPerformanceOverlay_isFixed(overlay));
        ov_fixedSlot(NULL, NULL);
        OV_EXPECT(XPerformanceOverlay_isFixed(overlay) ==
                      XAbstractButton_isChecked(
                          (XAbstractButton*)s_ov.m_fixedCheck),
                  "悬浮窗设置页: 固定开关与悬浮层同步");
        XAbstractButton_setChecked((XAbstractButton*)s_ov.m_fixedCheck, true);
        ov_fixedSlot(NULL, NULL);
        OV_EXPECT(XPerformanceOverlay_isFixed(overlay),
                  "悬浮窗设置页: 固定态复原(默认锁定)");

        /* ---- 预设位置：换位生效 → 复位右下 ---- */
        {
            XPoint before_pos = XPerformanceOverlay_position(overlay);
            XComboBox_setCurrentIndex(s_ov.m_presetCombo, 0); /* 左上 */
            ov_presetSlot(NULL, NULL);
            OV_EXPECT(XWidget_x((XWidget*)overlay) != before_pos.x ||
                          XWidget_y((XWidget*)overlay) != before_pos.y,
                      "悬浮窗设置页: 预设位置移动悬浮层");
            ov_resetSlot(NULL, NULL);
            OV_EXPECT(XPerformanceOverlay_isFixed(overlay) &&
                          XComboBox_currentIndex(s_ov.m_presetCombo) == 8,
                      "悬浮窗设置页: 复位默认回右下");
        }
    }

#undef OV_EXPECT
    XPrintf("XGuiAutoTest: 悬浮窗设置页 %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}

#else /* 裁剪: 契约桩（符号常在, 主文件可无条件链接） */

XWidget* hsf_page_overlay_build(XWidget* parent,
                                          DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    XPrintf("XGuiAutoTest: [FAIL] 悬浮窗设置页依赖悬浮层/控件被裁剪，"
            "无法构建\n");
    return (XWidget*)0;
}

int hsf_page_overlay_autotest(XWidget* page)
{
    (void)page;
    XPrintf("XGuiAutoTest: [FAIL] 悬浮窗设置页被裁剪，无法自测\n");
    return -1;
}

void hsf_page_overlay_adapt(XWidget* page)
{
    (void)page;
}

void hsf_page_overlay_restoreSettings(void)
{
    /* 页面被裁剪：无悬浮层可恢复（主窗也不会建悬浮层）。 */
}

#endif /* XGUI_PERFORMANCE_OVERLAY_ON && 控件段 */

/* ==================== 本框架创建入口 ==================== */

#include "HsfOverlayPage.h"

XWidget* HsfOverlayPage_create(XWidget* parent, HsfMenuNavFn navFn,
                               void* userData)
{
    /* 直接返回 build 的页根（页根即堆叠条目，也是 adapt 钩子的入参
       ——移植页的窄宽分档按页根宽度判定）。 */
    (void)navFn;
    return hsf_page_overlay_build(parent, HsfPages_setStatus, userData);
}
