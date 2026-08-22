#ifndef XEVENT_H
#define XEVENT_H
#ifdef __cplusplus
extern "C" {
#endif
#include<stdio.h>
#include<stdint.h>
#include<stdbool.h>
#include"XClass.h"
#include"XTypes.h"
#include"XEventType.h"
#include"XSignalSlot.h"
#include"XAtomic.h"
#include"XSocketDescriptor.h"
#include"XGeometry.h"
typedef struct XThreadData XThreadData;
// 事件回调函数类型
typedef void (*XEventCB)(XEvent* event);

XCLASS_DEFINE_BEGING(XEvent)
XCLASS_DEFINE_ENUM(XEvent, SetAccepted) = XCLASS_VTABLE_GET_SIZE(XClass),
XCLASS_DEFINE_ENUM(XEvent, Clone),
XCLASS_DEFINE_END(XEvent)
//事件基类
typedef struct XEvent
{
    XClass m_class;
    int type;                     //事件类型代码
    // 标志位（16 bits）
    uint16_t reserved : 10;//预留未来扩展
    uint16_t accepted : 1; //接受事件
    uint16_t spontaneous:1;             // 是否为自发事件(非用户触发)
    uint16_t posted:1;                    // 1 是否来自事件队列
    uint16_t input_event : 1;//是否是输入事件（键盘、鼠标、触摸等）
    uint16_t pointer_event : 1;//是否是指针设备事件（鼠标、触控笔）
    uint16_t single_point_event : 1;//是否是单点事件（区别于多点触控）
}XEvent;
XVtable* XEvent_class_init();
/**
 * @brief 创建基础事件
 * @param code 事件类型，可使用 XEvent_registerEventType 返回的自定义类型。
 * @return 新创建的基础事件；内存分配失败时返回 NULL。
 * @note 返回对象由调用者拥有，成功投递到事件队列后转移所有权。
 */
XEvent* XEvent_create_ex(XMemoryType memory,  XEventType code);
/**
 * @brief 初始化基础事件
 * @param event 调用者提供的未初始化事件存储，不可为 NULL。
 * @param type 事件类型。
 */
void XEvent_init(XEvent* event, XEventType type);
#define XEvent_deinit_base                           XClass_deinit_base
#define XEvent_delete_base                           XClass_delete_base
#define XEvent_DataPtr(event)                   (&(((XEvent*)event)->data))
#define XEvent_Data(event,dataType)             (*((dataType*)XEvent_DataPtr(event)))

/**
 * @brief 接受事件，标记为已处理
 * @param event 事件对象指针，不可为 NULL
 */
void XEvent_accept(XEvent* event);

/**
 * @brief 克隆事件（仅基类部分）
 * @param event 要克隆的事件对象指针，不可为 NULL
 * @return 返回新分配的事件副本；失败时返回 NULL
 */
XEvent* XEvent_clone_base(const XEvent* event);

/**
 * @brief 忽略事件，标记为未处理
 * @param event 事件对象指针，不可为 NULL
 */
void XEvent_ignore(XEvent* event);

/**
 * @brief 判断事件是否已被接受
 * @param event 事件对象指针，不可为 NULL
 * @return 若事件已被接受则返回 true，否则返回 false
 */
bool XEvent_isAccepted(const XEvent* event);

/**
 * @brief 判断事件是否为输入事件（如键盘、鼠标、触摸等）
 * @param event 事件对象指针，不可为 NULL
 * @return 是输入事件则返回 true，否则返回 false
 */
bool XEvent_isInputEvent(const XEvent* event);

/**
 * @brief 判断事件是否为指针事件（如鼠标或触摸点相关事件）
 * @param event 事件对象指针，不可为 NULL
 * @return 是指针事件则返回 true，否则返回 false
 */
bool XEvent_isPointerEvent(const XEvent* event);

/**
 * @brief 判断事件是否为单点事件（仅包含一个触点或指针位置）
 * @param event 事件对象指针，不可为 NULL
 * @return 是单点事件则返回 true，否则返回 false
 */
bool XEvent_isSinglePointEvent(const XEvent* event);

/**
 * @brief 设置事件的接受状态（内部接口）
 * @param event 事件对象指针，不可为 NULL
 * @param accepted 目标状态：true 表示接受，false 表示忽略
 */
void XEvent_setAccepted_base(XEvent* event, bool accepted);

/**
 * @brief 判断事件是否为自发事件（由系统或用户真实触发）
 * @param event 事件对象指针，不可为 NULL
 * @return 是自发事件则返回 true，程序合成事件则返回 false
 */
bool XEvent_spontaneous(const XEvent* event);

/**
 * @brief 获取事件的具体类型
 * @param event 事件对象指针，不可为 NULL
 * @return 事件类型枚举值（如 XEventType_MousePress 等）
 */
XEventType XEvent_type(const XEvent* event);

/**
 * @brief 键盘修饰键位掩码，可按位组合。
 * @note 多个修饰键使用按位或组合，例如 ControlModifier | ShiftModifier。
 */
typedef enum XKeyboardModifier {
    XKeyboardModifier_NoModifier = 0x00000000,      ///< 未按下修饰键。
    XKeyboardModifier_ShiftModifier = 0x00000001,   ///< Shift 修饰键。
    XKeyboardModifier_ControlModifier = 0x00000002, ///< Control 修饰键。
    XKeyboardModifier_AltModifier = 0x00000004,     ///< Alt 修饰键。
    XKeyboardModifier_MetaModifier = 0x00000008,    ///< Meta 或系统修饰键。
    XKeyboardModifier_KeypadModifier = 0x00000010   ///< 数字小键盘来源标志。
} XKeyboardModifier;

typedef uint32_t XKeyboardModifiers;

/** @brief 鼠标按键位掩码，与 Qt::MouseButton 的常用取值对应。 */
typedef enum XMouseButton {
    XMouseButton_NoButton = 0x00000000,      ///< 无触发按键。
    XMouseButton_LeftButton = 0x00000001,    ///< 鼠标左键。
    XMouseButton_RightButton = 0x00000002,   ///< 鼠标右键。
    XMouseButton_MiddleButton = 0x00000004,  ///< 鼠标中键。
    XMouseButton_BackButton = 0x00000008,    ///< 鼠标后退键。
    XMouseButton_ForwardButton = 0x00000010  ///< 鼠标前进键。
} XMouseButton;

/**
 * @brief 与平台无关的按键码（对标 Qt 6.8 Qt::Key 取值约定）。
 * @details 按键码使用统一约定，平台后端（X11/Win32）在事件注入前把
 *          原生键值映射为下列取值，应用代码只依赖本枚举：
 *          - 可打印字符：直接使用 ASCII/Unicode 码位（如 'A'=0x41、
 *            '1'=0x31、空格=0x20），0 表示无按键；
 *          - 特殊功能键：使用 0x01000000 起的保留区间，取值与
 *            Qt::Key 完全一致，未来迁移 Qt 时可逐位对齐。
 *          方向键/回车/退格等功能键与 Qt::Key 同名同值。
 */
typedef enum XKey {
    XKey_None = 0,                    /**< 无按键（与 Qt::Key_unknown 语义区分，值为 0）。 */
    XKey_Space = 0x20,                /**< 空格键。 */
    XKey_Exclam = 0x21,               /**< 惊叹号 !。 */
    XKey_QuoteDbl = 0x22,             /**< 双引号 "。 */
    XKey_NumberSign = 0x23,           /**< 井号 #。 */
    XKey_Dollar = 0x24,               /**< 美元符 $。 */
    XKey_Percent = 0x25,              /**< 百分号 %。 */
    XKey_Ampersand = 0x26,            /**< 与号 &。 */
    XKey_Apostrophe = 0x27,           /**< 单引号 '。 */
    XKey_ParenLeft = 0x28,            /**< 左括号 (。 */
    XKey_ParenRight = 0x29,           /**< 右括号 )。 */
    XKey_Asterisk = 0x2a,             /**< 星号 *。 */
    XKey_Plus = 0x2b,                 /**< 加号 +。 */
    XKey_Comma = 0x2c,                /**< 逗号 ,。 */
    XKey_Minus = 0x2d,                /**< 减号 -。 */
    XKey_Period = 0x2e,               /**< 句点 .。 */
    XKey_Slash = 0x2f,                /**< 斜杠 /。 */
    XKey_0 = 0x30,                    /**< 数字 0。 */
    XKey_1 = 0x31,                    /**< 数字 1。 */
    XKey_2 = 0x32,                    /**< 数字 2。 */
    XKey_3 = 0x33,                    /**< 数字 3。 */
    XKey_4 = 0x34,                    /**< 数字 4。 */
    XKey_5 = 0x35,                    /**< 数字 5。 */
    XKey_6 = 0x36,                    /**< 数字 6。 */
    XKey_7 = 0x37,                    /**< 数字 7。 */
    XKey_8 = 0x38,                    /**< 数字 8。 */
    XKey_9 = 0x39,                    /**< 数字 9。 */
    XKey_Colon = 0x3a,                /**< 冒号 :。 */
    XKey_Semicolon = 0x3b,            /**< 分号 ;。 */
    XKey_Less = 0x3c,                 /**< 小于号 <。 */
    XKey_Equal = 0x3d,                /**< 等号 =。 */
    XKey_Greater = 0x3e,              /**< 大于号 >。 */
    XKey_Question = 0x3f,             /**< 问号 ?。 */
    XKey_At = 0x40,                   /**< @ 符号。 */
    XKey_A = 0x41,                    /**< 字母 A。 */
    XKey_B = 0x42,                    /**< 字母 B。 */
    XKey_C = 0x43,                    /**< 字母 C。 */
    XKey_D = 0x44,                    /**< 字母 D。 */
    XKey_E = 0x45,                    /**< 字母 E。 */
    XKey_F = 0x46,                    /**< 字母 F。 */
    XKey_G = 0x47,                    /**< 字母 G。 */
    XKey_H = 0x48,                    /**< 字母 H。 */
    XKey_I = 0x49,                    /**< 字母 I。 */
    XKey_J = 0x4a,                    /**< 字母 J。 */
    XKey_K = 0x4b,                    /**< 字母 K。 */
    XKey_L = 0x4c,                    /**< 字母 L。 */
    XKey_M = 0x4d,                    /**< 字母 M。 */
    XKey_N = 0x4e,                    /**< 字母 N。 */
    XKey_O = 0x4f,                    /**< 字母 O。 */
    XKey_P = 0x50,                    /**< 字母 P。 */
    XKey_Q = 0x51,                    /**< 字母 Q。 */
    XKey_R = 0x52,                    /**< 字母 R。 */
    XKey_S = 0x53,                    /**< 字母 S。 */
    XKey_T = 0x54,                    /**< 字母 T。 */
    XKey_U = 0x55,                    /**< 字母 U。 */
    XKey_V = 0x56,                    /**< 字母 V。 */
    XKey_W = 0x57,                    /**< 字母 W。 */
    XKey_X = 0x58,                    /**< 字母 X。 */
    XKey_Y = 0x59,                    /**< 字母 Y。 */
    XKey_Z = 0x5a,                    /**< 字母 Z。 */
    XKey_BracketLeft = 0x5b,          /**< 左方括号 [。 */
    XKey_Backslash = 0x5c,            /**< 反斜杠 \。 */
    XKey_BracketRight = 0x5d,         /**< 右方括号 ]。 */
    XKey_AsciiCircum = 0x5e,          /**< 脱字符 ^。 */
    XKey_Underscore = 0x5f,           /**< 下划线 _。 */
    XKey_QuoteLeft = 0x60,            /**< 反引号 `。 */
    XKey_a = 0x61,                    /**< 小写字母 a。 */
    XKey_b = 0x62,                    /**< 小写字母 b。 */
    XKey_c = 0x63,                    /**< 小写字母 c。 */
    XKey_d = 0x64,                    /**< 小写字母 d。 */
    XKey_e = 0x65,                    /**< 小写字母 e。 */
    XKey_f = 0x66,                    /**< 小写字母 f。 */
    XKey_g = 0x67,                    /**< 小写字母 g。 */
    XKey_h = 0x68,                    /**< 小写字母 h。 */
    XKey_i = 0x69,                    /**< 小写字母 i。 */
    XKey_j = 0x6a,                    /**< 小写字母 j。 */
    XKey_k = 0x6b,                    /**< 小写字母 k。 */
    XKey_l = 0x6c,                    /**< 小写字母 l。 */
    XKey_m = 0x6d,                    /**< 小写字母 m。 */
    XKey_n = 0x6e,                    /**< 小写字母 n。 */
    XKey_o = 0x6f,                    /**< 小写字母 o。 */
    XKey_p = 0x70,                    /**< 小写字母 p。 */
    XKey_q = 0x71,                    /**< 小写字母 q。 */
    XKey_r = 0x72,                    /**< 小写字母 r。 */
    XKey_s = 0x73,                    /**< 小写字母 s。 */
    XKey_t = 0x74,                    /**< 小写字母 t。 */
    XKey_u = 0x75,                    /**< 小写字母 u。 */
    XKey_v = 0x76,                    /**< 小写字母 v。 */
    XKey_w = 0x77,                    /**< 小写字母 w。 */
    XKey_x = 0x78,                    /**< 小写字母 x。 */
    XKey_y = 0x79,                    /**< 小写字母 y。 */
    XKey_z = 0x7a,                    /**< 小写字母 z。 */
    XKey_BraceLeft = 0x7b,            /**< 左花括号 {。 */
    XKey_Bar = 0x7c,                  /**< 竖线 |。 */
    XKey_BraceRight = 0x7d,           /**< 右花括号 }。 */
    XKey_AsciiTilde = 0x7e,           /**< 波浪号 ~。 */
    XKey_Escape = 0x01000000,         /**< Esc 键。 */
    XKey_Tab = 0x01000001,            /**< Tab 键。 */
    XKey_Backtab = 0x01000002,        /**< 反向 Tab（Shift+Tab）。 */
    XKey_Backspace = 0x01000003,      /**< 退格键。 */
    XKey_Return = 0x01000004,         /**< 回车键（主键盘）。 */
    XKey_Enter = 0x01000005,          /**< 小键盘回车（与 Return 区分）。 */
    XKey_Insert = 0x01000006,         /**< 插入键。 */
    XKey_Delete = 0x01000007,         /**< 删除键。 */
    XKey_Pause = 0x01000008,          /**< 暂停键。 */
    XKey_Print = 0x01000009,          /**< 打印屏幕键。 */
    XKey_SysReq = 0x0100000a,         /**< SysReq 键。 */
    XKey_Clear = 0x0100000b,          /**< 清除键。 */
    XKey_Home = 0x01000010,           /**< Home 键。 */
    XKey_End = 0x01000011,            /**< End 键。 */
    XKey_Left = 0x01000012,           /**< 左方向键。 */
    XKey_Up = 0x01000013,             /**< 上方向键。 */
    XKey_Right = 0x01000014,          /**< 右方向键。 */
    XKey_Down = 0x01000015,           /**< 下方向键。 */
    XKey_PageUp = 0x01000016,         /**< 上一页键。 */
    XKey_PageDown = 0x01000017,       /**< 下一页键。 */
    XKey_Shift = 0x01000020,          /**< Shift 修饰键。 */
    XKey_Control = 0x01000021,        /**< Control 修饰键。 */
    XKey_Meta = 0x01000022,           /**< Meta/Super 修饰键。 */
    XKey_Alt = 0x01000023,            /**< Alt 修饰键。 */
    XKey_AltGr = 0x01001103,          /**< AltGr 修饰键。 */
    XKey_CapsLock = 0x01000024,       /**< CapsLock 切换键。 */
    XKey_NumLock = 0x01000025,        /**< NumLock 切换键。 */
    XKey_ScrollLock = 0x01000026,     /**< ScrollLock 切换键。 */
    XKey_F1 = 0x01000030,             /**< F1 功能键。 */
    XKey_F2 = 0x01000031,             /**< F2 功能键。 */
    XKey_F3 = 0x01000032,             /**< F3 功能键。 */
    XKey_F4 = 0x01000033,             /**< F4 功能键。 */
    XKey_F5 = 0x01000034,             /**< F5 功能键。 */
    XKey_F6 = 0x01000035,             /**< F6 功能键。 */
    XKey_F7 = 0x01000036,             /**< F7 功能键。 */
    XKey_F8 = 0x01000037,             /**< F8 功能键。 */
    XKey_F9 = 0x01000038,             /**< F9 功能键。 */
    XKey_F10 = 0x01000039,            /**< F10 功能键。 */
    XKey_F11 = 0x0100003a,            /**< F11 功能键。 */
    XKey_F12 = 0x0100003b,            /**< F12 功能键。 */
    XKey_F13 = 0x0100003c,            /**< F13 功能键。 */
    XKey_F14 = 0x0100003d,            /**< F14 功能键。 */
    XKey_F15 = 0x0100003e,            /**< F15 功能键。 */
    XKey_F16 = 0x0100003f,            /**< F16 功能键。 */
    XKey_F17 = 0x01000040,            /**< F17 功能键。 */
    XKey_F18 = 0x01000041,            /**< F18 功能键。 */
    XKey_F19 = 0x01000042,            /**< F19 功能键。 */
    XKey_F20 = 0x01000043,            /**< F20 功能键。 */
    XKey_F21 = 0x01000044,            /**< F21 功能键。 */
    XKey_F22 = 0x01000045,            /**< F22 功能键。 */
    XKey_F23 = 0x01000046,            /**< F23 功能键。 */
    XKey_F24 = 0x01000047,            /**< F24 功能键。 */
    XKey_F25 = 0x01000048,            /**< F25 功能键。 */
    XKey_F26 = 0x01000049,            /**< F26 功能键。 */
    XKey_F27 = 0x0100004a,            /**< F27 功能键。 */
    XKey_F28 = 0x0100004b,            /**< F28 功能键。 */
    XKey_F29 = 0x0100004c,            /**< F29 功能键。 */
    XKey_F30 = 0x0100004d,            /**< F30 功能键。 */
    XKey_F31 = 0x0100004e,            /**< F31 功能键。 */
    XKey_F32 = 0x0100004f,            /**< F32 功能键。 */
    XKey_F33 = 0x01000050,            /**< F33 功能键。 */
    XKey_F34 = 0x01000051,            /**< F34 功能键。 */
    XKey_F35 = 0x01000052             /**< F35 功能键。 */
} XKey;


/** @brief 携带按键和修饰键数据的键盘事件。 */
XCLASS_DEFINE_BEGING(XKeyEvent)
XCLASS_DEFINE_EXTEND_END(XKeyEvent, XEvent)

typedef struct XKeyEvent {
    XEvent m_class;                 ///< 继承 XEvent。
    int m_key;                      ///< 与平台无关的按键码（XKey 枚举或 ASCII 码位）。
    XKeyboardModifiers m_modifiers; ///< 事件发生时按下的修饰键。
    bool m_autoRepeat;              ///< 是否为系统自动重复产生（按住不放）。
} XKeyEvent;

/**
 * @brief 创建键盘事件。
 * @param type 键盘事件类型，通常为 XEVENT_TYPE_KEY_PRESS 或 XEVENT_TYPE_KEY_RELEASE。
 * @param key 与平台无关的按键码。
 * @param modifiers 事件发生时按下的修饰键组合。
 * @return 新键盘事件；内存分配失败时返回 NULL。
 * @note 返回对象由调用者拥有，投递成功后由事件队列取得所有权。
 */
XKeyEvent* XKeyEvent_create_ex(XMemoryType memory,  XEventType type, int key, XKeyboardModifiers modifiers);
/**
 * @brief 初始化调用者提供的键盘事件存储。
 * @param event 待初始化的键盘事件，不可为 NULL。
 * @param type 键盘事件类型。
 * @param key 与平台无关的按键码。
 * @param modifiers 事件发生时按下的修饰键组合。
 */
void XKeyEvent_init(XKeyEvent* event, XEventType type, int key, XKeyboardModifiers modifiers);
/**
 * @brief 获取按键码。
 * @param event 键盘事件实例。
 * @return 按键码；event 为 NULL 时返回 0。
 */
int XKeyEvent_key(const XKeyEvent* event);
/**
 * @brief 获取修饰键位掩码。
 * @param event 键盘事件实例。
 * @return 修饰键组合；event 为 NULL 时返回 NoModifier。
 */
XKeyboardModifiers XKeyEvent_modifiers(const XKeyEvent* event);
/**
 * @brief 判断是否为系统自动重复产生的按键事件（对标 QKeyEvent::isAutoRepeat）。
 * @param event 键盘事件实例。
 * @return true 表示按住按键不放由系统重复产生；false 为首次按下/释放。
 */
bool XKeyEvent_autoRepeat(const XKeyEvent* event);
/**
 * @brief 设置自动重复标志（平台后端注入重复按键时使用）。
 * @param event 键盘事件实例；不可为 NULL。
 * @param autoRepeat 目标状态。
 */
void XKeyEvent_setAutoRepeat(XKeyEvent* event, bool autoRepeat);

#define XKeyEvent_delete_base XEvent_delete_base
#define XKeyEvent_deinit_base XEvent_deinit_base

/** @brief 携带按键、修饰键和位置数据的鼠标事件。 */
XCLASS_DEFINE_BEGING(XMouseEvent)
XCLASS_DEFINE_EXTEND_END(XMouseEvent, XEvent)

typedef struct XMouseEvent {
    XEvent m_class;                 ///< 继承 XEvent。
    XMouseButton m_button;          ///< 触发该事件的鼠标按键；移动事件为 NoButton。
    XMouseButton m_buttons;         ///< 事件发生时所有处于按下状态的鼠标按键（位掩码）。
    XKeyboardModifiers m_modifiers; ///< 事件发生时按下的修饰键。
    XPoint m_position;              ///< 事件源对象局部坐标。
} XMouseEvent;

/**
 * @brief 创建鼠标事件。
 * @param type 鼠标事件类型，例如按下、释放、双击或移动。
 * @param button 触发该事件的鼠标按键；移动事件通常为 NoButton。
 * @param modifiers 事件发生时按下的键盘修饰键组合。
 * @param position 事件源对象坐标系中的局部位置。
 * @return 新鼠标事件；内存分配失败时返回 NULL。
 * @note 返回对象由调用者拥有，投递成功后由事件队列取得所有权。
 */
XMouseEvent* XMouseEvent_create_ex(XMemoryType memory,  XEventType type, XMouseButton button,
                                XKeyboardModifiers modifiers, XPoint position);
/**
 * @brief 初始化调用者提供的鼠标事件存储。
 * @param event 待初始化的鼠标事件，不可为 NULL。
 * @param type 鼠标事件类型。
 * @param button 触发该事件的鼠标按键。
 * @param modifiers 事件发生时按下的键盘修饰键组合。
 * @param position 事件源对象坐标系中的局部位置。
 */
void XMouseEvent_init(XMouseEvent* event, XEventType type, XMouseButton button,
                      XKeyboardModifiers modifiers, XPoint position);
/**
 * @brief 获取触发事件的鼠标按键。
 * @param event 鼠标事件实例。
 * @return 鼠标按键；event 为 NULL 时返回 NoButton。
 */
XMouseButton XMouseEvent_button(const XMouseEvent* event);
/**
 * @brief 获取事件发生时处于按下状态的全部鼠标按键（对标 QMouseEvent::buttons）。
 * @param event 鼠标事件实例。
 * @return 按键位掩码组合；event 为 NULL 时返回 NoButton。
 * @note 移动/滚轮事件也填写该字段，供应用判断拖拽等状态。
 */
XMouseButton XMouseEvent_buttons(const XMouseEvent* event);
/**
 * @brief 设置事件发生时处于按下状态的全部鼠标按键（平台后端注入时使用）。
 * @param event 鼠标事件实例；不可为 NULL。
 * @param buttons 按键位掩码组合。
 */
void XMouseEvent_setButtons(XMouseEvent* event, XMouseButton buttons);

/**
 * @brief 获取修饰键位掩码。
 * @param event 鼠标事件实例。
 * @return 修饰键组合；event 为 NULL 时返回 NoModifier。
 */
XKeyboardModifiers XMouseEvent_modifiers(const XMouseEvent* event);
/**
 * @brief 获取事件源对象局部坐标。
 * @param event 鼠标事件实例。
 * @return 局部坐标；event 为 NULL 时返回零坐标。
 */
XPoint XMouseEvent_position(const XMouseEvent* event);

#define XMouseEvent_delete_base XEvent_delete_base
#define XMouseEvent_deinit_base XEvent_deinit_base

// ------------------ 工具 ------------------

/**
 * @brief 注册一个可供应用程序使用的自定义事件类型。
 * @param hint 期望使用的事件编号；传入 -1 表示自动分配可用编号。
 * @return 成功时返回实际事件编号；没有可用编号时返回 -1。
 * @note 指定的 hint 已被占用时会自动分配其他编号。
 */
int XEvent_registerEventType(int hint);

//删除事件
typedef struct XDeferredDeleteEvent
{
    XEvent m_base;
    bool isDelete;
    int loopLevel;   // Qt 6.8: 调用 deleteLater 时的事件循环嵌套层级
    int scopeLevel;  // Qt 6.8: 调用 deleteLater 时的作用域层级
}XDeferredDeleteEvent;
XDeferredDeleteEvent*XDeferredDeleteEvent_create_ex(XMemoryType memory,  bool isDelete, int loopLevel, int scopeLevel);
bool XDeferredDeleteEvent_shouldDeliver(const XDeferredDeleteEvent* event,
                                        const XThreadData* threadData,
                                        bool explicitlyRequested);
void XDeferredDeleteEvent_handler(XDeferredDeleteEvent* event, XObject* receiver);
//定时器事件
typedef struct XTimerEvent
{
    XEvent m_base;
    union 
    {
        XTimerId timerId;//本质就是XFd
        XFd fd;
    };
}XTimerEvent;
XTimerEvent* XTimerEvent_create_ex(XMemoryType memory,  XTimerId id);
XTimerId XTimerEvent_timerId(const XTimerEvent* event);
//套接字活动事件
typedef struct XEventSockAct
{
    XEvent m_base;
    //XSocketDescriptor socket;
    XFd fd;
    XSocketActType actType;//活动类型
}XEventSockAct;
XEventSockAct* XEventSockAct_create_ex(XMemoryType memory,  XFd fd, XSocketActType actType);
typedef struct XEventSockClose
{
    XEvent m_base;
    //XSocketDescriptor socket;
    XFd fd;
}XEventSockClose;
XEventSockClose* XEventSockClose_create_ex(XMemoryType memory,  XFd fd);
//孩子事件
typedef struct {
    XEvent m_base;
    XObject* child;
} XChildEvent;
/**
 * @brief 创建一个子对象事件
 * @param type 事件类型（例如添加、移除或 polish 状态）
 * @param child 发生事件的子对象
 * @return 新创建的 XChildEvent 实例，需由调用者负责释放
 */
XChildEvent* XChildEvent_create_ex(XMemoryType memory,  XEventType type, XObject* child);

/**
 * @brief 判断子对象事件是否为“已添加”类型
 * @param event 指向 XEvent 的常量指针（通常为 XChildEvent 实例）
 * @return 若事件表示子对象被添加，则返回 true；否则返回 false
 */
bool XChildEvent_added(const XChildEvent* event);

/**
 * @brief 获取子对象事件中涉及的子对象
 * @param event 指向 XEvent 的常量指针（通常为 XChildEvent 实例）
 * @return 与事件关联的子对象指针，若事件无效则返回 NULL
 */
XObject* XChildEvent_child(const XChildEvent* event);

/**
 * @brief 判断子对象事件是否为“已 polish”类型
 * @param event 指向 XEvent 的常量指针（通常为 XChildEvent 实例）
 * @return 若事件表示子对象已完成布局和绘制准备（polished），则返回 true；否则返回 false
 */
bool XChildEvent_polished(const XChildEvent* event);

/**
 * @brief 判断子对象事件是否为“已移除”类型
 * @param event 指向 XEvent 的常量指针（通常为 XChildEvent 实例）
 * @return 若事件表示子对象被移除，则返回 true；否则返回 false
 */
bool XChildEvent_removed(const XChildEvent* event);
//子对象事件默认处理
void XChildEvent_handler(XChildEvent* event, XObject* receiver);
//动态属性事件
typedef struct {
    XEvent m_base;
    XByteArray* propertyName;
} XDynamicPropertyChangeEvent;
XDynamicPropertyChangeEvent* XDynamicPropertyChangeEvent_create_ex(XMemoryType memory,  const char* name);
const char* XDynamicPropertyChangeEvent_propertyName(const XEvent* event);
//函数运行事件
XCLASS_DEFINE_BEGING(XEventFunc)
XCLASS_DEFINE_EXTEND_END(XEventFunc, XEvent)

typedef struct XEventFunc
{
    XEvent event;
    XCallableToRun func; // 需要执行的函数
    XVarList* argList;                   // 函数参数
}XEventFunc;
/**
 * @brief 创建函数事件
 * @param func 要执行的函数
 * @param argList 函数参数
 * @return 新创建的函数事件
 */
XEventFunc* XEventFunc_create_ex(XMemoryType memory,  XCallableToRun func, XVarList* argList, void(*del_argList)(XVarList*));
void XEventFunc_init(XEventFunc* event, void(*func)(XVarList*), XVarList* argList, void(*del_argList)(XVarList*));
XVtable* XEventFunc_class_init();
/**
 * @brief 执行函数事件的回调
 * @param event 函数事件
 */
void XEventFunc_handler(XEventFunc* event);


//槽函数调用事件
XCLASS_DEFINE_BEGING(XMetaCallEvent)
XCLASS_DEFINE_EXTEND_END(XMetaCallEvent, XEvent)

typedef struct XMetaCallEvent
{
    XEvent event;
    XSlotFunc1 func;               // 需要执行的槽函数
    XObject* sender;              // 发送者对象
    size_t signal_id;          // Qt 6.8: 信号索引 (对标 QMetaCallEvent::signalId)
    XVarList* argList;            // 函数参数
    //void(*del)(XVarList*);        // XVarList释放函数
   XSemaphore* sem;              //信号量
   XAtomic_int32_t* ref_count;   // 参数引用计数
}XMetaCallEvent;

/**
 * @brief 创建槽函数事件
 * @param sender 发送者对象
 * @param receiver 接收者对象
 * @param func 槽函数
 * @param argList 槽函数参数
 * @param del  槽函数参数释放规则
 * @param ref_count 参数引用计数
 * @param priority 事件优先级
 * @return 新创建的槽函数事件
 */
XMetaCallEvent* XMetaCallEvent_create_ex(XMemoryType memory,  XObject* sender, XSlotFunc1 func, size_t signal_id,
    XVarList* argList, XAtomic_int32_t* ref_count, XSemaphore* sem);
XVtable* XMetaCallEvent_class_init();
/**
 * @brief 执行槽函数事件的回调
 * @param event 槽函数事件
 */
void XMetaCallEvent_handler(XMetaCallEvent* event, XObject* receiver);

#ifdef __cplusplus
}
#endif	

/* XClass create API default-memory wrappers. */
#undef XChildEvent_create
#define XChildEvent_create(...) XChildEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XDeferredDeleteEvent_create
#define XDeferredDeleteEvent_create(...) XDeferredDeleteEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XDynamicPropertyChangeEvent_create
#define XDynamicPropertyChangeEvent_create(name) \
	XDynamicPropertyChangeEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, name)
#undef XEvent_create
#define XEvent_create(...) XEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XEventFunc_create
#define XEventFunc_create(...) XEventFunc_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XEventSockAct_create
#define XEventSockAct_create(...) XEventSockAct_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XEventSockClose_create
#define XEventSockClose_create(...) XEventSockClose_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XKeyEvent_create
#define XKeyEvent_create(...) XKeyEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XMetaCallEvent_create
#define XMetaCallEvent_create(...) XMetaCallEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XMouseEvent_create
#define XMouseEvent_create(...) XMouseEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)
#undef XTimerEvent_create
#define XTimerEvent_create(...) XTimerEvent_create_ex(XMEMORY_TYPE_MULTIPOOL, __VA_ARGS__)

#endif // !XDataFrameCommunicatorEvent_H
