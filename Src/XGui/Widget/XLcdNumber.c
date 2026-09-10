/**
 * @file       XLcdNumber.c
 * @brief      LCD 数码管控件实现（对标 Qt 6.8 QLCDNumber 全部公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XLcdNumber.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XVarList.h"
#include "XString.h"
#include "XGuiConfig.h"
#include "XWidget_Protected.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON

/* ==================== 内部工具（对标 qlcdnumber.cpp 静态助手） ==================== */

/** @brief 十进制格式化（对标 double2string 的 Dec 分支：%*.*g 精度收缩）。 */
static int xlcd_formatDec(double num, int ndigits, char* out, size_t cap,
                          bool* oflow)
{
    int nd = ndigits;
    int len = 0;
    do {
        len = snprintf(out, cap, "%*.*g", ndigits, nd, num);
        if (len < 0) len = 0;
        if ((size_t)len >= cap) len = (int)cap - 1;
        /* Qt 将 "e+" 的 '+' 换为空格以省一位；本实现同样处理。 */
        {
            char* e = strchr(out, 'e');
            if (e && e[1] == '+') e[1] = ' ';
        }
    } while (nd-- && (int)len > ndigits);
    if (oflow) *oflow = (int)len > ndigits;
    return len;
}

/** @brief 整数按进制格式化（对标 int2string：右对齐、负号前置、溢出判定）。 */
static int xlcd_formatInt(int num, int base, int ndigits, char* out,
                          size_t cap, bool* oflow)
{
    bool negative;
    int len = 0;
    unsigned un;
    if (num < 0) {
        negative = true;
        un = (unsigned)(-(long long)num);
    } else {
        negative = false;
        un = (unsigned)num;
    }
    switch (base) {
    case XLcdNumberMode_Hex:
        len = snprintf(out, cap, "%*x", ndigits, un);
        break;
    case XLcdNumberMode_Dec:
        len = snprintf(out, cap, "%*i", ndigits, num);
        break;
    case XLcdNumberMode_Oct:
        len = snprintf(out, cap, "%*o", ndigits, un);
        break;
    case XLcdNumberMode_Bin: {
        char buf[42];
        char* p = &buf[41];
        int nlen = 0;
        *p = '\0';
        do {
            *--p = (char)((un & 1u) + '0');
            un >>= 1;
            ++nlen;
        } while (un != 0);
        len = ndigits - nlen;
        if (len < 0) len = 0;
        if ((size_t)len < cap) {
            memset(out, ' ', (size_t)len);
            strncpy(out + len, p, cap - (size_t)len - 1);
            out[cap - 1] = '\0';
            len += nlen;
        } else {
            len = (int)cap - 1;
        }
        break;
    }
    default:
        break;
    }
    if (len < 0) len = 0;
    if (negative) {
        /* 对标 Qt：负号落在第一个非空格字符之前。 */
        char* first = out;
        while (*first == ' ' && first < out + len) ++first;
        if (first > out) {
            first[-1] = '-';
        } else if ((size_t)len + 1 < cap) {
            memmove(out + 1, out, strlen(out) + 1);
            out[0] = '-';
            ++len;
        }
    }
    if (oflow) *oflow = (int)len > ndigits;
    return len;
}

/** @brief 浮点按进制格式化（对标 double2string：非 Dec 走整数等价）。 */
static int xlcd_formatDouble(double num, int base, int ndigits, char* out,
                             size_t cap, bool* oflow)
{
    if (base != XLcdNumberMode_Dec) {
        if (num >= 2147483648.0 || num < -2147483648.0) {
            if (oflow) *oflow = true;
            out[0] = '\0';
            return 0;
        }
        return xlcd_formatInt((int)num, base, ndigits, out, cap, oflow);
    }
    return xlcd_formatDec(num, ndigits, out, cap, oflow);
}

/** @brief 段编号表（逐字移植 Qt getSegments：0..6 七段、7 小数点、
 *         8/9 冒号两点、99 结束；覆盖 QLCDNumber 全部合法字符）。 */
static const char* xlcd_segments(char ch)
{
    static const char segments[30][8] = {
        { 0, 1, 2, 4, 5, 6, 99, 0},             /* 0    0 / O        */
        { 2, 5, 99, 0, 0, 0, 0, 0},             /* 1    1            */
        { 0, 2, 3, 4, 6, 99, 0, 0},             /* 2    2            */
        { 0, 2, 3, 5, 6, 99, 0, 0},             /* 3    3            */
        { 1, 2, 3, 5, 99, 0, 0, 0},             /* 4    4            */
        { 0, 1, 3, 5, 6, 99, 0, 0},             /* 5    5 / S        */
        { 0, 1, 3, 4, 5, 6, 99, 0},             /* 6    6            */
        { 0, 2, 5, 99, 0, 0, 0, 0},             /* 7    7            */
        { 0, 1, 2, 3, 4, 5, 6, 99},             /* 8    8            */
        { 0, 1, 2, 3, 5, 6, 99, 0},             /* 9    9 / g        */
        { 3, 99, 0, 0, 0, 0, 0, 0},             /* 10   -            */
        { 7, 99, 0, 0, 0, 0, 0, 0},             /* 11   .            */
        { 0, 1, 2, 3, 4, 5, 99, 0},             /* 12   A            */
        { 1, 3, 4, 5, 6, 99, 0, 0},             /* 13   B            */
        { 0, 1, 4, 6, 99, 0, 0, 0},             /* 14   C            */
        { 2, 3, 4, 5, 6, 99, 0, 0},             /* 15   D            */
        { 0, 1, 3, 4, 6, 99, 0, 0},             /* 16   E            */
        { 0, 1, 3, 4, 99, 0, 0, 0},             /* 17   F            */
        { 1, 3, 4, 5, 99, 0, 0, 0},             /* 18   h            */
        { 1, 2, 3, 4, 5, 99, 0, 0},             /* 19   H            */
        { 1, 4, 6, 99, 0, 0, 0, 0},             /* 20   L            */
        { 3, 4, 5, 6, 99, 0, 0, 0},             /* 21   o            */
        { 0, 1, 2, 3, 4, 99, 0, 0},             /* 22   P            */
        { 3, 4, 99, 0, 0, 0, 0, 0},             /* 23   r            */
        { 4, 5, 6, 99, 0, 0, 0, 0},             /* 24   u            */
        { 1, 2, 4, 5, 6, 99, 0, 0},             /* 25   U            */
        { 1, 2, 3, 5, 6, 99, 0, 0},             /* 26   Y            */
        { 8, 9, 99, 0, 0, 0, 0, 0},             /* 27   :            */
        { 0, 1, 2, 3, 99, 0, 0, 0},             /* 28   ' (度符号)   */
        { 99, 0, 0, 0, 0, 0, 0, 0} };           /* 29   空格/非法    */
    if (ch >= '0' && ch <= '9') return segments[ch - '0'];
    if (ch >= 'A' && ch <= 'F') return segments[ch - 'A' + 12];
    if (ch >= 'a' && ch <= 'f') return segments[ch - 'a' + 12];
    switch (ch) {
    case '-': return segments[10];
    case 'O': return segments[0];
    case 'g': return segments[9];
    case '.': return segments[11];
    case 'h': return segments[18];
    case 'H': return segments[19];
    case 'l': case 'L': return segments[20];
    case 'o': return segments[21];
    case 'p': case 'P': return segments[22];
    case 'r': case 'R': return segments[23];
    case 'u': case 'U': return segments[25];
    case 'y': case 'Y': return segments[26];
    case ':': return segments[27];
    case '\'': return segments[28];
    default: return segments[29];
    }
}

/** @brief 初始化显示串：ndigits 个空格，末位 '0'（对标构造默认显示）。 */
static void xlcd_resetString(XLcdNumber* self)
{
    memset(self->m_digitStr, ' ', (size_t)self->m_digitCount);
    if (self->m_digitCount > 0)
        self->m_digitStr[self->m_digitCount - 1] = '0';
    self->m_digitStr[self->m_digitCount] = '\0';
}

/** @brief 刷新 sizeHint 存储位（对标 QLCDNumber::sizeHint 公式）。 */
static void xlcd_updateSizeHint(XLcdNumber* self)
{
    XSize hint;
    hint.width = 10 + 9 * (self->m_digitCount +
                           (self->m_smallDecimalPoint ? 0 : 1));
    hint.height = 23;
    XWidget_setSizeHint((XWidget*)self, &hint);
}

/** @brief 显示串设置（对标 QLCDNumberPrivate::internalSetString）：
 *         非 smallPoint 时右对齐裁剪到 ndigits；smallPoint 时小数点并入
 *         前一数字位（点标志存入 m_points 供绘制）。 */
static void xlcd_internalSetString(XLcdNumber* self, const char* s)
{
    char buffer[XLCDNUMBER_STR_MAX];
    bool points[XLCDNUMBER_STR_MAX];
    int ndigits = self->m_digitCount;
    int len = s ? (int)strlen(s) : 0;
    int i;
    int index;
    memset(points, 0, sizeof(points));
    memset(buffer, ' ', (size_t)(ndigits > 0 ? ndigits : 0));
    if (ndigits <= 0) {
        self->m_digitStr[0] = '\0';
        memset(self->m_points, 0, sizeof(self->m_points));
        return;
    }
    if (!self->m_smallDecimalPoint) {
        if (len == ndigits) {
            memcpy(buffer, s, (size_t)ndigits);
        } else if (len > ndigits) {
            memcpy(buffer, s + (len - ndigits), (size_t)ndigits);
        } else {
            memcpy(buffer + (ndigits - len), s, (size_t)len);
        }
    } else {
        bool lastWasPoint = true;
        index = -1;
        for (i = 0; i < len; ++i) {
            if (s[i] == '.') {
                if (lastWasPoint) {
                    if (index == ndigits - 1) break;
                    ++index;
                    buffer[index] = ' ';
                }
                if (index >= 0) points[index] = true;
                lastWasPoint = true;
            } else {
                if (index == ndigits - 1) break;
                ++index;
                buffer[index] = s[i];
                if (index >= 0) points[index] = false;
                lastWasPoint = false;
            }
        }
        if (index < ndigits - 1) {
            for (i = index; i >= 0; --i) {
                buffer[ndigits - 1 - index + i] = buffer[i];
                points[ndigits - 1 - index + i] = points[i];
            }
            for (i = 0; i < ndigits - index - 1; ++i) {
                buffer[i] = ' ';
                points[i] = false;
            }
        }
    }
    memcpy(self->m_digitStr, buffer, (size_t)ndigits);
    self->m_digitStr[ndigits] = '\0';
    memcpy(self->m_points, points, sizeof(self->m_points));
    XWidget_update((XWidget*)self);
}

/** @brief 发射 overflow 信号（无连接时释放参数，防泄漏）。 */
static void xlcd_emitOverflow(XLcdNumber* self)
{
    XVarList* arguments = XVarList_create(0);
    if (!arguments) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XLcdNumber_overflow_signal, arguments,
                           NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 取调色板角色颜色（无调色板能力时回退纯黑/白）。 */
static uint32_t xlcd_color(const XLcdNumber* self, XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, XPaletteColorGroup_Current, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/* ==================== 绘制（七段数码管；几何为控件均分简化） ==================== */

/** @brief 用填充矩形画一段（seg 0..6 七段、7 小数点、8/9 冒号点；
 *         段位语义对标 drawSegment，几何按控件均分简化）。 */
static void xlcd_drawSegment(XLcdNumber* self, XPainter* painter,
                             int x, int y, int segLen, int seg,
                             uint32_t on, uint32_t off)
{
    uint32_t color = (self->m_segmentStyle ==
                      (int)XLcdNumberSegmentStyle_Outline) ? off : on;
    int w = (self->m_segmentStyle == (int)XLcdNumberSegmentStyle_Flat)
                ? 1 : (segLen / 8 < 1 ? 1 : segLen / 8);
    XRect r;
    switch (seg) {
    case 0: XRect_init(&r, x, y, segLen, w); break;                          /* a 顶 */
    case 1: XRect_init(&r, x + segLen, y, w, segLen); break;                  /* b 右上 */
    case 2: XRect_init(&r, x + segLen, y + segLen, w, segLen); break;          /* c 右下 */
    case 3: XRect_init(&r, x, y + 2 * segLen, segLen, w); break;               /* d 底 */
    case 4: XRect_init(&r, x, y + segLen, w, segLen); break;                   /* e 左下 */
    case 5: XRect_init(&r, x, y, w, segLen); break;                            /* f 左上 */
    case 6: XRect_init(&r, x, y + segLen, segLen, w); break;                   /* g 中 */
    case 7: XRect_init(&r, x + segLen + w, y + 2 * segLen - w,
                       w, w); break;                                           /* 小数点 */
    case 8: XRect_init(&r, x + segLen / 2 - w, y + segLen / 2,
                       w, w); break;                                           /* 冒号上 */
    case 9: XRect_init(&r, x + segLen / 2 - w, y + 2 * segLen - w - segLen / 4,
                       w, w); break;                                           /* 冒号下 */
    default: return;
    }
    (void)off;
    XPainter_fillRect(painter, &r, color);
}

/** @brief 绘制一位数字（字符经段表映射；point 为 smallPoint 点标志）。 */
static void xlcd_drawDigit(XLcdNumber* self, XPainter* painter,
                           int x, int y, int segLen, char ch,
                           bool point, uint32_t on, uint32_t off)
{
    const char* segs = xlcd_segments(ch);
    int i;
    for (i = 0; i < 8 && segs[i] != 99; ++i)
        xlcd_drawSegment(self, painter, x, y, segLen, segs[i], on, off);
    if (point)
        xlcd_drawSegment(self, painter, x, y, segLen, 7, on, off);
}

/** @brief paintEvent：边框 → 按位绘制显示串（数字位均分内容区）。 */
static void VX_lcdNumber_paintEvent(XWidget* self, XEvent* event)
{
    XLcdNumber* lcd = (XLcdNumber*)self;
    XPainter painter;
    XImage* image;
    XRect r;
    XPoint offset;
    int ndigits;
    int dw;
    int segLen;
    int startX;
    int i;
    uint32_t on;
    uint32_t off;
    int fw;
    if (!lcd || !event) return;
    r.width = XWidget_width(self);
    r.height = XWidget_height(self);
    if (r.width <= 2 || r.height <= 2) return;
    image = XWidget_paintDevice(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    /* 边框（继承 XFrame；frameWidth 决定内容起始）。 */
    XFrame_drawFrame((XFrame*)self, &painter);
    fw = XFrame_frameWidth((XFrame*)self);
    on = xlcd_color(lcd, XPaletteColorRole_WindowText);
    off = xlcd_color(lcd, XPaletteColorRole_Window);
    ndigits = lcd->m_digitCount;
    if (ndigits <= 0 || r.width <= 2 * fw + 4) {
        XPainter_deinit(&painter);
        return;
    }
    dw = (r.width - 2 * fw) / ndigits;
    segLen = (r.height - 2 * fw - 6) / 2;
    if (segLen < 2 || dw < 6) {
        XPainter_deinit(&painter);
        return;
    }
    startX = fw + (r.width - 2 * fw - ndigits * dw) / 2;
    for (i = 0; i < ndigits; ++i) {
        int x = startX + i * dw;
        int y = fw + (r.height - 2 * fw - (2 * segLen + 5)) / 2;
        char ch = lcd->m_digitStr[i];
        bool pt = lcd->m_points[i] != 0;
        if (ch == '.') {
            /* 独立小数点位（非 smallPoint 模式）。 */
            xlcd_drawSegment(lcd, &painter, x, y, segLen, 7, on, off);
        } else {
            xlcd_drawDigit(lcd, &painter, x, y, segLen, ch, pt, on, off);
        }
    }
    XPainter_deinit(&painter);
}



/* ==================== 生命周期与虚表 ==================== */

XVtable* XLcdNumber_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XLcdNumber)
    XVTABLE_INHERIT_XCLASS(XFrame);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_lcdNumber_paintEvent);
    return XVTABLE_DEFAULT;
}

void XLcdNumber_init(XLcdNumber* self, XWidget* parent, XWidgetFlags flags)
{
    XLcdNumber_init_2(self, 5u, parent, flags);
}

void XLcdNumber_init_2(XLcdNumber* self, unsigned numDigits,
                       XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XFrame_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XLcdNumber);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 对标 QLCDNumberPrivate::init：Box|Raised 边框、Dec、非小点、
       Filled 段风格。 */
    XFrame_setFrameStyle((XFrame*)self,
                         (int)XFrameShape_Box | (int)XFrameShadow_Raised);
    self->m_value = 0.0;
    self->m_mode = (int)XLcdNumberMode_Dec;
    self->m_smallDecimalPoint = false;
    self->m_segmentStyle = (int)XLcdNumberSegmentStyle_Filled;
    if (numDigits > 99u) numDigits = 99u;
    self->m_digitCount = (int)numDigits;
    xlcd_resetString(self);
    xlcd_updateSizeHint(self);
}

XLcdNumber* XLcdNumber_create_ex(XMemoryType memory, XWidget* parent,
                                 XWidgetFlags flags)
{
    XLcdNumber* self = (XLcdNumber*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XLcdNumber_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

XLcdNumber* XLcdNumber_create_ex_2(XMemoryType memory, unsigned numDigits,
                                   XWidget* parent, XWidgetFlags flags)
{
    XLcdNumber* self = (XLcdNumber*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XLcdNumber_init_2(self, numDigits, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 专属属性 ==================== */

bool XLcdNumber_smallDecimalPoint(const XLcdNumber* self)
{
    return self ? self->m_smallDecimalPoint : false;
}

void XLcdNumber_setSmallDecimalPoint(XLcdNumber* self, bool on)
{
    if (!self || self->m_smallDecimalPoint == on) return;
    self->m_smallDecimalPoint = on;
    xlcd_updateSizeHint(self);
    XWidget_update((XWidget*)self);
}

int XLcdNumber_digitCount(const XLcdNumber* self)
{
    return self ? self->m_digitCount : 0;
}

void XLcdNumber_setDigitCount(XLcdNumber* self, int numDigits)
{
    char buf[XLCDNUMBER_STR_MAX];
    bool of = false;
    if (!self) return;
    /* 对标 Qt：0..99 之外钳位（Qt 侧输出警告）。 */
    if (numDigits > 99) numDigits = 99;
    if (numDigits < 0) numDigits = 0;
    if (numDigits == self->m_digitCount) return;
    self->m_digitCount = numDigits;
    /* 对标 Qt：位数变化后按当前值与进制重新裁剪显示。 */
    if (self->m_mode == (int)XLcdNumberMode_Dec)
        xlcd_formatDec(self->m_value, numDigits, buf, sizeof(buf), &of);
    else
        xlcd_formatInt((int)self->m_value, self->m_mode, numDigits,
                       buf, sizeof(buf), &of);
    xlcd_internalSetString(self, buf);
    xlcd_updateSizeHint(self);
    XWidget_update((XWidget*)self);
}

XLcdNumberMode XLcdNumber_mode(const XLcdNumber* self)
{
    return self ? (XLcdNumberMode)self->m_mode : XLcdNumberMode_Dec;
}

void XLcdNumber_setMode(XLcdNumber* self, XLcdNumberMode mode)
{
    if (!self || (int)mode == self->m_mode) return;
    self->m_mode = (int)mode;
    /* 对标 Qt：setMode 后以当前值重显（进制变化重算显示串）。 */
    XLcdNumber_display_3(self, self->m_value);
}

XLcdNumberSegmentStyle XLcdNumber_segmentStyle(const XLcdNumber* self)
{
    return self ? (XLcdNumberSegmentStyle)self->m_segmentStyle
                : XLcdNumberSegmentStyle_Filled;
}

void XLcdNumber_setSegmentStyle(XLcdNumber* self,
                                XLcdNumberSegmentStyle style)
{
    if (!self || (int)style == self->m_segmentStyle) return;
    self->m_segmentStyle = (int)style;
    XWidget_update((XWidget*)self);
}

bool XLcdNumber_checkOverflowInt(const XLcdNumber* self, int num)
{
    char buf[XLCDNUMBER_STR_MAX];
    bool of = false;
    if (!self) return false;
    xlcd_formatInt(num, self->m_mode, self->m_digitCount,
                   buf, sizeof(buf), &of);
    return of;
}

bool XLcdNumber_checkOverflowDouble(const XLcdNumber* self, double num)
{
    char buf[XLCDNUMBER_STR_MAX];
    bool of = false;
    if (!self) return false;
    xlcd_formatDouble(num, self->m_mode, self->m_digitCount,
                      buf, sizeof(buf), &of);
    return of;
}

double XLcdNumber_value(const XLcdNumber* self)
{
    return self ? self->m_value : 0.0;
}

int XLcdNumber_intValue(const XLcdNumber* self)
{
    /* 对标 qRound：四舍五入到最近整数。 */
    if (!self) return 0;
    return (int)(self->m_value >= 0.0 ? self->m_value + 0.5
                                      : self->m_value - 0.5);
}

/* ==================== 显示槽 ==================== */

void XLcdNumber_display(XLcdNumber* self, const char* utf8)
{
    const char* s;
    char cleaned[XLCDNUMBER_STR_MAX];
    size_t i = 0;
    if (!self) return;
    s = utf8 ? utf8 : "";
    /* 对标 display(const QString&)：值取字符串可解析前缀的数值，
       不可解析为 0；显示串忽略 mode/smallDecimalPoint。 */
    {
        char* end = NULL;
        double v = strtod(s, &end);
        self->m_value = (end && end != s) ? v : 0.0;
    }
    for (i = 0; s[i] != '\0' && i < XLCDNUMBER_STR_MAX - 1; ++i)
        cleaned[i] = s[i];
    cleaned[i] = '\0';
    xlcd_internalSetString(self, cleaned);
}

void XLcdNumber_display_2(XLcdNumber* self, int num)
{
    char buf[XLCDNUMBER_STR_MAX];
    bool of = false;
    if (!self) return;
    self->m_value = (double)num;
    xlcd_formatInt(num, self->m_mode, self->m_digitCount,
                   buf, sizeof(buf), &of);
    if (of)
        xlcd_emitOverflow(self);
    else
        xlcd_internalSetString(self, buf);
}

void XLcdNumber_display_3(XLcdNumber* self, double num)
{
    char buf[XLCDNUMBER_STR_MAX];
    bool of = false;
    if (!self) return;
    self->m_value = num;
    xlcd_formatDouble(num, self->m_mode, self->m_digitCount,
                      buf, sizeof(buf), &of);
    if (of)
        xlcd_emitOverflow(self);
    else
        xlcd_internalSetString(self, buf);
}

void XLcdNumber_setHexMode(XLcdNumber* self)
{
    XLcdNumber_setMode(self, XLcdNumberMode_Hex);
}

void XLcdNumber_setDecMode(XLcdNumber* self)
{
    XLcdNumber_setMode(self, XLcdNumberMode_Dec);
}

void XLcdNumber_setOctMode(XLcdNumber* self)
{
    XLcdNumber_setMode(self, XLcdNumberMode_Oct);
}

void XLcdNumber_setBinMode(XLcdNumber* self)
{
    XLcdNumber_setMode(self, XLcdNumberMode_Bin);
}

/* ==================== 信号 ==================== */

void* XLcdNumber_overflow_signal(XLcdNumber* self)
{
    (void)self;
    return (void*)(size_t)XLcdNumber_overflow_signal;
}

#endif /* XWIDGET_ON && XFRAME_ON && XLCDNUMBER_ON */
