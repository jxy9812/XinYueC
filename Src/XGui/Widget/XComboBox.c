/**
 * @file       XComboBox.c
 * @brief      XComboBox 下拉组合框控件实现（对标 Qt 6.8 QComboBox）。
 * @details    内部结构：文本项存于 XString* 对象数组（每项为独立拥有
 *             的 XString 对象；项数据/图标为平行的 XString* 数组）。
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
#if XGUI_ON
#include "XStringUtils.h"

#include "XAlgorithm.h"
#if XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON

#include "XComboBox.h"
#include "XCompleter.h"
#include "XCommonStyle.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XString.h"
#include "XWidget_Protected.h"
#include "XListView.h"
#include "XAbstractItemModel.h"
#include "XAbstractItemView.h"
#include "XIcon.h"     /* 项图标解码（XTabBar 页签图标同款先例，P3-56）。 */
#include "XPixmap.h"
#include "XImage.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XWindow.h"
#include "XCoreApplication.h"
#include "XGuiApplication.h"
#include "XScreen.h" /* availableGeometry（弹层屏幕适配）。 */
#if XGUI_ON && XPLATFORM_FBDEV_ON
#include "XPlatformDisplayDriver.h" /* 屏缺席时面板框回退（XWindow.c xwin_screenRect 同源口径；XWindowDecoration.c 同层先例）。 */
#endif
#include "XColor.h"

/* 弹出列表几何常量 */
#define XCOMBOBOX_BUTTON_W  16
#define XCOMBOBOX_ITEM_H    20

/* ==================== 前向声明 ==================== */
static void  VXComboBox_paintEvent(XWidget* self, XEvent* event);
static void  VXComboBox_showEvent(XWidget* self, XEvent* event);
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
static void  xcombo_changeCurrentIndex(XComboBox* self, int index);
static bool  xcombo_hasExternalModel(const XComboBox* self);
static int   xcombo_countInternal(const XComboBox* self);
static void  xcombo_mirrorModel(XComboBox* self);
static void  xcombo_detachModelSignals(XComboBox* self);
#if XTABLEWIDGET_ON
static void  xcombo_completerActivatedSlot(XObject* receiver, XVarList* args);
#endif /* XTABLEWIDGET_ON */

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

/**
 * @brief 按**颜色组**取调色板颜色。
 * @details [Qt 对齐] 禁用控件走 QPalette::Disabled 组（灰显）——Qt 的
 *          QComboBox 禁用态用 Disabled 组的文字色，"不可点"一眼可见；
 *          本组合框此前恒用 Current 组，禁用态与可用态**像素无差**，
 *          用户无从分辨控件已锁（现场反馈「点不动也不像锁」）。 */
static uint32_t xcombo_colorGroup(const XComboBox* self,
                                  XPaletteColorGroup group,
                                  XPaletteColorRole role)
{
#if XPALETTE_ON
    XPalette palette = XWidget_palette((XWidget*)self);
    XColor c = XPalette_color(&palette, group, role);
    return XColor_rgba(&c);
#else
    (void)self;
    (void)group;
    (void)role;
    return 0xFF000000u;
#endif /* XPALETTE_ON */
}

/* ==================== 项图标解码缓存（P3-56） ====================
 * 对标 Qt 的 DecorationRole 图标：闭态 CE_ComboBoxLabel 与弹层装饰按
 * iconSize 预缩放绘制。解码走页签条先例（XTabBar.c xtabbar_drawTabIcon：
 * XIcon_init_file_2 → XIcon_pixmap → XPixmap_toImage），缓存 XImage 归
 * 组合框自持（装饰接口借用指针，弹层 paint 期读取，须活到下一次失效）。 */

/** @brief 取第 index 项的解码图标（懒解码缓存；无图标/解码失败 NULL）。
 * @note  m_itemImages 与条目平行（insertItem/removeItem/clear 同步搬运）；
 *        setItemIcon/setIconSize 失效对应缓存，下次取用重解码。 */
static XImage* xcombo_itemImageAt(XComboBox* self, int index)
{
    const char* path;
    XIcon icon;
    XPixmap pixmap;
    XImage image;
    XImage* out;
    if (!self || index < 0 || index >= self->m_itemCount ||
        !self->m_itemIcons || !self->m_itemIcons[index])
        return NULL;
    path = XString_toUtf8(self->m_itemIcons[index]);
    if (!path || !path[0]) return NULL;
    if (self->m_itemImages && self->m_itemImages[index])
        return self->m_itemImages[index];
    /* QSize 口径：按宽、高分别解码（Qt QIcon::pixmap(size) 同口径）。 */
    XIcon_init_file_2(&icon, path);
    if (XIcon_isNull(&icon)) {
        XClassDeinit((XClass*)&icon);
        return NULL;
    }
    XPixmap_init(&pixmap);
    XIcon_pixmap(&icon, self->m_iconSize, self->m_iconSize,
                 XIconMode_Normal, XIconState_Off, &pixmap);
    XImage_init(&image);
    XPixmap_toImage(&pixmap, &image);
    XClassDeinit((XClass*)&pixmap);
    XClassDeinit((XClass*)&icon);
    if (XImage_width(&image) <= 0 || XImage_height(&image) <= 0) {
        XClassDeinit((XClass*)&image);
        return NULL;
    }
    /* 堆上落缓存（XImage_create 归组合框，deinit 统一释放）：
     * 解码图经 EXClass_Move 移入堆对象（VXImage_move 只转 m_data，不动
     * 目标壳的类元数据），源壳随后按栈对象规则收尾——此前 create 后直接
     * 丢弃解码图，缓存恒为空图（弹层装饰宽 0，QtAlign :787 假失败）。 */
    out = XImage_create();
    if (!out) {
        XClassDeinit((XClass*)&image);
        return NULL;
    }
    XClassMove((XClass*)out, (XClass*)&image);
    XClassDeinit((XClass*)&image);
    if (!self->m_itemImages) {
        int i;
        int cap = self->m_itemCapacity > 0 ? self->m_itemCapacity : 1;
        self->m_itemImages =
            (XImage**)XCalloc_System((size_t)cap, sizeof(XImage*));
        if (!self->m_itemImages) {
            XClassDelete((XClass*)out);
            return NULL;
        }
        for (i = 0; i < cap; ++i) self->m_itemImages[i] = NULL;
    }
    self->m_itemImages[index] = out;
    return out;
}

/** @brief 弹层行高：iconSize+4 与基线行高取大（P3-56）。
 * @note  缺省 16px 时恒等 XCOMBOBOX_ITEM_H（视觉零差）；大图标下拉行随
 *        涨以容下装饰（XListView 装饰绘制对超行高图像跳过）。命中换算
 *        （xcomboPopupView_rowAtY）、分隔条定位、弹层几何三处同源取值。 */
static int xcombo_popupItemHeight(const XComboBox* self)
{
    int h = self ? self->m_iconSize + 4 : 0;
    if (h < XCOMBOBOX_ITEM_H) h = XCOMBOBOX_ITEM_H;
    return h;
}

/** @brief 显示事件（对标 QComboBox::showEvent，qcombobox.cpp:3090-3099）：
 *  AdjustToContentsOnFirstShow 档在首次显示时失效一次尺寸缓存并触发布局
 *  重协商，随后置 shownOnce（此后内容变化不再自动变宽，冻结语义）。 */
static void VXComboBox_showEvent(XWidget* self, XEvent* event)
{
    XComboBox* combo = (XComboBox*)self;
    if (combo && !combo->m_shownOnce &&
        combo->m_sizeAdjustPolicy ==
            (uint8_t)XComboBoxSizeAdjustPolicy_AdjustToContentsOnFirstShow) {
        combo->m_sizeHintValid = false;
        XWidget_updateGeometry(self);
    }
    combo->m_shownOnce = true;
    XClass_Parent(XWidget, EXWidget_ShowEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
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
    XPaletteColorGroup group;
    if (!combo || r.width <= 4 || r.height <= 4) return;
    r.x = 0; r.y = 0;
    /* [Qt 对齐] 禁用控件走 Disabled 颜色组（灰显）；可用态走当前组。 */
    group = XWidget_isEnabled(self) ? XPaletteColorGroup_Current
                                    : XPaletteColorGroup_Disabled;
    base     = xcombo_colorGroup(combo, group, XPaletteColorRole_Base);
    dark     = xcombo_colorGroup(combo, group, XPaletteColorRole_Dark);
    light    = xcombo_colorGroup(combo, group, XPaletteColorRole_Light);
    text     = xcombo_colorGroup(combo, group, XPaletteColorRole_Text);
    highlight = xcombo_colorGroup(combo, group, XPaletteColorRole_Highlight);
    highlightedText = xcombo_colorGroup(combo, group,
                                        XPaletteColorRole_HighlightedText);
    button = xcombo_colorGroup(combo, group, XPaletteColorRole_Button);

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
        /* Fusion/公共风格接管：面板 + 下拉按钮区走 CC_ComboBox。 */
        XStyle* style = XWidget_style((const XWidget*)self);
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCC_ComboBox);
        /* [P3-55 Qt 对齐] frame 下发（qcombobox.cpp:1240 `option->frame =
         * d->frame`；填值先例 XAbstractSpinBox.c:158 的 m_spinFrame）——
         * 此前恒由 init 缺省 true 顶替，setFrame(false) 视觉零变化。
         * 样式消费端（xcs_drawComboBox false 时跳边框/边距归零）由
         * B2 批在 XCommonStyle.c 落地，本批只接通承载。 */
        opt.m_spinFrame = combo->m_frame;
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
    /* [P3-55 Qt 对齐] frame=false：无样式回退路径不再画四边边框
     * （对齐样式路径 QStyle::drawControl(CE_ComboBox) 的 frame 语义；
     * 下拉按钮区照画——Qt 无边框组合框仍保留下拉钮）。 */
    if (combo->m_frame) {
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
    /* 当前项文本（部件化后列表由独立弹出视图承载，自身恒画文本）。
     * [可编辑不双绘 2026-10-27 框架根修] 对标 Qt QCommonStyle
     * CE_ComboBoxLabel（qcommonstyle.cpp:2250）：闭态文本**仅非可编辑
     * 时**绘制——`if (!cb->currentText.isEmpty() && !cb->editable)`。
     * 可编辑态的文本由**内嵌编辑框**承载（QComboBox 的 lineEdit 子控件
     * 占据编辑区并自行绘制），闭态若再画一遍即**同一位置双绘**：两串字
     * 形互相叠印、互不可读（XFontComboBox 构造即 setEditable(true)，
     * qfontcombobox.cpp:403 —— 真机/桌面截图实证：字体下拉叠印
     * 「Noto Sans」与「Noto Sans: Gurmukhi」）。本实现此前**无条件**画
     * 闭态文本 ⇒ 与内嵌编辑框双绘，即该 P1 叠印的框架根因。 */
    if (!combo->m_editable) {
        /* [P3-56 Qt 对齐] 闭态当前项图标（Qt CE_ComboBoxLabel：图标占
         * iconSize.width()+4、文本随之后移，qcommonstyle.cpp:2227-2245）；
         * 解码图像按 iconSize 预缩放并由组合框自持（xcombo_itemImageAt），
         * 无图标保持原版式（textX=6）。 */
        int textX = 6;
        if (combo->m_currentIndex >= 0 &&
            combo->m_currentIndex < combo->m_itemCount) {
            XImage* icon = xcombo_itemImageAt(combo, combo->m_currentIndex);
            if (icon) {
                XPainter_drawImage(&painter, icon, textX,
                                   (r.height - XImage_height(icon)) / 2);
                textX += XImage_width(icon) + 4;
            }
        }
        if (combo->m_currentIndex >= 0 &&
            combo->m_currentIndex < combo->m_itemCount &&
            combo->m_items[combo->m_currentIndex]) {
            /* [Qt 对齐 2026-11-02] 闭态文本按**可用宽**省略（Qt
               QComboBox 走 QCommonStyle CE_ComboBoxLabel → elidedText）：
               此前直绘全文，控件被布局收窄后文字溢出画到相邻控件上
               （窄窗下无线下拉的长 SSID 压住「扫描」钮）。 */
            {
                const char* cur =
                    XString_toUtf8(combo->m_items[combo->m_currentIndex]);
                const XFont* cfont = &((XWidget*)combo)->m_font;
                int avail = r.width - textX - XCOMBOBOX_BUTTON_W - 8;
                if (avail > 0 && cur) {
                    char elided[256];
                    /* mode 1 = Qt::ElideRight（编号已统一）。 */
                    const char* shown =
                        XCommonStyle_elideText(elided, (int)sizeof(elided),
                                               cur, 1, avail, cfont);
                    XPainter_drawText(&painter, textX,
                                      (r.height - 14) / 2 + 12,
                                      shown, text);
                } else {
                    XPainter_drawText(&painter, textX,
                                      (r.height - 14) / 2 + 12,
                                      cur, text);
                }
            }
        } else if (combo->m_placeholderText &&
                   XString_toUtf8(combo->m_placeholderText) &&
                   XString_toUtf8(combo->m_placeholderText)[0]) {
            XPainter_drawText(&painter, textX, (r.height - 14) / 2 + 12,
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
    for (i = 0; i < other->m_itemCount; ++i) {
        XComboBox_addItem(self, other->m_items[i]);
        /* 分隔条标记随条目复制（m_itemSeparators 为平行数组；Qt 拷贝
           分隔条同样是空文本 + AccessibleDescriptionRole 数据）。 */
        if (other->m_itemSeparators && other->m_itemSeparators[i] &&
            self->m_itemSeparators && i < self->m_itemCount)
            self->m_itemSeparators[i] = true;
    }
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
    /* [P0-04] 尺寸缓存/首显位不继承：拷贝件按未显示、未计算落位
     * （弹层/显示态同为瞬态，同口径）。 */
    self->m_sizeHintValid = false;
    self->m_shownOnce = false;
    if (self->m_placeholderText && other->m_placeholderText)
        XString_assign(self->m_placeholderText, other->m_placeholderText);
}

/** @brief 移动语义：基类移动后转移项数组，源对象归默认值。 */
static void VXComboBox_move(XComboBox* self, XComboBox* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XComboBox_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    /* 移动前必须释放目标原有的拥有条目与数组缓冲：clear 只析构条目
     * 对象，三个数组缓冲若不释放即被覆盖丢失（参照 VXLayout_move）；
     * 数据/图标平行数组须与条目数组一同转移，避免容量失配越界。 */
    XComboBox_clear(self);
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
    if (self->m_itemImages) {
        XFree_System(self->m_itemImages);
        self->m_itemImages = NULL;
    }
    if (self->m_itemSeparators) {
        XFree_System(self->m_itemSeparators);
        self->m_itemSeparators = NULL;
    }
    self->m_itemCapacity = 0;
    self->m_items = other->m_items;
    self->m_itemData = other->m_itemData;
    self->m_itemIcons = other->m_itemIcons;
    self->m_itemImages = other->m_itemImages;
    self->m_itemSeparators = other->m_itemSeparators;
    self->m_itemCount = other->m_itemCount;
    self->m_itemCapacity = other->m_itemCapacity;
    self->m_currentIndex = other->m_currentIndex;
    other->m_items = NULL;
    other->m_itemData = NULL;
    other->m_itemIcons = NULL;
    other->m_itemImages = NULL;
    other->m_itemSeparators = NULL;
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
    /* [P0-04] 尺寸缓存/首显位不随迁：弹层/显示态为瞬态，同口径按
     * 未显示、未计算落位。 */
    self->m_sizeHintValid = false;
    self->m_shownOnce = false;
    if (self->m_placeholderText)
        XClassDelete((XClass*)self->m_placeholderText);
    self->m_placeholderText = other->m_placeholderText;
    other->m_placeholderText = XString_create();
    other->m_maxCount = 2147483647;
    other->m_maxVisibleItems = 10;
    other->m_duplicatesEnabled = false;
    other->m_editable = false;
    other->m_completerMode = false;
    other->m_completionActive = false;
    other->m_completionRow = -1;
    other->m_insertPolicy = (uint8_t)XComboBoxInsertPolicy_InsertAtBottom;
    other->m_sizeAdjustPolicy =
        (uint8_t)XComboBoxSizeAdjustPolicy_AdjustToContentsOnFirstShow;
    other->m_frame = true;
    other->m_sizeHintValid = false;
    other->m_shownOnce = false;
    /* m_placeholderText 已转移并重建。 */
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
    /* 断开外部模型的信号连接（模型可能比组合框活得久；连接表挂在发送者
     * 一侧，接收者析构不会自动摘除 —— 必须在析构期显式 disconnect）。 */
    xcombo_detachModelSignals(self);
    /* 行编辑框为组合框所有（setEditable/setLineEdit 契约「组合框销毁时
       释放」；父链不代删子控件——XTextEdit m_editor 同款显式删除）。 */
    if (self->m_lineEdit) {
        XClassDelete((XClass*)self->m_lineEdit);
        self->m_lineEdit = NULL;
    }
    if (self->m_placeholderText) {
        XClassDelete((XClass*)self->m_placeholderText);
        self->m_placeholderText = NULL;
    }
    if (self->m_popupView) {
        XClassDelete((XClass*)self->m_popupView);
        self->m_popupView = NULL;
    }
    if (self->m_model) {
        /* 外部 setModel 安装的模型为借用（Qt 同），只释放内部懒创建的。 */
        if (self->m_modelOwned)
            XClassDelete((XClass*)self->m_model);
        self->m_model = NULL;
        self->m_modelOwned = false;
    }
    for (i = 0; i < self->m_itemCount; ++i) {
        if (self->m_items[i]) XClassDelete((XClass*)self->m_items[i]);
        if (self->m_itemData && self->m_itemData[i])
            XClassDelete((XClass*)self->m_itemData[i]);
        if (self->m_itemIcons && self->m_itemIcons[i])
            XClassDelete((XClass*)self->m_itemIcons[i]);
        if (self->m_itemImages && self->m_itemImages[i])
            XClassDelete((XClass*)self->m_itemImages[i]);
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
    if (self->m_itemImages) {
        XFree_System(self->m_itemImages);
        self->m_itemImages = NULL;
    }
    if (self->m_itemSeparators) {
        XFree_System(self->m_itemSeparators);
        self->m_itemSeparators = NULL;
    }
    self->m_itemCount = 0;
    self->m_itemCapacity = 0;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/** @brief 键盘：Up/Down/Home/End 改当前项（对标 QComboBox 键盘导航；
 *  可编辑模式下焦点在内嵌编辑框，本路径仅服务非编辑焦点）。 */
static void xcombo_emitActivatedPair(XComboBox* self, int index);

/** @brief 从 index 起沿 step 方向找可停留条目（跳过分隔条）。
 * @details 对标 Qt：分隔条的模型 flags 无 ItemIsEnabled/ItemIsSelectable，
 *          键盘/滚轮导航不会停在分隔条上（QComboBox 键盘路径经视图的
 *          可选中判定）。越界返回 -1。 */
static int xcombo_nextSelectable(const XComboBox* self, int index, int step)
{
    if (!self || step == 0) return -1;
    while (index >= 0 && index < self->m_itemCount) {
        if (!self->m_itemSeparators || !self->m_itemSeparators[index])
            return index;
        index += step;
    }
    return -1;
}

/* [滚轮换项 2026-10-07] 非编辑态且弹层未开时，按滚轮角度从当前项向上/
   下找下一个可用项（跳过禁用项由条目模型无禁用位而退化为相邻项直达），
   setCurrentIndex 后走既有 xcombo_emitActivatedPair；事件 accept。 */
static void VXComboBox_wheelEvent(XWidget* self, XEvent* event)
{
    XComboBox* cb = (XComboBox*)self;
    XWheelEvent* we;
    XPoint delta;
    int dy;
    int steps;
    int index;
    int count;
    if (!cb || !event || XEvent_type(event) != XEVENT_TYPE_WHEEL) return;
    if (cb->m_popupVisible || cb->m_editable) {
        XClass_Parent(XWidget, EXWidget_WheelEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    count = XComboBox_count(cb);
    if (count <= 0) {
        XEvent_accept(event);
        return;
    }
    we = (XWheelEvent*)event;
    delta = XWheelEvent_angleDelta(we);
    dy = (delta.y != 0) ? delta.y : delta.x;
    steps = (dy > 0) ? -1 : ((dy < 0) ? 1 : 0); /* 上滚=前一项（同 Qt）。 */
    if (steps == 0) {
        XEvent_accept(event);
        return;
    }
    index = XComboBox_currentIndex(cb);
    if (index < 0) index = 0;
    else {
        index += steps;
        if (index < 0) index = 0;
        if (index > count - 1) index = count - 1;
    }
    index = xcombo_nextSelectable(cb, index, steps);
    if (index < 0) {
        XEvent_accept(event);
        return;
    }
    if (index != XComboBox_currentIndex(cb)) {
        XComboBox_setCurrentIndex(cb, index);
        xcombo_emitActivatedPair(cb, index);
    }
    XEvent_accept(event);
}

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
    if (key == (int)XKey_Up) {
        int next = xcombo_nextSelectable(combo, idx < 0 ? 0 : idx - 1, -1);
        if (next >= 0) XComboBox_setCurrentIndex(combo, next);
    } else if (key == (int)XKey_Down) {
        int next = xcombo_nextSelectable(combo,
                                         idx < 0 ? 0 : idx + 1, 1);
        if (next >= 0) XComboBox_setCurrentIndex(combo, next);
    } else if (key == (int)XKey_Home && count > 0) {
        int next = xcombo_nextSelectable(combo, 0, 1);
        if (next >= 0) XComboBox_setCurrentIndex(combo, next);
    } else if (key == (int)XKey_End && count > 0) {
        int next = xcombo_nextSelectable(combo, count - 1, -1);
        if (next >= 0) XComboBox_setCurrentIndex(combo, next);
    } else {
        XEvent_ignore(event);
        return;
    }
    XEvent_accept(event);
}

/* update 函数定义在后段，此处先声明供槽桥接调用。 */
static void xcombo_updateSizeHint(XComboBox* self);

/* SizeHint 虚槽桥接（对标 Qt 的 sizeHint 虚函数）：先按当前内容
   重算缓存，再读回，等价「查询时重算」语义。 */
static XSize VXComboBox_sizeHintSlot(const XWidget* self);
static XSize VXComboBox_minimumSizeHintSlot(const XWidget* self);

static XSize VXComboBox_sizeHintSlot(const XWidget* self)
{
    if (!self) { XSize out; XSize_init(&out, -1, -1); return out; }
    xcombo_updateSizeHint((XComboBox*)self);
    return XClass_Parent(XWidget, EXWidget_SizeHint,
                         XSize (*)(const XWidget*))(self);
}

static XSize VXComboBox_minimumSizeHintSlot(const XWidget* self)
{
    if (!self) { XSize out; XSize_init(&out, -1, -1); return out; }
    xcombo_updateSizeHint((XComboBox*)self);
    return XClass_Parent(XWidget, EXWidget_MinimumSizeHint,
                         XSize (*)(const XWidget*))(self);
}

XVtable* XComboBox_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XComboBox)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VXComboBox_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MinimumSizeHint, VXComboBox_minimumSizeHintSlot);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXComboBox_paintEvent);
    /* [P0-04] AdjustToContentsOnFirstShow 首显失效一次尺寸缓存。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXComboBox_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent, VXComboBox_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent, VXComboBox_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent, VXComboBox_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ChangeEvent, VXComboBox_changeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXComboBox_keyPressEvent);
    /* [滚轮换项 2026-10-07] 对标 QComboBox::wheelEvent（弹层未开且非
     * 编辑态时按 angleDelta 移动当前项并发射 activated；SH_ComboBox_
     * AllowWheelScrolling 在 QCommonStyle 缺省 true）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_WheelEvent, VXComboBox_wheelEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXComboBox_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXComboBox_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXComboBox_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXComboBox_deinit);
    /* [Qt 对齐 2026-11-11] 弹层宽度虚槽缺省实现（宽同控件；
     * XFontComboBox 重载为 min(宽×5/3, 屏宽)）。 */
    XVTABLE_OVERLOAD_DEFAULT(EXComboBox_PopupWidth,
                             XComboBox_popupWidth_base);

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
    self->m_itemSeparators = NULL;
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
    self->m_insertPolicy = (uint8_t)XComboBoxInsertPolicy_InsertAtBottom;
    /* [P0-04 Qt 对齐] 缺省 AdjustToContentsOnFirstShow（Qt
     * qcombobox_p.h:396）——尺寸提示缓存化后首显冻结；此前误取
     * AdjustToContents（内容驱动）导致运行期重填不收口。 */
    self->m_sizeAdjustPolicy =
        (uint8_t)XComboBoxSizeAdjustPolicy_AdjustToContentsOnFirstShow;
    self->m_minimumContentsLength = 0;
    self->m_frame = true;
    self->m_placeholderText = XString_create();
    self->m_popupVisible = false;
    self->m_popupView = NULL;
    self->m_model = NULL;
    self->m_modelOwned = false;
    self->m_modelColumn = 0;
    self->m_rootRow = 0;
    self->m_rootCol = 0;
    self->m_validator = NULL;
    self->m_itemImages = NULL;
    self->m_grabTimer = XTIMER_INVALID_ID;
    self->m_savedHeight = 0;
    /* [P0-04] 尺寸缓存态：未计算/未显示（XMemset 归零后的显式表达）。 */
    self->m_sizeHintValid = false;
    self->m_shownOnce = false;
    self->m_hintFontProbe = 0;
    /* 悬停外观 opt-in：绘制按 State_MouseOver 出悬停高亮，ENTER/
     * LEAVE 须标脏自矩形（对标 Qt polish 的 WA_Hover 收口）。 */
    XWidget_setAttribute((XWidget*)self, XWidgetAttribute_Hover, true);
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

int XComboBox_count(const XComboBox* self) { return xcombo_countInternal(self); }
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
void XComboBox_setFrame(XComboBox* self, bool on)
{
    /* 对标 QComboBox::setFrame：同值早退，变化后请求重绘（qcombobox.cpp
     * setFrame 的 `if (d->frame == frame) return; ... update();`）。 */
    if (!self || self->m_frame == on) return;
    self->m_frame = on;
    XWidget_update((XWidget*)self);
}
bool XComboBox_hasFrame(const XComboBox* self) { return self ? self->m_frame : true; }
int XComboBox_insertPolicy(const XComboBox* self)
{
    return self ? self->m_insertPolicy : XComboBoxInsertPolicy_InsertAtBottom;
}
void XComboBox_setInsertPolicy(XComboBox* self, int policy)
{
    if (self && policy >= 0 && policy <= 6) self->m_insertPolicy = (uint8_t)policy;
}
int XComboBox_sizeAdjustPolicy(const XComboBox* self)
{
    return self ? self->m_sizeAdjustPolicy
                : XComboBoxSizeAdjustPolicy_AdjustToContents;
}
static void xcombo_updateSizeHint(XComboBox* self);

void XComboBox_setSizeAdjustPolicy(XComboBox* self, int policy)
{
    if (self && policy >= 0 && policy <= 2) {
        if (policy == (int)self->m_sizeAdjustPolicy) return; /* Qt 同值早退。 */
        self->m_sizeAdjustPolicy = (uint8_t)policy;
        /* [P0-04 Qt 对齐] 策略切换即失效重算（qcombobox.cpp:1617-1625：
         * d->sizeHint = QSize(); adjustComboBoxSize(); updateGeometry()）。 */
        self->m_sizeHintValid = false;
        xcombo_updateSizeHint(self); /* [sizeHint] 策略参与几何后随刷新。 */
    }
}
int XComboBox_minimumContentsLength(const XComboBox* self)
{
    return self ? self->m_minimumContentsLength : 0;
}
void XComboBox_setMinimumContentsLength(XComboBox* self, int characters)
{
    if (!self || characters < 0 ||
        characters == self->m_minimumContentsLength)
        return;
    self->m_minimumContentsLength = characters;
    /* [P0-04 Qt 对齐] 仅内容驱动两档随 mcl 重算（qcombobox.cpp:1650-1661
     * 的 `AdjustToContents || AdjustToMinimumContentsLengthWithIcon` 闸）；
     * OnFirstShow 档维持冻结（mcl 只参与下一次重算）。 */
    if (self->m_sizeAdjustPolicy ==
            (uint8_t)XComboBoxSizeAdjustPolicy_AdjustToContents ||
        self->m_sizeAdjustPolicy ==
            (uint8_t)XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon) {
        self->m_sizeHintValid = false;
        xcombo_updateSizeHint(self); /* [sizeHint]。 */
    }
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
    /* [P0-89] 占位宽计入首选宽 ⇒ 变更即重算（此前只 update 重绘）。 */
    self->m_sizeHintValid = false;
    xcombo_updateSizeHint(self);
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
    /* [P0-89] 占位宽计入首选宽 ⇒ 变更即重算。 */
    self->m_sizeHintValid = false;
    xcombo_updateSizeHint(self);
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

/* ==================== 内容尺寸提示（[sizeHint 2026-10-07]） ====================
   对标 QComboBox::sizeHint（qcombobox.cpp:2518）：逐项字体测宽取最大值
   （AdjustToMinimumContentsLengthWithIcon 档按 minimumContentsLength 字
   符宽与最长项取大），拼按钮/边距后经风格 sizeFromContents(CT_ComboBox)
   收口；m_sizeAdjustPolicy/m_minimumContentsLength 自此参与几何（此前为
   纯存储）。AddItem/RemoveItem/SetItemText/策略变更处经
   xcombo_updateSizeHint 刷新存储位。 */

/** @brief 尺寸缓存字体指纹探针（非空多字形；度量 O(1)，见
 *  m_hintFontProbe 注记）。 */
#define XCOMBO_HINT_PROBE "mxX9_组合框"

/** @brief 计算内容尺寸提示并刷新 XWidget_sizeHint 存储位。
 *  [P0-04 Qt 对齐 2026-11-11] 缓存语义对标 Qt recomputeSizeHint
 *  （qcombobox.cpp:347-390）的 `!sh.isValid()` 分支：缓存有效直读；
 *  失效时机由内容变化路径（仅 AdjustToContents 档）、策略/图标/占位
 *  变更（无条件）与首显（AdjustToContentsOnFirstShow 档一次）驱动，
 *  见各 setter 与 VXComboBox_showEvent。 */
static void xcombo_updateSizeHint(XComboBox* self)
{
    XSize hint;
    XStyleOption opt;
    const XFont* font;
    const char* placeholder;
    int maxW = 0;
    int contentH = XCOMBOBOX_ITEM_H;
    int i;
    int charsW;
    int probeW;
    bool hasIcon = false;
    if (!self) return;
    font = &((XWidget*)self)->m_font;
    probeW = XPainter_textWidth(font, XCOMBO_HINT_PROBE);
    /* [P0-04] 缓存有效直读。字体指纹漂移补失效——X 的 setFont 不发
     * ChangeEvent（XWidget_setFont 直改值 + updateGeometry），Qt 靠
     * FontChange 失效（qcombobox.cpp:3043），以探针宽比对补上该路。 */
    if (self->m_sizeHintValid && probeW == self->m_hintFontProbe) return;
    self->m_hintFontProbe = probeW;
    for (i = 0; i < self->m_itemCount; ++i) {
        const char* text = self->m_items[i] ? XString_toUtf8(self->m_items[i])
                                            : NULL;
        const char* iconPath =
            (self->m_itemIcons && self->m_itemIcons[i])
                ? XString_toUtf8(self->m_itemIcons[i]) : NULL;
        int w = text ? XPainter_textWidth(font, text) : 0;
        /* [P3-56 Qt 对齐] 带图标条目：宽计 iconSize+4 且 hasIcon 置位
         * （qcombobox.cpp:357-364）。 */
        if (iconPath && iconPath[0]) {
            hasIcon = true;
            w += self->m_iconSize + 4;
        }
        if (w > maxW) maxW = w;
    }
    /* [P0-89 Qt 对齐] 空列表基线宽 = 7×'x'（qcombobox.cpp:361-364；
     * 此前空列表 maxW=0 ⇒ 首选宽只剩按钮+边距）。 */
    if (self->m_itemCount == 0) {
        char probe[2] = { 'x', '\0' };
        maxW = 7 * XPainter_textWidth(font, probe);
    }
    charsW = 0;
    if (self->m_sizeAdjustPolicy ==
            XComboBoxSizeAdjustPolicy_AdjustToMinimumContentsLengthWithIcon &&
        self->m_minimumContentsLength > 0) {
        char probe[2] = { 'M', '\0' };
        int mw = XPainter_textWidth(font, probe);
        charsW = mw * self->m_minimumContentsLength;
        /* [Qt 对齐 2026-11-02] 该策略下**只**按最小内容长度定尺（Qt
           qcombobox.cpp recomputeSizeHint：AdjustToMinimumContents-
           LengthWithIcon 取 minimumContentsLength，不看项文本）。此前取
           「项文本最大宽与最小内容长的较大者」→ 长文本项（网卡名/SSID）
           把 sizeHint/minimumSizeHint 撑大 → 所在行最小宽超视口 → 窄窗
           下整行被裁（网络页窄窗横向截断根因之一）。 */
        maxW = charsW;
    }
    /* [P0-89 Qt 对齐] placeholder 计入首选宽（qcombobox.cpp:380-382；
     * 闭态确实会画占位文本（paintEvent 尾段），长占位此前被省略号截断）。 */
    placeholder = self->m_placeholderText
        ? XString_toUtf8(self->m_placeholderText) : NULL;
    if (placeholder && placeholder[0]) {
        int pw = XPainter_textWidth(font, placeholder);
        if (pw > maxW) maxW = pw;
    }
    /* [P3-56 Qt 对齐] 带图标：最小高 iconSize+2（qcombobox.cpp:387-390；
     * 缺省 16px 时 18 < 行高 20，视觉无差；大图标下拉行高随涨）。 */
    if (hasIcon && self->m_iconSize + 2 > contentH)
        contentH = self->m_iconSize + 2;
    XStyleOption_init(&opt, XStyleCT_ComboBox);
    /* [P3-55] frame 参与内容尺寸：XCommonStyle 的 CT_ComboBox 分支读
     * m_spinFrame 定 frameWidth（Qt initStyleOption 同步 option->frame，
     * qcombobox.cpp:1240）。 */
    opt.m_spinFrame = self->m_frame;
    opt.m_rect.width = maxW + XCOMBOBOX_BUTTON_W + 8; /* 文本+按钮+余量。 */
    opt.m_rect.height = contentH;
    {
        XSize content;
        XSize_init(&content, maxW, contentH);
        hint = XStyle_sizeFromContents(XWidget_style((const XWidget*)self),
                                       XStyleCT_ComboBox, &opt, content);
    }
    XWidget_setSizeHint((XWidget*)self, &hint);
    /* [minimumSizeHint 补齐 2026-10-27 视觉走查] Qt QComboBox::
       minimumSizeHint() = recomputeSizeHint —— sizeHint 与 minimumSizeHint
       同源（qcombobox.cpp:2505-2509）。此前本类不设 minimumSizeHint ⇒
       XWidgetItem_minimumSize 的 minHint 无效回退 (0,0) ⇒ 布局 min=0：
       盒布局竖排（按 hint 高给尺寸）在空间不足时「低于首选」均分收缩
       分支可把高度压到 min=0 附近的任意小值（ATV61 窄档翻竖排后下拉框
       塌成 6px 高，桌面截图实证）。补上同值 minimumSizeHint 后，布局
       收缩有真实下界（qGeomCalc 的 size < minimumSize 早退分支得以
       生效）。 */
    XWidget_setMinimumSizeHint((XWidget*)self, &hint);
    /* [变更后必须让布局重协商 2026-11-10 对标 Qt] Qt 的 addItem/insertItem/
     * setSizeAdjustPolicy 等都走 recomputeSizeHint 并触发 updateGeometry
     * （qcombobox.cpp）。此前本函数只写 sizeHint 存储位、**不通知布局**：
     * 运行期 addItem 加进更长的项（网卡名/SSID/落盘策略名）后下拉仍是旧宽
     * ⇒ 长项被省略号截断，要等别的控件触发重排才恢复。 */
    XWidget_updateGeometry((XWidget*)self);
    /* [P0-04] 重算完成，缓存转有效（下一次查询直读）。 */
    self->m_sizeHintValid = true;
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
    /* [P0-54] 真 completer 在装：行编辑控制器已驱动其补全弹层（候选/
       采纳/键导航），内建前缀过滤停用，避免补全弹层与过滤下拉双层弹层
       （优先级取舍见 XComboBox_setCompleter 注记）；completer 解除后
       内建路径自动恢复（completerMode 不因解除复位）。 */
    if (XLineEdit_completer(self->m_lineEdit)) return;
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
    XObject_connect((XObject*)edit,
                      (size_t)XLineEdit_textChanged_signal(edit),
                      (XObject*)self, xcombo_editTextChangedFwd,
                      XConnectionType_Direct);
    XObject_connect((XObject*)edit,
                      (size_t)XLineEdit_textEdited_signal(edit),
                      (XObject*)self, xcombo_editTextEditedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)edit,
                      (size_t)XLineEdit_returnPressed_signal(edit),
                      (XObject*)self, xcombo_editReturnPressedSlot,
                      XConnectionType_Direct);
    XObject_connect((XObject*)edit,
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
            /* 回填文本必须在 m_lineEdit 挂上之前取：currentText 在可编辑态
             * 优先取行编辑内容（xcombo_editTextSource），新编辑框为空会把
             * 回填值变成空串。 */
            const char* prefill = edit ? XComboBox_currentText_2(self) : NULL;
            self->m_lineEdit = (XLineEdit*)edit;
            if (self->m_lineEdit) {
                /* 对标 Qt setEditable(true)：新建的行编辑经 setLineEdit 安装，
                 * 安装路径以 currentText 回填（qcombobox.cpp:1783-1800 →
                 * 1836）；回填在挂接信号之前，不产生 editTextChanged。 */
                XLineEdit_setText(self->m_lineEdit, prefill ? prefill : "");
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
            /* 删除可能处于对象自身事件/信号发射帧（编辑框补全回调内
               触发 setEditable(false)），同步删 UAF，延迟到事件循环
               归还后回收（对标 Qt deleteLater 语义）。 */
            XObject_deleteLater((XObject*)self->m_lineEdit);
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
    XString* prefill;
    if (!self || !edit || edit == self->m_lineEdit) return;
    if (!self->m_editable) self->m_editable = true;
    if (self->m_completionActive && self->m_popupVisible)
        XComboBox_hidePopup_base(self);
    /* 对标 QComboBox::setLineEdit（qcombobox.cpp:1836 `edit->setText(
     * currentText());`）：回填值取自**安装前**的 currentText（新编辑框挂上
     * 后 currentText 会改读新编辑框的空文本 ⇒ 必须在换框前取，且用堆副本
     * 免旧框 deleteLater 后悬垂）。Qt 在挂接信号**之前**回填，故不产生
     * editTextChanged（本实现同样先回填后连接）。 */
    prefill = XComboBox_currentText(self);
    if (self->m_lineEdit) {
        /* 旧编辑框是本控件输入事件的转发目标，可能在自身按键/输入法
           回调链中被换（对标 Qt QComboBox::setLineEdit 的延迟删除）。 */
        XObject_deleteLater((XObject*)self->m_lineEdit);
        self->m_lineEdit = NULL;
    }
    self->m_lineEdit = edit;
    XLineEdit_setText(edit, prefill ? XString_toUtf8(prefill) : "");
    if (prefill) XClassDelete((XClass*)prefill);
    XWidget_setParent_2((XWidget*)edit, (XWidget*)self);
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
static void VXComboPopupView_mouseMoveEvent(XWidget* self, XEvent* event);
static int xcomboPopupView_rowAtY(const XComboPopupView* view, int y);
static void xcombo_emitHighlightedPair(XComboBox* self, int row);
static void VXComboPopupView_mouseReleaseEvent(XWidget* self, XEvent* event);
static void VXComboPopupView_keyPressEvent(XWidget* self, XEvent* event);
static void VXComboPopupView_paintEvent(XWidget* self, XEvent* event);

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

/** @brief 移动：悬停行命中即置视图当前行并发射 highlighted 对（对标
 *  QComboBoxPrivateContainer::eventFilter 的 MouseMove 分支——视图内
 *  indexAt 命中即 view->setCurrentIndex，经 currentChanged →
 *  emitHighlighted 成对发射；qcombobox.cpp eventFilter MouseMove）。 */
static void VXComboPopupView_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XComboPopupView* view = (XComboPopupView*)self;
    XMouseEvent* me;
    XPoint pos;
    int row;
    if (!view || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) {
        XClass_Parent(XListView, EXWidget_MouseMoveEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    row = xcomboPopupView_rowAtY(view, pos.y);
    if (row >= 0 && view->m_owner && view->m_owner->m_itemSeparators &&
        view->m_owner->m_itemSeparators[row])
        row = -1; /* 分隔条不参与悬停高亮（Qt：非 Enabled/Selectable 行）。 */
    if (row >= 0 && view->m_owner) {
        int cur = XAbstractItemView_currentRow((XAbstractItemView*)view);
        if (row != cur) {
            XAbstractItemView_setCurrentIndex((XAbstractItemView*)view,
                                              row,
                                              view->m_owner->m_modelColumn);
            xcombo_emitHighlightedPair(view->m_owner, row);
        }
    }
    XClass_Parent(XListView, EXWidget_MouseMoveEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 弹层本地坐标 y → 组合框条目行号。
 * @note  与 XListView 绘制/命中同口径：隐藏行（补全过滤）占高 0 跳过，
 *        按可见行序以 xcombo_popupItemHeight 步进换算（P3-56 大图标行
 *        随涨，与 setRowHeight 同源）；无可命中可见行返回 -1。补全过滤
 *        后直接 y/行高 会映射到错误的伪行。 */
static int xcomboPopupView_rowAtY(const XComboPopupView* view, int y)
{
    XComboBox* owner;
    int slot;
    int row;
    int visible = 0;
    if (!view || !view->m_owner || y < 0) return -1;
    owner = view->m_owner;
    slot = y / xcombo_popupItemHeight(owner);
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
        if (row >= 0 && view->m_owner->m_itemSeparators &&
            view->m_owner->m_itemSeparators[row])
            row = -1; /* 分隔条不可激活（Qt flags 去掉 Enabled/Selectable）。 */
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

/** @brief 发射 highlighted(int) + textHighlighted(const char*) 信号对
 *  （对标 QComboBoxPrivate::emitHighlighted，qcombobox.cpp:1411-1419；
 *  越界行守卫对标 index.isValid() 早退）。 */
static void xcombo_emitHighlightedPair(XComboBox* self, int row)
{
    const char* text;
    if (!self || row < 0 || row >= self->m_itemCount) return;
    /* 分隔条不参与高亮（Qt：非 Enabled/Selectable 行不可高亮）。 */
    if (self->m_itemSeparators && self->m_itemSeparators[row]) return;
    text = XComboBox_itemText_2(self, row);
    xcombo_emitInt(self,
                   (size_t)XComboBox_highlighted_signal(self, row), row);
    xcombo_emitText(self,
                    (size_t)XComboBox_textHighlighted_signal(self, text),
                    text);
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
    {
        /* 对标 Qt：弹层视图 currentChanged → emitHighlighted（1076）。
           导航行变化时成对发射 highlighted/textHighlighted。 */
        int rowBefore = XAbstractItemView_currentRow(
            (XAbstractItemView*)view);
        XClass_Parent(XListView, EXWidget_KeyPressEvent,
                      void (*)(XWidget*, XEvent*))(self, event);
        if (view->m_owner) {
            int rowAfter = XAbstractItemView_currentRow(
                (XAbstractItemView*)view);
            if (rowAfter != rowBefore)
                xcombo_emitHighlightedPair(view->m_owner, rowAfter);
        }
    }
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

/** @brief 绘制：XListView 行内容之上补 1px 弹层面板边框。
 * @details 对标 Qt QComboBox 弹层容器带框（qcombobox.cpp v6.8.3）：
 *  QComboBoxPrivateContainer 以 QFrame(parent, Qt::Popup) 承载弹层，
 *  updateStyleSettings() 经 SH_ComboBox_PopupFrameStyle setFrameStyle、
 *  构造路径另有 setLineWidth(1) 兜底，内层视图反而显式 NoFrame——
 *  「框在容器不在视图」。本封装弹层无独立容器件（XComboPopupView 一体
 *  承载视图与弹层窗），故框画在弹层视图自身四缘，几何零变化。
 *  颜色经 palette(Dark) 取框架色（默认 Fusion #9F9F9F，XPalette.c
 *  background.darker(150)；演示主题中性描边 #5A6572 属 Test 层 QSS 规则、
 *  库层不可达，随主题对 QSS 可达处的演进另行对齐）。
 *  猎捕 defect 溯源（r1#11/r2#6 两轮旧结论更正）：弹层本为不透明白底
 *  （XListView paint 的 Base 整幅填充）且 X11 合成正常（弹层开合 root
 *  像素差实测 >0、「透出」残字均在弹层矩形之外）——白底贴同白列表
 *  零对比读作「透明叠印」，本框即其根修。行为红线不动：越界收起/
 *  键盘拦截/候选选择三语义全在按下/释放/按键覆写内，本函数只叠缘框。 */
static void VXComboPopupView_paintEvent(XWidget* self, XEvent* event)
{
    XComboPopupView* view = (XComboPopupView*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    uint32_t frame;
    if (!view) return;
    /* 父调先行：行内容/高亮/滚区全由 XListView 原实现承载。 */
    XClass_Parent(XListView, EXWidget_PaintEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
    image = XWidget_paintImage(self);
    if (!image) return;
    XRect_init(&r, 0, 0, XWidget_width(self), XWidget_height(self));
    XPainter_init(&painter, NULL);
    if (!XPainter_begin_image(&painter, image)) {
        XPainter_deinit(&painter);
        return;
    }
    offset = XWidget_paintOffset(self);
    if (offset.x != 0 || offset.y != 0)
        XPainter_translate(&painter, (float)offset.x, (float)offset.y);
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Dark);
        frame = XColor_rgba(&c);
    }
#else
    frame = 0xFF9F9F9Fu;
#endif
    {
        XRect e = r;
        e.height = 1;
        XPainter_fillRect(&painter, &e, frame);
        e = r; e.y = r.y + r.height - 1; e.height = 1;
        XPainter_fillRect(&painter, &e, frame);
        e = r; e.width = 1;
        XPainter_fillRect(&painter, &e, frame);
        e = r; e.x = r.x + r.width - 1; e.width = 1;
        XPainter_fillRect(&painter, &e, frame);
    }
    /* [Qt 对齐 2026-11-11] 分隔条行绘制横线。Qt 6.8 的分隔条是空文本 +
     * AccessibleDescriptionRole="separator" 的模型行，由条目委托绘制成
     * 分隔线（本机探针：文本为空、flags 去掉 Enabled/Selectable）。本库
     * 无委托体系，在弹层自绘：按可见行序换算 y（隐藏行占 0 高，与
     * xcomboPopupView_rowAtY 同口径），行中部画 1px 线。 */
    if (view->m_owner) {
        int row;
        int visible = 0;
        int lineY;
        int rowH = xcombo_popupItemHeight(view->m_owner);
        uint32_t sepColor;
#if XPALETTE_ON
        {
            XPalette palette = XWidget_palette(self);
            XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                      XPaletteColorRole_Mid);
            sepColor = XColor_rgba(&c);
        }
#else
        sepColor = 0xFF9F9F9Fu;
#endif
        for (row = 0; row < view->m_owner->m_itemCount; ++row) {
            if (XListView_isRowHidden(view, row)) continue;
            if (view->m_owner->m_itemSeparators &&
                view->m_owner->m_itemSeparators[row]) {
                XRect line;
                lineY = visible * rowH + rowH / 2;
                XRect_init(&line, r.x + 6, r.y + lineY,
                           r.width - 12, 1);
                if (line.width > 0)
                    XPainter_fillRect(&painter, &line, sepColor);
            }
            ++visible;
        }
    }
    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 弹出列表子类虚表：覆写按下/释放/按下键/绘制，其余继承
 *  XListView（绘制仅叠弹层面板缘框，行语义零介入）。 */
static XVtable* XComboPopupView_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XComboPopupView)
    XVTABLE_INHERIT_XCLASS(XListView);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXComboPopupView_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXComboPopupView_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXComboPopupView_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent,
                             VXComboPopupView_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VXComboPopupView_paintEvent);
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

/** @brief 把条目图标重放到弹层视图 DecorationRole（P3-56；弹层已建时）。
 * @note  逐行**整体重放**（含 NULL 清除）：装饰表按 (row,col) 存借用
 *        指针，条目增删/图标变更后旧行指针必须整体刷新，否则悬垂；
 *        调用点＝syncModel（视图 getter/弹层显隐前均经此）与视图懒建后。 */
static void xcombo_syncDecorations(XComboBox* self)
{
    int i;
    if (!self || !self->m_popupView) return;
    for (i = 0; i < self->m_itemCount; ++i) {
        XAbstractItemView_setItemDecoration(
            (XAbstractItemView*)self->m_popupView, i, self->m_modelColumn,
            xcombo_itemImageAt(self, i));
    }
}

/** @brief 把组合框条目同步进数据模型（行数与文本对齐）。
 * @note  只刷新**内部懒创建**的模型：外部 setModel 安装的模型为借用，
 *        其内容归安装方（Qt 6.8 里模型才是数据源，本实现的条目数组仍是
 *        数据源，二者各自独立；此处不再把条目写进外部模型——
 *        2026-11-11 修正：此前 view()/model() 会覆盖外部模型的行与文本）。 */
static void xcombo_syncModel(XComboBox* self)
{
    int i;
    if (!self) return;
    if (!self->m_model) {
        self->m_model = XAbstractItemModel_create();
        if (!self->m_model) return;
        self->m_modelOwned = true;
    }
    /* [P3-56] 图标装饰重放：与模型内容同步同拍（外部模型路径同样执行——
     * 图标存本地平行数组，不随模型归属变化）。 */
    xcombo_syncDecorations(self);
    if (!self->m_modelOwned) return; /* 外部模型：不动其内容。 */
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
            XObject_connect((XObject*)self->m_popupView,
                (size_t)XAbstractItemView_activated_signal(
                    (XAbstractItemView*)self->m_popupView, 0, 0),
                (XObject*)self, xcombo_viewActivatedSlot,
                XConnectionType_Direct);
            /* [P3-56] 新视图接上模型后补放图标装饰（syncModel 先于视图
               懒建，当时无处可放）。 */
            xcombo_syncDecorations(self);
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
    if (self->m_popupView) {
        /* 旧弹层视图是 activated 等信号的 Direct 发送方与弹层输入/绘制
           事件主体：若在视图自身信号链（activated→用户槽→setView）中
           被换，同步删会在其 mouseRelease→activated 发射帧返回途中释放
           结构体造成 UAF；延迟到事件循环归还后回收（对标 Qt setView）。 */
        XObject_deleteLater((XObject*)self->m_popupView);
    }
    self->m_popupView = view;
}

XAbstractItemModel* XComboBox_model(XComboBox* self)
{
    if (self) xcombo_syncModel(self);
    return self ? self->m_model : NULL;
}

/* ==================== 外部模型镜像（Qt：模型是唯一数据源） ====================
 * [Qt 对齐 2026-11-11] Qt 6.8 的 QComboBox 没有自己的条目表：count()/
 * itemText() 全部取自 d->model（本机探针：setModel 后 count 随模型=2、
 * currentText 取模型第 0 行；删除外部模型后 count=0、currentIndex=-1）。
 * 本实现的条目数组改为**模型的镜像**：模型信号驱动重建，条目写入
 * （addItem/insertItem/setItemText/removeItem/clear）在外部模型安装期间
 * 写回模型。图标/项数据模型无角色承载，留在组合框平行数组（Qt 走
 * DecorationRole/UserRole；本库 XAbstractItemModel 无 role 参数）。 */

/** @brief 当前安装的模型是否为外部（借用）模型。 */
static bool xcombo_hasExternalModel(const XComboBox* self)
{
    return self && self->m_model && !self->m_modelOwned;
}

/** @brief count() 的统一取数（外部模型取模型行数，等价 Qt model->rowCount）。 */
static int xcombo_countInternal(const XComboBox* self)
{
    if (!self) return 0;
    if (xcombo_hasExternalModel(self))
        return XAbstractItemModel_rowCount(self->m_model);
    return self->m_itemCount;
}

/** @brief 确保四个平行数组容量 >= need（新槽置空）。
 * @note  与既有 insertItem 的增长路径同型；这里把 data/icons/separators
 *        一并分配，镜像重建时无需再判空。 */
static bool xcombo_reserveItems(XComboBox* self, int need)
{
    int cap;
    XString** arr;
    XImage** imgs;
    bool* flags;
    int i;
    if (!self) return false;
    if (need <= self->m_itemCapacity) return true;
    cap = self->m_itemCapacity > 0 ? self->m_itemCapacity : 8;
    while (cap < need) cap *= 2;
    arr = (XString**)XRealloc_System(self->m_items,
                                     sizeof(XString*) * (size_t)cap);
    if (!arr) return false;
    self->m_items = arr;
    arr = (XString**)XRealloc_System(self->m_itemData,
                                     sizeof(XString*) * (size_t)cap);
    if (!arr) return false;
    self->m_itemData = arr;
    arr = (XString**)XRealloc_System(self->m_itemIcons,
                                     sizeof(XString*) * (size_t)cap);
    if (!arr) return false;
    self->m_itemIcons = arr;
    /* [P3-56] 解码缓存随容量同扩（懒解码数组，新槽置空）。 */
    imgs = (XImage**)XRealloc_System(self->m_itemImages,
                                     sizeof(XImage*) * (size_t)cap);
    if (!imgs) return false;
    self->m_itemImages = imgs;
    flags = (bool*)XRealloc_System(self->m_itemSeparators,
                                   sizeof(bool) * (size_t)cap);
    if (!flags) return false;
    self->m_itemSeparators = flags;
    for (i = self->m_itemCapacity; i < cap; ++i) {
        self->m_itemData[i] = NULL;
        self->m_itemIcons[i] = NULL;
        self->m_itemImages[i] = NULL;
        self->m_itemSeparators[i] = false;
    }
    self->m_itemCapacity = cap;
    return true;
}

/** @brief 按外部模型行/列重建条目文本镜像（图标/数据保留原下标内容）。 */
static void xcombo_mirrorModel(XComboBox* self)
{
    int rows;
    int i;
    if (!xcombo_hasExternalModel(self)) return;
    rows = XAbstractItemModel_rowCount(self->m_model);
    if (rows < 0) rows = 0;
    if (!xcombo_reserveItems(self, rows > 0 ? rows : 1)) return;
    for (i = 0; i < self->m_itemCount; ++i) {
        if (self->m_items[i]) {
            XClassDelete((XClass*)self->m_items[i]);
            self->m_items[i] = NULL;
        }
    }
    /* 缩容时释放超出行数的本地图标/数据（平行数组随条目对齐）。 */
    for (i = rows; i < self->m_itemCount; ++i) {
        if (self->m_itemData && self->m_itemData[i]) {
            XClassDelete((XClass*)self->m_itemData[i]);
            self->m_itemData[i] = NULL;
        }
        if (self->m_itemIcons && self->m_itemIcons[i]) {
            XClassDelete((XClass*)self->m_itemIcons[i]);
            self->m_itemIcons[i] = NULL;
        }
    }
    for (i = 0; i < rows; ++i) {
        const char* text = XAbstractItemModel_data_2(
            self->m_model, i, self->m_modelColumn);
        self->m_items[i] = XString_create_utf8(text ? text : "");
        self->m_itemSeparators[i] = false;
    }
    self->m_itemCount = rows;
}

/** @brief 模型变化后收敛当前项（Qt rowsInserted/rowsRemoved 的公共尾段）。
 * @details 口径（Qt 6.8 本机探针）：
 *          - 空模型 ⇒ currentIndex=-1（变化时发射 currentIndexChanged(-1)）；
 *          - 原无当前项且非空 ⇒ 第 0 行（模型首次填入自动选中，探针
 *            `m->setStringList(...)` 后 cur 0/text "p"）；
 *          - 当前项越界 ⇒ 末行（缩容后不悬空）。 */
static void xcombo_convergeCurrent(XComboBox* self)
{
    int rows;
    if (!self) return;
    rows = xcombo_countInternal(self);
    if (rows == 0) {
        if (self->m_currentIndex != -1) {
            self->m_currentIndex = -1;
            xcombo_changeCurrentIndex(self, -1);
        }
    } else if (self->m_currentIndex < 0) {
        self->m_currentIndex = 0;
        xcombo_changeCurrentIndex(self, 0);
    } else if (self->m_currentIndex >= rows) {
        self->m_currentIndex = rows - 1;
        xcombo_changeCurrentIndex(self, self->m_currentIndex);
    }
}

/** @brief rowsInserted 槽：重建镜像并把当前项之前插入的行静默计入索引。
 * @details Qt rowsInserted（qcombobox.cpp:1131-1153）：当前项索引作为持久
 *          索引静默右移（不发 currentIndexChanged），空模型首行插入时改走
 *          setCurrentIndex(0)（发射）。本函数按信号载荷 (first,last) 判方位。 */
static void xcombo_modelRowsInsertedSlot(XObject* receiver, XVarList* args)
{
    XComboBox* self = (XComboBox*)receiver;
    int first = 0;
    int last = 0;
    int added;
    if (!self || !xcombo_hasExternalModel(self)) return;
    if (args) {
        first = XVarList_arg(args, int);
        last = XVarList_arg(args, int);
    }
    added = last - first + 1;
    if (added < 1) added = 1;
    {
        int oldCurrent = self->m_currentIndex;
        xcombo_mirrorModel(self);
        if (oldCurrent < 0)
            xcombo_convergeCurrent(self); /* 空→首行：发 currentIndexChanged(0) */
        else if (first <= oldCurrent) {
            self->m_currentIndex = oldCurrent + added; /* 静默平移。 */
            if (self->m_currentIndex >= self->m_itemCount)
                xcombo_convergeCurrent(self);
        }
    }
    xcombo_updateSizeHint(self);
    XWidget_update((XWidget*)self);
}

/** @brief rowsRemoved 槽：重建镜像并按移除位置收敛当前项。
 * @details 口径同本地 XComboBox_removeItem 的 rowsRemoved 分支：移除点
 *          在当前项之前 ⇒ 左移（发射，与既有实现一致）；移除当前项或当前
 *          项越界 ⇒ 取 min(rows-1, first)（Qt `qcombobox.cpp:1175-1185`）。 */
static void xcombo_modelRowsRemovedSlot(XObject* receiver, XVarList* args)
{
    XComboBox* self = (XComboBox*)receiver;
    int first = 0;
    int last = 0;
    int removed;
    int oldCurrent;
    if (!self || !xcombo_hasExternalModel(self)) return;
    if (args) {
        first = XVarList_arg(args, int);
        last = XVarList_arg(args, int);
    }
    removed = last - first + 1;
    if (removed < 1) removed = 1;
    oldCurrent = self->m_currentIndex;
    xcombo_mirrorModel(self);
    if (self->m_itemCount == 0) {
        if (self->m_currentIndex != -1) {
            self->m_currentIndex = -1;
            xcombo_changeCurrentIndex(self, -1);
        }
    } else if (oldCurrent >= 0) {
        if (first + removed <= oldCurrent) {
            int next = oldCurrent - removed;
            if (next >= self->m_itemCount) next = self->m_itemCount - 1;
            self->m_currentIndex = next;
            xcombo_changeCurrentIndex(self, next);
        } else if (first <= oldCurrent) {
            int next = first < self->m_itemCount ? first : self->m_itemCount - 1;
            self->m_currentIndex = next;
            xcombo_changeCurrentIndex(self, next);
        }
    }
    if (self->m_currentIndex >= self->m_itemCount)
        xcombo_convergeCurrent(self);
    xcombo_updateSizeHint(self);
    XWidget_update((XWidget*)self);
}

/** @brief dataChanged 槽：重建文本镜像；当前行文本变化补发
 *         currentTextChanged（Qt dataChanged 分支，qcombobox.cpp:1088-1115）。 */
static void xcombo_modelDataChangedSlot(XObject* receiver, XVarList* args)
{
    XComboBox* self = (XComboBox*)receiver;
    int topRow = 0;
    int bottomRow = 0;
    const char* text;
    if (!self || !xcombo_hasExternalModel(self)) return;
    if (args) {
        topRow = XVarList_arg(args, int);
        (void)XVarList_arg(args, int); /* topCol（本库单列镜像不使用）。 */
        bottomRow = XVarList_arg(args, int);
    }
    xcombo_mirrorModel(self);
    if (self->m_currentIndex >= topRow && self->m_currentIndex <= bottomRow) {
        text = XComboBox_itemText_2(self, self->m_currentIndex);
        if (!text) text = "";
        if (self->m_editable && self->m_lineEdit) {
            const char* edit = XLineEdit_text(self->m_lineEdit);
            if (!edit || XStrcmp(edit, text) != 0)
                XLineEdit_setText(self->m_lineEdit, text);
        }
        xcombo_emitText(self,
                        (size_t)XComboBox_currentTextChanged_signal(self, text),
                        text);
    }
    xcombo_updateSizeHint(self);
    XWidget_update((XWidget*)self);
}

/** @brief 模型销毁槽：清空引用与条目（Qt：模型删除后 count=0、cur=-1）。
 * @note  借用模型被外部释放时本组合框必须失联，否则后续 count/itemText
 *        读悬垂模型；槽内只清引用，不做 emit 以外的模型访问。 */
static void xcombo_modelDestroyedSlot(XObject* receiver, XVarList* args)
{
    XComboBox* self = (XComboBox*)receiver;
    int i;
    (void)args;
    if (!self || !xcombo_hasExternalModel(self)) return;
    self->m_model = NULL;
    self->m_modelOwned = false;
    for (i = 0; i < self->m_itemCount; ++i) {
        if (self->m_items[i]) {
            XClassDelete((XClass*)self->m_items[i]);
            self->m_items[i] = NULL;
        }
        if (self->m_itemData && self->m_itemData[i]) {
            XClassDelete((XClass*)self->m_itemData[i]);
            self->m_itemData[i] = NULL;
        }
        if (self->m_itemIcons && self->m_itemIcons[i]) {
            XClassDelete((XClass*)self->m_itemIcons[i]);
            self->m_itemIcons[i] = NULL;
        }
        if (self->m_itemSeparators) self->m_itemSeparators[i] = false;
    }
    self->m_itemCount = 0;
    if (self->m_popupView)
        XAbstractItemView_setModel((XAbstractItemView*)self->m_popupView, NULL);
    if (self->m_currentIndex != -1) {
        self->m_currentIndex = -1;
        xcombo_changeCurrentIndex(self, -1);
    }
    xcombo_updateSizeHint(self);
    XWidget_update((XWidget*)self);
}

/** @brief 断开外部模型的全部信号连接（换模型/析构前调用）。 */
static void xcombo_detachModelSignals(XComboBox* self)
{
    XObject* m;
    if (!self || !self->m_model) return;
    m = (XObject*)self->m_model;
    XObject_disconnect(m, (size_t)XAbstractItemModel_rowsInserted_signal(
                               self->m_model, 0, 0),
                         (XObject*)self, xcombo_modelRowsInsertedSlot);
    XObject_disconnect(m, (size_t)XAbstractItemModel_rowsRemoved_signal(
                               self->m_model, 0, 0),
                         (XObject*)self, xcombo_modelRowsRemovedSlot);
    XObject_disconnect(m, (size_t)XAbstractItemModel_dataChanged_signal(
                               self->m_model, 0, 0, 0, 0, 0),
                         (XObject*)self, xcombo_modelDataChangedSlot);
    /* destroyed 信号标识经 XSignal 取（信号函数 = 标识，直接取函数地址，
     * 不能调用——调用会真的发射一次 destroyed）。 */
    XObject_disconnect(m, XSignal(XObject_destroyed_signal),
                         (XObject*)self, xcombo_modelDestroyedSlot);
}

void XComboBox_setModel(XComboBox* self, XAbstractItemModel* model)
{
    /* 对标 QComboBox::setModel（qcombobox.cpp:2038-2062）：
     * - 0 模型是空操作（Qt 告警后返回，不"清空释放"）；
     * - 同模型重复设置为空操作；
     * - 新模型**借用**，本控件不取得所有权；
     * - 只释放**本控件拥有**的旧模型（Qt：旧模型 parent 为本控件才
     *   delete，否则仅断开连接）。
     * [Qt 对齐 2026-11-11] 新模型是唯一数据源：装好后立即镜像其行文本，
     * 并挂 rowsInserted/rowsRemoved/dataChanged/destroyed 信号（见
     * xcombo_mirrorModel 段说明）。 */
    if (!self || !model || self->m_model == model) return;
    xcombo_detachModelSignals(self);
    if (self->m_popupView)
        XAbstractItemView_setModel((XAbstractItemView*)self->m_popupView,
                                   model);
    if (self->m_model && self->m_modelOwned) {
        /* 旧模型可能正被弹层视图的条目渲染/activated 发射链引用；
           延迟回收，避免信号在途时结构体被同步释放。 */
        XObject_deleteLater((XObject*)self->m_model);
    }
    self->m_model = model;
    self->m_modelOwned = false; /* 外部模型：借用，析构不释放。 */
    /* 换模型后旧内容作废：释放本地条目文本与平行载荷，按模型重建。 */
    {
        int i;
        for (i = 0; i < self->m_itemCount; ++i) {
            if (self->m_items[i]) {
                XClassDelete((XClass*)self->m_items[i]);
                self->m_items[i] = NULL;
            }
            if (self->m_itemData && self->m_itemData[i]) {
                XClassDelete((XClass*)self->m_itemData[i]);
                self->m_itemData[i] = NULL;
            }
            if (self->m_itemIcons && self->m_itemIcons[i]) {
                XClassDelete((XClass*)self->m_itemIcons[i]);
                self->m_itemIcons[i] = NULL;
            }
            if (self->m_itemSeparators) self->m_itemSeparators[i] = false;
        }
        self->m_itemCount = 0;
    }
    self->m_currentIndex = -1;
    xcombo_mirrorModel(self);
    /* 挂接模型信号（Qt 组合框对模型结构/数据/销毁全响应）。 */
    {
        XObject* m = (XObject*)model;
        XObject_connect(m, (size_t)XAbstractItemModel_rowsInserted_signal(
                                 model, 0, 0),
                          (XObject*)self, xcombo_modelRowsInsertedSlot,
                          XConnectionType_Direct);
        XObject_connect(m, (size_t)XAbstractItemModel_rowsRemoved_signal(
                                 model, 0, 0),
                          (XObject*)self, xcombo_modelRowsRemovedSlot,
                          XConnectionType_Direct);
        XObject_connect(m, (size_t)XAbstractItemModel_dataChanged_signal(
                                 model, 0, 0, 0, 0, 0),
                          (XObject*)self, xcombo_modelDataChangedSlot,
                          XConnectionType_Direct);
        XObject_connect(m, XSignal(XObject_destroyed_signal),
                          (XObject*)self, xcombo_modelDestroyedSlot,
                          XConnectionType_Direct);
    }
    /* Qt 口径：setModel 后当前项按新模型重设——非空取 0（发射），空则 -1。 */
    if (self->m_itemCount > 0) {
        self->m_currentIndex = 0;
        xcombo_changeCurrentIndex(self, 0);
    }
    xcombo_updateSizeHint(self);
    XWidget_update((XWidget*)self);
}

int XComboBox_modelColumn(const XComboBox* self)
{ return self ? self->m_modelColumn : 0; }

void XComboBox_setModelColumn(XComboBox* self, int column)
{
    if (!self) return;
    self->m_modelColumn = column;
    if (self->m_popupView)
        XListView_setModelColumn(self->m_popupView, column);
    /* 外部模型：itemText 取自该列（Qt modelColumn 语义），换列即重镜像。 */
    if (xcombo_hasExternalModel(self)) {
        xcombo_mirrorModel(self);
        xcombo_convergeCurrent(self);
        xcombo_updateSizeHint(self);
        XWidget_update((XWidget*)self);
    }
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
    /* [P3-57 Qt 对齐] 转发弹层视图（qcombobox.cpp:2014-2019：
     * view()->setItemDelegate(delegate)）——视图编辑闭环按
     * XItemDelegate* 虚表分派（行级→列级→本指针→内建默认委托，
     * XAbstractItemView.c xaiv_resolveDelegate）。NULL 按 Qt 口径拒绝
     * （qWarning「cannot set a 0 delegate」的静默等价，不转发不清除；
     * 恢复视图默认委托可经 view() 后直调视图级 setter）。委托为借用
     * 语义，组合框与视图均不取得所有权（Qt 同）——传入非 XItemDelegate
     * 指针并在开启弹层编辑后使用为未定义行为。 */
    if (!self || !delegate) return;
    XAbstractItemView_setItemDelegate(
        (XAbstractItemView*)XComboBox_view(self), delegate);
}

void* XComboBox_itemDelegate(const XComboBox* self)
{
    /* [P3-57 Qt 对齐] 读弹层视图（qcombobox.cpp:2006-2012：
     * view()->itemDelegate()；懒建同 Qt view()）。内建默认委托仅在
     * 编辑会话期由视图懒创建，不经本查询返回。 */
    if (!self) return NULL;
    return XAbstractItemView_itemDelegate(
        (const XAbstractItemView*)XComboBox_view((XComboBox*)self));
}

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
            XClassDelete((XClass*)cur);
        }
    }
    return out;
}

XVariant* XComboBox_inputMethodQuery_2(XComboBox* self, int query,
                                       const XVariant* argument)
{
    /* 对标 QComboBox::inputMethodQuery(Qt::InputMethodQuery, const QVariant&)
     * （qcombobox.h:161）。Qt 6.8 本机探针（只读/可编辑两态，单参/双参各
     * 一遍）：
     *  - 可编辑 ⇒ ImCursorRectangle/ImSurroundingText/ImCursorPosition
     *    三项与**内嵌行编辑框**的同名查询逐项相等（rect QRect(66,0 9x19)、
     *    text "HelloWorldLongText"、pos 11），双参结果与单参一致；
     *  - 只读 ⇒ ImCursorRectangle 为 QWidget 缺省的 (w/2,0,1,h)，其余两项
     *    为无效变体；
     *  - 非空 argument 不改变结果。
     * 本实现：可编辑态把这三项转发行编辑框（走 XWidget_inputMethodQuery
     * 虚槽，行编辑自身重载），其余查询一律回落基类控件实现；argument 为
     * 借用承载（当前无查询项消费，Qt 的行编辑实现同样忽略它）。 */
    if (!self) return NULL;
#if XINPUTMETHOD_ON
    if (self->m_editable && self->m_lineEdit &&
        (query == (int)XInputMethodQuery_ImCursorRectangle ||
         query == (int)XInputMethodQuery_ImSurroundingText ||
         query == (int)XInputMethodQuery_ImCursorPosition)) {
        (void)argument;
        return XWidget_inputMethodQuery((const XWidget*)self->m_lineEdit,
                                        (XInputMethodQuery)query);
    }
    (void)argument;
    return XWidget_inputMethodQuery((const XWidget*)self,
                                    (XInputMethodQuery)query);
#else
    /* 输入法子系统裁剪（XINPUTMETHOD_ON=0）：XWidget_inputMethodQuery 不
     * 参与编译，本重载按 Qt「无效 QVariant」口径返回 NULL。 */
    (void)query;
    (void)argument;
    return NULL;
#endif /* XINPUTMETHOD_ON */
}

int XComboBox_currentIndex(const XComboBox* self)
{
    return self ? self->m_currentIndex : -1;
}

/** @brief 当前项变更的统一出口（对标 QComboBoxPrivate::setCurrentIndex 尾
 *  部 + emitCurrentIndexChanged，qcombobox.cpp:2157-2185/1421-1431）：行编
 *  辑回填 → currentIndexChanged(index) → currentTextChanged(text)。
 *  去重判定（indexChanged）由调用方承担；可编辑回填对标
 *  qcombobox.cpp:2165-2171（itemText 写回行编辑，分叉态收敛）。 */
static void xcombo_changeCurrentIndex(XComboBox* self, int index)
{
    if (!self) return;
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
    const char* text;
    if (edit) return edit;
    if (!self || self->m_currentIndex < 0 ||
        self->m_currentIndex >= self->m_itemCount ||
        !self->m_items[self->m_currentIndex])
        return "";
    /* Qt 的 currentText() 对空项返回**空 QString 而非 null**；
     * XString_toUtf8 对空串返回 NULL（XString.c 空串早退），此处归一为
     * ""——与下方无效索引的 "" 保持同一契约（QComboBox::itemText 同）。 */
    text = XString_toUtf8(self->m_items[self->m_currentIndex]);
    return text ? text : "";
}
XString* XComboBox_itemText(const XComboBox* self, int index)
{
    if (self && index >= 0 && index < self->m_itemCount && self->m_items[index])
        return XString_create_copy(self->m_items[index]);
    return NULL;
}
const char* XComboBox_itemText_2(const XComboBox* self, int index)
{
    const char* text;
    if (self && index >= 0 && index < self->m_itemCount && self->m_items[index]) {
        text = XString_toUtf8(self->m_items[index]);
        /* 空项（分隔条/空文本项）归一为 ""：Qt itemText() 返回空 QString，
         * 调用方不会得到 null；无效索引同样返回 ""。 */
        return text ? text : "";
    }
    return "";
}
int XComboBox_findText(const XComboBox* self, const XString* text)
{
    int i;
    if (!self || !text) return -1;
    /* 等价 findText(text, MatchExactly|MatchCaseSensitive)（QComboBox 缺省）。 */
    return XComboBox_findText_3(self, XString_toUtf8(text),
                                (unsigned)(XComboBoxMatchExactly |
                                           XComboBoxMatchCaseSensitive));
}
int XComboBox_findText_2(const XComboBox* self, const char* text)
{
    return XComboBox_findText_3(self, text,
                                (unsigned)(XComboBoxMatchExactly |
                                           XComboBoxMatchCaseSensitive));
}

/** @brief 单个字符串按 Qt::MatchFlags 匹配（Qt 6.8 本机探针口径）。
 * @details - MatchExactly：QVariant/QString 相等比较，**恒大小写敏感**
 *            （探针：MatchExactly 不带 MatchCaseSensitive 时 "RED" 不命中
 *            "red"，QAbstractItemModel::match 的 MatchExactly 分支走
 *            `value == idxData`）；
 *          - MatchContains/StartsWith/EndsWith：缺省大小写不敏感，带
 *            MatchCaseSensitive 才敏感（探针 contains/starts/ends 均命中）；
 *          - MatchFixedString：按字符串比较（与 Contains 等组合时同普通
 *            子串语义）；MatchRegularExpression/MatchWildcard 无引擎 ⇒
 *            调用方返回 -1（不静默降级）。 */
static bool xcombo_matchUtf8(const char* haystack, const char* needle,
                             unsigned flags)
{
    unsigned type = flags & (unsigned)XComboBoxMatchTypeMask;
    bool cs = (flags & (unsigned)XComboBoxMatchCaseSensitive) != 0u;
    size_t hl;
    size_t nl;
    if (!haystack || !needle) return false;
    if (type == (unsigned)XComboBoxMatchRegularExpression ||
        type == (unsigned)XComboBoxMatchWildcard)
        return false; /* 无正则/通配引擎（见函数注记）。 */
    switch (type) {
    case (unsigned)XComboBoxMatchContains: {
        size_t i;
        hl = XStrlen(haystack);
        nl = XStrlen(needle);
        if (nl == 0) return true;
        if (nl > hl) return false;
        for (i = 0; i + nl <= hl; ++i) {
            if (cs ? (XStrncmp(haystack + i, needle, nl) == 0)
                   : (XStrncasecmp(haystack + i, needle, nl) == 0))
                return true;
        }
        return false;
    }
    case (unsigned)XComboBoxMatchStartsWith:
        nl = XStrlen(needle);
        return cs ? (XStrncmp(haystack, needle, nl) == 0)
                  : (XStrncasecmp(haystack, needle, nl) == 0);
    case (unsigned)XComboBoxMatchEndsWith: {
        hl = XStrlen(haystack);
        nl = XStrlen(needle);
        if (nl == 0) return true;
        if (nl > hl) return false;
        return cs ? (XStrncmp(haystack + hl - nl, needle, nl) == 0)
                  : (XStrncasecmp(haystack + hl - nl, needle, nl) == 0);
    }
    case (unsigned)XComboBoxMatchExactly:
    case (unsigned)XComboBoxMatchFixedString:
    default:
        /* MatchExactly：Qt 走 QVariant 相等 ⇒ 恒大小写敏感（探针口径）。 */
        return cs ? (XStrcmp(haystack, needle) == 0)
                  : (type == (unsigned)XComboBoxMatchExactly
                         ? (XStrcmp(haystack, needle) == 0)
                         : (XStrcasecmp(haystack, needle) == 0));
    }
}

int XComboBox_findText_3(const XComboBox* self, const char* text,
                         unsigned flags)
{
    int i;
    if (!self || !text) return -1;
    for (i = 0; i < self->m_itemCount; ++i) {
        if (!self->m_items[i]) continue;
        if (xcombo_matchUtf8(XString_toUtf8(self->m_items[i]), text, flags))
            return i;
    }
    return -1;
}

void XComboBox_setCurrentIndex(XComboBox* self, int index)
{
    int old;
    if (!self) return;
    if (index < -1) index = -1;
    /* 对标 qcombobox.cpp:2150-2152 setCurrentIndex：index>=0 时以
     * model->index(index,...) 构造——越界行号给出无效模型索引，
     * QComboBoxPrivate::setCurrentIndex 置 currentIndex 为无效
     * （currentIndex() 读 -1），并经 emitCurrentIndexChanged 发射
     * currentIndexChanged(-1)/currentTextChanged("")。不钳位到末项。 */
    if (index >= self->m_itemCount) index = -1;
    old = self->m_currentIndex;
    if (index == old) return;
    self->m_currentIndex = index;
    xcombo_changeCurrentIndex(self, index);
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
    XString* copy;
    if (!self || !text) return;
    /* 对标 Qt：index = qBound(0, index, itemCount)（负数插到最前、
       超出项数则追加到尾部）。 */
    if (index < 0) index = 0;
    if (index > self->m_itemCount) index = self->m_itemCount;
    /* 对标 qcombobox.cpp:2310-2312 insertItem：钳位后 index>=maxCount
       直接返回（容量满时不插入、不从尾部挤掉既有条目）。 */
    if (index >= self->m_maxCount) return;
    /* [Qt 对齐 2026-11-11] 外部模型是唯一数据源：插入写到模型行
       （insertRows + setData），镜像由模型信号驱动（Qt 的
       QComboBox::insertItem 就是往 d->model 插行）。 */
    if (xcombo_hasExternalModel(self)) {
        int row = index;
        if (XAbstractItemModel_columnCount(self->m_model) == 0)
            XAbstractItemModel_insertColumns(self->m_model, 0, 1);
        if (!XAbstractItemModel_insertRows(self->m_model, row, 1)) return;
        XAbstractItemModel_setData(self->m_model, row, self->m_modelColumn,
                                   text);
        while (xcombo_countInternal(self) > self->m_maxCount)
            XAbstractItemModel_removeRows(
                self->m_model, xcombo_countInternal(self) - 1, 1);
        return;
    }
    if (self->m_itemCount >= self->m_itemCapacity) {
        /* 四组平行数组（文本/数据/图标/分隔标记）一并扩容（新槽置空）。 */
        if (!xcombo_reserveItems(self, self->m_itemCount + 1)) return;
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
    if (self->m_itemImages)
        XMemmove(&self->m_itemImages[index + 1], &self->m_itemImages[index],
                 sizeof(XImage*) * (size_t)(self->m_itemCount - index));
    if (self->m_itemSeparators)
        XMemmove(&self->m_itemSeparators[index + 1],
                 &self->m_itemSeparators[index],
                 sizeof(bool) * (size_t)(self->m_itemCount - index));
    self->m_items[index] = copy;
    if (self->m_itemData) self->m_itemData[index] = NULL;
    if (self->m_itemIcons) self->m_itemIcons[index] = NULL;
    if (self->m_itemImages) self->m_itemImages[index] = NULL;
    if (self->m_itemSeparators) self->m_itemSeparators[index] = false;
    ++self->m_itemCount;
    /* 对标 Qt 6.8 QComboBoxPrivate::rowsInserted（qcombobox.cpp:1131-1153）：
       空 combo 插入首项且无 placeholderText 时自动 setCurrentIndex(0)
       （并发射 currentIndexChanged/currentTextChanged）——此前注释声称
       「首项插入保持 -1」与 Qt 源码不符（2026-11-03 实证：工具条 combo
       addItem 后闭态仍显示占位文本而非首项）。有 placeholderText 时不
       自动选中（Qt 口径：显示占位文本，闭态留空）。仅当插入位置在当前
       项之前时当前项索引右移（Qt d->inserting 抑制 rowsInserted 发射，
       persistent index 静默平移——须先于尾部裁剪，否则裁剪后的索引
       右移可能越过 m_itemCount-1 越界）。 */
    if (self->m_currentIndex < 0 && self->m_itemCount == 1 && index == 0 &&
        !(self->m_placeholderText &&
          XString_toUtf8(self->m_placeholderText) &&
          XString_toUtf8(self->m_placeholderText)[0])) {
        /* 经统一收口置当前项：可编辑 combo 行编辑回填（分叉态收敛）+
           发射对信号，与 setCurrentIndex 同路径（Qt rowsInserted→
           setCurrentIndex(0) 同样走 QComboBoxPrivate::setCurrentIndex）。
           直翻 m_currentIndex 裸发射会让可编辑态 currentText 载荷取到
           行编辑残留（空串）而非首项文本——2026-11-03 FontComboBox
           currentFamily 非空断言失败实证。 */
        self->m_currentIndex = 0;
        xcombo_changeCurrentIndex(self, 0);
    } else if (index <= self->m_currentIndex) {
        ++self->m_currentIndex;
    }
    /* 对标 qcombobox.cpp:2346：插入后超出 maxCount 时从尾部裁剪
       （removeRows(itemCount-1, ...)；裁剪经 removeItem 的 rowsRemoved
       语义调整当前项并发射对信号）。 */
    if (self->m_itemCount > self->m_maxCount) {
        while (self->m_itemCount > self->m_maxCount)
            XComboBox_removeItem(self, self->m_itemCount - 1);
    }
    /* [Qt 对齐 2026-11-02] Qt 的 insertItem 内部经 rowsInserted 触发
       sizeHint 刷新；此前本实现只在 addItem* 外层补刷 → 直接调
       insertItem/insertItem_2 时首选尺寸不更新（后续布局按旧宽排）。
       刷新收到本函数内，addItem* 外层那两处补刷随之删除。
       [P0-04 Qt 对齐] rowsInserted 仅在 AdjustToContents 档失效重算
       （qcombobox.cpp:1110-1125）；其余档首显后冻结（冻结语义见
       XComboBox_setSizeAdjustPolicy 注记）。 */
    if (self->m_sizeAdjustPolicy ==
        (uint8_t)XComboBoxSizeAdjustPolicy_AdjustToContents) {
        self->m_sizeHintValid = false;
        xcombo_updateSizeHint(self);
    }
    XWidget_update((XWidget*)self);
}
void XComboBox_insertItem_2(XComboBox* self, int index, const char* text)
{
    XString_Init_Utf8(tmp, text ? text : "");
    XComboBox_insertItem(self, index, tmp);
    XClassDeinit((XClass*)tmp);
}

void XComboBox_insertItem_3(XComboBox* self, int index, const char* iconPath,
                            const char* text, const char* userData)
{
    /* 对标 QComboBox::insertItem(index, icon, text, userData)：
     * 图标/数据在插入后立即落到该条目（与 setItemIcon/setItemData 同源），
     * 避免「先插入再补字段」的中间态被观察者看到。钳位索引由
     * insertItem_2 完成；本条目的实际落点按同口径重算（负数→0、越界→末尾）。 */
    int at;
    if (!self || !text) return;
    at = index;
    if (at < 0) at = 0;
    if (at > xcombo_countInternal(self)) at = xcombo_countInternal(self);
    XComboBox_insertItem_2(self, index, text);
    if (iconPath) XComboBox_setItemIcon_2(self, at, iconPath);
    if (userData) XComboBox_setItemData_2(self, at, userData);
}

void XComboBox_insertItem_4(XComboBox* self, int index, const char* text,
                            const char* userData)
{
    XComboBox_insertItem_3(self, index, NULL, text, userData);
}

void XComboBox_insertItems(XComboBox* self, int index, const XStringList* texts)
{
    int i, n;
    if (!self || !texts) return;
    /* 对标 QComboBox::insertItems（qcombobox.cpp:2357-2370）：先把起点
       `qBound(0, index, count())` 收口一次再**逐项前移**——此前只在循环
       内判 `index >= 0`，负数起点下每次 insertItem 又把新项插到 0，整批
       顺序被反转（Qt 实测插入 [x,y] 得 x,y 原序）。 */
    if (index < 0) index = 0;
    if (index > self->m_itemCount) index = self->m_itemCount;
    n = (int)XVector_size_base((const XContainer*)texts);
    for (i = 0; i < n; ++i) {
        XString* s = *(XString**)XStringList_at_base((const XVector*)texts, i);
        XComboBox_insertItem(self, index, s);
        ++index;
    }
}
void XComboBox_insertItems_2(XComboBox* self, int index, const char* const* texts)
{
    int i;
    if (!self || !texts) return;
    /* 起点钳制口径同 XComboBox_insertItems（Qt qBound(0,index,count)）。 */
    if (index < 0) index = 0;
    if (index > self->m_itemCount) index = self->m_itemCount;
    for (i = 0; texts[i]; ++i) {
        XComboBox_insertItem_2(self, index, texts[i]);
        ++index;
    }
}

void XComboBox_addItem(XComboBox* self, const XString* text)
{
    /* 对标 Qt addItem：追加到尾部。 */
    XComboBox_insertItem(self, self ? xcombo_countInternal(self) : 0, text);
    /* sizeHint 刷新已收进 insertItem（Qt 同口径），此处不重复。 */
}
void XComboBox_addItem_2(XComboBox* self, const char* text)
{
    XComboBox_insertItem_2(self, self ? xcombo_countInternal(self) : 0, text);
    /* sizeHint 刷新已收进 insertItem（Qt 同口径），此处不再补刷。 */
}

void XComboBox_addItem_3(XComboBox* self, const char* iconPath,
                         const char* text, const char* userData)
{
    XComboBox_insertItem_3(self, self ? xcombo_countInternal(self) : 0,
                           iconPath, text, userData);
}

void XComboBox_addItem_4(XComboBox* self, const char* text,
                         const char* userData)
{
    XComboBox_insertItem_4(self, self ? xcombo_countInternal(self) : 0, text,
                           userData);
}

void XComboBox_addItems(XComboBox* self, const XStringList* texts)
{
    /* 对标 Qt addItems：从当前项数处顺序追加。 */
    XComboBox_insertItems(self, self ? xcombo_countInternal(self) : 0, texts);
}
void XComboBox_addItems_2(XComboBox* self, const char* const* texts)
{
    XComboBox_insertItems_2(self, self ? xcombo_countInternal(self) : 0,
                            texts);
}

void XComboBox_insertSeparator(XComboBox* self, int index)
{
    /* 对标 QComboBox::insertSeparator（qcombobox.cpp 6.8）。本机 Qt 6.8.0
     * 探针：插入的是**空文本**行、该行
     * `itemData(i, Qt::AccessibleDescriptionRole) == "separator"`、模型
     * flags 去掉 ItemIsEnabled/ItemIsSelectable（可程序化选中，但弹层交互
     * 不可选）；索引口径与 insertItem 一致（-1 插最前、99 追加）。
     * 本库模型无角色体系，以平行标记数组承载，查询见 isItemSeparator。
     * 注：外部模型（setModel）没有角色存储，此时分隔标记只存在本地镜像，
     *     模型后续结构变化触发重镜像会清掉标记（Qt 存 AccessibleDescription
     *     Role，故不受影响）——差异已在 XComboBox_setModel 注记。 */
    int at;
    if (!self) return;
    at = index;
    if (at < 0) at = 0;
    if (at > xcombo_countInternal(self)) at = xcombo_countInternal(self);
    if (at >= self->m_maxCount) return; /* Qt：先 insertItem 再打标记，插入被
                                           maxCount 拒绝时不留标记。 */
    XComboBox_insertItem_2(self, index, "");
    if (at < self->m_itemCount && self->m_itemSeparators) {
        self->m_itemSeparators[at] = true;
        XWidget_update((XWidget*)self);
    }
}

bool XComboBox_isItemSeparator(const XComboBox* self, int index)
{
    if (!self || !self->m_itemSeparators) return false;
    if (index < 0 || index >= self->m_itemCount) return false;
    return self->m_itemSeparators[index];
}

void XComboBox_removeItem(XComboBox* self, int index)
{
    XString* removed;
    int oldCurrent;
    if (!self || index < 0 || index >= self->m_itemCount) return;
    /* [Qt 对齐 2026-11-11] 外部模型：删除写回模型（rowsRemoved 槽按 Qt
       rowsRemoved 口径收敛当前项）。 */
    if (xcombo_hasExternalModel(self)) {
        XAbstractItemModel_removeRows(self->m_model, index, 1);
        return;
    }
    oldCurrent = self->m_currentIndex;
    removed = self->m_items[index];
    XMemmove(&self->m_items[index], &self->m_items[index + 1],
             sizeof(XString*) * (size_t)(self->m_itemCount - index - 1));
    if (self->m_itemData) {
        if (self->m_itemData[index])
            XClassDelete((XClass*)self->m_itemData[index]);
        XMemmove(&self->m_itemData[index], &self->m_itemData[index + 1],
                 sizeof(XString*) * (size_t)(self->m_itemCount - index - 1));
    }
    if (self->m_itemIcons) {
        if (self->m_itemIcons[index])
            XClassDelete((XClass*)self->m_itemIcons[index]);
        XMemmove(&self->m_itemIcons[index], &self->m_itemIcons[index + 1],
                 sizeof(XString*) * (size_t)(self->m_itemCount - index - 1));
    }
    if (self->m_itemImages) {
        if (self->m_itemImages[index])
            XClassDelete((XClass*)self->m_itemImages[index]);
        XMemmove(&self->m_itemImages[index], &self->m_itemImages[index + 1],
                 sizeof(XImage*) * (size_t)(self->m_itemCount - index - 1));
    }
    if (self->m_itemSeparators)
        XMemmove(&self->m_itemSeparators[index],
                 &self->m_itemSeparators[index + 1],
                 sizeof(bool) * (size_t)(self->m_itemCount - index - 1));
    --self->m_itemCount;
    XClassDelete((XClass*)removed);
    /* 对标 QComboBoxPrivate::rowsRemoved（qcombobox.cpp:1155-1185）：
     * 移除引发的当前项变化经 emitCurrentIndexChanged 发射对信号。 */
    if (self->m_itemCount == 0) {
        /* 模型清空：setCurrentIndex(无效) → currentIndex=-1（1170-1173）。 */
        if (self->m_currentIndex != -1) {
            self->m_currentIndex = -1;
            xcombo_changeCurrentIndex(self, -1);
        }
    } else if (index < oldCurrent) {
        /* 当前项在移除点之后：行号左移（Qt persistent index 自动平移，
         * 1180-1183 发射）。 */
        --self->m_currentIndex;
        xcombo_changeCurrentIndex(self, self->m_currentIndex);
    } else if (index == oldCurrent && oldCurrent >= 0) {
        /* 当前项被移除：Qt 重选 min(count-1, max(indexBeforeChange,0))
         * （1175-1178）——即移除点处的后继项。 */
        int re = self->m_itemCount - 1;
        if (re > index) re = index;
        if (re < 0) re = 0;
        self->m_currentIndex = re;
        xcombo_changeCurrentIndex(self, re);
    }
    /* index > oldCurrent：当前项在移除点之前，不受影响不发射（Qt 同）。 */
    /* [P0-04 Qt 对齐] rowsRemoved 仅在 AdjustToContents 档失效重算
     * （qcombobox.cpp:1150-1161）。 */
    if (self->m_sizeAdjustPolicy ==
        (uint8_t)XComboBoxSizeAdjustPolicy_AdjustToContents) {
        self->m_sizeHintValid = false;
        xcombo_updateSizeHint(self);
    }
    XWidget_update((XWidget*)self);
}

/** @brief 条目文本变更后的当前项联动（对标 QComboBoxPrivate::dataChanged
 *  qcombobox.cpp:1088-1115：变更行覆盖当前项时，可编辑走行编辑回填——
 *  行编辑 textChanged 已连接 editTextChanged/currentTextChanged
 *  （1850-1851），非可编辑走 updateCurrentText）。changed=false（同文本
 *  重设）时不动：QLineEdit::setText 与 updateCurrentText（2919-2927）均
 *  按等值去重，不产生信号。 */
static void xcombo_itemTextChanged(XComboBox* self, int index, bool changed)
{
    const char* newText;
    if (!self || !changed) return;
    if (index < 0 || index >= self->m_itemCount) return;
    if (index != self->m_currentIndex) return;
    newText = self->m_items[index] ? XString_toUtf8(self->m_items[index])
                                   : "";
    if (self->m_editable && self->m_lineEdit) {
        /* 行编辑回填：textChanged 转发槽发 editTextChanged；currentText
         * 分叉态随之收敛（Qt 由 dataChanged→lineEdit->setText 承担）。 */
        const char* edit = XLineEdit_text(self->m_lineEdit);
        if (!edit || XStrcmp(edit, newText ? newText : "") != 0)
            XLineEdit_setText(self->m_lineEdit, newText ? newText : "");
    }
    xcombo_emitText(self,
                    (size_t)XComboBox_currentTextChanged_signal(
                        self, newText ? newText : ""),
                    newText ? newText : "");
}

void XComboBox_setItemText(XComboBox* self, int index, const XString* text)
{
    const char* oldText;
    bool changed;
    if (!self || !text || index < 0 || index >= self->m_itemCount) return;
    /* [Qt 对齐 2026-11-11] 外部模型：写回模型列（dataChanged 槽驱动镜像与
       currentTextChanged）。 */
    if (xcombo_hasExternalModel(self)) {
        XAbstractItemModel_setData(self->m_model, index, self->m_modelColumn,
                                   text);
        return;
    }
    oldText = self->m_items[index] ? XString_toUtf8(self->m_items[index])
                                   : "";
    changed = XStrcmp(oldText, XString_toUtf8(text)) != 0;
    XString_assign(self->m_items[index], text);
    xcombo_updateSizeHint(self); /* [sizeHint] 与 _2 版同口径（文本宽变化）。 */
    xcombo_itemTextChanged(self, index, changed);
    XWidget_update((XWidget*)self);
}
void XComboBox_setItemText_2(XComboBox* self, int index, const char* text)
{
    const char* oldText;
    bool changed;
    if (!self || !text || index < 0 || index >= self->m_itemCount) return;
    if (xcombo_hasExternalModel(self)) {
        XAbstractItemModel_setData_2(self->m_model, index, self->m_modelColumn,
                                     text);
        return;
    }
    oldText = self->m_items[index] ? XString_toUtf8(self->m_items[index])
                                   : "";
    changed = XStrcmp(oldText, text) != 0;
    XString_assign_utf8(self->m_items[index], text);
    xcombo_updateSizeHint(self); /* [sizeHint] 文本宽变化随刷新。 */
    xcombo_itemTextChanged(self, index, changed);
    XWidget_update((XWidget*)self);
}

void XComboBox_clear(XComboBox* self)
{
    int i;
    bool hadCurrent;
    if (!self) return;
    /* [Qt 对齐 2026-11-11] 外部模型：清空写回模型（removeRows 全量；
       rowsRemoved 槽收敛 currentIndex=-1）。 */
    if (xcombo_hasExternalModel(self)) {
        int rows = xcombo_countInternal(self);
        if (rows > 0) XAbstractItemModel_removeRows(self->m_model, 0, rows);
        return;
    }
    hadCurrent = (self->m_currentIndex != -1);
    for (i = 0; i < self->m_itemCount; ++i) {
        if (self->m_items[i]) XClassDelete((XClass*)self->m_items[i]);
        if (self->m_itemData && self->m_itemData[i]) {
            XClassDelete((XClass*)self->m_itemData[i]);
            self->m_itemData[i] = NULL;
        }
        if (self->m_itemIcons && self->m_itemIcons[i]) {
            XClassDelete((XClass*)self->m_itemIcons[i]);
            self->m_itemIcons[i] = NULL;
        }
        if (self->m_itemImages && self->m_itemImages[i]) {
            XClassDelete((XClass*)self->m_itemImages[i]);
            self->m_itemImages[i] = NULL;
        }
        if (self->m_itemSeparators) self->m_itemSeparators[i] = false;
    }
    self->m_itemCount = 0;
    self->m_currentIndex = -1;
    /* 对标 QComboBox::clear（qcombobox.cpp:2935 removeRows）→ rowsRemoved
     * 模型空分支（qcombobox.cpp:1170-1173）setCurrentIndex(无效)：发射
     * currentIndexChanged(-1)/currentTextChanged("")；原无当前项则
     * indexChanged=false 不发射（QComboBoxPrivate::setCurrentIndex 去重）。 */
    if (hadCurrent)
        xcombo_changeCurrentIndex(self, -1);
    /* [P0-04 Qt 对齐] 清空属内容变化：仅 AdjustToContents 档失效重算。 */
    if (self->m_sizeAdjustPolicy ==
        (uint8_t)XComboBoxSizeAdjustPolicy_AdjustToContents) {
        self->m_sizeHintValid = false;
        xcombo_updateSizeHint(self); /* [sizeHint] 清空随刷新。 */
    }
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

/** @brief 依据行数重设弹层几何（组合框正下方，宽同组合框）。
 *  [屏幕适配 2026-10-07] 对标 Qt QComboBox::showPopup 的屏幕可用区判定
 *  （qcombobox.cpp：先取弹层锚点所在屏 availableGeometry，下方空间不足
 *  翻到组合框上方并把高度收缩到较大一侧）：下方剩余空间不足所需高度时
 *  改定位到组合框上方；高度钳到 min(所需高, max(下方空间, 上方空间))；
 *  x/y 钳进可用区。靠屏底的组合框弹出后条目不再溢出屏幕不可选。 */
/* [弹窗随转·屏缺席回退 2026-10-22] XScreen 未接线（fbdev 无窗口系统
 * 环境）时 screenAt/primaryScreen 恒 NULL——屏幕适配整块被跳过、弹层
 * 永不翻面/钳高（真机 armel-fbdev 探针实证 screen=(nil)）。几何事实
 * 源回退到显示驱动面板框：XWindow.c xwin_screenRect 既定口径「面板即
 * 屏幕」，XWindowDecoration.c 为同层包含 XPlatformDisplayDriver.h 先
 * 例。成功返回 true 并写 *out（未旋转面板框，逻辑域换算由调用方按
 * opener 旋转角统一换轴）。 */
static bool xcombo_fallbackScreenRect(XRect* out)
{
#if XGUI_ON && XPLATFORM_FBDEV_ON
    const XPlatformDisplayDriverOps* ops = XPlatformDisplayDriver_active();
    XPlatformDisplayInfo info;
    if (ops && ops->probe && ops->probe(&info) &&
        info.m_width > 0 && info.m_height > 0)
    {
        XRect_init(out, 0, 0, info.m_width, info.m_height);
        return true;
    }
#else
    (void)out;
#endif
    return false;
}
/** @brief 弹层宽度基类缺省实现（宽同控件；对标 QComboBox 弹层宽度）。
 * @note  Qt 6.8 本机探针：120 宽普通 QComboBox 弹层宽 120；
 *        QFontComboBox 同控件宽下弹层宽 200（=120×5/3，屏宽 800 未触顶）。 */
int XComboBox_popupWidth_base(const XComboBox* self)
{
    return self ? XWidget_width((const XWidget*)self) : 0;
}

/** @brief 弹层宽度（虚槽分派；派生类可重载 EXComboBox_PopupWidth）。
 * @details 基类链等价 Qt 的 QComboBox 弹层宽度；XFontComboBox 重载后得到
 *          `min(控件宽×5/3, 屏宽)` 的字体下拉口径。虚表缺省（子类未继承
 *          本类 vtable 的异常情形）回落基类实现。 */
int XComboBox_popupWidth(const XComboBox* self)
{
    int (*fn)(const XComboBox*);
    if (!self) return 0;
    if (!XClassGetVtable((XClass*)self)) return XComboBox_popupWidth_base(self);
    fn = XClassGetVirtualFunc((XClass*)self, EXComboBox_PopupWidth,
                              int (*)(const XComboBox*));
    if (!fn || fn == XComboBox_popupWidth_base)
        return XComboBox_popupWidth_base(self);
    return fn(self);
}

static void xcombo_popupReposition(XComboBox* self, int rows)
{
    XListView* view;
    XPoint origin;
    XPoint g;
    XRect r;
    int wantH;
    XScreen* screen;
    XRect avail;
    bool haveAvail;
    int belowSpace;
    int aboveSpace;
    if (!self || !self->m_popupView) return;
    XRect_init(&avail, 0, 0, 0, 0); /* 探针防未初始化（screen NULL 时）。 */
    view = self->m_popupView;
    XWidget_setWindowFlags((XWidget*)view, (XWidgetFlags)XWindowType_Popup);
    origin.x = 0;
    origin.y = XWidget_height((XWidget*)self);
    /* [弹窗随转·逻辑全局 2026-10-22] 弹层顶层窗几何必须用**逻辑全局**坐
       标。XWidget_mapToGlobal 自 2026-10-18 旋转战役起返回**设备全局**
       坐标（窗口局部先做旋转正变换 R 再加原点，供 fbinput 输入/光标链
       使用；XWidget.c:5863 注明口径）；本函数把返回值当逻辑几何直接
       setGeometryRect，0° 时 R 恒等逐位一致，90/270° 则把弹层放到设备
       坐标处（真机昆仑通态实测：弹层错位到 geom=(687,284)，footprint
       恰压 combo 列——点开的释放沿误中弹层行再次换角，弹层闪现即逝、
       后续触控被错位弹层抓取吞掉）。此处改自持父链累加逻辑偏移 +
       顶层逻辑原点（与 XWidget_accumulateOffset 同口径，不施 R）：
       rot==0 时与 mapToGlobal 逐位一致；mapToGlobal 既有契约不动。
       【勘误 2026-10-22】原注「下方 screenAt/availableGeometry 亦逻辑域
       口径，随之归位」与事实不符：屏对象不随窗口旋转（XScreen.c 全文
       无旋转感知；fbdev 登记恒为未旋转面板框，XPlatformFramebuffer_
       posix.c 屏注册处），下方钳位已改按逻辑域换算，见下。 */
    XPoint_init(&g, 0, 0);
    {
        XWidget* top = XWidget_topLevelWidget((XWidget*)self);
        const XWidget* w = (const XWidget*)self;
        while (w && !w->m_isWindow) {
            g.x += w->m_windowRect.x;
            g.y += w->m_windowRect.y;
            w = (const XWidget*)XObject_parent((XObject*)w);
        }
        if (top) {
            g.x += top->m_windowRect.x;
            g.y += top->m_windowRect.y;
        }
    }
    g.x += origin.x;
    g.y += origin.y;
    /* [P3-56] 行高随 iconSize 涨（缺省 16px 恒等基线，视觉零差）。 */
    {
        int rowH = xcombo_popupItemHeight(self);
        XListView_setRowHeight(view, rowH);
        wantH = rows * rowH + 2;
    }
    /* [弹窗随转·逻辑域可见范围 2026-10-22] avail 是屏对象自身的未旋转
       矩形，而 g/弹层几何是 opener 逻辑全局域——90/270 两域轴对调，原
       样钳位即混域：1024x600 面板转竖屏后拿 avail(1024x600) 管
       600x1024 逻辑域，下半屏 belowSpace=600-g.y 变负无谓翻面、弹高被
       钳到 600-comboY（可见逻辑域实际 1024-comboY）、横向钳位恒不生
       效。屏缺席时回退显示驱动面板框（xcombo_fallbackScreenRect）。取
       得未旋转矩形后按 opener 顶层有效旋转角换轴（旋转只对调宽高、原
       点不随转，XWindowRotation.h 口径；与 demo wn_orientApply 的 R2
       钳位同源），得「当前窗口逻辑域可见范围」再参与翻面/钳高。弹层
       旋转继承 opener（XWindow show 路径重继承），换轴依据即 opener
       角；rot==0 恒等零扰动。 */
    screen = XGuiApplication_screenAt(&g);
    if (!screen)
        screen = XGuiApplication_primaryScreen();
    haveAvail = false;
    if (screen)
    {
        avail = XScreen_availableGeometry(screen);
        haveAvail = true;
    }
    else
    {
        haveAvail = xcombo_fallbackScreenRect(&avail);
    }
    if (haveAvail)
    {
        if (XWindow_rotationSwapsAxes(
                XWidget_contentRotationDegrees(
                    XWidget_topLevelWidget((XWidget*)self))))
        {
            int t = avail.width;
            avail.width = avail.height;
            avail.height = t;
        }
        belowSpace = avail.y + avail.height - g.y;
        aboveSpace = g.y - XWidget_height((XWidget*)self) - avail.y;
        /* 高度收缩：优先下方；下方不足且上方更宽裕则翻到上方。 */
        if (wantH > belowSpace)
        {
            if (aboveSpace > belowSpace)
            {
                int fitH = wantH;
                int minH = xcombo_popupItemHeight(self) + 2;
                if (fitH > aboveSpace) fitH = aboveSpace;
                if (fitH < minH) fitH = minH;
                g.y = g.y - XWidget_height((XWidget*)self) - fitH;
                wantH = fitH;
            }
            else if (belowSpace >= xcombo_popupItemHeight(self) + 2)
            {
                wantH = belowSpace;
            }
            /* 上下都放不下一行：保持下方原位（交由窗口裁剪，同旧口径）。 */
        }
        if (g.x < avail.x) g.x = avail.x;
        if (g.y < avail.y) g.y = avail.y;
    }
    /* [Qt 对齐 2026-11-11] 弹层宽走虚槽（基类=控件宽；XFontComboBox 的
       Qt 口径 min(控件宽×5/3, 屏宽) 由重载给出，qfontcombobox.cpp:526-537）。
       超屏时钳到可用区宽并把 x 收回可用区右界内。 */
    {
        int popupW = XComboBox_popupWidth(self);
        if (popupW < 1) popupW = XWidget_width((XWidget*)self);
        if (haveAvail) {
            if (popupW > avail.width) popupW = avail.width;
            if (popupW < 1) popupW = 1;
            if (g.x + popupW > avail.x + avail.width)
                g.x = avail.x + avail.width - popupW;
            if (g.x < avail.x) g.x = avail.x;
        }
        XRect_init(&r, g.x, g.y, popupW, wantH);
    }
    {
        /* 诊断探针（XGUI_ROT_DEBUG=1 开启；弹层逻辑域换轴验收用，
         * stderr，与 XWindow.c 探针同模式，收口保留环境门控）。 */
        static signed char dbg = -1;
        if (dbg < 0) dbg = (getenv("XGUI_ROT_DEBUG") != NULL);
        if (dbg)
            fprintf(stderr,
                    "[ROT] combo popup: rot=%d screen=%p haveAvail=%d "
                    "avail=(%d,%d %dx%d) "
                    "g=(%d,%d) rect=(%d,%d %dx%d)\n",
                    XWidget_contentRotationDegrees(
                        XWidget_topLevelWidget((XWidget*)self)),
                    (void*)screen, (int)haveAvail,
                    avail.x, avail.y, avail.width, avail.height,
                    g.x, g.y, r.x, r.y, r.width, r.height);
    }
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
    /* [弹窗随转·opener 焦点 2026-10-18] 弹出前置 combo 持焦（对齐 Qt：
       QComboBox 点击弹出后自身持焦点，qcombobox.cpp popup 打开时
       d->comboBox()->setFocus()）：本控件默认 NoFocus（init :3606，
       不进 Tab 序），press 集中移焦不生效——弹层建柄（下方
       XWidget_show）的 owner 解析在 focusWindow（X11 平台层焦点依赖
       WM FocusIn，Xvfb/无 WM 恒缺）与 appFocusWidget（控件层焦点）
       两路全空时放弃 owner，连锁=弹层不继承 opener 内容旋转角（重开
       恒持首开角）且 Z 序失保护。显式 setFocus 直写控件层焦点，不改
       Tab 序（NoFocus 策略位保持），建柄 owner 解析的 appFocusWidget
       回退（XWidget_createWindow 弹窗随转·owner 兜底）由此接通。 */
    XWidget_setFocus((XWidget*)self);
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
        self->m_grabTimer = XObject_startTimer(
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
       currentRow 跟随）。高亮行变化成对发射 highlighted 对（Qt：
       弹层视图 currentChanged → emitHighlighted，qcombobox.cpp:1076）。 */
    {
        int prevRow = self->m_completionRow;
        if (self->m_completionRow < 0 ||
            self->m_completionRow >= self->m_itemCount ||
            XListView_isRowHidden(view, self->m_completionRow))
            self->m_completionRow = (int16_t)first;
        XAbstractItemView_setCurrentIndex((XAbstractItemView*)view,
                                          self->m_completionRow,
                                          self->m_modelColumn);
        if (self->m_completionRow != prevRow)
            xcombo_emitHighlightedPair(self, self->m_completionRow);
    }
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
    self->m_completionRow = (int16_t)row;
    XAbstractItemView_setCurrentIndex((XAbstractItemView*)view, row,
                                      self->m_modelColumn);
    /* 对标 Qt：补全弹层（与全量弹层同一容器视图）的 currentChanged →
       emitHighlighted（qcombobox.cpp:1076）——键盘移动高亮行同样成对
       发射 highlighted/textHighlighted。 */
    xcombo_emitHighlightedPair(self, row);
    XWidget_update((XWidget*)view);
    /* 补主动上屏：弹层为独立顶层窗口无宿主帧泵，异步 PAINT 不保证
       上屏（复扫-5 N2 残2 同家族；先例 xcombo_popupReposition）。 */
    XWidget_flushBackingStore((XWidget*)view, NULL);
}

void XComboBox_showPopup(XComboBox* self)
{
    /* 对标 QComboBox::showPopup（public virtual，qcombobox.cpp:2621）：
     * 公共入口即基类实现；子类按 Qt 惯例只重载 `showPopup` 名下的行为
     * （本库以 `_base` 承载基类链）。 */
    XComboBox_showPopup_base(self);
}

void XComboBox_hidePopup(XComboBox* self)
{
    /* 对标 QComboBox::hidePopup（public virtual，qcombobox.cpp:2866）。 */
    XComboBox_hidePopup_base(self);
}

void XComboBox_showPopup_base(XComboBox* self)
{
    XListView* view;
    int rows;
    if (!self || self->m_popupVisible) return;
    /* 对标 Qt 6.8 qcombobox.cpp:2621-2625 `if (count() <= 0) return;`：
     * 无条目时不建弹层（本实现此前对空组合框也弹一行空列表）。 */
    if (self->m_itemCount <= 0) return;
    view = XComboBox_view(self);
    if (!view) return;
    xcombo_syncModel(self);
    /* 全量弹出前清残留过滤（补全收起时已清，此处兜底）：弹层为全量态。 */
    xcombo_clearRowHidden(self);
    self->m_completionActive = false;
    self->m_completionRow = -1;
    rows = self->m_itemCount < self->m_maxVisibleItems
        ? self->m_itemCount
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
        XClassDelete((XClass*)self->m_itemIcons[index]);
    self->m_itemIcons[index] = repl;
    /* [P3-56] 旧解码图缓存即失效（重解码在下次取用），弹层装饰即时
       重放（装饰表借指针，不得留旧图）；NULL 路径同样走重放清旧装饰。 */
    if (self->m_itemImages && self->m_itemImages[index]) {
        XClassDelete((XClass*)self->m_itemImages[index]);
        self->m_itemImages[index] = NULL;
    }
    if (self->m_popupView)
        XAbstractItemView_setItemDecoration(
            (XAbstractItemView*)self->m_popupView, index, self->m_modelColumn,
            xcombo_itemImageAt(self, index));
    /* [P0-04 Qt 对齐] 图标变更属内容变化（Qt 走 DecorationRole
     * dataChanged，qcombobox.cpp:1084-1096）：仅 AdjustToContents 档
     * 失效重算。 */
    if (self->m_sizeAdjustPolicy ==
        (uint8_t)XComboBoxSizeAdjustPolicy_AdjustToContents) {
        self->m_sizeHintValid = false;
        xcombo_updateSizeHint(self);
    }
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
    if (tmp) XClassDelete((XClass*)tmp);
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
        XClassDelete((XClass*)self->m_itemData[index]);
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
    if (tmp) XClassDelete((XClass*)tmp);
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
    if (!self || !data) return -1;
    /* 等价 findData(data, UserRole, MatchExactly|MatchCaseSensitive)（Qt 缺省）。 */
    return XComboBox_findData_3(self, XString_toUtf8(data),
                                (unsigned)(XComboBoxMatchExactly |
                                           XComboBoxMatchCaseSensitive));
}
int XComboBox_findData_2(const XComboBox* self, const char* data)
{
    return XComboBox_findData_3(self, data,
                                (unsigned)(XComboBoxMatchExactly |
                                           XComboBoxMatchCaseSensitive));
}
int XComboBox_findData_3(const XComboBox* self, const char* data,
                         unsigned flags)
{
    int i;
    if (!self || !data) return -1;
    /* 项数据为组合框本地平行数组（模型无 UserRole 承载，见
       XComboBox_setModel 注记）；匹配口径与 findText_3 同源。 */
    for (i = 0; i < self->m_itemCount; ++i) {
        if (!self->m_itemData || !self->m_itemData[i]) continue;
        if (xcombo_matchUtf8(XString_toUtf8(self->m_itemData[i]), data, flags))
            return i;
    }
    return -1;
}

#if XTABLEWIDGET_ON
/** @brief 补全器采纳联动（[P0-54] 对标 QComboBoxPrivate::completerActivated，
 *  qcombobox.cpp:1246-1273）：按采纳文本命中条目 → 行编辑回填 + 置当前项
 *  + 发射 activated/textActivated 对 + 收下拉弹层（复用
 *  xcombo_completionAccept 口径）。文本未命中条目表时不置当前项（补全器
 *  自备模型的词条不属于本组合框，Qt 的 findText 回落同返回 -1 不动）。 */
static void xcombo_completerActivatedSlot(XObject* receiver, XVarList* args)
{
    XComboBox* self = (XComboBox*)receiver;
    int row;
    if (!self || !self->m_editable || !self->m_lineEdit) return;
    if (!args) return;
    XVarList_args_1(args, XString*, text);
    if (!text) return;
    row = xcombo_findTextMatch(self, XString_toUtf8(text));
    if (row < 0) return;
    XComboBox_setEditText_2(self, XComboBox_itemText_2(self, row));
    XComboBox_setCurrentIndex(self, row);
    xcombo_emitActivatedPair(self, row);
    XComboBox_hidePopup_base(self);
}
#endif /* XTABLEWIDGET_ON */

void XComboBox_setCompleter(XComboBox* self, XCompleter* completer)
{
    XCompleter* old;
    if (!self) return;
    /* [P0-54 Qt 对齐] 非可编辑/无行编辑直接忽略（qcombobox.cpp:1953-1956
     * 的 `if (!d->lineEdit) { qWarning(...); return; }` 静默等价）。 */
    if (!self->m_editable || !self->m_lineEdit) return;
    old = XLineEdit_completer(self->m_lineEdit);
    if (old == completer) return;
#if XTABLEWIDGET_ON
    /* 替换/解除：摘除旧补全器的 activated 联动（XObject_connect_1 不去重，
       先断后连防叠加）并清其对本组合框的关联位（借用卫生）。 */
    if (old) {
        XObject_disconnect((XObject*)old,
                             (size_t)XCompleter_activated_signal(old, NULL),
                             (XObject*)self, xcombo_completerActivatedSlot);
        if (XCompleter_widget(old) == (XWidget*)self)
            XCompleter_setWidget(old, NULL);
    }
    /* [P0-54] 行编辑承载补全链路（前缀过滤/补全弹层/键导航），等价
     * Qt d->lineEdit->setCompleter(c)；NULL 同样经此清除（解除必须真的
     * 摘掉行编辑指针——completer() 从行编辑派生，否则解除无效）。 */
    XLineEdit_setCompleter(self->m_lineEdit, completer);
    if (completer) {
        /* 对标 Qt：补全器无模型时接通组合框条目模型（安装时
           completer->setModel(d->model) 承载，qcombobox.cpp:1957）。 */
        if (!XCompleter_model(completer))
            XCompleter_setModel(completer, XComboBox_model(self));
        /* 对标 Qt：activated → 置当前项 + 发射 activated 对。 */
        XObject_connect((XObject*)completer,
                          (size_t)XCompleter_activated_signal(completer, NULL),
                          (XObject*)self, xcombo_completerActivatedSlot,
                          XConnectionType_Direct);
        /* 对标 Qt 末尾 c->setWidget(this)（qcombobox.cpp:1960-1961）：行
           编辑安装先置行编辑，此处覆盖为组合框——补全弹层锚到组合框。 */
        XCompleter_setWidget(completer, (XWidget*)self);
        /* completerMode 降级承载（见头注）：安装即置位；completer 在装
           期间内建前缀过滤停用（xcombo_editTextEditedSlot 闸），避免
           双层弹层。setCompleter(NULL) 不动本开关（显式意图保留）。 */
        self->m_completerMode = true;
    }
#else
    (void)old;
    (void)completer;
#endif /* XTABLEWIDGET_ON */
}
XCompleter* XComboBox_completer(const XComboBox* self)
{
    /* [P0-54 Qt 对齐] 读内嵌行编辑（qcombobox.cpp:1973-1977）——
     * 单一事实源在行编辑控制器，本组合框不再存第二份。 */
    if (!self || !self->m_lineEdit) return NULL;
    return XLineEdit_completer(self->m_lineEdit);
}

void XComboBox_setIconSize(XComboBox* self, int size)
{
    int i;
    if (!self || size <= 0 || size == (int)self->m_iconSize) return;
    self->m_iconSize = (int16_t)size;
    /* [P3-56] 解码缓存按旧尺寸落盘 ⇒ 全清（懒重解码），装饰立即重放。 */
    if (self->m_itemImages) {
        for (i = 0; i < self->m_itemCount; ++i) {
            if (self->m_itemImages[i]) {
                XClassDelete((XClass*)self->m_itemImages[i]);
                self->m_itemImages[i] = NULL;
            }
        }
    }
    xcombo_syncDecorations(self);
    /* [P0-04 Qt 对齐] setIconSize 无条件失效重算（qcombobox.cpp:1685-1691：
     * view()->setIconSize(size); d->sizeHint = QSize(); updateGeometry()）。 */
    self->m_sizeHintValid = false;
    xcombo_updateSizeHint(self);
    XWidget_update((XWidget*)self);
}
int XComboBox_iconSize(const XComboBox* self)
{ return self ? self->m_iconSize : 0; }

#endif /* XWIDGET_ON && XCOMBOBOX_ON && XLINEEDIT_ON */

#endif /* XGUI_ON */
