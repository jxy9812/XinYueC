/** @file       XGuiMirrorFidelityTest.c
 *  @brief      perf9 路2：镜像保真回环测试（白块时序口/换页脏区完整性/
 *              档位热切换/FB_REQUEST 首帧实效）。
 *  @details    伪 fbdev 显示驱动（RAM 帧缓冲，XGuiDialogMoveTest 同款）+
 *              真实 XGuiServer(host 主窗)/XGuiClient（回环设备对，零网络）
 *              的离屏端到端：
 *               - 白块时序口（假说：点击→让位条带→捕获撞还原→tile 编到
 *                 桌面色 0xEF7D）：弹层开关风暴 + 弹层拖拽出板（让位条带
 *                 含真桌面暴露→面板必现 0xEF7D 填色）后，断言客户端镜像
 *                 逐像素 == 主窗后备缓冲（0xEF7D 一旦进镜像即字节不符）。
 *                 面板侧另扫 0xEF7D 证明刺激真实落到直写路径（测试自证
 *                 不空转）。
 *               - 换页脏区完整性（差带账本家族第四例排查）：页切换后
 *                 泵至稳态，镜像逐像素 == 新页模型；差分像素里统计「旧页
 *                 残留指纹」（旧页主色）——回环零丢包下任何残留=采集/
 *                 认领/标脏真缺陷；全 0 则叠影系交付崩塌症状（路1 矩阵），
 *                 本项只兜底不重复修。
 *               - 热切换不崩：会话中档位 resource→performance→resource，
 *                 每档稳态保真断言。
 *               - FB_REQUEST 首帧实效（任务5）：新客户端首帧全量时延
 *                 A/B（默认 vs XGUI_REMOTE_FB_REQUEST_OFF=1），performance
 *                 与 resource 各跑一轮。
 *              须 -DXPLATFORM_FBDEV_ON=ON（假驱动契约与 XWindow/setVisible
 *              的 fbdev 弹层暴露恢复分支均受该门控）构建才激活；桌面默认
 *              构建编出 SKIP 壳。BUFFER_COUNT=2 走真机同款双缓冲差带账本。
 *  @author     XinYueC 团队（perf9 战役）
 ******************************************************************************/
#include "XGuiConfig.h"
#include "XGuiRemoteProto.h" /* XGUI_REMOTE_ON 单一来源（XGuiRemoteTest 惯例）。 */

#if XWIDGET_ON && XGUI_REMOTE_ON && XGUI_ON && XPLATFORM_FBDEV_ON

#include "XGuiApplication.h"
#include "XWidget.h"
#include "XObject.h"
#include "XEvent.h"
#include "XPushButton.h"
#include "XPalette.h"
#include "XGuiServer.h"
#include "XGuiClient.h"
#include "XGuiRemoteLoopback.h"
#include "XWindowDecoration.h"
#include "XTitleBar.h"
#include "XStyleOption.h"
#include "XWindowSystemInterface.h"
#include "XPlatformDisplayDriver.h"
#include "XPlatformBackingStore.h"
#include "XBackingStore.h"
#include "XImage.h"
#include "XImageFormat.h"
#include "XDateTime.h"
#include "XThread.h"
#include "XMemory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==================== 伪 fbdev 显示驱动（RAM 帧缓冲） ==================== */

/* 面板 960x600: 左上 (0,0)+(320x220)=镜像宿主窗; 右下 (600,340)+(
 * 320x260)=镜像客户端窗——客户端的上屏结果直接呈现在同一面板上，
 * 断言用「面板区域 vs 面板区域」平移比对（设备 fb=用户所见唯一真值，
 * 规避 XWidget_grab 路径1 读 FULL+BUFFER_COUNT=2 翻会后陈旧半区的
 * 快照伪影——离屏实证：同一时刻 grab 返回 2 帧前的占位灰）。 */
#define MF_PANEL_W 960
#define MF_PANEL_H 600
#define MF_HOST_X 0
#define MF_HOST_Y 0
#define MF_HOST_W 320
#define MF_HOST_H 220
#define MF_CLI_X 600
#define MF_CLI_Y 340
#define MF_STRIDE ((size_t)MF_PANEL_W * 2 + 64)

static uint8_t mf_fb[MF_STRIDE * MF_PANEL_H * 2];
static int mf_visible;

static bool mf_probe(XPlatformDisplayInfo* outInfo)
{
    if (!outInfo) return true;
    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->m_width = MF_PANEL_W;
    outInfo->m_height = MF_PANEL_H;
    outInfo->m_format = XImageFormat_RGB16;
    outInfo->m_bitsPerPixel = 16;
    outInfo->m_stride = MF_STRIDE;
    outInfo->m_frameBuffer = mf_fb;
    outInfo->m_frameBufferSize = sizeof(mf_fb);
    outInfo->m_doubleBuffered = true;
    return true;
}

static bool mf_negotiate(XImageFormat preferred, XImageFormat* outFormat)
{
    if (!outFormat) return false;
    *outFormat = preferred;
    return true;
}

static bool mf_pan(int bufferIndex)
{
    if (bufferIndex != 0 && bufferIndex != 1) return false;
    mf_visible = bufferIndex;
    return true;
}

static bool mf_cacheSync(XPlatformDisplayCacheMode mode, void* address,
                         size_t length)
{
    (void)mode; (void)address; (void)length;
    return true;
}

static bool mf_waitVsync(int timeoutMilliseconds)
{
    (void)timeoutMilliseconds;
    return true;
}

static size_t mf_stride(int width, XImageFormat format)
{
    (void)format;
    return width > 0 ? (size_t)width * 2 + 64 : 0;
}

static const XPlatformDisplayDriverOps mf_ops = {
    "mirrorfidelity-test-fbdev", XPLATFORM_DISPLAY_DRIVER_ABI_VERSION,
    mf_probe,    mf_negotiate, mf_pan,
    mf_cacheSync, mf_waitVsync, mf_stride
};

/* ==================== 断言设施 ==================== */

static int mf_failures = 0;

static void mf_expect(bool cond, const char* what)
{
    if (!cond)
    {
        fprintf(stderr, "[MF-FAIL] %s\n", what ? what : "");
        ++mf_failures;
    }
}

static int64_t mf_nowMs(void)
{
    return XDateTime_currentMSecsSinceEpoch();
}

/** @brief 任意表面格式像素 → 0xFFRRGGBB（测试仅用 RGB565/ARGB32）。
 *  @note  窄→宽扩展用位复制（g6: (v<<2)|(v>>4)，与框架 painter/codec
 *         往返口径一致；四舍五入版会产生 ±1 假失配——R0 实证）。 */
static uint32_t mf_pixelAt(const XImage* img, int x, int y)
{
    const uint8_t* row;
    int bpl = XImage_bytesPerLine(img);
    if (!img || !img->m_data) return 0;
    row = XImage_constBits(img) + (size_t)y * (size_t)bpl;
    if (XImage_format(img) == XImageFormat_RGB16)
    {
        unsigned v = (unsigned)row[x * 2] | ((unsigned)row[x * 2 + 1] << 8);
        unsigned r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
        return 0xFF000000u | (((r << 3) | (r >> 2)) << 16) |
               (((g << 2) | (g >> 4)) << 8) | ((b << 3) | (b >> 2));
    }
    {
        /* ARGB32 小端：B,G,R,A。 */
        unsigned b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2];
        return 0xFF000000u | (r << 16) | (g << 8) | b;
    }
}

/** @brief 面板可见缓冲 u16 原值（RGB565）。 */
static unsigned mf_panelRaw(int x, int y)
{
    const uint8_t* vis = mf_fb + (size_t)mf_visible *
                         (size_t)MF_PANEL_H * MF_STRIDE;
    if (x < 0 || y < 0 || x >= MF_PANEL_W || y >= MF_PANEL_H) return 0;
    return (unsigned)vis[y * MF_STRIDE + x * 2] |
           ((unsigned)vis[y * MF_STRIDE + x * 2 + 1] << 8);
}

static void mf_dumpPpm(const char* tag, int barH); /* 前置: 比对失败取证用。 */

/** @brief 镜像保真断言（客户端后备缓冲直读版）。
 *  @details 先 XWidget_repaint(client)（同步整窗 paint→flush，bb=整窗，
 *           活动缓冲即 m_fb 全量新翻）再走 XWidget_grab 路径1 深拷贝
 *           后备缓冲——规避跨窗共享 fbdev 差带账本/翻页序的互相污染
 *           （真机面板只有宿主进程在呈现，客户端窗摆上同一伪面板构造
 *           的面板读数属测试伪象，R4/R5 黑屏实证为伪象）。真值=宿主
 *           窗面板区（用户在设备屏所见）。
 *           exclude=弹层矩形（面板坐标=宿主坐标；镜像语义=宿主内容，
 *           弹层覆盖区一并跳过）。
 *  @return 失配像素数；leak=0xEF7D 计数（仅失配像素计——宿主标题栏
 *          本身就是桌面灰底，匹配像素不计）、old=旧页主色残留计数
 *          （ARGB 快照域）经出参带回。 */
static int mf_compareMirror(XGuiClient* client, int barH,
                            const XRect* exclude, uint32_t oldPageColor,
                            int* outLeak, int* outOld, const char* tag)
{
    XImage* snap;
    const uint8_t* vis = mf_fb + (size_t)mf_visible *
                         (size_t)MF_PANEL_H * MF_STRIDE;
    int x, y, bad = 0, leak = 0, old = 0;
    if (!client) return -1;
    /* 客户端自家 CSD 标题栏覆盖其后备缓冲顶部 barH+1 行(实证校准:
     * barH 行仍为条底), 镜像帧顶带被盖不参与比对。 */
    int y0 = barH > 0 ? barH + 1 : 0;
    XWidget_repaint((XWidget*)client); /* 同步整窗重绘→活动缓冲=最新。 */
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    snap = XWidget_grab((XWidget*)client);
    if (!snap)
    {
        fprintf(stderr, "[MF-FAIL] %s: snap missing\n", tag ? tag : "");
        ++mf_failures;
        return -1;
    }
    for (y = y0; y < MF_HOST_H; ++y)
    {
        for (x = 0; x < MF_HOST_W; ++x)
        {
            uint32_t a = mf_pixelAt(snap, x, y);
            unsigned pv = (unsigned)vis[y * MF_STRIDE + x * 2] |
                          ((unsigned)vis[y * MF_STRIDE + x * 2 + 1] << 8);
            unsigned pr = (pv >> 11) & 0x1F, pg = (pv >> 5) & 0x3F,
                     pb = pv & 0x1F;
            uint32_t e = 0xFF000000u | (((pr << 3) | (pr >> 2)) << 16) |
                         (((pg << 2) | (pg >> 4)) << 8) |
                         ((pb << 3) | (pb >> 2));
            if (exclude && x >= exclude->x &&
                x < exclude->x + exclude->width &&
                y >= exclude->y && y < exclude->y + exclude->height)
                continue;
            if (a != e)
            {
                if (bad == 0)
                    fprintf(stderr, "[MF-FAIL] %s: first mismatch (%d,%d) "
                                    "act=%06X exp=%06X\n",
                            tag ? tag : "", x, y, a & 0xFFFFFF, e & 0xFFFFFF);
                if (a == 0xFFEFEFEFu)
                    ++leak; /* 桌面色指纹仅计失配像素(宿主标题栏本身即
                             * 桌面灰底, 匹配不计)。 */
                if (oldPageColor && a == oldPageColor) ++old;
                ++bad;
            }
        }
    }
    if (outLeak) *outLeak = leak;
    if (outOld) *outOld = old;
    if (getenv("MF_DUMP"))
    {
        /* [perf9 路2 诊断] 每轮把客户端 grab(ARGB→RGB888)与面板宿主区
         * (RAW565 小端原值)落盘+按 16 行带打失配直方图, 供像素级取证。 */
        char path[128];
        FILE* f;
        int band[MF_HOST_H / 16 + 1];
        memset(band, 0, sizeof(band));
        snprintf(path, sizeof(path), "/tmp/mfc_%s_snap.ppm",
                 tag ? tag : "x");
        f = fopen(path, "wb");
        if (f)
        {
            fprintf(f, "P6\n%d %d\n255\n", MF_HOST_W, MF_HOST_H);
            for (y = 0; y < MF_HOST_H; ++y)
                for (x = 0; x < MF_HOST_W; ++x)
                {
                    uint32_t p = mf_pixelAt(snap, x, y);
                    fputc((p >> 16) & 0xFF, f);
                    fputc((p >> 8) & 0xFF, f);
                    fputc(p & 0xFF, f);
                }
            fclose(f);
        }
        snprintf(path, sizeof(path), "/tmp/mfc_%s_panel565.raw",
                 tag ? tag : "x");
        f = fopen(path, "wb");
        if (f)
        {
            fprintf(f, "MF565 %d %d %d\n", MF_HOST_W, MF_HOST_H, y0);
            for (y = 0; y < MF_HOST_H; ++y)
                for (x = 0; x < MF_HOST_W; ++x)
                {
                    unsigned v = mf_panelRaw(x, y);
                    fputc((uint8_t)(v & 0xFF), f);
                    fputc((uint8_t)(v >> 8), f);
                }
            fclose(f);
        }
        /* 重扫一遍统计带分布(与上方同一失配口径)。 */
        for (y = y0; y < MF_HOST_H; ++y)
            for (x = 0; x < MF_HOST_W; ++x)
            {
                uint32_t a = mf_pixelAt(snap, x, y);
                unsigned pv = (unsigned)vis[y * MF_STRIDE + x * 2] |
                              ((unsigned)vis[y * MF_STRIDE + x * 2 + 1] << 8);
                unsigned pr = (pv >> 11) & 0x1F, pg = (pv >> 5) & 0x3F,
                         pb = pv & 0x1F;
                uint32_t e = 0xFF000000u |
                             (((pr << 3) | (pr >> 2)) << 16) |
                             (((pg << 2) | (pg >> 4)) << 8) |
                             ((pb << 3) | (pb >> 2));
                if (a != e) ++band[y / 16];
            }
        fprintf(stderr, "[MF-DUMP] %s bands16:", tag ? tag : "x");
        for (y = 0; y < (int)(MF_HOST_H / 16 + 1); ++y)
            fprintf(stderr, " %d", band[y]);
        fprintf(stderr, "\n");
    }
    XClassDelete(snap);
    return bad;
}

/** @brief 可见面板当前可见缓冲里的桌面色 0xEF7D 像素计数（刺激落笔
 *  自证：拖拽出板后必 >0，证明白条带真出现在直写路径上）。 */
static int mf_panelDesktopPixels(void)
{
    const uint8_t* vis = mf_fb + (size_t)mf_visible *
                         (size_t)MF_PANEL_H * MF_STRIDE;
    int x, y, n = 0;
    for (y = 0; y < MF_PANEL_H; ++y)
    {
        for (x = 0; x < MF_PANEL_W; ++x)
        {
            unsigned v = (unsigned)vis[y * MF_STRIDE + x * 2] |
                         ((unsigned)vis[y * MF_STRIDE + x * 2 + 1] << 8);
            if (v == 0xEF7Du) ++n;
        }
    }
    return n;
}

/** @brief MF_DUMP=1 时把面板宿主区/镜像显示区裁剪落 PPM（诊断用）。 */
static void mf_dumpPpm(const char* tag, int barH)
{
    FILE* f;
    char path[128];
    const uint8_t* vis = mf_fb + (size_t)mf_visible *
                         (size_t)MF_PANEL_H * MF_STRIDE;
    int x, y;
    if (!getenv("MF_DUMP")) return;
    snprintf(path, sizeof(path), "/tmp/mf_%s_full.ppm", tag);
    f = fopen(path, "wb");
    if (f)
    {
        fprintf(f, "P6\n%d %d\n255\n", MF_PANEL_W, MF_PANEL_H);
        for (y = 0; y < MF_PANEL_H; ++y)
            for (x = 0; x < MF_PANEL_W; ++x)
            {
                unsigned v = mf_panelRaw(x, y);
                fputc(((v >> 11) & 0x1F) << 3, f);
                fputc(((v >> 5) & 0x3F) << 2, f);
                fputc((v & 0x1F) << 3, f);
            }
        fclose(f);
    }
    snprintf(path, sizeof(path), "/tmp/mf_%s_host.ppm", tag);
    f = fopen(path, "wb");
    if (f)
    {
        fprintf(f, "P6\n%d %d\n255\n", MF_HOST_W, MF_HOST_H);
        for (y = 0; y < MF_HOST_H; ++y)
            for (x = 0; x < MF_HOST_W; ++x)
            {
                unsigned v = mf_panelRaw(x, y);
                fputc(((v >> 11) & 0x1F) << 3, f);
                fputc(((v >> 5) & 0x3F) << 2, f);
                fputc((v & 0x1F) << 3, f);
            }
        fclose(f);
    }
    snprintf(path, sizeof(path), "/tmp/mf_%s_mirror.ppm", tag);
    f = fopen(path, "wb");
    if (f)
    {
        fprintf(f, "P6\n%d %d\n255\n", MF_HOST_W, MF_HOST_H);
        for (y = 0; y < MF_HOST_H; ++y)
            for (x = 0; x < MF_HOST_W; ++x)
            {
                unsigned v = mf_panelRaw(MF_CLI_X + x, MF_CLI_Y + y);
                fputc(((v >> 11) & 0x1F) << 3, f);
                fputc(((v >> 5) & 0x3F) << 2, f);
                fputc((v & 0x1F) << 3, f);
            }
        fclose(f);
    }
    (void)vis;
}

/* ==================== 泵与稳态等待（XGuiRemoteTest 同纪律） ==================== */

/** @brief 泵至客户端 tile/update 计数 stableMs 无增长，上限 timeoutMs。 */
static void mf_pumpStable(XGuiClient* client, int stableMs, int timeoutMs)
{
    XGuiRemoteStats st, last;
    int64_t lastChange = mf_nowMs();
    int64_t deadline = mf_nowMs() + timeoutMs;
    XGuiClient_statistics(client, &last);
    while (mf_nowMs() < deadline)
    {
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XThread_msleep(5);
        XGuiClient_statistics(client, &st);
        if (st.tileCount != last.tileCount ||
            st.updateCount != last.updateCount)
        {
            last = st;
            lastChange = mf_nowMs();
        }
        else if (mf_nowMs() - lastChange >= stableMs)
        {
            break;
        }
    }
    XGuiApplication_processEvents(XEventLoop_AllEvents);
}

/* ==================== 调色板小工具 ==================== */

static void mf_paintColor(XWidget* w, int r, int g, int b)
{
    XPalette pal;
    XColor c;
    XPalette_init_default(&pal);
    XColor_init_rgb(&c, r, g, b, 255);
    XPalette_setColor(&pal, XPaletteColorGroup_Active,
                      XPaletteColorRole_Window, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Inactive,
                      XPaletteColorRole_Window, c);
    XPalette_setColor(&pal, XPaletteColorGroup_Disabled,
                      XPaletteColorRole_Window, c);
    XWidget_setPalette(w, &pal);
    XWidget_setAutoFillBackground(w, true);
}

/* ==================== 刺激注入 ==================== */

static void mf_injectClick(XWidget* top, int gx, int gy)
{
    XWindow* win = XWidget_windowHandle(top);
    XPoint local;
    XPoint global;
    if (!win) return;
    XPoint_init(&local, gx, gy); /* 主窗在 (0,0)：局部==全局。 */
    XPoint_init(&global, gx, gy);
    (void)XWindowSystemInterface_handleMouseEvent_ex(
        win, XEVENT_TYPE_MOUSE_BUTTON_PRESS, XMouseButton_LeftButton,
        XMouseButton_LeftButton, 0, local, &global, (uint32_t)mf_nowMs());
    (void)XWindowSystemInterface_handleMouseEvent_ex(
        win, XEVENT_TYPE_MOUSE_BUTTON_RELEASE, XMouseButton_LeftButton,
        XMouseButton_NoButton, 0, local, &global, (uint32_t)mf_nowMs());
}

/* ==================== 槽 ==================== */

static XWidget* mf_pageA;
static XWidget* mf_pageB;
static XWidget* mf_dialog;
static int mf_dialogOpen;
static XWidget* mf_parentForClick;
static int mf_btnPageX = 52, mf_btnPageY = 45;
static int mf_btnDlgX = 148, mf_btnDlgY = 45;
static int mf_cliErrors;
static int mf_cliDisconnects;
static int mf_cliLastReason;

static void mf_onCliError(XObject* sender, XVarList* args)
{
    (void)sender;
    ++mf_cliErrors;
    fprintf(stderr, "[MF] client errorOccurred #%d\n", mf_cliErrors);
}

static void mf_onCliDisconnected(XObject* sender, XVarList* args)
{
    (void)sender;
    ++mf_cliDisconnects;
    mf_cliLastReason = -1;
    if (args)
    {
        XVarList_start(args);
        mf_cliLastReason = XVarList_arg(args, int);
    }
    fprintf(stderr, "[MF] client disconnected #%d reason=%d\n",
            mf_cliDisconnects, mf_cliLastReason);
}

static void mf_onSrvDisconnected(XObject* sender, XVarList* args)
{
    int sessionId = -1;
    (void)sender;
    if (args)
    {
        XVarList_start(args);
        sessionId = XVarList_arg(args, int);
    }
    fprintf(stderr, "[MF] server clientDisconnected session=%d\n",
            sessionId);
}

static void mf_onSrvError(XObject* sender, XVarList* args)
{
    int sessionId = -1;
    int errCode = -1;
    (void)sender;
    if (args)
    {
        XVarList_start(args);
        sessionId = XVarList_arg(args, int);
        errCode = XVarList_arg(args, int);
    }
    fprintf(stderr, "[MF] server sessionError session=%d err=%d\n",
            sessionId, errCode);
}

static void mf_onPageClicked(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    /* 换页：当前页 hide、另一页 show（XStackedWidget 语义的最小模型）。 */
    if (XWidget_isVisible(mf_pageA))
    {
        XWidget_hide(mf_pageA);
        XWidget_show(mf_pageB);
    }
    else
    {
        XWidget_hide(mf_pageB);
        XWidget_show(mf_pageA);
    }
}

static void mf_onDlgClicked(XObject* sender, XVarList* args)
{
    (void)sender; (void)args;
    if (mf_dialogOpen)
    {
        XWidget_hide(mf_dialog);
        mf_dialogOpen = 0;
    }
    else
    {
        XWidget_show(mf_dialog);
        mf_dialogOpen = 1;
    }
}

/** @brief 弹层状态收敛到目标态（点击至多 2 次，状态漂移防御）。 */
static void mf_setDialog(bool open)
{
    int guard;
    for (guard = 0; guard < 2 && XWidget_isVisible(mf_dialog) != open;
         ++guard)
    {
        mf_injectClick(mf_parentForClick, mf_btnDlgX, mf_btnDlgY);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
    }
}

/* ==================== 用例 ==================== */

