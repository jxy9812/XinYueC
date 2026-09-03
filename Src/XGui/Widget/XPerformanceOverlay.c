/******************************************************************************
 * @file       XPerformanceOverlay.c
 * @brief      XGui 性能悬浮层控件实现。
 ******************************************************************************/
#include "XPerformanceOverlay.h"
#include "XWidget_Protected.h"
#include "XFont.h"
#include "XImage.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON

static bool performanceOverlay_drawContent(XWidget* widget,
                                           XPainter* painter,
                                           void* userData);

static void performanceOverlay_updateText(XPerformanceOverlay* self)
{
    char text[160];
    size_t used = 0;
    int n;
    if (!self) return;
    text[0] = '\0';
#if XGUI_PERFORMANCE_OVERLAY_FPS_ON
    if (self->m_fpsVisible) {
        n = snprintf(text + used, sizeof(text) - used, "FPS %.1f", self->m_fps);
        if (n > 0)
            used += (size_t)n < sizeof(text) - used
                        ? (size_t)n : sizeof(text) - used - 1;
    }
#endif
#if XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON
    if (self->m_frameTimeVisible) {
        if (used > 0 && used + 1 < sizeof(text)) text[used++] = '\n';
        n = snprintf(text + used, sizeof(text) - used, "帧耗时 %.2f/%.2f ms",
                     self->m_frameMs, self->m_maxFrameMs);
        if (n > 0)
            used += (size_t)n < sizeof(text) - used
                        ? (size_t)n : sizeof(text) - used - 1;
    }
#endif
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
    if (self->m_networkVisible) {
        if (used > 0 && used + 1 < sizeof(text)) text[used++] = '\n';
        if (self->m_networkAvailable) {
            double downloadRate = self->m_networkRxKbps;
            double uploadRate = self->m_networkTxKbps;
            const char* downloadUnit = "KB/s";
            const char* uploadUnit = "KB/s";
            if (downloadRate >= 1024.0) {
                downloadRate /= 1024.0;
                downloadUnit = "MB/s";
            }
            if (uploadRate >= 1024.0) {
                uploadRate /= 1024.0;
                uploadUnit = "MB/s";
            }
            n = snprintf(text + used, sizeof(text) - used,
                         "下载 %.1f %s 上传 %.1f %s",
                         downloadRate, downloadUnit, uploadRate, uploadUnit);
        } else {
            n = snprintf(text + used, sizeof(text) - used,
                         "下载 无 上传 无");
        }
        if (n > 0)
            used += (size_t)n < sizeof(text) - used
                        ? (size_t)n : sizeof(text) - used - 1;
    }
#endif
    if (used == 0)
        (void)snprintf(text, sizeof(text), "Perf disabled");
    else
        text[used] = '\0';
    XLabel_setText_2(&self->m_base, text);
}

static void VXPerformanceOverlay_copy(XPerformanceOverlay* self,
                                      const XPerformanceOverlay* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self))
        XPerformanceOverlay_init(self, NULL, 0);
    XClass_Parent(XLabel, EXClass_Copy,
                  void(*)(XLabel*, const XLabel*))(&self->m_base,
                                                    &other->m_base);
    self->m_sampleStartUsecs = other->m_sampleStartUsecs;
    self->m_sampleUsecs = other->m_sampleUsecs;
    self->m_maxUsecs = other->m_maxUsecs;
    self->m_sampleFrames = other->m_sampleFrames;
    self->m_fps = other->m_fps;
    self->m_frameMs = other->m_frameMs;
    self->m_maxFrameMs = other->m_maxFrameMs;
    self->m_networkAvailable = other->m_networkAvailable;
    self->m_networkSampleUsecs = other->m_networkSampleUsecs;
    self->m_networkRxBytes = other->m_networkRxBytes;
    self->m_networkTxBytes = other->m_networkTxBytes;
    self->m_networkRxKbps = other->m_networkRxKbps;
    self->m_networkTxKbps = other->m_networkTxKbps;
    self->m_backgroundColor = other->m_backgroundColor;
    self->m_fpsVisible = other->m_fpsVisible;
    self->m_frameTimeVisible = other->m_frameTimeVisible;
    self->m_networkVisible = other->m_networkVisible;
    self->m_movable = other->m_movable;
    self->m_fixed = other->m_fixed;
    self->m_dragging = false;
    self->m_dragOffsetX = other->m_dragOffsetX;
    self->m_dragOffsetY = other->m_dragOffsetY;
}

static void VXPerformanceOverlay_move(XPerformanceOverlay* self,
                                      XPerformanceOverlay* other)
{
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self))
        XPerformanceOverlay_init(self, NULL, 0);
    XClass_Parent(XLabel, EXClass_Move,
                  void(*)(XLabel*, XLabel*))(&self->m_base, &other->m_base);
    self->m_sampleStartUsecs = other->m_sampleStartUsecs;
    self->m_sampleUsecs = other->m_sampleUsecs;
    self->m_maxUsecs = other->m_maxUsecs;
    self->m_sampleFrames = other->m_sampleFrames;
    self->m_fps = other->m_fps;
    self->m_frameMs = other->m_frameMs;
    self->m_maxFrameMs = other->m_maxFrameMs;
    self->m_networkAvailable = other->m_networkAvailable;
    self->m_networkSampleUsecs = other->m_networkSampleUsecs;
    self->m_networkRxBytes = other->m_networkRxBytes;
    self->m_networkTxBytes = other->m_networkTxBytes;
    self->m_networkRxKbps = other->m_networkRxKbps;
    self->m_networkTxKbps = other->m_networkTxKbps;
    self->m_backgroundColor = other->m_backgroundColor;
    self->m_fpsVisible = other->m_fpsVisible;
    self->m_frameTimeVisible = other->m_frameTimeVisible;
    self->m_networkVisible = other->m_networkVisible;
    self->m_movable = other->m_movable;
    self->m_fixed = other->m_fixed;
    self->m_dragging = other->m_dragging;
    self->m_dragOffsetX = other->m_dragOffsetX;
    self->m_dragOffsetY = other->m_dragOffsetY;
    other->m_sampleStartUsecs = 0;
    other->m_sampleUsecs = 0;
    other->m_maxUsecs = 0;
    other->m_sampleFrames = 0;
    other->m_fps = 0.0;
    other->m_frameMs = 0.0;
    other->m_maxFrameMs = 0.0;
    other->m_networkAvailable = false;
    other->m_networkSampleUsecs = 0;
    other->m_networkRxBytes = 0;
    other->m_networkTxBytes = 0;
    other->m_networkRxKbps = 0.0;
    other->m_networkTxKbps = 0.0;
    other->m_fpsVisible = true;
    other->m_frameTimeVisible = true;
    other->m_networkVisible = true;
    other->m_movable = true;
    other->m_fixed = false;
    other->m_dragging = false;
    other->m_dragOffsetX = 0;
    other->m_dragOffsetY = 0;
}

static void VXPerformanceOverlay_deinit(XPerformanceOverlay* self)
{
    if (!self) return;
    XClass_Deinit_Parent(XLabel, &self->m_base);
}

XVtable* XPerformanceOverlay_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPerformanceOverlay)
    XVTABLE_INHERIT_XCLASS(XLabel);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPerformanceOverlay_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPerformanceOverlay_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPerformanceOverlay_deinit);
    return XVTABLE_DEFAULT;
}

void XPerformanceOverlay_init(XPerformanceOverlay* self, XWidget* parent,
                              XWidgetFlags flags)
{
    XPalette palette;
    if (!self) return;
    memset(self, 0, sizeof(*self));
    XLabel_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XPerformanceOverlay);
    self->m_backgroundColor = 0xd9000000u;
    self->m_fpsVisible = true;
    self->m_frameTimeVisible = true;
    self->m_networkVisible = true;
    self->m_movable = true;
    XLabel_setMargin(&self->m_base, 4);
    XLabel_setAlignment(&self->m_base, XAlignment_Left | XAlignment_Top);
    XFrame_setFrameStyle((XFrame*)&self->m_base,
                         (int)XFrameShape_NoFrame | (int)XFrameShadow_Plain);
#if XPALETTE_ON
    palette = XWidget_palette((XWidget*)&self->m_base);
    XPalette_setColor(&palette, XPaletteColorGroup_Current,
                      XPaletteColorRole_WindowText,
                      XColor_create_argb(0xfff2f6f8u));
    XPalette_setColor(&palette, XPaletteColorGroup_Current,
                      XPaletteColorRole_Text,
                      XColor_create_argb(0xfff2f6f8u));
    XWidget_setPalette((XWidget*)&self->m_base, &palette);
