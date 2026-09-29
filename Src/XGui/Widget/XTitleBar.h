/**
 * @file       XTitleBar.h
 * @brief      XTitleBar 标题栏控件类（对标桌面 WM 标题栏功能全集的
 *             可定制控件）。
 * @details    对标桌面 WM 标题栏功能全集；原 XWindowDecoration 绘制实现
 *             迁入为本类默认实现（单一事实源）：窗口标题（超宽省略）+
 *             窗口图标/系统菜单钮 + 最小化/最大化(还原)/关闭三键 + 活动
 *             窗口配色，绘制经样式系统 CC_TitleBar（调色板/标准图标/PM
 *             度量），与桌面由 WM 绘制的标题栏共用同一套风格开关。
 *             宿主顶层窗口 = XWidget_parentWidget：标题/图标/flags/
 *             windowState/焦点窗口活动判定全部取自宿主；宽度跟随宿主、
 *             高度由 XTitleBar_defaultHeight 决定，几何钉位与输入拦截归
 *             XWindowDecoration 装饰模块，本类只画。用户可继承本类覆写
 *             paintEvent（经 XWidget_paintEvent_base 分派）定制外观、
 *             覆写 hitTest（经 XTitleBar_hitTest_base 分派）定制命中，
 *             并经 XWidget_setTitleBarWidget 挂载到宿主窗口。
 * @note       本模块无独立开关，随 XWIDGET_ON/XWINDOW_ON/XSTYLE_ON/
 *             XWINDOWEVENT_ON 生效。本类不依赖 Win32、POSIX、Qt 或其他
 *             平台 API；绘制事件由 XWidget 事件体系转发。保护接口
 *             （buildOption/defaultHeight/活动子控件注入）声明见
 *             XTitleBar_Protected.h，调用方必须显式包含。
 * @author     XinYueC 团队
 */
#ifndef XTITLEBAR_H
#define XTITLEBAR_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "XGuiConfig.h"
#include "XWidget.h"

#if XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON

/* ==================== 虚函数表（新增槽位自 XWidget 之后追加） ==================== */

/**
 * @brief XTitleBar 虚函数表枚举。
 * @details 继承 XWidget 的全部槽位（含 EXWidget_PaintEvent 绘制槽，
 *          默认实现画 CC_TitleBar 条带，子类可重载定制外观）；本类只
 *          新增 HitTest 一个槽位，枚举值从 XCLASS_VTABLE_GET_SIZE(XWidget)
 *          开始，对标样式 hitTestComplexControl 的标题栏命中测试（按钮
 *          返回对应 XStyleSC_TitleBar* 子控件位，条内空白返回
 *          SC_TitleBarLabel/0）。槽位数值由 XClass 宏管理，子类只能在
 *          自身虚表中覆盖或追加。
 */
XCLASS_DEFINE_BEGING(XTitleBar)
XCLASS_DEFINE_ENUM(XTitleBar, HitTest) = XCLASS_VTABLE_GET_SIZE(XWidget), /**< 标题栏命中测试槽位（默认经样式 hitTestComplexControl）。 */
XCLASS_DEFINE_END(XTitleBar)

/* ==================== 结构体定义 ==================== */

/**
 * @brief      XTitleBar 标题栏控件对象。
 * @details    m_class 是第一个成员，因此该对象可向上转换为 XWidget 与
 *             XObject。本类无自有堆资源：宽度跟随宿主、高度由
 *             XTitleBar_defaultHeight 决定，几何钉位归装饰模块。所有
 *             成员均属于实现状态，调用方不得直接修改，应通过本文件与
 *             XTitleBar_Protected.h 声明的 API 访问。
 * @note       释放约定：XObject 派生控件的常规释放走异步
 *             XObject_deleteLater（事件循环下次处理时安全释放）；同步
 *             XTitleBar_delete_base 极少使用，仅供显式同步释放场景。
 */
typedef struct XTitleBar
{
    XWidget m_class;         /**< 基类成员；必须是第一个，由 XClass 管理，
                                  禁止手工修改。 */
    int m_activeSubControls; /**< 当前活动子控件位（XStyleSC_TitleBar*
                                  组合；0=无）：装饰模块按住/悬停按钮时
                                  注入（按住优先，armed?armed:hot 合并
                                  口径），默认实现组装选项时注入
                                  CC_TitleBar 的 activeSubControls 并按
                                  按下态高亮该钮。 */
    bool m_stripKick;        /**< 首帧装饰补拍已排标志：默认 paintEvent
                                  首次绘制后补排一次自身重绘，兜底真机
                                  惰性字体初始化竞态导致的首帧标题字形
                                  缺席（原 XWindowDecoration 绘制尾部
                                  行为迁入）。 */
    XTimerId m_releaseVerifyTimer; /**< 松手校验帧一次性定时器 id
                                  （XTIMER_INVALID_ID=未排程；实现状态，
                                  禁止直接修改）：装饰模块 RELEASE 分支经
                                  XTitleBar_armReleaseVerify 排程，到时
                                  自毁并对宿主顶层补一帧重绘（兜底拖拽
                                  改尺寸最终帧在框架下游——WM 回注/dde
                                  合成器等段——偶发搁浅时的可见停滞）。
                                  析构时若仍挂起由本类 Deinit 槽回收。 */
} XTitleBar;

/* ==================== 类初始化 / 构造与析构 ==================== */

/**
 * @brief      初始化 XTitleBar 类共享虚函数表并返回表指针。
 * @details    先继承 XWidget 全部槽位，再注册本类 HitTest 新槽位与
 *             PaintEvent/Deinit/Copy/Move 重载；返回指针具有静态生命
 *             周期，调用方不得释放或修改。
 * @return     XTitleBar 类的共享 XVtable 指针。
 */
XVtable* XTitleBar_class_init(void);

/**
 * @brief      初始化 XTitleBar 对象（栈上构造；挂载为宿主子控件后生效）。
 * @details    清零后初始化 XWidget 基类并设置 XTitleBar 虚表；parent 为
 *             宿主顶层控件（装饰模块挂载），flags 传 0 表示普通子控件
 *             （标题栏不得作为顶层窗口）。m_activeSubControls 置 0、
 *             m_stripKick 置 false、m_releaseVerifyTimer 置
 *             XTIMER_INVALID_ID（未排程）。调用方负责生命周期结束后
 *             的 XTitleBar_deinit_base。
 * @param      self   待初始化对象；不可为 NULL，且必须尚未初始化。
 * @param      parent 宿主控件借用指针；可为 NULL（此时默认实现不绘制，
 *                    挂载到宿主后才生效），函数不取得其所有权。
 * @param      flags  窗口标志；标题栏应传 0（子控件）。
 * @return     无返回值；self 不满足初始化前提时调用方不得继续使用对象。
 */
void XTitleBar_init(XTitleBar* self, XWidget* parent, XWidgetFlags flags);

/**
 * @brief      使用默认内存类型创建标题栏控件。
 * @param      parent 宿主控件借用指针；可为 NULL，宏不取得其所有权。
 * @param      flags  窗口标志；标题栏应传 0（子控件）。
 * @return     新建的已初始化对象指针；分配失败返回 NULL。成功返回的
 *             对象由调用方拥有，必须使用 XTitleBar_delete_base 释放。
 */
#define XTitleBar_create(parent, flags) \
    XTitleBar_create_ex(XCLASS_DEFAULT_MEMORY_TYPE, (parent), (flags))

/**
 * @brief      使用指定内存类型创建标题栏控件。
 * @param      memory 对象分配所使用的 XMemoryType；只影响对象分配与释放。
 * @param      parent 宿主控件借用指针；可为 NULL，函数不取得其所有权。
 * @param      flags  窗口标志；标题栏应传 0（子控件）。
 * @return     新建的已初始化对象指针；分配或初始化失败返回 NULL。成功
 *             返回的堆对象由调用方拥有，必须使用 XTitleBar_delete_base
 *             释放。
 */
XTitleBar* XTitleBar_create_ex(XMemoryType memory, XWidget* parent,
                               XWidgetFlags flags);

/**
 * @brief      反初始化 XTitleBar 对象并释放资源（栈/外部存储对象入口；
 *             宏映射 XClass 基类入口，经虚表 EXClass_Deinit 槽分派）。
 * @details    释放约定：XObject 派生控件的常规释放走异步
 *             XObject_deleteLater；同步 deinit_base 极少使用，仅供显式
 *             同步释放场景。必须与 XTitleBar_init 成对调用。
 * @param      self 待反初始化对象；NULL 或虚表未初始化时不执行任何操作。
 * @return     无返回值。
 */
#define XTitleBar_deinit_base(self) XClass_deinit_base((XClass*)(self))
/**
 * @brief      删除堆上的 XTitleBar 对象（先经虚表反初始化，再按登记分配
 *             器释放结构体内存；宏映射 XClass 基类入口）。
 * @details    释放约定：常规释放走异步 XObject_deleteLater；同步
 *             delete_base 极少使用，仅供显式同步释放场景（如无事件循环
 *             的测试环境）。只能配对 XTitleBar_create/create_ex 返回的
 *             堆对象，栈对象走 XTitleBar_deinit_base。
 * @param      self 待删除的堆对象；NULL 不执行任何操作。
 * @return     无返回值。
 */
#define XTitleBar_delete_base(self) XClass_delete_base((XClass*)(self))

/* ==================== 类型守卫（装饰路径动态类型校验） ==================== */

/**
 * @brief      动态判断控件是否为 XTitleBar 或其派生类实例。
 * @details    XWidget_setTitleBarWidget 是控件级通用槽，不限定条控件类
 *             型（任意控件可挂，XDockWidget 等消费方照单全收）；仅窗口
 *             装饰路径的类型化操作（命中测试虚槽 EXTitleBar_HitTest、
 *             活动子控件注入、CC_TitleBar 选项组装访问 m_activeSubCon
 *             trols 等本类布局）要求条派生自本类，否则虚槽越界读/结构
 *             体字段越界写。识别机制=虚表槽位指纹：比对 EXClass_Copy/
 *             EXClass_Move 两槽与 XTitleBar_class_init 注册的拷贝/移动
 *             实现——任何经 XVTABLE_INHERIT_XCLASS(XTitleBar) 派生的类
 *             都按槽复制继承这两槽，未继承本类虚表的类不可能持有这两个
 *             static 函数指针；同时校验 HitTest 槽非空，证明命中虚槽可
 *             安全分派。比对全程经 XVtable_at 越界安全读取，异类控件
 *             恒返回 false，无越界访问。
 * @param      candidate 待判定控件借用指针；可为 NULL。
 * @return     candidate 为 XTitleBar 或其派生实例返回 true；空指针、虚
 *             表未初始化或非本类体系控件返回 false。
 * @warning    派生类若重载 Copy 与 Move 两槽将无法被识别（保守返回
 *             false，装饰路径随之降级为仅摆位/纯子控件放行，属安全方
 *             向的误判，不会引入内存错误）。
 */
bool XTitleBar_isBar(const XWidget* candidate);

/* ==================== 虚函数调度入口 ==================== */

/**
 * @brief      通过当前虚表分派标题栏命中测试。
 * @details    默认实现组装 CC_TitleBar 选项后交样式
 *             hitTestComplexControl：按钮区域返回对应 XStyleSC_TitleBar*
 *             子控件位，条内空白返回 SC_TitleBarLabel/0，宿主不可装饰或
 *             选项组装失败返回 0。子类可重载 EXTitleBar_HitTest 槽位定
 *             制命中；重载实现中需要基类行为时用 XClass_Parent 访问父
 *             类槽位，不得递归调用本入口。
 * @param      self 目标标题栏控件；可为 NULL，NULL 或虚表未初始化时
 *                  返回 0。
 * @param      pos 命中点（本控件局部坐标，与宿主条带坐标一致——标题栏
 *                 钉在宿主 0,0）；可为 NULL，NULL 时返回 0。借用语义，
 *                 函数不取得其所有权。
 * @return     XStyleSC_TitleBar* 子控件位组合；未命中任何子控件返回 0。
 */
int XTitleBar_hitTest_base(XTitleBar* self, const XPoint* pos);

/* ==================== 功能函数 ==================== */

/**
 * @brief      武装一次「松手校验帧」：约 100ms 后对宿主顶层补一帧全窗
 *             重绘。
 * @details    拖拽改尺寸松手（RELEASE）兜底：框架重绘调度链与松手
 *             flush 语义本身同轮闭环，但最终帧在框架下游段（WM 几何
 *             回注、桌面合成器 present→屏等）仍可能偶发搁浅，下游无
 *             事件时只能等空闲周期定时器补发，用户观感即 1~2s 停滞。
 *             本接口在松手 flush 之后排一次性 CoarseTimer（默认
 *             100ms），到时停表并对宿主顶层 XWidget_update——内容未
 *             一致则产全窗 PAINT 补齐（帧耗毫秒级），已一致则屏幕零
 *             变化无可见副作用；无论搁浅发生在哪一段，可见停滞上限被
 *             压到约本延迟量级。幂等：已有校验帧未触发时重复调用不
 *             重排。排障回退：环境变量 XWD_RELEASE_VERIFY=0 禁用
 *             （默认启用，沿 XGUI_FLUSH_FULLFALLBACK 惯例）。
 * @param      self 目标标题栏控件；可为 NULL，NULL 或虚表未初始化时
 *                  不执行任何操作。宿主缺席（未挂载）时同样不排程。
 * @return     无返回值。
 * @note       定时器挂在条控件自身（XObject 定时器体系）；条控件析构
 *             时本类 Deinit 槽回收挂起定时器，无悬空触发。
 */
void XTitleBar_armReleaseVerify(XTitleBar* self);

#endif /* XWIDGET_ON && XWINDOW_ON && XSTYLE_ON && XWINDOWEVENT_ON */

#ifdef __cplusplus
}
#endif
#endif /* XTITLEBAR_H */
