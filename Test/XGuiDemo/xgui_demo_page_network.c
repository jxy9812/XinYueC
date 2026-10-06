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
 *                平台不支持（非 Windows 后端）时应用按钮如实反馈。
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
#include "XLabel.h"
#include "XCheckBox.h"
#include "XLineEdit.h"
#include "XComboBox.h"
#include "XPushButton.h"
#include "XHostAddress.h"
#include "XNetworkInterface.h"
#include "XSystem.h" /* XSystem_environment: 环境变量唯一入口。 */
#include "xgui_demo_pages.h"

/*
 * 控件段宏守卫：枚举与配置依赖 XNetwork + XNetworkInterface 模块，
 * 页面控件依赖 Label/ComboBox/LineEdit/PushButton/CheckBox。任一被
 * 裁剪时降级为契约桩（build 返回 NULL、autotest 返回 -1），主文件可
 * 无条件链接。
 */
#if XNETWORK_ON && XNETWORK_INTERFACE_ON && XWIDGET_ON && \
    XLABEL_ON && XCOMBOBOX_ON && XLINEEDIT_ON && XPUSHBUTTON_ON && \
    XCHECKBOX_ON

/* ==================== 布局常量 ==================== */

/** @brief 页根装配基线（776x458 内容区设计稿；adapt 只钉底两行）。 */
#define NET_CAP_X      12     /**< 字段标题列 x。 */
#define NET_FIELD_X    96     /**< 输入控件列 x。 */
#define NET_FIELD_W    300    /**< 输入控件宽。 */
#define NET_ROW_H      32     /**< 配置字段行距。 */

/* ==================== 页面内部控件登记（demo 单实例 static 自持） ==== */

/** @brief 网络设置页内部登记表。 */
static struct
{
    XWidget*         m_root;        /**< 页面根控件（契约返回值；堆对象）。 */
    DemoPageStatusFn m_status;      /**< 主窗口状态栏反馈回调（借用）。 */
    void*            m_user;        /**< 回调上下文（主窗口指针，借用）。 */
    char             m_lastStatus[192]; /**< 最近一次反馈文本（自测断言用）。 */
    bool             m_ready;       /**< build 已完成且控件指针有效。 */

    /* ---- 网卡选择区 ---- */
    XLabel*    m_titleLabel;   /**< 页标题。 */
    XLabel*    m_capAdapter;   /**< 「网卡」标题。 */
    XComboBox* m_adapterCombo; /**< 网卡下拉框（枚举所有接口）。 */
    XPushButton* m_refreshBtn; /**< 刷新按钮。 */

    /* ---- 信息展示区 ---- */
    XLabel*    m_infoLabel;    /**< 多行配置信息区（模式/IPv4/DNS/DHCP/MAC）。 */
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

/** @brief IPv6 静态编辑框启停（右列三件 + 应用钮；随 IPv6 模式独立，
 *         与 IPv4 模式互不影响）。 */
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
        entries = XNetworkInterface_addressEntries(iface);
        for (j = 0; entries && j < XVector_size_base(entries); ++j) {
            XNetworkAddressEntry* entry =
                (XNetworkAddressEntry*)XVector_at_base(entries, j);
            XString* text;
            if (!entry || entry->ip.protocol != XHostAddress_IPv6Protocol)
                continue;
            text = XHostAddress_toString(&entry->ip);
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

/** @brief 接口对象回退信息（平台不支持配置查询时的降级展示）。 */
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
        char info[512];
        char ipv4[96];
        char ipv6[224];
        const XString* mac;
        if (!iface || XNetworkInterface_index(iface) != ifIndex) continue;
        found = true;
        mac = XNetworkInterface_hardwareAddress_const(iface);
        entries = XNetworkInterface_addressEntries(iface);
        snprintf(info, sizeof(info),
                 "\xE7\x8A\xB6\xE6\x80\x81: %s  "
                 "\xE7\xB1\xBB\xE5\x9E\x8B: %s\n",
                 /* 状态: / 类型: */
                 XNetworkInterface_isUp(iface)
                     ? "Up" : "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5",
                 net_typeName(XNetworkInterface_type(iface)));
        ipv4[0] = '\0';
        ipv6[0] = '\0';
        for (j = 0; entries && j < XVector_size_base(entries); ++j) {
            XNetworkAddressEntry* entry =
                (XNetworkAddressEntry*)XVector_at_base(entries, j);
            XString* ipText;
            if (!entry) continue;
            ipText = XHostAddress_toString(&entry->ip);
            if (!ipText) continue;
            if (entry->ip.protocol == XHostAddress_IPv6Protocol) {
                size_t len = strlen(ipv6);
                snprintf(ipv6 + len, sizeof(ipv6) - len, "%s%s",
                         (len > 0) ? "  " : "", XString_toUtf8(ipText));
            } else {
                size_t len = strlen(ipv4);
                snprintf(ipv4 + len, sizeof(ipv4) - len, "%s%s",
                         (len > 0) ? ", " : "", XString_toUtf8(ipText));
            }
            XClassDelete(ipText);
        }
        {
            size_t len = strlen(info);
            snprintf(info + len, sizeof(info) - len,
                     "IPv4: %s\nIPv6: %s\nMAC: %s",
                     ipv4[0] ? ipv4 : "-",
                     ipv6[0] ? ipv6 : "-",
                     (mac && XString_toUtf8(mac) && XString_toUtf8(mac)[0])
                         ? XString_toUtf8(mac) : "-");
        }
        if (s_net.m_infoLabel)
            XLabel_setText_2(s_net.m_infoLabel, info);
    }
    XClassDelete(interfaces);
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

/** @brief 按 v6 展开态重排页面（折叠=编辑视图；展开=全列表让位视图）。
 *  @details 展开时信息区/编辑区整体隐藏，全列表占内容区（每行一个地
 *           址）；窗口过矮按行高截断显示（计数仍完整可见于收起行）。 */
static void net_relayout(void)
{
    XWidget* root = s_net.m_root;
    int rootW;
    int rootH;
    int y;
    int i;
    if (!root) return;
    rootW = XWidget_width(root);
    rootH = XWidget_height(root);
    if (rootW < 360 || rootH < 300) return; /* 过窄保持装配几何。 */
    if (s_net.m_statusLabel)
        XWidget_setGeometry(s_net.m_statusLabel, 12, rootH - 22,
                            rootW - 24, 22);
    if (!s_net.m_v6Expanded && rootW >= 700) {
        /* ---- 折叠·宽版：信息区一行 v6 + 双列编辑区（IPv4 左 / IPv6 右） ---- */
        if (s_net.m_infoLabel) {
            XWidget_setGeometry(s_net.m_infoLabel, 12, 80, rootW - 24, 104);
            XWidget_show((XWidget*)s_net.m_infoLabel);
        }
        if (s_net.m_v6Label)
            XWidget_setGeometry(s_net.m_v6Label, 12, 188, rootW - 24, 20);
        if (s_net.m_v6ExpandCheck)
            XWidget_setGeometry((XWidget*)s_net.m_v6ExpandCheck,
                                rootW - 176, 186, 164, 22);
        /* 模式行：IPv4 左 / IPv6 右，两枚开关互不联动。 */
        if (s_net.m_capV4Mode)
            XWidget_setGeometry((XWidget*)s_net.m_capV4Mode, 12, 224, 80, 22);
        if (s_net.m_v4ModeCombo)
            XWidget_setGeometry((XWidget*)s_net.m_v4ModeCombo, 96, 222, 150, 26);
        if (s_net.m_applyBtn)
            XWidget_setGeometry((XWidget*)s_net.m_applyBtn, 254, 220, 110, 30);
        if (s_net.m_capV6Mode)
            XWidget_setGeometry((XWidget*)s_net.m_capV6Mode, 420, 224, 80, 22);
        if (s_net.m_v6ModeCombo)
            XWidget_setGeometry((XWidget*)s_net.m_v6ModeCombo, 504, 222, 140, 26);
        if (s_net.m_applyV6Btn)
            XWidget_setGeometry((XWidget*)s_net.m_applyV6Btn, 652, 220, 110, 30);
        /* 左列：IPv4 静态五件。 */
        y = 262;
        for (i = 0; i < 5; ++i) {
            XLabel** cap = &s_net.m_capIp;
            XLineEdit** edit = &s_net.m_ipEdit;
            if (i == 1) { cap = &s_net.m_capMask; edit = &s_net.m_maskEdit; }
            else if (i == 2) { cap = &s_net.m_capGw; edit = &s_net.m_gwEdit; }
            else if (i == 3) { cap = &s_net.m_capDns1; edit = &s_net.m_dns1Edit; }
            else if (i == 4) { cap = &s_net.m_capDns2; edit = &s_net.m_dns2Edit; }
            if (*cap)
                XWidget_setGeometry((XWidget*)*cap, 12, y + 2, 80, 20);
            if (*edit)
                XWidget_setGeometry((XWidget*)*edit, 96, y, 300, 22);
            y += 26;
        }
        /* 右列：IPv6 静态三件（地址 / 前缀 / 网关）。 */
        if (s_net.m_capV6Addr)
            XWidget_setGeometry((XWidget*)s_net.m_capV6Addr, 420, 264, 100, 20);
        if (s_net.m_v6AddrEdit)
            XWidget_setGeometry((XWidget*)s_net.m_v6AddrEdit, 524, 262, 264, 24);
        if (s_net.m_capV6Prefix)
            XWidget_setGeometry((XWidget*)s_net.m_capV6Prefix, 420, 294, 36, 20);
        if (s_net.m_v6PrefixEdit)
            XWidget_setGeometry((XWidget*)s_net.m_v6PrefixEdit, 460, 292, 64, 24);
        if (s_net.m_capV6Gw)
            XWidget_setGeometry((XWidget*)s_net.m_capV6Gw, 420, 324, 60, 20);
        if (s_net.m_v6GwEdit)
            XWidget_setGeometry((XWidget*)s_net.m_v6GwEdit, 484, 322, 304, 24);
        /* 双列整体显形（从展开态返回时恢复）。 */
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
        if (s_net.m_infoLabel) XWidget_show((XWidget*)s_net.m_infoLabel);
        if (s_net.m_hintLabel)
            XWidget_setGeometry(s_net.m_hintLabel, 12, rootH - 44,
                                rootW - 24, 20);
        net_updateV6Label();
    } else if (!s_net.m_v6Expanded) {
        /* ---- 折叠·窄版（rootW<700，如导航面板展开占位）：模式行堆叠，
         * IPv4/IPv6 字段纵排（前缀与网关同行），窄根不裁列。 ---- */
        if (s_net.m_infoLabel) {
            XWidget_setGeometry(s_net.m_infoLabel, 12, 80, rootW - 24, 104);
            XWidget_show((XWidget*)s_net.m_infoLabel);
        }
        if (s_net.m_v6Label)
            XWidget_setGeometry(s_net.m_v6Label, 12, 188, rootW - 24, 20);
        if (s_net.m_v6ExpandCheck)
            XWidget_setGeometry((XWidget*)s_net.m_v6ExpandCheck,
                                rootW - 176, 186, 164, 22);
        if (s_net.m_capV4Mode)
            XWidget_setGeometry((XWidget*)s_net.m_capV4Mode, 12, 224, 80, 22);
        if (s_net.m_v4ModeCombo)
            XWidget_setGeometry((XWidget*)s_net.m_v4ModeCombo, 96, 222, 130, 26);
        if (s_net.m_applyBtn)
            XWidget_setGeometry((XWidget*)s_net.m_applyBtn, 232, 220, 100, 30);
        if (s_net.m_capV6Mode)
            XWidget_setGeometry((XWidget*)s_net.m_capV6Mode, 12, 254, 80, 22);
        if (s_net.m_v6ModeCombo)
            XWidget_setGeometry((XWidget*)s_net.m_v6ModeCombo, 96, 252, 130, 26);
        if (s_net.m_applyV6Btn)
            XWidget_setGeometry((XWidget*)s_net.m_applyV6Btn, 232, 250, 100, 30);
        y = 292;
        for (i = 0; i < 5; ++i) {
            XLabel** cap = &s_net.m_capIp;
            XLineEdit** edit = &s_net.m_ipEdit;
            if (i == 1) { cap = &s_net.m_capMask; edit = &s_net.m_maskEdit; }
            else if (i == 2) { cap = &s_net.m_capGw; edit = &s_net.m_gwEdit; }
            else if (i == 3) { cap = &s_net.m_capDns1; edit = &s_net.m_dns1Edit; }
            else if (i == 4) { cap = &s_net.m_capDns2; edit = &s_net.m_dns2Edit; }
            if (*cap)
                XWidget_setGeometry((XWidget*)*cap, 12, y + 2, 80, 20);
            if (*edit)
                XWidget_setGeometry((XWidget*)*edit, 96, y, rootW - 120, 22);
            y += 24;
        }
        if (s_net.m_capV6Addr)
            XWidget_setGeometry((XWidget*)s_net.m_capV6Addr, 12, 400, 100, 20);
        if (s_net.m_v6AddrEdit)
            XWidget_setGeometry((XWidget*)s_net.m_v6AddrEdit, 116, 398,
                                rootW - 140, 24);
        if (s_net.m_capV6Prefix)
            XWidget_setGeometry((XWidget*)s_net.m_capV6Prefix, 12, 428, 36, 20);
        if (s_net.m_v6PrefixEdit)
            XWidget_setGeometry((XWidget*)s_net.m_v6PrefixEdit, 52, 426, 64, 24);
        if (s_net.m_capV6Gw)
            XWidget_setGeometry((XWidget*)s_net.m_capV6Gw, 140, 428, 60, 20);
        if (s_net.m_v6GwEdit)
            XWidget_setGeometry((XWidget*)s_net.m_v6GwEdit, 204, 426,
                                rootW - 228, 24);
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
        if (s_net.m_infoLabel) XWidget_show((XWidget*)s_net.m_infoLabel);
        if (s_net.m_hintLabel)
            XWidget_setGeometry(s_net.m_hintLabel, 12, rootH - 44,
                                rootW - 24, 20);
        net_updateV6Label();
    } else {
        /* ---- 展开：全列表让位视图（信息/编辑区隐藏） ---- */
        int lineH = 18;
        int maxLines = (rootH - 74 - 46 - 8) / lineH;
        int shown = s_net.m_v6Count > 0 ? s_net.m_v6Count : 1;
        char list[1500];
        if (shown > maxLines) shown = maxLines > 0 ? maxLines : 1;
        /* 列表文本按显示行数截断（逐行取 m_v6All 前缀）。 */
        {
            const char* p = s_net.m_v6All;
            int lines = 1;
            size_t len = 0;
            list[0] = '\0';
            if (s_net.m_v6Count <= 0) snprintf(list, sizeof(list), "-");
            else while (p && *p && lines <= shown) {
                const char* nl = strchr(p, '\n');
                size_t seg = nl ? (size_t)(nl - p) : strlen(p);
                if (sizeof(list) - len > seg + 2) {
                    snprintf(list + len, sizeof(list) - len, "%s%.*s",
                             (lines > 1) ? "\n" : "", (int)seg, p);
                    len = strlen(list);
                }
                if (!nl) break;
                p = nl + 1;
                ++lines;
            }
            if (s_net.m_v6Count > shown) {
                size_t len = strlen(list);
                snprintf(list + len, sizeof(list) - len, "\n\xE2\x80\xA6");
                /* …（窗口过矮截断提示） */
            }
        }
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
        if (s_net.m_hintLabel) XWidget_hide((XWidget*)s_net.m_hintLabel);
        if (s_net.m_v6Label) {
            XLabel_setTextPixelSize(s_net.m_v6Label, 12);
            XWidget_setGeometry(s_net.m_v6Label, 12, 74, rootW - 24,
                                shown * lineH + 8);
            XWidget_show(s_net.m_v6Label);
            XLabel_setText_2(s_net.m_v6Label, list);
        }
        if (s_net.m_v6ExpandCheck)
            XWidget_setGeometry((XWidget*)s_net.m_v6ExpandCheck,
                                12, 74 + shown * lineH + 12, 180, 24);
        return;
    }
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
    if (!XNetworkInterface_configSupported()) {
        net_report("\xE5\xBD\x93\xE5\x89\x8D\xE5\xB9\xB3\xE5\x8F\xB0\xE4\xB8"
                   "\x8D\xE6\x94\xAF\xE6\x8C\x81\xE7\xBD\x91\xE5\x8D\xA1"
                   "\xE9\x85\x8D\xE7\xBD\xAE", NULL); /* 当前平台不支持网卡配置 */
        return;
    }
    addr = s_net.m_v6AddrEdit ? XLineEdit_text(s_net.m_v6AddrEdit) : "";
    prefixText = s_net.m_v6PrefixEdit ? XLineEdit_text(s_net.m_v6PrefixEdit) : "";
    gw = s_net.m_v6GwEdit ? XLineEdit_text(s_net.m_v6GwEdit) : "";
    prefix = atoi(prefixText && prefixText[0] ? prefixText : "64");
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
    char mac[40];
    char v6addr[64];
    char v6gw[64];
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
        return;
    }
    net_addrText(&config->m_ipv4Address, ip, sizeof(ip));
    net_addrText(&config->m_ipv4Netmask, mask, sizeof(mask));
    net_addrText(&config->m_ipv4Gateway, gw, sizeof(gw));
    net_addrText(&config->m_dhcpServer, dhcp, sizeof(dhcp));
    net_addrText(&config->m_dnsPrimary, dns1, sizeof(dns1));
    net_addrText(&config->m_dnsSecondary, dns2, sizeof(dns2));
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
    XClassDelete(config);
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
        if (!iface || !iface->isValid) continue;
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
    if (!XNetworkInterface_configSupported()) {
        net_report("\xE5\xBD\x93\xE5\x89\x8D\xE5\xB9\xB3\xE5\x8F\xB0\xE4\xB8"
                   "\x8D\xE6\x94\xAF\xE6\x8C\x81\xE7\xBD\x91\xE5\x8D\xA1"
                   "\xE9\x85\x8D\xE7\xBD\xAE", NULL); /* 当前平台不支持网卡配置 */
        return;
    }
    mode = s_net.m_v4ModeCombo ? XComboBox_currentIndex(s_net.m_v4ModeCombo) : 0;
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

/** @brief 组装一行「标题 + 单行编辑」。 */
static void net_buildField(const char* caption, int y, XLabel** outCap,
                           XLineEdit** outEdit)
{
    if (outCap && outEdit) {
        XLabel* cap = XLabel_create(s_net.m_root, 0);
        XLineEdit* edit = XLineEdit_create(s_net.m_root, 0);
        if (cap) {
            XLabel_setText_2(cap, caption);
            XWidget_setGeometry((XWidget*)cap, NET_CAP_X, y + 2, 80, 22);
            XWidget_show((XWidget*)cap);
        }
        if (edit) {
            XWidget_setGeometry((XWidget*)edit, NET_FIELD_X, y,
                                NET_FIELD_W, 24);
            XWidget_show((XWidget*)edit);
        }
        *outCap = cap;
        *outEdit = edit;
    }
}

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
    s_net.m_root = XWidget_create(parent, 0);
    if (!s_net.m_root) return (XWidget*)0;

    /* ---- 标题 ---- */
    s_net.m_titleLabel = XLabel_create(s_net.m_root, 0);
    if (!s_net.m_titleLabel) return s_net.m_root;
    XLabel_setText_2(s_net.m_titleLabel,
                     "\xE7\xBD\x91\xE7\xBB\x9C\xE8\xAE\xBE\xE7\xBD\xAE");
                     /* 网络设置 */
    XLabel_setTextPixelSize(s_net.m_titleLabel, 15);
    XWidget_setGeometry((XWidget*)s_net.m_titleLabel, 12, 8, 300, 24);
    XWidget_show((XWidget*)s_net.m_titleLabel);

    /* ---- 网卡选择行 ---- */
    s_net.m_capAdapter = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capAdapter) {
        XLabel_setText_2(s_net.m_capAdapter,
                         "\xE7\xBD\x91\xE5\x8D\xA1"); /* 网卡 */
        XWidget_setGeometry((XWidget*)s_net.m_capAdapter, 12, 44, 40, 22);
        XWidget_show((XWidget*)s_net.m_capAdapter);
    }
    s_net.m_adapterCombo = XComboBox_create(s_net.m_root, 0);
    if (!s_net.m_adapterCombo) return s_net.m_root;
    XWidget_setGeometry((XWidget*)s_net.m_adapterCombo, 56, 42, 500, 26);
    XObject_connect_2((XObject*)s_net.m_adapterCombo,
                      XSignal(XComboBox_activated_signal),
                      net_adapterActivatedSlot);
    XWidget_show((XWidget*)s_net.m_adapterCombo);
    s_net.m_refreshBtn = XPushButton_create(s_net.m_root, 0);
    if (s_net.m_refreshBtn) {
        XPushButton_setText_2(s_net.m_refreshBtn,
                              "\xE5\x88\xB7\xE6\x96\xB0"); /* 刷新 */
        XWidget_setGeometry((XWidget*)s_net.m_refreshBtn, 568, 42, 64, 26);
        XObject_connect_2((XObject*)s_net.m_refreshBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          net_refreshSlot);
        XWidget_show((XWidget*)s_net.m_refreshBtn);
    }

    /* ---- 信息展示区（5 行：模式/IPv4/DNS/DHCP/MAC） ---- */
    s_net.m_infoLabel = XLabel_create(s_net.m_root, 0);
    if (!s_net.m_infoLabel) return s_net.m_root;
    XLabel_setText_2(s_net.m_infoLabel,
                     "\xE6\xAD\xA3\xE5\x9C\xA8\xE6\x9E\x9A\xE4\xB8\xBE"
                     "\xE7\xBD\x91\xE5\x8D\xA1\xE2\x80\xA6"); /* 正在枚举网卡… */
    XLabel_setTextPixelSize(s_net.m_infoLabel, 13);
    XLabel_setAlignment(s_net.m_infoLabel,
                        XAlignment_Left | XAlignment_Top);
    XWidget_setGeometry((XWidget*)s_net.m_infoLabel, 12, 80, 660, 104);
    XWidget_show((XWidget*)s_net.m_infoLabel);

    /* ---- IPv6 地址区（折叠一行/展开全列表）+ 展开开关 ---- */
    s_net.m_v6Label = XLabel_create(s_net.m_root, 0);
    if (!s_net.m_v6Label) return s_net.m_root;
    XLabel_setText_2(s_net.m_v6Label, "IPv6: -");
    XLabel_setTextPixelSize(s_net.m_v6Label, 12);
    XLabel_setAlignment(s_net.m_v6Label,
                        XAlignment_Left | XAlignment_Top);
    XWidget_setGeometry((XWidget*)s_net.m_v6Label, 12, 188, 660, 20);
    XWidget_show((XWidget*)s_net.m_v6Label);
    s_net.m_v6ExpandCheck = XCheckBox_create(s_net.m_root, 0);
    if (s_net.m_v6ExpandCheck) {
        XAbstractButton_setText_2((XAbstractButton*)s_net.m_v6ExpandCheck,
                                  "\xE5\xB1\x95\xE5\xBC\x80\xE5\x85\xA8"
                                  "\xE9\x83\xA8 IPv6"); /* 展开全部 IPv6 */
        XWidget_setGeometry((XWidget*)s_net.m_v6ExpandCheck, 488, 186,
                            164, 22);
        XObject_connect_2((XObject*)s_net.m_v6ExpandCheck,
                          XSignal(XAbstractButton_toggled_signal),
                          net_v6ExpandSlot);
        XWidget_show((XWidget*)s_net.m_v6ExpandCheck);
    }

    /* ---- IPv4 模式行（左列开关；DHCP 态下「应用配置」=切回自动） ---- */
    s_net.m_capV4Mode = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capV4Mode) {
        XLabel_setText_2(s_net.m_capV4Mode, "IPv4");
        XWidget_setGeometry((XWidget*)s_net.m_capV4Mode, 12, 224, 80, 22);
        XWidget_show((XWidget*)s_net.m_capV4Mode);
    }
    s_net.m_v4ModeCombo = XComboBox_create(s_net.m_root, 0);
    if (!s_net.m_v4ModeCombo) return s_net.m_root;
    XComboBox_addItem_2(s_net.m_v4ModeCombo,
                        "DHCP \xE8\x87\xAA\xE5\x8A\xA8\xE8\x8E\xB7"
                        "\xE5\x8F\x96"); /* DHCP 自动获取 */
    XComboBox_addItem_2(s_net.m_v4ModeCombo,
                        "\xE9\x9D\x99\xE6\x80\x81 IP"); /* 静态 IP */
    XComboBox_setCurrentIndex(s_net.m_v4ModeCombo, 0);
    XWidget_setGeometry((XWidget*)s_net.m_v4ModeCombo, 96, 222, 150, 26);
    XObject_connect_2((XObject*)s_net.m_v4ModeCombo,
                      XSignal(XComboBox_activated_signal),
                      net_v4ModeSlot);
    XWidget_show((XWidget*)s_net.m_v4ModeCombo);
    s_net.m_applyBtn = XPushButton_create(s_net.m_root, 0);
    if (s_net.m_applyBtn) {
        XPushButton_setText_2(s_net.m_applyBtn,
                              "\xE5\xBA\x94\xE7\x94\xA8\xE9\x85\x8D"
                              "\xE7\xBD\xAE"); /* 应用配置 */
        XWidget_setGeometry((XWidget*)s_net.m_applyBtn, 254, 220, 110, 30);
        XObject_connect_2((XObject*)s_net.m_applyBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          net_applySlot);
        XWidget_show((XWidget*)s_net.m_applyBtn);
    }

    /* ---- IPv6 模式行（右列开关；自动=RA/SLAAC 只读，手动=追加静态） ---- */
    s_net.m_capV6Mode = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capV6Mode) {
        XLabel_setText_2(s_net.m_capV6Mode, "IPv6");
        XWidget_setGeometry((XWidget*)s_net.m_capV6Mode, 420, 224, 80, 22);
        XWidget_show((XWidget*)s_net.m_capV6Mode);
    }
    s_net.m_v6ModeCombo = XComboBox_create(s_net.m_root, 0);
    if (!s_net.m_v6ModeCombo) return s_net.m_root;
    XComboBox_addItem_2(s_net.m_v6ModeCombo,
                        "\xE8\x87\xAA\xE5\x8A\xA8\xE8\x8E\xB7\xE5\x8F"
                        "\x96"); /* 自动获取 */
    XComboBox_addItem_2(s_net.m_v6ModeCombo,
                        "\xE6\x89\x8B\xE5\x8A\xA8\xE8\xBF\xBD\xE5\x8A"
                        "\xA0"); /* 手动追加 */
    XComboBox_setCurrentIndex(s_net.m_v6ModeCombo, 0);
    XWidget_setGeometry((XWidget*)s_net.m_v6ModeCombo, 504, 222, 140, 26);
    XObject_connect_2((XObject*)s_net.m_v6ModeCombo,
                      XSignal(XComboBox_activated_signal),
                      net_v6ModeSlot);
    XWidget_show((XWidget*)s_net.m_v6ModeCombo);
    s_net.m_applyV6Btn = XPushButton_create(s_net.m_root, 0);
    if (s_net.m_applyV6Btn) {
        XPushButton_setText_2(s_net.m_applyV6Btn,
                              "\xE5\xBA\x94\xE7\x94\xA8 IPv6"); /* 应用 IPv6 */
        XWidget_setGeometry((XWidget*)s_net.m_applyV6Btn, 652, 220, 110, 30);
        XObject_connect_2((XObject*)s_net.m_applyV6Btn,
                          XSignal(XAbstractButton_clicked_signal),
                          net_applyV6Slot);
        XWidget_show((XWidget*)s_net.m_applyV6Btn);
    }

    /* ---- 左列：IPv4 静态五件 ---- */
    net_buildField("IP", 262, &s_net.m_capIp, &s_net.m_ipEdit);
    net_buildField("\xE5\xAD\x90\xE7\xBD\x91\xE6\x8E\xA9\xE7\xA0\x81",
                   288, &s_net.m_capMask, &s_net.m_maskEdit); /* 子网掩码 */
    net_buildField("\xE9\xBB\x98\xE8\xAE\xA4\xE7\xBD\x91\xE5\x85\xB3",
                   314, &s_net.m_capGw, &s_net.m_gwEdit); /* 默认网关 */
    net_buildField("\xE9\xA6\x96\xE9\x80\x89 DNS", 340,
                   &s_net.m_capDns1, &s_net.m_dns1Edit); /* 首选 DNS */
    net_buildField("\xE5\xA4\x87\xE7\x94\xA8 DNS", 366,
                   &s_net.m_capDns2, &s_net.m_dns2Edit); /* 备用 DNS */
    net_setV4EditsEnabled(false); /* 初值来自选中网卡（初始 DHCP 态置灰）。 */

    /* ---- 右列：IPv6 静态三件（追加式；网关可空） ---- */
    s_net.m_capV6Addr = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capV6Addr) {
        XLabel_setText_2(s_net.m_capV6Addr,
                         "IPv6 \xE5\x9C\xB0\xE5\x9D\x80"); /* IPv6 地址 */
        XWidget_setGeometry((XWidget*)s_net.m_capV6Addr, 420, 264, 100, 20);
        XWidget_show((XWidget*)s_net.m_capV6Addr);
    }
    s_net.m_v6AddrEdit = XLineEdit_create(s_net.m_root, 0);
    if (!s_net.m_v6AddrEdit) return s_net.m_root;
    XWidget_setGeometry((XWidget*)s_net.m_v6AddrEdit, 524, 262, 264, 24);
    XWidget_show((XWidget*)s_net.m_v6AddrEdit);
    s_net.m_capV6Prefix = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capV6Prefix) {
        XLabel_setText_2(s_net.m_capV6Prefix,
                         "\xE5\x89\x8D\xE7\xBC\x80"); /* 前缀 */
        XWidget_setGeometry((XWidget*)s_net.m_capV6Prefix, 420, 294, 36, 20);
        XWidget_show((XWidget*)s_net.m_capV6Prefix);
    }
    s_net.m_v6PrefixEdit = XLineEdit_create(s_net.m_root, 0);
    if (s_net.m_v6PrefixEdit) {
        XLineEdit_setText(s_net.m_v6PrefixEdit, "64");
        XWidget_setGeometry((XWidget*)s_net.m_v6PrefixEdit, 460, 292, 64, 24);
        XWidget_show((XWidget*)s_net.m_v6PrefixEdit);
    }
    s_net.m_capV6Gw = XLabel_create(s_net.m_root, 0);
    if (s_net.m_capV6Gw) {
        XLabel_setText_2(s_net.m_capV6Gw,
                         "\xE7\xBD\x91\xE5\x85\xB3"); /* 网关 */
        XWidget_setGeometry((XWidget*)s_net.m_capV6Gw, 420, 324, 60, 20);
        XWidget_show((XWidget*)s_net.m_capV6Gw);
    }
    s_net.m_v6GwEdit = XLineEdit_create(s_net.m_root, 0);
    if (s_net.m_v6GwEdit) {
        XWidget_setGeometry((XWidget*)s_net.m_v6GwEdit, 484, 322, 304, 24);
        XWidget_show((XWidget*)s_net.m_v6GwEdit);
    }
    net_setV6EditsEnabled(false); /* IPv6 模式初始=自动获取（右列置灰）。 */

    /* ---- 提示 + 状态行（adapt 钉底） ---- */
    s_net.m_hintLabel = XLabel_create(s_net.m_root, 0);
    if (s_net.m_hintLabel) {
        XLabel_setText_2(s_net.m_hintLabel,
                         "\xE5\xBA\x94\xE7\x94\xA8\xE7\xBB\x8F\xE7\xB3\xBB"
                         "\xE7\xBB\x9F netsh \xE9\x80\x9A\xE8\xB7\xAF"
                         "\xEF\xBC\x88\xE9\x9C\x80\xE7\xAE\xA1\xE7\x90\x86"
                         "\xE5\x91\x98\xE6\x9D\x83\xE9\x99\x90\xEF\xBC\x89"
                         "\xEF\xBC\x9B\xE5\x88\x87\xE6\x8D\xA2\xE4\xBC\x9A"
                         "\xE7\x9F\xAD\xE6\x9A\x82\xE4\xB8\xAD\xE6\x96\xAD"
                         "\xE8\xAF\xA5\xE7\xBD\x91\xE5\x8D\xA1");
                         /* 应用经系统 netsh 通路（需管理员权限）；切换会短暂中断该网卡 */
        XLabel_setTextPixelSize(s_net.m_hintLabel, 12);
        XWidget_setGeometry((XWidget*)s_net.m_hintLabel, 12, 452, 660, 20);
        XWidget_show((XWidget*)s_net.m_hintLabel);
    }
    s_net.m_statusLabel = XLabel_create(s_net.m_root, 0);
    if (!s_net.m_statusLabel) return s_net.m_root;
    XLabel_setText_2(s_net.m_statusLabel,
                     "\xE5\xB0\xB1\xE7\xBB\xAA"); /* 就绪 */
    XWidget_setGeometry((XWidget*)s_net.m_statusLabel, 12, 474, 660, 22);
    XWidget_show((XWidget*)s_net.m_statusLabel);

    /* ---- 统一重排（折叠视图定版）+ 首轮枚举回填。 ---- */
    net_relayout();
    net_refreshAdapters();

    s_net.m_ready = true;
    net_page_network_headless_hook();
    return s_net.m_root;
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

