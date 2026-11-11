/**
 * @file       HsfDataLog.c
 * @brief      数据-日志叶子页：服务端运行日志分页（0x0119）→ 表格 + 翻页。
 * @details    [数据源] 服务端 SQLite logs 表（关键运维事件定点入库：
 *             conn=会话接入/断开、ctrl=控制受理/拒绝、alarm=阀告警、
 *             auto=自动运行/模式切换、auth=登录成功/被拒），经
 *             HSF_CMD_LOG_PAGE（{"limit","offset","beforeTs","loTs","hiTs",
 *             "category","kw"} → {"ok","rows":[{ts,category,text}…],
 *             "total","count","offset","limit"}）请求-应答拉取。
 *             [结构] 照 HsfDataHistory 的自建根结构模式：工具条
 *             （HsfDataBar，本页自己一份实例）+ 表格 + 分页控件（挂工具条
 *             行 2）+ 200ms 应答轮询（按服务端回显的 offset/limit 认领，
 *             与 histTimerCb 同机制）。
 *             [形态约束·必读] HsfUi.c 的 kHsfPageAdapt[10] 固定调
 *             HsfDataTablePage_adapt（HsfUi.c 本模块不可改），它按结构
 *             偏移取 m_layout/m_bar——本页结构**前四成员必须与
 *             HsfDataTablePage 同型同序**（XWidget/XBoxLayout/XLabel/
 *             XWidget*）。HsfDataHistory 当年把 m_status 插在 m_table
 *             位置、误挂 HsfDataTablePage_applyFont 曾按异型偏移读出
 *             垃圾指针段错误——本页只共享到 m_bar 为止（adapt 只摸前
 *             四成员），字号缩放走自己的 logApplyFonts，不复用
 *             HsfDataTablePage_applyFont。
 */
#include "HsfDataLog.h"

#include "HsfSettingsLocal.h" /* 轮询周期=本地设置「界面刷新周期」 */

#include "HsfLinkClient.h"
#include "HsfDataBar.h"
#include "HsfExport.h"      /* 共享导出模块（CSV / 真 XLSX）。 */
#include "HsfTableSort.h"
#include "HsfTheme.h"
#include "HsfUi.h"   /* hsfUi_pageFontPx（本地设置·按页字号） */
#include "XBoxLayout.h"
#include "XComboBox.h"
#include "XDate.h"
#include "XDateTime.h"
#include "XFileDialog.h"  /* 导出走框架文件对话框（格式=过滤器档位） */
#include "XHeaderView.h"
#include "XLabel.h"
#include "XLineEdit.h"
#include "XObject.h"     /* XObject_connect_1 / destroyed 信号。 */
#include "XPushButton.h"
#include "XString.h"
#include "XTableWidget.h"
#include "XTime.h"
#include "XTimer.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XTABLEWIDGET_ON && XLABEL_ON && XPUSHBUTTON_ON && XLAYOUT_ON

/** @brief 本页字号折算（本地设置 FontScale/Page10）。 */
static int logPx(int px)
{
    return hsfUi_pageFontPx(HSF_PAGE_DATA_LOG, px);
}

/** @brief 每页行数缺省（与 HSF_LOG_PAGE_DEFAULT 同值；档位对齐历史页）。 */
#define LOG_PAGE_LIMIT   20
/** @brief 类别数（conn/ctrl/alarm/auto/auth/sys；下拉 0 号位是"全部类别"）。
 *  @details [为什么有第 6 档 sys] C 端 logs 表只收五个定点事件类；
 *           **Python 服务端**另收大量非事件类业务日志（采集失败、健康变化、
 *           配置变更、装配保真行…），统一归 "sys"（见 server_py/hsfsrv/
 *           proto.py 的 LOG_CAT_SYS）。表里不认这一档的话，那类行的
 *           「类别」列会显示原始字面 "sys"（2026-11-10 现场截图反馈
 *           「中文」），且下拉筛不到它。C 服务端不产生 sys 行 ⇒ 选中它
 *           只会得到空列表（服务端按字面下推 WHERE，不做白名单校验），
 *           不是报错。
 */
#define LOG_CATEGORY_COUNT 6
/** @brief 页大小可选档（与历史页同档口径）。 */
static const int kLogPageSizes[3] = { 10, 20, 50 };

/** @brief 全量导出拉取的单批行数（固定值，独占一档 limit）。 */
#define LOG_EXPORT_CHUNK   40
/** @brief 导出批的应答看门狗拍数（25ms × 40 ≈ 1s 没等到就重发）。 */
#define LOG_EXPORT_WATCHDOG_TICKS  40
/** @brief 导出批最多重发次数（连续不回才放弃）。 */
#define LOG_EXPORT_MAX_RETRIES     3
/** @brief 导出期间的应答轮询周期（毫秒；一批一跳，取完恢复）。 */
#define LOG_EXPORT_POLL_MS   25
/** @brief XLSX 全量导出的行数上限（in-memory 工作簿；与历史页同预算）。
 *  @details CSV 流式写盘无上限；XLSX 的 zip 包要整体写出（finish 才
 *           saveAs），行数无上限会把板端内存吃穿。触顶按已取部分收尾
 *           并在状态行注明——绝不静默少行。 */
#define LOG_EXPORT_XLSX_MAX_ROWS   12000
/* 编译期兜底：导出批 limit 必须与页大小 10/20/50 不相交（应答槽单份，
 * 本页按服务端回显的 (offset,limit) 认领应答，见 logTimerCb）。 */
#if (LOG_EXPORT_CHUNK == 10) || (LOG_EXPORT_CHUNK == 20) || \
    (LOG_EXPORT_CHUNK == 50) || (LOG_EXPORT_CHUNK < 1) || \
    (LOG_EXPORT_CHUNK > 50)
#error "log export chunk limit must be disjoint from page sizes 10/20/50"
#endif


/** @brief 日志列头（3 列；静态存活：表格直存指针）。 */
static const char* const kLogHeaders[3] = {
    "时间",   /* 时间 */
    "类别",   /* 类别 */
    "事件"    /* 事件 */
};

/** @brief 类别下拉显示名（与工具条通道下拉对齐；0=全部）。 */
static const char* const kLogCategoryNames[LOG_CATEGORY_COUNT + 1] = {
    "全部类别",
    "连接",   /* conn */
    "控制",   /* ctrl */
    "告警",   /* alarm */
    "自动",   /* auto */
    "鉴权",   /* auth */
    "系统"    /* sys（Python 服务端专有的一类业务日志） */
};
/** @brief 类别入库字面（与 HsfServerStore.h HsfLogRow 口径一致；0=空=全部）。 */
static const char* const kLogCategoryKeys[LOG_CATEGORY_COUNT + 1] = {
    "",
    "conn",
    "ctrl",
    "alarm",
    "auto",
    "auth",
    "sys"
};

