/**
 * @file       HsfDataHistory.c
 * @brief      数据-历史数据页：服务端历史分页（0x0118）→ 表格 + 翻页。
 * @details    [数据源] 服务端 SQLite 落盘（history 表），经 0x0118 分页查询
 *             拉取；与 Python 客户端同接口同键名（offset/limit/beforeTs →
 *             total/cols/rows/…）。
 *             [显示口径] 服务端只发原始值，显示格式化在
 *             `HsfLinkHistory.c`（与服务端口径一致），本页只负责摆表 +
 *             翻页 + 状态行。
 *             [轮询] 应答是异步的（请求-应答），故页面用 200ms 定时器看
 *             结果有没有变（offset/行数）→ 有变就重填表格（不整页重建，
 *             避免闪）。
 */
#include "HsfDataHistory.h"

#include "HsfSettingsLocal.h" /* 轮询周期=本地设置「界面刷新周期」 */

#include "HsfLinkClient.h"
#include "HsfLinkHistory.h"
#include "HsfTheme.h"
#include "XBoxLayout.h"
#include "XHeaderView.h"
#include "XLabel.h"
#include "XPushButton.h"
#include "XTableWidget.h"
#include "XTimer.h"
#include "XMemory.h"
#include "XPrintf.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#if XWIDGET_ON && XTABLEWIDGET_ON && XLABEL_ON && XPUSHBUTTON_ON && XLAYOUT_ON

/** @brief 每页行数（协议上限 50）。 */
#define HIST_PAGE_LIMIT   20

/** @brief 历史数据列头（与 Python 客户端 COLUMNS 同序）。 */
static const char* const kHistoryHeaders[15] = {
    "\xE6\x97\xB6\xE9\x97\xB4",                          /* 时间 */
    "\xE6\xA8\xA1\xE5\xBC\x8F",                          /* 模式 */
    "\xE6\x89\xB8\xE6\x8D\x8C\xE7\x8A\xB6\xE6\x80\x81",    /* 搅拌状态 */
    "\xE8\xBE\x93\xE5\x87\xBA\xE9\xA2\x91\xE7\x8E\x87(Hz)", /* 输出频率 */
    "\xE8\xBE\x93\xE5\x87\xBA\xE7\x94\xB5\xE6\xB5\x81(A)",  /* 输出电流 */
    "\xE6\x89\xB8\xE6\x8D\x8C\xE8\xBD\xAC\xE9\x80\x9F(rpm)", /* 搅拌转速 */
    "\xE6\xB7\xA1\xE5\xBA\xA6" "1(NTU)",                /* 浊度1 */
    "\xE6\xB7\xA1\xE5\xBA\xA6" "2(NTU)",                /* 浊度2 */
    "\xE7\x9E\xAC\xE6\x97\xB6\xE6\xB5\x81\xE9\x87\x8F",      /* 瞬时流量 */
    "\xE6\xAD\xA3\xE7\xB4\xAF\xE8\xAE\xA1",                /* 正累计 */
    "\xE5\x8E\x8B\xE5\xB7\xAE(MPa)",                        /* 压差 */
    "\xE7\x94\xB5\xE7\xAE\xB1\xE6\xB8\xA9\xE5\xBA\xA6(\xE2\x84\x83)", /* 电箱温度 */
    "\xE7\x94\xB5\xE7\xAE\xB1\xE6\xB9\xBF\xE5\xBA\xA6(%RH)",  /* 电箱湿度 */
    "DI(32\xE4\xB8\x8D)",                                 /* DI(32位) */
    "DO(32\xE4\xB8\x8D)"                                  /* DO(32位) */
};

/** @brief 历史数据页对象。 */
typedef struct HsfDataHistory {
    XWidget      m_base;        /**< 页根（第一成员）。 */
    XBoxLayout   m_layout;      /**< 页根竖排布局。 */
    XLabel       m_title;       /**< 页标题。 */
    XLabel       m_status;      /**< 状态行（共 N 条 · 第 x/y 页）。 */
    XTableWidget m_table;       /**< 数据表。 */
    XPushButton  m_prev;        /**< 上一页。 */
    XPushButton  m_next;        /**< 下一页。 */
    XTimer*      m_timer;       /**< 应答轮询定时器（200ms）。 */
    int          m_offset;      /**< 当前页起始偏移。 */
    int          m_shownOffset; /**< 已填表的偏移（变了就重填）。 */
    int          m_shownCount;  /**< 已填表的行数。 */
} HsfDataHistory;

/* ==================== 填表 ==================== */

