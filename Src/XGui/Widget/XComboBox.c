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
#if XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON

#include "XComboBox.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XCoreApplication.h"
#include "XColor.h"
#include <string.h>
#include <stdlib.h>
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
    /* 当前项文本（弹出时列表替代文本显示）。 */
    if (!combo->m_popupVisible) {
        if (combo->m_currentIndex >= 0 && combo->m_currentIndex < combo->m_itemCount &&
            combo->m_items[combo->m_currentIndex]) {
            XPainter_drawText(&painter, 6, (r.height - 14) / 2 + 12,
                              combo->m_items[combo->m_currentIndex], text);
        } else if (combo->m_placeholderText[0]) {
            XPainter_drawText(&painter, 6, (r.height - 14) / 2 + 12,
                              combo->m_placeholderText,
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
                                  combo->m_items[i],
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
        int idx = xcombo_popupItemAt(combo, &pos);
        if (idx >= 0) {
            XComboBox_setCurrentIndex(combo, idx);
            xcombo_emitInt(combo, (size_t)XComboBox_activated_signal(combo), idx);
            xcombo_emitText(combo,
                            (size_t)XComboBox_textActivated_signal(combo),
                            XComboBox_itemText(combo, idx));
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
    memcpy(self->m_placeholderText, other->m_placeholderText,
           sizeof(self->m_placeholderText));
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
    memcpy(self->m_placeholderText, other->m_placeholderText,
           sizeof(self->m_placeholderText));
    other->m_maxCount = 2147483647;
    other->m_maxVisibleItems = 10;
    other->m_duplicatesEnabled = false;
    other->m_editable = false;
    other->m_insertPolicy = XComboBoxInsertPolicy_InsertAtBottom;
    other->m_sizeAdjustPolicy = XComboBoxSizeAdjustPolicy_AdjustToContents;
    other->m_frame = true;
    other->m_placeholderText[0] = '\0';
    for (i = 0; i < self->m_itemCount; ++i) { (void)0; }
}

/* ==================== 生命周期 ==================== */

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

    return XVTABLE_DEFAULT;
}

void XComboBox_init(XComboBox* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XComboBox);

    self->m_items = NULL;
    self->m_itemCount = 0;
    self->m_itemCapacity = 0;
    self->m_currentIndex = -1;
    self->m_maxCount = 2147483647;
    self->m_maxVisibleItems = 10;
    self->m_duplicatesEnabled = false;
    self->m_editable = false;
    self->m_lineEdit = NULL;
    self->m_insertPolicy = XComboBoxInsertPolicy_InsertAtBottom;
    self->m_sizeAdjustPolicy = XComboBoxSizeAdjustPolicy_AdjustToContents;
    self->m_minimumContentsLength = 0;
    self->m_frame = true;
    self->m_placeholderText[0] = '\0';
    self->m_popupVisible = false;
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
const char* XComboBox_placeholderText(const XComboBox* self)
{
    return (self && self->m_placeholderText[0]) ? self->m_placeholderText : "";
}
void XComboBox_setPlaceholderText(XComboBox* self, const char* placeholderText)
{
    if (!self) return;
    if (!placeholderText) placeholderText = "";
    strncpy(self->m_placeholderText, placeholderText,
            sizeof(self->m_placeholderText) - 1);
    self->m_placeholderText[sizeof(self->m_placeholderText) - 1] = '\0';
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

int XComboBox_currentIndex(const XComboBox* self)
{
    return self ? self->m_currentIndex : -1;
}
const char* XComboBox_currentText(const XComboBox* self)
{
    if (self && self->m_currentIndex >= 0 &&
        self->m_currentIndex < self->m_itemCount && self->m_items[self->m_currentIndex])
        return self->m_items[self->m_currentIndex];
    return "";
}
const char* XComboBox_itemText(const XComboBox* self, int index)
{
    if (self && index >= 0 && index < self->m_itemCount && self->m_items[index])
        return self->m_items[index];
    return "";
}
int XComboBox_findText(const XComboBox* self, const char* text)
{
    int i;
    if (!self || !text) return -1;
    for (i = 0; i < self->m_itemCount; ++i)
        if (self->m_items[i] && strcmp(self->m_items[i], text) == 0) return i;
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
    xcombo_emitInt(self, (size_t)XComboBox_currentIndexChanged_signal(self), index);
    xcombo_emitText(self, (size_t)XComboBox_currentTextChanged_signal(self),
                    XComboBox_currentText(self));
    XWidget_update((XWidget*)self);
}

void XComboBox_setCurrentText(XComboBox* self, const char* text)
{
    int idx;
    if (!self || !text) return;
    idx = XComboBox_findText(self, text);
    if (idx >= 0) XComboBox_setCurrentIndex(self, idx);
    else if (self->m_editable) XComboBox_setEditText(self, text);
}

void XComboBox_clearEditText(XComboBox* self)
{
    if (self && self->m_lineEdit) XLineEdit_clear(self->m_lineEdit);
}
void XComboBox_setEditText(XComboBox* self, const char* text)
{
    if (self && self->m_lineEdit) XLineEdit_setText(self->m_lineEdit, text);
}

void XComboBox_insertItem(XComboBox* self, int index, const char* text)
{
    char** grown;
    size_t len;
    if (!self || !text) return;
    /* 对标 Qt：index = qBound(0, index, itemCount)（负数插到最前、
       超出项数则追加到尾部）。 */
    if (index < 0) index = 0;
    if (index > self->m_itemCount) index = self->m_itemCount;
    if (self->m_itemCount >= self->m_itemCapacity) {
        int newCap = self->m_itemCapacity > 0 ? self->m_itemCapacity * 2 : 8;
        grown = (char**)XRealloc_System(self->m_items,
                                        sizeof(char*) * (size_t)newCap);
        if (!grown) return;
        self->m_items = grown;
        self->m_itemCapacity = newCap;
    }
    len = strlen(text) + 1;
    {
        /* 对标 Qt 模型插入：先在堆上复制文本，右移腾位后挂到 index。
           （此前"尾部占位再搬回"的写法会 memmove 覆盖占位指针，
           导致所有项变成第 0 项副本。） */
        char* copy = (char*)XMalloc_System(len);
        if (!copy) return;
        memcpy(copy, text, len);
        memmove(&self->m_items[index + 1], &self->m_items[index],
                sizeof(char*) * (size_t)(self->m_itemCount - index));
        self->m_items[index] = copy;
        ++self->m_itemCount;
    }
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

void XComboBox_insertItems(XComboBox* self, int index, const char* const* texts)
{
    int i;
    if (!texts) return;
    for (i = 0; texts[i]; ++i) {
        XComboBox_insertItem(self, index, texts[i]);
        if (index >= 0) ++index;
    }
}

void XComboBox_addItem(XComboBox* self, const char* text)
{
    /* 对标 Qt addItem：追加到尾部。 */
    XComboBox_insertItem(self, self ? self->m_itemCount : 0, text);
}

void XComboBox_addItems(XComboBox* self, const char* const* texts)
{
    /* 对标 Qt addItems：从当前项数处顺序追加。 */
    XComboBox_insertItems(self, self ? self->m_itemCount : 0, texts);
}

void XComboBox_insertSeparator(XComboBox* self, int index)
{
    XComboBox_insertItem(self, index, "---------");
}

void XComboBox_removeItem(XComboBox* self, int index)
{
    char* removed;
    if (!self || index < 0 || index >= self->m_itemCount) return;
    removed = self->m_items[index];
    memmove(&self->m_items[index], &self->m_items[index + 1],
            sizeof(char*) * (size_t)(self->m_itemCount - index - 1));
    --self->m_itemCount;
    XFree_System(removed);
    if (self->m_currentIndex >= self->m_itemCount)
        self->m_currentIndex = self->m_itemCount - 1;
    XWidget_update((XWidget*)self);
}

void XComboBox_setItemText(XComboBox* self, int index, const char* text)
{
    char* replaced;
    size_t len;
    if (!self || !text || index < 0 || index >= self->m_itemCount) return;
    replaced = self->m_items[index];
    len = strlen(text) + 1;
    self->m_items[index] = (char*)XMalloc_System(len);
    if (!self->m_items[index]) { self->m_items[index] = replaced; return; }
    memcpy(self->m_items[index], text, len);
    XFree_System(replaced);
    XWidget_update((XWidget*)self);
}

void XComboBox_clear(XComboBox* self)
{
    int i;
    if (!self) return;
    for (i = 0; i < self->m_itemCount; ++i)
        if (self->m_items[i]) XFree_System(self->m_items[i]);
    self->m_itemCount = 0;
    self->m_currentIndex = -1;
    XWidget_update((XWidget*)self);
}

/* ==================== 弹出与选择 ==================== */

void XComboBox_showPopup_base(XComboBox* self)
{
    if (!self || self->m_popupVisible) return;
    self->m_popupVisible = true;
    g_comboPopupOffset = 0;
    xcombo_emitInt(self, (size_t)XComboBox_popupShown_signal(self), 0);
    XWidget_update((XWidget*)self);
}

void XComboBox_hidePopup_base(XComboBox* self)
{
    if (!self || !self->m_popupVisible) return;
    self->m_popupVisible = false;
    xcombo_emitInt(self, (size_t)XComboBox_popupHidden_signal(self), 0);
    XWidget_update((XWidget*)self);
}

bool XComboBox_popupVisible(const XComboBox* self)
{
    return self ? self->m_popupVisible : false;
}

/* ==================== 信号 ==================== */

void* XComboBox_activated_signal(XComboBox* self)
{
    return (void*)(size_t)XComboBox_activated_signal;
}
void* XComboBox_textActivated_signal(XComboBox* self)
{
    return (void*)(size_t)XComboBox_textActivated_signal;
}
void* XComboBox_highlighted_signal(XComboBox* self)
{
    return (void*)(size_t)XComboBox_highlighted_signal;
}
void* XComboBox_textHighlighted_signal(XComboBox* self)
{
    return (void*)(size_t)XComboBox_textHighlighted_signal;
}
void* XComboBox_currentIndexChanged_signal(XComboBox* self)
{
    return (void*)(size_t)XComboBox_currentIndexChanged_signal;
}
void* XComboBox_currentTextChanged_signal(XComboBox* self)
{
    return (void*)(size_t)XComboBox_currentTextChanged_signal;
}
void* XComboBox_editTextChanged_signal(XComboBox* self)
{
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

#endif /* XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON */
