/**
 * @file       HsfDataBar.c
 * @brief      数据域控制工具条实现：两行控件 + 统一动作回调。
 * @details    行1=起始/结束时间 + 通道 + 列筛选 + 筛选/清除筛选 +
 *             行数统计（右靠）；行2=小数位数 + 导出格式 + 导出/重新
 *             加载/生成折线图（绿）+ 表格|图表切换（右靠）。
 *             「数据分析」按需求裁掉。各段说明用弱色标签（VFD 页弱化
 *             文案同口径），控件高 28 触控友好。
 *             **窄窗自适应**（HsfDataBar_adapt，按工具条实际可用宽分五档）：
 *             ⓪ 标签全显+常规宽 → ① 收说明标签 → ② 收控件最小宽+行距 →
 *             ③ 隐去「共 N 行」（1024 宽窗两行最小宽仍差几像素时）→
 *             ④ 行1/行2 翻竖排（手机竖屏连双日期框横排都放不下时，
 *             控件不搬家只翻容器方向）。
 *             「共 N 行」最小宽按**当前文本实测宽**取，5 位数不再被截字。
 *             槽连接口径=HsfMenuPage 同款（receiver=工具条对象；本模块
 *             m_base 是首成员，槽首参直转安全）。sender 分派用
 *             XObject_sender()（对标 QObject::sender()，线程栈口径）。
 */
#include "HsfDataBar.h"

#include "HsfTheme.h"
#include "XObject.h"
#include "XMemory.h"
#include "XFont.h"
#include "XString.h"
#include "XPrintf.h"
#include "XSystem.h"
#include "XPainter.h"
#include <string.h>
#include <stdio.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_BOX_ON && \
    XDATETIMEEDIT_ON && XCOMBOBOX_ON && XSPINBOX_ON

/* ---- UTF-8 字面量（源文件 ASCII 化口径） ---- */
#define T_START     "起始时间"          /* 起始时间 */
#define T_END       "结束时间"          /* 结束时间 */
#define T_CHANNEL   "通道"                                   /* 通道 */
#define T_COLFILTER "列筛选"                       /* 列筛选 */
#define T_FILTER    "筛选"                                   /* 筛选 */
#define T_CLEAR     "清除筛选"          /* 清除筛选 */
#define T_ROWS      "共 %lld 行"                             /* 共 %lld 行 */
#define T_DECIMALS  "显示小数位数" /* 显示小数位数 */
#define T_EXPORT    "导出"                                   /* 导出 */
#define T_RELOAD    "重新加载"           /* 重新加载 */
#define T_CHART     "生成折线图" /* 生成折线图 */
#define T_TABLE     "表格"                                   /* 表格 */
#define T_CHARTTAB  "图表"                                   /* 图表 */
#define T_ALL_CHANNELS "全部通道"       /* 全部通道 */
#define T_FMT_XLSX  "Excel (.xlsx)"
#define T_FMT_CSV   "CSV (.csv)"
#define T_MAXPTS    "每图例最多点" /* 每图例最多点 */

/** @brief 工具条对象；m_base 必须是第一个成员（槽首参直转依据）。 */
typedef struct HsfDataBar
{
    XWidget        m_base;        /**< 条容器；必须是第一个成员。 */
    XBoxLayout     m_root;        /**< 垂直：两行。 */
    XBoxLayout     m_row1;        /**< 行1（时间/通道/筛选/行数）。 */
    XBoxLayout     m_row2;        /**< 行2（小数位/导出/视图切换）。 */

    XLabel         m_lbStart;     /**< 「起始时间」。 */
    XDateTimeEdit  m_dtStart;     /**< 起始时间框。 */
    XLabel         m_lbEnd;       /**< 「结束时间」。 */
    XDateTimeEdit  m_dtEnd;       /**< 结束时间框。 */
    XLabel         m_lbChannel;   /**< 「通道」。 */
    XComboBox      m_channel;     /**< 通道下拉。 */
    XComboBox      m_colFilter;   /**< 列筛选下拉。 */

    XPushButton    m_btnFilter;   /**< 筛选。 */
    XPushButton    m_btnClear;    /**< 清除筛选。 */
    XLabel         m_lbRows;      /**< 共 N 行（右靠）。 */

    XLabel         m_lbDecimals;  /**< 「显示小数位数」。 */
    XSpinBox       m_decimals;    /**< 小数位数（0~6）。 */
    XLabel         m_lbMaxPts;    /**< 「每图例最多点」。 */
    XSpinBox       m_maxPoints;   /**< 每图例最多点数（图表降采样上限）。 */
    XComboBox      m_exportFmt;   /**< 导出格式。 */
    XPushButton    m_btnExport;   /**< 导出。 */
    XPushButton    m_btnReload;   /**< 重新加载。 */
    XPushButton    m_btnChart;    /**< 生成折线图（绿底白字）。 */

    XPushButton    m_btnTable;    /**< 表格（checkable）。 */
    XPushButton    m_btnChartTab; /**< 图表（checkable）。 */

    HsfDataBarActionFn m_fn;      /**< 动作回调（借用）。 */
    void*          m_fnUser;      /**< 回调上下文（借用）。 */
    int            m_viewMode;    /**< 当前视图：0=表格 1=图表。 */
    unsigned int   m_features;    /**< 控件特性位掩码（HsfDataBarFeature_*）；
                                       缺省 HsfDataBarFeature_All＝全显。 */
    bool           m_labelsOn;    /**< 说明标签当前是否展开（窄档收标签时为
                                       false）；与特性位掩码取交集，决定小数
                                       位/最多点两张标签的显隐。 */
    int            m_fontPct;     /**< 已应用的字号百分比（实时重放用）。 */
    int            m_adaptW;      /**< 上次分档的可用宽（-1=未算）。 */
    int            m_adaptLevel;  /**< 上次分档结果：0=常规 1=收标签 2=收宽
                                       3=再隐「共 N 行」 4=行1/行2 翻竖排。 */
    int            m_adaptCompact;/**< 上次分档的 compact 位（-1=未算）。 */
    int            m_rowsMinW;    /**< 「共 N 行」按当前文本算出的最小宽
                                       （大数字不再被自身最小宽截字）。 */
    bool           m_suppress;    /**< 编程式 set* 期间抑制变更回调。 */
} HsfDataBar;

/* ==================== 内部小件 ==================== */

#define BAR_CTRL_H 28   /**< 控件统一高（行紧凑；触控 28 下限）。 */

static void barActionSlot(XObject* receiver, XVarList* args);
/* [自适应前置声明] adapt 与 setTotalRows 互调：setTotalRows 按**实际文本**
 * 重算「共 N 行」最小宽后要把档位判据作废，让帧泵下一拍重新分档。 */
static void barSetWidths(HsfDataBar* self, bool tight);
static void barSetRowsVisible(HsfDataBar* self, bool on);

