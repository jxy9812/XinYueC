/**
 * @file       XVirtualKeyboardInputEngine.h
 * @brief      XVirtualKeyboardInputEngine 输入引擎公开 API（对标 Qt 6.8
 *             QVirtualKeyboardInputEngine，qvirtualkeyboardinputengine.h
 *             全公共面）。
 * @details    对齐要点（Qt 6.8.3 实读口径）：
 *             - 继承 XObject；实例仅由 InputContext TU 创建（Qt 私有
 *               构造 :39 + QML_UNCREATABLE 口径；创建入口落保护头）；
 *             - 枚举逐值照抄（隐式 0..19/0..n，按名使用不硬编码数值）：
 *               TextCase{Lower,Upper}；InputMode 20 值{Latin,Numeric,
 *               Dialable,Pinyin,Cangjie,Zhuyin,Hangul,Hiragana,Katakana,
 *               FullwidthLatin,Greek,Cyrillic,Arabic,Hebrew,
 *               ChineseHandwriting,JapaneseHandwriting,KoreanHandwriting,
 *               Thai,Stroke,Romaji}；PatternRecognitionMode{None,
 *               PatternRecognitionDisabled=None,Handwriting（源码拼写
 *               HandwritingRecoginition 照抄）}；ReselectFlag{
 *               WordBeforeCursor=0x1,WordAfterCursor=0x2,
 *               WordAtCursor=WordBeforeCursor|WordAfterCursor}；
 *             - 虚键族：virtualKeyPress（repeat=true 开启长按重复——
 *               首 XVIRTUALKEYBOARD_REPEAT_FIRST_MS 后每
 *               XVIRTUALKEYBOARD_REPEAT_MS 一次，XObject 定时器承载，
 *               对标 qvirtualkeyboardinputengine.cpp:208-210/704-711）/
 *               virtualKeyCancel/virtualKeyRelease/virtualKeyClick；
 *               keyEvent 经当前输入法 keyEvent 槽分派，已消费返回 true；
 *             - traceBegin（返回 XVirtualKeyboardTrace*）/traceEnd/
 *               reselect/clickPreeditText 分派输入法对应槽；
 *             - 装配：setInputMethod 双向接线（qvirtualkeyboardinputengine.cpp
 *               :363-377 口径——旧插法 clearInputMode+断开候选信号，新
 *               插法 setInputEngine+连接 selectionListsChanged 刷新候选
 *               模型）；inputModes 由输入法 inputModes(locale) 填充；
 *             - 语言插件注册表：locale→工厂函数表（Protected 头承载，
 *               QML 插件机制的引擎内等价物；tcime/hangul/openwnn/thai
 *               预留点）；
 *             - 信号 11 个照抄 :121-132（逐行清点锚，非 12）。
 * @note       模块总开关 XVIRTUALKEYBOARD_ON；实现只依赖 XinYueC 抽象层。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XVIRTUALKEYBOARDINPUTENGINE_H
#define XVIRTUALKEYBOARDINPUTENGINE_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XObject.h"
#include "XMemory.h"

#if XVIRTUALKEYBOARD_ON

/* 前向声明。 */
typedef struct XVirtualKeyboardInputContext XVirtualKeyboardInputContext;
typedef struct XVirtualKeyboardAbstractInputMethod
    XVirtualKeyboardAbstractInputMethod;
typedef struct XVirtualKeyboardSelectionListModel
    XVirtualKeyboardSelectionListModel;
typedef struct XVirtualKeyboardTrace XVirtualKeyboardTrace;

/* ==================== 常量 ==================== */

/** @brief 长按重复起振延时（毫秒；对齐 Qt 引擎 600ms 口径；测试注入
 *         可覆写）。 */
#ifndef XVIRTUALKEYBOARD_REPEAT_FIRST_MS
#define XVIRTUALKEYBOARD_REPEAT_FIRST_MS 600
#endif
/** @brief 长按重复间隔（毫秒；对齐 Qt 引擎稳态 50ms 口径；测试注入可
 *         覆写）。 */
#ifndef XVIRTUALKEYBOARD_REPEAT_MS
#define XVIRTUALKEYBOARD_REPEAT_MS 50
#endif

/* ==================== 枚举 ==================== */

/**
 * @brief      大小写（对标 TextCase）。
 */
typedef enum XVirtualKeyboardInputEngineTextCase
{
    XVirtualKeyboardInputEngineTextCase_Lower = 0, /**< 小写。 */
    XVirtualKeyboardInputEngineTextCase_Upper      /**< 大写。 */
} XVirtualKeyboardInputEngineTextCase;

/**
 * @brief      输入模式（对标 InputMode；隐式值 0..19 照抄源码顺序，
 *             一律按枚举名使用，不硬编码数值——跨版本漂移防护）。
 */
typedef enum XVirtualKeyboardInputEngineInputMode
{
    XVirtualKeyboardInputEngineInputMode_Latin = 0,              /**< 拉丁。 */
    XVirtualKeyboardInputEngineInputMode_Numeric,                /**< 数字。 */
    XVirtualKeyboardInputEngineInputMode_Dialable,               /**< 可拨号。 */
    XVirtualKeyboardInputEngineInputMode_Pinyin,                 /**< 拼音。 */
    XVirtualKeyboardInputEngineInputMode_Cangjie,                /**< 仓颉。 */
    XVirtualKeyboardInputEngineInputMode_Zhuyin,                 /**< 注音。 */
    XVirtualKeyboardInputEngineInputMode_Hangul,                 /**< 谚文。 */
    XVirtualKeyboardInputEngineInputMode_Hiragana,               /**< 平假名。 */
    XVirtualKeyboardInputEngineInputMode_Katakana,               /**< 片假名。 */
    XVirtualKeyboardInputEngineInputMode_FullwidthLatin,         /**< 全角拉丁。 */
    XVirtualKeyboardInputEngineInputMode_Greek,                  /**< 希腊。 */
    XVirtualKeyboardInputEngineInputMode_Cyrillic,               /**< 西里尔。 */
    XVirtualKeyboardInputEngineInputMode_Arabic,                 /**< 阿拉伯。 */
    XVirtualKeyboardInputEngineInputMode_Hebrew,                 /**< 希伯来。 */
    XVirtualKeyboardInputEngineInputMode_ChineseHandwriting,     /**< 中文手写。 */
    XVirtualKeyboardInputEngineInputMode_JapaneseHandwriting,    /**< 日文手写。 */
    XVirtualKeyboardInputEngineInputMode_KoreanHandwriting,      /**< 韩文手写。 */
    XVirtualKeyboardInputEngineInputMode_Thai,                   /**< 泰文。 */
    XVirtualKeyboardInputEngineInputMode_Stroke,                 /**< 笔画。 */
    XVirtualKeyboardInputEngineInputMode_Romaji                  /**< 罗马字。 */
} XVirtualKeyboardInputEngineInputMode;

/**
 * @brief      模式识别模式（对标 PatternRecognitionMode；"Recoginition"
 *             拼写照抄 Qt 源码）。
 */
typedef enum XVirtualKeyboardInputEnginePatternRecognitionMode
{
    XVirtualKeyboardInputEnginePatternRecognitionMode_None = 0,
    /**< 无。 */
    XVirtualKeyboardInputEnginePatternRecognitionMode_PatternRecognitionDisabled =
        XVirtualKeyboardInputEnginePatternRecognitionMode_None,
    /**< 禁用（=None，Qt 别名照抄）。 */
    XVirtualKeyboardInputEnginePatternRecognitionMode_Handwriting = 1,
    /**< 手写。 */
    XVirtualKeyboardInputEnginePatternRecognitionMode_HandwritingRecoginition =
        XVirtualKeyboardInputEnginePatternRecognitionMode_Handwriting
    /**< 手写（Qt 源码拼写别名照抄）。 */
} XVirtualKeyboardInputEnginePatternRecognitionMode;

/**
 * @brief      再选标志（对标 ReselectFlag；可按位或）。
 */
typedef enum XVirtualKeyboardInputEngineReselectFlag
{
    XVirtualKeyboardInputEngineReselectFlag_WordBeforeCursor = 0x1,
    /**< 光标前词。 */
    XVirtualKeyboardInputEngineReselectFlag_WordAfterCursor = 0x2,
    /**< 光标后词。 */
    XVirtualKeyboardInputEngineReselectFlag_WordAtCursor =
        XVirtualKeyboardInputEngineReselectFlag_WordBeforeCursor |
        XVirtualKeyboardInputEngineReselectFlag_WordAfterCursor
    /**< 光标处词（前后并集）。 */
} XVirtualKeyboardInputEngineReselectFlag;

/** @brief 再选标志位集合（对标 Q_FLAG ReselectFlags）。 */
typedef uint32_t XVirtualKeyboardInputEngineReselectFlags;

/* ==================== 类定义 ==================== */

/** @brief 声明 XVirtualKeyboardInputEngine 虚函数枚举：继承 XObject
 *         （无新增槽位；TimerEvent 重载沿用基类槽位）。 */
XCLASS_DEFINE_BEGING(XVirtualKeyboardInputEngine)
XCLASS_DEFINE_EXTEND_END(XVirtualKeyboardInputEngine, XObject)

/**
 * @brief      输入引擎对象；m_class 必须为第一个成员。
 * @details    m_data 私有块保存上下文反向引用/当前输入法/输入模式集/
 *             候选模型/重复定时器/locale 工厂注册表。
 */
typedef struct XVirtualKeyboardInputEngine
{
    XObject m_class;   /**< 第一个成员，由 XObject 管理。 */
    void* m_data;      /**< 私有数据块，由对象拥有；仅供实现使用。 */
} XVirtualKeyboardInputEngine;

/**
 * @brief      初始化类虚函数表并返回共享表指针。
 * @return     类共享虚函数表指针（进程期常驻，借用）；不失败。
 */
XVtable* XVirtualKeyboardInputEngine_class_init(void);

/* ==================== 生命周期（所有权边界） ==================== */

/**
 * @brief      通过 XClass 虚表反初始化（栈/外部存储对象使用）。
 * @details    创建入口在 Protected 头（仅 InputContext TU 构造），
 *             归属方（XVirtualKeyboardInputContext）在自身 TU 内释放，
 *             故释放接口必须在公开头声明。
 */
#define XVirtualKeyboardInputEngine_deinit_base(self) \
    XClass_deinit_base((XClass*)(self))
/**
 * @brief      删除堆上对象（归属方释放自有引擎用；释放后指针失效）。
 */
#define XVirtualKeyboardInputEngine_delete_base(self) \
    XClass_delete_base((XClass*)(self))

/* ==================== 虚键族（Q_INVOKABLE 对标） ==================== */

/**
 * @brief      虚键按下（对标 virtualKeyPress）。
 * @details    记录活动键；repeat=true 且 keyEvent 未被输入法消费时启
 *             动长按重复（首 600ms 后 50ms/次；每次重复发
 *             virtualKeyClicked(isAutoRepeat=true) 并重投输入法）。
 * @param      self 引擎对象；可为 NULL。
 * @param      key 键值（XKey_* 或 ASCII 字符码）。
 * @param      text 键文本（UTF-8 借用，调用期间有效；可为 NULL）。
 * @param      modifiers 修饰位（XKeyboardModifier 位集）。
 * @param      repeat true=允许长按重复。
 * @return     无。
 */
void XVirtualKeyboardInputEngine_virtualKeyPress(
        XVirtualKeyboardInputEngine* self, int key, const char* text,
        uint32_t modifiers, bool repeat);

/**
 * @brief      取消虚键按下（对标 virtualKeyCancel；杀重复计时清活动键）。
 * @param      self 引擎对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XVirtualKeyboardInputEngine_virtualKeyCancel(
        XVirtualKeyboardInputEngine* self);

/**
 * @brief      虚键释放（对标 virtualKeyRelease；活动键转先前键并杀
 *             重复计时）。
 * @param      self 引擎对象；可为 NULL。
 * @param      key 键值（须与活动键一致；不一致仅按无活动键处理）。
 * @param      text 键文本（UTF-8 借用；可为 NULL）。
 * @param      modifiers 修饰位（可按位组合）。
 * @return     存在活动键返回 true；无活动键/入参非法返回 false。
 */
bool XVirtualKeyboardInputEngine_virtualKeyRelease(
        XVirtualKeyboardInputEngine* self, int key, const char* text,
        uint32_t modifiers);

/**
 * @brief      虚键单击（对标 virtualKeyClick）。
 * @details    keyEvent 投当前输入法；已消费或无输入法时发
 *             virtualKeyClicked（isAutoRepeat=false）。
 * @param      self 引擎对象；可为 NULL。
 * @param      key 键值（XKey_* 或 ASCII 字符码）。
 * @param      text 键文本（UTF-8 借用；可为 NULL）。
 * @param      modifiers 修饰位（可按位组合）。
 * @return     按键已被处理返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardInputEngine_virtualKeyClick(
        XVirtualKeyboardInputEngine* self, int key, const char* text,
        uint32_t modifiers);

/**
 * @brief      开始一笔手写轨迹（对标 traceBegin）。
 * @param      self 引擎对象；可为 NULL。
 * @param      traceId 轨迹标识（调用方定义）。
 * @param      patternRecognitionMode 识别模式枚举值。
 * @return     新建 XVirtualKeyboardTrace*（无识别引擎/无输入法返回
 *             NULL）；不支持的识别模式返回 NULL。所有权归调用方装配
 *             链，用毕经 traceEnd/释放契约处理。
 */
XVirtualKeyboardTrace* XVirtualKeyboardInputEngine_traceBegin(
        XVirtualKeyboardInputEngine* self, int traceId,
        int patternRecognitionMode);

/**
 * @brief      结束一笔并送识别（对标 traceEnd）。
 * @details    转发输入法 traceEnd 槽；调用方保留轨迹对象所有权（用毕
 *             *_delete_base）。
 * @param      self 引擎对象；可为 NULL。
 * @param      trace 轨迹对象（traceBegin 返回；可为 NULL）。
 * @return     识别已受理返回 true；trace 为 NULL/未注册槽位返回 false。
 */
bool XVirtualKeyboardInputEngine_traceEnd(XVirtualKeyboardInputEngine* self,
                                          XVirtualKeyboardTrace* trace);

/**
 * @brief      再选光标处词（对标 reselect）。
 * @param      self 引擎对象；可为 NULL。
 * @param      cursorPosition 光标位置（UTF-16 代码单元偏移）。
 * @param      reselectFlags 再选标志位集合（可按位组合）。
 * @return     输入法已再选返回 true；未装配输入法/入参非法返回 false。
 */
bool XVirtualKeyboardInputEngine_reselect(XVirtualKeyboardInputEngine* self,
                                          int cursorPosition,
                                          XVirtualKeyboardInputEngineReselectFlags reselectFlags);

/**
 * @brief      组串点击（对标 clickPreeditText；转发输入法）。
 * @param      self 引擎对象；可为 NULL，NULL 时不执行操作。
 * @param      cursorPosition 点击位置（UTF-16 代码单元偏移）。
 * @return     无返回值。
 */
void XVirtualKeyboardInputEngine_clickPreeditText(
        XVirtualKeyboardInputEngine* self, int cursorPosition);

/* ==================== 状态查询与装配 ==================== */

/**
 * @brief      返回装配的输入上下文（对标 inputContext；借用）。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     上下文借用指针（所有权在上下文侧，不得释放）；未装配
 *             或 self 为 NULL 返回 NULL。
 */
XVirtualKeyboardInputContext* XVirtualKeyboardInputEngine_inputContext(
        const XVirtualKeyboardInputEngine* self);

/**
 * @brief      返回活动键（按下中；对标 activeKey）。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     活动键键值；无活动键或 self 为 NULL 返回 0。
 */
int XVirtualKeyboardInputEngine_activeKey(
        const XVirtualKeyboardInputEngine* self);

/**
 * @brief      返回先前键（对标 previousKey）。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     先前键键值；无先前键或 self 为 NULL 返回 0。
 */
int XVirtualKeyboardInputEngine_previousKey(
        const XVirtualKeyboardInputEngine* self);

/**
 * @brief      返回当前输入法（对标 inputMethod；借用）。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     输入法借用指针（引擎不持有所有权，不得释放）；未装配
 *             或 self 为 NULL 返回 NULL。
 */
XVirtualKeyboardAbstractInputMethod*
XVirtualKeyboardInputEngine_inputMethod(
        const XVirtualKeyboardInputEngine* self);

/**
 * @brief      装配当前输入法（对标 setInputMethod；仅上下文/面板 TU
 *             装配链调用——Qt 由布局层赋值，本实现同等语义）。
 * @details    双向接线：旧输入法 clearInputMode + 断开候选信号连接，
 *             新输入法 setInputEngine/setInputContext + 连接
 *             selectionListsChanged；随后刷新输入模式集与候选模型数据
 *             源并发 inputMethodChanged。
 * @param      self 引擎对象；可为 NULL。
 * @param      inputMethod 输入法对象（引擎不持有所有权；NULL 允许，
 *             等价卸载）。
 * @return     装配成功返回 true。
 */
bool XVirtualKeyboardInputEngine_setInputMethod(
        XVirtualKeyboardInputEngine* self,
        XVirtualKeyboardAbstractInputMethod* inputMethod);

/**
 * @brief      查询支持的输入模式集（对标 inputModes；由当前输入法填
 *             充，locale 变化/输入法装配时刷新）。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @param      outModes 输出数组（调用方提供存储空间；可为 NULL 仅询数）。
 * @param      maxCount 输出数组容量。
 * @return     实际模式数。
 */
int XVirtualKeyboardInputEngine_inputModes(
        const XVirtualKeyboardInputEngine* self, int* outModes,
        int maxCount);

/**
 * @brief      返回当前输入模式（对标 inputMode）。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     当前输入模式枚举值；self 为 NULL 返回 0。
 */
int XVirtualKeyboardInputEngine_inputMode(
        const XVirtualKeyboardInputEngine* self);

/**
 * @brief      切换输入模式（对标 setInputMode）。
 * @details    模式不在当前输入模式集内时拒绝（Qt qWarning 口径，本实
 *             现 XPrintf 诊断）返回 false；否则委托输入法
 *             setInputMode(locale, mode)，成功后刷新并发
 *             inputModeChanged。
 * @param      self 引擎对象；可为 NULL。
 * @param      inputMode 目标输入模式枚举值。
 * @return     切换成功返回 true；模式不在当前模式集内/委托失败/self
 *             为 NULL 返回 false 且对象保持不变。
 */
bool XVirtualKeyboardInputEngine_setInputMode(
        XVirtualKeyboardInputEngine* self,
        XVirtualKeyboardInputEngineInputMode inputMode);

/**
 * @brief      返回候选列表模型（对标 wordCandidateListModel；引擎持
 *             有，调用方不得释放）。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     模型借用指针（引擎拥有，不得释放）；未创建或 self 为
 *             NULL 返回 NULL。
 */
XVirtualKeyboardSelectionListModel*
XVirtualKeyboardInputEngine_wordCandidateListModel(
        const XVirtualKeyboardInputEngine* self);

/**
 * @brief      返回候选列表可见提示（对标 wordCandidateListVisibleHint）。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     提示可见返回 true；self 为 NULL 返回 false。
 */
bool XVirtualKeyboardInputEngine_wordCandidateListVisibleHint(
        const XVirtualKeyboardInputEngine* self);

/**
 * @brief      查询支持的识别模式集（对标 patternRecognitionModes）。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @param      outModes 输出数组（调用方提供存储空间；可为 NULL，NULL
 *             时仅查询条数）。
 * @param      maxCount outModes 容量上限（条数）；outModes 为 NULL 时
 *             忽略。
 * @return     实际模式数（由当前输入法申报；默认 0）。
 */
int XVirtualKeyboardInputEngine_patternRecognitionModes(
        const XVirtualKeyboardInputEngine* self, int* outModes,
        int maxCount);

/* ==================== 复位/更新（引擎级） ==================== */

/**
 * @brief      复位输入法状态（Qt 私有槽 reset 的引擎级入口：转发输入
 *             法 reset 槽并发 inputMethodReset）。
 * @details    面板 closePopup 弃草稿路径经此调用（对齐 Qt hide/reset
 *             链）；不切换输入模式。
 * @param      self 引擎对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XVirtualKeyboardInputEngine_reset(XVirtualKeyboardInputEngine* self);

/**
 * @brief      通知输入法外部变化（Qt 私有槽 update 的引擎级入口：转
 *             发输入法 update 槽并发 inputMethodUpdate）。
 * @param      self 引擎对象；可为 NULL，NULL 时不执行操作。
 * @return     无返回值。
 */
void XVirtualKeyboardInputEngine_update(XVirtualKeyboardInputEngine* self);

/* ==================== 信号（11 个，纯 ID getter，对标 :121-132） ==================== */

/**
 * @brief      virtualKeyClicked(key,text,modifiers,isAutoRepeat) 信号
 *             标识（虚键已点击；面板按键语义回退路径消费）。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @param      key 仅占位（本 getter 不读取）。
 * @param      text 仅占位（本 getter 不读取）。
 * @param      modifiers 仅占位（本 getter 不读取）。
 * @param      isAutoRepeat 仅占位（本 getter 不读取）。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_virtualKeyClicked_signal(
        XVirtualKeyboardInputEngine* self, int key, const char* text,
        uint32_t modifiers, bool isAutoRepeat);
/**
 * @brief      activeKeyChanged(int key) 信号标识。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_activeKeyChanged_signal(
        XVirtualKeyboardInputEngine* self, int key);
/**
 * @brief      previousKeyChanged(int key) 信号标识。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_previousKeyChanged_signal(
        XVirtualKeyboardInputEngine* self, int key);
/**
 * @brief      inputMethodChanged() 信号标识。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_inputMethodChanged_signal(
        XVirtualKeyboardInputEngine* self);
/**
 * @brief      inputMethodReset() 信号标识。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_inputMethodReset_signal(
        XVirtualKeyboardInputEngine* self);
/**
 * @brief      inputMethodUpdate() 信号标识。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_inputMethodUpdate_signal(
        XVirtualKeyboardInputEngine* self);
/**
 * @brief      inputModesChanged() 信号标识。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_inputModesChanged_signal(
        XVirtualKeyboardInputEngine* self);
/**
 * @brief      inputModeChanged() 信号标识。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_inputModeChanged_signal(
        XVirtualKeyboardInputEngine* self);
/**
 * @brief      patternRecognitionModesChanged() 信号标识。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_patternRecognitionModesChanged_signal(
        XVirtualKeyboardInputEngine* self);
/**
 * @brief      wordCandidateListModelChanged() 信号标识。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_wordCandidateListModelChanged_signal(
        XVirtualKeyboardInputEngine* self);
/**
 * @brief      wordCandidateListVisibleHintChanged() 信号标识。
 * @param      self 引擎对象借用指针；可为 NULL。
 * @return     信号标识借用指针（内部存储，不得释放）；self 为 NULL
 *             返回 NULL。
 */
void* XVirtualKeyboardInputEngine_wordCandidateListVisibleHintChanged_signal(
        XVirtualKeyboardInputEngine* self);

#endif /* XVIRTUALKEYBOARD_ON */

#ifdef __cplusplus
}
#endif
#endif /* XVIRTUALKEYBOARDINPUTENGINE_H */