/** @brief 用最近一次历史结果重填表格 + 状态行。 */
static void histFillTable(HsfDataHistory* self)
{
    const HsfHistoryPage* page = HsfLinkClient_history();
    int i;
    int c;

    if (!self || !page || !page->m_valid) return;
    XTableWidget_setRowCount(&self->m_table, page->m_count);
    for (i = 0; i < page->m_count; ++i) {
        for (c = 0; c < 15; ++c) {
            XTableWidgetItem* it =
                XTableWidgetItem_create_utf8(page->m_rows[i].m_cells[c], 0);
            if (!it) continue;
            XTableWidget_setItem(&self->m_table, i, c, it);
            /* [异常行高亮] 采样那刻存在异常（故障/浊度越限/压差越限）——
             * 与 Python 客户端同口径：整行浅红底，现场一眼扫出异常段。 */
            if (page->m_rows[i].m_abnormal) {
                XColor bg;
                XColor_init_rgb(&bg, 0xFD, 0xE7, 0xE9, 0xFF);
                XTableWidgetItem_setBackground(it, &bg);
            }
        }
    }
    self->m_shownOffset = page->m_offset;
    self->m_shownCount = page->m_count;
    {
        const int limit = (page->m_limit > 0) ? page->m_limit : HIST_PAGE_LIMIT;
        const int cur = (limit > 0) ? (page->m_offset / limit + 1) : 1;
        const int pages = (limit > 0) ? ((page->m_total + limit - 1) / limit) : 1;
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "\xE5\x85\xB1 %d \xE6\x9D\xA1 \xC2\xB7 \xE7\xAC\xAC %d/%d \xE9\xA1\xB5%s",
                 page->m_total, cur, pages,
                 page->m_count ? "" :
                 "\xEF\xBC\x88\xE6\x97\xA0\xE6\x95\xB0\xE6\x8D\xAE\xEF\xBC\x89");
        XLabel_setText_2(&self->m_status, buf);
    }
    XWidget_setEnabled((XWidget*)&self->m_prev, self->m_shownOffset > 0);
    XWidget_setEnabled((XWidget*)&self->m_next,
                       self->m_shownOffset + self->m_shownCount < page->m_total);
    XWidget_update((XWidget*)&self->m_table);
}

/* ==================== 翻页 ==================== */

/** @brief 发起一页请求（offset 为该页起点）。 */
static void histRequest(HsfDataHistory* self, int offset)
{
    if (!self) return;
    if (offset < 0) offset = 0;
    self->m_offset = offset;
    (void)HsfLinkClient_historyPage(offset, HIST_PAGE_LIMIT);
}

/** @brief 上一页。 */
static void histPrevSlot(XObject* receiver, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)((char*)receiver -
                                            offsetof(HsfDataHistory, m_prev));
    (void)args;
    if (!self) return;
    if (self->m_offset <= 0) return;
    histRequest(self, self->m_offset - HIST_PAGE_LIMIT);
}

/** @brief 下一页。 */
static void histNextSlot(XObject* receiver, XVarList* args)
{
    HsfDataHistory* self = (HsfDataHistory*)((char*)receiver -
                                            offsetof(HsfDataHistory, m_next));
    const HsfHistoryPage* page;
    (void)args;
    if (!self) return;
    page = HsfLinkClient_history();
    if (!page || !page->m_valid) return;
    if (self->m_offset + page->m_count >= page->m_total) return;
    histRequest(self, self->m_offset + HIST_PAGE_LIMIT);
}

/* ==================== 轮询 ==================== */

/** @brief 200ms 拍：应答到了就填表。 */
static void histTimerCb(void* userData, XTimerData* timer)
{
    HsfDataHistory* self = (HsfDataHistory*)userData;
    const HsfHistoryPage* page;
    (void)timer;
    if (!self) return;
    page = HsfLinkClient_history();
    if (!page || !page->m_valid) return;
    if (page->m_offset != self->m_shownOffset ||
        page->m_count != self->m_shownCount) {
        histFillTable(self);
    }
}

