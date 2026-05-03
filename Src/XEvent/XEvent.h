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
 * @param receiver 事件接收对象
 * @param code 事件类型
 * @param timestamp 时间戳，0表示使用当前时间
 * @param priority 事件优先级
 * @return 新创建的基础事件
 */
XEvent* XEvent_create(XEventType code);
/**
 * @brief 初始化基础事件
 * @param event 要初始化的事件
 * @param code 事件类型
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

// ------------------ 工具 ------------------

int XEvent_registerEventType(int hint);

//删除事件
typedef struct XEventDeferredDelete
{
    XEvent m_base;
    bool isDelete;
}XEventDeferredDelete;
XEventDeferredDelete*XEventDeferredDelete_create(bool isDelete);
void XEventDeferredDelete_handler(XEventDeferredDelete* event, XObject* receiver);
//定时器事件
typedef struct XEventTimer
{
    XEvent m_base;
    XTimerId timerId;
}XEventTimer;
XEventTimer* XEventTimer_create(XTimerId id);
XTimerId XEventTimer_timerId(const XEventTimer* event);
//套接字活动事件
typedef struct XEventSockAct
{
    XEvent m_base;
    XSocketDescriptor socket;
    XSocketActType actType;//活动类型
}XEventSockAct;
XEventSockAct* XEventSockAct_create(XSocketDescriptor socket, XSocketActType actType);
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
typedef struct XEventFunc
{
    XEvent event;
    void (*func)(void* args); // 需要执行的函数
    XVarList* argList;                   // 函数参数
}XEventFunc;
/**
 * @brief 创建函数事件
 * @param func 要执行的函数
 * @param argList 函数参数
 * @return 新创建的函数事件
 */
XEventFunc* XEventFunc_create(void (*func)(XVarList*), XVarList* argList, void(*del_argList)(XVarList*));
void XEventFunc_init(XEventFunc* event, void(*func)(XVarList*), XVarList* argList, void(*del_argList)(XVarList*));
XVtable* XEventFunc_class_init();
/**
 * @brief 执行函数事件的回调
 * @param event 函数事件
 */
void XEventFunc_handler(XEventFunc* event);


//槽函数调用事件
typedef struct XEventMetaCall
{
    XEvent event;
    XSlotFunc1 func;               // 需要执行的槽函数
    XObject* sender;              // 发送者对象
    XVarList* argList;            // 函数参数
    //void(*del)(XVarList*);        // XVarList释放函数
    XAtomic_int32_t* ref_count;   // 参数引用计数
    XSemaphore* sem;              //信号量
}XEventMetaCall;

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
XEventMetaCall* XEventMetaCall_create(XObject* sender, XSlotFunc1 func,
    XVarList* argList, XAtomic_int32_t* ref_count, XSemaphore* sem);
XVtable* XEventMetaCall_class_init();
/**
 * @brief 执行槽函数事件的回调
 * @param event 槽函数事件
 */
void XEventMetaCall_handler(XEventMetaCall* event, XObject* receiver);

#ifdef __cplusplus
}
#endif	
#endif // !XDataFrameCommunicatorEvent_H
