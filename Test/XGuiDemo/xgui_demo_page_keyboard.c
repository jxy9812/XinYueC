/******************************************************************************
 * @file       xgui_demo_page_keyboard.c
 * @brief      XGuiWindowDemo 屏幕虚拟键盘页（XLineEdit 输入 + 底部
 *             XVirtualKeyboard 弹层，独立翻译单元）。
 * @details    实现契约头 xgui_demo_pages.h 中的
 *             demo_page_keyboard_build / demo_page_keyboard_autotest：
 *             - build：页内装配三框并列 hints 自动切键盘演示——默认
 *               框（无 hints）/ 数字框（ImhDigitsOnly）/ 英文框
 *               （ImhLatinOnly），点框取焦点即按提示位自动切换键盘布
 *               局（Keyboard.qml:42-49 优先级口径）+ 第二输入框（焦
 *               点跟随重绑演示）+ 只读操作说明行（XLABEL_ON 门控，
 *               缺省回退只读 XLineEdit）+ XVirtualKeyboard（autoPopup 开，
 *               初始隐藏；XKEYBOARD_IME_ON 时启用拼音 IME 并置英文
 *               态——User1 槽位载入内置拼音布局，「中/EN」键可见可
 *               玩；守护轮询在应用焦点落到编辑框后自动挂顶层窗口底
 *               部弹出）；
 *             - autotest：全程非阻塞，全部直调 API + XObject_event_base
 *               合成鼠标/键盘事件（不依赖守护 tick 到达——200ms 轮询在
 *               帧 3 同步 autotest 序列不保证触发，自动弹出联动留真键
 *               盘目验）；断言 popup 几何、字符写入、退格/光标/换行、
 *               大写布局 Shift 契约与收层；XKEYBOARD_IME_ON 时追加阶
 *               段 6 IME 端到端（组串拦截/候选带/空格上屏/切回英文直
 *               写/换框重绑组串复位；组串断言经 xkb 垫片双世界——框
 *               架世界读 context.preeditText 镜像，既有世界直读
 *               m_ime.m_buffer，语义不变）；XKEYBOARD_IME_PHRASE_ON
 *               时阶段 6 追加词组演示（isReady 守卫双分支：ready=点
 *               nihao 上屏词组「你好」+6 字节，缺资产=单字「你」回
 *               退锁 +3 字节）；XVIRTUALKEYBOARD_ON 时追加阶段 7
 *               hints 自动切布局断言（数字框=digits 12 键/英文框=
 *               main 40 键锁 Latin/默认框回落 main）。
 * @note       文件所有权：本文件为页面自包含实现，不改动契约头、主文
 *             件、CMakeLists 与 Src/；页面内部控件指针存文件级 static
 *             结构（demo 单实例，build 登记、autotest 使用）。XKEYBOARD_ON
 *             或 XLINEEDIT_ON 关闭时整页降级：build 返回 NULL（主文件
 *             既有 NULL 分支跳过注册）、autotest 返回 0。
 * @author     XinYueC 团队
 ******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "XPrintf.h"
#include "xgui_demo_pages.h"

#if XWIDGET_ON && XKEYBOARD_ON && XLINEEDIT_ON

#include "XClass.h"
#include "XMemory.h"
#include "XObject.h"
#include "XEvent.h"
#include "XGuiApplication.h" /* virtualKeyboard()：页面迁移到默认面板单例。 */
#include "XVirtualKeyboard.h"
#include "XSystem.h" /* XSystem_environment：环境变量唯一入口。 */
#include "XLineEdit.h"
#include "XVarList.h"
#if XKEYBOARD_IME_ON
#include "XPinyinTable.h" /* 首候选对照（find 零拷贝借用）。 */
#endif
#if XKEYBOARD_IME_PHRASE_ON
#include "XPinyinPhrase.h" /* 词组演示（ready 守卫双分支）。 */
#endif
#if XVIRTUALKEYBOARD_ON
/* 虚拟键盘框架（组串/候选消费面 Qt 形态化；宏未注册时编译出）。 */
#include "XVirtualKeyboardInputContext.h"
#include "XVirtualKeyboardInputEngine.h"
#include "XVirtualKeyboardSelectionListModel.h"
#include "XString.h"
#include "XVariant.h"
#endif
#if XLABEL_ON && XFRAME_ON
#include "XLabel.h" /* 只读操作说明行（缺省回退只读 XLineEdit）。 */
#endif

/* ==================== 页面自持状态（demo 单实例） ==================== */

typedef struct KbdPageState
{
    XWidget* page;              /**< 页面根控件（主文件持有）。 */
    DemoPageStatusFn status;    /**< 主窗口状态栏回调（借用）。 */
    void* user;                 /**< 状态回调上下文（借用）。 */
    XLineEdit* editor;          /**< 默认框（无 hints，回落 main 布局；
                                     父链级联析构）。 */
    XLineEdit* editorDigits;    /**< 数字框（ImhDigitsOnly→digits 12
                                     键；父链级联析构）。 */
    XLineEdit* editorLatin;     /**< 英文框（ImhLatinOnly→main 锁
                                     Latin；父链级联析构）。 */
    XLineEdit* editor2;         /**< 第二编辑框（演示焦点切换重绑组串
                                     复位；父链级联析构）。 */
    XVirtualKeyboard* keyboard;        /**< 屏幕虚拟键盘（父链/宿主级联析构）。 */
    int returnPressedCount;     /**< 编辑框 returnPressed 信号计数。 */
    int readyCount;             /**< 键盘 ready 信号计数。 */
    int cancelCount;            /**< 键盘 cancel 信号计数。 */
} KbdPageState;

static KbdPageState s_kbd;

/* ==================== 信号槽（真实交互状态反馈） ==================== */

/** @brief 编辑框回车 → 状态栏反馈（真实键入链路的目验锚点）。 */
static void kbd_returnPressedSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_kbd.returnPressedCount;
    if (s_kbd.status)
        s_kbd.status(s_kbd.user, "键盘页：编辑框回车（returnPressed）");
}

/** @brief 键盘确认键 → 状态栏反馈（应用接线 closePopup 的挂点演示）。 */
static void kbd_readySlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_kbd.readyCount;
    if (s_kbd.status)
        s_kbd.status(s_kbd.user, "键盘页：确认（ready 信号）");
}

/** @brief 键盘关闭/收起键 → 状态栏反馈（cancel 后弹层自动收层）。 */
static void kbd_cancelSlot(XObject* receiver, XVarList* args)
{
    (void)receiver;
    (void)args;
    ++s_kbd.cancelCount;
    if (s_kbd.status)
        s_kbd.status(s_kbd.user, "键盘页：收起（cancel 信号）");
}

/* ==================== autotest 工具 ==================== */

/** @brief 按标签找按钮索引（未找到返回 -1）。 */
static int kbd_findButton(XVirtualKeyboard* kb, const char* label)
{
    uint32_t i;
    for (i = 0; i < XVirtualKeyboard_buttonCount(kb); ++i) {
        const char* t = XVirtualKeyboard_buttonText(kb, i);
        if (t && strcmp(t, label) == 0) return (int)i;
    }
    return -1;
}

/** @brief 向键盘直发合成鼠标按压+释放（XObject_event_base，与真实输入
 *         同路径；popovers=0 时字符键按压即触发，释放仅解除按压态）。 */
static void kbd_clickButton(XVirtualKeyboard* kb, int idx)
{
    XMouseEvent me;
    XPoint pos;
    if (idx < 0 || (uint32_t)idx >= XVirtualKeyboard_buttonCount(kb)) return;
    pos.x = kb->m_keyRects[idx].x + kb->m_keyRects[idx].width / 2;
    pos.y = kb->m_keyRects[idx].y + kb->m_keyRects[idx].height / 2;
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)kb, (XEvent*)&me);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)kb, (XEvent*)&me);
}

/** @brief 直点工具栏图标（菜单条第 slot 等分格中心——与绘制/命中同
 *         一居中公式，格心恒在图标热区内；press+release 走真实
 *         menuBarHit→图标激活路径，无头注入不直写私有状态）。 */
static void kbd_clickBarSlot(XVirtualKeyboard* kb, int slot)
{
    XMouseEvent me;
    XPoint pos;
    XRect bar = kb->m_menuBarRect;
    if (slot < 0 || slot > 3 || bar.width <= 0 || bar.height <= 0) return;
    pos.x = bar.x + bar.width * (2 * slot + 1) / 8;
    pos.y = bar.y + bar.height / 2;
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                     XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)kb, (XEvent*)&me);
    XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                     XMouseButton_LeftButton, 0, pos);
    XObject_event_base((XObject*)kb, (XEvent*)&me);
}

/* ==================== 组串/候选读取垫片（双世界随迁） ================ */

#if XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON
/** @brief 组串缓冲读取垫片：框架世界（XVIRTUALKEYBOARD_ON）面板
 *         m_ime 成员已按设计删除（apiMapping#8），组串状态镜像=
 *         context.preeditText()（拼音插件组串变化经 setPreeditText
 *         同步，apiMapping#13）；既有世界直读 m_ime.m_buffer。断言
 *         语义不变、路径更换。preeditText 返回新建 XString*（真头
 *         文件契约），取 UTF-8 拷入静态缓冲（组串容量 15 字节）后
 *         立即释放。 */
static const char* kbd_composeText(const XVirtualKeyboard* kb)
{
    static char s_buf[32];
    XString* s = XVirtualKeyboardInputContext_preeditText(
        XVirtualKeyboardInputContext_instance());
    const char* u = s ? XString_toUtf8(s) : NULL;
    (void)kb;
    if (u) {
        size_t i = 0;
        for (; i + 1 < sizeof(s_buf) && u[i] != '\0'; ++i)
            s_buf[i] = u[i];
        s_buf[i] = '\0';
    } else {
        s_buf[0] = '\0';
    }
    if (s) XClassDelete((XClass*)s);
    return s_buf;
}


/** @brief 首候选文本比对垫片：框架世界经 engine.wordCandidateListModel()
 *         dataAt(0, Display)（XVariantType_String 承载——设计
 *         apiMapping#3/4；门禁对齐：XVariant String 载荷内部形
 *         态按 XString* 假定，拼音插件落地后如不符按实际调整）；
 *         既有世界 XPinyinEngine_candidateAt 直读。 */
static bool kbd_firstCandidateIs(const XVirtualKeyboard* kb, const char* utf8)
{
    const XVirtualKeyboardInputContext* ctx =
        XVirtualKeyboardInputContext_instance();
    XVirtualKeyboardSelectionListModel* model =
        XVirtualKeyboardInputEngine_wordCandidateListModel(
            XVirtualKeyboardInputContext_inputEngine(ctx));
    XVariant* v =
        model ? XVirtualKeyboardSelectionListModel_dataAt(
                    model, 0,
                    XVirtualKeyboardSelectionListModelRole_Display)
              : NULL;
    bool ok = false;
    (void)kb;
    if (v && XVariant_data(v)) {
        const XString* s = (const XString*)XVariant_data(v);
        const char* u = s ? XString_toUtf8((XString*)s) : NULL;
        ok = (u != NULL && strcmp(u, utf8) == 0);
    }
    if (v) XClassDelete((XClass*)v);
    return ok;
}
#endif /* XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON */

/* ==================== 页面装配（契约接口） ======================== */

/** @brief 构建键盘页：编辑框在上、键盘 autoPopup 常备（初始隐藏）。 */
XWidget* demo_page_keyboard_build(XWidget* parent,
                                  DemoPageStatusFn status, void* user)
{
    XWidget* page;
    if (!parent) return NULL;
    page = XWidget_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, parent, 0);
    if (!page) return NULL;
    memset(&s_kbd, 0, sizeof(s_kbd));
    s_kbd.page = page;
    s_kbd.status = status;
    s_kbd.user = user;

    /* ---- 默认框（无 hints，回落 main 布局；StrongFocus 为控件构造
     *     默认）。 ---- */
    s_kbd.editor = XLineEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, page, 0);
    if (s_kbd.editor) {
        XWidget_setGeometry((XWidget*)s_kbd.editor, 12, 8, 420, 28);
        XLineEdit_setPlaceholderText(s_kbd.editor,
                                     "默认框：无提示位 → 键盘主布局（main）");
        XObject_connect_1((XObject*)s_kbd.editor,
                          (size_t)XLineEdit_returnPressed_signal(NULL),
                          (XObject*)page, kbd_returnPressedSlot,
                          XConnectionType_Direct);
        XWidget_show((XWidget*)s_kbd.editor);
    }

    /* ---- 数字框（ImhDigitsOnly → digits 12 键；hints 自动切键盘
     *     演示第一框，Keyboard.qml:42-49 优先级口径）。 ---- */
    s_kbd.editorDigits =
        XLineEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, page, 0);
    if (s_kbd.editorDigits) {
        XWidget_setGeometry((XWidget*)s_kbd.editorDigits, 12, 44, 420, 28);
        XWidget_setInputMethodHints((XWidget*)s_kbd.editorDigits,
                                    XInputMethodHint_DigitsOnly);
        XLineEdit_setPlaceholderText(s_kbd.editorDigits,
                                     "数字框（ImhDigitsOnly）→ 自动切 12 键数字盘");
        XWidget_show((XWidget*)s_kbd.editorDigits);
    }

    /* ---- 英文框（ImhLatinOnly → main 布局并锁定拉丁模式；hints 自
     *     动切键盘演示第二框）。 ---- */
    s_kbd.editorLatin =
        XLineEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, page, 0);
    if (s_kbd.editorLatin) {
        XWidget_setGeometry((XWidget*)s_kbd.editorLatin, 12, 80, 420, 28);
        XWidget_setInputMethodHints((XWidget*)s_kbd.editorLatin,
                                    XInputMethodHint_LatinOnly);
        XLineEdit_setPlaceholderText(s_kbd.editorLatin,
                                     "英文框（ImhLatinOnly）→ 主布局锁定拉丁");
        XWidget_show((XWidget*)s_kbd.editorLatin);
    }

    /* ---- 第二输入框（演示守护 tick 焦点跟随重绑时组串自动复位）。 ---- */
    s_kbd.editor2 = XLineEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, page, 0);
    if (s_kbd.editor2) {
        XWidget_setGeometry((XWidget*)s_kbd.editor2, 12, 116, 420, 28);
        XLineEdit_setPlaceholderText(s_kbd.editor2,
                                     "点击第二个输入框 → 键盘跟随重绑（拼音组串复位）");
        XWidget_show((XWidget*)s_kbd.editor2);
    }

    /* ---- 只读操作说明行（XLABEL_ON 门控；缺省回退只读 XLineEdit）。 ---- */
#if XLABEL_ON && XFRAME_ON
    {
        XLabel* hint = XLabel_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, page, 0);
        if (hint) {
            /* 高度按 heightForWidth 实算（对齐 QLabel::heightForWidth
             * 口径）：固定 40px 只装三行，第四行被裁半（2026-10-01 用
             * 户实机报告 D2，user-report-label-clip.png 实锚）；文本与
             * wordWrap 先置再算，宽 420 与几何一致。 */
            int hintH;
            XLabel_setText_2(hint,
                             "三框并列演示 hints 自动切键盘：默认框=主布局 / 数字框=12 键数字盘 / "
                             "英文框=主布局锁拉丁；拼音：中/EN 切换 → 字母上候选带 → "
                             "空格 / 点 chip / 1-9 上屏 → 回车=原字母 → 退格先删组串");
            XLabel_setWordWrap(hint, true);
            hintH = XLabel_heightForWidth(hint, 420);
            XWidget_setGeometry((XWidget*)hint, 12, 152, 420,
                                hintH > 0 ? hintH : 40);
            XWidget_show((XWidget*)hint);
        }
    }
#else
    {
        XLineEdit* hint = XLineEdit_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, page, 0);
        if (hint) {
            XWidget_setGeometry((XWidget*)hint, 12, 152, 420, 28);
            XLineEdit_setText(hint,
                              "默认/数字/英文三框自动切键盘；拼音：中/EN 切换 → 空格/1-9 上屏");
            XLineEdit_setReadOnly(hint, true);
            XWidget_show((XWidget*)hint);
        }
    }
#endif /* XLABEL_ON && XFRAME_ON */

    /* ---- XVirtualKeyboard：应用默认面板单例（XGuiApplication 拥有，
       随应用析构；不再自建）。迁移根因（双面板叠挂）：XWidget 按下位
       置驱动钩子（XGuiApplication_virtualKeyboardNotifyPress，PRESS
       汇聚点）对每次按下调默认单例（无条件惰性创建）——本页若仍
       create_ex 自建面板，进程内任意一次按下即诞生单例，点编辑框时
       单例即时弹 + 本页守护 ≤200ms 跟弹，双面板瞬态叠挂。单例化后按
       下路径与守护同源同实例：PRESS 命中编辑框即时弹（notifyPress）、
       守护轮询降为焦点跟随兜底，autoPopup 开启照旧。 ---- */
    s_kbd.keyboard = XGuiApplication_virtualKeyboard();
    if (s_kbd.keyboard) {
        XVirtualKeyboard_setAutoPopup(s_kbd.keyboard, true);
#if XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON
        /* 启用拼音 IME 并置英文态：User1 槽位载入拼音布局（「中/EN」
           键可见可玩），初始英文态保证既有 qwe 直写契约与「可被开关
           关闭」演示（设计 testPlan 口径）。 */
        XVirtualKeyboard_setImeEnabled(s_kbd.keyboard, true);
        XVirtualKeyboard_setImeChinese(s_kbd.keyboard, false);
#endif /* XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON */
        XObject_connect_1((XObject*)s_kbd.keyboard,
                          (size_t)XVirtualKeyboard_ready_signal(NULL),
                          (XObject*)page, kbd_readySlot,
                          XConnectionType_Direct);
        XObject_connect_1((XObject*)s_kbd.keyboard,
                          (size_t)XVirtualKeyboard_cancel_signal(NULL),
                          (XObject*)page, kbd_cancelSlot,
                          XConnectionType_Direct);
        /* 初始隐藏：单例顶层构造已 Hidden（XVirtualKeyboard.h 契约），
           显式补置降级为幂等兜底（页面重建/弹出态残留复位）——原注释
           的「子控件不带 WState_Hidden 随页面 show 传播画出白卡」根因
           随自建面板一并退场。弹出由 popup/守护轮询/按下位置驱动。 */
        XWidget_setVisible((XWidget*)s_kbd.keyboard, false);
    }
    return page;
}

/* ==================== 无头截图钩子（Tools/VirtualKeyboard/style_check.py 专用） ================ */

#if XWIDGET_ON && XKEYBOARD_ON && XLINEEDIT_ON

/** @brief 环境变量开关判定（值首字符=='1' 视为开；未设=关）。 */
static bool kbd_envFlag(const char* name)
{
    const char* v = XSystem_environment(name); /* 库内唯一环境入口（禁直呼 getenv）。 */
    return v && v[0] == '1';
}

/** @brief 令指定按钮进入按下保持态（直写武装位，不注入释放——按压
 *         配色/气泡是绘制期状态，无释放即可稳定截图）。 */
static void kbd_pressHold(XVirtualKeyboard* kb, int idx)
{
    if (idx < 0 || (uint32_t)idx >= XVirtualKeyboard_buttonCount(kb)) return;
    kb->m_pressedKey = (uint32_t)idx;
    kb->m_pressArmed = true;
    XWidget_update((XWidget*)kb);
}

/** @brief 弹出证据行（widget/band 两行）：弹层挂接态的最小 stdout 凭
 *         证——FAILED 行无条件打印，成功侧证据同权（外部探针门禁仅设
 *         XGUI_KB_AUTOSHOW 不设 DUMP，缺这两行会把弹出成功误判为未恢
 *         复）。widget 行用几何（父相对原点+尺寸，与截图同一坐标系），
 *         不用 XWidget_rect——那返回 m_contentsRect（控件自身局部，恒
 *         y=0），脚本像素取样按「widget 原点+键矩形偏移」走查会整体
 *         错位。 */
static void kbd_dumpGeometryBrief(XVirtualKeyboard* kb)
{
    XRect band = kb->m_imeBandRect;
    XPrintf("XKB-GEO widget x=%d y=%d w=%d h=%d\n",
            XWidget_x((XWidget*)kb), XWidget_y((XWidget*)kb),
            XWidget_width((XWidget*)kb), XWidget_height((XWidget*)kb));
    XPrintf("XKB-GEO band x=%d y=%d w=%d h=%d\n", band.x, band.y,
            band.width, band.height);
}

/** @brief 打印布局几何（XKB-GEO 前缀行；供脚本与 xkb_rebuildLayout
 *         边界式公式双簿比对——布局漂移在像素采样前即被截获）。 */
static void kbd_dumpGeometry(XVirtualKeyboard* kb)
{
    uint32_t i;
    int rows = 0;
    kbd_dumpGeometryBrief(kb);
    for (i = 0; i < XVirtualKeyboard_buttonCount(kb); ++i) {
        const XRect* kr = &kb->m_keyRects[i];
        if (rows == 0 || kr->y != kb->m_keyRects[i - 1].y) ++rows;
        XPrintf("XKB-GEO key i=%u ctrl=%d x=%d y=%d w=%d h=%d label=%s\n",
                (unsigned)i, (int)kb->m_keyCtrls[i], kr->x, kr->y,
                kr->width, kr->height,
                XVirtualKeyboard_buttonText(kb, i) ? XVirtualKeyboard_buttonText(kb, i) : "");
    }
    XPrintf("XKB-GEO rows=%d buttons=%u\n", rows,
            (unsigned)XVirtualKeyboard_buttonCount(kb));
    /* 状态行（外部探针按行前缀解析）：popped=弹层挂接态、ime=拼音插件
       装载镜像、chinese=引擎输入模式是否 Pinyin（面板中文态判据同源）、
       composing=组串非空（kbd_composeText 垫片）、h=控件当前高——组串
       收缩态即候选带一条高（需求③证据行）、phys=物理键盘组串会话标
       记（需求②③输入源判据：仅外置键盘输入收缩/收层，屏幕键输入全
       量面板）。 */
    {
        int chinese = 0;
        int composing = 0;
        int phys = 0;
        int cand = 0;
        int pageCount = 1;
#if XVIRTUALKEYBOARD_ON
        {
            const XVirtualKeyboardInputContext* ctx =
                XVirtualKeyboardInputContext_instance();
            XVirtualKeyboardInputEngine* eng =
                XVirtualKeyboardInputContext_inputEngine(ctx);
            chinese = (eng && XVirtualKeyboardInputEngine_inputMode(eng) ==
                                (int)XVirtualKeyboardInputEngineInputMode_Pinyin)
                          ? 1
                          : 0;
        }
        phys = kb->m_physKeyActive ? 1 : 0;
#endif
#if XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON
        composing = kbd_composeText(kb)[0] != '\0';
#endif
#if XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON
        {
            const XVirtualKeyboardInputContext* ctx2 =
                XVirtualKeyboardInputContext_instance();
            XVirtualKeyboardInputEngine* eng2 =
                XVirtualKeyboardInputContext_inputEngine(ctx2);
            XVirtualKeyboardSelectionListModel* mdl = eng2
                ? XVirtualKeyboardInputEngine_wordCandidateListModel(eng2)
                : NULL;
            cand = mdl ? (int)XVirtualKeyboardSelectionListModel_count(mdl)
                       : 0;
        }
        {
            int pageSize = kb->m_candidatePageSize > 0
                               ? kb->m_candidatePageSize
                               : 1;
            pageCount = (cand + pageSize - 1) / pageSize;
            if (pageCount < 1) pageCount = 1;
        }
#endif
        XPrintf("XKB-GEO state popped=%d ime=%d chinese=%d composing=%d "
                "phys=%d h=%d cand=%d pg=%d/%d\n",
                kb->m_popped ? 1 : 0, kb->m_imeEnabled ? 1 : 0, chinese,
                composing, phys, XWidget_height((XWidget*)kb), cand,
                kb->m_candidatePage + 1, pageCount);
    }
}

void demo_page_keyboard_headless_hook(void)
{
    const char* env;
    if (!s_kbd.page || !s_kbd.editor || !s_kbd.keyboard) return;
    if (!kbd_envFlag("XGUI_KB_AUTOSHOW")) return;
    /* 弹出（挂宿主顶层窗口底部，与真机「点编辑框自动弹出」同一函数
       路径；焦点同步落编辑框，hints 映射与真机一致）。 */
    XWidget_setFocus((XWidget*)s_kbd.editor);
    XVirtualKeyboard_popup(s_kbd.keyboard, (XWidget*)s_kbd.editor);
    /* 失败判定用弹层挂接状态 m_popped，不用 popupVisible：本钩子在宿
       主窗口 show() 之前同步运行（xgui_window_demo.c 事件循环前调用
       点），popupVisible→XWidget_effectiveVisible 要求父链 m_visible，
       宿主未 show 恒 false——会把挂接成功误报为 FAILED 并提前返回
       （dump 与场景注入全被跳过，风格检查动态层全空）。m_popped 由
       XVirtualKeyboard_popup 成功路径（挂宿主+重定位+show）置位、closePopup
       复位，pre-show 时刻即反应真实挂接结果。 */
    if (!s_kbd.keyboard->m_popped) {
        XPrintf("XKB-GEO popup FAILED\n");
        return;
    }
    /* 气泡场景：内置 TextLower 布局 + popovers 开（拼音表无 POPOVER
       位，气泡只在内置布局存在）。 */
    env = XSystem_environment("XGUI_KB_MODE");
    if (env && strcmp(env, "textlower") == 0) {
        XVirtualKeyboard_setPopovers(s_kbd.keyboard, true);
        XVirtualKeyboard_setMode(s_kbd.keyboard, XKeyboardMode_TextLower);
    }
    /* 中文态候选带（bandH=contentH/(rows+1) 预留顶行；setImeChinese
       声明受 XVIRTUALKEYBOARD_ON 门控，与页面 build 同组合门控）。 */
#if XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON
    if (kbd_envFlag("XGUI_KB_CHINESE")) {
        /* 默认框（ImhNone）弹出现按 hints 对称映射回落 main（TextLower），
           而候选带/组串拦截仅 User1 模式生效——中文态场景显式回拼音槽
           位再切中文（等效真机「点中/EN 进拼音」的落态，缺陷修复前的
           弹出即 User1 依赖不再成立）。 */
        XVirtualKeyboard_setMode(s_kbd.keyboard, XKeyboardMode_User1);
        XVirtualKeyboard_setImeChinese(s_kbd.keyboard, true);
    }
    /* 款型注入（视觉验收补充无头路径：九键/英文全键此前仅
     * setLayoutKind 可达）：t9=拼音九键、english=英文全键、其余值=回
     * 拼音全键。置于 CHINESE/COMPOSE/PHYSKEY 之前使「CHINESE+LAYOUT+
     * COMPOSE」可组合（九键表先装、组串/多击注入才点得到组键——
     * COMPOSE 与 PHYSKEY 早于 LAYOUT 时注入落在旧表上为 no-op）；
     * 切款后镜像键盘选择面板的落位口径——IME 启用且停 User1 槽位
     * （build 默认落点）时随行落 TextLower，款型主表即刻可见。 */
    env = XSystem_environment("XGUI_KB_LAYOUT");
    if (env && env[0]) {
        XKeyboardLayoutKind kind = XKeyboardLayout_PinyinFull;
        if (strcmp(env, "t9") == 0)
            kind = XKeyboardLayout_PinyinT9;
        else if (strcmp(env, "english") == 0)
            kind = XKeyboardLayout_EnglishFull;
        XVirtualKeyboard_setLayoutKind(s_kbd.keyboard, kind);
#if XVIRTUALKEYBOARD_ON
        if (s_kbd.keyboard->m_imeEnabled &&
            s_kbd.keyboard->m_mode == XKeyboardMode_User1)
            XVirtualKeyboard_setMode(s_kbd.keyboard,
                                     XKeyboardMode_TextLower);
#endif
    }
    /* 组串注入：逐字符直点同名字符键（与真机点键同一事件路径）；九键
       组键标签为多字母（"ABC"…），单字符未命中且为 '2'..'9' 时按数字
       组口径换算组标签重试——九键多击注入（6444=拼 mi）同路径可达。 */
    env = XSystem_environment("XGUI_KB_COMPOSE");
    if (env && env[0]) {
        int i;
        for (i = 0; env[i]; ++i) {
            char label[2];
            int idx;
            label[0] = env[i];
            label[1] = '\0';
            idx = kbd_findButton(s_kbd.keyboard, label);
            if (idx < 0 && env[i] >= '2' && env[i] <= '9') {
                static const char* const kGroups[8] = {
                    "ABC", "DEF", "GHI", "JKL",
                    "MNO", "PQRS", "TUV", "WXYZ"
                };
                idx = kbd_findButton(s_kbd.keyboard,
                                     kGroups[env[i] - '2']);
            }
            kbd_clickButton(s_kbd.keyboard, idx);
        }
    }
    /* 候选带翻页注入（真实命中路径）：BANDPAGE=next/prev → 组串带右
       端 ">" / "<" 热区中心注入鼠标 press+release——menuBarHit→
       BandHit→翻页/消费链与真机点按同路径（热区常量 24/2 与
       XVirtualKeyboard.c 布局口径对齐，改值需同步）。 */
    env = XSystem_environment("XGUI_KB_BANDPAGE");
    if (env && env[0] && s_kbd.keyboard->m_imeBandRect.height > 0) {
        XMouseEvent me;
        XPoint pos;
        pos.x = s_kbd.keyboard->m_imeBandRect.x +
                s_kbd.keyboard->m_imeBandRect.width - 2 - 24 / 2;
        if (strcmp(env, "prev") == 0) pos.x -= 2 * 24;
        pos.y = s_kbd.keyboard->m_imeBandRect.y +
                s_kbd.keyboard->m_imeBandRect.height / 2;
        XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_PRESS,
                         XMouseButton_LeftButton, 0, pos);
        XObject_event_base((XObject*)s_kbd.keyboard, (XEvent*)&me);
        XMouseEvent_init(&me, XEVENT_TYPE_MOUSE_BUTTON_RELEASE,
                         XMouseButton_LeftButton, 0, pos);
        XObject_event_base((XObject*)s_kbd.keyboard, (XEvent*)&me);
    }
#endif /* XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON */
    /* 物理按键注入（真路径验收）：逐字符经应用层入口
       XGuiApplication_virtualKeyboardNotifyKey——XWidget_dispatchKeyEvent
       调用的物理按键转化层入口，与真机按键同链（组串进字/数字选候选/
       英文直落+自动收层语义全由此承载，需求①②③联动证据）。键映射
       口径：小写=ASCII 本值、大写=ASCII+Shift、数字=ASCII 本值、空格=
       XKey_Space，其余字符不在口径内跳过。置于 CHINESE/COMPOSE 之后可
       组合（CHINESE=1+PHYSKEY=ni2：物理键进组串 ni→2 提交上屏）。 */
    env = XSystem_environment("XGUI_KB_PHYSKEY");
    if (env && env[0]) {
        int i;
        for (i = 0; env[i]; ++i) {
            int key;
            XKeyboardModifiers mods = XKeyboardModifier_NoModifier;
            char c = env[i];
            if (c >= 'a' && c <= 'z') {
                key = (int)c;
            } else if (c >= 'A' && c <= 'Z') {
                key = (int)c;
                mods = XKeyboardModifier_ShiftModifier;
            } else if (c >= '0' && c <= '9') {
                key = (int)c;
            } else if (c == ' ') {
                key = XKey_Space;
            } else {
                continue; /* 口径外字符（符号等）不注入。 */
            }
            XGuiApplication_virtualKeyboardNotifyKey(key, mods);
        }
        XPrintf("XKB-GEO phys text=%s\n", XLineEdit_text(s_kbd.editor));
    }
    /* 按下保持（不注入释放：按压态/气泡稳定成帧）。 */
    env = XSystem_environment("XGUI_KB_PRESS_LABEL");
    if (env && env[0])
        kbd_pressHold(s_kbd.keyboard, kbd_findButton(s_kbd.keyboard, env));
    /* 工具栏面板/悬浮注入（视觉验收补充无头路径：选择面板/文字编辑
     * 面板/紧凑悬浮此前仅工具栏图标可达）：selector=键盘选择面板、
     * edit=文字编辑面板、float=紧凑悬浮小键盘——均直点对应工具栏图
     * 标（真实 menuBarHit→图标激活路径，不直写私有状态）；紧凑态转独
     * 立顶层 Popup 落宿主右下角，XKB-GEO widget 行即其全局坐标（截图
     * 定位用），panel 证据行供脚本断言注入生效。 */
    env = XSystem_environment("XGUI_KB_PANEL");
    if (env && env[0]) {
        if (strcmp(env, "selector") == 0)
            kbd_clickBarSlot(s_kbd.keyboard, 1);
        else if (strcmp(env, "edit") == 0)
            kbd_clickBarSlot(s_kbd.keyboard, 2);
        else if (strcmp(env, "float") == 0)
            kbd_clickBarSlot(s_kbd.keyboard, 0);
        XPrintf("XKB-GEO panel selector=%d edit=%d compact=%d\n",
                s_kbd.keyboard->m_layoutSelectorOpen ? 1 : 0,
                s_kbd.keyboard->m_editPanelOpen ? 1 : 0,
                s_kbd.keyboard->m_compactFloat ? 1 : 0);
    }
    if (kbd_envFlag("XGUI_KB_CLOSE")) {
        XVirtualKeyboard_closePopup(s_kbd.keyboard);
        /* closePopup 已改为无条件隐藏（pre-show 收层同样补置
           WState_Hidden，不再有「isVisible 护栏跳过隐藏→show 位残留、
           宿主 show 后整块画出」的 closed 场景残影），此处的显式补清
           降级为幂等兜底（对旧框架二进制仍成立），使截图落到「收起
           后」状态。语义复位（m_popped/组串引擎/契约连接）由
           closePopup 完成。 */
        XWidget_setVisible((XWidget*)s_kbd.keyboard, false);
    }
    /* 证据行门控：DUMP=1 走全量键位几何（style_check 双簿比对与像素
       取样坐标系）；仅 AUTOSHOW（外部探针门禁形态）也有 widget/band
       弹出证据行。收层场景（CLOSE 且未设 DUMP）不印——证据行描述
       弹层挂接态，m_popped 已复位即无证据可呈（DUMP=1 全量快照照印
       契约不变，closed 场景像素走查依赖之）。 */
    if (kbd_envFlag("XGUI_KB_DUMP"))
        kbd_dumpGeometry(s_kbd.keyboard);
    else if (s_kbd.keyboard->m_popped)
        kbd_dumpGeometryBrief(s_kbd.keyboard);
}

XWidget* demo_page_keyboard_screenshot_target(void)
{
    /* 紧凑悬浮态：键盘=独立顶层 Popup，不在主窗 paintImage 内——返回
       键盘自身让主文件截其背后图像（尺寸=紧凑矩形）；其余状态 NULL
       （主窗口径）。 */
    if (!s_kbd.keyboard || !s_kbd.keyboard->m_compactFloat) return NULL;
    return (XWidget*)s_kbd.keyboard;
}

#else /* !(XWIDGET_ON && XKEYBOARD_ON && XLINEEDIT_ON) */

void demo_page_keyboard_headless_hook(void)
{
    /* 开关关闭降级：零操作（页面 build 亦返回 NULL，钩子无实例）。 */
}

XWidget* demo_page_keyboard_screenshot_target(void)
{
    return NULL; /* 开关关闭降级：无键盘实例。 */
}

#endif /* XWIDGET_ON && XKEYBOARD_ON && XLINEEDIT_ON */

/* ==================== 页面自测（契约接口） ======================== */

int demo_page_keyboard_autotest(XWidget* page)
{
    int failures = 0;
    XWidget* top;
    int kbH = 0;

/* 断言输出定式（与 advanced 页 ADV_EXPECT 一致）。 */
#define KBD_EXPECT(cond, what)                                              \
    do {                                                                    \
        if (cond) XPrintf("XGuiAutoTest: [PASS] %s\n", what);               \
        else { XPrintf("XGuiAutoTest: [FAIL] %s\n", what); ++failures; }    \
    } while (0)

    if (!s_kbd.page || page != (XWidget*)s_kbd.page) return -1;
    if (!s_kbd.editor || !s_kbd.keyboard) {
        KBD_EXPECT(false, "XVirtualKeyboard 页实例缺失");
        return failures;
    }

    /* ---- -1. 守护常驻回归锁（评审修复）：autoPopup 默认开，构造即
     *     常驻轮询——不依赖首次 popup() 拉起（demo 进程有事件调度器，
     *     timer id 断言可靠；无头回归测试无调度器不做此断言）。
     *     2026-10-03：远程客户端页 build 关断单例 autoPopup（RC 页经
     *     悬浮条键盘钮全权接管），本页 autotest 显式恢复开态再断言
     *     ——守护断言锚定本页契约，不受其他页面开关时序影响。 ---- */
    XVirtualKeyboard_setAutoPopup(s_kbd.keyboard, true);
    KBD_EXPECT(s_kbd.keyboard->m_autoPopup &&
                   s_kbd.keyboard->m_guardTimer != XTIMER_INVALID_ID,
               "构造后守护轮询常驻（autoPopup 默认开，不依赖首次 popup）");

    /* ---- 0. popup：挂编辑框顶层窗口底部，宽==宿主宽、贴底。 ---- */
    XWidget_setFocus((XWidget*)s_kbd.editor);
    XVirtualKeyboard_popup(s_kbd.keyboard, (XWidget*)s_kbd.editor);
    top = XWidget_topLevelWidget((XWidget*)s_kbd.editor);
    KBD_EXPECT(top != NULL && XVirtualKeyboard_popupVisible(s_kbd.keyboard),
               "XVirtualKeyboard popup 后弹层可见");
    if (top && XVirtualKeyboard_popupVisible(s_kbd.keyboard)) {
        kbH = XVirtualKeyboard_height(s_kbd.keyboard);
        KBD_EXPECT(XVirtualKeyboard_width(s_kbd.keyboard) == XWidget_width(top),
                   "弹层宽==宿主顶层窗口宽");
        KBD_EXPECT(XVirtualKeyboard_y(s_kbd.keyboard) ==
                       XWidget_height(top) - kbH,
                   "弹层贴宿主底部（y==hostH-kbH）");
        KBD_EXPECT(kbH == XWidget_height(top) / 2,
                   "弹层高按 hostH/2 钳位（600→300）");
        KBD_EXPECT(XWidget_hasFocus((XWidget*)s_kbd.editor),
                   "弹出后焦点保持在编辑框（键盘 NoFocus）");
    }

    /* ---- 1. 合成点击 q w e → 编辑框 text=="qwe"。 ---- */
    {
        int qIdx = kbd_findButton(s_kbd.keyboard, "q");
        int wIdx = kbd_findButton(s_kbd.keyboard, "w");
        int eIdx = kbd_findButton(s_kbd.keyboard, "e");
        KBD_EXPECT(qIdx >= 0 && wIdx >= 0 && eIdx >= 0,
                   "小写布局可定位 q/w/e 键");
        kbd_clickButton(s_kbd.keyboard, qIdx);
        kbd_clickButton(s_kbd.keyboard, wIdx);
        kbd_clickButton(s_kbd.keyboard, eIdx);
        KBD_EXPECT(strcmp(XLineEdit_text(s_kbd.editor), "qwe") == 0,
                   "点键 q w e 写入 qwe");
    }

    /* ---- 2. 退格 → "qw"；<- → 光标断言；-> 复位；换行 → 信号计数。 ---- */
    {
        int bsIdx = kbd_findButton(s_kbd.keyboard, XKEYBOARD_LBL_BACKSPACE);
        int leftIdx = kbd_findButton(s_kbd.keyboard, XKEYBOARD_LBL_LEFT);
        int rightIdx = kbd_findButton(s_kbd.keyboard, XKEYBOARD_LBL_RIGHT);
        int nlIdx = kbd_findButton(s_kbd.keyboard, XKEYBOARD_LBL_NEWLINE);
        kbd_clickButton(s_kbd.keyboard, bsIdx);
        KBD_EXPECT(strcmp(XLineEdit_text(s_kbd.editor), "qw") == 0,
                   "点退格删除末字符得 qw");
        kbd_clickButton(s_kbd.keyboard, leftIdx);
        KBD_EXPECT(XLineEdit_cursorPosition(s_kbd.editor) == 1,
                   "点 <- 光标到 1");
        kbd_clickButton(s_kbd.keyboard, rightIdx);
        KBD_EXPECT(XLineEdit_cursorPosition(s_kbd.editor) == 2,
                   "点 -> 光标回末尾");
        kbd_clickButton(s_kbd.keyboard, nlIdx);
        KBD_EXPECT(s_kbd.returnPressedCount == 1,
                   "点换行触发 returnPressed 信号计数 1");
    }

    /* ---- 3. 大写布局点 Q → 大写断言（Shift 修饰位契约）。 ---- */
    {
        XVirtualKeyboard_setMode(s_kbd.keyboard, XKeyboardMode_TextUpper);
        KBD_EXPECT(XVirtualKeyboard_mode(s_kbd.keyboard) ==
                       XKeyboardMode_TextUpper,
                   "setMode 切大写布局");
        {
            int qIdx = kbd_findButton(s_kbd.keyboard, "Q");
            kbd_clickButton(s_kbd.keyboard, qIdx);
        }
        KBD_EXPECT(strcmp(XLineEdit_text(s_kbd.editor), "qwQ") == 0,
                   "大写布局点 Q 输出大写 Q");
    }

    /* ---- 4. closePopup → 弹层不可见；autoPopup 开时守护常驻轮询不
     *     随层停（收层后焦点再落编辑框仍自动弹出，评审修复回归锁）。 ---- */
    XVirtualKeyboard_closePopup(s_kbd.keyboard);
    KBD_EXPECT(!XVirtualKeyboard_popupVisible(s_kbd.keyboard),
               "closePopup 后弹层不可见");
    KBD_EXPECT(s_kbd.keyboard->m_autoPopup &&
                   s_kbd.keyboard->m_guardTimer != XTIMER_INVALID_ID,
               "closePopup 后守护轮询仍常驻（autoPopup 开不随层停）");

    /* ---- 5. autoPopup 点击弹出联动不做同步断言：守护轮询 200ms tick
     *     在帧 3 同步 autotest 序列不保证到达（调度点
     *     xgui_window_demo.c demo_ext_pages_autotest），留真键盘目验
     *     （真触摸点击编辑框弹出、长按退格重复、气泡放大、四模式切换、
     *     收起后物理键入直达编辑框）。 ---- */

#if XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON
    /* ---- 6. IME 端到端（中文态组串拦截 → 候选带 → 空格上屏 → 切回
     *     英文直写 → 换框重绑组串复位；面板 IME API 已随框架化挂
     *     XVIRTUALKEYBOARD_ON 门控，框架未开时本段整体编译出）。 ---- */
    {
        const XPinyinTableEntry* begin = NULL;
        uint16_t cnt = 0;
        const char* firstNi = NULL;
        /* 阶段 4 closePopup 按契约解绑了输出目标（收层即解绑，框架化
           后保持——closePopup 同时断开 commitRequested/keyEventReq
           uested 二信号落地），重绑 editor（setTextArea/popup 重连）
           等价真实 UX 的「点编辑框弹出」输入上下文——否则空格上屏/
           英文直写全部在写入链的 m_target==NULL 守卫处自然落空。守护
           边沿语义下重弹由焦点边沿驱动，本页直呼 popup 不受抑制态拦
           截（XVirtualKeyboard_popup 保底契约）。 */
        XVirtualKeyboard_setTextArea(s_kbd.keyboard, (XWidget*)s_kbd.editor);
        /* 回 User1 拼音槽位（阶段 3 留在 TextUpper，IME 拦截仅在
           User1 模式生效）。 */
        XVirtualKeyboard_setMode(s_kbd.keyboard, XKeyboardMode_User1);
        XPinyinTable_find("ni", &begin, &cnt);
        if (begin && cnt > 0) firstNi = begin[0].m_utf8;

        KBD_EXPECT(XVirtualKeyboard_setImeChinese(s_kbd.keyboard, true) &&
                       XVirtualKeyboard_imeChinese(s_kbd.keyboard),
                   "切中文态成功");
        KBD_EXPECT(s_kbd.keyboard->m_imeBandRect.height > 0,
                   "中文态候选带预留（m_imeBandRect.height>0）");
        {
            int nIdx = kbd_findButton(s_kbd.keyboard, "n");
            int iIdx = kbd_findButton(s_kbd.keyboard, "i");
            KBD_EXPECT(nIdx >= 0 && iIdx >= 0, "拼音布局可定位 n/i 键");
            kbd_clickButton(s_kbd.keyboard, nIdx);
            kbd_clickButton(s_kbd.keyboard, iIdx);
        }
        KBD_EXPECT(strcmp(XLineEdit_text(s_kbd.editor), "qwQ") == 0,
                   "组串拦截：字母不直写编辑框");
        KBD_EXPECT(strcmp(kbd_composeText(s_kbd.keyboard), "ni") == 0,
                   "组串缓冲为 ni（框架世界=preeditText 镜像）");
        {
            int spIdx = kbd_findButton(s_kbd.keyboard, " ");
            kbd_clickButton(s_kbd.keyboard, spIdx);
        }
        KBD_EXPECT(firstNi != NULL &&
                       strlen(XLineEdit_text(s_kbd.editor)) == 6 &&
                       strncmp(XLineEdit_text(s_kbd.editor), "qwQ", 3) == 0 &&
                       strcmp(XLineEdit_text(s_kbd.editor) + 3, firstNi) == 0,
                   "空格提交全局首候选（qwQ+ni 首条目，3 字节增长）");
#if XKEYBOARD_IME_PHRASE_ON
        /* ---- 词组演示（isReady() 守卫双分支；缺资产象限由红改绿且
         *     语义有意义——ready=词组上屏，缺资产=单字回退锁）。 ---- */
        if (XPinyinPhrase_isReady()) {
            const char* tail;
            kbd_clickButton(s_kbd.keyboard,
                            kbd_findButton(s_kbd.keyboard, "n"));
            kbd_clickButton(s_kbd.keyboard,
                            kbd_findButton(s_kbd.keyboard, "i"));
            kbd_clickButton(s_kbd.keyboard,
                            kbd_findButton(s_kbd.keyboard, "h"));
            kbd_clickButton(s_kbd.keyboard,
                            kbd_findButton(s_kbd.keyboard, "a"));
            kbd_clickButton(s_kbd.keyboard,
                            kbd_findButton(s_kbd.keyboard, "o"));
            KBD_EXPECT(strcmp(kbd_composeText(s_kbd.keyboard), "nihao") == 0,
                       "INV2 组串 nihao（词组演示，V1 组不出跨音节串）");
            KBD_EXPECT(kbd_firstCandidateIs(s_kbd.keyboard,
                                            "\xE4\xBD\xA0\xE5\xA5\xBD"),
                       "首候选 chip=你好（词组前单字垫后；框架世界=候选"
                       "模型 dataAt(0)）");
            kbd_clickButton(s_kbd.keyboard,
                            kbd_findButton(s_kbd.keyboard, " "));
            tail = XLineEdit_text(s_kbd.editor);
            KBD_EXPECT((int)strlen(tail) >= 6 &&
                           strcmp(tail + strlen(tail) - 6,
                                  "\xE4\xBD\xA0\xE5\xA5\xBD") == 0,
                       "空格上屏词组你好（恰 +6 字节，m_commit 扩容）");
        } else {
            const char* tail;
            kbd_clickButton(s_kbd.keyboard,
                            kbd_findButton(s_kbd.keyboard, "n"));
            kbd_clickButton(s_kbd.keyboard,
                            kbd_findButton(s_kbd.keyboard, "i"));
            KBD_EXPECT(kbd_firstCandidateIs(s_kbd.keyboard, "\xE4\xBD\xA0"),
                       "缺资产回退：首候选=你（首路单字）");
            kbd_clickButton(s_kbd.keyboard,
                            kbd_findButton(s_kbd.keyboard, " "));
            tail = XLineEdit_text(s_kbd.editor);
            KBD_EXPECT((int)strlen(tail) >= 3 &&
                           strcmp(tail + strlen(tail) - 3,
                                  "\xE4\xBD\xA0") == 0,
                       "缺资产回退：空格上屏你（恰 +3 字节）");
        }
#endif /* XKEYBOARD_IME_PHRASE_ON */
        {
            int imeIdx = kbd_findButton(s_kbd.keyboard, XKEYBOARD_LBL_IME);
            int qIdx = kbd_findButton(s_kbd.keyboard, "q");
            int lenBefore = (int)strlen(XLineEdit_text(s_kbd.editor));
            KBD_EXPECT(imeIdx >= 0, "拼音布局可定位中/EN 键");
            kbd_clickButton(s_kbd.keyboard, imeIdx);
            KBD_EXPECT(!XVirtualKeyboard_imeChinese(s_kbd.keyboard),
                       "点中/EN 切回英文态");
            kbd_clickButton(s_kbd.keyboard, qIdx);
            {
                const char* t = XLineEdit_text(s_kbd.editor);
                KBD_EXPECT((int)strlen(t) == lenBefore + 1 &&
                               t[lenBefore] == 'q',
                           "英文态点 q 直写编辑框（恰追加 1 字节 'q'）");
            }
        }
        /* 换框重绑复位：组串中 popup 第二编辑框（同步 setTextArea 路
           径）→ 组串复位。 */
        XVirtualKeyboard_setImeChinese(s_kbd.keyboard, true);
        kbd_clickButton(s_kbd.keyboard,
                        kbd_findButton(s_kbd.keyboard, "n"));
        KBD_EXPECT(kbd_composeText(s_kbd.keyboard)[0] == 'n',
                   "前置：组串中（n）");
        if (s_kbd.editor2) {
            XWidget_setFocus((XWidget*)s_kbd.editor2);
            XVirtualKeyboard_popup(s_kbd.keyboard, (XWidget*)s_kbd.editor2);
        }
        KBD_EXPECT(kbd_composeText(s_kbd.keyboard)[0] == '\0',
                   "换框重绑后组串复位（换框残留拼音风险回归锁；框架"
                   "世界=engine reset→preeditText 空）");
        XVirtualKeyboard_closePopup(s_kbd.keyboard);
    }
#endif /* XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON */

#if XVIRTUALKEYBOARD_ON
    /* ---- 7. hints 自动切键盘三框演示（XVIRTUALKEYBOARD_ON 门控；
     *     数字框 ImhDigitsOnly / 英文框 ImhLatinOnly / 默认框并列，
     *     Keyboard.qml:42-49 优先级口径；popup 消费焦点控件 hints
     *     ——经 XInputMethod_defaultQueryHandler 查询桥取 ImHints，
     *     故逐框先 setFocus 再 popup）。守护轮询 200ms tick 在帧 3
     *     同步序列不保证到达，本段直呼 popup 驱动（保底契约）。 ---- */
    if (s_kbd.editorDigits && s_kbd.editorLatin) {
        XWidget_setInputMethodHints((XWidget*)s_kbd.editorDigits,
                                    XInputMethodHint_DigitsOnly);
        XWidget_setFocus((XWidget*)s_kbd.editorDigits);
        XVirtualKeyboard_popup(s_kbd.keyboard, (XWidget*)s_kbd.editorDigits);
        KBD_EXPECT(XVirtualKeyboard_buttonCount(s_kbd.keyboard) == 12,
                   "数字框（ImhDigitsOnly）自动切 digits 12 键");
        KBD_EXPECT(kbd_findButton(s_kbd.keyboard, "0") >= 0 &&
                       kbd_findButton(s_kbd.keyboard, "9") >= 0,
                   "digits 盘含 0..9 键帽");
        XVirtualKeyboard_closePopup(s_kbd.keyboard);
        XWidget_setInputMethodHints((XWidget*)s_kbd.editorLatin,
                                    XInputMethodHint_LatinOnly);
        XWidget_setFocus((XWidget*)s_kbd.editorLatin);
        XVirtualKeyboard_popup(s_kbd.keyboard, (XWidget*)s_kbd.editorLatin);
        KBD_EXPECT(XVirtualKeyboard_buttonCount(s_kbd.keyboard) == 40 &&
                       XVirtualKeyboard_mode(s_kbd.keyboard) ==
                           XKeyboardMode_TextLower,
                   "英文框（ImhLatinOnly）自动切 main 40 键并锁 Latin"
                   "（TextLower）");
        XVirtualKeyboard_closePopup(s_kbd.keyboard);
        XWidget_setFocus((XWidget*)s_kbd.editor);
        XVirtualKeyboard_popup(s_kbd.keyboard, (XWidget*)s_kbd.editor);
        KBD_EXPECT(XVirtualKeyboard_buttonCount(s_kbd.keyboard) == 40 &&
                       XVirtualKeyboard_mode(s_kbd.keyboard) ==
                           XKeyboardMode_TextLower,
                   "默认框（无 hints）回落 main 布局（hints 自动切回；"
                   "LatinOnly→ImhNone→main 回落锁）");
        XVirtualKeyboard_closePopup(s_kbd.keyboard);
        /* 回归锁（用户实测 main→digits→main 数字盘残留缺陷）：main 布局
           →数字框→默认框全序列，布局随目标对称往返——ImhNone 重绑方向
           必须回落 main，数字盘不得残留；IME 能力保留（imeEnabled 不随
           重绑翻转，拼音可用），引擎输入模式按 build 口径回英文态。 */
        XWidget_setFocus((XWidget*)s_kbd.editorDigits);
        XVirtualKeyboard_popup(s_kbd.keyboard, (XWidget*)s_kbd.editorDigits);
        KBD_EXPECT(XVirtualKeyboard_buttonCount(s_kbd.keyboard) == 12,
                   "回归锁前序：数字框（ImhDigitsOnly）再切 12 键数字盘");
        XVirtualKeyboard_closePopup(s_kbd.keyboard);
        XWidget_setFocus((XWidget*)s_kbd.editor);
        XVirtualKeyboard_popup(s_kbd.keyboard, (XWidget*)s_kbd.editor);
        KBD_EXPECT(XVirtualKeyboard_buttonCount(s_kbd.keyboard) == 40 &&
                       XVirtualKeyboard_mode(s_kbd.keyboard) ==
                           XKeyboardMode_TextLower,
                   "回归锁：默认框重绑回落 main（main→digits→main 往返，"
                   "数字盘不残留）");
        KBD_EXPECT(XVirtualKeyboard_imeEnabled(s_kbd.keyboard),
                   "回归锁：ImhNone 重绑不翻转 IME 能力（拼音可用）");
        KBD_EXPECT(XVirtualKeyboardInputEngine_inputMode(
                       XVirtualKeyboardInputContext_inputEngine(
                           XVirtualKeyboardInputContext_instance())) ==
                       (int)XVirtualKeyboardInputEngineInputMode_Latin,
                   "回归锁：ImhNone 重绑引擎回英文态（Latin，中文可重进）");
        XVirtualKeyboard_closePopup(s_kbd.keyboard);
        /* 三框 hints 保留（页面演示态：真键盘点框即自动切）。 */
    } else {
        KBD_EXPECT(false, "三框 hints 演示控件缺失");
    }
#endif /* XVIRTUALKEYBOARD_ON */

    /* 恢复交互演示初始态：IME 启用时回 User1+英文态（保「中」键可见
       可玩）；否则回小写布局（既有口径）。 */
#if XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON
    XVirtualKeyboard_setImeChinese(s_kbd.keyboard, false);
    XVirtualKeyboard_setMode(s_kbd.keyboard, XKeyboardMode_User1);
#else
    XVirtualKeyboard_setMode(s_kbd.keyboard, XKeyboardMode_TextLower);
#endif /* XKEYBOARD_IME_ON && XVIRTUALKEYBOARD_ON */
    return failures;
}

#else /* !(XWIDGET_ON && XKEYBOARD_ON && XLINEEDIT_ON) */

/* 开关关闭降级：build 返回 NULL（主文件既有 NULL 分支跳过注册）。 */
XWidget* demo_page_keyboard_build(XWidget* parent,
                                  DemoPageStatusFn status, void* user)
{
    (void)parent;
    (void)status;
    (void)user;
    return NULL;
}

int demo_page_keyboard_autotest(XWidget* page)
{
    (void)page;
    return 0;
}

void demo_page_keyboard_headless_hook(void)
{
    /* 开关关闭降级：零操作（无头截图钩子契约符号仍须存在）。 */
}

#endif /* XWIDGET_ON && XKEYBOARD_ON && XLINEEDIT_ON */
