#if XGUI_ON

#include "XErrorMessage.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XPainter.h"
#include "XGuiConfig.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XFont.h"
#include "XFont8x16.h"
#include "XWidget_Protected.h"
#if XFRAME_ON && XLABEL_ON
#include "XLabel.h"  /* P0-E8：横幅文本标签（AutoText 富文本子集）。 */
#include "XPalette.h"/* 警示文字色（深红）经调色板下发标签。 */
#endif

#if XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON

/* ==================== 字模度量（P0 固定尺寸链路配套） ==================== */

/** @brief 对象字模行高（字库位图行高 × 像素缩放；XLabel label_lineHeight
 *         同源口径：scale = pixelSize>0 ? pixelSize : 位图行高）。 */
static int xerr_fontLineHeight(const XErrorMessage* self)
{
    XFont font;
    const XFontFace* face;
    XFontFaceInfo info;
    int base = XFONT8X16_HEIGHT;
    int scaleNum;
    if (!self) return base;
    font = XWidget_font((XWidget*)self);
    XMemset(&info, 0, sizeof(info));
    face = XFont_face(&font);
    if (face && XFontFace_info_base(face, &font, &info) &&
        info.m_kind == XFontFace_Bitmap && info.m_bitmap.m_height > 0)
        base = info.m_bitmap.m_height;
    scaleNum = XFont_pixelSize(&font) > 0 ? XFont_pixelSize(&font) : base;
    XClassDeinit(&font);
    /* 行高 = 位图行高 × (pixelSize / 位图行高) = 像素字号（恒 ≥1）。 */
    return scaleNum < 1 ? 1 : scaleNum;
}

/** @brief 按对象字模测 UTF-8 单行文本宽（XPainter_textWidthRange 同源）。 */
static int xerr_textWidth(const XErrorMessage* self, const char* utf8)
{
    XFont font;
    int width;
    if (!self || !utf8 || !utf8[0]) return 0;
    font = XWidget_font((XWidget*)self);
    width = XPainter_textWidthRange(&font, utf8, 0, (int)XStrlen(utf8));
    XClassDeinit(&font);
    return width > 0 ? width : 0;
}

/** @brief 内容驱动尺寸收口（P0 消息族口径：QErrorMessage 为内容驱动
 *         的对话框，但**不设固定尺寸**——Qt 6.8.3 无 min/max 约束、
 *         可任意拉伸（本机探针：minimumWidth/Height==0、
 *         maximumWidth/Height==16777215，resize(700,500) 生效），
 *         故只按内容求"首选尺寸"并 resize，不 setFixedSize、不补
 *         MSWindowsFixedSizeDialogHint（r2 对齐，账本 229）。
 *  @details 横幅单行模型：宽 = max(300, 左强调条让位 4 + 文本边距
 *           12 + 文本实测宽 + 右边距 12)；高 = max(40, 行高 + 12) +
 *           XDialog_decorationTopOffset——CSD 平台装饰条占客户区顶部，
 *           窗高不同步追加则横幅可视区被装饰条压住（文本绘制已按装
 *           饰高避让，见 VX_errMsg_paintEvent）。300x40 下限是内容
 *           首选宽的下沿（旧默认 100x30 仅容 ~6 汉字且垂直裁切）；
 *           用户可拖大/拖小，后续文本变化只按 expandedTo 增长。 */
static void xerr_updateSize(XErrorMessage* self)
{
    int width;
    int height;
    const char* utf8;
    if (!self) return;
    utf8 = (self->m_message && XString_toUtf8(self->m_message))
               ? XString_toUtf8(self->m_message)
               : "";
    width = xerr_textWidth(self, utf8) + 4 + 12 + 12;
    if (width < 300) width = 300;
    height = xerr_fontLineHeight(self) + 12;
    if (height < 40) height = 40;
    height += XDialog_decorationTopOffset((const XDialog*)self);
    /* 对标 Qt 的布局约束（SetDefaultConstraint）：内容变大时按
     * expandedTo 增长，内容变小时不回缩（可见期用户拖出的尺寸被尊重；
     * 不可见期当前尺寸是构造缺省，不参与取大以免钉死）。 */
    if (XWidget_isVisible((XWidget*)self)) {
        if (XWidget_width((XWidget*)self) > width)
            width = XWidget_width((XWidget*)self);
        if (XWidget_height((XWidget*)self) > height)
            height = XWidget_height((XWidget*)self);
    }
    XWidget_resize((XWidget*)self, width, height);
}

/** @brief 首选尺寸虚槽（按内容重算，语义等价 Qt 的布局驱动 sizeHint）。
 *  @details Qt 的 QErrorMessage 未重写 sizeHint，其 QGridLayout 的
 *           totalSizeHint 由内容决定（本机实测空消息 326x159）；本库
 *           无 QLayout，按同一内容模型给出首选尺寸，只作提示、不约束
 *           实际几何（与 Qt 一致可拉伸）。 */
static XSize VX_errMsg_sizeHintSlot(const XWidget* self)
{
    XSize out;
    const XErrorMessage* em = (const XErrorMessage*)self;
    const char* utf8;
    if (!self) { XSize_init(&out, -1, -1); return out; }
    utf8 = (em->m_message && XString_toUtf8(em->m_message))
               ? XString_toUtf8(em->m_message)
               : "";
    XSize_init(&out, xerr_textWidth(em, utf8) + 4 + 12 + 12,
               xerr_fontLineHeight(em) + 12);
    if (out.width < 300) out.width = 300;
    if (out.height < 40) out.height = 40;
    return out;
}

static void VX_errMsg_paintEvent(XWidget* self, XEvent* event)
{
    XErrorMessage* em = (XErrorMessage*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r;
    uint32_t text;
    if (!em || !event) return;
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
    r.x = 0; r.y = 0;
    r.width = XWidget_width(self);
    r.height = XWidget_height(self);
    /* 对标 Qt 错误级视觉惯例（Qt 无独立 ErrorMessage 样式，取
     * qmessagebox.cpp Critical 分级红 + QWidget 样式表错误横幅
     * 惯例；按台账 #56 自定义轻量警示皮肤）：浅红警示底色带 +
     * 左侧深红强调条 + 深红文字。文本仍画于 (12, h/2)，布局与
     * 可读性不变；XErrorMessage 派生自 XDialog（非 XLabel），
     * 不走 #9 的 autoFillBackground 背景角色回填口径，警示底色
     * 由本绘制槽直接承担。 */
    XPainter_fillRect(&painter, &r, 0xFFFFEBEEu);
    {
        XRect bar;
        XRect_init(&bar, 0, 0, 4, r.height);
        XPainter_fillRect(&painter, &bar, 0xFFD32F2Fu);
    }
    /* 深红警示文字（浅红底上对比度可辨）。文本垂直居中于「装饰条以下
     * 的可视区」：CSD 平台装饰条画在客户区顶部，窗高已含装饰让位
     * （xerr_updateSize），中点取装饰高以下区段，避免文本被条压住。
     * P0-E8：有内建 XLabel 时文本由标签自绘（AutoText 富文本子集，
     * 含 `<b>x</b>` 加粗；标签是子控件，子控件在父控件之后绘制），
     * 此处不再自绘，避免双画。 */
    text = 0xFFB71C1Cu;
#if XFRAME_ON && XLABEL_ON
    if (!em->m_textLabel)
#endif
    {
        if (em->m_message && XString_toUtf8(em->m_message) &&
            XString_toUtf8(em->m_message)[0])
            XPainter_drawText(&painter, 12,
                              XDialog_decorationTopOffset((const XDialog*)em) +
                                  (r.height -
                                   XDialog_decorationTopOffset(
                                       (const XDialog*)em)) / 2,
                              XString_toUtf8(em->m_message), text);
    }
    XPainter_deinit(&painter);
}

/** @brief 释放一条待显示队列项（消息 + 类型成对出队并释放）。
 *  @note  必须先取**值**再移除：`XVector_remove_base` 会把后继元素前移，
 *         元素槽地址随之改写——先 remove 再从原槽读指针会读到下一条的
 *         指针（首条泄漏、次条双释放；menu 族 apitest 在根控件析构时
 *         实锅段错误）。 */
static void xerr_pendingDrop(XErrorMessage* self)
{
    XString* msg = NULL;
    XString* type = NULL;
    XString** msgp;
    XString** typep;
    if (!self || !self->m_pendingMessages || !self->m_pendingTypes) return;
    if (XVector_size_base((const XContainer*)self->m_pendingMessages) <= 0)
        return;
    msgp = (XString**)XVector_at_base(self->m_pendingMessages, 0);
    typep = (XString**)XVector_at_base(self->m_pendingTypes, 0);
    if (msgp) msg = *msgp;
    if (typep) type = *typep;
    XVector_remove_base(self->m_pendingMessages, 0, 1);
    XVector_remove_base(self->m_pendingTypes, 0, 1);
    if (msg) XClassDelete((XClass*)msg);
    if (type) XClassDelete((XClass*)type);
}

/** @brief 队列追加一条（消息/类型各深拷贝一份；调用方保证非 NULL）。 */
static void xerr_pendingPush(XErrorMessage* self, const char* msg,
                             const char* type)
{
    XString* msgCopy;
    XString* typeCopy;
    if (!self || !self->m_pendingMessages || !self->m_pendingTypes) return;
    msgCopy = XString_create_utf8(msg ? msg : "");
    typeCopy = XString_create_utf8(type ? type : "");
    if (!msgCopy || !typeCopy) {
        if (msgCopy) XClassDelete((XClass*)msgCopy);
        if (typeCopy) XClassDelete((XClass*)typeCopy);
        return;
    }
    XVector_push_back_1_base(self->m_pendingMessages, &msgCopy);
    XVector_push_back_1_base(self->m_pendingTypes, &typeCopy);
}

/** @brief 展示文本同步到内建标签并摆位（P0-E8）。
 *  @details 标签承载 m_message 的可见文本：文本格式 AutoText ⇒
 *           `<b>x</b>` 等标记按 XLabel 的富文本子集渲染（与
 *           XMessageBox::m_textLabel 同一份实现，不另写解析器；对标
 *           Qt 用 QTextEdit::setHtml，qerrormessage.cpp:363-367）。
 *           几何取「左强调条 4 + 文本边距 12」到右 12 的内区、装饰条
 *           以下的整高（标签 Left|VCenter 对齐 ⇒ 单行垂直居中，与
 *           原自绘 drawText 的 (12, 中点) 同观感）。 */
static void xerr_syncTextLabel(XErrorMessage* self)
{
#if XFRAME_ON && XLABEL_ON
    int deco;
    int left = 12;
    int availW;
    int availH;
    const char* utf8;
    if (!self || !self->m_textLabel) return;
    utf8 = (self->m_message && XString_toUtf8(self->m_message))
               ? XString_toUtf8(self->m_message)
               : "";
    XLabel_setText_2(self->m_textLabel, utf8);
    deco = XDialog_decorationTopOffset((const XDialog*)self);
    availW = XWidget_width((XWidget*)self) - left - 12;
    availH = XWidget_height((XWidget*)self) - deco - 8;
    if (availW < 0) availW = 0;
    if (availH < 0) availH = 0;
    XWidget_setGeometry((XWidget*)self->m_textLabel, left, deco + 4,
                        availW, availH);
#else
    (void)self;
#endif
}

static void VXErrorMessage_deinit(XErrorMessage* self)
{
    if (!self) return;
    if (self->m_message) {
        XClassDelete(self->m_message);
        self->m_message = NULL;
    }
    if (self->m_currentMessage) {
        XClassDelete(self->m_currentMessage);
        self->m_currentMessage = NULL;
    }
    if (self->m_currentType) {
        XClassDelete((XClass*)self->m_currentType);
        self->m_currentType = NULL;
    }
    if (self->m_doNotShow) {
        XClassDelete((XClass*)self->m_doNotShow);
        self->m_doNotShow = NULL;
    }
    if (self->m_doNotShowType) {
        XClassDelete((XClass*)self->m_doNotShowType);
        self->m_doNotShowType = NULL;
    }
    if (self->m_pendingMessages) {
        while (XVector_size_base((const XContainer*)self->m_pendingMessages) > 0)
            xerr_pendingDrop(self);
        XClassDelete((XClass*)self->m_pendingMessages);
        self->m_pendingMessages = NULL;
    }
    if (self->m_pendingTypes) {
        XClassDelete((XClass*)self->m_pendingTypes);
        self->m_pendingTypes = NULL;
    }
    /* P0-E8 的横幅标签是子控件：随父子链级联释放（XMessageBox 对
     * m_textLabel 同口径），此处只清指针不留悬垂。 */
#if XFRAME_ON && XLABEL_ON
    self->m_textLabel = NULL;
#endif
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

/** @brief 显示事件：按当前装饰/文本态重算首选尺寸（对标 Qt 布局在
 *         显示时的尺寸收敛；不设固定尺寸，见 xerr_updateSize 注）。 */
static void VXErrorMessage_showEvent(XWidget* self, XEvent* event)
{
    if (self && event && XEvent_type(event) == XEVENT_TYPE_SHOW) {
        xerr_updateSize((XErrorMessage*)self);
        xerr_syncTextLabel((XErrorMessage*)self);
    }
    XClass_Parent(XDialog, EXWidget_ShowEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief 尺寸变化：内建标签几何随内区重排（P0-E8）。 */
static void VXErrorMessage_resizeEvent(XWidget* self, XEvent* event)
{
    (void)event;
    if (self)
        xerr_syncTextLabel((XErrorMessage*)self);
}

XVtable* XErrorMessage_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XErrorMessage)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_errMsg_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXErrorMessage_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXErrorMessage_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_SizeHint, VX_errMsg_sizeHintSlot);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXErrorMessage_deinit);
    return XVTABLE_DEFAULT;
}

