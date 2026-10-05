/******************************************************************************
 * @file       XPerformanceOverlay.c
 * @brief      XGui 性能悬浮层控件实现。
 ******************************************************************************/
#include "XPerformanceOverlay.h"
#include "XStringUtils.h"

#include "XAlgorithm.h"
#include "XWidget_Protected.h"
#include "XEvent.h"
#include "XFont.h"
#include "XImage.h"
#include "XMemory.h"
#include "XString.h"
#include "XSystem.h"
#include "XWindowEvent.h" /* 触摸手势事件（TouchDragEvent 槽判定认领） */

#if XGUI_PERFORMANCE_OVERLAY_ON && XWIDGET_ON && XFRAME_ON && XLABEL_ON

static bool performanceOverlay_drawContent(XWidget* widget,
                                           XPainter* painter,
                                           void* userData);
static void VXPerformanceOverlay_paintEvent(XWidget* self, XEvent* event);
static void VXPerformanceOverlay_touchDragEvent(XWidget* self, XEvent* event);

/** @brief 网络行文本（默认逐行拼装与格式模板共用）。 */
static size_t performanceOverlay_netText(const XPerformanceOverlay* self,
                                         char* buf, size_t cap)
{
    int n;
    if (!buf || cap == 0u) return 0u;
    if (self->m_networkAvailable) {
        double down = self->m_networkRxKbps;
        double up = self->m_networkTxKbps;
        const char* downUnit = "KB/s";
        const char* upUnit = "KB/s";
        if (down >= 1024.0) {
            down /= 1024.0;
            downUnit = "MB/s";
        }
        if (up >= 1024.0) {
            up /= 1024.0;
            upUnit = "MB/s";
        }
        n = XSnprintf(buf, cap, "下载 %.1f %s 上传 %.1f %s",
                      down, downUnit, up, upUnit);
    } else {
        n = XSnprintf(buf, cap, "下载 无 上传 无");
    }
    if (n <= 0) {
        buf[0] = '\0';
        return 0u;
    }
    return (size_t)n < cap ? (size_t)n : cap - 1u;
}

#if XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON || XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
/** @brief 使用率格式化：负值（无基线/不支持）输出 "-"。 */
static void performanceOverlay_percentText(double value, char* buf,
                                           size_t cap)
{
    if (value < 0.0)
        XSnprintf(buf, cap, "-");
    else
        XSnprintf(buf, cap, "%.1f%%", value);
}
#endif

#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
/** @brief 字节数格式化：自动选 B/KB/MB/GB 单位（1024 进制，同网络速率口径）。 */
static void performanceOverlay_amountText(uint64_t bytes, char* buf,
                                          size_t cap)
{
    double value = (double)bytes;
    if (bytes < 1024u)
        XSnprintf(buf, cap, "%u B", (unsigned)bytes);
    else if (bytes < (1024u * 1024u))
        XSnprintf(buf, cap, "%.1f KB", value / 1024.0);
    else if (bytes < (1024ull * 1024ull * 1024ull))
        XSnprintf(buf, cap, "%.1f MB", value / (1024.0 * 1024.0));
    else
        XSnprintf(buf, cap, "%.1f GB", value / (1024.0 * 1024.0 * 1024.0));
}

/** @brief 准确数文本：已用[/总量]；总量的单位独立换算。 */
static void performanceOverlay_usedTotalText(const XPerformanceOverlay* self,
                                             char* buf, size_t cap)
{
    char usedBuf[32];
    performanceOverlay_amountText(self->m_memoryUsedBytes, usedBuf,
                                  sizeof(usedBuf));
    if (self->m_memoryTotalBytes > 0u) {
        char totalBuf[32];
        performanceOverlay_amountText(self->m_memoryTotalBytes, totalBuf,
                                      sizeof(totalBuf));
        XSnprintf(buf, cap, "%s/%s", usedBuf, totalBuf);
    } else {
        XSnprintf(buf, cap, "%s", usedBuf);
    }
}

/** @brief 内存行文本（默认逐行拼装与格式模板共用）。 */
static size_t performanceOverlay_memoryText(const XPerformanceOverlay* self,
                                            char* buf, size_t cap)
{
    int n;
    if (!buf || cap == 0u) return 0u;
    if (!self->m_memoryValid) {
        n = XSnprintf(buf, cap, "内存 无");
    } else if (self->m_memoryDisplay ==
               XPerformanceOverlayMemoryDisplay_PercentOnly) {
        char percentText[16];
        performanceOverlay_percentText(self->m_memoryPercent, percentText,
                                       sizeof(percentText));
        n = XSnprintf(buf, cap, "内存 %s", percentText);
    } else if (self->m_memoryDisplay ==
               XPerformanceOverlayMemoryDisplay_AmountOnly) {
        char amountText[80];
        performanceOverlay_usedTotalText(self, amountText, sizeof(amountText));
        n = XSnprintf(buf, cap, "内存 %s", amountText);
    } else {
        char amountText[80];
        char percentText[16];
        performanceOverlay_usedTotalText(self, amountText, sizeof(amountText));
        performanceOverlay_percentText(self->m_memoryPercent, percentText,
                                       sizeof(percentText));
        n = XSnprintf(buf, cap, "内存 %s %s", amountText, percentText);
    }
    if (n <= 0) {
        buf[0] = '\0';
        return 0u;
    }
    return (size_t)n < cap ? (size_t)n : cap - 1u;
}

/** @brief 按当前来源采样一次内存数据（OS 平台优先，库内统计兜底）。 */
static void performanceOverlay_sampleMemory(XPerformanceOverlay* self)
{
    XSystemMemoryInfo info;
    bool sampled = false;
    if (self->m_memorySource != XPerformanceOverlayMemorySource_Library &&
        XSystem_memoryInfo(&info)) {
        self->m_memoryUsedBytes = info.usedBytes;
        self->m_memoryTotalBytes = info.totalBytes;
        self->m_memoryPercent =
            info.totalBytes > 0u
                ? 100.0 * (double)info.usedBytes / (double)info.totalBytes
                : -1.0;
        self->m_memoryValid = true;
        sampled = true;
    } else if (self->m_memorySource != XPerformanceOverlayMemorySource_System) {
        /* 库内口径：准确数=系统分配器+内存池在用；百分比只取有固定容量
           的内存池（系统堆无上限，不参与百分比基准）。 */
        XMemoryStatistics stats = XMemory_statistics_2(XMEMORY_TYPE_HYBRID);
        uint64_t percentTotal = (uint64_t)stats.poolTotalBytes;
        self->m_memoryUsedBytes = (uint64_t)stats.systemBytes +
                                  (uint64_t)stats.poolUsedBytes;
        self->m_memoryTotalBytes = 0u;
        self->m_memoryPercent =
            percentTotal > 0u
                ? 100.0 * (double)(uint64_t)stats.poolUsedBytes /
                      (double)percentTotal
                : -1.0;
        self->m_memoryValid = self->m_memoryUsedBytes > 0u ||
                              percentTotal > 0u;
        sampled = true;
    }
    if (!sampled) {
        /* 显式 System 来源且平台不支持：保持最近数据无效显示。 */
        self->m_memoryValid = false;
        self->m_memoryUsedBytes = 0u;
        self->m_memoryTotalBytes = 0u;
        self->m_memoryPercent = -1.0;
    }
}
#endif /* XGUI_PERFORMANCE_OVERLAY_MEMORY_ON */

/** @brief 模板占位符取值；返回 NULL 表示未知占位符（原样保留）。
 *  @details 指标不可见或被宏裁剪时返回空串（模板作者应配合开关写作）。 */
