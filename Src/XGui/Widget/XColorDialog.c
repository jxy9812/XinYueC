/******************************************************************************
 * @file       XColorDialog.c
 * @brief      颜色对话框控件实现（对标 Qt 6.8 QColorDialog 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。setCurrentColor 在颜色
 *             实际变化时发射 currentColorChanged；colorSelected 由应用在
 *             接受动作处手动触发。getColor 无 GUI 环境直接返回 initial。
 * @note       本文件不依赖任何平台 API；原生颜色面板为后续扩展。
 * @author     XinYueC 团队
 */

#include "XGuiConfig.h"
#include "XString.h"
#include "XMemory.h"
#include "XVarList.h"
#include "XEvent.h"
#include "XColor.h"
/* 真实弹窗依赖（对标 Qt QColorDialog::getColor 的对话框组装路径）： */
#include "XCoreApplication.h"  /* qApp 等价物：有应用实例才允许模态循环 */
#include "XGuiApplication.h"   /* 主屏查询（弹窗居中） */
#include "XScreen.h"           /* 屏幕几何 */
#include "XLabel.h"            /* R/G/B 标签 */
#include "XSpinBox.h"          /* R/G/B 分量输入 */
#include "XPushButton.h"       /* 确定/取消 */
#include "XBoxLayout.h"        /* 对话框布局 */
#include "XPainter.h"          /* 色块网格/预览块绘制 */

#if XWIDGET_ON && XDIALOG_ON

#include "XColorDialog.h"
#include "XWidget_Protected.h"

/* ==================== 内部辅助 ==================== */

/** @brief 发射携带 XColor 值参数的信号。 */
static void xcolordialog_emitColor(XColorDialog* self, size_t signal,
                                   XColor color)
{
    XVarList* args = XVarList_Create(XVar(XColor, color));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 类与实例生命周期 ==================== */

XVtable* XColorDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XColorDialog)
    XVTABLE_INHERIT_XCLASS(XDialog);
    return XVTABLE_DEFAULT;
}

void XColorDialog_init(XColorDialog* self, XColor initial, XWidget* parent,
                       XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XColorDialog);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_currentColor = initial;
    self->m_selectedColor = initial;
    self->m_options = 0;
}

void XColorDialog_init_default(XColorDialog* self, XWidget* parent)
{
    XColorDialog_init(self, XColor_White, parent, 0);
}

XColorDialog* XColorDialog_create_ex(XMemoryType memory, XColor initial,
                                     XWidget* parent, XWidgetFlags flags)
{
    XColorDialog* self = (XColorDialog*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XColorDialog_init(self, initial, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 实例属性 ==================== */

void XColorDialog_setCurrentColor(XColorDialog* self, XColor color)
{
    if (!self) return;
    if (XColor_equals(&self->m_currentColor, &color))
        return;
    self->m_currentColor = color;
    xcolordialog_emitColor(self, (size_t)XColorDialog_currentColorChanged_signal,
                           color);
}

XColor XColorDialog_currentColor(const XColorDialog* self)
{
    return self ? self->m_currentColor : XColor_create();
}

XColor XColorDialog_selectedColor(const XColorDialog* self)
{
    return self ? self->m_selectedColor : XColor_create();
}

void XColorDialog_setOption(XColorDialog* self, XColorDialogOption option, bool on)
{
    if (!self) return;
    if (on) self->m_options |= (XColorDialogOptions)option;
    else self->m_options &= (XColorDialogOptions)~option;
}

bool XColorDialog_testOption(const XColorDialog* self, XColorDialogOption option)
{
    return self ? (self->m_options & (XColorDialogOptions)option) != 0 : false;
}

void XColorDialog_setOptions(XColorDialog* self, XColorDialogOptions options)
{ if (self) self->m_options = options; }

XColorDialogOptions XColorDialog_options(const XColorDialog* self)
{ return self ? self->m_options : 0; }

/* ==================== 静态便捷函数（真实弹窗） ====================
 * 对标 Qt QColorDialog::getColor：构造 XDialog + 标准色块网格（8 列×
 * 6 行=48 槽位）+ R/G/B 分量输入 + 预览块 + 确定/取消，经 XDialog_exec
 * 阻塞式模态循环（应用模态、Escape→reject）；无 GUI 环境（无
 * XCoreApplication 实例，如无头测试）保持桩约定：直接返回 initial。 */

/** @brief 色块网格列数（对标 QColorDialog 标准色区 8 列布局）。 */
#define XCD_GRID_COLS 8
/** @brief 色块网格行数（对标 QColorDialog 标准色区 6 行布局）。 */
#define XCD_GRID_ROWS 6

/** @brief GUI 环境探测：存在 XCoreApplication 实例才执行真实模态循环。 */
static bool xcd_guiReady(void)
{
    return XCoreApplication_instance() != NULL;
}

/** @brief 以 UTF-8 设置对象 objectName（对标 QObject::setObjectName）。 */
static void xcd_setName(XObject* obj, const char* name)
{
    XString tmp;
    if (!obj) return;
    XString_init(&tmp);
    XString_assign_utf8(&tmp, name);
    XObject_setObjectName(obj, &tmp);
    XClass_deinit_base((XClass*)&tmp);
}

/* 子控件 objectName 常量（对标 Qt 对话框私有子对象命名；槽内经
 * findChild 取回）。 */
#define XCD_NAME_GRID    "qt_color_dialog_grid"
#define XCD_NAME_PREVIEW "qt_color_dialog_preview"
#define XCD_NAME_R       "qt_color_dialog_r"
#define XCD_NAME_G       "qt_color_dialog_g"
#define XCD_NAME_B       "qt_color_dialog_b"
#define XCD_NAME_OK      "qt_color_dialog_ok"
#define XCD_NAME_CANCEL  "qt_color_dialog_cancel"

/** @brief 按 objectName 查找对话框直接子控件（对标 QObject::findChild）。 */
static XWidget* xcd_childByName(XDialog* dlg, const char* name)
{
    XString tmp;
    XWidget* w;
    if (!dlg) return NULL;
    XString_init(&tmp);
    XString_assign_utf8(&tmp, name);
    w = (XWidget*)XObject_findChild((XObject*)dlg, &tmp,
                                    XFindDirectChildrenOnly);
    XClass_deinit_base((XClass*)&tmp);
    return w;
}

/** @brief 弹窗主屏居中（对标 Qt 静态便捷函数把对话框定位于屏幕中央）。 */
static void xcd_centerOnScreen(XWidget* w)
{
    XScreen* screen;
    XRect g;
    if (!w) return;
    screen = XGuiApplication_primaryScreen();
    if (!screen) return;
    g = XScreen_geometry(screen);
    if (g.width <= 0 || g.height <= 0) return;
    XWidget_move(w, g.x + (g.width - XWidget_width(w)) / 2,
                    g.y + (g.height - XWidget_height(w)) / 2);
}

/** @brief 初始化 48 槽位标准色表。
 * @details 布局对标 QColorDialog 标准色区（8 列×6 行）；具体色值为
 *          XGui 简化生成——第 0 列灰阶六档，第 1..7 列按色相 60° 步进
 *          × 饱和度/明度阵列（纯色/浅色/更浅/较暗/更暗/极暗），未
 *          复刻 Qt 内置色表数值（@note）。 */
static void xcd_initStandardTable(XColorDialog* dlg)
{
    int r, c;
    if (!dlg) return;
    for (r = 0; r < XCD_GRID_ROWS; ++r) {
        for (c = 0; c < XCD_GRID_COLS; ++c) {
            XColor color;
            if (c == 0) {
                int gray = r * 51; /* 0,51,102,153,204,255 灰阶列。 */
                color = XColor_create_rgb(gray, gray, gray, 255);
            } else {
                int hue = (c - 1) * 60; /* 0,60,...,360 色相列。 */
                int s, v;
                switch (r) {
                case 0: s = 255; v = 255; break; /* 纯色。 */
                case 1: s = 128; v = 255; break; /* 较浅。 */
                case 2: s = 51;  v = 255; break; /* 更浅。 */
                case 3: s = 255; v = 204; break; /* 较暗。 */
                case 4: s = 255; v = 128; break; /* 更暗。 */
                default: s = 255; v = 76; break; /* 极暗。 */
                }
                color = XColor_create_hsv(hue % 360, s, v, 255);
            }
            XColorDialog_setStandardColor(dlg, r * XCD_GRID_COLS + c, color);
        }
    }
}

/* ---------- 色块控件：网格选择 + 单色预览（本地派生 XWidget，参照
 * XComboBox.c 内嵌 XComboEdit 的本地子类方案） ---------- */
XCLASS_DEFINE_BEGING(XColorSwatch)
XCLASS_DEFINE_EXTEND_END(XColorSwatch, XWidget)

/** @brief 色块控件对象；m_base 必须是第一个成员。 */
typedef struct XColorSwatch
{
    XWidget m_base;        /**< 基类成员；必须是第一个。 */
    XColorDialog* m_owner; /**< 属主对话框（借用；可为 NULL）。 */
    bool m_grid;           /**< true=标准色网格（可点击选色）；false=单色预览块。 */
    int m_selected;        /**< 网格选中下标（-1 未选中）。 */
} XColorSwatch;

static void VXColorSwatch_paintEvent(XWidget* self, XEvent* event);
static void VXColorSwatch_mousePressEvent(XWidget* self, XEvent* event);

/** @brief 色块控件虚表：覆写绘制与按下命中，其余继承 XWidget。 */
static XVtable* XColorSwatch_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XColorSwatch)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXColorSwatch_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXColorSwatch_mousePressEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建色块控件（堆分配；grid=true 网格选择、false 单色预览）。 */
static XColorSwatch* xcd_swatchCreate(XColorDialog* owner, bool grid)
{
    XColorSwatch* sw = (XColorSwatch*)XMemory_malloc(sizeof(*sw),
                                                    XCLASS_DEFAULT_MEMORY_TYPE);
    if (!sw) return NULL;
    XMemset(sw, 0, sizeof(*sw));
    XWidget_init(&sw->m_base, (XWidget*)owner, 0);
    XClassSetVtable(sw, XColorSwatch);
    sw->m_owner = owner;
    sw->m_grid = grid;
    sw->m_selected = -1;
    Set_Class_Memory(sw, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(sw, true);
    XWidget_setAutoFillBackground(&sw->m_base, false);
    return sw;
}

/** @brief 绘制：网格模式画 48 色块 + 选中高亮；预览模式整块填当前色。 */
static void VXColorSwatch_paintEvent(XWidget* self, XEvent* event)
{
    XColorSwatch* sw = (XColorSwatch*)self;
    XImage* image;
    XPoint offset;
    XPainter painter;
    int w = XWidget_width(self);
    int h = XWidget_height(self);
    if (!sw || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    image = XWidget_paintImage(self);
    if (!image) return; /* 未上屏/未创建后备存储时无绘制目标。 */
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
    if (sw->m_grid) {
        XColorDialog* dlg = sw->m_owner;
        XRect cell;
        int cw = w > 0 ? w / XCD_GRID_COLS : 0;
        int ch = h > 0 ? h / XCD_GRID_ROWS : 0;
        int r, c;
        cell.width = cw > 2 ? cw - 2 : cw;
        cell.height = ch > 2 ? ch - 2 : ch;
        for (r = 0; r < XCD_GRID_ROWS; ++r) {
            for (c = 0; c < XCD_GRID_COLS; ++c) {
                int idx = r * XCD_GRID_COLS + c;
                XColor color =
                    XColorDialog_standardColor(dlg, idx);
                cell.x = c * cw + 1;
                cell.y = r * ch + 1;
                /* 选中槽位：先铺白色外圈再内缩填色（简化高亮描边）。 */
                if (idx == sw->m_selected) {
                    XRect halo = cell;
                    halo.x -= 1;
                    halo.y -= 1;
                    halo.width += 2;
                    halo.height += 2;
                    XPainter_fillRect(&painter, &halo, 0xFFFFFFFFu);
                }
                XPainter_fillRect(&painter, &cell, XColor_rgba(&color));
            }
        }
    } else {
        XRect full;
        XColor color = sw->m_owner
            ? sw->m_owner->m_currentColor : XColor_create();
        full.x = 0;
        full.y = 0;
        full.width = w;
        full.height = h;
        XPainter_fillRect(&painter, &full, XColor_rgba(&color));
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 同步 R/G/B 三个旋入框与预览块到当前色（点击色块后调用）。 */
static void xcd_syncInputs(XColorDialog* dlg)
{
    XSpinBox* r = (XSpinBox*)xcd_childByName(&dlg->m_base, XCD_NAME_R);
    XSpinBox* g = (XSpinBox*)xcd_childByName(&dlg->m_base, XCD_NAME_G);
    XSpinBox* b = (XSpinBox*)xcd_childByName(&dlg->m_base, XCD_NAME_B);
    XWidget* preview =
        xcd_childByName(&dlg->m_base, XCD_NAME_PREVIEW);
    if (r) XSpinBox_setValue(r, XColor_red(&dlg->m_currentColor));
    if (g) XSpinBox_setValue(g, XColor_green(&dlg->m_currentColor));
    if (b) XSpinBox_setValue(b, XColor_blue(&dlg->m_currentColor));
    if (preview) XWidget_update(preview);
}

/** @brief 网格命中：按下色块置当前色（对标 QColorDialog 点击标准色格）。 */
static void VXColorSwatch_mousePressEvent(XWidget* self, XEvent* event)
{
    XColorSwatch* sw = (XColorSwatch*)self;
    if (sw && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        XPoint pos = XMouseEvent_position((XMouseEvent*)event);
        int w = XWidget_width(self);
        int h = XWidget_height(self);
        int cw = w > 0 ? w / XCD_GRID_COLS : 0;
        int ch = h > 0 ? h / XCD_GRID_ROWS : 0;
        if (sw->m_grid && cw > 0 && ch > 0 && sw->m_owner) {
            int c = pos.x / cw;
            int r = pos.y / ch;
            if (c >= 0 && c < XCD_GRID_COLS && r >= 0 && r < XCD_GRID_ROWS) {
                int idx = r * XCD_GRID_COLS + c;
                if (idx < sw->m_owner->m_standardCount) {
                    sw->m_selected = idx;
                    XColorDialog_setCurrentColor(
                        sw->m_owner,
                        XColorDialog_standardColor(sw->m_owner, idx));
                    xcd_syncInputs(sw->m_owner);
                    XWidget_update(self);
                }
            }
            XEvent_accept(event);
            return;
        }
    }
    XClass_Parent(XWidget, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief R/G/B 旋入框联动槽：任一分量变化 → 重组当前色并刷新预览
 *  （对标 QColorDialog RGB 分量编辑实时联动 currentColor）。 */
static void xcd_rgbChangedSlot(XObject* receiver, XVarList* args)
{
    XColorDialog* dlg = (XColorDialog*)receiver;
    XSpinBox* r;
    XSpinBox* g;
    XSpinBox* b;
    (void)args;
    if (!dlg) return;
    r = (XSpinBox*)xcd_childByName(&dlg->m_base, XCD_NAME_R);
    g = (XSpinBox*)xcd_childByName(&dlg->m_base, XCD_NAME_G);
    b = (XSpinBox*)xcd_childByName(&dlg->m_base, XCD_NAME_B);
    if (!r || !g || !b) return;
    XColorDialog_setCurrentColor(
        dlg, XColor_create_rgb(XSpinBox_value(r), XSpinBox_value(g),
                               XSpinBox_value(b), 255));
    XWidget_update(xcd_childByName(&dlg->m_base, XCD_NAME_PREVIEW));
}

/** @brief 确定/取消槽连接对（按钮文本与 XDialogButtonBox 标准一致）。 */
static void xcd_acceptSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (!receiver) return;
    /* 对标 QColorDialog::accept：selectedColor = currentColor 并发射
     * colorSelected（经既有信号函数完成）。 */
    XColorDialog_colorSelected_signal((XColorDialog*)receiver,
                                      ((XColorDialog*)receiver)->m_currentColor);
    XDialog_accept((XDialog*)receiver);
}

static void xcd_rejectSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver) XDialog_reject((XDialog*)receiver);
}

XColor XColorDialog_getColor(XColor initial, XWidget* parent,
                             const XString* title, XColorDialogOptions options)
{
    XColorDialog* dlg;
    XBoxLayout* root = NULL;
    XBoxLayout* bar = NULL;
    XBoxLayout* rgbRow = NULL;
    bool accepted;
    if (!xcd_guiReady()) {
        /* 无 GUI 环境（无头测试）：不做模态执行，返回 initial（桩约定）。 */
        dlg = XColorDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, initial,
                                     parent, 0);
        if (!dlg) return initial;
        if (title)
            XWidget_setWindowTitle((XWidget*)dlg, title);
        XColorDialog_setOptions(dlg, options);
        XColorDialog_delete_base(dlg);
        return initial;
    }
    dlg = XColorDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, initial, parent,
                                 (XWidgetFlags)XWindowType_Dialog);
    if (!dlg) return initial;
    if (title)
        XWidget_setWindowTitle((XWidget*)dlg, title);
    XColorDialog_setOptions(dlg, options);
    xcd_initStandardTable(dlg);
    root = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, (XWidget*)dlg);
    if (!root) {
        XColorDialog_delete_base(dlg);
        return initial;
    }
    XLayout_setContentsMargins((XLayout*)root, 12, 12, 12, 12);
    XLayout_setSpacing((XLayout*)root, 8);
    {
        /* 标准色网格（8×6，可点击选色，对标 QColorDialog 标准色区）。 */
        XColorSwatch* grid = xcd_swatchCreate(dlg, true);
        if (grid) {
            xcd_setName((XObject*)grid, XCD_NAME_GRID);
            XWidget_setMinimumSize((XWidget*)grid, 224, 168);
            XBoxLayout_addWidget(root, (XWidget*)grid);
        }
        /* R/G/B 分量行（对标 QColorDialog RGB 编辑区）。 */
        rgbRow = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
        if (rgbRow) {
            static const char* const names[3] = { "R", "G", "B" };
            static const char* const slots[3] =
                { XCD_NAME_R, XCD_NAME_G, XCD_NAME_B };
            int i;
            int comps[3];
            XColor_getRgb(&dlg->m_currentColor, &comps[0], &comps[1],
                          &comps[2], NULL);
            for (i = 0; i < 3; ++i) {
                XLabel* lb = XLabel_create((XWidget*)dlg, 0);
                XSpinBox* spin;
                if (lb) {
                    XLabel_setText_2(lb, names[i]);
                    XBoxLayout_addWidget(rgbRow, (XWidget*)lb);
                }
                spin = XSpinBox_create((XWidget*)dlg, 0);
                if (spin) {
                    XSpinBox_setRange(spin, 0, 255);
                    XSpinBox_setValue(spin, comps[i]);
                    xcd_setName((XObject*)spin, slots[i]);
                    XWidget_setMinimumSize((XWidget*)spin, 72, 24);
                    XBoxLayout_addWidget(rgbRow, (XWidget*)spin);
                    XObject_connect_1((XObject*)spin,
                                      (size_t)XSpinBox_valueChanged_signal,
                                      (XObject*)dlg, xcd_rgbChangedSlot,
                                      XConnectionType_Direct);
                }
            }
            XBoxLayout_addLayout(root, (XLayout*)rgbRow);
        }
        /* 预览块（对标 QColorDialog 预览区，随当前色刷新）。 */
        {
            XColorSwatch* preview = xcd_swatchCreate(dlg, false);
            if (preview) {
                xcd_setName((XObject*)preview, XCD_NAME_PREVIEW);
                XWidget_setMinimumSize((XWidget*)preview, 224, 36);
                XBoxLayout_addWidget(root, (XWidget*)preview);
            }
        }
        if (!(options & (XColorDialogOptions)XColorDialog_NoButtons)) {
            XPushButton* ok = XPushButton_create((XWidget*)dlg, 0);
            XPushButton* cancel = XPushButton_create((XWidget*)dlg, 0);
            bar = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
            if (bar) {
                XBoxLayout_addStretch(bar, 1);
                if (ok) {
                    XAbstractButton_setText_2((XAbstractButton*)ok, "确定");
                    XWidget_setMinimumSize((XWidget*)ok, 80, 28);
                    xcd_setName((XObject*)ok, XCD_NAME_OK);
                    XBoxLayout_addWidget(bar, (XWidget*)ok);
                    XObject_connect_1((XObject*)ok,
                                      (size_t)XAbstractButton_clicked_signal,
                                      (XObject*)dlg, xcd_acceptSlot,
                                      XConnectionType_Direct);
                }
                if (cancel) {
                    XAbstractButton_setText_2((XAbstractButton*)cancel, "取消");
                    XWidget_setMinimumSize((XWidget*)cancel, 80, 28);
                    xcd_setName((XObject*)cancel, XCD_NAME_CANCEL);
                    XBoxLayout_addWidget(bar, (XWidget*)cancel);
                    XObject_connect_1((XObject*)cancel,
                                      (size_t)XAbstractButton_clicked_signal,
                                      (XObject*)dlg, xcd_rejectSlot,
                                      XConnectionType_Direct);
                }
                XBoxLayout_addLayout(root, (XLayout*)bar);
            }
        }
    }
    /* 阻塞模态执行（复用 XDialog exec：应用模态 + Escape→reject）。 */
    XWidget_resize((XWidget*)dlg, 320, 340);
    xcd_centerOnScreen((XWidget*)dlg);
    accepted = XDialog_exec(&dlg->m_base) == 1;
    {
        XColor result;
        if (accepted)
            result = XColorDialog_selectedColor(dlg);
        else
            result = XColor_create(); /* 对标 Qt：取消返回失效颜色。 */
        if (root) XLayout_delete_base((XLayout*)root);
        if (rgbRow) XLayout_delete_base((XLayout*)rgbRow);
        if (bar) XLayout_delete_base((XLayout*)bar);
        XColorDialog_delete_base(dlg);
        return result;
    }
}

XColor XColorDialog_getColor_2(XColor initial, XWidget* parent,
                               const char* title, XColorDialogOptions options)
{
    XString* t = title ? XString_create_utf8(title) : NULL;
    XColor result = XColorDialog_getColor(initial, parent, t, options);
    if (t) XString_delete_base((XClass*)t);
    return result;
}

/* ==================== 信号 ==================== */

void* XColorDialog_currentColorChanged_signal(XColorDialog* self, XColor color)
{
    xcolordialog_emitColor(self, (size_t)XColorDialog_currentColorChanged_signal,
                           color);
    return (void*)(size_t)XColorDialog_currentColorChanged_signal;
}

void* XColorDialog_colorSelected_signal(XColorDialog* self, XColor color)
{
    if (self)
        self->m_selectedColor = color;
    xcolordialog_emitColor(self, (size_t)XColorDialog_colorSelected_signal,
                           color);
    return (void*)(size_t)XColorDialog_colorSelected_signal;
}


/* ==================== Task 2.21 回检补齐：自定义/标准颜色 ============== */

int XColorDialog_customCount(const XColorDialog* self)
{
    return self ? self->m_customCount : 0;
}

void XColorDialog_setCustomColor(XColorDialog* self, int index, XColor color)
{
    if (!self || index < 0 || index >= 16) return;
    self->m_customColors[index] = color;
    if (index >= self->m_customCount)
        self->m_customCount = index + 1;
}

XColor XColorDialog_customColor(const XColorDialog* self, int index)
{
    XColor black;
    XColor_init_rgb(&black, 0, 0, 0, 0);
    if (!self || index < 0 || index >= 16) return black;
    return self->m_customColors[index];
}

void XColorDialog_setStandardColor(XColorDialog* self, int index, XColor color)
{
    if (!self || index < 0 || index >= 48) return;
    self->m_standardColors[index] = color;
    if (index >= self->m_standardCount)
        self->m_standardCount = index + 1;
}

XColor XColorDialog_standardColor(const XColorDialog* self, int index)
{
    XColor black;
    XColor_init_rgb(&black, 0, 0, 0, 0);
    if (!self || index < 0 || index >= 48) return black;
    return self->m_standardColors[index];
}

void XColorDialog_open(XColorDialog* self)
{
    if (!self) return;
    XWidget_show((XWidget*)self);
}

#endif /* XWIDGET_ON && XDIALOG_ON */