void XErrorMessage_init(XErrorMessage* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    /* Qt 6.8.3 QErrorMessage 无固定尺寸约束（无 min/max、可拉伸）；
     * 窗口类型位不在此叠加——XDialog_init 对无类型位叠加 Dialog，
     * 尊重调用方（r2 对齐：移除 r1 的 MSWindowsFixedSizeDialogHint，
     * 账本 229）。 */
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XErrorMessage);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_doneShown = true;
    self->m_message = XString_create();
    self->m_currentMessage = XString_create();
    self->m_currentType = XString_create();
    self->m_doNotShow = XStringList_create();
    self->m_doNotShowType = XStringList_create();
    self->m_pendingMessages = XVector_Create(XString*);
    self->m_pendingTypes = XVector_Create(XString*);
#if XFRAME_ON && XLABEL_ON
    /* P0-E8：横幅文本由内建 XLabel 承载（AutoText ⇒ 富文本子集；
     * 与 XMessageBox::m_textLabel 同款：子控件、鼠标穿透、警示文字色
     * 经调色板下发以保持原深红观感）。 */
    self->m_textLabel = XLabel_create((XWidget*)self, 0);
    if (self->m_textLabel) {
#if XPALETTE_ON
        XPalette palette;
#endif
        XWidget_setAttribute((XWidget*)self->m_textLabel,
                             XWidgetAttribute_TransparentForMouseEvents, true);
        XLabel_setTextFormat(self->m_textLabel, XLabelTextFormat_AutoText);
#if XPALETTE_ON
        /* 警示文字深红（XPalette 是无堆资源值类型，按值取副本改色再
         * 回写即可，无需释放）。 */
        palette = XWidget_palette((XWidget*)self->m_textLabel);
        XPalette_setColor(&palette, XPaletteColorGroup_Active,
                          XPaletteColorRole_WindowText,
                          XColor_create_rgb(0xB7, 0x1C, 0x1C, 0xFF));
        XWidget_setPalette((XWidget*)self->m_textLabel, &palette);
#endif
    }
