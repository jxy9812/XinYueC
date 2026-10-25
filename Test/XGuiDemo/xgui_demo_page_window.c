/******************************************************************************
 * @file       xgui_demo_page_window.c
 * @brief      XGuiWindowDemo「窗口设置」页（系统设置：皮肤/边框/状态栏/
 *             最大化/窗口方向/鼠标显示）。
 * @details    契约见 xgui_demo_pages.h（demo_page_window_settings_build /
 *             demo_page_window_settings_autotest / adapt）。
 *
 *             页面内容（2026-10-17 随「系统设置」二级菜单新增，用户口径：
 *             窗口常用设置单独一个设置界面）：
 *              - 皮肤六种：浅色·现代蓝（默认）/暖沙/青竹 + 深色·墨蓝/
 *                暖橙/森绿——调色板经 XStyleHints_setColorScheme 深浅
 *                翻转（联动机制自动切内置深浅标准组，文本色随调色板），
 *                盒外观经 XStyle_installStyleSheet 换表（1..5 规格表
 *                参数化懒生成），完毕静态场景失效+整窗重绘；
 *              - 边框(标题栏)：XWindowType_FramelessWindowHint 位动态
 *                翻转（XWidget_setWindowFlags 活窗路径即时同步
 *                _MOTIF_WM_HINTS 并经 XWindowDecoration_syncWindow 重算
 *                CSD 保留边距）；开关后经 demo_main_refreshChrome 重排
 *                demo 蓝条/内容区/静态基底（保留边距收放不会自发触发）；
 *              - 状态栏：主窗底部 DemoStatusBar 经主文件访问器显隐
 *                （内容区底部预留 26px 同步收放）；
 *              - 最大化：XWidget_showMaximized/showNormal（进入方向切换
 *                前若最大化先还原，避免几何被最大化接管）；
 *              - 窗口方向四方位：0° 横屏/90° 竖屏/180° 倒置横屏/270°
 *                倒置竖屏=窗口外框几何旋转（90°/270° 宽高对调；0° 基准
 *                尺寸懒捕获共用；按主屏可用几何钳位防越屏）；
 *              - 鼠标光标：XGuiApplication 覆盖光标栈——勾选=清空栈
 *                （确保 Blank override 出栈，fbdev 软件精灵恢复跟随）、
 *                取消勾选=压入 Blank（隐藏，幂等防栈堆积）；
 *              - 「复位默认」一键恢复（全部控件回默认态并重新应用）。
 *
 *             autotest 口径：全程非阻塞，直呼槽/真实控件路径，逐项切换
 *             并断言框架 getter 回读，结束恢复默认态。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "XGuiConfig.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XSystem.h" /* 内存安全阀：XSystem_memoryInfo 读 MemAvailable（2026-10-23）。 */
#include "XWidget.h"
#include "XWindow.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XComboBox.h"
#include "XPushButton.h"
#include "XStyle.h"
#include "XStyleHints.h"
#include "XGuiApplication.h"
#include "XCursor.h"
#include "XBoxLayout.h"
#include "XWindowDecoration.h"
#include "XDateTime.h"
#include "XListView.h"
#include "XAbstractItemView.h"
#include "XEvent.h"
#include "xgui_demo_theme.h"
#include "xgui_demo_pages.h"
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
#include "XPerformanceOverlay.h" /* 窄宽档提示行换行时按悬浮层几何让位。 */
#endif

/* 原生样式断言（Windows 专项门禁；其它平台编译期整段出局）：
 * 「显示边框」必须表现为「框架自绘条在、原生装饰不在」。框架强制 CSD
 * 时原生窗按 WS_POPUP 组装，样式位仍可能残留 WS_CAPTION/WS_THICKFRAME
 * 之类的装饰位——那正是系统标题栏与自绘条并存的物证，故直接读原生
 * 样式断言，而不是只读框架侧 flags 回读（后者在双条时同样是干净的）。 */
#ifdef _WIN32
#include <windows.h>
#define WN_HAS_NATIVE_STYLE_FN 1
static int wn_nativeStyleBits(XWidget* top)
{
    XWindowId id;
    DWORD style;
    if (!top || !top->m_windowHandle) return -1;
    id = XWindow_winId((XWindow*)top->m_windowHandle);
    if (!id) return -1;
    style = (DWORD)GetWindowLongPtrW((HWND)(uintptr_t)id, GWL_STYLE);
    /* 返回装饰位集合（非零=存在原生标题栏/边框）。 */
    return (int)(style & (WS_CAPTION | WS_THICKFRAME | WS_SYSMENU |
                          WS_MINIMIZEBOX | WS_MAXIMIZEBOX));
}
#endif

/*
 * 控件段宏守卫：Widget/Label/CheckBox/ComboBox/PushButton + 样式/调色板/
 * 光标段；2026-10-17 容器化后排布由 XBoxLayout 承载，再一并依赖
 * XLAYOUT/XLAYOUT_BOX。任一被裁剪时降级为契约桩（build 返回 NULL、
 * autotest 返回 -1）。
 */
#if XWIDGET_ON && XFRAME_ON && XLABEL_ON && XCHECKBOX_ON && \
    XCOMBOBOX_ON && XPUSHBUTTON_ON && XCURSOR_ON && XGUIAPPLICATION_ON && \
    XLAYOUT_ON && XLAYOUT_BOX_ON

/* ==================== 皮肤规格表（参数化 CSS 生成，自包含规则口径） ====
 * 文本色一律走调色板（方案联动翻深浅），此处只着色盒底/描边；指示器
 * 只描边不填充护对勾/圆点；箭头/步进按钮列不设底色防覆盖（单胜出规
 * 则每态自包含整套盒外观，见 xgui_demo_theme.h 文件头口径）。 */

/** @brief 皮肤规格（index 0=缺省完整主题不走本表，1..5=参数化生成）。 */
typedef struct {
    const char* name;       /**< 下拉框条目文本。 */
    int         dark;       /**< 1=深色方案（调色板联动翻深），0=浅色。 */
    const char* btnBg;      /**< 按钮常态底。 */
    const char* btnBorder;  /**< 常态描边/次级分区。 */
    const char* hoverBg;    /**< 悬停底。 */
    const char* accent;     /**< 强调线（悬停/聚焦/按下/选中描边）。 */
    const char* pressedBg;  /**< 按压底。 */
    const char* checkedBg;  /**< 选中保持底。 */
    const char* inputBg;    /**< 输入类常态底。 */
    const char* disabledBg; /**< 禁用底。 */
    const char* disabledBorder; /**< 禁用描边。 */
} WinSkinSpec;

/** @brief 皮肤表：浅色×3 + 深色×3（2026-10-17 用户口径「多弄几种」）。 */
static const WinSkinSpec kWinSkins[5] = {
    /* 1 浅色·暖沙 */
    { NULL, 0,
      "#FFF8F0", "#8A7A6A", "#FBEEDD", "#C77826",
      "#F5E2C8", "#F5DFC0", "#FFFDF9", "#F2EDE6", "#D8CFC4" },
    /* 2 浅色·青竹 */
    { NULL, 0,
      "#F4FAF5", "#5F7F6A", "#E2F3E6", "#1F8A4C",
      "#CDE9D5", "#C4E6CF", "#FAFEFB", "#ECF2EE", "#CFDCD3" },
    /* 3 深色·墨蓝 */
    { NULL, 1,
      "#2D3340", "#5A6572", "#39414F", "#7AA2FF",
      "#1D222B", "#27436E", "#262B34", "#262A31", "#3A4048" },
    /* 4 深色·暖橙 */
    { NULL, 1,
      "#322D28", "#6E625A", "#403931", "#FF9E4A",
      "#241F1B", "#5E3A1B", "#2B2723", "#2B2724", "#453E38" },
    /* 5 深色·森绿 */
    { NULL, 1,
      "#26302A", "#5C6E62", "#31403A", "#6FE0A8",
      "#1C241F", "#2A4A38", "#222B25", "#242C27", "#3C463F" }
};

/** @brief 组装非缺省皮肤的样式表（懒生成进静态缓冲；index 1..5）。 */
static const char* wn_skinCss(int index)
{
    static char bufs[5][2048];
    static bool built[5];
    const WinSkinSpec* s;
    if (index < 1 || index > 5) return xgui_demo_theme_css;
    if (built[index - 1]) return bufs[index - 1];
    s = &kWinSkins[index - 1];
    snprintf(bufs[index - 1], sizeof(bufs[index - 1]),
        "XPushButton {"
        " background-color: %s;"
        " border: 1px solid %s;"
        " border-radius: 5px; } "
        "XPushButton:hover {"
        " background-color: %s;"
        " border: 1px solid %s;"
        " border-radius: 5px; } "
        "XPushButton:focus {"
        " background-color: %s;"
        " border: 1px solid %s;"
        " border-radius: 5px; } "
        "XPushButton:pressed {"
        " background-color: %s;"
        " border: 1px solid %s;"
        " border-radius: 5px; } "
        "XPushButton:checked {"
        " background-color: %s;"
        " border: 1px solid %s;"
        " border-radius: 5px; } "
        "XPushButton:disabled {"
        " background-color: %s;"
        " border: 1px solid %s;"
        " border-radius: 5px; } "
        "XLineEdit {"
        " background-color: %s;"
        " border: 1px solid %s;"
        " border-radius: 4px; } "
        "XLineEdit:hover {"
        " background-color: %s;"
        " border: 1px solid %s;"
        " border-radius: 4px; } "
        "XLineEdit:focus {"
        " background-color: %s;"
        " border: 1px solid %s;"
        " border-radius: 4px; } "
        "XLineEdit:disabled {"
        " background-color: %s;"
        " border: 1px solid %s;"
        " border-radius: 4px; } "
        "XComboBox {"
        " border: 1px solid %s;"
        " border-radius: 4px; } "
        "XComboBox:hover {"
        " border: 1px solid %s;"
        " border-radius: 4px; } "
        "XComboBox:focus {"
        " border: 1px solid %s;"
        " border-radius: 4px; } "
        "XComboBox:disabled {"
        " border: 1px solid %s;"
        " border-radius: 4px; } "
        "XCheckBox {"
        " border: 1px solid %s;"
        " border-radius: 3px; } "
        "XCheckBox:hover {"
        " border: 1px solid %s;"
        " border-radius: 3px; } "
        "XRadioButton {"
        " border: 1px solid %s;"
        " border-radius: 3px; } "
        "XRadioButton:hover {"
        " border: 1px solid %s;"
        " border-radius: 3px; } ",
        s->btnBg, s->btnBorder,
        s->hoverBg, s->accent,
        s->btnBg, s->accent,
        s->pressedBg, s->accent,
        s->checkedBg, s->accent,
        s->disabledBg, s->disabledBorder,
        s->inputBg, s->btnBorder,
        s->inputBg, s->accent,
        s->inputBg, s->accent,
        s->disabledBg, s->disabledBorder,
        s->btnBorder,
        s->accent,
        s->accent,
        s->disabledBorder,
        s->btnBorder,
        s->accent,
        s->btnBorder,
        s->accent);
    built[index - 1] = true;
    return bufs[index - 1];
}


/* ==================== 页面内部控件登记（demo 单实例 static 自持） ==== */

/** @brief 窗口设置页内部登记表。 */
static struct
{
    XWidget*         m_root;        /**< 页面根控件（契约返回值；堆对象）。 */
    DemoPageStatusFn m_status;      /**< 主窗口状态栏反馈回调（借用）。 */
    void*            m_user;        /**< 回调上下文（主窗口指针，借用）。 */
    char             m_lastStatus[160]; /**< 最近一次反馈文本（自测断言用）。 */
    bool             m_ready;       /**< build 已完成且控件指针有效。 */

    /* ---- 皮肤 ---- */
    XLabel*      m_titleLabel;   /**< 页标题。 */
    XLabel*      m_capSkin;      /**< 「皮肤」分组标题。 */
    XLabel*      m_capSkinName;  /**< 「主题」行标题。 */
    XComboBox*   m_skinCombo;    /**< 浅色/深色墨蓝/深色暖橙。 */

    /* ---- 窗口框架 ---- */
    XLabel*      m_capFrame;     /**< 「窗口框架」分组标题。 */
    XCheckBox*   m_frameCheck;   /**< 显示边框(标题栏)。 */
    XCheckBox*   m_statusCheck;  /**< 显示状态栏。 */
    XCheckBox*   m_maxCheck;     /**< 最大化显示。 */
    XCheckBox*   m_fullCheck;    /**< 全屏显示（CSD 标题栏一并隐藏）。 */

    /* ---- 方向与鼠标 ---- */
    XLabel*      m_capOrient;    /**< 「方向与鼠标」分组标题。 */
    XLabel*      m_capOrientName; /**< 「窗口方向」行标题。 */
    XComboBox*   m_orientCombo;  /**< 横屏/竖屏。 */
    XCheckBox*   m_mouseCheck;   /**< 显示鼠标光标。 */

    /* ---- 操作与反馈 ---- */
    XPushButton* m_resetBtn;     /**< 复位默认。 */
    XLabel*      m_hintLabel;    /**< 说明行（钉底上方）。 */
    XLabel*      m_statusLabel;  /**< 操作结果行（钉底）。 */

    bool         m_syncing;      /**< 回读/编程应用中（抑制槽重复生效）。 */
    bool         m_rotationProbed;    /**< 旋转平台能力是否已探测（一次性）。 */
    bool         m_rotationSupported; /**< 平台是否支持软件内容旋转。 */
    int          m_savedW;       /**< 0° 基准宽（懒捕获；0=未捕获）。 */
    int          m_savedH;       /**< 0° 基准高（四方位共用）。 */

    /* ---- 布局器（2026-10-17 容器化；build 装配段挂根，静态自持） ---- */
    XBoxLayout   m_rootLayout;   /**< 根垂直布局器（页内唯一 setLayout）。 */
    XBoxLayout   m_skinRow;      /**< 「主题」标题+下拉行（水平）。 */
    XBoxLayout   m_frameCol;     /**< 「窗口框架」开关列（垂直，含行）。 */
    XBoxLayout   m_frameRow;     /**< 边框+状态栏开关行（水平）。 */
    XBoxLayout   m_orientRow;    /**< 窗口方向+鼠标开关行（水平）。 */
} s_wn;

/* ==================== 内部辅助 ==================== */

/** @brief 反馈到页面状态行与主窗状态栏（登记最近文本供自测断言）。 */
static void wn_report(const char* text)
{
    if (!text) return;
    snprintf(s_wn.m_lastStatus, sizeof(s_wn.m_lastStatus), "%s", text);
    if (s_wn.m_statusLabel)
        XLabel_setText_2(s_wn.m_statusLabel, text);
    if (s_wn.m_status)
        s_wn.m_status(s_wn.m_user, text);
}

/** @brief 主窗顶层控件借用指针（user 即 DemoWin*，m_base 为首成员）。 */
static XWidget* wn_toplevel(void)
{
    return demo_main_toplevel(s_wn.m_user);
}

/** @brief 皮肤换表 + 方案联动 + 整窗重绘（皮肤槽/复位共用）。 */
static void wn_applySkin(int index)
{
    XStyleHints* hints = XGuiApplication_styleHints();
    const char* css = xgui_demo_theme_css;
    if (index >= 1 && index <= 5) {
        /* 规格表皮肤：深浅方案按表联动（浅色×3 前排/深色×3 后排）。 */
        css = wn_skinCss(index);
        if (hints) XStyleHints_setColorScheme(
            hints, kWinSkins[index - 1].dark
                       ? XStyleHintsColorScheme_Dark
                       : XStyleHintsColorScheme_Light);
    } else {
        index = 0;
        css = xgui_demo_theme_css;
        if (hints) XStyleHints_setColorScheme(
            hints, XStyleHintsColorScheme_Light);
    }
    if (!XStyle_installStyleSheet(css))
        wn_report("\xE7\x9A\xAE\xE8\x82\xA4\xE6\xA0\xB7\xE5\xBC\x8F\xE8\xA1\xA8\xE5\xAE\x89\xE8\xA3\x85\xE5\xA4\xB1\xE8\xB4\xA5");
    /* 换表+方案联动只影响后续绘制：主窗静态基底（窗口背景/标题栏带）
       带缓存，显式失效重建并整窗重绘（否则内容区留旧浅底=白底白字）。 */
    demo_main_sceneInvalidate(s_wn.m_user);
}

/** @brief 页面控件 ← 主窗当前态回读（build/复位共用；m_syncing 抑制槽）。 */
static void wn_syncFromWindow(void)
{
    XWidget* top = wn_toplevel();
    if (!top) return;
    s_wn.m_syncing = true;
    if (s_wn.m_frameCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_frameCheck,
            (XWidget_windowFlags(top) &
             (XWidgetFlags)XWindowType_FramelessWindowHint) == 0);
    if (s_wn.m_statusCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_statusCheck,
                                   demo_main_statusBarVisible(s_wn.m_user));
    if (s_wn.m_maxCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck,
                                   XWidget_isMaximized(top));
    if (s_wn.m_fullCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck,
                                   XWidget_isFullScreen(top));
    /* 方向回读（2026-10-18）：改读**有效旋转角**，不再按宽高比推断
       （h>w 在 180° 下同样成立，会把 180/270 误打回 0/1；且 90° 下逻辑
       宽高是竖的，footprint 对调在平台层完成，本页读到的仍是逻辑值）。 */
    if (s_wn.m_orientCombo)
        XComboBox_setCurrentIndex(s_wn.m_orientCombo,
                                  XWidget_contentRotationDegrees(top) / 90);
    /* 皮肤回读：深色方案→首个深色档（墨蓝/暖橙/森绿同方案不可分辨，
     * 取 3）；浅色/未指定→浅色档。鼠标回读：覆盖光标栈空=显示。 */
    if (s_wn.m_skinCombo) {
        XStyleHints* hints = XGuiApplication_styleHints();
        int idx = 0;
        if (hints && XStyleHints_colorScheme(hints) ==
                         XStyleHintsColorScheme_Dark)
            idx = 3;
        XComboBox_setCurrentIndex(s_wn.m_skinCombo, idx);
    }
    if (s_wn.m_mouseCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_mouseCheck,
                                   XGuiApplication_overrideCursor() == NULL);
    s_wn.m_syncing = false;
}

/* ==================== 槽 ==================== */

/** @brief 皮肤切换。 */
static void wn_skinSlot(XObject* receiver, XVarList* args)
{
    int index;
    (void)receiver; (void)args;
    if (!s_wn.m_skinCombo || s_wn.m_syncing) return;
    index = XComboBox_currentIndex(s_wn.m_skinCombo);
    if (index < 0) index = 0;
    wn_applySkin(index);
    wn_report(index == 0 ? "\xE5\xB7\xB2\xE5\x88\x87\xE6\x8D\xA2\xE6\xB5\x85\xE8\x89\xB2\xE7\x9A\xAE\xE8\x82\xA4"
                         : "\xE5\xB7\xB2\xE5\x88\x87\xE6\x8D\xA2\xE6\xB7\xB1\xE8\x89\xB2\xE7\x9A\xAE\xE8\x82\xA4\xEF\xBC\x88\xE6\x96\x87\xE6\x9C\xAC\xE8\x89\xB2\xE9\x9A\x8F\xE8\xB0\x83\xE8\x89\xB2\xE6\x9D\xBF\xE8\x81\x94\xE5\x8A\xA8\xEF\xBC\x89");
}

/** @brief 边框(标题栏)显隐：FramelessWindowHint 位动态翻转。 */
static void wn_frameSlot(XObject* receiver, XVarList* args)
{
    XWidget* top;
    bool show;
    XWidgetFlags flags;
    (void)receiver; (void)args;
    top = wn_toplevel();
    if (!top || !s_wn.m_frameCheck || s_wn.m_syncing) return;
    show = XAbstractButton_isChecked((XAbstractButton*)s_wn.m_frameCheck);
    flags = XWidget_windowFlags(top);
    if (show)
        flags &= (XWidgetFlags)~(XWidgetFlags)XWindowType_FramelessWindowHint;
    else
        flags |= (XWidgetFlags)XWindowType_FramelessWindowHint;
    XWidget_setWindowFlags(top, flags);
    /* CSD 保留边距收放只走框架 setFrameMargins：demo 蓝条文字几何/
       内容区避让/静态基底须显式重排（2026-10-17 无边框文字错位）。 */
    demo_main_refreshChrome(s_wn.m_user);
    wn_report(show ? "\xE5\xB7\xB2\xE6\x98\xBE\xE7\xA4\xBA\xE8\xBE\xB9\xE6\xA1\x86(\xE6\xA0\x87\xE9\xA2\x98\xE6\xA0\x8F)" : "\xE5\xB7\xB2\xE9\x9A\x90\xE8\x97\x8F\xE8\xBE\xB9\xE6\xA1\x86(\xE6\xA0\x87\xE9\xA2\x98\xE6\xA0\x8F)");
}

/** @brief 状态栏显隐（经主文件访问器，内容区底部预留同步收放）。 */
static void wn_statusSlot(XObject* receiver, XVarList* args)
{
    bool show;
    (void)receiver; (void)args;
    if (!s_wn.m_statusCheck || s_wn.m_syncing) return;
    show = XAbstractButton_isChecked((XAbstractButton*)s_wn.m_statusCheck);
    demo_main_setStatusBarVisible(s_wn.m_user, show);
    wn_report(show ? "\xE5\xB7\xB2\xE6\x98\xBE\xE7\xA4\xBA\xE7\x8A\xB6\xE6\x80\x81\xE6\xA0\x8F" : "\xE5\xB7\xB2\xE9\x9A\x90\xE8\x97\x8F\xE7\x8A\xB6\xE6\x80\x81\xE6\xA0\x8F");
}

/** @brief 最大化切换（取消最大化还原普通几何）。 */
static void wn_maxSlot(XObject* receiver, XVarList* args)
{
    XWidget* top;
    bool max;
    bool wasFull;
    (void)receiver; (void)args;
    top = wn_toplevel();
    if (!top || !s_wn.m_maxCheck || s_wn.m_syncing) return;
    max = XAbstractButton_isChecked((XAbstractButton*)s_wn.m_maxCheck);
    wasFull = XWidget_isFullScreen(top);
    if (max && wasFull) {
        /* 全屏与最大化互斥（Qt WindowStates 语义）：静默清全屏勾选，
           状态替换由 showMaximized 一步完成。 */
        s_wn.m_syncing = true;
        if (s_wn.m_fullCheck)
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck,
                                       false);
        s_wn.m_syncing = false;
    }
    if (max)
        XWidget_showMaximized(top);
    else
        XWidget_showNormal(top);
    if (wasFull) {
        /* 【FullScreen→Maximized 直切退出语义 2026-10-24】此槽翻掉的
         * 全屏不经 wn_fullSlot 退出臂，其两笔收尾在此补齐：
         * ①「显示边框(标题栏)」复选框恢复可用（全屏期被置灰；退出
         *   全屏恢复可用=wn_fullSlot 同款，勾选值全程未被触碰按原值
         *   生效）；
         * ②chrome 快照重刷（demo 根布局 contentsMargins.top=
         *   demo_sysbarH() 快照）：showMaximized 内装饰层已按退出全屏
         *   恢复条带边距（标题栏回归），但 demo 布局消费的是快照值，
         *   不重刷则内容停在旧让位偏移——标题栏压品牌条裁切（桌面
         *   实机复现 2a 的页面侧余量；框架侧 RESIZE 回放踢只保证布
         *   局按快照重跑，快照值仍须本调用更新）。
         * 非 wasFull 的常态最大化往返边距未变，不进本分支零开销。 */
        if (s_wn.m_frameCheck)
            XWidget_setEnabled((XWidget*)s_wn.m_frameCheck, true);
        demo_main_refreshChrome(s_wn.m_user);
    }
    wn_report(max ? "\xE5\xB7\xB2\xE6\x9C\x80\xE5\xA4\xA7\xE5\x8C\x96\xE6\x98\xBE\xE7\xA4\xBA" : "\xE5\xB7\xB2\xE8\xBF\x98\xE5\x8E\x9F\xE6\x99\xAE\xE9\x80\x9A\xE5\x87\xA0\xE4\xBD\x95");
}

/** @brief 全屏切换（游戏式满屏：CSD 标题栏随全屏态隐藏，退出还原；
 *  与最大化互斥；随旋转保持铺满——旋转感知几何交换覆盖 FullScreen）。 */
static void wn_fullSlot(XObject* receiver, XVarList* args)
{
    XWidget* top;
    bool full;
    (void)receiver; (void)args;
    top = wn_toplevel();
    if (!top || !s_wn.m_fullCheck || s_wn.m_syncing) return;
    full = XAbstractButton_isChecked((XAbstractButton*)s_wn.m_fullCheck);
    if (full) {
        s_wn.m_syncing = true;
        if (s_wn.m_maxCheck)
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck,
                                       false);
        s_wn.m_syncing = false;
        XWidget_showFullScreen(top);
        /* 【全屏态边框复选框置灰 2026-10-23】全屏没有标题栏概念，「显示
         * 边框(标题栏)」选项无意义：置灰禁用（Qt 全屏窗口也不提供边框
         * 开关语义）。全屏期间勾选状态保持原值不被全屏改写（此处不动
         * checked）；退出全屏恢复可用并按其勾选状态生效。 */
        if (s_wn.m_frameCheck)
            XWidget_setEnabled((XWidget*)s_wn.m_frameCheck, false);
        /* 【chrome 快照重刷 2026-10-23】demo 根布局 contentsMargins.top
         * =demo_sysbarH()（装饰条带高的快照）。全屏翻转改变条带高
         * （stripHeight 全屏归零/退出恢复），快照必须随之重取，否则
         * 布局停在旧让位偏移（全屏条位留白/退出顶条叠盖）——与
         * wn_frameSlot 翻转 flags 后同调 refreshChrome 的既有约定一致
         * （框架侧 RESIZE 重排踢只重跑布局解算，解算消费的是快照值）。
         * demo 蓝条文字几何/内容区避让/静态基底一并重排。 */
        demo_main_refreshChrome(s_wn.m_user);
        wn_report("\xE5\xB7\xB2\xE5\x85\xA8\xE5\xB1\x8F\xEF\xBC\x88\xE6\xA0\x87\xE9\xA2\x98\xE6\xA0\x8F\xE9\x9A\x8F\xE5\x85\xA8\xE5\xB1\x8F\xE9\x9A\x90\xE8\x97\x8F\xEF\xBC\x8C\xE9\x9A\x8F\xE6\x97\x8B\xE8\xBD\xAC\xE9\x93\xBA\xE6\xBB\xA1\xEF\xBC\x89");
    } else {
        XWidget_showNormal(top);
        /* 退出全屏：恢复边框复选框可用（勾选值全程未被全屏触碰，按其
         * 原勾选状态生效）。 */
        if (s_wn.m_frameCheck)
            XWidget_setEnabled((XWidget*)s_wn.m_frameCheck, true);
        demo_main_refreshChrome(s_wn.m_user); /* 快照重刷（同上）。 */
        wn_report("\xE5\xB7\xB2\xE9\x80\x80\xE5\x87\xBA\xE5\x85\xA8\xE5\xB1\x8F");
    }
}

/** @brief 窗口方向：横竖屏宽高对调（进竖屏存横屏尺寸；按屏钳位）。 */
/** @brief 内存安全阀阈值（kB）：换向（角度真变化）前 MemAvailable 低于
 *         该值即拒绝。默认 8192（8MB，覆盖换向重建峰值 + 全帧重绘 +
 *         系统全局波动的余量）；环境变量 XGUI_ROT_MEM_FLOOR_KB 覆写
 *         （0=关闭阀，供低内存对照实验）。阈值懒解析一次（GUI 线程
 *         单线程，无竞态）。 */
static int wn_memFloorKb(void)
{
    static int floorKb = -1;
    if (floorKb < 0) {
        const char* env = getenv("XGUI_ROT_MEM_FLOOR_KB");
        long v = env ? strtol(env, NULL, 10) : 8192;
        floorKb = v < 0 ? 0 : (int)(v > 1048576 ? 1048576 : v);
    }
    return floorKb;
}

static int wn_fullDemoMode(void); /* 全屏取证驱动档位（env
                                   * XGUI_WN_FULL_DEMO；autotest 区定义，
                                   * 构建尾取证驱动先于定义使用）。 */
static bool wn_fullDemoHold(void); /* 全屏取证保持钩子（档位 1；autotest
                                    * 区定义，构建尾先于定义使用）。 */
static void wn_fullDemoTrampoline(XObject* receiver, XVarList* args); /* 取证
                                    * 驱动排队蹦床（autotest 区定义，构建
                                    * 尾先于定义使用）。 */
static signed char s_wnFullDemoPending = 0; /* 蹦床一次性档位账（0=空转）。 */

/** @brief 内存安全阀判定（2026-10-23）：MemAvailable 达不到阈值返回
 *         false（拒绝换向）。查询口=XSystem_memoryInfo（复用既有
 *         Drive 层 /proc/meminfo 封装，性能悬浮窗同源）；查询不可用
 *         （无 OS/宏裁剪）按不拦处理——阀是低内存防 OOM 的保险丝，
 *         不因平台无查询口而砍掉换向功能（打印一次性诊断留痕）。 */
static bool wn_memFloorOk(void)
{
    static signed char queried = 0;
    XSystemMemoryInfo info;
    int floorKb = wn_memFloorKb();
    if (floorKb == 0) return true;
    if (!XSystem_memoryInfo(&info)) {
        if (!queried) {
            queried = 1;
            fprintf(stderr, "[WN] mem floor: XSystem_memoryInfo unavailable, "
                            "valve bypassed\n");
        }
        return true;
    }
    if (info.availableBytes < (uint64_t)floorKb * 1024u) {
        fprintf(stderr, "[WN] mem floor: refuse rotation, avail=%llu kB < "
                        "floor=%d kB\n",
                (unsigned long long)(info.availableBytes / 1024u), floorKb);
        return false;
    }
    return true;
}

/** @brief 窗口方向应用（0/90/180/270；基准=0° 横屏尺寸）。
 * @details 四方位只旋转窗口外框几何：0°/180° 同基准宽、90°/270° 宽高
 *          对调，按主屏可用几何钳位防越屏；最大化先还原（几何由窗口
 *          状态接管）。基准尺寸懒捕获（首次离开 0° 时若未存，按当前
 *          横向几何取正）。
 * @return  1=已应用；0=平台不支持；-1=内存安全阀拒绝（2026-10-23，
 *          角度真变化且 MemAvailable 低于 wn_memFloorKb 阈值——不部分
 *          生效、不留半初始化状态，内存回升后重试即可）。 */
static int wn_orientApply(int deg, int* outW, int* outH)
{
    XWidget* top = wn_toplevel();
    XScreen* screen;
    int availW = 4096;
    int availH = 4096;
    int bw, bh, w, h, x, y;
    bool wasMax;
    bool wasFull;
    if (!top) return 0; /* 无顶层=探测必败（原裸 return 非零值返回 UB，告警修）。 */
    /* 内存安全阀（2026-10-23）：角度真变化才查——同角重应用（wn_orientApply
       二次置闩路径）与页面构建期的能力探测（直呼 setter，不经本函数）
       不受阀影响。拒绝发生在任何状态改动之前：不 showNormal、不设角、
       不改几何，窗口保持当前方向原样（无半初始化态）。 */
    if (deg != XWidget_contentRotationDegrees(top) && !wn_memFloorOk())
        return -1;
    wasMax = XWidget_isMaximized(top);
    wasFull = XWidget_isFullScreen(top);
    if (wasFull) {
        /* 全屏态换向保持全屏（同最大化路径；旋转感知几何交换覆盖
           FullScreen）：归位软件状态后换向，尾部重进全屏。 */
        XWidget_showNormal(top);
        s_wn.m_syncing = true;
        if (s_wn.m_fullCheck)
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck,
                                       true);
        s_wn.m_syncing = false;
    } else if (wasMax) {
        /* 最大化态换向保持最大化（2026-10-22 用户需求）：归位软件状态
           后换向，换向经 showMaximized 取旋转感知的最大化几何（90/270
           目标=对调屏矩形，footprint 恰铺满面板），最大化标志与勾选框
           全程保持。 */
        XWidget_showNormal(top);
        s_wn.m_syncing = true;
        if (s_wn.m_maxCheck)
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck,
                                       true);
        s_wn.m_syncing = false;
    }
    /* 平台支持探测（2026-10-18）：setter 返回 false 时下拉框置灰并给
       说明文案，不让用户点了没反应。
       [2026-10-23 改] 探测改为**只问能力、不实际换角**：原实现把窗口真
       转到 90° 再转回，等于在没有配套几何变化的情况下跑两遍完整旋转
       链路（切零拷贝缓冲、重建后备存储、整帧重传），在 win32 上会与
       后续真正的换角流程叠加成重入风暴（实测自测跑到此步即中止）。
       能力查询本身是纯读（XWindow_rotationSupported），无副作用。 */
    if (!s_wn.m_rotationProbed)
    {
        s_wn.m_rotationProbed = true;
        s_wn.m_rotationSupported =
            top && top->m_windowHandle
                ? XWindow_rotationSupported((XWindow*)top->m_windowHandle)
                : false;
        if (!s_wn.m_rotationSupported && s_wn.m_orientCombo)
            XWidget_setEnabled((XWidget*)s_wn.m_orientCombo, false);
        fprintf(stderr, "[WN] rot probe: supported=%d top=%p handle=%p\n",
                (int)s_wn.m_rotationSupported, (void*)top,
                top ? top->m_windowHandle : NULL);
        fflush(stderr);
    }
    screen = XGuiApplication_primaryScreen();
    if (screen) {
        XRect avail = XScreen_availableGeometry(screen);
        /* 【R2 2026-10-18】钳位前先对调：竖向 footprint 才是屏幕上真正
           占的形状。若按原始（横）可用几何钳位，(480,800) 在 800×480
           面板上会被钳成 480×480，footprint 对调后变成 480×480 白边。 */
        if (XWindow_rotationSwapsAxes(deg)) {
            int t = avail.width;
            avail.width = avail.height;
            avail.height = t;
        }
        availW = avail.width;
        availH = avail.height;
    }
    /* 0° 基准懒捕获：横屏取当前；当前已竖向则按对调推回。 */
    bw = s_wn.m_savedW;
    bh = s_wn.m_savedH;
    if (bw <= 0 || bh <= 0) {
        int cw = XWidget_width(top);
        int ch = XWidget_height(top);
        if (cw >= ch) { bw = cw; bh = ch; }
        else { bw = ch; bh = cw; }
        s_wn.m_savedW = bw;
        s_wn.m_savedH = bh;
    }
    w = (deg == 90 || deg == 270) ? bh : bw;
    h = (deg == 90 || deg == 270) ? bw : bh;
    if (w > availW) w = availW;
    if (h > availH) h = availH;
    /* 内容旋转（2026-10-18）——**必须先设旋转角、后设几何**：
       平台层 setGeometry 的原生 footprint 对调读的是执行时刻的旋转角
       （xpwn_applyRotationFootprint），几何在前会把未对调的 w/h 落给
       原生窗、随后旋转层再按新角换算 → 双重错位（90/270 实测原生窗
       700x1000 而上屏描述符 1000x700，整屏错乱）。0/180 不换轴才对
       顺序不敏感。旋转由平台层 present/输入按角换算（footprint 对调
       在 XPlatformNativeWindow 侧自动完成），本页不手动对调几何。
       返回 false=平台不支持（win32/android 恒不支持；fbdev 直写侧
       2026-10-21 定稿起恒可旋转——影子缓冲架构 present 侧 R⁻¹ 行写
       有帧可转），此时连几何也一并跳过（外框维持 0° 基准），由调用方
       给出说明文案。 */
    if (!XWidget_setContentRotation(top, deg))
    {
        if (outW) *outW = w;
        if (outH) *outH = h;
        return 0; /* 0 = 平台不支持（槽据此置灰说明）。 */
    }
    if (wasFull) {
        /* 全屏：几何交给旋转感知的软件全屏（目标=对调屏矩形，铺满）。 */
        XWidget_showFullScreen(top);
        XWidget_setContentRotation(top, deg); /* 同角二次置闩（全帧重画）。 */
        if (outW) *outW = w;
        if (outH) *outH = h;
        return 1;
    }
    if (wasMax) {
        /* 最大化（全屏）：几何交给旋转感知的软件最大化（目标=对调屏
           矩形，footprint 恰铺满面板），w/h 钳制结果不参与。 */
        XWidget_showMaximized(top);
        XWidget_setContentRotation(top, deg); /* 同角二次置闩（全帧重画）。 */
        if (outW) *outW = w;
        if (outH) *outH = h;
        return 1;
    }
    x = XWidget_x(top);
    y = XWidget_y(top);
    XWidget_setGeometry(top, x, y, w, h);
    /* 同角二次置闩：过渡态落笔的残迹由布局稳定后的整帧重画抹平（见
       XWidget_setContentRotation 注）。
       【历史备注 2026-10-18 目验期】曾在此处按换轴方位调 demo_main_
       refreshChrome 兜底页容器重排（XSplitter 事件链断开期间，90→0
       回程页容器/页根恒卡竖屏几何、页脚标签钉 y=840 被裁）；主线修复
       XSplitter 基类事件链（XObject→XWidget）后 resize 级联正常传播，
       该调用整体摘除——且其重算装饰边距实测有 2px 级品牌条高度漂移
       （30-67→30-69），摘除后 0° 复原逐位一致。若 XSplitter 事件链
       回退，此处需恢复 refreshChrome 兜底。 */
    XWidget_setContentRotation(top, deg);
    if (outW) *outW = w;
    if (outH) *outH = h;
    return 1;
}

static void wn_orientSlot(XObject* receiver, XVarList* args)
{
    static const char* const kNames[4] = {
        "\xE6\xA8\xAA\xE5\xB1\x8F\xEF\xBC\x88" "0" "\xC2\xB0\xEF\xBC\x89",
        "\xE7\xAB\x96\xE5\xB1\x8F\xEF\xBC\x88" "90" "\xC2\xB0\xEF\xBC\x89",
        "\xE5\x80\x92\xE7\xBD\xAE\xE6\xA8\xAA\xE5\xB1\x8F\xEF\xBC\x88" "180" "\xC2\xB0\xEF\xBC\x89",
        "\xE5\x80\x92\xE7\xBD\xAE\xE7\xAB\x96\xE5\xB1\x8F\xEF\xBC\x88" "270" "\xC2\xB0\xEF\xBC\x89"
    }; /* 横屏(0°)/竖屏(90°)/倒置横屏(180°)/倒置竖屏(270°) */
    char buf[96];
    int index;
    int rc;
    int w = 0;
    int h = 0;
    (void)receiver; (void)args;
    if (!s_wn.m_orientCombo || s_wn.m_syncing) return;
    index = XComboBox_currentIndex(s_wn.m_orientCombo);
    if (index < 0 || index > 3) index = 0;
    rc = wn_orientApply(index * 90, &w, &h);
    if (rc < 0)
    {
        /* 内存安全阀拒绝（2026-10-23）：明确文案而非静默，方向保持原样；
           下拉框回读窗口当前角（m_syncing 抑制槽），选择不留在新档——
           内存回升后重选即可再试。 */
        wn_report("\xE5\x86\x85\xE5\xAD\x98\xE4\xB8\x8D\xE8\xB6\xB3\xEF\xBC\x8C\xE5\xB7\xB2\xE4\xBF\x9D\xE6\x8C\x81\xE5\xBD\x93\xE5\x89\x8D\xE6\x96\xB9\xE5\x90\x91");
        wn_syncFromWindow();
        return;
    }
    if (rc == 0)
    {
        /* 平台不支持软件旋转（win32/android；fbdev 2026-10-21 定稿起
           恒可旋转）：下拉框已在探测阶段置灰，这里给明确说明而非静默
           无反应。 */
        wn_report("\xE5\xBD\x93\xE5\x89\x8D\xE5\xB9\xB3\xE5\x8F\xB0\xE4\xB8\x8D\xE6\x94\xAF\xE6\x8C\x81\xE5\x86\x85\xE5\xAE\xB9\xE6\x97\x8B\xE8\xBD\xAC\xEF\xBC\x8C\xE5\x8F\xAF\xE7\x94\xA8\xE6\x88\xB7\xE7\xAB\xAF\xE5\x8F\xA3\xE6\x94\xAF\xE6\x8C\x81");
        return;
    }
    /* 报告文案加角度标记（2026-10-18）：180° 与 0° 宽高完全相同，只打
       (WxH) 在状态行上二者一模一样——这正是「180° 完全无感」的用户
       可见面。 */
    snprintf(buf, sizeof(buf),
             "\xE5\xB7\xB2\xE5\x88\x87\xE6\x8D\xA2\xEF\xBC\x9A%s\xEF\xBC\x88%dx%d\xEF\xBC\x89",
             kNames[index], w, h); /* 已切换：…(WxH) */
    wn_report(buf);
}

/** @brief 鼠标光标显隐：覆盖光标栈压 Blank（隐藏）/ 清空栈（恢复显示）。
 * @details fbdev 触摸跟随软件精灵的有效形状取覆盖栈顶（XCursor.c
 *          xcursor_swEffectiveShape：非空 override 优先，Blank=停画），
 *          本页是全库唯一覆盖栈用户，开关即屏上光标真实显隐。 */
static void wn_mouseSlot(XObject* receiver, XVarList* args)
{
    bool show;
    (void)receiver; (void)args;
    if (!s_wn.m_mouseCheck || s_wn.m_syncing) return;
    show = XAbstractButton_isChecked((XAbstractButton*)s_wn.m_mouseCheck);
    if (show) {
        /* 显示=确保 Blank override 清除：栈深不定（他处/历史压栈），
           逐层弹出直至空，有效形状回落窗口最近形状，精灵恢复跟随。 */
        while (XGuiApplication_overrideCursor())
            XGuiApplication_restoreOverrideCursor();
        wn_report("\xE5\xB7\xB2\xE6\x98\xBE\xE7\xA4\xBA\xE9\xBC\xA0\xE6\xA0\x87\xE5\x85\x89\xE6\xA0\x87");
    } else {
        /* 隐藏=压 Blank；栈顶已是 Blank 则幂等跳过，防重复取消勾选
           把栈越压越深（复位/回读按 override==NULL 判显示态）。 */
        if (!XGuiApplication_overrideCursor() ||
            XCursor_shape(XGuiApplication_overrideCursor()) !=
                XCursor_Blank) {
            XCursor blank;
            XCursor_init(&blank);
            XCursor_setShape(&blank, XCursor_Blank);
            XGuiApplication_setOverrideCursor(&blank);
            XClassDeinit(&blank);
        }
        wn_report("\xE5\xB7\xB2\xE9\x9A\x90\xE8\x97\x8F\xE9\xBC\xA0\xE6\xA0\x87\xE5\x85\x89\xE6\xA0\x87");
    }
}

/** @brief 复位默认：控件回默认态并逐项重新应用。 */
static void wn_resetSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_wn.m_ready) return;
    s_wn.m_syncing = true;
    if (s_wn.m_skinCombo)
        XComboBox_setCurrentIndex(s_wn.m_skinCombo, 0);
    if (s_wn.m_frameCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_frameCheck, true);
    if (s_wn.m_statusCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_statusCheck, true);
    if (s_wn.m_maxCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck, false);
    if (s_wn.m_fullCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck, false);
    if (s_wn.m_orientCombo)
        XComboBox_setCurrentIndex(s_wn.m_orientCombo, 0);
    if (s_wn.m_mouseCheck)
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_mouseCheck, true);
    s_wn.m_syncing = false;

    wn_applySkin(0);
    {
        XWidget* top = wn_toplevel();
        if (top) {
            XWidgetFlags flags = XWidget_windowFlags(top);
            flags &= (XWidgetFlags)~(XWidgetFlags)
                XWindowType_FramelessWindowHint;
            XWidget_setWindowFlags(top, flags);
            /* 【全屏复位 2026-10-24】复位默认含退出全屏（默认态无全
               屏）：全屏态 isMaximized 为假（FullScreen 与 Maximized
               互斥落账），旧判据漏全屏——复位后窗口滞留全屏且边框复
               选框滞留置灰。showNormal 经装饰层恢复条带+边距，快照
               由下方 setStatusBarVisible 内的 refreshChrome 重取。 */
            if (XWidget_isMaximized(top) || XWidget_isFullScreen(top))
                XWidget_showNormal(top);
            /* 全屏期「显示边框」复选框被置灰：复位恢复可用（勾选值
               由上方 syncing 段已强制回 true=显示，与默认态一致）。 */
            if (s_wn.m_frameCheck)
                XWidget_setEnabled((XWidget*)s_wn.m_frameCheck, true);
        }
    }
    demo_main_setStatusBarVisible(s_wn.m_user, true);
    /* 回 0°（含最大化还原 + 显式归零旋转角）。显式归零是必需的：复位
       全程持 m_syncing，setCurrentIndex 不触发槽，旋转角不会自动落 0。
       【内存安全阀 2026-10-23】回 0° 同受阀约束（同为换向重建）；拒绝
       时窗口保持旋转态，其余复位项照常生效，状态行如实报因而非「已
       复位默认」。 */
    if (wn_orientApply(0, NULL, NULL) < 0)
        wn_report("\xE5\x86\x85\xE5\xAD\x98\xE4\xB8\x8D\xE8\xB6\xB3\xEF\xBC\x8C\xE5\xB7\xB2\xE4\xBF\x9D\xE6\x8C\x81\xE5\xBD\x93\xE5\x89\x8D\xE6\x96\xB9\xE5\x90\x91");
    else
        wn_report("\xE5\xB7\xB2\xE5\xA4\x8D\xE4\xBD\x8D\xE9\xBB\x98\xE8\xAE\xA4");
    XGuiApplication_restoreOverrideCursor();
    wn_syncFromWindow();
}

/* ==================== 装配工具 ==================== */

/** @brief 组装一个分组标题行（灰字小号；高固定 22，宽随布局伸展）。 */
static XLabel* wn_buildCaption(const char* text)
{
    XLabel* label = XLabel_create(s_wn.m_root, 0);
    if (label) {
        XLabel_setText_2(label, text);
        XLabel_setTextPixelSize(label, 13);
        XWidget_setForegroundRole((XWidget*)label,
                                  XPaletteColorRole_Mid);
        XWidget_setFixedHeight((XWidget*)label, 22);
        XWidget_show((XWidget*)label);
    }
    return label;
}

