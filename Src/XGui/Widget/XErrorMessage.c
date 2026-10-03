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
    XFont_deinit_base(&font);
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
    XFont_deinit_base(&font);
    return width > 0 ? width : 0;
}

/** @brief 内容驱动尺寸收口（P0 消息族口径：QErrorMessage 为内容驱动
 *         的不可拉伸对话框；对标 QMessageBoxPrivate::updateSize 同型，
 *         结果 setFixedSize 固定）。
 *  @details 横幅单行模型：宽 = max(300, 左强调条让位 4 + 文本边距
 *           12 + 文本实测宽 + 右边距 12)；高 = max(40, 行高 + 12) +
 *           XDialog_decorationTopOffset——CSD 平台装饰条占客户区顶部，
 *           窗高不同步追加则横幅可视区被装饰条压住（文本绘制已按装
 *           饰高避让，见 VX_errMsg_paintEvent）。300x40 下限对标 Qt
 *           QErrorMessage 常规默认约 300x40~60 的下沿（账本 #5：旧
 *           默认 100x30 仅容 ~6 汉字且垂直裁切）——消息文本实测宽超
 *           出下限即随文本增长，算小算大都以该锚点判定。 */
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
    XWidget_setFixedSize((XWidget*)self, width, height);
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
     * （xerr_updateSize），中点取装饰高以下区段，避免文本被条压住。 */
    text = 0xFFB71C1Cu;
    if (em->m_message && XString_toUtf8(em->m_message) &&
        XString_toUtf8(em->m_message)[0])
        XPainter_drawText(&painter, 12,
                          XDialog_decorationTopOffset((const XDialog*)em) +
                              (r.height -
                               XDialog_decorationTopOffset(
                                   (const XDialog*)em)) / 2,
                          XString_toUtf8(em->m_message), text);
    XPainter_deinit(&painter);
}

static void VXErrorMessage_deinit(XErrorMessage* self)
{
    if (!self) return;
    if (self->m_message) {
        XString_delete_base(self->m_message);
        self->m_message = NULL;
    }
    XClass_Deinit_Parent(XDialog, (XDialog*)self);
}

/** @brief 显示事件：按当前装饰/文本态重算固定尺寸（P0 消息族链路
 *         收口点，对标 XMessageBox showEvent 末尾 updateSize 同型）。 */
static void VXErrorMessage_showEvent(XWidget* self, XEvent* event)
{
    if (self && event && XEvent_type(event) == XEVENT_TYPE_SHOW)
        xerr_updateSize((XErrorMessage*)self);
    XClass_Parent(XDialog, EXWidget_ShowEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

XVtable* XErrorMessage_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XErrorMessage)
    XVTABLE_INHERIT_XCLASS(XDialog);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_errMsg_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXErrorMessage_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXErrorMessage_deinit);
    return XVTABLE_DEFAULT;
}

void XErrorMessage_init(XErrorMessage* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    /* P0 消息族口径（对标 QErrorMessage 内容驱动不可拉伸；同型先例
     * XMessageBox_init 的 MSWindowsFixedSizeDialogHint）：补固定尺寸
     * 窗口提示位。窗口类型位不在此设——XDialog_init 对无类型位叠加
     * Dialog，尊重调用方。 */
    flags |= (XWidgetFlags)XWindowType_MSWindowsFixedSizeDialogHint;
    XDialog_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XErrorMessage);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_doneShown = true;
    self->m_message = XString_create();
    /* 账本 #5 根修：创建即按内容收口固定尺寸（XWidget_init 的有父分
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

void XErrorMessage_showMessage(XErrorMessage* self, const char* msg)
{
    if (!self || !msg) return;
    if (!self->m_message) self->m_message = XString_create();
    if (self->m_message)
        XString_assign_utf8(self->m_message, msg ? msg : "");
    /* 对标 QMessageBox::setText 末尾 updateSize：文本变化重算固定尺寸
     * （消息文本实测宽参与宽度，见 xerr_updateSize），再显示；显示时
     * showEvent 亦按真实装饰态复算。 */
    xerr_updateSize(self);
    XWidget_show((XWidget*)self);
}

const char* XErrorMessage_currentMessage(const XErrorMessage* self)
{
    const char* text;
    if (!self || !self->m_message) return "";
    text = XString_toUtf8(self->m_message);
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