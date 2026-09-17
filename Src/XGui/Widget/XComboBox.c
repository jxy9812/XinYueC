/**
 * @file       XComboBox.c
 * @brief      XComboBox 下拉组合框控件实现（对标 Qt 6.8 QComboBox）。
 * @details    内部结构：文本项存于 char** 动态数组（每项独立分配）。
 *             显示区绘制当前项文本 + 下拉箭头按钮；左键点击弹出项
 *             列表（popupVisible 状态内嵌绘制——选中高亮跟随鼠标）；
 *             可编辑模式内嵌 XLineEdit 占满左侧（编辑文本经
 *             editTextChanged/InsertPolicy 插入语义）。
 *             信号语义对标 Qt：activated 在用户选择时发射；
 *             currentIndexChanged/currentTextChanged 在当前项变化时
 *             发射；editTextChanged 仅可编辑模式编辑变化时发射。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#if XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON

#include "XComboBox.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XString.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#include <stdio.h>

/* 弹出列表几何常量 */
#define XCOMBOBOX_BUTTON_W  16
#define XCOMBOBOX_ITEM_H    20

/* ==================== 前向声明 ==================== */
static void  VXComboBox_paintEvent(XWidget* self, XEvent* event);
static void  VXComboBox_mousePressEvent(XWidget* self, XEvent* event);
static void  VXComboBox_mouseMoveEvent(XWidget* self, XEvent* event);
static void  VXComboBox_mouseReleaseEvent(XWidget* self, XEvent* event);
static void  VXComboBox_changeEvent(XWidget* self, XEvent* event);
static void  VXComboBox_copy(XComboBox* self, const XComboBox* other);
static void  VXComboBox_move(XComboBox* self, XComboBox* other);

/* ==================== 内部辅助 ==================== */

static uint32_t xcombo_color(const XComboBox* self, XPaletteColorRole role)
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

/** @brief 发射 int 信号。 */
static void xcombo_emitInt(XComboBox* self, size_t signal, int value)
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

/** @brief 发射 const char* 信号。 */
static void xcombo_emitText(XComboBox* self, size_t signal, const char* text)
{
    XVarList* arguments;
    if (!text) text = "";
    arguments = XVarList_Create(XVar(const char*, text));
    if (!arguments) return;
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, arguments, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(arguments);
    }
}

/** @brief 鼠标位置 → 弹出列表项索引（-1 = 无）。 */
static int xcombo_popupItemAt(const XComboBox* self, const XPoint* pos)
{
    int idx;
    if (!pos || pos->x < 0 || pos->x > XWidget_width((XWidget*)self)) return -1;
    idx = pos->y / XCOMBOBOX_ITEM_H;
    if (idx < 0 || idx >= self->m_itemCount) return -1;
    return idx;
}

/* ==================== 弹出状态（内嵌绘制） ==================== */

/** @brief 当前弹出的显示偏移（项多时滚动，内部）。 */
static int g_comboPopupOffset = 0;

/* ==================== 虚槽实现 ==================== */

