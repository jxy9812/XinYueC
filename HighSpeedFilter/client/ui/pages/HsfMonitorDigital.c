/**
 * @file       HsfMonitorDigital.c
 * @brief      数字量监视/操作页实现：圆点网格看板 + 统一操作闸 + 全量 API。
 * @details    结构（对标 Qt 的 QWidget 子类习惯，框架零改动）：
 *             - HsfDigitalBoard：文件内自定义控件（XCLASS 机制继承
 *               XWidget），paintEvent 自绘「圆点 + 名称」网格（绿=通、
 *               红=断开），sizeHint 虚槽按当前宽度算行数给高度提示
 *               （滚动壳 widgetResizable 语义下：宽随视口、高出滚动），
 *               mousePressEvent 命中圆点且统一闸放行时经写回调出 UI；
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
#define DIGITAL_COL_MIN_W      132  /**< 横屏单列最小宽（容纳「CHxxx 名称」）。 */
#define DIGITAL_COL_MIN_W_C    108  /**< 竖屏单列最小宽（窄屏多排一列）。 */
#define DIGITAL_GAP            12   /**< 单元间横/纵间距。 */
#define DIGITAL_SECTION_GAP    18   /**< DI/DO 两区段之间的间距。 */
#define DIGITAL_NAME_MAX       64   /**< 名称缓冲上限（含 NUL）。 */

/* ---- 看板颜色（ARGB；语义由 API 状态决定，非业务色） ---- */
#define DIGITAL_COLOR_ON       0xFF2E9E4Fu  /**< 通（绿）。 */
#define DIGITAL_COLOR_OFF      0xFFD93636u  /**< 断开（红）。 */
#define DIGITAL_COLOR_RING     0x40000000u  /**< 圆点描边（半透明黑）。 */
#define DIGITAL_COLOR_TEXT     0xE6202020u  /**< 名称文本。 */
#define DIGITAL_COLOR_HINT     0xFF8A8A8Au  /**< 空态/提示。 */

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
    XCheckBox*       m_opCheck;      /**< 操作闸勾选框借用（未建 NULL）。 */
    bool             m_compact;      /**< 竖屏形态（adapt 分档，缺省 false）。 */
    bool             m_autoMode;     /**< 自动运行模式（true=只读查看+隐藏闸 UI）。 */
    bool             m_pageBuilt;    /**< 页已创建（勾选框行显隐可操作）。 */
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
    XFont font;
    if (colsOut) *colsOut = 1;
    if (cellWOut) *cellWOut = DIGITAL_COL_MIN_W;
    if (cellHOut) *cellHOut = DIGITAL_CELL_H_MIN;
    if (rowsOut) *rowsOut = 0;
    if (!board || width <= DIGITAL_BOARD_MARGIN * 2) return;
    avail = width - DIGITAL_BOARD_MARGIN * 2;
    cols = (avail + DIGITAL_GAP) /
           ((s_digital.m_compact ? DIGITAL_COL_MIN_W_C : DIGITAL_COL_MIN_W) +
            DIGITAL_GAP);
    if (cols < 1) cols = 1;
    cellW = (avail - (cols - 1) * DIGITAL_GAP) / cols;
    if (cellW < (s_digital.m_compact ? DIGITAL_COL_MIN_W_C
                                     : DIGITAL_COL_MIN_W))
        cellW = s_digital.m_compact ? DIGITAL_COL_MIN_W_C
                                    : DIGITAL_COL_MIN_W;
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

