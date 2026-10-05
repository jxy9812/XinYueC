/**
 * @file       XVirtualKeyboard.h
 * @brief      XVirtualKeyboard 屏幕虚拟键盘控件（对标 LVGL 9.2.2 lv_keyboard
 *             全部公共语义）。
 * @details    功能范围：
 *             - 继承：XVirtualKeyboard 继承 XWidget；矩阵行为（键位布局/命中/
 *               激活）由本类内部静态私有函数承载（xkb_rebuildLayout/
 *               xkb_hitTest/xkb_activateButton），不单拆公开按钮矩阵
 *               控件——XGui 无按钮矩阵控件（XButtonGroup 是纯逻辑分组
 *               对象），自绘网格有在库先例（XCalendarWidget 日期网格）；
 *             - 布局：4+1 种内置映射模式（TextLower/TextUpper/Special/
 *               Number/User1..User4，对标 lv_keyboard_mode_t；User1..4
 *               默认回落小写映射），键位宽度为每行独立归一化的相对单
 *               位（每行按自身单位总数铺满行宽，对标 lv_buttonmatrix）；
 *               setMap 是接入其它字符集/输入法布局的唯一官方途径；
 *             - 布局款型（Sogou 改版一阶段，XGui 扩展）：layoutKind=
 *               PinyinFull（默认，搜狗全键 4 行 35 键）/PinyinT9（九键
 *               4 行 21 键）/EnglishFull（与 PinyinFull 同几何、固定英
 *               文态）三层独立于 XKeyboardMode——款型主表装载进
 *               TextLower/TextUpper 双槽位（setLayoutKind 时覆盖装
 *               载，此后 setMap 仍可按槽位覆写），Special/Number/
 *               User1..4/Digits/Dialpad 槽位与 hints 自动切换机制完
 *               全不动（现状可恢复承诺）；PinyinFull/EnglishFull 主表
 *               shift 键为布局层三态展示（小写→大写一次性→锁定，大写
 *               态换装大写键帽表）；
 *             - 输出：setTextArea 绑定输出目标（借用；仅存指针，不改
 *               焦点/组），按键经合成 XKeyEvent（与真实输入同路径，可
 *               获编辑框校验/掩码/撤销全套语义）或公开插入 API 直写；
 *             - 弹层（XGui 扩展，非 LVGL 语义）：popup/closePopup 把
 *               键盘挂到编辑框顶层窗口底部（子控件浮层，无独立 OS 窗
 *               口、无应用模态登记）；setHostWindow 悬浮模式例外：
 *               悬浮锚非空时跳过挂父，键盘保持独立顶层窗口、悬浮锚定
 *               宿主全局底部并 raise 到日历弹层等一切同应用窗口之上
 *               （对标 QVirtualKeyboardInputPanel 独立顶层窗口形态；
 *               锚=弹层容器时自动解析主窗口顶层，几何与普通编辑框弹
 *               出完全一致）；自动弹收主判据=按下位置驱动
 *               （notifyPress：指针 PRESS 命中受支持编辑框→弹出/重绑，
 *               命中非编辑区域→收起；经 XGuiApplication 转发自
 *               XWidget_dispatchPointerEvent 的 PRESS 汇聚点，标准触摸
 *               UX），守护轮询（setAutoPopup，机制=守护定时器轮询
 *               XWidget_appFocusWidget + destroyed 防悬垂连接；XGui
 *               事件过滤器对焦点/输入桥事件不可达，与 XCompleter 守护
 *               轮询先例一致）降为焦点跟随换框兜底（IME 候选带重定
 *               位等稳态巡检仍依赖轮询）；
 *             - 气泡 popover：非浮层、绘制期实现（按压中的 POPOVER 键
 *               文本在键位上方一倍键高处绘制）；
 *             - 菜单条（Sogou 改版二阶段，XGui 扩展）：键区上方常驻一
 *               条菜单条（条高=既有组串带预留通道 contentH/(rows+1)，
 *               所有布局与模式一致预留，原气泡带预留通道并入本条）；拼
 *               音中文态且组串非空（preedit 非空）时渲染为既有拼音候
 *               选带（「正在输入的字母和匹配到的中文」替换菜单，矩形=
 *               条矩形），否则渲染为四图标工具栏（悬浮切换/键盘选择/
 *               文字编辑/收起，四等分对标搜狗；收起=closePopup 直连，
 *               文字编辑/悬浮切换为桩）；
 *             - 键盘选择面板（Sogou 改版三阶段，XGui 扩展）：点工具栏
 *               「键盘选择」图标，面板内部自绘选择面板覆盖键区（非独
 *               立窗口）：拼音全键/拼音九键/英文全键三行、当前项打勾
 *               （勾线绘制），点选项=setLayoutKind 并关闭（IME 启用且
 *               停 User1 槽位时随行落 TextLower 让款型主表可见），点
 *               面板外/再点图标=关闭；面板绘制风格与键面一致；
 *             - 文字编辑面板（Sogou 改版三阶段收尾，XGui 扩展）：点工
 *               具栏「文字编辑」图标进入面板本地自绘编辑面板（标题行
 *               「文字编辑」+右端返回箭头；主区大块白底 上/左/开始选
 *               择/右/下 + 底行 Home/全选/End；右列 退格/复制/剪切/
 *               粘贴）：方向/Home/End 合成方向键到绑定目标（选择态携
 *               带 ShiftModifier）、「开始选择」切换面板本地选择态
 *               （按下视觉保持）、全选=Ctrl+A、⌫=退格；复制/剪切/粘
 *               贴=目标控件公开 API 优先（XLineEdit/XPlainTextEdit/
 *               XTextEdit vtable 识别直调），未识别目标回落合成 Ctrl+
 *               C/X/V；无绑定目标时动作键画禁用态（灰字）。返回（标
 *               题行箭头或再点工具栏图标）回原布局，进入/退出不影响
 *               弹层状态与目标绑定；与键盘选择面板互斥开合；
 *             - 紧凑悬浮态（Sogou 改版四阶段，XGui 扩展）：工具栏「悬
 *               浮切换」图标在停靠态与紧凑悬浮态间切换——进入=键盘原
 *               位转独立顶层 Popup（复用悬浮形态转换路径 setParent
 *               (NULL,0)+setWindowFlags(Popup)），尺寸=宽 min(宿主宽
 *               45%,420)、高按同比例缩放（保持 行数+工具栏 宽高比），
 *               落位宿主右下角；退出=回宿主底部全宽停靠（既有内嵌几
 *               何）。本态为第三形态（compactFloat，与 m_floating/
 *               setHostWindow 悬浮锚语义正交）：xkb_reposition 宿主几
 *               何跟随被 m_compactFloat 守卫跳过（不抢几何，用户可拖
 *               移）；拖移=按住工具栏空白（图标盒之外）拖移窗口（按
 *               下记全局锚点偏移、move 求 delta 移窗、压在图标上不打
 *               启拖动）；守护 tick 顶层 raise 维护继续生效；closePopup
 *               与 popup() 全部复位（popup 重评估形态）；
 *             - 九键路由（Sogou 改版三阶段，XVK∧IME 门控）：T9 款型
 *               且中文态时数字组键（ABC/DEF/…/WXYZ）接引擎九键数字
 *               通道（feedT9Digit）、分词=feedDigitSeparator、重输=
 *               engine reset；组串显示=数字串（composingText 直显口
 *               径，候选带复用），候选/上屏/退格走既有组串链；0 键与
 *               左列标点直写；EnglishFull 固定英文态（字母恒直写，中
 *               /EN 键按压收敛回英文）；
 *             - 信号：ready（确认键，对标 LV_EVENT_READY）/cancel（关
 *               闭/收起键，对标 LV_EVENT_CANCEL）/buttonActivated（任
 *               意按钮激活，对标按钮矩阵 VALUE_CHANGED，参数为按钮 id）；
 *             - Qt Virtual Keyboard 面板接线（XGui 扩展，
 *               XVIRTUALKEYBOARD_ON 门控）：本控件=InputPanel 等价物
 *               （Keyboard.qml 职责并入），输入状态机零直连——m_ime
 *               成员删除，拼音组串归 XVirtualKeyboardPinyinInputMethod
 *               插件独占（内嵌 XPinyinEngine 实例）；面板消费面全部
 *               Qt 形态：候选=engine.wordCandidateListModel()（dataAt
 *               取 XVariant*）、组串显示=context.preeditText()、分页
 *               =面板本地 UI 状态、提交/虚键=context 公共信号
 *               commitRequested/keyEventRequested→既有写入链；按键经
 *               engine 虚键单点路由（插件可消费，未消费回落内置语义）；
 *               中/EN 切换=engine.setInputMode(Pinyin/Latin)；守护
 *               guardTick 焦点边沿（dismissFix 三段边沿）保留为兜底，
 *               弹收主判据=按下位置（notifyPress）；closeOnReturn 收
 *               面板分支。四便
 *               捷 API（setImeEnabled/setImeChinese/imeEnabled/
 *               imeChinese）保留为薄委托。详见
 *               Src/XGui/VirtualKeyboard/ 与 setImeEnabled。
 * @note       模块总开关 XKEYBOARD_ON 定义于 XGuiConfig.h；=0 时裁剪
 *             全部公共 API。核心不硬依赖编辑控件：XLineEdit/
 *             XPlainTextEdit/XTextEdit 仅是适配层依赖（写入/识别按
 *             各自 X*_ON 开关降级），合成键路径类型无关。
 * @note       自动弹出/跟随为 XGui 扩展（LVGL 键盘无任何自身显隐逻
 *             辑）；LVGL 的『消费即中止事件链』在 XGui void 返回信号
 *             下不存在，cancel 发射后弹层可见即自动收层亦为扩展行为。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARD_H
#define XVIRTUALKEYBOARD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "XGuiConfig.h"
#include "XWidget.h"
#include "XGeometry.h"

#if XWIDGET_ON && XKEYBOARD_ON

/* ==================== 常量 ==================== */

/** @brief 当前布局按钮数上限（超出的 setMap 整体拒绝）。 */
#define XKEYBOARD_MAX_BUTTONS 64

/* 键帽/候选等键盘文字的等比缩放系数：labelPx = 键行高×35%（钳
 * [9,48]）。35% 由 800×600 缺省标定（键行高≈46px → 16px，与历史
 * 恒定字号一致）；键盘放大时文字同等放大（等比例缩放需求）。 */
#define XKB_LABEL_PX_PER_KEYH_PERCENT 35
/** @brief 无选中/无按下按钮（对标 LV_BUTTONMATRIX_BUTTON_NONE）。 */
#define XKEYBOARD_BUTTON_NONE 0xFFFFFFFFu
/** @brief 模式槽位总数（4 内置 + 4 用户；VK 合成布局落 8..9 独立计）。
 *         第 9 位（下标 8）保留 NULL 尾语义仅对内置+用户段成立——
 *         m_maps 数组实际容量见 XKEYBOARD_MODE_SLOT_COUNT。 */
#define XKEYBOARD_MODE_COUNT 8
/** @brief 槽位存储容量（4 内置 + 4 用户 + Digits/Dialpad 两 VK 合成
 *         布局；第 11 位保留 NULL 尾）。setMap 仍只接受 0..7。 */
#define XKEYBOARD_MODE_SLOT_COUNT 10
/** @brief 长按重复起振延时（毫秒；对标 lv_indev 长按默认 400ms）。 */
#ifndef XKEYBOARD_LONG_PRESS_START_MS
#define XKEYBOARD_LONG_PRESS_START_MS 400
#endif
/** @brief 长按重复间隔（毫秒；对标 lv_indev 重复默认 100ms）。 */
#ifndef XKEYBOARD_REPEAT_MS
#define XKEYBOARD_REPEAT_MS 100
#endif
/** @brief 守护轮询周期（毫秒；XGui 扩展，对标 XCompleter 守护先例）。 */
#ifndef XKEYBOARD_GUARD_INTERVAL_MS
#define XKEYBOARD_GUARD_INTERVAL_MS 200
#endif
/** @brief 弹层高度下界（像素；钳位次序：hostH/2 → 下界 → 上界）。 */
#ifndef XKEYBOARD_POPUP_MIN_H
#define XKEYBOARD_POPUP_MIN_H 120
#endif
/** @brief 弹层宽度下界（像素；宿主未布局（宽 0）时的保底面板宽——
 *  键位矩形可命中，reposition 不把面板缩为零宽）。 */
#ifndef XKEYBOARD_POPUP_MIN_W
#define XKEYBOARD_POPUP_MIN_W 240
#endif
/** @brief 弹层高度上界余量（像素；恒给编辑区保留的高度）。 */
#ifndef XKEYBOARD_POPUP_MARGIN_H
#define XKEYBOARD_POPUP_MARGIN_H 32
#endif

/**
 * @brief      按键宽度单位提取（控制字低 4 位；1 = 占满该行的 1 份）。
 */
#define XKEYBOARD_BTN(w) ((XKeyboardButtonCtrl)((w) | XKEYBOARD_CTRL_POPOVER))

/* ==================== 枚举 ==================== */

/**
 * @brief      布局模式（对标 lv_keyboard_mode_t，0..7 数值一致）。
 * @details    TextLower 小写字母；TextUpper 大写字母（LVGL 9.2.2 大写
 *             是独立布局，无 shift 锁存/自动回落）；Special 特殊符号；
 *             Number 数字；User1..User4 自定义槽位（默认回落小写映射，
 *             经 setMap 替换后成为第 5~8 种布局）；Digits/Dialpad 为
 *             VK 对齐新增合成布局（hints→布局映射专用，12 键数字/
 *             3x4 拨号盘，不可 setMap 覆写）。
 */
typedef enum XKeyboardMode
{
    XKeyboardMode_TextLower = 0,  /**< 小写字母布局。 */
    XKeyboardMode_TextUpper = 1,  /**< 大写字母布局（独立布局非锁存）。 */
    XKeyboardMode_Special = 2,    /**< 特殊符号布局。 */
    XKeyboardMode_Number = 3,     /**< 数字布局（17 键）。 */
    XKeyboardMode_User1 = 4,      /**< 用户自定义布局 1（默认回落小写）。 */
    XKeyboardMode_User2 = 5,      /**< 用户自定义布局 2（默认回落小写）。 */
    XKeyboardMode_User3 = 6,      /**< 用户自定义布局 3（默认回落小写）。 */
    XKeyboardMode_User4 = 7,      /**< 用户自定义布局 4（默认回落小写）。 */
    XKeyboardMode_Digits = 8,     /**< VK 合成数字布局（12 键；hints
                                       DigitsOnly 映射目标）。 */
    XKeyboardMode_Dialpad = 9     /**< VK 合成拨号布局（3x4；hints
                                       DialableCharactersOnly 映射目标）。 */
} XKeyboardMode;

/**
 * @brief      按键控制字（对标 lv_buttonmatrix_ctrl_t 常量子集）。
 * @details    低 4 位为宽度单位（XKEYBOARD_BTN 宏一并置 POPOVER 位）；
 *             标志位可按位组合。BUTTON_FLAGS = NO_REPEAT|CLICK_TRIG|
 *             CHECKED（对标 LV_KEYBOARD_CTRL_BUTTON_FLAGS）：模式切换/
 *             关闭/确认/光标键携带（不重复、点击触发、选中样式）。
 * @note       隐藏键（LV_BUTTONMATRIX_CTRL_HIDDEN）、checkable toggle/
 *             one_check 不在本控件范围。
 */
typedef enum XKeyboardButtonCtrl
{
    XKEYBOARD_CTRL_WIDTH_MASK = 0x000F,     /**< 宽度单位掩码（低 4 位）。 */
    XKEYBOARD_CTRL_NO_REPEAT = 0x0100,      /**< 禁止长按重复。 */
    XKEYBOARD_CTRL_CLICK_TRIG = 0x0200,     /**< 释放时触发（默认按下触发）。 */
    XKEYBOARD_CTRL_CHECKED = 0x0400,        /**< 静态选中样式（非 toggle）。 */
    XKEYBOARD_CTRL_POPOVER = 0x0800,        /**< 按压放大气泡（ popovers=0 时
                                                 生效表剥除该位）。 */
    XKEYBOARD_CTRL_BUTTON_FLAGS = XKEYBOARD_CTRL_NO_REPEAT |
                                  XKEYBOARD_CTRL_CLICK_TRIG |
                                  XKEYBOARD_CTRL_CHECKED /**< 控制键标志组合。 */
} XKeyboardButtonCtrl;

/**
 * @brief      布局款型（Sogou 改版一阶段；对标安卓搜狗输入法全键/九键）。
 * @details    款型层独立于 XKeyboardMode：款型主表经 setLayoutKind 装
 *             载进 TextLower/TextUpper 双槽位（PinyinFull/EnglishFull
 *             大小写各一表，T9 双槽位同装一表），Special/Number/
 *             User1..4/Digits/Dialpad 槽位与 hints 自动切换机制不受影
 *             响（既有模式机制保持不变）。默认 PinyinFull。
 */
typedef enum XKeyboardLayoutKind
{
    XKeyboardLayout_PinyinFull = 0, /**< 拼音全键（搜狗对标；4 行 35 键，
                                         行4 含中/EN 与搜索键）。 */
    XKeyboardLayout_PinyinT9,       /**< 拼音九键（搜狗对标；4 行 21 键，
                                         左列标点直插+主区 3x4；分词/重
                                         输/数字键引擎接线后续阶段）。 */
    XKeyboardLayout_EnglishFull     /**< 英文全键（与 PinyinFull 同几何
                                         共表；固定英文态——字母不进 IME
                                         组串、候选带不预留）。 */
} XKeyboardLayoutKind;

/* ==================== 控制键标签（UTF-8 常量） ==================== */

/** @brief 退格键标签（字库无 ⌫ 字形，键位文案全部用文本）。 */
#define XKEYBOARD_LBL_BACKSPACE "\xE9\x80\x80\xE6\xA0\xBC"          /* 退格 */
/** @brief 换行键标签。 */
#define XKEYBOARD_LBL_NEWLINE "\xE6\x8D\xA2\xE8\xA1\x8C"            /* 换行 */
/** @brief 光标左移键标签。 */
#define XKEYBOARD_LBL_LEFT "<-"
/** @brief 光标右移键标签。 */
#define XKEYBOARD_LBL_RIGHT "->"
/** @brief 确认键标签（发射 ready 信号，不自动收层）。 */
#define XKEYBOARD_LBL_OK "\xE7\xA1\xAE\xE8\xAE\xA4"                 /* 确认 */
/** @brief 关闭键标签（发射 cancel 信号，弹层可见即收层）。 */
#define XKEYBOARD_LBL_CLOSE "\xE5\x85\xB3\xE9\x97\xAD"              /* 关闭 */
/** @brief 收起键标签（语义同关闭键）。 */
#define XKEYBOARD_LBL_DISMISS "\xE6\x94\xB6\xE8\xB5\xB7"            /* 收起 */
/** @brief 数字模式符号翻转键标签。 */
#define XKEYBOARD_LBL_SIGN "+/-"
/** @brief 换行键兼容匹配别名（同时匹配本串与换行标签，兼容从 LVGL
 *         逐键移植的自定义 map；对标 lv_keyboard_def_event_cb 的
 *         "Enter"/NEW_LINE 双匹配）。 */
#define XKEYBOARD_LBL_ENTER "Enter"
/** @brief 小写切换键标签。 */
#define XKEYBOARD_LBL_LOWER "abc"
/** @brief 大写切换键标签。 */
#define XKEYBOARD_LBL_UPPER "ABC"
/** @brief 符号切换键标签。 */
#define XKEYBOARD_LBL_SPECIAL "1#"
/** @brief 中/EN 切换键标签（拼音 IME 布局专用；XKEYBOARD_IME_ON）。 */
#define XKEYBOARD_LBL_IME "\xE4\xB8\xAD/EN"                         /* 中/EN */
/** @brief 布局款型 shift 键标签（PinyinFull/EnglishFull 行 3；字库无
 *         ⇧ 字形，键位文案全部用文本，同退格键口径）。三态循环：小写→
 *         大写（一次性，字母键入后自动回落）→锁定（键面 CHECKED 高
 *         亮）→小写。 */
#define XKEYBOARD_LBL_SHIFT "Shift"
/** @brief 符号切换键标签（PinyinFull/T9 行 4；切既有 Special 模式）。 */
#define XKEYBOARD_LBL_SYMBOL "\xE7\xAC\xA6"                         /* 符 */
/** @brief 数字切换键标签（PinyinFull/T9 行 4；切既有 Number 模式，
 *         数字键走数字盘，主表不再含数字行）。 */
#define XKEYBOARD_LBL_NUMBERS "123"
/** @brief 搜索键标签（PinyinFull/T9 行 4 末键；与换行/确认同语义——
 *         合成 XKey_Return，closeOnReturn 分支同享）。 */
#define XKEYBOARD_LBL_SEARCH "\xE6\x90\x9C\xE7\xB4\xA2"             /* 搜索 */
/** @brief 中文句号键标签（PinyinFull 行 4 与 T9 标点列共用；多字节
 *         UTF-8 经写入链直插）。 */
#define XKEYBOARD_LBL_CJK_PERIOD "\xE3\x80\x82"                     /* 。 */
/** @brief T9 标点列全角逗号标签（左列直插标点）。 */
#define XKEYBOARD_LBL_T9_COMMA "\xEF\xBC\x8C"                       /* ， */
/** @brief T9 标点列全角问号标签（左列直插标点）。 */
#define XKEYBOARD_LBL_T9_QMARK "\xEF\xBC\x9F"                       /* ？ */
/** @brief T9 标点列全角叹号标签（左列直插标点）。 */
#define XKEYBOARD_LBL_T9_EXMARK "\xEF\xBC\x81"                      /* ！ */
/** @brief T9 分词键标签（引擎接线后续阶段；本阶段激活原地消费）。 */
#define XKEYBOARD_LBL_T9_SEG "\xE5\x88\x86\xE8\xAF\x8D"             /* 分词 */
/** @brief T9 重输键标签（引擎接线后续阶段；本阶段激活原地消费）。 */
#define XKEYBOARD_LBL_T9_RETYPE "\xE9\x87\x8D\xE8\xBE\x93"          /* 重输 */

/* ==================== 类定义 ==================== */
XCLASS_DEFINE_BEGING(XVirtualKeyboard)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboard, XWidget)

/**
 * @brief      XVirtualKeyboard 屏幕虚拟键盘对象；m_base 必须是第一个成员。
 * @details    字段含义：
 *             - m_target：输出目标编辑框（借用；可为 NULL）；
 *             - m_mode/m_popovers/m_autoPopup：布局模式、气泡开关、自
 *               动弹出开关（XGui 扩展）；m_layoutKind/m_shiftState：
 *               布局款型与款型 shift 展示态（Sogou 改版一阶段扩展）；
 *             - m_popped：弹层可见状态；
 *             - m_maps[9]/m_ctrls[9]：各模式文本/控制字槽位（借用指
 *               针，0..3 内置、4..7 用户默认回落小写表、末位 NULL）；
 *             - m_keyRects[64]/m_keyCtrls[64]/m_keyCount：当前布局的
 *               键位矩形、生效控制字（popovers=0 时剥 POPOVER 位）与
 *               按钮数（换行分隔符不计数）；
 *             - m_menuBarRect/m_pressedTool：键盘顶部菜单条矩形（常驻
 *               预留）与按下中工具栏槽位（Sogou 改版二阶段扩展）；
 *             - m_layoutSelectorOpen：键盘选择面板开合（Sogou 改版三
 *               阶段扩展，面板本地 UI 状态）；
 *             - m_editPanelOpen/m_editSelArmed：文字编辑面板开合与选
 *               择态（Sogou 改版三阶段收尾扩展，面板本地 UI 状态）；
 *             - m_compactFloat/m_compactDrag/m_dragOffX/m_dragOffY：紧
 *               凑悬浮态、拖移进行中与拖移锚点偏移（Sogou 改版四阶段
 *               扩展）；
 *             - m_selectedKey：最后激活按钮；m_pressedKey/m_pressArmed：
 *               按下中按钮与触发武装位（滑动出键取消、回滑恢复）；
 *             - m_repeatTimer/m_guardTimer：长按重复与守护轮询定时器
 *               （autoPopup 默认开，构造即常驻守护轮询；closePopup 仅
 *               在 autoPopup 关时随层停守护，autoPopup 开则轮询继续）；
 *             - m_host/m_hostW/m_hostH：弹层宿主顶层窗口与几何缓存；
 *             - m_targetConn/m_hostConn：防悬垂 destroyed 连接句柄；
 *             - m_prevMouseGrab/m_prevKbdGrab：popup 悬浮抢占前的抓取
 *               者快照（closePopup 成对归还后清；未收层重弹不覆盖）；
 *             - m_popupHeight：弹层高度缓存（守护重定位用）；
 *             - m_imeBandRect/m_imeEnabled（XVIRTUALKEYBOARD_ON 门
 *               控）：内嵌候选带矩形（控件局部坐标，h==0=无带）与拼
 *               音插件装载镜像（true=engine 已装 Pinyin 插件，默认
 *               false；状态机本体在插件，面板零直连）；
 *             - m_commitConn/m_keyEventConn/m_preeditConn
 *               （XVIRTUALKEYBOARD_ON）：context commitRequested/
 *               keyEventRequested/preeditTextChanged 连接句柄
 *               （setTextArea/popup 连接、closePopup 断开；preedit
 *               一路承载组串态停靠几何收缩事件源）；
 *             - m_prevFocus（XVIRTUALKEYBOARD_ON）：守护边沿判定的上
 *               一 tick 焦点采样（dismissFix：去 m_target 化）；
 *             - m_candidatePage/m_candidatePageSize
 *               （XVIRTUALKEYBOARD_ON）：候选带面板本地分页状态（0 基
 *               页号与页容量 [1,9]）。
 *             调用者不得手工修改字段；一律走公开 API。
 */
typedef struct XVirtualKeyboard
{
    XWidget m_base;                    /**< 基类成员；必须是第一个。 */
    XWidget* m_target;                 /**< 输出目标编辑框（借用；NULL=未绑定）。 */
    XKeyboardMode m_mode;              /**< 当前布局模式。 */
    XKeyboardLayoutKind m_layoutKind;  /**< 布局款型（默认 PinyinFull；
                                            主表装载进文本双槽位，见
                                            setLayoutKind）。 */
    int m_shiftState;                  /**< 布局层 shift 展示态（款型主
                                            表专用；0=小写、1=大写一次
                                            性、2=锁定，见
                                            XKEYBOARD_LBL_SHIFT）。 */
    bool m_imeChineseSaved;            /**< 中英态跨款型保存（进英文全键
                                            时快照、回拼音全键/九键时恢
                                            复，见 setLayoutKind——否则
                                            英文款型上点 中/EN 收敛回英
                                            文后切回中文款型仍残 English
                                            态，拼音键盘打字无组串无候
                                            选）。 */
    char m_t9TapDigit;                 /**< 九键英文多击当前数字组（'2'..'9'；
                                            0=无活动循环，见
                                            xkb_t9EnglishKey）。 */
    int m_t9TapIdx;                    /**< 九键英文多击组内循环位次。 */
    XTimerId m_t9TapTimer;             /**< 九键英文多击确认定时器（超时
                                            闭合循环；非周期）。 */
    bool m_popovers;                   /**< 按压放大气泡开关。 */
    bool m_autoPopup;                  /**< 自动弹出开关（XGui 扩展，默认 true）。 */
    bool m_popped;                     /**< 弹层可见状态。 */
    bool m_pressArmed;                 /**< 按下触发武装位（滑动出键即取消）。 */
    const char* const* m_maps[XKEYBOARD_MODE_SLOT_COUNT + 1];  /**< 各模式文本槽位（借用；末位 NULL）。 */
    const XKeyboardButtonCtrl* m_ctrls[XKEYBOARD_MODE_SLOT_COUNT + 1]; /**< 各模式控制字槽位（借用）。 */
    XRect m_keyRects[XKEYBOARD_MAX_BUTTONS];   /**< 键位矩形（控件局部坐标）。 */
    XKeyboardButtonCtrl m_keyCtrls[XKEYBOARD_MAX_BUTTONS]; /**< 生效控制字表。 */
    const char* m_keyLabels[XKEYBOARD_MAX_BUTTONS]; /**< 生效键帽表（借用，
                                        重建落表；buttonText 读此而非模式
                                        槽位——shift 逻辑态不重建布局）。 */
    uint32_t m_keyCount;               /**< 当前布局按钮数（换行分隔符不计数）。 */
    XRect m_menuBarRect;               /**< 键盘顶部菜单条矩形（控件局部坐
                                            标；键区上方常驻预留，条高=
                                            contentH/(rows+1)，height==0
                                            =未布局）。组串态=候选带矩形
                                            （候选替换菜单），空闲态=图标
                                            工具栏（四等分：悬浮切换/键
                                            盘选择/文字编辑/收起）。 */
    uint32_t m_selectedKey;            /**< 最后激活按钮；无则 XKEYBOARD_BUTTON_NONE。 */
    uint32_t m_pressedKey;             /**< 按下中按钮；无则 XKEYBOARD_BUTTON_NONE。 */
    int m_pressedTool;                 /**< 按下中工具栏槽位（0..3，按压武
                                            装释放触发；-1=无）。 */
    bool m_layoutSelectorOpen;         /**< 键盘选择面板开合（Sogou 改版
                                            三阶段；面板内部自绘覆盖键区，
                                            开启期面板外点击一律关闭并消
                                            费；随 closePopup/隐藏复位）。 */
    bool m_editPanelOpen;              /**< 文字编辑面板开合（Sogou 改版
                                            三阶段收尾；覆盖键区，与键盘
                                            选择面板互斥；随 closePopup/
                                            隐藏复位）。 */
    bool m_editSelArmed;               /**< 文字编辑面板选择态（「开始选
                                            择」切换；方向/Home/End 合成
                                            键携带 ShiftModifier；面板关
                                            闭复位）。 */
    bool m_compactFloat;               /**< 紧凑悬浮态（第三形态；工具栏
                                            「悬浮切换」切换；独立顶层
                                            Popup 宽 min(宿主宽45%,420)
                                            高同比例、宿主右下角；守卫
                                            xkb_reposition 几何跟随跳过；
                                            closePopup/popup 复位）。 */
    bool m_compactDrag;                /**< 紧凑态拖移进行中（按住工具栏
                                            空白启动；move 求 delta 移窗；
                                            release 结束）。 */
    bool m_physKeyActive;              /**< 物理键盘组串会话（需求②③输入
                                            源判据，2026-10-04 用户澄清：
                                            「英文态直落收层/中文态收缩
                                            只留候选带」仅限外置键盘输入
                                            ，屏幕键盘输入无此要求）——
                                            notifyKey 中文路由消费置位、
                                            组串结束（preedit 空）/收层/
                                            重弹复位；屏幕键组串永不收缩
                                            不收层。 */
    int m_dragOffX;                    /**< 拖移锚点偏移（按下时全局锚-
                                            窗口左上 x）。 */
    int m_dragOffY;                    /**< 拖移锚点偏移（按下时全局锚-
                                            窗口左上 y）。 */
    XTimerId m_repeatTimer;            /**< 长按重复定时器（XTIMER_INVALID_ID=未启动）。 */
    XTimerId m_guardTimer;             /**< 守护轮询定时器（XTIMER_INVALID_ID=未启动）。 */
    XWidget* m_host;                   /**< 弹层宿主顶层窗口（借用；NULL=未挂载）。 */
    bool m_userCollapsed;              /**< 用户收起闩锁（收起键/确认后置位：
                                            守护轮询不再自动重弹，直到用户
                                            再次按下编辑框；防「收起即弹回」）。 */
    int m_hostW;                       /**< 宿主宽度缓存（reposition 工作变量）。 */
    int m_hostH;                       /**< 宿主高度缓存（reposition 工作变量）。 */
    XConnection* m_geomConn[8];        /**< 宿主/范围顶层几何信号连接句柄（借用；
                                            [0..3]=宿主 x/y/width/height 变化、
                                            [4..7]=对话框浮层形态范围顶层同序——
                                            2026-10-03 所有者裁定「几何跟随走事
                                            件推送不走轮询」：WM 拖拽/缩放经
                                            ConfigureNotify→XWindow_setGeometry-
                                            Fields 发射逐字段信号，Queued 连接
                                            投递 XMetaCallEvent 事件循环异步执
                                            行（2026-10-03 所有者裁定「投递函数
                                            事件」），reposition 几何短路聚合；
                                            closePopup/宿主亡清。 */
    XWidget* m_geomScope;              /**< 几何信号连接中的范围顶层（对话框浮层
                                            形态；借用，断连清扫用）。 */
    XTimerId m_geomSyncTimer;          /**< 几何重排聚合定时器（30ms 单发；拖拽/
                                            缩放的高频信号槽内只置 pending，同
                                            一批次聚合为一次 reposition——对齐
                                            主窗重绘节奏防「键盘先窜出」观感，
                                            拖动停止 ≤30ms 必达，非周期轮询）。 */
    XWidget* m_hostOverride;           /**< 悬浮模式宿主锚（借用；NULL=内嵌
                                            挂父模式）。可传弹层容器（Popup
                                            型顶层），popup() 时自动解析主
                                            窗口为实际锚；closePopup 自动
                                            清空。 */
    XWidget* m_hostHint;               /**< 悬浮锚解析提示（借用；setHost
                                            Window 时刻的焦点顶层快照=弹层
                                            打开前用户所在主窗口，transient
                                            parent 语义；closePopup 清空）。 */
    bool m_floating;                   /**< 当前弹层=独立顶层悬浮形态（悬浮
                                            锚生效期弹出；closePopup 复位）。 */
    XConnection* m_targetConn;         /**< 目标 destroyed 防悬垂连接句柄（借用）。 */
    XConnection* m_hostConn;           /**< 宿主 destroyed 防悬垂连接句柄（借用）。 */
    XWidget* m_prevMouseGrab;          /**< popup 悬浮抢占前的鼠标抓取者快照
                                            （借用；closePopup 归还后清）。 */
    XWidget* m_prevKbdGrab;            /**< popup 悬浮抢占前的键盘抓取者快
                                            照（借用；closePopup 归还后清）。 */
    int m_popupHeight;                 /**< 弹层高度缓存（守护重定位复用）。 */
    XWidget* m_prevFocus;              /**< 守护边沿上一焦点采样（dismissFix；
                                            无条件成员——三段边沿判据在
                                            XVK=0 时仍承载 WA14 边沿化）。 */
#if XVIRTUALKEYBOARD_ON
    XRect m_imeBandRect;               /**< 拼音候选带矩形（控件局部坐标；
                                            height==0 表示无带）。Sogou 改
                                            版二阶段起组串态时=菜单条矩形
                                            （候选带替换菜单渲染在条内），
                                            空闲态归零（条渲染工具栏）。 */
    bool m_imeEnabled;                 /**< 拼音插件装载镜像（true=engine
                                            已装 Pinyin 插件；默认 false）。 */
    XConnection* m_commitConn;         /**< commitRequested 落地契约连接
                                            （借用；NULL=未连接）。 */
    XConnection* m_keyEventConn;       /**< keyEventRequested 落地契约连接
                                            （借用；NULL=未连接）。 */
    XConnection* m_preeditConn;        /**< preeditTextChanged 组串收缩连
                                            接（借用；NULL=未连接）——
                                            组串态停靠几何翻转事件源。 */
    int m_candidatePage;               /**< 候选带当前页（面板本地，0 基）。 */
    int m_candidatePageSize;           /**< 候选带页容量（面板本地，[1,9]）。 */
#endif
} XVirtualKeyboard;

/* ==================== 生命周期 ==================== */

/**
 * @brief      初始化类虚函数表并返回共享表指针。
 * @return     类共享虚函数表指针（进程期常驻，借用）；不失败。
 */
XVtable* XVirtualKeyboard_class_init(void);
/**
 * @brief      初始化键盘控件（栈对象路径；须先备妥对象内存）。
 * @details    初始化前提：self 为未初始化（或已 deinit）的内存块，由
 *             调用者提供存储；初始化后成对调用 XClassDeinit
 *             释放内部资源（定时器/连接/弹层）。
 * @param      self 待初始化键盘对象；不能为 NULL。
 * @param      parent 父控件借用指针；可为 NULL（无宿主，弹层挂顶层）。
 * @param      flags 控件标志（可按位组合）。
 * @return     无返回值；self 为 NULL 时函数不执行任何操作。
 */
void XVirtualKeyboard_init(XVirtualKeyboard* self, XWidget* parent, XWidgetFlags flags);
/**
 * @brief      创建堆上键盘对象（XVirtualKeyboard_create 宏的主实现；
 *             对标初始化前提与释放方式：堆对象用后须调
 *             XClassDelete 回收）。
 * @param      memory 对象内存类型（XMemoryType）。
 * @param      parent 父控件借用指针；可为 NULL。
 * @param      flags 控件标志（可按位组合）。
 * @return     新建键盘对象指针，调用方负责 XClassDelete
 *             释放；分配失败返回 NULL。
 */
#define XVirtualKeyboard_create(parent, flags) XVirtualKeyboard_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))
XVirtualKeyboard* XVirtualKeyboard_create_ex(XMemoryType memory, XWidget* parent, XWidgetFlags flags);

/* ==================== 父类 XWidget API 宏转发 ==================== */

#define XVirtualKeyboard_setEnabled(self, enabled) XWidget_setEnabled((XWidget*)(self), (enabled))
#define XVirtualKeyboard_isEnabled(self) XWidget_isEnabled((const XWidget*)(self))
#define XVirtualKeyboard_setVisible(self, visible) XWidget_setVisible((XWidget*)(self), (visible))
#define XVirtualKeyboard_isVisible(self) XWidget_isVisible((const XWidget*)(self))
#define XVirtualKeyboard_show(self) XWidget_show((XWidget*)(self))
#define XVirtualKeyboard_hide(self) XWidget_hide((XWidget*)(self))
#define XVirtualKeyboard_raise(self) XWidget_raise((XWidget*)(self))
#define XVirtualKeyboard_setGeometry(self, x, y, w, h) XWidget_setGeometry((XWidget*)(self), (x), (y), (w), (h))
#define XVirtualKeyboard_setGeometryRect(self, rect) XWidget_setGeometryRect((XWidget*)(self), (rect))
#define XVirtualKeyboard_x(self) XWidget_x((const XWidget*)(self))
#define XVirtualKeyboard_y(self) XWidget_y((const XWidget*)(self))
#define XVirtualKeyboard_width(self) XWidget_width((const XWidget*)(self))
#define XVirtualKeyboard_height(self) XWidget_height((const XWidget*)(self))
#define XVirtualKeyboard_resize(self, w, h) XWidget_resize((XWidget*)(self), (w), (h))
#define XVirtualKeyboard_update(self) XWidget_update((XWidget*)(self))
#define XVirtualKeyboard_setParent(self, parent, flags) XWidget_setParent((XWidget*)(self), (parent), (flags))
#define XVirtualKeyboard_parentWidget(self) XWidget_parentWidget((const XWidget*)(self))

/* ==================== 绑定与布局属性（对标 lv_keyboard public API） ==================== */

/**
 * @brief      绑定输出目标编辑框（对标 lv_keyboard_set_textarea）。
 * @details    仅存借用指针，不改任何焦点、不进任何焦点组；目标为 NULL
 *             表示解绑。非受支持类型（XLineEdit/XPlainTextEdit/XTextEdit
 *             之外的 vtable）拒绝并 XPrintf 诊断、保持原绑定；合成键路
 *             径本身类型无关，类型识别仅用于直写降级路径。绑定时对
 *             target 挂 XObject_destroyed_signal 防悬垂连接（销毁即
 *             m_target=NULL 并 closePopup），解绑/换绑时断开旧连接。
 * @param      self 目标键盘；可为 NULL。
 * @param      target 输出目标（借用）；可为 NULL 解绑。
 * @return     无返回值。
 */
void XVirtualKeyboard_setTextArea(XVirtualKeyboard* self, XWidget* target);
/**
 * @brief      查询输出目标编辑框（对标 lv_keyboard_get_textarea）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     借用指针；未绑定或 self 为 NULL 返回 NULL。
 */
XWidget* XVirtualKeyboard_textArea(const XVirtualKeyboard* self);
/**
 * @brief      设置布局模式并重建键位（对标 lv_keyboard_set_mode）。
 * @details    模式相同直接返回；否则按槽位表重建键位矩形并请求重绘。
 * @param      self 目标键盘；可为 NULL。
 * @param      mode 目标模式（XKeyboardMode）。
 * @return     无返回值。
 */
void XVirtualKeyboard_setMode(XVirtualKeyboard* self, XKeyboardMode mode);
/**
 * @brief      查询布局模式（对标 lv_keyboard_get_mode）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     当前模式；self 为 NULL 返回 TextLower。
 */
XKeyboardMode XVirtualKeyboard_mode(const XVirtualKeyboard* self);
/**
 * @brief      设置布局款型并装载款型主表（Sogou 改版一阶段；XGui 扩展）。
 * @details    款型层独立于 XKeyboardMode：装载动作只覆写 TextLower/
 *             TextUpper 双槽位（PinyinFull/EnglishFull 装搜狗全键大小
 *             写双表——EnglishFull 与 PinyinFull 同几何共表，固定英文
 *             态由组串路由/候选带判据的款型分支承载；PinyinT9 双槽位
 *             同装九键表），Special/Number/User1..4/Digits/Dialpad 槽
 *             位与 hints 自动切换机制完全不动（既有模式机制保持不变，
 *             现状可恢复）。装载后：shift 展示态复位、TextUpper 读数
 *             归位 TextLower、组串草稿弃置（engine reset）、重建键位。
 *             当前处于 Special/Number/User1 等其它模式时模式保持、仅
 *             换表（回到文本模式即见新款型主表）；此后 setMap 仍可按
 *             槽位覆写（后设者胜，至下次款型切换再被覆盖）。款型相同
 *             直接返回；越界款型拒绝。
 * @param      self 目标键盘；可为 NULL。
 * @param      kind 目标款型（XKeyboardLayoutKind）。
 * @return     无返回值。
 */
void XVirtualKeyboard_setLayoutKind(XVirtualKeyboard* self,
                                    XKeyboardLayoutKind kind);
/**
 * @brief      查询布局款型（Sogou 改版一阶段；XGui 扩展）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     当前款型；self 为 NULL 返回 PinyinFull。
 */
XKeyboardLayoutKind XVirtualKeyboard_layoutKind(const XVirtualKeyboard* self);
/**
 * @brief      启停按压放大气泡（对标 lv_keyboard_set_popovers）。
 * @details    状态不变直接返回；否则重建生效控制字表（popovers=0 剥
 *             全部 POPOVER 位）并请求重绘。注意：重建以 setMap 装入的
 *             槽位控制字为源，此前经 setButtonCtrl 的单键覆写会被重置
 *             （对标 LVGL set_popovers 重建 ctrl 映射的行为）。
 * @param      self 目标键盘；可为 NULL。
 * @param      enabled true 启用气泡，false 关闭（默认）。
 * @return     无返回值。
 */
void XVirtualKeyboard_setPopovers(XVirtualKeyboard* self, bool enabled);
/**
 * @brief      查询气泡开关（对标 lv_keyboard_get_popovers）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     启用返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboard_popovers(const XVirtualKeyboard* self);
/**
 * @brief      用自定义映射替换指定模式槽位（对标 lv_keyboard_set_map）。
 * @details    接入其它字符集/输入法布局的唯一官方途径。map/ctrl 生命
 *             周期归调用方且须存活到下次 setMap（借用指针，不拷贝）。
 *             表以 NULL 数组尾或 "" 按钮终止；"\n" 为换行分隔符（不占
 *             按钮索引）；按钮数超 XKEYBOARD_MAX_BUTTONS 整体拒绝（保
 *             持原布局，XPrintf 诊断）。mode 为当前模式时立即重建键位。
 * @param      self 目标键盘；可为 NULL。
 * @param      mode 目标模式槽位（0..7）。
 * @param      map 按钮文本映射（UTF-8 借用指针数组）；NULL 拒绝。
 * @param      ctrlMap 按钮控制字映射（与 map 按钮一一对应，不含换行
 *             分隔符；可为 NULL，全部按宽度 1 处理）。
 * @return     无返回值。
 */
void XVirtualKeyboard_setMap(XVirtualKeyboard* self, XKeyboardMode mode,
                      const char* const* map,
                      const XKeyboardButtonCtrl* ctrlMap);

/* ==================== 布局查询（对标 lv_buttonmatrix 经 keyboard 暴露面） ==================== */

/**
 * @brief      查询当前生效模式的按钮文本映射（对标
 *             lv_keyboard_get_map_array）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     文本映射借用指针（UTF-8；NULL 数组尾）；self 为 NULL 返
 *             回 NULL。不得释放或修改。
 */
const char* const* XVirtualKeyboard_mapArray(const XVirtualKeyboard* self);
/**
 * @brief      查询当前布局按钮数（换行分隔符不计数；对标
 *             lv_buttonmatrix_get_button_cnt）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     按钮数；self 为 NULL 返回 0。
 */
uint32_t XVirtualKeyboard_buttonCount(const XVirtualKeyboard* self);
/**
 * @brief      查询最后激活按钮（对标 lv_buttonmatrix_get_selected_button）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     按钮索引；无则 XKEYBOARD_BUTTON_NONE。
 */
uint32_t XVirtualKeyboard_selectedButton(const XVirtualKeyboard* self);
/**
 * @brief      查询按钮文本（对标 lv_keyboard_get_button_text）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @param      buttonId 按钮索引（换行分隔符不占索引）。
 * @return     文本借用指针（UTF-8）；越界或 self 为 NULL 返回 NULL。
 */
const char* XVirtualKeyboard_buttonText(const XVirtualKeyboard* self, uint32_t buttonId);
/**
 * @brief      查询按钮生效控制字（对标 lv_buttonmatrix_get_button_ctrl）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @param      buttonId 按钮索引。
 * @return     生效控制字（含 popovers 剥离与 setButtonCtrl 覆写）；
 *             越界或 self 为 NULL 返回 0。
 */
XKeyboardButtonCtrl XVirtualKeyboard_buttonCtrl(const XVirtualKeyboard* self,
                                         uint32_t buttonId);
/**
 * @brief      单键控制字覆写（对标 lv_buttonmatrix_set_button_ctrl）。
 * @details    宽度位与标志位一并生效并立即重建键位布局。该覆写写在
 *             生效表上，setPopovers 重建时会被槽位源表重置。
 * @param      self 目标键盘；可为 NULL。
 * @param      buttonId 按钮索引；越界忽略。
 * @param      ctrl 目标控制字。
 * @return     无返回值。
 */
void XVirtualKeyboard_setButtonCtrl(XVirtualKeyboard* self, uint32_t buttonId,
                             XKeyboardButtonCtrl ctrl);

/* ==================== 按键语义复用（对标 lv_keyboard_def_event_cb） ==================== */

/**
 * @brief      按内置语义处理指定按钮（可复用入口，对标
 *             lv_keyboard_def_event_cb）。
 * @details    控制键文本匹配（"abc"/"ABC"/"1#"/确认/关闭/收起/换行/
 *             "Enter"/<-/->/退格/+/-）与字符写入；自定义布局/自定义
 *             接收方先自行处理、未消费的回落本函数复用按键语义。
 * @param      self 目标键盘；可为 NULL。
 * @param      buttonId 按钮索引；越界返回 false。
 * @return     已识别处理返回 true；按钮为空标签或参数无效返回 false。
 */
