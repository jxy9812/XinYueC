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
#if XGUI_ON
#include "XStringUtils.h"

#include "XAlgorithm.h"
#if XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON

#include "XSpinBox.h"
#include "XString.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#if XPALETTE_ON
#include "XPalette.h"
#endif /* XPALETTE_ON */
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

/** @brief 读取基类特殊值文本（空串安全）。 */
static const char* spinbox_specialText(const XSpinBox* self)
{
    const char* t;
    if (!self || !self->m_base.m_specialValueText) return "";
    t = XString_toUtf8(self->m_base.m_specialValueText);
    return t ? t : "";
}

/** @brief 读取前缀文本（空串安全）。 */
static const char* spinbox_prefixText(const XSpinBox* self)
{
    const char* t;
    if (!self || !self->m_prefix) return "";
    t = XString_toUtf8(self->m_prefix);
    return t ? t : "";
}

/** @brief 读取后缀文本（空串安全）。 */
static const char* spinbox_suffixText(const XSpinBox* self)
{
    const char* t;
    if (!self || !self->m_suffix) return "";
    t = XString_toUtf8(self->m_suffix);
    return t ? t : "";
}



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
    return XStrdup(text ? text : "");
}

/* [死码清理] spinbox_strfree 已删除：本文件无调用点（审计清单）。 */

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
 * @brief      剥离前缀/后缀与首尾空白（对标 QAbstractSpinBoxPrivate::stripped，
 *             qabstractspinbox.cpp:1451-1477）。
 * @details    Qt 语义：只在前缀确实匹配时剥前缀、后缀确实匹配时剥后缀，
 *             随后 trimmed()；**千分位分隔符保留**（cleanText 实测
 *             '1,234,567'）。此前本函数无条件剥 ','，导致 cleanText 与
 *             Qt 不一致（gap 94）。
 * @param self 微调框对象。
 * @param text 输入文本（UTF-8）。
 * @param out  输出缓冲（与 text 不同址）。
 * @param cap  缓冲容量。
 * @return     恒 true（Qt 无失败分支；无匹配时原样 trimmed 拷出）。
 */
static bool spinbox_stripText(const XSpinBox* self, const char* text,
                              char* out, size_t cap)
{
    const char* p;
    size_t plen;
    size_t slen;
    size_t len;
    size_t o = 0;
    if (!self || !text || !out || cap == 0) return false;
    plen = XStrlen(spinbox_prefixText(self));
    slen = XStrlen(spinbox_suffixText(self));
    p = text;
    len = XStrlen(p);
    if (plen && len >= plen &&
        XStrncmp(p, spinbox_prefixText(self), plen) == 0) {
        p += plen;
        len -= plen;
    }
    if (slen && len >= slen &&
        XStrcmp(p + len - slen, spinbox_suffixText(self)) == 0)
        len -= slen;
    /* 去除首尾空白（Qt trimmed；仅 ASCII 空白，与本库 trim 口径一致）。 */
    while (len > 0 && (*p == ' ' || *p == '\t'))
        { ++p; --len; }
    while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t')) --len;
    while (o + 1 < cap && o < len) {
        out[o] = p[o];
        ++o;
    }
    out[o] = '\0';
    return true;
}

/** @brief 按显示进制解析已剥离文本（含 +/-；跳过千分位逗号；失败 ok=false）。
 *  @details 手写累加而非 XStrtol：base 10 需容忍千分位分隔符
 *           （Qt 的 locale.toInt 直接接受分组符，qspinbox.cpp:1121-1134
 *           另有「含分隔符时去符重试」的兜底），且要防溢出不回绕。 */
static int spinbox_parseValue(const XSpinBox* self, const char* stripped,
                              bool* ok)
{
    const char* p;
    long long v = 0;
    int sign = 1;
    int base;
    bool any = false;
    bool overflow = false;
    if (ok) *ok = false;
    if (!self || !stripped) return 0;
    base = self->m_displayIntegerBase;
    if (base < 2) base = 10;
    p = stripped;
    if (*p == '-') { sign = -1; ++p; }
    else if (*p == '+') { ++p; }
    for (; *p; ++p) {
        int d;
        char c = *p;
        if (base == 10 && c == ',') continue; /* 千分位分隔符 */
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else return 0; /* 非数字字符：整体失败 */
        if (d >= base) return 0;
        any = true;
        if (!overflow) {
            v = v * base + d;
            if (sign > 0 && v > 2147483647LL) overflow = true;
            if (sign < 0 && -v < -2147483648LL) overflow = true;
        }
    }
    if (!any) return 0;
    if (overflow)
        v = (sign > 0) ? 2147483647LL : 2147483648LL;
    if (ok) *ok = true;
    return (int)(sign * v);
}

/** @brief 数值部分格式化（按 displayIntegerBase；base 10 且开启千分位时
 *          每 3 位插入逗号（左首组为 n%3 位）；负数带 '-'）。
 *  @note  这是裸数值文本（不含前后缀/特殊值），与 Qt QSpinBox::textFromValue
 *         同口径。 */
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

/** @brief 完整显示文本（前缀 + 裸数值 + 后缀；特殊值整段替换）。
 *  @details 对标 Qt QAbstractSpinBoxPrivate::updateEdit 的组合式
 *           （qabstractspinbox.cpp:1770-1773）：
 *           `specialValue() ? specialValueText : prefix +
 *            textFromValue(value) + suffix`。 */
static char* spinbox_displayText(const XSpinBox* self, int val)
{
    size_t plen;
    size_t slen;
    size_t nlen;
    char* out;
    const char* bare;
    char* bareAlloc;
    if (!self) return NULL;
    if (spinbox_specialText(self)[0] && val == self->m_min)
        return spinbox_strdup(spinbox_specialText(self));
    bareAlloc = XSpinBox_textFromValue_base(self, val);
    if (!bareAlloc) return NULL;
    bare = bareAlloc;
    plen = XStrlen(spinbox_prefixText(self));
    slen = XStrlen(spinbox_suffixText(self));
    nlen = XStrlen(bare);
    out = (char*)XMalloc_System(plen + nlen + slen + 1);
    if (!out) {
        XFree_System(bareAlloc);
        return NULL;
    }
    if (plen) XMemcpy(out, spinbox_prefixText(self), plen);
    XMemcpy(out + plen, bare, nlen);
    if (slen) XMemcpy(out + plen + nlen, spinbox_suffixText(self), slen);
    out[plen + nlen + slen] = '\0';
    XFree_System(bareAlloc);
    return out;
}

/**
 * @brief      数字校验回调（装到内嵌编辑框，对标 QIntValidator）。
 * @details    P0-5 根因修复：回调收到的 text 是整段显示文本
 *             （prefix + 数字段 + suffix；值==minimum 且设置特殊值
 *             文本时整段为特殊值文本）。修复前按整段逐字符验数字——
 *             设置前缀/后缀/特殊值文本后任何键入都判 Invalid，控制器
 *             按"可用→不可用"翻转回滚该次编辑（XLineControl
 *             xlc_finishChange），编辑框完全不可用。对标 Qt
 *             QSpinBoxPrivate::validateAndInterpret：先剥离前后缀、
 *             识别特殊值文本，再仅校验数字段；空段按 Intermediate
 *             口径（允许清空/继续键入）。
 */
static XLineEditValidatorState spinbox_validateNumeric(
    XLineEdit* edit, const char* text, void* userData)
{
    const XSpinBox* spin = (const XSpinBox*)userData;
    const char* p;
    int digits = 0;
    char stripped[1024];
    int base;
    (void)edit;
    if (!text || text[0] == '\0') return XLineEditValidatorState_Intermediate;
    if (spin) {
        /* 特殊值文本整段精确匹配即可接受（与 VXSpinBox_validate、
           spinbox_onTextChanged 的特殊值口径一致）。 */
        if (spinbox_specialText(spin)[0] &&
            XStrcmp(text, spinbox_specialText(spin)) == 0)
            return XLineEditValidatorState_Acceptable;
        /* 剥前缀/后缀与首尾空白（千分位逗号保留，下方按位放行）——
           与 cleanText/interpret 解析口径一致。 */
        if (!spinbox_stripText(spin, text, stripped, sizeof(stripped)))
            return XLineEditValidatorState_Invalid;
        text = stripped;
    }
    if (text[0] == '\0') return XLineEditValidatorState_Intermediate;
    base = spin ? spin->m_displayIntegerBase : 10;
    if (base < 2) base = 10;
    p = text;
    if (p[0] == '-' || p[0] == '+') ++p; /* 允许符号开头 */
    for (; *p; ++p) {
        int d;
        char c = *p;
        if (c == ',' && base == 10) continue; /* 千分位分隔符 */
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') d = c - 'A' + 10;
        else return XLineEditValidatorState_Invalid;
        if (d >= base) return XLineEditValidatorState_Invalid;
        ++digits;
    }
    return digits > 0 ? XLineEditValidatorState_Acceptable
                      : XLineEditValidatorState_Intermediate;
}

/** @brief 用当前值刷新编辑框文本（无变化时跳过；cleared 期间早退）。
 *  @details 对标 Qt updateEdit 的 `if (newText == edit->displayText() ||
 *           cleared) return;`（qabstractspinbox.cpp:1773）——clear() 后
 *           失焦/显示事件不得把数值文本回填（gap 97）。 */
static void spinbox_refreshText(XSpinBox* self)
{
    XLineEdit* edit;
    char* buf;
    if (!self) return;
    /* cleared 期间保持「前缀+后缀」清空态（Qt updateEdit 早退）。 */
    if (self->m_base.m_cleared) return;
    edit = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
    if (!edit) return;
    buf = spinbox_displayText(self, self->m_value);
    if (!buf) return;
    if (XStrcmp(XLineEdit_text(edit), buf) != 0)
        XLineEdit_setText(edit, buf);
    XFree_System(buf);
}

/** @brief 应用值语义：钳位/刷新/发射 valueChanged（内部提交共用）。
 *  @details 对标 Qt QAbstractSpinBoxPrivate::setValue：无论值是否变化都
 *           复位 cleared（`cleared = false;` 在 setValue 首段无条件执行，
 *           故 clear() 后 setValue(同值) 也要恢复显示文本——探针实测
 *           setValue(44) 同值后文本回 '$44 ms'）。 */
static void spinbox_setValueInternal(XSpinBox* self, int value)
{
    if (!self) return;
    self->m_base.m_cleared = false;
    if (value == self->m_value) {
        spinbox_refreshText(self);
        self->m_textDirty = false;
        return;
    }
    self->m_value = value;
    self->m_textDirty = false;
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
    /* 特殊值文本：值归 minimum。 */
    if (spinbox_specialText(self)[0] &&
        XStrcmp(text, spinbox_specialText(self)) == 0) {
        /* 对标 Qt editorTextChanged→setValue：合法文本提交时 setValue
         * 无条件复位 cleared（qabstractspinbox.cpp:1750-1768）。 */
        self->m_base.m_cleared = false;
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
    if (!spinbox_stripText(self, text, stripped, sizeof(stripped))) {
        self->m_textDirty = true;
        return;
    }
    v = spinbox_parseValue(self, stripped, &ok);
    if (!ok) { self->m_textDirty = true; return; }
    if (v >= self->m_min && v <= self->m_max) {
        if (self->m_base.m_keyboardTracking) {
            /* 对标 Qt editorTextChanged：合法文本即刻 setValue——
             * setValue 无条件复位 cleared，故这里同样清标志（探针：
             * clear 后程序性 setText 合法文本仍提交值，kt=1）。 */
            self->m_base.m_cleared = false;
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

/** @brief 步进按钮列几何（以控件本地坐标表达）。
 *
 *  @details 与样式层同源：CC_SpinBox/SC_SpinBoxUp 由
 *  XStyle_subControlRect 给出（样式内部经 xcs_spinBoxColumn 单点推导），
 *  故命中区恒等于实绘按钮区。此前本处写死 `w-16` / `h/2`，与样式
 *  公式分叉：窄控件下样式列宽可能只有 w/4，命中区却仍按 16px 判定
 *  ——「点箭头没反应 / 点输入框却步进」的成因。
 *
 *  无样式时回落既有口径（16px 右列、上下各半），与
 *  XSpinBox 的非样式绘制路径一致。 */
static bool spinbox_buttonRect(const XSpinBox* self, XRect* out)
{
    int w = XWidget_width((XWidget*)self);
    int h = XWidget_height((XWidget*)self);
    if (w <= 2 || h <= 2) return false;
#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        XStyleOption opt;
        XRect up;
        XStyleOption_init(&opt, XStyleCC_SpinBox);
        XRect_init(&opt.m_rect, 0, 0, w, h);
        opt.m_state = XWidget_isEnabled((XWidget*)self)
            ? (uint32_t)XStyleState_Enabled : 0;
        opt.m_spinFrame = self->m_base.m_frame;
        opt.m_spinSymbols = XAbstractSpinBox_buttonSymbols(
            (XAbstractSpinBox*)self);
        up = XStyle_subControlRect(XWidget_style((const XWidget*)self),
                                   XStyleCC_SpinBox, &opt,
                                   XStyleSC_SpinBoxUp, (XWidget*)self);
        if (up.width > 0 && up.height > 0) {
            /* 上/下按钮等高等宽，列高 = 2*hb；合成整列判定。 */
            XRect_init(out, up.x, up.y, up.width, up.height * 2);
            return true;
        }
        return false;
    }
#endif /* XSTYLE_ON */
    XRect_init(out, w - XSPINBOX_BUTTON_W, 0, XSPINBOX_BUTTON_W, h);
    return true;
}

/** @brief 命中检测：点击位置是否在按钮列内。 */
static bool spinbox_hitButton(const XSpinBox* self, const XPoint* pos)
{
    XRect col;
    if (!pos || !spinbox_buttonRect(self, &col)) return false;
    return pos->x >= col.x && pos->x < col.x + col.width &&
           pos->y >= col.y && pos->y < col.y + col.height;
}

/** @brief 按钮区上/下半：上箭头（true）/下箭头（false）。 */
static bool spinbox_buttonIsUp(const XSpinBox* self, const XPoint* pos)
{
    XRect col;
    if (!pos || !spinbox_buttonRect(self, &col)) return false;
    return pos->y < col.y + col.height / 2;
}

/* ==================== 虚槽实现 ==================== */

/** @brief 自适应十进制步长（对标 Qt QSpinBoxPrivate::calculateAdaptiveDecimalStep，
 *         qspinbox.cpp:400-417）。
 *
 *  @details Qt 公式：
 *           - |value| < 100 ⇒ 步长 1；
 *           - 否则 signCompensation = (value 与 steps 异号) ? 1 : 0，
 *             log = (int)log10(|value| - signCompensation) - 1，
 *             步长 = 10^log。
 *           实测口径（本机 Qt 6.8.0 + offscreen）：99→+1、100→+10、
 *           500→+10、999→+10、1000→+100、12345→+1000、-500→+10、
 *           -1000→+10、1000 stepBy(2)→+200。
 *           [整数等价实现] 不引入 libm：`while (a >= 100) { a /= 10;
 *           step *= 10; }` 与 10^(floor(log10(a))-1) 对 a≥100 恒等
 *           （a=100→10、999→10、1000→100、12344→1000）。
 *
 *  @param self  微调框对象。
 *  @param steps 本次步数（仅用其符号判补偿；绝对值不参与步长计算）。
 *  @return 正步长（≥1）。 */
static int spinbox_adaptiveStep(const XSpinBox* self, int steps)
{
    long long absv;
    int a;
    int step = 1;
    if (!self) return 1;
    absv = (self->m_value < 0) ? -(long long)self->m_value
                               : (long long)self->m_value;
    if (absv < 100) return 1;
    /* INT_MIN 的 |value| 溢出 int：夹到 INT_MAX 再算（Qt 侧 qAbs 对
       INT_MIN 无定义，本库显式夹取以保行为确定）。 */
    if (absv > 2147483647LL) absv = 2147483647LL;
    a = (int)(((self->m_value < 0) == (steps < 0)) ? absv : absv - 1);
    while (a >= 100) {
        a /= 10;
        step *= 10;
    }
    return step < 1 ? 1 : step;
}

/** @brief 步进（钳位/循环；Home/End 大步数=边界跳转；刷新文本并发射信号）。 */
static void VXSpinBox_stepBy(XAbstractSpinBox* self, int steps)
{
    XSpinBox* spin = (XSpinBox*)self;
    int v;
    int step;
    long long t;
    if (!spin) return;
    /* 单步步长：DefaultStepType=singleStep；AdaptiveDecimalStepType 按
       Qt 口径随当前值量级自适应（见 spinbox_adaptiveStep）。 */
    step = spin->m_singleStep;
    if (spin->m_stepType == XAbstractSpinBoxStepType_AdaptiveDecimalStepType)
        step = spinbox_adaptiveStep(spin, steps);
    if (steps >= XSPINBOX_BOUNDARY_STEP || steps <= -XSPINBOX_BOUNDARY_STEP) {
        /* Home/End 边界跳转（wrapping 时同样直接到边界）。 */
        v = (steps > 0) ? spin->m_max : spin->m_min;
    } else if (self->m_wrapping && spin->m_min != spin->m_max) {
        long long range = (long long)spin->m_max - spin->m_min + 1;
        t = (long long)spin->m_value +
            (long long)steps * step;
        if (t > spin->m_max)
            v = (int)(spin->m_min + (t - spin->m_max - 1) % range);
        else if (t < spin->m_min)
            v = (int)(spin->m_max - (spin->m_min - t - 1) % range);
        else
            v = (int)t;
    } else {
        t = (long long)spin->m_value +
            (long long)steps * step;
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
    /* P1-R30 根因修复：取消 NoButtons 整段早退。对标 Qt 6.8
     * QAbstractSpinBox::initStyleOption——NoButtons 时 subControls
     * 仍含 SC_SpinBoxFrame|SC_SpinBoxEditField（EditField 全宽），
     * 仅不含 SC_SpinBoxUp|SC_SpinBoxDown；此前早退使 CC_SpinBox
     * 整体不再绘制，边框连带消失。按钮列的跳过改由两处完成：
     * 样式路径经 m_spinSymbols 原值透传（=2 时样式侧不画
     * bevel/箭头），非样式回退路径经下方 NoButtons 分支。 */
    r.x = 0; r.y = 0;
    button = spinbox_color(spin, XPaletteColorRole_Button);
    dark   = spinbox_color(spin, XPaletteColorRole_Dark);

    image = XWidget_paintImage(self);
    if (!image) return;
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);

#if XSTYLE_ON
    if (XWidget_style((const XWidget*)self) != NULL) {
        /* Fusion/公共风格接管：面板 + 上/下按钮走 CC_SpinBox。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        const int btnState = XAbstractSpinBox_buttonState(
            (const XAbstractSpinBox*)spin);
        XStyleOption_init(&opt, XStyleCC_SpinBox);
        opt.m_rect = r;
        opt.m_state = XWidget_isEnabled(self)
            ? XStyleState_Enabled : 0;
        /* 对标 Qt initStyleOption（qabstractspinbox.cpp:1691-1695）：
         * buttonState 非零（按住步进按钮/键盘连发）时画凹面，否则凸起。 */
        if (btnState != (int)XAbstractSpinBoxButtonState_None)
            opt.m_state |= XStyleState_Sunken;
        else if (XWidget_isEnabled(self))
            opt.m_state |= XStyleState_Raised;
        if (XWidget_hasFocus(self))
            opt.m_state |= XStyleState_HasFocus;
        if (XWidget_underMouse(self) && XWidget_isEnabled(self))
            opt.m_state |= XStyleState_MouseOver;
        /* frame 对接基类属性（对标 Qt：setFrame(false) 时样式不再绘制
         * 微调框边框；XStyle 按该标志裁剪边框绘制与 EditField 边距）。 */
        opt.m_spinFrame = spin->m_base.m_frame;
        /* P1-R30：m_spinSymbols 原值透传（0 上下箭头/1 加减号/
         * 2 无按钮）。原 `==PlusMinus?1:0` 折叠把 NoButtons=2 折成
         * 0，样式照画步进按钮列+箭头，setButtonSymbols 公共 API
         * 静默失效；样式侧本已支持 2（XCommonStyle.c subControlRect
         * :3493 EditField 全宽/Up-Down 空矩形、sizeFromContents
         * :2929 不加按钮宽、绘制 :1189 跳过 bevel/箭头）。 */
        opt.m_spinSymbols = symbols;
        opt.m_spinStepEnabled =
            XAbstractSpinBox_stepEnabled_base((XAbstractSpinBox*)spin);
        /* 活动子控件=按住方向（对标 Qt initStyleOption 的
         * `buttonState & Up/Down ⇒ activeSubControls`）。 */
        opt.m_spinActiveUp =
            (btnState & (int)XAbstractSpinBoxButtonState_Up) != 0;
        opt.m_spinActiveDown =
            (btnState & (int)XAbstractSpinBoxButtonState_Down) != 0;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette(self);
#endif
        XStyle_drawComplexControl(style, XStyleCC_SpinBox, &opt,
                                  &painter, self);
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
    /* P1-R30 配套：非样式回退路径同步修——NoButtons 不画按钮列
     * （列背景/分隔线/符号全免；编辑框几何由基类 resizeEvent 在
     * NoButtons 下保证全宽，对标 Qt 无样式时 CC_SpinBox 仅
     * Frame+EditField）。 */
    if (symbols == XAbstractSpinBoxButtonSymbols_NoButtons) {
        XPainter_end(&painter);
        XPainter_deinit(&painter);
        return;
    }
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
        int s = 4;
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

/** @brief 鼠标按下：命中按钮区则进入长按连发（立即步进+阈值定时器），
 *         否则交编辑框。
 *  @details 并入基类 XAbstractSpinBox_spinButtonPress（对标 Qt
 *           QAbstractSpinBoxPrivate::updateState）：按下即步进一拍，
 *           阈值（样式 500ms）后按重复率（150ms）连发；accelerated 时
 *           逐拍缩短。此前只在按下时步进一次，长按无连发（gap 98）。 */
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
        bool up = spinbox_buttonIsUp(spin, &pos);
        /* Ctrl/Shift 等修饰键在按住期间决定 ×10 步进（Qt 取按下事件的
         * keyboardModifiers，连发拍沿用）。 */
        XAbstractSpinBox_setStepModifierDown((XAbstractSpinBox*)spin,
            (XMouseEvent_modifiers(me) &
             XKeyboardModifier_ControlModifier) != 0);
        XAbstractSpinBox_spinButtonPress((XAbstractSpinBox*)spin, up);
        XEvent_accept(event);
        return;
    }
    XEvent_ignore(event);
}

/** @brief 鼠标移动：按住步进按钮期间保持/切换方向，移出按钮列即停止连发
 *         （对标 Qt QAbstractSpinBox::mouseMoveEvent 的 updateState/reset
 *         分支）；未按住时转发父类。 */
static void VXSpinBox_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XSpinBox* spin = (XSpinBox*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) {
        if (self && event)
            XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                          void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    if (!XAbstractSpinBox_isSpinButtonActive((XAbstractSpinBox*)spin)) {
        XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (spinbox_hitButton(spin, &pos)) {
        XAbstractSpinBox_spinButtonPress((XAbstractSpinBox*)spin,
                                         spinbox_buttonIsUp(spin, &pos));
    } else {
        XAbstractSpinBox_spinButtonRelease((XAbstractSpinBox*)spin);
    }
    XEvent_accept(event);
}

/** @brief 抬起：停止长按连发并复位按钮态（对标 Qt reset）。 */
static void VXSpinBox_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XSpinBox* spin = (XSpinBox*)self;
    if (!spin || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    XAbstractSpinBox_spinButtonRelease((XAbstractSpinBox*)spin);
    XEvent_accept(event);
}

/** @brief 校验虚槽：特殊值恒可接受；剥前后缀后按进制解析并判界。
 *  @details 对齐 Qt QSpinBox::validate → validateAndInterpret
 *           （qspinbox.cpp:1077-1160）：
 *           - stripped 只剥**确实匹配**的前/后缀（缺前缀不判 Invalid——
 *             实测 validate("3") 为 Acceptable）；
 *           - 空串/单独符号按 Intermediate（max!=min 时）；
 *           - 负号但下限非负 ⇒ Invalid（Qt 的 -0 特例）；
 *           - 越界：符号侧越界 Invalid、其余 Intermediate（Qt 同）；
 *           - max==min 且不等于该值 ⇒ Invalid。
 *           此前前缀缺失直接返回 Invalid（gap 95）。 */
static XValidatorState VXSpinBox_validate(XAbstractSpinBox* self,
                                          const char* input, int* pos)
{
    XSpinBox* spin = (XSpinBox*)self;
    char stripped[1024];
    int v;
    bool ok;
    (void)pos;
    if (!spin || !input) return XValidatorState_Invalid;
    if (spinbox_specialText(spin)[0] &&
        XStrcmp(input, spinbox_specialText(spin)) == 0)
        return XValidatorState_Acceptable;
    if (!spinbox_stripText(spin, input, stripped, sizeof(stripped)))
        return XValidatorState_Invalid;
    if (spin->m_max != spin->m_min &&
        (stripped[0] == '\0' ||
         (spin->m_min < 0 && XStrcmp(stripped, "-") == 0) ||
         (spin->m_max >= 0 && XStrcmp(stripped, "+") == 0)))
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
        XMemmove(input, input + 1, o);
        --o;
    }
    while (o > 0 && input[o - 1] == ' ') input[--o] = '\0';
}

/** @brief 前缀文本虚槽（借用；XSpinBox 的自有前缀）。
 *  @details 基类 clear/Shift+Home/End 选区需要前缀长度，Qt 把它放在
 *           QAbstractSpinBoxPrivate；本库前缀归本类，故以虚槽交出。 */
static const char* VXSpinBox_prefixText(const XAbstractSpinBox* self)
{
    return spinbox_prefixText((const XSpinBox*)self);
}

/** @brief 后缀文本虚槽（借用）。 */
static const char* VXSpinBox_suffixText(const XAbstractSpinBox* self)
{
    return spinbox_suffixText((const XSpinBox*)self);
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
    /* 对标 Qt QAbstractSpinBoxPrivate::interpret 首行：
     * `if (type == UnknownType || cleared) return;`——cleared 期间保持
     * 清空态，直到 setValue 或用户重新键入（gap 97）。此前在此清零标志，
     * 失焦提交立刻把旧值回填。 */
    if (self->m_cleared) return;
    t = XLineEdit_text(edit);
    XSnprintf(buf, sizeof(buf), "%s", t ? t : "");
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
        if (spinbox_stripText(spin, buf, core, sizeof(core))) {
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

/** @brief 文本转值虚槽：剥前后缀后解析（失败返回 minimum）。
 *  @details 对标 QSpinBox::valueFromText → validateAndInterpret
 *           （qspinbox.cpp:513-521）：stripped 只剥匹配的前后缀，
 *           缺前缀同样可解析（实测 valueFromText("3")==3）。 */
static int VXSpinBox_valueFromText(XSpinBox* self, const char* text)
{
    char stripped[1024];
    int v;
    bool ok;
    if (!self) return 0;
    if (!text) return self->m_min;
    if (spinbox_specialText(self)[0] &&
        XStrcmp(text, spinbox_specialText(self)) == 0)
        return self->m_min;
    if (!spinbox_stripText(self, text, stripped, sizeof(stripped)))
        return self->m_min;
    v = spinbox_parseValue(self, stripped, &ok);
    return ok ? v : self->m_min;
}

/** @brief 值转文本虚槽：裸数值文本（对标 QSpinBox::textFromValue）。
 *  @details 不含前后缀、不处理特殊值文本——特殊值由 updateEdit
 *           （spinbox_displayText）整段替换（gap 96）。 */
static char* VXSpinBox_textFromValue(XSpinBox* self, int val)
{
    char num[96];
    if (!self) return NULL;
    spinbox_formatValue(self, val, num, sizeof(num));
    return spinbox_strdup(num);
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
    if (self->m_prefix) {
        XClassDelete(self->m_prefix);
        self->m_prefix = NULL;
    }
    if (self->m_suffix) {
        XClassDelete(self->m_suffix);
        self->m_suffix = NULL;
    }
    if (other->m_prefix)
        self->m_prefix = XString_create_copy(other->m_prefix);
    if (other->m_suffix)
        self->m_suffix = XString_create_copy(other->m_suffix);
    /* 基类拷贝重建了编辑框：重新挂接文本变化/提交槽并刷新显示。 */
    edit = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
    if (edit) {
        XObject_connect_2((XObject*)edit,
                          (size_t)XLineEdit_textChanged_signal(edit),
                          spinbox_onTextChanged);
        XObject_connect_2((XObject*)edit,
                          (size_t)XLineEdit_editingFinished_signal(edit),
                          spinbox_onEditingFinished);
        /* P0-5 配套：重建的编辑框未装数字校验器，此处重挂并让
           userData 指向拷贝目标自身（保持与 init 一致的数字门禁，
           也避免 userData 遗留指向源对象）。 */
        XLineEdit_setValidator(edit, spinbox_validateNumeric, self);
        spinbox_refreshText(self);
    }
}

/** @brief 移动语义：基类移动后转移数值字段与前后缀，源对象归默认值。 */
static void VXSpinBox_move(XSpinBox* self, XSpinBox* other)
{
    XLineEdit* edit;
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
    if (self->m_prefix) {
        XClassDelete(self->m_prefix);
        self->m_prefix = NULL;
    }
    if (self->m_suffix) {
        XClassDelete(self->m_suffix);
        self->m_suffix = NULL;
    }
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
    /* P0-5 配套：基类 move 转移的编辑框其校验器 userData 仍指向源
       对象，改指本对象——否则源对象析构后任何键入经校验回调读
       悬垂指针（修复前回调不读 userData，此隐患由 P0-5 激活）。 */
    edit = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
    if (edit)
        XLineEdit_setValidator(edit, spinbox_validateNumeric, self);
}

/** @brief 析构：释放前后缀后转父类。 */
static void VXSpinBox_deinit(XClass* obj)
{
    XSpinBox* self = (XSpinBox*)obj;
    if (!self) return;
    if (self->m_prefix) {
        XClassDelete(self->m_prefix);
        self->m_prefix = NULL;
    }
    if (self->m_suffix) {
        XClassDelete(self->m_suffix);
        self->m_suffix = NULL;
    }
    XClass_Deinit_Parent(XAbstractSpinBox, (XAbstractSpinBox*)obj);
}

/* ==================== 生命周期 ==================== */

/* SizeHint 虚槽桥接（定义在本文件尺寸计算段）。 */
static XSize VXSpinBox_sizeHintSlot(const XWidget* self);
static XSize VXSpinBox_minimumSizeHintSlot(const XWidget* self);

XVtable* XSpinBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XSpinBox)
    XVTABLE_INHERIT_XCLASS(XAbstractSpinBox);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXSpinBox_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXSpinBox_minimumSizeHintSlot);

    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepBy, VXSpinBox_stepBy);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Validate, VXSpinBox_validate);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Fixup, VXSpinBox_fixup);
    /* Clear 不重载：QSpinBox 同样不重载 QAbstractSpinBox::clear——基类
     * 实现已按 PrefixText/SuffixText 虚槽拼出「前缀+后缀」并置 cleared。 */
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_StepEnabled, VXSpinBox_stepEnabled);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_Interpret, VXSpinBox_interpret);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_UpdateEdit, VXSpinBox_updateEdit);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_PrefixText, VXSpinBox_prefixText);
    XVTABLE_OVERLOAD_DEFAULT(EXAbstractSpinBox_SuffixText, VXSpinBox_suffixText);
    XVTABLE_OVERLOAD_DEFAULT(EXSpinBox_ValueFromText, VXSpinBox_valueFromText);
    XVTABLE_OVERLOAD_DEFAULT(EXSpinBox_TextFromValue, VXSpinBox_textFromValue);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXSpinBox_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXSpinBox_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXSpinBox_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXSpinBox_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXSpinBox_deinit);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXSpinBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXSpinBox_move);

    return XVTABLE_DEFAULT;
}

/** @brief 按量程探针量内容宽（对标 Qt QAbstractSpinBox::sizeHint 的
 *         fixedContent 探针，qabstractspinbox.cpp:875-890；minimumSizeHint
 *         同构，:915-930）。
 *
 *  @details Qt 口径：取 textFromValue(minimum)/(maximum) 里更宽者，各
 *           追加 fixedContent 后量宽；specialValueText 单独量（不追加
 *           fixedContent）；最后 `w += 2`（光标闪烁位）。fixedContent =
 *           prefix + suffix + ' '（sizeHint）或 prefix + ' '
 *           （minimumSizeHint——Qt 注释原文 "minimumSizeHint cares about
 *           the prefix"，后缀不计入最小宽）。三段合计给显示留出
 *           「1px 词间 + 2px 光标」余量，文本永不贴死右缘——这是 Qt
 *           防内嵌编辑框按光标横向滚动裁字的兜底。
 *           [本库映射] textFromValue_base 自 2026-11-11 起与 Qt 同契约
 *           （裸数值、不含前后缀/特殊值），故探针直接取该虚槽再拼
 *           fixedContent——与 Qt 的 `fixedContent + s` 组合逐字同构。
 *
 *  @param self       微调框对象。
 *  @param withSuffix true=sizeHint 探针（含后缀）；false=minimumSizeHint
 *                   探针（不含后缀）。
 *  @return 探针内容宽（含 +2 光标位；不小于 0）。
 */
static int spinbox_probeContentWidth(const XSpinBox* self, bool withSuffix)
{
    XFont font;
    char fixed[512];
    char probeBuf[576];
    int w = 0;
    int probe[2];
    int i;
    if (!self) return 0;
    font = XWidget_font((XWidget*)self);
    /* fixedContent = prefix + suffix + ' '（sizeHint）/ prefix + ' '
       （minimumSizeHint）；Qt 追加的那个空格让「最大值文本」不贴死可用区
       右缘（词间距余量）。 */
    if (withSuffix)
        XSnprintf(fixed, sizeof(fixed), "%s%s ",
                  spinbox_prefixText(self), spinbox_suffixText(self));
    else
        XSnprintf(fixed, sizeof(fixed), "%s ", spinbox_prefixText(self));
    probe[0] = XSpinBox_minimum(self);
    probe[1] = XSpinBox_maximum(self);
    for (i = 0; i < 2; ++i) {
        /* 数值段取 textFromValue 虚槽（裸数值，与显示同源：同进制/千分位
           口径），截到 18 字符对标 Qt 的 `s.truncate(18)`
           （qabstractspinbox.cpp:877-884，防病态超长数值拖爆度量）。
           拼接缓冲 576 = fixed(512) + 数值段(18) + NUL + 余量，
           XSnprintf 按容量截尾不越界。 */
        char* bare = XSpinBox_textFromValue_base(self, probe[i]);
        if (!bare) continue;
        if (XStrlen(bare) > 18) bare[18] = '\0';
        XSnprintf(probeBuf, sizeof(probeBuf), "%s%s", fixed, bare);
        {
            const int pw = XPainter_textWidth(&font, probeBuf);
            if (pw > w) w = pw;
        }
        XFree_System(bare);
    }
    /* specialValueText 单独量（Qt :886-889；不追加 fixedContent——
       值==minimum 时整段替换显示，前后缀都让位）。 */
    {
        const char* sp = spinbox_specialText(self);
        if (sp && sp[0]) {
            const int sw = XPainter_textWidth(&font, sp);
            if (sw > w) w = sw;
        }
    }
    XClassDeinit(&font);
    /* 光标闪烁位（Qt :890 `w += 2; // cursor blinking space`）。 */
    w += 2;
    if (w < 0) w = 0;
    return w;
}

/** @brief 计算 sizeHint/minimumSizeHint（Qt 公式 + 本库内嵌编辑框框边修正）。
 *
 *  @details [对齐 Qt 6.8.3 qabstractspinbox.cpp:866-939]
 *           宽 = 探针内容宽（见 spinbox_probeContentWidth）→
 *           style()->sizeFromContents(CT_SpinBox)（补按钮列 + 外框）；
 *           高 = 内嵌编辑框自身 sizeHint/minimumSizeHint 的高（随字体走，
 *           Qt :873/:912）。结果按 Qt 语义缓存在基类
 *           m_cachedSizeHint/m_cachedMinimumSizeHint（查询时按需算，
 *           属性变化时由各 setter/event 失效，见
 *           spinbox_invalidateSizeHints）。
 *           [XGui 特有项：chromeW] Qt 的内嵌编辑框是 frameless
 *           （setLineEdit 里 setFrame(!SH_SpinBox_ButtonsInsideFrame)，
 *           qabstractspinbox.cpp:690），编辑区矩形就是文本区 ⇒ Qt 无此
 *           修正项；本库内嵌 XLineEdit 带框，绘制路径 textStartX/textEnd
 *           各让 frame?4:2 ⇒ 探针宽必须再加编辑框自身左右框边，否则
 *           文本比可用区宽 ⇒ 编辑框按光标横向滚动保后缀、裁前导数字
 *           （现场「10000 ms」显示成「0000 ms」根因，2026-11-10）。
 *           取法与绘制同源：编辑框 sizeHint 减其当前文本度量（不写死
 *           像素，字号/主题自动跟随）。XLineEdit sizeHint 的 40px 下限
 *           只在文本极短时抬高该差值——方向是多给余量，不会造成截断。
 *
 *  @param self    微调框对象。
 *  @param minimum false=sizeHint（探针含后缀）；true=minimumSizeHint
 *                （探针不含后缀，高取编辑框 minimumSizeHint）。
 *  @return 建议尺寸。
 */
static XSize spinbox_buildSizeHint(const XSpinBox* self, bool minimum)
{
    XSize out;
    int w;
    int h = 0;
    int chromeW = 0;  /**< 内嵌编辑框自身的左右边框+边距（绘制路径要占）。 */
    if (!self) { XSize_init(&out, -1, -1); return out; }
    w = spinbox_probeContentWidth(self, !minimum);
    /* 高度取内嵌编辑框自身提示（对标 Qt :873/:912）——含编辑框自己的
       上下框边内缩，随字体走；探针宽度极端小（前缀/后缀全空、量程
       0..0）时也不塌 0。 */
    {
        XLineEdit* edit = self->m_base.m_lineEdit;
        if (edit) {
            XSize eh = minimum
                ? XWidget_minimumSizeHint((XWidget*)edit)
                : XWidget_sizeHint((XWidget*)edit);
            h = eh.height;
            /* chromeW（XGui 特有修正，见函数头注）：编辑框 sizeHint 减
               其当前文本度量 = 左右框边+边距；度量用编辑框自己的字体。
               [两枚提示都加] 最小宽若不含 chrome，按最小宽排布时编辑框
               自身 8px 框边会把**数字**裁掉（Qt 最小宽语义只允许裁后缀
               ——Qt 编辑框 frameless 无此项，本库修正必须与 sizeHint
               同持，才能保住「压缩只裁后缀」的 Qt 失效模式）。取法对
               当前文本有轻值依赖：XLineEdit sizeHint 的 40px 下限把该
               差值钳在 [8,25] 区间，方向恒为多给余量（缓存亦不随
               setValue 失效，宽度不抖动）。 */
            {
                XFont efont;
                const char* lt = XLineEdit_text(edit);
                int ltw = 0;
                efont = XWidget_font((XWidget*)edit);
                if (lt) ltw = XPainter_textWidth(&efont, lt);
                if (eh.width > ltw) chromeW = eh.width - ltw;
                XClassDeinit(&efont);
            }
        }
    }
    if (h <= 0) h = 14;
    /* [步进列/外框按「绘制同源」不动点预算 2026-11-11 走查实锅] 原走
     * sizeFromContents(CT_SpinBox)，其列宽公式代入的是**探针内容高**
     * （编辑框提示高），而绘制侧 xcs_spinBoxColumn 用**控件最终高度**
     * ——HSF 各页 setFixedHeight(40) ⇒ 预算 bh=14/列 22 vs 实绘
     * bh=17/列 26，差的 4~6px 从编辑区里扣（chromeW 加的余量还会被
     * 绘制侧 w/4 项吃掉 1/4）⇒ 「1440 rpm」首位被裁成竖条（变频器页
     * 1280x800/1024x768 走查复现）。改走 XAbstractSpinBox_hintWidthFor：
     * 用样式自己的 subControlRect 做不动点迭代（列高按 min/max 高度
     * 约束夹出的真实高度），与绘制同一条几何真值源；探针宽 + chromeW
     * 语义不变（chromeW=内嵌编辑框自身左右框边，绘制时从编辑区再扣）。 */
    w = XAbstractSpinBox_hintWidthFor((const XAbstractSpinBox*)self,
                                      w + chromeW);
    {
        int frame = XAbstractSpinBox_frameWidthFor(
            (const XAbstractSpinBox*)self);
        XSize_init(&out, w, h + frame * 2);
    }
    return out;
}

/** @brief 查询（并按需重算）尺寸缓存（对标 Qt sizeHint()/minimumSizeHint()
 *         的 `if (d->cached…isEmpty())` 按需计算分支）。
 *
 *  @details 缓存位在基类 XAbstractSpinBox（width<0=未缓存）。写入走的
 *           是「查询时按需回填」而非推送——const 形参下写缓存字段与本库
 *           XLayoutItem_sizeHint_base 的度量缓存同一惯例（只写缓存，
 *           不改变行为语义）。
 *
 *  @param self    微调框对象。
 *  @param minimum false=sizeHint；true=minimumSizeHint。
 *  @return 建议尺寸（缓存优先）。
 */
static XSize spinbox_sizeHintCached(const XSpinBox* self, bool minimum)
{
    XSize* cache;
    if (!self) { XSize out; XSize_init(&out, -1, -1); return out; }
    cache = minimum ? (XSize*)&self->m_base.m_cachedMinimumSizeHint
                    : (XSize*)&self->m_base.m_cachedSizeHint;
    if (cache->width < 0)
        *cache = spinbox_buildSizeHint(self, minimum);
    return *cache;
}

/** @brief SizeHint 虚槽（对标 QAbstractSpinBox::sizeHint）：缓存优先。 */
static XSize VXSpinBox_sizeHintSlot(const XWidget* self)
{
    return spinbox_sizeHintCached((const XSpinBox*)self, false);
}

/** @brief MinimumSizeHint 虚槽（对标 QAbstractSpinBox::minimumSizeHint）：
 *         独立缓存；探针不含后缀（Qt :916 fixedContent = prefix + ' '）。 */
static XSize VXSpinBox_minimumSizeHintSlot(const XWidget* self)
{
    return spinbox_sizeHintCached((const XSpinBox*)self, true);
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
    /* 活动子控件态由基类 m_buttonState 承载（构造态无按住按钮）。 */
    /* 悬停外观 opt-in：绘制按 State_MouseOver 出悬停高亮，ENTER/
     * LEAVE 须标脏自矩形（对标 Qt polish 的 WA_Hover 收口）。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_Hover, true);

    edit = XAbstractSpinBox_lineEdit((XAbstractSpinBox*)self);
    if (edit) {
        spinbox_refreshText(self);
        /* 输入法提示自标注（对标 QSpinBoxPrivate::init 的
           qspinbox.cpp:41 setInputMethodHints(Qt::ImhDigitsOnly)）：落
           内嵌编辑框本体。XGui 焦点代理方向与 Qt 相反（容器代理到编辑
           框、焦点落编辑框本体，见 XAbstractSpinBox.c
           spinbox_createDefaultLineEdit 注释），虚拟键盘守护按焦点控件
           查询 ImHints，故提示须落编辑框（Qt 落 QSpinBox 本体的
           XGui 等价位）。DigitsOnly 属排他掩码段，硬锁数字布局。 */
        XWidget_setInputMethodHints((XWidget*)edit,
                                    XInputMethodHint_DigitsOnly);
        XLineEdit_setValidator(edit, spinbox_validateNumeric, self);
        XObject_connect_2((XObject*)edit,
                          (size_t)XLineEdit_textChanged_signal(edit),
                          spinbox_onTextChanged);
        XObject_connect_2((XObject*)edit,
                          (size_t)XLineEdit_editingFinished_signal(edit),
                          spinbox_onEditingFinished);
    }
    /* [sizeHint 补齐 2026-10-27 → 缓存化 2026-11-10] 建议尺寸不再于
     * init 期推送：sizeHint/minimumSizeHint 虚槽已按 Qt 语义缓存优先
     * （spinbox_sizeHintCached），首次布局协商时按需计算——构造期无
     * 通知与 Qt 构造口径一致（Qt 构造函数同样不预计算 sizeHint）。 */
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