static const char* performanceOverlay_placeholder(
    const XPerformanceOverlay* self, const char* key, size_t keyLen,
    char* scratch, size_t scratchCap)
{
#define KEY_IS(literal) \
    (keyLen == sizeof(literal) - 1u && XMemcmp(key, literal, keyLen) == 0)
    if (KEY_IS("fps")) {
#if XGUI_PERFORMANCE_OVERLAY_FPS_ON
        if (self->m_fpsVisible) {
            XSnprintf(scratch, scratchCap, "%.1f", self->m_fps);
            return scratch;
        }
#endif
        return "";
    }
    if (KEY_IS("framems")) {
#if XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON
        if (self->m_frameTimeVisible) {
            XSnprintf(scratch, scratchCap, "%.2f", self->m_frameMs);
            return scratch;
        }
#endif
        return "";
    }
    if (KEY_IS("maxframems")) {
#if XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON
        if (self->m_frameTimeVisible) {
            XSnprintf(scratch, scratchCap, "%.2f", self->m_maxFrameMs);
            return scratch;
        }
#endif
        return "";
    }
    if (KEY_IS("cpu") || KEY_IS("gpu")) {
#if XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON
        if (self->m_sysStatVisible) {
            performanceOverlay_percentText(
                KEY_IS("cpu") ? self->m_cpuPercent : self->m_gpuPercent,
                scratch, scratchCap);
            return scratch;
        }
#endif
        return "";
    }
    if (KEY_IS("net")) {
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
        if (self->m_networkVisible) {
            performanceOverlay_netText(self, scratch, scratchCap);
            return scratch;
        }
#endif
        return "";
    }
    if (KEY_IS("mem") || KEY_IS("memamount") || KEY_IS("mempercent")) {
#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
        if (self->m_memoryVisible) {
            if (KEY_IS("mem")) {
                performanceOverlay_memoryText(self, scratch, scratchCap);
                return scratch;
            }
            if (self->m_memoryValid) {
                if (KEY_IS("memamount")) {
                    performanceOverlay_usedTotalText(self, scratch,
                                                     scratchCap);
                    return scratch;
                }
                performanceOverlay_percentText(self->m_memoryPercent, scratch,
                                               scratchCap);
                return scratch;
            }
            /* 指标无效：细分占位符与整体 {mem} 的"无"口径保持一致。 */
            XSnprintf(scratch, scratchCap, KEY_IS("mempercent") ? "-" : "无");
            return scratch;
        }
#endif
        return "";
    }
    return NULL;
#undef KEY_IS
}

/** @brief 按自定义模板渲染文本：占位符替换 + 字面 \n 转义为换行。 */
static void performanceOverlay_renderFormat(const XPerformanceOverlay* self,
                                            char* text, size_t cap)
{
    const char* p;
    size_t used = 0u;
    if (!text || cap == 0u) return;
    text[0] = '\0';
    if (!self->m_format) return;
    p = XString_toUtf8(self->m_format);
    if (!p) return;
    while (*p != '\0' && used + 1u < cap) {
        if (p[0] == '\\' && p[1] == 'n') {
            text[used++] = '\n';
            p += 2;
            continue;
        }
        if (p[0] == '{') {
            const char* end = XStrchr(p + 1, '}');
            if (end) {
                char scratch[96];
                const char* value = performanceOverlay_placeholder(
                    self, p + 1, (size_t)(end - (p + 1)), scratch,
                    sizeof(scratch));
                if (value) {
                    size_t valueLen = XStrlen(value);
                    if (used + valueLen >= cap)
                        valueLen = cap - 1u - used;
                    XMemcpy(text + used, value, valueLen);
                    used += valueLen;
                    p = end + 1;
                    continue;
                }
            }
        }
        text[used++] = *p++;
    }
    text[used] = '\0';
}

