/******************************************************************************
 * @file       xgui_demo_page_overlay_settings.c
 * @brief      XGuiWindowDemo「悬浮窗设置」页（系统设置：性能悬浮层属性）。
 * @details    契约见 xgui_demo_pages.h（demo_page_overlay_settings_build /
 *             demo_page_overlay_settings_autotest / adapt）。
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
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "XGuiConfig.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XWidget.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XComboBox.h"
#include "XPushButton.h"
#include "XPerformanceOverlay.h"
#include "xgui_demo_pages.h"

/*
 * 控件段宏守卫：悬浮层编译开关 + CheckBox/ComboBox/PushButton/Label。
 * 任一被裁剪时降级为契约桩（build 返回 NULL、autotest 返回 -1）。
 */
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON && \
    XCHECKBOX_ON && XCOMBOBOX_ON && XPUSHBUTTON_ON

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
    XPushButton* m_resetBtn;     /**< 复位默认。 */
    XLabel*      m_hintLabel;    /**< 说明行（钉底上方）。 */
    XLabel*      m_statusLabel;  /**< 操作结果行（钉底）。 */

    bool         m_syncing;      /**< 回读同步中（抑制状态行刷屏）。 */
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
    ov_report(checked ? "\xE6\x98\xBE\xE7\xA4\xBA FPS"
                      : "\xE9\x9A\x90\xE8\x97\x8F FPS"); /* 显示/隐藏 FPS */
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
    ov_report(checked ? "\xE6\x98\xBE\xE7\xA4\xBA\xE5\xB8\xA7\xE8\x80\x97"
                      : "\xE9\x9A\x90\xE8\x97\x8F\xE5\xB8\xA7\xE8\x80\x97");
                      /* 显示/隐藏帧耗时 */
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
    ov_report(checked ? "\xE6\x98\xBE\xE7\xA4\xBA CPU/GPU"
                      : "\xE9\x9A\x90\xE8\x97\x8F CPU/GPU");
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
    ov_report(checked ? "\xE6\x98\xBE\xE7\xA4\xBA\xE7\xBD\x91\xE7\xBB\x9C"
                      : "\xE9\x9A\x90\xE8\x97\x8F\xE7\xBD\x91\xE7\xBB\x9C");
                      /* 显示/隐藏网络 */
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
    ov_report(checked ? "\xE6\x98\xBE\xE7\xA4\xBA\xE5\x86\x85\xE5\xAD\x98"
                      : "\xE9\x9A\x90\xE8\x97\x8F\xE5\x86\x85\xE5\xAD\x98");
                      /* 显示/隐藏内存 */
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
    ov_report("\xE5\x86\x85\xE5\xAD\x98\xE8\xA1\x8C\xE6\x98\xBE\xE7\xA4"
              "\xBA\xE7\xA7\x8D\xE7\xB1\xBB\xE5\xB7\xB2\xE6\x9B\xB4\xE6"
              "\x8D\xA2"); /* 内存行显示种类已更换 */
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
    ov_report("\xE5\x86\x85\xE5\xAD\x98\xE6\x95\xB0\xE6\x8D\xAE\xE6\x9D"
              "\xA5\xE6\xBA\x90\xE5\xB7\xB2\xE6\x9B\xB4\xE6\x8D\xA2");
              /* 内存数据来源已更换 */
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
    ov_report("\xE5\xB7\xB2\xE5\xBA\x94\xE7\x94\xA8\xE9\xA2\x84\xE8\xAE"
              "\xBE\xE4\xBD\x8D\xE7\xBD\xAE"); /* 已应用预设位置 */
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
    ov_report(checked ? "\xE5\xB7\xB2\xE9\x94\x81\xE5\xAE\x9A\xE4\xBD\x8D"
                      "\xE7\xBD\xAE" : "\xE5\xB7\xB2\xE8\xA7\xA3\xE9\x99"
                      "\xA4\xE5\x9B\xBA\xE5\xAE\x9A"); /* 已锁定位置/已解除固定 */
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
    ov_report(checked ? "\xE5\xB7\xB2\xE5\x85\x81\xE8\xAE\xB8\xE6\x8B\x96"
                      "\xE5\x8A\xA8" : "\xE5\xB7\xB2\xE7\xA6\x81\xE6\xAD"
                      "\xA2\xE6\x8B\x96\xE5\x8A\xA8"); /* 已允许拖动/已禁止拖动 */
}

