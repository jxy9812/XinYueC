/**
 * @file       HsfSettingsLocal.c
 * @brief      本地设置页：显示（紧凑模式/全屏）/ 数据刷新（界面刷新周期）。
 * @details    [对齐 Python 客户端 LocalSettingsPage] 两张卡：
 *              ① 显示：紧凑模式（强制竖屏窄档，经 HsfUi_setForceCompact
 *                 即时生效）+ 全屏显示（主窗 showFullScreen/showNormal，
 *                 与窗口设置页共用 Window/FullScreen 键）；
 *              ② 数据刷新：界面刷新周期（各数据页轮询定时器建页时取
 *                 HsfSettingsLocal_refreshMs()）。
 *             仅本机生效，不发给服务端；改动即写 INI 并 sync 落盘
 *             （XSettings 无空闲落盘钩子，不 sync 则退出前崩溃会丢）。
 *             [与 Python 的有意偏差] Python 的「界面缩放」下拉本端不设：
 *             框架无运行时缩放钩子（字号由各控件 setTextPixelSize 显式
 *             指定），摆出来就是改了没反应的假控件——远端设置页采集
 *             配置只读化的同一原则。Python 端该下拉亦未接线（摆设）。
 */
#include "HsfSettingsLocal.h"

#include "HsfTheme.h"
#include "HsfUi.h"
#include "HsfSettingsStore.h"
#include "XAbstractButton.h"
#include "XBoxLayout.h"
#include "XCheckBox.h"
#include "XLabel.h"
#include "XSpinBox.h"
#include "XMemory.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XCHECKBOX_ON && XSPINBOX_ON

/* ---- 键名（hsf_client.ini；Display/Compact 由 HsfUi 启动恢复共读） ---- */
#define LS_KEY_COMPACT   "Display/Compact"   /**< 1=强制紧凑 -1=跟随自动 */
#define LS_KEY_FULLSCR   "Window/FullScreen" /**< 与窗口设置页同键（单事实源） */
#define LS_KEY_REFRESH   "Data/RefreshMs"    /**< 界面刷新周期 ms */

#define LS_REFRESH_DEF    200  /**< 刷新周期缺省（Python 同值）。 */
#define LS_REFRESH_MIN    100  /**< 刷新周期下限（Python 同值）。 */
#define LS_REFRESH_MAX    2000 /**< 刷新周期上限（Python 同值）。 */

/** @brief 本地设置页对象。 */
typedef struct HsfSettingsLocal {
    XWidget      m_base;        /**< 页根（第一成员）。 */
    XBoxLayout   m_layout;      /**< 页根竖排布局。 */
    XLabel       m_title;       /**< 页标题。 */
    XCheckBox    m_chkCompact;  /**< 紧凑模式（强制窄档）。 */
    XCheckBox    m_chkFull;     /**< 全屏显示。 */
    XSpinBox     m_refresh;     /**< 界面刷新周期（ms）。 */
    void*        m_user;        /**< 主窗契约上下文（HsfPages_setStatus 用）。 */
    bool         m_noPersist;   /**< 建页期编程赋值中（不得落盘）。 */
} HsfSettingsLocal;

/* ==================== 槽 ==================== */

/** @brief 紧凑模式：即时生效 + 落盘（勾=强制窄档，去勾=恢复自动判定）。 */
static void lsCompactSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_chkCompact));
    bool on;
    (void)args;
    if (!self || self->m_noPersist) return;
    on = XCheckBox_isChecked(&self->m_chkCompact);
    HsfUi_setForceCompact(on);
    HsfSettingsStore_setInt(LS_KEY_COMPACT, on ? 1 : -1);
    HsfSettingsStore_sync();
    HsfPages_setStatus(self->m_user,
                       on ? "\xE7\xB4\xA7\xE5\x87\x91\xE6\xA8\xA1\xE5\xBC\x8F\xEF\xBC\x9A\xE5\xBC\x80"
                          : "\xE7\xB4\xA7\xE5\x87\x91\xE6\xA8\xA1\xE5\xBC\x8F\xEF\xBC\x9A\xE5\x85\xB3\xEF\xBC\x88\xE8\x87\xAA\xE5\x8A\xA8\xE5\x88\xA4\xE5\xAE\x9A\xEF\xBC\x89");
    /* 紧凑模式：开 / 紧凑模式：关（自动判定） */
}

/** @brief 全屏显示：主窗窗口态翻转 + 落盘（窗口设置页启动恢复共读同键）。 */
static void lsFullSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_chkFull));
    XWidget* top;
    bool on;
    (void)args;
    if (!self || self->m_noPersist) return;
    top = XWidget_window(&self->m_base);
    on = XCheckBox_isChecked(&self->m_chkFull);
    if (top) {
        if (on) XWidget_showFullScreen(top);
        else    XWidget_showNormal(top);
    }
    HsfSettingsStore_setInt(LS_KEY_FULLSCR, on ? 1 : 0);
    HsfSettingsStore_sync();
    HsfPages_setStatus(self->m_user,
                       on ? "\xE5\x85\xA8\xE5\xB1\x8F\xEF\xBC\x9A\xE5\xBC\x80"
                          : "\xE5\x85\xA8\xE5\xB1\x8F\xEF\xBC\x9A\xE5\x85\xB3");
    /* 全屏：开 / 全屏：关 */
}

/** @brief 刷新周期改动：落盘（各页轮询定时器在下一次建页/重启后生效）。 */
static void lsRefreshSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)((char*)receiver -
                                          offsetof(HsfSettingsLocal, m_refresh));
    char buf[64];
    (void)args;
    if (!self || self->m_noPersist) return;
    HsfSettingsStore_setInt(LS_KEY_REFRESH, XSpinBox_value(&self->m_refresh));
    HsfSettingsStore_sync();
    snprintf(buf, sizeof(buf),
             "\xE7\x95\x8C\xE9\x9D\xA2\xE5\x88\xB7\xE6\x96\xB0\xE5\x91\xA8\xE6\x9C\x9F\xEF\xBC\x9A%d ms", /* 界面刷新周期：N ms */
             XSpinBox_value(&self->m_refresh));
    HsfPages_setStatus(self->m_user, buf);
}

/* ==================== 建页 ==================== */

XWidget* HsfSettingsLocal_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData)
{
    HsfSettingsLocal* self;
    (void)navFn;

    if (!parent) return NULL;
    self = (HsfSettingsLocal*)XMemory_malloc(sizeof(HsfSettingsLocal),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    self->m_user = userData;       /* 状态栏契约上下文（HsfWin）。 */
    HsfTheme_applyDefaultFont(&self->m_base);

    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 16, 12, 16, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title,
                     "\xE6\x9C\xAC\xE5\x9C\xB0\xE8\xAE\xBE\xE7\xBD\xAE"); /* 本地设置 */
    XLabel_setTextPixelSize(&self->m_title, 16);
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    /* ---- 显示卡 ---- */
    XCheckBox_init(&self->m_chkCompact, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_chkCompact);
    XCheckBox_setText_2(&self->m_chkCompact,
                        "\xE7\xB4\xA7\xE5\x87\x91\xE6\xA8\xA1\xE5\xBC\x8F\xEF\xBC\x88\xE7\xAB\x96\xE5\xB1\x8F/\xE5\xB0\x8F\xE5\xB1\x8F\xEF\xBC\x89"); /* 紧凑模式（竖屏/小屏） */
    /* 初值=持久化值（缺省 -1=跟随自动 → 不勾）。建页期只读键不写键。 */
    self->m_noPersist = true;
    XAbstractButton_setChecked((XAbstractButton*)&self->m_chkCompact,
                               HsfSettingsStore_getInt(LS_KEY_COMPACT, -1) == 1);
    self->m_noPersist = false;
    XWidget_show((XWidget*)&self->m_chkCompact);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_chkCompact);
    XObject_connect_2((XObject*)&self->m_chkCompact,
                      XSignal(XAbstractButton_toggled_signal), lsCompactSlot);

    XCheckBox_init(&self->m_chkFull, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_chkFull);
    XCheckBox_setText_2(&self->m_chkFull,
                        "\xE5\x85\xA8\xE5\xB1\x8F\xE6\x98\xBE\xE7\xA4\xBA"); /* 全屏显示 */
    self->m_noPersist = true;
    XAbstractButton_setChecked((XAbstractButton*)&self->m_chkFull,
                               HsfSettingsStore_getInt(LS_KEY_FULLSCR, 0) == 1);
    self->m_noPersist = false;
    XWidget_show((XWidget*)&self->m_chkFull);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_chkFull);
    XObject_connect_2((XObject*)&self->m_chkFull,
                      XSignal(XAbstractButton_toggled_signal), lsFullSlot);

    /* ---- 数据刷新卡 ---- */
    XSpinBox_init(&self->m_refresh, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_refresh);
    XSpinBox_setRange(&self->m_refresh, LS_REFRESH_MIN, LS_REFRESH_MAX);
    XSpinBox_setSingleStep(&self->m_refresh, 100);
    XSpinBox_setSuffix(&self->m_refresh, " ms");
    self->m_noPersist = true;
    XSpinBox_setValue(&self->m_refresh, HsfSettingsLocal_refreshMs());
    self->m_noPersist = false;
    XWidget_setFixedWidth((XWidget*)&self->m_refresh, 140);
    XWidget_show((XWidget*)&self->m_refresh);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_refresh);
    XObject_connect_2((XObject*)&self->m_refresh,
                      XSignal(XSpinBox_valueChanged_signal),
                      lsRefreshSlot);

    /* 尾部弹性空白：多余竖向空间归页尾，控件保持自然高（Python
     * root.addStretch(1) 同款——不加则布局把余量摊给各控件拉高）。 */
    XBoxLayout_addStretch(&self->m_layout, 1);

    XWidget_show(&self->m_base);
    return &self->m_base;
}

void HsfSettingsLocal_adapt(XWidget* page, bool compact)
{
    HsfSettingsLocal* self = (HsfSettingsLocal*)page;
    if (!self) return;
    /* 竖屏/窄窗：页边距 16,12→8,6 腾内容宽（与本页 create 边距同源）。
     * 幂等（帧泵每循环调用）。自建根结构必须走自己的 adapt（见
     * HsfDataHistory_adapt 处的类型混淆教训）。 */
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 8 : 16, compact ? 6 : 12,
                               compact ? 8 : 16, compact ? 6 : 12);
}

int HsfSettingsLocal_refreshMs(void)
{
    int ms = HsfSettingsStore_getInt(LS_KEY_REFRESH, LS_REFRESH_DEF);
    if (ms < LS_REFRESH_MIN) ms = LS_REFRESH_MIN;
    if (ms > LS_REFRESH_MAX) ms = LS_REFRESH_MAX;
    return ms;
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XCHECKBOX_ON && XSPINBOX_ON */