static void performanceOverlay_updateText(XPerformanceOverlay* self)
{
    char text[256];
    size_t used = 0;
    int n;
    if (!self) return;
    if (self->m_format) {
        performanceOverlay_renderFormat(self, text, sizeof(text));
        goto committed; /* 模板与默认拼装共用收尾（setText+尺寸自适应） */
    }
    text[0] = '\0';
#if XGUI_PERFORMANCE_OVERLAY_FPS_ON
    if (self->m_fpsVisible) {
        n = XSnprintf(text + used, sizeof(text) - used, "FPS %.1f", self->m_fps);
        if (n > 0)
            used += (size_t)n < sizeof(text) - used
                        ? (size_t)n : sizeof(text) - used - 1;
    }
#endif
#if XGUI_PERFORMANCE_OVERLAY_FRAME_TIME_ON
    if (self->m_frameTimeVisible) {
        if (used > 0 && used + 1 < sizeof(text)) text[used++] = '\n';
        n = XSnprintf(text + used, sizeof(text) - used, "帧耗时 %.2f/%.2f ms",
                     self->m_frameMs, self->m_maxFrameMs);
        if (n > 0)
            used += (size_t)n < sizeof(text) - used
                        ? (size_t)n : sizeof(text) - used - 1;
    }
#endif
#if XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON
    if (self->m_sysStatVisible) {
        char cpuText[16];
        char gpuText[16];
        if (used > 0 && used + 1 < sizeof(text)) text[used++] = '\n';
        performanceOverlay_percentText(self->m_cpuPercent, cpuText,
                                       sizeof(cpuText));
        performanceOverlay_percentText(self->m_gpuPercent, gpuText,
                                       sizeof(gpuText));
        n = XSnprintf(text + used, sizeof(text) - used, "CPU %s GPU %s",
                      cpuText, gpuText);
        if (n > 0)
            used += (size_t)n < sizeof(text) - used
                        ? (size_t)n : sizeof(text) - used - 1;
    }
#endif
#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
    if (self->m_memoryVisible) {
        char memText[96];
        size_t memLen;
        if (used > 0 && used + 1 < sizeof(text)) text[used++] = '\n';
        memLen = performanceOverlay_memoryText(self, memText, sizeof(memText));
        if (memLen > 0 && used + memLen < sizeof(text)) {
            XMemcpy(text + used, memText, memLen);
            used += memLen;
        }
    }
#endif
#if XGUI_PERFORMANCE_OVERLAY_NETWORK_ON
    if (self->m_networkVisible) {
        char netText[96];
        size_t netLen;
        if (used > 0 && used + 1 < sizeof(text)) text[used++] = '\n';
        netLen = performanceOverlay_netText(self, netText, sizeof(netText));
        if (netLen > 0 && used + netLen < sizeof(text)) {
            XMemcpy(text + used, netText, netLen);
            used += netLen;
        }
    }
#endif
    if (used == 0)
        (void)XSnprintf(text, sizeof(text), "Perf disabled");
    else
        text[used] = '\0';
committed:
    XLabel_setText_2(&self->m_base, text);
    /* 尺寸自适应（setAutoFitSize 开启时）：宽=最宽行文本宽+左右边距，
       高=文字底边贴住下边框线（下边距归零，保留上 margin 起排）。
       尺寸未变时不 resize，不构成回环；默认关闭，调用方显式 setSize
       不被改写；位置跟随由调用方（重锚）负责。 */
    if (self->m_autoFitSize) {
        XWidget* base = (XWidget*)&self->m_base;
        XMargins cm = XWidget_contentsMargins(base);
        int margin = XLabel_margin(&self->m_base);
        XFont fontCopy = XWidget_font(base);
        int maxLineW = 0;
        const char* line = text;
        int wantW;
        int wantH;
        int curW;
        int curH;
        while (*line != '\0') {
            /* textWidth 遇 '\n' 即停：逐行求最宽行。 */
            int lineW = XPainter_textWidth(&fontCopy, line);
            if (lineW > maxLineW) maxLineW = lineW;
            line = XStrchr(line, '\n');
            if (!line) break;
            ++line;
        }
        XClassDeinit(&fontCopy);
        curW = XWidget_width(base);
        curH = XWidget_height(base);
        wantW = maxLineW > 0 ? maxLineW + margin * 2 + cm.left + cm.right
                             : curW;
        wantH = XLabel_heightForWidth(&self->m_base, wantW) - margin
                - cm.bottom;
        if (wantW > 0 && wantH > 0 && (wantW != curW || wantH != curH))
            XWidget_resize(base, wantW, wantH);
    }
}

/** @brief 触摸手势槽：按住拖动（DragBegin）判定认领——本悬浮层可移
 *         （movable 且未钉死）即接受，框架转左键按住拖动仿真（press 续
 *         持+MOVE 随行+RELEASE 收口），宿主既有的鼠标拖移管线
 *         （beginDrag/dragTo/endDrag）原样驱动；其余手势种类不认领
 *         （tap 等维持合成鼠标语义）。 */
