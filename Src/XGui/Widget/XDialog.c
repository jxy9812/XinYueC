/**
 * @file       XDialog.c
 * @brief      对话框控件实现（对标 Qt 6.8 QDialog 核心公共 API）。
 * @details    与同名头文件的公共 API 一一对应；内部实现细节见
 *             头文件 @note 与函数级 Doxygen 注释。
 * @author     XinYueC 团队
 */

#include "XDialog.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XGuiApplication.h"
#include "XEventLoop.h"
#include "XGuiConfig.h"

#include "XCoreApplication.h"
#include "XPrintf.h"
#include "XThreadData.h"        /* xdlg_execShouldFinish：quitNow 标记感知 */
#include "XWidget_Protected.h"
#include "XWindowEvent.h"
#include "XImage.h"
#include "XPalette.h"
#include "XContainer.h"
#include "XVector.h"
#include "XPushButton.h"
#include "XTextEdit.h"
#include "XPlainTextEdit.h"
#include "XTextControl.h"
#include "XPainter.h"
#include "XIcon.h"
#include "XAlignment.h"
#if XMESSAGEBOX_ON
#include "XMessageBox.h" /* xdlg_dialogCode 的消息盒角色映射（向下识别）。 */
#endif
/* CSD 内容避让查询前提与 XWindowDecoration.h 的声明门槛一致（XWIDGET_ON
 * 已由 XDIALOG_ON 的配置门保证）。查询关断时避让恒零偏移，布局零变化。 */
#if XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON
#include "XWindowDecoration.h" /* 装饰保留边距查询（让位先例=XMainWindow 布局）。 */
#include "XTitleBar.h"         /* 默认标题条识别：位移直接子控件时跳过条带本身。 */
#define XDLG_CSD_QUERY_ON 1
#else
#define XDLG_CSD_QUERY_ON 0
#endif
#if XLAYOUT_ON
#include "XLayout.h" /* 布局挂载对话框：增量改写根布局顶边距实现避让。 */
#endif

#if XWIDGET_ON && XDIALOG_ON

static void xdlg_emitVoid(XDialog* self, size_t signal)
{
    XVarList* args = XVarList_create(0);
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self, signal, args, NULL, NULL,
                           XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

static void xdlg_emitFinished(XDialog* self, int result)
{
    XVarList* args = XVarList_Create(XVar(int, result));
    if (!args) return;
    if (self && ((XObject*)self)->m_signalSlot) {
        XObject_emitSignal((XObject*)self,
            (size_t)XDialog_finished_signal, args, NULL, NULL,
            XEVENT_PRIORITY_NORMAL);
    } else {
        XVarList_delete(args);
    }
}

/* ==================== 标题栏与边框（对标 Win10 对话框窗口观感） ==================== */

/** @defgroup xdlg_tb 对话框标题栏组内部口径
 *  行高 28 与派生面板「标题让位 contentTop=28」契约严格相等
 *  （XMessageBox.c xmsg_contentTop / XColorDialog.c 同口径：有标题时
 *  内容自 y=28 起），标题带恰好占满让位条、不侵占内容区；无标题的
 *  子控件对话框不画带也不画 [×]（与 contentTop 回落 12 的原布局互
 *  锁，布局不被遮挡）。视觉参数对标 Win10：标题栏底 #F9F9F9、左
 *  图标位 16px、标题左对齐黑字、右端 [×] 24x24（悬停 #E81123 白字
 *  形、按住加深 #C50E1F）、面板 1px 活动边框 #B0B0B0。 */
#define XDLG_TB_HEIGHT       28    /**< 标题栏行高 = contentTop 让位口径 */
#define XDLG_TB_ICON_EXTENT  16    /**< 左侧窗口图标位边长（可选） */
#define XDLG_TB_CLOSE_EXTENT 24    /**< [×] 关闭钮边长（Win10 caption 钮） */
#define XDLG_TB_BAND_COLOR   0xFFF9F9F9u /**< 标题栏底色（Win10 极浅灰白） */
#define XDLG_TB_RULE_COLOR   0xFFE5E5E5u /**< 标题栏底部分隔发丝线 */
#define XDLG_FRAME_COLOR     0xFFB0B0B0u /**< 面板 1px 边框（Win10 活动边框） */
#define XDLG_TB_GLYPH_COLOR  0xFF1F1F1Fu /**< [×] 常态字形（近黑） */
#define XDLG_CLOSE_HOVER     0xFFE81123u /**< [×] 悬停底色（Win10 关闭红） */
#define XDLG_CLOSE_PRESSED   0xFFC50E1Fu /**< [×] 按住底色（加深一档） */
#define XDLG_CLOSE_GLYPH_HOT 0xFFFFFFFFu /**< 悬停/按住时的白色字形 */

/* [×] 悬停/按压跟踪（单指针语义：同一时刻至多一个光标落点）。对话
 * 框契约头不可扩字段，状态落在文件域；owner 切换时同步刷新前后两
 * 个对话框的脏区，悬停残影最迟在指针下次移动时消除（与框架
 * WA_UnderMouse 的清理时机同档）。 */
static const XDialog* xdlg_tb_owner = NULL;
static bool xdlg_tb_hover = false;
static bool xdlg_tb_pressed = false;

/* 「画过标题带」登记：仅本对话框 paintEvent 实际绘制过标题栏后才参
 * 与 [×] 命中——XWizard/XErrorMessage 等覆写 paintEvent 且不回链
 * XDialog 面板绘制的派生类虽然继承本类鼠标重载，但右上角不会出现
 * 隐形关闭热区。槽位只在 done() 摘除，比较不 leading 解引用。 */
#define XDLG_TB_TRACK_MAX 4
static const XDialog* xdlg_tb_painted[XDLG_TB_TRACK_MAX];

static bool xdlg_tb_paintedContains(const XDialog* self)
{
    int i;
    for (i = 0; i < XDLG_TB_TRACK_MAX; ++i)
        if (xdlg_tb_painted[i] == self) return true;
    return false;
}

static void xdlg_tb_markPainted(const XDialog* self)
{
    int i;
    if (xdlg_tb_paintedContains(self)) return;
    for (i = 0; i < XDLG_TB_TRACK_MAX; ++i) {
        if (!xdlg_tb_painted[i]) {
            xdlg_tb_painted[i] = self;
            return;
        }
    }
    /* 槽满：整体左移腾出末位（需 >4 个带标题对话框同屏才触发）。 */
    for (i = 1; i < XDLG_TB_TRACK_MAX; ++i)
        xdlg_tb_painted[i - 1] = xdlg_tb_painted[i];
    xdlg_tb_painted[XDLG_TB_TRACK_MAX - 1] = self;
}

static void xdlg_tb_unmark(const XDialog* self)
{
    int i;
    for (i = 0; i < XDLG_TB_TRACK_MAX; ++i)
        if (xdlg_tb_painted[i] == self) xdlg_tb_painted[i] = NULL;
}

/** @brief  标题栏绘制/交互同键判定：子控件形态 + 已设非空窗口标题。
 *  @details 与派生面板 contentTop=28 让位口径同键（无标题保持原布局
 *           不占位）；顶层窗口形态有平台标题栏承载，不画带（与既有
 *           标题文本绘制的 m_isWindow 排除口径一致）。 */
static bool xdlg_titlebarGate(const XDialog* self)
{
    const XString* title;
    const char* utf8;
    if (!self || ((const XWidget*)self)->m_isWindow) return false;
    title = XWidget_windowTitle((const XWidget*)self);
    utf8 = title ? XString_toUtf8(title) : NULL;
    return utf8 && utf8[0];
}

/** @brief  窗口标题 UTF-8 文本（借用指针；无标题返回 NULL）。 */
static const char* xdlg_titleUtf8(const XDialog* self)
{
    const XString* title;
    if (!self) return NULL;
    title = XWidget_windowTitle((const XWidget*)self);
    return title ? XString_toUtf8(title) : NULL;
}

/** @brief  [×] 关闭钮命中（对话框局部坐标；与绘制几何严格同源：
 *          右缘留 1px 边框列，上缘 2px 呼吸位，24x24）。 */
static bool xdlg_closeButtonHit(const XDialog* self, int x, int y)
{
    int bx;
    if (!self) return false;
    if (XWidget_width((const XWidget*)self) < XDLG_TB_CLOSE_EXTENT + 8)
        return false; /* 过窄面板不画钮（与绘制同键），无从命中。 */
    bx = XWidget_width((const XWidget*)self) - 1 - XDLG_TB_CLOSE_EXTENT;
    return x >= bx && x < bx + XDLG_TB_CLOSE_EXTENT &&
           y >= 2 && y < 2 + XDLG_TB_CLOSE_EXTENT;
}

/** @brief      未显式定位的对话框居中到父窗口中央。
 *  @details    对标 QDialogPrivate::adjustPosition（qdialog.cpp:871，
 *              QDialog 首次显示按父窗口居中）：Qt 以父窗口中心
 *              p = mapToGlobal(0,0) + parent->size()/2 为基准，再
 *              p -= size()/2 得全局落点。子控件形态（历史形态，现经
 *              init 叠加 Dialog 类型后对话框恒为顶层窗口，此分支保
 *              留兼容 overrideWindowFlags 改型的对象）几何为父系坐
 *              标，把全局落点换算回父系坐标必须减去父控件在顶层窗
 *              口内的偏移——此前公式误用加法（pw + (tw-dw)/2），页
 *              偏移被双倍计入，对话框整体被推向右下（实测文件框底
 *              缘溢出 56px——夜间台账 #27/#28）。窗口形态对话框按
 *              父级顶层窗口屏幕几何居中。open/exec/showEvent 每次
 *              显示均调用；显式定位过（Moved）的对话框尊重调用方
 *              位置不再搬动，自动居中不登记 Moved（见函数体）。 */
static void xdlg_centerToParentWindow(XDialog* self)
{
    XWidget* selfw = (XWidget*)self;
    XWidget* parent;
    XWidget* top;
    XWidget* w;
    int pw = 0;
    int py = 0;
    int dw;
    int dh;
    int tw;
    int th;
    if (!selfw) return;
    /* 对标 QDialog::showEvent 的 !WA_Moved 门（qdialog.cpp:861）：显式
     * setGeometry/move 过的对话框尊重调用方位置，open/exec/show 的
     * 居中一律不生效（验收：用户移动过或显式 move 后不再重定位）。 */
    if (XWidget_testAttribute(selfw, XWidgetAttribute_Moved)) return;
    /* 居中收尾统一复位 Moved（对标 qdialog.cpp:864 的
     * setAttribute(WA_Moved, false)）：自动居中不是显式定位，后续
     * show（如 updateSize 定尺寸后的 showEvent 重居中）仍按新几何
     * 重新居中；只有用户显式 move/setGeometry 才永久固定位置。 */
    if (selfw->m_isWindow) {
        /* 窗口形态对话框（flags 带 Window/Popup，拥有独立原生窗）：
         * 居中于父级顶层窗口的屏幕几何（Qt QDialog::adjustPosition
         * 对标——以父窗口为参照居中，非屏幕居中）。 */
        XPoint po;
        XPoint origin;
        XWidget* ptop;
        parent = XWidget_parentWidget(selfw);
        ptop = parent ? XWidget_topLevelWidget(parent) : NULL;
        if (!ptop || ptop == selfw) return;
        XPoint_init(&origin, 0, 0);
        po = XWidget_mapToGlobal(ptop, &origin);
        dw = XWidget_width(selfw);
        dh = XWidget_height(selfw);
        tw = XWidget_width(ptop);
        th = XWidget_height(ptop);
        {
            int x = po.x + (tw > dw ? (tw - dw) / 2 : 0);
            int y = po.y + (th > dh ? (th - dh) / 2 : 0);
            if (x < 0) x = 0;
            if (y < 0) y = 0;
            XWidget_move(selfw, x, y);
        }
        XWidget_setAttribute(selfw, XWidgetAttribute_Moved, false);
        return;
    }
    parent = XWidget_parentWidget(selfw);
    if (!parent) return;
    top = XWidget_topLevelWidget(selfw);
    if (!top || top == selfw) return;
    dw = XWidget_width(selfw);
    dh = XWidget_height(selfw);
    tw = XWidget_width(top);
    th = XWidget_height(top);
    w = parent;
    while (w && w != top) {
        pw += XWidget_x(w);
        py += XWidget_y(w);
        w = XWidget_parentWidget(w);
    }
    /* 对标 qdialog.cpp adjustPosition 落点换算：目标=顶层窗口中心
     * 邻域（全局坐标 (tw-dw)/2, (th-dh)/2），换算回父系坐标减去父
     * 控件偏移 (pw,py)。Qt 的 WM 框架余量 extraw/extrah（10/40）仅
     * 对有原生装饰的窗口有意义，XGui 子控件形态无装饰，取 0。 */
    XWidget_move(selfw,
                 (tw > dw ? (tw - dw) / 2 : 0) - pw,
                 (th > dh ? (th - dh) / 2 : 0) - py);
    XWidget_setAttribute(selfw, XWidgetAttribute_Moved, false);
}

/* ==================== CSD 内容避让（框架自绘标题条让位） ==================== */

/** @brief  对话框内容的 CSD 避让顶偏移（像素；未装饰恒 0）。
 *  @details 框架自绘装饰（XWindowDecoration）把标题条画在客户区顶部
 *           （高约一个条带），对话框子控件仍从 y≈0 布局时首行内容被
 *           条带遮住（用户实测：独立顶层消息盒图标半截；此前系统标
 *           题栏模式正常——条带在客户区外无需避让）。主窗口已按
 *           XWindowDecoration_marginsFor 整体下移布局（XMainWindow
 *           同源先例），对话框经本查询取同值。仅对话框自身为顶层窗
 *           口且被框架装饰时非零：系统标题栏模式、CSD 抑制位
 *           （XWindow_setCsdFrameSuppressed 置位等价——未装饰时保留
 *           边距恒零）与子控件形态（历史 overrideWindowFlags 对象，
 *           装饰判定对非顶层恒假）一律 0。 */
static int xdlg_csdTopOffset(const XDialog* self)
{
    const XWidget* selfw = (const XWidget*)self;
    if (!selfw || !selfw->m_isWindow) return 0;
#if XDLG_CSD_QUERY_ON
    /* marginsFor 未装饰返回全零边距，已覆盖系统条/抑制两态；条控件
     * 未承载（建窗前预测期）回退 XTitleBar_defaultHeight，与主窗口
     * 让位口径一致。 */
    return XWindowDecoration_marginsFor(selfw).top;
#else
    return 0;
#endif
}

int XDialog_decorationTopOffset(const XDialog* self)
{
    return xdlg_csdTopOffset(self);
}

/** @brief  套用 CSD 内容避让：子控件布局按装饰条高整体下移（增量幂等）。
 *  @details 两条路径按对话框形态二选一，均以 m_csdAppliedTop 记账、
 *           只补「目标-已套用」差值，重复调用（show/exec/open 多入口）
 *           与差值回退（CSD 动态关闭后再显示，delta 为负）皆安全：
 *           - 布局挂载（XColorDialog/XFileDialog/XInputDialog 等经
 *             XBoxLayout_create(dlg) 自动挂到对话框的根布局）：根布局
 *             顶边距 += delta。此后每次布局激活（显示尾/RESIZE 尾/
 *             updateGeometry）都自带偏移，天然覆盖后续 CSD 拖边改尺
 *             寸；派生面板文件不在本次修复所有权内亦无需改动。
 *           - 显式几何（无布局）：直接子控件整体 move 下移 delta——
 *             覆盖进度对话框、XWizard/XErrorMessage 与自定义对话框等
 *             自排布形态。装饰标题条本身（默认 XTitleBar 或
 *             XWidget_titleBarWidget 自定义条）与顶层子窗口跳过；消
 *             息盒除外——其 xmsg_contentTop 已含偏移自排布（showEvent
 *             的 updateSize 与 RESIZE 重排同源），再整体位移即双重让
 *             位（xdlg_dialogCode 同款 vtable 向下识别先例）。
 *           调用点=SHOW 事件/exec/open（见各调用点时序注），模态与
 *           收起（最小化）行为不经过本函数，零影响。 */
static void xdlg_applyContentAvoidance(XDialog* self)
{
    XWidget* selfw = (XWidget*)self;
    int offset;
    int delta;
    bool selfManaged = false;
    if (!self || !selfw) return;
    offset = xdlg_csdTopOffset(self);
    delta = offset - self->m_csdAppliedTop;
    if (delta == 0) return;
#if XMESSAGEBOX_ON
    selfManaged = XClassGetVtable((const XObject*)self) ==
                  XMessageBox_class_init();
    /* 消息盒全自管（xmsg_contentTop 已含偏移），布局/位移两条路径都
     * 不介入，只记账保持差值口径一致。 */
    if (selfManaged) {
        self->m_csdAppliedTop = offset;
        return;
    }
#endif
#if XLAYOUT_ON
    {
        XLayout* layout = XWidget_layout(selfw);
        if (layout) {
            XMargins m;
            XMargins_init(&m, 0, 0, 0, 0);
            m = XLayout_contentsMargins(layout);
            XLayout_setContentsMargins(layout, m.left, m.top + delta,
                                       m.right, m.bottom);
            self->m_csdAppliedTop = offset;
            return;
        }
    }
#endif
    if (!selfManaged) {
        XObject* object = (XObject*)selfw;
        if (object->m_children) {
            int count;
            int i;
            XObject* const* children =
                (XObject* const*)XContainerDataAddr(object->m_children);
            XWidget* customBar = NULL;
#if XDLG_CSD_QUERY_ON && XGUI_CUSTOM_TITLEBAR_ON
            customBar = XWidget_titleBarWidget(selfw);
#endif
            count = XVector_size_base((const XContainer*)object->m_children);
            for (i = 0; i < count; ++i) {
                XWidget* w;
                if (!children[i] || !children[i]->is_widget) continue;
                w = (XWidget*)children[i];
                if (w->m_isWindow) continue; /* 顶层子窗口不随父几何摆位。 */
                if (w == customBar) continue; /* 控件级自定义标题条。 */
#if XDLG_CSD_QUERY_ON
                /* 装饰默认标题条（父=本对话框的 XTitleBar 子控件）。 */
                if (XClassGetVtable((const XObject*)w) ==
                    XTitleBar_class_init())
                    continue;
#endif
                XWidget_move(w, XWidget_x(w), XWidget_y(w) + delta);
            }
        }
    }
    self->m_csdAppliedTop = offset;
}

/** @brief 对话框面板绘制：底色 + 1px 浅灰边框 + Win10 观感标题栏。
 *  @details 框架层根因说明（XWidget.c 不在本次修复所有权内，故在对话框
 *           层自绘面板）：XWidget_paintEvent_default（autofill 路径）把
 *           事件脏区先加 paintOffset 折算到顶层后备存储坐标，再用控件
 *           自身"局部尺寸"去裁剪——任何不在窗口原点的子控件，其填充矩形
 *           都会被错误裁剪（demo 对话框位于 (232,238)、尺寸 320x140，
 *           裁出负高直接整块跳过填充），面板因此永不上屏，文字/按钮
 *           悬浮在未渲染底色上，表现为"对话框弹不出"。本重载按正确
 *           顺序绘制：先在控件局部坐标用局部尺寸裁剪，再平移折算。
 *
 *           用户实证：对话框浮出后"无标题栏无边框，只有内容浮在主窗
 *           上"。本轮升级：①面板 1px 边框由深灰 #7A7A7A 提为对标
 *           Win10 活动边框的浅灰 #B0B0B0，并改走 XPainter 描边（与
 *           标题栏同一 painter 会话，端点内含，像素级等价原 setPixel
 *           四边）；②子控件形态 + 有标题的面板顶部绘制完整标题栏行
 *           （底色 + 左图标位 + 左对齐标题 + 右端 [×] 关闭钮），[×]
 *           点击走 XDialog_reject（Esc 同语义），悬停浅红白字形；带
 *           高 28 与派生面板 contentTop 让位口径严格相等，消息框/文
 *           件/颜色/输入便捷路径（均设 caption）自动继承。绘制顺序：
 *           标题带 → 分隔线 → 图标/字形 → 标题 → 边框最后压轴，保证
 *           边框四周完整不被标题带覆盖。 */
static void VXDialog_paintEvent(XWidget* self, XEvent* event)
{
    XPaintEvent* pe;
    XImage* image;
    XRect rect;
    XPalette palette;
    XColor color;
    int w;
    int h;
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_PAINT) return;
    pe = (XPaintEvent*)event;
    image = XWidget_paintImage(self);
    if (!image) return;
    palette = XWidget_palette(self);
    color = XPalette_color(&palette, XPaletteColorGroup_Active,
                           XPaletteColorRole_Window);
    rect = XPaintEvent_rect(pe);
    w = XWidget_width(self);
    h = XWidget_height(self);
    /* 1) 控件局部坐标：本次 PAINT 区域 ∩ 控件矩形（空脏区直接返回；
     *    paintTree 只对与脏区相交的控件派发 PAINT，此处防御性再裁一
     *    次，对标 qwidget.cpp paintEvent 内 dirty 区域语义）。 */
    if (rect.x < 0) { rect.width += rect.x; rect.x = 0; }
    if (rect.y < 0) { rect.height += rect.y; rect.y = 0; }
    if (rect.x + rect.width > w) rect.width = w - rect.x;
    if (rect.y + rect.height > h) rect.height = h - rect.y;
    if (rect.width <= 0 || rect.height <= 0) return;
    /* 2) 底色与前景同一 painter 会话。底色只铺「PAINT 区域 ∩ 控件矩
     *    形」并经 XPainter_fillRect 受表面裁剪约束（flush 按刷区域外
     *    接矩形设置的 wholeBbox，XPainter_begin_image 会将其继承为初
     *    始 painter 裁剪）——填充 ⊆ 提交区域恒成立，脏区外的旧前景
     *    （图标/文本/按钮）在跨帧持久的后备缓冲中原样保留。此前「整
     *    框裸 XImage_fillRect」绕过表面裁剪：局部悬停帧先把整框铺成
     *    背景白、前景又被表面裁剪钳在脏区内画不回来，缓冲里脏区外
     *    前景就此丢失，后续提交别处脏区时成片白带/底部花屏条。布局
     *    位移（字体懒加载重排/居中移动）的旧位残影由
     *    XWidget_recomputeGeometry 的「新旧矩形一并失效」（父层
     *    updateRect(old∪new)）承担，不再依赖整框重铺。 */
    {
        XPainter painter;
        XPoint o = XWidget_paintOffset(self);
        /* 面板高度不足一带（h<28）的退化对话框不画标题栏：避免带/
         * 分隔线/关闭钮矩形外溢邻区；未画带即不登记 [×] 命中。 */
        bool titlebar = xdlg_titlebarGate((const XDialog*)self) &&
                        h >= XDLG_TB_HEIGHT;
        XPainter_init(&painter, NULL);
        if (!XPainter_begin_image(&painter, image)) {
            XPainter_deinit(&painter);
            return;
        }
        /* 底色：PAINT 区域 ∩ 控件矩形（局部 rect 按 paintOffset 折算
         * 到后备存储坐标），坐标口径与下方前景一致。 */
        {
            XRect fill;
            fill.x = rect.x + o.x;
            fill.y = rect.y + o.y;
            fill.width = rect.width;
            fill.height = rect.height;
            XPainter_fillRect(&painter, &fill, XColor_rgba(&color));
        }
        /* 3) 标题栏 + 面板描边（Win10 对话框窗口观感）：坐标沿用本函
         *    数既有的"paintOffset 手工折算"口径。标题带只在「子控
         *    件形态 + 已设窗口标题」时绘制（xdlg_titlebarGate 口径）：
         *    与派生面板 contentTop=28 让位同键，无标题保持原布局不占
         *    位。 */
        if (titlebar) {
            XRect band;
            XRect rule;
            band.x = o.x;
            band.y = o.y;
            band.width = w;
            band.height = XDLG_TB_HEIGHT;
            rule.x = o.x;
            rule.y = o.y + XDLG_TB_HEIGHT - 1;
            rule.width = w;
            rule.height = 1;
            /* 标题栏底色（Win10 极浅灰白）+ 底部 1px 分隔发丝线。 */
            XPainter_fillRect(&painter, &band, XDLG_TB_BAND_COLOR);
            XPainter_fillRect(&painter, &rule, XDLG_TB_RULE_COLOR);
            xdlg_tb_markPainted((const XDialog*)self);
            {
                /* 左侧窗口图标位（可选 16px，Win10 小图标）：无图标
                 * 时标题左移补位。XWidget_windowIcon 返回共享副本，
                 * 用毕 XIcon_deinit_base（契约同 XWidget_font）。 */
                XIcon icon = XWidget_windowIcon(self);
                int textX = o.x + 8;
                if (!XIcon_isNull(&icon)) {
                    XIcon_paint(&icon, &painter, o.x + 8, o.y + 6,
                                XDLG_TB_ICON_EXTENT, XDLG_TB_ICON_EXTENT,
                                (uint32_t)(XAlignment_Left | XAlignment_Top),
                                XIconMode_Normal, XIconState_Off);
                    textX = o.x + 8 + XDLG_TB_ICON_EXTENT + 8;
                }
                XIcon_deinit_base(&icon);
                if (w >= XDLG_TB_CLOSE_EXTENT + 8) {
                    /* 右端 [×] 关闭钮 24x24：右缘让出 1px 边框列、上
                     * 缘 2px 呼吸位（与 xdlg_closeButtonHit 同一几何）。
                     * 悬停浅红 #E81123 白字形、按住加深 #C50E1F，常态
                     * 透明底近黑 10x10 字形（Win10 caption 关闭钮）。
                     * 面板过窄（w<32）不画钮，避免命中/绘制几何出界。 */
                    XRect btn;
                    bool hover;
                    bool pressed;
                    int gx;
                    int gy;
                    btn.x = o.x + w - 1 - XDLG_TB_CLOSE_EXTENT;
                    btn.y = o.y + 2;
                    btn.width = XDLG_TB_CLOSE_EXTENT;
                    btn.height = XDLG_TB_CLOSE_EXTENT;
                    hover = (xdlg_tb_owner == (const XDialog*)self) &&
                            xdlg_tb_hover;
                    pressed = hover && xdlg_tb_pressed;
                    if (hover)
                        XPainter_fillRect(&painter, &btn,
                                          pressed ? XDLG_CLOSE_PRESSED
                                                  : XDLG_CLOSE_HOVER);
                    XPainter_setPen(&painter,
                                    hover ? XDLG_CLOSE_GLYPH_HOT
                                          : XDLG_TB_GLYPH_COLOR);
                    gx = btn.x + (XDLG_TB_CLOSE_EXTENT - 10) / 2;
                    gy = btn.y + (XDLG_TB_CLOSE_EXTENT - 10) / 2;
                    XPainter_drawLine(&painter, gx, gy, gx + 9, gy + 9);
                    XPainter_drawLine(&painter, gx + 9, gy, gx, gy + 9);
                }
                {
                    /* 标题左对齐黑字（palette WindowText，对标 Win10
                     * 标题字色），右界让到 [×] 前 4px，垂直居中单行。
                     * 进入本分支前 xdlg_titlebarGate 已保证标题非空。 */
                    XFont font = XWidget_font(self);
                    XRect tr;
                    XColor textColor = XPalette_color(
                        &palette, XPaletteColorGroup_Active,
                        XPaletteColorRole_WindowText);
                    tr.x = textX;
                    tr.y = o.y;
                    tr.width = (o.x + w - 1 - XDLG_TB_CLOSE_EXTENT - 4) - textX;
                    tr.height = XDLG_TB_HEIGHT;
                    XPainter_setFont(&painter, &font);
                    if (tr.width > 0) {
#if XPAINTER_TEXTLAYOUT_ON
                        XPainter_drawTextRect(&painter, &tr,
                                              XPAINTER_TEXT_ALIGN_LEFT |
                                              XPAINTER_TEXT_ALIGN_VCENTER |
                                              XPAINTER_TEXT_SINGLE_LINE,
                                              xdlg_titleUtf8((const XDialog*)self),
                                              XColor_rgba(&textColor));
#else
                        XPainter_drawText(&painter, tr.x, o.y + 17,
                                          xdlg_titleUtf8((const XDialog*)self),
                                          XColor_rgba(&textColor));
#endif
                    }
                    XFont_deinit_base(&font);
                }
            }
        }
        /* 4) 面板 1px 边框 #B0B0B0 最后描：压住标题带/分隔线与边框相
         *    接的行/列，保证 Win10 活动边框四周完整（端点内含，严格
         *    落在控件矩形内，不外溢邻区）。 */
        XPainter_setPen(&painter, XDLG_FRAME_COLOR);
        XPainter_drawLine(&painter, o.x, o.y, o.x + w - 1, o.y);
        XPainter_drawLine(&painter, o.x, o.y + h - 1, o.x + w - 1, o.y + h - 1);
        XPainter_drawLine(&painter, o.x, o.y, o.x, o.y + h - 1);
        XPainter_drawLine(&painter, o.x + w - 1, o.y, o.x + w - 1, o.y + h - 1);
        XPainter_end(&painter);
        XPainter_deinit(&painter);
    }
}