/** @brief 类别码 → 中文显示名（与下拉同表；未知码原样显示，不丢信息）。
 *  @details [为什么要映射] 协议里传的是 ASCII 类别码（conn/ctrl/alarm/auto/
 *           auth，与 C 服务端 HsfServerStore 的口径一致），界面要给人看的
 *           是中文；下拉早就用 kLogCategoryNames 显示中文了，只有表格
 *           单元格漏了映射，整列是 auth/conn（现场截图确认）。
 */
static const char* logCategoryText(const char* key)
{
    int i;
    if (!key || !key[0]) return "-";
    for (i = 1; i <= LOG_CATEGORY_COUNT; ++i) {
        if (strcmp(key, kLogCategoryKeys[i]) == 0) return kLogCategoryNames[i];
    }
    return key;
}


/**
 * @brief 日志页对象。
 * @warning m_base..m_bar 四成员与 HsfDataTablePage 同型同序（见文件头
 *          「形态约束」）——顺序不可动。
 */
typedef struct HsfDataLogPage {
    /* [布局兼容前缀] ↓↓ 与 HsfDataTablePage 前四成员逐一同型同序 ↓↓ */
    XWidget      m_base;        /**< 页根（第一成员）。 */
    XBoxLayout   m_layout;      /**< 页根竖排布局。 */
    XLabel       m_title;       /**< 页标题。 */
    XWidget*     m_bar;         /**< 通用工具条（本页自己一份实例；借用）。 */
    /* [布局兼容前缀] ↑↑ 以上四成员勿动 ↑↑ */
    XLabel       m_status;      /**< 状态行（共 N 条 · 第 x/y 页）。 */
    XTableWidget m_table;       /**< 数据表（时间/类别/事件 3 列）。 */
    HsfTableSort m_sort;      /**< 双击表头排序状态（当前页内）。 */
    XPushButton  m_prev;        /**< 上一页。 */
    XPushButton  m_next;        /**< 下一页。 */
    XComboBox    m_pageSizeBox; /**< 每页条数（10/20/50）。 */
    XLineEdit    m_kw;          /**< 关键字输入（下推服务端）。 */
    XTimer*      m_timer;       /**< 应答轮询定时器（界面刷新周期）。 */
    int64_t      m_tzOffsetMs;  /**< 本地时区偏移（epoch 与日历显示互转）。 */
    int64_t      m_loTs;        /**< 时间窗下界（epoch 毫秒；<=0 不限）。 */
    int64_t      m_hiTs;        /**< 时间窗上界（epoch 毫秒；<=0 不限）。 */
    int64_t      m_anchorTs;    /**< 分页窗口锚点 beforeTs（0=下次发包取"现在"）。 */
    char         m_kwText[160]; /**< 当前生效的关键字（空=不筛）。 */
    char         m_category[16];/**< 当前生效的类别字面（空=全部）。 */
    int          m_pageSize;    /**< 当前页大小。 */
    int          m_offset;      /**< 当前页起始偏移。 */
    int          m_shownOffset; /**< 已填表的偏移（变了就重填）。 */
    int          m_shownCount;  /**< 已填表的行数。 */
    int          m_shownTotal;  /**< 已填表那一页的命中总行数（翻页判界用）。 */
    uint32_t     m_seenSeq;     /**< 已处置（填表或丢弃）的应答序号。 */
    /* ---- 在途请求归属（应答槽只有一份；页面拿不到 reqId） ----
     * 与 HsfDataHistory 同款：按服务端回显的 offset/limit 认领。本页两类
     * 消费者（表格页 10/20/50、导出批 40）limit 恒不相交，见 logTimerCb。 */
    bool         m_pageReqValid;   /**< 有在途分页请求。 */
    int          m_pageReqOffset;  /**< 在途分页请求的 offset。 */
    int          m_pageReqLimit;   /**< 在途分页请求的 limit。 */
    /* ---- 全量导出（按当前筛选条件逐批拉全；0x0119 连续分页） ----
     * [定格语义] 发起那一刻把 锚点/时间窗/类别/关键字/路径/格式定格进
     * m_export* 快照；等待期间用户改界面（翻页/改筛选/改类别）不影响
     * 在途导出的内容——logs 表持续入库，锚点不定格会每批漂移。 */
    bool         m_exportFetching; /**< 全量导出累加中（逐批在途）。 */
    int          m_exportOffset;   /**< 在途导出批的 offset（应答归属校验）。 */
    int          m_exportLimit;    /**< 在途导出批的 limit（恒 LOG_EXPORT_CHUNK）。 */
    int          m_exportTotal;    /**< 首批应答记下的命中总行数（0=未知）。 */
    int          m_exportWritten;  /**< 已写进行文件的行数。 */
    int          m_exportIdle;     /**< 在途批等应答的拍数（看门狗）。 */
    int          m_exportRetries;  /**< 当前批已重发次数（超限放弃）。 */
    bool         m_exportTruncated;/**< XLSX 触上限截断（状态行注明）。 */
    HsfExportWriter m_exportWriter;/**< 流式写出器（open..finish 生命周期）。 */
    HsfExportFormat m_exportFormat;/**< 本次导出的格式（对话框过滤器定格）。 */
    char         m_exportPath[HSF_EXPORT_PATH_CAP]; /**< 落地路径前缀（无扩展名）。 */
    int64_t      m_exportAnchor;   /**< 冻结的分页锚点 beforeTs。 */
    int64_t      m_exportLoTs;     /**< 冻结的时间窗下界。 */
    int64_t      m_exportHiTs;     /**< 冻结的时间窗上界。 */
    char         m_exportCategory[16]; /**< 冻结的类别字面（空=全部）。 */
    char         m_exportKw[160];  /**< 冻结的关键字（空=不筛）。 */
    int          m_rowBaseH;    /**< 建页时行高（随字号缩放）。 */
} HsfDataLogPage;

/* ==================== 时间显示 ==================== */

/**
 * @brief epoch 毫秒 → "yyyy-MM-dd HH:mm:ss"（本地时区；与 HsfLinkHistory.c
 *        hsfHistFormatTs 同算法——"当前本地时刻 − 该行年龄"反推，框架的
 *        XDate/XTime 无 epoch 构造口；格式手工拼（框架 toString 不补零）。
 */
static void logFormatTs(int64_t ts, char* out, int cap)
{
    int64_t age;
    int64_t days;
    int remMs;
    XDate date;
    XTime time;

    if (!out || cap <= 0) return;
    out[0] = '\0';
    if (ts <= 0) {
        snprintf(out, (size_t)cap, "--");
        return;
    }
    age = XDateTime_currentWallMSecsSinceEpoch() - ts;
    if (age < 0) age = 0;
    days = age / 86400000LL;
    remMs = (int)(age % 86400000LL);
    date = XDate_currentDate();
    time = XTime_currentTime();
    date = XDate_addDays(&date, -days);
    time = XTime_addMSecs(&time, -remMs);
    snprintf(out, (size_t)cap, "%04d-%02d-%02d %02d:%02d:%02d",
             XDate_year(&date), XDate_month(&date), XDate_day(&date),
             XTime_hour(&time), XTime_minute(&time), XTime_second(&time));
}

/** @brief 本地时区偏移（毫秒）：日历型 XDateTime − 墙钟 epoch（历史页同款）。 */
static int64_t logTzOffsetMs(void)
{
    XDateTime now = XDateTime_currentDateTime();
    return XDateTime_toMSecsSinceEpoch(&now) -
           XDateTime_currentWallMSecsSinceEpoch();
}

/* ==================== 填表 ==================== */

/** @brief 用**本页自己的那份应答**重填表格 + 状态行。
 *  @param self 页面。
 *  @param page 已按 offset/limit 归属确认为"本页那一页"的应答。
 *  @details 应答槽独立于历史页（0x0119 专用 m_logPage），但导出与分页
 *           同槽，仍要调用方先做归属判定（logTimerCb）。 */

/**
 * @brief 双击表头：排序当前页（口径与 Python 客户端 _header_dblclicked 一致：
 *        新列⇒降序；同列降⇒升序；同列升⇒取消排序）。
 */
/* [前向声明] 取消排序分支要回调请求函数（定义在下方）。 */
static void logRequest(HsfDataLogPage* self, int offset);

static void HsfDataLogPageHeaderDblClicked(XObject* receiver, XVarList* args)
{
    HsfDataLogPage* self = (HsfDataLogPage*)receiver;
    int col = -1;
    if (!self) return;
    if (args) {
        XVarList_start(args);
        col = XVarList_arg(args, int);
    }
    HsfTableSort_toggle(&self->m_sort, col);
    if (self->m_sort.m_col < 0) {
        /* [取消排序 ⇒ 回到服务端原序] 表里此刻已是排过序的行，本地没有
         * 原序可还原 ⇒ 重取当前页（服务端按 ts 降序给回原序）。 */
        logRequest(self, self->m_offset);
        return;
    }
    HsfTableSort_apply(&self->m_sort, &self->m_table);
    HsfTableSort_updateIndicator(&self->m_sort, &self->m_table);
}

static void logFillTable(HsfDataLogPage* self, const HsfLinkLogPage* page)
{
    int i;
    char buf[192];

    if (!self || !page || !page->m_valid) return;
    if (!page->m_ok) {
        /* [否定应答可观测] 未开库/服务端拒绝时把原因顶到状态行，别让
         * 页面停留在"读取中"。 */
        XTableWidget_setRowCount(&self->m_table, 0);
        self->m_shownOffset = page->m_offset;
        self->m_shownCount = 0;
        self->m_shownTotal = 0;
        snprintf(buf, sizeof(buf), "查询失败：%s",
                 page->m_err[0] ? page->m_err : "服务端拒绝");
        XLabel_setText_2(&self->m_status, buf);
        if (self->m_bar) HsfDataBar_setTotalRows(self->m_bar, 0);
        XWidget_setEnabled((XWidget*)&self->m_prev, false);
        XWidget_setEnabled((XWidget*)&self->m_next, false);
        return;
    }
    XTableWidget_setRowCount(&self->m_table, page->m_count);
    for (i = 0; i < page->m_count; ++i) {
        char tsText[40];
        XTableWidgetItem* it;
        logFormatTs(page->m_rows[i].m_ts, tsText, (int)sizeof(tsText));
        it = XTableWidgetItem_create_utf8(tsText, 0);
        if (it) XTableWidget_setItem(&self->m_table, i, 0, it);
        it = XTableWidgetItem_create_utf8(
                logCategoryText(page->m_rows[i].m_category), 0);
        if (it) XTableWidget_setItem(&self->m_table, i, 1, it);
        it = XTableWidgetItem_create_utf8(page->m_rows[i].m_text, 0);
        if (it) XTableWidget_setItem(&self->m_table, i, 2, it);
    }
    self->m_shownOffset = page->m_offset;
    self->m_shownCount = page->m_count;
    self->m_shownTotal = page->m_total;
    {
        const int limit = (page->m_limit > 0) ? page->m_limit : self->m_pageSize;
        const int cur = (limit > 0) ? (page->m_offset / limit + 1) : 1;
        const int pages = (limit > 0) ? ((page->m_total + limit - 1) / limit) : 1;
        snprintf(buf, sizeof(buf),
                 "共 %d 条（本页 %d） · 第 %d/%d 页%s",
                 page->m_total, page->m_count, cur, pages,
                 page->m_count ? "" : "（无数据）");
        XLabel_setText_2(&self->m_status, buf);
    }
    if (self->m_bar)
        HsfDataBar_setTotalRows(self->m_bar, page->m_total);
    XWidget_setEnabled((XWidget*)&self->m_prev, self->m_shownOffset > 0);
    XWidget_setEnabled((XWidget*)&self->m_next,
                       self->m_shownOffset + self->m_shownCount < page->m_total);
    XWidget_update((XWidget*)&self->m_table);
}

/* ==================== 请求 ==================== */

/** @brief 发起一页请求（offset 为该页起点；窗口/类别/关键字随请求下发）。
 *  @details [锚点] 新查询（筛选/类别/页大小/重新加载）先置 m_anchorTs=0，
 *           本函数发包前取"现在"并定格；翻页沿用同一锚点 ⇒ 整段分页钉在
 *           同一批数据上（logs 表持续入库，不带锚翻页会被新行顶偏）。 */
static void logRequest(HsfDataLogPage* self, int offset)
{
    if (!self) return;
    if (offset < 0) offset = 0;
    if (self->m_pageSize <= 0) self->m_pageSize = LOG_PAGE_LIMIT;
    self->m_offset = offset;
    self->m_pageReqValid = false;
    if (self->m_anchorTs <= 0)
        self->m_anchorTs = XDateTime_currentWallMSecsSinceEpoch();
    if (!HsfLinkClient_logQueryWindow(offset, self->m_pageSize,
                                      self->m_anchorTs,
                                      self->m_loTs, self->m_hiTs,
                                      self->m_category[0] ? self->m_category : NULL,
                                      self->m_kwText[0] ? self->m_kwText : NULL)) {
        /* [失败终局可观测] 连接未就绪/会话缺失时请求根本发不出去。 */
        XLabel_setText_2(&self->m_status, "查询失败：服务端未连接");
        XPrintf("HsfDataLog: log query failed (offset=%d, limit=%d)\n",
                offset, self->m_pageSize);
        return;
    }
    self->m_pageReqOffset = offset;
    self->m_pageReqLimit = self->m_pageSize;
    self->m_pageReqValid = true;
}

/* ==================== 翻页 / 筛选 ==================== */

/** @brief 上一页。 */
static void logPrevSlot(XObject* receiver, XVarList* args)
{
    HsfDataLogPage* self = (HsfDataLogPage*)((char*)receiver -
                                            offsetof(HsfDataLogPage, m_prev));
    (void)args;
    if (!self) return;
    if (self->m_offset <= 0) return;
    logRequest(self, self->m_offset - self->m_pageSize);
}

/** @brief 下一页（判界用本页快照 m_shown*，与历史页同口径）。 */
static void logNextSlot(XObject* receiver, XVarList* args)
{
    HsfDataLogPage* self = (HsfDataLogPage*)((char*)receiver -
                                            offsetof(HsfDataLogPage, m_next));
    (void)args;
    if (!self) return;
    if (self->m_shownCount <= 0) return;
    if (self->m_shownOffset + self->m_shownCount >= self->m_shownTotal) return;
    logRequest(self, self->m_offset + self->m_pageSize);
}

/** @brief 筛选：工具条时间窗 + 关键字 + 类别 → 服务端筛，回第 1 页。 */
static void logApplyFilter(HsfDataLogPage* self)
{
    XDateTime lo;
    XDateTime hi;
    int64_t loMs;
    int64_t hiMs;
    const char* text;
    if (!self || !self->m_bar) return;
    lo = HsfDataBar_startDateTime(self->m_bar);
    hi = HsfDataBar_endDateTime(self->m_bar);
    loMs = XDateTime_isValid(&lo) ? XDateTime_toMSecsSinceEpoch(&lo) : 0;
    hiMs = XDateTime_isValid(&hi) ? XDateTime_toMSecsSinceEpoch(&hi) : 0;
    /* [本地日历 → 墙钟 epoch] 与历史页同口径：时间框存本地日历时刻，
     * 库里 ts 是 UTC epoch 毫秒，差一个本地时区偏移，此处减。 */
    if (loMs > 0) loMs -= self->m_tzOffsetMs;
    if (hiMs > 0) hiMs -= self->m_tzOffsetMs;
    if (loMs > 0 && hiMs > 0 && loMs > hiMs) {
        XLabel_setText_2(&self->m_status, "起始时间晚于结束时间，未筛选");
        return;
    }
    self->m_loTs = loMs;
    self->m_hiTs = hiMs;
    text = XLineEdit_text(&self->m_kw);
    snprintf(self->m_kwText, sizeof(self->m_kwText), "%s", text ? text : "");
    self->m_anchorTs = 0;   /* 新查询：重取"现在"作锚。 */
    logRequest(self, 0);
    XLabel_setText_2(&self->m_status, "筛选中…");
}

/** @brief 清除筛选：清关键字 + 时间窗不设界，回第 1 页。
 *  @note  类别下拉保持用户当前选择（类别是"看哪一类"的视图设置，
 *         与历史页"列选择不随清除筛选复位"同一口径）。 */
static void logClearFilter(HsfDataLogPage* self)
{
    XDateTime now;
    if (!self) return;
    self->m_loTs = 0;
    self->m_hiTs = 0;
    self->m_kwText[0] = '\0';
    XLineEdit_setText(&self->m_kw, "");
    if (self->m_bar) {
        now = XDateTime_currentDateTime();
        HsfDataBar_setEndDateTime(self->m_bar, &now);
    }
    self->m_anchorTs = 0;
    logRequest(self, 0);
    XLabel_setText_2(&self->m_status, "已清除筛选（时间/关键字；类别保持）");
}

/** @brief 关键字输入框回车 = 应用筛选（沿用当前时间窗/类别）。 */
static void logKwReturnSlot(XObject* receiver, XVarList* args)
{
    HsfDataLogPage* self = (HsfDataLogPage*)((char*)receiver -
                                            offsetof(HsfDataLogPage, m_kw));
    (void)args;
    logApplyFilter(self);
}

/** @brief 改每页条数：回第 1 页（行号口径变了，停在原页号没意义）。 */
static void logPageSizeSlot(XObject* receiver, XVarList* args)
{
    HsfDataLogPage* self = (HsfDataLogPage*)((char*)receiver -
                                            offsetof(HsfDataLogPage, m_pageSizeBox));
    int idx;
    (void)args;
    if (!self) return;
    idx = XComboBox_currentIndex(&self->m_pageSizeBox);
    if (idx < 0 || idx >= 3) return;
    if (kLogPageSizes[idx] == self->m_pageSize) return;
    self->m_pageSize = kLogPageSizes[idx];
    self->m_anchorTs = 0;
    logRequest(self, 0);
}

/* ==================== 全量导出（文件对话框 + 逐批拉全） ====================
 * [口径] 与历史页同款：导出 = **按当前筛选条件的全部日志行**（同一组
 * WHERE 连续分页拉全），不是表格当前页那几行。格式由文件对话框的过滤
 * 器档位决定（"Excel 工作簿 (*.xlsx);;CSV (*.csv)"）；行走共享模块的
 * 流式写出器来一批写一批；锚点/时间窗/类别/关键字在发起那一刻定格。
 * [权限闸] 0x0119 无 export 形态（服务端按读面放行），**下载权限闸在
 * 客户端这里**：无 HSF_PERM_DOWNLOAD 直接拒（与历史页同一款客户端闸）；
 * 0x0119 只读不涉密，权限面比 0x0118 宽是协议设计使然。 */

