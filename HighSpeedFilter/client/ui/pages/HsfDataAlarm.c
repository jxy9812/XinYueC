/**
 * @file       HsfDataAlarm.c
 * @brief      数据-报警页：设备健康异常**边沿记录** + 自动运行告警事件
 *             （两条事件线合一张表）。
 * @details    [数据源与口径] 与 Python 客户端 AlarmPage 同源：客户端盯着
 *             服务端的设备健康表（0x0105 推送 / 0x0107 查询），某台设备
 *             **由正常转入异常**的那一刻记一行（边沿触发，同一次异常不
 *             重复刷）；恢复正常后清掉标记，下次再异常还能再记。
 *             [数据源二：自动运行告警（2026-11-13 补）] 服务端自动模式的
 *             报警（阀超时/急停/动作失败/设备不可用/全停失败）经
 *             XAutoController_raiseAlarm 入队，服务端每拍 take 后推
 *             HSF_TOPIC_AUTO_ALARM（一条一帧，kind/atMs/text）并入库
 *             alarms 表——此前客户端**既不订阅也不解析**该主题（帧根本
 *             不会到达），报警页永远收不到自动运行告警（板端实测"报警
 *             信息不进报警表"根因）。现由 HsfLinkClient **常驻订阅**
 *             （事件发生在哪个页面不可预知，漏采不可补）+ 本页回调落表：
 *             一帧记一行（事件不去重），类型列="自动运行"、描述列=告警
 *             原文、状态列=告警类型中文；类型下拉尾项「自动运行」可单独
 *             筛。显示时刻用客户端接收时刻（服务端 atMs 口径两端不同：
 *             C 墙钟 / Python 单调钟，不可作跨端展示依据）。
 *             [为什么记在客户端] 服务端只报"当前状态"，"什么时候开始的、
 *             持续了多久"只有客户端的时间线知道 ⇒ 报警记录属于客户端侧
 *             的历史（与服务端落盘的 alarms 表是两条线：那条记服务端自己
 *             产生的事件，如急停锁存）。
 *             [表列] 时间 / 类型 / 描述 / 状态（与 Python 同四列）。
 *             [2026-11-11 对齐 Python AlarmPage] 无图表/无双视图（用户
 *             定案：日志和报警都不要图表）；筛选=类型 + 时间窗 + 关键字
 *             （设备/类型/状态包含）；客户端内部分页（10/20/50，缺省 20，
 *             统计/导出按筛选命中全集）；滚动锁定（锁定期新报警只入库
 *             不刷表，解锁补刷一次）；清除筛选时列回全勾 + 类型回全部
 *             （三页里唯一，Python 同口径）。判定与分页边界在
 *             HsfAlarmLogic（纯逻辑；回归锁 Test/XProtocolTest/
 *             hsf_regression_test.c 报警逻辑节单 TU 收录钉住）。
 */
#include "HsfDataAlarm.h"

#include "HsfSettingsLocal.h" /* 轮询周期=本地设置「界面刷新周期」 */

#include "HsfAlarmLogic.h"  /* 纯逻辑：行可见判定 + 分页边界（口径单一来源） */
#include "HsfLinkClient.h"
#include "HsfDataBar.h"
#include "HsfExport.h"      /* 共享导出模块（CSV / 真 XLSX）。 */
#include "HsfTheme.h"
#include "HsfUi.h"   /* hsfUi_pageFontPx（本地设置·按页字号） */
#include "XAbstractButton.h" /* 滚动锁定复选框的 toggled 信号 */
#include "XBoxLayout.h"
#include "XCheckBox.h"
#include "XComboBox.h"
#include "XDateTime.h"
#include "XFileDialog.h"  /* 导出走框架文件对话框（格式=过滤器档位） */
#include "XHeaderView.h"
#include "XLabel.h"
#include "XLineEdit.h"      /* 关键字输入（设备/类型/状态） */
#include "XTableWidget.h"
#include "XTableView.h"     /* 列筛选（setColumnHidden） */
#include "XTimer.h"
#include "XTime.h"
#include "XMemory.h"
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

/** @brief 客户端内部分页档位（与历史/日志页同款 10/20/50；缺省 20=中间档，
 *         不持久化——报警页故意不像历史/日志页那样把页大小写本机 INI）。 */
static const int kAlarmPageSizes[HSF_ALARM_PAGE_SIZES_COUNT] = { 10, 20, 50 };

/** @brief 类型下拉里「自动运行」项的下标（0=全部类型，1..8=设备类型，
 *         9=自动运行告警；与 create 里 channelItems 的铺法绑死，改一处
 *         必须同步另一处——栈数组长度按"列数常量"绑死是 AGENTS 铁律）。 */
#define ALARM_KIND_FILTER_AUTO   9
/** @brief 自动运行告警的类型列文案（与类型下拉尾项逐字一致，筛选按全等匹配）。 */
#define ALARM_KIND_TEXT_AUTO     "自动运行"

/** @brief 报警列头（与 Python AlarmPage 同四列）。 */
static const char* const kAlarmHeaders[4] = {
    "时间",   /* 时间 */
    "类型",   /* 类型 */
    "描述",   /* 描述 */
    "状态"    /* 状态 */
};

/** @brief 报警四列的列宽（事实源；建表/列筛选/字号重放共用）。
 *  @note  列筛选隐藏某列时把该列宽置 0（XTableWidget 绘制按列宽逐列画，
 *         宽 0 ⇒ 不占位、不绘制）。 */
static const int kAlarmColWidths[4] = { 120, 120, 180, 120 };

/** @brief 取第 c 列的基准列宽（越界返回 0）。 */
static int alarmColWidth(int c)
{
    return (c >= 0 && c < 4) ? kAlarmColWidths[c] : 0;
}

/** @brief 一条报警记录。 */
typedef struct HsfAlarmRow {
    char m_time[16];    /**< 发生时刻（HH:MM:SS）。 */
    char m_name[160];   /**< 设备名/告警原文（描述列）。自动运行告警的
                         *   text 服务端原文最长 128 字节 ⇒ 缓冲须容得下
                         *   （原先 32 只装得下设备名，snprintf 会静默截断）。 */
    char m_kind[24];    /**< 设备类型/「自动运行」（类型列）。 */
    char m_state[24];   /**< 状态文案（健康态/告警类型中文）。 */
} HsfAlarmRow;

/** @brief 报警页对象。 */
typedef struct HsfDataAlarm {
    XWidget      m_base;      /**< 页根（第一成员）。 */
    XBoxLayout   m_layout;    /**< 页根竖排布局。 */
    XLabel       m_title;     /**< 页标题。 */
    XLabel       m_status;    /**< 状态/提示行（筛选/导出/分页统计）。 */
    XWidget*     m_bar;       /**< 通用工具条（本页自己一套；借用）。 */
    XTableWidget m_table;     /**< 报警表。 */
    /* [行 2 页级控件] 分页/关键字/滚动锁定（attachRow2 挂进工具条行 2）。 */
    XPushButton  m_prev;      /**< 上一页。 */
    XPushButton  m_next;      /**< 下一页。 */
    XComboBox    m_pageSizeBox; /**< 每页条数（10/20/50）。 */
    XLineEdit    m_kw;        /**< 关键字输入（设备/类型/状态；回车=应用）。 */
    XCheckBox    m_freeze;    /**< 滚动锁定（锁定期新报警只入库不刷表）。 */
    int          m_kindFilter;/**< 类型筛选：0=全部；1..8 → 设备类型 0..7。 */
    int64_t      m_loTs;      /**< 时间窗下界（<=0 不限）。 */
    int64_t      m_hiTs;      /**< 时间窗上界（<=0 不限）。 */
    char         m_kwText[160]; /**< 当前生效关键字（去首尾空白；空=不筛）。 */
    int          m_pageSize;  /**< 当前页大小（kAlarmPageSizes 档；缺省 20）。 */
    int          m_pageIndex; /**< 当前页号（0 基；铺表时按全集夹取）。 */
    int          m_filteredCount; /**< 最近一次铺表算出的筛选命中全集行数
                                       （翻页判界用；0=尚未铺过）。 */
    bool         m_frozen;    /**< 滚动锁定当前态（与复选框勾选态同步）。 */
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

/* [前向声明] 自动运行告警回调（TOPIC_AUTO_ALARM）在 alarmFillTable 定义
 * 之前落表后要触发一次重铺（事件到达即上屏，不等 200ms 拍）。 */
static void alarmFillTable(HsfDataAlarm* self);

/**
 * @brief 本页筛选状态 → 纯逻辑筛选结构（口径单一来源 = HsfAlarmLogic）。
 * @details 类型下拉下标在页面层折成比较文本：0=全部类型 ⇒ NULL（不限）；
 *          1..8 → 设备类型 0..7 的展示文本；9=「自动运行」（自动运行告警
 *          行的类型列文案，全等匹配；与 Python AlarmPage._kind_text 同
 *          「记录里存的就是该文本」口径）。
 */
static void alarmBuildFilter(const HsfDataAlarm* self, HsfAlarmFilter* f)
{
    f->m_kindText = NULL;
    if (self->m_kindFilter > 0) {
        if (self->m_kindFilter == ALARM_KIND_FILTER_AUTO) {
            f->m_kindText = ALARM_KIND_TEXT_AUTO;
        } else {
            f->m_kindText = HsfLinkClient_kindText(self->m_kindFilter - 1);
            if (f->m_kindText && !f->m_kindText[0]) f->m_kindText = NULL;
        }
    }
    f->m_loTs = self->m_loTs;
    f->m_hiTs = self->m_hiTs;
    f->m_kw = self->m_kwText;
}

/** @brief 行缓冲压入一条（满了丢最旧；字段 snprintf 截断安全）。 */
static void alarmPushRow(HsfDataAlarm* self, int64_t tsMs,
                         const char* timeText, const char* kindText,
                         const char* nameText, const char* stateText)
{
    if (self->m_count >= ALARM_MAX_ROWS) {
        /* 满了就丢最旧的一条（与 Python 的 rows[-MAX_ROWS:] 同效果）。 */
        int k;
        for (k = 1; k < ALARM_MAX_ROWS; ++k) {
            self->m_rows[k - 1] = self->m_rows[k];
            self->m_ts[k - 1] = self->m_ts[k];
        }
        self->m_count = ALARM_MAX_ROWS - 1;
    }
    self->m_ts[self->m_count] = tsMs;
    snprintf(self->m_rows[self->m_count].m_time,
             sizeof(self->m_rows[0].m_time), "%s", timeText ? timeText : "--");
    snprintf(self->m_rows[self->m_count].m_kind,
             sizeof(self->m_rows[0].m_kind), "%s", kindText ? kindText : "");
    snprintf(self->m_rows[self->m_count].m_name,
             sizeof(self->m_rows[0].m_name), "%s", nameText ? nameText : "");
    snprintf(self->m_rows[self->m_count].m_state,
             sizeof(self->m_rows[0].m_state), "%s", stateText ? stateText : "");
    self->m_count++;
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
    alarmPushRow(self, XDateTime_currentMSecsSinceEpoch(), timeText,
                 HsfLinkClient_kindText(dev->m_kind), dev->m_name,
                 HsfLinkClient_healthText(dev->m_health));
}

/** @brief 自动运行告警类型中文（HSF_AUTO_ALARM_*，与 proto.py AUTO_ALARM_*
 *         同值；未知类型回退「自动运行」，不显示裸数字）。 */
static const char* alarmAutoKindText(int kind)
{
    switch (kind) {
    case HSF_AUTO_ALARM_ACTION_FAILED:  return "动作失败";
    case HSF_AUTO_ALARM_DEVICE_UNAVAIL: return "设备不可用";
    case HSF_AUTO_ALARM_CUSTOM:         return "自定义";
    case HSF_AUTO_ALARM_ESTOP:          return "急停";
    case HSF_AUTO_ALARM_VALVE_TIMEOUT:  return "阀超时";
    /* [流量计告警 2026-11-15] 上限/下限/空管/系统 + 进水泵空管保护动作。 */
    case HSF_AUTO_ALARM_FLOWMETER:      return "流量计";
    /* [设备故障 2026-11-15] 变频器故障等：进报警历史表 + 锁存 + 停机。 */
    case HSF_AUTO_ALARM_DEVICE_FAULT:   return "设备故障";
    /* [压差过高 2026-11-15] 运行保护的压差过大动作。 */
    case HSF_AUTO_ALARM_PRESS_HIGH:      return "压差过高";
    default:                            return ALARM_KIND_TEXT_AUTO;
    }
}

/**
 * @brief 自动运行告警事件落表（TOPIC_AUTO_ALARM 常驻订阅 ⇒ 全天候采集）。
 * @details [一帧一行，不去重] 健康边沿按设备名去重（同一次异常只记一行）；
 *          自动告警是**一次性事件**（服务端已按条产生），一帧记一行。
 *          [时刻口径] 显示时刻用客户端接收时刻——服务端帧里的 atMs 两端
 *          口径不同（C 墙钟 / Python 单调钟），不能当跨端展示依据。
 *          [滚动锁定] 与 alarmTimerCb 同款：入库无条件，刷表看锁定。
 */
static void alarmAutoAlarmCb(void* userData, const HsfLinkAutoAlarm* alarm)
{
    HsfDataAlarm* self = (HsfDataAlarm*)userData;
    XTime t;
    char timeText[16];
    if (!self || !alarm) return;
    t = XTime_currentTime();
    snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d",
             XTime_hour(&t), XTime_minute(&t), XTime_second(&t));
    /* [设备类别 2026-11-15] 帧里带 dev ⇒ 类型列显示该设备类别（如"流量计"），
     * 缺键（老服务端）⇒ 仍是"自动运行"（与旧行为一致）。 */
    alarmPushRow(self, XDateTime_currentMSecsSinceEpoch(), timeText,
                 (alarm->m_dev >= 0)
                     ? HsfLinkClient_kindText(alarm->m_dev)
                     : ALARM_KIND_TEXT_AUTO,
                 alarm->m_text,
                 alarmAutoKindText(alarm->m_kind));
    if (!self->m_frozen) alarmFillTable(self);
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

/** @brief 本地时区偏移（毫秒）：日历型 XDateTime − 墙钟 epoch，归整分钟。
 *  @note  [2026-11-11] 两次采样相减的 ±1ms 噪声会让整秒时刻少 1 秒并在
 *         两值间跳；时区偏移恒为整分钟倍数 ⇒ 就近归到 60000ms 网格。
 *         同 HsfLinkHistory.c 的 hsfHistTzOffsetMs。 */
static int64_t alarmTzOffsetMs(void)
{
    XDateTime now;
    int64_t raw;
    int64_t half;
    now = XDateTime_currentDateTime();
    raw = XDateTime_toMSecsSinceEpoch(&now) -
          XDateTime_currentWallMSecsSinceEpoch();
    half = (raw >= 0) ? 30000LL : -30000LL;
    return ((raw + half) / 60000LL) * 60000LL;
}

/** @brief 一条记录是否落在当前筛选条件内（类型 + 时间窗 + 关键字）。
 *  @details 判定本体在 HsfAlarmLogic_rowVisible（纯逻辑，回归锁单 TU
 *           收录钉住）；本页只负责把自己的筛选状态折成 HsfAlarmFilter。
 *           关键字匹配 = 设备名/类型/状态三列文本"包含"（不含时间列）。 */
static bool alarmRowVisible(const HsfDataAlarm* self, int index)
{
    HsfAlarmFilter f;
    if (index < 0 || index >= self->m_count) return false;
    alarmBuildFilter(self, &f);
    return HsfAlarmLogic_rowVisible(&f, self->m_ts[index],
                                    self->m_rows[index].m_kind,
                                    self->m_rows[index].m_name,
                                    self->m_rows[index].m_state);
}

/** @brief 按「列筛选」隐藏列（位掩码；未勾的列调 setColumnHidden）。
 *  @note  [2026-11-11 回归正规口径] 远端 6fc0adb0（Qt 6.8 全量对齐）后
 *         `setColumnHidden` 转发 header 隐藏段、表格绘制跳过隐藏段，
 *         与历史页同一口径；列宽恢复交给 alarmApplyFontsAll（全宽×pct）。 */
static void alarmApplyColumnFilter(HsfDataAlarm* self)
{
    uint32_t mask;
    int c;
    if (!self || !self->m_bar) return;
    mask = HsfDataBar_columnFilterMask(self->m_bar);
    for (c = 0; c < 4; ++c) {
        const bool hide = !(mask & (1u << c));
        if (XTableView_isColumnHidden(&self->m_table.m_base, c) != hide)
            XTableView_setColumnHidden(&self->m_table.m_base, c, hide);
    }
}

/** @brief 把**当前页**那一段记录铺进表格（全集倒序＝最新在上；分页切片）。
 *  @details [统计与翻页对齐 Python AlarmPage._rebuild] 筛选命中全集在
 *           内存（HsfAlarmLogic_rowVisible 判定），表格只铺当前页切片：
 *           页号越界先夹取（筛选/解锁后行数变少不滞留空页）；状态行 =
 *           「筛选命中 N 条 · 第 x/y 页 · 本页 M」，N/x/y 全由**全集**算
 *           （与导出/统计同一口径，不受当前页影响）；上/下页钮按判界使
 *           能（上一页=页号>0，下一页=页号+1<页数；setEnabled 有真同值
 *           早退，每拍重放安全）。 */
static void alarmFillTable(HsfDataAlarm* self)
{
    HsfAlarmFilter f;
    int total = 0;
    int pages;
    int start;
    int expect;
    int r;
    int shown = 0;
    int skipped = 0;
    char buf[160];
    if (!self) return;
    alarmBuildFilter(self, &f);
    for (r = self->m_count - 1; r >= 0; --r) {
        if (HsfAlarmLogic_rowVisible(&f, self->m_ts[r],
                                     self->m_rows[r].m_kind,
                                     self->m_rows[r].m_name,
                                     self->m_rows[r].m_state))
            ++total;
    }
    pages = HsfAlarmLogic_pageCount(total, self->m_pageSize);
    start = HsfAlarmLogic_pageStart(total, self->m_pageSize,
                                    self->m_pageIndex);
    self->m_filteredCount = total;   /* 翻页判界与状态行共用这一份。 */
    /* 页号同步回夹取后的值（后面状态行/判界都用它）。 */
    self->m_pageIndex = (self->m_pageSize > 0) ? start / self->m_pageSize : 0;
    expect = total - start;
    if (expect > self->m_pageSize) expect = self->m_pageSize;
    if (expect < 0) expect = 0;
    XTableWidget_setRowCount(&self->m_table, expect);
    for (r = self->m_count - 1; r >= 0; --r) {
        const HsfAlarmRow* row = &self->m_rows[r];
        const char* texts[4];
        int c;
        if (!HsfAlarmLogic_rowVisible(&f, self->m_ts[r], row->m_kind,
                                      row->m_name, row->m_state))
            continue;
        if (skipped < start) { ++skipped; continue; }  /* 前面页的行跳过。 */
        if (shown >= self->m_pageSize) break;
        texts[0] = row->m_time;
        texts[1] = row->m_kind;
        texts[2] = row->m_name;
        texts[3] = row->m_state;
        for (c = 0; c < 4; ++c) {
            XTableWidgetItem* it = XTableWidgetItem_create_utf8(texts[c], 0);
            if (!it) continue;
            /* [居中显示 2026-11-12 用户口径] 三张数据表单元格统一居中。 */
            XTableWidgetItem_setTextAlignment(it, XAlignment_Center);
            XTableWidget_setItem(&self->m_table, shown, c, it);
        }
        ++shown;
    }
    if (self->m_bar) HsfDataBar_setTotalRows(self->m_bar, total);
    snprintf(buf, sizeof(buf), "筛选命中 %d 条 · 第 %d/%d 页 · 本页 %d",
             total, self->m_pageIndex + 1, pages, shown);
    XLabel_setText_2(&self->m_status, buf);
    if (self->m_bar) {
        XWidget_setEnabled((XWidget*)&self->m_prev, self->m_pageIndex > 0);
        XWidget_setEnabled((XWidget*)&self->m_next,
                           self->m_pageIndex + 1 < pages);
    }
    alarmApplyColumnFilter(self);
    XWidget_update((XWidget*)&self->m_table);
}

/** @brief 报警页导出取格上下文（网格行 → 记录行号 + 可见列映射）。 */
typedef struct AlarmExportCtx {
    const HsfDataAlarm* page;     /**< 页面（取 m_rows / m_count）。 */
    const int*          map;      /**< 网格行 → 报警记录行号（全集倒序）。 */
    const int*          cols;     /**< 导出列 → 记录字段下标（可见列升序）。 */
    int                 colCount; /**< 导出列数（cols 长度）。 */
} AlarmExportCtx;

/** @brief 导出取格回调：列序按当前可见列映射（时间/类型/描述/状态）。 */
static bool alarmExportCell(void* user, int row, int col, char* buf, int cap)
{
    const AlarmExportCtx* ctx = (const AlarmExportCtx*)user;
    const HsfAlarmRow* r;
    int field;
    if (buf && cap > 0) buf[0] = '\0';
    if (!ctx || !ctx->page || !ctx->map || !ctx->cols || !buf || cap <= 0)
        return false;
    if (row < 0 || row >= ctx->page->m_count) return false;
    if (col < 0 || col >= ctx->colCount) return false;
    r = &ctx->page->m_rows[ctx->map[row]];
    field = ctx->cols[col];
    switch (field) {
    case 0: snprintf(buf, (size_t)cap, "%s", r->m_time); return true;
    case 1: snprintf(buf, (size_t)cap, "%s", r->m_kind); return true;
    case 2: snprintf(buf, (size_t)cap, "%s", r->m_name); return true;
    case 3: snprintf(buf, (size_t)cap, "%s", r->m_state); return true;
    default: return false;
    }
}

/** @brief 导出当前筛选命中**全集**到文件（CSV 或真 XLSX；需 Download 权限）。
 *  @details [数据全在本页] 报警记录是**客户端侧的边沿历史**（服务端健康
 *           推送的下游产物，上限 ALARM_MAX_ROWS 行），不经过任何分页查询
 *           ——"全量"就是当前筛选命中的全部行，一次写盘即可，无需历史/
 *           日志页那套逐批拉取（也**无需冻结列**：同步一次写盘，表头与
 *           行内容用同一份当下可见列集合；历史/日志页冻结列是因为它们
 *           逐批异步取数，发起后列勾选还会变）。
 *           [口径：按筛选全集，不是当前页]（用户口径 2026-11-11）表格
 *           现在是客户端内部分页的，界面上只有当前页那几十行；导出按
 *           筛选命中全集（最新在前），与 Python AlarmPage._export_data
 *           同口径。检查顺序同 Python：先"无可导出的数据"（全集空），
 *           再查 Download 权限。
 *           [列跟随可见列] 表头与取格用同一份列集合（列筛选掩码升序；
 *           掩码 0 的病态输入回退全部 4 列）——界面看到哪些列就导哪些列。
 *           [格式=对话框过滤器档位] 框架文件对话框（与历史/日志页同串
 *           同序：0=Excel 工作簿、1=CSV）。用户取消（返回空串）＝本次
 *           不导出，界面保持原样。 */
static void alarmExportCsv(HsfDataAlarm* self)
{
    HsfExportGrid grid;
    AlarmExportCtx ctx;
    const char* heads[4];
    int cols[4];
    int colCount = 0;
    int map[ALARM_MAX_ROWS];
    int n = 0;
    int r;
    int c;
    int filterIdx = 0;
    uint32_t mask;
    char path[320];
    char prefix[320];
    char buf[400];
    XString* chosen = NULL;

    if (!self) return;
    /* [可见全集倒序] 最新的在前，且只导**当前筛选命中全集**（类型 +
     * 时间窗 + 关键字；客户端分页不影响）。网格行 → 记录行的映射先算
     * 好，取格回调按映射取。 */
    for (r = self->m_count - 1; r >= 0; --r) {
        if (!alarmRowVisible(self, r)) continue;
        if (n >= ALARM_MAX_ROWS) break;
        map[n++] = r;
    }
    if (n <= 0) {
        XLabel_setText_2(&self->m_status, "无可导出的数据");
        return;
    }
    if (!HsfLinkClient_can(HSF_PERM_DOWNLOAD)) {
        XLabel_setText_2(&self->m_status, "权限不足：下载数据需要普通用户及以上账户");
        return;
    }
    /* 列映射：按列筛选掩码取可见列（升序）；掩码裁到本页 4 位后若一个
     * 列都不剩（病态输入），回退全部 4 列——导出永不落成 0 列文件。 */
    mask = self->m_bar ? HsfDataBar_columnFilterMask(self->m_bar) : 0u;
    mask &= 0xFu;
    if (!mask) mask = 0xFu;
    for (c = 0; c < 4; ++c) {
        if (mask & (1u << c)) {
            cols[colCount] = c;
            heads[colCount] = kAlarmHeaders[c];
            ++colCount;
        }
    }
    snprintf(prefix, sizeof(prefix), "./hsf_alarm_%lld",
             (long long)(XDateTime_currentMSecsSinceEpoch() / 1000));
    chosen = XFileDialog_getSaveFileName_2(
        (XWidget*)&self->m_base, "导出报警记录", prefix,
        "Excel 工作簿 (*.xlsx);;CSV (*.csv)", &filterIdx);
    if (chosen) {
        const char* u8 = XString_toUtf8(chosen);
        snprintf(prefix, sizeof(prefix), "%s", u8 ? u8 : "");
        XClassDelete(chosen);
    } else {
        prefix[0] = '\0';
    }
    if (!prefix[0]) return;   /* 用户取消：静默收场 */
    HsfExport_stripKnownExt(prefix);
    ctx.page = self;
    ctx.map = map;
    ctx.cols = cols;
    ctx.colCount = colCount;
    memset(&grid, 0, sizeof(grid));
    grid.path = prefix;
    grid.sheetName = "报警";
    grid.format = HsfExport_formatFromFilterIndex(filterIdx);
    grid.colCount = colCount;
    grid.rowCount = n;
    grid.header = heads;
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

/** @brief 把关键字框文本存入生效关键字（去首尾空白；空=不筛）。
 *  @note  Python .strip() 同口径；只裁 ASCII 空白/制表符，关键字本身允许
 *         中文等 UTF-8 字节（匹配是 strstr 字节级包含）。 */
static void alarmStoreKeyword(HsfDataAlarm* self, const char* text)
{
    const char* p = text ? text : "";
    const char* end;
    size_t len;
    while (*p == ' ' || *p == '\t') ++p;
    end = p + strlen(p);
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) --end;
    len = (size_t)(end - p);
    if (len >= sizeof(self->m_kwText)) len = sizeof(self->m_kwText) - 1;
    if (len > 0) memcpy(self->m_kwText, p, len);
    self->m_kwText[len] = '\0';
}

/** @brief 筛选：读工具条类型/时间窗 + 关键字框，回第 1 页并重铺。
 *  @details [类型下拉不立即生效] 类型取下拉**当前值**（下标 0=全部类型）
 *           ——页面没接「下拉变化」回调，只在按「筛选」/关键字回车时读
 *           （Python 报警页同口径：没接 extraComboChanged）。
 *           [关键字] 当下读框（去首尾空白）；关键字回车与「筛选」同一
 *           入口（alarmKwReturnSlot 也走本函数）。 */
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
    /* [本地日历 → 墙钟 epoch] 时间框存本地日历时刻，记录 ts 是 UTC epoch
     * 毫秒，差一个本地时区偏移，此处减（历史/日志页同口径）。 */
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
    alarmStoreKeyword(self, XLineEdit_text(&self->m_kw));
    self->m_pageIndex = 0;   /* 新筛选：回第 1 页（历史/日志页同款）。 */
    alarmFillTable(self);
}