/** @brief 组装一个开关复选框（宽 w 高 22）。 */
static XCheckBox* wn_buildCheck(const char* text, int w, XSlotFunc2 slot)
{
    XCheckBox* check = XCheckBox_create(s_wn.m_root, 0);
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

/* ==================== 契约实现 ==================== */

XWidget* demo_page_window_settings_build(XWidget* parent,
                                         DemoPageStatusFn status, void* user)
{
    /* demo 单实例：重复 build 前清空登记表（旧页面随旧父链析构）。 */
    memset(&s_wn, 0, sizeof(s_wn));
    s_wn.m_status = status;
    s_wn.m_user = user;
    if (!parent) return (XWidget*)0;
    if (!demo_main_toplevel(user)) {
        XPrintf("XGuiAutoTest: [FAIL] \xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE4\xB8\xBB\xE7\xAA\x97\xE9\xA1\xB6\xE5\xB1\x82\xE4\xB8\x8D\xE5\x8F\xAF\xE7\x94\xA8\n");
        return (XWidget*)0;
    }
    s_wn.m_root = XWidget_create(parent, 0);
    if (!s_wn.m_root) return (XWidget*)0;

    /* ---- 标题 ---- */
    s_wn.m_titleLabel = XLabel_create(s_wn.m_root, 0);
    if (!s_wn.m_titleLabel) return s_wn.m_root;
    XLabel_setText_2(s_wn.m_titleLabel, "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE");
    XLabel_setTextPixelSize(s_wn.m_titleLabel, 15);
    XWidget_setFixedHeight((XWidget*)s_wn.m_titleLabel, 24);
    XWidget_show((XWidget*)s_wn.m_titleLabel);

    /* ---- 皮肤 ---- */
    s_wn.m_capSkin = wn_buildCaption("\xE7\x9A\xAE\xE8\x82\xA4");
    s_wn.m_capSkinName = XLabel_create(s_wn.m_root, 0);
    if (s_wn.m_capSkinName) {
        XLabel_setText_2(s_wn.m_capSkinName, "\xE4\xB8\xBB\xE9\xA2\x98");
        XWidget_setFixedWidth((XWidget*)s_wn.m_capSkinName, 64);
        XWidget_setFixedHeight((XWidget*)s_wn.m_capSkinName, 22);
        XWidget_show((XWidget*)s_wn.m_capSkinName);
    }
    s_wn.m_skinCombo = XComboBox_create(s_wn.m_root, 0);
    if (!s_wn.m_skinCombo) return s_wn.m_root;
    XComboBox_addItem_2(s_wn.m_skinCombo, "\xE6\xB5\x85\xE8\x89\xB2 \xC2\xB7 \xE7\x8E\xB0\xE4\xBB\xA3\xE8\x93\x9D\xEF\xBC\x88\xE9\xBB\x98\xE8\xAE\xA4\xEF\xBC\x89");
    XComboBox_addItem_2(s_wn.m_skinCombo, "\xE6\xB5\x85\xE8\x89\xB2 \xC2\xB7 \xE6\x9A\x96\xE6\xB2\x99");
    XComboBox_addItem_2(s_wn.m_skinCombo, "\xE6\xB5\x85\xE8\x89\xB2 \xC2\xB7 \xE9\x9D\x92\xE7\xAB\xB9");
    XComboBox_addItem_2(s_wn.m_skinCombo, "\xE6\xB7\xB1\xE8\x89\xB2 \xC2\xB7 \xE5\xA2\xA8\xE8\x93\x9D");
    XComboBox_addItem_2(s_wn.m_skinCombo, "\xE6\xB7\xB1\xE8\x89\xB2 \xC2\xB7 \xE6\x9A\x96\xE6\xA9\x99");
    XComboBox_addItem_2(s_wn.m_skinCombo, "\xE6\xB7\xB1\xE8\x89\xB2 \xC2\xB7 \xE6\xA3\xAE\xE7\xBB\xBF");
    XWidget_setFixedWidth((XWidget*)s_wn.m_skinCombo, 220);
    XWidget_setFixedHeight((XWidget*)s_wn.m_skinCombo, 26);
    XObject_connect_2((XObject*)s_wn.m_skinCombo,
                      XSignal(XComboBox_activated_signal), wn_skinSlot);
    XWidget_show((XWidget*)s_wn.m_skinCombo);

    /* ---- 窗口框架 ---- */
    s_wn.m_capFrame = wn_buildCaption("\xE7\xAA\x97\xE5\x8F\xA3\xE6\xA1\x86\xE6\x9E\xB6");
    s_wn.m_frameCheck = wn_buildCheck("\xE6\x98\xBE\xE7\xA4\xBA\xE8\xBE\xB9\xE6\xA1\x86(\xE6\xA0\x87\xE9\xA2\x98\xE6\xA0\x8F)", 190,
                                      wn_frameSlot);
    s_wn.m_statusCheck = wn_buildCheck("\xE6\x98\xBE\xE7\xA4\xBA\xE7\x8A\xB6\xE6\x80\x81\xE6\xA0\x8F", 150,
                                       wn_statusSlot);
    s_wn.m_maxCheck = wn_buildCheck("\xE6\x9C\x80\xE5\xA4\xA7\xE5\x8C\x96\xE6\x98\xBE\xE7\xA4\xBA", 190, wn_maxSlot);
    s_wn.m_fullCheck = wn_buildCheck("\xE5\x85\xA8\xE5\xB1\x8F\xE6\x98\xBE\xE7\xA4\xBA", 150, wn_fullSlot);

    /* ---- 方向与鼠标 ---- */
    s_wn.m_capOrient = wn_buildCaption("\xE6\x96\xB9\xE5\x90\x91\xE4\xB8\x8E\xE9\xBC\xA0\xE6\xA0\x87");
    s_wn.m_capOrientName = XLabel_create(s_wn.m_root, 0);
    if (s_wn.m_capOrientName) {
        XLabel_setText_2(s_wn.m_capOrientName, "\xE7\xAA\x97\xE5\x8F\xA3\xE6\x96\xB9\xE5\x90\x91");
        XWidget_setFixedWidth((XWidget*)s_wn.m_capOrientName, 64);
        XWidget_setFixedHeight((XWidget*)s_wn.m_capOrientName, 22);
        XWidget_show((XWidget*)s_wn.m_capOrientName);
    }
    s_wn.m_orientCombo = XComboBox_create(s_wn.m_root, 0);
    if (!s_wn.m_orientCombo) return s_wn.m_root;
    XComboBox_addItem_2(s_wn.m_orientCombo, "\xE6\xA8\xAA\xE5\xB1\x8F\xEF\xBC\x88" "0" "\xC2\xB0\xEF\xBC\x89");
    XComboBox_addItem_2(s_wn.m_orientCombo, "\xE7\xAB\x96\xE5\xB1\x8F\xEF\xBC\x88" "90" "\xC2\xB0\xEF\xBC\x89");
    XComboBox_addItem_2(s_wn.m_orientCombo, "\xE5\x80\x92\xE7\xBD\xAE\xE6\xA8\xAA\xE5\xB1\x8F\xEF\xBC\x88" "180" "\xC2\xB0\xEF\xBC\x89");
    XComboBox_addItem_2(s_wn.m_orientCombo, "\xE5\x80\x92\xE7\xBD\xAE\xE7\xAB\x96\xE5\xB1\x8F\xEF\xBC\x88" "270" "\xC2\xB0\xEF\xBC\x89");
    XWidget_setFixedWidth((XWidget*)s_wn.m_orientCombo, 170);
    XWidget_setFixedHeight((XWidget*)s_wn.m_orientCombo, 26);
    XObject_connect_2((XObject*)s_wn.m_orientCombo,
                      XSignal(XComboBox_activated_signal), wn_orientSlot);
    XWidget_show((XWidget*)s_wn.m_orientCombo);
    s_wn.m_mouseCheck = wn_buildCheck("\xE6\x98\xBE\xE7\xA4\xBA\xE9\xBC\xA0\xE6\xA0\x87\xE5\x85\x89\xE6\xA0\x87", 170,
                                      wn_mouseSlot);

    /* ---- 操作与反馈 ---- */
    s_wn.m_resetBtn = XPushButton_create(s_wn.m_root, 0);
    if (!s_wn.m_resetBtn) return s_wn.m_root;
    XPushButton_setText_2(s_wn.m_resetBtn, "\xE5\xA4\x8D\xE4\xBD\x8D\xE9\xBB\x98\xE8\xAE\xA4");
    XWidget_setFixedWidth((XWidget*)s_wn.m_resetBtn, 110);
    XWidget_setFixedHeight((XWidget*)s_wn.m_resetBtn, 30);
    XObject_connect_2((XObject*)s_wn.m_resetBtn,
                      XSignal(XAbstractButton_clicked_signal), wn_resetSlot);
    XWidget_show((XWidget*)s_wn.m_resetBtn);
    s_wn.m_hintLabel = XLabel_create(s_wn.m_root, 0);
    if (s_wn.m_hintLabel) {
        XLabel_setText_2(s_wn.m_hintLabel,
                         "\xE6\x94\xB9\xE5\x8A\xA8\xE5\x8D\xB3\xE6\x97\xB6\xE7\x94\x9F\xE6\x95\x88\xEF\xBC\x9B\xE7\xAA\x97\xE5\x8F\xA3\xE6\x96\xB9\xE5\x90\x91=\xE5\x9B\x9B\xE6\x96\xB9\xE4\xBD\x8D\xE5\xA4\x96\xE6\xA1\x86\xE6\x97\x8B\xE8\xBD\xAC\xEF\xBC\x88\xE6\x8C\x89\xE4\xB8\xBB\xE5\xB1\x8F\xE5\x8F\xAF\xE7\x94\xA8\xE5\x87\xA0\xE4\xBD\x95\xE9\x92\xB3\xE4\xBD\x8D\xEF\xBC\x89\xEF\xBC\x9B"
                         "\xE9\x9A\x90\xE8\x97\x8F\xE9\xBC\xA0\xE6\xA0\x87\xE5\x90\x8E\xE7\xA7\xBB\xE5\x8A\xA8\xE6\x97\xA0\xE5\x85\x89\xE6\xA0\x87\xEF\xBC\x8C\xE5\x9B\x9E\xE6\x9C\xAC\xE9\xA1\xB5\xE5\x8B\xBE\xE9\x80\x89\xE6\x81\xA2\xE5\xA4\x8D");
        XLabel_setTextPixelSize(s_wn.m_hintLabel, 12);
        /* 不设定高：高度由 hfw（窄档换行）/推送的 sizeHint（宽档单行）
         * 驱动（见 adapt；2026-10-25 框架 hfw 修复后删除绕 Bug 定高）。 */
        XWidget_show((XWidget*)s_wn.m_hintLabel);
    }
    s_wn.m_statusLabel = XLabel_create(s_wn.m_root, 0);
    if (!s_wn.m_statusLabel) return s_wn.m_root;
    XLabel_setText_2(s_wn.m_statusLabel, "\xE5\xB0\xB1\xE7\xBB\xAA");
    XWidget_setFixedHeight((XWidget*)s_wn.m_statusLabel, 22);
    XWidget_show((XWidget*)s_wn.m_statusLabel);

    /* ---- 布局装配（2026-10-17 容器化）：根垂直布局七段----
     * [标题][皮肤][窗口框架][方向与鼠标][复位][伸展][提示/状态贴底]；
     * 段间 addSpacing 近似原设计稿节奏（8/4/12/4/6/14/6/16），行内
     * 间距用行级 spacing/加间距，行高由各控件固定高承载（原 adapt
     * 钉底语义改由 stretch+贴底两行承载）。 */
    XBoxLayout_init(&s_wn.m_rootLayout, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&s_wn.m_rootLayout, 0);
    XLayout_setContentsMargins((XLayout*)&s_wn.m_rootLayout, 12, 8, 12, 8);
    XBoxLayout_addWidget(&s_wn.m_rootLayout, (XWidget*)s_wn.m_titleLabel);
    XBoxLayout_addSpacing(&s_wn.m_rootLayout, 8);
    XBoxLayout_addWidget(&s_wn.m_rootLayout, (XWidget*)s_wn.m_capSkin);
    XBoxLayout_addSpacing(&s_wn.m_rootLayout, 4);
    /* 主题行：行左缩进 12（对齐原 x=24 起排），标题|6|下拉（行高 26）。 */
    XBoxLayout_init(&s_wn.m_skinRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_wn.m_skinRow, 0);
    XLayout_setContentsMargins((XLayout*)&s_wn.m_skinRow, 12, 0, 0, 0);
    XBoxLayout_addWidgetEx(&s_wn.m_skinRow, (XWidget*)s_wn.m_capSkinName, 0,
                           XAlignment_VCenter);
    XBoxLayout_addSpacing(&s_wn.m_skinRow, 6);
    XBoxLayout_addWidget(&s_wn.m_skinRow, (XWidget*)s_wn.m_skinCombo);
    XBoxLayout_addStretch(&s_wn.m_skinRow, 1); /* 吸收行尾余量保持左聚。 */
    XBoxLayout_addLayout(&s_wn.m_rootLayout, (XLayout*)&s_wn.m_skinRow);
    XBoxLayout_addSpacing(&s_wn.m_rootLayout, 12);
    XBoxLayout_addWidget(&s_wn.m_rootLayout, (XWidget*)s_wn.m_capFrame);
    XBoxLayout_addSpacing(&s_wn.m_rootLayout, 4);
    /* 框架开关列（缩进 12）：[边框|26|状态栏] 行 + 6 + 最大化行。 */
    XBoxLayout_init(&s_wn.m_frameCol, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&s_wn.m_frameCol, 6);
    XLayout_setContentsMargins((XLayout*)&s_wn.m_frameCol, 12, 0, 0, 0);
    XBoxLayout_init(&s_wn.m_frameRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_wn.m_frameRow, 26);
    XBoxLayout_addWidget(&s_wn.m_frameRow, (XWidget*)s_wn.m_frameCheck);
    XBoxLayout_addWidget(&s_wn.m_frameRow, (XWidget*)s_wn.m_statusCheck);
    XBoxLayout_addStretch(&s_wn.m_frameRow, 1); /* 吸收行尾余量保持左聚。 */
    XBoxLayout_addLayout(&s_wn.m_frameCol, (XLayout*)&s_wn.m_frameRow);
    XBoxLayout_addWidget(&s_wn.m_frameCol, (XWidget*)s_wn.m_maxCheck);
    XBoxLayout_addWidget(&s_wn.m_frameCol, (XWidget*)s_wn.m_fullCheck);
    XBoxLayout_addLayout(&s_wn.m_rootLayout, (XLayout*)&s_wn.m_frameCol);
    XBoxLayout_addSpacing(&s_wn.m_rootLayout, 14);
    XBoxLayout_addWidget(&s_wn.m_rootLayout, (XWidget*)s_wn.m_capOrient);
    XBoxLayout_addSpacing(&s_wn.m_rootLayout, 6);
    /* 方向行：窗口方向|6|下拉 · 20 · 显示鼠标光标（VC 对齐 26 行高）。 */
    XBoxLayout_init(&s_wn.m_orientRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_wn.m_orientRow, 0);
    XLayout_setContentsMargins((XLayout*)&s_wn.m_orientRow, 12, 0, 0, 0);
    XBoxLayout_addWidgetEx(&s_wn.m_orientRow, (XWidget*)s_wn.m_capOrientName,
                           0, XAlignment_VCenter);
    XBoxLayout_addSpacing(&s_wn.m_orientRow, 6);
    XBoxLayout_addWidget(&s_wn.m_orientRow, (XWidget*)s_wn.m_orientCombo);
    XBoxLayout_addSpacing(&s_wn.m_orientRow, 20);
    XBoxLayout_addWidgetEx(&s_wn.m_orientRow, (XWidget*)s_wn.m_mouseCheck,
                           0, XAlignment_VCenter);
    XBoxLayout_addStretch(&s_wn.m_orientRow, 1); /* 吸收行尾余量保持左聚。 */
    XBoxLayout_addLayout(&s_wn.m_rootLayout, (XLayout*)&s_wn.m_orientRow);
    XBoxLayout_addSpacing(&s_wn.m_rootLayout, 16);
    XBoxLayout_addWidget(&s_wn.m_rootLayout, (XWidget*)s_wn.m_resetBtn);
    XBoxLayout_addStretch(&s_wn.m_rootLayout, 1);
    XBoxLayout_addWidget(&s_wn.m_rootLayout, (XWidget*)s_wn.m_hintLabel);
    XBoxLayout_addSpacing(&s_wn.m_rootLayout, 6);
    XBoxLayout_addWidget(&s_wn.m_rootLayout, (XWidget*)s_wn.m_statusLabel);
    XWidget_setLayout(s_wn.m_root, (XLayout*)&s_wn.m_rootLayout);

    /* ---- 控件初值 ← 主窗当前态 ---- */
    wn_syncFromWindow();

    s_wn.m_ready = true;
    /* 【全屏取证驱动 2026-10-23】env XGUI_WN_FULL_DEMO 分档驱动：
     *   "1"=进全屏并保持（--screenshot 存「标题栏隐藏+内容铺满」帧）；
     *   "2"=取消「显示边框」（FramelessWindowHint=off 形态，与全屏帧
     *        逐像素对照的基准帧）；
     *   "3"=进全屏再退出（存「退出还原」帧，与普通基线帧对照）。
     * 时序关键：页面构建先于主文件启动尾的无条件 showNormal
     * （xgui_window_demo.c show 块），此处同步直调会被其归位吞掉——
     * 蹦床经 queued toggled 连接把应用时序推到事件循环首拍（show 后），
     * 与用户点击同一 wn_fullSlot/wn_frameSlot 槽路径（XTEST 物理点击
     * 在本环境命中层错位不可用，autotest HITPROBE 先例）。档位消费后
     * 蹦床空转，连接保留无副作用。正常运行（无 env）零扰动。 */
    if (wn_fullDemoMode() == 2) {
        s_wnFullDemoPending = 2;
        XObject_connect_1((XObject*)s_wn.m_frameCheck,
                          XSignal(XAbstractButton_toggled_signal),
                          (XObject*)s_wn.m_root, wn_fullDemoTrampoline,
                          XConnectionType_Queued);
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_frameCheck,
                                   false); /* 触发 queued 蹦床。 */
    } else if (wn_fullDemoMode() > 0) {
        s_wnFullDemoPending = wn_fullDemoMode();
        XObject_connect_1((XObject*)s_wn.m_fullCheck,
                          XSignal(XAbstractButton_toggled_signal),
                          (XObject*)s_wn.m_root, wn_fullDemoTrampoline,
                          XConnectionType_Queued);
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck,
                                   true); /* 触发 queued 蹦床。 */
    }
    return s_wn.m_root;
}

/* ==================== 自适应重排（xgui_demo_pages.h 契约） ============ */

/** @brief 窄宽分档阈值（逻辑宽）：覆盖 400 级手机竖屏；宽于该值的窗口
 *  （桌面 1000x700 等）恒走 build 装配的桌面口径，布局零扰动。 */
#define WN_NARROW_PAGE_WIDTH 520

void demo_page_window_settings_adapt(XWidget* page)
{
    /* 2026-10-17 容器化：页内排布交给根布局器（提示/状态行经 stretch
     * 贴底、随根几何自动重排），桌面口径不做手写几何。2026-10-25 起
     * 增加窄宽分档：build 装配的固定值（行标题列宽/开关行高/提示单行）
     * 都按桌面口径写死，400 级竖屏下提示行溢出右缘且压进 FPS 悬浮层、
     * 「窗口方向」四字标题顶到下拉框、框架组开关行距低于拇指触控目标
     * ——这些只在窄宽档出现，故分档收敛在本入口，宽档恒复原桌面
     * 常量，resize 跨档来回不残留。 */
    bool narrow;
    if (!page || !s_wn.m_ready || page != s_wn.m_root) return;
    narrow = XWidget_width(page) > 0 &&
             XWidget_width(page) < WN_NARROW_PAGE_WIDTH;

    /* ---- 页底提示行：窄宽档开换行并按 FPS 悬浮层宽收窄换行宽 ----
     * 提示文本约 64 个全角字符，桌面 1000 宽单行放得下；400 宽单行
     * 必然溢出窗口右缘被裁，且贴底行与右下锚定的悬浮层同落窗口底带、
     * 被盖掉大半。窄宽档：换行 + 最大宽收窄到让开悬浮层（悬浮层右缘
     * 贴窗右缘，页根右缘与之对齐，按其宽让位即可整行避让），行高按
     * heightForWidth 给足；悬浮层隐藏/未解算尺寸时先不让位，下一轮
     * resize/切页 adapt 会补上；用户把悬浮层预设到它处时也只是多换
     * 一行，无害冗余。 */
    if (s_wn.m_hintLabel) {
        if (narrow) {
            int wrapW = XWidget_width(page) - 24; /* 根布局左右边距 12+12。 */
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
            XPerformanceOverlay* overlay =
                (XPerformanceOverlay*)demo_main_overlay(s_wn.m_user);
            if (overlay && XWidget_isVisible((XWidget*)overlay)) {
                XRect og = XPerformanceOverlay_geometry(overlay);
                if (og.width > 0 && og.width < wrapW)
                    wrapW -= og.width + 12; /* 悬浮层宽 + 呼吸间距。 */
            }
#endif
            if (wrapW < 160) wrapW = 160; /* 下限：再窄宁可叠窗缘不碎行。 */
            XLabel_setWordWrap(s_wn.m_hintLabel, true);
            XWidget_setMaximumWidth((XWidget*)s_wn.m_hintLabel, wrapW);
        } else {
            XLabel_setWordWrap(s_wn.m_hintLabel, false);
            XWidget_setMaximumWidth((XWidget*)s_wn.m_hintLabel,
                                    XWIDGET_MAX_SIZE);
        }
        /* [定高约束删除 2026-10-25 用户裁定] 不再 setFixedHeight：此前
         * XLabel 从不推送 sizeHint（布局视标签为 0 高）才在此手工算行
         * 数定高。框架修复（hfw handler + XLayoutItem 钳位 + sizeHint
         * 推送）后，盒布局的 hfw 替换路径按 maximumWidth 自行算出换行
         * 高，宽档经推送的 sizeHint 得单行高——手工定高反而以 min/max
         * 钳住框架的新鲜值（文本一改即又错），属绕 Bug 补丁，删除。 */
    }

    /* ---- 「窗口方向」行标题列：64 是桌面口径，真机默认字号下「窗口
     * 方向」四字 advance 约 74px，字尾把行内 6px 间距吃剩 2px 顶到下
     * 拉框左边框——窄宽档放宽标题列让间距回到设计值（上方「主题」
     * 两字 64 放得下，不动）。竖向居中由装配的 addWidgetEx(VCenter)
     * 承载（布局引擎按格居中、XLabel 文字默认 VCenter），不在此另调。 ---- */
    if (s_wn.m_capOrientName)
        XWidget_setFixedWidth((XWidget*)s_wn.m_capOrientName,
                              narrow ? 88 : 64);

    /* ---- 「方向与鼠标」行：窄宽档整行翻竖排单列 ----
     * 桌面横排行 [标题|6|方向下拉 ·20· 鼠标开关] 实需 12+64..88+6+170+
     * 20+170=442..466 逻辑宽，400 竖屏行内只有 ~364：行尾「显示鼠标光标」被
     * 推过右缘裁字（真机像素口径「光」只余七成、「标」整字不见）。行
     * 内三件都是定宽控件且文字不可省——收窄任一件都会转而裁下拉闭态
     * 条目（「竖屏（90°）」等）或开关文字，故按 dialogs 页行盒先例用
     * setDirection 整行翻竖排（幂等，内部失效重排）：四件纵列后各占
     * 整行宽（400 档行内宽 ~364），170 定宽原样直用，零裁字；行内
     * 6/20 间距件转为纵列行距，视觉成立。宽档翻回横排复原桌面装配，
     * resize 跨档来回不残留。 ---- */
    XBoxLayout_setDirection(
        &s_wn.m_orientRow,
        narrow ? XBoxLayoutDirection_TopToBottom
               : XBoxLayoutDirection_LeftToRight);

    /* ---- 「窗口框架」开关组：22 高 + 6 间距（行距 28）在拇指触控下
     * 相邻行易误触（触控目标 ≥40 的通行建议）→ 窄宽档把开关拉高到
     * 40：XCheckBox 指示器(13px)与文字都按控件矩形垂直居中绘制，拉高
     * 只扩大命中区不改笔画，行距随之 ≥46。「显示鼠标光标」同是开关
     * 触控件（竖排独占一行，40 高恰为该行触控带），一并拉高。 ---- */
    if (s_wn.m_frameCheck)
        XWidget_setFixedHeight((XWidget*)s_wn.m_frameCheck, narrow ? 40 : 22);
    if (s_wn.m_statusCheck)
        XWidget_setFixedHeight((XWidget*)s_wn.m_statusCheck, narrow ? 40 : 22);
    if (s_wn.m_maxCheck)
        XWidget_setFixedHeight((XWidget*)s_wn.m_maxCheck, narrow ? 40 : 22);
    if (s_wn.m_fullCheck)
        XWidget_setFixedHeight((XWidget*)s_wn.m_fullCheck, narrow ? 40 : 22);
    if (s_wn.m_mouseCheck)
        XWidget_setFixedHeight((XWidget*)s_wn.m_mouseCheck, narrow ? 40 : 22);
}

/* ==================== autotest ==================== */

/** @brief 全屏取证驱动档位（env XGUI_WN_FULL_DEMO，进程级读一次缓存）。
 *  @return 0=未置位（正常运行）；1=全屏保持；2=无边框形态；3=全屏往返。
 *  @details 黑盒取证专用。档位 1 时窗口设置页 autotest 进全屏断言后
 *  提前返回（停在全屏态，跳过退出与复位）；构建尾按档位自动驱动真实
 *  槽路径（见 demo_page_window_settings_build 尾注）——供 --screenshot/
 *  xwd 抓帧取证（全屏帧/无边框基准帧/退出还原帧）。正常回归不带该
 *  env（档位 0），走完整往返。物理 XTEST 点击在本环境命中层错位不可
 *  用（autotest HITPROBE 先例），页面级直呼槽是同 API 序列的替身驱动。 */
static int wn_fullDemoMode(void)
{
    static signed char cached = -1;
    if (cached < 0) {
        const char* env = getenv("XGUI_WN_FULL_DEMO");
        cached = 0;
        if (env && env[0] && !env[1]) {
            if (env[0] == '1') cached = 1;
            else if (env[0] == '2') cached = 2;
            else if (env[0] == '3') cached = 3;
            else if (env[0] == '4') cached = 4; /* 直切最大化保持。 */
            else if (env[0] == '5') cached = 5; /* 直切最大化再还原。 */
        }
    }
    return cached;
}

/** @brief 全屏取证保持判定（autotest 提前返回档；仅档位 1 保持）。 */
static bool wn_fullDemoHold(void)
{
    return wn_fullDemoMode() == 1;
}

/** @brief 取证驱动排队蹦床（一次性）：事件循环首拍（show 后）按档位
 *  重放真实槽路径。构建期被主文件启动尾 showNormal 归位的状态在此
 *  重新落地；消费后置账空转（连接保留，后续用户 toggle 零副作用）。 */
static void wn_fullDemoTrampoline(XObject* receiver, XVarList* args)
{
    int mode = s_wnFullDemoPending;
    (void)receiver; (void)args;
    if (!mode) return;
    s_wnFullDemoPending = 0;
    if (mode == 1) {
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck, true);
        wn_fullSlot(NULL, NULL);
    } else if (mode == 2) {
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_frameCheck,
                                   false);
        wn_frameSlot(NULL, NULL);
    } else if (mode == 3) {
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck, true);
        wn_fullSlot(NULL, NULL);
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck, false);
        wn_fullSlot(NULL, NULL);
    } else if (mode == 4 || mode == 5) {
        /* 【直切序列 2026-10-24】tick1 进全屏，随后把蹦床改挂在最大化
         * 勾选框的 queued toggled 上、勾选之——tick2 走 wn_maxSlot 真实
         * 槽路径落地 FullScreen→Maximized 直切（2a 复现序列）。档位 4
         * 停在直切态（标题栏回归+内容下移重排的取证帧）；档位 5 续排
         * tick3 取消最大化还原普通。 */
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck, true);
        wn_fullSlot(NULL, NULL);
        s_wnFullDemoPending = 41;
        XObject_connect_1((XObject*)s_wn.m_maxCheck,
                          XSignal(XAbstractButton_toggled_signal),
                          (XObject*)s_wn.m_root, wn_fullDemoTrampoline,
                          XConnectionType_Queued);
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck, true);
    } else if (mode == 41) {
        wn_maxSlot(NULL, NULL); /* 直切：FullScreen→Maximized 一步替换。 */
        if (wn_fullDemoMode() == 5) {
            s_wnFullDemoPending = 51;
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck,
                                       false);
        }
    } else if (mode == 51) {
        wn_maxSlot(NULL, NULL); /* 直切后还原普通（最大化→普通）。 */
    }
}