/** @brief 取点位显示名（buf 恒 NUL 收尾；无名称用缺省「点 N」）。
 *  @details 已设通道号（>=0）时加「CH<k> 」前缀（显示在名称前）。 */static void digital_displayName(int index, char* buf, int size)
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
    snprintf(buf, (size_t)size, "\xE7\x82\xB9 %d", index + 1); /* 点 N */
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
            "\xE8\xBE\x93\xE5\x85\xA5\xE7\x82\xB9\xE4\xB8\x8D\xE5\x8F\xAF"
            "\xE6\x93\x8D\xE4\xBD\x9C"); /* 输入点不可操作 */
        return false;
    }
    if (s_digital.m_autoMode) {
        digital_setStatus(
            "\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD"
            "\x3A\x20\xE7\x95\x8C\xE9\x9D\xA2\xE5\x8F\xAA\xE8\xAF\xBB");
        /* 自动运行中: 界面只读 */
        return false;
    }
    if (!s_digital.m_opEnabled) {
        digital_setStatus(
            "\xE6\x93\x8D\xE4\xBD\x9C\xE5\xB7\xB2\xE7\xA6\x81\xE6\xAD\xA2"
            "\xEF\xBC\x88\xE7\x95\x8C\xE9\x9D\xA2\xE5\x8F\xAA\xE8\xAF\xBB"
            "\xEF\xBC\x89"); /* 操作已禁止（界面只读） */
        return false;
    }
    if (!s_digital.m_writeFn) {
        digital_setStatus(
            "\xE6\x9C\xAA\xE6\xB3\xA8\xE5\x86\x8C\xE5\x86\x99\xE5\x87\xBA"
            "\xE5\x8F\xA3"); /* 未注册写出口 */
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
                         ? DIGITAL_COLOR_ON : DIGITAL_COLOR_OFF;
    char name[DIGITAL_NAME_MAX];
    /* 圆点：DO=实心圆（填充+细描边）；DI=空心环（底色填充+状态色
     * 3px 环，形状一眼区分输入/输出）。按压中的点放大 2px 反馈。 */
    {
        int dotR = DIGITAL_DOT_MAX / 2;
        if (board->m_pressedIndex == index) dotR += 2;
        if (s_digital.m_points[index].m_kind == HSF_DIGITAL_OUTPUT) {
            XPainter_setBrush(painter, color);
            XPainter_setPen(painter, DIGITAL_COLOR_RING);
            XPainter_setPenWidth(painter, 1);
            XPainter_drawEllipse_2(painter, (float)cx, (float)cy,
                                   (float)dotR, (float)dotR);
        } else {
            XPainter_setBrush(painter, 0xFFFFFFFFu);
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
                              name, DIGITAL_COLOR_TEXT);
    }
}

/** @brief 绘制区段标题（「输入 (DI) N 点」/「输出 (DO) N 点」，左对齐粗体感）。 */
static void digital_drawSectionTitle(XPainter* painter, int y, int width,
                                     int titleH, bool isDo, int count)
{
    char text[96];
    XRect rect;
    snprintf(text, sizeof(text), "%s (%s) %d \xE7\x82\xB9",
             isDo ? "\xE8\xBE\x93\xE5\x87\xBA" : "\xE8\xBE\x93\xE5\x85\xA5",
             isDo ? "DO" : "DI", count);
    /* 输出 (DO) N 点 / 输入 (DI) N 点 */
    XRect_init(&rect, DIGITAL_BOARD_MARGIN, y,
               width - DIGITAL_BOARD_MARGIN * 2, titleH);
    XPainter_drawTextRect(painter, &rect,
                          XPAINTER_TEXT_ALIGN_LEFT |
                              XPAINTER_TEXT_ALIGN_VCENTER,
                          text, DIGITAL_COLOR_TEXT);
}

/** @brief 绘制事件：清背景 → DI 段（标题+网格）→ DO 段（标题+网格）。 */
static void VDigitalBoard_paintEvent(XWidget* self, XEvent* event)
{
    HsfDigitalBoard* board = (HsfDigitalBoard*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XFont font;
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

    /* 页面底色（对标 autoFillBackground 手工版：看板自绘透明底）。 */
    XPainter_fillRect(&painter,
                      (const XRect*)&(XRect){0, 0, w, h}, 0xFFFFFFFFu);

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
        digital_drawSectionTitle(&painter, diY, w, titleH, false, diCount);
        for (slot = 0; slot < diCount; ++slot) {
            int row = slot / cols;
            int col = slot % cols;
            i = digital_slotToIndex(0, slot);
            if (i < 0) continue;
            digital_drawPoint(&painter, board, i,
                              DIGITAL_BOARD_MARGIN + col * (cellW + DIGITAL_GAP),
                              y + row * (cellH + DIGITAL_GAP),
                              cellW, cellH, textH);
        }
    }
    /* DO 段（输出区，下段）。 */
    if (doY >= 0 && doCount > 0) {
        int y = doY + titleH + 4;
        digital_drawSectionTitle(&painter, doY, w, titleH, true, doCount);
        for (slot = 0; slot < doCount; ++slot) {
            int row = slot / cols;
            int col = slot % cols;
            i = digital_slotToIndex(1, slot);
            if (i < 0) continue;
            digital_drawPoint(&painter, board, i,
                              DIGITAL_BOARD_MARGIN + col * (cellW + DIGITAL_GAP),
                              y + row * (cellH + DIGITAL_GAP),
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
                              "\xE6\x97\xA0\xE6\x95\xB0\xE5\xAD\x97\xE9\x87"
                              "\x8F\xE7\x82\xB9", /* 无数字量点 */
                              DIGITAL_COLOR_HINT);
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
    XCheckBox     m_opCheck;   /**< 统一操作闸勾选框（界面开关；自动模式隐藏）。 */
    HsfDigitalBoard* m_board;  /**< 圆点网格看板（堆；随页级联释放）。 */
    XLabel        m_hint;      /**< 底部提示。 */
} HsfMonitorDigital;

/** @brief 形态自适应（横竖屏；幂等、廉价，HsfUi adapt 表按页调用）。
 *  @details 竖屏（compact）：列最小宽收窄（窄屏也能排 2~3 列）、单元
 *           触控目标加高（44dp 口径）、页边距收窄、顶行改纵向堆叠
 *           （标题独占一行居中，勾选框整行，触控友好）；
 *           横屏恢复横向顶行与宽列。列数本就随视口宽实时反解
 *           （paint/hit/hint 同源），无需重排。 */
void HsfMonitorDigital_adapt(XWidget* page, bool compact)
{
    HsfMonitorDigital* self = (HsfMonitorDigital*)page;
    if (!self) return;
    s_digital.m_compact = compact;
    XWidget_setMinimumHeight((XWidget*)self->m_board,
                             compact ? DIGITAL_TOUCH_H_MIN
                                     : DIGITAL_CELL_H_MIN + 32);
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 6 : 12, 10, compact ? 6 : 12, 12);
    /* 标题行恒横向（标题居中 + 勾选框贴右，横竖屏同构）；竖屏加高触控。 */
    XBoxLayout_setDirection(&self->m_topRow, XBoxLayoutDirection_LeftToRight);
    XWidget_setFixedHeight((XWidget*)&self->m_title, compact ? 30 : 26);
    XWidget_setFixedHeight((XWidget*)&self->m_opCheck, compact ? 30 : 22);
    digital_boardRefresh();
}

/** @brief 操作闸勾选框槽：勾选状态即统一闸（唯一写入口在 API，UI 只映射）。 */
static void digital_opCheckSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorDigital* self = (HsfMonitorDigital*)receiver;
    (void)args;
    if (!self) return;
    HsfMonitorDigital_setOperationEnabled(
        XCheckBox_isChecked(&self->m_opCheck));
    digital_setStatus(s_digital.m_opEnabled
                          ? "\xE7\x95\x8C\xE9\x9D\xA2\xE6\x93\x8D\xE4\xBD"
                            "\x9C\xE5\xB7\xB2\xE5\x85\x81\xE8\xAE\xB8"
                          : "\xE7\x95\x8C\xE9\x9D\xA2\xE6\x93\x8D\xE4\xBD"
                            "\x9C\xE5\xB7\xB2\xE7\xA6\x81\xE6\xAD\xA2");
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
    XWidget_setMinimumHeight((XWidget*)board, DIGITAL_CELL_H_MIN + 32);

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
                     "\xE6\x95\xB0\xE5\xAD\x97\xE9\x87\x8F"); /* 数字量 */
    XLabel_setTextPixelSize(&self->m_title, 17);
    XLabel_setAlignment(&self->m_title,
                        XAlignment_HCenter | XAlignment_VCenter);
    XWidget_show((XWidget*)&self->m_title);

    XCheckBox_init(&self->m_opCheck, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_opCheck);
    XCheckBox_setText_2(&self->m_opCheck,
                        "\xE5\x85\x81\xE8\xAE\xB8\xE7\x95\x8C\xE9\x9D\xA2"
                        "\xE6\x93\x8D\xE4\xBD\x9C"); /* 允许界面操作 */
    XCheckBox_setChecked(&self->m_opCheck, s_digital.m_opEnabled);
    XObject_connect_1((XObject*)&self->m_opCheck,
                      XSignal(XAbstractButton_toggled_signal),
                      (XObject*)self, digital_opCheckSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_opCheck);
    s_digital.m_opCheck = &self->m_opCheck; /* 闸回显借用（页内成员）。 */
    /* 自动运行模式：勾选框行隐藏、闸锁定只读（API 恢复后重显）。 */
    if (s_digital.m_autoMode) {
        XWidget_hide((XWidget*)&self->m_opCheck);
    }
    s_digital.m_pageBuilt = true;

    /* ---- 底部提示 ---- */
    XLabel_init(&self->m_hint, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_hint);
    XLabel_setText_2(&self->m_hint,
                     "\xE7\xBB\xBF\x3D\xE9\x80\x9A\x20\xE7\xBA\xA2\x3D"
                     "\xE6\x96\xAD\xE5\xBC\x80\x20\xE2\x80\x94\x20\xE7\xA9"
                     "\xBA\xE5\xBF\x83\x3D\x44\x49\xE5\x8F\xAA\xE8\xAF\xBB"
                     "\x20\xE5\xAE\x9E\xE5\xBF\x83\x3D\x44\x4F\xE5\x8F\xAF"
                     "\xE6\x93\x8D\xE4\xBD\x9C\x20\xE7\x82\xB9\xE5\x87\xBB"
                     "\xE5\xAE\x9E\xE5\xBF\x83\xE5\x9C\x86\xE6\x93\x8D\xE4"
                     "\xBD\x9C\xEF\xBC\x88\xE9\x9C\x80\xE5\x85\x81\xE8\xAE"
                     "\xB8\xE7\x95\x8C\xE9\x9D\xA2\xE6\x93\x8D\xE4\xBD\x9C"
                     "\xEF\xBC\x89");
    /* 绿=通 红=断开 — 空心=DI只读 实心=DO可操作 点击实心圆操作（需允许界面操作） */
    XLabel_setTextPixelSize(&self->m_hint, 12);
    XLabel_setAlignment(&self->m_hint,
                        XAlignment_HCenter | XAlignment_VCenter);
    XWidget_setForegroundRole((XWidget*)&self->m_hint,
                              XPaletteColorRole_Mid);
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
    XBoxLayout_addWidget(&self->m_topRow, (XWidget*)&self->m_opCheck);
    XBoxLayout_addLayout(&self->m_layout, &self->m_topRow);

    XBoxLayout_addWidget(&self->m_layout, (XWidget*)board);
    XBoxLayout_setStretch(&self->m_layout, 1, 1); /* 看板吃伸展。 */

    XBoxLayout_addWidget(&self->m_layout, (XWidget*)&self->m_hint);

    /* 模型为空（业务层尚未接入）时种缺省演示数据：页面首开即有内容
     * 可看；业务层一句 setCount/setXxx 即可整体覆盖（真实接入后本批
     * 演示点被换掉，不残留）。 */
    if (s_digital.m_count <= 0)
        digital_seedDemo();

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

void HsfMonitorDigital_setValue(int index, HsfDigitalState state)
{
    if (index < 0 || index >= s_digital.m_count) return;
    if (state != HSF_DIGITAL_OFF && state != HSF_DIGITAL_ON) return;
    if (s_digital.m_points[index].m_state == state) return;
    s_digital.m_points[index].m_state = state;
    if (s_digital.m_board) XWidget_update((XWidget*)s_digital.m_board);
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
    if (s_digital.m_board) XWidget_update((XWidget*)s_digital.m_board);
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
    copy = NULL;
    if (name && name[0]) {
        size_t bytes = strlen(name) + 1;
        copy = (char*)XMemory_malloc(bytes, XCLASS_DEFAULT_MEMORY_TYPE);
        if (!copy) return;
        memcpy(copy, name, bytes);
    }
    XFree_System(s_digital.m_points[index].m_name);
    s_digital.m_points[index].m_name = copy;
    if (s_digital.m_board) XWidget_update((XWidget*)s_digital.m_board);
}

void HsfMonitorDigital_name(int index, char* buf, int size)
{
    if (!buf || size <= 0) return;
    buf[0] = '\0';
    if (index < 0 || index >= s_digital.m_count) {
        snprintf(buf, (size_t)size, "\xE7\x82\xB9 %d", index + 1);
        return;
    }
    digital_displayName(index, buf, size);
}

void HsfMonitorDigital_setChannel(int index, int channel)
{
    if (index < 0 || index >= s_digital.m_count) return;
    if (s_digital.m_points[index].m_channel == channel) return;
    s_digital.m_points[index].m_channel = channel;
    if (s_digital.m_board) XWidget_update((XWidget*)s_digital.m_board);
}

int HsfMonitorDigital_channel(int index)
{
    if (index < 0 || index >= s_digital.m_count) return -1;
    return s_digital.m_points[index].m_channel;
}

void HsfMonitorDigital_setOperationEnabled(bool enabled)
{
    /* 自动运行模式：界面强制只读，API 也锁闸（唯一解锁口是
     * setInteractiveMode(false) 退出自动模式）。 */
    if (s_digital.m_autoMode) {
        if (!s_digital.m_opEnabled) return;
        s_digital.m_opEnabled = false;
        if (s_digital.m_opCheck)
            XCheckBox_setChecked(s_digital.m_opCheck, false);
        digital_boardRefresh();
        return;
    }
    if (s_digital.m_opEnabled == enabled) return;
    s_digital.m_opEnabled = enabled;
    /* UI 开关随模型态回显（对齐 Qt setChecked 语义：同值不发 toggled，
     * 故此处静默置位不触发 digital_opCheckSlot 回环）。 */
    if (s_digital.m_opCheck)
        XCheckBox_setChecked(s_digital.m_opCheck, enabled);
    digital_boardRefresh();
}

bool HsfMonitorDigital_operationEnabled(void)
{
    return s_digital.m_opEnabled;
}

void HsfMonitorDigital_setInteractiveMode(bool interactive)
{
    if (s_digital.m_autoMode == (!interactive)) return;
    s_digital.m_autoMode = !interactive;
    if (interactive) {
        /* 退出自动模式：重显闸开关，闸值回到勾选框当前态（保持此前
         * API/用户设定；缺省关）。 */
        if (s_digital.m_opCheck) {
            s_digital.m_opEnabled =
                XCheckBox_isChecked(s_digital.m_opCheck);
            XWidget_show((XWidget*)s_digital.m_opCheck);
        }
    } else {
        /* 进入自动模式：隐藏闸开关 + 强制只读（真实通道在自动程序
         * 手里，界面不允许再下发操作，只允许查看）。 */
        s_digital.m_opEnabled = false;
        if (s_digital.m_opCheck)
            XCheckBox_setChecked(s_digital.m_opCheck, false);
        if (s_digital.m_opCheck)
            XWidget_hide((XWidget*)s_digital.m_opCheck);
        digital_setStatus(
            "\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD"
            "\x3A\x20\xE7\x95\x8C\xE9\x9D\xA2\xE5\x8F\xAA\xE8\xAF\xBB");
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
        "\xE9\xA3\x8E\xE6\x9C\xBA\xE5\x90\xAF\xE5\x81\x9C",   /* 风机启停 */
        "\xE6\xB3\xB5\xE5\x90\xAF\xE5\x81\x9C",               /* 泵启停 */
        "\xE9\x98\x80\xE9\x97\xA8\x31",                       /* 阀门1 */
        "\xE9\x98\x80\xE9\x97\xA8\x32",                       /* 阀门2 */
        "\xE6\x8A\xA5\xE8\xAD\xA6\xE7\x81\xAF",               /* 报警灯 */
        "\xE6\x95\x85\xE9\x9A\x9C\xE4\xBD\x8D",               /* 故障位 */
        "\xE8\xBF\x90\xE8\xA1\x8C\xE4\xBD\x8D",               /* 运行位 */
        "\xE5\xA4\x87\xE7\x94\xA8\xE6\xB3\xB5",               /* 备用泵 */
        "\xE6\x90\x85\xE6\x8B\x8C\xE5\x99\xA8",               /* 搅拌器 */
        "\xE5\x8A\xA0\xE7\x83\xAD\xE5\x99\xA8",               /* 加热器 */
        "\xE6\x8E\x92\xE9\xA3\x8E\xE6\x9C\xBA",               /* 排风机 */
        "\xE7\x85\xA7\xE6\x98\x8E",                           /* 照明 */
        "\xE9\x9A\x94\xE7\xA6\xBB\xE5\x99\xA8",               /* 隔离器 */
        "\xE6\x80\xBB\xE7\xBA\xBF\xE5\x81\x9C\xE6\x9C\xBA",   /* 总线停机 */
        "\xE7\xBB\xB4\xE4\xBF\xAE\xE6\xA8\xA1\xE5\xBC\x8F",   /* 维修模式 */
        "\xE6\xB8\x85\xE6\xB4\x97\xE9\x98\x80"                /* 清洗阀 */
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
     * hits=0；状态栏显示自动模式提示）。 */
    if (XSystem_environment("XHSF_DIGITAL_AUTO"))
        HsfMonitorDigital_setInteractiveMode(false);
    return 16;
}
#endif /* XGUI_ON */

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