#endif
    XWidget_setGeometry((XWidget*)&self->m_base, 0, 0, 180, 60);
    XPerformanceOverlay_reset(self);
}

XPerformanceOverlay* XPerformanceOverlay_create_ex(XMemoryType memory,
                                                    XWidget* parent,
                                                    XWidgetFlags flags)
{
    XPerformanceOverlay* self = (XPerformanceOverlay*)XMemory_malloc(
        sizeof(*self), memory);
    if (!self) return NULL;
    XPerformanceOverlay_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

void XPerformanceOverlay_setFontFamily(XPerformanceOverlay* self,
                                       const char* family)
{
    XFont source;
    XFont font;
    if (!self) return;
    source = XWidget_font((XWidget*)&self->m_base);
    XFont_init(&font);
    XFont_copy_base(&font, &source);
    XFont_deinit_base(&source);
    XFont_setFamily(&font, family);
    XWidget_setFont((XWidget*)&self->m_base, &font);
    XFont_deinit_base(&font);
}

void XPerformanceOverlay_setFpsVisible(XPerformanceOverlay* self,
                                       bool visible)
{
#if XGUI_PERFORMANCE_OVERLAY_FPS_ON
    if (!self || self->m_fpsVisible == visible) return;
    self->m_fpsVisible = visible;
    performanceOverlay_updateText(self);
#else
    (void)self;
    (void)visible;
#endif
}

bool XPerformanceOverlay_isFpsVisible(const XPerformanceOverlay* self)
{
#if XGUI_PERFORMANCE_OVERLAY_FPS_ON
    return self ? self->m_fpsVisible : false;
#else
    (void)self;
    return false;
#endif
}

void XPerformanceOverlay_setFrameTimeVisible(XPerformanceOverlay* self,
                                             bool visible)
{
#if XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON
    if (!self || self->m_frameTimeVisible == visible) return;
    self->m_frameTimeVisible = visible;
    performanceOverlay_updateText(self);
#else
    (void)self;
    (void)visible;
#endif
}

bool XPerformanceOverlay_isFrameTimeVisible(
    const XPerformanceOverlay* self)
{
#if XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON
    return self ? self->m_frameTimeVisible : false;
#else
    (void)self;
    return false;
#endif
}

void XPerformanceOverlay_setNetworkVisible(XPerformanceOverlay* self,
                                           bool visible)
{
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
    if (!self || self->m_networkVisible == visible) return;
    self->m_networkVisible = visible;
    performanceOverlay_updateText(self);
#else
    (void)self;
    (void)visible;
#endif
}

bool XPerformanceOverlay_isNetworkVisible(const XPerformanceOverlay* self)
{
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
    return self ? self->m_networkVisible : false;
#else
    (void)self;
    return false;
#endif
}

void XPerformanceOverlay_setPresetPosition(XPerformanceOverlay* self,
                                           XPerformanceOverlayPosition position,
                                           int windowWidth, int windowHeight,
                                           int margin)
{
    XRect geo;
    int x;
    int y;
    int maxX;
    int maxY;
    if (!self || windowWidth <= 0 || windowHeight <= 0) return;
    if (margin < 0) margin = 0;
    geo = XPerformanceOverlay_geometry(self);
    switch (position) {
    case XPerformanceOverlayPosition_TopLeft:
        x = margin; y = margin; break;
    case XPerformanceOverlayPosition_TopCenter:
        x = (windowWidth - geo.width) / 2; y = margin; break;
    case XPerformanceOverlayPosition_TopRight:
        x = windowWidth - geo.width - margin; y = margin; break;
    case XPerformanceOverlayPosition_CenterLeft:
        x = margin; y = (windowHeight - geo.height) / 2; break;
    case XPerformanceOverlayPosition_Center:
        x = (windowWidth - geo.width) / 2;
        y = (windowHeight - geo.height) / 2;
        break;
    case XPerformanceOverlayPosition_CenterRight:
        x = windowWidth - geo.width - margin;
        y = (windowHeight - geo.height) / 2;
        break;
    case XPerformanceOverlayPosition_BottomLeft:
        x = margin; y = windowHeight - geo.height - margin; break;
    case XPerformanceOverlayPosition_BottomCenter:
        x = (windowWidth - geo.width) / 2;
        y = windowHeight - geo.height - margin;
        break;
    case XPerformanceOverlayPosition_BottomRight:
        x = windowWidth - geo.width - margin;
        y = windowHeight - geo.height - margin;
        break;
    default:
        return;
    }
    maxX = windowWidth - geo.width;
    maxY = windowHeight - geo.height;
    if (maxX < 0) maxX = 0;
    if (maxY < 0) maxY = 0;
    if (x < 0) x = 0;
    else if (x > maxX) x = maxX;
    if (y < 0) y = 0;
    else if (y > maxY) y = maxY;
    XPerformanceOverlay_setPosition(self, x, y);
}

void XPerformanceOverlay_setMovable(XPerformanceOverlay* self, bool movable)
{
    if (!self) return;
    self->m_movable = movable;
    if (!movable) self->m_dragging = false;
}

bool XPerformanceOverlay_isMovable(const XPerformanceOverlay* self)
{
    return self ? self->m_movable : false;
}

void XPerformanceOverlay_setFixed(XPerformanceOverlay* self, bool fixed)
{
    if (!self) return;
    self->m_fixed = fixed;
    if (fixed) self->m_dragging = false;
}

bool XPerformanceOverlay_isFixed(const XPerformanceOverlay* self)
{
    return self ? self->m_fixed : false;
}

bool XPerformanceOverlay_beginDrag(XPerformanceOverlay* self, int x, int y)
{
    XRect geo;
    if (!self || !self->m_movable || self->m_fixed) return false;
    geo = XPerformanceOverlay_geometry(self);
    if (x < geo.x || y < geo.y || x >= geo.x + geo.width ||
        y >= geo.y + geo.height)
        return false;
    self->m_dragOffsetX = x - geo.x;
    self->m_dragOffsetY = y - geo.y;
    self->m_dragging = true;
    return true;
}

bool XPerformanceOverlay_dragTo(XPerformanceOverlay* self, int x, int y,
                                int windowWidth, int windowHeight)
{
    XRect geo;
    int newX;
    int newY;
    if (!self || !self->m_dragging || self->m_fixed) return false;
    geo = XWidget_geometry((XWidget*)&self->m_base);
    newX = x - self->m_dragOffsetX;
    newY = y - self->m_dragOffsetY;
    if (windowWidth > 0) {
        if (geo.width >= windowWidth) newX = 0;
        else if (newX < 0) newX = 0;
        else if (newX + geo.width > windowWidth)
            newX = windowWidth - geo.width;
    }
    if (windowHeight > 0) {
        if (geo.height >= windowHeight) newY = 0;
        else if (newY < 0) newY = 0;
        else if (newY + geo.height > windowHeight)
            newY = windowHeight - geo.height;
    }
    if (geo.x == newX && geo.y == newY) return false;
    XPerformanceOverlay_setPosition(self, newX, newY);
    return true;
}

void XPerformanceOverlay_endDrag(XPerformanceOverlay* self)
{
    if (self) self->m_dragging = false;
}

bool XPerformanceOverlay_isDragging(const XPerformanceOverlay* self)
{
    return self ? self->m_dragging : false;
}

void XPerformanceOverlay_reset(XPerformanceOverlay* self)
{
    if (!self) return;
    self->m_sampleStartUsecs = 0;
    self->m_sampleUsecs = 0;
    self->m_maxUsecs = 0;
    self->m_sampleFrames = 0;
    self->m_fps = 0.0;
    self->m_frameMs = 0.0;
    self->m_maxFrameMs = 0.0;
    self->m_networkAvailable = false;
    self->m_networkSampleUsecs = 0;
    self->m_networkRxBytes = 0;
    self->m_networkTxBytes = 0;
    self->m_networkRxKbps = 0.0;
    self->m_networkTxKbps = 0.0;
    performanceOverlay_updateText(self);
}

void XPerformanceOverlay_updateFrame(XPerformanceOverlay* self,
                                     int64_t frameStartUsecs,
                                     int64_t nowUsecs)
{
    int64_t frameUsecs;
    int64_t interval;
    int64_t updateUsecs;
    if (!self || frameStartUsecs <= 0 || nowUsecs < frameStartUsecs)
        return;

    /* A caller must provide one monotonic time domain.  If the source was
       reset or moved backwards, discard the partial window before counting
       the first frame in the new domain. */
    if (self->m_sampleStartUsecs > 0 &&
        (frameStartUsecs < self->m_sampleStartUsecs ||
         nowUsecs < self->m_sampleStartUsecs)) {
        self->m_sampleStartUsecs = 0;
        self->m_sampleUsecs = 0;
        self->m_maxUsecs = 0;
        self->m_sampleFrames = 0;
    }

    frameUsecs = nowUsecs - frameStartUsecs;
    if (self->m_sampleStartUsecs <= 0)
        self->m_sampleStartUsecs = frameStartUsecs;
    self->m_sampleUsecs += frameUsecs;
    if (frameUsecs > self->m_maxUsecs)
        self->m_maxUsecs = frameUsecs;
    ++self->m_sampleFrames;
    interval = nowUsecs - self->m_sampleStartUsecs;
#if XGUI_PERFORMANCE_OVERLAY_UPDATE_MS > 0
    updateUsecs = (int64_t)XGUI_PERFORMANCE_OVERLAY_UPDATE_MS * 1000LL;
#else
    updateUsecs = 1;
#endif
    if (interval >= updateUsecs &&
        self->m_sampleFrames > 0) {
        self->m_fps = (double)self->m_sampleFrames * 1000000.0 /
                      (double)interval;
        self->m_frameMs = (double)self->m_sampleUsecs /
                          (double)self->m_sampleFrames / 1000.0;
        self->m_maxFrameMs = (double)self->m_maxUsecs / 1000.0;
        performanceOverlay_updateText(self);
        self->m_sampleStartUsecs = nowUsecs;
        self->m_sampleUsecs = 0;
        self->m_maxUsecs = 0;
        self->m_sampleFrames = 0;
    }
}

void XPerformanceOverlay_updateNetwork(XPerformanceOverlay* self,
                                       bool available,
                                       uint64_t rxBytes,
                                       uint64_t txBytes,
                                       int64_t nowUsecs)
{
    int64_t interval;
    if (!self) return;
    if (!available || nowUsecs <= 0)
    {
        self->m_networkAvailable = false;
        self->m_networkSampleUsecs = 0;
        self->m_networkRxBytes = 0;
        self->m_networkTxBytes = 0;
        self->m_networkRxKbps = 0.0;
        self->m_networkTxKbps = 0.0;
        performanceOverlay_updateText(self);
        return;
    }

    interval = self->m_networkSampleUsecs > 0
                   ? nowUsecs - self->m_networkSampleUsecs : 0;
    if (self->m_networkAvailable && interval > 0)
    {
        uint64_t rxDelta = rxBytes >= self->m_networkRxBytes
                               ? rxBytes - self->m_networkRxBytes : 0;
        uint64_t txDelta = txBytes >= self->m_networkTxBytes
                               ? txBytes - self->m_networkTxBytes : 0;
        self->m_networkRxKbps = (double)rxDelta * 1000000.0 /
                                (double)interval / 1024.0;
        self->m_networkTxKbps = (double)txDelta * 1000000.0 /
                                (double)interval / 1024.0;
    }
    else
    {
        self->m_networkRxKbps = 0.0;
        self->m_networkTxKbps = 0.0;
    }
    self->m_networkAvailable = true;
    self->m_networkSampleUsecs = nowUsecs;
    self->m_networkRxBytes = rxBytes;
    self->m_networkTxBytes = txBytes;
    performanceOverlay_updateText(self);
}

void XPerformanceOverlay_draw(XPerformanceOverlay* self, XPainter* painter)
{
    int x;
    int y;
    XRect geo;
    int width;
    int height;
    XWidget* widget;
    if (!self || !painter) return;
    geo = XPerformanceOverlay_geometry(self);
    x = geo.x;
    y = geo.y;
    width = geo.width;
    height = geo.height;
    if (width <= 0 || height <= 0) return;
    widget = (XWidget*)&self->m_base;
    (void)XWidget_drawContentCached(widget, painter, x, y, width, height,
                                    performanceOverlay_drawContent, self);
}

/** @brief 按内容坐标系（0,0 起点）绘制悬浮层外观。 */
static bool performanceOverlay_drawContent(XWidget* widget,
                                           XPainter* painter,
                                           void* userData)
{
    XPerformanceOverlay* self;
    XRect rect;
    self = (XPerformanceOverlay*)userData;
    if (!self || !painter) return false;
    XRect_init(&rect, 0, 0, XWidget_width(widget), XWidget_height(widget));
    XPainter_fillRect(painter, &rect, self->m_backgroundColor);
    XLabel_drawContents(&self->m_base, painter);
    return true;
}

#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON */
