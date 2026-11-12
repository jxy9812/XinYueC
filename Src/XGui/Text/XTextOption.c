/**
 * @file       XTextOption.c
 * @brief      XTextOption 文本选项值类型实现（对标 Qt 6.8 QTextOption）。
 * @details    缺省值与 Qt 6.8.3 逐项对拍（本机 Qt 6.8.0 offscreen 探针实测，
 *             QTextOption raw：align=1/wrap=1/tab=-1/design=0/flags=0/dir=2；
 *             QTextOption(Qt::AlignLeft) raw2：dir=0=应用布局方向）：
 *             - 默认构造（qtextoption.cpp）：委托 QTextOption(Qt::AlignLeft)
 *               后把 direction 覆写为 Qt::LayoutDirectionAuto；
 *             - 单参构造（qtextoption.cpp QTextOption(Qt::Alignment)）：
 *               direction = QGuiApplication::layoutDirection()（无应用回落
 *               LTR，Qt 同口径）；
 *             - 制表位缺省：Tab 构造 position=80、type=LeftTab、delimiter=0；
 *             - setTabArray：整体替换为 LeftTab 制表位（qtextoption.cpp
 *               setTabArray 的转换循环同口径）。
 * @author     XinYueC 团队
 ******************************************************************************/
#if XGUI_ON

#include "XTextOption.h"
#include "XGuiConfig.h"
#include "XMemory.h"

#if XGUIAPPLICATION_ON
#include "XGuiApplication.h"
#endif

/* ==================== 内部工具 ==================== */

static double xto_abs(double value)
{
    return value < 0 ? -value : value;
}

/* 对标 Qt qFuzzyCompare(double,double)：
 * qAbs(p1 - p2) * 1000000000000. <= qMin(qAbs(p1), qAbs(p2))。 */
static bool xto_fuzzyEqualPosition(double a, double b)
{
    double diff = xto_abs(a - b);
    double lo = xto_abs(a);
    double right = xto_abs(b);
    if (right < lo) lo = right;
    return diff * 1000000000000.0 <= lo;
}

/* 对标 Tab 缺省构造：position(80)、type(LeftTab)、delimiter=QChar()。 */
static XTextOptionTab xto_defaultTab(void)
{
    XTextOptionTab tab;
    tab.position = 80.0;
    tab.type = (int)XTextOptionTab_Left;
    tab.delimiter = 0;
    return tab;
}

#if XGUIAPPLICATION_ON
static int xto_appLayoutDirection(void)
{
    return (int)XGuiApplication_layoutDirection();
}
#else
static int xto_appLayoutDirection(void)
{
    return 0; /* 无 GUI 应用层：按 LTR（Qt 的 QGuiApplication 缺省同口径）。 */
}
#endif

/* ==================== 构造族 ==================== */

void XTextOption_init(XTextOption* self)
{
    if (!self) return;
    self->m_alignment = (int)0x1; /* Qt::AlignLeft。 */
    self->m_wrapMode = (int)XTextOptionWrap_WordWrap;
    /* QTextOption() 在委托后把 direction 覆写为 LayoutDirectionAuto。 */
    self->m_textDirection = 2;
    self->m_flags = 0;
    self->m_useDesignMetrics = 0;
    self->m_tabStopDistance = -1.0;
    self->m_tabCount = 0;
    /* 定容制表位随构造清零：create/create_alignment 以栈上局部值返回并整值
     * 拷贝进 XTextDocument（AGENTS「栈上局部结构体先 memset」纪律——当前
     * 读取路径虽有 m_tabCount=0 门控，但死区垃圾日后变真读即是实锅）。 */
    XMemset(self->m_tabs, 0, sizeof(self->m_tabs));
}

void XTextOption_init_alignment(XTextOption* self, int alignment)
{
    if (!self) return;
    XTextOption_init(self);
    self->m_alignment = alignment;
    /* QTextOption(Qt::Alignment)：direction = QGuiApplication::layoutDirection()。 */
    self->m_textDirection = xto_appLayoutDirection();
}

XTextOption XTextOption_create(void)
{
    XTextOption option;
    XTextOption_init(&option);
    return option;
}

XTextOption XTextOption_create_alignment(int alignment)
{
    XTextOption option;
    XTextOption_init_alignment(&option, alignment);
    return option;
}

/* ==================== 标量属性 ==================== */

void XTextOption_setAlignment(XTextOption* self, int alignment)
{
    if (self) self->m_alignment = alignment;
}

int XTextOption_alignment(const XTextOption* self)
{
    return self ? self->m_alignment : (int)0x1;
}

void XTextOption_setTextDirection(XTextOption* self, int direction)
{
    if (self) self->m_textDirection = direction;
}

int XTextOption_textDirection(const XTextOption* self)
{
    return self ? self->m_textDirection : 2;
}

void XTextOption_setWrapMode(XTextOption* self, int wrapMode)
{
    if (self) self->m_wrapMode = wrapMode;
}

int XTextOption_wrapMode(const XTextOption* self)
{
    return self ? self->m_wrapMode : (int)XTextOptionWrap_WordWrap;
}

void XTextOption_setFlags(XTextOption* self, uint32_t flags)
{
    if (self) self->m_flags = flags;
}

uint32_t XTextOption_flags(const XTextOption* self)
{
    return self ? self->m_flags : 0u;
}

void XTextOption_setTabStopDistance(XTextOption* self, double distance)
{
    if (self) self->m_tabStopDistance = distance;
}

double XTextOption_tabStopDistance(const XTextOption* self)
{
    return self ? self->m_tabStopDistance : -1.0;
}

void XTextOption_setUseDesignMetrics(XTextOption* self, bool enable)
{
    if (self) self->m_useDesignMetrics = enable ? 1 : 0;
}

bool XTextOption_useDesignMetrics(const XTextOption* self)
{
    return self ? self->m_useDesignMetrics != 0 : false;
}

/* ==================== 制表位（定容有界子集） ==================== */

int XTextOption_setTabArray(XTextOption* self, const double* positions,
                            int count)
{
    XTextOptionTab tab = xto_defaultTab();
    int written = 0;
    int i;
    if (!self) return 0;
    if (count < 0) count = 0;
    if (!positions) count = 0; /* NULL 位置数组 = 清空（整体替换语义）。 */
    for (i = 0; i < count; ++i) {
        if (written >= XTEXTOPTION_MAX_TABS) break;
        tab.position = positions[i];
        self->m_tabs[written] = tab;
        ++written;
    }
    self->m_tabCount = written;
    return written;
}

int XTextOption_tabArray(const XTextOption* self, double* out, int capacity)
{
    int written = 0;
    int i;
    if (!self || !out || capacity <= 0) return 0;
    for (i = 0; i < self->m_tabCount && written < capacity; ++i) {
        out[written] = self->m_tabs[i].position;
        ++written;
    }
    return written;
}

int XTextOption_setTabs(XTextOption* self, const XTextOptionTab* tabs,
                        int count)
{
    int written = 0;
    int i;
    if (!self) return 0;
    if (count < 0) count = 0;
    if (!tabs) count = 0; /* NULL 列表 = 清空（整体替换语义）。 */
    for (i = 0; i < count; ++i) {
        if (written >= XTEXTOPTION_MAX_TABS) break;
        self->m_tabs[written] = tabs[i];
        ++written;
    }
    self->m_tabCount = written;
    return written;
}

int XTextOption_tabCount(const XTextOption* self)
{
    return self ? self->m_tabCount : 0;
}

bool XTextOption_tab(const XTextOption* self, int index, XTextOptionTab* out)
{
    if (!self || index < 0 || index >= self->m_tabCount) return false;
    if (out) *out = self->m_tabs[index];
    return true;
}

bool XTextOption_tab_equals(const XTextOptionTab* a, const XTextOptionTab* b)
{
    if (a == b) return true; /* 同一指针（含同为 NULL）必相等。 */
    if (!a || !b) return false;
    /* Qt Tab::operator==：type 相等 && qFuzzyCompare(position) &&
       delimiter == other.delimiter。 */
    if (a->type != b->type) return false;
    if (a->delimiter != b->delimiter) return false;
    return xto_fuzzyEqualPosition(a->position, b->position);
}

#endif /* XGUI_ON */
