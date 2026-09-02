/******************************************************************************
 * @file       XPerformanceOverlay.c
 * @brief      XGui 性能悬浮层控件实现。
 ******************************************************************************/
#include "XPerformanceOverlay.h"
#include "XFont.h"
#include "XMemory.h"
#include <stdio.h>
#include <string.h>

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON

static void performanceOverlay_updateText(XPerformanceOverlay* self)
{
    char text[160];
    size_t used = 0;
    int n;
    if (!self) return;
    text[0] = '\0';
#if XGUI_PERFORMANCE_OVERLAY_FPS_ON
    n = snprintf(text + used, sizeof(text) - used, "FPS %.1f", self->m_fps);
    if (n > 0)
        used += (size_t)n < sizeof(text) - used
                    ? (size_t)n : sizeof(text) - used - 1;
#endif
#if XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON
    if (used > 0 && used + 1 < sizeof(text)) text[used++] = '\n';
    n = snprintf(text + used, sizeof(text) - used, "Frame %.2f/%.2f ms",
                 self->m_frameMs, self->m_maxFrameMs);
    if (n > 0)
        used += (size_t)n < sizeof(text) - used
                    ? (size_t)n : sizeof(text) - used - 1;
#endif
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
    if (used > 0 && used + 1 < sizeof(text)) text[used++] = '\n';
    if (self->m_networkAvailable)
        n = snprintf(text + used, sizeof(text) - used,
                     "NET RX %.1f TX %.1f KB/s",
                     self->m_networkRxKbps, self->m_networkTxKbps);
    else
        n = snprintf(text + used, sizeof(text) - used,
                     "NET RX n/a TX n/a");
    if (n > 0)
        used += (size_t)n < sizeof(text) - used
                    ? (size_t)n : sizeof(text) - used - 1;
#endif
    if (used == 0)
        (void)snprintf(text, sizeof(text), "Perf disabled");
    else
        text[used] = '\0';
    XWidget_setUpdatesEnabled((XWidget*)&self->m_base, false);
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
    self->m_overlayWidth = other->m_overlayWidth;
    self->m_overlayHeight = other->m_overlayHeight;
    self->m_overlayMargin = other->m_overlayMargin;
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
    self->m_overlayWidth = other->m_overlayWidth;
    self->m_overlayHeight = other->m_overlayHeight;
    self->m_overlayMargin = other->m_overlayMargin;
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
    self->m_overlayWidth = 180;
    self->m_overlayHeight = 60;
    self->m_overlayMargin = 8;
    XWidget_setUpdatesEnabled((XWidget*)&self->m_base, false);
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
    XWidget_setGeometry((XWidget*)&self->m_base, 0, 0,
                        self->m_overlayWidth, self->m_overlayHeight);
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

void XPerformanceOverlay_setTextPixelSize(XPerformanceOverlay* self,
                                          int pixelHeight)
{
    if (!self) return;
    XLabel_setTextPixelSize(&self->m_base, pixelHeight);
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
    if (!self || frameStartUsecs <= 0 || nowUsecs < frameStartUsecs)
        return;
    frameUsecs = nowUsecs - frameStartUsecs;
    if (self->m_sampleStartUsecs <= 0)
        self->m_sampleStartUsecs = frameStartUsecs;
    self->m_sampleUsecs += frameUsecs;
    if (frameUsecs > self->m_maxUsecs)
        self->m_maxUsecs = frameUsecs;
    ++self->m_sampleFrames;
    interval = nowUsecs - self->m_sampleStartUsecs;
    if (interval >= (int64_t)XGUI_PERFORMANCE_OVERLAY_UPDATE_MS * 1000LL &&
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

void XPerformanceOverlay_draw(XPerformanceOverlay* self, XPainter* painter,
                              int windowWidth, int windowHeight)
{
    int x;
    int y;
    XRect geo;
    if (!self || !painter || windowWidth <= 0 || windowHeight <= 0) return;
    x = windowWidth - self->m_overlayWidth - self->m_overlayMargin;
    y = self->m_overlayMargin;
    if (x < self->m_overlayMargin) x = self->m_overlayMargin;
    if (y + self->m_overlayHeight > windowHeight)
        y = windowHeight - self->m_overlayHeight - self->m_overlayMargin;
    if (y < self->m_overlayMargin) y = self->m_overlayMargin;
    geo = XWidget_geometry((XWidget*)&self->m_base);
    if (geo.x != x || geo.y != y || geo.width != self->m_overlayWidth ||
        geo.height != self->m_overlayHeight) {
        XWidget_setUpdatesEnabled((XWidget*)&self->m_base, false);
        XWidget_setGeometry((XWidget*)&self->m_base, x, y,
                            self->m_overlayWidth, self->m_overlayHeight);
    }
    {
        XRect rect = { x, y, self->m_overlayWidth, self->m_overlayHeight };
        XPainter_fillRect(painter, &rect, self->m_backgroundColor);
    }
    if (XPainter_save(painter)) {
        XPainter_translate(painter, (float)x, (float)y);
        XLabel_drawContents(&self->m_base, painter);
        XPainter_restore(painter);
    }
}

#endif /* XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON */
