/**
 * @file       HsfSettingsRemote.c
 * @brief      远端设置页：运行模式 / 自动运行 / 自动模式配置 / 连接信息 /
 *             采集配置 / 数据存储 / 登录策略。
 * @details    [对齐 Python 客户端 RemoteSettingsPage] 卡片、字段与动作逐项对应：
 *              ① 运行模式：模式文案 + 说明 + 切换到手动/自动（需 ModeSwitch）；
 *              ② 自动运行：运行态文案 + 说明 + 启动/停止/急停/复位
 *                 （需 AutoControl）；急停/复位为 op=3/4（2026-11-11 补齐，
 *                 对齐 Python bridge.send_auto_control 的四操作字口径）——
 *                 与模式切换**分开**——切自动模式只是"授权"，不等于"在跑"；
 *              ②b 自动模式配置（2026-11-11 三模式）：产水/反洗/顺洗各一块
 *                 （启动设备多选 + 搅拌电机转速/方向），应用走 0x0117 auto 节
 *                 （需 AccountAdmin）；服务端校验失败的原文进状态栏（fail-closed）；
 *                 [2026-11-12 形态对齐 Python] 每块三段（块标题 / 启动设备
 *                 多选行 / 搅拌电机行），「启动设备」用**多选下拉 + 勾选
 *                 浮层**（公共多选组件 HsfMultiSelect）取代 8 个平铺复选框；
 *              ③ 连接信息：服务端地址 / 用户 / 角色；
 *              ④ 采集配置：7 路设备（搅拌泵/温湿度/进水浊度仪/出水浊度仪/流量计/
 *                 模拟量/数字量）开关 + 轮询周期，刷新/应用（需 AccountAdmin）；
 *              ⑤ 数据存储：启用/路径/入库周期/容量上限/到限策略/保留分段 +
 *                 容量实况 + 当前库文件，保存（需 AccountAdmin）；
 *              ⑥ 登录策略：匿名/同账并发/会话上限（需 AccountAdmin）。
 *              数据源：0x0116 配置快照、0x0115 自动运行、0x0105 运行模式，
 *              键名与 Python 端 proto.CFGK_* 一致。
 *              [回显时机] 只在**新快照到达**（configGetSeq 变化）时回填控件，
 *              否则 200ms 巡检会把用户正在改的值覆盖回去；auto 节只在应答
 *              确实带它时铺回（局部应答不得把模式块冲回缺省）。
 *              [整体视觉对齐 Python 2026-11-12] Python 七张 Card 各有标题、
 *              状态标签 24px 粗体（运行模式）/20px（自动运行）、说明段走
 *              caption 次级色、按钮行一律「按钮靠左 + 行尾 stretch」。C 端
 *              对齐口径：① 七块**全部**带卡标题（运行模式/自动运行/连接
 *              信息三张本轮补齐，样式沿用本页既有分区标题字号基准）；
 *              ② 状态标签字号对齐（运行模式 24px 粗体 / 自动运行 20px）；
 *              ③ 说明段仍是次级文本色（HsfTheme_applySecondaryText）；
 *              ④ 按钮行一律靠左（保存 / 应用登录策略两钮原先竖排通栏，
 *              本轮改横排 + 行尾 stretch）。
 *              [卡容器 2026-11-12 弃 XGroupBox] 七分区包进页内私有自绘卡
 *              HsfSrCard（视觉/生命周期抄 HsfHomePage.c 的 HsfHomeCard：
 *              白底圆角 + 粗体标题 + m_bodyLay 竖排，堆分配挂 parent 随父
 *              级联析构、不 Overload Deinit）——XGroupBox 析构删子树有
 *              UAF（cdb 实锤，见 HsfSrCard 结构体注释），不再使用。
 */
#include "HsfSettingsRemote.h"

#include "HsfSettingsLocal.h" /* 巡检周期=本地设置「界面刷新周期」 */

#include "HsfLinkClient.h"
#include "HsfLinkConfig.h"
#include "HsfMultiSelect.h" /* 公共多选组件：启动设备多选下拉 + 勾选浮层。 */
#include "HsfTheme.h"
#include "HsfUi.h"     /* 字号折算（hsfUi_pageFontPx/tagFontBase/reapply）。 */
#include "XObject.h"   /* XObject_connect_2 / XObject_destroyed_signal（析构收尾）。 */
#include "XBoxLayout.h"
#include "XCheckBox.h"
#include "XComboBox.h"
#include "XFont.h"     /* 控件字号折算（XFont_setPixelSize）。 */
#include "XEvent.h"            /* 卡片自绘：XEVENT_TYPE_PAINT / XEvent_type。 */
#include "XGridLayout.h"       /* 采集配置三列表格（设备 / 采集 / 周期）。 */
#include "XLabel.h"
#include "XLineEdit.h"
#include "XPainter.h" /* adapt：说明行保高按标签自字实测行高；卡面圆角绘制。 */
#include "XWidget_Protected.h" /* 卡片自绘：XWidget_paintImage/paintOffset。 */
#include "XProgressBar.h"      /* 存储容量占用条（对齐 Python 客户端）。 */
#include "XPushButton.h"
#include "XSpinBox.h"
#include "XTimer.h"
#include "XMemory.h"
#include "XString.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XCHECKBOX_ON && XSPINBOX_ON

/** @brief 采集配置行数（与 Python/服务端键一一对应）。
 *  [2026-11-10 逐台] 现场两台浊度仪 + 一台流量计各自可调，行序固定：
 *  0=搅拌泵(变频器) 1=温湿度 2=进水浊度仪 3=出水浊度仪 4=流量计 5=模拟量 6=数字量。 */
#define SR_COLLECT_ROWS 7

/** @brief 轮询周期范围（与 Python CFG_POLL_MS_MIN/MAX 同值）。 */
#define SR_POLL_MS_MIN 50
#define SR_POLL_MS_MAX 60000
/** @brief 容量上限范围（MB；与 Python STORAGE_MAX_MB_* 同值；0=不限）。 */
#define SR_MAX_MB_MIN 0
#define SR_MAX_MB_MAX (1024 * 1024)

/** @brief 采集配置行（开关 + 周期；标签为静态串）。 */
typedef struct HsfSrCollectRow {
    XCheckBox m_chk;
    XSpinBox  m_spin;
} HsfSrCollectRow;

/** @brief 自动模式配置块数（产水/反洗/顺洗；协议稳定顺序）。 */
#define SR_AUTO_MODES HSF_AUTO_MODE_COUNT
/** @brief 每模式可勾选设备数（valve1..6 + stirPump + inletPump）。 */
#define SR_AUTO_DEVS  HSF_AUTO_DEVICE_COUNT
/* 搅拌转速折算基点（铭牌额定点 50Hz≈1440rpm；Python AUTO_STIR_RATED_*
 * 同值同口径——转速是操作员语言、Hz 是变频器实发量，界面两个都给）。 */
#define SR_AUTO_RATED_RPM 1440.0
#define SR_AUTO_RATED_HZ  50.0
/* [2026-11-12 形态对齐 Python] 自动模式块行标签列宽基线（Python 端
 * lbl1/lbl2 都 setMinimumWidth(110) ⇒ 两行的标签成列）。 */
#define SR_AUTO_CAP_MIN_W 110
/** @brief 「方向」下拉最小宽（对齐变频器页 dir_combo 的 100）。 */
#define SR_AUTO_DIR_MIN_W 100
/** @brief 循环「比较」下拉最小宽下限（随字号折算；实际宽按字体实测更大者）。 */
#define SR_CYCLE_CMP_MIN_W 120
/** @brief 循环「压差阈值」输入框最小宽下限（随字号折算）。 */
#define SR_CYCLE_PRESS_MIN_W 88/** @brief 「方向」下拉文字外的余量（箭头 + 左右内边距，随字号折算）。 */
#define SR_AUTO_DIR_PAD   44
/* 「启动设备」多选钮的最小/最大宽（Python setMinimumWidth(150) /
 * setMaximumWidth(420)：不设上限会一路铺满整行，420 装得下常见组合）。 */
#define SR_AUTO_SEL_MIN_W 150
#define SR_AUTO_SEL_MAX_W 420

/* [编译期断言] 本页块数/设备数与协议常量绑死；MSVC C 模式不认
 * _Static_assert ⇒ 负数组长度 typedef（不匹配报 C2118/C2148）。 */
typedef char SrAutoModeCountAssert[(SR_AUTO_MODES == 3) ? 1 : -1];
typedef char SrAutoDevCountAssert[(SR_AUTO_DEVS == 8) ? 1 : -1];

/* ==================== 页内私有卡片容器 ==================== */

/**
 * @brief 白底软描边卡片（页内私有自绘；结构/生命周期照抄 HsfHomePage.c
 *        的 HsfHomeCard——堆分配挂 parent、随父级联析构、不 Overload
 *        Deinit，首页多年正常退出的久经验证形态）。
 * @details [为什么不用 XGroupBox] XGroupBox 析构删子树存在 UAF（cdb 实锤：
 *          退出期 VXGroupBox_deinit → XClass_delete_base → VXLabel_deinit →
 *          label_clearContents → 已释放容器）⇒ 弃用，换页内私有自绘卡片
 *          （对齐 Python Card(QFrame) 自绘形态——pages_other.py 注释里本就
 *          写着「C 端 HsfCard 同款」）。
 *          [UAF 红线] paintEvent 只画卡面（圆角底 + 软边框），不对子控件
 *          做任何操作；析构走默认（子控件随控件树级联，由基类统一收）。
 *          标题用 XLabel 成员（照 HsfHomeCard 的实现抄最稳），带
 *          hsfFontBase 标记 ⇒ 字号随页面缩放走 hsfUi_reapplyTaggedFonts。
 */
typedef struct HsfSrCard
{
    XWidget    m_base;    /**< 容器；第一个成员。 */
    XBoxLayout m_layout;  /**< 竖排：标题 / 内容（margins 抄 HsfHomeCard）。 */
    XLabel     m_title;   /**< 卡片标题（粗体，SR_HEAD_FONT_PX 基准）。 */
    XBoxLayout m_bodyLay; /**< 内容区布局（段内控件/行布局挂这里）。 */
} HsfSrCard;

/** @brief 自动模式配置块（单模式：设备多选 + 搅拌电机参数；2026-11-12）。
 *  @details 设备下标 = HsfConfigAutoMode.m_devices 稳定位序（0..5=电动球阀
 *           1..6、6=搅拌泵、7=进水泵），发送与回显都按下标走，不依赖勾选
 *           顺序；方向下拉 0=正转（false）1=反转（true），与变频器页
 *           userData=reverse 同口径。
 *           [2026-11-12 形态对齐 Python pages_other.py:_build_auto_mode_block]
 *           「启动设备」由 **8 个平铺复选框（4×2 网格）** 改为**多选下拉
 *           按钮 + 勾选浮层**（公共多选组件 HsfMultiSelect）：三个模式块
 *           各占三行（块标题 / 设备行 / 搅拌电机行），不再各吃 6 行复选框
 *           （用户反馈「界面样子不好看，特别是自动模式的」）。汇总文案
 *           三态与 Python MultiCheckCombo 同口径：全勾=「全部设备」、
 *           全不勾=「未选择」、部分=勾中的名字顿号连接。 */
typedef struct HsfSrAutoMode {
    HsfMultiSelect m_devSel; /**< 启动设备多选下拉（内嵌公共多选组件）。
                              *   [所有权] 组件内部的**勾选浮层（无父顶层
                              *   Popup）与项名 XString** 由 srPageDestroyedSlot
                              *   在页 destroyed 时经 HsfMultiSelect_destroy
                              *   释放；按钮本体是本页子控件，随控件树级联
                              *   销毁（组件不删它——见 HsfMultiSelect.h 契约）。 */
    XLabel   m_devCap;  /**< 行1 标签「启动设备」（标签列定宽基准）。 */
    XLabel   m_stirCap; /**< 行2 标签「搅拌电机」（标签列定宽基准）。 */
    XSpinBox m_rpm;     /**< 搅拌电机转速（rpm；0..1440 缺省 1440）。 */
    XComboBox m_dir;    /**< 方向（0=正转 1=反转）。 */
    XLabel   m_hzHint;  /**< rpm→Hz 折算提示（恒占位，随转速刷新）。 */
    /* [逐模式阀复位 2026-11-12] 两个开关随模式下发（缺省：停止=勾/急停=不勾）。 */
    XLabel   m_resetCap;    /**< 行3 标签「阀复位」（与上两行同列宽）。 */
    XCheckBox m_resetStop;  /**< 停止时关阀复位。 */
    XCheckBox m_resetEstop; /**< 急停时关阀复位。 */
} HsfSrAutoMode;

/** @brief 远端设置页对象。 */
typedef struct HsfSettingsRemote {
    XWidget      m_base;        /**< 页根（第一成员）。 */
    XBoxLayout   m_layout;      /**< 页根竖排布局。 */
    XLabel       m_title;       /**< 页标题。 */
    /* ---- 运行模式（卡片容器 2026-11-12·对齐 Python Card） ---- */
    HsfSrCard*   m_cardMode;    /**< 卡容器：运行模式（页内自绘卡，随父级联析构）。 */
    XLabel       m_headMode;    /**< 旧标题成员（标题现由卡 paint 期的 m_title 承担，保留兼容旧引用）。 */
    XLabel       m_mode;
    XLabel       m_modeHint;
    XPushButton  m_btnManual;
    XPushButton  m_btnAuto;
    /* ---- 自动运行 ---- */
    HsfSrCard*   m_cardAuto;    /**< 卡容器：自动运行（页内自绘卡）。 */
    XLabel       m_headAuto;    /**< 旧标题成员（标题现由卡 m_title 承担）。 */
    XLabel       m_auto;
    XLabel       m_autoHint;
    XPushButton  m_btnAutoStart;
    XPushButton  m_btnAutoStop;
    XPushButton  m_btnEstop;      /**< 急停（op=3；永远放行 + 锁存）。 */
    XPushButton  m_btnReset;      /**< 复位（op=4；解除急停/报警锁存）。 */
    /* ---- 自动模式配置（产水/反洗/顺洗三模式块；2026-11-11）---- */
    HsfSrCard*   m_cardCfg;     /**< 卡容器：自动模式配置（页内自绘卡）。 */
    HsfSrAutoMode m_autoMode[SR_AUTO_MODES]; /**< 三模式块（稳定顺序）。 */
    XLabel       m_autoCfgHint;  /**< 配置卡说明（wordWrap，adapt 保高）。 */
    XPushButton  m_btnAutoApply; /**< 应用自动配置（0x0117 auto 节：modes+cycle 一次）。 */
    /* ---- 自动循环（组合模式「自动」；2026-11-12 与 Python 对齐）---- */
    XLabel      m_cycleHead;     /**< 卡内小标题「自动循环（组合模式「自动」）」。 */
    XCheckBox   m_cycleEnable;   /**< 启用自动循环模式。 */
    XLabel      m_cyclePressCap; /**< 行标签「压差阈值」。 */
    XLineEdit   m_cyclePress;    /**< 阈值文本（MPa；0.00~10.00）。 */
    XLabel      m_cyclePressUnit;/**< 单位「MPa」。 */
    XLabel      m_cycleCmpCap;   /**< 行标签「比较」。 */
    XComboBox   m_cycleCompare;  /**< 0=gt（大于才切）1=lt（小于才切）。 */
    XCheckBox   m_cycleFallback; /**< 产水定时兜底（缺省关）。 */
    XLabel      m_cycleTimerCap; /**< 行标签「产水」。 */
    XLabel      m_cycleBackCap;  /**< 行标签「反洗」。 */
    XLabel      m_cycleRinseCap; /**< 行标签「顺洗」。 */
    XSpinBox    m_cycleTimerMin; /**< 产水兜底分钟（1..1440）。 */
    XSpinBox    m_cycleBackMin;  /**< 反洗分钟（1..1440）。 */
    XSpinBox    m_cycleRinseMin; /**< 顺洗分钟（1..1440）。 */
    XLabel      m_cycleHint;     /**< 循环说明。 */
    /* ---- 连接信息 ---- */
    HsfSrCard*   m_cardConn;    /**< 卡容器：连接信息（页内自绘卡）。 */
    XLabel       m_headConn;    /**< 旧标题成员（标题现由卡 m_title 承担，保留兼容旧引用）。 */
    XLabel       m_conn;
    /* ---- 采集配置 ---- */
    HsfSrCard*   m_cardCollect;   /**< 卡容器：采集配置（页内自绘卡）。 */
    HsfSrCollectRow m_collect[SR_COLLECT_ROWS];
    /** @brief 采集配置网格（3 列：设备 / 采集 / 周期）。
     *  @details [为什么改网格 2026-11-12·对齐 Python pages_other.py:4573]
     *           原实现每行一个独立 HBoxLayout ⇒ 行标签宽由 sizeHint 定
     *           （「进水浊度仪」4 字 vs「搅拌泵」3 字）⇒ 勾选框与周期框
     *           **逐行错位**，一眼看去像没对齐。Python 用 QGridLayout
     *           + 独立表头行（其注释记着一条坑：表头若塞进数据行同格，
     *           后加者会把数据行控件顶掉）。本帧同样：表头独占第 0 行，
     *           数据行 1..7。
     *           [内嵌而非 malloc] 网格与标签都是结构体内嵌成员 ⇒ 随页
     *           对象级联析构，无手工释放、无壳泄漏（auto 设备区那套
     *           malloc 网格/标签属另一处既存写法，本处不复制）。 */
    XGridLayout m_collectGrid;
    XLabel      m_collectHead[3];   /**< 表头：设备 / 采集 / 周期。 */
    XLabel      m_collectName[SR_COLLECT_ROWS]; /**< 各行设备名。 */
    XLabel       m_collectHint;
    XPushButton  m_btnCfgRefresh;
    XPushButton  m_btnCfgApply;
    /* ---- 运行保护（2026-11-15）---- */
    HsfSrCard*   m_cardProtect;    /**< 卡容器：运行保护（页内自绘卡）。 */
    XCheckBox    m_protectStop;    /**< 超时后停止设备（true=停）。 */
    XSpinBox     m_protectTimeout;/**< 空管判定时长（秒；0=关闭保护）。 */
    XLabel       m_protectCap;     /**< 「空管判定时长」行标签（内嵌成员）。 */
    /* [压差过大保护 2026-11-15] */
    XCheckBox    m_pressHighEnable;/**< 启用压差过大保护。 */
    XSpinBox     m_pressHighLimit; /**< 压差上限（kPa；界面用 kPa，协议用 MPa）。 */
    XCheckBox    m_pressHighStop;  /**< 触发后停止设备。 */
    XLabel       m_pressHighCap;   /**< 「压差上限」行标签（内嵌成员）。 */
    XLabel       m_protectHint;
    XLabel       m_protectHint2;   /**< 压差过大保护分区的说明（内嵌成员）。 */
    XPushButton  m_btnProtectApply;
    bool         m_protectSeen;    /**< 收到过 protect 节快照（2026-11-15）。 */
    /* ---- 数据存储 ---- */
    HsfSrCard*   m_cardStore;     /**< 卡容器：数据存储（页内自绘卡）。 */
    XCheckBox    m_storeEnable;
    XLineEdit    m_storePath;
    XSpinBox     m_storeSample;
    XSpinBox     m_storeMax;
    XComboBox    m_storePolicy;
    XSpinBox     m_storeKeep;
    /** [入库触发 2026-11-12] 0=持续采集入库（always）1=仅自动运行时（auto）。 */
    XComboBox    m_storeTrigger;
    XLabel       m_policyHint;
    XProgressBar m_storageBar;   /**< 容量占用条（0..100%；上限不限时恒 0）。 */
    XLabel       m_storageUse;
    XLabel       m_dbPath;
    bool         m_storagePaused; /**< 落盘已停写（用量行刷警告色）。 */
    XPushButton  m_btnSave;
    /* ---- 登录策略（账号）---- */
    HsfSrCard*   m_cardLogin;     /**< 卡容器：登录策略（页内自绘卡）。 */
    XCheckBox    m_loginAnon;     /**< 允许匿名登录。 */
    XCheckBox    m_loginConcur;   /**< 允许同账并发。 */
    XSpinBox     m_loginMax;      /**< 会话上限（0=不限）。 */
    XLabel       m_loginHint;     /**< 策略说明/生效提示。 */
    XPushButton  m_btnLoginSave;  /**< 应用登录策略。 */
    /* ---- 运行期 ---- */
    XTimer*      m_timer;
    void*        m_statusUser;   /**< 状态栏宿主（HsfPages_setStatus 的 user）。 */
    bool         m_fetched;      /**< 已发过配置查询（断开后复位）。 */
    bool         m_wasVisible;   /**< 上一拍本页是否可见（进页重拉的上升沿）。 */
    int          m_seenGetSeq;   /**< 已回填的快照序号。 */
    int          m_seenWriteSeq; /**< 已提示的写应答序号。 */
    int          m_seenCmdSeq;   /**< 已提示的控制应答序号（自动运行）。 */
    char         m_lastModeErr[96]; /**< 上次提示过的模式切换错误。 */
    /* ---- 在途操作（「正在…」的终态兜底 2026-11-11）----
     * [为什么要有] 现场报「点了保存一直停在『正在应用数据存储配置…』」：
     * 各类操作都是**发出去就不管**，只有「收到回包」才有终态提示（配置写/
     * 控制应答/模式错误三处）。一旦回包**丢了或对端没回**，状态栏就永远
     * 停在「正在…」——用户看着像界面死了。⇒ 统一登记在途操作 + 定时器
     * 超时兜底：**任何「正在…」都必须有终态**（成功/失败/超时三选一）。
     * m_opKind：0=无 1=采集 2=落盘 3=登录 4=切手动 5=切自动 6=自动启
     * 7=自动停 8=急停 9=复位 10=自动模式配置。 */
    int          m_opKind;      /**< 在途操作类型（0=无）。 */
    int          m_opTicks;     /**< 在途计数（超时兜底用）。 */
    bool         m_opMsgSticky; /**< 刚写过「操作终态」消息：例行消息（配置已刷新）
                                 *   不得覆盖它（用户要看到的是「已保存/失败/
                                 *   未收到回执」，不是「配置已刷新」）。 */
} HsfSettingsRemote;

/* ==================== 小工具 ==================== */

/** @brief 状态栏提示（userData 为导航宿主窗口；NULL 时忽略）。 */
static void srStatus(HsfSettingsRemote* self, const char* fmt, ...)
{
    char buf[256];
    va_list ap;
    if (!self || !self->m_statusUser) return;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    HsfPages_setStatus(self->m_statusUser, buf);
}

/** @brief 在途操作的超时兜底阈值（定时器拍数；页面定时器=界面刷新周期，
 *         缺省 200ms ⇒ 50 拍 ≈ 10s）。 */
#define SR_OP_TIMEOUT_TICKS 50

/** @brief 登记一个在途操作（「正在…」的终态兜底用）。 */
static void srOpBegin(HsfSettingsRemote* self, int kind)
{
    if (!self) return;
    self->m_opKind = kind;
    self->m_opTicks = 0;
    self->m_opMsgSticky = false;   /* 新操作开始：上一条终态消息可以被替换 */
}

/** @brief 在途操作轮询：超过阈值仍没终态 ⇒ 明确告诉用户「没收到回执」。
 *  @details 这一条是现场报「点了保存一直停在『正在应用…』」的兜底：回包丢失
 *           或对端没回时，状态栏不再无限停在「正在…」，而是给出可行动的
 *           提示（去服务端日志看）。 */
static void srOpPoll(HsfSettingsRemote* self)
{
    if (!self || self->m_opKind <= 0) return;
    if (++self->m_opTicks <= SR_OP_TIMEOUT_TICKS) return;
    srStatus(self, "未收到服务端回执（可能已超时）：请查看服务端日志");
    self->m_opMsgSticky = true;
    self->m_opKind = 0;
}

/** @brief 只在值真变时 setEnabled（避免巡检每拍触发重绘）。 */
static void srEnable(XWidget* w, bool on)
{
    if (!w) return;
    if (XWidget_isEnabled(w) != on) XWidget_setEnabled(w, on);
}

/** @brief 采集行控件（开关/周期）启用态。 */
static void srEnableCollect(HsfSettingsRemote* self, bool on)
{
    int i;
    for (i = 0; i < SR_COLLECT_ROWS; ++i) {
        srEnable((XWidget*)&self->m_collect[i].m_chk, on);
        srEnable((XWidget*)&self->m_collect[i].m_spin, on);
    }
    srEnable((XWidget*)&self->m_btnCfgApply, on);
}

/** @brief 自动模式配置块（设备多选/转速/方向/应用钮）启用态。
 *  @details 与采集卡同款只读闸（无管理权限改了也应用不上——Python 端
 *           `_auto_mode_blocks` 的 setEnabled(can_cfg) 同口径）。 */
static void srEnableAuto(HsfSettingsRemote* self, bool on)
{
    int m;
    if (!self) return;
    for (m = 0; m < SR_AUTO_MODES; ++m) {
        srEnable(HsfMultiSelect_button(&self->m_autoMode[m].m_devSel), on);
        srEnable((XWidget*)&self->m_autoMode[m].m_rpm, on);
        srEnable((XWidget*)&self->m_autoMode[m].m_dir, on);
        srEnable((XWidget*)&self->m_autoMode[m].m_resetStop, on);
        srEnable((XWidget*)&self->m_autoMode[m].m_resetEstop, on);
    }
    /* [自动循环 2026-11-12] 同一权限门（无管理权改了也应用不上）。 */
    srEnable((XWidget*)&self->m_cycleEnable, on);
    srEnable((XWidget*)&self->m_cyclePress, on);
    srEnable((XWidget*)&self->m_cycleCompare, on);
    srEnable((XWidget*)&self->m_cycleFallback, on);
    srEnable((XWidget*)&self->m_cycleTimerMin, on);
    srEnable((XWidget*)&self->m_cycleBackMin, on);
    srEnable((XWidget*)&self->m_cycleRinseMin, on);
    srEnable((XWidget*)&self->m_btnAutoApply, on);
}

/** @brief 某模式块「启动设备」的勾选掩码（位 i = 第 i 个设备；0=全不勾）。
 *  @note  多选组件的掩码 ↔ 协议 m_devices[] 一一对应（下标同序），发送
 *         与回显都走这个掩码，不依赖用户勾选的先后顺序。 */
static uint32_t srAutoDevMask(const HsfSettingsRemote* self, int m)
{
    if (!self || m < 0 || m >= SR_AUTO_MODES) return 0u;
    return HsfMultiSelect_mask(&self->m_autoMode[m].m_devSel);
}

/** @brief 回写某模式块「启动设备」的勾选掩码（服务端应答 ⇒ 整体回显）。
 *  @note  组件内部同值早退 ⇒ 回显同值不产生脏区。 */
static void srAutoDevSetMask(HsfSettingsRemote* self, int m, uint32_t mask)
{
    if (!self || m < 0 || m >= SR_AUTO_MODES) return;
    HsfMultiSelect_setMask(&self->m_autoMode[m].m_devSel, mask);
}

/** @brief 页销毁：收掉三个「启动设备」多选组件的浮层与项名表。
 *  @details 浮层是无父顶层 Popup（不随控件树级联）⇒ 必须显式 destroy，
 *           否则每进一次本页泄漏三个 Popup 窗口。destroyed 在
 *           VXObject_deinit 早期发出（页面字段仍有效），用 connect_2
 *           （无 receiver）：槽首参=信号 sender，即页对象本身。
 *           HsfMultiSelect_destroy **不删按钮**（按钮是本页子控件，随控件
 *           树级联）——按组件头文件的所有权契约。 */
static void srPageDestroyedSlot(XObject* sender, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)sender;
    int m;
    (void)args;
    if (!self) return;
    for (m = 0; m < SR_AUTO_MODES; ++m)
        HsfMultiSelect_destroy(&self->m_autoMode[m].m_devSel);
}

/** @brief 搅拌转速 → Hz 折算提示（三模式块共用一份文案公式）。 */
static void srSyncAutoHzHint(HsfSettingsRemote* self)
{
    int m;
    char buf[48];
    if (!self) return;
    for (m = 0; m < SR_AUTO_MODES; ++m) {
        snprintf(buf, sizeof(buf), "折算 ≈ %.1f Hz",
                 XSpinBox_value(&self->m_autoMode[m].m_rpm)
                     / SR_AUTO_RATED_RPM * SR_AUTO_RATED_HZ);
        /* XLabel 同值早退：每拍同步零脏区。 */
        XLabel_setText_2(&self->m_autoMode[m].m_hzHint, buf);
    }
}

/* ==================== 槽：运行模式 / 自动运行 ==================== */

static void srManualSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                            offsetof(HsfSettingsRemote, m_btnManual));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_MODE_SWITCH)) {
        srStatus(self, "权限不足：切换运行模式需要管理员账户");
        return;
    }
    if (HsfLinkClient_setRunMode(false))
    {
        srOpBegin(self, 4);
        srStatus(self, "正在切换到手动模式…");
    }
    else
        srStatus(self, "切换失败：服务端未连接");
}

static void srAutoSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                            offsetof(HsfSettingsRemote, m_btnAuto));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_MODE_SWITCH)) {
        srStatus(self, "权限不足：切换运行模式需要管理员账户");
        return;
    }
    if (HsfLinkClient_setRunMode(true))
    {
        srOpBegin(self, 5);
        srStatus(self, "正在切换到自动模式…");
    }
    else
        srStatus(self, "切换失败：服务端未连接");
}

static void srAutoStartSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnAutoStart));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_AUTO_CONTROL)) {
        srStatus(self, "权限不足：自动运行控制需要管理员账户");
        return;
    }
    if (HsfLinkClient_autoControl(HSF_AUTO_OP_START))
    {
        srOpBegin(self, 6);
        srStatus(self, "正在启动自动运行…");
    }
    else
        srStatus(self, "操作失败：服务端未连接");
}

static void srAutoStopSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnAutoStop));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_AUTO_CONTROL)) {
        srStatus(self, "权限不足：自动运行控制需要管理员账户");
        return;
    }
    if (HsfLinkClient_autoControl(HSF_AUTO_OP_STOP))
    {
        srOpBegin(self, 7);
        srStatus(self, "正在停止自动运行…");
    }
    else
        srStatus(self, "操作失败：服务端未连接");
}

static void srEstopSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnEstop));
    (void)args;
    if (!self) return;
    /* [权限闸同 srAutoStartSlot] 急停虽"永远放行"于服务端闸（停机类动作
     * 不被挡），客户端入口仍按 AutoControl 权限点把关（界面可达性口径）。 */
    if (!HsfLinkClient_can(HSF_PERM_AUTO_CONTROL)) {
        srStatus(self, "权限不足：自动运行控制需要管理员账户");
        return;
    }
    if (HsfLinkClient_autoControl(HSF_AUTO_OP_ESTOP))
    {
        /* [急停口径] 服务端停自动控制器并全停受控设备 + 锁存（复位前
         * 拒绝一切启动）；设备级急停（切断动力）是硬件链职责。 */
        srOpBegin(self, 8);
        srStatus(self, "正在急停（停控制器并全停受控设备）…");
    }
    else
        srStatus(self, "操作失败：服务端未连接");
}

static void srResetSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnReset));
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_AUTO_CONTROL)) {
        srStatus(self, "权限不足：自动运行控制需要管理员账户");
        return;
    }
    if (HsfLinkClient_autoControl(HSF_AUTO_OP_RESET))
    {
        /* [复位口径] 解除急停锁存 + 报警锁存，回到停止态——**不是**
         * 重新启动（要再跑须再按启动）。 */
        srOpBegin(self, 9);
        srStatus(self, "正在复位（解除急停/报警锁存）…");
    }
    else
        srStatus(self, "操作失败：服务端未连接");
}

/* ==================== 槽：自动模式配置（0x0117 auto 节；2026-11-11） ==================== */

/** @brief 应用自动模式配置（三模式块 → auto 配置节；需管理员）。
 *  @details [fail-closed] 服务端逐模式校验（modes 缺失/未知模式 id/设备
 *           全未知/stirRpm 非整数都拒整包），失败包络的 err 原文经
 *           configError 进状态栏——对齐 Python `_apply_auto` 的兜底
 *           （不静默、不假成功）。成功应答带 auto 节全量 ⇒ configGetSeq
 *           推进 → 回显（「界面显示的就是生效的」）。 */
static void srAutoApplySlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                    offsetof(HsfSettingsRemote, m_btnAutoApply));
    HsfConfigAuto cfg;
    int m;
    int i;
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        srStatus(self, "未连接服务端");
        return;
    }
    if (!HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN)) {
        srStatus(self, "权限不足：修改服务端配置需要管理员账户");
        return;
    }
    /* [栈上局部结构体先 memset] /RTC1 口径：结构体交给框架/发送前一律清零。 */
    memset(&cfg, 0, sizeof(cfg));
    for (m = 0; m < SR_AUTO_MODES; ++m) {
        uint32_t mask = srAutoDevMask(self, m);
        cfg.m_modes[m].m_stirRpm =
            XSpinBox_value(&self->m_autoMode[m].m_rpm);
        cfg.m_modes[m].m_stirReverse =
            XComboBox_currentIndex(&self->m_autoMode[m].m_dir) == 1;
        for (i = 0; i < SR_AUTO_DEVS; ++i)
            cfg.m_modes[m].m_devices[i] = (mask & (1u << i)) != 0;
        /* [逐模式阀复位 2026-11-12] 每次提交都带。 */
        cfg.m_modes[m].m_resetValvesOnStop =
            XCheckBox_isChecked(&self->m_autoMode[m].m_resetStop);
        cfg.m_modes[m].m_resetValvesOnEstop =
            XCheckBox_isChecked(&self->m_autoMode[m].m_resetEstop);
    }
    /* [自动循环 2026-11-12] 与三模式**同一份 auto 节、一次提交**。
     * 阈值是文本 ⇒ 解析失败**不发送**（fail-closed：写错一位会把「大于
     * 才切」变成另一个数；宁可报错让人重填）。 */
    {
        const char* txt = XLineEdit_text(&self->m_cyclePress);
        char* end = NULL;
        double val = txt ? strtod(txt, &end) : 0.0;
        if (!txt || !txt[0] || (end && *end != '\0') || val < 0.0 ||
            val > HSF_AUTO_CYCLE_PRESS_THRESHOLD_MAX) {
            srStatus(self, "压差阈值需为 0~10 的数字（MPa），请重填后再应用");
            return;
        }
        cfg.m_cycle.m_pressThreshold = val;
    }
    cfg.m_cycle.m_enabled = XCheckBox_isChecked(&self->m_cycleEnable);
    cfg.m_cycle.m_timerFallback = XCheckBox_isChecked(&self->m_cycleFallback);
    cfg.m_cycle.m_timerMinutes = XSpinBox_value(&self->m_cycleTimerMin);
    cfg.m_cycle.m_backwashMinutes = XSpinBox_value(&self->m_cycleBackMin);
    cfg.m_cycle.m_rinseMinutes = XSpinBox_value(&self->m_cycleRinseMin);
    snprintf(cfg.m_cycle.m_pressCompare, sizeof(cfg.m_cycle.m_pressCompare),
             "%s", XComboBox_currentIndex(&self->m_cycleCompare) == 1
                        ? HSF_AUTO_PRESS_COMPARE_LT : HSF_AUTO_PRESS_COMPARE_GT);
    if (HsfLinkClient_configSetAuto(&cfg))
    {
        srOpBegin(self, 10);
        /* [文案 2026-11-12·逐字对齐 Python _apply_auto（pages_other.py:5406）]。 */
        srStatus(self, "正在应用自动配置（模式 + 循环）…");
    }
    else
        srStatus(self, "应用失败：请求未发出");
}

/* ==================== 槽：采集配置 / 数据存储 ==================== */

/** @brief 拉一次配置快照（刷新按钮/首次连接）。 */
static bool srFetchConfig(HsfSettingsRemote* self, bool silent)
{
    if (!self) return false;
    if (!HsfLinkClient_isReady()) {
        if (!silent) srStatus(self, "未连接服务端");
        return false;
    }
    (void)HsfLinkClient_queryRunMode();   /* 顺带对账模式/自动运行态 */
    if (!HsfLinkClient_configGet()) {
        if (!silent) srStatus(self, "读取配置失败：请求未发出");
        return false;
    }
    if (!silent) srStatus(self, "正在刷新配置…");
    return true;
}

static void srCfgRefreshSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnCfgRefresh));
    (void)args;
    srFetchConfig(self, false);
}

/** @brief 应用采集配置（开关即时生效；周期下一拍生效）。 */
static void srCfgApplySlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnCfgApply));
    HsfConfigCollect c;
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        srStatus(self, "未连接服务端");
        return;
    }
    if (!HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN)) {
        srStatus(self, "权限不足：修改服务端配置需要管理员账户");
        return;
    }
    memset(&c, 0, sizeof(c));
    c.m_vfdEnabled = XCheckBox_isChecked(&self->m_collect[0].m_chk);
    c.m_temphumEnabled = XCheckBox_isChecked(&self->m_collect[1].m_chk);
    /* [逐台开关 2026-11-10] 两台浊度仪是两台独立设备（站1/站3）：行 2/3
     * 各勾各的，不再互相联动。共同键 turbidityEnabled 仍发（旧服务端只认
     * 它）：取「任一台启用」，语义与旧版"共用开关"一致。 */
    c.m_turbidity1Enabled = XCheckBox_isChecked(&self->m_collect[2].m_chk);
    c.m_turbidity2Enabled = XCheckBox_isChecked(&self->m_collect[3].m_chk);
    c.m_turbidityEnabled = c.m_turbidity1Enabled || c.m_turbidity2Enabled;
    c.m_flowmeterEnabled = XCheckBox_isChecked(&self->m_collect[4].m_chk);
    c.m_analogEnabled = XCheckBox_isChecked(&self->m_collect[5].m_chk);
    c.m_digitalEnabled = XCheckBox_isChecked(&self->m_collect[6].m_chk);
    c.m_vfdPollMs = XSpinBox_value(&self->m_collect[0].m_spin);
    c.m_temphumPollMs = XSpinBox_value(&self->m_collect[1].m_spin);
    c.m_turbidity1PollMs = XSpinBox_value(&self->m_collect[2].m_spin);
    c.m_turbidity2PollMs = XSpinBox_value(&self->m_collect[3].m_spin);
    c.m_flowmeterPollMs = XSpinBox_value(&self->m_collect[4].m_spin);
    c.m_analogPollMs = XSpinBox_value(&self->m_collect[5].m_spin);
    c.m_digitalPollMs = XSpinBox_value(&self->m_collect[6].m_spin);
    /* 兼容键：两台浊度的共同缺省取进水浊度仪 的值（旧服务端只认这个键）。 */
    c.m_turbidityPollMs = c.m_turbidity1PollMs;
    if (HsfLinkClient_configSetCollect(&c))
    {
        srOpBegin(self, 1);
        srStatus(self, "正在应用采集配置…");
    }
    else
        srStatus(self, "应用失败：请求未发出");
}

/**
 * @brief 应用运行保护配置（2026-11-15；进水泵空管超时 + 是否停设备）。
 * @details 与 Python 端 _apply_protect 同口径：界面用秒、协议用毫秒；
 *          需要管理员权限（保护动作会停设备）。
 */
static void srProtectApplySlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_btnProtectApply));
    HsfConfigProtect p;
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        srStatus(self, "未连接服务端");
        return;
    }
    if (!HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN)) {
        srStatus(self, "权限不足：修改运行保护需要管理员账户");
        return;
    }
    memset(&p, 0, sizeof(p));
    /* [界面用秒、协议用毫秒] 现场口径是"泵开着多久"（分钟级），秒好读。 */
    p.m_inletNoFlowTimeoutMs = XSpinBox_value(&self->m_protectTimeout) * 1000;
    p.m_inletNoFlowStop = XCheckBox_isChecked(&self->m_protectStop);
    /* [压差过大保护 2026-11-15] 界面用 kPa、协议用 MPa（÷1000 换算）。
     * [为什么不用 MPa] 桌面端只有整数微调框（无小数微调框）⇒ MPa 的
     * 0.8 表达不了；统一 kPa（800 kPa = 0.8 MPa），与 Python 端同款。 */
    p.m_pressHighEnabled = XCheckBox_isChecked(&self->m_pressHighEnable);
    p.m_pressHighLimitMpa =
        (double)XSpinBox_value(&self->m_pressHighLimit) / 1000.0;
    p.m_pressHighStop = XCheckBox_isChecked(&self->m_pressHighStop);
    if (HsfLinkClient_configSetProtect(&p)) {
        srOpBegin(self, 1);
        srStatus(self, "正在应用运行保护配置…");
    } else {
        srStatus(self, "应用失败：请求未发出");
    }
}

/** @brief 策略键（下拉项数据；缺省 ring）。 */
static const char* srPolicyKey(const HsfSettingsRemote* self)
{
    const XString* s;
    if (!self) return "ring";
    s = XComboBox_currentData(&self->m_storePolicy);
    if (!s) return "ring";
    return XString_toUtf8(s);
}

/** @brief 策略说明文字（与 Python STORAGE_POLICY_HINTS 同义）。 */
static const char* srPolicyHintText(const char* key)
{
    if (!key) return "";
    if (strcmp(key, "stop") == 0)
        return "库满后停止入库，已有数据原样保留只读。";
    if (strcmp(key, "rotate") == 0)
        return "库满后改名归档成带时间戳的分段文件并另起新库，只保留最近 N 段。";
    return "库满时删最旧的整段数据，磁盘占用封顶在上限。适合长期无人值守。";
}

/** @brief 策略变化：更新说明 + 「保留分段」只在 rotate 下可改。
 *  @param gate 权限/连接闸（connected && canCfg）；缺省 true。
 *  @details [权限闸 2026-11-12·对齐 Python pages_other.py:5227]
 *           Python：`_storage_keep.setEnabled(can_cfg and (currentData()=="rotate"))`
 *           ——「保留分段」既要有策略闸、也要有权限闸。C 端原实现只有策略
 *           闸，且 srRefresh 里它在「其余存储控件全部置灰」**之后**执行 ⇒
 *           未连接/无管理员权限时它仍可编辑：改了应用不上，用户还以为生效。
 *           ⇒ 启用条件改成「闸 ∧ rotate」。 */
static void srPolicyChanged(HsfSettingsRemote* self, bool gate)
{
    const char* key;
    if (!self) return;
    key = srPolicyKey(self);
    XLabel_setText_2(&self->m_policyHint, srPolicyHintText(key));
    srEnable((XWidget*)&self->m_storeKeep,
             gate && strcmp(key, "rotate") == 0);
}

static void srPolicySlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                        offsetof(HsfSettingsRemote, m_storePolicy));
    (void)args;
    /* [闸口径 2026-11-12] 与 srRefresh 同款闸（连接 + 管理员权限）。 */
    srPolicyChanged(self, HsfLinkClient_isReady() &&
                              HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN));
}

/** @brief 保存数据存储配置（全字段，与 Python 同键）。 */
static void srSaveSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                            offsetof(HsfSettingsRemote, m_btnSave));
    const char* path;
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        srStatus(self, "未连接服务端");
        return;
    }
    if (!HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN)) {
        srStatus(self, "权限不足：修改服务端配置需要管理员账户");
        return;
    }
    path = XLineEdit_text(&self->m_storePath);
    if (!path) path = "";
    /* [路径去首尾空白 2026-11-12·对齐 Python pages_other.py:5119]
     * Python 发前 .strip()；C 原样发 ⇒ 用户误敲的空格会原样落服务端
     * （拼出带空格的库路径）。 */
    {
        char trimmed[256];
        size_t n = 0;
        const char* p = path;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        while (*p && n + 1 < sizeof(trimmed)) trimmed[n++] = *p++;
        trimmed[n] = '\0';
        while (n > 0 && (trimmed[n - 1] == ' ' || trimmed[n - 1] == '\t' ||
                         trimmed[n - 1] == '\r' || trimmed[n - 1] == '\n'))
            trimmed[--n] = '\0';
        path = trimmed;
    }
    /* [没有收到落盘节就不写回 2026-11-10] 服务端快照里**没有** storage 节
     * （旧服务端/局部应答/解析失败）时，本页的勾选框是**初始态**（未勾选）
     * ⇒ 此时点「应用」会把 storage/enabled=0 写回服务端 ⇒ 落盘被静默关闭
     * ⇒ 历史表格从此空（现场"表格数据咋没了"）。口径：缺省值不能当成
     * 「已关闭」——没收到就不写这一节。 */
    {
        const HsfConfig* cfg = HsfLinkClient_config();
        if (!cfg || !cfg->m_valid || !cfg->m_storagePresent) {
            srStatus(self, "未收到服务端的落盘配置，已跳过（避免误关落盘）");
            return;
        }
    }
    if (HsfLinkClient_configSetStorageEx(
            XCheckBox_isChecked(&self->m_storeEnable), path,
            XSpinBox_value(&self->m_storeSample),
            XSpinBox_value(&self->m_storeMax), srPolicyKey(self),
            XSpinBox_value(&self->m_storeKeep),
            XComboBox_currentData(&self->m_storeTrigger)))
    {
        srOpBegin(self, 2);
        srStatus(self, "正在应用数据存储配置…");
    }
    else
        srStatus(self, "应用失败：请求未发出");
}

/** @brief 应用登录策略（匿名/同账并发/会话上限；需管理员）。 */
static void srLoginSaveSlot(XObject* receiver, XVarList* args)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)((char*)receiver -
                                    offsetof(HsfSettingsRemote, m_btnLoginSave));
    HsfConfigLogin login;
    (void)args;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        srStatus(self, "未连接服务端");
        return;
    }
    if (!HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN)) {
        srStatus(self, "权限不足：修改登录策略需要管理员账户");
        return;
    }
    memset(&login, 0, sizeof(login));
    login.m_allowAnonymous = XCheckBox_isChecked(&self->m_loginAnon);
    login.m_allowConcurrent = XCheckBox_isChecked(&self->m_loginConcur);
    login.m_maxSessions = XSpinBox_value(&self->m_loginMax);
    if (HsfLinkClient_configSetLogin(&login))
    {
        srOpBegin(self, 3);
        srStatus(self, "正在应用登录策略…");
    }
    else
        srStatus(self, "应用失败：请求未发出");
}

/* ==================== 回填 / 刷新 ==================== */

/** @brief 把服务端配置快照铺到控件（仅新快照到达时调用）。 */
/** @brief 双主题取色（亮/深各一套；与 HsfHomePage 的 homeThemeClr 同款）。
 *  @note  语义色取自 HsfTheme.h 的四态令牌（亮/深各一套，WCAG 已核算：
 *         正文 ≥4.5:1）。注意**不用** Python 的 #ED6C02：它在白底只有
 *         3.11:1（正文不达标），HsfTheme 定稿已弃选该值改 #A85900。 */
static uint32_t srThemeClr(uint32_t light, uint32_t dark)
{
    return HsfTheme_isDarkSkin() ? dark : light;
}

#define SR_CLR_OK      srThemeClr(HSF_COLOR_STATE_OK, \
                                  HSF_COLOR_STATE_OK_DARK)
#define SR_CLR_WARN    srThemeClr(HSF_COLOR_STATE_WARN, \
                                  HSF_COLOR_STATE_WARN_DARK)
#define SR_CLR_OFFLINE srThemeClr(HSF_COLOR_STATE_OFFLINE, \
                                  HSF_COLOR_STATE_OFFLINE_DARK)

/** @brief 给标签上文字色（**调色板通道**，Qt 正道）。
 *  @details [通道澄清 2026-11-12] 本引擎的控件自绘文本**不消费 QSS 的
 *           color 属性**（那才是不可达的那条路），但**调色板角色是通的**：
 *           XLabel.c 的 label_foregroundRole → label_color 按调色板角色
 *           取色绘制；框架也提供 XWidget_setForegroundRole（对标
 *           QWidget::setForegroundRole）。故状态上色走「改调色板
 *           WindowText 角色」——只动本控件的调色板副本，不影响其它控件。
 *           [主题切换自愈] 主题切换会重钉调色板 ⇒ 本页每拍重刷一次
 *           （setPalette 有同值早退，稳态零成本）。 */
static void srSetTextColor(XWidget* w, uint32_t argb)
{
    XPalette p;
    XColor c;
    if (!w) return;
    p = XWidget_palette(w);
    c = XColor_create_rgb((int)((argb >> 16) & 0xFFu), (int)((argb >> 8) & 0xFFu),
                          (int)(argb & 0xFFu), 0xFF);
    /* [两组都要设 2026-11-12] XLabel 取色走
     * XPalette_color(palette, XPaletteColorGroup_Current, role)，而 Current
     * 组归一化后可能落到 **Inactive** ⇒ 只设 Active 组会「改了没生效」
     * （首版实锅：截图里状态标签仍是黑字）。HsfHomePage 的
     * hsfHomeLabelSetColor 同款做法（Active + Inactive 都设）。 */
    XPalette_setColor(&p, XPaletteColorGroup_Active,
                      XPaletteColorRole_WindowText, c);
    XPalette_setColor(&p, XPaletteColorGroup_Inactive,
                      XPaletteColorRole_WindowText, c);
    XWidget_setPalette(w, &p);
    /* [前景角色必须显式钉死 2026-11-12·同 HsfTheme_applySecondaryText]
     * XLabel 的前景角色默认**按背景角色推断**（XLabel.c label_foregroundRole）：
     * 背景角色=Base ⇒ 推断成 Text 角色（不是 WindowText）⇒ 只改
     * WindowText 的颜色不生效（首版实锅：状态标签仍是黑字，截图实测）。
     * 显式钉 WindowText 后调色板颜色才真是它的文字色。 */
    XWidget_setForegroundRole(w, XPaletteColorRole_WindowText);
}

/** @brief 正文色（取自**页根**的调色板解析结果；随主题变化）。
 *  @details [值行正文色 2026-11-12·对齐 Python pages_other.py:4855/5017/
 *           5023] Python 把「服务端地址·用户·角色」「用量：…」「当前库
 *           文件：…」三行当**普通标签**（默认字号 + 正文色），只有说明段
 *           走 caption 灰。C 端原先这三行走说明行（12px 次级灰）⇒ 值比
 *           说明还小还淡，层次与 Python 相反。
 *           [为什么取页根的盘] 页根从不显式 setPalette ⇒ XWidget_palette
 *           返回**应用调色板**（XWidget.c:12115-12127），亮=黑、深=
 *           #E8EAED，正是普通标签的正文色；同时免去在本页引
 *           XGuiApplication.h（少一个模块依赖）。 */
static uint32_t srValueTextColor(HsfSettingsRemote* self)
{
    XPalette p;
    XColor c;
    if (!self) return 0xFF000000u;
    p = XWidget_palette((const XWidget*)&self->m_base);
    c = XPalette_color(&p, XPaletteColorGroup_Current,
                       XPaletteColorRole_WindowText);
    return XColor_rgba(&c);
}

static void srPopulate(HsfSettingsRemote* self)
{
    const HsfConfig* cfg;
    if (!self) return;
    cfg = HsfLinkClient_config();
    if (!cfg || !cfg->m_valid) return;

    /* [局部应答纪律 2026-11-12·对齐 Python _populate 的逐节守卫]
     * 解析层每次应答都把整个 HsfConfig 清零重填 ⇒ 局部应答（应用存储/
     * 登录/采集配置只带自己那一节）里**没带的节全是零值**。若无条件铺回
     * 控件，用户点一次「应用登录策略」就会把采集卡/存储卡/登录卡冲成
     * 缺省（周期 0、落盘关闭、用量 --），再点一次「应用」就把这些缺省
     * 写回服务端——Python 端 2026-11-10 实锅同款（「谁给的字段谁负责」）。
     * 逐节守卫口径（Python pages_other.py _populate 同款）：
     *   collect 节在 ⇒ 才刷采集卡；storage 节在 ⇒ 才刷存储卡；
     *   login 节在 ⇒ 才刷登录卡；dbPath/storageStats 任一在 ⇒ 才刷
     *   「当前库文件 / 用量」两行；auto 节在 ⇒ 才刷三模式块。 */
    if (cfg->m_collectPresent) {
    XCheckBox_setChecked(&self->m_collect[0].m_chk, cfg->m_collect.m_vfdEnabled);
    XCheckBox_setChecked(&self->m_collect[1].m_chk, cfg->m_collect.m_temphumEnabled);
    /* [逐台开关 2026-11-10] 两台浊度各显示自己的开关（旧服务端无逐台键时
     * HsfLinkConfig 已按共同键铺开，到这里就是两台同值）。 */
    XCheckBox_setChecked(&self->m_collect[2].m_chk,
                         cfg->m_collect.m_turbidity1Enabled);
    XCheckBox_setChecked(&self->m_collect[3].m_chk,
                         cfg->m_collect.m_turbidity2Enabled);
    XCheckBox_setChecked(&self->m_collect[4].m_chk, cfg->m_collect.m_flowmeterEnabled);
    XCheckBox_setChecked(&self->m_collect[5].m_chk, cfg->m_collect.m_analogEnabled);
    XCheckBox_setChecked(&self->m_collect[6].m_chk, cfg->m_collect.m_digitalEnabled);
    XSpinBox_setValue(&self->m_collect[0].m_spin, cfg->m_collect.m_vfdPollMs);
    XSpinBox_setValue(&self->m_collect[1].m_spin, cfg->m_collect.m_temphumPollMs);
    XSpinBox_setValue(&self->m_collect[2].m_spin, cfg->m_collect.m_turbidity1PollMs);
    XSpinBox_setValue(&self->m_collect[3].m_spin, cfg->m_collect.m_turbidity2PollMs);
    XSpinBox_setValue(&self->m_collect[4].m_spin, cfg->m_collect.m_flowmeterPollMs);
    XSpinBox_setValue(&self->m_collect[5].m_spin, cfg->m_collect.m_analogPollMs);
    XSpinBox_setValue(&self->m_collect[6].m_spin, cfg->m_collect.m_digitalPollMs);
    }   /* ← collect 节守卫闭合 */

    if (cfg->m_storagePresent) {
    XCheckBox_setChecked(&self->m_storeEnable, cfg->m_storage.m_enabled);
    XLineEdit_setText(&self->m_storePath, cfg->m_storage.m_path);
    XSpinBox_setValue(&self->m_storeSample, cfg->m_storage.m_sampleMs);
    XSpinBox_setValue(&self->m_storeMax, cfg->m_storage.m_maxMb);
    {
        const char* pol = cfg->m_storage.m_fullPolicy[0]
                              ? cfg->m_storage.m_fullPolicy : "ring";
        int idx = XComboBox_findData_2(&self->m_storePolicy, pol);
        if (idx < 0 && strcmp(pol, "keepOldest") == 0)   /* 旧服务端兼容。 */
            idx = XComboBox_findData_2(&self->m_storePolicy, "ring");
        XComboBox_setCurrentIndex(&self->m_storePolicy, idx >= 0 ? idx : 0);
        srPolicyChanged(self, HsfLinkClient_isReady() &&
                                  HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN));
    }
    XSpinBox_setValue(&self->m_storeKeep,
                      cfg->m_storage.m_keepFiles > 0
                          ? cfg->m_storage.m_keepFiles : 3);
    /* [入库触发 2026-11-12] always/auto → 下拉下标；未知值回 0（持续采集）。 */
    XComboBox_setCurrentIndex(&self->m_storeTrigger,
        strcmp(cfg->m_storage.m_historyTrigger, HSF_HISTORY_TRIGGER_AUTO) == 0
            ? 1 : 0);
    }   /* ← storage 节守卫闭合 */

    /* 登录策略（旧服务端不下发 login 节 ⇒ 解析出缺省值，界面照常显示）。 */
    if (cfg->m_loginPresent) {
    XCheckBox_setChecked(&self->m_loginAnon, cfg->m_login.m_allowAnonymous);
    XCheckBox_setChecked(&self->m_loginConcur, cfg->m_login.m_allowConcurrent);
    XSpinBox_setValue(&self->m_loginMax,
                      cfg->m_login.m_maxSessions > 0
                          ? cfg->m_login.m_maxSessions : 0);
    }   /* ← login 节守卫闭合 */
    /* [运行保护节 2026-11-15] 只在应答确实带它时铺（同一纪律：局部应答
     * 不带 protect ⇒ 不得把保护卡冲回缺省，那等于悄悄改了保护动作）。 */
    if (cfg->m_protectPresent) {
        self->m_protectSeen = true;
        XSpinBox_setValue(&self->m_protectTimeout,
                          cfg->m_protect.m_inletNoFlowTimeoutMs / 1000);
        XCheckBox_setChecked(&self->m_pressHighEnable,
                              cfg->m_protect.m_pressHighEnabled);
        XSpinBox_setValue(&self->m_pressHighLimit,
                          (int)(cfg->m_protect.m_pressHighLimitMpa * 1000.0));
        XCheckBox_setChecked(&self->m_pressHighStop,
                              cfg->m_protect.m_pressHighStop);
        XCheckBox_setChecked(&self->m_protectStop,
                              cfg->m_protect.m_inletNoFlowStop);
    }   /* ← protect 节守卫闭合 */
    /* [auto 节只在应答确实带它时铺回] 与 storageDbPath/storageStats 同一
     * 纪律（2026-11-10「谁给的字段谁负责」）：应用采集/落盘/登录策略的
     * 局部应答不带 auto ⇒ m_autoPresent=false ⇒ 不得把三模式块冲回缺省。 */
    if (cfg->m_auto.m_autoPresent) {
        int m;
        int i;
        for (m = 0; m < SR_AUTO_MODES; ++m) {
            const HsfConfigAutoMode* src = &cfg->m_auto.m_modes[m];
            uint32_t mask = 0u;
            for (i = 0; i < SR_AUTO_DEVS; ++i)
                if (src->m_devices[i]) mask |= (uint32_t)(1u << i);
            srAutoDevSetMask(self, m, mask);
            XSpinBox_setValue(&self->m_autoMode[m].m_rpm, src->m_stirRpm);
            XComboBox_setCurrentIndex(&self->m_autoMode[m].m_dir,
                                      src->m_stirReverse ? 1 : 0);
            /* [逐模式阀复位 2026-11-12] 回显两个开关。 */
            XCheckBox_setChecked(&self->m_autoMode[m].m_resetStop,
                                 src->m_resetValvesOnStop);
            XCheckBox_setChecked(&self->m_autoMode[m].m_resetEstop,
                                 src->m_resetValvesOnEstop);
        }
        /* [自动循环回显 2026-11-12] 只在本节确实带 cycle 时铺（老服务端
         * 不带 ⇒ 保持控件缺省，不把「没数据」说成「已关循环」）。 */
        if (cfg->m_auto.m_cyclePresent) {
            const HsfConfigAutoCycle* c = &cfg->m_auto.m_cycle;
            char txt[16];
            XCheckBox_setChecked(&self->m_cycleEnable, c->m_enabled);
            snprintf(txt, sizeof(txt), "%.2f", c->m_pressThreshold);
            XLineEdit_setText(&self->m_cyclePress, txt);
            XComboBox_setCurrentIndex(&self->m_cycleCompare,
                strcmp(c->m_pressCompare, HSF_AUTO_PRESS_COMPARE_LT) == 0 ? 1 : 0);
            XCheckBox_setChecked(&self->m_cycleFallback, c->m_timerFallback);
            XSpinBox_setValue(&self->m_cycleTimerMin, c->m_timerMinutes);
            XSpinBox_setValue(&self->m_cycleBackMin, c->m_backwashMinutes);
            XSpinBox_setValue(&self->m_cycleRinseMin, c->m_rinseMinutes);
        }
    }
    if (cfg->m_statsPresent) {
    XLabel_setText_2(&self->m_dbPath,
                     cfg->m_dbPath[0] ? cfg->m_dbPath
                                      : "未启用（落盘关闭）");
    /* 用量实况（与 Python _populate_usage 同字段同序）。 */
    {
        const HsfConfigStats* st = &cfg->m_stats;
        char buf[384];
        const double usedMb = st->m_usedMb > 0.0
                                  ? st->m_usedMb
                                  : (double)st->m_usedBytes / (1024.0 * 1024.0);
        int pct = 0;
        if (st->m_maxMb > 0) {
            pct = (int)(usedMb / (double)st->m_maxMb * 100.0 + 0.5);
            if (pct < 0) pct = 0;
            if (pct > 100) pct = 100;
            snprintf(buf, sizeof(buf),
                     "用量：已用 %.2f MB · 上限 %d MB · %d%% · 行数 %d",
                     usedMb, st->m_maxMb, pct, st->m_rows);
        } else {
            /* 上限不限 ⇒ 条恒 0%：没有"用了百分之多少"可言，硬算会误导。 */
            snprintf(buf, sizeof(buf),
                     "用量：已用 %.2f MB · 上限不限 · 行数 %d",
                     usedMb, st->m_rows);
        }
        /* [策略名词并排 2026-11-10 对齐 Python] Python 端把落盘策略也拼进这行
         * （"策略 循环覆盖"），现场一眼看出库满了会怎样（循环覆盖/停写/轮转）。 */
        {
            const char* pol = st->m_policy;
            const char* polText = NULL;
            if (pol && pol[0]) {
                if (strcmp(pol, "ring") == 0) polText = "循环覆盖";
                else if (strcmp(pol, "stop") == 0) polText = "停写";
                else if (strcmp(pol, "rotate") == 0) polText = "轮转分段";
                else polText = pol;
            }
            if (polText) {
                char extra[64];
                snprintf(extra, sizeof(extra), " · 策略 %s", polText);
                strncat(buf, extra, sizeof(buf) - strlen(buf) - 1);
            }
        }
        if (st->m_segments > 0) {
            char extra[64];
            snprintf(extra, sizeof(extra), " · 分段 %d", st->m_segments);
            strncat(buf, extra, sizeof(buf) - strlen(buf) - 1);
        }
        if (st->m_droppedRows > 0) {
            char extra[80];
            snprintf(extra, sizeof(extra), " · 已循环丢弃 %d 行",
                     st->m_droppedRows);
            strncat(buf, extra, sizeof(buf) - strlen(buf) - 1);
        }
        if (st->m_paused) {
            char extra[192];
            snprintf(extra, sizeof(extra), "\n[已停写] %s", st->m_pauseReason);
            strncat(buf, extra, sizeof(buf) - strlen(buf) - 1);
        }
        XLabel_setText_2(&self->m_storageUse, buf);
        /* [停写警示上色 2026-11-12·对齐 Python pages_other.py:5485-5494]
         * Python 把「[已停写] …」整行刷成警告色；C 端同一行文字用警告色
         * （停写是异常态，要显眼）。**未停写时是正文色**（Python 同款：
         * 非停写时该行就是普通标签，不走 caption 灰）。
         * [为什么上色挪到 srRefresh 每拍] 一旦 setPalette 钉过，控件就
         * 不再解析应用调色板（XWidget.c:12115 m_paletteSet）⇒ 从停写态
         * 恢复必须**显式**钉回正文色，且主题切换后要重钉 ⇒ 统一在
         * srRefresh 每拍按当前应用盘刷一次（setPalette 有同值早退，稳态
         * 零脏区；与 m_mode/m_auto 状态色同款自愈口径）。 */
        self->m_storagePaused = st->m_paused;
        /* 容量条：上限已知才给百分比；未启用落盘（无统计）时清 0。 */
        XProgressBar_setValue(&self->m_storageBar, pct);
    }       /* ← 用量统计块闭合 */
    }       /* ← 统计节守卫闭合 */
}           /* ← srPopulate 闭合 */

/** @brief 巡检：模式/自动标签 + 权限门 + 按序号回填/提示。 */
static void srRefresh(HsfSettingsRemote* self)
{
    const bool connected = HsfLinkClient_isReady();
    const bool canMode = HsfLinkClient_can(HSF_PERM_MODE_SWITCH);
    const bool canAuto = HsfLinkClient_can(HSF_PERM_AUTO_CONTROL);
    const bool canCfg = HsfLinkClient_can(HSF_PERM_ACCOUNT_ADMIN);
    int autoMode = 0;
    int running = 0;
    bool modeKnown;

    if (!self) return;
    modeKnown = HsfLinkClient_runMode(&autoMode);

    /* [进页重拉配置 2026-11-12·对齐 Python on_enter → _fetch_config(silent)]
     * Python 每次进本页都静默 GET 一次（别人在别处改过配置，本页要看到
     * 最新值）。C 端页面**常驻**（建页即建、切页只切显隐），没有 on_enter
     * 钩子 ⇒ 用「可见性上升沿」等效：刚变为可见 ⇒ 静默重拉一次；隐藏时
     * 复位标记（下次进页再触发）。
     * [为什么不每拍拉] 每拍 GET 会把用户正在编辑的控件值冲掉（快照回填），
     * 且浪费带宽——只在进页那一刻拉一次，与 Python 同口径。 */
    {
        const bool visible = XWidget_isVisible((XWidget*)&self->m_base);
        if (visible && !self->m_wasVisible) {
            self->m_wasVisible = true;
            if (connected)
                (void)srFetchConfig(self, true);
        } else if (!visible) {
            self->m_wasVisible = false;
        }
    }

    /* 运行模式。[状态上色 2026-11-12·对齐 Python pages_other.py:5261-5269]
     * 自动=警告琥珀（远程控制被禁用，要留神）／手动=正常绿（可远程操作）／
     * 未知=离线灰。走调色板通道（见 srSetTextColor 的通道澄清）。 */
    XLabel_setText_2(&self->m_mode,
                     !modeKnown ? "未知"
                     : autoMode ? "自动（远程控制已禁用）"
                                : "手动（允许远程操作）");
    srSetTextColor((XWidget*)&self->m_mode,
                   !modeKnown ? SR_CLR_OFFLINE
                   : autoMode ? SR_CLR_WARN
                              : SR_CLR_OK);

    /* 自动运行：手动 / 自动未运行 / 自动运行中 / 未知。
     * [状态上色 2026-11-12] Python 口径：运行中=警告琥珀（设备正在按逻辑
     * 动作）、其余（手动/已停止/状态未知）=灰。 */
    if (!modeKnown)
        XLabel_setText_2(&self->m_auto, "未知");
    else if (!autoMode)
        XLabel_setText_2(&self->m_auto, "手动模式（自动运行不可用）");
    else if (!HsfLinkClient_autoRunning(&running))
        XLabel_setText_2(&self->m_auto, "自动模式 · 运行状态未知");
    else
        XLabel_setText_2(&self->m_auto,
                         running ? "自动模式 · 运行中"
                                 : "自动模式 · 已停止");
    srSetTextColor((XWidget*)&self->m_auto,
                   (modeKnown && autoMode && running)
                       ? SR_CLR_WARN
                       : SR_CLR_OFFLINE);

    /* 连接信息。 */
    {
        char buf[256];
        if (connected)
            snprintf(buf, sizeof(buf), "服务端 %s · 用户 %s · 角色 %s",
                     HsfLinkClient_serverAddress(),
                     HsfLinkClient_user(), HsfLinkClient_roleName());
        else
            snprintf(buf, sizeof(buf), "未连接");
        XLabel_setText_2(&self->m_conn, buf);
    }
    /* [用量行上色每拍重钉 2026-11-12] 停写=警告色、否则=正文色（Python
     * pages_other.py:5485-5494 同口径）。每拍按当前盘重钉：控件一旦
     * 钉过调色板就不再解析应用盘（XWidget.c:12115）⇒ 主题切换/停写态
     * 切换都要靠这次重钉自愈（setPalette 同值早退，稳态零脏区）。 */
    srSetTextColor((XWidget*)&self->m_storageUse,
                   self->m_storagePaused ? SR_CLR_WARN
                                         : srValueTextColor(self));

    /* 权限门（界面只做可达性；服务端逐帧强制）。 */
    srEnable((XWidget*)&self->m_btnManual,
             connected && canMode && modeKnown && autoMode);
    srEnable((XWidget*)&self->m_btnAuto,
             connected && canMode && modeKnown && !autoMode);
    {
        const bool ctrl = connected && canAuto && modeKnown && autoMode;
        srEnable((XWidget*)&self->m_btnAutoStart, ctrl && !(running && 1));
        srEnable((XWidget*)&self->m_btnAutoStop, ctrl && running);
        /* [急停/复位 2026-11-11] 停机类动作服务端永远放行 ⇒ 不按自动模式/
         * 运行位收窄，只按「连接 + AutoControl 权限」把关（急停要能在
         * 任何授权态按下去；复位同理——锁存态下启动钮已灰，复位是出路）。 */
        srEnable((XWidget*)&self->m_btnEstop, connected && canAuto);
        srEnable((XWidget*)&self->m_btnReset, connected && canAuto);
    }
    srEnableAuto(self, connected && canCfg);
    srSyncAutoHzHint(self);
    srEnable((XWidget*)&self->m_btnCfgRefresh, connected);
    srEnableCollect(self, connected && canCfg);
    srEnable((XWidget*)&self->m_storeEnable, connected && canCfg);
    srEnable((XWidget*)&self->m_storePath, connected && canCfg);
    srEnable((XWidget*)&self->m_storeSample, connected && canCfg);
    srEnable((XWidget*)&self->m_storeMax, connected && canCfg);
    srEnable((XWidget*)&self->m_storePolicy, connected && canCfg);
    srEnable((XWidget*)&self->m_storeTrigger, connected && canCfg);
    srEnable((XWidget*)&self->m_btnSave, connected && canCfg);
    srEnable((XWidget*)&self->m_loginAnon, connected && canCfg);
    srEnable((XWidget*)&self->m_loginConcur, connected && canCfg);
    srEnable((XWidget*)&self->m_loginMax, connected && canCfg);
    srEnable((XWidget*)&self->m_btnLoginSave, connected && canCfg);
    srEnable((XWidget*)&self->m_protectStop, connected && canCfg);
    srEnable((XWidget*)&self->m_protectTimeout, connected && canCfg);
    /* [2026-11-15 审计·对齐 Python] 「应用」要等**第一次 protect 快照
     * 到达**：没收到就按控件缺省值下发 ⇒ 把服务端的保护配置静默改成缺省
     * （保护被关掉）。Python 端同款（_protect_seen）。 */
    srEnable((XWidget*)&self->m_btnProtectApply,
             connected && canCfg && self->m_protectSeen);
    srEnable((XWidget*)&self->m_pressHighEnable, connected && canCfg);
    srEnable((XWidget*)&self->m_pressHighLimit, connected && canCfg);
    srEnable((XWidget*)&self->m_pressHighStop, connected && canCfg);
    srPolicyChanged(self, connected && canCfg);   /* 保留分段：rotate 且有权才可改。 */

    /* 配置快照到达 → 回填。 */
    {
        const int gseq = HsfLinkClient_configGetSeq();
        if (gseq != self->m_seenGetSeq) {
            self->m_seenGetSeq = gseq;
            srPopulate(self);
            /* [例行消息不覆盖操作终态 2026-11-11] 写完配置后服务端会推一
             * 次新快照 ⇒ 「配置已刷新」会把「配置已保存」盖掉 ⇒ 用户只看到
             * 一句无关的话，看不出保存成没成。⇒ 刚写过终态时跳过本条。 */
            if (!self->m_opMsgSticky)
                srStatus(self, "配置已刷新");
        }
    }
    /* 配置写应答 → 提示 + 回拉一次（服务端可能夹取数值）。
     * [auto 节 2026-11-11] opKind=10（自动模式配置）同走本路：成功文案
     * 与采集/落盘区分，失败原文（未知配置节/模式校验拒绝）进状态栏。 */
    {
        const int wseq = HsfLinkClient_configSeq();
        if (wseq != self->m_seenWriteSeq) {
            const int kind = self->m_opKind;
            self->m_seenWriteSeq = wseq;
            if ((kind >= 1 && kind <= 3) || kind == 10)
                self->m_opKind = 0;   /* 在途的「配置写」已有终态 */
            self->m_opMsgSticky = true;
            if (HsfLinkClient_configOk()) {
                /* [成功文案分节 2026-11-12·对齐 Python pages_other.py:5101/
                 * 5131/5151] Python 三节各有各的终态（采集=开关即时/周期下一
                 * 拍、存储=含容量上限与满额策略、登录=下次登录起），C 端原来
                 * 统一一句「配置已保存」⇒ 用户看不出到底哪一节生效了。 */
                const char* okText = "配置已保存（以服务端回显为准）";
                if (kind == 1)
                    okText = "采集配置已生效（开关即时，周期下一拍）";
                else if (kind == 2)
                    okText = "数据存储配置已生效（含容量上限与满额策略）";
                else if (kind == 3)
                    okText = "登录策略已生效（下次登录起）";
                else if (kind == 10)
                    /* [文案 2026-11-12·逐字对齐 Python _apply_auto 成功态
                     *（pages_other.py:5409「自动配置已生效（三个模式 +
                     * 自动循环）」）。] */
                    okText = "自动配置已生效（三个模式 + 自动循环）";
                srStatus(self, "%s", okText);
                (void)HsfLinkClient_configGet();
            } else {
                srStatus(self, kind == 10 ? "应用失败：%s" : "保存失败：%s",
                         HsfLinkClient_configError());
            }
        }
    }
    /* 自动运行应答 → 提示（比"已发送"更准确）。
     * [跨页命令应答隔离 2026-11-12·对齐 Python 每请求独立闭包]
     * HsfLinkClient_commandSeq() 是**全局**命令序号：变频器/数字量/阀/泵/
     * 自动控制的命令应答都推进它。而本页定时器建页即启、切页不停（页面
     * 常驻），用户在别页操作设备时，本页会把**别人的**应答当成自己的，
     * 追加一句「操作已受理 / 操作失败：…」盖掉原页更有信息量的终态
     * （现场症状：换个页面操作设备，这页状态栏冒出一句无关的话）。
     * ⇒ 只有本页在途（m_opKind 6..9 = 启动/停止/急停/复位）时才出提示；
     *   其余情况只静默记住序号推进（m_seenCmdSeq），不碰状态栏。 */
    {
        const int cseq = HsfLinkClient_commandSeq();
        if (cseq != self->m_seenCmdSeq) {
            const int kind = self->m_opKind;
            self->m_seenCmdSeq = cseq;
            if (kind >= 6 && kind <= 9) {
                self->m_opKind = 0;   /* 在途的「自动运行启停/急停/复位」已有终态 */
                if (HsfLinkClient_lastCommandError()[0])
                    srStatus(self, "操作失败：%s",
                             HsfLinkClient_lastCommandError());
                else
                    srStatus(self, "操作已受理");
                self->m_opMsgSticky = true;
            }
        }
    }
    /* 模式切换失败原因（RUN_MODE 应答 err）。 */
    {
        const char* err = HsfLinkClient_lastModeError();
        if (err && err[0] && strcmp(err, self->m_lastModeErr) != 0) {
            snprintf(self->m_lastModeErr, sizeof(self->m_lastModeErr), "%s",
                     err);
            srStatus(self, "切换失败：%s", err);
            self->m_opMsgSticky = true;
            if (self->m_opKind == 4 || self->m_opKind == 5)
                self->m_opKind = 0;
        }
    }
    /* 模式切换**成功**的终态：目标态已生效（回包里没有单独的 ok 键，靠
     * 模式值回落判定）——否则「正在切换到自动模式…」也可能永远停着。 */
    if (self->m_opKind == 4 || self->m_opKind == 5) {
        int cur = 0;
        if (modeKnown && HsfLinkClient_runMode(&cur)) {
            const bool wantAuto = (self->m_opKind == 5);
            if (wantAuto == (cur != 0)) {
                srStatus(self, "已切换到%s模式", wantAuto ? "自动" : "手动");
                self->m_opMsgSticky = true;
                self->m_opKind = 0;
            }
        }
    }
}

/** @brief 巡检定时器。 */
static void srTimerCb(void* userData, XTimerData* timer)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)userData;
    (void)timer;
    if (!self) return;
    if (!HsfLinkClient_isReady()) {
        self->m_fetched = false;   /* 断开后复位：重连首拍补拉。 */
    } else if (!self->m_fetched) {
        self->m_fetched = true;
        srFetchConfig(self, true);
    } else {
        /* [自愈 2026-11-10 板端实锅] 「拉过一次」不等于「拿到过快照」：
         * 板端客户端与服务端同时启动，首发的 0x0116 若撞在会话就绪/推送
         * 洪峰的窗口里可能丢掉 ⇒ 控件永远停在**出厂缺省**（全部未勾、
         * 周期 200ms），而服务端其实一直在按 INI 采集 —— 现场看到的就是
         * 「勾没打上、间隔改不了」。故：**没有有效快照就继续拉**，拿到后
         * 立刻停（否则每拍新快照都会覆盖用户正在改的值）。 */
        const HsfConfig* cfg = HsfLinkClient_config();
        if (!cfg || !cfg->m_valid)
            (void)srFetchConfig(self, true);
    }
    srRefresh(self);
    /* 在途操作兜底：回包迟迟不来 ⇒ 明确提示，不再让状态栏停在「正在…」。 */
    srOpPoll(self);
}

/* ==================== 创建 ==================== */

/* ---- 本页字号基准常量（建页折算与 srApplyFonts 重放共用同一份；
 *      基准=源码常量、pct 现读现算，全局改动经 reapplyAllFonts 回放） ---- */
#define SR_TITLE_FONT_PX  16  /**< 页标题「远端设置」。 */
#define SR_MODE_FONT_PX   24  /**< 运行模式大字（对齐 Python 24px 粗体）。 */
#define SR_AUTO_FONT_PX   20  /**< 自动运行状态行（对齐 Python 20px）。 */
#define SR_HEAD_FONT_PX   14  /**< 分区标题（匿名 malloc 标签）。 */
#define SR_HINT_FONT_PX   12  /**< 说明小字。 */
#define SR_CTRL_FONT_PX   16  /**< 按钮/输入控件文本（默认渲染口径）。 */
#define SR_CTRL_H         34  /**< 按钮定高（随字号重放）。 */

/** @brief 给控件挂提示框（UTF-8 字面 → 临时 XString；set 完即删）。
 *  @note  XWidget_setToolTip 收 XString 借用指针（字面包装临时对象，
 *         set 完即删，无堆残留）。现仅数据存储卡两处在用（m_storeMax/
 *         m_storeKeep）；自动模式配置卡的提示已按用户定案删除
 *         （2026-11-13）。 */
static void srSetTip(XWidget* w, const char* text)
{
    XString* tip;
    if (!w) return;
    tip = text ? XString_create_utf8(text) : NULL;
    XWidget_setToolTip(w, tip);
    if (tip) XClassDelete((XClass*)tip);
}

/** @brief 建一个说明标签（小字、自动换行；字号按本页合比折算）。
 *  @details [字号重放 2026-11-12] 说明/状态标签此前**只**在建页时折算一次，
 *           没进 hsfUi_reapplyTaggedFonts 的重放通道（hsfUi_tagFontBase 标
 *           基准）⇒ 全局字号缩放后这些行保持建页字号、与周边控件脱节。
 *           本函数补 tag（幂等，锚点与其它已 tag 控件同源）。 */
static void srInitHint(XLabel* label, XWidget* parent, const char* text)
{
    XLabel_init(label, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)label);
    XLabel_setTextPixelSize(label,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE, SR_HINT_FONT_PX));
    hsfUi_tagFontBase((XWidget*)label, SR_HINT_FONT_PX);
    HsfTheme_applySecondaryText((XWidget*)label);
    XLabel_setWordWrap(label, true);
    XLabel_setText_2(label, text);
    XWidget_show((XWidget*)label);
}

/** @brief 建一个「值行」标签（正文色 + 自动换行；**不是**说明小字）。
 *  @details [值行 vs 说明行 2026-11-12·对齐 Python pages_other.py:4855/
 *           5017/5023] Python 的连接信息 / 用量 / 当前库文件三行是**普通
 *           标签**（默认字号、正文色），只有说明段（hint）走 caption 灰。
 *           C 端原先这三行走 srInitHint（12px 次级灰）⇒ 值比说明还小还
 *           淡，层次与 Python 相反（本轮对齐的重点之一）。
 *           本入口：**不钉调色板** ⇒ 运行时按应用调色板解析正文色（主题
 *           切换自动跟随，无需重钉）；字号走 SR_CTRL_FONT_PX（与本页其它
 *           值行/控件文字同档），并挂 fontBase 基准供全局/逐页缩放重放。 */
static void srInitValue(XLabel* label, XWidget* parent, const char* text)
{
    XLabel_init(label, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)label);
    XLabel_setTextPixelSize(label,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE, SR_CTRL_FONT_PX));
    hsfUi_tagFontBase((XWidget*)label, SR_CTRL_FONT_PX);
    XLabel_setWordWrap(label, true);
    XLabel_setText_2(label, text);
    XWidget_show((XWidget*)label);
}

/** @brief 建一个按钮并接到槽（字号按本页合比折算，定高随字号重放）。 */
static void srInitButton(XPushButton* btn, XWidget* parent, const char* text,
                         XObject* receiver, XSlotFunc1 slot)
{
    XPushButton_init(btn, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)btn);
    {
        XFont f = XWidget_font((XWidget*)btn);
        XFont_setPixelSize(&f,
            hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE, SR_CTRL_FONT_PX));
        XWidget_setFont((XWidget*)btn, &f);
        XClassDeinit(&f);
    }
    XPushButton_setText_2(btn, text);
    /* [2026-11-17 合并裁定] 本文件在本地提交与远端**两边都是旧版**
     * （无 HsfMultiSelect / 自动模式配置 / 按页字号折算），合并后的
     * 新版整体来自本地未提交的 WIP ⇒ 冲突里"远端侧"的 `34` 是**上一代
     * 函数**的字面量，搬进新一代（字号按本页折算）函数里属跨代拼接：
     * 字号放大后按钮定高不跟涨 ⇒ 文字被裁。故此处取新世代口径
     * （随字号折算），与同文件 re-apply 路径 `(SR_CTRL_H*pct+50)/100`
     * 同一基准。其余冲突一律按远端。 */
    XWidget_setFixedHeight((XWidget*)btn,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE, SR_CTRL_H));
    XObject_connect((XObject*)btn, XSignal(XAbstractButton_clicked_signal),
                      receiver, slot, XConnectionType_Direct);
    XWidget_show((XWidget*)btn);
}

