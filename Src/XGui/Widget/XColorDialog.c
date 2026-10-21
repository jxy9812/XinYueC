/******************************************************************************
 * @file       XColorDialog.c
 * @brief      颜色对话框控件实现（对标 Qt 6.8 QColorDialog 公共 API）。
 * @details    与同名头文件的公共 API 一一对应。setCurrentColor 在颜色
 *             实际变化时发射 currentColorChanged；colorSelected 由应用在
 *             接受动作处手动触发。getColor 无 GUI 环境直接返回 initial。
 * @note       本文件不依赖任何平台 API；原生颜色面板为后续扩展。
 * @author     XinYueC 团队
 */

#if XGUI_ON

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
#if XDIALOGBUTTONBOX_ON
#include "XDialogButtonBox.h"  /* 标准按钮行（对标 QDialogButtonBox） */
#endif

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

/* 自定义/标准颜色表：进程级共享存储（对标 Qt QColorDialog::customCount/
 * customColor/setCustomColor/standardColor/setStandardColor 五函数的
 * static 语义，qcolordialog.h:60-64——自定义颜色全进程共享）。本库
 * 保留 per-instance API 形态（已定版的形态决策），但存储由 per-instance
 * 字段改为文件级静态表：多对话框实例读写同一份表，与 Qt 行为一致。
 * 零初始化即全零 rgba（无效色/black 语义，XColor_create() 同值）。 */
static XColor g_xcdCustomColors[16];
static XColor g_xcdStandardColors[48];
static int g_xcdCustomCount;   /**< 已设自定义槽计数（高水位 +1）。 */
static int g_xcdStandardCount; /**< 已设标准槽计数（高水位 +1）。 */

/* ==================== 类与实例生命周期 ==================== */

static void VXColorDialog_showEvent(XWidget* self, XEvent* event);

XVtable* XColorDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XColorDialog)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXColorDialog_showEvent);
    return XVTABLE_DEFAULT;
}

void XColorDialog_init(XColorDialog* self, XColor initial, XWidget* parent,
                       XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    /* 固定/可拉伸归类（2026-10-02 所有者指令②：逐类抓 Qt 6.8.3 原文
     * 核实，qcolordialog.cpp 判固定类）——initWidgets() 落
     * QLayout::SetFixedSize（窗口恒随布局 sizeHint 吸附、用户拖边无
     * 效），构造尾再 setSizeGripEnabled(false) 显式去尺寸把手；全文无
     * MSWindowsFixedSizeDialogHint 字面位，固定语义由布局约束承载。
     * XGui 布局模型无 SetFixedSize 等位，按消息族同型以该提示位声明
     * 固定窗口（XMessageBox_init:765 先例；窗口类型位尊重调用方，不
     * 在此叠加）。 */
    flags |= (XWidgetFlags)XWindowType_MSWindowsFixedSizeDialogHint;
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
/** @brief 预览块描边色（与 XDialog 面板边框同族的 1px 中性灰）。 */
#define XCD_PREVIEW_BORDER_COLOR 0xFFB0B0B0u
/** @brief 对话框宽度下限（r1 猎捕实证合格常量，仅兜底布局 hint）。 */
#define XCD_MIN_WIDTH 320
/** @brief 对话框高度下限（r1 猎捕实证合格常量，仅兜底布局 hint）。 */
#define XCD_MIN_HEIGHT 340

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
    XClassDeinit((XClass*)&tmp);
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
                                    XFindChildOption_DirectChildrenOnly);
    XClassDeinit((XClass*)&tmp);
    return w;
}