/** @brief 清除筛选：类型回全部 + 时间窗回数据全区间 + 关键字清空 +
 *         列回全勾，回第 1 页重铺。
 *  @details [三页故意不同，别统一] 报警页是历史/日志/报警三页里**唯一**
 *           清除时把列勾选也复位的页：历史/日志页的列/类别是"看哪一列/
 *           哪一类"的**视图设置**、清除筛选时故意保持（Python 端 docstring
 *           明说"我只要清时间范围，结果列也全回来了会让人莫名其妙"）；
 *           报警页按 Python AlarmPage._clear_filter 口径把列回全勾
 *           （check_all）+ 类型回全部。
 *           [列联动] setColumnFilterMask 与真实勾选同口径发
 *           ColumnFilterChanged ⇒ alarmApplyColumnFilter 立即重排列显隐；
 *           掩码已是全勾时同值早退、无回调也无需要（列本就全显）。
 *           [时间框回填数据实际区间] 第一条/最后一条记录的完整时刻
 *           （"看得见的就是全部"，不清空也不复位成"过去 24h"）；无记录
 *           时两框保持原值（XDateTimeEdit 无"空"态，Python 端清空文本框
 *           的等价落点只剩"不动"——时间窗状态已真清零）。 */
static void alarmClearFilter(HsfDataAlarm* self)
{
    XDateTime dt;
    if (!self) return;
    self->m_kindFilter = 0;
    self->m_loTs = 0;
    self->m_hiTs = 0;
    self->m_kwText[0] = '\0';
    XLineEdit_setText(&self->m_kw, "");
    if (self->m_bar) {
        HsfDataBar_setChannelIndex(self->m_bar, 0);
        HsfDataBar_setColumnFilterMask(self->m_bar, 0xFu); /* 4 列全勾 */
        if (self->m_count > 0) {
            XDateTime_setMSecsSinceEpoch(&dt, self->m_ts[0] + self->m_tzOffsetMs);
            HsfDataBar_setStartDateTime(self->m_bar, &dt);
            XDateTime_setMSecsSinceEpoch(&dt, self->m_ts[self->m_count - 1] +
                                             self->m_tzOffsetMs);
            HsfDataBar_setEndDateTime(self->m_bar, &dt);
        }
    }
    self->m_pageIndex = 0;   /* 回第 1 页。 */
    alarmFillTable(self);
}