bool XVirtualKeyboard_handleButton(XVirtualKeyboard* self, uint32_t buttonId);

/* ==================== 弹层（XGui 扩展，对标 XComboBox popup 命名） ==================== */

/**
 * @brief      把键盘挂到编辑框顶层窗口底部弹出（XGui 扩展，对标
 *             XComboBox_showPopup 命名）。
 * @details    等价 setTextArea(editor) 绑定后：父=XWidget_topLevelWidget
 *             (editor)，几何=setGeometry(0, hostH-kbH, hostW, kbH)（宽
 *             铺满宿主、贴底），show+raise；键盘自身 NoFocus 不抢焦点
 *             （焦点保持/交还 editor，物理键入直达编辑框）。高度钳位
 *             次序：kbH=hostH/2 → 下界 XKEYBOARD_POPUP_MIN_H(120) →
 *             上界 hostH-XKEYBOARD_POPUP_MARGIN_H(32)（上界最终生效，
 *             恒给编辑区留 32px）。悬浮模式（setHostWindow 锚生效）：
 *             跳过挂父，键盘保持独立顶层窗口（锚=Popup 型弹层容器时
 *             自动解析主窗口顶层），几何=锚全局坐标
 *             (gx, gy+hostH-kbH, hostW, kbH)——外观/尺寸/位置与普通
 *             编辑框弹出的内嵌形态完全一致，仅以顶层悬浮并 raise 压过
 *             日历弹层等一切同应用窗口；随后抢占弹层模态双抓取并接管
 *             原生鼠标捕获（弹层 show 即 grabMouse/grabKeyboard+1ms 后
 *             原生 SetCapture，悬浮面板不夺取则键面点击被跨顶层抓取
 *             改道/原生捕获吞掉）。启动守护轮询定时器。重复调用按新
 *             编辑框重定位（多编辑框跟随重绑）。面板构造即隐藏（init
 *             对非窗口子控件形态补置 WState_Hidden，顶层构造已
 *             Hidden）：首次弹出前不参与页面绘制与命中，弹出经本接口
 *             show 唤起后键面与候选带绘制/命中照旧。
 * @param      self 目标键盘；可为 NULL。
 * @param      editor 输入编辑框（借用）；NULL 无操作。
 * @return     无返回值。
 */
void XVirtualKeyboard_popup(XVirtualKeyboard* self, XWidget* editor);
/**
 * @brief      收起弹层（隐藏 + 停长按重复定时器 + 解除目标绑定）。
 * @details    隐藏为无条件 setVisible(false)（不以 isVisible 为前置）：
 *             pre-show 收层（宿主未 show）同样补置 WState_Hidden，杜
 *             绝弹层 show 位残留、宿主 show 后整块画出的残影；已可见
 *             面板行为不变。悬浮面板（setHostWindow 锚生效期弹出）收
 *             层先释放双抓取与原生鼠标捕获再隐藏，保持顶层归属（不归
 *             还主窗口）并自动清空悬浮锚（回内嵌模式）。守护轮询按运
 *             行条件保留：autoPopup 开时常驻轮询继续（兜底）；autoPopup
 *             关时守护随层停。收层同时解除编辑框绑定（m_target 置空、
 *             防悬垂连接断开）。
 *             弹收主判据=按下位置驱动（notifyPress）：收起后再按受支
 *             持编辑框（含同框，无需焦点往返）即经按下路径重弹——旧
 *             dismissFix「同框 retap 不重弹」契约已作废（反直觉：同框
 *             唤回只能靠焦点往返）；守护边沿仅为焦点跟随换框兜底。
 *             对标 XComboBox_hidePopup/XCompleter 收层形态（焦点不归
 *             还）。
 * @param      self 目标键盘；可为 NULL。
 * @return     无返回值。
 */
void XVirtualKeyboard_closePopup(XVirtualKeyboard* self);
/**
 * @brief      查询弹层弹出状态（对标 XComboBox_popupVisible 的弹出位语义）。
 * @details    返回 m_popped 生命周期位：popup() 成功路径置位、
 *             closePopup() 复位——与「挂接是否生效」严格同步，而非
 *             XWidget_isVisible 的生效可见。根因：面板是宿主顶层窗口的
 *             子控件浮层（无独立 OS 窗口），宿主未 show 时生效可见按
 *             父链口径恒假，apitest 直呼 popup 与 demo 宿主首显前的
 *             无头钩子会把挂接成功误报为未弹。收层须走 closePopup
 *             （同步复位本状态；直接 setVisible 不在契约内）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     弹层处于弹出状态返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboard_popupVisible(const XVirtualKeyboard* self);
/**
 * @brief      设置悬浮模式宿主锚（XGui 扩展；独立顶层输入面板宿主）。
 * @details    host 非空进入悬浮模式：随后 popup() 不再把键盘挂父为编辑
 *             框顶层窗口的子控件浮层，而是保持独立顶层窗口形态，以全局
 *             坐标悬浮在 host 客户区底部（宽=host 全宽、高与内嵌形态
 *             同一钳位公式——外观/尺寸/位置与普通编辑框弹出完全一致），
 *             show+raise 置顶压过日历弹层等一切同应用窗口。host 允许
 *             直接传弹层容器（日历弹层内编辑器场景）：弹层容器是
 *             Popup 型独立顶层且 widget 父链/transient parent 均无主窗
 *             口反向引用，popup() 时按可信次序解析主窗口为实际锚——
 *             ①本接口时刻的焦点顶层快照（契约时序=布锚先于落焦，快照
 *             即弹层打开前用户所在主窗口，transient parent 语义）；
 *             ②上一次内嵌弹层的宿主顶层（普通编辑框键盘的记忆宿主）；
 *             ③应用顶层表可见非 Popup 顶层中客户面积最大者（g_xapp 存
 *             在的多窗口应用）；全缺回落锚自身（键盘仍悬浮压过弹层）。
 *             防悬垂 destroyed 连接照旧挂最终锚，宿主销毁路径不变；悬
 *             浮期守护除 Resize 外还复核锚全局位移（主窗口拖动→跟随
 *             重定位）。closePopup 自动清空锚与快照回内嵌模式。须在
 *             popup()/焦点驱动自动弹出之前调用。host 为 NULL 等价清
 *             除。
 * @param      self 目标键盘；可为 NULL。
 * @param      host 悬浮锚顶层窗口/弹层容器（借用）；NULL 清除。
 * @return     无返回值。
 */
void XVirtualKeyboard_setHostWindow(XVirtualKeyboard* self, XWidget* host);
/**
 * @brief      查询悬浮模式宿主锚（XGui 扩展）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     悬浮锚借用指针（设置时的原值，未经解析）；未设置或
 *             self 为 NULL 返回 NULL。
 */
XWidget* XVirtualKeyboard_hostWindow(const XVirtualKeyboard* self);
/**
 * @brief      设置自动弹出开关（XGui 扩展；默认 true）。
 * @details    on 时自动弹收机制随行：主判据=按下位置驱动（notifyPress，
 *             指针 PRESS 命中受支持编辑框弹/重绑、非编辑收），守护定
 *             时器常驻轮询作焦点跟随换框兜底（应用焦点落在受支持编辑
 *             框即自动 popup/跟随重绑；经守护轮询实现；XGui 事件过滤
 *             器对本控件全部关键事件不可达，机制与 XCompleter 守护先
 *             例一致）。守护在构造时即随默认值 true 常驻启动（无事件
 *             调度器环境启动失败时由本接口重试）；值未变时重复调用亦
 *             会补启缺席的守护（自愈）。off 时按下路径与守护轮询一并
 *             停（notifyPress 直接返回），应用显式 popup/closePopup 全
 *             权接管。closePopup 不停 autoPopup 开的守护。
 * @param      self 目标键盘；可为 NULL。
 * @param      on true 开启自动弹出，false 关闭。
 * @return     无返回值。
 */