#endif
    /* 账本 #5 根修：创建即按内容收口首选尺寸（XWidget_init 的有父分
     * 支预置子控件默认 100x30 曾让横幅只剩 30px 高、100px 宽），显示
     * 时 showEvent 再按真实装饰态重算。 */
    xerr_updateSize(self);
}

XErrorMessage* XErrorMessage_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XErrorMessage* self = (XErrorMessage*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XErrorMessage_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== "不再显示" 抑制链（对标 QErrorMessagePrivate
 *                     isMessageToBeShown / doNotShow / doNotShowType） ==== */

/** @brief 抑制表含指定文本（空表安全）。 */
static bool xerr_contains(const XStringList* list, const char* text)
{
    int64_t i;
    int64_t n;
    if (!list || !text || !text[0]) return false;
    n = XStringList_size_base((const XContainer*)list);
    for (i = 0; i < n; ++i) {
        const XString* e = (const XString*)
            XStringList_at_base((const XVector*)list, i);
        if (e && XString_toUtf8(e) &&
            XStrcmp(XString_toUtf8(e), text) == 0)
            return true;
    }
    return false;
}

/** @brief 抑制表追加文本（去重）。 */
static void xerr_append(XStringList* list, const char* text)
{
    XString* copy;
    if (!list || !text || !text[0]) return;
    if (xerr_contains(list, text)) return;
    copy = XString_create_utf8(text);
    if (!copy) return;
    XStringList_push_back_move_base((XVector*)list, copy);
    /* _move 只转移内容，源壳仍归调用方（AGENTS 所有权命名铁律）；本处
     * copy 是 XString_create_utf8 的堆对象，不回收即每追加一条泄漏
     * sizeof(XString)（对齐测试实测 +104 字节/轮，复核遍仍增长）。 */
    XClassDelete((XClass*)copy);
}

/** @brief 该消息是否应显示（对标 QErrorMessagePrivate::
 *         isMessageToBeShown，qerrormessage.cpp:343-347）：
 *         `return !message.isEmpty() && (type.isEmpty() ?
 *          !doNotShow.contains(message) : !doNotShowType.contains(type))`。
 * @note  **空串消息直接短路**（既不显示也不入队）——r2 对齐 Qt 原文
 *         与实测（本机 showMessage("") 后 visible==0、文本不变）；r1 的
 *         "空串参与显示" 偏差与既有回归锁（xgui_demo_apitest_menus.c
 *         的空串清消息断言）一并撤销，见 testMigrations。
 *         NULL 仍为无操作（同 apitest「showMessage(NULL) 为无操作」锁）。 */
static bool xerr_isMessageToBeShown(const XErrorMessage* self,
                                    const char* msg, const char* type)
{
    if (!self || !msg || !msg[0]) return false;
    if (type && type[0])
        return !xerr_contains(self->m_doNotShowType, type);
    return !xerr_contains(self->m_doNotShow, msg);
}

/** @brief 弹出队首可显示消息（对标 QErrorMessagePrivate::nextPending，
 *         qerrormessage.cpp:349-366）。
 *  @details 逐条出队：被抑制的丢弃，第一条可显示的写入横幅文本 /
 *           currentMessage / currentType 并复位"再次显示"复选框
 *           （Qt 在 nextPending 里 again->setChecked(true)）。队列空或
 *           全部被抑制返回 false。
 *  @param self 目标控件；NULL 返回 false。
 *  @return 弹出一条可显示消息返回 true。
 */
static bool xerr_nextPending(XErrorMessage* self)
{
    if (!self || !self->m_pendingMessages || !self->m_pendingTypes)
        return false;
    while (XVector_size_base((const XContainer*)self->m_pendingMessages) > 0) {
        XString** msgp = (XString**)XVector_at_base(self->m_pendingMessages, 0);
        XString** typep = (XString**)XVector_at_base(self->m_pendingTypes, 0);
        const char* msg = (msgp && *msgp) ? XString_toUtf8(*msgp) : "";
        const char* type = (typep && *typep) ? XString_toUtf8(*typep) : "";
        bool shown = xerr_isMessageToBeShown(self, msg, type);
        if (!shown) {
            xerr_pendingDrop(self);
            continue;
        }
        if (!self->m_message) self->m_message = XString_create();
        if (self->m_message) XString_assign_utf8(self->m_message, msg);
        /* P0-E8：新横幅文本同步进内建标签（AutoText 富文本子集）。 */
        xerr_syncTextLabel(self);
        if (!self->m_currentMessage) self->m_currentMessage = XString_create();
        if (self->m_currentMessage) XString_assign_utf8(self->m_currentMessage, msg);
        if (!self->m_currentType) self->m_currentType = XString_create();
        if (self->m_currentType) XString_assign_utf8(self->m_currentType, type);
        /* 对标 Qt nextPending 的 again->setChecked(true)：每条新消息
         * 显示时"再次显示"复选框复位为勾选（不抑制）。 */
        self->m_doneShown = true;
        xerr_pendingDrop(self);
        return true;
    }
    return false;
}

void XErrorMessage_showMessage(XErrorMessage* self, const char* msg)
{
    XErrorMessage_showMessage_2(self, msg, "");
}

void XErrorMessage_showMessage_2(XErrorMessage* self, const char* msg,
                                 const char* type)
{
    if (!self || !msg) return;
    /* 对标 QErrorMessage::showMessage(message, type)
     * （qerrormessage.cpp:398-405）：
     *   ① 先过 isMessageToBeShown（**入队前**就过滤，含空串短路）；
     *   ② 入 pending 队列；
     *   ③ 仅当对话框当前不可见时弹出队首并显示——可见期只入队，
     *      横幅文本保持前一条（本机 6.8.3 实测）。 */
    if (!xerr_isMessageToBeShown(self, msg, type)) return;
    xerr_pendingPush(self, msg, type);
    if (!XWidget_isVisible((XWidget*)self)) {
        if (xerr_nextPending(self)) {
            xerr_updateSize(self);
            XWidget_update((XWidget*)self);
            XWidget_show((XWidget*)self);
        }
    }
}

void XErrorMessage_done(XErrorMessage* self, int result)
{
    if (!self) return;
    /* 对标 QErrorMessage::done（qerrormessage.cpp:292-307）：
     * ① 复选框等价位为 false（"不再显示"）时：type 空记消息文本（且
     *    文本非空），type 非空记类型；
     * ② **清空** currentMessage/currentType；
     * ③ 转发 QDialog::done 收口；④ 队列还有可显示消息则重新 show()
     *    并保持可见（横幅换成下一条）。 */
    if (!self->m_doneShown) {
        const char* type = (self->m_currentType &&
                            XString_toUtf8(self->m_currentType))
                               ? XString_toUtf8(self->m_currentType)
                               : "";
        if (!type[0]) {
            const char* cur = (self->m_currentMessage &&
                               XString_toUtf8(self->m_currentMessage))
                                  ? XString_toUtf8(self->m_currentMessage)
                                  : "";
            if (cur[0]) xerr_append(self->m_doNotShow, cur);
        } else {
            xerr_append(self->m_doNotShowType, type);
        }
    }
    if (self->m_currentMessage)
        XString_assign_utf8(self->m_currentMessage, "");
    if (self->m_currentType)
        XString_assign_utf8(self->m_currentType, "");
    XDialog_done(&self->m_base, result);
    if (xerr_nextPending(self)) {
        xerr_updateSize(self);
        XWidget_update((XWidget*)self);
        XWidget_show((XWidget*)self);
    }
}

const char* XErrorMessage_currentMessage(const XErrorMessage* self)
{
    const char* text;
    if (!self || !self->m_currentMessage) return "";
    text = XString_toUtf8(self->m_currentMessage);
    return text ? text : "";
}

void XErrorMessage_setDoneShown(XErrorMessage* self, bool on)
{
    if (!self) return;
    self->m_doneShown = on;
}

bool XErrorMessage_isDoneShown(const XErrorMessage* self)
{
    return self ? self->m_doneShown : false;
}






#endif /* XWIDGET_ON && XDIALOG_ON && XERRORMESSAGE_ON */

#endif /* XGUI_ON */
