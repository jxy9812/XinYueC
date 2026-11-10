/**
 * @file       HsfMonitorDigital.c
 * @brief      数字量监视/操作页实现：圆点网格看板 + 统一操作闸 + 全量 API。
 * @details    结构（对标 Qt 的 QWidget 子类习惯，框架零改动）：
 *             - HsfDigitalBoard：文件内自定义控件（XCLASS 机制继承
 *               XWidget），paintEvent 自绘「圆点 + 名称」网格（绿=通、
 *               红=断开），sizeHint 虚槽按当前宽度算行数给高度提示
 *               （滚动壳 widgetResizable 语义下：宽随视口、高出滚动），
 *               mousePressEvent 命中圆点且统一闸放行时经写回调出 UI；
 *               **局部刷新**：标脏按单元格矩形（XWidget_updateRect），
 *               paintEvent 消费 PAINT 矩形并跳过不相交的点格（见
 *               digital_cellRect 注；快照每拍重放 16 点名称曾把页面推到
 *               159.8fps，setName 同值早退 + 局部刷新后回 2fps）；
 *             - 数据模型（静态模块级 s_digital）：点数/状态/名称数组 +
 *               统一闸 + 写回调，全部经本头公开 API 读写（调用前页可
 *               未创建，安全空操作）；名称惰性拷贝、点数变化重建数组。
 *             页内装配：顶行（返回钮 + 标题 + 「允许界面操作」勾选框，
 *             勾选框即统一闸的界面开关）+ 看板 + 底部提示；与兄弟页
 *             同款 HsfTheme 字体装配；绘制颜色不引业务语义：绿/红仅
 *             表示 API 写入的 ON/OFF 态。
 *             兼容自测钩子：XHSF_DIGITAL_AUTOTEST=1 时灌 16 点演示
 *             数据（走公开 API，验证回显链路），供离屏截图闭环。
 */
#include "HsfMonitorDigital.h"

#include "HsfTheme.h"
#include "HsfPages.h"
#include "HsfUi.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XAbstractButton.h"
#include "XBoxLayout.h"
#include "XObject.h"
#include "XMemory.h"
#include "XWidget_Protected.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XPainter.h"
#include "XAlgorithm.h"
#include "XSystem.h"
#include "XTimer.h"
#include <string.h>
#include <stdio.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/* ---- 看板几何常量（逻辑像素） ---- */
#define DIGITAL_BOARD_MARGIN   16   /**< 看板内容边距。 */
#define DIGITAL_CELL_H_MIN     52   /**< 单元最小高（圆点行+名称行）。 */
#define DIGITAL_TOUCH_H_MIN    64   /**< 竖屏单元最小高（44dp 触控口径）。 */
#define DIGITAL_DOT_MAX        28   /**< 圆点最大直径。 */
#define DIGITAL_DOT_MIN        16   /**< 圆点最小直径。 */
#define DIGITAL_COL_MIN_W      132  /**< 横屏单列最小宽（**下限**；实际按最长名实测放宽）。 */
#define DIGITAL_COL_MIN_W_C    108  /**< 竖屏单列最小宽（窄屏多排一列）。 */
#define DIGITAL_NAME_PAD       16   /**< 名称两侧留白（实测名宽 + 本值 = 单列最小宽）。 */
#define DIGITAL_GAP            12   /**< 单元间横/纵间距。 */
#define DIGITAL_SECTION_GAP    18   /**< DI/DO 两区段之间的间距。 */
#define DIGITAL_NAME_MAX       64   /**< 名称缓冲上限（含 NUL）。 */

/* ---- 字号基准（本地设置 FontScale/Page3 的 100% 取值；逐项等于旧常量） ----
 * [2026-11-08] 看板是自绘控件：圆点名称的字号来自**看板自身字体**
 * （paintEvent 的 XWidget_font(self)），与标题/提示的 XLabel 字号是两套。
 * 旧代码只折算标题/提示/联动高，从未设置看板字体 ⇒ 名称恒定不缩放
 * （与模拟量页同源同问题）。基准值集中在此，100% 时与旧常量逐位相同。 */
#define DIGITAL_TITLE_PX       17   /**< 页标题基准字号（px）。 */
#define DIGITAL_HINT_PX        12   /**< 底部提示基准字号（px）。 */
#define DIGITAL_TITLE_ROW_H    26   /**< 标题行基准高（横屏）。 */
#define DIGITAL_TITLE_ROW_H_C  30   /**< 标题行基准高（竖屏触控）。 */
#define DIGITAL_BOARD_EXTRA_H  32   /**< 看板最小高余量（单元最小高之上）。 */
#define DIGITAL_BOARD_FONT_PX  16   /**< 看板基准字号（主题 12pt@96dpi=16px）。 */
#define DIGITAL_FONT_PCT_MIN   50   /**< 页字号下限（%，与 HsfUi 钳口同值）。 */
#define DIGITAL_FONT_PCT_MAX   200  /**< 页字号上限（%，与 HsfUi 钳口同值）。 */

/* ---- 看板颜色（ARGB；语义由 API 状态决定，非业务色） ---- */
#define DIGITAL_COLOR_ON       0xFF2E9E4Fu  /**< 通（绿）。 */
#define DIGITAL_COLOR_OFF      0xFFD93636u  /**< 断开（红）。 */
#define DIGITAL_COLOR_RING     0x40000000u  /**< 圆点描边（半透明黑）。 */
#define DIGITAL_COLOR_TEXT     0xE6202020u  /**< 名称文本。 */
#define DIGITAL_COLOR_HINT     0xFF8A8A8Au  /**< 空态/提示。 */

/* [随主题取套 2026-11-11 双主题走查] 看板自绘面/文字按当前主题取亮/深
 * 套（与流量计页 fmThemeClr 同款）：亮套=上表现行值零改动；深套=
 * HsfTheme.h 深色令牌——原整块白底在深色页面上成「墨青中央白块」
 * （走查实锅，五档白占比 0.96~1.00）。深套核算：看板底=窗底
 * #1B1F24（正文落底 13.74/次级 7.71）；圆点面=面板 #23282F；
 * 灯色 OK_DARK/FAULT_FILL_DARK（非文本件 vs 窗底 6.22/4.22 ≥3）、
 * 名称 TEXT_DARK、提示 SECONDARY_DARK。DI 空心圆的白底填充同取套
 * （深套=面板底，状态色环 vs 面板 4.30/3.20 ≥3）。 */
static uint32_t digitalThemeClr(uint32_t light, uint32_t dark)
{
    return HsfTheme_isDarkSkin() ? dark : light;
}

/* ==================== 看板控件（文件内自定义控件） ==================== */

/**
 * @brief      看板控件对象；m_base 必须是第一个成员。
 * @details    自绘网格：paintEvent 按当前宽算列数/行数 → 圆点+名称；
 *             sizeHint 虚槽按「查询时重算」给高度（宽随视口来自滚动壳
 *             widgetResizable，高度提示驱动竖滚）。
 */
XCLASS_DEFINE_BEGING(HsfDigitalBoard)
XCLASS_DEFINE_EXTEND_END(HsfDigitalBoard, XWidget)

typedef struct HsfDigitalBoard
{
    XWidget m_base;        /**< 基类成员；必须是第一个。 */
    int     m_pressedIndex; /**< 按压中的点位（-1=无；按压反馈用）。 */
#if XGUI_ON
    int     m_clickTestId;  /**< 点击自测定时器（XTIMER_INVALID_ID=未启用）。 */
    int     m_clickExpect;  /**< 期望被写回调记录的下标（-1=无）。 */
    int     m_clickHits;    /**< 写回调命中计数。 */
    int     m_clickStep;    /**< 自测步进（0=DO 圆心，1=DI 圆心，2=空白）。 */
#endif /* XGUI_ON */
} HsfDigitalBoard;

/* ==================== 数据模型（模块级；API 唯一落点） ==================== */

/** @brief 数字量单点（名称深拷贝；状态/类型/通道号随 API 翻转）。 */
typedef struct HsfDigitalPoint
{
    char* m_name;          /**< 名称（堆拷贝，UTF-8；NULL=缺省名）。 */
    HsfDigitalState m_state; /**< 当前态（OFF=红/ON=绿）。 */
    HsfDigitalKind  m_kind;  /**< 点类型（DI=只读空心环/DO=可写实心圆）。 */
    int m_channel;         /**< 通道号（<0=未设置不显示；显示在名称前）。 */
} HsfDigitalPoint;

/** @brief 页面共享模型（页可未创建时 API 仍可用，故独立于页对象）。 */
static struct
{
    HsfDigitalPoint* m_points; /**< 点位数组（堆；NULL=0 点）。 */
    int              m_count;  /**< 当前点数。 */
    bool             m_opEnabled; /**< 统一操作闸（缺省 false=只读）。 */
    HsfDigitalWriteFn m_writeFn;   /**< 写操作出口（可 NULL=无出口）。 */
    void*            m_writeUser;  /**< 写回调用户数据。 */
    void (*m_setStatus)(void*, const char*); /**< 状态栏反馈（可 NULL）。 */
    void*            m_statusUser;  /**< 状态栏反馈用户数据。 */
    HsfDigitalBoard* m_board;        /**< 已建看板借用指针（未建 NULL）。 */
    XCheckBox*       m_opCheck;      /**< 保留占位（本地闸已移除；勿用）。 */
    bool             m_compact;      /**< 竖屏形态（adapt 分档，缺省 false）。 */
    bool             m_autoMode;     /**< 自动运行模式（true=只读查看+隐藏闸 UI）。 */
    bool             m_pageBuilt;    /**< 页已创建（勾选框行显隐可操作）。 */
    int              m_fontPct;      /**< 已应用的页字号百分比（0=未应用按 100）。 */
    bool             m_fontCompact;  /**< 上次应用字号时的形态档（同值早退）。 */
    int              m_fontBasePx;   /**< 看板基准字号 px（首次锁定，防叠加缩放）。 */
} s_digital;

/* ---- 区段布局（DI 上段 / DO 下段，各自成行不混排） ---- */

/** @brief 区段标题行高（看板字体一行文本高 + 4px 余量）。 */
static void font_titleHeight(const HsfDigitalBoard* board, int* hOut)
{
    XFont font;
    int h;
    if (!hOut) return;
    *hOut = 20;
    if (!board) return;
    font = XWidget_font((XWidget*)board);
    h = XPainter_textHeight(&font);
    XClassDeinit(&font);
    if (h > 0) *hOut = h + 4;
}

/** @brief 数某类型点数（一次遍历双计数）。 */
static void digital_countKinds(int* diOut, int* doOut)
{
    int i;
    int di = 0;
    int dof = 0;
    for (i = 0; i < s_digital.m_count; ++i) {
        if (s_digital.m_points[i].m_kind == HSF_DIGITAL_OUTPUT) ++dof;
        else ++di;
    }
    if (diOut) *diOut = di;
    if (doOut) *doOut = dof;
}

/** @brief 显示槽位 → 全局点下标（section: 0=DI 段、1=DO 段；slot 段内序号）。
 *  @details 段内序号按全局下标升序（与点数 API 的下标语义一致）。 */
static int digital_slotToIndex(int section, int slot)
{
    int i;
    int seen = 0;
    for (i = 0; i < s_digital.m_count; ++i) {
        bool isOut = (s_digital.m_points[i].m_kind == HSF_DIGITAL_OUTPUT);
        if ((section == 1) != isOut) continue;
        if (seen == slot) return i;
        ++seen;
    }
    return -1;
}

/** @brief 全局点下标 → 显示槽位（出参 section/slot；不属于任何段返回 false）。 */
static bool digital_indexToSlot(int index, int* sectionOut, int* slotOut)
{
    int i;
    int seen = 0;
    if (index < 0 || index >= s_digital.m_count) return false;
    {
        bool isOut = (s_digital.m_points[index].m_kind == HSF_DIGITAL_OUTPUT);
        for (i = 0; i <= index; ++i) {
            bool iOut = (s_digital.m_points[i].m_kind == HSF_DIGITAL_OUTPUT);
            if ((isOut && !iOut) || (!isOut && iOut)) continue;
            if (i == index) {
                if (sectionOut) *sectionOut = isOut ? 1 : 0;
                if (slotOut) *slotOut = seen;
                return true;
            }
            ++seen;
        }
    }
    return false;
}

/** @brief 合成某点的显示名（前置声明：列宽实测要用它）。 */
static void digital_displayName(int index, char* buf, int size);

/** @brief 量一遍所有显示名的最大像素宽（含 "CH<n> " 前缀）。
 *  @details [2026-11-05 长名溢出根修] 原来列宽按固定常量 132px 算，而现场名
 *           形如 "CH16 电磁阀1 关到位"（12~14 个全角字）远超这个宽度，绘制
 *           又带 DONT_CLIP ⇒ 右侧相邻单元被压字、整行糊成一片（模拟器 32 点
 *           联调实测）。列宽必须按**实际最长的名字**量出来，不能拍一个常量。
 */
static int digital_measureMaxNameW(const HsfDigitalBoard* board, int textH)
{
    XFont font;
    int maxW = 0;
    int i;
    (void)textH;
    if (!board || s_digital.m_count <= 0) return 0;
    font = XWidget_font((XWidget*)board);
    for (i = 0; i < s_digital.m_count; ++i) {
        char name[DIGITAL_NAME_MAX];
        int w;
        digital_displayName(i, name, (int)sizeof(name));
        if (!name[0]) continue;
        w = XPainter_textWidth(&font, name);
        if (w > maxW) maxW = w;
    }
    XClassDeinit(&font);
    return maxW;
}

/** @brief 基准像素 × 页字号百分比（四舍五入）。
 *  @details 与 HsfUi.c 的 hsfUi_pageFontPx 同式同值（`(px*pct+50)/100`），
 *           区别只在 pct 由调用方显式给出——实时重放路径必须用**刚读到**
 *           的 pct，不能再用惰性缓存的旧值。 */
static int digitalScale(int px, int pct)
{
    if (pct < DIGITAL_FONT_PCT_MIN) pct = DIGITAL_FONT_PCT_MIN;
    if (pct > DIGITAL_FONT_PCT_MAX) pct = DIGITAL_FONT_PCT_MAX;
    return (px * pct + 50) / 100;
}

/** @brief 当前生效的页字号百分比（未应用过按 100）。 */
static int digitalFontPct(void)
{
    return s_digital.m_fontPct > 0 ? s_digital.m_fontPct : 100;
}

/** @brief 行列几何探针：按当前宽算列数与单元尺寸（paint/hint 共用）。 */
static void digital_board_metrics(const HsfDigitalBoard* board, int width,
                                  int* colsOut, int* cellWOut,
                                  int* cellHOut, int* rowsOut)
{
    int cols;
    int cellW;
    int cellH;
    int avail;
    int dotH;
    int textH;
    int minColW;
    XFont font;
    if (colsOut) *colsOut = 1;
    if (cellWOut) *cellWOut = DIGITAL_COL_MIN_W;
    if (cellHOut) *cellHOut = DIGITAL_CELL_H_MIN;
    if (rowsOut) *rowsOut = 0;
    if (!board || width <= DIGITAL_BOARD_MARGIN * 2) return;
    avail = width - DIGITAL_BOARD_MARGIN * 2;
    /* 单列最小宽：取「设计下限」与「实测最长名 + 左右内边距」的较大者。
     * 名字更长时宁可少排一列（滚动壳可纵向滚动），也不让文字互相压。
     * [字号联动] 下限按页字号同比放大（100% 时等于原常量）；实测名宽
     * 本身随看板字体自动变（digital_measureMaxNameW 读 XWidget_font），
     * 故两条口径在任意百分比下都不冲突。 */
    minColW = digitalScale(s_digital.m_compact ? DIGITAL_COL_MIN_W_C
                                               : DIGITAL_COL_MIN_W,
                           digitalFontPct());
    {
        const int nameW = digital_measureMaxNameW(board, 0);
        if (nameW > 0 && nameW + DIGITAL_NAME_PAD > minColW)
            minColW = nameW + DIGITAL_NAME_PAD;
        /* 上限：再长也不超过可视宽的一半，否则一屏只放得下一列。 */
        if (minColW > avail / 2) minColW = avail / 2;
        if (minColW < 1) minColW = 1;
    }
    cols = (avail + DIGITAL_GAP) / (minColW + DIGITAL_GAP);
    if (cols < 1) cols = 1;
    cellW = (avail - (cols - 1) * DIGITAL_GAP) / cols;
    if (cellW < minColW) cellW = minColW;
    font = XWidget_font((XWidget*)board);
    textH = XPainter_textHeight(&font);
    XClassDeinit(&font);
    dotH = DIGITAL_DOT_MAX;
    /* 圆点行 + 4px 间距 + 名称行（DI/DO 形状已区分，不再画类型标签）。
     * 竖屏单元加高（触控口径），多出的高度摊进名称行下缘留白。 */
    cellH = dotH + 4 + textH + 8;
    if (s_digital.m_compact && cellH < DIGITAL_TOUCH_H_MIN)
        cellH = DIGITAL_TOUCH_H_MIN;
    else if (cellH < DIGITAL_CELL_H_MIN)
        cellH = DIGITAL_CELL_H_MIN;
    if (colsOut) *colsOut = cols;
    if (cellWOut) *cellWOut = cellW;
    if (cellHOut) *cellHOut = cellH;
}

/** @brief 区段制整版几何：两段各自的行数/起 Y + 标题行高（paint/hit/hint 共用）。
 *  @details 版面自上而下：DI 段标题行（有 DI 点才有）→ DI 网格 → 段间距 →
 *           DO 段标题行（有 DO 点才有）→ DO 网格。段内 ceil 除法成行。 */
static void digital_board_metrics2(const HsfDigitalBoard* board, int width,
                                   int* colsOut, int* cellWOut,
                                   int* cellHOut, int* titleHOut,
                                   int* diYOut, int* diRowsOut,
                                   int* doYOut, int* doRowsOut)
{
    int cols;
    int cellW;
    int cellH;
    int titleH;
    int diCount;
    int doCount;
    int diRows;
    int doRows;
    int y;
    digital_board_metrics(board, width, &cols, &cellW, &cellH, NULL);
    font_titleHeight(board, &titleH);
    digital_countKinds(&diCount, &doCount);
    diRows = (cols > 0) ? (diCount + cols - 1) / cols : 0;
    doRows = (cols > 0) ? (doCount + cols - 1) / cols : 0;
    y = DIGITAL_BOARD_MARGIN;
    if (diCount > 0) {
        if (diYOut) *diYOut = y;
        y += titleH + 4 + diRows * (cellH + DIGITAL_GAP) - DIGITAL_GAP;
        y += DIGITAL_SECTION_GAP;
    } else if (diYOut) {
        *diYOut = -1; /* 空段隐藏。 */
    }
    if (doCount > 0) {
        if (doYOut) *doYOut = y;
    } else if (doYOut) {
        *doYOut = -1;
    }
    if (diRowsOut) *diRowsOut = diRows;
    if (doRowsOut) *doRowsOut = doRows;
    if (colsOut) *colsOut = cols;
    if (cellWOut) *cellWOut = cellW;
    if (cellHOut) *cellHOut = cellH;
    if (titleHOut) *titleHOut = titleH;
}

/**
 * @brief      单点单元格在看板内的矩形（局部标脏与 paintEvent 共用）。
 * @param      index 全局点下标。
 * @param      out   出参矩形（看板局部坐标）。
 * @return     true=可定位；false=几何未知/点不存在（调用方回退整板）。
 * @details    排版公式与 VDigitalBoard_paintEvent 逐项同源（两段标题 +
 *             网格行列），改版面必须两处同改，否则局部刷新漏脏留残影。
 *             [局部标脏 2026-11-08] 与模拟量页同款：值变化只脏该点格。
 */
static bool digital_cellRect(int index, XRect* out)
{
    const HsfDigitalBoard* board = s_digital.m_board;
    int section;
    int slot;
    int cols;
    int cellW;
    int cellH;
    int titleH;
    int diY;
    int diRows;
    int doY;
    int doRows;
    int width;
    int rowsTop;
    if (!out || !board) return false;
    if (!digital_indexToSlot(index, &section, &slot)) return false;
    width = XWidget_width((const XWidget*)board);
    if (width <= DIGITAL_BOARD_MARGIN * 2) return false;
    digital_board_metrics2(board, width, &cols, &cellW, &cellH, &titleH,
                           &diY, &diRows, &doY, &doRows);
    (void)diRows;
    (void)doRows;
    if (cols <= 0) return false;
    if (section == 0) {
        if (diY < 0) return false;
        rowsTop = diY + titleH + 4;
    } else {
        if (doY < 0) return false;
        rowsTop = doY + titleH + 4;
    }
    XRect_init(out,
               DIGITAL_BOARD_MARGIN + (slot % cols) * (cellW + DIGITAL_GAP),
               rowsTop + (slot / cols) * (cellH + DIGITAL_GAP), cellW, cellH);
    return true;
}

/** @brief 把某点的单元格标脏（几何不可定位时回退整板，保证不出残影）。 */
static void digital_invalidateCell(int index)
{
    XRect rect;
    if (!s_digital.m_board) return;
    if (digital_cellRect(index, &rect))
        XWidget_updateRect((XWidget*)s_digital.m_board, &rect);
    else
        XWidget_update((XWidget*)s_digital.m_board);
}

/** @brief 矩形是否与本次绘制的脏区相交（脏区 NULL=全量绘制）。 */
static bool digital_rectDirty(const XRect* rect, const XRect* clip)
{
    XRect hit;
    if (!rect) return false;
    if (!clip) return true;
    hit = XRect_intersected(rect, clip);
    return hit.width > 0 && hit.height > 0;
}

/** @brief 取点位显示名（buf 恒 NUL 收尾；无名称用缺省「点 N」）。
 *  @details 已设通道号（>=0）时加「CH<k> 」前缀（显示在名称前）。 */
static void digital_displayName(int index, char* buf, int size)
{
    const char* base;
    if (!buf || size <= 0) return;
    base = (index >= 0 && index < s_digital.m_count &&
            s_digital.m_points[index].m_name &&
            s_digital.m_points[index].m_name[0])
               ? s_digital.m_points[index].m_name : NULL;
    if (index >= 0 && index < s_digital.m_count &&
        s_digital.m_points[index].m_channel >= 0) {
        snprintf(buf, (size_t)size, "CH%d %s",
                 s_digital.m_points[index].m_channel,
                 base ? base : "");
        return;
    }
    if (base) {
        snprintf(buf, (size_t)size, "%s", base);
        return;
    }
    /* [2026-11-08 修正] 原为 snprintf(..., "点 N", index + 1)：格式串没有
     * %d（多给的实参被忽略）⇒ 无名无通道号的点恒显示字面「点 N」。 */
    snprintf(buf, (size_t)size, "点 %d", index + 1);
}

/** @brief 状态栏反馈（未挂接时静默）。 */
static void digital_setStatus(const char* text)
{
    if (s_digital.m_setStatus)
        s_digital.m_setStatus(s_digital.m_statusUser, text);
}

/**
 * @brief      点击命中处理：DO 点 + 闸放行 + 有出口才出写回调；
 *             DI 点恒只读（提示「输入点不可操作」），其余被拦情形只做
 *             状态栏提示（操作闸 UI 语义在状态栏可见）。
 * @return     true=已交业务回调（按压态由 mouse 事件管理）；false=被拦。
 */
static bool digital_tryWrite(int index)
{
    char text[DIGITAL_NAME_MAX + 32];
    char name[DIGITAL_NAME_MAX];
    HsfDigitalState target;
    if (index < 0 || index >= s_digital.m_count) return false;
    if (s_digital.m_points[index].m_kind != HSF_DIGITAL_OUTPUT) {
        digital_setStatus(
            "输入点不可"
            "操作"); /* 输入点不可操作 */
        return false;
    }
    if (s_digital.m_autoMode) {
        digital_setStatus(
            "自动运行中"
            ": 界面只读");
        /* 自动运行中: 界面只读 */
        return false;
    }
    if (!s_digital.m_opEnabled) {
        digital_setStatus("操作已禁止（界面只读）");
        return false;
    }
    if (!s_digital.m_writeFn) {
        digital_setStatus("未注册写出口");
        return false;
    }
    /* 目标态=当前态取反（切换语义；真实下发以业务回调为准）。 */
    target = (s_digital.m_points[index].m_state == HSF_DIGITAL_ON)
                 ? HSF_DIGITAL_OFF : HSF_DIGITAL_ON;
    s_digital.m_writeFn(s_digital.m_writeUser, index, target);
    return true;
}

/** @brief 绘制一个点位（圆点+名称+DI/DO 标签）于指定单元矩形。 */
static void digital_drawPoint(XPainter* painter, const HsfDigitalBoard* board,
                              int index, int cellX, int cellY,
                              int cellW, int cellH, int textH)
{
    int cx = cellX + cellW / 2;
    int cy = cellY + DIGITAL_DOT_MAX / 2;
    uint32_t color = (s_digital.m_points[index].m_state == HSF_DIGITAL_ON)
                         ? digitalThemeClr(DIGITAL_COLOR_ON,
                                           HSF_COLOR_STATE_OK_DARK)
                         : digitalThemeClr(DIGITAL_COLOR_OFF,
                                           HSF_COLOR_STATE_FAULT_FILL_DARK);
    char name[DIGITAL_NAME_MAX];
    /* [禁用态视觉：对齐 Qt 的 disabled 调色板] 自动模式/权限不足时 DO 点
     * 虽然内部已拒绝下发（digital_tryWrite），但此前**外观完全不变**——
     * 用户看到实心圆以为能点，点了没反应才知道被禁。这里把 DO 圆点整体
     * 调灰（QPalette::Disabled 同思路：状态色去饱和 + 描边变浅），并在
     * 点旁补一个锁提示；DI 是只读量测点，不受操作闸影响，保持原色。 */
    bool pointDisabled = (s_digital.m_points[index].m_kind == HSF_DIGITAL_OUTPUT) &&
                         (!s_digital.m_opEnabled || s_digital.m_autoMode);
    if (pointDisabled) {
        /* 去饱和：状态色与灰按 1:3 混合（保持明暗可辨但明显退居次要）。 */
        uint32_t r = (color >> 16) & 0xFFu;
        uint32_t g = (color >> 8) & 0xFFu;
        uint32_t b = color & 0xFFu;
        r = (r + 3u * 0xA0u) / 4u;
        g = (g + 3u * 0xA0u) / 4u;
        b = (b + 3u * 0xA0u) / 4u;
        color = 0xFF000000u | (r << 16) | (g << 8) | b;
    }
    /* 圆点：DO=实心圆（填充+细描边）；DI=空心环（底色填充+状态色
     * 3px 环，形状一眼区分输入/输出）。按压中的点放大 2px 反馈。 */
    {
        int dotR = DIGITAL_DOT_MAX / 2;
        /* 禁用态不放大：连按压反馈都不给，避免"看起来还有反应"。 */
        if (board->m_pressedIndex == index && !pointDisabled) dotR += 2;
        if (s_digital.m_points[index].m_kind == HSF_DIGITAL_OUTPUT) {
            XPainter_setBrush(painter, color);
            XPainter_setPen(painter, pointDisabled ? 0xFFD0D0D0u
                                                  : DIGITAL_COLOR_RING);
            XPainter_setPenWidth(painter, 1);
            XPainter_drawEllipse_2(painter, (float)cx, (float)cy,
                                   (float)dotR, (float)dotR);
        } else {
            XPainter_setBrush(painter,
                              digitalThemeClr(0xFFFFFFFFu,
                                              HSF_COLOR_PANEL_DARK));
            XPainter_setPen(painter, color);
            XPainter_setPenWidth(painter, 3);
            XPainter_drawEllipse_2(painter, (float)cx, (float)cy,
                                   (float)(dotR - 2), (float)(dotR - 2));
        }
    }
    /* 名称：圆点下方一行，水平居中（已设通道号时带 CH 前缀）。
     * DontClip：名称超单元宽时溢出绘制（不切字）；列宽已按
     * 「CHxxx 名称」加宽，溢出只是极端长名的兜底。 */
    digital_displayName(index, name, (int)sizeof(name));
    {
        XRect textRect;
        XRect_init(&textRect, cellX, cy + DIGITAL_DOT_MAX / 2 + 4,
                   cellW, textH + 4);
        XPainter_drawTextRect(painter, &textRect,
                              XPAINTER_TEXT_ALIGN_HCENTER |
                                  XPAINTER_TEXT_ALIGN_TOP |
                                  XPAINTER_TEXT_DONT_CLIP,
                              name,
                              digitalThemeClr(DIGITAL_COLOR_TEXT,
                                              HSF_COLOR_TEXT_DARK));
    }
}

/** @brief 绘制区段标题（「输入 (DI) N 点」/「输出 (DO) N 点」，左对齐粗体感）。 */
static void digital_drawSectionTitle(XPainter* painter, int y, int width,
                                     int titleH, bool isDo, int count)
{
    char text[96];
    XRect rect;
    snprintf(text, sizeof(text), "%s (%s) %d 点",
             isDo ? "输出" : "输入",
             isDo ? "DO" : "DI", count);
    /* 输出 (DO) N 点 / 输入 (DI) N 点 */
    XRect_init(&rect, DIGITAL_BOARD_MARGIN, y,
               width - DIGITAL_BOARD_MARGIN * 2, titleH);
    XPainter_drawTextRect(painter, &rect,
                          XPAINTER_TEXT_ALIGN_LEFT |
                              XPAINTER_TEXT_ALIGN_VCENTER,
                          text,
                          digitalThemeClr(DIGITAL_COLOR_TEXT,
                                          HSF_COLOR_TEXT_DARK));
}

/** @brief 绘制事件：清背景 → DI 段（标题+网格）→ DO 段（标题+网格）。 */
static void VDigitalBoard_paintEvent(XWidget* self, XEvent* event)
{
    HsfDigitalBoard* board = (HsfDigitalBoard*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XFont font;
    XRect clip;
    const XRect* clipPtr = NULL;
    int w;
    int h;
    int cols;
    int cellW;
    int cellH;
    int titleH;
    int diY;
    int diRows;
    int doY;
    int doRows;
    int textH;
    int diCount;
    int doCount;
    int slot;
    int i;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    image = XWidget_paintImage(self);
    if (!image) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPAINTER_CLIP_ON
    /* [消费 PAINT 裁剪矩形 2026-11-08 与模拟量页同款] 标脏已按单元格
     * 粒度（digital_invalidateCell），这里必须把事件矩形设成裁剪并跳过
     * 不相交的点格，否则一次值变化仍会把 16 个点全部重排重画。 */
    clip = XPaintEvent_rect((const XPaintEvent*)event);
    if (clip.width > 0 && clip.height > 0) {
        XPainter_setClipRect(&painter, &clip,
                             XPainterClipOperation_ReplaceClip);
        clipPtr = &clip;
    }
#endif /* XPAINTER_CLIP_ON */

    /* 页面底色（对标 autoFillBackground 手工版：看板自绘透明底）。
     * [随主题取套 2026-11-11] 深套落窗底令牌——原写死白底在深色页面
     * 中央浮出白块（走查实锅），正文/灯色各点已随 digitalThemeClr 取套。 */
    XPainter_fillRect(&painter, (const XRect*)&(XRect){0, 0, w, h},
                      digitalThemeClr(0xFFFFFFFFu, HSF_COLOR_WINDOW_DARK));

    font = XWidget_font(self);
    XPainter_setFont(&painter, &font);
    XPainter_setRenderHints(&painter, XPainterRenderHint_Antialiasing, true);
    textH = XPainter_textHeight(&font);
    XClassDeinit(&font);
    digital_board_metrics2(board, w, &cols, &cellW, &cellH, &titleH,
                           &diY, &diRows, &doY, &doRows);
    digital_countKinds(&diCount, &doCount);
    /* DI 段（输入区，上段）。 */
    if (diY >= 0 && diCount > 0) {
        int y = diY + titleH + 4;
        XRect titleRect;
        XRect_init(&titleRect, DIGITAL_BOARD_MARGIN, diY,
                   w - DIGITAL_BOARD_MARGIN * 2, titleH);
        if (digital_rectDirty(&titleRect, clipPtr))
            digital_drawSectionTitle(&painter, diY, w, titleH, false, diCount);
        for (slot = 0; slot < diCount; ++slot) {
            int row = slot / cols;
            int col = slot % cols;
            XRect cellRect;
            i = digital_slotToIndex(0, slot);
            if (i < 0) continue;
            XRect_init(&cellRect,
                       DIGITAL_BOARD_MARGIN + col * (cellW + DIGITAL_GAP),
                       y + row * (cellH + DIGITAL_GAP), cellW, cellH);
            if (!digital_rectDirty(&cellRect, clipPtr)) continue;
            digital_drawPoint(&painter, board, i, cellRect.x, cellRect.y,
                              cellW, cellH, textH);
        }
    }
    /* DO 段（输出区，下段）。 */
    if (doY >= 0 && doCount > 0) {
        int y = doY + titleH + 4;
        XRect titleRect;
        XRect_init(&titleRect, DIGITAL_BOARD_MARGIN, doY,
                   w - DIGITAL_BOARD_MARGIN * 2, titleH);
        if (digital_rectDirty(&titleRect, clipPtr))
            digital_drawSectionTitle(&painter, doY, w, titleH, true, doCount);
        /* [只读提示] 自动模式/操作闸关闭时，在 DO 段标题右侧常驻说明。
         * 与「点位变灰」配套：灰圆点说明"不能点"，这行说明"为什么不能点"。
         * 缺了它用户只看到一片灰，仍不知道是权限、模式还是断线。 */
        if ((!s_digital.m_opEnabled || s_digital.m_autoMode) &&
            digital_rectDirty(&titleRect, clipPtr)) {
            const char* hint = s_digital.m_autoMode
                                   ? "自动运行中：输出点仅可查看"
                                   : "界面操作已禁止：输出点仅可查看";
            XRect hintRect;
            XRect_init(&hintRect, DIGITAL_BOARD_MARGIN, doY,
                       w - DIGITAL_BOARD_MARGIN * 2, titleH);
            XPainter_drawTextRect(&painter, &hintRect,
                                  XPAINTER_TEXT_ALIGN_RIGHT |
                                      XPAINTER_TEXT_ALIGN_VCENTER,
                                  hint,
                                  digitalThemeClr(0xFF9A9A9Au,
                                                  HSF_COLOR_TEXT_SECONDARY_DARK));
        }
        for (slot = 0; slot < doCount; ++slot) {
            int row = slot / cols;
            int col = slot % cols;
            XRect cellRect;
            i = digital_slotToIndex(1, slot);
            if (i < 0) continue;
            XRect_init(&cellRect,
                       DIGITAL_BOARD_MARGIN + col * (cellW + DIGITAL_GAP),
                       y + row * (cellH + DIGITAL_GAP), cellW, cellH);
            if (!digital_rectDirty(&cellRect, clipPtr)) continue;
            digital_drawPoint(&painter, board, i, cellRect.x, cellRect.y,
                              cellW, cellH, textH);
        }
    }
    /* 空态提示（两段全空）。 */
    if (s_digital.m_count <= 0 || (diCount <= 0 && doCount <= 0)) {
        XRect textRect;
        XRect_init(&textRect, 0, 0, w, h);
        XPainter_drawTextRect(&painter, &textRect,
                              XPAINTER_TEXT_ALIGN_HCENTER |
                                  XPAINTER_TEXT_ALIGN_VCENTER,
                              "无数字量点",
                              digitalThemeClr(DIGITAL_COLOR_HINT,
                                              HSF_COLOR_TEXT_SECONDARY_DARK));
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 鼠标按下：命中圆点（圆心距 <= 半径+4px 容差）→ 记按压态。
 *  @details 命中走区段映射（DI 上段/DO 下段各自反解），DI 点不进按压态。 */
static void VDigitalBoard_mousePressEvent(XWidget* self, XEvent* event)
{
    HsfDigitalBoard* board = (HsfDigitalBoard*)self;
    XMouseEvent* mouse;
    XPoint pos;
    int w;
    int cols;
    int cellW;
    int cellH;
    int titleH;
    int diY;
    int diRows;
    int doY;
    int doRows;
    int section;
    int secY;
    int row;
    int col;
    int slot;
    int index;
    int dx;
    int dy;
    int hit = -1;
    if (!self || !event ||
        (XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
         XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK))
        return;
    mouse = (XMouseEvent*)event;
    if (XMouseEvent_button(mouse) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(mouse);
    w = XWidget_width(self);
    digital_board_metrics2(board, w, &cols, &cellW, &cellH, &titleH,
                           &diY, &diRows, &doY, &doRows);
    for (section = 0; section < 2; ++section) {
        secY = (section == 0) ? diY : doY;
        if (secY < 0) continue;
        /* 段内命中：行/列反解出槽位 → 全局下标 → 圆心距判定。 */
        row = (pos.y - secY - titleH - 4) / (cellH + DIGITAL_GAP);
        col = (pos.x - DIGITAL_BOARD_MARGIN) / (cellW + DIGITAL_GAP);
        if (row < 0 || col < 0 || col >= cols) continue;
        slot = row * cols + col;
        index = digital_slotToIndex(section, slot);
        if (index < 0) continue;
        {
            int cx = DIGITAL_BOARD_MARGIN + col * (cellW + DIGITAL_GAP) +
                     cellW / 2;
            int cy = secY + titleH + 4 + row * (cellH + DIGITAL_GAP) +
                     DIGITAL_DOT_MAX / 2;
            dx = pos.x - cx;
            dy = pos.y - cy;
            if (dx * dx + dy * dy <=
                (DIGITAL_DOT_MAX / 2 + 4) * (DIGITAL_DOT_MAX / 2 + 4)) {
                hit = index;
                break;
            }
        }
    }
    (void)diRows; (void)doRows;
    if (hit >= 0) {
        /* DI 点不进按压态（恒只读，无按压反馈）。 */
        if (s_digital.m_points[hit].m_kind == HSF_DIGITAL_OUTPUT) {
            board->m_pressedIndex = hit;
            XWidget_update(self); /* 按压反馈（+2px）即时可见。 */
        }
        XEvent_accept(event);
        return;
    }
    XEvent_ignore(event);
}

/** @brief 鼠标释放：按压态中且松在圆点内 → 走统一闸/写回调；清按压态。 */
static void VDigitalBoard_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    HsfDigitalBoard* board = (HsfDigitalBoard*)self;
    XMouseEvent* mouse;
    XPoint pos;
    int pressed;
    if (!self || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE)
        return;
    pressed = board->m_pressedIndex;
    board->m_pressedIndex = -1;
    XWidget_update(self);
    if (pressed < 0) return;
    mouse = (XMouseEvent*)event;
    pos = XMouseEvent_position(mouse);
    /* 释放仍须落在同一圆点（按下拖出=取消，按钮习惯）；坐标按区段制
     * 反解（按压点所属段）。 */
    {
        int w = XWidget_width(self);
        int cols;
        int cellW;
        int cellH;
        int titleH;
        int diY;
        int diRows;
        int doY;
        int doRows;
        int secY;
        int section;
        int slot;
        int row;
        int col;
        int cx;
        int cy;
        int dx;
        int dy;
        digital_board_metrics2((HsfDigitalBoard*)self, w, &cols, &cellW,
                               &cellH, &titleH, &diY, &diRows, &doY, &doRows);
        if (!digital_indexToSlot(pressed, &section, &slot)) {
            XEvent_accept(event);
            return;
        }
        secY = (section == 0) ? diY : doY;
        if (secY < 0) {
            XEvent_accept(event);
            return;
        }
        row = slot / cols;
        col = slot % cols;
        cx = DIGITAL_BOARD_MARGIN + col * (cellW + DIGITAL_GAP) + cellW / 2;
        cy = secY + titleH + 4 + row * (cellH + DIGITAL_GAP) +
             DIGITAL_DOT_MAX / 2;
        dx = pos.x - cx;
        dy = pos.y - cy;
        if (dx * dx + dy * dy <=
            (DIGITAL_DOT_MAX / 2 + 4) * (DIGITAL_DOT_MAX / 2 + 4))
            digital_tryWrite(pressed);
    }
    XEvent_accept(event);
}

/** @brief 区段制整版高度（sizeHint/minimumSizeHint 共用；给定宽反解）。 */
static int digital_boardHeightFor(const HsfDigitalBoard* board, int width)
{
    int cols;
    int cellW;
    int cellH;
    int titleH;
    int diY;
    int diRows;
    int doY;
    int doRows;
    int diCount;
    int doCount;
    int height;
    digital_board_metrics2(board, width, &cols, &cellW, &cellH, &titleH,
                           &diY, &diRows, &doY, &doRows);
    digital_countKinds(&diCount, &doCount);
    if (diCount <= 0 && doCount <= 0)
        return DIGITAL_BOARD_MARGIN * 2 + DIGITAL_CELL_H_MIN;
    /* 两段各占「标题行+4 + 行高」累加（空段隐藏；尾段后留边距）。 */
    height = DIGITAL_BOARD_MARGIN;
    if (diY >= 0)
        height += titleH + 4 +
                  diRows * (cellH + DIGITAL_GAP) - DIGITAL_GAP +
                  DIGITAL_SECTION_GAP;
    if (doY >= 0)
        height += titleH + 4 +
                  doRows * (cellH + DIGITAL_GAP) - DIGITAL_GAP;
    height += DIGITAL_BOARD_MARGIN - DIGITAL_SECTION_GAP;
    if (height < DIGITAL_BOARD_MARGIN * 2 + DIGITAL_CELL_H_MIN)
        height = DIGITAL_BOARD_MARGIN * 2 + DIGITAL_CELL_H_MIN;
    return height;
}

/** @brief sizeHint 虚槽（查询时重算）：宽随当前板宽，高按区段内容。 */
static XSize VDigitalBoard_sizeHintSlot(const XWidget* self)
{
    XSize out;
    int width;
    if (!self) {
        XSize_init(&out, -1, -1);
        return out;
    }
    width = XWidget_width((XWidget*)self);
    XSize_init(&out, width,
               digital_boardHeightFor((const HsfDigitalBoard*)self, width));
    return out;
}

/** @brief 最小提示：宽给「一列」下限（不得把当前板宽当下限——滚动壳
 *         widgetResizable 以 minimumSizeHint 收窄内容，宽随视口收缩是
 *         Qt 语义）；高按当前板宽反解——高度下限若按单列最坏情况给死，
 *         宽视口下内容被假下限撑出一屏空白（滚动壳内容高取
 *         max(视口, minHint, sizeHint)），随宽收敛同 sizeHint 口径，
 *         窄视口由滚动壳两遍收敛兜底。 */
static XSize VDigitalBoard_minimumSizeHintSlot(const XWidget* self)
{
    XSize out;
    int minW;
    int curW;
    if (!self) {
        XSize_init(&out, -1, -1);
        return out;
    }
    minW = DIGITAL_BOARD_MARGIN * 2 +
           (s_digital.m_compact ? DIGITAL_COL_MIN_W_C : DIGITAL_COL_MIN_W);
    curW = XWidget_width((XWidget*)self);
    if (curW < minW) curW = minW;
    XSize_init(&out, minW,
               digital_boardHeightFor((const HsfDigitalBoard*)self, curW));
    return out;
}

/** @brief 状态/点数/宽度变化统一走 update（轻量重绘）。 */
static void digital_boardRefresh(void)
{
    if (s_digital.m_board) {
        XWidget_update((XWidget*)s_digital.m_board);
        XWidget_updateGeometry((XWidget*)s_digital.m_board);
    }
}

/* ---- XCLASS 虚表：PaintEvent/Mouse/SizeHint 覆盖，其余继承 XWidget ---- */

#if XGUI_ON
static void VDigitalBoard_timerEvent(XObject* object, XTimerEvent* event);
static void digital_seedDemo(void); /* 演示数据（定义在 autotest 段）。 */
#endif /* XGUI_ON */

XVtable* HsfDigitalBoard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfDigitalBoard)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VDigitalBoard_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VDigitalBoard_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VDigitalBoard_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VDigitalBoard_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint,
                             VDigitalBoard_minimumSizeHintSlot);
#if XGUI_ON
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VDigitalBoard_timerEvent);
#endif /* XGUI_ON */
    return XVTABLE_DEFAULT;
}

/* ==================== 页对象与装配 ==================== */

/** @brief 数字量页对象；m_base 必须是第一个成员。
 *  @note   返回语义由左侧导航树/主菜单承担，页内不放返回钮。 */
typedef struct HsfMonitorDigital
{
    XWidget       m_base;      /**< 页容器；必须是第一个成员。 */
    XBoxLayout    m_layout;    /**< 页内垂直布局。 */
    XBoxLayout    m_topRow;    /**< 顶行：标题居中 + 勾选框贴右（同一行）。 */
    XLabel        m_title;     /**< 页标题（粗）。 */
    XCheckBox     m_opCheck;   /**< 保留占位（本地闸已移除；勿用）。 */
    HsfDigitalBoard* m_board;  /**< 圆点网格看板（堆；随页级联释放）。 */
    XLabel        m_hint;      /**< 底部提示。 */
} HsfMonitorDigital;

/** @brief 看板基准字号（px）：取页根继承字体（主题 12pt@96dpi = 16px）。
 *  @details 只在首次调用时求值并**锁定**：之后一律以它为基准乘百分比，
 *           否则「150% → 160%」会拿已放大的字号当新基准，缩放层层叠加。
 *           页根字体来自 HsfTheme_applyDefaultFont（只设家族，pixelSize
 *           为 -1）⇒ 这里按 pointSizeF 折算到像素（与 paint 度量同源：
 *           XPainter 的 scale = pixelSize/unitsPerEm，见 XPainter.c:10448）。 */
static int digital_boardBaseFontPx(XWidget* page)
{
    XFont f;
    int px;
    if (s_digital.m_fontBasePx > 0) return s_digital.m_fontBasePx;
    px = 0;
    if (page) {
        f = XWidget_font(page);
        px = XFont_pixelSize(&f);
        if (px <= 0) px = XPainter_fontPixelSize(XFont_pointSizeF(&f));
        XClassDeinit(&f);
    }
    if (px <= 0) px = DIGITAL_BOARD_FONT_PX;
    s_digital.m_fontBasePx = px;
    return px;
}

/** @brief 看板字体按像素字号应用（同值早退）。
 *  @details 看板是自绘控件：paintEvent 用 XWidget_font(self) 取字体，
 *           textH=XPainter_textHeight 驱动单元高/列宽/命中区（metrics 同源），
 *           故只要把字体设对，整块版面自动跟随。XWidget_setFont 内含
 *           updateGeometry + update（XWidget.c:11890-11891），sizeHint /
 *           最小提示随之失效重算。 */
static void digital_applyBoardFontPx(int px)
{
    XFont f;
    if (!s_digital.m_board || px <= 0) return;
    f = XWidget_font((XWidget*)s_digital.m_board);
    if (XFont_pixelSize(&f) == px) { /* 同值早退（帧泵/重放不空转）。 */
        XClassDeinit(&f);
        return;
    }
    XFont_setPixelSize(&f, px);
    XWidget_setFont((XWidget*)s_digital.m_board, &f);
    XClassDeinit(&f);
}

/** @brief 页字体与联动度量统一应用（create / adapt / 实时重放唯一入口）。
 *  @param page    页根（&self->m_base）。
 *  @param compact 形态档（决定标题行高与看板最小高）。
 *  @param pct     页字号百分比（50..200；100=基准，逐像素同旧观感）。
 *  @details 完整换算链：本地设置 FontScale/Page3 → hsfUi_pageFontPct
 *           → digitalScale（基准 × pct）→ XWidget_setFont（看板字体）
 *           → paint 的 textH → digital_board_metrics 的单元高/列宽
 *           → 圆点网格布局。标题/提示是 XLabel（自持字体），同 pct 重设。 */
static void digitalApplyFontMetrics(XWidget* page, bool compact, int pct)
{
    HsfMonitorDigital* self = (HsfMonitorDigital*)page;
    int minH;
    if (!self || !self->m_board) return;
    if (pct < DIGITAL_FONT_PCT_MIN) pct = DIGITAL_FONT_PCT_MIN;
    if (pct > DIGITAL_FONT_PCT_MAX) pct = DIGITAL_FONT_PCT_MAX;
    /* 标签只在「字号或形态档真变」时重设（XLabel_setTextPixelSize 无同值
     * 早退，adapt 在拖窗时会被反复调用）。 */
    if (pct != s_digital.m_fontPct || compact != s_digital.m_fontCompact) {
        XLabel_setTextPixelSize(&self->m_title,
                                digitalScale(DIGITAL_TITLE_PX, pct));
        XWidget_setFixedHeight((XWidget*)&self->m_title,
                               digitalScale(compact ? DIGITAL_TITLE_ROW_H_C
                                                    : DIGITAL_TITLE_ROW_H, pct));
        XLabel_setTextPixelSize(&self->m_hint,
                                digitalScale(DIGITAL_HINT_PX, pct));
        /* metrics 读 m_fontPct 折算列宽 → 必须先落值，重绘才拿到新列宽。 */
        s_digital.m_fontPct = pct;
        s_digital.m_fontCompact = compact;
    }
    digital_applyBoardFontPx(digitalScale(digital_boardBaseFontPx(page), pct));
    /* 看板最小高：单元最小高 + 字号联动余量（竖屏取触控下限，同旧口径）。 */
    minH = compact ? DIGITAL_TOUCH_H_MIN
                   : DIGITAL_CELL_H_MIN + digitalScale(DIGITAL_BOARD_EXTRA_H, pct);
    XWidget_setMinimumHeight((XWidget*)self->m_board, minH);
}

/** @brief 字号实时重放（hsfUi_registerPageFontApplier 契约）。
 *  @details 与 adapt 共用 digitalApplyFontMetrics ⇒ 重放 = 一次完整的
 *           字号重设；末尾补看板 update/updateGeometry，防旧字形残影。
 *           [与回落路径互斥] 本页已注册本函数 ⇒ hsfUi_reapplyPageFonts
 *           不再走「清 adapt 去重标记 + hsf_adaptPages」分支，故这里必须
 *           把 adapt 里与字号相关的三件事（标题/提示字号、标题行高、
 *           看板最小高）全部覆盖——已由共用函数保证。 */
static void digitalApplyFonts(XWidget* page, int pct)
{
    digitalApplyFontMetrics(page, s_digital.m_compact, pct);
    digital_boardRefresh();
}

/** @brief 形态自适应（横竖屏；幂等、廉价，HsfUi adapt 表按页调用）。
 *  @details 竖屏（compact）：列最小宽收窄（窄屏也能排 2~3 列）、单元
 *           触控目标加高（44dp 口径）、页边距收窄；
 *           横屏恢复宽列。列数本就随视口宽实时反解（paint/hit/hint
 *           同源），无需重排。字号/联动度量经 digitalApplyFontMetrics
 *           统一应用。 */
void HsfMonitorDigital_adapt(XWidget* page, bool compact)
{
    HsfMonitorDigital* self = (HsfMonitorDigital*)page;
    if (!self) return;
    s_digital.m_compact = compact;
    /* 字号/联动度量走唯一入口（pct 每次现读，本地设置改完不必重启）。 */
    digitalApplyFontMetrics(page, compact,
                            hsfUi_pageFontPct(HSF_PAGE_MONITOR_DIGITAL));
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 6 : 12, 10, compact ? 6 : 12, 12);
    /* 标题行恒横向（标题居中 + 勾选框贴右，横竖屏同构）；竖屏加高触控。 */
    XBoxLayout_setDirection(&self->m_topRow, XBoxLayoutDirection_LeftToRight);
    digital_boardRefresh();
}

/** @brief 操作闸勾选框槽：勾选状态即统一闸（唯一写入口在 API，UI 只映射）。 */
static void digital_opCheckSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorDigital* self = (HsfMonitorDigital*)receiver;
    (void)args;
    if (!self) return;
    HsfMonitorDigital_setOperationEnabled(true);
    digital_setStatus(s_digital.m_opEnabled
                              ? "界面操作已允许"
                              : "界面操作已禁止");
    /* 「界面已允许操作」/「界面已禁止操作」 */
}

XWidget* HsfMonitorDigital_create(XWidget* parent, HsfMenuNavFn navFn,
                                  void* userData)
{
    HsfMonitorDigital* self;
    HsfDigitalBoard* board;
    (void)navFn;      /* 返回语义由导航树承担；签名随 builders 契约保留。 */
    (void)userData;
    if (!parent) return NULL;
    self = (HsfMonitorDigital*)XMemory_malloc(sizeof(HsfMonitorDigital),
                                              XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true); /* 整块随主窗级联释放。 */

    HsfTheme_applyDefaultFont(&self->m_base);

    /* ---- 看板（自定义控件，先建后装配） ---- */
    board = (HsfDigitalBoard*)XMemory_malloc(sizeof(HsfDigitalBoard),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!board) {
        XFree_System(self);
        return NULL;
    }
    memset(board, 0, sizeof(*board));
    XWidget_init(&board->m_base, &self->m_base, 0);
    XClassSetVtable(board, HsfDigitalBoard);
    Set_Class_Memory(board, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(board, true);
    board->m_pressedIndex = -1;
#if XGUI_ON
    board->m_clickTestId = XTIMER_INVALID_ID;
    board->m_clickExpect = -1;
#endif /* XGUI_ON */
    s_digital.m_board = board;
    self->m_board = board;
    XWidget_show((XWidget*)board);

    /* ---- 顶行：标题 + 操作闸勾选框（返回语义由导航树承担，页内不放钮） ---- */
    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    {
        XFont f = XWidget_font((XWidget*)&self->m_title);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&self->m_title, &f);
        XClassDeinit(&f);
    }
    XLabel_setText_2(&self->m_title,
                     "数字量"); /* 数字量 */
    XLabel_setAlignment(&self->m_title,
                        XAlignment_HCenter | XAlignment_VCenter);
    XWidget_show((XWidget*)&self->m_title);

    /* [本地「允许界面操作」勾选框已移除] 2026-11-06 对齐 Python：控制可用性
     * 由**全局运行模式**决定（自动模式只读 / 手动模式可操作）+ 连接态，
     * 不再要求用户先勾一个本地开关（现场观感=点不动）。 */
    s_digital.m_pageBuilt = true;

    /* ---- 底部提示 ---- */
    XLabel_init(&self->m_hint, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_hint);
    /* [底部提示只讲图例，不复述闸门原因] 只读的**原因**分两种（自动运行 /
     * 未连接·权限不足），且二者在视觉上都是「DO 点变灰」。原因由 DO 段
     * 标题右侧那行常驻说明承担（见本文件 drawPoint 上方的只读提示块：
     * 「自动运行中：输出点仅可查看」/「界面操作已禁止：输出点仅可查看」），
     * 底部这行只负责解释颜色与空心/实心语义——两处都写"自动"会把
     * 权限不足误报成自动运行，正是本轮要修的那类文案漂移。 */
    XLabel_setText_2(&self->m_hint,
                     "绿=通 红=断开 — 空心=DI只读  实心=DO可操作"
                     "（只读原因见输出区右上角）");
    /* 绿=通 红=断开 — 空心=DI只读 实心=DO可操作（只读原因见输出区右上角） */
    XLabel_setAlignment(&self->m_hint,
                        XAlignment_HCenter | XAlignment_VCenter);
    HsfTheme_applySecondaryText((XWidget*)&self->m_hint);
    XWidget_show((XWidget*)&self->m_hint);

    /* ---- 页内垂直布局 ---- */
    XBoxLayout_init(&self->m_layout, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)&self->m_layout, 12, 10, 12, 12);
    XLayout_setSpacing((XLayout*)&self->m_layout, 8);
    XWidget_setLayout(&self->m_base, (XLayout*)&self->m_layout);

    XBoxLayout_init(&self->m_topRow, XBoxLayoutDirection_LeftToRight);
    XLayout_setSpacing((XLayout*)&self->m_topRow, 12);
    /* 标题 + 勾选框同一行（XBoxLayout）：两侧伸展因子 1:1 把标题挤在
     * 行内水平居中，勾选框贴行尾（右上角）；勾选框隐藏（自动模式）时
     * 标题仍在伸展对称点居中，布局不跳。 */
    XBoxLayout_addStretch(&self->m_topRow, 1);
    XBoxLayout_addWidget(&self->m_topRow, (XWidget*)&self->m_title);
    XBoxLayout_addSpacing(&self->m_topRow, 12);
    XBoxLayout_addStretch(&self->m_topRow, 1);
    XBoxLayout_addLayout(&self->m_layout, &self->m_topRow);

    XBoxLayout_addWidget(&self->m_layout, (XWidget*)board);
    XBoxLayout_setStretch(&self->m_layout, 1, 1); /* 看板吃伸展。 */

    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_hint);

    /* [不再铺演示数据] 2026-11-10：与模拟量页**同口径**收敛。
     * 现场只接了真实点位；铺 16 个编造的点位（风机启停/阀门1~6/清洗阀…，
     * 通道号 CH101~CH208，**一半标成 DO 可点**）会让操作员以为那些点真的
     * 在线，是**假数据**（现场最忌讳）。更险的是：演示点可点 ⇒ 点了会走
     * hsfWinDigitalWrite 的 `index - n` 算出通道号发给服务端，**可能误控
     * 真实通道**（点数少于演示下标时尤其危险）。故：模型为空时页面就是
     * 空的，等服务端快照来了再长出点位；演示数据只在自测钩子
     * （XHSF_DIGITAL_AUTOTEST）与**显式离线演示**（--no-login，由 HsfUi
     * 建窗后调 seedDemoForOffline）两条非生产路径下灌。登录路径不灌；
     * 登录后服务端快照 setCount 整体覆盖本批演示点。 */
    if (s_digital.m_count <= 0 && XSystem_environment("XHSF_DIGITAL_AUTOTEST"))
        digital_seedDemo();

    /* 字号/联动度量一次到位（create 与 adapt/实时重放同一入口；100% 时
     * 与本页原常量逐像素一致 ⇒ 默认观感零回归）。 */
    digitalApplyFontMetrics(&self->m_base, false,
                            hsfUi_pageFontPct(HSF_PAGE_MONITOR_DIGITAL));
    /* 字号实时重放登记（本地设置改动时由 HsfUi 调回，不重启即生效）。 */
    hsfUi_registerPageFontApplier(HSF_PAGE_MONITOR_DIGITAL, &self->m_base,
                                  digitalApplyFonts);
    XWidget_show(&self->m_base);
    return &self->m_base;
}

/* ==================== 公开 API（模型唯一写入口；页未建时安全） ==================== */

void HsfMonitorDigital_setCount(int count)
{
    int i;
    HsfDigitalPoint* next;
    if (count < 0) count = 0;
    if (count == s_digital.m_count) return;
    next = NULL;
    if (count > 0) {
        next = (HsfDigitalPoint*)XMemory_malloc(
            (size_t)count * sizeof(HsfDigitalPoint),
            XCLASS_DEFAULT_MEMORY_TYPE);
        if (!next) return; /* 分配失败保持旧模型（安全降级）。 */
        memset(next, 0, (size_t)count * sizeof(HsfDigitalPoint));
        /* 共同前缀逐点迁移（状态与名称指针；扩点新增 OFF）。 */
        for (i = 0; i < count && i < s_digital.m_count; ++i)
            next[i] = s_digital.m_points[i];
    }
    XFree_System(s_digital.m_points);
    s_digital.m_points = next;
    s_digital.m_count = count;
    digital_boardRefresh();
}

int HsfMonitorDigital_count(void)
{
    return s_digital.m_count;
}

void HsfMonitorDigital_seedDemoForOffline(void)
{
    /* [仅离线演示] 由 HsfUi 在 --no-login 建窗后调用（登录路径不调用）：
     * 离线态没有服务端快照，数字量模型恒空，页面只有「无数字量点」空态，
     * 指定截图无法验收点位字号/标签重叠。只在显式 --no-login 路径灌（登录
     * 路径不灌——现场真实点位绝不能被假数据顶替；登录后服务端快照
     * setCount 会整体覆盖本批演示点）。 */
    if (s_digital.m_count > 0) return;
    digital_seedDemo();
}

void HsfMonitorDigital_setValue(int index, HsfDigitalState state)
{
    if (index < 0 || index >= s_digital.m_count) return;
    if (state != HSF_DIGITAL_OFF && state != HSF_DIGITAL_ON) return;
    if (s_digital.m_points[index].m_state == state) return;
    s_digital.m_points[index].m_state = state;
    digital_invalidateCell(index);
}

HsfDigitalState HsfMonitorDigital_value(int index)
{
    if (index < 0 || index >= s_digital.m_count) return HSF_DIGITAL_OFF;
    return s_digital.m_points[index].m_state;
}

void HsfMonitorDigital_setKind(int index, HsfDigitalKind kind)
{
    if (index < 0 || index >= s_digital.m_count) return;
    if (kind != HSF_DIGITAL_INPUT && kind != HSF_DIGITAL_OUTPUT) return;
    if (s_digital.m_points[index].m_kind == kind) return;
    s_digital.m_points[index].m_kind = kind;
    digital_invalidateCell(index);
}

HsfDigitalKind HsfMonitorDigital_kind(int index)
{
    if (index < 0 || index >= s_digital.m_count) return HSF_DIGITAL_OUTPUT;
    return s_digital.m_points[index].m_kind;
}

void HsfMonitorDigital_setName(int index, const char* name)
{
    char* copy;
    if (index < 0 || index >= s_digital.m_count) return;
    /* [同值早退 2026-11-08] 快照每次推送都重放 16 个点位名称；此前每条
     * 都 alloc/free + 标脏 ⇒ 单次推送 16 笔脏区、16 帧（实测数字量页
     * 159.8fps）。名称是静态配置，相同则什么都不做。 */
    if (strcmp(s_digital.m_points[index].m_name
                   ? s_digital.m_points[index].m_name : "",
               name ? name : "") == 0)
        return;
    copy = NULL;
    if (name && name[0]) {
        size_t bytes = strlen(name) + 1;
        copy = (char*)XMemory_malloc(bytes, XCLASS_DEFAULT_MEMORY_TYPE);
        if (!copy) return;
        memcpy(copy, name, bytes);
    }
    XFree_System(s_digital.m_points[index].m_name);
    s_digital.m_points[index].m_name = copy;
    digital_invalidateCell(index);
}

void HsfMonitorDigital_name(int index, char* buf, int size)
{
    if (!buf || size <= 0) return;
    buf[0] = '\0';
    if (index < 0 || index >= s_digital.m_count) {
        snprintf(buf, (size_t)size, "点 %d", index + 1);
        return;
    }
    digital_displayName(index, buf, size);
}

void HsfMonitorDigital_setChannel(int index, int channel)
{
    if (index < 0 || index >= s_digital.m_count) return;
    if (s_digital.m_points[index].m_channel == channel) return;
    s_digital.m_points[index].m_channel = channel;
    digital_invalidateCell(index);
}

int HsfMonitorDigital_channel(int index)
{
    if (index < 0 || index >= s_digital.m_count) return -1;
    return s_digital.m_points[index].m_channel;
}

void HsfMonitorDigital_setOperationEnabled(bool enabled)
{
    /* 自动运行模式：界面强制只读（唯一解锁口是退出自动模式）。 */
    if (s_digital.m_autoMode) {
        if (!s_digital.m_opEnabled) return;
        s_digital.m_opEnabled = false;
        digital_boardRefresh();
        return;
    }
    if (s_digital.m_opEnabled == enabled) return;
    s_digital.m_opEnabled = enabled;
    digital_boardRefresh();
}

bool HsfMonitorDigital_operationEnabled(void)
{
    return s_digital.m_opEnabled;
}

void HsfMonitorDigital_setInteractiveMode(bool interactive)
{
    /* [本函数是诚实 setter：调用方说退手动就退] env 自测钩子
     * （XHSF_DIGITAL_AUTO）的"别被驱动翻回手动"不在这里判——见
     * HsfUi.c hsfWinApplyControlGates 的驱动侧让路。头文件
     * （HsfMonitorDigital.h:153-162）与本函数的往返契约必须保持可用。 */
    if (s_digital.m_autoMode == (!interactive)) return;
    s_digital.m_autoMode = !interactive;
    if (interactive) {
        /* 退出自动模式：恢复可操作（对齐 Python：手动模式即可操作）。 */
        s_digital.m_opEnabled = true;
    } else {
        /* 进入自动模式：强制只读（真实通道在自动程序手里，界面不允许
         * 再下发操作，只允许查看）。 */
        s_digital.m_opEnabled = false;
        digital_setStatus(
            "自动运行中"
            ": 界面只读");
        /* 自动运行中: 界面只读 */
    }
    digital_boardRefresh();
}

bool HsfMonitorDigital_interactiveMode(void)
{
    return !s_digital.m_autoMode;
}

void HsfMonitorDigital_setWriteHandler(HsfDigitalWriteFn handler,
                                       void* userData)
{
    s_digital.m_writeFn = handler;
    s_digital.m_writeUser = userData;
}

void HsfMonitorDigital_attachStatus(void (*setStatus)(void*, const char*),
                                    void* user)
{
    s_digital.m_setStatus = setStatus;
    s_digital.m_statusUser = user;
}

#if XGUI_ON

/* ---- 演示数据 + 点击链路自测（autotest 专用） ---- */

/** @brief 灌 16 点演示数据（DI/DO 混合 + 通道号；create 空模型与
 *         autotest 共用）。业务层接入后 setCount 即整体覆盖。 */
static void digital_seedDemo(void)
{
    static const char* kNames[16] = {
        "风机启停",   /* 风机启停 */
        "泵启停",               /* 泵启停 */
        "阀门1",                       /* 阀门1 */
        "阀门2",                       /* 阀门2 */
        "报警灯",               /* 报警灯 */
        "故障位",               /* 故障位 */
        "运行位",               /* 运行位 */
        "备用泵",               /* 备用泵 */
        "搅拌器",               /* 搅拌器 */
        "加热器",               /* 加热器 */
        "排风机",               /* 排风机 */
        "照明",                           /* 照明 */
        "隔离器",               /* 隔离器 */
        "总线停机",   /* 总线停机 */
        "维修模式",   /* 维修模式 */
        "清洗阀"                /* 清洗阀 */
    };
    int i;
    HsfMonitorDigital_setCount(16);
    for (i = 0; i < 16; ++i) {
        HsfMonitorDigital_setName(i, kNames[i]);
        HsfMonitorDigital_setValue(i,
            (i & 1) ? HSF_DIGITAL_ON : HSF_DIGITAL_OFF);
        /* DI/DO 混合演示：偶数下标=DI（只读空心环），奇数=DO（可操作）。 */
        HsfMonitorDigital_setKind(i,
            (i & 1) ? HSF_DIGITAL_OUTPUT : HSF_DIGITAL_INPUT);
        /* 通道号演示：DI 段 CH101 起排、DO 段 CH201 起排（显示在名称前）。 */
        HsfMonitorDigital_setChannel(i, (i & 1) ? 201 + i / 2 : 101 + i / 2);
    }
}

/** @brief autotest 写回调：记录最近一次写请求（下标+目标态）。 */
static int g_clickLastIndex = -1;
static HsfDigitalState g_clickLastTarget = HSF_DIGITAL_OFF;
static int g_clickHits = 0;

static void digital_clickRecord(void* userData, int index,
                                HsfDigitalState target)
{
    (void)userData;
    g_clickLastIndex = index;
    g_clickLastTarget = target;
    ++g_clickHits;
}

/** @brief 自测步进语义：0=点 DO 首点（应回调）、1=点 DI 首点（应拦）、
 *         2=点空白（无反应）、3=收尾断言+状态栏汇报。 */
static void digital_clickSend(HsfDigitalBoard* board, int step)
{
    int cols;
    int cellW;
    int cellH;
    int titleH;
    int diY;
    int diRows;
    int doY;
    int doRows;
    int diCount;
    int doCount;
    int x;
    int y;
    XMouseEvent* me;
    XPoint pos;
    digital_board_metrics2(board, XWidget_width(&board->m_base), &cols,
                           &cellW, &cellH, &titleH, &diY, &diRows,
                           &doY, &doRows);
    digital_countKinds(&diCount, &doCount);
    /* 圆心 X：第 0 列；Y：段标题下首行圆心。 */
    x = DIGITAL_BOARD_MARGIN + cellW / 2;
    if (step == 0) { /* DO 段首点圆心。 */
        if (doY < 0 || doCount <= 0) return;
        y = doY + titleH + 4 + DIGITAL_DOT_MAX / 2;
    } else if (step == 1) { /* DI 段首点圆心。 */
        if (diY < 0 || diCount <= 0) return;
        y = diY + titleH + 4 + DIGITAL_DOT_MAX / 2;
    } else { /* 段间空白带（两段之间，必无圆点）。 */
        if (diY < 0 || doY < 0) return;
        x = DIGITAL_BOARD_MARGIN + 2;
        y = doY - DIGITAL_SECTION_GAP / 2;
    }
    XPoint_init(&pos, x, y);
    me = XMouseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                               XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                               XMouseButton_LeftButton,
                               (XKeyboardModifiers)0, pos);
    if (me) {
        XMouseEvent_setButtons(me, XMouseButton_LeftButton);
        XObject_event_base((XObject*)board, (XEvent*)me);
        XClassDelete((XEvent*)me);
    }
    me = XMouseEvent_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                               XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                               XMouseButton_LeftButton,
                               (XKeyboardModifiers)0, pos);
    if (me) {
        XMouseEvent_setButtons(me, XMouseButton_NoButton);
        XObject_event_base((XObject*)board, (XEvent*)me);
        XClassDelete((XEvent*)me);
    }
}

/** @brief TimerEvent：点击自测步进泵（看板虚表挂载；自测专用）。 */
static void VDigitalBoard_timerEvent(XObject* object, XTimerEvent* event)
{
    HsfDigitalBoard* board = (HsfDigitalBoard*)object;
    if (!board || !event) return;
    if (board->m_clickTestId == XTIMER_INVALID_ID ||
        XTimerEvent_timerId(event) != board->m_clickTestId)
        return;
    if (board->m_clickStep <= 2) {
        int before = g_clickHits;
        /* 自动运行模式下 DO 点击也应被拦（界面只读），step0 期望改 0。 */
        bool autoMode = !HsfMonitorDigital_interactiveMode();
        digital_clickSend(board, board->m_clickStep);
        if (board->m_clickStep == 0) {
            /* DO 点击：手动模式应回调一次且目标=取反；自动模式应拦。 */
            board->m_clickExpect = digital_slotToIndex(1, 0);
            if (autoMode) {
                if (g_clickHits == before) {
                    digital_setStatus(
                        "CLICK-TEST step0 DO(auto-locked): PASS");
                } else {
                    digital_setStatus("CLICK-TEST step0 DO(auto-locked): FAIL");
                    XPrintf("[digital-click] step0 auto FAIL hits=%d\n",
                            g_clickHits);
                }
            } else if (g_clickHits == before + 1 &&
                g_clickLastIndex == board->m_clickExpect &&
                (g_clickLastTarget == HSF_DIGITAL_ON ||
                 g_clickLastTarget == HSF_DIGITAL_OFF)) {
                digital_setStatus(
                    "CLICK-TEST step0 DO: PASS");
            } else {
                digital_setStatus("CLICK-TEST step0 DO: FAIL");
                XPrintf("[digital-click] step0 FAIL hits=%d idx=%d\n",
                        g_clickHits, g_clickLastIndex);
            }
        } else if (board->m_clickStep == 1) {
            /* DI 点击：恒只读，不应回调。 */
            if (g_clickHits == before) {
                digital_setStatus("CLICK-TEST step1 DI: PASS");
            } else {
                digital_setStatus("CLICK-TEST step1 DI: FAIL");
                XPrintf("[digital-click] step1 FAIL hits=%d\n", g_clickHits);
            }
        } else { /* 空白：不应回调。 */
            if (g_clickHits == before) {
                digital_setStatus("CLICK-TEST step2 blank: PASS");
            } else {
                digital_setStatus("CLICK-TEST step2 blank: FAIL");
            }
        }
        ++board->m_clickStep;
        return;
    }
    /* 收尾：停泵并汇报总判定（手动模式期望恰好 1 次回调；自动模式
     * 期望 0 次——DO 也被锁）。 */
    XObject_killTimer((XObject*)board, board->m_clickTestId);
    board->m_clickTestId = XTIMER_INVALID_ID;
    {
        int hits = g_clickHits;
        bool ok = HsfMonitorDigital_interactiveMode() ? (hits == 1)
                                                      : (hits == 0);
        digital_setStatus(ok ? "CLICK-TEST ALL PASS"
                             : "CLICK-TEST FAIL");
        XPrintf("[digital-click] done hits=%d lastIdx=%d lastTarget=%d "
                "auto=%d\n", hits, g_clickLastIndex,
                (int)g_clickLastTarget,
                HsfMonitorDigital_interactiveMode() ? 0 : 1);
    }
}

int HsfMonitorDigital_autotest(XWidget* page)
{
    HsfDigitalBoard* board;
    if (!XSystem_environment("XHSF_DIGITAL_AUTOTEST")) return -1;
    if (!page) return -1;
    digital_seedDemo();
    HsfMonitorDigital_setOperationEnabled(true);
    /* 点击链路自测：注册记录回调，起步进泵（300ms/步；直投合成鼠标
     * 事件到看板局部坐标——DO 圆心应回调、DI 圆心应拦、空白应无反应）。 */
    board = s_digital.m_board;
    if (board && s_digital.m_board) {
        HsfMonitorDigital_setWriteHandler(digital_clickRecord, NULL);
        g_clickHits = 0;
        g_clickLastIndex = -1;
        board->m_clickStep = 0;
        board->m_clickTestId = XObject_startTimer_ms(
            (XObject*)board, 300u, XTimerType_CoarseTimer);
    }
    /* 自动模式演示：XHSF_DIGITAL_AUTO=1 时进自动运行态（勾选框隐藏+
     * 界面只读），验证锁界面 UI（点击自测仍会跑，DO 点击被拦属预期，
     * hits=0；状态栏显示自动模式提示）。
     * [防被闸门翻回] 本地只建立态；"闸门别退手动"由驱动侧让路承担
     * （HsfUi.c hsfWinApplyControlGates 读同名 env 后跳过驱动），
     * 本 setter 对任何调用方保持诚实。 */
    if (XSystem_environment("XHSF_DIGITAL_AUTO"))
        HsfMonitorDigital_setInteractiveMode(false);
    return 16;
}
#endif /* XGUI_ON */

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