/** @brief 日志页导出取格上下文。 */
typedef struct LogExportCtx {
    const HsfLinkLogPage* page; /**< 服务端返回的那一页。 */
} LogExportCtx;

/** @brief 导出取格回调：时间（格式化）/ 类别 / 事件 三列。 */
static bool logExportCell(void* user, int row, int col, char* buf, int cap)
{
    const LogExportCtx* ctx = (const LogExportCtx*)user;
    if (buf && cap > 0) buf[0] = '\0';
    if (!ctx || !ctx->page || !buf || cap <= 0) return false;
    if (row < 0 || row >= ctx->page->m_count) return false;
    switch (col) {
    case 0:
        logFormatTs(ctx->page->m_rows[row].m_ts, buf, cap);
        return true;
    case 1:
        snprintf(buf, (size_t)cap, "%s", ctx->page->m_rows[row].m_category);
        return true;
    case 2:
        snprintf(buf, (size_t)cap, "%s", ctx->page->m_rows[row].m_text);
        return true;
    default:
        return false;
    }
}

/** @brief 按当前导出状态设置应答轮询周期（导出期间提速，空闲恢复）。 */
static void logTimerIntervalApply(HsfDataLogPage* self)
{
    if (!self || !self->m_timer) return;
    XTimer_setInterval(self->m_timer,
                       self->m_exportFetching ? LOG_EXPORT_POLL_MS
                                              : HsfSettingsLocal_refreshMs());
}

/** @brief 收尾导出（写完 or 中途放弃）：落盘/清理 + 状态行 + 周期重算。
 *  @param complete true=按计划写完（finish：XLSX saveAs/CSV 关闭）；
 *                  false=中途放弃（abort：删半成品 CSV/丢弃 XLSX 文档）。
 *  @param failNote 失败文案（complete=false 时用；NULL=保留状态行现文）。 */
static void logExportDone(HsfDataLogPage* self, bool complete,
                          const char* failNote)
{
    char buf[300];
    bool ok = false;
    if (!self) return;
    if (complete) ok = HsfExportWriter_finish(&self->m_exportWriter);
    else HsfExportWriter_abort(&self->m_exportWriter);
    self->m_exportFetching = false;
    self->m_exportIdle = 0;
    self->m_exportRetries = 0;
    logTimerIntervalApply(self);
    if (!complete) {
        if (failNote) XLabel_setText_2(&self->m_status, failNote);
        return;
    }
    if (!ok) {
        XLabel_setText_2(&self->m_status, "导出失败：文件写入错误");
        return;
    }
    snprintf(buf, sizeof(buf), "已导出 %d 行 → %s%s",
             self->m_exportWritten,
             HsfExportWriter_path_const(&self->m_exportWriter),
             self->m_exportTruncated ? "（超出 Excel 上限，已截断）" : "");
    XLabel_setText_2(&self->m_status, buf);
}

/** @brief 发一批导出请求（按**定格**的锚点/时间窗/类别/关键字）。
 *  @return 请求已发出返回 true；发出失败由调用方收尾。 */
static bool logExportRequest(HsfDataLogPage* self, int offset)
{
    if (!self) return false;
    if (!HsfLinkClient_logQueryWindow(offset, LOG_EXPORT_CHUNK,
                                      self->m_exportAnchor,
                                      self->m_exportLoTs, self->m_exportHiTs,
                                      self->m_exportCategory[0]
                                          ? self->m_exportCategory : NULL,
                                      self->m_exportKw[0]
                                          ? self->m_exportKw : NULL))
        return false;
    /* [应答归属] 导出批 limit 恒 LOG_EXPORT_CHUNK（40），与页大小
     * 10/20/50 不相交（编译期锁见 LOG_EXPORT_CHUNK），一条应答只会落进
     * 一个分支。 */
    self->m_exportOffset = offset;
    self->m_exportLimit = LOG_EXPORT_CHUNK;
    self->m_exportIdle = 0;
    return true;
}

/** @brief 导出批应答到达：写一批 → 推进下一批 → 取完收尾（连续取，不跳批）。 */
static void logExportContinue(HsfDataLogPage* self,
                              const HsfLinkLogPage* page)
{
    LogExportCtx ctx;
    char prog[160];
    int got;
    int i;
    bool done;
    if (!self || !page || !page->m_valid) {
        logExportDone(self, false, "导出失败：查询失败");
        return;
    }
    if (!page->m_ok) {
        /* [否定应答] 服务端拒绝（未开库等）：保留原文收尾。 */
        char ebuf[160];
        snprintf(ebuf, sizeof(ebuf), "导出失败：%s",
                 page->m_err[0] ? page->m_err : "服务端拒绝");
        logExportDone(self, false, ebuf);
        return;
    }
    self->m_exportIdle = 0;
    got = page->m_count;
    if (got > 0) {
        ctx.page = page;
        for (i = 0; i < got; ++i) {
            /* [XLSX 内存闸] 同历史页：CSV 全量到底，XLSX 触上限截断。 */
            if (self->m_exportFormat == HsfExportFormat_Xlsx &&
                self->m_exportWritten >= LOG_EXPORT_XLSX_MAX_ROWS) {
                self->m_exportTruncated = true;
                break;
            }
            if (!HsfExportWriter_writeRow(&self->m_exportWriter, i,
                                          logExportCell, &ctx)) {
                logExportDone(self, false, "导出失败：文件写入错误");
                return;
            }
            ++self->m_exportWritten;
        }
    }
    if (self->m_exportTotal <= 0 && page->m_total > 0)
        self->m_exportTotal = page->m_total;
    done = self->m_exportTruncated || got <= 0 ||
           got < self->m_exportLimit ||
           (self->m_exportTotal > 0 &&
            self->m_exportWritten >= self->m_exportTotal);
    if (done) {
        logExportDone(self, true, NULL);
        return;
    }
    snprintf(prog, sizeof(prog), "正在导出… 已写 %d 行 / 共 %d 行",
             self->m_exportWritten, self->m_exportTotal);
    XLabel_setText_2(&self->m_status, prog);
    if (!logExportRequest(self, self->m_exportOffset + got))
        logExportDone(self, false, "导出失败：服务端未连接");
}

/** @brief 发起导出（工具条「导出」）：无权限→状态行拒绝；有权限→弹
 *  文件对话框（格式=过滤器档位）→ 按当前筛选条件全量逐批拉取落盘。 */
