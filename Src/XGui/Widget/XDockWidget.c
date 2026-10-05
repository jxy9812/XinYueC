#include "XDockWidget.h"
#include "XStyle.h"
#include "XStyleOption.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiConfig.h"
#include "XAction.h"
#include "XMainWindow_Protected.h"
/* 拖出阈值（对标 QApplication::startDragDistance 的全局拖拽启动距离）。 */
#include "XApplication.h"

#include "XAlgorithm.h"
#include "XStringUtils.h"
#include "XPainter.h"
#include "XTextUtf8.h"
#include "XWidget_Protected.h"

#if XWIDGET_ON && XDOCKWIDGET_ON

/* 拖动跟随节流与调试跟踪共用的毫秒时钟。 */
#include "XDateTime.h"

/* ==================== 调试跟踪（XGUI_DOCK_TRACE，问题关闭后移除） ==================== */

#if XGUI_DOCK_TRACE
#include <stdio.h>
/* 拖放链路逐事件跟踪：毫秒时间戳 + 函数名 + 关键决策值，行末即刷。
 * 移动事件逐条记录——两次跟踪行的时间差即事件环真实节奏，用于定位
 * "停靠操作卡顿数秒"类问题（时间戳断崖 = 冻结窗口）。 */
#define XDW_TRACE(...)                                                    \
    do {                                                                  \
        int64_t xdwTraceMs = XDateTime_currentMSecsSinceEpoch();          \
        printf("[DOCK %lld.%03lld %s] ", (long long)(xdwTraceMs / 1000),  \
               (long long)(xdwTraceMs % 1000), __func__);                 \
        printf(__VA_ARGS__);                                              \
        printf("\n");                                                     \
        fflush(stdout);                                                   \
    } while (0)
#else
#define XDW_TRACE(...) do {} while (0)
#endif

/* ==================== 内部常量 ==================== */

/** @brief 标题条交互高度：绘制 20 像素标题 + 1 像素底部分隔线；
 *         setWidget 的内容区从该高度起（与既有 setWidget/绘制口径一致）；
 *         垂直标题条时同值转为竖条宽度（对标 Qt QDockWidgetLayout
 *         垂直标题区宽 = titleHeight，qdockwidget.cpp:504-506）。 */
#define XDW_TITLE_H 21
/** @brief 标题条右侧关闭按钮命中区宽度（对标 Qt 标题条关闭按钮）。 */
#define XDW_CLOSE_BOX 18
/** @brief 标题文本左边距（手绘路径 drawText 起笔 x；垂直竖排横向
 *         居中口径不受影响）。 */
#define XDW_TITLE_LEFT_PAD 6
/** @brief 标题文本右侧安全余量（省略截断可用宽度口径的一部分）。 */
#define XDW_TITLE_SLACK 8

/** @brief 调用 XWidget 基类事件实现（经 XClass_Parent 取基类虚表槽位）。
 * @note  不可用 XWidget_*_base 入口转发：该入口按对象虚表再分派，会
 *        重新命中本类重载形成自递归（与 XToolBar 的
 *        XClass_Parent 转发同口径）。 */
#define xdw_callParent(self, eventSlot, eventArg)                      \
    XClass_Parent(XWidget, EXWidget_##eventSlot,                      \
                  void (*)(XWidget*, XEvent*))((self), (eventArg))

/* ==================== 内部工具 ==================== */

/** @brief 拖动跟随节流时钟（文件级：同一时刻至多一个拖动会话）。
 *         对标 Qt 鼠标事件压缩：拖动跟随为绝对定位，中间高频移动
 *         事件只需取最新一条落位。 */
static int64_t xdw_lastFollowMs;

/** @brief 宿主销毁槽前向声明（定义见保护接口一节；deinit 先用到）。 */
static void xdw_hostDestroyedSlot(XObject* receiver, XVarList* args);

/** @brief setFloating 实现体前向声明（定义见公共 API 一节；拖出路径
 *         先用到——对标 Qt startDrag→unplug 不消费 undockedGeometry）。 */
static void xdw_setFloatingImpl(XDockWidget* self, bool floating,
                                bool useUndockedGeometry);

