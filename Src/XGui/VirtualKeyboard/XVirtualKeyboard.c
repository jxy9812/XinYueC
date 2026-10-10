/**
 * @file       XVirtualKeyboard.c
 * @brief      XVirtualKeyboard 屏幕虚拟键盘控件实现（对标 LVGL 9.2.2
 *             lv_keyboard + lv_buttonmatrix 语义）。
 * @details    与公开头 XVirtualKeyboard.h 的公共 API 一一对应；实现要点：
 *             - 矩阵行为内聚：键位布局解析（xkb_rebuildLayout，每行按
 *               自身单位总数归一化铺满行宽，对标 lv_buttonmatrix.c 归
 *               一化公式）、命中测试（xkb_hitTest，O(n) 查
 *               m_keyRects[]）、按钮激活分发（xkb_activateButton，控
 *               制键文本匹配 + 字符写入 + 三信号发射）全部为本文件
 *               static 私有函数，不单拆公开按钮矩阵控件；
 *             - 触发时序（对标 lv_buttonmatrix）：默认按下触发；ctrl
 *               含 CLICK_TRIG 或 POPOVER 时释放触发；按住滑动出键取消
 *               武装不触发、回滑恢复；NO_REPEAT 未设的键按住
 *               XKEYBOARD_LONG_PRESS_START_MS 起以 XKEYBOARD_REPEAT_MS
 *               重复（XObject_startTimer_ms 经 EXObject_TimerEvent 虚
 *               槽接收 tick，非本类定时器 id 链回基类）；
 *             - 写入链（对标 lv_keyboard → textarea 单向调用）：单字节
 *               ASCII 键合成 XKeyEvent 经 XObject_event_base 直发目标
 *               （与真实输入同路径，大写字母键自动携带 ShiftModifier
 *               ——xlc_keyToText 把 a-z 归一为大写后按 Shift 位决定输
 *               出大小写）；多字节 UTF-8 键按目标 vtable 分派公开插入
 *               API 直写（XLineEdit_insert/XPlainTextEdit_insertPlainText/
 *               XTextEdit_insertPlainText，各自受 X*_ON 开关门控）；
 *             - 布局款型（Sogou 改版一阶段）：PinyinFull（默认，搜狗
 *               全键 35 键大小写双表）/PinyinT9（九键 21 键，分词/重
 *               输/字母组未接线键原地消费）/EnglishFull（同几何共表、
 *               固定英文态）；款型装载只覆写文本双槽位
 *               （xkb_installKindTables，既有模式机制/hints 自动切换
 *               不动），组串带/组串路由判据由「mode==User1」写死泛化
 *               为「PinyinFull 且中文态」（Sogou 改版四阶段对齐九键，
 *               款型主表/引擎拼音表两载体统一；xkb_imeBandWanted/
 *               xkb_composeRouteWanted）；款型主表 Shift 键=布局层三
 *               态展示（小写→大写一次性→锁定）；搜索键=换行同语义合
 *               成 Return + 强调色键面（XKB_ACCENT）；
 *             - 联动机制（XGui 适配）：XGui 事件过滤器对本控件全部关
 *               键事件不可达（XObject notify 仅遍历接收者自身过滤器，
 *               FOCUS_IN/输入桥直投 event_base），弹层联动一律走守护
 *               定时器轮询（xkb_guardTick：焦点跟随/编辑框隐藏/宿主
 *               Resize 三查）+ XObject_destroyed_signal 防悬垂连接，
 *               与 XCompleter 守护轮询先例同型；
 *             - 适配层分支守卫：XLineEdit/XPlainTextEdit/XTextEdit 的
 *               识别与直写各自受 XLINEEDIT_ON/XPLAINTEXTEDIT_ON/
 *               XTEXTEDIT_ON 门控；三开关全 0 时键盘核心（布局/绘制/
 *               弹层/信号/合成键写入）仍可用，setTextArea 接受任意
 *               XWidget*（合成键路径类型无关），仅直写回退与类型识别
 *               降级（xkb_supportedTarget 恒 true）；
 *             - 拼音 IME 接线（XVIRTUALKEYBOARD_ON 门控）：输入状态
 *               机零直连（m_ime 成员删除，插件独占）——四条触发路径
 *               （按下/释放/长按重复/复用入口）全部汇经
 *               xkb_activateButton，单点经 xkb_routeKey 投 engine 虚
 *               键（拼音插件消费：中文态 [a-z] 进组串/数字选候选/
 *               空格回车上屏/退格删组串；中/EN 与数字分页拦截在面板
 *               本地），未消费按键原语义放行；候选（词组+单字混排，
 *               变宽 chip）数据源=engine.wordCandidateListModel
 *               （dataAt 取 XVariant*）、组串显示=context.preeditText
 *               （状态镜像）、分页=面板本地状态；带三件套
 *               （xkb_imeBandLayout/Paint/Hit）共用同一几何，布局期
 *               预留顶行、复用气泡带预算，命中在 xkb_hitTest 之前直
 *               触发不进武装链；提交/虚键=context 公共信号
 *               commitRequested/keyEventRequested→既有写入链；
 *             - 主题化（单一绘制路径）：LVGL 9.2.2 默认主题浅色模板
 *               的键面圆角矩形/按压颜色数学/候选带配色全部收在本文
 *               件局部常量（XKB_LVGL_*）；XKEYBOARD_THEME_LVGL_ON 宏
 *               已删除（双分支合并为 LVGL 单路径，调色板旧分支移除），
 *               布局常量/命中几何不变；
 *             - 菜单条（Sogou 改版二阶段）：键区上方常驻一条菜单条
 *               （条高=既有组串带预留通道 contentH/(rows+1)，所有布局
 *               与模式一致预留，原气泡带 contentH/rows 预留通道并入本
 *               条）；双模渲染——拼音中文态且组串非空时=既有拼音候选
 *               带（复用带三件套，矩形=条矩形，候选替换菜单），否则=
 *               四图标工具栏（xkb_menuBarLayout/Paint/Hit 四等分对标
 *               搜狗，画笔线条/矩形自绘随条高缩放，按压态视觉与键面
 *               一致；命中在 xkb_hitTest 之前，按压武装释放触发；收起
 *               =closePopup 直连，文字编辑/悬浮切换为桩）；
 *             - 键盘选择面板（Sogou 改版三阶段）：xkb_openLayoutSelector
 *               开合面板本地自绘选择面板（xkb_layoutSelectorLayout/
 *               Paint/Hit，覆盖键区不改窗口）：拼音全键/拼音九键/英文
 *               全键三行、当前项打勾（勾线），点选项=setLayoutKind 并
 *               关闭（IME 启用且停 User1 槽位时随行落 TextLower 让款
 *               型主表可见——引擎与候选链不重启），点面板外/再点图标
 *               =关闭（面板开启期点击一律先经面板命中消费）；
 *             - 九键路由（Sogou 改版三阶段，XVK∧IME 门控）：T9 款型
 *               且中文态时数字组键/分词/重输经 xkb_t9RouteKey 单点接
 *               引擎九键数字通道（feedT9Digit/feedDigitSeparator/
 *               engine reset，馈入后经 engine update 槽走插件 xvkpy_
 *               sync 既有镜像链——组串显示=数字串、候选经模型钩子刷
 *               新）；空格/退格/回车（搜索）走既有 routeKey 虚键链
 *               （门扩展 xkb_t9RouteWanted）；0 键与左列标点直写；
 *               EnglishFull 固定英文态（中/EN 键按压收敛回英文，字母
 *               恒直写）；
 *             - 文字编辑面板（Sogou 改版三阶段收尾）：xkb_openEditPanel
 *               开合面板本地自绘编辑面板（xkb_editPanelLayout/Paint/
 *               Hit，标题行+主区白底方向区+右列操作列，对标搜狗文字
 *               编辑页）：方向/Home/End 经 xkb_sendKey 合成到绑定目标
 *               （选择态 m_editSelArmed 携带 ShiftModifier）、开始选
 *               择切换选择态（按下视觉保持）、全选=Ctrl+A、⌫=退格；
 *               复制/剪切/粘贴公开 API 优先（三编辑控件 vtable 识别直
 *               调 xkb_editPanelClipboard，各受 X*_ON 门控），未识别
 *               目标回落合成 Ctrl+C/X/V；无绑定目标动作键禁用态（灰
 *               字）；返回箭头/再点图标回原布局，进入/退出不动弹层与
 *               绑定；与键盘选择面板互斥开合；
 *             - 紧凑悬浮态（Sogou 改版四阶段）：xkb_toggleCompactFloat
 *               桩填实——xkb_compactFloatEnter/Exit 在停靠态（子控件
 *               浮层）与紧凑悬浮态（独立顶层 Popup 宽 min(宿主宽45%,
 *               420) 高同比例、宿主右下角）间原位转换（复用悬浮形态
 *               转换路径 setParent(NULL,0)+setWindowFlags(Popup)，退
 *               出 setParent(host,0) 交还 reposition）；第三形态
 *               compactFloat 与 m_floating/setHostWindow 正交，
 *               xkb_reposition 由 m_compactFloat 守卫跳过（不抢几何），
 *               守护 tick 顶层 raise 维护继续生效；拖移=按住工具栏空
 *               白（xkb_menuBarIconHot 图标盒之外）拖窗（按下记全局锚
 *               点偏移、move 求 delta 移窗、release 结束，压在图标上
 *               不启动拖动）；closePopup/popup 全部复位。2026-10-06
 *               拖移步接入 fbdev 免重绘移窗（xwd_applyMove 同款：面板
 *               钳边+让位条带归位+既有后备缓冲整窗直搬两缓冲）——真机
 *               缺陷=移窗后旧位无人还原（残影）+依赖零散整窗 PAINT 补
 *               画新位（A33 FPS 5.9），见 xkb_compactDragMovePresent。
 * @author     XinYueC 团队
 ******************************************************************************/
#include "CXinYueConfig.h"
#include "XStringUtils.h"

#if XWIDGET_ON && XKEYBOARD_ON

#include "XVirtualKeyboard.h"
#include "XWidget_Protected.h"
#include "XMemory.h"
#include "XEvent.h"
#include "XVarList.h"
#include "XString.h"
#include "XPrintf.h"
#include "XPainter.h"
#if XWINDOW_ON
#include "XWindow.h" /* 悬浮面板接管原生鼠标捕获（XWindow_setMouseGrabEnabled）。 */
#include "XWindowEvent.h" /* 触摸手势事件（TouchDragEvent 槽判定认领）。 */
#endif
#if XAPPLICATION_ON
#include "XApplication.h" /* 悬浮锚主窗口解析：应用顶层表（XCalendarWidget 弹层容器锚）。 */
#include "XVector.h"      /* XApplication_topLevelWidgets 返回向量遍历/释放。 */
#endif
#if XLINEEDIT_ON
#include "XLineEdit.h"
#endif /* XLINEEDIT_ON */
#if XPLAINTEXTEDIT_ON
#include "XPlainTextEdit.h"
#endif /* XPLAINTEXTEDIT_ON */
#if XTEXTEDIT_ON
#include "XTextEdit.h"
#endif /* XTEXTEDIT_ON */
#if XKEYBOARD_IME_ON
#include "XPinyinEngine.h" /* 拼音布局静态表（XPinyinEngine_map/ctrlMap）。 */
#endif
#if XKEYBOARD_IME_PHRASE_ON
#include "XPinyinPhrase.h" /* 词组库懒加载（setImeEnabled(true)）。 */
#endif
#include "XWindowDecoration.h" /* 紧凑拖移 fbdev 移窗三件套：面板探测/
                               * 让位条带归位（xwd_applyMove 同款机制公共
                               * 出口，拖动残影根修 2026-10-06）。 */
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
#include "XBackingStore.h"
#include "XPlatformBackingStore.h" /* 紧凑拖移免重绘直搬：纯移动内容零
                                   * 变化，把既有后备缓冲整窗直写两缓冲
                                   * （零 flush 零翻页，A33 拖动 FPS 根修）。 */
#endif
#if XVIRTUALKEYBOARD_ON
#include "XVirtualKeyboardInputContext.h"
#include "XVirtualKeyboardInputContext_Protected.h"
#include "XVirtualKeyboardInputEngine.h"
#include "XVirtualKeyboardSelectionListModel.h"
#include "XVirtualKeyboardSelectionListModel_Protected.h"
#include "XVirtualKeyboardSettings.h"
#include "XVirtualKeyboardObserver.h"
#include "XVirtualKeyboardObserver_Protected.h"
#include "XVirtualKeyboardPinyinInputMethod.h"
#include "XVirtualKeyboardPlainInputMethod.h"
#endif /* XVIRTUALKEYBOARD_ON */

/* 长按重复节拍宏的双世界兜底：XVK=1 时由引擎头（#ifndef 门控，先包含
   者定义）给出 600/50；XVK=0 时引擎头整体裁剪、而 startRepeat/repeatTick
   的面板本地重复承载仍在（守护轮询形态），此处按同值兜底——两世界编
   译一致（独立评审②：VK=0 /Zs 实证 C2065）。 */
#ifndef XVIRTUALKEYBOARD_REPEAT_FIRST_MS
#define XVIRTUALKEYBOARD_REPEAT_FIRST_MS 600
#endif
#ifndef XVIRTUALKEYBOARD_REPEAT_MS
#define XVIRTUALKEYBOARD_REPEAT_MS 50
#endif

/* ==================== 内置静态映射表（对标 lv_keyboard.c:87-206） ==================== */

/** @brief 字符键控制字（宽度单位 | POPOVER 位，对标 LV_KB_BTN）。 */
#define XKB_BTN(w) ((XKeyboardButtonCtrl)((w) | (int)XKEYBOARD_CTRL_POPOVER))
/** @brief 选中样式字符键（CHECKED|POPOVER|宽度，对标 LVGL
 *         LV_BUTTONMATRIX_CTRL_CHECKED | LV_KB_BTN(w)，文本布局第 3 行）。 */
#define XKB_CHECKED_BTN(w) ((XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | (w) | (int)XKEYBOARD_CTRL_POPOVER))
/** @brief 控制键标志组合（NO_REPEAT|CLICK_TRIG|CHECKED）。 */
#define XKB_FLAGS ((int)XKEYBOARD_CTRL_BUTTON_FLAGS)

/* TEXT_LOWER：4 行 40 键（逐键复刻 LVGL 9.2.2 默认小写布局）。 */
static const char* const s_kbMapTextLower[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
    XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_UPPER, "a", "s", "d", "f", "g", "h", "j", "k", "l",
    XKEYBOARD_LBL_NEWLINE, "\n",
    "_", "-", "z", "x", "c", "v", "b", "n", "m", ".", ",", ":", "\n",
    XKEYBOARD_LBL_DISMISS, XKEYBOARD_LBL_LEFT, " ", XKEYBOARD_LBL_RIGHT,
    XKEYBOARD_LBL_OK,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlTextLower[] = {
    (XKeyboardButtonCtrl)(XKB_FLAGS | 5),                       /* 1# */
    XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), /* q..t */
    XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), /* y..p */
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 7),     /* 退格 */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 6),                       /* ABC */
    XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), /* a..e */
    XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3),             /* f..l */
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 7),     /* 换行 */
    XKB_CHECKED_BTN(1),                                         /* _ */
    XKB_CHECKED_BTN(1),                                         /* - */
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), /* z..b */
    XKB_BTN(1), XKB_BTN(1),                                     /* n m */
    XKB_CHECKED_BTN(1),                                         /* . */
    XKB_CHECKED_BTN(1),                                         /* , */
    XKB_CHECKED_BTN(1),                                         /* : */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 收起 */
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),     /* <- */
    (XKeyboardButtonCtrl)6,                                     /* 空格 */
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),     /* -> */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2)                        /* 确认 */
};

/* TEXT_UPPER：与 LOWER 完全同构，字母全大写；行 2 切换键 abc、行 4 首
 * 键关闭（对标 LVGL 差异位）。 */
static const char* const s_kbMapTextUpper[] = {
    "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
    XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_LOWER, "A", "S", "D", "F", "G", "H", "J", "K", "L",
    XKEYBOARD_LBL_NEWLINE, "\n",
    "_", "-", "Z", "X", "C", "V", "B", "N", "M", ".", ",", ":", "\n",
    XKEYBOARD_LBL_CLOSE, XKEYBOARD_LBL_LEFT, " ", XKEYBOARD_LBL_RIGHT,
    XKEYBOARD_LBL_OK,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlTextUpper[] = {
    (XKeyboardButtonCtrl)(XKB_FLAGS | 5),
    XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4),
    XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4), XKB_BTN(4),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 7),
    (XKeyboardButtonCtrl)(XKB_FLAGS | 6),
    XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3),
    XKB_BTN(3), XKB_BTN(3), XKB_BTN(3), XKB_BTN(3),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 7),
    XKB_CHECKED_BTN(1),                                         /* _ */
    XKB_CHECKED_BTN(1),                                         /* - */
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1),
    XKB_CHECKED_BTN(1),                                         /* . */
    XKB_CHECKED_BTN(1),                                         /* , */
    XKB_CHECKED_BTN(1),                                         /* : */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    (XKeyboardButtonCtrl)6,
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2)
};

/* SPECIAL：4 行；行 1 数字+退格、行 2/3 符号、行 4 同 LOWER 行 4。 */
static const char* const s_kbMapSpecial[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_LOWER, "+", "&", "/", "*", "=", "%", "!", "?", "#",
    "<", ">", "\n",
    "\\", "@", "$", "(", ")", "{", "}", "[", "]", ";", "\"", "'", "\n",
    XKEYBOARD_LBL_DISMISS, XKEYBOARD_LBL_LEFT, " ", XKEYBOARD_LBL_RIGHT,
    XKEYBOARD_LBL_OK,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlSpecial[] = {
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),     /* 退格 */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* abc */
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1),                                     /* 行3 共 12 键
                                                                  （原缺 1 项：
                                                                   ctrl 表 39 项
                                                                   对 40 键整体
                                                                   错位——行3 末
                                                                   键 "'" 吃到收
                                                                   起的宽 2、<->
                                                                   吃到空格宽
                                                                   6/确认的
                                                                   CLICK_TRIG、
                                                                   确认越界读；光
                                                                   标键按下不触发
                                                                   实测暴露） */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    (XKeyboardButtonCtrl)6,
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2)
};

/* NUMBER：4 行 4 列；数字模式无 abc/1# 切换键，收起键发 CANCEL 非切回
 * 文本；退格仅宽度无控制标志（按住可重复，对标 KB.c:166-177）。 */
static const char* const s_kbMapNumber[] = {
    "1", "2", "3", XKEYBOARD_LBL_DISMISS, "\n",
    "4", "5", "6", XKEYBOARD_LBL_OK, "\n",
    "7", "8", "9", XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_SIGN, "0", ".", XKEYBOARD_LBL_LEFT, XKEYBOARD_LBL_RIGHT,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlNumber[] = {
    /* 逐位对齐 LVGL default_kb_ctrl_num_map（KB.c:172-177）：数字键与
     * 行 4 五键为裸宽度 1，仅收起/确认带控制标志、退格仅宽度可重复。 */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 收起 */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 确认 */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)2,                                     /* 退格（可重复） */
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1,                                     /* +/- */
    (XKeyboardButtonCtrl)1,                                     /* 0 */
    (XKeyboardButtonCtrl)1,                                     /* . */
    (XKeyboardButtonCtrl)1,                                     /* <- */
    (XKeyboardButtonCtrl)1                                      /* -> */
};

/* DIGITS：VK 对齐新增合成布局（12 键 4 行；hints DigitsOnly 映射目
 * 标，对标 fallback/digits.qml 口径——数字 3 列 + 退格/确认/收起/光
 * 标控制键，键语义全部复用既有分发）。 */
static const char* const s_kbMapDigits[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    XKEYBOARD_LBL_BACKSPACE, "0", XKEYBOARD_LBL_OK,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlDigits[] = {
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)2,                                     /* 退格（可重复） */
    (XKeyboardButtonCtrl)2,                                     /* 0（可重复） */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2)                        /* 确认 */
};

/* DIALPAD：VK 对齐新增合成布局（3x4；hints DialableCharactersOnly 昂
 * 射目标，对标 fallback/dialpad.qml 口径——* 0 # 底行，12 键电话拨
 * 号盘特征）。 */
static const char* const s_kbMapDialpad[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "*", "0", "#",
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlDialpad[] = {
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1, (XKeyboardButtonCtrl)1,
    (XKeyboardButtonCtrl)1,                                     /* *（符号直写） */
    (XKeyboardButtonCtrl)2,                                     /* 0 */
    (XKeyboardButtonCtrl)1                                      /* #（符号直写） */
};

/* PINYIN_FULL：搜狗全键款型主表（4 行 35 键，对标安卓搜狗全键布局）：
 * 行1：q w e r t y u i o p（单位总数 10）；
 * 行2：a s d f g h j k l（单位总数 9）；
 * 行3：Shift(2) z x c v b n m 退格(2)（单位总数 11）——旧「1#+数字行」
 *      与「收起/<- ->/换行」行删除：数字走 123=既有 Number 模式、大写
 *      走款型 shift 三态、⌫=退格；
 * 行4：符(2) 123(2) ,(1) 空格(3,可重复) 。(1) 中/EN(2) 搜索(2)（单位总
 *      数 13）——符=切 Special、123=切 Number、中/EN=既有中英切换、
 *      搜索=合成 Return（强调色键面，见 XKB_ACCENT）。
 * 大写键帽表与主表同构同控制字（款型 shift 大写态换装显示）；EnglishFull
 * 与 PinyinFull 同几何共用本组表（固定英文态是判据分支非键帽差异）。 */
static const char* const s_kbMapPinyinFull[] = {
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    XKEYBOARD_LBL_SHIFT, "z", "x", "c", "v", "b", "n", "m",
    XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_SYMBOL, XKEYBOARD_LBL_NUMBERS, ",", " ",
    XKEYBOARD_LBL_CJK_PERIOD, XKEYBOARD_LBL_IME, XKEYBOARD_LBL_SEARCH,
    NULL
};
static const char* const s_kbMapPinyinFullUpper[] = {
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "\n",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", "\n",
    XKEYBOARD_LBL_SHIFT, "Z", "X", "C", "V", "B", "N", "M",
    XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_SYMBOL, XKEYBOARD_LBL_NUMBERS, ",", " ",
    XKEYBOARD_LBL_CJK_PERIOD, XKEYBOARD_LBL_IME, XKEYBOARD_LBL_SEARCH,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlPinyinFull[] = {
    /* 行1：q..p（POPOVER 字符键）。 */
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    /* 行2：a..l。 */
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    /* 行3：Shift（控制键宽 2，释放触发不重复）+ z..m + 退格（宽 2 可
     * 重复，灰面同旧表口径）。 */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),
    XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1), XKB_BTN(1),
    XKB_BTN(1), XKB_BTN(1),
    (XKeyboardButtonCtrl)((int)XKEYBOARD_CTRL_CHECKED | 2),
    /* 行4：符/123/中/EN/搜索（控制键）+, 。（字符）+ 空格（可重复）。 */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 符 */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 123 */
    XKB_BTN(1),                                                 /* , */
    (XKeyboardButtonCtrl)3,                                     /* 空格（可重复） */
    XKB_BTN(1),                                                 /* 。 */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 中/EN */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2)                        /* 搜索 */
};

/* PINYIN_T9：搜狗九键款型主表（4 行 21 键，对标安卓搜狗九键布局）：
 * 左列 ，。？！直插标点；主区 3x4：行1 分词 ABC DEF 退格、行2 GHI JKL
 * MNO 重输、行3 PQRS TUV WXYZ 0、行4（功能行）符 123 空格 中/EN 搜索。
 * 分词/重输/字母组标签的引擎接线在后续阶段——本阶段只落表与标签，激
 * 活原地消费（xkb_t9LabelUnwired，防标签字面量直写/误触既有 ABC 语义）；
 * 各行单位总数统一 14（左列 2 + 主区 12）。 */
static const char* const s_kbMapPinyinT9[] = {
    XKEYBOARD_LBL_T9_COMMA, XKEYBOARD_LBL_T9_SEG, "ABC", "DEF",
    XKEYBOARD_LBL_BACKSPACE, "\n",
    XKEYBOARD_LBL_CJK_PERIOD, "GHI", "JKL", "MNO", XKEYBOARD_LBL_T9_RETYPE,
    "\n",
    XKEYBOARD_LBL_T9_QMARK, "PQRS", "TUV", "WXYZ", "0", "\n",
    XKEYBOARD_LBL_T9_EXMARK, XKEYBOARD_LBL_SYMBOL, XKEYBOARD_LBL_NUMBERS,
    " ", XKEYBOARD_LBL_IME, XKEYBOARD_LBL_SEARCH,
    NULL
};
static const XKeyboardButtonCtrl s_kbCtrlPinyinT9[] = {
    /* 行1：，（标点直写）+ 分词/ABC/DEF（未接线标签）+ 退格（可重复）。 */
    XKB_BTN(2), (XKeyboardButtonCtrl)3, (XKeyboardButtonCtrl)3,
    (XKeyboardButtonCtrl)3, (XKeyboardButtonCtrl)3,
    /* 行2：。+ GHI/JKL/MNO + 重输。 */
    XKB_BTN(2), (XKeyboardButtonCtrl)3, (XKeyboardButtonCtrl)3,
    (XKeyboardButtonCtrl)3, (XKeyboardButtonCtrl)3,
    /* 行3：？+ PQRS/TUV/WXYZ + 0（数字可重复）。 */
    XKB_BTN(2), (XKeyboardButtonCtrl)3, (XKeyboardButtonCtrl)3,
    (XKeyboardButtonCtrl)3, (XKeyboardButtonCtrl)3,
    /* 行4：！+ 符/123/中/EN/搜索（控制键）+ 空格（可重复）。 */
    XKB_BTN(2),
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 符 */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 123 */
    (XKeyboardButtonCtrl)4,                                     /* 空格（可重复） */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2),                       /* 中/EN */
    (XKeyboardButtonCtrl)(XKB_FLAGS | 2)                        /* 搜索 */
};

/** @brief 款型主表装载（init 默认款型与 setLayoutKind 共用；只覆写
 *         TextLower/TextUpper 双槽位，其余槽位与既有回落机制不动）。
 * @details PinyinFull/EnglishFull 大小写各装一表（EnglishFull 与
 *          PinyinFull 同几何共表，固定英文态是组串路由/候选带判据的
 *          款型分支）；PinyinT9 无大小写键帽变体，双槽位同装一表
 *          （款型切换复位 shift 展示态，TextUpper 读数不会落旧表）。 */
static void xkb_installKindTables(XVirtualKeyboard* self)
{
    const char* const* lowerMap = s_kbMapPinyinFull;
    const XKeyboardButtonCtrl* lowerCtrl = s_kbCtrlPinyinFull;
    const char* const* upperMap = s_kbMapPinyinFullUpper;
    const XKeyboardButtonCtrl* upperCtrl = s_kbCtrlPinyinFull;
    if (self->m_layoutKind == XKeyboardLayout_PinyinT9) {
        lowerMap = s_kbMapPinyinT9;
        lowerCtrl = s_kbCtrlPinyinT9;
        upperMap = s_kbMapPinyinT9;
        upperCtrl = s_kbCtrlPinyinT9;
    }
    self->m_maps[XKeyboardMode_TextLower] = lowerMap;
    self->m_ctrls[XKeyboardMode_TextLower] = lowerCtrl;
    self->m_maps[XKeyboardMode_TextUpper] = upperMap;
    self->m_ctrls[XKeyboardMode_TextUpper] = upperCtrl;
}

/* 槽位容器：0..3 内置、4..7（USER_1..4）默认回落小写表（对标
 * KB.c:179-206）、8..9 VK 合成布局、末位 NULL（经 init 装入实例槽位
 * 数组）。init 装入后随即按默认款型覆写 0/1 文本双槽位
 * （xkb_installKindTables：默认 PinyinFull 搜狗全键；本组旧内置表仍
 * 为 Special/Number/User1..4 槽位与款型外回落的事实源，现状可恢复）。 */
static const char* const* const s_kbMapDefaults[XKEYBOARD_MODE_SLOT_COUNT] = {
    s_kbMapTextLower, s_kbMapTextUpper, s_kbMapSpecial, s_kbMapNumber,
    s_kbMapTextLower, s_kbMapTextLower, s_kbMapTextLower, s_kbMapTextLower,
    s_kbMapDigits, s_kbMapDialpad
};
static const XKeyboardButtonCtrl* const s_kbCtrlDefaults[XKEYBOARD_MODE_SLOT_COUNT] = {
    s_kbCtrlTextLower, s_kbCtrlTextUpper, s_kbCtrlSpecial, s_kbCtrlNumber,
    s_kbCtrlTextLower, s_kbCtrlTextLower, s_kbCtrlTextLower, s_kbCtrlTextLower,
    s_kbCtrlDigits, s_kbCtrlDialpad
};

/* ==================== 前向声明 ==================== */
static void VXKeyboard_paintEvent(XWidget* self, XEvent* event);
static void VXKeyboard_resizeEvent(XWidget* self, XEvent* event);
static void VXKeyboard_mousePressEvent(XWidget* self, XEvent* event);
static void VXKeyboard_mouseReleaseEvent(XWidget* self, XEvent* event);
static void VXKeyboard_mouseMoveEvent(XWidget* self, XEvent* event);
static void VXKeyboard_touchDragEvent(XWidget* self, XEvent* event);
static void VXKeyboard_hideEvent(XWidget* self, XEvent* event);
static void VXKeyboard_timerEvent(XObject* object, XTimerEvent* event);
static void VXKeyboard_deinit(XVirtualKeyboard* self);
static void VXKeyboard_copy(XVirtualKeyboard* self, const XVirtualKeyboard* other);
static void VXKeyboard_move(XVirtualKeyboard* self, XVirtualKeyboard* other);
static void xkb_rebuildLayout(XVirtualKeyboard* self);
static uint32_t xkb_hitTest(const XVirtualKeyboard* self, int x, int y);
static bool xkb_activateButton(XVirtualKeyboard* self, uint32_t buttonId);
static void xkb_menuBarPaint(XVirtualKeyboard* self, XPainter* painter);
static bool xkb_menuBarHit(XVirtualKeyboard* self, int x, int y);
static int xkb_menuBarCellAt(const XVirtualKeyboard* self, int x, int y);
static void xkb_toggleCompactFloat(XVirtualKeyboard* self);
static void xkb_openLayoutSelector(XVirtualKeyboard* self);
static void xkb_openEditPanel(XVirtualKeyboard* self);
static bool xkb_layoutSelectorHit(XVirtualKeyboard* self, int x, int y);
static void xkb_layoutSelectorPaint(XVirtualKeyboard* self, XPainter* painter);
static bool xkb_editPanelHit(XVirtualKeyboard* self, int x, int y);
static void xkb_editPanelPaint(XVirtualKeyboard* self, XPainter* painter);
static void xkb_compactFloatEnter(XVirtualKeyboard* self);
static void xkb_compactFloatExit(XVirtualKeyboard* self);
static bool xkb_menuBarIconHot(const XVirtualKeyboard* self, int slot,
                               int x, int y);
static void xkb_writeText(XVirtualKeyboard* self, const char* text);
static void xkb_sendKey(XVirtualKeyboard* self, int key,
                        XKeyboardModifiers modifiers);
static void xkb_signFlip(XVirtualKeyboard* self);
static void xkb_installKindTables(XVirtualKeyboard* self);
static void xkb_kindShiftApply(XVirtualKeyboard* self);
static bool xkb_t9LabelUnwired(const char* text);
static void xkb_startRepeat(XVirtualKeyboard* self);
static void xkb_repeatTick(XVirtualKeyboard* self);
static void xkb_guardTick(XVirtualKeyboard* self);
static void xkb_reposition(XVirtualKeyboard* self);
static void xkb_bindTargetDestroyed(XVirtualKeyboard* self, XWidget* newTarget);
static void xkb_bindHostDestroyed(XVirtualKeyboard* self, XWidget* newHost);
static void xkb_hostGeomSlot(XObject* receiver, XVarList* args);
static void xkb_bindGeometrySignals(XVirtualKeyboard* self, bool bind);
static void xkb_stopGuard(XVirtualKeyboard* self);
#if XVIRTUALKEYBOARD_ON
static bool xkb_routeKey(XVirtualKeyboard* self, const char* text);
static bool xkb_imeComposing(const XVirtualKeyboard* self);
static bool xkb_imeBandHit(XVirtualKeyboard* self, int x, int y);
static void xkb_applyFocusHints(XVirtualKeyboard* self);
static void xkb_syncShiftMode(XVirtualKeyboard* self);
static void xkb_notifyObserver(const XVirtualKeyboard* self);
static void xkb_bindContextConns(XVirtualKeyboard* self, bool bind);
static void xkb_commitSlot(XObject* receiver, XVarList* args);
static void xkb_keyEventSlot(XObject* receiver, XVarList* args);
static void xkb_preeditSlot(XObject* receiver, XVarList* args);
static bool xkb_composeCollapsedWanted(const XVirtualKeyboard* self);
#endif

/* ==================== 内部辅助 ==================== */

#if XVIRTUALKEYBOARD_ON

/** @brief 取进程单例输入上下文（惰性；未初始化返回 NULL）。 */
static XVirtualKeyboardInputContext* xkb_contextOf(const XVirtualKeyboard* self)
{
    (void)self;
    return XVirtualKeyboardInputContext_instance();
}

/** @brief 取引擎（经上下文单例；NULL 安全）。 */
static XVirtualKeyboardInputEngine* xkb_engineOf(const XVirtualKeyboard* self)
{
    XVirtualKeyboardInputContext* ctx = xkb_contextOf(self);
    return ctx ? XVirtualKeyboardInputContext_inputEngine(ctx) : NULL;
}

/** @brief 取候选列表模型（引擎持有；NULL 安全）。 */
static XVirtualKeyboardSelectionListModel* xkb_modelOf(const XVirtualKeyboard* self)
{
    XVirtualKeyboardInputEngine* engine = xkb_engineOf(self);
    return engine
               ? XVirtualKeyboardInputEngine_wordCandidateListModel(engine)
               : NULL;
}

/** @brief 中文态读数（=engine 当前输入模式为 Pinyin 且插件已装载）。 */
static bool xkb_imeChineseState(const XVirtualKeyboard* self)
{
    XVirtualKeyboardInputEngine* engine;
    if (!self || !self->m_imeEnabled) return false;
    engine = xkb_engineOf(self);
    if (!engine) return false;
    return XVirtualKeyboardInputEngine_inputMode(engine) ==
           (int)XVirtualKeyboardInputEngineInputMode_Pinyin;
}

/** @brief 组串路由判据（Sogou 改版四阶段对齐九键判据，评审①死锁修
 *         复）：PinyinFull 款型且中文态即路由——款型主表（TextLower/
 *         TextUpper 槽位的搜狗全键表，键盘选择面板落位形态）与引擎内
 *         置拼音表（User1 槽位）两载体统一进组串，不再要求「当前槽位
 *         表==引擎表」（款型主表是独立数组，旧判据下面板落 TextLower
 *         后字母直写死锁且无 UI 路径回 User1）。EnglishFull 固定英文
 *         态不路由（字母不进 IME 组串）；PinyinT9 走九键数字通道独立
 *         判据（xkb_t9RouteWanted）。 */
static bool xkb_composeRouteWanted(const XVirtualKeyboard* self)
{
    if (!self || !self->m_imeEnabled) return false;
    return self->m_layoutKind == XKeyboardLayout_PinyinFull &&
           xkb_imeChineseState(self);
}

/** @brief 候选带预留判据（Sogou 改版四阶段对齐九键）：PinyinFull 款
 *         型或 PinyinT9 款型且中文态即预留（款型主表/引擎拼音表两载
 *         体统一——组串显示与候选消费不区分表载体）；EnglishFull 不预
 *         留。非文本模式（数字盘等）切模式即弃组串，条渲染工具栏。 */
static bool xkb_imeBandWanted(const XVirtualKeyboard* self)
{
    if (!self || !self->m_imeEnabled) return false;
    if (!xkb_imeChineseState(self)) return false;
    return self->m_layoutKind == XKeyboardLayout_PinyinFull ||
           self->m_layoutKind == XKeyboardLayout_PinyinT9;
}

/** @brief 九键路由判据（Sogou 改版三阶段）：T9 款型且中文态——数字
 *         组键/分词/重输/空格/退格/回车在此门内接引擎组串链。 */
static bool xkb_t9RouteWanted(const XVirtualKeyboard* self)
{
    if (!self || !self->m_imeEnabled) return false;
    return self->m_layoutKind == XKeyboardLayout_PinyinT9 &&
           xkb_imeChineseState(self);
}
#endif /* XVIRTUALKEYBOARD_ON */

/** @brief 九键英文多击循环复位（闭合当前多击周期；幂等零副作用）。 */
static void xkb_t9TapReset(XVirtualKeyboard* self)
{
    if (!self) return;
    if (self->m_t9TapTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_t9TapTimer);
        self->m_t9TapTimer = XTIMER_INVALID_ID;
    }
    self->m_t9TapDigit = 0;
    self->m_t9TapIdx = 0;
}

/** @brief 九键英文多击按键处理（T9 款型非中文路由态；xkb_
 *         activateButton 调用）。
 * @details 标准多击（multi-tap）：数字组键连击循环字母（ABC 第 1 击
 *          =a、第 2 击=b、第 3 击=c、第 4 击回 a；连击窗口 800ms 由
 *          m_t9TapTimer 闭合），组内进位=退格删上一字母再写新字母，
 *          换组/超时自然分段；0=空格；左列标点 ，。？！ 转写 ASCII
 *          （英文语境全角标点不合用）；分词/重输原地消费（英文态无
 *          组串语义）。旧口径整表吞掉使英文九键完全不可输入（实测
 *          反馈）。字母恒小写（T9 主表无 shift 键）。
 * @return  键已消费返回 true；非多击键返回 false（回落既有链）。 */
static bool xkb_t9EnglishKey(XVirtualKeyboard* self, const char* text)
{
    static const char* const kGroups[8] = {
        "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ"
    };
    static const char* const kLetters[8] = {
        "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"
    };
    char letter[2];
    char digit;
    int g;
    int glen = 0;
    for (g = 0; g < 8; ++g) {
        if (XStrcmp(text, kGroups[g]) != 0) continue;
        digit = (char)('2' + g);
        while (kLetters[g][glen] != '\0') ++glen;
        if (self->m_t9TapDigit == digit) {
            /* 同组连击：退格删上一字母（写入链合成 Backspace），写下一
             * 循环位次；窗口重启。 */
            self->m_t9TapIdx = (self->m_t9TapIdx + 1) % glen;
            xkb_sendKey(self, XKey_Backspace, XKeyboardModifier_NoModifier);
        } else {
            xkb_t9TapReset(self);
            self->m_t9TapDigit = digit;
            self->m_t9TapIdx = 0;
        }
        letter[0] = kLetters[g][self->m_t9TapIdx];
        letter[1] = '\0';
        xkb_writeText(self, letter);
        if (self->m_t9TapTimer != XTIMER_INVALID_ID)
            XObject_killTimer((XObject*)self, self->m_t9TapTimer);
        self->m_t9TapTimer = XObject_startTimer_ms(
            (XObject*)self, 800, XTimerType_CoarseTimer);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_T9_COMMA) == 0) {
        xkb_writeText(self, ",");
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_CJK_PERIOD) == 0) {
        xkb_writeText(self, ".");
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_T9_QMARK) == 0) {
        xkb_writeText(self, "?");
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_T9_EXMARK) == 0) {
        xkb_writeText(self, "!");
        return true;
    }
    if (XStrcmp(text, "0") == 0) {
        xkb_writeText(self, " "); /* 英文九键 0=空格（搜狗口径）。 */
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_T9_SEG) == 0 ||
        XStrcmp(text, XKEYBOARD_LBL_T9_RETYPE) == 0)
        return true; /* 分词/重输：英文态无组串语义，原地消费。 */
    return false;
}

#if XVIRTUALKEYBOARD_ON
#if XKEYBOARD_IME_ON
/** @brief 取插件独占九键状态机（T9 路由馈入用；非拼音插件返回 NULL）。
 *  @note   面板对组串状态机的既有契约=零直连（插件独占）；而九键数字
 *          通道的馈入 API（feedT9Digit/feedDigitSeparator）无引擎级
 *          入口（virtualKeyClick 只承载字母/选候选数字，插件 keyEvent
 *          消费面见 XVirtualKeyboardPinyinInputMethod.c），本阶段按
 *          任务书「键盘侧九键路由接 XPinyinEngine 馈入函数」口径经
 *          公开头结构成员馈入（vtable 校验守卫）；馈入后的组串/候选
 *          镜像仍统一走插件 xvkpy_sync 链（xkb_t9Sync→engine update
 *          槽），零直连边界只在馈入一点收窄。 */
static XPinyinEngine* xkb_t9EngineOf(const XVirtualKeyboard* self)
{
    XVirtualKeyboardInputEngine* engine = xkb_engineOf(self);
    XVirtualKeyboardAbstractInputMethod* im;
    if (!engine) return NULL;
    im = XVirtualKeyboardInputEngine_inputMethod(engine);
    if (!im || XClassGetVtable((XClass*)im) !=
                   XVirtualKeyboardPinyinInputMethod_class_init())
        return NULL;
    return &((XVirtualKeyboardPinyinInputMethod*)im)->m_ime;
}
#endif /* XKEYBOARD_IME_ON */

/** @brief 九键馈入后的插件镜像同步（engine update 槽→插件 xvkpy_
 *         sync：preedit=数字组串 + 候选列表信号——组串直显数字、候
 *         选带复用既有刷新链）。 */
static void xkb_t9Sync(XVirtualKeyboard* self)
{
    XVirtualKeyboardInputEngine* engine = xkb_engineOf(self);
    if (engine) XVirtualKeyboardInputEngine_update(engine);
}

/** @brief 九键数字组键标签 → 数字字符（'2'..'9'；非组标签返回 0）。 */
static int xkb_t9DigitOfLabel(const char* text)
{
    static const struct
    {
        const char* m_label;
        char m_digit;
    } kT9DigitMap[] = {
        {"ABC", '2'}, {"DEF", '3'}, {"GHI", '4'}, {"JKL", '5'},
        {"MNO", '6'}, {"PQRS", '7'}, {"TUV", '8'}, {"WXYZ", '9'},
    };
    size_t i;
    if (!text) return 0;
    for (i = 0; i < sizeof(kT9DigitMap) / sizeof(kT9DigitMap[0]); ++i) {
        if (XStrcmp(text, kT9DigitMap[i].m_label) == 0)
            return kT9DigitMap[i].m_digit;
    }
    return 0;
}

/**
 * @brief 九键路由单点（xkb_activateButton 调用；xkb_t9RouteWanted 门内）。
 * @details 数字组标签→多击字母循环（2026-10-04 用户需求：九键中文
 *          打字=同键快速单击在组内三/四字母间切换——首击落组首字母、
 *          同键窗口内连击组串退格删上一字母再落下一循环位次，字母经
 *          既有字母通道 routeKey 进组串精确匹配候选；窗口 800ms=英文
 *          多击同款 m_t9TapTimer，窗口外/换键落组首字母追加。组串结
 *          束（提交/删空）经 preeditSlot 复位多击态，防陈旧位次跨词
 *          生效）。原数字匹配通道 feedT9Digit 保留引擎级入口
 *          （XPinyinT9Test 状态机锁），面板不再走数字匹配——字母通道
 *          候选与全键同源，消歧由多击承担。分词→feedDigitSeparator
 *          （字母通道组串下为幂等空转，键保留语义）；重输→engine
 *          reset（公共槽→插件 reset→resetComposition+镜像同步，IDLE
 *          空转）。其余标签（0/空格/退格/搜索/标点/中EN）返回 false
 *          放行既有链。
 * @return 已消费返回 true；未识别返回 false。
 */
static bool xkb_t9RouteKey(XVirtualKeyboard* self, const char* text)
{
#if !XKEYBOARD_IME_ON
    (void)self;
    (void)text; /* 引擎裁剪：数字通道不存在，一律回落原地消费。 */
#else
    XPinyinEngine* ime;
    static const char* const kLetters[8] = {
        "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz"
    };
    int digit;
    if (!self || !text || !text[0]) return false;
    digit = xkb_t9DigitOfLabel(text);
    if (digit) {
        char letter[2];
        int g = digit - '2';
        int glen = 0;
        int idx;
        ime = xkb_t9EngineOf(self);
        if (!ime) return false;
        while (kLetters[g][glen] != '\0') ++glen;
        /* 多击位次在局部推进、字母落串成功后才写回成员：同键退格换位
           次的那一拍若组串被清空（组串仅一字时），会同步触发
           preeditSlot 的多击态复位把成员清零——从成员读位次会永远落
           回组首字母（快速单击不切换，2026-10-04 用户实测回归根因）。 */
        if (self->m_t9TapDigit == (char)digit) {
            /* 同键连击：组串退格删上一字母，落下一循环位次（非组串态
               不加退格——上一字母已随提交/删空消耗）。 */
            idx = (self->m_t9TapIdx + 1) % glen;
            if (xkb_imeComposing(self))
                (void)xkb_routeKey(self, XKEYBOARD_LBL_BACKSPACE);
        } else {
            idx = 0;
        }
        letter[0] = kLetters[g][idx];
        letter[1] = '\0';
        /* 原始字母通道（不过 INV2 截断）：多击中间态 "ng"/"nh" 等是
           必经路径，feedLetter 的最长合法后缀截断会把 "n"+g 截成 "g"
           （组串永远到不了 "ni"）——九键多击必须无截断入串。 */
        (void)XPinyinEngine_feedLetterRaw(ime, letter[0]);
        self->m_t9TapDigit = (char)digit;
        self->m_t9TapIdx = idx;
        if (self->m_t9TapTimer != XTIMER_INVALID_ID)
            XObject_killTimer((XObject*)self, self->m_t9TapTimer);
        self->m_t9TapTimer = XObject_startTimer_ms(
            (XObject*)self, 800, XTimerType_CoarseTimer);
        xkb_t9Sync(self);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_T9_SEG) == 0) {
        ime = xkb_t9EngineOf(self);
        if (ime) (void)XPinyinEngine_feedDigitSeparator(ime);
        xkb_t9Sync(self);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_T9_RETYPE) == 0) {
        XVirtualKeyboardInputEngine* engine = xkb_engineOf(self);
        if (engine) XVirtualKeyboardInputEngine_reset(engine);
        XWidget_update((XWidget*)self);
        return true;
    }
#endif
    return false;
}

/** @brief 组串中判定（=context.preeditText 非空——插件状态镜像）。 */
static bool xkb_imeComposing(const XVirtualKeyboard* self)
{
    XVirtualKeyboardInputContext* ctx;
    XString* preedit;
    bool composing;
    if (!self || !self->m_imeEnabled) return false;
    ctx = xkb_contextOf(self);
    if (!ctx) return false;
    preedit = XVirtualKeyboardInputContext_preeditText(ctx);
    composing = preedit && XString_toUtf8(preedit) &&
                XString_toUtf8(preedit)[0] != '\0';
    if (preedit) XClassDelete(preedit);
    return composing;
}

/** @brief 候选总数读数（engine.wordCandidateListModel().count）。 */
static int xkb_candidateCount(const XVirtualKeyboard* self)
{
    XVirtualKeyboardSelectionListModel* model = xkb_modelOf(self);
    return model ? XVirtualKeyboardSelectionListModel_count(model) : 0;
}

/** @brief 组串收缩态判据（用户需求③「中文状态转化显示中文时只显示
 *         第一行候选带，屏幕按键都可以隐藏」）：面板弹出+停靠形态（子
 *         控件浮层挂宿主底部）+物理键盘组串会话+组串中+候选带在位。
 *         输入源门（2026-10-04 用户澄清）：需求②③仅限外置键盘输入，
 *         屏幕键盘输入无此要求——点键组串保持全量面板（缩成一条键区
 *         消失即无法继续打字），仅 m_physKeyActive（notifyKey 中文路
 *         由消费置位）承载收缩。紧凑悬浮（m_compactFloat）/悬浮锚
 *         （m_floating）/对话框外置浮层形态本轮不做收缩——三者各有
 *         独立几何主权（用户拖移/锚定落位/overlay 分流），收缩仅承
 *         载停靠分支。 */
static bool xkb_composeCollapsedWanted(const XVirtualKeyboard* self)
{
    if (!self || !self->m_popped) return false;
    if (self->m_floating || self->m_compactFloat) return false;
    if (!self->m_physKeyActive) return false; /* 屏幕键组串不收缩。 */
    if (XWidget_isWindow((XWidget*)self) &&
        XWidget_parentWidget((XWidget*)self))
        return false; /* 对话框外置浮层形态（repositionOverlay 分流）。 */
    return xkb_imeComposing(self) && xkb_imeBandWanted(self);
}

/** @brief 取候选文本快照（dataAt→XVariant String→UTF-8 拷入缓冲）。 */
static bool xkb_candidateText(const XVirtualKeyboard* self, int index, char* buf,
                              int bufSize)
{
    XVirtualKeyboardSelectionListModel* model = xkb_modelOf(self);
    XVariant* value;
    const XString* text;
    const char* utf8;
    if (!model || !buf || bufSize <= 0) return false;
    buf[0] = '\0';
    value = XVirtualKeyboardSelectionListModel_dataAt(
        model, index, (int)XVirtualKeyboardSelectionListModelRole_Display);
    if (!value) return false;
    text = XVariant_toString_const(value);
    utf8 = text ? XString_toUtf8(text) : NULL;
    if (utf8) XStrncpy(buf, utf8, (size_t)bufSize);
    buf[bufSize - 1] = '\0';
    XClassDelete(value);
    return utf8 != NULL;
}

/** @brief 组串文本快照（context.preeditText→缓冲）。 */
static void xkb_preeditSnapshot(const XVirtualKeyboard* self, char* buf,
                                int bufSize)
{
    XVirtualKeyboardInputContext* ctx;
    XString* preedit;
    if (!buf || bufSize <= 0) return;
    buf[0] = '\0';
    ctx = xkb_contextOf(self);
    if (!ctx) return;
    preedit = XVirtualKeyboardInputContext_preeditText(ctx);
    if (preedit) {
        const char* utf8 = XString_toUtf8(preedit);
        if (utf8) XStrncpy(buf, utf8, (size_t)bufSize);
        XClassDelete(preedit);
    }
    buf[bufSize - 1] = '\0';
}

/** @brief 候选总页数（钳 [0, ...)；ceil(count/pageSize)）。 */
static int xkb_candidatePageCount(const XVirtualKeyboard* self)
{
    int count = xkb_candidateCount(self);
    int pageSize = self->m_candidatePageSize > 0 ? self->m_candidatePageSize
                                                 : 1;
    if (count <= 0) return 0;
    return (count + pageSize - 1) / pageSize;
}

/** @brief 布局/模式切换后通知 Observer（layoutChanged 通知链）。 */
static void xkb_notifyObserver(const XVirtualKeyboard* self)
{
    const char* layoutType = "main";
    XVirtualKeyboardInputEngine* engine;
    int mode;
    if (!self) return;
    switch (self->m_mode) {
    case XKeyboardMode_Special: layoutType = "symbols"; break;
    case XKeyboardMode_Number: layoutType = "numbers"; break;
    case XKeyboardMode_Digits: layoutType = "digits"; break;
    case XKeyboardMode_Dialpad: layoutType = "dialpad"; break;
    default: layoutType = "main"; break;
    }
    engine = xkb_engineOf(self);
    mode = engine ? XVirtualKeyboardInputEngine_inputMode(engine) : 0;
    XVirtualKeyboardObserver_invalidateLayout(XVirtualKeyboardObserver_instance(),
                                              layoutType, "zh_CN", mode);
}

#endif /* XVIRTUALKEYBOARD_ON */

/**
 * @brief 判定目标控件是否为受支持的输入目标（适配层类型识别 + WA14
 *        放行回退）。
 * @details 双口径：①vtable 快路径——逐一比对三编辑控件（XCompleter.c:
 *          535 先例），直写+合成键双写入通道齐备；②WA_InputMethodEnabled
 *          放行回退——对齐 Qt inputMethodAccepted=WA14 口径
 *          （qwidget.cpp:9057-9057 同族），任意开了 WA14 的控件皆可绑
 *          定。修复「XDateEdit/XDateTimeEdit 字段壳获焦后面板闪退」：
 *          壳（XDateTimeEdit vtable）曾在此被拒（init 置 WA14+DigitsOnly，
 *          XDateTimeEdit.c:3067-3070）→popup 经 setTextArea 早退不弹/
 *          守护 ②′ 与 notifyPress 幂等收层。放行目标的写入面=合成键
 *          注入（xkb_sendKey 经 XObject_event_base 直发 m_target，类型
 *          无关）：单字节 ASCII 字符键/Return 照常可达——XDateTimeEdit
 *          壳的数字键由其 keyPressEvent 分段消化（xdt_typeDigit，按下
 *          即终结不转发行编辑），行为对齐无头 apitest 注入口径；多字节/
 *          IME 上屏直写仍仅认三编辑控件 vtable（writeText 尾部诊断忽
 *          略），+/- 符号翻转仍仅 XLineEdit——能力不对称在 writeText
 *          注释与本回退处双声明。三个适配开关全 0 时类型识别降级：
 *          恒返回 true（setTextArea 接受任意 XWidget*）。
 */
static bool xkb_supportedTarget(const XWidget* target)
{
    XVtable* vt;
    if (!target) return true;
    vt = XClassGetVtable(target);
#if XLINEEDIT_ON
    if (vt == XLineEdit_class_init()) return true;
#endif
#if XPLAINTEXTEDIT_ON
    if (vt == XPlainTextEdit_class_init()) return true;
#endif
#if XTEXTEDIT_ON
    if (vt == XTextEdit_class_init()) return true;
#endif
#if !XLINEEDIT_ON && !XPLAINTEXTEDIT_ON && !XTEXTEDIT_ON
    (void)vt;
    return true; /* 类型识别降级：接受任意控件（合成键路径类型无关）。 */
#else
    /* WA14 放行回退：非三编辑控件但显式开启输入法属性（WA14）即接受
       绑定（合成键注入焦点路径承载写入；直写回退缺席见函数头）。 */
    return XWidget_testAttribute(target, XWidgetAttribute_InputMethodEnabled);
#endif
}

/** @brief 判定命中控件是否落在键盘面板子树内（notifyPress 豁免判据）。
 *  @details 沿父链上溯比对 self：面板自身或其子控件命中→true（键盘自
 *           身按键 PRESS 不驱动弹收，弹出态按键照常输入）。 */
static bool xkb_insidePanel(const XVirtualKeyboard* self, const XWidget* hit)
{
    const XWidget* w = hit;
    while (w) {
        if (w == (const XWidget*)self) return true;
        w = XWidget_parentWidget(w);
    }
    return false;
}

/** @brief T9 未接线键标签判定（分词/重输/字母组标签）。
 *  @details Sogou 改版一阶段只落表与标签：这些键的引擎接线在后续阶
 *           段，激活原地消费——「ABC」等组标签不拦截会误触既有大写切
 *           换语义、「分词/重输」会按字符键回路把字面量写进编辑框。
 *           数字 0 不在列（无组串语义，按普通字符直写）。 */
static bool xkb_t9LabelUnwired(const char* text)
{
    static const char* const kT9Unwired[] = {
        XKEYBOARD_LBL_T9_SEG, XKEYBOARD_LBL_T9_RETYPE,
        "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ"
    };
    size_t i;
    if (!text) return false;
    for (i = 0; i < sizeof(kT9Unwired) / sizeof(kT9Unwired[0]); ++i) {
        if (XStrcmp(text, kT9Unwired[i]) == 0) return true;
    }
    return false;
}

/** @brief 布局款型 shift 展示态落地（大小写键帽换装 rebuild）。
 *  @details 展示态 0=小写（TextLower 槽位表）、1/2=大写键帽
 *           （TextUpper 槽位表；锁定态 shift 键 CHECKED 高亮在
 *           xkb_rebuildLayout 内按展示态打位）。直改 m_mode 读数不经
 *           setMode——shift 展示切换不弃组串草稿（对齐 toggleShift→
 *           syncShiftMode 路径口径）。 */
static void xkb_kindShiftApply(XVirtualKeyboard* self)
{
    XKeyboardMode target = (self->m_shiftState != 0)
                               ? XKeyboardMode_TextUpper
                               : XKeyboardMode_TextLower;
    if (self->m_mode != target) {
        self->m_mode = target;
#if XVIRTUALKEYBOARD_ON
        xkb_notifyObserver(self);
#endif
    }
    xkb_rebuildLayout(self);
    XWidget_update((XWidget*)self);
}

#if XAPPLICATION_ON
/** @brief 判定顶层控件是否 Popup 型窗口（悬浮锚识别弹层容器）。
 *  @details 类型位=windowFlags & TypeMask（XWidget_windowType 口径）；
 *           XDateTimeEdit 日历弹层容器在 xdt_popupReposition 里
 *           XWidget_setWindowFlags(Popup)。XWINDOW_ON=0 无弹层形态，
 *           恒 false。 */
static bool xkb_isPopupTypeWindow(const XWidget* w)
{
    if (!w) return false;
#if XWINDOW_ON
    return XWidget_windowType(w) == XWindowType_Popup;
#else
    (void)w;
    return false; /* 无窗口层：Popup 型弹层容器不存在。 */
#endif
}

/** @brief 悬浮锚候选可用性（非键盘自身/非锚本身/顶层/非 Popup 型）。 */
static bool xkb_anchorCandidateOk(const XVirtualKeyboard* self,
                                  const XWidget* host, const XWidget* w)
{
    if (!w || w == (const XWidget*)self || w == host) return false;
    if (!XWidget_isWindow(w) || xkb_isPopupTypeWindow(w)) return false;
    return true;
}

/** @brief 悬浮锚解析：锚为弹层容器时定位主窗口顶层。
 *  @details 弹层容器是 NULL 父的独立 Popup 顶层，其 widget 父链与桥接
 *           窗 transient parent 均无主窗口反向引用（XWidget_createWindow
 *           的 owner 链走父控件链，弹层父链为空），应用顶层表
 *           （XApplication_topLevelWidgets）在 XGuiApplication 单根应
 *           用（不创建 XApplication 基类实例，g_xapp 空）下亦不可用——
 *           按可信次序取候选：① m_hostHint=setHostWindow 时刻焦点顶
 *           层快照（弹层打开前用户所在主窗口，transient parent 语义）；
 *           ② m_host=上一次内嵌弹层的宿主顶层（普通编辑框键盘的记忆
 *           宿主）；③ 应用顶层表（g_xapp 存在的多窗口应用）可见非
 *           Popup 顶层中客户面积最大者。全缺回落锚自身（键盘仍悬浮
 *           压过弹层，仅几何缩为弹层底部）。 */
static XWidget* xkb_resolveFloatingHost(XVirtualKeyboard* self, XWidget* host)
{
    XVector* tops;
    XWidget* best;
    int64_t bestArea;
    size_t n;
    size_t i;
    if (!xkb_isPopupTypeWindow(host)) return host; /* 锚即主窗口顶层。 */
    if (xkb_anchorCandidateOk(self, host, self->m_hostHint))
        return self->m_hostHint;
    if (xkb_anchorCandidateOk(self, host, self->m_host))
        return self->m_host;
    tops = XApplication_topLevelWidgets();
    if (!tops) return host;
    best = NULL;
    bestArea = -1;
    n = XVector_size_base((const XContainer*)tops);
    for (i = 0; i < n; ++i) {
        XWidget* w = XVector_At_Base(tops, (int64_t)i, XWidget*);
        int64_t area;
        if (!xkb_anchorCandidateOk(self, host, w)) continue;
        if (!XWidget_isVisible(w)) continue;
        area = (int64_t)XWidget_width(w) * (int64_t)XWidget_height(w);
        if (area > bestArea) {
            bestArea = area;
            best = w;
        }
    }
    XClassDelete((XClass*)tops);
    return best ? best : host;
}
#else /* !XAPPLICATION_ON */
static XWidget* xkb_resolveFloatingHost(XVirtualKeyboard* self, XWidget* host)
{
    (void)self;
    return host; /* 无应用顶层表：锚原样（调用方保证非 Popup 型）。 */
}
#endif /* XAPPLICATION_ON */

/** @brief 目标 destroyed 防悬垂槽：解绑并收层（守护 tick 解引用已销毁
 *         editor 即崩溃，此连接是前置防线）。 */
static void xkb_targetDestroyedSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)receiver;
    (void)args;
    if (!self) return;
    self->m_target = NULL;
    self->m_targetConn = NULL; /* 发送方连接表随销毁失效，句柄不可再断。 */
    XVirtualKeyboard_closePopup(self);
}

/** @brief 宿主 destroyed 防悬垂槽：清宿主、收层并摘挂归还单例。
 *  @details destroyed 发射于宿主 is_deleting_children 置位之前
 *           （XObject.c VXObject_deinit），此刻 setParent(NULL) 经
 *           prev_parent 分支（XVector_remove_base）把面板从宿主
 *           children 摘除，随后的子控件级联删除循环不再连带删除堆
 *           面板——面板存活并归还 XGuiApplication 惰性单例所有
 *           （m_virtualKeyboard 未清，后续按压经 virtualKeyboard()
 *           拿到的仍是活对象），下次 popup() 无条件
 *           bindHostDestroyed+setParent 自动重挂新宿主（幂等）。 */
static void xkb_hostDestroyedSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)receiver;
    (void)args;
    if (!self) return;
    self->m_host = NULL;
    self->m_hostConn = NULL; /* 发送方连接表随销毁失效，句柄不可再断。 */
    /* 几何信号宿主侧同款处置（连接表随销毁失效，置 NULL 不可再断）；
       范围顶层若仍存活由下方 closePopup→bind(false) 真断。 */
    self->m_geomConn[0] = NULL;
    self->m_geomConn[1] = NULL;
    self->m_geomConn[2] = NULL;
    self->m_geomConn[3] = NULL;
    self->m_hostW = 0;
    self->m_hostH = 0;
    XVirtualKeyboard_closePopup(self);
    /* 摘挂归还：脱离濒死宿主的 children 列表，躲过级联删除；同时
       归位顶层形态并重注册顶层表（XWidget_setParent NULL 分支）。 */
    XWidget_setParent((XWidget*)self, NULL, 0);
}

/* ==================== 键盘主题化（单一 LVGL 路径；原
 *                       XKEYBOARD_THEME_LVGL_ON 双分支已合并） ==================== */
/* LVGL 9.2.2 默认主题浅色模板色板（lv_theme_default.c 浅色口径；全部
 * 收在键盘自身局部常量，不动全局调色板。标注"派生值"者为按
 * lv_color_mix/lv_color_darken 公式逐通道手算，未运行时取色验证）：
 * SCR=0xF5F5F5、CARD/白键=0xFFFFFF、TEXT=0x212121、GREY=0xE0E0E0、
 * 主色=0x2196F3（lv_palette BLUE，仅局部常量）。 */
/* 中/EN 键动态键帽（搜狗口径：键帽随中英态渲染当前态，命中/路由恒匹
   配槽位表标签 XKEYBOARD_LBL_IME 不变）。 */
#define XKB_IME_KEY_CN "\xE4\xB8\xAD" /* 中 */
#define XKB_IME_KEY_EN "EN"

#define XKB_LVGL_SCR           0xFFF5F5F5u /* 面板底（lighten(GREY,4)）。 */
#define XKB_LVGL_CARD          0xFFFFFFFFu /* 键面/候选带卡底（card）。 */
#define XKB_LVGL_TEXT          0xFF212121u /* 键文字（darken(GREY,4)）。 */
#define XKB_LVGL_GREY          0xFFE0E0E0u /* checked 键底/灰边（lighten(GREY,2)）。 */
#define XKB_LVGL_PRIMARY       0xFF2196F3u /* 主色（默认 BLUE）。 */
#define XKB_LVGL_PRIMARY_MUTED 0xFFD2EAFCu /* 主色@20%opa 白卡合成（派生值；按本模板 LV_COLOR_MIX_ROUND_OFS=0 复算——旧值 0xD3EAFD 为 OFS=128 四舍五入口径，Δ1/255）。 */
#define XKB_LVGL_PRESSED_FACE  0xFFDCDCDCu /* pressed=darken(c,35) 白键派生值。 */
#define XKB_LVGL_PRESSED_TEXT  0xFF1C1C1Cu /* pressed 文字同步压暗（派生值）。 */
#define XKB_LVGL_DISABLED_TEXT 0xFF808080u /* disabled=向 GREY 混 LV_OPA_50=127（派生值；OFS 无关恒 0x808080——旧值 0x818181 需 OPA=128+OFS=128，与 lv_color.h 口径矛盾）。 */
#define XKB_LVGL_ARROW_TEXT    0xFF616161u /* 翻页箭头文字灰（派生值）。 */
#define XKB_ACCENT             0xFF3F7CEFu /* 搜狗蓝强调色（搜索键面）。 */
#define XKB_ACCENT_PRESSED     0xFF3263BFu /* 强调色按压态（darken 20% 派生值）。 */
#define XKB_ACCENT_TEXT        0xFFFFFFFFu /* 强调色键面文字（白字）。 */

/** @brief 键圆角半径（rowH>=44px 取 8、否则 4——≈12dp/8dp@130dpi 的
 *         像素取整口径，对齐 LVGL RADIUS_DEFAULT 12/8dp+小屏减半）。 */
static int xkb_keyRadius(int rowH)
{
    return rowH >= 44 ? 8 : 4;
}

/**
 * @brief 键面/候选带矩形绘制：圆角矩形一次成形（画刷填充+画笔描边）。
 * @details XPAINTER_SHAPE_ON 开且矩形足够容纳圆角时走
 *          XPainter_drawRoundedRect；SHAPE=0 或矩形过小回退 fillRect
 *          方角 + 1px 描边（功能不缺）。border==fill 时无可见描边
 *          （LVGL 键面无 border、无阴影的口径）。
 */
static void xkb_roundRect(XPainter* painter, const XRect* rect,
                          uint32_t fill, uint32_t border, int radius)
{
#if XPAINTER_SHAPE_ON
    if (radius > 0 && rect->width > 2 * radius && rect->height > 2 * radius) {
        XPainter_setBrush(painter, fill);
        XPainter_setPen(painter, border);
        XPainter_drawRoundedRect(painter, rect, radius, radius);
        return;
    }
#else
    (void)radius;
#endif
    XPainter_fillRect(painter, rect, fill);
    if (border != fill) {
        XRect edge = *rect;
        edge.height = 1;
        XPainter_fillRect(painter, &edge, border);
        edge = *rect;
        edge.y = rect->y + rect->height - 1;
        edge.height = 1;
        XPainter_fillRect(painter, &edge, border);
        edge = *rect;
        edge.width = 1;
        XPainter_fillRect(painter, &edge, border);
        edge = *rect;
        edge.x = rect->x + rect->width - 1;
        edge.width = 1;
        XPainter_fillRect(painter, &edge, border);
    }
}

/* ==================== 键盘顶部菜单条（Sogou 改版二阶段；键区上方常
 *                       驻，双模：组串态=拼音候选带 / 空闲态=图标工具
 *                       栏。条矩形=m_menuBarRect，xkb_rebuildLayout
 *                       预留） ==================== */

/** @brief 菜单条内缩边距（px；图标盒与条边的呼吸空间）。 */
#define XKB_MENU_PAD 3
/** @brief 工具栏图标盒边长上限（px；随条高缩放钳位——超高条不无限
 *         放大图标，对标搜狗工具栏图标近恒定尺寸）。 */
#define XKB_MENU_ICON_MAX 26

/** @brief 菜单条工具栏槽位（对标安卓搜狗工具栏四键序：悬浮切换/键盘
 *         选择/文字编辑/收起；四等分热区）。 */
typedef enum XkbToolBarSlot
{
    XKB_TOOL_FLOAT = 0,    /**< 悬浮切换（本阶段桩）。 */
    XKB_TOOL_LAYOUT = 1,   /**< 键盘选择（本阶段桩）。 */
    XKB_TOOL_EDIT = 2,     /**< 文字编辑（本阶段桩）。 */
    XKB_TOOL_COLLAPSE = 3, /**< 收起（XVirtualKeyboard_closePopup 直连）。 */
    XKB_TOOL_COUNT = 4     /**< 工具栏槽数（四等分）。 */
} XkbToolBarSlot;

/** @brief 菜单条内部子区域几何（布局/绘制/命中三方共用同一几何函数
 *        产出，保证所见即所点）。 */
typedef struct XkbMenuBarGeo
{
    XRect bar;                      /**< 整条矩形（=m_menuBarRect，控件
                                         局部坐标）。 */
    XRect cell[XKB_TOOL_COUNT];     /**< 四等分热区（图标格内居中；末格
                                         含条宽余数，命中/绘制同源）。 */
} XkbMenuBarGeo;

#if XVIRTUALKEYBOARD_ON
/** @brief 菜单条候选带模态判定：拼音中文态且组串非空（preedit 非空）
 *         且带矩形在场（组串态时=条矩形）——条渲染为既有拼音候选带
 *         （「正在输入的字母和匹配到的中文」替换菜单）；否则渲染为图
 *         标工具栏。 */
static bool xkb_menuBarBandMode(const XVirtualKeyboard* self)
{
    return self && self->m_menuBarRect.height > 0 &&
           self->m_imeBandRect.height > 0 && xkb_imeComposing(self);
}
#endif

/**
 * @brief 菜单条几何计算（paint 与 hit 共用；条缺席时全部子区间清零）。
 * @details 四等分对标搜狗工具栏排布：第 i 格=[条x+条宽*i/4,
 *          条x+条宽*(i+1)/4)，格高=整条高（边界累进式，末格含余数）；
 *          格宽=contentW/4 恒 ≥ 键宽/4（款型主表行单位总数 ≥4，全键
 *          行 1 单位键宽=contentW/10），热区居中即图标格。
 */
static void xkb_menuBarLayout(const XVirtualKeyboard* self, XkbMenuBarGeo* geo)
{
    int i;
    if (!geo) return;
    XRect_init(&geo->bar, 0, 0, 0, 0);
    for (i = 0; i < XKB_TOOL_COUNT; ++i) XRect_init(&geo->cell[i], 0, 0, 0, 0);
    if (!self || self->m_menuBarRect.height <= 0 ||
        self->m_menuBarRect.width <= 0)
        return;
    geo->bar = self->m_menuBarRect;
    {
        int x = geo->bar.x;
        for (i = 0; i < XKB_TOOL_COUNT; ++i) {
            int xEnd = geo->bar.x +
                       (int)(((int64_t)geo->bar.width * (i + 1)) /
                             XKB_TOOL_COUNT);
            geo->cell[i].x = x;
            geo->cell[i].y = geo->bar.y;
            geo->cell[i].width = xEnd - x;
            geo->cell[i].height = geo->bar.height;
            x = xEnd;
        }
    }
}

/** @brief 菜单条命中 → 工具栏槽位（条内必返 0..3；条外返回 -1）。与
 *         xkb_menuBarLayout 同源边界（所见即所点）。 */
static int xkb_menuBarCellAt(const XVirtualKeyboard* self, int x, int y)
{
    XkbMenuBarGeo geo;
    int i;
    xkb_menuBarLayout(self, &geo);
    if (geo.bar.width <= 0 || geo.bar.height <= 0) return -1;
    if (x < geo.bar.x || x >= geo.bar.x + geo.bar.width ||
        y < geo.bar.y || y >= geo.bar.y + geo.bar.height)
        return -1;
    for (i = 0; i < XKB_TOOL_COUNT; ++i) {
        if (x < geo.cell[i].x + geo.cell[i].width) return i;
    }
    return XKB_TOOL_COUNT - 1;
}

/** @brief 工具栏图标热区判定（紧凑悬浮态拖移分流用）：热区=格内居中
 *         图标盒（与 xkb_menuBarPaint 绘制盒同源公式）外扩 2px 容差，
 *         盒外格内空白=拖移起点（压在图标上不启动拖动）。 */
static bool xkb_menuBarIconHot(const XVirtualKeyboard* self, int slot,
                               int x, int y)
{
    XkbMenuBarGeo geo;
    int box;
    int ix;
    int iy;
    if (!self || slot < 0 || slot >= XKB_TOOL_COUNT) return false;
    xkb_menuBarLayout(self, &geo);
    if (geo.bar.height <= 0 || geo.cell[slot].width <= 0) return false;
    box = geo.bar.height - 2 * XKB_MENU_PAD;
    if (box < 8) box = 8;
    if (box > XKB_MENU_ICON_MAX) box = XKB_MENU_ICON_MAX;
    ix = geo.cell[slot].x + (geo.cell[slot].width - box) / 2 - 2;
    iy = geo.cell[slot].y + (geo.cell[slot].height - box) / 2 - 2;
    return x >= ix && x < ix + box + 4 && y >= iy && y < iy + box + 4;
}

/**
 * @brief 菜单条工具栏动作（收起=closePopup 直连；其余三键本阶段桩，
 *        后续阶段填实）。
 */
/** @brief 进入紧凑悬浮态（工具栏「悬浮切换」；第三形态 compactFloat）。
 *  @details 停靠态（弹出的子控件浮层）原位转独立顶层 Popup——复用既有
 *           悬浮形态转换路径 setParent(NULL,0) 归位顶层 +
 *           setWindowFlags(Popup)（popup() 悬浮分支同款）；尺寸=宽
 *           min(宿主宽45%,420)、高按同比例缩放（docked 高×宽比不变，
 *           保持 行数+工具栏 比例）；落位宿主右下角（宿主几何读
 *           XWindow 记账，reposition 同款口径）。进入后
 *           xkb_reposition 的宿主几何跟随被 m_compactFloat 守卫跳过
 *           （不抢几何，用户可拖移）；守护 tick 顶层 raise 维护继续
 *           生效。与 m_floating/setHostWindow 悬浮锚形态互斥（锚形态
 *           已是独立顶层，本态不适用）；键盘选择/文字编辑面板随转换
 *           关闭（互斥面板本地 UI）。 */
static void xkb_compactFloatEnter(XVirtualKeyboard* self)
{
    XRect r;
    XPoint hostGlobal;
    XWindow* hostWin;
    int hostW;
    int hostH;
    int dockedH;
    int compactW;
    int compactH;
    if (!self || !self->m_popped || !self->m_host || self->m_floating)
        return; /* 未弹出/悬浮锚形态不适用（第三形态与①悬浮锚互斥）。 */
    self->m_layoutSelectorOpen = false; /* 面板本地 UI 随转换关。 */
    self->m_editPanelOpen = false;
    self->m_editSelArmed = false;
    /* 转独立顶层 Popup（popup() 悬浮分支同款转换路径）。子→顶层重挂
       会同步走一拍 HIDE（hideEvent 复位弹层位/面板/紧凑位/按压态并弃
       组串草稿——与收层链同口径），运行态在 show 之后统一回填（popup
       同款：m_popped 在 show 后置位）。 */
    XWidget_setParent((XWidget*)self, NULL, 0);
    XWidget_setWindowFlags((XWidget*)self, (XWidgetFlags)XWindowType_Popup);
    XWidget_show((XWidget*)self);
    self->m_popped = true;
    self->m_compactFloat = true;
    self->m_compactDrag = false;
    /* 尺寸：宽=min(宿主宽45%,420)，高按同比例（保持 行数+工具栏 比例
       ——docked 宽=宿主宽，比值=compactW/hostW）。 */
    hostW = self->m_hostW > 0 ? self->m_hostW
                              : XWidget_width(self->m_host);
    if (hostW < 1) hostW = 1;
    hostH = self->m_hostH > 0 ? self->m_hostH
                              : XWidget_height(self->m_host);
    if (hostH < 1) hostH = 1;
    dockedH = XWidget_height((XWidget*)self);
    compactW = hostW * 45 / 100;
    if (compactW > 420) compactW = 420;
    if (compactW < 1) compactW = 1;
    compactH = dockedH * compactW / hostW;
    if (compactH < 1) compactH = 1;
    /* 落位宿主右下角。 */
    hostWin = (XWindow*)XWidget_windowHandle(self->m_host);
    if (hostWin) {
        XPoint hp = XWindow_position(hostWin);
        hostGlobal.x = hp.x;
        hostGlobal.y = hp.y;
    } else {
        XPoint origin;
        origin.x = 0;
        origin.y = 0;
        hostGlobal = XWidget_mapToGlobal(self->m_host, &origin);
    }
    XRect_init(&r, hostGlobal.x + hostW - compactW,
               hostGlobal.y + hostH - compactH, compactW, compactH);
    XWidget_setGeometryRect((XWidget*)self, &r);
    /* 顶层无宿主帧泵：主动补首帧上屏（popup() 悬浮分支同款）。 */
    XWidget_flushBackingStore((XWidget*)self, NULL);
    XWidget_raise((XWidget*)self);
}

