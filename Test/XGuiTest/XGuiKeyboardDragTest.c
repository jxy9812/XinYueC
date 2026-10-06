/** @file       XGuiKeyboardDragTest.c
 *  @brief      fbdev 屏幕键盘紧凑悬浮拖移离屏测试（拖动残影+逐 MOVE 重绘
 *              风暴的回归锁）。
 *  @details    真机缺陷（昆仑通态 A33 2026-10-06）：紧凑悬浮键盘（工具栏
 *              「悬浮切换」第三形态，独立顶层 Popup）拖移每步只
 *              setGeometryRect——让位条带无人还原（旧位键盘像素长期残留
 *              =残影），且移窗后依赖零散整窗 PAINT 补画新位（A33 上每帧
 *              全键盘软件重绘+整窗提交，拖动 FPS 5.9）。本测试以伪 fbdev
 *              显示驱动（RAM 帧缓冲，双缓冲+行距垫高）离屏复现整链：
 *              「宿主+编辑框→键盘停靠弹出→工具栏点悬浮切换→工具栏空白
 *              按住拖动 4 步→拖回→松手」每步全帧断言可见缓冲 == 合成模
 *              型（宿主后备缓冲全区 ∪ 键盘后备缓冲矩形覆盖）；残影/条带
 *              误填/账本漏搬都会在逐像素字节比对中现形。并量化拖动步的
 *              flush 次数（g_flushBsCount）：纯移动内容零变化，修复后每
 *              步提交必须绕过 flushBackingStore（快照直提语义）。
 *  @note       独立 main，不卷入主程序（CMake 同 XGuiDialogMove_Test 接
 *              线）。构建组合须 -DXPLATFORM_FBDEV_ON=ON +
 *              -DXGUI_BACKINGSTORE_BUFFER_COUNT=2 +
 *              -DXGUI_BACKINGSTORE_IMAGE_FORMAT_RGB16=ON（真机 DIRECT+2
 *              全 trio，位深一致性守卫要求面板 16bpp==后备 16bpp）。
 *  @author     XinYueC 团队
 */
#include "XGuiConfig.h"
#include "XGuiApplication.h"
#include "XCoreApplication.h"
#include "XWidget.h"
#include "XLineEdit.h"
#include "XVirtualKeyboard.h"
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

#if XGUI_ON && XPLATFORM_FBDEV_ON && XVIRTUALKEYBOARD_ON

/* 拖动步 flush 量化探针（XWidget.c 全局计数器，非静态）。 */
extern volatile unsigned long g_flushBsCount;

/* ==================== 伪 fbdev 显示驱动（RAM 帧缓冲） ==================== */

#define T_PANEL_W 512
#define T_PANEL_H 384
/* 行距垫高 64B：证明 present/blit/fill 全链按驱动行距寻址。 */
#define T_STRIDE ((size_t)T_PANEL_W * 2 + 64)

static uint8_t t_fb[T_STRIDE * T_PANEL_H * 2];
static int t_visible; /* 伪 pan 记账：当前可见缓冲号（0/1）。 */
static int t_panCalls;
static int t_cacheSyncCalls;

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
    /* 测试替身：任意首选格式均可直写（与框架编译期图像格式解耦）。 */
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
    ++t_cacheSyncCalls;
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
    "keyboarddrag-test-fbdev", XPLATFORM_DISPLAY_DRIVER_ABI_VERSION,
    t_probe,      t_negotiate, t_pan,
    t_cacheSync,  t_waitVsync, t_stride
};

/* ==================== 断言设施 ==================== */

static int t_failures = 0;

static void t_expect(bool cond, const char* what)
{
    if (!cond)
    {
        fprintf(stderr, "[KD-FAIL] %s\n", what ? what : "");
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

/** @brief 全帧断言：可见缓冲逐像素字节 == 合成模型（宿主后备缓冲全区，
 *  键盘矩形内覆盖为其后备缓冲内容）。返回失败数增量。
 *  @note   诊断转储旋钮 KD_DUMP=1：打印失配包围盒+样本。 */
static int t_assertComposite(XWidget* host, XWidget* kb,
                             const char* tag)
{
    XImage* hi = t_storeImage(host);
    XImage* ki = t_storeImage(kb);
    int pb;
    int imgBplH;
    int imgBplK;
    const uint8_t* bitsH;
    const uint8_t* bitsK;
    const uint8_t* vis;
    XRect kg;
    int x;
    int y;
    int bad = 0;
    if (!hi || !ki)
    {
        fprintf(stderr, "[KD-FAIL] %s: store image missing\n", tag);
        ++t_failures;
        return 1;
    }
    pb = (XImageFormat_bitDepth(XImage_format(hi)) + 7) / 8;
    imgBplH = XImage_bytesPerLine(hi);
    imgBplK = XImage_bytesPerLine(ki);
    bitsH = XImage_constBits(hi);
    bitsK = XImage_constBits(ki);
    vis = t_fb + (size_t)t_visible * (size_t)T_PANEL_H * T_STRIDE;
    kg = XWidget_geometry(kb);
    {
        static int dump = -1;
        int mmX0 = 0;
        int mmX1 = 0;
        int mmY0 = 0;
        int mmY1 = 0;
        if (dump < 0) dump = getenv("KD_DUMP") ? 1 : 0;
        for (y = 0; y < T_PANEL_H; ++y)
        {
            for (x = 0; x < T_PANEL_W; ++x)
            {
                const uint8_t* e;
                const uint8_t* a;
                if (x >= kg.x && x < kg.x + kg.width && y >= kg.y &&
                    y < kg.y + kg.height)
                    e = bitsK + (size_t)(y - kg.y) * (size_t)imgBplK +
                        (size_t)(x - kg.x) * (size_t)pb;
                else
                    e = bitsH + (size_t)y * (size_t)imgBplH +
                        (size_t)x * (size_t)pb;
                a = vis + (size_t)y * T_STRIDE + (size_t)x * (size_t)pb;
                if (memcmp(a, e, (size_t)pb) != 0)
                {
                    if (bad == 0)
                    {
                        fprintf(stderr,
                                "[KD-FAIL] %s: first mismatch at (%d,%d) "
                                "kb=(%d,%d,%d,%d)\n",
                                tag ? tag : "", x, y, kg.x, kg.y, kg.width,
                                kg.height);
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
                    if (bad >= 8 && !dump)
                    {
                        /* 提前收场也要记账：调用方有无 t_expect 包裹，
                         * 失败都必须计入（否则拖动步断言静默漏计）。 */
                        fprintf(stderr, "[KD-FAIL] %s: >=%d mismatched "
                                        "pixels (early exit)\n",
                                tag ? tag : "", bad);
                        ++t_failures;
                        return bad;
                    }
                }
            }
        }
        if (bad && dump)
            fprintf(stderr, "  mismatch bbox=(%d,%d)-(%d,%d)\n",
                    mmX0, mmY0, mmX1, mmY1);
    }
    if (bad)
    {
        fprintf(stderr, "[KD-FAIL] %s: %d mismatched pixels\n",
                tag ? tag : "", bad);
        ++t_failures;
    }
    return bad;
}

/* ==================== 合成鼠标事件注入 ==================== */

/** @brief 调试打印旋钮（KD_DEBUG=1）：注入前后的键盘内部态。 */
static int t_debug(void)
{
    static int on = -1;
    if (on < 0) on = getenv("KD_DEBUG") ? 1 : 0;
    return on;
}

/** @brief 向键盘控件直投一次鼠标事件（键盘为顶层 Popup，局部坐标=全局
 *  坐标-窗口原点；经 XCoreApplication_sendEvent 走真实 vtable 分发链）。 */
static void t_kbMouseEvent(XVirtualKeyboard* kb, XEventType type,
                           XMouseButton button, XMouseButton buttons,
                           int globalX, int globalY)
{
    XMouseEvent me;
    XPoint local;
    XPoint global;
    XRect kg = XWidget_geometry((XWidget*)kb);
    XPoint_init(&local, globalX - kg.x, globalY - kg.y);
    XPoint_init(&global, globalX, globalY);
    XMouseEvent_init(&me, type, button, 0, local);
    XMouseEvent_setButtons(&me, buttons);
    XMouseEvent_setGlobalPosition(&me, &global);
    if (t_debug())
        fprintf(stderr,
                "[KD] evt type=%d btn=%d global=(%d,%d) local=(%d,%d) "
                "kb=(%d,%d,%d,%d) bar=(%d,%d,%d,%d) tool=%d compact=%d\n",
                (int)type, (int)button, globalX, globalY, local.x, local.y,
                kg.x, kg.y, kg.width, kg.height, kb->m_menuBarRect.x,
                kb->m_menuBarRect.y, kb->m_menuBarRect.width,
                kb->m_menuBarRect.height, kb->m_pressedTool,
                kb->m_compactFloat);
    XCoreApplication_sendEvent((XObject*)kb, (XEvent*)&me);
    if (t_debug())
        fprintf(stderr, "[KD]   -> after: tool=%d compact=%d drag=%d\n",
                kb->m_pressedTool, kb->m_compactFloat, kb->m_compactDrag);
}

/* ==================== 用例 ==================== */

int main(void)
{
    XGuiApplication* app = NULL;
    XWidget* host = NULL;
    XWidget* landmark = NULL;
    XWidget* editor = NULL;
    XVirtualKeyboard* kb = NULL;
    XRect kg0;
    XRect bar;
    int gx;
    int gy;
    int step;
    unsigned long flushBefore;
    unsigned long flushMovesMark;
    unsigned long flushMovesSum = 0;
    int panBefore;

    if (!XPlatformDisplayDriver_register(&t_ops))
    {
        fprintf(stderr, "[KD-FAIL] fake display driver register\n");
        return 1;
    }
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    if (!app)
    {
        fprintf(stderr, "[KD-FAIL] app create\n");
        return 1;
    }

    /* 1) 宿主窗=全面板：深绿底 + 橙色地标（拖动走廊上，验证「还原的是
     *    宿主真实内容」）。编辑框=键盘弹出目标。 */
    host = XWidget_create(NULL, 0);
    if (!host)
    {
        fprintf(stderr, "[KD-FAIL] host create\n");
        return 1;
    }
    XWidget_setGeometry(host, 0, 0, T_PANEL_W, T_PANEL_H);
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
        XWidget_setPalette(host, &pal);
        XWidget_setAutoFillBackground(host, true);
    }
    landmark = XWidget_create(host, 0);
    if (!landmark) return 1;
    XWidget_setGeometry(landmark, 40, 40, 30, 30);
    {
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
        XWidget_setPalette(landmark, &pal);
        XWidget_setAutoFillBackground(landmark, true);
        XWidget_show(landmark);
    }
    editor = XLineEdit_create(host, 0);
    if (!editor) return 1;
    XWidget_setGeometry(editor, 20, 20, 200, 28);
    XWidget_show(editor);
    XWidget_show(host);
    XWidget_repaint(host);
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    /* 2) 键盘：停靠形态弹出（子控件浮层，第三形态进入前置）。 */
    kb = XVirtualKeyboard_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, host, 0);
    if (!kb)
    {
        fprintf(stderr, "[KD-FAIL] keyboard create\n");
        return 1;
    }
    XVirtualKeyboard_popup(kb, editor);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    t_expect(XVirtualKeyboard_popupVisible(kb), "keyboard popped");
    bar = kb->m_menuBarRect;
    t_expect(bar.height > 0 && bar.width > 0, "dock menubar laid out");

    /* 3) 工具栏槽 0（悬浮切换）格心点击→紧凑悬浮态。坐标换算：工具栏
     *    矩形是键盘窗口局部量，全局=键盘原点+局部（停靠键盘在宿主底部）。 */
    {
        XRect kgDock = XWidget_geometry((XWidget*)kb);
        gx = kgDock.x + bar.x + bar.width / 8;
        gy = kgDock.y + bar.y + bar.height / 2;
    }
    t_kbMouseEvent(kb, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                   XMouseButton_LeftButton, XMouseButton_LeftButton, gx, gy);
    t_kbMouseEvent(kb, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                   XMouseButton_LeftButton, XMouseButton_NoButton, gx, gy);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    t_expect(kb->m_compactFloat, "compact float engaged");
    t_expect(XWidget_isWindow((XWidget*)kb), "keyboard is top-level window");
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    bar = kb->m_menuBarRect;
    t_expect(bar.height > 0 && bar.width > 0, "compact menubar laid out");

    kg0 = XWidget_geometry((XWidget*)kb);
    t_expect(kg0.width > 0 && kg0.height > 0, "compact geometry sane");
    t_expect(kg0.x >= 0 && kg0.y >= 0 &&
                 kg0.x + kg0.width <= T_PANEL_W &&
                 kg0.y + kg0.height <= T_PANEL_H,
             "compact geometry inside panel");
    t_expect(t_assertComposite(host, (XWidget*)kb, "compact baseline") == 0,
             "baseline frame == composite(host, kb)");

    /* 4) 工具栏槽 1 左缘空白（图标热区盒外）按住→拖动 4 步→拖回 4 步
     *    →松手。紧凑落位=宿主右下角，拖动走廊向左上（右移即撞面板右
     *    缘）。每步断言几何真移动（防拖动未接管空转假绿）+ 全帧合成
     *    断言（残影=旧位键盘像素，字节比对必红）。 */
    gx = kg0.x + bar.x + bar.width / 4 + 3;
    gy = kg0.y + bar.y + bar.height / 2;
    t_kbMouseEvent(kb, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                   XMouseButton_LeftButton, XMouseButton_LeftButton, gx, gy);
    t_expect(kb->m_compactDrag, "drag armed after toolbar blank press");
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    flushBefore = g_flushBsCount;
    panBefore = t_panCalls;
    for (step = 1; step <= 4; ++step)
    {
        char tag[64];
        XRect kgNow;
        unsigned long f0 = g_flushBsCount;
        XThread_msleep(25); /* 放行 60fps 手势限帧闸（同对话框测试）。 */
        gx -= 13;
        gy -= 9;
        t_kbMouseEvent(kb, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton,
                       XMouseButton_LeftButton, gx, gy);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        kgNow = XWidget_geometry((XWidget*)kb);
        snprintf(tag, sizeof(tag), "move-out step %d", step);
        t_expect(kgNow.x == kg0.x - 13 * step && kgNow.y == kg0.y - 9 * step,
                 tag); /* 几何未动=拖移未接管，本测试自证失效。 */
        t_assertComposite(host, (XWidget*)kb, tag);
        flushMovesSum += g_flushBsCount - f0;
        if (t_debug())
            fprintf(stderr, "[KD] step %d flushDelta=%lu\n", step,
                    g_flushBsCount - f0);
    }
    for (step = 1; step <= 4; ++step)
    {
        char tag[64];
        XRect kgNow;
        unsigned long f0 = g_flushBsCount;
        XThread_msleep(25);
        gx += 13;
        gy += 9;
        t_kbMouseEvent(kb, XEVENT_TYPE_MOUSE_MOVE, XMouseButton_NoButton,
                       XMouseButton_LeftButton, gx, gy);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        kgNow = XWidget_geometry((XWidget*)kb);
        snprintf(tag, sizeof(tag), "move-back step %d", step);
        t_expect(kgNow.x == kg0.x - 13 * (4 - step) &&
                     kgNow.y == kg0.y - 9 * (4 - step),
                 tag);
        t_assertComposite(host, (XWidget*)kb, tag);
        flushMovesSum += g_flushBsCount - f0;
        if (t_debug())
            fprintf(stderr, "[KD] back %d flushDelta=%lu\n", step,
                    g_flushBsCount - f0);
    }
    t_kbMouseEvent(kb, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                   XMouseButton_LeftButton, XMouseButton_NoButton, gx, gy);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    flushMovesMark = g_flushBsCount; /* 释放重绘基线（整窗 repaint 预期）。 */
    t_expect(!kb->m_compactDrag, "drag ended on release");
    t_expect(t_assertComposite(host, (XWidget*)kb, "release sweep") == 0,
             "release sweep keeps composite frame");
    {
        XRect kgEnd = XWidget_geometry((XWidget*)kb);
        t_expect(kgEnd.x == kg0.x && kgEnd.y == kg0.y,
                 "round-trip geometry restored");
    }

    /* 5) 拖动步 flush 量化：纯移动内容零变化，逐 MOVE 的 flushBacking
     *    Store（全键盘软件重绘+整窗提交）即真机 FPS 5.9 的主因。修复后
     *    拖动 8 步 flush 增量必须为 0（提交走条带归位+后备缓冲直搬，
     *    绕过 flush；释放整窗 repaint 落定不在此账，KD_GATE=1 打开）。 */
    if (getenv("KD_GATE"))
    {
        /* 闸=逐 MOVE 步的 flush 增量总和（8 步必须全零）；释放整窗
         * repaint 落定与后续派发不在此账（flushMovesMark 仅供打印）。 */
        t_expect(flushMovesSum == 0,
                 "no flushBackingStore during pure-move drag steps");
    }
    fprintf(stderr,
            "[KD] move flushes=%lu (section %lu) pans=%d "
            "(before: flush=%lu pan=%d)\n",
            flushMovesSum, flushMovesMark - flushBefore,
            t_panCalls - panBefore, flushBefore, panBefore);

    if (t_failures == 0)
        fprintf(stderr, "[KD-PASS] keyboard compact drag ok (panCalls=%d)\n",
                t_panCalls);
    XClassDelete((XClass*)kb);
    XClassDelete(editor);
    XClassDelete(landmark);
    XClassDelete(host);
    XClassDelete(app);
    XPlatformDisplayDriver_unregister(&t_ops);
    return t_failures ? 1 : 0;
}

#else /* !XGUI_ON || !XPLATFORM_FBDEV_ON || !XVIRTUALKEYBOARD_ON */

int main(void)
{
    fprintf(stderr,
            "[KD-SKIP] requires XGUI_ON && XPLATFORM_FBDEV_ON && "
            "XVIRTUALKEYBOARD_ON (build with -DXPLATFORM_FBDEV_ON=ON)\n");
    return 0;
}

#endif /* XGUI_ON && XPLATFORM_FBDEV_ON && XVIRTUALKEYBOARD_ON */
