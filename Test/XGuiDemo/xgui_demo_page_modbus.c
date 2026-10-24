/**
 * @file    xgui_demo_page_modbus.c
 * @brief   XGuiDemo 扩展页：Modbus 主站/从站调试（双页拆分，串口 RTU /
 *          网络 TCP 双通道）。
 * @author  XinYueC 团队
 * @details 契约见 xgui_demo_pages.h（demo_page_modbus_master_build /
 *          demo_page_modbus_slave_build / autotest / adapt）。
 *
 * 页 17 = 主站（Master）：连接参数合并行（通道/服务器或串口/从站地址/单事务）
 *          → 远程 IO 模块适配（输入/输出点数）→ 连接/断开 → 轮询周期 →
 *          状态行 + IO 点位网格（输入只读指示、输出点击翻转写线圈）。
 * 页 18 = 从站（Server）：参数合并行（通道/监听端口/串口/本站地址，串口
 *          项随通道显隐）→ 启动/停止 → **全宽纵向四段式数据区**：
 *          段① DI（标题+点数 SpinBox+按点数重建+DI全开/全关 + 换行灯格）、
 *          段② DO（同构）、段③ IR/HR 两表上下堆叠铺满整宽（行内编辑回车/
 *          失焦提交 XModbusServer_setData2 即时生效+dataWritten 计数），
 *          主站可随即读走，构成端到端联调闭环。
 *          [10-18 二次追加·滚动条纪律] 全页唯一滚动条=页根竖滚区：
 *          DI/DO 灯格放不下换行往下放（对标主站页 IO 网格换行口径，
 *          宿主高=行数×行高随宽度重算）；寄存器表列宽自适应铺满表宽、
 *          16 行全展开；内容超高只出整页右侧一条竖滚条，任何子段无滚动条。
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
#if !defined(_WIN32)
#include <sys/socket.h> /* [CLOSE-WAIT 根修 2026-10-19] MSG_PEEK 死链探测。 */
#include <errno.h>
#endif
#include "XGuiConfig.h"
#include "XPrintf.h"
#include "XObject.h"
#include "XWidget.h"
#include "XBoxLayout.h" /* [布局容器化 2026-10-17] 页内排布改盒式布局器管理。 */
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
#include "XItemDelegate.h"
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
#include "XSerialPortInfo.h" /* 串口下拉：每次弹出前重枚举本机端口。 */
#endif
#define MB_PAGE_ENABLED (XLAYOUT_ON && XLAYOUT_BOX_ON && XWIDGET_ON && \
                         XLABEL_ON && XCOMBOBOX_ON && \
                         XLINEEDIT_ON && XPUSHBUTTON_ON && XCHECKBOX_ON && \
                         XSPINBOX_ON && XTABLEWIDGET_ON)
#else
#define MB_PAGE_ENABLED 0
#endif

#if MB_PAGE_ENABLED

/* ==================== 布局常量 ==================== */
#define MB_ROW_H        24      /* 标准行高。 */

/* 数据面板行容量（DI/DO 各段上限；IR/HR 寄存器表行数）。
 * [10-18 三次追加 2026-10-18] 寄存器个数改可调：MB_SLAVE_REG_ROWS 降格为
 * 「默认行数」，上限 MB_SLAVE_REG_MAX_ROWS=256（每表数量 SpinBox 1..256，
 * 变更走 setValueCount 保值 + setRowCount 同步，见 mb_slaveApplyRegCounts）。 */
#define MB_SLAVE_MAX_POINTS     64
#define MB_SLAVE_REG_ROWS       16
#define MB_SLAVE_REG_MAX_ROWS   256

/* [10-18 二次追加·滚动条纪律] 常量再瘦身：单行灯格滚动壳高
 * （MB_SLAVE_LAMP_ROW_H）、数据区固定段高估算（MB_SLAVE_DATA_CHROME_H）、
 * 表最小可见高（MB_SLAVE_MIN_REG_H）随「滚动壳删除 + 表满高全展开 +
 * 两遍试摆删除」一并作废；只留表最小宽与视口兜底宽两个下限值。 */
/* [实测校正 2026-10-18] 140 → 180：表内除地址/值两列外还有行号列
 * （XTW_HEADER_W=40），140 宽装不下「行号+地址+值」，
 * 值列（行内编辑框）被挤到可视区外（实测表内出横滚条、值列不可见）。 */
#define MB_REG_MIN_COL_W       180   /* 寄存器表最小宽（视口未定时兜底）。 */
#define MB_SLAVE_MIN_AVAIL_W   320   /* 数据区最小可用宽（视口未定时兜底）。 */

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

/* ==================== 从站自绘开关灯格（可点、无连接门控） ====================
 * [四区同屏 2026-10-18] 从站 DI/DO 复用自绘圆圈画法（照搬 MbDiLamp），但
 * 基类换 XCheckBox——物理上可点，且不挂 MbDoLamp 那道硬编码的
 * `s_mb.device` 连接门控（:415 实读）：需求要求「单机可调」，启动前
 * 点灯即写本地数据区。画法与 MbDiLamp 逐行同款，仅前缀 DI/DO 可变。 */

#if XWIDGET_ON && XCHECKBOX_ON && XABSTRACTBUTTON_ON && XPAINTER_ON
XCLASS_DEFINE_BEGING(MbSwLamp)
XCLASS_DEFINE_EXTEND_END(MbSwLamp, XCheckBox)

/** @brief 从站开关灯格：绿=1 红=0 圆圈 + 「DI/DOoo」编号；单击翻转。 */
typedef struct MbSwLamp
{
    XCheckBox m_base;   /**< XCheckBox 基类（checkable 点击链路）；第一成员。 */
    int    m_index;     /**< 点编号（DIoo/DOoo 文本）。 */
    char   m_prefix[3]; /**< "DI" 或 "DO"（含结尾 NUL）。 */
} MbSwLamp;

/** @brief 绘制：圆圈（checked 绿/未勾红）+ 编号（照搬 MbDiLamp 画法）。
 *  [2026-10-13 坑位复用] drawEllipse_2 走 brush 填充+pen 描边，必须先
 *  setBrush——默认 NoBrush 时只描边（截图空心白圈实证）。 */
static void VMbSwLamp_paintEvent(XWidget* self, XEvent* event)
{
    MbSwLamp* lamp = (MbSwLamp*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XRect rect;
    uint32_t color;
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
    color = XCheckBox_isChecked(lamp) ? 0xff2ecc40u : 0xffe03434u;
    {
        float cx = rect.x + 9.0f;
        float cy = rect.y + rect.height / 2.0f;
        XPainter_setBrush(&painter, color);
        XPainter_setPen(&painter, 0xff202020u);
        XPainter_drawEllipse_2(&painter, cx, cy, 7.0f, 7.0f);
    }
    {
        XFont font;
        XRect trect;
        char text[12];
        snprintf(text, sizeof(text), "%s%02d", lamp->m_prefix, lamp->m_index);
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

XVtable* MbSwLamp_class_init(void)
{
    XVTABLE_INIT_DEFAULT(MbSwLamp)
    XVTABLE_INHERIT_XCLASS(XCheckBox);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VMbSwLamp_paintEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建并挂父（prefix 传 "DI"/"DO"）。 */
static void MbSwLamp_init(MbSwLamp* self, XWidget* parent, int index,
                          const char* prefix)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XCheckBox_init(&self->m_base, parent, 0);
    XClassSetVtable(self, MbSwLamp);
    self->m_index = index;
    self->m_prefix[0] = prefix ? prefix[0] : 'D';
    self->m_prefix[1] = prefix && prefix[1] ? prefix[1] : 'I';
    self->m_prefix[2] = '\0';
    /* [禁令 D-6 2026-10-18] 此处及全仓任何地方都**不得**对本类调
     * XAbstractButton_setAutoExclusive，也**不得**把灯格加入任何
     * XButtonGroup：64 个灯格是同父兄弟，autoExclusive 一开，
     * setChecked(true) 末尾（XAbstractButton.c:620-621）会把同父全部
     * 已勾兄弟置 false（:328-345），64 格将互相熄灭。当前安全**仅因**
     * XAbstractButton_init 路径未给该位赋真值，属「恰好没踩」。 */
    XWidget_show((XWidget*)self);
}
#endif /* XWIDGET_ON && XCHECKBOX_ON && XABSTRACTBUTTON_ON && XPAINTER_ON */

/* ==================== 主站页状态 ==================== */

typedef struct MbDoLamp MbDoLamp; /* 前向：DO 圆圈控件（定义在状态容器后）。 */

/** @brief 主站通道类型。 */
typedef enum {
    MB_MASTER_TCP = 0,   /* Modbus TCP（网络）。 */
    MB_MASTER_RTU = 1    /* Modbus RTU（串口）。 */
} MbMasterChannel;

/** @brief 主站侧状态容器（单实例静态，页生命周期=进程）。 */
typedef struct MbMaster {
    XWidget*        root;             /* 控件挂载父级（=pageRoot=内容根）。 */
    XWidget*        pageRoot;         /* 内容根（layRoot 宿主；几何由 scroll 承载）。 */
    XScrollArea*    scroll;           /* 整页竖滚区（页宿主内，唯一滚动条）。 */
    XWidget*        pageHost;         /* 页宿主（堆叠条目；resize 铺满 scroll）。 */
    int             viewportH;        /* scroll 视口高快照（显示行折算用）。 */
    /* [布局容器化 2026-10-17] 页内排布全权移交盒式布局器：页根纵列+
     * 行/列子布局器。子布局经 addLayout 按借用指针挂入父布局器，
     * 对象寿命须覆盖页面生命周期——全部落在本静态容器。 */
    XBoxLayout      layRoot;          /* 页根纵向主布局（表格伸缩贴底）。 */
    XBoxLayout      layParams;        /* 参数合并行（通道/服务器或串口/从站地址/单事务）。 */
    XBoxLayout      layIo;            /* 输入/输出点数行。 */
    XBoxLayout      layPoll;          /* 轮询+连接行。 */
    XBoxLayout      layGridIn;        /* 输入点列（节标题+网格宿主）。 */
    XBoxLayout      layGridOut;       /* 输出点列（标题行+网格宿主）。 */
    XBoxLayout      layGridOutTitle;  /* 输出点标题行（标题+全开/全关）。 */
    XBoxLayout      layGrid;          /* 点位双列行。 */
    XBoxLayout      layIrTitle;       /* IR 段标题行（「输入寄存器 IR」+数量+重建）。 */
    XBoxLayout      layHrTitle;       /* HR 段标题行（同上）。 */
    int             irCount;          /* IR 表当前行数（=地址 0..irCount-1）。 */
    int             hrCount;          /* HR 表当前行数。 */
    XComboBox*      channelCombo;     /* 通道：TCP/串口。 */
    XLabel*         tcpLbl;           /* TCP 行标签（随通道显隐）。 */
    XLineEdit*      tcpAddrEdit;      /* TCP: 服务器地址。 */
    XLineEdit*      tcpPortEdit;      /* TCP: 端口。 */
    XLabel*         serialLbl;        /* 串口行标签（随通道显隐）。 */
    XComboBox*      serialNameCombo;  /* 串口: 设备名（下拉，每次弹出重枚举）。 */
    XComboBox*      serialBaudCombo;  /* 串口: 波特率。 */
    XSpinBox*       unitSpin;         /* 从站地址(1..247)。 */
    XSpinBox*       inCountSpin;      /* 远程 IO 输入点数(0..64)。 */
    XSpinBox*       outCountSpin;     /* 远程 IO 输出点数(0..64)。 */
    XSpinBox*       pollSpin;         /* 轮询周期 ms(10..5000)。 */
    XPushButton*    connectBtn;       /* 连接/断开。 */
    XPushButton*    allOnBtn;         /* 输出全开。 */
    XPushButton*    allOffBtn;        /* 输出全关。 */
    /* 寄存器两表面板（IR/HR 分表，从站同款样式）。 */
    XSpinBox*       irCountSpin;      /* IR 数量(1..16)。 */
    XSpinBox*       hrCountSpin;      /* HR 数量(1..16)。 */
    XLabel*         irStartLbl;       /* IR「起始」标签。 */
    XLabel*         hrStartLbl;       /* HR「起始」标签。 */
    XSpinBox*       irAddrSpin;       /* IR 起始地址(0..65535)。
                                          [2026-10-24 用户需求] 远端寄存器
                                          不总在 0 基——读/写/地址列全链
                                          =起始+行号。 */
    XSpinBox*       hrAddrSpin;       /* HR 起始地址。 */
    XComboBox*      irFmtCombo;       /* IR 值显示格式（16进制/10进制）。 */
    XComboBox*      hrFmtCombo;       /* HR 值显示格式。 */
    int             irHex;            /* IR 当前 1=16进制 0=10进制（默认 16）。 */
    int             hrHex;            /* HR 当前格式。 */
    int             irAddr;           /* IR 当前起始地址（Spin 副本）。 */
    int             hrAddr;           /* HR 当前起始地址。 */
    XTableWidget*   irTable;          /* IR 表（值格双击编辑=FC6 写；显示
                                          格式随 irFmtCombo。[10-19 框架
                                          编辑器迁移] 框架委托编辑器承担）。 */
    XTableWidget*   hrTable;          /* HR 表（同上）。 */
    bool            regSuppress;      /* 回填抑制。 */
    XModbusReply*   regPendingWriteReply;   /* 寄存器写在途。 */
    XLabel*         stateLabel;       /* 状态行。 */
    XLabel*         statLabel;        /* 统计行（请求/错误）。 */
    XWidget*        inGridHost;       /* 输入点网格宿主。 */
    XWidget*        outGridHost;      /* 输出点网格宿主。 */
    /* [布局容器化 2026-10-17] 节标题/栏名引用（build 装配进对应布局行）。 */
    XLabel*         ioLbl;            /* 「远程 IO 模块」节标题。 */
    XLabel*         inLbl;            /* 「输入点」标签。 */
    XLabel*         outLbl;           /* 「输出点」标签。 */
    XLabel*         pollLbl;          /* 「轮询(ms)」标签。 */
    XLabel*         diTitleLbl;       /* 「输入点状态(只读)」标题。 */
    XLabel*         doTitleLbl;       /* 「输出点(点击翻转)」标题。 */
    XLabel*         irTitleLbl;       /* 「输入寄存器 IR」段标题。 */
    XLabel*         hrTitleLbl;       /* 「保持寄存器 HR」段标题。 */
    XPushButton*    applyIrBtn;       /* IR「按点数重建」。 */
    XPushButton*    applyHrBtn;       /* HR「按点数重建」。 */
    /* 引擎对象（通道切换重建）。 */
    XModbusDevice*  device;           /* 当前设备（TcpClient 或 RtuSerialClient）。 */
    XModbusReply*   pendingReply;     /* 在途读请求（finished 后 deleteLater）。 */
    XModbusDataUnit* pollUnit;        /* 轮询单元（DiscreteInputs）。 */
    XModbusDataUnit* coilUnit;        /* 线圈轮询单元（DO 状态回读）。 */
    XModbusDataUnit* irUnit;          /* IR 表轮询单元（0..行数-1，2026-10-24
                                          联调缺口：此前轮询只覆盖位区，寄存
                                          器表仅手动「读」刷新——从站改值主站
                                          表不跟新）。 */
    XModbusDataUnit* hrUnit;          /* HR 表轮询单元（同上）。 */
    int              irUnitCountCache; /* irUnit 建立时行数（变更即重建）。 */
    int              hrUnitCountCache; /* hrUnit 建立时行数。 */
    int              irUnitAddrCache;  /* irUnit 建立时起始地址（同上）。 */
    int              hrUnitAddrCache;  /* hrUnit 建立时起始地址。 */
    int              pollPhase;       /* 轮询相位 0=DI 1=DO 2=IR 3=HR。 */
    int              pollPhaseSent;   /* 在途请求的相位（应答回填分派）。 */
    XCheckBox*       singleTxCheck;    /* 单事务模式多选框（TCP 通道显示）。 */
    bool             singleTxMode;     /* 单事务模式：写不与在途读流水线并发。 */
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
    XWidget*        root;             /* 控件挂载父级（=pageRoot=内容根）。 */
    XWidget*        pageRoot;         /* 内容根（layRoot 宿主；堆叠几何由 scroll 承载）。 */
    /* [10-18 二次追加·整页竖滚] 返回给页堆叠的页根=scroll（只竖滚），
     * 内容件=内容根（宽随视口、高按内容首选，见 fitScrollContent）。 */
    XScrollArea*    scroll;           /* 整页竖滚区（页堆叠条目，唯一滚动条）。 */
    /* [布局容器化 2026-10-17] 同主站页：行/列布局器落静态容器。 */
    XBoxLayout      layRoot;          /* 内容根纵向主布局。 */
    /* [10-18 用户追加规格④ 参数行合并] 通道/监听端口/串口(随通道显隐)/
     * 本站地址并一行：隐藏控件条目在主轴分配中取 0 宽（isEmpty⇒size=0，
     * XBoxLayout.c:461 + XLayoutItem.c:229），TCP/串口切换行内自折叠。 */
    XBoxLayout      layChannel;       /* 参数合并行（通道/端口/串口/本站地址）。 */
    XBoxLayout      layStart;         /* 启动/状态/统计行（页根上部不动）。 */
    /* [全宽纵向四段式 2026-10-18] 数据区：layData（TopToBottom）直接
     * 平铺六条目——DI 标题行/DI 灯格宿主/DO 标题行/DO 灯格宿主/IR 表/
     * HR 表。两标题行是**未绑控件的子布局**（addLayout 合法）；宿主与
     * 两表是控件条目，不再套「列容器」中间层（每层只有「控件+自己的
     * 布局」一跳，规避 activate 不级联与内层坐标系错位的实测坑）。 */
    XBoxLayout      layData;          /* 数据区纵向主布局（恒纵向，无双档）。 */
    XBoxLayout      layDiTitle;       /* DI 标题行（标题+点数 SpinBox+按点数重建+DI全开/全关）。 */
    XBoxLayout      layDoTitle;       /* DO 标题行（标题+点数 SpinBox+按点数重建+全开/全关）。 */
    XWidget*        dataArea;         /* 数据区容器（页根条目 3）。 */
    XComboBox*      channelCombo;     /* 通道：TCP 监听/串口。 */
    XLabel*         tcpLbl;           /* 监听端口标签（随通道显隐）。 */
    XLineEdit*      tcpPortEdit;      /* TCP: 监听端口。 */
    XLabel*         serialLbl;        /* 串口标签（随通道显隐）。 */
    XComboBox*      serialNameCombo;  /* 串口: 设备名（下拉，每次弹出重枚举）。 */
    XComboBox*      serialBaudCombo;  /* 串口: 波特率。 */
    XSpinBox*       unitSpin;         /* 本站地址(1..247)。 */
    XSpinBox*       inCountSpin;      /* 输入点数。 */
    XSpinBox*       outCountSpin;     /* 输出点数。 */
    XPushButton*    startBtn;         /* 启动/停止。 */
    XLabel*         stateLabel;       /* 状态行。 */
    XLabel*         statLabel;        /* 请求计数行。 */
    /* [10-18 用户追加规格⑤] 「按点数重建」拆两枚，分置①②首行紧挨
     * 各自点数 SpinBox；原独立重建行删除。槽分立（sender 不可信契约）
     * 但共用同一实现（重建按两个 SpinBox 全量解算，见 mb_slaveApplyCounts）。 */
    XPushButton*    applyDiBtn;       /* 段①「按点数重建(输入)」。 */
    XPushButton*    applyDoBtn;       /* 段②「按点数重建(输出)」。 */
    XPushButton*    applyIrBtn;       /* 段③「按点数重建」(IR) [10-19]。 */
    XPushButton*    applyHrBtn;       /* 段④「按点数重建」(HR) [10-19]。 */
    XPushButton*    diAllOnBtn;       /* DI 全开（本地批量置 DiscreteInputs）。 */
    XPushButton*    diAllOffBtn;      /* DI 全关。 */
    XPushButton*    allOnBtn;         /* DO 全开。 */
    XPushButton*    allOffBtn;        /* DO 全关。 */
    /* 灯格区：DI/DO 各一块裸宿主直接作 layData 控件条目（滚动壳随
     * [10-18 二次追加] 删除），灯格按「可见列数」换行铺进宿主（C7：
     * 裸 XWidget 在布局里 sizeHint 回落 0 ⇒ 定尺+显式 sizeHint 双落）。 */
    XWidget*        diGridHost;       /* DI 换行灯格宿主（宽=列数×单元宽）。 */
    XWidget*        doGridHost;       /* DO 换行灯格宿主（高=行数×单元高）。 */
    MbSwLamp*       diCells[MB_SLAVE_MAX_POINTS];  /* DI 灯格。 */
    MbSwLamp*       doCells[MB_SLAVE_MAX_POINTS];  /* DO 灯格。 */
    /* 寄存器区：IR/HR 两张常驻表，各 2 列 × 16 行，值列常驻 XLineEdit
     * （一次挂载、从不叠加 ⇒ editingFinished 的 sender 恒等于该框自身，
     * 槽内按 sender 指针反查行号可靠）。 */
    /* [10-18 三次追加 2026-10-18] 寄存器区按线圈段同款布局重做：每表一条
     * 段标题行（未绑控件的子布局，addLayout 合法——与 layDiTitle/layDoTitle
     * 同款、同坑位规避）+ 数量 SpinBox（1..256，变更走
     * mb_slaveApplyRegCounts：setValueCount 保值 + setRowCount 同步）。 */
    XBoxLayout      layIrTitle;       /* IR 段标题行（「输入寄存器 IR」+数量）。 */
    XBoxLayout      layHrTitle;       /* HR 段标题行（「保持寄存器 HR」+数量）。 */
    XLabel*         irTitleLbl;       /* IR 段标题（autotest 纵向次序断言用）。 */
    XLabel*         hrTitleLbl;       /* HR 段标题（同上）。 */
    XSpinBox*       irCountSpin;      /* IR 数量(1..256)。 */
    XSpinBox*       hrCountSpin;      /* HR 数量(1..256)。 */
    XComboBox*      irFmtCombo;       /* IR 值显示格式（16进制/10进制）。 */
    XComboBox*      hrFmtCombo;       /* HR 值显示格式。 */
    int             irHex;            /* IR 当前 1=16进制 0=10进制（默认 10）。 */
    int             hrHex;            /* HR 当前格式。 */
    int             irCount;          /* IR 当前生效行数（Spin 变更的副本）。 */
    int             hrCount;          /* HR 当前生效行数（同上）。 */
    XTableWidget*   irTable;          /* 输入寄存器表。 */
    XTableWidget*   hrTable;          /* 保持寄存器表。 */
    /* [10-19 框架编辑器迁移] 行观察态皆纯文本 item，常驻编辑框行数组
     * （irRows/hrRows）退役——编辑由框架委托编辑器承载（表级
     * setItemsEditable），提交落库走 cellChanged 桥（见
     * mb_slaveRegCellChangedAt）。 */
    /* 视口快照（幂等门控用；壳宽预算 ioBudgetW 随滚动壳删除）。 */
    int             gridCols;         /* 灯格可见列数 = 段宽/单元宽（宽度驱动）。 */
    int             gridPts;          /* 上次重建时的 max(点数)，点数变化触发重建。 */
    /* 引擎对象（常驻：停止只断连不销毁 ⇒ 停止期间仍可单机调点位）。 */
    XModbusServer*  server;           /* TcpServer 或 RtuSerialServer。 */
    bool            listening;        /* 监听中（与 server 存在解耦）。 */
    /* [CLOSE-WAIT 根修 2026-10-19] 死链巡检定时器（1s）：对端 FIN 的
     * EOF 在 IO 环→XAbstractSocket 事件层被吞（0 字节读只 ContinueRead，
     * 不发 readyRead/disconnected/不置错，XAbstractSocket.c 读事件分支），
     * 服务端 onClientDisconnected 永不触发 ⇒ accepted fd 成 CLOSE-WAIT。
     * 框架三处修复点（XAbstractSocket.c/XNetIoRingPosix.c/
     * XAbstractNetIoRing.c）均在并行 lane 未提交改动中，不可动；本页以
     * 公开 API 自巡检回收（见 mb_slaveSweepDeadClients）。 */
    XTimer*         reapTimer;        /* 死链巡检定时器（页根子对象级联析构）。 */
    /* 数据表类型键。 */
    XModbusRegisterType inEnum, outEnum, irEnum, hrEnum;
    /* [保值 2026-10-18] 四张数据单元的缓存指针。map 只在 server 建好时
     * 挂一次；「按点数重建」/再次启动只调 setValueCount 改容量，**不再
     * 重建 map**——重建会把 map 里已有的值全部丢掉（实测：本地已点亮的
     * DO00/DO03/DI01 在按「按点数重建」后全退回红）。换通道销毁 server
     * 时一并置 NULL。[16点读回根修 2026-10-19] DI/DO 位单元首建即满容
     * 量 64 且不再 resize（位类型运行态扩容不生效，见 rebuildMap 注释）；
     * IR/HR 仍走 setValueCount（寄存器路径实测正常）。 */
    XModbusDataUnit*   unitIn;          /* DiscreteInputs 单元（64 位满容量）。 */
    XModbusDataUnit*   unitCoil;        /* Coils 单元（64 位满容量）。 */
    XModbusDataUnit*   unitIr;          /* InputRegisters 单元。 */
    XModbusDataUnit*   unitHr;          /* HoldingRegisters 单元。 */
    /* 数据区副本（重挂/重建灯格用）。 */
    int             inCount;
    int             outCount;
    unsigned        writeEvents;      /* dataWritten 事件计数。 */
    /* [四区同屏 2026-10-18] 三个抑制标志——定性不同，勿混：
     *  - localWriteSuppress：**必需**。VXModbusServer_writeData 尾部
     *    无条件发 dataWritten（XModbusServer.c:942），本端 setData2 若不
     *    抑制即回环震荡（点灯→dataWritten→回填→toggled→再写）。
     *  - bitEchoSuppress / regEchoSuppress：**防御性冗余，非必需**。
     *    灯格侧有 setChecked 同值早退不 emit（XAbstractButton.c:597-601）
     *    + 槽内差量判断必然早退；编辑框侧程序化 setText 根本不发
     *    editingFinished（发射点仅 XLineControl.c:3611 Return 与
     *    XLineEdit.c:1166 focus-out）。保留仅为双保险，防后人误删。 */
    int             localWriteSuppress;
    int             bitEchoSuppress;
    int             regEchoSuppress;
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

/** @brief 构建一行标签（统一 13px 参数行小字；标题/栏名调用点覆盖字号）。
 *  [布局容器化 2026-10-17] 手写坐标改固定尺寸：定宽 w、定高 22，
 *  摆位交由调用点的盒式布局行按序分配。 */
static void mb_buildLabel(XWidget* parent, const char* text, int w,
                          XLabel** out)
{
    XLabel* label = XLabel_create(parent, 0);
    if (label) {
        XLabel_setText_2(label, text);
        /* [2026-10-13 重叠根修] 参数行标签统一 13px 小字：默认 16px 下
         * 3~4 字标签实际渲染宽超预留列宽，文本右缘压进同行输入框。 */
        XLabel_setTextPixelSize(label, 13);
        XWidget_setFixedWidth((XWidget*)label, w);
        XWidget_setFixedHeight((XWidget*)label, 22);
        XWidget_show((XWidget*)label);
    }
    if (out) *out = label;
}

/** @brief 通道下拉（TCP 网络 / 串口 RTU）。 */
static XComboBox* mb_buildChannelCombo(XWidget* parent, int w)
{
    XComboBox* combo = XComboBox_create(parent, 0);
    if (combo) {
        XComboBox_addItem_2(combo,
                "TCP \xE7\xBD\x91\xE7\xBB\x9C");      /* TCP 网络 */
        XComboBox_addItem_2(combo,
                "\xE4\xB8\xB2\xE5\x8F\xA3 RTU");      /* 串口 RTU */
        XComboBox_setCurrentIndex(combo, 0);
        XWidget_setFixedWidth((XWidget*)combo, w);
        XWidget_setFixedHeight((XWidget*)combo, 24);
        XWidget_show((XWidget*)combo);
    }
    return combo;
}

/** @brief 波特率下拉（默认 9600）。 */
static XComboBox* mb_buildBaudCombo(XWidget* parent, int w)
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
    XWidget_setFixedWidth((XWidget*)combo, w);
    XWidget_setFixedHeight((XWidget*)combo, 24);
    XWidget_show((XWidget*)combo);
    return combo;
}

/* ==================== 串口名下拉（每次弹出前重枚举） ==================== */

/** @details 设备名原为手输 XLineEdit；串口可用集合随插拔/占用动态变化，
 *           改下拉后每次**打开下拉前**重读 XSerialPortInfo_availablePorts()
 *           （Qt 惯例：QComboBox 子类重载 showPopup/或 popupShown 前 populate）。
 *           本框架 XComboBox 无 showPopup 虚槽，弹层唯一入口是左键按下
 *           （键盘仅上下改索引不弹层），故子类重载 mousePressEvent：左键
 *           按下且弹层未开 → 先重枚举再交基类弹出。 */
#if XMODBUS_RTU_ON
XCLASS_DEFINE_BEGING(MbSerialCombo)
XCLASS_DEFINE_EXTEND_END(MbSerialCombo, XComboBox)

typedef struct MbSerialCombo
{
    XComboBox m_base; /**< XComboBox 基类；必须是第一个成员。 */
} MbSerialCombo;

/** @brief 释放 XSerialPortInfo_availablePorts() 返回的列表（元素逐个释放）。 */
static void mb_serialPortInfoListFree(XSerialPortInfoList* list)
{
    size_t count;
    size_t i;
    if (!list) return;
    count = XVector_size_base((const XContainer*)list);
    for (i = 0; i < count; ++i) {
        XSerialPortInfo** slot =
            (XSerialPortInfo**)XVector_at_base(list, (int64_t)i);
        if (slot && *slot) XClassDelete(*slot);
    }
    XClassDelete(list);
}

/** @brief 重枚举本机串口并重建条目；保持原选择（仍在则选回，否则选首项）。
 *  @details 枚举失败（NULL）保留既有条目不动（宁可不刷新也不清空）；
 *           枚举成功但为空列表时补一条当前文本兜底（旧默认 /dev/ttyS1
 *           亦由此保留），避免组合框变空、连接参数读成空串。 */
static void mb_serialComboRefresh(XComboBox* combo)
{
    XSerialPortInfoList* list;
    XString* keep;
    const char* keepText;
    size_t count;
    size_t i;
    int idx;
    if (!combo) return;
    keep = XComboBox_currentText(combo); /* 自身副本，重填后可安全读取。 */
    keepText = keep ? XString_toUtf8(keep) : "";
    list = XSerialPortInfo_availablePorts();
    if (!list) {
        if (keep) XClassDelete(keep);
        return;
    }
    XComboBox_clear(combo);
    count = XVector_size_base((const XContainer*)list);
    for (i = 0; i < count; ++i) {
        XSerialPortInfo** slot =
            (XSerialPortInfo**)XVector_at_base(list, (int64_t)i);
        XSerialPortInfo* info = slot ? *slot : NULL;
        const XString* name;
        if (!info) continue;
        name = XSerialPortInfo_portName(info);
        if (name && XString_toUtf8(name) && XString_toUtf8(name)[0])
            XComboBox_addItem_2(combo, XString_toUtf8(name));
    }
    mb_serialPortInfoListFree(list);
    idx = (keepText && keepText[0])
              ? XComboBox_findText_2(combo, keepText) : -1;
    if (XComboBox_count(combo) <= 0)
        XComboBox_addItem_2(combo,
                            (keepText && keepText[0]) ? keepText
                                                      : "/dev/ttyS1");
    if (idx < 0 && XComboBox_count(combo) > 0) idx = 0;
    XComboBox_setCurrentIndex(combo, idx);
    if (keep) XClassDelete(keep);
}

/** @brief 左键按下：弹层将开未开时先重枚举（收起方向不刷）。 */
static void VMbSerialCombo_mousePressEvent(XWidget* self, XEvent* event)
{
    MbSerialCombo* combo = (MbSerialCombo*)self;
    if (combo && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        XMouseEvent_button((XMouseEvent*)event) == XMouseButton_LeftButton &&
        !combo->m_base.m_popupVisible)
        mb_serialComboRefresh(&combo->m_base);
    XClass_Parent(XComboBox, EXWidget_MousePressEvent,
                  XWidgetEventSlot)(self, event);
}

XVtable* MbSerialCombo_class_init(void)
{
    XVTABLE_INIT_DEFAULT(MbSerialCombo)
    XVTABLE_INHERIT_XCLASS(XComboBox);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VMbSerialCombo_mousePressEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 构建串口名下拉（含首轮枚举；初始项取当前可用串口）。 */
static XComboBox* mb_buildSerialCombo(XWidget* parent, int w)
{
    MbSerialCombo* combo = (MbSerialCombo*)XMalloc_System(
        sizeof(MbSerialCombo));
    if (!combo) return NULL;
    XMemset(combo, 0, sizeof(*combo));
    XComboBox_init(&combo->m_base, parent, 0);
    XClassSetVtable(combo, MbSerialCombo);
    XWidget_setFixedWidth((XWidget*)combo, w);
    XWidget_setFixedHeight((XWidget*)combo, 24);
    XWidget_show((XWidget*)combo);
    mb_serialComboRefresh(&combo->m_base); /* 首轮枚举（内置兜底默认名）。 */
    if (XComboBox_count(&combo->m_base) <= 0) {
        /* 首轮枚举失败/无串口：留默认名占位（连接侧同口径兜底）。 */
        XComboBox_addItem_2(&combo->m_base, "/dev/ttyS1");
        XComboBox_setCurrentIndex(&combo->m_base, 0);
    }
    return &combo->m_base;
}
#endif /* XMODBUS_RTU_ON */

static XSpinBox* mb_buildSpin(XWidget* parent, int w,
                              int min, int max, int val)
{
    XSpinBox* spin = XSpinBox_create(parent, 0);
    if (spin) {
        XSpinBox_setRange(spin, min, max);
        XSpinBox_setValue(spin, val);
        XWidget_setFixedWidth((XWidget*)spin, w);
        XWidget_setFixedHeight((XWidget*)spin, 24);
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
/* [四区同屏 2026-10-18] 本页所有「带索引/方向」的信号槽一律**按索引硬编
 *  码**（sender 在本框架派发时不可信，见 mb_slaveSetBitFromArgs 注释），
 *  不再做 sender 指针反查。 */

static void mb_masterStateSlot(XObject* sender, XVarList* args);
static void mb_masterWriteCoil(int index, bool on);
static void mb_masterDoAutoSlot(void* userData, XTimerData* timer);
static void mb_masterWriteReplyDone(XObject* sender, XVarList* args);
static void mb_masterDisconnectAutoSlot(void* userData, XTimerData* timer);
static void mb_masterRegReplyDone(XObject* sender, XVarList* args);
/* [2026-10-24 两表布局] 寄存器界面改从站同款 IR/HR 两张表（对齐样式）
 * ：原「类型/起始/数量/读」单表+列组自适应机器整建制退役——两区常驻
 * 常刷（轮询 DI→DO→IR→HR 四相），不存在选错监视档的问题。cellChanged
 * 桥槽（IR/HR 各一枚）→核心槽：值列 16 进制解析→FC6 写（单事务写让
 * 路同前）→"0x%04X" 规范化差量回写；地址列防误改回写（地址=行号 0 基
 * ，与从站表口径一致）。regSuppress（回填/扩容挂载/规范化回写）置位
 * 期间早退断环。 */
static void mb_masterIrCellChanged(XObject* receiver, XVarList* args);
static void mb_masterHrCellChanged(XObject* receiver, XVarList* args);
static void mb_masterRegCellChangedAt(XModbusRegisterType type, int row,
                                      int col);
static void mb_masterIrCountSlot(XObject* sender, XVarList* args);
static void mb_masterHrCountSlot(XObject* sender, XVarList* args);
static void mb_masterIrFmtSlot(XObject* sender, XVarList* args);
static void mb_masterHrFmtSlot(XObject* sender, XVarList* args);
static void mb_masterIrAddrSlot(XObject* sender, XVarList* args);
static void mb_masterHrAddrSlot(XObject* sender, XVarList* args);
static void mb_masterRewriteAddrCol(XTableWidget* tbl, int start);
static void mb_masterSetRegCfgEnabled(bool on);
static void mb_slaveMountRegRow(XTableWidget* tbl, int i, int hex);
static void mb_masterRegSyncViewport(void);
static void mb_masterSingleTxSlot(XObject* sender, XVarList* args);

/* ==================== 寄存器值显示格式（10/16 进制，两页共用） ====================
 * [2026-10-24 用户需求] 表格值显示方式可设：16进制/10进制。存储恒
 * uint16，切换只影响 显示格式/编辑解析/规范化回写 三个面；两页各一
 * 枚段内下拉（IR/HR 独立），默认主站 16 进制、从站 10 进制（沿用各自
 * 原行为）。 */

/** @brief 值 → 当前格式文本（hex: "0x%04X"；dec: "%u"）。 */
static void mb_regFormatText(char* buf, size_t cap, uint16_t v, int hex)
{
    if (hex) snprintf(buf, cap, "0x%04X", v);
    else snprintf(buf, cap, "%u", (unsigned)v);
}

/** @brief 文本 → 值（按当前格式解析；dec 模式容忍 0x 前缀——用户在
 *  10 进制档粘进 16 进制串时按 16 进制收，比静默截断成 0 温和）。 */
static long mb_regParseText(const char* text, int hex)
{
    if (!text || !text[0]) return 0;
    if (!hex && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        return strtol(text, NULL, 16);
    return strtol(text, NULL, hex ? 16 : 10);
}

/** @brief 寄存器表按点数扩缩（IR/HR 共用核心）：行数=SpinBox 现值，
 *  扩容补行（地址列只读 flags+值 "0"，与从站挂载同款）、缩容保值——
 *  回读 Fill 只填 0..行数-1，缩回后旧值仍在。抑制期防挂载/行数变化
 *  触发的 cellChanged 回环。 */
static void mb_masterRegApplyCount(XTableWidget* tbl, XSpinBox* spin,
                                   int* curCount, int hex)
{
    int n;
    int i;
    if (!tbl || !spin || !curCount) return;
    n = XSpinBox_value(spin);
    if (n < 1) n = 1;
    if (n > MB_SLAVE_REG_MAX_ROWS) n = MB_SLAVE_REG_MAX_ROWS;
    s_mb.regSuppress = 1;
    XTableWidget_setRowCount(tbl, n);
    for (i = 0; i < n; ++i)
        if (!XTableWidget_item(tbl, i, 0))
            mb_slaveMountRegRow(tbl, i, hex);
    s_mb.regSuppress = 0;
    *curCount = n;
}

/** @brief 显示格式切换重排：逐行按**旧格式**解析现文本、新格式回写
 *  （regSuppress 包全程断 cellChanged 环；值未变不发网络写）。 */
static void mb_masterRegReformat(XTableWidget* tbl, int oldHex, int newHex)
{
    int rows;
    int i;
    char buf[16];
    if (!tbl || oldHex == newHex) return;
    rows = XTableWidget_rowCount(tbl);
    s_mb.regSuppress = true;
    for (i = 0; i < rows; ++i) {
        const char* text = XTableWidget_text(tbl, i, 1);
        long v = mb_regParseText(text, oldHex);
        if (v < 0) v = 0;
        if (v > 0xFFFF) v = 0xFFFF;
        mb_regFormatText(buf, sizeof(buf), (uint16_t)v, newHex);
        if (!text || strcmp(text, buf) != 0)
            XTableWidget_setText(tbl, i, 1, buf);
    }
    s_mb.regSuppress = false;
}

/* 数量 SpinBox/按点数重建共用槽（IR/HR 各一枚； SpinBox 即时生效，
 * 按钮=显式触发点，与从站寄存器段同款交互）。行数变化=表高变化，
 * 扩缩后随即重定尺。 */
static void mb_masterIrCountSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_masterRegApplyCount(s_mb.irTable, s_mb.irCountSpin, &s_mb.irCount,
                           s_mb.irHex);
    mb_masterRewriteAddrCol(s_mb.irTable, s_mb.irAddr);
    mb_masterRegSyncViewport();
}

static void mb_masterHrCountSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_masterRegApplyCount(s_mb.hrTable, s_mb.hrCountSpin, &s_mb.hrCount,
                           s_mb.hrHex);
    mb_masterRewriteAddrCol(s_mb.hrTable, s_mb.hrAddr);
    mb_masterRegSyncViewport();
}

/* 显示格式切换槽（activated；重排现表 + 轮询回填/编辑解析随新格式）。 */
static void mb_masterIrFmtSlot(XObject* sender, XVarList* args)
{
    int old = s_mb.irHex;
    (void)sender; (void)args;
    s_mb.irHex = !(s_mb.irFmtCombo &&
                   XComboBox_currentIndex(s_mb.irFmtCombo) == 1);
    mb_masterRegReformat(s_mb.irTable, old, s_mb.irHex);
}

static void mb_masterHrFmtSlot(XObject* sender, XVarList* args)
{
    int old = s_mb.hrHex;
    (void)sender; (void)args;
    s_mb.hrHex = !(s_mb.hrFmtCombo &&
                   XComboBox_currentIndex(s_mb.hrFmtCombo) == 1);
    mb_masterRegReformat(s_mb.hrTable, old, s_mb.hrHex);
}

/** @brief 地址列整列重写（起始地址变更/扩容补行后调用）：行 i 地址=
 *  起始+i，suppress 断环（防误改回写槽会再入）。 */
static void mb_masterRewriteAddrCol(XTableWidget* tbl, int start)
{
    int rows;
    int i;
    char txt[12];
    if (!tbl) return;
    rows = XTableWidget_rowCount(tbl);
    s_mb.regSuppress = true;
    for (i = 0; i < rows; ++i) {
        snprintf(txt, sizeof(txt), "0x%04X", (unsigned)(start + i));
        if (strcmp(XTableWidget_text(tbl, i, 0), txt) != 0)
            XTableWidget_setText(tbl, i, 0, txt);
    }
    s_mb.regSuppress = false;
}

/* 起始地址变更槽：轮询单元/FC6 写/地址列全链=起始+行号，下一拍生效；
 * 表格地址列即时重写（值列不动，轮询自然刷新到新地址的值）。 */
static void mb_masterIrAddrSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (!s_mb.irAddrSpin) return;
    s_mb.irAddr = XSpinBox_value(s_mb.irAddrSpin);
    mb_masterRewriteAddrCol(s_mb.irTable, s_mb.irAddr);
}

static void mb_masterHrAddrSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (!s_mb.hrAddrSpin) return;
    s_mb.hrAddr = XSpinBox_value(s_mb.hrAddrSpin);
    mb_masterRewriteAddrCol(s_mb.hrTable, s_mb.hrAddr);
}

/* [10-19 框架编辑器迁移] 原手工挂框链（mb_masterRegEditBegin/RowSlot/
 * DblSlot，10-13/10-14 两轮根修的对象）整建制退役——编辑改由框架委托
 * 编辑器承担：双击（缺省 DoubleClicked 触发位）→edit()→内嵌行编辑器
 * （样式表 XItemLineEditor 规则着色），回车/失焦提交经基类
 * setItemText→模型 dataChanged 桥发 cellChanged 到
 * mb_masterRegCellChanged 落库。 */

/** @brief 寄存器值格/地址格提交核心（cellChanged 桥驱动，IR/HR 两表
 *  共用；表类型编译期由包装槽写死，行/列号取信号载荷）。
 *  - 值列（col==1）：16 进制解析→clamp→FC6 写模块（含单事务写让路，
 *    deferred 分支不回写文本=等回读 Fill 修正）→"0x%04X" 规范化差量
 *    回写（"abc"/越界提交后显示收敛）；
 *  - 地址列（col==0）：防误改回写（地址=行号 0 基，与从站表口径一致；
 *    挂载已设只读 flags，此处为双保险），已正确时早退。
 *  regSuppress（回填/扩容挂载/规范化回写）置位期间早退断环。 */
static void mb_masterRegCellChangedAt(XModbusRegisterType type, int row,
                                      int col)
{
    XTableWidget* tbl = (type == XModbusInputRegisters) ? s_mb.irTable
                                                        : s_mb.hrTable;
    int hex = (type == XModbusInputRegisters) ? s_mb.irHex : s_mb.hrHex;
    int base = (type == XModbusInputRegisters) ? s_mb.irAddr : s_mb.hrAddr;
    char txt[12];
    if (!tbl || row < 0 || row >= MB_SLAVE_REG_MAX_ROWS) return;
    if (s_mb.regSuppress) return;
    if (col == 0) {
        /* 地址=起始+行号（与轮询/写同源）。 */
        snprintf(txt, sizeof(txt), "0x%04X", (unsigned)(base + row));
        if (strcmp(XTableWidget_text(tbl, row, 0), txt) != 0) {
            s_mb.regSuppress = true;
            XTableWidget_setText(tbl, row, 0, txt);
            s_mb.regSuppress = false;
        }
        return;
    }
    if (col != 1) return;
    {
        const char* text = XTableWidget_text(tbl, row, 1);
        long val = mb_regParseText(text, hex);
        if (val < 0) val = 0;
        if (val > 0xFFFF) val = 0xFFFF;
        /* 写模块（回车=确认写；HR 可写，IR 写通常被模块拒——由应答
           体现）。基址+行号越出 65535 的行不写（地址空间外）。 */
        if (s_mb.device && base + row <= 65535) {
            XModbusDataUnit* unit;
            XModbusReply* reply;
            unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                             type, (uint16_t)(base + row),
                                             1);
            if (unit) {
                XModbusDataUnit_setValue(unit, 0, (uint16_t)val);
                /* [写让路+单事务 2026-10-15] 同单点翻转：单事务模式下
                   在途读先让路暂存（后写胜出），回读完成空闲槽补发；
                   deferred 分支直接返回——文本保持用户原输入，等回读
                   Fill 修正。 */
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
                    XObject_connect_1(
                        (XObject*)reply,
                        (size_t)XModbusReply_finished_signal(reply),
                        (XObject*)s_mb.root, mb_masterRegReplyDone,
                        XConnectionType_Direct);
                } else {
                    ++s_mb.errCount;
                }
            }
        }
        /* 规范化回写（差量）：编辑提交的原始文本收敛为当前格式口径。 */
        mb_regFormatText(txt, sizeof(txt), (uint16_t)val, hex);
        if (strcmp(XTableWidget_text(tbl, row, 1), txt) != 0) {
            s_mb.regSuppress = true;
            XTableWidget_setText(tbl, row, 1, txt);
            s_mb.regSuppress = false;
        }
    }
}

static void mb_masterIrCellChanged(XObject* receiver, XVarList* args)
{
    int row = -1;
    int col = -1;
    (void)receiver;
    if (!args) return;
    {
        XVarList_args_2(args, int, vr, int, vc);
        row = vr;
        col = vc;
    }
    mb_masterRegCellChangedAt(XModbusInputRegisters, row, col);
}

static void mb_masterHrCellChanged(XObject* receiver, XVarList* args)
{
    int row = -1;
    int col = -1;
    (void)receiver;
    if (!args) return;
    {
        XVarList_args_2(args, int, vr, int, vc);
        row = vr;
        col = vc;
    }
    mb_masterRegCellChangedAt(XModbusHoldingRegisters, row, col);
}


/** @brief 寄存器回读回填 16 进制表（IR/HR 两表共用，行 i=地址 i）。
 *  regSuppress 包全程：setItem 经模型桥发 cellChanged，落库槽须早退
 *  （回读不是用户编辑）。框架编辑器开着时同 Qt 语义（数据变化不回写
 *  编辑器，提交以编辑器内容为准）。 */
static void mb_masterRegFillTable(XTableWidget* tbl,
                                  const XModbusDataUnit* result, int hex)
{
    int i;
    int rows;
    if (!result || !tbl) return;
    rows = XTableWidget_rowCount(tbl);
    s_mb.regSuppress = true;
    for (i = 0; i < (int)XModbusDataUnit_valueCount(result) && i < rows;
         ++i) {
        uint16_t v = XModbusDataUnit_value(result, (size_t)i);
        char text[12];
        XTableWidgetItem item;
        mb_regFormatText(text, sizeof(text), v, hex);
        /* [脏区 2026-10-15] 同值跳过：setItem 已收窄为单格矩形失效，
           此处再免掉无变化写入——稳态轮询值不变时零像素失效，只有
           真变化的格子进脏区（Qt dataChanged 仅实际变更语义同）。 */
        {
            const XTableWidgetItem* cur = XTableWidget_item(tbl, i, 1);
            if (cur && cur->text &&
                strcmp(XString_toUtf8(cur->text), text) == 0)
                continue;
        }
        XMemset(&item, 0, sizeof(item));
        item.text = XString_create_utf8(text);
        XTableWidget_setItem(tbl, i, 1, &item);
        XClassDelete((XClass*)item.text);
    }
    s_mb.regSuppress = false;
}

/** @brief 寄存器写应答回收（读应答走主轮询 pendingReply 机器——两表
 *  布局后无手动读；寄存器写只剩表格编辑 FC6 一条路）。
 *  [根修 2026-10-18 认领语义沿用] sender 不可信，按挂载槽位认领。 */
static void mb_masterRegReplyDone(XObject* sender, XVarList* args)
{
    XModbusReply* reply = NULL;
    (void)args;
    if (sender && sender == (XObject*)s_mb.regPendingWriteReply) {
        reply = s_mb.regPendingWriteReply;
    } else if (s_mb.regPendingWriteReply) {
        reply = s_mb.regPendingWriteReply;
    }
    if (!reply) return;
    s_mb.regPendingWriteReply = NULL;
    XObject_deleteLater((XObject*)reply);
}

static void mb_masterReclaimCb(void* userData, XTimerData* timer);

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

/** @brief 输出全开/全关：逐格置勾选态并写模块。
 *  [根修 2026-10-18] 原实现 `sender == allOnBtn` 判向——同上，sender 非发射
 *  方，判向恒为假 ⇒ 全开被当成全关。拆成两个互不相似的槽，方向写死在
 *  编译期（与从站页 mb_slaveDoSetAll 同款）。 */
static void mb_masterDoSetAll(bool on)
{
    int i;
    if (!s_mb.device) return;
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

static void mb_masterAllOnSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_masterDoSetAll(true);
}

static void mb_masterAllOffSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_masterDoSetAll(false);
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

/** @brief 发起一轮输入点（DiscreteInputs）/线圈（Coils）/寄存器两表轮询。
 *  [2026-10-24 寄存器区入轮询+两表布局] 原相位机只有 DI/DO 两相，寄存
 *  器表仅手动「读」刷新——联调时从站改寄存器值主站表不跟新（位区全同
 *  步、寄存器区恒旧值的反差即此缺口）。相位轮转扩为 **DI→DO→IR→HR 四
 *  相**：IR/HR 相各读对应表的 0..行数-1（地址=行号，与从站两表口径一致
 *  ），应答分别回填 IR/HR 表（同值跳过=稳态零像素失效）。四相共用
 *  pendingReply/写让路/回收机器：单事务门控与 deferred 写冲洗对新相自
 *  动生效，无新增并发面。 */
static void mb_masterPollOnce(void)
{
    XModbusReply* reply;
    int phase;
    if (!s_mb.device || s_mb.pendingReply) return; /* 上一请求在途。 */
    /* 相位可用性轮转：DI 需输入点、DO 需输出点（[2026-10-13 DO 状态回读]
     * 模块勾选框回显），IR/HR 表行数>0 即参与。 */
    {
        int tries;
        for (tries = 0; tries < 4; ++tries) {
            if (s_mb.pollPhase == 0 && s_mb.inCount > 0) break;
            if (s_mb.pollPhase == 1 && s_mb.outCount > 0) break;
            if (s_mb.pollPhase == 2 && s_mb.irCount > 0) break;
            if (s_mb.pollPhase == 3 && s_mb.hrCount > 0) break;
            s_mb.pollPhase = (s_mb.pollPhase + 1) % 4;
        }
        if (tries >= 4) return; /* 四区皆不可用（防御，正常不可达）。 */
    }
    phase = s_mb.pollPhase;
    if (phase == 2 || phase == 3) {
        /* IR/HR 表：单元按 起始+行数 缓存复用，任一变更下一拍重建；
           越出 65535 地址空间的尾部行按协议上限钳掉（起始+数量-1
           >65535 时读不满，属正常协议行为，表格对应行保持旧文本）。 */
        XModbusDataUnit** unit = (phase == 2) ? &s_mb.irUnit : &s_mb.hrUnit;
        int isHr = (phase == 3);
        int base = isHr ? s_mb.hrAddr : s_mb.irAddr;
        int count = isHr ? s_mb.hrCount : s_mb.irCount;
        int cachedCount = isHr ? s_mb.hrUnitCountCache
                               : s_mb.irUnitCountCache;
        int cachedAddr = isHr ? s_mb.hrUnitAddrCache : s_mb.irUnitAddrCache;
        if (base < 0) base = 0;
        if (base > 65535) base = 65535;
        if (base + count > 65536) count = 65536 - base;
        if (count <= 0) {
            s_mb.pollPhase = (s_mb.pollPhase + 1) % 4;
            return;
        }
        if (!*unit || cachedCount != count || cachedAddr != base) {
            if (*unit) XClassDelete(*unit);
            *unit = XModbusDataUnit_create_ex(
                XCLASS_DEFAULT_MEMORY_TYPE,
                isHr ? XModbusHoldingRegisters : XModbusInputRegisters,
                (uint16_t)base, (size_t)count);
            if (!*unit) return;
            if (isHr) {
                s_mb.hrUnitCountCache = count;
                s_mb.hrUnitAddrCache = base;
            } else {
                s_mb.irUnitCountCache = count;
                s_mb.irUnitAddrCache = base;
            }
        }
        reply = XModbusClient_sendReadRequest((XModbusClient*)s_mb.device,
                                              *unit,
                                              XSpinBox_value(s_mb.unitSpin));
    } else if (phase == 1) {
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
        s_mb.pollPhaseSent = phase;
        s_mb.pendingReply = reply;
        XObject_connect_1((XObject*)reply,
                          (size_t)XModbusReply_finished_signal(reply),
                          (XObject*)s_mb.root, mb_masterOnReply,
                          XConnectionType_Direct);
    } else {
        ++s_mb.errCount;
    }
    s_mb.pollPhase = (s_mb.pollPhase + 1) % 4; /* 下一拍换相。 */
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
            if (result && s_mb.pollPhaseSent == 2) {
                /* IR 表回读回填（从站改值主站表跟新；同值跳过=稳态零
                   像素失效，见 mb_masterRegFillTable）。 */
                mb_masterRegFillTable(s_mb.irTable, result, s_mb.irHex);
            } else if (result && s_mb.pollPhaseSent == 3) {
                /* HR 表回读回填（同上）。 */
                mb_masterRegFillTable(s_mb.hrTable, result, s_mb.hrHex);
            } else if (result && s_mb.pollPhaseSent == 1) {
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
    if (s_mb.irUnit) {
        XClassDelete(s_mb.irUnit);
        s_mb.irUnit = NULL;
    }
    if (s_mb.hrUnit) {
        XClassDelete(s_mb.hrUnit);
        s_mb.hrUnit = NULL;
    }
    s_mb.pollPhase = 0;
    s_mb.pollPhaseSent = 0;
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
    /* [配置门控 2026-10-24] 断开解锁配置控件。 */
    mb_masterSetRegCfgEnabled(true);
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
            const char* name = s_mb.serialNameCombo
                ? XComboBox_currentText_2(s_mb.serialNameCombo) : "";
            int baud = 9600;
            if (!name || !name[0]) name = "/dev/ttyS1";
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
        /* [配置门控 2026-10-24] 会话中途禁改 起始/数量/重建/IO 点数。 */
        mb_masterSetRegCfgEnabled(false);
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

/** @brief 寄存器/IO 配置控件连接态门控（2026-10-24 用户需求）：已连接
 *  时禁改 起始地址/数量/按点数重建 与远程 IO 点数——会话中途改这些会
 *  让轮询/表格/从站所见不一致；断开（stopEngine）解锁。轮询周期与显示
 *  格式不受限（前者在线改周期既有槽安全，后者纯显示）。 */
static void mb_masterSetRegCfgEnabled(bool on)
{
    if (s_mb.irAddrSpin) XWidget_setEnabled((XWidget*)s_mb.irAddrSpin, on);
    if (s_mb.hrAddrSpin) XWidget_setEnabled((XWidget*)s_mb.hrAddrSpin, on);
    if (s_mb.irCountSpin) XWidget_setEnabled((XWidget*)s_mb.irCountSpin, on);
    if (s_mb.hrCountSpin) XWidget_setEnabled((XWidget*)s_mb.hrCountSpin, on);
    if (s_mb.applyIrBtn) XWidget_setEnabled((XWidget*)s_mb.applyIrBtn, on);
    if (s_mb.applyHrBtn) XWidget_setEnabled((XWidget*)s_mb.applyHrBtn, on);
    if (s_mb.inCountSpin) XWidget_setEnabled((XWidget*)s_mb.inCountSpin, on);
    if (s_mb.outCountSpin) XWidget_setEnabled((XWidget*)s_mb.outCountSpin, on);
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
    if (s_mb.serialNameCombo)
        XWidget_setVisible((XWidget*)s_mb.serialNameCombo,
                           idx == MB_MASTER_RTU);
    if (s_mb.serialBaudCombo)
        XWidget_setVisible((XWidget*)s_mb.serialBaudCombo,
                           idx == MB_MASTER_RTU);
    /* [LayoutRequest 2026-10-20] 显隐重排由框架接管：子控件 setVisible
     * 现已按 Qt 对齐（hide→父布局 invalidate→收口 post LayoutRequest，
     * show→沿可见父链同步激活），本页不再手工 updateGeometry。 */
}

/* ================================================================
 * 页 18：从站（Server）——全宽纵向四段式 2026-10-18
 *         [10-18 二次追加] 滚动条纪律 + 参数行合并 + 重建拆两
 * ================================================================
 * 返回给页堆叠的页根 = scroll（XScrollArea，水平条 AlwaysOff、竖条
 * AsNeeded——全页唯一滚动条）；内容件 = 内容根（root/pageRoot），
 * 宽恒跟随视口（水平跟随不滚）、高=内容首选高（超高由竖滚条承担，
 * 见 mb_slaveFitScrollContent）。内容根条目自上而下：
 *   layRoot(TopToBottom)
 *     ├─ 标题行        「Modbus 从站调试」
 *     ├─ layChannel    参数合并行：通道/监听端口/串口(随通道显隐)/本站地址
 *     ├─ layStart      启动/已停止/写事件行（页根上部不动）
 *     └─ dataArea      数据区（layData, TopToBottom）恒纵向单列：
 *         ├─ layDiTitle  「输入点 DI」+ 点数 SpinBox + 按点数重建(输入)
 *         │              + DI全开 + DI全关
 *         ├─ diGridHost  DI 圆圈换行铺排（放不下换行往下放一行，宿主
 *         │              宽=列数×单元宽、高=行数×单元高随宽度重算）
 *         ├─ layDoTitle  「输出点 DO」+ 点数 SpinBox + 按点数重建(输出)
 *         │              + 全开 + 全关
 *         ├─ doGridHost  DO 圆圈换行铺排（同上）
 *         ├─ layIrTitle  「输入寄存器 IR」+ 数量 SpinBox(1..256)
 *         │              [10-18 三次追加]
 *         ├─ irTable     IR 表（上下堆叠，见 applyRegGeometry 注释；
 *         │              行数=irCount，满高全展开+列宽自适应铺满）
 *         ├─ layHrTitle  「保持寄存器 HR」+ 数量 SpinBox(1..256)
 *         │              [10-18 三次追加]
 *         └─ hrTable     HR 表（同 irTable 口径，行数=hrCount）
 * 契约要点（逐条落在注释里，防后人改回旧口径）：
 *  1. 子布局只经 setLayout 绑到**控件**上，绝不 addLayout 进已绑控件的
 *     布局（XLayout.c:179-186 会解绑宿主）；行布局内的控件条目不
 *     reparent（C3）——标题行控件/灯格宿主/两表创建时父即 dataArea。
 *  2. 灯格**按索引硬编码槽**（toggled 的 sender 派发不可信，见
 *     mb_slaveSetBitFromArgs 注释）；寄存器编辑框**按 sender 指针反查**
 *     （editingFinished/textChanged 经 XLineEdit 以 self 为发射对象出栈，
 *     XSignalSlot.c:295 派发传发射对象 ⇒ 常驻框的 sender 恒等于该框自身，
 *     本页每框只挂一次、从不叠加；[10-18 三次追加] 行数 1..256 可变后
 *     逐行硬编码槽不可行，改 IR/HR 各一枚共享槽按指针反查行号）。
 *  3. 三个抑制标志的定性不同，见 MbSlave 结构体注释，勿混。
 *  4. server 常驻：停止只断连不销毁 ⇒ 停止期间仍可单机调点位。
 *  5. [滚动条纪律] DI/DO/表格任何子段一律不得出现滚动条：灯格换行
 *     （diScroll/doScroll 壳已删）、表满高全展开+列宽铺满；内容超高
 *     只由 scroll 的竖滚条承担。
 *  6. [10-19 框架编辑器迁移] 寄存器值格改走框架委托编辑器（表级
 *     setItemsEditable+DoubleClicked 触发），常驻挂框/页面侧重摆/
 *     逐框审计整建制退役——编辑器几何/可见性/生命周期由框架视图管理。
 */

static void mb_slaveSyncViewport(void);
static void mb_slaveRebuildGrids(void);
static void mb_slaveApplyRegGeometry(int availW);
static void mb_slaveEchoAll(void);
static void mb_slaveFitScrollContent(XScrollArea* area);
static XWidget* mb_slaveCreateScroll(XWidget* parent);
/* [10-19 框架编辑器迁移] 寄存器区可调行数；编辑/落库走框架委托编辑器
 * + cellChanged 桥（槽见双击链区域声明）。[2026-10-24] hex=值显示格式。 */
static void mb_slaveMountRegRow(XTableWidget* tbl, int i, int hex);
static void mb_slaveApplyRegCounts(void);

/** @brief 从站数据区：首次建 map 并挂四张单元；其后只调 setValueCount
 *         改容量（**保值**：不重建 map ⇒ 已写入的点位值不丢）。
 *  [根因修复 2026-10-18] 旧实现每次都新建 map + setMap_ref，实测点一次
 *  「按点数重建」或再点一次「启动」，本地写入的灯格/寄存器值全部归零。 */
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
    /* 已有单元：只改容量，值保留。[10-18 三次追加] IR/HR 两单元并入同
     * 一保值分支（数量 SpinBox 1..256 变更走这里：setValueCount 等量/
     * 增量重设不重建 map ⇒ 已写入的寄存器值不丢）。
     * [16点读回根修 2026-10-19] DI/DO 两张**位单元不再 resize**：实测
     * XModbusDataUnit_setValueCount 对位类型（XBitArray_resize 路径）的
     * 运行态扩容不生效——首建 8 后 setValueCount(16/24) map 仍按 8 判
     * 界，q≥9 的 FC1/FC2 全吃 IllegalDataAddress（TCP 写出端剥 0x80 位
     * 后线上伪装成「FC+ec 短帧」，主站 16 点轮询即 100% 错误率、显示停
     * 在初始全红；裸客户端 q=8 OK/q=9 起全挂、env 16/24 与运行时点
     * 「按点数重建」两条路径复现一致）。对策：位单元首建即按满容量
     * MB_SLAVE_MAX_POINTS 建足（见下），此后等量 setValueCount 亦无必要
     * ——地址 0..63 常开，「按点数重建」只改 UI/回显口径，map 永不重建
     * ⇒ 保值语义不变；主站按其配置点数读取全有效，超卖点读 0（等价
     * 「未配置点=0」）。寄存器（XVector 路径）扩容实测正常，照旧。 */
    if (s_mbSlave.unitIn && s_mbSlave.unitCoil &&
        s_mbSlave.unitIr && s_mbSlave.unitHr) {
        XModbusDataUnit_setValueCount(s_mbSlave.unitIr,
                                      (size_t)(s_mbSlave.irCount > 0
                                                  ? s_mbSlave.irCount : 1));
        XModbusDataUnit_setValueCount(s_mbSlave.unitHr,
                                      (size_t)(s_mbSlave.hrCount > 0
                                                  ? s_mbSlave.hrCount : 1));
        return;
    }
    map = XModbusDataUnitMap_create();
    if (!map) return;
    /* [16点读回根修 2026-10-19] 位单元首建即满容量 64（理由见上方保值
     * 分支注释——位类型运行态扩容不生效，不再依赖）；IR/HR 按当前数量。 */
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusDiscreteInputs, 0,
                                     (size_t)MB_SLAVE_MAX_POINTS);
    if (unit) {
        XMap_insert_base((XMapBase*)map, &s_mbSlave.inEnum, unit);
        XClassDelete(unit); /* map 已深拷贝，临时体即弃（缓存见下方统一取址）。 */
    }
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusCoils, 0,
                                     (size_t)MB_SLAVE_MAX_POINTS);
    if (unit) {
        XMap_insert_base((XMapBase*)map, &s_mbSlave.outEnum, unit);
        XClassDelete(unit);
    }
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusInputRegisters, 0,
                                     (size_t)(s_mbSlave.irCount > 0
                                                  ? s_mbSlave.irCount : 1));
    if (unit) {
        XMap_insert_base((XMapBase*)map, &s_mbSlave.irEnum, unit);
        XClassDelete(unit);
    }
    unit = XModbusDataUnit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     XModbusHoldingRegisters, 0,
                                     (size_t)(s_mbSlave.hrCount > 0
                                                  ? s_mbSlave.hrCount : 1));
    if (unit) {
        XMap_insert_base((XMapBase*)map, &s_mbSlave.hrEnum, unit);
        XClassDelete(unit);
    }
    /* [2026-10-23 UAF 根修] insert 走 XClass_copy_base 深拷贝：map 自持副本，
     * 上面的临时 unit 已逐个 XClassDelete——旧代码却把这四份**已释放**的
     * 临时指针缓存进 s_mbSlave.unitX，首建后缓存指针全部悬垂。ASAN 实证：
     * 页面 build 首建 → 「按点数重建」路径走上方保值分支，setValueCount
     * (unitIr) 读悬垂指针 heap-use-after-free abort（XModbusDataUnit.c:186
     * ← rebuildMap，ASAN autotest 因此到不了其后的窗口页）。
     * 对策：临时指针只负责 insert，缓存指针在四单元全部落位后按键取
     * map 内部**活体**（取值口径对齐 server 侧 XModbusServer.c:920
     * XMapBase_value_base）——保值分支 setValueCount 改容即改在 server
     * 实际服务的单元上，「不重建 map ⇒ 值保留」的语义才真正成立。
     * 取址放在全部 insert 之后：rb 树节点各自持 pair，不受后续插入搬移。 */
    s_mbSlave.unitIn = (XModbusDataUnit*)XMap_value_base(
                            (XMapBase*)map, &s_mbSlave.inEnum);
    s_mbSlave.unitCoil = (XModbusDataUnit*)XMap_value_base(
                            (XMapBase*)map, &s_mbSlave.outEnum);
    s_mbSlave.unitIr = (XModbusDataUnit*)XMap_value_base(
                            (XMapBase*)map, &s_mbSlave.irEnum);
    s_mbSlave.unitHr = (XModbusDataUnit*)XMap_value_base(
                            (XMapBase*)map, &s_mbSlave.hrEnum);
    XModbusServer_setMap_ref_base(s_mbSlave.server, map); /* Ref: 移交所有权。 */
}

/** @brief server 被销毁/换通道时清缓存指针（避免悬垂）。 */
static void mb_slaveDropUnits(void)
{
    s_mbSlave.unitIn = NULL;
    s_mbSlave.unitCoil = NULL;
    s_mbSlave.unitIr = NULL;
    s_mbSlave.unitHr = NULL;
}

/** @brief 本地单点差量写：灯格翻转 / 编辑框提交的唯一写路径。
 *  [本地-远端防打架 2026-10-18] writeData 尾部无条件发 dataWritten
 *  （XModbusServer.c:942），故必须夹 localWriteSuppress；另「值已相同
 *  则不写」把本地点击的回写量压到 1 格（旧的整列扫描已随灯格方案
 *  删除——那正是「全开数秒后整体掉回 OFF」的根因）。 */
static void mb_slaveSetBit(XModbusRegisterType type, int index, bool want)
{
    uint16_t cur = 0;
    if (!s_mbSlave.server || index < 0) return;
    if (!XModbusServer_data2(s_mbSlave.server, type, (uint16_t)index, &cur))
        return;
    if ((cur != 0) == want) return;
    s_mbSlave.localWriteSuppress = 1;
    XModbusServer_setData2(s_mbSlave.server, type, (uint16_t)index,
                           want ? 1u : 0u);
    s_mbSlave.localWriteSuppress = 0;
}

/** @brief 寄存器值格/地址格提交核心（cellChanged 桥驱动）。
 *  [10-19 框架编辑器迁移] 编辑改走框架委托编辑器（表级
 *  setItemsEditable+缺省 DoubleClicked 触发位，双击出内嵌行编辑框，
 *  回车/失焦提交经基类 setItemText→模型 setData→dataChanged 桥发
 *  cellChanged），本槽即该桥的 demo 侧落库端：
 *  - col==1：十进制 0..65535 解析→写数据区（差量早退）→规范化文本
 *    差量回写（"abc"/越界提交后显示收敛为合法值）；
 *  - col==0：地址列防误改回写（itemsEditable 是视图级开关，两列皆可
 *    双击编辑=Qt 缺省 item flags 语义；地址格被提交后按行号重写正确
 *    文本，地址文本已正确时早退）。
 *  - 本地 IR 保持可写（[R14 2026-10-18] writeData 不判表只读性，FC4
 *    只读是**主站侧**协议约束，与「单机可调」语义一致）。
 *  规范化/地址回写经 XTableWidget_setText 再入本槽：regEchoSuppress
 *  置位早退断环；本地写 map 段用 localWriteSuppress 掐 dataWritten
 *  回声（写事件计数语义=网络写，勿混）。 */
static void mb_slaveRegCellChangedAt(XModbusRegisterType type, int row,
                                     int col);

/** @brief cellChanged 桥两槽（IR/HR 各一枚，表类型编译期写死；
 *  行/列号取自信号载荷，无需逐行硬编码槽）。 */
static void mb_slaveIrCellChanged(XObject* receiver, XVarList* args);
static void mb_slaveHrCellChanged(XObject* receiver, XVarList* args);

/** @brief 值格提交核心实现（语义见前向声明处注释）。 */
static void mb_slaveRegCellChangedAt(XModbusRegisterType type, int row,
                                     int col)
{
    XTableWidget* tbl;
    char buf[12];
    if (!s_mbSlave.server) return;
    if (row < 0 || row >= MB_SLAVE_REG_MAX_ROWS) return;
    tbl = (type == XModbusInputRegisters) ? s_mbSlave.irTable
                                          : s_mbSlave.hrTable;
    if (!tbl) return;
    if (col == 0) {
        /* 地址列防误改回写：文本已正确早退（setText 会再入本槽，靠
         * 差量+suppress 双重断环）。 */
        snprintf(buf, sizeof(buf), "0x%04X", row);
        if (strcmp(XTableWidget_text(tbl, row, 0), buf) != 0) {
            s_mbSlave.regEchoSuppress = 1;
            XTableWidget_setText(tbl, row, 0, buf);
            s_mbSlave.regEchoSuppress = 0;
        }
        return;
    }
    if (col != 1) return;
    {
        const char* text = XTableWidget_text(tbl, row, 1);
        int hex = (type == XModbusInputRegisters) ? s_mbSlave.irHex
                                                  : s_mbSlave.hrHex;
        long want = mb_regParseText(text, hex);
        uint16_t cur = 0;
        if (want < 0) want = 0;
        if (want > 65535) want = 65535;
        if (!s_mbSlave.regEchoSuppress &&
            XModbusServer_data2(s_mbSlave.server, type, (uint16_t)row,
                                &cur) &&
            cur != (uint16_t)want) {
            s_mbSlave.localWriteSuppress = 1;
            XModbusServer_setData2(s_mbSlave.server, type, (uint16_t)row,
                                   (uint16_t)want);
            s_mbSlave.localWriteSuppress = 0;
        }
        /* 规范化回写（差量）：编辑提交的原始文本（如 "007"/"99999"）
         * 收敛为当前格式口径，与数据区读回一致。 */
        mb_regFormatText(buf, sizeof(buf), (uint16_t)want, hex);
        if (strcmp(XTableWidget_text(tbl, row, 1), buf) != 0) {
            s_mbSlave.regEchoSuppress = 1;
            XTableWidget_setText(tbl, row, 1, buf);
            s_mbSlave.regEchoSuppress = 0;
        }
    }
}

static void mb_slaveIrCellChanged(XObject* receiver, XVarList* args)
{
    int row = -1;
    int col = -1;
    (void)receiver;
    if (!args) return;
    {
        XVarList_args_2(args, int, vr, int, vc);
        row = vr;
        col = vc;
    }
    mb_slaveRegCellChangedAt(XModbusInputRegisters, row, col);
}

static void mb_slaveHrCellChanged(XObject* receiver, XVarList* args)
{
    int row = -1;
    int col = -1;
    (void)receiver;
    if (!args) return;
    {
        XVarList_args_2(args, int, vr, int, vc);
        row = vr;
        col = vc;
    }
    mb_slaveRegCellChangedAt(XModbusHoldingRegisters, row, col);
}

/** @brief 显示格式切换重排（从站版）：按旧格式解析现文本、新格式回写
 *  （regEchoSuppress 断环；数据区不动）。 */
static void mb_slaveRegReformat(XTableWidget* tbl, int oldHex, int newHex)
{
    int rows;
    int i;
    char buf[16];
    if (!tbl || oldHex == newHex) return;
    rows = XTableWidget_rowCount(tbl);
    s_mbSlave.regEchoSuppress = 1;
    for (i = 0; i < rows; ++i) {
        const char* text = XTableWidget_text(tbl, i, 1);
        long v = mb_regParseText(text, oldHex);
        if (v < 0) v = 0;
        if (v > 0xFFFF) v = 0xFFFF;
        mb_regFormatText(buf, sizeof(buf), (uint16_t)v, newHex);
        if (!text || strcmp(text, buf) != 0)
            XTableWidget_setText(tbl, i, 1, buf);
    }
    s_mbSlave.regEchoSuppress = 0;
}

/* 显示格式切换槽（activated；重排现表 + 回显/编辑解析随新格式）。 */
static void mb_slaveIrFmtSlot(XObject* sender, XVarList* args)
{
    int old = s_mbSlave.irHex;
    (void)sender; (void)args;
    s_mbSlave.irHex = !(s_mbSlave.irFmtCombo &&
                        XComboBox_currentIndex(s_mbSlave.irFmtCombo) == 1);
    mb_slaveRegReformat(s_mbSlave.irTable, old, s_mbSlave.irHex);
}

static void mb_slaveHrFmtSlot(XObject* sender, XVarList* args)
{
    int old = s_mbSlave.hrHex;
    (void)sender; (void)args;
    s_mbSlave.hrHex = !(s_mbSlave.hrFmtCombo &&
                        XComboBox_currentIndex(s_mbSlave.hrFmtCombo) == 1);
    mb_slaveRegReformat(s_mbSlave.hrTable, old, s_mbSlave.hrHex);
}

/** @brief 四组全刷回显（数据区→控件）：DI/DO 灯格 + IR/HR 编辑框。
 *  [四区同屏 2026-10-18] 下拉互斥切换已删 ⇒ 主站写来的变化四组全刷。
 *  两个 echoSuppress 是**防御性冗余**：灯格侧 setChecked 同值早退不
 *  emit（XAbstractButton.c:597-601）且槽内差量判断必然早退；编辑框侧
 *  程序化 setText 根本不发 editingFinished。保留只为双保险。 */
static void mb_slaveEchoAll(void)
{
    int i;
    if (!s_mbSlave.server) return;
    s_mbSlave.bitEchoSuppress = 1;
    for (i = 0; i < s_mbSlave.inCount && i < MB_SLAVE_MAX_POINTS; ++i) {
        uint16_t v = 0;
        if (s_mbSlave.diCells[i] &&
            XModbusServer_data2(s_mbSlave.server, s_mbSlave.inEnum,
                                (uint16_t)i, &v))
            XAbstractButton_setChecked((XAbstractButton*)s_mbSlave.diCells[i],
                                       v != 0);
    }
    for (i = 0; i < s_mbSlave.outCount && i < MB_SLAVE_MAX_POINTS; ++i) {
        uint16_t v = 0;
        if (s_mbSlave.doCells[i] &&
            XModbusServer_data2(s_mbSlave.server, s_mbSlave.outEnum,
                                (uint16_t)i, &v))
            XAbstractButton_setChecked((XAbstractButton*)s_mbSlave.doCells[i],
                                       v != 0);
    }
    s_mbSlave.bitEchoSuppress = 0;
    s_mbSlave.regEchoSuppress = 1;
    /* [10-19 框架编辑器迁移] 回显单路：统一写值列**文本 item**——
     * 值格无常驻框；框架编辑器开着时 Qt 同语义（数据变化不回写编辑器，
     * 提交以编辑器内容为准）。 */
    for (i = 0; i < s_mbSlave.irCount && i < MB_SLAVE_REG_MAX_ROWS; ++i) {
        uint16_t v = 0;
        char text[12];
        if (!XModbusServer_data2(s_mbSlave.server, s_mbSlave.irEnum,
                                 (uint16_t)i, &v))
            continue;
        mb_regFormatText(text, sizeof(text), v, s_mbSlave.irHex);
        {
            XTableWidgetItem item;
            XMemset(&item, 0, sizeof(item));
            item.text = XString_create_utf8(text);
            XTableWidget_setItem(s_mbSlave.irTable, i, 1, &item);
            XClassDelete((XClass*)item.text);
        }
    }
    for (i = 0; i < s_mbSlave.hrCount && i < MB_SLAVE_REG_MAX_ROWS; ++i) {
        uint16_t v = 0;
        char text[12];
        if (!XModbusServer_data2(s_mbSlave.server, s_mbSlave.hrEnum,
                                 (uint16_t)i, &v))
            continue;
        mb_regFormatText(text, sizeof(text), v, s_mbSlave.hrHex);
        {
            XTableWidgetItem item;
            XMemset(&item, 0, sizeof(item));
            item.text = XString_create_utf8(text);
            XTableWidget_setItem(s_mbSlave.hrTable, i, 1, &item);
            XClassDelete((XClass*)item.text);
        }
    }
    s_mbSlave.regEchoSuppress = 0;
}

/** @brief 从站写事件（主站改写数据区）：计数 + 四区回显。 */
static void mb_slaveOnDataWritten(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    /* [语义修正 2026-10-18] **先**查本地写抑制**再**计数：writeData 对
     * 本端 setData2 与远端网络写都会发 dataWritten（XModbusServer.c:942
     * 无条件发），若先计数，build 期回显/重建等内部 map 操作也会 +N，
     * 计数器语义变成「写事件」而非「网络写事件」（评审⑥：启动即 3、
     * 按点数重建再 +3）。抑制期内=本端写：不计数、也不回显。 */
    if (s_mbSlave.localWriteSuppress) return;
    ++s_mbSlave.writeEvents;
    if (s_mbSlave.statLabel) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "\xE5\x86\x99\xE4\xBA\x8B\xE4\xBB\xB6: %u",
                 s_mbSlave.writeEvents); /* 写事件: n */
        XLabel_setText_2(s_mbSlave.statLabel, buf);
    }
    mb_slaveEchoAll();
}

/** @brief 灯格翻转：按「索引硬编码」槽派发。
 *  [根因 2026-10-18] 本框架把 slot 作为**接收者侧**连接登记后，派发时传
 *  给槽的 sender 并非发射方自身——实测点 DO 灯格，mb_slaveLampToggled 收
 *  到的 sender 是**页根**（geometry=12,8 792x598 = pageRoot，vtable 也不是
 *  MbSwLamp），故「按 sender 指针反查行号」永远查不到 ⇒ 本地点灯/编辑
 *  一律写不进数据区（评审④/③ 的现象）。页 17 的 DO 灯格早就用
 *  mb_masterDoSlot0..15 逐点槽绕开了这一点，本页改为同一写法：
 *  一格一个槽、行号编译期写死，sender 是什么都不影响。 */
static void mb_slaveSetBitFromArgs(XModbusRegisterType type, int index,
                                   XVarList* args)
{
    bool on = false;
    if (args) {
        XVarList_args_1(args, bool, checked);
        on = checked;
    }
    mb_slaveSetBit(type, index, on);
}

#define MB_SLAVE_DI_SLOT(n) \
    static void mb_slaveLampDiSlot##n(XObject* s, XVarList* a) \
    { (void)s; mb_slaveSetBitFromArgs(XModbusDiscreteInputs, n, a); }
#define MB_SLAVE_DO_SLOT(n) \
    static void mb_slaveLampDoSlot##n(XObject* s, XVarList* a) \
    { (void)s; mb_slaveSetBitFromArgs(XModbusCoils, n, a); }

MB_SLAVE_DI_SLOT(0)  MB_SLAVE_DI_SLOT(1)  MB_SLAVE_DI_SLOT(2)
MB_SLAVE_DI_SLOT(3)  MB_SLAVE_DI_SLOT(4)  MB_SLAVE_DI_SLOT(5)
MB_SLAVE_DI_SLOT(6)  MB_SLAVE_DI_SLOT(7)  MB_SLAVE_DI_SLOT(8)
MB_SLAVE_DI_SLOT(9)  MB_SLAVE_DI_SLOT(10) MB_SLAVE_DI_SLOT(11)
MB_SLAVE_DI_SLOT(12) MB_SLAVE_DI_SLOT(13) MB_SLAVE_DI_SLOT(14)
MB_SLAVE_DI_SLOT(15) MB_SLAVE_DI_SLOT(16) MB_SLAVE_DI_SLOT(17)
MB_SLAVE_DI_SLOT(18) MB_SLAVE_DI_SLOT(19) MB_SLAVE_DI_SLOT(20)
MB_SLAVE_DI_SLOT(21) MB_SLAVE_DI_SLOT(22) MB_SLAVE_DI_SLOT(23)
MB_SLAVE_DI_SLOT(24) MB_SLAVE_DI_SLOT(25) MB_SLAVE_DI_SLOT(26)
MB_SLAVE_DI_SLOT(27) MB_SLAVE_DI_SLOT(28) MB_SLAVE_DI_SLOT(29)
MB_SLAVE_DI_SLOT(30) MB_SLAVE_DI_SLOT(31) MB_SLAVE_DI_SLOT(32)
MB_SLAVE_DI_SLOT(33) MB_SLAVE_DI_SLOT(34) MB_SLAVE_DI_SLOT(35)
MB_SLAVE_DI_SLOT(36) MB_SLAVE_DI_SLOT(37) MB_SLAVE_DI_SLOT(38)
MB_SLAVE_DI_SLOT(39) MB_SLAVE_DI_SLOT(40) MB_SLAVE_DI_SLOT(41)
MB_SLAVE_DI_SLOT(42) MB_SLAVE_DI_SLOT(43) MB_SLAVE_DI_SLOT(44)
MB_SLAVE_DI_SLOT(45) MB_SLAVE_DI_SLOT(46) MB_SLAVE_DI_SLOT(47)
MB_SLAVE_DI_SLOT(48) MB_SLAVE_DI_SLOT(49) MB_SLAVE_DI_SLOT(50)
MB_SLAVE_DI_SLOT(51) MB_SLAVE_DI_SLOT(52) MB_SLAVE_DI_SLOT(53)
MB_SLAVE_DI_SLOT(54) MB_SLAVE_DI_SLOT(55) MB_SLAVE_DI_SLOT(56)
MB_SLAVE_DI_SLOT(57) MB_SLAVE_DI_SLOT(58) MB_SLAVE_DI_SLOT(59)
MB_SLAVE_DI_SLOT(60) MB_SLAVE_DI_SLOT(61) MB_SLAVE_DI_SLOT(62)
MB_SLAVE_DI_SLOT(63)

MB_SLAVE_DO_SLOT(0)  MB_SLAVE_DO_SLOT(1)  MB_SLAVE_DO_SLOT(2)
MB_SLAVE_DO_SLOT(3)  MB_SLAVE_DO_SLOT(4)  MB_SLAVE_DO_SLOT(5)
MB_SLAVE_DO_SLOT(6)  MB_SLAVE_DO_SLOT(7)  MB_SLAVE_DO_SLOT(8)
MB_SLAVE_DO_SLOT(9)  MB_SLAVE_DO_SLOT(10) MB_SLAVE_DO_SLOT(11)
MB_SLAVE_DO_SLOT(12) MB_SLAVE_DO_SLOT(13) MB_SLAVE_DO_SLOT(14)
MB_SLAVE_DO_SLOT(15) MB_SLAVE_DO_SLOT(16) MB_SLAVE_DO_SLOT(17)
MB_SLAVE_DO_SLOT(18) MB_SLAVE_DO_SLOT(19) MB_SLAVE_DO_SLOT(20)
MB_SLAVE_DO_SLOT(21) MB_SLAVE_DO_SLOT(22) MB_SLAVE_DO_SLOT(23)
MB_SLAVE_DO_SLOT(24) MB_SLAVE_DO_SLOT(25) MB_SLAVE_DO_SLOT(26)
MB_SLAVE_DO_SLOT(27) MB_SLAVE_DO_SLOT(28) MB_SLAVE_DO_SLOT(29)
MB_SLAVE_DO_SLOT(30) MB_SLAVE_DO_SLOT(31) MB_SLAVE_DO_SLOT(32)
MB_SLAVE_DO_SLOT(33) MB_SLAVE_DO_SLOT(34) MB_SLAVE_DO_SLOT(35)
MB_SLAVE_DO_SLOT(36) MB_SLAVE_DO_SLOT(37) MB_SLAVE_DO_SLOT(38)
MB_SLAVE_DO_SLOT(39) MB_SLAVE_DO_SLOT(40) MB_SLAVE_DO_SLOT(41)
MB_SLAVE_DO_SLOT(42) MB_SLAVE_DO_SLOT(43) MB_SLAVE_DO_SLOT(44)
MB_SLAVE_DO_SLOT(45) MB_SLAVE_DO_SLOT(46) MB_SLAVE_DO_SLOT(47)
MB_SLAVE_DO_SLOT(48) MB_SLAVE_DO_SLOT(49) MB_SLAVE_DO_SLOT(50)
MB_SLAVE_DO_SLOT(51) MB_SLAVE_DO_SLOT(52) MB_SLAVE_DO_SLOT(53)
MB_SLAVE_DO_SLOT(54) MB_SLAVE_DO_SLOT(55) MB_SLAVE_DO_SLOT(56)
MB_SLAVE_DO_SLOT(57) MB_SLAVE_DO_SLOT(58) MB_SLAVE_DO_SLOT(59)
MB_SLAVE_DO_SLOT(60) MB_SLAVE_DO_SLOT(61) MB_SLAVE_DO_SLOT(62)
MB_SLAVE_DO_SLOT(63)

static XSlotFunc1 const kDiLampSlots[MB_SLAVE_MAX_POINTS] = {
    mb_slaveLampDiSlot0,  mb_slaveLampDiSlot1,  mb_slaveLampDiSlot2,
    mb_slaveLampDiSlot3,  mb_slaveLampDiSlot4,  mb_slaveLampDiSlot5,
    mb_slaveLampDiSlot6,  mb_slaveLampDiSlot7,  mb_slaveLampDiSlot8,
    mb_slaveLampDiSlot9,  mb_slaveLampDiSlot10, mb_slaveLampDiSlot11,
    mb_slaveLampDiSlot12, mb_slaveLampDiSlot13, mb_slaveLampDiSlot14,
    mb_slaveLampDiSlot15, mb_slaveLampDiSlot16, mb_slaveLampDiSlot17,
    mb_slaveLampDiSlot18, mb_slaveLampDiSlot19, mb_slaveLampDiSlot20,
    mb_slaveLampDiSlot21, mb_slaveLampDiSlot22, mb_slaveLampDiSlot23,
    mb_slaveLampDiSlot24, mb_slaveLampDiSlot25, mb_slaveLampDiSlot26,
    mb_slaveLampDiSlot27, mb_slaveLampDiSlot28, mb_slaveLampDiSlot29,
    mb_slaveLampDiSlot30, mb_slaveLampDiSlot31, mb_slaveLampDiSlot32,
    mb_slaveLampDiSlot33, mb_slaveLampDiSlot34, mb_slaveLampDiSlot35,
    mb_slaveLampDiSlot36, mb_slaveLampDiSlot37, mb_slaveLampDiSlot38,
    mb_slaveLampDiSlot39, mb_slaveLampDiSlot40, mb_slaveLampDiSlot41,
    mb_slaveLampDiSlot42, mb_slaveLampDiSlot43, mb_slaveLampDiSlot44,
    mb_slaveLampDiSlot45, mb_slaveLampDiSlot46, mb_slaveLampDiSlot47,
    mb_slaveLampDiSlot48, mb_slaveLampDiSlot49, mb_slaveLampDiSlot50,
    mb_slaveLampDiSlot51, mb_slaveLampDiSlot52, mb_slaveLampDiSlot53,
    mb_slaveLampDiSlot54, mb_slaveLampDiSlot55, mb_slaveLampDiSlot56,
    mb_slaveLampDiSlot57, mb_slaveLampDiSlot58, mb_slaveLampDiSlot59,
    mb_slaveLampDiSlot60, mb_slaveLampDiSlot61, mb_slaveLampDiSlot62,
    mb_slaveLampDiSlot63
};
static XSlotFunc1 const kDoLampSlots[MB_SLAVE_MAX_POINTS] = {
    mb_slaveLampDoSlot0,  mb_slaveLampDoSlot1,  mb_slaveLampDoSlot2,
    mb_slaveLampDoSlot3,  mb_slaveLampDoSlot4,  mb_slaveLampDoSlot5,
    mb_slaveLampDoSlot6,  mb_slaveLampDoSlot7,  mb_slaveLampDoSlot8,
    mb_slaveLampDoSlot9,  mb_slaveLampDoSlot10, mb_slaveLampDoSlot11,
    mb_slaveLampDoSlot12, mb_slaveLampDoSlot13, mb_slaveLampDoSlot14,
    mb_slaveLampDoSlot15, mb_slaveLampDoSlot16, mb_slaveLampDoSlot17,
    mb_slaveLampDoSlot18, mb_slaveLampDoSlot19, mb_slaveLampDoSlot20,
    mb_slaveLampDoSlot21, mb_slaveLampDoSlot22, mb_slaveLampDoSlot23,
    mb_slaveLampDoSlot24, mb_slaveLampDoSlot25, mb_slaveLampDoSlot26,
    mb_slaveLampDoSlot27, mb_slaveLampDoSlot28, mb_slaveLampDoSlot29,
    mb_slaveLampDoSlot30, mb_slaveLampDoSlot31, mb_slaveLampDoSlot32,
    mb_slaveLampDoSlot33, mb_slaveLampDoSlot34, mb_slaveLampDoSlot35,
    mb_slaveLampDoSlot36, mb_slaveLampDoSlot37, mb_slaveLampDoSlot38,
    mb_slaveLampDoSlot39, mb_slaveLampDoSlot40, mb_slaveLampDoSlot41,
    mb_slaveLampDoSlot42, mb_slaveLampDoSlot43, mb_slaveLampDoSlot44,
    mb_slaveLampDoSlot45, mb_slaveLampDoSlot46, mb_slaveLampDoSlot47,
    mb_slaveLampDoSlot48, mb_slaveLampDoSlot49, mb_slaveLampDoSlot50,
    mb_slaveLampDoSlot51, mb_slaveLampDoSlot52, mb_slaveLampDoSlot53,
    mb_slaveLampDoSlot54, mb_slaveLampDoSlot55, mb_slaveLampDoSlot56,
    mb_slaveLampDoSlot57, mb_slaveLampDoSlot58, mb_slaveLampDoSlot59,
    mb_slaveLampDoSlot60, mb_slaveLampDoSlot61, mb_slaveLampDoSlot62,
    mb_slaveLampDoSlot63
};


/** @brief 输出全开/全关（从站侧）：本地数据区逐点差量写。
 *  [sender 不可信 2026-10-18] 原实现用 `sender == allOnBtn` 判方向——与灯格
 *  同源的缺陷（槽收到的 sender 不是按钮本身），实测该判别恒为假 ⇒ 全开被
 *  当成全关。改为两个**互不相似**的槽，方向写死在编译期。 */
static void mb_slaveDoSetAll(bool on)
{
    int i;
    if (!s_mbSlave.server) return;
    s_mbSlave.bitEchoSuppress = 1;
    for (i = 0; i < s_mbSlave.outCount && i < MB_SLAVE_MAX_POINTS; ++i)
        if (s_mbSlave.doCells[i])
            XAbstractButton_setChecked((XAbstractButton*)s_mbSlave.doCells[i],
                                       on);
    s_mbSlave.bitEchoSuppress = 0;
    for (i = 0; i < s_mbSlave.outCount && i < MB_SLAVE_MAX_POINTS; ++i)
        if (s_mbSlave.doCells[i])
            mb_slaveSetBit(XModbusCoils, i, on);
}

static void mb_slaveAllOnSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveDoSetAll(true);
}

static void mb_slaveAllOffSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveDoSetAll(false);
}

/** @brief 输入全开/全关（从站侧）：与 mb_slaveDoSetAll 完全对称，目标
 *  换成 DiscreteInputs（[全宽纵向四段式 2026-10-18] DI 标题行新增「DI
 *  全开/全关」）。先 setChecked 再逐点 mb_slaveSetBit（localWriteSuppress
 *  包住 setData2，掐断 dataWritten 回声——防打架机制原样保留）。 */
static void mb_slaveDiSetAll(bool on)
{
    int i;
    if (!s_mbSlave.server) return;
    s_mbSlave.bitEchoSuppress = 1;
    for (i = 0; i < s_mbSlave.inCount && i < MB_SLAVE_MAX_POINTS; ++i)
        if (s_mbSlave.diCells[i])
            XAbstractButton_setChecked((XAbstractButton*)s_mbSlave.diCells[i],
                                       on);
    s_mbSlave.bitEchoSuppress = 0;
    for (i = 0; i < s_mbSlave.inCount && i < MB_SLAVE_MAX_POINTS; ++i)
        mb_slaveSetBit(XModbusDiscreteInputs, i, on);
}

static void mb_slaveDiAllOnSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveDiSetAll(true);
}

static void mb_slaveDiAllOffSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveDiSetAll(false);
}

/** @brief 初始化一行寄存器行（地址+值两格文本 item）。
 *  [10-19 框架编辑器迁移] 值格不再常驻挂 XLineEdit——观察态两格皆纯
 *  文本（观感对齐主站/Qt：平时无编辑框）；编辑走框架委托编辑器（表级
 *  setItemsEditable+缺省 DoubleClicked 触发位，双击出内嵌行编辑框，
 *  回车/失焦提交经模型 dataChanged 桥发 cellChanged 落库，见
 *  mb_slaveRegCellChangedAt）。build 期 0..N 与「数量 SpinBox 扩容
 *  补行」共用同一初始化；新行值随 applyRegCounts 尾部 echoAll 从 map
 *  回显（已填值不丢）。 */
static void mb_slaveMountRegRow(XTableWidget* tbl, int i, int hex)
{
    char addr[12];
    char val[12];
    XTableWidgetItem item;
    if (!tbl || i < 0 || i >= MB_SLAVE_REG_MAX_ROWS) return;
    snprintf(addr, sizeof(addr), "0x%04X", i);
    XMemset(&item, 0, sizeof(item));
    item.text = XString_create_utf8(addr);
    /* [10-19 对齐 Qt] 地址列只读：item 级 flags 去 ItemIsEditable
     * （0=缺省可编辑的兼容约定，故显式给「可选+可用」组合）——
     * 双击地址格不再进入编辑。 */
    item.itemFlags = XTableWidgetItemFlag_ItemIsSelectable |
                     XTableWidgetItemFlag_ItemIsEnabled;
    XTableWidget_setItem(tbl, i, 0, &item);
    XClassDelete((XClass*)item.text);
    mb_regFormatText(val, sizeof(val), 0, hex); /* 初值 0 按当前格式。 */
    XMemset(&item, 0, sizeof(item));
    item.text = XString_create_utf8(val);
    XTableWidget_setItem(tbl, i, 1, &item);
    XClassDelete((XClass*)item.text);
}

/** @brief 建一张寄存器表（2 列 × 当前行数，观察态纯文本）。
 *  [10-19 框架编辑器迁移] 表尾开视图级编辑门禁（对标 Qt item 缺省
 *  flags 含 ItemIsEditable；框架以视图开关承载，缺省 false 保持既有
 *  行为）并连 cellChanged→数据区落库槽（changedSlot 编译期定 IR/HR）。
 *  [常驻挂载 2026-10-18→已退役] 行数变化仍走 mb_slaveApplyRegCounts
 *  （扩容补行/收缩缩表），不整表重建。
 *  [10-18 三次追加] IR/HR 标识由**段标题行**承载（layIrTitle/layHrTitle）；
 *  表头口径随双击编辑更新为「值(双击编辑/回车生效)」。 */
static void mb_slaveBuildRegTable(XWidget* parent, XTableWidget** outTable,
                                  int rowCount, XObject* receiver,
                                  int hex, XSlotFunc1 changedSlot)
{
    static const char* const kHead[2] = {
        "\xE5\x9C\xB0\xE5\x9D\x80",             /* 地址 */
        "\xE5\x80\xBC(\xE5\x8F\x8C\xE5\x87\xBB\xE7\xBC\x96\xE8\xBE\x91/"
        "\xE5\x9B\x9E\xE8\xBD\xA6\xE7\x94\x9F\xE6\x95\x88)" /* 值(双击编辑/回车生效) */
    };
    XTableWidget* tbl;
    int i;
    if (!parent || !outTable || !receiver || !changedSlot) return;
    if (rowCount < 1) rowCount = 1;
    if (rowCount > MB_SLAVE_REG_MAX_ROWS) rowCount = MB_SLAVE_REG_MAX_ROWS;
    tbl = XTableWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, parent, 0);
    if (!tbl) return;
    XTableWidget_setColumnCount(tbl, 2);
    XTableWidget_setHorizontalHeaderLabels(tbl, kHead,
            (int)(sizeof(kHead) / sizeof(kHead[0])));
    XTableWidget_setRowCount(tbl, rowCount);
    /* [1019 晨 用户指令·表格零滚动条] 同主站寄存器表:AlwaysOff,
     * 尺寸自适应交由布局容器(表满高全展开,滚动壳纪律不变)。 */
    XAbstractScrollArea_setVerticalScrollBarPolicy(
        (XAbstractScrollArea*)tbl, XScrollBarPolicy_AlwaysOff);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)tbl, XScrollBarPolicy_AlwaysOff);
    for (i = 0; i < rowCount; ++i)
        mb_slaveMountRegRow(tbl, i, hex);
    XAbstractItemView_setItemsEditable(&tbl->m_base.m_base, true);
    XObject_connect_1((XObject*)tbl,
                      (size_t)XTableWidget_cellChanged_signal(
                          (XTableWidget*)NULL),
                      receiver, changedSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)tbl);
    *outTable = tbl;
}

/** @brief 灯格重建：DI/DO 各按「可见列数=段宽/单元宽」**换行铺排**。
 *  [10-18 二次追加·滚动条纪律] 旧「单行横排 + 横向滚动壳」删除——放不下
 *  换行往下放一行（对标主站页 IO 网格 (i%cols,i/cols) 换行口径
 *  ：1369-1385），宿主宽=列数×单元宽、高=行数×单元高随宽度重算；超高
 *  部分由整页竖滚区承担，DI/DO 子段不出滚动条。列数由 syncViewport
 *  解算（宽度驱动），点数由 SpinBox/重建按钮驱动，两者任一变化才重建。
 *  [R2 2026-10-18] 宿主仍须显式定尺**外加显式 sizeHint**：裸 XWidget 的
 *  sizeHint 初值 (-1,-1)（XWidget.c:3612）在布局度量里回退成 0（C7）；
 *  宿主作为 layData 条目参与「内容根首选高」求和（整页竖滚的内容高
 *  依赖该合计），不定 hint 会把内容首选高算矮。 */
static void mb_slaveRebuildGrids(void)
{
    int i;
    int cols;
    int diRows;
    int doRows;
    if (!s_mbSlave.diGridHost || !s_mbSlave.doGridHost) return;
    for (i = 0; i < MB_SLAVE_MAX_POINTS; ++i) {
        if (s_mbSlave.diCells[i]) {
            XClassDelete((XClass*)s_mbSlave.diCells[i]);
            s_mbSlave.diCells[i] = NULL;
        }
        if (s_mbSlave.doCells[i]) {
            XClassDelete((XClass*)s_mbSlave.doCells[i]);
            s_mbSlave.doCells[i] = NULL;
        }
    }
    /* 行数=ceil(点数/列数)；0 点时保一行兜底（布局里裸宿主 0 高会被
     * 度量回退成不可见）。宽/高与显式 sizeHint 同步落，见函数头注释。 */
    cols = s_mbSlave.gridCols > 0 ? s_mbSlave.gridCols : 1;
    diRows = (s_mbSlave.inCount + cols - 1) / cols;
    doRows = (s_mbSlave.outCount + cols - 1) / cols;
    if (diRows < 1) diRows = 1;
    if (doRows < 1) doRows = 1;
    {
        XSize hint;
        XWidget_setFixedWidth(s_mbSlave.diGridHost, cols * MB_CELL_W);
        XWidget_setFixedHeight(s_mbSlave.diGridHost, diRows * MB_CELL_H);
        XSize_init(&hint, cols * MB_CELL_W, diRows * MB_CELL_H);
        XWidget_setSizeHint(s_mbSlave.diGridHost, &hint);
        XWidget_setFixedWidth(s_mbSlave.doGridHost, cols * MB_CELL_W);
        XWidget_setFixedHeight(s_mbSlave.doGridHost, doRows * MB_CELL_H);
        XSize_init(&hint, cols * MB_CELL_W, doRows * MB_CELL_H);
        XWidget_setSizeHint(s_mbSlave.doGridHost, &hint);
    }
    for (i = 0; i < s_mbSlave.inCount && i < MB_SLAVE_MAX_POINTS; ++i) {
        MbSwLamp* cell = (MbSwLamp*)XMalloc_System(sizeof(MbSwLamp));
        if (!cell) break;
        MbSwLamp_init(cell, s_mbSlave.diGridHost, i, "DI");
        XWidget_setGeometry((XWidget*)cell,
                            (i % cols) * MB_CELL_W,
                            (i / cols) * MB_CELL_H,
                            MB_CELL_W - 4, MB_CELL_H - 4);
        XObject_connect_1((XObject*)cell,
                          (size_t)XAbstractButton_toggled_signal(
                              (XAbstractButton*)cell, false),
                          (XObject*)s_mbSlave.root, kDiLampSlots[i],
                          XConnectionType_Direct);
        s_mbSlave.diCells[i] = cell;
    }
    for (i = 0; i < s_mbSlave.outCount && i < MB_SLAVE_MAX_POINTS; ++i) {
        MbSwLamp* cell = (MbSwLamp*)XMalloc_System(sizeof(MbSwLamp));
        if (!cell) break;
        MbSwLamp_init(cell, s_mbSlave.doGridHost, i, "DO");
        XWidget_setGeometry((XWidget*)cell,
                            (i % cols) * MB_CELL_W,
                            (i / cols) * MB_CELL_H,
                            MB_CELL_W - 4, MB_CELL_H - 4);
        XObject_connect_1((XObject*)cell,
                          (size_t)XAbstractButton_toggled_signal(
                              (XAbstractButton*)cell, false),
                          (XObject*)s_mbSlave.root, kDoLampSlots[i],
                          XConnectionType_Direct);
        s_mbSlave.doCells[i] = cell;
    }
}

/** @brief 单表列宽重摆：地址:值 = 45%:55%（表宽变化时差量同步列宽）。 */
static void mb_slaveApplyRegColWidths(XTableWidget* tbl, int w)
{
    int addrW;
    if (!tbl) return;
    if (w <= 0) w = XWidget_width((XWidget*)tbl);
    if (w <= 0) w = MB_REG_MIN_COL_W;
    /* 行号列（XTW_HEADER_W=40）也占表宽，必须先扣掉，否则 45%/55% 的
     * 地址/值切分会把值列挤出可视区（实测值列被裁、表内出横滚条）。 */
    {
        int gutter = (int)tbl->m_headerWidth;
        int body = w - gutter;
        if (body < 20) body = 20;
        addrW = body * 45 / 100;
        if (addrW < 30) addrW = 30;
        XTableView_setColumnWidth(&tbl->m_base, 0, addrW);
        XTableView_setColumnWidth(&tbl->m_base, 1, body - addrW);
    }
}


/** @brief 寄存器表几何：IR/HR **上下堆叠**平铺进 layData，满高全展开。
 *  @details [上下堆叠而非并排的理由 2026-10-18] 并排各占一半须引入水平
 *           中间容器 + 两层嵌套布局，且本框架实测 XTableWidget 在盒式
 *           布局条目里不贡献可分配宽（旧宽档 irTable/hrTable 宽恒 0 的
 *           实测口径），每表还得按列容器宽定尺——几何链长、坑位多。
 *           上下堆叠把两表直接平铺进 layData（每层只有「控件+自己的
 *           布局」一跳），表宽直接 = dataArea 宽，与既有「定尺 +
 *           invalidate + activate + setGeometry」套路完全同构，实现最简。
 *           [10-18 二次追加·滚动条纪律] 旧「两遍法按视口余高对半」删除：
 *           表不再抢视口高度，高度恒= 水平表头高 + 16×统一行高（16 行
 *           全展开、行数 16 不变），垂直超出由整页竖滚区承担；表内无
 *           内容溢出 ⇒ 不出竖滚条，列宽仍按 地址:值=45:55 差量同步铺满
 *           表宽（行号列先扣）⇒ 不出横滚条。 */
static void mb_slaveApplyRegGeometry(int availW)
{
    XTableWidget* tabs[2];
    int i;
    int w;
    int h;
    tabs[0] = s_mbSlave.irTable;
    tabs[1] = s_mbSlave.hrTable;
    if (!tabs[0] && !tabs[1]) return;
    w = availW > 0 ? availW : MB_SLAVE_MIN_AVAIL_W;
    if (w < MB_REG_MIN_COL_W) w = MB_REG_MIN_COL_W;
    for (i = 0; i < 2; ++i) {
        int rows;
        if (!tabs[i]) continue;
        /* 满高 = 水平表头高 + 当前行数×统一行高（[10-18 三次追加] 行数
         * = irCount/hrCount 可调，读实际行数不抄常量；行高默认 24、
         * 表头高同理，读实际值）。 */
        rows = XTableWidget_rowCount(tabs[i]);
        h = (int)tabs[i]->m_headerHeight +
            XTableView_rowHeight(&tabs[i]->m_base) * rows;
        XWidget_setFixedWidth((XWidget*)tabs[i], w);
        XWidget_setFixedHeight((XWidget*)tabs[i], h);
    }
    /* setFixed 不失效布局（activate 会幂等早退），必须显式 invalidate。 */
    XLayout_invalidate((XLayout*)&s_mbSlave.layData);
    XLayout_activate((XLayout*)&s_mbSlave.layData);
    /* 显式落几何：保留 activate 摆出的 x/y，只回写定尺宽高（实测本框
     * 架 activate 有幂等早退路径，可能不把新定尺落到 widget 上）。 */
    for (i = 0; i < 2; ++i) {
        XTableWidget* tbl = tabs[i];
        int rows;
        if (!tbl) continue;
        rows = XTableWidget_rowCount(tbl);
        h = (int)tbl->m_headerHeight +
            XTableView_rowHeight(&tbl->m_base) * rows;
        XWidget_setGeometry((XWidget*)tbl,
                            XWidget_x((XWidget*)tbl),
                            XWidget_y((XWidget*)tbl), w, h);
        mb_slaveApplyRegColWidths(tbl, w);
    }
}


/** @brief 视口解算（幂等；build 末尾 / 内容根 resize / adapt 三入口共用，
 *  重复无害）。
 *  [全宽纵向四段式 2026-10-18] 双档（宽档横排/紧凑档纵排）与翻转逻辑
 *  整体删除——数据区恒纵向单列铺满整宽，无档位可言。
 *  [10-18 二次追加] 灯格**可见列数** = 段宽/单元宽（随宽度自动增减），
 *  列数或点数变化才重建（换行铺排，宿主高随行数重算）；表几何每拍
 *  跟随段宽（满高全展开 + 列宽铺满）。无独立滚动壳，无逐拍壳宽跟随。 */
static void mb_slaveSyncViewport(void)
{
    static int s_printed = 0;
    int availW;
    int cols;
    int maxPts;
    int rawW;
    if (!s_mbSlave.dataArea) return;
    /* 先激活页根布局再读数据区几何（基类 resize 路径的激活在事件
     * 派发之后，此处读宽须先行——同主站 MbMasterRoot 写法）。 */
    if (XWidget_layout(s_mbSlave.pageRoot))
        XLayout_activate(XWidget_layout(s_mbSlave.pageRoot));
    rawW = XWidget_width((XWidget*)s_mbSlave.dataArea);
    availW = rawW;
    /* 只防 0/负与未定尺兜底，真实几何优先（同旧口径）。 */
    if (availW < MB_SLAVE_MIN_AVAIL_W) availW = MB_SLAVE_MIN_AVAIL_W;
    /* 可见列数 = 宽度 / 单元宽（至少 1 列；宽度收紧时换行加行）。 */
    cols = availW / MB_CELL_W;
    if (cols < 1) cols = 1;
    maxPts = (s_mbSlave.inCount > s_mbSlave.outCount) ? s_mbSlave.inCount
                                                      : s_mbSlave.outCount;
    /* [R11 实测校正 2026-10-18] 首见**成规模**视口时打一条实测口径。
     * 门槛 rawW>200：堆叠布局首拍给的是控件默认尺寸（autotest 实测
     * vp=100x30），那时尚未就位，打出来无校正价值。 */
    if (!s_printed && rawW > 200) {
        s_printed = 1;
        XPrintf("XGuiWindowDemo: mbSlave vp=%dx%d avail=%d"
                " cols=%d pts=%d\n",
                s_mbSlave.pageRoot ? XWidget_width(s_mbSlave.pageRoot) : 0,
                s_mbSlave.pageRoot ? XWidget_height(s_mbSlave.pageRoot) : 0,
                rawW, cols, maxPts);
    }
    /* 幂等门控：只有列数或点数真变了才重建灯格（避免每次 resize 全量
     * 重建；表几何由 applyRegGeometry 每拍跟随，不经重建）。 */
    if (cols != s_mbSlave.gridCols || maxPts != s_mbSlave.gridPts) {
        s_mbSlave.gridCols = cols;
        s_mbSlave.gridPts = maxPts;
        mb_slaveRebuildGrids();
        mb_slaveEchoAll();
    }
    mb_slaveApplyRegGeometry(availW);
    /* [10-18 二次追加] 换行行数/表定尺变化都会改内容首选高——整页竖滚
     * 的内容高与滚动范围在此收敛（fit 内 setGeometry 无变化即幂等早退，
     * 不会与 resize 入口成环；内容高是视口宽的纯函数，不振荡）。 */
    mb_slaveFitScrollContent(s_mbSlave.scroll);
}

/** @brief 通道连接参数重刷（每次启动都跑 ⇒ 改端口后立即生效）。
 *  [泄漏修 2026-10-18] setConnectionParameter_move 两分支皆**所有权转移**
 *  （XModbusDevice.c:139-152：槽非空走 XClassMove，空走 XVariant_create_
 *  move），而 XVariant 是堆对象（XVariant.c:53-59 Set_Class_IsHeap）⇒
 *  转移后必须显式删外壳，否则每点一次「启动」漏一个。不用 _ref：那支
 *  是「删旧值+直接存指针」的借用语义，调用方还得保活。 */
static void mb_slaveApplyChannelParams(void)
{
    XModbusServer* srv = s_mbSlave.server;
    int channel = s_mbSlave.channelCombo
                    ? XComboBox_currentIndex(s_mbSlave.channelCombo) : 0;
    XVariant* var = NULL;
    if (!srv) return;
    XModbusServer_setServerAddress(srv, XSpinBox_value(s_mbSlave.unitSpin));
#if XMODBUS_TCP_ON
    if (channel == 0) {
        int port = s_mbSlave.tcpPortEdit
                       ? atoi(XLineEdit_text(s_mbSlave.tcpPortEdit)) : 15020;
        if (port <= 0 || port > 65535) port = 15020;
        var = XVariant_create_int(port);
        if (var) {
            XModbusDevice_setConnectionParameter_move(
                (XModbusDevice*)srv,
                XModbusDevice_NetworkPortParameter, var);
            XClassDelete((XClass*)var);
            var = NULL;
        }
    }
#endif
#if XMODBUS_RTU_ON
    if (channel == 1) {
        const char* name = s_mbSlave.serialNameCombo
                ? XComboBox_currentText_2(s_mbSlave.serialNameCombo) : "";
        int baud = 9600;
        if (!name || !name[0]) name = "/dev/ttyS1";
        if (s_mbSlave.serialBaudCombo)
            baud = atoi(XComboBox_currentText(s_mbSlave.serialBaudCombo));
        if (baud <= 0) baud = 9600;
        var = XString_toVariant(XString_create_utf8(name));
        if (var) {
            XModbusDevice_setConnectionParameter_move(
                (XModbusDevice*)srv,
                XModbusDevice_SerialPortNameParameter, var);
            XClassDelete((XClass*)var);
            var = NULL;
        }
        var = XVariant_create_int(baud);
        if (var) {
            XModbusDevice_setConnectionParameter_move(
                (XModbusDevice*)srv,
                XModbusDevice_SerialBaudRateParameter, var);
            XClassDelete((XClass*)var);
            var = NULL;
        }
    }
#endif
    (void)var;
}

/** @brief 建常驻 server 对象（幂等）：建对象即挂 map/接信号/刷参数。
 *  [常驻 2026-10-18] 写路径不需连接态——setData2 → writeData 只判
 *  server/unit/map/键/地址，全程不看 device state ⇒ 建好未监听也能
 *  单机点灯改寄存器。 */
static void mb_slaveEnsureServer(void)
{
    int channel = s_mbSlave.channelCombo
                    ? XComboBox_currentIndex(s_mbSlave.channelCombo) : 0;
    XModbusServer* srv = NULL;
    if (s_mbSlave.server) return;
#if XMODBUS_TCP_ON
    if (channel == 0) {
        XModbusTcpServer* ts = XModbusTcpServer_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE);
        if (ts) srv = (XModbusServer*)ts;
    }
#endif
#if XMODBUS_RTU_ON
    if (channel == 1) {
        XModbusRtuSerialServer* rs = XModbusRtuSerialServer_create_ex(
            XCLASS_DEFAULT_MEMORY_TYPE);
        if (rs) srv = (XModbusServer*)rs;
    }
#endif
    if (!srv) return;
    s_mbSlave.server = srv;
    mb_slaveRebuildMap();
    XObject_connect_1((XObject*)srv,
                      (size_t)XModbusServer_dataWritten_signal(NULL, 0, 0, 0),
                      (XObject*)s_mbSlave.root, mb_slaveOnDataWritten,
                      XConnectionType_Direct);
    mb_slaveApplyChannelParams();
}

/** @brief 从站点数/重建控件启动态门控（2026-10-24 用户需求）：监听中
 *  禁改 DI/DO/IR/HR 数量与「按点数重建」——服务会话中途改表形状会让
 *  主站轮询所见不一致；停止/启动失败解锁（停止期间可单机调）。 */
static void mb_slaveSetCountsEnabled(bool on)
{
    if (s_mbSlave.inCountSpin)
        XWidget_setEnabled((XWidget*)s_mbSlave.inCountSpin, on);
    if (s_mbSlave.outCountSpin)
        XWidget_setEnabled((XWidget*)s_mbSlave.outCountSpin, on);
    if (s_mbSlave.applyDiBtn)
        XWidget_setEnabled((XWidget*)s_mbSlave.applyDiBtn, on);
    if (s_mbSlave.applyDoBtn)
        XWidget_setEnabled((XWidget*)s_mbSlave.applyDoBtn, on);
    if (s_mbSlave.irCountSpin)
        XWidget_setEnabled((XWidget*)s_mbSlave.irCountSpin, on);
    if (s_mbSlave.hrCountSpin)
        XWidget_setEnabled((XWidget*)s_mbSlave.hrCountSpin, on);
    if (s_mbSlave.applyIrBtn)
        XWidget_setEnabled((XWidget*)s_mbSlave.applyIrBtn, on);
    if (s_mbSlave.applyHrBtn)
        XWidget_setEnabled((XWidget*)s_mbSlave.applyHrBtn, on);
}

/** @brief 从站启动/停止（server 常驻，停止只断连不销毁）。 */
static void mb_slaveStartStop(void)
{
    if (s_mbSlave.listening) {
        XModbusDevice_disconnectDevice((XModbusDevice*)s_mbSlave.server);
        s_mbSlave.listening = false;
        if (s_mbSlave.stateLabel)
            XLabel_setText_2(s_mbSlave.stateLabel,
                             "\xE5\xB7\xB2\xE5\x81\x9C\xE6\xAD\xA2"); /* 已停止 */
        if (s_mbSlave.startBtn)
            XPushButton_setText_2(s_mbSlave.startBtn,
                                  "\xE5\x90\xAF\xE5\x8A\xA8");  /* 启动 */
        mb_slaveSetCountsEnabled(true);
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
    mb_slaveSyncViewport();
    if (s_mbSlave.server) mb_slaveRebuildMap();
    mb_slaveEnsureServer();
    if (!s_mbSlave.server) return;
    /* 每次启动重刷参数：修「改端口/波特率后不生效」。 */
    mb_slaveApplyChannelParams();
    if (XModbusDevice_connectDevice((XModbusDevice*)s_mbSlave.server)) {
        s_mbSlave.listening = true;
        if (s_mbSlave.stateLabel)
            XLabel_setText_2(s_mbSlave.stateLabel,
                             "\xE7\x9B\x91\xE5\x90\xAC\xE4\xB8\xAD");
                             /* 监听中 */
        if (s_mbSlave.startBtn)
            XPushButton_setText_2(s_mbSlave.startBtn,
                                  "\xE5\x81\x9C\xE6\xAD\xA2");  /* 停止 */
        mb_slaveEchoAll();
        mb_slaveSetCountsEnabled(false);
    } else {
        /* [R12 行为变更 2026-10-18] 不再 deleteLater：失败保留对象，
         * 用户改端口可直接重试；停止期间数据区仍在（可继续单机调）。 */
        s_mbSlave.listening = false;
        if (s_mbSlave.stateLabel)
            XLabel_setText_2(s_mbSlave.stateLabel,
                             "\xE5\x90\xAF\xE5\x8A\xA8\xE5\xA4\xB1\xE8\xB4\xA5");
                             /* 启动失败 */
        if (s_mbSlave.startBtn)
            XPushButton_setText_2(s_mbSlave.startBtn,
                                  "\xE5\x90\xAF\xE5\x8A\xA8");  /* 启动 */
    }
}

/** @brief 从站启动按钮槽。 */
static void mb_slaveStartSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveStartStop();
}

/** @brief 从站通道切换槽：参数行（含标签）可见性 + 重建引擎对象。 */
static void mb_slaveOnChannelChanged(XObject* sender, XVarList* args)
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
    if (s_mbSlave.serialNameCombo)
        XWidget_setVisible((XWidget*)s_mbSlave.serialNameCombo, idx == 1);
    if (s_mbSlave.serialBaudCombo)
        XWidget_setVisible((XWidget*)s_mbSlave.serialBaudCombo, idx == 1);
    if (s_mbSlave.server) {
        /* 监听中不允许换通道（引擎类型已定型，否则要重连）。 */
        if (s_mbSlave.listening) return;
        /* [C16/R13 2026-10-18] 框架侧「发送方析构是否摘除接收方连接」
         * **未验证**（XObject.c:131-135 只释放自身 m_signalSlot；:89-125
         * 是事件过滤器表，不是信号反向索引）⇒ 不依赖框架语义，显式
         * 断开后再 deleteLater。 */
        XObject_disconnect_1((XObject*)s_mbSlave.server,
                             (size_t)XModbusServer_dataWritten_signal(
                                 NULL, 0, 0, 0),
                             (XObject*)s_mbSlave.root,
                             mb_slaveOnDataWritten);
        XObject_deleteLater((XObject*)s_mbSlave.server);
        s_mbSlave.server = NULL;
        mb_slaveDropUnits();   /* map 随 server 释放，缓存指针必须清。 */
    }
    mb_slaveEnsureServer();
    mb_slaveSyncViewport();
}

/** @brief 「按点数重建」公共实现（点数 SpinBox 调整后应用）。
 *  [10-18 用户追加规格⑤] 单按钮拆成 DI/DO 两枚后，本实现被两个分立槽
 *  共用：重建天然按两个 SpinBox 全量解算——mb_slaveRebuildMap 保值语义
 *  （不重建 map）原样保留。[16点读回根修 2026-10-19] DI/DO 位单元已满
 *  容量建足，本路径不再 resize 位单元（根因见 rebuildMap 注释），点数
 *  只影响灯格行数与回显范围。 */
static void mb_slaveApplyCounts(void)
{
    s_mbSlave.inCount = s_mbSlave.inCountSpin
                            ? XSpinBox_value(s_mbSlave.inCountSpin) : 8;
    s_mbSlave.outCount = s_mbSlave.outCountSpin
                             ? XSpinBox_value(s_mbSlave.outCountSpin) : 8;
    if (s_mbSlave.inCount > MB_SLAVE_MAX_POINTS)
        s_mbSlave.inCount = MB_SLAVE_MAX_POINTS;
    if (s_mbSlave.outCount > MB_SLAVE_MAX_POINTS)
        s_mbSlave.outCount = MB_SLAVE_MAX_POINTS;
    if (s_mbSlave.server) mb_slaveRebuildMap();
    /* 点数变 → 行数可能变 → 灯格重建（幂等门控在 syncViewport 内）。 */
    mb_slaveSyncViewport();
    mb_slaveEchoAll();
}

/** @brief 段①「按点数重建(输入)」槽：与段②槽互不相似（sender 不可信
 *  契约——方向/身份写死在槽身份里，不靠 sender 判别）。 */
static void mb_slaveApplyDiSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveApplyCounts();
}

/** @brief 段②「按点数重建(输出)」槽。 */
static void mb_slaveApplyDoSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveApplyCounts();
}

/** @brief IR/HR 数量 SpinBox 变更（[10-18 三次追加]）：1..256，变更走
 *  setValueCount 保值语义（mb_slaveRebuildMap 已把四单元全量并入保值
 *  分支）+ 表 setRowCount 同步 + 新行补文本行，已填值不丢。
 *  @details [10-19 框架编辑器迁移] 行观察态皆纯文本，扩缩容只剩行数
 *           同步：扩容新行 mb_slaveMountRegRow 写地址+初值，随后
 *           echoAll 从 map 回显（收缩前写进 map 的值随扩容回来——
 *           「已填值不丢」的闭环）；收缩由框架 setRowCount 缩表，无
 *           挂件指针要清。
 *           槽分立两枚（sender 不可信契约，同 applyDi/applyDo 口径）。 */
static void mb_slaveApplyRegCounts(void)
{
    int newIr = s_mbSlave.irCountSpin
                    ? XSpinBox_value(s_mbSlave.irCountSpin) : MB_SLAVE_REG_ROWS;
    int newHr = s_mbSlave.hrCountSpin
                    ? XSpinBox_value(s_mbSlave.hrCountSpin) : MB_SLAVE_REG_ROWS;
    XTableWidget* tabs[2];
    int counts[2];
    int t;
    if (newIr < 1) newIr = 1;
    if (newIr > MB_SLAVE_REG_MAX_ROWS) newIr = MB_SLAVE_REG_MAX_ROWS;
    if (newHr < 1) newHr = 1;
    if (newHr > MB_SLAVE_REG_MAX_ROWS) newHr = MB_SLAVE_REG_MAX_ROWS;
    tabs[0] = s_mbSlave.irTable;   tabs[1] = s_mbSlave.hrTable;
    counts[0] = newIr;             counts[1] = newHr;
    /* 先改 map 容量（保值）再动表：新行回显时值已可读。 */
    if (s_mbSlave.server) mb_slaveRebuildMap();
    for (t = 0; t < 2; ++t) {
        XTableWidget* tbl = tabs[t];
        int oldRows;
        int i;
        int hex = (t == 0) ? s_mbSlave.irHex : s_mbSlave.hrHex;
        if (!tbl) continue;
        oldRows = XTableWidget_rowCount(tbl);
        XTableWidget_setRowCount(tbl, counts[t]);
        if (counts[t] > oldRows) {
            for (i = oldRows; i < counts[t]; ++i)
                mb_slaveMountRegRow(tbl, i, hex);
        }
    }
    s_mbSlave.irCount = newIr;
    s_mbSlave.hrCount = newHr;
    mb_slaveEchoAll();       /* 新行从 map 回显；旧行刷新保值结果。 */
    /* 行数变化 ⇒ 表满高定尺与整页内容高都要重收敛（applyRegGeometry 读
     * 实际行数；syncViewport 幂等，重复无害）。 */
    mb_slaveSyncViewport();
}

/** @brief IR 数量 SpinBox 槽。 */
static void mb_slaveIrCountSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveApplyRegCounts();
}

/** @brief HR 数量 SpinBox 槽。 */
static void mb_slaveHrCountSlot(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    mb_slaveApplyRegCounts();
}

#if !defined(_WIN32) && XMODBUS_TCP_ON
/** @brief [CLOSE-WAIT 根修 2026-10-19] 死链客户端判定+回收（removeIf 谓词）。
 *  @details 探测手法与 posix 后端 EOF 复核同款（XDeviceNetwork_posix.c
 *           「假 EOF 复核」MSG_PEEK 分支）：对 fd 做 recv(MSG_PEEK|
 *           MSG_DONTWAIT)——**不消费字节**，与 io 环在途读无竞态：
 *           pr>0=有数据待读（false，交给正常 readyRead 路径）；
 *           pr<0 且 EAGAIN/EWOULDBLOCK=连接存活空闲（false）；
 *           pr==0=对端真 FIN（true，回收）；其余硬错误（ECONNRESET/
 *           ENOTSOCK/EBADF 等，含已 close 残留表项）一并回收。
 *           回收动作逐条镜像框架 XModbusTcpServer_onClientDisconnected
 *           +releaseClientSocket（全部公开 API）：摘映射表（return true
 *           交 removeIf）→断开本 socket 到 server 的信号→发
 *           modbusClientDisconnected（公开契约保持）→close+脱离父链+
 *           deleteLater（XTcpServer_incomingConnection 把 socket 挂为
 *           服务器子对象，只 close 不脱链会随断连累积到服务器销毁）。 */
static bool mb_slaveDeadClientPred(const void* pvKey, void* pvValue,
                                   void* args)
{
    XTcpSocket* client = *(XTcpSocket* const*)pvKey;
    XModbusServer* server = (XModbusServer*)args;
    intptr_t fd;
    ssize_t pr;
    unsigned char b;
    (void)pvValue;
    if (!client || !server) return false;
    fd = XTcpSocket_socketDescriptor_base((XAbstractSocket*)client);
    if (fd <= 0) return false; /* 未持描述符（理论不达），保守不回收。 */
    pr = recv((int)fd, &b, 1, MSG_PEEK | MSG_DONTWAIT);
    if (pr > 0) return false;              /* 有数据：活链，交正常路径。 */
    if (pr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return false;                      /* 空闲活链。 */
    /* 真 FIN（pr==0）或硬错误：回收。 */
    XObject_disconnect_1((XObject*)client, 0, (XObject*)server, NULL);
    XModbusTcpServer_modbusClientDisconnected_signal((XModbusTcpServer*)server,
                                                     client);
    XTcpSocket_close_base((XIODevice*)client);
    XObject_setParent((XObject*)client, NULL);
    XObject_deleteLater((XObject*)client);
    return true;
}

/** @brief 死链巡检定时器回调（1s 周期）：仅 TCP 服务器有意义（串口
 *  服务器无 m_connectedClients，强转读即垃圾——以通道是否 TCP 判别，
 *  server 类型恒随通道在 ensureServer 定型）。表空时 removeIf 零开销。 */
static void mb_slaveSweepDeadClientsCb(void* userData, XTimerData* timer)
{
    XModbusServer* server = s_mbSlave.server;
    XModbusTcpServer* tcp;
    (void)userData; (void)timer;
    if (!server || !s_mbSlave.channelCombo ||
        XComboBox_currentIndex(s_mbSlave.channelCombo) != 0)
        return;
    tcp = (XModbusTcpServer*)server;
    if (!tcp->m_connectedClients) return;
    XMapBase_removeIf_base((XMapBase*)tcp->m_connectedClients,
                           mb_slaveDeadClientPred, server);
}
#endif /* !_WIN32 && XMODBUS_TCP_ON */

/* ================================================================
 * 页面构建
 * ================================================================ */

/* ==================== 页根控件（表随视口重解算） ====================
 * [布局容器化 2026-10-17] adapt 置空后，寄存器表「列组随视口宽/显示
 * 行随视口高」的持续保持改由页根 resize 承接：先显式激活页根布局器
 * （基类 resize 路径的布局激活在事件派发之后，此处读表宽须先行），
 * 再按原 adapt 口径重解算表格内部——仅列宽/行数，不涉子件坐标。 */
static void mb_masterRegSyncViewport(void)
{
    /* [2026-10-24 两表布局] 表几何每拍跟随（mb_masterOnFit/扩容槽/构建
     * 末三入口）：宽=内容根宽（fit 链已设为视口宽），高=表头+行数×行高
     * （满高全展开，超页由整页竖滚承担），列宽 地址:值=45:55 铺满表宽
     * ——与从站 mb_slaveApplyRegGeometry 同套路。本框架实测
     * XTableWidget 在盒式布局条目不贡献可分配宽，必须显式定尺（旧单
     * 表的「撑满」是列组 sizeHint 恰近页宽的假象，两表 2 列 hint 只有
     * ~220px 就原形毕露）。 */
    XTableWidget* tabs[2];
    int w;
    int i;
    tabs[0] = s_mb.irTable;
    tabs[1] = s_mb.hrTable;
    if (!tabs[0] && !tabs[1]) return;
    w = s_mb.root ? XWidget_width(s_mb.root) : 0;
    if (w < 260) w = 260; /* fit 首拍前宽度未定，最小可读宽兜底。 */
    for (i = 0; i < 2; ++i) {
        XTableWidget* tbl = tabs[i];
        int rows;
        int h;
        if (!tbl) continue;
        rows = XTableWidget_rowCount(tbl);
        h = (int)tbl->m_headerHeight +
            XTableView_rowHeight(&tbl->m_base) * rows;
        XWidget_setFixedWidth((XWidget*)tbl, w);
        XWidget_setFixedHeight((XWidget*)tbl, h);
    }
    XLayout_invalidate((XLayout*)&s_mb.layRoot);
    XLayout_activate((XLayout*)&s_mb.layRoot);
    /* 显式落几何：保留 activate 摆出的 x/y，只回写定尺宽高（从站同款
     * 双保险，实测 activate 有幂等早退路径）。 */
    for (i = 0; i < 2; ++i) {
        XTableWidget* tbl = tabs[i];
        int rows;
        int h;
        if (!tbl) continue;
        rows = XTableWidget_rowCount(tbl);
        h = (int)tbl->m_headerHeight +
            XTableView_rowHeight(&tbl->m_base) * rows;
        XWidget_setGeometry((XWidget*)tbl, XWidget_x((XWidget*)tbl),
                            XWidget_y((XWidget*)tbl), w, h);
        mb_slaveApplyRegColWidths(tbl, w);
    }
}

XCLASS_DEFINE_BEGING(MbMasterRoot)
XCLASS_DEFINE_EXTEND_END(MbMasterRoot, XWidget)

/** @brief 主站页宿主：堆叠条目（页根）。resize 后铺满整页竖滚区
 *  scroll（滚动壳自身 resize 再驱动内容宽高收敛）。[10-19 对齐从站]
 *  旧口径「页根直铺+表格 stretch 钳入余高」作废：显示不全由页级竖滚
 *  条承担（屏 1024x600 显示不全的主诉）。 */
typedef struct MbMasterRoot
{
    XWidget m_base;      /**< XWidget 基类；必须是第一个成员。 */
} MbMasterRoot;

static void VMbMasterRoot_resizeEvent(XWidget* self, XEvent* event)
{
    XWidget* scroll;
    (void)event;
    if (!self) return;
    scroll = s_mb.scroll ? (XWidget*)s_mb.scroll : NULL;
    if (scroll) {
        XWidget_setGeometry(scroll, 0, 0, XWidget_width(self),
                            XWidget_height(self));
    }
}

XVtable* MbMasterRoot_class_init(void)
{
    XVTABLE_INIT_DEFAULT(MbMasterRoot)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VMbMasterRoot_resizeEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 构建主站页宿主（堆叠条目；scroll 在 build 内挂其下）。 */
static XWidget* mb_masterCreateRoot(XWidget* parent)
{
    MbMasterRoot* root = (MbMasterRoot*)XMalloc_System(sizeof(MbMasterRoot));
    if (!root) return (XWidget*)0;
    XMemset(root, 0, sizeof(*root));
    XWidget_init(&root->m_base, parent, 0);
    XClassSetVtable(root, MbMasterRoot);
    return &root->m_base;
}

typedef struct MbSlaveScroll MbSlaveScroll;
static void mb_slaveScrollSetOnFit(XWidget* scroll,
                                   void (*onFit)(MbSlaveScroll*));

/** @brief 主站 fit 收敛回调：内容布局 activate + 显示行解算（行数随
 *  视口高折算，变化由主调补第二遍 fit 收敛）。 */
static void mb_masterOnFit(MbSlaveScroll* scroll)
{
    XWidget* vp;
    (void)scroll;

    if (XWidget_layout(s_mb.root))
        XLayout_activate(XWidget_layout(s_mb.root));
    vp = XAbstractScrollArea_viewport((XAbstractScrollArea*)s_mb.scroll);
    s_mb.viewportH = vp ? XWidget_height(vp) : 0;
    mb_masterRegSyncViewport();
}

/** @brief [布局容器化 2026-10-17] 行/列布局装配定式：初始化并按序挂入
 *  父布局器（间距 4 对齐原 22/24 行高节奏；挂入次序=装配次序）。 */
static void mb_rowInit(XBoxLayout* row, XBoxLayout* parent,
                       XBoxLayoutDirection dir)
{
    XBoxLayout_init(row, dir);
    XLayout_setSpacing((XLayout*)row, 4);
    XBoxLayout_addLayout(parent, (XLayout*)row);
}

/** @brief [布局容器化 2026-10-17] 水平行左对齐收口：本框架布局器把
 *  行内多余空间均匀摊进行距（含链首/链尾），末尾补 stretch 条目把
 *  余量吃满——控件贴左成组，观感同改造前手写坐标。 */
static void mb_rowPack(XBoxLayout* row)
{
    XBoxLayout_addStretch(row, 1);
}

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
        /* [10-19 对齐从站] 堆叠条目 = 页宿主（MbMasterRoot,普通 XWidget
         * 条目——堆叠对 XScrollArea 条目的几何分配异常(实测只给
         * 184x150 且不再重摆),故滚动壳**内嵌页宿主内部**绕开）;页宿主
         * resize 铺满 scroll,scroll fit 驱动内容根(pageRoot)宽高。 */
        XWidget* pageHost = mb_masterCreateRoot(parent);
        XWidget* scrollRoot;
        XWidget* pageRoot;
        if (!pageHost) return (XWidget*)0;
        scrollRoot = mb_slaveCreateScroll(pageHost);
        if (!scrollRoot) {
            XObject_deleteLater(pageHost);
            return (XWidget*)0;
        }
        pageRoot = XWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                     scrollRoot, 0); /* 内容根(普通件)。 */
        if (!pageRoot) {
            XObject_deleteLater(scrollRoot);
            XObject_deleteLater(pageHost);
            return (XWidget*)0;
        }
        s_mb.pageHost = pageHost;
        s_mb.scroll = (XScrollArea*)scrollRoot;
        s_mb.pageRoot = pageRoot;
        s_mb.root = pageRoot;
        mb_slaveScrollSetOnFit(scrollRoot, mb_masterOnFit);
    }

    /* 页根纵向主布局：边距近似原装配 12/8；寄存器表 stretch 铺满余高
     * （原 adapt「表随视口拉满」的布局化承接）。 */
    XBoxLayout_init(&s_mb.layRoot, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_mb.layRoot, 12, 8, 12, 8);
    XLayout_setSpacing((XLayout*)&s_mb.layRoot, 4);

    /* 标题行。 */
    mb_buildLabel(s_mb.root,
                  "Modbus \xE4\xB8\xBB\xE7\xAB\x99\xE8\xB0\x83\xE8\xAF\x95",
                  300, &title); /* Modbus 主站调试 */
    if (title) {
        XLabel_setTextPixelSize(title, 16);
        XBoxLayout_addWidget(&s_mb.layRoot, (XWidget*)title);
    }

    /* 连接参数合并行（[10-20 用户追加规格] 原通道/服务器/串口/从站地址
     * 四行并一行——右边太空。随通道显隐的条目在隐藏时主轴取 0 宽
     * （XLayoutItem isEmpty），行内自折叠不占位，TCP/串口互斥显示）。 */
    mb_rowInit(&s_mb.layParams, &s_mb.layRoot,
               XBoxLayoutDirection_LeftToRight);
    {
        XLabel* lbl = NULL;
        mb_buildLabel(s_mb.root, "\xE9\x80\x9A\xE9\x81\x93", 56, &lbl);
                        /* 通道 */
        if (lbl) XBoxLayout_addWidget(&s_mb.layParams, (XWidget*)lbl);
    }
    s_mb.channelCombo = mb_buildChannelCombo(s_mb.root, 140);
    if (s_mb.channelCombo)
        XBoxLayout_addWidget(&s_mb.layParams, (XWidget*)s_mb.channelCombo);
    XBoxLayout_addSpacing(&s_mb.layParams, 12);
    XObject_connect_2((XObject*)s_mb.channelCombo,
                      XSignal(XComboBox_activated_signal),
                      mb_masterChannelSlot);

    /* TCP 参数组（随通道显隐）。 */
    mb_buildLabel(s_mb.root, "\xE6\x9C\x8D\xE5\x8A\xA1\xE5\x99\xA8",
                  56, &s_mb.tcpLbl); /* 服务器 */
    if (s_mb.tcpLbl)
        XBoxLayout_addWidget(&s_mb.layParams, (XWidget*)s_mb.tcpLbl);
    s_mb.tcpAddrEdit = XLineEdit_create(s_mb.root, 0);
    if (s_mb.tcpAddrEdit) {
        /* [联调] env XGUI_DEMO_MB_HOST 预置服务器地址（真机远程 IO 直连）。 */
        {
            const char* hostEnv = XSystem_environment("XGUI_DEMO_MB_HOST");
            XLineEdit_setText(s_mb.tcpAddrEdit,
                              (hostEnv && hostEnv[0]) ? hostEnv
                                                      : "127.0.0.1");
        }
        XWidget_setFixedWidth((XWidget*)s_mb.tcpAddrEdit, 120);
        XWidget_setFixedHeight((XWidget*)s_mb.tcpAddrEdit, 24);
        XWidget_show((XWidget*)s_mb.tcpAddrEdit);
        XBoxLayout_addWidget(&s_mb.layParams, (XWidget*)s_mb.tcpAddrEdit);
    }
    s_mb.tcpPortEdit = XLineEdit_create(s_mb.root, 0);
    if (s_mb.tcpPortEdit) {
        /* [联调] env XGUI_DEMO_MB_PORT 预置端口。 */
        {
            const char* portEnv = XSystem_environment("XGUI_DEMO_MB_PORT");
            XLineEdit_setText(s_mb.tcpPortEdit,
                              (portEnv && portEnv[0]) ? portEnv : "15020");
        }
        XWidget_setFixedWidth((XWidget*)s_mb.tcpPortEdit, 62);
        XWidget_setFixedHeight((XWidget*)s_mb.tcpPortEdit, 24);
        XWidget_show((XWidget*)s_mb.tcpPortEdit);
        XBoxLayout_addWidget(&s_mb.layParams, (XWidget*)s_mb.tcpPortEdit);
    }

    /* 串口参数组（随通道显隐，与 TCP 组互斥）。 */
    mb_buildLabel(s_mb.root, "\xE4\xB8\xB2\xE5\x8F\xA3", 44, &s_mb.serialLbl);
                    /* 串口 */
    if (s_mb.serialLbl) {
        XWidget_setVisible((XWidget*)s_mb.serialLbl, false);
        XBoxLayout_addWidget(&s_mb.layParams, (XWidget*)s_mb.serialLbl);
    }
    /* 串口名下拉：每次弹出前经 XSerialPortInfo 重枚举（见 MbSerialCombo）。 */
    s_mb.serialNameCombo = mb_buildSerialCombo(s_mb.root, 110);
    if (s_mb.serialNameCombo) {
        XWidget_setVisible((XWidget*)s_mb.serialNameCombo, false);
        XBoxLayout_addWidget(&s_mb.layParams,
                             (XWidget*)s_mb.serialNameCombo);
    }
    s_mb.serialBaudCombo = mb_buildBaudCombo(s_mb.root, 76);
    if (s_mb.serialBaudCombo) {
        XWidget_setVisible((XWidget*)s_mb.serialBaudCombo, false);
        XBoxLayout_addWidget(&s_mb.layParams,
                             (XWidget*)s_mb.serialBaudCombo);
    }
    XBoxLayout_addSpacing(&s_mb.layParams, 12);

    /* 从站地址（两通道共用）。 */
    {
        XLabel* lbl = NULL;
        mb_buildLabel(s_mb.root,
                      "\xE4\xBB\x8E\xE7\xAB\x99\xE5\x9C\xB0\xE5\x9D\x80",
                      80, &lbl); /* 从站地址 */
        if (lbl) XBoxLayout_addWidget(&s_mb.layParams, (XWidget*)lbl);
    }
    s_mb.unitSpin = mb_buildSpin(s_mb.root, 60, 1, 247, 1);
    if (s_mb.unitSpin)
        XBoxLayout_addWidget(&s_mb.layParams, (XWidget*)s_mb.unitSpin);

    /* [单事务模式 2026-10-15] TCP 通道专用多选框：勾选后写请求不与在途
     * 轮询读流水线并发——在途未完成时写排队（后写胜出），回读完成后
     * 补发。适配仅支持单事务处理的远程 IO 模块/从机。仅 TCP 模式显示
     * （RTU 串口本身即单事务半双工）。 */
    s_mb.singleTxCheck = XCheckBox_create(s_mb.root, 0);
    if (s_mb.singleTxCheck) {
        XAbstractButton_setText_2((XAbstractButton*)s_mb.singleTxCheck,
                                "\xE5\x8D\x95\xE4\xBA\x8B\xE5\x8A\xA1\xE6\xA8\xA1\xE5\xBC\x8F");
        /* 单事务模式 */
        XWidget_setFixedWidth((XWidget*)s_mb.singleTxCheck, 150);
        XWidget_setFixedHeight((XWidget*)s_mb.singleTxCheck, 24);
        XObject_connect_1((XObject*)s_mb.singleTxCheck,
                          (size_t)XAbstractButton_toggled_signal(
                              (XAbstractButton*)s_mb.singleTxCheck, false),
                          (XObject*)s_mb.root, mb_masterSingleTxSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)s_mb.singleTxCheck);
        XBoxLayout_addSpacing(&s_mb.layParams, 12);
        XBoxLayout_addWidget(&s_mb.layParams, (XWidget*)s_mb.singleTxCheck);
    }
    mb_rowPack(&s_mb.layParams);

    /* 远程 IO 模块适配：节标题独立成行。 */
    mb_buildLabel(s_mb.root,
                  "\xE8\xBF\x9C\xE7\xA8\x8B IO \xE6\xA8\xA1\xE5\x9D\x97",
                  130, &s_mb.ioLbl); /* 远程 IO 模块 */
    if (s_mb.ioLbl)
        XBoxLayout_addWidget(&s_mb.layRoot, (XWidget*)s_mb.ioLbl);

    /* 输入/输出点数行。 */
    mb_rowInit(&s_mb.layIo, &s_mb.layRoot, XBoxLayoutDirection_LeftToRight);
    mb_buildLabel(s_mb.root, "\xE8\xBE\x93\xE5\x85\xA5\xE7\x82\xB9",
                  60, &s_mb.inLbl); /* 输入点 */
    if (s_mb.inLbl)
        XBoxLayout_addWidget(&s_mb.layIo, (XWidget*)s_mb.inLbl);
    {
        const char* inEnv = XSystem_environment("XGUI_DEMO_MB_IN");
        s_mb.inCountSpin = mb_buildSpin(s_mb.root, 60, 0, 64,
                                        (inEnv && inEnv[0]) ? atoi(inEnv) : 8);
    }
    if (s_mb.inCountSpin)
        XBoxLayout_addWidget(&s_mb.layIo, (XWidget*)s_mb.inCountSpin);
    XBoxLayout_addSpacing(&s_mb.layIo, 22);
    mb_buildLabel(s_mb.root, "\xE8\xBE\x93\xE5\x87\xBA\xE7\x82\xB9",
                  60, &s_mb.outLbl); /* 输出点 */
    if (s_mb.outLbl)
        XBoxLayout_addWidget(&s_mb.layIo, (XWidget*)s_mb.outLbl);
    {
        const char* outEnv = XSystem_environment("XGUI_DEMO_MB_OUT");
        s_mb.outCountSpin = mb_buildSpin(s_mb.root, 60, 0, 64,
                                         (outEnv && outEnv[0]) ? atoi(outEnv)
                                                               : 8);
    }
    if (s_mb.outCountSpin)
        XBoxLayout_addWidget(&s_mb.layIo, (XWidget*)s_mb.outCountSpin);
    mb_rowPack(&s_mb.layIo);

    /* 轮询+连接行。 */
    mb_rowInit(&s_mb.layPoll, &s_mb.layRoot, XBoxLayoutDirection_LeftToRight);
    mb_buildLabel(s_mb.root, "\xE8\xBD\xAE\xE8\xAF\xA2(ms)",
                  80, &s_mb.pollLbl); /* 轮询(ms) */
    if (s_mb.pollLbl)
        XBoxLayout_addWidget(&s_mb.layPoll, (XWidget*)s_mb.pollLbl);
    s_mb.pollSpin = mb_buildSpin(s_mb.root, 70, 10, 5000, 1000);
    if (s_mb.pollSpin)
        XBoxLayout_addWidget(&s_mb.layPoll, (XWidget*)s_mb.pollSpin);
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
    XBoxLayout_addSpacing(&s_mb.layPoll, 40);
    s_mb.connectBtn = XPushButton_create(s_mb.root, 0);
    if (s_mb.connectBtn) {
        XPushButton_setText_2(s_mb.connectBtn,
                              "\xE8\xBF\x9E\xE6\x8E\xA5"); /* 连接 */
        XWidget_setFixedWidth((XWidget*)s_mb.connectBtn, 90);
        XWidget_setFixedHeight((XWidget*)s_mb.connectBtn, 28);
        XObject_connect_2((XObject*)s_mb.connectBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_masterConnectSlot);
        XWidget_show((XWidget*)s_mb.connectBtn);
        XBoxLayout_addWidget(&s_mb.layPoll, (XWidget*)s_mb.connectBtn);
    }
    mb_rowPack(&s_mb.layPoll);

    /* 状态/统计两行（定高 22 铺满行宽，文本左对齐=原 360 宽标签观感）。 */
    s_mb.stateLabel = XLabel_create(s_mb.root, 0);
    if (s_mb.stateLabel) {
        XLabel_setText_2(s_mb.stateLabel,
                         "\xE6\x9C\xAA\xE8\xBF\x9E\xE6\x8E\xA5"); /* 未连接 */
        XWidget_setFixedHeight((XWidget*)s_mb.stateLabel, 22);
        XWidget_show((XWidget*)s_mb.stateLabel);
        XBoxLayout_addWidget(&s_mb.layRoot, (XWidget*)s_mb.stateLabel);
    }
    s_mb.statLabel = XLabel_create(s_mb.root, 0);
    if (s_mb.statLabel) {
        XLabel_setText_2(s_mb.statLabel,
                         "\xE8\xAF\xB7\xE6\xB1\x82: 0  \xE9\x94\x99\xE8\xAF"
                         "\xAF: 0"); /* 请求: 0 错误: 0 */
        XWidget_setFixedHeight((XWidget*)s_mb.statLabel, 22);
        XWidget_show((XWidget*)s_mb.statLabel);
        XBoxLayout_addWidget(&s_mb.layRoot, (XWidget*)s_mb.statLabel);
    }

    /* 点位双列行：左=输入（只读指示），右=输出（点击翻转）；两列 360
     * 定宽对齐原装配（原 x=12/400 双列摆位的布局化近似），列距 28 同原。
     * 输出标题行内标题靠左、全开/全关经 addStretch 贴列右（原
     * x=564/636 摆位的布局化近似）。 */
    mb_rowInit(&s_mb.layGrid, &s_mb.layRoot, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&s_mb.layGrid, 28);

    /* 输入点列（只读指示）。 */
    mb_buildLabel(s_mb.root,
                  "\xE8\xBE\x93\xE5\x85\xA5\xE7\x82\xB9\xE7\x8A\xB6\xE6\x80"
                  "\x81(\xE5\x8F\xAA\xE8\xAF\xBB)",
                  180, &s_mb.diTitleLbl); /* 输入点状态(只读) */
    mb_rowInit(&s_mb.layGridIn, &s_mb.layGrid,
               XBoxLayoutDirection_TopToBottom);
    if (s_mb.diTitleLbl)
        XBoxLayout_addWidget(&s_mb.layGridIn, (XWidget*)s_mb.diTitleLbl);
    s_mb.inGridHost = XWidget_create(s_mb.root, 0);
    if (s_mb.inGridHost) {
        /* 定宽 360/定高 84（3 行点位，原紧凑档口径；网格单元坐标仍由
         * mb_masterRebuildGrids 按点数布点——表格内部口径不变）。 */
        XWidget_setFixedWidth(s_mb.inGridHost, 360);
        XWidget_setFixedHeight(s_mb.inGridHost, 84);
        XWidget_show(s_mb.inGridHost);
        XBoxLayout_addWidget(&s_mb.layGridIn, s_mb.inGridHost);
    }

    /* 输出点列（点击翻转）。 */
    mb_rowInit(&s_mb.layGridOut, &s_mb.layGrid,
               XBoxLayoutDirection_TopToBottom);
    mb_buildLabel(s_mb.root,
                  "\xE8\xBE\x93\xE5\x87\xBA\xE7\x82\xB9(\xE7\x82\xB9"
                  "\xE5\x87\xBB\xE7\xBF\xBB\xE8\xBD\xAC)",
                  160, &s_mb.doTitleLbl); /* 输出点(点击翻转) */
    mb_rowInit(&s_mb.layGridOutTitle, &s_mb.layGridOut,
               XBoxLayoutDirection_LeftToRight);
    if (s_mb.doTitleLbl)
        XBoxLayout_addWidget(&s_mb.layGridOutTitle, (XWidget*)s_mb.doTitleLbl);
    XBoxLayout_addStretch(&s_mb.layGridOutTitle, 1);
    s_mb.allOnBtn = XPushButton_create(s_mb.root, 0);
    if (s_mb.allOnBtn) {
        XPushButton_setText_2(s_mb.allOnBtn,
                              "\xE5\x85\xA8\xE5\xBC\x80"); /* 全开 */
        XWidget_setFixedWidth((XWidget*)s_mb.allOnBtn, 64);
        XWidget_setFixedHeight((XWidget*)s_mb.allOnBtn, 24);
        XObject_connect_2((XObject*)s_mb.allOnBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_masterAllOnSlot);
        XWidget_show((XWidget*)s_mb.allOnBtn);
        XBoxLayout_addWidget(&s_mb.layGridOutTitle, (XWidget*)s_mb.allOnBtn);
    }
    s_mb.allOffBtn = XPushButton_create(s_mb.root, 0);
    if (s_mb.allOffBtn) {
        XPushButton_setText_2(s_mb.allOffBtn,
                              "\xE5\x85\xA8\xE5\x85\xB3"); /* 全关 */
        XWidget_setFixedWidth((XWidget*)s_mb.allOffBtn, 64);
        XWidget_setFixedHeight((XWidget*)s_mb.allOffBtn, 24);
        XObject_connect_2((XObject*)s_mb.allOffBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_masterAllOffSlot);
        XWidget_show((XWidget*)s_mb.allOffBtn);
        XBoxLayout_addWidget(&s_mb.layGridOutTitle, (XWidget*)s_mb.allOffBtn);
    }
    s_mb.outGridHost = XWidget_create(s_mb.root, 0);
    if (s_mb.outGridHost) {
        XWidget_setFixedWidth(s_mb.outGridHost, 360);
        XWidget_setFixedHeight(s_mb.outGridHost, 84);
        XWidget_show(s_mb.outGridHost);
        XBoxLayout_addWidget(&s_mb.layGridOut, s_mb.outGridHost);
    }
    mb_rowPack(&s_mb.layGrid);

    /* ---- 寄存器两表面板（2026-10-24 改从站同款 IR/HR 分表）----
     * 原「类型/起始/数量/读」单表+写行退役：两区常驻常刷（轮询四相
     * DI→DO→IR→HR），不存在选错监视档的问题；值格双击编辑=FC6 写
     * （原写行能力由表格编辑承接），地址=行号 0 基与从站口径一致。 */
    mb_rowInit(&s_mb.layIrTitle, &s_mb.layRoot,
               XBoxLayoutDirection_LeftToRight);
    mb_buildLabel(s_mb.root,
                  "\xE8\xBE\x93\xE5\x85\xA5\xE5\xAF\x84\xE5\xAD\x98\xE5"
                  "\x99\xA8 IR", 150,
                  &s_mb.irTitleLbl); /* 输入寄存器 IR */
    if (s_mb.irTitleLbl)
        XBoxLayout_addWidget(&s_mb.layIrTitle, (XWidget*)s_mb.irTitleLbl);
    /* 起始地址（2026-10-24 用户需求）：远端寄存器不总在 0 基，读/写/
       地址列全链=起始+行号；已连接时禁改（见连接态门控）。 */
    mb_buildLabel(s_mb.root,
                  "\xE8\xB5\xB7\xE5\xA7\x8B", 36,
                  &s_mb.irStartLbl); /* 起始 */
    if (s_mb.irStartLbl)
        XBoxLayout_addWidget(&s_mb.layIrTitle, (XWidget*)s_mb.irStartLbl);
    s_mb.irAddrSpin = mb_buildSpin(s_mb.root, 60, 0, 65535, 0);
    if (s_mb.irAddrSpin) {
        XBoxLayout_addWidget(&s_mb.layIrTitle, (XWidget*)s_mb.irAddrSpin);
        XObject_connect_1((XObject*)s_mb.irAddrSpin,
                          (size_t)XSpinBox_valueChanged_signal(
                              (XSpinBox*)NULL),
                          (XObject*)s_mb.root, mb_masterIrAddrSlot,
                          XConnectionType_Direct);
    }
    s_mb.irAddr = 0;
    s_mb.irCountSpin = mb_buildSpin(s_mb.root, 64, 1, 16, 16);
    if (s_mb.irCountSpin) {
        XBoxLayout_addWidget(&s_mb.layIrTitle, (XWidget*)s_mb.irCountSpin);
        XObject_connect_1((XObject*)s_mb.irCountSpin,
                          (size_t)XSpinBox_valueChanged_signal(
                              (XSpinBox*)NULL),
                          (XObject*)s_mb.root, mb_masterIrCountSlot,
                          XConnectionType_Direct);
    }
    s_mb.applyIrBtn = XPushButton_create(s_mb.root, 0);
    if (s_mb.applyIrBtn) {
        XPushButton_setText_2(s_mb.applyIrBtn,
                              "\xE6\x8C\x89\xE7\x82\xB9\xE6\x95\xB0\xE9\x87"
                              "\x8D\xE5\xBB\xBA"); /* 按点数重建 */
        XWidget_setFixedWidth((XWidget*)s_mb.applyIrBtn, 110);
        XWidget_setFixedHeight((XWidget*)s_mb.applyIrBtn, 26);
        XObject_connect_2((XObject*)s_mb.applyIrBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_masterIrCountSlot);
        XWidget_show((XWidget*)s_mb.applyIrBtn);
        XBoxLayout_addWidget(&s_mb.layIrTitle, (XWidget*)s_mb.applyIrBtn);
    }
    /* 值显示格式（2026-10-24）：16进制/10进制，切换重排现表。 */
    s_mb.irFmtCombo = XComboBox_create(s_mb.root, 0);
    if (s_mb.irFmtCombo) {
        XComboBox_addItem_2(s_mb.irFmtCombo,
                "16\xE8\xBF\x9B\xE5\x88\xB6"); /* 16进制 */
        XComboBox_addItem_2(s_mb.irFmtCombo,
                "10\xE8\xBF\x9B\xE5\x88\xB6"); /* 10进制 */
        XComboBox_setCurrentIndex(s_mb.irFmtCombo, 0);
        XWidget_setFixedWidth((XWidget*)s_mb.irFmtCombo, 76);
        XWidget_setFixedHeight((XWidget*)s_mb.irFmtCombo, 24);
        XObject_connect_2((XObject*)s_mb.irFmtCombo,
                          XSignal(XComboBox_activated_signal),
                          mb_masterIrFmtSlot);
        XWidget_show((XWidget*)s_mb.irFmtCombo);
        XBoxLayout_addWidget(&s_mb.layIrTitle, (XWidget*)s_mb.irFmtCombo);
    }
    s_mb.irHex = 1;
    mb_rowPack(&s_mb.layIrTitle);
    mb_slaveBuildRegTable(s_mb.root, &s_mb.irTable, 16, s_mb.root,
                          s_mb.irHex, mb_masterIrCellChanged);
    if (s_mb.irTable)
        XBoxLayout_addWidget(&s_mb.layRoot, (XWidget*)s_mb.irTable);
    s_mb.irCount = 16;

    mb_rowInit(&s_mb.layHrTitle, &s_mb.layRoot,
               XBoxLayoutDirection_LeftToRight);
    mb_buildLabel(s_mb.root,
                  "\xE4\xBF\x9D\xE6\x8C\x81\xE5\xAF\x84\xE5\xAD\x98\xE5"
                  "\x99\xA8 HR", 150,
                  &s_mb.hrTitleLbl); /* 保持寄存器 HR */
    if (s_mb.hrTitleLbl)
        XBoxLayout_addWidget(&s_mb.layHrTitle, (XWidget*)s_mb.hrTitleLbl);
    mb_buildLabel(s_mb.root,
                  "\xE8\xB5\xB7\xE5\xA7\x8B", 36,
                  &s_mb.hrStartLbl); /* 起始 */
    if (s_mb.hrStartLbl)
        XBoxLayout_addWidget(&s_mb.layHrTitle, (XWidget*)s_mb.hrStartLbl);
    s_mb.hrAddrSpin = mb_buildSpin(s_mb.root, 60, 0, 65535, 0);
    if (s_mb.hrAddrSpin) {
        XBoxLayout_addWidget(&s_mb.layHrTitle, (XWidget*)s_mb.hrAddrSpin);
        XObject_connect_1((XObject*)s_mb.hrAddrSpin,
                          (size_t)XSpinBox_valueChanged_signal(
                              (XSpinBox*)NULL),
                          (XObject*)s_mb.root, mb_masterHrAddrSlot,
                          XConnectionType_Direct);
    }
    s_mb.hrAddr = 0;
    s_mb.hrCountSpin = mb_buildSpin(s_mb.root, 64, 1, 16, 16);
    if (s_mb.hrCountSpin) {
        XBoxLayout_addWidget(&s_mb.layHrTitle, (XWidget*)s_mb.hrCountSpin);
        XObject_connect_1((XObject*)s_mb.hrCountSpin,
                          (size_t)XSpinBox_valueChanged_signal(
                              (XSpinBox*)NULL),
                          (XObject*)s_mb.root, mb_masterHrCountSlot,
                          XConnectionType_Direct);
    }
    s_mb.applyHrBtn = XPushButton_create(s_mb.root, 0);
    if (s_mb.applyHrBtn) {
        XPushButton_setText_2(s_mb.applyHrBtn,
                              "\xE6\x8C\x89\xE7\x82\xB9\xE6\x95\xB0\xE9\x87"
                              "\x8D\xE5\xBB\xBA"); /* 按点数重建 */
        XWidget_setFixedWidth((XWidget*)s_mb.applyHrBtn, 110);
        XWidget_setFixedHeight((XWidget*)s_mb.applyHrBtn, 26);
        XObject_connect_2((XObject*)s_mb.applyHrBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_masterHrCountSlot);
        XWidget_show((XWidget*)s_mb.applyHrBtn);
        XBoxLayout_addWidget(&s_mb.layHrTitle, (XWidget*)s_mb.applyHrBtn);
    }
    s_mb.hrFmtCombo = XComboBox_create(s_mb.root, 0);
    if (s_mb.hrFmtCombo) {
        XComboBox_addItem_2(s_mb.hrFmtCombo,
                "16\xE8\xBF\x9B\xE5\x88\xB6"); /* 16进制 */
        XComboBox_addItem_2(s_mb.hrFmtCombo,
                "10\xE8\xBF\x9B\xE5\x88\xB6"); /* 10进制 */
        XComboBox_setCurrentIndex(s_mb.hrFmtCombo, 0);
        XWidget_setFixedWidth((XWidget*)s_mb.hrFmtCombo, 76);
        XWidget_setFixedHeight((XWidget*)s_mb.hrFmtCombo, 24);
        XObject_connect_2((XObject*)s_mb.hrFmtCombo,
                          XSignal(XComboBox_activated_signal),
                          mb_masterHrFmtSlot);
        XWidget_show((XWidget*)s_mb.hrFmtCombo);
        XBoxLayout_addWidget(&s_mb.layHrTitle, (XWidget*)s_mb.hrFmtCombo);
    }
    s_mb.hrHex = 1;
    mb_rowPack(&s_mb.layHrTitle);
    mb_slaveBuildRegTable(s_mb.root, &s_mb.hrTable, 16, s_mb.root,
                          s_mb.hrHex, mb_masterHrCellChanged);
    if (s_mb.hrTable)
        XBoxLayout_addWidget(&s_mb.layRoot, (XWidget*)s_mb.hrTable);
    s_mb.hrCount = 16;
    /* 装配收口：内容根挂布局器；内容根作 scroll 内容件（水平 AlwaysOff,
     * 竖条 AsNeeded=全页唯一滚动条；超高由竖滚承担）。 */
    XWidget_setLayout(s_mb.pageRoot, (XLayout*)&s_mb.layRoot);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)s_mb.scroll, XScrollBarPolicy_AlwaysOff);
    XScrollArea_setWidget(s_mb.scroll, s_mb.pageRoot);
    /* [程序化数量] env XGUI_DEMO_MB_COUNT=n：IR/HR 两表数量 SpinBox 同置
       n 并触发扩缩（动态行的程序化验证通道）。 */
    {
        const char* cntEnv = XSystem_environment("XGUI_DEMO_MB_COUNT");
        if (cntEnv && s_mb.irCountSpin && s_mb.hrCountSpin) {
            int n = atoi(cntEnv);
            if (n >= 1 && n <= 16) {
                XSpinBox_setValue(s_mb.irCountSpin, n);
                XSpinBox_setValue(s_mb.hrCountSpin, n);
                mb_masterIrCountSlot(NULL, NULL);
                mb_masterHrCountSlot(NULL, NULL);
                XPrintf("XGuiWindowDemo: modbus count=%d (IR/HR rows)\n", n);
            }
        }
    }
    mb_masterRebuildGrids();
    /* 两表首拍定尺（fit 链随后每拍跟随；宽度未定走最小可读宽兜底）。 */
    mb_masterRegSyncViewport();

    /* [端到端联调入口] env XGUI_DEMO_MB_AUTOSTART=master：构建后程序化
     * 连接（XAbstractButton_click 走完整激活流程；回环联调用）。
     * env XGUI_DEMO_MB_DO=索引列表(逗号分隔)：连接后 2 秒逐个勾选 DO——
     * 手动开关输出的程序化等效路径（XAbstractButton_click）。
     * [2026-10-24] regread 角色保留只连不读：寄存器区已常驻轮询，无手
     * 动读按钮可点。 */
    {
        const char* autoRole = XSystem_environment("XGUI_DEMO_MB_AUTOSTART");
        if (autoRole && (strcmp(autoRole, "master") == 0 ||
                         strcmp(autoRole, "regread") == 0 ||
                         strcmp(autoRole, "disconnect") == 0) &&
            s_mb.connectBtn) {
            XAbstractButton_click((XAbstractButton*)s_mb.connectBtn);
            XPrintf("XGuiWindowDemo: modbus autostart %s done\n", autoRole);
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
    /* [串口模式截图联调] env XGUI_DEMO_MB_SERIAL=1：构建后把通道切到
     * 串口 RTU（等价用户选择 combo 第 2 项，走 activated 槽完整路径），
     * 供截图/回归验证串口参数行布局。 */
    {
        const char* serialEnv = XSystem_environment("XGUI_DEMO_MB_SERIAL");
        if (serialEnv && serialEnv[0] && strcmp(serialEnv, "0") != 0 &&
            s_mb.channelCombo) {
            XComboBox_setCurrentIndex(s_mb.channelCombo, MB_MASTER_RTU);
            mb_masterChannelSlot(NULL, NULL);
            XPrintf("XGuiWindowDemo: modbus channel=RTU (serial mode) serial=%s\n",
                    s_mb.serialNameCombo
                        ? XComboBox_currentText_2(s_mb.serialNameCombo)
                        : "(none)");
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
    /* [10-19 对齐从站] 堆叠条目=页宿主(几何由堆叠分配;scroll 在其内
     * 铺满)。返回内容根会使页宿主永不在堆叠、几何永不分配(实测
     * scroll 卡 184x150)。 */
    return (XWidget*)s_mb.pageHost;
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
        /* [布局容器化 2026-10-17] 原几何相交断言改容器口径：行标签可见、
         * 输入框随布局分得非零尺寸、父子关系挂页根（坐标排布已移交
         * 布局器，读坐标不再构成契约）。 */
        if (!s_mb.tcpLbl || !XWidget_isVisible((XWidget*)s_mb.tcpLbl))
            ++failures;
        if (!s_mb.tcpAddrEdit ||
            XWidget_width((XWidget*)s_mb.tcpAddrEdit) <= 0 ||
            XWidget_height((XWidget*)s_mb.tcpAddrEdit) <= 0 ||
            XWidget_parentWidget((XWidget*)s_mb.tcpAddrEdit) !=
                s_mb.pageRoot) /* [10-19] 挂内容根(page=堆叠条目=页宿主)。 */
            ++failures;
        /* ---- [10-19 框架编辑器迁移] 编辑器开合/提交端到端回归锁 ----
         * edit(0,1) 开值格编辑器 → 改文本 → 直发 Return（拦截→委托
         * commit/close 链）→ 断言会话关闭+16 进制规范化回写（未连接，
         * 写模块段由 device 判空跳过，只走规范化回写）；地址列防误改
         * 回写同验。 */
        if (s_mb.irTable && s_mb.hrTable) {
            /* [2026-10-24 两表布局] IR/HR 各一张同款表：编辑门禁/地址
               只读 flags/提交规范化/防误改回写逐项锁（IR 表为主断言，
               HR 表补只读 flags 巡检）。 */
            XAbstractItemView* iv = &s_mb.irTable->m_base.m_base;
            if (!XAbstractItemView_itemsEditable(iv)) ++failures;
            if (!XAbstractItemView_itemsEditable(
                    &s_mb.hrTable->m_base.m_base))
                ++failures;
            /* [10-19 对齐 Qt item flags] 地址列只读：edit() 门禁拒绝
             * （首格=有数据格;末行地址格=空占位格,两态都必须拒绝）。 */
            if (XAbstractItemView_edit(iv, 0, 0)) ++failures;
            if (XAbstractItemView_edit(
                    iv, XTableWidget_rowCount(s_mb.irTable) - 1, 0))
                ++failures;
            /* 全地址格 flags 巡检（IR/HR 各全行）:必须显式只读。 */
            {
                int rr = XTableWidget_rowCount(s_mb.irTable);
                int r;
                for (r = 0; r < rr; ++r) {
                    uint32_t f = XTableWidget_itemFlags(s_mb.irTable, r, 0);
                    if ((f & XTableWidgetItemFlag_ItemIsEditable) || f == 0)
                        ++failures; /* 地址格必须显式只读。 */
                }
                rr = XTableWidget_rowCount(s_mb.hrTable);
                for (r = 0; r < rr; ++r) {
                    uint32_t f = XTableWidget_itemFlags(s_mb.hrTable, r, 0);
                    if ((f & XTableWidgetItemFlag_ItemIsEditable) || f == 0)
                        ++failures;
                }
            }
            if (XAbstractItemView_edit(iv, 0, 1) && iv->m_editor) {
                XKeyEvent* ke = XKeyEvent_create_ex(
                    XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_KEY_PRESS,
                    (int)XKey_Return, 0);
                XLineEdit_setText((XLineEdit*)iv->m_editor, "7b");
                if (ke) {
                    XObject_event_base((XObject*)iv->m_editor,
                                       (XEvent*)ke);
                    XClassDelete((XEvent*)ke);
                }
                if (XAbstractItemView_isEditing(iv)) ++failures;
                {
                    const char* cellText =
                        XTableWidget_text(s_mb.irTable, 0, 1);
                    if (!cellText || strcmp(cellText, "0x007B") != 0)
                        ++failures;
                }
                /* 地址列防误改：写坏文本→桥回写正确地址（=行号 0 基）。 */
                XTableWidget_setText(s_mb.irTable, 0, 0, "0xDEAD");
                {
                    const char* addrText =
                        XTableWidget_text(s_mb.irTable, 0, 0);
                    if (!addrText || strcmp(addrText, "0x0000") != 0)
                        ++failures;
                }
            } else {
                ++failures;
            }
            /* [2026-10-24 显示格式] 切 10 进制档：编辑解析/规范化随档；
               切回 16 进制：现表重排（99 → 0x0063）。 */
            if (s_mb.irFmtCombo) {
                XComboBox_setCurrentIndex(s_mb.irFmtCombo, 1);
                mb_masterIrFmtSlot(NULL, NULL);
                if (XAbstractItemView_edit(iv, 0, 1) && iv->m_editor) {
                    XKeyEvent* ke = XKeyEvent_create_ex(
                        XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_KEY_PRESS,
                        (int)XKey_Return, 0);
                    XLineEdit_setText((XLineEdit*)iv->m_editor, "99");
                    if (ke) {
                        XObject_event_base((XObject*)iv->m_editor,
                                           (XEvent*)ke);
                        XClassDelete((XEvent*)ke);
                    }
                    {
                        const char* cellText =
                            XTableWidget_text(s_mb.irTable, 0, 1);
                        if (!cellText || strcmp(cellText, "99") != 0)
                            ++failures; /* 10 进制档规范化。 */
                    }
                } else {
                    ++failures;
                }
                XComboBox_setCurrentIndex(s_mb.irFmtCombo, 0);
                mb_masterIrFmtSlot(NULL, NULL);
                {
                    const char* cellText =
                        XTableWidget_text(s_mb.irTable, 0, 1);
                    if (!cellText || strcmp(cellText, "0x0063") != 0)
                        ++failures; /* 切回 16 进制重排。 */
                }
            }
            /* [2026-10-24 起始地址] 改基址→地址列整列重写（行 i=起始+i）；
               恢复 0 基。 */
            if (s_mb.irAddrSpin) {
                XSpinBox_setValue(s_mb.irAddrSpin, 0x0F00);
                mb_masterIrAddrSlot(NULL, NULL);
                {
                    const char* a0 = XTableWidget_text(s_mb.irTable, 0, 0);
                    const char* a1 = XTableWidget_text(s_mb.irTable, 1, 0);
                    if (!a0 || strcmp(a0, "0x0F00") != 0)
                        ++failures;
                    if (!a1 || strcmp(a1, "0x0F01") != 0)
                        ++failures;
                }
                XSpinBox_setValue(s_mb.irAddrSpin, 0);
                mb_masterIrAddrSlot(NULL, NULL);
                {
                    const char* a0 = XTableWidget_text(s_mb.irTable, 0, 0);
                    if (!a0 || strcmp(a0, "0x0000") != 0)
                        ++failures;
                }
            }
        }
        return failures;
    }
}


XCLASS_DEFINE_BEGING(MbSlaveRoot)
XCLASS_DEFINE_EXTEND_END(MbSlaveRoot, XWidget)

/** @brief 从站内容根控件：resize 后先激活页根布局再重解算四区档位。
 *  [10-18 二次追加] 本件退居 scroll 内容件（页堆叠条目= scroll），
 *  宽由 fitScrollContent 设为视口宽——本钩子随之被触发（setGeometry
 *  内部先 activate 再派发 RESIZE），完成换行灯格/表几何重解算。 */
typedef struct MbSlaveRoot
{
    XWidget m_base;      /**< XWidget 基类；必须是第一个成员。 */
} MbSlaveRoot;

static void VMbSlaveRoot_resizeEvent(XWidget* self, XEvent* event)
{
    (void)event;
    if (!self) return;
    if (XWidget_layout(self))
        XLayout_activate(XWidget_layout(self));
    mb_slaveSyncViewport();
}

XVtable* MbSlaveRoot_class_init(void)
{
    XVTABLE_INIT_DEFAULT(MbSlaveRoot)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VMbSlaveRoot_resizeEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 构建从站内容根（带 resize 重解算钩子的 XWidget 子类实例）。 */
static XWidget* mb_slaveCreateRoot(XWidget* parent)
{
    MbSlaveRoot* root = (MbSlaveRoot*)XMalloc_System(sizeof(MbSlaveRoot));
    if (!root) return (XWidget*)0;
    XMemset(root, 0, sizeof(*root));
    XWidget_init(&root->m_base, parent, 0);
    XClassSetVtable(root, MbSlaveRoot);
    return &root->m_base;
}

/* ==================== 整页竖滚区（全页唯一滚动条） ====================
 * [10-18 二次追加·滚动条纪律] 页根包一个只竖滚的 XScrollArea 挂入页
 * 堆叠：水平条 AlwaysOff、竖条 AsNeeded（默认）；内容宽恒=视口宽
 * （水平跟随不滚），内容高=内容首选高（超高只出右侧一条竖滚条）。
 * widgetResizable 必须保持 false：resizable=true 会把内容几何钉成
 * 视口尺寸（XScrollArea.c:40-45），内容永远不超视口 ⇒ 竖滚无从发生；
 * 改由本子类 resizeEvent 自管内容宽高（resizable=false 时基类按内容
 * 自身尺寸排版并 setContentSize，本钩子随后重定宽高+刷新滚动范围）。 */
XCLASS_DEFINE_BEGING(MbSlaveScroll)
XCLASS_DEFINE_EXTEND_END(MbSlaveScroll, XScrollArea)

typedef struct MbSlaveScroll
{
    XScrollArea m_base;  /**< XScrollArea 基类；必须是第一个成员。 */
    /* [10-19 主站接入] fit 收敛后的页回调（主站：内容布局 activate +
     * 表显示行解算——行数变化改内容首选高，回调返回后主调再补一遍
     * fit 收敛；从站不设=NULL，行数解算走内容根 resizeEvent 既有链）。 */
    void (*onFit)(struct MbSlaveScroll* scroll);
} MbSlaveScroll;

/** @brief 整页竖滚承载：内容宽=视口宽、高=内容首选高，两遍收敛。
 *  @details 第一遍 activate 可能触发内容根 resize→syncViewport→灯格
 *           换行行数变化（宿主高变）⇒ 首选高已陈旧；第二遍清合计
 *           缓存重读（XLayout 缓存须显式 invalidate，setFixed/setSizeHint
 *           均不清——同 applyRegGeometry 实测口径），两遍后列数必然
 *           稳定（列数只随视口宽），高度精确。 */
static void mb_slaveFitScrollContent(XScrollArea* area)
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
    if (vw < 1) return;   /* 视口未就位（堆叠未给几何）不猜。 */
    XWidget_setFixedWidth(content, vw);
    for (pass = 0; pass < 2; ++pass) {
        XSize hint;
        XSize minHint;
        if (XWidget_layout(content))
            XLayout_invalidate(XWidget_layout(content));
        /* 有布局 ⇒ 布局合计首选/合计最小。[合计首选缺口 2026-10-18]
         * 无显式 sizeHint 的**定尺寸**条目（满高寄存器表）在合计首选里
         * 记 0（XLayoutItem.c:169-174 回退链 sizeHint<0→minSizeHint<0→0
         * 再被 max 钳位），而合计最小把 min=max 的定尺寸条目按实高累计
         * ——两者取大才是「所有条目都不被裁」的内容高。 */
        hint = XWidget_sizeHint(content);
        minHint = XWidget_minimumSizeHint(content);
        if (minHint.height > hint.height) hint.height = minHint.height;
        if (minHint.width > hint.width) hint.width = minHint.width;
        if (hint.width < 1) hint.width = vw;
        if (hint.height < 1) hint.height = vh > 0 ? vh : 1;
        XWidget_setFixedHeight(content, hint.height);
        /* setGeometry 内部先 activate 布局再派发 RESIZE（XWidget.c:
         * 4703-4715），内容根 resizeEvent 随之跑 syncViewport。 */
        XWidget_setGeometry(content, 0, 0, hint.width, hint.height);
        if (XWidget_layout(content))
            XLayout_activate(XWidget_layout(content));
    }
    /* 内容尺寸驱动滚动范围（同 XScrollArea.c:62-64 口径；宽=视口宽 ⇒
     * 水平恒无滚动需求，hPolicy=AlwaysOff 再加一道保险）。 */
    XAbstractScrollArea_setContentSize((XAbstractScrollArea*)area, vw,
        XWidget_height(content) > vh ? XWidget_height(content) : vh);
    /* 页回调（可空）：主站在此 activate 内容布局+解算显示行，随后由
     * resizeEvent 补第二遍 fit 收敛行数变化的实测高（同从站两遍定式）。 */
    {
        MbSlaveScroll* shell = (MbSlaveScroll*)area;
        if (shell && shell->onFit) shell->onFit(shell);
    }
}

static void VMbSlaveScroll_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self) return;
    /* 先跑基类（视口/滚动条/内容件随新尺寸重排），再按新视口宽重定
     * 内容宽高；onFit 中解算行数的页（主站）随后补第二遍 fit 收敛
     * 实测高。 */
    XClass_Parent(XScrollArea, EXWidget_ResizeEvent,
                  XWidgetEventSlot)(self, event);
    mb_slaveFitScrollContent((XScrollArea*)self);
    {
        MbSlaveScroll* shell = (MbSlaveScroll*)self;
        if (shell->onFit) {
            shell->onFit(shell);
            mb_slaveFitScrollContent((XScrollArea*)self);
        }
    }
}