/** @brief 退出紧凑悬浮态：回宿主底部全宽停靠（既有内嵌几何）。
 *  @details setParent(host,0) 归位子控件浮层形态并清 Popup 窗口类型
 *           （popup() ③ 默认分支同款——setParent 的 flags 参数即窗口
 *           类型位），几何交还 xkb_reposition（m_compactFloat 守卫先
 *           解，宿主底部全宽停靠公式照旧）；拖移态一并复位。 */
static void xkb_compactFloatExit(XVirtualKeyboard* self)
{
    if (!self || !self->m_compactFloat) return;
    self->m_compactFloat = false;
    self->m_compactDrag = false;
    /* 顶层→子控件重挂同样伴随一拍 HIDE（同进入路径），popped 位在
       show 之后回填（重弹层位，守护/命中链继续视作弹出态）。 */
    if (self->m_host)
        XWidget_setParent((XWidget*)self, self->m_host, 0);
    xkb_reposition(self);
    XWidget_show((XWidget*)self);
    self->m_popped = true;
    XWidget_raise((XWidget*)self);
    XWidget_update((XWidget*)self);
}

static void xkb_toggleCompactFloat(XVirtualKeyboard* self)
{
    /* 紧凑悬浮态切换（Sogou 改版四阶段）：停靠态→独立顶层紧凑悬浮，
       紧凑悬浮→回宿主底部全宽停靠。 */
    if (!self) return;
    if (self->m_compactFloat)
        xkb_compactFloatExit(self);
    else
        xkb_compactFloatEnter(self);
    XWidget_update((XWidget*)self);
}

static void xkb_openLayoutSelector(XVirtualKeyboard* self)
{
    /* 键盘选择面板开合（Sogou 改版三阶段）：面板开启期的工具栏点击
       已被 xkb_layoutSelectorHit 先行消费关闭，本入口实际只在关态被
       触达=开；取反形态保留幂等哨（面板外关闭链未达的路径兜底）。
       与文字编辑面板互斥（后开者胜）。 */
    if (!self) return;
    self->m_editPanelOpen = false;
    self->m_editSelArmed = false;
    self->m_layoutSelectorOpen = !self->m_layoutSelectorOpen;
    XWidget_update((XWidget*)self);
}

static void xkb_openEditPanel(XVirtualKeyboard* self)
{
    /* 文字编辑面板开合（Sogou 改版三阶段收尾）：编辑面板只覆盖键区，
       菜单条点击照常直达本入口——再点图标即关（返回路径之二）。与
       键盘选择面板互斥（后开者胜）。 */
    if (!self) return;
    self->m_layoutSelectorOpen = false;
    self->m_editPanelOpen = !self->m_editPanelOpen;
    if (!self->m_editPanelOpen) self->m_editSelArmed = false;
    XWidget_update((XWidget*)self);
}

/** @brief 工具栏槽位动作分发（释放触发单点；滑动出格取消不触发）。 */
static void xkb_menuBarActivate(XVirtualKeyboard* self, int slot)
{
    if (!self || slot < 0 || slot >= XKB_TOOL_COUNT) return;
    switch ((XkbToolBarSlot)slot) {
    case XKB_TOOL_FLOAT:
        xkb_toggleCompactFloat(self);
        break;
    case XKB_TOOL_LAYOUT:
        xkb_openLayoutSelector(self);
        break;
    case XKB_TOOL_EDIT:
        xkb_openEditPanel(self);
        break;
    case XKB_TOOL_COLLAPSE:
        XVirtualKeyboard_closePopup(self); /* 收起直连（规格口径）。 */
        break;
    default:
        break;
    }
}

/**
 * @brief 菜单条图标工具栏绘制（悬浮切换/键盘选择/文字编辑/收起）。
 * @details 条面=候选带同款卡白+灰边圆角（条带外观一致）；四图标画笔
 *          线条/矩形自绘、随条高缩放（图标盒=条内高钳 [8,26]，格内居
 *          中）；按压态视觉与键面一致（格底 PRESSED_FACE 圆角 + 图标
 *          PRESSED_TEXT，颜色数学同 LVGL 按压键面）。图标序对标搜狗：
 *          悬浮切换=画中画双矩形、键盘选择=面板+键点阵、文字编辑=文
 *          档行+铅笔、收起=下箭头。
 */
static void xkb_menuBarPaint(XVirtualKeyboard* self, XPainter* painter)
{
    XkbMenuBarGeo geo;
    int radius;
    int box;
    int i;
    if (!self || !painter || self->m_menuBarRect.height <= 0 ||
        self->m_menuBarRect.width <= 0)
        return;
    xkb_menuBarLayout(self, &geo);
    if (geo.bar.height <= 0 || geo.bar.width <= 0) return;
    radius = 8;
    if (radius > geo.bar.height / 2) radius = geo.bar.height / 2;
    xkb_roundRect(painter, &geo.bar, XKB_LVGL_CARD, XKB_LVGL_GREY, radius);
    box = geo.bar.height - 2 * XKB_MENU_PAD;
    if (box < 8) box = 8;
    if (box > XKB_MENU_ICON_MAX) box = XKB_MENU_ICON_MAX;
    for (i = 0; i < XKB_TOOL_COUNT; ++i) {
        XRect icon;
        bool pressed = (self->m_pressedTool == i);
        uint32_t ink = pressed ? XKB_LVGL_PRESSED_TEXT : XKB_LVGL_TEXT;
        if (geo.cell[i].width < box) continue; /* 退化格（条过窄）跳过。 */
        if (pressed) {
            /* 按压态格底（键面按压色，视觉与键面一致）。 */
            XRect face = geo.cell[i];
            face.x += 1;
            face.y += 1;
            face.width -= 2;
            face.height -= 2;
            xkb_roundRect(painter, &face, XKB_LVGL_PRESSED_FACE,
                          XKB_LVGL_PRESSED_FACE, 4);
        }
        icon.width = box;
        icon.height = box;
        icon.x = geo.cell[i].x + (geo.cell[i].width - box) / 2;
        icon.y = geo.cell[i].y + (geo.cell[i].height - box) / 2;
        XPainter_setPen(painter, ink);
        XPainter_setPenWidth(painter, 1);
        XPainter_setBrush_2(painter, XPainterBrushStyle_NoBrush);
        switch ((XkbToolBarSlot)i) {
        case XKB_TOOL_FLOAT: {
            /* 画中画：外框 + 右下小实块（悬浮/压缩切换）。 */
            XPainter_drawRect(painter, &icon);
            {
                XRect pip;
                int pipW = box / 3;
                if (pipW < 3) pipW = 3;
                pip.x = icon.x + icon.width - XKB_MENU_PAD - pipW;
                pip.y = icon.y + icon.height - XKB_MENU_PAD - pipW;
                pip.width = pipW;
                pip.height = pipW;
                XPainter_fillRect(painter, &pip, ink);
            }
            break;
        }
        case XKB_TOOL_LAYOUT: {
            /* 键盘面板：外圆角框 + 两行键点阵。 */
            XPainter_drawRect(painter, &icon);
            {
                int kw = box / 5;
                int row;
                if (kw < 2) kw = 2;
                for (row = 0; row < 2; ++row) {
                    int col;
                    for (col = 0; col < 3; ++col) {
                        XRect key;
                        key.width = kw;
                        key.height = kw;
                        key.x = icon.x + XKB_MENU_PAD +
                                col * (box - 2 * XKB_MENU_PAD - kw) / 2;
                        key.y = icon.y + XKB_MENU_PAD +
                                row * (box - 2 * XKB_MENU_PAD - kw);
                        XPainter_fillRect(painter, &key, ink);
                    }
                }
            }
            break;
        }
        case XKB_TOOL_EDIT: {
            /* 文字编辑：两行文档线 + 斜置铅笔（右上线身+左下笔尖）。 */
            XRect line = icon;
            line.height = 1;
            line.width = box / 2;
            line.x = icon.x;
            line.y = icon.y + box / 4;
            XPainter_fillRect(painter, &line, ink);
            line.y = icon.y + box / 2;
            XPainter_fillRect(painter, &line, ink);
            XPainter_drawLine(painter, icon.x + box / 3,
                              icon.y + icon.height - XKB_MENU_PAD,
                              icon.x + icon.width - XKB_MENU_PAD,
                              icon.y + XKB_MENU_PAD);
            XPainter_drawLine(painter, icon.x + box / 3,
                              icon.y + icon.height - XKB_MENU_PAD,
                              icon.x + box / 3 + box / 6,
                              icon.y + icon.height - XKB_MENU_PAD);
            break;
        }
        case XKB_TOOL_COLLAPSE: {
            /* 收起：下箭头（左上→中下→右上折线）。 */
            XPoint pts[3];
            pts[0].x = icon.x + box / 4;
            pts[0].y = icon.y + box / 3;
            pts[1].x = icon.x + icon.width / 2;
            pts[1].y = icon.y + icon.height - box / 4;
            pts[2].x = icon.x + icon.width - box / 4;
            pts[2].y = icon.y + box / 3;
            XPainter_drawPolyline(painter, pts, 3);
            break;
        }
        default:
            break;
        }
    }
}

/**
 * @brief 菜单条命中（mousePressEvent 在 xkb_hitTest 之前调用）。
 * @details 双模分流：组串态（xkb_menuBarBandMode）委托既有候选带命中
 *          （xkb_imeBandHit，press 直触发不进武装链）；空闲态=工具栏
 *          ——条内点击一律消费（不透键区），按压武装（m_pressedTool=
 *          槽位，按压态视觉与键面一致）+ 隐式抓取，释放落在原格才触发
 *          （xkb_menuBarActivate，滑动出格取消），与 CLICK_TRIG 键同
 *          时序。放行条件：条缺席（未布局/零高）返回 false。
 * @return 命中条返回 true（事件已消费）；未命中返回 false。
 */
static bool xkb_menuBarHit(XVirtualKeyboard* self, int x, int y)
{
    int slot;
    if (!self || self->m_menuBarRect.height <= 0 ||
        self->m_menuBarRect.width <= 0)
        return false;
    if (x < self->m_menuBarRect.x ||
        x >= self->m_menuBarRect.x + self->m_menuBarRect.width ||
        y < self->m_menuBarRect.y ||
        y >= self->m_menuBarRect.y + self->m_menuBarRect.height)
        return false;
#if XVIRTUALKEYBOARD_ON
    if (xkb_menuBarBandMode(self)) return xkb_imeBandHit(self, x, y);
#endif
    slot = xkb_menuBarCellAt(self, x, y);
    if (slot < 0) return false;
    /* 紧凑悬浮态（第三形态）拖移分流：压中图标热区→工具栏动作（按
       压武装释放触发照旧）；压条内空白（图标盒之外）→启动拖移——记
       全局锚点偏移（按下时全局-窗口左上）+隐式抓取，move 求 delta 移
       窗，release 结束（压在图标上不启动拖动）。 */
    if (self->m_compactFloat && !xkb_menuBarIconHot(self, slot, x, y)) {
        XPoint local;
        XPoint g;
        local.x = (short)x;
        local.y = (short)y;
        g = XWidget_mapToGlobal((XWidget*)self, &local);
        self->m_compactDrag = true;
        self->m_dragOffX = g.x - XWidget_x((XWidget*)self);
        self->m_dragOffY = g.y - XWidget_y((XWidget*)self);
        XWidget_grabMouse((XWidget*)self);
        return true;
    }
    self->m_pressedTool = slot; /* 按压武装（释放触发；视觉随 paint）。 */
    XWidget_grabMouse((XWidget*)self);
    return true;
}

/* ==================== 键盘选择面板（Sogou 改版三阶段；工具栏「键盘
 *                       选择」图标开合，面板内部自绘覆盖键区，非独立
 *                       窗口。行序=款型序，命中/绘制共用同一几何） ==== */

/** @brief 选择面板行数（拼音全键/拼音九键/英文全键）。 */
#define XKB_SELECTOR_ROW_COUNT 3
/** @brief 选择面板行 0 标签（拼音全键）。 */
#define XKB_SELECTOR_LBL_ROW0 \
    "\xE6\x8B\xBC\xE9\x9F\xB3\xE5\x85\xA8\xE9\x94\xAE" /* 拼音全键 */
/** @brief 选择面板行 1 标签（拼音九键）。 */
#define XKB_SELECTOR_LBL_ROW1 \
    "\xE6\x8B\xBC\xE9\x9F\xB3\xE4\xB9\x9D\xE9\x94\xAE" /* 拼音九键 */
/** @brief 选择面板行 2 标签（英文全键）。 */
#define XKB_SELECTOR_LBL_ROW2 \
    "\xE8\x8B\xB1\xE6\x96\x87\xE5\x85\xA8\xE9\x94\xAE" /* 英文全键 */

/** @brief 选择面板行→款型映射（行序=面板自上而下，对标任务书列序）。 */
static const XKeyboardLayoutKind kSelectorRowKinds[XKB_SELECTOR_ROW_COUNT] = {
    XKeyboardLayout_PinyinFull, XKeyboardLayout_PinyinT9,
    XKeyboardLayout_EnglishFull
};
/** @brief 选择面板行标签（与 kSelectorRowKinds 一一对应）。 */
static const char* const kSelectorRowLabels[XKB_SELECTOR_ROW_COUNT] = {
    XKB_SELECTOR_LBL_ROW0, XKB_SELECTOR_LBL_ROW1, XKB_SELECTOR_LBL_ROW2
};

/** @brief 选择面板内部子区域几何（布局/绘制/命中三方共用同一几何函数
 *        产出，保证所见即所点）。 */
typedef struct XkbLayoutSelectorGeo
{
    XRect panel;                   /**< 整面板矩形（键区=菜单条下方内容
                                        区，覆盖键位不改窗口）。 */
    XRect row[XKB_SELECTOR_ROW_COUNT]; /**< 三行选项（边界累进，末行含
                                        余数）。 */
} XkbLayoutSelectorGeo;

/** @brief 选择面板几何计算（paint 与 hit 共用；面板缺席时全清零）。 */
static void xkb_layoutSelectorLayout(const XVirtualKeyboard* self,
                                     XkbLayoutSelectorGeo* geo)
{
    int i;
    if (!geo) return;
    XRect_init(&geo->panel, 0, 0, 0, 0);
    for (i = 0; i < XKB_SELECTOR_ROW_COUNT; ++i)
        XRect_init(&geo->row[i], 0, 0, 0, 0);
    if (!self || !self->m_layoutSelectorOpen ||
        self->m_menuBarRect.height <= 0 || self->m_menuBarRect.width <= 0)
        return;
    /* 面板=键区（菜单条下方内容区）：宽同条、高=内容高-条高。 */
    geo->panel.x = self->m_menuBarRect.x;
    geo->panel.y = self->m_menuBarRect.y + self->m_menuBarRect.height;
    geo->panel.width = self->m_menuBarRect.width;
    geo->panel.height = XWidget_height((XWidget*)self) - 4 - geo->panel.y;
    if (geo->panel.height < 0) geo->panel.height = 0;
    {
        int y = geo->panel.y;
        for (i = 0; i < XKB_SELECTOR_ROW_COUNT; ++i) {
            int yEnd = geo->panel.y +
                       (int)(((int64_t)geo->panel.height * (i + 1)) /
                             XKB_SELECTOR_ROW_COUNT);
            geo->row[i].x = geo->panel.x;
            geo->row[i].y = y;
            geo->row[i].width = geo->panel.width;
            geo->row[i].height = yEnd - y;
            y = yEnd;
        }
    }
}

/** @brief 面板命中 → 行下标（面板内必返 0..2；面板外返回 -1）。与
 *         xkb_layoutSelectorLayout 同源边界（所见即所点）。 */
static int xkb_layoutSelectorRowAt(const XVirtualKeyboard* self, int x, int y)
{
    XkbLayoutSelectorGeo geo;
    int i;
    xkb_layoutSelectorLayout(self, &geo);
    if (geo.panel.width <= 0 || geo.panel.height <= 0) return -1;
    if (x < geo.panel.x || x >= geo.panel.x + geo.panel.width ||
        y < geo.panel.y || y >= geo.panel.y + geo.panel.height)
        return -1;
    for (i = 0; i < XKB_SELECTOR_ROW_COUNT; ++i) {
        if (y < geo.row[i].y + geo.row[i].height) return i;
    }
    return XKB_SELECTOR_ROW_COUNT - 1;
}

/**
 * @brief 选择面板命中（mousePressEvent 在菜单条/键区命中之前调用）。
 * @details 面板开启期点击一律消费：行内=点选项 setLayoutKind 并关闭
 *          （IME 启用且停 User1 槽位的拼音会话形态时随行落 TextLower
 *          ——款型主表即刻可见，九键路由/英文直写判据都以款型为准，
 *          引擎与落地契约连接不重启）；面板外（含菜单条/再点工具栏
 *          图标）=关闭——两向都吃掉本拍点击，不透键区不重触发。
 * @return 面板开启（事件已消费）返回 true；面板关闭返回 false。
 */
static bool xkb_layoutSelectorHit(XVirtualKeyboard* self, int x, int y)
{
    int row;
    if (!self || !self->m_layoutSelectorOpen) return false;
    row = xkb_layoutSelectorRowAt(self, x, y);
    if (row >= 0) {
        XVirtualKeyboard_setLayoutKind(self, kSelectorRowKinds[row]);
#if XVIRTUALKEYBOARD_ON
        if (self->m_imeEnabled && self->m_mode == XKeyboardMode_User1)
            XVirtualKeyboard_setMode(self, XKeyboardMode_TextLower);
#endif
    }
    self->m_layoutSelectorOpen = false; /* 行外=面板外点击：关闭。 */
    XWidget_update((XWidget*)self);
    return true;
}

/** @brief 选择面板绘制（覆盖键区最后绘制：整面先铺面板底色盖住底层
 *         键帽——三行卡片间/四周缝隙透出键位残影的割裂感根修，口径同
 *         编辑面板面板底；三行卡白圆角水平内缩 8px 成面板上的浮卡，
 *         当前端型行 GREY 高亮+左侧勾线，标签行内居中；勾线画笔自绘，
 *         字库无 ✓ 字形口径同键位文案）。 */
static void xkb_layoutSelectorPaint(XVirtualKeyboard* self, XPainter* painter)
{
    XkbLayoutSelectorGeo geo;
    int i;
    if (!self || !painter || !self->m_layoutSelectorOpen) return;
    xkb_layoutSelectorLayout(self, &geo);
    if (geo.panel.width <= 0 || geo.panel.height <= 0) return;
    /* 面板底=SCR 平涂（覆盖键区，先于行卡——编辑面板同款口径）。 */
    XPainter_fillRect(painter, &geo.panel, XKB_LVGL_SCR);
    for (i = 0; i < XKB_SELECTOR_ROW_COUNT; ++i) {
        XRect face = geo.row[i];
        bool checked = (self->m_layoutKind == kSelectorRowKinds[i]);
        uint32_t fill = checked ? XKB_LVGL_GREY : XKB_LVGL_CARD;
        int radius = 8;
        if (radius > face.height / 2) radius = face.height / 2;
        if (face.width < 25 || face.height < 5) continue;
        /* 水平内缩 8px（浮卡感，命中仍为整行面板宽——所见即所点为
           超集）；键面同款 -5 收边（填充+描边合成可视面四边内缩
           2px，见键位绘制）。 */
        face.x += 10;
        face.y += 2;
        face.width -= 20;
        face.height -= 5;
        xkb_roundRect(painter, &face, fill, fill, radius);
        XPainter_drawTextRect(painter, &face,
                              XPAINTER_TEXT_ALIGN_CENTER |
                                  XPAINTER_TEXT_SINGLE_LINE,
                              kSelectorRowLabels[i], XKB_LVGL_TEXT);
        if (checked) {
            /* 当前项勾线（行首方心内的折线勾，随行高缩放钳位）。 */
            XPoint pts[3];
            int box = face.height / 3;
            int cx = geo.row[i].x + geo.row[i].height / 2;
            int cy = face.y + face.height / 2;
            if (box < 6) box = 6;
            if (box > 18) box = 18;
            pts[0].x = cx - box / 2;
            pts[0].y = cy;
            pts[1].x = cx - box / 6;
            pts[1].y = cy + box / 3;
            pts[2].x = cx + box / 2;
            pts[2].y = cy - box / 3;
            XPainter_setPen(painter, XKB_LVGL_PRIMARY);
            XPainter_drawPolyline(painter, pts, 3);
        }
    }
}

/* ==================== 文字编辑面板（Sogou 改版三阶段收尾；工具栏「文
 *                       字编辑」图标开合，面板本地自绘覆盖键区：标题行
 *                       +主区白底方向区+右列操作列） ==================== */

/** @brief 编辑面板标题（左上）。 */
#define XKB_EDIT_LBL_TITLE \
    "\xE6\x96\x87\xE5\xAD\x97\xE7\xBC\x96\xE8\xBE\x91" /* 文字编辑 */
/** @brief 编辑面板「开始选择」键标签。 */
#define XKB_EDIT_LBL_SEL \
    "\xE5\xBC\x80\xE5\xA7\x8B\xE9\x80\x89\xE6\x8B\xA9" /* 开始选择 */
/** @brief 编辑面板「全选」键标签。 */
#define XKB_EDIT_LBL_ALL "\xE5\x85\xA8\xE9\x80\x89" /* 全选 */
/** @brief 编辑面板「复制」键标签。 */
#define XKB_EDIT_LBL_COPY "\xE5\xA4\x8D\xE5\x88\xB6" /* 复制 */
/** @brief 编辑面板「剪切」键标签。 */
#define XKB_EDIT_LBL_CUT "\xE5\x89\xAA\xE5\x88\x87" /* 剪切 */
/** @brief 编辑面板「粘贴」键标签。 */
#define XKB_EDIT_LBL_PASTE "\xE7\xB2\x98\xE8\xB4\xB4" /* 粘贴 */

/**
 * @brief 编辑面板动作槽位（布局对标搜狗文字编辑页：主区上↑/左←/开始
 *        选择/右→/下↓+底行 Home/全选/End；右列 退格/复制/剪切/粘贴）。
 */
typedef enum XkbEditPanelSlot
{
    XKB_EDIT_UP = 0,     /**< 上方向（选择态带 Shift）。 */
    XKB_EDIT_LEFT,       /**< 左方向。 */
    XKB_EDIT_SEL,        /**< 开始选择（面板本地选择态切换）。 */
    XKB_EDIT_RIGHT,      /**< 右方向。 */
    XKB_EDIT_DOWN,       /**< 下方向。 */
    XKB_EDIT_HOME,       /**< Home（选择态带 Shift）。 */
    XKB_EDIT_ALL,        /**< 全选（Ctrl+A）。 */
    XKB_EDIT_END,        /**< End（选择态带 Shift）。 */
    XKB_EDIT_BACKSPACE,  /**< ⌫ 退格（字库无 ⌫ 字形，键面用「退格」）。 */
    XKB_EDIT_COPY,       /**< 复制（公开 API 优先，回落 Ctrl+C）。 */
    XKB_EDIT_CUT,        /**< 剪切（公开 API 优先，回落 Ctrl+X）。 */
    XKB_EDIT_PASTE,      /**< 粘贴（公开 API 优先，回落 Ctrl+V）。 */
    XKB_EDIT_COUNT = 12  /**< 动作槽位总数。 */
} XkbEditPanelSlot;

/** @brief 编辑面板动作标签（与 XkbEditPanelSlot 一一对应；方向/退格
 *         标签复用既有键帽常量，字库无箭头字形口径同键位文案）。 */
static const char* const kEditPanelLabels[XKB_EDIT_COUNT] = {
    "\xE4\xB8\x8A",            /* 上 */
    XKEYBOARD_LBL_LEFT,        /* <- */
    XKB_EDIT_LBL_SEL,          /* 开始选择 */
    XKEYBOARD_LBL_RIGHT,       /* -> */
    "\xE4\xB8\x8B",            /* 下 */
    "Home",                    /* |← */
    XKB_EDIT_LBL_ALL,          /* 全选 */
    "End",                     /* →| */
    XKEYBOARD_LBL_BACKSPACE,   /* ⌫=退格 */
    XKB_EDIT_LBL_COPY,         /* 复制 */
    XKB_EDIT_LBL_CUT,          /* 剪切 */
    XKB_EDIT_LBL_PASTE         /* 粘贴 */
};

/** @brief 编辑面板内部子区域几何（布局/绘制/命中三方共用同一几何函数
 *        产出，保证所见即所点）。 */
typedef struct XkbEditPanelGeo
{
    XRect panel;                  /**< 整面板矩形（键区，含标题行）。 */
    XRect title;                  /**< 标题行（左「文字编辑」）。 */
    XRect back;                   /**< 返回箭头热区（标题行右端方形）。 */
    XRect main;                   /**< 主区大块白底（左 3/4 宽）。 */
    XRect cell[XKB_EDIT_COUNT];   /**< 动作热区（主区 8 + 右列 4；空白
                                       格不算槽位不在场内）。 */
} XkbEditPanelGeo;

/** @brief 编辑面板几何计算（paint 与 hit 共用；面板缺席时全清零）。
 *  @details 面板=键区（菜单条下方内容区）；标题行高=面板高/6（随键行
 *           高缩放），返回热区=标题行右端方形；右列宽=面板宽/4，主区
 *           占余宽；主区 4 行（↑/←中→/↓/底行）×3 列（边界累进，空白
 *           格留白），右列 4 行（退格/复制/剪切/粘贴）。 */
static void xkb_editPanelLayout(const XVirtualKeyboard* self,
                                XkbEditPanelGeo* geo)
{
    int i;
    int titleH;
    int rightW;
    int gridY;
    int gridH;
    int mainW;
    if (!geo) return;
    XRect_init(&geo->panel, 0, 0, 0, 0);
    XRect_init(&geo->title, 0, 0, 0, 0);
    XRect_init(&geo->back, 0, 0, 0, 0);
    XRect_init(&geo->main, 0, 0, 0, 0);
    for (i = 0; i < XKB_EDIT_COUNT; ++i) XRect_init(&geo->cell[i], 0, 0, 0, 0);
    if (!self || !self->m_editPanelOpen || self->m_menuBarRect.height <= 0 ||
        self->m_menuBarRect.width <= 0)
        return;
    geo->panel.x = self->m_menuBarRect.x;
    geo->panel.y = self->m_menuBarRect.y + self->m_menuBarRect.height;
    geo->panel.width = self->m_menuBarRect.width;
    geo->panel.height = XWidget_height((XWidget*)self) - 4 - geo->panel.y;
    if (geo->panel.height < 1) geo->panel.height = 1;
    titleH = geo->panel.height / 6;
    if (titleH < 1) titleH = 1;
    geo->title.x = geo->panel.x;
    geo->title.y = geo->panel.y;
    geo->title.width = geo->panel.width;
    geo->title.height = titleH;
    geo->back.x = geo->panel.x + geo->panel.width - titleH;
    geo->back.y = geo->panel.y;
    geo->back.width = titleH;
    geo->back.height = titleH;
    gridY = geo->panel.y + titleH;
    gridH = geo->panel.height - titleH;
    rightW = geo->panel.width / 4;
    mainW = geo->panel.width - rightW;
    geo->main.x = geo->panel.x;
    geo->main.y = gridY;
    geo->main.width = mainW;
    geo->main.height = gridH;
    /* 主区 4 行×3 列（行高/列宽边界累进；主区键占格，空白格留白）。 */
    {
        int colX[4];
        int rowY[5];
        for (i = 0; i < 4; ++i)
            colX[i] = geo->main.x +
                      (int)(((int64_t)mainW * i) / 3);
        colX[3] = geo->main.x + mainW;
        for (i = 0; i <= 4; ++i)
            rowY[i] = gridY + (int)(((int64_t)gridH * i) / 4);
        geo->cell[XKB_EDIT_UP].x = colX[1];
        geo->cell[XKB_EDIT_UP].y = rowY[0];
        geo->cell[XKB_EDIT_UP].width = colX[2] - colX[1];
        geo->cell[XKB_EDIT_UP].height = rowY[1] - rowY[0];
        geo->cell[XKB_EDIT_LEFT].x = colX[0];
        geo->cell[XKB_EDIT_LEFT].y = rowY[1];
        geo->cell[XKB_EDIT_LEFT].width = colX[1] - colX[0];
        geo->cell[XKB_EDIT_LEFT].height = rowY[2] - rowY[1];
        geo->cell[XKB_EDIT_SEL].x = colX[1];
        geo->cell[XKB_EDIT_SEL].y = rowY[1];
        geo->cell[XKB_EDIT_SEL].width = colX[2] - colX[1];
        geo->cell[XKB_EDIT_SEL].height = rowY[2] - rowY[1];
        geo->cell[XKB_EDIT_RIGHT].x = colX[2];
        geo->cell[XKB_EDIT_RIGHT].y = rowY[1];
        geo->cell[XKB_EDIT_RIGHT].width = colX[3] - colX[2];
        geo->cell[XKB_EDIT_RIGHT].height = rowY[2] - rowY[1];
        geo->cell[XKB_EDIT_DOWN].x = colX[1];
        geo->cell[XKB_EDIT_DOWN].y = rowY[2];
        geo->cell[XKB_EDIT_DOWN].width = colX[2] - colX[1];
        geo->cell[XKB_EDIT_DOWN].height = rowY[3] - rowY[2];
        geo->cell[XKB_EDIT_HOME].x = colX[0];
        geo->cell[XKB_EDIT_HOME].y = rowY[3];
        geo->cell[XKB_EDIT_HOME].width = colX[1] - colX[0];
        geo->cell[XKB_EDIT_HOME].height = rowY[4] - rowY[3];
        geo->cell[XKB_EDIT_ALL].x = colX[1];
        geo->cell[XKB_EDIT_ALL].y = rowY[3];
        geo->cell[XKB_EDIT_ALL].width = colX[2] - colX[1];
        geo->cell[XKB_EDIT_ALL].height = rowY[4] - rowY[3];
        geo->cell[XKB_EDIT_END].x = colX[2];
        geo->cell[XKB_EDIT_END].y = rowY[3];
        geo->cell[XKB_EDIT_END].width = colX[3] - colX[2];
        geo->cell[XKB_EDIT_END].height = rowY[4] - rowY[3];
    }
    /* 右列 4 键（列宽=面板宽/4，行高边界累进）。 */
    {
        int rx = geo->panel.x + geo->panel.width - rightW;
        for (i = 0; i < 4; ++i) {
            int y0 = gridY + (int)(((int64_t)gridH * i) / 4);
            int y1 = gridY + (int)(((int64_t)gridH * (i + 1)) / 4);
            XRect* r = &geo->cell[XKB_EDIT_BACKSPACE + i];
            r->x = rx;
            r->y = y0;
            r->width = rightW;
            r->height = y1 - y0;
        }
    }
}

/** @brief 编辑面板动作可用性（规格：公开 API 与 Ctrl 合成双缺才禁用
 *         ——合成链只要有绑定目标即通，故禁用态=无绑定目标；「开始选
 *         择」为面板本地态恒可用）。 */
static bool xkb_editPanelEnabled(const XVirtualKeyboard* self, int slot)
{
    if (!self) return false;
    if (slot == XKB_EDIT_SEL) return true;
    return self->m_target != NULL;
}

/** @brief 复制/剪切/粘贴动作（公开 API 优先：三编辑控件 vtable 识别
 *         直调，各受 X*_ON 门控；未识别目标回落合成 Ctrl+C/X/V 虚键
 *         ——XLineControl/XTextControl 既有 Ctrl 组合处理链承接）。 */
static void xkb_editPanelClipboard(XVirtualKeyboard* self, int slot)
{
    XWidget* target;
    int key;
    if (!self || !self->m_target) return;
    target = self->m_target;
    if (slot == XKB_EDIT_COPY)
        key = XKey_C;
    else if (slot == XKB_EDIT_CUT)
        key = XKey_X;
    else
        key = XKey_V;
#if XLINEEDIT_ON
    if (XClassGetVtable(target) == XLineEdit_class_init()) {
        XLineEdit* le = (XLineEdit*)target;
        if (slot == XKB_EDIT_COPY)
            XLineEdit_copy(le);
        else if (slot == XKB_EDIT_CUT)
            XLineEdit_cut(le);
        else
            XLineEdit_paste(le);
        return;
    }
#endif
#if XPLAINTEXTEDIT_ON
    if (XClassGetVtable(target) == XPlainTextEdit_class_init()) {
        XPlainTextEdit* pe = (XPlainTextEdit*)target;
        if (slot == XKB_EDIT_COPY)
            XPlainTextEdit_copy(pe);
        else if (slot == XKB_EDIT_CUT)
            XPlainTextEdit_cut(pe);
        else
            XPlainTextEdit_paste(pe);
        return;
    }
#endif
#if XTEXTEDIT_ON
    if (XClassGetVtable(target) == XTextEdit_class_init()) {
        XTextEdit* te = (XTextEdit*)target;
        if (slot == XKB_EDIT_COPY)
            XTextEdit_copy_2(te);
        else if (slot == XKB_EDIT_CUT)
            XTextEdit_cut_2(te);
        else
            XTextEdit_paste_2(te);
        return;
    }
#endif
    xkb_sendKey(self, key, XKeyboardModifier_ControlModifier);
}

/** @brief 编辑面板动作分发（命中单点；选择态使方向/Home/End 携带
 *         ShiftModifier；全选恒 Ctrl+A；⌫=退格；「开始选择」切换面板
 *         本地选择态不触目标）。 */
static void xkb_editPanelActivate(XVirtualKeyboard* self, int slot)
{
    XKeyboardModifiers mods = XKeyboardModifier_NoModifier;
    if (!self) return;
    if (slot != XKB_EDIT_SEL && self->m_editSelArmed)
        mods = XKeyboardModifier_ShiftModifier;
    switch ((XkbEditPanelSlot)slot) {
    case XKB_EDIT_UP:
        xkb_sendKey(self, XKey_Up, mods);
        break;
    case XKB_EDIT_DOWN:
        xkb_sendKey(self, XKey_Down, mods);
        break;
    case XKB_EDIT_LEFT:
        xkb_sendKey(self, XKey_Left, mods);
        break;
    case XKB_EDIT_RIGHT:
        xkb_sendKey(self, XKey_Right, mods);
        break;
    case XKB_EDIT_SEL:
        self->m_editSelArmed = !self->m_editSelArmed; /* 按下视觉保持。 */
        break;
    case XKB_EDIT_HOME:
        xkb_sendKey(self, XKey_Home, mods);
        break;
    case XKB_EDIT_END:
        xkb_sendKey(self, XKey_End, mods);
        break;
    case XKB_EDIT_ALL:
        xkb_sendKey(self, XKey_A, XKeyboardModifier_ControlModifier);
        break;
    case XKB_EDIT_BACKSPACE:
        xkb_sendKey(self, XKey_Backspace, XKeyboardModifier_NoModifier);
        break;
    case XKB_EDIT_COPY:
    case XKB_EDIT_CUT:
    case XKB_EDIT_PASTE:
        xkb_editPanelClipboard(self, slot);
        break;
    default:
        break;
    }
}

/**
 * @brief 编辑面板命中（mousePressEvent 在选择面板/菜单条命中之后、键
 *        区命中之前调用）。
 * @details 面板只覆盖键区：面板内点击一律消费（动作键 press 直触发，
 *          禁用键与空白消费不动作，不透键区）；面板外（菜单条区）透
 *          传工具栏——返回路径=标题行箭头或再点「文字编辑」图标，进
 *          入/退出不动弹层状态与目标绑定。
 * @return 面板开启且点击在面板内返回 true（事件已消费）；否则 false。
 */
static bool xkb_editPanelHit(XVirtualKeyboard* self, int x, int y)
{
    XkbEditPanelGeo geo;
    int i;
    if (!self || !self->m_editPanelOpen) return false;
    xkb_editPanelLayout(self, &geo);
    if (geo.panel.width <= 0 || geo.panel.height <= 0) return false;
    if (x < geo.panel.x || x >= geo.panel.x + geo.panel.width ||
        y < geo.panel.y || y >= geo.panel.y + geo.panel.height)
        return false;
    if (x >= geo.back.x && x < geo.back.x + geo.back.width &&
        y >= geo.back.y && y < geo.back.y + geo.back.height) {
        /* 返回：回原布局（纯面板 UI 状态复位）。 */
        self->m_editPanelOpen = false;
        self->m_editSelArmed = false;
        XWidget_update((XWidget*)self);
        return true;
    }
    for (i = 0; i < XKB_EDIT_COUNT; ++i) {
        const XRect* r = &geo.cell[i];
        if (x >= r->x && x < r->x + r->width &&
            y >= r->y && y < r->y + r->height) {
            if (xkb_editPanelEnabled(self, i))
                xkb_editPanelActivate(self, i);
            XWidget_update((XWidget*)self);
            return true;
        }
    }
    /* 面板内空白：消费不动作（不透键区）。 */
    XWidget_update((XWidget*)self);
    return true;
}

/** @brief 编辑面板键面绘制垫片（内缩 -5 收边口径同键位；fill 面圆角
 *         + 居中标签，禁用键灰字）。 */
static void xkb_editPanelDrawKey(XPainter* painter, const XRect* cell,
                                 const char* label, uint32_t fill,
                                 uint32_t textColor)
{
    XRect face = *cell;
    int radius = 8;
    if (radius > face.height / 2) radius = face.height / 2;
    if (face.width < 5 || face.height < 5) return;
    face.x += 2;
    face.y += 2;
    face.width -= 5;
    face.height -= 5;
    xkb_roundRect(painter, &face, fill, fill, radius);
    if (label && label[0])
        XPainter_drawTextRect(painter, &face,
                              XPAINTER_TEXT_ALIGN_CENTER |
                                  XPAINTER_TEXT_SINGLE_LINE,
                              label, textColor);
}

/** @brief 编辑面板绘制（覆盖键区最后绘制；标题行+主区大块白底方向区
 *         +右列操作列，配色=LVGL 浅色模板常量：键面 GREY 于白底上、
 *         选择态 PRIMARY_MUTED 强调、禁用键灰字；返回箭头折线自绘——
 *         字库无箭头字形口径同键位文案）。 */
static void xkb_editPanelPaint(XVirtualKeyboard* self, XPainter* painter)
{
    XkbEditPanelGeo geo;
    int radius;
    int i;
    if (!self || !painter || !self->m_editPanelOpen) return;
    xkb_editPanelLayout(self, &geo);
    if (geo.panel.width <= 0 || geo.panel.height <= 0) return;
    /* 面板底=SCR 平涂（覆盖键区）。 */
    XPainter_fillRect(painter, &geo.panel, XKB_LVGL_SCR);
    /* 标题行：左「文字编辑」+右端返回箭头（左向 chevron 折线）。 */
    XPainter_drawTextRect(painter, &geo.title,
                          XPAINTER_TEXT_ALIGN_LEFT |
                              XPAINTER_TEXT_ALIGN_VCENTER |
                              XPAINTER_TEXT_SINGLE_LINE,
                          XKB_EDIT_LBL_TITLE, XKB_LVGL_TEXT);
    {
        XPoint pts[3];
        int box = geo.back.height / 3;
        int cx = geo.back.x + geo.back.width / 2;
        int cy = geo.back.y + geo.back.height / 2;
        if (box < 6) box = 6;
        if (box > 18) box = 18;
        pts[0].x = cx + box / 2;
        pts[0].y = cy - box / 3;
        pts[1].x = cx - box / 2;
        pts[1].y = cy;
        pts[2].x = cx + box / 2;
        pts[2].y = cy + box / 3;
        XPainter_setPen(painter, XKB_LVGL_TEXT);
        XPainter_drawPolyline(painter, pts, 3);
    }
    /* 主区大块白底（卡白+灰边圆角）。 */
    radius = 8;
    if (radius > geo.main.height / 2) radius = geo.main.height / 2;
    xkb_roundRect(painter, &geo.main, XKB_LVGL_CARD, XKB_LVGL_GREY, radius);
    /* 主区 8 键：GREY 面（白底上可见）；选择态「开始选择」PRIMARY_
       MUTED 强调保持；无绑定目标灰字（禁用态）。 */
    for (i = XKB_EDIT_UP; i <= XKB_EDIT_END; ++i) {
        uint32_t fill = XKB_LVGL_GREY;
        uint32_t textColor = XKB_LVGL_TEXT;
        if (i == XKB_EDIT_SEL && self->m_editSelArmed)
            fill = XKB_LVGL_PRIMARY_MUTED; /* 选择态按下视觉保持。 */
        if (!xkb_editPanelEnabled(self, i)) textColor = XKB_LVGL_DISABLED_TEXT;
        xkb_editPanelDrawKey(painter, &geo.cell[i], kEditPanelLabels[i],
                             fill, textColor);
    }
    /* 右列 4 键（CARD 面标准键样；无绑定目标灰字）。 */
    for (i = XKB_EDIT_BACKSPACE; i < XKB_EDIT_COUNT; ++i) {
        uint32_t textColor =
            xkb_editPanelEnabled(self, i) ? XKB_LVGL_TEXT
                                          : XKB_LVGL_DISABLED_TEXT;
        xkb_editPanelDrawKey(painter, &geo.cell[i], kEditPanelLabels[i],
                             XKB_LVGL_CARD, textColor);
    }
}

#if XVIRTUALKEYBOARD_ON

/* ==================== 拼音候选带（面板内嵌顶行，XGui 扩展；数据源
 *                       =engine 候选模型 + context.preeditText 镜像） ==================== */

/** @brief 候选带左「中」模式指示 chip 宽（px，固定）。 */
#define XKB_IME_MODE_CHIP_W 40
/** @brief 候选带右端翻页箭头单元宽（"<"/">" 各一，px）。页码格宽不
 *         在此恒定——按页码文本实测推导（见 xkb_imePageInfoWidth），
 *         让页码与箭头同用整行字号恰好装下。 */
#define XKB_IME_PAGE_CELL_W 24
/** @brief 页码格宽上限（px；下限=XKB_IME_PAGE_CELL_W）：超宽页码（>3
 *         位无现实场景）按上限裁叠，防页码区吞掉候选 chip 排布区。 */
#define XKB_IME_PAGE_INFO_W_MAX 44
/** @brief 候选带内子区间间距（px）。 */
#define XKB_IME_GAP 4
/** @brief 候选带内缩边距（px）。 */
#define XKB_IME_PAD 2
/** @brief 变宽 chip 文本两侧垫宽（px；实宽=textWidth+2*垫宽 钳位）。 */
#define XKB_IME_CHIP_PAD 3
/** @brief 候选单页 chip 数上限（与 IME 页容量口径一致，数字键 1..9）。 */
#define XKB_IME_PAGE_MAX 9

/**
 * @brief 候选带内部子区域几何（布局/绘制/命中三方共用同一几何函数产
 *        出，保证所见即所点）。
 */
typedef struct XkbImeBandGeo
{
    XRect band;      /**< 整带矩形（控件局部坐标）。 */
    XRect modeChip;  /**< 「中」模式指示 chip（点按切中英）。 */
    XRect compose;   /**< 组串区（ASCII 组串显示，空闲留白）。 */
    XRect chips;     /**< 候选 chip 流式排布区。 */
    XRect pagePrev;  /**< "<" 上一页热区。 */
    XRect pageInfo;  /**< 页码区（"页/页数" ASCII）。 */
    XRect pageNext;  /**< ">" 下一页热区。 */
    int chipW;       /**< 单个候选 chip 最小宽（=键行高钳 8，近方形
                          下限；变宽 chip 实宽见 chipWv）。 */
    int chipGap;     /**< chip 间距（px）。 */
    int chipCount;   /**< 当前页实际排布 chip 数（超排布区截断后的
                          生效数；绘制与命中共同消费）。 */
    int chipXv[XKB_IME_PAGE_MAX]; /**< 每 chip 左 x（累进，所见即所点）。 */
    int chipWv[XKB_IME_PAGE_MAX]; /**< 每 chip 实宽（clamp(textWidth+
                          2*垫宽, chipMinW, chips 区宽)）。 */
} XkbImeBandGeo;

/** @brief 候选带排版字符宽（控件字体 ASCII 'a' 步进；下限 4px 防退
 *         化——组串区宽公式的字号输入，布局/绘制/命中同源）。 */
static int xkb_imeCharWidth(const XVirtualKeyboard* self)
{
    int w = XPainter_textWidth(&((XWidget*)self)->m_font, "a");
    return w < 4 ? 4 : w;
}

/** @brief 候选 chip 实宽（变宽口径：clamp(textWidth+2*垫宽, chipMinW,
 *         chips 区宽)；chipMinW=rowH 钳 8 近方形下限，超宽候选钳排布
 *         区）。空文本退化 chipMinW。布局与页容量换算同源。 */
static int xkb_imeChipTextWidth(const XVirtualKeyboard* self, const char* text,
                                int chipMinW, int areaW)
{
    int w = chipMinW;
    if (text && text[0])
    {
        w = XPainter_textWidth(&((XWidget*)self)->m_font, text) +
            2 * XKB_IME_CHIP_PAD;
        if (w < chipMinW) w = chipMinW;
        if (w > areaW) w = areaW;
    }
    return w;
}

static int xkb_imeFormatUInt(int value, char* out);

/**
 * @brief 页码区格宽（">"/"<" 之间，px）：按当前页码文本 "N/M" 以控件
 *        字体实测宽 +2*PAD 垫宽推导，钳 [XKB_IME_PAGE_CELL_W,
 *        XKB_IME_PAGE_INFO_W_MAX]。
 * @details 页码与两侧翻页箭头同用整行字号直绘（页码降号小字与箭头
 *          整行字号同行的浓淡混排根修），实测推导保证恰好装下不溢出
 *          裁叠；下限=既有 24px 固定格，紧凑悬浮态候选 chip 排布区在
 *          常见一位页码下不被多挤。布局/绘制/命中同源消费（所见即所
 *          点）。无候选（pageCount<=0）退 24px。
 */
static int xkb_imePageInfoWidth(const XVirtualKeyboard* self)
{
    char pageText[16];
    int pageCount;
    int pos = 0;
    int w;
    pageCount = self ? xkb_candidatePageCount(self) : 0;
    if (pageCount <= 0) return XKB_IME_PAGE_CELL_W;
    pos += xkb_imeFormatUInt(self->m_candidatePage + 1, pageText + pos);
    if (pos < (int)sizeof(pageText) - 2) pageText[pos++] = '/';
    pos += xkb_imeFormatUInt(pageCount, pageText + pos);
    pageText[pos] = '\0';
    w = XPainter_textWidth(&((XWidget*)self)->m_font, pageText) +
        2 * XKB_IME_PAD;
    if (w < XKB_IME_PAGE_CELL_W) w = XKB_IME_PAGE_CELL_W;
    if (w > XKB_IME_PAGE_INFO_W_MAX) w = XKB_IME_PAGE_INFO_W_MAX;
    return w;
}

/**
 * @brief 候选带几何计算（paint 与 hit 共用；无带时全部子区间清零）。
 * @param self 键盘对象；NULL 时 geo 清零返回。
 * @param rowH 键行高（chip 最小宽输入；<=0 时以带内高兜底）。
 * @param geo 输出几何（调用方提供存储空间）。
 */
static void xkb_imeBandLayout(const XVirtualKeyboard* self, int rowH,
                              XkbImeBandGeo* geo)
{
    int innerH;
    int composeW;
    int right;
    if (!geo) return;
    XRect_init(&geo->band, 0, 0, 0, 0);
    XRect_init(&geo->modeChip, 0, 0, 0, 0);
    XRect_init(&geo->compose, 0, 0, 0, 0);
    XRect_init(&geo->chips, 0, 0, 0, 0);
    XRect_init(&geo->pagePrev, 0, 0, 0, 0);
    XRect_init(&geo->pageInfo, 0, 0, 0, 0);
    XRect_init(&geo->pageNext, 0, 0, 0, 0);
    geo->chipW = 0;
    geo->chipGap = XKB_IME_GAP;
    geo->chipCount = 0;
    if (!self || self->m_imeBandRect.height <= 0 ||
        self->m_imeBandRect.width <= 0)
        return;
    geo->band = self->m_imeBandRect;
    innerH = geo->band.height - 2 * XKB_IME_PAD;
    if (innerH < 1) innerH = 1;
    /* 左「中」chip（固定 40px，点按=切中英）。 */
    geo->modeChip.x = geo->band.x + XKB_IME_PAD;
    geo->modeChip.y = geo->band.y + XKB_IME_PAD;
    geo->modeChip.width = XKB_IME_MODE_CHIP_W;
    geo->modeChip.height = innerH;
    /* 右端翻页区（自右向左：">" "页码" "<"）：箭头单元恒
       XKB_IME_PAGE_CELL_W，页码格宽按页码文本实测推导（整行字号恰好
       装下；布局/绘制/命中同源几何）。 */
    right = geo->band.x + geo->band.width - XKB_IME_PAD;
    geo->pageNext.x = right - XKB_IME_PAGE_CELL_W;
    geo->pageNext.width = XKB_IME_PAGE_CELL_W;
    geo->pageInfo.width = xkb_imePageInfoWidth(self);
    geo->pageInfo.x = geo->pageNext.x - geo->pageInfo.width;
    geo->pagePrev.x = geo->pageInfo.x - XKB_IME_PAGE_CELL_W;
    geo->pagePrev.width = XKB_IME_PAGE_CELL_W;
    geo->pageNext.y = geo->band.y + XKB_IME_PAD;
    geo->pageInfo.y = geo->band.y + XKB_IME_PAD;
    geo->pagePrev.y = geo->band.y + XKB_IME_PAD;
    geo->pageNext.height = innerH;
    geo->pageInfo.height = innerH;
    geo->pagePrev.height = innerH;
    /* 组串区（宽=min(带宽/3, 15×字符宽+16)，显示 ASCII 组串——15 为
       INV2 组串容量，跨音节组串最长 15 字母）。 */
    composeW = geo->band.width / 3;
    if (xkb_imeCharWidth(self) * 15 + 16 < composeW)
        composeW = xkb_imeCharWidth(self) * 15 + 16;
    if (composeW < 1) composeW = 1;
    geo->compose.x = geo->modeChip.x + geo->modeChip.width + XKB_IME_GAP;
    geo->compose.y = geo->band.y + XKB_IME_PAD;
    geo->compose.width = composeW;
    geo->compose.height = innerH;
    /* chip 排布区与 chip 最小尺寸（chipW=键行高钳 8 近方形下限；行高
       缺席以带内高兜底）。 */
    geo->chipW = rowH > 0 ? rowH : innerH;
    if (geo->chipW < 8) geo->chipW = 8;
    geo->chips.x = geo->compose.x + composeW + XKB_IME_GAP;
    geo->chips.y = geo->band.y + XKB_IME_PAD;
    geo->chips.width = geo->pagePrev.x - XKB_IME_GAP - geo->chips.x;
    if (geo->chips.width < 0) geo->chips.width = 0;
    geo->chips.height = innerH;
    /* 变宽 chip 几何（不受 PHRASE_ON 门控——单字 3 字节汉字等宽，视
       觉与近方形差异仅垫宽，属实现细节非行为分叉）：每 chip 实宽按
       候选文本测宽钳位，x 累进；绘制与命中消费同一数组（所见即所
       点）。 */
    geo->chipCount = 0;
    {
        int pageSize = self->m_candidatePageSize > 0 ? self->m_candidatePageSize
                                                     : 1;
        int first = self->m_candidatePage * pageSize;
        int count = xkb_candidateCount(self);
        int shown = count - first;
        int x = geo->chips.x;
        int i;
        char text[64];
        if (shown > pageSize) shown = pageSize;
        if (shown < 0) shown = 0;
        if (shown > XKB_IME_PAGE_MAX) shown = XKB_IME_PAGE_MAX;
        for (i = 0; i < shown; ++i) {
            int w;
            if (!xkb_candidateText(self, first + i, text, (int)sizeof(text)))
                break;
            w = xkb_imeChipTextWidth(self, text, geo->chipW,
                                     geo->chips.width);
            if (i > 0) x += geo->chipGap;
            if (x + w > geo->chips.x + geo->chips.width) break; /* 超出截断。 */
            geo->chipXv[i] = x;
            geo->chipWv[i] = w;
            x += w;
            geo->chipCount = i + 1;
        }
    }
}

/** @brief 页容量 k 模拟：每条 stride 页（候选 i 按 i%k 落页内位）的
 *         k chip 皆容纳返回 true。chip 宽与页内位置无关（按候选文本
 *         测宽钳位），仅页内累进 x 依赖 k。 */
static bool xkb_imePageFits(const XVirtualKeyboard* self, int count, int k,
                            int chipMinW, int areaX, int areaW, int gap)
{
    int x = areaX;
    int i;
    char text[64];
    for (i = 0; i < count; ++i) {
        int w;
        if (!xkb_candidateText(self, i, text, (int)sizeof(text))) return true;
        if (i % k == 0)
            x = areaX; /* 新 stride 页从头排。 */
        else
            x += gap;
        w = xkb_imeChipTextWidth(self, text, chipMinW, areaW);
        if (x + w > areaX + areaW) return false;
        x += w;
    }
    return true;
}

/** @brief 候选页容量注入（变宽 chip 口径；面板本地状态）：按当前候选
 *         全量测宽，自 k=min(9,count) 向下搜索『每条 stride 页的 k
 *         chip 皆容纳』的最大 k，钳位 [1,9]；同时钳当前页到有效域。 */
static void xkb_imeInjectPageSize(XVirtualKeyboard* self, int rowH)
{
    XkbImeBandGeo geo;
    int count;
    int maxK;
    int k;
    xkb_imeBandLayout(self, rowH, &geo);
    count = xkb_candidateCount(self);
    maxK = count < XKB_IME_PAGE_MAX ? count : XKB_IME_PAGE_MAX;
    for (k = maxK; k >= 2; --k) {
        if (xkb_imePageFits(self, count, k, geo.chipW, geo.chips.x,
                            geo.chips.width, geo.chipGap))
            break;
    }
    if (k < 1) k = 1;
    if (k > XKB_IME_PAGE_MAX) k = XKB_IME_PAGE_MAX;
    self->m_candidatePageSize = k;
    /* 页钳位（组串变化后总页数可能缩）。 */
    {
        int pages = xkb_candidatePageCount(self);
        if (self->m_candidatePage >= pages) self->m_candidatePage = pages - 1;
        if (self->m_candidatePage < 0) self->m_candidatePage = 0;
    }
}

/** @brief 组串变化后的页容量同步（feed 路径出口调用；带缺席时无操
 *         作，行高取首键矩形与 paint/hit 同源）。 */
static void xkb_imeSyncPageSize(XVirtualKeyboard* self)
{
    int rowH;
    if (!self || self->m_imeBandRect.height <= 0) return;
    rowH = self->m_keyCount > 0 ? (int)self->m_keyRects[0].height : 0;
    xkb_imeInjectPageSize(self, rowH);
}

/** @brief 无符号整数转十进制 ASCII（页码格式化专用，不引入 stdio；
 *         负值按 0 处理，最多 3 位）。 */
static int xkb_imeFormatUInt(int value, char* out)
{
    char tmp[4];
    int n = 0;
    int i = 0;
    if (value < 0) value = 0;
    do {
        tmp[n++] = (char)('0' + (value % 10));
        value /= 10;
    } while (value > 0 && n < 3);
    while (n > 0)
        out[i++] = tmp[--n];
    out[i] = '\0';
    return i;
}

/**
 * @brief 候选带绘制（组串区/候选 chip/翻页区/中英指示 chip）。
 * @details 配色=LVGL 浅色模板常量单路径（带卡白底圆角+灰边、组串区
 *          主色@20%、chip 键面样式、翻页箭头文字灰/禁用混灰；原
 *          XKEYBOARD_THEME_LVGL_ON 调色板分支已合并移除）。数据源=
 *          engine 候选模型（分页为面板本地状态）+context.preeditText。
 *          绘制前先经 xkb_imeBandLayout 取同一几何。
 */
static void xkb_imeBandPaint(XVirtualKeyboard* self, XPainter* painter)
{
    XkbImeBandGeo geo;
    uint32_t bandFace = XKB_LVGL_CARD;
    uint32_t bandBorder = XKB_LVGL_GREY;
    uint32_t chipFace = XKB_LVGL_CARD;
    uint32_t chipText = XKB_LVGL_TEXT;
    uint32_t chipBadge = XKB_LVGL_TEXT; /* 编号角标与正文同色（浅灰墨色
                                           曾把角标浓淡上限钳在近白，与
                                           近黑正文同带浓淡分叉）。 */
    uint32_t composeFace = XKB_LVGL_PRIMARY_MUTED;
    uint32_t composeText = XKB_LVGL_PRIMARY;
    uint32_t modeFace = XKB_LVGL_GREY;
    uint32_t modeText = XKB_LVGL_TEXT;
    uint32_t arrowCol = XKB_LVGL_ARROW_TEXT;
    uint32_t arrowDis = XKB_LVGL_DISABLED_TEXT;
    int radius;
    int pageIdx;
    int pageCount;
    int pageSize;
    int first;
    int i;
    char pageText[16];
    char preedit[64];
    char chipBuf[64];
    int pos = 0;

    if (!self || self->m_imeBandRect.height <= 0) return;
    xkb_imeBandLayout(self,
                      self->m_keyCount > 0 ? (int)self->m_keyRects[0].height
                                           : 0,
                      &geo);
    if (geo.band.height <= 0) return;
    pageIdx = self->m_candidatePage;
    pageCount = xkb_candidatePageCount(self);
    pageSize = self->m_candidatePageSize > 0 ? self->m_candidatePageSize : 1;
    first = pageIdx * pageSize; /* chip 绘制条数=geo.chipCount（同源几何）。 */
    /* 带面板（card 白底 + 灰边；圆角随带高钳位）。 */
    radius = 8;
    if (radius > geo.band.height / 2) radius = geo.band.height / 2;
    xkb_roundRect(painter, &geo.band, bandFace, bandBorder, radius);
    /* 「中」模式指示 chip（点击切中英；带仅中文态存在，恒显示「中」）。 */
    xkb_roundRect(painter, &geo.modeChip, modeFace, modeFace, 4);
    XPainter_drawTextRect(painter, &geo.modeChip,
                          XPAINTER_TEXT_ALIGN_CENTER |
                              XPAINTER_TEXT_SINGLE_LINE,
                          "\xE4\xB8\xAD", modeText);
    /* 组串区（主色弱高亮=『进行中』语义；空闲留白；文本=context 镜像）。 */
    xkb_roundRect(painter, &geo.compose, composeFace, composeFace, 4);
    xkb_preeditSnapshot(self, preedit, (int)sizeof(preedit));
    if (preedit[0])
        XPainter_drawTextRect(painter, &geo.compose,
                              XPAINTER_TEXT_ALIGN_LEFT |
                                  XPAINTER_TEXT_ALIGN_VCENTER |
                                  XPAINTER_TEXT_SINGLE_LINE,
                              preedit, composeText);
    /* 候选 chip 流式排布（候选 UTF-8 串居中；变宽几何 chipXv/chipWv
       由 xkb_imeBandLayout 按同源候选测宽产出，超出排布区截断——
       chipCount 为生效数）。 */
    for (i = 0; i < geo.chipCount; ++i) {
        XRect chip;
        if (!xkb_candidateText(self, first + i, chipBuf,
                               (int)sizeof(chipBuf)))
            break;
        chip.x = geo.chipXv[i];
        chip.y = geo.chips.y;
        chip.width = geo.chipWv[i];
        chip.height = geo.chips.height;
        xkb_roundRect(painter, &chip, chipFace, chipFace, 4);
        XPainter_drawTextRect(painter, &chip,
                              XPAINTER_TEXT_ALIGN_CENTER |
                                  XPAINTER_TEXT_SINGLE_LINE,
                              chipBuf, chipText);
        /* 候选编号角标（搜狗口径，2026-10-04 用户反馈）：chip 右上角
         * 小号数字=页内位序（1 基），与 xkb_imeInterceptDigit 的数字
         * 选候选映射同源（页基×页容量+位序），为数字键选候选与触点
         * 定位提供视觉锚。字号=chip 高 1/3 钳 [9,16]（临时换小字号绘
         * 制、画毕恢复键盘字号）。 */
        {
            char badge[2];
            XFont small;
            int px = geo.chips.height / 3;
            XRect br;
            if (px < 9) px = 9;
            if (px > 16) px = 16;
            badge[0] = (char)('1' + i);
            badge[1] = '\0';
            XFont_init(&small);
            XClassCopy(&small, &((XWidget*)self)->m_font);
            XFont_setPixelSize(&small, px);
            XPainter_setFont(painter, &small);
            br.x = chip.x + chip.width - px - 2;
            br.y = chip.y + 1;
            br.width = px + 2;
            br.height = px + 2;
            XPainter_drawTextRect(painter, &br,
                                  XPAINTER_TEXT_ALIGN_CENTER |
                                      XPAINTER_TEXT_SINGLE_LINE,
                                  badge, chipBadge);
            XPainter_setFont(painter, &((XWidget*)self)->m_font);
            XClassDeinit(&small);
        }
    }
    /* 右端翻页区（"<" 页码 ">"；单页/边界置灰且点按无效）。 */
    XPainter_drawTextRect(painter, &geo.pagePrev,
                          XPAINTER_TEXT_ALIGN_CENTER |
                              XPAINTER_TEXT_SINGLE_LINE,
                          "<",
                          (pageCount > 1 && pageIdx > 0) ? arrowCol
                                                         : arrowDis);
    if (pageCount > 0) {
        /* 页码整行字号直绘：格宽=xkb_imePageInfoWidth 按同源页码文本
           实测宽推导（钳 [24,44]），恰好装下不溢出裁叠（原 24px 恒定
           格宽曾逼出「页码降号 9-14px 小字」的同行字号混排——页码与
           箭头浓淡不一致的根因，现与两侧 "<"/">" 同字体同字号）。
           painter 当前字体即键盘字体（VXKeyboard_paintEvent 统一设置）。 */
        pos = 0;
        pos += xkb_imeFormatUInt(pageIdx + 1, pageText + pos);
        pageText[pos++] = '/';
        pos += xkb_imeFormatUInt(pageCount, pageText + pos);
        pageText[pos] = '\0';
        XPainter_drawTextRect(painter, &geo.pageInfo,
                              XPAINTER_TEXT_ALIGN_CENTER |
                                  XPAINTER_TEXT_SINGLE_LINE,
                              pageText,
                              pageCount > 1 ? arrowCol : arrowDis);
    }
    XPainter_drawTextRect(painter, &geo.pageNext,
                          XPAINTER_TEXT_ALIGN_CENTER |
                              XPAINTER_TEXT_SINGLE_LINE,
                          ">",
                          (pageCount > 1 && pageIdx < pageCount - 1)
                              ? arrowCol
                              : arrowDis);
}

/**
 * @brief 候选带命中（mousePressEvent 在 xkb_hitTest 之前调用）。
 * @details 命中即 press 直触发（chip/翻页/「中」chip），不进
 *          m_pressedKey 武装链（move/release 对 NONE 键零动作，天然
 *          兼容）；带内其余空白一并消费防误透键区。提交路径经
 *          xkb_writeText 复用既有写入链。
 * @return 命中带返回 true（事件已消费）；未命中返回 false。
 */
static bool xkb_imeBandHit(XVirtualKeyboard* self, int x, int y)
{
    XkbImeBandGeo geo;
    XVirtualKeyboardSelectionListModel* model;
    int pageCount;
    int pageSize;
    int first;
    int shown;
    int i;
    if (!self || self->m_imeBandRect.height <= 0) return false;
    if (x < self->m_imeBandRect.x ||
        x >= self->m_imeBandRect.x + self->m_imeBandRect.width ||
        y < self->m_imeBandRect.y ||
        y >= self->m_imeBandRect.y + self->m_imeBandRect.height)
        return false;
    xkb_imeBandLayout(self,
                      self->m_keyCount > 0 ? (int)self->m_keyRects[0].height
                                           : 0,
                      &geo);
    model = xkb_modelOf(self);
    /* 「中」chip：切英文态（带随重建隐藏；切回走布局「中/EN」键）。 */
    if (x >= geo.modeChip.x && x < geo.modeChip.x + geo.modeChip.width &&
        y >= geo.modeChip.y && y < geo.modeChip.y + geo.modeChip.height) {
        XVirtualKeyboard_setImeChinese(self, false);
        XWidget_update((XWidget*)self);
        return true;
    }
    pageCount = xkb_candidatePageCount(self);
    if (pageCount > 1) {
        if (x >= geo.pagePrev.x && x < geo.pagePrev.x + geo.pagePrev.width &&
            y >= geo.pagePrev.y && y < geo.pagePrev.y + geo.pagePrev.height) {
            if (self->m_candidatePage > 0) {
                --self->m_candidatePage; /* 面板本地翻页（钳位）。 */
                XWidget_update((XWidget*)self);
            }
            return true;
        }
        if (x >= geo.pageNext.x && x < geo.pageNext.x + geo.pageNext.width &&
            y >= geo.pageNext.y && y < geo.pageNext.y + geo.pageNext.height) {
            if (self->m_candidatePage < pageCount - 1) {
                ++self->m_candidatePage;
                XWidget_update((XWidget*)self);
            }
            return true;
        }
    }
    /* 候选 chip：点选即 model.selectItem（全量下标=页基+页内序；提交
       经插件 selectionListItemSelected→context.commit→面板写入链）；
       几何消费与绘制同一 chipXv/chipWv（所见即所点）。 */
    pageSize = self->m_candidatePageSize > 0 ? self->m_candidatePageSize : 1;
    first = self->m_candidatePage * pageSize;
    shown = xkb_candidateCount(self) - first;
    if (shown > pageSize) shown = pageSize;
    for (i = 0; i < geo.chipCount && i < shown; ++i) {
        if (x >= geo.chipXv[i] && x < geo.chipXv[i] + geo.chipWv[i] &&
            y >= geo.chips.y && y < geo.chips.y + geo.chips.height) {
            if (model)
                XVirtualKeyboardSelectionListModel_selectItem(model,
                                                              first + i);
            xkb_imeSyncPageSize(self); /* 提交后候选集变化，页容量同步。 */
            XWidget_update((XWidget*)self);
            return true;
        }
    }
    /* 带内其余空白：消费点击不透键区。 */
    return true;
}

/**
 * @brief 数字键 1..9 面板分页拦截（xkb_activateButton 单点调用）。
 * @details 【分页面板本地化（apiMapping#13）】组串中且有候选时按面板
 *          页状态换算全量下标 page*pageSize+d-1 → model.selectItem
 *          （提交经插件候选钩子→context.commit→写入链）；无候选/IDLE
 *          返回 false（放行为普通数字/插件吞掉语义）。
 * @return 已处理返回 true。
 */
static bool xkb_imeInterceptDigit(XVirtualKeyboard* self, char digit)
{
    XVirtualKeyboardSelectionListModel* model;
    int pageSize;
    int index;
    if (!self || digit < '1' || digit > '9') return false;
    if (!xkb_imeComposing(self)) return false;
    if (xkb_candidateCount(self) <= 0) return true; /* 组串中无候选：吞掉。 */
    model = xkb_modelOf(self);
    if (!model) return false;
    pageSize = self->m_candidatePageSize > 0 ? self->m_candidatePageSize : 1;
    index = self->m_candidatePage * pageSize + (digit - '0') - 1;
    if (index < 0 || index >= xkb_candidateCount(self)) {
        XWidget_update((XWidget*)self); /* 越界：吞掉（原语义）。 */
        return true;
    }
    XVirtualKeyboardSelectionListModel_selectItem(model, index);
    xkb_imeSyncPageSize(self);
    XWidget_update((XWidget*)self);
    return true;
}

/**
 * @brief Qt 形态单点路由：按钮文本 → engine 虚键（插件可消费）。
 * @details 未列出的控制键（确认/关闭/收起/中EN/+/-）不路由，面板本地
 *          语义先行；路由返回 false=插件未消费 → 回落内置语义匹配。
 *          拼音插件消费面：小写字母进组串/退格删组串/回车原串/空格
 *          首选或原串；Plain 插件恒不消费（拉丁直通等价现状）。
 * @return 已消费返回 true（激活分发终止）。
 */
static bool xkb_routeKey(XVirtualKeyboard* self, const char* text)
{
    XVirtualKeyboardInputEngine* engine = xkb_engineOf(self);
    int key = 0;
    char buf[2];
    const char* keyText = NULL;
    char ch;
    if (!engine || !text || !text[0]) return false;
    if (text[1] == '\0') {
        ch = text[0];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a'); /* 组串小写。 */
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == ' ') {
            buf[0] = ch;
            buf[1] = '\0';
            key = (int)(unsigned char)ch;
            keyText = buf;
        } else {
            return false; /* 符号键不路由（内置直写）。 */
        }
    } else if (XStrcmp(text, XKEYBOARD_LBL_BACKSPACE) == 0) {
        key = XKey_Backspace;
    } else if (XStrcmp(text, XKEYBOARD_LBL_NEWLINE) == 0 ||
               XStrcmp(text, XKEYBOARD_LBL_ENTER) == 0 ||
               XStrcmp(text, XKEYBOARD_LBL_SEARCH) == 0) {
        key = XKey_Return; /* 搜索键与换行同语义（组串中原串上屏）。 */
    } else if (XStrcmp(text, XKEYBOARD_LBL_LEFT) == 0 ||
               XStrcmp(text, XKEYBOARD_LBL_RIGHT) == 0) {
        return false; /* 光标键放行内置（组串显示在候选带，无害）。 */
    } else {
        return false;
    }
    return XVirtualKeyboardInputEngine_virtualKeyClick(engine, key, keyText,
                                                       XKeyboardModifier_NoModifier);
}

#endif /* XVIRTUALKEYBOARD_ON */

/* ==================== 布局解析（对标 lv_buttonmatrix.c:154-177） ==================== */

/**
 * @brief 按当前模式槽位表重建键位矩形与生效控制字表。
 * @details 键面文字字号恒为控件构造默认（pixelSize=-1/pointSize=12，
 *          绘制期经 painter pointSize→px 回退统一到全局恒定字号），
 *          不随键行高缩放——几何逐帧推导字号曾使键盘文字与非键盘区
 *          恒定字号文字同屏浓淡不一、且停靠/悬浮/缩放间漂移（根因 d1
 *          根修：原 xkb_syncLabelPixelSize 通道整体移除）。
 * @details 解析规则：map 以 NULL 数组尾或 "" 按钮终止；"\n" 为换行分
 *          隔符（不占按钮索引）；行内每键宽=行宽×该键单位/该行单位总
 *          数（整数累进边界，行内无缝隙）；行高=内容高/行数均分；
 *          popovers=0 时剥全部 POPOVER 位存 m_keyCtrls（对标 KB.c:
 *          468-487）；键区上方常驻一条菜单条（Sogou 改版二阶段，条高=
 *          contentH/(rows+1) 既有组串带预留通道，所有布局与模式一致预
 *          留——原「popovers=1 顶行含 POPOVER 键预留一行高气泡带」通
 *          道并入本条，气泡绘制期钳到条高）。按钮数超
 *          XKEYBOARD_MAX_BUTTONS 整体拒绝（保持原布局，XPrintf 诊断）。
 */
static void xkb_rebuildLayout(XVirtualKeyboard* self)
{
    const char* const* map;
    const XKeyboardButtonCtrl* ctrlMap;
    XKeyboardButtonCtrl ctrls[XKEYBOARD_MAX_BUTTONS];
    XRect rects[XKEYBOARD_MAX_BUTTONS];
    int rowCount = 1;
    uint32_t count = 0;
    int contentX;
    int contentY;
    int contentW;
    int contentH;
    int bandH = 0;
    int rowH;
    int row;
    int i;
    int rowStart[XKEYBOARD_MAX_BUTTONS + 1];
    int rows = 0;
    const char* labels[XKEYBOARD_MAX_BUTTONS];

    if (!self) return;
    map = self->m_maps[self->m_mode];
    if (!map) {
        self->m_keyCount = 0;
        XRect_init(&self->m_menuBarRect, 0, 0, 0, 0); /* 空表无条。 */
#if XVIRTUALKEYBOARD_ON
        XRect_init(&self->m_imeBandRect, 0, 0, 0, 0); /* 空表无带。 */
#endif
        return;
    }
    ctrlMap = self->m_ctrls[self->m_mode];
    rowStart[0] = 0; /* 首行起点（首行前无换行分隔符）。 */

    /* 第一遍：解析按钮/行结构并落生效控制字（临时表，拒绝时不破坏原布局）。 */
    for (i = 0; map[i] != NULL; ++i) {
        const char* label = map[i];
        if (label[0] == '\0') break; /* "" 按钮终止（兼容 LVGL 习惯）。 */
        if (XStrcmp(label, "\n") == 0) {
            /* >=：恰 64 个换行时 rows 会到 65，收尾写 rowStart[rows]=65
               将越过容量 65 的合法下标 0..64（评审复核发现的病态表越界）。 */
            if (rowCount >= XKEYBOARD_MAX_BUTTONS) {
                XPrintf("[XVirtualKeyboard] setMap: 行数超出上限 %d，整体拒绝\n",
                        (int)XKEYBOARD_MAX_BUTTONS);
                return; /* 保持原布局不变。 */
            }
            /* 换行分隔符=当前行结束：登记「下一行起点」（差一修复——
               原写 rowStart[rowCount-1] 覆盖了当前行起点，导致中间行
               起点漏登、末行起点成栈垃圾，第二遍按垃圾越界读 ctrls）。
               不变式：rowStart[0]=0、rowStart[row] 为第 row 行首键在
               ctrl/rects 表中的索引、rowStart[rows]=count。 */
            rowStart[rowCount] = (int)count;
            ++rowCount;
            continue;
        }
        if (count >= XKEYBOARD_MAX_BUTTONS) {
            XPrintf("[XVirtualKeyboard] setMap: 按钮数超出上限 %d，整体拒绝\n",
                    (int)XKEYBOARD_MAX_BUTTONS);
            return; /* 保持原布局不变。 */
        }
        {
            XKeyboardButtonCtrl ctrl = ctrlMap ? ctrlMap[count]
                                               : (XKeyboardButtonCtrl)1;
            int width = (int)ctrl & (int)XKEYBOARD_CTRL_WIDTH_MASK;
            if (width <= 0) width = 1;
            if (!self->m_popovers)
                ctrl = (XKeyboardButtonCtrl)(
                    (int)ctrl & ~(int)XKEYBOARD_CTRL_POPOVER);
            ctrls[count] = (XKeyboardButtonCtrl)(
                ((int)ctrl & ~(int)XKEYBOARD_CTRL_WIDTH_MASK) | width);
            labels[count] = label;
            ++count;
        }
    }
    rows = rowCount;
    rowStart[rows] = (int)count;

    /* 布局款型锁定态（shift 第三态）：Shift 键 CHECKED 高亮。生效表
       源=槽位表，此处按展示态打位（每次重建复核、不落槽位；大写一次
       性态不高亮，锁定态恒亮至回小写）。 */
    if ((self->m_layoutKind == XKeyboardLayout_PinyinFull ||
         self->m_layoutKind == XKeyboardLayout_EnglishFull) &&
        self->m_shiftState == 2) {
        for (i = 0; i < (int)count; ++i) {
            if (XStrcmp(labels[i], XKEYBOARD_LBL_SHIFT) == 0)
                ctrls[i] = (XKeyboardButtonCtrl)(
                    (int)ctrls[i] | (int)XKEYBOARD_CTRL_CHECKED);
        }
    }
#if XVIRTUALKEYBOARD_ON
    /* 中/EN 键中文态 CHECKED 高亮（面=GREY 与 Shift 锁定同款；键帽文
     * 本随态渲染 中/EN，见 VXKeyboard_paintEvent——静态标签无态指示，
     * 英文款型往返后用户无从分辨当前态，实测反馈）。 */
    if (xkb_imeChineseState(self)) {
        for (i = 0; i < (int)count; ++i) {
            if (XStrcmp(labels[i], XKEYBOARD_LBL_IME) == 0)
                ctrls[i] = (XKeyboardButtonCtrl)(
                    (int)ctrls[i] | (int)XKEYBOARD_CTRL_CHECKED);
        }
    }
#endif

    /* 第二遍：几何归一化（每行按自身单位总数铺满行宽）。 */
    {
        XRect area = XWidget_rect((XWidget*)self);
        contentX = area.x + 2;
        contentY = area.y + 2;
        contentW = area.width - 4;
        contentH = area.height - 4;
        if (contentW < 1) contentW = 1;
        if (contentH < 1) contentH = 1;
    }
#if XVIRTUALKEYBOARD_ON
    /* 组串收缩态（需求③）防反馈自缩：contentH 改按全量钳位高口径
       （m_popupHeight=reposition 全量分支记录的宿主钳位高，-4 同上方
       内边距）——收缩只裁控件高度，带高/行高/键矩形仍按全量算，组串
       结束恢复全量时键区布局零跳变；键区矩形越出收缩控件边界被绘制
       裁剪且命中失效（真实输入只派发控件边界内事件）。 */
    if (self->m_popupHeight > 0 && xkb_composeCollapsedWanted(self)) {
        contentH = self->m_popupHeight - 4;
        if (contentH < 1) contentH = 1;
    }
#endif
    /* 菜单条常驻预留（Sogou 改版二阶段）：条高=既有组串带预留通道
       contentH/(rows+1)，所有布局与模式一致预留（原「气泡带 contentH/
       rows」分支删除——预留通道并入本条，气泡绘制期钳到条高）；条矩形
       记入 m_menuBarRect，键区 y 自 contentY+bandH 起（既有偏移通道）。
       候选带判据（xkb_imeBandWanted）不变：中文态时 m_imeBandRect=条矩
       形（组串态候选带替换菜单渲染，复用带三件套），空闲态归零（条渲
       染图标工具栏，xkb_menuBarPaint/Hit）。 */
    bandH = contentH / (rows + 1);
    XRect_init(&self->m_menuBarRect, contentX, contentY, contentW, bandH);
#if XVIRTUALKEYBOARD_ON
    if (xkb_imeBandWanted(self)) {
        XRect_init(&self->m_imeBandRect, contentX, contentY, contentW,
                   bandH);
    } else {
        XRect_init(&self->m_imeBandRect, 0, 0, 0, 0);
    }
#endif
    rowH = (contentH - bandH) / rows;
    if (rowH < 1) rowH = 1;
#if XVIRTUALKEYBOARD_ON
    /* 页容量注入：按带内 chip 排布区宽度换算（钳位 [1,9]）。放本处而
       非 setImeEnabled 一处——setMap/setMode/setPopovers/setButtonCtrl
       /resize 的重建路径全部汇入本函数，任意重建后带几何与页容量同
       步刷新（风险清单②）。控件未布局（宽 0）时 xkb_imeBandLayout
       兜底钳位，无除零。 */
    if (self->m_imeBandRect.height > 0)
        xkb_imeInjectPageSize(self, rowH);
#endif

    for (row = 0; row < rows; ++row) {
        int total = 0;
        int x = contentX;
        int y = contentY + bandH + row * rowH;
        int b;
        for (b = rowStart[row]; b < rowStart[row + 1]; ++b)
            total += (int)ctrls[b] & (int)XKEYBOARD_CTRL_WIDTH_MASK;
        if (total <= 0) total = 1;
        for (b = rowStart[row]; b < rowStart[row + 1]; ++b) {
            int unit = (int)ctrls[b] & (int)XKEYBOARD_CTRL_WIDTH_MASK;
            /* 边界式：每键占 unit/total 行宽，x 为像素边界直加（原式
               (x-contentX+unit)*W/T 把像素偏移当单位累加，宽度逐键乘
               W/T 复利爆炸至 INT_MAX 溢出，键位矩形互相错位）。 */
            int xEnd = x + (int)(((int64_t)unit * contentW) / total);
            rects[b].x = x;
            rects[b].y = y;
            rects[b].width = xEnd - x;
            rects[b].height = rowH;
            x = xEnd;
        }
    }

    /* 提交（拒绝路径不触及成员，保持原布局）。 */
    self->m_keyCount = count;
    for (i = 0; i < (int)count; ++i) {
        self->m_keyCtrls[i] = ctrls[i];
        self->m_keyRects[i] = rects[i];
        self->m_keyLabels[i] = labels[i];
    }
}

/** @brief 命中测试（控件局部坐标 → 按钮索引；未命中返回 BUTTON_NONE）。 */
static uint32_t xkb_hitTest(const XVirtualKeyboard* self, int x, int y)
{
    uint32_t i;
    if (!self) return XKEYBOARD_BUTTON_NONE;
#if XVIRTUALKEYBOARD_ON
    /* 组串收缩态（需求③）：键区矩形按全量口径排布、越出收缩控件边界
     * 被绘制裁剪——命中同口径失效（候选带以下整段 miss），防「键面 +2
     * 内缩不可见但带底 2px 条带仍可触发首行键」的可见性/命中脱节。 */
    if (xkb_composeCollapsedWanted(self) &&
        y >= self->m_imeBandRect.y + self->m_imeBandRect.height)
        return XKEYBOARD_BUTTON_NONE;
#endif
    for (i = 0; i < self->m_keyCount; ++i) {
        const XRect* r = &self->m_keyRects[i];
        if (x >= r->x && x < r->x + r->width &&
            y >= r->y && y < r->y + r->height)
            return i;
    }
    return XKEYBOARD_BUTTON_NONE;
}

/* ==================== 写入链（对标 lv_keyboard → textarea） ==================== */

/** @brief 向目标合成一次按键（XKeyEvent_init + XObject_event_base 直发，
 *         与真实输入同路径；demo 先例 xgui_window_demo.c:1332-1349）。 */
static void xkb_sendKey(XVirtualKeyboard* self, int key, XKeyboardModifiers modifiers)
{
    XKeyEvent ke;
    if (!self || !self->m_target) return;
    XKeyEvent_init(&ke, XEVENT_TYPE_KEY_PRESS, key, modifiers);
    XObject_event_base((XObject*)self->m_target, (XEvent*)&ke);
}

/** @brief 数字模式 +/- 键：翻转目标文本首字符符号（仅 XLineEdit 目标）。
 *  @details 主路径=光标 home → 原文首字符为 +/- 时选 1 删 → 行首插入
 *           新符号（全程公开 API，保留撤销栈与光标位，对标 LVGL 借临
 *           时移动光标的等价实现）；XLineEdit 适配缺席时仅诊断忽略。 */
static void xkb_signFlip(XVirtualKeyboard* self)
{
#if XLINEEDIT_ON
    XLineEdit* le;
    char first;
    const char* flipped;
    if (!self || !self->m_target) return;
    if (XClassGetVtable(self->m_target) != XLineEdit_class_init()) {
        XPrintf("[XVirtualKeyboard] +/-: 目标类型不支持符号翻转（仅 XLineEdit）\n");
        return;
    }
    le = (XLineEdit*)self->m_target;
    XLineEdit_home(le, false);
    first = XLineEdit_text(le)[0];
    flipped = (first == '-') ? "+" : "-";
    if (first == '+' || first == '-') {
        XLineEdit_setSelection(le, 0, 1);
        XLineEdit_del(le);
    }
    XLineEdit_insert(le, flipped);
#else
    (void)self;
    XPrintf("[XVirtualKeyboard] +/-: XLineEdit 适配缺席（XLINEEDIT_ON=0），忽略\n");
#endif /* XLINEEDIT_ON */
}

/**
 * @brief 把按钮文本写入输出目标（字符键公共出口）。
 * @details 双路：单字节可打印 ASCII（0x20-0x7E）合成 XKeyEvent 直发
 *          （类型无关；大写字母键自动携带 ShiftModifier——漏 Shift 则
 *          TEXT_UPPER 打出全小写，先例 xgui_window_demo.c:1343-1346）；
 *          其余（多字节 UTF-8/换行符）按目标 vtable 分派公开插入 API
 *          直写，目标改文本后其自身 textChanged/textEdited 信号照常发
 *          射。类型未识别时仅诊断（不猜测写入通道）。
 */
static void xkb_writeText(XVirtualKeyboard* self, const char* text)
{
    size_t len;
    if (!self || !self->m_target || !text || !text[0]) return;
    len = XStrlen(text);
    if (len == 1 && text[0] >= 0x20 && text[0] <= 0x7E) {
        XKeyboardModifiers mods = XKeyboardModifier_NoModifier;
        char out = text[0];
#if XVIRTUALKEYBOARD_ON
        /* shifthandler 出字链（shiftHandler :507-510 对齐）：上下文大写
         * 态（shiftActive||capsLockActive）小写键帽打出大写——布局不随
         * shift 重建（视觉映射不变），大写在此单点落地。手动 TextUpper
         * 模式（1#/ABC 表切换）走大写键帽，上下文态为 false 不受影响。 */
        {
            XVirtualKeyboardInputContext* ctx = xkb_contextOf(self);
            if (ctx && out >= 'a' && out <= 'z' &&
                XVirtualKeyboardInputContext_isUppercase(ctx)) {
                out = (char)(out - 'a' + 'A');
                mods = XKeyboardModifier_ShiftModifier;
            }
        }
#endif
        if (out >= 'A' && out <= 'Z')
            mods = XKeyboardModifier_ShiftModifier;
        xkb_sendKey(self, (int)(unsigned char)out, mods);
        return;
    }
#if XLINEEDIT_ON
    if (XClassGetVtable(self->m_target) == XLineEdit_class_init()) {
        XLineEdit_insert((XLineEdit*)self->m_target, text);
        return;
    }
#endif
#if XPLAINTEXTEDIT_ON
    if (XClassGetVtable(self->m_target) == XPlainTextEdit_class_init()) {
        XPlainTextEdit_insertPlainText((XPlainTextEdit*)self->m_target, text);
        return;
    }
#endif
#if XTEXTEDIT_ON
    if (XClassGetVtable(self->m_target) == XTextEdit_class_init()) {
        XTextEdit_insertPlainText((XTextEdit*)self->m_target, text);
        return;
    }
#endif
    XPrintf("[XVirtualKeyboard] writeText: 目标类型不支持直写，忽略（合成键路径"
            "仅承载单字节 ASCII）\n");
}

/** @brief 换行动作：XLineEdit 合成 Return（触发 returnPressed 信号链）；
 *         多行控件经 writeText 直插 "\n"（XPlainTextEdit/XTextEdit 的
 *         insertPlainText 支持 \n 跨行）；其余目标回退合成 Return。 */
static void xkb_sendNewline(XVirtualKeyboard* self)
{
    if (!self || !self->m_target) return;
#if XLINEEDIT_ON
    if (XClassGetVtable(self->m_target) == XLineEdit_class_init()) {
        xkb_sendKey(self, XKey_Return, XKeyboardModifier_NoModifier);
        return;
    }
#endif
#if XPLAINTEXTEDIT_ON
    if (XClassGetVtable(self->m_target) == XPlainTextEdit_class_init()) {
        xkb_writeText(self, "\n");
        return;
    }
#endif
#if XTEXTEDIT_ON
    if (XClassGetVtable(self->m_target) == XTextEdit_class_init()) {
        xkb_writeText(self, "\n");
        return;
    }
#endif
    xkb_sendKey(self, XKey_Return, XKeyboardModifier_NoModifier);
}

/**
 * @brief 按内置语义处理指定按钮（可复用入口，对标 lv_keyboard_def_event_cb）。
 * @return 已识别处理返回 true；按钮无文本/索引越界返回 false。
 */
static bool xkb_activateButton(XVirtualKeyboard* self, uint32_t buttonId)
{
    const char* text;
    XVarList* args;
    if (!self || buttonId >= self->m_keyCount) return false;
    self->m_selectedKey = buttonId;
    /* 任意按钮激活信号（对标按钮矩阵 VALUE_CHANGED，param=按钮 id）。 */
    args = XVarList_Create(XVar(int32_t, buttonId));
    if (args) {
        if (((XObject*)self)->m_signalSlot)
            XObject_emitSignal((XObject*)self,
                               (size_t)XVirtualKeyboard_buttonActivated_signal(NULL, 0),
                               args, NULL, NULL, XEVENT_PRIORITY_NORMAL);
        else
            XVarList_delete(args);
    }
    text = XVirtualKeyboard_buttonText(self, buttonId);
    if (!text || !text[0]) return false;
    /* 注意：中/EN 键的键帽绘制按中英态渲染 中/EN（VXKeyboard_paintEvent），
     * 但激活路由恒匹配槽位表标签 XKEYBOARD_LBL_IME——勿在此按态替换。 */
#if XVIRTUALKEYBOARD_ON
    /* 九键路由（Sogou 改版三阶段）：T9 款型且中文态——数字组键（ABC/
       DEF/…/WXYZ）馈引擎九键数字通道（feedT9Digit）、分词=
       feedDigitSeparator、重输=engine reset；馈入后组串显示=数字串
       （composingText 直显口径）、候选经既有模型钩子刷新（候选带复
       用）。已消费即终止；未消费（IME 裁剪/EN 态/其余标签）回落下方
       原地消费与既有链。 */
    if (xkb_t9RouteWanted(self) && xkb_t9RouteKey(self, text)) {
        xkb_imeSyncPageSize(self); /* 组串变化：变宽 chip 页容量同步。 */
        XWidget_update((XWidget*)self);
        return true;
    }
#endif
    /* 九键英文多击（T9 款型非中文路由态；2026-10-04 实测反馈——旧口径
     * 整表吞掉使英文九键完全不可输入）：数字组键连击循环字母（ABC 连
     * 点 a→b→c，800ms 窗口闭合）、0=空格、左列标点转 ASCII、分词/重
     * 输原地消费。 */
#if XVIRTUALKEYBOARD_ON
    if (self->m_layoutKind == XKeyboardLayout_PinyinT9 &&
        !xkb_t9RouteWanted(self) &&
        xkb_t9EnglishKey(self, text)) {
        XWidget_update((XWidget*)self);
        return true;
    }
#else
    if (self->m_layoutKind == XKeyboardLayout_PinyinT9 &&
        xkb_t9EnglishKey(self, text)) {
        XWidget_update((XWidget*)self);
        return true;
    }
#endif
    /* T9 未接线键（分词/重输/字母组标签）原地消费：九键路由缺席（IME
       裁剪/英文态）时仍防标签字面量直写/误触既有 ABC 大写切换语义
       （Sogou 改版一阶段口径保留）。 */
    if (self->m_layoutKind == XKeyboardLayout_PinyinT9 &&
        xkb_t9LabelUnwired(text))
        return true;
    /* 中/EN 切换键（拼音表与款型主表固定键）：IME 启用=既有中英切换
       （engine.setInputMode 翻转，两态均拦截，否则标签经字符键回路把
       字面量写进编辑框）；未启用=一键装配进中文态（搜狗语义，装配缺
       席/失败原地消费）。旧「m_imeEnabled && mode==User1」门随判据泛
       化一并放开（中/EN 标签只存在于拼音表与款型主表）。 */
    if (XStrcmp(text, XKEYBOARD_LBL_IME) == 0) {
#if XVIRTUALKEYBOARD_ON
        if (self->m_layoutKind == XKeyboardLayout_EnglishFull) {
            /* EnglishFull 固定英文态（Sogou 改版三阶段）：中/EN 键按
               压收敛回英文（已启用=程序化切回英文清组串；未启用不装
               配）——字母恒直写不进组串，原地消费。 */
            if (self->m_imeEnabled)
                (void)XVirtualKeyboard_setImeChinese(self, false);
        } else if (self->m_imeEnabled) {
            XVirtualKeyboard_setImeChinese(self,
                                           !XVirtualKeyboard_imeChinese(self));
        } else {
            (void)XVirtualKeyboard_setImeEnabled(self, true);
        }
#endif
        return true;
    }
#if XVIRTUALKEYBOARD_ON
    /* 拼音面板本地拦截（四条触发路径全部汇经本分发函数）：数字 1..9
       ——组串中面板分页换算后 model.selectItem（提交经插件候选钩子→
       context.commit→写入链）；其余按键单点经 xkb_routeKey 投 engine
       虚键（插件可消费：字母进组串/退格删组串/回车原串/空格首选），
       未消费回落内置控制键匹配与字符写入。路由门=Sogou 改版四阶段判
       据（PinyinFull 款型且中文态——款型主表/引擎拼音表两载体统一，
       EnglishFull 固定英文态字母不进组串）∪ 九键判据（Sogou 改版三
       阶段，T9 款型且中文态：空格提交首选/退格删末位数字/搜索回车原
       串走同一虚键链；0 键与左列标点插件不消费回落直写）。 */
    if (xkb_composeRouteWanted(self) && text[1] == '\0' &&
        text[0] >= '1' && text[0] <= '9' &&
        xkb_imeInterceptDigit(self, text[0]))
        return true;
    if ((xkb_composeRouteWanted(self) || xkb_t9RouteWanted(self)) &&
        xkb_routeKey(self, text)) {
        xkb_imeSyncPageSize(self); /* 组串变化：变宽 chip 页容量同步。 */
        XWidget_update((XWidget*)self);
        return true;
    }
#endif
    /* 布局款型 shift 键（PinyinFull/EnglishFull 主表固定键）：小写→
       大写（一次性）→锁定 三态循环，大写态换装大写键帽表（rebuild），
       锁定态 Shift 键 CHECKED 高亮（见 xkb_rebuildLayout 打位）。 */
    if (XStrcmp(text, XKEYBOARD_LBL_SHIFT) == 0) {
        self->m_shiftState = (self->m_shiftState + 1) % 3;
        xkb_kindShiftApply(self);
        return true;
    }
    /* 模式切换键。 */
    if (XStrcmp(text, XKEYBOARD_LBL_LOWER) == 0) {
        XVirtualKeyboard_setMode(self, XKeyboardMode_TextLower);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_UPPER) == 0) {
#if XVIRTUALKEYBOARD_ON
        /* shifthandler 单点（Qt Keyboard.qml shift 键 click→toggleShift
           口径）：单击=临时大写（sticky）、间隔内双击=capsLock 钉住、
           caps 下单击=解除——双击判定/互斥在 context.toggleShift 内部
           （XStyleHints 双击间隔数据源）。模式读数随 isUppercase 同步
           （TextUpper/TextLower），键帽布局不重建（视觉映射不变；大写
           出字经 writeText 的上下文大写钩子落地）。 */
        XVirtualKeyboardInputContext_toggleShift(xkb_contextOf(self));
        xkb_syncShiftMode(self);
        return true;
#else
        XVirtualKeyboard_setMode(self, XKeyboardMode_TextUpper);
        return true;
#endif
    }
    if (XStrcmp(text, XKEYBOARD_LBL_SPECIAL) == 0) {
        XVirtualKeyboard_setMode(self, XKeyboardMode_Special);
        return true;
    }
    /* 符/123 切换键（款型主表行 4 固定键）：复用既有 Special/Number
       模式机制（数字走数字盘，款型主表不再含数字行）。 */
    if (XStrcmp(text, XKEYBOARD_LBL_SYMBOL) == 0) {
        XVirtualKeyboard_setMode(self, XKeyboardMode_Special);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_NUMBERS) == 0) {
        XVirtualKeyboard_setMode(self, XKeyboardMode_Number);
        return true;
    }
    /* 确认键：仅发 ready 信号不自动收层（对标 LVGL 默认无动作）。 */
    if (XStrcmp(text, XKEYBOARD_LBL_OK) == 0) {
        XVarList* empty = XVarList_create(0);
        if (empty) {
            if (((XObject*)self)->m_signalSlot)
                XObject_emitSignal((XObject*)self,
                                   (size_t)XVirtualKeyboard_ready_signal(NULL),
                                   empty, NULL, NULL,
                                   XEVENT_PRIORITY_NORMAL);
            else
                XVarList_delete(empty);
        }
        return true;
    }
    /* 关闭/收起键：发 cancel 信号；弹层可见即自动收层（XGui 扩展行为）。
       组串中先弃草稿（engine reset——closePopup 弃草稿链一致口径）。 */
    if (XStrcmp(text, XKEYBOARD_LBL_CLOSE) == 0 ||
        XStrcmp(text, XKEYBOARD_LBL_DISMISS) == 0) {
#if XVIRTUALKEYBOARD_ON
        if (xkb_imeComposing(self) && xkb_engineOf(self))
            XVirtualKeyboardInputEngine_reset(xkb_engineOf(self));
#endif
        XVarList* empty = XVarList_create(0);
        if (empty) {
            if (((XObject*)self)->m_signalSlot)
                XObject_emitSignal((XObject*)self,
                                   (size_t)XVirtualKeyboard_cancel_signal(NULL),
                                   empty, NULL, NULL,
                                   XEVENT_PRIORITY_NORMAL);
            else
                XVarList_delete(empty);
        }
        if (XVirtualKeyboard_popupVisible(self)) XVirtualKeyboard_closePopup(self);
        return true;
    }
    /* 换行键：同时匹配 "Enter" 字面量（对标 KB.c:371 兼容 LVGL 移植）。
       搜索键（款型主表行 4 固定键）同语义合流——合成 XKey_Return，
       closeOnReturn 分支同享。closeOnReturn 新增分支：设置开且非
       MultiLine → 回车后收面板。 */
    if (XStrcmp(text, XKEYBOARD_LBL_NEWLINE) == 0 ||
        XStrcmp(text, XKEYBOARD_LBL_ENTER) == 0 ||
        XStrcmp(text, XKEYBOARD_LBL_SEARCH) == 0) {
        xkb_sendNewline(self);
#if XVIRTUALKEYBOARD_ON
        {
            XVirtualKeyboardSettings* settings =
                XVirtualKeyboardSettings_instance();
            if (settings && XVirtualKeyboardSettings_closeOnReturn(settings) &&
                !(XVirtualKeyboardInputContext_inputMethodHints(
                      xkb_contextOf(self)) &
                  XInputMethodHint_MultiLine) &&
                self->m_popped)
                XVirtualKeyboard_closePopup(self);
        }
#endif
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_LEFT) == 0) {
        xkb_sendKey(self, XKey_Left, XKeyboardModifier_NoModifier);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_RIGHT) == 0) {
        xkb_sendKey(self, XKey_Right, XKeyboardModifier_NoModifier);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_BACKSPACE) == 0) {
        xkb_sendKey(self, XKey_Backspace, XKeyboardModifier_NoModifier);
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_SIGN) == 0) {
        xkb_signFlip(self);
        return true;
    }
    /* 中文态标点随中英切换（2026-10-04 用户需求）：全键中文态的
       ","/"。" 落 "，"/"。"，英文态（含 EnglishFull 固定英文——款型
       判据不认引擎中文快照）落半角 ","/"."；组串中标点仍直写不入
       组串（拼音字母通道无标点位）。 */
    if (text[1] == '\0' && text[0] == ',' && xkb_composeRouteWanted(self)) {
        xkb_writeText(self, "\xEF\xBC\x8C");
        return true;
    }
    if (XStrcmp(text, XKEYBOARD_LBL_CJK_PERIOD) == 0 &&
        !(xkb_composeRouteWanted(self) || xkb_t9RouteWanted(self))) {
        xkb_writeText(self, ".");
        return true;
    }
    /* 其余按钮文本按字符键写入。 */
    xkb_writeText(self, text);
    /* 布局款型一次性大写回落：字母键入后展示态回小写（键帽换装大写
       表时大写字母直出大写；锁定态不回落）。 */
    if (self->m_shiftState == 1 && text[1] == '\0' &&
        ((text[0] >= 'a' && text[0] <= 'z') ||
         (text[0] >= 'A' && text[0] <= 'Z'))) {
        self->m_shiftState = 0;
        xkb_kindShiftApply(self);
    }
    return true;
}

/* ==================== 定时器（长按重复 + 守护轮询） ==================== */

/** @brief 停止长按重复定时器（幂等）。 */
/* [死码清理] xkb_stopRepeat 已删除：全仓无调用点（见审计清单）。
 */
/** @brief 按下可重复键时启动长按计时（先 400ms 起振；先停旧计时再判
 *         NO_REPEAT，避免前一按键的挂起计时串键）。 */
static void xkb_startRepeat(XVirtualKeyboard* self)
{
    const char* label;
    if (!self || self->m_pressedKey == XKEYBOARD_BUTTON_NONE) return;
    if ((int)self->m_keyCtrls[self->m_pressedKey] &
        (int)XKEYBOARD_CTRL_NO_REPEAT)
        return;
    /* 释放触发键（CLICK_TRIG/POPOVER）禁长按重复：重复在按住期间触
       发一次 + 释放再触发一次=同一次点按双写（2026-10-04 用户实测
       符号键「按一下出两份字符」；结构上任何释放触发键可重复都必
       然双写）。 */
    if ((int)self->m_keyCtrls[self->m_pressedKey] &
            (int)XKEYBOARD_CTRL_CLICK_TRIG ||
        (int)self->m_keyCtrls[self->m_pressedKey] &
            (int)XKEYBOARD_CTRL_POPOVER)
        return;
    /* 长按重复仅承载可重复文本键：单字母与退格。控制键重复无语义且
       有害——中/EN 重复触发=来回切换闪烁（2026-10-04 用户实测：触屏
       点按略超 600ms 起振间隔就切过去又切回来，表现为多次才能按中）；
       数字/空格/符号与九键组键多字符标签同此禁复——触屏上数字/符号
       长按重复即「按一下出两份字符」（用户同日实测），九键组键重复
       =多击连击换位次。退格保留长按连删。 */
    label = self->m_keyLabels[self->m_pressedKey];
    if (!label) return;
    if (XStrcmp(label, XKEYBOARD_LBL_BACKSPACE) != 0 &&
        (label[1] != '\0' ||
         !((label[0] >= 'a' && label[0] <= 'z') ||
           (label[0] >= 'A' && label[0] <= 'Z'))))
        return;
    /* 节拍单一事实源=engine 头宏（600/50；测试注入覆写同源生效）——
       「重复从面板 400/100 移入 engine 600/50」的节拍承载点。 */
    self->m_repeatTimer = XObject_startTimer_ms(
        (XObject*)self, XVIRTUALKEYBOARD_REPEAT_FIRST_MS,
        XTimerType_CoarseTimer);
}

/** @brief 长按 tick：重发当前按住键并切换到 50ms 重复间隔（重启定时器
 *         即换挡，无需相位计数）。 */
static void xkb_repeatTick(XVirtualKeyboard* self)
{
    if (self->m_pressedKey == XKEYBOARD_BUTTON_NONE || !self->m_pressArmed) {
        return;
    }
    xkb_activateButton(self, self->m_pressedKey);
    self->m_repeatTimer = XObject_startTimer_ms(
        (XObject*)self, XVIRTUALKEYBOARD_REPEAT_MS, XTimerType_CoarseTimer);
}

/** @brief 停止守护轮询定时器（幂等）。 */
static void xkb_stopGuard(XVirtualKeyboard* self)
{
    if (!self || self->m_guardTimer == XTIMER_INVALID_ID) return;
    XObject_killTimer((XObject*)self, self->m_guardTimer);
    self->m_guardTimer = XTIMER_INVALID_ID;
}

/** @brief 启动守护轮询（幂等；运行条件=弹层可见或 autoPopup 开；
 *         XVIRTUALKEYBOARD_DESKTOP_ON=0 时守护整体停用——仅显式
 *         popup/总开关形态）。 */
static void xkb_startGuard(XVirtualKeyboard* self)
{
    if (!self || self->m_guardTimer != XTIMER_INVALID_ID) return;
#if !XVIRTUALKEYBOARD_DESKTOP_ON
    return;
#endif
    if (!self->m_autoPopup && !self->m_popped) return;
    self->m_guardTimer = XObject_startTimer_ms(
        (XObject*)self, XKEYBOARD_GUARD_INTERVAL_MS, XTimerType_CoarseTimer);
}

/** @brief 解析对话框外置浮层的「范围顶层」（浮层落位与守护复核共用）：
 *         沿宿主对话框父链上溯到首个非 Dialog 型顶层（主窗等；独立
 *         对话框退对话框自身）。浮层形态 m_host=宿主对话框。 */
static XWidget* xkb_overlayScope(XVirtualKeyboard* self)
{
    XWidget* dlgTop;
    XWidget* scope;
    XWidget* p;
    if (!self || !self->m_host) return NULL;
    dlgTop = self->m_host;
    scope = dlgTop;
    for (p = XWidget_parentWidget(dlgTop); p;
         p = XWidget_parentWidget(p)) {
        XWidget* t = XWidget_topLevelWidget(p);
        if (t && t != dlgTop &&
            (XWidget_windowFlags(t) & (XWidgetFlags)XWindowType_TypeMask) !=
                (XWidgetFlags)XWindowType_Dialog) {
            scope = t;
            break;
        }
    }
    return scope;
}

/** @brief 外置浮层落位（2026-10-02 所有者裁定：对话框内不塞内嵌屏幕
 *         键盘——点击对话框内输入框时与主窗其他输入完全一致，键盘以
 *         独立 Popup 顶层浮层承载，可越出对话框边界）。
 *  @details 前置：面板已由 XVirtualKeyboard_popup 的 Dialog 分支按
 *           Popup 形态挂编辑框顶层（transient，绝不作对话框子控件）。
 *           落位基准=「外层范围顶层」（xkb_overlayScope 解析）：宽取范
 *           围顶层宽（与主窗形态「弹层宽==宿主宽」同刻度，外置大键盘）；
 *           高沿用 xkb_reposition 同一钳位公式 kbH=clamp(h/2,120,h-32)
 *           （与主窗内嵌键盘视觉同规格）；y=贴范围顶层底部（2026-10-03
 *           所有者裁定「这种大小的底部对齐」：与主窗/悬浮形态底部对齐
 *           同款，面板铺范围底层，对话框中部内容不被遮蔽）；x=范围顶
 *           层全局 x（左对齐主窗）。 */
static void xkb_repositionOverlay(XVirtualKeyboard* self)
{
    XWidget* scope;
    XPoint origin;
    XPoint sg;
    int scopeW;
    int scopeH;
    int kbH;
    int x;
    int y;
    if (!self) return;
    scope = xkb_overlayScope(self);
    if (!scope) return;
    scopeW = XWidget_width(scope);
    scopeH = XWidget_height(scope);
    kbH = scopeH / 2;
    if (kbH < XKEYBOARD_POPUP_MIN_H) kbH = XKEYBOARD_POPUP_MIN_H;
    if (kbH > scopeH - XKEYBOARD_POPUP_MARGIN_H)
        kbH = scopeH - XKEYBOARD_POPUP_MARGIN_H;
    if (kbH < 1) kbH = 1;
    origin.x = 0;
    origin.y = 0;
    /* 几何读 XWindow 记账（reposition 同款零滞后口径）：信号发射点在
       控件层几何回写之前，槽链上读控件级是上一帧值。顶层窗口记账几何
       即全局坐标。 */
    if (XWidget_windowHandle(scope)) {
        XWindow* scopeWin = (XWindow*)XWidget_windowHandle(scope);
        XRect wg = XWindow_geometry(scopeWin);
        scopeW = wg.width;
        scopeH = wg.height;
        sg.x = wg.x;
        sg.y = wg.y;
        kbH = scopeH / 2;
        if (kbH < XKEYBOARD_POPUP_MIN_H) kbH = XKEYBOARD_POPUP_MIN_H;
        if (kbH > scopeH - XKEYBOARD_POPUP_MARGIN_H)
            kbH = scopeH - XKEYBOARD_POPUP_MARGIN_H;
        if (kbH < 1) kbH = 1;
    } else {
        sg = XWidget_mapToGlobal(scope, &origin);
    }
    /* y=贴范围顶层底部（2026-10-03 所有者裁定「这种大小的底部对齐」）：
       与主窗/悬浮形态「宿主底部全宽」同款逻辑——面板铺范围底层，编辑
       器与面板间的对话框中部内容不被遮蔽；顶层无父偏移，全局坐标即屏
       幕坐标（XComboBox 弹层 setGeometryRect 先例）。 */
    x = sg.x;
    y = sg.y + scopeH - kbH;
    self->m_popupHeight = kbH;
    {
        XRect r;
        XRect current;
        XRect_init(&r, x, y, scopeW, kbH);
        /* 几何短路（reposition 同款）：范围顶层 x/y/w/h 信号成对到达，
           目标与当前一致时零操作——防布局 churn 与外部合法调整被打回。 */
        current = XWidget_rect((XWidget*)self);
        if (current.x == r.x && current.y == r.y &&
            current.width == r.width && current.height == r.height)
            return;
        XWidget_setGeometryRect((XWidget*)self, &r);
    }
}

/** @brief 按宿主当前几何重定位弹层（高度钳位：hostH/2 → 下界 120 →
 *         上界 hostH-32，上界最终生效恒给编辑区留 32px）。
 *  @details 三形态（2026-10-03 合并裁定方案 A）：①内嵌（m_floating=
 *           false 且无父窗形态）按宿主局部坐标
 *           setGeometry(0, hostH-kbH, hostW, kbH)；②悬浮形态
 *           （setHostWindow 锚生效）按宿主全局坐标
 *           setGeometry(gx, gy+hostH-kbH, hostW, kbH)——顶层无父偏移
 *           全局坐标即屏幕坐标（XComboBox 弹层 setGeometryRect 先例），
 *           外观/尺寸与内嵌形态完全一致（同一钳位公式、宿主全宽），
 *           仅坐标系不同；③对话框外置浮层（isWindow+有父，popup ②
 *           分支）在入口分流至 xkb_repositionOverlay——贴范围顶层底
 *           部落位、范围顶层全宽（同款钳位公式）。
 *           触发源（2026-10-03 所有者裁定「几何跟随走事件推送不走轮
 *           询」）：popup 期连接宿主/范围顶层 XWindow 的
 *           x/y/width/heightChanged 四信号（XPlatformNativeWindow_
 *           setSizeHints 同源的 setGeometryFields 逐字段发射，WM 拖
 *           拽/缩放经 ConfigureNotify 回写即发），槽内直呼本函数；
 *           width/height 信号成对到达时第二次经几何短路空转。守护
 *           tick 不再承担几何轮询。 */
static void xkb_reposition(XVirtualKeyboard* self)
{
    int kbH;
    XRect target;
    XRect current;
    XWindow* hostWin;
    if (!self || !self->m_host) return;
    /* 紧凑悬浮态守卫（第三形态）：宿主几何跟随整体跳过——紧凑矩形归
       用户拖移/切换所有，宿主 resize/拖动不得覆盖（Sogou 改版四阶段
       规格「不抢几何」）；守护 tick 顶层 raise 维护不受影响。 */
    if (self->m_compactFloat) return;
    /* 宿主几何读 XWindow 记账（信号发射点 setGeometryFields 在控件层
     * applyWindowGeometry 之前——ConfigureNotify→handleGeometryChange
     * 先发 widthChanged 再派发 Resize，槽内读 XWidget_* 是上一帧值、
     * 逐帧缩放恒滞后一档（2026-10-03 实测）；顶层 XWindow 几何即全局
     * 坐标，恰为悬浮/浮层落位所需。未建窗回退控件级（纯程序内路径）。 */
    hostWin = (XWindow*)XWidget_windowHandle((XWidget*)self->m_host);
    if (hostWin) {
        self->m_hostW = XWindow_width(hostWin);
        self->m_hostH = XWindow_height(hostWin);
    } else {
        self->m_hostW = XWidget_width(self->m_host);
        self->m_hostH = XWidget_height(self->m_host);
    }
    /* 外置浮层形态（Popup 独立顶层 + transient 挂宿主，2026-10-02
     * 裁定）：按编辑器下方落位重算（几何推导见
     * xkb_repositionOverlay），不走下方子控件铺底公式。 */
    if (XWidget_isWindow((XWidget*)self) &&
        XWidget_parentWidget((XWidget*)self)) {
        xkb_repositionOverlay(self);
        return;
    }
    kbH = self->m_hostH / 2;
    if (kbH < XKEYBOARD_POPUP_MIN_H) kbH = XKEYBOARD_POPUP_MIN_H;
    if (kbH > self->m_hostH - XKEYBOARD_POPUP_MARGIN_H)
        kbH = self->m_hostH - XKEYBOARD_POPUP_MARGIN_H;
    if (kbH < 1) kbH = 1;
    self->m_popupHeight = kbH;
    if (self->m_floating) {
        XPoint anchorGlobal;
        /* 顶层宿主全局原点=XWindow 记账位（信号时刻已新，零滞后）。 */
        if (hostWin) {
            XPoint hostPos = XWindow_position(hostWin);
            anchorGlobal.x = hostPos.x;
            anchorGlobal.y = hostPos.y + self->m_hostH - kbH;
        } else {
            XPoint anchorLocal;
            anchorLocal.x = 0;
            anchorLocal.y = self->m_hostH - kbH;
            anchorGlobal = XWidget_mapToGlobal(self->m_host, &anchorLocal);
        }
        XRect_init(&target, anchorGlobal.x, anchorGlobal.y,
                   self->m_hostW, kbH);
    } else {
#if XVIRTUALKEYBOARD_ON
        if (xkb_composeCollapsedWanted(self)) {
            /* 组串收缩（需求③）：目标高=候选带高+上下 2px 内缩（与
               rebuildLayout contentY=2 边距同口径），贴宿主底部全宽。
               带高按宿主全量几何推（kbH 即上方全量钳位高、contentH=
               kbH-4 与 rebuildLayout 一致），行数数键矩形 y 分层——
               不拿收缩后自身高度算，防「收缩→带更矮→更收缩」反馈
               自缩；带矩形本身由 resizeEvent→rebuildLayout 按全量
               contentH 重排，越界键区被裁剪。 */
            int fullContentH = kbH - 4;
            int bandH;
            int collapsedRows = 0;
            uint32_t ki;
            if (fullContentH < 1) fullContentH = 1;
            for (ki = 0; ki < self->m_keyCount; ++ki) {
                if (ki == 0 ||
                    self->m_keyRects[ki].y != self->m_keyRects[ki - 1].y)
                    ++collapsedRows;
            }
            if (collapsedRows < 1) collapsedRows = 1;
            bandH = fullContentH / (collapsedRows + 1);
            if (bandH < 1) bandH = 1;
            XRect_init(&target, 0, self->m_hostH - (bandH + 4),
                       self->m_hostW, bandH + 4);
        } else
#endif
        {
            XRect_init(&target, 0, self->m_hostH - kbH, self->m_hostW, kbH);
        }
    }
    /* 几何短路：width/height 信号成对到达（每次缩放两槽），第二次
       与首次目标一致时零操作返回，杜绝布局重排 churn；同时保住外部
       对键盘几何的合法调整不被打回（XDateTimeEdit 时间行避让同款保
       护语义）。 */
    current = XWidget_rect((XWidget*)self);
    if (current.x == target.x && current.y == target.y &&
        current.width == target.width &&
        current.height == target.height)
        return;
    XWidget_setGeometryRect((XWidget*)self, &target);
}

/**
 * @brief Z 序不变式维护（子控件浮层形态；守护 tick 与几何同步批处理共用）。
 * @details 宿主内兄弟 raise（chrome 重排、外源抬层等）会压过弹出键
 *          盘——弹出时刻的 raise 只有一次，须持续维护。幂等：已在顶
 *          层时 XWidget_raise 内部早退，零副作用。鼠标抓取者非键盘在
 *          场时让位：completer 下拉等内容浮层可见期持有鼠标抓取、后
 *          显示者临时压过键盘是既有语义，不与其争层；其收层归还抓取
 *          后下一调用自然恢复。悬浮形态不适用（原生 Popup 独立于宿主
 *          窗口，子控件 raise 不可达；其 Z 序不变式留在守护 tick）。
 */
static void xkb_maintainZOrder(XVirtualKeyboard* self)
{
    XWidget* grabber;
    if (!self || !self->m_popped || self->m_floating || self->m_compactFloat)
        return; /* 顶层形态（悬浮锚/紧凑悬浮）不适用子控件 raise；其 Z
                   序不变式由守护 tick 顶层 raise 分支维护。 */
    grabber = XWidget_mouseGrabber();
    if (grabber == NULL || grabber == (XWidget*)self)
        XWidget_raise((XWidget*)self);
}

/**
 * @brief 守护轮询 tick（单次遍历，幂等；焦点跟随兜底，非弹收主判据）。
 * @details 自动弹收主判据=按下位置驱动（notifyPress，见
 *          XGuiApplication_virtualKeyboardNotifyPress 转发链：PRESS 命中
 *          受支持编辑框→弹/重绑、非编辑→收，点空白收起不依赖焦点迁
 *          移）。本守护轮询降为兜底，保留原因：①焦点跟随换框（物理键
 *          盘 Tab 遍历/程序化 setFocus 等无 PRESS 的焦点迁移仍自动重
 *          绑/收层）；②IME 候选带等稳态巡检依赖轮询（宿主 Resize 重
 *          定位③）。三段（统一边沿，去 m_target 水平判据）：①m_target
 *          不可见→closePopup（原样）；②边沿 focus != m_prevFocus——焦
 *          点可接受（xkb_supportedTarget=vtable 快路径 ∪ WA14 放行回
 *          退，Qt inputMethodAccepted 口径）&& isEnabled && 总开关→
 *          popup(focus)，否则（不可接受目标/NULL/总开关）若
 *          m_popped→closePopup；两路均更新 m_prevFocus=focus—
 *          对齐 Qt evaluateInputPanelVisible = m_visible &&
 *          (focusObject && inputMethodAccepted())（platforminputcontext.cpp
 *          :251-259/266-283）的 show/hide 双向。行为变化声明：点按
 *          「取焦点」的非编辑控件将收键盘（Qt 口径；按下路径同判据即
 *          时收，本段 200ms 内兜底二次确认）；不取焦点的控件（XGui 按
 *          钮默认不抢焦点）的收起由按下路径即时承载（旧「无边沿不收」
 *          缺口已由 notifyPress 闭合）；焦点往返重弹；同框 retap 经按
 *          下路径重弹（旧 dismissFix「同框 retap 不重弹」契约作废）；
 *          ③宿主 Resize→重定位（原样）。②′稳态不可接受收层：
 *          焦点存在、accept 判据为假且弹层仍在时幂等收层——accept 可
 *          在焦点不变时翻转（原地 opt-out/禁用/总开关切换），无边沿也
 *          要复核（Qt hide 臂的轮询化）。销毁路径已由 destroyed 防悬
 *          垂连接前置处理。
 * @note     accept 不含可见性合取：合成/无头环境从未 show 的顶层编辑
 *           框生效可见性恒假（XWidget_effectiveVisible 走父链），而其
 *           持有应用焦点即为活动输入目标（⑦.4 长按豁免同理）；控件
 *           隐藏/禁用在公共 API 下会自动清焦（setVisible(false)/
 *           setEnabled(false) 路径 clearFocusBase），失焦即由②覆盖。
 */
static void xkb_guardTick(XVirtualKeyboard* self)
{
    XWidget* focus;
    bool accept;
    if (!self) return;
    /* ① 目标不可见且非当前输入焦点→收层（页切换/祖先隐藏链）。
       判据加「非当前焦点」豁免：无头/合成环境里从未 show 的顶层编辑
       框生效可见性恒假（XWidget_isVisible 走父链），但其持有应用焦点
       即为活动输入目标——旧判据会在长按重复进行中把面板收掉（⑦.4 
       长按 600ms 起振失败的根因）。Qt 对齐口径：收层判据=焦点对象是
       否仍被输入法接受（edge 分支 accept），非控件可见性。 */
    if (self->m_target && self->m_target != XWidget_appFocusWidget() &&
        !XWidget_isVisible(self->m_target)) {
        XVirtualKeyboard_closePopup(self);
        return;
    }
    /* ② 焦点边沿（dismissFix 核心：判据去 m_target 化，边沿触发）。
       accept 兜底口径=qwidget.cpp:9057-9065（isEnabled()&&
       WA_InputMethodEnabled；评审遗留修正：原缺 isEnabled 合取，父链
       setEnabled(false) 传播禁用的持焦编辑框会误报可接受）。总开关关
       → 边沿分支走 closePopup 路径（面板随开关收起）。 */
    focus = XWidget_appFocusWidget();
#if XVIRTUALKEYBOARD_ON
    accept = focus && xkb_supportedTarget(focus) &&
             XWidget_isEnabled(focus) &&
             XWidget_testAttribute(
                 focus, XWidgetAttribute_InputMethodEnabled) &&
             XVirtualKeyboardSettings_keyboardEnabled(
                 XVirtualKeyboardSettings_instance());
#else
    accept = focus && xkb_supportedTarget(focus) &&
             XWidget_isEnabled(focus) &&
             XWidget_testAttribute(
                 focus, XWidgetAttribute_InputMethodEnabled);
#endif
    if (focus != self->m_prevFocus) {
        self->m_prevFocus = focus; /* 两路均更新采样（先记边沿已消费）。 */
        /* 焦点迁出（≠收起时目标）即清用户收起闩锁：换框跟随恢复。 */
        if (focus != self->m_target) self->m_userCollapsed = false;
        if (accept) {
            if (self->m_userCollapsed && focus == self->m_target) {
                /* 用户收起后原地（同目标）不重弹——收起语义保持。 */
                self->m_prevFocus = focus;
                return;
            }
            XVirtualKeyboard_popup(self, focus);
            return;
        }
        if (self->m_popped) {
            /* hide 方向（评审#4）：焦点边沿迁入不可接受目标（非编辑控件/
               NULL/总开关关）即收层——行为矩阵「总开关关→已弹层收
               起」与 ⑦.1 hide 方向断言的承载点；closePopup 内部同步
               m_prevFocus 采样，防收层瞬间被记为新边沿。 */
            XVirtualKeyboard_closePopup(self);
            return;
        }
    }
    /* ②′ 稳态不可接受收层（focusFocusDismiss）：焦点存在但不可接受
       （非受支持编辑控件/WA14 opt-out/禁用/总开关关）且弹层仍在→
       幂等收层。无边沿也要收：accept 判据可在焦点不变时原地翻转
       （setAttribute(WA14,false)/父链禁用传播/总开关切换），边沿分支
       只覆盖判据翻转恰好伴随焦点迁移的情形；焦点为 NULL 保持现状
       （Qt 无头模式口径，不主动收）。 */
    if (focus && !accept && self->m_popped) {
        XVirtualKeyboard_closePopup(self);
        return;
    }
    /* ③ 几何跟随已事件化（2026-10-03 所有者裁定「取消轮询」）：popup
       期连接宿主/范围顶层 XWindow 的 x/y/width/heightChanged 四信号
       （xkb_bindGeometrySignals），WM 拖拽/缩放经 ConfigureNotify→
       XWindow_setGeometryFields 逐字段发射→槽内即时 reposition——
       原宿主 Resize 轮询比对（含对话框浮层形态范围顶层复核与
       m_hostGX/GY、m_scope* 缓存）整体退役。本 tick 保留 Z 序不变式
       复核：悬浮形态（S5 回修）+ 子控件浮层形态（xkb_maintainZOrder，
       守护周期 200ms 兜底；几何同步批处理的逐批维护见
       xkb_hostGeomSlot 的 30ms 定时器路径）。 */
#if XWINDOW_ON
#endif
    /* 2026-10-05 移除守护周期抬层（用户裁决）：Popup 型窗口经
       XWidget.c 顶层表自动设 transient parent（owner=主窗）——owned
       popup 恒在 owner 之上，唤醒（popup/show）时置顶一次即稳定，无
       需周期维护。子控件浮层形态的兄弟抬层仍由 xkb_maintainZOrder
       负责（早退分支不适用顶层形态）。 */
    xkb_maintainZOrder(self);
}

/* ==================== 虚槽实现 ==================== */

/** @brief 绘制：面板底 + 逐键矩形 + 文本居中 + 按压态 + CHECKED 底色 +
 *         气泡带 + 拼音候选带（XVK）。全部键位文本用控件当前字体
 *         （家族=配置默认字库；像素字号恒为构造默认，不随行高缩放）
 *         居中绘制，控件零字体家族自带（不调
 *         XFont_setFamily）；键面/候选带配色=LVGL 单一路径（主题宏
 *         已删，双分支合并）。 */
static void VXKeyboard_paintEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    XPainter painter;
    XImage* image;
    XPoint offset;
    XRect r = XWidget_rect(self);
    int bandH = 0;
    int rowH = 0;
    uint32_t i;

    (void)event;
    if (!kb || r.width <= 4 || r.height <= 4) return;
    r.x = 0;
    r.y = 0;

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
    /* 绘制字体 = 控件字体副本 + 随键盘几何等比的像素字号（2026-10-05
     * 需求：键盘大字大、等比例缩放）。锚=首行键高，labelPx=键高×35%
     * 钳 [9,48]（800×600 缺省键高≈46px → 16px，与历史恒定字号一致，
     * 缺省观感不变；键盘随窗口/面板放大时字同等放大）。仅替换本次
     * paint 的 painter 字体，不改控件字体状态——历史 xkb_syncLabel
     * PixelSize 逐帧同步控件字号的漂移根因（根因 d1）不再复现：字号
     * 是几何的只读推导，几何不变则字号不变，无反馈回路。 */
    {
        XFont scaled;
        int keyH = kb->m_keyCount > 0 ? kb->m_keyRects[0].height : 0;
        int labelPx = keyH > 0 ? keyH * XKB_LABEL_PX_PER_KEYH_PERCENT / 100 : 0;
        if (labelPx < 9) labelPx = 9;
        if (labelPx > 48) labelPx = 48;
        XFont_init(&scaled);
        XClassCopy(&scaled, &((XWidget*)self)->m_font);
        XFont_setPixelSize(&scaled, labelPx);
        XPainter_setFont(&painter, &scaled);
        XClassDeinit(&scaled);
    }

    /* 主题化面板（LVGL keyboard 容器=scr 样式：平涂 0xF5F5F5、无凹
       边、无圆角；唯一绘制路径，调色板旧分支随
       XKEYBOARD_THEME_LVGL_ON 删除）。 */
    XPainter_fillRect(&painter, &r, XKB_LVGL_SCR);
    /* 气泡带高度（布局菜单条常驻后条=键区上方唯一预留带，气泡带通道
       并入条通道：条高=contentH/(rows+1)，气泡绘制钳在条内——原
       contentH/rows 会越过条底压住首行键面）。 */
    if (kb->m_popovers && kb->m_keyCount > 0) {
        int rows = 1;
        for (i = 0; i + 1 < kb->m_keyCount; ++i) {
            if (kb->m_keyRects[i + 1].y != kb->m_keyRects[i].y) ++rows;
        }
        for (i = 0; i < kb->m_keyCount; ++i) {
            if (kb->m_keyRects[i].y != kb->m_keyRects[0].y) break;
            if (((int)kb->m_keyCtrls[i] & (int)XKEYBOARD_CTRL_POPOVER) != 0) {
                bandH = (r.height - 4) / (rows + 1);
                break;
            }
        }
        rowH = kb->m_keyRects[0].height;
    }

#if XVIRTUALKEYBOARD_ON
    /* 菜单条双模（键区上方常驻顶行）：组串态=拼音候选带（布局已预留
       m_imeBandRect=条矩形，候选替换菜单）；空闲态=图标工具栏（四等
       分，收起=closePopup 直连）。 */
    if (xkb_menuBarBandMode(kb))
        xkb_imeBandPaint(kb, &painter);
    else
        xkb_menuBarPaint(kb, &painter);
#else
    /* XVK 裁剪：无候选带形态，条恒为图标工具栏。 */
    xkb_menuBarPaint(kb, &painter);
#endif

    /* 主题化逐键：底色（CHECKED→GREY、按住武装→dark_filter(35) 颜色
       数学）+ 圆角矩形一次成形（绘制期内缩 2px 形成键视觉间距，
       m_keyRects 命中几何不动；LVGL 键面无 border/无阴影）。 */
    for (i = 0; i < kb->m_keyCount; ++i) {
        const XRect* kr = &kb->m_keyRects[i];
        XRect faceRect;
        bool pressed = (kb->m_pressedKey == i && kb->m_pressArmed);
        bool checked = ((int)kb->m_keyCtrls[i] &
                        (int)XKEYBOARD_CTRL_CHECKED) != 0;
        const char* text = XVirtualKeyboard_buttonText(kb, i);
        /* 搜索键强调色（搜狗蓝键面白字，Sogou 改版一阶段；CHECKED 灰
           面与按压灰让位强调色，按压态用压暗强调保持反馈）。判据取
           LBL_SEARCH——中/EN 键帽替换（下方）只改 LBL_IME 标签，置于
           替换前后等价，声明集中在键循环块首。 */
        bool accent = text && XStrcmp(text, XKEYBOARD_LBL_SEARCH) == 0;
        uint32_t fill = pressed ? (accent ? XKB_ACCENT_PRESSED
                                          : XKB_LVGL_PRESSED_FACE)
                                : (accent ? XKB_ACCENT
                                          : (checked ? XKB_LVGL_GREY
                                                     : XKB_LVGL_CARD));
        uint32_t txt = accent ? XKB_ACCENT_TEXT
                              : (pressed ? XKB_LVGL_PRESSED_TEXT
                                         : XKB_LVGL_TEXT);
#if XVIRTUALKEYBOARD_ON
        /* 中/EN 键键帽随中英态渲染（搜狗口径；命中/路由恒匹配槽位表
         * 标签 XKEYBOARD_LBL_IME，仅绘制替换——静态标签无态指示）。
         * 顶部菜单条候选带中的「中」chip 已有态指示，此处覆盖键面。 */
        if (text && XStrcmp(text, XKEYBOARD_LBL_IME) == 0)
            text = xkb_imeChineseState(kb) ? XKB_IME_KEY_CN : XKB_IME_KEY_EN;
#endif
        if (kr->width < 5 || kr->height < 5) continue;
        faceRect.x = kr->x + 2;
        faceRect.y = kr->y + 2;
        /* 宽高 -5 而非 -4：xkb_roundRect→XPainter_drawRoundedRect 按
           路径 x+w/y+h 收边后再描 1px 同色边（落在填充区外侧的
           x+w/y+h 边界像素上，XPainter.c drawRoundedRect 填充+折线描
           边两段式），-4 会让键面白色右/下各溢出 1px、4px 面板缝变
           3px（风格检查 pixelPlan ①「4px 缝全为面板」失配）。-5 使
           填充+描边合成可视面恰为命中矩形四边内缩 2px。 */
        faceRect.width = kr->width - 5;
        faceRect.height = kr->height - 5;
        xkb_roundRect(&painter, &faceRect, fill, fill,
                      xkb_keyRadius(kr->height));
        if (text && text[0]) {
            /* 键帽标签按"键面∪本键命中矩形降部带"裁剪：drawTextRect
               默认裁剪到布局矩形，而行盒（ascent+descent+lineGap 的
               组合）把基线贴到 faceRect 底缘，p/q/y/g/j 的下伸部整段
               落在键面外被裁成 D/V/i 形（悬浮小键盘页巡检缺陷）。裁
               剪 y 向放宽到命中矩形底：相邻键的命中矩形互不重叠、行
               间无缝，墨迹最远落在自己键位的底缘，不会压花邻键键面；
               x 与标签布局（faceRect 居中）保持不变。save 失败则退回
               默认裁剪绘制（降部缺失但不影响键面）。 */
            XRect descRect = faceRect;
            if (XPainter_save(&painter))
            {
                descRect.height = kr->y + kr->height - descRect.y;
                XPainter_setClipRect(&painter, &descRect,
                                     XPainterClipOperation_IntersectClip);
                XPainter_drawTextRect(&painter, &faceRect,
                                      XPAINTER_TEXT_ALIGN_CENTER |
                                          XPAINTER_TEXT_SINGLE_LINE |
                                          XPAINTER_TEXT_DONT_CLIP,
                                      text, txt);
                XPainter_restore(&painter);
            }
            else
            {
                XPainter_drawTextRect(&painter, &faceRect,
                                      XPAINTER_TEXT_ALIGN_CENTER |
                                          XPAINTER_TEXT_SINGLE_LINE,
                                      text, txt);
            }
        }
        /* 气泡：非浮层、绘制期实现——按压中的 POPOVER 键文本在键位上
           方预留带内放大区域绘制（relocate 语义对标 BM.c:765-767,
           796-800；同键面样式）。 */
        if (pressed && bandH > 0 &&
            ((int)kb->m_keyCtrls[i] & (int)XKEYBOARD_CTRL_POPOVER) != 0 &&
            text && text[0]) {
            XRect bubble;
            int bw = kr->width * 2;
            if (bw < 24) bw = 24;
            bubble.x = kr->x + kr->width / 2 - bw / 2;
            if (bubble.x < 2) bubble.x = 2;
            if (bubble.x + bw > r.width - 2) bubble.x = r.width - 2 - bw;
            if (bubble.x < 2) bubble.x = 2;
            bubble.y = 2;
            bubble.width = bw;
            bubble.height = (bandH > rowH ? bandH : rowH) - 4;
            if (bubble.height < 1) bubble.height = 1;
            xkb_roundRect(&painter, &bubble, XKB_LVGL_CARD, XKB_LVGL_CARD,
                          xkb_keyRadius(rowH));
            XPainter_drawTextRect(&painter, &bubble,
                                  XPAINTER_TEXT_ALIGN_CENTER |
                                      XPAINTER_TEXT_SINGLE_LINE,
                                  text, XKB_LVGL_TEXT);
        }
    }

    /* 键盘选择面板（工具栏「键盘选择」图标开启；覆盖键区最后绘制，
       面板绘制风格与键面一致——关闭期空操作）。 */
    xkb_layoutSelectorPaint(kb, &painter);
    /* 文字编辑面板（工具栏「文字编辑」图标开启；覆盖键区最后绘制，
       与选择面板互斥开合——关闭期空操作）。 */
    xkb_editPanelPaint(kb, &painter);

    XPainter_end(&painter);
    XPainter_deinit(&painter);
}

/** @brief 调整大小：几何随尺寸重建（行宽/行高/气泡带均按比例重算）。 */
static void VXKeyboard_resizeEvent(XWidget* self, XEvent* event)
{
    if (!self || !event || XEvent_type(event) != XEVENT_TYPE_RESIZE) return;
    xkb_rebuildLayout((XVirtualKeyboard*)self);
    XWidget_update(self);
}

/** @brief 鼠标按下：命中→武装（按下触发键立即激活；CLICK_TRIG/POPOVER
 *         键留释放触发）+ 可重复键起振长按计时 + 隐式抓取（按住滑动
 *         出键的取消/恢复语义依赖移动事件，对标 XLineEdit 按压抓取）。 */
static void VXKeyboard_mousePressEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    XMouseEvent* me;
    XPoint pos;
    uint32_t id;
    if (!kb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_PRESS) return;
    me = (XMouseEvent*)event;
    if (XMouseEvent_button(me) != XMouseButton_LeftButton) {
        XEvent_ignore(event);
        return;
    }
    pos = XMouseEvent_position(me);
#if XWINDOW_ON
    /* 悬浮态界外按下：收层+重放（Qt Popup 点外语义）。悬浮双抓取+原
       生捕获是本面板收到界外点击的唯一通道（跨顶层抓取改道 XWidget.c
       派发入口把一切按下改投抓取者），界外坐标在键面命中天然全 miss，
       原实现吞掉即死锁（日历弹层越界收层/点选收层/主窗口关闭钮全部
       不可达）。先 closePopup 成对释放并归还抢占前抓取者，重放管线即
       无抓取态；重放单次派发无回环（界外坐标不可能再落回已收层键
       盘）。 */
    if (kb->m_floating && kb->m_popped &&
        (pos.x < 0 || pos.y < 0 ||
         pos.x >= XWidget_width(self) || pos.y >= XWidget_height(self))) {
        XPoint g = XMouseEvent_globalPosition(me);
        XWidget* redir;
        if (g.x == 0 && g.y == 0) /* 平台未注入全局坐标兜底（XEvent.h
                                     globalPosition 契约；win32 必注入）。 */
            g = XWidget_mapToGlobal(self, &pos);
        XVirtualKeyboard_closePopup(kb);
        /* closePopup 刚归还的被抢占弹层（日历/时间弹层链；prev==NULL
           表示无被抢占弹层，下方第一发重放已直达命中控件）。 */
        redir = XWidget_mouseGrabber();
        XWidget_replayPressAtGlobal(me, &g);
        /* 链式二次派发（S4：一次点击级联关闭整条弹层链并直达真实命中
           控件，主窗自绘关闭钮首击生效）：第一发重放被恢复的弹层抓取
           改道→弹层越界分支同步自关（此刻鼠标/键盘抓取者皆空）。判据
           三合一：①确有被抢占弹层归还（redir 非空且非自己）；②命中点
           在弹层顶层几何之外（排除「弹层内容自关」——如点选日期经
           calClick 收层，此拍已被弹层内容消费，不得再透传）；③两抓取
           者已空（弹层链确已闭合）。三判据齐备才补发第二发——此刻无
           抓取者，直达光标下控件（Qt 点外重放口径）。 */
        if (redir && redir != (XWidget*)kb) {
            XWidget* redirTop = XWidget_topLevelWidget(redir);
            if (redirTop && !XWidget_mouseGrabber() &&
                !XWidget_keyboardGrabber()) {
                XPoint lp = XWidget_mapFromGlobal(redirTop, &g);
                if (lp.x < 0 || lp.y < 0 ||
                    lp.x >= XWidget_width(redirTop) ||
                    lp.y >= XWidget_height(redirTop))
                    XWidget_replayPressAtGlobal(me, &g);
            }
        }
        XEvent_accept(event);
        return;
    }
#endif
    /* 键盘选择面板命中（最高优先，Sogou 改版三阶段）：面板开启期点
       击一律消费——行=选款型并关闭、面板外（含再点工具栏图标）=关闭；
       「点面板外/再点图标=关闭」不透键区不重触发。面板关闭零开销。 */
    if (xkb_layoutSelectorHit(kb, pos.x, pos.y)) {
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    /* 文字编辑面板命中（次优先，Sogou 改版三阶段收尾）：面板内点击
       一律消费（动作键 press 直触发、禁用键/空白消费不动作）；面板外
       （菜单条区）透传工具栏——「再点图标」返回即走此路。 */
    if (xkb_editPanelHit(kb, pos.x, pos.y)) {
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    /* 菜单条命中（xkb_hitTest 之前；Sogou 改版二阶段）：键位矩形起点
       已在条下方，两区域天然无交叠，未命中条才走按键路径。双模分流——
       组串态=候选带（xkb_imeBandHit 委托，press 直触发，不进
       m_pressedKey 武装链）；空闲态=图标工具栏（按压武装 m_pressedTool
       + 隐式抓取，按压态视觉与键面一致，释放触发）。 */
    if (xkb_menuBarHit(kb, pos.x, pos.y)) {
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    id = xkb_hitTest(kb, pos.x, pos.y);
    kb->m_pressedKey = id;
    kb->m_pressArmed = (id != XKEYBOARD_BUTTON_NONE);
    if (id != XKEYBOARD_BUTTON_NONE) {
        int ctrl = (int)kb->m_keyCtrls[id];
        if ((ctrl & (int)XKEYBOARD_CTRL_CLICK_TRIG) == 0 &&
            (ctrl & (int)XKEYBOARD_CTRL_POPOVER) == 0) {
            /* 默认按下触发（无 CLICK_TRIG 且无 POPOVER）。 */
            xkb_activateButton(kb, id);
        }
        xkb_startRepeat(kb);
    }
    XWidget_grabMouse(self);
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 紧凑悬浮拖移步的 fbdev 免重绘移窗（拖动残影+FPS 5.9 根修
 *         2026-10-06）。
 *  @details 调用前提：新几何已落地（条带归位以「本窗已离开旧位」为前
 *           提，xwd_applyMove 同序）。两步——①让位条带按归属归位还原
 *           （XWindowDecoration_restoreExposeStrips：条带与其他可见顶
 *           层的交集自各归属后备缓冲直搬两缓冲，真桌面余部填桌面底色；
 *           旧实现无此步，旧位键盘像素长期残留=真机残影）；②本窗既有
 *           后备缓冲整窗直写两缓冲（纯移动内容零变化，后备缓冲仍是上
 *           一帧合法合成结果——零重绘零 flush 零翻页，拖动每步成本从
 *           全键盘软件重绘+整窗提交（A33 实测 FPS 5.9）降到一次 memcpy；
 *           两缓冲同内容，HUD 等并发 present 的中途翻页也不失步，差带
 *           账本语义不变，松手 update 的真实 PAINT 照常收敛）。后备缺
 *           位（未首绘等罕见态）退化为整窗 update（旧行为，慢但无残
 *           影）。已知边界：直写无遮挡剔除，键盘扫过更高层弹层（性能
 *           悬浮窗）矩形时该矩形被键盘内容盖写 ≤ 一个悬浮窗重绘周期
 *           （250ms 档）自愈。 */
static void xkb_compactDragMovePresent(XVirtualKeyboard* self,
                                       const XRect* oldG, const XRect* newG)
{
    XWindow* win;
    if (!self || !oldG || !newG) return;
    win = (XWindow*)XWidget_windowHandle((XWidget*)self);
    if (!win) return;
    XWindowDecoration_restoreExposeStrips(oldG, newG, win);
#if XBACKINGSTORE_ON && XPLATFORMBACKINGSTORE_ON
    {
        XBackingStore* bs = XWidget_backingStore((XWidget*)self);
        XPlatformBackingStore* pbs = bs ? XBackingStore_handle(bs) : NULL;
        XImage* img = pbs ? XPlatformBackingStore_paintDevice(pbs) : NULL;
        if (pbs && img && !XImage_isNull(img))
        {
            XRect whole;
            XPoint origin;
            XRect_init(&whole, newG->x, newG->y, newG->width, newG->height);
            XPoint_init(&origin, newG->x, newG->y);
            XPlatformBackingStore_blitPanelRects(pbs, &whole, 1, &origin);
            return;
        }
    }
#endif
    /* 罕见退化（后备缺位/裁剪构建）：整窗 update 让常规管线补画。 */
    XWidget_update((XWidget*)self);
}

/** @brief 鼠标移动：按住滑动出键取消武装不触发、回滑恢复（BM.c:446-506
 *         语义）；仅在按下序列内生效。工具栏按住滑动出条同语义（取消
 *         武装，回条不恢复——工具栏为释放触发单发语义）。 */
static void VXKeyboard_mouseMoveEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    XMouseEvent* me;
    XPoint pos;
    uint32_t id;
    bool armed;
    if (!kb || !event || XEvent_type(event) != XEVENT_TYPE_MOUSE_MOVE) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (kb->m_compactDrag) {
        /* 紧凑悬浮拖移：move 求 delta 移窗（全局=窗口左上+本地，锚点
           偏移恒定→窗口随指移动；几何跟随守卫期 reposition 不参与）。
           fbdev 无 WM：几何落地后让位条带归位+既有后备缓冲整窗直搬
           （免重绘移窗，拖动残影+FPS 5.9 根修 2026-10-06），面板钳边
           同 xwd_applyMove；桌面 WM 环境维持纯 setGeometry（重铺归窗
           口系统）。 */
        XPoint g = XWidget_mapToGlobal((XWidget*)self, &pos);
        XRect oldG;
        XRect newG;
        XRect panel;
        int nx;
        int ny;
        oldG = XWidget_geometry(self);
        nx = g.x - kb->m_dragOffX;
        ny = g.y - kb->m_dragOffY;
        if (XWindowDecoration_fbdevPanelRect(&panel)) {
            if (nx < panel.x) nx = panel.x;
            if (ny < panel.y) ny = panel.y;
            if (nx + oldG.width > panel.x + panel.width)
                nx = panel.x + panel.width - oldG.width;
            if (ny + oldG.height > panel.y + panel.height)
                ny = panel.y + panel.height - oldG.height;
        }
        if (nx != oldG.x || ny != oldG.y) {
            XRect_init(&newG, nx, ny, oldG.width, oldG.height);
            XWidget_setGeometryRect(self, &newG);
            xkb_compactDragMovePresent(kb, &oldG, &newG);
        }
        XEvent_accept(event);
        return;
    }
    if (kb->m_pressedTool >= 0) {
        /* 工具栏按住滑动出条：取消武装（视觉复位；释放不再触发）。 */
        if (xkb_menuBarCellAt(kb, pos.x, pos.y) < 0) {
            kb->m_pressedTool = -1;
            XWidget_update(self);
        }
        XEvent_accept(event);
        return;
    }
    if (kb->m_pressedKey == XKEYBOARD_BUTTON_NONE) return;
    id = xkb_hitTest(kb, pos.x, pos.y);
    armed = (id == kb->m_pressedKey);
    if (armed != kb->m_pressArmed) {
        kb->m_pressArmed = armed;
        if (armed)
            xkb_startRepeat(kb); /* 回滑恢复：重起长按计时。 */
        else
        XWidget_update(self);
    }
    XEvent_accept(event);
}

/** @brief 触摸按住拖动判定（纯谓词，无副作用）：紧凑悬浮态且起点落在
 *         工具栏内空白（图标盒之外）→可认领。镜像 xkb_menuBarHit 紧凑
 *         分支的启动条件，供 TouchDragEvent 槽定夺（不在此起拖/抓取—
 *         拖拽由框架左键按住拖动仿真续演本控件既有鼠标拖移逻辑）。 */
static bool xkb_touchDragClaimAt(XVirtualKeyboard* self, int x, int y)
{
    int slot;
    if (!self || !self->m_compactFloat) return false;
    if (self->m_menuBarRect.height <= 0 || self->m_menuBarRect.width <= 0)
        return false;
    if (x < self->m_menuBarRect.x ||
        x >= self->m_menuBarRect.x + self->m_menuBarRect.width ||
        y < self->m_menuBarRect.y ||
        y >= self->m_menuBarRect.y + self->m_menuBarRect.height)
        return false;
#if XVIRTUALKEYBOARD_ON
    if (xkb_menuBarBandMode(self)) return false; /* IME 带非拖移面。 */
#endif
    slot = xkb_menuBarCellAt(self, x, y);
    if (slot < 0) return false;
    return !xkb_menuBarIconHot(self, slot, x, y); /* 压图标=工具栏动作。 */
}

/** @brief 触摸手势槽：按住拖动（DragBegin）判定认领——紧凑悬浮态工具
 *         栏空白即接受，框架转左键按住拖动仿真（press 续持+MOVE 随行
 *         +RELEASE 收口），本控件既有紧凑拖移管线（m_compactDrag：press
 *         起拖+grabMouse、move 求 delta 移窗、release 收尾）原样驱动；
 *         其余手势种类/形态不认领（tap 走按键面合成鼠标语义）。 */
static void VXKeyboard_touchDragEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    XPoint pos;
    if (!kb || !event || XEvent_type(event) != XEVENT_TYPE_TOUCH_DRAG)
        return;
    if (XTouchEvent_gesture((const XTouchEvent*)event) !=
        (int)XTouchGesture_DragBegin)
        return;
    pos = ((const XTouchEvent*)event)->m_position;
    if (!xkb_touchDragClaimAt(kb, pos.x, pos.y)) return;
    XEvent_accept(event);
}

/** @brief 鼠标释放：工具栏按住原格释放触发（滑动出格已取消武装）；
 *         释放触发键（CLICK_TRIG/POPOVER）在按住原键且未滑出时激活；
 *         解除按压态/抓取/长按计时。 */
static void VXKeyboard_mouseReleaseEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    XMouseEvent* me;
    XPoint pos;
    uint32_t id;
    if (!kb || !event ||
        XEvent_type(event) != XEVENT_TYPE_MOUSE_BUTTON_RELEASE) return;
    me = (XMouseEvent*)event;
    pos = XMouseEvent_position(me);
    if (kb->m_compactDrag) {
        /* 紧凑悬浮拖移收尾：结束拖移态并归还抓取（窗位已在 move 落
           定）；落定=同步整窗重绘（repaint）——拖移步免重绘直搬只保
           证可见面，真实整窗 PAINT 把两缓冲欠账一次结清（xwd 落定同
           款契约，异步 update 的区域=m_dirty 快照不可靠）。 */
        kb->m_compactDrag = false;
        if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
        XWidget_repaint(self);
        XEvent_accept(event);
        return;
    }
    if (kb->m_pressedTool >= 0) {
        /* 工具栏释放触发：落回原格才激活（收起=closePopup 直连）；
           按压态清除与抓取归还先行于键面逻辑。 */
        int slot = kb->m_pressedTool;
        kb->m_pressedTool = -1;
        if (xkb_menuBarCellAt(kb, pos.x, pos.y) == slot)
            xkb_menuBarActivate(kb, slot);
        if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
        XWidget_update(self);
        XEvent_accept(event);
        return;
    }
    id = xkb_hitTest(kb, pos.x, pos.y);
    if (kb->m_pressedKey != XKEYBOARD_BUTTON_NONE && kb->m_pressArmed &&
        id == kb->m_pressedKey) {
        int ctrl = (int)kb->m_keyCtrls[id];
        if ((ctrl & (int)XKEYBOARD_CTRL_CLICK_TRIG) != 0 ||
            (ctrl & (int)XKEYBOARD_CTRL_POPOVER) != 0)
            xkb_activateButton(kb, id);
    }
    kb->m_pressedKey = XKEYBOARD_BUTTON_NONE;
    kb->m_pressArmed = false;
#if XVIRTUALKEYBOARD_ON
#endif
    if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
    XWidget_update(self);
    XEvent_accept(event);
}

/** @brief 隐藏：停长按计时；弹层态复位。守护定时器按运行条件保留
 *         （autoPopup 开时键盘隐藏期间仍需轮询以便自动再弹出）。 */
static void VXKeyboard_hideEvent(XWidget* self, XEvent* event)
{
    XVirtualKeyboard* kb = (XVirtualKeyboard*)self;
    if (!kb || !event || XEvent_type(event) != XEVENT_TYPE_HIDE) return;
    kb->m_popped = false;
    kb->m_pressedKey = XKEYBOARD_BUTTON_NONE;
    kb->m_pressArmed = false;
    kb->m_pressedTool = -1; /* 工具栏按压武装随层清除。 */
    kb->m_layoutSelectorOpen = false; /* 键盘选择面板随隐藏关闭。 */
    kb->m_editPanelOpen = false; /* 文字编辑面板随隐藏关闭。 */
    kb->m_editSelArmed = false;
    kb->m_compactFloat = false; /* 紧凑悬浮态随隐藏复位。 */
    kb->m_compactDrag = false;
    kb->m_physKeyActive = false; /* 物理会话标记随隐藏复位。 */
    xkb_t9TapReset(kb); /* 九键英文多击循环随隐藏闭合。 */
#if XVIRTUALKEYBOARD_ON
    /* 页切换/祖先隐藏弃草稿（engine reset→插件 resetComposition，
       不翻转中文态）。 */
    if (xkb_engineOf(kb)) XVirtualKeyboardInputEngine_reset(xkb_engineOf(kb));
#endif
    if (XWidget_mouseGrabber() == self) XWidget_releaseMouse(self);
    /* 悬浮面板隐藏随行释放键盘抓取与原生鼠标捕获（弹出时 popup 悬浮
       分支夺取的弹层模态抓取，成对归还；identity 判据不误放他者）。 */
    if (XWidget_keyboardGrabber() == self) XWidget_releaseKeyboard(self);
#if XWINDOW_ON
    {
        XWindow* handle = XWidget_windowHandle(self);
        if (handle) XWindow_setMouseGrabEnabled(handle, false);
    }
#endif
    if (!kb->m_autoPopup) xkb_stopGuard(kb);
}

/** @brief 定时器分派：重复 tick + 守护 tick；非本类 id 链回基类
 *         （XCompleter.c:627-630 范式，漏链则基类定时器全哑）。 */
static void VXKeyboard_timerEvent(XObject* object, XTimerEvent* event)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)object;
    XTimerId id;
    if (!self || !event) return;
    id = (XTimerId)XTimerEvent_timerId(event);
    if (id == self->m_repeatTimer) {
        xkb_repeatTick(self);
        return;
    }
    if (id == self->m_guardTimer) {
        xkb_guardTick(self);
        return;
    }
    if (id == self->m_t9TapTimer) {
        /* 多击窗口超时闭合（字母已落编辑框；仅复位循环状态）。 */
        xkb_t9TapReset(self);
        return;
    }
    if (id == self->m_geomSyncTimer) {
        XObject_killTimer((XObject*)self, self->m_geomSyncTimer);
        self->m_geomSyncTimer = XTIMER_INVALID_ID;
        if (self->m_popped) {
            xkb_reposition(self);
            /* 几何批处理收尾同步维护 Z 序（拖拽/缩放逐批生效）：宿主
               resize 链路里 chrome 重排（demo 导航面板 raise 先例）在
               本批 reposition 之后到达时会把键盘压回，30ms 批处理节点
               抬回使非合作宿主的 bury 窗口从守护周期 200ms 收窄到
               ~30ms；合作宿主（demo 收尾同步抬回）本调用为幂等空转。 */
            xkb_maintainZOrder(self);
        }
        return;
    }
    XClass_Parent(XObject, EXObject_TimerEvent,
                  void (*)(XObject*, XTimerEvent*))(object, event);
}

/** @brief 反初始化：停全部定时器并解绑 m_target/m_host 连接后调父类。 */
static void VXKeyboard_deinit(XVirtualKeyboard* self)
{
    if (!self) return;
    xkb_stopGuard(self);
    xkb_t9TapReset(self); /* 九键英文多击确认定时器随析构停运。 */
    if (self->m_geomSyncTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_geomSyncTimer);
        self->m_geomSyncTimer = XTIMER_INVALID_ID;
    }
    if (self->m_targetConn) {
        XObject_disconnect_2(self->m_targetConn);
        self->m_targetConn = NULL;
    }
    if (self->m_hostConn) {
        XObject_disconnect_2(self->m_hostConn);
        self->m_hostConn = NULL;
    }
    self->m_target = NULL;
    self->m_host = NULL;
    self->m_hostOverride = NULL; /* 悬浮锚为借用指针，反初始化一并解除。 */
    self->m_hostHint = NULL;
    self->m_prevMouseGrab = NULL; /* 抓取者快照同属运行态借用，一并解除。 */
    self->m_prevKbdGrab = NULL;
    self->m_floating = false;
    self->m_keyCount = 0;
    XClass_Deinit_Parent(XWidget, (XWidget*)self);
}

/** @brief 深拷贝：基类深拷贝后复制槽位指针（借用语义）与布局快照；
 *         绑定/弹层/定时器属对象运行态，不随拷贝迁移（防悬垂）。 */
static void VXKeyboard_copy(XVirtualKeyboard* self, const XVirtualKeyboard* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XVirtualKeyboard_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Copy,
                  void(*)(XWidget*, const XWidget*))((XWidget*)self,
                                                     (const XWidget*)other);
    self->m_mode = other->m_mode;
    self->m_layoutKind = other->m_layoutKind; /* 款型与 shift 展示态随拷贝。 */
    self->m_shiftState = other->m_shiftState;
    self->m_imeChineseSaved = other->m_imeChineseSaved;
    self->m_t9TapDigit = other->m_t9TapDigit; /* 多击循环状态随拷贝快照。 */
    self->m_t9TapIdx = other->m_t9TapIdx;
    self->m_popovers = other->m_popovers;
    self->m_autoPopup = other->m_autoPopup;
    for (i = 0; i <= XKEYBOARD_MODE_SLOT_COUNT; ++i) {
        self->m_maps[i] = other->m_maps[i];
        self->m_ctrls[i] = other->m_ctrls[i];
    }
    self->m_keyCount = other->m_keyCount;
    for (i = 0; i < (int)self->m_keyCount; ++i) {
        self->m_keyCtrls[i] = other->m_keyCtrls[i];
        self->m_keyRects[i] = other->m_keyRects[i];
    }
    self->m_menuBarRect = other->m_menuBarRect; /* 菜单条几何随布局快照。 */
    self->m_hostW = other->m_hostW;
    self->m_hostH = other->m_hostH;
    self->m_popupHeight = other->m_popupHeight;
#if XVIRTUALKEYBOARD_ON
    /* 候选带几何/插件装载镜像/分页面板本地状态随拷贝迁移；落地契约
       连接/守护采样属运行态，不跨对象迁移。 */
    self->m_imeBandRect = other->m_imeBandRect;
    self->m_imeEnabled = other->m_imeEnabled;
    self->m_candidatePage = other->m_candidatePage;
    self->m_candidatePageSize = other->m_candidatePageSize;
#endif
    /* 运行态归零：绑定/连接/弹层/按压/定时器不跨对象迁移。 */
    self->m_selectedKey = XKEYBOARD_BUTTON_NONE;
    self->m_pressedKey = XKEYBOARD_BUTTON_NONE;
    self->m_pressArmed = false;
    self->m_pressedTool = -1;
    self->m_layoutSelectorOpen = false;
    self->m_editPanelOpen = false; /* 面板开合/选择态属运行态，不迁移。 */
    self->m_editSelArmed = false;
    self->m_compactFloat = false; /* 紧凑悬浮态/拖移属运行态，不迁移。 */
    self->m_compactDrag = false;
    self->m_physKeyActive = false; /* 物理会话标记属运行态，不迁移。 */
    self->m_dragOffX = 0;
    self->m_dragOffY = 0;
    self->m_popped = false;
    self->m_target = NULL;
    self->m_host = NULL;
    self->m_hostOverride = NULL; /* 悬浮锚属运行态借用，不跨对象迁移。 */
    self->m_hostHint = NULL;
    {
        int gi;
        for (gi = 0; gi < 8; ++gi) self->m_geomConn[gi] = NULL;
    }
    self->m_geomScope = NULL; /* 几何信号连接属运行态，不跨对象迁移。 */
    self->m_geomSyncTimer = XTIMER_INVALID_ID;
    self->m_prevMouseGrab = NULL; /* 抓取者快照同属运行态借用，不迁移。 */
    self->m_prevKbdGrab = NULL;
    self->m_floating = false;
    self->m_targetConn = NULL;
    self->m_hostConn = NULL;
    /* 守护/重复定时器按拷贝结果重同步（init 兜底路径可能已把守护拉
       起；autoPopup=false 时不得遗留运行中的轮询）。 */
    xkb_stopGuard(self);
    if (self->m_autoPopup) xkb_startGuard(self);
    XWidget_update((XWidget*)self);
}

/** @brief 移动语义：基类移动后整体转移槽位与布局快照；源定时器停运、
 *         源 destroyed 连接断开（连接以源为接收方，不可随指针转移），
 *         目标按新宿主重挂防悬垂连接；源归构造默认值。 */
static void VXKeyboard_move(XVirtualKeyboard* self, XVirtualKeyboard* other)
{
    int i;
    if (!self || !other || self == other) return;
    if (XClassIsVtableNull(self)) XVirtualKeyboard_init(self, NULL, 0);
    XClass_Parent(XWidget, EXClass_Move,
                  void(*)(XWidget*, XWidget*))((XWidget*)self,
                                               (XWidget*)other);
    xkb_stopGuard(other);
    if (other->m_targetConn) {
        XObject_disconnect_2(other->m_targetConn);
        other->m_targetConn = NULL;
    }
    if (other->m_hostConn) {
        XObject_disconnect_2(other->m_hostConn);
        other->m_hostConn = NULL;
    }
    self->m_mode = other->m_mode;
    self->m_layoutKind = other->m_layoutKind; /* 款型与 shift 展示态随移动。 */
    self->m_shiftState = other->m_shiftState;
    self->m_imeChineseSaved = other->m_imeChineseSaved;
    xkb_t9TapReset(self); /* 多击定时器属对象运行态，不随对象转移。 */
    xkb_t9TapReset(other); /* 源多击定时器同步停运（移动语义源停运口径）。 */
    other->m_imeChineseSaved = false;
    self->m_popovers = other->m_popovers;
    self->m_autoPopup = other->m_autoPopup;
    for (i = 0; i <= XKEYBOARD_MODE_SLOT_COUNT; ++i) {
        self->m_maps[i] = other->m_maps[i];
        self->m_ctrls[i] = other->m_ctrls[i];
        other->m_maps[i] = NULL;
        other->m_ctrls[i] = NULL;
    }
    self->m_keyCount = other->m_keyCount;
    other->m_keyCount = 0;
    for (i = 0; i < XKEYBOARD_MAX_BUTTONS; ++i) {
        self->m_keyCtrls[i] = other->m_keyCtrls[i];
        self->m_keyRects[i] = other->m_keyRects[i];
        other->m_keyCtrls[i] = (XKeyboardButtonCtrl)0;
        XRect_init(&other->m_keyRects[i], 0, 0, 0, 0);
    }
    self->m_menuBarRect = other->m_menuBarRect; /* 菜单条几何随布局转移。 */
    XRect_init(&other->m_menuBarRect, 0, 0, 0, 0);
    self->m_target = other->m_target;
    other->m_target = NULL;
    self->m_host = other->m_host;
    other->m_host = NULL;
    self->m_hostOverride = other->m_hostOverride; /* 悬浮态随宿主整体转移。 */
    other->m_hostOverride = NULL;
    self->m_hostHint = other->m_hostHint;
    other->m_hostHint = NULL;
    /* 抓取者快照不随移动转移（框架抓取态不在源对象上，快照即失效）：
       双方清空，悬浮重弹时由 popup 重拍。 */
    self->m_prevMouseGrab = NULL;
    other->m_prevMouseGrab = NULL;
    self->m_prevKbdGrab = NULL;
    other->m_prevKbdGrab = NULL;
    self->m_floating = other->m_floating;
    other->m_floating = false;
    self->m_hostW = other->m_hostW;
    self->m_hostH = other->m_hostH;
    self->m_popupHeight = other->m_popupHeight;
    other->m_hostW = 0;
    other->m_hostH = 0;
    other->m_popupHeight = 0;
#if XVIRTUALKEYBOARD_ON
    /* 候选带几何/插件装载镜像/分页状态随移动转移，源归零（构造默认：
       开关关/无带/首页/页容量 9）。 */
    self->m_imeBandRect = other->m_imeBandRect;
    self->m_imeEnabled = other->m_imeEnabled;
    self->m_candidatePage = other->m_candidatePage;
    self->m_candidatePageSize = other->m_candidatePageSize;
    /* 落地契约/长按重复连接不随移动转移（接收方 this 变更即失效），
       置空由 popup/setTextArea 幂等重连。 */
    self->m_commitConn = NULL;
    self->m_keyEventConn = NULL;
    self->m_preeditConn = NULL;
    other->m_commitConn = NULL;
    other->m_keyEventConn = NULL;
    other->m_preeditConn = NULL;
    XRect_init(&other->m_imeBandRect, 0, 0, 0, 0);
    other->m_imeEnabled = false;
    other->m_candidatePage = 0;
    other->m_candidatePageSize = 9;
#endif
    self->m_selectedKey = other->m_selectedKey;
    other->m_selectedKey = XKEYBOARD_BUTTON_NONE;
    other->m_pressedKey = XKEYBOARD_BUTTON_NONE;
    other->m_pressArmed = false;
    self->m_pressedTool = -1; /* 按压武装属运行态，不随移动转移。 */
    other->m_pressedTool = -1;
    self->m_layoutSelectorOpen = false; /* 面板开合属运行态，不随移动转移。 */
    other->m_layoutSelectorOpen = false;
    self->m_editPanelOpen = false;
    other->m_editPanelOpen = false;
    self->m_editSelArmed = false;
    other->m_editSelArmed = false;
    self->m_compactFloat = false; /* 紧凑悬浮态/拖移属运行态，不随移动转移。 */
    other->m_compactFloat = false;
    self->m_compactDrag = false;
    other->m_compactDrag = false;
    self->m_physKeyActive = false; /* 物理会话标记属运行态，不随移动转移。 */
    other->m_physKeyActive = false;
    self->m_dragOffX = 0;
    other->m_dragOffX = 0;
    self->m_dragOffY = 0;
    other->m_dragOffY = 0;
    other->m_popped = false;
    /* 款型源归构造默认值（PinyinFull/小写态；槽位表已随上方循环置空）。 */
    other->m_layoutKind = XKeyboardLayout_PinyinFull;
    other->m_shiftState = 0;
    /* 守护/重复定时器按移动结果重同步（init 兜底路径可能已把本方守
       护拉起，源侧 autoPopup 关时不得遗留运行中的轮询）。 */
    xkb_stopGuard(self);
    if (self->m_autoPopup) xkb_startGuard(self);
    /* 目标按新对象重挂防悬垂连接。 */
    if (self->m_target)
        self->m_targetConn = XObject_connect_1(
            (XObject*)self->m_target, XSignal(XObject_destroyed_signal),
            (XObject*)self, xkb_targetDestroyedSlot,
            XConnectionType_Direct);
    if (self->m_host)
        self->m_hostConn = XObject_connect_1(
            (XObject*)self->m_host, XSignal(XObject_destroyed_signal),
            (XObject*)self, xkb_hostDestroyedSlot,
            XConnectionType_Direct);
    XWidget_update((XWidget*)self);
}

/* ==================== 生命周期 ==================== */

XVtable* XVirtualKeyboard_class_init(void)
{
    XVTABLE_INIT_DEFAULT(XVirtualKeyboard)
    XVTABLE_INHERIT_XCLASS(XWidget);

    XVTABLE_OVERLOAD_DEFAULT(EXWidget_PaintEvent, VXKeyboard_paintEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_ResizeEvent, VXKeyboard_resizeEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MousePressEvent,
                             VXKeyboard_mousePressEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseReleaseEvent,
                             VXKeyboard_mouseReleaseEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_MouseMoveEvent,
                             VXKeyboard_mouseMoveEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_TouchDragEvent,
                             VXKeyboard_touchDragEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXWidget_HideEvent, VXKeyboard_hideEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXObject_TimerEvent, VXKeyboard_timerEvent);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Copy, VXKeyboard_copy);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Move, VXKeyboard_move);
    XVTABLE_OVERLOAD_DEFAULT(EXClass_Deinit, VXKeyboard_deinit);

    return XVTABLE_DEFAULT;
}

void XVirtualKeyboard_init(XVirtualKeyboard* self, XWidget* parent, XWidgetFlags flags)
{
    int i;
    if (!self) return;
    /* 结构体先整体清零再初始化基类（XWidget_init 只清基类部分；槽位
       数组不清零会带堆残留，见 XLineEdit.c 内置 action 槽注释）。 */
    XMemset(self, 0, sizeof(*self));
    XWidget_init((XWidget*)self, parent, flags);
    XClassSetVtable(self, XVirtualKeyboard);

    self->m_mode = XKeyboardMode_TextLower;
    self->m_layoutKind = XKeyboardLayout_PinyinFull; /* 默认款型：搜狗全键。 */
    self->m_shiftState = 0; /* 布局层 shift 展示态：小写。 */
    self->m_popovers = false;
    self->m_autoPopup = true;
    self->m_popped = false;
    self->m_userCollapsed = false;
    self->m_selectedKey = XKEYBOARD_BUTTON_NONE;
    self->m_pressedKey = XKEYBOARD_BUTTON_NONE;
    self->m_pressArmed = false;
    self->m_pressedTool = -1; /* 工具栏按压武装：无。 */
    self->m_layoutSelectorOpen = false; /* 键盘选择面板：关。 */
    self->m_editPanelOpen = false; /* 文字编辑面板：关。 */
    self->m_editSelArmed = false;
    self->m_compactFloat = false; /* 紧凑悬浮态：停靠。 */
    self->m_compactDrag = false;
    self->m_physKeyActive = false; /* 物理会话标记：无（memset 清零后显式口径）。 */
    self->m_dragOffX = 0;
    self->m_dragOffY = 0;
    for (i = 0; i < XKEYBOARD_MODE_SLOT_COUNT; ++i) {
        self->m_maps[i] = s_kbMapDefaults[i];
        self->m_ctrls[i] = s_kbCtrlDefaults[i];
    }
    /* 款型主表装载（默认 PinyinFull 覆写文本双槽位；其余槽位回落既有
       内置表——既有模式机制/hints 自动切换现状不变）。 */
    xkb_installKindTables(self);
    self->m_maps[XKEYBOARD_MODE_SLOT_COUNT] = NULL;
    self->m_ctrls[XKEYBOARD_MODE_SLOT_COUNT] = NULL;
    self->m_repeatTimer = XTIMER_INVALID_ID;
    self->m_guardTimer = XTIMER_INVALID_ID;
    self->m_geomSyncTimer = XTIMER_INVALID_ID;
    self->m_t9TapTimer = XTIMER_INVALID_ID;
#if XVIRTUALKEYBOARD_ON
    /* 候选带无带、插件装载镜像关、分页面板本地态首页/容量 9、落地
       契约连接未建、守护边沿采样空（状态机在插件，面板零直连）。 */
    self->m_prevFocus = NULL; /* 守护边沿采样（无条件成员）。 */
    XRect_init(&self->m_imeBandRect, 0, 0, 0, 0);
    self->m_imeEnabled = false;
    self->m_commitConn = NULL;
    self->m_keyEventConn = NULL;
    self->m_preeditConn = NULL;
    self->m_prevFocus = NULL;
    self->m_candidatePage = 0;
    self->m_candidatePageSize = 9;
#endif
    /* 弹层面板构造契约（对标 Qt InputPanel 构造即隐藏）：非窗口子控
       件形态补置 WState_Hidden——XWidget_init 只对顶层置 Hidden（子
       控件不带、随父 show 自动显示），页面 show 的传播会把未弹出的
       面板按默认子控件几何 (0,0,100,30) 整块画进页面（paintEvent 仅
       查宽高）并吞掉矩形内点击（childAt 逆序命中）——键盘页首行白
       卡残影根因。置位后 paintTree（!m_visible）与 childAt
       （WState_Hidden）双门拦截：未弹不绘制、不命中；popup()/显式
       show 唤起，弹出后键面与候选带绘制/命中照旧。顶层形态构造已
       Hidden、窗口形态子控件不随父传播（paintTree/childAt 均跳过
       isWindow），均无此患、不在此列。 */
    if (parent && !((XWidget*)self)->m_isWindow)
        XWidget_setVisible((XWidget*)self, false);
    xkb_rebuildLayout(self);
    /* autoPopup 默认开：构造即常驻守护轮询，兑现头文件『on 时守护定时
       器常驻轮询』承诺（首次 popup() 之前焦点落编辑框即可自动弹出）。
       无事件调度器环境（无头构造）下 startTimer 返回 INVALID_ID 不崩
       溃，后续 popup()/setAutoPopup 路径会重试拉起。 */
    xkb_startGuard(self);
}

XVirtualKeyboard* XVirtualKeyboard_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)XMemory_malloc(sizeof(XVirtualKeyboard), memory);
    if (!self) return NULL;
    XVirtualKeyboard_init(self, parent, flags);
    Set_Class_Memory(self, memory);
    Set_Class_IsHeap(self, true);
    return self;
}

/* ==================== 绑定与布局属性 ==================== */

/** @brief 重挂目标 destroyed 防悬垂连接（先断旧后连新；NULL 解绑）。 */
static void xkb_bindTargetDestroyed(XVirtualKeyboard* self, XWidget* newTarget)
{
    if (self->m_targetConn) {
        XObject_disconnect_2(self->m_targetConn);
        self->m_targetConn = NULL;
    }
    if (newTarget)
        self->m_targetConn = XObject_connect_1(
            (XObject*)newTarget, XSignal(XObject_destroyed_signal),
            (XObject*)self, xkb_targetDestroyedSlot,
            XConnectionType_Direct);
}

/** @brief 重挂宿主 destroyed 防悬垂连接（先断旧后连新；NULL 解绑）。 */
static void xkb_bindHostDestroyed(XVirtualKeyboard* self, XWidget* newHost)
{
    if (self->m_hostConn) {
        XObject_disconnect_2(self->m_hostConn);
        self->m_hostConn = NULL;
    }
    if (newHost)
        self->m_hostConn = XObject_connect_1(
            (XObject*)newHost, XSignal(XObject_destroyed_signal),
            (XObject*)self, xkb_hostDestroyedSlot,
            XConnectionType_Direct);
}

/** @brief 几何信号槽（宿主/范围顶层 x/y/width/heightChanged 任一变化）：
 *         弹层存活期即时重定位（拖拽/缩放逐帧跟随，2026-10-03 所有者
 *         裁定事件推送替代轮询）。 */
/* 几何跟随连接类型（平台分野）：win32 原生边框拖拽/缩放运行在
   DefWindowProc 模态消息循环里，期间框架事件循环不运行——Queued 投
   递的信号与全部定时器（30ms 合流/200ms 守护）均被饿死，键盘几何只
   能拖拽结束后一次性跟到位（真机实测）。Direct 直连使 reposition 在
   WM_SIZE→handleGeometryChange→setGeometryFields 的每帧同步链路内
   执行，拖拽全程逐帧跟随；同帧 x/y/w/h 四连发由 reposition 的几何
   短路吸收。非 win32 维持 Queued+30ms 合流（kwin 直连实测「键盘轻
   量重绘逐帧窜到新位、主窗整页重绘晚 1~2 帧」的跑出父窗口问题，
   2026-10-03 拖动感官裁定）。 */
#ifdef _WIN32
#  define XKB_GEOM_CONN_TYPE XConnectionType_Direct
#else
#  define XKB_GEOM_CONN_TYPE XConnectionType_Queued
#endif

static void xkb_hostGeomSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)receiver;
    (void)args;
    if (!self || !self->m_popped) return;
#ifdef _WIN32
    /* 直连逐帧跟随（win32 模态拖拽循环内唯一存活的通道）；同帧 x/y/
       w/h 四连发由 reposition 的几何短路吸收（第二次起目标几何一致
       零操作）。抬层维护一并逐帧执行（见 xkb_maintainZOrder）。 */
    xkb_reposition(self);
    xkb_maintainZOrder(self);
#else
    /* 聚合节流 30ms（2026-10-03 拖动感官裁定）：几何信号 Queued 投递
     * 的槽只置 pending 挂 30ms 单发定时器，同一拖拽批次聚合为一次
     * reposition。直呼形态实测键盘（轻量重绘）逐帧窜到新位、主窗
     * （整页重绘）晚 1~2 帧到位——视觉「键盘跑出父窗口」（真屏 kwin
     * 拖拽实测）；30ms≈主窗重绘节奏，键盘略滞后于主窗显示，两者同
     * 步感成立；拖动停止 ≤30ms 必达，非周期轮询。 */
    if (self->m_geomSyncTimer == XTIMER_INVALID_ID)
        self->m_geomSyncTimer = XObject_startTimer_ms(
            (XObject*)self, 30, XTimerType_CoarseTimer);
#endif
}

/** @brief 接/断弹层期的宿主与范围顶层几何信号（2026-10-03 所有者裁定
 *         「几何跟随走事件推送不走轮询」；popup 成功路径 bind=true、
 *         closePopup/宿主亡 bind=false）。
 *  @details 连接集：宿主顶层（m_host 的 windowHandle）x/y/width/
 *           heightChanged 四条（[0..3]）；对话框外置浮层形态再加范围
 *           顶层（xkb_overlayScope，主窗）同四条（[4..7]）——浮层落位
 *           基准是范围顶层而非对话框，主窗拖动/缩放必须跟随。WM 拖拽
 *           缩放经 ConfigureNotify→XWindowSystemInterface_
 *           handleGeometryChange→XWindow_setGeometryFields 逐字段发射
 *           （程序侧 setGeometry 同链），覆盖两向。连接类型按平台分野
 *           （XKB_GEOM_CONN_TYPE：win32=Direct 模态拖拽循环内逐帧跟
 *           随；其余=Queued+30ms 合流），槽内几何短路兜成对信号。宿主
 *           亡（destroyedSlot）时宿主侧
 *           句柄随发送方连接表失效，按 m_hostConn 同款直接置 NULL 不
 *           再 disconnect；范围顶层存活期长于对话框，closePopup 统一
 *           真断。 */
static void xkb_bindGeometrySignals(XVirtualKeyboard* self, bool bind)
{
    XWindow* hostWin;
    int i;
    for (i = 0; i < 8; ++i) {
        if (self->m_geomConn[i]) {
            XObject_disconnect_2(self->m_geomConn[i]);
            self->m_geomConn[i] = NULL;
        }
    }
    self->m_geomScope = NULL;
    if (!bind || !self->m_host || !self->m_popped) return;
    hostWin = (XWindow*)XWidget_windowHandle((XWidget*)self->m_host);
    if (!hostWin) return;
    self->m_geomConn[0] = XObject_connect_1(
        (XObject*)hostWin, XSignal(XWindow_xChanged_signal),
        (XObject*)self, xkb_hostGeomSlot, XKB_GEOM_CONN_TYPE);
    self->m_geomConn[1] = XObject_connect_1(
        (XObject*)hostWin, XSignal(XWindow_yChanged_signal),
        (XObject*)self, xkb_hostGeomSlot, XKB_GEOM_CONN_TYPE);
    self->m_geomConn[2] = XObject_connect_1(
        (XObject*)hostWin, XSignal(XWindow_widthChanged_signal),
        (XObject*)self, xkb_hostGeomSlot, XKB_GEOM_CONN_TYPE);
    self->m_geomConn[3] = XObject_connect_1(
        (XObject*)hostWin, XSignal(XWindow_heightChanged_signal),
        (XObject*)self, xkb_hostGeomSlot, XKB_GEOM_CONN_TYPE);
    if (XWidget_isWindow((XWidget*)self) &&
        XWidget_parentWidget((XWidget*)self)) {
        XWidget* scope = xkb_overlayScope(self);
        XWindow* scopeWin =
            (scope && scope != self->m_host)
                ? (XWindow*)XWidget_windowHandle(scope)
                : NULL;
        if (scopeWin) {
            self->m_geomConn[4] = XObject_connect_1(
                (XObject*)scopeWin, XSignal(XWindow_xChanged_signal),
                (XObject*)self, xkb_hostGeomSlot, XKB_GEOM_CONN_TYPE);
            self->m_geomConn[5] = XObject_connect_1(
                (XObject*)scopeWin, XSignal(XWindow_yChanged_signal),
                (XObject*)self, xkb_hostGeomSlot, XKB_GEOM_CONN_TYPE);
            self->m_geomConn[6] = XObject_connect_1(
                (XObject*)scopeWin, XSignal(XWindow_widthChanged_signal),
                (XObject*)self, xkb_hostGeomSlot, XKB_GEOM_CONN_TYPE);
            self->m_geomConn[7] = XObject_connect_1(
                (XObject*)scopeWin, XSignal(XWindow_heightChanged_signal),
                (XObject*)self, xkb_hostGeomSlot, XKB_GEOM_CONN_TYPE);
            self->m_geomScope = scope;
        }
    }
}

#if XVIRTUALKEYBOARD_ON

/** @brief 【落地契约槽】context.commitRequested(text) → 既有写入链。
 *         无目标编辑框/总开关关时丢弃（Qt 无平台集成层同样无处落地）。 */
static void xkb_commitSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)receiver;
    if (!self || !args) return;
    XVarList_args_1(args, const char*, commitText);
    if (!XVirtualKeyboardSettings_keyboardEnabled(
            XVirtualKeyboardSettings_instance()))
        return; /* 总开关关。 */
    if (commitText && commitText[0] && self->m_target)
        xkb_writeText(self, commitText);
    xkb_imeSyncPageSize(self);
    XWidget_update((XWidget*)self);
}

/** @brief 【落地契约槽】context.keyEventRequested(key,text,mods) →
 *         合成键/直写（sendKeyClick 通道）。 */
static void xkb_keyEventSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)receiver;
    if (!self || !args) return;
    XVarList_args_3(args, int, reqKey, const char*, reqText, uint32_t,
                    reqMods);
    if (!XVirtualKeyboardSettings_keyboardEnabled(
            XVirtualKeyboardSettings_instance()))
        return;
    if (!self->m_target) return;
    if (reqText && reqText[0] && !reqText[1] && reqText[0] >= 0x20 &&
        reqText[0] <= 0x7E)
        xkb_writeText(self, reqText); /* 单字节可打印走直写（大写带 Shift）。 */
    else
        xkb_sendKey(self, reqKey, (XKeyboardModifiers)reqMods);
}

/** @brief 【组串收缩槽】context.preeditTextChanged → 组串态停靠几何
 *         翻转（用户需求③）：组串开始收缩为顶部候选带一条、组串结束
 *         （提交/删空/切模式换款弃草稿）恢复全量。组串一切变化必经
 *         context.setPreeditText_2（XVirtualKeyboardPinyinInputMethod
 *         唯一组串写入点）先写值后发射——本槽为全覆盖中央事件源；
 *         reposition 几何短路使同态重复发射零开销，未弹/非停靠形态
 *         判据早退零副作用。 */
static void xkb_preeditSlot(XObject* receiver, XVarList* args)
{
    XVirtualKeyboard* self = (XVirtualKeyboard*)receiver;
    (void)args; /* 发射侧无载荷（xvkc_emit args=NULL），判据自取现值。 */
    if (!self) return;
    /* 组串结束（提交/删空/换绑弃草稿）即清物理会话标记与九键多击
       态：标记不随组串清位会遗留给后续屏幕键组串（composeCollapsed
       Wanted 只认标记∧组串），屏幕键打字被误收缩（2026-10-04 用户
       实测回归）；多击位次跨词残留会让下词首字母从循环中段落笔。 */
    if (!xkb_imeComposing(self)) {
        self->m_physKeyActive = false;
        xkb_t9TapReset(self);
    }
    if (!self->m_popped) return;
    xkb_reposition(self);
    XWidget_update((XWidget*)self);
}

/** @brief 连接/断开 context 落地契约信号（setTextArea/popup 连接，
 *         closePopup 断开；幂等）。长按重复不经 engine 信号回环：多面
 *         板共享进程单例 engine，任一面板的延迟析构都会摘清单例上的
 *         全部点击连接（探针实证 recv 恒 0），重复驱动回落面板本地定
 *         时器（节拍宏仍取 engine 头的 600/50，单一事实源不丢）。 */
static void xkb_bindContextConns(XVirtualKeyboard* self, bool bind)
{
    XVirtualKeyboardInputContext* ctx;
    if (!self) return;
    if (!bind) {
        if (self->m_commitConn) {
            XObject_disconnect_2(self->m_commitConn);
            self->m_commitConn = NULL;
        }
        if (self->m_keyEventConn) {
            XObject_disconnect_2(self->m_keyEventConn);
            self->m_keyEventConn = NULL;
        }
        if (self->m_preeditConn) {
            XObject_disconnect_2(self->m_preeditConn);
            self->m_preeditConn = NULL;
        }
        return;
    }
    if (self->m_commitConn && self->m_keyEventConn && self->m_preeditConn)
        return; /* 幂等。 */
    ctx = xkb_contextOf(self);
    if (!ctx) return;
    /* 连接判据只看连接句柄：XObject_connect_1 自建的是发送方（ctx）
       的信号槽（XObject.c:420-424），接收方（本面板）作为 receiver 不
       需要——若再以 m_signalSlot 判接收方，新建面板（从未作为发送方
       连接过信号）恒为 NULL，连接被静默跳过（commit/keyEvent 落地链
       全断，探针实证 commitConn 恒 0）。 */
    if (!self->m_commitConn)
        self->m_commitConn = XObject_connect_1(
            (XObject*)ctx,
            (size_t)XVirtualKeyboardInputContext_commitRequested_signal(
                NULL, NULL),
            (XObject*)self, xkb_commitSlot, XConnectionType_Direct);
    if (!self->m_keyEventConn)
        self->m_keyEventConn = XObject_connect_1(
            (XObject*)ctx,
            (size_t)XVirtualKeyboardInputContext_keyEventRequested_signal(
                NULL, 0, NULL, 0),
            (XObject*)self, xkb_keyEventSlot, XConnectionType_Direct);
    if (!self->m_preeditConn)
        self->m_preeditConn = XObject_connect_1(
            (XObject*)ctx,
            (size_t)XVirtualKeyboardInputContext_preeditTextChanged_signal(
                NULL),
            (XObject*)self, xkb_preeditSlot, XConnectionType_Direct);
}

/** @brief hints→布局映射（Keyboard.qml:42-49 优先级链；手写 N-A）。
 *  @details dialpad(Dialable) > numbers(FormattedNumbers) > digits
 *           (DigitsOnly) > numbers(PreferNumbers 软提示) > main；latin
 *           系只锁 Latin 输入模式（Keyboard.qml:1516-1519 口径），数
 *           字系切 Numeric/Dialable（仅当输入模式在当前插件申报集内
 *           才切，避免 Qt setInputMode 拒绝噪声）；无互斥 hints
 *           （ImhNone）对称回落 main 布局，但 IME 会话形态（User1 拼
 *           音槽位，槽位占用约定；或中文会话进行中的款型主表形态）完
 *           全保持——重绑普通编辑框不打断拼音会话
 *           （XKeyboardTest ④/⑤ 门禁口径）；回落时 IME 状态按页面
 *           build 口径恢复英文态：Latin 经下方申报集守卫设置（拼音
 *           插件申报 {Pinyin,Latin}，Plain 恒 {Latin}，未申报静默跳
 *           过），m_imeEnabled 不随重绑翻转——拼音能力保留（回 User1
 *           后中/EN 仍可进中文），上一目标残留的 Numeric/Pinyin 引擎
 *           态不外溢（main→digits→main 数字盘残留实测缺陷的对向修
 *           复）。 */
static void xkb_applyFocusHints(XVirtualKeyboard* self)
{
    XVirtualKeyboardInputContext* ctx;
    XVirtualKeyboardInputEngine* engine;
    uint32_t hints;
    XKeyboardMode mode = self->m_mode;
    int inputMode = -1;
    int modes[8];
    int modeCount;
    int i;
    if (!self || !self->m_target) return;
    ctx = xkb_contextOf(self);
    if (!ctx) return;
    hints = XVirtualKeyboardInputContext_inputMethodHints(ctx);
    if (hints & XInputMethodHint_DialableCharactersOnly) {
        mode = XKeyboardMode_Dialpad;
        inputMode = (int)XVirtualKeyboardInputEngineInputMode_Dialable;
    } else if (hints & XInputMethodHint_FormattedNumbersOnly) {
        mode = XKeyboardMode_Number;
        inputMode = (int)XVirtualKeyboardInputEngineInputMode_Numeric;
    } else if (hints & XInputMethodHint_DigitsOnly) {
        mode = XKeyboardMode_Digits;
        inputMode = (int)XVirtualKeyboardInputEngineInputMode_Numeric;
    } else if (hints & XInputMethodHint_PreferNumbers) {
        mode = XKeyboardMode_Number;
        inputMode = (int)XVirtualKeyboardInputEngineInputMode_Numeric;
    } else if (hints & (XInputMethodHint_LatinOnly |
                        XInputMethodHint_EmailCharactersOnly |
                        XInputMethodHint_UrlCharactersOnly)) {
        mode = XKeyboardMode_TextLower;
        inputMode = (int)XVirtualKeyboardInputEngineInputMode_Latin;
    } else {
        /* 无互斥 hints（ImhNone）：
           - IME 会话形态（当前在 User1 拼音槽位＝IME 面板的主布局，槽
             位占用约定；或中文会话进行中——款型选择面板落 TextLower 后
             的 T9/全键拼音会话，Sogou 改版三阶段）→ 完全保持：不重建
             布局、不动引擎输入模式——重绑普通编辑框不得打断拼音会话
             （XKeyboardTest ④/⑤ 门禁口径：setImeEnabled(true)→
             setTextArea(无 hints 框) 后 n/i 仍进组串，中文/英文态随引
             擎不翻转；demo build 的 User1+英文态初始演示态同理不被重
             绑打落）。
           - 其余布局（数字盘等专用布局残留）→ 对称回落 main（TextLower），
             引擎输入模式按页面 build 口径恢复英文态（Latin 经下方申报
             集守卫设置，未申报静默跳过），m_imeEnabled 不动——实测缺陷
             main→digits→main 数字盘残留即本方向缺失。 */
        if (self->m_mode == XKeyboardMode_User1 ||
            (self->m_imeEnabled && xkb_imeChineseState(self))) {
            mode = self->m_mode;
            inputMode = -1;
        } else {
            mode = XKeyboardMode_TextLower;
            inputMode = (int)XVirtualKeyboardInputEngineInputMode_Latin;
        }
    }
    if (mode != self->m_mode) XVirtualKeyboard_setMode(self, mode);
    engine = xkb_engineOf(self);
    if (inputMode >= 0 && engine) {
        modeCount = XVirtualKeyboardInputEngine_inputModes(engine, modes, 8);
        for (i = 0; i < modeCount; ++i) {
            if (modes[i] == inputMode) {
                XVirtualKeyboardInputEngine_setInputMode(
                    engine, (XVirtualKeyboardInputEngineInputMode)inputMode);
                break;
            }
        }
    }
    xkb_notifyObserver(self);
}

/** @brief shift/caps 读数→模式读数同步（不重建键帽布局）。
 *  @details isUppercase（shift||capsLock）→TextUpper，否则 TextLower；
 *           仅切 m_mode 读数（观察者/重绘照发）——重建会换装大写键帽
 *           表，字符键下标随测试侧 findButton("q") 契约失配（视觉映射
 *           不变口径）；大写出字经 xkb_writeText 的上下文大写钩子。 */
static void xkb_syncShiftMode(XVirtualKeyboard* self)
{
    XVirtualKeyboardInputContext* ctx;
    XKeyboardMode mode;
    if (!self) return;
    ctx = xkb_contextOf(self);
    if (!ctx) return;
    mode = XVirtualKeyboardInputContext_isUppercase(ctx)
               ? XKeyboardMode_TextUpper
               : XKeyboardMode_TextLower;
    if (self->m_mode == mode) return;
    self->m_mode = mode;
    xkb_notifyObserver(self);
    XWidget_update((XWidget*)self);
}


#endif /* XVIRTUALKEYBOARD_ON */

void XVirtualKeyboard_setTextArea(XVirtualKeyboard* self, XWidget* target)
{
    if (!self) return;
    if (target == self->m_target) return;
    if (target && !xkb_supportedTarget(target)) {
        XPrintf("[XVirtualKeyboard] setTextArea: 不支持的目标类型（三编辑控件/"
                "WA_InputMethodEnabled 控件之外），保持原绑定\n");
        return;
    }
    xkb_bindTargetDestroyed(self, target);
#if XVIRTUALKEYBOARD_ON
    /* 实际换绑（含守护 tick 焦点跟随重绑）即弃组串草稿（engine
       reset）——覆盖「换框残留拼音」风险；同目标重绑不误清。同步
       连接落地契约信号（有目标才连接；无面板连接时提交/虚键丢弃）。 */
    if (xkb_engineOf(self))
        XVirtualKeyboardInputEngine_reset(xkb_engineOf(self));
    xkb_bindContextConns(self, target != NULL);
#endif
    self->m_target = target;
#if XVIRTUALKEYBOARD_ON
    /* 绑定目标即上下文输入项（Qt inputContext.focusObject 口径）：后
       续 hints/包围文本查询以绑定目标为事实源——直呼 popup 的目标未
       持应用焦点时，焦点对象链拿不到它的 hints（closeOnReturn 的
       MultiLine 豁免失灵）。 */
    {
        XVirtualKeyboardInputContext* ctx = xkb_contextOf(self);
        if (ctx)
            XVirtualKeyboardInputContext_setFocusObject(
                ctx, (XObject*)target);
    }
    xkb_applyFocusHints(self); /* 绑定后按生效 hints 映射布局（含直呼
                                  setTextArea 路径；popup 内会再同步）。 */
#endif
}

XWidget* XVirtualKeyboard_textArea(const XVirtualKeyboard* self)
{
    return self ? self->m_target : NULL;
}

void XVirtualKeyboard_setMode(XVirtualKeyboard* self, XKeyboardMode mode)
{
    if (!self || mode == self->m_mode) return;
    if (mode < XKeyboardMode_TextLower ||
        mode > XKeyboardMode_Dialpad)
        return;
    self->m_mode = mode;
    xkb_t9TapReset(self); /* 模式切换闭合九键英文多击循环。 */
    /* 文本双槽位显式切换即复位布局款型 shift 展示态（款型大小写键帽
       由款型 Shift 键路径管理；外部 setMode 以目标键帽表为准回小写
       态，防展示态与键帽表脱节）。 */
    if (mode == XKeyboardMode_TextLower || mode == XKeyboardMode_TextUpper)
        self->m_shiftState = 0;
#if XVIRTUALKEYBOARD_ON
    /* 模式实际切换即弃组串草稿（engine reset；离开/重入 User1 均复
       位、不翻转中文态）并通知 Observer。 */
    if (xkb_engineOf(self))
        XVirtualKeyboardInputEngine_reset(xkb_engineOf(self));
    self->m_candidatePage = 0; /* 切布局重置面板本地页。 */
    xkb_notifyObserver(self);
#endif
    xkb_rebuildLayout(self);
    XWidget_update((XWidget*)self);
}

XKeyboardMode XVirtualKeyboard_mode(const XVirtualKeyboard* self)
{
    return self ? self->m_mode : XKeyboardMode_TextLower;
}

void XVirtualKeyboard_setLayoutKind(XVirtualKeyboard* self,
                                    XKeyboardLayoutKind kind)
{
    XKeyboardLayoutKind oldKind;
    if (!self || kind == self->m_layoutKind) return;
    if (kind < XKeyboardLayout_PinyinFull || kind > XKeyboardLayout_EnglishFull)
        return;
    oldKind = self->m_layoutKind;
    self->m_layoutKind = kind;
    xkb_t9TapReset(self); /* 换款闭合九键英文多击循环。 */
    self->m_shiftState = 0; /* 换款复位布局层 shift 展示态。 */
    if (self->m_mode == XKeyboardMode_TextUpper)
        self->m_mode = XKeyboardMode_TextLower; /* 大写读数随展示态复位归位。 */
    xkb_installKindTables(self); /* 款型主表覆写文本双槽位（其余槽位不动）。 */
#if XVIRTUALKEYBOARD_ON
    /* 换款弃组串草稿（engine reset，与 setMode 换表同口径）并通知
       Observer；面板本地分页一并归首页。 */
    if (xkb_engineOf(self))
        XVirtualKeyboardInputEngine_reset(xkb_engineOf(self));
    self->m_candidatePage = 0;
    /* 中英态跨英文款型保存/恢复（搜狗口径，2026-10-04 实测缺陷根修）：
       仅在进出英文全键的边界上生效——进英文全键快照当前中英态（引擎
       不翻转，英文款型的英文路由是判据分支）；从英文全键切出时恢复快
       照。否则英文款型上点 中/EN（收敛回英文）后切回中文款型仍残
       English 态——拼音键盘上打字母直落字面量、无组串无候选（用户视
       角「无法输入中文」）。中文款型之间互切不经此分支，中英态原样保
       持（restore 分支不得按「非英文款型」宽判——那会把互切也按陈旧
       快照翻掉，首轮回归 20 断言连锁失败的根因）。 */
    if (kind == XKeyboardLayout_EnglishFull) {
        if (oldKind != XKeyboardLayout_EnglishFull)
            self->m_imeChineseSaved = xkb_imeChineseState(self);
    } else if (oldKind == XKeyboardLayout_EnglishFull &&
               xkb_imeChineseState(self) != self->m_imeChineseSaved) {
        XVirtualKeyboard_setImeChinese(self, self->m_imeChineseSaved);
    }
    xkb_notifyObserver(self);
#endif
    xkb_rebuildLayout(self);
    XWidget_update((XWidget*)self);
}

XKeyboardLayoutKind XVirtualKeyboard_layoutKind(const XVirtualKeyboard* self)
{
    return self ? self->m_layoutKind : XKeyboardLayout_PinyinFull;
}

void XVirtualKeyboard_setPopovers(XVirtualKeyboard* self, bool enabled)
{
    if (!self || enabled == self->m_popovers) return;
    self->m_popovers = enabled;
    /* 重建生效 ctrl 表（popovers=0 剥 POPOVER 位；源=槽位表，对标
       KB.c:260-270,468-487）。 */
    xkb_rebuildLayout(self);
    XWidget_update((XWidget*)self);
}

bool XVirtualKeyboard_popovers(const XVirtualKeyboard* self)
{
    return self ? self->m_popovers : false;
}

void XVirtualKeyboard_setMap(XVirtualKeyboard* self, XKeyboardMode mode,
                      const char* const* map,
                      const XKeyboardButtonCtrl* ctrlMap)
{
    if (!self || !map) return;
    if (mode < XKeyboardMode_TextLower || mode > XKeyboardMode_User4) return;
    self->m_maps[mode] = map;
    self->m_ctrls[mode] = ctrlMap;
    if (mode == self->m_mode) {
        xkb_rebuildLayout(self);
        XWidget_update((XWidget*)self);
    }
}

const char* const* XVirtualKeyboard_mapArray(const XVirtualKeyboard* self)
{
    if (!self) return NULL;
    return self->m_maps[self->m_mode];
}

uint32_t XVirtualKeyboard_buttonCount(const XVirtualKeyboard* self)
{
    return self ? self->m_keyCount : 0;
}

uint32_t XVirtualKeyboard_selectedButton(const XVirtualKeyboard* self)
{
    return self ? self->m_selectedKey : XKEYBOARD_BUTTON_NONE;
}

const char* XVirtualKeyboard_buttonText(const XVirtualKeyboard* self, uint32_t buttonId)
{
    if (!self || buttonId >= self->m_keyCount) return NULL;
    /* 读重建落表的生效键帽（借用指针，与 map 同生命周期）：不读
       m_maps[m_mode]——shift/caps 态逻辑切模式读数不重建布局（视觉
       映射不变口径），经 mode 槽位直读会拿到新槽表键帽，键下标与
       findButton/handleButton 的查找口径错位。 */
    return self->m_keyLabels[buttonId];
}

XKeyboardButtonCtrl XVirtualKeyboard_buttonCtrl(const XVirtualKeyboard* self,
                                         uint32_t buttonId)
{
    if (!self || buttonId >= self->m_keyCount) return (XKeyboardButtonCtrl)0;
    return self->m_keyCtrls[buttonId];
}

void XVirtualKeyboard_setButtonCtrl(XVirtualKeyboard* self, uint32_t buttonId,
                             XKeyboardButtonCtrl ctrl)
{
    int width;
    if (!self || buttonId >= self->m_keyCount) return;
    /* 生效表覆写：宽度位与标志位一并生效（宽度 0 按单位 1 兜底）。 */
    width = (int)ctrl & (int)XKEYBOARD_CTRL_WIDTH_MASK;
    if (width <= 0) width = 1;
    self->m_keyCtrls[buttonId] = (XKeyboardButtonCtrl)(
        ((int)ctrl & ~(int)XKEYBOARD_CTRL_WIDTH_MASK) | width);
    xkb_rebuildLayout(self);
    XWidget_update((XWidget*)self);
}

/* ==================== 按键语义复用 ==================== */

bool XVirtualKeyboard_handleButton(XVirtualKeyboard* self, uint32_t buttonId)
{
    return xkb_activateButton(self, buttonId);
}

/* ==================== 弹层（XGui 扩展） ==================== */

void XVirtualKeyboard_popup(XVirtualKeyboard* self, XWidget* editor)
{
    XWidget* host;
    bool dialogHost;
    if (!self || !editor) return;
    self->m_userCollapsed = false; /* 显式/自动弹出即清用户收起闩锁。 */
    /* 紧凑悬浮态复位（第三形态）：popup 重评估停靠形态——紧凑矩形不
       跨弹出会话（切编辑框重弹即回宿主底部停靠），拖移态一并复位。 */
    self->m_compactFloat = false;
    self->m_compactDrag = false;
    self->m_physKeyActive = false; /* 物理会话标记不跨弹出会话。 */
    XVirtualKeyboard_setTextArea(self, editor); /* 含类型校验与防悬垂连接。 */
    if (self->m_target != editor) return; /* setTextArea 拒绝（类型不支持）。 */
#if XVIRTUALKEYBOARD_ON
    /* 首帧布局定版前置（缺陷⑤根修）：悬浮分支下方 flushBackingStore 是
       唯一同步首帧——契约连接/hints→模式映射若滞留在 show+flush 之后，
       首帧按残留模式（init TextLower/上一会话映射）上屏，随后的显式
       setMode 只能救第二帧（QWERTY 闪帧）。前置后 flush 即按定版
       m_mode 出帧；同目标重弹（setTextArea 同值早退不重跑）由本块补跑
       幂等对（bindContextConns 幂等、applyFocusHints 同值 setMode 早
       退），换绑路径 setTextArea 内已在同位序跑同一对函数，不引入新时
       序面。m_prevFocus 采样前移等价：show/setParent/setWindowFlags 均
       不改应用焦点（键盘 NoFocus 不夺焦）；键位矩形随后由 reposition
       的 setGeometry→resizeEvent→rebuildLayout 按最终几何重算，前置无
       几何失配。 */
    xkb_bindContextConns(self, true);
    xkb_applyFocusHints(self);
    self->m_prevFocus = XWidget_appFocusWidget();
#endif
    host = XWidget_topLevelWidget(editor);
    if (!host) return;
    /* 宿主形态三分支（2026-10-03 合并裁定方案 A，优先级自上而下）：
       ①悬浮锚（setHostWindow 生效）——显式锚定意图优先；
       ②对话框浮层（宿主为 Dialog 型顶层）——2026-10-02 所有者裁定
         「对话框内不塞内嵌屏幕键盘」：对话框宿主不再以子控件浮层钳进
         对话框（旧路径键盘铺宿主下半区，输入对话框被键盘大面积覆盖
         ——所有者实拍），改按 Popup 独立顶层浮层承载（transient 挂宿
         主对话框，落位贴范围顶层底部，xkb_reposition 经 isWindow+有
         父分流至 xkb_repositionOverlay）；浮层是独立原生窗，可越出对
         话框边界；应用模态门对 Popup 型顶层既有豁免（XWidget.c
         VXWidgetWindow_event「Popup 豁免」分支，QComboBox 弹层同款先
         例）——exec 模态下键帽依然可点（dlg-std 车道活体实证），不
         夺双抓取；
       ③默认——键盘挂宿主顶层窗口底部（XCompleter 弹层挂顶层窗口先
         例；子控件浮层形态，无独立 OS 窗口、无应用模态登记）。 */
    self->m_floating = (self->m_hostOverride != NULL);
    dialogHost = !self->m_floating &&
        (XWidget_windowFlags(host) & (XWidgetFlags)XWindowType_TypeMask) ==
        (XWidgetFlags)XWindowType_Dialog;
    if (self->m_floating) {
        /* ①悬浮模式（setHostWindow 锚生效）：跳过挂父，保持独立顶层窗
           口形态；锚为 Popup 型弹层容器时自动解析主窗口顶层为实际锚
           （几何因此锚定主窗口底部全宽，与普通编辑框弹出一致），raise
           压过日历弹层——时序上键盘 popup 晚于弹层最近一次 show（弹层
           show 即 raise），再显式 raise 一次兜底。 */
        XWidget* anchor = xkb_resolveFloatingHost(self, host);
        if (!anchor) anchor = host;
        xkb_bindHostDestroyed(self, anchor); /* 覆盖宿主 destroyed 防悬垂。 */
        self->m_host = anchor;
        if (XWidget_parentWidget((XWidget*)self))
            XWidget_setParent((XWidget*)self, NULL, 0); /* 归位顶层形态。 */
        /* 窗口类型=Popup（XComboBox 弹层同款）：Popup/ToolTip 等瞬态类
           型永不装饰（XWindowDecoration_activeFor 类型分支）——悬浮键
           盘外观因此与内嵌形态完全一致（无 CSD 标题栏条带）。 */
        XWidget_setWindowFlags((XWidget*)self,
                               (XWidgetFlags)XWindowType_Popup);
    } else {
        xkb_bindHostDestroyed(self, host);
        self->m_host = host;
        if (dialogHost)
            /* ②对话框宿主：Popup 独立顶层浮层（transient 挂宿主对话
               框，落位/首帧口径见下方 flush 与 xkb_repositionOverlay）。 */
            XWidget_setParent((XWidget*)self, host,
                              (XWidgetFlags)XWindowType_Popup);
        else
            /* ③默认：子控件浮层挂宿主顶层窗口底部。 */
            XWidget_setParent((XWidget*)self, host, 0);
    }
    xkb_reposition(self);
    XWidget_show((XWidget*)self);
    XWidget_raise((XWidget*)self);
    if (self->m_floating || dialogHost)
        /* 独立顶层浮层无宿主帧泵：主动补首帧上屏（悬浮锚与对话框浮层
           同口径；XComboBox 弹层 xcombo_popupShow「独立顶层窗口无宿主
           帧泵」同款）。 */
        XWidget_flushBackingStore((XWidget*)self, NULL);
    self->m_popped = true;
    /* 几何跟随接线（2026-10-03 所有者裁定事件推送替代轮询）：宿主/
       范围顶层 x/y/width/heightChanged 四信号连上，WM 拖拽/缩放逐帧
       即时跟随（槽内 reposition，几何短路防成对信号 churn）。 */
    xkb_bindGeometrySignals(self, true);
    if (self->m_floating) {
        /* 抢占弹层模态双抓取：日历弹层 show 即 grabMouse/grabKeyboard
           （跨顶层抓取改道 XWidget.c 派发入口）+ 1ms 后原生 SetCapture
           （win32 把全部鼠标消息收入弹层 HWND）——悬浮面板不夺取则键
           面点击被改道/吞掉。接管后键盘自身点击经本地抓取分支直达，
           物理按键经守护抓取 topLevel 失配回退焦点链照常到编辑框。
           closePopup/hideEvent 成对释放。 */
        /* 抢占前快照框架双抓取者（界外收层时归还，Qt closePopup 的
           previous-grabber 语义；抓取者仍是自己=未收层的重弹，保留旧
           快照不覆盖——closePopup 只按最早会话归还一次）。 */
        if (XWidget_mouseGrabber() != (XWidget*)self)
            self->m_prevMouseGrab = XWidget_mouseGrabber();
        if (XWidget_keyboardGrabber() != (XWidget*)self)
            self->m_prevKbdGrab = XWidget_keyboardGrabber();
        XWidget_grabMouse((XWidget*)self);
        XWidget_grabKeyboard((XWidget*)self);
#if XWINDOW_ON
        {
            XWindow* handle = XWidget_windowHandle((XWidget*)self);
            if (handle) XWindow_setMouseGrabEnabled(handle, true);
        }
#endif
    }
    /* 键盘 NoFocus（XWidget 基类默认）不抢焦点：焦点保持/交还 editor，
       物理键盘直入编辑框不受影响。 */
    xkb_startGuard(self);
}

void XVirtualKeyboard_closePopup(XVirtualKeyboard* self)
{
    if (!self) return;
    /* 用户收起闩锁置位：守护轮询不再原地自动重弹（焦点未迁移时），
     * 直到用户再次按下编辑框（notifyPress 清锁即弹）。 */
    self->m_userCollapsed = true;
    /* 悬浮面板收层先释放双抓取与原生鼠标捕获再隐藏（popup 悬浮分支
       夺取的弹层模态抓取成对归还；identity 判据不误放他者抓取），随后
       隐藏、保持顶层归属（无需归还主窗口，下次 popup 按锚重挂）并清
       空悬浮锚（回内嵌模式）。 */
    if (self->m_floating) {
        XWidget_releaseMouse((XWidget*)self);
        XWidget_releaseKeyboard((XWidget*)self);
#if XWINDOW_ON
        {
            XWindow* handle = XWidget_windowHandle((XWidget*)self);
            if (handle) XWindow_setMouseGrabEnabled(handle, false);
        }
#endif
        /* 归还抢占前抓取者（Qt closePopup 的 previous-grabber 语义；
           XMenu 嵌套弹层交接同款）：框架双抓取+原生鼠标捕获成对还原
           ——悬浮分支从未夺取原生键盘抓取（popup 侧仅 setMouseGrab
           Enabled），只还原鼠标侧。快照=自己（异常态）或已隐藏不还
           原（grabMouse 对不可见控件本就 no-op，isVisible 守卫为
           windowHandle 取用兜底），防把抓取交给不可见控件。 */
        if (self->m_prevMouseGrab &&
            self->m_prevMouseGrab != (XWidget*)self &&
            XWidget_isVisible(self->m_prevMouseGrab)) {
            XWidget_grabMouse(self->m_prevMouseGrab);
#if XWINDOW_ON
            {
                XWindow* prevHandle =
                    XWidget_windowHandle(self->m_prevMouseGrab);
                if (prevHandle) XWindow_setMouseGrabEnabled(prevHandle, true);
            }
#endif
        }
        if (self->m_prevKbdGrab &&
            self->m_prevKbdGrab != (XWidget*)self &&
            XWidget_isVisible(self->m_prevKbdGrab))
            XWidget_grabKeyboard(self->m_prevKbdGrab);
        self->m_prevMouseGrab = NULL;
        self->m_prevKbdGrab = NULL;
    }
    /* 无条件隐藏（不以 isVisible 为前置）：pre-show 收层（无头钩子在
       宿主 show 前调本接口）effectiveVisible 恒假，带「isVisible 才
       隐藏」护栏会跳过隐藏——弹层 show 位残留，宿主 show 后传播把整
       块面板画出（demo 无头钩子记录的 closed 场景残影根因）。已可见
       面板行为不变；未生效可见面板仅补置 WState_Hidden（m_visible
       不变、无 SHOW/HIDE 事件）。 */
    XWidget_setVisible((XWidget*)self, false);
    /* 几何信号先于弹层状态清（2026-10-03 事件化）：会话结束即断，宿主
       后续缩放/拖动不再触发槽；聚合定时器一并撤销。 */
    xkb_bindGeometrySignals(self, false);
    if (self->m_geomSyncTimer != XTIMER_INVALID_ID) {
        XObject_killTimer((XObject*)self, self->m_geomSyncTimer);
        self->m_geomSyncTimer = XTIMER_INVALID_ID;
    }
    self->m_popped = false;
    self->m_floating = false;
    self->m_layoutSelectorOpen = false; /* 键盘选择面板随层关闭。 */
    self->m_editPanelOpen = false; /* 文字编辑面板随层关闭。 */
    self->m_editSelArmed = false;
    self->m_compactFloat = false; /* 紧凑悬浮态随层复位（closePopup 全部复位）。 */
    self->m_compactDrag = false;
    self->m_physKeyActive = false; /* 物理会话标记随层复位。 */
    self->m_hostOverride = NULL; /* 收层自动清悬浮锚（回内嵌挂父模式）。 */
    self->m_hostHint = NULL;     /* 锚解析提示同周期失效。 */
#if XVIRTUALKEYBOARD_ON
    /* 收层弃组串草稿（含关闭键路径）：engine reset→插件 reset
       Composition（弃草稿链，不翻转中文态）+ 清面板本地页；断开落地
       契约连接（无面板连接时 commitRequested/keyEventRequested 无消
       费者=丢弃，文档化口径）；同步 m_prevFocus=当前焦点采样（按下路
       径收层后守护不误记边沿）。 */
    if (xkb_engineOf(self))
        XVirtualKeyboardInputEngine_reset(xkb_engineOf(self));
    self->m_candidatePage = 0;
    xkb_bindContextConns(self, false);
    self->m_prevFocus = XWidget_appFocusWidget();
#endif
    /* 守护按运行条件保留（与 xkb_startGuard 自述一致：弹层可见或
       autoPopup 开）：autoPopup 开时常驻轮询继续（焦点跟随换框兜底），
       仅弹层隐藏；autoPopup 关才随层停。 */
    if (!self->m_autoPopup) xkb_stopGuard(self);
    /* 收层保持解除目标绑定（多框跟随/防悬垂契约不动）。取舍结论（按
       下位置驱动定版）：旧 dismissFix「同框 retap 不重弹」契约作废——
       收起后再按同一编辑框由 notifyPress 按下路径即时重弹（无需焦点
       往返，点空白收起的对称操作）；守护边沿仅为换框兜底。 */
    if (self->m_targetConn) {
        XObject_disconnect_2(self->m_targetConn);
        self->m_targetConn = NULL;
    }
    self->m_target = NULL;
}

bool XVirtualKeyboard_popupVisible(const XVirtualKeyboard* self)
{
    /* 弹出状态 = m_popped 生命周期位（popup 成功路径置位、closePopup
     * 复位），非控件生效可见（XWidget_isVisible）。根因：面板是宿主
     * 顶层窗口的子控件浮层（XVirtualKeyboard_popup 挂宿主，无独立
     * OS 窗口），宿主未 show 时 XWidget_isVisible 按「父链生效可见」
     * 口径恒假——apitest「popup 直呼弹出」与 demo 无头钩子（宿主
     * show 前调 popup）两处挂接成功均被误报未弹。m_popped 与挂接
     * 生命周期严格同步：收层唯一入口 closePopup（文档口径），直接
     * setVisible 收层不在契约内；内部唯一消费点（dismiss 键
     * 「弹层弹出才收」）与平台上下文 isInputPanelVisible 语义不变。 */
    return self ? self->m_popped : false;
}

void XVirtualKeyboard_setHostWindow(XVirtualKeyboard* self, XWidget* host)
{
    XWidget* focus;
    if (!self) return;
    /* 仅存锚（借用）；悬浮形态在随后 popup() 生效（m_floating 置位）。
       已弹出期间改锚不重定位（先 closePopup 再设锚重弹；收层自动清锚
       契约使残留锚不越过一次弹层生命周期）。 */
    self->m_hostOverride = host;
    self->m_hostHint = NULL;
    if (host) {
        /* 锚为 Popup 型弹层容器时主窗口反向引用不可达（widget 父链/
           transient parent 均空，应用顶层表在 XGuiApplication 单根应用
           下不可用），此刻补拍焦点顶层快照作解析提示（m_hostHint）：
           契约时序=布锚先于落焦（XCalendarWidget beginYearEdit 先布锚
           后 setFocus），快照即弹层打开前用户所在主窗口顶层（transient
           parent 语义）；焦点为空/已在锚内/Popup 型时无快照，解析回落
           m_host/应用顶层表/锚自身。 */
        focus = XWidget_appFocusWidget();
        if (focus && !xkb_insidePanel(self, focus)) {
            XWidget* top = XWidget_isWindow(focus)
                               ? (XWidget*)focus
                               : XWidget_topLevelWidget(focus);
            if (top != host && XWidget_isWindow(top) &&
                !xkb_isPopupTypeWindow(top))
                self->m_hostHint = top;
        }
    }
    if (!host) self->m_floating = false;
}

XWidget* XVirtualKeyboard_hostWindow(const XVirtualKeyboard* self)
{
    return self ? self->m_hostOverride : NULL;
}

void XVirtualKeyboard_setAutoPopup(XVirtualKeyboard* self, bool on)
{
    if (!self) return;
    if (on == self->m_autoPopup) {
        /* 值未变不空转：守护缺席时补启（自愈 init 后被外部停掉、无调
           度器环境下首次启动失败后恢复等漂移，保证 on 恒常驻）。 */
        if (on && self->m_guardTimer == XTIMER_INVALID_ID)
            xkb_startGuard(self);
        return;
    }
    self->m_autoPopup = on;
    if (on)
        xkb_startGuard(self); /* 常驻轮询（运行条件满足即启动）。 */
    else if (!self->m_popped)
        xkb_stopGuard(self);
}

bool XVirtualKeyboard_autoPopup(const XVirtualKeyboard* self)
{
    return self ? self->m_autoPopup : false;
}

void XVirtualKeyboard_notifyPress(XVirtualKeyboard* self, XWidget* hit)
{
#if XVIRTUALKEYBOARD_DESKTOP_ON
    bool accept;
    if (!self || !self->m_autoPopup) return; /* autoPopup 关=应用全权接管。 */
    if (xkb_insidePanel(self, hit)) return;  /* 键盘自身按键不受影响。 */
    /* 穿透归属上溯（与按下派发循环 XWidget.c:1529-1547 同口径：
       XWidget_attrTest(TransparentForMouseEvents) 跳过、沿父链上抛）。
       childAt 不跳
       穿透控件，点日期时间字段编辑区时 hit=内嵌行编辑（F3-② 置穿透，
       XDateTimeEdit.c:3014；其 hints=ImhNone→误弹 QWERTY、合成数字键
       直插显示文本绕过分段消化）。收敛到实际接收按下事件的祖先=字段壳
       （WA14+DigitsOnly），键盘目标与事件接收者对齐；hit 为形参本地量，
       调用方汇聚点（XWidget.c:1520）不受影响。 */
    while (hit &&
           XWidget_testAttribute(hit,
                                 XWidgetAttribute_TransparentForMouseEvents))
        hit = XWidget_parentWidget(hit);
    /* accept 判据与守护 tick ②全等（supportedTarget=vtable 快路径 ∪
       WA14 放行回退 &&enabled&&总开关）：同框 opt-out 编辑框按下不弹
       （Qt inputMethodAccepted 口径），已弹则随非编辑分支幂等收层；
       WA14 放行使 XDateEdit/XDateTimeEdit 字段壳按下即弹面板不闪退。 */
    accept = hit && xkb_supportedTarget(hit) &&
             XWidget_isEnabled(hit) &&
             XWidget_testAttribute(hit, XWidgetAttribute_InputMethodEnabled);
#if XVIRTUALKEYBOARD_ON
    accept = accept &&
             XVirtualKeyboardSettings_keyboardEnabled(
                 XVirtualKeyboardSettings_instance());
#endif
    if (accept) {
        /* 弹出/重绑；已弹且同框→保持（不重放 hints，面板内手动模式
           切换不被复位置回）。 */
        if (!self->m_popped || self->m_target != hit)
            XVirtualKeyboard_popup(self, hit);
    } else if (self->m_popped) {
        /* 非编辑区域（页面空白/按钮/标签/不可接受编辑框）：dismiss，
           组串弃置等 closePopup 副作用随行。 */
        XVirtualKeyboard_closePopup(self);
    }
#else /* !XVIRTUALKEYBOARD_DESKTOP_ON */
    (void)self;
    (void)hit; /* 非桌面形态：守护同停，仅显式 popup/closePopup。 */
#endif /* XVIRTUALKEYBOARD_DESKTOP_ON */
}

bool XVirtualKeyboard_notifyKey(XVirtualKeyboard* self, int key,
                                XKeyboardModifiers modifiers)
{
    char text[2];
#if !XVIRTUALKEYBOARD_DESKTOP_ON
    (void)key;
    (void)modifiers;
    return false;
#else
    if (!self || !self->m_popped) return false; /* 面板未弹：不拦截。 */
    /* 带 Ctrl/Alt/Meta 修饰=快捷键语义（转化层在快捷键匹配之前，
       XWidget 派发入口）：不进组串链、不触发收层，放行走快捷键匹配
       （Qt 输入法只拦纯字符键口径）；Shift 保留（大写字符语义），
       Keypad 为来源标志不参与判定。 */
    if (modifiers & (XKeyboardModifier_ControlModifier |
                     XKeyboardModifier_AltModifier |
                     XKeyboardModifier_MetaModifier))
        return false;
#if XVIRTUALKEYBOARD_ON
    /* 组串中数字 1..9：面板分页拦截选候选（编号角标同源映射：
     * 全量下标=页基×页容量+位序）。这是物理数字键在组串态的首要
     * 语义（2026-10-04 用户反馈：想选 2 号候选却直落编辑框——按键
     * 绕过键盘转化层直达焦点控件）。款型门与屏键链同口径
     * （xkb_activateButton 数字拦截）：仅 PinyinFull 组串候选走面板
     * 分页——T9 组串态数字与屏键同链进 feedT9Digit（插件契约：T9 下
     * 仅 '1' 转选候选，2..9 扩组串），物理键/屏键不分歧。 */
    if (key >= '1' && key <= '9' && xkb_composeRouteWanted(self) &&
        xkb_imeComposing(self) && xkb_imeInterceptDigit(self, (char)key))
        return true;
#endif
    /* 字母/数字/空格/退格/回车：转虚拟键喂引擎（组串态进组串链；引擎
     * 不消费回落 false→按键照常进编辑框——英文态打字不受影响）。 */
    if (key >= 'a' && key <= 'z') {
        text[0] = (char)key;
        text[1] = '\0';
    } else if (key >= 'A' && key <= 'Z') {
        if (modifiers & XKeyboardModifier_ShiftModifier) {
            text[0] = (char)key; /* Shift 大写直写（组串小写归一在路由内）。 */
            text[1] = '\0';
        } else {
            text[0] = (char)(key - 'A' + 'a');
            text[1] = '\0';
        }
    } else if (key == XKey_Space) {
        text[0] = ' ';
        text[1] = '\0';
    } else if (key >= '0' && key <= '9') {
        /* 物理数字 0..9（PinyinFull 组串 1..9 选候选已在上方拦截）：
         * 组装进路由链与屏键同口径（T9 组串扩位/插件 '1' 选候选）；
         * 未被消费时英文态直落编辑框并随行收层（用户需求②），中文态
         * ——含组串中 0 与无组串数字——一律不收层照旧放行。 */
        text[0] = (char)key;
        text[1] = '\0';
    } else if (key == XKey_Backspace) {
        /* 退格/回车：与屏键同款型门（composeRouteWanted∪t9RouteWanted
         * ——xkb_activateButton 同门）——EnglishFull 刻意不翻转引擎态
         * （setLayoutKind 中文态快照口径），无门会把物理退格/回车喂进
         * 组串链且候选带缺席不可见；门外放行走内置语义（删编辑框字符
         * /returnPressed）。 */
        if (xkb_composeRouteWanted(self) || xkb_t9RouteWanted(self))
            return xkb_routeKey(self, XKEYBOARD_LBL_BACKSPACE); /* 标签多字节。 */
        return false;
    } else if (key == XKey_Return || key == XKey_Enter) {
        if (xkb_composeRouteWanted(self) || xkb_t9RouteWanted(self))
            return xkb_routeKey(self, XKEYBOARD_LBL_SEARCH);
        return false;
    } else {
        return false; /* 其余键（符号/光标/F 功能键等）不拦截。 */
    }
    /* 路由（与 xkb_activateButton 屏键链同款型口径）：T9 中文态走九键
     * 路由单点 xkb_t9RouteKey（数字组标签 feedT9Digit 扩组串——插件契
     * 约 '0'/'1' 不在组映射自然落 routeKey 选候选链）+ routeKey（退格
     * 删末位/回车原串）；PinyinFull 中文态走 routeKey（字母进组串/数
     * 字选候选/空格首选）。EnglishFull 固定英文态不路由——引擎态被
     * setLayoutKind 刻意保留时物理键不进组串链（候选带缺席组串不可
     * 见，屏键直写/物理键同口径）。未消费落下方统一收层点（英文态收
     * 层放行/中文态照旧放行）。 */
#if XVIRTUALKEYBOARD_ON
    {
        bool routed = false;
        if (xkb_t9RouteWanted(self))
            routed = xkb_t9RouteKey(self, text) ||
                     xkb_routeKey(self, text);
        else if (xkb_composeRouteWanted(self))
            routed = xkb_routeKey(self, text);
        if (routed) {
            /* 物理会话标记（需求②③输入源判据）随路由消费置位后主动
               重定位：首个字母的 preeditSlot 在 routeKey 内先行（彼时
               标记未置位走全量），此处补收缩拍——物理组串首键即收缩；
               屏幕键路径不经此块（xkb_activateButton 无标记），组串保
               持全量面板。 */
            self->m_physKeyActive = true;
            xkb_reposition(self);
            xkb_imeSyncPageSize(self); /* 组串变化：变宽 chip 页容量同步。 */
            XWidget_update((XWidget*)self);
            return true;
        }
    }
#endif
    /* 英文态物理字母/空格未被引擎消费（Plain 直通/拼音英文态不进组串）：
       放行进焦点编辑框前收层（用户需求②「英文状态直接输入，收起屏幕
       键盘」）——closePopup 置 m_userCollapsed 闩锁，守护不原地重弹，
       下次点编辑框由 notifyPress 清锁再弹。中文态组串/候选链即使插件
       未消费也不误收；Backspace/Enter 走标签路由直返，不参与收层。 */
    if (!xkb_imeChineseState(self)) XVirtualKeyboard_closePopup(self);
    return false;
#endif /* XVIRTUALKEYBOARD_DESKTOP_ON */
}

/* ==================== 拼音输入（XGui 扩展，XVIRTUALKEYBOARD_ON 门控；
 *                       薄委托引擎/插件） ==================== */

#if XVIRTUALKEYBOARD_ON

bool XVirtualKeyboard_setImeEnabled(XVirtualKeyboard* self, bool on)
{
    XVirtualKeyboardInputEngine* engine;
    XVirtualKeyboardAbstractInputMethod* plugin;
    if (!self) return false;
    if (on == self->m_imeEnabled) return true; /* 值未变无操作。 */
    engine = xkb_engineOf(self);
    if (!engine) return false;
    if (on) {
#if XKEYBOARD_IME_ON
        /* 薄委托（apiMapping#13）：engine 装 Pinyin 插件（XPinyinEngine
           实例=插件独占，面板零直连）+ setInputMode(Pinyin)（中文态
           默认 true），再装表切槽——使 setMap/setMode 内部重建即按候
           选带预留行高。 */
        plugin = (XVirtualKeyboardAbstractInputMethod*)
            XVirtualKeyboardPinyinInputMethod_create();
        if (!plugin) return false;
        {
            XVirtualKeyboardAbstractInputMethod* old =
                XVirtualKeyboardInputEngine_inputMethod(engine);
            XVirtualKeyboardInputEngine_setInputMethod(engine, plugin);
            if (old) {
                /* 旧输入法的 keyClick 等回调可能仍挂在引擎事件链上，
                   延迟回收防发射帧内同步释放。 */
                XObject_deleteLater((XObject*)old);
            }
        }
        self->m_imeEnabled = true;
#if XKEYBOARD_IME_PHRASE_ON
        /* 词组库懒加载（幂等+负结果粘滞；本处值未变早退保证仅首次
           enable 触发一次 IO——守护定时器等纯 UI 巡检路径不挂 IO）。 */
        XPinyinPhrase_load();
#endif
        XVirtualKeyboardInputEngine_setInputMode(
            engine, XVirtualKeyboardInputEngineInputMode_Pinyin);
        XVirtualKeyboard_setMap(self, XKeyboardMode_User1, XPinyinEngine_map(),
                         XPinyinEngine_ctrlMap());
        XVirtualKeyboard_setMode(self, XKeyboardMode_User1);
#else
        return false; /* IME 状态机裁剪：无拼音插件可装。 */
#endif
    } else {
        /* 薄委托：engine 装回 Plain 插件（旧插件实例由装配链卸载——
           本实现引擎不持有所有权，此处显式释放）。模式不动：User1 槽
           位仍持拼音表（槽位占用约定，不恢复原表也不切走——切走会让
           buttonCount 回落 40，丢失「槽位仍为拼音表」契约）；候选带随
           m_imeEnabled=false 在尾部统一重建中归零。 */
        XVirtualKeyboardAbstractInputMethod* old =
            XVirtualKeyboardInputEngine_inputMethod(engine);
        self->m_imeEnabled = false;
        XVirtualKeyboardInputEngine_setInputMethod(
            engine, (XVirtualKeyboardAbstractInputMethod*)
                        XVirtualKeyboardPlainInputMethod_create());
        if (old) XObject_deleteLater((XObject*)old);
    }
    /* 无论 setMode 是否已触发过重建（值未变早退），统一重建一次确保
       带矩形与布局同步（重建幂等且廉价）。 */
    xkb_rebuildLayout(self);
    xkb_notifyObserver(self);
    XWidget_update((XWidget*)self);
    return true;
}

bool XVirtualKeyboard_imeEnabled(const XVirtualKeyboard* self)
{
    return self ? self->m_imeEnabled : false;
}

bool XVirtualKeyboard_setImeChinese(XVirtualKeyboard* self, bool chinese)
{
    XVirtualKeyboardInputEngine* engine;
    if (!self || !self->m_imeEnabled) return false; /* IME 未启用拒绝。 */
    if (chinese == xkb_imeChineseState(self)) return true;
    engine = xkb_engineOf(self);
    if (!engine) return false;
    /* 薄委托：中/EN=输入模式切换（对齐 Qt 中/EN 键语义；插件内部清
       组串候选）。候选带显隐随中文态（行高 5/6 行重排），一次重建代
       价可接受。 */
    if (!XVirtualKeyboardInputEngine_setInputMode(
            engine, chinese ? XVirtualKeyboardInputEngineInputMode_Pinyin
                            : XVirtualKeyboardInputEngineInputMode_Latin))
        return false;
    self->m_candidatePage = 0;
    xkb_rebuildLayout(self);
    xkb_notifyObserver(self);
    XWidget_update((XWidget*)self);
    return true;
}

bool XVirtualKeyboard_imeChinese(const XVirtualKeyboard* self)
{
    return xkb_imeChineseState(self);
}

#endif /* XVIRTUALKEYBOARD_ON */

/* ==================== 信号（纯 ID getter） ==================== */

void* XVirtualKeyboard_ready_signal(XVirtualKeyboard* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboard_ready_signal;
}

void* XVirtualKeyboard_cancel_signal(XVirtualKeyboard* self)
{
    (void)self;
    return (void*)(size_t)XVirtualKeyboard_cancel_signal;
}

void* XVirtualKeyboard_buttonActivated_signal(XVirtualKeyboard* self, int32_t buttonId)
{
    (void)self;
    (void)buttonId;
    return (void*)(size_t)XVirtualKeyboard_buttonActivated_signal;
}

#endif /* XWIDGET_ON && XKEYBOARD_ON */
