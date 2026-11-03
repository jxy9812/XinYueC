/**
 * @file       HsfMonitorAnalog.c
 * @brief      模拟量监视页实现：数值卡网格看板 + 统一操作闸 + 全量 API。
 * @details    结构（与 HsfMonitorDigital 同构，框架零改动）：
 *             - HsfAnalogBoard：文件内自定义控件（XCLASS 机制继承
 *               XWidget），paintEvent 自绘「数值卡」网格——单卡三行：
 *               通道号+名称 / 原始值（整型口径）/ 转换值+单位（工程量
 *               double，按小数位格式化；量程越限显示报警红）；
 *               sizeHint 虚槽按当前宽算列数/行数给高度提示（滚动壳
 *               widgetResizable 语义：宽随视口、高出滚动）；
 *               mousePressEvent/mouseReleaseEvent 命中卡片且交互模式 +
 *               统一闸放行时经点击回调出 UI；
 *             - 数据模型（静态模块级 s_analog）：点数/双值/名称/单位/
 *               通道号/小数位/量程数组 + 统一闸 + 自动模式 + 点击回调，
 *               全部经本头公开 API 读写（调用前页可未创建，安全空操作）；
 *             - 页内装配：标题行（XBoxLayout 对称伸展：标题居中 + 操作
 *               闸勾选框贴右上角，横竖屏同构）+ 看板 + 底部提示；
 *             - 兼容自测钩子：XHSF_ANALOG_AUTOTEST=1 灌 12 点演示数据
 *               （走公开 API，验证回显链路）；XHSF_ANALOG_AUTO=1 额外
 *               进自动运行模式（验证锁界面）；点击自测直投合成鼠标事件
 *               断言回调/拦截（与数字量页同口径）。
 */
#include "HsfMonitorAnalog.h"

#include "HsfTheme.h"
#include "HsfPages.h"
#include "XLabel.h"
#include "XCheckBox.h"
#include "XAbstractButton.h"
#include "XLineEdit.h"
#include "XRadioButton.h"
#include "XDialog.h"
#include "XDialogButtonBox.h"
#include "XBoxLayout.h"
#include "XGridLayout.h"
#include "XObject.h"
#include "XMemory.h"
#include "XWidget_Protected.h"
#include "XEvent.h"
#include "XWindowEvent.h"
#include "XPainter.h"
#include "XCommonStyle.h"
#include "XAlgorithm.h"
#include "XSystem.h"
#include "XTimer.h"
#include <string.h>
#include <stdio.h>

#if XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON

/* ---- 看板几何常量（逻辑像素） ---- */
#define ANALOG_BOARD_MARGIN   16   /**< 看板内容边距。 */
#define ANALOG_CARD_H_MIN     84   /**< 单卡最小高（三行文本）。 */
#define ANALOG_TOUCH_H_MIN    96   /**< 竖屏单卡最小高（触控口径）。 */
#define ANALOG_COL_MIN_W      150  /**< 横屏单列最小宽（数值+单位）。 */
#define ANALOG_COL_MIN_W_C    128  /**< 竖屏单列最小宽（窄屏多排一列）。 */
#define ANALOG_GAP            12   /**< 卡片间横/纵间距。 */
#define ANALOG_SECTION_GAP    18   /**< AI/AO 两区段之间的间距。 */
#define ANALOG_NAME_MAX       64   /**< 名称/单位缓冲上限（含 NUL）。 */
#define ANALOG_BADGE_GAP      8    /**< 名称与右上 AI/AO 角标的间距。 */

/* ---- 卡片颜色（ARGB；语义由 API 状态决定，非业务色） ---- */
#define ANALOG_COLOR_TEXT     0xE6202020u  /**< 名称/主文本。 */
#define ANALOG_COLOR_VALUE    0xFF1F4E79u  /**< 转换值（品牌深蓝，主视觉）。 */
#define ANALOG_COLOR_RAW      0xFF6B6B6Bu  /**< 原始值（弱灰）。 */
#define ANALOG_COLOR_UNIT     0xFF8A8A8Au  /**< 单位（更弱）。 */
#define ANALOG_COLOR_HINT     0xFF8A8A8Au  /**< 空态/提示。 */
#define ANALOG_COLOR_ALARM    0xFFD93636u  /**< 量程越限（报警红）。 */
#define ANALOG_COLOR_CARD_BG  0x0A1F4E79u  /**< 卡片底（极淡品牌蓝）。 */
#define ANALOG_COLOR_CARD_ED  0x241F4E79u  /**< 卡片描边（淡品牌蓝）。 */

/* ==================== 数据模型（模块级；API 唯一落点） ==================== */

/** @brief 模拟量单点（名称/单位深拷贝；双值/类型/描述符随 API 翻转）。 */
typedef struct HsfAnalogPoint
{
    char*   m_name;     /**< 名称（堆拷贝，UTF-8；NULL=缺省名）。 */
    char*   m_unit;     /**< 单位（堆拷贝，UTF-8；NULL=不显示）。 */
    int     m_raw;      /**< 原始值（整型口径：ADC/寄存器原始字）。 */
    double  m_scaled;   /**< 转换值（工程量 double）。 */
    int     m_channel;  /**< 通道号（<0=未设置不显示）。 */
    int     m_decimals; /**< 转换值小数位（0..6；缺省 2）。 */
    double  m_min;      /**< 量程下限（转换值口径）。 */
    double  m_max;      /**< 量程上限（转换值口径）。 */
    bool    m_hasRange; /**< 是否启用越限判定（setRange 时置位）。 */
    HsfAnalogKind m_kind; /**< 点类型（AI=只读 / AO=可操作；缺省 AI）。 */
} HsfAnalogPoint;

/** @brief 页面共享模型（页可未创建时 API 仍可用，故独立于页对象）。 */
static struct
{
    HsfAnalogPoint* m_points;    /**< 点位数组（堆；NULL=0 点）。 */
    int             m_count;     /**< 当前点数。 */
    bool            m_opEnabled; /**< 统一操作闸（缺省 false=只读）。 */
    bool            m_autoMode;  /**< 自动运行模式（true=只读+隐藏闸 UI）。 */
    bool            m_pageBuilt; /**< 页已创建（闸 UI 显隐可操作）。 */
    int             m_columns;   /**< 强制列数（0=自动按宽反解）。 */
    HsfAnalogTapFn  m_tapFn;     /**< 卡片点击出口（可 NULL=无出口）。 */
    void*           m_tapUser;   /**< 点击回调用户数据。 */
    void (*m_setStatus)(void*, const char*); /**< 状态栏反馈（可 NULL）。 */
    void*           m_statusUser; /**< 状态栏反馈用户数据。 */
    struct HsfAnalogBoard* m_board; /**< 已建看板借用指针（未建 NULL）。 */
    XCheckBox*      m_opCheck;   /**< 操作闸勾选框借用（未建 NULL）。 */
    bool            m_compact;   /**< 竖屏形态（adapt 分档，缺省 false）。 */
} s_analog;

/* ==================== 看板控件（文件内自定义控件） ==================== */

/**
 * @brief      看板控件对象；m_base 必须是第一个成员。
 * @details    自绘网格：paintEvent 按当前宽算列数/行数 → 数值卡；
 *             sizeHint 虚槽按「查询时重算」给高度（宽随视口来自滚动壳
 *             widgetResizable，高度提示驱动竖滚）。
 */
XCLASS_DEFINE_BEGING(HsfAnalogBoard)
XCLASS_DEFINE_EXTEND_END(HsfAnalogBoard, XWidget)

typedef struct HsfAnalogBoard
{
    XWidget m_base;        /**< 基类成员；必须是第一个。 */
    int     m_pressedIndex; /**< 按压中的卡片（-1=无；按压反馈用）。 */
#if XGUI_ON
    int     m_tapTestId;    /**< 点击自测定时器（XTIMER_INVALID_ID=未启用）。 */
    int     m_tapStep;      /**< 自测步进（0=卡片命中，1=空白，2=收尾）。 */
#endif /* XGUI_ON */
} HsfAnalogBoard;

/** @brief 行列几何：按当前宽算列数与卡尺寸（paint/hit/hint 共用）。 */
/* ---- 区段布局（AI 上段 / AO 下段，各自成行不混排；与数字量页同构） ---- */

/** @brief 数某类型点数（一次遍历双计数）。 */
static void analog_countKinds(int* aiOut, int* aoOut)
{
    int i;
    int ai = 0;
    int ao = 0;
    for (i = 0; i < s_analog.m_count; ++i) {
        if (s_analog.m_points[i].m_kind == HSF_ANALOG_OUTPUT) ++ao;
        else ++ai;
    }
    if (aiOut) *aiOut = ai;
    if (aoOut) *aoOut = ao;
}

/** @brief 显示槽位 → 全局点下标（section: 0=AI 段、1=AO 段；slot 段内序号）。
 *  @details 段内序号按全局下标升序（与点数 API 的下标语义一致）。 */
static int analog_slotToIndex(int section, int slot)
{
    int i;
    int seen = 0;
    for (i = 0; i < s_analog.m_count; ++i) {
        bool isOut = (s_analog.m_points[i].m_kind == HSF_ANALOG_OUTPUT);
        if ((section == 1) != isOut) continue;
        if (seen == slot) return i;
        ++seen;
    }
    return -1;
}

/** @brief 全局点下标 → 显示槽位（出参 section/slot；不属于任何段返回 false）。 */
static bool analog_indexToSlot(int index, int* sectionOut, int* slotOut)
{
    int i;
    int seen = 0;
    if (index < 0 || index >= s_analog.m_count) return false;
    {
        bool isOut = (s_analog.m_points[index].m_kind == HSF_ANALOG_OUTPUT);
        for (i = 0; i <= index; ++i) {
            bool iOut = (s_analog.m_points[i].m_kind == HSF_ANALOG_OUTPUT);
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

static void analog_board_metrics(const HsfAnalogBoard* board, int width,
                                 int* colsOut, int* cardWOut,
                                 int* cardHOut, int* rowsOut)
{
    int cols;
    int cardW;
    int cardH;
    int avail;
    int minW;
    XFont font;
    int textH;
    if (colsOut) *colsOut = 1;
    if (cardWOut) *cardWOut = ANALOG_COL_MIN_W;
    if (cardHOut) *cardHOut = ANALOG_CARD_H_MIN;
    if (rowsOut) *rowsOut = 0;
    if (!board || width <= ANALOG_BOARD_MARGIN * 2) return;
    minW = s_analog.m_compact ? ANALOG_COL_MIN_W_C : ANALOG_COL_MIN_W;
    avail = width - ANALOG_BOARD_MARGIN * 2;
    if (s_analog.m_columns > 0) {
        cols = s_analog.m_columns; /* 强制列数（1..8 钳口在 API）。 */
        if (cols > 8) cols = 8;
    } else {
        cols = (avail + ANALOG_GAP) / (minW + ANALOG_GAP);
        if (cols < 1) cols = 1;
    }
    cardW = (avail - (cols - 1) * ANALOG_GAP) / cols;
    if (cardW < minW) cardW = minW;
    font = XWidget_font((XWidget*)board);
    textH = XPainter_textHeight(&font);
    XClassDeinit(&font);
    /* 三行文本（名称/原始值/转换值）+ 行距 + 上下内边距。 */
    cardH = 6 + textH + 4 + textH + 4 + textH + 4 + 8;
    if (cardH < ANALOG_CARD_H_MIN) cardH = ANALOG_CARD_H_MIN;
    if (s_analog.m_compact && cardH < ANALOG_TOUCH_H_MIN)
        cardH = ANALOG_TOUCH_H_MIN;
    if (colsOut) *colsOut = cols;
    if (cardWOut) *cardWOut = cardW;
    if (cardHOut) *cardHOut = cardH;
}

/** @brief 区段标题行高前置声明（metrics2 引用，定义在其后）。 */
static void analog_titleHeight(const HsfAnalogBoard* board, int* hOut);

/** @brief 区段制整版几何：两段各自的行数/起 Y + 标题行高（paint/hit/hint
 *         共用；与数字量页 metrics2 同构）。
 *  @details 版面自上而下：AI 段标题行（有 AI 点才有）→ AI 网格 → 段间距
 *           → AO 段标题行（有 AO 点才有）→ AO 网格。段内 ceil 除法成行。 */
static void analog_board_metrics2(const HsfAnalogBoard* board, int width,
                                  int* colsOut, int* cardWOut,
                                  int* cardHOut, int* titleHOut,
                                  int* aiYOut, int* aiRowsOut,
                                  int* aoYOut, int* aoRowsOut)
{
    int cols;
    int cardW;
    int cardH;
    int titleH;
    int aiCount;
    int aoCount;
    int aiRows;
    int aoRows;
    int y;
    analog_board_metrics(board, width, &cols, &cardW, &cardH, NULL);
    analog_titleHeight(board, &titleH);
    analog_countKinds(&aiCount, &aoCount);
    aiRows = (cols > 0) ? (aiCount + cols - 1) / cols : 0;
    aoRows = (cols > 0) ? (aoCount + cols - 1) / cols : 0;
    y = ANALOG_BOARD_MARGIN;
    if (aiCount > 0) {
        if (aiYOut) *aiYOut = y;
        y += titleH + 4 + aiRows * (cardH + ANALOG_GAP) - ANALOG_GAP;
        y += ANALOG_SECTION_GAP;
    } else if (aiYOut) {
        *aiYOut = -1; /* 空段隐藏。 */
    }
    if (aoCount > 0) {
        if (aoYOut) *aoYOut = y;
    } else if (aoYOut) {
        *aoYOut = -1;
    }
    if (aiRowsOut) *aiRowsOut = aiRows;
    if (aoRowsOut) *aoRowsOut = aoRows;
    if (colsOut) *colsOut = cols;
    if (cardWOut) *cardWOut = cardW;
    if (cardHOut) *cardHOut = cardH;
    if (titleHOut) *titleHOut = titleH;
}

/** @brief 区段标题行高（看板字体一行文本高 + 4px 余量）。 */
static void analog_titleHeight(const HsfAnalogBoard* board, int* hOut)
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

/** @brief 整版高度（sizeHint/minimumSizeHint 共用；给定宽反解，区段制）。 */
static int analog_boardHeightFor(const HsfAnalogBoard* board, int width)
{
    int cols;
    int cardW;
    int cardH;
    int titleH;
    int aiY;
    int aiRows;
    int aoY;
    int aoRows;
    int aiCount;
    int aoCount;
    int height;
    if (s_analog.m_count <= 0)
        return ANALOG_BOARD_MARGIN * 2 + ANALOG_CARD_H_MIN;
    analog_board_metrics2(board, width, &cols, &cardW, &cardH, &titleH,
                          &aiY, &aiRows, &aoY, &aoRows);
    analog_countKinds(&aiCount, &aoCount);
    height = ANALOG_BOARD_MARGIN;
    if (aiY >= 0)
        height += titleH + 4 +
                  aiRows * (cardH + ANALOG_GAP) - ANALOG_GAP +
                  ANALOG_SECTION_GAP;
    if (aoY >= 0)
        height += titleH + 4 +
                  aoRows * (cardH + ANALOG_GAP) - ANALOG_GAP;
    height += ANALOG_BOARD_MARGIN - ANALOG_SECTION_GAP;
    if (height < ANALOG_BOARD_MARGIN * 2 + ANALOG_CARD_H_MIN)
        height = ANALOG_BOARD_MARGIN * 2 + ANALOG_CARD_H_MIN;
    return height;
}

/** @brief 取点位显示名（buf 恒 NUL 收尾；无名称用缺省「点 N」）。
 *  @details 已设通道号（>=0）时加「CH<k> 」前缀（显示在名称前）。 */
static void analog_displayName(int index, char* buf, int size)
{
    const char* base;
    if (!buf || size <= 0) return;
    base = (index >= 0 && index < s_analog.m_count &&
            s_analog.m_points[index].m_name &&
            s_analog.m_points[index].m_name[0])
               ? s_analog.m_points[index].m_name : NULL;
    if (index >= 0 && index < s_analog.m_count &&
        s_analog.m_points[index].m_channel >= 0) {
        snprintf(buf, (size_t)size, "CH%d %s",
                 s_analog.m_points[index].m_channel,
                 base ? base : "");
        return;
    }
    if (base) {
        snprintf(buf, (size_t)size, "%s", base);
        return;
    }
    snprintf(buf, (size_t)size, "\xE7\x82\xB9 %d", index + 1); /* 点 N */
}

/** @brief 转换值格式化（按小数位；buf 恒 NUL 收尾）。 */
static void analog_formatScaled(int index, char* buf, int size)
{
    int decimals;
    if (!buf || size <= 0) return;
    decimals = (index >= 0 && index < s_analog.m_count)
                   ? s_analog.m_points[index].m_decimals : 2;
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;
    snprintf(buf, (size_t)size, "%.*f", decimals,
             (index >= 0 && index < s_analog.m_count)
                 ? s_analog.m_points[index].m_scaled : 0.0);
}

/** @brief 转换值是否越限（未设量程恒 false）。 */
static bool analog_isAlarmed(int index)
{
    if (index < 0 || index >= s_analog.m_count) return false;
    if (!s_analog.m_points[index].m_hasRange) return false;
    return s_analog.m_points[index].m_scaled < s_analog.m_points[index].m_min ||
           s_analog.m_points[index].m_scaled > s_analog.m_points[index].m_max;
}

/** @brief 状态栏反馈（未挂接时静默）。 */
static void analog_setStatus(const char* text)
{
    if (s_analog.m_setStatus)
        s_analog.m_setStatus(s_analog.m_statusUser, text);
}

/* ==================== AO 点编辑对话框（原始值/转换值二选一 + 互转） ==================== */

/**
 * @brief      AO 点编辑对话框（模态）。
 * @details    布局：点标题行 → 两张单选（改原始值 / 改转换值；缺省选
 *             转换值——工程量更直观）→ 数字编辑框（XLineEdit，
 *             XInputMethodHint_DigitsOnly → 聚焦自动弹屏幕数字键盘）→
 *             确定/取消（XDialogButtonBox）。互转规则：换算比保持——
 *             ratio = scaled/raw（raw≠0 时）；改原始值 → 转换值=新原始
 *             ×ratio；改转换值 → 原始值=新转换/ratio（四舍五入取整）。
 *             raw==0 时 ratio 未定义，锁死「改原始值」选项（只允许改
 *             转换值，原始值保持 0 不反推）。
 * @param parent 父控件（页根；对话框模态挂其上）。
 * @param index AO 点下标（调用方已保证合法）。
 */
static void analog_showEditDialog(XWidget* parent, int index);

/** @brief 对话框确认槽上下文（模态 exec 期间存活；静态 simplest）。 */
static struct
{
    XDialog*      m_dlg;      /**< 对话框借用。 */
    XLineEdit*    m_edit;     /**< 数值编辑框借用。 */
    XRadioButton* m_editRaw;  /**< 「改原始值」单选借用。 */
    XRadioButton* m_editScaled; /**< 「改转换值」单选借用。 */
    int           m_index;    /**< 编辑中的点下标。 */
} s_editCtx;

/** @brief 对话框确定槽：按所选目标写值 + 换算比互转 + 关闭。 */
static void analog_editAcceptSlot(XObject* receiver, XVarList* args)
{
    const char* text;
    double ratio = 1.0;
    bool canConvert;
    int raw;
    double scaled;
    (void)receiver; (void)args;
    if (!s_editCtx.m_dlg || !s_editCtx.m_edit) return;
    text = XLineEdit_text(s_editCtx.m_edit);
    if (!text || !text[0]) {
        XDialog_done(s_editCtx.m_dlg, 0); /* 空输入=取消。 */
        return;
    }
    raw = s_analog.m_points[s_editCtx.m_index].m_raw;
    scaled = s_analog.m_points[s_editCtx.m_index].m_scaled;
    canConvert = (raw != 0);
    if (canConvert) ratio = scaled / (double)raw;
    if (s_editCtx.m_editRaw && XRadioButton_isChecked(s_editCtx.m_editRaw) &&
        canConvert) {
        /* 改原始值：转换值=新原始 × ratio（换算比保持）。 */
        int newRaw = atoi(text);
        s_analog.m_points[s_editCtx.m_index].m_raw = newRaw;
        s_analog.m_points[s_editCtx.m_index].m_scaled =
            (double)newRaw * ratio;
    } else if (s_editCtx.m_editRaw &&
               XRadioButton_isChecked(s_editCtx.m_editRaw) && !canConvert) {
        /* raw==0：锁死改原始（前置守卫已置灰），理论不可达；保底只回显。 */
        analog_setStatus(
            "\xE5\x8E\x9F\xE5\xA7\x8B\xE5\x80\xBC\xE4\xB8\xBA\x30\x20\xE4"
            "\xB8\x8D\xE8\x83\xBD\xE6\x8D\xA2\xE7\xAE\x97"); /* 原始值为0 不能换算 */
    } else {
        /* 改转换值：原始值=新转换 / ratio（换算比保持；raw==0 保持 0）。 */
        double newScaled = atof(text);
        s_analog.m_points[s_editCtx.m_index].m_scaled = newScaled;
        if (canConvert)
            s_analog.m_points[s_editCtx.m_index].m_raw =
                (int)(newScaled / ratio + (newScaled >= 0 ? 0.5 : -0.5));
    }
    if (s_analog.m_board)
        XWidget_update((XWidget*)s_analog.m_board);
    /* 业务附加通知（tapFn 语义：AO 写请求已提交）。 */
    if (s_analog.m_tapFn)
        s_analog.m_tapFn(s_analog.m_tapUser, s_editCtx.m_index);
    XDialog_done(s_editCtx.m_dlg, 1);
}

/** @brief 对话框取消槽：直接关闭（不改值）。 */
static void analog_editRejectSlot(XObject* receiver, XVarList* args)
{
    (void)receiver; (void)args;
    if (s_editCtx.m_dlg)
        XDialog_done(s_editCtx.m_dlg, 0);
}

/** @brief AO 点编辑对话框实现（见前置声明注释）。 */
static void analog_showEditDialog(XWidget* parent, int index)
{
    static const char* kTitle = "\xE4\xBF\xAE\xE6\x94\xB9\xE6\xA8\xA1\xE6"
        "\x8B\x9F\xE9\x87\x8F"; /* 修改模拟量 */
    static const char* kLabelRaw = "\xE6\x94\xB9\xE5\x8E\x9F\xE5\xA7\x8B"
        "\xE5\x80\xBC"; /* 改原始值 */
    static const char* kLabelScaled = "\xE6\x94\xB9\xE8\xBD\xAC\xE6\x8D"
        "\xA2\xE5\x80\xBC"; /* 改转换值 */
    static const char* kOk = "\xE7\xA1\xAE\xE5\xAE\x9A";   /* 确定 */
    static const char* kCancel = "\xE5\x8F\x96\xE6\xB6\x88"; /* 取消 */
    XDialog* dlg;
    XBoxLayout* page;
    XGridLayout* grid;
    XLabel* caption;
    XLabel* capRaw;
    XLabel* capScaled;
    XLineEdit* edit;
    XRadioButton* rbRaw;
    XRadioButton* rbScaled;
    XDialogButtonBox* box;
    char name[ANALOG_NAME_MAX];
    char buf[ANALOG_NAME_MAX + 48];
    bool canConvert;
    if (!parent || index < 0 || index >= s_analog.m_count) return;
    dlg = XDialog_create(parent, 0);
    if (!dlg) return;
    memset(&s_editCtx, 0, sizeof(s_editCtx));
    s_editCtx.m_dlg = dlg;
    s_editCtx.m_index = index;
    canConvert = (s_analog.m_points[index].m_raw != 0);

    /* 页根垂直布局。 */
    page = (XBoxLayout*)XMemory_malloc(sizeof(XBoxLayout),
                                       XCLASS_DEFAULT_MEMORY_TYPE);
    XBoxLayout_init(page, XBoxLayoutDirection_TopToBottom);
    XLayout_setContentsMargins((XLayout*)page, 16, 12, 16, 12);
    XLayout_setSpacing((XLayout*)page, 8);
    XWidget_setLayout((XWidget*)dlg, (XLayout*)page);

    /* 标题行：点名称。 */
    analog_displayName(index, name, (int)sizeof(name));
    caption = XLabel_create((XWidget*)dlg, 0);
    if (caption) {
        HsfTheme_applyDefaultFont((XWidget*)caption);
        snprintf(buf, sizeof(buf), "%s", name);
        XLabel_setText_2(caption, buf);
        XLabel_setTextPixelSize(caption, 15);
        XWidget_setFixedHeight((XWidget*)caption, 24);
        XWidget_show((XWidget*)caption);
        XBoxLayout_addWidget(page, (XWidget*)caption);
    }

    /* 二选一：改原始值 / 改转换值（raw==0 时锁死改原始）。 */
    grid = (XGridLayout*)XMemory_malloc(sizeof(XGridLayout),
                                        XCLASS_DEFAULT_MEMORY_TYPE);
    XGridLayout_init(grid);
    XLayout_setSpacing((XLayout*)grid, 8);
    rbRaw = XRadioButton_create((XWidget*)dlg, 0);
    if (rbRaw) {
        HsfTheme_applyDefaultFont((XWidget*)rbRaw);
        XAbstractButton_setText_2((XAbstractButton*)rbRaw, kLabelRaw);
        XRadioButton_setChecked(rbRaw, false);
        if (!canConvert)
            XWidget_setEnabled((XWidget*)rbRaw, false); /* raw==0 锁死。 */
        XWidget_show((XWidget*)rbRaw);
    }
    capRaw = XLabel_create((XWidget*)dlg, 0);
    if (capRaw) {
        HsfTheme_applyDefaultFont((XWidget*)capRaw);
        snprintf(buf, sizeof(buf), "\xE5\xBD\x93\xE5\x89\x8D: %d",
                 s_analog.m_points[index].m_raw); /* 当前: N */
        XLabel_setText_2(capRaw, buf);
        XWidget_setFixedHeight((XWidget*)capRaw, 20);
        XWidget_show((XWidget*)capRaw);
    }
    rbScaled = XRadioButton_create((XWidget*)dlg, 0);
    if (rbScaled) {
        HsfTheme_applyDefaultFont((XWidget*)rbScaled);
        XAbstractButton_setText_2((XAbstractButton*)rbScaled, kLabelScaled);
        XRadioButton_setChecked(rbScaled, true); /* 缺省改转换值。 */
        XWidget_show((XWidget*)rbScaled);
    }
    capScaled = XLabel_create((XWidget*)dlg, 0);
    if (capScaled) {
        char val[32];
        HsfTheme_applyDefaultFont((XWidget*)capScaled);
        analog_formatScaled(index, val, (int)sizeof(val));
        snprintf(buf, sizeof(buf), "\xE5\xBD\x93\xE5\x89\x8D: %s",
                 val); /* 当前: X */
        XLabel_setText_2(capScaled, buf);
        XWidget_setFixedHeight((XWidget*)capScaled, 20);
        XWidget_show((XWidget*)capScaled);
    }
    XGridLayout_addWidget(grid, (XWidget*)rbRaw, 0, 0, 0);
    XGridLayout_addWidget(grid, (XWidget*)capRaw, 0, 1, 0);
    XGridLayout_addWidget(grid, (XWidget*)rbScaled, 1, 0, 0);
    XGridLayout_addWidget(grid, (XWidget*)capScaled, 1, 1, 0);
    XBoxLayout_addLayout(page, (XLayout*)grid);

    /* 数值编辑框（DigitsOnly hints → 聚焦自动弹屏幕数字键盘）。 */
    edit = XLineEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)dlg, 0);
    if (edit) {
        s_editCtx.m_edit = edit;
        HsfTheme_applyDefaultFont((XWidget*)edit);
        XWidget_setFixedHeight((XWidget*)edit, 32);
        XWidget_setInputMethodHints((XWidget*)edit,
                                    XInputMethodHint_DigitsOnly);
        /* 预填当前目标值（转换值口径；切换单选时不换字——简化，按确认
         * 时所选目标解释）。 */
        analog_formatScaled(index, buf, (int)sizeof(buf));
        XLineEdit_setText(edit, buf);
        XWidget_show((XWidget*)edit);
        XBoxLayout_addWidget(page, (XWidget*)edit);
    }
    s_editCtx.m_editRaw = rbRaw;
    s_editCtx.m_editScaled = rbScaled;

    /* 确定/取消。 */
    box = XDialogButtonBox_create((XWidget*)dlg, 0);
    if (box) {
        XAbstractButton* ok;
        XAbstractButton* cancel;
        ok = (XAbstractButton*)XDialogButtonBox_addButton_2(box, kOk,
            XDialogButtonBoxRole_AcceptRole);
        cancel = (XAbstractButton*)XDialogButtonBox_addButton_2(box, kCancel,
            XDialogButtonBoxRole_RejectRole);
        (void)ok; (void)cancel;
        XObject_connect_1((XObject*)box,
                          XSignal(XDialogButtonBox_accepted_signal),
                          (XObject*)dlg, analog_editAcceptSlot,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)box,
                          XSignal(XDialogButtonBox_rejected_signal),
                          (XObject*)dlg, analog_editRejectSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)box);
        XBoxLayout_addWidget(page, (XWidget*)box);
    }

    XWidget_setFixedSize((XWidget*)dlg, 360, 240);
    XDialog_exec(dlg);
    /* exec 返回后对话框生命周期由父子链回收（挂 parent 级联释放）。 */
    memset(&s_editCtx, 0, sizeof(s_editCtx));
}

/**
 * @brief      点击命中处理：交互模式 + 闸放行 + 有出口才出点击回调；
 *             其余情形只做状态栏提示。
 * @return     true=已交业务回调；false=被拦。
 */
static bool analog_tryTap(int index)
{
    if (index < 0 || index >= s_analog.m_count) return false;
    if (s_analog.m_autoMode) {
        analog_setStatus(
            "\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD"
            "\x3A\x20\xE7\x95\x8C\xE9\x9D\xA2\xE5\x8F\xAA\xE8\xAF\xBB");
        /* 自动运行中: 界面只读 */
        return false;
    }
    if (!s_analog.m_opEnabled) {
        analog_setStatus(
            "\xE6\x93\x8D\xE4\xBD\x9C\xE5\xB7\xB2\xE7\xA6\x81\xE6\xAD\xA2"
            "\xEF\xBC\x88\xE7\x95\x8C\xE9\x9D\xA2\xE5\x8F\xAA\xE8\xAF\xBB"
            "\xEF\xBC\x89"); /* 操作已禁止（界面只读） */
        return false;
    }
    /* AI 点恒只读（提示可查看语义）；AO 点弹编辑对话框（改原始值/
     * 转换值二选一，换算比保持互转）。 */
    if (s_analog.m_points[index].m_kind != HSF_ANALOG_OUTPUT) {
        analog_setStatus(
            "\xE8\xBE\x93\xE5\x85\xA5\xE7\x82\xB9\xE5\x8F\xAA\xE8\xAF\xBB"
            "\x28\x41\x49\x29"); /* 输入点只读(AI) */
        return false;
    }
    analog_showEditDialog(s_analog.m_board ? &s_analog.m_board->m_base
                                           : NULL, index);
    return true;
}

/** @brief 绘制单卡：底/描边 → 名称 → 原始值 → 转换值+单位。 */
static void analog_drawCard(XPainter* painter, const HsfAnalogBoard* board,
                            int index, int x, int y, int w, int h,
                            int textH)
{
    char name[ANALOG_NAME_MAX];
    char shown[ANALOG_NAME_MAX];
    char value[32];
    char unit[ANALOG_NAME_MAX];
    char line[ANALOG_NAME_MAX + 40];
    uint32_t valueColor = analog_isAlarmed(index) ? ANALOG_COLOR_ALARM
                                                  : ANALOG_COLOR_VALUE;
    /* 卡片底 + 细描边（按压中的卡描边加重反馈）。 */
    {
        XRect card;
        XRect_init(&card, x, y, w, h);
        XPainter_setPenWidth(painter, board->m_pressedIndex == index ? 2 : 1);
        XPainter_setPen(painter, board->m_pressedIndex == index
                                     ? ANALOG_COLOR_VALUE
                                     : ANALOG_COLOR_CARD_ED);
        XPainter_setBrush(painter, ANALOG_COLOR_CARD_BG);
        XPainter_drawRect(painter, &card);
    }
    /* 第一行：通道号+名称（左对齐，顶部）；AI/AO 角标（右上角，AI 灰
     * / AO 品牌蓝——可否操作一眼可辨）。名称超宽经 elideText 右省略
     * （对标 Qt elidedText，码点边界切割）——DONT_CLIP 溢出绘制会把
     * 长名顶进角标区（旧缺陷），此处名称恒不越过角标保留带。 */
    analog_displayName(index, name, (int)sizeof(name));
    {
        XRect r;
        const XFont* font = XPainter_font(painter);
        bool isAo = (index >= 0 && index < s_analog.m_count &&
                     s_analog.m_points[index].m_kind == HSF_ANALOG_OUTPUT);
        XRect_init(&r, x + 8, y + 6, w - 16 - 28 - ANALOG_BADGE_GAP, textH);
        XPainter_drawTextRect(painter, &r,
                              XPAINTER_TEXT_ALIGN_LEFT |
                                  XPAINTER_TEXT_ALIGN_TOP |
                                  XPAINTER_TEXT_DONT_CLIP,
                              XCommonStyle_elideText(shown, (int)sizeof(shown),
                                                     name, 1, r.width, font),
                              ANALOG_COLOR_TEXT);
        XRect_init(&r, x + w - 34, y + 6, 26, textH);
        XPainter_drawTextRect(painter, &r,
                              XPAINTER_TEXT_ALIGN_RIGHT |
                                  XPAINTER_TEXT_ALIGN_TOP |
                                  XPAINTER_TEXT_DONT_CLIP,
                              isAo ? "AO" : "AI",
                              isAo ? ANALOG_COLOR_VALUE
                                   : ANALOG_COLOR_UNIT);
    }
    /* 第二行：原始值（弱灰，整型口径）。 */
    if (index >= 0 && index < s_analog.m_count) {
        XRect r;
        snprintf(line, sizeof(line), "\xE5\x8E\x9F\xE5\xA7\x8B: %d",
                 s_analog.m_points[index].m_raw); /* 原始: N */
        XRect_init(&r, x + 8, y + 10 + textH, w - 16, textH);
        XPainter_drawTextRect(painter, &r,
                              XPAINTER_TEXT_ALIGN_LEFT |
                                  XPAINTER_TEXT_ALIGN_TOP |
                                  XPAINTER_TEXT_DONT_CLIP,
                              line, ANALOG_COLOR_RAW);
    }
    /* 第三行：转换值（品牌蓝/报警红）+ 单位（弱灰跟随）。 */
    analog_formatScaled(index, value, (int)sizeof(value));
    unit[0] = '\0';
    if (index >= 0 && index < s_analog.m_count &&
        s_analog.m_points[index].m_unit && s_analog.m_points[index].m_unit[0])
        snprintf(unit, (int)sizeof(unit), " %s",
                 s_analog.m_points[index].m_unit);
    snprintf(line, sizeof(line), "%s%s", value, unit);
    {
        XRect r;
        XRect_init(&r, x + 8, y + 14 + textH * 2, w - 16, textH + 4);
        XPainter_drawTextRect(painter, &r,
                              XPAINTER_TEXT_ALIGN_LEFT |
                                  XPAINTER_TEXT_ALIGN_TOP |
                                  XPAINTER_TEXT_DONT_CLIP,
                              line, valueColor);
    }
}

/** @brief 区段标题绘制前置声明（paintEvent 引用，定义在其后）。 */
static void analog_drawSectionTitle(XPainter* painter, int y, int width,
                                    int titleH, bool isAo, int count);

/** @brief 绘制事件：清背景 → AI 段（标题+网格）→ AO 段（标题+网格）。 */
static void VAnalogBoard_paintEvent(XWidget* self, XEvent* event)
{
    HsfAnalogBoard* board = (HsfAnalogBoard*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;
    XFont font;
    int w;
    int h;
    int cols;
    int cardW;
    int cardH;
    int titleH;
    int aiY;
    int aiRows;
    int aoY;
    int aoRows;
    int aiCount2;
    int aoCount2;
    int textH;
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
    analog_board_metrics2(board, w, &cols, &cardW, &cardH, &titleH,
                          &aiY, &aiRows, &aoY, &aoRows);
    /* AI 段（输入区，上段）。 */
    if (aiY >= 0) {
        int y = aiY + titleH + 4;
        int aiCount = 0;
        int slot;
        analog_countKinds(&aiCount, &aoCount2);
        analog_drawSectionTitle(&painter, aiY, w, titleH, false, aiCount);
        for (slot = 0; slot < aiCount; ++slot) {
            int row = slot / cols;
            int col = slot % cols;
            i = analog_slotToIndex(0, slot);
            if (i < 0) continue;
            analog_drawCard(&painter, board, i,
                            ANALOG_BOARD_MARGIN + col * (cardW + ANALOG_GAP),
                            y + row * (cardH + ANALOG_GAP),
                            cardW, cardH, textH);
        }
    }
    /* AO 段（输出区，下段）。 */
    if (aoY >= 0) {
        int y = aoY + titleH + 4;
        int aoCount = 0;
        int slot;
        analog_countKinds(&aiCount2, &aoCount);
        analog_drawSectionTitle(&painter, aoY, w, titleH, true, aoCount);
        for (slot = 0; slot < aoCount; ++slot) {
            int row = slot / cols;
            int col = slot % cols;
            i = analog_slotToIndex(1, slot);
            if (i < 0) continue;
            analog_drawCard(&painter, board, i,
                            ANALOG_BOARD_MARGIN + col * (cardW + ANALOG_GAP),
                            y + row * (cardH + ANALOG_GAP),
                            cardW, cardH, textH);
        }
    }
    /* 空态提示（两段全空）。 */
    if (s_analog.m_count <= 0) {
        XRect textRect;
        XRect_init(&textRect, 0, 0, w, h);
        XPainter_drawTextRect(&painter, &textRect,
                              XPAINTER_TEXT_ALIGN_HCENTER |
                                  XPAINTER_TEXT_ALIGN_VCENTER,
                              "\xE6\x97\xA0\xE6\xA8\xA1\xE6\x8B\x9F\xE9\x87"
                              "\x8F\xE7\x82\xB9", /* 无模拟量点 */
                              ANALOG_COLOR_HINT);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 区段标题绘制（「输入 (AI) N 点」/「输出 (AO) N 点」，左对齐）。 */
static void analog_drawSectionTitle(XPainter* painter, int y, int width,
                                    int titleH, bool isAo, int count)
{
    char text[96];
    XRect rect;
    snprintf(text, sizeof(text), "%s (%s) %d \xE7\x82\xB9",
             isAo ? "\xE8\xBE\x93\xE5\x87\xBA" : "\xE8\xBE\x93\xE5\x85\xA5",
             isAo ? "AO" : "AI", count);
    /* 输出 (AO) N 点 / 输入 (AI) N 点 */
    XRect_init(&rect, ANALOG_BOARD_MARGIN, y,
               width - ANALOG_BOARD_MARGIN * 2, titleH);
    XPainter_drawTextRect(painter, &rect,
                          XPAINTER_TEXT_ALIGN_LEFT |
                              XPAINTER_TEXT_ALIGN_VCENTER,
                          text, ANALOG_COLOR_TEXT);
}

/** @brief 命中测试：坐标 → 卡片下标（区段制：AI 上段/AO 下段各自反解，
 *         槽位→全局下标映射；矩形整卡命中）。 */
static int analog_hitCard(HsfAnalogBoard* board, int x, int y)
{
    int w = XWidget_width(&board->m_base);
    int cols;
    int cardW;
    int cardH;
    int titleH;
    int aiY;
    int aiRows;
    int aoY;
    int aoRows;
    int section;
    int secY;
    int row;
    int col;
    int slot;
    int index;
    analog_board_metrics2(board, w, &cols, &cardW, &cardH, &titleH,
                          &aiY, &aiRows, &aoY, &aoRows);
    for (section = 0; section < 2; ++section) {
        secY = (section == 0) ? aiY : aoY;
        if (secY < 0) continue;
        row = (y - secY - titleH - 4) / (cardH + ANALOG_GAP);
        col = (x - ANALOG_BOARD_MARGIN) / (cardW + ANALOG_GAP);
        if (row < 0 || col < 0 || col >= cols) continue;
        slot = row * cols + col;
        index = analog_slotToIndex(section, slot);
        if (index < 0) continue;
        return index; /* 矩形整卡语义（含卡内空隙外围容差=0）。 */
    }
    return -1;
}

/** @brief 鼠标按下：命中卡片 → 记按压态（按压描边反馈）。 */
static void VAnalogBoard_mousePressEvent(XWidget* self, XEvent* event)
{
    HsfAnalogBoard* board = (HsfAnalogBoard*)self;
    XMouseEvent* mouse;
    XPoint pos;
    int hit;
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
    hit = analog_hitCard(board, pos.x, pos.y);
    if (hit >= 0) {
        board->m_pressedIndex = hit;
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    XEvent_ignore(event);
}

/** @brief 鼠标释放：按压态中且松在同一卡 → 走模式/闸/回调链；清按压态。 */
static void VAnalogBoard_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    HsfAnalogBoard* board = (HsfAnalogBoard*)self;
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
    /* 释放仍须落在同一卡片（按下拖出=取消，按钮习惯）。 */
    if (analog_hitCard(board, pos.x, pos.y) == pressed)
        analog_tryTap(pressed);
    XEvent_accept(event);
}

/** @brief sizeHint 虚槽（查询时重算）：宽随当前板宽，高按内容行数。 */
static XSize VAnalogBoard_sizeHintSlot(const XWidget* self)
{
    XSize out;
    int width;
    if (!self) {
        XSize_init(&out, -1, -1);
        return out;
    }
    width = XWidget_width((XWidget*)self);
    XSize_init(&out, width,
               analog_boardHeightFor((const HsfAnalogBoard*)self, width));
    return out;
}

/** @brief 最小提示：宽给「一列」下限（滚动壳 widgetResizable 以此收窄
 *         内容，宽随视口收缩是 Qt 语义）；高按当前板宽反解——高度下限
 *         若按单列最坏情况给死，宽视口下内容会被假下限撑出一屏空白
 *         （滚动壳内容高取 max(视口, minHint, sizeHint)），随宽收敛同
 *         sizeHint 口径，窄视口由滚动壳两遍收敛兜底。 */
static XSize VAnalogBoard_minimumSizeHintSlot(const XWidget* self)
{
    XSize out;
    int minW;
    int curW;
    if (!self) {
        XSize_init(&out, -1, -1);
        return out;
    }
    minW = ANALOG_BOARD_MARGIN * 2 +
           (s_analog.m_compact ? ANALOG_COL_MIN_W_C : ANALOG_COL_MIN_W);
    curW = XWidget_width((XWidget*)self);
    if (curW < minW) curW = minW;
    XSize_init(&out, minW,
               analog_boardHeightFor((const HsfAnalogBoard*)self, curW));
    return out;
}

/** @brief 状态/点数/宽度变化统一走 update（轻量重绘）。 */
static void analog_boardRefresh(void)
{
    if (s_analog.m_board) {
        XWidget_update((XWidget*)s_analog.m_board);
        XWidget_updateGeometry((XWidget*)s_analog.m_board);
    }
}

/* ---- XCLASS 虚表：PaintEvent/Mouse/SizeHint 覆盖，其余继承 XWidget ---- */

#if XGUI_ON
static void VAnalogBoard_timerEvent(XObject* object, XTimerEvent* event);
static void analog_seedDemo(void); /* 演示数据（定义在 autotest 段）。 */
#endif /* XGUI_ON */

XVtable* HsfAnalogBoard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(HsfAnalogBoard)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VAnalogBoard_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VAnalogBoard_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VAnalogBoard_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VAnalogBoard_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint,
                             VAnalogBoard_minimumSizeHintSlot);
#if XGUI_ON
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VAnalogBoard_timerEvent);
#endif /* XGUI_ON */
    return XVTABLE_DEFAULT;
}

/* ==================== 页对象与装配 ==================== */

/** @brief 模拟量页对象；m_base 必须是第一个成员。
 *  @note   返回语义由左侧导航树/主菜单承担，页内不放返回钮。 */
typedef struct HsfMonitorAnalog
{
    XWidget       m_base;      /**< 页容器；必须是第一个成员。 */
    XBoxLayout    m_layout;    /**< 页内垂直布局。 */
    XBoxLayout    m_topRow;    /**< 顶行：标题居中 + 勾选框贴右（同一行）。 */
    XLabel        m_title;     /**< 页标题（粗）。 */
    XCheckBox     m_opCheck;   /**< 统一操作闸勾选框（界面开关；自动模式隐藏）。 */
    HsfAnalogBoard* m_board;   /**< 数值卡网格看板（堆；随页级联释放）。 */
    XLabel        m_hint;      /**< 底部提示。 */
} HsfMonitorAnalog;

/** @brief 操作闸勾选框槽：勾选状态即统一闸（唯一写入口在 API，UI 只映射）。 */
static void analog_opCheckSlot(XObject* receiver, XVarList* args)
{
    HsfMonitorAnalog* self = (HsfMonitorAnalog*)receiver;
    (void)args;
    if (!self) return;
    HsfMonitorAnalog_setOperationEnabled(
        XCheckBox_isChecked(&self->m_opCheck));
    analog_setStatus(s_analog.m_opEnabled
                         ? "\xE7\x95\x8C\xE9\x9D\xA2\xE6\x93\x8D\xE4\xBD"
                           "\x9C\xE5\xB7\xB2\xE5\x85\x81\xE8\xAE\xB8"
                         : "\xE7\x95\x8C\xE9\x9D\xA2\xE6\x93\x8D\xE4\xBD"
                           "\x9C\xE5\xB7\xB2\xE7\xA6\x81\xE6\xAD\xA2");
    /* 「界面已允许操作」/「界面已禁止操作」 */
}

/** @brief 形态自适应（横竖屏；幂等、廉价，HsfUi adapt 表按页调用）。
 *  @details 竖屏（compact）：列最小宽收窄（窄屏也能排 2~3 列）、卡片
 *           触控目标加高、页边距收窄；横屏恢复宽列。列数本就随视口宽
 *           实时反解（paint/hit/hint 同源），无需重排。 */
void HsfMonitorAnalog_adapt(XWidget* page, bool compact)
{
    HsfMonitorAnalog* self = (HsfMonitorAnalog*)page;
    if (!self) return;
    s_analog.m_compact = compact;
    XWidget_setMinimumHeight((XWidget*)self->m_board,
                             compact ? ANALOG_TOUCH_H_MIN
                                     : ANALOG_CARD_H_MIN + 32);
    XLayout_setContentsMargins((XLayout*)&self->m_layout,
                               compact ? 6 : 12, 10, compact ? 6 : 12, 12);
    /* 标题行恒横向（标题居中 + 勾选框贴右，横竖屏同构）；竖屏加高触控。 */
    XBoxLayout_setDirection(&self->m_topRow, XBoxLayoutDirection_LeftToRight);
    XWidget_setFixedHeight((XWidget*)&self->m_title, compact ? 30 : 26);
    XWidget_setFixedHeight((XWidget*)&self->m_opCheck, compact ? 30 : 22);
    analog_boardRefresh();
}

XWidget* HsfMonitorAnalog_create(XWidget* parent, HsfMenuNavFn navFn,
                                 void* userData)
{
    HsfMonitorAnalog* self;
    HsfAnalogBoard* board;
    (void)navFn;      /* 返回语义由导航树承担；签名随 builders 契约保留。 */
    (void)userData;
    if (!parent) return NULL;
    self = (HsfMonitorAnalog*)XMemory_malloc(sizeof(HsfMonitorAnalog),
                                             XCLASS_DEFAULT_MEMORY_TYPE);
    if (!self) return NULL;
    memset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, 0);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, true); /* 整块随主窗级联释放。 */

    HsfTheme_applyDefaultFont(&self->m_base);

    /* ---- 看板（自定义控件，先建后装配） ---- */
    board = (HsfAnalogBoard*)XMemory_malloc(sizeof(HsfAnalogBoard),
                                            XCLASS_DEFAULT_MEMORY_TYPE);
    if (!board) {
        XFree_System(self);
        return NULL;
    }
    memset(board, 0, sizeof(*board));
    XWidget_init(&board->m_base, &self->m_base, 0);
    XClassSetVtable(board, HsfAnalogBoard);
    Set_Class_Memory(board, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(board, true);
    board->m_pressedIndex = -1;
#if XGUI_ON
    board->m_tapTestId = XTIMER_INVALID_ID;
#endif /* XGUI_ON */
    s_analog.m_board = board;
    self->m_board = board;
    XWidget_show((XWidget*)board);
    XWidget_setMinimumHeight((XWidget*)board, ANALOG_CARD_H_MIN + 32);

    /* ---- 标题 + 操作闸勾选框（同一行：对称伸展标题居中、勾选框贴右） ---- */
    XLabel_init(&self->m_title, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_title);
    {
        XFont f = XWidget_font((XWidget*)&self->m_title);
        XFont_setBold(&f, true);
        XWidget_setFont((XWidget*)&self->m_title, &f);
        XClassDeinit(&f);
    }
    XLabel_setText_2(&self->m_title,
                     "\xE6\xA8\xA1\xE6\x8B\x9F\xE9\x87\x8F"); /* 模拟量 */
    XLabel_setTextPixelSize(&self->m_title, 17);
    XLabel_setAlignment(&self->m_title,
                        XAlignment_HCenter | XAlignment_VCenter);
    XWidget_show((XWidget*)&self->m_title);

    XCheckBox_init(&self->m_opCheck, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_opCheck);
    XCheckBox_setText_2(&self->m_opCheck,
                        "\xE5\x85\x81\xE8\xAE\xB8\xE7\x95\x8C\xE9\x9D\xA2"
                        "\xE6\x93\x8D\xE4\xBD\x9C"); /* 允许界面操作 */
    XCheckBox_setChecked(&self->m_opCheck, s_analog.m_opEnabled);
    XObject_connect_1((XObject*)&self->m_opCheck,
                      XSignal(XAbstractButton_toggled_signal),
                      (XObject*)self, analog_opCheckSlot,
                      XConnectionType_Direct);
    XWidget_show((XWidget*)&self->m_opCheck);
    s_analog.m_opCheck = &self->m_opCheck; /* 闸回显借用（页内成员）。 */
    /* 自动运行模式：勾选框隐藏、闸锁定只读（API 恢复后重显）。 */
    if (s_analog.m_autoMode)
        XWidget_hide((XWidget*)&self->m_opCheck);
    s_analog.m_pageBuilt = true;

    /* ---- 底部提示 ---- */
    XLabel_init(&self->m_hint, &self->m_base, 0);
    HsfTheme_applyDefaultFont((XWidget*)&self->m_hint);
    XLabel_setText_2(&self->m_hint,
                     "\xE6\xAF\x8F\xE5\x8D\xA1\x3A\x20\xE5\x8E\x9F\xE5\xA7"
                     "\x8B\xE5\x80\xBC\x20\x2B\x20\xE8\xBD\xAC\xE6\x8D\xA2"
                     "\xE5\x80\xBC\xEF\xBC\x88\xE8\xB6\x8A\xE9\x99\x90\xE6"
                     "\x8A\xA5\xE8\xAD\xA6\xE7\xBA\xA2\xEF\xBC\x89\x20\xE2"
                     "\x80\x94\x20\xE7\x82\xB9\xE5\x87\xBB\xE5\x8D\xA1\xE7"
                     "\x89\x87\xE6\x93\x8D\xE4\xBD\x9C\xEF\xBC\x88\xE9\x9C"
                     "\x80\xE5\x85\x81\xE8\xAE\xB8\xE7\x95\x8C\xE9\x9D\xA2"
                     "\xE6\x93\x8D\xE4\xBD\x9C\xEF\xBC\x89");
    /* 每卡: 原始值 + 转换值（越限报警红） — 点击卡片操作（需允许界面操作） */
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
     * 可看；业务层一句 setCount/setXxx 即可整体覆盖。 */
    if (s_analog.m_count <= 0)
        analog_seedDemo();

    XWidget_show(&self->m_base);
    return &self->m_base;
}

/* ==================== 公开 API（模型唯一写入口；页未建时安全） ==================== */

void HsfMonitorAnalog_setCount(int count)
{
    int i;
    HsfAnalogPoint* next;
    if (count < 0) count = 0;
    if (count == s_analog.m_count) return;
    next = NULL;
    if (count > 0) {
        next = (HsfAnalogPoint*)XMemory_malloc(
            (size_t)count * sizeof(HsfAnalogPoint),
            XCLASS_DEFAULT_MEMORY_TYPE);
        if (!next) return; /* 分配失败保持旧模型（安全降级）。 */
        memset(next, 0, (size_t)count * sizeof(HsfAnalogPoint));
        /* 共同前缀逐点迁移（深拷指针随结构体走；扩点新增 0 值卡）。 */
        for (i = 0; i < count && i < s_analog.m_count; ++i)
            next[i] = s_analog.m_points[i];
        /* 新增点缺省：小数位 2、类型 AI（只读监视）。 */
        for (i = s_analog.m_count; i < count; ++i) {
            next[i].m_decimals = 2;
            next[i].m_kind = HSF_ANALOG_INPUT;
        }
    }
    XFree_System(s_analog.m_points);
    s_analog.m_points = next;
    s_analog.m_count = count;
    analog_boardRefresh();
}

int HsfMonitorAnalog_count(void)
{
    return s_analog.m_count;
}

void HsfMonitorAnalog_setValue(int index, int raw, double scaled)
{
    HsfMonitorAnalog_setRaw(index, raw);
    HsfMonitorAnalog_setScaled(index, scaled);
}

void HsfMonitorAnalog_setRaw(int index, int raw)
{
    if (index < 0 || index >= s_analog.m_count) return;
    if (s_analog.m_points[index].m_raw == raw) return;
    s_analog.m_points[index].m_raw = raw;
    if (s_analog.m_board) XWidget_update((XWidget*)s_analog.m_board);
}

int HsfMonitorAnalog_raw(int index)
{
    if (index < 0 || index >= s_analog.m_count) return 0;
    return s_analog.m_points[index].m_raw;
}

void HsfMonitorAnalog_setScaled(int index, double scaled)
{
    if (index < 0 || index >= s_analog.m_count) return;
    if (s_analog.m_points[index].m_scaled == scaled) return;
    s_analog.m_points[index].m_scaled = scaled;
    if (s_analog.m_board) XWidget_update((XWidget*)s_analog.m_board);
}

double HsfMonitorAnalog_scaled(int index)
{
    if (index < 0 || index >= s_analog.m_count) return 0.0;
    return s_analog.m_points[index].m_scaled;
}

void HsfMonitorAnalog_setName(int index, const char* name)
{
    char* copy;
    if (index < 0 || index >= s_analog.m_count) return;
    copy = NULL;
    if (name && name[0]) {
        size_t bytes = strlen(name) + 1;
        copy = (char*)XMemory_malloc(bytes, XCLASS_DEFAULT_MEMORY_TYPE);
        if (!copy) return;
        memcpy(copy, name, bytes);
    }
    XFree_System(s_analog.m_points[index].m_name);
    s_analog.m_points[index].m_name = copy;
    if (s_analog.m_board) XWidget_update((XWidget*)s_analog.m_board);
}

void HsfMonitorAnalog_name(int index, char* buf, int size)
{
    if (!buf || size <= 0) return;
    buf[0] = '\0';
    if (index < 0 || index >= s_analog.m_count) {
        snprintf(buf, (size_t)size, "\xE7\x82\xB9 %d", index + 1);
        return;
    }
    analog_displayName(index, buf, size);
}

void HsfMonitorAnalog_setUnit(int index, const char* unit)
{
    char* copy;
    if (index < 0 || index >= s_analog.m_count) return;
    copy = NULL;
    if (unit && unit[0]) {
        size_t bytes = strlen(unit) + 1;
        copy = (char*)XMemory_malloc(bytes, XCLASS_DEFAULT_MEMORY_TYPE);
        if (!copy) return;
        memcpy(copy, unit, bytes);
    }
    XFree_System(s_analog.m_points[index].m_unit);
    s_analog.m_points[index].m_unit = copy;
    if (s_analog.m_board) XWidget_update((XWidget*)s_analog.m_board);
}

void HsfMonitorAnalog_unit(int index, char* buf, int size)
{
    if (!buf || size <= 0) return;
    buf[0] = '\0';
    if (index < 0 || index >= s_analog.m_count) return;
    if (s_analog.m_points[index].m_unit)
        snprintf(buf, (size_t)size, "%s", s_analog.m_points[index].m_unit);
}

void HsfMonitorAnalog_setChannel(int index, int channel)
{
    if (index < 0 || index >= s_analog.m_count) return;
    if (s_analog.m_points[index].m_channel == channel) return;
    s_analog.m_points[index].m_channel = channel;
    if (s_analog.m_board) XWidget_update((XWidget*)s_analog.m_board);
}

int HsfMonitorAnalog_channel(int index)
{
    if (index < 0 || index >= s_analog.m_count) return -1;
    return s_analog.m_points[index].m_channel;
}

void HsfMonitorAnalog_setDecimals(int index, int decimals)
{
    if (index < 0 || index >= s_analog.m_count) return;
    if (decimals < 0) decimals = 0;
    if (decimals > 6) decimals = 6;
    if (s_analog.m_points[index].m_decimals == decimals) return;
    s_analog.m_points[index].m_decimals = decimals;
    if (s_analog.m_board) XWidget_update((XWidget*)s_analog.m_board);
}

int HsfMonitorAnalog_decimals(int index)
{
    if (index < 0 || index >= s_analog.m_count) return 2;
    return s_analog.m_points[index].m_decimals;
}

void HsfMonitorAnalog_setRange(int index, double min, double max)
{
    HsfAnalogPoint* p;
    if (index < 0 || index >= s_analog.m_count) return;
    p = &s_analog.m_points[index];
    if (min >= max) {
        /* min>=max 视为不设限（清除报警判定）。 */
        if (!p->m_hasRange) return;
        p->m_hasRange = false;
    } else {
        p->m_min = min;
        p->m_max = max;
        p->m_hasRange = true;
    }
    if (s_analog.m_board) XWidget_update((XWidget*)s_analog.m_board);
}

bool HsfMonitorAnalog_range(int index, double* minOut, double* maxOut)
{
    if (index < 0 || index >= s_analog.m_count) return false;
    if (!s_analog.m_points[index].m_hasRange) return false;
    if (minOut) *minOut = s_analog.m_points[index].m_min;
    if (maxOut) *maxOut = s_analog.m_points[index].m_max;
    return true;
}

void HsfMonitorAnalog_setKind(int index, HsfAnalogKind kind)
{
    if (index < 0 || index >= s_analog.m_count) return;
    if (kind != HSF_ANALOG_INPUT && kind != HSF_ANALOG_OUTPUT) return;
    if (s_analog.m_points[index].m_kind == kind) return;
    s_analog.m_points[index].m_kind = kind;
    if (s_analog.m_board) XWidget_update((XWidget*)s_analog.m_board);
}

HsfAnalogKind HsfMonitorAnalog_kind(int index)
{
    if (index < 0 || index >= s_analog.m_count) return HSF_ANALOG_INPUT;
    return s_analog.m_points[index].m_kind;
}

void HsfMonitorAnalog_setColumns(int columns)
{
    int next = columns < 0 ? 0 : columns;
    if (s_analog.m_columns == next) return;
    s_analog.m_columns = next;
    analog_boardRefresh();
}

int HsfMonitorAnalog_columns(void)
{
    return s_analog.m_columns;
}

void HsfMonitorAnalog_setOperationEnabled(bool enabled)
{
    /* 自动运行模式：界面强制只读，API 也锁闸（唯一解锁口是
     * setInteractiveMode(false) 退出自动模式）。 */
    if (s_analog.m_autoMode) {
        if (!s_analog.m_opEnabled) return;
        s_analog.m_opEnabled = false;
        if (s_analog.m_opCheck)
            XCheckBox_setChecked(s_analog.m_opCheck, false);
        analog_boardRefresh();
        return;
    }
    if (s_analog.m_opEnabled == enabled) return;
    s_analog.m_opEnabled = enabled;
    /* UI 开关随模型态回显（对齐 Qt setChecked 语义：同值不发 toggled）。 */
    if (s_analog.m_opCheck)
        XCheckBox_setChecked(s_analog.m_opCheck, enabled);
    analog_boardRefresh();
}

bool HsfMonitorAnalog_operationEnabled(void)
{
    return s_analog.m_opEnabled;
}

void HsfMonitorAnalog_setInteractiveMode(bool interactive)
{
    if (s_analog.m_autoMode == (!interactive)) return;
    s_analog.m_autoMode = !interactive;
    if (interactive) {
        /* 退出自动模式：重显闸开关，闸值回到勾选框当前态。 */
        if (s_analog.m_opCheck) {
            s_analog.m_opEnabled =
                XCheckBox_isChecked(s_analog.m_opCheck);
            XWidget_show((XWidget*)s_analog.m_opCheck);
        }
    } else {
        /* 进入自动模式：隐藏闸开关 + 强制只读。 */
        s_analog.m_opEnabled = false;
        if (s_analog.m_opCheck)
            XCheckBox_setChecked(s_analog.m_opCheck, false);
        if (s_analog.m_opCheck)
            XWidget_hide((XWidget*)s_analog.m_opCheck);
        analog_setStatus(
            "\xE8\x87\xAA\xE5\x8A\xA8\xE8\xBF\x90\xE8\xA1\x8C\xE4\xB8\xAD"
            "\x3A\x20\xE7\x95\x8C\xE9\x9D\xA2\xE5\x8F\xAA\xE8\xAF\xBB");
        /* 自动运行中: 界面只读 */
    }
    analog_boardRefresh();
}

bool HsfMonitorAnalog_interactiveMode(void)
{
    return !s_analog.m_autoMode;
}

void HsfMonitorAnalog_setTapHandler(HsfAnalogTapFn handler, void* userData)
{
    s_analog.m_tapFn = handler;
    s_analog.m_tapUser = userData;
}

void HsfMonitorAnalog_attachStatus(void (*setStatus)(void*, const char*),
                                   void* user)
{
    s_analog.m_setStatus = setStatus;
    s_analog.m_statusUser = user;
}

#if XGUI_ON

/* ---- 演示数据 + 点击链路自测（autotest 专用） ---- */

/** @brief 灌 12 点演示数据（名称/通道号/单位/量程越限样例；create 空模型
 *         与 autotest 共用）。业务层接入后 setCount 即整体覆盖。 */
static void analog_seedDemo(void)
{
    static const char* kNames[12] = {
        "\xE6\xB3\xB5\xE5\x89\x8D\xE5\x8E\x8B\xE5\x8A\x9B",   /* 泵前压力 */
        "\xE6\xB3\xB5\xE5\x90\x8E\xE5\x8E\x8B\xE5\x8A\x9B",   /* 泵后压力 */
        "\xE8\xBF\x9B\xE6\x96\x99\xE6\xB8\xA9\xE5\xBA\xA6",   /* 进料温度 */
        "\xE5\x87\xBA\xE6\x96\x99\xE6\xB8\xA9\xE5\xBA\xA6",   /* 出料温度 */
        "\xE6\xB5\x81\xE9\x87\x8F",                           /* 流量 */
        "\xE6\xB6\xB2\xE4\xBD\x8D",                           /* 液位 */
        "\xE7\x94\xB5\xE6\x9C\xBA\xE7\x94\xB5\xE6\xB5\x81",   /* 电机电流 */
        "\xE7\x94\xB5\xE6\x9C\xBA\xE8\xBD\xAC\xE9\x80\x9F",   /* 电机转速 */
        "\xE7\x9C\x9F\xE7\xA9\xBA\xE5\xBA\xA6",               /* 真空度 */
        "\xE6\xB0\xB4\xE5\x88\x86",                           /* 水分 */
        "\xE9\x85\xB8\xE5\xBA\xA6",                           /* 酸度 */
        "\xE6\xB5\x93\xE5\xBA\xA6"                            /* 浓度 */
    };
    static const char* kUnits[12] = {
        "kPa", "kPa", "\xE2\x84\x83", "\xE2\x84\x83", /* kPa kPa ℃ ℃ */
        "L/min", "mm", "A", "rpm",
        "kPa", "%", "pH", "g/L"
    };
    static const int kChannels[12] = {
        301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312
    };
    static const int kDecimals[12] = { 1, 1, 1, 1, 0, 0, 2, 0, 1, 1, 2, 2 };
    int i;
    HsfMonitorAnalog_setCount(12);
    for (i = 0; i < 12; ++i) {
        int raw = 400 + i * 618;               /* 原始值演示（递增）。 */
        double scaled = (i + 1) * 2.75;        /* 转换值演示。 */
        HsfMonitorAnalog_setName(i, kNames[i]);
        HsfMonitorAnalog_setUnit(i, kUnits[i]);
        HsfMonitorAnalog_setChannel(i, kChannels[i]);
        HsfMonitorAnalog_setDecimals(i, kDecimals[i]);
        HsfMonitorAnalog_setValue(i, raw, scaled);
        /* AI/AO 混合演示：0/4/6/8=AO（可点弹编辑框），其余 AI（只读）。 */
        HsfMonitorAnalog_setKind(i,
            (i == 0 || i == 4 || i == 6 || i == 8) ? HSF_ANALOG_OUTPUT
                                                   : HSF_ANALOG_INPUT);
        /* 越限演示：2 个点设窄量程使其必越限（报警红）。 */
        if (i == 3) HsfMonitorAnalog_setRange(i, 0.0, 1.0);   /* 出料温度越上限。 */
        if (i == 10) HsfMonitorAnalog_setRange(i, 0.0, 0.5);  /* 酸度越上限。 */
    }
}

/* ---- 点击链路自测 ---- */

/** @brief autotest 点击回调：记录最近一次点击（下标）。 */
static int g_tapLastIndex = -1;
static int g_tapHits = 0;

static void analog_tapRecord(void* userData, int index)
{
    (void)userData;
    g_tapLastIndex = index;
    ++g_tapHits;
}

/** @brief 自测步进语义：0=点首卡（手动应回调/自动应拦）、1=点空白、
 *         2=收尾断言+状态栏汇报。 */
static void analog_tapSend(HsfAnalogBoard* board, int step)
{
    int cols;
    int cardW;
    int cardH;
    int titleH;
    int aiY;
    int aiRows;
    int aoY;
    int aoRows;
    int x;
    int y;
    XMouseEvent* me;
    XPoint pos;
    analog_board_metrics2(board, XWidget_width(&board->m_base), &cols,
                          &cardW, &cardH, &titleH, &aiY, &aiRows,
                          &aoY, &aoRows);
    if (step == 0) { /* AI 段首卡中心（自测把首点暂设 AI，见 autotest）。 */
        if (aiY < 0) return;
        x = ANALOG_BOARD_MARGIN + cardW / 2;
        y = aiY + titleH + 4 + cardH / 2;
    } else { /* 段间空白带（两段之间，必无卡片）。 */
        if (aiY < 0 || aoY < 0) return;
        x = ANALOG_BOARD_MARGIN + 2;
        y = aoY - ANALOG_SECTION_GAP / 2;
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
static void VAnalogBoard_timerEvent(XObject* object, XTimerEvent* event)
{
    HsfAnalogBoard* board = (HsfAnalogBoard*)object;
    if (!board || !event) return;
    if (board->m_tapTestId == XTIMER_INVALID_ID ||
        XTimerEvent_timerId(event) != board->m_tapTestId)
        return;
    if (board->m_tapStep <= 1) {
        int before = g_tapHits;
        /* 自测点首卡（演示数据下标 0=AO）：手动模式会弹模态编辑框——
         * 模态 exec 阻塞本定时器泵，故自测改在**演示灌数前**把首卡
         * 暂设 AI（拦截链可测、无模态）；自动模式全锁。 */
        bool autoMode = !HsfMonitorAnalog_interactiveMode();
        bool firstIsAi = (s_analog.m_count > 0 &&
                          s_analog.m_points[0].m_kind != HSF_ANALOG_OUTPUT);
        analog_tapSend(board, board->m_tapStep);
        if (board->m_tapStep == 0) {
            if (autoMode) {
                if (g_tapHits == before) {
                    analog_setStatus(
                        "TAP-TEST step0 card(auto-locked): PASS");
                } else {
                    analog_setStatus("TAP-TEST step0 card(auto-locked): FAIL");
                }
            } else if (firstIsAi) {
                /* AI 卡：恒只读，不应回调。 */
                if (g_tapHits == before) {
                    analog_setStatus("TAP-TEST step0 AI-locked: PASS");
                } else {
                    analog_setStatus("TAP-TEST step0 AI-locked: FAIL");
                    XPrintf("[analog-tap] step0 AI FAIL hits=%d\n",
                            g_tapHits);
                }
            } else {
                /* AO 卡（业务自测注入）：应回调一次。 */
                if (g_tapHits == before + 1 && g_tapLastIndex == 0) {
                    analog_setStatus("TAP-TEST step0 card: PASS");
                } else {
                    analog_setStatus("TAP-TEST step0 card: FAIL");
                    XPrintf("[analog-tap] step0 FAIL hits=%d idx=%d\n",
                            g_tapHits, g_tapLastIndex);
                }
            }
        } else { /* 空白：不应回调。 */
            if (g_tapHits == before) {
                analog_setStatus("TAP-TEST step1 blank: PASS");
            } else {
                analog_setStatus("TAP-TEST step1 blank: FAIL");
            }
        }
        ++board->m_tapStep;
        return;
    }
    /* 收尾：停泵、恢复首卡 AO（截图仍见 AO 角标）并汇报总判定。
     * 拦截链自测全程应 0 次回调（AI 锁 + 空白；自动模式亦 0）。 */
    XObject_killTimer((XObject*)board, board->m_tapTestId);
    board->m_tapTestId = XTIMER_INVALID_ID;
    HsfMonitorAnalog_setKind(0, HSF_ANALOG_OUTPUT);
    {
        int hits = g_tapHits;
        analog_setStatus(hits == 0 ? "TAP-TEST ALL PASS"
                                   : "TAP-TEST FAIL");
        XPrintf("[analog-tap] done hits=%d lastIdx=%d auto=%d\n",
                hits, g_tapLastIndex,
                HsfMonitorAnalog_interactiveMode() ? 0 : 1);
    }
}

int HsfMonitorAnalog_autotest(XWidget* page)
{
    HsfAnalogBoard* board;
    if (!XSystem_environment("XHSF_ANALOG_AUTOTEST")) return -1;
    if (!page) return -1;
    analog_seedDemo();
    HsfMonitorAnalog_setOperationEnabled(true);
    /* 点击链路自测：首卡暂设 AI（拦截链可测、无模态阻塞），自测收尾
     * 由 TimerEvent 泵恢复为 AO（截图仍见 AO 角标）。 */
    HsfMonitorAnalog_setKind(0, HSF_ANALOG_INPUT);
    board = s_analog.m_board;
    if (board) {
        HsfMonitorAnalog_setTapHandler(analog_tapRecord, NULL);
        g_tapHits = 0;
        g_tapLastIndex = -1;
        board->m_tapStep = 0;
        board->m_tapTestId = XObject_startTimer_ms(
            (XObject*)board, 300u, XTimerType_CoarseTimer);
    }
    /* 自动模式演示：XHSF_ANALOG_AUTO=1 时进自动运行态（勾选框隐藏+
     * 界面只读），点击自测期望 hits=0。 */
    if (XSystem_environment("XHSF_ANALOG_AUTO"))
        HsfMonitorAnalog_setInteractiveMode(false);
    return 12;
}
#endif /* XGUI_ON */

#endif /* XWIDGET_ON && XPUSHBUTTON_ON && XLABEL_ON */