static void logExportStart(HsfDataLogPage* self)
{
    XString* chosen = NULL;
    int filterIdx = 0;
    char defName[96];
    XDateTime now;
    XDate d;
    XTime t;
    if (!self) return;
    if (!HsfLinkClient_can(HSF_PERM_DOWNLOAD)) {
        XLabel_setText_2(&self->m_status,
                         "权限不足：下载数据需要普通用户及以上账户");
        return;
    }
    if (self->m_exportFetching) {
        XLabel_setText_2(&self->m_status, "正在导出中，请等本轮完成");
        return;
    }
    /* [格式=对话框过滤器档位] 框架文件对话框；过滤器串与历史页/Python
     * 客户端同串同序：0=Excel 工作簿、1=CSV。 */
    now = XDateTime_currentDateTime();
    d = XDateTime_date(&now);
    t = XDateTime_time(&now);
    snprintf(defName, sizeof(defName),
             "./hsf_log_%04d%02d%02d_%02d%02d%02d",
             XDate_year(&d), XDate_month(&d), XDate_day(&d),
             XTime_hour(&t), XTime_minute(&t), XTime_second(&t));
    chosen = XFileDialog_getSaveFileName_2(
        (XWidget*)&self->m_base, "导出运行日志", defName,
        "Excel 工作簿 (*.xlsx);;CSV (*.csv)", &filterIdx);
    if (chosen) {
        const char* u8 = XString_toUtf8(chosen);
        snprintf(self->m_exportPath, sizeof(self->m_exportPath), "%s",
                 u8 ? u8 : "");
        XClassDelete(chosen);
    } else {
        self->m_exportPath[0] = '\0';
    }
    if (!self->m_exportPath[0]) return;   /* 用户取消：静默收场 */
    HsfExport_stripKnownExt(self->m_exportPath);
    self->m_exportFormat = HsfExport_formatFromFilterIndex(filterIdx);
    /* [定格] 锚点 + 时间窗 + 类别 + 关键字快照（后续批次全按快照下发）。
     * logs 表持续入库：锚点不定格，相邻两批会被新行顶偏（漏行/重行）。 */
    self->m_exportAnchor = (self->m_anchorTs > 0)
        ? self->m_anchorTs
        : XDateTime_currentWallMSecsSinceEpoch();
    self->m_exportLoTs = self->m_loTs;
    self->m_exportHiTs = self->m_hiTs;
    snprintf(self->m_exportCategory, sizeof(self->m_exportCategory), "%s",
             self->m_category);
    snprintf(self->m_exportKw, sizeof(self->m_exportKw), "%s", self->m_kwText);
    /* 打开写出器（BOM/建文档 + 表头）。 */
    if (!HsfExportWriter_open(&self->m_exportWriter, self->m_exportFormat,
                              self->m_exportPath, "运行日志",
                              kLogHeaders, 3)) {
        XLabel_setText_2(&self->m_status, "导出失败：文件无法创建");
        return;
    }
    self->m_exportFetching = true;
    self->m_exportTotal = 0;
    self->m_exportWritten = 0;
    self->m_exportTruncated = false;
    self->m_exportIdle = 0;
    self->m_exportRetries = 0;
    XLabel_setText_2(&self->m_status, "正在导出…");
    logTimerIntervalApply(self);   /* 取数期间提速轮询（一批一跳） */
    if (!logExportRequest(self, 0))
        logExportDone(self, false, "导出失败：服务端未连接");
}

/* ==================== 工具条 / 轮询 ==================== */

/** @brief 工具条动作落地（本页自己那套的接线口）。 */
static void logBarAction(void* userData, int action)
{
    HsfDataLogPage* self = (HsfDataLogPage*)userData;
    int idx;
    if (!self) return;
    switch (action) {
    case HsfDataBarAction_Filter:
        logApplyFilter(self);
        break;
    case HsfDataBarAction_ClearFilter:
        logClearFilter(self);
        break;
    case HsfDataBarAction_Export:
        logExportStart(self);
        break;
    case HsfDataBarAction_Reload:
        self->m_anchorTs = 0;
        logRequest(self, self->m_offset);
        XLabel_setText_2(&self->m_status, "重新加载…");
        break;
    case HsfDataBarAction_ChannelChanged:
        /* 类别下拉（挂在工具条通道口）：0=全部，1..6=conn/ctrl/alarm/auto/
         * auth/sys（sys 见 LOG_CATEGORY_COUNT 的说明）。 */
        idx = self->m_bar ? HsfDataBar_channelIndex(self->m_bar) : 0;
        if (idx < 0 || idx > LOG_CATEGORY_COUNT) idx = 0;
        snprintf(self->m_category, sizeof(self->m_category), "%s",
                 kLogCategoryKeys[idx]);
        self->m_anchorTs = 0;
        logRequest(self, 0);
        break;
    case HsfDataBarAction_ViewChart:
    case HsfDataBarAction_GenerateChart:
        /* [日志页无图表形态] 时间/类别/事件是纯文本流，画折线无意义；
         * 「图表」钮被按下时把工具条选中态弹回表格（防"图表钮选中但
         * 界面没变"的假态），不发请求。 */
        if (self->m_bar) HsfDataBar_setViewMode(self->m_bar, 0);
        break;
    case HsfDataBarAction_ViewTable:
        /* 已是表格视图：无需动作（工具条按钮态已由其内部同步）。 */
        break;
    default:
        /* ColumnFilter/Decimals/MaxPoints：本页未启用对应控件，不会到达。 */
        break;
    }
}

/** @brief 界面刷新周期拍：应答到达 → 按**归属**分派（表格页 / 导出批）。
 *  @details 与 histTimerCb 同机制：应答槽只有一份（HsfLinkClient_logPage），
 *           按服务端回显的 offset/limit 认领。本页两类消费者的 limit 恒
 *           不相交（页=10/20/50、导出批=40）⇒ 一条应答只落进一个分支。
 *           [导出批看门狗] 在途导出批的应答可能被同拍到达的页应答顶出
 *           单份缓冲（用户在导出期间翻了页）：约 1s 没等到就按同一键
 *           重发，连续 3 次不回才放弃。 */
static void logTimerCb(void* userData, XTimerData* timer)
{
    HsfDataLogPage* self = (HsfDataLogPage*)userData;
    const HsfLinkLogPage* page;
    bool isPageReply;
    bool isExportReply;
    (void)timer;
    if (!self) return;
    /* [导出批看门狗] 同历史页 histTimerCb 的兜底（被顶掉 ≠ 丢终局）。 */
    if (self->m_exportFetching) {
        ++self->m_exportIdle;
        if (self->m_exportIdle > LOG_EXPORT_WATCHDOG_TICKS) {
            self->m_exportIdle = 0;
            if (++self->m_exportRetries > LOG_EXPORT_MAX_RETRIES) {
                logExportDone(self, false, "导出失败：服务端应答超时");
            } else if (!logExportRequest(self, self->m_exportOffset)) {
                logExportDone(self, false, "导出失败：服务端未连接");
            }
        }
    }
    page = HsfLinkClient_logPage();
    if (!page || !page->m_valid) return;
    /* [同一应答只处置一次] 序号每拍比对，重复读到同一条就返回。 */
    if (page->m_seq == self->m_seenSeq) return;

    isPageReply = self->m_pageReqValid &&
                  page->m_offset == self->m_pageReqOffset &&
                  page->m_limit == self->m_pageReqLimit;
    isExportReply = self->m_exportFetching &&
                    page->m_offset == self->m_exportOffset &&
                    page->m_limit == self->m_exportLimit;

    if (isExportReply) {
        self->m_seenSeq = page->m_seq;
        logExportContinue(self, page);
        return;
    }
    if (!isPageReply) {
        /* [无主应答 = 丢弃] 被取消/被取代的请求晚到的应答绝不铺表。 */
        self->m_seenSeq = page->m_seq;
        XPrintf("HsfDataLog: drop unattributed reply "
                "(offset=%d, limit=%d, seq=%u)\n",
                page->m_offset, page->m_limit, (unsigned)page->m_seq);
        return;
    }
    self->m_seenSeq = page->m_seq;
    self->m_pageReqValid = false;
    logFillTable(self, page);
}

/* ==================== 字号 / 销毁 ==================== */

static void logApplyFonts(XWidget* page, int pct);

/** @brief 页销毁：停并释放应答轮询定时器（独立堆对象、无父对象；不停
 *  就会在页面释放后继续回调 → UAF。历史页 histPageDestroyedSlot 同款）。 */
static void logPageDestroyedSlot(XObject* sender, XVarList* args)
{
    HsfDataLogPage* self = (HsfDataLogPage*)sender;
    (void)args;
    if (!self) return;
    if (self->m_timer) {
        XTimer_stop_base(self->m_timer);
        XClassDelete(self->m_timer);
        self->m_timer = NULL;
    }
    /* [导出在途收尾] 关掉 CSV 句柄/丢弃 XLSX 文档（半成品 CSV 尽力删除），
     * 别把句柄与行缓冲随页一起带走（泄漏）。 */
    if (self->m_exportFetching)
        HsfExportWriter_abort(&self->m_exportWriter);
}

/** @brief 字号实时重放：标题/状态/表格行高/工具条/行 2 控件同一比例。 */
static void logApplyFonts(XWidget* page, int pct)
{
    HsfDataLogPage* self = (HsfDataLogPage*)page;
    if (!self || pct <= 0) return;
    XLabel_setTextPixelSize(&self->m_title, logPx(16));
    XLabel_setTextPixelSize(&self->m_status, logPx(12));
    /* 行高随字号同步（仅当前页已填的行）。 */
    if (self->m_rowBaseH > 0) {
        const int rh = (self->m_rowBaseH * pct + 50) / 100;
        int r;
        for (r = 0; r < self->m_shownCount; ++r)
            XTableView_setRowHeight(&self->m_table.m_base, r, rh);
    }
    if (self->m_bar) HsfDataBar_applyFont(self->m_bar, pct);
    /* [行 2 页级控件随字号] 与历史页同款：固定高跟工具条同一比例重放。 */
    {
        XWidget* row2[4];
        int i;
        const int h = 28 * pct / 100;
        row2[0] = (XWidget*)&self->m_prev;
        row2[1] = (XWidget*)&self->m_next;
        row2[2] = (XWidget*)&self->m_pageSizeBox;
        row2[3] = (XWidget*)&self->m_kw;
        for (i = 0; i < 4; ++i) XWidget_setFixedHeight(row2[i], h);
    }
    {
        static const int kW[3] = { 185, 70, 560 };
        int c;
        for (c = 0; c < 3; ++c)
            XTableView_setColumnWidth(&self->m_table.m_base, c,
                                      kW[c] * pct / 100);
    }
}

/* ==================== 建页 ==================== */