/** @brief 失效两枚尺寸缓存（本文件侧收口；同 XAbstractSpinBox.c 的
 *         spinbox_invalidateSizeHints——两处各自 file-static，Qt 对应的
 *         缓存清零同样是私有字段直写）。 */
static void spinboxInvalidateHints(XSpinBox* self, bool alsoMinimum)
{
    if (!self) return;
    XSize_init(&self->m_base.m_cachedSizeHint, -1, -1);
    if (alsoMinimum)
        XSize_init(&self->m_base.m_cachedMinimumSizeHint, -1, -1);
}

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
    /* 对标 Qt QAbstractSpinBoxPrivate::setRange（qabstractspinbox.cpp
     * :1808-1827）：量程进探针 ⇒ 两枚缓存都失效 + updateGeometry 通知
     * 布局重协商（0..50 与 0..1440 差一位数，通知不到位则新值被旧宽度
     * 裁掉——历史实锅）。 */
    spinboxInvalidateHints(self, true);
    XWidget_updateGeometry((XWidget*)self);
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
    /* 对标 Qt QAbstractSpinBox::setValue（qabstractspinbox.cpp:1750-1768：
     * `cleared = false; updateEdit();` 无条件执行）——clear() 后
     * setValue(同值) 也要恢复显示文本（探针实测）。 */
    self->m_base.m_cleared = false;
    if (clamped == self->m_value) {
        spinbox_refreshText(self);
        return;
    }
    self->m_value = clamped;
    self->m_textDirty = false;
    spinbox_refreshText(self);
    /* 对标 Qt：setValue 不失效尺寸缓存（sizeHint 按 min/max 量宽，与
       当前值无关；此前「文本宽度随值变化就刷新」是旧口径残段）。 */
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
    return spinbox_prefixText(self);
}

void XSpinBox_setPrefix(XSpinBox* self, const char* prefix)
{
    if (!self) return;
    if (!prefix) prefix = "";
    {
        const char* cur = self->m_prefix
            ? XString_toUtf8(self->m_prefix) : NULL;
        if (cur && XStrcmp(cur, prefix) == 0) return;
        if (!cur && prefix[0] == '\0') return;
    }
    if (!self->m_prefix) self->m_prefix = XString_create();
    if (self->m_prefix)
        XString_assign_utf8(self->m_prefix, prefix);
    spinbox_refreshText(self);
    /* 对标 Qt QSpinBox::setPrefix（qspinbox.cpp:223-233）：前缀进
       sizeHint 与 minimumSizeHint 两枚探针 ⇒ 两枚缓存都失效 +
       updateGeometry（"minimumSizeHint cares about the prefix"）。 */
    spinboxInvalidateHints(self, true);
    XWidget_updateGeometry((XWidget*)self);
}

const char* XSpinBox_suffix(const XSpinBox* self)
{
    return spinbox_suffixText(self);
}

void XSpinBox_setSuffix(XSpinBox* self, const char* suffix)
{
    if (!self) return;
    if (!suffix) suffix = "";
    {
        const char* cur = self->m_suffix
            ? XString_toUtf8(self->m_suffix) : NULL;
        if (cur && XStrcmp(cur, suffix) == 0) return;
        if (!cur && suffix[0] == '\0') return;
    }
    if (!self->m_suffix) self->m_suffix = XString_create();
    if (self->m_suffix)
        XString_assign_utf8(self->m_suffix, suffix);
    spinbox_refreshText(self);
    /* 对标 Qt QSpinBox::setSuffix（qspinbox.cpp:261-271）：后缀只进
       sizeHint 探针 ⇒ 只失效 sizeHint 缓存 + updateGeometry
       （minimumSizeHint 不量后缀——Qt 注释口径，切调速方式后控件宽度
       跟着换；不算后缀则带单位数字框偏窄、文本被裁——历史实锅）。 */
    spinboxInvalidateHints(self, false);
    XWidget_updateGeometry((XWidget*)self);
}

char* XSpinBox_cleanText(const XSpinBox* self)
{
    char stripped[1024];
    const char* text;
    if (!self) return XStrdup("");
    text = self->m_base.m_lineEdit
        ? XLineEdit_text(self->m_base.m_lineEdit) : "";
    /* 特殊值文本整段匹配：原样返回（Qt stripped 对 specialValueText
     * 不剥任何东西，实测 cleanText()=='auto'）。 */
    if (spinbox_specialText(self)[0] &&
        XStrcmp(text, spinbox_specialText(self)) == 0)
        return XStrdup(text);
    /* 只剥前/后缀与首尾空白，**保留千分位分隔符**（Qt stripped 口径；
     * 实测 groupSeparatorShown 下 cleanText=='1,234,567'，gap 94）。 */
    if (!spinbox_stripText(self, text, stripped, sizeof(stripped)))
        return XStrdup(text);
    return XStrdup(stripped);
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
    /* 对标 Qt setDisplayIntegerBase（qspinbox.cpp:445-459）：只刷新显示
       文本，不失效尺寸缓存（Qt 原样：换进制后 sizeHint 缓存保持旧进制
       的宽度，直至量程/前后缀/字体/样式变化触发失效——Qt 行为保留）。 */
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
















#endif /* XWIDGET_ON && XSPINBOX_ON && XLINEEDIT_ON && XABSTRACTSPINBOX_ON */

#endif /* XGUI_ON */
