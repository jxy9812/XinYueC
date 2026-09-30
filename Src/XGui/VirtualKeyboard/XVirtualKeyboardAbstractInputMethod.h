/**
 * @file       XVirtualKeyboardAbstractInputMethod.h
 * @brief      XVirtualKeyboardAbstractInputMethod 语言输入法公共基类公
 *             开 API（对标 Qt 6.8 QVirtualKeyboardAbstractInputMethod）。
 * @details    对齐 Qt qvirtualkeyboardabstractinputmethod.h（6.8.3）：
 *             - 继承：QObject→XObject（先例 XInputMethod.h:52-53）；
 *             - 纯虚四件经 XCLASS_DEFINE_ENUM 槽位承载：InputModes/
 *               SetInputMode/SetTextCase/KeyEvent（Qt :33-37）；C 无纯
 *               虚语法，类初始化注册的默认实现为空操作——语言插件必须
 *               经 XVTABLE_OVERLOAD_DEFAULT 注册四个槽位，未注册时行
 *               为等价「不支持」（inputModes 空、keyEvent 不消费）；
 *             - 默认实现槽位（Qt :39-62）：SelectionLists/
 *               SelectionListItemCount/SelectionListData/
 *               SelectionListItemSelected/SelectionListRemoveItem/
 *               PatternRecognitionModes/TraceBegin/TraceEnd/Reselect/
 *               ClickPreeditText/Reset/Update/ClearInputMode；
 *             - selectionListData/模型文本角色一律返回新建 XVariant*
 *               （XVariantType_String 承载，XString_toVariant_utf8 先
 *               例），调用方释放；禁止裸 const char* 跨 API 传递文本；
 *             - 信号 3 个：selectionListChanged/selectionListActiveItem
 *               Changed/selectionListsChanged（Qt :55-57）；
 *             - setInputEngine 仅引擎可调（Qt friend 口径）→
 *               XVirtualKeyboardAbstractInputMethod_Protected.h（类名级
 *               保护头，与现有 12 个保护头同款命名）。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON 定义于 XGuiConfig.h；=0 时
 *             裁剪全部公共 API。实现只依赖 XinYueC 抽象层，禁止调用
 *             Win32、POSIX、Qt 或其他平台 API。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDABSTRACTINPUTMETHOD_H
#define XVIRTUALKEYBOARDABSTRACTINPUTMETHOD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XMemory.h"
#include "XVariant.h"

#if XVIRTUALKEYBOARD_ON

/* 前向声明（避免包含环：引擎头反向引用本头；本类自引用 typedef 与
   XPlatformInputContext.h 既有同款模式）。 */
typedef struct XVirtualKeyboardInputEngine XVirtualKeyboardInputEngine;
typedef struct XVirtualKeyboardInputContext XVirtualKeyboardInputContext;
typedef struct XVirtualKeyboardTrace XVirtualKeyboardTrace;
typedef struct XVirtualKeyboardAbstractInputMethod
    XVirtualKeyboardAbstractInputMethod;

/* ==================== 引擎侧枚举（本头内复制声明会有包含环，统一落
   引擎头；本头仅前向引用 int 语义槽位签名） ==================== */

/**
 * @brief      XVirtualKeyboardAbstractInputMethod 虚函数表枚举（继承
 *             XObject；对标 Qt 虚函数集，槽位序与签名一一对应）。
 * @note       InputModes/SetInputMode/SetTextCase/KeyEvent 四槽对应
 *             Qt 纯虚函数；其余为 Qt 有默认实现的虚函数（默认注册空
 *             实现）。locale 参数统一为 UTF-8 借用串（Qt 为 QString）。
 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardAbstractInputMethod)
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, InputModes) =
    XCLASS_VTABLE_GET_SIZE(XObject),   /**< 查询支持输入模式集（纯虚语义）。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, SetInputMode),   /**< 切换输入模式（纯虚语义）。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, SetTextCase),    /**< 设置大小写（纯虚语义）。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, KeyEvent),       /**< 按键处理（纯虚语义）。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, SelectionLists), /**< 查询候选列表集合。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, SelectionListItemCount), /**< 候选数。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, SelectionListData), /**< 候选数据（新建 XVariant*）。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, SelectionListItemSelected), /**< 候选选中回调。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, SelectionListRemoveItem), /**< 移除候选。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, PatternRecognitionModes), /**< 手写识别模式集。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, TraceBegin),     /**< 开始一笔（返回 XVirtualKeyboardTrace*）。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, TraceEnd),       /**< 结束一笔。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, Reselect),       /**< 再选光标处词。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, ClickPreeditText), /**< 组串点击。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, Reset),          /**< 复位（槽）。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, Update),         /**< 更新（槽）。 */
XCLASS_DEFINE_ENUM(XVirtualKeyboardAbstractInputMethod, ClearInputMode), /**< 清输入模式（槽，Qt REVISION(6,1)）。 */
XCLASS_DEFINE_END(XVirtualKeyboardAbstractInputMethod)

/* ==================== 虚槽签名 typedef（XClassGetVirtualFunc 用） ==================== */

/**
 * @brief      inputModes 虚槽签名。
 * @param      self 输入法对象。
 * @param      locale 区域语言（UTF-8 借用；NULL 按 "C"）。
 * @param      outModes 输出数组（调用方提供存储空间；可为 NULL 仅询数）。
 * @param      maxCount 输出数组容量。
 * @return     实际模式数（<=maxCount；不支持返回 0）。
 */
typedef int (*XVirtualKeyboardInputMethodInputModesSlot)(
        XVirtualKeyboardAbstractInputMethod* self, const char* locale,
        int* outModes, int maxCount);
/** @brief setInputMode 虚槽签名；返回是否接受。 */
typedef bool (*XVirtualKeyboardInputMethodSetInputModeSlot)(
        XVirtualKeyboardAbstractInputMethod* self, const char* locale,
        int inputMode);
/** @brief setTextCase 虚槽签名；返回是否接受。 */
typedef bool (*XVirtualKeyboardInputMethodSetTextCaseSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int textCase);
/** @brief keyEvent 虚槽签名；返回 true=按键已消费（对标 Qt 返回 bool）。 */
typedef bool (*XVirtualKeyboardInputMethodKeyEventSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int key, const char* text,
        uint32_t modifiers);
/** @brief selectionLists 虚槽签名（输出 Type 值数组，返回条数）。 */
typedef int (*XVirtualKeyboardInputMethodSelectionListsSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int* outTypes,
        int maxCount);
/** @brief selectionListItemCount 虚槽签名。 */
typedef int (*XVirtualKeyboardInputMethodSelectionListItemCountSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int type);
/**
 * @brief      selectionListData 虚槽签名。
 * @return     新建 XVariant*（Display=候选 UTF-8 串，XVariantType_String
 *             承载；整型角色为对应整型变体）；不支持返回 NULL。调用方
 *             负责释放。
 */
typedef XVariant* (*XVirtualKeyboardInputMethodSelectionListDataSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int type, int index,
        int role);
/** @brief selectionListItemSelected 虚槽签名。 */
typedef void (*XVirtualKeyboardInputMethodSelectionListItemSelectedSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int type, int index);
/** @brief selectionListRemoveItem 虚槽签名；返回是否已移除。 */
typedef bool (*XVirtualKeyboardInputMethodSelectionListRemoveItemSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int type, int index);
/** @brief patternRecognitionModes 虚槽签名（输出模式数组，返回条数）。 */
typedef int (*XVirtualKeyboardInputMethodPatternRecognitionModesSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int* outModes,
        int maxCount);
/** @brief traceBegin 虚槽签名；返回新建 XVirtualKeyboardTrace*（不支
 *         持返回 NULL）。 */
typedef XVirtualKeyboardTrace* (*XVirtualKeyboardInputMethodTraceBeginSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int traceId,
        int patternRecognitionMode);
/** @brief traceEnd 虚槽签名；返回识别是否已受理。 */
typedef bool (*XVirtualKeyboardInputMethodTraceEndSlot)(
        XVirtualKeyboardAbstractInputMethod* self,
        XVirtualKeyboardTrace* trace);
/** @brief reselect 虚槽签名；返回是否已再选。 */
typedef bool (*XVirtualKeyboardInputMethodReselectSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int cursorPosition,
        uint32_t reselectFlags);
/** @brief clickPreeditText 虚槽签名。 */
typedef void (*XVirtualKeyboardInputMethodClickPreeditTextSlot)(
        XVirtualKeyboardAbstractInputMethod* self, int cursorPosition);
/** @brief reset/update/clearInputMode 槽签名（Qt 公共槽）。 */
typedef void (*XVirtualKeyboardInputMethodVoidSlot)(
        XVirtualKeyboardAbstractInputMethod* self);

/* ==================== 类定义 ==================== */

/**
 * @brief      语言输入法公共基类对象；m_class 必须为第一个成员。
 * @details    m_data 私有块保存引擎/上下文反向引用（装配前为 NULL）；
 *             语言插件经结构体扩展继承本类型（m_data 后追加自有字段）。
 */
typedef struct XVirtualKeyboardAbstractInputMethod
{
    XObject m_class;   /**< 第一个成员，由 XObject 管理。 */
    void* m_data;      /**< 私有数据块（XVirtualKeyboardAbstractInput
                            MethodPrivate*），由对象拥有；仅供实现使用。 */
} XVirtualKeyboardAbstractInputMethod;

/**
 * @brief      初始化类虚函数表并返回共享表指针。
 * @return     类的共享 XVtable 指针。
 */
XVtable* XVirtualKeyboardAbstractInputMethod_class_init(void);

/**
 * @brief      初始化默认状态（未装配引擎/上下文，全部槽位为默认实现）。
 * @param      self 待初始化对象；必须与
 *             XVirtualKeyboardAbstractInputMethod_deinit_base 成对调用。
 */
void XVirtualKeyboardAbstractInputMethod_init(
        XVirtualKeyboardAbstractInputMethod* self);

/**
 * @brief      使用默认内存类型在堆上创建。
 * @return     新对象指针；失败返回 NULL，用 *_delete_base 释放。
 */
#define XVirtualKeyboardAbstractInputMethod_create() \
    XVirtualKeyboardAbstractInputMethod_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardAbstractInputMethod_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放资源（栈/外部存储对象使用）。 */
#define XVirtualKeyboardAbstractInputMethod_deinit_base(self) \
    XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上对象。 */
#define XVirtualKeyboardAbstractInputMethod_delete_base(self) \
    XClass_delete_base((XClass*)(self))

/* ==================== 反向引用（对标 Qt inputContext/inputEngine） ==================== */

/**
 * @brief      返回装配的输入上下文（对标 inputContext）。
 * @param      self 对象借用指针；可为 NULL。
 * @return     上下文借用指针（不转移所有权，不得释放）；未装配或
 *             self 为 NULL 返回 NULL。
 */
XVirtualKeyboardInputContext*
XVirtualKeyboardAbstractInputMethod_inputContext(
        const XVirtualKeyboardAbstractInputMethod* self);

/**
 * @brief      返回装配的输入引擎（对标 inputEngine）。
 * @param      self 对象借用指针；可为 NULL。
 * @return     引擎借用指针（不转移所有权，不得释放）；未装配或
 *             self 为 NULL 返回 NULL。
 */
XVirtualKeyboardInputEngine*
XVirtualKeyboardAbstractInputMethod_inputEngine(
        const XVirtualKeyboardAbstractInputMethod* self);

/* ==================== 虚函数公共调度入口（只查表分派，无业务逻辑） ==================== */

/**
 * @brief      inputModes 公共调度入口（对标纯虚 inputModes）。
 * @return     实际模式数；未注册槽位/入参非法返回 0。
 */
int XVirtualKeyboardAbstractInputMethod_inputModes_base(
        XVirtualKeyboardAbstractInputMethod* self, const char* locale,
        int* outModes, int maxCount);
/**
 * @brief      setInputMode 公共调度入口（只查表分派，无业务逻辑）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      locale 区域语言（UTF-8 借用；可为 NULL）。
 * @param      inputMode 目标输入模式枚举值。
 * @return     槽位已注册且实现接受返回 true；未注册槽位/入参非法返回
 *             false 且对象保持不变。
 */
bool XVirtualKeyboardAbstractInputMethod_setInputMode_base(
        XVirtualKeyboardAbstractInputMethod* self, const char* locale,
        int inputMode);
/**
 * @brief      setTextCase 公共调度入口（只查表分派，无业务逻辑）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      textCase 目标大小写模式枚举值。
 * @return     槽位已注册且实现接受返回 true；未注册槽位/入参非法返回
 *             false 且对象保持不变。
 */
bool XVirtualKeyboardAbstractInputMethod_setTextCase_base(
        XVirtualKeyboardAbstractInputMethod* self, int textCase);
/**
 * @brief      keyEvent 公共调度入口（只查表分派，无业务逻辑）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      key 键值。
 * @param      text 键文本（UTF-8 借用；可为 NULL）。
 * @param      modifiers 修饰位（可按位组合）。
 * @return     实现消费了按键返回 true；未注册槽位返回 false（不消费，
 *             交还键盘原语义）。
 */
bool XVirtualKeyboardAbstractInputMethod_keyEvent_base(
        XVirtualKeyboardAbstractInputMethod* self, int key, const char* text,
        uint32_t modifiers);
/**
 * @brief      selectionLists 公共调度入口（只查表分派，无业务逻辑）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      outTypes 调用方提供的存储空间（Type 值数组）；可为 NULL，
 *             NULL 时仅查询条数。
 * @param      maxCount outTypes 容量上限（条数）；outTypes 为 NULL 时
 *             忽略。
 * @return     实际模式/列表条数（最多写入 maxCount 条）；未注册槽位/
 *             入参非法返回 0。
 */
int XVirtualKeyboardAbstractInputMethod_selectionLists_base(
        XVirtualKeyboardAbstractInputMethod* self, int* outTypes,
        int maxCount);
/**
 * @brief      selectionListItemCount 公共调度入口（只查表分派）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      type 候选列表类型枚举值。
 * @return     该列表条目数；未注册槽位/入参非法返回 0。
 */
int XVirtualKeyboardAbstractInputMethod_selectionListItemCount_base(
        XVirtualKeyboardAbstractInputMethod* self, int type);
/**
 * @brief      selectionListData 公共调度入口；默认实现返回 NULL。
 * @return     新建 XVariant*；调用方负责释放。
 */
XVariant* XVirtualKeyboardAbstractInputMethod_selectionListData_base(
        XVirtualKeyboardAbstractInputMethod* self, int type, int index,
        int role);
/**
 * @brief      selectionListItemSelected 公共调度入口（默认空操作）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      type 候选列表类型枚举值。
 * @param      index 被选中条目索引。
 * @return     无返回值。
 */
void XVirtualKeyboardAbstractInputMethod_selectionListItemSelected_base(
        XVirtualKeyboardAbstractInputMethod* self, int type, int index);
/**
 * @brief      selectionListRemoveItem 公共调度入口（默认实现返回 false）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      type 候选列表类型枚举值。
 * @param      index 待移除条目索引。
 * @return     移除成功返回 true；未注册槽位/索引越界/入参非法返回
 *             false 且对象保持不变。
 */
bool XVirtualKeyboardAbstractInputMethod_selectionListRemoveItem_base(
        XVirtualKeyboardAbstractInputMethod* self, int type, int index);
/**
 * @brief      patternRecognitionModes 公共调度入口（默认实现返回 0）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      outModes 调用方提供的存储空间（模式值数组）；可为 NULL，
 *             NULL 时仅查询条数。
 * @param      maxCount outModes 容量上限（条数）；outModes 为 NULL 时
 *             忽略。
 * @return     实际支持的模式条数（最多写入 maxCount 条）；未注册槽位/
 *             入参非法返回 0。
 */
int XVirtualKeyboardAbstractInputMethod_patternRecognitionModes_base(
        XVirtualKeyboardAbstractInputMethod* self, int* outModes,
        int maxCount);
/**
 * @brief      traceBegin 公共调度入口；默认实现返回 NULL。
 * @return     新建 XVirtualKeyboardTrace*；调用方经引擎 traceEnd/释放
 *             契约处理。
 */
XVirtualKeyboardTrace* XVirtualKeyboardAbstractInputMethod_traceBegin_base(
        XVirtualKeyboardAbstractInputMethod* self, int traceId,
        int patternRecognitionMode);
/**
 * @brief      traceEnd 公共调度入口（默认实现返回 false）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      trace 轨迹对象（traceBegin 返回的所有权；可为 NULL）。
 * @return     识别受理返回 true；未注册槽位/trace 为 NULL 返回 false。
 */
bool XVirtualKeyboardAbstractInputMethod_traceEnd_base(
        XVirtualKeyboardAbstractInputMethod* self,
        XVirtualKeyboardTrace* trace);
/**
 * @brief      reselect 公共调度入口（默认实现返回 false）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      cursorPosition 光标位置（UTF-16 代码单元偏移）。
 * @param      reselectFlags 再选标志（可按位组合）。
 * @return     触发再选返回 true；未注册槽位/入参非法返回 false。
 */
bool XVirtualKeyboardAbstractInputMethod_reselect_base(
        XVirtualKeyboardAbstractInputMethod* self, int cursorPosition,
        uint32_t reselectFlags);
/**
 * @brief      clickPreeditText 公共调度入口（默认空操作）。
 * @param      self 对象借用指针；可为 NULL。
 * @param      cursorPosition 点击位置（UTF-16 代码单元偏移）。
 * @return     无返回值。
 */
void XVirtualKeyboardAbstractInputMethod_clickPreeditText_base(
        XVirtualKeyboardAbstractInputMethod* self, int cursorPosition);
/**
 * @brief      reset 公共调度入口（Qt 公共槽 reset；弃草稿收层）。
 * @param      self 对象借用指针；可为 NULL。
 * @return     无返回值。
 */
void XVirtualKeyboardAbstractInputMethod_reset_base(
        XVirtualKeyboardAbstractInputMethod* self);
/**
 * @brief      update 公共调度入口（Qt 公共槽 update；hints/位置刷新）。
 * @param      self 对象借用指针；可为 NULL。
 * @return     无返回值。
 */
void XVirtualKeyboardAbstractInputMethod_update_base(
        XVirtualKeyboardAbstractInputMethod* self);
/**
 * @brief      clearInputMode 公共调度入口（Qt 公共槽，REVISION(6,1)）。
 * @param      self 对象借用指针；可为 NULL。
 * @return     无返回值。
 */
void XVirtualKeyboardAbstractInputMethod_clearInputMode_base(
        XVirtualKeyboardAbstractInputMethod* self);

/* ==================== 信号（纯 ID getter，绝不在此发射） ==================== */

/**
 * @brief      selectionListChanged(int type) 信号标识（候选列表内容变
 *             化；对标 selectionListChanged）。引擎监听并刷新候选模型。
 * @param      self 对象借用指针；可为 NULL（被忽略）。
 * @param      type 仅占位（本 getter 不读取）。
 * @return     不透明的信号标识；不得解引用或释放。
 */
void* XVirtualKeyboardAbstractInputMethod_selectionListChanged_signal(
        XVirtualKeyboardAbstractInputMethod* self, int type);
/**
 * @brief      selectionListActiveItemChanged(int type, int index) 信号
 *             标识（高亮候选变化；对标同名信号）。
 * @param      self 对象借用指针；可为 NULL（被忽略）。
 * @param      type 仅占位（本 getter 不读取）。
 * @param      index 仅占位（本 getter 不读取）。
 * @return     不透明的信号标识；不得解引用或释放。
 */
void* XVirtualKeyboardAbstractInputMethod_selectionListActiveItemChanged_signal(
        XVirtualKeyboardAbstractInputMethod* self, int type, int index);
/**
 * @brief      selectionListsChanged() 信号标识（列表集合变化；引擎监
 *             听并重建候选模型，对标同名信号）。
 */
void* XVirtualKeyboardAbstractInputMethod_selectionListsChanged_signal(
        XVirtualKeyboardAbstractInputMethod* self);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDABSTRACTINPUTMETHOD_H */
