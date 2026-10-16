/**
 * @file    xgui_demo_page_modbus.c
 * @brief   XGuiDemo 扩展页：Modbus 主站/从站调试（双页拆分，串口 RTU /
 *          网络 TCP 双通道）。
 * @author  XinYueC 团队
 * @details 契约见 xgui_demo_pages.h（demo_page_modbus_master_build /
 *          demo_page_modbus_slave_build / autotest / adapt）。
 *
 * 页 17 = 主站（Master）：通道选择（TCP/串口 RTU）→ 连接参数 → 从站地址 →
 *          远程 IO 模块适配（输入/输出点数）→ 连接/断开 → 轮询周期 →
 *          状态行 + IO 点位网格（输入只读指示、输出点击翻转写线圈）。
 * 页 18 = 从站（Server）：通道选择（TCP 监听/串口 RTU）→ 参数 → 本站地址
 *          → 点数配置 → 启动/停止 → **数据面板**（可手动调整）：
 *          XTableWidget 段表——线圈 DO/离散输入 DI 用内嵌 XCheckBox
 *          点击即写数据区；输入/保持寄存器 IR/HR 用内嵌 XLineEdit 回车
 *          提交写数据区（XModbusServer_setData2 即时生效+dataWritten
 *          计数），主站可随即读走，构成端到端联调闭环。
 *
 * 主站「远程 IO 模块」适配语义：把远端从站当作远程 IO 模块——
 *   输入点 = 离散输入（DiscreteInputs，只读轮询显示）；
 *   输出点 = 线圈（Coils，点击翻转 → sendWriteRequest 写单线圈）。
 * 点数 SpinBox 变更即重建点位网格与轮询单元。
 *
 * 裁剪守卫：协议栈依赖 XMODBUS_CORE_ON（Client/Server/RTU/TCP 默认级联），
 * 控件依赖 Widget/Label/ComboBox/LineEdit/PushButton/CheckBox/SpinBox/
 * TableWidget。任一被裁剪时降级为契约桩（build 返回 NULL、autotest 返回
 * -1），主文件可无条件链接。
 *
 * 线程口径：XModbus 全家为 GUI 线程对象（事件驱动非阻塞），与演示页
 * 既有远程客户端页同款——全部信号槽 Direct 连接，无锁。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "XGuiConfig.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XWidget.h"
#include "XScrollArea.h"
#include "XAbstractScrollArea.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XLineEdit.h"
#include "XComboBox.h"
#include "XPushButton.h"
#include "XSpinBox.h"
#include "XTimer.h"
#include "XTableWidget.h"
#include "XWidget_Protected.h"
#include "XCoreApplication.h" /* paintImage/paintOffset（DI 指示灯自绘）。 */
#include "XSystem.h"
#include "XAbstractButton.h"
#include "xgui_demo_pages.h"

/* 协议栈头（XModbus 全家）。 */
#include "XModbus_config.h"
#if XMODBUS_CORE_ON && XMODBUS_CLIENT_ON && XMODBUS_SERVER_ON
#include "XModbusClient.h"
#include "XModbusServer.h"
#include "XModbusDataUnit.h"
#include "XModbusReply.h"
#if XMODBUS_TCP_ON
#include "XModbusTcpClient.h"
#include "XModbusTcpServer.h"
#endif
#if XMODBUS_RTU_ON
#include "XModbusRtuSerialClient.h"
#include "XModbusRtuSerialServer.h"
#include "XDeviceSerialPort.h"
#endif
#define MB_PAGE_ENABLED (XWIDGET_ON && XLABEL_ON && XCOMBOBOX_ON && \
                         XLINEEDIT_ON && XPUSHBUTTON_ON && XCHECKBOX_ON && \
                         XSPINBOX_ON && XTABLEWIDGET_ON)
#else
#define MB_PAGE_ENABLED 0
#endif

#if MB_PAGE_ENABLED

/* ==================== 布局常量 ==================== */
#define MB_ROW_H        24      /* 标准行高。 */

/* 数据面板行容量（DI/DO 各段上限；IR/HR 寄存器表行数）。 */
#define MB_SLAVE_MAX_POINTS 64
#define MB_SLAVE_REG_ROWS   16

/* ==================== DI 指示灯格（圆圈+编号自绘） ==================== */

#if XWIDGET_ON && XLABEL_ON && XPAINTER_ON
XCLASS_DEFINE_BEGING(MbDiLamp)
XCLASS_DEFINE_EXTEND_END(MbDiLamp, XLabel)

/** @brief DI 指示灯单元：左侧圆圈（绿=1 红=0）+ 右侧编号文本。 */
typedef struct MbDiLamp
{
    XLabel m_base;      /**< XLabel 基类；必须是第一个成员。 */
    bool   m_on;        /**< 输入位值（true=绿圈）。 */
    int    m_index;     /**< 点编号（DIoo 文本）。 */
} MbDiLamp;

/** @brief 页内默认字体（对齐 demo_apply_default_font：外挂轮廓家族
 *  优先，回退内置；DI 灯编号用 13px 小字）。 */
static void mb_apply_default_font(XFont* font, int pixelSize)
{
    if (!font) return;
    {
        const XString* exeDir = XCoreApplication_applicationDirPath();
        char fontPath[XFONT_EXTERNAL_FONT_PATH_MAX];
        if (exeDir &&
            snprintf(fontPath, sizeof(fontPath),
                     "%s/../Library/XFont/XFontOutlineCommon",
                     XString_toUtf8(exeDir)) > 0 &&
            strlen(fontPath) < sizeof(fontPath)) {
            XFont_setFamily(font, fontPath);
            XClassDelete((XClass*)exeDir);
            XFont_setPixelSize(font, pixelSize);
            return;
        }
        if (exeDir) XClassDelete((XClass*)exeDir);
        XFont_setPixelSize(font, pixelSize);
    }
}
/** @brief 绘制：底色 + 圆圈 + 编号。 */
static void VMbDiLamp_paintEvent(XWidget* self, XEvent* event)
{
    MbDiLamp* lamp = (MbDiLamp*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect rect, circle;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    XRect_init(&rect, 0, 0, XWidget_width(self), XWidget_height(self));
    /* 圆圈：左侧 14px 直径，垂直居中；绿=1 红=0。
     * [2026-10-13] drawEllipse_2 走 brush 填充+pen 描边管线——必须先
     * setBrush(填充色)/setPen(描边色)，默认 brush=NoBrush 时只描边
     * （截图空心白圈实证）。 */
    {
        float cx = rect.x + 9.0f;
        float cy = rect.y + rect.height / 2.0f;
        uint32_t fill = lamp->m_on ? 0xff2ecc40u : 0xffe03434u;
        XPainter_setBrush(&painter, fill);
        XPainter_setPen(&painter, 0xff202020u);
        XPainter_drawEllipse_2(&painter, cx, cy, 7.0f, 7.0f);
    }
    {
        XFont font;
        XRect trect;
        char text[12];
        snprintf(text, sizeof(text), "DI%02d", lamp->m_index);
        XFont_init(&font);
        mb_apply_default_font(&font, 13);
        XPainter_setFont(&painter, &font);
        XRect_init(&trect, rect.x + 22, rect.y,
                   rect.width - 22, rect.height);
        XPainter_drawTextRect(&painter, &trect,
                              XPAINTER_TEXT_ALIGN_LEFT |
                              XPAINTER_TEXT_ALIGN_VCENTER,
                              text,
                              lamp->m_on ? 0xff2ecc40u : 0xffe0e0e0u);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

XVtable* MbDiLamp_class_init(void)
{
    XVTABLE_INIT_DEFAULT(MbDiLamp)
    XVTABLE_INHERIT_XCLASS(XLabel);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VMbDiLamp_paintEvent);
    return XVTABLE_DEFAULT;
}

static void MbDiLamp_init(MbDiLamp* self, XWidget* parent, int index)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XLabel_init(&self->m_base, parent, 0);
    XClassSetVtable(self, MbDiLamp);
    self->m_on = false;
    self->m_index = index;
    XWidget_show((XWidget*)self);
}

/** @brief 置位并重绘（值变化才刷）。 */
static void MbDiLamp_setOn(MbDiLamp* self, bool on)
{
    if (!self || self->m_on == on) return;
    self->m_on = on;
    XWidget_update((XWidget*)self);
}

#endif /* XPAINTER_ON */

/** @brief 数据面板行控件（从站 cellWidget 与主站寄存器表共用）。 */
typedef struct MbSlaveRow {
    XCheckBox*      bit;              /* DO/DI 行：勾选=值 1。 */
    XLineEdit*      reg;              /* IR/HR 行：十进制 0..65535。 */
} MbSlaveRow;

/* ==================== 主站页状态 ==================== */

typedef struct MbDoLamp MbDoLamp; /* 前向：DO 圆圈控件（定义在状态容器后）。 */

/** @brief 主站通道类型。 */
typedef enum {
    MB_MASTER_TCP = 0,   /* Modbus TCP（网络）。 */
    MB_MASTER_RTU = 1    /* Modbus RTU（串口）。 */
} MbMasterChannel;

/** @brief 主站侧状态容器（单实例静态，页生命周期=进程）。 */
typedef struct MbMaster {
    XWidget*        root;             /* 页面根（堆叠布局分配几何）。 */
    XScrollArea*    scroll;           /* 内容滚动区（铺满根，widgetResizable）。 */
    XWidget*        body;             /* 内容容器（绝对定位原承载坐标不变）。 */
    XWidget*        pageRoot;         /* 页面根（scroll 的父级，堆叠布局分配几何）。 */
    int             regCols;          /* 寄存器表列组数（1..4，随视口宽自适应）。 */
    int             regDispRows;      /* 寄存器表当前显示行数。 */
    XComboBox*      channelCombo;     /* 通道：TCP/串口。 */
    XLabel*         tcpLbl;           /* TCP 行标签（随通道显隐）。 */
    XLineEdit*      tcpAddrEdit;      /* TCP: 服务器地址。 */
    XLineEdit*      tcpPortEdit;      /* TCP: 端口。 */
    XLabel*         serialLbl;        /* 串口行标签（随通道显隐）。 */
    XLineEdit*      serialNameEdit;   /* 串口: 设备名。 */
    XComboBox*      serialBaudCombo;  /* 串口: 波特率。 */
    XSpinBox*       unitSpin;         /* 从站地址(1..247)。 */
    XSpinBox*       inCountSpin;      /* 远程 IO 输入点数(0..64)。 */
    XSpinBox*       outCountSpin;     /* 远程 IO 输出点数(0..64)。 */
    XSpinBox*       pollSpin;         /* 轮询周期 ms(10..5000)。 */
    XPushButton*    connectBtn;       /* 连接/断开。 */
    XPushButton*    allOnBtn;         /* 输出全开。 */
    XPushButton*    allOffBtn;        /* 输出全关。 */
    /* 寄存器读写面板（IR/HR，16 进制）。 */
    XComboBox*      regTableCombo;    /* IR/HR 选择。 */
    XSpinBox*       regAddrSpin;      /* 起始地址。 */
    XSpinBox*       regCountSpin;     /* 数量(1..16)。 */
    XTableWidget*   regTable;         /* 数据表（16 进制显示，cellWidget 编辑）。 */
    MbSlaveRow      regRows[MB_SLAVE_REG_ROWS];  /* 行内嵌编辑控件。 */
    XLineEdit*      regWriteValue;    /* 写值框（16 进制）。 */
    XSpinBox*       regWriteAddr;     /* 写地址。 */
    bool            regSuppress;      /* 回填抑制。 */
    XModbusReply*   regPendingReply;        /* 寄存器读在途。 */
    XModbusReply*   regPendingWriteReply;   /* 寄存器写在途。 */
    XLabel*         stateLabel;       /* 状态行。 */
    XLabel*         statLabel;        /* 统计行（请求/错误）。 */
    XWidget*        inGridHost;       /* 输入点网格宿主。 */
    XWidget*        outGridHost;      /* 输出点网格宿主。 */
    /* 引擎对象（通道切换重建）。 */
    XModbusDevice*  device;           /* 当前设备（TcpClient 或 RtuSerialClient）。 */
    XModbusReply*   pendingReply;     /* 在途读请求（finished 后 deleteLater）。 */
    XModbusDataUnit* pollUnit;        /* 轮询单元（DiscreteInputs）。 */
    XModbusDataUnit* coilUnit;        /* 线圈轮询单元（DO 状态回读）。 */
    bool             coilPhase;       /* 轮询相位：false=DI true=DO。 */
    XCheckBox*       singleTxCheck;    /* 单事务模式多选框（TCP 通道显示）。 */
    bool             singleTxMode;     /* 单事务模式：写不与在途读流水线并发。 */
    bool             coilPhaseWasCoils; /* 在途请求的相位（回填分派）。 */
    bool             coilSyncBack;    /* 回填勾选框时抑制写回环。 */
    XTimer*         pollTimer;        /* 轮询定时器（页根子对象级联析构）。 */
    XTimer*         disconnectTimer;  /* 断开回归联调定时器。 */
    XTimer*         reclaimTimer;     /* Reply 回收定时器（1s 独立拍）。 */
    /* 远程 IO 配置副本（重建网格用）。 */
    int             inCount;
    int             outCount;
    /* 点位控件（上限 64）：DI=指示灯格；DO=勾选框（点击翻转写线圈）。 */
    MbDiLamp*       inCells[MB_SLAVE_MAX_POINTS];
    MbDoLamp*       outCells[MB_SLAVE_MAX_POINTS];
    /* 统计。 */
    unsigned        reqCount;
    unsigned        errCount;
    unsigned        timerTick;
    XString*        doAutoList;      /* env 预置的 DO 勾选序列(联调)。 */
    XModbusReply*   retiredReply;    /* 已 finished 待回收的 Reply（延迟删）。 */
    XModbusReply*   retiredWriteReply; /* 写应答延迟回收槽。 */
    /* [写让路 2026-10-15] 轮询读在途时到达的写请求暂存于此，待读应答
     * 完成后的空闲槽发出——远程 IO 类单事务从机忙于应答轮询时会丢弃
     * 交叠写帧，主站侧随后走 1s 超时×重试级联（轮询卡数秒+写未应用）。
     * 让路后从机只见串行请求，写必达且不卡。 */
    XModbusDataUnit* deferredWriteUnit; /* 待发写单元（NULL=无）。 */
} MbMaster;

/* ==================== 从站页状态 ==================== */

/** @brief 从站侧状态容器。 */
typedef struct MbSlave {
    XWidget*        root;             /* 页面根（堆叠布局分配几何）。 */
    XScrollArea*    scroll;           /* 内容滚动区（铺满根）。 */
    XWidget*        body;             /* 内容容器。 */
    XWidget*        pageRoot;         /* 页面根（scroll 的父级）。 */
    XComboBox*      channelCombo;     /* 通道：TCP 监听/串口。 */
    XLabel*         tcpLbl;           /* TCP 行标签（随通道显隐）。 */
    XLineEdit*      tcpPortEdit;      /* TCP: 监听端口。 */
    XLabel*         serialLbl;        /* 串口行标签（随通道显隐）。 */
    XLineEdit*      serialNameEdit;   /* 串口: 设备名。 */
    XComboBox*      serialBaudCombo;  /* 串口: 波特率。 */
    XSpinBox*       unitSpin;         /* 本站地址(1..247)。 */
    XSpinBox*       inCountSpin;      /* 输入点数。 */
    XSpinBox*       outCountSpin;     /* 输出点数。 */
    XPushButton*    startBtn;         /* 启动/停止。 */
    XLabel*         stateLabel;       /* 状态行。 */
    XLabel*         statLabel;        /* 请求计数行。 */
    /* 数据面板（可手动调整）。 */
    XTableWidget*   dataTable;        /* 数据表（地址+值两列）。 */
    XComboBox*      tableCombo;       /* 当前编辑表：DO/DI/IR/HR。 */
    XPushButton*    applyBtn;         /* 「按点数重建面板」按钮。 */
    MbSlaveRow      rows[MB_SLAVE_REG_ROWS > MB_SLAVE_MAX_POINTS
                             ? MB_SLAVE_REG_ROWS : MB_SLAVE_MAX_POINTS];
    /* 引擎对象。 */
    XModbusServer*  server;           /* TcpServer 或 RtuSerialServer。 */
    /* 数据表类型键。 */
    XModbusRegisterType inEnum, outEnum, irEnum, hrEnum;
    /* 数据区副本（重挂/面板重建用）。 */
    int             inCount;
    int             outCount;
    unsigned        writeEvents;      /* dataWritten 事件计数。 */
    int             panelSuppress;    /* 面板程序化回填时抑制回调环。 */
    int             bitEchoSuppress;  /* [全开掉线根修 2026-10-15] 主站写
        → dataWritten 回显 setChecked 时抑制 toggled 槽的整列差异写回：
        回显逐格 setChecked 期间，未更新格仍是旧态，toggled 槽的全列
        扫描会把旧态写回数据区，把主站刚写的值整批冲掉（昆仑通态实测：
        全开数秒后整体掉回 OFF——master 页 coilSyncBack 同款防护，从站
        页此前缺失）。 */
} MbSlave;

static MbMaster s_mb;
static MbSlave  s_mbSlave;

/* ==================== DO 输出圆圈（可点击翻转+连接门控） ==================== */

#if XWIDGET_ON && XABSTRACTBUTTON_ON && XPAINTER_ON
XCLASS_DEFINE_BEGING(MbDoLamp)
XCLASS_DEFINE_EXTEND_END(MbDoLamp, XCheckBox)

/** @brief DO 输出圆圈：绿=1 红=0 灰=未连接；单击翻转（checkable）。 */
typedef struct MbDoLamp
{
    XCheckBox m_base;        /**< XCheckBox 基类（点击/翻转链路已验证）；第一成员。 */
    int    m_index;          /**< 点编号。 */
} MbDoLamp;

/** @brief 绘制：圆圈（连接时绿/红随 checked；未连接灰）+ 编号。 */
static void VMbDoLamp_paintEvent(XWidget* self, XEvent* event)
{
    MbDoLamp* lamp = (MbDoLamp*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect rect;
    uint32_t color;
    bool connected;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    connected = s_mb.device != NULL;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    XRect_init(&rect, 0, 0, XWidget_width(self), XWidget_height(self));
    color = !connected ? 0xff9a9a9au
            : (XCheckBox_isChecked(lamp) ? 0xff2ecc40u : 0xffe03434u);
    {
        float cx = rect.x + 9.0f;
        float cy = rect.y + rect.height / 2.0f;
        uint32_t fill = !connected ? 0xffb8b8b8u : color;
        XPainter_setBrush(&painter, fill);
        XPainter_setPen(&painter, 0xff202020u);
        XPainter_drawEllipse_2(&painter, cx, cy, 7.0f, 7.0f);
    }
    {
        XFont font;
        XRect trect;
        char text[12];
        snprintf(text, sizeof(text), "DO%02d", lamp->m_index);
        XFont_init(&font);
        mb_apply_default_font(&font, 13);
        XPainter_setFont(&painter, &font);
        XRect_init(&trect, rect.x + 22, rect.y,
                   rect.width - 22, rect.height);
        XPainter_drawTextRect(&painter, &trect,
                              XPAINTER_TEXT_ALIGN_LEFT |
                              XPAINTER_TEXT_ALIGN_VCENTER,
                              text, color);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

XVtable* MbDoLamp_class_init(void)
{
    XVTABLE_INIT_DEFAULT(MbDoLamp)
    XVTABLE_INHERIT_XCLASS(XCheckBox);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VMbDoLamp_paintEvent);
    return XVTABLE_DEFAULT;
}

static void MbDoLamp_init(MbDoLamp* self, XWidget* parent, int index)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XCheckBox_init(&self->m_base, parent, 0);
    XClassSetVtable(self, MbDoLamp);
    self->m_index = index;
    XWidget_show((XWidget*)self);
}
#endif /* XABSTRACTBUTTON_ON */



/* ==================== 工具 ==================== */

/** @brief 构建一行标签（统一 13px 参数行小字；标题/栏名调用点覆盖字号）。 */
static void mb_buildLabelAt(XWidget* parent, const char* text, int x, int y,
                            int w, XLabel** out)
{
    XLabel* label = XLabel_create(parent, 0);
    if (label) {
        XLabel_setText_2(label, text);
        /* [2026-10-13 重叠根修] 参数行标签统一 13px 小字：默认 16px 下
         * 3~4 字标签实际渲染宽超预留列宽，文本右缘压进同行输入框。 */
        XLabel_setTextPixelSize(label, 13);
        XWidget_setGeometry((XWidget*)label, x, y, w, 22);
        XWidget_show((XWidget*)label);
    }
    if (out) *out = label;
}

/** @brief 通道下拉（TCP 网络 / 串口 RTU）。 */
static XComboBox* mb_buildChannelCombo(XWidget* parent, int x, int y, int w)
{
    XComboBox* combo = XComboBox_create(parent, 0);
    if (combo) {
        XComboBox_addItem_2(combo,
                "TCP \xE7\xBD\x91\xE7\xBB\x9C");      /* TCP 网络 */
        XComboBox_addItem_2(combo,
                "\xE4\xB8\xB2\xE5\x8F\xA3 RTU");      /* 串口 RTU */
        XComboBox_setCurrentIndex(combo, 0);
        XWidget_setGeometry((XWidget*)combo, x, y, w, 24);
        XWidget_show((XWidget*)combo);
    }
    return combo;
}

/** @brief 波特率下拉（默认 9600）。 */
static XComboBox* mb_buildBaudCombo(XWidget* parent, int x, int y, int w)
{
    static const char* const kBauds[] = {
        "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200"
    };
    XComboBox* combo = XComboBox_create(parent, 0);
    int i;
    if (!combo) return NULL;
    for (i = 0; i < (int)(sizeof(kBauds) / sizeof(kBauds[0])); ++i)
        XComboBox_addItem_2(combo, kBauds[i]);
    XComboBox_setCurrentIndex(combo, 3); /* 9600。 */
    XWidget_setGeometry((XWidget*)combo, x, y, w, 24);
    XWidget_show((XWidget*)combo);
    return combo;
}

static XSpinBox* mb_buildSpin(XWidget* parent, int x, int y, int w,
                              int min, int max, int val)
{
    XSpinBox* spin = XSpinBox_create(parent, 0);
    if (spin) {
        XSpinBox_setRange(spin, min, max);
        XSpinBox_setValue(spin, val);
        XWidget_setGeometry((XWidget*)spin, x, y, w, 24);
        XWidget_show((XWidget*)spin);
    }
    return spin;
}

/* ================================================================
 * 页 17：主站（Master）
 * ================================================================ */

#define MB_GRID_X0     8      /* 网格起点(栏内)。 */
#define MB_CELL_W      68
#define MB_CELL_H      22
#define MB_GRID_COLS   5

static void mb_masterOnReply(XObject* sender, XVarList* args);
static void mb_masterPollTimerCb(void* userData, XTimerData* timer);
/** @brief 轮询周期输入变化：即时改写运行中定时器周期（下一拍生效）。 */
static void mb_masterPollPeriodSlot(XObject* sender, XVarList* args);
static void mb_slaveBitSlot(XObject* sender, XVarList* args);
static void mb_slaveRegSlot(XObject* sender, XVarList* args);
static void mb_masterStateSlot(XObject* sender, XVarList* args);
static void mb_masterWriteCoil(int index, bool on);
static void mb_masterDoAutoSlot(void* userData, XTimerData* timer);
static void mb_masterWriteReplyDone(XObject* sender, XVarList* args);
static void mb_masterDisconnectAutoSlot(void* userData, XTimerData* timer);
static void mb_masterRegBuildRows(int dispRows);
static void mb_masterRegRowSlot(XObject* sender, XVarList* args);
static void mb_masterRegReplyDone(XObject* sender, XVarList* args);
static void mb_masterRegDblSlot(XObject* sender, XVarList* args);
static void mb_masterSingleTxSlot(XObject* sender, XVarList* args);

/** @brief 寄存器表行重建：清旧 cellWidget，按数量建 16 进制编辑框。 */
static void mb_masterRegEditCommit(int row);
/* ==================== 寄存器表动态列组（视口宽自适应） ====================
 * 行优先映射：显示序 i=row*regCols+colGroup，寄存器地址=起始+i（横向
 * 连续递增，与阅读顺序一致）。regRows[i] 语义=显示序 i 的编辑槽，
 * 挂载格=(i/regCols, 1+i%regCols)。列组宽=地址列 90+值列 80=170，
 * 组数=clamp((视口宽-行号列(字段实读 40)-值列余 20)/170, 1, 4)。 */
#define MB_REG_GROUP_W   170
#define MB_REG_MAX_COLS  4
/* [2026-10-14 显示行上限独立] 显示行是空行占位、不占寄存器编辑槽
 * （regRows[] 槽序=寄存器序，仍以 MB_SLAVE_REG_ROWS=16 为界）——
 * 此前误用 16 钳显示行，高窗口下表格下方留大片空白不肯补行。 */
#define MB_REG_MAX_DISP_ROWS 200
/* [根修 2026-10-15 表格度量对齐] 行高/表头带/行号列宽不再硬编码——
 * XTableWidget 上报内容尺寸用的是自身 m_headerHeight(XTW_HEADER_H=
 * 24，非 XTableView 的 XTV_HEADER_H=20) + m_rows×m_rowHeight(24)、
 * 宽=m_headerWidth(XTW_HEADER_W=40，非页面旧设 70)；按字段实读后
 * 填充行高恰等于部件高，滚动条严格大于才出现，不再常驻。 */
static int mb_regHeadH(void)
{
    return s_mb.regTable ? (int)s_mb.regTable->m_headerHeight : 24;
}
static int mb_regGutterW(void)
{
    return s_mb.regTable ? (int)s_mb.regTable->m_headerWidth : 40;
}
static int mb_regRowH(void)
{
    return s_mb.regTable ? (int)s_mb.regTable->m_base.m_rowHeight : 24;
}

static int mb_masterRegColCount(void)
{
    int vw;
    int cols;
    if (!s_mb.regTable) return 1;
#ifdef MB_BISECT_SINGLE_COL
    return 1; /* 二分排查：强制单列组。 */
#endif
    /* [2026-10-14 表随视口] 组数读表格自身宽（adapt 已先落几何）——
     * 此前读 body 宽而 body 钳 ≥740，窄窗永远 ≥3 组，违背「宽度不足
     * 一列多行」；现窄窗表格随视口收缩，可降到 1 列。行号列宽按控件
     * 字段实读（XTW_HEADER_W=40）。 */
    vw = XWidget_width((XWidget*)s_mb.regTable);
    cols = (vw - mb_regGutterW() - 20) / MB_REG_GROUP_W;
    if (cols < 1) cols = 1;
    if (cols > MB_REG_MAX_COLS) cols = MB_REG_MAX_COLS;
    /* [滞回 2026-10-14] 拖拽改宽在组数边界（±170px）来回抖动会每帧
     * 全量重建表格（BuildRows 40 子件）。已在 N 组时，宽度须跌破
     * N 组下界 30px 余量才降档——升档阈值不变，降档加滞回带。
     * [根修 2026-10-14] 滞回须对「当前组数 regCols」的下界判——
     * 原误写 cols（新算目标组，窄窗=1），170-30 恒满足致降档
     * 永不触发：宽窗建表 4 组后收窄，8 列压进 ~280px 互相叠字。 */
    if (s_mb.regCols > cols && cols >= 1 &&
        vw - mb_regGutterW() - 20 >= s_mb.regCols * MB_REG_GROUP_W - 30)
        return s_mb.regCols; /* 降档滞回：宽度仍在当前组数容差内。 */
    return cols;
}

/** @brief 列宽均分重摆：地址:值=9:8 比例拉满表宽（免横滚条+右侧无
 *  空白）。行列不变仅宽度变时单独调用，免整表重建。 */
static void mb_masterRegApplyColWidths(void)
{
    int tableW;
    int addrW;
    int valW;
    int g;
    if (!s_mb.regTable || s_mb.regCols < 1) return;
    tableW = XWidget_width((XWidget*)s_mb.regTable) - mb_regGutterW();
    if (tableW < 2 * s_mb.regCols) tableW = 2 * s_mb.regCols; /* 防负/零。 */
    addrW = tableW * 9 / (17 * s_mb.regCols);
    valW = tableW * 8 / (17 * s_mb.regCols);
    for (g = 0; g < s_mb.regCols; ++g) {
        XTableView_setColumnWidth(&s_mb.regTable->m_base, 2 * g, addrW);
        XTableView_setColumnWidth(&s_mb.regTable->m_base, 2 * g + 1, valW);
    }
}

/** @brief 显示行数统一口径：max(ceil(数量/列组), 视口可容纳行数)，钳
 *  [1,MB_REG_MAX_DISP_ROWS]——表格高度拉满主界面底部（空行占位，行
 *  数不占寄存器槽）；窗口极矮时需求行占上风、内容超高由竖滚动兜底。
 *  adapt 与数量变化槽共用同口径。 */
static int mb_masterRegFillRows(int cols)
{
    int count = s_mb.regCountSpin ? XSpinBox_value(s_mb.regCountSpin) : 8;
    int needRows = (count + cols - 1) / cols;
    int availH = s_mb.pageRoot ? XWidget_height(s_mb.pageRoot) : 0;
    int availRows;
    int dispRows;
    if (availH < 46) availH = 46;
    availRows = (availH - 502 - 12 - mb_regHeadH()) / mb_regRowH();
    if (availRows < 1) availRows = 1;
    if (availRows > MB_REG_MAX_DISP_ROWS) availRows = MB_REG_MAX_DISP_ROWS;
    if (needRows < 1) needRows = 1;
    dispRows = needRows > availRows ? needRows : availRows;
    if (dispRows > MB_REG_MAX_DISP_ROWS) dispRows = MB_REG_MAX_DISP_ROWS;
    return dispRows;
}

static void mb_masterRegBuildRows(int dispRows)
{
    int count = s_mb.regCountSpin ? XSpinBox_value(s_mb.regCountSpin) : 8;
    int rows = dispRows;
    int i;
    if (!s_mb.regTable) return;
    /* [对齐 Qt] 重建前先提交在编辑的挂载框（Qt 改模型前 delegate
     * commitData+closeEditor；否则 clearContents 静默丢弃用户输入，
     * 且失焦的 editingFinished 到 RowSlot 时 sender 已脱管匹配不上）。 */
    for (i = 0; i < MB_SLAVE_REG_ROWS; ++i)
        if (s_mb.regRows[i].reg)
            mb_masterRegEditCommit(i);
    XTableWidget_clearContents(s_mb.regTable);
    for (i = 0; i < MB_SLAVE_REG_ROWS; ++i) s_mb.regRows[i].reg = NULL;
    if (rows > MB_REG_MAX_DISP_ROWS) rows = MB_REG_MAX_DISP_ROWS;
    if (rows < 1) rows = 1;
    /* 动态列组：列数随视口宽自适应（列头「地址/值」×N 组）；行数由
       adapt 传入（≥ceil(数量/列组)，可拉大到填满视口高，空行占位）。 */
    s_mb.regCols = mb_masterRegColCount();
    s_mb.regDispRows = rows;
    {
        static const char* const kAddrHead = "\xE5\x9C\xB0\xE5\x9D\x80";   /* 地址 */
        static const char* const kValHead =
            "\xE5\x80\xBC(16\xE8\xBF\x9B\xE5\x88\xB6)";            /* 值(16进制) */
        const char* heads[2 * MB_REG_MAX_COLS];
        int g;
        int row;
        int total = count < rows * s_mb.regCols ? count
                                                : rows * s_mb.regCols;
        for (g = 0; g < s_mb.regCols; ++g) {
            heads[2 * g] = kAddrHead;
            heads[2 * g + 1] = kValHead;
        }
        XTableWidget_setColumnCount(s_mb.regTable, 2 * s_mb.regCols);
        XTableWidget_setHorizontalHeaderLabels(s_mb.regTable, heads,
                                               2 * s_mb.regCols);
        XTableWidget_setRowCount(s_mb.regTable, rows);
        /* 列宽均分：地址:值=9:8 比例拉满表宽（免横滚条+右侧无空白）。 */
        mb_masterRegApplyColWidths();
        for (i = 0; i < total; ++i) {
            /* 行优先：显示序 i=(row,组g)，g=i%regCols；组 g 的地址列=2g、
             * 值列=2g+1；寄存器地址=起始+i。 */
            char addr[12], val[8];
            XTableWidgetItem item;
            int g = i % s_mb.regCols;
            row = i / s_mb.regCols;
            snprintf(addr, sizeof(addr), "0x%04X",
                     XSpinBox_value(s_mb.regAddrSpin) + i);
            XMemset(&item, 0, sizeof(item));
            item.text = XString_create_utf8(addr);
            XTableWidget_setItem(s_mb.regTable, row, 2 * g, &item);
            XClassDelete((XClass*)item.text);
            snprintf(val, sizeof(val), "0x0000");
            XMemset(&item, 0, sizeof(item));
            item.text = XString_create_utf8(val);
            XTableWidget_setItem(s_mb.regTable, row, 2 * g + 1, &item);
            XClassDelete((XClass*)item.text);
        }
        (void)kAddrHead;
    }
}

/** @brief 双击值格：进入编辑（该格挂临时 XLineEdit，预填当前值全选）。 */
static void mb_masterRegEditBegin(int slot)
{
    XLineEdit* edit;
    const XTableWidgetItem* item;
    char val[8] = "0x0000";
    int row;
    int col;
    if (!s_mb.regTable || slot < 0 || slot >= MB_SLAVE_REG_ROWS) return;
    if (s_mb.regCols < 1) s_mb.regCols = 1;
    row = slot / s_mb.regCols;
    col = 2 * (slot % s_mb.regCols) + 1;
    /* 单编辑器口径（Qt 视图同）：先提交其它槽的挂载编辑。 */
    {
        int i;
        for (i = 0; i < MB_SLAVE_REG_ROWS; ++i)
            if (i != slot && s_mb.regRows[i].reg)
                mb_masterRegEditCommit(i);
    }
    if (s_mb.regRows[slot].reg) return; /* 已在编辑。 */
    fprintf(stderr, "[MB] Begin slot=%d row=%d col=%d\n", slot, row, col);
#ifdef MB_BISECT_NO_EDITOR
    return; /* 二分排查：不挂临时编辑框。 */
#endif
    item = XTableWidget_item(s_mb.regTable, row, col);
    if (item && item->text) {
        const char* cur = XString_toUtf8(item->text);
        if (cur && cur[0]) snprintf(val, sizeof(val), "%s", cur);
    }
    /* 转文本：移除挂载编辑（若有），清值格 item。 */
    XTableWidget_removeCellWidget(s_mb.regTable, row, col);
    {
        XTableWidgetItem blank;
        XMemset(&blank, 0, sizeof(blank));
        blank.text = XString_create_utf8("");
        XTableWidget_setItem(s_mb.regTable, row, col, &blank);
        XClassDelete((XClass*)blank.text);
    }
    edit = XLineEdit_create(s_mb.root, 0);
    if (!edit) return;
    XLineEdit_setText(edit, val);
    /* 全选由编辑框默认焦点行为覆盖；挂入表格并显示。 */
    XTableWidget_setCellWidget(s_mb.regTable, row, col, (XWidget*)edit);
    XWidget_show((XWidget*)edit);
    /* 双击编辑语义：编辑框主动取焦（Qt edit→editor->setFocus 同；
     * 双击的 PRESS 先于挂载抵达表格，须显式补焦点触发虚拟键盘）。 */
    XWidget_setFocus((XWidget*)edit);
    /* [对齐 Qt] editingFinished 两路共发：Return 或失焦确认（Qt 表格
     * delegate 编辑器 FocusOut→commitData+closeEditor 语义）。开启
     * finishOnFocusOut 后无改动失焦也收框（裸 QLineEdit 仅编辑过的
     * 失焦才发，不合视图编辑器场景）。 */
    XLineEdit_setFinishOnFocusOut(edit, true);
    XObject_connect_1((XObject*)edit,
                      (size_t)XLineEdit_editingFinished_signal(edit),
                      (XObject*)s_mb.root, mb_masterRegRowSlot,
                      XConnectionType_Direct);
    s_mb.regRows[slot].reg = edit;
}

/** @brief 编辑提交：解析 16 进制→写模块→转回文本显示。 */
static void mb_masterRegEditCommit(int row)
{
    fprintf(stderr, "[MB] Commit row=%d\n", row);
    XLineEdit* edit = s_mb.regRows[row].reg;
    long val;
    const char* text;
    char hex[8];
    XTableWidgetItem item;
    if (!edit) return;
    text = XLineEdit_text(edit);
    val = text ? strtol(text, NULL, 16) : 0;
    if (val < 0) val = 0;
    if (val > 0xFFFF) val = 0xFFFF;
    /* 写模块（回车=确认写；HR 可写，IR 写通常被模块拒——由应答体现）。 */
    if (s_mb.device) {
        XModbusDataUnit* unit;
        XModbusReply* reply;
        int table = s_mb.regTableCombo
                        ? XComboBox_currentIndex(s_mb.regTableCombo) : 1;
        unit = XModbusDataUnit_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE,
            table == 0 ? XModbusInputRegisters : XModbusHoldingRegisters,
            (uint16_t)(XSpinBox_value(s_mb.regAddrSpin) + row), 1);
        if (unit) {
            XModbusDataUnit_setValue(unit, 0, (uint16_t)val);
            /* [写让路+单事务 2026-10-15] 同单点翻转：单事务模式下在途
               读先让路暂存（后写胜出），回读完成空闲槽补发。 */
            if (s_mb.singleTxMode && s_mb.pendingReply) {
                if (s_mb.deferredWriteUnit)
                    XClassDelete(s_mb.deferredWriteUnit);
                s_mb.deferredWriteUnit = unit;
                return;
            }
            reply = XModbusClient_sendWriteRequest(
                (XModbusClient*)s_mb.device, unit,
                XSpinBox_value(s_mb.unitSpin));
            XClassDelete(unit);
            if (reply) {
                ++s_mb.reqCount;
                s_mb.regPendingWriteReply = reply;
                XObject_connect_1((XObject*)reply,
                                  (size_t)XModbusReply_finished_signal(reply),
                                  (XObject*)s_mb.root, mb_masterRegReplyDone,
                                  XConnectionType_Direct);
            } else {
                ++s_mb.errCount;
            }
        }
    }
    /* 转回文本显示（槽序 row=显示序 i：行 i/regCols、值列 1+i%regCols）。 */
    XMemset(&item, 0, sizeof(item));
    snprintf(hex, sizeof(hex), "0x%04lX", (unsigned long)val);
    item.text = XString_create_utf8(hex);
    XTableWidget_setItem(s_mb.regTable,
                         row / s_mb.regCols, 2 * (row % s_mb.regCols) + 1,
                         &item);
    XClassDelete((XClass*)item.text);
    XTableWidget_removeCellWidget(
        s_mb.regTable, row / s_mb.regCols, 2 * (row % s_mb.regCols) + 1);
    s_mb.regRows[row].reg = NULL;
}

/** @brief 寄存器面板：读 IR/HR 回填 16 进制表。 */
static void mb_masterRegFill(XModbusReply* reply)
{
    const XModbusDataUnit* result = XModbusReply_result_const(reply);
    int i;
    if (!result || !s_mb.regTable) return;
    s_mb.regSuppress = true;
    fprintf(stderr, "[MB] Fill\n");
    for (i = 0; i < (int)XModbusDataUnit_valueCount(result) &&
                i < MB_SLAVE_REG_ROWS; ++i) {
        uint16_t v = XModbusDataUnit_value(result, (size_t)i);
        char text[8];
        XTableWidgetItem item;
        int g = i % s_mb.regCols;
        int row = i / s_mb.regCols;
        int col = 2 * g + 1;
        if (s_mb.regRows[i].reg) { /* 编辑中：更新编辑框内容。 */
            snprintf(text, sizeof(text), "0x%04X", v);
            XLineEdit_setText(s_mb.regRows[i].reg, text);
            continue;
        }
        snprintf(text, sizeof(text), "0x%04X", v);
        /* [脏区 2026-10-15] 同值跳过：setItem 已收窄为单格矩形失效，
           此处再免掉无变化写入——稳态轮询值不变时零像素失效，只有
           真变化的格子进脏区（Qt dataChanged 仅实际变更语义同）。 */
        {
            const XTableWidgetItem* cur =
                XTableWidget_item(s_mb.regTable, row, col);
            if (cur && cur->text &&
                strcmp(XString_toUtf8(cur->text), text) == 0)
                continue;
        }
        XMemset(&item, 0, sizeof(item));
        item.text = XString_create_utf8(text);
        XTableWidget_setItem(s_mb.regTable, row, col, &item);
        XClassDelete((XClass*)item.text);
    }
    s_mb.regSuppress = false;
}

/** @brief 读按钮槽：按表类型发 FC3/FC4。 */
static void mb_masterRegReadSlot(XObject* sender, XVarList* args)
{
    XModbusDataUnit* unit;
    XModbusReply* reply;
    int table = s_mb.regTableCombo
                    ? XComboBox_currentIndex(s_mb.regTableCombo) : 1;
    (void)sender; (void)args;
    if (!s_mb.device) return;
    unit = XModbusDataUnit_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE,
        table == 0 ? XModbusInputRegisters : XModbusHoldingRegisters,
        (uint16_t)XSpinBox_value(s_mb.regAddrSpin),
        (size_t)XSpinBox_value(s_mb.regCountSpin));
    if (!unit) return;
    reply = XModbusClient_sendReadRequest((XModbusClient*)s_mb.device,
                                          unit,
                                          XSpinBox_value(s_mb.unitSpin));
    XClassDelete(unit);
    if (reply) {
        ++s_mb.reqCount;
        s_mb.regPendingReply = reply;
        XObject_connect_1((XObject*)reply,
                          (size_t)XModbusReply_finished_signal(reply),
                          (XObject*)s_mb.root, mb_masterRegReplyDone,
                          XConnectionType_Direct);
    } else {
        ++s_mb.errCount;
    }
}

/** @brief 写按钮槽：FC6 单地址写（写值框 16 进制）。 */
static void mb_masterRegWriteSlot(XObject* sender, XVarList* args)
{
    XModbusDataUnit* unit;
    XModbusReply* reply;
    int table = s_mb.regTableCombo
                    ? XComboBox_currentIndex(s_mb.regTableCombo) : 1;
    const char* text;
    long val;
    (void)sender; (void)args;
    if (!s_mb.device) return;
    text = s_mb.regWriteValue ? XLineEdit_text(s_mb.regWriteValue) : "0";
    val = strtol(text && text[0] ? text : "0", NULL, 16);
    if (val < 0) val = 0;
    if (val > 0xFFFF) val = 0xFFFF;
    unit = XModbusDataUnit_create_ex(
        XCLASS_DEFAULT_MEMORY_TYPE,
        table == 0 ? XModbusInputRegisters : XModbusHoldingRegisters,
        (uint16_t)XSpinBox_value(s_mb.regWriteAddr), 1);
    if (!unit) return;
    XModbusDataUnit_setValue(unit, 0, (uint16_t)val);
    /* [写让路+单事务 2026-10-15] 同单点翻转：单事务模式下在途读先
     * 让路暂存（后写胜出），回读完成空闲槽补发。 */
    if (s_mb.singleTxMode && s_mb.pendingReply) {
        if (s_mb.deferredWriteUnit)
            XClassDelete(s_mb.deferredWriteUnit);
        s_mb.deferredWriteUnit = unit;
        return;
    }
    reply = XModbusClient_sendWriteRequest((XModbusClient*)s_mb.device,
                                           unit,
                                           XSpinBox_value(s_mb.unitSpin));
    XClassDelete(unit);
    if (reply) {
        ++s_mb.reqCount;
        s_mb.regPendingWriteReply = reply;
        XObject_connect_1((XObject*)reply,
                          (size_t)XModbusReply_finished_signal(reply),
                          (XObject*)s_mb.root, mb_masterRegReplyDone,
                          XConnectionType_Direct);
    } else {
        ++s_mb.errCount;
    }
}

/** @brief 行编辑回车槽：按 sender 定位行提交。 */
static void mb_masterRegRowSlot(XObject* sender, XVarList* args)
{
    int i;
    (void)args;
    (void)sender;
    /* [根修 2026-10-14] 不按 sender 匹配：editingFinished 的发射者
     * 可能不是 regRows 登记的编辑框（页面 BuildRows 的常驻框与本路径
     * 挂载的临时框同格叠加，Return 的 accepted 链会路由到常驻框）。
     * 按「当前聚焦的行编辑框」定位：失焦确认时焦点已迁走，用
     * Commit 全量扫描（regRows 内非 NULL 者本函数语义=该行提交）。 */
    for (i = 0; i < MB_SLAVE_REG_ROWS; ++i) {
        if (s_mb.regRows[i].reg) {
            mb_masterRegEditCommit(i);
            return;
        }
    }
}

/** @brief 双击槽：值列(row,col=1) 进入编辑。 */
static void mb_masterRegDblSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        /* [根修 2026-10-13] XVarList_args_N 在块内声明 const 同名变量，
         * 会遮蔽块外预置的 row/col（块外恒 -1）——判定必须整段放进
         * 本块（解包变量的作用域只在块内）。 */
        XVarList_args_2(args, int, row, int, col);
        /* 动态列组：值列 col=2g+1（g=组序）→ 槽序=row*regCols+g。 */
        if (col >= 1 && row >= 0 && row < MB_SLAVE_REG_ROWS) {
            int g = (col - 1) / 2;
            if (g < s_mb.regCols)
                mb_masterRegEditBegin(row * s_mb.regCols + g);
        }
    }
}

/** @brief 寄存器读回归槽：程序化点「读」。 */
static void mb_masterRegReadAutoSlot(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    mb_masterRegReadSlot(NULL, NULL);
}

/** @brief 寄存器 Reply 完成：读回答回填；写应答回收。 */
static void mb_masterRegReplyDone(XObject* sender, XVarList* args)
{
    (void)args;
    if (sender == (XObject*)s_mb.regPendingReply) {
        mb_masterRegFill((XModbusReply*)sender);
        s_mb.regPendingReply = NULL;
        XObject_deleteLater(sender);
    } else if (sender == (XObject*)s_mb.regPendingWriteReply) {
        s_mb.regPendingWriteReply = NULL;
        XObject_deleteLater(sender);
    }
}

/** @brief 表类型切换/起始地址/数量变化：重建行（显示行与 adapt 同
 *  口径拉满视口高，数量增减不塌缩表格高度）。 */
static void mb_masterRegComboSlot(XObject* sender, XVarList* args)
{
    int cols;
    int rows;
    (void)sender; (void)args;
    if (s_mb.regCols < 1) s_mb.regCols = mb_masterRegColCount();
    cols = s_mb.regCols > 0 ? s_mb.regCols : 1;
    rows = mb_masterRegFillRows(cols);
    mb_masterRegBuildRows(rows);
}

static void mb_masterRegReadSlot(XObject* sender, XVarList* args);
static void mb_masterRegWriteSlot(XObject* sender, XVarList* args);
static void mb_masterRegComboSlot(XObject* sender, XVarList* args);
static void mb_masterReclaimCb(void* userData, XTimerData* timer);
static void mb_masterRegReadAutoSlot(void* userData, XTimerData* timer);

static void mb_masterWriteCoil(int index, bool on);

static uint16_t s_mbDoWritten[4]; /* 已写线圈位图（64 点=4×16bit）。 */

/** @brief DO 勾选公共体：差量写——只把「勾选态≠已写态」的格写模块。
 *  [2026-10-13] 原全列 16 帧幂等写会触发模块写限流（错误 40 实证），
 *  且无差别重写放大流量；改为位图差量，单次点击=单帧。 */
static void mb_masterDoIdx(int idx, XVarList* args)
{
    int i;
    (void)idx; (void)args;
    if (s_mb.coilSyncBack) return; /* 状态回填不写模块。 */
    for (i = 0; i < s_mb.outCount && i < MB_SLAVE_MAX_POINTS; ++i) {
        bool on;
        uint16_t word, mask;
        if (!s_mb.outCells[i]) continue;
        on = XAbstractButton_isChecked((const XAbstractButton*)
                                           s_mb.outCells[i]);
        word = s_mbDoWritten[i >> 4];
        mask = (uint16_t)(1u << (i & 15));
        if (((word & mask) != 0) != on) {
            mb_masterWriteCoil(i, on);
            if (on) s_mbDoWritten[i >> 4] |= mask;
            else    s_mbDoWritten[i >> 4] &= (uint16_t)~mask;
        }
    }
}

static void mb_masterDoSlot0(XObject* sender, XVarList* args)
{
    mb_masterDoIdx(0, args);
}

static void mb_masterDoSlot1(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(1, args);
}

static void mb_masterDoSlot2(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(2, args);
}

static void mb_masterDoSlot3(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(3, args);
}

static void mb_masterDoSlot4(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(4, args);
}

static void mb_masterDoSlot5(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(5, args);
}

static void mb_masterDoSlot6(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(6, args);
}

static void mb_masterDoSlot7(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(7, args);
}

static void mb_masterDoSlot8(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(8, args);
}

static void mb_masterDoSlot9(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(9, args);
}

static void mb_masterDoSlot10(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(10, args);
}

static void mb_masterDoSlot11(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(11, args);
}

static void mb_masterDoSlot12(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(12, args);
}

static void mb_masterDoSlot13(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(13, args);
}

static void mb_masterDoSlot14(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(14, args);
}

static void mb_masterDoSlot15(XObject* sender, XVarList* args)
{
    (void)sender;
    mb_masterDoIdx(15, args);
}




/** @brief 重建输入/输出点网格（点数变更/重建连接时调用）。 */
static void mb_masterRebuildGrids(void)
{
    int i;
    if (!s_mb.inGridHost || !s_mb.outGridHost) return;
    for (i = 0; i < MB_SLAVE_MAX_POINTS; ++i) {
        if (s_mb.inCells[i]) {
            XClassDelete((XClass*)s_mb.inCells[i]);
            s_mb.inCells[i] = NULL;
        }
        if (s_mb.outCells[i]) {
            XClassDelete((XClass*)s_mb.outCells[i]);
            s_mb.outCells[i] = NULL;
        }
    }
    for (i = 0; i < s_mb.inCount; ++i) {
        MbDiLamp* cell = (MbDiLamp*)XMalloc_System(sizeof(MbDiLamp));
        if (!cell) break;
        MbDiLamp_init(cell, s_mb.inGridHost, i);
        XWidget_setGeometry((XWidget*)cell,
                            MB_GRID_X0 + (i % MB_GRID_COLS) * MB_CELL_W,
                            (i / MB_GRID_COLS) * MB_CELL_H,
                            MB_CELL_W - 4, MB_CELL_H - 4);
        s_mb.inCells[i] = cell;
    }
    for (i = 0; i < s_mb.outCount; ++i) {
        /* [2026-10-13 图形化] DO 单元=MbDoLamp 圆圈（绿=1 红=0 灰=未连接），
         * checkable 单击翻转→clicked→per-index 槽写线圈；未连接 setEnabled
         * false 禁点。 */
        MbDoLamp* cell = (MbDoLamp*)XMalloc_System(sizeof(MbDoLamp));
        if (!cell) break;
        MbDoLamp_init(cell, s_mb.outGridHost, i);
        XWidget_setGeometry((XWidget*)cell,
                            MB_GRID_X0 + (i % MB_GRID_COLS) * MB_CELL_W,
                            (i / MB_GRID_COLS) * MB_CELL_H,
                            MB_CELL_W - 4, MB_CELL_H - 4);
        {
            static void (*const kDoSlots[16])(XObject*, XVarList*) = {
                mb_masterDoSlot0, mb_masterDoSlot1, mb_masterDoSlot2,
                mb_masterDoSlot3, mb_masterDoSlot4, mb_masterDoSlot5,
                mb_masterDoSlot6, mb_masterDoSlot7, mb_masterDoSlot8,
                mb_masterDoSlot9, mb_masterDoSlot10, mb_masterDoSlot11,
                mb_masterDoSlot12, mb_masterDoSlot13, mb_masterDoSlot14,
                mb_masterDoSlot15
            };
            if (i < 16)
                XObject_connect_1((XObject*)cell,
                                  (size_t)XAbstractButton_clicked_signal(
                                      (XAbstractButton*)NULL, false),
                                  (XObject*)s_mb.outGridHost, kDoSlots[i],
                                  XConnectionType_Direct);
        }
        s_mb.outCells[i] = cell;
    }
}

/** @brief 单事务模式多选框槽：记忆开关态（写路径按此态决定在途读
 *  未完成时让路暂存，还是沿用直发+超时重试级联的旧行为）。 */
static void mb_masterSingleTxSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_args_1(args, bool, checked);
        s_mb.singleTxMode = checked;
    }
}

/** @brief 输出全开/全关：逐格置勾选态并写模块（sender=allOnBtn 判向）。 */
static void mb_masterAllSlot(XObject* sender, XVarList* args)
{
    bool on;
    int i;
    (void)args;
    fprintf(stderr, "[ALL] slot on=%d device=%p\n",
            (int)(sender == (XObject*)s_mb.allOnBtn), (void*)s_mb.device);
    if (!s_mb.device) return;
    on = (sender == (XObject*)s_mb.allOnBtn);
    s_mb.coilSyncBack = true; /* 批量置位不触发差量写。 */
    for (i = 0; i < s_mb.outCount && i < MB_SLAVE_MAX_POINTS; ++i) {
        if (s_mb.outCells[i]) {
            XAbstractButton_setChecked((XAbstractButton*)s_mb.outCells[i], on);
            XWidget_update((XWidget*)s_mb.outCells[i]);
        }
        if (on) s_mbDoWritten[i >> 4] |= (uint16_t)(1u << (i & 15));
        else    s_mbDoWritten[i >> 4] &= (uint16_t)~(1u << (i & 15));
    }
    s_mb.coilSyncBack = false;
    /* [2026-10-13] 模块对 FC5 连发只收末帧（实测 16 连发仅 DO15 生效）——
     * 全开/全关改单帧 FC15 写多线圈。 */
    {
        XModbusDataUnit* unit =
            XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                      XModbusCoils, 0,
                                      (size_t)s_mb.outCount);
        XModbusReply* reply;
        if (!unit) return;
        for (i = 0; i < s_mb.outCount; ++i)
            XModbusDataUnit_setValue(unit, (size_t)i, on ? 1u : 0u);
        /* [写让路+单事务 2026-10-15] 同单点翻转：单事务模式下在途读
           先让路暂存，回读完成空闲槽补发——远程 IO 单事务从机忙于
           应答轮询时会丢弃交叠写帧（全开掉全关直接根因）。 */
        if (s_mb.singleTxMode && s_mb.pendingReply) {
            if (s_mb.deferredWriteUnit)
                XClassDelete(s_mb.deferredWriteUnit);
            s_mb.deferredWriteUnit = unit;
            return;
        }
        reply = XModbusClient_sendWriteRequest((XModbusClient*)s_mb.device,
                                               unit,
                                               XSpinBox_value(s_mb.unitSpin));
        XClassDelete(unit);
        if (reply) {
            ++s_mb.reqCount;
            s_mb.retiredWriteReply = reply; /* 回收定时器延迟删。 */
            XObject_connect_1((XObject*)reply,
                              (size_t)XModbusReply_finished_signal(reply),
                              (XObject*)s_mb.root, mb_masterWriteReplyDone,
                              XConnectionType_Direct);
        } else {
            ++s_mb.errCount;
        }
    }
}

/** @brief 输出点翻转：写单线圈。 */
static void mb_masterWriteCoil(int index, bool on)
{
    fprintf(stderr, "[WC] idx=%d on=%d\n", index, (int)on);
    XModbusDataUnit* unit;
    XModbusReply* reply;
    if (!s_mb.device || index < 0 || index >= s_mb.outCount) return;
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusCoils, (uint16_t)index, 1);
    if (!unit) return;
    XModbusDataUnit_setValue(unit, 0, on ? 1u : 0u);
    /* [写让路+单事务 2026-10-15] 同全开/全关：单事务模式下在途读先
     * 让路暂存（后写胜出），回读完成空闲槽补发；关=沿用直发+1s 超时
     * ×重试级联的旧行为（简单从机丢交叠写帧时靠重试兜底）。 */
    if (s_mb.singleTxMode && s_mb.pendingReply) {
        if (s_mb.deferredWriteUnit)
            XClassDelete(s_mb.deferredWriteUnit);
        s_mb.deferredWriteUnit = unit;
        return;
    }
    reply = XModbusClient_sendWriteRequest((XModbusClient*)s_mb.device,
                                           unit, XSpinBox_value(s_mb.unitSpin));
    fprintf(stderr, "[WC] send reply=%p\n", (void*)reply);
    XClassDelete(unit);
    if (reply) {
        ++s_mb.reqCount;
        /* [UAF 根修] 不可即刻 deleteLater：client 超时定时器仍持有该
         * Reply 引用，提前释放=定时器回调 UAF。挂 finished 自回收。 */
        XObject_connect_1((XObject*)reply,
                          (size_t)XModbusReply_finished_signal(reply),
                          (XObject*)s_mb.root, mb_masterWriteReplyDone,
                          XConnectionType_Direct);
    } else {
        ++s_mb.errCount;
    }
    /* 本地即时反馈（勾选态已由 toggled 置位；应答结果由统计行反映）。 */
}

/** @brief env XGUI_DEMO_MB_DO 程序化勾选槽：解析"0,3,7"式索引序列，
 *  逐个 XAbstractButton_click（等价用户点击；联调/回归用）。 */
static XTimer* s_mbDoAutoTimer = NULL;
static void mb_masterDoAutoSlot(void* userData, XTimerData* timer)
{
    const char* p;
    (void)userData; (void)timer;
    if (!s_mb.doAutoList) {
        if (s_mbDoAutoTimer) { XTimer_stop_base(s_mbDoAutoTimer); }
        return;
    }
    {
        const char* ap = XString_toUtf8(s_mb.doAutoList);
        if (ap && (strcmp(ap, "all") == 0 || strcmp(ap, "all-off") == 0)) {
            XObject* target = (strcmp(ap, "all") == 0)
                                  ? (XObject*)s_mb.allOnBtn
                                  : (XObject*)s_mb.allOffBtn;
            fprintf(stderr, "[AUTO-DO] %s click\n", ap);
            if (target)
                XAbstractButton_click((XAbstractButton*)target);
            if (s_mbDoAutoTimer) {
                XTimer_stop_base(s_mbDoAutoTimer);
                XObject_deleteLater((XObject*)s_mbDoAutoTimer);
                s_mbDoAutoTimer = NULL;
            }
            return;
        }
    }
    p = XString_toUtf8(s_mb.doAutoList);
    while (p && *p) {
        int idx = atoi(p);
        if (idx >= 0 && idx < s_mb.outCount && s_mb.outCells[idx]) {
                    XAbstractButton_click((XAbstractButton*)s_mb.outCells[idx]);
            XPrintf("XGuiWindowDemo: modbus autoDO %d clicked\n", idx);
        }
        p = strchr(p, ',');
        if (p) ++p;
    }
    /* 一次性序列：勾完即停定时器。回调栈内不可同步删自身
     * （XTimer_timeout_signal 返回后仍访问对象——ASan UAF 实证），
     * 交 deleteLater 异步回收（XObject 派生走异步释放约定）。 */
    if (s_mbDoAutoTimer) {
        XTimer_stop_base(s_mbDoAutoTimer);
        XObject_deleteLater((XObject*)s_mbDoAutoTimer);
        s_mbDoAutoTimer = NULL;
    }
}

/** @brief 发起一轮输入点（DiscreteInputs）/线圈（Coils）轮询请求。 */
static void mb_masterPollOnce(void)
{
    XModbusReply* reply;
    bool readCoils;
    if (!s_mb.device || s_mb.pendingReply) return; /* 上一请求在途。 */
    /* [2026-10-13 DO 状态回读] 轮询相位交替：DI（模块→UI）与 DO 线圈
     * （模块勾选框回显——用户要求输出线圈按位显示真实状态）。 */
    readCoils = s_mb.coilPhase && s_mb.outCount > 0;
    if (!readCoils && s_mb.inCount <= 0 && s_mb.outCount > 0)
        readCoils = true;
    if (!readCoils && s_mb.inCount <= 0) return;
    if (readCoils) {
        if (!s_mb.coilUnit) {
            s_mb.coilUnit = XModbusDataUnit_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, XModbusCoils, 0,
                (size_t)s_mb.outCount);
            if (!s_mb.coilUnit) return;
        }
        reply = XModbusClient_sendReadRequest((XModbusClient*)s_mb.device,
                                              s_mb.coilUnit,
                                              XSpinBox_value(s_mb.unitSpin));
    } else {
        if (!s_mb.pollUnit) {
            s_mb.pollUnit = XModbusDataUnit_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE, XModbusDiscreteInputs, 0,
                (size_t)s_mb.inCount);
            if (!s_mb.pollUnit) return;
        }
        reply = XModbusClient_sendReadRequest((XModbusClient*)s_mb.device,
                                              s_mb.pollUnit,
                                              XSpinBox_value(s_mb.unitSpin));
    }
    if (reply) {
        ++s_mb.reqCount;
        s_mb.coilPhaseWasCoils = readCoils;
        s_mb.pendingReply = reply;
        XObject_connect_1((XObject*)reply,
                          (size_t)XModbusReply_finished_signal(reply),
                          (XObject*)s_mb.root, mb_masterOnReply,
                          XConnectionType_Direct);
    } else {
        ++s_mb.errCount;
    }
    s_mb.coilPhase = readCoils ? false : true; /* 下一拍换相。 */
}

/** @brief 轮询定时器回调（XTimer 显式对象）。 */
/** @brief 1s 周期回收定时器：独立事件拍删 finished Reply——DeferredDelete
 *  会撞嵌套泵（repaint 内 readyRead 交错，ASan UAF 实证），改独立拍。 */
static void mb_masterReclaimCb(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    if (s_mb.retiredReply) {
        XObject_deleteLater((XObject*)s_mb.retiredReply);
        s_mb.retiredReply = NULL;
    }
    if (s_mb.retiredWriteReply) {
        XObject_deleteLater((XObject*)s_mb.retiredWriteReply);
        s_mb.retiredWriteReply = NULL;
    }
}

static void mb_masterPollTimerCb(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    ++s_mb.timerTick;
    mb_masterPollOnce();
}

/** @brief 轮询周期输入变化：即时改写运行中定时器周期（XTimer 周期
 *  触发重装订时读当前 interval，下一拍即生效，无需重启）。 */
static void mb_masterPollPeriodSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (s_mb.pollTimer && s_mb.pollSpin)
        XTimer_setInterval(s_mb.pollTimer,
                           (size_t)XSpinBox_value(s_mb.pollSpin));
}

/** @brief 写应答完成：回收一次性 Reply（防与 client 超时定时器竞争 UAF）。 */
static void mb_masterWriteReplyDone(XObject* sender, XVarList* args)
{
    (void)args;
    (void)sender;
    /* [2026-10-13] 暂不回收写 Reply（演示页生命周期兜底）：写应答与
     * DI 轮询应答在同 socket 交错到达，立即/延迟删均与 client 内部
     * pending/timer 状态竞争（ASan UAF 实证）。泄漏量=160B×点击次数。 */
}

/** @brief 收读应答：刷新输入点网格 + 回收 Reply。 */
static void mb_masterOnReply(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (s_mb.pendingReply) {
        XModbusReply* reply = s_mb.pendingReply;
        s_mb.pendingReply = NULL;
        s_mb.retiredReply = reply; /* 延迟回收：嵌套泵（repaint）内不可删。 */
        if (XModbusReply_error(reply) == XModbusDevice_NoError) {
            const XModbusDataUnit* result = XModbusReply_result_const(reply);
            if (result && s_mb.coilPhaseWasCoils) {
                /* DO 线圈回读：回填勾选框（勾选态=模块真实线圈位）。 */
                int i;
                size_t n = XModbusDataUnit_valueCount(result);
                if (n > (size_t)s_mb.outCount) n = (size_t)s_mb.outCount;
                s_mb.coilSyncBack = true; /* 抑制差量写回环。 */
                for (i = 0; i < (int)n; ++i) {
                    uint16_t v = XModbusDataUnit_value(result, (size_t)i);
                    if (s_mb.outCells[i]) {
                        XAbstractButton_setChecked(
                            (XAbstractButton*)s_mb.outCells[i], v != 0);
                        /* 位图同步：模块态即已写态。 */
                        if (v) s_mbDoWritten[i >> 4] |= (uint16_t)(1u << (i & 15));
                        else    s_mbDoWritten[i >> 4] &= (uint16_t)~(1u << (i & 15));
                        XWidget_update((XWidget*)s_mb.outCells[i]);
                    }
                }
                s_mb.coilSyncBack = false;
            } else if (result) {
                int i;
                size_t n = XModbusDataUnit_valueCount(result);
                if (n > (size_t)s_mb.inCount) n = (size_t)s_mb.inCount;
                for (i = 0; i < (int)n; ++i) {
                    uint16_t v = XModbusDataUnit_value(result, (size_t)i);
                    if (s_mb.inCells[i])
                        MbDiLamp_setOn(s_mb.inCells[i], v != 0);
                }
            }
        } else {
            ++s_mb.errCount;
        }
        XObject_deleteLater((XObject*)reply);
    }
    /* [写让路 2026-10-15] 读应答完成=空闲槽：补发暂存的写请求（全开/
       全关/单点翻转在轮询在途时到达的写，此前被暂存让路）。空闲槽发出
       保证单事务从机只见串行请求，写不丢不卡。 */
    if (s_mb.deferredWriteUnit && !s_mb.pendingReply) {
        XModbusReply* wreply = XModbusClient_sendWriteRequest(
            (XModbusClient*)s_mb.device, s_mb.deferredWriteUnit,
            XSpinBox_value(s_mb.unitSpin));
        if (wreply) {
            ++s_mb.reqCount;
            s_mb.retiredWriteReply = wreply; /* 回收定时器延迟删。 */
            XObject_connect_1((XObject*)wreply,
                              (size_t)XModbusReply_finished_signal(wreply),
                              (XObject*)s_mb.root, mb_masterWriteReplyDone,
                              XConnectionType_Direct);
        } else {
            ++s_mb.errCount;
        }
        XClassDelete(s_mb.deferredWriteUnit);
        s_mb.deferredWriteUnit = NULL;
    }
    /* 刷新统计行。 */
    if (s_mb.statLabel) {
        char buf[96];
        snprintf(buf, sizeof(buf),
                 "\xE8\xAF\xB7\xE6\xB1\x82: %u  \xE9\x94\x99\xE8\xAF\xAF: %u",
                 s_mb.reqCount, s_mb.errCount); /* 请求: n 错误: n */
        XLabel_setText_2(s_mb.statLabel, buf);
    }
}

static void mb_masterStopEngine(void)
{
    if (s_mb.pollTimer) {
        XTimer_stop_base(s_mb.pollTimer);
        XClassDelete((XClass*)s_mb.pollTimer);
        s_mb.pollTimer = NULL;
    }
    /* [2026-10-13 断开 UAF 根修二] Reply 一律不在此 deleteLater：
     * 在途 pendingReply 断开时会经 client clearAllPendingRequests 走
     * setError→finished→我们的 retire 槽（回收定时器随后删）；已 retire
     * 的可能已被回收定时器 deleteLater 过——同指针二次 deleteLater=
     * DeferredDelete 双排=处理已 free 对象（ASan 崩点实证）。 */
    s_mb.pendingReply = NULL;
    if (s_mb.deferredWriteUnit) {
        XClassDelete(s_mb.deferredWriteUnit);
        s_mb.deferredWriteUnit = NULL;
    }
    if (s_mb.pollUnit) {
        XClassDelete(s_mb.pollUnit);
        s_mb.pollUnit = NULL;
    }
    if (s_mb.coilUnit) {
        XClassDelete(s_mb.coilUnit);
        s_mb.coilUnit = NULL;
    }
    s_mb.coilPhase = false;
    s_mb.coilPhaseWasCoils = false;
    if (s_mb.device) {
        /* [2026-10-13 断开 UAF 根修] 先关自动重连再断：主动断开也会走
         * socket disconnected 回调，autoReconnect 开着时库内会挂 2s
         * 重连定时器——重连触发时 device 已被本函数 deleteLater=UAF
         * （用户点断开秒崩实证）。 */
        XModbusClient_setAutoReconnect((XModbusClient*)s_mb.device, false);
        XModbusDevice_disconnectDevice(s_mb.device);
        XObject_deleteLater(s_mb.device);
        s_mb.device = NULL;
    }
    /* [连接门控] 断开后 DO 圆圈禁点（灰圈）。 */
    {
        int di;
        for (di = 0; di < MB_SLAVE_MAX_POINTS; ++di)
            if (s_mb.outCells[di])
                XWidget_setEnabled((XWidget*)s_mb.outCells[di], false);
    }
    if (s_mb.stateLabel)
        XLabel_setText_2(s_mb.stateLabel,
                         "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5"); /* 未连接 */
}

/** @brief 按当前面板配置建引擎并连接（TCP/串口分支）。 */
static void mb_masterStartEngine(void)
{
    int channel = s_mb.channelCombo
                      ? XComboBox_currentIndex(s_mb.channelCombo) : 0;
    XModbusDevice* dev = NULL;
    XVariant* var;

    mb_masterStopEngine();

#if XMODBUS_TCP_ON
    if (channel == MB_MASTER_TCP) {
        XModbusTcpClient* cli = XModbusTcpClient_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE);
        if (!cli) return;
        dev = (XModbusDevice*)cli;
        {
            const char* addr = s_mb.tcpAddrEdit
                ? XLineEdit_text(s_mb.tcpAddrEdit) : "127.0.0.1";
            int port = s_mb.tcpPortEdit ? atoi(XLineEdit_text(s_mb.tcpPortEdit))
                                        : 15020;
            if (port <= 0 || port > 65535) port = 15020;
            var = XString_toVariant(XString_create_utf8(addr));
            if (var) {
                XModbusDevice_setConnectionParameter_move(dev,
                    XModbusDevice_NetworkAddressParameter, var);
                var = NULL;
            }
            var = XVariant_create_int(port);
            if (var) {
                XModbusDevice_setConnectionParameter_move(dev,
                    XModbusDevice_NetworkPortParameter, var);
                var = NULL;
            }
        }
    }
#endif
#if XMODBUS_RTU_ON
    if (channel == MB_MASTER_RTU) {
        XModbusRtuSerialClient* cli = XModbusRtuSerialClient_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE);
        if (!cli) return;
        dev = (XModbusDevice*)cli;
        {
            const char* name = s_mb.serialNameEdit
                ? XLineEdit_text(s_mb.serialNameEdit) : "/dev/ttyS1";
            int baud = 9600;
            if (s_mb.serialBaudCombo)
                baud = atoi(XComboBox_currentText(s_mb.serialBaudCombo));
            if (baud <= 0) baud = 9600;
            var = XString_toVariant(XString_create_utf8(name));
            if (var) {
                XModbusDevice_setConnectionParameter_move(dev,
                    XModbusDevice_SerialPortNameParameter, var);
                var = NULL;
            }
            var = XVariant_create_int(baud);
            if (var) {
                XModbusDevice_setConnectionParameter_move(dev,
                    XModbusDevice_SerialBaudRateParameter, var);
                var = NULL;
            }
            var = XVariant_create_int(XSerialPort_Data8);
            if (var) {
                XModbusDevice_setConnectionParameter_move(dev,
                    XModbusDevice_SerialDataBitsParameter, var);
                var = NULL;
            }
            var = XVariant_create_int(XSerialPort_NoParity);
            if (var) {
                XModbusDevice_setConnectionParameter_move(dev,
                    XModbusDevice_SerialParityParameter, var);
                var = NULL;
            }
            var = XVariant_create_int(XSerialPort_OneStop);
            if (var) {
                XModbusDevice_setConnectionParameter_move(dev,
                    XModbusDevice_SerialStopBitsParameter, var);
                var = NULL;
            }
        }
    }
#endif
    if (!dev) return;

    /* 真机 IO 模块多带单连接/请求限额保护，周期断连由库内自动重连承接。 */
    XModbusClient_setAutoReconnect((XModbusClient*)dev, true);
    XModbusClient_setReconnectInterval((XModbusClient*)dev, 2000);
    if (XModbusDevice_connectDevice(dev)) {
        s_mb.device = dev;
        s_mb.pollTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (s_mb.pollTimer) {
            XTimer_setInterval(s_mb.pollTimer,
                (size_t)(s_mb.pollSpin ? XSpinBox_value(s_mb.pollSpin)
                                       : 1000));
            XTimer_setTimerCallback(s_mb.pollTimer, mb_masterPollTimerCb);
            XTimer_setUserData(s_mb.pollTimer, NULL);
            XTimer_start_base(s_mb.pollTimer);
        }
        if (s_mb.stateLabel) {
            XLabel_setText_2(s_mb.stateLabel,
                             "\xE5\xB7\xB2\xE8\xBF\x9E\xE6\x8E\xA5"
                             "(\xE8\xBD\xAE\xE8\xAF\xA2\xE4\xB8\xAD)");
                             /* 已连接(轮询中) */
        }
        XMemset(s_mbDoWritten, 0, sizeof(s_mbDoWritten)); /* 重连后模块线圈态=初始 0。 */
        /* [连接门控] 连接成功 DO 圆圈可点。 */
        {
            int di;
            for (di = 0; di < s_mb.outCount && di < MB_SLAVE_MAX_POINTS; ++di)
                if (s_mb.outCells[di])
                    XWidget_setEnabled((XWidget*)s_mb.outCells[di], true);
        }
        XPushButton_setText_2(s_mb.connectBtn,
                              "\xE6\x96\xAD\xE5\xBC\x80"); /* 断开 */
        XObject_connect_1((XObject*)dev,
                          (size_t)XModbusDevice_stateChanged_signal(dev,
                              XModbusDevice_UnconnectedState),
                          (XObject*)s_mb.root, mb_masterStateSlot,
                          XConnectionType_Direct);
    } else {
        XObject_deleteLater(dev);
        if (s_mb.stateLabel)
            XLabel_setText_2(s_mb.stateLabel,
                             "\xE8\xBF\x9E\xE6\x8E\xA5\xE5\xA4\xB1\xE8\xB4\xA5");
                             /* 连接失败 */
    }
}

/** @brief 断开回归联调槽：5 秒后程序化点断开。 */
static void mb_masterDisconnectAutoSlot(void* userData, XTimerData* timer)
{
    (void)userData; (void)timer;
    if (s_mb.connectBtn)
        XAbstractButton_click((XAbstractButton*)s_mb.connectBtn);
}

/** @brief 设备状态槽：断开（含真机踢线）时清在途请求、复位 UI，
 *  自动重连由 XModbusClient 内置定时器承担（重连成功会再发 connected）。 */
static void mb_masterStateSlot(XObject* sender, XVarList* args)
{
    (void)sender;
    if (args) {
        XVarList_args_1(args, int, state);
        (void)state;
    }
    if (s_mb.device &&
        XModbusDevice_state(s_mb.device) == XModbusDevice_UnconnectedState) {
        if (s_mb.pendingReply) {
            XObject_deleteLater((XObject*)s_mb.pendingReply);
            s_mb.pendingReply = NULL;
        }
        if (s_mb.stateLabel)
            XLabel_setText_2(s_mb.stateLabel,
                             "\xE6\x96\xAD\xE7\xBA\xBF\xE9\x87\x8D\xE8\xBF\x9E\xE4\xB8\xAD");
                             /* 断线重连中 */
    }
}

/** @brief 连接/断开按钮槽。 */
static void mb_masterConnectSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (s_mb.device) {
        mb_masterStopEngine();
        XPushButton_setText_2(s_mb.connectBtn,
                              "\xE8\xBF\x9E\xE6\x8E\xA5"); /* 连接 */
    } else {
        /* 记录远程 IO 点数并重建网格。 */
        s_mb.inCount = s_mb.inCountSpin ? XSpinBox_value(s_mb.inCountSpin) : 8;
        s_mb.outCount = s_mb.outCountSpin
                            ? XSpinBox_value(s_mb.outCountSpin) : 8;
        if (s_mb.inCount > MB_SLAVE_MAX_POINTS) s_mb.inCount = MB_SLAVE_MAX_POINTS;
        if (s_mb.outCount > MB_SLAVE_MAX_POINTS) s_mb.outCount = MB_SLAVE_MAX_POINTS;
        mb_masterRebuildGrids();
        mb_masterStartEngine();
    }
}

/** @brief 主站通道切换槽：TCP/串口参数行（含标签）可见性互换。 */
static void mb_masterChannelSlot(XObject* sender, XVarList* args)
{
    int idx;
    (void)sender; (void)args;
    if (!s_mb.channelCombo) return;
    idx = XComboBox_currentIndex(s_mb.channelCombo);
    if (s_mb.tcpLbl)
        XWidget_setVisible((XWidget*)s_mb.tcpLbl, idx == MB_MASTER_TCP);
    if (s_mb.singleTxCheck)
        XWidget_setVisible((XWidget*)s_mb.singleTxCheck, idx == MB_MASTER_TCP);
    if (s_mb.tcpAddrEdit)
        XWidget_setVisible((XWidget*)s_mb.tcpAddrEdit, idx == MB_MASTER_TCP);
    if (s_mb.tcpPortEdit)
        XWidget_setVisible((XWidget*)s_mb.tcpPortEdit, idx == MB_MASTER_TCP);
    if (s_mb.serialLbl)
        XWidget_setVisible((XWidget*)s_mb.serialLbl, idx == MB_MASTER_RTU);
    if (s_mb.serialNameEdit)
        XWidget_setVisible((XWidget*)s_mb.serialNameEdit,
                           idx == MB_MASTER_RTU);
    if (s_mb.serialBaudCombo)
        XWidget_setVisible((XWidget*)s_mb.serialBaudCombo,
                           idx == MB_MASTER_RTU);
}

/* ================================================================
 * 页 18：从站（Server）
 * ================================================================ */

static void mb_slaveRefreshPanel(void);

/** @brief 从站数据表重挂（点数变化/启动时）：四表按配置尺寸注册。 */
static void mb_slaveRebuildMap(void)
{
    XModbusDataUnitMap* map;
    XModbusDataUnit* unit;
    if (!s_mbSlave.server) return;
    /* [2026-10-14 键值根修] 四表键必须=真实寄存器类型（processRequest 按
     * FC 推导 type 查表）；结构体 XMemset 清零后键全落 XModbusInvalid=0，
     * 查表恒失配 → 主站所有读请求吃 IllegalDataAddress 异常（请求/错误
     * 同步计数实证）。 */
    s_mbSlave.inEnum = XModbusDiscreteInputs;
    s_mbSlave.outEnum = XModbusCoils;
    s_mbSlave.irEnum = XModbusInputRegisters;
    s_mbSlave.hrEnum = XModbusHoldingRegisters;
    map = XModbusDataUnitMap_create();
    if (!map) return;
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusDiscreteInputs, 0,
                                     (size_t)(s_mbSlave.inCount > 0
                                                  ? s_mbSlave.inCount : 1));
    if (unit) {
        XMap_insert_base((XMapBase*)map, &s_mbSlave.inEnum, unit);
        XClassDelete(unit);
    }
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusCoils, 0,
                                     (size_t)(s_mbSlave.outCount > 0
                                                  ? s_mbSlave.outCount : 1));
    if (unit) {
        XMap_insert_base((XMapBase*)map, &s_mbSlave.outEnum, unit);
        XClassDelete(unit);
    }
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusInputRegisters, 0,
                                     MB_SLAVE_REG_ROWS);
    if (unit) {
        XMap_insert_base((XMapBase*)map, &s_mbSlave.irEnum, unit);
        XClassDelete(unit);
    }
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusHoldingRegisters, 0,
                                     MB_SLAVE_REG_ROWS);
    if (unit) {
        XMap_insert_base((XMapBase*)map, &s_mbSlave.hrEnum, unit);
        XClassDelete(unit);
    }
    XModbusServer_setMap_ref_base(s_mbSlave.server, map); /* Ref: 移交所有权。 */
}

/** @brief 从站写事件（主站改写数据区）：计数 + 面板回显。 */
static void mb_slaveOnDataWritten(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    ++s_mbSlave.writeEvents;
    if (s_mbSlave.statLabel) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "\xE5\x86\x99\xE4\xBA\x8B\xE4\xBB\xB6: %u",
                 s_mbSlave.writeEvents); /* 写事件: n */
        XLabel_setText_2(s_mbSlave.statLabel, buf);
    }
    /* 面板回显：主站写入的数据区变化同步到编辑控件
     * （panelSuppress 抑制本端程序化 setData 的回调环）。 */
    if (!s_mbSlave.panelSuppress) {
        int table = s_mbSlave.tableCombo
                        ? XComboBox_currentIndex(s_mbSlave.tableCombo) : 0;
        int i;
        if (table <= 1) {
            int count = (table == 0) ? s_mbSlave.outCount : s_mbSlave.inCount;
            XModbusRegisterType type =
                (table == 0) ? XModbusCoils : XModbusDiscreteInputs;
            /* [全开掉线根修 2026-10-15] 回显逐格 setChecked 会同步触发
               toggled 槽，槽内全列扫描会把未更新格的旧态写回数据区
               （FC15 写 8 线圈后模型被冲成部分旧态=全开数秒后整体掉
               OFF 的根因）。回显期间抑制位行 toggled 回写。 */
            s_mbSlave.bitEchoSuppress = 1;
            for (i = 0; i < count && i < MB_SLAVE_MAX_POINTS; ++i) {
                uint16_t v = 0;
                if (s_mbSlave.rows[i].bit) {
                    XModbusServer_data2(s_mbSlave.server, type,
                                        (uint16_t)i, &v);
                    XCheckBox_setChecked(s_mbSlave.rows[i].bit, v != 0);
                }
            }
            s_mbSlave.bitEchoSuppress = 0;
        } else {
            XModbusRegisterType type =
                (table == 2) ? XModbusInputRegisters
                             : XModbusHoldingRegisters;
            for (i = 0; i < MB_SLAVE_REG_ROWS; ++i) {
                uint16_t v = 0;
                char text[8];
                if (s_mbSlave.rows[i].reg) {
                    XModbusServer_data2(s_mbSlave.server, type,
                                        (uint16_t)i, &v);
                    snprintf(text, sizeof(text), "%u", (unsigned)v);
                    XLineEdit_setText(s_mbSlave.rows[i].reg, text);
                }
            }
        }
    }
}

/** @brief 按当前下拉选表重建数据面板行（cellWidget 内嵌编辑控件）。 */
static void mb_slaveRebuildPanel(void)
{
    int table = s_mbSlave.tableCombo
                    ? XComboBox_currentIndex(s_mbSlave.tableCombo) : 0;
    int rows = 0;
    int i;
    char addr[12];
    if (!s_mbSlave.dataTable) return;

    /* 释放旧行控件（setCellWidget 所有权归表，clearContents 清）。 */
    XTableWidget_clearContents(s_mbSlave.dataTable);
    for (i = 0; i < (int)(sizeof(s_mbSlave.rows) / sizeof(s_mbSlave.rows[0]));
         ++i) {
        s_mbSlave.rows[i].bit = NULL;
        s_mbSlave.rows[i].reg = NULL;
    }

    switch (table) {
    case 0: rows = s_mbSlave.outCount; break;  /* DO 线圈。 */
    case 1: rows = s_mbSlave.inCount; break;   /* DI 离散输入。 */
    default: rows = MB_SLAVE_REG_ROWS; break;  /* IR/HR 寄存器。 */
    }
    if (rows > MB_SLAVE_MAX_POINTS) rows = MB_SLAVE_MAX_POINTS;
    XTableWidget_setRowCount(s_mbSlave.dataTable, rows > 0 ? rows : 1);

    for (i = 0; i < rows; ++i) {
        XTableWidgetItem item;
        snprintf(addr, sizeof(addr), "0x%04X", i);
        /* 第 0 列：地址（只读文本）。 */
        XMemset(&item, 0, sizeof(item));
        item.text = XString_create_utf8(addr);
        XTableWidget_setItem(s_mbSlave.dataTable, i, 0, &item);
        XClassDelete((XClass*)item.text);
        if (table <= 1) {
            /* 位表：内嵌 CheckBox，勾选即写数据区。 */
            XCheckBox* box = XCheckBox_create(s_mbSlave.root, 0);
            if (!box) continue;
            XCheckBox_setChecked(box, false);
            XTableWidget_setCellWidget(s_mbSlave.dataTable, i, 1,
                                       (XWidget*)box);
            XWidget_show((XWidget*)box);
            XObject_connect_1((XObject*)box,
                              (size_t)XAbstractButton_toggled_signal(
                                  (XAbstractButton*)box, false),
                              (XObject*)s_mbSlave.root, mb_slaveBitSlot,
                              XConnectionType_Direct);
            s_mbSlave.rows[i].bit = box;
        } else {
            /* 寄存器表：内嵌 LineEdit，回车提交。 */
            XLineEdit* edit = XLineEdit_create(s_mbSlave.root, 0);
            if (!edit) continue;
            XLineEdit_setText(edit, "0");
            XTableWidget_setCellWidget(s_mbSlave.dataTable, i, 1,
                                       (XWidget*)edit);
            XWidget_show((XWidget*)edit);
            XObject_connect_1((XObject*)edit,
                              (size_t)XLineEdit_editingFinished_signal(edit),
                              (XObject*)s_mbSlave.root, mb_slaveRegSlot,
                              XConnectionType_Direct);
            s_mbSlave.rows[i].reg = edit;
        }
    }
}

/** @brief 位行勾选同步：全列扫描差异写数据区（朴素口径，64 行微秒级）。
 *  @note  XCheckBox toggled 信号携带 checked 但不带行号，cellWidget 无
 *         per-item userdata 通道，采用整列轮询把面板状态写穿到数据区。 */
static void mb_slaveSyncBitsFromPanel(int table)
{
    int i;
    XModbusRegisterType type =
        (table == 0) ? XModbusCoils : XModbusDiscreteInputs;
    int count = (table == 0) ? s_mbSlave.outCount : s_mbSlave.inCount;
    if (!s_mbSlave.server) return;
    for (i = 0; i < count && i < MB_SLAVE_MAX_POINTS; ++i) {
        uint16_t cur = 0;
        bool want;
        if (!s_mbSlave.rows[i].bit) continue;
        want = XAbstractButton_isChecked((const XAbstractButton*)
                                             s_mbSlave.rows[i].bit);
        XModbusServer_data2(s_mbSlave.server, type, (uint16_t)i, &cur);
        if ((cur != 0) != want) {
            s_mbSlave.panelSuppress = 1;
            XModbusServer_setData2(s_mbSlave.server, type, (uint16_t)i,
                                   want ? 1u : 0u);
            s_mbSlave.panelSuppress = 0;
        }
    }
}

/** @brief 寄存器行回车提交：全行扫描差异写数据区。 */
static void mb_slaveSyncRegsFromPanel(int table)
{
    int i;
    XModbusRegisterType type =
        (table == 2) ? XModbusInputRegisters : XModbusHoldingRegisters;
    if (!s_mbSlave.server) return;
    for (i = 0; i < MB_SLAVE_REG_ROWS; ++i) {
        uint16_t cur = 0;
        int want;
        const char* text;
        if (!s_mbSlave.rows[i].reg) continue;
        text = XLineEdit_text(s_mbSlave.rows[i].reg);
        want = text ? atoi(text) : 0;
        if (want < 0) want = 0;
        if (want > 65535) want = 65535;
        XModbusServer_data2(s_mbSlave.server, type, (uint16_t)i, &cur);
        if (cur != (uint16_t)want) {
            s_mbSlave.panelSuppress = 1;
            XModbusServer_setData2(s_mbSlave.server, type, (uint16_t)i,
                                   (uint16_t)want);
            s_mbSlave.panelSuppress = 0;
        }
    }
}

/** @brief 面板整表回填（数据区→控件；主站写事件后调用）。 */
static void mb_slaveRefreshPanel(void)
{
    int table = s_mbSlave.tableCombo
                    ? XComboBox_currentIndex(s_mbSlave.tableCombo) : 0;
    int i;
    if (!s_mbSlave.dataTable || !s_mbSlave.server) return;
    if (table <= 1) {
        int count = (table == 0) ? s_mbSlave.outCount : s_mbSlave.inCount;
        XModbusRegisterType type =
            (table == 0) ? XModbusCoils : XModbusDiscreteInputs;
        for (i = 0; i < count && i < MB_SLAVE_MAX_POINTS; ++i) {
            uint16_t v = 0;
            if (s_mbSlave.rows[i].bit) {
                XModbusServer_data2(s_mbSlave.server, type, (uint16_t)i, &v);
                XCheckBox_setChecked(s_mbSlave.rows[i].bit, v != 0);
            }
        }
    } else {
        XModbusRegisterType type =
            (table == 2) ? XModbusInputRegisters : XModbusHoldingRegisters;
        for (i = 0; i < MB_SLAVE_REG_ROWS; ++i) {
            uint16_t v = 0;
            char text[8];
            if (s_mbSlave.rows[i].reg) {
                XModbusServer_data2(s_mbSlave.server, type, (uint16_t)i, &v);
                snprintf(text, sizeof(text), "%u", (unsigned)v);
                XLineEdit_setText(s_mbSlave.rows[i].reg, text);
            }
        }
    }
}

/** @brief 表类型切换槽：重建面板+回填。 */
static void mb_slaveTableSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveRebuildPanel();
    mb_slaveRefreshPanel();
}

/** @brief 位行勾选统一槽。 */
static void mb_slaveBitSlot(XObject* sender, XVarList* args)
{
    int table = s_mbSlave.tableCombo
                    ? XComboBox_currentIndex(s_mbSlave.tableCombo) : 0;
    (void)sender;
    if (s_mbSlave.bitEchoSuppress) {
        /* 回显 setChecked 触发的 toggled：模型已是目标值，仅同步勾选
           视觉——不得回写数据区（否则旧态冲掉主站刚写的新值）。 */
        return;
    }
    if (args) {
        XVarList_args_1(args, bool, checked);
        (void)checked;
    }
    mb_slaveSyncBitsFromPanel(table);
}

/** @brief 寄存器行回车统一槽。 */
static void mb_slaveRegSlot(XObject* sender, XVarList* args)
{
    int table = s_mbSlave.tableCombo
                    ? XComboBox_currentIndex(s_mbSlave.tableCombo) : 0;
    (void)sender; (void)args;
    mb_slaveSyncRegsFromPanel(table);
}

/** @brief 「按点数重建」按钮槽（点数 SpinBox 调整后应用）。 */
static void mb_slaveApplySlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    s_mbSlave.inCount = s_mbSlave.inCountSpin
                            ? XSpinBox_value(s_mbSlave.inCountSpin) : 8;
    s_mbSlave.outCount = s_mbSlave.outCountSpin
                             ? XSpinBox_value(s_mbSlave.outCountSpin) : 8;
    if (s_mbSlave.inCount > MB_SLAVE_MAX_POINTS)
        s_mbSlave.inCount = MB_SLAVE_MAX_POINTS;
    if (s_mbSlave.outCount > MB_SLAVE_MAX_POINTS)
        s_mbSlave.outCount = MB_SLAVE_MAX_POINTS;
    if (s_mbSlave.server) mb_slaveRebuildMap();
    mb_slaveRebuildPanel();
    mb_slaveRefreshPanel();
}

/** @brief 从站启动/停止。 */
static void mb_slaveStartStop(void)
{
    int channel = s_mbSlave.channelCombo
                      ? XComboBox_currentIndex(s_mbSlave.channelCombo) : 0;
    XModbusServer* srv = NULL;
    XVariant* var;

    if (s_mbSlave.server) {
        XModbusDevice_disconnectDevice((XModbusDevice*)s_mbSlave.server);
        XObject_deleteLater((XObject*)s_mbSlave.server);
        s_mbSlave.server = NULL;
        if (s_mbSlave.stateLabel)
            XLabel_setText_2(s_mbSlave.stateLabel,
                             "\xE5\xB7\xB2\xE5\x81\x9C\xE6\xAD\xA2"); /* 已停止 */
        XPushButton_setText_2(s_mbSlave.startBtn,
                              "\xE5\x90\xAF\xE5\x8A\xA8"); /* 启动 */
        return;
    }

    s_mbSlave.inCount = s_mbSlave.inCountSpin
                            ? XSpinBox_value(s_mbSlave.inCountSpin) : 8;
    s_mbSlave.outCount = s_mbSlave.outCountSpin
                             ? XSpinBox_value(s_mbSlave.outCountSpin) : 8;
    if (s_mbSlave.inCount > MB_SLAVE_MAX_POINTS)
        s_mbSlave.inCount = MB_SLAVE_MAX_POINTS;
    if (s_mbSlave.outCount > MB_SLAVE_MAX_POINTS)
        s_mbSlave.outCount = MB_SLAVE_MAX_POINTS;

#if XMODBUS_TCP_ON
    if (channel == 0) {
        XModbusTcpServer* ts = XModbusTcpServer_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE);
        if (!ts) return;
        srv = (XModbusServer*)ts;
        {
            int port = s_mbSlave.tcpPortEdit
                           ? atoi(XLineEdit_text(s_mbSlave.tcpPortEdit))
                           : 15020;
            if (port <= 0 || port > 65535) port = 15020;
            var = XVariant_create_int(port);
            if (var) {
                XModbusDevice_setConnectionParameter_move(
                    (XModbusDevice*)srv,
                    XModbusDevice_NetworkPortParameter, var);
                var = NULL;
            }
        }
    }
#endif
#if XMODBUS_RTU_ON
    if (channel == 1) {
        XModbusRtuSerialServer* rs = XModbusRtuSerialServer_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE);
        if (!rs) return;
        srv = (XModbusServer*)rs;
        {
            const char* name = s_mbSlave.serialNameEdit
                ? XLineEdit_text(s_mbSlave.serialNameEdit) : "/dev/ttyS1";
            int baud = 9600;
            if (s_mbSlave.serialBaudCombo)
                baud = atoi(XComboBox_currentText(s_mbSlave.serialBaudCombo));
            if (baud <= 0) baud = 9600;
            var = XString_toVariant(XString_create_utf8(name));
            if (var) {
                XModbusDevice_setConnectionParameter_move(
                    (XModbusDevice*)srv,
                    XModbusDevice_SerialPortNameParameter, var);
                var = NULL;
            }
            var = XVariant_create_int(baud);
            if (var) {
                XModbusDevice_setConnectionParameter_move(
                    (XModbusDevice*)srv,
                    XModbusDevice_SerialBaudRateParameter, var);
                var = NULL;
            }
        }
    }
#endif
    if (!srv) return;

    XModbusServer_setServerAddress(srv, XSpinBox_value(s_mbSlave.unitSpin));
    s_mbSlave.server = srv;
    mb_slaveRebuildMap();
    XObject_connect_1((XObject*)srv,
                      (size_t)XModbusServer_dataWritten_signal(NULL, 0, 0, 0),
                      (XObject*)s_mbSlave.root, mb_slaveOnDataWritten,
                      XConnectionType_Direct);
    if (XModbusDevice_connectDevice((XModbusDevice*)srv)) {
        if (s_mbSlave.stateLabel)
            XLabel_setText_2(s_mbSlave.stateLabel,
                             "\xE7\x9B\x91\xE5\x90\xAC\xE4\xB8\xAD");
                             /* 监听中 */
        XPushButton_setText_2(s_mbSlave.startBtn,
                              "\xE5\x81\x9C\xE6\xAD\xA2"); /* 停止 */
        mb_slaveRefreshPanel();
    } else {
        XObject_deleteLater((XObject*)srv);
        s_mbSlave.server = NULL;
        if (s_mbSlave.stateLabel)
            XLabel_setText_2(s_mbSlave.stateLabel,
                             "\xE5\x90\xAF\xE5\x8A\xA8\xE5\xA4\xB1\xE8\xB4\xA5");
                             /* 启动失败 */
    }
}

/** @brief 从站启动按钮槽。 */
static void mb_slaveStartSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveStartStop();
}

/** @brief 从站通道切换槽：参数行（含标签）可见性。 */
static void mb_slaveChannelSlot(XObject* sender, XVarList* args)
{
    int idx;
    (void)sender; (void)args;
    if (!s_mbSlave.channelCombo) return;
    idx = XComboBox_currentIndex(s_mbSlave.channelCombo);
    if (s_mbSlave.tcpLbl)
        XWidget_setVisible((XWidget*)s_mbSlave.tcpLbl, idx == 0);
    if (s_mbSlave.tcpPortEdit)
        XWidget_setVisible((XWidget*)s_mbSlave.tcpPortEdit, idx == 0);
    if (s_mbSlave.serialLbl)
        XWidget_setVisible((XWidget*)s_mbSlave.serialLbl, idx == 1);
    if (s_mbSlave.serialNameEdit)
        XWidget_setVisible((XWidget*)s_mbSlave.serialNameEdit, idx == 1);
    if (s_mbSlave.serialBaudCombo)
        XWidget_setVisible((XWidget*)s_mbSlave.serialBaudCombo, idx == 1);
}

/* ================================================================
 * 页面构建
 * ================================================================ */

XWidget* demo_page_modbus_master_build(XWidget* parent,
                                       DemoPageStatusFn statusFn, void* user)
{
    XLabel* title = NULL;
    (void)statusFn; (void)user;

    XMemset(&s_mb, 0, sizeof(s_mb));
    s_mb.inCount = 8;
    s_mb.outCount = 8;

    if (!parent) return (XWidget*)0;
    {
        /* [滚动承载 2026-10-14] 页面 root=堆叠布局分配几何（随窗口缩放）；
         * 内部套 ScrollArea（铺满 root，widgetResizable）+ 内容容器 body。
         * 全部控件原坐标继续挂在 body 上——高度不足时出竖滚动条，底部
         * 控件（寄存器表等）始终可达。s_mb.root 指向 body，后续创建
         * 代码零改动。 */
        XWidget* pageRoot = XWidget_create(parent, 0);
        if (!pageRoot) return (XWidget*)0;
        s_mb.scroll = XScrollArea_create(pageRoot, 0);
        if (!s_mb.scroll) { XClassDelete((XClass*)pageRoot); return (XWidget*)0; }
        XScrollArea_setWidgetResizable(s_mb.scroll, false);
        s_mb.body = XWidget_create(NULL, 0);
        /* [滚动残影根修 2026-10-15] body 持 Base 底色+autofill：滚动移动
           body 后，其矩形内一切增量重绘先由 body 自身底色擦除（含表格
           下方无控件覆盖带）——残影=旧位置像素无人清。视口 autofill 在
           增量路径存在父/子绘序倒挂（视口底色盖表格内容，实验实证），
           故底色挂 body 不挂视口。 */
        XWidget_setBackgroundRole(s_mb.body, XPaletteColorRole_Base);
        XWidget_setAutoFillBackground(s_mb.body, true);
        if (!s_mb.body) { XClassDelete((XClass*)pageRoot); return (XWidget*)0; }
        XScrollArea_setWidget(s_mb.scroll, s_mb.body);
        XWidget_setGeometry((XWidget*)pageRoot, 0, 0, 740, 624);
        s_mb.pageRoot = pageRoot;
        /* 控件挂载基准=body（s_mb.root 转指 body，后续创建代码零改动）；
         * build 返回 pageRoot（堆叠布局向它分配几何）。 */
        s_mb.root = s_mb.body;
    }

    mb_buildLabelAt(s_mb.root,
                    "Modbus \xE4\xB8\xBB\xE7\xAB\x99\xE8\xB0\x83\xE8\xAF\x95",
                    12, 8, 300, &title); /* Modbus 主站调试 */
    if (title) XLabel_setTextPixelSize(title, 16);

    /* 连接参数区。 */
    mb_buildLabelAt(s_mb.root,
                    "\xE9\x80\x9A\xE9\x81\x93", 12, 44, 56, NULL); /* 通道 */
    s_mb.channelCombo = mb_buildChannelCombo(s_mb.root, 72, 42, 160);
    XObject_connect_2((XObject*)s_mb.channelCombo,
                      XSignal(XComboBox_activated_signal),
                      mb_masterChannelSlot);
    /* [单事务模式 2026-10-15] TCP 通道专用多选框：勾选后写请求不与在途
     * 轮询读流水线并发——在途未完成时写排队（后写胜出），回读完成后
     * 补发。适配仅支持单事务处理的远程 IO 模块/从机。仅 TCP 模式显示
     * （RTU 串口本身即单事务半双工）。 */
    s_mb.singleTxCheck = XCheckBox_create(s_mb.root, 0);
    if (s_mb.singleTxCheck) {
        XAbstractButton_setText_2((XAbstractButton*)s_mb.singleTxCheck,
                                "\xE5\x8D\x95\xE4\xBA\x8B\xE5\x8A\xA1\xE6\xA8\xA1\xE5\xBC\x8F");
        /* 单事务模式 */
        XWidget_setGeometry((XWidget*)s_mb.singleTxCheck, 172, 102, 190, 24);
        XObject_connect_1((XObject*)s_mb.singleTxCheck,
                          (size_t)XAbstractButton_toggled_signal(
                              (XAbstractButton*)s_mb.singleTxCheck, false),
                          (XObject*)s_mb.root, mb_masterSingleTxSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)s_mb.singleTxCheck);
    }

    mb_buildLabelAt(s_mb.root,
                    "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8",
                    12, 74, 64, &s_mb.tcpLbl); /* 服务器 */
    s_mb.tcpAddrEdit = XLineEdit_create(s_mb.root, 0);
    if (s_mb.tcpAddrEdit) {
        /* [联调] env XGUI_DEMO_MB_HOST 预置服务器地址（真机远程 IO 直连）。 */
        {
            const char* hostEnv = XSystem_environment("XGUI_DEMO_MB_HOST");
            XLineEdit_setText(s_mb.tcpAddrEdit,
                              (hostEnv && hostEnv[0]) ? hostEnv
                                                      : "127.0.0.1");
        }
        XWidget_setGeometry((XWidget*)s_mb.tcpAddrEdit, 80, 72, 130, 24);
        XWidget_show((XWidget*)s_mb.tcpAddrEdit);
    }
    s_mb.tcpPortEdit = XLineEdit_create(s_mb.root, 0);
    if (s_mb.tcpPortEdit) {
        /* [联调] env XGUI_DEMO_MB_PORT 预置端口。 */
        {
            const char* portEnv = XSystem_environment("XGUI_DEMO_MB_PORT");
            XLineEdit_setText(s_mb.tcpPortEdit,
                              (portEnv && portEnv[0]) ? portEnv : "15020");
        }
        XWidget_setGeometry((XWidget*)s_mb.tcpPortEdit, 218, 72, 66, 24);
        XWidget_show((XWidget*)s_mb.tcpPortEdit);
    }
    mb_buildLabelAt(s_mb.root,
                    "\xE4\xB8\xB2\xE5\x8F\xA3", 12, 74, 56, &s_mb.serialLbl);
                    /* 串口 */
    if (s_mb.serialLbl)
        XWidget_setVisible((XWidget*)s_mb.serialLbl, false);
    s_mb.serialNameEdit = XLineEdit_create(s_mb.root, 0);
    if (s_mb.serialNameEdit) {
        XLineEdit_setText(s_mb.serialNameEdit, "/dev/ttyS1");
        XWidget_setGeometry((XWidget*)s_mb.serialNameEdit, 80, 72, 130, 24);
        XWidget_setVisible((XWidget*)s_mb.serialNameEdit, false);
    }
    s_mb.serialBaudCombo = mb_buildBaudCombo(s_mb.root, 218, 72, 80);
    XWidget_setVisible((XWidget*)s_mb.serialBaudCombo, false);

    mb_buildLabelAt(s_mb.root,
                    "\xE4\xBB\x8E\xE7\xAB\x99\xE5\x9C\xB0\xE5\x9D\x80",
                    12, 104, 80, NULL); /* 从站地址 */
    s_mb.unitSpin = mb_buildSpin(s_mb.root, 96, 102, 60, 1, 247, 1);

    /* 远程 IO 模块适配。 */
    mb_buildLabelAt(s_mb.root,
                    "\xE8\xBF\x9C\xE7\xA8\x8B IO \xE6\xA8\xA1\xE5\x9D\x97",
                    12, 134, 130, NULL); /* 远程 IO 模块 */
    mb_buildLabelAt(s_mb.root,
                    "\xE8\xBE\x93\xE5\x85\xA5\xE7\x82\xB9",
                    12, 162, 60, NULL); /* 输入点 */
    {
        const char* inEnv = XSystem_environment("XGUI_DEMO_MB_IN");
        s_mb.inCountSpin = mb_buildSpin(s_mb.root, 76, 160, 60, 0, 64,
                                        (inEnv && inEnv[0]) ? atoi(inEnv) : 8);
    }
    mb_buildLabelAt(s_mb.root,
                    "\xE8\xBE\x93\xE5\x87\xBA\xE7\x82\xB9",
                    162, 162, 60, NULL); /* 输出点 */
    {
        const char* outEnv = XSystem_environment("XGUI_DEMO_MB_OUT");
        s_mb.outCountSpin = mb_buildSpin(s_mb.root, 226, 160, 60, 0, 64,
                                         (outEnv && outEnv[0]) ? atoi(outEnv)
                                                               : 8);
    }
    mb_buildLabelAt(s_mb.root,
                    "\xE8\xBD\xAE\xE8\xAF\xA2(ms)",
                    12, 192, 80, NULL); /* 轮询(ms) */
    s_mb.pollSpin = mb_buildSpin(s_mb.root, 96, 190, 70, 10, 5000, 1000);
    XObject_connect_1((XObject*)s_mb.pollSpin,
                      (size_t)XSpinBox_valueChanged_signal(
                          (XSpinBox*)NULL),
                      (XObject*)s_mb.root, mb_masterPollPeriodSlot,
                      XConnectionType_Direct);
    /* [联调] env XGUI_DEMO_MB_POLL 预置轮询周期 ms（10..5000）。 */
    {
        const char* pollEnv = XSystem_environment("XGUI_DEMO_MB_POLL");
        if (pollEnv && pollEnv[0]) {
            int pv = atoi(pollEnv);
            if (pv >= 10 && pv <= 5000)
                XSpinBox_setValue(s_mb.pollSpin, pv);
        }
    }

    s_mb.connectBtn = XPushButton_create(s_mb.root, 0);
    if (s_mb.connectBtn) {
        XPushButton_setText_2(s_mb.connectBtn,
                              "\xE8\xBF\x9E\xE6\x8E\xA5"); /* 连接 */
        XWidget_setGeometry((XWidget*)s_mb.connectBtn, 252, 188, 90, 28);
        XObject_connect_2((XObject*)s_mb.connectBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_masterConnectSlot);
        XWidget_show((XWidget*)s_mb.connectBtn);
    }

    s_mb.stateLabel = XLabel_create(s_mb.root, 0);
    if (s_mb.stateLabel) {
        XLabel_setText_2(s_mb.stateLabel,
                         "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5"); /* 未连接 */
        XWidget_setGeometry((XWidget*)s_mb.stateLabel, 12, 224, 360, 22);
        XWidget_show((XWidget*)s_mb.stateLabel);
    }
    s_mb.statLabel = XLabel_create(s_mb.root, 0);
    if (s_mb.statLabel) {
        XLabel_setText_2(s_mb.statLabel,
                         "\xE8\xAF\xB7\xE6\xB1\x82: 0  \xE9\x94\x99\xE8\xAF"
                         "\xAF: 0"); /* 请求: 0 错误: 0 */
        XWidget_setGeometry((XWidget*)s_mb.statLabel, 12, 248, 360, 22);
        XWidget_show((XWidget*)s_mb.statLabel);
    }

    /* 输入点网格（只读指示）。 */
    mb_buildLabelAt(s_mb.root,
                    "\xE8\xBE\x93\xE5\x85\xA5\xE7\x82\xB9\xE7\x8A\xB6\xE6\x80"
                    "\x81(\xE5\x8F\xAA\xE8\xAF\xBB)",
                    12, 278, 180, NULL); /* 输入点状态(只读) */
    s_mb.inGridHost = XWidget_create(s_mb.root, 0);
    if (s_mb.inGridHost) {
        XWidget_setGeometry((XWidget*)s_mb.inGridHost, 12, 300, 360, 132);
        XWidget_show((XWidget*)s_mb.inGridHost);
    }
    /* 输出点网格（点击翻转）。 */
    mb_buildLabelAt(s_mb.root,
                    "\xE8\xBE\x93\xE5\x87\xBA\xE7\x82\xB9(\xE7\x82\xB9"
                    "\xE5\x87\xBB\xE7\xBF\xBB\xE8\xBD\xAC)",
                    400, 278, 160, NULL); /* 输出点(点击翻转) */
    s_mb.allOnBtn = XPushButton_create(s_mb.root, 0);
    if (s_mb.allOnBtn) {
        XPushButton_setText_2(s_mb.allOnBtn,
                              "\xE5\x85\xA8\xE5\xBC\x80"); /* 全开 */
        XWidget_setGeometry((XWidget*)s_mb.allOnBtn, 564, 276, 64, 24);
        XObject_connect_2((XObject*)s_mb.allOnBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_masterAllSlot);
        XWidget_show((XWidget*)s_mb.allOnBtn);
    }
    s_mb.allOffBtn = XPushButton_create(s_mb.root, 0);
    if (s_mb.allOffBtn) {
        XPushButton_setText_2(s_mb.allOffBtn,
                              "\xE5\x85\xA8\xE5\x85\xB3"); /* 全关 */
        XWidget_setGeometry((XWidget*)s_mb.allOffBtn, 636, 276, 64, 24);
        XObject_connect_2((XObject*)s_mb.allOffBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_masterAllSlot);
        XWidget_show((XWidget*)s_mb.allOffBtn);
    }
    s_mb.outGridHost = XWidget_create(s_mb.root, 0);
    if (s_mb.outGridHost) {
        XWidget_setGeometry((XWidget*)s_mb.outGridHost, 400, 300, 360, 132);
        XWidget_show((XWidget*)s_mb.outGridHost);
    }

    /* ---- 寄存器读写面板（IR/HR，16 进制） ---- */
    mb_buildLabelAt(s_mb.root,
                    "\xE5\xAF\x84\xE5\xAD\x98\xE5\x99\xA8\xE8\xAF\xBB\xE5\x86\x99"
                    "(16\xE8\xBF\x9B\xE5\x88\xB6)",
                    12, 444, 220, NULL); /* 寄存器读写(16进制) */
    s_mb.regTableCombo = XComboBox_create(s_mb.root, 0);
    if (s_mb.regTableCombo) {
        XComboBox_addItem_2(s_mb.regTableCombo,
                "IR \xE8\xBE\x93\xE5\x85\xA5\xE5\xAF\x84\xE5\xAD\x98\xE5\x99\xA8");
                /* IR 输入寄存器 */
        XComboBox_addItem_2(s_mb.regTableCombo,
                "HR \xE4\xBF\x9D\xE6\x8C\x81\xE5\xAF\x84\xE5\xAD\x98\xE5\x99\xA8");
                /* HR 保持寄存器 */
        XComboBox_setCurrentIndex(s_mb.regTableCombo, 1);
        XWidget_setGeometry((XWidget*)s_mb.regTableCombo, 240, 442, 170, 24);
        XObject_connect_2((XObject*)s_mb.regTableCombo,
                          XSignal(XComboBox_activated_signal),
                          mb_masterRegComboSlot);
        XWidget_show((XWidget*)s_mb.regTableCombo);
    }
    mb_buildLabelAt(s_mb.root,
                    "\xE8\xB5\xB7\xE5\xA7\x8B", 420, 444, 40, NULL); /* 起始 */
    s_mb.regAddrSpin = mb_buildSpin(s_mb.root, 464, 442, 60, 0, 65535, 0);
    XObject_connect_1((XObject*)s_mb.regAddrSpin,
                      (size_t)XSpinBox_valueChanged_signal(
                          (XSpinBox*)NULL),
                      (XObject*)s_mb.root, mb_masterRegComboSlot,
                      XConnectionType_Direct);
    mb_buildLabelAt(s_mb.root,
                    "\xE6\x95\xB0\xE9\x87\x8F", 534, 444, 40, NULL); /* 数量 */
    s_mb.regCountSpin = mb_buildSpin(s_mb.root, 578, 442, 56, 1, 16, 8);
    XObject_connect_1((XObject*)s_mb.regCountSpin,
                      (size_t)XSpinBox_valueChanged_signal(
                          (XSpinBox*)NULL),
                      (XObject*)s_mb.root, mb_masterRegComboSlot,
                      XConnectionType_Direct);
    {
        XPushButton* rb = XPushButton_create(s_mb.root, 0);
        if (rb) {
            XPushButton_setText_2(rb,
                    "\xE8\xAF\xBB"); /* 读 */
            XWidget_setGeometry((XWidget*)rb, 644, 440, 56, 26);
            XObject_connect_2((XObject*)rb,
                              XSignal(XAbstractButton_clicked_signal),
                              mb_masterRegReadSlot);
            XWidget_show((XWidget*)rb);
        }
    }
    /* 第二行：写地址+写值+写入 */
    mb_buildLabelAt(s_mb.root,
                    "\xE5\x86\x99", 12, 476, 30, NULL); /* 写 */
    s_mb.regWriteAddr = mb_buildSpin(s_mb.root, 46, 474, 60, 0, 65535, 0);
    mb_buildLabelAt(s_mb.root,
                    "\xE5\x80\xBC", 116, 476, 30, NULL); /* 值 */
    s_mb.regWriteValue = XLineEdit_create(s_mb.root, 0);
    if (s_mb.regWriteValue) {
        XLineEdit_setText(s_mb.regWriteValue, "0x0000");
        XWidget_setGeometry((XWidget*)s_mb.regWriteValue, 150, 474, 100, 24);
        XWidget_show((XWidget*)s_mb.regWriteValue);
    }
    {
        XPushButton* wb = XPushButton_create(s_mb.root, 0);
        if (wb) {
            XPushButton_setText_2(wb,
                    "\xE5\x86\x99\xE5\x85\xA5"); /* 写入 */
            XWidget_setGeometry((XWidget*)wb, 264, 472, 70, 26);
            XObject_connect_2((XObject*)wb,
                              XSignal(XAbstractButton_clicked_signal),
                              mb_masterRegWriteSlot);
            XWidget_show((XWidget*)wb);
        }
    }
    s_mb.regTable = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           s_mb.root, 0);
    if (s_mb.regTable) {
        static const char* const kHead[] = {
            "\xE5\x9C\xB0\xE5\x9D\x80",               /* 地址 */
            "\xE5\x80\xBC(16\xE8\xBF\x9B\xE5\x88\xB6,\xE5\x9B\xDE\xE8\xBD\xA6\xE5\x86\x99\xE5\x85\xA5)"
                /* 值(16进制,回车写入) */
        };
        XWidget_setGeometry((XWidget*)s_mb.regTable, 12, 502, 740, 190);
        XTableWidget_setColumnCount(s_mb.regTable, 2);
        XTableWidget_setHorizontalHeaderLabels(s_mb.regTable, kHead,
                (int)(sizeof(kHead) / sizeof(kHead[0])));
        XTableWidget_setRowCount(s_mb.regTable, MB_SLAVE_REG_ROWS);
        XWidget_show((XWidget*)s_mb.regTable);
        /* [根修 2026-10-13] 双击信号连接必须在表格创建之后：原先连接
         * 写在创建前（s_mb.regTable 尚为 NULL），connect 空挂——信号
         * 照发、无人接收，双击编辑永不触发。只连双击（Qt QTableWidget
         * 默认 editTriggers=DoubleClicked；此前 cellClicked 同连一槽
         * 致单击即开编辑框且多框并存，与 Qt 语义相悖）。 */
        XObject_connect_1((XObject*)s_mb.regTable,
                          (size_t)XTableWidget_cellDoubleClicked_signal(
                              (XTableWidget*)NULL),
                          (XObject*)s_mb.root, mb_masterRegDblSlot,
                          XConnectionType_Direct);
    }
    /* [程序化数量] env XGUI_DEMO_MB_COUNT=n：数量 SpinBox 置 n 并触发
       重建（动态列/行的程序化验证通道）。 */
    {
        const char* cntEnv = XSystem_environment("XGUI_DEMO_MB_COUNT");
        if (cntEnv && s_mb.regCountSpin) {
            int n = atoi(cntEnv);
            if (n >= 1 && n <= 16) {
                XSpinBox_setValue(s_mb.regCountSpin, n);
                mb_masterRegBuildRows(
                    (n + s_mb.regCols - 1) / s_mb.regCols);
                XPrintf("XGuiWindowDemo: modbus count=%d rows=%d\n", n,
                        s_mb.regDispRows);
            }
        }
    }
    mb_masterRegBuildRows(8);
    mb_masterRebuildGrids();

    /* [端到端联调入口] env XGUI_DEMO_MB_AUTOSTART=master：构建后程序化
     * 连接（XAbstractButton_click 走完整激活流程；回环联调用）。
     * env XGUI_DEMO_MB_DO=索引列表(逗号分隔)：连接后 2 秒逐个勾选 DO——
     * 手动开关输出的程序化等效路径（XAbstractButton_click）。 */
    {
        const char* autoRole = XSystem_environment("XGUI_DEMO_MB_AUTOSTART");
        if (autoRole && (strcmp(autoRole, "master") == 0 ||
                         strcmp(autoRole, "regread") == 0 ||
                         strcmp(autoRole, "disconnect") == 0) &&
            s_mb.connectBtn) {
            XAbstractButton_click((XAbstractButton*)s_mb.connectBtn);
            XPrintf("XGuiWindowDemo: modbus autostart %s done\n", autoRole);
        }
        if (autoRole && strcmp(autoRole, "regread") == 0 && s_mb.regAddrSpin) {
            /* [寄存器读回归] 连接后 3 秒程序化点「读」(HR0..7)。 */
            XTimer* tr = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            if (tr) {
                XTimer_setInterval(tr, 3000);
                XTimer_setTimerCallback(tr, mb_masterRegReadAutoSlot);
                XTimer_setUserData(tr, NULL);
                XTimer_start_base(tr);
            }
        }
        {
            /* [断开回归联调] env=XGUI_DEMO_MB_AUTOSTART=disconnect：
             * 连接后 5 秒程序化点断开（用户点击的等价路径）。 */
            const char* doEnv = XSystem_environment("XGUI_DEMO_MB_DO");
            if (doEnv && strcmp(doEnv, "all") == 0 && s_mb.allOnBtn) {
                /* 全开回归：5 秒后程序化点「全开」。 */
                XTimer* ta = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
                if (ta) {
                    XTimer_setInterval(ta, 5000);
                    XTimer_setTimerCallback(ta, mb_masterDoAutoSlot);
                    XTimer_setUserData(ta, NULL);
                    XTimer_start_base(ta);
                    s_mb.doAutoList = XString_create_utf8("all");
                    s_mbDoAutoTimer = ta;
                }
            } else if (doEnv && doEnv[0] && s_mb.outCount > 0) {
                /* 2 秒后逐个勾选（等首轮连接建立）。 */
                XTimer* t = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
                if (t) {
                    XTimer_setInterval(t, 2000);
                    XTimer_setTimerCallback(t, mb_masterDoAutoSlot);
                    XTimer_setUserData(t, NULL);
                    XTimer_start_base(t);
                    s_mb.doAutoList = XString_create_utf8(doEnv);
                    s_mbDoAutoTimer = t;
                }
            }
        }
    }
    /* [断开回归联调] env AUTOSTART=disconnect：连接后 5 秒程序化点断开
     * （XAbstractButton_click=用户点击等价路径；回归断开 UAF 用）。 */
    {
        const char* autoRole2 = XSystem_environment("XGUI_DEMO_MB_AUTOSTART");
        if (autoRole2 && strcmp(autoRole2, "disconnect") == 0 &&
            s_mb.connectBtn) {
            XTimer* t = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
            if (t) {
                XTimer_setInterval(t, 5000);
                XTimer_setTimerCallback(t, mb_masterDisconnectAutoSlot);
                XTimer_setUserData(t, NULL);
                XTimer_start_base(t);
                s_mb.disconnectTimer = t;
            }
        }
    }
    return s_mb.pageRoot;
}

XWidget* demo_page_modbus_slave_build(XWidget* parent,
                                      DemoPageStatusFn statusFn, void* user)
{
    XLabel* title = NULL;
    int i;
    (void)statusFn; (void)user;

    XMemset(&s_mbSlave, 0, sizeof(s_mbSlave));
    s_mbSlave.inCount = 8;
    s_mbSlave.outCount = 8;

    if (!parent) return (XWidget*)0;
    {
        /* [滚动承载 2026-10-14] 同主站页：root=页面根，ScrollArea+body 承载。 */
        XWidget* pageRoot = XWidget_create(parent, 0);
        if (!pageRoot) return (XWidget*)0;
        s_mbSlave.scroll = XScrollArea_create(pageRoot, 0);
        if (!s_mbSlave.scroll) { XClassDelete((XClass*)pageRoot); return (XWidget*)0; }
        XScrollArea_setWidgetResizable(s_mbSlave.scroll, false);
        s_mbSlave.body = XWidget_create(NULL, 0);
        /* [滚动残影根修 2026-10-15] 同主站页：body 持 Base 底色+autofill。 */
        XWidget_setBackgroundRole(s_mbSlave.body, XPaletteColorRole_Base);
        XWidget_setAutoFillBackground(s_mbSlave.body, true);
        if (!s_mbSlave.body) { XClassDelete((XClass*)pageRoot); return (XWidget*)0; }
        XScrollArea_setWidget(s_mbSlave.scroll, s_mbSlave.body);
        XWidget_setGeometry((XWidget*)pageRoot, 0, 0, 740, 624);
        s_mbSlave.pageRoot = pageRoot;
        s_mbSlave.root = s_mbSlave.body;
    }

    mb_buildLabelAt(s_mbSlave.root,
                    "Modbus \xE4\xBB\x8E\xE7\xAB\x99\xE8\xB0\x83\xE8\xAF\x95",
                    12, 8, 300, &title); /* Modbus 从站调试 */
    if (title) XLabel_setTextPixelSize(title, 16);

    /* 连接参数区。 */
    mb_buildLabelAt(s_mbSlave.root,
                    "\xE9\x80\x9A\xE9\x81\x93", 12, 44, 56, NULL); /* 通道 */
    s_mbSlave.channelCombo = mb_buildChannelCombo(s_mbSlave.root, 72, 42, 160);
    XObject_connect_2((XObject*)s_mbSlave.channelCombo,
                      XSignal(XComboBox_activated_signal),
                      mb_slaveChannelSlot);

    mb_buildLabelAt(s_mbSlave.root,
                    "\xE7\x9B\x91\xE5\x90\xAC\xE7\xAB\xAF\xE5\x8F\xA3",
                    12, 74, 84, &s_mbSlave.tcpLbl); /* 监听端口 */
    s_mbSlave.tcpPortEdit = XLineEdit_create(s_mbSlave.root, 0);
    if (s_mbSlave.tcpPortEdit) {
        XLineEdit_setText(s_mbSlave.tcpPortEdit, "15020");
        XWidget_setGeometry((XWidget*)s_mbSlave.tcpPortEdit, 100, 72, 100, 24);
        XWidget_show((XWidget*)s_mbSlave.tcpPortEdit);
    }
    mb_buildLabelAt(s_mbSlave.root,
                    "\xE4\xB8\xB2\xE5\x8F\xA3", 12, 74, 56, &s_mbSlave.serialLbl);
                    /* 串口 */
    if (s_mbSlave.serialLbl)
        XWidget_setVisible((XWidget*)s_mbSlave.serialLbl, false);
    s_mbSlave.serialNameEdit = XLineEdit_create(s_mbSlave.root, 0);
    if (s_mbSlave.serialNameEdit) {
        XLineEdit_setText(s_mbSlave.serialNameEdit, "/dev/ttyS1");
        XWidget_setGeometry((XWidget*)s_mbSlave.serialNameEdit, 80, 72, 130, 24);
        XWidget_setVisible((XWidget*)s_mbSlave.serialNameEdit, false);
    }
    s_mbSlave.serialBaudCombo = mb_buildBaudCombo(s_mbSlave.root, 218, 72, 80);
    XWidget_setVisible((XWidget*)s_mbSlave.serialBaudCombo, false);

    mb_buildLabelAt(s_mbSlave.root,
                    "\xE6\x9C\xAC\xE7\xAB\x99\xE5\x9C\xB0\xE5\x9D\x80",
                    12, 104, 80, NULL); /* 本站地址 */
    s_mbSlave.unitSpin = mb_buildSpin(s_mbSlave.root, 96, 102, 60, 1, 247, 1);

    mb_buildLabelAt(s_mbSlave.root,
                    "\xE8\xBE\x93\xE5\x85\xA5\xE7\x82\xB9",
                    12, 134, 60, NULL); /* 输入点 */
    s_mbSlave.inCountSpin = mb_buildSpin(s_mbSlave.root, 76, 132, 60, 0, 64, 8);
    mb_buildLabelAt(s_mbSlave.root,
                    "\xE8\xBE\x93\xE5\x87\xBA\xE7\x82\xB9",
                    162, 134, 60, NULL); /* 输出点 */
    s_mbSlave.outCountSpin = mb_buildSpin(s_mbSlave.root, 226, 132, 60, 0, 64, 8);

    s_mbSlave.startBtn = XPushButton_create(s_mbSlave.root, 0);
    if (s_mbSlave.startBtn) {
        XPushButton_setText_2(s_mbSlave.startBtn,
                              "\xE5\x90\xAF\xE5\x8A\xA8"); /* 启动 */
        XWidget_setGeometry((XWidget*)s_mbSlave.startBtn, 12, 162, 90, 28);
        XObject_connect_2((XObject*)s_mbSlave.startBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_slaveStartSlot);
        XWidget_show((XWidget*)s_mbSlave.startBtn);
    }
    s_mbSlave.stateLabel = XLabel_create(s_mbSlave.root, 0);
    if (s_mbSlave.stateLabel) {
        XLabel_setText_2(s_mbSlave.stateLabel,
                         "\xE5\xB7\xB2\xE5\x81\x9C\xE6\xAD\xA2"); /* 已停止 */
        XWidget_setGeometry((XWidget*)s_mbSlave.stateLabel, 116, 166, 120, 22);
        XWidget_show((XWidget*)s_mbSlave.stateLabel);
    }
    s_mbSlave.statLabel = XLabel_create(s_mbSlave.root, 0);
    if (s_mbSlave.statLabel) {
        XLabel_setText_2(s_mbSlave.statLabel,
                         "\xE5\x86\x99\xE4\xBA\x8B\xE4\xBB\xB6: 0");
                         /* 写事件: 0 */
        XWidget_setGeometry((XWidget*)s_mbSlave.statLabel, 250, 166, 150, 22);
        XWidget_show((XWidget*)s_mbSlave.statLabel);
    }

    /* ---- 数据面板（可手动调整） ---- */
    mb_buildLabelAt(s_mbSlave.root,
                    "\xE6\x95\xB0\xE6\x8D\xAE\xE9\x9D\xA2\xE6\x9D\xBF"
                    "(\xE5\x8F\xAF\xE6\x89\x8B\xE5\x8A\xA8\xE4\xBF\xAE"
                    "\xE6\x94\xB9)",
                    12, 196, 220, NULL); /* 数据面板(可手动修改) */
    mb_buildLabelAt(s_mbSlave.root,
                    "\xE8\xA1\xA8", 300, 198, 30, NULL); /* 表 */
    s_mbSlave.tableCombo = XComboBox_create(s_mbSlave.root, 0);
    if (s_mbSlave.tableCombo) {
        XComboBox_addItem_2(s_mbSlave.tableCombo,
                "DO \xE7\xBA\xBF\xE5\x9C\x88");           /* DO 线圈 */
        XComboBox_addItem_2(s_mbSlave.tableCombo,
                "DI \xE7\xA6\xBB\xE6\x95\xA3\xE8\xBE\x93\xE5\x85\xA5");
                /* DI 离散输入 */
        XComboBox_addItem_2(s_mbSlave.tableCombo,
                "IR \xE8\xBE\x93\xE5\x85\xA5\xE5\xAF\x84\xE5\xAD\x98\xE5\x99\xA8");
                /* IR 输入寄存器 */
        XComboBox_addItem_2(s_mbSlave.tableCombo,
                "HR \xE4\xBF\x9D\xE6\x8C\x81\xE5\xAF\x84\xE5\xAD\x98\xE5\x99\xA8");
                /* HR 保持寄存器 */
        XComboBox_setCurrentIndex(s_mbSlave.tableCombo, 0);
        XWidget_setGeometry((XWidget*)s_mbSlave.tableCombo, 334, 194, 180, 24);
        XObject_connect_2((XObject*)s_mbSlave.tableCombo,
                          XSignal(XComboBox_activated_signal),
                          mb_slaveTableSlot);
        XWidget_show((XWidget*)s_mbSlave.tableCombo);
    }
    s_mbSlave.applyBtn = XPushButton_create(s_mbSlave.root, 0);
    if (s_mbSlave.applyBtn) {
        XPushButton_setText_2(s_mbSlave.applyBtn,
                              "\xE6\x8C\x89\xE7\x82\xB9\xE6\x95\xB0\xE9\x87"
                              "\x8D\xE5\xBB\xBA"); /* 按点数重建 */
        XWidget_setGeometry((XWidget*)s_mbSlave.applyBtn, 524, 192, 110, 26);
        XObject_connect_2((XObject*)s_mbSlave.applyBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_slaveApplySlot);
        XWidget_show((XWidget*)s_mbSlave.applyBtn);
    }

    s_mbSlave.dataTable = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, s_mbSlave.root, 0);
    if (s_mbSlave.dataTable) {
        static const char* const kHead[] = {
            "\xE5\x9C\xB0\xE5\x9D\x80",               /* 地址 */
            "\xE5\x80\xBC(\xE7\x82\xB9\xE5\x87\xBB/\xE5\x9B\x9E\xE8\xBD\xA6\xE7\x94\x9F\xE6\x95\x88)"
                /* 值(点击/回车生效) */
        };
        XWidget_setGeometry((XWidget*)s_mbSlave.dataTable, 12, 224, 622, 330);
        XTableWidget_setColumnCount(s_mbSlave.dataTable, 2);
        XTableWidget_setHorizontalHeaderLabels(s_mbSlave.dataTable, kHead,
                (int)(sizeof(kHead) / sizeof(kHead[0])));
        XWidget_show((XWidget*)s_mbSlave.dataTable);
    }
    mb_slaveRebuildPanel();

    /* [端到端联调入口] env XGUI_DEMO_MB_AUTOSTART=slave：构建后程序化
     * 启动监听（XAbstractButton_click 走完整激活流程；回环联调用）。 */
    {
        const char* autoRole = XSystem_environment("XGUI_DEMO_MB_AUTOSTART");
        if (autoRole && strcmp(autoRole, "slave") == 0 && s_mbSlave.startBtn) {
            XAbstractButton_click((XAbstractButton*)s_mbSlave.startBtn);
            XPrintf("XGuiWindowDemo: modbus autostart slave done\n");
        }
    }
    return s_mbSlave.pageRoot;
}

int demo_page_modbus_master_autotest(XWidget* page)
{
    /* 契约口径：非阻塞自检——只验控件存在与状态一致性，不发真实请求。 */
    if (!page) return -1;
    {
        int failures = 0;
        if (!s_mb.channelCombo || !s_mb.connectBtn || !s_mb.unitSpin)
            ++failures;
        if (!s_mb.inCountSpin || !s_mb.outCountSpin || !s_mb.pollSpin)
            ++failures;
        if (s_mb.device) ++failures; /* 构建后应未连接。 */
        /* [重叠回归断言] 同行标签与输入框几何不得相交。 */
        {
            XRect rl, rc;
            rl = XWidget_geometry((XWidget*)s_mb.tcpLbl);
            rc = XWidget_geometry((XWidget*)s_mb.tcpAddrEdit);
            if (rl.x + rl.width > rc.x) ++failures;
        }
        return failures;
    }
}

int demo_page_modbus_slave_autotest(XWidget* page)
{
    if (!page) return -1;
    {
        int failures = 0;
        if (!s_mbSlave.channelCombo || !s_mbSlave.startBtn)
            ++failures;
        if (!s_mbSlave.dataTable || !s_mbSlave.tableCombo)
            ++failures;
        if (s_mbSlave.server) ++failures;
        {
            XRect rl, rc;
            rl = XWidget_geometry((XWidget*)s_mbSlave.tcpLbl);
            rc = XWidget_geometry((XWidget*)s_mbSlave.tcpPortEdit);
            if (rl.x + rl.width > rc.x) ++failures;
        }
        return failures;
    }
}

void demo_page_modbus_master_adapt(XWidget* page)
{
    /* [滚动承载 2026-10-14] ScrollArea 铺满页面根（根几何由堆叠布局随
     * 窗口/导航占位分配）；body 保持装配宽度、内容需求高度（52+8+
     * 240+32+250+20+44+28+190+12=≈784），高度不足时出竖滚动条——底部
     * 寄存器表始终可达（滚动经 scrollContentsBy 重摆挂件链已通）。 */
    int pw;
    int ph;
    if (page != s_mb.pageRoot || !s_mb.scroll) return;
    pw = XWidget_width(page);
    ph = XWidget_height(page);
    if (pw <= 0 || ph <= 0) return;
    XWidget_setGeometry((XWidget*)s_mb.scroll, 0, 0, pw, ph);
    /* body：宽随视口（控件绝对坐标按 740 装配，视口更宽时留白）、
       高=内容需求 784（视口不足即出滚动条）。resizable 关闭——本框架
       的 resizable 会把 content 硬拉成视口尺寸（无 Qt sizeHint 语义），
       内容超出部分反而不可达。 */
    XWidget_setGeometry(s_mb.body, 0, 0, pw > 740 ? pw : 740, 784);
    /* [动态行列 2026-10-14] 列组数随视口宽、行数拉满视口高：
       需求行=ceil(数量/列组)；显示行=max(需求行, 视口可容纳行数) 钳
       16 槽——表格高度填满主界面底部（空行占位），页面竖滚动条消失；
       窗口极矮（视口容纳行 < 需求行）时内容超高，滚动兜底仍会出现。
       [表随视口 2026-10-14] 表宽=pw-24（不钳 740）——窄窗表格随视口
       收缩、列组可降到 1；其余控件仍按 body 740 横滚兜底。
       [次序 2026-10-14] 几何先行：列组数读表宽，旧次序（先重建后
       setGeometry）拿的是上一拍宽度，首建时为 0；行列不变仅宽变时
       只重摆列宽，免整表重建（拖拽每帧只走轻路径）。 */
    {
        int cols;
        int dispRows;
        int tableW;
        int tableH;
        int contentH;
        tableW = pw - 24;
        if (tableW < mb_regGutterW() + MB_REG_GROUP_W / 2)
            tableW = mb_regGutterW() + MB_REG_GROUP_W / 2;
        XWidget_setGeometry((XWidget*)s_mb.regTable, 12, 502, tableW,
                            mb_regHeadH() + (s_mb.regDispRows > 0 ?
                                      s_mb.regDispRows : 1) * mb_regRowH());
        cols = mb_masterRegColCount();
        dispRows = mb_masterRegFillRows(cols);
        if (s_mb.regDispRows != dispRows || s_mb.regCols != cols)
            mb_masterRegBuildRows(dispRows);
        else
            mb_masterRegApplyColWidths();
        tableH = mb_regHeadH() + dispRows * mb_regRowH();
        contentH = 502 + tableH + 12;
        /* [根修 2026-10-14] body 高度随表格内容走（≥784 装配下限）——
         * 此前钉死 784，表随视口拉高后表格底部被 body 裁掉：视口再高
         * 也只画 (784-502-28)/22≈11 行，下方留白还挂着滚动条。 */
        {
            int bodyW = pw > 740 ? pw : 740;
            int contentW = bodyW > 12 + tableW ? bodyW : 12 + tableW;
            XWidget_setGeometry(s_mb.body, 0, 0, bodyW,
                                contentH > 784 ? contentH : 784);
            XWidget_setGeometry((XWidget*)s_mb.regTable, 12, 502, tableW,
                                tableH);
            /* 内容宽=max(body 宽, 表右缘)——其余控件按 740 横滚兜底可达，
               表随视口收缩时不把内容宽拖小。 */
            XAbstractScrollArea_setContentSize(
                (XAbstractScrollArea*)s_mb.scroll, contentW, contentH);
        }
    }
}

void demo_page_modbus_slave_adapt(XWidget* page)
{
    /* 同主站页：内容需求高度 224+8+330+12≈574。 */
    int pw;
    int ph;
    if (page != s_mbSlave.pageRoot || !s_mbSlave.scroll) return;
    pw = XWidget_width(page);
    ph = XWidget_height(page);
    if (pw <= 0 || ph <= 0) return;
    XWidget_setGeometry((XWidget*)s_mbSlave.scroll, 0, 0, pw, ph);
    XWidget_setGeometry(s_mbSlave.body, 0, 0, pw > 740 ? pw : 740, 574);
    XAbstractScrollArea_setContentSize((XAbstractScrollArea*)s_mbSlave.scroll,
                                       pw > 740 ? pw : 740, 574);
}

#else /* !MB_PAGE_ENABLED */

/* 契约桩：模块被裁剪时主文件可无条件链接。 */
XWidget* demo_page_modbus_master_build(XWidget* parent,
                                       DemoPageStatusFn statusFn, void* user)
{
    (void)parent; (void)statusFn; (void)user;
    return (XWidget*)0;
}

XWidget* demo_page_modbus_slave_build(XWidget* parent,
                                      DemoPageStatusFn statusFn, void* user)
{
    (void)parent; (void)statusFn; (void)user;
    return (XWidget*)0;
}

int demo_page_modbus_master_autotest(XWidget* page)
{
    (void)page;
    return -1;
}

int demo_page_modbus_slave_autotest(XWidget* page)
{
    (void)page;
    return -1;
}

void demo_page_modbus_master_adapt(XWidget* page)
{
    (void)page;
}

void demo_page_modbus_slave_adapt(XWidget* page)
{
    (void)page;
}

#endif /* MB_PAGE_ENABLED */