/** @brief 弱色说明标签（13px；Mid 色——VFD 页弱化文案同口径）。 */
static void barInitLabel(XLabel* lb, XWidget* parent, const char* text)
{
    XLabel_init(lb, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)lb);
    XLabel_setText_2(lb, text);
    XLabel_setTextPixelSize(lb, 13);
    XLabel_setAlignment(lb, XAlignment_Left | XAlignment_VCenter);
    XWidget_setForegroundRole((XWidget*)lb, XPaletteColorRole_Mid);
    XWidget_show((XWidget*)lb);
}

/** @brief 通用按钮（高 28；连接统一动作槽）。 */
static void barInitBtn(HsfDataBar* self, XPushButton* btn, XWidget* parent,
                       const char* text)
{
    XPushButton_init(btn, parent, 0);
    HsfTheme_applyDefaultFont((XWidget*)btn);
    XPushButton_setText_2(btn, text);
    XWidget_setFixedHeight((XWidget*)btn, BAR_CTRL_H);
    XObject_connect_1((XObject*)btn,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)self, barActionSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)btn);
}

/** @brief 绿色主按钮样式（生成折线图）。
 *  @note  样式引擎单胜出规则口径（HsfThemeCss.h 文件头）：每个状态
 *         规则自包含整套盒外观；:checked/:disabled 未写则回落全局主题
 *         规则（本钮非 checkable，常态/悬停/按压已全覆盖）。 */
static void barApplyPrimaryStyle(XPushButton* btn)
{
    static const char css[] =
        "XPushButton {"
        " background-color: #2E9E5B;"
        " border: 1px solid #24824A;"
        " border-radius: 5px; }"
        "XPushButton:hover {"
        " background-color: #36AE67;"
        " border: 1px solid #24824A;"
        " border-radius: 5px; }"
        "XPushButton:pressed {"
        " background-color: #27894F;"
        " border: 1px solid #24824A;"
        " border-radius: 5px; }"
        "XPushButton:disabled {"
        " background-color: #7FBC9A;"
        " border: 1px solid #6EA487;"
        " border-radius: 5px; }";
    XString* s = XString_create_utf8(css);
    XPalette p;
    if (s) {
        XWidget_setStyleSheet((XWidget*)btn, s);
        XClassDelete((XClass*)s);
    }
    /* 主题口径：QSS 不消费文本色（引擎自绘文本走调色板）→ 改
     * ButtonText/HighlightedText 角色补白字（#2E9E5B 上白字对比
     * 3.6:1，非正文 ≥3:1 达标）。 */
    p = XWidget_palette((XWidget*)btn);
    XPalette_setColor(&p, XPaletteColorGroup_Active,
                      XPaletteColorRole_ButtonText,
                      XColor_create_rgb(255, 255, 255, 255));
    XPalette_setColor(&p, XPaletteColorGroup_Inactive,
                      XPaletteColorRole_ButtonText,
                      XColor_create_rgb(255, 255, 255, 255));
    XWidget_setPalette((XWidget*)btn, &p);
}

/** @brief 视图切换钮（表格/图表）checkable 化 + 初始选中态样式。 */
static void barInitViewBtn(HsfDataBar* self, XPushButton* btn,
                           XWidget* parent, const char* text, int mode)
{
    barInitBtn(self, btn, parent, text);
    XAbstractButton_setCheckable((XAbstractButton*)btn, true);
    XAbstractButton_setChecked((XAbstractButton*)btn, mode == 0);
}

/** @brief 控件显隐下发（幂等：已是目标态就不调 setVisible）。
 *  @note  XWidget_setVisible **故意**没有同值早退（框架口径：显隐承载
 *         布局激活与脏区补画，见 XWidget.c）——调用点必须自己先比显式隐
 *         藏位，否则每拍重放会把父布局打成永久脏（布局风暴）。 */
static void barSetVisible(XWidget* w, bool visible)
{
    if (!w) return;
    if (XWidget_isHidden(w) == !visible) return; /* 已是目标态。 */
    XWidget_setVisible(w, visible);
}

/** @brief 按特性位掩码裁剪控件（隐藏不在 mask 里的控件 + 配套标签）。
 *  @details 控件与标签成对（标签在控件左邻）：只藏控件留标签会留下孤零
 *           零的字。列筛选/导出格式两个下拉没有独立标签（占位文案在控件
 *           内），随控件一起藏。
 *           隐藏的布局条目按 XLayout 的 empty 条目口径跳过 ⇒ 不占位
 *           （XLayoutItem.c 的 VXWidgetItem_isEmpty：控件隐藏且未保留隐藏
 *           尺寸时条目为空）。
 *           只改显隐，**不发任何动作**（隐藏不是用户操作）。 */
static void barApplyFeatures(HsfDataBar* self)
{
    unsigned int f;
    bool chart;
    bool colFilter;
    bool decimals;
    bool maxPoints;
    bool exportFmt;
    if (!self) return;
    f = self->m_features;
    chart = (f & HsfDataBarFeature_Chart) != 0;
    colFilter = (f & HsfDataBarFeature_ColFilter) != 0;
    decimals = (f & HsfDataBarFeature_Decimals) != 0;
    maxPoints = (f & HsfDataBarFeature_MaxPoints) != 0;
    exportFmt = (f & HsfDataBarFeature_ExportFmt) != 0;
    /* 图表形态：「生成折线图」+「表格/图表」切换钮——无图表形态的页上
     * 「生成折线图」是假按钮（按下只会自己弹回表格），一并藏掉。 */
    barSetVisible((XWidget*)&self->m_btnChart, chart);
    barSetVisible((XWidget*)&self->m_btnTable, chart);
    barSetVisible((XWidget*)&self->m_btnChartTab, chart);
    /* 列筛选下拉（无独立标签）。 */
    barSetVisible((XWidget*)&self->m_colFilter, colFilter);
    /* 导出格式下拉（无独立标签）。 */
    barSetVisible((XWidget*)&self->m_exportFmt, exportFmt);
    /* 小数位 / 每图例最多点：微调框与标签成对；两张标签的显隐还要与
     * 窄档收标签状态取交集（见 barSetLabels）。 */
    barSetVisible((XWidget*)&self->m_decimals, decimals);
    barSetVisible((XWidget*)&self->m_maxPoints, maxPoints);
    barSetVisible((XWidget*)&self->m_lbDecimals,
                  decimals && self->m_labelsOn);
    barSetVisible((XWidget*)&self->m_lbMaxPts,
                  maxPoints && self->m_labelsOn);
}

/* ==================== 槽 ==================== */

/** @brief 统一动作槽：XObject_sender() 分派动作码 → 透传回调。
 *  @note  视图切换钮（checkable）：Qt 口径 checked 随点击自动翻转，
 *         但互斥（点「图表」后「表格」仍保持选中）需手工解除——Qt 里
 *         这对钮通常放 QButtonGroup(exclusive)。框架无分组则在此手工
 *         同步（回调环防护：只翻另一钮，不重入本钮）。 */
