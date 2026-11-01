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
