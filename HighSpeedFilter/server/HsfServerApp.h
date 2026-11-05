
/**
 * @file       HsfServerApp.h
 * @brief      服务端 XLink 接入与业务路由（登录鉴权/命令分发/遥测推送）。
 * @details    XLink 服务端装配：监听 TCP、启用内建挑战应答登录鉴权
 *             （缺省账户 admin/admin=只读、root/root=可改，落盘为
 *             <数据目录>/hsf_users.json 账户库），命令分发按角色门禁
 *             （控制类命令仅 XLinkAuthRole_Admin），周期推送变频器与
 *             模拟量遥测（BSON，见 XhsfLinkProto.h 消息面）。底层 IO
 *             由 HsfServerIo 提供（XIoChannelGroup + XModbusHub）。
 *             单线程事件循环口径：start/stop 须在 XCoreApplication
 *             事件循环内调用。
 */
#ifndef HSFSERVERAPP_H
#define HSFSERVERAPP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "CXinYueConfig.h"
#include "XhsfLinkProto.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 启动 XLink 服务端（监听 + 鉴权 + 路由 + 遥测推送）。
 * @param port 监听端口；0 取 HSF_XLINK_DEFAULT_PORT。
 * @return 监听成功返回 true；失败返回 false（已打日志）。
 * @note  幂等保护：已启动时先停旧实例再重启（端口变更用）。
 */
bool HsfServerApp_start(uint16_t port);

/**
 * @brief 停止服务端（关监听、回收全部会话；幂等）。
 */
void HsfServerApp_stop(void);

/**
 * @brief 当前在线会话数（诊断用）。
 */
int HsfServerApp_sessionCount(void);

/**
 * @brief 读取服务端运行模式。
 * @return 当前模式（手动/自动）。
 * @note  自动模式下全部 XLink 控制命令被拒绝（遥测/快照不受影响）。
 */
HsfRunMode HsfServerApp_runMode(void);

/**
 * @brief 设置服务端运行模式（落盘持久化 + 同步设备控制闸）。
 * @param mode 目标模式；非手动/自动的值被忽略。
 * @note  自动模式同时关闭已装配变频器的控制闸（双保险）。
 */
void HsfServerApp_setRunMode(HsfRunMode mode);

/**
 * @brief 通知"通道名已变更"：下一拍主动下发一帧只含名字的快照。
 * @note  通道名是部署参数（HsfServerConfig.h 的宏），运行期通常不变；
 *        本接口给"改了宏重新部署后不重启客户端"以及将来做热改名预留通路。
 *        下发的帧带 full=true 且只含 count + 全部名字键，客户端按
 *        "有名字键就更新名字"处理，不重建点表。
 */
void HsfServerApp_notifyChannelNamesChanged(void);

#ifdef __cplusplus
}
#endif
#endif /* HSFSERVERAPP_H */
