#ifndef XNAMESPACE_H
#define XNAMESPACE_H
#ifdef __cplusplus
extern "C" {
#endif
	/**
	 * @brief 定时器唯一标识符。
	 */
typedef size_t XTimerId;
/**
 * @brief 时间持续量（Duration）。
 */
typedef int64_t XDuration;

typedef enum XTimerType {
	XTimerType_PreciseTimer,   ///< 精确定时器（高精度，高功耗）
	XTimerType_CoarseTimer,   ///< 粗略定时器（允许 ±5% 误差，节能）
	XTimerType_VeryCoarseTimer// 仅保持完整的秒级精度。
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
//套接字活动类型
typedef enum {
	XSocketAct_Invalid = 0,
	XSocketAct_Read = 1,
	XSocketAct_Write = 2,
	XSocketAct_ReadWrite = XSocketAct_Read | XSocketAct_Write,
	XSocketAct_Exception = 4
} XSocketActType;
#ifdef __cplusplus
}
#endif
#endif // 