XVtable* MbSlaveScroll_class_init(void)
{
    XVTABLE_INIT_DEFAULT(MbSlaveScroll)
    XVTABLE_INHERIT_XCLASS(XScrollArea);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent,
                             VMbSlaveScroll_resizeEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 设置滚动壳 fit 收敛后的页回调（主站接入用；内部完整类型）。 */
static void mb_slaveScrollSetOnFit(XWidget* scroll,
                                   void (*onFit)(MbSlaveScroll* scroll))
{
    if (!scroll) return;
    ((MbSlaveScroll*)scroll)->onFit = onFit;
}

/** @brief 构建整页竖滚区（带内容重承载钩子的 XScrollArea 子类实例）。 */
static XWidget* mb_slaveCreateScroll(XWidget* parent)
{
    MbSlaveScroll* scroll =
        (MbSlaveScroll*)XMalloc_System(sizeof(MbSlaveScroll));
    if (!scroll) return (XWidget*)0;
    XMemset(scroll, 0, sizeof(*scroll));
    XScrollArea_init(&scroll->m_base, parent, 0);
    XClassSetVtable(scroll, MbSlaveScroll);
    return (XWidget*)scroll;
}

XWidget* demo_page_modbus_slave_build(XWidget* parent,
                                      DemoPageStatusFn statusFn, void* user)
{
    XLabel* title = NULL;
    (void)statusFn; (void)user;

    XMemset(&s_mbSlave, 0, sizeof(s_mbSlave));
    s_mbSlave.inCount = 8;
    s_mbSlave.outCount = 8;
    /* [10-18 三次追加] 寄存器默认行数 16（SpinBox 1..256 可调）。 */
    s_mbSlave.irCount = MB_SLAVE_REG_ROWS;
    s_mbSlave.hrCount = MB_SLAVE_REG_ROWS;

    if (!parent) return (XWidget*)0;
    {
        /* [10-18 二次追加] 页堆叠条目 = scroll（整页竖滚区，见
         * MbSlaveScroll 注释）；内容根（root/pageRoot）作 scroll 内容件，
         * 页内排布由其 XBoxLayout 分配（build 一次装配，resize→activate
         * 自动重排 + 重解算档位）。 */
        XWidget* scrollRoot = mb_slaveCreateScroll(parent);
        XWidget* pageRoot;
        if (!scrollRoot) return (XWidget*)0;
        pageRoot = mb_slaveCreateRoot(scrollRoot);
        if (!pageRoot) {
            XObject_deleteLater(scrollRoot);
            return (XWidget*)0;
        }
        s_mbSlave.scroll = (XScrollArea*)scrollRoot;
        s_mbSlave.pageRoot = pageRoot;
        s_mbSlave.root = pageRoot;
    }

    /* 内容根纵向主布局：边距 12/8/12/8，间距 4；条目 3 = dataArea。
     * [10-18 二次追加] 内容高=布局首选高（fitScrollContent 依此定高），
     * 无 stretch 条目——全部段高确定（行高 24 + 满高表），超高由竖滚。 */
    XBoxLayout_init(&s_mbSlave.layRoot, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&s_mbSlave.layRoot, 12, 8, 12, 8);
    XLayout_setSpacing((XLayout*)&s_mbSlave.layRoot, 4);

    /* 条目 0：标题行。 */
    mb_buildLabel(s_mbSlave.root,
                  "Modbus \xE4\xBB\x8E\xE7\xAB\x99\xE8\xB0\x83\xE8\xAF\x95",
                  300, &title); /* Modbus 从站调试 */
    if (title) {
        XLabel_setTextPixelSize(title, 16);
        XBoxLayout_addWidget(&s_mbSlave.layRoot, (XWidget*)title);
    }

    /* 条目 1：参数合并行（[10-18 用户追加规格④] 原通道/监听端口/串口/
     * 本站地址四行并一行——右边太空）。随通道显隐的控件条目在隐藏时
     * 主轴分配取 0 宽（XLayoutItem.c:229 isEmpty ⇒ XBoxLayout.c:461
     * size=0），TCP/串口切换行内自折叠，不占位。 */
    mb_rowInit(&s_mbSlave.layChannel, &s_mbSlave.layRoot,
               XBoxLayoutDirection_LeftToRight);
    {
        XLabel* lbl = NULL;
        mb_buildLabel(s_mbSlave.root, "\xE9\x80\x9A\xE9\x81\x93", 56, &lbl);
                        /* 通道 */
        if (lbl) XBoxLayout_addWidget(&s_mbSlave.layChannel, (XWidget*)lbl);
    }
    s_mbSlave.channelCombo = mb_buildChannelCombo(s_mbSlave.root, 120);
    if (s_mbSlave.channelCombo)
        XBoxLayout_addWidget(&s_mbSlave.layChannel,
                             (XWidget*)s_mbSlave.channelCombo);
    XBoxLayout_addSpacing(&s_mbSlave.layChannel, 12);
    mb_buildLabel(s_mbSlave.root,
                  "\xE7\x9B\x91\xE5\x90\xAC\xE7\xAB\xAF\xE5\x8F\xA3",
                  84, &s_mbSlave.tcpLbl); /* 监听端口 */
    if (s_mbSlave.tcpLbl)
        XBoxLayout_addWidget(&s_mbSlave.layChannel,
                             (XWidget*)s_mbSlave.tcpLbl);
    s_mbSlave.tcpPortEdit = XLineEdit_create(s_mbSlave.root, 0);
    if (s_mbSlave.tcpPortEdit) {
        XLineEdit_setText(s_mbSlave.tcpPortEdit, "15020");
        XWidget_setFixedWidth((XWidget*)s_mbSlave.tcpPortEdit, 100);
        XWidget_setFixedHeight((XWidget*)s_mbSlave.tcpPortEdit, 24);
        XWidget_show((XWidget*)s_mbSlave.tcpPortEdit);
        XBoxLayout_addWidget(&s_mbSlave.layChannel,
                             (XWidget*)s_mbSlave.tcpPortEdit);
    }
    mb_buildLabel(s_mbSlave.root, "\xE4\xB8\xB2\xE5\x8F\xA3", 56,
                  &s_mbSlave.serialLbl); /* 串口 */
    if (s_mbSlave.serialLbl) {
        XWidget_setVisible((XWidget*)s_mbSlave.serialLbl, false);
        XBoxLayout_addWidget(&s_mbSlave.layChannel,
                             (XWidget*)s_mbSlave.serialLbl);
    }
    /* 串口名下拉：每次弹出前经 XSerialPortInfo 重枚举（见 MbSerialCombo）。 */
    s_mbSlave.serialNameCombo = mb_buildSerialCombo(s_mbSlave.root, 130);
    if (s_mbSlave.serialNameCombo) {
        XWidget_setVisible((XWidget*)s_mbSlave.serialNameCombo, false);
        XBoxLayout_addWidget(&s_mbSlave.layChannel,
                             (XWidget*)s_mbSlave.serialNameCombo);
    }
    s_mbSlave.serialBaudCombo = mb_buildBaudCombo(s_mbSlave.root, 80);
    if (s_mbSlave.serialBaudCombo) {
        XWidget_setVisible((XWidget*)s_mbSlave.serialBaudCombo, false);
        XBoxLayout_addWidget(&s_mbSlave.layChannel,
                             (XWidget*)s_mbSlave.serialBaudCombo);
    }
    XBoxLayout_addSpacing(&s_mbSlave.layChannel, 12);
    {
        XLabel* lbl = NULL;
        mb_buildLabel(s_mbSlave.root,
                      "\xE6\x9C\xAC\xE7\xAB\x99\xE5\x9C\xB0\xE5\x9D\x80",
                      80, &lbl); /* 本站地址 */
        if (lbl) XBoxLayout_addWidget(&s_mbSlave.layChannel, (XWidget*)lbl);
    }
    s_mbSlave.unitSpin = mb_buildSpin(s_mbSlave.root, 60, 1, 247, 1);
    if (s_mbSlave.unitSpin)
        XBoxLayout_addWidget(&s_mbSlave.layChannel,
                             (XWidget*)s_mbSlave.unitSpin);
    mb_rowPack(&s_mbSlave.layChannel);
    XObject_connect_2((XObject*)s_mbSlave.channelCombo,
                      XSignal(XComboBox_activated_signal),
                      mb_slaveOnChannelChanged);

    /* 条目 2：启动/状态/统计行（按钮 28、标签 22；页根上部不动）。 */
    mb_rowInit(&s_mbSlave.layStart, &s_mbSlave.layRoot,
               XBoxLayoutDirection_LeftToRight);
    s_mbSlave.startBtn = XPushButton_create(s_mbSlave.root, 0);
    if (s_mbSlave.startBtn) {
        XPushButton_setText_2(s_mbSlave.startBtn,
                              "\xE5\x90\xAF\xE5\x8A\xA8"); /* 启动 */
        XWidget_setFixedWidth((XWidget*)s_mbSlave.startBtn, 90);
        XWidget_setFixedHeight((XWidget*)s_mbSlave.startBtn, 28);
        XObject_connect_2((XObject*)s_mbSlave.startBtn,
                          XSignal(XAbstractButton_clicked_signal),
                          mb_slaveStartSlot);
        XWidget_show((XWidget*)s_mbSlave.startBtn);
        XBoxLayout_addWidget(&s_mbSlave.layStart,
                             (XWidget*)s_mbSlave.startBtn);
    }
    XBoxLayout_addSpacing(&s_mbSlave.layStart, 10);
    s_mbSlave.stateLabel = XLabel_create(s_mbSlave.root, 0);
    if (s_mbSlave.stateLabel) {
        XLabel_setText_2(s_mbSlave.stateLabel,
                         "\xE5\xB7\xB2\xE5\x81\x9C\xE6\xAD\xA2"); /* 已停止 */
        XWidget_setFixedWidth((XWidget*)s_mbSlave.stateLabel, 120);
        XWidget_setFixedHeight((XWidget*)s_mbSlave.stateLabel, 22);
        XWidget_show((XWidget*)s_mbSlave.stateLabel);
        XBoxLayout_addWidget(&s_mbSlave.layStart,
                             (XWidget*)s_mbSlave.stateLabel);
    }
    XBoxLayout_addSpacing(&s_mbSlave.layStart, 10);
    s_mbSlave.statLabel = XLabel_create(s_mbSlave.root, 0);
    if (s_mbSlave.statLabel) {
        XLabel_setText_2(s_mbSlave.statLabel,
                         "\xE5\x86\x99\xE4\xBA\x8B\xE4\xBB\xB6: 0");
                         /* 写事件: 0 */
        XWidget_setFixedWidth((XWidget*)s_mbSlave.statLabel, 150);
        XWidget_setFixedHeight((XWidget*)s_mbSlave.statLabel, 22);
        XWidget_show((XWidget*)s_mbSlave.statLabel);
        XBoxLayout_addWidget(&s_mbSlave.layStart,
                             (XWidget*)s_mbSlave.statLabel);
    }
    mb_rowPack(&s_mbSlave.layStart);

    /* ---- 条目 3：数据区（全宽纵向四段式）----
     * dataArea 走「控件 + setLayout」路径；layData 恒纵向，八条目平铺：
     * DI 标题行/DI 灯格宿主/DO 标题行/DO 灯格宿主/IR 标题行/IR 表/
     * HR 标题行/HR 表。四条标题行都是未绑控件的子布局（addLayout 合法）；
     * 宿主与两表是控件条目，不再
     * 套「列容器」中间层（每层只有「控件+自己的布局」一跳，规避
     * activate 不级联与内层坐标系错位的实测坑，见 applyRegGeometry
     * 注释）。[10-18 二次追加] 无 stretch 条目、无滚动壳：段高全部
     * 确定（标题行 24 + 宿主 行数×22 + 满高表），超高由整页竖滚承担。 */
    s_mbSlave.dataArea = XWidget_create(s_mbSlave.root, 0);
    if (s_mbSlave.dataArea) {
        XBoxLayout_init(&s_mbSlave.layData, XBoxLayoutDirection_TopToBottom);
        XLayout_setSpacing((XLayout*)&s_mbSlave.layData, 4);
        XWidget_setLayout(s_mbSlave.dataArea, (XLayout*)&s_mbSlave.layData);
        XWidget_show(s_mbSlave.dataArea);
        XBoxLayout_addWidget(&s_mbSlave.layRoot, s_mbSlave.dataArea);

        /* ---- 段① DI：标题行（标题+点数 SpinBox+按点数重建(输入)
         * +DI全开/DI全关）---- 行内控件创建时父即 dataArea（C3：行布局
         * 控件条目不 reparent，挂接时 linkItem 见父子一致则不动）。 */
        mb_rowInit(&s_mbSlave.layDiTitle, &s_mbSlave.layData,
                   XBoxLayoutDirection_LeftToRight);
        {
            XLabel* lbl = NULL;
            mb_buildLabel(s_mbSlave.dataArea,
                          "\xE8\xBE\x93\xE5\x85\xA5\xE7\x82\xB9 DI"
                          "(\xE7\x82\xB9\xE5\x87\xBB\xE7\xBD\xAE\xE4\xB8\x8D)",
                          180, &lbl); /* 输入点 DI(点击置位) */
            if (lbl)
                XBoxLayout_addWidget(&s_mbSlave.layDiTitle,
                                     (XWidget*)lbl);
        }
        /* [联调 2026-10-19] env XGUI_DEMO_MB_SLAVE_IN/OUT 预置从站点数
         * （回环联调复现 16 点混合态用；缺省仍 8，同主站 IN/OUT 先例）。 */
        {
            const char* env = XSystem_environment("XGUI_DEMO_MB_SLAVE_IN");
            int v = (env && env[0]) ? atoi(env) : 8;
            if (v < 0 || v > 64) v = 8;
            s_mbSlave.inCountSpin = mb_buildSpin(s_mbSlave.dataArea, 60,
                                                 0, 64, v);
        }
        if (s_mbSlave.inCountSpin)
            XBoxLayout_addWidget(&s_mbSlave.layDiTitle,
                                 (XWidget*)s_mbSlave.inCountSpin);
        /* [10-18 用户追加规格⑤] 按点数重建(输入)：紧挨点数 SpinBox，
         * 与段②按钮分立双槽（sender 不可信契约，见 applyCounts 注释）。 */
        s_mbSlave.applyDiBtn = XPushButton_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.applyDiBtn) {
            XPushButton_setText_2(s_mbSlave.applyDiBtn,
                                  "\xE6\x8C\x89\xE7\x82\xB9\xE6\x95\xB0\xE9\x87"
                                  "\x8D\xE5\xBB\xBA"); /* 按点数重建 */
            XWidget_setFixedWidth((XWidget*)s_mbSlave.applyDiBtn, 110);
            XWidget_setFixedHeight((XWidget*)s_mbSlave.applyDiBtn, 26);
            XObject_connect_2((XObject*)s_mbSlave.applyDiBtn,
                              XSignal(XAbstractButton_clicked_signal),
                              mb_slaveApplyDiSlot);
            XWidget_show((XWidget*)s_mbSlave.applyDiBtn);
            XBoxLayout_addWidget(&s_mbSlave.layDiTitle,
                                 (XWidget*)s_mbSlave.applyDiBtn);
        }
        s_mbSlave.diAllOnBtn = XPushButton_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.diAllOnBtn) {
            XPushButton_setText_2(s_mbSlave.diAllOnBtn,
                                  "DI\xE5\x85\xA8\xE5\xBC\x80"); /* DI全开 */
            XWidget_setFixedWidth((XWidget*)s_mbSlave.diAllOnBtn, 72);
            XWidget_setFixedHeight((XWidget*)s_mbSlave.diAllOnBtn, 24);
            XObject_connect_2((XObject*)s_mbSlave.diAllOnBtn,
                              XSignal(XAbstractButton_clicked_signal),
                              mb_slaveDiAllOnSlot);
            XWidget_show((XWidget*)s_mbSlave.diAllOnBtn);
            XBoxLayout_addWidget(&s_mbSlave.layDiTitle,
                                 (XWidget*)s_mbSlave.diAllOnBtn);
        }
        s_mbSlave.diAllOffBtn = XPushButton_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.diAllOffBtn) {
            XPushButton_setText_2(s_mbSlave.diAllOffBtn,
                                  "DI\xE5\x85\xA8\xE5\x85\xB3"); /* DI全关 */
            XWidget_setFixedWidth((XWidget*)s_mbSlave.diAllOffBtn, 72);
            XWidget_setFixedHeight((XWidget*)s_mbSlave.diAllOffBtn, 24);
            XObject_connect_2((XObject*)s_mbSlave.diAllOffBtn,
                              XSignal(XAbstractButton_clicked_signal),
                              mb_slaveDiAllOffSlot);
            XWidget_show((XWidget*)s_mbSlave.diAllOffBtn);
            XBoxLayout_addWidget(&s_mbSlave.layDiTitle,
                                 (XWidget*)s_mbSlave.diAllOffBtn);
        }
        mb_rowPack(&s_mbSlave.layDiTitle);

        /* 段① 第二行：DI 灯格宿主（[10-18 二次追加·滚动条纪律] 滚动壳
         * 删除——圆圈换行铺排放得下整宽，宿主尺寸由 rebuild 按列数/
         * 行数重算；此处先给单行初始定尺防首拍度量回落 0（C7）。 */
        s_mbSlave.diGridHost = XWidget_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.diGridHost) {
            XSize hint;
            XWidget_setFixedWidth(s_mbSlave.diGridHost, 8 * MB_CELL_W);
            XWidget_setFixedHeight(s_mbSlave.diGridHost, MB_CELL_H);
            XSize_init(&hint, 8 * MB_CELL_W, MB_CELL_H);
            XWidget_setSizeHint(s_mbSlave.diGridHost, &hint);
            XWidget_show(s_mbSlave.diGridHost);
            XBoxLayout_addWidget(&s_mbSlave.layData, s_mbSlave.diGridHost);
        }

        /* ---- 段② DO：标题行（标题+点数 SpinBox+按点数重建(输出)
         * +全开/全关）---- */
        mb_rowInit(&s_mbSlave.layDoTitle, &s_mbSlave.layData,
                   XBoxLayoutDirection_LeftToRight);
        {
            XLabel* lbl = NULL;
            mb_buildLabel(s_mbSlave.dataArea,
                          "\xE8\xBE\x93\xE5\x87\xBA\xE7\x82\xB9 DO"
                          "(\xE7\x82\xB9\xE5\x87\xBB\xE7\xBF\xBB\xE8\xBD\xAC)",
                          180, &lbl); /* 输出点 DO(点击翻转) */
            if (lbl)
                XBoxLayout_addWidget(&s_mbSlave.layDoTitle,
                                     (XWidget*)lbl);
        }
        /* [联调 2026-10-19] env XGUI_DEMO_MB_SLAVE_OUT 同上（输出点数）。 */
        {
            const char* env = XSystem_environment("XGUI_DEMO_MB_SLAVE_OUT");
            int v = (env && env[0]) ? atoi(env) : 8;
            if (v < 0 || v > 64) v = 8;
            s_mbSlave.outCountSpin = mb_buildSpin(s_mbSlave.dataArea, 60,
                                                  0, 64, v);
        }
        if (s_mbSlave.outCountSpin)
            XBoxLayout_addWidget(&s_mbSlave.layDoTitle,
                                 (XWidget*)s_mbSlave.outCountSpin);
        /* [10-18 用户追加规格⑤] 按点数重建(输出)：与段①按钮分立双槽。 */
        s_mbSlave.applyDoBtn = XPushButton_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.applyDoBtn) {
            XPushButton_setText_2(s_mbSlave.applyDoBtn,
                                  "\xE6\x8C\x89\xE7\x82\xB9\xE6\x95\xB0\xE9\x87"
                                  "\x8D\xE5\xBB\xBA"); /* 按点数重建 */
            XWidget_setFixedWidth((XWidget*)s_mbSlave.applyDoBtn, 110);
            XWidget_setFixedHeight((XWidget*)s_mbSlave.applyDoBtn, 26);
            XObject_connect_2((XObject*)s_mbSlave.applyDoBtn,
                              XSignal(XAbstractButton_clicked_signal),
                              mb_slaveApplyDoSlot);
            XWidget_show((XWidget*)s_mbSlave.applyDoBtn);
            XBoxLayout_addWidget(&s_mbSlave.layDoTitle,
                                 (XWidget*)s_mbSlave.applyDoBtn);
        }
        s_mbSlave.allOnBtn = XPushButton_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.allOnBtn) {
            XPushButton_setText_2(s_mbSlave.allOnBtn,
                                  "\xE5\x85\xA8\xE5\xBC\x80"); /* 全开 */
            XWidget_setFixedWidth((XWidget*)s_mbSlave.allOnBtn, 64);
            XWidget_setFixedHeight((XWidget*)s_mbSlave.allOnBtn, 24);
            XObject_connect_2((XObject*)s_mbSlave.allOnBtn,
                              XSignal(XAbstractButton_clicked_signal),
                              mb_slaveAllOnSlot);
            XWidget_show((XWidget*)s_mbSlave.allOnBtn);
            XBoxLayout_addWidget(&s_mbSlave.layDoTitle,
                                 (XWidget*)s_mbSlave.allOnBtn);
        }
        s_mbSlave.allOffBtn = XPushButton_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.allOffBtn) {
            XPushButton_setText_2(s_mbSlave.allOffBtn,
                                  "\xE5\x85\xA8\xE5\x85\xB3"); /* 全关 */
            XWidget_setFixedWidth((XWidget*)s_mbSlave.allOffBtn, 64);
            XWidget_setFixedHeight((XWidget*)s_mbSlave.allOffBtn, 24);
            XObject_connect_2((XObject*)s_mbSlave.allOffBtn,
                              XSignal(XAbstractButton_clicked_signal),
                              mb_slaveAllOffSlot);
            XWidget_show((XWidget*)s_mbSlave.allOffBtn);
            XBoxLayout_addWidget(&s_mbSlave.layDoTitle,
                                 (XWidget*)s_mbSlave.allOffBtn);
        }
        mb_rowPack(&s_mbSlave.layDoTitle);

        /* 段② 第二行：DO 灯格宿主（同段①口径：无滚动壳，换行铺排）。 */
        s_mbSlave.doGridHost = XWidget_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.doGridHost) {
            XSize hint;
            XWidget_setFixedWidth(s_mbSlave.doGridHost, 8 * MB_CELL_W);
            XWidget_setFixedHeight(s_mbSlave.doGridHost, MB_CELL_H);
            XSize_init(&hint, 8 * MB_CELL_W, MB_CELL_H);
            XWidget_setSizeHint(s_mbSlave.doGridHost, &hint);
            XWidget_show(s_mbSlave.doGridHost);
            XBoxLayout_addWidget(&s_mbSlave.layData, s_mbSlave.doGridHost);
        }

        /* ---- 段③ 寄存器区：IR/HR 上下堆叠平铺进 layData（选择理由见
         * applyRegGeometry 注释：并排须水平中间容器+两层嵌套，且本框架
         * 表格在盒式布局条目不贡献可分配宽；上下堆叠表宽直接=整宽，
         * 套路同构、实现最简）。[10-18 二次追加] 无 stretch 条目：表
         * 满高全展开，几何由 syncViewport 每拍定尺重解算驱动，垂直超出
         * 由整页竖滚承担。[10-18 三次追加] 每表配段标题行（「输入寄存器
         * IR」/「保持寄存器 HR」+数量 SpinBox 1..256）——按线圈段同款
         * 「未绑控件子布局」定式（layDiTitle/layDoTitle 已验证同款可行，
         * 行内控件创建时父即 dataArea，C3）。---- */
        mb_rowInit(&s_mbSlave.layIrTitle, &s_mbSlave.layData,
                   XBoxLayoutDirection_LeftToRight);
        mb_buildLabel(s_mbSlave.dataArea,
                      "\xE8\xBE\x93\xE5\x85\xA5\xE5\xAF\x84\xE5\xAD\x98\xE5"
                      "\x99\xA8 IR", 150,
                      &s_mbSlave.irTitleLbl); /* 输入寄存器 IR */
        if (s_mbSlave.irTitleLbl)
            XBoxLayout_addWidget(&s_mbSlave.layIrTitle,
                                 (XWidget*)s_mbSlave.irTitleLbl);
        s_mbSlave.irCountSpin = mb_buildSpin(s_mbSlave.dataArea, 64, 1,
                                             MB_SLAVE_REG_MAX_ROWS,
                                             MB_SLAVE_REG_ROWS);
        if (s_mbSlave.irCountSpin) {
            XBoxLayout_addWidget(&s_mbSlave.layIrTitle,
                                 (XWidget*)s_mbSlave.irCountSpin);
            XObject_connect_1((XObject*)s_mbSlave.irCountSpin,
                              (size_t)XSpinBox_valueChanged_signal(
                                  (XSpinBox*)NULL),
                              (XObject*)s_mbSlave.root,
                              mb_slaveIrCountSlot,
                              XConnectionType_Direct);
        }
        /* [10-19 用户反馈] 寄存器区补「按点数重建」：与数量 SpinBox 同槽
         * （mb_slaveApplyRegCounts：setValueCount 保值+扩缩容挂件同步），
         * SpinBox 即时生效，按钮=显式触发点（数量框改动遗漏时的兜底）。 */
        s_mbSlave.applyIrBtn = XPushButton_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.applyIrBtn) {
            XPushButton_setText_2(s_mbSlave.applyIrBtn,
                                  "\xE6\x8C\x89\xE7\x82\xB9\xE6\x95\xB0\xE9\x87"
                                  "\x8D\xE5\xBB\xBA"); /* 按点数重建 */
            XWidget_setFixedWidth((XWidget*)s_mbSlave.applyIrBtn, 110);
            XWidget_setFixedHeight((XWidget*)s_mbSlave.applyIrBtn, 26);
            XObject_connect_2((XObject*)s_mbSlave.applyIrBtn,
                              XSignal(XAbstractButton_clicked_signal),
                              mb_slaveIrCountSlot);
            XWidget_show((XWidget*)s_mbSlave.applyIrBtn);
            XBoxLayout_addWidget(&s_mbSlave.layIrTitle,
                                 (XWidget*)s_mbSlave.applyIrBtn);
        }
        /* 值显示格式（2026-10-24）：默认 10 进制（沿用从站原行为）。 */
        s_mbSlave.irFmtCombo = XComboBox_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.irFmtCombo) {
            XComboBox_addItem_2(s_mbSlave.irFmtCombo,
                    "16\xE8\xBF\x9B\xE5\x88\xB6"); /* 16进制 */
            XComboBox_addItem_2(s_mbSlave.irFmtCombo,
                    "10\xE8\xBF\x9B\xE5\x88\xB6"); /* 10进制 */
            XComboBox_setCurrentIndex(s_mbSlave.irFmtCombo, 1);
            XWidget_setFixedWidth((XWidget*)s_mbSlave.irFmtCombo, 76);
            XWidget_setFixedHeight((XWidget*)s_mbSlave.irFmtCombo, 24);
            XObject_connect_2((XObject*)s_mbSlave.irFmtCombo,
                              XSignal(XComboBox_activated_signal),
                              mb_slaveIrFmtSlot);
            XWidget_show((XWidget*)s_mbSlave.irFmtCombo);
            XBoxLayout_addWidget(&s_mbSlave.layIrTitle,
                                 (XWidget*)s_mbSlave.irFmtCombo);
        }
        s_mbSlave.irHex = 0;
        mb_rowPack(&s_mbSlave.layIrTitle);
        mb_slaveBuildRegTable(s_mbSlave.dataArea, &s_mbSlave.irTable,
                              s_mbSlave.irCount, s_mbSlave.root,
                              s_mbSlave.irHex, mb_slaveIrCellChanged);
        if (s_mbSlave.irTable)
            XBoxLayout_addWidget(&s_mbSlave.layData,
                                 (XWidget*)s_mbSlave.irTable);
        mb_rowInit(&s_mbSlave.layHrTitle, &s_mbSlave.layData,
                   XBoxLayoutDirection_LeftToRight);
        mb_buildLabel(s_mbSlave.dataArea,
                      "\xE4\xBF\x9D\xE6\x8C\x81\xE5\xAF\x84\xE5\xAD\x98\xE5"
                      "\x99\xA8 HR", 150,
                      &s_mbSlave.hrTitleLbl); /* 保持寄存器 HR */
        if (s_mbSlave.hrTitleLbl)
            XBoxLayout_addWidget(&s_mbSlave.layHrTitle,
                                 (XWidget*)s_mbSlave.hrTitleLbl);
        s_mbSlave.hrCountSpin = mb_buildSpin(s_mbSlave.dataArea, 64, 1,
                                             MB_SLAVE_REG_MAX_ROWS,
                                             MB_SLAVE_REG_ROWS);
        if (s_mbSlave.hrCountSpin) {
            XBoxLayout_addWidget(&s_mbSlave.layHrTitle,
                                 (XWidget*)s_mbSlave.hrCountSpin);
            XObject_connect_1((XObject*)s_mbSlave.hrCountSpin,
                              (size_t)XSpinBox_valueChanged_signal(
                                  (XSpinBox*)NULL),
                              (XObject*)s_mbSlave.root,
                              mb_slaveHrCountSlot,
                              XConnectionType_Direct);
        }
        /* [10-19 用户反馈] HR 段同款「按点数重建」。 */
        s_mbSlave.applyHrBtn = XPushButton_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.applyHrBtn) {
            XPushButton_setText_2(s_mbSlave.applyHrBtn,
                                  "\xE6\x8C\x89\xE7\x82\xB9\xE6\x95\xB0\xE9\x87"
                                  "\x8D\xE5\xBB\xBA"); /* 按点数重建 */
            XWidget_setFixedWidth((XWidget*)s_mbSlave.applyHrBtn, 110);
            XWidget_setFixedHeight((XWidget*)s_mbSlave.applyHrBtn, 26);
            XObject_connect_2((XObject*)s_mbSlave.applyHrBtn,
                              XSignal(XAbstractButton_clicked_signal),
                              mb_slaveHrCountSlot);
            XWidget_show((XWidget*)s_mbSlave.applyHrBtn);
            XBoxLayout_addWidget(&s_mbSlave.layHrTitle,
                                 (XWidget*)s_mbSlave.applyHrBtn);
        }
        s_mbSlave.hrFmtCombo = XComboBox_create(s_mbSlave.dataArea, 0);
        if (s_mbSlave.hrFmtCombo) {
            XComboBox_addItem_2(s_mbSlave.hrFmtCombo,
                    "16\xE8\xBF\x9B\xE5\x88\xB6"); /* 16进制 */
            XComboBox_addItem_2(s_mbSlave.hrFmtCombo,
                    "10\xE8\xBF\x9B\xE5\x88\xB6"); /* 10进制 */
            XComboBox_setCurrentIndex(s_mbSlave.hrFmtCombo, 1);
            XWidget_setFixedWidth((XWidget*)s_mbSlave.hrFmtCombo, 76);
            XWidget_setFixedHeight((XWidget*)s_mbSlave.hrFmtCombo, 24);
            XObject_connect_2((XObject*)s_mbSlave.hrFmtCombo,
                              XSignal(XComboBox_activated_signal),
                              mb_slaveHrFmtSlot);
            XWidget_show((XWidget*)s_mbSlave.hrFmtCombo);
            XBoxLayout_addWidget(&s_mbSlave.layHrTitle,
                                 (XWidget*)s_mbSlave.hrFmtCombo);
        }
        s_mbSlave.hrHex = 0;
        mb_rowPack(&s_mbSlave.layHrTitle);
        mb_slaveBuildRegTable(s_mbSlave.dataArea, &s_mbSlave.hrTable,
                              s_mbSlave.hrCount, s_mbSlave.root,
                              s_mbSlave.hrHex, mb_slaveHrCellChanged);
        if (s_mbSlave.hrTable)
            XBoxLayout_addWidget(&s_mbSlave.layData,
                                 (XWidget*)s_mbSlave.hrTable);
    }

    /* 装配收口：内容根挂布局器；scroll 装内容件并定滚动纪律（水平条
     * AlwaysOff，竖条默认 AsNeeded=全页唯一滚动条）。此后堆叠布局重贴
     * scroll 几何即经 MbSlaveScroll resizeEvent→fitScrollContent→内容根
     * resize→activate 自动重排 + 重解算档位。 */
    XWidget_setLayout(s_mbSlave.pageRoot, (XLayout*)&s_mbSlave.layRoot);
    XAbstractScrollArea_setHorizontalScrollBarPolicy(
        (XAbstractScrollArea*)s_mbSlave.scroll, XScrollBarPolicy_AlwaysOff);
    XScrollArea_setWidget(s_mbSlave.scroll, s_mbSlave.root); /* 收编内容件。 */
    /* 常驻 server：建好即挂 map/接信号/刷参数 ⇒ 启动前就能单机点灯、
     * 改寄存器（写路径不依赖连接态）。 */
    mb_slaveEnsureServer();
#if !defined(_WIN32) && XMODBUS_TCP_ON
    /* [CLOSE-WAIT 根修 2026-10-19] 死链巡检定时器（1s，页根子对象级联
     * 析构；回调内自守卫：非 TCP 服务器即空转）。 */
    if (!s_mbSlave.reapTimer) {
        s_mbSlave.reapTimer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
        if (s_mbSlave.reapTimer) {
            XTimer_setInterval(s_mbSlave.reapTimer, 1000);
            XTimer_setTimerCallback(s_mbSlave.reapTimer,
                                    mb_slaveSweepDeadClientsCb);
            XTimer_setUserData(s_mbSlave.reapTimer, NULL);
            XObject_setParent((XObject*)s_mbSlave.reapTimer,
                              (XObject*)s_mbSlave.root);
            XTimer_start_base(s_mbSlave.reapTimer);
        }
    }
#endif
    /* 尾拍含整页竖滚首拍（syncViewport 收口调 fitScrollContent；视口
     * 未就位时自退避，show/resize 路径再收敛）。 */
    mb_slaveSyncViewport();

    /* [端到端联调入口] env XGUI_DEMO_MB_AUTOSTART=slave：构建后程序化
     * 启动监听（XAbstractButton_click 走完整激活流程；回环联调用）。 */
    {
        const char* autoRole = XSystem_environment("XGUI_DEMO_MB_AUTOSTART");
        if (autoRole && strcmp(autoRole, "slave") == 0 && s_mbSlave.startBtn) {
            XAbstractButton_click((XAbstractButton*)s_mbSlave.startBtn);
            XPrintf("XGuiWindowDemo: modbus autostart slave done\n");
        }
    }
    /* [10-18 二次追加] 页堆叠挂 scroll（整页竖滚区），内容件=内容根。 */
    return (XWidget*)s_mbSlave.scroll;
}

int demo_page_modbus_slave_autotest(XWidget* page)
{
    if (!page) return -1;
    {
        int failures = 0;
        if (!s_mbSlave.channelCombo || !s_mbSlave.startBtn)
            ++failures;
        /* [常驻 2026-10-18] 原 `if (s_mbSlave.server) ++failures;` 已不
         * 成立：build 末尾 ensureServer，server != NULL 是**期望**状态，
         * 须改验 listening（构建后未监听）。 */
        if (s_mbSlave.listening) ++failures;
        /* [布局容器化 2026-10-17] 同主站页：断言 isVisible+尺寸>0+父子
         * 关系（原坐标相交断言随布局容器化移除）。
         * [10-18 二次追加] page 入参=整页竖滚区（scroll），行内控件的
         * 父级是内容根 s_mbSlave.root（scroll 内容件）。 */
        if (!s_mbSlave.tcpLbl ||
            !XWidget_isVisible((XWidget*)s_mbSlave.tcpLbl))
            ++failures;
        if (!s_mbSlave.tcpPortEdit ||
            XWidget_width((XWidget*)s_mbSlave.tcpPortEdit) <= 0 ||
            XWidget_height((XWidget*)s_mbSlave.tcpPortEdit) <= 0 ||
            XWidget_parentWidget((XWidget*)s_mbSlave.tcpPortEdit) !=
                s_mbSlave.root)
            ++failures;
        /* ---- 四区存在（滚动壳 diScroll/doScroll 随 10-18 二次追加删除，
         *         字段已不存在——编译期锁死不复生）---- */
        if (!s_mbSlave.diGridHost || !s_mbSlave.doGridHost ||
            !s_mbSlave.irTable || !s_mbSlave.hrTable) ++failures;
        /* ---- 挂载链（确定性，与布局度量无关）----
         * [10-18 二次追加·整页竖滚] scroll 内容件=内容根；灯格宿主与
         * 两表直接平铺进 layData，父级= dataArea；参数合并行控件父级=
         * 内容根；「按点数重建」两枚直挂 dataArea。 */
        if (!s_mbSlave.scroll ||
            XScrollArea_widget(s_mbSlave.scroll) != s_mbSlave.root)
            ++failures;
        if (!s_mbSlave.dataArea ||
            XWidget_parentWidget((XWidget*)s_mbSlave.dataArea) !=
                s_mbSlave.root)
            ++failures;
        if (!s_mbSlave.diGridHost ||
            XWidget_parentWidget((XWidget*)s_mbSlave.diGridHost) !=
                s_mbSlave.dataArea) ++failures;
        if (!s_mbSlave.doGridHost ||
            XWidget_parentWidget((XWidget*)s_mbSlave.doGridHost) !=
                s_mbSlave.dataArea) ++failures;
        if (!s_mbSlave.irTable ||
            XWidget_parentWidget((XWidget*)s_mbSlave.irTable) !=
                s_mbSlave.dataArea) ++failures;
        if (!s_mbSlave.hrTable ||
            XWidget_parentWidget((XWidget*)s_mbSlave.hrTable) !=
                s_mbSlave.dataArea) ++failures;
        if (!s_mbSlave.inCountSpin ||
            XWidget_parentWidget((XWidget*)s_mbSlave.inCountSpin) !=
                s_mbSlave.dataArea) ++failures;
        if (!s_mbSlave.outCountSpin ||
            XWidget_parentWidget((XWidget*)s_mbSlave.outCountSpin) !=
                s_mbSlave.dataArea) ++failures;
        if (!s_mbSlave.applyDiBtn || !s_mbSlave.applyDoBtn ||
            XWidget_parentWidget((XWidget*)s_mbSlave.applyDiBtn) !=
                s_mbSlave.dataArea ||
            XWidget_parentWidget((XWidget*)s_mbSlave.applyDoBtn) !=
                s_mbSlave.dataArea) ++failures;
        if (!s_mbSlave.diAllOnBtn || !s_mbSlave.diAllOffBtn ||
            !s_mbSlave.allOnBtn || !s_mbSlave.allOffBtn) ++failures;
        /* ---- 滚动条纪律（[10-18 二次追加] 全页唯一滚动条=scroll 竖条：
         *         水平条必须 AlwaysOff；策略读的是存储值，与布局无关）---- */
        if (!s_mbSlave.scroll ||
            XAbstractScrollArea_horizontalScrollBarPolicy(
                (XAbstractScrollArea*)s_mbSlave.scroll) !=
                XScrollBarPolicy_AlwaysOff)
            ++failures;
        /* ---- 新布局视口快照：列数/点数解算已被首拍 syncViewport 落位
         *         （build 末尾必调）---- */
        if (s_mbSlave.gridCols < 1 || s_mbSlave.gridPts < 1) ++failures;
        /* ---- 换行灯格宿主尺寸公式（读 maximumSize：min=max 立即写入，
         *         与布局是否激活无关）----
         * [10-18 二次追加] 宽=列数×单元宽、高=ceil(点数/列数)×单元高
         * （放不下换行往下放一行，无滚动壳）。 */
        if (s_mbSlave.diGridHost && s_mbSlave.doGridHost) {
            int cols = s_mbSlave.gridCols > 0 ? s_mbSlave.gridCols : 1;
            int diRows = (s_mbSlave.inCount + cols - 1) / cols;
            int doRows = (s_mbSlave.outCount + cols - 1) / cols;
            if (diRows < 1) diRows = 1;
            if (doRows < 1) doRows = 1;
            if (XWidget_maximumSize((XWidget*)s_mbSlave.diGridHost).width !=
                    cols * MB_CELL_W ||
                XWidget_maximumSize((XWidget*)s_mbSlave.diGridHost).height !=
                    diRows * MB_CELL_H)
                ++failures;
            if (XWidget_maximumSize((XWidget*)s_mbSlave.doGridHost).width !=
                    cols * MB_CELL_W ||
                XWidget_maximumSize((XWidget*)s_mbSlave.doGridHost).height !=
                    doRows * MB_CELL_H)
                ++failures;
        } else {
            ++failures;
        }
        /* ---- 两表确已装配（rowCount/columnCount 判别力强且与布局度量
         *         无关；表本身不带定宽，故不读 maximumSize）
         * [10-18 三次追加] 行数断言改对 irCount/hrCount 副本（默认 16，
         * SpinBox 1..256 可调）。---- */
        if (s_mbSlave.irTable &&
            XTableWidget_rowCount(s_mbSlave.irTable) != s_mbSlave.irCount)
            ++failures;
        if (s_mbSlave.irTable && XTableWidget_columnCount(s_mbSlave.irTable) != 2)
            ++failures;
        if (s_mbSlave.hrTable &&
            XTableWidget_rowCount(s_mbSlave.hrTable) != s_mbSlave.hrCount)
            ++failures;
        if (s_mbSlave.hrTable && XTableWidget_columnCount(s_mbSlave.hrTable) != 2)
            ++failures;
        if (s_mbSlave.irTable == s_mbSlave.hrTable) ++failures; /* 两表独立 */
        /* ---- [10-18 三次追加] 段标题行：标签+数量 SpinBox 就位、范围
         *         1..256、默认 16；四段纵向次序不倒（DI/DO 宿主 → IR 标题
         *         → IR 表 → HR 标题 → HR 表；同在 dataArea 坐标系可直接
         *         比 y——兼锁旧「标题与表对调」缺陷不复生）。---- */
        if (!s_mbSlave.irTitleLbl || !s_mbSlave.hrTitleLbl ||
            !s_mbSlave.irCountSpin || !s_mbSlave.hrCountSpin)
            ++failures;
        if (s_mbSlave.irTitleLbl &&
            XWidget_parentWidget((XWidget*)s_mbSlave.irTitleLbl) !=
                s_mbSlave.dataArea) ++failures;
        if (s_mbSlave.hrTitleLbl &&
            XWidget_parentWidget((XWidget*)s_mbSlave.hrTitleLbl) !=
                s_mbSlave.dataArea) ++failures;
        if (s_mbSlave.irCountSpin &&
            XWidget_parentWidget((XWidget*)s_mbSlave.irCountSpin) !=
                s_mbSlave.dataArea) ++failures;
        if (s_mbSlave.hrCountSpin &&
            XWidget_parentWidget((XWidget*)s_mbSlave.hrCountSpin) !=
                s_mbSlave.dataArea) ++failures;
        if (s_mbSlave.irCountSpin &&
            (XSpinBox_minimum(s_mbSlave.irCountSpin) != 1 ||
             XSpinBox_maximum(s_mbSlave.irCountSpin) !=
                 MB_SLAVE_REG_MAX_ROWS ||
             XSpinBox_value(s_mbSlave.irCountSpin) != MB_SLAVE_REG_ROWS))
            ++failures;
        if (s_mbSlave.hrCountSpin &&
            (XSpinBox_minimum(s_mbSlave.hrCountSpin) != 1 ||
             XSpinBox_maximum(s_mbSlave.hrCountSpin) !=
                 MB_SLAVE_REG_MAX_ROWS ||
             XSpinBox_value(s_mbSlave.hrCountSpin) != MB_SLAVE_REG_ROWS))
            ++failures;
        if (s_mbSlave.doGridHost && s_mbSlave.irTitleLbl &&
            s_mbSlave.irTable && s_mbSlave.hrTitleLbl && s_mbSlave.hrTable) {
            int yDo = XWidget_y((XWidget*)s_mbSlave.doGridHost);
            int yIrT = XWidget_y((XWidget*)s_mbSlave.irTitleLbl);
            int yIr = XWidget_y((XWidget*)s_mbSlave.irTable);
            int yHrT = XWidget_y((XWidget*)s_mbSlave.hrTitleLbl);
            int yHr = XWidget_y((XWidget*)s_mbSlave.hrTable);
            if (!(yDo < yIrT && yIrT < yIr && yIr < yHrT && yHrT < yHr))
                ++failures;
        } else {
            ++failures;
        }
        /* ---- 表满高全展开 + 列宽铺满表宽（[10-18 二次追加·滚动条纪律]）----
         * 高 = 表头高 + 当前行数×统一行高（[10-18 三次追加] 行数=副本，
         * 垂直超出由整页竖滚承担）；地址:值 两列宽合计 = 表宽 - 行号列
         * （表内无横滚条空间）。 */
        {
            XTableWidget* tabs[2];
            int ti;
            tabs[0] = s_mbSlave.irTable;
            tabs[1] = s_mbSlave.hrTable;
            for (ti = 0; ti < 2; ++ti) {
                XTableWidget* tbl = tabs[ti];
                XSize mx;
                int fullH;
                int bodyW;
                int colSum;
                int rows;
                if (!tbl) { ++failures; continue; }
                rows = XTableWidget_rowCount(tbl);
                mx = XWidget_maximumSize((XWidget*)tbl);
                fullH = (int)tbl->m_headerHeight +
                        XTableView_rowHeight(&tbl->m_base) * rows;
                if (mx.height != fullH || mx.width < MB_REG_MIN_COL_W)
                    ++failures;
                bodyW = mx.width - (int)tbl->m_headerWidth;
                colSum = XTableView_columnWidth(&tbl->m_base, 0) +
                         XTableView_columnWidth(&tbl->m_base, 1);
                if (bodyW > 0 && colSum != bodyW) ++failures;
            }
        }
        /* ---- [10-19 框架编辑器迁移] 两表编辑门禁已开且值格无常驻框 ----
         * 观察态纯文本（用户反馈的核心观感）；编辑门禁=表级
         * setItemsEditable(true)，IR/HR 各自独立。 */
        if (!s_mbSlave.irTable || !s_mbSlave.hrTable ||
            !XAbstractItemView_itemsEditable(&s_mbSlave.irTable->m_base.m_base) ||
            !XAbstractItemView_itemsEditable(&s_mbSlave.hrTable->m_base.m_base))
            ++failures;
        if (XTableWidget_cellWidget(s_mbSlave.irTable, 0, 1) ||
            XTableWidget_cellWidget(s_mbSlave.hrTable, 0, 1))
            ++failures;
        /* [10-19 对齐 Qt item flags] 地址列只读：edit() 门禁直接拒绝；
         * 值列照常可编辑。 */
        if (XAbstractItemView_edit(&s_mbSlave.irTable->m_base.m_base,
                                   0, 0) ||
            XAbstractItemView_edit(&s_mbSlave.hrTable->m_base.m_base,
                                   0, 0))
            ++failures;
        if (!XAbstractItemView_edit(&s_mbSlave.irTable->m_base.m_base,
                                    0, 1))
            ++failures;
        XAbstractItemView_closeEditor(
            &s_mbSlave.irTable->m_base.m_base,
            s_mbSlave.irTable->m_base.m_base.m_editor,
            XItemDelegateEndEditHint_NoHint);
        /* ---- [10-19 框架编辑器迁移] 编辑器开合/提交端到端回归锁 ----
         * 程序化 edit() 开 IR[2] 编辑器 → 改文本 → 直发 Return（编辑器
         * 键盘拦截→委托 commitData/closeEditor 链）→ 断言会话关闭+
         * 数据区落库+单元格文本同步。 */
        {
            XAbstractItemView* iv = &s_mbSlave.irTable->m_base.m_base;
            uint16_t savedProbe = 0;
            uint16_t nowProbe = 0;
            XModbusServer_data2(s_mbSlave.server, XModbusInputRegisters,
                                2, &savedProbe);
            if (XAbstractItemView_edit(iv, 2, 1) && iv->m_editor) {
                XKeyEvent* ke = XKeyEvent_create_ex(
                    XCLASS_DEFAULT_MEMORY_TYPE, XEVENT_TYPE_KEY_PRESS,
                    (int)XKey_Return, 0);
                XLineEdit_setText((XLineEdit*)iv->m_editor, "555");
                if (ke) {
                    XObject_event_base((XObject*)iv->m_editor,
                                       (XEvent*)ke);
                    XClassDelete((XEvent*)ke);
                }
                if (XAbstractItemView_isEditing(iv)) ++failures;
                if (!XModbusServer_data2(s_mbSlave.server,
                                         XModbusInputRegisters, 2,
                                         &nowProbe) || nowProbe != 555)
                    ++failures;
                {
                    const char* cellText =
                        XTableWidget_text(s_mbSlave.irTable, 2, 1);
                    if (!cellText || strcmp(cellText, "555") != 0)
                        ++failures;
                }
                /* 还原。 */
                XModbusServer_setData2(s_mbSlave.server,
                                       XModbusInputRegisters, 2,
                                       savedProbe);
                XTableWidget_setText(s_mbSlave.irTable, 2, 1, "0");
            } else {
                ++failures;
            }
        }
        /* ---- 灯格按点数建成（默认 8 点）---- */
        if (s_mbSlave.inCount > 0 && !s_mbSlave.diCells[0]) ++failures;
        if (s_mbSlave.outCount > 0 && !s_mbSlave.doCells[0]) ++failures;
        /* ---- 单机可调前提：数据宿主就绪但未监听 ---- */
        if (!s_mbSlave.server) ++failures;
        /* ---- [CLOSE-WAIT 根修 2026-10-19] 死链巡检定时器已挂（页根
         *         子对象；回调自守卫，非 TCP 通道空转）---- */
#if !defined(_WIN32) && XMODBUS_TCP_ON
        if (!s_mbSlave.reapTimer) ++failures;
#endif
        /* ---- 灯格写路径（评审④ 断点回归锁）----
         * 框架派发槽时给出的 sender 不是发射方（实测是页根），故本页全部
         * 索引槽都按「编译期行号」取参；此处直接调 mb_slaveSetBit 走同一条
         * 写路径，验证「本地点灯 → 数据区」这一段（值进 map 即主站可读）。 */
        if (s_mbSlave.server && s_mbSlave.outCount > 3) {
            uint16_t saved = 0, now = 0;
            if (XModbusServer_data2(s_mbSlave.server, XModbusCoils, 3, &saved)) {
                mb_slaveSetBit(XModbusCoils, 3, true);
                if (!XModbusServer_data2(s_mbSlave.server, XModbusCoils, 3,
                                         &now) || now != 1)
                    ++failures;
                /* [10-18 用户追加规格⑤ 保值回归锁] 点「按点数重建(输出)」
                 * （编译期槽直调，走与按钮同一写路径）后值必须保留——
                 * mb_slaveRebuildMap 的 setValueCount 保值语义不允许被
                 * 新布局破坏；随后换行行数按新点数重算。 */
                s_mbSlave.outCountSpin
                    ? XSpinBox_setValue(s_mbSlave.outCountSpin, 10) : 0;
                mb_slaveApplyDoSlot(NULL, NULL);
                if (!XModbusServer_data2(s_mbSlave.server, XModbusCoils, 3,
                                         &now) || now != 1)
                    ++failures;
                {
                    int cols = s_mbSlave.gridCols > 0 ? s_mbSlave.gridCols : 1;
                    int doRows = (s_mbSlave.outCount + cols - 1) / cols;
                    if (doRows < 1) doRows = 1;
                    if (!s_mbSlave.doGridHost ||
                        XWidget_maximumSize((XWidget*)s_mbSlave.doGridHost)
                                .height != doRows * MB_CELL_H)
                        ++failures;
                }
                mb_slaveSetBit(XModbusCoils, 3, saved != 0);
                /* 还原点数：经段②槽走同一重建路径（幂等）。 */
                s_mbSlave.outCountSpin
                    ? XSpinBox_setValue(s_mbSlave.outCountSpin, 8) : 0;
                mb_slaveApplyDoSlot(NULL, NULL);
            } else {
                ++failures;
            }
        }
        /* ---- 本地写入通路（端到端断点回归锁，[10-19 框架编辑器迁移]）----
         * 覆盖「提交文本 → cellChanged 桥 → 寄存器堆」这段：setText 驱动
         * （与编辑器回车提交同一条模型落库链：setItemText→模型 setData→
         * dataChanged 桥→cellChanged 槽）→ data2 读回断言。IR/HR 各验
         * 一次，同时锁死两表桥接槽「表与行号」不串（HR 写不动 IR 区，
         * 反之亦然）。不依赖编辑器焦点/回车投递。 */
        if (s_mbSlave.server && s_mbSlave.irTable && s_mbSlave.hrTable) {
            int k;
            XTableWidget* tabs[2] = { s_mbSlave.irTable, s_mbSlave.hrTable };
            XModbusRegisterType types[2] = { XModbusInputRegisters,
                                             XModbusHoldingRegisters };
            for (k = 0; k < 2; ++k) {
                uint16_t saved = 0;
                uint16_t now = 0;
                uint16_t other = 0;
                char back[8];
                if (!XModbusServer_data2(s_mbSlave.server, types[k],
                                         3, &saved)) {
                    ++failures;
                    continue;
                }
                XTableWidget_setText(tabs[k], 3, 1, "1234");
                if (!XModbusServer_data2(s_mbSlave.server, types[k],
                                         3, &now) || now != 1234)
                    ++failures;
                /* 串槽检查：本表提交不得写到另一张表的寄存器区。 */
                if (XModbusServer_data2(s_mbSlave.server, types[1 - k],
                                        3, &other) && other == 1234 &&
                    saved != 1234)
                    ++failures;
                /* 还原：setData2 落库 + setText 同步文本（槽差量早退）。 */
                XModbusServer_setData2(s_mbSlave.server, types[k], 3, saved);
                snprintf(back, sizeof(back), "%u", (unsigned)saved);
                XTableWidget_setText(tabs[k], 3, 1, back);
            }
        }
        /* ---- [10-19 框架编辑器迁移] 观察态无框；编辑器几何/可见性/生命
         * 周期由框架视图管理，原「逐框审计/摆位跟随」机制随常驻框退役，
         * 无对应断言（编辑器开合走 XAbstractItemView_edit 框架路径）。 */
        /* ---- [10-18 三次追加] 数量扩缩容 + 保值 + 表内滚动回归 ----
         * IR[2]=4321（setText 驱动，与编辑提交同链）→ 扩到 64 行：
         * rowCount 同步、值不丢、新行回显 map 值；扩容后表满高 > 视口
         * ⇒ 竖滚条有行程，滚动条置中/复原（框架 scrollContentsBy 路径）；
         * 收缩回 16：值仍在 map。最后还原 IR[2]。 */
        if (s_mbSlave.server && s_mbSlave.irCountSpin && s_mbSlave.irTable) {
            uint16_t savedIr2 = 0;
            XScrollBar* vsb;
            if (!XModbusServer_data2(s_mbSlave.server,
                                     XModbusInputRegisters, 2, &savedIr2)) {
                ++failures;
                savedIr2 = 0;
            }
            XTableWidget_setText(s_mbSlave.irTable, 2, 1, "4321");
            XSpinBox_setValue(s_mbSlave.irCountSpin, 64);
            mb_slaveApplyRegCounts(); /* 与 valueChanged 槽同一路径，幂等。 */
            {
                uint16_t now = 0;
                const char* newCell;
                if (XTableWidget_rowCount(s_mbSlave.irTable) != 64 ||
                    !XModbusServer_data2(s_mbSlave.server,
                                         XModbusInputRegisters, 2, &now) ||
                    now != 4321)
                    ++failures;
                /* 新行（63）回显 map 值（未写过 ⇒ "0"）——「已填值不丢」
                 * 与新行初始化的闭环。 */
                newCell = XTableWidget_text(s_mbSlave.irTable, 63, 1);
                if (!newCell || strcmp(newCell, "0") != 0)
                    ++failures;
            }
            vsb = XAbstractScrollArea_verticalScrollBar(
                      (XAbstractScrollArea*)s_mbSlave.irTable);
            if (vsb && XScrollBar_maximum(vsb) > 0) {
                XScrollBar_setValue(vsb, XScrollBar_maximum(vsb) / 2);
                XScrollBar_setValue(vsb, 0);
            }
            XSpinBox_setValue(s_mbSlave.irCountSpin, MB_SLAVE_REG_ROWS);
            mb_slaveApplyRegCounts();
            {
                uint16_t now = 0;
                if (XTableWidget_rowCount(s_mbSlave.irTable) !=
                        MB_SLAVE_REG_ROWS ||
                    !XModbusServer_data2(s_mbSlave.server,
                                         XModbusInputRegisters, 2, &now) ||
                    now != 4321)
                    ++failures;
            }
            /* 还原 IR[2] 原值与单元格文本。 */
            XModbusServer_setData2(s_mbSlave.server, XModbusInputRegisters,
                                   2, savedIr2);
            {
                char back[8];
                snprintf(back, sizeof(back), "%u", (unsigned)savedIr2);
                XTableWidget_setText(s_mbSlave.irTable, 2, 1, back);
            }
        }
        return failures;
    }
}

/** @brief 从站页自适应重排（xgui_demo_pages.h 契约）：转发视口解算。
 *  @details [四区同屏 2026-10-18] 档位判定改由 mb_slaveSyncViewport 统一
 *          承担（build 末尾 / 页根 resizeEvent / 本函数三入口，幂等）。
 *          旧版「定宽 622/定高 330」硬口径随 dataTable 删除作废。 */
void demo_page_modbus_slave_adapt(XWidget* page)
{
    (void)page;
    mb_slaveSyncViewport();
}

/** @brief 主站页自适应重排（xgui_demo_pages.h 契约）：保留签名，空实现。
 * @details [布局容器化 2026-10-17] 页内几何分配全权移交页根 XBoxLayout
 *          （build 一次装配）——XStackedLayout 重贴根几何时经
 *          resize→XLayout_activate 自动重排，寄存器表列组/显示行随视
 *          口的自适应由页根 resizeEvent（MbMasterRoot）承接，本函数
 *          无事可做；主文件 demo_layout_content 调用点保持不动。 */
void demo_page_modbus_master_adapt(XWidget* page)
{
    (void)page;
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