XWidget* HsfDataHistory_create(XWidget* parent, HsfMenuNavFn navFn,
                               void* userData)
{
    HsfDataHistory* self;
    (void)navFn; (void)userData;

    if (!parent) return NULL;
    self = (HsfDataHistory*)XMemory_malloc(sizeof(HsfDataHistory),
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
                     "\xE5\x8E\x86\xE5\x8F\xB2\xE6\x95\xB0\xE6\x8D\xAE"); /* 历史数据 */
    XLabel_setTextPixelSize(&self->m_title, 16);
    XWidget_show((XWidget*)&self->m_title);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_title);

    XLabel_init(&self->m_status, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_status);
    XLabel_setTextPixelSize(&self->m_status, 12);
    XWidget_setForegroundRole((XWidget*)&self->m_status, XPaletteColorRole_Mid);
    XLabel_setText_2(&self->m_status,
                     "\xE8\xAF\xBB\xE5\x8F\x96\xE4\xB8\xAD"); /* 读取中 */
    XWidget_show((XWidget*)&self->m_status);
    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_status);

    XTableWidget_init(&self->m_table, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_table);
    XTableWidget_setColumnCount(&self->m_table, 15);
    XTableWidget_setHorizontalHeaderLabels(&self->m_table, kHistoryHeaders, 15);
    XTableWidget_setRowCount(&self->m_table, 0);
    {
        XHeaderView* hh = XTableView_horizontalHeader(&self->m_table.m_base);
        if (hh)
            XHeaderView_setSectionResizeMode(hh, XHeaderViewResizeMode_Fixed);
        /* [时间列较宽] "2026-11-06 17:56:23" 需 19 字符，150px 会截断
         * （显示成"2026-11-06 17:5…"）。 */
        XTableView_setColumnWidth(&self->m_table.m_base, 0, 185);
        XTableView_setColumnWidth(&self->m_table.m_base, 1, 80);
        XTableView_setColumnWidth(&self->m_table.m_base, 2, 90);
        XTableView_setColumnWidth(&self->m_table.m_base, 3, 110);
        XTableView_setColumnWidth(&self->m_table.m_base, 4, 110);
        XTableView_setColumnWidth(&self->m_table.m_base, 5, 120);
        XTableView_setColumnWidth(&self->m_table.m_base, 6, 100);
        XTableView_setColumnWidth(&self->m_table.m_base, 7, 100);
        XTableView_setColumnWidth(&self->m_table.m_base, 8, 110);
        XTableView_setColumnWidth(&self->m_table.m_base, 9, 110);
        XTableView_setColumnWidth(&self->m_table.m_base, 10, 100);
        XTableView_setColumnWidth(&self->m_table.m_base, 11, 110);
        XTableView_setColumnWidth(&self->m_table.m_base, 12, 110);
        XTableView_setColumnWidth(&self->m_table.m_base, 13, 110);
        XTableView_setColumnWidth(&self->m_table.m_base, 14, 110);
        /* [观感对齐 Python] 无行号列、无网格线。 */
        XTableWidget_setVerticalHeaderVisible(&self->m_table, false);
        XTableView_setShowGrid(&self->m_table.m_base, false);
    }
    XWidget_show((XWidget*)&self->m_table);
    XBoxLayout_addWidgetEx(&self->m_layout, (XWidget*)&self->m_table, 1, 0);

    {
        XHBoxLayout* bar = (XHBoxLayout*)XMemory_malloc(sizeof(XHBoxLayout),
                                                     XCLASS_DEFAULT_MEMORY_TYPE);
        if (bar) {
            XBoxLayout_init(bar, XBoxLayoutDirection_LeftToRight);
            XLayout_setSpacing((XLayout*)bar, 8);
            XPushButton_init(&self->m_prev, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_prev);
            XPushButton_setText_2(&self->m_prev,
                                 "\xE4\xB8\x8A\xE4\xB8\x80\xE9\xA1\xB5"); /* 上一页 */
            XWidget_setFixedHeight((XWidget*)&self->m_prev, 34);
            XObject_connect_1((XObject*)&self->m_prev,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)&self->m_prev, histPrevSlot,
                              XConnectionType_Direct);
            XWidget_show((XWidget*)&self->m_prev);
            XBoxLayout_addWidget(bar, (XWidget*)&self->m_prev);

            XPushButton_init(&self->m_next, &self->m_base, 0);
            HsfTheme_applyDefaultFont((XWidget*)&self->m_next);
            XPushButton_setText_2(&self->m_next,
                                 "\xE4\xB8\x8B\xE4\xB8\x80\xE9\xA1\xB5"); /* 下一页 */
            XWidget_setFixedHeight((XWidget*)&self->m_next, 34);
            XObject_connect_1((XObject*)&self->m_next,
                              XSignal(XAbstractButton_clicked_signal),
                              (XObject*)&self->m_next, histNextSlot,
                              XConnectionType_Direct);
            XWidget_show((XWidget*)&self->m_next);
            XBoxLayout_addWidget(bar, (XWidget*)&self->m_next);

            XBoxLayout_addStretch(bar, 1);
            XBoxLayout_addLayout(&self->m_layout, (XLayout*)bar);
        }
    }

    /* 应答轮询（200ms）+ 首屏请求。 */
    self->m_timer = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    if (self->m_timer) {
        /* 轮询周期取本地设置「界面刷新周期」（数据刷新卡；缺省 200ms）。 */
        XTimer_setInterval(self->m_timer, HsfSettingsLocal_refreshMs());
        XTimer_setTimerCallback(self->m_timer, histTimerCb);
        XTimer_setUserData(self->m_timer, self);
        XTimer_start_base(self->m_timer);
    }
    histRequest(self, 0);

    XWidget_show(&self->m_base);
    return &self->m_base;
}

void HsfDataHistory_adapt(XWidget* page, bool compact)
{
    HsfDataHistory* self = (HsfDataHistory*)page;
    if (!self) return;
    /* 竖屏/窄窗：页边距 16,12→8,6 腾内容宽（与本页 create 边距同源）。
     * 幂等（帧泵每循环调用）。本页是自建根结构（非 HsfDataTablePage），
     * 必须走自己的 adapt——历史上误挂 HsfDataTablePage_adapt 曾把本页
     * 强转成异型结构读 m_bar 偏移（垃圾指针）导致启动段错误。 */
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 8 : 16, compact ? 6 : 12,
                               compact ? 8 : 16, compact ? 6 : 12);
}

#endif /* XWIDGET_ON && XTABLEWIDGET_ON && XLABEL_ON && XPUSHBUTTON_ON && XLAYOUT_ON */
