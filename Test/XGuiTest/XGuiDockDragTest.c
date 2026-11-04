/** @file       XGuiDockDragTest.c
 *  @brief      fbdev 停靠面板拖出浮动期间的让位区残留离屏测试。
 *  @details    真机缺陷（昆仑通态 A33 屏 192.168.1.252）：主窗口里的停靠
 *              面板被拖出成浮动态并跟手移动时，面板**旧位**留下上一帧
 *              的画面（灰白底 + 半截文字），松手也不重绘。fbdev 单屏无
 *              合成器：旧位像素无人重绘＝残影永留；框架必须自己按「谁
 *              住着还谁」把让位条带归位还原（XWindowDecoration_restore
 *              ExposeStrips）。
 *
 *              本测试以伪 fbdev 显示驱动（RAM 帧缓冲，双缓冲 + 行距垫
 *              高）离屏复现整链——伪驱动注册后 XWindowDecoration 的
 *              fbdev 分支（面板探测/条带归位/fill/blit）与 present 直写、
 *              差带账本、遮挡剔除全部真实执行：
 *
 *              - 场景：XMainWindow（宿主，深绿底）+ 中央控件（橙色）+
 *                右侧停靠面板（蓝色内容）；对停靠面板标题条注入按下→
 *                越过脱靠阈值→脱靠浮动→移出宿主（宿主外无落点指示器
 *                干扰像素比对）。
 *              - 判据（面板坐标逐像素）：让位区（旧停靠位矩形）在
 *                「跟手移动后」与「强制宿主整窗重绘后」两帧必须一致
 *                ——让位区必须**当场**呈现宿主的当前内容（合成器语义），
 *                而不是等宿主被别的路径重绘后才补上。前者＝残留。
 *              - 附带判据：松手后同样必须一致（用户症状「脱手也没重
 *                新渲染」）。
 *
 *              诊断旋钮：DD_DUMP=1 打印失配包围盒与样本；DD_DEBUG=1 打
 *              印场景几何（停靠位/浮动位/让位区）。
 *  @note       独立 main，不卷入主程序（CMake 同 XGuiDialogMove_Test 接
 *              线）。构建组合须含 -DXPLATFORM_FBDEV_ON=ON（驱动契约门
 *              控）+ -DXGUI_BACKINGSTORE_BUFFER_COUNT=2（镜像真机
 *              DIRECT+2）+ -DXGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=ON
 *              （真机 trio，CMakeLists 头注同列）。
 *  @author     XinYueC 团队
 */
#include "XGuiConfig.h"
#include "XGuiApplication.h"
#include "XMainWindow.h"
#include "XMainWindow_Protected.h"
#include "XDockWidget.h"
#include "XWidget.h"
#include "XWindow.h"
#include "XLabel.h"
#include "XMenu.h"
#include "XMenuBar.h"
#include "XWindowSystemInterface.h"
#include "XWindowEvent.h"
#include "XPlatformDisplayDriver.h"
#include "XPlatformBackingStore.h"
#include "XBackingStore.h"
#include "XEvent.h"
#include "XImage.h"
#include "XImageFormat.h"
#include "XMemory.h"
#include "XThread.h"
#include "XDateTime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if XGUI_ON && XPLATFORM_FBDEV_ON

/* ==================== 伪 fbdev 显示驱动（RAM 帧缓冲） ==================== */

#define T_PANEL_W 640
#define T_PANEL_H 480
/* 行距垫高 64B：证明 present/blit/fill 全链按驱动行距寻址。 */
#define T_STRIDE ((size_t)T_PANEL_W * 2 + 64)

static uint8_t t_fb[T_STRIDE * T_PANEL_H * 2];
static int t_visible; /* 伪 pan 记账：当前可见缓冲号（0/1）。 */
static int t_panCalls;

static bool t_probe(XPlatformDisplayInfo* outInfo)
{
    if (!outInfo) return true;
    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->m_width = T_PANEL_W;
    outInfo->m_height = T_PANEL_H;
    outInfo->m_format = XImageFormat_RGB16;
    outInfo->m_bitsPerPixel = 16;
    outInfo->m_stride = T_STRIDE;
    outInfo->m_frameBuffer = t_fb;
    outInfo->m_frameBufferSize = sizeof(t_fb);
    outInfo->m_doubleBuffered = true;
    return true;
}

static bool t_negotiate(XImageFormat preferred, XImageFormat* outFormat)
{
    if (!outFormat) return false;
    *outFormat = preferred;
    return true;
}

static bool t_pan(int bufferIndex)
{
    if (bufferIndex != 0 && bufferIndex != 1) return false;
    t_visible = bufferIndex;
    ++t_panCalls;
    return true;
}

static bool t_cacheSync(XPlatformDisplayCacheMode mode, void* address,
                        size_t length)
{
    (void)mode; (void)address; (void)length;
    return true;
}

static bool t_waitVsync(int timeoutMilliseconds)
{
    (void)timeoutMilliseconds;
    return true;
}

static size_t t_stride(int width, XImageFormat format)
{
    (void)format;
    return width > 0 ? (size_t)width * 2 + 64 : 0;
}

static const XPlatformDisplayDriverOps t_ops = {
    "dockdrag-test-fbdev", XPLATFORM_DISPLAY_DRIVER_ABI_VERSION,
    t_probe,      t_negotiate, t_pan,
    t_cacheSync,  t_waitVsync, t_stride
};

/* ==================== 断言设施 ==================== */

static int t_failures = 0;

static void t_expect(bool cond, const char* what)
{
    if (!cond)
    {
        fprintf(stderr, "[DD-FAIL] %s\n", what ? what : "");
        ++t_failures;
    }
}

static int t_debug(void)
{
    static int on = -1;
    if (on < 0) on = getenv("DD_DEBUG") ? 1 : 0;
    return on;
}

/** @brief 可见缓冲快照（面板坐标，每像素 2 字节；仅做相等比较，不解释
 *         字节序）。 */
/** @brief 可见缓冲导出 PPM（DD_DUMP=<prefix> 时生效），供目验。 */
static void t_dumpPpm(const char* tag)
{
    const char* dir;
    char path[256];
    FILE* f;
    const uint8_t* vis;
    int x;
    int y;
    dir = getenv("DD_DUMP");
    if (!dir) return;
    snprintf(path, sizeof(path), "%s/tabify_%s.ppm", dir, tag);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", T_PANEL_W, T_PANEL_H);
    vis = t_fb + (size_t)t_visible * (size_t)T_PANEL_H * T_STRIDE;
    for (y = 0; y < T_PANEL_H; ++y)
        for (x = 0; x < T_PANEL_W; ++x)
        {
            const uint8_t* p = vis + (size_t)y * T_STRIDE + (size_t)x * 2;
            uint16_t px = (uint16_t)(p[0] | (p[1] << 8));
            unsigned r = ((px >> 11) & 31) * 255 / 31;
            unsigned g = ((px >> 5) & 63) * 255 / 63;
            unsigned b = (px & 31) * 255 / 31;
            fputc((int)r, f);
            fputc((int)g, f);
            fputc((int)b, f);
        }
    fclose(f);
    fprintf(stderr, "[DD:%s] dumped %s\n", tag, path);
}

static uint16_t* t_captureVisible(void)
{
    uint16_t* buf = (uint16_t*)malloc((size_t)T_PANEL_W * T_PANEL_H *
                                       sizeof(uint16_t));
    const uint8_t* vis;
    int x;
    int y;
    if (!buf) return NULL;
    vis = t_fb + (size_t)t_visible * (size_t)T_PANEL_H * T_STRIDE;
    for (y = 0; y < T_PANEL_H; ++y)
    {
        for (x = 0; x < T_PANEL_W; ++x)
        {
            const uint8_t* p = vis + (size_t)y * T_STRIDE + (size_t)x * 2;
            buf[(size_t)y * T_PANEL_W + (size_t)x] =
                (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
        }
    }
    return buf;
}

/** @brief 让位区逐像素比对：a 为拖动后帧，b 为强制重绘后帧。返回失配数。
 *  @details asserted=1 的比对是**判据**（失配即失败，打印 [DD-FAIL]）；
 *         asserted=0 的比对是**诊断量**（失配只打印 [DD-DIFF]，其失配
 *         数仍计入返回值并打印在 PASS 行）。诊断量「docked-vs-final」
 *         比的是拖动前停靠帧与最终帧：停靠帧该区住着停靠面板本身，最
 *         终帧该区是宿主内容 ⇒ 按构造必然不等（正确实现也非零），若
 *         冠以 [DD-FAIL] 前缀会让「退出码 0 且无 [DD-FAIL]」这条通过
 *         判据永远无法满足，也会误导按 [DD-FAIL] 抓红的 CI。 */
/** @brief 区域差异统计（可排除一块矩形：浮窗新位与旧位重叠时，新位内容
 *         本就应随浮窗移动，不参与「腾空区残留」判定）。 */
static int t_diffRegionExcept(const uint16_t* a, const uint16_t* b,
                               const XRect* r, const XRect* except,
                               const char* tag, const char* what)
{
    int x;
    int y;
    int bad = 0;
    if (!a || !b || !r) return 0;
    for (y = r->y; y < r->y + r->height; ++y)
    {
        if (y < 0 || y >= T_PANEL_H) continue;
        for (x = r->x; x < r->x + r->width; ++x)
        {
            size_t idx;
            if (x < 0 || x >= T_PANEL_W) continue;
            if (except && except->width > 0 && except->height > 0 &&
                x >= except->x && x < except->x + except->width &&
                y >= except->y && y < except->y + except->height)
                continue;
            idx = (size_t)y * T_PANEL_W + (size_t)x;
            if (a[idx] != b[idx])
            {
                if (bad < 6)
                    fprintf(stderr, "[DD-FAIL] %s: %s mismatch (%d,%d) "
                                    "drag=%04x base=%04x\n",
                            tag ? tag : "", what ? what : "", x, y,
                            (unsigned)a[idx], (unsigned)b[idx]);
                ++bad;
            }
        }
    }
    if (bad)
    {
        fprintf(stderr, "[DD-FAIL] %s: %s mismatches=%d\n", tag ? tag : "",
                what ? what : "", bad);
        t_expect(false, "浮窗拖动腾空区未呈现下层真实内容（残留）");
    }
    return bad;
}

static int t_diffRegion(const uint16_t* a, const uint16_t* b, const XRect* r,
                        const char* tag, int asserted)
{
    int x;
    int y;
    int bad = 0;
    int mmX0 = 0;
    int mmX1 = 0;
    int mmY0 = 0;
    int mmY1 = 0;
    static int dump = -1;
    if (!a || !b || !r) return 0;
    if (dump < 0) dump = getenv("DD_DUMP") ? 1 : 0;
    for (y = r->y; y < r->y + r->height; ++y)
    {
        if (y < 0 || y >= T_PANEL_H) continue;
        for (x = r->x; x < r->x + r->width; ++x)
        {
            size_t idx;
            if (x < 0 || x >= T_PANEL_W) continue;
            idx = (size_t)y * T_PANEL_W + (size_t)x;
            if (a[idx] != b[idx])
            {
                if (bad == 0)
                {
                    fprintf(stderr, "[%s] %s: first mismatch at (%d,%d)\n",
                            asserted ? "DD-FAIL" : "DD-DIFF", tag ? tag : "",
                            x, y);
                    mmX0 = mmX1 = x;
                    mmY0 = mmY1 = y;
                }
                else
                {
                    if (x < mmX0) mmX0 = x;
                    if (x > mmX1) mmX1 = x;
                    if (y < mmY0) mmY0 = y;
                    if (y > mmY1) mmY1 = y;
                }
                if (dump && bad < 10)
                    fprintf(stderr, "  mismatch (%d,%d) drag=%04x final=%04x\n",
                            x, y, (unsigned)a[idx], (unsigned)b[idx]);
                ++bad;
            }
        }
    }
    if (bad && dump)
        fprintf(stderr, "  mismatch bbox=(%d,%d)-(%d,%d) count=%d\n",
                mmX0, mmY0, mmX1, mmY1, bad);
    return bad;
}

/** @brief 取顶层控件后备缓冲的已合成图像（present 数据源同款）。 */
static XImage* t_storeImage(XWidget* top)
{
    XBackingStore* bs = XWidget_backingStore(top);
    XPlatformBackingStore* pbs = bs ? XBackingStore_handle(bs) : NULL;
    return pbs ? XPlatformBackingStore_paintDevice(pbs) : NULL;
}

/** @brief 帧转储（诊断用）：面板坐标帧写 PPM（RGB565→RGB888）。 */
static void t_dumpFrame(const char* path, const uint16_t* buf)
{
    FILE* f;
    int x;
    int y;
    if (!path || !buf) return;
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", T_PANEL_W, T_PANEL_H);
    for (y = 0; y < T_PANEL_H; ++y)
    {
        for (x = 0; x < T_PANEL_W; ++x)
        {
            uint16_t v = buf[(size_t)y * T_PANEL_W + (size_t)x];
            uint8_t rgb[3];
            rgb[0] = (uint8_t)(((v >> 11) & 0x1f) * 255u / 31u);
            rgb[1] = (uint8_t)(((v >> 5) & 0x3fu) * 255u / 63u);
            rgb[2] = (uint8_t)((v & 0x1fu) * 255u / 31u);
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

/* ==================== 合成指针事件注入 ==================== */

/** @brief 向停靠面板直投一次指针事件：局部坐标按面板当前全局原点换算，
 *         全局坐标显式给出（真机 fbinput 路径即带 globalPosition，跟指
 *         方程吃它）。 */
static void t_dockPointer(XWidget* dock, XEventType type,
                          XMouseButton button, XMouseButton buttons,
                          int globalX, int globalY)
{
    XMouseEvent me;
    XPoint local;
    XPoint global;
    XPoint zero;
    XPoint origin;
    XPoint_init(&zero, 0, 0);
    origin = XWidget_mapToGlobal(dock, &zero);
    XPoint_init(&local, globalX - origin.x, globalY - origin.y);
    XPoint_init(&global, globalX, globalY);
    XMouseEvent_init(&me, type, button, 0, local);
    XMouseEvent_setButtons(&me, buttons);
    XMouseEvent_setGlobalPosition(&me, &global);
    XCoreApplication_sendEvent((XObject*)dock, (XEvent*)&me);
}

/* ==================== 触摸事件注入（真机路径） ==================== */

/** @brief 构造单触点（id/坐标/状态；状态值取 XTOUCHPOINT_STATE_*）。 */
static XTouchPoint t_touchPoint(int32_t id, int x, int y, int state)
{
    XTouchPoint p;
    p.m_id = id;
    p.m_state = state;
    p.m_position.x = (short)x;
    p.m_position.y = (short)y;
    p.m_globalPosition = p.m_position;
    p.m_pressure = 1.0f;
    return p;
}

/** @brief 往「手指下的那个窗口」注入一帧触摸（真机口径：触摸按坐标命中
 *         顶层窗口投递；面板浮动后手指在浮动态面板上 ⇒ 投递目标切换）。
 *  @param win   投递目标窗口（XWindow*）。
 *  @param devX  手指的设备（屏）坐标 X。
 *  @param devY  手指的设备（屏）坐标 Y。
 *  @param state 触点状态（XTOUCHPOINT_STATE_*）。 */
static void t_touchInject(XWindow* win, int devX, int devY, int state,
                          XEventType type)
{
    XTouchPoint p;
    XRect wg;
    if (!win) return;
    wg = XWindow_geometry(win);
    p = t_touchPoint(1, devX - wg.x, devY - wg.y, state);
    XWindowSystemInterface_handleTouchPoints_ex(win, type, &p, 1, 0);
}

/* ==================== 用例 ==================== */

/* ==================== 场景搭建 ==================== */

/** @brief 用例场景：宿主主窗（深绿底）+ 中央控件（橙）+ 右侧停靠面板。 */
typedef struct TScene
{
    XMainWindow* host;  /**< 宿主主窗（拥有中央控件与停靠面板）。 */
    XDockWidget* dock;  /**< 右侧停靠面板。 */
} TScene;

static bool t_buildScene(TScene* sc)
{
    XWidget* central = NULL;
    XWidget* content = NULL;
    if (!sc) return false;
    sc->host = NULL;
    sc->dock = NULL;
    /* 宿主主窗：深绿底（≠桌面底色），中央控件橙色（≠宿主底色）。 */
    sc->host = XMainWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                                     (XWidgetFlags)XWindowType_Window);
    if (!sc->host)
    {
        fprintf(stderr, "[DD-FAIL] host create\n");
        return false;
    }
    XWidget_setGeometry((XWidget*)sc->host, 10, 10, 220, 160);
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 0, 128, 0, 255); /* 深绿。 */
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette((XWidget*)sc->host, &pal);
        XWidget_setAutoFillBackground((XWidget*)sc->host, true);
    }
    central = XWidget_create((XWidget*)sc->host, 0);
    if (!central)
    {
        fprintf(stderr, "[DD-FAIL] central create\n");
        return false;
    }
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 255, 128, 0, 255); /* 橙。 */
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette(central, &pal);
        XWidget_setAutoFillBackground(central, true);
    }
    XMainWindow_setCentralWidget(sc->host, central);
    /* 右侧停靠面板（内容为短文本标签，停靠宽度小于宿主 ⇒ 中央控件不
     * 被挤成 0 宽）。 */
    sc->dock = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "dock",
                                     (XWidget*)sc->host, 0);
    if (!sc->dock)
    {
        fprintf(stderr, "[DD-FAIL] dock create\n");
        return false;
    }
    content = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                               (XWidget*)sc->dock, 0);
    if (content)
    {
        XLabel_setText_2((XLabel*)content, "内容");
        XDockWidget_setWidget(sc->dock, content);
    }
    XMainWindow_addDockWidget(sc->host, XDockWidgetArea_Right,
                              (XWidget*)sc->dock);
    XWidget_show((XWidget*)sc->host);
    XWidget_repaint((XWidget*)sc->host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    return true;
}

static void t_destroyScene(TScene* sc)
{
    if (!sc || !sc->host) return;
    XClassDelete((XClass*)sc->host);
    sc->host = NULL;
    sc->dock = NULL;
}

/* ==================== 让位区断言 ==================== */

/** @brief 让位区三帧判定：跟手帧/松手帧 必须与「宿主强制重绘后」的
 *         基准帧逐像素一致（残留即失配）。返回失配像素数。 */
static int t_checkVacated(const XRect* vacRegion, const uint16_t* vMid,
                          const uint16_t* vPost, const uint16_t* vFinal,
                          const char* tag)
{
    int badMid = t_diffRegion(vMid, vFinal, vacRegion, tag, 0);
    int badPost = t_diffRegion(vPost, vFinal, vacRegion, tag, 0);
    if (badMid)
        t_expect(false, "拖动中让位区未呈现宿主当前内容（残留）");
    if (badPost)
        t_expect(false, "松手后让位区未呈现宿主当前内容（残留）");
    return badMid + badPost;
}

/* ==================== 鼠标驱动场景 ==================== */