static void barActionSlot(XObject* receiver, XVarList* args)
{
    HsfDataBar* self = (HsfDataBar*)receiver;
    XObject* sender;
    int action = -1;
    (void)args;
    if (!self) return;
    sender = XObject_sender((XObject*)self);
    if (sender == (XObject*)&self->m_btnFilter)       action = HsfDataBarAction_Filter;
    else if (sender == (XObject*)&self->m_btnClear)   action = HsfDataBarAction_ClearFilter;
    else if (sender == (XObject*)&self->m_btnExport)  action = HsfDataBarAction_Export;
    else if (sender == (XObject*)&self->m_btnReload)  action = HsfDataBarAction_Reload;
    else if (sender == (XObject*)&self->m_btnChart)   action = HsfDataBarAction_GenerateChart;
    else if (sender == (XObject*)&self->m_btnTable)   action = HsfDataBarAction_ViewTable;
    else if (sender == (XObject*)&self->m_btnChartTab) action = HsfDataBarAction_ViewChart;
    if (action < 0) return;

    if (action == HsfDataBarAction_ViewTable ||
        action == HsfDataBarAction_ViewChart) {
        /* 互斥手工同步（无 QButtonGroup 先翻本钮再对家，防重入环：
         * setChecked 不发 clicked，但发 toggled——本槽只挂 clicked）。 */
        int mode = (action == HsfDataBarAction_ViewChart) ? 1 : 0;
        self->m_viewMode = mode;
        XAbstractButton_setChecked((XAbstractButton*)&self->m_btnTable,
                                   mode == 0);
        XAbstractButton_setChecked((XAbstractButton*)&self->m_btnChartTab,
                                   mode == 1);
    }
    if (self->m_fn)
        self->m_fn(self->m_fnUser, action);
}

/** @brief 下拉/微调框变更槽：通道/列筛选/小数位改动 → 透传回调。
 *  @note  编程式 set* 期间 m_suppress=true，不发回调（防「页面重设
 *         下拉项 → 回调 → 页面再取值」的回调环）。 */
static void barChangeSlot(XObject* receiver, XVarList* args)
{
    HsfDataBar* self = (HsfDataBar*)receiver;
    XObject* sender;
    int action = -1;
    (void)args;
    if (!self || self->m_suppress) return;
    sender = XObject_sender((XObject*)self);
    if (sender == (XObject*)&self->m_channel)
        action = HsfDataBarAction_ChannelChanged;
    else if (sender == (XObject*)&self->m_colFilter)
        action = HsfDataBarAction_ColumnFilterChanged;
    else if (sender == (XObject*)&self->m_decimals)
        action = HsfDataBarAction_DecimalsChanged;
    else if (sender == (XObject*)&self->m_maxPoints)
        action = HsfDataBarAction_MaxPointsChanged;
    if (action < 0) return;
    if (self->m_fn)
        self->m_fn(self->m_fnUser, action);
}

/* ==================== 创建 ==================== */

XWidget* HsfDataBar_create(XWidget* parent)
{
    HsfDataBar* self;
    XDateTime now;
    if (!parent) return NULL;
    self = (HsfDataBar*)XMemory_malloc(sizeof(HsfDataBar),
                                       XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(HsfDataBar));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true); /* 整块随主窗级联释放。 */
    self->m_adaptW = -1;          /* 分档缓存：未算过。 */
    self->m_adaptCompact = -1;
    self->m_rowsMinW = 88;        /* 「共 N 行」文本驱动最小宽（setTotalRows 重算）。 */
    self->m_features = HsfDataBarFeature_All; /* 缺省全开＝行为与旧版逐字节一致。 */
    self->m_labelsOn = true;      /* 说明标签默认展开（窄档才收）。 */
    HsfTheme_applyDefaultFont(&self->m_base);

    /* ---- 行 1：起始/结束时间 + 通道 + 列筛选 + 筛选/清除 + 行数 ---- */
    barInitLabel(&self->m_lbStart, &self->m_base, T_START);
    XDateTimeEdit_init(&self->m_dtStart, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_dtStart);
    XDateTimeEdit_setDisplayFormat(&self->m_dtStart, "yyyy-MM-dd HH:mm:ss");
    /* [日历弹窗] 对标 QDateTimeEdit::calendarPopup=true：点控件右侧箭头
     * 弹日历（框架 XDateTimeEdit 已实现弹层容器/贴边翻转/点击外部与
     * Esc 收起/选中日期回写编辑框，见 XDateTimeEdit.c 的
     * xdt_popupShow / VXDateTimePopup_mousePressEvent / VXDateTimeEdit_
     * keyPressEvent）。不开启就只有键盘步进，现场没法点日期。 */
    XDateTimeEdit_setCalendarPopup(&self->m_dtStart, true);
    XWidget_setFixedHeight((XWidget*)&self->m_dtStart, BAR_CTRL_H);
    XWidget_setMinimumWidth((XWidget*)&self->m_dtStart, 210);
    XWidget_show((XWidget*)&self->m_dtStart);

    barInitLabel(&self->m_lbEnd, &self->m_base, T_END);
    XDateTimeEdit_init(&self->m_dtEnd, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_dtEnd);
    XDateTimeEdit_setDisplayFormat(&self->m_dtEnd, "yyyy-MM-dd HH:mm:ss");
    XDateTimeEdit_setCalendarPopup(&self->m_dtEnd, true);
    XWidget_setFixedHeight((XWidget*)&self->m_dtEnd, BAR_CTRL_H);
    XWidget_setMinimumWidth((XWidget*)&self->m_dtEnd, 210);
    XWidget_show((XWidget*)&self->m_dtEnd);
    /* 缺省区间：过去 24h ~ 现在（数据接入前的合理占位；Qt 页惯用）。
     * [2026-11-07 筛选无效根因] 此前只给结束框赋值、起始框停在
     * XDateTimeEdit_init 的"页面创建那一刻"⇒ 点「筛选」时时间窗宽度
     * ≈ 0（loMs == hiMs），服务端 `ts >= lo AND ts <= hi` 一行都查不到，
     * 界面上就是「共 0 条」，用户口径即"筛选基本没用"。两端都赋值。 */
    now = XDateTime_currentDateTime();
    XDateTimeEdit_setDateTime(&self->m_dtEnd, &now);
    {
        const XDateTime lo = XDateTime_addSecs(&now, -24 * 3600);
        XDateTimeEdit_setDateTime(&self->m_dtStart, &lo);
    }

    barInitLabel(&self->m_lbChannel, &self->m_base, T_CHANNEL);
    XComboBox_init(&self->m_channel, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_channel);
    XComboBox_addItem_2(&self->m_channel, T_ALL_CHANNELS);
    XComboBox_setPlaceholderText_2(&self->m_channel, T_CHANNEL);
    XWidget_setFixedHeight((XWidget*)&self->m_channel, BAR_CTRL_H);
    XWidget_setMinimumWidth((XWidget*)&self->m_channel, 96);
    XWidget_show((XWidget*)&self->m_channel);

    XComboBox_init(&self->m_colFilter, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_colFilter);
    XComboBox_setPlaceholderText_2(&self->m_colFilter, T_COLFILTER);
    XWidget_setFixedHeight((XWidget*)&self->m_colFilter, BAR_CTRL_H);
    XWidget_setMinimumWidth((XWidget*)&self->m_colFilter, 88);
    XWidget_show((XWidget*)&self->m_colFilter);

    barInitBtn(self, &self->m_btnFilter, &self->m_base, T_FILTER);
    barInitBtn(self, &self->m_btnClear, &self->m_base, T_CLEAR);

    /* 行数统计（右靠；粗体白底黑字同表格页标题口径）。 */
    XLabel_init(&self->m_lbRows, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_lbRows);
    XLabel_setText_2(&self->m_lbRows, "共 0 行"); /* 共 0 行 */
    XLabel_setTextPixelSize(&self->m_lbRows, 13);
    XLabel_setAlignment(&self->m_lbRows,
                        XAlignment_Left | XAlignment_VCenter);
    XWidget_setMinimumWidth((XWidget*)&self->m_lbRows, 88);
    XWidget_show((XWidget*)&self->m_lbRows);

    /* ---- 行 2：小数位 + 导出格式 + 导出/重载/折线图 + 视图切换 ---- */
    barInitLabel(&self->m_lbDecimals, &self->m_base, T_DECIMALS);
    XSpinBox_init(&self->m_decimals, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_decimals);
    XSpinBox_setRange(&self->m_decimals, 0, 6);
    XSpinBox_setValue(&self->m_decimals, 2);
    XWidget_setFixedHeight((XWidget*)&self->m_decimals, BAR_CTRL_H);
    XWidget_setFixedWidth((XWidget*)&self->m_decimals, 64);
    XWidget_show((XWidget*)&self->m_decimals);

    /* 每图例最多可见点数（图表降采样上限；超限等距抽样）。 */
    barInitLabel(&self->m_lbMaxPts, &self->m_base, T_MAXPTS);
    XSpinBox_init(&self->m_maxPoints, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_maxPoints);
    XSpinBox_setRange(&self->m_maxPoints, HSFDATABAR_MAX_POINTS_MIN,
                      HSFDATABAR_MAX_POINTS_MAX);
    XSpinBox_setSingleStep(&self->m_maxPoints, 100);
    XSpinBox_setValue(&self->m_maxPoints, HSFDATABAR_MAX_POINTS_DEFAULT);
    XWidget_setFixedHeight((XWidget*)&self->m_maxPoints, BAR_CTRL_H);
    XWidget_setFixedWidth((XWidget*)&self->m_maxPoints, 84);
    XWidget_show((XWidget*)&self->m_maxPoints);

    XComboBox_init(&self->m_exportFmt, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_exportFmt);
    XComboBox_addItem_2(&self->m_exportFmt, T_FMT_XLSX);
    XComboBox_addItem_2(&self->m_exportFmt, T_FMT_CSV);
    XWidget_setFixedHeight((XWidget*)&self->m_exportFmt, BAR_CTRL_H);
    XWidget_setMinimumWidth((XWidget*)&self->m_exportFmt, 110);
    XWidget_show((XWidget*)&self->m_exportFmt);

    barInitBtn(self, &self->m_btnExport, &self->m_base, T_EXPORT);
    barInitBtn(self, &self->m_btnReload, &self->m_base, T_RELOAD);
    barInitBtn(self, &self->m_btnChart, &self->m_base, T_CHART);
    barApplyPrimaryStyle(&self->m_btnChart);

    barInitViewBtn(self, &self->m_btnTable, &self->m_base, T_TABLE, 0);
    barInitViewBtn(self, &self->m_btnChartTab, &self->m_base, T_CHARTTAB, 1);

    /* ---- 两行布局 ---- */
    XBoxLayout_init(&self->m_row1, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_row1, 8);
    XBoxLayout_addWidget(&self->m_row1, (XWidget*)&self->m_lbStart);
    XBoxLayout_addWidget(&self->m_row1, (XWidget*)&self->m_dtStart);
    XBoxLayout_addWidget(&self->m_row1, (XWidget*)&self->m_lbEnd);
    XBoxLayout_addWidget(&self->m_row1, (XWidget*)&self->m_dtEnd);
    XBoxLayout_addWidget(&self->m_row1, (XWidget*)&self->m_lbChannel);
    XBoxLayout_addWidget(&self->m_row1, (XWidget*)&self->m_channel);
    XBoxLayout_addWidget(&self->m_row1, (XWidget*)&self->m_colFilter);
    XBoxLayout_addWidget(&self->m_row1, (XWidget*)&self->m_btnFilter);
    XBoxLayout_addWidget(&self->m_row1, (XWidget*)&self->m_btnClear);
    XBoxLayout_addStretch(&self->m_row1, 1);
    XBoxLayout_addWidget(&self->m_row1, (XWidget*)&self->m_lbRows);

    XBoxLayout_init(&self->m_row2, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_row2, 8);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_lbDecimals);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_decimals);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_lbMaxPts);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_maxPoints);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_exportFmt);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_btnExport);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_btnReload);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_btnChart);
    /* [页级分页控件并入点] 页面经 HsfDataBar_attachRow2 把控件插在**本条
     * 伸展条之后、两个视图切换钮之前**（插入点 = 条目数-2，见 attachRow2）：
     * 伸展条吃掉左侧剩余宽度，页级控件与「表格/图表」钮连成右靠段，视图
     * 切换钮仍在最右。attachRow2 用 insertWidget 在末尾两项之前插入，条目
     * 顺序稳定且幂等（先 remove 旧的再插）。 */
    XBoxLayout_addStretch(&self->m_row2, 1);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_btnTable);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_btnChartTab);

    XBoxLayout_init(&self->m_root, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_root, 0, 0, 0, 0);
    XLayout_setSpacing((XLayout*)&self->m_root, 6);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_root);
    XBoxLayout_addLayout(&self->m_root, (XLayout*)&self->m_row1);
    XBoxLayout_addLayout(&self->m_root, (XLayout*)&self->m_row2);

    /* 下拉/微调框变更 → 透传（页面即时重排显示；见 barChangeSlot）。 */
    XObject_connect_1((XObject*)&self->m_channel,
                      XSignal(XComboBox_currentIndexChanged_signal),
                      (XObject*)self, barChangeSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_colFilter,
                      XSignal(XComboBox_currentIndexChanged_signal),
                      (XObject*)self, barChangeSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_decimals,
                      XSignal(XSpinBox_valueChanged_signal),
                      (XObject*)self, barChangeSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)&self->m_maxPoints,
                      XSignal(XSpinBox_valueChanged_signal),
                      (XObject*)self, barChangeSlot,
                      XConnectionType_Direct);

    XWidget_show(&self->m_base);
    return &self->m_base;
}

/* ==================== API ==================== */

void HsfDataBar_setActionCallback(XWidget* bar, HsfDataBarActionFn fn,
                                  void* userData)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    if (!self) return;
    self->m_fn = fn;
    self->m_fnUser = userData;
}

void HsfDataBar_setFeatures(const XWidget* bar, unsigned int features)
{
    /* [const 形参] 首参按「只读视图」收 const 以免调用方误以为会改控件
     * 几何；内部要写 m_features，故显式去 const（工具条是堆对象，调用
     * 方本就是可写的那份）。 */
    HsfDataBar* self = (HsfDataBar*)(void*)bar;
    if (!self) return;
    self->m_features = features;
    barApplyFeatures(self);
    /* 裁剪后两行的最小宽需求变了 ⇒ 自适应分档结论作废，下一拍 adapt
     * 按新口径重算（否则行宽仍按旧需求判档，右端可能照样被裁）。 */
    self->m_adaptW = -1;
}

void HsfDataBar_setChannelItems(XWidget* bar, const char* const* items,
                                int count)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    int i;
    if (!self) return;
    self->m_suppress = true;
    XComboBox_clear(&self->m_channel);
    if (items && count > 0)
        for (i = 0; i < count; ++i)
            XComboBox_addItem_2(&self->m_channel,
                                items[i] ? items[i] : "");
    else
        XComboBox_addItem_2(&self->m_channel, T_ALL_CHANNELS);
    self->m_suppress = false;
}

int HsfDataBar_channelIndex(const XWidget* bar)
{
    const HsfDataBar* self = (const HsfDataBar*)bar;
    return self ? XComboBox_currentIndex(&self->m_channel) : -1;
}

void HsfDataBar_setChannelIndex(XWidget* bar, int index)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    if (!self) return;
    self->m_suppress = true;
    XComboBox_setCurrentIndex(&self->m_channel, index);
    self->m_suppress = false;
}

void HsfDataBar_setColumnFilterItems(XWidget* bar, const char* const* items,
                                     int count)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    int i;
    if (!self) return;
    self->m_suppress = true;
    XComboBox_clear(&self->m_colFilter);
    if (items)
        for (i = 0; i < count; ++i)
            XComboBox_addItem_2(&self->m_colFilter,
                                items[i] ? items[i] : "");
    self->m_suppress = false;
    /* 空表=留空（占位文本「列筛选」可见）。 */
}

int HsfDataBar_columnFilterIndex(const XWidget* bar)
{
    const HsfDataBar* self = (const HsfDataBar*)bar;
    return self ? XComboBox_currentIndex(&self->m_colFilter) : -1;
}

void HsfDataBar_setColumnFilterIndex(XWidget* bar, int index)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    if (!self) return;
    self->m_suppress = true;
    XComboBox_setCurrentIndex(&self->m_colFilter, index);
    self->m_suppress = false;
}

void HsfDataBar_setStartDateTime(XWidget* bar, const XDateTime* dt)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    if (!self || !dt) return;
    XDateTimeEdit_setDateTime(&self->m_dtStart, dt);
}

void HsfDataBar_setEndDateTime(XWidget* bar, const XDateTime* dt)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    if (!self || !dt) return;
    XDateTimeEdit_setDateTime(&self->m_dtEnd, dt);
}

XDateTime HsfDataBar_startDateTime(const XWidget* bar)
{
    const HsfDataBar* self = (const HsfDataBar*)bar;
    if (!self) {
        XDateTime bad;
        bad.m_date.m_jd = -1;
        bad.m_time.m_msecs = -1;
        return bad;
    }
    return *XDateTimeEdit_dateTime(&self->m_dtStart);
}

XDateTime HsfDataBar_endDateTime(const XWidget* bar)
{
    const HsfDataBar* self = (const HsfDataBar*)bar;
    if (!self) {
        XDateTime bad;
        bad.m_date.m_jd = -1;
        bad.m_time.m_msecs = -1;
        return bad;
    }
    return *XDateTimeEdit_dateTime(&self->m_dtEnd);
}

int HsfDataBar_decimalDigits(const XWidget* bar)
{
    const HsfDataBar* self = (const HsfDataBar*)bar;
    return self ? XSpinBox_value(&self->m_decimals) : -1;
}

void HsfDataBar_setDecimalDigits(XWidget* bar, int digits)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    if (!self) return;
    self->m_suppress = true;
    XSpinBox_setValue(&self->m_decimals, digits);
    self->m_suppress = false;
}

int HsfDataBar_exportFormatIndex(const XWidget* bar)
{
    const HsfDataBar* self = (const HsfDataBar*)bar;
    return self ? XComboBox_currentIndex(&self->m_exportFmt) : -1;
}

void HsfDataBar_setExportFormatIndex(XWidget* bar, int index)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    if (!self) return;
    if (index < 0 || index > 1) return;
    XComboBox_setCurrentIndex(&self->m_exportFmt, index);
}

void HsfDataBar_setTotalRows(XWidget* bar, long long rows)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    char buf[48];
    XFont font;
    int textW;
    if (!self) return;
    if (rows < 0) rows = 0;
    snprintf(buf, sizeof(buf), T_ROWS, rows);
    XLabel_setText_2(&self->m_lbRows, buf);
    /* [最小宽按实际文本 2026-11-08] 极窄档把「共 N 行」压到 64px（那是按
     * "共 0 行" 定的），5 位数的「共 11020 行」需要 ~78px ⇒ 标签自身把末尾
     * 「行」截掉。这里按 **当前文本实测宽**（+6 内边距）记下需求，由
     * barSetWidths 取 max(档位基准宽, 本值)；若仍放不下，adapt 会升到
     * 3 档直接隐藏这颗冗余计数（历史页状态行已有「共 N 条 … 库内共 N 条」）。 */
    font = XWidget_font((XWidget*)&self->m_lbRows);
    textW = XPainter_textWidth(&font, buf) + 6;
    XClassDeinit(&font);
    if (textW < 64) textW = 64;
    if (textW > 160) textW = 160;
    if (textW != self->m_rowsMinW) {
        self->m_rowsMinW = textW;
        barSetWidths(self, self->m_adaptLevel >= 2);
        self->m_adaptW = -1;   /* 文本变了 ⇒ 下一拍重新分档（可能升 3/4 档）。 */
    }
}

int HsfDataBar_maxPoints(const XWidget* bar)
{
    const HsfDataBar* self = (const HsfDataBar*)bar;
    int v;
    if (!self) return HSFDATABAR_MAX_POINTS_DEFAULT;
    v = XSpinBox_value(&self->m_maxPoints);
    if (v < HSFDATABAR_MAX_POINTS_MIN) v = HSFDATABAR_MAX_POINTS_MIN;
    if (v > HSFDATABAR_MAX_POINTS_MAX) v = HSFDATABAR_MAX_POINTS_MAX;
    return v;
}

void HsfDataBar_setMaxPoints(XWidget* bar, int points)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    if (!self) return;
    if (points < HSFDATABAR_MAX_POINTS_MIN) points = HSFDATABAR_MAX_POINTS_MIN;
    if (points > HSFDATABAR_MAX_POINTS_MAX) points = HSFDATABAR_MAX_POINTS_MAX;
    self->m_suppress = true;
    XSpinBox_setValue(&self->m_maxPoints, points);
    self->m_suppress = false;
}

/* ---- 行 2 页级控件挂接（分页/关键字） ---- */

/** @brief 从行 2 摘掉一个控件条目（不删控件；控件仍归原父对象）。 */
static void barDetachOne(XBoxLayout* row, XWidget* w)
{
    if (!row || !w) return;
    if (XLayout_indexOf((const XLayout*)row, w) >= 0)
        XLayout_removeWidget((XLayout*)row, w);
}

/* [detachRow2 已删 2026-11-08] 原 HsfDataBar_detachRow2 全库零调用点。
 * 它想防的是"工具条比页级控件活得久 ⇒ 布局里留悬垂控件条目"，但挂接
 * 实现根本没有这种生命周期：attachRow2 走 XBoxLayout_insertWidget →
 * XLayout_linkItem（沿布局链解析宿主工具条后 `XWidget_setParent_2`），
 * 把控件 reparent 成工具条的子对象；此后页面只持有工具条一个子树，
 * 工具条析构时 XObject 的子对象级联会把那些控件一并销毁，中间不存在
 * "控件已亡、条目还在"的窗口。既然没有任何页面需要"留控件、先摘条目"
 * 的第二种用法，这个零调用、零测试的公开接口只会让人误以为存在该用法，
 * 故按死代码清除（attachRow2 的幂等前缀仍复用 barDetachOne）。 */

void HsfDataBar_attachRow2(XWidget* bar, XWidget** widgets, int count,
                           XWidget** rightWidgets, int rightCount)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    int i;
    int idx;
    if (!self) return;
    /* [幂等] 先摘旧条目（重复 attach 不会叠条目、也不会把控件留在
     * 两个布局里；拔条目不改控件父对象）。 */
    for (i = 0; i < count; ++i)
        if (widgets && widgets[i]) barDetachOne(&self->m_row2, widgets[i]);
    for (i = 0; i < rightCount; ++i)
        if (rightWidgets && rightWidgets[i])
            barDetachOne(&self->m_row2, rightWidgets[i]);
    /* 插在"视图切换钮之前"：行 2 尾段为 [stretch][表格][图表]，故插入点
     * = 条目数 - 2（stretch 与两钮恒在，见 HsfDataBar_create）。 */
    idx = XLayout_count_base((const XLayout*)&self->m_row2);
    if (idx < 2) idx = 0;
    else idx -= 2;
    for (i = 0; i < count; ++i) {
        if (!widgets || !widgets[i]) continue;
        XBoxLayout_insertWidget(&self->m_row2, idx, widgets[i], 0);
        ++idx;
    }
    for (i = 0; i < rightCount; ++i) {
        if (!rightWidgets || !rightWidgets[i]) continue;
        XBoxLayout_insertWidget(&self->m_row2, idx, rightWidgets[i], 0);
        ++idx;
    }
    XLayout_activate((XLayout*)&self->m_row2);
    XLayout_activate((XLayout*)&self->m_root);
}

int HsfDataBar_viewMode(const XWidget* bar)
{
    const HsfDataBar* self = (const HsfDataBar*)bar;
    return self ? self->m_viewMode : 0;
}

void HsfDataBar_setViewMode(XWidget* bar, int mode)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    if (!self || (mode != 0 && mode != 1)) return;
    self->m_viewMode = mode;
    XAbstractButton_setChecked((XAbstractButton*)&self->m_btnTable,
                               mode == 0);
    XAbstractButton_setChecked((XAbstractButton*)&self->m_btnChartTab,
                               mode == 1);
}

/** @brief 字号实时重放：标签绝对值、输入/按钮按新旧比例，控件高同步。 */
void HsfDataBar_applyFont(XWidget* bar, int pct)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    XWidget* ws[14];
    int i;
    int oldPct;
    int h;
    if (!self || pct <= 0) return;
    oldPct = self->m_fontPct > 0 ? self->m_fontPct : 100;
    h = BAR_CTRL_H * pct / 100;
    XLabel_setTextPixelSize(&self->m_lbStart, 13 * pct / 100);
    XLabel_setTextPixelSize(&self->m_lbEnd, 13 * pct / 100);
    XLabel_setTextPixelSize(&self->m_lbChannel, 13 * pct / 100);
    XLabel_setTextPixelSize(&self->m_lbRows, 13 * pct / 100);
    XLabel_setTextPixelSize(&self->m_lbDecimals, 13 * pct / 100);
    XLabel_setTextPixelSize(&self->m_lbMaxPts, 13 * pct / 100);
    ws[0] = (XWidget*)&self->m_dtStart;
    ws[1] = (XWidget*)&self->m_dtEnd;
    ws[2] = (XWidget*)&self->m_channel;
    ws[3] = (XWidget*)&self->m_colFilter;
    ws[4] = (XWidget*)&self->m_btnFilter;
    ws[5] = (XWidget*)&self->m_btnClear;
    ws[6] = (XWidget*)&self->m_decimals;
    ws[7] = (XWidget*)&self->m_maxPoints;
    ws[8] = (XWidget*)&self->m_exportFmt;
    ws[9] = (XWidget*)&self->m_btnExport;
    ws[10] = (XWidget*)&self->m_btnReload;
    ws[11] = (XWidget*)&self->m_btnChart;
    ws[12] = (XWidget*)&self->m_btnTable;
    ws[13] = (XWidget*)&self->m_btnChartTab;
    for (i = 0; i < 14; ++i) {
        XWidget_setFixedHeight(ws[i], h);
        if (pct != oldPct) {
            XFont src = XWidget_font(ws[i]);
            int px = XFont_pixelSize(&src);
            if (px > 0) {
                XFont f;
                XFont_init(&f);
                XClassCopy(&f, &src);
                XFont_setPixelSize(&f, (px * pct + oldPct / 2) / oldPct);
                XWidget_setFont(ws[i], &f);
                XClassDeinit(&f);
            }
            XClassDeinit(&src);
        }
    }
    self->m_fontPct = pct;
    /* 字号变了→各控件最小尺寸提示全变→分档结论作废，下一拍 adapt 重算。 */
    self->m_adaptW = -1;
}

/** @brief 两行的**最小宽度需求**（像素；已含各条目自身 minimumSize）。
 *  @details 用布局自己的 minimumSize 累加值，而不是这里另抄一份宽度常量
 *           ——控件最小宽改一处即生效，不会两处漂移。 */
static int barMinWidthOf(const XBoxLayout* row)
{
    XSize size;
    if (!row) return 0;
    size = XLayoutItem_minimumSize_base((const XLayoutItem*)row);
    return size.width > 0 ? size.width : 0;
}

/** @brief **真实可视宽度** = 自身与全部祖先控件宽度的最小值。
 *  @details 不能用工具条自身宽度当可用宽：页根装在滚壳里
 *           （XScrollArea widgetResizable），当页的最小宽度超过视口时，
 *           滚壳把**内容**撑到最小宽（Qt 同款 p.expandedTo(m)），于是
 *           工具条量到的自己宽度 = 已撑开的 1022，而真正能看见的只有
 *           视口 ~853 —— 拿前者判断永远「够用」，右端照样被裁（日志页
 *           1024 宽窗实测）。祖先链里视口/宿主这些容器宽度才是边界，
 *           取整链最小值即得可视宽（宽窗时链上最小值就是视口宽，等价）。 */
static int barVisibleWidth(const XWidget* self)
{
    const XObject* node = (const XObject*)self;
    int minW = 0;
    while (node) {
        const XWidget* w = (const XWidget*)node;
        const int ww = XWidget_width(w);
        if (ww > 0 && (minW <= 0 || ww < minW)) minW = ww;
        node = XObject_parent((XObject*)node);
    }
    return minW;
}

/** @brief 说明标签显隐（幂等；窄档收标签）。
 *  @note  小数位/最多点两张标签还受**特性位掩码**管：窄档收标签是
 *         「宽不够」（临时），特性位关掉是「本页用不上」（永久）——两者
 *         取交集：特性位关掉的标签，窄档也不会把它放回来（否则每拍
 *         adapt 又显出来，裁剪失效）。 */
static void barSetLabels(HsfDataBar* self, bool on)
{
    if (!self) return;
    self->m_labelsOn = on;
    barSetVisible((XWidget*)&self->m_lbStart, on);
    barSetVisible((XWidget*)&self->m_lbEnd, on);
    barSetVisible((XWidget*)&self->m_lbChannel, on);
    barSetVisible((XWidget*)&self->m_lbDecimals,
                  on && (self->m_features & HsfDataBarFeature_Decimals) != 0);
    barSetVisible((XWidget*)&self->m_lbMaxPts,
                  on && (self->m_features & HsfDataBarFeature_MaxPoints) != 0);
}

/** @brief 「共 N 行」计数显隐（幂等；3 档窄到放不下时隐去）。 */
static void barSetRowsVisible(HsfDataBar* self, bool on)
{
    if (!self) return;
    barSetVisible((XWidget*)&self->m_lbRows, on);
}

/** @brief 控件标称最小宽两级：常规 / 极窄（幂等；setMinimumWidth 同值早退）。
 *  @details 时间框显示 `2026-11-07 12:15:56`（19 字符）+ 日历箭头，168 是
 *           能完整显示该格式的下限；下拉/导出格式同口径按文案缩。
 *           极窄档同时把行间距 8→4（行 2 有 12 个条目 = 11 个间距，省 ~44px
 *           ——比继续压控件宽更好：间距是纯留白，压它不影响可读性）。 */
static void barSetWidths(HsfDataBar* self, bool tight)
{
    XWidget_setMinimumWidth((XWidget*)&self->m_dtStart, tight ? 168 : 210);
    XWidget_setMinimumWidth((XWidget*)&self->m_dtEnd, tight ? 168 : 210);
    XWidget_setMinimumWidth((XWidget*)&self->m_channel, tight ? 80 : 96);
    XWidget_setMinimumWidth((XWidget*)&self->m_colFilter, tight ? 72 : 88);
    XWidget_setMinimumWidth((XWidget*)&self->m_exportFmt, tight ? 84 : 110);
    /* 两个微调框是固定宽（64/84）：极窄档各让 8px（值仍完整显示）。 */
    XWidget_setFixedWidth((XWidget*)&self->m_decimals, tight ? 56 : 64);
    XWidget_setFixedWidth((XWidget*)&self->m_maxPoints, tight ? 76 : 84);
    /* 「共 N 行」最小宽 = max(档位基准, 当前文本实测宽)：文本宽由
     * setTotalRows 记入 m_rowsMinW，大数字不再被自身最小宽截字。 */
    {
        const int baseW = tight ? 64 : 88;
        XWidget_setMinimumWidth((XWidget*)&self->m_lbRows,
                                self->m_rowsMinW > baseW ? self->m_rowsMinW
                                                         : baseW);
    }
    /* [间距同值早退在这里手工做] XLayout_setSpacing 无同值闸（无条件
     * invalidate），帧泵每拍调两次会把布局打成永久脏 → 每帧重排重绘。 */
    if (XLayout_spacing((const XLayout*)&self->m_row1) != (tight ? 4 : 8)) {
        XLayout_setSpacing((XLayout*)&self->m_row1, tight ? 4 : 8);
        XLayout_setSpacing((XLayout*)&self->m_row2, tight ? 4 : 8);
    }
}

/** @brief 行1/行2 横/竖排翻向（第 4 档专用；幂等，稳态零副作用）。
 *  @details 控件不搬家，只翻容器方向（HsfMonitorVfd_adapt 控制行翻排
 *           同款仓库惯用法）；两行尾部的伸展条是 magic 空白，
 *           XBoxLayout_setDirection 翻向时自动把 Expanding 转到新主轴，
 *           且**同值早退**（方向没变直接 return，不失效布局）——帧泵
 *           每拍重放安全。宽档复原 LeftToRight：跨档 resize 不残留
 *           （HsfWindowPage 窄宽分档「宽档恒复原」同口径）。 */
static void barSetVertical(HsfDataBar* self, bool on)
{
    const XBoxLayoutDirection dir = on ? XBoxLayoutDirection_TopToBottom
                                       : XBoxLayoutDirection_LeftToRight;
    XBoxLayout_setDirection(&self->m_row1, dir);
    XBoxLayout_setDirection(&self->m_row2, dir);
}

