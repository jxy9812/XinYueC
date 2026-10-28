/**
 * @file       xgui_demo_page_atv61.c
 * @brief      XGuiDemo 扩展页 21：施耐德 ATV61 变频器（XVfd 抽象层 + Modbus RTU）。
 * @details    把 XModbusRtuSerialClient（总线主站）+ XVfdModbusLink 链路 +
 *             XVfdAtv61 品牌子类接入 GUI（2026-10-27 XVfd 子系统落地迁移，
 *             原 XModbusAtv61 直挂模式已删除）：
 *
 *   ① 通信配置行：串口名下拉（每次弹出重枚举本机串口）+ 波特率下拉
 *      （4800/9600/19200/38400，默认 38400）+ 从站地址 +【连接/断开】；
 *   ② 运行控制行：【正转启动】【反转启动】【受控停机】【自由停车】
 *      【快速停机】【故障复位】——启动走 CiA402 状态机；
 *   ③ 调速行：频率给定文本框（Hz）+【下发】；
 *   ④ 监视区：ETA 状态字、输出频率、给定、电流、转矩、电网电压、热态；
 *   ⑤ 报警区：故障码 + 字母码 + 中文说明；
 *   ⑥ 轮询由链路计划表统一排拍（页内无定时器）：轮询开关/周期直接映射
 *      设备轮询配置（XVfdDevice_setPollInterval），动作优先于轮询
 *      （轮询在途时动作自动挂起补发）。
 *
 *   线程口径：全部同线程直连（XConnectionType_Direct），槽内只触碰控件与
 *   设备对象，不做阻塞等待；Modbus 事务异步，结果经设备信号回填。
 *
 *   ⚠ 变频器侧前置条件（不改则面板连不上/不动作）：
 *      [1.9 通信] Add=1~247、tbr=38.4kbps、tFO=8E1；
 *      [1.6 命令] CHCF=SIM 时 Fr1=Mdb（SEP/IO 另需 Cd1=Mdb）。
 *      详见 docs/ATV61_485变频器面板改造与调试指南.md。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "XGuiConfig.h"
#include "XPrintf.h"
#include "xgui_demo_pages.h"
#include "XLabel.h"
#include "XLineEdit.h"
#include "XComboBox.h"
#include "XCheckBox.h"
#include "XPushButton.h"
#include "XSpinBox.h"
#include "XBoxLayout.h"
#include "XScrollArea.h"
#include "XAbstractScrollArea.h"
#include "XPerformanceOverlay.h" /* HUD 让位（adapt 收口）。 */
#include "XLayout.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XVariant.h"
#include "XVector.h"
#include "XContainer.h"
#include "XDeviceSerialPort.h"
#include "XSerialPortInfo.h"

#include "XModbus_config.h"
#include "XVfd_config.h"
#if XMODBUS_CORE_ON && XMODBUS_CLIENT_ON && XMODBUS_RTU_ON && \
    XVFD_ON && XVFD_DEVICE_ON && XVFD_LINK_ON && XVFD_ATV61_ON
#include "XModbusRtuSerialClient.h"
#include "XModbusDevice.h"
#include "XVfdDevice.h"
#include "XVfdDevice_Protected.h"
#include "XVfdModbusLink.h"
#include "XVfdAtv61.h"
#define ATV_PAGE_ENABLED (XLAYOUT_ON && XLAYOUT_BOX_ON && XWIDGET_ON && \
                         XLABEL_ON && XCOMBOBOX_ON && XLINEEDIT_ON && \
                         XPUSHBUTTON_ON && XCHECKBOX_ON && XSPINBOX_ON)
#else
#define ATV_PAGE_ENABLED 0
#endif

#if ATV_PAGE_ENABLED

/* ==================== 页面静态状态 ==================== */

typedef struct Atv61Page {
    DemoPageStatusFn m_status;   /**< 主窗口状态栏回调。 */
    void* m_user;                /**< 主窗口指针。 */
    XWidget* m_root;             /**< 页面根控件。 */
    XWidget*   m_scroll;         /**< 整页竖滚区（页宿主=build 返回值）。 */
    XBoxLayout m_rootLayout;     /**< 页根垂直布局。 */
    XBoxLayout m_rows[6];        /**< 行布局池（成员存储：栈上局部布局在
                                  * build 返回后成悬垂指针，页根重排时崩）。 */

    /* 通信配置 */
    XComboBox* m_port;           /**< 串口名下拉。 */
    XComboBox* m_baud;           /**< 波特率下拉。 */
    XSpinBox* m_unit;            /**< 从站地址（1~247）。 */
    XPushButton* m_connBtn;      /**< 连接/断开切换钮。 */
    XLabel* m_connState;         /**< 连接状态行。 */

    /* 运行控制 */
    XPushButton* m_runBtn;       /**< 正转启动。 */
    XPushButton* m_revBtn;       /**< 反转启动。 */
    XPushButton* m_stopBtn;      /**< 受控停机。 */
    XPushButton* m_freeBtn;      /**< 自由停车。 */
    XPushButton* m_qstopBtn;     /**< 快速停机。 */
    XPushButton* m_resetBtn;     /**< 故障复位。 */

    /* 调速与轮询 */
    XLineEdit* m_freqEdit;       /**< 频率给定（Hz 文本）。 */
    XPushButton* m_freqBtn;      /**< 下发频率。 */
    XCheckBox* m_pollChk;        /**< 轮询开关（映射设备轮询配置）。 */
    XSpinBox* m_pollMs;          /**< 轮询周期（ms）。 */

    /* 监视与报警 */
    XLabel* m_monitor;           /**< 监视区（多行）。 */
    XLabel* m_alarm;             /**< 报警区（多行）。 */
    XLabel* m_hint;              /**< 操作提示。 */

    /* 引擎 */
    XModbusRtuSerialClient* m_client; /**< RTU 主站客户端（总线主站）。 */
    XVfdModbusLink* m_link;      /**< Modbus 链路适配器。 */
    XVfdAtv61* m_drive;          /**< ATV61 品牌子类。 */
    XVfdDevice* m_dev;           /**< 设备基类视图（= &m_drive->m_base.m_base）。 */
    bool m_connected;            /**< 是否已连接。 */
} Atv61Page;