/** @brief 弹窗主屏居中（对标 Qt 静态便捷函数把对话框定位于屏幕中央）。 */
static void xcd_centerOnScreen(XWidget* w)
{
    /* 窗口形态对话框（XDialog init 对无类型位叠加 Dialog 类型）几何
     * 为全局屏幕坐标：居中于父级顶层窗口（对标 QDialogPrivate::
     * adjustPosition）；子控件形态（历史/改型）几何为父系坐标，居
     * 中于父控件。无父时回退屏幕居中。 */
    XWidget* parent = w ? XWidget_parentWidget(w) : NULL;
    if (parent && w->m_isWindow) {
        XWidget* ptop = XWidget_topLevelWidget(parent);
        if (ptop && ptop != w) {
            XPoint origin;
            XPoint po;
            int dw = XWidget_width(w);
            int dh = XWidget_height(w);
            int tw = XWidget_width(ptop);
            int th = XWidget_height(ptop);
            XPoint_init(&origin, 0, 0);
            po = XWidget_mapToGlobal(ptop, &origin);
            XWidget_move(w, tw > dw ? po.x + (tw - dw) / 2 : po.x,
                            th > dh ? po.y + (th - dh) / 2 : po.y);
            return;
        }
    }
    if (parent) {
        int pw = XWidget_width(parent);
        int ph = XWidget_height(parent);
        int dw = XWidget_width(w);
        int dh = XWidget_height(w);
        XWidget_move(w, pw > dw ? (pw - dw) / 2 : 0,
                        ph > dh ? (ph - dh) / 2 : 0);
        return;
    }
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
}

/** @brief 按布局 sizeHint 定尺（对标 qcolordialog.cpp initWidgets 的
 *  mainLay->setSizeConstraint(QLayout::SetFixedSize)——窗口恒随布局
 *  sizeHint 吸附，尺寸是字体度量/边距/装饰条的函数，不再手写像素
 *  预算）。
 *  @details Windows Segoe UI 截图三连（旋入框半裁/按钮浮出窗外/预览
 *           条突兀）根因：旧实现以 setFixedSize(max(hint,320x340)) 在
 *           exec 预定期锁尺，而 XDialog 的 CSD 避让链
 *           （xdlg_applyContentAvoidance）在 SHOW 时才把框架自绘标题
 *           条高增量写入根布局顶边距——锁定高度不含条高，内容合计
 *           =窗高+条高，qGeomCalc 亏缺分支让后段条目相互覆盖/越出底
 *           缘。本函数两处调用收口：
 *           - exec 预定尺（addDecorationTop=true）：布局顶边距尚未含
 *             避让，显式外加装饰条高预测（XInputDialog exec 定尺同
 *             款；建窗前按控件侧 flags 预测，XWindowDecoration
 *             activeFor 契约）；
 *           - SHOW 精修（addDecorationTop=false）：父类 showEvent 已
 *             套用避让（增量在边距内），此刻 totalSizeHint 已含真实
 *             条高与真实字模，直接定尺不再叠加——双重计数防护。
 *           尺寸下限 320x340 保留 r1 猎捕合格常量（仅 hint 兜底，
 *           hint 更大时恒取 hint）。 */
static void xcd_applyLayoutSize(XColorDialog* dlg, bool addDecorationTop)
{
    XLayout* root;
    XSize hint;
    XMargins m;
    int fw;
    int fh;
    int marginDelta;
    if (!dlg) return;
    root = XWidget_layout((XWidget*)dlg);
    if (!root) return; /* 便捷函数外的裸实例（无内容布局）不干预。 */
    hint = XLayout_totalSizeHint(root);
    /* [竖向盒边距轴修正] XBoxLayout 合计度量恒把 l+r 记入主轴、t+b
     * 记入交叉轴（XBoxLayout.c calcMetrics 的边距累计段），竖向根盒
     * 在上下边距不对称（标题让位 28/CSD 避让增量 30——正是本对话框
     * 的常态形态）时高度 hint 短计 (t+b)-(l+r)、宽度 hint 同额虚增。
     * 对话框层按当前边距自愈（主轴实为高度），Windows 截图溢出的
     * 隐性根因之一即此短计叠加避让增量后击穿 340 下限余量。耦合注
     * 记：若 XBoxLayout 边距累计改为随方向取用，本修正须同步撤销。 */
    m = XLayout_contentsMargins(root);
    marginDelta = (m.top + m.bottom) - (m.left + m.right);
    if (marginDelta > 0 &&
        XBoxLayout_direction((XBoxLayout*)root) !=
            XBoxLayoutDirection_LeftToRight) {
        if (hint.height >= m.left + m.right)
            hint.height += marginDelta;
        if (hint.width >= m.top + m.bottom)
            hint.width -= marginDelta;
        if (hint.width < 0) hint.width = 0;
    }
    if (addDecorationTop)
        hint.height += XDialog_decorationTopOffset(&dlg->m_base);
    fw = hint.width > XCD_MIN_WIDTH ? hint.width : XCD_MIN_WIDTH;
    fh = hint.height > XCD_MIN_HEIGHT ? hint.height : XCD_MIN_HEIGHT;
    XWidget_setFixedSize((XWidget*)dlg, fw, fh);
}

/** @brief 显示事件：exec 预定尺后按真实装饰条高/真实字模复算一次
 *  （幂等；XInputDialog showEvent 同款收口）。
 *  @details 先链父类 showEvent——其居中与 CSD 避让链
 *           （xdlg_applyContentAvoidance）把真实条高增量写入根布局
 *           顶边距，返回后 totalSizeHint 即为含避让的最终需求；随后
 *           setFixedSize 触发 resize→布局重解算，末尾按新尺寸重新
 *           居中（自动居中不登记 Moved，居中合法）。 */
static void VXColorDialog_showEvent(XWidget* self, XEvent* event)
{
    bool show = self && event &&
                XEvent_type(event) == XEVENT_TYPE_SHOW;
    XClass_Parent(XDialog, EXWidget_ShowEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    if (show) {
        xcd_applyLayoutSize((XColorDialog*)self, false);
        xcd_centerOnScreen(self);
    }
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
        /* 单色预览块（对标 Qt QColorShower 预览框的 StyledPanel 观感）：
           内缩 1px 填当前色 + 1px 中性描边（与 XDialog 面板边框同族灰，
           克制不加渐变/阴影），预览色块不再与对话框底色融成一片。 */
        XRect full;
        XColor color = sw->m_owner
            ? sw->m_owner->m_currentColor : XColor_create();
        full.x = 1;
        full.y = 1;
        full.width = w > 2 ? w - 2 : w;
        full.height = h > 2 ? h - 2 : h;
        XPainter_fillRect(&painter, &full, XColor_rgba(&color));
        XPainter_setPen(&painter, XCD_PREVIEW_BORDER_COLOR);
        XPainter_drawLine(&painter, 0, 0, w - 1, 0);
        XPainter_drawLine(&painter, 0, h - 1, w - 1, h - 1);
        XPainter_drawLine(&painter, 0, 0, 0, h - 1);
        XPainter_drawLine(&painter, w - 1, 0, w - 1, h - 1);
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
                if (idx < g_xcdStandardCount) { /* 进程级共享表高水位。 */
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
    /* 对标 QColorDialog::accept = done(Accepted)：selectedColor =
     * currentColor 并发射 colorSelected（漏斗内完成）。 */
    XColorDialog_done((XColorDialog*)receiver, (int)XDialogCode_Accepted);
}

static void xcd_rejectSlot(XObject* receiver, XVarList* args)
{
    (void)args;
    if (receiver) XColorDialog_done((XColorDialog*)receiver,
                                    (int)XDialogCode_Rejected);
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
        XClassDelete(dlg);
        return initial;
    }
    /* 子控件形态：见 XInputDialog 同款注记（单原生窗口模型下窗口
     * 形态首帧 flush 不可靠）。 */
    dlg = XColorDialog_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, initial,
                                 parent, 0);
    if (!dlg) return initial;
    if (title)
        XWidget_setWindowTitle((XWidget*)dlg, title);
    XColorDialog_setOptions(dlg, options);
    xcd_initStandardTable(dlg);
    root = XBoxLayout_create(XBoxLayoutDirection_TopToBottom, (XWidget*)dlg);
    if (!root) {
        XClassDelete(dlg);
        return initial;
    }
    /* 标题条带让位（对标 Qt qcolordialog.cpp 布局：色板区在标题栏
     * 以下分带排布；XGui 子控件形态对话框无平台标题栏，标题由
     * XDialog 面板顶部带内绘制（XDialog.c 绘制带 y=4..22），色板区
     * 须下移让出该条带）。对齐 #20 XMessageBox.c xmsg_contentTop
     * 先例口径：有标题时内容顶起 28，无标题保持 12 不占位。夜间台
     * 账 #28 伴随：标题「选择颜色」上半被 48 色板首行遮半。 */
    {
        const XString* t = XWidget_windowTitle((const XWidget*)dlg);
        const char* u = t ? XString_toUtf8(t) : NULL;
        XLayout_setContentsMargins((XLayout*)root, 12,
                                   (u && u[0]) ? 28 : 12, 12, 12);
    }
    XLayout_setSpacing((XLayout*)root, 8);
    {
        int labelH = 0; /* 标签实测行高（字体度量；旋入框/按钮行下限联动）。 */
        /* 标准色网格（8×6，可点击选色，对标 QColorDialog 标准色区）。 */
        XColorSwatch* grid = xcd_swatchCreate(dlg, true);
        if (grid) {
            xcd_setName((XObject*)grid, XCD_NAME_GRID);
            XWidget_setMinimumSize((XWidget*)grid, 224, 168);
            XBoxLayout_addWidget(root, (XWidget*)grid);
        }
        /* R/G/B 分量行（对标 QColorDialog RGB 编辑区）。行高随字体
         * 度量：XLabel 不向布局发布实测 sizeHint（布局条目读控件存
         * 储位，标签族未回填），行高若只靠旋入框固定 24 下限则与字
         * 体无关（放大度量下文本被行高压住）。标签实测行高
         * （XLabel_sizeHint，随字模缩放）抬升旋入框下限，尺寸随字
         * 体度量联动。 */
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
                    if (i == 0) {
                        /* 首列标签实测行高定行（三列同字体同值）。 */
                        XSize lh = XLabel_sizeHint(lb);
                        if (lh.height > labelH) labelH = lh.height;
                    }
                    XBoxLayout_addWidget(rgbRow, (XWidget*)lb);
                }
                spin = XSpinBox_create((XWidget*)dlg, 0);
                if (spin) {
                    int spinMinH = labelH + 8 > 24 ? labelH + 8 : 24;
                    int spinMinW = labelH * 3 > 72 ? labelH * 3 : 72;
                    XSpinBox_setRange(spin, 0, 255);
                    XSpinBox_setValue(spin, comps[i]);
                    xcd_setName((XObject*)spin, slots[i]);
                    XWidget_setMinimumSize((XWidget*)spin,
                                           spinMinW, spinMinH);
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
#if XDIALOGBUTTONBOX_ON
            /* 标准按钮行（对标 qcolordialog.cpp initWidgets：buttons =
             * new QDialogButtonBox(q)，addButton(Ok)/addButton(Cancel)
             * 后 clicked→accept/reject、ok->setDefault(true)；WinLayout
             * 角色序右对齐与模态内 Tab 环链由盒内 xdb_relayout 承担，
             * 按钮行作为布局条目参与 sizeHint 求和，随盒行高自动定
             * 行，不再手写像素行）。 */
            XDialogButtonBox* box =
                XDialogButtonBox_create((XWidget*)dlg, 0);
            if (box) {
                XPushButton* ok = XDialogButtonBox_addButton_3(
                    box, XDialogButtonBoxStandard_Ok);
                XPushButton* cancel = XDialogButtonBox_addButton_3(
                    box, XDialogButtonBoxStandard_Cancel);
                /* 盒行尺寸入布局 hint：两钮 80x26 + 间距 8 + 盒边距
                 * 8 → 最小 176x34（盒子控件无自报 sizeHint，显式给
                 * 布局行高与行宽下限）；行高再随字体度量抬升（标签
                 * 实测行高 + 上下盒边距 8，放大度量下按钮行不被压
                 * 顶/贴底）。 */
                XWidget_setMinimumSize((XWidget*)box, 176,
                                       labelH + 12 > 34 ? labelH + 12 : 34);
                if (ok) {
                    xcd_setName((XObject*)ok, XCD_NAME_OK);
                    XPushButton_setDefault(ok, true);
                    XObject_connect_1((XObject*)ok,
                                      (size_t)XAbstractButton_clicked_signal,
                                      (XObject*)dlg, xcd_acceptSlot,
                                      XConnectionType_Direct);
                }
                if (cancel) {
                    xcd_setName((XObject*)cancel, XCD_NAME_CANCEL);
                    XObject_connect_1((XObject*)cancel,
                                      (size_t)XAbstractButton_clicked_signal,
                                      (XObject*)dlg, xcd_rejectSlot,
                                      XConnectionType_Direct);
                }
                XBoxLayout_addWidget(root, (XWidget*)box);
            }
#else
            XPushButton* ok = XPushButton_create((XWidget*)dlg, 0);
            XPushButton* cancel = XPushButton_create((XWidget*)dlg, 0);
            bar = XBoxLayout_create(XBoxLayoutDirection_LeftToRight, NULL);
            if (bar) {
                XBoxLayout_addStretch(bar, 1);
                if (ok) {
                    XAbstractButton_setText_2((XAbstractButton*)ok, "确定");
                    XWidget_setMinimumSize(
                        (XWidget*)ok, 80,
                        labelH + 10 > 28 ? labelH + 10 : 28);
                    xcd_setName((XObject*)ok, XCD_NAME_OK);
                    XBoxLayout_addWidget(bar, (XWidget*)ok);
                    XObject_connect_1((XObject*)ok,
                                      (size_t)XAbstractButton_clicked_signal,
                                      (XObject*)dlg, xcd_acceptSlot,
                                      XConnectionType_Direct);
                }
                if (cancel) {
                    XAbstractButton_setText_2((XAbstractButton*)cancel, "取消");
                    XWidget_setMinimumSize(
                        (XWidget*)cancel, 80,
                        labelH + 10 > 28 ? labelH + 10 : 28);
                    xcd_setName((XObject*)cancel, XCD_NAME_CANCEL);
                    XBoxLayout_addWidget(bar, (XWidget*)cancel);
                    XObject_connect_1((XObject*)cancel,
                                      (size_t)XAbstractButton_clicked_signal,
                                      (XObject*)dlg, xcd_rejectSlot,
                                      XConnectionType_Direct);
                }
                /* 对标 Qt 模态对话框内 Tab 焦点链不越出对话框的窗口
                   级语义（详见 XDialogButtonBox.c xdb_relayout 同款注
                   记）：显式 Tab 环链把确定/取消围成子树内闭环（夜
                   间台账 #23/#24 同根防范）。 */
                if (ok && cancel) {
                    XWidget_setTabOrder((XWidget*)ok, (XWidget*)cancel);
                    XWidget_setTabOrder((XWidget*)cancel, (XWidget*)ok);
                }
                XBoxLayout_addLayout(root, (XLayout*)bar);
            }
#endif /* XDIALOGBUTTONBOX_ON */
        }
    }
    /* 固定尺寸收口（指令② 同型：对标 qcolordialog.cpp 布局
     * SetFixedSize 的「窗口吸附布局 sizeHint」语义）——尺寸 =
     * max(布局实测 totalSizeHint + 装饰条高预测, 320x340)。布局实测
     * 覆盖显式 minimumSize 子控件（色板 224x168、RGB spinbox 72x24、
     * 预览 224x36、按钮盒 176x34）与字体度量（标签行高随字模），任
     * 何字体/DPI 下不溢出；装饰条高预测补 CSD 平台（Windows 框架自
     * 绘标题条）SHOW 期写入根布局顶边距的增量，exec 首帧即合尺，
     * SHOW 精修（VXColorDialog_showEvent）按真实条高兜底校正。 */
    XLayout_update((XLayout*)root);
    xcd_applyLayoutSize(dlg, true);
    xcd_centerOnScreen((XWidget*)dlg);
    /* 阻塞模态执行（复用 XDialog exec：应用模态 + Escape→reject）。 */
    accepted = XDialog_exec(&dlg->m_base) == 1;
    {
        XColor result;
        if (accepted)
            result = XColorDialog_selectedColor(dlg);
        else
            result = XColor_create(); /* 对标 Qt：取消返回失效颜色。 */
        if (root) XClassDelete((XLayout*)root);
        if (rgbRow) XClassDelete((XLayout*)rgbRow);
        if (bar) XClassDelete((XLayout*)bar);
        XClassDelete(dlg);
        return result;
    }
}

XColor XColorDialog_getColor_2(XColor initial, XWidget* parent,
                               const char* title, XColorDialogOptions options)
{
    XString* t = title ? XString_create_utf8(title) : NULL;
    XColor result = XColorDialog_getColor(initial, parent, t, options);
    if (t) XClassDelete((XClass*)t);
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


/* ==================== Task 2.21 回检补齐：自定义/标准颜色 ==============
 * 存储为文件级静态表（对标 Qt 五函数 static 语义，见文件头部注）；
 * self 仅承担 NULL 门面（任一实例读写同一份进程级表）。 */

int XColorDialog_customCount(const XColorDialog* self)
{
    (void)self;
    return g_xcdCustomCount;
}

void XColorDialog_setCustomColor(XColorDialog* self, int index, XColor color)
{
    if (!self || index < 0 || index >= 16) return;
    g_xcdCustomColors[index] = color;
    if (index >= g_xcdCustomCount)
        g_xcdCustomCount = index + 1;
}

XColor XColorDialog_customColor(const XColorDialog* self, int index)
{
    XColor black;
    XColor_init_rgb(&black, 0, 0, 0, 0);
    (void)self;
    if (index < 0 || index >= 16) return black;
    return g_xcdCustomColors[index];
}

void XColorDialog_setStandardColor(XColorDialog* self, int index, XColor color)
{
    if (!self || index < 0 || index >= 48) return;
    g_xcdStandardColors[index] = color;
    if (index >= g_xcdStandardCount)
        g_xcdStandardCount = index + 1;
}

XColor XColorDialog_standardColor(const XColorDialog* self, int index)
{
    XColor black;
    XColor_init_rgb(&black, 0, 0, 0, 0);
    (void)self;
    if (index < 0 || index >= 48) return black;
    return g_xcdStandardColors[index];
}

void XColorDialog_open(XColorDialog* self)
{
    if (!self) return;
    XWidget_show((XWidget*)self);
}

void XColorDialog_done(XColorDialog* self, int result)
{
    if (!self) return;
    /* 对标 QColorDialog::done（qcolordialog.cpp:2303-2316）：Accepted
     * → selectedColor=currentColor 并发射 colorSelected；否则
     * selectedColor 复位为失效色（QColor() 等价物）——实例复用第二次
     * 取消后 selectedColor() 不再是上一次 accept 的旧值。 */
    if (result == (int)XDialogCode_Accepted) {
        self->m_selectedColor = self->m_currentColor;
        xcolordialog_emitColor(self,
                               (size_t)XColorDialog_colorSelected_signal,
                               self->m_selectedColor);
    } else {
        self->m_selectedColor = XColor_create();
    }
    XDialog_done(&self->m_base, result);
    /* 对标 receiverToDisconnectOnClose：open_2 建立的 colorSelected
     * 连接在关闭收口时自动断开。 */
    if (self->m_openReceiver && self->m_openMember) {
        XObject_disconnect_1((XObject*)self,
                             (size_t)XColorDialog_colorSelected_signal,
                             self->m_openReceiver, self->m_openMember);
    }
    self->m_openReceiver = NULL;
    self->m_openMember = NULL;
}

void XColorDialog_open_2(XColorDialog* self, XObject* receiver,
                         XSlotFunc1 member)
{
    if (!self || !member) return;
    /* 重复 open 防叠连（XMessageBox_open_2 同款）：旧记录尚存时先按
     * 旧载荷断开旧连接（disconnect_1 只移除首个匹配，直接叠连会残留
     * 旧连接，receiver 每次收口被多路误触发）。 */
    if (self->m_openReceiver || self->m_openMember) {
        XObject_disconnect_1((XObject*)self,
                             (size_t)XColorDialog_colorSelected_signal,
                             self->m_openReceiver, self->m_openMember);
        self->m_openReceiver = NULL;
        self->m_openMember = NULL;
    }
    self->m_openReceiver = receiver;
    self->m_openMember = member;
    /* 对标 QColorDialog::open（qcolordialog.cpp:2179-2186）：连接
     * colorSelected(QColor)（Qt 文档同："connects its colorSelected()
     * signal to the slot"），非 finished——挂 colorSelected 承载的
     * 接收槽经 open_2 打开后能收到颜色载荷。关闭自动断开由
     * XColorDialog_done 承担。 */
    if (receiver) {
        XObject_connect_1((XObject*)self,
                          (size_t)XColorDialog_colorSelected_signal,
                          receiver, member, XConnectionType_Direct);
    }
    /* 对标 QDialog::open：窗口模态显示并立即返回（XMessageBox_open_2
     * 同款收口：经基类 XDialog_open 承载窗口模态改写/居中/置顶/模态
     * 门全套；与本文件 open 桩（仅 XWidget_show）的简化口径不同，
     * receiver 形态按 Qt QDialog::open 完整语义走基类）。 */
    XDialog_open(&self->m_base);
}

#endif /* XWIDGET_ON && XDIALOG_ON */

#endif /* XGUI_ON */