int main(void)
{
    XGuiApplication* app = NULL;
    XWidget* parent = NULL;
    XWidget* landmark = NULL;
    XWidget* btnPage = NULL;
    XWidget* btnDlg = NULL;
    XGuiServer* server = NULL;
    XGuiClient* client = NULL;
    XGuiRemoteLoopbackDevice* devA = NULL;
    XGuiRemoteLoopbackDevice* devB = NULL;
    int sessionId;
    int bad;
    int leak;
    int oldResidue;
    int i;
    int mfBarH = XTitleBar_defaultHeight(); /* 客户端窗 CSD 条高(镜像
                                             * 显示区纵向偏移)。 */
    uint32_t pageAColor565 = 0; /* 旧页主色指纹(RGB565 原值)。 */

    if (!XPlatformDisplayDriver_register(&mf_ops))
    {
        fprintf(stderr, "[MF-FAIL] fake display driver register\n");
        return 1;
    }
    app = XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL);
    if (!app)
    {
        fprintf(stderr, "[MF-FAIL] app create\n");
        return 1;
    }

    /* 1) 主窗（镜像宿主）：320x220（板 240 高留 20px 真桌面带，供
     *    越界条带 0xEF7D 落点）+ 深绿底 + 橙地标 + 两个整幅页 + 按钮。 */
    parent = XWidget_create(NULL, 0);
    if (!parent)
    {
        fprintf(stderr, "[MF-FAIL] parent create\n");
        return 1;
    }
    XWidget_setGeometry(parent, MF_HOST_X, MF_HOST_Y, MF_HOST_W, MF_HOST_H);
    mf_paintColor(parent, 0, 128, 0); /* 深绿（≠0xEF7D 桌面色）。 */

    mf_pageA = XWidget_create(parent, 0);
    XWidget_setGeometry(mf_pageA, 0, 60, MF_HOST_W, MF_HOST_H - 60);
    mf_paintColor(mf_pageA, 0xD9, 0x30, 0x25); /* 红。 */
    XWidget_show(mf_pageA);

    mf_pageB = XWidget_create(parent, 0);
    XWidget_setGeometry(mf_pageB, 0, 60, MF_HOST_W, MF_HOST_H - 60);
    mf_paintColor(mf_pageB, 0x2E, 0x9E, 0x44); /* 绿蓝页。 */
    XWidget_hide(mf_pageB); /* 子控件缺省自显形: 显式压住, 换页刺激由
                             * btnPage 交替显隐(否则 pageB 恒盖 pageA)。 */

    landmark = XWidget_create(mf_pageA, 0);
    XWidget_setGeometry(landmark, 40, 60, 40, 40);
    mf_paintColor(landmark, 255, 128, 0); /* 橙地标（旧页残留判据补强）。 */
    XWidget_show(landmark);

    btnPage = XPushButton_create(parent, 0);
    XWidget_setGeometry(btnPage, 8, 34, 88, 22); /* 标题栏(barH~29)之下。 */
    XPushButton_setText_2(btnPage, "换页");
    XObject_connect_2((XObject*)btnPage,
                      XSignal(XAbstractButton_clicked_signal),
                      mf_onPageClicked);
    XWidget_show(btnPage);

    btnDlg = XPushButton_create(parent, 0);
    XWidget_setGeometry(btnDlg, 104, 34, 88, 22);
    XPushButton_setText_2(btnDlg, "弹层");
    XObject_connect_2((XObject*)btnDlg,
                      XSignal(XAbstractButton_clicked_signal),
                      mf_onDlgClicked);
    XWidget_show(btnDlg);

    XWidget_show(parent);
    mf_parentForClick = parent;
    XWidget_repaint(parent);
    XGuiApplication_processEvents(XEventLoop_AllEvents);
    mf_dumpPpm("setup", 0);

    /* 2) 弹层：独立顶层，深蓝，压在页面上（镜像语义=宿主主窗内容，
     *    弹层永不该出现在镜像里）。初始隐藏——R0 基线=纯宿主画面。 */
    mf_dialog = XWidget_create(NULL, 0);
    if (!mf_dialog)
    {
        fprintf(stderr, "[MF-FAIL] dialog create\n");
        return 1;
    }
    XWidget_setGeometry(mf_dialog, 90, 70, 120, 80);
    mf_paintColor(mf_dialog, 0, 0, 255);
    /* 不 show：R0 前保持关闭；mf_dialogOpen 与实际可见性对齐。 */
    mf_dialogOpen = 0;
    XGuiApplication_processEvents(XEventLoop_AllEvents);

    /* 3) 服务器+客户端（回环设备对，零网络；performance 档起步=真机
     *    出事档）。 */
    server = XGuiServer_create(NULL);
    if (!server)
    {
        fprintf(stderr, "[MF-FAIL] server create\n");
        return 1;
    }
    XGuiServer_setProfileId(server,
        (getenv("MF_PROFILE") && strcmp(getenv("MF_PROFILE"), "resource") == 0)
            ? XGUI_REMOTE_PROFILE_RESOURCE
        : (getenv("MF_PROFILE") && strcmp(getenv("MF_PROFILE"), "latency") == 0)
            ? XGUI_REMOTE_PROFILE_LATENCY
            : XGUI_REMOTE_PROFILE_PERFORMANCE);
    if (!XGuiServer_host(server, parent))
    {
        mf_expect(false, "XGuiServer_host");
        goto done;
    }
    devA = XGuiRemoteLoopbackDevice_createPair(4u * 1024u * 1024u, &devB);
    if (!devA || !devB)
    {
        mf_expect(false, "loopback pair");
        goto done;
    }
    sessionId = XGuiServer_attachTransport(server, (XIODevice*)devA);
    mf_expect(sessionId > 0, "attachTransport");
    XObject_connect_2((XObject*)server,
                      XSignal(XGuiServer_clientDisconnected_signal),
                      mf_onSrvDisconnected);
    XObject_connect_2((XObject*)server,
                      XSignal(XGuiServer_sessionError_signal),
                      mf_onSrvError);
    client = XGuiClient_create(NULL, 0);
    if (!client)
    {
        mf_expect(false, "client create");
        goto done;
    }
    XGuiClient_setTransport(client, (XIODevice*)devB);
    XObject_connect_2((XObject*)client,
                      XSignal(XGuiClient_errorOccurred_signal),
                      mf_onCliError);
    XObject_connect_2((XObject*)client,
                      XSignal(XGuiClient_disconnected_signal),
                      mf_onCliDisconnected);
    XWidget_setGeometry((XWidget*)client, MF_CLI_X, MF_CLI_Y,
                        MF_HOST_W, MF_HOST_H); /* 与远端帧同尺寸(免 FIT
                        居中偏移); 同面板右下角: 上屏结果=镜像显示区
                        (真值断言源); 不与宿主区重叠。 */
    XWidget_show((XWidget*)client); /* 后备存储/paint 闭环需要可见态
                                     * (postPaintEvent 的 m_visible 门)。 */

    /* ---- R0 基线：首帧全量交付后镜像==面板（弹层关闭=纯宿主画面）。 */
    {
    mf_pumpStable(client, 300, 10000);
    {
        XGuiRemoteStats st;
        XGuiClient_statistics(client, &st);
        fprintf(stderr,
                "[MF] R0 baseline tiles=%u updates=%u fpsMilli=%u\n",
                st.tileCount, st.updateCount, st.fpsMilli);
        mf_expect(st.tileCount > 0, "R0 tiles delivered");
    }
    bad = mf_compareMirror(client, mfBarH, NULL, 0, &leak,
                           &oldResidue, "R0");
    mf_dumpPpm("r0", mfBarH);
    mf_expect(bad == 0, "R0 mirror == panel (full delivery)");
    if (bad)
        fprintf(stderr, "[MF] R0 mismatch=%d leak(0xEF7D)=%d\n", bad, leak);
    }

    /* ---- R1 点击风暴（弹层开关×6）：每轮开关弹层（面板让位条带/
     *      expose 恢复链活跃）后镜像（宿主域）必须仍==面板——白块
     *      时序口（捕获撞还原）若存在，条带 0xEF7D/陈旧面板像素必进
     *      tile；弹层在位轮 exclude 弹层矩形（镜像语义=宿主内容）。 */
    {
    for (i = 0; i < 6; ++i)
    {
        char tag[48];
        XRect exclude;
        bool open;
        mf_injectClick(parent, mf_btnDlgX, mf_btnDlgY);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        mf_pumpStable(client, 150, 5000);
        open = XWidget_isVisible(mf_dialog);
        if (open)
        {
            XRect dg = XWidget_geometry(mf_dialog);
            exclude = dg;
        }
        snprintf(tag, sizeof(tag), "R1 dlg-toggle %d", i);
        bad = mf_compareMirror(client, mfBarH,
                                      open ? &exclude : NULL, 0, &leak,
                                      NULL, tag);
        mf_expect(bad == 0, tag);
        if (bad)
            fprintf(stderr, "[MF] %s mismatch=%d leak=%d\n", tag, bad, leak);
    }
    mf_setDialog(false); /* 收敛到关闭（奇偶无关防御）。 */
    mf_pumpStable(client, 150, 5000);
    mf_expect(!XWidget_isVisible(mf_dialog), "R1 dialog closed at end");
    }

    /* ---- R2 拖拽出板（让位条带还原+真桌面带 0xEF7D 填色必上面板；
     *      镜像（宿主域，exclude 弹层）必须仍==面板——「还原完成前
     *      tile 编到白条带」假说的决定性断言）。 ---- */
    {
        XRect dg0 = XWidget_geometry(mf_dialog);
        XRect hostArea;
        int barH = XTitleBar_defaultHeight();
        int gx = 0, gy = 0;
        int blankX = -1;
        int bx;
        int panelWhite0, panelWhiteMax = 0;
        XWidget* barW;
        XPoint probe;
        XRect_init(&hostArea, 0, 0, MF_PANEL_W, MF_PANEL_H - 20);
        XPoint_init(&probe, dg0.width / 2, barH / 2);
        barW = XWidget_childAt(mf_dialog, &probe);
        if (barW && XTitleBar_isBar(barW))
        {
            XTitleBar* bar = (XTitleBar*)barW;
            for (bx = 0; bx < dg0.width && blankX < 0; ++bx)
            {
                XPoint p;
                XPoint_init(&p, bx, barH / 2);
                if (XTitleBar_hitTest_base(bar, &p) != XStyleSC_TitleBarSysMenu &&
                    XTitleBar_hitTest_base(bar, &p) != XStyleSC_TitleBarMinButton &&
                    XTitleBar_hitTest_base(bar, &p) != XStyleSC_TitleBarMaxButton &&
                    XTitleBar_hitTest_base(bar, &p) != XStyleSC_TitleBarNormalButton &&
                    XTitleBar_hitTest_base(bar, &p) != XStyleSC_TitleBarCloseButton)
                    blankX = bx;
            }
        }
        mf_expect(blankX >= 0, "R2 blank title-bar anchor");
        gx = dg0.x + (blankX >= 0 ? blankX : 0);
        gy = dg0.y + (barH > 1 ? barH / 2 : 1);
        /* 打开弹层再拖。 */
        mf_setDialog(true);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        mf_expect(XWidget_isVisible(mf_dialog), "R2 dialog reopened");
        panelWhite0 = mf_panelDesktopPixels();
        panelWhiteMax = panelWhite0;
        {
            /* 装饰状态机直投（XGuiDialogMoveTest 同款）。 */
            XMouseEvent me;
            XPoint local;
            XPoint global;
            XRect dgn = XWidget_geometry(mf_dialog);
            XPoint_init(&local, gx - dgn.x, gy - dgn.y);
            XPoint_init(&global, gx, gy);
            XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                             XMouseButton_LeftButton, 0, local);
            XMouseEvent_setButtons(&me, XMouseButton_LeftButton);
            XMouseEvent_setGlobalPosition(&me, &global);
            XWindowDecoration_handlePointer(mf_dialog, (XEvent*)&me);
        }
        mf_expect(XWindowDecoration_gestureActive(), "R2 drag armed");
        /* 拖出板外（右下越界）：每步镜像不得出现条带伪影。 */
        for (i = 1; i <= 3; ++i)
        {
            char tag[48];
            XMouseEvent me;
            XPoint local;
            XPoint global;
            XRect dgn;
            XThread_msleep(25);
            gx += 90;
            gy += 70; /* 大步长越出 320x240 板。 */
            dgn = XWidget_geometry(mf_dialog);
            XPoint_init(&local, gx - dgn.x, gy - dgn.y);
            XPoint_init(&global, gx, gy);
            XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_MOVE,
                             XMouseButton_NoButton, 0, local);
            XMouseEvent_setButtons(&me, XMouseButton_LeftButton);
            XMouseEvent_setGlobalPosition(&me, &global);
            XWindowDecoration_handlePointer(mf_dialog, (XEvent*)&me);
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            mf_pumpStable(client, 120, 5000);
            {
                int pw = mf_panelDesktopPixels();
                XGuiRemoteStats st;
                XGuiClient_statistics(client, &st);
                if (pw > panelWhiteMax) panelWhiteMax = pw;
                fprintf(stderr,
                        "[MF] R2 step %d stats tiles=%u updates=%u\n",
                        i, st.tileCount, st.updateCount);
            }
            snprintf(tag, sizeof(tag), "R2 drag-out step %d", i);
            {
                XRect dgNow = XWidget_geometry(mf_dialog);
                bad = mf_compareMirror(client, mfBarH, &dgNow, 0, &leak,
                                       NULL, tag);
            }
            mf_expect(bad == 0, tag);
            if (bad)
                fprintf(stderr, "[MF] %s mismatch=%d leak=%d\n", tag, bad,
                        leak);
        }
        /* 松手 + 关弹层（隐藏链填 0xEF7D 于越界余部）。 */
        {
            XMouseEvent me;
            XPoint local;
            XPoint global;
            XRect dgn = XWidget_geometry(mf_dialog);
            XPoint_init(&local, gx - dgn.x, gy - dgn.y);
            XPoint_init(&global, gx, gy);
            XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                             XMouseButton_LeftButton, 0, local);
            XMouseEvent_setButtons(&me, XMouseButton_NoButton);
            XMouseEvent_setGlobalPosition(&me, &global);
            XWindowDecoration_handlePointer(mf_dialog, (XEvent*)&me);
        }
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        mf_setDialog(false); /* 关闭弹层（隐藏链填 0xEF7D 于越界余部）。 */
        mf_pumpStable(client, 150, 5000);
        {
            int pw = mf_panelDesktopPixels();
            if (pw > panelWhiteMax) panelWhiteMax = pw;
        }
        fprintf(stderr,
                "[MF] R2 panel desktop-color pixels: before=%d max=%d "
                "(>0 = 刺激真实落到直写路径)\n",
                panelWhite0, panelWhiteMax);
        bad = mf_compareMirror(client, mfBarH, NULL, 0, &leak, NULL,
                               "R2 final");
        mf_expect(bad == 0, "R2 final mirror == panel");
        if (bad)
            fprintf(stderr, "[MF] R2 final mismatch=%d leak=%d\n", bad,
                    leak);
        /* 面板自证：拖出板+隐藏链必须真在面板上留下桌面色条带——
         * 若为 0，说明刺激未走 0xEF7D 路径（测试空转，结论降级）。 */
        mf_expect(panelWhiteMax > 0,
                  "R2 stimulus produced desktop strips on panel");
    }

    /* ---- R3 换页脏区完整性（回环零丢包：镜像==面板新页画面即无
     *       残留；失配像素里旧页主色计数=叠影残留指纹）。 ---- */
    (void)0; /* 旧页指纹改 ARGB 域（快照直读），见 R3 调用点。 */
    {
    for (i = 0; i < 4; ++i)
    {
        char tag[48];
        mf_injectClick(parent, mf_btnPageX, mf_btnPageY);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        mf_pumpStable(client, 200, 8000);
        snprintf(tag, sizeof(tag), "R3 page-switch %d", i);
        oldResidue = 0;
        bad = mf_compareMirror(client, mfBarH, NULL, 0xFFD93025u, &leak,
                               &oldResidue, tag);
        mf_expect(bad == 0, tag);
        fprintf(stderr,
                "[MF] %s mismatch=%d oldPageResidue=%d leak=%d\n",
                tag, bad, oldResidue, leak);
        mf_expect(oldResidue == 0, "R3 no old-page residue in mirror");
    }
    }

    /* ---- R4 档位热切换不崩：performance→resource→performance；
     *      每档稳态镜像==面板（换档黑帧/黑屏不愈合在此现形）。 ---- */
    {
        XGuiRemoteStats st0, st1;
        XGuiClient_statistics(client, &st0);
        XGuiServer_setProfileId(server, XGUI_REMOTE_PROFILE_RESOURCE);
        mf_pumpStable(client, 300, 10000);
        bad = mf_compareMirror(client, mfBarH, NULL, 0, &leak, NULL,
                               "R4->res");
        mf_expect(bad == 0, "R4 resource steady mirror");
        XGuiServer_setProfileId(server, XGUI_REMOTE_PROFILE_PERFORMANCE);
        mf_pumpStable(client, 300, 10000);
        bad = mf_compareMirror(client, mfBarH, NULL, 0, &leak, NULL,
                               "R4->perf");
        mf_expect(bad == 0, "R4 performance steady mirror");
        XGuiClient_statistics(client, &st1);
        fprintf(stderr,
                "[MF] R4 hot-switch ok tiles %u->%u\n",
                st0.tileCount, st1.tileCount);
    }

    /* ---- R5 FB_REQUEST 首帧实效（任务5）：performance 档新会话首帧
     *      全量时延 A/B（默认 vs XGUI_REMOTE_FB_REQUEST_OFF=1）。
     *      串行单会话：先拆当前客户端（BYE），再起新客户端。 ---- */
    {
        int variant;
        int r5bad;
        XGuiClient_disconnectFromServer(client);
        /* 泵若干圈让服务端走完 BYE→会话摘链(挂起帧排干), 防下一会话
         * 撞上未清理完的旧会话状态。 */
        {
            int drain;
            for (drain = 0; drain < 50; ++drain) {
                XGuiApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(5);
            }
        }
        XClassDelete(client);
        client = NULL;
        for (variant = 0; variant < 2; ++variant)
        {
            XGuiClient* c2 = NULL;
            XGuiRemoteLoopbackDevice *a2 = NULL, *b2 = NULL;
            XGuiRemoteStats st;
            int64_t t0, firstTileMs = -1, fullMs = -1;
            int settle;
            if (variant == 1)
                setenv("XGUI_REMOTE_FB_REQUEST_OFF", "1", 1);
            else
                setenv("XGUI_REMOTE_FB_REQUEST_OFF", "0", 1);
            /* 制造一次整页伤害让新会话有的可收：先换页。 */
            mf_injectClick(parent, mf_btnPageX, mf_btnPageY);
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            a2 = XGuiRemoteLoopbackDevice_createPair(4u * 1024u * 1024u, &b2);
            c2 = a2 && b2 ? XGuiClient_create(NULL, 0) : NULL;
            if (!a2 || !b2 || !c2)
            {
                mf_expect(false, "R5 topo");
                if (a2) XClassDelete((XClass*)a2);
                if (b2) XClassDelete((XClass*)b2);
                if (c2) XClassDelete(c2);
                continue;
            }
            if (XGuiServer_attachTransport(server, (XIODevice*)a2) <= 0) {
                mf_expect(false, "R5 attachTransport");
                XGuiClient_disconnectFromServer(c2);
                XGuiApplication_processEvents(XEventLoop_AllEvents);
                XClassDelete(c2);
                XClassDelete((XClass*)a2);
                XClassDelete((XClass*)b2);
                continue;
            }
            XGuiClient_setTransport(c2, (XIODevice*)b2);
            /* [perf9 路2 修复 2026-10-05] 与主 client 同几何+可见态:
             * 原实现漏设——c2 以裸默认尺寸(640x480)起, FIT 信箱把
             * 320x220 帧放大 2x 居中, mf_compareMirror 抓 grab 顶部
             * 320x220 即放大角部图, 与面板 1:1 全帧比出 ~3 万假失配
             * (R5 [MF-FAIL] 假阳性的完整根因; 像素内容本身为正确新
             * 页, 非交付缺陷)。同尺寸后 FIT 无缩放, 比对回到真 1:1。 */
            XWidget_setGeometry((XWidget*)c2, MF_CLI_X, MF_CLI_Y,
                                MF_HOST_W, MF_HOST_H);
            XWidget_show((XWidget*)c2);
            t0 = mf_nowMs();
            for (settle = 0; settle < 2000; ++settle)
            {
                XGuiApplication_processEvents(XEventLoop_AllEvents);
                XThread_msleep(5);
                XGuiClient_statistics(c2, &st);
                if (firstTileMs < 0 && st.tileCount > 0)
                    firstTileMs = mf_nowMs() - t0;
                if (fullMs < 0 && st.tileCount >= 6)
                {
                    /* 320x240/128 = 3x2 网格 = 6 tile=全量一轮。 */
                    fullMs = mf_nowMs() - t0;
                }
                if (fullMs >= 0 && mf_nowMs() - t0 > fullMs + 2000)
                    break;
            }
            mf_pumpStable(c2, 300, 8000);
            /* R5=FB_REQUEST 首帧实效 A/B(任务5)。[perf9 路2 2026-10-05]
             * 两处升级: ① 原实现 (void) 丢弃比对却打 [MF-FAIL] 首失配行,
             * 汇总行 mismatch= 引用外层 R4 残值——日志自相矛盾; 现如实
             * 入账。② 原不硬门的理由(新会话首采集单帧滞后)已随两根修
             * 消失(状态序先行+整窗同步重绘), c2 几何修正后 on/off 两轮
             * 严格比对实证 0 失配——升硬门: 新会话首采集必须字节级等于
             * 面板真值。 */
            r5bad = mf_compareMirror(c2, mfBarH, NULL, 0, &leak, NULL,
                                     variant == 0 ? "R5 fbreq-on" :
                                                    "R5 fbreq-off");
            mf_expect(r5bad == 0,
                      variant == 0 ? "R5 fbreq-on mirror==panel" :
                                     "R5 fbreq-off mirror==panel");
            mf_expect(st.tileCount > 0,
                      variant == 0 ? "R5 fbreq-on tiles" :
                                     "R5 fbreq-off tiles");
            fprintf(stderr,
                    "[MF] R5 %s firstTile=%lldms fullRound=%lldms "
                    "tiles=%u mismatch=%d\n",
                    variant == 0 ? "FB_REQUEST on " : "FB_REQUEST off",
                    (long long)firstTileMs, (long long)fullMs,
                    st.tileCount, r5bad);
            XGuiClient_disconnectFromServer(c2);
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            XClassDelete(c2);
            XClassDelete((XClass*)a2);
            XClassDelete((XClass*)b2);
        }
        unsetenv("XGUI_REMOTE_FB_REQUEST_OFF");
    }

    /* ---- R6 FIT 信箱（任务4 扩 + round1 移交复现探针）：缩放视图
     *      (240x300 ⊃ 320x220, s=0.75, offY=67>条高)下断言：
     *      a) 信箱底色随占位门（深灰，env=0 时历史黑）；
     *      b) 全量交付后宿主顶带（内容首 ~13 视图行=round1 真机「FIT
     *         视图宿主顶端 ~12 host 行恒黑 2.92%」移交缺陷区, offY=67
     *         使其落在客户端 CSD 条之下可见）不得黑带。 ---- */
    {
        XGuiClient* c3 = NULL;
        XGuiRemoteLoopbackDevice *a3 = NULL, *b3 = NULL;
        XImage* snap = NULL;
        int phOn = !(getenv("XGUI_REMOTE_TILE_PLACEHOLDER") &&
                     getenv("XGUI_REMOTE_TILE_PLACEHOLDER")[0] == '0' &&
                     getenv("XGUI_REMOTE_TILE_PLACEHOLDER")[1] == '\0');
        int blackAll = 0, blackTop = 0, grayBar = 0, blackBar = 0;
        int x, y, settle;
        int barY0 = mfBarH + 1;      /* grab 顶部被自家 CSD 条覆盖. */
        int fitOffY = 67;            /* 240x300: dstH=165, (300-165)/2. */
        int fitDstH = 165;           /* (int)(220*0.75+0.5). */
        a3 = XGuiRemoteLoopbackDevice_createPair(4u * 1024u * 1024u, &b3);
        c3 = a3 && b3 ? XGuiClient_create(NULL, 0) : NULL;
        if (!a3 || !b3 || !c3 ||
            XGuiServer_attachTransport(server, (XIODevice*)a3) <= 0)
        {
            mf_expect(false, "R6 topo");
            if (c3) XClassDelete(c3);
            if (a3) XClassDelete((XClass*)a3);
            if (b3) XClassDelete((XClass*)b3);
            goto done;
        }
        XGuiClient_setTransport(c3, (XIODevice*)b3);
        /* 缩放几何: 240x300 → s=0.75, 内容 240x165 居中(上下各 67px 边,
         * 内容顶落在 CSD 条(0..mfBarH)之下=宿主顶带可直接观测). */
        XWidget_setGeometry((XWidget*)c3, 620, 20, 240, 300);
        XWidget_show((XWidget*)c3);
        for (settle = 0; settle < 400; ++settle) {
            XGuiRemoteStats st;
            XGuiApplication_processEvents(XEventLoop_AllEvents);
            XThread_msleep(5);
            XGuiClient_statistics(c3, &st);
            if (st.tileCount >= 6 && settle > 40) break;
        }
        mf_pumpStable(c3, 300, 8000);
        XWidget_repaint((XWidget*)c3);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        snap = XWidget_grab((XWidget*)c3);
        if (!snap)
        {
            mf_expect(false, "R6 snap");
            XClassDelete(c3);
            XClassDelete((XClass*)a3);
            XClassDelete((XClass*)b3);
            goto done;
        }
        for (y = 0; y < XImage_height(snap); ++y)
            for (x = 0; x < XImage_width(snap); ++x)
            {
                uint32_t p = mf_pixelAt(snap, x, y);
                unsigned r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF,
                         b = p & 0xFF;
                bool isBlack = (r < 0x10 && g < 0x10 && b < 0x10);
                bool isPhGray = (r >= 0x1C && r <= 0x2C && g >= 0x1E &&
                                 g <= 0x2C && b >= 0x1C && b <= 0x2C);
                if (y < barY0) continue; /* 自家 CSD 条不参与. */
                if (y < fitOffY || y >= fitOffY + fitDstH)
                {
                    /* 信箱条: 占位开=深灰, 关=历史黑。 */
                    if (isPhGray) ++grayBar;
                    if (isBlack) ++blackBar;
                    continue;
                }
                if (isBlack)
                {
                    ++blackAll;
                    if (y < fitOffY + 13) ++blackTop; /* 宿主顶 ~17 行. */
                }
            }
        fprintf(stderr,
                "[MF] R6 FIT bar gray=%d black=%d | content black=%d "
                "topBand=%d (ph=%d)\n",
                grayBar, blackBar, blackAll, blackTop, phOn);
        if (getenv("MF_DUMP"))
        {
            FILE* f = fopen("/tmp/mfc_R6_snap.ppm", "wb");
            if (f)
            {
                fprintf(f, "P6\n%d %d\n255\n", XImage_width(snap),
                        XImage_height(snap));
                for (y = 0; y < XImage_height(snap); ++y)
                    for (x = 0; x < XImage_width(snap); ++x)
                    {
                        uint32_t p = mf_pixelAt(snap, x, y);
                        fputc((p >> 16) & 0xFF, f);
                        fputc((p >> 8) & 0xFF, f);
                        fputc(p & 0xFF, f);
                    }
                fclose(f);
            }
        }
        /* a) 信箱底色: 占位开→深灰为主, 关→黑为主。 */
        mf_expect(phOn ? (grayBar > 5000 && grayBar > blackBar)
                       : (blackBar > 5000 && blackBar > grayBar),
                  phOn ? "R6 letterbox placeholder gray"
                       : "R6 letterbox legacy black");
        /* b) round1 移交: 顶部带全量交付后不得恒黑(阈值按基线定,
         *    文字 AA 黑像素正常几十~几百)。 */
        mf_expect(blackTop < 400, "R6 FIT top band no black strip");
        /* b2) 增量损伤版: 换页后(FIT tile 级损伤映射路径)顶带仍不得
         *     黑带——round1 真机缺陷的精确复现口径(增量, 非全量)。 */
        mf_injectClick(parent, mf_btnPageX, mf_btnPageY);
        mf_pumpStable(c3, 300, 8000);
        XClassDelete(snap);
        snap = NULL;
        XWidget_repaint((XWidget*)c3);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        snap = XWidget_grab((XWidget*)c3);
        if (!snap)
        {
            mf_expect(false, "R6 snap2");
            XClassDelete(c3);
            XClassDelete((XClass*)a3);
            XClassDelete((XClass*)b3);
            goto done;
        }
        blackAll = 0;
        blackTop = 0;
        grayBar = 0;
        blackBar = 0;
        for (y = 0; y < XImage_height(snap); ++y)
            for (x = 0; x < XImage_width(snap); ++x)
            {
                uint32_t p = mf_pixelAt(snap, x, y);
                unsigned r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF,
                         b = p & 0xFF;
                bool isBlack = (r < 0x10 && g < 0x10 && b < 0x10);
                bool isPhGray = (r >= 0x1C && r <= 0x2C && g >= 0x1E &&
                                 g <= 0x2C && b >= 0x1C && b <= 0x2C);
                if (y < barY0) continue;
                if (y < fitOffY || y >= fitOffY + fitDstH)
                {
                    if (isPhGray) ++grayBar;
                    if (isBlack) ++blackBar;
                    continue;
                }
                if (isBlack)
                {
                    ++blackAll;
                    if (y < fitOffY + 13) ++blackTop;
                }
            }
        fprintf(stderr,
                "[MF] R6 FIT incremental: content black=%d topBand=%d "
                "bar gray=%d black=%d\n",
                blackAll, blackTop, grayBar, blackBar);
        mf_expect(blackTop < 400, "R6 FIT top band clean after page switch");
        XGuiClient_disconnectFromServer(c3);
        XGuiApplication_processEvents(XEventLoop_AllEvents);
        XClassDelete(c3);
        XClassDelete((XClass*)a3);
        XClassDelete((XClass*)b3);
    }

done:
    if (client) XClassDelete(client);
    if (server) XClassDelete(server);
    if (devA) XClassDelete((XClass*)devA);
    if (devB) XClassDelete((XClass*)devB);
    if (mf_dialog) XClassDelete(mf_dialog);
    if (btnDlg) XClassDelete(btnDlg);
    if (btnPage) XClassDelete(btnPage);
    if (landmark) XClassDelete(landmark);
    if (mf_pageB) XClassDelete(mf_pageB);
    if (mf_pageA) XClassDelete(mf_pageA);
    if (parent) XClassDelete(parent);
    if (app) XClassDelete(app);
    XPlatformDisplayDriver_unregister(&mf_ops);
    if (mf_failures == 0)
        fprintf(stderr, "[MF-PASS] mirror fidelity loopback ok\n");
    return mf_failures ? 1 : 0;
}

#else /* !XWIDGET_ON || !XGUI_REMOTE_ON || !XGUI_ON || !XPLATFORM_FBDEV_ON */

int main(void)
{
    fprintf(stderr,
            "[MF-SKIP] requires XWIDGET_ON && XGUI_REMOTE_ON && "
            "XGUI_ON && XPLATFORM_FBDEV_ON\n");
    return 0;
}

#endif /* XWIDGET_ON && XGUI_REMOTE_ON && XGUI_ON && XPLATFORM_FBDEV_ON */