static Atv61Page s_atv = { 0 };

/* ==================== 辅助 ==================== */

/** @brief 往主窗口状态栏回一句。 */
static void atv_status(const char* text)
{
    if (s_atv.m_status) s_atv.m_status(s_atv.m_user, text);
}

/** @brief 建一个水平行盒（行盒为页状态成员，生命周期与页同寿）。 */
static void atv_row(XBoxLayout* parent, XBoxLayout* row, XBoxLayoutDirection dir)
{
    XBoxLayout_init(row, dir);
    XBoxLayout_addLayout(parent, (XLayout*)row);
}

/** @brief 行内加标签。 */
static XLabel* atv_addLabel(XBoxLayout* row, const char* text, int width)
{
    XLabel* label = XLabel_create((XWidget*)s_atv.m_root, 0);
    if (!label) return NULL;
    XLabel_setText_2(label, text);
    if (width > 0) XWidget_setFixedWidth((XWidget*)label, width);
    XBoxLayout_addWidget(row, (XWidget*)label);
    return label;
}

/** @brief 行内加按钮。 */
static XPushButton* atv_addButton(XBoxLayout* row, const char* text, int width)
{
    XPushButton* btn = XPushButton_create((XWidget*)s_atv.m_root, 0);
    if (!btn) return NULL;
    XPushButton_setText_2(btn, text);
    if (width > 0) XWidget_setFixedWidth((XWidget*)btn, width);
    XBoxLayout_addWidget(row, (XWidget*)btn);
    return btn;
}

/** @brief 当前轮询配置（开关+周期）换算成设备轮询周期。 */
static uint32_t atv_pollIntervalFromUi(void)
{
    int ms;
    if (!s_atv.m_pollChk || !XCheckBox_isChecked(s_atv.m_pollChk)) return 0;
    ms = s_atv.m_pollMs ? XSpinBox_value(s_atv.m_pollMs) : 500;
    if (ms < 100) ms = 100;
    return (uint32_t)ms;
}

/** @brief 刷新监视区与报警区显示。 */
static void atv_refreshMonitor(void)
{
    const XVfdStatus* st = s_atv.m_dev ? XVfdDevice_status_const(s_atv.m_dev) : NULL;
    char buf[512];
    if (!st || !st->m_valid) {
        if (s_atv.m_monitor) {
            XLabel_setText_2(s_atv.m_monitor, "监视：尚无数据（连接后链路自动轮询）");
            XWidget_update((XWidget*)s_atv.m_monitor);
        }
        return;
    }
    snprintf(buf, sizeof(buf),
             "监视：ETA=0x%04X  状态=%s%s\n"
             "输出频率 %.1f Hz    给定 %.1f Hz    电机 %.0f\n"
             "转矩 %.1f %%    电网 %.1f V    电机电压 %.0f V\n"
             "变频器热态 %.0f %%    电机热态 %.0f %%    功率 %.0f %%",
             st->m_rawStateWord,
             st->m_running ? "运行" : (st->m_faulted ? "故障" : "待机"),
             st->m_reverse ? "（反转）" : "",
             st->m_outputFreqHz, st->m_setFreqHz,
             st->m_outputCurrentA,
             st->m_torquePercent, st->m_mainsVoltageV, st->m_outputVoltageV,
             st->m_driveThermalPercent, st->m_motorThermalPercent,
             st->m_outputPowerPercent);
    if (s_atv.m_monitor) {
        XLabel_setText_2(s_atv.m_monitor, buf);
        XWidget_update((XWidget*)s_atv.m_monitor);
    }
}

static void atv_refreshAlarm(void)
{
    const XVfdStatus* st = s_atv.m_dev ? XVfdDevice_status_const(s_atv.m_dev) : NULL;
    char buf[256];
    const char* name;
    const char* text;
    uint16_t code = st ? st->m_faultCode : 0;
    name = XVfdAtv61_faultName(code);
    text = XVfdAtv61_faultText(code);
    if (!code) {
        snprintf(buf, sizeof(buf), "报警：无故障%s",
                 (st && st->m_warning) ? "（存在警告，请查看状态字 ETA bit7）" : "");
    } else {
        snprintf(buf, sizeof(buf), "报警：故障码 %u（%s）%s%s",
                 (unsigned)code,
                 name ? name : "未知",
                 text ? " — " : "",
                 text ? text : "");
    }
    if (s_atv.m_alarm) {
        XLabel_setText_2(s_atv.m_alarm, buf);
        XWidget_update((XWidget*)s_atv.m_alarm);
    }
}

/** @brief 刷新连接状态行。 */
static void atv_refreshConnState(void)
{
    char buf[160];
    XModbusDevice_State st = s_atv.m_client
        ? XModbusDevice_state((XModbusDevice*)s_atv.m_client)
        : XModbusDevice_UnconnectedState;
    snprintf(buf, sizeof(buf),
             "连接：%s    从站地址 %d    波特率 %s    %s",
             st == XModbusDevice_ConnectedState ? "已连接" : "未连接",
             s_atv.m_unit ? XSpinBox_value(s_atv.m_unit) : 1,
             s_atv.m_baud && XComboBox_currentText(s_atv.m_baud)
                 ? XString_toUtf8(XComboBox_currentText(s_atv.m_baud)) : "-",
             (s_atv.m_dev && XVfdDevice_isBusy(s_atv.m_dev)) ? "（事务中）" : "");
    if (s_atv.m_connState) {
        XLabel_setText_2(s_atv.m_connState, buf);
        XWidget_update((XWidget*)s_atv.m_connState);
    }
}

/* ==================== 槽 ==================== */

/** @brief 运行控制动作分派（按被点按钮句柄区分动作）。 */
static void atv_dispatch(XObject* sender);

/** @brief 设备状态更新 → 刷新监视/报警区。 */
static void atv_onStatusUpdated(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    atv_refreshMonitor();
    atv_refreshAlarm();
    atv_refreshConnState();
}

/** @brief 设备动作完成 → 状态栏回显。 */
static void atv_onCommandCompleted(XObject* receiver, XVarList* args)
{
    (void)receiver;
    int op = -1;
    bool ok = false;
    if (args) {
        XVarList_start(args);
        op = (int)XVarList_arg(args, XVfdOperation);
        ok = XVarList_arg(args, bool);
    }
    switch (op) {
    case XVfdOperation_SetFrequency: atv_status(ok ? "频率给定已下发" : "频率给定下发失败"); break;
    case XVfdOperation_SetSpeed:     atv_status(ok ? "转速给定已下发" : "转速给定下发失败"); break;
    case XVfdOperation_Start:        atv_status(ok ? "启动成功" : "启动失败"); break;
    case XVfdOperation_Stop:         atv_status(ok ? "已受控停机" : "停机失败"); break;
    case XVfdOperation_FreeStop:     atv_status(ok ? "已自由停车" : "自由停车失败"); break;
    case XVfdOperation_QuickStop:    atv_status(ok ? "已快速停机" : "快速停机失败"); break;
    case XVfdOperation_ResetFault:   atv_status(ok ? "故障复位已下发" : "故障复位失败"); break;
    default: break;
    }
    atv_refreshConnState();
}

/** @brief 设备错误 → 状态栏。 */
static void atv_onDriverError(XObject* receiver, XVarList* args)
{
    (void)receiver;
    const char* msg = "";
    if (args) {
        XVarList_start(args);
        msg = XVarList_arg(args, const char*);
    }
    atv_status(msg && msg[0] ? msg : "设备错误");
}

/** @brief 连接/断开按钮。 */
static void atv_onConnClicked(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_atv.m_client) return;
    XModbusDevice* dev = (XModbusDevice*)s_atv.m_client;
    if (s_atv.m_connected) {
        XVfdModbusBase_detach(&s_atv.m_drive->m_base); /* 注销链路轮询计划 */
        XModbusDevice_disconnectDevice(dev);
        s_atv.m_connected = false;
        if (s_atv.m_connBtn) XPushButton_setText_2(s_atv.m_connBtn, "连接");
        atv_status("已断开串口");
        atv_refreshConnState();
        return;
    }

    /* 组装连接参数（串口名/波特率/8E1）。 */
    const char* portName = "";
    if (s_atv.m_port) {
        XString* txt = XComboBox_currentText(s_atv.m_port);
        if (txt) portName = XString_toUtf8(txt);
    }
    if (!portName || !portName[0]) {
        atv_status("请先选择串口");
        return;
    }
    int baud = 38400;
    if (s_atv.m_baud) {
        XString* txt = XComboBox_currentText(s_atv.m_baud);
        if (txt) {
            const char* utf = XString_toUtf8(txt);
            if (utf && utf[0]) baud = atoi(utf);
        }
    }
    XVariant* var = XString_toVariant(XString_create_utf8(portName));
    if (var) {
        XModbusDevice_setConnectionParameter_move(dev,
            XModbusDevice_SerialPortNameParameter, var);
    }
    var = XVariant_create_int(baud);
    if (var) {
        XModbusDevice_setConnectionParameter_move(dev,
            XModbusDevice_SerialBaudRateParameter, var);
    }
    var = XVariant_create_int(XSerialPort_Data8);
    if (var) {
        XModbusDevice_setConnectionParameter_move(dev,
            XModbusDevice_SerialDataBitsParameter, var);
    }
    var = XVariant_create_int(XSerialPort_EvenParity);
    if (var) {
        XModbusDevice_setConnectionParameter_move(dev,
            XModbusDevice_SerialParityParameter, var);
    }
    var = XVariant_create_int(XSerialPort_OneStop);
    if (var) {
        XModbusDevice_setConnectionParameter_move(dev,
            XModbusDevice_SerialStopBitsParameter, var);
    }

    if (!XModbusDevice_connectDevice(dev)) {
        atv_status("串口打开失败（检查串口占用/设备存在）");
        atv_refreshConnState();
        return;
    }
    /* 串口已通：设备挂链路（登记轮询计划，周期取 UI 当前配置） */
    if (s_atv.m_drive && s_atv.m_link) {
        int unit = s_atv.m_unit ? XSpinBox_value(s_atv.m_unit) : 1;
        if (unit < 1 || unit > 247) unit = 1;
        XVfdDevice_setPollInterval(s_atv.m_dev, atv_pollIntervalFromUi());
        if (!XVfdModbusBase_setLink(&s_atv.m_drive->m_base, s_atv.m_link, unit)) {
            atv_status("设备挂链路失败");
            XModbusDevice_disconnectDevice(dev);
            atv_refreshConnState();
            return;
        }
    }
    s_atv.m_connected = true;
    if (s_atv.m_connBtn) XPushButton_setText_2(s_atv.m_connBtn, "断开");
    atv_status("串口已连接（链路轮询已启动）");
    atv_refreshConnState();
}