/** @brief      多行文本编辑判定（对话框 Enter 让键豁免）。
 *  @details    对标 Qt 6.8 qdialog.cpp keyPressEvent：焦点在
 *              QTextEdit/QPlainTextEdit 类多行编辑器时 Enter 交给
 *              编辑器换行，对话框不得抢去派发默认按钮。XGui 侧按
 *              vtable 精确比对：XTextEdit/XPlainTextEdit。注意
 *              XTextControl（多行编辑的模型控制器）继承 XObject 而非
 *              XWidget，不可能成为焦点控件，不参与判定。 */
static bool dialog_focusIsMultilineEditor(const XWidget* widget)
{
    XVtable* vtable;
    if (!widget) return false;
    vtable = XClassGetVtable(widget);
    return vtable == XTextEdit_class_init() ||
           vtable == XPlainTextEdit_class_init();
}

/** @brief      先序遍历对话框子树找默认按钮。
 *  @details    对标 Qt 6.8 QDialog::keyPressEvent 无显式默认时回落
 *              「第一个可见可用 autoDefault 按钮」（findChildren 顺
 *              序=插入序，先序遍历同序）。显式 setDefault 的按钮全
 *              树最高优先（Qt d->defaultButton 语义），命中即短路。
 *              可见性取有效可见（XWidget_isVisible 已对齐 Qt 的
 *              isVisible 语义），可用性按 WA_Disabled 位（同 Qt
 *              isEnabled）。 */
static void dialog_walkForDefaultButton(XObject* object,
                                        XPushButton** explicitDefault,
                                        XPushButton** firstAutoDefault)
{
    int count;
    int i;
    if (!object || *explicitDefault) return;
    if (XClassGetVtable(object) == XPushButton_class_init()) {
        XPushButton* button = (XPushButton*)object;
        if (XWidget_isVisible((XWidget*)button) &&
            XWidget_isEnabled((XWidget*)button)) {
            if (button->m_defaultButton) {
                *explicitDefault = button;
                return;
            }
            /* 对标 Qt 6.8 QPushButton::autoDefault 语义：对话框内的
             * QPushButton 默认即 autoDefault（autoDefaultControl 是
             * 按父链 windowType==Dialog 判定，而 XGui 存在以
             * parent+flags=0 构造的子控件形态对话框——如 XMessageBox
             * 便捷路径——其内按钮按该判定会失去 autoDefault）。本遍
             * 历起点即对话框，子树内按钮仅显式 Off 才退出候选。 */
            if (!*firstAutoDefault &&
                button->m_autoDefault != XPushButtonAutoDefault_Off)
                *firstAutoDefault = button;
        }
    }
    if (object->m_children) {
        count = XVector_size_base((const XContainer*)object->m_children);
        for (i = 0; i < count; ++i) {
            XObject* const* children =
                (XObject* const*)XContainerDataAddr(object->m_children);
            dialog_walkForDefaultButton(children[i],
                                        explicitDefault, firstAutoDefault);
            if (*explicitDefault) return;
        }
    }
}

/** @brief      解析对话框当前生效的默认按钮（显式优先，回落首个
 *              可见可用 autoDefault）；无则 NULL。 */
static XPushButton* dialog_defaultButton(XDialog* dialog)
{
    XPushButton* explicitDefault = NULL;
    XPushButton* firstAutoDefault = NULL;
    dialog_walkForDefaultButton((XObject*)dialog,
                                &explicitDefault, &firstAutoDefault);
    return explicitDefault ? explicitDefault : firstAutoDefault;
}

/** @brief      对话框子树内是否已持有应用焦点控件。 */
static bool dialog_containsFocus(const XDialog* self)
{
    const XWidget* focus = XWidget_appFocusWidget();
    const XWidget* w;
    if (!focus) return false;
    for (w = focus; w; w = XWidget_parentWidget(w))
        if ((const XWidget*)self == w) return true;
    return false;
}

/** @brief      对话框可见后抢占初始焦点（对标 Qt showModal 的
 *              initialFocusWidget 落点：默认按钮优先，无则对话框
 *              自身）。不抢焦点时平台键按原生窗口树投递，对话框收
 *              不到任何按键（配合 VXGuiApplication_notify 的键重定
 *              向注释）。 */
static void dialog_grabInitialFocus(XDialog* self)
{
    XPushButton* button;
    if (!self || dialog_containsFocus(self)) return;
    button = dialog_defaultButton(self);
    XWidget_setFocusReason(button ? (XWidget*)button : (XWidget*)self,
                           XFocusReason_Other);
}