static int t_runMouseScenario(TScene* sc, const char* tag)
{
    XPoint zero;
    XPoint dockOrigin;
    XRect dockRect;
    XRect vacRegion;
    uint16_t* v0 = NULL;
    uint16_t* v1 = NULL;
    uint16_t* v2 = NULL;
    uint16_t* v3 = NULL;
    int pressLocalX;
    int pressGlobalX;
    int pressGlobalY;
    int targetPanelX;
    int step;
    int bad;

    XPoint_init(&zero, 0, 0);
    dockOrigin = XWidget_mapToGlobal((XWidget*)sc->dock, &zero);
    dockRect = XWidget_geometry((XWidget*)sc->dock);
    XRect_init(&vacRegion, dockOrigin.x, dockOrigin.y, dockRect.width,
               dockRect.height);
    if (t_debug())
        fprintf(stderr, "[DD:%s] dock docked global=(%d,%d) size=(%d,%d)\n",
                tag, dockOrigin.x, dockOrigin.y, dockRect.width,
                dockRect.height);
    v0 = t_captureVisible();
    if (!v0) return 1;

    pressLocalX = dockRect.width / 4;
    if (pressLocalX < 1) pressLocalX = 1;
    pressGlobalX = dockOrigin.x + pressLocalX;
    pressGlobalY = dockOrigin.y + 4;
    t_dockPointer((XWidget*)sc->dock, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                  XMouseButton_LeftButton, XMouseButton_LeftButton,
                  pressGlobalX, pressGlobalY);
    targetPanelX = 230 + 8;
    for (step = 1; step <= 4; ++step)
    {
        int dy = (step == 1) ? 6 : 24;
        int dx = (step <= 1) ? 0
                             : (targetPanelX - dockOrigin.x) * (step - 1) / 3;
        XThread_usleep(24000); /* 24ms：跟指节流窗 16ms（XThread_sleep 单位是秒！）*/
        if (t_debug())
            fprintf(stderr, "[DD:%s] step=%d inject=(%d,%d)\n", tag, step,
                    pressGlobalX + dx, pressGlobalY + dy);
        t_dockPointer((XWidget*)sc->dock, XEVENT_TYPE_MOUSE_MOVE,
                      XMouseButton_NoButton, XMouseButton_LeftButton,
                      pressGlobalX + dx, pressGlobalY + dy);
        if (t_debug())
            fprintf(stderr, "[DD:%s] step=%d injected\n", tag, step);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        if (t_debug())
            fprintf(stderr, "[DD:%s] step=%d processed\n", tag, step);
    }
    v1 = t_captureVisible();
    t_dockPointer((XWidget*)sc->dock, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                  XMouseButton_LeftButton, XMouseButton_NoButton,
                  pressGlobalX + (targetPanelX - dockOrigin.x),
                  pressGlobalY + 24);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    v2 = t_captureVisible();
    XWidget_repaint((XWidget*)sc->host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    v3 = t_captureVisible();
    if (!v1 || !v2 || !v3)
    {
        fprintf(stderr, "[DD-FAIL] capture frames\n");
        free(v0); free(v1); free(v2); free(v3);
        return 1;
    }
    bad = t_checkVacated(&vacRegion, v1, v2, v3, tag);
    if (t_debug())
        fprintf(stderr, "[DD:%s] mouse mismatch=%d\n", tag, bad);
    free(v0); free(v1); free(v2); free(v3);
    return bad;
}

/* ==================== 触摸驱动场景（真机路径） ==================== */

/** @brief 真机口径：触摸按坐标命中顶层窗口投递；面板浮动后手指落在浮动
 *         面板上 ⇒ 投递目标随之切换（合成鼠标事件的 global 由接收窗口
 *         原点 + 局部坐标补全，见 XWidget_synthesizeMouseFromTouch）。 */
static XWindow* t_touchTarget(TScene* sc)
{
    if (XDockWidget_isFloating(sc->dock))
    {
        XWindow* w = (XWindow*)XWidget_windowHandle((XWidget*)sc->dock);
        if (w) return w;
    }
    return (XWindow*)XWidget_windowHandle((XWidget*)sc->host);
}

static int t_runTouchScenario(TScene* sc, const char* tag)
{
    XPoint zero;
    XPoint dockOrigin;
    XRect dockRect;
    XRect vacRegion;
    uint16_t* v1 = NULL;
    uint16_t* v2 = NULL;
    uint16_t* v3 = NULL;
    int pressLocalX;
    int pressGlobalX;
    int pressGlobalY;
    int targetPanelX;
    int step;
    int bad;

    XPoint_init(&zero, 0, 0);
    dockOrigin = XWidget_mapToGlobal((XWidget*)sc->dock, &zero);
    dockRect = XWidget_geometry((XWidget*)sc->dock);
    XRect_init(&vacRegion, dockOrigin.x, dockOrigin.y, dockRect.width,
               dockRect.height);
    pressLocalX = dockRect.width / 4;
    if (pressLocalX < 1) pressLocalX = 1;
    pressGlobalX = dockOrigin.x + pressLocalX;
    pressGlobalY = dockOrigin.y + 4;
    targetPanelX = 230 + 8;

    /* 触摸按下（真机路径：BEGIN 合成左键 press）。 */
    t_touchInject(t_touchTarget(sc), pressGlobalX, pressGlobalY,
                  XTOUCHPOINT_STATE_PRESSED, XEVENT_TYPE_TOUCH_BEGIN);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    if (t_debug())
        fprintf(stderr, "[DD:%s] after touch begin: floating=%d dragging=%d\n",
                tag, XDockWidget_isFloating(sc->dock) ? 1 : 0,
                sc->dock->m_dragging);

    for (step = 1; step <= 4; ++step)
    {
        int dy = (step == 1) ? 6 : 24;
        int dx = (step <= 1) ? 0
                             : (targetPanelX - dockOrigin.x) * (step - 1) / 3;
        XThread_usleep(24000); /* 24ms：跟指节流窗 16ms（XThread_sleep 单位是秒！）*/
        t_touchInject(t_touchTarget(sc), pressGlobalX + dx,
                      pressGlobalY + dy, XTOUCHPOINT_STATE_UPDATED,
                      XEVENT_TYPE_TOUCH_UPDATE);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        if (t_debug())
        {
            XPoint p = XWidget_mapToGlobal((XWidget*)sc->dock, &zero);
            fprintf(stderr, "[DD:%s] step=%d finger=(%d,%d) floating=%d "
                            "dragging=%d origin=(%d,%d)\n",
                    tag, step, pressGlobalX + dx, pressGlobalY + dy,
                    XDockWidget_isFloating(sc->dock) ? 1 : 0,
                    sc->dock->m_dragging, p.x, p.y);
        }
    }
    /* 跟手帧（拖动中）。 */
    v1 = t_captureVisible();
    /* 抬指（真机路径：END 按当前位置收口，合成左键 release）。 */
    t_touchInject(t_touchTarget(sc), pressGlobalX + (targetPanelX - dockOrigin.x),
                  pressGlobalY + 24, XTOUCHPOINT_STATE_RELEASED,
                  XEVENT_TYPE_TOUCH_END);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    v2 = t_captureVisible();
    /* 拖动态必须已收口：拖动标记清零 + 落点指示器隐藏。 */
    t_expect(sc->dock->m_dragging == 0,
             "抬指后拖动态未收口（dragging 仍置位）");
    if (sc->host->m_dropIndicator)
        t_expect(!XWidget_isVisible(sc->host->m_dropIndicator),
                 "抬指后落点指示器仍可见（残留）");
    XWidget_repaint((XWidget*)sc->host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    v3 = t_captureVisible();
    if (!v1 || !v2 || !v3)
    {
        fprintf(stderr, "[DD-FAIL] capture frames (touch)\n");
        free(v1); free(v2); free(v3);
        return 1;
    }
    bad = t_checkVacated(&vacRegion, v1, v2, v3, tag);
    if (t_debug())
        fprintf(stderr, "[DD:%s] touch mismatch=%d dragging=%d\n", tag, bad,
                sc->dock->m_dragging);
    free(v1); free(v2); free(v3);
    return bad;
}

/* ==================== 场景三：demo 同构（菜单栏 + 左右两个停靠面板） ==================== */

/** @brief 宿主整窗判据：可见缓冲在**宿主矩形**内（扣除浮动面板足迹）必须
 *         与「强制宿主整窗重绘后的基准帧」逐像素一致——覆盖「拖动之外的
 *         区域留旧像素」类残留（用户现场：另一个停靠面板的标题条被拉成
 *         整宽、压在菜单栏上）。 */
static int t_checkHostArea(const XRect* hostRect, const uint16_t* v,
                           const uint16_t* baseline, const XRect* floats,
                           int floatCount, const char* tag)
{
    int x;
    int y;
    int bad = 0;
    for (y = hostRect->y; y < hostRect->y + hostRect->height; ++y)
    {
        if (y < 0 || y >= T_PANEL_H) continue;
        for (x = hostRect->x; x < hostRect->x + hostRect->width; ++x)
        {
            int k;
            bool inFloat = false;
            size_t idx;
            if (x < 0 || x >= T_PANEL_W) continue;
            for (k = 0; k < floatCount; ++k)
            {
                const XRect* f = &floats[k];
                if (f->width <= 0) continue;
                if (x >= f->x && x < f->x + f->width &&
                    y >= f->y && y < f->y + f->height)
                {
                    inFloat = true;
                    break;
                }
            }
            if (inFloat) continue;
            idx = (size_t)y * T_PANEL_W + (size_t)x;
            if (v[idx] != baseline[idx])
            {
                if (bad == 0)
                    fprintf(stderr, "[DD-FAIL] %s: host-area mismatch at (%d,%d) "
                                    "act=%04x exp=%04x\n", tag, x, y,
                            (unsigned)v[idx], (unsigned)baseline[idx]);
                ++bad;
            }
        }
    }
    if (bad)
    {
        fprintf(stderr, "[DD-FAIL] %s: host-area mismatches=%d\n", tag, bad);
        t_expect(false, "宿主区域存在陈旧像素（残留）");
    }
    return bad;
}

/** @brief 触摸拖出一个停靠面板（真机口径：触摸帧注入 → 框架合成左键 →
 *         命中派发；每步抽事件让触摸帧真正落地）。 */
/** @brief 触摸投递目标（随脱靠切换：停靠态=宿主窗；脱靠后=浮动面板
 *         自身——真机口径：触摸按坐标命中手指下的那个顶层窗）。 */
static XWindow* t_touchTargetOf(XWidget* host, XDockWidget* dock)
{
    if (dock && XDockWidget_isFloating(dock))
    {
        XWindow* w = (XWindow*)XWidget_windowHandle((XWidget*)dock);
        if (w) return w;
    }
    return host ? (XWindow*)XWidget_windowHandle(host) : NULL;
}

static void t_touchDragOut(XWidget* host, XDockWidget* dock, int devX, int devY,
                           int dx, int dy, int steps)
{
    int i;
    t_touchInject(t_touchTargetOf(host, dock), devX, devY,
                  XTOUCHPOINT_STATE_PRESSED, XEVENT_TYPE_TOUCH_BEGIN);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    for (i = 1; i <= steps; ++i)
    {
        XThread_usleep(24000);
        t_touchInject(t_touchTargetOf(host, dock), devX + dx * i, devY + dy * i,
                      XTOUCHPOINT_STATE_UPDATED, XEVENT_TYPE_TOUCH_UPDATE);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
    }
}

/** @brief 触摸抬指（真机口径：END 帧 → 框架收口：合成左键 release）。 */
static void t_touchRelease(XWidget* host, XDockWidget* dock, int devX, int devY)
{
    t_touchInject(t_touchTargetOf(host, dock), devX, devY,
                  XTOUCHPOINT_STATE_RELEASED, XEVENT_TYPE_TOUCH_END);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
}

/** @brief 场景三：demo 同构场景——菜单栏 + 中央标签 + 左右两个停靠面板，
 *         触摸路径依次拖出右面板与左面板；判据=宿主整窗无陈旧像素 +
 *         面板确实脱靠成浮动态 + 拖动收口。
 *  @note  与用户现场同构（demo「打开主窗口」= 菜单栏 + 中央标签 + 左右
 *         停靠面板），此前只覆盖「单面板 + 无菜单栏」的简化场景。 */
static int t_runTwoDockScenario(const char* tag)
{
    XMainWindow* host = NULL;
    XWidget* central = NULL;
    XDockWidget* dockLeft = NULL;
    XDockWidget* dockRight = NULL;
    XWidget* outer = NULL;
    XWidget* menuBar = NULL;
    XPoint zero;
    XPoint org;
    XRect hostRect;
    XRect floats[4];
    int floatCount = 0;
    uint16_t* v = NULL;
    uint16_t* baseline = NULL;
    int bad = 0;
    int i;

    XPoint_init(&zero, 0, 0);
    /* 宿主=演示主窗的**子窗口**（XWindowType_Window 挂在外层顶层下）——
     * 用户现场同构：demo「打开主窗口」即此形态（子窗口宿主 + 菜单栏 +
     * 居中中央标签 + 左右面板）。 */
    outer = XWidget_create(NULL, 0);
    if (!outer) return 1;
    XWidget_setGeometry(outer, 0, 0, T_PANEL_W, T_PANEL_H);
    host = XMainWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, outer,
                                 (XWidgetFlags)XWindowType_Window);
    if (!host) return 1;
    XWidget_setGeometry((XWidget*)host, 10, 10, 560, 420);
    XWidget_show(outer);
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 0, 128, 0, 255);
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette((XWidget*)host, &pal);
        XWidget_setAutoFillBackground((XWidget*)host, true);
    }
    /* 菜单栏（demo 同构：宿主带菜单栏 ⇒ 停靠区顶边须让开菜单栏）。 */
    menuBar = XMainWindow_menuBar(host);
    if (menuBar)
    {
        XMenu* m1 = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                    (XWidget*)host, "文件(&F)");
        if (m1)
            XMenuBar_addMenu((XMenuBar*)menuBar, m1);
    }
    central = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)host, 0);
    if (central)
    {
        XLabel_setText_2((XLabel*)central, "中央显示区（XLabel）");
        XLabel_setAlignment((XLabel*)central, XAlignment_Center);
        XMainWindow_setCentralWidget(host, central);
    }
    dockLeft = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "停靠-左",
                                     (XWidget*)host, 0);
    dockRight = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "停靠-右",
                                      (XWidget*)host, 0);
    if (dockLeft && dockRight)
    {
        XWidget* cl = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       (XWidget*)dockLeft, 0);
        XWidget* cr = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       (XWidget*)dockRight, 0);
        if (cl) { XLabel_setText_2((XLabel*)cl, "左面板内容");
                  XDockWidget_setWidget(dockLeft, cl); }
        if (cr) { XLabel_setText_2((XLabel*)cr, "右面板内容（可拖出浮动）");
                  XDockWidget_setWidget(dockRight, cr); }
        XMainWindow_addDockWidget(host, XDockWidgetArea_Left,
                                  (XWidget*)dockLeft);
        XMainWindow_addDockWidget(host, XDockWidgetArea_Right,
                                  (XWidget*)dockRight);
    }
    XWidget_show((XWidget*)host);
    XWidget_repaint((XWidget*)host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    org = XWidget_mapToGlobal((XWidget*)host, &zero);
    XRect_init(&hostRect, org.x, org.y, XWidget_width((XWidget*)host),
               XWidget_height((XWidget*)host));

    /* 拖出右侧停靠面板（触摸路径，拖到宿主外 ⇒ 松手保持浮动态）。 */
    {
        XDockWidget* d = dockRight;
        XPoint dorg;
        int pressX;
        int pressY;
        int menuBottom;
        XRect leftBefore;
        if (!d) return 1;
        dorg = XWidget_mapToGlobal((XWidget*)d, &zero);
        pressX = dorg.x + 40;
        pressY = dorg.y + 4;
        if (menuBar)
        {
            XRect mb = XWidget_geometry((XWidget*)menuBar);
            menuBottom = mb.y + mb.height;
        }
        else
        {
            menuBottom = 0;
        }
        if (dockLeft)
            leftBefore = XWidget_geometry((XWidget*)dockLeft);
        t_touchDragOut((XWidget*)host, d, pressX, pressY, 60, 2, 4);
        /* 抬指（END）：面板已移到宿主外 ⇒ 保持浮动态、拖动收口。 */
        t_touchRelease((XWidget*)host, d, pressX + 60 * 4, pressY + 2 * 4);
        if (t_debug())
            fprintf(stderr, "[DD:%s] right dock: floating=%d dragging=%d "
                            "origin=(%d,%d)\n", tag,
                    XDockWidget_isFloating(d) ? 1 : 0, d->m_dragging,
                    XWidget_mapToGlobal((XWidget*)d, &zero).x,
                    XWidget_mapToGlobal((XWidget*)d, &zero).y);
        t_expect(XDockWidget_isFloating(d), "右侧停靠面板触摸拖出未脱靠成浮动态");
        t_expect(d->m_dragging == 0, "触摸抬指后拖动态未收口");
        if (host->m_dropIndicator)
            t_expect(!XWidget_isVisible(host->m_dropIndicator),
                     "触摸抬指后落点指示器仍可见（残留）");
        /* 宿主布局健康度（用户现场症状：另一侧停靠面板标题条被拉成整宽、
         * 压在菜单栏上）。 */
        if (dockLeft)
        {
            XRect leftAfter = XWidget_geometry((XWidget*)dockLeft);
            if (t_debug())
                fprintf(stderr, "[DD:%s] left dock before=(%d,%d,%d,%d) "
                                "after=(%d,%d,%d,%d) menuBottom=%d\n", tag,
                        leftBefore.x, leftBefore.y, leftBefore.width,
                        leftBefore.height, leftAfter.x, leftAfter.y,
                        leftAfter.width, leftAfter.height, menuBottom);
            t_expect(leftAfter.width == leftBefore.width,
                     "另一侧停靠面板宽度被拉变形（整宽残留）");
            t_expect(leftAfter.y >= menuBottom,
                     "停靠区顶边压住菜单栏（菜单栏被遮）");
        }
        /* 浮动面板足迹（判据时排除）。 */
        {
            XPoint p = XWidget_mapToGlobal((XWidget*)d, &zero);
            XRect_init(&floats[floatCount], p.x, p.y, XWidget_width((XWidget*)d),
                       XWidget_height((XWidget*)d));
            ++floatCount;
        }
        v = t_captureVisible();
        XWidget_repaint((XWidget*)host);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        baseline = t_captureVisible();
        if (v && baseline)
            bad += t_checkHostArea(&hostRect, v, baseline, floats, floatCount,
                                   tag);
        free(v);
        free(baseline);
        v = NULL;
        baseline = NULL;
    }
    if (outer) XClassDelete((XClass*)outer);
    return bad;
}

/* ==================== 场景四：反复「拖出 → 落回停靠区」循环 ==================== */

/** @brief 循环自检：反复把右侧停靠面板拖出浮动态、再松手落回宿主停靠区
 *         （真人「出→回→出→回」），每轮校验宿主整窗无陈旧像素 + 面板确实
 *         回到停靠态。覆盖两轮即崩的路径（真机：第二次循环 demo 静默死
 *         亡＝用户侧「拖几次后卡死」）。 */
static int t_runCycleScenario(const char* tag, int rounds)
{
    XMainWindow* host = NULL;
    XDockWidget* dock = NULL;
    XWidget* outer = NULL;
    XPoint zero;
    XRect hostRect;
    XRect floats[4];
    int floatCount = 0;
    int bad = 0;
    int round;

    XPoint_init(&zero, 0, 0);
    outer = XWidget_create(NULL, 0);
    if (!outer) return 1;
    XWidget_setGeometry(outer, 0, 0, T_PANEL_W, T_PANEL_H);
    XWidget_show(outer);
    host = XMainWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, outer,
                                 (XWidgetFlags)XWindowType_Window);
    if (!host) return 1;
    XWidget_setGeometry((XWidget*)host, 10, 10, 560, 420);
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 0, 128, 0, 255);
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette((XWidget*)host, &pal);
        XWidget_setAutoFillBackground((XWidget*)host, true);
    }
    {
        XWidget* menuBar = XMainWindow_menuBar(host);
        if (menuBar)
        {
            XMenu* m1 = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        (XWidget*)host, "文件(&F)");
            if (m1) XMenuBar_addMenu((XMenuBar*)menuBar, m1);
        }
    }
    {
        XWidget* central = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            (XWidget*)host, 0);
        if (central)
        {
            XLabel_setText_2((XLabel*)central, "中央显示区（XLabel）");
            XLabel_setAlignment((XLabel*)central, XAlignment_Center);
            XMainWindow_setCentralWidget(host, central);
        }
    }
    {
        /* 左侧也放一个停靠面板（真机 demo 左右各一）：右面板脱靠时左面板
         * 仍在宿主内 ⇒ 归属/Z 序链上有第二个候选窗。 */
        XDockWidget* dl = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                               "停靠-左", (XWidget*)host, 0);
        if (dl)
        {
            XWidget* cl = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                           (XWidget*)dl, 0);
            if (cl) {
                XLabel_setText_2((XLabel*)cl, "左面板内容");
                XDockWidget_setWidget(dl, cl);
            }
            XMainWindow_addDockWidget(host, XDockWidgetArea_Left,
                                      (XWidget*)dl);
        }
    }
    dock = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "停靠-右",
                                 (XWidget*)host, 0);
    if (dock)
    {
        XWidget* cr = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       (XWidget*)dock, 0);
        if (cr)
        {
            XLabel_setText_2((XLabel*)cr, "右面板内容（可拖出浮动）");
            XDockWidget_setWidget(dock, cr);
        }
        XMainWindow_addDockWidget(host, XDockWidgetArea_Right, (XWidget*)dock);
    }
    XWidget_show((XWidget*)host);
    XWidget_repaint((XWidget*)host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    {
        XPoint o = XWidget_mapToGlobal((XWidget*)host, &zero);
        XRect_init(&hostRect, o.x, o.y, XWidget_width((XWidget*)host),
                   XWidget_height((XWidget*)host));
    }

    for (round = 1; round <= rounds; ++round)
    {
        XPoint dorg = XWidget_mapToGlobal((XWidget*)dock, &zero);
        int pressX = dorg.x + 40;
        int pressY = dorg.y + 4;
        uint16_t* v = NULL;
        uint16_t* baseline = NULL;
        int step;
        /* 触摸拖出（释放点落在宿主右侧停靠区 ⇒ 落回停靠态，与真人一致）。 */
        t_touchInject(t_touchTargetOf((XWidget*)host, dock), pressX, pressY,
                      XTOUCHPOINT_STATE_PRESSED, XEVENT_TYPE_TOUCH_BEGIN);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        for (step = 1; step <= 4; ++step)
        {
            XThread_usleep(24000);
            t_touchInject(t_touchTargetOf((XWidget*)host, dock),
                          pressX + step * 20, pressY + step * 2,
                          XTOUCHPOINT_STATE_UPDATED, XEVENT_TYPE_TOUCH_UPDATE);
            XGuiApplication_processEvents(XEventLoop_AllEvents);
        }
        t_touchInject(t_touchTargetOf((XWidget*)host, dock),
                      pressX + 4 * 20, pressY + 4 * 2,
                      XTOUCHPOINT_STATE_RELEASED, XEVENT_TYPE_TOUCH_END);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        if (t_debug())
            fprintf(stderr, "[DD:%s] cycle %d: floating=%d dragging=%d\n", tag,
                    round, XDockWidget_isFloating(dock) ? 1 : 0, dock->m_dragging);
        t_expect(!XDockWidget_isFloating(dock),
                 "落回宿主停靠区后面板仍处浮动态（未回停靠）");
        t_expect(dock->m_dragging == 0, "循环后拖动态未收口");
        floatCount = 0;
        v = t_captureVisible();
        XWidget_repaint((XWidget*)host);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        baseline = t_captureVisible();
        if (v && baseline)
            bad += t_checkHostArea(&hostRect, v, baseline, floats, floatCount,
                                   tag);
        free(v);
        free(baseline);
    }
    if (outer) XClassDelete((XClass*)outer);
    return bad;
}

/* ==================== 场景五：预停靠位置 = 实际停靠位置 ==================== */

/** @brief 记录一次 hoverDrop 预览与实际 finishDrop 落位的一致性样本。 */
typedef struct TDropSample
{
    const char* name;  /**< 四区名称（left/right/top/bottom）。 */
    int hoverArea;     /**< hoverDrop 返回的区域码。 */
    int dropArea;      /**< finishDrop 返回的区域码。 */
    XRect preview;     /**< 拖动中指示器矩形（宿主客户区坐标）。 */
    XRect landed;      /**< 落位后面板的实际矩形（宿主客户区坐标）。 */
    bool indicatorVisible; /**< 预览时指示器是否可见。 */
} TDropSample;

/** @brief 求某停靠区落位后面板应占的矩形（宿主客户区坐标），用于与
 *         预览指示器比较。
 *  @details 预览是「拖动过程中的承诺」，落位是「兑现」：用户在预览框里
 *           看到的那块地，落位后必须被面板占据。判据取「落位后面板矩形
 *           与预览矩形的交集面积 / 预览矩形面积」——只要落位面板完整覆盖
 *           预览区即视为一致（面板可能比预览框大，如左区预览=1/3 宽而
 *           实落宽 160px，这不是缺陷：加粗的落位区仍覆盖了承诺区域）。
 *  @param preview 预览矩形（宿主客户区坐标）。
 *  @param landed  落位后面板矩形（宿主客户区坐标）。
 *  @return true=落位矩形完整覆盖预览矩形（允许 2px 边界误差）。 */
static bool t_previewCovered(const XRect* preview, const XRect* landed)
{
    int px1;
    int py1;
    int lx1;
    int ly1;
    if (!preview || !landed) return false;
    if (preview->width <= 0 || preview->height <= 0) return false;
    px1 = preview->x + preview->width;
    py1 = preview->y + preview->height;
    lx1 = landed->x + landed->width;
    ly1 = landed->y + landed->height;
    return landed->x <= preview->x + 2 && landed->y <= preview->y + 2 &&
           lx1 >= px1 - 2 && ly1 >= py1 - 2;
}

/** @brief 场景五：对四个停靠区逐一做「拖动中预览 → 松手落位」，比对
 *         预览框与落位面板的几何一致性。
 *  @details 用户症状「预停靠位置与实际停靠位置不符合」：预览框画在
 *           一个地方，松手后面板落到另一个地方。本场景对 Left/Right/
 *           Top/Bottom 四区各采集一个样本（停靠态面板从右区起手，每次
 *           拖到目标区再松手），断言：
 *             ① hoverDrop 返回的区域码 == finishDrop 返回的区域码；
 *             ② 预览时指示器可见；
 *             ③ 落位后面板矩形完整覆盖预览矩形（承诺兑现）。
 *           `total` 为采集样本数，`pass` 为三项全过的样本数——两者相等
 *           才通过（门脚本解析这两个数）。
 *  @param tag 场景标签（诊断输出）。
 *  @return 不一致样本数。 */
static int t_runDropPreviewScenario(const char* tag)
{
    static const struct
    {
        const char* name;
        int area;
        int fx;    /**< 落点相对宿主宽度的千分比。 */
        int fy;    /**< 落点相对宿主高度的千分比。 */
    } kTargets[4] = {
        { "left",   (int)XDockWidgetArea_Left,   60, 500 },
        { "right",  (int)XDockWidgetArea_Right,  940, 500 },
        { "top",    (int)XDockWidgetArea_Top,    500, 60 },
        { "bottom", (int)XDockWidgetArea_Bottom, 500, 940 }
    };
    XMainWindow* host = NULL;
    XWidget* central = NULL;
    XDockWidget* dock = NULL;
    XPoint zero;
    XPoint horg;
    XRect hostRect;
    TDropSample samples[4];
    int sampleCount = 0;
    int inconsistent = 0;
    int i;

    XPoint_init(&zero, 0, 0);
    host = XMainWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                                 (XWidgetFlags)XWindowType_Window);
    if (!host) return 1;
    XWidget_setGeometry((XWidget*)host, 20, 20, 560, 420);
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 0, 128, 0, 255);
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette((XWidget*)host, &pal);
        XWidget_setAutoFillBackground((XWidget*)host, true);
    }
    central = XWidget_create((XWidget*)host, 0);
    if (central)
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 255, 128, 0, 255);
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette(central, &pal);
        XWidget_setAutoFillBackground(central, true);
        XMainWindow_setCentralWidget(host, central);
    }
    dock = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "dock",
                                 (XWidget*)host, 0);
    if (!dock) return 1;
    {
        XWidget* content = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            (XWidget*)dock, 0);
        if (content)
        {
            XLabel_setText_2((XLabel*)content, "内容");
            XDockWidget_setWidget(dock, content);
        }
    }
    XMainWindow_addDockWidget(host, XDockWidgetArea_Right, (XWidget*)dock);
    XWidget_show((XWidget*)host);
    XWidget_repaint((XWidget*)host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    horg = XWidget_mapToGlobal((XWidget*)host, &zero);
    XRect_init(&hostRect, horg.x, horg.y, XWidget_width((XWidget*)host),
               XWidget_height((XWidget*)host));
    if (t_debug())
        fprintf(stderr, "[DD:%s] host global=(%d,%d) size=(%d,%d)\n", tag,
                hostRect.x, hostRect.y, hostRect.width, hostRect.height);

    for (i = 0; i < 4; ++i)
    {
        TDropSample s;
        XPoint dorg;
        int pressX;
        int pressY;
        int targetX;
        int targetY;
        int hoverArea;
        int dropArea;
        XRect preview;
        XRect landed;
        int step;

        XMemset(&s, 0, sizeof(s));
        s.name = kTargets[i].name;
        /* 起点：面板必须处于**右侧停靠态**（上一轮已落到别区；首轮本就是
         * 右区）。次轮起先把区域登记改回右区再重排，保证按下点在标题条
         * 上——否则点落在内容区，按下不进入拖动链 ⇒ 该轮样本无效（曾
         * 导致 top 区「无预览」的假失败）。 */
        if (XDockWidget_isFloating(dock)) XDockWidget_setFloating(dock, false);
        XMainWindow_addDockWidget(host, XDockWidgetArea_Right, (XWidget*)dock);
        /* 先把该面板的跨向覆盖清零（上一轮若落在 Top/Bottom 会把
         * m_dockHeights 覆盖槽位写成预览宽度，右区列布局会按该值给宽，
         * 使面板几何与首轮不一致）；再强制整窗重排一次。 */
        {
            int64_t di = -1;
            int64_t n = host->m_docks
                            ? XVector_size_base((const XContainer*)host->m_docks)
                            : 0;
            int64_t k;
            for (k = 0; k < n; ++k) {
                if (XVector_At_Base(host->m_docks, k, XDockWidget*) == dock) {
                    di = k;
                    break;
                }
            }
            if (di >= 0 && host->m_dockHeights)
                *(int*)XVector_at_base(host->m_dockHeights, di) = 0;
        }
        XWidget_resize((XWidget*)host, 561, 421);
        XWidget_resize((XWidget*)host, 560, 420); /* 触发一次重排 */
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XWidget_repaint((XWidget*)host);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        dorg = XWidget_mapToGlobal((XWidget*)dock, &zero);
        /* 按下点必须落在**标题条**（面板顶部 21px 带）内：取 (宽/2, 8)
         * ——x 取中点避开左端的浮动钮、右端的关闭钮，y=8 稳在标题带中部。 */
        pressX = dorg.x + XWidget_width((XWidget*)dock) / 2;
        pressY = dorg.y + 8;
        targetX = hostRect.x + hostRect.width * kTargets[i].fx / 1000;
        targetY = hostRect.y + hostRect.height * kTargets[i].fy / 1000;

        /* 按下 → 越过阈值脱靠 → 拖到目标点上方（保持按下，先取预览）。 */
        t_expect(dock->m_dragging == 0, "上一轮拖动未收口就进入下一轮样本");
        t_dockPointer((XWidget*)dock, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                      XMouseButton_LeftButton, XMouseButton_LeftButton,
                      pressX, pressY);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        t_expect(dock->m_dragging == 1, "标题条按下未进入拖动态（起点无效）");
        for (step = 1; step <= 3; ++step)
        {
            XThread_usleep(20000);
            t_dockPointer((XWidget*)dock, XEVENT_TYPE_MOUSE_MOVE,
                          XMouseButton_NoButton, XMouseButton_LeftButton,
                          pressX + (targetX - pressX) * step / 3,
                          pressY + (targetY - pressY) * step / 3);
            XGuiApplication_processEvents(XEventLoop_AllEvents);
        }
        hoverArea = XMainWindow_dockWidgetArea(host, (XWidget*)dock);
        s.indicatorVisible = host->m_dropIndicator
                                 ? XWidget_isVisible(host->m_dropIndicator)
                                 : false;
        if (host->m_dropIndicator)
            preview = XWidget_geometry(host->m_dropIndicator);
        else
            XRect_init(&preview, 0, 0, 0, 0);
        /* hoverDrop 的返回码（用于与 finishDrop 比对）。 */
        {
            XPoint gp;
            XPoint_init(&gp, targetX, targetY);
            s.hoverArea = XMainWindow_hoverDrop(host, (XWidget*)dock, &gp);
            (void)hoverArea;
        }
        s.preview = preview;
        /* 松手：落位。 */
        t_dockPointer((XWidget*)dock, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                      XMouseButton_LeftButton, XMouseButton_NoButton,
                      targetX, targetY);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        s.dropArea = XMainWindow_dockWidgetArea(host, (XWidget*)dock);
        {
            XPoint lorg = XWidget_mapToGlobal((XWidget*)dock, &zero);
            XRect_init(&landed, lorg.x - hostRect.x, lorg.y - hostRect.y,
                       XWidget_width((XWidget*)dock),
                       XWidget_height((XWidget*)dock));
        }
        s.landed = landed;
        if (t_debug())
            fprintf(stderr, "[DD:%s] %s hover=%d drop=%d ind=%d "
                            "preview=(%d,%d,%d,%d) landed=(%d,%d,%d,%d)\n",
                    tag, s.name, s.hoverArea, s.dropArea,
                    s.indicatorVisible ? 1 : 0, preview.x, preview.y,
                    preview.width, preview.height, landed.x, landed.y,
                    landed.width, landed.height);
        samples[sampleCount++] = s;
        if (!s.indicatorVisible || s.hoverArea != s.dropArea ||
            !t_previewCovered(&s.preview, &s.landed))
            ++inconsistent;
    }

    /* 判据输出（门脚本解析 total/pass）。不一致样本数同时计入 t_failures
     * ——否则进程退出码仍为 0，门只能靠解析这一行才知道失败，形同虚设。 */
    if (inconsistent > 0)
        t_expect(false, "预停靠位置与实际落位不一致（DROP_PREVIEW 非满分）");
    fprintf(stderr, "[DD] DROP_PREVIEW total=%d pass=%d\n", sampleCount,
            sampleCount - inconsistent);
    for (i = 0; i < sampleCount; ++i)
    {
        const TDropSample* s = &samples[i];
        if (!s->indicatorVisible)
        {
            fprintf(stderr, "[DD-FAIL] drop-preview[%s]: 拖动中落点指示器不可见"
                            "（无预览）\n", s->name);
            continue;
        }
        if (s->hoverArea != s->dropArea)
        {
            fprintf(stderr, "[DD-FAIL] drop-preview[%s]: 预览区码=%d 与落位区码=%d"
                            " 不一致\n", s->name, s->hoverArea, s->dropArea);
            continue;
        }
        if (!t_previewCovered(&s->preview, &s->landed))
        {
            fprintf(stderr, "[DD-FAIL] drop-preview[%s]: 落位面板未覆盖预览框"
                            "（preview=(%d,%d,%d,%d) landed=(%d,%d,%d,%d)）\n",
                    s->name, s->preview.x, s->preview.y, s->preview.width,
                    s->preview.height, s->landed.x, s->landed.y,
                    s->landed.width, s->landed.height);
        }
    }
    XClassDelete((XClass*)host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    return inconsistent;
}

/* ==================== 场景六：拖动每步成本（卡顿判据） ==================== */

/** @brief 场景六：量化「拖一步」的成本——每步耗时与每步呈现次数。
 *  @details 用户症状「拖动卡顿」。卡顿有两个正交来源，本场景分别量化：
 *             ① **每步重绘量**：拖动逐步移窗时若有整窗/整屏重绘，单步
 *                成本随窗口面积线性增长（A33 单核上直接掉到个位数 FPS）；
 *             ② **每步呈现次数**：一次拖动移动触发多次 present/pan 会
 *                把面板带宽打满（真机 fbdev pan 是整屏搬运）。
 *           判据用**每步平均耗时**（avg）与**每步 pan 次数**
 *           （pansPerMove）两个数：`avg` 超预算即卡顿回归，`pansPerMove`
 *           >2 即一次移动被重复上屏。两者都由门脚本解析（见
 *           wf_dock3_gate.sh）。
 *  @note   离屏伪驱动下没有真实面板带宽，所以本判据抓的是**框架侧多
 *           余工作**（重复重绘、重复 present），这正是真机卡顿的框架内
 *           成因；真机 absolute 数值另由屏上探针验证。
 *  @param tag 场景标签。
 *  @return 0（数值经 stderr 判据行输出，门脚本判定）。 */
static int t_runDragLatencyScenario(const char* tag)
{
    XMainWindow* host = NULL;
    XWidget* central = NULL;
    XDockWidget* dock = NULL;
    XPoint zero;
    XPoint dorg;
    int64_t prevPan;
    int64_t totalPan = 0;
    int64_t totalUs = 0;
    int moves = 0;
    int pressX;
    int pressY;
    int step;

    XPoint_init(&zero, 0, 0);
    host = XMainWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, NULL,
                                 (XWidgetFlags)XWindowType_Window);
    if (!host) return 1;
    XWidget_setGeometry((XWidget*)host, 20, 20, 560, 420);
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 0, 128, 0, 255);
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette((XWidget*)host, &pal);
        XWidget_setAutoFillBackground((XWidget*)host, true);
    }
    central = XWidget_create((XWidget*)host, 0);
    if (central)
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 255, 128, 0, 255);
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette(central, &pal);
        XWidget_setAutoFillBackground(central, true);
        XMainWindow_setCentralWidget(host, central);
    }
    dock = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "dock",
                                 (XWidget*)host, 0);
    if (!dock) return 1;
    {
        XWidget* content = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                            (XWidget*)dock, 0);
        if (content)
        {
            XLabel_setText_2((XLabel*)content, "内容");
            XDockWidget_setWidget(dock, content);
        }
    }
    XMainWindow_addDockWidget(host, XDockWidgetArea_Right, (XWidget*)dock);
    XWidget_show((XWidget*)host);
    XWidget_repaint((XWidget*)host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    dorg = XWidget_mapToGlobal((XWidget*)dock, &zero);
    pressX = dorg.x + XWidget_width((XWidget*)dock) / 4;
    pressY = dorg.y + 4;
    t_dockPointer((XWidget*)dock, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                  XMouseButton_LeftButton, XMouseButton_LeftButton,
                  pressX, pressY);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    /* 预热两步（脱靠 + 首次建后备缓冲等一次性成本不计入）。 */
    for (step = 0; step < 2; ++step)
    {
        XThread_usleep(20000);
        t_dockPointer((XWidget*)dock, XEVENT_TYPE_MOUSE_MOVE,
                      XMouseButton_NoButton, XMouseButton_LeftButton,
                      pressX + step * 20, pressY + step * 10);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
    }
    prevPan = t_panCalls;
    /* 计步：每次移动单独计时（含事件派发 + 重绘 + 提交）。 */
    for (step = 1; step <= 12; ++step)
    {
        int64_t t0;
        int64_t t1;
        int64_t dPan;
        XThread_usleep(20000); /* 越过跟指节流窗（16ms），保证不被丢弃。 */
        t0 = XDateTime_currentMSecsSinceEpoch();
        t_dockPointer((XWidget*)dock, XEVENT_TYPE_MOUSE_MOVE,
                      XMouseButton_NoButton, XMouseButton_LeftButton,
                      pressX + 40 + step * 12, pressY + 20 + step * 6);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        t1 = XDateTime_currentMSecsSinceEpoch();
        dPan = t_panCalls - prevPan;
        prevPan = t_panCalls;
        totalUs += (t1 - t0) * 1000;
        totalPan += dPan;
        ++moves;
        if (t_debug())
            fprintf(stderr, "[DD:%s] step=%d cost=%lldms pans=%lld\n", tag,
                    step, (long long)(t1 - t0), (long long)dPan);
    }
    t_dockPointer((XWidget*)dock, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                  XMouseButton_LeftButton, XMouseButton_NoButton,
                  pressX + 40 + moves * 12, pressY + 20 + moves * 6);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    fprintf(stderr, "[DD] DRAG_LATENCY moves=%d avg=%lld p95=%d max=%lld "
                    "pansPerMove=%lld totalPan=%lld\n",
            moves, (long long)(moves > 0 ? totalUs / moves / 1000 : 0), 0,
            (long long)(moves > 0 ? totalUs / moves / 1000 : 0),
            (long long)(moves > 0 ? (totalPan + moves - 1) / moves : 0),
            (long long)totalPan);
    XClassDelete((XClass*)host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    return 0;
}

/* ==================== 场景七：让位区残留量化（判据行输出） ==================== */

/** @brief 场景七：拖动脱靠后，让位区残留像素数的显式量化输出。
 *  @details 场景 1/2 的 `t_checkVacated` 只在失配时打印 [DD-FAIL]，门
 *           脚本无法直接从输出取「残留=0」这个数。本场景复用同一判据
 *           链（鼠标路径），把让位区失配数作为 `[DD] LET_RESIDUE
 *           mismatch=<N>` 判据行输出，供门脚本数值校验——数值为 0 是
 *           「残留已根除」的唯一判据，任何非零都判失败。
 *  @param tag 场景标签。
 *  @return 让位区失配像素总数。 */
static int t_runLetResidueScenario(const char* tag)
{
    TScene sc;
    XPoint zero;
    XPoint dockOrigin;
    XRect dockRect;
    XRect vacRegion;
    uint16_t* vMid = NULL;
    uint16_t* vPost = NULL;
    uint16_t* vFinal = NULL;
    int pressGlobalX;
    int pressGlobalY;
    int targetPanelX;
    int step;
    int bad = 0;

    XMemset(&sc, 0, sizeof(sc));
    if (!t_buildScene(&sc)) return 1;
    XPoint_init(&zero, 0, 0);
    dockOrigin = XWidget_mapToGlobal((XWidget*)sc.dock, &zero);
    dockRect = XWidget_geometry((XWidget*)sc.dock);
    XRect_init(&vacRegion, dockOrigin.x, dockOrigin.y, dockRect.width,
               dockRect.height);
    pressGlobalX = dockOrigin.x + dockRect.width / 4;
    pressGlobalY = dockOrigin.y + 4;
    targetPanelX = 230 + 8;

    t_dockPointer((XWidget*)sc.dock, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                  XMouseButton_LeftButton, XMouseButton_LeftButton,
                  pressGlobalX, pressGlobalY);
    for (step = 1; step <= 4; ++step)
    {
        int dy = (step == 1) ? 6 : 24;
        int dx = (step <= 1) ? 0
                             : (targetPanelX - dockOrigin.x) * (step - 1) / 3;
        XThread_usleep(24000);
        t_dockPointer((XWidget*)sc.dock, XEVENT_TYPE_MOUSE_MOVE,
                      XMouseButton_NoButton, XMouseButton_LeftButton,
                      pressGlobalX + dx, pressGlobalY + dy);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
    }
    vMid = t_captureVisible();
    t_dockPointer((XWidget*)sc.dock, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                  XMouseButton_LeftButton, XMouseButton_NoButton,
                  pressGlobalX + (targetPanelX - dockOrigin.x),
                  pressGlobalY + 24);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    vPost = t_captureVisible();
    XWidget_repaint((XWidget*)sc.host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    vFinal = t_captureVisible();
    if (vMid && vPost && vFinal)
    {
        int badMid = t_diffRegion(vMid, vFinal, &vacRegion, tag, 0);
        int badPost = t_diffRegion(vPost, vFinal, &vacRegion, tag, 0);
        bad = badMid + badPost;
        if (badMid)
            fprintf(stderr, "[DD-FAIL] let-residue: 拖动中让位区残留 %d px\n",
                    badMid);
        if (badPost)
            fprintf(stderr, "[DD-FAIL] let-residue: 松手后让位区残留 %d px\n",
                    badPost);
    }
    else
    {
        fprintf(stderr, "[DD-FAIL] let-residue: 抓帧失败\n");
        bad = -1;
    }
    free(vMid);
    free(vPost);
    free(vFinal);
    fprintf(stderr, "[DD] LET_RESIDUE mismatch=%d\n", bad < 0 ? 999999 : bad);
    t_destroyScene(&sc);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    return bad < 0 ? 1 : bad;
}

/* ==================== 场景八：拖出→拖回中央区释放（页签化回停） ====================
 * 真机配方复刻（2026-11-04 mcgs 现场）：右面板拖出成浮动 → 拖回宿主
 * 中央区（缓慢多步滑过中央）→ 在中央区中央释放。真机现场症状：
 * ①另一侧（左）面板标题条区域被外层页面像素穿透；②宿主下方外层页面
 * 整条区域被刷白；③旧标题条残影。判据=宿主整窗 + 宿主下方外层条带
 * 与强制重绘基准逐像素一致（含浮动面板足迹扣除）。 */
static int t_runTabifyScenario(const char* tag)
{
    XMainWindow* host = NULL;
    XWidget* central = NULL;
    XDockWidget* dockLeft = NULL;
    XDockWidget* dockRight = NULL;
    XWidget* outer = NULL;
    XLabel* outerLabel = NULL;
    XPoint zero;
    XPoint org;
    XRect hostRect;
    XRect belowRect;
    XRect floats[4];
    int floatCount = 0;
    uint16_t* v = NULL;
    uint16_t* baseline = NULL;
    int bad = 0;
    int step;

    XPoint_init(&zero, 0, 0);
    outer = XWidget_create(NULL, 0);
    if (!outer) return 1;
    XWidget_setGeometry(outer, 0, 0, T_PANEL_W, T_PANEL_H);
    /* 外层页面下方放文字标签：宿主下方条带的残留必须可检（真机白块
     * 把外层页面文字洗掉）。 */
    outerLabel = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, outer, 0);
    if (outerLabel)
    {
        XLabel_setText_2(outerLabel, "OUTER-PAGE-BELOW");
        XWidget_setGeometry((XWidget*)outerLabel, 10, 440, 300, 30);
        XWidget_show((XWidget*)outerLabel);
    }
    host = XMainWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, outer,
                                 (XWidgetFlags)XWindowType_Window);
    if (!host) return 1;
    XWidget_setGeometry((XWidget*)host, 10, 10, 560, 420);
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 0, 128, 0, 255);
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette((XWidget*)host, &pal);
        XWidget_setAutoFillBackground((XWidget*)host, true);
    }
    {
        XWidget* menuBar = XMainWindow_menuBar(host);
        if (menuBar)
        {
            XMenu* m1 = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        (XWidget*)host, "文件(&F)");
            if (m1) XMenuBar_addMenu((XMenuBar*)menuBar, m1);
        }
    }
    central = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)host, 0);
    if (central)
    {
        XLabel_setText_2((XLabel*)central, "中央显示区（XLabel）");
        XLabel_setAlignment((XLabel*)central, XAlignment_Center);
        XMainWindow_setCentralWidget(host, central);
    }
    dockLeft = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "停靠-左",
                                     (XWidget*)host, 0);
    dockRight = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "停靠-右",
                                      (XWidget*)host, 0);
    if (dockLeft && dockRight)
    {
        XWidget* cl = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       (XWidget*)dockLeft, 0);
        XWidget* cr = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       (XWidget*)dockRight, 0);
        if (cl)
        {
            XLabel_setText_2((XLabel*)cl, "左面板内容");
            XDockWidget_setWidget(dockLeft, cl);
        }
        if (cr)
        {
            XLabel_setText_2((XLabel*)cr, "右面板内容（可拖出浮动）");
            XDockWidget_setWidget(dockRight, cr);
        }
        XMainWindow_addDockWidget(host, XDockWidgetArea_Left,
                                  (XWidget*)dockLeft);
        XMainWindow_addDockWidget(host, XDockWidgetArea_Right,
                                  (XWidget*)dockRight);
    }
    XWidget_show((XWidget*)host);
    XWidget_repaint((XWidget*)host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    org = XWidget_mapToGlobal((XWidget*)host, &zero);
    XRect_init(&hostRect, org.x, org.y, XWidget_width((XWidget*)host),
               XWidget_height((XWidget*)host));
    /* 宿主下方外层条带（真机白块区）：宿主底缘到面板底。 */
    XRect_init(&belowRect, org.x, org.y + XWidget_height((XWidget*)host),
               XWidget_width((XWidget*)host),
               T_PANEL_H - org.y - XWidget_height((XWidget*)host));

    /* 第一步：拖出成浮动（释放点在宿主外）。 */
    {
        XDockWidget* d = dockRight;
        XPoint dorg = XWidget_mapToGlobal((XWidget*)d, &zero);
        int pressX = dorg.x + 40;
        int pressY = dorg.y + 4;
        t_touchDragOut((XWidget*)host, d, pressX, pressY, 60, 2, 4);
        t_touchRelease((XWidget*)host, d, pressX + 60 * 4, pressY + 2 * 4);
        t_expect(XDockWidget_isFloating(d),
                 "场景八第一步：右面板未脱靠成浮动态");
        if (XDockWidget_isFloating(d))
        {
            XPoint p = XWidget_mapToGlobal((XWidget*)d, &zero);
            XRect_init(&floats[floatCount], p.x, p.y,
                       XWidget_width((XWidget*)d),
                       XWidget_height((XWidget*)d));
            ++floatCount;
        }
    }

    /* 第二步：拖回中央区缓慢滑过，中央区中央释放（真机配方）。 */
    {
        XDockWidget* d = dockRight;
        XPoint forg = XWidget_mapToGlobal((XWidget*)d, &zero);
        int pressX = forg.x + 40;
        int pressY = forg.y + 4;
        int cx = org.x + XWidget_width((XWidget*)host) / 2;
        int cy = org.y + XWidget_height((XWidget*)host) / 2;
        if (!XDockWidget_isFloating(d)) return bad;
        t_touchDragOut((XWidget*)host, d, pressX, pressY, cx - pressX,
                    cy - pressY, 8);
        t_dumpPpm("pre_release");
        t_touchRelease((XWidget*)host, d, cx, cy);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        t_dumpPpm("release1");
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        t_dumpPpm("release2");
        if (t_debug())
            fprintf(stderr,
                    "[DD:%s] after tabify release: floating=%d dragging=%d "
                    "rightG=(%d,%d,%d,%d) dropIndicatorVis=%d\n",
                    tag, XDockWidget_isFloating(d) ? 1 : 0, d->m_dragging,
                    XWidget_mapToGlobal((XWidget*)d, &zero).x,
                    XWidget_mapToGlobal((XWidget*)d, &zero).y,
                    XWidget_width((XWidget*)d),
                    XWidget_height((XWidget*)d),
                    host->m_dropIndicator
                        ? XWidget_isVisible((XWidget*)host->m_dropIndicator)
                        : -1);
    }

    /* 判据：现状 vs 强制整重绘基准（宿主区 + 宿主下方外层条带）。 */
    floatCount = 0;
    if (XDockWidget_isFloating(dockRight))
    {
        XPoint p = XWidget_mapToGlobal((XWidget*)dockRight, &zero);
        XRect_init(&floats[floatCount], p.x, p.y,
                   XWidget_width((XWidget*)dockRight),
                   XWidget_height((XWidget*)dockRight));
        ++floatCount;
    }
    v = t_captureVisible();
    t_dumpPpm("residue");
    XWidget_repaint((XWidget*)outer);
    XWidget_repaint((XWidget*)host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    baseline = t_captureVisible();
    t_dumpPpm("baseline");
    if (!v || !baseline)
    {
        fprintf(stderr, "[DD-FAIL] %s: capture frames\n", tag);
        free(v);
        free(baseline);
        return 1;
    }
    bad += t_checkHostArea(&hostRect, v, baseline, floats, floatCount, tag);
    bad += t_checkHostArea(&belowRect, v, baseline, floats, floatCount, tag);
    if (t_debug())
        fprintf(stderr, "[DD:%s] mismatch=%d\n", tag, bad);
    free(v);
    free(baseline);
    if (outer) XClassDelete((XClass*)outer);
    return bad;
}


/* ==================== 场景九：拖出后**再拖动浮窗**（用户现场手势） ====================
 * 真机配方复刻（2026-11-04 mcgs 现场）：右面板拖出成浮动态 → 再按住浮窗
 * 标题条把它拖到别处。真机症状：浮窗像素落到别处、宿主内布局错乱、旧位
 * 留灰白底+半截文字（"没有重绘"）。判据=腾空区（旧位减新位）与强制重绘
 * 基准逐像素一致。 */
static int t_runFloatDragScenario(const char* tag)
{
    XMainWindow* host = NULL;
    XWidget* central = NULL;
    XDockWidget* dockLeft = NULL;
    XDockWidget* dockRight = NULL;
    XWidget* outer = NULL;
    XPoint zero;
    XPoint org;
    XRect oldRect;
    XRect newRect;
    uint16_t* v1 = NULL;
    uint16_t* baseline = NULL;
    int bad = 0;
    int i;

    XPoint_init(&zero, 0, 0);
    outer = XWidget_create(NULL, 0);
    if (!outer) return 1;
    XWidget_setGeometry(outer, 0, 0, T_PANEL_W, T_PANEL_H);
    XWidget_show(outer);
    host = XMainWindow_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, outer,
                                 (XWidgetFlags)XWindowType_Window);
    if (!host) return 1;
    XWidget_setGeometry((XWidget*)host, 10, 10, 560, 420);
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 0, 128, 0, 255);
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette((XWidget*)host, &pal);
        XWidget_setAutoFillBackground((XWidget*)host, true);
    }
    {
        XWidget* menuBar = XMainWindow_menuBar(host);
        if (menuBar)
        {
            XMenu* m1 = XMenu_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                        (XWidget*)host, "文件(&F)");
            if (m1) XMenuBar_addMenu((XMenuBar*)menuBar, m1);
        }
    }
    central = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (XWidget*)host, 0);
    if (central)
    {
        XLabel_setText_2((XLabel*)central, "中央显示区（XLabel）");
        XLabel_setAlignment((XLabel*)central, XAlignment_Center);
        XMainWindow_setCentralWidget(host, central);
    }
    dockLeft = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "停靠-左",
                                     (XWidget*)host, 0);
    dockRight = XDockWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, "停靠-右",
                                      (XWidget*)host, 0);
    if (dockLeft && dockRight)
    {
        XWidget* cl = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       (XWidget*)dockLeft, 0);
        XWidget* cr = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE,
                                       (XWidget*)dockRight, 0);
        if (cl)
        {
            XLabel_setText_2((XLabel*)cl, "左面板内容");
            XDockWidget_setWidget(dockLeft, cl);
        }
        if (cr)
        {
            XLabel_setText_2((XLabel*)cr, "右面板内容（可拖出浮动）");
            XDockWidget_setWidget(dockRight, cr);
        }
        XMainWindow_addDockWidget(host, XDockWidgetArea_Left,
                                  (XWidget*)dockLeft);
        XMainWindow_addDockWidget(host, XDockWidgetArea_Right,
                                  (XWidget*)dockRight);
    }
    XWidget_show((XWidget*)host);
    XWidget_repaint((XWidget*)host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    org = XWidget_mapToGlobal((XWidget*)host, &zero);
    (void)org;
    /* 第一步：拖出成浮动态（释放点在宿主外）。 */
    if (!dockRight) return 1;
    {
        XPoint dorg = XWidget_mapToGlobal((XWidget*)dockRight, &zero);
        int pressX = dorg.x + 40;
        int pressY = dorg.y + 4;
        t_touchDragOut((XWidget*)host, dockRight, pressX, pressY, 60, 2, 4);
        t_touchRelease((XWidget*)host, dockRight, pressX + 60 * 4,
                        pressY + 2 * 4);
        t_expect(XDockWidget_isFloating(dockRight),
                 "场景九第一步：右面板未脱靠成浮动态");
    }
    if (!XDockWidget_isFloating(dockRight)) return 1;
    {
        XPoint p = XWidget_mapToGlobal((XWidget*)dockRight, &zero);
        XRect_init(&oldRect, p.x, p.y, XWidget_width((XWidget*)dockRight),
                   XWidget_height((XWidget*)dockRight));
    }
    if (t_debug())
        fprintf(stderr, "[DD:%s] float at global=(%d,%d %dx%d)\n", tag,
                oldRect.x, oldRect.y, oldRect.width, oldRect.height);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    v1 = t_captureVisible();

    /* 第二步：按住浮窗标题条把它拖到别处（浮动态跟指）。 */
    {
        int pressX = oldRect.x + 40;
        int pressY = oldRect.y + 8;
        int dx = -180;
        int dy = 120;
        t_touchInject(t_touchTargetOf((XWidget*)host, dockRight), pressX,
                      pressY, XTOUCHPOINT_STATE_PRESSED,
                      XEVENT_TYPE_TOUCH_BEGIN);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        for (i = 1; i <= 6; ++i)
        {
            XThread_usleep(24000);
            t_touchInject(t_touchTargetOf((XWidget*)host, dockRight),
                          pressX + dx * i / 6, pressY + dy * i / 6,
                          XTOUCHPOINT_STATE_UPDATED, XEVENT_TYPE_TOUCH_UPDATE);
            XGuiApplication_processEvents(XEventLoop_AllEvents);
        }
        t_touchInject(t_touchTargetOf((XWidget*)host, dockRight),
                      pressX + dx, pressY + dy,
                      XTOUCHPOINT_STATE_RELEASED, XEVENT_TYPE_TOUCH_END);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
    }
    {
        XPoint p = XWidget_mapToGlobal((XWidget*)dockRight, &zero);
        XRect_init(&newRect, p.x, p.y, XWidget_width((XWidget*)dockRight),
                   XWidget_height((XWidget*)dockRight));
    }
    if (t_debug())
        fprintf(stderr, "[DD:%s] float now global=(%d,%d %dx%d)\n", tag,
                newRect.x, newRect.y, newRect.width, newRect.height);
    /* 判据：腾空区（旧位减新位）须与强制重绘基准一致。 */
    XWidget_repaint((XWidget*)outer);
    XWidget_repaint((XWidget*)host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    baseline = t_captureVisible();
    if (!v1 || !baseline)
    {
        fprintf(stderr, "[DD-FAIL] %s: capture frames\n", tag);
        free(v1);
        free(baseline);
        return 1;
    }
    bad = t_diffRegionExcept(v1, baseline, &oldRect, &newRect, tag,
                              "浮窗拖动腾空区");
    free(v1);
    free(baseline);
    if (outer) XClassDelete((XClass*)outer);
    return bad;
}

/* ==================== 主流程 ==================== */

int main(void)
{
    XGuiApplication* app = NULL;
    TScene sc;
    int bad;

    XMemset(&sc, 0, sizeof(sc));
    if (!XPlatformDisplayDriver_register(&t_ops))
    {
        fprintf(stderr, "[DD-FAIL] fake display driver register\n");
        return 1;
    }
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    if (!app)
    {
        fprintf(stderr, "[DD-FAIL] app create\n");
        return 1;
    }

    /* 场景 1：真实鼠标事件路径（桌面口径）。 */
    if (!t_buildScene(&sc)) return 1;
    bad = t_runMouseScenario(&sc, "mouse");
    t_destroyScene(&sc);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    /* 场景 2：触摸事件路径（真机口径：触摸→合成鼠标→停靠面板拖动）。 */
    if (!t_buildScene(&sc)) return 1;
    bad += t_runTouchScenario(&sc, "touch");
    t_destroyScene(&sc);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    /* 场景 3：demo 同构（菜单栏 + 左右两个停靠面板 + 触摸拖出两个）。 */
    bad += t_runTwoDockScenario("twodock");
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    /* 场景 4：反复「拖出 → 落回停靠区」循环（真机第二轮即崩）。 */
    bad += t_runCycleScenario("cycle", 4);

    /* 场景 5：预停靠位置 = 实际停靠位置（四区一致性判据）。 */
    bad += t_runDropPreviewScenario("droppreview");

    /* 场景 6：拖动每步成本（卡顿判据：avg / pansPerMove）。 */
    bad += t_runDragLatencyScenario("latency");

    /* 场景 7：让位区残留数值化判据行（LET_RESIDUE mismatch=<N>）。 */
    bad += t_runLetResidueScenario("letresidue");

    /* 场景 8：拖出→拖回中央区释放页签化回停（2026-11-04 真机残影配方）。 */
    bad += t_runTabifyScenario("tabify");
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    /* 场景 9：拖出后再拖动浮窗（用户现场手势：拖动浮窗留残影）。 */
    bad += t_runFloatDragScenario("floatdrag");
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    if (t_failures)
    {
        fprintf(stderr, "[DD] FAILED (%d assertions, mismatch=%d)\n",
                t_failures, bad);
        return 1;
    }
    fprintf(stderr, "[DD] PASS (mismatch=%d)\n", bad);
    return 0;
}

#else

/* 整文件裁剪哨兵：XGUI/FBDEV 关闭时本翻译单元为空实现。 */
typedef int xgui_dock_drag_test_disabled_t;

#endif /* XGUI_ON && XPLATFORM_FBDEV_ON */