/** @brief 下发频率给定。 */
static void atv_onFreqClicked(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_atv.m_dev) return;
    if (!s_atv.m_connected) { atv_status("未连接，无法下发"); return; }
    double hz = 0.0;
    if (s_atv.m_freqEdit) {
        const char* txt = XLineEdit_text(s_atv.m_freqEdit);
        if (txt && txt[0]) hz = atof(txt);
    }
    if (!XVfdDevice_setFrequency(s_atv.m_dev, hz)) {
        atv_status("频率给定下发失败（总线忙或未连接）");
        return;
    }
}

/** @brief 运行控制按钮组（connect_2：第一参=真 sender=被点按钮）。 */
static void atv_onRunClicked(XObject* sender, XVarList* args)
{
    (void)args;
    atv_dispatch(sender);
}
/* 动作分派：用按钮句柄比对（页内单实例，句柄唯一）。 */
static void atv_dispatch(XObject* sender)
{
    if (!s_atv.m_dev || !s_atv.m_connected) {
        atv_status("未连接，无法操作");
        return;
    }
    if (sender == (XObject*)s_atv.m_runBtn) {
        XVfdDevice_start(s_atv.m_dev, false);
    } else if (sender == (XObject*)s_atv.m_revBtn) {
        XVfdDevice_start(s_atv.m_dev, true);
    } else if (sender == (XObject*)s_atv.m_stopBtn) {
        XVfdDevice_stop(s_atv.m_dev);
    } else if (sender == (XObject*)s_atv.m_freeBtn) {
        XVfdDevice_freeStop(s_atv.m_dev);
    } else if (sender == (XObject*)s_atv.m_qstopBtn) {
        XVfdDevice_quickStop(s_atv.m_dev);
    } else if (sender == (XObject*)s_atv.m_resetBtn) {
        XVfdDevice_resetFault(s_atv.m_dev);
    }
}

/** @brief 轮询开关切换 → 映射设备轮询配置（链路计划表即时生效）。 */
static void atv_onPollToggled(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_atv.m_dev || !s_atv.m_connected) return;
    XVfdDevice_setPollInterval(s_atv.m_dev, atv_pollIntervalFromUi());
}

/** @brief 轮询周期变更 → 映射设备轮询配置。 */
static void atv_onPollMsChanged(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_atv.m_dev || !s_atv.m_connected) return;
    XVfdDevice_setPollInterval(s_atv.m_dev, atv_pollIntervalFromUi());
}

/** @brief 重枚举串口下拉（保留原选择）。 */
static void atv_refreshPorts(void)
{
    if (!s_atv.m_port) return;
    XString* keep = XComboBox_currentText(s_atv.m_port);
    const char* keepText = keep ? XString_toUtf8(keep) : NULL;
    char keepBuf[128];
    keepBuf[0] = 0;
    if (keepText) snprintf(keepBuf, sizeof(keepBuf), "%s", keepText);
    if (keep) XClassDelete(keep);

    XSerialPortInfoList* list = XSerialPortInfo_availablePorts();
    if (!list) return;                     /* 枚举失败：保留既有条目 */
    XComboBox_clear(s_atv.m_port);
    size_t count = XVector_size_base((const XContainer*)list);
    size_t i;
    for (i = 0; i < count; ++i) {
        XSerialPortInfo** slot =
            (XSerialPortInfo**)XVector_at_base(list, (int64_t)i);
        XSerialPortInfo* info = slot ? *slot : NULL;
        if (!info) continue;
        const XString* name = XSerialPortInfo_portName(info);
        if (!name) continue;
        const char* utf = XString_toUtf8(name);
        if (utf && utf[0]) XComboBox_addItem_2(s_atv.m_port, utf);
    }
    if (keepBuf[0]) {
        int idx = XComboBox_findText_2(s_atv.m_port, keepBuf);
        if (idx >= 0) XComboBox_setCurrentIndex(s_atv.m_port, idx);
    }
    if (XComboBox_count(s_atv.m_port) == 0) {
        /* 枚举为空：按平台给个默认串口名兜底 */
#if defined(_WIN32)
        XComboBox_addItem_2(s_atv.m_port, "COM1");
#else
        XComboBox_addItem_2(s_atv.m_port, "/dev/ttyS1");
#endif
    }
}

/* ---- 整页竖滚壳（2026-10-27 视觉走查立项②） ----
 * 窄档翻竖排后总高远超视口（rows 竖排+说明行换行），底部说明文字被
 * 页底缘裁断且无滚动条（真机/桌面截图实证）。套「页宿主=scroll、内容
 * 根宽恒跟随视口、高=内容首选高」壳（views 页 ViewsScroll 同款已验证
 * 模式）：resizeEvent → fit（定内容宽/高 + setContentSize 驱动竖滚范
 * 围）。本页 rows 池固定无双档互移，无 views 页的装配交互问题。 */
XCLASS_DEFINE_BEGING(AtvScroll)
XCLASS_DEFINE_EXTEND_END(AtvScroll, XScrollArea)

typedef struct AtvScroll
{
    XScrollArea m_base; /**< 基类成员；必须是第一个。 */
} AtvScroll;

static void atv_fitScrollContent(XScrollArea* area);

static void VAtvScroll_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    XClass_Parent(XScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    atv_fitScrollContent((XScrollArea*)self);
}

XVtable* AtvScroll_class_init(void)
{
    XVTABLE_INIT_DEFAULT(AtvScroll)
    XVTABLE_INHERIT_XCLASS(XScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VAtvScroll_resizeEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 内容根定尺收口：宽=视口宽（水平恒不滚），高=布局合计首选/最
 *         小取大（定尺寸条目不被裁）。 */
static void atv_fitScrollContent(XScrollArea* area)
{
    XWidget* viewport;
    XWidget* content;
    int vw;
    int vh;
    int pass;
    if (!area) return;
    viewport = XAbstractScrollArea_viewport(area);
    content = XScrollArea_widget(area);
    if (!viewport || !content) return;
    vw = XWidget_width(viewport);
    vh = XWidget_height(viewport);
    if (vw < 1) return;   /* 视口未就位（堆叠未给几何）不猜。 */
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
    XAbstractScrollArea_setContentSize(area, vw,
        XWidget_height(content) > vh ? XWidget_height(content) : vh);
}

/* ==================== 构建 ==================== */

XWidget* demo_page_atv61_build(XWidget* parent,
                               DemoPageStatusFn status, void* user)
{
    XWidget* root;
    XBoxLayout* row;
    int i;
    int rowIdx = 0;

    if (!parent) return NULL;
    memset(&s_atv, 0, sizeof(s_atv));
    {
        /* [整页竖滚 2026-10-27 立项②] 页宿主=scroll（返回值），内容根=
         * 原页面根：窄档翻竖排后总高远超视口，由竖滚条承担（底部说明
         * 不再被页底缘裁断）。AtvScroll 壳 resizeEvent → fit 收口。 */
        AtvScroll* scroll =
            (AtvScroll*)XMemory_malloc(sizeof(AtvScroll),
                                       XCLASS_DEFAULT_MEMORY_TYPE);
        if (!scroll) return NULL;
        XMemset(scroll, 0, sizeof(*scroll));
        XScrollArea_init(&scroll->m_base, parent, 0);
        XClassSetVtable(scroll, AtvScroll);
        Set_Class_Memory(scroll, XCLASS_DEFAULT_MEMORY_TYPE);
        Set_Class_IsHeap(scroll, true);
        XAbstractScrollArea_setVerticalScrollBarPolicy(
            (XAbstractScrollArea*)scroll, XScrollBarPolicy_AsNeeded);
        XAbstractScrollArea_setHorizontalScrollBarPolicy(
            (XAbstractScrollArea*)scroll, XScrollBarPolicy_AlwaysOff);
        s_atv.m_scroll = (XWidget*)scroll;
        parent = (XWidget*)scroll; /* 内容根改挂 scroll（下句 create 用）。 */
    }
    root = XWidget_create(parent, 0);
    if (!root) return NULL;
    XScrollArea_setWidget((XScrollArea*)s_atv.m_scroll, root); /* 收编内容件。 */
    s_atv.m_root = root;
    s_atv.m_status = status;
    s_atv.m_user = user;

    XBoxLayout_init(&s_atv.m_rootLayout, XBoxLayoutDirection_TopToBottom);
    XWidget_setLayout(root, (XLayout*)&s_atv.m_rootLayout);

    /* ---- 标题 ---- */
    {
        XLabel* title = XLabel_create(root, 0);
        if (title) {
            XLabel_setText_2(title, "ATV61 变频器（XVfd 抽象层 + Modbus RTU）");
            XBoxLayout_addWidget(&s_atv.m_rootLayout, (XWidget*)title);
        }
    }

    /* ---- 通信配置行 ---- */
    row = &s_atv.m_rows[rowIdx++];
    atv_row(&s_atv.m_rootLayout, row, XBoxLayoutDirection_LeftToRight);
    atv_addLabel(row, "串口", 48);
    s_atv.m_port = XComboBox_create(root, 0);
    if (s_atv.m_port) {
        XWidget_setFixedWidth((XWidget*)s_atv.m_port, 160);
        XBoxLayout_addWidget(row, (XWidget*)s_atv.m_port);
    }
    atv_addLabel(row, "波特率", 56);
    s_atv.m_baud = XComboBox_create(root, 0);
    if (s_atv.m_baud) {
        static const char* bauds[4] = { "4800", "9600", "19200", "38400" };
        for (i = 0; i < 4; ++i) XComboBox_addItem_2(s_atv.m_baud, bauds[i]);
        XComboBox_setCurrentIndex(s_atv.m_baud, 3);   /* 38400 = ATV61 最高档 */
        XWidget_setFixedWidth((XWidget*)s_atv.m_baud, 90);
        XBoxLayout_addWidget(row, (XWidget*)s_atv.m_baud);
    }
    atv_addLabel(row, "站号", 40);
    s_atv.m_unit = XSpinBox_create(root, 0);
    if (s_atv.m_unit) {
        XSpinBox_setRange(s_atv.m_unit, 1, 247);
        XSpinBox_setValue(s_atv.m_unit, 1);
        XWidget_setFixedWidth((XWidget*)s_atv.m_unit, 70);
        XBoxLayout_addWidget(row, (XWidget*)s_atv.m_unit);
    }
    s_atv.m_connBtn = atv_addButton(row, "连接", 80);
    atv_refreshPorts();

    /* ---- 连接状态行 ---- */
    s_atv.m_connState = XLabel_create(root, 0);
    if (s_atv.m_connState) {
        XBoxLayout_addWidget(&s_atv.m_rootLayout, (XWidget*)s_atv.m_connState);
    }

    /* ---- 运行控制行 ---- */
    row = &s_atv.m_rows[rowIdx++];
    atv_row(&s_atv.m_rootLayout, row, XBoxLayoutDirection_LeftToRight);
    s_atv.m_runBtn = atv_addButton(row, "正转启动", 88);
    s_atv.m_revBtn = atv_addButton(row, "反转启动", 88);
    s_atv.m_stopBtn = atv_addButton(row, "受控停机", 88);
    s_atv.m_freeBtn = atv_addButton(row, "自由停车", 88);
    s_atv.m_qstopBtn = atv_addButton(row, "快速停机", 88);
    s_atv.m_resetBtn = atv_addButton(row, "故障复位", 88);

    /* ---- 调速行 ---- */
    row = &s_atv.m_rows[rowIdx++];
    atv_row(&s_atv.m_rootLayout, row, XBoxLayoutDirection_LeftToRight);
    atv_addLabel(row, "频率给定(Hz)", 128);
    s_atv.m_freqEdit = XLineEdit_create(root, 0);
    if (s_atv.m_freqEdit) {
        XLineEdit_setText(s_atv.m_freqEdit, "50.0");
        XWidget_setFixedWidth((XWidget*)s_atv.m_freqEdit, 100);
        XBoxLayout_addWidget(row, (XWidget*)s_atv.m_freqEdit);
    }
    s_atv.m_freqBtn = atv_addButton(row, "下发频率", 88);
    atv_addLabel(row, "轮询", 40);
    s_atv.m_pollChk = XCheckBox_create(root, 0);
    if (s_atv.m_pollChk) {
        XAbstractButton_setText_2((XAbstractButton*)s_atv.m_pollChk, "自动");
        XAbstractButton_setChecked((XAbstractButton*)s_atv.m_pollChk, true);
        XBoxLayout_addWidget(row, (XWidget*)s_atv.m_pollChk);
    }
    s_atv.m_pollMs = XSpinBox_create(root, 0);
    if (s_atv.m_pollMs) {
        XSpinBox_setRange(s_atv.m_pollMs, 100, 10000);
        XSpinBox_setValue(s_atv.m_pollMs, 500);
        XWidget_setFixedWidth((XWidget*)s_atv.m_pollMs, 80);
        XBoxLayout_addWidget(row, (XWidget*)s_atv.m_pollMs);
    }
    atv_addLabel(row, "ms", 30);

    /* ---- 监视区 ---- */
    s_atv.m_monitor = XLabel_create(root, 0);
    if (s_atv.m_monitor) {
        XLabel_setText_2(s_atv.m_monitor, "监视：尚无数据");
        XBoxLayout_addWidget(&s_atv.m_rootLayout, (XWidget*)s_atv.m_monitor);
    }

    /* ---- 报警区 ---- */
    s_atv.m_alarm = XLabel_create(root, 0);
    if (s_atv.m_alarm) {
        XLabel_setText_2(s_atv.m_alarm, "报警：无故障");
        XBoxLayout_addWidget(&s_atv.m_rootLayout, (XWidget*)s_atv.m_alarm);
    }

    /* ---- 提示 ---- */
    s_atv.m_hint = XLabel_create(root, 0);
    if (s_atv.m_hint) {
        XLabel_setText_2(s_atv.m_hint,
            "变频器侧前置：[1.9 通信] Add=1~247 / tbr=38.4k / tFO=8E1；"
            "[1.6 命令] CHCF=SIM 时 Fr1=Mdb。轮询由链路计划表统一排拍。");
        XBoxLayout_addWidget(&s_atv.m_rootLayout, (XWidget*)s_atv.m_hint);
    }

    /* ---- 引擎对象（父=页根，随父子链级联析构） ---- */
    s_atv.m_client = XModbusRtuSerialClient_create();
    if (s_atv.m_client) {
        XModbusClient_setTimeout((XModbusClient*)s_atv.m_client, 800);
        XModbusClient_setNumberOfRetries((XModbusClient*)s_atv.m_client, 2);
        XModbusClient_setAutoReconnect((XModbusClient*)s_atv.m_client, true);
        XModbusClient_setReconnectInterval((XModbusClient*)s_atv.m_client, 2000);
        XObject_setParent((XObject*)s_atv.m_client, (XObject*)root);
    }
    s_atv.m_link = XVfdModbusLink_create();
    if (s_atv.m_link) {
        XVfdModbusLink_setClient(s_atv.m_link, (XModbusClient*)s_atv.m_client);
        XVfdLink_setTickMs(&s_atv.m_link->m_base, 50);
        XObject_setParent((XObject*)s_atv.m_link, (XObject*)root);
    }
    s_atv.m_drive = XVfdAtv61_create();
    if (s_atv.m_drive) {
        s_atv.m_dev = &s_atv.m_drive->m_base.m_base;
        XObject_setParent((XObject*)s_atv.m_drive, (XObject*)root);
        XObject_connect_1((XObject*)s_atv.m_drive,
                          XSignal(XVfdDevice_statusUpdated_signal),
                          (XObject*)root, atv_onStatusUpdated,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)s_atv.m_drive,
                          XSignal(XVfdDevice_commandCompleted_signal),
                          (XObject*)root, atv_onCommandCompleted,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)s_atv.m_drive,
                          XSignal(XVfdDevice_errorOccurred_signal),
                          (XObject*)root, atv_onDriverError,
                          XConnectionType_Direct);
    }

    /* ---- 信号接线 ---- */
    if (s_atv.m_connBtn) {
        XObject_connect_1((XObject*)s_atv.m_connBtn,
                          (size_t)XPushButton_clicked_signal(NULL, false),
                          (XObject*)root, atv_onConnClicked,
                          XConnectionType_Direct);
    }
    if (s_atv.m_freqBtn) {
        XObject_connect_1((XObject*)s_atv.m_freqBtn,
                          (size_t)XPushButton_clicked_signal(NULL, false),
                          (XObject*)root, atv_onFreqClicked,
                          XConnectionType_Direct);
    }
    {
        XPushButton* btns[6];
        int n;
        btns[0] = s_atv.m_runBtn;
        btns[1] = s_atv.m_revBtn;
        btns[2] = s_atv.m_stopBtn;
        btns[3] = s_atv.m_freeBtn;
        btns[4] = s_atv.m_qstopBtn;
        btns[5] = s_atv.m_resetBtn;
        for (n = 0; n < 6; ++n) {
            if (!btns[n]) continue;
            /* connect_2：槽第一参=真 sender（被点按钮），据此分派动作。 */
            XObject_connect_2((XObject*)btns[n],
                              (size_t)XPushButton_clicked_signal(NULL, false),
                              atv_onRunClicked);
        }
    }
    if (s_atv.m_pollChk) {
        XObject_connect_1((XObject*)s_atv.m_pollChk,
                          (size_t)XAbstractButton_toggled_signal(NULL, false),
                          (XObject*)root, atv_onPollToggled,
                          XConnectionType_Direct);
    }
    if (s_atv.m_pollMs) {
        XObject_connect_1((XObject*)s_atv.m_pollMs,
                          XSignal(XSpinBox_valueChanged_signal),
                          (XObject*)root, atv_onPollMsChanged,
                          XConnectionType_Direct);
    }

    atv_refreshConnState();
    atv_refreshMonitor();
    atv_refreshAlarm();
    return s_atv.m_scroll; /* 页宿主=滚动区（fit 由其 resizeEvent 收口）。 */
}

/* ==================== 自测 ==================== */

int demo_page_atv61_autotest(XWidget* page)
{
    int fail = 0;
    /* [整页竖滚] build 返回页宿主 scroll（与调度槽位同源），守卫比对
     * 口径随之从内容根 m_root 换成 m_scroll，否则自测恒 -1。 */
    if (!page || page != s_atv.m_scroll) return -1;

    if (!s_atv.m_port) { printf("XGuiAutoTest: [FAIL] ATV61 串口下拉缺失\n"); ++fail; }
    else printf("XGuiAutoTest: [PASS] ATV61 串口下拉存在\n");

    if (!s_atv.m_baud) { printf("XGuiAutoTest: [FAIL] ATV61 波特率下拉缺失\n"); ++fail; }
    else {
        XString* txt = XComboBox_currentText(s_atv.m_baud);
        const char* utf = txt ? XString_toUtf8(txt) : NULL;
        if (!utf || strcmp(utf, "38400") != 0) {
            printf("XGuiAutoTest: [FAIL] ATV61 默认波特率应为 38400（ATV61 最高档）\n");
            ++fail;
        } else {
            printf("XGuiAutoTest: [PASS] ATV61 默认波特率 38400\n");
        }
        if (txt) XClassDelete(txt);
    }

    if (!s_atv.m_drive || !s_atv.m_link || !s_atv.m_dev) {
        printf("XGuiAutoTest: [FAIL] ATV61 引擎缺失（设备/链路）\n");
        ++fail;
    } else {
        printf("XGuiAutoTest: [PASS] ATV61 引擎已建（XVfdAtv61 + XVfdModbusLink）\n");
        if (!XVfdDevice_hasCapability(s_atv.m_dev, XVfdCap_Reverse)) {
            printf("XGuiAutoTest: [FAIL] 能力位图缺反转位\n");
            ++fail;
        } else {
            printf("XGuiAutoTest: [PASS] 能力位图（反转位）\n");
        }
    }

    if (s_atv.m_connected) {
        printf("XGuiAutoTest: [FAIL] ATV61 构建后不应处于已连接态\n");
        ++fail;
    } else {
        printf("XGuiAutoTest: [PASS] ATV61 构建后未连接\n");
    }

    if (XVfdAtv61_faultName(9) == NULL ||
        strcmp(XVfdAtv61_faultName(9), "OCF") != 0) {
        printf("XGuiAutoTest: [FAIL] 故障码表映射异常（9 应为 OCF）\n");
        ++fail;
    } else {
        printf("XGuiAutoTest: [PASS] 故障码表映射（9=OCF）\n");
    }

    printf("XGuiAutoTest: ATV61 面板自测失败 %d 项\n", fail);
    return fail;
}

/* ==================== 自适应 ==================== */

void demo_page_atv61_adapt(XWidget* page)
{
    /* [窄档翻竖排 2026-10-27 视觉走查 P1] 三行横排定宽控件合计：
     * 通信行 48+160+56+90+40+70+80=544、控制行 88*6=528、调速行
     * 128+100+88+40+80+30=466+checkbox——400 级竖屏行内宽 ~364 全部
     * 溢出右缘。按 window 页先例 setDirection 整行翻竖排（幂等）。
     * 宽档翻回横排复原桌面装配，resize 跨档来回不残留。 */
    bool narrow;
    if (!page || (page != s_atv.m_root && page != s_atv.m_scroll)) return;
    narrow = XWidget_width(page) > 0 && XWidget_width(page) < 600;
    XBoxLayout_setDirection(&s_atv.m_rows[0],
        narrow ? XBoxLayoutDirection_TopToBottom
               : XBoxLayoutDirection_LeftToRight);
    XBoxLayout_setDirection(&s_atv.m_rows[1],
        narrow ? XBoxLayoutDirection_TopToBottom
               : XBoxLayoutDirection_LeftToRight);
    XBoxLayout_setDirection(&s_atv.m_rows[2],
        narrow ? XBoxLayoutDirection_TopToBottom
               : XBoxLayoutDirection_LeftToRight);
    /* 翻排改变内容高：scroll 壳主动收口一次（resizeEvent 只盖窗口
     * 尺寸变化路径）。 */
    if (s_atv.m_scroll)
        atv_fitScrollContent((XScrollArea*)s_atv.m_scroll);
    /* [HUD 让位 2026-10-27 视觉走查 P2-8] 底部说明行与右下锚定的 FPS
     * 悬浮层同落页底带，说明行尾部被遮（横屏截图实证）：hint 最大宽
     * 按悬浮层几何收窄（window 页 adapt 同款先例）；悬浮层隐藏/未解算
     * 时先不让位，下一轮 adapt 补上。 */
#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON
    if (s_atv.m_hint) {
        int wrapW = XWidget_width(page) - 24;
        XPerformanceOverlay* overlay =
            (XPerformanceOverlay*)demo_main_overlay(s_atv.m_user);
        if (overlay && XWidget_isVisible((XWidget*)overlay)) {
            XRect og = XPerformanceOverlay_geometry(overlay);
            if (og.width > 0 && og.width < wrapW)
                wrapW -= og.width + 12;
        }
        if (wrapW < 160) wrapW = 160;
        XLabel_setWordWrap(s_atv.m_hint, true);
        XWidget_setMaximumWidth((XWidget*)s_atv.m_hint, wrapW);
    }
#endif
}

#else /* !ATV_PAGE_ENABLED */

/* 裁剪契约桩：控件或 XVfd/Modbus 模块被裁掉时，主文件跳过本页注册。 */
XWidget* demo_page_atv61_build(XWidget* parent, DemoPageStatusFn status, void* user)
{
    (void)parent; (void)status; (void)user;
    return NULL;
}
int demo_page_atv61_autotest(XWidget* page)
{
    (void)page;
    return -1;
}
void demo_page_atv61_adapt(XWidget* page)
{
    (void)page;
}

#endif /* ATV_PAGE_ENABLED */
