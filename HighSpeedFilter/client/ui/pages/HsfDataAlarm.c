/**
 * @file       HsfDataAlarm.c
 * @brief      数据-报警页：设备健康异常**边沿记录**（对齐 Python 客户端）。
 * @details    [数据源与口径] 与 Python 客户端 AlarmPage 同源：客户端盯着
 *             服务端的设备健康表（0x0105 推送 / 0x0107 查询），某台设备
 *             **由正常转入异常**的那一刻记一行（边沿触发，同一次异常不
 *             重复刷）；恢复正常后清掉标记，下次再异常还能再记。
 *             [为什么记在客户端] 服务端只报"当前状态"，"什么时候开始的、
 *             持续了多久"只有客户端的时间线知道 ⇒ 报警记录属于客户端侧
 *             的历史（与服务端落盘的 alarms 表是两条线：那条记服务端自己
 *             产生的事件，如急停锁存）。
 *             [表列] 时间 / 类型 / 描述 / 状态（与 Python 同四列）。
 */
#include "HsfDataAlarm.h"

#include "HsfSettingsLocal.h" /* 轮询周期=本地设置「界面刷新周期」 */

#include "HsfLinkClient.h"
#include "HsfDataBar.h"
#include "HsfExport.h"      /* 共享导出模块（CSV / 真 XLSX）。 */
#include "HsfTheme.h"
#include "HsfUi.h"   /* hsfUi_pageFontPx（本地设置·按页字号） */
#include "XBoxLayout.h"
#include "XChart.h"
#include "XChartView.h"
#include "XComboBox.h"
#include "XDateTime.h"
#include "XDevice.h"
#include "XIODevice.h"   /* XIODevice_WriteOnly/_Truncate（导出 CSV） */
#include "XHeaderView.h"
#include "XLabel.h"
#include "XLineSeries.h"
#include "XTableWidget.h"
#include "XTableView.h"     /* 列筛选（setColumnHidden） */
#include "XTimer.h"
#include "XTime.h"
#include "XValueAxis.h"
#include "XMemory.h"
#include "XXYSeries.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XTABLEWIDGET_ON && XLABEL_ON

/** @brief 本页字号折算（本地设置 FontScale/Page11）。 */
static int alarmPx(int px)
{
    return hsfUi_pageFontPx(HSF_PAGE_DATA_ALARM, px);
}

/** @brief 报警记录最多保留多少行（与 Python MAX_ROWS 同量级）。 */
#define ALARM_MAX_ROWS   200

/** @brief 报警列头（与 Python AlarmPage 同四列）。 */
static const char* const kAlarmHeaders[4] = {
    "时间",   /* 时间 */
    "类型",   /* 类型 */
    "描述",   /* 描述 */
    "状态"    /* 状态 */
};

/** @brief 一条报警记录。 */
typedef struct HsfAlarmRow {
    char m_time[16];    /**< 发生时刻（HH:MM:SS）。 */
    char m_name[32];    /**< 设备名（描述列）。 */
    char m_kind[24];    /**< 设备类型（类型列）。 */
    char m_state[24];   /**< 状态文案。 */
} HsfAlarmRow;

/** @brief 报警页对象。 */
typedef struct HsfDataAlarm {
    XWidget      m_base;      /**< 页根（第一成员）。 */
    XBoxLayout   m_layout;    /**< 页根竖排布局。 */
    XLabel       m_title;     /**< 页标题。 */
    XLabel       m_status;    /**< 状态/提示行（筛选/导出结果）。 */
    XWidget*     m_bar;       /**< 通用工具条（本页自己一套；借用）。 */
    XTableWidget m_table;     /**< 报警表。 */
    XChartView   m_chartView; /**< 图表视图（报警次数/分钟）。 */
    XChart*      m_chart;     /**< 折线图（create 所有权在页）。 */
    XLineSeries* m_series;    /**< 次数曲线。 */
    XValueAxis*  m_axisY;     /**< 数值轴。 */
    XValueAxis*  m_axisX;     /**< 时间轴（刻度＝分钟桶序号；标签渲染 时:分）。 */
    int64_t      m_chartFirstMin; /**< 图首桶的分钟号（epoch 毫秒/60000）。 */
    int          m_viewMode;  /**< 0=表格 1=图表。 */
    int          m_kindFilter;/**< 类型筛选：0=全部；1..8 → 设备类型 0..7。 */
    int64_t      m_loTs;      /**< 时间窗下界（<=0 不限）。 */
    int64_t      m_hiTs;      /**< 时间窗上界（<=0 不限）。 */
    int64_t      m_tzOffsetMs;/**< 本地时区偏移（epoch 与日历显示互转用）。 */
    XTimer*      m_timer;     /**< 健康巡检定时器（200ms）。 */
    HsfAlarmRow  m_rows[ALARM_MAX_ROWS]; /**< 记录缓冲（旧的在前）。 */
    int64_t      m_ts[ALARM_MAX_ROWS];   /**< 每条的时刻（epoch 毫秒；筛选用）。 */
    int          m_count;     /**< 已有记录数。 */
    /* [边沿标记] 每台设备"是否已报过警"（按名字索引，设备数是个位数）。 */
    char         m_seenName[32][32];
    bool         m_seenFlag[32];
    int          m_seenCount;
} HsfDataAlarm;