/** @brief 工具条动作落地（报警页自己那套）。
 *  @note  无图表形态（用户定案 2026-11-11：日志和报警都不要图表）⇒
 *         GenerateChart/ViewTable/ViewChart 三个动作本页不再接（工具条
 *         的 Chart 特性位已关，两颗切换钮 + 生成折线图钮随位隐藏）。 */
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
        break;
    case HsfDataBarAction_ColumnFilterChanged:
        alarmApplyColumnFilter(self);
        break;
    default:
        break;
    }
}

/* ==================== 行 2：分页 / 关键字 / 滚动锁定 ==================== */

/** @brief 上一页（判界：页号 >0 才翻；翻页纯本地重铺，无网络请求）。 */
static void alarmPrevSlot(XObject* receiver, XVarList* args)
{
    HsfDataAlarm* self = (HsfDataAlarm*)((char*)receiver -
                                         offsetof(HsfDataAlarm, m_prev));
    (void)args;
    if (!self) return;
    if (self->m_pageIndex > 0) {
        --self->m_pageIndex;
        alarmFillTable(self);
    }
}

/** @brief 下一页（判界：页号+1 < 页数才翻；页数按最近筛选命中全集算）。 */
static void alarmNextSlot(XObject* receiver, XVarList* args)
{
    HsfDataAlarm* self = (HsfDataAlarm*)((char*)receiver -
                                         offsetof(HsfDataAlarm, m_next));
    (void)args;
    if (!self) return;
    if (self->m_pageIndex + 1 < HsfAlarmLogic_pageCount(self->m_filteredCount,
                                                        self->m_pageSize)) {
        ++self->m_pageIndex;
        alarmFillTable(self);
    }
}

/** @brief 改每页条数：回第 1 页重铺（数据在内存，不重取）。
 *  @note  [pageSizeChanged 发的是下拉 index 不是页大小值] currentIndexChanged
 *         载荷是**下标**（0/1/2）⇒ 本槽经 sender 找回页对象后**必须读
 *         currentIndex 再查 kAlarmPageSizes 表**，绝不能把下标当页大小用
 *         （写成带参槽会把页大小错设成 1/2——Python 端同款实测坑）。 */
static void alarmPageSizeSlot(XObject* receiver, XVarList* args)
{
    HsfDataAlarm* self = (HsfDataAlarm*)((char*)receiver -
                                         offsetof(HsfDataAlarm, m_pageSizeBox));
    int idx;
    (void)args;
    if (!self) return;
    idx = XComboBox_currentIndex(&self->m_pageSizeBox);
    if (idx < 0 || idx >= HSF_ALARM_PAGE_SIZES_COUNT) return;
    if (kAlarmPageSizes[idx] == self->m_pageSize) return; /* 同值早退。 */
    self->m_pageSize = kAlarmPageSizes[idx];
    self->m_pageIndex = 0;
    alarmFillTable(self);
}

/** @brief 关键字回车 = 应用筛选（沿用当前时间窗/类型；与「筛选」同入口）。 */
static void alarmKwReturnSlot(XObject* receiver, XVarList* args)
{
    HsfDataAlarm* self = (HsfDataAlarm*)((char*)receiver -
                                         offsetof(HsfDataAlarm, m_kw));
    (void)args;
    alarmApplyFilter(self);
}

/** @brief 滚动锁定勾选变化：锁定期新报警只入库不刷表；解锁补刷一次。
 *  @note  [语义对齐历史/Python] 锁定只停**铺表**，边沿记录照常入库
 *         （alarmAppend 无条件跑）；解锁那一刻补一次重铺——锁定期到达的
 *         报警按最新全集重新切片，一次可见。toggled 在状态翻转**之后**
 *         发射 ⇒ 槽里直接读勾选态即新态。receiver=复选框内嵌对象（真
 *         XObject，框架信号槽契约），offsetof 折回页指针（历史/日志页
 *         行 2 控件同款）。 */
static void alarmFreezeSlot(XObject* receiver, XVarList* args)
{
    HsfDataAlarm* self = (HsfDataAlarm*)((char*)receiver -
                                         offsetof(HsfDataAlarm, m_freeze));
    (void)args;
    if (!self) return;
    self->m_frozen = XCheckBox_isChecked(&self->m_freeze);
    if (!self->m_frozen) alarmFillTable(self);   /* 解锁补刷一次。 */
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
    /* [滚动锁定] 新报警照常入库（上面 alarmAppend 无条件跑）；锁定期只
     * 跳过刷表，解锁槽里补刷一次。 */
    if (changed && !self->m_frozen) alarmFillTable(self);
}

