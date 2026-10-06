/** @file       XGuiDialogMoveTest.c
 *  @brief      fbdev 弹层拖动让位条带归位还原离屏测试（拖动对话框白块
 *              根修 2026-10-05 的回归锁）。
 *  @details    真机缺陷：fbdev DIRECT+2 下拖动对话框，每步移动把让位
 *              条带无条件填桌面底色（住着父窗内容也被洗掉），拖动中
 *              原位留白块，松手 clearOutsideWindow 整窗 expose 才擦
 *              平。根修后条带按归属顶层自后备缓冲直搬归位。本测试以
 *              伪 fbdev 显示驱动（RAM 帧缓冲，双缓冲+行距垫高）离屏
 *              复现整链——伪驱动注册后 XWindowDecoration 的 fbdev 分
 *              支（xwd_panelRect 探测/条带恢复/fill/blit）全部真实执
 *              行，present 直写/差带账本/遮挡剔除同链生效：
 *              - 「弹层出现→拖动 3 步→拖回 3 步→松手」每步全帧断言
 *                可见缓冲 == 两后备缓冲合成模型（父窗全区 ∪ 弹层矩形
 *                覆盖）；账本漏同步/遮挡剔错/条带误填都会在逐像素字节
 *                比对中现形（账本矩形与整搬决策的行为级子sume 断言，
 *                静态账本结构不可直读，见函数注）；
 *              - 伪驱动 pan 只记可见缓冲号，帧内容逐字节比对同时验证
 *                桌面色 0xEF7D 图案不出现于让位条带（旧缺陷指纹）。
 *              构建：-DXPLATFORM_FBDEV_ON=ON（驱动契约门控）+
 *              -DXGUI_BACKINGSTORE_BUFFER_COUNT=2（镜像真机 DIRECT+2）。
 *  @note       独立 main，不卷入主程序（CMake 同 XGuiRemoteAuth_Test
 *              接线）。XGUI_CSD 默认 Framework 档：测试环境无需环境变
 *              量即走框架装饰拖拽链。
 *  @author     XinYueC 团队
 *  @note       构建组合须含 -DXGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=ON
 *              （真机 DIRECT+2 全 trio，CMakeLists 头注同列）：替身
 *              negotiate 恒认可任意首选格式（与框架编译期图像格式解
 *              耦），桌面缺省 RGB32 下位深与面板 16bpp 行距失配——
 *              present 位深一致性守卫（XPlatformBackingStore_posix.c
 *              2026-10-06）会正确拒绝直写回落软件路径，本测试 fb 全
 *              帧断言即红。memhunt fbdev 通道 RGB32 复核：守卫前
 *              ASan global-buffer-overflow，守卫后干净红（断言失败、
 *              零越界）。
 */
#include "XGuiConfig.h"
#include "XGuiApplication.h"
#include "XWidget.h"
#include "XWindowDecoration.h"
#include "XTitleBar.h"
#include "XStyleOption.h" /* 标题栏按钮子控件位（空白锚点扫描用）。 */
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

/** @brief 标题栏按钮子控件位判定（与 XWindowDecoration xwd_isButtonSc
 *  同口径：命中即按钮，非按钮=可拖拽空白）。 */
static bool t_isButtonSc(int sc)
{
    return sc == XStyleSC_TitleBarSysMenu ||
           sc == XStyleSC_TitleBarMinButton ||
           sc == XStyleSC_TitleBarMaxButton ||
           sc == XStyleSC_TitleBarNormalButton ||
           sc == XStyleSC_TitleBarCloseButton;
}

/* ==================== 伪 fbdev 显示驱动（RAM 帧缓冲） ==================== */

#define T_PANEL_W 320
#define T_PANEL_H 240
/* 行距垫高 64B：证明 present/blit/fill 全链按驱动行距寻址（非 width*bpp）。 */
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
    /* 测试替身：任意首选格式均可直写（present 直写守卫要求面板格式
     * ==图像格式，替身恒认可以便与框架编译期图像格式解耦）。 */
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
    "dialogmove-test-fbdev", XPLATFORM_DISPLAY_DRIVER_ABI_VERSION,
    t_probe,      t_negotiate, t_pan,
    t_cacheSync,  t_waitVsync, t_stride
};

/* ==================== 断言设施 ==================== */

static int t_failures = 0;

static void t_expect(bool cond, const char* what)
{
    if (!cond)
    {
        fprintf(stderr, "[DM-FAIL] %s\n", what ? what : "");
        ++t_failures;
    }
}

/** @brief 取顶层控件后备缓冲的已合成图像（present 数据源同款）。 */
static XImage* t_storeImage(XWidget* top)
{
    XBackingStore* bs = XWidget_backingStore(top);
    XPlatformBackingStore* pbs = bs ? XBackingStore_handle(bs) : NULL;
    return pbs ? XPlatformBackingStore_paintDevice(pbs) : NULL;
}

/** @brief 全帧断言：可见缓冲逐像素字节 == 合成模型（父窗后备缓冲全
 *  区，弹层矩形内覆盖为其后备缓冲内容）。任一差带账本漏搬/遮挡剔除
 *  错切/条带误填（桌面色 0xEF7D 图案）都会在此现形——静态差带账本
 *  g_xpbsFbBackDirty 不可直读，行为级全帧比对是其正确性的严格上界
 *  （漏搬一行即一字节不符）。返回失败数增量。 */
static int t_assertComposite(XWidget* parent, XWidget* dialog,
                             const char* tag)
{
    XImage* pi = t_storeImage(parent);
    XImage* di = t_storeImage(dialog);
    int pb;
    int imgBplP;
    int imgBplD;
    const uint8_t* bitsP;
    const uint8_t* bitsD;
    const uint8_t* vis;
    XRect dg;
    int x;
    int y;
    int bad = 0;
    if (!pi || !di)
    {
        fprintf(stderr, "[DM-FAIL] %s: store image missing\n", tag);
        ++t_failures;
        return 1;
    }
    pb = (XImageFormat_bitDepth(XImage_format(pi)) + 7) / 8;
    imgBplP = XImage_bytesPerLine(pi);
    imgBplD = XImage_bytesPerLine(di);
    bitsP = XImage_constBits(pi);
    bitsD = XImage_constBits(di);
    vis = t_fb + (size_t)t_visible * (size_t)T_PANEL_H * T_STRIDE;
    dg = XWidget_geometry(dialog);
    {
        /* 诊断转储旋钮：DM_DUMP=1 时打印失配包围盒+样本（定位残帧行带
         * 分布用；默认关，不影响门禁）。 */
        static int dump = -1;
        int mmX0 = 0;
        int mmX1 = 0;
        int mmY0 = 0;
        int mmY1 = 0;
        if (dump < 0) dump = getenv("DM_DUMP") ? 1 : 0;
        for (y = 0; y < T_PANEL_H; ++y)
        {
            for (x = 0; x < T_PANEL_W; ++x)
            {
                const uint8_t* e;
                const uint8_t* a;
                if (x >= dg.x && x < dg.x + dg.width && y >= dg.y &&
                    y < dg.y + dg.height)
                    e = bitsD + (size_t)(y - dg.y) * (size_t)imgBplD +
                        (size_t)(x - dg.x) * (size_t)pb;
                else
                    e = bitsP + (size_t)y * (size_t)imgBplP +
                        (size_t)x * (size_t)pb;
                a = vis + (size_t)y * T_STRIDE + (size_t)x * (size_t)pb;
                if (memcmp(a, e, (size_t)pb) != 0)
                {
                    if (bad == 0)
                    {
                        fprintf(stderr,
                                "[DM-FAIL] %s: first mismatch at (%d,%d) "
                                "dlg=(%d,%d,%d,%d)\n",
                                tag ? tag : "", x, y, dg.x, dg.y, dg.width,
                                dg.height);
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
                        fprintf(stderr,
                                "  mismatch (%d,%d) exp=%02x%02x "
                                "act=%02x%02x\n",
                                x, y, e[0], e[1], a[0], a[1]);
                    ++bad;
                    if (bad >= 8 && !dump) return bad;
                }
            }
        }
        if (bad && dump)
            fprintf(stderr, "  mismatch bbox=(%d,%d)-(%d,%d)\n",
                    mmX0, mmY0, mmX1, mmY1);
    }
    if (bad)
    {
        fprintf(stderr, "[DM-FAIL] %s: %d mismatched pixels\n",
                tag ? tag : "", bad);
        ++t_failures;
    }
    return bad;
}

/* ==================== 合成拖拽事件注入 ==================== */

/** @brief 向装饰状态机直投一次指针事件（全局坐标按窗口当前几何换算，
 *  与 fbinput 注入口径一致——2026-10-04 根修后拖拽增量吃 globalPosition）。 */
static void t_dragEvent(XWidget* dlg, XEventType type, XMouseButton button,
                        XMouseButton buttons, int globalX, int globalY)
{
    XMouseEvent me;
    XPoint local;
    XPoint global;
    XRect dg = XWidget_geometry(dlg);
    XPoint_init(&local, globalX - dg.x, globalY - dg.y);
    XPoint_init(&global, globalX, globalY);
    XMouseEvent_init(&me, type, button, 0, local);
    XMouseEvent_setButtons(&me, buttons);
    XMouseEvent_setGlobalPosition(&me, &global);
    XWindowDecoration_handlePointer(dlg, (XEvent*)&me);
}

/* ==================== 用例 ==================== */

int main(void)
{
    XGuiApplication* app = NULL;
    XWidget* parent = NULL;
    XWidget* parentLandmark = NULL;
    XWidget* parentLandmark2 = NULL;
    XWidget* dialog = NULL;
    XRect dg0;
    int barH;
    int step;
    int gx;
    int gy;

    if (!XPlatformDisplayDriver_register(&t_ops))
    {
        fprintf(stderr, "[DM-FAIL] fake display driver register\n");
        return 1;
    }
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    if (!app)
    {
        fprintf(stderr, "[DM-FAIL] app create\n");
        return 1;
    }

    /* 1) 父窗=全面板：深绿底 + 两块橙色地标（拖出走廊上，验证「还原的
     *    是父窗真实内容」而非任意非白像素）。 */
    parent = XWidget_create(NULL, 0);
    if (!parent)
    {
        fprintf(stderr, "[DM-FAIL] parent create\n");
        return 1;
    }
    XWidget_setGeometry(parent, 0, 0, T_PANEL_W, T_PANEL_H);
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 0, 128, 0, 255); /* 深绿（≠桌面 0xEF7D 底）。 */
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette(parent, &pal);
        XWidget_setAutoFillBackground(parent, true);
    }
    parentLandmark = XWidget_create(parent, 0);
    if (!parentLandmark) return 1;
    XWidget_setGeometry(parentLandmark, 70, 60, 40, 40);
    parentLandmark2 = XWidget_create(parent, 0);
    if (!parentLandmark2) return 1;
    XWidget_setGeometry(parentLandmark2, 200, 150, 30, 30);
    {
        XWidget* lms[2];
        int i;
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 255, 128, 0, 255); /* 橙地标。 */
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        lms[0] = parentLandmark;
        lms[1] = parentLandmark2;
        for (i = 0; i < 2; ++i)
        {
            XWidget_setPalette(lms[i], &pal);
            XWidget_setAutoFillBackground(lms[i], true);
            XWidget_show(lms[i]);
        }
    }
    XWidget_show(parent);
    XWidget_repaint(parent);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    /* 2) 对话框：独立弹层顶层，蓝底，压在地标 1 上。 */
    dialog = XWidget_create(NULL, 0);
    if (!dialog)
    {
        fprintf(stderr, "[DM-FAIL] dialog create\n");
        return 1;
    }
    XWidget_setGeometry(dialog, 60, 50, 120, 80);
    {
        XPalette pal;
        XColor c;
        XPalette_init_default(&pal);
        XColor_init_rgb(&c, 0, 0, 255, 255); /* 蓝（≠父窗绿/桌面色）。 */
        XPalette_setColor(&pal, XPaletteColorGroup_Active,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                          XPaletteColorRole_Window, c);
        XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                          XPaletteColorRole_Window, c);
        XWidget_setPalette(dialog, &pal);
        XWidget_setAutoFillBackground(dialog, true);
    }
    XWidget_show(dialog);
    XWidget_repaint(dialog);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    dg0 = XWidget_geometry(dialog);
    barH = XTitleBar_defaultHeight();
    t_expect(barH > 0, "title bar height positive");
    t_expect(t_assertComposite(parent, dialog, "baseline") == 0,
             "baseline frame == composite(parent, dialog@60,50)");

    /* 3) 弹层出现→拖动 3 步（右下 (+16,+12)/步）→拖回 3 步→松手。
     *    每步先断言几何真移动（防装饰未接管导致的空转假绿），再全帧
     *    断言：让位条带必须逐字节等于父窗后备缓冲内容（真机旧缺陷=
     *    桌面底色 0xEF7D 图案，字节比对必不符）。25ms 间隔放行 60fps
     *    手势限帧闸。 */
    /* 拖拽锚点：条带内「空白区」（hitTest=非按钮子控件位）逐 x 扫描—
     * 按钮几何随宽度/样式漂移（120 宽条：SysMenu 居左、Min/Max/Close
     * 居右，固定 x 会撞钮：按压臂化后拖拽永不接管=测试空转假绿，
     * 2026-10-05 RED 轮实证）。 */
    {
        XPoint midProbe;
        XWidget* barW;
        int bx;
        int blankX = -1;
        XPoint_init(&midProbe, dg0.width / 2, barH / 2);
        barW = XWidget_childAt(dialog, &midProbe);
        if (barW && XTitleBar_isBar(barW))
        {
            XTitleBar* bar = (XTitleBar*)barW;
            for (bx = 0; bx < dg0.width && blankX < 0; ++bx)
            {
                XPoint p;
                XPoint_init(&p, bx, barH / 2);
                if (!t_isButtonSc(XTitleBar_hitTest_base(bar, &p)))
                    blankX = bx;
            }
        }
        t_expect(blankX >= 0, "blank title-bar drag anchor found");
        gx = dg0.x + (blankX >= 0 ? blankX : 0);
    }
    gy = dg0.y + (barH > 1 ? barH / 2 : 1);
    t_dragEvent(dialog, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                XMouseButton_LeftButton, XMouseButton_LeftButton, gx, gy);
    t_expect(XWindowDecoration_gestureActive(),
             "drag armed after title-bar press");
    t_assertComposite(parent, dialog, "press");
    for (step = 1; step <= 3; ++step)
    {
        char tag[64];
        XRect dgNow;
        XThread_msleep(25);
        gx += 16;
        gy += 12;
        t_dragEvent(dialog, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton,
                    XMouseButton_LeftButton, gx, gy);
        dgNow = XWidget_geometry(dialog);
        snprintf(tag, sizeof(tag), "move-out step %d", step);
        t_expect(dgNow.x == dg0.x + 16 * step && dgNow.y == dg0.y + 12 * step,
                 tag); /* 几何未动=装饰未接管，本测试自证失效。 */
        t_assertComposite(parent, dialog, tag);
    }
    for (step = 1; step <= 3; ++step)
    {
        char tag[64];
        XRect dgNow;
        XThread_msleep(25);
        gx -= 16;
        gy -= 12;
        t_dragEvent(dialog, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton,
                    XMouseButton_LeftButton, gx, gy);
        dgNow = XWidget_geometry(dialog);
        snprintf(tag, sizeof(tag), "move-back step %d", step);
        t_expect(dgNow.x == dg0.x + 16 * (3 - step) &&
                     dgNow.y == dg0.y + 12 * (3 - step),
                 tag);
        t_assertComposite(parent, dialog, tag);
    }
    t_dragEvent(dialog, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                XMouseButton_LeftButton, XMouseButton_NoButton, gx, gy);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    t_expect(t_assertComposite(parent, dialog, "release sweep") == 0,
             "release sweep keeps composite frame (no wash)");

    /* 4) 松手后几何应回到初始位（拖出 3 步+拖回 3 步零位移丢失）。 */
    {
        XRect dgEnd = XWidget_geometry(dialog);
        t_expect(dgEnd.x == dg0.x && dgEnd.y == dg0.y,
                 "round-trip geometry restored");
    }

    /* 5) 快照路径自证：默认 env（XGUI_DRAG_SNAPSHOT_BLIT 未设=开）下，
     *    上述 6 个落地步必须真实走快照直写（步数计数增长）。计数不涨
     *    =门控/条件失配静默回落旧路径，全帧断言再绿也是假绿（快照模
     *    式从未被测到）。 */
    {
        int snapSteps = XWindowDecoration_dragSnapshotStepCount();
        t_expect(snapSteps >= 6, "snapshot blit engaged (step count grew)");
        fprintf(stderr, "[DM] snapshot steps after cycle A: %d\n",
                snapSteps);
    }

    /* 6) 旧路径对照环（XGUI_DRAG_SNAPSHOT_BLIT=0）：同一拖拽走廊再走一
     *    轮，快照步计数必须不再增长（env 门控真实生效），全帧断言同口径
     *    通过（旧 flush 路径行为保持）。env 每手势开始现读，setenv 立即
     *    生效。 */
    if (setenv("XGUI_DRAG_SNAPSHOT_BLIT", "0", 1) != 0)
        fprintf(stderr, "[DM-FAIL] setenv legacy\n");
    {
        int before = XWindowDecoration_dragSnapshotStepCount();
        t_dragEvent(dialog, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                    XMouseButton_LeftButton, XMouseButton_LeftButton, gx, gy);
        t_expect(XWindowDecoration_gestureActive(),
                 "legacy drag armed after title-bar press");
        t_assertComposite(parent, dialog, "legacy press");
        for (step = 1; step <= 3; ++step)
        {
            char tag[64];
            XRect dgNow;
            XThread_msleep(25);
            gx += 16;
            gy += 12;
            t_dragEvent(dialog, XEVENT_TYPE_MOUSE_MOVE,
                        XMouseButton_NoButton, XMouseButton_LeftButton,
                        gx, gy);
            dgNow = XWidget_geometry(dialog);
            snprintf(tag, sizeof(tag), "legacy move-out step %d", step);
            t_expect(dgNow.x == dg0.x + 16 * step &&
                         dgNow.y == dg0.y + 12 * step,
                     tag);
            t_assertComposite(parent, dialog, tag);
        }
        for (step = 1; step <= 3; ++step)
        {
            char tag[64];
            XRect dgNow;
            XThread_msleep(25);
            gx -= 16;
            gy -= 12;
            t_dragEvent(dialog, XEVENT_TYPE_MOUSE_MOVE,
                        XMouseButton_NoButton, XMouseButton_LeftButton,
                        gx, gy);
            dgNow = XWidget_geometry(dialog);
            snprintf(tag, sizeof(tag), "legacy move-back step %d", step);
            t_expect(dgNow.x == dg0.x + 16 * (3 - step) &&
                         dgNow.y == dg0.y + 12 * (3 - step),
                     tag);
            t_assertComposite(parent, dialog, tag);
        }
        t_dragEvent(dialog, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                    XMouseButton_LeftButton, XMouseButton_NoButton, gx, gy);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        t_expect(t_assertComposite(parent, dialog, "legacy release sweep") ==
                     0,
                 "legacy release sweep keeps composite frame");
        t_expect(XWindowDecoration_dragSnapshotStepCount() == before,
                 "legacy env keeps snapshot step count frozen");
        fprintf(stderr, "[DM] snapshot steps after legacy cycle: %d\n",
                XWindowDecoration_dragSnapshotStepCount());
    }
    if (unsetenv("XGUI_DRAG_SNAPSHOT_BLIT") != 0)
        fprintf(stderr, "[DM-FAIL] unsetenv\n");

    if (t_failures == 0)
        fprintf(stderr, "[DM-PASS] dialog move strip restoration ok "
                        "(panCalls=%d)\n",
                t_panCalls);
    if (dialog) XClassDelete(dialog);
    if (parentLandmark) XClassDelete(parentLandmark);
    if (parentLandmark2) XClassDelete(parentLandmark2);
    if (parent) XClassDelete(parent);
    if (app) XClassDelete(app);
    XPlatformDisplayDriver_unregister(&t_ops);
    return t_failures ? 1 : 0;
}

#else /* !XGUI_ON || !XPLATFORM_FBDEV_ON */

int main(void)
{
    fprintf(stderr,
            "[DM-SKIP] requires XGUI_ON && XPLATFORM_FBDEV_ON "
            "(build with -DXPLATFORM_FBDEV_ON=ON)\n");
    return 0;
}

#endif /* XGUI_ON && XPLATFORM_FBDEV_ON */