int demo_page_window_settings_autotest(XWidget* page)
{
    int failures = 0;
    XWidget* top;
#define WN_EXPECT(cond, what) \
    do { if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
         else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } } while (0)

    if (!page || !s_wn.m_ready || page != s_wn.m_root)
        return -1;
    top = wn_toplevel();
    WN_EXPECT(top != NULL, "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE4\xB8\xBB\xE7\xAA\x97\xE9\xA1\xB6\xE5\xB1\x82\xE5\x80\x9F\xE7\x94\xA8\xE5\x8F\xAF\xE7\x94\xA8");

    /* ---- 控件登记完整 ---- */
    WN_EXPECT(s_wn.m_skinCombo && s_wn.m_frameCheck && s_wn.m_statusCheck &&
              s_wn.m_maxCheck && s_wn.m_orientCombo && s_wn.m_mouseCheck &&
              s_wn.m_resetBtn,
              "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE6\x8E\xA7\xE4\xBB\xB6\xE5\x85\xA8\xE9\x83\xA8\xE7\x99\xBB\xE8\xAE\xB0");

    if (top) {
        /* ---- 边框：去框 → 回读 Frameless 位 → 复原 ---- */
        bool frameBefore = (XWidget_windowFlags(top) &
                            (XWidgetFlags)XWindowType_FramelessWindowHint)
                           == 0;
        /* 真实点击路径：勾选/取消必须经按钮的按压-释放派发帧触发槽
           （toggled 在 mouseReleaseEvent 栈内发射），而不是先 setChecked
           再直呼槽——后者绕开了「窗口 flags/几何在释放帧内被改动」这条
           用户实际走到的路径，这正是双条与卡死长期测不出的原因。 */
        XMouseEvent cme;
        XPoint cpos;
        XPoint_init(&cpos, 8, XWidget_height((XWidget*)s_wn.m_frameCheck) / 2);
        XMouseEvent_init(&cme, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                         XMouseButton_LeftButton, 0, cpos);
        XObject_event_base((XObject*)s_wn.m_frameCheck, (XEvent*)&cme);
        XMouseEvent_init(&cme, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                         XMouseButton_LeftButton, 0, cpos);
        XObject_event_base((XObject*)s_wn.m_frameCheck, (XEvent*)&cme);
        WN_EXPECT((XWidget_windowFlags(top) &
                   (XWidgetFlags)XWindowType_FramelessWindowHint) != 0,
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE9\x9A\x90\xE8\x97\x8F\xE8\xBE\xB9\xE6\xA1\x86\xE7\x94\x9F\xE6\x95\x88(Frameless \xE4\xBD\x8D)");
        /* 去框态：框架装饰必须失活，自绘条不得残留（双条的另一半）。 */
        WN_EXPECT(!XWindowDecoration_activeFor(top),
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x8E\xBB\xE6\xA1\x86\xE6\x80\x81\xE8\x87\xAA\xE7\xBB\x98\xE6\xA0\x87\xE9\xA2\x98\xE6\xA0\x8F\xE5\xB7\xB2\xE5\xA4\xB1\xE6\xB4\xBB");
        /* 再点一次：勾回（释放帧内同样要能回到稳定态，不得卡死）。 */
        XMouseEvent_init(&cme, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                         XMouseButton_LeftButton, 0, cpos);
        XObject_event_base((XObject*)s_wn.m_frameCheck, (XEvent*)&cme);
        XMouseEvent_init(&cme, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                         XMouseButton_LeftButton, 0, cpos);
        XObject_event_base((XObject*)s_wn.m_frameCheck, (XEvent*)&cme);
        WN_EXPECT((XWidget_windowFlags(top) &
                   (XWidgetFlags)XWindowType_FramelessWindowHint) == 0,
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE8\xBE\xB9\xE6\xA1\x86\xE5\xA4\x8D\xE5\x8E\x9F");
        WN_EXPECT(XAbstractButton_isChecked((XAbstractButton*)s_wn.m_frameCheck)
                      == frameBefore,
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE8\xBE\xB9\xE6\xA1\x86\xE5\xA4\x8D\xE5\x8E\x9F");
        /* 边框开态（强制 CSD）：框架条必须接管，且原生装饰位不得残留。
           这两条合起来就是「不得同时出现系统标题栏与自定义标题栏」的
           机器判据——只查 Frameless 位会被「位对了但原生样式没落地」
           的假绿骗过（用户实测双条即此形态）。 */
        if (frameBefore) {
        WN_EXPECT(XWindowDecoration_activeFor(top),
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE8\xBE\xB9\xE6\xA1\x86\xE5\xBC\x80\xE6\x80\x81\xE6\xA1\x86\xE6\x9E\xB6\xE8\x87\xAA\xE7\xBB\x98\xE6\xA0\x87\xE9\xA2\x98\xE6\xA0\x8F\xE7\x94\x9F\xE6\x95\x88");
#ifdef WN_HAS_NATIVE_STYLE_FN
        {
            int bits = wn_nativeStyleBits(top);
            WN_EXPECT(bits == 0,
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE6\xA1\x86\xE6\x9E\xB6\xE5\xBC\x80\xE6\x80\x81\xE6\x97\xA0\xE5\x8E\x9F\xE7\x94\x9F\xE8\xA3\x85\xE9\xA5\xB0\xE4\xBD\x8D(\xE5\x8D\x95\xE6\xA0\x87\xE9\xA2\x98\xE6\xA0\x8F)");
        }
#endif
        }

        /* ---- 状态栏：隐藏 → 访问器回读 → 复原 ---- */
        {
            bool sbBefore = demo_main_statusBarVisible(s_wn.m_user);
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_statusCheck,
                                       false);
            wn_statusSlot(NULL, NULL);
            WN_EXPECT(demo_main_statusBarVisible(s_wn.m_user) == false,
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE9\x9A\x90\xE8\x97\x8F\xE7\x8A\xB6\xE6\x80\x81\xE6\xA0\x8F\xE7\x94\x9F\xE6\x95\x88");
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_statusCheck,
                                       sbBefore);
            wn_statusSlot(NULL, NULL);
            WN_EXPECT(
                demo_main_statusBarVisible(s_wn.m_user) == sbBefore,
                "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE7\x8A\xB6\xE6\x80\x81\xE6\xA0\x8F\xE5\xA4\x8D\xE5\x8E\x9F");
        }

        /* ---- 最大化：最大化 → isMaximized 回读 → 还原 ---- */
        {
            bool maxBefore = XWidget_isMaximized(top);
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck,
                                       !maxBefore);
            wn_maxSlot(NULL, NULL);
            WN_EXPECT(XWidget_isMaximized(top) == !maxBefore,
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE6\x9C\x80\xE5\xA4\xA7\xE5\x8C\x96\xE7\x94\x9F\xE6\x95\x88");
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck,
                                       maxBefore);
            wn_maxSlot(NULL, NULL);
            WN_EXPECT(XWidget_isMaximized(top) == maxBefore,
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE6\x9C\x80\xE5\xA4\xA7\xE5\x8C\x96\xE8\xBF\x98\xE5\x8E\x9F");
        }

        /* ---- 方向四方位：90° 竖 → 180° 横(同基准) → 270° 竖 → 0° 还原 ----
         * 竖屏口径=高>宽；对调值按主屏可用几何钳位（1000 高进 900 高屏
         * 即钳 900），故不断言精确对调值；180°/0° 须精确还原基准宽高。
         * 2026-10-18 增补：断言**有效旋转角**逐档命中并回 0——这是
         * 「180° 完全无感」的直接可测面（宽高与 0° 相同，只看宽高
         * 断言无法发现旋转没生效或没还原）。 */
        {
            int w0 = XWidget_width(top);
            int h0 = XWidget_height(top);
            if (h0 <= w0 && w0 > 0) {
                int deg;
                for (deg = 90; deg <= 270; deg += 90) {
                    XComboBox_setCurrentIndex(s_wn.m_orientCombo, deg / 90);
                    wn_orientSlot(NULL, NULL);
                    if (s_wn.m_rotationSupported)
                        WN_EXPECT(XWidget_contentRotationDegrees(top) == deg,
                                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE6\x9C\x89\xE6\x8A\x8A\xE9\xA1\xB5\xE5\xAE\x9A\xE8\xA7\x92\xE6\x89\x8D\xE4\xBD\x8D\xEF\xBC\x88\xE5\xB9\xB2\xE7\x94\x9F\xE6\x95\xB0\xEF\xBC\x89");
                    if (deg == 180)
                        WN_EXPECT(XWidget_width(top) > XWidget_height(top),
                                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x80\x92\xE7\xBD\xAE\xE6\xA8\xAA\xE5\xB1\x8F\xE5\xAE\xBD>\xE9\xAB\x98");
                    else
                        WN_EXPECT(XWidget_height(top) > XWidget_width(top),
                                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE7\xAB\x96\xE5\xB1\x8F\xE5\xAE\xBD\xE9\xAB\x98\xE5\xAF\xB9\xE8\xB0\x83");
                }
                XComboBox_setCurrentIndex(s_wn.m_orientCombo, 0);
                wn_orientSlot(NULL, NULL);
                if (s_wn.m_rotationSupported)
                    WN_EXPECT(XWidget_contentRotationDegrees(top) == 0,
                              "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x9B\x9E\xE5\x8D\xA0\xE5\x85\x8D\xE8\xBD\xAC\xE7\x8E\x87\xE5\xBD\x92\xE9\x9B\xB6");
                WN_EXPECT(XWidget_width(top) == w0 &&
                              XWidget_height(top) == h0,
                          "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE6\xA8\xAA\xE5\xB1\x8F 0\xC2\xB0 \xE8\xBF\x98\xE5\x8E\x9F\xE5\x9F\xBA\xE5\x87\x86\xE5\xB0\xBA\xE5\xAF\xB8");
            }
            /* ---- 方向切换：再走一遍 90°/180°/270° 往返 ----
               （与上方四方位循环同口径；此处只保留行为断言，不模拟弹层
               打开/收起的 UI 时序——弹层模拟在同步自测栈内会与布局重排
               叠加，属测试自身的稳定性问题，与产品行为无关。） */
            {
                int deg;
                for (deg = 90; deg <= 270; deg += 90) {
                    XComboBox_setCurrentIndex(s_wn.m_orientCombo, deg / 90);
                    wn_orientSlot(NULL, NULL);
                    WN_EXPECT(XWidget_contentRotationDegrees(top) == deg ||
                                  !s_wn.m_rotationSupported,
                              "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\xBC\xB9\xE5\xB1\x82\xE9\x80\x89\xE6\x96\xB9\xE4\xBD\x8D\xE5\x90\x8E\xE6\x97\x8B\xE8\xBD\xAC\xE8\xA7\x92\xE7\x94\x9F\xE6\x95\x88");
                    if (deg == 180)
                        WN_EXPECT(XWidget_width(top) == w0 &&
                                      XWidget_height(top) == h0,
                                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: 180\xC2\xB0 \xE5\xAE\xBD\xE9\xAB\x98\xE4\xB8\x8E 0\xC2\xB0 \xE5\x90\x8C");
                }
                XComboBox_setCurrentIndex(s_wn.m_orientCombo, 0);
                wn_orientSlot(NULL, NULL);
            }
        }

        /* ---- 鼠标：隐藏 → 覆盖栈非空 → 恢复 ---- */
        {
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_mouseCheck,
                                       false);
            wn_mouseSlot(NULL, NULL);
            WN_EXPECT(XGuiApplication_overrideCursor() != NULL,
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE9\x9A\x90\xE8\x97\x8F\xE9\xBC\xA0\xE6\xA0\x87\xE7\x94\x9F\xE6\x95\x88(\xE8\xA6\x86\xE7\x9B\x96\xE6\xA0\x88 Blank)");
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_mouseCheck,
                                       true);
            wn_mouseSlot(NULL, NULL);
            WN_EXPECT(XGuiApplication_overrideCursor() == NULL,
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE9\xBC\xA0\xE6\xA0\x87\xE6\x81\xA2\xE5\xA4\x8D(\xE8\xA6\x86\xE7\x9B\x96\xE6\xA0\x88\xE5\xBC\xB9\xE5\x87\xBA)");
        }

        /* ---- 皮肤：切深色 → 方案回读 Dark → 复位浅色 ---- */
        {
            XStyleHints* hints = XGuiApplication_styleHints();
            XComboBox_setCurrentIndex(s_wn.m_skinCombo, 3);
            wn_skinSlot(NULL, NULL);
            WN_EXPECT(hints && XStyleHints_colorScheme(hints) ==
                                  XStyleHintsColorScheme_Dark,
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE6\xB7\xB1\xE8\x89\xB2\xE7\x9A\xAE\xE8\x82\xA4\xE6\x96\xB9\xE6\xA1\x88\xE7\x94\x9F\xE6\x95\x88");
        }

        /* ---- 全屏（2026-10-23）：普通态进全屏 → isFullScreen 回读 +
         * 与最大化互斥回读 → 退出还原。CSD 标题栏随全屏隐藏（装饰层
         * 窗口状态消费）由框架侧保证，本段锁定 API 序列的状态语义：
         * 与用户勾选复选框同一条 wn_fullSlot 路径（XGUI_CSD 双模式
         * 同跑，行为语义一致）。取证保持钩子：XGUI_WN_FULL_DEMO 非
         * "0" 时停在全屏态提前返回（不退出不复位，供 --screenshot/
         * xwd 抓全屏帧；正常回归不带该 env 走完整往返）。 ---- */
        {
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck,
                                       true);
            wn_fullSlot(NULL, NULL);
            WN_EXPECT(XWidget_isFullScreen(top),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x85\xA8\xE5\xB1\x8F\xE8\xBF\x9B\xE5\x85\xA5\xE7\x94\x9F\xE6\x95\x88\x28\x69\x73\x46\x75\x6C\x6C\x53\x63\x72\x65\x65\x6E\x29");
            WN_EXPECT(!XWidget_isMaximized(top),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x85\xA8\xE5\xB1\x8F\xE4\xB8\x8E\xE6\x9C\x80\xE5\xA4\xA7\xE5\x8C\x96\xE4\xBA\x92\xE6\x96\xA5\x28\x6D\x61\x78\x20\xE4\xBD\x8D\xE6\xB8\x85\xE9\x9B\xB6\x29");
            /* 【全屏态边框复选框置灰 2026-10-23】置灰 + 勾选值保持。 */
            WN_EXPECT(s_wn.m_frameCheck &&
                          !XWidget_isEnabled((XWidget*)s_wn.m_frameCheck),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x85\xA8\xE5\xB1\x8F\xE6\x80\x81\xE8\xBE\xB9\xE6\xA1\x86\xE5\xA4\x8D\xE9\x80\x89\xE6\xA1\x86\xE7\xBD\xAE\xE7\x81\xB0\x28\x69\x73\x45\x6E\x61\x62\x6C\x65\x64\x3D\x66\x61\x6C\x73\x65\x29");
            WN_EXPECT(s_wn.m_frameCheck &&
                          XAbstractButton_isChecked(
                              (XAbstractButton*)s_wn.m_frameCheck),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x85\xA8\xE5\xB1\x8F\xE6\x9C\x9F\xE9\x97\xB4\xE8\xBE\xB9\xE6\xA1\x86\xE5\x8B\xBE\xE9\x80\x89\xE4\xBF\x9D\xE6\x8C\x81\xE5\x8E\x9F\xE5\x80\xBC");
            if (wn_fullDemoHold()) {
                WN_EXPECT(!s_wn.m_fullCheck ||
                              XAbstractButton_isChecked(
                                  (XAbstractButton*)s_wn.m_fullCheck),
                          "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x85\xA8\xE5\xB1\x8F\xE5\x8B\xBE\xE9\x80\x89\xE4\xBF\x9D\xE6\x8C\x81\x28\xE5\x8F\x96\xE8\xAF\x81\xE9\x92\xA9\xE5\xAD\x90\x29");
                wn_report("\xE5\xB7\xB2\xE5\x85\xA8\xE5\xB1\x8F\xEF\xBC\x88\xE5\x8F\x96\xE8\xAF\x81\xE4\xBF\x9D\xE6\x8C\x81\xEF\xBC\x89");
                return failures; /* 取证保持：停在全屏态，跳过退出与复位。 */
            }
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck,
                                       false);
            wn_fullSlot(NULL, NULL);
            WN_EXPECT(!XWidget_isFullScreen(top),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE9\x80\x80\xE5\x87\xBA\xE5\x85\xA8\xE5\xB1\x8F\xE8\xBF\x98\xE5\x8E\x9F\x28\x21\x69\x73\x46\x75\x6C\x6C\x53\x63\x72\x65\x65\x6E\x29");
            WN_EXPECT(s_wn.m_frameCheck &&
                          XWidget_isEnabled((XWidget*)s_wn.m_frameCheck),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE9\x80\x80\xE5\x87\xBA\xE5\x85\xA8\xE5\xB1\x8F\xE8\xBE\xB9\xE6\xA1\x86\xE5\xA4\x8D\xE9\x80\x89\xE6\xA1\x86\xE6\x81\xA2\xE5\xA4\x8D\x28\x69\x73\x45\x6E\x61\x62\x6C\x65\x64\x3D\x74\x72\x75\x65\x29");

            /* 最大化态进全屏（同尺寸状态替换路径：几何未变的 FullScreen
             * 位翻转，装饰边距随状态重算+同尺寸重排）：先最大化，再全
             * 屏，断言状态替换完成；再从全屏直还原（normalGeometry 链
             * 完整性：退出回正常态而非最大化/全屏几何）。 */
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck,
                                       true);
            wn_maxSlot(NULL, NULL);
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck,
                                       true);
            wn_fullSlot(NULL, NULL);
            WN_EXPECT(XWidget_isFullScreen(top) && !XWidget_isMaximized(top),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE6\x9C\x80\xE5\xA4\xA7\xE5\x8C\x96\xE6\x80\x81\xE8\xBF\x9B\xE5\x85\xA8\xE5\xB1\x8F\x28\xE7\x8A\xB6\xE6\x80\x81\xE6\x9B\xBF\xE6\x8D\xA2\x29");
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck,
                                       false);
            wn_fullSlot(NULL, NULL);
            WN_EXPECT(!XWidget_isFullScreen(top) && !XWidget_isMaximized(top),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x85\xA8\xE5\xB1\x8F\xE7\x9B\xB4\xE8\xBF\x98\xE5\x8E\x9F\x28\xE5\x87\xA0\xE4\xBD\x95\xE5\x9B\x9E\xE6\xAD\xA3\xE5\xB8\xB8\xE6\x80\x81\x29");

            /* 【FullScreen→Maximized 直切 2026-10-24（2a 回归锁定）】：
               全屏态直点最大化（状态一步替换），此槽不经 wn_fullSlot
               退出臂——直切后最大化落账、全屏清零、边框复选框恢复可
               用；再取消最大化回普通态。 */
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck,
                                       true);
            wn_fullSlot(NULL, NULL);
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck,
                                       true);
            wn_maxSlot(NULL, NULL);
            WN_EXPECT(XWidget_isMaximized(top) && !XWidget_isFullScreen(top),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x85\xA8\xE5\xB1\x8F\xE7\x9B\xB4\xE5\x88\x87\xE6\x9C\x80\xE5\xA4\xA7\xE5\x8C\x96\x28\xE7\x8A\xB6\xE6\x80\x81\xE6\x9B\xBF\xE6\x8D\xA2\x29");
            WN_EXPECT(s_wn.m_frameCheck &&
                          XWidget_isEnabled((XWidget*)s_wn.m_frameCheck),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE7\x9B\xB4\xE5\x88\x87\xE5\x90\x8E\xE8\xBE\xB9\xE6\xA1\x86\xE5\xA4\x8D\xE9\x80\x89\xE6\xA1\x86\xE6\x81\xA2\xE5\xA4\x8D\x28\x32\x61\x20\xE9\x80\x80\xE5\x87\xBA\xE8\xAF\xAD\xE4\xB9\x89\x29");
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck,
                                       false);
            wn_maxSlot(NULL, NULL);
            WN_EXPECT(!XWidget_isMaximized(top) && !XWidget_isFullScreen(top),
                      "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE7\x9B\xB4\xE5\x88\x87\xE5\x90\x8E\xE8\xBF\x98\xE5\x8E\x9F\xE6\x99\xAE\xE9\x80\x9A\xE6\x80\x81");
        }
    }

    /* ---- 复位默认：全部回默认并断言 ---- */
    /* 【复位从全屏态 2026-10-24】复位须覆盖全屏：先置全屏再复位，断言
       复位退出全屏且边框复选框恢复可用（复位前全屏置灰中）。 */
    XAbstractButton_setChecked((XAbstractButton*)s_wn.m_fullCheck, true);
    wn_fullSlot(NULL, NULL);
    wn_resetSlot(NULL, NULL);
    WN_EXPECT(top && !XWidget_isFullScreen(top) && !XWidget_isMaximized(top),
              "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x85\xA8\xE5\xB1\x8F\xE6\x80\x81\xE5\xA4\x8D\xE4\xBD\x8D\xE9\x80\x80\xE5\x87\xBA\xE5\x85\xA8\xE5\xB1\x8F");
    WN_EXPECT(s_wn.m_frameCheck &&
                  XWidget_isEnabled((XWidget*)s_wn.m_frameCheck),
              "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\x85\xA8\xE5\xB1\x8F\xE6\x80\x81\xE5\xA4\x8D\xE4\xBD\x8D\xE5\x90\x8E\xE8\xBE\xB9\xE6\xA1\x86\xE5\xA4\x8D\xE9\x80\x89\xE6\xA1\x86\xE6\x81\xA2\xE5\xA4\x8D");
    wn_resetSlot(NULL, NULL);
    {
        XStyleHints* hints = XGuiApplication_styleHints();
        WN_EXPECT(s_wn.m_skinCombo &&
                      XComboBox_currentIndex(s_wn.m_skinCombo) == 0,
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\xA4\x8D\xE4\xBD\x8D\xE5\x90\x8E\xE7\x9A\xAE\xE8\x82\xA4\xE5\x9B\x9E\xE6\xB5\x85\xE8\x89\xB2");
        WN_EXPECT(hints && XStyleHints_colorScheme(hints) ==
                              XStyleHintsColorScheme_Light,
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\xA4\x8D\xE4\xBD\x8D\xE5\x90\x8E\xE6\x96\xB9\xE6\xA1\x88\xE5\x9B\x9E\xE6\xB5\x85\xE8\x89\xB2");
        WN_EXPECT(top && (XWidget_windowFlags(top) &
                          (XWidgetFlags)XWindowType_FramelessWindowHint) == 0,
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\xA4\x8D\xE4\xBD\x8D\xE5\x90\x8E\xE8\xBE\xB9\xE6\xA1\x86\xE6\x98\xBE\xE7\xA4\xBA");
        WN_EXPECT(demo_main_statusBarVisible(s_wn.m_user),
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\xA4\x8D\xE4\xBD\x8D\xE5\x90\x8E\xE7\x8A\xB6\xE6\x80\x81\xE6\xA0\x8F\xE6\x98\xBE\xE7\xA4\xBA");
        WN_EXPECT(top && !XWidget_isMaximized(top),
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\xA4\x8D\xE4\xBD\x8D\xE5\x90\x8E\xE9\x9D\x9E\xE6\x9C\x80\xE5\xA4\xA7\xE5\x8C\x96");
        WN_EXPECT(XGuiApplication_overrideCursor() == NULL,
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE5\xA4\x8D\xE4\xBD\x8D\xE5\x90\x8E\xE9\xBC\xA0\xE6\xA0\x87\xE6\x98\xBE\xE7\xA4\xBA");
    }

#undef WN_EXPECT
    XPrintf("XGuiAutoTest: \xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5 %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}

#else /* 裁剪: 契约桩（符号常在, 主文件可无条件链接） */

XWidget* demo_page_window_settings_build(XWidget* parent,
                                         DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    XPrintf("XGuiAutoTest: [FAIL] \xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5\xE4\xBE\x9D\xE8\xB5\x96\xE6\x8E\xA7\xE4\xBB\xB6\xE6\xAE\xB5\xE8\xA2\xAB\xE8\xA3\x81\xE5\x89\xAA\xEF\xBC\x8C\xE6\x97\xA0\xE6\xB3\x95\xE6\x9E\x84\xE5\xBB\xBA\n");
    return (XWidget*)0;
}

int demo_page_window_settings_autotest(XWidget* page)
{
    (void)page;
    XPrintf("XGuiAutoTest: [FAIL] \xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5\xE8\xA2\xAB\xE8\xA3\x81\xE5\x89\xAA\xEF\xBC\x8C\xE6\x97\xA0\xE6\xB3\x95\xE8\x87\xAA\xE6\xB5\x8B\n");
    return -1;
}

void demo_page_window_settings_adapt(XWidget* page)
{
    (void)page;
}

#endif /* 控件段守卫 */