void demo_page_network_adapt(XWidget* page)
{
    int rootW;
    int rootH;
    if (!page || page != s_net.m_root) return;
    rootW = XWidget_width(page);
    rootH = XWidget_height(page);
    if (rootW < 360 || rootH < 300) return; /* 过窄保持装配几何。 */
    /* 选择行随根宽收放（右缘留刷新钮位）；其余由 net_relayout 统一
     * 分配（折叠/展开双态各自的几何与显隐）。 */
    if (s_net.m_adapterCombo) {
        int comboW = rootW - 56 - 84;
        if (comboW < 200) comboW = 200;
        XWidget_setGeometry((XWidget*)s_net.m_adapterCombo, 56, 42,
                            comboW, 26);
    }
    if (s_net.m_refreshBtn)
        XWidget_setGeometry((XWidget*)s_net.m_refreshBtn, rootW - 72, 42,
                            64, 26);
    net_relayout();
}

/* ==================== autotest ==================== */

int demo_page_network_autotest(XWidget* page)
{
    int failures = 0;
#define NET_EXPECT(cond, what) \
    do { if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what); \
         else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; } } while (0)

    if (!page || !s_net.m_ready || page != s_net.m_root)
        return -1;

    /* ---- 控件登记完整 ---- */
    NET_EXPECT(s_net.m_adapterCombo && s_net.m_refreshBtn &&
               s_net.m_infoLabel && s_net.m_v4ModeCombo && s_net.m_applyBtn &&
               s_net.m_ipEdit && s_net.m_maskEdit && s_net.m_gwEdit &&
               s_net.m_dns1Edit && s_net.m_dns2Edit &&
               s_net.m_hintLabel && s_net.m_statusLabel,
               "网络设置页: 控件全部登记");

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
    NET_EXPECT(s_net.m_v6Count >= 1 &&
                   XWidget_height(s_net.m_v6Label) <= 30,
               "网络设置页: IPv6 折叠态单行显示");
    XAbstractButton_setChecked((XAbstractButton*)s_net.m_v6ExpandCheck,
                               true);
    net_v6ExpandSlot(NULL, NULL);
    NET_EXPECT(s_net.m_v6Expanded &&
                   XWidget_height(s_net.m_v6Label) > 30 &&
                   !XWidget_isVisible((XWidget*)s_net.m_v4ModeCombo),
               "网络设置页: 展开态全列表增高且编辑区让位隐藏");
    XAbstractButton_setChecked((XAbstractButton*)s_net.m_v6ExpandCheck,
                               false);
    net_v6ExpandSlot(NULL, NULL);
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

#endif /* XNETWORK_ON && XNETWORK_INTERFACE_ON && 控件段 */