/** @brief 分区标题（匿名 malloc 标签）字号折算 + 基准标注。
 *  @details 无结构体字段 → 重放走 hsfUi_reapplyTaggedFonts（按
 *           hsfFontBase 动态属性现读现算，幂等锚点同源）。
 *           [两种用法 2026-11-12] ① **卡标题**（运行模式/自动运行/
 *           连接信息/采集配置/数据存储/登录策略——对齐 Python 每张 Card
 *           都有标题，见 pages_other.py:4683 起）；② 采集配置网格的
 *           表头单元格（设备/采集/周期，与 Python QGridLayout 第 0 行
 *           同位）。二者同款样式，不另立视觉语言。 */
static void srInitHead(XLabel* head, XWidget* parent, const char* text)
{
    XLabel_init(head, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)head);
    XLabel_setText_2(head, text);
    XLabel_setTextPixelSize(head,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE, SR_HEAD_FONT_PX));
    hsfUi_tagFontBase((XWidget*)head, SR_HEAD_FONT_PX);
    XWidget_show((XWidget*)head);
}

/** @brief 给控件加粗（对齐 Python 运行模式状态标签的 font-weight:bold）。
 *  @details [字重通道 2026-11-12] 本引擎控件自绘文本不吃 QSS 的
 *           font-weight，但 **XFont 的 weight 通道是通的**（XFont_setBold
 *           → XFont_setWeight(700)），与 HsfHomePage 的取值标签
 *           （homePage 行）同款做法：取控件当前字体副本 → 加粗 → 回写。
 *           [与字号折算的次序] XLabel_setTextPixelSize 是**沿用当前字体**
 *           只改像素高（XLabel.c:3583 先 copy 源字体再 setPixelSize），
 *           故「先加粗、后设字号」粗体不被冲掉；字号重放（srApplyFonts）
 *           同样只改像素高 ⇒ 粗体常驻，主题切换/全局缩放都不丢。 */
static void srApplyBold(XWidget* w)
{
    XFont f;
    if (!w) return;
    f = XWidget_font(w);
    XFont_setBold(&f, true);
    XWidget_setFont(w, &f);
    XClassDeinit(&f);
}

/** @brief 行说明标签（匿名 malloc 标签）字号折算 + 基准标注（基准 16）。 */
static void srInitRowName(XLabel* name, XWidget* parent, const char* text)
{
    XLabel_init(name, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)name);
    XLabel_setText_2(name, text);
    XLabel_setTextPixelSize(name,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE, SR_CTRL_FONT_PX));
    hsfUi_tagFontBase((XWidget*)name, SR_CTRL_FONT_PX);
    XWidget_show((XWidget*)name);
}

/** @brief 本页字号实时重放（合比 pct 由 hsfUi_reapplyPageFonts 传入）。 */
static void srApplyFonts(XWidget* page, int pct);

/** @brief 控件文本字号 × 合比直写（按钮/输入/勾选/微调/下拉通用）。 */
static void srApplyCtrlFont(XWidget* w, int pct)
{
    XFont f;
    if (!w || pct <= 0) return;
    f = XWidget_font(w);
    XFont_setPixelSize(&f, (SR_CTRL_FONT_PX * pct + 50) / 100);
    XWidget_setFont(w, &f);
    XClassDeinit(&f);
}

/** @brief 自动模式块行标签的定宽（对齐 Python setMinimumWidth(110)）。
 *  @details [为什么不写死 110] 页面字号可缩放 50~200%：写死像素在放大后
 *           标签列不跟着变 ⇒「启动设备 / 搅拌电机」两列不再成列、右侧控件
 *           被挤到别处。这里按**标签自己的字体**实测文字宽
 *           （XPainter_textWidth），再与 110（按合比折算）取大者 ⇒ 既守住
 *           Python 的 110 基线，又不写死像素（本仓库「定宽标签按当前字体
 *           实测」的规矩）。 */
static int srAutoCapWidth(const XLabel* cap, int pct)
{
    XFont font;
    const XString* txt;
    const char* utf8;
    int w = 0;
    int base;
    if (!cap) return 0;
    font = XWidget_font((const XWidget*)cap);
    txt = XLabel_text(cap);          /* 借用指针，不释放。 */
    utf8 = txt ? XString_toUtf8(txt) : NULL;
    if (utf8) w = XPainter_textWidth(&font, utf8);
    XClassDeinit(&font);
    w += 8;                          /* 余量：标签文字别贴着右侧控件。 */
    base = (SR_AUTO_CAP_MIN_W * pct + 50) / 100;
    return (w > base) ? w : base;
}

/** @brief 某模式块「方向」下拉的定宽（按**最终字体**实测；随字号折算）。
 *  @details 不设宽度时下拉框按内容 sizeHint 收得很窄 ⇒ 项文字被省略成
 *           「…」（截图实见「方向 ···」）。Python 端 QComboBox 宽度由最长项
 *           + 箭头自然撑开；本端按 XPainter_textWidth 实测最长项（「正转」
 *           与「反转」同宽）+ 箭头与内边距，并与变频器页 dir_combo 的
 *           100px 基线取大者（同一「方向下拉」不出现两种宽度口径）。
 *  @note  必须在字号落定之后调用（否则量的是旧字体）。 */
static void srAutoDirMetrics(HsfSettingsRemote* self, int m, int pct)
{
    XFont font;
    int w = 0;
    int base;
    if (!self || m < 0 || m >= SR_AUTO_MODES) return;
    font = XWidget_font((const XWidget*)&self->m_autoMode[m].m_dir);
    w = XPainter_textWidth(&font, "正转");
    XClassDeinit(&font);
    w += (SR_AUTO_DIR_PAD * pct + 50) / 100;   /* 下拉箭头 + 左右内边距。 */
    base = (SR_AUTO_DIR_MIN_W * pct + 50) / 100;
    if (w < base) w = base;
    XWidget_setMinimumWidth((XWidget*)&self->m_autoMode[m].m_dir, w);
}

/** @brief 自动循环「压差阈值」输入框的最小宽（按字体实测「0.00」+内边距）。
 *  @details [不写死宽] 页面字号 50~200% 可缩放、窗口宽也可变：写死像素
 *           放大后裁字、窄屏又占死横向空间。只给**下界**（装得下最宽
 *           合法输入），超出的由行内 stretch/布局分配。 */
static void srCyclePressMetrics(HsfSettingsRemote* self, int pct)
{
    XFont f;
    int w;
    if (!self || pct <= 0) return;
    f = XWidget_font((const XWidget*)&self->m_cyclePress);
    w = XPainter_textWidth(&f, "0.00") + 24;
    XClassDeinit(&f);
    if (w < (SR_CYCLE_PRESS_MIN_W * pct + 50) / 100)
        w = (SR_CYCLE_PRESS_MIN_W * pct + 50) / 100;
    XWidget_setMinimumWidth((XWidget*)&self->m_cyclePress, w);
}

/** @brief 自动循环「比较」下拉的最小宽（按字体实测最长项 + 箭头内边距）。 */
static void srCycleCmpMetrics(HsfSettingsRemote* self, int pct)
{
    XFont f;
    int w;
    if (!self || pct <= 0) return;
    f = XWidget_font((const XWidget*)&self->m_cycleCompare);
    /* [实测用最长项] 2026-11-12 项文案对齐 Python 后最长项是 7 字
     *（「大于阈值时切换」），按它量宽防省略号。 */
    w = XPainter_textWidth(&f, "大于阈值时切换") + 44;
    XClassDeinit(&f);
    if (w < (SR_CYCLE_CMP_MIN_W * pct + 50) / 100)
        w = (SR_CYCLE_CMP_MIN_W * pct + 50) / 100;
    XWidget_setMinimumWidth((XWidget*)&self->m_cycleCompare, w);
}

/** @brief 某模式块的多选钮尺寸（高度/最小宽/最大宽；随字号折算）。
 *  @details Python 端 devices.setMinimumWidth(150) /
 *           setMaximumWidth(420)：不设上限会一路铺满整行（截图实见），
 *           420 装得下「电动球阀1、电动球阀2、进水泵」这类常见组合，
 *           全选时收敛成短文案「全部设备」不占宽。 */
static void srAutoDevSelMetrics(HsfSettingsRemote* self, int m, int pct)
{
    XWidget* btn;
    if (!self || m < 0 || m >= SR_AUTO_MODES) return;
    btn = HsfMultiSelect_button(&self->m_autoMode[m].m_devSel);
    if (!btn) return;
    XWidget_setFixedHeight(btn, (SR_CTRL_H * pct + 50) / 100);
    XWidget_setMinimumWidth(btn, (SR_AUTO_SEL_MIN_W * pct + 50) / 100);
    XWidget_setMaximumWidth(btn, (SR_AUTO_SEL_MAX_W * pct + 50) / 100);
}

/** @brief 某模式块 rpm 微调框的最小宽（含后缀 sizeHint，按**当前字体**
 *         实测；建页与字号重放两处都必须调）。
 *  @details [首位被裁根修 2026-11-13] 框架 XAbstractSpinBox 的
 *           minimumSizeHint 按 Qt 契约**不含后缀**（"minimumSizeHint
 *           doesn't care about the suffix"）⇒ 行内空间不足时布局可把框
 *           压到 minimumSizeHint（<「1440 rpm」全宽）⇒ 内嵌编辑框按光标
 *           横向滚动保后缀、裁前导数字（实测「1440 rpm」显示成
 *           「440 rpm」）。最小宽取控件**自身 sizeHint**（探针=
 *           max(min,max) 数值 + 后缀 + chromeW，全字体实测、不写死像素）
 *           ⇒ 布局收缩下界抬到全文本宽，行内余量分配、可放大不受限
 *           （宽只给下界，与 srCyclePressMetrics 同款口径）。
 *  @note  必须在**字号落定之后**调用（FONT_CHANGE 已失效两枚提示缓存
 *         ⇒ sizeHint 查询即按新字体重算）。 */
static void srAutoRpmMetrics(HsfSettingsRemote* self, int m)
{
    if (!self || m < 0 || m >= SR_AUTO_MODES) return;
    XWidget_setMinimumWidth((XWidget*)&self->m_autoMode[m].m_rpm,
                            XWidget_sizeHint(
                                (XWidget*)&self->m_autoMode[m].m_rpm).width);
}

/** @brief 某模式块的标签列重定宽（字号落定后按新字实测；两行共用一宽）。
 *  @details [为什么两行必须同宽] Python 端给 lbl1/lbl2 各设 minWidth(110)
 *           就是为了**两行成列**（行1 的控件起点 = 行2 的控件起点）。本端
 *           按实测取「两个标签里更宽的那个」，两行写成同一个值 ⇒ 无论字号/
 *           文案怎么变，「启动设备」与「搅拌电机」两行的控件起点永远对齐。
 *  @note  必须在**字号落定之后**调用（否则量的是旧字体）。 */
static void srAutoCapMetrics(HsfSettingsRemote* self, int m, int pct)
{
    int w1;
    int w2;
    int w;
    if (!self || m < 0 || m >= SR_AUTO_MODES) return;
    w1 = srAutoCapWidth(&self->m_autoMode[m].m_devCap, pct);
    w2 = srAutoCapWidth(&self->m_autoMode[m].m_stirCap, pct);
    w = (w1 > w2) ? w1 : w2;
    XWidget_setMinimumWidth((XWidget*)&self->m_autoMode[m].m_devCap, w);
    XWidget_setMinimumWidth((XWidget*)&self->m_autoMode[m].m_stirCap, w);
}

/** @brief 本页字号实时重放实现（源码基准 16/24/20/14/12/34）。 */
static void srApplyFonts(XWidget* page, int pct)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)page;
    int i;
    int m;
    if (!self || pct <= 0) return;
    XLabel_setTextPixelSize(&self->m_title,
                            (SR_TITLE_FONT_PX * pct + 50) / 100);
    XLabel_setTextPixelSize(&self->m_mode,
                            (SR_MODE_FONT_PX * pct + 50) / 100);
    XLabel_setTextPixelSize(&self->m_auto,
                            (SR_AUTO_FONT_PX * pct + 50) / 100);
    /* 按钮（定高随字号）。 */
    XWidget_setFixedHeight((XWidget*)&self->m_btnManual,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnAuto,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnAutoStart,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnAutoStop,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnEstop,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnReset,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnCfgRefresh,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnCfgApply,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnSave,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnLoginSave,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnAutoApply,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_btnProtectApply,
                           (SR_CTRL_H * pct + 50) / 100);
    /* [库路径框 / 策略下拉定高补重放 2026-11-12·对齐 Python 高度交布局]
     * 这两个控件建页时定高 34，但 srApplyFonts 只重放了按钮 ⇒ 字号放大
     * 150~200% 后字体变大、框高不变 ⇒ 竖向裁字。Python 端 QLineEdit/
     * QComboBox 高度全交布局（无定高）。 */
    XWidget_setFixedHeight((XWidget*)&self->m_storePath,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_storePolicy,
                           (SR_CTRL_H * pct + 50) / 100);
    srApplyCtrlFont((XWidget*)&self->m_btnManual, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnAuto, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnAutoStart, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnAutoStop, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnEstop, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnReset, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnCfgRefresh, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnCfgApply, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnSave, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnLoginSave, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnAutoApply, pct);
    /* 运行保护卡（勾选 + 时长框）。 */
    srApplyCtrlFont((XWidget*)&self->m_protectStop, pct);
    srApplyCtrlFont((XWidget*)&self->m_protectTimeout, pct);
    srApplyCtrlFont((XWidget*)&self->m_btnProtectApply, pct);
    srApplyCtrlFont((XWidget*)&self->m_pressHighEnable, pct);
    srApplyCtrlFont((XWidget*)&self->m_pressHighLimit, pct);
    srApplyCtrlFont((XWidget*)&self->m_pressHighStop, pct);
    /* 采集行（勾选 + 微调框）。 */
    for (i = 0; i < SR_COLLECT_ROWS; ++i) {
        srApplyCtrlFont((XWidget*)&self->m_collect[i].m_chk, pct);
        srApplyCtrlFont((XWidget*)&self->m_collect[i].m_spin, pct);
    }
    /* 自动模式配置卡（三模式块：设备多选/转速/方向/折算提示）。 */
    for (m = 0; m < SR_AUTO_MODES; ++m) {
        srApplyCtrlFont(HsfMultiSelect_button(&self->m_autoMode[m].m_devSel),
                        pct);
        srApplyCtrlFont((XWidget*)&self->m_autoMode[m].m_rpm, pct);
        /* 最小宽=含后缀 sizeHint（防行内挤压裁首位；根修注记见
         * srAutoRpmMetrics——必须在字号落定后调，此处 srApplyCtrlFont
         * 的 FONT_CHANGE 已失效提示缓存）。 */
        srAutoRpmMetrics(self, m);
        srApplyCtrlFont((XWidget*)&self->m_autoMode[m].m_dir, pct);
        srApplyCtrlFont((XWidget*)&self->m_autoMode[m].m_resetStop, pct);
        srApplyCtrlFont((XWidget*)&self->m_autoMode[m].m_resetEstop, pct);
        XWidget_setFixedHeight((XWidget*)&self->m_autoMode[m].m_dir,
                               (SR_CTRL_H * pct + 50) / 100);
        XLabel_setTextPixelSize(&self->m_autoMode[m].m_hzHint,
                                (SR_HINT_FONT_PX * pct + 50) / 100);
        srAutoDevSelMetrics(self, m, pct);
        srAutoDirMetrics(self, m, pct);
        /* 注：两行的标签（m_devCap/m_stirCap）也带 hsfFontBase 标记 ⇒
         * 字号由下面的 hsfUi_reapplyTaggedFonts 统一重放；它们的**列宽**
         * 必须在字号落定后才量 ⇒ 见循环后的 srAutoCapMetrics。 */
    }
    /* 自动循环块（2026-11-12）：勾选/文本框/下拉/微调同字号重放。 */
    srApplyCtrlFont((XWidget*)&self->m_cycleEnable, pct);
    srApplyCtrlFont((XWidget*)&self->m_cyclePress, pct);
    srApplyCtrlFont((XWidget*)&self->m_cycleCompare, pct);
    srApplyCtrlFont((XWidget*)&self->m_cycleFallback, pct);
    srApplyCtrlFont((XWidget*)&self->m_cycleTimerMin, pct);
    srApplyCtrlFont((XWidget*)&self->m_cycleBackMin, pct);
    srApplyCtrlFont((XWidget*)&self->m_cycleRinseMin, pct);
    XWidget_setFixedHeight((XWidget*)&self->m_cyclePress,
                           (SR_CTRL_H * pct + 50) / 100);
    XWidget_setFixedHeight((XWidget*)&self->m_cycleCompare,
                           (SR_CTRL_H * pct + 50) / 100);
    /* [宽只给下界] 按最终字体实测；行内余量由布局分配（不写死像素）。 */
    srCyclePressMetrics(self, pct);
    srCycleCmpMetrics(self, pct);
    /* 存储卡 / 登录策略卡控件。 */
    srApplyCtrlFont((XWidget*)&self->m_storeEnable, pct);
    srApplyCtrlFont((XWidget*)&self->m_storeTrigger, pct);
    XWidget_setFixedHeight((XWidget*)&self->m_storeTrigger,
                           (SR_CTRL_H * pct + 50) / 100);
    srApplyCtrlFont((XWidget*)&self->m_storePath, pct);
    srApplyCtrlFont((XWidget*)&self->m_storeSample, pct);
    srApplyCtrlFont((XWidget*)&self->m_storeMax, pct);
    srApplyCtrlFont((XWidget*)&self->m_storePolicy, pct);
    srApplyCtrlFont((XWidget*)&self->m_storeKeep, pct);
    srApplyCtrlFont((XWidget*)&self->m_loginAnon, pct);
    srApplyCtrlFont((XWidget*)&self->m_loginConcur, pct);
    srApplyCtrlFont((XWidget*)&self->m_loginMax, pct);
    /* 无字段引用的匿名标签（分区标题/行说明）：基准挂动态属性。 */
    hsfUi_reapplyTaggedFonts(page, pct);
    /* [标签列宽必须在字号落定后量] 三模式块的两行标签（m_devCap/
     * m_stirCap）是结构体成员但带了 hsfFontBase 标记 ⇒ 字号在上一行走
     * 重放通道；这里再按**最终字体**实测给列宽（否则量的是旧字，字号放大
     * 后标签列变窄、两行不再成列）。 */
    for (m = 0; m < SR_AUTO_MODES; ++m)
        srAutoCapMetrics(self, m, pct);
    XWidget_update(page);
}

/* ==================== 卡片容器实现（照 HsfHomeCard 形态抄） ==================== */

/* [取色] 复用本页既有的 srThemeClr（亮/深两套，见「回填 / 刷新」节）：
 * 亮套字面量与 HsfHomeCard 卡面同源（白底 / 软描边 #D5DBE2）；深套取
 * HsfTheme.h 深色令牌（面板 #23282F / 软描边 #39414C，与 QSS 控件底/
 * 软描边同源）。 */

/** @brief 卡面：圆角底 + 软边框（视觉抄 HsfHomePage.c HsfHomeCard 的
 *         paintEvent——同圆角 5、同 1px 描边、同主题取色）。
 *  @note  [UAF 红线] paint 里只画卡面，**不对子控件做任何操作**
 *         （XGroupBox 的教训——析构期对子树操作踩出 UAF）。 */
static void VHsfSrCard_paintEvent(XWidget* self, XEvent* event)
{
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;

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
    XRect_init(&r, 0, 0, XWidget_width(self), XWidget_height(self));
    XPainter_setPen(&painter,
                    srThemeClr(0xFFD5DBE2u, HSF_COLOR_SOFT_BORDER_DARK));
    XPainter_setPenWidth(&painter, 1);
    XPainter_setBrush(&painter, srThemeClr(0xFFFFFFFFu, HSF_COLOR_PANEL_DARK));
    XPainter_drawRoundedRect(&painter, &r, 5, 5);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

XCLASS_DEFINE_BEGING(HsfSrCardWidget)
XCLASS_DEFINE_EXTEND_END(HsfSrCardWidget, XWidget)

XVtable* HsfSrCard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfSrCardWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VHsfSrCard_paintEvent);
    /* [不 Overload Deinit] 析构走默认：子控件（标题 label）随控件树级联
     * 收掉，本类不自动手工删子（XGroupBox 的 UAF 教训——见 HsfSrCard
     * 结构体注释；HsfHomeCard 同样没有 Overload Deinit）。 */
    return XVTABLE_DEFAULT;
}

/** @brief 建卡片（堆分配挂 parent、随父级联析构；title 空=不建标题行）。
 *  @details 外层布局 margins/spacing 抄 HsfHomeCard（14,10,14,12 / 8）；
 *           内容区 m_bodyLay 竖排 spacing 8（沿用本页原卡内行距），尾部
 *           stretch 让内容顶对齐（同 HsfHomeCard）。标题粗体 +
 *           SR_HEAD_FONT_PX 基准 + hsfFontBase 标记（页面字号缩放经
 *           hsfUi_reapplyTaggedFonts 重放）。 */
static HsfSrCard* srCardCreate(XWidget* parent, const char* title)
{
    HsfSrCard* card;
    if (!parent) return NULL;
    card = (HsfSrCard*)XMemory_malloc(sizeof(HsfSrCard),
                                      XCLASS_DEFAULT_MEMORY_TYPE);
    if (!card) return NULL;
    memset(card, 0, sizeof(*card));
    XWidget_init(&card->m_base, parent, 0);
    XClassGetVtable(card) = HsfSrCard_class_init();
    Set_Class_Memory(card, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(card, true);
    HsfTheme_applyDefaultFont(&card->m_base);

    XBoxLayout_init(&card->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&card->m_layout, 14, 10, 14, 12);
    XLayout_setSpacing((XLayout*)&card->m_layout, 8);
    XWidget_setLayout(&card->m_base, (XLayout*)&card->m_layout);

    if (title && title[0]) {
        XLabel_init(&card->m_title, &card->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&card->m_title);
        srApplyBold((XWidget*)&card->m_title); /* 先加粗、后设字号（不冲掉）。 */
        XLabel_setTextPixelSize(&card->m_title,
            hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE, SR_HEAD_FONT_PX));
        hsfUi_tagFontBase((XWidget*)&card->m_title, SR_HEAD_FONT_PX);
        XLabel_setText_2(&card->m_title, title);
        XWidget_show((XWidget*)&card->m_title);
        XBoxLayout_addWidget(&card->m_layout, (XWidget*)&card->m_title);
    }

    XBoxLayout_init(&card->m_bodyLay, XBoxLayoutDirection_TopToBottom);
    XLayout_setSpacing((XLayout*)&card->m_bodyLay, 8);
    XBoxLayout_addLayout(&card->m_layout, (XLayout*)&card->m_bodyLay);
    XBoxLayout_addStretch(&card->m_layout, 1);

    XWidget_show(&card->m_base);
    return card;
}

XWidget* HsfSettingsRemote_create(XWidget* parent, HsfMenuNavFn navFn,
                                  void* userData)
{
    static const char* const kCollectLabels[SR_COLLECT_ROWS] = {
        "搅拌泵", "温湿度", "进水浊度仪", "出水浊度仪", "流量计", "模拟量", "数字量"
    };
    HsfSettingsRemote* self;
    int i;

    (void)navFn;
    if (!parent) return NULL;
    self = (HsfSettingsRemote*)XMemory_malloc(sizeof(HsfSettingsRemote),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    self->m_statusUser = userData;
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    HsfTheme_applyDefaultFont(&self->m_base);

    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 16, 12, 16, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title, "远端设置");
    XLabel_setTextPixelSize(&self->m_title,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE, SR_TITLE_FONT_PX));
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    /* ---- ① 运行模式（卡片容器 2026-11-12·对齐 Python Card("运行模式")）----
     * [卡片化] 白底圆角自绘卡（HsfSrCard，视觉/生命周期抄 HsfHomeCard；
     * XGroupBox 析构删子树有 UAF，弃用）替代原 srInitHead 标题行——分区
     * 有「块」的观感，模块边界一眼可辨；段内控件挂卡内 m_bodyLay。 */
    self->m_cardMode = srCardCreate(&self->m_base, "运行模式");
    if (!self->m_cardMode) { XPrintf("HsfSettingsRemote: 卡创建失败（运行模式）\n"); }
    if (self->m_cardMode) XBoxLayout_addWidget(&self->m_layout, (XWidget*)self->m_cardMode);

    XLabel_init(&self->m_mode, &self->m_cardMode->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_mode);
    /* [粗体 2026-11-12·对齐 Python font-weight:bold] 运行模式是本页
     * 最大的一行状态（Python 24px 粗体），C 端原 20px 常规 ⇒ 视觉重心
     * 上不来。字重走 XFont 的 weight 通道（见 srApplyBold）。 */
    srApplyBold((XWidget*)&self->m_mode);
    XLabel_setTextPixelSize(&self->m_mode,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE, SR_MODE_FONT_PX));
    XLabel_setText_2(&self->m_mode, "未知");
    XWidget_show((XWidget*)&self->m_mode);
    XBoxLayout_addWidget(&self->m_cardMode->m_bodyLay, (XWidget*)&self->m_mode);

    srInitHint(&self->m_modeHint, &self->m_cardMode->m_base,
               "自动模式会拒绝全部远程控制（设备由现场逻辑驱动）；"
               "手动模式允许管理员远程操作。");
    XBoxLayout_addWidget(&self->m_cardMode->m_bodyLay, (XWidget*)&self->m_modeHint);
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitButton(&self->m_btnManual, &self->m_cardMode->m_base, "切换到手动",
                         (XObject*)&self->m_btnManual, srManualSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnManual);
            /* [缺槽修复 2026-11-12] 原调用缺第 5 参 srAutoSlot（陈旧 obj
             * 掩盖的源码损伤，链接绿但按钮无槽——点了没反应）。补齐。 */
            srInitButton(&self->m_btnAuto, &self->m_cardMode->m_base, "切换到自动",
                         (XObject*)&self->m_btnAuto, srAutoSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnAuto);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardMode->m_bodyLay, (XLayout*)row);
        }
    }

    /* ---- ② 自动运行（卡片容器 2026-11-12·对齐 Python Card("自动运行")）----
     * 同 ①：白底圆角自绘卡（HsfSrCard）替代外层标题行，卡标题由卡承担。 */
    self->m_cardAuto = srCardCreate(&self->m_base, "自动运行");
    if (!self->m_cardAuto) { XPrintf("HsfSettingsRemote: 卡创建失败（自动运行）\n"); }
    if (self->m_cardAuto) XBoxLayout_addWidget(&self->m_layout, (XWidget*)self->m_cardAuto);

    XLabel_init(&self->m_auto, &self->m_cardAuto->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_auto);
    /* [字号 2026-11-12·对齐 Python font-size:20px] 原 15px，与运行
     * 模式行（24px）拉不开层次；Python 自动运行状态行是 20px。 */
    XLabel_setTextPixelSize(&self->m_auto,
        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE, SR_AUTO_FONT_PX));
    XLabel_setText_2(&self->m_auto, "--");
    XWidget_show((XWidget*)&self->m_auto);
    XBoxLayout_addWidget(&self->m_cardAuto->m_bodyLay, (XWidget*)&self->m_auto);

    srInitHint(&self->m_autoHint, &self->m_cardAuto->m_base,
               "切到自动模式只是授权；要真正开始自动运行，需按「启动自动运行」。"
               "服务端重启后控制器一律为停止状态。");
    XBoxLayout_addWidget(&self->m_cardAuto->m_bodyLay, (XWidget*)&self->m_autoHint);
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitButton(&self->m_btnAutoStart, &self->m_cardAuto->m_base, "启动自动运行",
                         (XObject*)&self->m_btnAutoStart, srAutoStartSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnAutoStart);
            srInitButton(&self->m_btnAutoStop, &self->m_cardAuto->m_base, "停止自动运行",
                         (XObject*)&self->m_btnAutoStop, srAutoStopSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnAutoStop);
            /* [急停/复位 2026-11-11] op=3/4 补齐四操作字口径（与 Python
             * bridge.send_auto_control 同通道）：急停=停控制器+全停受控
             * 设备+锁存；复位=解两把锁回停止态（**不是**重新启动）。 */
            srInitButton(&self->m_btnEstop, &self->m_cardAuto->m_base, "急停",
                         (XObject*)&self->m_btnEstop, srEstopSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnEstop);
            srInitButton(&self->m_btnReset, &self->m_cardAuto->m_base, "复位",
                         (XObject*)&self->m_btnReset, srResetSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnReset);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardAuto->m_bodyLay, (XLayout*)row);
        }
    }

    /* ---- ②c 自动模式配置（产水/反洗/顺洗；2026-11-11 三模式）----
     * [三模式各自配置] 自动运行按模式推进，每个模式启动哪些设备由这里
     * 勾选（可多选）；搅拌电机的转速与方向也是**逐模式**的。
     * [2026-11-12 形态对齐 Python pages_other.py:_build_auto_mode_block]
     * 每块三段（竖排）：① 块标题 ② 行1「启动设备」多选下拉 ③ 行2「搅拌
     * 电机」转速 + 折算提示 + 方向。原先 8 个平铺复选框的 4×2 网格已删
     * （三个块各吃 6 行 ⇒ 页面又长又乱；用户反馈「界面样子不好看，特别
     * 是自动模式的」）。「启动设备」改用**公共多选组件** HsfMultiSelect
     * （按钮 + 勾选浮层 + 掩码）——全库只有这一份机制（数据工具条的列
     * 筛选/图例同款），不复制第二份。
     * [服务端已接入] 校验失败（modes 缺失/未知模式 id/设备全未知/stirRpm
     * 非整数）回失败包络 ⇒ err 原文经 configError 进状态栏（fail-closed，
     * 不静默不假成功）；成功应答带 auto 节全量 ⇒ 快照回显。 */
    /* ---- 卡容器（2026-11-12·对齐 Python Card("自动模式配置")）----
     * 同 ①：白底圆角自绘卡（HsfSrCard）替代外层标题行，卡标题由卡承担；
     * 卡内的循环小标题与三模式块小标题保留（parent 改卡）。 */
    self->m_cardCfg = srCardCreate(&self->m_base, "自动模式配置");
    if (!self->m_cardCfg) { XPrintf("HsfSettingsRemote: 卡创建失败（自动模式配置）\n"); }
    if (self->m_cardCfg) XBoxLayout_addWidget(&self->m_layout, (XWidget*)self->m_cardCfg);
    srInitHint(&self->m_autoCfgHint, &self->m_cardCfg->m_base,
               "每个模式各自设置：勾选自动运行进入该模式时要启动的设备（可多选），"
               "并设定搅拌电机的运行转速与方向。改完按「应用」下发服务端；"
               "服务端拒绝时原因会显示在状态栏。");
    XBoxLayout_addWidget(&self->m_cardCfg->m_bodyLay, (XWidget*)&self->m_autoCfgHint);
    {
        static const char* const kAutoModeNames[SR_AUTO_MODES] = {
            HSF_AUTO_MODE_NAME_PRODUCE, HSF_AUTO_MODE_NAME_BACKWASH,
            HSF_AUTO_MODE_NAME_RINSE
        };
        static const char* const kAutoDevNames[SR_AUTO_DEVS] = {
            "电动球阀1", "电动球阀2", "电动球阀3", "电动球阀4",
            "电动球阀5", "电动球阀6", "搅拌泵", "进水泵"
        };
        const int pct = hsfUi_pageFontPct(HSF_PAGE_SETTINGS_REMOTE);
        int m;
        for (m = 0; m < SR_AUTO_MODES; ++m) {
            XLabel* mhead = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                                    XCLASS_DEFAULT_MEMORY_TYPE);
            XHBoxLayout* row1 = (XHBoxLayout*)XMemory_malloc(
                sizeof(XHBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
            if (mhead) {
                srInitHead(mhead, &self->m_cardCfg->m_base, kAutoModeNames[m]);
                XBoxLayout_addWidget(&self->m_cardCfg->m_bodyLay, (XWidget*)mhead);
            }
            /* ---- 行1：启动设备（多选下拉 + 勾选浮层）---- */
            if (row1) {
                HsfMultiSelectConfig cfg;
                XWidget* btn;
                XBoxLayout_init(row1, XBoxLayoutDirection_LeftToRight);
                XLayout_setSpacing((XLayout*)row1, 8);
                srInitRowName(&self->m_autoMode[m].m_devCap, &self->m_cardCfg->m_base,
                              "启动设备");
                XBoxLayout_addWidget(row1,
                                     (XWidget*)&self->m_autoMode[m].m_devCap);
                /* [多选组件文案三态] 全勾=「全部设备」、全不勾=「未选择」、
                 * 部分=勾中的名字顿号连接（对齐 Python MultiCheckCombo）；
                 * 允许全不勾（min_checked=0，Python 同值）。 */
                memset(&cfg, 0, sizeof(cfg));
                cfg.items = kAutoDevNames;
                cfg.count = SR_AUTO_DEVS;
                cfg.initialMask = 0u;   /* Python initial="none"：缺省全不勾。 */
                cfg.allowNone = true;   /* min_checked=0：允许全不勾。 */
                cfg.minChecked = 0;
                /* [空表也带箭头] 缺省掩码=0 与创建态相同 ⇒ setMask 同值
                 * 早退，按钮文字**停在 emptyText**（不会再走 noneText
                 * 分支）⇒ 空表文案必须自带下拉符，否则三块各显示没有箭头的
                 * 「未选择」（Python 端 QComboBox 自带箭头，截图对不齐）。
                 * [字形] 用「∨」(U+2228)：内置字库缺「▾」(U+25BE) ⇒ 写 ▾
                 * 也看不见（2026-11-12 截图实锅）。 */
                cfg.emptyText = "未选择  ∨";
                cfg.allText = "全部设备  ∨";
                cfg.noneText = "未选择  ∨";
                cfg.fn = NULL;          /* 本页无需变更回调（应用时读掩码）。 */
                cfg.userData = NULL;
                if (HsfMultiSelect_create(&self->m_autoMode[m].m_devSel,
                                          &self->m_cardCfg->m_base, &cfg)) {
                    btn = HsfMultiSelect_button(&self->m_autoMode[m].m_devSel);
                    HsfTheme_applyDefaultFont(btn);
                    srApplyCtrlFont(btn, pct);
                    srAutoDevSelMetrics(self, m, pct);
                    XWidget_show(btn);
                    XBoxLayout_addWidget(row1, btn);
                    /* [行尾 stretch] 多选钮有宽度上限 ⇒ 行内剩余空间不要摊
                     * 成项间大空隙把钮顶到最右（Python 端同款教训）。 */
                    XBoxLayout_addStretch(row1, 1);
                    XBoxLayout_addLayout(&self->m_cardCfg->m_bodyLay, (XLayout*)row1);
                } else {
                    XClassDelete((XClass*)row1);
                }
            }
            /* ---- 行2：搅拌电机（转速 + 折算提示 + 方向）---- */
            {
                XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(
                    sizeof(XHBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
                XLabel* cap2 = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                                       XCLASS_DEFAULT_MEMORY_TYPE);
                if (row && cap2) {
                    XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
                    XLayout_setSpacing((XLayout*)row, 8);
                    srInitRowName(&self->m_autoMode[m].m_stirCap, &self->m_cardCfg->m_base,
                                  "搅拌电机");
                    XBoxLayout_addWidget(row,
                                         (XWidget*)&self->m_autoMode[m].m_stirCap);
                    XSpinBox_init(&self->m_autoMode[m].m_rpm, &self->m_cardCfg->m_base, 0);
                    HsfTheme_applyDefaultFont(
                        (XWidget*)&self->m_autoMode[m].m_rpm);
                    XSpinBox_setRange(&self->m_autoMode[m].m_rpm,
                                      HSF_AUTO_STIR_RPM_MIN,
                                      HSF_AUTO_STIR_RPM_MAX);
                    XSpinBox_setSingleStep(&self->m_autoMode[m].m_rpm, 10);
                    XSpinBox_setSuffix(&self->m_autoMode[m].m_rpm, " rpm");
                    /* [初值=铭牌额定点] 与变频器页「转速调速」缺省同口径；
                     * 服务端 auto 节接入后以应答回显为准。 */
                    XSpinBox_setValue(&self->m_autoMode[m].m_rpm,
                                      HSF_AUTO_STIR_RPM_DEF);
                    XWidget_show((XWidget*)&self->m_autoMode[m].m_rpm);
                    /* 最小宽=含后缀 sizeHint（建页首帧就量，否则首帧被行内
                     * 挤压裁首位；根修注记见 srAutoRpmMetrics）。 */
                    srAutoRpmMetrics(self, m);
                    XBoxLayout_addWidget(row,
                                         (XWidget*)&self->m_autoMode[m].m_rpm);
                    /* 折算提示：结构体字段（srSyncAutoHzHint 每拍刷新）。 */
                    XLabel_init(&self->m_autoMode[m].m_hzHint, &self->m_cardCfg->m_base, 0);
                    HsfTheme_applyDefaultFont(
                        (XWidget*)&self->m_autoMode[m].m_hzHint);
                    XLabel_setTextPixelSize(&self->m_autoMode[m].m_hzHint,
                        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE,
                                         SR_HINT_FONT_PX));
                    HsfTheme_applySecondaryText(
                        (XWidget*)&self->m_autoMode[m].m_hzHint);
                    XLabel_setText_2(&self->m_autoMode[m].m_hzHint, "");
                    XWidget_show((XWidget*)&self->m_autoMode[m].m_hzHint);
                    XBoxLayout_addWidget(row,
                                         (XWidget*)&self->m_autoMode[m].m_hzHint);
                    srInitRowName(cap2, &self->m_cardCfg->m_base, "方向");
                    XBoxLayout_addWidget(row, (XWidget*)cap2);
                    XComboBox_init(&self->m_autoMode[m].m_dir, &self->m_cardCfg->m_base, 0);
                    HsfTheme_applyDefaultFont(
                        (XWidget*)&self->m_autoMode[m].m_dir);
                    /* userData 语义 = reverse?（0=正转 1=反转），与变频器
                     * 页 dir_combo 同口径。 */
                    XComboBox_addItem_2(&self->m_autoMode[m].m_dir, "正转");
                    XComboBox_addItem_2(&self->m_autoMode[m].m_dir, "反转");
                    XComboBox_setCurrentIndex(&self->m_autoMode[m].m_dir, 0);
                    XWidget_setFixedHeight(
                        (XWidget*)&self->m_autoMode[m].m_dir,
                        hsfUi_pageFontPx(HSF_PAGE_SETTINGS_REMOTE,
                                         SR_CTRL_H));
                    XWidget_show((XWidget*)&self->m_autoMode[m].m_dir);
                    XBoxLayout_addWidget(row,
                                         (XWidget*)&self->m_autoMode[m].m_dir);
                    XBoxLayout_addStretch(row, 1);
                    XBoxLayout_addLayout(&self->m_cardCfg->m_bodyLay, (XLayout*)row);
                } else {
                    if (row) XClassDelete((XClass*)row);
                    if (cap2) XClassDelete((XClass*)cap2);
                }
            }
            /* ---- 行3：阀复位开关（逐模式；2026-11-12 用户定案）----
             * 停止缺省关回（保持既有「停止即全停含阀」）；急停缺省**不**
             * 关阀（阀可能停在中间位，叠一条“关”是叠加未知动作）。 */
            {
                XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(
                    sizeof(XHBoxLayout), XCLASS_DEFAULT_MEMORY_TYPE);
                if (row) {
                    XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
                    XLayout_setSpacing((XLayout*)row, 8);
                    srInitRowName(&self->m_autoMode[m].m_resetCap, &self->m_cardCfg->m_base,
                                  "阀复位");
                    XBoxLayout_addWidget(row,
                                         (XWidget*)&self->m_autoMode[m].m_resetCap);
                    XCheckBox_init(&self->m_autoMode[m].m_resetStop,
                                   &self->m_cardCfg->m_base, 0);
                    HsfTheme_applyDefaultFont(
                        (XWidget*)&self->m_autoMode[m].m_resetStop);
                    XCheckBox_setText_2(&self->m_autoMode[m].m_resetStop,
                                        "停止时关阀复位");
                    XCheckBox_setChecked(&self->m_autoMode[m].m_resetStop, true);
                    XWidget_show((XWidget*)&self->m_autoMode[m].m_resetStop);
                    XBoxLayout_addWidget(row,
                                         (XWidget*)&self->m_autoMode[m].m_resetStop);
                    XCheckBox_init(&self->m_autoMode[m].m_resetEstop,
                                   &self->m_cardCfg->m_base, 0);
                    HsfTheme_applyDefaultFont(
                        (XWidget*)&self->m_autoMode[m].m_resetEstop);
                    XCheckBox_setText_2(&self->m_autoMode[m].m_resetEstop,
                                        "急停时关阀复位");
                    XCheckBox_setChecked(&self->m_autoMode[m].m_resetEstop, false);
                    XWidget_show((XWidget*)&self->m_autoMode[m].m_resetEstop);
                    XBoxLayout_addWidget(row,
                                         (XWidget*)&self->m_autoMode[m].m_resetEstop);
                    XBoxLayout_addStretch(row, 1);
                    XBoxLayout_addLayout(&self->m_cardCfg->m_bodyLay, (XLayout*)row);
                }
            }
            /* 标签列宽：按当前字实测 + Python 的 110 基线（两行成列）。 */
            srAutoCapMetrics(self, m, pct);
            /* 方向下拉定宽：建页首帧就量（否则首帧被省略成「…」）。 */
            srAutoDirMetrics(self, m, pct);
        }
        srSyncAutoHzHint(self);   /* 首拍先给缺省 1440rpm 的折算文案。 */
    }
    /* ---- 自动循环（组合模式「自动」；2026-11-12 与 Python 对齐）----
     * [同一张卡、同一份 auto 节、**一个**应用钮] 用户定案「这两个卡片合起来一次
     * 提交设置」：循环配置就在自动模式配置卡内分段，底部按钮一次下发
     * {modes:{…}, cycle:{…}}。 */
    srInitHead(&self->m_cycleHead, &self->m_cardCfg->m_base,
               "自动循环（组合模式「自动」）");
    XBoxLayout_addWidget(&self->m_cardCfg->m_bodyLay, (XWidget*)&self->m_cycleHead);
    XCheckBox_init(&self->m_cycleEnable, &self->m_cardCfg->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_cycleEnable);
    XCheckBox_setText_2(&self->m_cycleEnable, "启用自动循环模式");
    XCheckBox_setChecked(&self->m_cycleEnable, true);   /* 协议缺省=true。 */
    XWidget_show((XWidget*)&self->m_cycleEnable);
    XBoxLayout_addWidget(&self->m_cardCfg->m_bodyLay, (XWidget*)&self->m_cycleEnable);
    /* 行1：压差阈值（文本，MPa）+ 比较方向。 */
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitRowName(&self->m_cyclePressCap, &self->m_cardCfg->m_base, "压差阈值");
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cyclePressCap);
            XLineEdit_init(&self->m_cyclePress, &self->m_cardCfg->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_cyclePress);
            XLineEdit_setText(&self->m_cyclePress, "0.50");
            /* [最小宽而非定宽] 宽交给布局：只保证装得下「0.00」+内边距。 */
            srCyclePressMetrics(self, hsfUi_pageFontPct(HSF_PAGE_SETTINGS_REMOTE));
            XWidget_setFixedHeight((XWidget*)&self->m_cyclePress,
                (SR_CTRL_H * hsfUi_pageFontPct(HSF_PAGE_SETTINGS_REMOTE) + 50) / 100);
            XWidget_show((XWidget*)&self->m_cyclePress);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cyclePress);
            srInitHint(&self->m_cyclePressUnit, &self->m_cardCfg->m_base, "MPa");
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cyclePressUnit);
            srInitRowName(&self->m_cycleCmpCap, &self->m_cardCfg->m_base, "比较");
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cycleCmpCap);
            XComboBox_init(&self->m_cycleCompare, &self->m_cardCfg->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_cycleCompare);
            /* userData 不用（setItemData 只收字符串）：下标即协议值，0=gt/1=lt。
             * [项文案 2026-11-12·逐字对齐 Python AUTO_PRESS_COMPARE_LABELS
             *（client_py proto.py:268-271「大于阈值时切换/小于阈值时切换」）。] */
            XComboBox_addItem_2(&self->m_cycleCompare, "大于阈值时切换");
            XComboBox_addItem_2(&self->m_cycleCompare, "小于阈值时切换");
            XComboBox_setCurrentIndex(&self->m_cycleCompare, 0);
            XWidget_setFixedHeight((XWidget*)&self->m_cycleCompare,
                (SR_CTRL_H * hsfUi_pageFontPct(HSF_PAGE_SETTINGS_REMOTE) + 50) / 100);
            /* [最小宽] 不设宽时下拉按 sizeHint 收窄 ⇒ 最长项被省略成
             * 「大于阈值…」（离屏截图实见）；只给下界，不锁死宽度。 */
            srCycleCmpMetrics(self, hsfUi_pageFontPct(HSF_PAGE_SETTINGS_REMOTE));
            XWidget_show((XWidget*)&self->m_cycleCompare);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cycleCompare);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardCfg->m_bodyLay, (XLayout*)row);
        }
    }
    /* 行2：定时兜底 + 三个阶段时长。 */
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            XCheckBox_init(&self->m_cycleFallback, &self->m_cardCfg->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_cycleFallback);
            XCheckBox_setText_2(&self->m_cycleFallback,
                                "产水定时兜底（默认关）");
            XCheckBox_setChecked(&self->m_cycleFallback, false);
            XWidget_show((XWidget*)&self->m_cycleFallback);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cycleFallback);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardCfg->m_bodyLay, (XLayout*)row);
        }
    }
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitRowName(&self->m_cycleTimerCap, &self->m_cardCfg->m_base, "产水");
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cycleTimerCap);
            XSpinBox_init(&self->m_cycleTimerMin, &self->m_cardCfg->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_cycleTimerMin);
            XSpinBox_setRange(&self->m_cycleTimerMin, HSF_AUTO_CYCLE_MINUTES_MIN,
                              HSF_AUTO_CYCLE_MINUTES_MAX);
            XSpinBox_setSuffix(&self->m_cycleTimerMin, " 分钟");
            XSpinBox_setValue(&self->m_cycleTimerMin,
                              HSF_AUTO_CYCLE_TIMER_MINUTES_DEF);
            XWidget_show((XWidget*)&self->m_cycleTimerMin);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cycleTimerMin);
            srInitRowName(&self->m_cycleBackCap, &self->m_cardCfg->m_base, "反洗");
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cycleBackCap);
            XSpinBox_init(&self->m_cycleBackMin, &self->m_cardCfg->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_cycleBackMin);
            XSpinBox_setRange(&self->m_cycleBackMin, HSF_AUTO_CYCLE_MINUTES_MIN,
                              HSF_AUTO_CYCLE_MINUTES_MAX);
            XSpinBox_setSuffix(&self->m_cycleBackMin, " 分钟");
            XSpinBox_setValue(&self->m_cycleBackMin,
                              HSF_AUTO_CYCLE_BACKWASH_MINUTES_DEF);
            XWidget_show((XWidget*)&self->m_cycleBackMin);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cycleBackMin);
            srInitRowName(&self->m_cycleRinseCap, &self->m_cardCfg->m_base, "顺洗");
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cycleRinseCap);
            XSpinBox_init(&self->m_cycleRinseMin, &self->m_cardCfg->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_cycleRinseMin);
            XSpinBox_setRange(&self->m_cycleRinseMin, HSF_AUTO_CYCLE_MINUTES_MIN,
                              HSF_AUTO_CYCLE_MINUTES_MAX);
            XSpinBox_setSuffix(&self->m_cycleRinseMin, " 分钟");
            XSpinBox_setValue(&self->m_cycleRinseMin,
                              HSF_AUTO_CYCLE_RINSE_MINUTES_DEF);
            XWidget_show((XWidget*)&self->m_cycleRinseMin);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_cycleRinseMin);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardCfg->m_bodyLay, (XLayout*)row);
        }
    }
    srInitHint(&self->m_cycleHint, &self->m_cardCfg->m_base,
               "循环顺序固定：产水 →（压差达标或定时兜底）→ 反洗 →（定时）→ 顺洗 →（定时）→ 产水；"
               "压差读不到时不切换（不按缺数据误判）。停止/急停后从当前阶段继续，按复位回产水。");
    XBoxLayout_addWidget(&self->m_cardCfg->m_bodyLay, (XWidget*)&self->m_cycleHint);
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            /* [钮文案 2026-11-12·逐字对齐 Python _auto_apply
             *（pages_other.py:4960「应用自动配置（需管理员）」）。] */
            srInitButton(&self->m_btnAutoApply, &self->m_cardCfg->m_base,
                         "应用自动配置（需管理员）",
                         (XObject*)&self->m_btnAutoApply, srAutoApplySlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnAutoApply);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardCfg->m_bodyLay, (XLayout*)row);
        }
    }

    /* ---- ③ 连接信息（卡片容器 2026-11-12·对齐 Python Card("连接信息")）----
     * 同 ①：白底圆角自绘卡（HsfSrCard）替代外层标题行，卡标题由卡承担。 */
    self->m_cardConn = srCardCreate(&self->m_base, "连接信息");
    if (!self->m_cardConn) { XPrintf("HsfSettingsRemote: 卡创建失败（连接信息）\n"); }
    if (self->m_cardConn) XBoxLayout_addWidget(&self->m_layout, (XWidget*)self->m_cardConn);
    srInitValue(&self->m_conn, &self->m_cardConn->m_base, "--");
    XBoxLayout_addWidget(&self->m_cardConn->m_bodyLay, (XWidget*)&self->m_conn);

    /* ---- ④ 采集配置（卡片容器 2026-11-12·对齐 Python Card("采集配置")）----
     * 同 ①：白底圆角自绘卡（HsfSrCard）替代外层标题行，卡标题由卡承担；
     * 网格的表头行（设备/采集/周期）保留，parent 改卡。 */
    self->m_cardCollect = srCardCreate(&self->m_base, "采集配置");
    if (!self->m_cardCollect) { XPrintf("HsfSettingsRemote: 卡创建失败（采集配置）\n"); }
    if (self->m_cardCollect) XBoxLayout_addWidget(&self->m_layout, (XWidget*)self->m_cardCollect);
    /* [采集配置改网格 2026-11-12·对齐 Python pages_other.py:4573-4617]
     * 表头独占第 0 行（设备/采集/周期），数据行 1..7 同列对齐 ⇒ 勾选框与
     * 周期框逐行成列（旧实现每行独立横排 ⇒ 标签宽度不同导致错位）。
     * 网格与标签全是内嵌成员 ⇒ 随页对象级联析构（无 malloc 壳泄漏）。 */
    XGridLayout_init(&self->m_collectGrid);
    XLayout_setContentsMargins((XLayout*)&self->m_collectGrid, 0, 0, 0, 0);
    XLayout_setSpacing((XLayout*)&self->m_collectGrid, 8);
    srInitHead(&self->m_collectHead[0], &self->m_cardCollect->m_base, "设备");
    srInitHead(&self->m_collectHead[1], &self->m_cardCollect->m_base, "采集");
    srInitHead(&self->m_collectHead[2], &self->m_cardCollect->m_base, "周期");
    XGridLayout_addWidget(&self->m_collectGrid,
                          (XWidget*)&self->m_collectHead[0], 0, 0, 0);
    XGridLayout_addWidget(&self->m_collectGrid,
                          (XWidget*)&self->m_collectHead[1], 0, 1, 0);
    XGridLayout_addWidget(&self->m_collectGrid,
                          (XWidget*)&self->m_collectHead[2], 0, 2, 0);
    for (i = 0; i < SR_COLLECT_ROWS; ++i) {
        srInitRowName(&self->m_collectName[i], &self->m_cardCollect->m_base,
                      kCollectLabels[i]);
        XGridLayout_addWidget(&self->m_collectGrid,
                              (XWidget*)&self->m_collectName[i], i + 1, 0, 0);

        XCheckBox_init(&self->m_collect[i].m_chk, &self->m_cardCollect->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_collect[i].m_chk);
        XCheckBox_setText_2(&self->m_collect[i].m_chk, "");
        XWidget_show((XWidget*)&self->m_collect[i].m_chk);
        XGridLayout_addWidget(&self->m_collectGrid,
                              (XWidget*)&self->m_collect[i].m_chk, i + 1, 1, 0);

        XSpinBox_init(&self->m_collect[i].m_spin, &self->m_cardCollect->m_base, 0);
        HsfTheme_applyDefaultFont((XWidget*)&self->m_collect[i].m_spin);
        XSpinBox_setRange(&self->m_collect[i].m_spin, SR_POLL_MS_MIN,
                          SR_POLL_MS_MAX);
        XSpinBox_setSingleStep(&self->m_collect[i].m_spin, 50);
        XSpinBox_setSuffix(&self->m_collect[i].m_spin, " ms");
        XSpinBox_setValue(&self->m_collect[i].m_spin, 200);
        /* [宽度交布局] XSpinBox::sizeHint 按最大可能值量宽，页面不定宽。 */
        XWidget_show((XWidget*)&self->m_collect[i].m_spin);
        XGridLayout_addWidget(&self->m_collectGrid,
                              (XWidget*)&self->m_collect[i].m_spin, i + 1, 2, 0);
    }
    XBoxLayout_addLayout(&self->m_cardCollect->m_bodyLay, (XLayout*)&self->m_collectGrid);
    srInitHint(&self->m_collectHint, &self->m_cardCollect->m_base,
               "两台浊度仪是两台独立设备：采集开关与周期各自独立（可单独停一台检修）；"
               "流量计周期也单列。\n"
               "电磁阀/进水泵跟随数字量的开关。"
               "改开关立即生效（被停的设备健康转数据过期），改周期下一拍生效。");
    XBoxLayout_addWidget(&self->m_cardCollect->m_bodyLay, (XWidget*)&self->m_collectHint);
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitButton(&self->m_btnCfgRefresh, &self->m_cardCollect->m_base, "刷新",
                         (XObject*)&self->m_btnCfgRefresh, srCfgRefreshSlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnCfgRefresh);
            srInitButton(&self->m_btnCfgApply, &self->m_cardCollect->m_base, "应用（需管理员）",
                         (XObject*)&self->m_btnCfgApply, srCfgApplySlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnCfgApply);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardCollect->m_bodyLay, (XLayout*)row);
        }
    }

    /* ---- ④-2 运行保护（2026-11-15·对齐 Python Card("运行保护")）----
     * 进水泵开着、流量计却一直报"空管" ⇒ 到时限按配置动作（停设备/只报警）。
     * 位置：采集配置之后、数据存储之前（都属设备侧的事）。
     * [时长用秒] 现场口径是"泵开着多久"（分钟级），秒比毫秒好读；协议里
     * 仍是毫秒（与服务端同源）。 */
    self->m_cardProtect = srCardCreate(&self->m_base, "运行保护");
    if (!self->m_cardProtect) { XPrintf("HsfSettingsRemote: 卡创建失败（运行保护）\n"); }
    if (self->m_cardProtect) XBoxLayout_addWidget(&self->m_layout, (XWidget*)self->m_cardProtect);
    srInitHint(&self->m_protectHint, &self->m_cardProtect->m_base,
               "进水泵开启后，若流量计仍报「空管」并持续到设定时长，说明水路没通"
               "（或流量计异常）：可只报警留记录，也可直接停止设备。"
               "时长 0 = 关闭该保护（缺省不启用）。");
    XBoxLayout_addWidget(&self->m_cardProtect->m_bodyLay, (XWidget*)&self->m_protectHint);
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            XCheckBox_init(&self->m_protectStop, &self->m_cardProtect->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_protectStop);
            XCheckBox_setText_2(&self->m_protectStop, "超时后停止设备");
            /* [缺省勾上 2026-11-15·对齐 Python] Python 客户端这两个「停止
             * 设备」勾选框**缺省是勾上的**（PROTECT_*_STOP_DEF=True），
             * C 端原来缺省不勾 ⇒ 两端客户端对同一份配置的读法不同（用户
             * 在 C 端点「应用」= 悄悄把"停设备"改成"只报警"）。⇒ 同口径：
             * 缺省与服务端缺省一致。 */
            XCheckBox_setChecked(&self->m_protectStop,
                                 HSF_PROTECT_INLET_NOFLOW_STOP_DEF);
            XWidget_show((XWidget*)&self->m_protectStop);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_protectStop);
            {
                /* [行标签用**内嵌成员**] 临时分配的控件挂进布局后由布局管理，
                 * 生命周期容易出错（AGENTS 铁律：控件归布局管 ⇒ 父布局失效
                 * 顺序敏感）。本页其它行标签都用内嵌成员，这里同款。 */
                srInitRowName(&self->m_protectCap, &self->m_cardProtect->m_base,
                              "空管判定时长");
                XBoxLayout_addWidget(row, (XWidget*)&self->m_protectCap);
            }
            XSpinBox_init(&self->m_protectTimeout, &self->m_cardProtect->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_protectTimeout);
            XSpinBox_setRange(&self->m_protectTimeout, 0, 3600);
            XSpinBox_setSingleStep(&self->m_protectTimeout, 10);
            XSpinBox_setSuffix(&self->m_protectTimeout, " 秒");
            XSpinBox_setValue(&self->m_protectTimeout, 0);   /* 0 = 关闭该保护 */
            XWidget_show((XWidget*)&self->m_protectTimeout);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_protectTimeout);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardProtect->m_bodyLay, (XLayout*)row);
        }
    }
    /* ---- 压差过大保护（2026-11-15·对齐 Python Card 里的同名分区）----
     * 压差（模拟量通道 0）连续超上限约 2 秒 ⇒ 触发：滤芯堵塞/水路异常时泵
     * 在憋压，继续跑会伤设备 ⇒ 报警（进报警历史表）+ 按配置停机。
     * [上限用 kPa] 桌面端只有整数微调框 ⇒ MPa 的 0.8 表达不了 ⇒ 界面用
     * kPa（800 kPa = 0.8 MPa），提交时换算成 MPa（与 Python 端同款）。 */
    srInitHint(&self->m_protectHint2, &self->m_cardProtect->m_base,
               "压差过大保护：压差连续超过上限并持续约 2 秒即触发（滤芯堵塞/"
               "水路异常时泵会憋压，继续跑会伤设备）。上限与现场工艺相关，"
               "请按实际情况设定；不启用则不判。");
    XBoxLayout_addWidget(&self->m_cardProtect->m_bodyLay,
                         (XWidget*)&self->m_protectHint2);
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            XCheckBox_init(&self->m_pressHighEnable,
                           &self->m_cardProtect->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_pressHighEnable);
            XCheckBox_setText_2(&self->m_pressHighEnable,
                                "启用压差过大保护");
            XWidget_show((XWidget*)&self->m_pressHighEnable);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_pressHighEnable);
            srInitRowName(&self->m_pressHighCap, &self->m_cardProtect->m_base,
                          "压差上限");
            XBoxLayout_addWidget(row, (XWidget*)&self->m_pressHighCap);
            XSpinBox_init(&self->m_pressHighLimit,
                           &self->m_cardProtect->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_pressHighLimit);
            XSpinBox_setRange(&self->m_pressHighLimit, 0, 10000);
            XSpinBox_setSingleStep(&self->m_pressHighLimit, 10);
            XSpinBox_setSuffix(&self->m_pressHighLimit, " kPa");
            XSpinBox_setValue(&self->m_pressHighLimit, 800);
            XWidget_show((XWidget*)&self->m_pressHighLimit);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_pressHighLimit);
            XCheckBox_init(&self->m_pressHighStop,
                           &self->m_cardProtect->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_pressHighStop);
            XCheckBox_setText_2(&self->m_pressHighStop, "触发后停止设备");
            /* [缺省勾上 2026-11-15·对齐 Python] 同上：缺省 = 服务端缺省
             * （HSF_PROTECT_PRESS_HIGH_STOP_DEF = true）。 */
            XCheckBox_setChecked(&self->m_pressHighStop,
                                 HSF_PROTECT_PRESS_HIGH_STOP_DEF);
            XWidget_show((XWidget*)&self->m_pressHighStop);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_pressHighStop);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardProtect->m_bodyLay, (XLayout*)row);
        }
    }
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitButton(&self->m_btnProtectApply, &self->m_cardProtect->m_base,
                         "应用（需管理员）",
                         (XObject*)&self->m_btnProtectApply, srProtectApplySlot);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnProtectApply);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardProtect->m_bodyLay, (XLayout*)row);
        }
    }

    /* ---- ⑤ 数据存储（卡片容器 2026-11-12·对齐 Python Card("数据存储…")）----
     * 同 ①：白底圆角自绘卡（HsfSrCard）替代外层标题行，卡标题由卡承担
     * （标题文本沿用原分区标题全量口径）；容量条/用量行等全保留。 */
    self->m_cardStore = srCardCreate(&self->m_base,
                                     "数据存储（SQLite：历史/日志/报警）");
    if (!self->m_cardStore) { XPrintf("HsfSettingsRemote: 卡创建失败（数据存储）\n"); }
    if (self->m_cardStore) XBoxLayout_addWidget(&self->m_layout, (XWidget*)self->m_cardStore);
    XCheckBox_init(&self->m_storeEnable, &self->m_cardStore->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_storeEnable);
    XCheckBox_setText_2(&self->m_storeEnable, "启用落盘");
    /* [缺省勾上 2026-11-10] 服务端落盘缺省是**开**（storage/enabled 缺省 1）。
     * 勾选框默认未勾 ⇒ 服务端快照没到就点「应用」会把落盘关掉 ⇒ 历史
     * 表格从此空（现场"表格数据咋没了"）。缺省态与服务端口径一致。 */
    XCheckBox_setChecked(&self->m_storeEnable, true);
    XWidget_show((XWidget*)&self->m_storeEnable);
    XBoxLayout_addWidget(&self->m_cardStore->m_bodyLay, (XWidget*)&self->m_storeEnable);

    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
        if (row && name) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitRowName(name, &self->m_cardStore->m_base, "库文件路径");
            /* [宽度交布局 2026-11-10] 原先定宽 96：字号放大后「库文件路径」
             * 四个字就裁字。改由 XLabel 的 sizeHint 定宽，余量给右边的路径
             * 输入框（stretch=1）。 */
            XBoxLayout_addWidget(row, (XWidget*)name);

            XLineEdit_init(&self->m_storePath, &self->m_cardStore->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storePath);
            XLineEdit_setPlaceholderText(&self->m_storePath,
                "空 = 服务端程序目录\\hsf_data.db；相对路径也按程序目录展开");
            XWidget_setFixedHeight((XWidget*)&self->m_storePath, 34);
            XWidget_show((XWidget*)&self->m_storePath);
            XBoxLayout_addWidgetEx(row, (XWidget*)&self->m_storePath, 1, 0);
            XBoxLayout_addLayout(&self->m_cardStore->m_bodyLay, (XLayout*)row);
        } else {
            if (row) XClassDelete((XClass*)row);
            if (name) XClassDelete((XClass*)name);
        }
    }
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name1 = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name2 = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (row && name1 && name2) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitRowName(name1, &self->m_cardStore->m_base, "入库周期");
            XBoxLayout_addWidget(row, (XWidget*)name1);
            XSpinBox_init(&self->m_storeSample, &self->m_cardStore->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storeSample);
            XSpinBox_setRange(&self->m_storeSample, 1000, 3600000);
            XSpinBox_setSingleStep(&self->m_storeSample, 1000);
            XSpinBox_setSuffix(&self->m_storeSample, " ms");
            XSpinBox_setValue(&self->m_storeSample,
                              HSF_STORAGE_SAMPLE_MS_DEF);
            /* [宽度交布局 2026-11-10] 原先定宽 150：字号放大后 "3600000 ms"
             * 装不下（末位被裁）。改由 XSpinBox::sizeHint 定宽——它按量程
             * 两端 + 当前值里最宽的文本量（含后缀），布局按需给。 */
            XWidget_show((XWidget*)&self->m_storeSample);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_storeSample);

            srInitRowName(name2, &self->m_cardStore->m_base, "容量上限");
            XBoxLayout_addWidget(row, (XWidget*)name2);
            XSpinBox_init(&self->m_storeMax, &self->m_cardStore->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storeMax);
            XSpinBox_setRange(&self->m_storeMax, SR_MAX_MB_MIN, SR_MAX_MB_MAX);
            XSpinBox_setSingleStep(&self->m_storeMax, 50);
            XSpinBox_setSuffix(&self->m_storeMax, " MB");
            /* [「0=不限」专用文案 2026-11-12·对齐 Python pages_other.py:4672]
             * 缺省显示 "0 MB" 会被读成「限 0 字节」——框架有 specialValueText
             * （对标 QSpinBox::setSpecialValueText），取最小值时显示它。 */
            XSpinBox_setSpecialValueText(&self->m_storeMax, "不限");
            /* [tooltip 2026-11-12·对齐 Python pages_other.py:4673] */
            srSetTip((XWidget*)&self->m_storeMax,
                     "0 = 不限；库达到上限后按下方策略处理");
            XSpinBox_setValue(&self->m_storeMax, HSF_STORAGE_MAX_MB_DEF);
            XWidget_show((XWidget*)&self->m_storeMax);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_storeMax);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardStore->m_bodyLay, (XLayout*)row);
        } else {
            if (row) XClassDelete((XClass*)row);
            if (name1) XClassDelete((XClass*)name1);
            if (name2) XClassDelete((XClass*)name2);
        }
    }
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name1 = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name2 = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                               XCLASS_DEFAULT_MEMORY_TYPE);
        if (row && name1 && name2) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitRowName(name1, &self->m_cardStore->m_base, "写满后");
            XBoxLayout_addWidget(row, (XWidget*)name1);

            XComboBox_init(&self->m_storePolicy, &self->m_cardStore->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storePolicy);
            /* 落盘策略项为中文长文案，宽度必须按内容定（登录策略应答/
             * 主题字号缩放后重排都要跟随）⇒ 登记内容驱动档，不随 Qt 6.8
             * 缺省 AdjustToContentsOnFirstShow 首显冻结（Qt68对齐 P0-04
             * 的可见行为变化，本页登记回内容驱动档）。 */
            XComboBox_setSizeAdjustPolicy(
                &self->m_storePolicy,
                XComboBoxSizeAdjustPolicy_AdjustToContents);
            XComboBox_addItem_2(&self->m_storePolicy, "循环覆盖（删最旧）");
            XComboBox_setItemData_2(&self->m_storePolicy, 0, "ring");
            XComboBox_addItem_2(&self->m_storePolicy, "到限停写（只读保留）");
            XComboBox_setItemData_2(&self->m_storePolicy, 1, "stop");
            XComboBox_addItem_2(&self->m_storePolicy, "分段滚动（按段归档）");
            XComboBox_setItemData_2(&self->m_storePolicy, 2, "rotate");
            XComboBox_setCurrentIndex(&self->m_storePolicy, 0);
            XWidget_setFixedHeight((XWidget*)&self->m_storePolicy, 34);
            XWidget_setMinimumWidth((XWidget*)&self->m_storePolicy, 240);
            XObject_connect((XObject*)&self->m_storePolicy,
                              XSignal(XComboBox_currentIndexChanged_signal),
                              (XObject*)&self->m_storePolicy, srPolicySlot,
                              XConnectionType_Direct);
            XWidget_show((XWidget*)&self->m_storePolicy);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_storePolicy);

            srInitRowName(name2, &self->m_cardStore->m_base, "保留分段");
            XBoxLayout_addWidget(row, (XWidget*)name2);
            XSpinBox_init(&self->m_storeKeep, &self->m_cardStore->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storeKeep);
            XSpinBox_setRange(&self->m_storeKeep, 1, 99);
            /* [tooltip 2026-11-12·对齐 Python pages_other.py:4689] */
            srSetTip((XWidget*)&self->m_storeKeep,
                     "仅「分段滚动」策略用：总上限会均分到各段");
            XSpinBox_setValue(&self->m_storeKeep, 3);
            XWidget_show((XWidget*)&self->m_storeKeep);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_storeKeep);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardStore->m_bodyLay, (XLayout*)row);
        } else {
            if (row) XClassDelete((XClass*)row);
            if (name1) XClassDelete((XClass*)name1);
            if (name2) XClassDelete((XClass*)name2);
        }
    }
    /* [入库触发 2026-11-12 用户定案] 持续采集入库 / 仅自动运行时入库。
     * 只影响**落盘**（遥测采集/界面推送照旧）——界面数据不缺，只是历史表
     * 少了「没在自动生产」的时段。 */
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
        if (row && name) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitRowName(name, &self->m_cardStore->m_base, "入库触发");
            XBoxLayout_addWidget(row, (XWidget*)name);
            XComboBox_init(&self->m_storeTrigger, &self->m_cardStore->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_storeTrigger);
            XComboBox_addItem_2(&self->m_storeTrigger,
                                "持续采集入库（与自动模式无关）");
            XComboBox_setItemData_2(&self->m_storeTrigger, 0,
                                    HSF_HISTORY_TRIGGER_ALWAYS);
            XComboBox_addItem_2(&self->m_storeTrigger,
                                "仅自动运行时入库（停机期间不记）");
            XComboBox_setItemData_2(&self->m_storeTrigger, 1,
                                    HSF_HISTORY_TRIGGER_AUTO);
            XComboBox_setCurrentIndex(&self->m_storeTrigger, 0);
            XWidget_setFixedHeight((XWidget*)&self->m_storeTrigger, 34);
            XWidget_setMinimumWidth((XWidget*)&self->m_storeTrigger, 240);
            XWidget_show((XWidget*)&self->m_storeTrigger);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_storeTrigger);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardStore->m_bodyLay, (XLayout*)row);
        } else {
            if (row) XClassDelete((XClass*)row);
            if (name) XClassDelete((XClass*)name);
        }
    }
    srInitHint(&self->m_policyHint, &self->m_cardStore->m_base, srPolicyHintText("ring"));
    XBoxLayout_addWidget(&self->m_cardStore->m_bodyLay, (XWidget*)&self->m_policyHint);

    /* [容量条 2026-11-10 对齐 Python 客户端] Python 版存储卡里是「进度条 +
     * 用量文字」两件套（pages_other.py `_storage_usage` / `_populate_usage`）：
     * 条宽随页宽、高 12、不显示内嵌文本（百分比在下方文字里），上限为 0
     * （不限）时恒 0% —— 这时候没有"用了百分之多少"可言，硬算会误导。 */
    XProgressBar_init(&self->m_storageBar, &self->m_cardStore->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_storageBar);
    XProgressBar_setRange(&self->m_storageBar, 0, 100);
    XProgressBar_setValue(&self->m_storageBar, 0);
    XProgressBar_setTextVisible(&self->m_storageBar, false);
    XWidget_setFixedHeight((XWidget*)&self->m_storageBar, 12);
    XWidget_show((XWidget*)&self->m_storageBar);
    XBoxLayout_addWidget(&self->m_cardStore->m_bodyLay, (XWidget*)&self->m_storageBar);

    srInitValue(&self->m_storageUse, &self->m_cardStore->m_base, "用量：--");
    XBoxLayout_addWidget(&self->m_cardStore->m_bodyLay, (XWidget*)&self->m_storageUse);

    srInitValue(&self->m_dbPath, &self->m_cardStore->m_base, "当前库文件：--");
    XBoxLayout_addWidget(&self->m_cardStore->m_bodyLay, (XWidget*)&self->m_dbPath);

    srInitButton(&self->m_btnSave, &self->m_cardStore->m_base, "保存（需管理员）",
                 (XObject*)&self->m_btnSave, srSaveSlot);
    /* [按钮行 2026-11-12·对齐 Python row4] Python 的保存钮在
     * QHBoxLayout 里 + 行尾 stretch ⇒ 按钮按自身 sizeHint 宽、**靠左**；
     * C 端原先直接挂竖排布局 ⇒ 交叉轴铺满（通栏大按钮，一眼就与
     * 其它卡不一致）。改挂横排 + 行尾 stretch（与本页其余按钮行同款）。
     * 按钮恒建（行分配失败才回落竖排），避免 OOM 时功能整体消失。 */
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnSave);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardStore->m_bodyLay, (XLayout*)row);
        } else {
            XBoxLayout_addWidget(&self->m_cardStore->m_bodyLay, (XWidget*)&self->m_btnSave);
        }
    }

    /* ---- ⑥ 登录策略（卡片容器 2026-11-12·对齐 Python Card("登录策略…")）----
     * 同 ①：白底圆角自绘卡（HsfSrCard）替代外层标题行，卡标题由卡承担。 */
    self->m_cardLogin = srCardCreate(&self->m_base, "登录策略（账号）");
    if (!self->m_cardLogin) { XPrintf("HsfSettingsRemote: 卡创建失败（登录策略）\n"); }
    if (self->m_cardLogin) XBoxLayout_addWidget(&self->m_layout, (XWidget*)self->m_cardLogin);
    XCheckBox_init(&self->m_loginAnon, &self->m_cardLogin->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_loginAnon);
    XCheckBox_setText_2(&self->m_loginAnon, "允许匿名登录（只读）");
    XWidget_show((XWidget*)&self->m_loginAnon);
    XBoxLayout_addWidget(&self->m_cardLogin->m_bodyLay, (XWidget*)&self->m_loginAnon);

    XCheckBox_init(&self->m_loginConcur, &self->m_cardLogin->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_loginConcur);
    XCheckBox_setText_2(&self->m_loginConcur, "允许同账并发（同账户多端在线）");
    XWidget_show((XWidget*)&self->m_loginConcur);
    XBoxLayout_addWidget(&self->m_cardLogin->m_bodyLay, (XWidget*)&self->m_loginConcur);

    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        XLabel* name = (XLabel*)XMemory_malloc(sizeof(XLabel),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
        if (row && name) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            srInitRowName(name, &self->m_cardLogin->m_base, "会话上限");
            /* [宽度交布局 2026-11-10] 同上：标签宽度交给 sizeHint。 */
            XBoxLayout_addWidget(row, (XWidget*)name);
            XSpinBox_init(&self->m_loginMax, &self->m_cardLogin->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_loginMax);
            XSpinBox_setRange(&self->m_loginMax, 0, 256);
            XSpinBox_setSingleStep(&self->m_loginMax, 1);
            /* [「不限」专用文案 2026-11-12·对齐 Python pages_other.py:4745]
             * 原后缀写「个（0=不限）」，最小值时显示成「0 个（0=不限）」——自相
             * 重复。改用 specialValueText 显示「不限」，后缀只留「个」。 */
            XSpinBox_setSuffix(&self->m_loginMax, " 个");
            XSpinBox_setSpecialValueText(&self->m_loginMax, "不限");
            XSpinBox_setValue(&self->m_loginMax, 8);
            XWidget_show((XWidget*)&self->m_loginMax);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_loginMax);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardLogin->m_bodyLay, (XLayout*)row);
        } else {
            if (row) XClassDelete((XClass*)row);
            if (name) XClassDelete((XClass*)name);
        }
    }
    srInitHint(&self->m_loginHint, &self->m_cardLogin->m_base,
               "写入立即对下一次登录生效并落盘 hsf_login_policy.json；\n"
               "已在线会话不受影响（关闭同账并发不会踢人）。");
    XBoxLayout_addWidget(&self->m_cardLogin->m_bodyLay, (XWidget*)&self->m_loginHint);

    srInitButton(&self->m_btnLoginSave, &self->m_cardLogin->m_base, "应用登录策略（需管理员）",
                 (XObject*)&self->m_btnLoginSave, srLoginSaveSlot);
    /* [按钮行 2026-11-12·对齐 Python login_btns] 同上：按钮靠左、行尾
     * stretch（原先竖排布局里通栏）。 */
    {
        XHBoxLayout* row = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                        XCLASS_DEFAULT_MEMORY_TYPE);
        if (row) {
            XBoxLayout_init(row, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)row, 8);
            XBoxLayout_addWidget(row, (XWidget*)&self->m_btnLoginSave);
            XBoxLayout_addStretch(row, 1);
            XBoxLayout_addLayout(&self->m_cardLogin->m_bodyLay, (XLayout*)row);
        } else {
            XBoxLayout_addWidget(&self->m_cardLogin->m_bodyLay,
                                 (XWidget*)&self->m_btnLoginSave);
        }
    }

    XBoxLayout_addStretch(&self->m_layout, 1);

    self->m_seenCmdSeq = HsfLinkClient_commandSeq();
    self->m_seenWriteSeq = HsfLinkClient_configSeq();
    self->m_seenGetSeq = HsfLinkClient_configGetSeq();

    self->m_timer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_timer) {
        XTimer_setInterval(self->m_timer, HsfSettingsLocal_refreshMs());
        XTimer_setTimerCallback(self->m_timer, srTimerCb);
        XTimer_setUserData(self->m_timer, self);
        XTimer_start_base(self->m_timer);
    }

    /* 字号重放注册（全局/按页字号改动即时生效；本页是自建根结构）。 */
    hsfUi_registerPageFontApplier(HSF_PAGE_SETTINGS_REMOTE, &self->m_base,
                                  srApplyFonts);
    /* [析构收尾] 三个「启动设备」多选组件的**勾选浮层**是无父顶层 Popup
     * （不随控件树级联）、项名是堆 XString ⇒ 必须在页销毁时显式收掉，
     * 否则每进一次远端设置页泄漏三个浮层窗口。destroyed 在
     * VXObject_deinit 早期发出（此时页面字段仍有效），用 connect_2
     * （无 receiver）把 self 作为 sender 上下文——与 HsfDataBar 同款。
     * **按钮不删**：它是本页的子控件，随控件树级联（HsfMultiSelect 契约）。 */
    XObject_connect_2((XObject*)&self->m_base,
                      XSignal(XObject_destroyed_signal),
                      srPageDestroyedSlot);

    XWidget_show(&self->m_base);
    return &self->m_base;
}

/**
 * @brief 远端设置页窄宽自适应（2026-11-09 竖屏适配批次补齐）。
 * @details [为什么只动两处] 本页行最宽的是「写满后 + 策略下拉(最小宽 240)
 *          + 保留分段 + spin(100)」≈513px——480 手机档必裁右端。其余行
 *          （采集 96+60+150、登录 96+170）在 480 档视口 ~456px 内本就放得
 *          下，不动。窄档两板斧：页边距 16,12→8,6（与 HsfDataAlarm_adapt
 *          同口径）；策略下拉最小宽 240→160（行宽 ~433 放得下，下拉本身
 *          可横向滚动条目）。宽档恢复 create 原值，幂等（帧泵每轮驱动）。
 *          [自动模式配置卡 2026-11-12 形态对齐 Python] 三模式块的「启动
 *          设备」已由 4 列勾选框网格改为**多选下拉按钮**（有 150~420 的
 *          宽区间 ⇒ 窄档只是钮被压到 150 的最小宽，不会再撑爆整行），故
 *          此处无需按列数回流；模式块内两行的标签列宽由 srAutoCapMetrics
 *          按**当前字实测**给（Python 端 minWidth 110 同口径）。
 */
void HsfSettingsRemote_adapt(XWidget* page, bool compact)
{
    HsfSettingsRemote* self = (HsfSettingsRemote*)page;
    /* 自动换行标签全集（adapt 期按实测钉保高下限）。
     * 注：局部数组取 self 成员地址，不能做成 static 初始化器（非常量）。
     * [值行也在列 2026-11-12] m_conn / m_storageUse / m_dbPath 已从「说明
     * 小字」改成「正文值行」（srInitValue），但仍是 wordWrap=true ⇒ 长
     * 文本（库文件全路径、用量+策略长句）在窄档一样会换行，故一并保高。 */
    XLabel* const kHints[] = {
        &self->m_modeHint,   &self->m_autoHint,
        &self->m_autoCfgHint, &self->m_conn,
        &self->m_collectHint, &self->m_policyHint,
        &self->m_storageUse, &self->m_dbPath,
        &self->m_loginHint,
    };
    int availW;
    int i;
    if (!self) return;
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 8 : 16, compact ? 6 : 12,
                               compact ? 8 : 16, compact ? 6 : 12);
    XWidget_setMinimumWidth((XWidget*)&self->m_storePolicy,
                            compact ? 160 : 240);
    /* [换行说明行保高 2026-11-11 走查] wordWrap 说明标签在窄档（600 竖档
     * 等）会换到第 2 行，但布局对「自动换行」标签分配的高度不足其
     * heightForWidth 应答（600x960 实测「…停止状态。」第 2 行整行被裁、
     * 句号丢失；探针取证 label 实分 18px < hfw(584)=24）——框架布局链的
     * 既有缺口。这里用既有 _adapt 档位机制兜底：按实测判「当前档会换行」
     * （标签无换行宽 = minimumSizeHint().width > 可用宽）才把最小高钉到
     * 2 行（行高取标签自字实测，同 HsfDataHistory_adapt 的按实测口径）；
     * 宽档/不换行标签恢复 0（不松布局）。幂等：帧泵每轮驱动，同值早退。
     * 【框架根因待立 ticket】XBoxLayout 对 hfw 条目的几何分配与
     * XScrollArea 两遍收敛的提示缓存交错时，自动换行标签实分高度可低于
     * 其 hfw 应答（含硬 \n 标签不受影响——其 sizeHint 已含多行高）。 */
    availW = XWidget_width(page) -
             (compact ? 8 * 2 : 16 * 2);
    if (availW < 1) availW = 1;
    for (i = 0; i < (int)(sizeof(kHints) / sizeof(kHints[0])); ++i) {
        XLabel* lb = kHints[i];
        XSize noWrap;
        XFont font;
        int lineH;
        int floorH = 0;
        if (!lb) continue;
        noWrap = XWidget_minimumSizeHint_base((XWidget*)lb);
        if (noWrap.width > availW) {
            font = XWidget_font((XWidget*)lb);
            lineH = XPainter_textHeight(&font);
            XClassDeinit(&font);
            floorH = lineH * 2;
        }
        XWidget_setMinimumHeight((XWidget*)lb, floorH);
    }
    XLayout_activate((XLayout*)&self->m_layout);
}

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XCHECKBOX_ON && XSPINBOX_ON */