void HsfDataBar_adapt(XWidget* bar, bool compact)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    int avail;
    int need;
    int level;
    int need1H = -1; /* 第 4 档判据值：行 1 横排最小宽（翻转前量得）。 */
    if (!self) return;
    /* [分档按**实际可用宽度**，不只看形态位 2026-11-08] 原实现只看
     * layoutMode（竖屏形态）决定收不收说明标签：1280 宽窗算「宽屏」，
     * 标签全显 → 两行最小宽 1016/1183 而可用宽只有 ~1070，行 1「共 N 行」
     * 被截、行 2「关键字」框整个伸出窗外（1600 才够）。
     * 现在按工具条自身宽度与两行最小宽需求比档，逐级收紧：
     *   ⓪ 标签全显 + 常规最小宽；
     *   ① 收起全部说明标签；
     *   ② 再收控件自身最小宽 + 行间距；
     *   ③ 仍放不下（1024 宽窗 + 导航栏实测 need≈816 > avail≈808）⇒
     *      **隐去「共 N 行」**：它是两行里唯一的纯只读计数，历史页状态
     *      行已给出「共 N 条 … 库内共 N 条」；继续压缩只会把末尾字截掉。
     *   ④ 连行 1 双日期框都放不下（**行 1 单独**横排最小宽 > avail；
     *      480 手机竖屏 avail≈456 << 行1最小宽，滚壳横向 AlwaysOff，
     *      硬挤就是右端整段看不见）⇒ **行1/行2 各自翻竖排**：控件不
     *      搬家只翻容器方向，行宽降到最宽单项（≈双日期框 168~280），
     *      行高换行数由竖屏高度兜底。
     * [按宽度缓存分档结果] 帧泵每拍调用本函数；若每次都从头「先常规、量、
     * 再收紧」，会每拍把最小宽/间距来回改两遍 ⇒ 布局永久脏、每帧重排。
     * 缓存 (avail, compact) 命中即直接套用上次档位，稳态零副作用。
     * [第 4 档判据必须横排口径 2026-11-09] 翻竖排后布局 minimumSize 变
     * 竖排口径（宽=最宽单项），拿它再判「放不放得下」会横竖来回翻、每拍
     * 两翻布局永久脏 ⇒ 档位缓存命中只按缓存档位重放（零量宽零翻向），
     * 未命中先 barSetVertical(false) 复原横排再量。 */
    avail = barVisibleWidth(&self->m_base);
    if (compact || avail <= 0) {
        /* 形态位判定（竖屏）或几何未定：按原口径收标签；avail 已知且
         * 连行 1 双日期框都放不下时再翻竖排（第 4 档——480 手机竖屏
         * 窗宽 <860 恒走本分支，只在宽档级联里补 ④ 够不着真机）。
         * [顺带对齐档位账本] 本分支原套常规宽、记 level 2，与
         * setTotalRows 的 barSetWidths(m_adaptLevel>=2) 重放口径（紧缩）
         * 互相打架；现按账本 level≥2 ⇒ 紧缩宽，两处不再来回改。 */
        int lvl;
        bool vertical;
        if (self->m_adaptW == avail &&
            self->m_adaptCompact == (compact ? 1 : 0) &&
            self->m_adaptLevel >= 0) {
            /* 同值早退：稳态只重放缓存档位（零量宽、零翻向）。 */
            lvl = self->m_adaptLevel;
            vertical = lvl >= 4;
        } else {
            barSetVertical(self, false); /* 量横排口径，先复原。 */
            barSetLabels(self, !compact);
            barSetWidths(self, compact); /* level≥2 ⇒ 紧缩宽（账本口径）。 */
            barSetRowsVisible(self, !compact);
            /* [第 4 档判据] 行 1 单独横排最小宽 > avail：连双日期框
             * 都放不下，横排必裁（判据口径与宽档级联的 ④ 一致）。 */
            vertical = compact && avail > 0 &&
                       barMinWidthOf(&self->m_row1) > avail;
            lvl = vertical ? 4 : (compact ? 2 : 0);
        }
        barSetVertical(self, vertical);
        barSetLabels(self, !compact);
        barSetWidths(self, lvl >= 2);
        barSetRowsVisible(self, !compact);
        self->m_adaptW = avail;
        self->m_adaptCompact = compact ? 1 : 0;
        self->m_adaptLevel = lvl;
        return;
    }
    if (self->m_adaptW == avail && self->m_adaptCompact == 0 &&
        self->m_adaptLevel >= 0) {
        barSetLabels(self, self->m_adaptLevel == 0);
        barSetWidths(self, self->m_adaptLevel >= 2);
        barSetRowsVisible(self, self->m_adaptLevel < 3);
        barSetVertical(self, self->m_adaptLevel >= 4);
        return;
    }
    barSetLabels(self, true);
    barSetWidths(self, false);
    barSetRowsVisible(self, true);
    barSetVertical(self, false); /* 宽档基线含方向：复原横排再量（不残留）。 */
    level = 0;
    need = barMinWidthOf(&self->m_row1);
    {
        const int need2 = barMinWidthOf(&self->m_row2);
        if (need2 > need) need = need2;
    }
    if (need > avail) {
        barSetLabels(self, false);
        level = 1;
        need = barMinWidthOf(&self->m_row1);
        {
            const int need2 = barMinWidthOf(&self->m_row2);
            if (need2 > need) need = need2;
        }
        if (need > avail) {
            barSetWidths(self, true);
            level = 2;
            need = barMinWidthOf(&self->m_row1);
            {
                const int need2 = barMinWidthOf(&self->m_row2);
                if (need2 > need) need = need2;
            }
            if (need > avail) {
                barSetRowsVisible(self, false);
                level = 3;
                /* [第 4 档：竖排档 2026-11-09] 判据换**行 1 单独**的横排
                 * 最小宽（双日期框是最宽段，它放不下横排就全放不下）：
                 * 480 手机竖屏 avail≈456 << 行1最小宽，滚壳横向
                 * AlwaysOff，硬挤右端整段看不见 ⇒ 两行各自翻 TopToBottom
                 * （控件不搬家只翻向，翻向后行宽=最宽单项 ≤ avail）。 */
                need1H = barMinWidthOf(&self->m_row1);
                if (need1H > avail) {
                    barSetVertical(self, true);
                    level = 4;
                }
            }
        }
    }
    self->m_adaptW = avail;
    self->m_adaptLevel = level;
    if (XSystem_environment("XHSF_BAR_METRICS")) {
        int n1 = barMinWidthOf(&self->m_row1);
        int n2 = barMinWidthOf(&self->m_row2);
        XObject* p = XObject_parent((XObject*)self);
        int pw = p ? XWidget_width((XWidget*)p) : -1;
        int winW = 0;
        XWidget* t = &self->m_base;
        /* [档名+判据值 2026-11-09 补第 4 档] 竖排档翻转后布局 minimumSize
         * 已变竖排口径（宽=最宽单项），故 need1/need2 在 level 4 是竖排行
         * 宽；判据用的横排行 1 最小宽另印 need1H（翻转前量得，其余档=n1）。 */
        static const char* const kLevelName[5] = {
            "wide", "noLabels", "tight", "noRows", "vertical" };
        const char* lname = (level >= 0 && level < 5)
                                ? kLevelName[level] : "?";
        const int crit1 = (level == 4 && need1H >= 0) ? need1H : n1;
        while (XObject_parent((XObject*)t) &&
               !((XWidget*)XObject_parent((XObject*)t))->m_isWindow)
            t = (XWidget*)XObject_parent((XObject*)t);
        if (XObject_parent((XObject*)t))
            winW = XWidget_width((XWidget*)XObject_parent((XObject*)t));
        XPrintf("[bar-metrics] win=%d parentW=%d avail=%d level=%d(%s) "
                "need1=%d need2=%d need1H=%d\n",
                winW, pw, avail, level, lname, n1, n2, crit1);
    }
}

#endif /* 守卫 */
