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
#ifdef __cplusplus
extern "C" {
#endif

#include "XWidget.h"

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
XImage* XWidget_paintDevice(const XWidget* self);
/**
 * @brief      返回控件在顶层后备存储中的原点偏移（像素）。
 * @details    供 paintEvent 内 XPainter_translate 使用，把子控件本地坐标
 *             平移到后备存储坐标。
 */
XPoint XWidget_paintOffset(const XWidget* self);
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
 * @brief      控件事件总入口（对标 QWidget::event）。
 * @details    按 XEventType 分派到 18 个事件虚函数；未识别事件回退
 *             XObject 默认 Event 实现。
 * @param      self 接收事件的控件；可为 NULL。
 * @param      event 待分派事件；可为 NULL。
 * @return     事件已处理返回 true。
 */
bool XWidget_event_base(XWidget* self, XEvent* event);
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
XImage* XWidget_beginContentCache(XWidget* self, int width, int height);
/**
 * @brief      标记控件内容缓存已与当前控件外观同步。
 * @details    通常在控件把自身内容画入 XWidget_beginContentCache 返回的
 *             XImage 后调用；此后 XWidget_contentCacheUsable 才会返回 true。
 */
void XWidget_markContentCacheReady(XWidget* self);

#ifdef __cplusplus
}
#endif
#endif /* XWIDGET_PROTECTED_H */