/**
 * @brief 图表 X 轴刻度标签：分钟桶序号 → 本地 时:分。
 * @param axis 轴（未用）。
 * @param value 桶序号（0=图首桶）。
 * @param buf 输出缓冲。
 * @param cap 缓冲容量。
 * @param userData 页面实例。
 * @details [口径 2026-11-10] 图上原来只有裸桶序号（0/1/2…），看不出是几点
 *          几分。桶号 × 60000 + 首桶分钟号即该桶的墙钟毫秒，再补本地时区
 *          偏移展开成日历。
 */
static void alarmXLabelText(const XValueAxis* axis, double value, char* buf,
                            int cap, void* userData)
{
    HsfDataAlarm* self = (HsfDataAlarm*)userData;
    XDateTime dt;
    XTime t;
    int64_t ms;

    (void)axis;
    if (!buf || cap <= 0) return;
    buf[0] = '\0';
    if (!self || self->m_chartFirstMin <= 0) return;
    ms = (self->m_chartFirstMin + (int64_t)(value + 0.5)) * 60000 +
         self->m_tzOffsetMs;
    if (!XDateTime_setMSecsSinceEpoch(&dt, ms)) return;
    t = XDateTime_time(&dt);
    snprintf(buf, (size_t)cap, "%02d:%02d", XTime_hour(&t), XTime_minute(&t));
}

/** @brief 记一条报警（同一设备的同一次异常不重复记）。 */
static void alarmAppend(HsfDataAlarm* self, const HsfLinkDeviceHealth* dev,
                        const char* timeText)
{
    int i;
    if (!self || !dev) return;
    for (i = 0; i < self->m_seenCount; ++i) {
        if (strcmp(self->m_seenName[i], dev->m_name) == 0) {
            if (self->m_seenFlag[i]) return;      /* 已在异常态，不重复记 */
            self->m_seenFlag[i] = true;           /* 新的一次异常 */
            goto store;
        }
    }
    if (self->m_seenCount < 32) {
        snprintf(self->m_seenName[self->m_seenCount],
                 sizeof(self->m_seenName[0]), "%s", dev->m_name);
        self->m_seenFlag[self->m_seenCount] = true;
        self->m_seenCount++;
    }
store:
    if (self->m_count >= ALARM_MAX_ROWS) {
        /* 满了就丢最旧的一条（与 Python 的 rows[-MAX_ROWS:] 同效果）。 */
        int k;
        for (k = 1; k < ALARM_MAX_ROWS; ++k) {
            self->m_rows[k - 1] = self->m_rows[k];
            self->m_ts[k - 1] = self->m_ts[k];
        }
        self->m_count = ALARM_MAX_ROWS - 1;
    }
    self->m_ts[self->m_count] = XDateTime_currentMSecsSinceEpoch();
    snprintf(self->m_rows[self->m_count].m_time,
             sizeof(self->m_rows[0].m_time), "%s", timeText ? timeText : "--");
    snprintf(self->m_rows[self->m_count].m_name,
             sizeof(self->m_rows[0].m_name), "%s", dev->m_name);
    snprintf(self->m_rows[self->m_count].m_kind,
             sizeof(self->m_rows[0].m_kind), "%s",
             HsfLinkClient_kindText(dev->m_kind));
    snprintf(self->m_rows[self->m_count].m_state,
             sizeof(self->m_rows[0].m_state), "%s",
             HsfLinkClient_healthText(dev->m_health));
    self->m_count++;
}