XWidget* HsfDataLog_create(XWidget* parent, HsfMenuNavFn navFn,
                           void* userData)
{
    HsfDataLogPage* self;
    (void)navFn; (void)userData;
    if (!parent) return NULL;
    self = (HsfDataLogPage*)XMemory_malloc(sizeof(HsfDataLogPage),
                                           XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true);
    HsfTheme_applyDefaultFont(&self->m_base);

    self->m_tzOffsetMs = logTzOffsetMs();

    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 16, 12, 16, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    XLabel_setText_2(&self->m_title, "日志");
    XLabel_setTextPixelSize(&self->m_title, logPx(16));
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    /* 工具条（本页自己一份实例；历史/报警页同款）。类别下拉挂通道口。 */
    self->m_bar = HsfDataBar_create(&self->m_base);
    if (self->m_bar) {
        XDateTime now;
        HsfDataBar_setChannelItems(self->m_bar, kLogCategoryNames,
                                   LOG_CATEGORY_COUNT + 1);
        HsfDataBar_setChannelIndex(self->m_bar, 0);
        now = XDateTime_currentDateTime();
        HsfDataBar_setEndDateTime(self->m_bar, &now);
        HsfDataBar_setActionCallback(self->m_bar, logBarAction, self);
        /* [按页裁剪工具条控件 2026-11-10] 本页**没有图表形态**，也没有
         * 列筛选/小数位/最大点数的处理分支 ⇒ 共享工具条上这几个控件是
         * 「可见但改了没反应」的假控件；「生成折线图」按下也只会自己弹回
         * 表格（假按钮）。Python 端日志页**根本没有**这些控件。故一律
         * 藏掉，别让假控件占着用户的注意力。
         * [导出格式下拉也删 2026-11-10] 导出格式改由文件对话框的过滤器
         * 档位决定（logExportStart），下拉没有存在意义——一并藏掉。 */
        HsfDataBar_setFeatures(self->m_bar,
            HsfDataBarFeature_All
            & ~(unsigned int)(HsfDataBarFeature_Chart
                              | HsfDataBarFeature_ColFilter
                              | HsfDataBarFeature_Decimals
                              | HsfDataBarFeature_MaxPoints
                              | HsfDataBarFeature_ExportFmt));
        XBoxLayout_addWidget(&self->m_layout, self->m_bar);
    }

    XLabel_init(&self->m_status, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_status);
    XLabel_setTextPixelSize(&self->m_status, logPx(12));
    HsfTheme_applySecondaryText((XWidget*)&self->m_status);
    XLabel_setText_2(&self->m_status, "读取中");
    XWidget_show((XWidget*)&self->m_status);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_status);

    XTableWidget_init(&self->m_table, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_table);
    XTableWidget_setColumnCount(&self->m_table, 3);
    XTableWidget_setHorizontalHeaderLabels(&self->m_table, kLogHeaders, 3);
    XTableWidget_setRowCount(&self->m_table, 0);
    self->m_rowBaseH = XTableView_rowHeight(&self->m_table.m_base, 0);
    {
        XHeaderView* hh = XTableView_horizontalHeader(&self->m_table.m_base);
        if (hh)
            XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Fixed);
        /* [时间列较宽] "2026-11-09 17:56:23" 需 19 字符（历史页同款 185）。 */
        XTableView_setColumnWidth(&self->m_table.m_base, 0, 185);
        XTableView_setColumnWidth(&self->m_table.m_base, 1, 70);
        XTableView_setColumnWidth(&self->m_table.m_base, 2, 560);
        /* [观感对齐历史页] 无行号列、无网格线。 */
        XTableWidget_setVerticalHeaderVisible(&self->m_table, false);
        XTableView_setShowGrid(&self->m_table.m_base, false);    {
        XHeaderView* hh = XTableView_horizontalHeader(&self->m_table.m_base);
        HsfTableSort_init(&self->m_sort);
        if (hh) {
            /* [单击口径 2026-11-11] 同历史页：框架表头 clickable 缺省 true
             * （Qt qtableview.cpp:577-589），本页排序为双击三态 ⇒ 关掉单击
             * 可点，避免单击翻转排序指示器（双击发射不受该位门控）。 */
            XHeaderView_setSectionsClickable(hh, false);
            /* [双击列标题排序] 与 Python 客户端同口径；作用于当前页。 */
            XObject_connect_1((XObject*)hh,
                              XSignal(XHeaderView_sectionDoubleClicked_signal),
                              (XObject*)self, HsfDataLogPageHeaderDblClicked,
                              XConnectionType_Direct);
        }
    }

    }
    XWidget_show((XWidget*)&self->m_table);
    XBoxLayout_addWidgetEx(&self->m_layout, (XWidget*)&self->m_table, 1, 0);

    /* 分页/关键字控件：以页根为父，布局位置挂进工具条行 2
     * （attachRow2 会 reparent 到工具条，历史页同款注记）。 */
    XPushButton_init(&self->m_prev, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_prev);
    XPushButton_setText_2(&self->m_prev, "上一页");
    XWidget_setFixedHeight((XWidget*)&self->m_prev, 28);
    XObject_connect_1((XObject*)&self->m_prev,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&self->m_prev, logPrevSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_prev);

    XPushButton_init(&self->m_next, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_next);
    XPushButton_setText_2(&self->m_next, "下一页");
    XWidget_setFixedHeight((XWidget*)&self->m_next, 28);
    XObject_connect_1((XObject*)&self->m_next,
                      XSignal(XAbstractButton_clicked_signal),
                      (XObject*)&self->m_next, logNextSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_next);

    XComboBox_init(&self->m_pageSizeBox, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_pageSizeBox);
    XComboBox_addItem_2(&self->m_pageSizeBox, "10 条");
    XComboBox_addItem_2(&self->m_pageSizeBox, "20 条");
    XComboBox_addItem_2(&self->m_pageSizeBox, "50 条");
    XComboBox_setCurrentIndex(&self->m_pageSizeBox, 1);
    XWidget_setFixedHeight((XWidget*)&self->m_pageSizeBox, 28);
    XWidget_setMinimumWidth((XWidget*)&self->m_pageSizeBox, 92);
    XObject_connect_1((XObject*)&self->m_pageSizeBox,
                      XSignal(XComboBox_currentIndexChanged_signal),
                      (XObject*)&self->m_pageSizeBox, logPageSizeSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_pageSizeBox);

    XLineEdit_init(&self->m_kw, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_kw);
    XLineEdit_setPlaceholderText(&self->m_kw, "关键字（事件/类别）");
    XLineEdit_setClearButtonEnabled(&self->m_kw, true);
    XWidget_setFixedHeight((XWidget*)&self->m_kw, 28);
    XWidget_setMinimumWidth((XWidget*)&self->m_kw, 160);
    XObject_connect_1((XObject*)&self->m_kw,
                      XSignal(XLineEdit_returnPressed_signal),
                      (XObject*)&self->m_kw, logKwReturnSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_kw);

    if (self->m_bar) {
        XWidget* row2[4];
        row2[0] = (XWidget*)&self->m_prev;
        row2[1] = (XWidget*)&self->m_next;
        row2[2] = (XWidget*)&self->m_pageSizeBox;
        row2[3] = (XWidget*)&self->m_kw;
        HsfDataBar_attachRow2(self->m_bar, row2, 4, NULL, 0);
    }

    self->m_pageSize = LOG_PAGE_LIMIT;
    self->m_kwText[0] = '\0';
    self->m_category[0] = '\0';

    /* 应答轮询（界面刷新周期）+ 首屏请求。 */
    self->m_timer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_timer) {
        XTimer_setInterval(self->m_timer, HsfSettingsLocal_refreshMs());
        XTimer_setTimerCallback(self->m_timer, logTimerCb);
        XTimer_setUserData(self->m_timer, self);
        XTimer_start_base(self->m_timer);
    }
    logRequest(self, 0);

    hsfUi_registerPageFontApplier(HSF_PAGE_DATA_LOG, &self->m_base,
                                  logApplyFonts);
    /* [页销毁收尾] 停并释放应答轮询定时器（destroyed 在 VXObject_deinit
     * 早期发出，字段仍有效；connect_2 无 receiver，self 作上下文闭包）。 */
    XObject_connect_2((XObject*)&self->m_base,
                      XSignal(XObject_destroyed_signal),
                      logPageDestroyedSlot);
    XWidget_show(&self->m_base);
    return &self->m_base;
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON && XLABEL_ON && XPUSHBUTTON_ON && XLAYOUT_ON */