/** @brief 绘制：边框 + 当前项文本 + 下拉箭头；弹出时叠加列表。 */
static void VXComboBox_paintEvent(XWidget* self, XEvent* event)
{
    XComboBox* combo = (XComboBox*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r = XWidget_rect(self);
    uint32_t base;
    uint32_t dark;
    uint32_t light;
    uint32_t button;
    uint32_t text;
    uint32_t highlight;
    uint32_t highlightedText;
    if (!combo || r.width <= 4 || r.height <= 4) return;
    r.x = 0; r.y = 0;
    base     = xcombo_color(combo, XPaletteColorRole_Base);
    dark     = xcombo_color(combo, XPaletteColorRole_Dark);
    light    = xcombo_color(combo, XPaletteColorRole_Light);
    text     = xcombo_color(combo, XPaletteColorRole_Text);
    highlight = xcombo_color(combo, XPaletteColorRole_Highlight);
    highlightedText = xcombo_color(combo, XPaletteColorRole_HighlightedText);
    button = xcombo_color(combo, XPaletteColorRole_Button);

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
    if (XStyle_defaultStyle() != NULL) {
        /* Fusion/公共风格接管：面板 + 下拉按钮区走 CC_ComboBox。 */
        XStyle* style = XStyle_defaultStyle();
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCC_ComboBox);
        opt.m_rect = r;
        opt.m_state = XWidget_isEnabled(self)
            ? XStyleState_Enabled | XStyleState_Raised : 0;
        if (combo->m_popupVisible)
            opt.m_state |= XStyleState_Sunken | XStyleState_On;
        if (XWidget_hasFocus(self))
            opt.m_state |= XStyleState_HasFocus;
        if (XWidget_underMouse(self) && XWidget_isEnabled(self))
            opt.m_state |= XStyleState_MouseOver;
        opt.m_text = "";
#if XPALETTE_ON
        opt.m_palette = XWidget_palette(self);
#endif
        XStyle_drawComplexControl(style, XStyleCC_ComboBox, &opt,
                                  &painter, self);
        goto xcombo_style_text;
    }
#endif /* XSTYLE_ON */
    XPainter_fillRect(&painter, &r, base);
    {
        XRect e = r;
        e.height = 1;
        XPainter_fillRect(&painter, &e, dark);
        e = r; e.width = 1;
        XPainter_fillRect(&painter, &e, dark);
        e = r; e.x = r.x + r.width - 1; e.width = 1;
        XPainter_fillRect(&painter, &e, light);
        e = r; e.y = r.y + r.height - 1; e.height = 1;
        XPainter_fillRect(&painter, &e, light);
    }
    /* 下拉箭头按钮区。 */
    {
        XRect btn = { r.x + r.width - XCOMBOBOX_BUTTON_W, r.y,
                      XCOMBOBOX_BUTTON_W, r.height };
        XPainter_fillRect(&painter, &btn, button);
        {
            int cx = btn.x + XCOMBOBOX_BUTTON_W / 2;
            int cy = btn.y + r.height / 2;
            XPainter_setPen(&painter, dark);
            XPainter_drawLine(&painter, cx - 4, cy - 2, cx + 4, cy - 2);
            XPainter_drawLine(&painter, cx - 4, cy - 2, cx, cy + 3);
            XPainter_drawLine(&painter, cx + 4, cy - 2, cx, cy + 3);
        }
    }
xcombo_style_text: {}
    /* style 分支与原路径共用：当前项文本在下方绘制。 */
    /* 当前项文本（弹出时列表替代文本显示）。 */
    if (!combo->m_popupVisible) {
        if (combo->m_currentIndex >= 0 && combo->m_currentIndex < combo->m_itemCount &&
            combo->m_items[combo->m_currentIndex]) {
            XPainter_drawText(&painter, 6, (r.height - 14) / 2 + 12,
                              XString_toUtf8(combo->m_items[combo->m_currentIndex]), text);
        } else if (combo->m_placeholderText &&
                   XString_toUtf8(combo->m_placeholderText) &&
                   XString_toUtf8(combo->m_placeholderText)[0]) {
            XPainter_drawText(&painter, 6, (r.height - 14) / 2 + 12,
                              XString_toUtf8(combo->m_placeholderText),
                              xcombo_color(combo, XPaletteColorRole_Mid));
        }
    }
    else {
        /* 弹出列表：覆盖绘制项行（当前项高亮）。 */
        XRect list = r;
        int rows = combo->m_itemCount;
        int i;
        if (rows > combo->m_maxVisibleItems) rows = combo->m_maxVisibleItems;
        list.height = rows * XCOMBOBOX_ITEM_H + 2;
        XPainter_fillRect(&painter, &list, base);
        for (i = g_comboPopupOffset; i < g_comboPopupOffset + rows &&
                                     i < combo->m_itemCount; ++i) {
            XRect row = { r.x, (i - g_comboPopupOffset) * XCOMBOBOX_ITEM_H,
                          r.width - XCOMBOBOX_BUTTON_W, XCOMBOBOX_ITEM_H };
            if (i == combo->m_currentIndex)
                XPainter_fillRect(&painter, &row, highlight);
            if (combo->m_items[i])
                XPainter_drawText(&painter, row.x + 4, row.y + 14,
                                  XString_toUtf8(combo->m_items[i]),
                                  i == combo->m_currentIndex ? highlightedText
                                                             : text);
        }
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 左键：弹出/收起切换；弹出中点击行 = 选择。 */
static void VXComboBox_mousePressEvent(XWidget* self, XEvent* event)
{
    XComboBox* combo = (XComboBox*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!combo || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
    if (combo->m_popupVisible) {
        /* grabMouse 下点击超出自身边界 = 点击外部关闭弹出。 */
        if (pos.x < 0 || pos.y < 0 ||
            pos.x >= XWidget_width(self) || pos.y >= XWidget_height(self)) {
            XComboBox_hidePopup_base(combo);
            XEvent_accept(event);
            return;
        }
        int idx = xcombo_popupItemAt(combo, &pos);
        if (idx >= 0) {
            XComboBox_setCurrentIndex(combo, idx);
            xcombo_emitInt(combo, (size_t)XComboBox_activated_signal(combo, idx), idx);
            xcombo_emitText(combo,
                            (size_t)XComboBox_textActivated_signal(
                                combo, XComboBox_itemText_2(combo, idx)),
                            XComboBox_itemText_2(combo, idx));
        }
        XComboBox_hidePopup_base(combo);
        XEvent_accept(event);
        return;
    }
    XComboBox_showPopup_base(combo);
    XEvent_accept(event);
}

/** @brief 弹出中移动：高亮跟随（重绘）。 */
static void VXComboBox_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XComboBox* combo = (XComboBox*)self;
    if (!combo || !combo->m_popupVisible) return;
    XWidget_update(self);
    XEvent_ignore(event);
}

static void VXComboBox_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XEvent_ignore(event);
    (void)self;
}

static void VXComboBox_changeEvent(XWidget* self, XEvent* event)
{
    XEvent_ignore(event);
    (void)self;
}

/** @brief 深拷贝：基类拷贝后复制项数组与字段。 */
static void VXComboBox_copy(XComboBox* self, const XComboBox* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XComboBox_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    XComboBox_clear(self);
    for (i = 0; i < other->m_itemCount; ++i)
        XComboBox_addItem(self, other->m_items[i]);
    self->m_currentIndex = other->m_currentIndex;
    self->m_maxCount = other->m_maxCount;
    self->m_maxVisibleItems = other->m_maxVisibleItems;
    self->m_duplicatesEnabled = other->m_duplicatesEnabled;
    self->m_editable = other->m_editable;
    self->m_insertPolicy = other->m_insertPolicy;
    self->m_sizeAdjustPolicy = other->m_sizeAdjustPolicy;
    self->m_minimumContentsLength = other->m_minimumContentsLength;
    self->m_frame = other->m_frame;
    if (self->m_placeholderText && other->m_placeholderText)
        XString_assign(self->m_placeholderText, other->m_placeholderText);
}

/** @brief 移动语义：基类移动后转移项数组，源对象归默认值。 */
static void VXComboBox_move(XComboBox* self, XComboBox* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XComboBox_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    XComboBox_clear(self);
    self->m_items = other->m_items;
    self->m_itemCount = other->m_itemCount;
    self->m_itemCapacity = other->m_itemCapacity;
    self->m_currentIndex = other->m_currentIndex;
    other->m_items = NULL;
    other->m_itemCount = 0;
    other->m_itemCapacity = 0;
    other->m_currentIndex = -1;
    self->m_maxCount = other->m_maxCount;
    self->m_maxVisibleItems = other->m_maxVisibleItems;
    self->m_duplicatesEnabled = other->m_duplicatesEnabled;
    self->m_editable = other->m_editable;
    self->m_insertPolicy = other->m_insertPolicy;
    self->m_sizeAdjustPolicy = other->m_sizeAdjustPolicy;
    self->m_minimumContentsLength = other->m_minimumContentsLength;
    self->m_frame = other->m_frame;
    if (self->m_placeholderText) XString_delete_base(self->m_placeholderText);
    self->m_placeholderText = other->m_placeholderText;
    other->m_placeholderText = XString_create();
    other->m_maxCount = 2147483647;
    other->m_maxVisibleItems = 10;
    other->m_duplicatesEnabled = false;
    other->m_editable = false;
    other->m_insertPolicy = XComboBoxInsertPolicy_InsertAtBottom;
    other->m_sizeAdjustPolicy = XComboBoxSizeAdjustPolicy_AdjustToContents;
    other->m_frame = true;
    /* m_placeholderText 已转移并重建。 */
    for (i = 0; i < self->m_itemCount; ++i) { (void)0; }
}

/* ==================== 生命周期 ==================== */

static void VXComboBox_deinit(XComboBox* self)
{
    int i;
    if (!self) return;
    if (self->m_placeholderText) {
        XString_delete_base(self->m_placeholderText);
        self->m_placeholderText = NULL;
    }
    for (i = 0; i < self->m_itemCount; ++i) {
        if (self->m_items[i]) XString_delete_base(self->m_items[i]);
        if (self->m_itemData && self->m_itemData[i])
            XString_delete_base(self->m_itemData[i]);
        if (self->m_itemIcons && self->m_itemIcons[i])
            XString_delete_base(self->m_itemIcons[i]);
        self->m_items[i] = NULL;
    }
    if (self->m_items) {
        XFree_System(self->m_items);
        self->m_items = NULL;
    }
    if (self->m_itemData) {
        XFree_System(self->m_itemData);
        self->m_itemData = NULL;
    }
    if (self->m_itemIcons) {
        XFree_System(self->m_itemIcons);
        self->m_itemIcons = NULL;
    }
    self->m_itemCount = 0;
    self->m_itemCapacity = 0;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XComboBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XComboBox)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXComboBox_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXComboBox_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXComboBox_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VXComboBox_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXComboBox_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXComboBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXComboBox_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXComboBox_deinit);

    return XVTABLE_DEFAULT;
}

