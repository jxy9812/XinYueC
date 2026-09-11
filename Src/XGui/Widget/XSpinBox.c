/**
 * @file       XSpinBox.c
 * @brief      XSpinBox 整数微调框实现（继承 XAbstractSpinBox；对齐
 *             Qt 6.8 QSpinBox）。
 * @details    数值语义：min/max 范围（默认 0..99）、value 钳位、
 *             singleStep 单步、wrapping 循环（继承基类）、Home/End
 *             边界跳转（经大步数 stepBy 语义）。内嵌编辑框（基类拥有）
 *             文本变化实时解析——合法且界内则更新 value 并发射
 *             valueChanged/textChanged；非法或越界保持原值，Return/
 *             失焦提交时按 correctionMode 修正（CorrectToPreviousValue
 *             恢复上次有效值；CorrectToNearestValue 剥离非法字符后
 *             钳位到最近合法值）。显示文本 = prefix + 按
 *             displayIntegerBase/千分位格式化的数值 + suffix；
 *             value==minimum 且设置特殊值文本时整段显示特殊值文本。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#if XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON

#include "XSpinBox.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

#define XSPINBOX_BUTTON_W 16
/** @brief Home/End 边界跳转步数阈值（|steps| ≥ 该值时跳转到范围边界）。 */
#define XSPINBOX_BOUNDARY_STEP (INT_MAX / 4)

/* ==================== 前向声明 ==================== */
static void VXSpinBox_stepBy(XAbstractSpinBox* self, int steps);
static void VXSpinBox_paintEvent(XWidget* self, XEvent* event);
static void VXSpinBox_mousePressEvent(XWidget* self, XEvent* event);
static XValidatorState VXSpinBox_validate(XAbstractSpinBox* self,
                                          const char* input, int* pos);
static void VXSpinBox_fixup(XAbstractSpinBox* self, char* input,
                            size_t capacity);
static void VXSpinBox_clear(XAbstractSpinBox* self);
static int  VXSpinBox_stepEnabled(XAbstractSpinBox* self);
static void VXSpinBox_interpret(XAbstractSpinBox* self);
static void VXSpinBox_updateEdit(XAbstractSpinBox* self);
static int  VXSpinBox_valueFromText(XSpinBox* self, const char* text);
static char* VXSpinBox_textFromValue(XSpinBox* self, int val);
static void VXSpinBox_deinit(XClass* obj);
static void VXSpinBox_copy(XSpinBox* self, const XSpinBox* other);
static void VXSpinBox_move(XSpinBox* self, XSpinBox* other);

/* ==================== 内部辅助 ==================== */

static uint32_t spinbox_color(const XSpinBox* self, XPaletteColorRole role)
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

static int spinbox_clamp(const XSpinBox* self, int value)
{
    if (value < self->m_min) return self->m_min;
    if (value > self->m_max) return self->m_max;
    return value;
}

static char* spinbox_strdup(const char* text)
{
    return XMemory_strdup(text ? text : "");
}

static void spinbox_strfree(char** ptext)
{
    if (ptext && *ptext) {
        XFree_System(*ptext);
        *ptext = NULL;
    }
}

/** @brief 发射 int 参数信号（valueChanged）。 */
static void spinbox_emitInt(XSpinBox* self, size_t signal, int value)
{
    XVarList* arguments = XVarList_Create(XVar(int, value));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 发射 const char* 参数信号（textChanged）。 */
static void spinbox_emitStr(XSpinBox* self, size_t signal, const char* text)
{
    XVarList* arguments;
    if (!self || !text) return;
    arguments = XVarList_Create(XVar(const char*, text));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/**
 * @brief 剥离前缀/后缀/千分位/首尾空白。
 * @param strict true：前缀/后缀不匹配视为失败；false：按匹配与否部分剥离。
 */
static bool spinbox_stripText(const XSpinBox* self, const char* text,
                              char* out, size_t cap, bool strict)
{
    const char* p;
    size_t plen;
    size_t slen;
    size_t len;
    size_t o = 0;
    size_t i;
    if (!self || !text || !out || cap == 0) return false;
    plen = self->m_prefix ? strlen(self->m_prefix) : 0;
    slen = self->m_suffix ? strlen(self->m_suffix) : 0;
    p = text;
    if (plen) {
        if (strncmp(p, self->m_prefix, plen) != 0) {
            if (strict) return false;
        } else {
            p += plen;
        }
    }
    len = strlen(p);
    if (slen) {
        if (len < slen || strcmp(p + len - slen, self->m_suffix) != 0) {
            if (strict) return false;
        } else {
            len -= slen;
        }
    }
    /* 去除首尾空白。 */
    while (len > 0 && *p == ' ') { ++p; --len; }
    while (len > 0 && p[len - 1] == ' ') --len;
    for (i = 0; i < len && o + 1 < cap; ++i) {
        if (p[i] == ',') continue; /* 千分位分隔符剥离 */
        out[o++] = p[i];
    }
    out[o] = '\0';
    return true;
}

/** @brief 按显示进制解析已剥离文本（含 +/-；失败时 ok=false）。 */
static int spinbox_parseValue(const XSpinBox* self, const char* stripped,
                              bool* ok)
{
    const char* p;
    long v;
    char* end;
    int sign = 1;
    if (ok) *ok = false;
    if (!self || !stripped) return 0;
    p = stripped;
    if (*p == '-') { sign = -1; ++p; }
    else if (*p == '+') { ++p; }
    if (!*p) return 0;
    v = strtol(p, &end, self->m_displayIntegerBase);
    if (end == p || *end != '\0') return 0;
    if (ok) *ok = true;
    return (int)(sign * v);
}

/** @brief 数值部分格式化（按 displayIntegerBase；base 10 且开启千分位时
 *          每 3 位插入逗号（左首组为 n%3 位）；负数带 '-'）。 */
static void spinbox_formatValue(const XSpinBox* self, int val, char* out,
                                size_t cap)
{
    int base;
    char digits[64];
    int n = 0;
    int k;
    int i = 0;
    int cnt = 0;
    int g0;
    unsigned int absv;
    bool neg;
    if (!self || !out || cap == 0) return;
    base = self->m_displayIntegerBase;
    if (base < 2) base = 10;
    neg = val < 0;
    absv = neg ? (unsigned int)(-(long long)val) : (unsigned int)val;
    if (absv == 0) digits[n++] = '0';
    while (absv > 0) {
        unsigned int d = absv % (unsigned int)base;
        digits[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        absv /= (unsigned int)base;
    }
    /* 千分位左首组宽度：n%3（整除时 3），此后每 3 位一组。 */
    g0 = (n % 3 == 0) ? 3 : n % 3;
    if (neg) out[i++] = '-';
    for (k = n - 1; k >= 0; --k) {
        if ((size_t)i + 1 >= cap) break;
        out[i++] = digits[k];
        ++cnt;
        if (base == 10 && self->m_base.m_groupSeparatorShown &&
            cnt < n && (cnt == g0 || (cnt - g0) % 3 == 0) &&
            (size_t)i + 1 < cap)
            out[i++] = ',';
    }
    out[i] = '\0';
}

/** @brief 完整显示文本（前缀 + 数值 + 后缀；特殊值整段替换）。 */
static char* spinbox_textFromValue(const XSpinBox* self, int val)
{
    char num[96];
    size_t plen;
    size_t slen;
    size_t nlen;
    char* out;
    if (!self) return NULL;
    if (self->m_base.m_specialValueText &&
        self->m_base.m_specialValueText[0] && val == self->m_min)
        return spinbox_strdup(self->m_base.m_specialValueText);
    spinbox_formatValue(self, val, num, sizeof(num));
    plen = self->m_prefix ? strlen(self->m_prefix) : 0;
    slen = self->m_suffix ? strlen(self->m_suffix) : 0;
    nlen = strlen(num);
    out = (char*)XMalloc_System(plen + nlen + slen + 1);
    if (!out) return NULL;
    if (plen) memcpy(out, self->m_prefix, plen);
    memcpy(out + plen, num, nlen);
    if (slen) memcpy(out + plen + nlen, self->m_suffix, slen);
    out[plen + nlen + slen] = '\0';
    return out;
}

/**
 * @brief      数字校验回调（装到内嵌编辑框，对标 QIntValidator）。
 * @details    空/纯数字（可带负号）→ Acceptable；部分可构成数字 →
 *             Intermediate；含其他字符 → Invalid（编辑框拒绝该次
 *             编辑，字母等无法进入文本）。
 */
static XLineEditValidatorState spinbox_validateNumeric(
    XLineEdit* edit, const char* text, void* userData)
{
    const char* p;
    int digits = 0;
    (void)edit;
    (void)userData;
    if (!text || text[0] == '\0') return XLineEditValidatorState_Intermediate;
    p = text;
    if (p[0] == '-' || p[0] == '+') ++p; /* 允许符号开头 */
    for (; *p; ++p) {
        if (*p >= '0' && *p <= '9') { ++digits; continue; }
        return XLineEditValidatorState_Invalid; /* 非数字字符：拒绝 */
    }
    return digits > 0 ? XLineEditValidatorState_Acceptable
                      : XLineEditValidatorState_Intermediate;
}

/** @brief 用当前值刷新编辑框文本（无变化时跳过）。 */
static void spinbox_refreshText(XSpinBox* self)
{
    XLineEdit* edit;
    char* buf;
    if (!self) return;
    edit = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
    if (!edit) return;
    buf = XSpinBox_textFromValue_base(self, self->m_value);
    if (!buf) return;
    if (strcmp(XLineEdit_text(edit), buf) != 0)
        XLineEdit_setText(edit, buf);
    XFree_System(buf);
}

/** @brief 应用值语义：钳位/刷新/发射 valueChanged（内部提交共用）。 */
static void spinbox_setValueInternal(XSpinBox* self, int value)
{
    if (!self) return;
    if (value == self->m_value) {
        spinbox_refreshText(self);
        self->m_textDirty = false;
        return;
    }
    self->m_value = value;
    self->m_textDirty = false;
    self->m_base.m_cleared = false;
    spinbox_refreshText(self);
    spinbox_emitInt(self, (size_t)XSpinBox_valueChanged_signal(self),
                    self->m_value);
}

/** @brief 文本变化转发槽：编辑框文本变化 → 自身 textChanged；合法解析
 *         则更新 value（键盘跟踪开启时）。 */
static void spinbox_onTextChanged(XObject* sender, XVarList* args)
{
    XLineEdit* edit = (XLineEdit*)sender;
    XSpinBox* self = edit
        ? (XSpinBox*)XWidget_parentWidget((XWidget*)edit)
        : NULL;
    const char* text;
    char stripped[1024];
    int v;
    bool ok;
    (void)args;
    if (!self) return;
    text = XLineEdit_text(edit);
    /* 编辑框文本变化转发为自身 textChanged 信号（含前缀/后缀）。 */
    spinbox_emitStr(self, (size_t)XSpinBox_textChanged_signal(self), text);
    if (text[0] != '\0') self->m_base.m_cleared = false;
    /* 特殊值文本：值归 minimum。 */
    if (self->m_base.m_specialValueText &&
        self->m_base.m_specialValueText[0] &&
        strcmp(text, self->m_base.m_specialValueText) == 0) {
        if (self->m_value != self->m_min) {
            self->m_value = self->m_min;
            spinbox_emitInt(self,
                            (size_t)XSpinBox_valueChanged_signal(self),
                            self->m_value);
        }
        self->m_textDirty = false;
        return;
    }
    if (text[0] == '\0') { self->m_textDirty = true; return; }
    if (!spinbox_stripText(self, text, stripped, sizeof(stripped), true)) {
        self->m_textDirty = true;
        return;
    }
    v = spinbox_parseValue(self, stripped, &ok);
    if (!ok) { self->m_textDirty = true; return; }
    if (v >= self->m_min && v <= self->m_max) {
        if (self->m_base.m_keyboardTracking) {
            if (v != self->m_value) {
                self->m_value = v;
                spinbox_emitInt(self,
                                (size_t)XSpinBox_valueChanged_signal(self),
                                self->m_value);
            }
            self->m_textDirty = false;
        } else {
            /* 键盘跟踪关闭：value 变化推迟到提交（interpret）时生效。 */
            self->m_textDirty = (v != self->m_value);
        }
    } else {
        /* 越界文本：保持原值；提交时按修正模式处理。 */
        self->m_textDirty = true;
    }
}

/** @brief 提交槽（编辑框 editingFinished）：按修正模式解释当前文本。 */
static void spinbox_onEditingFinished(XObject* sender, XVarList* args)
{
    XLineEdit* edit = (XLineEdit*)sender;
    XSpinBox* self = edit
        ? (XSpinBox*)XWidget_parentWidget((XWidget*)edit)
        : NULL;
    (void)args;
    if (!self) return;
    XAbstractSpinBox_interpretText((XAbstractSpinBox*)self);
}

/** @brief 命中检测：点击位置是否在按钮区（右 16px）。 */
static bool spinbox_hitButton(const XSpinBox* self, const XPoint* pos)
{
    int w = XWidget_width((XWidget*)self);
    return pos && pos->x >= w - XSPINBOX_BUTTON_W;
}

/** @brief 按钮区上/下半：上箭头（true）/下箭头（false）。 */
static bool spinbox_buttonIsUp(const XSpinBox* self, const XPoint* pos)
{
    int h = XWidget_height((XWidget*)self);
    return pos->y < h / 2;
}

/* ==================== 虚槽实现 ==================== */

/** @brief 步进（钳位/循环；Home/End 大步数=边界跳转；刷新文本并发射信号）。 */
static void VXSpinBox_stepBy(XAbstractSpinBox* self, int steps)
{
    XSpinBox* spin = (XSpinBox*)self;
    int v;
    long long t;
    if (!spin) return;
    if (steps >= XSPINBOX_BOUNDARY_STEP || steps <= -XSPINBOX_BOUNDARY_STEP) {
        /* Home/End 边界跳转（wrapping 时同样直接到边界）。 */
        v = (steps > 0) ? spin->m_max : spin->m_min;
    } else if (self->m_wrapping && spin->m_min != spin->m_max) {
        long long range = (long long)spin->m_max - spin->m_min + 1;
        t = (long long)spin->m_value +
            (long long)steps * spin->m_singleStep;
        if (t > spin->m_max)
            v = (int)(spin->m_min + (t - spin->m_max - 1) % range);
        else if (t < spin->m_min)
            v = (int)(spin->m_max - (spin->m_min - t - 1) % range);
        else
            v = (int)t;
    } else {
        t = (long long)spin->m_value +
            (long long)steps * spin->m_singleStep;
        v = spinbox_clamp(spin, (int)t);
    }
    spinbox_setValueInternal(spin, v);
}

/** @brief 绘制：基类编辑框之上叠加按钮区箭头。 */
static void VXSpinBox_paintEvent(XWidget* self, XEvent* event)
{
    XSpinBox* spin = (XSpinBox*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r = XWidget_rect(self);
    uint32_t button;
    uint32_t dark;
    int symbols;
    int bx;
    int bh;
    (void)event;
    if (!spin || r.width <= 2 || r.height <= 2) return;
    symbols = XAbstractSpinBox_buttonSymbols((XAbstractSpinBox*)spin);
    if (symbols == XAbstractSpinBoxButtonSymbols_NoButtons) return;
    r.x = 0; r.y = 0;
    button = spinbox_color(spin, XPaletteColorRole_Button);
    dark   = spinbox_color(spin, XPaletteColorRole_Dark);

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

    bx = r.x + r.width - XSPINBOX_BUTTON_W;
    bh = r.height / 2;
    {
        XRect area = { bx, r.y, XSPINBOX_BUTTON_W, r.height };
        XPainter_fillRect(&painter, &area, button);
    }
    {
        XRect sep = { bx - 1, r.y, 1, r.height };
        XPainter_fillRect(&painter, &sep, dark);
    }
    if (symbols == XAbstractSpinBoxButtonSymbols_UpDownArrows) {
        int cx = bx + XSPINBOX_BUTTON_W / 2;
        int cy1 = r.y + bh / 2;
        int cy2 = r.y + bh + bh / 2;
        int s = 3;
        XPainter_setPen(&painter, dark);
        XPainter_drawLine(&painter, cx - s, cy1 + 1, cx + s, cy1 + 1);
        XPainter_drawLine(&painter, cx - s, cy1 + 1, cx, cy1 - s);
        XPainter_drawLine(&painter, cx + s, cy1 + 1, cx, cy1 - s);
        XPainter_drawLine(&painter, cx - s, cy2 - 1, cx + s, cy2 - 1);
        XPainter_drawLine(&painter, cx - s, cy2 - 1, cx, cy2 + s);
        XPainter_drawLine(&painter, cx + s, cy2 - 1, cx, cy2 + s);
    } else {
        int cx = bx + XSPINBOX_BUTTON_W / 2;
        int cy1 = r.y + bh / 2;
        int cy2 = r.y + bh + bh / 2;
        XPainter_setPen(&painter, dark);
        XPainter_drawLine(&painter, cx - 2, cy1, cx + 2, cy1);
        XPainter_drawLine(&painter, cx - 2, cy2, cx + 2, cy2);
        XPainter_drawLine(&painter, cx, cy2 - 2, cx, cy2 + 2);
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 鼠标按下：命中按钮区则步进，否则交给编辑框。 */
static void VXSpinBox_mousePressEvent(XWidget* self, XEvent* event)
{
    XSpinBox* spin = (XSpinBox*)self;
    XMouseEvent* me;
    XPoint pos;
    int symbols;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    symbols = XAbstractSpinBox_buttonSymbols((XAbstractSpinBox*)spin);
    if (symbols == XAbstractSpinBoxButtonSymbols_NoButtons) return;
    pos = XMouseEvent_position(me);
    if (spinbox_hitButton(spin, &pos)) {
        int up = spinbox_buttonIsUp(spin, &pos);
        XAbstractSpinBox_stepBy_base((XAbstractSpinBox*)spin, up ? 1 : -1);
        XEvent_accept(event);
        return;
    }
    XEvent_ignore(event);
}

/** @brief 校验虚槽：特殊值恒可接受；剥离前后缀后按进制解析并判界。 */
static XValidatorState VXSpinBox_validate(XAbstractSpinBox* self,
                                          const char* input, int* pos)
{
    XSpinBox* spin = (XSpinBox*)self;
    char stripped[1024];
    int v;
    bool ok;
    (void)pos;
    if (!spin || !input) return XValidatorState_Invalid;
    if (self->m_specialValueText && self->m_specialValueText[0] &&
        strcmp(input, self->m_specialValueText) == 0)
        return XValidatorState_Acceptable;
    if (!spinbox_stripText(spin, input, stripped, sizeof(stripped), true))
        return XValidatorState_Invalid; /* 前缀/后缀缺失不可接受 */
    if (spin->m_max != spin->m_min &&
        (stripped[0] == '\0' ||
         (spin->m_min < 0 && strcmp(stripped, "-") == 0) ||
         (spin->m_max >= 0 && strcmp(stripped, "+") == 0)))
        return XValidatorState_Intermediate;
    if (stripped[0] == '-' && spin->m_min >= 0)
        return XValidatorState_Invalid; /* 负号但下限非负（对标 Qt 特例） */
    v = spinbox_parseValue(spin, stripped, &ok);
    if (!ok) return XValidatorState_Invalid;
    if (v >= spin->m_min && v <= spin->m_max)
        return XValidatorState_Acceptable;
    if (spin->m_max == spin->m_min)
        return XValidatorState_Invalid;
    if ((v >= 0 && v > spin->m_max) || (v < 0 && v < spin->m_min))
        return XValidatorState_Invalid;
    return XValidatorState_Intermediate;
}

/** @brief 修正虚槽：剥离非当前进制字符与千分位（就地收缩）。 */
static void VXSpinBox_fixup(XAbstractSpinBox* self, char* input,
                            size_t capacity)
{
    XSpinBox* spin = (XSpinBox*)self;
    size_t i;
    size_t o = 0;
    int base;
    (void)capacity;
    if (!spin || !input) return;
    base = spin->m_displayIntegerBase;
    for (i = 0; input[i]; ++i) {
        char c = input[i];
        bool keep = false;
        if (c == ',') continue;
        if (c == '-' || c == '+') {
            keep = true;
        } else if (c >= '0' && c <= '9') {
            keep = (c - '0') < base;
        } else if (c >= 'a' && c <= 'z') {
            keep = (c - 'a' + 10) < base;
        } else if (c >= 'A' && c <= 'Z') {
            keep = (c - 'A' + 10) < base;
        }
        if (keep) input[o++] = c;
    }
    input[o] = '\0';
    /* 去除首尾空白。 */
    while (o > 0 && input[0] == ' ') {
        memmove(input, input + 1, o);
        --o;
    }
    while (o > 0 && input[o - 1] == ' ') input[--o] = '\0';
}

/** @brief 清空虚槽：保留前缀/后缀清空数值部分（对标 Qt）。 */
static void VXSpinBox_clear(XAbstractSpinBox* self)
{
    XSpinBox* spin = (XSpinBox*)self;
    XLineEdit* edit;
    char buf[512];
    if (!spin) return;
    self->m_cleared = true;
    edit = self->m_lineEdit;
    if (!edit) return;
    snprintf(buf, sizeof(buf), "%s%s",
             spin->m_prefix ? spin->m_prefix : "",
             spin->m_suffix ? spin->m_suffix : "");
    XLineEdit_setText(edit, buf);
    XLineEdit_setCursorPosition(edit,
        (int)(spin->m_prefix ? strlen(spin->m_prefix) : 0));
}

/** @brief 步进使能虚槽：按当前值是否到边界返回位组合（wrapping 恒双向）。 */
static int VXSpinBox_stepEnabled(XAbstractSpinBox* self)
{
    XSpinBox* spin = (XSpinBox*)self;
    int flags = 0;
    if (!spin) return 0;
    if (XAbstractSpinBox_isReadOnly(self))
        return XAbstractSpinBoxStepEnabledFlag_StepNone;
    if (self->m_wrapping)
        return XAbstractSpinBoxStepEnabledFlag_StepUpEnabled |
               XAbstractSpinBoxStepEnabledFlag_StepDownEnabled;
    if (spin->m_value > spin->m_min)
        flags |= XAbstractSpinBoxStepEnabledFlag_StepDownEnabled;
    if (spin->m_value < spin->m_max)
        flags |= XAbstractSpinBoxStepEnabledFlag_StepUpEnabled;
    return flags;
}

/** @brief 提交虚槽：校验/修正当前文本并应用值语义（对标 Qt 私有 interpret）。 */
static void VXSpinBox_interpret(XAbstractSpinBox* self)
{
    XSpinBox* spin = (XSpinBox*)self;
    XLineEdit* edit;
    char buf[1024];
    char core[1024];
    const char* t;
    int pos = 0;
    XValidatorState st;
    int v;
    bool ok;
    if (!spin) return;
    edit = self->m_lineEdit;
    if (!edit) return;
    if (self->m_cleared) { self->m_cleared = false; return; }
    t = XLineEdit_text(edit);
    snprintf(buf, sizeof(buf), "%s", t ? t : "");
    st = XAbstractSpinBox_validate_base(self, buf, &pos);
    if (st == XValidatorState_Acceptable) {
        v = XSpinBox_valueFromText_base(spin, buf);
        spinbox_setValueInternal(spin, spinbox_clamp(spin, v));
        return;
    }
    if (self->m_correctionMode ==
        XAbstractSpinBoxCorrectionMode_CorrectToNearestValue) {
        /* 先 fixup 再校验。 */
        XAbstractSpinBox_fixup_base(self, buf, sizeof(buf));
        if (XAbstractSpinBox_validate_base(self, buf, &pos) ==
            XValidatorState_Acceptable) {
            v = XSpinBox_valueFromText_base(spin, buf);
            spinbox_setValueInternal(spin, spinbox_clamp(spin, v));
            return;
        }
        /* 修正后仍非法：尝试剥离后直接钳位解析（取最近合法值）。 */
        if (spinbox_stripText(spin, buf, core, sizeof(core), true)) {
            v = spinbox_parseValue(spin, core, &ok);
            if (ok) {
                spinbox_setValueInternal(spin, spinbox_clamp(spin, v));
                return;
            }
        }
        /* 完全无法解析：回退上次有效值。 */
        spinbox_setValueInternal(spin, spin->m_value);
        return;
    }
    /* CorrectToPreviousValue：恢复上次有效值显示。 */
    spinbox_setValueInternal(spin, spin->m_value);
}

/** @brief 显示刷新虚槽：按 textFromValue 刷新编辑框文本。 */
static void VXSpinBox_updateEdit(XAbstractSpinBox* self)
{
    spinbox_refreshText((XSpinBox*)self);
}

/** @brief 文本转值虚槽：剥离前后缀后解析（失败返回 minimum）。 */
static int VXSpinBox_valueFromText(XSpinBox* self, const char* text)
{
    char stripped[1024];
    int v;
    bool ok;
    if (!self) return 0;
    if (!text) return self->m_min;
    if (self->m_base.m_specialValueText &&
        self->m_base.m_specialValueText[0] &&
        strcmp(text, self->m_base.m_specialValueText) == 0)
        return self->m_min;
    if (!spinbox_stripText(self, text, stripped, sizeof(stripped), true))
        return self->m_min;
    v = spinbox_parseValue(self, stripped, &ok);
    return ok ? v : self->m_min;
}

/** @brief 值转文本虚槽：prefix + 格式化数值 + suffix（特殊值整段替换）。 */
static char* VXSpinBox_textFromValue(XSpinBox* self, int val)
{
    return spinbox_textFromValue(self, val);
}

/** @brief 深拷贝：基类拷贝后复制数值字段与前后缀。 */
static void VXSpinBox_copy(XSpinBox* self, const XSpinBox* other)
{
    XLineEdit* edit;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XSpinBox_init(self, NULL, 0);
    XClass_Parent(XAbstractSpinBox, EXClass_Copy,
                  void(*)(XAbstractSpinBox*, const XAbstractSpinBox*))(
                      (XAbstractSpinBox*)self, (const XAbstractSpinBox*)other);
    self->m_min = other->m_min;
    self->m_max = other->m_max;
    self->m_value = other->m_value;
    self->m_singleStep = other->m_singleStep;
    self->m_stepType = other->m_stepType;
    self->m_displayIntegerBase = other->m_displayIntegerBase;
    self->m_textDirty = other->m_textDirty;
    spinbox_strfree(&self->m_prefix);
    spinbox_strfree(&self->m_suffix);
    self->m_prefix = spinbox_strdup(other->m_prefix);
    self->m_suffix = spinbox_strdup(other->m_suffix);
    /* 基类拷贝重建了编辑框：重新挂接文本变化/提交槽并刷新显示。 */
    edit = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
    if (edit) {
        XObject_connect_2((XObject*)edit,
                          (size_t)XLineEdit_textChanged_signal(edit),
                          spinbox_onTextChanged);
        XObject_connect_2((XObject*)edit,
                          (size_t)XLineEdit_editingFinished_signal(edit),
                          spinbox_onEditingFinished);
        spinbox_refreshText(self);
    }
}

/** @brief 移动语义：基类移动后转移数值字段与前后缀，源对象归默认值。 */
static void VXSpinBox_move(XSpinBox* self, XSpinBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XSpinBox_init(self, NULL, 0);
    XClass_Parent(XAbstractSpinBox, EXClass_Move,
                  void(*)(XAbstractSpinBox*, XAbstractSpinBox*))(
                      (XAbstractSpinBox*)self, (XAbstractSpinBox*)other);
    self->m_min = other->m_min;
    self->m_max = other->m_max;
    self->m_value = other->m_value;
    self->m_singleStep = other->m_singleStep;
    self->m_stepType = other->m_stepType;
    self->m_displayIntegerBase = other->m_displayIntegerBase;
    self->m_textDirty = other->m_textDirty;
    spinbox_strfree(&self->m_prefix);
    spinbox_strfree(&self->m_suffix);
    self->m_prefix = other->m_prefix;
    self->m_suffix = other->m_suffix;
    other->m_prefix = NULL;
    other->m_suffix = NULL;
    other->m_min = 0;
    other->m_max = 99;
    other->m_value = 0;
    other->m_singleStep = 1;
    other->m_stepType = XAbstractSpinBoxStepType_DefaultStepType;
    other->m_displayIntegerBase = 10;
    other->m_textDirty = false;
}

/** @brief 析构：释放前后缀后转父类。 */
static void VXSpinBox_deinit(XClass* obj)
{
    XSpinBox* self = (XSpinBox*)obj;
    if (!self) return;
    spinbox_strfree(&self->m_prefix);
    spinbox_strfree(&self->m_suffix);
    XClass_Deinit_Parent(XAbstractSpinBox, (XAbstractSpinBox*)obj);
}

/* ==================== 生命周期 ==================== */

XVtable* XSpinBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSpinBox)
    XVTABLE_INHERIT_XCLASS(XAbstractSpinBox);

    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepBy, VXSpinBox_stepBy);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Validate, VXSpinBox_validate);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Fixup, VXSpinBox_fixup);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Clear, VXSpinBox_clear);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepEnabled, VXSpinBox_stepEnabled);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Interpret, VXSpinBox_interpret);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_UpdateEdit, VXSpinBox_updateEdit);
    XVTABLE_OVERLOAD_DEFAULT(EXSpinBox_ValueFromText, VXSpinBox_valueFromText);
    XVTABLE_OVERLOAD_DEFAULT(EXSpinBox_TextFromValue, VXSpinBox_textFromValue);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXSpinBox_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXSpinBox_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSpinBox_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSpinBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSpinBox_move);

    return XVTABLE_DEFAULT;
}

void XSpinBox_init(XSpinBox* self, XWidget* parent, XWidgetFlags flags)
{
    XLineEdit* edit;
    if (!self) return;
    XAbstractSpinBox_init((XAbstractSpinBox*)self, parent, flags);
    XClassSetVtable(self, XSpinBox);

    self->m_min = 0;
    self->m_max = 99;
    self->m_value = 0;
    self->m_singleStep = 1;
    self->m_stepType = XAbstractSpinBoxStepType_DefaultStepType;
    self->m_displayIntegerBase = 10;
    self->m_prefix = NULL;
    self->m_suffix = NULL;
    self->m_textDirty = false;

    edit = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
    if (edit) {
        spinbox_refreshText(self);
        XLineEdit_setValidator(edit, spinbox_validateNumeric, self);
        XObject_connect_2((XObject*)edit,
                          (size_t)XLineEdit_textChanged_signal(edit),
                          spinbox_onTextChanged);
        XObject_connect_2((XObject*)edit,
                          (size_t)XLineEdit_editingFinished_signal(edit),
                          spinbox_onEditingFinished);
    }
}

XSpinBox* XSpinBox_create_ex(XMemoryType memory, XWidget* parent,
                             XWidgetFlags flags)
{
    XSpinBox* self = (XSpinBox*)XMemory_malloc(sizeof(XSpinBox), memory);
    if (!self) return NULL;
    XSpinBox_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 数值 API（对标 QSpinBox public API） ==================== */

int XSpinBox_minimum(const XSpinBox* self)
{
    return self ? self->m_min : 0;
}

void XSpinBox_setMinimum(XSpinBox* self, int min)
{
    if (!self) return;
    XSpinBox_setRange(self, min, self->m_max > min ? self->m_max : min);
}

int XSpinBox_maximum(const XSpinBox* self)
{
    return self ? self->m_max : 99;
}

void XSpinBox_setMaximum(XSpinBox* self, int max)
{
    if (!self) return;
    XSpinBox_setRange(self, self->m_min < max ? self->m_min : max, max);
}

void XSpinBox_setRange(XSpinBox* self, int min, int max)
{
    if (!self) return;
    /* 对标 Qt：min>max 时上限收敛为 min（不做交换）。 */
    self->m_min = min;
    self->m_max = (min < max) ? max : min;
    XSpinBox_setValue(self, self->m_value);
}

int XSpinBox_value(const XSpinBox* self)
{
    return self ? self->m_value : 0;
}

void XSpinBox_setValue(XSpinBox* self, int value)
{
    int clamped;
    if (!self) return;
    clamped = spinbox_clamp(self, value);
    if (clamped == self->m_value) {
        spinbox_refreshText(self);
        return;
    }
    self->m_value = clamped;
    self->m_textDirty = false;
    self->m_base.m_cleared = false;
    spinbox_refreshText(self);
    spinbox_emitInt(self, (size_t)XSpinBox_valueChanged_signal(self),
                    self->m_value);
}

int XSpinBox_singleStep(const XSpinBox* self)
{
    return self ? self->m_singleStep : 1;
}

void XSpinBox_setSingleStep(XSpinBox* self, int step)
{
    if (self && step >= 0) self->m_singleStep = step;
}

const char* XSpinBox_prefix(const XSpinBox* self)
{
    return (self && self->m_prefix) ? self->m_prefix : "";
}

void XSpinBox_setPrefix(XSpinBox* self, const char* prefix)
{
    if (!self) return;
    if (!prefix) prefix = "";
    if (self->m_prefix && strcmp(self->m_prefix, prefix) == 0) return;
    spinbox_strfree(&self->m_prefix);
    self->m_prefix = spinbox_strdup(prefix);
    spinbox_refreshText(self);
}

const char* XSpinBox_suffix(const XSpinBox* self)
{
    return (self && self->m_suffix) ? self->m_suffix : "";
}

void XSpinBox_setSuffix(XSpinBox* self, const char* suffix)
{
    if (!self) return;
    if (!suffix) suffix = "";
    if (self->m_suffix && strcmp(self->m_suffix, suffix) == 0) return;
    spinbox_strfree(&self->m_suffix);
    self->m_suffix = spinbox_strdup(suffix);
    spinbox_refreshText(self);
}

char* XSpinBox_cleanText(const XSpinBox* self)
{
    char stripped[1024];
    const char* text;
    if (!self) return XMemory_strdup("");
    text = self->m_base.m_lineEdit
        ? XLineEdit_text(self->m_base.m_lineEdit) : "";
    /* 宽松剥离：前缀/后缀按匹配与否部分剥离；千分位与首尾空白去除。 */
    if (!spinbox_stripText(self, text, stripped, sizeof(stripped), false))
        return XMemory_strdup(text);
    return XMemory_strdup(stripped);
}

int XSpinBox_stepType(const XSpinBox* self)
{
    return self ? self->m_stepType
                : XAbstractSpinBoxStepType_DefaultStepType;
}

void XSpinBox_setStepType(XSpinBox* self, int stepType)
{
    if (!self) return;
    if (stepType != XAbstractSpinBoxStepType_DefaultStepType &&
        stepType != XAbstractSpinBoxStepType_AdaptiveDecimalStepType)
        return;
    self->m_stepType = stepType;
}

int XSpinBox_displayIntegerBase(const XSpinBox* self)
{
    return self ? self->m_displayIntegerBase : 10;
}

void XSpinBox_setDisplayIntegerBase(XSpinBox* self, int base)
{
    if (!self) return;
    /* 对标 Qt：越界（<2 或 >36）回退 10。 */
    if (base < 2 || base > 36) base = 10;
    if (self->m_displayIntegerBase == base) return;
    self->m_displayIntegerBase = base;
    spinbox_refreshText(self);
}

/* ==================== 虚槽调度入口（*_base） ==================== */

int XSpinBox_valueFromText_base(const XSpinBox* self, const char* text)
{
    if (!self) return 0;
    return XClassGetVirtualFunc((XSpinBox*)self, EXSpinBox_ValueFromText,
                                int(*)(XSpinBox*, const char*))(
        (XSpinBox*)self, text);
}

char* XSpinBox_textFromValue_base(const XSpinBox* self, int val)
{
    if (!self) return NULL;
    return XClassGetVirtualFunc((XSpinBox*)self, EXSpinBox_TextFromValue,
                                char*(*)(XSpinBox*, int))(
        (XSpinBox*)self, val);
}

/* ==================== 信号 ==================== */

void* XSpinBox_valueChanged_signal(XSpinBox* self)
{
    return (void*)(size_t)XSpinBox_valueChanged_signal;
}

void* XSpinBox_textChanged_signal(XSpinBox* self)
{
    return (void*)(size_t)XSpinBox_textChanged_signal;
}

int XSpinBox_decimals(const XSpinBox* self) { return 0; }
void XSpinBox_setDecimals(XSpinBox* self, int decimals) { (void)self; (void)decimals; }
#endif /* XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON */
