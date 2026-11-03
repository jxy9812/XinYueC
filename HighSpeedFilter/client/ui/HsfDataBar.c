/**
 * @file       HsfDataBar.c
 * @brief      数据域控制工具条实现：两行控件 + 统一动作回调。
 * @details    行1=起始/结束时间 + 通道 + 列筛选 + 筛选/清除筛选 +
 *             行数统计（右靠）；行2=小数位数 + 导出格式 + 导出/重新
 *             加载/生成折线图（绿）+ 表格|图表切换（右靠）。
 *             「数据分析」按需求裁掉。各段说明用弱色标签（VFD 页弱化
 *             文案同口径），控件高 28 触控友好。
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
#include <string.h>
#include <stdio.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON && XLAYOUT_ON && XLAYOUT_BOX_ON && \
    XDATETIMEEDIT_ON && XCOMBOBOX_ON && XSPINBOX_ON

/* ---- UTF-8 字面量（源文件 ASCII 化口径） ---- */
#define T_START     "\xE8\xB5\xB7\xE5\xA7\x8B\xE6\x97\xB6\xE9\x97\xB4"          /* 起始时间 */
#define T_END       "\xE7\xBB\x93\xE6\x9D\x9F\xE6\x97\xB6\xE9\x97\xB4"          /* 结束时间 */
#define T_CHANNEL   "\xE9\x80\x9A\xE9\x81\x93"                                   /* 通道 */
#define T_COLFILTER "\xE5\x88\x97\xE7\xAD\x9B\xE9\x80\x89"                       /* 列筛选 */
#define T_FILTER    "\xE7\xAD\x9B\xE9\x80\x89"                                   /* 筛选 */
#define T_CLEAR     "\xE6\xB8\x85\xE9\x99\xA4\xE7\xAD\x9B\xE9\x80\x89"          /* 清除筛选 */
#define T_ROWS      "\xE5\x85\xB1 %lld \xE8\xA1\x8C"                             /* 共 %lld 行 */
#define T_DECIMALS  "\xE6\x98\xBE\xE7\xA4\xBA\xE5\xB0\x8F\xE6\x95\xB0\xE4\xBD\x8D\xE6\x95\xB0" /* 显示小数位数 */
#define T_EXPORT    "\xE5\xAF\xBC\xE5\x87\xBA"                                   /* 导出 */
#define T_RELOAD    "\xE9\x87\x8D\xE6\x96\xB0\xE5\x8A\xA0\xE8\xBD\xBD"           /* 重新加载 */
#define T_CHART     "\xE7\x94\x9F\xE6\x88\x90\xE6\x8A\x98\xE7\xBA\xBF\xE5\x9B\xBE" /* 生成折线图 */
#define T_TABLE     "\xE8\xA1\xA8\xE6\xA0\xBC"                                   /* 表格 */
#define T_CHARTTAB  "\xE5\x9B\xBE\xE8\xA1\xA8"                                   /* 图表 */
#define T_ALL_CHANNELS "\xE5\x85\xA8\xE9\x83\xA8\xE9\x80\x9A\xE9\x81\x93"       /* 全部通道 */
#define T_FMT_XLSX  "Excel (.xlsx)"
#define T_FMT_CSV   "CSV (.csv)"

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
    XComboBox      m_exportFmt;   /**< 导出格式。 */
    XPushButton    m_btnExport;   /**< 导出。 */
    XPushButton    m_btnReload;   /**< 重新加载。 */
    XPushButton    m_btnChart;    /**< 生成折线图（绿底白字）。 */

    XPushButton    m_btnTable;    /**< 表格（checkable）。 */
    XPushButton    m_btnChartTab; /**< 图表（checkable）。 */

    HsfDataBarActionFn m_fn;      /**< 动作回调（借用）。 */
    void*          m_fnUser;      /**< 回调上下文（借用）。 */
    int            m_viewMode;    /**< 当前视图：0=表格 1=图表。 */
} HsfDataBar;

/* ==================== 内部小件 ==================== */

#define BAR_CTRL_H 28   /**< 控件统一高（行紧凑；触控 28 下限）。 */

static void barActionSlot(XObject* receiver, XVarList* args);

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
    HsfTheme_applyDefaultFont(&self->m_base);

    /* ---- 行 1：起始/结束时间 + 通道 + 列筛选 + 筛选/清除 + 行数 ---- */
    barInitLabel(&self->m_lbStart, &self->m_base, T_START);
    XDateTimeEdit_init(&self->m_dtStart, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_dtStart);
    XDateTimeEdit_setDisplayFormat(&self->m_dtStart, "yyyy-MM-dd HH:mm:ss");
    XWidget_setFixedHeight((XWidget*)&self->m_dtStart, BAR_CTRL_H);
    XWidget_setMinimumWidth((XWidget*)&self->m_dtStart, 170);
    XWidget_show((XWidget*)&self->m_dtStart);

    barInitLabel(&self->m_lbEnd, &self->m_base, T_END);
    XDateTimeEdit_init(&self->m_dtEnd, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_dtEnd);
    XDateTimeEdit_setDisplayFormat(&self->m_dtEnd, "yyyy-MM-dd HH:mm:ss");
    XWidget_setFixedHeight((XWidget*)&self->m_dtEnd, BAR_CTRL_H);
    XWidget_setMinimumWidth((XWidget*)&self->m_dtEnd, 170);
    XWidget_show((XWidget*)&self->m_dtEnd);
    /* 缺省区间：过去 24h ~ 现在（数据接入前的合理占位；Qt 页惯用）。 */
    now = XDateTime_currentDateTime();
    XDateTimeEdit_setDateTime(&self->m_dtEnd, &now);

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
    XLabel_setText_2(&self->m_lbRows, "\xE5\x85\xB1 0 \xE8\xA1\x8C"); /* 共 0 行 */
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
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_exportFmt);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_btnExport);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_btnReload);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_btnChart);
    XBoxLayout_addStretch(&self->m_row2, 1);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_btnTable);
    XBoxLayout_addWidget(&self->m_row2, (XWidget*)&self->m_btnChartTab);

    XBoxLayout_init(&self->m_root, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_root, 0, 0, 0, 0);
    XLayout_setSpacing((XLayout*)&self->m_root, 6);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_root);
    XBoxLayout_addLayout(&self->m_root, (XLayout*)&self->m_row1);
    XBoxLayout_addLayout(&self->m_root, (XLayout*)&self->m_row2);

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

void HsfDataBar_setChannelItems(XWidget* bar, const char* const* items,
                                int count)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    int i;
    if (!self) return;
    XComboBox_clear(&self->m_channel);
    if (items && count > 0)
        for (i = 0; i < count; ++i)
            XComboBox_addItem_2(&self->m_channel,
                                items[i] ? items[i] : "");
    else
        XComboBox_addItem_2(&self->m_channel, T_ALL_CHANNELS);
}

int HsfDataBar_channelIndex(const XWidget* bar)
{
    const HsfDataBar* self = (const HsfDataBar*)bar;
    return self ? XComboBox_currentIndex(&self->m_channel) : -1;
}

void HsfDataBar_setColumnFilterItems(XWidget* bar, const char* const* items,
                                     int count)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    int i;
    if (!self) return;
    XComboBox_clear(&self->m_colFilter);
    if (items)
        for (i = 0; i < count; ++i)
            XComboBox_addItem_2(&self->m_colFilter,
                                items[i] ? items[i] : "");
    /* 空表=留空（占位文本「列筛选」可见）。 */
}

int HsfDataBar_columnFilterIndex(const XWidget* bar)
{
    const HsfDataBar* self = (const HsfDataBar*)bar;
    return self ? XComboBox_currentIndex(&self->m_colFilter) : -1;
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
    XSpinBox_setValue(&self->m_decimals, digits);
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
    if (!self) return;
    if (rows < 0) rows = 0;
    snprintf(buf, sizeof(buf), T_ROWS, rows);
    XLabel_setText_2(&self->m_lbRows, buf);
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

void HsfDataBar_adapt(XWidget* bar, bool compact)
{
    HsfDataBar* self = (HsfDataBar*)bar;
    if (!self) return;
    /* 窄屏：说明标签隐藏腾宽（时间框/下拉是必留项）；
     * 宽屏恢复显示。幂等（帧泵每循环调用）。 */
    XWidget_setVisible((XWidget*)&self->m_lbStart, !compact);
    XWidget_setVisible((XWidget*)&self->m_lbEnd, !compact);
    XWidget_setVisible((XWidget*)&self->m_lbChannel, !compact);
    XWidget_setVisible((XWidget*)&self->m_lbDecimals, !compact);
}

#endif /* 守卫 */
