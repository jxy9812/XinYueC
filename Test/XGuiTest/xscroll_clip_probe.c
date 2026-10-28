/**
 * @file       xscroll_clip_probe.c
 * @brief      滚动区子控件越界（不被父窗口裁剪）最小复现探针（临时诊断工具）。
 * @details    现象（安卓 + 桌面双端实证）：滚动页面时，行内的
 *             XComboBox / XLineEdit / XSpinBox / XPushButton 随内容移出
 *             父容器（viewport）边界后**仍被完整绘制**（越到上方品牌条
 *             / 标题栏上），正常应被父容器裁掉；同行 XLabel 位置正常。
 *
 *             本探针复刻该结构：顶栏（brand）+ 滚动区（viewport + 内容）
 *             + 多行「标签/微调框/输入框/下拉框/按钮」，滚动指定量后
 *             打印每个控件的几何（相对父 / 全局）与视口边界，区分两种
 *             可能：
 *               ① 几何越界——控件 mapToGlobal 已落在视口上边界之上；
 *               ② 绘制未裁剪——几何在视口内，但像素画到了视口外。
 *             用法：xscroll_clip_probe [scrollValue] [winW] [winH]
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <stdlib.h>

#include "XGuiApplication.h"
#include "XWidget.h"
#include "XScrollArea.h"
#include "XAbstractScrollArea.h"
#include "XScrollBar.h"
#include "XLabel.h"
#include "XSpinBox.h"
#include "XLineEdit.h"
#include "XComboBox.h"
#include "XPushButton.h"
#include "XTimer.h"
#include "XString.h"

#define PROBE_ROWS 40
#define PROBE_COLS 5

static XWidget* g_brand = NULL;
static XWidget* g_area = NULL;
static XWidget* g_content = NULL;
static XWidget* g_cells[PROBE_ROWS][PROBE_COLS];
static int g_scroll = 100;

/** @brief 打印控件几何：相对父矩形 / 客户区 / 相对父位置 / 全局位置。 */
static void dumpGeom(const char* tag, XWidget* w)
{
    XRect g;
    XRect c;
    XPoint p;
    XPoint gp;
    if (!w) { printf("%-14s <null>\n", tag); fflush(stdout); return; }
    g = XWidget_geometry(w);
    c = XWidget_contentsRect(w);
    p = XWidget_pos(w);
    gp = XWidget_mapToGlobal(w, &p);
    printf("%-14s geom=(%d,%d %dx%d) contents=(%d,%d %dx%d) "
           "pos=(%d,%d) global=(%d,%d)\n",
           tag, g.x, g.y, g.width, g.height,
           c.x, c.y, c.width, c.height,
           p.x, p.y, gp.x, gp.y);
    fflush(stdout);
}

static void onProbeTick(void* userData, XTimerData* timer)
{
    XWidget* viewport;
    XScrollBar* vsb;
    XPoint vp;
    int i;
    (void)userData; (void)timer;

    vsb = XAbstractScrollArea_verticalScrollBar((XAbstractScrollArea*)g_area);
    if (vsb) {
        printf("[probe] vbar value=%d range=%d..%d pageStep=%d\n",
               XScrollBar_value(vsb), XScrollBar_minimum(vsb),
               XScrollBar_maximum(vsb), XScrollBar_pageStep(vsb));
        XScrollBar_setValue(vsb, g_scroll);
        printf("[probe] after set value=%d\n", XScrollBar_value(vsb));
    }
    fflush(stdout);

    viewport = XAbstractScrollArea_viewport((XAbstractScrollArea*)g_area);
    printf("[probe] ===== geometry after scroll =====\n");
    dumpGeom("brand", g_brand);
    dumpGeom("area", g_area);
    dumpGeom("viewport", viewport);
    dumpGeom("content", g_content);
    if (viewport) {
        vp = XWidget_pos(viewport);
        printf("[probe] viewport global top-left = (%d,%d)\n", vp.x, vp.y);
    }
    for (i = 0; i < PROBE_ROWS; ++i) {
        char tag[64];
        static const char* names[PROBE_COLS] = { "label", "spin", "edit", "combo", "btn" };
        int k;
        for (k = 0; k < PROBE_COLS; ++k) {
            snprintf(tag, sizeof(tag), "r%d.%s", i, names[k]);
            dumpGeom(tag, g_cells[i][k]);
        }
        if (i >= 2) break; /* 只打前三行 + 最后一行 */
    }
    dumpGeom("last.label", g_cells[PROBE_ROWS - 1][0]);
    dumpGeom("last.spin", g_cells[PROBE_ROWS - 1][1]);
    printf("[probe] PROBE-DONE\n");
    fflush(stdout);
    /* 保持运行直到 /tmp/scroll_probe_go 出现，便于外部抓帧目验。 */
    {
        FILE* f = fopen("/tmp/scroll_probe_go", "rb");
        if (f) { fclose(f); XGuiApplication_quit(); }
    }
}

