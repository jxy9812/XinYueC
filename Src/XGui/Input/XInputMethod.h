/******************************************************************************
 * @file       XInputMethod.h
 * @brief      XInputMethod 输入法类（对标 Qt 6.8 QInputMethod 全部公开 API）。
 * @details    XInputMethod 继承 XObject，是进程内输入法访问入口，由
 *             XGuiApplication_inputMethod() 惰性创建并拥有：
 *             - 输入项状态：输入项变换（3×3 仿射矩阵）/ 输入项矩形 /
 *               裁剪矩形，关联光标/锚点/键盘矩形；
 *             - 虚拟键盘控制：isVisible / setVisible / show / hide /
 *               isAnimating；
 *             - 输入状态刷新：update / reset / commit / invokeAction；
 *             - 区域与方向：locale / inputDirection；
 *             - 静态查询 queryFocusObject（通过可注册的 C 回调访问焦点对象）；
 *             - 全部 8 个通知信号：cursorRectangleChanged /
 *               anchorRectangleChanged / keyboardRectangleChanged /
 *               inputItemClipRectangleChanged / visibleChanged /
 *               animatingChanged / localeChanged / inputDirectionChanged。
 *             网络层转发：show/hide/update/reset/commit/invokeAction 及全部
 *             状态查询在 XPLATFORMINPUTCTX_ON 时委托给绑定的
 *             XPlatformInputContext（嵌入式无系统 IME 的空后端）；未绑定时
 *             使用对象内部默认值（矩形为零、不可见、不动画、方向 LTR）。
 *             矩形计算说明：光标/锚点/裁剪矩形按 Qt 语义查询当前焦点对象，
 *             将回调返回的 XRectF 按输入项变换映射到窗口坐标；未提供属性时
 *             返回零矩形。输入项矩形仍由 setInputItemRectangle 单独维护。
 * @note       模块开关 XINPUTMETHOD_ON 定义于 XGuiConfig.h；置 0 时裁剪
 *             整个 XInputMethod 公共 API，XGuiApplication_inputMethod()
 *             返回 NULL。
 * @author     XinYueC 团队
 ******************************************************************************/
#ifndef XINPUTMETHOD_H
#define XINPUTMETHOD_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XClass.h"
#include "XObject.h"
#include "XMemory.h"
#include "XGeometry.h"
#include "XString.h"
#include "XVariant.h"
/** @brief 平台输入上下文前向声明；XPLATFORMINPUTCTX_ON 时由
 * XPlatformInputContext.h 提供完整定义，此处避免循环包含。 */
typedef struct XPlatformInputContext XPlatformInputContext;

#if XINPUTMETHOD_ON

/** @brief 私有实现前向声明；仅供实现访问。 */
typedef struct XInputMethodPrivate XInputMethodPrivate;/** @brief 声明 XInputMethod 虚函数枚举：继承 XObject（无新增槽位）。 */
XCLASS_DEFINE_BEGING(XInputMethod)
XCLASS_DEFINE_EXTEND_END(XInputMethod, XObject)



/** @brief 输入项动作（对标 Qt 6.8 QInputMethod::Action，取值一致）。 */
typedef enum XInputMethodAction
{
    XInputMethodAction_Click = 0,       /**< 普通点击/轻触。 */
    XInputMethodAction_ContextMenu      /**< 右键/长按上下文菜单点击。 */
} XInputMethodAction;

/** @brief 输入法查询项（对标 Qt 6.8 Qt::InputMethodQuery，取值一致；可位或）。 */
typedef enum XInputMethodQuery
{
    XInputMethodQuery_ImEnabled = 0x00000001,         /**< 输入法是否可用。 */
    XInputMethodQuery_ImCursorRectangle = 0x00000002, /**< 光标矩形。 */
    XInputMethodQuery_ImFont = 0x00000004,            /**< 输入字体。 */
    XInputMethodQuery_ImCursorPosition = 0x00000008,  /**< 光标位置。 */
    XInputMethodQuery_ImSurroundingText = 0x00000010, /**< 环绕文本。 */
    XInputMethodQuery_ImCurrentSelection = 0x00000020,/**< 当前选区。 */
    XInputMethodQuery_ImMaximumTextLength = 0x00000040, /**< 最大文本长度。 */
    XInputMethodQuery_ImAnchorPosition = 0x00000080,  /**< 锚点位置。 */
    XInputMethodQuery_ImHints = 0x00000100,           /**< 输入提示位。 */
    XInputMethodQuery_ImPreferredLanguage = 0x00000200, /**< 首选语言。 */
    XInputMethodQuery_ImAbsolutePosition = 0x00000400,  /**< 绝对位置。 */
    XInputMethodQuery_ImTextBeforeCursor = 0x00000800,  /**< 光标前文本。 */
    XInputMethodQuery_ImTextAfterCursor = 0x00001000,   /**< 光标后文本。 */
    XInputMethodQuery_ImEnterKeyType = 0x00002000,      /**< 回车键类型。 */
    XInputMethodQuery_ImAnchorRectangle = 0x00004000,   /**< 锚点矩形。 */
    XInputMethodQuery_ImInputItemClipRectangle = 0x00008000, /**< 输入项裁剪矩形。 */
    XInputMethodQuery_ImReadOnly = 0x00010000,          /**< 只读标志。 */
    XInputMethodQuery_ImPlatformData = 0x80000000,      /**< 平台私有数据。 */
    XInputMethodQuery_ImQueryInput = (0x00000002 | 0x00000008 | 0x00000010 |
                                      0x00000020 | 0x00000040 | 0x00000080),
    /**< 输入相关分组：光标矩形+光标位置+环绕文本+当前选区+锚点矩形+锚点位置。 */
    XInputMethodQuery_ImQueryAll = 0xffffffff           /**< 全部查询项。 */
} XInputMethodQuery;

/** @brief 输入法查询项位集合（对标 Qt::InputMethodQueries）。 */
typedef uint32_t XInputMethodQueries;

/**
 * @brief      焦点对象输入法查询回调（对标 QWidget::inputMethodQuery）。
 * @details    回调返回新建 XVariant，所有权转移给调用者；返回 NULL 表示
 *             焦点对象没有该属性。focusObject 为当前焦点对象借用指针，
 *             userData 为注册时传入的借用上下文。矩形查询项应返回
 *             `XVariantType_User` 且 payload 为 `XRectF`；ImEnabled 返回布尔变体。
 */
typedef XVariant* (*XInputMethodQueryHandler)(XObject* focusObject,
                                              XInputMethodQuery query,
                                              const XVariant* argument,
                                              void* userData);

/** @brief 输入方向（对标 Qt 6.8 Qt::LayoutDirection，取值一致）。 */
typedef enum XInputMethodLayoutDirection
{
    XInputMethodLayoutDirection_LeftToRight = 0, /**< 从左到右。 */
    XInputMethodLayoutDirection_RightToLeft,     /**< 从右到左。 */
    XInputMethodLayoutDirection_Auto             /**< 自动（跟随区域设置）。 */
} XInputMethodLayoutDirection;

/**
 * @brief      3×3 仿射变换矩阵（对标 Qt 6.8 QTransform 数据布局）。
 * @details    行主序存储 m11..m33，与 QTransform(m11,m12,m13,m21,m22,m23,
 *             m31,m32,m33) 对齐；单位矩阵为对角全 1。用于把输入项坐标
 *             变换到窗口坐标。
 */
typedef struct XInputMethodTransform
{
    float xm11; /**< 第 1 行第 1 列。 */
    float xm12; /**< 第 1 行第 2 列。 */
    float xm13; /**< 第 1 行第 3 列（平移 x）。 */
    float xm21; /**< 第 2 行第 1 列。 */
    float xm22; /**< 第 2 行第 2 列。 */
    float xm23; /**< 第 2 行第 3 列（平移 y）。 */
    float xm31; /**< 第 3 行第 1 列。 */
    float xm32; /**< 第 3 行第 2 列。 */
    float xm33; /**< 第 3 行第 3 列（透视分母，恒 1）。 */
} XInputMethodTransform;

/**
 * @brief      将 t 置为单位矩阵（对角 1，其余 0）。
 * @param      t 目标矩阵；可为 NULL。
 */
void XInputMethodTransform_identity(XInputMethodTransform* t);

/**
 * @brief      XInputMethod 输入法对象；m_class 必须为第一个成员。
 * @details    输入项状态与平台上下文借用指针保存在 m_data 私有块中。
 */
typedef struct XInputMethod
{
    XObject              m_class; /**< 第一个成员，由 XObject 管理。 */
    XInputMethodPrivate* m_data;  /**< 私有数据块，由 XInputMethod 拥有。 */
} XInputMethod;

/**
 * @brief      初始化 XInputMethod 类虚函数表并返回共享表指针。
 * @return     XInputMethod 类的共享 XVtable 指针。
 */
XVtable* XInputMethod_class_init(void);

/**
 * @brief      初始化默认状态的 XInputMethod。
 * @details    默认输入项变换为单位矩阵、输入项矩形为零矩形、未绑定平台
 *             输入上下文。
 * @param      self 待初始化对象；必须与 XInputMethod_deinit_base 成对调用。
 */
void XInputMethod_init(XInputMethod* self);

/**
 * @brief      使用默认内存类型在堆上创建 XInputMethod。
 * @return     新对象指针；失败返回 NULL，调用方用 XInputMethod_delete_base 释放。
 */
#define XInputMethod_create() XInputMethod_create_ex(XCLASS_DEFAULT_MEMORY_TYPE)

/**
 * @brief      使用指定内存类型在堆上创建 XInputMethod。
 * @param      memory 对象内存类型。
 * @return     新对象指针；失败返回 NULL。
 */
XInputMethod* XInputMethod_create_ex(XMemoryType memory);

/** @brief 通过 XClass 虚表释放 XInputMethod 资源（栈/外部存储对象使用）。 */
#define XInputMethod_deinit_base(self) XClass_deinit_base((XClass*)(self))
/** @brief 删除堆上的 XInputMethod 对象。 */
#define XInputMethod_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 平台上下文绑定（网络层） ==================== */

/**
 * @brief      绑定平台输入上下文（对标 QInputMethodPrivate 的平台后端关联）。
 * @details    借用不持有；由 XGuiApplication_inputMethod() 创建时自动绑定
 *             到 XPlatformInputContext。置 NULL 可解除绑定。
 * @param      self 目标对象；可为 NULL。
 * @param      context 平台输入上下文借用指针；可为 NULL。
 */
void XInputMethod_setPlatformContext(XInputMethod* self,
                                     XPlatformInputContext* context);

/**
 * @brief      返回当前绑定的平台输入上下文。
 * @return     借用指针；未绑定返回 NULL。
 */
XPlatformInputContext* XInputMethod_platformContext(const XInputMethod* self);

/** @brief 设置/清除焦点对象输入法查询回调。 */
void XInputMethod_setQueryHandler(XInputMethod* self,
                                  XInputMethodQueryHandler handler,
                                  void* userData);

/* ==================== 输入项状态（对标 QInputMethod） ==================== */

/**
 * @brief      返回输入项变换（对标 QInputMethod::inputItemTransform）。
 * @return     当前仿射矩阵副本。
 */
XInputMethodTransform XInputMethod_inputItemTransform(const XInputMethod* self);

/**
 * @brief      设置输入项变换（对标 QInputMethod::setInputItemTransform）。
 * @details    与 Qt 一致：变换变化时依次发射 cursorRectangleChanged /
 *             anchorRectangleChanged。rect 为 NULL 时复位为单位矩阵。
 * @param      self 目标对象；可为 NULL。
 * @param      transform 新变换；可为 NULL。
 */
void XInputMethod_setInputItemTransform(XInputMethod* self,
                                        const XInputMethodTransform* transform);

/**
 * @brief      返回输入项几何（输入项坐标，对标 QInputMethod::inputItemRectangle）。
 * @return     输入项矩形副本。
 */
XRectF XInputMethod_inputItemRectangle(const XInputMethod* self);

/**
 * @brief      设置输入项几何（输入项坐标，对标 QInputMethod::setInputItemRectangle）。
 * @param      self 目标对象；可为 NULL。
 * @param      rect 新矩形；NULL 按零矩形处理。
 */
void XInputMethod_setInputItemRectangle(XInputMethod* self, const XRectF* rect);

/**
 * @brief      返回输入项裁剪矩形（对标 QInputMethod::inputItemClipRectangle）。
 * @details    通过 XInputMethodQueryHandler 查询 ImInputItemClipRectangle，
 *             再按输入项变换映射到窗口坐标；无有效查询结果返回零矩形。
 * @return     窗口坐标下的裁剪矩形副本。
 */
XRectF XInputMethod_inputItemClipRectangle(const XInputMethod* self);

/**
 * @brief      返回光标矩形（对标 QInputMethod::cursorRectangle）。
 * @details    通过 XInputMethodQueryHandler 查询 ImCursorRectangle，再按
 *             输入项变换映射到窗口坐标；无有效查询结果返回零矩形。
 * @return     窗口坐标下的光标矩形副本。
 */
XRectF XInputMethod_cursorRectangle(const XInputMethod* self);

/**
 * @brief      返回锚点矩形（对标 QInputMethod::anchorRectangle）。
 * @details    通过 XInputMethodQueryHandler 查询 ImAnchorRectangle，再按
 *             输入项变换映射到窗口坐标；无有效查询结果返回零矩形。
 * @return     窗口坐标下的锚点矩形副本。
 */
XRectF XInputMethod_anchorRectangle(const XInputMethod* self);

/**
 * @brief      返回虚拟键盘矩形（对标 QInputMethod::keyboardRectangle）。
 * @return     窗口坐标下的键盘矩形副本；平台无键盘或未绑定上下文时为零矩形。
 */
XRectF XInputMethod_keyboardRectangle(const XInputMethod* self);

/* ==================== 可见性 / 动画（对标 QInputMethod） ==================== */

/**
 * @brief      虚拟键盘是否可见（对标 QInputMethod::isVisible）。
 * @return     true 可见；无平台上下文恒 false。
 */
bool XInputMethod_isVisible(const XInputMethod* self);

/**
 * @brief      设置虚拟键盘可见性（对标 QInputMethod::setVisible）。
 * @param      self 目标对象；可为 NULL。
 * @param      visible true 等价 show()、false 等价 hide()。
 */
void XInputMethod_setVisible(XInputMethod* self, bool visible);

/**
 * @brief      虚拟键盘是否正在动画（对标 QInputMethod::isAnimating）。
 * @return     true 动画中；无平台上下文恒 false。
 */
bool XInputMethod_isAnimating(const XInputMethod* self);

/**
 * @brief      当前输入区域语言（对标 QInputMethod::locale）。
 * @return     新建 XString（IETF 语言标签，默认 "C"）；调用方用
 *             XString_delete_base 释放。
 */
XString* XInputMethod_locale(const XInputMethod* self);

/**
 * @brief      当前输入方向（对标 QInputMethod::inputDirection）。
 * @return     方向枚举；无平台上下文恒 LeftToRight。
 */
XInputMethodLayoutDirection XInputMethod_inputDirection(const XInputMethod* self);

/* ==================== 静态查询（对标 QInputMethod::queryFocusObject） ==================== */

/**
 * @brief      向焦点对象查询输入法属性（对标 QInputMethod::queryFocusObject）。
 * @details    调用已注册的 XInputMethodQueryHandler；未注册或回调返回空时
 *             与 Qt 焦点对象未响应查询的无效 QVariant 语义一致。
 * @param      query 查询项。
 * @param      argument 查询参数；可为 NULL。
 * @return     回调新建的 XVariant；无结果返回 NULL，调用方负责释放。
 */
XVariant* XInputMethod_queryFocusObject(XInputMethodQuery query,
                                        const XVariant* argument);

/* ==================== 输入法动作（对标 QInputMethod 槽函数） ==================== */

/**
 * @brief      请求打开虚拟键盘（对标 QInputMethod::show）。
 * @param      self 目标对象；可为 NULL。
 */
void XInputMethod_show(XInputMethod* self);

/**
 * @brief      请求关闭虚拟键盘（对标 QInputMethod::hide）。
 * @param      self 目标对象；可为 NULL。
 */
void XInputMethod_hide(XInputMethod* self);

/**
 * @brief      通知平台输入法编辑状态变化（对标 QInputMethod::update）。
 * @details    与 Qt 一致：queries 含 ImCursorRectangle 时发
 *             cursorRectangleChanged、含 ImAnchorRectangle 时发
 *             anchorRectangleChanged、含 ImInputItemClipRectangle 时发
 *             inputItemClipRectangleChanged，并转发
 *             context->update(queries)。
 * @param      self 目标对象；可为 NULL。
 * @param      queries 变化的查询项位集合。
 */
void XInputMethod_update(XInputMethod* self, XInputMethodQueries queries);

/**
 * @brief      复位输入法状态（对标 QInputMethod::reset；转发平台上下文）。
 * @param      self 目标对象；可为 NULL。
 */
void XInputMethod_reset(XInputMethod* self);

/**
 * @brief      提交当前组合中的词句（对标 QInputMethod::commit；转发平台上下文）。
 * @param      self 目标对象；可为 NULL。
 */
void XInputMethod_commit(XInputMethod* self);

/**
 * @brief      通知输入法组合词被点击（对标 QInputMethod::invokeAction）。
 * @param      self 目标对象；可为 NULL。
 * @param      action 动作类型。
 * @param      cursorPosition 点击处光标位置。
 */
void XInputMethod_invokeAction(XInputMethod* self, XInputMethodAction action,
                               int cursorPosition);

/* ==================== 信号（8 个，对标 QInputMethod 全部信号） ==================== */

/** @brief 光标矩形变化信号（对标 cursorRectangleChanged）。 */
void* XInputMethod_cursorRectangleChanged_signal(XInputMethod* self);

/** @brief 锚点矩形变化信号（对标 anchorRectangleChanged）。 */
void* XInputMethod_anchorRectangleChanged_signal(XInputMethod* self);

/** @brief 键盘矩形变化信号（对标 keyboardRectangleChanged）。 */
void* XInputMethod_keyboardRectangleChanged_signal(XInputMethod* self);

/** @brief 输入项裁剪矩形变化信号（对标 inputItemClipRectangleChanged）。 */
void* XInputMethod_inputItemClipRectangleChanged_signal(XInputMethod* self);

/** @brief 可见性变化信号（对标 visibleChanged）。 */
void* XInputMethod_visibleChanged_signal(XInputMethod* self);

/** @brief 动画状态变化信号（对标 animatingChanged）。 */
void* XInputMethod_animatingChanged_signal(XInputMethod* self);

/** @brief 区域语言变化信号（对标 localeChanged）。 */
void* XInputMethod_localeChanged_signal(XInputMethod* self);

/** @brief 输入方向变化信号（对标 inputDirectionChanged）。 */
void* XInputMethod_inputDirectionChanged_signal(XInputMethod* self,
        XInputMethodLayoutDirection newDirection);

#ifdef __cplusplus
}
#endif

#endif /* XINPUTMETHOD_ON */
#endif /* XINPUTMETHOD_H */