static void xdw_emitInt(XDockWidget* self, size_t signal, int value)
{
    XVarList* args = XVarList_Create(XVar(int, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xdw_emitBool(XDockWidget* self, size_t signal, bool value)
{
    XVarList* args = XVarList_Create(XVar(bool, value));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/**
 * @brief      判断面板是否启用垂直标题条。
 * @details    对标 Qt QStyleOptionDockWidget::verticalTitleBar（绘制经
 *             transposed 转置，qcommonstyle.cpp:2171-2179；布局经
 *             QDockWidgetLayout 垂直标题区分支，qdockwidget.cpp:504-510）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     XDockWidgetFeature_VerticalTitleBar 置位返回 true。
 */
static bool xdw_vertical(const XDockWidget* dock)
{
    return dock && (dock->m_features & XDockWidgetFeature_VerticalTitleBar);
}

/**
 * @brief      浮动态是否由原生标题条装饰（nativeDeco 路径）。
 * @details    对标 Qt QDockWidgetLayout::nativeWindowDeco（qdockwidget.cpp:
 *             246-250）：wmSupportsNativeWindowDeco 在 Windows 恒真，故即
 *             "浮动态且无自定义标题条"。此形态下标题由原生工具窗标题承载
 *             （Tool 窗型，见 xdw_setFloatingImpl），控件不绘标题带、不布
 *             局标题区、不保留按钮命中区（qdockwidget.cpp:684-685
 *             hideButtons = nativeDeco）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     浮动且无自定义标题条返回 true。
 */
static XWidget* xdw_titleBar(const XDockWidget* dock);

static bool xdw_nativeDeco(const XDockWidget* dock)
{
    return dock && dock->m_floating && !xdw_titleBar(dock);
}

/**
 * @brief      判断标题条局部坐标是否命中关闭按钮。
 * @details    水平标题条命中右侧 XDW_CLOSE_BOX 条带；垂直标题条对标
 *             Qt 的转置几何（qcommonstyle.cpp:3082-3093 按钮沉到转置
 *             矩形右缘 = 竖条底部）：命中竖条底部 XDW_CLOSE_BOX 条带。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      pos 面板局部坐标；可为 NULL。
 * @return     命中标题条关闭区返回 true。
 */
static bool xdw_closeHit(const XDockWidget* dock, const XPoint* pos)
{
    int w;
    int h;
    if (!dock || !pos) return false;
    /* 原生装饰浮动态无自定义标题带（xdw_nativeDeco），关闭钮由原生
     * 标题条承载，控件侧命中区不复存在（对标 Qt updateButtons 的
     * hideButtons = nativeDeco 分支）。 */
    if (xdw_nativeDeco(dock)) return false;
    if (xdw_vertical(dock)) {
        h = XWidget_height((XWidget*)dock);
        return pos->x >= 0 && pos->x < XDW_TITLE_H &&
               pos->y >= h - XDW_CLOSE_BOX && pos->y < h;
    }
    w = XWidget_width((XWidget*)dock);
    return pos->y >= 0 && pos->y < XDW_TITLE_H &&
           pos->x >= w - XDW_CLOSE_BOX && pos->x < w;
}

/** @brief 判断局部坐标是否落在浮动按钮内（关闭钮左侧一格/垂直条底部
 *         上一格；Floatable 时两种状态都显示，点击切换停靠/浮动）。
 *         垂直命中区与关闭钮同口径转置（对标 qcommonstyle.cpp:3095-3111
 *         close 占右缘、float 占其左一格的转置几何）。 */
static bool xdw_floatHit(const XDockWidget* dock, const XPoint* pos)
{
    int w;
    int h;
    if (!dock || !pos) return false;
    if (xdw_nativeDeco(dock)) return false; /* 原生装饰：按钮归原生标题 */
    if (xdw_vertical(dock)) {
        h = XWidget_height((XWidget*)dock);
        return pos->x >= 0 && pos->x < XDW_TITLE_H &&
               pos->y >= h - XDW_CLOSE_BOX * 2 &&
               pos->y < h - XDW_CLOSE_BOX;
    }
    w = XWidget_width((XWidget*)dock);
    return pos->y >= 0 && pos->y < XDW_TITLE_H &&
           pos->x >= w - XDW_CLOSE_BOX * 2 && pos->x < w - XDW_CLOSE_BOX;
}

/**
 * @brief      判断局部坐标是否落在标题条内。
 * @details    水平标题条取顶部 XDW_TITLE_H 高条带；垂直标题条取左侧
 *             XDW_TITLE_H 宽竖条（与布局/绘制同一口径，见
 *             xdw_layoutTitleBar/xdw_paintVerticalTitle）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      pos 面板局部坐标；可为 NULL。
 * @return     坐标位于标题条带内返回 true。
 */
static bool xdw_titleHit(const XDockWidget* dock, const XPoint* pos)
{
    if (!pos) return false;
    /* 原生装饰浮动态：标题带归原生标题条，客户区按下不启动拖动
     * （浮动窗的移动由原生标题拖拽承担，对标 Qt nativeDeco 形态）。 */
    if (xdw_nativeDeco(dock)) return false;
    if (xdw_vertical(dock))
        return pos->x >= 0 && pos->x < XDW_TITLE_H;
    return pos->y >= 0 && pos->y < XDW_TITLE_H;
}

/**
 * @brief      广播可见性变化并联动宿主与切换动作。
 * @details    对标 Qt：QDockWidget::visibilityChanged 随真实显隐发射，
 *             同时按 QDockWidgetPrivate::syncViewAction 同步
 *             toggleViewAction 的 checked 位；显隐改变停靠区占位时经
 *             宿主回链请求主窗口重排（对标 QMainWindowLayout::update）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      visible 最新生效可见状态。
 * @return     无返回值。
 */
static void xdw_announceVisible(XDockWidget* dock, bool visible)
{
    if (!dock) return;
    if (dock->m_toggleAction)
        XAction_setChecked(dock->m_toggleAction, visible);
    if (dock->m_announcedVisible == visible) return;
    dock->m_announcedVisible = visible;
    xdw_emitBool(dock, (size_t)XDockWidget_visibilityChanged_signal, visible);
    if (dock->m_host)
        XMainWindow_updateDockLayout((XMainWindow*)dock->m_host);
}

/**
 * @brief      把内容控件摆到标题条以下全部区域。
 * @details    对标 Qt：QDockWidget 的内容控件恒填充标题条以外的全部
 *             客户区，停靠/浮动/缩放三态都跟随（QDockWidgetLayout 总是
 *             把 contents 重设为标题栏旁整块）。水平标题条时内容区为
 *             标题条（含分隔线，XDW_TITLE_H）以下整块；垂直标题条时
 *             对标 qdockwidget.cpp:541-543（r.setLeft(_titleArea.right()
 *             + 1)）改为标题竖条右侧整块。尺寸不足标题条时钳位 0（与
 *             既有 setWidget 口径一致）。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     无返回值。
 */
static void xdw_layoutContent(XDockWidget* dock)
{
    XRect r;
    int w;
    int h;
    if (!dock || !dock->m_widget) return;
    w = XWidget_width((XWidget*)dock);
    h = XWidget_height((XWidget*)dock);
    if (xdw_nativeDeco(dock)) {
        /* 原生装饰浮动态：标题由原生标题条承载（框架外），客户区全部
         * 归内容（对标 QDockWidgetLayout 的 !nativeDeco 门——仅无原生
         * 装饰时把标题高度计入内容布局）。 */
        XRect_init(&r, 0, 0, w, h);
    } else if (xdw_vertical(dock)) {
        /* 垂直标题条：内容区在竖条右侧（qdockwidget.cpp:541-543）。 */
        XRect_init(&r, XDW_TITLE_H, 0,
                   w > XDW_TITLE_H ? w - XDW_TITLE_H : 0, h);
    } else {
        XRect_init(&r, 0, XDW_TITLE_H, w,
                   h > XDW_TITLE_H ? h - XDW_TITLE_H : 0);
    }
    XWidget_setGeometryRect(dock->m_widget, &r);
}

/**
 * @brief      读取当前自定义标题条（随宏态分流：父类槽位或自有成员）。
 * @details    XGUI_CUSTOM_TITLEBAR_ON=1 时标题条存于父类控件级槽位，
 *             统一经 XWidget_titleBarWidget 读槽（写入经本类薄包装
 *             setter：父类挂载 + 停靠侧挂载即钉位，同走该槽位）；=0 时
 *             读本类自有成员，行为与改造前逐行等价。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     自定义标题条借用指针；未设置或空指针返回 NULL。
 */
static XWidget* xdw_titleBar(const XDockWidget* dock)
{
#if XGUI_CUSTOM_TITLEBAR_ON
    return XWidget_titleBarWidget((const XWidget*)dock);
#else
    return dock ? dock->m_titleBar : NULL;
#endif
}

/**
 * @brief      把自定义标题条摆满标题条区（复扫 R-83 配套）。
 * @details    对标 Qt QDockWidgetLayout：自定义标题条由面板布局接管，
 *             置于标题条区呈现。水平标题条时标题条区高度恒 XDW_TITLE_H
 *             （与内容区起点同口径），自定义条沿该区铺满宽度；垂直标题
 *             条时对标 qdockwidget.cpp:504-506（_titleArea =
 *             QSize(titleHeight, height - fw*2)）转为左侧 XDW_TITLE_H
 *             宽竖条、铺满全高；控件自身已有有效尺寸（0<有效边<=
 *             XDW_TITLE_H）时尊重其厚度。
 * @param      dock 目标停靠面板；可为 NULL。
 * @return     无返回值。
 */
static void xdw_layoutTitleBar(XDockWidget* dock)
{
    XRect r;
    int w;
    int h;
    XWidget* bar = xdw_titleBar(dock);
    if (!dock || !bar) return;
    w = XWidget_width((XWidget*)dock);
    h = XWidget_height(bar);
    if (h < 1 || h > XDW_TITLE_H) h = XDW_TITLE_H;
    XRect_init(&r, 0, 0, w, h);
    XWidget_setGeometryRect(bar, &r);
}

/**
 * @brief      按可用宽度绘制单行标题，超宽逐字截断补省略号。
 * @details    手绘路径的标题省略（对标 qcommonstyle CE_DockWidgetTitle
 *             的文本呈现：qcommonstyle.cpp:2184-2185 drawItemText，内部
 *             经 QCommonStylePrivate::drawItemText 的 elidedText，
 *             qcommonstyle.cpp:947）。量测用 XPainter_textWidth
 *             （XPainter.h:1273，取绘制器当前字体 XPainter_font）：
 *             整串放得下则原样单次绘制；放不下时按 UTF-8 码点边界
 *             （XTextUtf8_seqLen，绝不切在多字节序列中间）逐字累进、
 *             宽度经 XPainter_textWidthRange 逐级复测，直至"前缀宽 +
 *             省略号宽"超出可用宽为止，补 "..." 输出。
 * @param      painter 目标绘制器（已绑定设备）。
 * @param      x 首字形左上角 X。
 * @param      baselineY 文本基线 Y。
 * @param      utf8 UTF-8 标题文本；NULL/空串直接无操作。
 * @param      avail 可用宽度（像素）。
 * @param      color ARGB32 文本颜色。
 * @return     无返回值。
 */
static void xdw_drawElidedText(XPainter* painter, int x, int baselineY,
                               const char* utf8, int avail, uint32_t color)
{
    const XFont* font;
    int titleLen;
    int dotsW;
    int maxW;
    int keep;
    int len;
    char* buf;
    if (!painter || !utf8 || avail <= 0) return;
    font = XPainter_font(painter);
    if (XPainter_textWidth(font, utf8) <= avail) {
        /* 放得下：整串原样绘制（不触发省略）。 */
        XPainter_drawText(painter, x, baselineY, utf8, color);
        return;
    }
    dotsW = XPainter_textWidth(font, "...");
    maxW = avail - dotsW;
    titleLen = (int)XStrlen(utf8);
    keep = 0;
    len = 0;
    if (maxW > 0) {
        while (len < titleLen) {
            int w;
            len += XTextUtf8_seqLen(utf8 + len, titleLen - len);
            w = XPainter_textWidthRange(font, utf8, 0, len);
            if (w > maxW) break;
            keep = len;
        }
    }
    if (keep <= 0) return;
    /* 前缀 + 三个点 + NUL，恰好 keep+4 字节（库内 XMemory 分配，
     * 与 XCommonStyle 的省略路径同一口径）。 */
    buf = (char*)XMalloc_System((size_t)keep + 4);
    if (!buf) return;
    XStrncpy(buf, utf8, (size_t)keep);
    buf[keep] = '\0';
    XStrcpy(buf + keep, "...");
    XPainter_drawText(painter, x, baselineY, buf, color);
    XFree_System(buf);
}

/**
 * @brief      垂直标题条自绘：高亮竖条 + 标题逐字竖排 + 底部关闭标记。
 * @details    对标 Qt CE_DockWidgetTitle 的 verticalTitleBar 转置呈现
 *             （qcommonstyle.cpp:2170-2179：r=r.transposed() 后旋转 -90°
 *             绘制；本框架点阵字库按逐字形下排近似）与按钮转置沉底
 *             （qcommonstyle.cpp:3082-3093：转置几何下按钮贴右缘 =
 *             竖条底部）。标题条区为左侧 XDW_TITLE_H 宽竖条（与
 *             xdw_layoutContent/xdw_layoutTitleBar 同口径）：
 *             - 竖排起点基线 14（与水平路径同顶部留白），字进距取行高
 *               （XPainter_textHeight）；
 *             - 底部让位关闭钮命中区（Closable 时 XDW_CLOSE_BOX）；
 *             - 右缘 1px 竖分隔线对应水平路径底部分隔线。
 * @param      dock 目标停靠面板；可为 NULL。
 * @param      painter 目标绘制器（已绑定设备并完成平移）。
 * @param      h 面板高度（像素）。
 * @param      highlight 标题条底色（ARGB32）。
 * @param      windowText 文本/分隔线颜色（ARGB32）。
 * @return     无返回值。
 */
static void xdw_paintVerticalTitle(XDockWidget* dock, XPainter* painter,
                                   int h, uint32_t highlight,
                                   uint32_t windowText)
{
    XRect band;
    XRect line;
    const char* utf8;
    const XFont* font;
    int advance;
    int limitY;
    int baseline;
    int p;
    int len;
    int closeX;
    if (!dock || !painter) return;
    /* 竖条底色（水平路径 head 区的对改造型）。 */
    XRect_init(&band, 0, 0, XDW_TITLE_H, h);
    XPainter_fillRect(painter, &band, highlight);
    /* 标题逐字竖排：逐码点推进（XTextUtf8_seqLen 不切多字节序列），
     * 每字横向居中于竖条、纵向按行高下移。 */
    utf8 = dock->m_title ? XString_toUtf8(dock->m_title) : "";
    if (utf8 && utf8[0]) {
        font = XPainter_font(painter);
        advance = XPainter_textHeight(font);
        if (advance < 1) advance = 1;
        limitY = h - ((dock->m_features & XDockWidgetFeature_Closable)
                          ? XDW_CLOSE_BOX : 0) - XDW_TITLE_SLACK;
        len = (int)XStrlen(utf8);
        baseline = 14;
        p = 0;
        while (p < len && baseline <= limitY) {
            int glyphLen = XTextUtf8_seqLen(utf8 + p, len - p);
            int glyphW;
            int x;
            if (glyphLen <= 0) break;
            glyphW = XPainter_textWidthRange(font, utf8, p, p + glyphLen);
            x = (XDW_TITLE_H - glyphW) / 2;
            if (x < 0) x = 0;
            XPainter_drawGlyph(painter, x, baseline, utf8 + p, windowText);
            p += glyphLen;
            baseline += advance;
        }
    }
    if (dock->m_features & XDockWidgetFeature_Closable) {
        /* 关闭标记沉底（命中区见 xdw_closeHit 垂直分支）。 */
        closeX = (XDW_TITLE_H -
                  XPainter_textWidth(XPainter_font(painter), "×")) / 2;
        if (closeX < 0) closeX = 0;
        XPainter_drawText(painter, closeX, h - XDW_CLOSE_BOX + 14,
                          "×", windowText);
    }
    /* 竖条右缘分隔线（水平路径底部分隔线的对改造型）。 */
    XRect_init(&line, XDW_TITLE_H, 0, 1, h);
    XPainter_fillRect(painter, &line, windowText);
}

/* ==================== 事件处理 ==================== */

static void VX_dockWidget_paintEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect head;
    XRect line;
    uint32_t highlight;
    uint32_t windowText;
    int w;
    int h;
    if (!dock || !event) return;
    w = XWidget_width(self);
    h = XWidget_height(self);
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
    /* 对标 Qt QDockWidget::paintEvent：先以 palette Window 回填整片
     * 面板背景。浮动态面板是独立顶层窗口，无宿主背景兜底——不回填
     * 时标题条以下露出原生窗口黑底，内容文字（深色）画在黑底上不
     * 可见（实测浮出面板内容区全黑）；停靠态宿主 autofill 同色，
     * 观感不变。 */
    {
        XRect full;
        uint32_t window;
#if XPALETTE_ON
        XPalette dpalette = XWidget_palette(self);
        XColor c = XPalette_color(&dpalette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Window);
        window = XColor_rgba(&c);
#else
        window = 0xFFF0F0F0u;
#endif /* XPALETTE_ON */
        XRect_init(&full, 0, 0, w, XWidget_height(self));
        XPainter_fillRect(&painter, &full, window);
    }
    if (xdw_nativeDeco(dock)) {
        /* 原生装饰浮动态：标题由原生工具窗标题承载，控件不再绘制标题
         * 带/分隔线/按钮（对标 Qt QDockWidget::paintEvent 的
         * `!nativeDeco && !customTitleBar` 绘制门，qdockwidget.cpp；
         * 否则与原生标题叠成双标题栏，实测浮出窗口双份 □ ×）。 */
        XPainter_deinit(&painter);
        return;
    }
#if XPALETTE_ON
    {
        XPalette palette = XWidget_palette(self);
        XColor c = XPalette_color(&palette, XPaletteColorGroup_Current,
                                  XPaletteColorRole_Highlight);
        highlight = XColor_rgba(&c);
        c = XPalette_color(&palette, XPaletteColorGroup_Current,
                           XPaletteColorRole_WindowText);
        windowText = XColor_rgba(&c);
    }
#else
    highlight = 0xFF3080C0u;
    windowText = 0xFF000000u;
#endif /* XPALETTE_ON */
#if XSTYLE_ON
    /* 复扫 R-83：自定义标题条接管呈现时，默认标题带不再由本控件绘制
     * （对标 Qt QDockWidgetLayout——自定义条即标题区的唯一呈现者），
     * 该区域交由自定义条子控件自绘。 */
    if (XStyle_defaultStyle() != NULL && !xdw_titleBar(dock)) {
        /* Fusion/公共风格接管：标题栏走 CE_DockWidgetTitle
         * （highlight 标题条 + 标题文本 + 底部分隔线）。 */
        XStyle* style = XStyle_defaultStyle();
        XStyleOption opt;
        XStyleOption_init(&opt, XStyleCE_DockWidgetTitle);
        XRect_init(&opt.m_rect, 0, 0, w, 20);
        opt.m_state = XWidget_isEnabled((XWidget*)dock)
            ? XStyleState_Enabled : 0;
        opt.m_text = dock->m_title ? XString_toUtf8(dock->m_title) : "";
        opt.m_closable = (dock->m_features &
                          XDockWidgetFeature_Closable) != 0;
        opt.m_movable = (dock->m_features &
                         XDockWidgetFeature_Movable) != 0;
        opt.m_floatable = (dock->m_features &
                           XDockWidgetFeature_Floatable) != 0;
#if XPALETTE_ON
        opt.m_palette = XWidget_palette((XWidget*)dock);
#endif
        XStyle_drawControl(style, XStyleCE_DockWidgetTitle, &opt, &painter,
                           (XWidget*)dock);
        XPainter_deinit(&painter);
        return;
    }
#endif /* XSTYLE_ON */
    if (xdw_titleBar(dock)) {
        /* 自定义标题条接管：跳过默认标题带/分隔线绘制。 */
        XPainter_deinit(&painter);
        return;
    }
    if (xdw_vertical(dock)) {
        /* 垂直标题条：左侧竖条自绘（逐字竖排 + 按钮沉底）。 */
        xdw_paintVerticalTitle(dock, &painter, h, highlight, windowText);
        XPainter_deinit(&painter);
        return;
    }
    XRect_init(&head, 0, 0, w, 20);
    XPainter_fillRect(&painter, &head, highlight);
    /* 标题文本：超宽省略（对标 qcommonstyle CE_DockWidgetTitle 的
     * drawItemText 省略，qcommonstyle.cpp:2184-2185/947）。可用宽度 =
     * 面板宽 − 右侧按钮区（关闭/浮动各 XDW_CLOSE_BOX）− 左边距 −
     * 右侧安全余量。 */
    xdw_drawElidedText(&painter, XDW_TITLE_LEFT_PAD, 14,
                       dock->m_title ? XString_toUtf8(dock->m_title) : "",
                       w - XDW_CLOSE_BOX * 2 - XDW_TITLE_LEFT_PAD -
                           XDW_TITLE_SLACK,
                       windowText);
    if (dock->m_features & XDockWidgetFeature_Closable) {
        /* Closable：绘制关闭标记（对标 Qt 标题条）。 */
        XPainter_drawText(&painter, w - XDW_CLOSE_BOX + 4, 14, "×",
                          windowText);
    }
    XRect_init(&line, 0, 20, w, 1);
    XPainter_fillRect(&painter, &line, windowText);
    XPainter_deinit(&painter);
}

/**
 * @brief      鼠标按下：标题条按钮命中与拖动启动（对标 Qt 浮动
 *             QDockWidget 标题条交互）。
 * @details    关闭按钮命中且 Closable 特性开启时隐藏面板（任务裁定：
 *             close 槽回归停靠区语义简化为隐藏，面板保持登记可复显）；
 *             浮动按钮命中且 Floatable 开启时切换停靠/浮动（对标
 *             QDockWidgetTitleButton clicked→toggleTopLevel）；标题条
 *             命中且 Movable 开启时开始拖动（记录抓取偏移并提升窗口）。
 *             停靠态标题条按下且 Floatable 开启时记录起始全局点并抓
 *             取鼠标，移动越过阈值（见 mouseMoveEvent）后脱靠成独立
 *             顶层窗口跟随（对标 Qt 拖出浮动；拖拽重停靠——拖回主窗
 *             /其它停靠区——见 mouseReleaseEvent）。两类拖动启动时均
 *             按 Ctrl 修饰键记录 m_ctrlDrag（对标 qdockwidget.cpp:934，
 *             qdockwidget_p.h:50 ctrlDrag）：置位时移动不吸附宿主落点、
 *             释放不落位，面板保持浮动。
 * @param      self 目标控件。
 * @param      event 鼠标按下事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mousePressEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!dock || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) {
        xdw_callParent(self, MousePressEvent, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    /* 复扫 R-83：自定义标题条接管标题区交互——关闭钮/拖动属默认标题
     * 条行为；自定义条在位时按下事件归其子控件（子 ignore 才落到本
     * 处理），此处一律不再按默认条命中处理。 */
    if (!xdw_titleBar(dock) && xdw_closeHit(dock, &pos) &&
        (dock->m_features & 0x1)) {
        /* 对标 Qt：标题条关闭按钮关闭面板；简化为隐藏（保持登记）。 */
        XWidget_setVisible(self, false);
        XEvent_accept(event);
        return;
    }
    if (!xdw_titleBar(dock) && dock->m_floating && xdw_titleHit(dock, &pos) &&
        (dock->m_features & 0x2 /* Movable：标题条可拖动 */) &&
        XMouseEvent_button(me) == XMouseButton_LeftButton) {
        /* 原生装饰浮动态（nativeDeco）时 xdw_titleHit 恒假：标题条在
         * 客户区上方（局部 y<0 的负带），驱动已把 Tool 窗
         * WM_NCLBUTTONDOWN(HTCAPTION) 转译为本按下事件（对标 Qt
         * nativeDeco 路径 NonClientAreaMouseButtonPress → initDrag
         * (nca=true)+startDrag，qdockwidget.cpp:1097-1111）——按下即
         * 拖动，无 startDragDistance 阈值（拖回停靠走 hover/finishDrop）。 */
        XPoint g = XWidget_mapToGlobal(self, &pos);
        XDW_TRACE("float-drag start pos=(%d,%d) global=(%d,%d) nc=%d",
                  pos.x, pos.y, g.x, g.y, xdw_nativeDeco(dock));
        dock->m_dragging = true;
        /* Ctrl 拖动判定（对标 Qt qdockwidget.cpp:934：Floatable 且
         * ControlModifier → ctrlDrag；置位时移动不吸附、释放不落位）。 */
        dock->m_ctrlDrag =
            (dock->m_features & XDockWidgetFeature_Floatable) != 0 &&
            (XMouseEvent_modifiers(me) &
             XKeyboardModifier_ControlModifier) != 0;
        dock->m_dragOffset.x = g.x - XWidget_x(self);
        dock->m_dragOffset.y = g.y - XWidget_y(self);
        dock->m_pressGlobal = g; /* 释放时与当前点比较过滤原地单击 */
        XWidget_raise(self); /* 对标 Qt：拖动前激活提升浮动窗口 */
        /* 全局鼠标抓取：指针拖出窗口后事件仍转投本控件（win32 普通
         * 移动消息不带全局坐标，抓取同时保证拖动坐标链稳定）。 */
        XWidget_grabMouse(self);
        XEvent_accept(event);
        return;
    }
    if (!xdw_titleBar(dock) && !dock->m_floating && dock->m_host &&
        xdw_titleHit(dock, &pos) &&
        (dock->m_features & XDockWidgetFeature_Floatable) &&
        (dock->m_features & XDockWidgetFeature_Movable) &&
        XMouseEvent_button(me) == XMouseButton_LeftButton) {
        /* 停靠态拖出（对标 Qt initDrag）：按下仅记录起始全局点并抓
         * 取鼠标，移动越过 startDragDistance 阈值才脱靠。 */
        XPoint armG = XWidget_mapToGlobal(self, &pos);
        XDW_TRACE("docked-drag arm pos=(%d,%d) global=(%d,%d)",
                  pos.x, pos.y, armG.x, armG.y);
        dock->m_dragging = true;
        /* Ctrl 拖动判定（对标 Qt qdockwidget.cpp:934；分支条件已保证
         * Floatable，此处只看 ControlModifier）。 */
        dock->m_ctrlDrag =
            (XMouseEvent_modifiers(me) &
             XKeyboardModifier_ControlModifier) != 0;
        dock->m_dragOffset = XWidget_mapToGlobal(self, &pos);
        XWidget_grabMouse(self);
        XEvent_accept(event);
        return;
    }
    xdw_callParent(self, MousePressEvent, event);
}

/**
 * @brief      鼠标双击：标题条内且 Floatable 时切换停靠/浮动（对标
 *             QDockWidget::mouseDoubleClickEvent → toggleTopLevel）。
 * @param      self 目标控件。
 * @param      event 鼠标双击事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mouseDoubleClickEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XMouseEvent* me;
    XPoint pos;
    if (!dock || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_DBL_CLICK) {
        xdw_callParent(self, MouseDoubleClickEvent, event);
        return;
    }
    if (xdw_titleBar(dock)) {
        xdw_callParent(self, MouseDoubleClickEvent, event);
        return;
    }
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if ((xdw_titleHit(dock, &pos) ||
         (xdw_nativeDeco(dock) && pos.y < 0)) &&
        (dock->m_features & XDockWidgetFeature_Floatable) &&
        XMouseEvent_button(me) == XMouseButton_LeftButton) {
        /* nativeDeco 标题条双击（客户区负 y 带）同样切换：对标 Qt
         * NonClientAreaMouseButtonDblClick → toggleTopLevel（qdockwidget.cpp:
         * 1128），浮动工具窗无自定义条时的唯一浮/停切换手势之一。 */
        XDockWidget_setFloating(dock, !dock->m_floating);
        XDW_TRACE("dbl-click: toggle -> floating=%d",
                  dock->m_floating ? 1 : 0);
        XEvent_accept(event);
        return;
    }
    xdw_callParent(self, MouseDoubleClickEvent, event);
}

/**
 * @brief      鼠标移动：拖动中的浮动窗口跟随全局坐标平移（对标 Qt 拖动
 *             浮动 QDockWidget 移动顶层窗口）；停靠态拖动越过阈值先
 *             脱靠（setFloating(true)）再继续跟随；浮动拖动期间衔接
 *             宿主落点指示器。
 * @details    跟随坐标经控件局部点 mapToGlobal 求全局（事件全局坐标
 *             在 win32 普通移动消息中不携带），脱靠后窗口原位映射，
 *             抓取点相对窗口的偏移保持不变。脱靠调用走内部实现体
 *             （不消费 undockedGeometry，对标 Qt startDrag→unplug 用
 *             当前几何原地脱靠，qdockwidget.cpp:767；公共 setFloating
 *             才恢复 undockedGeometry，qdockwidget.cpp:1465）。浮动拖
 *             动的每次移动在 XWidget_move 之后调用
 *             XMainWindow_hoverDrop 更新宿主落点指示器（对标
 *             qdockwidget.cpp:1051-1052：!ctrlDrag 时
 *             mwlayout->hover(widgetItem, globalPos)）；Ctrl 拖动
 *             （m_ctrlDrag）与无宿主时跳过。
 * @param      self 目标控件。
 * @param      event 鼠标移动事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XMouseEvent* me;
    if (!dock || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) {
        xdw_callParent(self, MouseMoveEvent, event);
        return;
    }
    if (dock->m_dragging) {
        XPoint g;
        me = (XMouseEvent*)event;
        if (!(XMouseEvent_buttons(me) & XMouseButton_LeftButton)) {
            /* 按键已释放：安全终止拖动；浮动态顺带按当前位置收尾
             * （finishDrop 内部隐藏落点指示器，悬停宿主内则落位），
             * 防止指示器残留与拖动悬挂。 */
            dock->m_dragging = false;
            XWidget_releaseMouse(self);
            if (dock->m_floating && dock->m_host && !dock->m_ctrlDrag) {
                XPoint lp = XMouseEvent_position(me);
                XPoint gg = XWidget_mapToGlobal(self, &lp);
                XMainWindow_finishDrop((XMainWindow*)dock->m_host,
                                       self, &gg);
            }
            xdw_callParent(self, MouseMoveEvent, event);
            return;
        }
        {
            XPoint local = XMouseEvent_position(me);
            g = XWidget_mapToGlobal(self, &local);
        }
        /* 移动压缩（对标 Qt 鼠标事件压缩，QWidgetWindow 聚合移动）：
         * 拖动跟随是绝对定位，高频移动（真实游戏鼠标 1kHz、远端批量
         * 注入）逐条 SetWindowPos+重绘+呈现会把呈现链路压出秒级停顿
         * （OrayIdd+AMD 栈实测 2.4-5.5s 断崖，见 XDW_TRACE 时间线）。
         * 距上次跟随 <16ms 直接丢弃本条中间位置——下一条移动以最新
         * 位置跟上，松开前的最后一条永远精确落位。 */
        {
            int64_t nowMs = XDateTime_currentMSecsSinceEpoch();
            if (nowMs - xdw_lastFollowMs < 16 &&
                nowMs >= xdw_lastFollowMs) {
                XDW_TRACE("move skip (throttled) global=(%d,%d)",
                          g.x, g.y);
                XEvent_accept(event);
                return;
            }
            xdw_lastFollowMs = nowMs;
        }
        if (!dock->m_floating) {
            /* 停靠态拖出阈值（对标 Qt qdockwidget.cpp:982-985）：按下
             * 点到当前点的曼哈顿距离严格大于 QApplication::
             * startDragDistance 才启动拖动并脱靠，单击不误浮动。 */
            int dx = g.x - dock->m_dragOffset.x;
            int dy = g.y - dock->m_dragOffset.y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx + dy <= XApplication_startDragDistance()) {
                XEvent_accept(event);
                return;
            }
            XDW_TRACE("undock: dist=(%d,%d) -> setFloating(true)", dx, dy);
            xdw_setFloatingImpl(dock, true, false);
            /* 实现体内部按"程序性浮动"清了 m_dragging 并解除了抓取：
             * 本路径是拖出延续，需重新置位拖动态并重新抓取鼠标，否则
             * 跟随中断、释放时回归停靠判定因 !m_dragging 早退。 */
            dock->m_dragging = true;
            XWidget_grabMouse(self);
            /* 脱靠窗口已映射到拖出前屏幕位置：重算抓取偏移保持
             * 抓取点相对窗口不动。 */
            dock->m_dragOffset.x = g.x - XWidget_x(self);
            dock->m_dragOffset.y = g.y - XWidget_y(self);
        }
        XWidget_move(self, g.x - dock->m_dragOffset.x,
                     g.y - dock->m_dragOffset.y);
        XDW_TRACE("move global=(%d,%d) win=(%d,%d)", g.x, g.y,
                  XWidget_x(self), XWidget_y(self));
        if (dock->m_floating && dock->m_host && !dock->m_ctrlDrag) {
            /* 拖动中衔接宿主落点预览（qdockwidget.cpp:1051-1052）。 */
            XMainWindow_hoverDrop((XMainWindow*)dock->m_host, self, &g);
        }
        XEvent_accept(event);
        return;
    }
    xdw_callParent(self, MouseMoveEvent, event);
}

/**
 * @brief      鼠标释放：结束标题栏拖动并解除全局鼠标抓取；浮动拖动
 *             释放经宿主落位服务衔接最终落点（对标 Qt 拖回停靠）。
 * @details    释放时先解鼠标抓取，随后把释放点全局坐标交给
 *             XMainWindow_finishDrop（对标 Qt endDrag→
 *             QMainWindowLayout::plug，qdockwidget.cpp:822）：返回
 *             非 NoDockWidgetArea 表示已落位（宿主内部完成
 *             setFloating(false)+重排，对标 plug→setWindowState）；
 *             返回 NoDockWidgetArea 表示无有效落点，面板保持浮动
 *             （对标 plug 失败分支：stay in the floating state，
 *             qdockwidget.cpp:822-824）。Ctrl 拖动（m_ctrlDrag）跳过
 *             落位（对标 qdockwidget.cpp:934 置位 ctrlDrag 后 endDrag
 *             经 layout->restore 保持浮动）。
 * @param      self 目标控件。
 * @param      event 鼠标释放事件。
 * @return     无返回值。
 */
static void VX_dockWidget_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XDockWidget* dock = (XDockWidget*)self;
    XMouseEvent* me;
    XPoint local;
    XPoint g;
    int area;
    if (!dock || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE ||
        !dock->m_dragging) {
        xdw_callParent(self, MouseReleaseEvent, event);
        return;
    }
    dock->m_dragging = false;
    /* 约束：落位调用在 releaseMouse 之后（先解除抓取再触发宿主重排）。 */
    XWidget_releaseMouse(self);
    if (dock->m_floating && dock->m_host && !dock->m_ctrlDrag) {
        me = (XMouseEvent*)event;
        local = XMouseEvent_position(me);
        g = XWidget_mapToGlobal(self, &local);
        /* 对标 Qt：仅真实拖动（位移超过 startDragDistance）才尝试落位
         * ——原地单击（无位移）不落位，面板保持浮动；否则双击标题条
         * 的首击释放即回停靠、双击事件再浮出，净效果浮着不动（实测
         * "双击无法回归"根因）。 */
        {
            int dx = g.x - dock->m_pressGlobal.x;
            int dy = g.y - dock->m_pressGlobal.y;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx + dy <= XApplication_startDragDistance()) {
                XDW_TRACE("release: below threshold dist=(%d,%d), "
                          "stay floating", dx, dy);
                XEvent_accept(event);
                return;
            }
        }
        XDW_TRACE("release: finishDrop global=(%d,%d)", g.x, g.y);
        area = XMainWindow_finishDrop((XMainWindow*)dock->m_host, self, &g);
        XDW_TRACE("release: finishDrop area=%d", area);
        (void)area;
    }
    /* 拖动状态复位（对标 Qt 拖动结束销毁 DragState：ctrlDrag=false，
     * qdockwidget.cpp:744；下次按下重新判定）。 */
    dock->m_ctrlDrag = false;
    XEvent_accept(event);
    return;
}

/** @brief 显示事件：转发父类后广播 visibilityChanged(true)。 */
static void VX_dockWidget_showEvent(XWidget* self, XEvent* event)
{
    xdw_callParent(self, ShowEvent, event);
    if (self)
        xdw_announceVisible((XDockWidget*)self, XWidget_isVisible(self));
}

/** @brief 隐藏事件：转发父类后广播 visibilityChanged(false)。 */
static void VX_dockWidget_hideEvent(XWidget* self, XEvent* event)
{
    xdw_callParent(self, HideEvent, event);
    if (self)
        xdw_announceVisible((XDockWidget*)self, false);
}

/**
 * @brief      关闭事件：隐藏面板并拒绝关闭（对标 Qt QDockWidget 关闭
 *             语义）。
 * @details    对标 Qt：QDockWidget 关闭即隐藏（closeEvent→hide），面板
 *             保持登记与浮动态，toggleViewAction 可复显。本框架驱动侧
 *             约定"关闭事件被接受→隐藏并销毁原生窗口"——浮动态若走该
 *             路径，原生工具窗被销毁后 toggleViewAction 复显将落空。
 *             故此处在控件侧完成隐藏后拒绝事件：控件隐藏经可见性传播
 *             收起原生窗（窗对象保留），驱动侧 accept→destroy 分支不
 *             触发。停靠态被关闭时同样只隐藏（面板保持登记可复显，与
 *             既有标题条关闭钮的"隐藏"语义一致）。
 * @param      self 目标控件。
 * @param      event 关闭事件。
 * @return     无返回值。
 */