void XVirtualKeyboard_setAutoPopup(XVirtualKeyboard* self, bool on);
/**
 * @brief      查询自动弹出开关（XGui 扩展）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     开启返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboard_autoPopup(const XVirtualKeyboard* self);
/**
 * @brief      按下位置驱动通知（XGui 扩展；自动弹收主判据，标准触摸 UX）。
 * @details    由 XWidget_dispatchPointerEvent 的 PRESS 汇聚点经
 *             XGuiApplication_virtualKeyboardNotifyPress 转发调用（默认
 *             面板单例；XWidget 核心不依赖键盘类型）。契约：
 *             - hit 为键盘面板自身（或其子控件）→ 无操作（键盘自身按
 *               键 PRESS 不受影响，弹出态按键照常输入）；
 *             - hit 为受支持编辑框且 isEnabled && WA_InputMethodEnabled
 *               && 总开关开（判据与守护 tick accept 全等）→ popup(hit)
 *               弹出/重绑；已弹且同框则保持（不重放 hints，保住面板内
 *               手动模式切换）；
 *             - 其余（页面空白/按钮/标签等非编辑区域）→ 弹层可见则
 *               closePopup（dismiss 语义：组串弃置等副作用随行）。
 *             无头/合成环境无指针事件即不触发，显式 popup 直呼契约不
 *             变。门控：XVIRTUALKEYBOARD_DESKTOP_ON=0（守护同停的非桌
 *             面形态）整段空实现；autoPopup=false 时无操作（应用全权
 *             接管）。可在任意线程语境直呼（幂等，守护 200ms 兜底与
 *             按下即时并存无冲突）。
 * @param      self 目标键盘；可为 NULL。
 * @param      hit 指针 PRESS 命中的控件（childAt 结果，借用）；可为
 *             NULL（顶层空白回退形态，按非编辑处理）。
 * @return     无返回值。
 */
void XVirtualKeyboard_notifyPress(XVirtualKeyboard* self, XWidget* hit);

/**
 * @brief      物理按键转发（XGui 扩展；屏幕键盘弹出时的按键转化层）。
 * @details    分派入口（XWidget_dispatchKeyEvent）在按键进焦点控件之前
 *             调用：面板未弹恒 false（零开销透传）；面板弹出时——组串
 *             中数字 1..9=选对应编号候选（与候选 chip 编号角标同源映
 *             射，全量下标=页基×页容量+位序）；字母/空格/退格/回车转
 *             虚键喂引擎（拼音组串态进组串链，与点按屏幕键同路径）；
 *             引擎未消费（英文态打字等）返回 false 放行按键照常进编
 *             辑框。返回 true 表示按键已被键盘转化层消费，分派终止。
 * @param      self 键盘对象；NULL 返回 false。
 * @param      key  键值（XKey_*；ASCII 字母为小写/大写码位）。
 * @param      modifiers 修饰键（Shift 决定大写直写或组串小写归一）。
 * @return     按键已消费返回 true；放行返回 false。
 */
bool XVirtualKeyboard_notifyKey(XVirtualKeyboard* self, int key,
                                XKeyboardModifiers modifiers);

/* ==================== 拼音输入（XGui 扩展，XVIRTUALKEYBOARD_ON 门控；
 *                       薄委托引擎/插件） ==================== */
#if XVIRTUALKEYBOARD_ON

#if XKEYBOARD_IME_ON
/**
 * @brief      拼音布局文本表取用（44 键 5 行静态借用表）。
 * @details    供 setImeEnabled 装 User1 槽位（XPinyinEngine.c 内置静
 *             态存储，满足 setMap 借用生命周期契约）；NULL 数组尾终
 *             止、"\n" 行分隔，与 setMap 解析协议一致。
 * @return     文本映射借用指针（UTF-8；不得释放或修改）。
 */
const char* const* XPinyinEngine_map(void);
/**
 * @brief      拼音布局控制字表取用（与 XPinyinEngine_map() 一一对应）。
 * @return     控制字映射借用指针（不得释放或修改）。
 */
const XKeyboardButtonCtrl* XPinyinEngine_ctrlMap(void);
#endif /* XKEYBOARD_IME_ON */

/**
 * @brief      启停拼音 IME（XGui 扩展；薄委托）。
 * @details    on=true：engine 装配 Pinyin 插件（工厂创建，XPinyinEngine
 *             实例=插件独占）、engine.setInputMode(Pinyin)（中文态默
 *             认 true）、装载内置拼音布局至 User1 槽位并切至 User1，
 *             触发词组库懒加载（XKEYBOARD_IME_PHRASE_ON 门控；幂等+
 *             负结果粘滞）；on=false：engine 装配回 Plain 插件并回
 *             TextLower。不恢复用户原 User1 表（槽位占用约定：后设者
 *             胜）。值未变时无操作。
 * @param      self 目标键盘；可为 NULL。
 * @param      on true 启用拼音 IME，false 关闭。
 * @return     设置成功返回 true；self 为 NULL 或 XVIRTUALKEYBOARD_ON=0
 *             返回 false。
 */
bool XVirtualKeyboard_setImeEnabled(XVirtualKeyboard* self, bool on);
/**
 * @brief      查询拼音 IME 总开关（XGui 扩展；=Pinyin 插件已装配镜像）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     启用返回 true；默认关闭（false）。
 */
bool XVirtualKeyboard_imeEnabled(const XVirtualKeyboard* self);
/**
 * @brief      中/英切换（XGui 扩展；薄委托 engine.setInputMode）。
 * @details    等价点「中/EN」键：engine.setInputMode(Pinyin/Latin)（切
 *             换时插件清组串），重建布局（候选带显隐随中文态）。
 * @param      self 目标键盘；可为 NULL。
 * @param      chinese true 切中文态，false 切英文态。
 * @return     设置成功返回 true；IME 未启用、self 为 NULL 或
 *             XVIRTUALKEYBOARD_ON=0 返回 false。
 */
bool XVirtualKeyboard_setImeChinese(XVirtualKeyboard* self, bool chinese);
/**
 * @brief      查询中文态（XGui 扩展；=engine 当前输入模式为 Pinyin）。
 * @param      self 键盘对象借用指针；可为 NULL。
 * @return     中文态返回 true；IME 关闭或 self 为 NULL 返回 false。
 */
bool XVirtualKeyboard_imeChinese(const XVirtualKeyboard* self);

#endif /* XVIRTUALKEYBOARD_ON */

/* ==================== 信号（纯 ID getter，绝不在此发射） ==================== */

/**
 * @brief      ready() 信号标识（确认键；对标 LV_EVENT_READY）。
 * @details    纯 ID getter（库惯例：忽略 self 返回信号标识），绝不在此
 *             发射。连接：XObject_connect_1((XObject*)kb,
 *             (size_t)XVirtualKeyboard_ready_signal(NULL), receiver, slot,
 *             XConnectionType_Direct)。确认键不自动收层（对标 LVGL 默
 *             认无动作），应用一行接线 XVirtualKeyboard_closePopup。
 * @param      self 键盘对象借用指针；可为 NULL（被忽略）。
 * @return     不透明的信号标识；不得解引用或释放。
 */
void* XVirtualKeyboard_ready_signal(XVirtualKeyboard* self);
/**
 * @brief      cancel() 信号标识（关闭/收起键；对标 LV_EVENT_CANCEL）。
 * @details    纯 ID getter。发射后弹层可见即自动收层（XGui 扩展行为，
 *             非 LVGL 语义）。
 * @param      self 键盘对象借用指针；可为 NULL（被忽略）。
 * @return     不透明的信号标识。
 */
void* XVirtualKeyboard_cancel_signal(XVirtualKeyboard* self);
/**
 * @brief      buttonActivated(int32_t buttonId) 信号标识（任意按钮激
 *             活；对标按钮矩阵 VALUE_CHANGED，参数为按钮 id）。
 * @details    纯 ID getter。自定义布局/IME 挂点：读取侧
 *             XVarList_args_1(args, int32_t, buttonId)。
 * @param      self 键盘对象借用指针；可为 NULL（被忽略）。
 * @param      buttonId 仅占位（本 getter 不读取）。
 * @return     不透明的信号标识。
 */
void* XVirtualKeyboard_buttonActivated_signal(XVirtualKeyboard* self, int32_t buttonId);

#endif /* XWIDGET_ON && XKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARD_H */
