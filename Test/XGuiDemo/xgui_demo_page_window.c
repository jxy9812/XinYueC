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
#include "xgui_demo_theme.h"
#include "xgui_demo_pages.h"

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
    (void)receiver; (void)args;
    top = wn_toplevel();
    if (!top || !s_wn.m_maxCheck || s_wn.m_syncing) return;
    max = XAbstractButton_isChecked((XAbstractButton*)s_wn.m_maxCheck);
    if (max)
        XWidget_showMaximized(top);
    else
        XWidget_showNormal(top);
    wn_report(max ? "\xE5\xB7\xB2\xE6\x9C\x80\xE5\xA4\xA7\xE5\x8C\x96\xE6\x98\xBE\xE7\xA4\xBA" : "\xE5\xB7\xB2\xE8\xBF\x98\xE5\x8E\x9F\xE6\x99\xAE\xE9\x80\x9A\xE5\x87\xA0\xE4\xBD\x95");
}

/** @brief 窗口方向：横竖屏宽高对调（进竖屏存横屏尺寸；按屏钳位）。 */
/** @brief 窗口方向应用（0/90/180/270；基准=0° 横屏尺寸）。
 * @details 四方位只旋转窗口外框几何：0°/180° 同基准宽、90°/270° 宽高
 *          对调，按主屏可用几何钳位防越屏；最大化先还原（几何由窗口
 *          状态接管）。基准尺寸懒捕获（首次离开 0° 时若未存，按当前
 *          横向几何取正）。 */
static int wn_orientApply(int deg, int* outW, int* outH)
{
    XWidget* top = wn_toplevel();
    XScreen* screen;
    int availW = 4096;
    int availH = 4096;
    int bw, bh, w, h, x, y;
    bool wasMax;
    if (!top) return 0; /* 无顶层=探测必败（原裸 return 非零值返回 UB，告警修）。 */
    wasMax = XWidget_isMaximized(top);
    if (wasMax) {
        /* 最大化（全屏）态换向保持全屏（2026-10-22 用户需求）：归位软件
           状态后换向，换向经 showMaximized 取旋转感知的最大化几何
           （90/270 目标=对调屏矩形，footprint 恰铺满面板），最大化标志
           与勾选框全程保持。 */
        XWidget_showNormal(top);
        s_wn.m_syncing = true;
        if (s_wn.m_maxCheck)
            XAbstractButton_setChecked((XAbstractButton*)s_wn.m_maxCheck,
                                       true);
        s_wn.m_syncing = false;
    }
    /* 平台支持探测（2026-10-18）：setter 返回 false 时下拉框置灰并给
       说明文案，不让用户点了没反应。 */
    if (!s_wn.m_rotationProbed)
    {
        s_wn.m_rotationProbed = true;
        /* 探测口径：setter 只在「平台不支持且角度非 0」时返回 false，故用
           一个非 0 角试一次再立即还原到探测前的角度。build 阶段在首次
           show 之前调用，此时无任何帧上屏，来回不产生可见闪烁。 */
        {
            int before = XWidget_contentRotationDegrees(top);
            s_wn.m_rotationSupported = XWidget_setContentRotation(top, 90);
            XWidget_setContentRotation(top, before);
        }
        if (!s_wn.m_rotationSupported && s_wn.m_orientCombo)
            XWidget_setEnabled((XWidget*)s_wn.m_orientCombo, false);
        fprintf(stderr, "[WN] rot probe: supported=%d top=%p handle=%p\n",
                (int)s_wn.m_rotationSupported, (void*)top,
                top ? top->m_windowHandle : NULL);
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
    int w = 0;
    int h = 0;
    (void)receiver; (void)args;
    if (!s_wn.m_orientCombo || s_wn.m_syncing) return;
    index = XComboBox_currentIndex(s_wn.m_orientCombo);
    if (index < 0 || index > 3) index = 0;
    if (!wn_orientApply(index * 90, &w, &h))
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
            if (XWidget_isMaximized(top))
                XWidget_showNormal(top);
        }
    }
    demo_main_setStatusBarVisible(s_wn.m_user, true);
    /* 回 0°（含最大化还原 + 显式归零旋转角）。显式归零是必需的：复位
       全程持 m_syncing，setCurrentIndex 不触发槽，旋转角不会自动落 0。 */
    wn_orientApply(0, NULL, NULL);
    XGuiApplication_restoreOverrideCursor();
    wn_syncFromWindow();
    wn_report("\xE5\xB7\xB2\xE5\xA4\x8D\xE4\xBD\x8D\xE9\xBB\x98\xE8\xAE\xA4");
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
        XWidget_setFixedHeight((XWidget*)s_wn.m_hintLabel, 20);
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
    return s_wn.m_root;
}

/* ==================== 自适应重排（xgui_demo_pages.h 契约） ============ */

void demo_page_window_settings_adapt(XWidget* page)
{
    /* 2026-10-17 容器化：页内排布全部交给根布局器（提示/状态行经
     * stretch 贴底、随根几何自动重排），此处不再做任何手写几何；
     * 契约签名保留（主文件调用点不动）。 */
    (void)page;
}

/* ==================== autotest ==================== */

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
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_frameCheck,
                                   false);
        wn_frameSlot(NULL, NULL);
        WN_EXPECT((XWidget_windowFlags(top) &
                   (XWidgetFlags)XWindowType_FramelessWindowHint) != 0,
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE9\x9A\x90\xE8\x97\x8F\xE8\xBE\xB9\xE6\xA1\x86\xE7\x94\x9F\xE6\x95\x88(Frameless \xE4\xBD\x8D)");
        XAbstractButton_setChecked((XAbstractButton*)s_wn.m_frameCheck,
                                   frameBefore);
        wn_frameSlot(NULL, NULL);
        WN_EXPECT(((XWidget_windowFlags(top) &
                    (XWidgetFlags)XWindowType_FramelessWindowHint) == 0)
                      == frameBefore,
                  "\xE7\xAA\x97\xE5\x8F\xA3\xE8\xAE\xBE\xE7\xBD\xAE\xE9\xA1\xB5: \xE8\xBE\xB9\xE6\xA1\x86\xE5\xA4\x8D\xE5\x8E\x9F");

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
    }

    /* ---- 复位默认：全部回默认并断言 ---- */
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
