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
        XClassDelete(self->m_message);
        self->m_message = NULL;
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
    self->m_currentType = XString_create();
    self->m_doNotShow = XStringList_create();
    self->m_doNotShowType = XStringList_create();
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
}

/** @brief 该消息是否应显示（对标 QErrorMessagePrivate::
 *         isMessageToBeShown，qerrormessage.cpp:376-381）：空消息不
 *         显示；type 空按消息文本维度查 doNotShow，type 非空按类型
 *         维度查 doNotShowType。 */
static bool xerr_isMessageToBeShown(const XErrorMessage* self,
                                    const char* msg, const char* type)
{
    if (!self || !msg || !msg[0]) return false;
    if (type && type[0])
        return !xerr_contains(self->m_doNotShowType, type);
    return !xerr_contains(self->m_doNotShow, msg);
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
     * （qerrormessage.cpp:389-402）：已抑制的消息不再弹出、不改当前
     * 态。（Qt 另有 pending 队列承载 qtHandler 的异步日志消息；本库
     * 无消息处理器设施，直接显示为轻量等价，队列未复刻。） */
    if (!xerr_isMessageToBeShown(self, msg, type)) return;
    if (!self->m_message) self->m_message = XString_create();
    if (self->m_message)
        XString_assign_utf8(self->m_message, msg ? msg : "");
    if (!self->m_currentType) self->m_currentType = XString_create();
    if (self->m_currentType)
        XString_assign_utf8(self->m_currentType, type ? type : "");
    /* 对标 QMessageBox::setText 末尾 updateSize：文本变化重算固定尺寸
     * （消息文本实测宽参与宽度，见 xerr_updateSize），再显示；显示时
     * showEvent 亦按真实装饰态复算。 */
    xerr_updateSize(self);
    XWidget_show((XWidget*)self);
}

void XErrorMessage_done(XErrorMessage* self, int result)
{
    if (!self) return;
    /* 对标 QErrorMessage::done（qerrormessage.cpp:283-306）：复选框
     * 等价位为 false（"不再显示"）时，type 空记消息文本、type 非空记
     * 类型；随后框架内不再弹出。Qt 收口后清 currentMessage/currentType
     * 内部态；本库 m_message 兼作横幅显示文本与 currentMessage() 读数，
     * 保留不清（记录）。 */
    if (!self->m_doneShown) {
        const char* type = (self->m_currentType &&
                            XString_toUtf8(self->m_currentType))
                               ? XString_toUtf8(self->m_currentType)
                               : "";
        if (!type[0] && self->m_message && XString_toUtf8(self->m_message)) {
            xerr_append(self->m_doNotShow,
                        XString_toUtf8(self->m_message));
        } else if (type[0]) {
            xerr_append(self->m_doNotShowType, type);
        }
    }
    XDialog_done(&self->m_base, result);
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