/** @brief 健康恢复正常 ⇒ 清掉该设备的报警标记（下次再异常还能再记）。 */
static void alarmClear(HsfDataAlarm* self, const char* name)
{
    int i;
    if (!self || !name) return;
    for (i = 0; i < self->m_seenCount; ++i) {
        if (strcmp(self->m_seenName[i], name) == 0)
            self->m_seenFlag[i] = false;
    }
}

/** @brief 本地时区偏移（毫秒）：日历型 XDateTime − 墙钟 epoch。 */
static int64_t alarmTzOffsetMs(void)
{
    XDateTime now = XDateTime_currentDateTime();
    return XDateTime_toMSecsSinceEpoch(&now) -
           XDateTime_currentWallMSecsSinceEpoch();
}

/** @brief 一条记录是否落在当前筛选条件内（类型 + 时间窗）。 */
static bool alarmRowVisible(const HsfDataAlarm* self, int index)
{
    const HsfAlarmRow* row;
    const int64_t ts = self->m_ts[index];
    if (index < 0 || index >= self->m_count) return false;
    row = &self->m_rows[index];
    if (self->m_kindFilter > 0) {
        const char* kind = HsfLinkClient_kindText(self->m_kindFilter - 1);
        if (!kind || strcmp(kind, row->m_kind) != 0) return false;
    }
    if (self->m_loTs > 0 && ts > 0 && ts < self->m_loTs) return false;
    if (self->m_hiTs > 0 && ts > self->m_hiTs) return false;
    return true;
}

/** @brief 按「列筛选」隐藏列（0/-1=全部列）。 */
static void alarmApplyColumnFilter(HsfDataAlarm* self)
{
    int idx;
    int c;
    if (!self || !self->m_bar) return;
    idx = HsfDataBar_columnFilterIndex(self->m_bar);
    for (c = 0; c < 4; ++c) {
        const bool hide = (idx > 0 && c != idx - 1);
        if (XTableView_isColumnHidden(&self->m_table.m_base, c) != hide)
            XTableView_setColumnHidden(&self->m_table.m_base, c, hide);
    }
}

/** @brief 把记录铺进表格（最新在上；只铺当前筛选命中的行）。 */
static void alarmFillTable(HsfDataAlarm* self)
{
    int r;
    int shown = 0;
    if (!self) return;
    for (r = self->m_count - 1; r >= 0; --r)
        if (alarmRowVisible(self, r)) ++shown;
    XTableWidget_setRowCount(&self->m_table, shown);
    shown = 0;
    for (r = self->m_count - 1; r >= 0; --r) {
        const HsfAlarmRow* row = &self->m_rows[r];
        const char* texts[4];
        int c;
        if (!alarmRowVisible(self, r)) continue;
        texts[0] = row->m_time;
        texts[1] = row->m_kind;
        texts[2] = row->m_name;
        texts[3] = row->m_state;
        for (c = 0; c < 4; ++c) {
            XTableWidgetItem* it = XTableWidgetItem_create_utf8(texts[c], 0);
            if (!it) continue;
            XTableWidget_setItem(&self->m_table, shown, c, it);
        }
        ++shown;
    }
    if (self->m_bar)
        HsfDataBar_setTotalRows(self->m_bar, shown);
    alarmApplyColumnFilter(self);
    XWidget_update((XWidget*)&self->m_table);
}

/** @brief 报警页导出取格上下文（网格行 → 报警记录行号）。 */
typedef struct AlarmExportCtx {
    const HsfDataAlarm* page; /**< 页面（取 m_rows / m_count）。 */
    const int*          map;   /**< 网格行 → 报警记录行号（可见行，倒序）。 */
} AlarmExportCtx;

/** @brief 导出取格回调：时间 / 类型 / 描述 / 状态 四列。 */
static bool alarmExportCell(void* user, int row, int col, char* buf, int cap)
{
    const AlarmExportCtx* ctx = (const AlarmExportCtx*)user;
    const HsfAlarmRow* r;
    if (buf && cap > 0) buf[0] = '\0';
    if (!ctx || !ctx->page || !ctx->map || !buf || cap <= 0) return false;
    if (row < 0 || row >= ctx->page->m_count) return false;
    r = &ctx->page->m_rows[ctx->map[row]];
    switch (col) {
    case 0: snprintf(buf, (size_t)cap, "%s", r->m_time); return true;
    case 1: snprintf(buf, (size_t)cap, "%s", r->m_kind); return true;
    case 2: snprintf(buf, (size_t)cap, "%s", r->m_name); return true;
    case 3: snprintf(buf, (size_t)cap, "%s", r->m_state); return true;
    default: return false;
    }
}

/** @brief 导出当前筛选命中行到文件（CSV 或真 XLSX；需 Download 权限）。 */
static void alarmExportCsv(HsfDataAlarm* self)
{
    HsfExportGrid grid;
    AlarmExportCtx ctx;
    static const char* const kHead[4] = { "时间", "类型", "描述", "状态" };
    int map[ALARM_MAX_ROWS];
    int n = 0;
    int r;
    char path[96];
    char prefix[96];
    char buf[256];

    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_DOWNLOAD)) {
        XLabel_setText_2(&self->m_status, "权限不足：下载数据需要普通用户及以上账户");
        return;
    }
    /* [可见行倒序] 与旧实现一致：最新的在前，且只导**当前筛选命中**的行
     * （类型过滤 + 时间窗）。网格行 → 记录行的映射先算好，取格回调按
     * 映射取。 */
    for (r = self->m_count - 1; r >= 0; --r) {
        if (!alarmRowVisible(self, r)) continue;
        if (n >= ALARM_MAX_ROWS) break;
        map[n++] = r;
    }
    snprintf(prefix, sizeof(prefix), "./hsf_alarm_%lld",
             (long long)(XDateTime_currentMSecsSinceEpoch() / 1000));
    ctx.page = self;
    ctx.map = map;
    memset(&grid, 0, sizeof(grid));
    grid.path = prefix;
    grid.sheetName = "报警";
    grid.format = HsfExport_formatFromBarIndex(
        HsfDataBar_exportFormatIndex(self->m_bar));
    grid.colCount = 4;
    grid.rowCount = n;
    grid.header = kHead;
    grid.cell = alarmExportCell;
    grid.user = &ctx;
    if (!HsfExport_write(&grid)) {
        XLabel_setText_2(&self->m_status, "导出失败：文件写入错误");
        return;
    }
    (void)HsfExport_outputPath(&grid, path, (int)sizeof(path));
    snprintf(buf, sizeof(buf), "已导出 %d 行 → %s", n, path);
    XLabel_setText_2(&self->m_status, buf);
}

/** @brief 折线图：当前筛选命中行按分钟计数（x=分钟序号，y=条数）。 */
static void alarmBuildChart(HsfDataAlarm* self)
{
    int64_t mins[64];
    int cnt[64];
    int nb = 0;
    int i;
    int vmax = 0;
    if (!self || !self->m_chart) return;
    if (self->m_series)
        XXYSeries_clear((XXYSeries*)self->m_series);
    for (i = 0; i < self->m_count; ++i) {
        const int64_t ts = self->m_ts[i];
        const int64_t m = ts > 0 ? ts / 60000 : -1;
        if (!alarmRowVisible(self, i) || m < 0) continue;
        if (nb > 0 && mins[nb - 1] == m) {
            cnt[nb - 1]++;
        } else if (nb < 64) {
            mins[nb] = m;
            cnt[nb] = 1;
            ++nb;
        } else {
            cnt[63]++;
        }
    }
    for (i = 0; i < nb; ++i) {
        if (self->m_series)
            XXYSeries_append((XXYSeries*)self->m_series, (double)i,
                             (double)cnt[i]);
        if (cnt[i] > vmax) vmax = cnt[i];
    }
    /* [X 轴刻度＝分钟桶] 记住首桶分钟号，标签回调据此还原 时:分
     * （原先只有裸桶序号，图上读不出时间）。 */
    self->m_chartFirstMin = (nb > 0) ? mins[0] : 0;
    if (self->m_axisY)
        XValueAxis_setRange(self->m_axisY, 0.0,
                            vmax > 0 ? (double)vmax + 1.0 : 1.0);
    XChart_setTitle_2(self->m_chart, "报警次数 / 分钟");
    XChartView_updateChart(&self->m_chartView);
}

/** @brief 切表格/图表视图。 */
static void alarmSetView(HsfDataAlarm* self, int mode)
{
    if (!self) return;
    self->m_viewMode = (mode == 1) ? 1 : 0;
    XWidget_setVisible((XWidget*)&self->m_table, self->m_viewMode == 0);
    XWidget_setVisible(&self->m_chartView.m_base, self->m_viewMode == 1);
    if (self->m_bar)
        HsfDataBar_setViewMode(self->m_bar, self->m_viewMode);
}

/** @brief 筛选：读工具条类型/时间窗，重铺表格。 */
static void alarmApplyFilter(HsfDataAlarm* self)
{
    XDateTime lo;
    XDateTime hi;
    int64_t loMs;
    int64_t hiMs;
    if (!self || !self->m_bar) return;
    lo = HsfDataBar_startDateTime(self->m_bar);
    hi = HsfDataBar_endDateTime(self->m_bar);
    loMs = XDateTime_isValid(&lo) ? XDateTime_toMSecsSinceEpoch(&lo) : 0;
    hiMs = XDateTime_isValid(&hi) ? XDateTime_toMSecsSinceEpoch(&hi) : 0;
    if (loMs > 0) loMs -= self->m_tzOffsetMs;
    if (hiMs > 0) hiMs -= self->m_tzOffsetMs;
    if (loMs > 0 && hiMs > 0 && loMs > hiMs) {
        XLabel_setText_2(&self->m_status, "起始时间晚于结束时间，未筛选");
        return;
    }
    self->m_loTs = loMs;
    self->m_hiTs = hiMs;
    self->m_kindFilter = HsfDataBar_channelIndex(self->m_bar);
    if (self->m_kindFilter < 0) self->m_kindFilter = 0;
    alarmFillTable(self);
    if (self->m_viewMode == 1) alarmBuildChart(self);
}

/** @brief 清除筛选：类型回全部、时间窗回数据全区间。 */
static void alarmClearFilter(HsfDataAlarm* self)
{
    XDateTime dt;
    if (!self) return;
    self->m_kindFilter = 0;
    self->m_loTs = 0;
    self->m_hiTs = 0;
    if (self->m_bar) {
        HsfDataBar_setColumnFilterIndex(self->m_bar, 0);
        HsfDataBar_setChannelIndex(self->m_bar, 0);
        if (self->m_count > 0) {
            XDateTime_setMSecsSinceEpoch(&dt, self->m_ts[0] + self->m_tzOffsetMs);
            HsfDataBar_setStartDateTime(self->m_bar, &dt);
            XDateTime_setMSecsSinceEpoch(&dt, self->m_ts[self->m_count - 1] +
                                             self->m_tzOffsetMs);
            HsfDataBar_setEndDateTime(self->m_bar, &dt);
        }
    }
    alarmFillTable(self);
    if (self->m_viewMode == 1) alarmBuildChart(self);
}

/** @brief 工具条动作落地（报警页自己那套）。 */
static void alarmBarAction(void* userData, int action)
{
    HsfDataAlarm* self = (HsfDataAlarm*)userData;
    if (!self) return;
    switch (action) {
    case HsfDataBarAction_Filter:
        alarmApplyFilter(self);
        break;
    case HsfDataBarAction_ClearFilter:
        alarmClearFilter(self);
        break;
    case HsfDataBarAction_Export:
        alarmExportCsv(self);
        break;
    case HsfDataBarAction_Reload:
        alarmFillTable(self);
        if (self->m_viewMode == 1) alarmBuildChart(self);
        break;
    case HsfDataBarAction_GenerateChart:
        alarmBuildChart(self);
        alarmSetView(self, 1);
        break;
    case HsfDataBarAction_ViewTable:
        alarmSetView(self, 0);
        break;
    case HsfDataBarAction_ViewChart:
        alarmBuildChart(self);
        alarmSetView(self, 1);
        break;
    case HsfDataBarAction_ColumnFilterChanged:
        alarmApplyColumnFilter(self);
        break;
    default:
        break;
    }
}

/** @brief 200ms 拍：巡检健康表，边沿记报警。 */
static void alarmTimerCb(void* userData, XTimerData* timer)
{
    HsfDataAlarm* self = (HsfDataAlarm*)userData;
    HsfLinkDeviceHealth devs[32];
    int n;
    int i;
    bool changed = false;
    char timeText[16];
    (void)timer;
    if (!self) return;
    n = HsfLinkClient_deviceHealth(devs, 32, NULL);
    if (n <= 0) return;
    {
        /* 时刻：当前本地时间的 HH:MM:SS。 */
        XTime t = XTime_currentTime();
        snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d",
                 XTime_hour(&t), XTime_minute(&t), XTime_second(&t));
    }
    for (i = 0; i < n; ++i) {
        const HsfLinkDeviceHealth* dev = &devs[i];
        const bool abnormal = (dev->m_health >= 2);  /* 2 数据过期 3 离线 4 故障 */
        if (abnormal) {
            const int before = self->m_count;
            alarmAppend(self, dev, timeText);
            if (self->m_count != before) changed = true;
        } else {
            alarmClear(self, dev->m_name);
        }
    }
    if (changed) alarmFillTable(self);
}

/** @brief 字号实时重放（本地设置改动时由 HsfUi 调回）。 */
static void alarmApplyFontsAll(XWidget* page, int pct)
{
    HsfDataAlarm* self = (HsfDataAlarm*)page;
    if (!self || pct <= 0) return;
    XLabel_setTextPixelSize(&self->m_title, 16 * pct / 100);
    XLabel_setTextPixelSize(&self->m_status, 12 * pct / 100);
}

XWidget* HsfDataAlarm_create(XWidget* parent, HsfMenuNavFn navFn,
                             void* userData)
{
    HsfDataAlarm* self;
    (void)navFn; (void)userData;

    if (!parent) return NULL;
    self = (HsfDataAlarm*)XMemory_malloc(sizeof(HsfDataAlarm),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    HsfTheme_applyDefaultFont(&self->m_base);

    self->m_tzOffsetMs = alarmTzOffsetMs();

    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 16, 12, 16, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title,
                     "报警"); /* 报警 */
    XLabel_setTextPixelSize(&self->m_title, alarmPx(16));
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    XLabel_init(&self->m_status, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_status);
    XLabel_setTextPixelSize(&self->m_status, 12);
    XWidget_setForegroundRole((XWidget*)&self->m_status, XPaletteColorRole_Mid);
    XLabel_setText_2(&self->m_status, "异常边沿记录（客户端侧）");
    XWidget_show((XWidget*)&self->m_status);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_status);

    /* 工具条（本页自己一套；与历史/日志页同款，各自一份实例）。 */
    self->m_bar = HsfDataBar_create(&self->m_base);
    if (self->m_bar) {
        static const char* const kKinds[8] = {
            "变频器", "温湿度", "流量计", "模拟量",
            "485总线", "数字量", "电动球阀组", "水质"
        };
        const char* channelItems[9];
        const char* colItems[5];
        XDateTime now;
        int i;
        channelItems[0] = "全部类型";
        for (i = 0; i < 8; ++i) channelItems[i + 1] = kKinds[i];
        HsfDataBar_setChannelItems(self->m_bar, channelItems, 9);
        HsfDataBar_setChannelIndex(self->m_bar, 0);
        colItems[0] = "全部列";
        for (i = 0; i < 4; ++i) colItems[i + 1] = kAlarmHeaders[i];
        HsfDataBar_setColumnFilterItems(self->m_bar, colItems, 5);
        HsfDataBar_setColumnFilterIndex(self->m_bar, 0);
        now = XDateTime_currentDateTime();
        HsfDataBar_setEndDateTime(self->m_bar, &now);
        HsfDataBar_setActionCallback(self->m_bar, alarmBarAction, self);
        /* [按页裁剪工具条控件 2026-11-10] 本页有图表形态、有列筛选，但
         * **没有**「小数位」与「每图例最多点数」的处理分支（alarmBarAction
         * 里无对应 case）⇒ 共享工具条上这两个微调框是「可见但改了没反应」
         * 的假控件，藏掉。本页其余控件（图表/列筛选/导出格式）都真实接线，
         * 保持显示。 */
        HsfDataBar_setFeatures(self->m_bar,
            HsfDataBarFeature_All
            & ~(unsigned int)(HsfDataBarFeature_Decimals
                              | HsfDataBarFeature_MaxPoints));
        XBoxLayout_addWidget(&self->m_layout, self->m_bar);
    }

    XTableWidget_init(&self->m_table, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_table);
    XTableWidget_setColumnCount(&self->m_table, 4);
    XTableWidget_setHorizontalHeaderLabels(&self->m_table, kAlarmHeaders, 4);
    XTableWidget_setRowCount(&self->m_table, 0);
    {
        XHeaderView* hh = XTableView_horizontalHeader(&self->m_table.m_base);
        if (hh)
            XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Fixed);
        XTableView_setColumnWidth(&self->m_table.m_base, 0, 120);
        XTableView_setColumnWidth(&self->m_table.m_base, 1, 120);
        XTableView_setColumnWidth(&self->m_table.m_base, 2, 180);
        XTableView_setColumnWidth(&self->m_table.m_base, 3, 120);
        XTableWidget_setVerticalHeaderVisible(&self->m_table, false);
        XTableView_setShowGrid(&self->m_table.m_base, false);
    }
    XWidget_show((XWidget*)&self->m_table);
    XBoxLayout_addWidgetEx(&self->m_layout, (XWidget*)&self->m_table, 1, 0);

    /* 图表视图（与表格同格二选一；次数/分钟）。 */
    XChartView_init(&self->m_chartView, &self->m_base, 0);
    XWidget_setSizePolicy(&self->m_chartView.m_base,
                          XWidgetSizePolicy_Expanding,
                          XWidgetSizePolicy_Expanding);
    self->m_chart = XChart_create();
    if (self->m_chart) {
        self->m_series = XLineSeries_create();
        if (self->m_series)
            XChart_addSeries(self->m_chart, self->m_series,
                             XChartSeriesType_Line);
        XChart_createDefaultAxes(self->m_chart);
        self->m_axisY = (XValueAxis*)XChart_axisY(self->m_chart);
        self->m_axisX = (XValueAxis*)XChart_axisX(self->m_chart);
        if (self->m_axisX) {
            /* [X 轴显示 时:分 2026-11-10] 桶序号 → 本地时钟（每分钟一桶）。
             * 缺省格式串只有桶号，图上读不出是几点几分。 */
            XValueAxis_setLabelTextCallback(self->m_axisX, alarmXLabelText,
                                            self);
        }
        XChartView_setChart(&self->m_chartView, self->m_chart);
    }
    XChartView_setPanEnabled(&self->m_chartView, true);
    XWidget_hide(&self->m_chartView.m_base);
    XBoxLayout_addWidgetEx(&self->m_layout, &self->m_chartView.m_base, 1, 0);

    self->m_timer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_timer) {
        /* 轮询周期取本地设置「界面刷新周期」（数据刷新卡；缺省 200ms）。 */
        XTimer_setInterval(self->m_timer, HsfSettingsLocal_refreshMs());
        XTimer_setTimerCallback(self->m_timer, alarmTimerCb);
        XTimer_setUserData(self->m_timer, self);
        XTimer_start_base(self->m_timer);
    }

    hsfUi_registerPageFontApplier(HSF_PAGE_DATA_ALARM, &self->m_base,
                                  alarmApplyFontsAll);
    XWidget_show(&self->m_base);
    return &self->m_base;
}

void HsfDataAlarm_adapt(XWidget* page, bool compact)
{
    HsfDataAlarm* self = (HsfDataAlarm*)page;
    if (!self) return;
    /* 竖屏/窄窗：页边距 16,12→8,6 腾内容宽（与本页 create 边距同源）。
     * 幂等（帧泵每循环调用）。本页是自建根结构（非 HsfDataTablePage），
     * 必须走自己的 adapt——历史上误挂 HsfDataTablePage_adapt 曾把本页
     * 强转成异型结构读 m_bar 偏移（垃圾指针）导致启动段错误。 */
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 8 : 16, compact ? 6 : 12,
                               compact ? 8 : 16, compact ? 6 : 12);
    if (self->m_bar)
        HsfDataBar_adapt(self->m_bar, compact);
    XLayout_activate((XLayout*)&self->m_layout);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON && XLABEL_ON */