/** @brief      先序收集对话框子树内可 Tab 聚焦的子控件。
 *  @details    对标 Qt 6.8 qapplication.cpp
 *              focusNextPrevChild_helper 的候选判定：enabled、可见、
 *              focusPolicy 含 TabFocus 位（StrongFocus 含该位）。文档
 *              序（先序）即 Qt 焦点链顺序。 */
static void xdlg_collectTabCandidates(XObject* object, XVector* out)
{
    int count;
    int i;
    if (!object || !out) return;
    if (object->is_widget) {
        XWidget* w = (XWidget*)object;
        if (XWidget_isEnabled(w) && XWidget_isVisible(w) &&
            (XWidget_focusPolicy(w) & XWidgetFocusPolicy_TabFocus))
            XVector_push_back_1_base(out, &w);
    }
    if (object->m_children) {
        count = XVector_size_base((const XContainer*)object->m_children);
        for (i = 0; i < count; ++i) {
            XObject* const* children =
                (XObject* const*)XContainerDataAddr(object->m_children);
            xdlg_collectTabCandidates(children[i], out);
        }
    }
}

/** @brief      模态子树内 Tab/Shift+Tab 焦点环绕（不越出对话框）。
 *  @details    对标 Qt 6.8 QWidget::focusNextPrevChild：QDialog 是独
 *              立原生窗口，焦点链天然局限在对话框子树内。XGui 单原
 *              生窗口模型下，XWidget 事件层 Tab 兜底按顶层窗口全域
 *              文档序移动焦点（XWidget_focusChainTarget），Tab×N 会
 *              越出模态子树（夜间台账 #23：Tab×2 后焦点落在页面触
 *              发按钮上，Esc 随即失效、对话框滞留）。本函数把移动限
 *              制在对话框子树内并环绕。注意：按键自子控件沿父链上
 *              抛到达本对话框时，子控件自身的窗口域兜底可能已消费
 *              Tab——按钮盒路径由盒的显式 Tab 环链（XWidget_setTab
 *              Order 闭环，见 XDialogButtonBox.c xdb_relayout）先行
 *              在子树内消费；本函数兜住其余情形（焦点在对话框自身
 *              或非 Tab 候选控件上）。 */
static bool xdlg_focusNextPrevChild(XDialog* self, bool next)
{
    XVector* list;
    XWidget* current;
    XWidget* target = NULL;
    int count;
    int i;
    int idx = -1;
    if (!self) return false;
    list = XVector_Create(XWidget*);
    if (!list) return false;
    xdlg_collectTabCandidates((XObject*)self, list);
    count = (int)XVector_size_base((const XContainer*)list);
    if (count == 0) {
        XVector_delete_base((XClass*)list);
        return false;
    }
    current = XWidget_appFocusWidget();
    for (i = 0; i < count; ++i) {
        if (XVector_At_Base(list, (int64_t)i, XWidget*) == current) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        /* 焦点不在子树内（如初始焦点落在对话框自身）：Qt 语义从链
         * 首（Tab）/链尾（Shift+Tab）进入。 */
        target = XVector_At_Base(list, next ? 0 : (int64_t)count - 1,
                                 XWidget*);
    } else {
        int step = next ? 1 : count - 1;
        target = XVector_At_Base(list, (int64_t)((idx + step) % count),
                                 XWidget*);
    }
    XVector_delete_base((XClass*)list);
    if (!target || target == current) return false;
    XWidget_setFocusReason(target, next ? XFocusReason_Tab
                                        : XFocusReason_Backtab);
    return true;
}

static void VXDialog_keyPressEvent(XWidget* self, XEvent* event)
{
    XDialog* dialog = (XDialog*)self;
    if (dialog && event &&
        XEvent_type(event) == XEVENT_TYPE_KEY_PRESS) {
        /* 对标 QDialog::keyPressEvent：Escape 触发 reject()。 */
        if (((XKeyEvent*)event)->m_key == (int)XKey_Escape) {
            XDialog_reject(dialog);
            XEvent_accept(event);
            return;
        }
        /* 对标 Qt 6.8 qpushbutton.cpp QPushButton::keyPressEvent：焦
           点落在 autoDefault 按钮上时，Enter/Return 点击的是聚焦按
           钮本身而非默认按钮。XGui 子控件形态对话框以 parent+flags=0
           构造，XPushButton_autoDefault 的「父链 windowType==Dialog」
           判定失效（Auto 恒解为 false），聚焦取消钮后回车仍触发确
           定（夜间台账 #24）——此处按 dialog_walkForDefaultButton 同
           口径直接判 m_autoDefault != Off。 */
        if (((XKeyEvent*)event)->m_key == (int)XKey_Tab ||
            ((XKeyEvent*)event)->m_key == (int)XKey_Backtab) {
            /* 对标 QWidget::event 的 Tab/Shift+Tab 焦点遍历，限定在
               模态子树内环绕（详见 xdlg_focusNextPrevChild 注）。
               Shift 修饰按 Qt 惯例把 Tab 反向为 Backtab。 */
            int key = ((XKeyEvent*)event)->m_key;
            int mods = (int)((XKeyEvent*)event)->m_modifiers;
            bool next = (key == (int)XKey_Tab) ==
                        ((mods & (int)XKeyboardModifier_ShiftModifier) == 0);
            if ((mods & ~(int)XKeyboardModifier_ShiftModifier) == 0 &&
                xdlg_focusNextPrevChild(dialog, next)) {
                XEvent_accept(event);
                return;
            }
        }
        if (((XKeyEvent*)event)->m_key == (int)XKey_Return ||
            ((XKeyEvent*)event)->m_key == (int)XKey_Enter) {
            XWidget* focus = XWidget_focusWidget(self);
            if (!dialog_focusIsMultilineEditor(focus)) {
                XPushButton* button = dialog_defaultButton(dialog);
                /* 焦点在可见可用 autoDefault 按钮上：点击聚焦按钮
                   （Qt QPushButton::keyPressEvent 语义，见上注）。 */
                if (focus &&
                    XClassGetVtable(focus) == XPushButton_class_init()) {
                    XPushButton* focused = (XPushButton*)focus;
                    if (XWidget_isVisible(focus) &&
                        XWidget_isEnabled(focus) &&
                        focused->m_autoDefault != XPushButtonAutoDefault_Off)
                        button = focused;
                }
                if (button) {
                    XPushButton_click(button);
                    XEvent_accept(event);
                    return;
                }
            }
        }
    }
    /* 对标 QDialog::keyPressEvent 非 Esc 分支静态调用基类实现
       （QWidget::keyPressEvent 默认 ignore 以便沿父链传播）。
       此前经 XWidget_keyPressEvent_base 转发：该 _base 入口按对象虚表
       再分派回最派生重载 VXDialog_keyPressEvent，非 Esc 按键
       即形成无界自递归栈溢出（复扫 P0-1；XWizard/XInputDialog/
       XColorDialog/XFileDialog/XProgressDialog/XErrorMessage 均未
       覆写 keyPress，收到按键全数命中）。现按 XDockWidget/
       XToolBar 的 XClass_Parent 口径静态取 XWidget 类虚表 keyPress
       槽位（即 XWidget 本类默认实现 XWidget_ignoreEvent_default，
       与 VXWidget_event 分派到 XWidget 本类时所用同一层）。 */
    XClass_Parent(XWidget, EXWidget_KeyPressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief  对话框显示事件：未显式定位过的对话框按父窗口重新居中。
 *  @details 对标 QDialog::showEvent（qdialog.cpp:859-868）：非自发
 *           show 且无 WA_Moved 时 adjustPosition(parentWidget())，并
 *           把 Moved 位复位——「本次居中不算显式定位」，用户显式
 *           move/setGeometry 过（Moved 置位）的对话框不再被搬动，
 *           每次未定位 show 都重新居中。XGui 窗口几何即客户区屏幕
 *           坐标（平台层内部 AdjustWindowRectEx 扩边），Qt 以框架
 *           位置 move 故需 extraw/extrah 修正；本实现 move 语义为
 *           客户区原点，无框架偏移可补，居中公式相应省略该修正。 */
static void VXDialog_showEvent(XWidget* self, XEvent* event)
{
    XDialog* dialog = (XDialog*)self;
    if (dialog && event && XEvent_type(event) == XEVENT_TYPE_SHOW) {
        if (!XWidget_testAttribute(self, XWidgetAttribute_Moved)) {
            xdlg_centerToParentWindow(dialog);
            /* 复位 Moved：本次居中不是显式定位（对标 qdialog.cpp:864
             * setAttribute(WA_Moved, false)），下次 show 仍重新居中。 */
            XWidget_setAttribute(self, XWidgetAttribute_Moved, false);
        }
        /* 对标 Qt modal 属性语义（qdialog.cpp:996-1008「等价
         * windowModality=ApplicationModal」+ qwidget.cpp:11438-11452
         * WA_ShowModal 置位即同步模态）：setModal(true) 的对话框经裸
         * XWidget_show 显示（非 exec/open）时登记应用模态门，补齐
         * 「show 消费 modal 属性」缺口。exec/open 已各自无条件登记，
         * 同为对全局门的覆写，重复登记无害。 */
        if (dialog->m_modal)
            XWidget_setApplicationModalWidget(self);
        /* CSD 内容避让（装饰条让位）：顶层首显建窗（XWidget_create
         * Window→XWindowDecoration_syncWindow 落盘 frameMargins）早于
         * SHOW 事件，此处查询已是真实条高；未装饰/系统条模式增量恒
         * 零直接短路。覆写 showEvent 且末尾静态父调本实现的派生类
         * （XMessageBox/XProgressDialog）在此统一收口；纯 show() 路径
         * 其后 XWidget_setVisible 尾部的 XLayout_activate 按已改写边
         * 距解算，布局挂载对话框当帧避让。 */
        xdlg_applyContentAvoidance(dialog);
    }
    XClass_Parent(XWidget, EXWidget_ShowEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief  对话框关闭事件：可见时等价 reject，拒绝被吞的关闭。
 *  @details 对标 QDialog::closeEvent（qdialog.cpp:727-741）：可见时
 *           reject()（done(Rejected)→隐藏+rejected/finished 信号），
 *           reject 后仍可见（派生类拒绝隐藏）则 ignore 关闭事件；
 *           不可见时直接接受。XDialog_reject→done→隐藏为同步，正常
 *           一次即隐藏。 */
static void VXDialog_closeEvent(XWidget* self, XEvent* event)
{
    XDialog* dialog = (XDialog*)self;
    if (dialog && event && XEvent_type(event) == XEVENT_TYPE_CLOSE) {
        if (XWidget_isVisible(self)) {
            XDialog_reject(dialog);
            if (XWidget_isVisible(self)) {
                XEvent_ignore(event);
                return;
            }
        }
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XWidget, EXWidget_CloseEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief  对话框隐藏事件：退出 exec 事件循环，不发射任何信号。
 *  @details 对标 QDialogPrivate::setVisible(false) 的 eventLoop->exit
 *           （qdialog.cpp:839-842）与 finished() 文档（qdialog.cpp:
 *           1079-1082：hide()/setVisible(false) 不发射 finished）——
 *           exec 循环退出但信号只出自 done/accept/reject 收口。 */
static void VXDialog_hideEvent(XWidget* self, XEvent* event)
{
    XDialog* dialog = (XDialog*)self;
    if (dialog && event && XEvent_type(event) == XEVENT_TYPE_HIDE) {
        dialog->m_inExec = false;
        /* 对称解除模态门：直接 setVisible(false) 不经 done/xdlg_close
         * 时，门会永久留在自身（其它顶层窗全被阻塞）。gate==self 守卫
         * 与 xdlg_close 的解除同口径，覆写无害。 */
        if (XWidget_applicationModalWidget() == (XWidget*)self)
            XWidget_setApplicationModalWidget(NULL);
    }
    XClass_Parent(XWidget, EXWidget_HideEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/* ==================== 标题栏 [×] 鼠标交互（Win10 caption 关闭钮） ==================== */

/** @brief  按下：命中 [×] 即武装按压态（悬停红底加深），接受事件。
 *  @details 仅在「标题栏 gate + 本对话框画过标题带」时响应——后者使
 *           覆写 paintEvent 且不回链 XDialog 面板绘制的派生类（如
 *           XWizard）不会出现隐形关闭热区。未命中走基类默认（忽略，
 *           保持既有父链传播语义）。 */
static void VXDialog_mousePressEvent(XWidget* self, XEvent* event)
{
    XDialog* dialog = (XDialog*)self;
    if (dialog && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_PRESS &&
        xdlg_titlebarGate(dialog) && xdlg_tb_paintedContains(dialog) &&
        xdlg_closeButtonHit(dialog,
                            ((const XMouseEvent*)event)->m_position.x,
                            ((const XMouseEvent*)event)->m_position.y)) {
        xdlg_tb_owner = dialog;
        xdlg_tb_hover = true;
        xdlg_tb_pressed = true;
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    XClass_Parent(XWidget, EXWidget_MousePressEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief  释放：按住后仍在 [×] 内松开 → reject（Esc 同语义，复用
 *          QDialog reject 路径 done(0) + rejected 信号）。
 *  @details Win10 caption 关闭钮的「按下武装、释放命中触发」按钮语
 *           义；done() 内会解除跟踪态，reject 前先刷新一次脏区避免
 *           隐藏帧残留按压红底。 */
static void VXDialog_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XDialog* dialog = (XDialog*)self;
    if (dialog && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_BUTTON_RELEASE &&
        xdlg_tb_owner == dialog && xdlg_tb_pressed) {
        bool inside =
            xdlg_closeButtonHit(dialog,
                                ((const XMouseEvent*)event)->m_position.x,
                                ((const XMouseEvent*)event)->m_position.y);
        xdlg_tb_pressed = false;
        XEvent_accept(event);
        if (inside) {
            XWidget_update(self);
            XDialog_reject(dialog);
            return;
        }
        if (xdlg_tb_hover) {
            xdlg_tb_hover = false;
            XWidget_update(self);
        }
        return;
    }
    XClass_Parent(XWidget, EXWidget_MouseReleaseEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

/** @brief  移动：跟踪指针是否落在 [×] 上（悬停浅红），不消费事件。
 *  @details 子控件形态对话框的标题栏/边距区无子控件，命中测试即本
 *           对话框；自子控件忽略上抛的移动事件同样按本对话框局部坐
 *           标命中，悬停跟踪因此覆盖整个对话框子树。owner 切换时同
 *           步刷新前后两个对话框（前一对话框悬停残影即时消除）。 */
static void VXDialog_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XDialog* dialog = (XDialog*)self;
    if (dialog && event &&
        XEvent_type(event) == XEVENT_TYPE_MOUSE_MOVE &&
        xdlg_titlebarGate(dialog) && xdlg_tb_paintedContains(dialog)) {
        bool hover =
            xdlg_closeButtonHit(dialog,
                                ((const XMouseEvent*)event)->m_position.x,
                                ((const XMouseEvent*)event)->m_position.y);
        if (xdlg_tb_owner != dialog) {
            const XDialog* prev = xdlg_tb_owner;
            xdlg_tb_owner = dialog;
            xdlg_tb_hover = hover;
            xdlg_tb_pressed = false;
            if (prev) XWidget_update((XWidget*)prev);
            XWidget_update(self);
        } else if (hover != xdlg_tb_hover) {
            xdlg_tb_hover = hover;
            xdlg_tb_pressed = xdlg_tb_pressed && hover;
            XWidget_update(self);
        }
        /* 不 accept：悬停跟踪不改变既有鼠标传播语义。 */
    }
    XClass_Parent(XWidget, EXWidget_MouseMoveEvent,
                  void (*)(XWidget*, XEvent*))(self, event);
}

XVtable* XDialog_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XDialog)
    XVTABLE_INHERIT_XCLASS(XWidget);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXDialog_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_KeyPressEvent, VXDialog_keyPressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ShowEvent, VXDialog_showEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VXDialog_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_CloseEvent, VXDialog_closeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXDialog_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXDialog_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXDialog_mouseMoveEvent);
    return XVTABLE_DEFAULT;
}

void XDialog_init(XDialog* self, XWidget* parent, XWidgetFlags flags)
{
    if (!self) return;
    XMemset(self, 0, sizeof(*self));
    /* 对标 QDialog 构造（qdialog.cpp:374-378）：flags 不含窗口类型位
     * 时自动叠加 Dialog 类型。Dialog 数值（0x3）含 Window 位，XWidget
     * init 的 isWindow 判定即命中——XGui 对话框与 Qt QDialog 一样恒
     * 为独立顶层窗口（带父控件也是），不作为父链子控件参与布局/绘
     * 制/命中。显式传入 Popup/Tool/ToolTip 等其它类型位时尊重调用方。 */
    if ((flags & (XWidgetFlags)XWindowType_TypeMask) == 0)
        flags |= (XWidgetFlags)XWindowType_Dialog;
    XWidget_init(&self->m_base, parent, flags);
    XClassSetVtable(self, XDialog);
    Set_Class_Memory(self, XCLASS_DEFAULT_MEMORY_TYPE);
    Set_Class_IsHeap(self, false);
    /* 对标 Qt：QDialog 恒为顶层窗口、自动回填 palette Window 背景
     * （历史子控件形态对话框透明根因的兜底，窗口形态同样无害）。 */
    XWidget_setAutoFillBackground((XWidget*)self, true);
    /* R-81 根因：m_modal 默认 true 与 Qt QDialog 默认 false 相反，且
       show() 不消费该属性（只有 exec/open 模态化），isModal() 查询值
       与实际行为不自洽。对标 Qt 6.8.3：modal 默认 false，仅 exec（无
       条件应用模态）/open（setModal(true)）时模态化。 */
    self->m_modal = false;
    self->m_result = 0;
    self->m_inExec = false;
    self->m_sizeGripEnabled = false;
    self->m_resetModalityTo = -1;
    /* CSD 内容避让增量记账清零（XMemset 已覆盖，显式赋值同其余字段
     * 口径；show/exec/open 首次套用时按全量偏移补足）。 */
    self->m_csdAppliedTop = 0;
}

XDialog* XDialog_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags)
{
    XDialog* self = (XDialog*)XMemory_malloc(sizeof(*self), memory);
    if (!self) return NULL;
    XDialog_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/** @brief      exec 循环的「宿主终结」感知（夜间台账 F-② 根修）。
 *  @details    返回 true 表示对话框必须立即收尾（返回值经 *quit 语义
 *              区分来源，仅供注释说明，不改变收尾动作）：应用已请求
 *              退出，或对话框的宿主顶层窗口已不可见。
 *
 *              机制（实测复现链）：XGui 对话框阻塞是裸 while+
 *              processEvents（非 QEventLoop），quit 标记的循环退出它
 *              天生感知不到；而 XDialog_done 的收尾兜底只挂在
 *              XGuiApplication_removeWindow（WM ✕/WM_DELETE→destroy→
 *              注销→lastWindowClosed）路径。自绘标题栏装饰的 [✕] 与
 *              系统菜单「关闭」走 XWidget_close（XWidget.c:4489：
 *              CloseEvent 接受后仅 setVisible(false)），不注销不退出，
 *              兜底整条不经过——主窗隐藏后子控件对话框连同宿主一起
 *              从屏幕消失，exec 循环仍阻塞在 WaitForMoreEvents 上，
 *              进程滞留成无界面不可退出态（只能 SIGKILL）。
 *
 *              对标 Qt：QCoreApplication::exit 会退出全部嵌套事件循环
 *              （qcoreapplication.cpp，exit 遍历 d->eventLoops），
 *              QDialog::exec 的内层循环随 quit 一并返回。XGui 侧
 *              XCoreApplication_exit 在主应用循环在跑时走 g_execLoop
 *              早退分支（只标主循环、不置 m_quitNow、不遍历循环栈，
 *              该文件不在本次修复所有权内），故本感知按两路互补：
 *              - quitNow：exit() 在主循环未建立/已收尾路径置位的标记
 *                （XThreadData.m_quitNow），覆盖无主循环 exec 与
 *                主循环退出后再入的场合；
 *              - 宿主可见性：对话框宿主顶层（子控件形态）或对话框
 *                自身顶层（窗口形态）effectively 不可见即收尾——与
 *                removeWindow 兜底已定案的「模态框随宿主窗体终结」
 *                语义（done(0)）严格同键；XGui 单原生窗口模型下
 *                隐藏宿主后对话框无任何可交互窗面，「对话框仍可用」
 *                不可能成立，取 Qt「app 不滞留」一翼（对标口径已
 *                报备出口准则「二选一并注明」）。
 *              主循环在跑且仅 quit 不藏窗的场合（主循环被标退、
 *              m_quitNow 未置、窗面仍可见）不属滞留态：对话框仍可
 *              交互收尾，控制权回主循环后即感知退出，无悬挂风险。 */
static bool xdlg_execShouldFinish(XDialog* self, XWidget* selfw)
{
    XThreadData* data;
    XWidget* top;
    if (!self || !selfw) return false;
    data = XThreadData_current();
    if (data && data->m_quitNow)
        return true;
    top = selfw->m_isWindow ? selfw : XWidget_topLevelWidget(selfw);
    if (!top || !XWidget_isVisible(top))
        return true;
    return false;
}

/** @brief  恢复 open() 临时切换的窗口模态（对标 QDialogPrivate::
 *          resetModalitySetByOpen，qdialog.cpp:458-471）。
 *  @details open() 记录了原模态值时恢复之；用户此后显式 setWindow
 *           Modality 的保护位（Qt WA_SetWindowModality）XGui 未建模，
 *           以「记录即恢复」等价实现。done/exec 关闭路径调用。 */
static void xdlg_resetModalitySetByOpen(XDialog* self)
{
    if (!self || self->m_resetModalityTo == -1) return;
    XWidget_setWindowModality((XWidget*)self,
                              (XWindowModality)self->m_resetModalityTo);
    self->m_resetModalityTo = -1;
}

/** @brief  正确关闭对话框并置结果码（对标 QDialogPrivate::close，
 *          qdialog.cpp:118-150）：setResult、退出 exec 循环、清应用
 *          模态门、恢复 open 临时模态、隐藏并清理标题带跟踪态。 */
static void xdlg_close(XDialog* self, int resultCode)
{
    if (!self) return;
    self->m_result = resultCode;
    self->m_inExec = false;
    if (XWidget_applicationModalWidget() == (XWidget*)self)
        XWidget_setApplicationModalWidget(NULL);
    xdlg_resetModalitySetByOpen(self);
    XWidget_setVisible((XWidget*)self, false);
    /* 隐藏即解除本对话框的 [×] 悬停/按压跟踪与「画过标题带」登记
     * （复显后由 paintEvent 重新登记，不留残态/悬停红底）。 */
    if (xdlg_tb_owner == (const XDialog*)self) {
        xdlg_tb_owner = NULL;
        xdlg_tb_hover = false;
        xdlg_tb_pressed = false;
    }
    xdlg_tb_unmark(self);
    /* 隐藏后标脏原矩形：非窗口隐藏分支无重绘调度，屏幕残留对话框
       最后一帧鬼影；把矩形折算进顶层脏区后，合成器按可见内容重画
       该区域（对话框已隐藏即父级/邻居内容）。 */
    XWidget_update((XWidget*)self);
}

/** @brief  结果码 → 对话框码映射（对标 QDialogPrivate::dialogCode 与
 *          QMessageBoxPrivate::dialogCode 的角色覆写）。
 *  @details 基类语义：rescode 原样（<=Accepted(1) 即 DialogCode；
 *           >1 时既非 Accepted 也非 Rejected，不映射任何信号——Qt
 *           同值直通）。QMessageBox 覆写：>1 的标准按钮位值按最近点
 *           击按钮角色映射 Accepted/Rejected（qmessagebox.cpp:471-
 *           491）。C 结构体无虚私有函数，此处按 vtable 精确比对向
 *           下识别 XMessageBox（先例：dialog_focusIsMultilineEditor
 *           的 XTextEdit 比对），任一宏裁剪时退化为基类语义。 */
static int xdlg_dialogCode(const XDialog* self)
{
    int rescode = self ? self->m_result : 0;
#if XMESSAGEBOX_ON && XDIALOGBUTTONBOX_ON
    /* 双宏同开才编译本段：XMessageBox::m_buttonBox 字段受
       XDIALOGBUTTONBOX_ON 守卫（XMessageBox.h），仅开 XMESSAGEBOX_ON
       时此处解引用编译失败——角色映射退化为基类语义即可。 */
    if (rescode > (int)XDialogCode_Accepted && self) {
        const XMessageBox* box = (const XMessageBox*)self;
        if (XClassGetVtable((const XObject*)self) ==
                XMessageBox_class_init() &&
            box->m_buttonBox && box->m_clicked) {
            switch (XDialogButtonBox_buttonRole(box->m_buttonBox,
                                                box->m_clicked)) {
            case XDialogButtonBoxRole_AcceptRole:
            case XDialogButtonBoxRole_YesRole:
                return (int)XDialogCode_Accepted;
            case XDialogButtonBoxRole_RejectRole:
            case XDialogButtonBoxRole_NoRole:
                return (int)XDialogCode_Rejected;
            default:
                break;
            }
        }
    }
#endif /* XMESSAGEBOX_ON && XDIALOGBUTTONBOX_ON */
    return rescode;
}

int XDialog_exec(XDialog* self)
{
    XWidget* selfw;
    bool deleteOnClose;
    int result;
    if (!self) return 0;
    selfw = (XWidget*)self;
    /* 对标 QDialog::exec（qdialog.cpp:553-556）：递归 exec 打印警告
       并返回 -1（警告经 XPrintf，控制台打印纪律）。 */
    if (self->m_inExec) {
        XPrintf("XDialog::exec: Recursive call detected\n");
        return -1;
    }
    /* 对标 qdialog.cpp:558-559：exec 期间暂时清除 DeleteOnClose，返
       回时按原状态兑现删除。 */
    deleteOnClose = XWidget_testAttribute(selfw,
                                          XWidgetAttribute_DeleteOnClose);
    XWidget_setAttribute(selfw, XWidgetAttribute_DeleteOnClose, false);
    /* 对标 qdialog.cpp:561：exec 先恢复 open 遗留的临时窗口模态。 */
    xdlg_resetModalitySetByOpen(self);
    self->m_inExec = true;
    XDialog_setResult(self, 0);
    xdlg_centerToParentWindow(self);
    XWidget_show(selfw);
    /* 显示即标脏本对话框矩形：窗口分支 show→update 已覆盖，此处冗
       余标脏无副作用（兼容 overrideWindowFlags 改型的兜底）。 */
    XWidget_update(selfw);
    /* 对标 QDialog::exec：exec 期间无条件应用模态（Qt 语义为
       ApplicationModal），不受 modal 属性默认值影响。 */
    XWidget_setApplicationModalWidget(selfw);
    /* CSD 内容避让（装饰条让位）：show 返回时建窗已完成、frameMargins
       已落盘，赶在下方 updateGeometry 布局激活前套用——布局挂载对话
       框按新边距解算，显式几何对话框子控件已位移。幂等（showEvent
       已套用则增量零短路）。 */
    xdlg_applyContentAvoidance(self);
    /* show 后强制激活布局：布局的挂起激活不保证随 show 走到
       （XBoxLayout 子控件曾零几何不绘制）。幂等。 */
    XWidget_updateGeometry(selfw);
    /* 对标 QDialog::exec：阻塞于事件循环直到 done()。WaitForMoreEvents
       防忙等空转（复扫 R-23）。 */
    dialog_grabInitialFocus(self);
    while (self->m_inExec) {
        XCoreApplication_processEvents(XEventLoop_AllEvents |
                                       XEventLoop_WaitForMoreEvents);
        /* 宿主终结感知（远端合并带入的 F-② 根修）：quit 标记或宿主
           顶层不可见即按 done(0) 收尾——主窗隐藏/应用退出不再滞留成
           不可退出进程（与 removeWindow 兜底同键）。 */
        if (!self->m_inExec)
            break; /* done() 已收尾：正常路径。 */
        if (xdlg_execShouldFinish(self, selfw)) {
            XDialog_done(self, 0);
            break;
        }
        XWidget_setApplicationModalWidget(selfw);
    }
    if (XWidget_applicationModalWidget() == selfw)
        XWidget_setApplicationModalWidget(NULL);
    /* 先取结果再兑现删除（对标 qdialog.cpp:583-587 的 result()/
       delete this 次序），删除后不得再解引用 self。 */
    result = self->m_result;
    if (deleteOnClose)
        XDialog_delete_base(self);
    return result;
}

void XDialog_done(XDialog* self, int result)
{
    int dialogCode;
    if (!self) return;
    xdlg_close(self, result);
    /* 对标 QDialog::done（qdialog.cpp:608-626）：先按结果码映射发射
       accepted()/rejected()，再发射 finished(result)——hide 本身不
       发信号（验收 13），信号只出自 done/accept/reject 收口。 */
    dialogCode = xdlg_dialogCode(self);
    if (dialogCode == (int)XDialogCode_Accepted)
        xdlg_emitVoid(self, (size_t)XDialog_accepted_signal);
    else if (dialogCode == (int)XDialogCode_Rejected)
        xdlg_emitVoid(self, (size_t)XDialog_rejected_signal);
    xdlg_emitFinished(self, result);
}

void XDialog_accept(XDialog* self)
{
    /* 对标 QDialog::accept = done(Accepted)：accepted() 由 done 内部
       按结果码发射，不在此重复。 */
    XDialog_done(self, (int)XDialogCode_Accepted);
}

void XDialog_reject(XDialog* self)
{
    /* 对标 QDialog::reject = done(Rejected)：rejected() 由 done 内部
       按结果码发射。 */
    XDialog_done(self, (int)XDialogCode_Rejected);
}

int XDialog_result(const XDialog* self) { return self ? self->m_result : 0; }
void XDialog_setResult(XDialog* self, int result) { if (self) self->m_result = result; }
void XDialog_setModal(XDialog* self, bool modal)
{
    if (!self || self->m_modal == modal) return;
    self->m_modal = modal;
    /* 对标 qwidget.cpp:11440-11445（WA_ShowModal 置位处理）：modal 置
     * 真且窗口模态仍为 NonModal 时同步 ApplicationModal，使 isModal()/
     * windowModality()/XWidget_isModal() 查询一致，且值经建窗链路自
     * 然下发（有句柄即推 XWindow，无句柄由建窗统一推入）——Qt 中
     * QMessageBox 构造完 windowModality 已是 ApplicationModal。 */
    if (modal &&
        XWidget_windowModality((XWidget*)self) ==
            XWindowModality_NonModal)
        XWidget_setWindowModality((XWidget*)self,
                                  XWindowModality_ApplicationModal);
    /* 可见的对话框即时生效模态登记（对标 open()/setModal 语义）。 */
    if (modal && XWidget_isVisible((XWidget*)self))
        XWidget_setApplicationModalWidget((XWidget*)self);
}
bool XDialog_isModal(const XDialog* self) { return self ? self->m_modal : false; }

void XDialog_open(XDialog* self)
{
    XWidget* selfw;
    if (!self) return;
    selfw = (XWidget*)self;
    /* 对标 QDialog::open（qdialog.cpp:509-526）：open 前若窗口模态
       不是 WindowModal，记录原值并临时改为 WindowModal，关闭时由
       xdlg_close→resetModalitySetByOpen 恢复（验收：open 前后读
       windowModality 依次为 原值→WindowModal→原值）。 */
    if (XWidget_windowModality(selfw) != XWindowModality_WindowModal) {
        self->m_resetModalityTo = (int)XWidget_windowModality(selfw);
        XWidget_setWindowModality(selfw, XWindowModality_WindowModal);
    }
    XDialog_setResult(self, 0);
    xdlg_centerToParentWindow(self);
    XWidget_show(selfw);
    /* 窗口形态对话框：show 后 z 序默认排在主窗之下，整窗被主窗覆盖
     * （"只出现文字没有窗口"实测根因）。置顶提到主窗之上（对标
     * QDialog 打开即激活前置；XWindow_raise 无平台 Z 序接口时为
     * no-op，win32 后端补齐后此调用即真实生效）。 */
    if (selfw->m_isWindow)
        XWidget_raise(selfw);
    /* 显示即标脏本对话框矩形（根因同 XDialog_exec 注）：兜底一次重
     * 复标脏，保证面板/文本/按钮下一帧真实上屏。 */
    XWidget_update(selfw);
    /* CSD 内容避让（装饰条让位，同 exec 注）：open 路径无 exec 的
     * updateGeometry 收尾，而 XWidget_show 内的布局激活先于本套用
     * ——此处补一次强制激活，让改写后的根布局顶边距当帧生效（增量
     * 幂等，显式几何对话框子控件此时已位移，激活对其无副作用）。 */
    xdlg_applyContentAvoidance(self);
    XWidget_updateGeometry(selfw);
    /* 项目模态门（应用模态为窗口模态阻塞的既定等价物，demo 约定
       "模态门照常生效"）：open 显示期间登记，done/close 解除。 */
    XWidget_setApplicationModalWidget(selfw);
    dialog_grabInitialFocus(self);
}

void XDialog_setSizeGripEnabled(XDialog* self, bool enable)
{ if (self) self->m_sizeGripEnabled = enable; }
bool XDialog_isSizeGripEnabled(const XDialog* self)
{ return self ? self->m_sizeGripEnabled : false; }

void* XDialog_accepted_signal(XDialog* self)
{ (void)self; return (void*)(size_t)XDialog_accepted_signal; }
void* XDialog_rejected_signal(XDialog* self)
{ (void)self; return (void*)(size_t)XDialog_rejected_signal; }
void* XDialog_finished_signal(XDialog* self, int result)
{ (void)self; (void)result; return (void*)(size_t)XDialog_finished_signal; }








#endif /* XWIDGET_ON && XDIALOG_ON */