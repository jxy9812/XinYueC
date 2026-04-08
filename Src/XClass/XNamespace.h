#ifndef XNAMESPACE_H
#define XNAMESPACE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef enum XTimerType {
	XCoarseTimer,   ///< 粗略定时器（允许 ±5% 误差，节能）
	XPreciseTimer   ///< 精确定时器（高精度，高功耗）
} XTimerType;
/**
 * @brief 查找子对象的选项（对应 Qt::FindChildOption）
 *
 * 用于控制 QObject::findChild() / findChildren() 的查找行为。
 */
typedef enum XFindChildOption {
	XFindDirectChildrenOnly = 0x0,  /**< 仅查找直接子对象（不递归） */
	XFindChildrenRecursively = 0x1  /**< 递归查找所有后代子对象（默认行为） */
}XFindChildOption;

#ifdef __cplusplus
}
#endif
#endif // 