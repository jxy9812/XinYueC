/**
 * @file       XVfdLink_Protected.h
 * @brief      总线链路基类内部接口（仅供子类与子系统内部使用，不对外公开）。
 * @details    目前子类（XVfdModbusLink）经公开 API 即可完成提交与健康回报；
 *             本文件预留子类对计划表与健康表的直接访问，随 CAN/Raw 链路落地扩充。
 */
#ifndef XVFDLINK_PROTECTED_H
#define XVFDLINK_PROTECTED_H

#include "XVfdLink.h"

#if XVFD_ON
#if XVFD_LINK_ON

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 获取链路内嵌计划表（子类直接排程用；借用，禁释放）。
 * @param link 链路实例（非 NULL）
 * @return 计划表指针
 */
XVfdPollScheduler* XVfdLink_scheduler(XVfdLink* link);

#ifdef __cplusplus
}
#endif

#endif /* XVFD_LINK_ON */
#endif /* XVFD_ON */
#endif /* XVFDLINK_PROTECTED_H */