/** @brief 字号切换。 */
static void ov_fontSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    static const int kSizes[5] = { 12, 13, 14, 16, 18 };
    int index;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay || !s_ov.m_fontCombo || s_ov.m_syncing) return;
    index = XComboBox_currentIndex(s_ov.m_fontCombo);
    if (index < 0 || index > 4) index = 3;
    XPerformanceOverlay_setTextPixelSize(overlay, kSizes[index]);
    ov_report("\xE5\xAD\x97\xE5\x8F\xB7\xE5\xB7\xB2\xE6\x9B\xB4\xE6\x8D"
              "\xA2"); /* 字号已更换 */
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
    ov_report(checked ? "\xE5\xB7\xB2\xE5\xBC\x80\xE5\x90\xAF\xE8\x87\xAA"
                      "\xE9\x80\x82\xE5\xBA\x94\xE5\xB0\xBA\xE5\xAF\xB8"
                      : "\xE5\xB7\xB2\xE5\x85\xB3\xE9\x97\xAD\xE8\x87\xAA"
                        "\xE9\x80\x82\xE5\xBA\x94"); /* 已开启自适应尺寸/已关闭自适应 */
}

/** @brief 复位默认：悬浮层 reset + 控件回读 + 恢复自动重锚。 */
static void ov_resetSlot(XObject* receiver, XVarList* args)
{
    XPerformanceOverlay* overlay;
    (void)receiver; (void)args;
    overlay = ov_target();
    if (!overlay) return;
    XPerformanceOverlay_reset(overlay);
    demo_main_overlay_applyPreset(
        s_ov.m_user, (int)XPerformanceOverlayPosition_BottomRight);
    /* 下拉框无对应 getter 的项随复位回默认档位。 */
    if (s_ov.m_presetCombo)
        XComboBox_setCurrentIndex(s_ov.m_presetCombo, 8); /* 右下 */
    if (s_ov.m_fontCombo)
        XComboBox_setCurrentIndex(s_ov.m_fontCombo, 3); /* 16 */
    ov_syncFromOverlay();
    ov_report("\xE5\xB7\xB2\xE6\x81\xA2\xE5\xA4\x8D\xE9\xBB\x98\xE8\xAE"
              "\xA4"); /* 已恢复默认 */
}

/* ==================== 装配 ==================== */

/** @brief 组装一个分组标题行（13px 小字）。 */
static XLabel* ov_buildCaption(const char* text, int x, int y)
{
    XLabel* label = XLabel_create(s_ov.m_root, 0);
    if (label) {
        XLabel_setText_2(label, text);
        XLabel_setTextPixelSize(label, 13);
        XWidget_setGeometry((XWidget*)label, x, y, 320, 20);
        XWidget_show((XWidget*)label);
    }
    return label;
}

/** @brief 组装一个开关复选框。 */
static XCheckBox* ov_buildCheck(const char* text, int x, int y, int w,
                                XSlotFunc2 slot)
{
    XCheckBox* check = XCheckBox_create(s_ov.m_root, 0);
    if (check) {
        XAbstractButton_setText_2((XAbstractButton*)check, text);
        XWidget_setGeometry((XWidget*)check, x, y, w, 22);
        if (slot)
            XObject_connect_2((XObject*)check,
                              XSignal(XAbstractButton_toggled_signal), slot);
        XWidget_show((XWidget*)check);
    }
    return check;
}

XWidget* demo_page_overlay_settings_build(XWidget* parent,
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
                     "\xE6\x82\xAC\xE6\xB5\xAE\xE7\xAA\x97\xE8\xAE\xBE"
                     "\xE7\xBD\xAE"); /* 悬浮窗设置 */
    XLabel_setTextPixelSize(s_ov.m_titleLabel, 15);
    XWidget_setGeometry((XWidget*)s_ov.m_titleLabel, 12, 8, 300, 24);
    XWidget_show((XWidget*)s_ov.m_titleLabel);

    /* ---- 显示数据 ---- */
    s_ov.m_capRows = ov_buildCaption(
        "\xE6\x98\xBE\xE7\xA4\xBA\xE6\x95\xB0\xE6\x8D\xAE", 12, 40);
    s_ov.m_fpsCheck = ov_buildCheck("FPS", 24, 64, 90, ov_fpsSlot);
    s_ov.m_frameCheck = ov_buildCheck(
        "\xE5\xB8\xA7\xE8\x80\x97\xE6\x97\xB6", 124, 64, 110,
        ov_frameSlot); /* 帧耗时 */
    s_ov.m_sysCheck = ov_buildCheck("CPU/GPU", 244, 64, 120, ov_sysSlot);
    s_ov.m_netCheck = ov_buildCheck(
        "\xE7\xBD\x91\xE7\xBB\x9C", 374, 64, 100, ov_netSlot); /* 网络 */
    s_ov.m_memCheck = ov_buildCheck(
        "\xE5\x86\x85\xE5\xAD\x98", 484, 64, 100, ov_memSlot); /* 内存 */

    /* ---- 内存行 ---- */
    s_ov.m_capMem = ov_buildCaption(
        "\xE5\x86\x85\xE5\xAD\x98\xE8\xA1\x8C", 12, 96); /* 内存行 */
    s_ov.m_capMemDisp = XLabel_create(s_ov.m_root, 0);
    if (s_ov.m_capMemDisp) {
        XLabel_setText_2(s_ov.m_capMemDisp,
                         "\xE6\x98\xBE\xE7\xA4\xBA\xE7\xA7\x8D\xE7\xB1"
                         "\xBB"); /* 显示种类 */
        XWidget_setGeometry((XWidget*)s_ov.m_capMemDisp, 24, 122, 64, 22);
        XWidget_show((XWidget*)s_ov.m_capMemDisp);
    }
    s_ov.m_memDispCombo = XComboBox_create(s_ov.m_root, 0);
    if (!s_ov.m_memDispCombo) return s_ov.m_root;
    XComboBox_addItem_2(s_ov.m_memDispCombo,
                        "\xE5\x87\x86\xE7\xA1\xAE\xE6\x95\xB0"); /* 准确数 */
    XComboBox_addItem_2(s_ov.m_memDispCombo,
                        "\xE7\x99\xBE\xE5\x88\x86\xE6\xAF\x94"); /* 百分比 */
    XComboBox_addItem_2(s_ov.m_memDispCombo,
                        "\xE4\xB8\xA4\xE8\x80\x85\xE9\x83\xBD\xE6\x98\xBE"
                        "\xE7\xA4\xBA"); /* 两者都显示 */
    XWidget_setGeometry((XWidget*)s_ov.m_memDispCombo, 94, 120, 170, 26);
    XObject_connect_2((XObject*)s_ov.m_memDispCombo,
                      XSignal(XComboBox_activated_signal), ov_memDispSlot);
    XWidget_show((XWidget*)s_ov.m_memDispCombo);
    s_ov.m_capMemSrc = XLabel_create(s_ov.m_root, 0);
    if (s_ov.m_capMemSrc) {
        XLabel_setText_2(s_ov.m_capMemSrc,
                         "\xE6\x95\xB0\xE6\x8D\xAE\xE6\x9D\xA5\xE6\xBA"
                         "\x90"); /* 数据来源 */
        XWidget_setGeometry((XWidget*)s_ov.m_capMemSrc, 296, 122, 64, 22);
        XWidget_show((XWidget*)s_ov.m_capMemSrc);
    }
    s_ov.m_memSrcCombo = XComboBox_create(s_ov.m_root, 0);
    if (!s_ov.m_memSrcCombo) return s_ov.m_root;
    XComboBox_addItem_2(s_ov.m_memSrcCombo,
                        "\xE8\x87\xAA\xE5\x8A\xA8"); /* 自动 */
    XComboBox_addItem_2(s_ov.m_memSrcCombo,
                        "\xE7\xB3\xBB\xE7\xBB\x9F\xE5\x86\x85\xE5\xAD\x98");
                        /* 系统内存 */
    XComboBox_addItem_2(s_ov.m_memSrcCombo,
                        "\xE5\xBA\x93\xE5\x86\x85\xE5\x86\x85\xE5\xAD\x98");
                        /* 库内内存 */
    XWidget_setGeometry((XWidget*)s_ov.m_memSrcCombo, 366, 120, 190, 26);
    XObject_connect_2((XObject*)s_ov.m_memSrcCombo,
                      XSignal(XComboBox_activated_signal), ov_memSrcSlot);
    XWidget_show((XWidget*)s_ov.m_memSrcCombo);

    /* ---- 位置 ---- */
    s_ov.m_capPos = ov_buildCaption(
        "\xE4\xBD\x8D\xE7\xBD\xAE", 12, 152); /* 位置 */
    s_ov.m_capPreset = XLabel_create(s_ov.m_root, 0);
    if (s_ov.m_capPreset) {
        XLabel_setText_2(s_ov.m_capPreset,
                         "\xE9\xA2\x84\xE8\xAE\xBE\xE4\xBD\x8D\xE7\xBD"
                         "\xAE"); /* 预设位置 */
        XWidget_setGeometry((XWidget*)s_ov.m_capPreset, 24, 178, 64, 22);
        XWidget_show((XWidget*)s_ov.m_capPreset);
    }
    s_ov.m_presetCombo = XComboBox_create(s_ov.m_root, 0);
    if (!s_ov.m_presetCombo) return s_ov.m_root;
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "\xE5\xB7\xA6\xE4\xB8\x8A");   /* 左上 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "\xE4\xB8\x8A\xE4\xB8\xAD");   /* 上中 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "\xE5\x8F\xB3\xE4\xB8\x8A");   /* 右上 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "\xE5\xB7\xA6\xE4\xB8\xAD");   /* 左中 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "\xE5\xB1\x85\xE4\xB8\xAD");   /* 居中 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "\xE5\x8F\xB3\xE4\xB8\xAD");   /* 右中 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "\xE5\xB7\xA6\xE4\xB8\x8B");   /* 左下 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "\xE4\xB8\x8B\xE4\xB8\xAD");   /* 下中 */
    XComboBox_addItem_2(s_ov.m_presetCombo,
                        "\xE5\x8F\xB3\xE4\xB8\x8B");   /* 右下 */
    XComboBox_setCurrentIndex(s_ov.m_presetCombo, 8);
    XWidget_setGeometry((XWidget*)s_ov.m_presetCombo, 94, 176, 170, 26);
    XObject_connect_2((XObject*)s_ov.m_presetCombo,
                      XSignal(XComboBox_activated_signal), ov_presetSlot);
    XWidget_show((XWidget*)s_ov.m_presetCombo);
    s_ov.m_fixedCheck = ov_buildCheck(
        "\xE5\x9B\xBA\xE5\xAE\x9A\xE4\xBD\x8D\xE7\xBD\xAE", 300, 178,
        110, ov_fixedSlot); /* 固定位置 */
    s_ov.m_movableCheck = ov_buildCheck(
        "\xE5\x85\x81\xE8\xAE\xB8\xE6\x8B\x96\xE5\x8A\xA8", 420, 178,
        110, ov_movableSlot); /* 允许拖动 */

    /* ---- 外观 ---- */
    s_ov.m_capStyle = ov_buildCaption(
        "\xE5\xA4\x96\xE8\xA7\x82", 12, 208); /* 外观 */
    s_ov.m_capFont = XLabel_create(s_ov.m_root, 0);
    if (s_ov.m_capFont) {
        XLabel_setText_2(s_ov.m_capFont,
                         "\xE5\xAD\x97\xE5\x8F\xB7"); /* 字号 */
        XWidget_setGeometry((XWidget*)s_ov.m_capFont, 24, 234, 64, 22);
        XWidget_show((XWidget*)s_ov.m_capFont);
    }
    s_ov.m_fontCombo = XComboBox_create(s_ov.m_root, 0);
    if (!s_ov.m_fontCombo) return s_ov.m_root;
    XComboBox_addItem_2(s_ov.m_fontCombo, "12");
    XComboBox_addItem_2(s_ov.m_fontCombo, "13");
    XComboBox_addItem_2(s_ov.m_fontCombo, "14");
    XComboBox_addItem_2(s_ov.m_fontCombo, "16");
    XComboBox_addItem_2(s_ov.m_fontCombo, "18");
    XComboBox_setCurrentIndex(s_ov.m_fontCombo, 3);
    XWidget_setGeometry((XWidget*)s_ov.m_fontCombo, 94, 232, 120, 26);
    XObject_connect_2((XObject*)s_ov.m_fontCombo,
                      XSignal(XComboBox_activated_signal), ov_fontSlot);
    XWidget_show((XWidget*)s_ov.m_fontCombo);
    s_ov.m_autofitCheck = ov_buildCheck(
        "\xE8\x87\xAA\xE9\x80\x82\xE5\xBA\x94\xE5\xB0\xBA\xE5\xAF\xB8",
        240, 234, 140, ov_autofitSlot); /* 自适应尺寸 */

    /* ---- 操作与反馈 ---- */
    s_ov.m_resetBtn = XPushButton_create(s_ov.m_root, 0);
    if (!s_ov.m_resetBtn) return s_ov.m_root;
    XPushButton_setText_2(s_ov.m_resetBtn,
                          "\xE5\xA4\x8D\xE4\xBD\x8D\xE9\xBB\x98\xE8\xAE"
                          "\xA4"); /* 复位默认 */
    XWidget_setGeometry((XWidget*)s_ov.m_resetBtn, 12, 272, 110, 30);
    XObject_connect_2((XObject*)s_ov.m_resetBtn,
                      XSignal(XAbstractButton_clicked_signal), ov_resetSlot);
    XWidget_show((XWidget*)s_ov.m_resetBtn);
    s_ov.m_hintLabel = XLabel_create(s_ov.m_root, 0);
    if (s_ov.m_hintLabel) {
        XLabel_setText_2(s_ov.m_hintLabel,
                         "\xE6\x94\xB9\xE5\x8A\xA8\xE5\x8D\xB3\xE6\x97"
                         "\xB6\xE7\x94\x9F\xE6\x95\x88\xEF\xBC\x9B\xE9\x80"
                         "\x89\xE9\xA2\x84\xE8\xAE\xBE\xE4\xBD\x8D\xE7\xBD"
                         "\xAE\xE5\x90\x8E\xE7\xAA\x97\xE5\x8F\xA3\xE7\xBC"
                         "\xA9\xE6\x94\xBE\xE4\xB8\x8D\xE5\x86\x8D\xE8\x87"
                         "\xAA\xE5\x8A\xA8\xE8\xB4\xB4\xE5\x8F\xB3\xE4\xB8"
                         "\x8B\xE8\xA7\x92");
                         /* 改动即时生效；选预设位置后窗口缩放不再自动贴右下角 */
        XLabel_setTextPixelSize(s_ov.m_hintLabel, 12);
        XWidget_setGeometry((XWidget*)s_ov.m_hintLabel, 12, 312, 660, 20);
        XWidget_show((XWidget*)s_ov.m_hintLabel);
    }
    s_ov.m_statusLabel = XLabel_create(s_ov.m_root, 0);
    if (!s_ov.m_statusLabel) return s_ov.m_root;
    XLabel_setText_2(s_ov.m_statusLabel,
                     "\xE5\xB0\xB1\xE7\xBB\xAA"); /* 就绪 */
    XWidget_setGeometry((XWidget*)s_ov.m_statusLabel, 12, 336, 660, 22);
    XWidget_show((XWidget*)s_ov.m_statusLabel);

    /* ---- 控件初值 ← 悬浮层当前态 ---- */
    ov_syncFromOverlay();

    s_ov.m_ready = true;
    return s_ov.m_root;
}

/* ==================== 自适应重排（xgui_demo_pages.h 契约） ============ */

void demo_page_overlay_settings_adapt(XWidget* page)
{
    int rootW;
    int rootH;
    if (!page || page != s_ov.m_root) return;
    rootW = XWidget_width(page);
    rootH = XWidget_height(page);
    if (rootW < 360 || rootH < 300) return; /* 过窄保持装配几何。 */
    if (s_ov.m_hintLabel)
        XWidget_setGeometry((XWidget*)s_ov.m_hintLabel, 12, rootH - 56,
                            rootW - 24, 20);
    if (s_ov.m_statusLabel)
        XWidget_setGeometry((XWidget*)s_ov.m_statusLabel, 12, rootH - 30,
                            rootW - 24, 22);
}

/* ==================== autotest ==================== */

int demo_page_overlay_settings_autotest(XWidget* page)
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

XWidget* demo_page_overlay_settings_build(XWidget* parent,
                                          DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    XPrintf("XGuiAutoTest: [FAIL] 悬浮窗设置页依赖悬浮层/控件被裁剪，"
            "无法构建\n");
    return (XWidget*)0;
}

int demo_page_overlay_settings_autotest(XWidget* page)
{
    (void)page;
    XPrintf("XGuiAutoTest: [FAIL] 悬浮窗设置页被裁剪，无法自测\n");
    return -1;
}

void demo_page_overlay_settings_adapt(XWidget* page)
{
    (void)page;
}

#endif /* XGUI_PERFORMANCE_OVERLAY_ON && 控件段 */
