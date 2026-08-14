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
#include"XData/XGeometry.h"
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
XEvent* XEvent_create(XEventType code);
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

/** @brief 携带按键和修饰键数据的键盘事件。 */
XCLASS_DEFINE_BEGING(XKeyEvent)
XCLASS_DEFINE_EXTEND_END(XKeyEvent, XEvent)

typedef struct XKeyEvent {
    XEvent m_class;                 ///< 继承 XEvent。
    int m_key;                      ///< 与平台无关的按键码。
    XKeyboardModifiers m_modifiers; ///< 事件发生时按下的修饰键。
} XKeyEvent;

/**
 * @brief 创建键盘事件。
 * @param type 键盘事件类型，通常为 XEVENT_TYPE_KEY_PRESS 或 XEVENT_TYPE_KEY_RELEASE。
 * @param key 与平台无关的按键码。
 * @param modifiers 事件发生时按下的修饰键组合。
 * @return 新键盘事件；内存分配失败时返回 NULL。
 * @note 返回对象由调用者拥有，投递成功后由事件队列取得所有权。
 */
XKeyEvent* XKeyEvent_create(XEventType type, int key, XKeyboardModifiers modifiers);
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

#define XKeyEvent_delete_base XEvent_delete_base
#define XKeyEvent_deinit_base XEvent_deinit_base

/** @brief 携带按键、修饰键和位置数据的鼠标事件。 */
XCLASS_DEFINE_BEGING(XMouseEvent)
XCLASS_DEFINE_EXTEND_END(XMouseEvent, XEvent)

typedef struct XMouseEvent {
    XEvent m_class;                 ///< 继承 XEvent。
    XMouseButton m_button;          ///< 触发事件的鼠标按键。
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
XMouseEvent* XMouseEvent_create(XEventType type, XMouseButton button,
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
XDeferredDeleteEvent*XDeferredDeleteEvent_create(bool isDelete, int loopLevel, int scopeLevel);
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
XTimerEvent* XTimerEvent_create(XTimerId id);
XTimerId XTimerEvent_timerId(const XTimerEvent* event);
//套接字活动事件
typedef struct XEventSockAct
{
    XEvent m_base;
    //XSocketDescriptor socket;
    XFd fd;
    XSocketActType actType;//活动类型
}XEventSockAct;
XEventSockAct* XEventSockAct_create(XFd fd, XSocketActType actType);
typedef struct XEventSockClose
{
    XEvent m_base;
    //XSocketDescriptor socket;
    XFd fd;
}XEventSockClose;
XEventSockClose* XEventSockClose_create(XFd fd);
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
XChildEvent* XChildEvent_create(XEventType type, XObject* child);

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
XDynamicPropertyChangeEvent* XDynamicPropertyChangeEvent_create(const char* name);
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
XEventFunc* XEventFunc_create(XCallableToRun func, XVarList* argList, void(*del_argList)(XVarList*));
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
XMetaCallEvent* XMetaCallEvent_create(XObject* sender, XSlotFunc1 func, size_t signal_id,
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
#endif // !XDataFrameCommunicatorEvent_H
