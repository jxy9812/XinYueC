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
#include "HsfTheme.h"
#include "XBoxLayout.h"
#include "XHeaderView.h"
#include "XLabel.h"
#include "XTableWidget.h"
#include "XTimer.h"
#include "XTime.h"
#include "XMemory.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XTABLEWIDGET_ON && XLABEL_ON

/** @brief 报警记录最多保留多少行（与 Python MAX_ROWS 同量级）。 */
#define ALARM_MAX_ROWS   200

/** @brief 报警列头（与 Python AlarmPage 同四列）。 */
static const char* const kAlarmHeaders[4] = {
    "\xE6\x97\xB6\xE9\x97\xB4",   /* 时间 */
    "\xE7\xB1\xBB\xE5\x9E\x8B",   /* 类型 */
    "\xE6\x8F\x8F\xE8\xBF\xB0",   /* 描述 */
    "\xE7\x8A\xB6\xE6\x80\x81"    /* 状态 */
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
    XTableWidget m_table;     /**< 报警表。 */
    XTimer*      m_timer;     /**< 健康巡检定时器（200ms）。 */
    HsfAlarmRow  m_rows[ALARM_MAX_ROWS]; /**< 记录缓冲（旧的在前）。 */
    int          m_count;     /**< 已有记录数。 */
    /* [边沿标记] 每台设备"是否已报过警"（按名字索引，设备数是个位数）。 */
    char         m_seenName[32][32];
    bool         m_seenFlag[32];
    int          m_seenCount;
} HsfDataAlarm;

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
        for (k = 1; k < ALARM_MAX_ROWS; ++k)
            self->m_rows[k - 1] = self->m_rows[k];
        self->m_count = ALARM_MAX_ROWS - 1;
    }
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

/** @brief 把记录铺进表格（最新在上）。 */
static void alarmFillTable(HsfDataAlarm* self)
{
    int r;
    if (!self) return;
    XTableWidget_setRowCount(&self->m_table, self->m_count);
    for (r = 0; r < self->m_count; ++r) {
        const HsfAlarmRow* row = &self->m_rows[self->m_count - 1 - r];
        const char* texts[4];
        int c;
        texts[0] = row->m_time;
        texts[1] = row->m_kind;
        texts[2] = row->m_name;
        texts[3] = row->m_state;
        for (c = 0; c < 4; ++c) {
            XTableWidgetItem* it = XTableWidgetItem_create_utf8(texts[c], 0);
            if (!it) continue;
            XTableWidget_setItem(&self->m_table, r, c, it);
        }
    }
    XWidget_update((XWidget*)&self->m_table);
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

    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 16, 12, 16, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title,
                     "\xE6\x8A\xA5\xE8\xAD\xA6"); /* 报警 */
    XLabel_setTextPixelSize(&self->m_title, 16);
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

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

    self->m_timer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_timer) {
        /* 轮询周期取本地设置「界面刷新周期」（数据刷新卡；缺省 200ms）。 */
        XTimer_setInterval(self->m_timer, HsfSettingsLocal_refreshMs());
        XTimer_setTimerCallback(self->m_timer, alarmTimerCb);
        XTimer_setUserData(self->m_timer, self);
        XTimer_start_base(self->m_timer);
    }

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
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON && XLABEL_ON */