static void VX_dockWidget_closeEvent(XWidget* self, XEvent* event)
{
    if (!self || !event) return;
    XDW_TRACE("closeEvent: hide + ignore");
    XWidget_setVisible(self, false);
    XEvent_ignore(event);
}

/**
 * @brief      尺寸变更事件：内容控件重摆到标题条旁全部区域；浮动态
 *             记录 undockedGeometry。
 * @details    对标 Qt：QDockWidget 内容恒随面板尺寸跟随（停靠态主窗口
 *             重排、浮动态顶层缩放、回归停靠三态的几何变化都经
 *             ResizeEvent 到达），修复此前 setWidget 仅一次性摆位、
 *             之后内容矩形不随面板更新的缺陷。浮动态 resize 记录
 *             m_undockedGeometry（对标 qdockwidget.cpp:1710-1712：
 *             "if the mainwindow is plugging us, we don't want to
 *             update undocked geometry"——本框架回归停靠的 resize 发生
 *             在 setFloating(false) 已置 m_floating=false 之后，天然
 *             不记，无需 Qt 的 pluggingWidget 旁路）。
 * @param      self 目标控件。
 * @param      event 尺寸变更事件。
 * @return     无返回值。
 */
static void VX_dockWidget_resizeEvent(XWidget* self, XEvent* event)
{
    xdw_callParent(self, ResizeEvent, event);
    if (self) {
        XDockWidget* dock = (XDockWidget*)self;
        xdw_layoutTitleBar(dock);
        xdw_layoutContent(dock);
        if (dock->m_floating) {
            /* 浮动态几何快照（qdockwidget_p.h:88），供下次
             * setFloating(true) 恢复（qdockwidget.cpp:1465）。 */
            dock->m_undockedGeometry.x = XWidget_x(self);
            dock->m_undockedGeometry.y = XWidget_y(self);
            dock->m_undockedGeometry.width = XWidget_width(self);
            dock->m_undockedGeometry.height = XWidget_height(self);
            dock->m_undockedValid = true;
        }
    }
}

/* ==================== 生命周期与虚表 ==================== */

static void VX_dockWidget_deinit(XDockWidget* self)
{
    if (!self) return;
    if (self->m_widget) {
        XClassDelete(self->m_widget);
        self->m_widget = NULL;
    }
    if (self->m_title) {
        XClassDelete(self->m_title);
        self->m_title = NULL;
    }
    if (self->m_toggleAction) {
        /* 切换动作归面板所有（对标 Qt toggleViewAction 归 dock 所有）。 */
        XClassDelete(self->m_toggleAction);
        self->m_toggleAction = NULL;
    }
    {
        /* 复扫 R-83 配套：自定义标题条为借用承载（宏开启=父类槽位，
         * 经 xdw_titleBar/XWidget_titleBarWidget 读）——析构级联删子前
         * 先摘除父链，控件归还调用方（面板不删除）；父类槽位清空由
         * 基类 VXWidget_deinit 收尾，行为与自有成员置空等价。 */
        XWidget* bar = xdw_titleBar(self);
        if (bar) {
            if (XWidget_parentWidget(bar) == (XWidget*)self)
                XWidget_setParent(bar, NULL, 0);
        }
    }
    if (self->m_host) {
        /* 析构时摘除宿主销毁监听（setHost 建立的回链）。 */
        XObject_disconnect_1((XObject*)self->m_host,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xdw_hostDestroyedSlot);
    }
    self->m_host = NULL;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

XVtable* XDockWidget_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDockWidget)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VX_dockWidget_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VX_dockWidget_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VX_dockWidget_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VX_dockWidget_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseDoubleClickEvent,
                             VX_dockWidget_mouseDoubleClickEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VX_dockWidget_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VX_dockWidget_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VX_dockWidget_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VX_dockWidget_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VX_dockWidget_deinit);
    return XVTABLE_DEFAULT;
}

void XDockWidget_init(XDockWidget* self, const char* utf8Title,
                      XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XDockWidget);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    self->m_title = XString_create_utf8(utf8Title ? utf8Title : "");
    self->m_features = (int)XDockWidgetFeature_Closable |
                       (int)XDockWidgetFeature_Movable |
                       (int)XDockWidgetFeature_Floatable;
    self->m_allowedAreas = (int)XDockWidgetArea_All;
    self->m_floating = false;
    /* 对标 Qt qwidget.cpp paintBackground 的 DrawAsRoot 分支（问题
     * #33 同款根因，XMainWindow_init/XDialog_init 同款置位先例）：
     * 面板脱靠成独立顶层窗口后，停靠态依赖的宿主主窗口背景填充
     * 不复存在，原生窗口 background_pixel=0（黑）直接露出——标题
     * 条以下全部区域黑底，深色内容文字画在黑底上不可见（实测浮出
     * 停靠面板内容区全黑）。置位后由默认绘制槽按 palette Window
     * 回填整片背景。 */
    XWidget_setAutoFillBackground(self, true);
}

XDockWidget* XDockWidget_create_ex(XMemoryType memory,
                                   const char* utf8Title,
                                   XWidget* parent, XWidgetFlags flags)
{
    XDockWidget* self =
        (XDockWidget*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDockWidget_init(self, utf8Title, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 公共 API ==================== */

void XDockWidget_setWidget(XDockWidget* self, XWidget* widget)
{
    if (!self || self->m_widget == widget) return;
    if (self->m_widget) {
        /* 对标 Qt setWidget 的替换语义：已有内容时先摘除旧控件（转独立
         * 顶层即脱离本面板父链），所有权转移给调用方、由调用方决定释放
         * （Qt 中旧 widget 脱离 dock 后归调用方管理，dock 不再删除）。 */
        XWidget_setParent(self->m_widget, NULL, 0);
    }
    self->m_widget = widget;
    if (widget)
        XWidget_setParent(widget, (XWidget*)self, 0);
    /* 对标 Qt：新内容立即摆到标题条以下全部区域。 */
    xdw_layoutContent(self);
}

XWidget* XDockWidget_widget(const XDockWidget* self)
{
    return self ? self->m_widget : NULL;
}

void XDockWidget_setFeatures(XDockWidget* self, int features)
{
    if (!self) return;
    /* 对标 Qt setFeatures：先按 FeatureMask 裁剪无效高位。 */
    features &= (int)XDockWidgetFeature_FeatureMask;
    if (self->m_features == features) return;
    self->m_features = features;
    xdw_emitInt(self, (size_t)XDockWidget_featuresChanged_signal,
                features);
}

void XDockWidget_setFeature(XDockWidget* self, int feature, bool on)
{
    int features;
    if (!self) return;
    /* 对标 QDockWidget::setFeature(feature, on)：单特性开关，其余
     * 特性保持不变。 */
    features = on
        ? (self->m_features | (feature & (int)XDockWidgetFeature_FeatureMask))
        : (self->m_features & ~(feature & (int)XDockWidgetFeature_FeatureMask));
    XDockWidget_setFeatures(self, features);
}

int XDockWidget_features(const XDockWidget* self)
{
    return self ? self->m_features : 0;
}

/**
 * @brief      setFloating 实现体（公共入口与拖出路径共用）。
 * @details    useUndockedGeometry=true 时（公共 setFloating）转浮动
 *             优先恢复上次浮动态几何（对标 Qt 公共 setFloating 用
 *             undockedGeometry，qdockwidget.cpp:1464-1468）；false 时
 *             （拖出路径）用当前几何原地脱靠（对标 Qt startDrag→
 *             unplug，qdockwidget.cpp:767）。转浮动先 move 后 resize：
 *             浮动态 ResizeEvent 记录 undockedGeometry（resizeEvent）
 *             时位置已定，避免把 reparent 瞬间的过渡坐标存进快照。
 * @param      self 目标停靠面板；可为 NULL。
 * @param      floating true 转浮动，false 回归停靠。
 * @param      useUndockedGeometry true 恢复保存的浮动几何（有效时）。
 * @return     无返回值。
 */
static void xdw_setFloatingImpl(XDockWidget* self, bool floating,
                                bool useUndockedGeometry)
{
    XWidget* selfw;
    XWidget* host;
    XPoint origin;
    XPoint globalPos;
    int w;
    int h;
    bool wasVisible;
    if (!self || self->m_floating == floating) return;
    XDW_TRACE("setFloating %d (undocked=%d)", floating ? 1 : 0,
              useUndockedGeometry ? 1 : 0);
    selfw = (XWidget*)self;
    host = self->m_host;
    /* 注意：不设「无宿主即早退」守卫——浮动态翻转与 topLevelChanged
     * 发射不依赖宿主登记（qdockwidget.cpp:1188-1193 的无可回归区守卫
     * 由下方宿主分支跳过承载）：无宿主面板 setFloating(false) 仍须
     * 复位 m_floating 并发射信号（回归契约，apitest D 段口径），
     * 提前返回会同时吞掉状态翻转与信号。 */
    /* 记录当前几何与全局位置（对标 Qt：浮动时窗口保持屏幕位置尺寸）。
     * 必须在重设父对象前完成，子控件坐标经父链映射才有意义。 */
    XPoint_init(&origin, 0, 0);
    globalPos = XWidget_mapToGlobal(selfw, &origin);
    w = XWidget_width(selfw);
    h = XWidget_height(selfw);
    wasVisible = XWidget_isVisible(selfw);
    self->m_floating = floating;
    /* 对标 Qt setFloating 首行的 endDrag(Abort)（qdockwidget.cpp:1461）：
     * 状态切换必须终止进行中的拖动并解除本控件持有的全局鼠标抓取——
     * 否则双击切换（press 已抓取）后抓取泄漏，后续所有窗口的鼠标事件
     * 被重定向进本面板，表现为多次操作后整体交互失效。 */
    if (self->m_dragging) {
        self->m_dragging = false;
        XWidget_releaseMouse(selfw);
    }
    self->m_ctrlDrag = false;
    if (floating) {
        /* 对标 Qt：setFloating(true) 脱离主窗布局，转成独立顶层窗口。
         * 窗型对标 qdockwidget.cpp:1203-1222：默认标题条（无自定义条）
         * 走原生装饰路径（Qt::Tool+WindowTitleHint+CloseButtonHint，
         * wmSupportsNativeWindowDeco 在 Windows 恒真）——映射 Tool 窗
         * 型（细标题条仅关闭钮，标题文本由原生标题承载）；设了自定义
         * 标题条走 Qt::Tool|FramelessWindowHint——映射 Popup 无框窗，
         * 自定义条继续自绘。几何来源对标 qdockwidget.cpp:1464-1468：
         * undockedGeometry 有效（qdockwidget_p.h:88）则恢复上次浮动
         * 几何，否则保留当前尺寸并映射到原全局位置。 */
        if (useUndockedGeometry && self->m_undockedValid) {
            if (self->m_undockedGeometry.width > 0)
                w = self->m_undockedGeometry.width;
            if (self->m_undockedGeometry.height > 0)
                h = self->m_undockedGeometry.height;
            globalPos.x = self->m_undockedGeometry.x;
            globalPos.y = self->m_undockedGeometry.y;
        }
        XWidget_setParent(selfw, NULL,
                          (XWidgetFlags)(xdw_titleBar(self)
                                             ? XWindowType_Popup
                                             : XWindowType_Tool));
        /* 标题同值跳过（对标 Qt QWidget::setWindowTitle 对同值标题的
         * 幂等语义）：反复浮/停切换传同一标题时不再重建平台窗口标题
         * 字符串——旧实现每轮无条件重设，驱动 XWidget 侧堆字符串整建
         * 整毁，拖浮循环内存统计近似线性增长（每轮 ~96B 计量漂移）
         * 的直接来源；标题变化路径行为不变。 */
        {
            const XString* cur = XWidget_windowTitle(selfw);
            if (!cur ||
                !XString_equals(cur, self->m_title, XChar_CaseSensitive))
                XWidget_setWindowTitle(selfw, self->m_title);
        }
        if (w <= 0) w = 200; /* 无宿主几何时的兜底尺寸 */
        if (h <= 0) h = 150;
        /* 先 move 后 resize：浮动态 ResizeEvent 记录 undockedGeometry
         * 时位置已定（见实现体 @details）。 */
        XWidget_move(selfw, globalPos.x, globalPos.y);
        XWidget_resize(selfw, w, h);
        if (wasVisible) {
            XWidget_show(selfw);
            XWidget_raise(selfw);
            XWidget_activateWindow(selfw); /* 对标 Qt：浮动窗获得焦点 */
        }
    } else {
        /* 对标 Qt：setFloating(false) 回归停靠区，重新挂回宿主主窗口，
         * 几何交还主窗口停靠布局（随后统一重排）。无宿主登记（从未
         * addDockWidget）时跳过挂回——浮动翻转与信号已在下方无条件
         * 承载（见函数首注）；此时不得走 setParent(selfw, NULL, 0)，
         * 否则面板被错误转成普通顶层窗口、浮/停语义失真。 */
        if (host) {
            XWidget_setParent(selfw, host, 0);
            if (wasVisible) XWidget_show(selfw);
        }
    }
    if (host)
        XMainWindow_updateDockLayout((XMainWindow*)host);
    XDW_TRACE("setFloating done -> %d", self->m_floating ? 1 : 0);
    xdw_emitBool(self, (size_t)XDockWidget_topLevelChanged_signal,
                 floating);
    /* 对标 Qt：floating 与 dockLocationChanged 在同一切换点成对发射
     * （qdockwidget.cpp:1234-1242）——转浮动携带 NoDockWidgetArea，
     * 回归停靠携带宿主登记区域。 */
    if (floating) {
        xdw_emitInt(self, (size_t)XDockWidget_dockLocationChanged_signal,
                    (int)XDockWidgetArea_NoDockWidgetArea);
    } else {
        xdw_emitInt(self, (size_t)XDockWidget_dockLocationChanged_signal,
                    XMainWindow_dockWidgetArea(
                        (const XMainWindow*)host, selfw));
    }
}

void XDockWidget_setFloating(XDockWidget* self, bool floating)
{
    /* 公共入口：恢复语义对标 Qt 公共 setFloating（qdockwidget.cpp:
     * 1464-1468 用 undockedGeometry）；拖出路径走实现体 false 分支。 */
    xdw_setFloatingImpl(self, floating, true);
}

bool XDockWidget_isFloating(const XDockWidget* self)
{
    return self ? self->m_floating : false;
}

void XDockWidget_setAllowedAreas(XDockWidget* self, int areas)
{
    if (!self || self->m_allowedAreas == areas) return;
    self->m_allowedAreas = areas;
    xdw_emitInt(self, (size_t)XDockWidget_allowedAreasChanged_signal,
                areas);
}

int XDockWidget_allowedAreas(const XDockWidget* self)
{
    return self ? self->m_allowedAreas : 0;
}

bool XDockWidget_isAreaAllowed(const XDockWidget* self, int area)
{
    return self ? (self->m_allowedAreas & area) != 0 : false;
}

#if XGUI_CUSTOM_TITLEBAR_ON
void XDockWidget_setTitleBarWidget(XDockWidget* self, XWidget* widget)
{
    if (!self) return;
    /* 挂载/解挂/借用契约统一交父类控件级通用槽实现（同指针幂等短路、
     * 旧条隐藏解挂归还创建方不释放、新条挂为本面板子控件并显式 show、
     * update + 顶层装饰同步）；查询 API 无额外语义，头文件宏映射父类
     * XWidget_titleBarWidget 纯读槽。本面板内部消费点（布局钉位/绘制
     * 分流/输入门禁/析构解挂）统一经 xdw_titleBar 读父类槽位。 */
    XWidget_setTitleBarWidget((XWidget*)self, widget);
    /* 评审返修补回挂载即钉位（原自有 setter 语义）：父类实现只挂载不
     * 摆位（摆位归消费方），若不在挂载点立即铺满标题条区，已定型尺寸
     * 且可见的活面板换挂/新挂 0 尺寸条时，条保持 0/旧几何直至面板下一
     * 次 resizeEvent。钳宽=面板宽、高钳入 [1,XDW_TITLE_H]；条为空自短路
     * （xdw_layoutTitleBar 内判空）。 */
    xdw_layoutTitleBar(self);
}
#else
void XDockWidget_setTitleBarWidget(XDockWidget* self, XWidget* widget)
{
    XWidget* old;
    if (!self || self->m_titleBar == widget) return;
    old = self->m_titleBar;
    if (old) {
        /* 借用承载（头文件契约 m_titleBar 为借用）：旧自定义标题条
         * 不删除，若曾挂到本面板则摘父链归还调用方（同 setWidget 的
         * 摘除语义）。 */
        XWidget_setVisible(old, false);
        if (XWidget_parentWidget(old) == (XWidget*)self)
            XWidget_setParent(old, NULL, 0);
    }
    self->m_titleBar = widget;
    if (widget) {
        /* 复扫 R-83：此前纯存储——自定义标题条不 reparent/不入标题条
         * 区布局/paint 不消费，永不可见。现对标 Qt QDockWidget::
         * setTitleBarWidget 的接管呈现：挂为本面板子控件、铺满标题条
         * 区、显式 show（框架显式 show 语义）；绘制分流与标题区鼠标
         * 门禁见 paintEvent/mousePressEvent。传 NULL 恢复默认标题条。 */
        XWidget_setParent(widget, (XWidget*)self, 0);
        xdw_layoutTitleBar(self);
        XWidget_show(widget);
    }
    XWidget_update((XWidget*)self);
}

XWidget* XDockWidget_titleBarWidget(const XDockWidget* self)
{
    return self ? self->m_titleBar : NULL;
}
#endif /* XGUI_CUSTOM_TITLEBAR_ON */

/** @brief toggleViewAction 槽：翻转面板可见性（对标 QDockWidget
 *         toggleViewAction 的 triggered→setVisible 桥接）。 */
static void xdw_toggleViewSlot(XObject* receiver, XVarList* args)
{
    XDockWidget* dock = (XDockWidget*)receiver;
    (void)args;
    if (!dock) return;
    XWidget_setVisible((XWidget*)dock, !XWidget_isVisible((XWidget*)dock));
}

XAction* XDockWidget_toggleViewAction(XDockWidget* self)
{
#if XACTION_ON
    XAction* action;
    if (!self) return NULL;
    if (self->m_toggleAction) return self->m_toggleAction;
    action = XAction_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL, NULL);
    if (!action) return NULL;
    self->m_toggleAction = action;
    XAction_setCheckable(action, true); /* 对标 Qt：切换动作可选中 */
    XAction_setText_2(action,
                      self->m_title ? XString_toUtf8(self->m_title) : "");
    XAction_setChecked(action, XWidget_isVisible((XWidget*)self));
    XObject_connect_1((XObject*)action, XSignal(XAction_triggered_signal),
                      (XObject*)self, xdw_toggleViewSlot,
                      XConnectionType_Direct);
    return self->m_toggleAction;
#else
    /* XACTION_ON 裁剪时保留遗留回退（无动作子系统）。 */
    (void)self;
    return NULL;
#endif /* XACTION_ON */
}

/* ==================== 保护接口（XDockWidget_Protected.h） ==================== */

/** @brief 宿主销毁槽：宿主主窗口释放时摘除回链（对标 Qt 的 QPointer
 *         防悬空语义；浮动面板已脱离宿主控件树，必须显式摘链）。 */
static void xdw_hostDestroyedSlot(XObject* receiver, XVarList* args)
{
    XDockWidget* dock = (XDockWidget*)receiver;
    (void)args;
    if (dock) dock->m_host = NULL;
}

void XDockWidget_setHost(XDockWidget* self, XWidget* host)
{
    if (!self) return;
    if (self->m_host) {
        XObject_disconnect_1((XObject*)self->m_host,
                             XSignal(XObject_destroyed_signal),
                             (XObject*)self, xdw_hostDestroyedSlot);
    }
    self->m_host = host;
    if (host) {
        XObject_connect_1((XObject*)host,
                          XSignal(XObject_destroyed_signal),
                          (XObject*)self, xdw_hostDestroyedSlot,
                          XConnectionType_Direct);
    }
}

XWidget* XDockWidget_host(const XDockWidget* self)
{
    return self ? self->m_host : NULL;
}

/* ==================== 信号 ==================== */

void* XDockWidget_featuresChanged_signal(XDockWidget* self, int features)
{
    (void)self; (void)features;
    return (void*)(size_t)XDockWidget_featuresChanged_signal;
}

void* XDockWidget_topLevelChanged_signal(XDockWidget* self, bool topLevel)
{
    (void)self; (void)topLevel;
    return (void*)(size_t)XDockWidget_topLevelChanged_signal;
}

void* XDockWidget_allowedAreasChanged_signal(XDockWidget* self, int areas)
{
    (void)self; (void)areas;
    return (void*)(size_t)XDockWidget_allowedAreasChanged_signal;
}

void* XDockWidget_visibilityChanged_signal(XDockWidget* self, bool visible)
{
    (void)self; (void)visible;
    return (void*)(size_t)XDockWidget_visibilityChanged_signal;
}
void* XDockWidget_dockLocationChanged_signal(XDockWidget* self, int area)
{
    (void)self; (void)area;
    return (void*)(size_t)XDockWidget_dockLocationChanged_signal;
}














#endif /* XWIDGET_ON && XDOCKWIDGET_ON */