void XComboBox_init(XComboBox* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XComboBox);

    self->m_items = NULL;
    self->m_itemData = NULL;
    self->m_itemIcons = NULL;
    self->m_itemCount = 0;
    self->m_itemCapacity = 0;
    self->m_currentIndex = -1;
    self->m_maxCount = 2147483647;
    self->m_maxVisibleItems = 10;
    self->m_iconSize = 16;
    self->m_duplicatesEnabled = false;
    self->m_editable = false;
    self->m_lineEdit = NULL;
    self->m_insertPolicy = XComboBoxInsertPolicy_InsertAtBottom;
    self->m_sizeAdjustPolicy = XComboBoxSizeAdjustPolicy_AdjustToContents;
    self->m_minimumContentsLength = 0;
    self->m_frame = true;
    self->m_placeholderText = XString_create();
    self->m_popupVisible = false;
    self->m_savedHeight = 0;
}

XComboBox* XComboBox_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XComboBox* self = (XComboBox*)XMemory_malloc(sizeof(XComboBox), memory);
    if (!self) return NULL;
    XComboBox_init(self, parent, flags);
    Set_Class_Memory(self, memory); Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 项管理 ==================== */

int XComboBox_count(const XComboBox* self) { return self ? self->m_itemCount : 0; }
int XComboBox_maxCount(const XComboBox* self) { return self ? self->m_maxCount : 0; }

void XComboBox_setMaxCount(XComboBox* self, int max)
{
    if (!self || max < 0) return;
    self->m_maxCount = max;
    while (self->m_itemCount > max) XComboBox_removeItem(self, self->m_itemCount - 1);
}

int XComboBox_maxVisibleItems(const XComboBox* self)
{
    return self ? self->m_maxVisibleItems : 10;
}
void XComboBox_setMaxVisibleItems(XComboBox* self, int maxItems)
{
    if (self && maxItems > 0) self->m_maxVisibleItems = maxItems;
}
bool XComboBox_duplicatesEnabled(const XComboBox* self)
{
    return self ? self->m_duplicatesEnabled : false;
}
void XComboBox_setDuplicatesEnabled(XComboBox* self, bool enable)
{
    if (self) self->m_duplicatesEnabled = enable;
}
void XComboBox_setFrame(XComboBox* self, bool on) { if (self) self->m_frame = on; }
bool XComboBox_hasFrame(const XComboBox* self) { return self ? self->m_frame : true; }
int XComboBox_insertPolicy(const XComboBox* self)
{
    return self ? self->m_insertPolicy : XComboBoxInsertPolicy_InsertAtBottom;
}
void XComboBox_setInsertPolicy(XComboBox* self, int policy)
{
    if (self && policy >= 0 && policy <= 6) self->m_insertPolicy = policy;
}
int XComboBox_sizeAdjustPolicy(const XComboBox* self)
{
    return self ? self->m_sizeAdjustPolicy
                : XComboBoxSizeAdjustPolicy_AdjustToContents;
}
void XComboBox_setSizeAdjustPolicy(XComboBox* self, int policy)
{
    if (self && policy >= 0 && policy <= 2) self->m_sizeAdjustPolicy = policy;
}
int XComboBox_minimumContentsLength(const XComboBox* self)
{
    return self ? self->m_minimumContentsLength : 0;
}
void XComboBox_setMinimumContentsLength(XComboBox* self, int characters)
{
    if (self && characters >= 0) self->m_minimumContentsLength = characters;
}
XString* XComboBox_placeholderText(const XComboBox* self)
{
    if (!self || !self->m_placeholderText) return NULL;
    return XString_create_copy(self->m_placeholderText);
}
const char* XComboBox_placeholderText_2(const XComboBox* self)
{
    const char* text;
    if (!self || !self->m_placeholderText) return "";
    text = XString_toUtf8(self->m_placeholderText);
    return (text && text[0]) ? text : "";
}
void XComboBox_setPlaceholderText(XComboBox* self, const XString* placeholderText)
{
    if (!self) return;
    if (!self->m_placeholderText)
        self->m_placeholderText = XString_create();
    if (self->m_placeholderText)
        XString_assign_utf8(self->m_placeholderText,
                            placeholderText ? XString_toUtf8(placeholderText) : "");
    XWidget_update((XWidget*)self);
}
void XComboBox_setPlaceholderText_2(XComboBox* self, const char* placeholderText)
{
    if (!self) return;
    if (!placeholderText) placeholderText = "";
    if (!self->m_placeholderText)
        self->m_placeholderText = XString_create();
    if (self->m_placeholderText)
        XString_assign_utf8(self->m_placeholderText, placeholderText);
    XWidget_update((XWidget*)self);
}
bool XComboBox_isEditable(const XComboBox* self)
{
    return self ? self->m_editable : false;
}

void XComboBox_setEditable(XComboBox* self, bool editable)
{
    if (!self || self->m_editable == editable) return;
    self->m_editable = editable;
    if (editable) {
        /* 对标 Qt setEditable(true)：创建内嵌编辑框。 */
        if (!self->m_lineEdit) {
            self->m_lineEdit = XLineEdit_create((XWidget*)self, 0);
            if (self->m_lineEdit) {
                XWidget_setGeometry((XWidget*)self->m_lineEdit, 2, 2,
                                    XWidget_width((XWidget*)self) -
                                        XCOMBOBOX_BUTTON_W - 4,
                                    XWidget_height((XWidget*)self) - 4);
                XWidget_show((XWidget*)self->m_lineEdit);
            }
        }
    }
    else {
        /* 对标 Qt setEditable(false)：销毁内嵌编辑框。 */
        if (self->m_lineEdit) {
            XLineEdit_delete_base(self->m_lineEdit);
            self->m_lineEdit = NULL;
        }
    }
    XWidget_update((XWidget*)self);
}
XLineEdit* XComboBox_lineEdit(const XComboBox* self)
{
    return self ? self->m_lineEdit : NULL;
}

void XComboBox_setLineEdit(XComboBox* self, XLineEdit* edit)
{
    if (!self || !edit || edit == self->m_lineEdit) return;
    if (!self->m_editable) self->m_editable = true;
    if (self->m_lineEdit) {
        XLineEdit_delete_base(self->m_lineEdit);
        self->m_lineEdit = NULL;
    }
    self->m_lineEdit = edit;
    XWidget_setParentPlain((XWidget*)edit, (XWidget*)self);
    XWidget_setGeometry((XWidget*)edit, 2, 2,
                        XWidget_width((XWidget*)self) -
                            XCOMBOBOX_BUTTON_W - 4,
                        XWidget_height((XWidget*)self) - 4);
    XWidget_show((XWidget*)edit);
    XWidget_update((XWidget*)self);
}

int XComboBox_currentIndex(const XComboBox* self)
{
    return self ? self->m_currentIndex : -1;
}
XString* XComboBox_currentText(const XComboBox* self)
{
    if (self && self->m_currentIndex >= 0 &&
        self->m_currentIndex < self->m_itemCount && self->m_items[self->m_currentIndex])
        return XString_create_copy(self->m_items[self->m_currentIndex]);
    return XString_create();
}
const char* XComboBox_currentText_2(const XComboBox* self)
{
    if (self && self->m_currentIndex >= 0 &&
        self->m_currentIndex < self->m_itemCount && self->m_items[self->m_currentIndex])
        return XString_toUtf8(self->m_items[self->m_currentIndex]);
    return "";
}
XString* XComboBox_itemText(const XComboBox* self, int index)
{
    if (self && index >= 0 && index < self->m_itemCount && self->m_items[index])
        return XString_create_copy(self->m_items[index]);
    return NULL;
}
const char* XComboBox_itemText_2(const XComboBox* self, int index)
{
    if (self && index >= 0 && index < self->m_itemCount && self->m_items[index])
        return XString_toUtf8(self->m_items[index]);
    return "";
}
int XComboBox_findText(const XComboBox* self, const XString* text)
{
    int i;
    if (!self || !text) return -1;
    for (i = 0; i < self->m_itemCount; ++i)
        if (self->m_items[i] && XString_equals(self->m_items[i], text,
                                               XChar_CaseSensitive))
            return i;
    return -1;
}
int XComboBox_findText_2(const XComboBox* self, const char* text)
{
    int i;
    if (!self || !text) return -1;
    for (i = 0; i < self->m_itemCount; ++i)
        if (self->m_items[i] && XString_equals_utf8(self->m_items[i], text,
                                                    XChar_CaseSensitive))
            return i;
    return -1;
}

void XComboBox_setCurrentIndex(XComboBox* self, int index)
{
    int old;
    if (!self) return;
    if (index < -1) index = -1;
    if (index >= self->m_itemCount) index = self->m_itemCount - 1;
    old = self->m_currentIndex;
    if (index == old) return;
    self->m_currentIndex = index;
    xcombo_emitInt(self, (size_t)XComboBox_currentIndexChanged_signal(self, index), index);
    xcombo_emitText(self, (size_t)XComboBox_currentTextChanged_signal(
                            self, XComboBox_currentText_2(self)),
                    XComboBox_currentText_2(self));
    XWidget_update((XWidget*)self);
}

void XComboBox_setCurrentText(XComboBox* self, const XString* text)
{
    int idx;
    if (!self || !text) return;
    idx = XComboBox_findText(self, text);
    if (idx >= 0) XComboBox_setCurrentIndex(self, idx);
    else if (self->m_editable) XComboBox_setEditText(self, text);
}
void XComboBox_setCurrentText_2(XComboBox* self, const char* text)
{
    int idx;
    if (!self || !text) return;
    idx = XComboBox_findText_2(self, text);
    if (idx >= 0) XComboBox_setCurrentIndex(self, idx);
    else if (self->m_editable) XComboBox_setEditText_2(self, text);
}

void XComboBox_clearEditText(XComboBox* self)
{
    if (self && self->m_lineEdit) XLineEdit_clear(self->m_lineEdit);
}
void XComboBox_setEditText(XComboBox* self, const XString* text)
{
    if (self && self->m_lineEdit && text)
        XLineEdit_setText(self->m_lineEdit, XString_toUtf8(text));
}
void XComboBox_setEditText_2(XComboBox* self, const char* text)
{
    if (self && self->m_lineEdit) XLineEdit_setText(self->m_lineEdit, text);
}

void XComboBox_insertItem(XComboBox* self, int index, const XString* text)
{
    XString** grown;
    XString* copy;
    if (!self || !text) return;
    /* 对标 Qt：index = qBound(0, index, itemCount)（负数插到最前、
       超出项数则追加到尾部）。 */
    if (index < 0) index = 0;
    if (index > self->m_itemCount) index = self->m_itemCount;
    if (self->m_itemCount >= self->m_itemCapacity) {
        int newCap = self->m_itemCapacity > 0 ? self->m_itemCapacity * 2 : 8;
        grown = (XString**)XRealloc_System(self->m_items,
                                           sizeof(XString*) * (size_t)newCap);
        if (!grown) return;
        self->m_items = grown;
        if (self->m_itemData) {
            XString** g2 = (XString**)XRealloc_System(
                self->m_itemData, sizeof(XString*) * (size_t)newCap);
            if (!g2) return;
            self->m_itemData = g2;
        }
        if (self->m_itemIcons) {
            XString** g3 = (XString**)XRealloc_System(
                self->m_itemIcons, sizeof(XString*) * (size_t)newCap);
            if (!g3) return;
            self->m_itemIcons = g3;
        }
        self->m_itemCapacity = newCap;
    }
    copy = XString_create_copy(text);
    if (!copy) return;
    XMemmove(&self->m_items[index + 1], &self->m_items[index],
             sizeof(XString*) * (size_t)(self->m_itemCount - index));
    if (self->m_itemData)
        XMemmove(&self->m_itemData[index + 1], &self->m_itemData[index],
                 sizeof(XString*) * (size_t)(self->m_itemCount - index));
    if (self->m_itemIcons)
        XMemmove(&self->m_itemIcons[index + 1], &self->m_itemIcons[index],
                 sizeof(XString*) * (size_t)(self->m_itemCount - index));
    self->m_items[index] = copy;
    if (self->m_itemData) self->m_itemData[index] = NULL;
    if (self->m_itemIcons) self->m_itemIcons[index] = NULL;
    ++self->m_itemCount;
    /* 对标 Qt：插入后超出 maxCount 时从尾部裁剪。 */
    if (self->m_itemCount > self->m_maxCount) {
        while (self->m_itemCount > self->m_maxCount)
            XComboBox_removeItem(self, self->m_itemCount - 1);
    }
    /* 对标 Qt：插入不自动改变 currentIndex（首项插入也保持 -1，
       由调用方显式 setCurrentIndex）。仅当插入位置在当前项之前
       时当前项索引右移。 */
    if (index <= self->m_currentIndex) ++self->m_currentIndex;
    XWidget_update((XWidget*)self);
}
void XComboBox_insertItem_2(XComboBox* self, int index, const char* text)
{
    XString_Init_Utf8(tmp, text ? text : "");
    XComboBox_insertItem(self, index, tmp);
    XString_deinit_base(tmp);
}

void XComboBox_insertItems(XComboBox* self, int index, const XStringList* texts)
{
    int i, n;
    if (!self || !texts) return;
    n = (int)XVector_size_base((const XVector*)texts);
    for (i = 0; i < n; ++i) {
        XString* s = *(XString**)XStringList_at_base(texts, i);
        XComboBox_insertItem(self, index, s);
        if (index >= 0) ++index;
    }
}
void XComboBox_insertItems_2(XComboBox* self, int index, const char* const* texts)
{
    int i;
    if (!texts) return;
    for (i = 0; texts[i]; ++i) {
        XComboBox_insertItem_2(self, index, texts[i]);
        if (index >= 0) ++index;
    }
}

void XComboBox_addItem(XComboBox* self, const XString* text)
{
    /* 对标 Qt addItem：追加到尾部。 */
    XComboBox_insertItem(self, self ? self->m_itemCount : 0, text);
}
void XComboBox_addItem_2(XComboBox* self, const char* text)
{
    XComboBox_insertItem_2(self, self ? self->m_itemCount : 0, text);
}

void XComboBox_addItems(XComboBox* self, const XStringList* texts)
{
    /* 对标 Qt addItems：从当前项数处顺序追加。 */
    XComboBox_insertItems(self, self ? self->m_itemCount : 0, texts);
}
void XComboBox_addItems_2(XComboBox* self, const char* const* texts)
{
    XComboBox_insertItems_2(self, self ? self->m_itemCount : 0, texts);
}

void XComboBox_insertSeparator(XComboBox* self, int index)
{
    XComboBox_insertItem_2(self, index, "---------");
}

void XComboBox_removeItem(XComboBox* self, int index)
{
    XString* removed;
    if (!self || index < 0 || index >= self->m_itemCount) return;
    removed = self->m_items[index];
    XMemmove(&self->m_items[index], &self->m_items[index + 1],
             sizeof(XString*) * (size_t)(self->m_itemCount - index - 1));
    if (self->m_itemData) {
        if (self->m_itemData[index])
            XString_delete_base(self->m_itemData[index]);
        XMemmove(&self->m_itemData[index], &self->m_itemData[index + 1],
                 sizeof(XString*) * (size_t)(self->m_itemCount - index - 1));
    }
    if (self->m_itemIcons) {
        if (self->m_itemIcons[index])
            XString_delete_base(self->m_itemIcons[index]);
        XMemmove(&self->m_itemIcons[index], &self->m_itemIcons[index + 1],
                 sizeof(XString*) * (size_t)(self->m_itemCount - index - 1));
    }
    --self->m_itemCount;
    XString_delete_base(removed);
    if (self->m_currentIndex >= self->m_itemCount)
        self->m_currentIndex = self->m_itemCount - 1;
    XWidget_update((XWidget*)self);
}

void XComboBox_setItemText(XComboBox* self, int index, const XString* text)
{
    if (!self || !text || index < 0 || index >= self->m_itemCount) return;
    XString_assign(self->m_items[index], text);
    XWidget_update((XWidget*)self);
}
void XComboBox_setItemText_2(XComboBox* self, int index, const char* text)
{
    if (!self || !text || index < 0 || index >= self->m_itemCount) return;
    XString_assign_utf8(self->m_items[index], text);
    XWidget_update((XWidget*)self);
}

void XComboBox_clear(XComboBox* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_itemCount; ++i) {
        if (self->m_items[i]) XString_delete_base(self->m_items[i]);
        if (self->m_itemData && self->m_itemData[i]) {
            XString_delete_base(self->m_itemData[i]);
            self->m_itemData[i] = NULL;
        }
        if (self->m_itemIcons && self->m_itemIcons[i]) {
            XString_delete_base(self->m_itemIcons[i]);
            self->m_itemIcons[i] = NULL;
        }
    }
    self->m_itemCount = 0;
    self->m_currentIndex = -1;
    XWidget_update((XWidget*)self);
}

/* ==================== 弹出与选择 ==================== */

void XComboBox_showPopup_base(XComboBox* self)
{
    int rows;
    int newH;
    if (!self || self->m_popupVisible) return;
    self->m_popupVisible = true;
    g_comboPopupOffset = 0;
    /* 临时扩展高度以完整显示弹出列表（对标 QComboBox 弹出窗口）。 */
    self->m_savedHeight = XWidget_height((XWidget*)self);
    if (self->m_savedHeight < 20) self->m_savedHeight = 26;
    rows = self->m_itemCount;
    if (rows > self->m_maxVisibleItems) rows = self->m_maxVisibleItems;
    if (rows < 1) rows = 1;
    newH = rows * XCOMBOBOX_ITEM_H + 2;
    if (newH > self->m_savedHeight)
        XWidget_resize((XWidget*)self, XWidget_width((XWidget*)self), newH);
    XWidget_grabMouse((XWidget*)self);
    xcombo_emitInt(self, (size_t)XComboBox_popupShown_signal(self), 0);
    XWidget_update((XWidget*)self);
}

void XComboBox_hidePopup_base(XComboBox* self)
{
    if (!self || !self->m_popupVisible) return;
    int rh;
    XWidget* parent;
    self->m_popupVisible = false;
    rh = self->m_savedHeight > 0 ? self->m_savedHeight : 26;
    XWidget_resize((XWidget*)self, XWidget_width((XWidget*)self), rh);
    XWidget_releaseMouse((XWidget*)self);
    xcombo_emitInt(self, (size_t)XComboBox_popupHidden_signal(self), 0);
    XWidget_update((XWidget*)self);
    /* 收缩后暴露的区域属于父控件，必须让父控件重绘清除残留。 */
    parent = (XWidget*)XObject_parent((XObject*)self);
    if (parent) XWidget_update(parent);
}

bool XComboBox_popupVisible(const XComboBox* self)
{
    return self ? self->m_popupVisible : false;
}

/* ==================== 信号 ==================== */

void* XComboBox_activated_signal(XComboBox* self, int index)
{
    (void)index;
    return (void*)(size_t)XComboBox_activated_signal;
}
void* XComboBox_textActivated_signal(XComboBox* self, const char* text)
{
    (void)text;
    return (void*)(size_t)XComboBox_textActivated_signal;
}
void* XComboBox_highlighted_signal(XComboBox* self, int index)
{
    (void)index;
    return (void*)(size_t)XComboBox_highlighted_signal;
}
void* XComboBox_textHighlighted_signal(XComboBox* self, const char* text)
{
    (void)text;
    return (void*)(size_t)XComboBox_textHighlighted_signal;
}
void* XComboBox_currentIndexChanged_signal(XComboBox* self, int index)
{
    (void)index;
    return (void*)(size_t)XComboBox_currentIndexChanged_signal;
}
void* XComboBox_currentTextChanged_signal(XComboBox* self, const char* text)
{
    (void)text;
    return (void*)(size_t)XComboBox_currentTextChanged_signal;
}
void* XComboBox_editTextChanged_signal(XComboBox* self, const char* text)
{
    (void)text;
    return (void*)(size_t)XComboBox_editTextChanged_signal;
}
void* XComboBox_popupShown_signal(XComboBox* self)
{
    return (void*)(size_t)XComboBox_popupShown_signal;
}
void* XComboBox_popupHidden_signal(XComboBox* self)
{
    return (void*)(size_t)XComboBox_popupHidden_signal;
}

















/* ==================== Task 2.8：图标/数据/补全器 ==================== */

static XString** xcombo_ensureParallel(XString*** slot, int capacity)
{
    XString** arr;
    if (!slot) return NULL;
    if (!*slot) {
        arr = (XString**)XCalloc_System((size_t)(capacity > 0 ? capacity : 8),
                                        sizeof(XString*));
        if (arr) *slot = arr;
        return *slot;
    }
    return *slot;
}

void XComboBox_setItemIcon(XComboBox* self, int index, const XString* path)
{
    XString* repl;
    if (!self || index < 0 || index >= self->m_itemCount) return;
    if (!self->m_itemIcons)
        xcombo_ensureParallel(&self->m_itemIcons, self->m_itemCapacity);
    if (!self->m_itemIcons) return;
    repl = path ? XString_create_copy(path) : NULL;
    if (path && !repl) return;
    if (self->m_itemIcons[index])
        XString_delete_base(self->m_itemIcons[index]);
    self->m_itemIcons[index] = repl;
    XWidget_update((XWidget*)self);
}
void XComboBox_setItemIcon_2(XComboBox* self, int index, const char* path)
{
    XString* tmp = NULL;
    if (path) {
        tmp = XString_create_utf8(path);
        if (!tmp) return;
    }
    XComboBox_setItemIcon(self, index, tmp);
    if (tmp) XString_delete_base(tmp);
}
const XString* XComboBox_itemIcon(const XComboBox* self, int index)
{
    if (!self || index < 0 || index >= self->m_itemCount ||
        !self->m_itemIcons)
        return NULL;
    return self->m_itemIcons[index];
}
const char* XComboBox_itemIcon_2(const XComboBox* self, int index)
{
    const XString* s;
    s = XComboBox_itemIcon(self, index);
    return s ? XString_toUtf8(s) : "";
}

void XComboBox_setItemData(XComboBox* self, int index, const XString* data)
{
    XString* repl;
    if (!self || index < 0 || index >= self->m_itemCount) return;
    if (!self->m_itemData)
        xcombo_ensureParallel(&self->m_itemData, self->m_itemCapacity);
    if (!self->m_itemData) return;
    repl = data ? XString_create_copy(data) : NULL;
    if (data && !repl) return;
    if (self->m_itemData[index])
        XString_delete_base(self->m_itemData[index]);
    self->m_itemData[index] = repl;
}
void XComboBox_setItemData_2(XComboBox* self, int index, const char* data)
{
    XString* tmp = NULL;
    if (data) {
        tmp = XString_create_utf8(data);
        if (!tmp) return;
    }
    XComboBox_setItemData(self, index, tmp);
    if (tmp) XString_delete_base(tmp);
}
const XString* XComboBox_itemData(const XComboBox* self, int index)
{
    if (!self || index < 0 || index >= self->m_itemCount ||
        !self->m_itemData)
        return NULL;
    return self->m_itemData[index];
}
const char* XComboBox_itemData_2(const XComboBox* self, int index)
{
    const XString* s;
    s = XComboBox_itemData(self, index);
    return s ? XString_toUtf8(s) : "";
}

const XString* XComboBox_currentData(const XComboBox* self)
{
    return XComboBox_itemData(self, XComboBox_currentIndex(self));
}

int XComboBox_findData(const XComboBox* self, const XString* data)
{
    int i;
    if (!self || !data) return -1;
    for (i = 0; i < self->m_itemCount; ++i) {
        if (self->m_itemData && self->m_itemData[i] &&
            XString_equals(self->m_itemData[i], data, XChar_CaseSensitive))
            return i;
    }
    return -1;
}
int XComboBox_findData_2(const XComboBox* self, const char* data)
{
    XString* tmp = NULL;
    int out;
    if (!data) return -1;
    tmp = XString_create_utf8(data);
    if (!tmp) return -1;
    out = XComboBox_findData(self, tmp);
    XString_delete_base(tmp);
    return out;
}

void XComboBox_setCompleter(XComboBox* self, XCompleter* completer)
{ if (self) self->m_completer = completer; }
XCompleter* XComboBox_completer(const XComboBox* self)
{ return self ? self->m_completer : NULL; }

void XComboBox_setIconSize(XComboBox* self, int size)
{
    if (self && size > 0) {
        self->m_iconSize = size;
        XWidget_update((XWidget*)self);
    }
}
int XComboBox_iconSize(const XComboBox* self)
{ return self ? self->m_iconSize : 0; }

#endif /* XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON */
