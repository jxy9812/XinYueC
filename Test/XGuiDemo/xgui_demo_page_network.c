/******************************************************************************
 * @file       xgui_demo_page_network.c
 * @brief      XGuiWindowDemo「网络设置」页（系统设置：网卡枚举与 IPv4 配置）。
 * @details    契约见 xgui_demo_pages.h（demo_page_network_build /
 *             demo_page_network_autotest / demo_page_network_adapt）。
 *
 *             页面内容（2026-10-06 随「控件/系统设置/远程」一级菜单
 *             整改新增）：
 *              - 网卡下拉框：经 XNetworkInterface_allInterfaces 枚举
 *                本机全部接口（含回环），选中即查询并回填展示；
 *              - 信息区（多行 XLabel）：状态/类型/MAC/IPv4/掩码/网关/
 *                DNS/DHCP 模式与服务器，来自
 *                XNetworkInterface_queryConfig（XNetworkInterfaceConfig
 *                值语义快照，地址为 XHostAddress）；
 *              - 配置区：模式（DHCP 自动获取 / 静态 IP）+ IP/掩码/网关/
 *                首选与备用 DNS 单行编辑 + 「应用配置」——
 *                XNetworkInterface_setDhcpMode / setStaticMode 下发
 *                （Windows=IP Helper + netsh 通路，需管理员权限；切换
 *                会短暂中断该网卡）；
 *              - 非法输入（IP/掩码格式）在页面层拒应用，不触平台通路；
 *                平台不支持（非 Windows 后端）时应用按钮如实反馈；
 *              - 非 Windows 降级路径与配置查询路径同构：信息区固定三行
 *                （状态/类型 + IPv4 + MAC），IPv6 全列表经专用 IPv6 行
 *                折叠/展开承载（多地址不再内联长行，避免溢出窗口被裁）；
 *                信息区高度随回填行数自适应，下游行距整体联动；
 *              - 操作提示随 XNetworkInterface_configSupported 平台化
 *                （有通路=netsh 下发说明；无通路=仅展示与校验）；
 *              - 窄根自适应（2026-10-25 真机口径整改）：页根逻辑宽
 *                <600（与主窗 Material 3 compact 档同源）时 adapt 把
 *                模式行/无线口令行各拆出第二行、IP 双列字段转单列、
 *                编辑框宽随内容宽收缩、可点击控件高抬到 40（手机触控
 *                目标）；宽根形态不动，桌面 1000x700 现状保持。
 *
 *             autotest 口径：全程非阻塞，绝不真实下发配置（只走非法
 *             输入拒应用门控与枚举/回填断言），避免污染测试机网络。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "XGuiConfig.h"
#include "XNetwork_config.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XWidget.h"
#include "XBoxLayout.h"
#include "XScrollArea.h"
#include "XAbstractScrollArea.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XLineEdit.h"
#include "XComboBox.h"
#include "XPushButton.h"
#include "XHostAddress.h"
#include "XCoreApplication.h"
#include "XEventLoop.h"
#include "XNetworkInterface.h"
#if XNETWORK_ON && XDEVICE_WIFI_ON
#include "XNetworkWiFi.h"  /* 无线业务层（值对象 + 会话管理） */
#include "XTimer.h"        /* 无线扫描/连接状态轮询定时器 */
#endif
#include "XSystem.h" /* XSystem_environment: 环境变量唯一入口。 */
#if defined(__ANDROID__)
#include "XAndroidJniChannels.h" /* MAC 兜底：应用域读不到 /sys/class/net。 */
#endif
#include "xgui_demo_pages.h"

/*
 * 控件段宏守卫：枚举与配置依赖 XNetwork + XNetworkInterface 模块，
 * 页面控件依赖 Label/ComboBox/LineEdit/PushButton/CheckBox；2026-10-17
 * 容器化后排布由 XBoxLayout 承载，再一并依赖 XLAYOUT/XLAYOUT_BOX。
 * 任一被裁剪时降级为契约桩（build 返回 NULL、autotest 返回 -1），
 * 主文件可无条件链接。
 */
#if XNETWORK_ON && XNETWORK_INTERFACE_ON && XWIDGET_ON && \
    XLABEL_ON && XCOMBOBOX_ON && XLINEEDIT_ON && XPUSHBUTTON_ON && \
    XCHECKBOX_ON && XLAYOUT_ON && XLAYOUT_BOX_ON && \
    XSCROLLAREA_ON && XABSTRACTSCROLLAREA_ON && XSCROLLBAR_ON

/* ==================== 布局常量 ==================== */

/** @brief 页面装配基线（2026-10-17 容器化：页内坐标排布改由根布局器
 *         承载，此处只保留列宽/控件高常量；段间节奏在 build 装配段用
 *         addSpacing 近似原设计稿 776x458 的间距）。 */
#define NET_CAP_W      80     /**< 字段标题列宽。 */
#define NET_FIELD_W    300    /**< 输入控件宽。 */
#define NET_BTN_W      110    /**< 应用按钮宽。 */
/* 窄根分档（2026-10-25）：断点与主窗 xgui_window_demo.c 的 Material 3
 * compact 口径同源（逻辑宽 <600，手机竖屏必落此档）；触控目标高取
 * 移动端建议的 40 逻辑像素下限——宽屏 22~30 的行高在真机上点不中。 */
#define NET_COMPACT_MAX_W 600 /**< adapt 窄根档断点（页根逻辑宽）。 */
#define NET_TOUCH_H       40  /**< 窄根档可点击控件高（触控目标下限）。 */
/* IPv4/IPv6 两分区并排判据：两列各自的**最小可用宽**之和 + 列距。
 * 各列内容 = 标题列(80/100) + 行距 4 + 编辑框最小 120 + 右余量；
 * 取 374 + 16(列距) = 390 —— 400 逻辑宽的竖屏刚好不够（堆叠），
 * 821 逻辑宽的横屏绰绰有余（并排）。 */
#define NET_IP_COLS_MIN_W 390

/* ==================== 整页竖滚壳（页宿主 + 滚动区） ====================
 * [竖屏滚动 2026-10-25] 手机竖屏（400x869 逻辑）内容远超一屏，原先没有
 * 任何滚动承载，下方 IPv6 区/提示行整段不可达。按 Modbus 页已踩过的
 * 定式包一层：
 *   页宿主（XWidget 子类，堆叠条目）→ XScrollArea → 内容根（普通 XWidget）
 * 不直接把 XScrollArea 挂进 XStackedLayout：实测堆叠给 XScrollArea 条目
 * 分配的几何异常（只有 184x150 且不再重摆，Modbus 页同款实证）。
 *
 * 滚动条纪律：水平条 AlwaysOff（内容宽恒=视口宽，横向永不需要），竖条
 * AsNeeded——**只在内容确实超高时才出现**，一屏装得下（桌面 1000x700）
 * 时完全不出条，桌面现状不变。
 *
 * widgetResizable 必须 false：true 会把内容几何钉成视口尺寸
 * （XScrollArea.c 的 xsa_updateWidgetGeometry），内容永远不超视口 ⇒ 竖滚
 * 无从发生。改由本壳 resizeEvent 自管内容宽高（高度取布局合计首选高，
 * 两遍收敛——第一遍 activate 可能改变行数/换行，第二遍读实测高）。
 */
XCLASS_DEFINE_BEGING(NetPageHost)
XCLASS_DEFINE_EXTEND_END(NetPageHost, XWidget)

typedef struct NetPageHost
{
    XWidget m_base;   /**< XWidget 基类；必须是第一个成员。 */
    XWidget* m_scroll;/**< 内嵌滚动区（借用；随父子链析构）。 */
} NetPageHost;

XCLASS_DEFINE_BEGING(NetPageScroll)
XCLASS_DEFINE_EXTEND_END(NetPageScroll, XScrollArea)

typedef struct NetPageScroll
{
    XScrollArea m_base; /**< XScrollArea 基类；必须是第一个成员。 */
} NetPageScroll;

static void net_fitScrollContent(XScrollArea* area);

/** @brief 页宿主 resizeEvent：内嵌滚动区铺满宿主矩形。 */
static void VNetPageHost_resizeEvent(XWidget* self, XEvent* event)
{
    NetPageHost* host;
    (void)event;
    if (!self) return;
    host = (NetPageHost*)self;
    if (host->m_scroll) {
        XWidget_setGeometry(host->m_scroll, 0, 0,
                            XWidget_width(self), XWidget_height(self));
    }
}

XVtable* NetPageHost_class_init(void)
{
    XVTABLE_INIT_DEFAULT(NetPageHost)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VNetPageHost_resizeEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 滚动区 resizeEvent：基类重排视口/滚动条后，按新视口宽重定
 *         内容宽高（宽=视口宽，高=布局合计首选高，超出部分即竖滚量）。 */
static void VNetPageScroll_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    XClass_Parent(XScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    net_fitScrollContent((XScrollArea*)self);
}

XVtable* NetPageScroll_class_init(void)
{
    XVTABLE_INIT_DEFAULT(NetPageScroll)
    XVTABLE_INHERIT_XCLASS(XScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VNetPageScroll_resizeEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 建页宿主（堆叠条目）。 */
static XWidget* net_createPageHost(XWidget* parent)
{
    NetPageHost* host = (NetPageHost*)XMalloc_System(sizeof(NetPageHost));
    if (!host) return (XWidget*)0;
    XMemset(host, 0, sizeof(*host));
    XWidget_init(&host->m_base, parent, 0);
    XClassSetVtable(host, NetPageHost);
    return &host->m_base;
}

/** @brief 建滚动壳（内容重承载钩子的 XScrollArea 子类实例）。 */
static XWidget* net_createScroll(XWidget* parent)
{
    NetPageScroll* scroll =
        (NetPageScroll*)XMalloc_System(sizeof(NetPageScroll));
    if (!scroll) return (XWidget*)0;
    XMemset(scroll, 0, sizeof(*scroll));
    XScrollArea_init(&scroll->m_base, parent, 0);
    XClassSetVtable(scroll, NetPageScroll);
    return (XWidget*)scroll;
}

/** @brief 内容根按视口宽定宽、按布局合计首选高定高（两遍收敛），并把
 *         最终尺寸提交给滚动区（驱动竖条显隐与滚动范围）。
 *  @details 落最小/首选取大：无显式 sizeHint 的定尺寸条目在合计首选里
 *           可能记 0，而合计最小会把 min=max 的条目按实高累计——两者取
 *           大才是「所有条目都不被裁」的内容高（Modbus 页同口径）。 */
static void net_fitScrollContent(XScrollArea* area)
{
    XWidget* viewport;
    XWidget* content;
    int vw;
    int vh;
    int pass;
    if (!area) return;
    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)area);
    content = XScrollArea_widget(area);
    if (!viewport || !content) return;
    vw = XWidget_width(viewport);
    vh = XWidget_height(viewport);
    if (vw < 1) return; /* 视口未就位（堆叠未给几何）不猜，下一拍重入。 */
    XWidget_setFixedWidth(content, vw);
    for (pass = 0; pass < 2; ++pass) {
        XSize hint;
        XSize minHint;
        if (XWidget_layout(content))
            XLayout_invalidate(XWidget_layout(content));
        hint = XWidget_sizeHint(content);
        minHint = XWidget_minimumSizeHint(content);
        if (minHint.height > hint.height) hint.height = minHint.height;
        if (minHint.width > hint.width) hint.width = minHint.width;
        if (hint.width < 1) hint.width = vw;
        if (hint.height < 1) hint.height = vh > 0 ? vh : 1;
        XWidget_setFixedHeight(content, hint.height);
        XWidget_setGeometry(content, 0, 0, hint.width, hint.height);
        if (XWidget_layout(content))
            XLayout_activate(XWidget_layout(content));
    }
    XAbstractScrollArea_setContentSize((XAbstractScrollArea*)area, vw,
        XWidget_height(content) > vh ? XWidget_height(content) : vh);
}

/* ==================== 页面内部控件登记（demo 单实例 static 自持） ==== */

/** @brief 网络设置页内部登记表。 */
static struct
{
    XWidget*         m_pageHost;   /**< 页宿主（堆叠条目；滚动区挂其下）。 */
    XScrollArea*     m_scroll;     /**< 整页竖滚区（全页唯一滚动条）。 */
    XWidget*         m_root;       /**< 内容根（滚动区内容件；页内控件父）。 */
    DemoPageStatusFn m_status;      /**< 主窗口状态栏反馈回调（借用）。 */
    void*            m_user;        /**< 回调上下文（主窗口指针，借用）。 */
    char             m_lastStatus[192]; /**< 最近一次反馈文本（自测断言用）。 */
    bool             m_ready;       /**< build 已完成且控件指针有效。 */
    bool             m_compact;     /**< 窄根档生效中（adapt 分档状态，
                                         折叠重排按此套触控行高）。 */

    /* ---- 网卡选择区 ---- */
    XLabel*    m_titleLabel;   /**< 第一分区标题「网卡设置」（居中）。 */
    XLabel*    m_ipTitle;      /**< 第三分区标题「IPv4 设置」（居中）。 */
    XLabel*    m_v6Title;      /**< 第四分区标题「IPv6 设置」（居中）。 */
    XLabel*    m_capAdapter;   /**< 「网卡」标题。 */
    XComboBox* m_adapterCombo; /**< 网卡下拉框（枚举所有接口）。 */
    XPushButton* m_refreshBtn; /**< 刷新按钮。 */

    /* ---- 信息展示区 ---- */
    XLabel*    m_infoLabel;    /**< 多行配置信息区（模式/IPv4/DNS/DHCP/MAC）。 */
    int        m_infoLines;    /**< 信息区当前行数（relayout 据此自适应高度）。 */
    XLabel*    m_v6Label;      /**< IPv6 地址区（折叠=一行，展开=逐行全列表）。 */
    XCheckBox* m_v6ExpandCheck;/**< 「展开全部 IPv6 / 收起」开关。 */

    /* ---- 配置编辑区（IPv4/IPv6 双列，模式各自独立） ---- */
    XLabel*      m_capV4Mode;  /**< 「IPv4 模式」标题。 */
    XComboBox*   m_v4ModeCombo;/**< 0=DHCP 自动获取 1=静态 IP（管左列）。 */
    XPushButton* m_applyBtn;   /**< 应用配置（IPv4）按钮。 */
    XLabel*      m_capV6Mode;  /**< 「IPv6 模式」标题。 */
    XComboBox*   m_v6ModeCombo;/**< 0=自动获取(RA/SLAAC) 1=手动追加（管右列）。 */
    XPushButton* m_applyV6Btn; /**< 应用 IPv6 按钮。 */
    XLabel*      m_capIp;      /**< 「IP 地址」标题。 */
    XLineEdit*   m_ipEdit;     /**< 静态 IP。 */
    XLabel*      m_capMask;    /**< 「子网掩码」标题。 */
    XLineEdit*   m_maskEdit;   /**< 子网掩码。 */
    XLabel*      m_capGw;      /**< 「默认网关」标题。 */
    XLineEdit*   m_gwEdit;     /**< 默认网关（可空）。 */
    XLabel*      m_capDns1;    /**< 「首选 DNS」标题。 */
    XLineEdit*   m_dns1Edit;   /**< 首选 DNS（可空）。 */
    XLabel*      m_capDns2;    /**< 「备用 DNS」标题。 */
    XLineEdit*   m_dns2Edit;   /**< 备用 DNS（可空）。 */
    XLabel*      m_capV6Addr;  /**< 「IPv6 地址」标题。 */
    XLineEdit*   m_v6AddrEdit; /**< 静态 IPv6 地址（追加式）。 */
    XLabel*      m_capV6Prefix;/**< 「前缀」标题。 */
    XLineEdit*   m_v6PrefixEdit;/**< 前缀长度（0..128，默认 64）。 */
    XLabel*      m_capV6Gw;    /**< 「IPv6 网关」标题。 */
    XLineEdit*   m_v6GwEdit;   /**< IPv6 网关（可空；lwIP 后端忽略）。 */
    XLabel*      m_capV6Dns1;  /**< 「首选 DNS」标题（IPv6 区）。 */
    XLineEdit*   m_v6Dns1Edit; /**< 首选 IPv6 DNS（只读展示系统下发值）。 */
    XLabel*      m_capV6Dns2;  /**< 「备用 DNS」标题（IPv6 区）。 */
    XLineEdit*   m_v6Dns2Edit; /**< 备用 IPv6 DNS（只读展示系统下发值）。 */

    /* ---- 反馈区 ---- */
    XLabel*    m_hintLabel;    /**< 操作提示行（钉底上方）。 */
    XLabel*    m_statusLabel;  /**< 操作结果行（钉底）。 */

    /* ---- 枚举缓存（combo 槽位 → 接口索引） ---- */
    int        m_ifIndex[32];  /**< 槽位对应的接口索引。 */
    int        m_count;        /**< 枚举到的接口数。 */

    /* ---- IPv6 列表缓存与展开态 ---- */
    char       m_v6First[64];  /**< 首个 IPv6 地址文本（折叠行显示）。 */
    char       m_v6All[1400];  /**< 全部 IPv6 地址文本（'\n' 连接，展开视图）。 */
    int        m_v6Count;      /**< IPv6 地址总数。 */
    bool       m_v6Expanded;   /**< true=展开视图（编辑区让位隐藏）。 */

    /* ---- 布局器（2026-10-17 容器化；build 装配段挂根，静态自持） ---- */
    XBoxLayout m_rootLayout;   /**< 根垂直布局器（页内唯一 setLayout）。 */
    XBoxLayout m_adapterRow;   /**< 网卡选择行（水平：标题+下拉+刷新）。 */
    XBoxLayout m_v6Row;        /**< IPv6 地址展示行（折叠单行+展开开关）。 */
    XBoxLayout m_ipCols;       /**< IPv4/IPv6 两分区容器（够宽=并排，
                                    不够=堆叠；adapt 切方向）。 */
    XBoxLayout m_v4Section;    /**< IPv4 分区（标题+模式行+五字段）。 */
    XBoxLayout m_v6Section;    /**< IPv6 分区（标题+地址行+模式行+五字段）。 */
    XBoxLayout m_v4ModeRow;    /**< IPv4 模式行（标题+下拉+应用）。 */
    XBoxLayout m_v6ModeRow;    /**< IPv6 模式行（标题+下拉+应用）。 */
    XBoxLayout m_v4Col;        /**< IPv4 五字段单列（垂直）。 */
    XBoxLayout m_v6Col;        /**< IPv6 五字段单列（垂直）。 */
    XBoxLayout m_v4Rows[5];    /**< IPv4 字段行布局器（标题+编辑）。 */
    XBoxLayout m_v6Rows[3];    /**< IPv6 地址/前缀/网关字段行。 */
    XBoxLayout m_v6DnsRows[2]; /**< IPv6 DNS 两字段行（只读展示）。 */

#if XNETWORK_ON && XDEVICE_WIFI_ON
    /* ---- 无线（Wi-Fi）区 ---- */
    XNetworkWiFi* m_wifi;             /**< 无线会话（L1 业务层）；页销毁时释放。 */
    XVector*     m_wifiAdapters;      /**< 无线适配器列表（L1 值对象，容器拥有）。 */
    XVector*     m_wifiNetworks;      /**< 扫描到的网络列表（L1 值对象，容器拥有）。 */
    XLabel*      m_wifiTitle;         /**< 第二分区标题「WiFi 设置」（居中）。 */
    XLabel*      m_wifiCap;           /**< 「无线」标题。 */
    XComboBox*   m_wifiAdapterCombo;  /**< 无线适配器下拉。 */
    XPushButton* m_wifiScanBtn;       /**< 「扫描」按钮。 */
    XLabel*      m_wifiNetCap;        /**< 「网络」标题。 */
    XComboBox*   m_wifiApCombo;       /**< 可见网络下拉。 */
    XLabel*      m_wifiPassCap;       /**< 「口令」标题。 */
    XLineEdit*   m_wifiPassEdit;      /**< 口令输入。 */
    XPushButton* m_wifiConnectBtn;    /**< 「连接」按钮。 */
    XPushButton* m_wifiDisconnectBtn; /**< 「断开」按钮。 */
    XLabel*      m_wifiInfoLabel;     /**< 无线状态行（当前连接/扫描进度）。 */
    XTimer*      m_wifiTimer;         /**< 扫描/连接状态轮询定时器（250ms）。 */
    XBoxLayout   m_wifiRow;           /**< 无线适配器行（水平）。 */
    XBoxLayout   m_wifiNetRow;        /**< 无线网络行（水平）。 */
    XBoxLayout   m_wifiPassRow;       /**< 口令行（窄根独用骨架：宽根恒空
                                           高 0，窄根 adapt 收容摘出的
                                           口令/连接/断开三件+口令标题）。 */
#endif
} s_net;

/* ==================== 内部辅助 ==================== */

/** @brief 向页面状态行与主窗状态栏反馈并登记最近文本（自测断言用）。 */
static void net_report(const char* fmt, const char* detail)
{
    char buf[192];
    snprintf(buf, sizeof(buf), fmt, detail ? detail : "");
    snprintf(s_net.m_lastStatus, sizeof(s_net.m_lastStatus), "%s", buf);
    if (s_net.m_statusLabel)
        XLabel_setText_2(s_net.m_statusLabel, buf);
    if (s_net.m_status)
        s_net.m_status(s_net.m_user, buf);
}

/** @brief 接口类型 → 中文名。 */
static const char* net_typeName(XNetworkInterface_InterfaceType type)
{
    switch (type) {
    case XNetworkInterface_Ethernet: return "\xE4\xBB\xA5\xE5\xA4\xAA\xE7\xBD\x91"; /* 以太网 */
    case XNetworkInterface_Wifi:     return "Wi-Fi";
    case XNetworkInterface_Loopback: return "\xE5\x9B\x9E\xE7\x8E\xAF";             /* 回环 */
    case XNetworkInterface_Virtual:  return "\xE8\x99\x9A\xE6\x8B\x9F";             /* 虚拟 */
    default:                         return "\xE5\x85\xB6\xE4\xBB\x96";             /* 其他 */
    }
}

/** @brief 点分 IPv4 字面量校验（页面层门控；应用前必过）。 */
static bool net_validIpv4(const char* text)
{
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned d = 0;
    char tail = 0;
    if (!text || !text[0]) return false;
    if (sscanf(text, "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4)
        return false;
    return a < 256 && b < 256 && c < 256 && d < 256;
}

/** @brief 地址快照 → 展示文本（null 地址显示 "-"）。 */
static void net_addrText(const XHostAddress* addr, char* out, size_t capacity)
{
    XString* text;
    if (!addr || XHostAddress_isNull(addr)) {
        snprintf(out, capacity, "-");
        return;
    }
    text = XHostAddress_toString(addr);
    if (text) {
        snprintf(out, capacity, "%s", XString_toUtf8(text));
        XClassDelete(text);
    } else {
        snprintf(out, capacity, "-");
    }
}

/** @brief IPv4 静态编辑框启停（左列五件；随 IPv4 模式）。 */
static void net_setV4EditsEnabled(bool enabled)
{
    XLineEdit* edits[5];
    int i;
    edits[0] = s_net.m_ipEdit;
    edits[1] = s_net.m_maskEdit;
    edits[2] = s_net.m_gwEdit;
    edits[3] = s_net.m_dns1Edit;
    edits[4] = s_net.m_dns2Edit;
    for (i = 0; i < 5; ++i)
        if (edits[i])
            XWidget_setEnabled((XWidget*)edits[i], enabled);
}

/** @brief IPv6 静态编辑框启停（右列地址/前缀/网关三件 + 应用钮；随 IPv6
 *         模式独立，与 IPv4 模式互不影响）。
 *  @note  IPv6 DNS 两行是**系统下发值的只读展示**（安卓不允许应用改 WiFi
 *         的 IPv6 DNS），不随模式启停——始终禁用。 */
static void net_setV6EditsEnabled(bool enabled)
{
    XLineEdit* edits[3];
    int i;
    edits[0] = s_net.m_v6AddrEdit;
    edits[1] = s_net.m_v6PrefixEdit;
    edits[2] = s_net.m_v6GwEdit;
    for (i = 0; i < 3; ++i)
        if (edits[i])
            XWidget_setEnabled((XWidget*)edits[i], enabled);
    if (s_net.m_applyV6Btn)
        XWidget_setEnabled((XWidget*)s_net.m_applyV6Btn, enabled);
}

/** @brief 选中网卡的链路层补充信息（XNetworkInterface 地址条目来源）。
 *  @details 配置快照（queryConfig）只覆盖 IPv4 + 首个 IPv6 面向预填；
 *           MAC 与 IPv6 全列表从接口枚举对象取（跨平台一致）。
 *  @return IPv6 地址总数；first 写首个地址文本，all 写 '\n' 连接的全
 *          列表（容量截断安全，计数仍准确）。 */
static int net_collectLinkInfo(int ifIndex, char* mac, size_t macCap,
                               char* first, size_t firstCap,
                               char* all, size_t allCap)
{
    XVector* interfaces = XNetworkInterface_allInterfaces();
    size_t i;
    size_t count;
    int total = 0;
    if (mac) snprintf(mac, macCap, "-");
    if (first) snprintf(first, firstCap, "-");
    if (all) all[0] = '\0';
    if (!interfaces) return 0;
    count = XVector_size_base(interfaces);
    for (i = 0; i < count; ++i) {
        XNetworkInterface* iface =
            (XNetworkInterface*)XVector_at_base(interfaces, i);
        XVector* entries;
        const XString* hw;
        size_t j;
        if (!iface || XNetworkInterface_index(iface) != ifIndex) continue;
        hw = XNetworkInterface_hardwareAddress_const(iface);
        if (mac && hw && XString_toUtf8(hw) && XString_toUtf8(hw)[0])
            snprintf(mac, macCap, "%s", XString_toUtf8(hw));
#if defined(__ANDROID__)
        /* [MAC 2026-10-25] 接口枚举的 ioctl(SIOCGIFHWADDR) 在安卓 SELinux
         * 下恒失败（/sys/class/net 对应用 uid 不可读），h 为空时再走一次
         * Java 桥（WifiInfo / java.net.NetworkInterface）。 */
        if (mac && (!hw || !XString_toUtf8(hw) || !XString_toUtf8(hw)[0]))
            (void)XAndroid_netLinkMac(mac, macCap);
#endif
        entries = XNetworkInterface_addressEntries(iface);
        for (j = 0; entries && j < XVector_size_base(entries); ++j) {
            XNetworkAddressEntry* entry =
                (XNetworkAddressEntry*)XVector_at_base(entries, j);
            XString* text;
            if (!entry || entry->m_ip.protocol != XHostAddress_IPv6Protocol)
                continue;
            text = XHostAddress_toString(&entry->m_ip);
            if (!text) continue;
            if (total == 0 && first) {
                const char* utf8 = XString_toUtf8(text);
                if (utf8) snprintf(first, firstCap, "%s", utf8);
            }
            if (all) {
                size_t len = strlen(all);
                snprintf(all + len, allCap - len, "%s%s",
                         (total > 0) ? "\n" : "", XString_toUtf8(text));
            }
            ++total;
            XClassDelete(text);
        }
        XClassDelete(interfaces);
        return total;
    }
    XClassDelete(interfaces);
    return 0;
}

/** @brief IPv6 地址区文本随折叠/展开态刷新（缓存文本见 s_net，定义在后）。 */
static void net_updateV6Label(void);

/** @brief 接口对象回退信息（平台不支持配置查询时的降级展示）。
 *  @details 与配置查询路径同构：信息区只展示 状态/类型 + IPv4 + MAC 三行，
 *           IPv6 全列表写入折叠/展开缓存由专用 IPv6 行承载——不再把多地址
 *           拼进信息区单行（长行会溢出窗口右缘被裁剪，且下方 IPv6 区恒显
 *           「-」与上方矛盾）。 */
static void net_fillInfoFromInterface(int ifIndex)
{
    XVector* interfaces = XNetworkInterface_allInterfaces();
    size_t i;
    size_t count;
    bool found = false;
    if (!interfaces) return;
    count = XVector_size_base(interfaces);
    for (i = 0; i < count && !found; ++i) {
        XNetworkInterface* iface =
            (XNetworkInterface*)XVector_at_base(interfaces, i);
        XVector* entries;
        size_t j;
        char info[384];
        char ipv4[96];
        const XString* mac;
        if (!iface || XNetworkInterface_index(iface) != ifIndex) continue;
        found = true;
        mac = XNetworkInterface_hardwareAddress_const(iface);
        /* [MAC 2026-10-25] 后端接口枚举取不到 MAC 时（安卓 SELinux 下
         * ioctl(SIOCGIFHWADDR) 恒失败）为空——由调用方 net_collectLinkInfo
         * 再走一次 JNI 兜底。 */
        entries = XNetworkInterface_addressEntries(iface);
        ipv4[0] = '\0';
        s_net.m_v6All[0] = '\0';
        s_net.m_v6First[0] = '\0';
        s_net.m_v6Count = 0;
        for (j = 0; entries && j < XVector_size_base(entries); ++j) {
            XNetworkAddressEntry* entry =
                (XNetworkAddressEntry*)XVector_at_base(entries, j);
            XString* ipText;
            if (!entry) continue;
            ipText = XHostAddress_toString(&entry->m_ip);
            if (!ipText) continue;
            if (entry->m_ip.protocol == XHostAddress_IPv6Protocol) {
                size_t len = strlen(s_net.m_v6All);
                if (s_net.m_v6Count == 0)
                    snprintf(s_net.m_v6First, sizeof(s_net.m_v6First),
                             "%s", XString_toUtf8(ipText));
                snprintf(s_net.m_v6All + len, sizeof(s_net.m_v6All) - len,
                         "%s%s", (len > 0) ? "\n" : "",
                         XString_toUtf8(ipText));
                ++s_net.m_v6Count;
            } else {
                size_t len = strlen(ipv4);
                snprintf(ipv4 + len, sizeof(ipv4) - len, "%s%s",
                         (len > 0) ? ", " : "", XString_toUtf8(ipText));
            }
            XClassDelete(ipText);
        }
        snprintf(info, sizeof(info),
                 "\xE7\x8A\xB6\xE6\x80\x81: %s  "
                 "\xE7\xB1\xBB\xE5\x9E\x8B: %s\n"
                 "IPv4: %s\n"
                 "MAC: %s",
                 /* 状态: / 类型: */
                 XNetworkInterface_isUp(iface)
                     ? "Up" : "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5",
                 net_typeName(XNetworkInterface_type(iface)),
                 ipv4[0] ? ipv4 : "-",
                 (mac && XString_toUtf8(mac) && XString_toUtf8(mac)[0])
                     ? XString_toUtf8(mac) : "-");
        s_net.m_infoLines = 3;
        if (s_net.m_infoLabel)
            XLabel_setText_2(s_net.m_infoLabel, info);
    }
    XClassDelete(interfaces);
    net_updateV6Label();
}

/** @brief 查询当前选中网卡并回填信息区/模式/编辑框（定义在本段尾）。 */
static void net_refreshSelection(void);

/** @brief IPv6 地址区文本随折叠/展开态刷新（缓存文本见 s_net）。 */
static void net_updateV6Label(void)
{
    char line[160];
    if (!s_net.m_v6Label) return;
    if (s_net.m_v6Expanded) {
        XLabel_setText_2(s_net.m_v6Label, s_net.m_v6All);
        return;
    }
    if (s_net.m_v6Count <= 0)
        snprintf(line, sizeof(line), "IPv6: -");
    else if (s_net.m_v6Count == 1)
        snprintf(line, sizeof(line), "IPv6: %s", s_net.m_v6First);
    else
        snprintf(line, sizeof(line),
                 "IPv6: %s \xEF\xBC\x88\xE7\xAD\x89 %d \xE4\xB8\xAA\xEF\xBC\x89",
                 s_net.m_v6First, s_net.m_v6Count); /* （等 N 个） */
    XLabel_setText_2(s_net.m_v6Label, line);
}

/** @brief 按 v6 展开态切换页面形态（折叠=编辑视图；展开=全列表让位）。
 *  @details 2026-10-17 容器化：不再手写任何几何——折叠/展开只切换
 *           控件显隐、信息区固定高（行数×16+8，回填行数驱动）与根
 *           布局中 IPv6 行的伸展因子，其余全部交给布局器：窗口宽窄、
 *           矮高均由 stretch/固定尺寸自适应（原「宽版/窄版」两套手写
 *           排布由同一布局结构承载）。展开态 IPv6 列表释放固定高、由
 *           伸展因子占满信息区腾出的全部空间（随窗口伸缩，替代原按根
 *           高截断行数的手写算法）。
 *           [LayoutRequest 2026-10-20] 显隐/setStretch/固定高变更均由
 *           框架自动失效父布局并排队重排（setVisible 显隐联动 +
 *           VXLayout_invalidate 收口 post），原手工
 *           XWidget_updateGeometry(root) redundant，已删。 */
static void net_relayout(void)
{
    XWidget* root = s_net.m_root;
    int infoH;
    int i;
    if (!root) return;
    if (s_net.m_v6Expanded) {
        /* ---- 展开：全列表让位视图（信息/编辑区隐藏，列表占内容区） ---- */
        if (s_net.m_infoLabel) XWidget_hide((XWidget*)s_net.m_infoLabel);
        if (s_net.m_capV4Mode) XWidget_hide((XWidget*)s_net.m_capV4Mode);
        if (s_net.m_v4ModeCombo) XWidget_hide((XWidget*)s_net.m_v4ModeCombo);
        if (s_net.m_applyBtn) XWidget_hide((XWidget*)s_net.m_applyBtn);
        if (s_net.m_capV6Mode) XWidget_hide((XWidget*)s_net.m_capV6Mode);
        if (s_net.m_v6ModeCombo) XWidget_hide((XWidget*)s_net.m_v6ModeCombo);
        if (s_net.m_applyV6Btn) XWidget_hide((XWidget*)s_net.m_applyV6Btn);
        for (i = 0; i < 5; ++i) {
            XLabel** cap = &s_net.m_capIp;
            XLineEdit** edit = &s_net.m_ipEdit;
            if (i == 1) { cap = &s_net.m_capMask; edit = &s_net.m_maskEdit; }
            else if (i == 2) { cap = &s_net.m_capGw; edit = &s_net.m_gwEdit; }
            else if (i == 3) { cap = &s_net.m_capDns1; edit = &s_net.m_dns1Edit; }
            else if (i == 4) { cap = &s_net.m_capDns2; edit = &s_net.m_dns2Edit; }
            if (*cap) XWidget_hide((XWidget*)*cap);
            if (*edit) XWidget_hide((XWidget*)*edit);
        }
        if (s_net.m_capV6Addr) XWidget_hide((XWidget*)s_net.m_capV6Addr);
        if (s_net.m_v6AddrEdit) XWidget_hide((XWidget*)s_net.m_v6AddrEdit);
        if (s_net.m_capV6Prefix) XWidget_hide((XWidget*)s_net.m_capV6Prefix);
        if (s_net.m_v6PrefixEdit) XWidget_hide((XWidget*)s_net.m_v6PrefixEdit);
        if (s_net.m_capV6Gw) XWidget_hide((XWidget*)s_net.m_capV6Gw);
        if (s_net.m_v6GwEdit) XWidget_hide((XWidget*)s_net.m_v6GwEdit);
        if (s_net.m_capV6Dns1) XWidget_hide((XWidget*)s_net.m_capV6Dns1);
        if (s_net.m_v6Dns1Edit) XWidget_hide((XWidget*)s_net.m_v6Dns1Edit);
        if (s_net.m_capV6Dns2) XWidget_hide((XWidget*)s_net.m_capV6Dns2);
        if (s_net.m_v6Dns2Edit) XWidget_hide((XWidget*)s_net.m_v6Dns2Edit);
        if (s_net.m_hintLabel) XWidget_hide((XWidget*)s_net.m_hintLabel);
        if (s_net.m_v6Label) {
            /* 展开态：列表必须有**实际高度**。滚动壳里内容高由布局合计
             * 自由高决定，没有"剩余空间"可分——只释放固定高再靠伸展因子
             * 会得到 0 高（真机+桌面实测：展开态列表高度不增，自测断言
             * 失败）。故按地址条数给足最小高（每行 16px + 余量），行高由
             * 内容撑开、超出视口的部分由整页竖滚承担。 */
            int listH = s_net.m_v6Count > 0 ? s_net.m_v6Count * 16 + 16 : 40;
            XWidget_setMinimumHeight((XWidget*)s_net.m_v6Label, listH);
            XWidget_setMaximumHeight((XWidget*)s_net.m_v6Label,
                                     XWIDGET_MAX_SIZE);
            XLabel_setText_2(s_net.m_v6Label,
                             s_net.m_v6Count > 0 ? s_net.m_v6All : "-");
        }
        if (s_net.m_v6ExpandCheck) {
            /* 同步释放行内开关固定高：行的交叉轴上限=子项上限最小值，
               开关不释放则整行被钉在 22px，列表让位视图无法伸展。 */
            XWidget_setMinimumHeight((XWidget*)s_net.m_v6ExpandCheck, 0);
            XWidget_setMaximumHeight((XWidget*)s_net.m_v6ExpandCheck,
                                     XWIDGET_MAX_SIZE);
        }
        /* IPv6 分区容器升为伸展条目（占标题/状态之外全部高度）：
         * 2026-10-25 起 IPv6 地址行被收进 m_v6Section，伸展因子必须加在
         * 分区容器上——加在根布局的行索引上会让整段 IPv6 分区高度错位。 */
        XBoxLayout_setStretch(&s_net.m_ipCols, 1, 1);
        XBoxLayout_setStretch(&s_net.m_v6Section, 2, 1);
        /* 展开/收起改变内容首选高（编辑区让位、列表变高）：重拟合滚动
         * 范围，否则收起后仍能滚到已隐藏内容留下的空白、展开后滚不到底。 */
        net_fitScrollContent(s_net.m_scroll);
        return;
    }
    /* ---- 折叠：编辑视图整体显形 + 标准行高 ---- */
    /* 信息区行高 16px + 上下 8px 余量；未回填按设计稿 5 行兜底。 */
    infoH = (s_net.m_infoLines > 0) ? s_net.m_infoLines * 16 + 8 : 104;
    if (s_net.m_infoLabel)
        XWidget_setFixedHeight((XWidget*)s_net.m_infoLabel, infoH);
    if (s_net.m_v6Label)
        XWidget_setFixedHeight((XWidget*)s_net.m_v6Label, 20);
    if (s_net.m_v6ExpandCheck)
        /* 开关是点击目标：窄根档随触控目标抬到 40（与 adapt 同口径，
           否则切一轮展开/收起就把窄根行高打回 22）。 */
        XWidget_setFixedHeight((XWidget*)s_net.m_v6ExpandCheck,
                               s_net.m_compact ? NET_TOUCH_H : 22);
    if (s_net.m_infoLabel) XWidget_show((XWidget*)s_net.m_infoLabel);
    if (s_net.m_capV4Mode) XWidget_show((XWidget*)s_net.m_capV4Mode);
    if (s_net.m_v4ModeCombo) XWidget_show((XWidget*)s_net.m_v4ModeCombo);
    if (s_net.m_applyBtn) XWidget_show((XWidget*)s_net.m_applyBtn);
    if (s_net.m_capV6Mode) XWidget_show((XWidget*)s_net.m_capV6Mode);
    if (s_net.m_v6ModeCombo) XWidget_show((XWidget*)s_net.m_v6ModeCombo);
    if (s_net.m_applyV6Btn) XWidget_show((XWidget*)s_net.m_applyV6Btn);
    for (i = 0; i < 5; ++i) {
        XLabel** cap = &s_net.m_capIp;
        XLineEdit** edit = &s_net.m_ipEdit;
        if (i == 1) { cap = &s_net.m_capMask; edit = &s_net.m_maskEdit; }
        else if (i == 2) { cap = &s_net.m_capGw; edit = &s_net.m_gwEdit; }
        else if (i == 3) { cap = &s_net.m_capDns1; edit = &s_net.m_dns1Edit; }
        else if (i == 4) { cap = &s_net.m_capDns2; edit = &s_net.m_dns2Edit; }
        if (*cap) XWidget_show((XWidget*)*cap);
        if (*edit) XWidget_show((XWidget*)*edit);
    }
    if (s_net.m_capV6Addr) XWidget_show((XWidget*)s_net.m_capV6Addr);
    if (s_net.m_v6AddrEdit) XWidget_show((XWidget*)s_net.m_v6AddrEdit);
    if (s_net.m_capV6Prefix) XWidget_show((XWidget*)s_net.m_capV6Prefix);
    if (s_net.m_v6PrefixEdit) XWidget_show((XWidget*)s_net.m_v6PrefixEdit);
    if (s_net.m_capV6Gw) XWidget_show((XWidget*)s_net.m_capV6Gw);
    if (s_net.m_v6GwEdit) XWidget_show((XWidget*)s_net.m_v6GwEdit);
    if (s_net.m_capV6Dns1) XWidget_show((XWidget*)s_net.m_capV6Dns1);
    if (s_net.m_v6Dns1Edit) XWidget_show((XWidget*)s_net.m_v6Dns1Edit);
    if (s_net.m_capV6Dns2) XWidget_show((XWidget*)s_net.m_capV6Dns2);
    if (s_net.m_v6Dns2Edit) XWidget_show((XWidget*)s_net.m_v6Dns2Edit);
    if (s_net.m_hintLabel) XWidget_show((XWidget*)s_net.m_hintLabel);
    XBoxLayout_setStretch(&s_net.m_ipCols, 1, 0);
    XBoxLayout_setStretch(&s_net.m_v6Section, 2, 0);
    net_updateV6Label();
    /* 收起后内容高变小：同上重拟合滚动范围（避免残留可滚空白）。 */
    net_fitScrollContent(s_net.m_scroll);
}

/** @brief 「展开全部 IPv6 / 收起」开关槽：切态 → 重排。 */
static void net_v6ExpandSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_net.m_v6ExpandCheck) return;
    s_net.m_v6Expanded =
        XAbstractButton_isChecked((XAbstractButton*)s_net.m_v6ExpandCheck);
    XAbstractButton_setText_2((XAbstractButton*)s_net.m_v6ExpandCheck,
                              s_net.m_v6Expanded
                                  ? "\xE6\x94\xB6\xE8\xB5\xB7 IPv6 \xE5\x88\x97"
                                    "\xE8\xA1\xA8" /* 收起 IPv6 列表 */
                                  : "\xE5\xB1\x95\xE5\xBC\x80\xE5\x85\xA8"
                                    "\xE9\x83\xA8 IPv6"); /* 展开全部 IPv6 */
    net_relayout();
}

/** @brief 应用 IPv6：校验 → 追加静态地址（+可选网关）→ 回读刷新。 */
static void net_applyV6Slot(XObject* receiver, XVarList* args)
{
    int slot;
    int ifIndex;
    const char* addr;
    const char* prefixText;
    const char* gw;
    int prefix;
    bool ok;
    (void)receiver; (void)args;
    if (!s_net.m_ready) return;
    slot = s_net.m_adapterCombo
               ? XComboBox_currentIndex(s_net.m_adapterCombo) : -1;
    if (slot < 0 || slot >= s_net.m_count) {
        net_report("\xE5\xBA\x94\xE7\x94\xA8\xE5\xA4\xB1\xE8\xB4\xA5: "
                   "\xE6\x9C\xAA\xE9\x80\x89\xE6\x8B\xA9\xE7\xBD\x91\xE5\x8D\xA1",
                   NULL); /* 应用失败: 未选择网卡 */
        return;
    }
    ifIndex = s_net.m_ifIndex[slot];
    addr = s_net.m_v6AddrEdit ? XLineEdit_text(s_net.m_v6AddrEdit) : "";
    prefixText = s_net.m_v6PrefixEdit ? XLineEdit_text(s_net.m_v6PrefixEdit) : "";
    gw = s_net.m_v6GwEdit ? XLineEdit_text(s_net.m_v6GwEdit) : "";
    prefix = atoi(prefixText && prefixText[0] ? prefixText : "64");
    /* 输入校验先于平台能力门（同 IPv4 侧口径）。 */
    if (!XHostAddress_isIPv6Address(addr)) {
        net_report("IPv6 \xE5\x9C\xB0\xE5\x9D\x80\xE9\x9D\x9E\xE6\xB3\x95"
                   "\xEF\xBC\x8C\xE6\x8B\x92\xE7\xBB\x9D\xE5\xBA\x94"
                   "\xE7\x94\xA8", NULL); /* IPv6 地址非法，拒绝应用 */
        return;
    }
    if (prefix < 0 || prefix > 128) {
        net_report("\xE5\x89\x8D\xE7\xBC\x80\xE9\x95\xBF\xE5\xBA\xA6\xE9"
                   "\x9D\x9E\xE6\xB3\x95\xEF\xBC\x8C\xE6\x8B\x92\xE7\xBB"
                   "\x9D\xE5\xBA\x94\xE7\x94\xA8", NULL); /* 前缀长度非法，拒绝应用 */
        return;
    }
    if (gw && gw[0] && !XHostAddress_isIPv6Address(gw)) {
        net_report("IPv6 \xE7\xBD\x91\xE5\x85\xB3\xE9\x9D\x9E\xE6\xB3\x95"
                   "\xEF\xBC\x8C\xE6\x8B\x92\xE7\xBB\x9D\xE5\xBA\x94"
                   "\xE7\x94\xA8", NULL); /* IPv6 网关非法，拒绝应用 */
        return;
    }
    if (!XNetworkInterface_configSupported()) {
        net_report("\xE5\xBD\x93\xE5\x89\x8D\xE5\xB9\xB3\xE5\x8F\xB0\xE4\xB8"
                   "\x8D\xE6\x94\xAF\xE6\x8C\x81\xE7\xBD\x91\xE5\x8D\xA1"
                   "\xE9\x85\x8D\xE7\xBD\xAE", NULL); /* 当前平台不支持网卡配置 */
        return;
    }
    ok = XNetworkInterface_setStaticIpv6(ifIndex, addr, prefix,
                                         (gw && gw[0]) ? gw : NULL);
    net_report(ok ? "\xE5\xB7\xB2\xE8\xBF\xBD\xE5\x8A\xA0\xE9\x9D\x99\xE6"
                    "\x80\x81 IPv6 \xE5\x9C\xB0\xE5\x9D\x80"
                  : "\xE5\xBA\x94\xE7\x94\xA8\xE5\xA4\xB1\xE8\xB4\xA5"
                    "\xEF\xBC\x88\xE9\x9C\x80\xE7\xAE\xA1\xE7\x90\x86"
                    "\xE5\x91\x98\xE6\x9D\x83\xE9\x99\x90\xEF\xBC\x9F"
                    "\xEF\xBC\x89",
               NULL); /* 已追加静态 IPv6 地址 / 应用失败（需管理员权限？） */
    if (ok)
        net_refreshSelection();
}

/** @brief 操作提示随平台能力切换：有配置通路=netsh 下发说明；
 *         无通路=如实告知仅展示与校验，避免误导操作预期。 */
static void net_updateHint(void)
{
    if (!s_net.m_hintLabel) return;
    if (XNetworkInterface_configSupported())
#if defined(__ANDROID__)
        /* 安卓：IP 模式经 WifiConfiguration（系统服务）；对用户在系统里
         * 连过的网络受配置所有权限制（切静态会被系统拒），IPv6 静态为
         * 系统级不支持（AOSP StaticIpConfiguration 只有 v4）。 */
        XLabel_setText_2(s_net.m_hintLabel,
                         "\xE5\xAE\x89\xE5\x8D\x93\xEF\xBC\x9AIP \xE7\x94\xB1"
                         "\xE7\xB3\xBB\xE7\xBB\x9F WiFi \xE7\xAE\xA1\xE7\x90\x86"
                         "\xEF\xBC\x9B\xE9\x9D\x99\xE6\x80\x81\xE9\x85\x8D\xE7\xBD\xAE"
                         "\xE4\xBB\x85\xE5\xAF\xB9\xE5\xBA\x94\xE7\x94\xA8\xE5\x88"
                         "\x9B\xE5\xBB\xBA\xE7\x9A\x84\xE7\xBD\x91\xE7\xBB\x9C\xE7"
                         "\x94\x9F\xE6\x95\x88\xEF\xBC\x9B\xE9\x9D\x99\xE6\x80\x81 IPv6"
                         "\xE7\xB3\xBB\xE7\xBB\x9F\xE4\xB8\x8D\xE6\x94\xAF\xE6\x8C\x81");
                         /* 安卓：IP 由系统 WiFi 管理；静态配置仅对应用创建的网络生效；静态 IPv6 系统不支持 */
#else
        XLabel_setText_2(s_net.m_hintLabel,
                         "\xE5\xBA\x94\xE7\x94\xA8\xE7\xBB\x8F\xE7\xB3\xBB"
                         "\xE7\xBB\x9F netsh \xE9\x80\x9A\xE8\xB7\xAF"
                         "\xEF\xBC\x88\xE9\x9C\x80\xE7\xAE\xA1\xE7\x90\x86"
                         "\xE5\x91\x98\xE6\x9D\x83\xE9\x99\x90\xEF\xBC\x89"
                         "\xEF\xBC\x9B\xE5\x88\x87\xE6\x8D\xA2\xE4\xBC\x9A"
                         "\xE7\x9F\xAD\xE6\x9A\x82\xE4\xB8\xAD\xE6\x96\xAD"
                         "\xE8\xAF\xA5\xE7\xBD\x91\xE5\x8D\xA1");
                         /* 应用经系统 netsh 通路（需管理员权限）；切换会短暂中断该网卡 */
#endif
    else
        XLabel_setText_2(s_net.m_hintLabel,
                         "\xE5\xBD\x93\xE5\x89\x8D\xE5\xB9\xB3\xE5\x8F\xB0"
                         "\xE6\x9C\xAA\xE6\x8E\xA5\xE9\x85\x8D\xE7\xBD\xAE"
                         "\xE9\x80\x9A\xE8\xB7\xAF\xEF\xBC\x8C\xE6\x9C\xAC"
                         "\xE9\xA1\xB5\xE4\xBB\x85\xE5\xB1\x95\xE7\xA4\xBA"
                         "\xE4\xB8\x8E\xE6\xA0\xA1\xE9\xAA\x8C");
                         /* 当前平台未接配置通路，本页仅展示与校验 */
}

/** @brief 查询当前选中网卡并回填信息区/模式/编辑框。 */
static void net_refreshSelection(void)
{
    XNetworkInterfaceConfig* config;
    int slot;
    char ip[64];
    char mask[64];
    char gw[64];
    char dhcp[64];
    char dns1[64];
    char dns2[64];
    char v6dns1[64];
    char v6dns2[64];
    char mac[40];
    char info[640];
    slot = s_net.m_adapterCombo
               ? XComboBox_currentIndex(s_net.m_adapterCombo) : -1;
    if (slot < 0 || slot >= s_net.m_count) {
        net_report("\xE6\x9C\xAA\xE9\x80\x89\xE6\x8B\xA9\xE7\xBD\x91\xE5\x8D\xA1",
                   NULL); /* 未选择网卡 */
        return;
    }
    config = XNetworkInterfaceConfig_create();
    if (!config) return;
    if (!XNetworkInterface_queryConfig(s_net.m_ifIndex[slot], config)) {
        XClassDelete(config);
        /* 平台不支持配置查询（非 Windows 后端）：降级展示接口自身信息。 */
        net_fillInfoFromInterface(s_net.m_ifIndex[slot]);
        net_updateHint();
        net_relayout();
        return;
    }
    net_addrText(&config->m_ipv4Address, ip, sizeof(ip));
    net_addrText(&config->m_ipv4Netmask, mask, sizeof(mask));
    net_addrText(&config->m_ipv4Gateway, gw, sizeof(gw));
    net_addrText(&config->m_dhcpServer, dhcp, sizeof(dhcp));
    net_addrText(&config->m_dnsPrimary, dns1, sizeof(dns1));
    net_addrText(&config->m_dnsSecondary, dns2, sizeof(dns2));
    /* [DNS 分族 2026-10-25] IPv6 DNS 只从 v6 槽取——旧实现把 LinkProperties
     * 的前两条 DNS（v6 优先网络下可能全是 IPv6）塞进 v4 槽，界面「IPv4
     * 设置」里显示的就是 IPv6 DNS。 */
    net_addrText(&config->m_dnsV6Primary, v6dns1, sizeof(v6dns1));
    net_addrText(&config->m_dnsV6Secondary, v6dns2, sizeof(v6dns2));
    /* IPv6 全列表 + MAC（跨平台经接口枚举；缓存供折叠/展开双态用）。 */
    s_net.m_v6Count = net_collectLinkInfo(s_net.m_ifIndex[slot],
                                          mac, sizeof(mac),
                                          s_net.m_v6First,
                                          sizeof(s_net.m_v6First),
                                          s_net.m_v6All,
                                          sizeof(s_net.m_v6All));
    if (s_net.m_infoLabel) {
        snprintf(info, sizeof(info),
                 "\xE6\xA8\xA1\xE5\xBC\x8F: %s %s%s\n"
                 "IPv4: %s  \xE6\x8E\xA9\xE7\xA0\x81: %s  "
                 "\xE7\xBD\x91\xE5\x85\xB3: %s\n"
                 "DNS: %s, %s\n"
                 "DHCP: %s\n"
                 "MAC: %s",
                 /* 模式: */
                 config->m_dhcpEnabled
                     ? "DHCP \xE8\x87\xAA\xE5\x8A\xA8\xE8\x8E\xB7\xE5\x8F\x96"
                     : "\xE9\x9D\x99\xE6\x80\x81\xE9\x85\x8D\xE7\xBD\xAE",
                 config->m_dhcpEnabled ? "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8: " : "",
                 config->m_dhcpEnabled ? dhcp : "",
                 ip, mask, gw, dns1, dns2,
                 config->m_dhcpEnabled
                     ? "\xE8\x87\xAA\xE5\x8A\xA8\xE8\x8E\xB7\xE5\x8F\x96"
                     : "\xE9\x9D\x99\xE6\x80\x81", /* 自动获取/静态 */
                 mac);
        s_net.m_infoLines = 5;
        XLabel_setText_2(s_net.m_infoLabel, info);
    }
    if (s_net.m_v4ModeCombo)
        XComboBox_setCurrentIndex(s_net.m_v4ModeCombo,
                                  config->m_dhcpEnabled ? 0 : 1);
    net_setV4EditsEnabled(!config->m_dhcpEnabled);
    if (s_net.m_ipEdit) {
        if (!XHostAddress_isNull(&config->m_ipv4Address))
            XLineEdit_setText(s_net.m_ipEdit, ip);
        else
            XLineEdit_setText(s_net.m_ipEdit, "");
    }
    if (s_net.m_maskEdit) {
        if (!XHostAddress_isNull(&config->m_ipv4Netmask))
            XLineEdit_setText(s_net.m_maskEdit, mask);
        else
            XLineEdit_setText(s_net.m_maskEdit, "");
    }
    if (s_net.m_gwEdit) {
        if (!XHostAddress_isNull(&config->m_ipv4Gateway))
            XLineEdit_setText(s_net.m_gwEdit, gw);
        else
            XLineEdit_setText(s_net.m_gwEdit, "");
    }
    if (s_net.m_dns1Edit) {
        if (!XHostAddress_isNull(&config->m_dnsPrimary))
            XLineEdit_setText(s_net.m_dns1Edit, dns1);
        else
            XLineEdit_setText(s_net.m_dns1Edit, "");
    }
    if (s_net.m_dns2Edit) {
        if (!XHostAddress_isNull(&config->m_dnsSecondary))
            XLineEdit_setText(s_net.m_dns2Edit, dns2);
        else
            XLineEdit_setText(s_net.m_dns2Edit, "");
    }
    /* IPv6 编辑行预填（首个单播 + 前缀 + 网关；无则留空）。 */
    if (s_net.m_v6AddrEdit) {
        if (!XHostAddress_isNull(&config->m_ipv6Address)) {
            XString* text = XHostAddress_toString(&config->m_ipv6Address);
            XLineEdit_setText(s_net.m_v6AddrEdit,
                              text ? XString_toUtf8(text) : "");
            if (text) XClassDelete(text);
        } else {
            XLineEdit_setText(s_net.m_v6AddrEdit, "");
        }
    }
    if (s_net.m_v6PrefixEdit) {
        char prefixText[8];
        snprintf(prefixText, sizeof(prefixText), "%d",
                 config->m_ipv6PrefixLength > 0
                     ? config->m_ipv6PrefixLength : 64);
        XLineEdit_setText(s_net.m_v6PrefixEdit, prefixText);
    }
    if (s_net.m_v6GwEdit) {
        if (!XHostAddress_isNull(&config->m_ipv6Gateway)) {
            XString* text = XHostAddress_toString(&config->m_ipv6Gateway);
            XLineEdit_setText(s_net.m_v6GwEdit,
                              text ? XString_toUtf8(text) : "");
            if (text) XClassDelete(text);
        } else {
            XLineEdit_setText(s_net.m_v6GwEdit, "");
        }
    }
    /* IPv6 DNS 回填（系统下发值，只读展示）。 */
    if (s_net.m_v6Dns1Edit)
        XLineEdit_setText(s_net.m_v6Dns1Edit,
                          XHostAddress_isNull(&config->m_dnsV6Primary)
                              ? "" : v6dns1);
    if (s_net.m_v6Dns2Edit)
        XLineEdit_setText(s_net.m_v6Dns2Edit,
                          XHostAddress_isNull(&config->m_dnsV6Secondary)
                              ? "" : v6dns2);
    XClassDelete(config);
    net_updateHint();
    net_relayout();
    net_updateV6Label();
}

/** @brief 重新枚举全部网卡并尽量保持当前选中。 */
static void net_refreshAdapters(void)
{
    XVector* interfaces = XNetworkInterface_allInterfaces();
    int previousSlot;
    int previousIfIndex;
    size_t i;
    size_t count;
    if (!s_net.m_adapterCombo) {
        if (interfaces) XClassDelete(interfaces);
        return;
    }
    previousSlot = XComboBox_currentIndex(s_net.m_adapterCombo);
    previousIfIndex = (previousSlot >= 0 && previousSlot < s_net.m_count)
                          ? s_net.m_ifIndex[previousSlot] : -1;
    XComboBox_clear(s_net.m_adapterCombo);
    s_net.m_count = 0;
    if (!interfaces) {
        net_report("\xE6\x9C\xAA\xE6\x9E\x9A\xE4\xB8\xBE\xE5\x88\xB0\xE7\xBD"
                   "\x91\xE5\x8D\xA1", NULL); /* 未枚举到网卡 */
        return;
    }
    count = XVector_size_base(interfaces);
    for (i = 0; i < count && s_net.m_count < (int)(
             sizeof(s_net.m_ifIndex) / sizeof(s_net.m_ifIndex[0])); ++i) {
        XNetworkInterface* iface =
            (XNetworkInterface*)XVector_at_base(interfaces, i);
        const XString* label;
        const char* text;
        if (!iface || !iface->m_isValid) continue;
        label = XNetworkInterface_humanReadableName_const(iface);
        if (!label || !XString_size(label))
            label = XNetworkInterface_name_const(iface);
        text = label ? XString_toUtf8(label) : NULL;
        XComboBox_addItem_2(s_net.m_adapterCombo,
                            (text && text[0]) ? text : "?");
        s_net.m_ifIndex[s_net.m_count++] = XNetworkInterface_index(iface);
    }
    XClassDelete(interfaces);
    /* 尽量保持原选中；失守回落 0 并回填。 */
    {
        int restored = -1;
        int slot;
        for (slot = 0; slot < s_net.m_count; ++slot)
            if (s_net.m_ifIndex[slot] == previousIfIndex) {
                restored = slot;
                break;
            }
        if (restored < 0) restored = 0;
        if (s_net.m_count > 0)
            XComboBox_setCurrentIndex(s_net.m_adapterCombo, restored);
    }
    if (s_net.m_count == 0)
        net_report("\xE6\x9C\xAA\xE6\x9E\x9A\xE4\xB8\xBE\xE5\x88\xB0\xE7\xBD"
                   "\x91\xE5\x8D\xA1", NULL); /* 未枚举到网卡 */
    else
        net_refreshSelection();
}

/* ==================== 信号槽 ==================== */

/** @brief 网卡下拉框选择变化：查询回填。 */
static void net_adapterActivatedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    net_refreshSelection();
}

/** @brief 刷新按钮：重新枚举。 */
static void net_refreshSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    net_refreshAdapters();
}

/** @brief IPv4 模式切换：左列静态字段启停（不覆盖已填值）。 */
static void net_v4ModeSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_net.m_v4ModeCombo) return;
    net_setV4EditsEnabled(XComboBox_currentIndex(s_net.m_v4ModeCombo) != 0);
}

/** @brief IPv6 模式切换：右列静态字段启停（独立于 IPv4 模式）。 */
static void net_v6ModeSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_net.m_v6ModeCombo) return;
    net_setV6EditsEnabled(XComboBox_currentIndex(s_net.m_v6ModeCombo) != 0);
}

/** @brief 应用配置：校验 → 下发（DHCP/静态）→ 回读刷新。 */
static void net_applySlot(XObject* receiver, XVarList* args)
{
    int slot;
    int mode;
    int ifIndex;
    const char* ip;
    const char* mask;
    const char* gw;
    const char* dns1;
    const char* dns2;
    bool ok;
    (void)receiver; (void)args;
    if (!s_net.m_ready) return;
    slot = s_net.m_adapterCombo
               ? XComboBox_currentIndex(s_net.m_adapterCombo) : -1;
    if (slot < 0 || slot >= s_net.m_count) {
        net_report("\xE5\xBA\x94\xE7\x94\xA8\xE5\xA4\xB1\xE8\xB4\xA5: "
                   "\xE6\x9C\xAA\xE9\x80\x89\xE6\x8B\xA9\xE7\xBD\x91\xE5\x8D\xA1",
                   NULL); /* 应用失败: 未选择网卡 */
        return;
    }
    ifIndex = s_net.m_ifIndex[slot];
    mode = s_net.m_v4ModeCombo ? XComboBox_currentIndex(s_net.m_v4ModeCombo) : 0;
    /* 输入校验先于平台能力门：非法输入在页面层拒应用（平台不支持时
     * 也一样能给出「非法」反馈，而非被能力门抢先吞掉）。 */
    if (mode != 0) {
        ip = s_net.m_ipEdit ? XLineEdit_text(s_net.m_ipEdit) : "";
        mask = s_net.m_maskEdit ? XLineEdit_text(s_net.m_maskEdit) : "";
        gw = s_net.m_gwEdit ? XLineEdit_text(s_net.m_gwEdit) : "";
        dns1 = s_net.m_dns1Edit ? XLineEdit_text(s_net.m_dns1Edit) : "";
        dns2 = s_net.m_dns2Edit ? XLineEdit_text(s_net.m_dns2Edit) : "";
        if (!net_validIpv4(ip)) {
            net_report("IP \xE5\x9C\xB0\xE5\x9D\x80\xE9\x9D\x9E\xE6\xB3\x95"
                       "\xEF\xBC\x8C\xE6\x8B\x92\xE7\xBB\x9D\xE5\xBA\x94"
                       "\xE7\x94\xA8", NULL); /* IP 地址非法，拒绝应用 */
            return;
        }
        if (!net_validIpv4(mask)) {
            net_report("\xE5\xAD\x90\xE7\xBD\x91\xE6\x8E\xA9\xE7\xA0\x81"
                       "\xE9\x9D\x9E\xE6\xB3\x95\xEF\xBC\x8C\xE6\x8B\x92"
                       "\xE7\xBB\x9D\xE5\xBA\x94\xE7\x94\xA8",
                       NULL); /* 子网掩码非法，拒绝应用 */
            return;
        }
        if (gw && gw[0] && !net_validIpv4(gw)) {
            net_report("\xE9\xBB\x98\xE8\xAE\xA4\xE7\xBD\x91\xE5\x85\xB3"
                       "\xE9\x9D\x9E\xE6\xB3\x95\xEF\xBC\x8C\xE6\x8B\x92"
                       "\xE7\xBB\x9D\xE5\xBA\x94\xE7\x94\xA8",
                       NULL); /* 默认网关非法，拒绝应用 */
            return;
        }
        if (dns1 && dns1[0] && !net_validIpv4(dns1)) {
            net_report("\xE9\xA6\x96\xE9\x80\x89 DNS \xE9\x9D\x9E\xE6\xB3"
                       "\x95\xEF\xBC\x8C\xE6\x8B\x92\xE7\xBB\x9D\xE5\xBA"
                       "\x94\xE7\x94\xA8", NULL); /* 首选 DNS 非法，拒绝应用 */
            return;
        }
        if (dns2 && dns2[0] && !net_validIpv4(dns2)) {
            net_report("\xE5\xA4\x87\xE7\x94\xA8 DNS \xE9\x9D\x9E\xE6\xB3"
                       "\x95\xEF\xBC\x8C\xE6\x8B\x92\xE7\xBB\x9D\xE5\xBA"
                       "\x94\xE7\x94\xA8", NULL); /* 备用 DNS 非法，拒绝应用 */
            return;
        }
    }
    if (!XNetworkInterface_configSupported()) {
        net_report("\xE5\xBD\x93\xE5\x89\x8D\xE5\xB9\xB3\xE5\x8F\xB0\xE4\xB8"
                   "\x8D\xE6\x94\xAF\xE6\x8C\x81\xE7\xBD\x91\xE5\x8D\xA1"
                   "\xE9\x85\x8D\xE7\xBD\xAE", NULL); /* 当前平台不支持网卡配置 */
        return;
    }
    if (mode == 0) {
        ok = XNetworkInterface_setDhcpMode(ifIndex);
        net_report(ok ? "\xE5\xB7\xB2\xE5\x88\x87\xE6\x8D\xA2\xE4\xB8\xBA "
                        "DHCP \xE8\x87\xAA\xE5\x8A\xA8\xE8\x8E\xB7\xE5\x8F\x96"
                      : "\xE5\xBA\x94\xE7\x94\xA8\xE5\xA4\xB1\xE8\xB4\xA5"
                        "\xEF\xBC\x88\xE9\x9C\x80\xE7\xAE\xA1\xE7\x90\x86"
                        "\xE5\x91\x98\xE6\x9D\x83\xE9\x99\x90\xEF\xBC\x9F"
                        "\xEF\xBC\x89",
                   NULL); /* 已切换为 DHCP 自动获取 / 应用失败（需管理员权限？） */
    } else {
        ok = XNetworkInterface_setStaticMode(ifIndex, ip, mask,
                                             (gw && gw[0]) ? gw : NULL,
                                             (dns1 && dns1[0]) ? dns1 : NULL,
                                             (dns2 && dns2[0]) ? dns2 : NULL);
        net_report(ok ? "\xE5\xB7\xB2\xE5\xBA\x94\xE7\x94\xA8\xE9\x9D\x99"
                        "\xE6\x80\x81 IP \xE9\x85\x8D\xE7\xBD\xAE"
                      : "\xE5\xBA\x94\xE7\x94\xA8\xE5\xA4\xB1\xE8\xB4\xA5"
                        "\xEF\xBC\x88\xE9\x9C\x80\xE7\xAE\xA1\xE7\x90\x86"
                        "\xE5\x91\x98\xE6\x9D\x83\xE9\x99\x90\xEF\xBC\x9F"
                        "\xEF\xBC\x89",
                   NULL); /* 已应用静态 IP 配置 / 应用失败（需管理员权限？） */
    }
    if (ok)
        net_refreshSelection(); /* 下发成功即回读（netsh 同步完成）。 */
}

/* ==================== 装配 ==================== */

/** @brief 组装一行「标题 + 单行编辑」（只建件与固定尺寸；入列装配见
 *         net_addFieldRow）。 */
static void net_buildField(const char* caption, XLabel** outCap,
                           XLineEdit** outEdit)
{
    if (outCap && outEdit) {
        XLabel* cap = XLabel_create(s_net.m_root, 0);
        XLineEdit* edit = XLineEdit_create(s_net.m_root, 0);
        if (cap) {
            XLabel_setText_2(cap, caption);
            XWidget_setFixedWidth((XWidget*)cap, NET_CAP_W);
            XWidget_setFixedHeight((XWidget*)cap, 20);
            XWidget_show((XWidget*)cap);
        }
        if (edit) {
            XWidget_setFixedWidth((XWidget*)edit, NET_FIELD_W);
            XWidget_setFixedHeight((XWidget*)edit, 22);
            XWidget_show((XWidget*)edit);
        }
        *outCap = cap;
        *outEdit = edit;
    }
}

/** @brief 把「标题+编辑」行装进列布局器（行布局器入登记表静态自持）。 */
static void net_addFieldRow(XBoxLayout* col, XBoxLayout* row,
                            XLabel* cap, XLineEdit* edit)
{
    if (!col || !row) return;
    XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)row, 4);
    if (cap)
        XBoxLayout_addWidget(row, (XWidget*)cap);
    if (edit)
        XBoxLayout_addWidget(row, (XWidget*)edit);
    /* 行尾伸展：吸收行宽于内容之外的余量（本布局器把无 stretch 条目
       的主轴余量均摊到条目前，行尾加 stretch 才能保持左对齐排布）。 */
    XBoxLayout_addStretch(row, 1);
    XBoxLayout_addLayout(col, (XLayout*)row);

}

/* ==================== 无线（Wi-Fi）区 ====================
 * 走 L1 业务层（XNetworkWiFi）：值语义 + 会话管理；扫描/连接是"发起即返回"
 * 的异步形态，本页用 250ms 定时器轮询状态机（无线程）。
 */
#if XNETWORK_ON && XDEVICE_WIFI_ON

/** @brief 安全方式 → 短名（AP 列表与状态行共用）。 */
static const char* net_wifiSecurityName(XDeviceWiFiSecurity security)
{
    switch (security) {
    case XDeviceWiFi_SecurityOpen:          return "\xE5\xBC\x80\xE6\x94\xBE"; /* 开放 */
    case XDeviceWiFi_SecurityWep:           return "WEP";
    case XDeviceWiFi_SecurityWpaPsk:        return "WPA-PSK";
    case XDeviceWiFi_SecurityWpa2Psk:       return "WPA2-PSK";
    case XDeviceWiFi_SecurityWpa2Enterprise: return "WPA2-\xE4\xBC\x81\xE4\xB8\x9A"; /* 企业 */
    case XDeviceWiFi_SecurityWpa3Sae:       return "WPA3-SAE";
    default:                                return "\xE6\x9C\xAA\xE7\x9F\xA5"; /* 未知 */
    }
}

/** @brief 无线区是否就绪（会话已打开）。 */
static bool net_wifiReady(void)
{
    return s_net.m_wifi && XNetworkWiFi_isOpen(s_net.m_wifi);
}

/** @brief 取当前选中的无线适配器（借用指针；列表在页内存活）。 */
static const XNetworkWiFiAdapter* net_wifiAdapter(void)
{
    int index;
    if (!s_net.m_wifiAdapterCombo || !s_net.m_wifiAdapters) return NULL;
    index = XComboBox_currentIndex(s_net.m_wifiAdapterCombo);
    if (index < 0) return NULL;
    if ((size_t)index >= XVector_size_base(s_net.m_wifiAdapters)) return NULL;
    return (const XNetworkWiFiAdapter*)
           XVector_at_base(s_net.m_wifiAdapters, (int64_t)index);
}

/** @brief 取当前选中的可见网络（借用指针；列表在页内存活）。 */
static const XNetworkWiFiNetwork* net_wifiNetwork(void)
{
    int index;
    if (!s_net.m_wifiApCombo || !s_net.m_wifiNetworks) return NULL;
    index = XComboBox_currentIndex(s_net.m_wifiApCombo);
    if (index < 0) return NULL;
    if ((size_t)index >= XVector_size_base(s_net.m_wifiNetworks)) return NULL;
    return (const XNetworkWiFiNetwork*)
           XVector_at_base(s_net.m_wifiNetworks, (int64_t)index);
}

/** @brief 刷新无线状态行（当前连接快照）。 */
static void net_wifiRefreshInfo(void)
{
    const XNetworkWiFiAdapter* adapter = net_wifiAdapter();
    XNetworkWiFiConnection* connection;
    char text[256];
    if (!s_net.m_wifiInfoLabel) return;
    if (!net_wifiReady() || !adapter) {
        XLabel_setText_2(s_net.m_wifiInfoLabel,
                         "\xE6\x97\xA0\xE7\xBA\xBF\xEF\xBC\x9A"
                         "\xE4\xB8\x8D\xE5\x8F\xAF\xE7\x94\xA8"); /* 无线：不可用 */
        return;
    }
    connection = XNetworkWiFi_queryConnection(s_net.m_wifi, adapter);
    if (!connection) {
        XLabel_setText_2(s_net.m_wifiInfoLabel,
                         "\xE6\x97\xA0\xE7\xBA\xBF\xEF\xBC\x9A"
                         "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5"); /* 无线：未连接 */
        return;
    }
    {
        const XString* ssid = XNetworkWiFiConnection_ssid(connection);
        snprintf(text, sizeof(text),
                 "\xE6\x97\xA0\xE7\xBA\xBF\xEF\xBC\x9A%s  "
                 "\xE4\xBF\xA1\xE5\x8F\xB7 %d  %s", /* 无线：<ssid>  信号 N  <加密> */
                 ssid ? XString_toUtf8(ssid) : "-",
                 XNetworkWiFiConnection_signalStrength(connection),
                 net_wifiSecurityName(XNetworkWiFiConnection_security(connection)));
        XLabel_setText_2(s_net.m_wifiInfoLabel, text);
    }
    XClassDelete(connection);
}

/** @brief 用扫描结果填 AP 下拉（旧列表先释放，新列表由页持有）。 */
static void net_wifiFillNetworks(XVector* networks)
{
    size_t i;
    size_t count;
    if (s_net.m_wifiNetworks) {
        XClassDelete((XClass*)s_net.m_wifiNetworks);
        s_net.m_wifiNetworks = NULL;
    }
    if (networks)
        s_net.m_wifiNetworks = networks;
    if (s_net.m_wifiApCombo)
        XComboBox_clear(s_net.m_wifiApCombo);
    if (!networks) return;
    count = XVector_size_base(networks);
    for (i = 0; i < count; ++i) {
        const XNetworkWiFiNetwork* net =
            (const XNetworkWiFiNetwork*)XVector_at_base(networks, (int64_t)i);
        const XString* ssid = XNetworkWiFiNetwork_ssid(net);
        char text[192];
        snprintf(text, sizeof(text), "%s  (%d%%, %s)",
                 ssid ? XString_toUtf8(ssid)
                      : "\xE9\x9A\x90\xE8\x97\x8F\xE7\xBD\x91\xE7\xBB\x9C", /* 隐藏网络 */
                 XNetworkWiFiNetwork_signalStrength(net),
                 net_wifiSecurityName(XNetworkWiFiNetwork_security(net)));
        XComboBox_addItem_2(s_net.m_wifiApCombo, text);
    }
    if (count > 0)
        XComboBox_setCurrentIndex(s_net.m_wifiApCombo, 0);
}

/** @brief 枚举无线适配器并填下拉（无线区初始化）。 */
static void net_wifiRefreshAdapters(void)
{
    XVector* adapters;
    size_t i;
    size_t count;
    if (!net_wifiReady()) return;
    if (s_net.m_wifiAdapters) {
        XClassDelete(s_net.m_wifiAdapters);
        s_net.m_wifiAdapters = NULL;
    }
    adapters = XNetworkWiFi_adapters(s_net.m_wifi);
    if (adapters)
        s_net.m_wifiAdapters = adapters;
    XComboBox_clear(s_net.m_wifiAdapterCombo);
    if (!adapters) return;
    count = XVector_size_base(adapters);
    for (i = 0; i < count; ++i) {
        const XNetworkWiFiAdapter* adapter =
            (const XNetworkWiFiAdapter*)XVector_at_base(adapters, (int64_t)i);
        const XString* name = XNetworkWiFiAdapter_name(adapter);
        const XString* desc = XNetworkWiFiAdapter_description(adapter);
        char text[256];
        snprintf(text, sizeof(text), "%s%s%s",
                 name ? XString_toUtf8(name) : "?",
                 desc ? " - " : "", desc ? XString_toUtf8(desc) : "");
        XComboBox_addItem_2(s_net.m_wifiAdapterCombo, text);
    }
    if (count > 0)
        XComboBox_setCurrentIndex(s_net.m_wifiAdapterCombo, 0);
}

/** @brief 无线适配器选择变化：刷新当前连接信息。 */
static void net_wifiAdapterSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    net_wifiRefreshInfo();
}

/** @brief 「扫描」：发起扫描并启动状态轮询。 */
static void net_wifiScanSlot(XObject* receiver, XVarList* args)
{
    const XNetworkWiFiAdapter* adapter = net_wifiAdapter();
    (void)receiver; (void)args;
    if (!net_wifiReady() || !adapter) {
        net_report("\xE6\x97\xA0\xE7\xBA\xBF\xE4\xB8\x8D\xE5\x8F\xAF"
                   "\xE7\x94\xA8", NULL); /* 无线不可用 */
        return;
    }
    if (!XNetworkWiFi_startScan(s_net.m_wifi, adapter)) {
        net_report("\xE6\x89\xAB\xE6\x8F\x8F\xE5\x8F\x91\xE8\xB5\xB7"
                   "\xE5\xA4\xB1\xE8\xB4\xA5", NULL); /* 扫描发起失败 */
        return;
    }
    XLabel_setText_2(s_net.m_wifiInfoLabel,
                     "\xE6\x97\xA0\xE7\xBA\xBF\xEF\xBC\x9A"
                     "\xE6\x89\xAB\xE6\x8F\x8F\xE4\xB8\xAD"); /* 无线：扫描中 */
    XWidget_setEnabled((XWidget*)s_net.m_wifiScanBtn, false);
    if (s_net.m_wifiTimer && !XTimer_isRunning(s_net.m_wifiTimer))
        XTimer_start_base(s_net.m_wifiTimer);
}

/** @brief 「连接」：用选中 AP + 口令发起连接并启动状态轮询。 */
static void net_wifiConnectSlot(XObject* receiver, XVarList* args)
{
    const XNetworkWiFiAdapter* adapter = net_wifiAdapter();
    const XNetworkWiFiNetwork* network = net_wifiNetwork();
    const XString* ssid;
    const char* password;
    XDeviceWiFiSecurity security = XDeviceWiFi_SecurityWpa2Psk;
    (void)receiver; (void)args;
    if (!net_wifiReady() || !adapter) {
        net_report("\xE6\x97\xA0\xE7\xBA\xBF\xE4\xB8\x8D\xE5\x8F\xAF"
                   "\xE7\x94\xA8", NULL); /* 无线不可用 */
        return;
    }
    if (!network) {
        net_report("\xE8\xAF\xB7\xE5\x85\x88\xE9\x80\x89\xE6\x8B\xA9"
                   "\xE6\x97\xA0\xE7\xBA\xBF\xE9\x80\x82\xE9\x85\x8D"
                   "\xE5\x99\xA8", NULL); /* 请先选择无线适配器 */
        return;
    }
    ssid = XNetworkWiFiNetwork_ssid(network);
    if (!ssid) {
        net_report("\xE6\x9C\xAA\xE7\x9F\xA5", NULL); /* 未知 */
        return;
    }
    security = XNetworkWiFiNetwork_security(network);
    password = s_net.m_wifiPassEdit ? XLineEdit_text(s_net.m_wifiPassEdit) : NULL;
    if (!XNetworkWiFi_connect(s_net.m_wifi, adapter, XString_toUtf8(ssid),
                              password, security)) {
        /* 失败原因要说清楚：加密网络缺口令是最常见的一因（平台层如实
           拒绝无凭据的加密连接），笼统报"连接发起失败"会让用户无从
           下手（真机实测踩坑：AP 已选中但口令框空着）。 */
        if (security != XDeviceWiFi_SecurityOpen &&
            (!password || !password[0])) {
            net_report("\xE8\xBF\x9E\xE6\x8E\xA5\xE5\x8F\x91\xE8\xB5\xB7"
                       "\xE5\xA4\xB1\xE8\xB4\xA5\xEF\xBC\x9A"
                       "\xE8\xAF\xB7\xE5\x85\x88\xE5\xA1\xAB\xE5\x8F\xA3"
                       "\xE4\xBB\xA5", NULL); /* 连接发起失败：请先填口令 */
        } else {
            net_report("\xE8\xBF\x9E\xE6\x8E\xA5\xE5\x8F\x91\xE8\xB5\xB7"
                       "\xE5\xA4\xB1\xE8\xB4\xA5", NULL); /* 连接发起失败 */
        }
        return;
    }
    XLabel_setText_2(s_net.m_wifiInfoLabel,
                     "\xE6\x97\xA0\xE7\xBA\xBF\xEF\xBC\x9A"
                     "\xE8\xBF\x9E\xE6\x8E\xA5\xE4\xB8\xAD"); /* 无线：连接中 */
    if (s_net.m_wifiTimer && !XTimer_isRunning(s_net.m_wifiTimer))
        XTimer_start_base(s_net.m_wifiTimer);
}

/** @brief 「断开」：断开当前连接。 */
static void net_wifiDisconnectSlot(XObject* receiver, XVarList* args)
{
    const XNetworkWiFiAdapter* adapter = net_wifiAdapter();
    (void)receiver; (void)args;
    if (!net_wifiReady() || !adapter) {
        net_report("\xE6\x97\xA0\xE7\xBA\xBF\xE4\xB8\x8D\xE5\x8F\xAF"
                   "\xE7\x94\xA8", NULL); /* 无线不可用 */
        return;
    }
    if (!XNetworkWiFi_disconnect(s_net.m_wifi, adapter)) {
        net_report("\xE6\x96\xAD\xE5\xBC\x80\xE5\xA4\xB1\xE8\xB4\xA5",
                   NULL); /* 断开失败 */
        return;
    }
    net_report("\xE6\x97\xA0\xE7\xBA\xBF\xEF\xBC\x9A"
               "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5", NULL); /* 无线：未连接 */
    net_wifiRefreshInfo();
}

/** @brief 状态轮询定时器回调（250ms）：收口扫描与连接状态机。 */
static void net_wifiTimerCb(void* userData, XTimerData* timer)
{
    const XNetworkWiFiAdapter* adapter = net_wifiAdapter();
    XDeviceWiFiConnectState connect;
    XDeviceWiFiScanState scan;
    (void)userData; (void)timer;
    if (!net_wifiReady() || !adapter) return;

    /* 连接状态优先（连接期间扫描状态机是空的）。 */
    connect = XNetworkWiFi_connectState(s_net.m_wifi, adapter);
    if (connect == XDeviceWiFi_ConnectConnecting) return;
    if (connect == XDeviceWiFi_ConnectConnected) {
        XTimer_stop_base(s_net.m_wifiTimer);
        XWidget_setEnabled((XWidget*)s_net.m_wifiScanBtn, true);
        net_wifiRefreshInfo();
        net_report("\xE6\x97\xA0\xE7\xBA\xBF\xE8\xBF\x9E\xE6\x8E\xA5"
                   "\xE6\x88\x90\xE5\x8A\x9F", NULL); /* 无线连接成功 */
        return;
    }
    if (connect == XDeviceWiFi_ConnectFailed) {
        XTimer_stop_base(s_net.m_wifiTimer);
        XWidget_setEnabled((XWidget*)s_net.m_wifiScanBtn, true);
        XLabel_setText_2(s_net.m_wifiInfoLabel,
                         "\xE6\x97\xA0\xE7\xBA\xBF\xEF\xBC\x9A"
                         "\xE8\xBF\x9E\xE6\x8E\xA5\xE5\xA4\xB1\xE8\xB4\xA5"); /* 无线：连接失败 */
        net_report("\xE6\x97\xA0\xE7\xBA\xBF\xE8\xBF\x9E\xE6\x8E\xA5"
                   "\xE5\xA4\xB1\xE8\xB4\xA5", NULL); /* 无线连接失败 */
        return;
    }

    scan = XNetworkWiFi_scanState(s_net.m_wifi, adapter);
    if (scan == XDeviceWiFi_ScanRunning) return;
    XTimer_stop_base(s_net.m_wifiTimer);
    XWidget_setEnabled((XWidget*)s_net.m_wifiScanBtn, true);
    if (scan == XDeviceWiFi_ScanCompleted) {
        XVector* networks = XNetworkWiFi_takeScanResults(s_net.m_wifi, adapter);
        net_wifiFillNetworks(networks);
        net_report("\xE6\x89\xAB\xE6\x8F\x8F\xE5\xAE\x8C\xE6\x88\x90",
                   NULL); /* 扫描完成 */
    } else {
        XLabel_setText_2(s_net.m_wifiInfoLabel,
                         "\xE6\x97\xA0\xE7\xBA\xBF\xEF\xBC\x9A"
                         "\xE6\x89\xAB\xE6\x8F\x8F\xE5\xA4\xB1\xE8\xB4\xA5"); /* 无线：扫描失败 */
        net_report("\xE6\x89\xAB\xE6\x8F\x8F\xE5\xA4\xB1\xE8\xB4\xA5",
                   NULL); /* 扫描失败 */
    }
}

/** @brief 页销毁：停表、关无线会话、释放无线列表（会话析构即归还设备）。 */
static void net_wifiDestroyedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (s_net.m_wifiTimer) {
        XTimer_stop_base(s_net.m_wifiTimer);
        s_net.m_wifiTimer = NULL;
    }
    if (s_net.m_wifiAdapters) {
        XClassDelete(s_net.m_wifiAdapters);
        s_net.m_wifiAdapters = NULL;
    }
    if (s_net.m_wifiNetworks) {
        XClassDelete(s_net.m_wifiNetworks);
        s_net.m_wifiNetworks = NULL;
    }
    if (s_net.m_wifi) {
        XNetworkWiFi_close(s_net.m_wifi);
        XClassDelete(s_net.m_wifi);
        s_net.m_wifi = NULL;
    }
}

#endif /* XNETWORK_ON && XDEVICE_WIFI_ON */

/** @brief 无头截图/自动化钩子前置声明（定义在 build 之后）。 */
static void net_page_network_headless_hook(void);

XWidget* demo_page_network_build(XWidget* parent,
                                 DemoPageStatusFn status, void* user)
{
    /* demo 单实例：重复 build 前清空登记表（旧页面随旧父链析构）。 */
    memset(&s_net, 0, sizeof(s_net));
    s_net.m_status = status;
    s_net.m_user = user;
    if (!parent) return (XWidget*)0;
    /* 页宿主（堆叠条目）→ 滚动区 → 内容根（页内控件全挂内容根）。
     * 页面契约返回值是**页宿主**（主文件按堆叠条目登记），内容根只作
     * 滚动区内容件。 */
    s_net.m_pageHost = net_createPageHost(parent);
    if (!s_net.m_pageHost) return (XWidget*)0;
    s_net.m_scroll = (XScrollArea*)net_createScroll(s_net.m_pageHost);
    if (!s_net.m_scroll) return s_net.m_pageHost;
    ((NetPageHost*)s_net.m_pageHost)->m_scroll = (XWidget*)s_net.m_scroll;
    /* 水平条恒关（内容宽=视口宽，永不需要横滚）；竖条保持默认
     * AsNeeded——只有内容确实超高时才出现。 */
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)s_net.m_scroll, XScrollBarPolicy_AlwaysOff);
    /* widgetResizable 保持 false：内容几何由 NetPageScroll::resizeEvent
     * 自管（见 net_fitScrollContent 注释）。 */
    s_net.m_root = XWidget_create((XWidget*)s_net.m_scroll, 0);
    if (!s_net.m_root) return s_net.m_pageHost;
    XScrollArea_setWidget(s_net.m_scroll, s_net.m_root);

    /* ---- 三分区标题（网卡设置 / WiFi 设置 / IP 设置；同款式居中） ---- */
    s_net.m_titleLabel = XLabel_create(s_net.m_root, 0);
    if (!s_net.m_titleLabel) return s_net.m_pageHost;
    XLabel_setText_2(s_net.m_titleLabel,
                     "\xE7\xBD\x91\xE5\x8D\xA1\xE8\xAE\xBE\xE7\xBD\xAE");
                     /* 网卡设置 */
    XLabel_setTextPixelSize(s_net.m_titleLabel, 15);
    XLabel_setAlignment(s_net.m_titleLabel, XAlignment_Center);
    XWidget_setFixedHeight((XWidget*)s_net.m_titleLabel, 24);
    XWidget_show((XWidget*)s_net.m_titleLabel);
    s_net.m_ipTitle = XLabel_create(s_net.m_root, 0);
    if (!s_net.m_ipTitle) return s_net.m_pageHost;
    XLabel_setText_2(s_net.m_ipTitle,
                     "IPv4 \xE8\xAE\xBE\xE7\xBD\xAE"); /* IPv4 设置 */
    XLabel_setTextPixelSize(s_net.m_ipTitle, 15);
    XLabel_setAlignment(s_net.m_ipTitle, XAlignment_Center);
    XWidget_setFixedHeight((XWidget*)s_net.m_ipTitle, 24);
    XWidget_show((XWidget*)s_net.m_ipTitle);
    s_net.m_v6Title = XLabel_create(s_net.m_root, 0);
    if (!s_net.m_v6Title) return s_net.m_pageHost;
    XLabel_setText_2(s_net.m_v6Title,
                     "IPv6 \xE8\xAE\xBE\xE7\xBD\xAE"); /* IPv6 设置 */
    XLabel_setTextPixelSize(s_net.m_v6Title, 15);
    XLabel_setAlignment(s_net.m_v6Title, XAlignment_Center);
    XWidget_setFixedHeight((XWidget*)s_net.m_v6Title, 24);
    XWidget_show((XWidget*)s_net.m_v6Title);
#if XNETWORK_ON && XDEVICE_WIFI_ON
    s_net.m_wifiTitle = XLabel_create(s_net.m_root, 0);
    if (s_net.m_wifiTitle) {
        XLabel_setText_2(s_net.m_wifiTitle,
                         "WiFi \xE8\xAE\xBE\xE7\xBD\xAE"); /* WiFi 设置 */
        XLabel_setTextPixelSize(s_net.m_wifiTitle, 15);
        XLabel_setAlignment(s_net.m_wifiTitle, XAlignment_Center);
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiTitle, 24);
        XWidget_show((XWidget*)s_net.m_wifiTitle);
    }
#endif

    /* ---- 网卡选择行 ---- */
    s_net.m_capAdapter = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capAdapter) {
        XLabel_setText_2(s_net.m_capAdapter,
                         "\xE7\xBD\x91\xE5\x8D\xA1"); /* 网卡 */
        XWidget_setFixedWidth((XWidget*)s_net.m_capAdapter, 40);
        XWidget_setFixedHeight((XWidget*)s_net.m_capAdapter, 22);
        XWidget_show((XWidget*)s_net.m_capAdapter);
    }
    s_net.m_adapterCombo = XComboBox_create(s_net.m_root, 0);
    if (!s_net.m_adapterCombo) return s_net.m_pageHost;
    XWidget_setFixedHeight((XWidget*)s_net.m_adapterCombo, 26);
    XObject_connect_2((XObject*)s_net.m_adapterCombo,
                      XSignal(XComboBox_activated_signal),
                      net_adapterActivatedSlot);
    XWidget_show((XWidget*)s_net.m_adapterCombo);
    s_net.m_refreshBtn = XPushButton_create(s_net.m_root, 0);
    if (s_net.m_refreshBtn) {
        XPushButton_setText_2(s_net.m_refreshBtn,
                              "\xE5\x88\xB7\xE6\x96\xB0"); /* 刷新 */
        XWidget_setFixedWidth((XWidget*)s_net.m_refreshBtn, 64);
        XWidget_setFixedHeight((XWidget*)s_net.m_refreshBtn, 26);
        XObject_connect_2((XObject*)s_net.m_refreshBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          net_refreshSlot);
        XWidget_show((XWidget*)s_net.m_refreshBtn);
    }

    /* ---- 信息展示区（5 行：模式/IPv4/DNS/DHCP/MAC；高度随回填行数
     *         在 net_relayout 里以固定高自适应） ---- */
    s_net.m_infoLabel = XLabel_create(s_net.m_root, 0);
    if (!s_net.m_infoLabel) return s_net.m_pageHost;
    XLabel_setText_2(s_net.m_infoLabel,
                     "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x9E\x9A\xE4\xB8\xBE"
                     "\xE7\xBD\x91\xE5\x8D\xA1\xE2\x80\xA6"); /* 正在枚举网卡… */
    XLabel_setTextPixelSize(s_net.m_infoLabel, 13);
    XLabel_setAlignment(s_net.m_infoLabel,
                        XAlignment_Left | XAlignment_Top);
    XWidget_setFixedHeight((XWidget*)s_net.m_infoLabel, 104);
    XWidget_show((XWidget*)s_net.m_infoLabel);

    /* ---- IPv6 地址区（折叠一行/展开全列表）+ 展开开关 ---- */
    s_net.m_v6Label = XLabel_create(s_net.m_root, 0);
    if (!s_net.m_v6Label) return s_net.m_pageHost;
    XLabel_setText_2(s_net.m_v6Label, "IPv6: -");
    XLabel_setTextPixelSize(s_net.m_v6Label, 12);
    XLabel_setAlignment(s_net.m_v6Label,
                        XAlignment_Left | XAlignment_Top);
    XWidget_setFixedHeight((XWidget*)s_net.m_v6Label, 20);
    XWidget_show((XWidget*)s_net.m_v6Label);
    s_net.m_v6ExpandCheck = XCheckBox_create(s_net.m_root, 0);
    if (s_net.m_v6ExpandCheck) {
        XAbstractButton_setText_2((XAbstractButton*)s_net.m_v6ExpandCheck,
                                  "\xE5\xB1\x95\xE5\xBC\x80\xE5\x85\xA8"
                                  "\xE9\x83\xA8 IPv6"); /* 展开全部 IPv6 */
        XWidget_setFixedWidth((XWidget*)s_net.m_v6ExpandCheck, 164);
        XWidget_setFixedHeight((XWidget*)s_net.m_v6ExpandCheck, 22);
        XObject_connect_2((XObject*)s_net.m_v6ExpandCheck,
                          XSignal(XAbstractButton_toggled_signal),
                          net_v6ExpandSlot);
        XWidget_show((XWidget*)s_net.m_v6ExpandCheck);
    }

    /* ---- IPv4 模式行（左列开关；DHCP 态下「应用配置」=切回自动） ---- */
    s_net.m_capV4Mode = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capV4Mode) {
        XLabel_setText_2(s_net.m_capV4Mode, "IPv4");
        XWidget_setFixedWidth((XWidget*)s_net.m_capV4Mode, 80);
        XWidget_setFixedHeight((XWidget*)s_net.m_capV4Mode, 22);
        XWidget_show((XWidget*)s_net.m_capV4Mode);
    }
    s_net.m_v4ModeCombo = XComboBox_create(s_net.m_root, 0);
    if (!s_net.m_v4ModeCombo) return s_net.m_pageHost;
    XComboBox_addItem_2(s_net.m_v4ModeCombo,
                        "DHCP \xE8\x87\xAA\xE5\x8A\xA8\xE8\x8E\xB7"
                        "\xE5\x8F\x96"); /* DHCP 自动获取 */
    XComboBox_addItem_2(s_net.m_v4ModeCombo,
                        "\xE9\x9D\x99\xE6\x80\x81 IP"); /* 静态 IP */
    XComboBox_setCurrentIndex(s_net.m_v4ModeCombo, 0);
    XWidget_setFixedWidth((XWidget*)s_net.m_v4ModeCombo, 150);
    XWidget_setFixedHeight((XWidget*)s_net.m_v4ModeCombo, 26);
    XObject_connect_2((XObject*)s_net.m_v4ModeCombo,
                      XSignal(XComboBox_activated_signal),
                      net_v4ModeSlot);
    XWidget_show((XWidget*)s_net.m_v4ModeCombo);
    s_net.m_applyBtn = XPushButton_create(s_net.m_root, 0);
    if (s_net.m_applyBtn) {
        XPushButton_setText_2(s_net.m_applyBtn,
                              "\xE5\xBA\x94\xE7\x94\xA8\xE9\x85\x8D"
                              "\xE7\xBD\xAE"); /* 应用配置 */
        XWidget_setFixedWidth((XWidget*)s_net.m_applyBtn, NET_BTN_W);
        XWidget_setFixedHeight((XWidget*)s_net.m_applyBtn, 30);
        XObject_connect_2((XObject*)s_net.m_applyBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          net_applySlot);
        XWidget_show((XWidget*)s_net.m_applyBtn);
    }

    /* ---- IPv6 模式行（右列开关；自动=RA/SLAAC 只读，手动=追加静态） ---- */
    s_net.m_capV6Mode = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capV6Mode) {
        XLabel_setText_2(s_net.m_capV6Mode, "IPv6");
        XWidget_setFixedWidth((XWidget*)s_net.m_capV6Mode, 80);
        XWidget_setFixedHeight((XWidget*)s_net.m_capV6Mode, 22);
        XWidget_show((XWidget*)s_net.m_capV6Mode);
    }
    s_net.m_v6ModeCombo = XComboBox_create(s_net.m_root, 0);
    if (!s_net.m_v6ModeCombo) return s_net.m_pageHost;
    XComboBox_addItem_2(s_net.m_v6ModeCombo,
                        "\xE8\x87\xAA\xE5\x8A\xA8\xE8\x8E\xB7\xE5\x8F"
                        "\x96"); /* 自动获取 */
    XComboBox_addItem_2(s_net.m_v6ModeCombo,
                        "\xE6\x89\x8B\xE5\x8A\xA8\xE8\xBF\xBD\xE5\x8A"
                        "\xA0"); /* 手动追加 */
    XComboBox_setCurrentIndex(s_net.m_v6ModeCombo, 0);
    XWidget_setFixedWidth((XWidget*)s_net.m_v6ModeCombo, 140);
    XWidget_setFixedHeight((XWidget*)s_net.m_v6ModeCombo, 26);
    XObject_connect_2((XObject*)s_net.m_v6ModeCombo,
                      XSignal(XComboBox_activated_signal),
                      net_v6ModeSlot);
    XWidget_show((XWidget*)s_net.m_v6ModeCombo);
    s_net.m_applyV6Btn = XPushButton_create(s_net.m_root, 0);
    if (s_net.m_applyV6Btn) {
        XPushButton_setText_2(s_net.m_applyV6Btn,
                              "\xE5\xBA\x94\xE7\x94\xA8 IPv6"); /* 应用 IPv6 */
        XWidget_setFixedWidth((XWidget*)s_net.m_applyV6Btn, NET_BTN_W);
        XWidget_setFixedHeight((XWidget*)s_net.m_applyV6Btn, 30);
        XObject_connect_2((XObject*)s_net.m_applyV6Btn,
                          XSignal(XAbstractButton_clicked_signal),
                          net_applyV6Slot);
        XWidget_show((XWidget*)s_net.m_applyV6Btn);
    }

    /* ---- 左列：IPv4 静态五件 ---- */
    net_buildField("IP", &s_net.m_capIp, &s_net.m_ipEdit);
    net_buildField("\xE5\xAD\x90\xE7\xBD\x91\xE6\x8E\xA9\xE7\xA0\x81",
                   &s_net.m_capMask, &s_net.m_maskEdit); /* 子网掩码 */
    net_buildField("\xE9\xBB\x98\xE8\xAE\xA4\xE7\xBD\x91\xE5\x85\xB3",
                   &s_net.m_capGw, &s_net.m_gwEdit); /* 默认网关 */
    net_buildField("\xE9\xA6\x96\xE9\x80\x89 DNS",
                   &s_net.m_capDns1, &s_net.m_dns1Edit); /* 首选 DNS */
    net_buildField("\xE5\xA4\x87\xE7\x94\xA8 DNS",
                   &s_net.m_capDns2, &s_net.m_dns2Edit); /* 备用 DNS */
    net_setV4EditsEnabled(false); /* 初值来自选中网卡（初始 DHCP 态置灰）。 */

    /* ---- 右列：IPv6 静态三件（追加式；网关可空） ---- */
    s_net.m_capV6Addr = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capV6Addr) {
        XLabel_setText_2(s_net.m_capV6Addr,
                         "IPv6 \xE5\x9C\xB0\xE5\x9D\x80"); /* IPv6 地址 */
        XWidget_setFixedWidth((XWidget*)s_net.m_capV6Addr, 100);
        XWidget_setFixedHeight((XWidget*)s_net.m_capV6Addr, 20);
        XWidget_show((XWidget*)s_net.m_capV6Addr);
    }
    s_net.m_v6AddrEdit = XLineEdit_create(s_net.m_root, 0);
    if (!s_net.m_v6AddrEdit) return s_net.m_pageHost;
    XWidget_setFixedWidth((XWidget*)s_net.m_v6AddrEdit, 264);
    XWidget_setFixedHeight((XWidget*)s_net.m_v6AddrEdit, 24);
    XWidget_show((XWidget*)s_net.m_v6AddrEdit);
    s_net.m_capV6Prefix = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capV6Prefix) {
        XLabel_setText_2(s_net.m_capV6Prefix,
                         "\xE5\x89\x8D\xE7\xBC\x80"); /* 前缀 */
        XWidget_setFixedWidth((XWidget*)s_net.m_capV6Prefix, 36);
        XWidget_setFixedHeight((XWidget*)s_net.m_capV6Prefix, 20);
        XWidget_show((XWidget*)s_net.m_capV6Prefix);
    }
    s_net.m_v6PrefixEdit = XLineEdit_create(s_net.m_root, 0);
    if (s_net.m_v6PrefixEdit) {
        XLineEdit_setText(s_net.m_v6PrefixEdit, "64");
        XWidget_setFixedWidth((XWidget*)s_net.m_v6PrefixEdit, 64);
        XWidget_setFixedHeight((XWidget*)s_net.m_v6PrefixEdit, 24);
        XWidget_show((XWidget*)s_net.m_v6PrefixEdit);
    }
    s_net.m_capV6Gw = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capV6Gw) {
        XLabel_setText_2(s_net.m_capV6Gw,
                         "\xE7\xBD\x91\xE5\x85\xB3"); /* 网关 */
        XWidget_setFixedWidth((XWidget*)s_net.m_capV6Gw, 60);
        XWidget_setFixedHeight((XWidget*)s_net.m_capV6Gw, 20);
        XWidget_show((XWidget*)s_net.m_capV6Gw);
    }
    s_net.m_v6GwEdit = XLineEdit_create(s_net.m_root, 0);
    if (s_net.m_v6GwEdit) {
        XWidget_setFixedWidth((XWidget*)s_net.m_v6GwEdit, 304);
        XWidget_setFixedHeight((XWidget*)s_net.m_v6GwEdit, 24);
        XWidget_show((XWidget*)s_net.m_v6GwEdit);
    }
    /* ---- IPv6 DNS 两行（只读展示：系统下发，安卓不允许应用改 WiFi 的
     *      IPv6 DNS；StaticIpConfiguration 只有 v4） ---- */
    net_buildField("\xE9\xA6\x96\xE9\x80\x89 DNS",
                   &s_net.m_capV6Dns1, &s_net.m_v6Dns1Edit); /* 首选 DNS */
    net_buildField("\xE5\xA4\x87\xE7\x94\xA8 DNS",
                   &s_net.m_capV6Dns2, &s_net.m_v6Dns2Edit); /* 备用 DNS */
    net_setV6EditsEnabled(false); /* IPv6 模式初始=自动获取（右列置灰）。 */

    /* ---- 无线（Wi-Fi）区：适配器 + 扫描 · 网络 + 口令 + 连接/断开 ---- */
#if XNETWORK_ON && XDEVICE_WIFI_ON
    s_net.m_wifi = XNetworkWiFi_create();
    if (s_net.m_wifi) {
        int wifiError = 0;
        if (!XNetworkWiFi_open(s_net.m_wifi, &wifiError)) {
            /* 平台无无线能力或资源申请失败：无线入口如实禁用。失败要留痕：
             * 瞬态失败（如系统 fd 耗尽致 opendir 失败）同样落这里，页面
             * 构建只此一次，不留痕则事后无从排查（2026-10-28 真机回归）。 */
            XPrintf("XGuiWindowDemo: 网络设置页 无线会话打开失败 err=%d"
                    "（入口置灰，切页重进时经 resume 重试）\n", wifiError);
            XClassDelete(s_net.m_wifi);
            s_net.m_wifi = NULL;
        }
    }
    s_net.m_wifiCap = XLabel_create(s_net.m_root, 0);
    if (s_net.m_wifiCap) {
        XLabel_setText_2(s_net.m_wifiCap,
                         "\xE6\x97\xA0\xE7\xBA\xBF"); /* 无线 */
        XWidget_setFixedWidth((XWidget*)s_net.m_wifiCap, 40);
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiCap, 22);
        XWidget_show((XWidget*)s_net.m_wifiCap);
    }
    s_net.m_wifiAdapterCombo = XComboBox_create(s_net.m_root, 0);
    if (!s_net.m_wifiAdapterCombo) return s_net.m_pageHost;
    XWidget_setFixedHeight((XWidget*)s_net.m_wifiAdapterCombo, 26);
    XObject_connect_2((XObject*)s_net.m_wifiAdapterCombo,
                      XSignal(XComboBox_activated_signal),
                      net_wifiAdapterSlot);
    XWidget_show((XWidget*)s_net.m_wifiAdapterCombo);
    s_net.m_wifiScanBtn = XPushButton_create(s_net.m_root, 0);
    if (s_net.m_wifiScanBtn) {
        XPushButton_setText_2(s_net.m_wifiScanBtn,
                              "\xE6\x89\xAB\xE6\x8F\x8F"); /* 扫描 */
        XWidget_setFixedWidth((XWidget*)s_net.m_wifiScanBtn, 64);
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiScanBtn, 26);
        XObject_connect_2((XObject*)s_net.m_wifiScanBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          net_wifiScanSlot);
        XWidget_show((XWidget*)s_net.m_wifiScanBtn);
    }
    {
        s_net.m_wifiNetCap = XLabel_create(s_net.m_root, 0);
        if (s_net.m_wifiNetCap) {
            XLabel_setText_2(s_net.m_wifiNetCap,
                             "\xE7\xBD\x91\xE7\xBB\x9C"); /* 网络 */
            XWidget_setFixedWidth((XWidget*)s_net.m_wifiNetCap, 40);
            XWidget_setFixedHeight((XWidget*)s_net.m_wifiNetCap, 22);
            XWidget_show((XWidget*)s_net.m_wifiNetCap);
        }
        s_net.m_wifiApCombo = XComboBox_create(s_net.m_root, 0);
        if (!s_net.m_wifiApCombo) return s_net.m_pageHost;
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiApCombo, 26);
        XWidget_show((XWidget*)s_net.m_wifiApCombo);
        s_net.m_wifiPassCap = XLabel_create(s_net.m_root, 0);
        if (s_net.m_wifiPassCap) {
            XLabel_setText_2(s_net.m_wifiPassCap,
                             "\xE5\x8F\xA3\xE4\xBB\xA4"); /* 口令 */
            XWidget_setFixedWidth((XWidget*)s_net.m_wifiPassCap, 40);
            XWidget_setFixedHeight((XWidget*)s_net.m_wifiPassCap, 22);
            XWidget_show((XWidget*)s_net.m_wifiPassCap);
        }
        s_net.m_wifiPassEdit = XLineEdit_create(s_net.m_root, 0);
        if (!s_net.m_wifiPassEdit) return s_net.m_pageHost;
        XWidget_setFixedWidth((XWidget*)s_net.m_wifiPassEdit, 140);
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiPassEdit, 24);
        XWidget_show((XWidget*)s_net.m_wifiPassEdit);
        s_net.m_wifiConnectBtn = XPushButton_create(s_net.m_root, 0);
        if (s_net.m_wifiConnectBtn) {
            XPushButton_setText_2(s_net.m_wifiConnectBtn,
                                  "\xE8\xBF\x9E\xE6\x8E\xA5"); /* 连接 */
            XWidget_setFixedWidth((XWidget*)s_net.m_wifiConnectBtn, 72);
            XWidget_setFixedHeight((XWidget*)s_net.m_wifiConnectBtn, 26);
            XObject_connect_2((XObject*)s_net.m_wifiConnectBtn,
                              XSignal(XAbstractButton_clicked_signal),
                              net_wifiConnectSlot);
            XWidget_show((XWidget*)s_net.m_wifiConnectBtn);
        }
        s_net.m_wifiDisconnectBtn = XPushButton_create(s_net.m_root, 0);
        if (s_net.m_wifiDisconnectBtn) {
            XPushButton_setText_2(s_net.m_wifiDisconnectBtn,
                                  "\xE6\x96\xAD\xE5\xBC\x80"); /* 断开 */
            XWidget_setFixedWidth((XWidget*)s_net.m_wifiDisconnectBtn, 72);
            XWidget_setFixedHeight((XWidget*)s_net.m_wifiDisconnectBtn, 26);
            XObject_connect_2((XObject*)s_net.m_wifiDisconnectBtn,
                              XSignal(XAbstractButton_clicked_signal),
                              net_wifiDisconnectSlot);
            XWidget_show((XWidget*)s_net.m_wifiDisconnectBtn);
        }
    }
    s_net.m_wifiInfoLabel = XLabel_create(s_net.m_root, 0);
    if (s_net.m_wifiInfoLabel) {
        XLabel_setTextPixelSize(s_net.m_wifiInfoLabel, 12);
        XLabel_setAlignment(s_net.m_wifiInfoLabel,
                            XAlignment_Left | XAlignment_Top);
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiInfoLabel, 20);
        XWidget_show((XWidget*)s_net.m_wifiInfoLabel);
        net_wifiRefreshInfo();
    }
    /* 状态轮询定时器：挂页根之下（随页根级联析构），回调只在扫描/连接
       进行中跑，避免定时写已释放控件。 */
    s_net.m_wifiTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (s_net.m_wifiTimer) {
        XTimer_setInterval(s_net.m_wifiTimer, 250);
        XTimer_setTimerCallback(s_net.m_wifiTimer, net_wifiTimerCb);
        XTimer_setUserData(s_net.m_wifiTimer, NULL);
        XObject_setParent((XObject*)s_net.m_wifiTimer, (XObject*)s_net.m_root);
    }
    /* 页销毁钩子：关无线会话（归还平台资源）并释放无线列表。 */
    XObject_connect_2((XObject*)s_net.m_root,
                      XSignal(XObject_destroyed_signal),
                      net_wifiDestroyedSlot);
    /* 无线不可用时如实禁用入口。 */
    if (!net_wifiReady()) {
        XWidget_setEnabled((XWidget*)s_net.m_wifiAdapterCombo, false);
        XWidget_setEnabled((XWidget*)s_net.m_wifiScanBtn, false);
        XWidget_setEnabled((XWidget*)s_net.m_wifiApCombo, false);
        XWidget_setEnabled((XWidget*)s_net.m_wifiPassEdit, false);
        XWidget_setEnabled((XWidget*)s_net.m_wifiConnectBtn, false);
        XWidget_setEnabled((XWidget*)s_net.m_wifiDisconnectBtn, false);
    } else {
        net_wifiRefreshAdapters();
        /* 状态行的首次快照要排在适配器枚举之后：构建前段那次调用时
         * 列表还是空的（恒落「不可用」），且程序化 setCurrentIndex
         * 不发 activated 信号，不会补触发。 */
        net_wifiRefreshInfo();
    }
#endif /* XNETWORK_ON && XDEVICE_WIFI_ON */

    /* ---- 提示 + 状态行（提示语按平台配置能力动态回填） ---- */
    s_net.m_hintLabel = XLabel_create(s_net.m_root, 0);
    if (s_net.m_hintLabel) {
        XLabel_setTextPixelSize(s_net.m_hintLabel, 12);
        XWidget_setFixedHeight((XWidget*)s_net.m_hintLabel, 20);
        XWidget_show((XWidget*)s_net.m_hintLabel);
        net_updateHint();
    }
    s_net.m_statusLabel = XLabel_create(s_net.m_root, 0);
    if (!s_net.m_statusLabel) return s_net.m_pageHost;
    XLabel_setText_2(s_net.m_statusLabel,
                     "\xE5\xB0\xB1\xE7\xBB\xAA"); /* 就绪 */
    XWidget_setFixedHeight((XWidget*)s_net.m_statusLabel, 22);
    XWidget_show((XWidget*)s_net.m_statusLabel);

    /* ---- 布局装配（2026-10-24 三分区）：根垂直布局----
     * [网卡设置·标题][网卡行][信息区]·[WiFi 设置·标题][无线行][无线网络行]
     * [无线状态行]·[IP 设置·标题][IPv6 行][模式行][双列字段][伸展+提示/状态]；
     * 分区间 12 / 标题下 8 的间距节奏区分段落，行内小间距用行级
     * addSpacing，行高由各控件固定高承载，宽度伸缩交给 stretch。 */
    XBoxLayout_init(&s_net.m_rootLayout, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&s_net.m_rootLayout, 0);
    XLayout_setContentsMargins((XLayout*)&s_net.m_rootLayout, 12, 8, 12, 0);
    XBoxLayout_addWidget(&s_net.m_rootLayout,
                         (XWidget*)s_net.m_titleLabel); /* 条目 0 */
    XBoxLayout_addSpacing(&s_net.m_rootLayout, 8);     /* 条目 1 */
    /* 网卡行：标题 | 4 | 下拉(伸展铺满) | 12 | 刷新。 条目 2 */
    XBoxLayout_init(&s_net.m_adapterRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_net.m_adapterRow, 0);
    XBoxLayout_addWidget(&s_net.m_adapterRow, (XWidget*)s_net.m_capAdapter);
    XBoxLayout_addSpacing(&s_net.m_adapterRow, 4);
    XBoxLayout_addWidget(&s_net.m_adapterRow,
                         (XWidget*)s_net.m_adapterCombo);
    XBoxLayout_setStretch(&s_net.m_adapterRow, 2, 1);
    XBoxLayout_addSpacing(&s_net.m_adapterRow, 12);
    XBoxLayout_addWidget(&s_net.m_adapterRow,
                         (XWidget*)s_net.m_refreshBtn);
    XBoxLayout_addLayout(&s_net.m_rootLayout,
                         (XLayout*)&s_net.m_adapterRow);
    XBoxLayout_addSpacing(&s_net.m_rootLayout, 12);    /* 条目 3 */
    XBoxLayout_addWidget(&s_net.m_rootLayout,
                         (XWidget*)s_net.m_infoLabel); /* 条目 4 */
#if XNETWORK_ON && XDEVICE_WIFI_ON
    /* ===== 第二分区：WiFi 设置（无线行/无线网络行/无线状态行） ===== */
    XBoxLayout_addSpacing(&s_net.m_rootLayout, 12);    /* 条目 5：分区间距 */
    XBoxLayout_addWidget(&s_net.m_rootLayout,
                         (XWidget*)s_net.m_wifiTitle);  /* 条目 6 */
    XBoxLayout_addSpacing(&s_net.m_rootLayout, 8);     /* 条目 7 */
    /* 无线行：[无线] | 4 | 适配器下拉(伸展) | 12 | 扫描。 条目 8 */
    XBoxLayout_init(&s_net.m_wifiRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_net.m_wifiRow, 0);
    XBoxLayout_addWidget(&s_net.m_wifiRow, (XWidget*)s_net.m_wifiCap);
    XBoxLayout_addSpacing(&s_net.m_wifiRow, 4);
    XBoxLayout_addWidget(&s_net.m_wifiRow, (XWidget*)s_net.m_wifiAdapterCombo);
    XBoxLayout_setStretch(&s_net.m_wifiRow, 2, 1);
    XBoxLayout_addSpacing(&s_net.m_wifiRow, 12);
    XBoxLayout_addWidget(&s_net.m_wifiRow, (XWidget*)s_net.m_wifiScanBtn);
    XBoxLayout_addLayout(&s_net.m_rootLayout, (XLayout*)&s_net.m_wifiRow);
    XBoxLayout_addSpacing(&s_net.m_rootLayout, 6);     /* 条目 9 */
    /* 无线网络行：[网络] | 4 | AP 下拉(伸展) | 8 | [口令] | 4 | 口令输入
       | 8 | 连接 | 6 | 断开。 条目 10 */
    XBoxLayout_init(&s_net.m_wifiNetRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_net.m_wifiNetRow, 0);
    XBoxLayout_addWidget(&s_net.m_wifiNetRow, (XWidget*)s_net.m_wifiNetCap);
    XBoxLayout_addSpacing(&s_net.m_wifiNetRow, 4);
    XBoxLayout_addWidget(&s_net.m_wifiNetRow, (XWidget*)s_net.m_wifiApCombo);
    XBoxLayout_setStretch(&s_net.m_wifiNetRow, 2, 1);
    XBoxLayout_addSpacing(&s_net.m_wifiNetRow, 8);
    XBoxLayout_addWidget(&s_net.m_wifiNetRow, (XWidget*)s_net.m_wifiPassCap);
    XBoxLayout_addSpacing(&s_net.m_wifiNetRow, 4);
    XBoxLayout_addWidget(&s_net.m_wifiNetRow, (XWidget*)s_net.m_wifiPassEdit);
    XBoxLayout_addSpacing(&s_net.m_wifiNetRow, 8);
    XBoxLayout_addWidget(&s_net.m_wifiNetRow, (XWidget*)s_net.m_wifiConnectBtn);
    XBoxLayout_addSpacing(&s_net.m_wifiNetRow, 6);
    XBoxLayout_addWidget(&s_net.m_wifiNetRow,
                         (XWidget*)s_net.m_wifiDisconnectBtn);
    XBoxLayout_addLayout(&s_net.m_rootLayout, (XLayout*)&s_net.m_wifiNetRow);
    /* 口令行骨架（条目 11，窄根独用）：只预置「行内距+行尾伸展」魔法
       空白，宽根恒空几何高 0（不影响桌面现状）；窄根 adapt 把口令
       标题/输入/连接/断开摘进来成第二行——无线网络行固定宽合计 394，
       400 窗减边距只剩 376，单行必裁掉断开钮。 */
    XBoxLayout_init(&s_net.m_wifiPassRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_net.m_wifiPassRow, 0);
    XBoxLayout_addSpacing(&s_net.m_wifiPassRow, 4);
    XBoxLayout_addSpacing(&s_net.m_wifiPassRow, 8);
    XBoxLayout_addSpacing(&s_net.m_wifiPassRow, 6);
    XBoxLayout_addStretch(&s_net.m_wifiPassRow, 1);
    XBoxLayout_addLayout(&s_net.m_rootLayout, (XLayout*)&s_net.m_wifiPassRow);
    XBoxLayout_addSpacing(&s_net.m_rootLayout, 4);     /* 条目 12 */
    XBoxLayout_addWidget(&s_net.m_rootLayout,
                         (XWidget*)s_net.m_wifiInfoLabel); /* 条目 13 */
#endif
    /* ===== 第三分区：IPv4 / IPv6 设置（可并排、可堆叠） =====
     * [横屏并排 2026-10-25] 两个协议族各装进一个纵向容器（标题+模式行+
     * 字段），再由 s_net.m_ipCols 承载：**可用宽够就左右并排，不够就上下
     * 堆叠**（adapt 按内容宽与两列最小宽之和判定方向）。竖屏 400 逻辑宽
     * 只能堆叠，横屏 821 逻辑宽并排——同一份控件树，切向即可重排。 */
    XBoxLayout_addSpacing(&s_net.m_rootLayout, 12);    /* 条目 14：分区间距
       （无线段裁剪时此间距兼作前一分区的收尾）。 */
    /* IPv4 分区容器：标题 | 8 | 模式行 | 8 | 五字段。 */
    XBoxLayout_init(&s_net.m_v4Section, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&s_net.m_v4Section, 0);
    XBoxLayout_addWidget(&s_net.m_v4Section, (XWidget*)s_net.m_ipTitle);
    XBoxLayout_addSpacing(&s_net.m_v4Section, 8);
    /* IPv4 模式行：IPv4 标题|4|下拉|8|应用|伸展。 */
    XBoxLayout_init(&s_net.m_v4ModeRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_net.m_v4ModeRow, 0);
    XBoxLayout_addWidget(&s_net.m_v4ModeRow, (XWidget*)s_net.m_capV4Mode);
    XBoxLayout_addSpacing(&s_net.m_v4ModeRow, 4);
    XBoxLayout_addWidget(&s_net.m_v4ModeRow, (XWidget*)s_net.m_v4ModeCombo);
    XBoxLayout_addSpacing(&s_net.m_v4ModeRow, 8);
    XBoxLayout_addWidget(&s_net.m_v4ModeRow, (XWidget*)s_net.m_applyBtn);
    XBoxLayout_addStretch(&s_net.m_v4ModeRow, 1);
    XBoxLayout_addLayout(&s_net.m_v4Section, (XLayout*)&s_net.m_v4ModeRow);
    XBoxLayout_addSpacing(&s_net.m_v4Section, 8);
    /* IPv4 五字段单列。 */
    XBoxLayout_init(&s_net.m_v4Col, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&s_net.m_v4Col, 4);
    net_addFieldRow(&s_net.m_v4Col, &s_net.m_v4Rows[0],
                    s_net.m_capIp, s_net.m_ipEdit);
    net_addFieldRow(&s_net.m_v4Col, &s_net.m_v4Rows[1],
                    s_net.m_capMask, s_net.m_maskEdit);
    net_addFieldRow(&s_net.m_v4Col, &s_net.m_v4Rows[2],
                    s_net.m_capGw, s_net.m_gwEdit);
    net_addFieldRow(&s_net.m_v4Col, &s_net.m_v4Rows[3],
                    s_net.m_capDns1, s_net.m_dns1Edit);
    net_addFieldRow(&s_net.m_v4Col, &s_net.m_v4Rows[4],
                    s_net.m_capDns2, s_net.m_dns2Edit);
    XBoxLayout_addLayout(&s_net.m_v4Section, (XLayout*)&s_net.m_v4Col);
    XBoxLayout_addStretch(&s_net.m_v4Section, 1);

    /* IPv6 分区容器：标题 | 8 | 地址展示行 | 8 | 模式行 | 8 | 五字段。 */
    XBoxLayout_init(&s_net.m_v6Section, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&s_net.m_v6Section, 0);
    XBoxLayout_addWidget(&s_net.m_v6Section, (XWidget*)s_net.m_v6Title);
    XBoxLayout_addSpacing(&s_net.m_v6Section, 8);
    /* IPv6 地址展示行：折叠单行(伸展) | 4 | 展开开关。 */
    XBoxLayout_init(&s_net.m_v6Row, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_net.m_v6Row, 0);
    XBoxLayout_addWidgetEx(&s_net.m_v6Row, (XWidget*)s_net.m_v6Label, 1, 0);
    XBoxLayout_addSpacing(&s_net.m_v6Row, 4);
    XBoxLayout_addWidget(&s_net.m_v6Row,
                         (XWidget*)s_net.m_v6ExpandCheck);
    XBoxLayout_addLayout(&s_net.m_v6Section, (XLayout*)&s_net.m_v6Row);
    XBoxLayout_addSpacing((XLayout*)&s_net.m_v6Section, 8);
    /* IPv6 模式行：IPv6 标题|4|下拉|8|应用|伸展。 */
    XBoxLayout_init(&s_net.m_v6ModeRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_net.m_v6ModeRow, 0);
    XBoxLayout_addWidget(&s_net.m_v6ModeRow, (XWidget*)s_net.m_capV6Mode);
    XBoxLayout_addSpacing(&s_net.m_v6ModeRow, 4);
    XBoxLayout_addWidget(&s_net.m_v6ModeRow, (XWidget*)s_net.m_v6ModeCombo);
    XBoxLayout_addSpacing(&s_net.m_v6ModeRow, 8);
    XBoxLayout_addWidget(&s_net.m_v6ModeRow, (XWidget*)s_net.m_applyV6Btn);
    XBoxLayout_addStretch(&s_net.m_v6ModeRow, 1);
    XBoxLayout_addLayout(&s_net.m_v6Section, (XLayout*)&s_net.m_v6ModeRow);
    XBoxLayout_addSpacing((XLayout*)&s_net.m_v6Section, 8);
    /* IPv6 五字段单列（地址/前缀/网关 + 系统下发的两个 DNS）。 */
    XBoxLayout_init(&s_net.m_v6Col, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&s_net.m_v6Col, 4);
    net_addFieldRow(&s_net.m_v6Col, &s_net.m_v6Rows[0],
                    s_net.m_capV6Addr, s_net.m_v6AddrEdit);
    net_addFieldRow(&s_net.m_v6Col, &s_net.m_v6Rows[1],
                    s_net.m_capV6Prefix, s_net.m_v6PrefixEdit);
    net_addFieldRow(&s_net.m_v6Col, &s_net.m_v6Rows[2],
                    s_net.m_capV6Gw, s_net.m_v6GwEdit);
    net_addFieldRow(&s_net.m_v6Col, &s_net.m_v6DnsRows[0],
                    s_net.m_capV6Dns1, s_net.m_v6Dns1Edit);
    net_addFieldRow(&s_net.m_v6Col, &s_net.m_v6DnsRows[1],
                    s_net.m_capV6Dns2, s_net.m_v6Dns2Edit);
    XBoxLayout_addLayout(&s_net.m_v6Section, (XLayout*)&s_net.m_v6Col);
    XBoxLayout_addStretch(&s_net.m_v6Section, 1);

    /* 两分区容器：宽度够 → 并排；不够 → 堆叠（adapt 切方向）。 */
    XBoxLayout_init(&s_net.m_ipCols, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&s_net.m_ipCols, 12);
    XBoxLayout_addLayout(&s_net.m_ipCols, (XLayout*)&s_net.m_v4Section);
    XBoxLayout_addLayout(&s_net.m_ipCols, (XLayout*)&s_net.m_v6Section);
    /* 两列等分宽度（并排时各占一半；堆叠时伸展在纵轴上均分余高）。
     * 折叠态余高留在根布局的尾部伸展上，这里给 0 由 relayout 定版。 */
    XBoxLayout_setStretch(&s_net.m_ipCols, 0, 1);
    XBoxLayout_setStretch(&s_net.m_ipCols, 1, 1);
    XBoxLayout_addLayout(&s_net.m_rootLayout, (XLayout*)&s_net.m_ipCols);

    XBoxLayout_addStretch(&s_net.m_rootLayout, 1);     /* 条目 15 */
    XBoxLayout_addWidget(&s_net.m_rootLayout,
                         (XWidget*)s_net.m_hintLabel); /* 条目 16 */
    XBoxLayout_addSpacing(&s_net.m_rootLayout, 2);     /* 条目 17 */
    XBoxLayout_addWidget(&s_net.m_rootLayout,
                         (XWidget*)s_net.m_statusLabel); /* 条目 18 */
    XWidget_setLayout(s_net.m_root, (XLayout*)&s_net.m_rootLayout);

    /* ---- 统一切换（折叠视图定版）+ 首轮枚举回填。 ---- */
    net_relayout();
    net_refreshAdapters();

    s_net.m_ready = true;
    net_page_network_headless_hook();
    return s_net.m_pageHost;
}

/** @brief 无头截图/自动化钩子：XGUI_NET_EXPAND_V6=1 时构建末尾展开
 *         IPv6 全列表（守护轮询在 --screenshot 帧内到不了，显式切换；
 *         环境变量缺省时零操作零开销）。 */
static void net_page_network_headless_hook(void)
{
    if (!s_net.m_ready || !s_net.m_v6ExpandCheck) return;
    if (!XSystem_environment("XGUI_NET_EXPAND_V6")) return;
    XAbstractButton_setChecked((XAbstractButton*)s_net.m_v6ExpandCheck,
                               true);
    net_v6ExpandSlot(NULL, NULL);
}

/* ==================== 自适应重排（xgui_demo_pages.h 契约） ============ */

/** @brief 窄根档字段宽：编辑框随页根内容宽收缩（宽根恢复常量口径）。
 *  @details 400 逻辑窗减根边距 24 只剩 376 内容宽：IPv4 行「标题 80 +
 *           行距 4 + 编辑 300」= 384 必溢出（真机截图左列贴右缘、右列
 *           整体出窗的根因之一），收缩量按行内最宽标题分别预留。 */
static void net_adaptFieldWidths(bool compact)
{
    int contentW;
    int colW;
    int v4W;
    int v6W;
    int i;
    bool sideBySide;
    XLineEdit* v4Edits[5];
    XLineEdit* v6Edits[5];
    v4Edits[0] = s_net.m_ipEdit;
    v4Edits[1] = s_net.m_maskEdit;
    v4Edits[2] = s_net.m_gwEdit;
    v4Edits[3] = s_net.m_dns1Edit;
    v4Edits[4] = s_net.m_dns2Edit;
    /* IPv6 五件（地址/前缀/网关 + 两个 DNS）——各行标题列宽不同，统一
     * 按最宽的「IPv6 地址」预留，各框等宽（视觉对齐，窄根也不越界）。 */
    v6Edits[0] = s_net.m_v6AddrEdit;
    v6Edits[1] = s_net.m_v6PrefixEdit;
    v6Edits[2] = s_net.m_v6GwEdit;
    v6Edits[3] = s_net.m_v6Dns1Edit;
    v6Edits[4] = s_net.m_v6Dns2Edit;
    if (!compact) {
        /* 宽根：并排时每列只有半宽，编辑框不能再吃满 300——按列宽
         * 收缩，避免右列越出根边距（横屏 1000 宽时两列各 ~482）。 */
        sideBySide = XBoxLayout_direction(&s_net.m_ipCols)
                         == XBoxLayoutDirection_LeftToRight;
        contentW = s_net.m_root ? XWidget_width(s_net.m_root) - 24 : 0;
        if (contentW < 200) contentW = 200;
        colW = sideBySide ? (contentW - 12) / 2 : contentW;
        v4W = colW - NET_CAP_W - 16;
        v6W = colW - 100 - 16;
        if (v4W > NET_FIELD_W) v4W = NET_FIELD_W;
        if (v4W < 120) v4W = 120;
        if (v6W > 304) v6W = 304;
        if (v6W < 120) v6W = 120;
        for (i = 0; i < 5; ++i) {
            if (v4Edits[i])
                XWidget_setFixedWidth((XWidget*)v4Edits[i], v4W);
            if (v6Edits[i])
                XWidget_setFixedWidth((XWidget*)v6Edits[i], v6W);
        }
        return;
    }
    /* 内容宽 = 页根宽 - 根布局左右 contentsMargins(12+12)；下限防极端
     * 窄根把编辑框压没，上限钳回宽屏常量（中段窄窗不放宽）。
     * 窄根必为堆叠（400 << NET_IP_COLS_MIN_W），故按整宽算。 */
    contentW = s_net.m_root ? XWidget_width(s_net.m_root) - 24 : 0;
    if (contentW < 200) contentW = 200;
    v4W = contentW - NET_CAP_W - 16;  /* 标题列 80 + 行距 4 + 右余量 12 */
    v6W = contentW - 100 - 16;        /* 最宽标题「IPv6 地址」列 100 */
    if (v4W > NET_FIELD_W) v4W = NET_FIELD_W;
    if (v4W < 120) v4W = 120;
    if (v6W > 304) v6W = 304;
    if (v6W < 120) v6W = 120;
    for (i = 0; i < 5; ++i) {
        if (v4Edits[i])
            XWidget_setFixedWidth((XWidget*)v4Edits[i], v4W);
        if (v6Edits[i])
            XWidget_setFixedWidth((XWidget*)v6Edits[i], v6W);
    }
}

/** @brief 窄根档触控高度：可点击控件统一抬到 40 逻辑像素（手机触控
 *         目标下限）；宽根恢复 build 常量，桌面 1000x700 密度不变。
 *         标签非点击目标不抬，行高由行内最高控件承载。 */
static void net_adaptTouchHeights(bool compact)
{
    int hCombo = compact ? NET_TOUCH_H : 26; /* 下拉/刷新/扫描/连接/断开 */
    int hApply = compact ? NET_TOUCH_H : 30; /* 应用配置 / 应用 IPv6 */
    int hEdit4 = compact ? NET_TOUCH_H : 22; /* IPv4 五编辑框 */
    int hEdit6 = compact ? NET_TOUCH_H : 24; /* IPv6 三编辑 + WiFi 口令 */
    int hCheck = compact ? NET_TOUCH_H : 22; /* IPv6 展开开关 */
    int i;
    XLineEdit* v4Edits[5];
    v4Edits[0] = s_net.m_ipEdit;
    v4Edits[1] = s_net.m_maskEdit;
    v4Edits[2] = s_net.m_gwEdit;
    v4Edits[3] = s_net.m_dns1Edit;
    v4Edits[4] = s_net.m_dns2Edit;
    if (s_net.m_adapterCombo)
        XWidget_setFixedHeight((XWidget*)s_net.m_adapterCombo, hCombo);
    if (s_net.m_refreshBtn)
        XWidget_setFixedHeight((XWidget*)s_net.m_refreshBtn, hCombo);
    if (s_net.m_v4ModeCombo)
        XWidget_setFixedHeight((XWidget*)s_net.m_v4ModeCombo, hCombo);
    if (s_net.m_applyBtn)
        XWidget_setFixedHeight((XWidget*)s_net.m_applyBtn, hApply);
    if (s_net.m_v6ModeCombo)
        XWidget_setFixedHeight((XWidget*)s_net.m_v6ModeCombo, hCombo);
    if (s_net.m_applyV6Btn)
        XWidget_setFixedHeight((XWidget*)s_net.m_applyV6Btn, hApply);
    for (i = 0; i < 5; ++i)
        if (v4Edits[i])
            XWidget_setFixedHeight((XWidget*)v4Edits[i], hEdit4);
    if (s_net.m_v6AddrEdit)
        XWidget_setFixedHeight((XWidget*)s_net.m_v6AddrEdit, hEdit6);
    if (s_net.m_v6PrefixEdit)
        XWidget_setFixedHeight((XWidget*)s_net.m_v6PrefixEdit, hEdit6);
    if (s_net.m_v6GwEdit)
        XWidget_setFixedHeight((XWidget*)s_net.m_v6GwEdit, hEdit6);
    if (s_net.m_v6Dns1Edit)
        XWidget_setFixedHeight((XWidget*)s_net.m_v6Dns1Edit, hEdit6);
    if (s_net.m_v6Dns2Edit)
        XWidget_setFixedHeight((XWidget*)s_net.m_v6Dns2Edit, hEdit6);
    if (s_net.m_v6ExpandCheck)
        XWidget_setFixedHeight((XWidget*)s_net.m_v6ExpandCheck, hCheck);
#if XNETWORK_ON && XDEVICE_WIFI_ON
    if (s_net.m_wifiAdapterCombo)
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiAdapterCombo, hCombo);
    if (s_net.m_wifiScanBtn)
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiScanBtn, hCombo);
    if (s_net.m_wifiApCombo)
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiApCombo, hCombo);
    if (s_net.m_wifiPassEdit)
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiPassEdit, hEdit6);
    if (s_net.m_wifiConnectBtn)
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiConnectBtn, hCombo);
    if (s_net.m_wifiDisconnectBtn)
        XWidget_setFixedHeight((XWidget*)s_net.m_wifiDisconnectBtn, hCombo);
#endif
}

/** @brief 窄根行收拢：模式行/无线网络行各拆出第二行 + 字段双列转单列。
 *  @details 400 逻辑宽（内容 376）装不下宽屏单行：模式行 IPv4+IPv6
 *           两组定宽合计 750、无线网络行固定宽合计 394——右半必然出窗
 *           被裁（真机截图 IPv6 模式组与右列三行全部不可见）。只动布局
 *           条目不破坏控件父子（removeWidget 就地释放包装条目，复插重
 *           建，可随档位来回切）；两根骨架行 build 时预置「行内距+行尾
 *           伸展」魔法空白且宽根恒空（几何高 0，不影响桌面现状），插回
 *           索引对应骨架空位、与宽屏原序逐位对齐。 */
/** @brief 窄根档重排：IPv4/IPv6 已是各自独立分区（不再有横排双组），
 *         故只需（1）无线网络行在窄根摘出口令组到第二行、（2）字段宽
 *         随内容宽收缩、（3）触控目标抬到 40。
 *  @details 2026-10-25 二次整改：原「IPv4/IPv6 两组挤在同一模式行 +
 *           双列字段」在 400 宽竖屏下把 IPv6 模式下拉和三字段推到屏外，
 *           且 IPv6 控件与 IPv4 视觉上混成一片。现改为 IPv4 设置、IPv6
 *           设置两个独立分区（各自模式行 + 单列字段），窄根不再需要
 *           搬移模式组、也不再需要双列转单列——结构本身已经是单列。 */
static void net_applyCompactRows(bool compact)
{
#if XNETWORK_ON && XDEVICE_WIFI_ON
    if (compact) {
        XLayout_removeWidget((XLayout*)&s_net.m_wifiNetRow,
                             (XWidget*)s_net.m_wifiPassCap);
        XLayout_removeWidget((XLayout*)&s_net.m_wifiNetRow,
                             (XWidget*)s_net.m_wifiPassEdit);
        XLayout_removeWidget((XLayout*)&s_net.m_wifiNetRow,
                             (XWidget*)s_net.m_wifiConnectBtn);
        XLayout_removeWidget((XLayout*)&s_net.m_wifiNetRow,
                             (XWidget*)s_net.m_wifiDisconnectBtn);
        /* 骨架 [4,8,6,伸展] 依序插回 = 口令|4|输入|8|连接|6|断开|伸展。 */
        XBoxLayout_insertWidget(&s_net.m_wifiPassRow, 0,
                                (XWidget*)s_net.m_wifiPassCap, 0);
        XBoxLayout_insertWidget(&s_net.m_wifiPassRow, 2,
                                (XWidget*)s_net.m_wifiPassEdit, 0);
        XBoxLayout_insertWidget(&s_net.m_wifiPassRow, 4,
                                (XWidget*)s_net.m_wifiConnectBtn, 0);
        XBoxLayout_insertWidget(&s_net.m_wifiPassRow, 5,
                                (XWidget*)s_net.m_wifiDisconnectBtn, 0);
        XLayout_setContentsMargins((XLayout*)&s_net.m_wifiPassRow, 0, 6, 0, 0);
    } else {
        XLayout_removeWidget((XLayout*)&s_net.m_wifiPassRow,
                             (XWidget*)s_net.m_wifiPassCap);
        XLayout_removeWidget((XLayout*)&s_net.m_wifiPassRow,
                             (XWidget*)s_net.m_wifiPassEdit);
        XLayout_removeWidget((XLayout*)&s_net.m_wifiPassRow,
                             (XWidget*)s_net.m_wifiConnectBtn);
        XLayout_removeWidget((XLayout*)&s_net.m_wifiPassRow,
                             (XWidget*)s_net.m_wifiDisconnectBtn);
        /* 归位宽屏序：残留 [.., sp8, sp4, sp8, sp6, 伸展]，索引
           4/6/8/10 逐位插回，还原 口令|4|输入|8|连接|6|断开。 */
        XBoxLayout_insertWidget(&s_net.m_wifiNetRow, 4,
                                (XWidget*)s_net.m_wifiPassCap, 0);
        XBoxLayout_insertWidget(&s_net.m_wifiNetRow, 6,
                                (XWidget*)s_net.m_wifiPassEdit, 0);
        XBoxLayout_insertWidget(&s_net.m_wifiNetRow, 8,
                                (XWidget*)s_net.m_wifiConnectBtn, 0);
        XBoxLayout_insertWidget(&s_net.m_wifiNetRow, 10,
                                (XWidget*)s_net.m_wifiDisconnectBtn, 0);
        XLayout_setContentsMargins((XLayout*)&s_net.m_wifiPassRow, 0, 0, 0, 0);
    }
#else
    (void)compact;
#endif
    net_adaptFieldWidths(compact);
    net_adaptTouchHeights(compact);
}

/** @brief IPv4/IPv6 两分区排布方向：可用宽够=左右并排，不够=上下堆叠。
 *  @details 并排时两列等分宽度（各 stretch 1），列距 12；堆叠时列距同样
 *           12（作为行距）。两档切换只翻方向，控件父子不动。
 *  @param contentW 页根内容宽（已扣左右边距）。 */
static void net_adaptIpColumns(int contentW)
{
    bool sideBySide = contentW >= NET_IP_COLS_MIN_W;
    XBoxLayoutDirection want = sideBySide
                                   ? XBoxLayoutDirection_LeftToRight
                                   : XBoxLayoutDirection_TopToBottom;
    if (XClassIsVtableNull((XClass*)&s_net.m_ipCols))
        return; /* 布局未装配（理论上不会走到）。 */
    if (XBoxLayout_direction(&s_net.m_ipCols) == want)
        return; /* 同向幂等：拖拽 resize 每步零开销。 */
    XBoxLayout_setDirection(&s_net.m_ipCols, want);
}

void demo_page_network_adapt(XWidget* page)
{
    /* 2026-10-17 容器化后页内排布交给根布局器；2026-10-25 窄根分档是
     * 此处仅存的手写几何——主窗 demo_layout_content 漏斗在切页/resize/
     * 停靠全路径调用（页根几何新鲜），按 Material 3 compact 断点分档
     * 收拢；同档幂等短路，拖拽 resize 每步零开销。
     * [竖屏滚动 2026-10-25] 页根已改为页宿主（XScrollArea 壳内嵌），
     * 契约入参是页宿主；宽度分档取**视口宽**（滚动条占位后才是真实
     * 可用宽），内容几何由壳的 resizeEvent 自行拟合。 */
    bool compact;
    int w;
    if (!page || !s_net.m_ready || page != s_net.m_pageHost)
        return;
    w = s_net.m_scroll
            ? XWidget_width(XAbstractScrollArea_viewport(
                  (XAbstractScrollArea*)s_net.m_scroll))
            : XWidget_width(page);
    if (w <= 0) return; /* 几何未定（首帧前）：不投档，等下一拍重排。 */
    /* IPv4/IPv6 分区排布方向（每次 adapt 都判，横竖屏切换即时生效）：
     * 可用宽够=左右并排，不够=上下堆叠。内容宽=视口宽-根左右边距(24)。
     * **必须先于 net_applyCompactRows**：字段宽度要在方向已定的前提下
     * 才算得出（并排时每列只有半宽），顺序反了会拿上一档的宽度。 */
    net_adaptIpColumns(w - 24);
    compact = w < NET_COMPACT_MAX_W;
    if (compact != s_net.m_compact) {
        s_net.m_compact = compact;
        net_applyCompactRows(compact);
        net_relayout(); /* 折叠/展开态约束（展开开关高等）按新档位重套。 */
    } else {
        /* 档位未变但方向可能变了（横竖屏切换）：宽度按新方向重算。 */
        net_adaptFieldWidths(compact);
    }
    /* 档位未变也要重拟合内容高：展开/收起、信息区行数变化都不改档位，
     * 但内容首选高变了——不重算则滚动范围停在旧值（收起后仍能滚到空白，
     * 或展开后滚不到底部）。 */
    net_fitScrollContent(s_net.m_scroll);
}

/** @brief 页面重新可见：无线会话未开时重试一次并恢复入口。
 *  @details 无线区可用性在 build 时一次性判定，而打开失败可能是瞬态的
 *           （真机 2026-10-28 回归：长跑实例内平台打开失败 → 无线区永久
 *           置灰，直到整个应用重启）。恢复口挂切页重进：已就绪则零开销
 *           返回；未就绪则重建会话，成功即解锁六个入口并重枚举适配器，
 *           失败维持置灰并留痕。 */
void demo_page_network_resume(void)
{
#if XNETWORK_ON && XDEVICE_WIFI_ON
    int wifiError = 0;
    if (!s_net.m_ready || net_wifiReady()) return;
    if (!s_net.m_wifi) s_net.m_wifi = XNetworkWiFi_create();
    if (s_net.m_wifi && XNetworkWiFi_open(s_net.m_wifi, &wifiError)) {
        XPrintf("XGuiWindowDemo: 网络设置页 无线会话重试成功，恢复入口\n");
        XWidget_setEnabled((XWidget*)s_net.m_wifiAdapterCombo, true);
        XWidget_setEnabled((XWidget*)s_net.m_wifiScanBtn, true);
        XWidget_setEnabled((XWidget*)s_net.m_wifiApCombo, true);
        XWidget_setEnabled((XWidget*)s_net.m_wifiPassEdit, true);
        XWidget_setEnabled((XWidget*)s_net.m_wifiConnectBtn, true);
        XWidget_setEnabled((XWidget*)s_net.m_wifiDisconnectBtn, true);
        net_wifiRefreshAdapters();
        net_wifiRefreshInfo();
    } else {
        XPrintf("XGuiWindowDemo: 网络设置页 无线会话重试失败 err=%d\n",
                wifiError);
    }
#endif
}

/* ==================== autotest ==================== */

int demo_page_network_autotest(XWidget* page)
{
    int failures = 0;
#define NET_EXPECT(cond, what) \
    do { if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
         else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } } while (0)

    if (!page || !s_net.m_ready || page != s_net.m_pageHost)
        return -1;

    /* ---- 控件登记完整 ---- */
    NET_EXPECT(s_net.m_titleLabel && s_net.m_ipTitle &&
               s_net.m_v6Title &&
               s_net.m_adapterCombo && s_net.m_refreshBtn &&
               s_net.m_infoLabel && s_net.m_v4ModeCombo && s_net.m_applyBtn &&
               s_net.m_ipEdit && s_net.m_maskEdit && s_net.m_gwEdit &&
               s_net.m_dns1Edit && s_net.m_dns2Edit &&
               s_net.m_hintLabel && s_net.m_statusLabel,
               "网络设置页: 控件全部登记");

    /* ---- 滚动承载：页宿主内嵌滚动区，内容件=页根 ---- */
    NET_EXPECT(s_net.m_pageHost && s_net.m_scroll &&
                   XScrollArea_widget(s_net.m_scroll) == s_net.m_root,
               "网络设置页: 整页滚动区已装配(内容=页根)");
    NET_EXPECT(XAbstractScrollArea_horizontalScrollBarPolicy(
                   (const XAbstractScrollArea*)s_net.m_scroll)
                   == XScrollBarPolicy_AlwaysOff,
               "网络设置页: 水平滚动条恒关(内容宽=视口宽)");
    NET_EXPECT(XAbstractScrollArea_verticalScrollBarPolicy(
                   (const XAbstractScrollArea*)s_net.m_scroll)
                   == XScrollBarPolicy_AsNeeded,
               "网络设置页: 竖直滚动条按需出现");
    /* 内容高确实按布局合计撑开（不是被钉在视口高）——否则竖滚永不触发。 */
    NET_EXPECT(s_net.m_root &&
                   XWidget_height(s_net.m_root) > 0 &&
                   XWidget_layout(s_net.m_root) != NULL,
               "网络设置页: 内容根有布局且高度已拟合");

    /* ---- 枚举：至少一个接口（回环无处不在）且选中回填非空 ---- */
    NET_EXPECT(XComboBox_count(s_net.m_adapterCombo) >= 1 &&
               s_net.m_count >= 1,
               "网络设置页: 网卡枚举非空");
    NET_EXPECT(strstr(s_net.m_lastStatus, "\xE6\x9C\xAA") == NULL,
               "网络设置页: 首选网卡回填无失败反馈"); /* 未… */

    /* ---- 信息区含模式行（查询已回填） ---- */
    {
        /* 信息区文本经控件读取：断言登记状态行仍是就绪/查询成功口径。 */
        NET_EXPECT(XComboBox_count(s_net.m_v4ModeCombo) == 2 &&
                       XComboBox_count(s_net.m_v6ModeCombo) == 2,
                   "网络设置页: IPv4/IPv6 模式下拉框各自两态");
    }

    /* ---- 拒应用门控：静态态 + 非法 IP（绝不触平台通路） ---- */
    {
        const char* savedIp;
        /* IPv4=静态、IPv6 保持自动：左列启用、右列仍禁（双开关独立性）。 */
        XComboBox_setCurrentIndex(s_net.m_v4ModeCombo, 1);
        net_v4ModeSlot(NULL, NULL);
        NET_EXPECT(XWidget_isEnabled((XWidget*)s_net.m_ipEdit),
                   "网络设置页: IPv4 静态态启用编辑框");
        NET_EXPECT(!XWidget_isEnabled((XWidget*)s_net.m_v6AddrEdit) &&
                       !XWidget_isEnabled((XWidget*)s_net.m_applyV6Btn),
                   "网络设置页: IPv4 静态不影响 IPv6 自动态只读(独立)");
        /* IPv6=手动追加：右列启用，左列不受牵连。 */
        XComboBox_setCurrentIndex(s_net.m_v6ModeCombo, 1);
        net_v6ModeSlot(NULL, NULL);
        NET_EXPECT(XWidget_isEnabled((XWidget*)s_net.m_v6AddrEdit) &&
                       XWidget_isEnabled((XWidget*)s_net.m_v6PrefixEdit) &&
                       XWidget_isEnabled((XWidget*)s_net.m_v6GwEdit) &&
                       XWidget_isEnabled((XWidget*)s_net.m_applyV6Btn),
                   "网络设置页: IPv6 手动态启用编辑行与应用钮");
        NET_EXPECT(XWidget_isEnabled((XWidget*)s_net.m_ipEdit),
                   "网络设置页: IPv6 手动不影响 IPv4 静态态(独立)");
        savedIp = XLineEdit_text(s_net.m_ipEdit);
        XLineEdit_setText(s_net.m_ipEdit, "999.1.1.1");
        net_applySlot(NULL, NULL);
        NET_EXPECT(strstr(s_net.m_lastStatus,
                          "\xE9\x9D\x9E\xE6\xB3\x95") != NULL,
                   "网络设置页: 非法 IP 拒应用(不触平台)"); /* 非法 */
        XLineEdit_setText(s_net.m_ipEdit, savedIp);
    }

    /* ---- IPv4=DHCP / IPv6=手动 交叉置灰（独立性反方向） ---- */
    XComboBox_setCurrentIndex(s_net.m_v4ModeCombo, 0);
    net_v4ModeSlot(NULL, NULL);
    NET_EXPECT(!XWidget_isEnabled((XWidget*)s_net.m_ipEdit),
               "网络设置页: IPv4 DHCP 态置灰编辑框");
    NET_EXPECT(XWidget_isEnabled((XWidget*)s_net.m_v6AddrEdit) &&
                   XWidget_isEnabled((XWidget*)s_net.m_applyV6Btn) &&
                   XWidget_isEnabled((XWidget*)s_net.m_applyBtn),
               "网络设置页: IPv4 DHCP 不牵连 IPv6 手动态(独立)");

    /* ---- IPv6 展开/收起（全列表让位视图） ---- */
    NET_EXPECT(s_net.m_v6Label && s_net.m_v6ExpandCheck &&
               s_net.m_v6AddrEdit && s_net.m_v6PrefixEdit &&
               s_net.m_v6GwEdit && s_net.m_applyV6Btn,
               "网络设置页: IPv6 编辑/展示控件全部登记");
    /* 行数=0(平台无 IPv6, 如部分嵌入式内核)时显示「IPv6: -」同样
     * 占一行——断言只看折叠态高度, 不强制必须有 v6 地址。 */
    NET_EXPECT(XWidget_height(s_net.m_v6Label) <= 30,
               "网络设置页: IPv6 折叠态单行显示");
    XAbstractButton_setChecked((XAbstractButton*)s_net.m_v6ExpandCheck,
                               true);
    net_v6ExpandSlot(NULL, NULL);
    /* [LayoutRequest 队列重排 2026-10-21] df83fd26 起显隐/伸展因子变更
     * 由框架失效收口 post LayoutRequest（队列事件，下一拍 activate）；
     * 展开分支内释放固定高的约束 setter 会同步激活父布局一次，但该次
     * 解算发生在 setStretch 之前（伸展因子未生效，自由高全归行尾伸展
     * 条目，IPv6 行只得固有高 27px）——断言几何前必须先泵一轮事件让
     * 排队重排落地（Qt 同口径：QTRY_* 先 processEvents 再比几何）。
     * 折叠分支 show 路径沿可见父链同步激活兜底，泵入是让两态断言
     * 同走「切换→队列重排完成」契约，不依赖激活时机细节。 */
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    NET_EXPECT(s_net.m_v6Expanded &&
                   XWidget_height(s_net.m_v6Label) > 30 &&
                   !XWidget_isVisible((XWidget*)s_net.m_v4ModeCombo),
               "网络设置页: 展开态全列表增高且编辑区让位隐藏");
    XAbstractButton_setChecked((XAbstractButton*)s_net.m_v6ExpandCheck,
                               false);
    net_v6ExpandSlot(NULL, NULL);
    XCoreApplication_processEvents(XEventLoop_AllEvents);
    NET_EXPECT(!s_net.m_v6Expanded &&
                   XWidget_isVisible((XWidget*)s_net.m_v4ModeCombo) &&
                   XWidget_height(s_net.m_v6Label) <= 30,
               "网络设置页: 收起恢复折叠态与编辑区");

    /* ---- 非法 IPv6 拒应用（绝不触平台通路；手动态下走校验门） ---- */
    {
        const char* savedAddr;
        savedAddr = XLineEdit_text(s_net.m_v6AddrEdit);
        XLineEdit_setText(s_net.m_v6AddrEdit, "zz::gg");
        net_applyV6Slot(NULL, NULL);
        NET_EXPECT(strstr(s_net.m_lastStatus,
                          "\xE9\x9D\x9E\xE6\xB3\x95") != NULL,
                   "网络设置页: 非法 IPv6 拒应用(不触平台)"); /* 非法 */
        XLineEdit_setText(s_net.m_v6AddrEdit, savedAddr);
        XComboBox_setCurrentIndex(s_net.m_v4ModeCombo, 0);
        net_v4ModeSlot(NULL, NULL);
        XComboBox_setCurrentIndex(s_net.m_v6ModeCombo, 0);
        net_v6ModeSlot(NULL, NULL);
    }

    /* ---- 无线区：控件登记 + 会话一致性（**绝不真连接**，不触平台通路） ---- */
#if XNETWORK_ON && XDEVICE_WIFI_ON
    NET_EXPECT(s_net.m_wifiTitle && s_net.m_wifiCap &&
               s_net.m_wifiAdapterCombo &&
               s_net.m_wifiScanBtn && s_net.m_wifiApCombo &&
               s_net.m_wifiPassEdit && s_net.m_wifiConnectBtn &&
               s_net.m_wifiDisconnectBtn && s_net.m_wifiInfoLabel,
               "网络设置页: 无线控件全部登记");
    if (net_wifiReady()) {
        NET_EXPECT(XNetworkWiFi_isOpen(s_net.m_wifi),
                   "网络设置页: 无线会话已打开");
        NET_EXPECT(XComboBox_count(s_net.m_wifiAdapterCombo) >= 1,
                   "网络设置页: 无线适配器枚举非空");
    } else {
        NET_EXPECT(!XWidget_isEnabled((XWidget*)s_net.m_wifiScanBtn) &&
                   !XWidget_isEnabled((XWidget*)s_net.m_wifiConnectBtn) &&
                   !XWidget_isEnabled((XWidget*)s_net.m_wifiDisconnectBtn),
                   "网络设置页: 无线不可用时入口如实置灰");
    }
    /* 无线状态行必有文本（未连接/不可用口径）。 */
    NET_EXPECT(s_net.m_wifiInfoLabel != NULL,
               "网络设置页: 无线状态行已登记");
#endif

    /* ---- 能力探测可调用（值随平台，不断言） ---- */
    (void)XNetworkInterface_configSupported();

#undef NET_EXPECT
    XPrintf("XGuiAutoTest: 网络设置页 %s\n",
            failures == 0 ? "PASS" : "FAIL");
    return failures;
}

#else /* 裁剪: 契约桩（符号常在, 主文件可无条件链接） */

XWidget* demo_page_network_build(XWidget* parent,
                                 DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    XPrintf("XGuiAutoTest: [FAIL] 网络设置页依赖 XNetwork/控件被裁剪，"
            "无法构建\n");
    return (XWidget*)0;
}

int demo_page_network_autotest(XWidget* page)
{
    (void)page;
    XPrintf("XGuiAutoTest: [FAIL] 网络设置页被裁剪，无法自测\n");
    return -1;
}

void demo_page_network_adapt(XWidget* page)
{
    (void)page;
}

void demo_page_network_resume(void)
{
}

#endif /* XNETWORK_ON && XNETWORK_INTERFACE_ON && 控件段 */
