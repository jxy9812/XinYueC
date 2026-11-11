/******************************************************************************
 * @file       XWidget_Protected.h
 * @brief      XWidget 基类保护接口（仅供子类与内部实现使用）。
 * @details    本文件集中声明 Qt QWidget 中属于 protected 的事件虚函数、
 *             尺寸提示/高度回调、绘制设备与内容缓存辅助接口，以及平台
 *             接入钩子；普通用户代码不应直接包含或调用。XWidget.h 只
 *             保留公开 API。
 ******************************************************************************/
#ifndef XWIDGET_PROTECTED_H
#define XWIDGET_PROTECTED_H

#if XGUI_ON
#ifdef __cplusplus
extern "C" {
#endif

#include "XWidget.h"
#include "XObject_Protected.h" /* 下方 XWidget_event_base 宏展开为 XObject_event_base（XObject 保护接口） */

#if XPAINTDEVICE_ON
typedef struct XPaintDevice XPaintDevice; /* 前向声明；完整定义见 XPaintDevice.h。 */
#endif

/* ==================== 图形效果内部接口（对标 QWidgetPrivate 的 effect 槽） ========== */

/**
 * @brief      把控件侧 graphicsEffect 槽摘空（仅当槽内确实是 effect）。
 * @details    供 XGraphicsEffect 的 Deinit 槽调用：效果对象被**直接删除**
 *             时反查源控件并断开关联（对标 Qt
 *             QGraphicsEffectSourcePrivate::detach 把
 *             `widget->d_func()->graphicsEffect` 置空；本机 Qt 6.8 实测
 *             `delete effect` 后 `w.graphicsEffect()` 返回 NULL）。
 *             命中时刷新不透明判定并标脏控件全幅（旧效果输出需被覆写）；
 *             未命中（控件从未挂该效果，或已换装别的效果）不做任何事。
 * @param      self   目标控件；可为 NULL。
 * @param      effect 正被销毁的效果对象；可为 NULL。
 * @return     命中并摘除返回 true；否则 false。
 */
bool XWidget_graphicsEffectDetach(XWidget* self, XGraphicsEffect* effect);

/* ==================== 尺寸提示/高度回调（对标 Qt protected 子类重载） ==================== */

/**
 * @brief      设置尺寸提示（布局子系统接入前的存储位；QWidget 子类通过
 *             override sizeHint() 提供，本实现以存储位等价提供）。
 * @param      self 目标控件；可为 NULL。
 * @param      hint 新尺寸提示；可为 NULL。
 * @return     无返回值；参数无效时不执行操作。
 */
void XWidget_setSizeHint(XWidget* self, const XSize* hint);
/**
 * @brief      设置最小尺寸提示（布局子系统接入前的存储位）。
 * @param      self 目标控件；可为 NULL。
 * @param      hint 新最小尺寸提示；可为 NULL。
 * @return     无返回值；参数无效时不执行操作。
 */
void XWidget_setMinimumSizeHint(XWidget* self, const XSize* hint);
/**
 * @brief      设置控件按宽度计算的高度回调。
 * @param      self 目标控件；可为 NULL。
 * @param      handler 回调函数；可为 NULL 以清除回调。
 * @param      userData 回调上下文指针；由调用方管理生命周期。
 * @return     无返回值。
 */
void XWidget_setHeightForWidthHandler(XWidget* self,
                                      XWidgetHeightForWidthHandler handler,
                                      void* userData);

/* ==================== 绘制设备与控件内容缓存（仅供 paintEvent 内使用） ==================== */

/**
 * @brief      返回绘制设备（对标 QBackingStore::paintDevice 的控件入口）。
 * @details    顶层控件返回后备存储内部 XImage；子控件返回顶层后备存储
 *             XImage（配合 XWidget_paintOffset 完成平移）。未显示或无
 *             后备存储返回 NULL。只能在后备存储 beginPaint/endPaint
 *             之间使用。
 */
XImage* XWidget_paintImage(const XWidget* self);
#if XPAINTDEVICE_ON
/**
 * @brief      返回控件绘制设备描述（对标 QWidget 作为 QPaintDevice 的
 *             度量入口：width/height/dpr/dpi 等）。
 * @param      self 目标控件指针。
 * @return     绘制设备借用指针；无效返回 NULL。
 */
XPaintDevice* XWidget_paintDevice(const XWidget* self);
#endif /* XPAINTDEVICE_ON */
/**
 * @brief      返回控件在顶层后备存储中的原点偏移（像素）。
 * @details    供 paintEvent 内 XPainter_translate 使用，把子控件本地坐标
 *             平移到后备存储坐标。
 */
XPoint XWidget_paintOffset(const XWidget* self);

/** @brief 返回应用当前持有键盘焦点的控件（全局；IME 事件路由用）。 */
XWidget* XWidget_appFocusWidget(void);
/**
 * @brief      控件内容绘制回调。
 * @param      self 目标控件。
 * @param      painter 已绑定到缓存图像或直接绘制目标的绘制器。
 * @param      userData 由调用方传入的上下文；可为 NULL。
 * @return     内容绘制成功返回 true。
 */
typedef bool (*XWidgetContentDrawProc)(XWidget* self, XPainter* painter,
                                       void* userData);
/**
 * @brief      把控件内容画到目标绘制器，并自动维护控件内容缓存。
 * @details    这是缓存机制的顶层入口：缓存可用时直接整块 blit；缓存置脏
 *             或尺寸不匹配时，基类负责 begin/mark，子类只管通过
 *             drawContent 把自身内容画到缓存坐标系（0,0 起点）。若离屏
 *             分配失败则回退为直接绘制目标绘制器。
 * @param      self 目标控件；可为 NULL。
 * @param      target 最终目标绘制器；不可为 NULL。
 * @param      x/y    目标绘制器中的内容位置（像素）。
 * @param      width/height 内容尺寸（像素）。
 * @param      drawContent 内容绘制回调，负责按内容坐标系绘制控件外观。
 * @param      userData 透传给 drawContent 的上下文；可为 NULL。
 * @return     成功绘制返回 true。
 */
bool XWidget_drawContentCached(XWidget* self, XPainter* target,
                               int x, int y, int width, int height,
                               XWidgetContentDrawProc drawContent,
                               void* userData);

/* ==================== 事件分派（对标 QWidget protected 事件虚函数） ==================== */

/**
 * @brief      控件事件总入口（对标 QWidget::event；继承自 XObject，宏复用）。
 * @details    按 XEventType 分派到 23 个事件虚函数；未识别事件回退
 *             XObject 默认 Event 实现。此入口属 protected 接口，只暴露给
 *             子类/内部实现使用，宏定义放在本保护头文件。
 * @param      self 接收事件的控件；可为 NULL。
 * @param      event 待分派事件；可为 NULL。
 * @return     事件已处理返回 true。
 */
#define XWidget_event_base(self, event)  XObject_event_base((XObject*)(self), (event))
/** @brief 绘制事件槽（对标 QWidget::paintEvent）。 */
void XWidget_paintEvent_base(XWidget* self, XEvent* event);
/** @brief 调整大小事件槽（对标 QWidget::resizeEvent）。 */
void XWidget_resizeEvent_base(XWidget* self, XEvent* event);
/** @brief 移动事件槽（对标 QWidget::moveEvent）。 */
void XWidget_moveEvent_base(XWidget* self, XEvent* event);
/** @brief 关闭事件槽（对标 QWidget::closeEvent；默认接受）。 */
void XWidget_closeEvent_base(XWidget* self, XEvent* event);
/** @brief 焦点进入事件槽（对标 QWidget::focusInEvent）。 */
void XWidget_focusInEvent_base(XWidget* self, XEvent* event);
/** @brief 焦点离开事件槽（对标 QWidget::focusOutEvent）。 */
void XWidget_focusOutEvent_base(XWidget* self, XEvent* event);
/** @brief 指针进入事件槽（对标 QWidget::enterEvent）。 */
void XWidget_enterEvent_base(XWidget* self, XEvent* event);
/** @brief 指针离开事件槽（对标 QWidget::leaveEvent）。 */
void XWidget_leaveEvent_base(XWidget* self, XEvent* event);
/** @brief 键盘按下事件槽（对标 QWidget::keyPressEvent）。 */
void XWidget_keyPressEvent_base(XWidget* self, XEvent* event);
/** @brief 键盘释放事件槽（对标 QWidget::keyReleaseEvent）。 */
void XWidget_keyReleaseEvent_base(XWidget* self, XEvent* event);
/** @brief 输入法组合/提交事件槽（对标 QWidget::inputMethodEvent）。 */
void XWidget_inputMethodEvent_base(XWidget* self, XEvent* event);
/** @brief 上下文菜单事件槽（对标 QWidget::contextMenuEvent）。 */
void XWidget_contextMenuEvent_base(XWidget* self, XEvent* event);
/** @brief 拖放进入事件槽（对标 QWidget::dragEnterEvent）。 */
void XWidget_dragEnterEvent_base(XWidget* self, XEvent* event);
/** @brief 拖放移动事件槽（对标 QWidget::dragMoveEvent）。 */
void XWidget_dragMoveEvent_base(XWidget* self, XEvent* event);
/** @brief 拖放离开事件槽（对标 QWidget::dragLeaveEvent）。 */
void XWidget_dragLeaveEvent_base(XWidget* self, XEvent* event);
/** @brief 放下事件槽（对标 QWidget::dropEvent）。 */
void XWidget_dropEvent_base(XWidget* self, XEvent* event);
/** @brief 鼠标按下事件槽（对标 QWidget::mousePressEvent）。 */
void XWidget_mousePressEvent_base(XWidget* self, XEvent* event);
/** @brief 鼠标释放事件槽（对标 QWidget::mouseReleaseEvent）。 */
void XWidget_mouseReleaseEvent_base(XWidget* self, XEvent* event);
/** @brief 鼠标双击事件槽（对标 QWidget::mouseDoubleClickEvent）。 */
void XWidget_mouseDoubleClickEvent_base(XWidget* self, XEvent* event);
/** @brief 鼠标移动事件槽（对标 QWidget::mouseMoveEvent）。 */
void XWidget_mouseMoveEvent_base(XWidget* self, XEvent* event);
/** @brief 滚轮事件槽（对标 QWidget::wheelEvent）。 */
void XWidget_wheelEvent_base(XWidget* self, XEvent* event);
/** @brief 显示事件槽（对标 QWidget::showEvent）。 */
void XWidget_showEvent_base(XWidget* self, XEvent* event);
/** @brief 隐藏事件槽（对标 QWidget::hideEvent）。 */
void XWidget_hideEvent_base(XWidget* self, XEvent* event);
/** @brief 属性/状态变更事件槽（对标 QWidget::changeEvent）。 */
void XWidget_changeEvent_base(XWidget* self, XEvent* event);

/* ==================== 焦点遍历与显隐扩展点（对标 QWidget protected 虚函数） ==================== */

/**
 * @brief      焦点遍历扩展槽（对标 QWidget::focusNextPrevChild 虚函数）。
 * @details    Qt 把焦点遍历的决定权交给所在顶层窗口，子类可覆写本槽接管
 *             遍历顺序（QLabel/QMenu/QDateTimeEdit/QAbstractItemView 均
 *             覆写）。默认实现把遍历决定权上交所在顶层窗口（沿父链到
 *             顶层后，以窗口当前焦点控件为起点续链）。子类覆写时须自行
 *             完成焦点迁移并返回是否成功切换。
 * @param      self 发起遍历的控件；可为 NULL。
 * @param      next true=向后（Tab）遍历；false=向前（Shift+Tab）遍历。
 * @return     成功切换焦点返回 true；否则返回 false。
 */
bool XWidget_focusNextPrevChild_base(XWidget* self, bool next);
/**
 * @brief      显隐扩展槽（对标 QWidget::setVisible 虚函数）。
 * @details    Qt 的 setVisible 是 public virtual，对话框/浮层类靠覆写它
 *             接管显隐时机（QDialog::setVisible 等）。默认实现走基类
 *             显隐逻辑（XWidget_setVisible 的全部既有语义）。子类覆写
 *             时应先调用基类实现再叠加自身行为。
 * @param      self 目标控件；可为 NULL。
 * @param      visible true=显示，false=隐藏。
 * @return     无返回值。
 */
void XWidget_setVisible_base(XWidget* self, bool visible);

/* ==================== 平台/测试接入钩子（内部） ==================== */

/**
 * @brief      顶层控件窗口几何变化钩子（XWidgetWindow 事件桥调用）。
 * @details    由平台 ConfigureNotify/GeometryChange 驱动，同步几何字段并
 *             派发 move/resize 事件。
 * @param      self 顶层控件；可为 NULL。
 * @param      geometry 平台报告的新几何；可为 NULL。
 * @param      oldSize 平台报告的旧尺寸；可为 NULL。
 */
void XWidget_applyWindowGeometry(XWidget* self, const XRect* geometry,
                                 const XSize* oldSize);
/**
 * @brief      顶层控件可见状态变化钩子（XWidgetWindow 事件桥调用）。
 * @param      self 顶层控件；可为 NULL。
 * @param      visible 新显式可见状态。
 */
void XWidget_applyWindowVisibility(XWidget* self, bool visible);
/**
 * @brief      顶层控件后备存储刷新入口：把脏区放到离屏缓冲并上屏。
 * @details    paintEvent 由平台/应用事件环触发后集中调用；子控件脏区
 *             已折算到顶层坐标。无后备存储时自动按控件尺寸创建。
 * @param      self 顶层控件；可为 NULL。
 * @param      region 待刷新的顶层区域；可为 NULL 表示整窗。
 */
void XWidget_flushBackingStore(XWidget* self, const XRegion* region);

/**
 * @brief      使控件内容缓存失效；内容、字体、调色板、几何或 visibility
 *             变化时由 XWidget update 自动置脏，手工重绘后也可显式调用。
 * @details    控件内容缓存是可选的离屏 XImage，配合
 *             XWidget_beginContentCache/XWidget_markContentCacheReady 使用：
 *             高频绘制但内容只有低频变化（如性能悬浮层）时，先一次性把
 *             自身内容画进缓存，后续帧只做整块 blit，避免逐字形重排光栅化。
 *             该机制属于 XWidget 基类能力，XLabel/XPerformanceOverlay 等
 *             子类均可复用。未启用缓存的控件保持 NULL，不影响普通绘制。
 */
void XWidget_invalidateContentCache(XWidget* self);
/**
 * @brief      判断控件内容缓存当前是否可用于指定尺寸。
 * @details    返回 true 表示缓存已存在、未置脏且尺寸匹配；可直接
 *             blit 到目标绘制设备，无需重建。
 * @param      self 目标控件；可为 NULL。
 * @param      width  缓存所需宽度（像素）。
 * @param      height 缓存所需高度（像素）。
 */
bool XWidget_contentCacheUsable(const XWidget* self, int width, int height);
/**
 * @brief      获取控件内容缓存图像（借用指针；不会分配）。
 * @details    仅在 XWidget_contentCacheUsable 通过后用于 blit；缓存可能为
 *             NULL 或尺寸过时。返回指针生命周期同 self，调用方不得释放。
 */
XImage* XWidget_contentCacheImage(const XWidget* self);
/**
 * @brief      获取或重建控件内容缓存图像；供控件绘制自身内容使用。
 * @details    若缓存缺失或尺寸不匹配则重建为 ARGB32；本函数不改变脏标记，
 *             调用方绘制完成后必须调用 XWidget_markContentCacheReady。
 * @param      width  缓存宽度（像素）。
 * @param      height 缓存高度（像素）。
 * @return     可直接绑定 XPainter 的缓存图像（借用）；分配失败返回 NULL。
 */
XImage* XWidget_beginContentCacheFormat(XWidget* self, int width, int height,
                                        XImageFormat format);
/**
 * @brief      标记控件内容缓存已与当前控件外观同步。
 * @details    通常在控件把自身内容画入 XWidget_beginContentCache 返回的
 *             XImage 后调用；此后 XWidget_contentCacheUsable 才会返回 true。
 */
void XWidget_markContentCacheReady(XWidget* self);

/* ==================== 界外按下重放（悬浮弹层点外语义，内部） ==================== */

/**
 * @brief      把一次界外按下转发到全局坐标下的顶层窗口（内部）。
 * @details    全局坐标解析顶层（XGuiApplication_topLevelAt→桥窗口反查
 *             控件）后经 XWindowSystemInterface_handleMouseEvent_ex 完整
 *             负载注入 PRESS——从桥接窗口事件总入口走完整派发管线（装
 *             饰拦截→childAt→真实靶→notifyPress 汇聚点），与原生平台按
 *             下同路。调用方必须已处于无抓取态（先 closePopup 成对归还
 *             双抓取），单次派发无回环（界外坐标不可能再落回已收层键
 *             盘）。对标 Qt qwidgetwindow.cpp 关层后向光标下控件重放。
 * @param      src 原始按下事件（取按键/按键集合/修饰键/时间戳；借用）。
 * @param      global 屏幕全局坐标（按下点）。
 * @return     已派发返回 true；参数无效、无可重放顶层或窗口/应用层裁
 *             剪（对应宏门关闭）返回 false。
 */
bool XWidget_replayPressAtGlobal(const XMouseEvent* src, const XPoint* global);

/* ==================== 跨顶层坐标换算（2026-10-18 新增） ==================== */

/**
 * @brief      跨顶层窗口换算坐标（一步到位，替代 mapToGlobal→
 *             mapFromGlobal 复合往返）。
 * @details    两窗旋转角不同时，复合往返会把起点窗的旋转信息抵消两次
 *             （mapToGlobal 做 R_from，mapFromGlobal 再对目标窗做
 *             R⁻¹_to，中间还隔原点加减），旋转信息彻底失真——鼠标抓取
 *             跨顶层改道、触摸抓取跨顶层改道、右键菜单全局坐标三处必须
 *             走本函数。实现：R_fromTop(L) → -origin_from → +origin_to
 *             → R⁻¹_toTop → -offset_to，全程在设备全局坐标域走。
 *             rot==0 时全链退化为原点加减，逐位不变。
 * @param      fromTop 源顶层控件（借用）；可为 NULL（跳过源侧换算）。
 * @param      toTop   目标顶层控件（借用）；可为 NULL（跳过目标侧换算）。
 * @param      fromLocalLogical 源控件本地**逻辑**坐标；可为 NULL。
 * @return      目标控件本地逻辑坐标；入参非法返回 (0,0)。
 */
XPoint XWidget_mapAcrossTopLevel(XWidget* fromTop, XWidget* toTop,
                                 const XPoint* fromLocalLogical);

/* ==================== 待激活焦点靶兑现（对标 Qt focus_child，内部） ==================== */

/**
 * @brief      窗口激活上报路径兑现其待激活焦点靶（内部）。
 * @details    QWidget::setFocus 在窗口未激活时不移全局焦点，仅把焦点诉求
 *             记在窗口上（focus_child；qwidget.cpp:6668-6670 else 分支
 *             updateFocusChild），窗口激活后由激活路径真正移焦。X 侧由
 *             XWidget_setFocus_2 的未激活闸登记（每窗口单靶），本入口在
 *             XWindow_reportWindowStateChanged 检测到 Active 位由无到有
 *             时按平台窗口反查登记表兑现（摘账后回放 setFocus，靶资格由
 *             setFocus 入口守卫复验）。
 * @param      window 激活上报的平台窗口（借用）；NULL 不操作。
 * @return     无返回值。
 */
void XWidget_pendingFocusRedeem(XWindow* window);

/* ==================== 应用模态（对标 QApplication activeModalWidget） ==================== */

/**
 * @brief      设置当前应用模态控件（对标 QApplication::setActiveModalWidget
 *             的内部登记；供 XDialog/open-exec 路径与模态输入拦截使用）。
 * @param      widget 模态控件（借用）；NULL 清除。
 */
void XWidget_setApplicationModalWidget(XWidget* widget);
/** @brief      返回当前应用模态控件（借用）；无则 NULL。 */
XWidget* XWidget_applicationModalWidget(void);

/**
 * @brief 递归摘除子树在事件层的输入侧静态登记（焦点/鼠标抓取/隐式按压靶/
 *        触点抓取表/键盘抓取），**不改变任何控件的显隐状态**。
 * @details 对标 Qt 6.8.3 QWidget::destroy 经 QWidgetPrivate::
 *          deactivateWidgetCleanup 清 qt_button_down 与各抓取登记的口径
 *          （qwidget.cpp:2331-2334）。QWidget::setVisible(false) 的隐藏
 *          分支只摘除**指向自身**的登记（父容器隐藏不递归清子控件），
 *          故对话框"子树即将延迟析构"的场景需要本递归版本。
 *          **不得**用"逐控件 setVisible(false)"代替：那会把每个子控件
 *          永久标记为显式隐藏（WA_WState_Hidden），对话框复用（再次 open）
 *          时 showChildren 会跳过它们，呈现空面板——Qt 的 showChildren
 *          同理（qwidget.cpp:8189 跳过 WA_WState_Hidden）。
 * @param self 起始控件；可为 NULL（不操作）。自身也会被清理。
 * @note 仅供框架内部与对话框收口路径使用，不对外公开。
 */
void XWidget_deactivateInputRegistrations(XWidget* self);

/* ==================== 布局失效投递（对标 Qt postEvent LayoutRequest） ==================== */

/**
 * @brief      向控件投递 LayoutRequest（队列事件，压缩合并同接收者）。
 * @details    对标 Qt `QCoreApplication::postEvent(parent,
 *             QEvent::LayoutRequest)`（qwidget.cpp:10527
 *             updateGeometry_helper 无布局父级分支 / qlayout.cpp:1063
 *             activate 尾部 mw->updateGeometry() 的同效段）：接收者在事件
 *             分发中 activate 自身布局；无布局接收者沿父链上抛（视口转发
 *             同位，QAbstractScrollAreaFilter→QScrollArea::event）。
 * @param      receiver 接收控件；NULL 不操作。
 * @note       仅供框架内部（XLayout_activate 尾部、显隐联动收口）使用。
 * @note       压缩语义：走 `XCoreApplication_postEvent`，由 Qt 6.8 口径的
 *             `compressEvent` 在**入队前**合并同接收者的重复纯通知事件
 *             （同一接收者至多 1 个在飞）；不使用无锁 tryPostEvent（其
 *             1024 槽环在长同步阶段会满并丢事件）。**不要**在接收者上加
 *             「已投递」占位位——事件被丢弃时占位位不会复位，该控件此后
 *             永久收不到布局请求（2026-10-31 实测：主窗标题栏/状态栏/
 *             悬浮层布局停摆）。
 */
void XWidget_postLayoutRequest(XWidget* receiver);

/* ==================== 接管布局登记表（setLayout_ref 所有权承载，内部） ==================== */

#if XLAYOUT_ON
/**
 * @brief      按布局反查摘除 _ref 接管登记（XLayout 摘挂/析构路径接线用）。
 * @details    XWidget_setLayout_ref 把接管关系登记进 (控件,布局) 表，但布局
 *             被绕过 setLayout 的路径改挂/摘挂/销毁时（XLayout_linkItem/
 *             attachWidget/detachWidget/VXLayout_deinit），登记不会自动
 *             摘除——残项持已释放布局指针，地址复用后控件析构的 TakeFor
 *             返回值可与新挂接布局撞等式 → XClassDelete 误删借用布局
 *             （双删）；或析构 TakeFor 落空 → 接管布局泄漏。本入口供
 *             XLayout 侧在清宿主 m_layout 前同步摘账，使所有权随挂接
 *             全路径同步（对标 Qt qwidget.cpp:10284/10322
 *             `l->setParent(this)` 的所属权口径）。
 * @param      layout 被接管的布局；NULL 不操作。
 * @return     摘账前登记该布局的宿主控件（借用）；布局本无登记返回 NULL。
 */
XWidget* XWidget_ownedLayoutUnregister(XLayout* layout);
/**
 * @brief      接管登记随布局对象移动转移（from 名下账目改记到 to 名下）。
 * @details    VXLayout_move 把布局内容自 other 壳转入 self 且宿主挂接随之
 *             改指 self 时调用：other 壳即将销毁，其名下登记必须改绑承载
 *             内容的活对象，否则宿主析构 TakeFor 携已销毁的壳 → 残账
 *             悬垂/接管布局泄漏。from 本无登记（借用布局）时不产生新账，
 *             借用属性保留。
 * @param      from 移出内容的布局壳；NULL 不操作。
 * @param      to 承接内容的布局；NULL 不操作。
 * @return     无返回值。
 */
void XWidget_ownedLayoutTransfer(XLayout* from, XLayout* to);
#endif /* XLAYOUT_ON */

/* ==================== 失效期遮挡裁剪挂起（浮层子控件擦除链） ==================== */

/**
 * @brief      挂起失效期遮挡裁剪（addDirtyRegionEx 的
 *             opaqueChildren/opaqueSiblings 扣除），配对 resume 恢复。
 * @details    浮层子控件（落点指示器等）移位/显隐时旧位擦除走 parent
 *             update，会被 parent 的不透明子控件整块扣空 ⇒ 无人重画、
 *             旧位像素永留（真机指示器换位残影）。调用方须保证挂起区
 *             间极短（包住一次几何变更/差带重绘即可）。可嵌套（计数）。
 * @note       仅供框架内部（XMainWindow 落点指示器链）使用。
 */
void XWidget_suspendDirtyOcclusion(void);
void XWidget_resumeDirtyOcclusion(void);

#ifdef __cplusplus
}
#endif

#endif /* XGUI_ON */

#endif /* XWIDGET_PROTECTED_H */
