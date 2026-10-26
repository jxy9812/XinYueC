/**
 * @file       xgui_demo_page_atv61.c
 * @brief      XGuiDemo 扩展页 21：施耐德 ATV61 变频器（Modbus RTU 主站面板）。
 * @details    把 XModbusRtuSerialClient + XModbusAtv61 驱动接入 GUI：
 *
 *   ① 通信配置行：串口名下拉（每次弹出重枚举本机串口）+ 波特率下拉
 *      （4800/9600/19200/38400，默认 38400）+ 从站地址 +【连接/断开】；
 *   ② 运行控制行：【正转启动】【反转启动】【受控停机】【自由停车】
 *      【快速停机】【故障复位】——启动走 CiA402 状态机
 *      （Shutdown→Switch on→Enable operation），不是"写 0x000F 就完事"；
 *   ③ 调速行：频率给定文本框（Hz，0.1 精度，ATV61 用 0.1Hz 定点）+【下发】；
 *   ④ 监视区：ETA 状态字、输出频率、电机电流、转矩、电网电压、变频器
 *      热态、电机热态、电机功率；
 *   ⑤ 报警区：故障码 + 故障缩写（OCF/OLF/SLF1…）+ 中文说明 + 复位提示；
 *   ⑥ 自动轮询（周期可调，默认 500ms）。
 *
 *   线程口径：全部同线程直连（XConnectionType_Direct），槽内只触碰控件与
 *   驱动对象，不做阻塞等待；Modbus 事务异步，结果经驱动信号回填。
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
#include "XLayout.h"
#include "XTimer.h"
#include "XString.h"
#include "XStringUtils.h"
#include "XVariant.h"
#include "XVector.h"
#include "XContainer.h"
#include "XDeviceSerialPort.h"
#include "XSerialPortInfo.h"

#include "XModbus_config.h"
#if XMODBUS_CORE_ON && XMODBUS_CLIENT_ON && XMODBUS_RTU_ON && XMODBUS_ATV61_ON
#include "XModbusRtuSerialClient.h"
#include "XModbusDevice.h"
#include "XModbusAtv61.h"
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
    XCheckBox* m_pollChk;        /**< 自动轮询开关。 */
    XSpinBox* m_pollMs;          /**< 轮询周期（ms）。 */

    /* 监视与报警 */
    XLabel* m_monitor;           /**< 监视区（多行）。 */
    XLabel* m_alarm;             /**< 报警区（多行）。 */
    XLabel* m_hint;              /**< 操作提示。 */

    /* 引擎 */
    XModbusRtuSerialClient* m_client; /**< RTU 主站客户端。 */
    XModbusAtv61* m_drive;       /**< ATV61 驱动。 */
    XTimer* m_pollTimer;         /**< 轮询定时器（父=页根）。 */
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

/** @brief 刷新监视区与报警区显示。 */
static void atv_refreshMonitor(void)
{
    const XModbusAtv61Status* st = s_atv.m_drive
        ? XModbusAtv61_status_const(s_atv.m_drive) : NULL;
    char buf[512];
    if (!st || !st->valid) {
        if (s_atv.m_monitor) {
            XLabel_setText_2(s_atv.m_monitor, "监视：尚无数据（连接后自动轮询）");
            XWidget_update((XWidget*)s_atv.m_monitor);
        }
        return;
    }
    snprintf(buf, sizeof(buf),
             "监视：ETA=0x%04X  状态=%s%s\n"
             "输出频率 %.1f Hz    给定 %.1f Hz    电机 %u\n"
             "转矩 %.1f %%    电网 %.1f V    电机电压 %.0f V\n"
             "变频器热态 %.0f %%    电机热态 %.0f %%    功率 %.0f %%",
             st->eta,
             st->running ? "运行" : (st->faulted ? "故障" : "待机"),
             st->reverse ? "（反转）" : "",
             st->outputFreqHz, st->referenceHz,
             (unsigned)st->motorCurrent,
             st->motorTorquePercent, st->mainsVoltage, st->motorVoltage,
             st->driveThermalPercent, st->motorThermalPercent,
             st->motorPowerPercent);
    if (s_atv.m_monitor) {
        XLabel_setText_2(s_atv.m_monitor, buf);
        XWidget_update((XWidget*)s_atv.m_monitor);
    }
}

static void atv_refreshAlarm(void)
{
    const XModbusAtv61Status* st = s_atv.m_drive
        ? XModbusAtv61_status_const(s_atv.m_drive) : NULL;
    char buf[256];
    const char* name;
    const char* text;
    uint16_t code = st ? st->lastFault : 0;
    name = XModbusAtv61_faultName(code);
    text = XModbusAtv61_faultText(code);
    if (!code) {
        snprintf(buf, sizeof(buf), "报警：无故障%s",
                 (st && st->warning) ? "（存在警告，请查看状态字 ETA bit7）" : "");
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
             (s_atv.m_drive && XModbusAtv61_isBusy(s_atv.m_drive)) ? "（事务中）" : "");
    if (s_atv.m_connState) {
        XLabel_setText_2(s_atv.m_connState, buf);
        XWidget_update((XWidget*)s_atv.m_connState);
    }
}

/* ==================== 槽 ==================== */

/** @brief 运行控制动作分派（按被点按钮句柄区分动作）。 */
static void atv_dispatch(XObject* sender);

/** @brief 驱动状态更新 → 刷新监视/报警区。 */
static void atv_onStatusUpdated(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    atv_refreshMonitor();
    atv_refreshAlarm();
    atv_refreshConnState();
}

/** @brief 驱动动作完成 → 状态栏回显。 */
static void atv_onCommandCompleted(XObject* receiver, XVarList* args)
{
    (void)receiver;
    int op = -1;
    bool ok = false;
    if (args) {
        XVarList_start(args);
        op = (int)XVarList_arg(args, XModbusAtv61_Operation);
        ok = XVarList_arg(args, bool);
    }
    switch (op) {
    case XModbusAtv61_Op_ReadStatus:   atv_status(ok ? "状态读取完成" : "状态读取失败"); break;
    case XModbusAtv61_Op_SetFrequency: atv_status(ok ? "频率给定已下发" : "频率给定下发失败"); break;
    case XModbusAtv61_Op_SetSpeed:     atv_status(ok ? "转速给定已下发" : "转速给定下发失败"); break;
    case XModbusAtv61_Op_Start:        atv_status(ok ? "启动成功" : "启动失败"); break;
    case XModbusAtv61_Op_Stop:         atv_status(ok ? "已受控停机" : "停机失败"); break;
    case XModbusAtv61_Op_FreeStop:     atv_status(ok ? "已自由停车" : "自由停车失败"); break;
    case XModbusAtv61_Op_QuickStop:    atv_status(ok ? "已快速停机" : "快速停机失败"); break;
    case XModbusAtv61_Op_ResetFault:   atv_status(ok ? "故障复位已下发" : "故障复位失败"); break;
    default: break;
    }
    atv_refreshConnState();
}

/** @brief 驱动错误 → 状态栏与报警区。 */
static void atv_onDriverError(XObject* receiver, XVarList* args)
{
    (void)receiver;
    const char* msg = "";
    if (args) {
        XVarList_start(args);
        msg = XVarList_arg(args, const char*);
    }
    atv_status(msg && msg[0] ? msg : "驱动错误");
}

/** @brief 连接/断开按钮。 */
static void atv_onConnClicked(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_atv.m_client) return;
    XModbusDevice* dev = (XModbusDevice*)s_atv.m_client;
    if (s_atv.m_connected) {
        XModbusDevice_disconnectDevice(dev);
        if (s_atv.m_pollTimer) XTimer_stop_base(s_atv.m_pollTimer);
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

    int unit = s_atv.m_unit ? XSpinBox_value(s_atv.m_unit) : 1;
    if (unit < 1 || unit > 247) unit = 1;
    if (s_atv.m_drive) XModbusAtv61_attach(s_atv.m_drive, (XModbusClient*)s_atv.m_client, unit);

    if (!XModbusDevice_connectDevice(dev)) {
        atv_status("串口打开失败（检查串口占用/设备存在）");
        atv_refreshConnState();
        return;
    }
    s_atv.m_connected = true;
    if (s_atv.m_connBtn) XPushButton_setText_2(s_atv.m_connBtn, "断开");
    if (s_atv.m_pollTimer) XTimer_start_base(s_atv.m_pollTimer);
    atv_status("串口已连接");
    atv_refreshConnState();
}

/** @brief 下发频率给定。 */
static void atv_onFreqClicked(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_atv.m_drive) return;
    if (!s_atv.m_connected) { atv_status("未连接，无法下发"); return; }
    double hz = 0.0;
    if (s_atv.m_freqEdit) {
        const char* txt = XLineEdit_text(s_atv.m_freqEdit);
        if (txt && txt[0]) hz = atof(txt);
    }
    if (!XModbusAtv61_setFrequency(s_atv.m_drive, hz)) {
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
    if (!s_atv.m_drive || !s_atv.m_connected) {
        atv_status("未连接，无法操作");
        return;
    }
    if (sender == (XObject*)s_atv.m_runBtn) {
        XModbusAtv61_start(s_atv.m_drive, false);
    } else if (sender == (XObject*)s_atv.m_revBtn) {
        XModbusAtv61_start(s_atv.m_drive, true);
    } else if (sender == (XObject*)s_atv.m_stopBtn) {
        XModbusAtv61_stop(s_atv.m_drive);
    } else if (sender == (XObject*)s_atv.m_freeBtn) {
        XModbusAtv61_freeStop(s_atv.m_drive);
    } else if (sender == (XObject*)s_atv.m_qstopBtn) {
        XModbusAtv61_quickStop(s_atv.m_drive);
    } else if (sender == (XObject*)s_atv.m_resetBtn) {
        XModbusAtv61_resetFault(s_atv.m_drive);
    }
}

/** @brief 轮询定时器回调。 */
static void atv_pollTimerCb(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    if (!s_atv.m_drive || !s_atv.m_connected) return;
    if (s_atv.m_pollChk && !XCheckBox_isChecked(s_atv.m_pollChk)) return;
    XModbusAtv61_readStatus(s_atv.m_drive);
}

/** @brief 轮询开关切换 → 启停定时器。 */
static void atv_onPollToggled(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_atv.m_pollTimer) return;
    if (s_atv.m_pollChk && XCheckBox_isChecked(s_atv.m_pollChk)) {
        if (s_atv.m_connected) XTimer_start_base(s_atv.m_pollTimer);
    } else {
        XTimer_stop_base(s_atv.m_pollTimer);
    }
}

/** @brief 轮询周期变更 → 重设定时器间隔。 */
static void atv_onPollMsChanged(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (!s_atv.m_pollTimer || !s_atv.m_pollMs) return;
    int ms = XSpinBox_value(s_atv.m_pollMs);
    if (ms < 100) ms = 100;
    XTimer_setInterval(s_atv.m_pollTimer, (size_t)ms);
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
    root = XWidget_create(parent, 0);
    if (!root) return NULL;
    s_atv.m_root = root;
    s_atv.m_status = status;
    s_atv.m_user = user;

    XBoxLayout_init(&s_atv.m_rootLayout, XBoxLayoutDirection_TopToBottom);
    XWidget_setLayout(root, (XLayout*)&s_atv.m_rootLayout);

    /* ---- 标题 ---- */
    {
        XLabel* title = XLabel_create(root, 0);
        if (title) {
            XLabel_setText_2(title, "ATV61 变频器（Modbus RTU 主站）");
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
    atv_addLabel(row, "频率给定(Hz)", 88);
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
            "[1.6 命令] CHCF=SIM 时 Fr1=Mdb。");
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
    s_atv.m_drive = XModbusAtv61_create();
    if (s_atv.m_drive) {
        XModbusAtv61_attach(s_atv.m_drive, (XModbusClient*)s_atv.m_client, 1);
        XObject_setParent((XObject*)s_atv.m_drive, (XObject*)root);
        XObject_connect_1((XObject*)s_atv.m_drive,
                          XSignal(XModbusAtv61_statusUpdated_signal),
                          (XObject*)root, atv_onStatusUpdated,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)s_atv.m_drive,
                          XSignal(XModbusAtv61_commandCompleted_signal),
                          (XObject*)root, atv_onCommandCompleted,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)s_atv.m_drive,
                          XSignal(XModbusAtv61_errorOccurred_signal),
                          (XObject*)root, atv_onDriverError,
                          XConnectionType_Direct);
    }

    /* ---- 轮询定时器 ---- */
    s_atv.m_pollTimer = XTimer_create();
    if (s_atv.m_pollTimer) {
        XTimer_setInterval(s_atv.m_pollTimer, 500);
        XTimer_setTimerCallback(s_atv.m_pollTimer, atv_pollTimerCb);
        XTimer_setUserData(s_atv.m_pollTimer, NULL);
        XObject_setParent((XObject*)s_atv.m_pollTimer, (XObject*)root);
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
    return root;
}

/* ==================== 自测 ==================== */

int demo_page_atv61_autotest(XWidget* page)
{
    int fail = 0;
    if (!page || page != s_atv.m_root) return -1;

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

    if (!s_atv.m_drive) { printf("XGuiAutoTest: [FAIL] ATV61 驱动缺失\n"); ++fail; }
    else printf("XGuiAutoTest: [PASS] ATV61 驱动已建\n");

    if (s_atv.m_connected) {
        printf("XGuiAutoTest: [FAIL] ATV61 构建后不应处于已连接态\n");
        ++fail;
    } else {
        printf("XGuiAutoTest: [PASS] ATV61 构建后未连接\n");
    }

    if (XModbusAtv61_faultName(9) == NULL ||
        strcmp(XModbusAtv61_faultName(9), "OCF") != 0) {
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
    (void)page;
    /* 盒式布局自适应：无需手工重排（控件宽度固定、纵向流式排布）。 */
}

#else /* !ATV_PAGE_ENABLED */

/* 裁剪契约桩：控件或 Modbus 模块被裁掉时，主文件跳过本页注册。 */
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