/** @brief 字号实时重放（本地设置改动时由 HsfUi 调回）。
 *  @details [2026-11-10 补齐] 原版只重放标题/状态两个标签——表格本体
 *           字号、列宽与工具条都不跟（与历史/日志页同款口径补齐）。
 *           pct 为合比（全局 × 页）；表格/工具条基准同建页（16 / 120·
 *           120·180·120 / BAR），与「基准=源码常量」幂等锚点一致。 */
static void alarmApplyFontsAll(XWidget* page, int pct)
{
    HsfDataAlarm* self = (HsfDataAlarm*)page;
    if (!self || pct <= 0) return;
    XLabel_setTextPixelSize(&self->m_title, 16 * pct / 100);
    XLabel_setTextPixelSize(&self->m_status, 12 * pct / 100);
    {
        /* 表格本体字号（基准 16）。 */
        XFont tf = XWidget_font((XWidget*)&self->m_table);
        XFont_setPixelSize(&tf, 16 * pct / 100);
        XWidget_setFont((XWidget*)&self->m_table, &tf);
        XClassDeinit(&tf);
    }
    {
        /* 列宽重放：全宽×pct。列显隐由 setColumnHidden 独立承担，
         * 缩放重放不会把隐藏列"撑回来"。 */
        int c;
        for (c = 0; c < 4; ++c)
            XTableView_setColumnWidth(&self->m_table.m_base, c,
                                      alarmColWidth(c) * pct / 100);
    }
    /* 行高随字号（表格缺省行高 24 为基准；只铺已存在的行）。 */
    {
        const int rows = XTableWidget_rowCount(&self->m_table);
        const int rh = (24 * pct + 50) / 100;
        int r;
        for (r = 0; r < rows; ++r)
            XTableView_setRowHeight(&self->m_table.m_base, r, rh);
    }
    /* [行2 页级控件随字号] 分页/关键字/滚动锁定并入工具条行 2：固定高
     * 与工具条同一比例重放（历史/日志页同款；数组长度/赋值数/循环上界
     * 三处一致）。 */
    {
        XWidget* row2[5];
        int i;
        const int h = 28 * pct / 100;
        row2[0] = (XWidget*)&self->m_prev;
        row2[1] = (XWidget*)&self->m_next;
        row2[2] = (XWidget*)&self->m_pageSizeBox;
        row2[3] = (XWidget*)&self->m_kw;
        row2[4] = (XWidget*)&self->m_freeze;
        for (i = 0; i < 5; ++i) XWidget_setFixedHeight(row2[i], h);
    }
    if (self->m_bar) HsfDataBar_applyFont(self->m_bar, pct);
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
    HsfTheme_applySecondaryText((XWidget*)&self->m_status);
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
        const char* channelItems[ALARM_KIND_FILTER_AUTO + 1];
        const char* colItems[4];
        XDateTime now;
        int i;
        channelItems[0] = "全部类型";
        for (i = 0; i < 8; ++i) channelItems[i + 1] = kKinds[i];
        /* [尾项=自动运行] 自动运行告警行的类型列文案（ALARM_KIND_TEXT_AUTO
         * 逐字一致 ⇒ 类型筛选按全等匹配能单选这一类；见 alarmBuildFilter）。 */
        channelItems[ALARM_KIND_FILTER_AUTO] = ALARM_KIND_TEXT_AUTO;
        HsfDataBar_setChannelItems(self->m_bar, channelItems,
                                   ALARM_KIND_FILTER_AUTO + 1);
        HsfDataBar_setChannelIndex(self->m_bar, 0);
        /* 列筛选 = **勾选显示哪些列**（多选复选；默认全勾）；口径对齐 Python
         * 的 MultiCheckCombo（只列列名，全选时按钮文字「全部列（全部）」）。 */
        for (i = 0; i < 4; ++i) colItems[i] = kAlarmHeaders[i];
        HsfDataBar_setColumnFilterItems(self->m_bar, colItems, 4);
        now = XDateTime_currentDateTime();
        HsfDataBar_setEndDateTime(self->m_bar, &now);
        HsfDataBar_setActionCallback(self->m_bar, alarmBarAction, self);
        /* [按页裁剪工具条控件 2026-11-11 对齐 Python] 报警页**没有图表
         * 形态**（用户定案：日志和报警都不要图表；Python 端折线图与表格/
         * 图表双视图已整体删除）⇒ Chart 位关掉，「生成折线图」+「表格/
         * 图表」三颗钮随位隐藏。也**没有**「小数位」「每图例最多点数」
         * 的处理分支（alarmBarAction 里无对应 case）⇒ 两个微调框是
         * 「可见但改了没反应」的假控件，藏掉。
         * [图例钮一并藏 2026-11-11 复审] Legend 位随 Chart 同关：本页从不
         * setLegendItems（空表时按钮点了直接 return，多选组件的空表守卫见
         * HsfMultiSelect.c 的 msBtnSlot），显出来就是「选了不兑现」死钮。
         * [导出格式下拉已删 2026-11-10] 格式改由文件对话框的过滤器档位
         * 决定（alarmExportCsv），下拉没有存在意义。保留：类型下拉/时间
         * 窗/列多选/筛选/清除筛选/导出/重新加载。 */
        HsfDataBar_setFeatures(self->m_bar,
            HsfDataBarFeature_All
            & ~(unsigned int)(HsfDataBarFeature_Chart
                              | HsfDataBarFeature_Legend
                              | HsfDataBarFeature_Decimals
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
        XTableView_setColumnWidth(&self->m_table.m_base, 0, kAlarmColWidths[0]);
        XTableView_setColumnWidth(&self->m_table.m_base, 1, kAlarmColWidths[1]);
        XTableView_setColumnWidth(&self->m_table.m_base, 2, kAlarmColWidths[2]);
        XTableView_setColumnWidth(&self->m_table.m_base, 3, kAlarmColWidths[3]);
        XTableWidget_setVerticalHeaderVisible(&self->m_table, false);
        XTableView_setShowGrid(&self->m_table.m_base, false);
    }
    XWidget_show((XWidget*)&self->m_table);
    XBoxLayout_addWidgetEx(&self->m_layout, (XWidget*)&self->m_table, 1, 0);

    /* [分页/关键字/滚动锁定控件] 建页时以页根为父，但**布局位置**挂进
     * 顶部工具条第二行（attachRow2 内部走布局挂接，XLayout_linkItem 会
     * 沿布局链上溯并 XWidget_setParent_2 到工具条——挂接后父对象是工具
     * 条，不是页根；控件是页面结构体的内嵌成员，地址不随父链变化，故
     * 信号 receiver 继续接控件自身。历史/日志页同款注记）。 */
    XPushButton_init(&self->m_prev, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_prev);
    XPushButton_setText_2(&self->m_prev, "上一页");
    XWidget_setFixedHeight((XWidget*)&self->m_prev, 28);
    XObject_connect((XObject*)&self->m_prev,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&self->m_prev, alarmPrevSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_prev);

    XPushButton_init(&self->m_next, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_next);
    XPushButton_setText_2(&self->m_next, "下一页");
    XWidget_setFixedHeight((XWidget*)&self->m_next, 28);
    XObject_connect((XObject*)&self->m_next,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&self->m_next, alarmNextSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_next);

    /* 每页条数（与历史/日志页同档口径；槽读 currentIndex 查表，见
     * alarmPageSizeSlot 的坑注）。 */
    XComboBox_init(&self->m_pageSizeBox, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_pageSizeBox);
    XComboBox_addItem_2(&self->m_pageSizeBox, "10 条");
    XComboBox_addItem_2(&self->m_pageSizeBox, "20 条");
    XComboBox_addItem_2(&self->m_pageSizeBox, "50 条");
    XComboBox_setCurrentIndex(&self->m_pageSizeBox, 1);
    XWidget_setFixedHeight((XWidget*)&self->m_pageSizeBox, 28);
    XWidget_setMinimumWidth((XWidget*)&self->m_pageSizeBox, 92);
    XObject_connect((XObject*)&self->m_pageSizeBox,
                      XSignal(XComboBox_currentIndexChanged_signal),
                      (XObject*)&self->m_pageSizeBox, alarmPageSizeSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_pageSizeBox);

    /* 关键字筛选（客户端内筛：设备/类型/状态包含匹配，回车=应用）。 */
    XLineEdit_init(&self->m_kw, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_kw);
    XLineEdit_setPlaceholderText(&self->m_kw, "关键字（设备/类型/状态）");
    XLineEdit_setClearButtonEnabled(&self->m_kw, true);
    XWidget_setFixedHeight((XWidget*)&self->m_kw, 28);
    XWidget_setMinimumWidth((XWidget*)&self->m_kw, 160);
    XObject_connect((XObject*)&self->m_kw,
                      XSignal(XLineEdit_returnPressed_signal),
                      (XObject*)&self->m_kw, alarmKwReturnSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_kw);

    /* 滚动锁定（文本对齐 Python DataPageToolbar 的冻结框口径）：勾上=
     * 新报警只入库不刷表；取消勾选补刷一次（alarmFreezeSlot）。 */
    XCheckBox_init(&self->m_freeze, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_freeze);
    XCheckBox_setText_2(&self->m_freeze, "滚动锁定（停止刷表，后台照常采样）");
    XWidget_setFixedHeight((XWidget*)&self->m_freeze, 28);
    XObject_connect((XObject*)&self->m_freeze,
                      XSignal(XAbstractButton_toggled_signal),
                      (XObject*)&self->m_freeze, alarmFreezeSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_freeze);

    if (self->m_bar) {
        XWidget* row2[5];
        row2[0] = (XWidget*)&self->m_prev;
        row2[1] = (XWidget*)&self->m_next;
        row2[2] = (XWidget*)&self->m_pageSizeBox;
        row2[3] = (XWidget*)&self->m_kw;
        row2[4] = (XWidget*)&self->m_freeze;
        HsfDataBar_attachRow2(self->m_bar, row2, 5, NULL, 0);
    }

    /* [分页初值] 页大小=中间档 20（不持久化，与 Python 同口径）；页号
     * 第 1 页；翻页钮按 0 行全集判界置灰（首铺前状态行保持本页引言）。 */
    self->m_pageSize = 20;
    self->m_pageIndex = 0;
    self->m_filteredCount = 0;
    self->m_frozen = false;
    XWidget_setEnabled((XWidget*)&self->m_prev, false);
    XWidget_setEnabled((XWidget*)&self->m_next, false);

    self->m_timer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_timer) {
        /* 轮询周期取本地设置「界面刷新周期」（数据刷新卡；缺省 200ms）。 */
        XTimer_setInterval(self->m_timer, HsfSettingsLocal_refreshMs());
        XTimer_setTimerCallback(self->m_timer, alarmTimerCb);
        XTimer_setUserData(self->m_timer, self);
        XTimer_start_base(self->m_timer);
    }

    /* [自动运行告警事件源] TOPIC_AUTO_ALARM 由 HsfLinkClient 连接期常驻
     * 订阅（页建在启动期 ⇒ 回调先于任何告警到达；事件发生在任何页面都
     * 落本页行缓冲）。回调在主事件循环内直呼（框架链路口径），可直改 UI。 */
    HsfLinkClient_setAutoAlarmCallback(alarmAutoAlarmCb, self);

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