static void VXPerformanceOverlay_touchDragEvent(XWidget* self, XEvent* event)
{
    XPerformanceOverlay* overlay = (XPerformanceOverlay*)self;
    if (!overlay || !event ||
        XEvent_type(event) != XEVENT_TYPE_TOUCH_DRAG)
        return;
    if (XTouchEvent_gesture((const XTouchEvent*)event) !=
        (int)XTouchGesture_DragBegin)
        return;
    if (!overlay->m_movable || overlay->m_fixed)
        return; /* 不可移/钉死：不认领（不构成滚动面的宿主区域回落滚轮）。 */
    XEvent_accept(event);
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
    self->m_cpuPercent = other->m_cpuPercent;
    self->m_gpuPercent = other->m_gpuPercent;
    self->m_memoryUsedBytes = other->m_memoryUsedBytes;
    self->m_memoryTotalBytes = other->m_memoryTotalBytes;
    self->m_memoryPercent = other->m_memoryPercent;
    self->m_memoryValid = other->m_memoryValid;
    if (self->m_format) {
        XClassDelete((XClass*)self->m_format);
        self->m_format = NULL;
    }
    self->m_format = other->m_format
                         ? XString_create_copy(other->m_format)
                         : NULL;
    self->m_backgroundColor = other->m_backgroundColor;
    self->m_fpsVisible = other->m_fpsVisible;
    self->m_frameTimeVisible = other->m_frameTimeVisible;
    self->m_networkVisible = other->m_networkVisible;
    self->m_sysStatVisible = other->m_sysStatVisible;
    self->m_memoryVisible = other->m_memoryVisible;
    self->m_memoryDisplay = other->m_memoryDisplay;
    self->m_memorySource = other->m_memorySource;
    self->m_autoFitSize = other->m_autoFitSize;
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
    self->m_cpuPercent = other->m_cpuPercent;
    self->m_gpuPercent = other->m_gpuPercent;
    self->m_memoryUsedBytes = other->m_memoryUsedBytes;
    self->m_memoryTotalBytes = other->m_memoryTotalBytes;
    self->m_memoryPercent = other->m_memoryPercent;
    self->m_memoryValid = other->m_memoryValid;
    if (self->m_format) XClassDelete((XClass*)self->m_format);
    self->m_format = other->m_format;
    other->m_format = NULL;
    self->m_backgroundColor = other->m_backgroundColor;
    self->m_fpsVisible = other->m_fpsVisible;
    self->m_frameTimeVisible = other->m_frameTimeVisible;
    self->m_networkVisible = other->m_networkVisible;
    self->m_sysStatVisible = other->m_sysStatVisible;
    self->m_memoryVisible = other->m_memoryVisible;
    self->m_memoryDisplay = other->m_memoryDisplay;
    self->m_memorySource = other->m_memorySource;
    self->m_autoFitSize = other->m_autoFitSize;
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
    other->m_cpuPercent = -1.0;
    other->m_gpuPercent = -1.0;
    other->m_memoryUsedBytes = 0;
    other->m_memoryTotalBytes = 0;
    other->m_memoryPercent = -1.0;
    other->m_memoryValid = false;
    other->m_fpsVisible = true;
    other->m_frameTimeVisible = true;
    other->m_networkVisible = true;
    other->m_sysStatVisible = true;
    other->m_memoryVisible = true;
    other->m_memoryDisplay = XPerformanceOverlayMemoryDisplay_Both;
    other->m_memorySource = XPerformanceOverlayMemorySource_Auto;
    other->m_autoFitSize = false;
    other->m_movable = true;
    other->m_fixed = false;
    other->m_dragging = false;
    other->m_dragOffsetX = 0;
    other->m_dragOffsetY = 0;
}

static void VXPerformanceOverlay_deinit(XPerformanceOverlay* self)
{
    if (!self) return;
    if (self->m_format) {
        XClassDelete((XClass*)self->m_format);
        self->m_format = NULL;
    }
    XClass_Deinit_Parent(XLabel, &self->m_base);
}

XVtable* XPerformanceOverlay_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XPerformanceOverlay)
    XVTABLE_INHERIT_XCLASS(XLabel);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXPerformanceOverlay_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXPerformanceOverlay_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXPerformanceOverlay_deinit);
    /* 作为顶层子控件参与控件树绘制：先于兄弟们（业务页面）之下绘制会
       被不透明兄弟（如 GroupBox）盖住，父控件把它 raise 到末位后由本
       入口最后绘制，保证悬浮层永远在最上层。 */
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent,
                             VXPerformanceOverlay_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_TouchDragEvent,
                             VXPerformanceOverlay_touchDragEvent);
    return XVTABLE_DEFAULT;
}

void XPerformanceOverlay_init(XPerformanceOverlay* self, XWidget* parent,
                              XWidgetFlags flags)
{
    XPalette palette;
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    XLabel_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XPerformanceOverlay);
    self->m_backgroundColor = 0xd9000000u;
    self->m_fpsVisible = true;
    self->m_frameTimeVisible = true;
    self->m_networkVisible = true;
    self->m_sysStatVisible = true;
    self->m_memoryVisible = true;
    self->m_memoryDisplay = XPerformanceOverlayMemoryDisplay_Both;
    self->m_memorySource = XPerformanceOverlayMemorySource_Auto;
    self->m_cpuPercent = -1.0;
    self->m_gpuPercent = -1.0;
    self->m_memoryPercent = -1.0;
    self->m_memoryValid = false;
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
    XClassCopy(&font, &source);
    XClassDeinit(&source);
    XFont_setFamily(&font, family);
    XWidget_setFont((XWidget*)&self->m_base, &font);
    XClassDeinit(&font);
    performanceOverlay_updateText(self); /* 字号/行高可能变：重算贴底高度 */
}

void XPerformanceOverlay_setTextPixelSize(XPerformanceOverlay* self,
                                          int pixelHeight)
{
    if (!self) return;
    XLabel_setTextPixelSize(&self->m_base, pixelHeight);
    performanceOverlay_updateText(self); /* 同上：字号变化重算贴底高度 */
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

void XPerformanceOverlay_setSysStatVisible(XPerformanceOverlay* self,
                                           bool visible)
{
#if XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON
    if (!self || self->m_sysStatVisible == visible) return;
    self->m_sysStatVisible = visible;
    if (visible) {
        /* 打开时立即建一次采样基线，下个统计窗口即出真值。 */
        self->m_cpuPercent = XSystem_cpuUsagePercent();
        self->m_gpuPercent = XSystem_gpuUsagePercent();
    }
    performanceOverlay_updateText(self);
#else
    (void)self;
    (void)visible;
#endif
}

bool XPerformanceOverlay_isSysStatVisible(const XPerformanceOverlay* self)
{
#if XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON
    return self ? self->m_sysStatVisible : false;
#else
    (void)self;
    return false;
#endif
}

void XPerformanceOverlay_setMemoryVisible(XPerformanceOverlay* self,
                                          bool visible)
{
#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
    if (!self || self->m_memoryVisible == visible) return;
    self->m_memoryVisible = visible;
    if (visible) {
        /* 打开时立即采样一次，本统计窗口即出真值。 */
        performanceOverlay_sampleMemory(self);
    }
    performanceOverlay_updateText(self);
#else
    (void)self;
    (void)visible;
#endif
}

bool XPerformanceOverlay_isMemoryVisible(const XPerformanceOverlay* self)
{
#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
    return self ? self->m_memoryVisible : false;
#else
    (void)self;
    return false;
#endif
}

void XPerformanceOverlay_setMemoryDisplay(
    XPerformanceOverlay* self, XPerformanceOverlayMemoryDisplay display)
{
#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
    if (!self) return;
    if (display != XPerformanceOverlayMemoryDisplay_AmountOnly &&
        display != XPerformanceOverlayMemoryDisplay_PercentOnly)
        display = XPerformanceOverlayMemoryDisplay_Both;
    if (self->m_memoryDisplay == display) return;
    self->m_memoryDisplay = display;
    performanceOverlay_updateText(self);
#else
    (void)self;
    (void)display;
#endif
}

XPerformanceOverlayMemoryDisplay XPerformanceOverlay_memoryDisplay(
    const XPerformanceOverlay* self)
{
#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
    return self ? self->m_memoryDisplay
                : XPerformanceOverlayMemoryDisplay_Both;
#else
    (void)self;
    return XPerformanceOverlayMemoryDisplay_Both;
#endif
}

void XPerformanceOverlay_setMemorySource(
    XPerformanceOverlay* self, XPerformanceOverlayMemorySource source)
{
#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
    if (!self) return;
    if (source != XPerformanceOverlayMemorySource_System &&
        source != XPerformanceOverlayMemorySource_Library)
        source = XPerformanceOverlayMemorySource_Auto;
    if (self->m_memorySource == source) return;
    self->m_memorySource = source;
    /* 来源切换改变数据口径：立即按新来源重采样。 */
    performanceOverlay_sampleMemory(self);
    performanceOverlay_updateText(self);
#else
    (void)self;
    (void)source;
#endif
}

XPerformanceOverlayMemorySource XPerformanceOverlay_memorySource(
    const XPerformanceOverlay* self)
{
#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
    return self ? self->m_memorySource
                : XPerformanceOverlayMemorySource_Auto;
#else
    (void)self;
    return XPerformanceOverlayMemorySource_Auto;
#endif
}

void XPerformanceOverlay_setAutoFitSize(XPerformanceOverlay* self,
                                          bool enabled)
{
    if (!self || self->m_autoFitSize == enabled) return;
    self->m_autoFitSize = enabled;
    performanceOverlay_updateText(self); /* 开启即按当前文本收框一次 */
}

bool XPerformanceOverlay_isAutoFitSize(const XPerformanceOverlay* self)
{
    return self ? self->m_autoFitSize : false;
}

void XPerformanceOverlay_setFormat(XPerformanceOverlay* self,
                                   const char* format)
{
    if (!self) return;
    if (self->m_format) {
        XClassDelete((XClass*)self->m_format);
        self->m_format = NULL;
    }
    if (format && format[0])
        self->m_format = XString_create_utf8(format);
    performanceOverlay_updateText(self);
}

const char* XPerformanceOverlay_format(const XPerformanceOverlay* self)
{
    if (!self || !self->m_format) return NULL;
    return XString_toUtf8(self->m_format);
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
    self->m_cpuPercent = -1.0;
    self->m_gpuPercent = -1.0;
    self->m_memoryUsedBytes = 0;
    self->m_memoryTotalBytes = 0;
    self->m_memoryPercent = -1.0;
    self->m_memoryValid = false;
#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
    /* 内存为快照型指标（非差值），reset 即采样一次，构造/复位后首帧
       就显示真值；CPU/GPU 等需两次采样的指标仍按统计窗口出值。 */
    if (self->m_memoryVisible)
        performanceOverlay_sampleMemory(self);
#endif
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
#if XGUI_PERFORMANCE_OVERLAY_SYSSTAT_ON
        /* CPU/GPU 与 FPS 同节奏采样（250ms 统计窗口一次）。updateText
           还会被每帧的网络采样触发，放那里会把间隔缩到帧级——短间隔
           差值噪声大且常为 0，显示被覆盖成恒 0。 */
        if (self->m_sysStatVisible) {
            self->m_cpuPercent = XSystem_cpuUsagePercent();
            self->m_gpuPercent = XSystem_gpuUsagePercent();
        }
#endif
#if XGUI_PERFORMANCE_OVERLAY_MEMORY_ON
        /* 内存为快照型指标（非差值），同样按统计窗口节奏采样。 */
        if (self->m_memoryVisible)
            performanceOverlay_sampleMemory(self);
#endif
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

/** @brief 绘制事件：在控件树中按自身几何绘制（ paintTree 已裁剪到脏区）。 */
static void VXPerformanceOverlay_paintEvent(XWidget* self, XEvent* event)
{
    XImage* image;
    XPoint offset;
    XPainter painter;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
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
    (void)XWidget_drawContentCached(self, &painter, 0, 0,
                                    XWidget_width(self),
                                    XWidget_height(self),
                                    performanceOverlay_drawContent, self);
    XPainter_end(&painter);
    XPainter_deinit(&painter);
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
