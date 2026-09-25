/**
 * @file       XComboBox.c
 * @brief      XComboBox 下拉组合框控件实现（对标 Qt 6.8 QComboBox）。
 * @details    内部结构：文本项存于 char** 动态数组（每项独立分配）。
 *             显示区绘制当前项文本 + 下拉箭头按钮；左键点击弹出项
 *             列表（popupVisible 状态内嵌绘制——选中高亮跟随鼠标）；
 *             可编辑模式内嵌 XLineEdit 占满左侧（编辑文本经
 *             editTextChanged/InsertPolicy 插入语义）。
 *             补全（可编辑 + completerMode）：输入前缀经内嵌行编辑子类
 *             （XComboEdit）拦截键盘，textEdited 驱动复用下拉弹层的
 *             前缀过滤（XListView 行隐藏承载，大小写不敏感，对标
 *             QCompleter PopupCompletion）；Enter 采纳高亮补全，Esc
 *             收起弹层不改文本，Up/Down/PageUp/PageDown 移动高亮。
 *             插入策略（对标 Qt 6.8 QComboBoxPrivate::returnPressed/
 *             editingFinished）：可编辑文本在 Enter（returnPressed）
 *             与失焦（editingFinished）结算 insertPolicy；NoInsert 不
 *             插入；duplicates 关闭且文本已存在仅置当前项；
 *             InsertAtCurrent 以编辑文本替换当前项文本（无当前项时
 *             AtCurrent/After/Before 不动作，对标 Qt）；Enter 结算
 *             后发射 activated/textActivated。
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
#include "XCompleter.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XString.h"
#include "XWidget_Protected.h"
#include "XListView.h"
#include "XAbstractItemModel.h"
#include "XAbstractItemView.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XWindow.h"
#include "XCoreApplication.h"
#include "XGuiApplication.h"
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
static void  VXComboBox_timerEvent(XObject* object, XTimerEvent* event);
static void  xcombo_releaseGrab(XComboBox* self);
static void  xcombo_completionRefresh(XComboBox* self, const char* prefix);
static void  xcombo_completionAccept(XComboBox* self);
static void  xcombo_completionNavigate(XComboBox* self, int step);

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
    /* 当前项文本（部件化后列表由独立弹出视图承载，自身恒画文本）。 */
    {
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
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 左键：弹出/收起切换；弹出中点击行 = 选择。 */
static void VXComboBox_mousePressEvent(XWidget* self, XEvent* event)
{
    XComboBox* combo = (XComboBox*)self;
    XMouseEvent* me;
    if (!combo || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    /* 部件化弹窗：自身点击 = 弹出/收起切换；行选择由弹出视图的
       activated 信号联动（xcombo_viewActivatedSlot）。 */
    if (combo->m_popupVisible)
        XComboBox_hidePopup_base(combo);
    else
        XComboBox_showPopup_base(combo);
    XEvent_accept(event);
}

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
    self->m_completerMode = other->m_completerMode;
    /* 补全弹层为瞬态：拷贝不继承弹层状态（对标拷贝后弹层关闭）。 */
    self->m_completionActive = false;
    self->m_completionRow = -1;
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
    self->m_completerMode = other->m_completerMode;
    /* 补全弹层为瞬态：移动后按关闭态落位（弹层窗口本身不随迁）。 */
    self->m_completionActive = false;
    self->m_completionRow = -1;
    self->m_insertPolicy = other->m_insertPolicy;
    self->m_sizeAdjustPolicy = other->m_sizeAdjustPolicy;
    self->m_minimumContentsLength = other->m_minimumContentsLength;
    self->m_frame = other->m_frame;
    if (self->m_placeholderText)
        XString_delete_base((XClass*)self->m_placeholderText);
    self->m_placeholderText = other->m_placeholderText;
    other->m_placeholderText = XString_create();
    other->m_maxCount = 2147483647;
    other->m_maxVisibleItems = 10;
    other->m_duplicatesEnabled = false;
    other->m_editable = false;
    other->m_completerMode = false;
    other->m_completionActive = false;
    other->m_completionRow = -1;
    other->m_insertPolicy = XComboBoxInsertPolicy_InsertAtBottom;
    other->m_sizeAdjustPolicy = XComboBoxSizeAdjustPolicy_AdjustToContents;
    other->m_frame = true;
    /* m_placeholderText 已转移并重建。 */
    for (i = 0; i < self->m_itemCount; ++i) { (void)0; }
}

/* ==================== 延迟抓取定时器（参照 XMenu） ==================== */

/** @brief 弹出后的延迟抓取定时器：窗口映射完成后执行平台 XGrabPointer，
 *  使点击弹窗外部的按键都送达弹窗（模态关闭）。 */
static void VXComboBox_timerEvent(XObject* object, XTimerEvent* event)
{
    XComboBox* self = (XComboBox*)object;
    XTimerId id;
    XWindow* handle;

    if (self && event &&
        XTimerEvent_timerId(event) == self->m_grabTimer) {
        id = self->m_grabTimer;
        self->m_grabTimer = XTIMER_INVALID_ID;
        XObject_killTimer((XObject*)self, id);
        /* 弹窗可能在定时器触发前已被收起：仅在仍弹出时启用平台抓取。 */
        if (self->m_popupVisible && self->m_popupView) {
            handle = XWidget_windowHandle((XWidget*)self->m_popupView);
            if (handle) {
                XWindow_setMouseGrabEnabled(handle, true);
                /* 对标 Qt 弹层双抓取（grabForPopup：鼠标+键盘成对）：
                   平台 XGrabKeyboard 需窗口完成映射，与 XGrabPointer
                   同在此延迟点执行；Esc/方向键由此直达弹层。 */
                XWindow_setKeyboardGrabEnabled(handle, true);
            }
        }
        XEvent_accept((XEvent*)event);
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(object, event);
}

/* ==================== 生命周期 ==================== */

static void VXComboBox_deinit(XComboBox* self)
{
    int i;
    if (!self) return;
    /* 先解除弹窗模态抓取（内部引用弹窗指针，须先于删除视图执行）。 */
    xcombo_releaseGrab(self);
    if (self->m_placeholderText) {
        XString_delete_base((XClass*)self->m_placeholderText);
        self->m_placeholderText = NULL;
    }
    if (self->m_popupView) {
        XListView_delete_base((XClass*)self->m_popupView);
        self->m_popupView = NULL;
    }
    if (self->m_model) {
        XAbstractItemModel_delete_base((XClass*)self->m_model);
        self->m_model = NULL;
    }
    for (i = 0; i < self->m_itemCount; ++i) {
        if (self->m_items[i]) XString_delete_base((XClass*)self->m_items[i]);
        if (self->m_itemData && self->m_itemData[i])
            XString_delete_base((XClass*)self->m_itemData[i]);
        if (self->m_itemIcons && self->m_itemIcons[i])
            XString_delete_base((XClass*)self->m_itemIcons[i]);
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

/** @brief 键盘：Up/Down/Home/End 改当前项（对标 QComboBox 键盘导航；
 *  可编辑模式下焦点在内嵌编辑框，本路径仅服务非编辑焦点）。 */
static void VXComboBox_keyPressEvent(XWidget* self, XEvent* event)
{
    XComboBox* combo = (XComboBox*)self;
    XKeyEvent* ke;
    int key;
    int idx;
    int count;
    if (!combo || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) return;
    if (combo->m_editable) { XEvent_ignore(event); return; }
    ke = (XKeyEvent*)event;
    key = ke->m_key;
    idx = XComboBox_currentIndex(combo);
    count = XComboBox_count(combo);
    if (key == (int)XKey_Up && idx > 0)
        XComboBox_setCurrentIndex(combo, idx - 1);
    else if (key == (int)XKey_Down && idx + 1 < count)
        XComboBox_setCurrentIndex(combo, idx + 1);
    else if (key == (int)XKey_Home && count > 0)
        XComboBox_setCurrentIndex(combo, 0);
    else if (key == (int)XKey_End && count > 0)
        XComboBox_setCurrentIndex(combo, count - 1);
    else {
        XEvent_ignore(event);
        return;
    }
    XEvent_accept(event);
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
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXComboBox_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXComboBox_timerEvent);
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
    self->m_completerMode = false;
    self->m_completionActive = false;
    self->m_completionRow = -1;
    self->m_insertPolicy = XComboBoxInsertPolicy_InsertAtBottom;
    self->m_sizeAdjustPolicy = XComboBoxSizeAdjustPolicy_AdjustToContents;
    self->m_minimumContentsLength = 0;
    self->m_frame = true;
    self->m_placeholderText = XString_create();
    self->m_popupVisible = false;
    self->m_popupView = NULL;
    self->m_model = NULL;
    self->m_modelColumn = 0;
    self->m_rootRow = 0;
    self->m_rootCol = 0;
    self->m_validator = NULL;
    self->m_itemDelegate = NULL;
    self->m_grabTimer = XTIMER_INVALID_ID;
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

/* ==================== 可编辑补全与插入策略 ==================== */
/*
 * 对标 Qt 6.8 QComboBox 可编辑路径：
 * - 补全：QCompleter PopupCompletion 子集——输入前缀过滤弹层（复用
 *   下拉弹层，XListView 行隐藏承载过滤），Enter 采纳、Esc 收起不改
 *   文本、Up/Down/PageUp/PageDown 移动高亮；
 * - 插入策略：QComboBoxPrivate::returnPressed（Enter 结算 insertPolicy）
 *   与 QComboBoxPrivate::editingFinished（失焦/Enter 后置同步命中项）。
 */

/** @brief ASCII 字母小写折叠（UTF-8 其余字节原样；UTF-8 自同步保证
 *  字节级前缀比较等价字符级，仅大小写折叠限 ASCII，对标 QCompleter
 *  默认 CaseInsensitive 的简化承载）。 */
static char xcombo_asciiLower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

/** @brief 大小写不敏感前缀匹配：item 以 prefix（UTF-8）开头返回 true。 */
static bool xcombo_prefixMatchUtf8(const char* item, const char* prefix)
{
    size_t i;
    if (!item || !prefix) return false;
    for (i = 0; prefix[i]; ++i) {
        if (item[i] == '\0') return false;
        if (xcombo_asciiLower(item[i]) != xcombo_asciiLower(prefix[i]))
            return false;
    }
    return true;
}

/** @brief 大小写不敏感 UTF-8 相等（对标 completer 情形 matchFlags 的
 *  MatchFixedString|CaseInsensitive；completer 关闭时走 findText 大小写
 *  敏感路径，见 xcombo_findTextMatch）。 */
static bool xcombo_equalsInsensitiveUtf8(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        if (xcombo_asciiLower(*a) != xcombo_asciiLower(*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/** @brief 大小写不敏感比较 a < b（对标 InsertAlphabetically 的
 *  text.toLower() < itemText(i).toLower()）。
 * @note  Qt 侧 toLower() 比较是 UTF-16 码点序：ASCII 折叠大小写后按
 *  码点比；非 ASCII（如中文）无大小写，按码点比。UTF-8 的自同步编码
 *  保证无符号字节序 == 码点序，故非 ASCII 字节须以 unsigned char 比较
 *  （原实现按有符号 char，>=0x80 字节为负，中文恒排 ASCII 之前，且
 *  前缀分支返回值颠倒——"ab" vs "abc" 误判 a 不小于 b，顺序错插）。 */
static bool xcombo_lessInsensitiveUtf8(const char* a, const char* b)
{
    if (!a || !b) return false;
    while (*a && *b) {
        unsigned char la = (unsigned char)xcombo_asciiLower(*a);
        unsigned char lb = (unsigned char)xcombo_asciiLower(*b);
        if (la != lb) return la < lb;
        ++a;
        ++b;
    }
    return *b != '\0'; /* a 为 b 真前缀时 a < b；全等返回 false。 */
}

/** @brief 编辑结束查找：补全开启时大小写不敏感（对标 Qt matchFlags
 *  随 completer 大小写性取值），否则沿用 findText 大小写敏感语义。 */
static int xcombo_findTextMatch(const XComboBox* self, const char* text)
{
    int i;
    if (!self || !text) return -1;
    if (!self->m_completerMode) return XComboBox_findText_2(self, text);
    for (i = 0; i < self->m_itemCount; ++i)
        if (self->m_items[i] &&
            xcombo_equalsInsensitiveUtf8(XString_toUtf8(self->m_items[i]),
                                         text))
            return i;
    return -1;
}

/** @brief 发射既有 activated(int) + textActivated(const char*) 信号对
 *  （对标 QComboBoxPrivate::emitActivated）。 */
static void xcombo_emitActivatedPair(XComboBox* self, int index)
{
    xcombo_emitInt(self, (size_t)XComboBox_activated_signal(self, index),
                   index);
    xcombo_emitText(self,
                    (size_t)XComboBox_textActivated_signal(
                        self, XComboBox_itemText_2(self, index)),
                    XComboBox_itemText_2(self, index));
}

/** @brief 结算插入策略（对标 QComboBoxPrivate::returnPressed 主体）。
 * @param userActivated true=Enter 用户激活（结算后发射 activated 对）；
 *                      false=失焦结算（仅插入/置当前项，不发射激活）。
 * @note  对标 Qt：NoInsert 直接返回；空文本返回；项数达 maxCount 且非
 *        InsertAtCurrent 返回；duplicates 关闭且文本已存在仅置当前项；
 *        InsertAtCurrent 为替换当前项文本（不插入）；AtCurrent/
 *        AfterCurrent/BeforeCurrent 在列表空/无当前项时不动作
 *        （对标 QComboBoxPrivate::_q_returnPressed 的无效当前项 return）。
 */
static void xcombo_insertByPolicy(XComboBox* self, bool userActivated)
{
    const char* text;
    int policy;
    int index = -1;
    int i;
    if (!self || !self->m_editable || !self->m_lineEdit) return;
    policy = self->m_insertPolicy;
    if (policy == XComboBoxInsertPolicy_NoInsert) return;
    text = XLineEdit_text(self->m_lineEdit);
    if (!text || !text[0]) return;
    if (self->m_itemCount >= self->m_maxCount &&
        policy != XComboBoxInsertPolicy_InsertAtCurrent)
        return;
    /* 对标 Qt：结算前 deselect 并把光标移到行尾。 */
    XLineEdit_deselect(self->m_lineEdit);
    XLineEdit_end(self->m_lineEdit, false);
    if (!self->m_duplicatesEnabled) {
        index = xcombo_findTextMatch(self, text);
        if (index >= 0) {
            XComboBox_setCurrentIndex(self, index);
            if (userActivated) xcombo_emitActivatedPair(self, index);
            return;
        }
    }
    switch (policy) {
    case XComboBoxInsertPolicy_InsertAtTop:
        index = 0;
        break;
    case XComboBoxInsertPolicy_InsertAtBottom:
        index = self->m_itemCount;
        break;
    case XComboBoxInsertPolicy_InsertAtCurrent:
    case XComboBoxInsertPolicy_InsertAfterCurrent:
    case XComboBoxInsertPolicy_InsertBeforeCurrent:
        /* 对标 Qt 6.8 QComboBoxPrivate::_q_returnPressed：AtCurrent/
           AfterCurrent/BeforeCurrent 在 currentIndex 无效（列表空/无
           当前项）时直接 return 不动作，不退化为插入到 0。 */
        if (self->m_itemCount == 0 || self->m_currentIndex < 0 ||
            self->m_currentIndex >= self->m_itemCount)
            return;
        if (policy == XComboBoxInsertPolicy_InsertAtCurrent) {
            /* 替换当前项文本即完成（Qt：不再插入、不发射激活）。 */
            XComboBox_setItemText_2(self, self->m_currentIndex, text);
            return;
        } else if (policy == XComboBoxInsertPolicy_InsertAfterCurrent) {
            index = self->m_currentIndex + 1;
        } else {
            index = self->m_currentIndex;
        }
        break;
    case XComboBoxInsertPolicy_InsertAlphabetically:
        index = 0;
        for (i = 0; i < self->m_itemCount; ++i, ++index) {
            if (xcombo_lessInsensitiveUtf8(
                    text, XComboBox_itemText_2(self, i)))
                break;
        }
        break;
    default:
        break;
    }
    if (index >= 0) {
        XComboBox_insertItem_2(self, index, text);
        XComboBox_setCurrentIndex(self, index);
        if (userActivated)
            xcombo_emitActivatedPair(self, XComboBox_currentIndex(self));
    }
}

/** @brief 转发槽：内嵌编辑框 textChanged → editTextChanged(text)
 *  （此前 editTextChanged 仅声明无发射点，永不触发）。 */
static void xcombo_editTextChangedFwd(XObject* receiver, XVarList* args)
{
    XComboBox* self = (XComboBox*)receiver;
    const char* text = NULL;
    if (!args || !self) {
        if (args) XVarList_delete(args);
        return;
    }
    XVarList_args_1(args, const char*, t);
    text = t ? t : "";
    if (((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
                           (size_t)XComboBox_editTextChanged_signal,
                           args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/** @brief 转发槽：编辑框 textEdited（仅用户编辑）→ 补全过滤刷新
 *  （对标 QCompleter 经行编辑用户输入驱动 setCompletionPrefix；
 *  程序化 setEditText 不弹补全）。 */
static void xcombo_editTextEditedSlot(XObject* receiver, XVarList* args)
{
    XComboBox* self = (XComboBox*)receiver;
    const char* text = NULL;
    if (args) {
        XVarList_args_1(args, const char*, t);
        text = t;
    }
    if (!self || !self->m_editable || !self->m_completerMode ||
        !self->m_lineEdit)
        return;
    xcombo_completionRefresh(self, text ? text
                                        : XLineEdit_text(self->m_lineEdit));
}

/** @brief 转发槽：编辑框 returnPressed（Enter）→ 结算插入策略（用户
 *  激活路径，结算后发射 activated 对；对标 Qt returnPressed 连接）。 */
static void xcombo_editReturnPressedSlot(XObject* receiver, XVarList* args)
{
    XComboBox* self = (XComboBox*)receiver;
    (void)args;
    if (!self || !self->m_editable || !self->m_lineEdit) return;
    /* 补全弹层存活时 Enter 已被 XComboEdit 按键路径拦截采纳，不落此
       结算（双保险：弹层存活直接忽略）。 */
    if (self->m_popupVisible) return;
    xcombo_insertByPolicy(self, true);
}

/** @brief 转发槽：编辑框 editingFinished（Return 后沿或失焦）→ 同步
 *  命中项/失焦插入结算（对标 QComboBoxPrivate::editingFinished）。
 * @note  Qt 6.8.3 失焦只做"命中即置当前项+激活"同步；本实现在此之上
 *  把未命中文本也按 insertPolicy 插入（任务规定的编辑结束语义，差异
 *  见头文件注记）。Enter 双发场景由"文本==当前项文本"早退自然去重。 */
static void xcombo_editEditingFinishedSlot(XObject* receiver, XVarList* args)
{
    XComboBox* self = (XComboBox*)receiver;
    const char* text;
    int index;
    (void)args;
    if (!self || !self->m_editable || !self->m_lineEdit) return;
    /* 补全弹层存活时挂起结算（对标 Qt editingFinished 的 completer
     * popup 可见早退），仅收起弹层不改文本。 */
    if (self->m_popupVisible) {
        XComboBox_hidePopup_base(self);
        return;
    }
    text = XLineEdit_text(self->m_lineEdit);
    if (!text || !text[0]) return;
    if (self->m_currentIndex >= 0 &&
        self->m_currentIndex < self->m_itemCount &&
        XString_toUtf8(self->m_items[self->m_currentIndex]) &&
        XStrcmp(XString_toUtf8(self->m_items[self->m_currentIndex]),
                text) == 0)
        return;
    index = xcombo_findTextMatch(self, text);
    if (index >= 0) {
        XComboBox_setCurrentIndex(self, index);
        xcombo_emitActivatedPair(self, index);
        return;
    }
    xcombo_insertByPolicy(self, false);
}

/** @brief 连接编辑框 → 组合框的编辑联动信号（textChanged 转发、
 *  textEdited 补全刷新、returnPressed/editingFinished 插入策略结算）；
 *  内嵌与外部安装的编辑框（setLineEdit）共用，对标 Qt 对行编辑的
 *  信号挂接不区分安装方式。 */
static void xcombo_connectLineEdit(XComboBox* self, XLineEdit* edit)
{
    if (!self || !edit) return;
    XObject_connect_1((XObject*)edit,
                      (size_t)XLineEdit_textChanged_signal(edit),
                      (XObject*)self, xcombo_editTextChangedFwd,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)edit,
                      (size_t)XLineEdit_textEdited_signal(edit),
                      (XObject*)self, xcombo_editTextEditedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)edit,
                      (size_t)XLineEdit_returnPressed_signal(edit),
                      (XObject*)self, xcombo_editReturnPressedSlot,
                      XConnectionType_Direct);
    XObject_connect_1((XObject*)edit,
                      (size_t)XLineEdit_editingFinished_signal(edit),
                      (XObject*)self, xcombo_editEditingFinishedSlot,
                      XConnectionType_Direct);
}

/*
 * 内嵌行编辑子类（XComboEdit）：XLineEdit 不可直接修改，参照
 * XComboPopupView 方案本地派生，覆写 keyPressEvent 拦截补全弹层存活
 * 期间的 Enter/Esc/Up/Down/PageUp/PageDown（其余按键原样交给基类控制
 * 器正常编辑）。外部安装的编辑框无此子类，Esc/方向键拦截为内嵌路径
 * 专属简化（Enter/失焦结算经信号连接对两者一致生效）。
 */
XCLASS_DEFINE_BEGING(XComboEdit)
XCLASS_DEFINE_EXTEND_END(XComboEdit, XLineEdit)

/** @brief 组合框内嵌行编辑对象；m_base 必须是第一个成员。 */
typedef struct XComboEdit
{
    XLineEdit  m_base;   /**< 基类成员（嵌 XLineEdit）；必须是第一个。 */
    XComboBox* m_owner;  /**< 属主组合框（借用；可为 NULL）。 */
} XComboEdit;

static void VXComboEdit_keyPressEvent(XWidget* self, XEvent* event);

/** @brief 键盘：补全弹层存活期间拦截 Enter（采纳）/Esc（收起不改文
 *  本）/Up/Down/PageUp/PageDown（移动高亮）；其余键交基类控制器。 */
static void VXComboEdit_keyPressEvent(XWidget* self, XEvent* event)
{
    XComboEdit* edit = (XComboEdit*)self;
    XComboBox* combo;
    XKeyEvent* ke;
    int key;
    if (!edit || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) {
        XClass_Parent(XLineEdit, EXWidget_KeyPressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    combo = edit->m_owner;
    ke = (XKeyEvent*)event;
    key = ke->m_key;
    if (!combo || !combo->m_editable || !combo->m_completerMode) {
        XClass_Parent(XLineEdit, EXWidget_KeyPressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    if (combo->m_completionActive && combo->m_popupVisible) {
        if (key == (int)XKey_Return || key == (int)XKey_Enter) {
            xcombo_completionAccept(combo);
            XEvent_accept(event);
            return;
        }
        if (key == (int)XKey_Escape) {
            /* 对标 QCompleter：Esc 收起补全弹层，编辑文本不变。 */
            XComboBox_hidePopup_base(combo);
            XEvent_accept(event);
            return;
        }
        if (key == (int)XKey_Up || key == (int)XKey_Down) {
            xcombo_completionNavigate(combo, key == (int)XKey_Up ? -1 : 1);
            XEvent_accept(event);
            return;
        }
        if (key == (int)XKey_PageUp || key == (int)XKey_PageDown) {
            xcombo_completionNavigate(
                combo, key == (int)XKey_PageUp ? -combo->m_maxVisibleItems
                                               : combo->m_maxVisibleItems);
            XEvent_accept(event);
            return;
        }
    } else if (key == (int)XKey_Escape && combo->m_popupVisible) {
        /* 全量下拉弹层存活：Esc 收起（不改编辑文本，对标 Qt）。 */
        XComboBox_hidePopup_base(combo);
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XLineEdit, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 内嵌行编辑子类虚表：仅覆写按下键，其余继承 XLineEdit。 */
static XVtable* XComboEdit_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XComboEdit)
    XVTABLE_INHERIT_XCLASS(XLineEdit);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXComboEdit_keyPressEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建内嵌行编辑（堆分配，属主由参数回指）。 */
static XComboEdit* xcomboEdit_create(XComboBox* owner)
{
    XComboEdit* edit = (XComboEdit*)XMemory_malloc(
        sizeof(XComboEdit), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!edit) return NULL;
    XMemset(edit, 0, sizeof(*edit));
    XLineEdit_init(&edit->m_base, (XWidget*)owner, 0);
    XClassSetVtable(edit, XComboEdit);
    edit->m_owner = owner;
    Set_Class_Memory(edit, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(edit, true);
    return edit;
}

void XComboBox_setEditable(XComboBox* self, bool editable)
{
    if (!self || self->m_editable == editable) return;
    self->m_editable = editable;
    if (editable) {
        /* 对标 Qt setEditable(true)：创建内嵌编辑框（子类承载补全键）。 */
        if (!self->m_lineEdit) {
            XComboEdit* edit = xcomboEdit_create(self);
            self->m_lineEdit = (XLineEdit*)edit;
            if (self->m_lineEdit) {
                XWidget_setGeometry((XWidget*)self->m_lineEdit, 2, 2,
                                    XWidget_width((XWidget*)self) -
                                        XCOMBOBOX_BUTTON_W - 4,
                                    XWidget_height((XWidget*)self) - 4);
                XWidget_show((XWidget*)self->m_lineEdit);
                /* 对标 Qt：编辑联动信号统一挂接（编辑文本变化发射
                 * editTextChanged；textEdited 驱动补全；Enter/失焦
                 * 结算插入策略）。 */
                xcombo_connectLineEdit(self, self->m_lineEdit);
            }
        }
    }
    else {
        /* 对标 Qt setEditable(false)：销毁内嵌编辑框；补全弹层存活时
           一并收起（全量弹层不受影响）。 */
        if (self->m_completionActive && self->m_popupVisible)
            XComboBox_hidePopup_base(self);
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
    if (self->m_completionActive && self->m_popupVisible)
        XComboBox_hidePopup_base(self);
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
    /* 对标 Qt setLineEdit：安装的编辑框同样挂接编辑联动信号。 */
    xcombo_connectLineEdit(self, edit);
    XWidget_update((XWidget*)self);
}

void XComboBox_setCompleterMode(XComboBox* self, bool enable)
{
    if (!self || self->m_completerMode == enable) return;
    self->m_completerMode = enable;
    /* 关闭补全：存活的过滤弹层立即收起（不改编辑文本）。 */
    if (!enable && self->m_completionActive && self->m_popupVisible)
        XComboBox_hidePopup_base(self);
}

bool XComboBox_isCompleterMode(const XComboBox* self)
{
    return self ? self->m_completerMode : false;
}

/* ==================== 弹出列表部件化 ==================== */

/*
 * 内置弹出列表子类（XComboPopupView）：XListView 不可直接修改，而
 * XAbstractItemView 的 mousePress 依赖 indexAt 命中才发射 pressed，
 * XListView 的 indexAt 对越界坐标（x 忽略、y/(行高) 向零截断、无上界
 * 校验）会给出伪行号或不发射，无法在组合框侧以信号区分"外部点击"。
 * 故参照 XMenu 的"mousePress 判断 pos 超界"方案，在本地子类覆写
 * 按下/释放：位置超出视图几何即收起弹窗并吞掉事件；窗内事件原样
 * 交给 XListView 处理（行选择、activated 联动不变）。另覆写按键：
 * Esc 消费收层、导航键转发基类后补弹层重绘上屏（基类无 Esc 分支且
 * setCurrentIndex 不触发重绘，弹层为顶层窗无兜底——详见
 * VXComboPopupView_keyPressEvent 注）。
 */
XCLASS_DEFINE_BEGING(XComboPopupView)
XCLASS_DEFINE_EXTEND_END(XComboPopupView, XListView)

/** @brief 组合框内置弹出列表对象；m_base 必须是第一个成员。 */
typedef struct XComboPopupView
{
    XListView  m_base;   /**< 基类成员（嵌 XListView）；必须是第一个。 */
    XComboBox* m_owner;  /**< 属主组合框（借用；可为 NULL）。 */
} XComboPopupView;

static void VXComboPopupView_mousePressEvent(XWidget* self, XEvent* event);
static void VXComboPopupView_mouseReleaseEvent(XWidget* self, XEvent* event);
static void VXComboPopupView_keyPressEvent(XWidget* self, XEvent* event);

/** @brief 判断弹出层本地坐标是否落在视图矩形内。
 * @note  入参为事件相对弹层窗口的本地坐标，直接与弹层尺寸比较；
 *        历史实现误用 XWidget_rect（父链/全局口径 (65,270)）比较，
 *        弹层内点击恒判越界——按下即收起、永不激活选中（14.115）。 */
static bool xcomboPopupView_contains(const XComboPopupView* view, int x, int y)
{
    if (!view) return false;
    return x >= 0 && y >= 0 &&
           x < XWidget_width((const XWidget*)view) &&
           y < XWidget_height((const XWidget*)view);
}

/** @brief 按下：越界（弹窗外部）点击收起弹窗；窗内交给 XListView。 */
static void VXComboPopupView_mousePressEvent(XWidget* self, XEvent* event)
{
    XComboPopupView* view = (XComboPopupView*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!view || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        XClass_Parent(XListView, EXWidget_MousePressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (!xcomboPopupView_contains(view, pos.x, pos.y)) {
        /* 模态抓取期间转发到本窗口的弹窗外按下：一律收起并吞掉，
           防止越界坐标被 indexAt 映射成伪行触发误选。 */
        if (view->m_owner)
            XComboBox_hidePopup_base(view->m_owner);
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XListView, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 弹层本地坐标 y → 组合框条目行号。
 * @note  与 XListView 绘制/命中同口径：隐藏行（补全过滤）占高 0 跳过，
 *        按可见行序以 XCOMBOBOX_ITEM_H 步进换算；无可命中可见行返回
 *        -1。补全过滤后直接 y/行高 会映射到错误的伪行。 */
static int xcomboPopupView_rowAtY(const XComboPopupView* view, int y)
{
    XComboBox* owner;
    int slot;
    int row;
    int visible = 0;
    if (!view || !view->m_owner || y < 0) return -1;
    owner = view->m_owner;
    slot = y / XCOMBOBOX_ITEM_H;
    for (row = 0; row < owner->m_itemCount; ++row) {
        if (XListView_isRowHidden((XListView*)view, row)) continue;
        if (visible == slot) return row;
        ++visible;
    }
    return -1;
}

/** @brief 释放：窗内按行直接激活（选择+收起经 activated 槽联动）；
 *         越界释放吞掉（防止负坐标截断映射为 0 行误激活）。
 * @note  不再转发基类释放：基类 activated 依赖 IndexAt 虚槽，弹层
 *        子类虚表在该槽位解析不稳（xlv_indexAt 不被调用），故此处
 *        本地按可见行序换算行号（xcomboPopupView_rowAtY，过滤弹层
 *        跳过隐藏行）并显式发射 activated(row)，选择/收起仍经
 *        xcombo_viewActivatedSlot。 */
static void VXComboPopupView_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XComboPopupView* view = (XComboPopupView*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!view || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) {
        XClass_Parent(XListView, EXWidget_MouseReleaseEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (!xcomboPopupView_contains(view, pos.x, pos.y)) {
        XEvent_accept(event);
        return;
    }
    if (view->m_owner) {
        int row = xcomboPopupView_rowAtY(view, pos.y);
        if (row >= 0) {
            XVarList* args;
            XAbstractItemView_setCurrentIndex((XAbstractItemView*)view,
                                              row,
                                              view->m_owner->m_modelColumn);
            if (((XObject*)view)->m_signalSlot) {
                args = XVarList_Create(XVar(int, row));
                if (args) {
                    XObject_emitSignal((XObject*)view,
                        (size_t)XAbstractItemView_activated_signal(
                            (XAbstractItemView*)view, row,
                            view->m_owner->m_modelColumn),
                        args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
                }
            }
        }
    }
    XEvent_accept(event);
}

/** @brief 键盘：Esc 收层（复扫-5 N1 残1）；导航/激活键转发基类后补
 *  弹层表面重绘上屏（复扫-5 N2 残2）。
 * @note  弹层存活期间键盘抓取以本视图为派发起点（xcombo_popupShow 的
 *  XWidget_grabKeyboard），且本视图是独立顶层窗（父链终点）：基类
 *  XAbstractItemView_keyPressEvent 无 Escape 分支（Up/Down/Return
 *  之外的事件既不处理也不 ignore，XAbstractItemView.c:1843 起），
 *  事件沿父链无处上抛即被丢弃——Esc 因此不收层（对照 XMenu 于自身
 *  keyPress 消费 Esc：XMenu.c:1154-1157）。
 *  对标 Qt：弹层存活 Esc 恒收层——QAbstractItemView::keyPressEvent
 *  对 Escape 走 event->ignore()（qabstractitemview.cpp:2502），未接受
 *  键由弹层容器/弹窗层兜底关闭（qcombobox.cpp QComboBoxPrivateContainer
 *  eventFilter 的 Cancel 分支同语义）。本子类即弹层容器，故在此消费。
 *  高亮滞留根因：基类键盘导航（无修饰分支 XAbstractItemView.c:1954-
 *  1958）经 XAbstractItemView_setCurrentIndex 移动当前行，而该函数
 *  只改内部索引不同步重绘（XAbstractItemView.c:316-341；对比
 *  xaiv_setCurrentPreservingSelection 有 update）——"当前行已移动、
 *  高亮滞留旧行"。对标 Qt QAbstractItemView::currentChanged 的
 *  update(previous)（qabstractitemview.cpp:3808）：行移动必重绘；
 *  本文件不可改基类，故在转发基类完成移动后补 update + 主动 flush
 *  （弹层为独立顶层窗口无宿主帧泵，异步 PAINT 不足以上屏——本文件
 *  xcombo_popupReposition/xcombo_popupShow 已有主动补帧先例）。 */
static void VXComboPopupView_keyPressEvent(XWidget* self, XEvent* event)
{
    XComboPopupView* view = (XComboPopupView*)self;
    int key;
    if (!view || !event ||
        XEvent_type(event) != XEVENT_TYPE_KEY_PRESS) {
        XClass_Parent(XListView, EXWidget_KeyPressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    key = ((XKeyEvent*)event)->m_key;
    if (key == (int)XKey_Escape) {
        /* Esc 恒收层（hidePopup_base 幂等：非弹出态早退）。 */
        if (view->m_owner)
            XComboBox_hidePopup_base(view->m_owner);
        XEvent_accept(event);
        return;
    }
    /* 导航（Up/Down/PageUp/PageDown/Home/End）与激活（Return/Enter）
     * 仍由基类实现：行移动钳制/选择同步/activated 发射（activated →
     * xcombo_viewActivatedSlot 回填收层）语义不变，此处不做第二实现。 */
    XClass_Parent(XListView, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    /* 基类未接受（空模型/越界钳制为 ignore）不补绘；Return/Enter 已由
       activated 联动收层（弹层已隐藏）亦无需补绘。 */
    if (!XEvent_isAccepted(event)) return;
    if (view->m_owner && view->m_owner->m_popupVisible) {
        switch (key) {
        case (int)XKey_Up:
        case (int)XKey_Down:
        case (int)XKey_PageUp:
        case (int)XKey_PageDown:
        case (int)XKey_Home:
        case (int)XKey_End:
            XWidget_update((XWidget*)view);
            XWidget_flushBackingStore((XWidget*)view, NULL);
            break;
        default:
            break;
        }
    }
}

/** @brief 弹出列表子类虚表：仅覆写按下/释放/按下键，其余继承 XListView。 */
static XVtable* XComboPopupView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XComboPopupView)
    XVTABLE_INHERIT_XCLASS(XListView);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXComboPopupView_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXComboPopupView_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXComboPopupView_keyPressEvent);
    return XVTABLE_DEFAULT;
}

/** @brief 创建内置弹出列表（堆分配，属主由参数回指）。 */
static XComboPopupView* xcomboPopupView_create(XComboBox* owner)
{
    XComboPopupView* view = (XComboPopupView*)XMemory_malloc(
        sizeof(XComboPopupView), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!view) return NULL;
    XMemset(view, 0, sizeof(*view));
    XListView_init(&view->m_base, NULL, 0);
    XClassSetVtable(view, XComboPopupView);
    view->m_owner = owner;
    Set_Class_Memory(view, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(view, true);
    return view;
}

/** @brief 把组合框条目同步进数据模型（行数与文本对齐）。 */
static void xcombo_syncModel(XComboBox* self)
{
    int i;
    if (!self) return;
    if (!self->m_model) {
        self->m_model = XAbstractItemModel_create();
        if (!self->m_model) return;
    }
    XAbstractItemModel_setDimension(self->m_model,
        self->m_itemCount > 0 ? self->m_itemCount : 0, 1);
    for (i = 0; i < self->m_itemCount; ++i) {
        XAbstractItemModel_setData_2(self->m_model, i, self->m_modelColumn,
            self->m_items[i] ? XString_toUtf8(self->m_items[i]) : "");
    }
}

/** @brief 解除弹窗模态鼠标抓取：杀抓取定时器 + 公共层/平台解抓。 */
static void xcombo_releaseGrab(XComboBox* self)
{
    XWindow* handle;
    if (!self) return;
    if (self->m_grabTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_grabTimer);
        self->m_grabTimer = XTIMER_INVALID_ID;
    }
    if (self->m_popupView) {
        XWidget_releaseMouse((XWidget*)self->m_popupView);
        /* 对标 Qt closePopup：鼠标/键盘成对解抓（grabForPopup 的逆操作），
           漏解键盘抓取会使弹层收起后全局按键仍被劫持。 */
        XWidget_releaseKeyboard((XWidget*)self->m_popupView);
        handle = XWidget_windowHandle((XWidget*)self->m_popupView);
        if (handle) {
            XWindow_setMouseGrabEnabled(handle, false);
            XWindow_setKeyboardGrabEnabled(handle, false);
        }
    }
}

/** @brief 弹出视图行激活（单击）联动：选中条目、发射信号并收起弹窗。 */
static void xcombo_viewActivatedSlot(XObject* receiver, XVarList* args)
{
    XComboBox* combo = (XComboBox*)receiver;
    int row = -1;
    int col = -1;
    if (!combo || !args) return;
    row = XVarList_arg(args, int);
    col = XVarList_arg(args, int);
    if (row >= 0 && row < combo->m_itemCount) {
        XComboBox_setCurrentIndex(combo, row);
        /* 可编辑：用户选择后编辑文本回填为选中项（对标 Qt 激活路径
           的 lineEdit 回填；程序化 setCurrentIndex 不回填，保持既有
           行为；回填为程序化 setText，不发射 textEdited，不会回环
           触发补全过滤）。 */
        if (combo->m_editable && combo->m_lineEdit)
            XComboBox_setEditText_2(combo, XComboBox_itemText_2(combo, row));
        xcombo_emitInt(combo,
                       (size_t)XComboBox_activated_signal(combo, row), row);
        xcombo_emitText(combo,
                        (size_t)XComboBox_textActivated_signal(
                            combo, XComboBox_itemText_2(combo, row)),
                        XComboBox_itemText_2(combo, row));
    }
    XComboBox_hidePopup_base(combo);
}

XListView* XComboBox_view(XComboBox* self)
{
    if (!self) return NULL;
    xcombo_syncModel(self);
    if (!self->m_popupView) {
        /* 内置弹窗使用本地子类：覆写按下/释放以支持"点击弹窗外部
           自动收起"（XComboPopupView，仍是 XListView 派生对象）。 */
        self->m_popupView = (XListView*)xcomboPopupView_create(self);
        if (self->m_popupView) {
            XAbstractItemView_setModel(
                (XAbstractItemView*)self->m_popupView, self->m_model);
            XListView_setModelColumn(self->m_popupView, self->m_modelColumn);
            /* 行激活（单击）联动选择并收起弹窗。 */
            XObject_connect_1((XObject*)self->m_popupView,
                (size_t)XAbstractItemView_activated_signal(
                    (XAbstractItemView*)self->m_popupView, 0, 0),
                (XObject*)self, xcombo_viewActivatedSlot,
                XConnectionType_Direct);
        }
    }
    return self->m_popupView;
}

void XComboBox_setView(XComboBox* self, XListView* view)
{
    if (!self || !view || view == self->m_popupView) return;
    /* 弹出中换视图：先按常规路径收起，保证旧视图的抓取被解除。 */
    if (self->m_popupVisible)
        XComboBox_hidePopup_base(self);
    if (self->m_model)
        XAbstractItemView_setModel((XAbstractItemView*)view, self->m_model);
    XListView_setModelColumn(view, self->m_modelColumn);
    if (self->m_popupView)
        XListView_delete_base((XClass*)self->m_popupView);
    self->m_popupView = view;
}

XAbstractItemModel* XComboBox_model(XComboBox* self)
{
    if (self) xcombo_syncModel(self);
    return self ? self->m_model : NULL;
}

void XComboBox_setModel(XComboBox* self, XAbstractItemModel* model)
{
    if (!self || self->m_model == model) return;
    if (self->m_popupView)
        XAbstractItemView_setModel((XAbstractItemView*)self->m_popupView,
                                   model);
    if (self->m_model)
        XAbstractItemModel_delete_base((XClass*)self->m_model);
    self->m_model = model;
}

int XComboBox_modelColumn(const XComboBox* self)
{ return self ? self->m_modelColumn : 0; }

void XComboBox_setModelColumn(XComboBox* self, int column)
{
    if (!self) return;
    self->m_modelColumn = column;
    if (self->m_popupView)
        XListView_setModelColumn(self->m_popupView, column);
}

void XComboBox_setRootModelIndex(XComboBox* self, int row, int col)
{
    if (!self) return;
    self->m_rootRow = row;
    self->m_rootCol = col;
}

void XComboBox_rootModelIndex(const XComboBox* self, int* row, int* col)
{
    if (row) *row = self ? self->m_rootRow : 0;
    if (col) *col = self ? self->m_rootCol : 0;
}

void XComboBox_setValidator(XComboBox* self, void* validator)
{ if (self) self->m_validator = validator; }

void* XComboBox_validator(const XComboBox* self)
{ return self ? self->m_validator : NULL; }

void XComboBox_setItemDelegate(XComboBox* self, void* delegate)
{
    /* 对标 QComboBox::setItemDelegate：委托体系未建，仅保存不透明
       指针（借用语义，不取得所有权）。 */
    if (self) self->m_itemDelegate = delegate;
}

void* XComboBox_itemDelegate(const XComboBox* self)
{ return self ? self->m_itemDelegate : NULL; }

XString* XComboBox_inputMethodQuery(XComboBox* self, int query)
{
    XString* out = XString_create();
    if (!out) return NULL;
    (void)query; /* 简化承载：仅编辑文本类查询；其余返回空文本。 */
    if (self && self->m_editable && self->m_lineEdit)
        XString_assign_utf8(out, XLineEdit_text(self->m_lineEdit));
    else {
        XString* cur = XComboBox_currentText(self);
        if (cur) {
            XString_assign(out, cur);
            XString_delete_base((XClass*)cur);
        }
    }
    return out;
}

int XComboBox_currentIndex(const XComboBox* self)
{
    return self ? self->m_currentIndex : -1;
}
/** @brief 可编辑模式下取行编辑显示值作为 currentText 来源（分叉态）。
 * @note  对标 Qt 6.8 文档 currentText 语义：editable 时 "current text
 *  is the value displayed by the line edit"。本封装只在编辑框文本与
 *  当前项文本分叉（用户正在编辑/程序化 setEditText 且未命中项）时
 *  返回编辑框文本；两者同步（采纳回填/程序化置当前项后一致）时返回
 *  NULL 走当前项文本路径，保证 _2 版本借用指针口径稳定。不可编辑或
 *  无编辑框恒返回 NULL。 */
static const char* xcombo_editTextSource(const XComboBox* self)
{
    const char* edit;
    if (!self || !self->m_editable || !self->m_lineEdit) return NULL;
    edit = XLineEdit_text(self->m_lineEdit);
    if (!edit) return NULL;
    if (self->m_currentIndex >= 0 &&
        self->m_currentIndex < self->m_itemCount &&
        self->m_items[self->m_currentIndex]) {
        const char* item = XString_toUtf8(self->m_items[self->m_currentIndex]);
        if (item && XStrcmp(item, edit) == 0)
            return NULL; /* 同步态：编辑框显示的就是当前项文本。 */
    }
    return edit;
}

XString* XComboBox_currentText(const XComboBox* self)
{
    const char* edit = xcombo_editTextSource(self);
    if (edit) return XString_create_utf8(edit);
    if (self && self->m_currentIndex >= 0 &&
        self->m_currentIndex < self->m_itemCount && self->m_items[self->m_currentIndex])
        return XString_create_copy(self->m_items[self->m_currentIndex]);
    return XString_create();
}
const char* XComboBox_currentText_2(const XComboBox* self)
{
    const char* edit = xcombo_editTextSource(self);
    if (edit) return edit;
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
    /* 可编辑时程序化置当前项回填内嵌编辑框（对标 Qt
     * QComboBoxPrivate::updateLineEdit）：currentText 分叉态口径
     * 「程序化置当前项后一致」（见 xcombo_editTextSource 注），回填后
     * 编辑框与当前项文本同步、分叉态收敛，currentTextChanged 载荷才
     * 能取到当前项文本而非残留编辑文本。须在发射前回填。 */
    if (self->m_editable && self->m_lineEdit) {
        const char* itemText =
            (index >= 0 && index < self->m_itemCount &&
             self->m_items[index])
                ? XString_toUtf8(self->m_items[index]) : "";
        const char* edit = XLineEdit_text(self->m_lineEdit);
        if (!edit || XStrcmp(edit, itemText ? itemText : "") != 0)
            XLineEdit_setText(self->m_lineEdit,
                              itemText ? itemText : "");
    }
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
    XString_deinit_base((XClass*)tmp);
}

void XComboBox_insertItems(XComboBox* self, int index, const XStringList* texts)
{
    int i, n;
    if (!self || !texts) return;
    n = (int)XVector_size_base((const XContainer*)texts);
    for (i = 0; i < n; ++i) {
        XString* s = *(XString**)XStringList_at_base((const XVector*)texts, i);
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
            XString_delete_base((XClass*)self->m_itemData[index]);
        XMemmove(&self->m_itemData[index], &self->m_itemData[index + 1],
                 sizeof(XString*) * (size_t)(self->m_itemCount - index - 1));
    }
    if (self->m_itemIcons) {
        if (self->m_itemIcons[index])
            XString_delete_base((XClass*)self->m_itemIcons[index]);
        XMemmove(&self->m_itemIcons[index], &self->m_itemIcons[index + 1],
                 sizeof(XString*) * (size_t)(self->m_itemCount - index - 1));
    }
    --self->m_itemCount;
    XString_delete_base((XClass*)removed);
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
        if (self->m_items[i]) XString_delete_base((XClass*)self->m_items[i]);
        if (self->m_itemData && self->m_itemData[i]) {
            XString_delete_base((XClass*)self->m_itemData[i]);
            self->m_itemData[i] = NULL;
        }
        if (self->m_itemIcons && self->m_itemIcons[i]) {
            XString_delete_base((XClass*)self->m_itemIcons[i]);
            self->m_itemIcons[i] = NULL;
        }
    }
    self->m_itemCount = 0;
    self->m_currentIndex = -1;
    XWidget_update((XWidget*)self);
}

/* ==================== 弹出与选择 ==================== */

/* ==================== 补全弹层（复用下拉弹层承载） ==================== */

/** @brief 清除弹层行过滤（全部置可见；仅操作已创建的弹层视图，不在
 *  收起/显示路径上懒创建视图）。 */
static void xcombo_clearRowHidden(XComboBox* self)
{
    int i;
    if (!self || !self->m_popupView) return;
    for (i = 0; i < self->m_itemCount; ++i)
        XListView_setRowHidden(self->m_popupView, i, false);
}

/** @brief 依据行数重设弹层几何（组合框正下方，宽同组合框）。 */
static void xcombo_popupReposition(XComboBox* self, int rows)
{
    XListView* view;
    XPoint origin;
    XPoint g;
    XRect r;
    if (!self || !self->m_popupView) return;
    view = self->m_popupView;
    XWidget_setWindowFlags((XWidget*)view, (XWidgetFlags)XWindowType_Popup);
    origin.x = 0;
    origin.y = XWidget_height((XWidget*)self);
    g = XWidget_mapToGlobal((XWidget*)self, &origin);
    XListView_setRowHeight(view, XCOMBOBOX_ITEM_H);
    XRect_init(&r, g.x, g.y,
               XWidget_width((XWidget*)self),
               rows * XCOMBOBOX_ITEM_H + 2);
    XWidget_setGeometryRect((XWidget*)view, &r);
    /* 独立顶层窗口无宿主帧泵：补全过滤态改尺寸后主动补一帧
       （show 路径由 xcombo_popupShow 负责首帧）。 */
    if (self->m_popupVisible)
        XWidget_flushBackingStore((XWidget*)view, NULL);
}

/** @brief 弹出弹层（几何 + popupShown/show/raise/首帧/模态抓取全流程；
 *  全量下拉与补全弹层共用）。rows < 1 时占 1 行（保持空列表既有可见
 *  弹层行为）。 */
static void xcombo_popupShow(XComboBox* self, int rows)
{
    XListView* view;
    if (!self || !self->m_popupView) return;
    view = self->m_popupView;
    if (rows < 1) rows = 1;
    xcombo_popupReposition(self, rows);
    self->m_popupVisible = true;
    xcombo_emitInt(self, (size_t)XComboBox_popupShown_signal(self), 0);
    XWidget_show((XWidget*)view);
    /* 对标 Qt QWidgetPrivate::show_helper 的 Popup 分支（qwidget.cpp:
       8038-8043「new popups and tools need to be raised」）：弹层每次
       show 都必须置顶。XWidget_raise→XWindow_raise 当前为平台无关
       no-op（XWindow.c:2019 无平台 Z 序接口），而 X11 规范 MapWindow
       并不改堆叠序（x11protocol.txt MapWindow 节），弹层 X11 窗口若
       早于主窗口建立即永居其下——表现为「弹层已映射、缓冲有内容、
       屏幕不可见」。平台唯一置顶通道是 requestActivate→XRaiseWindow
       （XPlatformNativeWindow_posix.c:5144）；未映射窗口的激活在平台
       层挂起（m_deferredActivation，MapNotify 后补做），无 BadMatch。 */
    XWidget_activateWindow((XWidget*)view);
    XWidget_raise((XWidget*)view);
    /* 独立顶层窗口无宿主帧泵：主动完成首帧绘制上屏（参照 XMenu）。 */
    XWidget_flushBackingStore((XWidget*)view, NULL);
    /* 模态鼠标抓取（参照 XMenu）：公共层立即设置直投目标，使点击弹窗
       外部的事件也路由到弹窗（由 XComboPopupView 判定越界并收起）；
       平台 XGrabPointer 需要窗口完成映射，延迟到 1ms 精确定时器执行。
       键盘抓取同步建立（对标 Qt openPopup→grabForPopup 鼠标+键盘成对
       抓取，qapplication.cpp:3327-3339）——Esc/方向键直达弹层。 */
    XWidget_grabMouse((XWidget*)view);
    XWidget_grabKeyboard((XWidget*)view);
    if (self->m_grabTimer == XTIMER_INVALID_ID) {
        self->m_grabTimer = XObject_startTimer_ms(
            (XObject*)self, 1u, XTimerType_PreciseTimer);
    }
    XWidget_update((XWidget*)self);
}

/** @brief 补全过滤刷新（对标 QCompleter setCompletionPrefix + popup）：
 *  按前缀（大小写不敏感）过滤弹层行并高亮；空前缀/无匹配不弹层或
 *  收起弹层；命中时复用下拉弹层（行隐藏承载过滤，行号仍为条目索引）。
 * @note  由编辑框 textEdited（仅用户编辑）驱动；程序化 setEditText
 *  不触发（对标 Qt 补全不响应程序化文本设置）。 */
static void xcombo_completionRefresh(XComboBox* self, const char* prefix)
{
    XListView* view;
    int matches = 0;
    int first = -1;
    int i;
    if (!self || !self->m_editable || !self->m_completerMode ||
        !self->m_lineEdit)
        return;
    if (!prefix) prefix = "";
    view = XComboBox_view(self); /* 懒创建并同步模型（复用下拉弹层）。 */
    if (!view) return;
    if (!prefix[0]) {
        if (self->m_popupVisible) XComboBox_hidePopup_base(self);
        return;
    }
    for (i = 0; i < self->m_itemCount; ++i) {
        bool hit = self->m_items[i] &&
                   xcombo_prefixMatchUtf8(XString_toUtf8(self->m_items[i]),
                                          prefix);
        XListView_setRowHidden(view, i, !hit);
        if (hit) {
            ++matches;
            if (first < 0) first = i;
        }
    }
    if (matches == 0) {
        /* 对标 QCompleter PopupCompletion：无匹配不弹层（弹层存活则
           收起，编辑文本不变）。 */
        if (self->m_popupVisible) XComboBox_hidePopup_base(self);
        return;
    }
    /* 高亮：上次补全行仍命中则粘滞，否则首个命中（对标 completer
       currentRow 跟随）。 */
    if (self->m_completionRow < 0 ||
        self->m_completionRow >= self->m_itemCount ||
        XListView_isRowHidden(view, self->m_completionRow))
        self->m_completionRow = first;
    XAbstractItemView_setCurrentIndex((XAbstractItemView*)view,
                                      self->m_completionRow,
                                      self->m_modelColumn);
    self->m_completionActive = true;
    if (!self->m_popupVisible)
        xcombo_popupShow(self, matches);
    else
        xcombo_popupReposition(self, matches); /* 命中数变化改弹层高。 */
}

/** @brief Enter 采纳补全（对标 QCompleter activated → 编辑器回填 +
 *  QComboBoxPrivate::completerActivated → 置当前项 + emitActivated）。
 *  程序化回填不发射 textEdited，不会回环触发补全过滤。 */
static void xcombo_completionAccept(XComboBox* self)
{
    int row;
    if (!self || !self->m_editable || !self->m_lineEdit) return;
    row = self->m_completionRow;
    if (row < 0 || row >= self->m_itemCount) return;
    XComboBox_setEditText_2(self, XComboBox_itemText_2(self, row));
    XComboBox_setCurrentIndex(self, row);
    xcombo_emitActivatedPair(self, row);
    XComboBox_hidePopup_base(self);
}

/** @brief Up/Down/PageUp/PageDown 在命中行间移动高亮（对标 completer
 *  popup 键导航；边界钳制不环绕，步长为可见行数）。 */
static void xcombo_completionNavigate(XComboBox* self, int step)
{
    XListView* view = self ? self->m_popupView : NULL;
    int row;
    int remaining;
    int dir;
    if (!view || !self->m_completionActive) return;
    row = self->m_completionRow;
    if (row < 0 || row >= self->m_itemCount) return;
    dir = step > 0 ? 1 : -1;
    remaining = step > 0 ? step : -step;
    while (remaining-- > 0) {
        int next = row + dir;
        while (next >= 0 && next < self->m_itemCount &&
               XListView_isRowHidden(view, next))
            next += dir;
        if (next < 0 || next >= self->m_itemCount) break;
        row = next;
    }
    if (row == self->m_completionRow) return;
    self->m_completionRow = row;
    XAbstractItemView_setCurrentIndex((XAbstractItemView*)view, row,
                                      self->m_modelColumn);
    XWidget_update((XWidget*)view);
    /* 补主动上屏：弹层为独立顶层窗口无宿主帧泵，异步 PAINT 不保证
       上屏（复扫-5 N2 残2 同家族；先例 xcombo_popupReposition）。 */
    XWidget_flushBackingStore((XWidget*)view, NULL);
}

void XComboBox_showPopup_base(XComboBox* self)
{
    XListView* view;
    int rows;
    if (!self || self->m_popupVisible) return;
    view = XComboBox_view(self);
    if (!view) return;
    xcombo_syncModel(self);
    /* 全量弹出前清残留过滤（补全收起时已清，此处兜底）：弹层为全量态。 */
    xcombo_clearRowHidden(self);
    self->m_completionActive = false;
    self->m_completionRow = -1;
    rows = self->m_itemCount < self->m_maxVisibleItems
        ? (self->m_itemCount > 0 ? self->m_itemCount : 1)
        : self->m_maxVisibleItems;
    xcombo_popupShow(self, rows);
}

void XComboBox_hidePopup_base(XComboBox* self)
{
    if (!self || !self->m_popupVisible) return;
    self->m_popupVisible = false;
    /* 退出补全过滤态并清除行隐藏：下次全量弹出不残留隐藏行。 */
    self->m_completionActive = false;
    self->m_completionRow = -1;
    xcombo_clearRowHidden(self);
    /* 解除模态抓取：杀延迟抓取定时器 + 公共层/平台解抓（选择收起
       经 xcombo_viewActivatedSlot 亦走本路径）。 */
    xcombo_releaseGrab(self);
    if (self->m_popupView)
        XWidget_hide((XWidget*)self->m_popupView);
    /* 焦点回交（对标 Qt closePopup 的焦点还原，qapplication.cpp:
       3368-3377「active_window->focusWidget() 重新聚焦」）：弹出时
       activateWindow 曾把应用焦点窗口指向弹层窗口；收起后若焦点仍
       滞留弹层，按键会投递给已隐藏窗口，组合框所在顶层窗口重新
       激活以恢复按键链。 */
    {
        XWidget* host = XWidget_topLevelWidget((XWidget*)self);
        if (host && host->m_isWindow &&
            XGuiApplication_focusWindow() ==
                (XWindow*)XWidget_windowHandle((XWidget*)self->m_popupView))
            XWidget_activateWindow(host);
    }
    xcombo_emitInt(self, (size_t)XComboBox_popupHidden_signal(self), 0);
    XWidget_update((XWidget*)self);
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
        XString_delete_base((XClass*)self->m_itemIcons[index]);
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
    if (tmp) XString_delete_base((XClass*)tmp);
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
        XString_delete_base((XClass*)self->m_itemData[index]);
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
    if (tmp) XString_delete_base((XClass*)tmp);
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
    XString_delete_base((XClass*)tmp);
    return out;
}

void XComboBox_setCompleter(XComboBox* self, XCompleter* completer)
{
    XCompleter* old;
    if (!self || self->m_completer == completer) return;
    old = self->m_completer;
    self->m_completer = completer;
#if XTABLEWIDGET_ON
    /* 对标 QComboBox::setCompleter 的安装接通语义（降级承载）：
       - 借用安装，不取得所有权（Qt 同）；
       - 补全器无模型时接通组合框条目模型（Qt：!completer->model() 时
         completer->setModel(d->model)）；
       - 记录关联控件，安装非空补全器即驱动内置补全过滤路径
         （m_completerMode 置位，语义对标"安装补全器后行编辑获得
         popup 补全"；XGui 无 QCompleter 弹出栈，内置 completerMode
         前缀过滤弹层为既定降级承载，取舍见头文件注记）；
       - setCompleter(NULL) 仅解除指针，不动 completerMode 开关（保留
         显式 setCompleterMode 的用户意图）；
       - 替换/解除时原补全器仍回指本组合框则清其关联位（对标
         XLineEdit_setCompleter 的借用卫生，防悬挂）。 */
    if (completer) {
        if (!XCompleter_model(completer))
            XCompleter_setModel(completer, XComboBox_model(self));
        XCompleter_setWidget(completer, (XWidget*)self);
        self->m_completerMode = true;
    }
    if (old && XCompleter_widget(old) == (XWidget*)self)
        XCompleter_setWidget(old, NULL);
#endif /* XTABLEWIDGET_ON */
}
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