int main(int argc, char** argv)
{
    XWidget* mainWin;
    XTimer* tick;
    int i;
    int k;
    int w = 1000;
    int h = 700;

    if (argc > 1) g_scroll = atoi(argv[1]);
    if (argc > 2) w = atoi(argv[2]);
    if (argc > 3) h = atoi(argv[3]);

    if (!XGuiApplication_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, 0, NULL)) {
        printf("probe: app create failed\n");
        return 1;
    }
    mainWin = (XWidget*)XMemory_malloc(sizeof(XWidget), XCLASS_DEFAULT_MEMORY_TYPE);
    if (!mainWin) return 1;
    XWidget_init(mainWin, NULL, (XWidgetFlags)XWindowType_Window);
    XWidget_resize(mainWin, w, h);
    XWidget_setWindowTitle(mainWin, XString_create_utf8("scroll clip probe"));
    XWidget_show(mainWin);

    /* 顶栏（模拟 demo 品牌条）：高 40，滚动区从 y=40 起。 */
    g_brand = XWidget_create(mainWin, 0);
    XWidget_setGeometry(g_brand, 0, 0, w, 40);
    XWidget_setAutoFillBackground(g_brand, true);
    XWidget_show(g_brand);

    g_area = (XWidget*)XScrollArea_create(mainWin, 0);
    XWidget_setGeometry(g_area, 0, 40, w, h - 40);
    XWidget_show(g_area);

    g_content = XWidget_create((XWidget*)g_area, 0);
    XWidget_resize(g_content, w, PROBE_ROWS * 30 + 20);
    XScrollArea_setWidget((XScrollArea*)g_area, g_content);

    for (i = 0; i < PROBE_ROWS; ++i) {
        int y = 10 + i * 30;
        XLabel* lbl = XLabel_create(g_content, 0);
        XSpinBox* spin = XSpinBox_create(g_content, 0);
        XLineEdit* edit = XLineEdit_create(g_content, 0);
        XComboBox* combo = XComboBox_create(g_content, 0);
        XPushButton* btn = XPushButton_create(g_content, 0);
        if (lbl) {
            XLabel_setText_2(lbl, "\xE6\xA0\x87\xE7\xAD\xBE"); /* 标签 */
            XWidget_setFixedWidth((XWidget*)lbl, 56);
            XWidget_setFixedHeight((XWidget*)lbl, 22);
            XWidget_setGeometry((XWidget*)lbl, 10, y, 56, 22);
            XWidget_show((XWidget*)lbl);
        }
        if (spin) {
            XSpinBox_setValue(spin, i + 1);
            XWidget_setFixedWidth((XWidget*)spin, 60);
            XWidget_setFixedHeight((XWidget*)spin, 24);
            XWidget_setGeometry((XWidget*)spin, 80, y - 1, 60, 24);
            XWidget_show((XWidget*)spin);
        }
        if (edit) {
            XLineEdit_setText(edit, "15020");
            XWidget_setFixedWidth((XWidget*)edit, 100);
            XWidget_setFixedHeight((XWidget*)edit, 24);
            XWidget_setGeometry((XWidget*)edit, 160, y - 1, 100, 24);
            XWidget_show((XWidget*)edit);
        }
        if (combo) {
            XComboBox_addItem_2(combo, "TCP");
            XComboBox_addItem_2(combo, "RTU");
            XComboBox_setCurrentIndex(combo, 0);
            XWidget_setFixedWidth((XWidget*)combo, 120);
            XWidget_setFixedHeight((XWidget*)combo, 24);
            XWidget_setGeometry((XWidget*)combo, 280, y - 1, 120, 24);
            XWidget_show((XWidget*)combo);
        }
        if (btn) {
            XPushButton_setText_2(btn, "\xE6\x8A\x95\xE7\x82\xB9\xE6\x95\xB0\xE9\x87\x8D\xE5\xBB\xBA");
            XWidget_setFixedWidth((XWidget*)btn, 90);
            XWidget_setFixedHeight((XWidget*)btn, 28);
            XWidget_setGeometry((XWidget*)btn, 420, y - 2, 90, 28);
            XWidget_show((XWidget*)btn);
        }
        g_cells[i][0] = (XWidget*)lbl;
        g_cells[i][1] = (XWidget*)spin;
        g_cells[i][2] = (XWidget*)edit;
        g_cells[i][3] = (XWidget*)combo;
        g_cells[i][4] = (XWidget*)btn;
    }
    (void)k;

    tick = XTimer_create_ex(XCLASS_DEFAULT_MEMORY_TYPE);
    XTimer_setInterval(tick, 200);
    XTimer_setTimerCallback(tick, onProbeTick);
    XTimer_start_base(tick);

    XGuiApplication_exec();
    return 0;
}
